#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactBallSwitch.h"
#include "ChaosImpactMenuWidget.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactScreen.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Input/Events.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	/**
	 * The 訓練場 invitation over the first mode select of a run (its two choices, closed with back), then the rules
	 * screen's ボールスイッチ (two balls switched off). Screenshots in Saved/MenuQA.
	 */
	class FMenuExtrasCommand : public IAutomationLatentCommand
	{
	public:
		explicit FMenuExtrasCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - StartedAt > 150.0)
			{
				Test->AddError(TEXT("Timed out."));
				return true;
			}
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld())
				{
					World = Context.World();
				}
			}
			AChaosImpactPlayerController* PC = World ? Cast<AChaosImpactPlayerController>(World->GetFirstPlayerController()) : nullptr;
			UChaosImpactMenuWidget* Menu = PC ? PC->GetMenuWidget() : nullptr;
			if (!PC || !Menu || !PC->GetPawn() || Now < NextAt)
			{
				return false;
			}
			const auto Shot = [](const TCHAR* Name)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MenuQA"), Name), true, false);
			};
			const auto Press = [Menu](const FKey& Key)
			{
				Menu->HandleAnyUserKeyDown(FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0));
			};
			switch (Stage)
			{
			case 0:
				PC->ShowMenuScreen(EChaosImpactScreen::ModeSelect);
				Stage = 1;
				NextAt = Now + 3.0;
				return false;
			case 1:
				Shot(TEXT("invite_0.png"));
				Stage = 2;
				NextAt = Now + 0.4;
				return false;
			case 2:
				Press(EKeys::Right);
				Stage = 3;
				NextAt = Now + 3.5;
				return false;
			case 3:
				Shot(TEXT("invite_1.png"));
				Stage = 4;
				NextAt = Now + 0.4;
				return false;
			case 4:
				Press(EKeys::Escape);
				Stage = 5;
				NextAt = Now + 0.8;
				return false;
			case 5:
				Test->TestEqual(TEXT("Closed with back: left on 訓練場"), Menu->GetSelectedIndex(), 6);
				Shot(TEXT("invite_closed.png"));
				PC->OpenMatchRules(EChaosImpactScreen::VSSelect);
				Stage = 6;
				NextAt = Now + 1.5;
				return false;
			case 6:
				Shot(TEXT("rules.png"));
				Test->TestEqual(TEXT("The rules have 7 entries (ボールスイッチ last)"), Menu->GetEntryCount(), 7);
				// Up from the first row lands on 決定 (the bottom row reads 戻る, ボールスイッチ, 決定); one left is ボールスイッチ.
				Menu->Navigate(EKeys::Up);
				Menu->Navigate(EKeys::Left);
				Stage = 7;
				NextAt = Now + 0.5;
				return false;
			case 7:
				Test->TestEqual(TEXT("Up, then left: ボールスイッチ"), Menu->GetSelectedIndex(), 6);
				Menu->ConfirmSelection();
				Stage = 8;
				NextAt = Now + 0.8;
				return false;
			case 8:
				Test->TestEqual(TEXT("ボールスイッチ: 15 balls (normal first) + すべてオン + もどる + すべてオフ"), Menu->GetEntryCount(), 18);
				Menu->ConfirmSelection();
				Menu->Navigate(EKeys::Right);
				Menu->ConfirmSelection();
				Stage = 9;
				NextAt = Now + 0.8;
				return false;
			case 9:
			{
				const FChaosImpactMatchRules& Rules = PC->GetPendingMatchRules();
				Test->TestFalse(TEXT("Normal switched off"), ChaosImpactBallSwitch::IsOn(Rules, EChaosImpactBallType::Normal));
				Test->TestFalse(TEXT("Fire switched off"), ChaosImpactBallSwitch::IsOn(Rules, EChaosImpactBallType::Fire));
				Test->TestTrue(TEXT("Ice still on"), ChaosImpactBallSwitch::IsOn(Rules, EChaosImpactBallType::Ice));
				Shot(TEXT("ballswitch.png"));
				// Down twice to the bottom row (もどる), right twice to すべてオフ, pressed; then back is refused.
				Menu->Navigate(EKeys::Down);
				Menu->Navigate(EKeys::Down);
				Menu->Navigate(EKeys::Right);
				Menu->Navigate(EKeys::Right);
				Menu->ConfirmSelection();
				Menu->GoBack();
				Stage = 10;
				NextAt = Now + 0.5;
				return false;
			}
			case 10:
				Test->TestEqual(TEXT("すべてオフ: every kind off"), ChaosImpactBallSwitch::CountOn(PC->GetPendingMatchRules()), 0);
				Test->TestEqual(TEXT("Nothing on: back refused, still on the switch"), Menu->GetEntryCount(), 18);
				Test->TestTrue(TEXT("Nothing on: もどる greyed out"), Menu->IsEntryDisabled(Menu->GetEntryCount() - 2));
				Shot(TEXT("ballswitch_alloff.png"));
				Stage = 11;
				NextAt = Now + 0.5;
				return false;
			case 11:
				// Left once: すべてオン, pressed; now back closes it.
				Menu->Navigate(EKeys::Left);
				Menu->ConfirmSelection();
				Menu->GoBack();
				Stage = 12;
				NextAt = Now + 0.8;
				return false;
			case 12:
				Test->TestEqual(TEXT("All on again"), ChaosImpactBallSwitch::CountOn(PC->GetPendingMatchRules()), ChaosImpactBallTypes::Count);
				Test->TestEqual(TEXT("Back on the rules"), Menu->GetEntryCount(), 7);
				Shot(TEXT("rules_after.png"));
				Stage = 13;
				NextAt = Now + 0.5;
				return false;
			default:
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactMenuExtrasTest, "ChaosImpact.Menu.InviteAndSwitch",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactMenuExtrasTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FMenuExtrasCommand(this));
	return true;
}

#endif
