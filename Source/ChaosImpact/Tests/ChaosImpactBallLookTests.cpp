#if WITH_DEV_AUTOMATION_TESTS
// The normal ball's look (glossy black, bands of light-blue light) in the hand and on the floor; マグロ holding a snowball
// after a prison ball (no cage left showing); a big snowball thrown at someone near comes down on them at full speed.
#include "ChaosImpactBall.h"
#include "ChaosImpactBallTypes.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactGameMode.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactTrainingTarget.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	class FBallLookCommand : public IAutomationLatentCommand
	{
	public:
		explicit FBallLookCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		bool Update() override
		{
			AChaosImpactPlayerController* PC = nullptr;
			for (const auto& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld()) { PC = Cast<AChaosImpactPlayerController>(Context.World()->GetFirstPlayerController()); }
			}
			AChaosImpactCharacter* Player = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			AChaosImpactPlayerState* State = Player ? Player->GetPlayerState<AChaosImpactPlayerState>() : nullptr;
			if (FPlatformTime::Seconds() - Started > 120.0)
			{
				Test->AddError(FString::Printf(TEXT("Timed out at step %d"), Step));
				return true;
			}
			if (!Player || !State || !PC->IsGameplayActive())
			{
				return false;
			}
			UWorld* World = Player->GetWorld();
			if (Camera.IsValid())
			{
				const FVector Focus = Player->GetActorLocation() + FVector(60, 0, 20);
				const FVector Eye = Focus + FVector(300, 220, 160);
				Camera->SetActorLocationAndRotation(Eye, (Focus - Eye).Rotation());
				PC->SetViewTarget(Camera.Get());
			}
			const double Now = World->GetTimeSeconds();
			if (Now < NextAt)
			{
				return false;
			}
			const auto Shot = [](const TCHAR* Name)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("BallLookQA"), Name), true, false);
			};
			const auto Give = [&](const EChaosImpactBallType Type)
			{
				AChaosImpactBall* Ball = World->SpawnActor<AChaosImpactBall>(Player->GetActorLocation() + FVector(0, 0, 500), FRotator::ZeroRotator);
				Ball->SetBallType(Type);
				Ball->MakePickup();
				const bool bTaken = Player->TryPickupBall(Ball);
				Ball->Destroy();
				return bTaken;
			};
			// Any cage (a procedural mesh under a held ball) showing now.
			const auto CageShown = [Player]()
			{
				TArray<UProceduralMeshComponent*> Meshes;
				Player->GetComponents(Meshes);
				for (const UProceduralMeshComponent* Mesh : Meshes)
				{
					const USceneComponent* Parent = Mesh->GetAttachParent();
					if (Parent && Parent->IsA<UStaticMeshComponent>() && Mesh->IsVisible() && Parent->GetOwner() == Player
						&& Parent->GetName().Contains(TEXT("Held")))
					{
						return true;
					}
				}
				return false;
			};
			switch (Step)
			{
			case 0:
				Original = State->CharacterIndex;
				State->CharacterIndex = 5;
				State->ColourChoice = 0;
				State->ForceNetUpdate();
				Player->ResetForOnlineMatch(Player->GetActorLocation(), FRotator::ZeroRotator);
				Camera = World->SpawnActor<ACameraActor>();
				Camera->GetCameraComponent()->SetFieldOfView(48.0f);
				NextAt = Now + 0.8;
				break;
			case 1:
				Test->TestTrue(TEXT("The normal ball's material is there"), ChaosImpactBallTypes::GetNormalBallMaterial() != nullptr);
				Test->TestTrue(TEXT("Normal ball picked up"), Give(EChaosImpactBallType::Normal));
				Test->TestTrue(TEXT("Second normal ball"), Give(EChaosImpactBallType::Normal));
				// A few lying on the floor in front.
				for (int32 Index = 0; Index < 4; ++Index)
				{
					AChaosImpactBall* Lying = World->SpawnActor<AChaosImpactBall>(Player->GetActorLocation() + FVector(150.0f + Index * 45.0f, -90.0f + Index * 60.0f, -60.0f),
						FRotator(0.0f, Index * 40.0f, Index * 25.0f));
					if (Lying)
					{
						Lying->SetBallType(EChaosImpactBallType::Normal);
						Lying->MakePickup();
						Floor.Add(Lying);
					}
				}
				NextAt = Now + 0.6;
				break;
			case 2:
				Shot(TEXT("NormalHeldAndFloor.png"));
				NextAt = Now + 0.4;
				break;
			case 3:
				for (AChaosImpactBall* Lying : Floor) { if (IsValid(Lying)) { Lying->Destroy(); } }
				Player->RequestDropBall();
				Player->RequestDropBall();
				Test->TestTrue(TEXT("Prison ball picked up"), Give(EChaosImpactBallType::Prison));
				NextAt = Now + 0.5;
				break;
			case 4:
				Test->TestTrue(TEXT("The prison ball's cage shows while it is held"), CageShown());
				Shot(TEXT("FishPrison.png"));
				Player->RequestDropBall();
				NextAt = Now + 0.5;
				break;
			case 5:
				Test->TestTrue(TEXT("Snowball picked up"), Give(EChaosImpactBallType::Snow));
				NextAt = Now + 0.6;
				break;
			case 6:
				Test->TestFalse(TEXT("No cage over the snowball afterwards"), CageShown());
				Shot(TEXT("FishSnowAfterPrison.png"));
				NextAt = Now + 0.3;
				break;
			case 7:
			{
				// A big snowball over the head at a target close in front: full speed, aimed down onto it; far off: as before.
				const FVector Ahead = Player->GetActorForwardVector().GetSafeNormal2D();
				Player->SetAIAimDirection(Ahead);
				const float Full = 1600.0f;
				AChaosImpactTrainingTarget* Near = World->SpawnActor<AChaosImpactTrainingTarget>(Player->GetActorLocation() + Ahead * 350.0f, FRotator::ZeroRotator);
				float NearUp = 0.0f;
				const float NearSpeed = Player->GetOverheadSnowSpeed(3.0f, Full, &NearUp);
				UE_LOG(LogTemp, Display, TEXT("BALLLOOK near target: speed %.0f up %.0f"), NearSpeed, NearUp);
				Test->TestEqual(TEXT("Thrown at full speed at someone near"), NearSpeed, Full);
				Test->TestTrue(TEXT("Flatter than the usual lob, to come down on them"), NearUp < ChaosImpactBallTypes::SnowThrowUpSpeed);
				// Where it is at their distance: about their middle.
				if (Near)
				{
					const float Gravity = -World->GetGravityZ();
					const float T = 350.0f / Full;
					const float Release = static_cast<float>(Player->GetActorLocation().Z + Player->GetOverheadHoldOffset(EChaosImpactBallType::Snow, 3.0f).Z);
					const float Z = Release + NearUp * T - 0.5f * Gravity * T * T;
					UE_LOG(LogTemp, Display, TEXT("BALLLOOK at the target: %.0f (its middle %.0f)"), Z, Near->GetActorLocation().Z);
					Test->TestTrue(TEXT("It comes down on them"), FMath::Abs(Z - Near->GetActorLocation().Z) < 80.0f);
					Near->Destroy();
				}
				float FarUp = 0.0f;
				Player->GetOverheadSnowSpeed(3.0f, Full, &FarUp);
				Test->TestEqual(TEXT("Nobody near: the usual lob"), FarUp, ChaosImpactBallTypes::SnowThrowUpSpeed);
				Player->RequestDropBall();
				State->CharacterIndex = Original;
				State->ForceNetUpdate();
				PC->SetViewTarget(Player);
				if (Camera.IsValid()) { Camera->Destroy(); }
				return true;
			}
			default:
				return true;
			}
			++Step;
			return false;
		}

	private:
		FAutomationTestBase* Test;
		int32 Step = 0;
		int32 Original = 0;
		TArray<AChaosImpactBall*> Floor;
		TWeakObjectPtr<ACameraActor> Camera;
		double NextAt = 0.0;
		double Started = FPlatformTime::Seconds();
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBallLookTest, "ChaosImpact.Look.BallsAndSnow", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FBallLookTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FBallLookCommand(this));
	return true;
}

