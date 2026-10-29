#if WITH_DEV_AUTOMATION_TESTS
#include "ChaosImpactBallSpawner.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactDuelStage.h"
#include "ChaosImpactGameMode.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactPlayerController.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/DamageEvents.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/SpringArmComponent.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	/**
	 * A local duel against a CPU, or between two players sharing the screen (ランクマッチ's rules), run on
	 * /Game/ThirdPerson/Lvl_ThirdPerson?CITraining=1?CITargets=0?CILocalPlayers=1?CIVersus=1?CIMatch=1?CIDuel=1?CIMatchCPU=1
	 * (or CILocalPlayers=2?CIMatchCPU=0) with -CIMatchSeconds=10 (short rounds). The opponent is held still and the rounds are decided three ways:
	 * a knockout (the player), the time with more health left (the opponent), and sudden death's first hit (the player).
	 * The match must end 2 - 1 to the player. Screenshots in Saved/DuelQA.
	 */
	class FLocalDuelCommand : public IAutomationLatentCommand
	{
	public:
		explicit FLocalDuelCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - StartedAt > 170.0)
			{
				Test->AddError(FString::Printf(TEXT("Timed out at step %d"), Step));
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
			AChaosImpactCharacter* Me = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			const AChaosImpactGameState* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
			if (!Me || !Match || Now < NextAt)
			{
				return false;
			}
			// The other duellist: the CPU, or the second player sharing the screen (CILocalPlayers=2, CIMatchCPU=0).
			AChaosImpactCharacter* Cpu = nullptr;
			const int32 OwnSide = Match->Duel.SideOf(PC->PlayerState);
			if (OwnSide != INDEX_NONE && Match->Duel.Sides[1 - OwnSide])
			{
				Cpu = Cast<AChaosImpactCharacter>(Match->Duel.Sides[1 - OwnSide]->GetPawn());
			}
			if (Cpu)
			{
				// Held still the whole time, so the rounds go the way this test decides.
				Cpu->SetTrainingMenuFrozen(true);
			}
			const auto Shot = [this](const FString& Name)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DuelQA"), ShotPrefix + Name), true, false);
			};
			const auto Hurt = [](AChaosImpactCharacter* Victim, AChaosImpactCharacter* By, const float Amount)
			{
				if (Victim && By)
				{
					Victim->TakeDamage(Amount, FDamageEvent(), By->GetController(), By);
				}
			};
			const FChaosImpactDuelState& Duel = Match->Duel;
			switch (Step)
			{
			case 0:
				// The level started its duel before the test came in: started again from its opening.
				if (!bRestarted)
				{
					if (!Match->IsDuel())
					{
						return false;
					}
					if (AChaosImpactGameMode* Mode = World->GetAuthGameMode<AChaosImpactGameMode>())
					{
						Mode->ConfigureVersusMatch(Match->Rules);
					}
					bRestarted = true;
					return false;
				}
				// The opening on the duel arena.
				if (!Match->IsDuel() || Match->Phase != EChaosImpactOnlinePhase::Intro || Match->GetIntroElapsedSeconds() < 2.5f)
				{
					return false;
				}
				{
					bool bArena = false;
					for (TActorIterator<AChaosImpactDuelStage> It(World); It; ++It)
					{
						bArena = true;
					}
					Test->TestTrue(TEXT("The duel is played on the duel arena"), bArena);
					Test->TestTrue(TEXT("Two sides"), Duel.Sides[0] != nullptr && Duel.Sides[1] != nullptr);
					int32 SunCount = 0;
					for (TActorIterator<ADirectionalLight> It(World); It; ++It)
					{
						const auto* Sun = It->FindComponentByClass<UDirectionalLightComponent>();
						if (!Sun || !Sun->IsVisible()) { continue; }
						++SunCount;
						Test->TestTrue(TEXT("Duel illumination is almost overhead"), Sun->GetForwardVector().Z < -.999f);
						Test->TestTrue(TEXT("Duel shadows have a broad, soft light source"), Sun->LightSourceAngle >= 4.f);
					}
					Test->TestTrue(TEXT("Arena has a sun to illuminate the players"), SunCount > 0);
					const bool bPair = World->GetGameInstance() && World->GetGameInstance()->GetLocalPlayers().Num() > 1;
					Test->TestEqual(TEXT("A CPU only for a lone player"), Match->Rules.CPUCount, bPair ? 0 : 1);
					Test->TestEqual(TEXT("This player is side 0"), OwnSide, 0);
				}
				ShotPrefix = World->GetGameInstance() && World->GetGameInstance()->GetLocalPlayers().Num() > 1 ? TEXT("Pair_") : TEXT("Cpu_");
				Shot(TEXT("0_Opening.png"));
				++Step;
				return false;
			case 1:
				// The ball slot first: three different special kinds, never the nova or the prison, and the pads put
				// out only those (and normal balls) once it is over.
				if (Match->Phase == EChaosImpactOnlinePhase::Match && Duel.Step == EChaosImpactDuelStep::BallDraw)
				{
					const float DrawAge = Match->GetDuelStepElapsed();
					if (!bSlotSpinShot && DrawAge >= 0.9f)
					{
						bSlotSpinShot = true;
						Shot(TEXT("1a_SlotSpinning.png"));
					}
					if (!bSlotStopShot && DrawAge >= 3.4f)
					{
						bSlotStopShot = true;
						Shot(TEXT("1b_SlotStopped.png"));
						Test->TestEqual(TEXT("Three kinds drawn"), Duel.BallPicks.Num(), 3);
						TSet<uint8> Distinct(Duel.BallPicks);
						Test->TestEqual(TEXT("All different"), Distinct.Num(), Duel.BallPicks.Num());
						Test->TestFalse(TEXT("No nova"), Distinct.Contains(static_cast<uint8>(EChaosImpactBallType::Nova)));
						Test->TestFalse(TEXT("No prison"), Distinct.Contains(static_cast<uint8>(EChaosImpactBallType::Prison)));
						Test->TestFalse(TEXT("No normal (always there anyway)"), Distinct.Contains(static_cast<uint8>(EChaosImpactBallType::Normal)));
					}
					return false;
				}
				if (Match->Phase != EChaosImpactOnlinePhase::Match || Duel.Step != EChaosImpactDuelStep::RoundIntro || Match->GetDuelStepElapsed() < 0.3f)
				{
					return false;
				}
				Test->TestTrue(TEXT("The slot was shown"), bSlotSpinShot && bSlotStopShot);
				CheckCameras(Me, Cpu);
				for (TActorIterator<AChaosImpactBallSpawner> It(World); It; ++It)
				{
					// A pad of the duel arena (not one of the training arena's).
					if (FVector::Dist2D(It->GetActorLocation(), FVector(Match->StageCenter)) > 2500.0f)
					{
						continue;
					}
					for (int32 Type = 1; Type < ChaosImpactBallTypes::Count; ++Type)
					{
						const bool bDrawn = Duel.BallPicks.Contains(static_cast<uint8>(Type));
						Test->TestEqual(FString::Printf(TEXT("A pad puts out %s only when drawn"),
							ChaosImpactBallTypes::GetInternalName(static_cast<EChaosImpactBallType>(Type))),
							It->GetChance(static_cast<EChaosImpactBallType>(Type)) > 0.0f, bDrawn);
					}
					Test->TestTrue(TEXT("Normal balls too"), It->GetChance(EChaosImpactBallType::Normal) > 0.0f);
					break;
				}
				Shot(TEXT("1_Round1.png"));
				++Step;
				return false;
			case 2:
				if (Duel.Step != EChaosImpactDuelStep::Fight || Match->GetDuelStepElapsed() < 0.25f)
				{
					return false;
				}
				Shot(TEXT("2_Fight.png"));
				CheckScreenMovement(Me);
				if (Cpu)
				{
					Cpu->SetTrainingMenuFrozen(false);
					CheckScreenMovement(Cpu);
					Cpu->SetTrainingMenuFrozen(true);
				}
				NextAt = Now + 1.5;
				++Step;
				return false;
			case 3:
				// Round 1: the CPU knocked out (one hit first, to see the health on the scoreboard).
				Hurt(Cpu, Me, 1.0f);
				Shot(TEXT("3_CpuHit.png"));
				NextAt = Now + 0.6;
				++Step;
				return false;
			case 4:
				Hurt(Cpu, Me, 2.0f);
				++Step;
				return false;
			case 5:
				if (Duel.Step != EChaosImpactDuelStep::RoundOver || Match->GetDuelStepElapsed() < 0.35f)
				{
					return false;
				}
				Test->TestEqual(TEXT("Round 1 to the player by knockout"), Duel.RoundWinner, 0);
				Test->TestEqual(TEXT("Round 1 ended by a knockout"), static_cast<int32>(Duel.RoundEnd), static_cast<int32>(EChaosImpactDuelEnd::Knockout));
				Shot(TEXT("4_KO.png"));
				NextAt = Now + 1.6;
				++Step;
				return false;
			case 6:
				Shot(TEXT("5_RoundWin.png"));
				++Step;
				return false;
			case 7:
				// Round 2: the player loses a point of health and the time runs out.
				if (Duel.Round != 2 || Duel.Step != EChaosImpactDuelStep::Fight)
				{
					return false;
				}
				Test->TestTrue(TEXT("Both back at full health for round 2"), FMath::IsNearlyEqual(Me->GetHealth(), Me->GetMaxHealth())
					&& Cpu && !Cpu->IsEliminated() && FMath::IsNearlyEqual(Cpu->GetHealth(), Cpu->GetMaxHealth()));
				CheckCameras(Me, Cpu);
				Hurt(Me, Cpu, 1.0f);
				++Step;
				return false;
			case 8:
				if (Duel.Step != EChaosImpactDuelStep::RoundOver)
				{
					return false;
				}
				Test->TestEqual(TEXT("Round 2 to the CPU on health"), Duel.RoundWinner, 1);
				Test->TestEqual(TEXT("Round 2 ended on time"), static_cast<int32>(Duel.RoundEnd), static_cast<int32>(EChaosImpactDuelEnd::Time));
				Shot(TEXT("6_Time.png"));
				++Step;
				return false;
			case 9:
				// Round 3: level at the bell, then sudden death.
				if (Duel.Step != EChaosImpactDuelStep::RoundIntro || Duel.Round != 3 || Match->GetDuelStepElapsed() < 0.3f)
				{
					return false;
				}
				Shot(TEXT("7_FinalRound.png"));
				++Step;
				return false;
			case 10:
				if (Duel.Step != EChaosImpactDuelStep::SuddenDeath || Match->GetDuelStepElapsed() < 0.4f)
				{
					return false;
				}
				Shot(TEXT("8_SuddenDeath.png"));
				NextAt = Now + 1.5;
				++Step;
				return false;
			case 11:
				Hurt(Cpu, Me, 1.0f);
				++Step;
				return false;
			case 12:
				if (Duel.Step != EChaosImpactDuelStep::RoundOver && Match->Phase != EChaosImpactOnlinePhase::Results)
				{
					return false;
				}
				Test->TestEqual(TEXT("Round 3 to the player in sudden death"), Duel.RoundWinner, 0);
				Test->TestEqual(TEXT("Round 3 ended by the first hit"), static_cast<int32>(Duel.RoundEnd),
					static_cast<int32>(EChaosImpactDuelEnd::SuddenDeath));
				++Step;
				return false;
			case 13:
				if (Match->Phase != EChaosImpactOnlinePhase::Results || Match->GetPhaseElapsedSeconds() < 0.35f)
				{
					return false;
				}
				Test->TestEqual(TEXT("The player wins the match"), Duel.MatchWinner, 0);
				Test->TestTrue(TEXT("2 - 1"), Duel.Wins[0] == 2 && Duel.Wins[1] == 1);
				Test->TestEqual(TEXT("The winner tops the ranking"), Match->GetRanking().IsEmpty() ? nullptr
					: static_cast<APlayerState*>(Match->GetRanking()[0]), Duel.Sides[0].Get());
				Shot(TEXT("9_YouWin.png"));
				NextAt = Now + 6.0;
				++Step;
				return false;
			case 14:
				Shot(TEXT("10_Results.png"));
				NextAt = Now + 0.5;
				++Step;
				return false;
			case 15:
				// Ordinary VS must restore the authored view, without changing either pawn or its world aim.
				if (AChaosImpactGameState* MutableMatch = World->GetGameState<AChaosImpactGameState>())
				{
					MutableMatch->Rules.bDuel = false;
				}
				NextAt = Now + 0.3;
				++Step;
				return false;
			case 16:
				if (Cpu)
				{
					Test->TestTrue(TEXT("Ordinary VS restores matching camera angles"),
						Me->GetCameraBoom()->GetComponentRotation().Equals(Cpu->GetCameraBoom()->GetComponentRotation(), 0.1f));
				}
				if (AChaosImpactGameState* MutableMatch = World->GetGameState<AChaosImpactGameState>())
				{
					MutableMatch->Rules.bDuel = true;
				}
				return true;
			default:
				return true;
			}
		}

	private:
		void CheckCameras(AChaosImpactCharacter* First, AChaosImpactCharacter* Other)
		{
			if (!First || !Other) { Test->AddError(TEXT("Missing duellist for camera checks")); return; }
			const FRotator FirstView = First->GetCameraBoom()->GetComponentRotation();
			const FRotator OtherView = Other->GetCameraBoom()->GetComponentRotation();
			Test->TestTrue(TEXT("Duel cameras face opposite ends"),
				FMath::IsNearlyEqual(FMath::Abs(FMath::FindDeltaAngleDegrees(FirstView.Yaw, OtherView.Yaw)), 180.f, 0.1f));
			Test->TestTrue(TEXT("Both cameras retain the same top-down pitch"), FMath::IsNearlyEqual(FirstView.Pitch, OtherView.Pitch, 0.1f));
			for (AChaosImpactCharacter* Viewer : {First, Other})
			{
				const AChaosImpactCharacter* Opponent = Viewer == First ? Other : First;
				const FVector TowardOpponent = (Opponent->GetActorLocation() - Viewer->GetActorLocation()).GetSafeNormal2D();
				Test->TestTrue(TEXT("Each player sees the opponent ahead, toward screen top"),
					FVector::DotProduct(Viewer->GetCameraBoom()->GetForwardVector().GetSafeNormal2D(), TowardOpponent) > 0.95f);
			}
		}
		void CheckScreenMovement(AChaosImpactCharacter* Pawn)
		{
			const FRotator CameraYaw(0.f, Pawn->GetCameraBoom()->GetComponentRotation().Yaw, 0.f);
			Pawn->ConsumeMovementInputVector();
			Pawn->DoMove(0.f, 1.f);
			Test->TestTrue(TEXT("Forward movement follows this player's screen"),
				Pawn->ConsumeMovementInputVector().GetSafeNormal().Equals(CameraYaw.Vector(), 0.01f));
			Pawn->DoMove(1.f, 0.f);
			Test->TestTrue(TEXT("Right movement follows this player's screen"),
				Pawn->ConsumeMovementInputVector().GetSafeNormal().Equals(FRotationMatrix(CameraYaw).GetUnitAxis(EAxis::Y), 0.01f));
		}
		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Step = 0;
		bool bRestarted = false;
		bool bSlotSpinShot = false;
		bool bSlotStopShot = false;
		FString ShotPrefix;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactLocalDuelTest, "ChaosImpact.Match.LocalDuel",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FChaosImpactLocalDuelTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FLocalDuelCommand(this));
	return true;
}
#endif
