#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactCyberStage.h"
#include "ChaosImpactForgeStage.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactScreen.h"
#include "ChaosImpactSnowStage.h"
#include "ChaosImpactTempleStage.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	/**
	 * A VS stage's gimmick filmed through its warning, its strike and after (run with -CIGimmickStage=3..6): a local match
	 * to watch on that stage, the gimmick brought on at once, pictures at set moments. Saved/GimmickQA/stage<N>_<n>.png.
	 * Stage 4 is also filmed with players on its moving walkways (the walkways carry them).
	 */
	class FGimmickShotsCommand : public IAutomationLatentCommand
	{
	public:
		explicit FGimmickShotsCommand(FAutomationTestBase* InTest) : Test(InTest)
		{
			FParse::Value(FCommandLine::Get(), TEXT("CIGimmickStage="), StageNumber);
			FParse::Value(FCommandLine::Get(), TEXT("CIGimmickTag="), Tag);
		}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - StartedAt > 240.0)
			{
				Test->AddError(FString::Printf(TEXT("Stuck at step %d"), Step));
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
			const AChaosImpactGameState* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
			if (!PC || Now < NextAt)
			{
				return false;
			}
			switch (Step)
			{
			case 0:
				if (Now - StartedAt < 4.0) { return false; }
				PC->ShowMenuScreen(EChaosImpactScreen::ModeSelect);
				PC->BeginLocalSpectate();
				PC->ChooseStage(FMath::Clamp(StageNumber, 1, 6) - 1);
				Step = 1;
				NextAt = Now + 1.0;
				return false;
			case 1:
				if (!World->GetMapName().Contains(FString::Printf(TEXT("Lvl_VS_Stage%d"), StageNumber)) || !Match
					|| Match->Phase != EChaosImpactOnlinePhase::Match)
				{
					return false;
				}
				Step = 2;
				NextAt = Now + 3.0;
				return false;
			case 2:
				if (!Trigger(World))
				{
					Test->AddError(FString::Printf(TEXT("No gimmick to bring on for stage %d"), StageNumber));
					return true;
				}
				TriggeredAt = Now;
				Step = 3;
				return false;
			default:
			{
				static const float Times[] = {0.4f, 1.4f, 2.3f, 2.9f, 3.4f, 4.2f, 6.0f, 8.6f};
				if (Shot >= UE_ARRAY_COUNT(Times))
				{
					return true;
				}
				if (Now - TriggeredAt >= Times[Shot])
				{
					FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("GimmickQA"),
						FString::Printf(TEXT("stage%d%s_%d.png"), StageNumber, *Tag, Shot + 1)), true, false);
					++Shot;
				}
				return false;
			}
			}
		}

	private:
		bool Trigger(UWorld* World) const
		{
			for (TActorIterator<AChaosImpactTempleStage> It(World); It; ++It) { It->StartPillar(0); return true; }
			for (TActorIterator<AChaosImpactCyberStage> It(World); It; ++It) { It->StartGatePattern(2); return true; }
			for (TActorIterator<AChaosImpactSnowStage> It(World); It; ++It) { It->StartWindGust(0); return true; }
			for (TActorIterator<AChaosImpactForgeStage> It(World); It; ++It) { It->StartVentPair(0); return true; }
			return false;
		}

		FAutomationTestBase* Test = nullptr;
		int32 StageNumber = 3;
		FString Tag;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		double TriggeredAt = 0.0;
		int32 Step = 0;
		int32 Shot = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactGimmickShotsTest, "ChaosImpact.Versus.GimmickShots",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactGimmickShotsTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FGimmickShotsCommand(this));
	return true;
}

#endif
