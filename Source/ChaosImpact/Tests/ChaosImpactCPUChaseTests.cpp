#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactBall.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactPlayerController.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"

namespace
{
	/**
	 * The "hit once and run" player against さいきょう. On a local VS level with one player and one CPU
	 * (?CITraining=1?CIMatch=1?CIMatchCPU=1, -CIMatchSeconds=100): the player is given a point, then for 60 seconds only
	 * runs (towards open floor, away from the CPU) and dashes out of every ball coming at it. Counted: how often the
	 * runner is still hit, how often the CPU throws, and how long the CPU spends thinking per frame.
	 */
	class FCPUChaseCommand : public IAutomationLatentCommand
	{
	public:
		explicit FCPUChaseCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld())
				{
					World = Context.World();
				}
			}
			const AChaosImpactGameState* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
			if (Now - StartedAt > 240.0)
			{
				Test->AddError(TEXT("The match never got going."));
				return true;
			}
			if (!Match || Match->Phase != EChaosImpactOnlinePhase::Match)
			{
				return false;
			}
			AChaosImpactPlayerController* PC = Cast<AChaosImpactPlayerController>(World->GetFirstPlayerController());
			AChaosImpactCharacter* Runner = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			if (!Runner)
			{
				return false;
			}
			if (!CPU.IsValid())
			{
				for (TActorIterator<AChaosImpactCPUController> It(World); It; ++It)
				{
					It->SetDifficulty(ChaosImpactMatch::CPULevelStrongest);
					CPU = Cast<AChaosImpactCharacter>(It->GetPawn());
				}
				if (!CPU.IsValid())
				{
					return false;
				}
				// The runner has scored once: from here it only has to stay alive.
				if (AChaosImpactPlayerState* State = Runner->GetPlayerState<AChaosImpactPlayerState>())
				{
					State->Points = 1;
				}
				ChaseStartedAt = Now;
				ThinkAtStart = AChaosImpactCPUController::DevThinkSeconds;
				FramesAtStart = GFrameCounter;
				LastHealth = Runner->GetHealth();
			}
			const AChaosImpactCharacter* Hunter = CPU.Get();
			if (!Hunter)
			{
				return false;
			}
			// Hits taken (a knockout counts its last hit; health comes back full at the respawn).
			if (Runner->GetHealth() < LastHealth)
			{
				Hits += FMath::RoundToInt(LastHealth - Runner->GetHealth());
			}
			if (Runner->IsEliminated() && !bWasEliminated)
			{
				++Knockouts;
			}
			bWasEliminated = Runner->IsEliminated();
			LastHealth = Runner->GetHealth();
			SlowestFrame = FMath::Max(SlowestFrame, static_cast<float>(FApp::GetDeltaTime()));
			if (!Runner->IsEliminated())
			{
				Run(World, Runner, Hunter);
			}
			if (Now - ChaseStartedAt < 60.0)
			{
				return false;
			}
			const int64 Frames = FMath::Max<int64>(static_cast<int64>(GFrameCounter - FramesAtStart), 1);
			const double ThinkMs = (AChaosImpactCPUController::DevThinkSeconds - ThinkAtStart) * 1000.0 / Frames;
			const double FrameMs = (Now - ChaseStartedAt) * 1000.0 / Frames;
			UE_LOG(LogTemp, Display, TEXT("CHASE runner hit %d times (%d knockouts) in 60 s; CPU thinks %.3f ms a frame; frame %.2f ms (slowest %.1f)"),
				Hits, Knockouts, ThinkMs, FrameMs, SlowestFrame * 1000.0f);
			Test->TestTrue(TEXT("Running away does not keep さいきょう off"), Hits >= 1);
			return true;
		}

	private:
		/** Away from the CPU towards open floor; a dash out of any ball about to arrive. */
		void Run(UWorld* World, AChaosImpactCharacter* Runner, const AChaosImpactCharacter* Hunter)
		{
			const FVector Here = Runner->GetActorLocation();
			const FVector Away = (Here - Hunter->GetActorLocation()).GetSafeNormal2D();
			FCollisionQueryParams Params(SCENE_QUERY_STAT(ChaosImpactChaseTest), false, Runner);
			FVector Best = Away;
			float BestScore = -TNumericLimits<float>::Max();
			for (int32 Candidate = 0; Candidate < 16; ++Candidate)
			{
				const FVector Direction = FVector::ForwardVector.RotateAngleAxis(Candidate * 22.5f, FVector::UpVector);
				FHitResult Wall;
				const float Free = World->LineTraceSingleByChannel(Wall, Here, Here + Direction * 700.0f, ECC_WorldStatic, Params)
					? Wall.Distance : 700.0f;
				// Never off the edge.
				FHitResult Floor;
				const FVector Ahead = Here + Direction * FMath::Min(Free, 300.0f);
				const bool bFloor = World->LineTraceSingleByChannel(Floor, Ahead, Ahead - FVector(0.0f, 0.0f, 600.0f), ECC_WorldStatic, Params);
				const float Score = (bFloor ? 0.0f : -100.0f) + FVector::DotProduct(Direction, Away) + Free / 700.0f * 1.2f;
				if (Score > BestScore)
				{
					BestScore = Score;
					Best = Direction;
				}
			}
			Runner->AddMovementInput(Best, 1.0f);
			for (TActorIterator<AChaosImpactBall> It(World); It; ++It)
			{
				const AChaosImpactBall* Ball = *It;
				if (Ball->IsPickup() || Ball->HasDetonated() || Ball->GetAttachParentActor() || Ball->GetThrowingPawn() != Hunter)
				{
					continue;
				}
				const FVector Velocity = Ball->GetBallVelocity();
				const float Speed2 = static_cast<float>(Velocity.SizeSquared2D());
				if (Speed2 < 10000.0f)
				{
					continue;
				}
				const FVector Offset = Here - Ball->GetActorLocation();
				const float Time = static_cast<float>(FVector::DotProduct(FVector(Offset.X, Offset.Y, 0.0f), FVector(Velocity.X, Velocity.Y, 0.0f))) / Speed2;
				const FVector Closest = Ball->GetActorLocation() + Velocity * Time;
				if (Time > 0.0f && Time < 0.3f && FVector::Dist2D(Closest, Here) < 170.0f && Runner->CanDashNow())
				{
					FVector Side = FVector::CrossProduct(FVector::UpVector, Velocity.GetSafeNormal2D());
					if (FVector::DotProduct(Side, Here - Closest) < 0.0f)
					{
						Side = -Side;
					}
					Runner->RequestAIDash(Side);
					break;
				}
			}
		}

		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double ChaseStartedAt = 0.0;
		double ThinkAtStart = 0.0;
		uint64 FramesAtStart = 0;
		TWeakObjectPtr<AChaosImpactCharacter> CPU;
		float LastHealth = 0.0f;
		int32 Hits = 0;
		int32 Knockouts = 0;
		bool bWasEliminated = false;
		float SlowestFrame = 0.0f;
	};

	/**
	 * How heavy さいきょう CPUs are: four of them playing a VS match the player only watches
	 * (?CITraining=1?CIVersus=1?CIMatch=1?CIMatchCPU=4?CISpectate=1?CILocalPlayers=1?CIKeyboardPlayer=0, -CIMatchSeconds=100).
	 */
	class FCPUWeightCommand : public IAutomationLatentCommand
	{
	public:
		explicit FCPUWeightCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld())
				{
					World = Context.World();
				}
			}
			const AChaosImpactGameState* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
			if (Now - StartedAt > 200.0)
			{
				Test->AddError(TEXT("The match never got going."));
				return true;
			}
			if (!Match || Match->Phase != EChaosImpactOnlinePhase::Match)
			{
				return false;
			}
			if (MeasureFrom <= 0.0)
			{
				int32 Count = 0;
				for (TActorIterator<AChaosImpactCPUController> It(World); It; ++It)
				{
					It->SetDifficulty(ChaosImpactMatch::CPULevelStrongest);
					++Count;
				}
				MeasureFrom = Now + 3.0;
				UE_LOG(LogTemp, Display, TEXT("CPUWEIGHT %d さいきょう CPUs"), Count);
				return false;
			}
			if (Now < MeasureFrom)
			{
				return false;
			}
			if (ThinkAtStart < 0.0)
			{
				for (int32 Index = 0; Index < 6; ++Index)
				{
					SectionsAtStart[Index] = AChaosImpactCPUController::DevSectionSeconds[Index];
				}
				ThinkAtStart = AChaosImpactCPUController::DevThinkSeconds;
				FramesAtStart = GFrameCounter;
				return false;
			}
			SlowestFrame = FMath::Max(SlowestFrame, static_cast<float>(FApp::GetDeltaTime()));
			if (Now - MeasureFrom < 40.0)
			{
				return false;
			}
			const int64 Frames = FMath::Max<int64>(static_cast<int64>(GFrameCounter - FramesAtStart), 1);
			UE_LOG(LogTemp, Display, TEXT("CPUWEIGHT parts (ms a frame): perception %.3f learning %.3f target %.3f dodging %.3f offence %.3f positioning %.3f"),
				(AChaosImpactCPUController::DevSectionSeconds[0] - SectionsAtStart[0]) * 1000.0 / Frames,
				(AChaosImpactCPUController::DevSectionSeconds[1] - SectionsAtStart[1]) * 1000.0 / Frames,
				(AChaosImpactCPUController::DevSectionSeconds[2] - SectionsAtStart[2]) * 1000.0 / Frames,
				(AChaosImpactCPUController::DevSectionSeconds[3] - SectionsAtStart[3]) * 1000.0 / Frames,
				(AChaosImpactCPUController::DevSectionSeconds[4] - SectionsAtStart[4]) * 1000.0 / Frames,
				(AChaosImpactCPUController::DevSectionSeconds[5] - SectionsAtStart[5]) * 1000.0 / Frames);
			UE_LOG(LogTemp, Display, TEXT("CPUWEIGHT think %.3f ms a frame (all CPUs); frame %.2f ms, slowest %.1f ms"),
				(AChaosImpactCPUController::DevThinkSeconds - ThinkAtStart) * 1000.0 / Frames, (Now - MeasureFrom) * 1000.0 / Frames,
				SlowestFrame * 1000.0f);
			return true;
		}

	private:
		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double MeasureFrom = 0.0;
		double ThinkAtStart = -1.0;
		double SectionsAtStart[6] = {};
		uint64 FramesAtStart = 0;
		float SlowestFrame = 0.0f;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactCPUChaseTest, "ChaosImpact.Versus.CPUChase",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactCPUChaseTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FCPUChaseCommand(this));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactCPUWeightTest, "ChaosImpact.Versus.CPUWeight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactCPUWeightTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FCPUWeightCommand(this));
	return true;
}

#endif
