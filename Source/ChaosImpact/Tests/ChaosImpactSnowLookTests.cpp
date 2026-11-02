#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactBall.h"
#include "ChaosImpactBallTypes.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactCharacterRoster.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	/**
	 * A full-grown snowball with every character: walked up to its biggest size, charged to full over the head (a
	 * picture of each in Saved/SnowLookQA), then hurled at an opponent standing 3 m ahead, who must be hit rather than
	 * flown over. Training arena with one CPU (?CITraining=1?CICPUCount=1?CITargets=0).
	 */
	class FSnowLookCommand : public IAutomationLatentCommand
	{
	public:
		explicit FSnowLookCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - StartedAt > 400.0) { Test->AddError(FString::Printf(TEXT("Snow look test timeout at character %d stage %d"), Look, Stage)); return true; }
			AChaosImpactPlayerController* PC = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld()) { PC = PC ? PC : Cast<AChaosImpactPlayerController>(Context.World()->GetFirstPlayerController()); }
			}
			UWorld* World = PC ? PC->GetWorld() : nullptr;
			AChaosImpactCharacter* Player = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			if (!World || !Player || !PC->IsGameplayActive()) { return false; }
			AChaosImpactCharacter* CPU = nullptr;
			for (TActorIterator<AChaosImpactCPUController> It(World); It; ++It) { CPU = Cast<AChaosImpactCharacter>(It->GetPawn()); It->SetActorTickEnabled(false); }
			if (!CPU) { return false; }
			const auto Place = [](AChaosImpactCharacter* Character, const FVector& At) { Character->SetActorLocation(At, false, nullptr, ETeleportType::TeleportPhysics); };
			// The mouse is the aim: while throwing at the opponent it points at them (at the arrow's height), every frame,
			// so a real mouse moved meanwhile cannot turn the throw.
			if (Stage >= 12 && Stage <= 13)
			{
				FVector2D Screen;
				if (PC->ProjectWorldLocationToScreen(CPU->GetActorLocation() + FVector(0, 0, AChaosImpactCharacter::AimGuideHeight), Screen))
				{ PC->SetMouseLocation(FMath::RoundToInt(Screen.X), FMath::RoundToInt(Screen.Y)); }
			}
			if (Now < NextAt) { return false; }
			switch (Stage)
			{
			case 0:
			{
				// Each character in turn, from a clean start, with a snowball in hand.
				if (Look >= ChaosImpactRoster::Num()) { return true; }
				if (!Player->GetCharacterMovement()->IsMovingOnGround()) { return false; }
				if (Origin.IsZero()) { Origin = Player->GetActorLocation(); }
				Player->LookOverride = Look;
				Player->ResetForOnlineMatch(Origin, Toward.Rotation());
				Place(CPU, Origin - Toward * 700.0f + FVector(0, 0, 50));
				AChaosImpactBall* Pickup = World->SpawnActorDeferred<AChaosImpactBall>(AChaosImpactBall::StaticClass(),
					FTransform(Player->GetActorLocation() + FVector(0, 0, 400)), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				Pickup->SetBallType(EChaosImpactBallType::Snow);
				Pickup->FinishSpawning(FTransform(Player->GetActorLocation() + FVector(0, 0, 400)));
				Pickup->MakePickup();
				if (Player->TryPickupBall(Pickup)) { Pickup->Destroy(); }
				Test->TestEqual(TEXT("The snowball is in hand"), Player->GetCarriedBallType(0), EChaosImpactBallType::Snow);
				Steps = 0; Stage = 1; return false;
			}
			case 1:
				// Walked back and forth until it is as big as it gets.
				if (Steps < 48 && Player->GetSnowGrowth(0) < 0.999f)
				{
					Place(Player, Origin + Toward * (100.0f * (Steps % 12 < 6 ? Steps % 6 : 6 - Steps % 6)));
					++Steps; return false;
				}
				Test->TestTrue(TEXT("Grown to its full size"), Player->GetSnowGrowth(0) > 0.98f);
				// (Not a reset: that empties the hands.)
				Place(Player, Origin); Player->SetActorRotation(Toward.Rotation());
				Player->BeginThrowInput();
				Stage = 2; NextAt = Now + Player->GetMaxChargeSeconds() + 0.6; return false;
			case 2:
				// Charged to full over the head.
				Test->TestTrue(TEXT("Charged to full"), Player->GetThrowChargeAlpha() >= 0.999f);
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SnowLookQA"),
					FString::Printf(TEXT("snow-full-%02d.png"), Look)), true, false);
				Stage = 3; NextAt = Now + 0.3; return false;
			case 3:
				Player->RequestCancelThrow();
				++Look; Stage = 0; NextAt = Now + 0.2; return false;
			case 10:
			{
				// First, with hands still empty: a full-grown snowball hurled at someone 3 m ahead comes down on them.
				if (!Player->GetCharacterMovement()->IsMovingOnGround()) { return false; }
				if (Origin.IsZero()) { Origin = Player->GetActorLocation(); }
				Player->LookOverride = INDEX_NONE;
				Player->ResetForOnlineMatch(Origin, Toward.Rotation());
				AChaosImpactBall* Pickup = World->SpawnActorDeferred<AChaosImpactBall>(AChaosImpactBall::StaticClass(),
					FTransform(Player->GetActorLocation() + FVector(0, 0, 400)), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				Pickup->SetBallType(EChaosImpactBallType::Snow);
				Pickup->FinishSpawning(FTransform(Player->GetActorLocation() + FVector(0, 0, 400)));
				Pickup->MakePickup();
				if (Player->TryPickupBall(Pickup)) { Pickup->Destroy(); }
				Steps = 0; Stage = 11; return false;
			}
			case 11:
				if (Steps < 48 && Player->GetSnowGrowth(0) < 0.999f)
				{
					Place(Player, Origin + Toward * (100.0f * (Steps % 12 < 6 ? Steps % 6 : 6 - Steps % 6)));
					++Steps; return false;
				}
				Place(Player, Origin); Player->SetActorRotation(Toward.Rotation());
				CPU->ResetForOnlineMatch(Origin + Toward * 300.0f, (-Toward).Rotation());
				Stage = 12; NextAt = Now + 0.3; return false;
			case 12:
				// (A ball lying about can be picked up on the walk: the snowball must still be the one thrown.)
				if (!Test->TestEqual(TEXT("The full-grown snowball is the one in hand"), Player->GetCarriedBallType(0), EChaosImpactBallType::Snow)) { return true; }
				CPUHealth = CPU->GetHealth();
				Player->BeginThrowInput();
				Stage = 13; NextAt = Now + Player->GetMaxChargeSeconds() + 0.3; return false;
			case 13:
				Player->EndThrowInput();
				HitDeadline = Now + 2.5;
				Stage = 14; NextAt = Now + 0.4; return false;
			case 14:
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SnowLookQA"), TEXT("snow-near-hit.png")), true, false);
				if (CPU->GetHealth() >= CPUHealth && Now < HitDeadline) { NextAt = Now + 0.1; return false; }
				UE_LOG(LogTemp, Display, TEXT("SNOWLOOK opponent 3 m ahead: health %.1f -> %.1f"), CPUHealth, CPU->GetHealth());
				Test->TestTrue(TEXT("A big snowball hurled over the head hits the opponent right in front"), CPU->GetHealth() < CPUHealth);
				// Then every character's look.
				Stage = 0; NextAt = Now + 0.5; return false;
			default:
				return true;
			}
		}

	private:
		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0, HitDeadline = 0.0;
		int32 Stage = 10, Look = 0, Steps = 0;
		float CPUHealth = 0.0f;
		FVector Origin = FVector::ZeroVector;
		FVector Toward = FVector::ForwardVector;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactSnowLookTest, "ChaosImpact.Training.SnowLook",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactSnowLookTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FSnowLookCommand(this));
	return true;
}

#endif
