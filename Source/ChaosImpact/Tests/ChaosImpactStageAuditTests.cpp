#if WITH_DEV_AUTOMATION_TESTS
#include "ChaosImpactCharacter.h"
#include "ChaosImpactCyberStage.h"
#include "ChaosImpactForgeStage.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactSnowStage.h"
#include "ChaosImpactStageBase.h"
#include "ChaosImpactTempleStage.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/OverlapResult.h"
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
	 * A look over the VS stage of the level it runs in (a local match: Lvl_VS_StageN?...?CIMatch=1?CIStage=N-1 with
	 * -CIMatchSeconds=600). Logs STAGEAUDIT lines: holes in the floor, how much of the floor lies in the sun's shadow
	 * and which blocks cast it, and spawn points or ball pads stuck in geometry. Then films it from above, straight down
	 * and through the player's own camera at several points, then sets off the stage's gimmick and films its warning and
	 * the gimmick itself from above. Saved/StageAudit/<stage>_*.png.
	 */
	class FStageAuditCommand : public IAutomationLatentCommand
	{
	public:
		explicit FStageAuditCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - StartedAt > 240.0)
			{
				Test->AddError(FString::Printf(TEXT("Stage audit timed out at step %d"), Step));
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
			const AChaosImpactGameState* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
			if (!PC || !Match || Now < NextAt)
			{
				return false;
			}
			// Everybody else stands still the whole time.
			for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
			{
				if (!It->IsPlayerControlled())
				{
					It->SetTrainingMenuFrozen(true);
					It->SetActorLocation(FVector(0.0f, 0.0f, -20000.0f), false, nullptr, ETeleportType::TeleportPhysics);
				}
			}
			const auto Shot = [this](const FString& Name)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("StageAudit"), Label + TEXT("_") + Name), true, false);
			};
			if (Step == 0)
			{
				if (!Match->bVersusMatch || Match->Phase != EChaosImpactOnlinePhase::Match)
				{
					return false;
				}
				for (TActorIterator<AChaosImpactStageBase> It(World); It; ++It)
				{
					Stage = *It;
				}
				if (!Stage.IsValid())
				{
					Test->AddError(TEXT("No VS stage in this level"));
					return true;
				}
				Label = Stage->GetClass()->GetName().Replace(TEXT("ChaosImpact"), TEXT(""));
				Audit(World);
				Step = 1;
				return false;
			}
			const FVector Center = Stage->GetCenter();
			const float Half = Stage->GetHalfExtent();
			const auto Look = [&](const FVector& From, const FVector& At, const float Fov)
			{
				if (!Camera.IsValid())
				{
					Camera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity);
				}
				Camera->SetActorLocationAndRotation(From, (At - From).Rotation());
				Camera->GetCameraComponent()->SetFieldOfView(Fov);
				PC->SetViewTarget(Camera.Get());
			};
			APawn* Me = PC->GetPawn();
			switch (Step)
			{
			case 1:
				Look(Center + FVector(-Half * 1.15f, -Half * 1.15f, Half * 1.25f), Center, 62.0f);
				NextAt = Now + 0.8; ++Step; return false;
			case 2:
				Shot(TEXT("0_Overview.png"));
				NextAt = Now + 0.4; ++Step; return false;
			case 3:
				Look(Center + FVector(Half * 1.15f, Half * 0.2f, Half * 0.9f), Center, 62.0f);
				NextAt = Now + 0.8; ++Step; return false;
			case 4:
				Shot(TEXT("1_OverviewOther.png"));
				NextAt = Now + 0.4; ++Step; return false;
			case 5:
				Look(Center + FVector(0.0f, -1.0f, Half * 2.3f), Center, 70.0f);
				NextAt = Now + 0.8; ++Step; return false;
			case 6:
				Shot(TEXT("2_TopDown.png"));
				NextAt = Now + 0.4; ++Step; return false;
			default:
			{
				// Through the player's own camera, standing at spawn points and at the most shaded spots.
				const int32 Index = (Step - 7) / 2;
				if (!Me)
				{
					return true;
				}
				if (Index >= Views.Num())
				{
					// The gimmick: its warning part way through, then the gimmick itself.
					const int32 Gimmick = Step - 7 - Views.Num() * 2;
					if (Gimmick == 0)
					{
						AChaosImpactStageBase* Here = Stage.Get();
						if (auto* Snow = Cast<AChaosImpactSnowStage>(Here)) { Snow->StartWindGust(1); }
						else if (auto* Forge = Cast<AChaosImpactForgeStage>(Here)) { Forge->StartVentPair(0); }
						else if (auto* Temple = Cast<AChaosImpactTempleStage>(Here)) { Temple->StartPillar(0); }
						else if (auto* Cyber = Cast<AChaosImpactCyberStage>(Here)) { Cyber->StartGatePattern(0); }
						else { return true; }
						Look(Center + FVector(-Half * 1.15f, -Half * 1.15f, Half * 1.25f), Center, 62.0f);
						NextAt = Now + 1.6; ++Step; return false;
					}
					if (Gimmick == 1)
					{
						Shot(TEXT("4_GimmickWarn.png"));
						NextAt = Now + 1.4; ++Step; return false;
					}
					if (Gimmick == 2)
					{
						Shot(TEXT("5_Gimmick.png"));
						NextAt = Now + 0.5; ++Step; return false;
					}
					return true;
				}
				if ((Step - 7) % 2 == 0)
				{
					PC->SetViewTarget(Me);
					Me->SetActorLocation(Views[Index] + FVector(0.0f, 0.0f, 100.0f), false, nullptr, ETeleportType::TeleportPhysics);
					NextAt = Now + 0.9; ++Step; return false;
				}
				Shot(FString::Printf(TEXT("3_Play%02d.png"), Index));
				NextAt = Now + 0.2; ++Step; return false;
			}
			}
		}

	private:
		void Audit(UWorld* World)
		{
			const FVector Center = Stage->GetCenter();
			const float Half = Stage->GetHalfExtent();
			FVector SunTravel(0.0f, 0.0f, -1.0f);
			for (TActorIterator<ADirectionalLight> It(World); It; ++It)
			{
				SunTravel = It->GetActorForwardVector();
			}
			FCollisionQueryParams Query(TEXT("StageAudit"), true);
			for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
			{
				Query.AddIgnoredActor(*It);
			}
			int32 Cells = 0, Holes = 0, Walkable = 0, Shaded = 0;
			TMap<FString, int32> Casters;
			TArray<TPair<float, FVector>> ShadedSpots;
			constexpr float StepSize = 150.0f;
			FString HoleList;
			for (float X = -Half + StepSize * 0.5f; X < Half; X += StepSize)
			{
				for (float Y = -Half + StepSize * 0.5f; Y < Half; Y += StepSize)
				{
					++Cells;
					const FVector Top = Center + FVector(X, Y, 2500.0f);
					FHitResult Floor;
					if (!World->LineTraceSingleByChannel(Floor, Top, Top - FVector(0.0f, 0.0f, 4500.0f), ECC_WorldStatic, Query))
					{
						++Holes;
						if (HoleList.Len() < 600)
						{
							HoleList += FString::Printf(TEXT(" (%.0f,%.0f)"), X, Y);
						}
						continue;
					}
					if (Floor.ImpactNormal.Z < 0.7f || Floor.ImpactPoint.Z < Center.Z - 400.0f)
					{
						continue;
					}
					++Walkable;
					// A character's chest here: does the sun reach it?
					const FVector Chest = Floor.ImpactPoint + FVector(0.0f, 0.0f, 110.0f);
					FHitResult Block;
					if (World->LineTraceSingleByChannel(Block, Chest, Chest - SunTravel * 6000.0f, ECC_Visibility, Query))
					{
						++Shaded;
						Casters.FindOrAdd(Block.GetComponent() ? Block.GetComponent()->GetName() : TEXT("?"))++;
						ShadedSpots.Add({static_cast<float>(FVector::Dist2D(Floor.ImpactPoint, Center)), Floor.ImpactPoint});
					}
				}
			}
			Casters.ValueSort([](const int32 A, const int32 B) { return A > B; });
			FString Top;
			int32 Shown = 0;
			for (const auto& Pair : Casters)
			{
				if (Shown++ >= 14)
				{
					break;
				}
				Top += FString::Printf(TEXT(" %s=%d"), *Pair.Key, Pair.Value);
			}
			UE_LOG(LogTemp, Display, TEXT("STAGEAUDIT %s half=%.0f cells=%d holes=%d walkable=%d shaded=%d (%.0f%%) sun=%s"), *Label, Half, Cells, Holes,
				Walkable, Shaded, Walkable > 0 ? 100.0f * Shaded / Walkable : 0.0f, *SunTravel.ToString());
			UE_LOG(LogTemp, Display, TEXT("STAGEAUDIT %s casters:%s"), *Label, *Top);
			UE_LOG(LogTemp, Display, TEXT("STAGEAUDIT %s holes:%s"), *Label, *HoleList);
			// Spawns and pads must stand free: a character's capsule there must not overlap anything.
			const TArray<FVector> Spawns = Stage->GetSpawnPoints();
			const TArray<FVector> Balls = Stage->GetBallPoints();
			const auto Stuck = [&](const FVector& Floor, const float Radius, const float HalfHeight) -> FString
			{
				FHitResult Down;
				const bool bFloor = World->LineTraceSingleByChannel(Down, Floor + FVector(0, 0, 200), Floor - FVector(0, 0, 200), ECC_WorldStatic, Query);
				TArray<FOverlapResult> Overlaps;
				World->OverlapMultiByChannel(Overlaps, Floor + FVector(0.0f, 0.0f, HalfHeight + 6.0f), FQuat::Identity, ECC_Pawn,
					FCollisionShape::MakeCapsule(Radius, HalfHeight), Query);
				FString Into;
				for (const FOverlapResult& Overlap : Overlaps)
				{
					if (Overlap.GetComponent() && Overlap.GetComponent()->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block)
					{
						Into += Overlap.GetComponent()->GetName() + TEXT(" ");
					}
				}
				return FString::Printf(TEXT("floor=%s dz=%.0f %s"), bFloor ? TEXT("yes") : TEXT("NO"), bFloor ? Down.ImpactPoint.Z - Floor.Z : 0.0f,
					Into.IsEmpty() ? TEXT("free") : *(TEXT("INTO ") + Into));
			};
			for (int32 I = 0; I < Spawns.Num(); ++I)
			{
				UE_LOG(LogTemp, Display, TEXT("STAGEAUDIT %s spawn %d %s: %s"), *Label, I, *(Spawns[I] - Center).ToString(), *Stuck(Spawns[I], 42.0f, 90.0f));
			}
			for (int32 I = 0; I < Balls.Num(); ++I)
			{
				UE_LOG(LogTemp, Display, TEXT("STAGEAUDIT %s pad %d %s: %s"), *Label, I, *(Balls[I] - Center).ToString(), *Stuck(Balls[I], 30.0f, 40.0f));
			}
			// Where to film from: by the wall on the camera's side (the camera looks toward +X, so that is the -X edge:
			// the walls there must not hide the player), four spawns, then the shaded spots nearest the middle.
			for (float Reach = Half - 260.0f; Reach > Half * 0.5f; Reach -= 150.0f)
			{
				FHitResult Floor;
				const FVector Probe = Center + FVector(-Reach, 0.0f, 0.0f);
				if (World->LineTraceSingleByChannel(Floor, Probe + FVector(0.0f, 0.0f, 400.0f), Probe - FVector(0.0f, 0.0f, 400.0f), ECC_Visibility)
					&& Stuck(Floor.ImpactPoint, 42.0f, 90.0f).Contains(TEXT("free")))
				{
					Views.Add(Floor.ImpactPoint);
					break;
				}
			}
			for (int32 I = 0; I < Spawns.Num() && Views.Num() < 5; I += FMath::Max(1, Spawns.Num() / 4))
			{
				Views.Add(Spawns[I]);
			}
			ShadedSpots.Sort([](const TPair<float, FVector>& A, const TPair<float, FVector>& B) { return A.Key < B.Key; });
			for (int32 I = 0; I < ShadedSpots.Num() && Views.Num() < 8; I += FMath::Max(1, ShadedSpots.Num() / 3))
			{
				Views.Add(ShadedSpots[I].Value);
			}
		}

		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Step = 0;
		TWeakObjectPtr<AChaosImpactStageBase> Stage;
		TWeakObjectPtr<ACameraActor> Camera;
		FString Label;
		TArray<FVector> Views;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactStageAuditTest, "ChaosImpact.Versus.StageAudit",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FChaosImpactStageAuditTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FStageAuditCommand(this));
	return true;
}
#endif
