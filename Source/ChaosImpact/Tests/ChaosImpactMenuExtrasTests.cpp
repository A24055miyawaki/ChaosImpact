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
				Menu->Navigate(EKeys::Up);
				Stage = 7;
				NextAt = Now + 0.5;
				return false;
			case 7:
				Test->TestEqual(TEXT("Up from the first row reaches ボールスイッチ"), Menu->GetSelectedIndex(), 6);
				Menu->ConfirmSelection();
				Stage = 8;
				NextAt = Now + 0.8;
				return false;
			case 8:
				Test->TestEqual(TEXT("ボールスイッチ: 14 balls + すべてオン + もどる"), Menu->GetEntryCount(), 16);
				Menu->ConfirmSelection();
				Menu->Navigate(EKeys::Right);
				Menu->ConfirmSelection();
				Stage = 9;
				NextAt = Now + 0.8;
				return false;
			case 9:
			{
				const FChaosImpactMatchRules& Rules = PC->GetPendingMatchRules();
				Test->TestFalse(TEXT("Fire switched off"), ChaosImpactBallSwitch::IsOn(Rules, EChaosImpactBallType::Fire));
				Test->TestFalse(TEXT("Ice switched off"), ChaosImpactBallSwitch::IsOn(Rules, EChaosImpactBallType::Ice));
				Test->TestTrue(TEXT("Thunder still on"), ChaosImpactBallSwitch::IsOn(Rules, EChaosImpactBallType::Thunder));
				Shot(TEXT("ballswitch.png"));
				Stage = 10;
				NextAt = Now + 0.5;
				return false;
			}
			case 10:
				Menu->GoBack();
				Stage = 11;
				NextAt = Now + 0.8;
				return false;
			case 11:
				Shot(TEXT("rules_after.png"));
				Stage = 12;
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
