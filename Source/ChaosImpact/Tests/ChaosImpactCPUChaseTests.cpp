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
#include "Misc/CommandLine.h"
#include "Kismet/GameplayStatics.h"

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
				for (int32 Index = 0; Index < 10; ++Index)
				{
					PartsAtStart[Index] = AChaosImpactCPUController::DevPartSeconds[Index];
				}
				ThinkAtStart = AChaosImpactCPUController::DevThinkSeconds;
				LastThink = ThinkAtStart;
				for (int32 Index = 0; Index < 10; ++Index) { LastPart[Index] = AChaosImpactCPUController::DevPartSeconds[Index]; }
				for (int32 Index = 0; Index < 6; ++Index) { LastSection[Index] = AChaosImpactCPUController::DevSectionSeconds[Index]; }
				FramesAtStart = GFrameCounter;
				return false;
			}
			SlowestFrame = FMath::Max(SlowestFrame, static_cast<float>(FApp::GetDeltaTime()));
			// The most the CPUs thought in one frame (a frame's hitch from them, or from something else).
			MostThinkFrame = FMath::Max(MostThinkFrame, AChaosImpactCPUController::DevThinkSeconds - LastThink);
			LastThink = AChaosImpactCPUController::DevThinkSeconds;
			for (int32 Index = 0; Index < 10; ++Index)
			{
				MostPart[Index] = FMath::Max(MostPart[Index], AChaosImpactCPUController::DevPartSeconds[Index] - LastPart[Index]);
				LastPart[Index] = AChaosImpactCPUController::DevPartSeconds[Index];
			}
			for (int32 Index = 0; Index < 6; ++Index)
			{
				MostSection[Index] = FMath::Max(MostSection[Index], AChaosImpactCPUController::DevSectionSeconds[Index] - LastSection[Index]);
				LastSection[Index] = AChaosImpactCPUController::DevSectionSeconds[Index];
			}
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
			{
				static const TCHAR* const Names[] = {TEXT("PlanSureShot"), TEXT("CollectExactThreats"), TEXT("EvaluateEscape"), TEXT("UpdateEvasion"),
					TEXT("ChooseSafeDirection"), TEXT("GetHazardAt"), TEXT("FindVantagePoint"), TEXT("RouteToward"), TEXT("SelectPickup"), TEXT("UpdateLearning")};
				FString Line;
				for (int32 Index = 0; Index < 10; ++Index)
				{
					Line += FString::Printf(TEXT(" %s %.3f"), Names[Index], (AChaosImpactCPUController::DevPartSeconds[Index] - PartsAtStart[Index]) * 1000.0 / Frames);
				}
				UE_LOG(LogTemp, Display, TEXT("CPUWEIGHT detail (ms a frame):%s"), *Line);
			}
			UE_LOG(LogTemp, Display, TEXT("CPUWEIGHT most thought in one frame: %.2f ms"), MostThinkFrame * 1000.0);
			UE_LOG(LogTemp, Display, TEXT("CPUWEIGHT most in one frame: parts %.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f / sections %.1f %.1f %.1f %.1f %.1f %.1f"),
				MostPart[0] * 1000.0, MostPart[1] * 1000.0, MostPart[2] * 1000.0, MostPart[3] * 1000.0, MostPart[4] * 1000.0, MostPart[5] * 1000.0,
				MostPart[6] * 1000.0, MostPart[7] * 1000.0, MostPart[8] * 1000.0, MostPart[9] * 1000.0, MostSection[0] * 1000.0, MostSection[1] * 1000.0,
				MostSection[2] * 1000.0, MostSection[3] * 1000.0, MostSection[4] * 1000.0, MostSection[5] * 1000.0);
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
		double PartsAtStart[10] = {};
		double LastThink = 0.0;
		double MostThinkFrame = 0.0;
		double LastPart[10] = {};
		double MostPart[10] = {};
		double LastSection[6] = {};
		double MostSection[6] = {};
		uint64 FramesAtStart = 0;
		float SlowestFrame = 0.0f;
	};
}

