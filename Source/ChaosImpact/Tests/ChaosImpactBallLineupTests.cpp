#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactBall.h"
#include "ChaosImpactBallSpawner.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactPlayerController.h"
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
	/**
	 * Every kind of ball lying side by side on the floor, seen from the game's height and up close, to check that no two
	 * look alike at a glance (their colours and their marks). Also each kind held in turn, for its HUD slot.
	 * Run in training (?CITraining=1?CITargets=0). Saved/BallLineup/*.png.
	 */
	class FBallLineupCommand : public IAutomationLatentCommand
	{
	public:
		explicit FBallLineupCommand(FAutomationTestBase* InTest) : Test(InTest) {}

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
			if (!Player || !PC->IsGameplayActive())
			{
				if (RealNow - StartedAt > 120.0)
				{
					Test->AddError(TEXT("No playable character."));
					return true;
				}
				return false;
			}
			const double Now = World->GetTimeSeconds();
			if (Now < NextAt)
			{
				return false;
			}
			const auto Shot = [](const FString& Name)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("BallLineup"), Name), true, false);
			};
			const FVector Forward = Player->GetActorForwardVector().GetSafeNormal2D();
			const FVector Side = FVector::CrossProduct(FVector::UpVector, Forward);
			switch (Stage)
			{
			case 0:
			{
				for (TActorIterator<AChaosImpactBallSpawner> It(World); It; ++It)
				{
					It->SetActorTickEnabled(false);
					It->SetActorHiddenInGame(true);
				}
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It)
				{
					It->Destroy();
				}
				for (TActorIterator<AChaosImpactCPUController> It(World); It; ++It)
				{
					if (APawn* Pawn = It->GetPawn())
					{
						Pawn->SetActorLocation(Pawn->GetActorLocation() + FVector(0.0f, 0.0f, -5000.0f));
					}
				}
				// Two rows in front of the player (all kinds, the plain ball first).
				FHitResult Floor;
				const FVector Feet = Player->GetActorLocation();
				for (int32 Index = 0; Index < ChaosImpactBallTypes::Count; ++Index)
				{
					const int32 Row = Index / 5;
					const int32 Column = Index % 5;
					FVector At = Feet + Forward * (260.0f + Row * 150.0f) + Side * ((Column - 2.0f) * 150.0f);
					if (World->LineTraceSingleByObjectType(Floor, At + FVector(0.0f, 0.0f, 300.0f), At - FVector(0.0f, 0.0f, 600.0f),
						FCollisionObjectQueryParams(ECC_WorldStatic)))
					{
						At = Floor.ImpactPoint + FVector(0.0f, 0.0f, 40.0f);
					}
					const FTransform Where(FRotator::ZeroRotator, At);
					AChaosImpactBall* Ball = World->SpawnActorDeferred<AChaosImpactBall>(AChaosImpactBall::StaticClass(), Where, nullptr, nullptr,
						ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
					if (Ball)
					{
						Ball->SetBallType(ChaosImpactBallTypes::FromIndex(Index));
						Ball->FinishSpawning(Where);
						Ball->MakePickup();
						Ball->SetActorEnableCollision(false);
					}
				}
				Center = Feet + Forward * 410.0f;
				Stage = 1;
				NextAt = Now + 1.2;
				return false;
			}
			case 1:
				Shot(TEXT("Lineup_GameView.png"));
				Stage = 2;
				NextAt = Now + 0.3;
				return false;
			case 2:
			{
				FActorSpawnParameters Parameters;
				Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				ACameraActor* Camera = World->SpawnActor<ACameraActor>(Parameters);
				Camera->GetCameraComponent()->SetFieldOfView(62.0f);
				const FVector Eye = Center - Forward * 330.0f + FVector(0.0f, 0.0f, 520.0f);
				Camera->SetActorLocationAndRotation(Eye, (Center - Eye).Rotation());
				PC->SetViewTarget(Camera);
				Stage = 3;
				NextAt = Now + 1.0;
				return false;
			}
			case 3:
				Shot(TEXT("Lineup_Close.png"));
				PC->SetViewTarget(Player);
				Kind = 1;
				Stage = 4;
				NextAt = Now + 0.3;
				return false;
			case 4:
				// Each special kind held (alone), for its slot on the HUD.
				if (Kind >= ChaosImpactBallTypes::Count)
				{
					return true;
				}
				Player->ResetForOnlineMatch(Player->GetActorLocation(), Player->GetActorRotation());
				Player->GiveContestedPickup(ChaosImpactBallTypes::FromIndex(Kind));
				Stage = 5;
				NextAt = Now + 0.35;
				return false;
			case 5:
				Shot(FString::Printf(TEXT("Held_%02d_%s.png"), Kind, ChaosImpactBallTypes::GetInternalName(ChaosImpactBallTypes::FromIndex(Kind))));
				++Kind;
				Stage = 4;
				NextAt = Now + 0.2;
				return false;
			default:
				return true;
			}
		}

	private:
		FAutomationTestBase* Test = nullptr;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Stage = 0;
		int32 Kind = 1;
		FVector Center = FVector::ZeroVector;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactBallLineupTest, "ChaosImpact.VFX.BallLineup",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactBallLineupTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FBallLineupCommand(this));
	return true;
}

#endif
