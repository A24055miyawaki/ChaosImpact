#if WITH_DEV_AUTOMATION_TESTS
// 操作 in three steps by controller (2026-11-16): the nicknames, the chosen nickname's devices as cards, then that
// device's settings; B a step back each time, out to the tabs at the end. On the title world (mode select).
// Saved/SettingsQA/nick_*.png.
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
	using FNickScreen = UChaosImpactSettingsScreen;

	void NickPress(const FKey Key)
	{
		FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0));
		FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0));
	}

	class FNicknameFlowCommand : public IAutomationLatentCommand
	{
	public:
		explicit FNicknameFlowCommand(FAutomationTestBase* InTest) : Test(InTest) {}

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
			FNickScreen* Settings = Menu ? Menu->GetSettingsScreen() : nullptr;
			if (!PC || !Menu || !Settings)
			{
				return false;
			}
			const auto Shot = [](const TCHAR* Name)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SettingsQA"), Name), true, false);
			};
			const auto Next = [this, Now](const double Seconds) { ++Step; NextAt = Now + Seconds; return false; };
			switch (Step)
			{
			case 0:
				if (Now - StartedAt < 3.0) { return false; }
				PC->ShowMenuScreen(EChaosImpactScreen::ModeSelect);
				PC->OpenSettings();
				// Down twice to 操作, A into it: the nicknames.
				NickPress(EKeys::Gamepad_DPad_Down);
				NickPress(EKeys::Gamepad_DPad_Down);
				Test->TestTrue(TEXT("操作"), Settings->GetTab() == FNickScreen::ETab::Controls);
				NickPress(EKeys::Gamepad_FaceButton_Bottom);
				Test->TestTrue(TEXT("The nicknames first"), !Settings->IsChoosingTab() && Settings->GetControlsStep() == FNickScreen::EControlsStep::Nicknames);
				Test->TestTrue(TEXT("…with ＋ to make one"), Settings->FindRow(FNickScreen::RowNewProfile) != INDEX_NONE);
				Test->TestTrue(TEXT("…the cursor on the nickname being set"),
					Settings->GetRows().IsValidIndex(Settings->GetSelectedRow()) && Settings->GetRows()[Settings->GetSelectedRow()].Id >= FNickScreen::RowProfileItem);
				return Next(0.8);
			case 1:
				Shot(TEXT("nick_1_Nicknames.png"));
				return Next(0.3);
			case 2:
				NickPress(EKeys::Gamepad_FaceButton_Bottom);
				Test->TestTrue(TEXT("A on a nickname: its devices"), Settings->GetControlsStep() == FNickScreen::EControlsStep::Devices);
				NickPress(EKeys::Gamepad_DPad_Right);
				Device = Settings->GetEditedDevice();
				return Next(0.8);
			case 3:
				Shot(TEXT("nick_2_Devices.png"));
				return Next(0.3);
			case 4:
				NickPress(EKeys::Gamepad_FaceButton_Bottom);
				Test->TestTrue(TEXT("A on a card: that device's settings"), Settings->GetControlsStep() == FNickScreen::EControlsStep::Buttons
					&& Settings->GetEditedDevice() == Device);
				Test->TestTrue(TEXT("…with its buttons"), Settings->FindRow(FNickScreen::RowBinding) != INDEX_NONE);
				return Next(0.8);
			case 5:
				Shot(TEXT("nick_3_Buttons.png"));
				return Next(0.3);
			case 6:
				NickPress(EKeys::Gamepad_FaceButton_Right);
				Test->TestTrue(TEXT("B: back to the devices"), Settings->GetControlsStep() == FNickScreen::EControlsStep::Devices);
				NickPress(EKeys::Gamepad_DPad_Down);
				Test->TestTrue(TEXT("Down from the cards: 名前を変える"), Settings->GetRows().IsValidIndex(Settings->GetSelectedRow())
					&& Settings->GetRows()[Settings->GetSelectedRow()].Id == FNickScreen::RowRenameProfile);
				NickPress(EKeys::Gamepad_FaceButton_Right);
				Test->TestTrue(TEXT("B: back to the nicknames"), Settings->GetControlsStep() == FNickScreen::EControlsStep::Nicknames && !Settings->IsChoosingTab());
				NickPress(EKeys::Gamepad_FaceButton_Right);
				Test->TestTrue(TEXT("B: out to the tabs"), Settings->IsChoosingTab() && Settings->IsOpen());
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
		int32 Step = 0;
		EChaosImpactDevice Device = EChaosImpactDevice::KeyboardMouse;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactNicknameFlowTest, "ChaosImpact.UI.NicknameFlow",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FChaosImpactNicknameFlowTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FNicknameFlowCommand(this));
	return true;
}
#endif