namespace
{
	/**
	 * How strong さいきょう is: one さいきょう against three つよい in a VS match the player only watches (the same options as
	 * CPUWeight, ?CIMatchCPU=4), sped up twice; the points each side made over two minutes of play.
	 */
	class FCPUStrengthCommand : public IAutomationLatentCommand
	{
	public:
		explicit FCPUStrengthCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld()) { World = Context.World(); }
			}
			const AChaosImpactGameState* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
			if (Now - StartedAt > 260.0)
			{
				Test->AddError(TEXT("The match never got going."));
				return true;
			}
			if (!Match || Match->Phase != EChaosImpactOnlinePhase::Match)
			{
				return false;
			}
			if (!bSet)
			{
				bSet = true;
				int32 Index = 0;
				for (TActorIterator<AChaosImpactCPUController> It(World); It; ++It, ++Index)
				{
					It->SetDifficulty(Index == 0 ? ChaosImpactMatch::CPULevelStrongest : ChaosImpactMatch::CPULevelStrong);
					if (Index == 0) { Strongest = *It; }
				}
				UGameplayStatics::SetGlobalTimeDilation(World, 2.0f);
				EndAt = World->GetTimeSeconds() + 120.0;
				return false;
			}
			if (World->GetTimeSeconds() < EndAt)
			{
				return false;
			}
			UGameplayStatics::SetGlobalTimeDilation(World, 1.0f);
			int32 Mine = 0;
			int32 Others = 0;
			int32 OtherCount = 0;
			for (TActorIterator<AChaosImpactCPUController> It(World); It; ++It)
			{
				const AChaosImpactPlayerState* State = It->GetPlayerState<AChaosImpactPlayerState>();
				const int32 Points = State ? State->Points : 0;
				if (*It == Strongest.Get()) { Mine = Points; }
				else { Others += Points; ++OtherCount; }
			}
			const float Average = OtherCount ? static_cast<float>(Others) / OtherCount : 0.0f;
			UE_LOG(LogTemp, Display, TEXT("CPUSTRENGTH さいきょう %d points, つよい average %.1f (x%.2f)"), Mine, Average, Average > 0.0f ? Mine / Average : 0.0f);
			Test->TestTrue(TEXT("さいきょう outscores つよい"), Mine > Average);
			return true;
		}

	private:
		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		bool bSet = false;
		double EndAt = 0.0;
		TWeakObjectPtr<AChaosImpactCPUController> Strongest;
	};
}

