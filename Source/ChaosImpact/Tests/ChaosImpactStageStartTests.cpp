#if WITH_DEV_AUTOMATION_TESTS
// A local VS match started on each stage (-CIStageCheck=<index>, else stage 2): the view comes down to the player's own
// top-down camera and they can play (screenshots in Saved/StageStartQA).
#include "Misc/AutomationTest.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactGameMode.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactMatchTypes.h"
#include "ChaosImpactPlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/SpringArmComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	class FStageStartCommand : public IAutomationLatentCommand
	{
	public:
		explicit FStageStartCommand(FAutomationTestBase* InTest) : Test(InTest) {}
		bool Update() override
		{
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld()) { World = Context.World(); }
			}
			AChaosImpactGameMode* GM = World ? World->GetAuthGameMode<AChaosImpactGameMode>() : nullptr;
			const AChaosImpactGameState* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
			const double Now = FPlatformTime::Seconds();
			if (Now - Started > 150.0) { Test->AddError(FString::Printf(TEXT("Stage start stalled at %d"), Step)); return true; }
			if (!GM || !Match || Now < WaitUntil) { return false; }
			AChaosImpactPlayerController* PC = Cast<AChaosImpactPlayerController>(World->GetFirstPlayerController());
			AChaosImpactCharacter* Own = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			const auto Shot = [this](const TCHAR* Name)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("StageStartQA"),
					FString::Printf(TEXT("Stage%d-%s.png"), StageIndex + 1, Name)), true, false);
			};
			const auto Report = [&](const TCHAR* When)
			{
				FVector View; FRotator Rotation;
				PC->GetPlayerViewPoint(View, Rotation);
				UE_LOG(LogTemp, Display, TEXT("STAGESTART stage %d %s: phase %d locked %d view %s rot %s, player %s, boom rot %s arm %.0f, target %s"),
					StageIndex + 1, When, static_cast<int32>(Match->Phase), Own && Own->IsMatchInputLocked() ? 1 : 0, *View.ToCompactString(),
					*Rotation.ToCompactString(), Own ? *Own->GetActorLocation().ToCompactString() : TEXT("-"),
					Own ? *Own->GetCameraBoom()->GetComponentRotation().ToCompactString() : TEXT("-"),
					Own ? Own->GetCameraBoom()->TargetArmLength : 0.0f, *GetNameSafe(PC->GetViewTarget()));
			};
			switch (Step)
			{
			case 0:
			{
				StageIndex = 1;
				FParse::Value(FCommandLine::Get(), TEXT("CIStageCheck="), StageIndex);
				FChaosImpactMatchRules Rules;
				Rules.StageIndex = StageIndex;
				Rules.CPUCount = 1;
				Rules.CPULevel = 1;
				GM->ConfigureVersusMatch(Rules);
				GM->ConfirmTeamsAndStart();
				Step = 1;
				return false;
			}
			case 1:
				if (!PC || !Own)
				{
					return false;
				}
				Report(TEXT("opening"));
				Shot(TEXT("1-Opening"));
				WaitUntil = Now + 3.0;
				Step = 2;
				return false;
			case 2:
				Report(TEXT("3s"));
				Shot(TEXT("2-3s"));
				WaitUntil = Now + 17.0;
				Step = 3;
				return false;
			case 3:
			{
				Report(TEXT("20s"));
				Shot(TEXT("3-20s"));
				Test->TestEqual(TEXT("The match is on"), static_cast<int32>(Match->Phase), static_cast<int32>(EChaosImpactOnlinePhase::Match));
				Test->TestFalse(TEXT("The player can play"), Own->IsMatchInputLocked());
				FVector View; FRotator Rotation;
				PC->GetPlayerViewPoint(View, Rotation);
				Test->TestTrue(FString::Printf(TEXT("The view looks down on the player (pitch %.0f)"), Rotation.Pitch), Rotation.Pitch < -30.0f);
				Test->TestTrue(FString::Printf(TEXT("The view is near the player (%.0f cm)"), FVector::Dist2D(View, Own->GetActorLocation())),
					FVector::Dist2D(View, Own->GetActorLocation()) < 2500.0f);
				Test->TestTrue(TEXT("The view is the player's own"), PC->GetViewTarget() == Own);
				GM->EndMatchForTests();
				return true;
			}
			default:
				return true;
			}
		}

	private:
		FAutomationTestBase* Test;
		double Started = FPlatformTime::Seconds();
		double WaitUntil = 0.0;
		int32 Step = 0;
		int32 StageIndex = 1;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStageStartTest, "ChaosImpact.Versus.StageStart", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FStageStartTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FStageStartCommand(this));
	return true;
}
#endif
