#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactMenuWidget.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactSettings.h"
#include "ChaosImpactSettingsScreen.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	using FSaveScreen = UChaosImpactSettingsScreen;

	AChaosImpactPlayerController* FindSaveTestController()
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World() && Context.World()->IsGameWorld())
			{
				if (auto* PC = Cast<AChaosImpactPlayerController>(Context.World()->GetFirstPlayerController()))
				{
					return PC;
				}
			}
		}
		return nullptr;
	}

	void PressSaveKey(const FKey Key)
	{
		FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0));
		FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0));
	}

	/**
	 * 保存 on the settings screen (title world, mode select): a change shows at once but is unsaved; もどる then asks;
	 * キャンセル stays; 保存しないでもどる puts it back; S saves (no question after); a key binding changed and the screen
	 * closed from outside is put back too; the question's answers move with the arrows and Escape cancels. A picture of the
	 * question goes to Saved/SettingsQA/09-AskSave.png. The volume is put back afterwards.
	 */
	class FSettingsSaveCommand : public IAutomationLatentCommand
	{
	public:
		explicit FSettingsSaveCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual ~FSettingsSaveCommand() override
		{
			if (OriginalVolume >= 0)
			{
				ChaosImpactSettings::SetMasterVolume(OriginalVolume);
			}
		}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - StartedAt > 60.0)
			{
				Test->AddError(FString::Printf(TEXT("Stuck at stage %d."), Stage));
				return true;
			}
			if (Now < NextAt)
			{
				return false;
			}
			AChaosImpactPlayerController* PC = FindSaveTestController();
			UChaosImpactMenuWidget* Menu = PC ? PC->GetMenuWidget() : nullptr;
			FSaveScreen* Settings = Menu ? Menu->GetSettingsScreen() : nullptr;
			if (!PC || !Menu || !Settings)
			{
				return false;
			}
			const auto Next = [this, Now](const double Seconds)
			{
				++Stage;
				NextAt = Now + Seconds;
				return false;
			};
			const auto Open = [PC, Settings]()
			{
				PC->ShowMenuScreen(EChaosImpactScreen::ModeSelect);
				PC->OpenSettings();
				Settings->SetTab(FSaveScreen::ETab::Other);
				Settings->SelectRow(Settings->FindRow(FSaveScreen::RowVolume));
				// (The cursor back in the tab column, the volume row still picked: Escape there is もどる.)
				Settings->BackToTabs();
			};
			using namespace ChaosImpactSettings;
			switch (Stage)
			{
			case 0:
				if (Now - StartedAt < 3.0)
				{
					return false;
				}
				OriginalVolume = GetMasterVolume();
				// (Where three steps down are possible.)
				SetMasterVolume(OriginalVolume < 40 ? 50 : OriginalVolume);
				StartVolume = GetMasterVolume();
				Open();
				Test->TestTrue(TEXT("Settings open"), Settings->IsOpen());
				Test->TestFalse(TEXT("Nothing unsaved on opening"), Settings->HasUnsavedChanges());
				// A change: it shows at once, and is not saved.
				Settings->Adjust(-1);
				Test->TestEqual(TEXT("The volume changes at once"), GetMasterVolume(), StartVolume / 10 * 10 - 10);
				Test->TestTrue(TEXT("…and is unsaved"), Settings->HasUnsavedChanges());
				// もどる asks; キャンセル stays with the change.
				PressSaveKey(EKeys::Escape);
				Test->TestTrue(TEXT("Leaving with a change asks whether to save"), Settings->IsAskingToSave() && Settings->IsOpen());
				Test->TestTrue(TEXT("The question starts on 保存してもどる"), Settings->GetSaveAnswerChoice() == FSaveScreen::ESaveAnswer::SaveAndLeave);
				PressSaveKey(EKeys::Right);
				Test->TestTrue(TEXT("Right moves to 保存しないでもどる"), Settings->GetSaveAnswerChoice() == FSaveScreen::ESaveAnswer::LeaveWithoutSaving);
				return Next(0.6);
			case 1:
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SettingsQA"), TEXT("09-AskSave.png")), true, false);
				return Next(0.6);
			case 2:
				PressSaveKey(EKeys::Escape);
				Test->TestFalse(TEXT("Escape on the question cancels it"), Settings->IsAskingToSave());
				Test->TestTrue(TEXT("…staying in the settings"), Settings->IsOpen() && PC->GetCurrentScreen() == EChaosImpactScreen::Settings);
				Test->TestTrue(TEXT("…with the change still there"), Settings->HasUnsavedChanges() && GetMasterVolume() != StartVolume);
				// 保存しないでもどる: back as it was.
				PressSaveKey(EKeys::Escape);
				PressSaveKey(EKeys::Right);
				PressSaveKey(EKeys::Enter);
				Test->TestFalse(TEXT("保存しないでもどる leaves"), Settings->IsOpen());
				Test->TestTrue(TEXT("…to the mode select"), PC->GetCurrentScreen() == EChaosImpactScreen::ModeSelect);
				Test->TestEqual(TEXT("…and the volume is as it was"), GetMasterVolume(), StartVolume);
				// Saved with S: kept, and もどる no longer asks.
				Open();
				Settings->Adjust(-1);
				Saved = GetMasterVolume();
				PressSaveKey(EKeys::S);
				Test->TestFalse(TEXT("S saves"), Settings->HasUnsavedChanges());
				PressSaveKey(EKeys::Escape);
				Test->TestFalse(TEXT("Saved: もどる leaves without asking"), Settings->IsOpen() || Settings->IsAskingToSave());
				Test->TestEqual(TEXT("…keeping the saved volume"), GetMasterVolume(), Saved);
				// 保存してもどる from the question.
				Open();
				Settings->Adjust(-1);
				Saved = GetMasterVolume();
				PressSaveKey(EKeys::Escape);
				PressSaveKey(EKeys::Enter);
				Test->TestFalse(TEXT("保存してもどる leaves"), Settings->IsOpen());
				Test->TestEqual(TEXT("…keeping the change"), GetMasterVolume(), Saved);
				// A key binding changed, then the screen closed from outside (no answer): put back.
				Open();
				Settings->SetTab(FSaveScreen::ETab::Controls);
				{
					const int32 Profile = Settings->GetEditedProfile();
					const EChaosImpactDevice Device = Settings->GetEditedDevice();
					const FKey Before = GetKey(Profile, Device, EChaosImpactAction::DropBall, 1);
					// (Whatever it was, something else.)
					SetKey(Profile, Device, EChaosImpactAction::DropBall, 1, Device == EChaosImpactDevice::KeyboardMouse
						? (Before == EKeys::F7 ? EKeys::F8 : EKeys::F7)
						: (Before == EKeys::Gamepad_LeftThumbstick ? EKeys::Gamepad_RightThumbstick : EKeys::Gamepad_LeftThumbstick));
					Test->TestTrue(TEXT("A key taken off is unsaved"), Settings->HasUnsavedChanges());
					PC->CloseSettings();
					Test->TestTrue(TEXT("Closed from outside: the key is back"), GetKey(Profile, Device, EChaosImpactAction::DropBall, 1) == Before);
				}
				// The 保存 button with the mouse.
				Open();
				Settings->Adjust(-1);
				Settings->HandleMouseDown(FVector2D(220.0f, 754.0f), EKeys::LeftMouseButton);
				Test->TestFalse(TEXT("The 保存 button saves"), Settings->HasUnsavedChanges());
				PC->CloseSettings();
				return true;
			default:
				return true;
			}
		}

	private:
		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Stage = 0;
		int32 OriginalVolume = -1;
		int32 StartVolume = 0;
		int32 Saved = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactSettingsSaveTest, "ChaosImpact.Menu.SettingsSave",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactSettingsSaveTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FSettingsSaveCommand(this));
	return true;
}

#endif
