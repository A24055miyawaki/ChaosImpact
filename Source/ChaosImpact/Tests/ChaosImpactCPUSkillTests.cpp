#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactCPUController.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactGameState.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"

namespace
{
	/**
	 * Run in a local VS match with CPUs (see the test's log line). Measures how well the CPUs move and shoot:
	 * how far they walk, how long they spend stuck against something, and how often they hit the idle human.
	 */
	class FCPUSkillCommand : public IAutomationLatentCommand
	{
	public:
		explicit FCPUSkillCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld())
				{
					World = Context.World();
					break;
				}
			}
			const AChaosImpactGameState* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
			const APlayerController* Ready = World ? World->GetFirstPlayerController() : nullptr;
			// Works in a VS match and in the training arena, whose blocks are where a CPU can get stuck.
			const bool bMatchWorld = Match && Match->bVersusMatch;
			const bool bRunning = World && Ready && Ready->GetPawn()
				&& (!bMatchWorld || Match->Phase == EChaosImpactOnlinePhase::Match);
			if (!bRunning || Now - StartedAt < 6.0)
			{
				if (SampleCount > 0)
				{
					Report();
					return true;
				}
				if (Now - StartedAt > 200.0)
				{
					Test->AddError(TEXT("No world with a player to watch."));
					return true;
				}
				return false;
			}
			if (SampleCount * 0.2f >= 50.0f)
			{
				Report();
				return true;
			}
			if (Now < NextSampleAt)
			{
				return false;
			}
			const float Interval = NextSampleAt > 0.0 ? static_cast<float>(Now - NextSampleAt + 0.2) : 0.2f;
			NextSampleAt = Now + 0.2;
			++SampleCount;

			const APlayerController* First = World->GetFirstPlayerController();
			const AChaosImpactCharacter* Human = First ? Cast<AChaosImpactCharacter>(First->GetPawn()) : nullptr;
			if (Human && !Human->IsEliminated())
			{
				// Health only ever drops from a hit; respawns raise it again.
				if (LastHumanHealth > 0.0f && Human->GetHealth() < LastHumanHealth)
				{
					HumanHits += FMath::RoundToInt(LastHumanHealth - Human->GetHealth());
				}
				LastHumanHealth = Human->GetHealth();
			}
			else if (Human)
			{
				LastHumanHealth = -1.0f;
			}

			for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
			{
				AChaosImpactCharacter* Character = *It;
				if (!Cast<AChaosImpactCPUController>(Character->GetController()) || Character->IsEliminated())
				{
					continue;
				}
				const FVector Location = Character->GetActorLocation();
				FVector& Last = LastLocations.FindOrAdd(Character, Location);
				const float Moved = FVector::Dist2D(Location, Last);
				Last = Location;
				Travel += Moved;
				++CPUSamples;
				// Barely moving for several samples in a row is the "stuck against a wall" the player sees.
				float& Still = StillSeconds.FindOrAdd(Character);
				Still = Moved < 6.0f ? Still + Interval : 0.0f;
				StuckSeconds += Moved < 6.0f ? Interval : 0.0f;
				if (Still >= 1.0f)
				{
					++StuckEpisodes;
					Still = 0.0f;
				}
			}
			return false;
		}

	private:
		void Report() const
		{
			const float Seconds = SampleCount * 0.2f;
			const float PerCPU = CPUSamples > 0 ? Travel / (CPUSamples * 0.2f) : 0.0f;
			UE_LOG(LogTemp, Display,
				TEXT("CPUSKILL seconds=%.1f travelPerSecond=%.0f stuckSeconds=%.1f stuckEpisodes=%d humanHits=%d"),
				Seconds, PerCPU, StuckSeconds, StuckEpisodes, HumanHits);
			Test->TestTrue(TEXT("The CPUs kept moving"), PerCPU > 80.0f);
		}

		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextSampleAt = 0.0;
		int32 SampleCount = 0;
		int32 CPUSamples = 0;
		float Travel = 0.0f;
		float StuckSeconds = 0.0f;
		int32 StuckEpisodes = 0;
		int32 HumanHits = 0;
		float LastHumanHealth = -1.0f;
		TMap<TWeakObjectPtr<AChaosImpactCharacter>, FVector> LastLocations;
		TMap<TWeakObjectPtr<AChaosImpactCharacter>, float> StillSeconds;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactCPUSkillTest, "ChaosImpact.Versus.CPUSkill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::EngineFilter)

bool FChaosImpactCPUSkillTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FCPUSkillCommand(this));
	return true;
}

#endif
