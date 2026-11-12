#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactCharacter.h"
#include "ChaosImpactCyberStage.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactScreen.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	/**
	 * ステージ4's moving walkways: a point on one is pushed along it (the four all turning the same way round), a point beside
	 * it is not, and a character left standing on one is carried along it about as far as its speed says in a second.
	 */
	class FWalkwayCommand : public IAutomationLatentCommand
	{
	public:
		explicit FWalkwayCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - StartedAt > 180.0)
			{
				Test->AddError(FString::Printf(TEXT("Stuck at step %d"), Step));
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
			if (!PC || Now < NextAt)
			{
				return false;
			}
			AChaosImpactCyberStage* Stage = nullptr;
			for (TActorIterator<AChaosImpactCyberStage> It(World); It; ++It)
			{
				Stage = *It;
			}
			switch (Step)
			{
			case 0:
				if (Now - StartedAt < 4.0) { return false; }
				PC->ShowMenuScreen(EChaosImpactScreen::ModeSelect);
				PC->BeginLocalSpectate();
				PC->ChooseStage(3);
				Step = 1;
				NextAt = Now + 1.0;
				return false;
			case 1:
			{
				if (!World->GetMapName().Contains(TEXT("Lvl_VS_Stage4")) || !Stage || !Match || Match->Phase != EChaosImpactOnlinePhase::Match)
				{
					return false;
				}
				const FTransform Frame = Stage->GetActorTransform();
				for (int32 Quarter = 0; Quarter < 4; ++Quarter)
				{
					const FRotator Turn(0.0f, 90.0f * Quarter, 0.0f);
					const FVector On = Frame.TransformPosition(Turn.RotateVector(FVector(AChaosImpactCyberStage::WalkwayX, 300.0f, 121.0f)));
					const FVector Beside = Frame.TransformPosition(Turn.RotateVector(FVector(AChaosImpactCyberStage::WalkwayX - 400.0f, 300.0f, 121.0f)));
					FVector Push;
					Test->TestTrue(FString::Printf(TEXT("Walkway %d pushes"), Quarter), Stage->GetWalkwayPush(On, Push));
					const FVector Expected = Frame.TransformVectorNoScale(Turn.RotateVector(FVector(0.0f, AChaosImpactCyberStage::WalkwaySpeed, 0.0f)));
					Test->TestTrue(FString::Printf(TEXT("Walkway %d goes the same way round"), Quarter), Push.Equals(Expected, 1.0f));
					Test->TestFalse(FString::Printf(TEXT("Beside walkway %d nothing"), Quarter), Stage->GetWalkwayPush(Beside, Push));
				}
				// A CPU left standing (its brain taken away) on the first walkway.
				for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
				{
					if (!It->IsPlayerControlled())
					{
						Rider = *It;
						break;
					}
				}
				if (!Rider.IsValid())
				{
					Test->AddError(TEXT("No CPU to put on the walkway"));
					return true;
				}
				if (AController* Brain = Rider->GetController())
				{
					Brain->UnPossess();
				}
				const float Half = Rider->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
				Rider->SetActorLocation(Frame.TransformPosition(FVector(AChaosImpactCyberStage::WalkwayX, -500.0f, 121.0f + Half + 2.0f)), false, nullptr,
					ETeleportType::TeleportPhysics);
				Step = 2;
				NextAt = Now + 0.6;
				return false;
			}
			case 2:
				if (!Rider.IsValid()) { Test->AddError(TEXT("The rider went")); return true; }
				From = Rider->GetActorLocation();
				Step = 3;
				NextAt = Now + 1.0;
				return false;
			default:
			{
				if (!Rider.IsValid()) { Test->AddError(TEXT("The rider went")); return true; }
				const FVector Moved = Rider->GetActorLocation() - From;
				const FVector Along = Stage->GetActorTransform().TransformVectorNoScale(FVector(0.0f, 1.0f, 0.0f));
				const float Carried = static_cast<float>(FVector::DotProduct(Moved, Along));
				UE_LOG(LogTemp, Display, TEXT("WALKWAY carried %.0f cm in a second (sideways %.0f)"), Carried, (Moved - Along * Carried).Size2D());
				Test->TestTrue(FString::Printf(TEXT("Carried along about its speed (%.0f)"), Carried),
					Carried > AChaosImpactCyberStage::WalkwaySpeed * 0.7f && Carried < AChaosImpactCyberStage::WalkwaySpeed * 1.35f);
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("GimmickQA"), TEXT("walkway.png")), true, false);
				return true;
			}
			}
		}

	private:
		FAutomationTestBase* Test = nullptr;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Step = 0;
		TWeakObjectPtr<AChaosImpactCharacter> Rider;
		FVector From = FVector::ZeroVector;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactWalkwayTest, "ChaosImpact.Versus.Walkway",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactWalkwayTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FWalkwayCommand(this));
	return true;
}

#endif
