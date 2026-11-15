#if WITH_DEV_AUTOMATION_TESTS
#include "ChaosImpactBall.h"
#include "ChaosImpactBallSpawner.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactGadget.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactTrainingArena.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	/** A barrier ball charged in training: its preview shows, and the wall stands just where the preview had it. */
	class FBarrierPreviewCommand : public IAutomationLatentCommand
	{
	public:
		explicit FBarrierPreviewCommand(FAutomationTestBase* InTest) : Test(InTest) {}
		bool Update() override
		{
			AChaosImpactPlayerController* PC = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld()) { PC = Cast<AChaosImpactPlayerController>(Context.World()->GetFirstPlayerController()); }
			}
			AChaosImpactCharacter* Player = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			const double Now = FPlatformTime::Seconds();
			if (Now - Started > 90.0) { Test->AddError(FString::Printf(TEXT("Barrier preview stalled at %d"), Stage)); return true; }
			if (!Player || Now < NextAt) { return false; }
			UWorld* World = Player->GetWorld();
			const auto Next = [this, Now](const double Delay) { ++Stage; NextAt = Now + Delay; return false; };
			const auto Give = [Player, World](const EChaosImpactBallType Type)
			{
				const FTransform At(Player->GetActorLocation() + FVector(0, 0, 500));
				AChaosImpactBall* Ball = World->SpawnActorDeferred<AChaosImpactBall>(AChaosImpactBall::StaticClass(), At, nullptr, nullptr,
					ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				Ball->SetBallType(Type);
				Ball->FinishSpawning(At);
				Ball->MakePickup();
				const bool bTaken = Player->TryPickupBall(Ball);
				Ball->Destroy();
				return bTaken;
			};
			switch (Stage)
			{
			case 0:
			{
				if (!PC->IsGameplayActive() || !Player->GetCharacterMovement()->IsMovingOnGround()) { return false; }
				for (TActorIterator<AChaosImpactCPUController> It(World); It; ++It) { It->SetActorTickEnabled(false); }
				for (TActorIterator<AChaosImpactBallSpawner> It(World); It; ++It) { It->Destroy(); }
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It) { It->Destroy(); }
				AChaosImpactGadget::ClearAll(World);
				FVector Origin = Player->GetActorLocation();
				for (TActorIterator<AChaosImpactTrainingArena> It(World); It; ++It) { Origin = It->GetCenter() + FVector(-350, -350, 100); break; }
				Player->ResetForOnlineMatch(Origin, FRotator::ZeroRotator);
				return Next(0.5);
			}
			case 1:
				Test->TestTrue(TEXT("A barrier ball in hand"), Give(EChaosImpactBallType::Barrier));
				Player->BeginThrowInput();
				return Next(0.25);
			case 2:
			{
				Test->TestTrue(TEXT("Charging the barrier ball"), Player->IsChargingThrow());
				Test->TestTrue(TEXT("Its preview shows while charging"), Player->IsBarrierPreviewShown());
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("GadgetQA"), TEXT("BarrierPreview.png")), true, false);
				return Next(0.2);
			}
			case 3:
			{
				// The preview's wall, then the throw in the same moment.
				const bool bPredicted = Player->PredictBarrierPlacement(Player->GetThrowChargeAlpha(), PlannedFloor, PlannedHeading);
				Test->TestTrue(TEXT("The preview has a place for the wall"), bPredicted);
				Player->EndThrowInput();
				// Walking off sideways as it flies changes nothing: it stands where shown, across the throw.
				const FVector Aside = FVector::CrossProduct(FVector::UpVector, PlannedHeading) * 260.0f;
				Player->SetActorLocation(Player->GetActorLocation() + Aside, true);
				return Next(1.8);
			}
			case 4:
			{
				Test->TestFalse(TEXT("The preview is gone once thrown"), Player->IsBarrierPreviewShown());
				AChaosImpactGadget* Wall = nullptr;
				for (TActorIterator<AChaosImpactGadget> It(World); It; ++It)
				{
					if (It->IsBarrier() && !It->HasEnded()) { Wall = *It; }
				}
				Test->TestNotNull(TEXT("The wall stands"), Wall);
				if (Wall)
				{
					const float Off = static_cast<float>(FVector::Dist(Wall->GetActorLocation(), PlannedFloor));
					UE_LOG(LogTemp, Display, TEXT("BARRIERPREVIEW wall %.1f cm from its preview, facing %.4f"), Off,
						FVector::DotProduct(Wall->GetActorForwardVector(), PlannedHeading));
					Test->TestTrue(FString::Printf(TEXT("Just where the preview had it (%.1f cm off)"), Off), Off < 2.0f);
					Test->TestTrue(TEXT("Facing the way the preview had it"), FVector::DotProduct(Wall->GetActorForwardVector(), PlannedHeading) > 0.999);
				}
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("GadgetQA"), TEXT("BarrierPlaced.png")), true, false);
				return Next(0.3);
			}
			default:
				AChaosImpactGadget::ClearAll(World);
				return true;
			}
		}

	private:
		FAutomationTestBase* Test;
		int32 Stage = 0;
		double Started = FPlatformTime::Seconds();
		double NextAt = 0.0;
		FVector PlannedFloor = FVector::ZeroVector;
		FVector PlannedHeading = FVector::ForwardVector;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrierPreviewTest, "ChaosImpact.Gadget.BarrierPreview",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FBarrierPreviewTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FBarrierPreviewCommand(this));
	return true;
}
#endif
