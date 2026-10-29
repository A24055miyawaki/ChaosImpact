#if WITH_DEV_AUTOMATION_TESTS
#include "ChaosImpactGameMode.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactSettings.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "InputKeyEventArgs.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	/** One listen-server process, with a synthetic second room member (not a network connection).
	 * Exercises real configured-key input, short/released/menu holds, keyboard/gamepad completion and the results return.
	 * Run on Lvl_ThirdPerson?listen?CITraining=1?CITargets=0, without the dev auto-ready/start switches. */
	class FRecruitmentCommand : public IAutomationLatentCommand
	{
	public:
		explicit FRecruitmentCommand(FAutomationTestBase* InTest) : Test(InTest) {}
		bool Update() override
		{
			UWorld* World = nullptr;
			for (const auto& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld()) { World = Context.World(); break; }
			}
			auto* PC = World ? Cast<AChaosImpactPlayerController>(World->GetFirstPlayerController()) : nullptr;
			auto* Mode = World ? World->GetAuthGameMode<AChaosImpactGameMode>() : nullptr;
			auto* Room = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
			const double Now = FPlatformTime::Seconds();
			if (Now - Started > 40.)
			{
				Test->AddError(FString::Printf(TEXT("Recruitment flow timed out at step %d"), Step));
				Cleanup(PC);
				return true;
			}
			if (!PC || !Mode || !Room || Now < NextAt) { return false; }
			const auto Wait = [this, Now](const double Seconds) { ++Step; NextAt = Now + Seconds; return false; };
			const auto Key = [PC](const FKey& Value, const EInputEvent Event)
			{
				PC->InputKey(FInputKeyEventArgs(nullptr, FInputDeviceId::CreateFromInternalId(0), Value, Event, 1.f, false, 0u));
			};
			switch (Step)
			{
			case 0:
				if (Now - Started < 3.) { return false; }
				if (!PC->IsOnlineRoomHost() || !Room->bOnlineRoom)
				{
					Test->AddError(TEXT("Requires ?listen?CITraining=1."));
					return true;
				}
				Guest = World->SpawnActor<AChaosImpactPlayerState>();
				Guest->SetPlayerName(TEXT("TEST_GUEST"));
				Guest->JoinOrder = 1;
				Room->AddPlayerState(Guest.Get());
				PC->ResumeGameplay();
				ReadyKey = ChaosImpactSettings::GetActiveKey(ChaosImpactSettings::GetPlayerProfile(PC),
					EChaosImpactDevice::KeyboardMouse, EChaosImpactAction::LobbyReady, 0);
				Test->TestTrue(TEXT("Host can close with another member"), PC->CanCloseRecruitment());
				Key(ReadyKey, IE_Pressed);
				return Wait(.55);
			case 1:
				if (!bHoldShotRequested)
				{
					bHoldShotRequested = true;
					FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("RecruitmentQA/Hold.png")), true, false);
					NextAt = Now + .15;
					return false;
				}
				Test->TestFalse(TEXT("A short hold does not close recruitment"), Room->bRecruitmentClosed);
				Test->TestTrue(TEXT("The hold gauge advances"), PC->GetRecruitmentHoldProgress() > .05f);
				Key(ReadyKey, IE_Released);
				return Wait(.2);
			case 2:
				Test->TestEqual(TEXT("Releasing cancels hold"), PC->GetRecruitmentHoldProgress(), 0.f);
				PC->ShowMenuScreen(EChaosImpactScreen::Pause);
				Key(ReadyKey, IE_Pressed);
				return Wait(1.5);
			case 3:
				Test->TestFalse(TEXT("Holding in pause cannot close recruitment"), Room->bRecruitmentClosed);
				Key(ReadyKey, IE_Released);
				PC->ResumeGameplay();
				return Wait(.2);
			case 4:
				Key(ReadyKey, IE_Pressed);
				return Wait(1.6);
			case 5:
				Test->TestTrue(TEXT("Full keyboard hold closes recruitment"), Room->bRecruitmentClosed);
				Test->TestEqual(TEXT("Full hold opens rule selection"), PC->GetCurrentScreen(), EChaosImpactScreen::MatchRules);
				Key(ReadyKey, IE_Released);
				PC->ResumeGameplay();
				// Exercise the real results-to-lobby transition, including stale rules, deadline and ready flags.
				Room->bVersusMatch = true;
				Room->Phase = EChaosImpactOnlinePhase::Results;
				Room->PhaseStartedAt = Room->GetServerWorldTimeSeconds() - ChaosImpactMatch::ResultsRevealSeconds - 1.;
				Room->bRulesDecided = true;
				Room->ReadyDeadline = Room->GetServerWorldTimeSeconds() + .25;
				Room->Rules.bDuel = true;
				Guest->bReadyForMatch = true;
				Guest->bViewingResults = true;
				PC->GetPlayerState<AChaosImpactPlayerState>()->bReadyForMatch = true;
				PC->GetPlayerState<AChaosImpactPlayerState>()->bViewingResults = true;
				PC->ReturnToLobbyFromResults();
				return Wait(1.);
			case 6:
				Test->TestEqual(TEXT("Back in the lobby"), Room->Phase, EChaosImpactOnlinePhase::Lobby);
				Test->TestFalse(TEXT("Recruitment reopens"), Room->bRecruitmentClosed);
				Test->TestFalse(TEXT("Old confirmation removed"), Room->bRulesDecided);
				Test->TestEqual(TEXT("No automatic-start deadline"), Room->ReadyDeadline, 0.);
				Test->TestTrue(TEXT("Last rule choice retained"), Room->Rules.bDuel);
				Test->TestFalse(TEXT("Readiness cleared"), Guest->bReadyForMatch);
				Test->TestTrue(TEXT("Other player may keep viewing results"), Guest->bViewingResults);
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("RecruitmentQA/Reopened.png")), true, false);
				return Wait(1.);
			case 7:
				Test->TestEqual(TEXT("Remains recruiting beyond the previous deadline"), Room->Phase, EChaosImpactOnlinePhase::Lobby);
				PadKey = ChaosImpactSettings::GetActiveKey(ChaosImpactSettings::GetPlayerProfile(PC),
					EChaosImpactDevice::Xbox, EChaosImpactAction::LobbyReady, 0);
				Key(PadKey, IE_Pressed);
				return Wait(1.6);
			case 8:
				Test->TestTrue(TEXT("Full gamepad hold closes recruitment"), Room->bRecruitmentClosed);
				Test->TestEqual(TEXT("Gamepad opens rule selection"), PC->GetCurrentScreen(), EChaosImpactScreen::MatchRules);
				Cleanup(PC);
				return true;
			default: return true;
			}
		}
	private:
		void Cleanup(AChaosImpactPlayerController* PC)
		{
			if (PC)
			{
				for (const FKey& Key : {ReadyKey, PadKey})
				{
					if (Key.IsValid()) { PC->InputKey(FInputKeyEventArgs(nullptr, FInputDeviceId::CreateFromInternalId(0), Key, IE_Released, 0.f, false, 0u)); }
				}
			}
			if (Guest.IsValid()) { Guest->Destroy(); }
		}
		FAutomationTestBase* Test;
		TWeakObjectPtr<AChaosImpactPlayerState> Guest;
		FKey ReadyKey, PadKey;
		bool bHoldShotRequested = false;
		int32 Step = 0;
		double Started = FPlatformTime::Seconds(), NextAt = 0.;
	};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactRecruitmentTest, "ChaosImpact.Online.RecruitmentHoldAndReturn",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FChaosImpactRecruitmentTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FRecruitmentCommand(this));
	return true;
}
#endif
