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
	 * The mode select and the VS mode screens: opened, walked round with the arrow keys (each key's landing checked),
	 * a screenshot at each stop. Saved/MenuQA.
	 */
	class FModeMenuShotsCommand : public IAutomationLatentCommand
	{
	public:
		explicit FModeMenuShotsCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - StartedAt > 120.0)
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
			const auto Shot = [](const FString& Name)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MenuQA"), Name), true, false);
			};
			// The walk: a key, then where it must land (ModeSelect indices 0 ソロ 1 VS 2 トレーニング 3 タイトルへ 4 設定 5 カラー 6 訓練場).
			struct FStep { FKey Key; int32 Want; };
			static const FStep ModeWalk[] = {{EKeys::Right, 1}, {EKeys::Right, 6}, {EKeys::Down, 4}, {EKeys::Left, 5}, {EKeys::Left, 2},
				{EKeys::Down, 3}, {EKeys::Up, 2}, {EKeys::Up, 0}};
			switch (Stage)
			{
			case 0:
				PC->ShowMenuScreen(EChaosImpactScreen::ModeSelect);
				NextAt = Now + 1.6;
				Stage = 1;
				return false;
			case 1:
				Test->TestEqual(TEXT("Mode select has its seven tiles"), Menu->GetEntryCount(), 7);
				Shot(TEXT("mode_00.png"));
				Step = 0;
				Stage = 2;
				NextAt = Now + 0.6;
				return false;
			case 2:
				if (Step < UE_ARRAY_COUNT(ModeWalk))
				{
					Menu->Navigate(ModeWalk[Step].Key);
					Test->TestEqual(FString::Printf(TEXT("Mode select step %d"), Step), Menu->GetSelectedIndex(), ModeWalk[Step].Want);
					++Step;
					Stage = 3;
					NextAt = Now + 0.7;
					return false;
				}
				PC->ShowMenuScreen(EChaosImpactScreen::VSSelect);
				Stage = 4;
				NextAt = Now + 1.6;
				return false;
			case 3:
				Shot(FString::Printf(TEXT("mode_%02d.png"), Step));
				Stage = 2;
				NextAt = Now + 0.3;
				return false;
			case 4:
				Test->TestEqual(TEXT("VS mode has its four tiles"), Menu->GetEntryCount(), 4);
				Shot(TEXT("vs_00.png"));
				Stage = 5;
				NextAt = Now + 0.5;
				return false;
			case 5:
				Menu->Navigate(EKeys::Right);
				Stage = 6;
				NextAt = Now + 0.7;
				return false;
			case 6:
				Shot(TEXT("vs_01.png"));
				Menu->Navigate(EKeys::Right);
				Stage = 7;
				NextAt = Now + 0.7;
				return false;
			case 7:
				Test->TestEqual(TEXT("VS: right twice reaches トーナメント"), Menu->GetSelectedIndex(), 3);
				Shot(TEXT("vs_02.png"));
				Menu->Navigate(EKeys::Down);
				Stage = 8;
				NextAt = Now + 0.7;
				return false;
			case 8:
				Shot(TEXT("vs_03.png"));
				Stage = 9;
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
		int32 Step = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactModeMenuShotsTest, "ChaosImpact.Menu.ModeShots",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactModeMenuShotsTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FModeMenuShotsCommand(this));
	return true;
}

#endif
