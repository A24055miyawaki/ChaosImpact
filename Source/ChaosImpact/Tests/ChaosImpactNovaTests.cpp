#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactBall.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactHazardZone.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactTornado.h"
#include "Engine/Engine.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	AChaosImpactPlayerController* FindNovaController()
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World() && Context.World()->IsGameWorld())
			{
				if (auto* PC = Cast<AChaosImpactPlayerController>(Context.World()->GetFirstPlayerController()))
				{
					return PC;
				}
			}
		}
		return nullptr;
	}

	void CaptureNova(const TCHAR* Name)
	{
		FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("NovaQA"), Name), true, false);
	}

	/**
	 * The nova (a long charge rooted to the spot, swelling overhead, a landing area, a great blast) and the snowball
	 * rolled along the ground (lifted overhead when big, taken into the hand when small), in the training arena with
	 * one CPU (?CITraining=1?CICPUCount=1?CITargets=0). Screenshots go to Saved/NovaQA.
	 */
	class FNovaCommand : public IAutomationLatentCommand
	{
	public:
		explicit FNovaCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			AChaosImpactPlayerController* PC = FindNovaController();
			UWorld* World = PC ? PC->GetWorld() : nullptr;
			AChaosImpactCharacter* Player = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			if (!World || !Player)
			{
				return Waited(Now, TEXT("No training player was created."));
			}
			// Keep the ordinary aim facing the open way. During a nova charge, live play moves its cursor with WASD/left stick.
			if (Stage > 0)
			{
				FVector2D Screen;
				const float AimDistance = Player->IsChargingNova() ? 1800.0f : 450.0f;
				if (PC->ProjectWorldLocationToScreen(Player->GetActorLocation() + Toward * AimDistance, Screen))
				{
					PC->SetMouseLocation(FMath::RoundToInt(Screen.X), FMath::RoundToInt(Screen.Y));
				}
			}
			if (Now < NextAt)
			{
				// While charging a nova, try to walk: it must not move.
				if (Stage == 1)
				{
					Player->DoMove(0.0f, 1.0f);
				}
				return false;
			}
			AChaosImpactCharacter* CPU = CPUCharacter.Get();
			if (Stage > 0 && !CPU)
			{
				Test->AddError(TEXT("The CPU character disappeared."));
				return true;
			}
			const auto Restore = [](AChaosImpactCharacter* Character, const FVector& Location)
			{
				Character->ResetForOnlineMatch(Character->GetActorLocation(), Character->GetActorRotation());
				Character->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
			};
			const auto Give = [World](AChaosImpactCharacter* Character, const EChaosImpactBallType Type)
			{
				const FTransform Where(FRotator::ZeroRotator, Character->GetActorLocation() + FVector(0, 0, 400));
				AChaosImpactBall* Pickup = World->SpawnActorDeferred<AChaosImpactBall>(AChaosImpactBall::StaticClass(), Where,
					nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				if (!Pickup)
				{
					return false;
				}
				Pickup->SetBallType(Type);
				Pickup->FinishSpawning(Where);
				Pickup->MakePickup();
				const bool bTaken = Character->TryPickupBall(Pickup);
				Pickup->Destroy();
				return bTaken;
			};
			const auto FindThrown = [World, Player](const EChaosImpactBallType Type) -> AChaosImpactBall*
			{
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It)
				{
					if (It->GetBallType() == Type && It->WasThrownBy(Player) && !It->IsPickup() && !It->HasDetonated())
					{
						return *It;
					}
				}
				return nullptr;
			};

			switch (Stage)
			{
			case 0:
			{
				for (TActorIterator<AChaosImpactCPUController> It(World); It; ++It)
				{
					CPUCharacter = Cast<AChaosImpactCharacter>(It->GetPawn());
					It->SetActorTickEnabled(false);
				}
				CPU = CPUCharacter.Get();
				bool bWindAbout = false;
				for (TActorIterator<AChaosImpactTornado> It(World); It; ++It)
				{
					bWindAbout = true;
				}
				if (!CPU || !PC->IsGameplayActive() || Player->IsEliminated() || CPU->IsEliminated()
					|| !Player->GetCharacterMovement()->IsMovingOnGround() || bWindAbout)
				{
					return Waited(Now, TEXT("Run with ?CITraining=1?CICPUCount=1 so a CPU exists."));
				}
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It)
				{
					if (!It->IsPickup() && !It->GetAttachParentActor())
					{
						It->Destroy();
					}
				}
				Origin = Player->GetActorLocation();
				ChooseOpenDirection(World, Player);
				Player->ResetForOnlineMatch(Origin, Toward.Rotation());
				Restore(CPU, Origin + FVector::CrossProduct(FVector::UpVector, Toward) * 1500.0f);
				Test->TestTrue(TEXT("A nova can be picked up"), Give(Player, EChaosImpactBallType::Nova));
				Test->TestEqual(TEXT("It is in the right hand"), Player->GetCarriedBallType(0), EChaosImpactBallType::Nova);
				Stage = 10;
				NextAt = Now + 0.4;
				return false;
			}
			case 10:
				NormalArm = Player->GetCameraBoom()->TargetArmLength;
				CaptureNova(TEXT("NV-00-Held.png"));
				Stage = 11;
				NextAt = Now + 0.15;
				return false;
			case 11:
				WalkSpeed = Player->GetCharacterMovement()->MaxWalkSpeed;
				Player->BeginThrowInput();
				Test->TestTrue(TEXT("Charging a nova"), Player->IsChargingNova());
				ChargeFrom = Player->GetActorLocation();
				ChargeBegan = Now;
				Stage = 12;
				NextAt = Now + 0.6;
				return false;
			case 12:
				// Arms up, the nova still small over the head: seen from the side for a moment.
				Player->GetCameraBoom()->SetWorldRotation(FRotator(-12.0f, Toward.Rotation().Yaw + 90.0f, 0.0f));
				Stage = 13;
				NextAt = Now + 0.35;
				return false;
			case 13:
				CaptureNova(TEXT("NV-00b-ArmsUp.png"));
				Stage = 14;
				NextAt = Now + 0.1;
				return false;
			case 14:
				Player->GetCameraBoom()->SetWorldRotation(FRotator(-60.0f, 0.0f, 0.0f));
				Stage = 1;
				NextAt = ChargeBegan + 1.3;
				return false;
			case 1:
			{
				const double Charged = Now - ChargeBegan;
				if (Shots == 0)
				{
					Test->TestEqual(TEXT("Charging a nova roots its thrower"), Player->GetCharacterMovement()->MaxWalkSpeed, 0.0f);
					Test->TestTrue(TEXT("Nova charge uses a freely selected stage point"), Player->HasNovaTargetPoint()
						&& FVector::Dist2D(Player->GetNovaTargetPoint(), Player->GetActorLocation()) > 1200.0f);
					Test->TestTrue(TEXT("Nova charge shows the whole-stage overview"),
						Player->GetCameraBoom()->TargetArmLength > NormalArm + 1000.0f
						&& Player->GetCameraBoom()->GetComponentRotation().Pitch < -80.0f);
					UE_LOG(LogTemp, Display, TEXT("NOVA charge %.2f after %.1fs"), Player->GetThrowChargeAlpha(), Charged);
					Test->TestTrue(TEXT("A nova's charge is long"), Player->GetThrowChargeAlpha() < 0.5f);
					CaptureNova(TEXT("NV-01-Charging.png"));
					++Shots;
					NextAt = Now + 1.3;
					return false;
				}
				if (Shots == 1)
				{
					CaptureNova(TEXT("NV-02-Swelling.png"));
					++Shots;
					NextAt = ChargeBegan + ChaosImpactBallTypes::NovaChargeSeconds + 0.4;
					return false;
				}
				Test->TestTrue(TEXT("The thrower stayed where it stood"), FVector::Dist2D(Player->GetActorLocation(), ChargeFrom) < 10.0f);
				Test->TestEqual(TEXT("Fully charged"), Player->GetThrowChargeAlpha(), 1.0f);
				float Radius = 0.0f;
				const bool bLands = Player->PredictThrowLanding(1.0f, PredictedLanding, Radius);
				Test->TestTrue(TEXT("Its landing is shown"), bLands);
				Test->TestTrue(TEXT("The area is the blast's"), FMath::IsNearlyEqual(Radius,
					ChaosImpactBallTypes::GetNovaBlastRadius(ChaosImpactBallTypes::NovaMaxScale), 1.0f));
				UE_LOG(LogTemp, Display, TEXT("NOVA predicted landing %.0f away, area %.0f, aim (%.2f %.2f) toward (%.2f %.2f)"),
					FVector::Dist2D(PredictedLanding, Player->GetActorLocation()), Radius, Player->GetAimDirection().X,
					Player->GetAimDirection().Y, Toward.X, Toward.Y);
				// Someone standing where it will come down.
				Restore(CPU, PredictedLanding + FVector(0, 0, 100) + FVector::CrossProduct(FVector::UpVector, Toward) * 300.0f);
				CPUHealth = CPU->GetHealth();
				Stage = 2;
				NextAt = Now + 0.4;
				return false;
			}
			case 2:
				CaptureNova(TEXT("NV-03-FullCharge.png"));
				Stage = 29;
				NextAt = Now + 0.15;
				return false;
			case 29:
			{
				// Where it will land now, as it is let go (the aim may have been drawn onto the CPU meanwhile).
				float Radius = 0.0f;
				Player->PredictThrowLanding(1.0f, PredictedLanding, Radius);
				Player->EndThrowInput();
			}
				Stage = 3;
				NextAt = Now + 0.45;
				return false;
			case 3:
			{
				const AChaosImpactBall* Nova = FindThrown(EChaosImpactBallType::Nova);
				Test->TestNotNull(TEXT("The nova is thrown"), Nova);
				if (Nova)
				{
					Test->TestTrue(TEXT("As big as it was charged"), FMath::IsNearlyEqual(Nova->GetSnowScale(), ChaosImpactBallTypes::NovaMaxScale, 0.1f));
				}
				Test->TestFalse(TEXT("No longer rooted once thrown"), Player->IsChargingNova());
				CaptureNova(TEXT("NV-04-Thrown.png"));
				Stage = 4;
				PhaseStartedAt = Now;
				return false;
			}
			case 4:
			{
				AChaosImpactHazardZone* Blast = nullptr;
				for (TActorIterator<AChaosImpactHazardZone> It(World); It; ++It)
				{
					Blast = It->GetZoneType() == EChaosImpactBallType::Nova ? *It : Blast;
				}
				if (!Blast)
				{
					if (Now - PhaseStartedAt > 5.0)
					{
						Test->AddError(TEXT("The nova never burst."));
						return true;
					}
					return false;
				}
				UE_LOG(LogTemp, Display, TEXT("NOVA burst %.0f from where it was shown to land, CPU health %.0f -> %.0f"),
					FVector::Dist2D(Blast->GetActorLocation(), PredictedLanding), CPUHealth, CPU->GetHealth());
				// Right where it was shown, even with the CPU under its way.
				Test->TestTrue(TEXT("It lands where it was shown to"), FVector::Dist2D(Blast->GetActorLocation(), PredictedLanding) < 40.0f);
				Test->TestTrue(TEXT("A nova always knocks out a player it catches"), CPU->IsEliminated());
				Stage = 5;
				Shots = 0;
				NextAt = Now + 0.12;
				return false;
			}
			case 5:
			{
				const TCHAR* Names[] = {TEXT("NV-05-Blast.png"), TEXT("NV-06-Dome.png"), TEXT("NV-07-After.png")};
				CaptureNova(Names[Shots]);
				++Shots;
				NextAt = Now + (Shots == 1 ? 0.35 : 0.8);
				if (Shots >= 3)
				{
					Stage = 20;
					NextAt = Now + 2.5;
				}
				return false;
			}
			case 20:
				// Well after the blast the camera is back where it was.
				UE_LOG(LogTemp, Display, TEXT("NOVA camera after the throw %.0f (normal %.0f)"), Player->GetCameraBoom()->TargetArmLength, NormalArm);
				Test->TestTrue(TEXT("The camera comes back after a nova"), FMath::Abs(Player->GetCameraBoom()->TargetArmLength - NormalArm) < 40.0f);
				// Snow: rolled along the ground in front, growing as it goes.
				Restore(CPU, Origin + FVector::CrossProduct(FVector::UpVector, Toward) * 1500.0f);
				Player->ResetForOnlineMatch(Origin, Toward.Rotation());
				Test->TestTrue(TEXT("A snowball can be picked up"), Give(Player, EChaosImpactBallType::Snow));
				SnowStart = Player->GetActorLocation();
				WalkSteps = 0;
				Stage = 21;
				return false;
			case 21:
				if (WalkSteps < 36)
				{
					const float Along = 100.0f * (WalkSteps % 12 < 6 ? WalkSteps % 6 : 6 - WalkSteps % 6);
					Player->SetActorLocation(SnowStart + Toward * Along, false, nullptr, ETeleportType::TeleportPhysics);
					++WalkSteps;
					return false;
				}
				Stage = 22;
				NextAt = Now + 0.6;
				return false;
			case 22:
				CaptureNova(TEXT("NV-08-SnowRolled.png"));
				Stage = 220;
				NextAt = Now + 0.15;
				return false;
			case 220:
				Player->BeginThrowInput();
				Stage = 23;
				NextAt = Now + 0.6;
				return false;
			case 23:
			{
				CaptureNova(TEXT("NV-09-SnowLifted.png"));
				// A snowball aims with the arrow alone: the camera stays put.
				Test->TestTrue(TEXT("The camera does not move for a snowball"), FMath::Abs(Player->GetCameraBoom()->TargetArmLength - NormalArm) < 5.0f
					&& Player->GetCameraBoom()->TargetOffset.IsNearlyZero(1.0f));
				Stage = 230;
				NextAt = Now + 0.15;
				return false;
			}
			case 230:
				Player->EndThrowInput();
				Stage = 24;
				NextAt = Now + 0.12;
				return false;
			case 24:
			{
				const AChaosImpactBall* Snow = FindThrown(EChaosImpactBallType::Snow);
				Test->TestTrue(TEXT("A big snowball leaves from over the head"),
					Snow && Snow->GetActorLocation().Z > Player->GetActorLocation().Z + 150.0f);
				Stage = 25;
				NextAt = Now + 1.5;
				return false;
			}
			case 25:
				Player->ResetForOnlineMatch(Origin, Toward.Rotation());
				Test->TestTrue(TEXT("A small snowball can be picked up"), Give(Player, EChaosImpactBallType::Snow));
				Stage = 26;
				NextAt = Now + 0.5;
				return false;
			case 26:
				CaptureNova(TEXT("NV-10-SmallRolled.png"));
				Stage = 260;
				NextAt = Now + 0.15;
				return false;
			case 260:
				Player->BeginThrowInput();
				Stage = 27;
				NextAt = Now + 0.5;
				return false;
			case 27:
				CaptureNova(TEXT("NV-11-SmallInHand.png"));
				Stage = 270;
				NextAt = Now + 0.15;
				return false;
			case 270:
				Player->EndThrowInput();
				Stage = 28;
				NextAt = Now + 0.1;
				return false;
			case 28:
			{
				const AChaosImpactBall* Snow = FindThrown(EChaosImpactBallType::Snow);
				Test->TestTrue(TEXT("A small snowball is thrown from the hand"),
					Snow && Snow->GetActorLocation().Z < Player->GetActorLocation().Z + 120.0f);
				Stage = 30;
				NextAt = Now + 1.2;
				return false;
			}
			case 30:
				// A nova's charge called off: free to move again, the nova still in hand.
				Player->ResetForOnlineMatch(Origin, Toward.Rotation());
				Test->TestTrue(TEXT("Another nova"), Give(Player, EChaosImpactBallType::Nova));
				Player->BeginThrowInput();
				Stage = 31;
				NextAt = Now + 0.8;
				return false;
			case 31:
				Player->RequestCancelThrow();
				Player->EndThrowInput();
				Test->TestFalse(TEXT("Called off, no longer charging"), Player->IsChargingNova());
				Test->TestEqual(TEXT("Still in hand"), Player->GetCarriedBallType(0), EChaosImpactBallType::Nova);
				Stage = 32;
				NextAt = Now + 1.5;
				return false;
			case 32:
				Test->TestEqual(TEXT("Free to walk again"), Player->GetCharacterMovement()->MaxWalkSpeed, WalkSpeed);
				UE_LOG(LogTemp, Display, TEXT("NOVA camera after calling it off %.0f (normal %.0f)"), Player->GetCameraBoom()->TargetArmLength, NormalArm);
				Test->TestTrue(TEXT("The camera comes back after calling a nova off"), FMath::Abs(Player->GetCameraBoom()->TargetArmLength - NormalArm) < 40.0f);
				// Knocked out in the middle of charging one.
				Player->BeginThrowInput();
				Stage = 34;
				NextAt = Now + 2.0;
				return false;
			case 34:
				Test->TestTrue(TEXT("Charging again"), Player->IsChargingNova());
				Player->ApplyBlind(3.0f);
				Test->TestTrue(TEXT("In smoke"), Player->GetBlindAmount() > 0.5f);
				UGameplayStatics::ApplyDamage(Player, 10.0f, nullptr, nullptr, nullptr);
				Test->TestTrue(TEXT("Knocked out"), Player->IsEliminated());
				Test->TestEqual(TEXT("Going down clears the smoke at once"), Player->GetBlindAmount(), 0.0f);
				PhaseStartedAt = Now;
				Stage = 35;
				return false;
			case 35:
				if (Player->IsEliminated())
				{
					if (Now - PhaseStartedAt > 10.0)
					{
						Test->AddError(TEXT("The player never came back."));
						return true;
					}
					return false;
				}
				Stage = 36;
				NextAt = Now + 0.8;
				return false;
			case 36:
				UE_LOG(LogTemp, Display, TEXT("NOVA camera after going down %.0f (normal %.0f)"), Player->GetCameraBoom()->TargetArmLength, NormalArm);
				Test->TestTrue(TEXT("The camera comes back after going down with a nova"), FMath::Abs(Player->GetCameraBoom()->TargetArmLength - NormalArm) < 40.0f
					&& Player->GetCameraBoom()->TargetOffset.IsNearlyZero(5.0f));
				// A CPU with a nova charges it up and throws it too.
				Player->ResetForOnlineMatch(Origin, Toward.Rotation());
				Restore(CPU, Origin + Toward * 1500.0f);
				Test->TestTrue(TEXT("The CPU takes a nova"), Give(CPU, EChaosImpactBallType::Nova));
				for (TActorIterator<AChaosImpactCPUController> It(World); It; ++It)
				{
					It->SetActorTickEnabled(true);
				}
				PhaseStartedAt = Now;
				Shots = 0;
				Stage = 33;
				return false;
			case 33:
			{
				if (Shots == 0 && CPU->IsChargingNova() && Now - PhaseStartedAt > 3.0)
				{
					CaptureNova(TEXT("NV-12-CPUCharging.png"));
					++Shots;
				}
				bool bThrown = false;
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It)
				{
					bThrown |= It->GetBallType() == EChaosImpactBallType::Nova && It->WasThrownBy(CPU) && !It->IsPickup();
				}
				if (bThrown)
				{
					UE_LOG(LogTemp, Display, TEXT("NOVA the CPU threw its nova after %.1fs"), Now - PhaseStartedAt);
					return true;
				}
				if (Now - PhaseStartedAt > 12.0)
				{
					Test->AddError(TEXT("The CPU never threw its nova."));
					return true;
				}
				return false;
			}
			default:
				return true;
			}
		}

	private:
		bool Waited(const double Now, const TCHAR* Error)
		{
			if (Now - StartedAt < 60.0)
			{
				return false;
			}
			Test->AddError(Error);
			return true;
		}

		/** The way with the most open, level floor ahead of the player. */
		void ChooseOpenDirection(UWorld* World, AActor* Player)
		{
			FCollisionQueryParams Params(SCENE_QUERY_STAT(NovaTestOpen), false, Player);
			const auto FloorAt = [&](const FVector& Where)
			{
				FHitResult Floor;
				return World->LineTraceSingleByObjectType(Floor, Where + FVector(0, 0, 50), Where - FVector(0, 0, 600),
					FCollisionObjectQueryParams(ECC_WorldStatic), Params) ? static_cast<float>(Floor.ImpactPoint.Z) : -1.0e6f;
			};
			const float Ground = FloorAt(Origin);
			float BestOpen = -1.0f;
			for (int32 Turn = 0; Turn < 24; ++Turn)
			{
				const FVector Candidate = FRotator(0.0f, Turn * 15.0f, 0.0f).Vector();
				FHitResult Blocked;
				const float Open = World->SweepSingleByObjectType(Blocked, Origin, Origin + Candidate * 2600.0f, FQuat::Identity,
					FCollisionObjectQueryParams(ECC_WorldStatic), FCollisionShape::MakeSphere(90.0f), Params)
					? static_cast<float>(Blocked.Distance) : 2600.0f;
				bool bLevel = true;
				for (const float Along : {400.0f, 1000.0f, 1800.0f})
				{
					bLevel &= FMath::Abs(FloorAt(Origin + Candidate * Along) - Ground) < 30.0f;
				}
				if (bLevel && Open > BestOpen)
				{
					BestOpen = Open;
					Toward = Candidate;
				}
			}
		}

		FAutomationTestBase* Test;
		FVector Toward = FVector(1.0f, 0.0f, 0.0f);
		FVector Origin = FVector::ZeroVector;
		FVector ChargeFrom = FVector::ZeroVector;
		FVector PredictedLanding = FVector::ZeroVector;
		FVector SnowStart = FVector::ZeroVector;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		double ChargeBegan = 0.0;
		double PhaseStartedAt = 0.0;
		int32 Stage = 0;
		int32 Shots = 0;
		int32 WalkSteps = 0;
		float CPUHealth = 0.0f;
		float WalkSpeed = 0.0f;
		float NormalArm = 0.0f;
		TWeakObjectPtr<AChaosImpactCharacter> CPUCharacter;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactNovaTest, "ChaosImpact.Training.Nova",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactNovaTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FNovaCommand(this));
	return true;
}

#endif
