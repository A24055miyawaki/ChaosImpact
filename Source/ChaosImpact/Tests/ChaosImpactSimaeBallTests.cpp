#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactBall.h"
#include "ChaosImpactBallSpawner.h"
#include "ChaosImpactHazardZone.h"
#include "ChaosImpactTornado.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactSimaeBird.h"
#include "Camera/CameraActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "TimerManager.h"

namespace
{
	class FSimaeBallCommand : public IAutomationLatentCommand
	{
	public:
		explicit FSimaeBallCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			AChaosImpactPlayerController* PC = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld())
				{
					PC = Cast<AChaosImpactPlayerController>(Context.World()->GetFirstPlayerController());
					if (PC) { break; }
				}
			}
			UWorld* World = PC ? PC->GetWorld() : nullptr;
			AChaosImpactCharacter* Player = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			if (FPlatformTime::Seconds() - StartedAt > 70.0)
			{
				Test->AddError(TEXT("SIMAE test timed out waiting for flock behavior."));
				return true;
			}
			if (!World || !Player || !PC->IsGameplayActive()) { return false; }
			const double Now = World->GetTimeSeconds();
			if (Now < NextAt) { return false; }
			AChaosImpactCharacter* Enemy = Target.Get();
			if (Stage > 0 && !Enemy) { Test->AddError(TEXT("SIMAE enemy disappeared.")); return true; }
			const auto Capture = [](const TCHAR* Name)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SimaeBallQA"), Name), true, false);
			};
			const auto ClearBirds = [World]()
			{
				for (TActorIterator<AChaosImpactSimaeBird> It(World); It; ++It) { It->Destroy(); }
			};
			switch (Stage)
			{
			case 0:
			{
				for (TActorIterator<AChaosImpactCPUController> It(World); It; ++It)
				{
					Enemy = Cast<AChaosImpactCharacter>(It->GetPawn());
					It->SetActorTickEnabled(false);
				}
				if (!Enemy || !Player->GetCharacterMovement()->IsMovingOnGround()) { return false; }
				Target = Enemy;
				// Keep randomly spawned/previously thrown balls out of a focused delayed-damage test.
				for (TActorIterator<AChaosImpactBallSpawner> It(World); It; ++It)
				{
					World->GetTimerManager().ClearAllTimersForObject(*It);
					It->Destroy();
				}
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It) { It->Destroy(); }
				for (TActorIterator<AChaosImpactHazardZone> It(World); It; ++It) { It->Destroy(); }
				for (TActorIterator<AChaosImpactTornado> It(World); It; ++It) { It->Destroy(); }
				Origin = Player->GetActorLocation();
				Toward = Player->GetActorForwardVector().GetSafeNormal2D();
				Player->ResetForOnlineMatch(Origin, Toward.Rotation());
				Enemy->ResetForOnlineMatch(Origin + Toward * 520.0f, (-Toward).Rotation());
				PlayerHealth = Player->GetHealth();
				Health = Enemy->GetHealth();
				ClearBirds();
				Test->TestNotNull(TEXT("SIMAE supplied bird texture is loadable"),
					LoadObject<UTexture2D>(nullptr, ChaosImpactBallTypes::SimaeAssets::BirdTexture));
				// The real ball must hit the floor beside the opponent and release the flock, without a direct hit.
				const FVector Side = FVector::CrossProduct(FVector::UpVector, Toward);
				const FVector At = Enemy->GetActorLocation() + Side * 210.0f;
				const FTransform Where(FRotator::ZeroRotator, At);
				AChaosImpactBall* NewBall = World->SpawnActorDeferred<AChaosImpactBall>(AChaosImpactBall::StaticClass(),
					Where, Player, Player, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				NewBall->SetBallType(EChaosImpactBallType::Simae);
				NewBall->FinishSpawning(Where);
				NewBall->Launch(-FVector::UpVector, 550.0f, EChaosImpactBallFlightMode::Arc, 0.0f);
				Ball = NewBall;
				++Stage;
				NextAt = Now + 0.25;
				return false;
			}
			case 1:
			{
				if (!Ball.IsValid() || !Ball->HasDetonated()) { return false; }
				int32 Count = 0;
				for (TActorIterator<AChaosImpactSimaeBird> It(World); It; ++It) { ++Count; }
				Test->TestEqual(TEXT("SIMAE floor impact releases twelve birds"), Count, ChaosImpactBallTypes::SimaeBirdCount);
				int32 Bursts = 0;
				for (TActorIterator<AChaosImpactSimaeFeatherBurst> It(World); It; ++It) { ++Bursts; }
				Test->TestTrue(TEXT("SIMAE floor impact triggers the feather and shock-wave effect"), Bursts > 0);
				Test->TestEqual(TEXT("SIMAE birds do not damage at release"), Enemy->GetHealth(), Health);
				Capture(TEXT("SB-01-Flock.png"));
				++Stage;
				return false;
			}
			case 2:
			{
				float Remaining = 0.0f;
				if (!AChaosImpactSimaeBird::HasPerchedBird(Enemy, &Remaining)) { return false; }
				Test->TestEqual(TEXT("SIMAE arrival is harmless before the delayed peck"), Enemy->GetHealth(), Health);
				Test->TestTrue(TEXT("SIMAE leaves a reaction window before the first peck"), Remaining > 0.7f);
				Capture(TEXT("SB-02-Perched.png"));
				Stage = 15;
				NextAt = 0.0;
				return false;
			}
			case 15:
			{
				if (Enemy->GetHealth() == Health) { return false; }
				Test->TestEqual(TEXT("SIMAE the first peck deals only one damage"), Enemy->GetHealth(), Health - 1.0f);
				float Remaining = 0.0f;
				float Cycle = 0.0f;
				int32 PecksLeft = 0;
				Test->TestTrue(TEXT("SIMAE birds stay for a second peck"),
					AChaosImpactSimaeBird::HasPerchedBird(Enemy, &Remaining, &Cycle, &PecksLeft));
				Test->TestEqual(TEXT("SIMAE warning counts down the second peck separately"), Cycle, ChaosImpactBallTypes::SimaePeckIntervalSeconds);
				Test->TestEqual(TEXT("SIMAE one peck remains after the first hit"), PecksLeft, 1);
				Test->TestTrue(TEXT("SIMAE second peck is not instantaneous"), Remaining > 0.5f);
				Capture(TEXT("SB-05-FirstPeck.png"));
				Stage = 3;
				NextAt = Now + ChaosImpactBallTypes::SimaePeckIntervalSeconds + 0.3;
				return false;
			}
			case 3:
				Test->TestEqual(TEXT("SIMAE the flock deals two separate one-damage pecks, not one per bird"), Enemy->GetHealth(), Health - 2.0f);
				Test->TestEqual(TEXT("SIMAE the thrower stays unharmed"), Player->GetHealth(), PlayerHealth);
				Test->TestFalse(TEXT("SIMAE birds leave after the second peck"), AChaosImpactSimaeBird::HasPerchedBird(Enemy));
				ClearBirds();
				Enemy->ResetForOnlineMatch(Origin + Toward * 520.0f, (-Toward).Rotation());
				Health = Enemy->GetHealth();
				AChaosImpactSimaeBird::ReleaseFlock(World, Enemy->GetActorLocation(), Player);
				++Stage;
				return false;
			case 4:
				if (!AChaosImpactSimaeBird::HasPerchedBird(Enemy)) { return false; }
				Enemy->RequestAIDash(-Toward);
				Test->TestTrue(TEXT("SIMAE dash starts while birds are perched"), Enemy->IsDashing());
				++Stage;
				NextAt = Now + 2.4;
				return false;
			case 5:
				Test->TestEqual(TEXT("SIMAE shaking off the flock prevents damage"), Enemy->GetHealth(), Health);
				Test->TestFalse(TEXT("SIMAE a dash removes every perched bird"), AChaosImpactSimaeBird::HasPerchedBird(Enemy));
				ClearBirds();
				// Nobody in range: no bird may target its thrower or teammate, and the whole flock must expire.
				Player->SoloTeam = 8;
				Enemy->SoloTeam = 8;
				AChaosImpactSimaeBird::ReleaseFlock(World, Enemy->GetActorLocation(), Player);
				for (TActorIterator<AChaosImpactSimaeBird> It(World); It; ++It)
				{
					Test->TestNull(TEXT("SIMAE a friendly flock has no target"), It->GetTargetCharacter());
				}
				++Stage;
				NextAt = Now + 8.2;
				return false;
			case 6:
			{
				int32 Count = 0;
				for (TActorIterator<AChaosImpactSimaeBird> It(World); It; ++It) { ++Count; }
				Test->TestEqual(TEXT("SIMAE birds expire within eight seconds"), Count, 0);
				int32 RemainingBursts = 0;
				for (TActorIterator<AChaosImpactSimaeFeatherBurst> It(World); It; ++It) { ++RemainingBursts; }
				Test->TestEqual(TEXT("SIMAE temporary feather effects also expire"), RemainingBursts, 0);
				Player->SoloTeam = -1;
				Enemy->SoloTeam = -1;
				const FVector Side = FVector::CrossProduct(FVector::UpVector, Toward);
				SecondTarget = World->SpawnActor<AChaosImpactCharacter>(Player->GetClass(),
					Origin + Toward * 520.0f + Side * 260.0f, FRotator::ZeroRotator);
				if (!SecondTarget.IsValid()) { Test->AddError(TEXT("SIMAE could not create a second opponent.")); return true; }
				if (AController* Controller = SecondTarget->GetController()) { Controller->SetActorTickEnabled(false); }
				AChaosImpactSimaeBird::ReleaseFlock(World, Origin + Toward * 300.0f, Player);
				int32 FirstAssignments = 0;
				int32 SecondAssignments = 0;
				for (TActorIterator<AChaosImpactSimaeBird> It(World); It; ++It)
				{
					FirstAssignments += It->GetTargetCharacter() == Enemy ? 1 : 0;
					SecondAssignments += It->GetTargetCharacter() == SecondTarget.Get() ? 1 : 0;
				}
				Test->TestEqual(TEXT("SIMAE six birds initially target the first opponent"), FirstAssignments, 6);
				Test->TestEqual(TEXT("SIMAE six birds initially target the second opponent"), SecondAssignments, 6);
				// An opponent that appears AFTER release must still receive some in-flight birds.
				ThirdTarget = World->SpawnActor<AChaosImpactCharacter>(Player->GetClass(),
					Origin + Toward * 800.0f - Side * 260.0f, FRotator::ZeroRotator);
				if (!ThirdTarget.IsValid()) { Test->AddError(TEXT("SIMAE could not create a third opponent.")); return true; }
				if (AController* Controller = ThirdTarget->GetController()) { Controller->SetActorTickEnabled(false); }
				Stage = 16;
				NextAt = Now + 0.55;
				return false;
			}
			case 16:
			{
				int32 Assignments[3] = {0, 0, 0};
				for (TActorIterator<AChaosImpactSimaeBird> It(World); It; ++It)
				{
					Assignments[0] += It->GetTargetCharacter() == Enemy ? 1 : 0;
					Assignments[1] += It->GetTargetCharacter() == SecondTarget.Get() ? 1 : 0;
					Assignments[2] += It->GetTargetCharacter() == ThirdTarget.Get() ? 1 : 0;
				}
				UE_LOG(LogTemp, Display, TEXT("SIMAE dynamic distribution %d / %d / %d"), Assignments[0], Assignments[1], Assignments[2]);
				Test->TestTrue(TEXT("SIMAE spreads in-flight birds onto a newly arriving third enemy"),
					Assignments[0] >= 3 && Assignments[1] >= 3 && Assignments[2] >= 3);
				Test->TestEqual(TEXT("SIMAE redistribution keeps all twelve birds"), Assignments[0] + Assignments[1] + Assignments[2], 12);
				Capture(TEXT("SB-06-DistributedFlock.png"));
				Stage = 17;
				NextAt = Now + 0.2;
				return false;
			}
			case 17:
			{
				ClearBirds();
				if (SecondTarget.IsValid()) { SecondTarget->Destroy(); }
				if (ThirdTarget.IsValid()) { ThirdTarget->Destroy(); }
				Player->ResetForOnlineMatch(Origin, Toward.Rotation());
				for (int32 Slot = 0; Slot < 2; ++Slot)
				{
					const FTransform Where(FRotator::ZeroRotator, Origin + FVector::UpVector * 350.0f);
					AChaosImpactBall* Pickup = World->SpawnActorDeferred<AChaosImpactBall>(AChaosImpactBall::StaticClass(),
						Where, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
					Pickup->SetBallType(EChaosImpactBallType::Simae);
					Pickup->FinishSpawning(Where);
					Pickup->MakePickup();
					Test->TestTrue(TEXT("SIMAE can be collected in both inventory slots"), Player->TryPickupBall(Pickup));
					Pickup->Destroy();
				}
				const TArray<EChaosImpactBallType> Inventory{EChaosImpactBallType::Simae, EChaosImpactBallType::Simae};
				const uint8 Packed = ChaosImpactBallTypes::Pack(Inventory);
				Test->TestEqual(TEXT("SIMAE survives the four-bit inventory encoding"),
					ChaosImpactBallTypes::GetPackedSlot(Packed, 1), EChaosImpactBallType::Simae);
				TArray<UStaticMeshComponent*> Parts;
				Player->GetComponents(Parts);
				int32 CorrectHands = 0;
				for (const UStaticMeshComponent* Part : Parts)
				{
					if (Part->GetName() == TEXT("HeldBallMesh") || Part->GetName() == TEXT("LeftHeldBallMesh"))
					{
						CorrectHands += Part->GetStaticMesh() && Part->GetStaticMesh()->GetName() == TEXT("SM_SimaeBall") ? 1 : 0;
						if (Part->GetStaticMesh())
						{
							const FBox Box = Part->GetStaticMesh()->GetBoundingBox();
							Test->TestTrue(TEXT("SIMAE face mesh has a normal ball diameter"), Box.GetSize().GetMax() > 95.0 && Box.GetSize().GetMax() < 110.0);
							Test->TestTrue(TEXT("SIMAE face mesh is centered vertically in the hand"), FMath::Abs(Box.GetCenter().Z) < 1.0);
							UE_LOG(LogTemp, Display, TEXT("SIMAE hand %s mesh bounds %s world scale %s"),
								*Part->GetName(), *Part->GetStaticMesh()->GetBoundingBox().GetSize().ToString(), *Part->GetComponentScale().ToString());
						}
					}
				}
				Test->TestEqual(TEXT("SIMAE both hands use the face mesh"), CorrectHands, 2);
				Player->SetTrainingMenuFrozen(true);
				Camera = World->SpawnActor<ACameraActor>();
				const FVector Eye = Origin + Toward * 300.0f + FVector::UpVector * 130.0f;
				Camera->SetActorLocationAndRotation(Eye, (Origin + FVector::UpVector * 35.0f - Eye).Rotation());
				PC->SetViewTarget(Camera.Get());
				Stage = 7;
				NextAt = Now + 0.4;
				return false;
			}
			case 7:
				Capture(TEXT("SB-03-BothHands.png"));
				++Stage;
				NextAt = Now + 0.2;
				return false;
			case 8:
				Player->SetTrainingMenuFrozen(false);
				PC->SetViewTarget(Player);
				if (Camera.IsValid()) { Camera->Destroy(); }
				AChaosImpactSimaeBird::ReleaseFlock(World, Player->GetActorLocation(), Enemy);
				++Stage;
				return false;
			case 9:
				if (!AChaosImpactSimaeBird::HasPerchedBird(Player)) { return false; }
				Capture(TEXT("SB-04-PlayerWarning.png"));
				++Stage;
				NextAt = Now + 0.2;
				return false;
			case 10:
				Player->RequestAIDash(-Toward);
				++Stage;
				NextAt = Now + 2.2;
				return false;
			case 11:
				Test->TestEqual(TEXT("SIMAE a human player's dash prevents the peck"), Player->GetHealth(), Player->GetMaxHealth());
				Player->ResetForOnlineMatch(Origin, Toward.Rotation());
				ClearBirds();
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It) { It->Destroy(); }
				// Invoke the strong CPU's normal Tick after it notices a perch, without requiring an uncontrolled match.
				Enemy->ResetForOnlineMatch(Origin + Toward * 520.0f, (-Toward).Rotation());
				AChaosImpactSimaeBird::ReleaseFlock(World, Enemy->GetActorLocation(), Player);
				++Stage;
				return false;
			case 12:
				if (!AChaosImpactSimaeBird::HasPerchedBird(Enemy)) { return false; }
				if (AChaosImpactCPUController* CPU = Cast<AChaosImpactCPUController>(Enemy->GetController()))
				{
					CPU->SetDifficulty(ChaosImpactMatch::CPULevelStrong);
					CPU->Tick(0.016f);
				}
				++Stage;
				NextAt = Now + 0.75;
				return false;
			case 13:
				if (AChaosImpactCPUController* CPU = Cast<AChaosImpactCPUController>(Enemy->GetController())) { CPU->Tick(0.016f); }
				Test->TestTrue(TEXT("SIMAE the strong CPU uses a dash to shake birds off"), Enemy->IsDashing());
				++Stage;
				NextAt = Now + 2.1;
				return false;
			case 14:
				Test->TestEqual(TEXT("SIMAE CPU shake-off prevents damage"), Enemy->GetHealth(), Enemy->GetMaxHealth());
				ClearBirds();
				Enemy->ResetForOnlineMatch(Origin + Toward * 520.0f, (-Toward).Rotation());
				Health = Enemy->GetHealth();
				AChaosImpactSimaeBird::ReleaseFlock(World, Enemy->GetActorLocation(), Player);
				Stage = 18;
				return false;
			case 18:
				if (Enemy->GetHealth() == Health) { return false; }
				Test->TestEqual(TEXT("SIMAE a target can act after taking the first peck"), Enemy->GetHealth(), Health - 1.0f);
				Enemy->RequestAIDash(-Toward);
				Test->TestTrue(TEXT("SIMAE a dash is available between pecks"), Enemy->IsDashing());
				Stage = 19;
				NextAt = Now + ChaosImpactBallTypes::SimaePeckIntervalSeconds + 1.3;
				return false;
			case 19:
				Test->TestEqual(TEXT("SIMAE dashing after the first peck prevents the second"), Enemy->GetHealth(), Health - 1.0f);
				Test->TestFalse(TEXT("SIMAE a shaken-off flock never comes back to the same target"), AChaosImpactSimaeBird::HasPerchedBird(Enemy));
				ClearBirds();
				UE_LOG(LogTemp, Display, TEXT("SIMAE 12 birds, spaced pecks, mid-combo escape, dynamic distribution, allies, lifetime, hands, HUD and CPU checked"));
				return true;
			default: return true;
			}
		}

	private:
		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Stage = 0;
		float Health = 0.0f;
		float PlayerHealth = 0.0f;
		FVector Origin = FVector::ZeroVector;
		FVector Toward = FVector::ForwardVector;
		TWeakObjectPtr<AChaosImpactCharacter> Target;
		TWeakObjectPtr<AChaosImpactBall> Ball;
		TWeakObjectPtr<AChaosImpactCharacter> SecondTarget;
		TWeakObjectPtr<AChaosImpactCharacter> ThirdTarget;
		TWeakObjectPtr<ACameraActor> Camera;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactSimaeBallTest, "ChaosImpact.Training.SimaeBall",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactSimaeBallTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FSimaeBallCommand(this));
	return true;
}

#endif