namespace
{
	/** A match won by this machine's player (started with -CIDevPaint=1): the podium shows the picture they wear. */
	class FResultsPaintCommand : public IAutomationLatentCommand
	{
	public:
		explicit FResultsPaintCommand(FAutomationTestBase* InTest) : Test(InTest) {}
		bool Update() override
		{
			UWorld* World = nullptr;
			for (const auto& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld()) { World = Context.World(); }
			}
			auto* GM = World ? World->GetAuthGameMode<AChaosImpactGameMode>() : nullptr;
			auto* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
			APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
			const double Now = FPlatformTime::Seconds();
			if (Now - Started > 150.0) { Test->AddError(TEXT("Timed out")); return true; }
			if (!GM || !Match || !PC || Now < NextAt) { return false; }
			if (PC->IsPaused()) { PC->SetPause(false); }
			switch (Step)
			{
			case 0:
			{
				FChaosImpactMatchRules Rules;
				Rules.CPUCount = 1;
				GM->ConfigureVersusMatch(Rules);
				GM->ConfirmTeamsAndStart();
				break;
			}
			case 1:
				if (Match->Phase != EChaosImpactOnlinePhase::Match) { return false; }
				if (AChaosImpactPlayerState* Own = PC->GetPlayerState<AChaosImpactPlayerState>())
				{
					Test->TestTrue(TEXT("This player wears a picture (-CIDevPaint)"), Own->PaintKey != 0);
					Own->Points = 50;
				}
				GM->EndMatchForTests();
				NextAt = Now + 6.0;
				break;
			case 2:
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("BallLookQA/ResultsPaint.png")), true, false);
				NextAt = Now + 1.0;
				break;
			default:
				return true;
			}
			++Step;
			return false;
		}
	private:
		FAutomationTestBase* Test;
		int32 Step = 0;
		double NextAt = 0.0;
		double Started = FPlatformTime::Seconds();
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FResultsPaintTest, "ChaosImpact.Look.ResultsPaint", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FResultsPaintTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FResultsPaintCommand(this));
	return true;
}
#endif
