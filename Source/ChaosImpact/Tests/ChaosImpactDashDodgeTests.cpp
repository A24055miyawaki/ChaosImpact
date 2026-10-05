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

namespace
{
	/**
	 * A dash is untouchable: a ball meeting someone mid-dash flies on through them (no hit, no flash, no stop), and
	 * the same ball does hit someone standing. Training arena with one CPU (?CITraining=1?CICPUCount=1?CITargets=0).
	 */
	class FDashDodgeCommand : public IAutomationLatentCommand
	{
	public:
		explicit FDashDodgeCommand(FAutomationTestBase* InTest) : Test(InTest) {}

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
			if (World && Stage == 2 && Ball.IsValid() && Target.IsValid())
			{
				UE_LOG(LogTemp, Display, TEXT("DASHDODGE t=%.3f ball %s pickup=%d vel=%.0f | cpu %s dashing=%d"), Now - LaunchedAt,
					*Ball->GetActorLocation().ToCompactString(), Ball->IsPickup(), Ball->GetBallVelocity().Size(),
					*Target->GetActorLocation().ToCompactString(), Target->IsDashing());
			}
			if (!World || !Player || Now < NextAt)
			{
				return Waited(Now);
			}
			AChaosImpactCharacter* CPU = Target.Get();
			const auto LaunchAt = [&]() -> AChaosImpactBall*
			{
				const FVector Start = Player->GetActorLocation() + Toward * 90.0f;
				const FTransform Where(Toward.Rotation(), Start);
				AChaosImpactBall* Ball = World->SpawnActorDeferred<AChaosImpactBall>(AChaosImpactBall::StaticClass(), Where,
					Player, Player, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				Ball->FinishSpawning(Where);
				Ball->Launch(Toward, 2100.0f, EChaosImpactBallFlightMode::Straight, 0.0f);
				return Ball;
			};
			switch (Stage)
			{
			case 0:
			{
				for (TActorIterator<AChaosImpactCPUController> It(World); It; ++It)
				{
					CPU = Cast<AChaosImpactCharacter>(It->GetPawn());
					It->SetActorTickEnabled(false);
				}
				if (!CPU || !PC->IsGameplayActive() || !Player->GetCharacterMovement()->IsMovingOnGround())
				{
					return Waited(Now);
				}
				Target = CPU;
				Toward = Player->GetActorForwardVector().GetSafeNormal2D();
				CPU->ResetForOnlineMatch(Player->GetActorLocation() + Toward * 520.0f, (-Toward).Rotation());
				CPU->SetActorLocation(Player->GetActorLocation() + Toward * 520.0f, false, nullptr, ETeleportType::TeleportPhysics);
				Stage = 1;
				NextAt = Now + 0.5;
				return false;
			}
			case 1:
				// Dashing into the ball: they meet mid-dash.
				Health = CPU->GetHealth();
				Ball = LaunchAt();
				LaunchedAt = Now;
				CPU->RequestAIDash(-Toward);
				Test->TestTrue(TEXT("The CPU is dashing"), CPU->IsDashing());
				Stage = 2;
				NextAt = Now + 0.4;
				return false;
			case 2:
			{
				Test->TestEqual(TEXT("A ball met mid-dash does no harm"), CPU->GetHealth(), Health);
				const AChaosImpactBall* Flown = Ball.Get();
				const float Past = Flown ? static_cast<float>(FVector::DotProduct(Flown->GetActorLocation() - CPU->GetActorLocation(), Toward)) : 0.0f;
				UE_LOG(LogTemp, Display, TEXT("DASHDODGE ball %.0f past the dasher, still flying=%d"), Past, Flown && !Flown->IsPickup());
				Test->TestTrue(TEXT("It flies on through them instead of stopping on them"), Flown && !Flown->IsPickup() && Past > 150.0f);
				if (Flown)
				{
					Ball->Destroy();
				}
				CPU->SetActorLocation(Player->GetActorLocation() + Toward * 520.0f, false, nullptr, ETeleportType::TeleportPhysics);
				Stage = 3;
				NextAt = Now + 1.0;
				return false;
			}
			case 3:
				// Standing: the same throw hits.
				Health = CPU->GetHealth();
				Ball = LaunchAt();
				Stage = 4;
				NextAt = Now + 0.5;
				return false;
			case 4:
				Test->TestTrue(TEXT("A ball meeting someone standing hits them"), CPU->IsEliminated() || CPU->GetHealth() < Health);
				return true;
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
		float Health = 0.0f;
		double LaunchedAt = 0.0;
		FVector Toward = FVector::ForwardVector;
		TWeakObjectPtr<AChaosImpactCharacter> Target;
		TWeakObjectPtr<AChaosImpactBall> Ball;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactDashDodgeTest, "ChaosImpact.Training.DashDodge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactDashDodgeTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FDashDodgeCommand(this));
	return true;
}

#endif
