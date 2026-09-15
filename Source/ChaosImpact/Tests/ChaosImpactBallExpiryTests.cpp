#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactBall.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"

namespace
{
	AChaosImpactPlayerController* FindBallExpiryTestController()
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

	AChaosImpactBall* SpawnBallExpiryTestBall(UWorld* World, const FVector& Location)
	{
		const FTransform SpawnTransform(FRotator::ZeroRotator, Location);
		AChaosImpactBall* Ball = World->SpawnActorDeferred<AChaosImpactBall>(AChaosImpactBall::StaticClass(),
			SpawnTransform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Ball)
		{
			Ball->FinishSpawning(SpawnTransform);
		}
		return Ball;
	}

	class FBallExpiryCommand : public IAutomationLatentCommand
	{
	public:
		explicit FBallExpiryCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now < NextAt)
			{
				return false;
			}
			AChaosImpactPlayerController* PC = FindBallExpiryTestController();
			UWorld* World = PC ? PC->GetWorld() : nullptr;
			const APawn* Player = PC ? PC->GetPawn() : nullptr;
			if (Stage == 0)
			{
				if (!World || !Player || !PC->IsGameplayActive())
				{
					if (Now - StartedAt < 40.0)
					{
						return false;
					}
					Test->AddError(TEXT("No training player was created."));
					return true;
				}
				// CPUs go and collect lying balls, which would look like an early expiry.
				for (TActorIterator<AChaosImpactCPUController> It(World); It; ++It)
				{
					It->SetActorTickEnabled(false);
				}
				// Well away from the player so nothing is collected by accident.
				const FVector Origin = Player->GetActorLocation();
				AChaosImpactBall* Short = SpawnBallExpiryTestBall(World, Origin + FVector(700.0f, 0.0f, 0.0f));
				AChaosImpactBall* Spawned = SpawnBallExpiryTestBall(World, Origin + FVector(-700.0f, 0.0f, 0.0f));
				AChaosImpactBall* Default = SpawnBallExpiryTestBall(World, Origin + FVector(0.0f, 700.0f, 0.0f));
				if (!Short || !Spawned || !Default)
				{
					Test->AddError(TEXT("Test balls could not be spawned."));
					return true;
				}
				Short->SetLandedPickupLifetime(1.5f, 0.8f);
				Short->MakeRollingPickup(FVector::ZeroVector);
				Spawned->MakePickup();
				Default->MakeRollingPickup(FVector::ZeroVector);
				ShortBall = Short;
				SpawnerBall = Spawned;
				DefaultBall = Default;
				Stage = 1;
				NextAt = Now + 1.0;
				return false;
			}
			if (Stage == 1)
			{
				Test->TestTrue(TEXT("A landed ball is still there before its time is up"), ShortBall.IsValid());
				Stage = 2;
				NextAt = Now + 1.0;
				return false;
			}
			Test->TestFalse(TEXT("A landed ball disappears once its time is up"), ShortBall.IsValid());
			Test->TestTrue(TEXT("A spawner-style hovering pickup never expires"), SpawnerBall.IsValid());
			Test->TestTrue(TEXT("The default lifetime is longer than two seconds"), DefaultBall.IsValid());
			if (AChaosImpactBall* Ball = SpawnerBall.Get())
			{
				Ball->Destroy();
			}
			if (AChaosImpactBall* Ball = DefaultBall.Get())
			{
				Ball->Destroy();
			}
			return true;
		}

	private:
		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Stage = 0;
		TWeakObjectPtr<AChaosImpactBall> ShortBall;
		TWeakObjectPtr<AChaosImpactBall> SpawnerBall;
		TWeakObjectPtr<AChaosImpactBall> DefaultBall;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactBallExpiryTest, "ChaosImpact.Training.LandedBallExpiry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::EngineFilter)

bool FChaosImpactBallExpiryTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FBallExpiryCommand(this));
	return true;
}

#endif
