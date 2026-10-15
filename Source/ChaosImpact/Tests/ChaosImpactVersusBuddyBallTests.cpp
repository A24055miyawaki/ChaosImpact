#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactBallFormComponent.h"
#include "ChaosImpactBuddyBall.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactPlayerController.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	/**
	 * Curling up into a ball in a local VS team battle (run on
	 * /Game/ThirdPerson/Lvl_ThirdPerson?CITraining=1?CITargets=0?CIVersus=1?CIMatch=1?CIMatchCPU=3?CITeams=2?CIStage=0):
	 * the player curls up (the shell pops out in the team's colour), an enemy cannot take it, a teammate can and throws
	 * it into an enemy (one point, the player uncurls); a CPU teammate left to itself walks over and picks it up (the
	 * ball kept, not destroyed). Pictures of each moment go to Saved/BuddyBallQA/Versus.
	 */
	class FVersusBuddyBallCommand : public IAutomationLatentCommand
	{
	public:
		explicit FVersusBuddyBallCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - StartedAt > 150.0)
			{
				Test->AddError(FString::Printf(TEXT("Stuck at step %d."), Step));
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
			if (!World || Now < NextAt)
			{
				return false;
			}
			AChaosImpactPlayerController* PC = Cast<AChaosImpactPlayerController>(World->GetFirstPlayerController());
			AChaosImpactCharacter* P = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			const AChaosImpactGameState* Match = World->GetGameState<AChaosImpactGameState>();
			const auto Shot = [](const TCHAR* Name)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("BuddyBallQA"), TEXT("Versus"), Name), true, false);
			};
			const auto Watch = [&](const FVector& Target, const FVector& From)
			{
				if (!Camera.IsValid())
				{
					Camera = World->SpawnActor<ACameraActor>(From, FRotator::ZeroRotator);
					Camera->GetCameraComponent()->SetFieldOfView(50.0f);
					Camera->GetCameraComponent()->bConstrainAspectRatio = false;
				}
				Camera->SetActorLocationAndRotation(From, (Target - From).Rotation());
				PC->SetViewTarget(Camera.Get());
			};
			const auto Ai = [World](AChaosImpactCharacter* Who, const bool bOn)
			{
				if (Who)
				{
					if (AController* C = Who->GetController())
					{
						C->SetActorTickEnabled(bOn);
					}
				}
			};
			switch (Step)
			{
			case 0:
			{
				// A team battle opens on its team select: start with the teams as they are.
				if (PC && Match && Match->Phase == EChaosImpactOnlinePhase::TeamSelect && !bStartAsked)
				{
					bStartAsked = true;
					PC->RequestStartTeamMatch();
				}
				if (!P || !Match || !Match->bVersusMatch || Match->Phase != EChaosImpactOnlinePhase::Match || Match->GetPhaseElapsedSeconds() < 1.0f)
				{
					if (Now - LastLog > 3.0)
					{
						LastLog = Now;
						UE_LOG(LogTemp, Display, TEXT("BUDDYVS waiting: pawn %d match %d versus %d phase %d elapsed %.1f"), P ? 1 : 0, Match ? 1 : 0,
							Match && Match->bVersusMatch ? 1 : 0, Match ? static_cast<int32>(Match->Phase) : -1, Match ? Match->GetPhaseElapsedSeconds() : 0.0f);
					}
					return false;
				}
				Test->TestTrue(TEXT("A team battle"), Match->IsTeamBattle());
				for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
				{
					if (*It == P)
					{
						continue;
					}
					Ai(*It, false);
					if (AChaosImpactGameState::AreTeammates(World, P, *It))
					{
						Mate = *It;
					}
					else if (!Enemy.IsValid())
					{
						Enemy = *It;
					}
				}
				if (!Test->TestNotNull(TEXT("A CPU teammate"), Mate.Get()) || !Test->TestNotNull(TEXT("An enemy"), Enemy.Get()))
				{
					return true;
				}
				Test->TestTrue(TEXT("Curling up is open in a local VS team battle"), P->GetBallForm()->IsAvailable());
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It)
				{
					It->Destroy();
				}
				Origin = P->GetActorLocation();
				P->ResetForOnlineMatch(Origin, FRotator::ZeroRotator);
				Mate->ResetForOnlineMatch(Origin + FVector(0.0f, 520.0f, 0.0f), FRotator::ZeroRotator);
				Enemy->ResetForOnlineMatch(Origin + FVector(1100.0f, -500.0f, 0.0f), FRotator(0.0f, 180.0f, 0.0f));
				Step = 1;
				NextAt = Now + 0.8;
				return false;
			}
			case 1:
				Test->TestTrue(TEXT("The player curls up"), P->GetBallForm()->Toggle());
				Buddy = P->GetBallForm()->GetFormBall();
				if (!Test->TestNotNull(TEXT("The ball"), Buddy.Get()))
				{
					return true;
				}
				Watch(Origin, Origin + FVector(-360.0f, -300.0f, 200.0f));
				Step = 2;
				NextAt = Now + 0.12;
				return false;
			case 2:
				Shot(TEXT("1_Curling.png"));
				Step = 3;
				NextAt = Now + 1.2;
				return false;
			case 3:
				Shot(TEXT("2_Waiting.png"));
				Test->TestFalse(TEXT("An enemy cannot take a curled-up player"), Buddy->ConsumePickup(Enemy.Get()));
				Mate->SetActorLocation(Buddy->GetActorLocation() + FVector(0.0f, 140.0f, 60.0f));
				{
					// The ball may already have taken the teammate standing by it (it picks itself up within reach).
					const bool bCan = Mate->GetBallForm()->CanCollect(P);
					const bool bHeld = Buddy->GetCarrier() == Mate.Get() || Buddy->ConsumePickup(Mate.Get());
					if (!bHeld)
					{
						UE_LOG(LogTemp, Warning, TEXT("BUDDYVS pickup refused: can collect %d, carried %d/%d, carrier %s"), bCan ? 1 : 0,
							Mate->GetCarriedBallCount(), Mate->GetMaximumCarriedBalls(), *GetNameSafe(Buddy->GetCarrier()));
					}
					Test->TestTrue(TEXT("A teammate picks them up"), bHeld);
				}
				{
					// The enemy in front of the teammate, toward the stage's middle (never off its edge).
					FVector Toward = FVector(-Mate->GetActorLocation().X, -Mate->GetActorLocation().Y, 0.0f).GetSafeNormal();
					Toward = Toward.IsNearlyZero() ? FVector::ForwardVector : Toward;
					Enemy->ResetForOnlineMatch(Mate->GetActorLocation() + Toward * 650.0f, Toward.Rotation() + FRotator(0.0f, 180.0f, 0.0f));
				}
				Watch(Mate->GetActorLocation(), Mate->GetActorLocation() + FVector(-380.0f, -300.0f, 220.0f));
				Step = 4;
				NextAt = Now + 0.6;
				return false;
			case 4:
				Shot(TEXT("3_Held.png"));
				Mate->SetAIAimDirection((Enemy->GetActorLocation() - Mate->GetActorLocation()).GetSafeNormal2D());
				Mate->BeginThrowInput();
				Step = 5;
				NextAt = Now + 0.6;
				return false;
			case 5:
				Mate->EndThrowInput();
				Watch((Mate->GetActorLocation() + Enemy->GetActorLocation()) * 0.5f,
					(Mate->GetActorLocation() + Enemy->GetActorLocation()) * 0.5f + FVector(-500.0f, -650.0f, 420.0f));
				Step = 6;
				NextAt = Now + 0.3;
				return false;
			case 6:
				UE_LOG(LogTemp, Display, TEXT("BUDDYVS flying: player %s at %s, enemy %s at %s, ball at %s v %s"), *P->GetName(),
					*P->GetActorLocation().ToCompactString(), *Enemy->GetName(), *Enemy->GetActorLocation().ToCompactString(),
					Buddy.IsValid() ? *Buddy->GetActorLocation().ToCompactString() : TEXT("-"),
					Buddy.IsValid() ? *Buddy->GetVelocity().ToCompactString() : TEXT("-"));
				Shot(TEXT("4_Flying.png"));
				Step = 7;
				NextAt = Now + 1.4;
				return false;
			case 7:
				UE_LOG(LogTemp, Display, TEXT("BUDDYVS after: player at %s hp %.0f out %d, enemy at %s hp %.0f out %d"),
					*P->GetActorLocation().ToCompactString(), P->GetHealth(), P->IsEliminated() ? 1 : 0,
					*Enemy->GetActorLocation().ToCompactString(), Enemy->GetHealth(), Enemy->IsEliminated() ? 1 : 0);
				Test->TestFalse(TEXT("The hit uncurls the player"), P->IsInBallForm());
				Test->TestEqual(TEXT("One point to the enemy"), Enemy->GetHealth(), Enemy->GetMaxHealth() - 1.0f);
				Watch(P->GetActorLocation(), P->GetActorLocation() + FVector(-420.0f, -260.0f, 220.0f));
				Shot(TEXT("5_Uncurled.png"));
				Step = 8;
				NextAt = Now + 1.0;
				return false;
			case 8:
				// A CPU teammate left to itself: it comes for the ball and keeps it (the passenger stays curled up).
				P->ResetForOnlineMatch(Origin, FRotator::ZeroRotator);
				Mate->ResetForOnlineMatch(Origin + FVector(0.0f, 600.0f, 0.0f), FRotator::ZeroRotator);
				Enemy->ResetForOnlineMatch(Origin + FVector(2400.0f, 1200.0f, 0.0f), FRotator::ZeroRotator);
				Step = 9;
				NextAt = Now + 0.8;
				return false;
			case 9:
				Test->TestTrue(TEXT("Curls up again"), P->GetBallForm()->Toggle());
				Buddy = P->GetBallForm()->GetFormBall();
				Ai(Mate.Get(), true);
				Step = 10;
				NextAt = Now + 0.5;
				return false;
			case 10:
				if (!Buddy.IsValid() || Buddy->GetCarrier() != Mate.Get())
				{
					if (Now - StartedAt > 140.0)
					{
						Test->AddError(TEXT("The CPU teammate never picked the ball up"));
						return true;
					}
					return false;
				}
				Ai(Mate.Get(), false);
				Test->TestTrue(TEXT("Still curled up in the CPU's hand"), P->IsInBallForm());
				Test->TestTrue(TEXT("The CPU holds the actual ball"),
					Mate->GetBallForm()->GetHeldBall(0) == Buddy.Get() || Mate->GetBallForm()->GetHeldBall(1) == Buddy.Get());
				Watch(Mate->GetActorLocation(), Mate->GetActorLocation() + FVector(-380.0f, -300.0f, 220.0f));
				Step = 11;
				NextAt = Now + 0.5;
				return false;
			case 11:
				Shot(TEXT("6_CPUHolds.png"));
				Step = 120;
				NextAt = Now + 0.3;
				return false;
			case 120:
				P->GetBallForm()->Toggle();
				Step = 12;
				NextAt = Now + 0.12;
				return false;
			case 12:
				Shot(TEXT("7_Pop.png"));
				Test->TestFalse(TEXT("Uncurls on the button"), P->IsInBallForm());
				PC->SetViewTarget(P);
				Step = 13;
				NextAt = Now + 0.5;
				return false;
			default:
				return true;
			}
		}

	private:
		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Step = 0;
		double LastLog = 0.0;
		bool bStartAsked = false;
		FVector Origin = FVector::ZeroVector;
		TWeakObjectPtr<AChaosImpactCharacter> Mate;
		TWeakObjectPtr<AChaosImpactCharacter> Enemy;
		TWeakObjectPtr<AChaosImpactBuddyBall> Buddy;
		TWeakObjectPtr<ACameraActor> Camera;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactVersusBuddyBallTest, "ChaosImpact.Versus.BuddyBall",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactVersusBuddyBallTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FVersusBuddyBallCommand(this));
	return true;
}

#endif
