#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactMenuWidget.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactScreen.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	/**
	 * The rules page with the d-pad (a controller has no mouse): down from the rows reaches the bottom row, which goes left
	 * and right as it is drawn (戻る, ボールスイッチ, 決定); up goes back to the rows; ボールスイッチ opens from it and
	 * closing it lands on it again; down from the rows then comes back to ボールスイッチ. Saved/MenuQA/rules_cursor.png.
	 */
	class FRulesCursorCommand : public IAutomationLatentCommand
	{
	public:
		explicit FRulesCursorCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - StartedAt > 60.0)
			{
				Test->AddError(TEXT("Timed out"));
				return true;
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
			if (!Menu || Now < NextAt || Now - StartedAt < 3.0)
			{
				return false;
			}
			const auto Press = [Menu](const FKey& Key) { Menu->Navigate(Key); return Menu->GetSelectedIndex(); };
			switch (Stage)
			{
			case 0:
				PC->OpenMatchRules(EChaosImpactScreen::Playing);
				++Stage;
				NextAt = Now + 1.0;
				return false;
			case 1:
			{
				Test->TestTrue(TEXT("The rules page is up"), PC->GetCurrentScreen() == EChaosImpactScreen::MatchRules);
				// Down past the rows: 決定.
				int32 At = Menu->GetSelectedIndex();
				for (int32 Step = 0; Step < 5 && At <= 3; ++Step)
				{
					At = Press(EKeys::Gamepad_DPad_Down);
				}
				Test->TestEqual(TEXT("Down from the rows reaches 決定"), At, 4);
				Test->TestEqual(TEXT("Left of 決定 is ボールスイッチ"), Press(EKeys::Gamepad_DPad_Left), 6);
				Test->TestEqual(TEXT("Left of ボールスイッチ is 戻る"), Press(EKeys::Gamepad_DPad_Left), 5);
				Test->TestEqual(TEXT("Left of 戻る goes round to 決定"), Press(EKeys::Gamepad_DPad_Left), 4);
				Test->TestEqual(TEXT("Right of 決定 goes round to 戻る"), Press(EKeys::Gamepad_DPad_Right), 5);
				Test->TestEqual(TEXT("Right of 戻る is ボールスイッチ"), Press(EKeys::Gamepad_DPad_Right), 6);
				const int32 Up = Press(EKeys::Gamepad_DPad_Up);
				Test->TestTrue(FString::Printf(TEXT("Up from the bottom row is a rule row (%d)"), Up), Up >= 0 && Up <= 3);
				Test->TestEqual(TEXT("Down again comes back to ボールスイッチ"), Press(EKeys::Gamepad_DPad_Down), 6);
				// The same with the keyboard's arrows.
				Test->TestEqual(TEXT("Arrow right: 決定"), Press(EKeys::Right), 4);
				Test->TestEqual(TEXT("Arrow left: ボールスイッチ"), Press(EKeys::Left), 6);
				Menu->ConfirmSelection();
				++Stage;
				NextAt = Now + 0.8;
				return false;
			}
			case 2:
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MenuQA"), TEXT("rules_ballswitch_open.png")), true, false);
				Test->TestTrue(TEXT("A on ボールスイッチ opens it (its tiles are the entries now)"), Menu->GetEntryCount() > 7);
				Menu->Navigate(EKeys::Gamepad_FaceButton_Right);
				PC->ShowMenuScreen(EChaosImpactScreen::MatchRules);
				++Stage;
				NextAt = Now + 0.8;
				return false;
			default:
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MenuQA"), TEXT("rules_cursor.png")), true, false);
				PC->CancelMatchRules();
				return true;
			}
		}

	private:
		FAutomationTestBase* Test = nullptr;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Stage = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactRulesCursorTest, "ChaosImpact.Menu.RulesCursor",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactRulesCursorTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FRulesCursorCommand(this));
	return true;
}

#endif
