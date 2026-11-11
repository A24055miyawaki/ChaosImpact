#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactBall.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactHook.h"
#include "ChaosImpactPlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	/**
	 * Training as it starts (the player where they come in, nobody moved): a hook and then an anchor thrown straight up
	 * the screen (the camera's way), filmed through the game's own camera as they fly, to see nothing is drawn where it
	 * should not be (a cable off to one side). Saved/HookQA/View_*.png.
	 */
	class FHookViewCommand : public IAutomationLatentCommand
	{
	public:
		explicit FHookViewCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double RealNow = FPlatformTime::Seconds();
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld())
				{
					World = Context.World();
					break;
				}
			}
			AChaosImpactPlayerController* PC = World ? Cast<AChaosImpactPlayerController>(World->GetFirstPlayerController()) : nullptr;
			AChaosImpactCharacter* Player = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			if (!Player || !PC->IsGameplayActive() || !PC->PlayerCameraManager)
			{
				if (RealNow - StartedAt > 120.0)
				{
					Test->AddError(TEXT("No playable character."));
					return true;
				}
				return false;
			}
			const double Now = World->GetTimeSeconds();
			if (Stage == 0)
			{
				NextAt = Now + 1.5;
				Stage = 1;
				return false;
			}
			if (Now < NextAt)
			{
				return false;
			}
			const auto Shot = [](const FString& Name)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("HookQA"), Name), true, false);
			};
			// Three throws: a hook (a ball on the way, as it comes), a hook with nothing on the way (its full reach), an anchor.
			const int32 Throw = Stage / 10;
			const bool bAnchor = Throw == 2;
			const int32 Step = Stage % 10;
			const FString Label = Throw == 0 ? TEXT("Hook") : Throw == 1 ? TEXT("HookEmpty") : TEXT("Anchor");
			switch (Step)
			{
			case 1:
			{
				// Up the screen: the camera's facing, flat.
				const FVector Up = PC->PlayerCameraManager->GetCameraRotation().Vector().GetSafeNormal2D();
				if (Throw >= 1)
				{
					for (TActorIterator<AChaosImpactBall> It(World); It; ++It)
					{
						if (It->IsPickup())
						{
							It->Destroy();
						}
					}
				}
				const FTransform At(FRotator::ZeroRotator, Player->GetActorLocation() + Up * 70.0f + FVector(0.0f, 0.0f, 30.0f));
				AChaosImpactBall* Ball = World->SpawnActorDeferred<AChaosImpactBall>(AChaosImpactBall::StaticClass(), At, Player, Player,
					ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				if (Ball)
				{
					Ball->SetBallType(bAnchor ? EChaosImpactBallType::Anchor : EChaosImpactBallType::Hook);
					Ball->FinishSpawning(At);
					Ball->Launch(Up, 2000.0f, EChaosImpactBallFlightMode::Straight, 0.0f);
				}
				UE_LOG(LogTemp, Log, TEXT("Hook view: %s thrown from %s toward %s"), *Label,
					*Player->GetActorLocation().ToCompactString(), *Up.ToCompactString());
				ReleasedAt = Now;
				Stage += 1;
				NextAt = Now + 0.12;
				return false;
			}
			case 2:
			case 3:
			case 4:
			case 5:
				Shot(FString::Printf(TEXT("View_%s_%d.png"), *Label, Step - 1));
				Stage += 1;
				NextAt = ReleasedAt + 0.12 + (Step - 1) * 0.2;
				return false;
			case 6:
				if (Throw >= 2)
				{
					return true;
				}
				Stage = (Throw + 1) * 10 + 1;
				NextAt = Now + 2.5;
				return false;
			default:
				return true;
			}
		}

	private:
		FAutomationTestBase* Test = nullptr;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		double ReleasedAt = 0.0;
		int32 Stage = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactHookViewTest, "ChaosImpact.Training.HookView",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::EngineFilter)

bool FChaosImpactHookViewTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FHookViewCommand(this));
	return true;
}

#endif
