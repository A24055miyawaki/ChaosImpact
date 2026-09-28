#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactCharacter.h"
#include "ChaosImpactGameState.h"
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
	AChaosImpactPlayerController* FindStageShotController()
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
	 * Not a check but a tool: the picture for the stage's card on the stage select screen. Run on a local VS level
	 * (?CITraining=1?CITargets=0?CIVersus=1?CIMatch=1?CIMatchCPU=0?CIStage=N); the stage is shot from above at an
	 * angle with no HUD and no characters, to Saved/StageShots/Stage<N+1>.png. Copy it to Content/UI/StageSelect.
	 */
	class FStageShotCommand : public IAutomationLatentCommand
	{
	public:
		explicit FStageShotCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now < NextAt)
			{
				return false;
			}
			AChaosImpactPlayerController* PC = FindStageShotController();
			UWorld* World = PC ? PC->GetWorld() : nullptr;
			const AChaosImpactGameState* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
			if (!Match || !Match->bVersusMatch || Match->Phase != EChaosImpactOnlinePhase::Match)
			{
				if (Now - StartedAt < 150.0)
				{
					return false;
				}
				Test->AddError(TEXT("The VS match never started."));
				return true;
			}
			switch (Stage)
			{
			case 0:
			{
				for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
				{
					It->SetActorHiddenInGame(true);
				}
				// The same framing for every stage: from one corner, high up, the whole arena in view.
				const FVector Center = Match->StageCenter;
				const float Reach = Match->StageHalfExtent;
				const FVector Eye = Center + FVector(-Reach * 1.18f, -Reach * 1.18f, Reach * 0.92f);
				Camera = World->SpawnActor<ACameraActor>(Eye, (Center - Eye).Rotation());
				Camera->GetCameraComponent()->SetFieldOfView(70.0f);
				Camera->GetCameraComponent()->SetConstraintAspectRatio(false);
				PC->SetViewTarget(Camera.Get());
				++Stage;
				NextAt = Now + 2.0;
				return false;
			}
			case 1:
			{
				// A screenshot is taken at the end of the frame, so the view had to change a step earlier.
				const FString Name = FString::Printf(TEXT("Stage%d.png"), Match->Rules.StageIndex + 1);
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("StageShots"), Name),
					false, false);
				++Stage;
				NextAt = Now + 1.0;
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
		int32 Stage = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactStageShotTest, "ChaosImpact.Tools.StageShot",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactStageShotTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FStageShotCommand(this));
	return true;
}

#endif
