#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactCharacter.h"
#include "ChaosImpactCharacterRoster.h"
#include "ChaosImpactPlayerController.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	/**
	 * Mug Carp's coffee up close: from three quarters above and from straight over the cup, so the liquid's colour,
	 * ripples and gloss can be checked (Saved/CapCoffeeQA). Training arena (?CITraining=1?CICPUCount=0?CITargets=0).
	 */
	class FCapCoffeeCommand : public IAutomationLatentCommand
	{
	public:
		explicit FCapCoffeeCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - StartedAt > 90.0) { Test->AddError(TEXT("Cap coffee test timeout")); return true; }
			if (Now < NextAt) { return false; }
			AChaosImpactPlayerController* PC = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld()) { PC = PC ? PC : Cast<AChaosImpactPlayerController>(Context.World()->GetFirstPlayerController()); }
			}
			UWorld* World = PC ? PC->GetWorld() : nullptr;
			AChaosImpactCharacter* Player = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			if (!World || !Player || !PC->IsGameplayActive() || !Player->GetCharacterMovement()->IsMovingOnGround()) { return false; }
			const auto Look = [&](const FVector& From, const FVector& At)
			{
				if (!Camera.IsValid()) { Camera = World->SpawnActor<ACameraActor>(); Camera->GetCameraComponent()->SetFieldOfView(32.0f); }
				Camera->SetActorLocationAndRotation(From, (At - From).Rotation());
				PC->SetViewTarget(Camera.Get());
			};
			const FVector Middle = Player->GetActorLocation() + FVector(0, 0, 20);
			const FVector Forward = Player->GetActorForwardVector().GetSafeNormal2D();
			switch (Stage)
			{
			case 0:
				Player->LookOverride = ChaosImpactRoster::CapIndex;
				Stage = 1; NextAt = Now + 1.0; return false;
			case 1:
				Look(Middle + Forward * 150.0f + FVector(0, 0, 150.0f), Middle);
				Stage = 2; NextAt = Now + 1.0; return false;
			case 2:
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("CapCoffeeQA"), TEXT("coffee-three-quarter.png")), true, false);
				Stage = 3; NextAt = Now + 0.4; return false;
			case 3:
				Look(Middle + Forward * 30.0f + FVector(0, 0, 210.0f), Middle);
				Stage = 4; NextAt = Now + 1.0; return false;
			case 4:
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("CapCoffeeQA"), TEXT("coffee-top.png")), true, false);
				Stage = 5; NextAt = Now + 0.6; return false;
			case 5:
				// And how it reads from the play camera.
				PC->SetViewTarget(Player);
				Stage = 6; NextAt = Now + 1.0; return false;
			case 6:
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("CapCoffeeQA"), TEXT("coffee-play.png")), true, false);
				Stage = 7; NextAt = Now + 0.6; return false;
			default:
				return true;
			}
		}

	private:
		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Stage = 0;
		TWeakObjectPtr<ACameraActor> Camera;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactCapCoffeeTest, "ChaosImpact.Characters.Cap.CoffeeLook",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactCapCoffeeTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FCapCoffeeCommand(this));
	return true;
}

#endif
