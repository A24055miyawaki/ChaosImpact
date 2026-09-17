#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactGameState.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactScreen.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	AChaosImpactPlayerController* FindVersusLocalMenuController()
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

	/** VS LOCAL through the real menu steps: two players, with the given number of CPUs. */
	class FVersusLocalMenuCommand : public IAutomationLatentCommand
	{
	public:
		FVersusLocalMenuCommand(FAutomationTestBase* InTest, const int32 InCPUCount) : Test(InTest), CPUCount(InCPUCount) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now < NextAt)
			{
				return false;
			}
			AChaosImpactPlayerController* PC = FindVersusLocalMenuController();
			UWorld* World = PC ? PC->GetWorld() : nullptr;
			if (Stage == 0)
			{
				if (!World || Now - StartedAt < 2.0)
				{
					if (Now - StartedAt < 60.0)
					{
						return false;
					}
					Test->AddError(TEXT("No player controller."));
					return true;
				}
				// What a training menu overlay leaves behind: split screen held off by the viewport.
				if (UGameViewportClient* Viewport = World->GetGameViewport())
				{
					Viewport->SetForceDisableSplitscreen(true);
				}
				PC->ShowMenuScreen(EChaosImpactScreen::ModeSelect);
				PC->BeginVersusLocal();
				PC->PrepareTrainingControllerAssignment(2);
				PC->RegisterKeyboardMouseJoin();
				PC->RegisterControllerJoin(101, 1);
				PC->ConfirmControllerAssignments();
				PC->ConfirmCharacterSelection();
				Test->TestEqual(TEXT("The rules screen counts both local players"), PC->GetMatchHumanCount(), 2);
				for (int32 Step = 0; Step < 10 && PC->GetPendingMatchRules().CPUCount != CPUCount; ++Step)
				{
					PC->AdjustMatchRule(2, PC->GetPendingMatchRules().CPUCount < CPUCount ? 1 : -1);
				}
				Test->TestEqual(TEXT("The chosen number of CPUs"), PC->GetPendingMatchRules().CPUCount, CPUCount);
				PC->ConfirmMatchRules();
				Stage = 1;
				NextAt = Now + 2.0;
				return false;
			}

			const AChaosImpactGameState* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
			const bool bPlaying = World && World->URL.HasOption(TEXT("CIMatch=1")) && Match && Match->bVersusMatch
				&& Match->Phase == EChaosImpactOnlinePhase::Match && !Match->IsMatchInputLocked();
			if (!bPlaying)
			{
				if (Now - StartedAt < 150.0)
				{
					return false;
				}
				Test->AddError(TEXT("The local VS match never started."));
				return true;
			}
			if (Stage == 1)
			{
				// A moment into the match, past the opening's own screen changes.
				Stage = 2;
				NextAt = Now + 2.0;
				return false;
			}
			const UGameInstance* GameInstance = World->GetGameInstance();
			const UGameViewportClient* Viewport = World->GetGameViewport();
			int32 PlayersWithPawns = 0;
			if (GameInstance)
			{
				for (const ULocalPlayer* LocalPlayer : GameInstance->GetLocalPlayers())
				{
					const APlayerController* Controller = LocalPlayer ? LocalPlayer->GetPlayerController(World) : nullptr;
					PlayersWithPawns += Controller && Controller->GetPawn() ? 1 : 0;
				}
			}
			UE_LOG(LogTemp, Display, TEXT("VSLOCALTEST cpu=%d players=%d pawns=%d competitors=%d rulesCpu=%d splitOff=%d splitType=%d"),
				CPUCount, GameInstance ? GameInstance->GetLocalPlayers().Num() : -1, PlayersWithPawns,
				Match->GetCompetitors(true).Num(), Match->Rules.CPUCount,
				Viewport && Viewport->IsSplitscreenForceDisabled() ? 1 : 0,
				Viewport ? static_cast<int32>(Viewport->GetCurrentSplitscreenConfiguration()) : -1);
			Test->TestEqual(TEXT("Both local players are in the match"), GameInstance ? GameInstance->GetLocalPlayers().Num() : 0, 2);
			Test->TestEqual(TEXT("Both local players have a character"), PlayersWithPawns, 2);
			Test->TestFalse(TEXT("The screen is not held full-screen"), Viewport && Viewport->IsSplitscreenForceDisabled());
			Test->TestTrue(TEXT("The screen is split for the two players"),
				Viewport && Viewport->GetCurrentSplitscreenConfiguration() != ESplitScreenType::None);
			Test->TestEqual(TEXT("The chosen CPUs joined"), Match->Rules.CPUCount, CPUCount);
			Test->TestEqual(TEXT("Players and CPUs compete"), Match->GetCompetitors(true).Num(), 2 + CPUCount);
			FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("VersusQA"),
				FString::Printf(TEXT("LocalMenu-2P-CPU%d.png"), CPUCount)), true, false);
			return true;
		}

	private:
		FAutomationTestBase* Test;
		int32 CPUCount = 0;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Stage = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactVersusLocalMenuTest, "ChaosImpact.Versus.LocalMenuTwoPlayers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::EngineFilter)

bool FChaosImpactVersusLocalMenuTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FVersusLocalMenuCommand(this, 0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactVersusLocalMenuCPUTest, "ChaosImpact.Versus.LocalMenuTwoPlayersCPU",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::EngineFilter)

bool FChaosImpactVersusLocalMenuCPUTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FVersusLocalMenuCommand(this, 1));
	return true;
}

#endif
