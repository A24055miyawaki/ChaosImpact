#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactBall.h"
#include "ChaosImpactBallSpawner.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactHazardZone.h"
#include "ChaosImpactSimaeBird.h"
#include "ChaosImpactPlayerController.h"
#include "Camera/CameraActor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"

namespace
{
	/**
	 * The drive ball: thrown straight ahead at a CPU standing well off to the side, it is steered there with the
	 * movement controls (rooted thrower turning to watch it, camera on the ball) and bursts on it for one hit; a second
	 * one, steered round in circles (clear of the walls, which a special ball bursts on), keeps being steered though its
	 * thrower is hit, then fizzles out harmlessly when its time is up, and the camera comes back. A third snaps round;
	 * a dash lets go of it. A fourth, stopped with the throw button short of the CPU, ends there harmlessly (as on a
	 * wall). A fifth flies straight on through a normal ball (knocked away) and a fire ball (burst). Last, the drop
	 * button rolls the ball in hand out onto the floor. Training arena with one
	 * CPU (?CITraining=1?CICPUCount=1?CITargets=0). Screenshots go to Saved/DriveBallQA.
	 */
	class FDriveBallCommand : public IAutomationLatentCommand
	{
	public:
		explicit FDriveBallCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			AChaosImpactPlayerController* PC = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld())
				{
					PC = PC ? PC : Cast<AChaosImpactPlayerController>(Context.World()->GetFirstPlayerController());
				}
			}
			UWorld* World = PC ? PC->GetWorld() : nullptr;
			AChaosImpactCharacter* Player = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			if (Now - StartedAt > 110.0)
			{
				Test->AddError(FString::Printf(TEXT("Stuck at stage %d."), Stage));
				return true;
			}
			if (!World || !Player)
			{
				return false;
			}
			AChaosImpactCharacter* CPU = Target.Get();
			// The mouse aims straight ahead the whole time.
			if (Stage > 0)
			{
				FVector2D Screen;
				if (PC->ProjectWorldLocationToScreen(Origin + Toward * 500.0f, Screen))
				{
					PC->SetMouseLocation(FMath::RoundToInt(Screen.X), FMath::RoundToInt(Screen.Y));
				}
			}
			// While a ball is being steered toward the CPU, hold the controls that way (as a player would).
			if (bSteering && Player->IsDriving() && CPU)
			{
				if (const AChaosImpactBall* Driven = Player->GetDrivenBall())
				{
					const FRotator Yaw(0.0f, Player->GetCameraBoom()->GetComponentRotation().Yaw, 0.0f);
					const FVector Forward = FRotationMatrix(Yaw).GetUnitAxis(EAxis::X);
					const FVector Right = FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y);
					const FVector Want = (CPU->GetActorLocation() - Driven->GetActorLocation()).GetSafeNormal2D();
					Player->DoMove(static_cast<float>(FVector::DotProduct(Want, Right)), static_cast<float>(FVector::DotProduct(Want, Forward)));
					// Stopped well short of them with the throw button.
					if (bStopWhenNear && FVector::Dist2D(Driven->GetActorLocation(), CPU->GetActorLocation()) < 420.0f)
					{
						StoppedBall = Driven;
						Player->BeginThrowInput();
						Player->EndThrowInput();
						bStopWhenNear = false;
						bSteering = false;
					}
					MaxCameraOffset = FMath::Max(MaxCameraOffset, static_cast<float>(Player->GetCameraBoom()->TargetOffset.Size2D()));
					MaxThrowerMove = FMath::Max(MaxThrowerMove, static_cast<float>(FVector::Dist2D(Player->GetActorLocation(), Origin)));
				}
			}
			else if (bReversing && Player->IsDriving())
			{
				// Back the way it came.
				const FRotator Yaw(0.0f, Player->GetCameraBoom()->GetComponentRotation().Yaw, 0.0f);
				const FVector Back = -FirstHeading;
				Player->DoMove(static_cast<float>(FVector::DotProduct(Back, FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y))),
					static_cast<float>(FVector::DotProduct(Back, FRotationMatrix(Yaw).GetUnitAxis(EAxis::X))));
			}
			else if (bCircling && Player->IsDriving())
			{
				// The controls point where it should go: always off to its right, it goes round and round.
				if (const AChaosImpactBall* Driven = Player->GetDrivenBall())
				{
					const FRotator Yaw(0.0f, Player->GetCameraBoom()->GetComponentRotation().Yaw, 0.0f);
					const FVector Forward = FRotationMatrix(Yaw).GetUnitAxis(EAxis::X);
					const FVector Right = FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y);
					const FVector Want = Driven->GetVelocity().GetSafeNormal2D().RotateAngleAxis(90.0f, FVector::UpVector);
					Player->DoMove(static_cast<float>(FVector::DotProduct(Want, Right)), static_cast<float>(FVector::DotProduct(Want, Forward)));
				}
			}
			if (Now < NextAt)
			{
				return false;
			}
			const auto Give = [World, Player]()
			{
				const FTransform Where(FRotator::ZeroRotator, Player->GetActorLocation() + FVector(0, 0, 400));
				AChaosImpactBall* Pickup = World->SpawnActorDeferred<AChaosImpactBall>(AChaosImpactBall::StaticClass(), Where,
					nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				Pickup->SetBallType(EChaosImpactBallType::Drive);
				Pickup->FinishSpawning(Where);
				Pickup->MakePickup();
				const bool bTaken = Player->TryPickupBall(Pickup);
				Pickup->Destroy();
				return bTaken;
			};
			switch (Stage)
			{
			case 0:
			{
				if (!PC->IsGameplayActive() || !Player->GetCharacterMovement()->IsMovingOnGround())
				{
					return false;
				}
				for (TActorIterator<AChaosImpactCPUController> It(World); It; ++It)
				{
					CPU = Cast<AChaosImpactCharacter>(It->GetPawn());
					It->SetActorTickEnabled(false);
				}
				if (!CPU)
				{
					return false;
				}
				for (TActorIterator<AChaosImpactBallSpawner> It(World); It; ++It)
				{
					World->GetTimerManager().ClearAllTimersForObject(*It);
					It->Destroy();
				}
				// Whatever the CPU let loose before it was stopped (a flock, a fire trail) is cleared away too.
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It)
				{
					It->Destroy();
				}
				for (TActorIterator<AChaosImpactSimaeBird> It(World); It; ++It)
				{
					It->Destroy();
				}
				for (TActorIterator<AChaosImpactHazardZone> It(World); It; ++It)
				{
					It->Destroy();
				}
				Target = CPU;
				// Full health and empty hands, whatever the CPU did before it was stopped.
				Player->ResetForOnlineMatch(Player->GetActorLocation(), Player->GetActorRotation());
				Origin = Player->GetActorLocation();
				Toward = Player->GetActorForwardVector().GetSafeNormal2D();
				const FVector Side = FVector::CrossProduct(FVector::UpVector, Toward);
				// Well off the line of the throw: a straight ball would miss by far (and the turn stays clear of the
				// training arena's central bank wall, whose end is just beyond).
				CPU->ResetForOnlineMatch(Origin + Toward * 300.0f + Side * 650.0f, (-Toward).Rotation());
				Health = CPU->GetHealth();
				Test->TestTrue(TEXT("The player holds a drive ball"), Give());
				Stage = 1;
				NextAt = Now + 0.4;
				return false;
			}
			case 1:
			{
				// A close camera beside the player for the pose screenshots (charging, pushing out, steering).
				const FVector Side = FVector::CrossProduct(FVector::UpVector, Toward);
				const FVector Eye = Origin + Toward * 120.0f - Side * 270.0f + FVector(0.0f, 0.0f, 70.0f);
				ACameraActor* Close = World->SpawnActor<ACameraActor>(Eye, (Origin + Toward * 70.0f + FVector(0.0f, 0.0f, 30.0f) - Eye).Rotation());
				if (Close)
				{
					PC->SetViewTarget(Close);
					CloseCamera = Close;
				}
				Player->BeginThrowInput();
				Stage = 2;
				NextAt = Now + 0.25;
				return false;
			}
			case 2:
				Shoot(TEXT("0_Charging"));
				Player->EndThrowInput();
				bSteering = true;
				ThrownAt = Now;
				Stage = 40;
				NextAt = Now + 0.1;
				return false;
			case 40:
				Shoot(TEXT("0_Push"));
				Stage = 3;
				NextAt = Now + 0.4;
				return false;
			case 3:
				// (The screenshot just before can hold the game up a moment.)
				if (!Player->IsDriving() && Now - ThrownAt < 2.0)
				{
					return false;
				}
				Test->TestTrue(TEXT("Thrown, the player steers it"), Player->IsDriving());
				if (const AChaosImpactBall* Driven = Player->GetDrivenBall())
				{
					const float Facing = static_cast<float>(FVector::DotProduct(Player->GetActorForwardVector().GetSafeNormal2D(),
						(Driven->GetActorLocation() - Player->GetActorLocation()).GetSafeNormal2D()));
					UE_LOG(LogTemp, Display, TEXT("DRIVE thrower faces its ball: %.2f"), Facing);
					Test->TestTrue(TEXT("The thrower turns to watch its ball"), Facing > 0.9f);
				}
				Shoot(TEXT("1_Steering"));
				Stage = 4;
				return false;
			case 4:
				if (ACameraActor* Close = CloseCamera.Get())
				{
					PC->SetViewTarget(Player);
					Close->Destroy();
				}
				// Until it has burst on the CPU (or its time ran out).
				if (CPU && CPU->GetHealth() >= Health && Player->IsDriving() && Now - ThrownAt < 5.0)
				{
					return false;
				}
				UE_LOG(LogTemp, Display, TEXT("DRIVE steered hit after %.2f s: CPU health %.0f -> %.0f, camera out %.0f, thrower moved %.0f"),
					Now - ThrownAt, Health, CPU ? CPU->GetHealth() : -1.0f, MaxCameraOffset, MaxThrowerMove);
				Test->TestTrue(TEXT("Steered round, it hits the CPU off to the side"), CPU && CPU->GetHealth() == Health - 1.0f);
				Test->TestTrue(TEXT("The camera followed the ball"), MaxCameraOffset > 250.0f);
				Test->TestTrue(TEXT("The thrower stood still while steering"), MaxThrowerMove < 30.0f);
				Shoot(TEXT("2_Hit"));
				bSteering = false;
				Stage = 5;
				NextAt = Now + 0.4;
				return false;
			case 5:
				Test->TestFalse(TEXT("The hit ended the steering"), Player->IsDriving());
				// Another, steered round in circles until its time is up: it fizzles out.
				Health = CPU ? CPU->GetHealth() : 0.0f;
				Test->TestTrue(TEXT("The player holds another drive ball"), Give());
				Stage = 6;
				NextAt = Now + 0.4;
				return false;
			case 6:
				Player->BeginThrowInput();
				Stage = 7;
				NextAt = Now + 0.25;
				return false;
			case 7:
				Player->EndThrowInput();
				bCircling = true;
				ThrownAt = Now;
				Stage = 8;
				NextAt = Now + 1.0;
				return false;
			case 8:
				Test->TestTrue(TEXT("The second is steered too"), Player->IsDriving());
				Shoot(TEXT("3_Circling"));
				// Hit while steering: it keeps steering.
				PlayerHealth = Player->GetHealth();
				UGameplayStatics::ApplyDamage(Player, 1.0f, CPU ? CPU->GetController() : nullptr, CPU, nullptr);
				Stage = 30;
				NextAt = Now + 0.3;
				return false;
			case 30:
				UE_LOG(LogTemp, Display, TEXT("DRIVE thrower hit: health %.0f -> %.0f, still steering %d"), PlayerHealth,
					Player->GetHealth(), Player->IsDriving() ? 1 : 0);
				Test->TestTrue(TEXT("The thrower was hit"), Player->GetHealth() < PlayerHealth);
				Test->TestTrue(TEXT("Hit, it keeps steering"), Player->IsDriving());
				Stage = 9;
				return false;
			case 9:
				if (Player->IsDriving() && Now - ThrownAt < 8.0)
				{
					return false;
				}
				bCircling = false;
				UE_LOG(LogTemp, Display, TEXT("DRIVE control ended after %.2f s"), Now - ThrownAt);
				Test->TestTrue(TEXT("Steering lasts its time"), Now - ThrownAt > 5.6 && Now - ThrownAt < 6.6);
				Test->TestEqual(TEXT("Fizzling out hurts nobody"), CPU ? CPU->GetHealth() : -1.0f, Health);
				Stage = 10;
				NextAt = Now + 1.5;
				return false;
			case 10:
				UE_LOG(LogTemp, Display, TEXT("DRIVE camera back to %.0f"), Player->GetCameraBoom()->TargetOffset.Size2D());
				Test->TestTrue(TEXT("The camera comes back to the player"), Player->GetCameraBoom()->TargetOffset.Size2D() < 40.0f);
				// A third: sent back the way it came, it snaps round at once.
				Test->TestTrue(TEXT("The player holds a third drive ball"), Give());
				Stage = 11;
				NextAt = Now + 0.4;
				return false;
			case 11:
				Player->BeginThrowInput();
				Stage = 12;
				NextAt = Now + 0.25;
				return false;
			case 12:
				Player->EndThrowInput();
				Stage = 13;
				NextAt = Now + 0.6;
				return false;
			case 13:
				if (const AChaosImpactBall* Driven = Player->GetDrivenBall())
				{
					FirstHeading = Driven->GetVelocity().GetSafeNormal2D();
				}
				bReversing = true;
				Stage = 14;
				NextAt = Now + 0.12;
				return false;
			case 14:
			{
				const AChaosImpactBall* Driven = Player->GetDrivenBall();
				const float Dot = Driven ? static_cast<float>(FVector::DotProduct(Driven->GetVelocity().GetSafeNormal2D(), FirstHeading)) : 1.0f;
				UE_LOG(LogTemp, Display, TEXT("DRIVE reversal: heading against the first %.2f after 0.12 s"), Dot);
				Test->TestTrue(TEXT("Sent back the way it came, it snaps round at once"), Dot < -0.9f);
				Shoot(TEXT("4_Reversed"));
				bReversing = false;
				Stage = 15;
				NextAt = Now + 0.6;
				return false;
			}
			case 15:
				if (Player->IsDriving())
				{
					Player->RequestAIDash(-FirstHeading);
				}
				Stage = 16;
				NextAt = Now + 0.3;
				return false;
			case 16:
				Test->TestFalse(TEXT("A dash lets go of it"), Player->IsDriving());
				// A fourth: steered at the CPU and set off just short of it.
				Health = CPU ? CPU->GetHealth() : 0.0f;
				Test->TestTrue(TEXT("The player holds a fourth drive ball"), Give());
				Stage = 17;
				NextAt = Now + 1.2;
				return false;
			case 17:
				Player->BeginThrowInput();
				Stage = 18;
				NextAt = Now + 0.25;
				return false;
			case 18:
				Player->EndThrowInput();
				bSteering = true;
				bStopWhenNear = true;
				ThrownAt = Now;
				Stage = 19;
				NextAt = Now + 0.3;
				return false;
			case 19:
				if (bStopWhenNear && Now - ThrownAt < 5.0)
				{
					return false;
				}
				Stage = 20;
				NextAt = Now + 0.6;
				return false;
			case 20:
				UE_LOG(LogTemp, Display, TEXT("DRIVE stopped short of the CPU: health %.0f -> %.0f, ball ended %d"), Health,
					CPU ? CPU->GetHealth() : -1.0f, !StoppedBall.IsValid() || StoppedBall->HasDetonated() ? 1 : 0);
				Test->TestFalse(TEXT("Stopped short of the CPU, it was never let near"), bStopWhenNear);
				Test->TestEqual(TEXT("Stopped short, it hurts nobody (as on a wall)"), CPU ? CPU->GetHealth() : -1.0f, Health);
				Test->TestTrue(TEXT("Stopped, the ball ends there"), !StoppedBall.IsValid() || StoppedBall->HasDetonated());
				Test->TestFalse(TEXT("Stopping it ends the steering"), Player->IsDriving());
				Shoot(TEXT("5_Stopped"));
				// A fifth, thrown softly straight ahead, meets a normal ball and then a fire ball coming the other way.
				Test->TestTrue(TEXT("The player holds a fifth drive ball"), Give());
				Stage = 21;
				NextAt = Now + 0.6;
				return false;
			case 21:
				Player->BeginThrowInput();
				Stage = 22;
				NextAt = Now + 0.05;
				return false;
			case 22:
				Player->EndThrowInput();
				Stage = 23;
				NextAt = Now + 0.35;
				return false;
			case 23:
			{
				const AChaosImpactBall* Driven = Player->GetDrivenBall();
				Test->TestTrue(TEXT("The fifth is steered"), Driven != nullptr);
				if (!Driven)
				{
					Stage = 26;
					return false;
				}
				FirstHeading = Driven->GetVelocity().GetSafeNormal2D();
				const FVector At = Driven->GetActorLocation();
				const auto Oncoming = [&](const EChaosImpactBallType Type, const float Ahead)
				{
					const FTransform Where(FRotator::ZeroRotator, At + FirstHeading * Ahead);
					AChaosImpactBall* Ball = World->SpawnActorDeferred<AChaosImpactBall>(AChaosImpactBall::StaticClass(), Where,
						CPU, CPU, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
					Ball->SetBallType(Type);
					Ball->FinishSpawning(Where);
					Ball->Launch(-FirstHeading, 1500.0f, EChaosImpactBallFlightMode::Straight, 0.0f);
					return Ball;
				};
				OncomingNormal = Oncoming(EChaosImpactBallType::Normal, 380.0f);
				OncomingFire = Oncoming(EChaosImpactBallType::Fire, 700.0f);
				Stage = 24;
				NextAt = Now + 0.5;
				return false;
			}
			case 24:
			{
				const AChaosImpactBall* Driven = Player->GetDrivenBall();
				const float Dot = Driven ? static_cast<float>(FVector::DotProduct(Driven->GetVelocity().GetSafeNormal2D(), FirstHeading)) : -1.0f;
				UE_LOG(LogTemp, Display, TEXT("DRIVE through other balls: still steered %d, heading %.2f, normal knocked away %d, fire burst %d"),
					Player->IsDriving() ? 1 : 0, Dot, OncomingNormal.IsValid() && OncomingNormal->IsPickup() ? 1 : 0,
					!OncomingFire.IsValid() || OncomingFire->HasDetonated() ? 1 : 0);
				Test->TestTrue(TEXT("Other balls do not stop it"), Player->IsDriving() && Driven && !Driven->HasDetonated());
				Test->TestTrue(TEXT("Other balls do not turn it"), Dot > 0.97f);
				Test->TestTrue(TEXT("A normal ball it meets is knocked away"), OncomingNormal.IsValid() && OncomingNormal->IsPickup());
				Test->TestTrue(TEXT("A fire ball it meets bursts"), !OncomingFire.IsValid() || OncomingFire->HasDetonated());
				Shoot(TEXT("6_Through"));
				Player->StopDrivenBall();
				Stage = 25;
				NextAt = Now + 0.5;
				return false;
			}
			case 25:
				// The drop button: the ball in hand rolls out onto the floor.
				Test->TestTrue(TEXT("The player holds a ball to drop"), Give());
				Player->RequestDropBall();
				Stage = 26;
				NextAt = Now + 0.3;
				return false;
			case 26:
			{
				int32 Dropped = 0;
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It)
				{
					Dropped += It->IsPickup() && It->GetBallType() == EChaosImpactBallType::Drive
						&& FVector::Dist2D(It->GetActorLocation(), Player->GetActorLocation()) < 500.0f ? 1 : 0;
				}
				UE_LOG(LogTemp, Display, TEXT("DRIVE dropped: in hand %d, on the floor %d"), Player->GetCarriedBallCount(), Dropped);
				Test->TestEqual(TEXT("Dropped, the hand is empty"), Player->GetCarriedBallCount(), 0);
				Test->TestEqual(TEXT("Dropped, it lies on the floor in front"), Dropped, 1);
				Shoot(TEXT("7_Dropped"));
				Stage = 27;
				NextAt = Now + 0.5;
				return false;
			}
			default:
				return true;
			}
		}

	private:
		static void Shoot(const TCHAR* Name)
		{
			FScreenshotRequest::RequestScreenshot(
				FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DriveBallQA"), FString(Name) + TEXT(".png")), true, false);
		}

		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		double ThrownAt = 0.0;
		int32 Stage = 0;
		float Health = 0.0f;
		float PlayerHealth = 0.0f;
		float MaxCameraOffset = 0.0f;
		float MaxThrowerMove = 0.0f;
		bool bSteering = false;
		bool bCircling = false;
		bool bReversing = false;
		bool bStopWhenNear = false;
		TWeakObjectPtr<const AChaosImpactBall> StoppedBall;
		TWeakObjectPtr<AChaosImpactBall> OncomingNormal;
		TWeakObjectPtr<AChaosImpactBall> OncomingFire;
		FVector FirstHeading = FVector::ForwardVector;
		FVector Origin = FVector::ZeroVector;
		FVector Toward = FVector::ForwardVector;
		TWeakObjectPtr<AChaosImpactCharacter> Target;
		TWeakObjectPtr<ACameraActor> CloseCamera;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactDriveBallTest, "ChaosImpact.Training.DriveBall",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactDriveBallTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FDriveBallCommand(this));
	return true;
}

#endif
