#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactBall.h"
#include "ChaosImpactCharacter.h"
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
	AChaosImpactPlayerController* FindOverheadTestController()
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

	void CaptureOverhead(const TCHAR* Name)
	{
		FScreenshotRequest::RequestScreenshot(
			FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("OverheadQA"), Name), true, false);
	}

	bool GiveOverheadTestBall(UWorld* World, AChaosImpactCharacter* Player, const EChaosImpactBallType Type)
	{
		const FTransform SpawnTransform(FRotator::ZeroRotator, Player->GetActorLocation() + FVector(0, 0, 400));
		AChaosImpactBall* Ball = World->SpawnActorDeferred<AChaosImpactBall>(AChaosImpactBall::StaticClass(),
			SpawnTransform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Ball)
		{
			return false;
		}
		Ball->SetBallType(Type);
		Ball->FinishSpawning(SpawnTransform);
		Ball->MakePickup();
		const bool bTaken = Player->TryPickupBall(Ball);
		Ball->Destroy();
		return bTaken;
	}

	bool IsOnPlayerScreen(const APlayerController* PC, const FVector& Location)
	{
		FVector2D Screen;
		int32 Width = 0;
		int32 Height = 0;
		PC->GetViewportSize(Width, Height);
		return PC->ProjectWorldLocationToScreen(Location, Screen, true)
			&& Screen.X >= 0.0 && Screen.Y >= 0.0 && Screen.X <= Width && Screen.Y <= Height;
	}

	class FOverheadCommand : public IAutomationLatentCommand
	{
	public:
		explicit FOverheadCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now < NextAt)
			{
				return false;
			}
			AChaosImpactPlayerController* PC = FindOverheadTestController();
			UWorld* World = PC ? PC->GetWorld() : nullptr;
			AChaosImpactCharacter* Player = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			AChaosImpactCharacter* CPU = CPUCharacter.Get();
			if (!World || !Player || (Stage > 0 && !CPU))
			{
				if (Now - StartedAt < 40.0)
				{
					return false;
				}
				Test->AddError(TEXT("Run with ?CITraining=1?CICPUCount=1 so a player and a CPU exist."));
				return true;
			}
			const auto PlaceCPU = [&](const FVector& Location)
			{
				CPU->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
			};

			switch (Stage)
			{
			case 0:
				for (TActorIterator<AChaosImpactCPUController> It(World); It; ++It)
				{
					CPUCharacter = Cast<AChaosImpactCharacter>(It->GetPawn());
					It->SetActorTickEnabled(false);
				}
				CPU = CPUCharacter.Get();
				if (!CPU || !PC->IsGameplayActive())
				{
					CPUCharacter.Reset();
					return Now - StartedAt < 40.0 ? false : (Test->AddError(TEXT("No CPU or gameplay.")), true);
				}
				CPU->GetCharacterMovement()->DisableMovement();
				Origin = Player->GetActorLocation();
				Player->ResetForOnlineMatch(Origin, Player->GetActorRotation());
				PlaceCPU(Origin + FVector(250.0f, 320.0f, 0.0f));
				Test->TestEqual(TEXT("A CPU is labelled CPU1"), CPU->GetOverheadDisplayName(), FString(TEXT("CPU1")));
				Test->TestFalse(TEXT("The player has a name to show"), Player->GetOverheadDisplayName().IsEmpty());
				Test->TestTrue(TEXT("Fire ball picked up"), GiveOverheadTestBall(World, Player, EChaosImpactBallType::Fire));
				Test->TestTrue(TEXT("Ice ball picked up"), GiveOverheadTestBall(World, Player, EChaosImpactBallType::Ice));
				Stage = 1;
				NextAt = Now + 0.8;
				return false;
			case 1:
				CaptureOverhead(TEXT("OH-01-Names-Before-Swap.png"));
				Player->RequestBallSwap();
				Test->TestEqual(TEXT("Swap puts the left-hand ball in the right hand"),
					Player->GetCarriedBallType(0), EChaosImpactBallType::Ice);
				Test->TestEqual(TEXT("Swap puts the right-hand ball in the left hand"),
					Player->GetCarriedBallType(1), EChaosImpactBallType::Fire);
				Stage = 2;
				NextAt = Now + 0.09;
				return false;
			case 2:
				CaptureOverhead(TEXT("OH-02-Swap-Animating.png"));
				Stage = 3;
				NextAt = Now + 0.6;
				return false;
			case 3:
			{
				CaptureOverhead(TEXT("OH-03-Swapped.png"));
				Player->RequestBallSwap();
				Test->TestEqual(TEXT("Swapping again restores the order"),
					Player->GetCarriedBallType(0), EChaosImpactBallType::Fire);
				// Find the nearest spot just outside this player's view.
				const FVector Toward = FVector(1.0f, 0.4f, 0.0f).GetSafeNormal();
				NearOffscreen = Origin + Toward * 3000.0f;
				for (float Distance = 700.0f; Distance <= 3000.0f; Distance += 50.0f)
				{
					if (!IsOnPlayerScreen(PC, Origin + Toward * Distance))
					{
						NearOffscreen = Origin + Toward * (Distance + 150.0f);
						break;
					}
				}
				PlaceCPU(NearOffscreen);
				Stage = 4;
				NextAt = Now + 0.4;
				return false;
			}
			case 4:
				Test->TestFalse(TEXT("The CPU is outside the view for the arrow check"),
					IsOnPlayerScreen(PC, CPU->GetActorLocation()));
				CaptureOverhead(TEXT("OH-04-Offscreen-Near.png"));
				PlaceCPU(Origin + (NearOffscreen - Origin).GetSafeNormal2D() * 3600.0f);
				Stage = 5;
				NextAt = Now + 0.4;
				return false;
			case 5:
				CaptureOverhead(TEXT("OH-05-Offscreen-Far.png"));
				Player->ResetForOnlineMatch(Origin, Player->GetActorRotation());
				GiveOverheadTestBall(World, Player, EChaosImpactBallType::Ice);
				Player->RequestBallSwap();
				Test->TestEqual(TEXT("With one ball a swap changes nothing"), Player->GetCarriedBallCount(), 1);
				Test->TestEqual(TEXT("With one ball its type stays"), Player->GetCarriedBallType(0), EChaosImpactBallType::Ice);
				Stage = 6;
				NextAt = Now + 0.3;
				return false;
			default:
				return true;
			}
		}

	private:
		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Stage = 0;
		FVector Origin = FVector::ZeroVector;
		FVector NearOffscreen = FVector::ZeroVector;
		TWeakObjectPtr<AChaosImpactCharacter> CPUCharacter;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactOverheadTest, "ChaosImpact.Training.OverheadMarkers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::EngineFilter)

bool FChaosImpactOverheadTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FOverheadCommand(this));
	return true;
}

#endif
