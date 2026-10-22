#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactBall.h"
#include "ChaosImpactBallSpawner.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactScreen.h"
#include "ChaosImpactTempleStage.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
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
	 * VS ステージ3 (the sky temple): picked on the stage select screen, a local match travels to its own level; the
	 * islands hold the spawn and ball points; the altar's pad gives special balls only; a character and a ball go
	 * through a portal and come out of the opposite corner (the ball hurting whoever is there); a pillar of light
	 * hurts whoever stands on its seal. Pictures in Saved/TempleStageQA.
	 */
	class FTempleStageCommand : public IAutomationLatentCommand
	{
	public:
		explicit FTempleStageCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - StartedAt > 240.0)
			{
				Test->AddError(FString::Printf(TEXT("Stuck at step %d."), Step));
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
			if (!World || Now < NextAt)
			{
				return false;
			}
			AChaosImpactPlayerController* PC = Cast<AChaosImpactPlayerController>(World->GetFirstPlayerController());
			const AChaosImpactGameState* Match = World->GetGameState<AChaosImpactGameState>();
			AChaosImpactTempleStage* Stage = nullptr;
			for (TActorIterator<AChaosImpactTempleStage> It(World); It; ++It)
			{
				Stage = *It;
			}
			const auto Shot = [](const FString& Name)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TempleStageQA"), Name), true, false);
			};
			const auto World3 = [Stage](const FVector& Offset) { return Stage->GetActorTransform().TransformPosition(Offset); };
			switch (Step)
			{
			case 0:
				// From the menus: a local match to watch, on ステージ3.
				if (!PC || !PC->GetWorld())
				{
					return false;
				}
				PC->ShowMenuScreen(EChaosImpactScreen::ModeSelect);
				PC->BeginLocalSpectate();
				PC->ChooseStage(2);
				Step = 1;
				NextAt = Now + 1.0;
				return false;
			case 1:
				if (!World->GetMapName().Contains(TEXT("Lvl_VS_Stage3")) || !Stage || !Match || Match->Phase != EChaosImpactOnlinePhase::Match)
				{
					return false;
				}
				UE_LOG(LogTemp, Display, TEXT("TEMPLETEST in %s, phase %d"), *World->GetMapName(), static_cast<int32>(Match->Phase));
				Test->TestEqual(TEXT("The match frames the temple's square"), Match->StageHalfExtent, AChaosImpactTempleStage::TempleHalfExtent);
				Shot(TEXT("1_Battle.png"));
				Step = 2;
				NextAt = Now + 3.0;
				return false;
			case 2:
			{
				Shot(TEXT("2_Battle.png"));
				// Every spawn and ball point stands on an island's floor.
				FCollisionQueryParams Params(SCENE_QUERY_STAT(TempleQA), false);
				int32 OnFloor = 0;
				TArray<FVector> Points = Stage->GetSpawnPoints();
				Points.Append(Stage->GetBallPoints());
				for (const FVector& Point : Points)
				{
					FHitResult Hit;
					if (World->LineTraceSingleByObjectType(Hit, Point + FVector(0.0f, 0.0f, 300.0f), Point - FVector(0.0f, 0.0f, 300.0f),
						FCollisionObjectQueryParams(ECC_WorldStatic), Params) && FMath::Abs(Hit.ImpactPoint.Z - Point.Z) < 30.0f)
					{
						++OnFloor;
					}
					else
					{
						Test->AddError(FString::Printf(TEXT("No floor under %s"), *Point.ToCompactString()));
					}
				}
				Test->TestEqual(TEXT("Spawn and ball points on floor"), OnFloor, Points.Num());
				// The altar's pad: special balls only.
				bool bAltar = false;
				for (TActorIterator<AChaosImpactBallSpawner> It(World); It; ++It)
				{
					if (FVector::Dist2D(It->GetActorLocation(), Stage->GetActorLocation()) < 200.0f)
					{
						bAltar = true;
						Test->TestEqual(TEXT("The altar never gives a normal ball"), It->GetChance(EChaosImpactBallType::Normal), 0.0f);
						if (const AChaosImpactBall* Ball = It->GetActiveBall())
						{
							Test->TestTrue(TEXT("The ball on the altar is special"), Ball->GetBallType() != EChaosImpactBallType::Normal);
						}
					}
				}
				Test->TestTrue(TEXT("A ball pad on the altar"), bAltar);
				// The CPUs hold still from here (their own controllers stop), so the checks below are not disturbed.
				for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
				{
					if (It->GetController() && !It->IsPlayerControlled())
					{
						It->GetController()->SetActorTickEnabled(false);
						It->GetCharacterMovement()->StopMovementImmediately();
						Cast<AChaosImpactCharacter>(*It)->CancelChargingThrow();
						Characters.Add(*It);
					}
				}
				if (!Test->TestTrue(TEXT("Enough CPUs to test with"), Characters.Num() >= 3))
				{
					return true;
				}
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It)
				{
					if (!It->IsPickup())
					{
						It->Destroy();
					}
				}
				// A character steps into portal 0 (north-east) ...
				AChaosImpactCharacter* Walker = Characters[0].Get();
				Walker->SetActorLocation(World3(AChaosImpactTempleStage::GetPortalOffset(0) + AChaosImpactTempleStage::GetPortalFacing(0) * 30.0f
					+ FVector(0.0f, 0.0f, 100.0f)), false, nullptr, ETeleportType::TeleportPhysics);
				Walker->GetCharacterMovement()->StopMovementImmediately();
				Step = 3;
				NextAt = Now + 0.4;
				return false;
			}
			case 3:
			{
				// ... and comes out of portal 2 (south-west).
				const FVector Exit = World3(AChaosImpactTempleStage::GetPortalOffset(2));
				const float Distance = FVector::Dist2D(Characters[0]->GetActorLocation(), Exit);
				UE_LOG(LogTemp, Display, TEXT("TEMPLETEST walker at %s, %.0f from the far gate"), *Characters[0]->GetActorLocation().ToCompactString(), Distance);
				Test->TestTrue(TEXT("Walking into a portal comes out of the opposite corner's"), Distance < 400.0f);
				Step = 31;
				return false;
			}
			case 31:
			{
				// A ball thrown into portal 1 (south-east) flies out of portal 3 (north-west) into whoever stands there
				// (a CPU that is up and not mid-dash: a dash slips through a ball).
				AChaosImpactCharacter* Thrower = Characters[1].Get();
				AChaosImpactCharacter* Target = nullptr;
				for (int32 Index = 2; Index < Characters.Num(); ++Index)
				{
					if (Characters[Index].IsValid() && !Characters[Index]->IsEliminated() && !Characters[Index]->IsDashing() && Characters[Index]->GetHealth() > 1.0f)
					{
						Target = Characters[Index].Get();
						TargetIndex = Index;
						break;
					}
				}
				if (!Target)
				{
					return false;
				}
				const FVector InFacing = AChaosImpactTempleStage::GetPortalFacing(1);
				Thrower->SetActorLocation(World3(AChaosImpactTempleStage::GetPortalOffset(1) + InFacing * 520.0f + FVector(0.0f, 0.0f, 100.0f)),
					false, nullptr, ETeleportType::TeleportPhysics);
				Target->SetActorLocation(World3(AChaosImpactTempleStage::GetPortalOffset(3) + AChaosImpactTempleStage::GetPortalFacing(3) * 460.0f
					+ FVector(0.0f, 0.0f, 100.0f)), false, nullptr, ETeleportType::TeleportPhysics);
				TargetHealth = Target->GetHealth();
				const FTransform Where(FRotator::ZeroRotator, Thrower->GetActorLocation() - InFacing * 120.0f + FVector(0.0f, 0.0f, 10.0f));
				AChaosImpactBall* Ball = World->SpawnActorDeferred<AChaosImpactBall>(AChaosImpactBall::StaticClass(), Where, Thrower, Thrower,
					ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				Ball->SetBallType(EChaosImpactBallType::Normal);
				Ball->FinishSpawning(Where);
				Ball->Launch(-InFacing, 2200.0f, EChaosImpactBallFlightMode::Straight);
				Step = 4;
				NextAt = Now + 0.25;
				return false;
			}
			case 4:
				Shot(TEXT("3_Portal.png"));
				Step = 5;
				NextAt = Now + 1.0;
				return false;
			case 5:
			{
				AChaosImpactCharacter* Target = Characters[TargetIndex].Get();
				UE_LOG(LogTemp, Display, TEXT("TEMPLETEST target health %.1f -> %.1f at %s (try %d)"), TargetHealth, Target->GetHealth(),
					*Target->GetActorLocation().ToCompactString(), Tries + 1);
				const bool bHit = Target->GetHealth() < TargetHealth || Target->IsEliminated();
				if (!bHit && ++Tries < 3)
				{
					Step = 31;
					NextAt = Now + 0.5;
					return false;
				}
				Test->TestTrue(TEXT("The ball came through the portal and hit the CPU beyond it"), bHit);
				// A pillar of light on seal 0 (north) with a CPU standing on it.
				AChaosImpactCharacter* Victim = Characters[1].Get();
				Victim->SetActorLocation(World3(AChaosImpactTempleStage::GetSealOffset(0) + FVector(0.0f, 0.0f, 100.0f)), false, nullptr,
					ETeleportType::TeleportPhysics);
				VictimHealth = Victim->GetHealth();
				Stage->StartPillar(0);
				Step = 6;
				NextAt = Now + AChaosImpactTempleStage::PillarWarnSeconds * 0.6f;
				return false;
			}
			case 6:
				Test->TestEqual(TEXT("The warning does not hurt"), Characters[1]->GetHealth(), VictimHealth);
				Shot(TEXT("4_PillarWarning.png"));
				Step = 7;
				NextAt = Now + AChaosImpactTempleStage::PillarWarnSeconds * 0.4f + 0.25f;
				return false;
			case 7:
				Shot(TEXT("5_PillarStrike.png"));
				Step = 8;
				NextAt = Now + 0.6;
				return false;
			case 8:
			{
				AChaosImpactCharacter* Victim = Characters[1].Get();
				UE_LOG(LogTemp, Display, TEXT("TEMPLETEST pillar: health %.1f -> %.1f"), VictimHealth, Victim->GetHealth());
				Test->TestTrue(TEXT("The pillar of light hurts whoever stands on its seal"), Victim->GetHealth() < VictimHealth || Victim->IsEliminated());
				// The whole temple from above.
				Camera = World->SpawnActor<ACameraActor>(World3(FVector(-7200.0f, 0.0f, 8200.0f)), FRotator(-48.0f, 0.0f, 0.0f));
				Camera->GetCameraComponent()->SetFieldOfView(70.0f);
				PC->SetViewTarget(Camera.Get());
				Step = 9;
				NextAt = Now + 1.5;
				return false;
			}
			case 9:
				Shot(TEXT("6_Overview.png"));
				Camera->SetActorLocationAndRotation(World3(FVector(-2600.0f, -2200.0f, 1500.0f)), FRotator(-28.0f, 35.0f, 0.0f));
				Step = 10;
				NextAt = Now + 1.0;
				return false;
			case 10:
				Shot(TEXT("7_Low.png"));
				Step = 11;
				NextAt = Now + 0.5;
				return false;
			default:
				return true;
			}
		}

	private:
		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Step = 0;
		TArray<TWeakObjectPtr<AChaosImpactCharacter>> Characters;
		float TargetHealth = 0.0f;
		int32 TargetIndex = 2;
		int32 Tries = 0;
		float VictimHealth = 0.0f;
		TWeakObjectPtr<ACameraActor> Camera;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactTempleStageTest, "ChaosImpact.Versus.TempleStage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactTempleStageTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FTempleStageCommand(this));
	return true;
}

#endif
