#if WITH_DEV_AUTOMATION_TESTS
// The settings screen by controller only (2026-11-16): the cursor starts in the tab column; the D-pad goes up and down
// it (a tab opens as it is reached), A goes into the tab's settings, B comes back out, and 保存 and もどる are in the
// column too. On the title world (mode select). Saved/SettingsQA/pad_*.png. The volume is put back afterwards.
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
	using FPadScreen = UChaosImpactSettingsScreen;

	void PadPress(const FKey Key)
	{
		FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0));
		FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0));
	}

	void PadShot(const TCHAR* Name)
	{
		FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SettingsQA"), Name), true, false);
	}

	class FSettingsPadNavCommand : public IAutomationLatentCommand
	{
	public:
		explicit FSettingsPadNavCommand(FAutomationTestBase* InTest) : Test(InTest) {}
		virtual ~FSettingsPadNavCommand() override
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
				Test->AddError(FString::Printf(TEXT("Stuck at step %d."), Step));
				return true;
			}
			if (Now < NextAt)
			{
				return false;
			}
			AChaosImpactPlayerController* PC = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld())
				{
					PC = Cast<AChaosImpactPlayerController>(Context.World()->GetFirstPlayerController());
				}
			}
			UChaosImpactMenuWidget* Menu = PC ? PC->GetMenuWidget() : nullptr;
			FPadScreen* Settings = Menu ? Menu->GetSettingsScreen() : nullptr;
			if (!PC || !Menu || !Settings)
			{
				return false;
			}
			const auto Next = [this, Now](const double Seconds) { ++Step; NextAt = Now + Seconds; return false; };
			switch (Step)
			{
			case 0:
				if (Now - StartedAt < 3.0) { return false; }
				OriginalVolume = ChaosImpactSettings::GetMasterVolume();
				ChaosImpactSettings::SetMasterVolume(OriginalVolume < 40 ? 50 : OriginalVolume);
				PC->ShowMenuScreen(EChaosImpactScreen::ModeSelect);
				PC->OpenSettings();
				Test->TestTrue(TEXT("The cursor starts in the tab column"), Settings->IsChoosingTab() && Settings->GetSideCursor() == 0);
				PadPress(EKeys::Gamepad_DPad_Down);
				Test->TestTrue(TEXT("D-pad down moves to the next tab and opens it"),
					Settings->GetSideCursor() == 1 && Settings->GetTab() == FPadScreen::ETab::Picture && Settings->IsChoosingTab());
				PadPress(EKeys::Gamepad_RightShoulder);
				Test->TestTrue(TEXT("RB no longer changes tab"), Settings->GetTab() == FPadScreen::ETab::Picture);
				return Next(0.8);
			case 1:
				PadShot(TEXT("pad_1_Tabs.png"));
				return Next(0.3);
			case 2:
				PadPress(EKeys::Gamepad_FaceButton_Bottom);
				Test->TestFalse(TEXT("A goes into the tab's settings"), Settings->IsChoosingTab());
				Test->TestTrue(TEXT("…with a row picked"), Settings->GetRows().IsValidIndex(Settings->GetSelectedRow()));
				PadPress(EKeys::Gamepad_DPad_Down);
				return Next(0.6);
			case 3:
				PadShot(TEXT("pad_2_Rows.png"));
				return Next(0.3);
			case 4:
				PadPress(EKeys::Gamepad_FaceButton_Right);
				Test->TestTrue(TEXT("B comes back out to the tabs"), Settings->IsChoosingTab() && Settings->GetSideCursor() == 1);
				Test->TestTrue(TEXT("…the screen still open"), Settings->IsOpen());
				// To その他, in, the volume down (a change), back out, and down to 保存.
				PadPress(EKeys::Gamepad_DPad_Down);
				PadPress(EKeys::Gamepad_DPad_Down);
				Test->TestTrue(TEXT("その他"), Settings->GetTab() == FPadScreen::ETab::Other);
				PadPress(EKeys::Gamepad_FaceButton_Bottom);
				Settings->SelectRow(Settings->FindRow(FPadScreen::RowVolume));
				PadPress(EKeys::Gamepad_DPad_Left);
				Test->TestTrue(TEXT("A change, unsaved"), Settings->HasUnsavedChanges());
				PadPress(EKeys::Gamepad_FaceButton_Right);
				PadPress(EKeys::Gamepad_DPad_Down);
				Test->TestEqual(TEXT("Down from the last tab is 保存"), Settings->GetSideCursor(), FPadScreen::SideSave);
				Test->TestTrue(TEXT("…the tab staying open beside it"), Settings->GetTab() == FPadScreen::ETab::Other);
				return Next(0.8);
			case 5:
				PadShot(TEXT("pad_3_Save.png"));
				return Next(0.3);
			case 6:
				PadPress(EKeys::Gamepad_FaceButton_Bottom);
				Test->TestFalse(TEXT("A on 保存 saves"), Settings->HasUnsavedChanges());
				PadPress(EKeys::Gamepad_DPad_Down);
				Test->TestEqual(TEXT("Below 保存 is もどる"), Settings->GetSideCursor(), FPadScreen::SideBack);
				PadPress(EKeys::Gamepad_DPad_Down);
				Test->TestEqual(TEXT("…and round to the first tab"), Settings->GetSideCursor(), 0);
				PadPress(EKeys::Gamepad_DPad_Up);
				PadPress(EKeys::Gamepad_FaceButton_Bottom);
				Test->TestFalse(TEXT("A on もどる leaves"), Settings->IsOpen());
				return true;
			default:
				return true;
			}
		}

	private:
		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Step = 0;
		int32 OriginalVolume = -1;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactSettingsPadNavTest, "ChaosImpact.UI.SettingsPadNav",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FChaosImpactSettingsPadNavTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FSettingsPadNavCommand(this));
	return true;
}
#endif
