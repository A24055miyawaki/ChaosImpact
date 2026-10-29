#if WITH_DEV_AUTOMATION_TESTS
#include "ChaosImpactBall.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactPlayerController.h"
#include "Engine/DamageEvents.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	/**
	 * The feel of speed, filmed in training (/Game/ThirdPerson/Lvl_ThirdPerson?CITraining=1?CITargets=0?CICPUCount=1):
	 * a dash, a fully charged throw just after it leaves the hand and the ball in flight, and a ball striking the CPU.
	 * Saved/SpeedFeelQA.
	 */
	class FSpeedFeelShotsCommand : public IAutomationLatentCommand
	{
	public:
		explicit FSpeedFeelShotsCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - StartedAt > 90.0)
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
			if (!Me || !PC->IsGameplayActive() || Now < NextAt)
			{
				return false;
			}
			AChaosImpactCharacter* Cpu = nullptr;
			for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
			{
				if (*It != Me && !It->IsPlayerControlled())
				{
					Cpu = *It;
				}
			}
			const auto Shot = [](const TCHAR* Name)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SpeedFeelQA"), Name), true, false);
			};
			const auto GiveBall = [World, Me]()
			{
				AChaosImpactBall* Ball = World->SpawnActor<AChaosImpactBall>(Me->GetActorLocation() + FVector(0, 0, 200), FRotator::ZeroRotator);
				Ball->MakePickup();
				Ball->ConsumePickup(Me);
			};
			switch (Step)
			{
			case 0:
				// The CPU stands still, out of the way, the whole time.
				if (Cpu)
				{
					Cpu->SetTrainingMenuFrozen(true);
				}
				NextAt = Now + 2.0;
				++Step;
				return false;
			case 1:
				if (Cpu)
				{
					Cpu->SetTrainingMenuFrozen(true);
					Cpu->SetActorLocation(Me->GetActorLocation() + FVector(900.0f, 0.0f, 0.0f));
				}
				Me->RequestAIDash(FVector(0.0f, 1.0f, 0.0f));
				++Step;
				return false;
			case 2:
				// Filmed in the middle of the dash.
				if (!Me->IsDashing())
				{
					return false;
				}
				// A moment into it, so the afterimages have been left behind.
				if (DashSeenAt < 0.0)
				{
					DashSeenAt = Now;
				}
				if (Now - DashSeenAt < 0.1)
				{
					return false;
				}
				Shot(TEXT("1_Dash.png"));
				NextAt = Now + 1.2;
				++Step;
				return false;
			case 3:
				GiveBall();
				Me->SetAIAimDirection(FVector(1.0f, 0.0f, 0.0f));
				Me->BeginThrowInput();
				NextAt = Now + 1.7;
				++Step;
				return false;
			case 4:
				Me->EndThrowInput();
				ThrownAt = Now;
				++Step;
				return false;
			case 5:
				// The moment it leaves the hand.
				if (Me->IsThrowReleasePending() && Now - ThrownAt < 2.0)
				{
					return false;
				}
				Shot(TEXT("2_HardThrow.png"));
				NextAt = Now + 0.06;
				++Step;
				return false;
			case 6:
				Shot(TEXT("3_BallFlight.png"));
				NextAt = Now + 1.5;
				++Step;
				return false;
			case 7:
				if (Cpu)
				{
					// A hard ball straight into the CPU.
					FActorSpawnParameters Params;
					Params.Owner = Me;
					Params.Instigator = Me;
					const FVector From = Cpu->GetActorLocation() - FVector(250.0f, 0.0f, 0.0f);
					if (AChaosImpactBall* Ball = World->SpawnActor<AChaosImpactBall>(From, FRotator::ZeroRotator, Params))
					{
						Ball->Launch(FVector(1.0f, 0.0f, 0.0f), 3200.0f, EChaosImpactBallFlightMode::Straight, 0.0f);
					}
				}
				HitAt = Now;
				CpuHealth = Cpu ? Cpu->GetHealth() : 0.0f;
				++Step;
				return false;
			case 8:
				// The moment it strikes (its health drops).
				if (Cpu && Cpu->GetHealth() >= CpuHealth && Now - HitAt < 1.0)
				{
					return false;
				}
				Shot(TEXT("4_Hit.png"));
				NextAt = Now + 0.5;
				++Step;
				return false;
			default:
				return true;
			}
		}

	private:
		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		double ThrownAt = 0.0;
		double HitAt = 0.0;
		float CpuHealth = 0.0f;
		double DashSeenAt = -1.0;
		int32 Step = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactSpeedFeelShotsTest, "ChaosImpact.UI.SpeedFeelShots",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FChaosImpactSpeedFeelShotsTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FSpeedFeelShotsCommand(this));
	return true;
}
#endif