namespace
{
	/**
	 * さいきょう in a duel: one さいきょう against one つよい (?CIMatch=1?CIDuel=1?CIMatchCPU=2?CISpectate=1), sped up twice; the
	 * rounds each won and who won the duel.
	 */
	/**
	 * A duel between two CPUs (a duel is a player against one CPU: the player's own side is handed to a CPU as well), sped
	 * up twice. Side BossSide (-CIDuelBossSide=, 0 by default) is さいきょう; the other is つよい, or さいきょう too with
	 * bMirror. Its duel play (bDuelBoss) as in a match, or with bMirror the boss side only (-CIDuelNoBoss: neither).
	 */
	class FCPUDuelStrengthCommand : public IAutomationLatentCommand
	{
	public:
		explicit FCPUDuelStrengthCommand(FAutomationTestBase* InTest, const bool bInMirror = false) : Test(InTest), bMirror(bInMirror) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld()) { World = Context.World(); }
			}
			AChaosImpactGameState* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
			if (Now - StartedAt > 600.0)
			{
				Test->AddError(TEXT("The duel never finished."));
				return true;
			}
			if (!Match || !Match->IsDuel() || (Match->Phase != EChaosImpactOnlinePhase::Match && Match->Phase != EChaosImpactOnlinePhase::Results))
			{
				return false;
			}
			if (!bSet)
			{
				APlayerState* Members[2] = {Match->Duel.Sides[0].Get(), Match->Duel.Sides[1].Get()};
				if (!Members[0] || !Members[1] || !Members[0]->GetPawn() || !Members[1]->GetPawn())
				{
					return false;
				}
				AChaosImpactCPUController* Brains[2] = {nullptr, nullptr};
				for (int32 Side = 0; Side < 2; ++Side)
				{
					Brains[Side] = Cast<AChaosImpactCPUController>(Members[Side]->GetOwningController());
					if (Brains[Side])
					{
						continue;
					}
					APawn* Body = Members[Side]->GetPawn();
					if (APlayerController* Player = Cast<APlayerController>(Members[Side]->GetOwningController()))
					{
						Player->UnPossess();
						Player->SetViewTarget(Body);
					}
					FActorSpawnParameters Params;
					Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
					Brains[Side] = World->SpawnActor<AChaosImpactCPUController>(AChaosImpactCPUController::StaticClass(),
						Body->GetActorLocation(), Body->GetActorRotation(), Params);
					Brains[Side]->MarkAsMatchCPU();
					Brains[Side]->Possess(Body);
					Match->Duel.Sides[Side] = Brains[Side]->GetPlayerState<APlayerState>();
				}
				int32 BossSide = 0;
				FParse::Value(FCommandLine::Get(), TEXT("CIDuelBossSide="), BossSide);
				BossSide = FMath::Clamp(BossSide, 0, 1);
				const bool bNoBoss = FParse::Param(FCommandLine::Get(), TEXT("CIDuelNoBoss"));
				for (int32 Side = 0; Side < 2; ++Side)
				{
					Brains[Side]->SetDifficulty(Side == BossSide || bMirror ? ChaosImpactMatch::CPULevelStrongest : ChaosImpactMatch::CPULevelStrong);
					Brains[Side]->SetDuelBossForTests(bMirror || bNoBoss ? TOptional<bool>(Side == BossSide && !bNoBoss) : TOptional<bool>());
				}
				Strongest = Match->Duel.Sides[BossSide];
				Other = Match->Duel.Sides[1 - BossSide];
				bSet = true;
				UE_LOG(LogTemp, Display, TEXT("CPUDUEL set: side %d さいきょう%s, side %d %s"), BossSide, bNoBoss ? TEXT("") : TEXT(" (duel play)"),
					1 - BossSide, bMirror ? TEXT("さいきょう") : TEXT("つよい"));
				UGameplayStatics::SetGlobalTimeDilation(World, 2.0f);
				return false;
			}
			// Hits each side takes (its health going down).
			for (const TWeakObjectPtr<APlayerState>& Who : {Strongest, Other})
			{
				const AChaosImpactCharacter* Body = Who.IsValid() ? Cast<AChaosImpactCharacter>(Who->GetPawn()) : nullptr;
				if (!Body)
				{
					continue;
				}
				const float Health = Body->IsEliminated() ? 0.0f : Body->GetHealth();
				float& Last = LastHealth.FindOrAdd(Who.Get(), Health);
				if (Health < Last)
				{
					HitsTaken.FindOrAdd(Who.Get()) += FMath::Max(1, FMath::RoundToInt(Last - Health));
				}
				Last = Health;
			}
			if (Match->Duel.Step != EChaosImpactDuelStep::Finished && Match->Phase != EChaosImpactOnlinePhase::Results)
			{
				return false;
			}
			UGameplayStatics::SetGlobalTimeDilation(World, 1.0f);
			const int32 Side = Match->Duel.SideOf(Strongest.Get());
			const int32 Mine = Side != INDEX_NONE ? Match->Duel.Wins[Side] : 0;
			const int32 Theirs = Side != INDEX_NONE ? Match->Duel.Wins[1 - Side] : 0;
			const TCHAR* Result = Match->Duel.MatchWinner == INDEX_NONE ? TEXT("draw") : Match->Duel.MatchWinner == Side ? TEXT("won") : TEXT("lost");
			UE_LOG(LogTemp, Display, TEXT("CPUDUEL result: さいきょう (side %d) %s %d - %d against %s (%d rounds; hits taken %d, dealt %d; %.0f s)"),
				Side, Result, Mine, Theirs, bMirror ? TEXT("さいきょう") : TEXT("つよい"), Match->Duel.Round,
				HitsTaken.FindRef(Strongest.Get()), HitsTaken.FindRef(Other.Get()), Now - StartedAt);
			Test->TestTrue(TEXT("Who played which side is known"), Side != INDEX_NONE);
			return true;
		}

	private:
		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		bool bSet = false;
		bool bMirror = false;
		TWeakObjectPtr<APlayerState> Strongest;
		TWeakObjectPtr<APlayerState> Other;
		TMap<APlayerState*, float> LastHealth;
		TMap<APlayerState*, int32> HitsTaken;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactCPUDuelStrengthTest, "ChaosImpact.Versus.CPUDuelStrength",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactCPUDuelStrengthTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FCPUDuelStrengthCommand(this));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactCPUDuelBossTest, "ChaosImpact.Versus.CPUDuelBoss",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactCPUDuelBossTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FCPUDuelStrengthCommand(this, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactCPUStrengthTest, "ChaosImpact.Versus.CPUStrength",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactCPUStrengthTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FCPUStrengthCommand(this));
	return true;
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
