#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactCharacter.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactSplashStage.h"
#include "ChaosImpactVersusStage.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	AChaosImpactPlayerController* FindSplashTestController()
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

	/**
	 * Run on a local VS level opened with ?CITraining=1?CIMatch=1?CIStage=1 and 7 CPUs (8 competitors):
	 * the splash stage is the one in use, every spawn and ball point stands on open floor, the CPUs get
	 * around it and score, and nobody leaves the arena. Screenshots go to Saved/SplashStage.
	 */
	class FSplashStageCommand : public IAutomationLatentCommand
	{
	public:
		explicit FSplashStageCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now < NextAt)
			{
				return false;
			}
			AChaosImpactPlayerController* PC = FindSplashTestController();
			UWorld* World = PC ? PC->GetWorld() : nullptr;
			AChaosImpactGameState* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
			if (Now - StartedAt > 150.0)
			{
				Test->AddError(FString::Printf(TEXT("Timed out at stage %d"), Stage));
				return true;
			}
			if (!Match || !Match->bVersusMatch || Match->Phase != EChaosImpactOnlinePhase::Match)
			{
				return false;
			}
			const FVector Center = Match->StageCenter;
			const auto Capture = [](const TCHAR* Name)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SplashStage"), Name),
					true, false);
			};
			const auto ShowFrom = [&](const FVector& Eye, const FVector& LookAt, const float FieldOfView)
			{
				if (!Camera.IsValid())
				{
					Camera = World->SpawnActor<ACameraActor>(Eye, FRotator::ZeroRotator);
				}
				Camera->SetActorLocationAndRotation(Eye, (LookAt - Eye).Rotation());
				Camera->GetCameraComponent()->SetFieldOfView(FieldOfView);
				Camera->GetCameraComponent()->SetConstraintAspectRatio(false);
				PC->SetViewTarget(Camera.Get());
			};

			switch (Stage)
			{
			case 0:
			{
				int32 SplashStages = 0;
				int32 StandardStages = 0;
				AChaosImpactSplashStage* Splash = nullptr;
				for (TActorIterator<AChaosImpactSplashStage> It(World); It; ++It) { ++SplashStages; Splash = *It; }
				for (TActorIterator<AChaosImpactVersusStage> It(World); It; ++It) { ++StandardStages; }
				Test->TestEqual(TEXT("One splash stage"), SplashStages, 1);
				Test->TestEqual(TEXT("No standard stage beside it"), StandardStages, 0);
				Test->TestEqual(TEXT("The rules name the splash stage"), Match->Rules.StageIndex, 1);
				Test->TestEqual(TEXT("The game state knows the bigger stage"), Match->StageHalfExtent,
					AChaosImpactSplashStage::SplashHalfExtent);
				if (!Splash)
				{
					return true;
				}
				Test->TestTrue(TEXT("Made from the editable Blueprint"), Splash->GetClass()->GetName().StartsWith(TEXT("BP_SplashStage")));

				FCollisionObjectQueryParams Walls;
				Walls.AddObjectTypesToQuery(ECC_WorldStatic);
				FCollisionQueryParams Params(SCENE_QUERY_STAT(SplashStageTest), false);
				for (ACharacter* Character : TActorRange<ACharacter>(World)) { Params.AddIgnoredActor(Character); }
				const auto CheckPoint = [&](const FVector& Point, const TCHAR* Kind, const int32 Index, const float Radius,
					const float HalfHeight)
				{
					FHitResult Floor;
					const bool bFloor = World->LineTraceSingleByObjectType(Floor, Point + FVector(0, 0, 400),
						Point - FVector(0, 0, 400), Walls, Params);
					Test->TestTrue(FString::Printf(TEXT("%s %d stands on the floor"), Kind, Index),
						bFloor && FMath::Abs(Floor.ImpactPoint.Z - Point.Z) < 4.0f);
					const bool bBlocked = World->OverlapAnyTestByObjectType(Point + FVector(0, 0, HalfHeight + 6.0f),
						FQuat::Identity, Walls, FCollisionShape::MakeCapsule(Radius, HalfHeight), Params);
					Test->TestFalse(FString::Printf(TEXT("%s %d is clear of the blocks"), Kind, Index), bBlocked);
					Test->TestTrue(FString::Printf(TEXT("%s %d is inside the walls"), Kind, Index),
						FMath::Abs(Point.X - Center.X) < AChaosImpactSplashStage::SplashHalfExtent - 60.0f
						&& FMath::Abs(Point.Y - Center.Y) < AChaosImpactSplashStage::SplashHalfExtent - 60.0f);
				};
				const TArray<FVector> Spawns = Splash->GetSpawnPoints();
				const TArray<FVector> Balls = Splash->GetBallPoints();
				Test->TestTrue(TEXT("Enough spawn points for 8"), Spawns.Num() >= 8);
				for (int32 Index = 0; Index < Spawns.Num(); ++Index) { CheckPoint(Spawns[Index], TEXT("Spawn"), Index, 44.0f, 96.0f); }
				for (int32 Index = 0; Index < Balls.Num(); ++Index) { CheckPoint(Balls[Index], TEXT("Ball pad"), Index, 40.0f, 40.0f); }
				float Closest = TNumericLimits<float>::Max();
				for (int32 A = 0; A < Spawns.Num(); ++A)
				{
					for (int32 B = A + 1; B < Spawns.Num(); ++B) { Closest = FMath::Min(Closest, FVector::Dist2D(Spawns[A], Spawns[B])); }
				}
				UE_LOG(LogTemp, Display, TEXT("SPLASHQA spawns %d, ball pads %d, closest spawns %.0f"), Spawns.Num(), Balls.Num(), Closest);
				Test->TestTrue(TEXT("Spawns keep a safe distance apart"), Closest >= 1000.0f);

				int32 Competitors = 0;
				for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It) { ++Competitors; }
				Test->TestEqual(TEXT("Eight competitors"), Competitors, 8);
				Capture(TEXT("00-Play.png"));
				Stage = 1;
				NextAt = Now + 0.5;
				return false;
			}
			case 1:
				ShowFrom(Center + FVector(0, -1, 9800), Center, 70.0f);
				Stage = 2;
				NextAt = Now + 0.8;
				return false;
			case 2:
				// A screenshot is taken at the end of the frame, so the view only changes on the next step.
				Capture(TEXT("01-Overhead.png"));
				Stage = 3;
				NextAt = Now + 0.3;
				return false;
			case 3:
				ShowFrom(Center + FVector(-4600, -4600, 3600), Center, 70.0f);
				Stage = 4;
				NextAt = Now + 0.8;
				return false;
			case 4:
				Capture(TEXT("02-Angle.png"));
				Stage = 41;
				NextAt = Now + 0.3;
				return false;
			case 41:
				ShowFrom(Center + FVector(2200, -700, 900), Center + FVector(900, 500, 0), 75.0f);
				Stage = 42;
				NextAt = Now + 0.8;
				return false;
			case 42:
				Capture(TEXT("03-Close.png"));
				Stage = 44;
				NextAt = Now + 0.3;
				return false;
			case 44:
			{
				// Close to a terrace floor, to see its surface.
				const float Reach = AChaosImpactSplashStage::SplashHalfExtent;
				ShowFrom(Center + FVector(Reach * 0.62f, -700.0f, 900.0f), Center + FVector(Reach * 0.86f, 0.0f, 260.0f), 70.0f);
				Stage = 45;
				NextAt = Now + 1.2;
				return false;
			}
			case 45:
				Capture(TEXT("05-Terrace.png"));
				Stage = 43;
				NextAt = Now + 0.3;
				return false;
			case 43:
				PC->SetViewTarget(PC->GetPawn());
				PlayStartedAt = Now;
				Stage = 5;
				return false;
			case 5:
			{
				// Follow how high each character gets while the CPUs play: the terraces are meant to be used.
				for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
				{
					// Only a climb counts: someone who started on a terrace has to reach the ground first.
					const float Feet = It->GetActorLocation().Z - Center.Z - 96.0f;
					float& Highest = HighestFeet.FindOrAdd(It->GetName(), -1.0f);
					if (Feet < 30.0f)
					{
						Highest = FMath::Max(Highest, 0.0f);
					}
					else if (Highest >= 0.0f)
					{
						Highest = FMath::Max(Highest, Feet);
					}
				}
				if (Now - PlayStartedAt < 30.0)
				{
					return false;
				}
				int32 Climbers = 0;
				for (const TPair<FString, float>& Pair : HighestFeet)
				{
					UE_LOG(LogTemp, Display, TEXT("SPLASHQA %s highest climb %.0f"), *Pair.Key, Pair.Value);
					Climbers += Pair.Value > 100.0f ? 1 : 0;
				}
				UE_LOG(LogTemp, Display, TEXT("SPLASHQA %d of %d climbed from the ground onto a raised tier"), Climbers, HighestFeet.Num());
				// The CPUs have played for a while: they found balls, hit someone and stayed in the arena.
				int32 TotalPoints = 0;
				for (APlayerState* State : Match->PlayerArray)
				{
					if (const AChaosImpactPlayerState* Member = Cast<AChaosImpactPlayerState>(State)) { TotalPoints += Member->Points; }
				}
				UE_LOG(LogTemp, Display, TEXT("SPLASHQA points after %.0f s: %d"), Now - PlayStartedAt, TotalPoints);
				Test->TestTrue(TEXT("The CPUs score on this stage"), TotalPoints > 0);
				Test->TestTrue(TEXT("CPUs climb onto the raised tiers"), Climbers > 0);
				for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
				{
					const FVector At = It->GetActorLocation();
					Test->TestTrue(FString::Printf(TEXT("%s stays inside the arena"), *It->GetName()),
						FMath::Abs(At.X - Center.X) < AChaosImpactSplashStage::SplashHalfExtent
						&& FMath::Abs(At.Y - Center.Y) < AChaosImpactSplashStage::SplashHalfExtent && At.Z > Center.Z - 50.0f);
				}
				Capture(TEXT("04-Match.png"));
				Stage = 6;
				NextAt = Now + 0.5;
				return false;
			}
			default:
				return true;
			}
		}

	private:
		FAutomationTestBase* Test;
		TWeakObjectPtr<ACameraActor> Camera;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		double PlayStartedAt = 0.0;
		int32 Stage = 0;
		TMap<FString, float> HighestFeet;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactSplashStageTest, "ChaosImpact.Versus.SplashStage",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactSplashStageTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FSplashStageCommand(this));
	return true;
}

#endif
