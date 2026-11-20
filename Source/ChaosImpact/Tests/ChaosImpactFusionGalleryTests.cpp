#if WITH_DEV_AUTOMATION_TESTS
// 合体ボール: every pair's look side by side (2026-11-17). All 90 pairs, ten to a page in two rows in front of the player,
// lying as balls on the floor; then a few held in the hand. Saved/FusionQA/Gallery_N.png (the pairs of each page in the log,
// "FUSIONGALLERY"). Run on a training stage without CPUs: Lvl_VS_Stage1?CITraining=1?CITargets=0?CICPUCount=0
#include "Misc/AutomationTest.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactFusion.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HighResScreenshot.h"
#include "Misc/Paths.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/PlayerCameraManager.h"

namespace
{
	class FFusionGalleryCommand : public IAutomationLatentCommand
	{
	public:
		explicit FFusionGalleryCommand(FAutomationTestBase* InTest) : Test(InTest)
		{
			for (int32 A = 1; A < ChaosImpactBallTypes::Count; ++A)
			{
				for (int32 B = A; B < ChaosImpactBallTypes::Count; ++B)
				{
					const EChaosImpactBallType TypeA = ChaosImpactBallTypes::FromIndex(A);
					const EChaosImpactBallType TypeB = ChaosImpactBallTypes::FromIndex(B);
					if (ChaosImpactFusion::CanFuseTypes(TypeA, TypeB))
					{
						Pairs.Add({ChaosImpactFusion::PickPrimary(TypeA, TypeB), ChaosImpactFusion::PickSecondary(TypeA, TypeB)});
					}
				}
			}
		}

		bool Update() override
		{
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld()) { World = Context.World(); }
			}
			const double Now = FPlatformTime::Seconds();
			if (Now - Started > 240.0) { Test->AddError(FString::Printf(TEXT("Gallery stalled at page %d"), Page)); return true; }
			if (!World || Now < WaitUntil) { return false; }
			APlayerController* PC = World->GetFirstPlayerController();
			AChaosImpactCharacter* Own = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			if (!Own) { return false; }
			if (Page < 0)
			{
				Test->TestEqual(TEXT("Every pair of the 17 specials joins, and each with itself (2026-11-19)"), Pairs.Num(), 136 + 17);
				// A closer camera, so each ball's look can be seen.
				if (USpringArmComponent* Arm = Own->FindComponentByClass<USpringArmComponent>())
				{
					Arm->TargetArmLength *= 0.55f;
				}
				Page = 0;
				WaitUntil = Now + 3.0;
				return false;
			}
			if (bShotPending)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("FusionQA"),
					FString::Printf(TEXT("Gallery_%d.png"), Page)), true, false);
				bShotPending = false;
				++Page;
				WaitUntil = Now + 0.5;
				return false;
			}
			for (const TWeakObjectPtr<AChaosImpactBall>& Ball : Shown)
			{
				if (Ball.IsValid()) { Ball->Destroy(); }
			}
			Shown.Reset();
			constexpr int32 PerPage = 10;
			if (Page * PerPage >= Pairs.Num())
			{
				return true;
			}
			// Two rows of five across the view, in front of the player.
			// In the air in front of the camera, two rows of five across the view (each ball hovers where it is put).
			const FVector Eye = PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraLocation() : Own->GetActorLocation();
			const FRotator View = PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraRotation() : Own->GetActorRotation();
			const FVector Forward = View.Vector();
			const FVector Right = FRotationMatrix(View).GetUnitAxis(EAxis::Y);
			const FVector Up = FRotationMatrix(View).GetUnitAxis(EAxis::Z);
			const FVector Base = Eye + Forward * FVector::Dist(Eye, Own->GetActorLocation()) * 0.42f;
			FString Names;
			for (int32 Index = 0; Index < PerPage && Page * PerPage + Index < Pairs.Num(); ++Index)
			{
				const TPair<EChaosImpactBallType, EChaosImpactBallType>& Pair = Pairs[Page * PerPage + Index];
				const int32 Row = Index / 5;
				const int32 Column = Index % 5;
				const FVector At = Base + Right * (Column - 2) * 110.0f + Up * (Row == 0 ? 62.0f : -62.0f);
				const FTransform Where(FRotator::ZeroRotator, At);
				AChaosImpactBall* Ball = World->SpawnActorDeferred<AChaosImpactBall>(AChaosImpactBall::StaticClass(), Where, nullptr, nullptr,
					ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				if (!Ball) { continue; }
				Ball->SetBallType(Pair.Key);
				Ball->SetFusedType(Pair.Value);
				Ball->FinishSpawning(Where);
				Ball->MakePickup();
				Shown.Add(Ball);
				Names += FString::Printf(TEXT(" [%d]%s"), Index, *ChaosImpactFusion::GetFusionLabel(Pair.Key, Pair.Value));
			}
			UE_LOG(LogTemp, Display, TEXT("FUSIONGALLERY page %d:%s"), Page, *Names);
			bShotPending = true;
			WaitUntil = Now + 1.5;
			return false;
		}

	private:
		FAutomationTestBase* Test;
		double Started = FPlatformTime::Seconds();
		double WaitUntil = 0.0;
		int32 Page = -1;
		bool bShotPending = false;
		TArray<TPair<EChaosImpactBallType, EChaosImpactBallType>> Pairs;
		TArray<TWeakObjectPtr<AChaosImpactBall>> Shown;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactFusionGalleryTest, "ChaosImpact.Training.FusionGallery",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FChaosImpactFusionGalleryTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FFusionGalleryCommand(this));
	return true;
}
#endif
