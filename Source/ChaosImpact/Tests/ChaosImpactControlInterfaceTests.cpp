#if WITH_DEV_AUTOMATION_TESTS
#include "ChaosImpactSettings.h"
#include "ChaosImpactSettingsScreen.h"
#include "ChaosImpactMenuWidget.h"
#include "ChaosImpactPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	class FControlInterfaceCommand : public IAutomationLatentCommand
	{
	public:
		explicit FControlInterfaceCommand(FAutomationTestBase* InTest) : Test(InTest) {}
		bool Update() override
		{
			using namespace ChaosImpactSettings;
			using Screen = UChaosImpactSettingsScreen;
			AChaosImpactPlayerController* PC = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{ if (Context.World() && Context.World()->IsGameWorld()) { PC = Cast<AChaosImpactPlayerController>(Context.World()->GetFirstPlayerController()); } }
			UChaosImpactMenuWidget* Menu = PC ? PC->GetMenuWidget() : nullptr;
			Screen* Settings = Menu ? Menu->GetSettingsScreen() : nullptr;
			const double Now = FPlatformTime::Seconds();
			if (Now - Started > 90) { Test->AddError(FString::Printf(TEXT("Control interface stalled at %d"),Stage)); Cleanup(PC); return true; }
			if (!Settings || Now < NextAt) { return false; }
			const auto Next = [this,Now](double Delay) { ++Stage; NextAt = Now + Delay; return false; };
			const auto Shot = [](const TCHAR* Name)
			{ FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("ControlInterfaceQA"),Name),true,false); };
			switch (Stage)
			{
			case 0:
				if (!PC->IsGameplayActive()) { return false; }
				Profile = AddProfile(TEXT("QA") + FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(6));
				if (Profile <= 0) { Test->AddError(TEXT("No spare test profile")); return true; }
				OriginalBall = PC->GetTrainingSummonBallType();
				PC->TogglePauseMenu(); PC->OpenSettings(); Settings->SetTab(Screen::ETab::Controls); Settings->EditControls(Profile,EChaosImpactDevice::KeyboardMouse);
				Test->TestTrue(TEXT("Controls has a visible diagram picker entry"),Settings->FindRow(Screen::RowControlPreview) == 0);
				Settings->SelectRow(Settings->FindRow(Screen::RowControlPreview)); Settings->Activate();
				Test->TestTrue(TEXT("Picker opens"),Settings->IsControlStylePickerOpen());
				Settings->HandleMouseMove(FVector2D(300,672));
				Test->TestTrue(TEXT("Hover previews hold mouse controls"),Settings->GetPreviewControlScheme() == EChaosImpactControlScheme::MouseHold);
				Test->TestTrue(TEXT("Preview does not change saved controls"),GetControlScheme(Profile,EChaosImpactDevice::KeyboardMouse) == EChaosImpactControlScheme::Normal);
				return Next(0.7);
			case 1:
				if (!Settings->IsControlVideoPlaying() || Settings->GetControlVideoTime() < 0.1) { return false; }
				Test->TestTrue(TEXT("Recorded mouse hold movie plays even though settings pause the world"),PC->GetWorld()->IsPaused());
				Shot(TEXT("01-MouseHoldPicker.png")); return Next(0.5);
			case 2:
				Settings->HandleMouseDown(FVector2D(1300,835),EKeys::LeftMouseButton);
				Test->TestTrue(TEXT("Apply saves the previewed style"),GetControlScheme(Profile,EChaosImpactDevice::KeyboardMouse) == EChaosImpactControlScheme::MouseHold);
				Test->TestFalse(TEXT("Apply closes only the picker"),Settings->IsControlStylePickerOpen());
				Settings->OpenControlStylePicker(); Settings->MoveSelection(-1);
				Settings->HandleMouseDown(FVector2D(200,835),EKeys::LeftMouseButton);
				Test->TestTrue(TEXT("Back cancels the preview without changing controls"),GetControlScheme(Profile,EChaosImpactDevice::KeyboardMouse) == EChaosImpactControlScheme::MouseHold);
				Settings->EditControls(Profile,EChaosImpactDevice::Switch); Settings->OpenControlStylePicker(); Settings->MoveSelection(1);
				Test->TestTrue(TEXT("Switch preview can select simple"),Settings->GetPreviewControlScheme() == EChaosImpactControlScheme::Simple);
				Settings->MoveSelection(1);
				Test->TestTrue(TEXT("Pad picker cycles only the two usable styles"),Settings->GetPreviewControlScheme() == EChaosImpactControlScheme::Normal);
				Settings->MoveSelection(1); return Next(0.7);
			case 3:
				if (!Settings->IsControlVideoPlaying() || Settings->GetControlVideoTime() < 0.1) { return false; }
				Test->TestTrue(TEXT("Recorded controller movie plays while settings are paused"),PC->GetWorld()->IsPaused());
				Shot(TEXT("02-SwitchSimplePicker.png")); return Next(0.5);
			case 4:
				Settings->Activate();
				Test->TestTrue(TEXT("Controller confirm applies simple without affecting keyboard"),GetControlScheme(Profile,EChaosImpactDevice::Switch) == EChaosImpactControlScheme::Simple
					&& GetControlScheme(Profile,EChaosImpactDevice::KeyboardMouse) == EChaosImpactControlScheme::MouseHold);
				PC->CloseSettings(); PC->ResumeGameplay(); PC->ToggleTrainingOverlay();
				Test->TestTrue(TEXT("Live training menu opens"),PC->IsTrainingOverlayOpen());
				WorldTime = PC->GetWorld()->GetTimeSeconds();
				Menu->OpenTrainingBallPicker();
				Test->TestEqual(TEXT("All twelve balls plus back appear together"),Menu->GetEntryCount(),ChaosImpactBallTypes::Count + 1);
				return Next(0.7);
			case 5:
				Test->TestTrue(TEXT("The ball list does not pause world time"),PC->GetWorld()->GetTimeSeconds() > WorldTime);
				Shot(TEXT("03-TrainingBallList.png")); return Next(0.5);
			case 6:
				// Select every type through the same menu navigation and confirm path used by mouse/pad input.
				for (int32 Type = 0; Type < ChaosImpactBallTypes::Count; ++Type)
				{
					Menu->OpenTrainingBallPicker();
					for (int32 Move = 0; Move < ChaosImpactBallTypes::Count + 1 && Menu->GetSelectedIndex() != Type; ++Move) { Menu->Navigate(EKeys::Tab); }
					Menu->ConfirmSelection();
					Test->TestEqual(TEXT("Selected ball type is applied directly"),static_cast<int32>(PC->GetTrainingSummonBallType()),Type);
					Test->TestFalse(TEXT("Picking a ball returns to training options"),Menu->IsTrainingBallPickerOpen());
					Test->TestEqual(TEXT("Selection returns to the list button"),Menu->GetSelectedIndex(),4);
				}
				Menu->OpenTrainingBallPicker(); Menu->Navigate(EKeys::Gamepad_DPad_Left);
				Test->TestEqual(TEXT("Grid left stays on its row"),Menu->GetSelectedIndex(),10);
				Menu->Navigate(EKeys::Gamepad_DPad_Up);
				Test->TestEqual(TEXT("Grid up moves one row"),Menu->GetSelectedIndex(),7);
				Menu->GoBack();
				Test->TestTrue(TEXT("Backing out keeps the selected ball"),PC->GetTrainingSummonBallType() == EChaosImpactBallType::Drive);
				Cleanup(PC); return true;
			default: Cleanup(PC); return true;
			}
		}
	private:
		void Cleanup(AChaosImpactPlayerController* PC)
		{
			if (PC)
			{
				if (PC->GetCurrentScreen() == EChaosImpactScreen::Settings) { PC->CloseSettings(); }
				if (PC->GetCurrentScreen() == EChaosImpactScreen::Pause) { PC->ResumeGameplay(); }
				if (Profile > 0) { PC->SetTrainingSummonBallType(OriginalBall); }
				if (PC->IsTrainingOverlayOpen()) { PC->CloseTrainingOverlay(); }
			}
			if (Profile > 0) { ChaosImpactSettings::RemoveProfile(Profile); Profile = -1; }
		}
		FAutomationTestBase* Test;
		int32 Stage = 0, Profile = -1;
		double Started = FPlatformTime::Seconds(), NextAt = 0;
		float WorldTime = 0;
		EChaosImpactBallType OriginalBall = EChaosImpactBallType::Normal;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactControlInterfaceTest,"ChaosImpact.Controls.Interface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FChaosImpactControlInterfaceTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FControlInterfaceCommand(this));
	return true;
}
#endif
