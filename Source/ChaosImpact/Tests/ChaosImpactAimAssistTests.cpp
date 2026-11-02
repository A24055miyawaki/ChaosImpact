#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactCharacter.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"

namespace
{
	/**
	 * A mouse aims exactly (2026-11): with an opponent 28 m off and 20 degrees from where the mouse points, a charged
	 * throw's aim stays on the cursor (the charge magnet is for sticks), and the drawn arrow points straight at the
	 * cursor on screen. Training arena with one CPU (?CITraining=1?CICPUCount=1?CITargets=0).
	 */
	class FAimAssistCommand : public IAutomationLatentCommand
	{
	public:
		explicit FAimAssistCommand(FAutomationTestBase* InTest) : Test(InTest) {}

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
			if (!World || !Player)
			{
				return Waited(Now);
			}
			if (Stage > 0)
			{
				// The mouse points straight along Toward, 20 degrees off the opponent (at the arrow's own height).
				FVector2D Screen;
				if (PC->ProjectWorldLocationToScreen(Player->GetActorLocation() + FVector(0, 0, AChaosImpactCharacter::AimGuideHeight)
					+ Toward * 450.0f, Screen))
				{
					PC->SetMouseLocation(FMath::RoundToInt(Screen.X), FMath::RoundToInt(Screen.Y));
					Cursor = FVector2D(FMath::RoundToInt(Screen.X), FMath::RoundToInt(Screen.Y));
				}
			}
			if (Now < NextAt)
			{
				return false;
			}
			switch (Stage)
			{
			case 0:
			{
				AChaosImpactCharacter* CPU = nullptr;
				for (TActorIterator<AChaosImpactCPUController> It(World); It; ++It)
				{
					CPU = Cast<AChaosImpactCharacter>(It->GetPawn());
					It->SetActorTickEnabled(false);
				}
				if (!CPU || !PC->IsGameplayActive() || !Player->GetCharacterMovement()->IsMovingOnGround())
				{
					return Waited(Now);
				}
				// Somewhere with floor 28 m off, 20 degrees round from the way aimed.
				const FVector Origin = Player->GetActorLocation();
				bool bPlaced = false;
				for (int32 Turn = 0; Turn < 24 && !bPlaced; ++Turn)
				{
					Toward = FRotator(0.0f, Turn * 15.0f, 0.0f).Vector();
					const FVector There = Origin + Toward.RotateAngleAxis(20.0f, FVector::UpVector) * 2800.0f;
					FHitResult Floor;
					if (World->LineTraceSingleByObjectType(Floor, There + FVector(0, 0, 300), There - FVector(0, 0, 600),
						FCollisionObjectQueryParams(ECC_WorldStatic)) && FMath::Abs(Floor.ImpactPoint.Z - (Origin.Z - 96.0f)) < 60.0f)
					{
						CPU->SetActorLocation(Floor.ImpactPoint + FVector(0, 0, 100), false, nullptr, ETeleportType::TeleportPhysics);
						bPlaced = true;
					}
				}
				if (!bPlaced)
				{
					Test->AddError(TEXT("No floor 28 m off to stand the opponent on."));
					return true;
				}
				Opponent = CPU;
				Player->ResetForOnlineMatch(Origin, Toward.Rotation());
				Stage = 1;
				NextAt = Now + 0.4;
				return false;
			}
			case 1:
			{
				const FVector ToOpponent = (Opponent->GetActorLocation() - Player->GetActorLocation()).GetSafeNormal2D();
				const float Before = FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(Player->GetAimDirection(), ToOpponent)));
				UE_LOG(LogTemp, Display, TEXT("AIMASSIST before charging: %.1f degrees off the opponent"), Before);
				Test->TestTrue(TEXT("The mouse aims well off the opponent"), Before > 12.0f);
				// Nothing to throw, but a charge needs a ball: give one.
				if (Player->GetCarriedBallCount() == 0)
				{
					for (TActorIterator<AChaosImpactBall> It(World); It; ++It)
					{
						if (It->IsPickupAvailable() && Player->TryPickupBall(*It))
						{
							It->Destroy();
							break;
						}
					}
				}
				Player->BeginThrowInput();
				Stage = 2;
				NextAt = Now + 0.3;
				return false;
			}
			case 2:
			{
				const float OffCursor = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Player->GetAimDirection(), Toward), -1.0, 1.0)));
				UE_LOG(LogTemp, Display, TEXT("AIMASSIST charging: %.2f degrees off where the mouse points"), OffCursor);
				Test->TestTrue(TEXT("Charging, a mouse's aim stays where it points"), OffCursor < 1.0f);
				// The arrow as drawn: from its start toward its end, it points at the cursor.
				const FVector Start = Player->GetAimGuideStartWorldLocation();
				FVector2D From, To;
				if (PC->ProjectWorldLocationToScreen(Start, From) && PC->ProjectWorldLocationToScreen(Start + Player->GetAimDirection() * Player->GetAimGuideLength(), To))
				{
					const FVector2D Arrow = (To - From).GetSafeNormal(), Pointer = (Cursor - From).GetSafeNormal();
					const float Off = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector2D::DotProduct(Arrow, Pointer), -1.0, 1.0)));
					UE_LOG(LogTemp, Display, TEXT("AIMASSIST arrow on screen: %.2f degrees off the cursor"), Off);
					Test->TestTrue(TEXT("The arrow points at the cursor"), Off < 1.5f);
				}
				else { Test->AddError(TEXT("The arrow is not on screen.")); }
				Player->RequestCancelThrow();
				return true;
			}
			default:
				return true;
			}
		}

	private:
		bool Waited(const double Now)
		{
			if (Now - StartedAt > 60.0)
			{
				Test->AddError(TEXT("The training arena never got ready."));
				return true;
			}
			return false;
		}

		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Stage = 0;
		FVector Toward = FVector::ForwardVector;
		FVector2D Cursor = FVector2D::ZeroVector;
		TWeakObjectPtr<AChaosImpactCharacter> Opponent;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactAimAssistTest, "ChaosImpact.Training.AimAssist",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactAimAssistTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FAimAssistCommand(this));
	return true;
}

#endif
