#if WITH_DEV_AUTOMATION_TESTS
#include "ChaosImpactCharacter.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactGameViewportClient.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactPlayerMarkerLayout.h"
#include "ChaosImpactResults.h"
#include "ChaosImpactSettings.h"
#include "ChaosImpactSpectatorPawn.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/DamageEvents.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	// Real host + participant processes, guest launched with -CIAutoPlayers=2 from the title.
	// Host uses -CIAutoVersus=0:0:25; no fake connections or pre-created training players.
	class FOnlineSplitCommand : public IAutomationLatentCommand
	{
	public:
		explicit FOnlineSplitCommand(FAutomationTestBase* InTest) : Test(InTest)
		{ FParse::Value(FCommandLine::Get(), TEXT("CISplitWatch="), WatchIndex); }
		bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - Started > 160.) { Test->AddError(FString::Printf(TEXT("Split-screen online join timed out at step %d"), Step)); return true; }
			UWorld* World = nullptr;
			for (const auto& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld()) { World = Context.World(); break; }
			}
			if (!World || World->GetNetMode() == NM_Standalone) { return false; }
			auto* Match = World->GetGameState<AChaosImpactGameState>();
			UGameInstance* GI = World->GetGameInstance();
			if (!Match || !GI || (Step < 2 && Match->CountHumanMembers() + Match->CountSpectators() < 3)) { return false; }
			if (Step == 0)
			{
				if (RosterAt == 0.) { RosterAt = Now; }
				if (Now - RosterAt < 1.) { return false; }
				Test->TestEqual(TEXT("Host and both participants appear in the roster"), Match->CountHumanMembers() + Match->CountSpectators(), 3);
				int32 Seconds = 0;
				for (APlayerState* State : Match->PlayerArray)
				{
					const auto* Member = Cast<AChaosImpactPlayerState>(State);
					Seconds += Member && Member->bSecondOfMachine ? 1 : 0;
				}
				Test->TestEqual(TEXT("One real secondary player is registered"), Seconds, 1);
				if (World->GetNetMode() == NM_Client)
				{
					Test->TestEqual(TEXT("Participant retains two local players"), GI->GetNumLocalPlayers(), 2);
					Test->TestNotNull(TEXT("Configured game viewport handles duel layouts"), Cast<UChaosImpactGameViewportClient>(World->GetGameViewport()));
					if (GI->GetNumLocalPlayers() != 2) { return true; }
					for (const ULocalPlayer* Local : GI->GetLocalPlayers())
					{
						const auto* PC = Cast<AChaosImpactPlayerController>(Local->GetPlayerController(World));
						if (!PC || !PC->GetPawn()) { return false; }
						Test->TestTrue(TEXT("Each participant possesses its own character"), PC->GetPawn()->IsLocallyControlled());
						Test->TestTrue(TEXT("Both views receive separate vertical halves"), FMath::IsNearlyEqual(Local->Size.X, .5f));
						Test->TestTrue(TEXT("Both views retain full height"), FMath::IsNearlyEqual(Local->Size.Y, 1.f));
					}
					Test->TestFalse(TEXT("Split-screen is enabled in the window"), World->GetGameViewport()->IsSplitscreenForceDisabled());
					FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("OnlineSplitQA/GuestLobby.png")), true, false);
					if (WatchIndex >= 0)
					{
						auto* PC = Cast<AChaosImpactPlayerController>(GI->GetLocalPlayers()[WatchIndex]->GetPlayerController(World));
						const auto Device = PC->IsUsingGamepad() ? EChaosImpactDevice::Xbox : EChaosImpactDevice::KeyboardMouse;
						const FKey Key = ChaosImpactSettings::GetActiveKey(ChaosImpactSettings::GetPlayerProfile(PC), Device, EChaosImpactAction::LobbySpectate, 0);
						PC->HandleBoundKeyPressed(Key);
					}
				}
				++Step;
			}
			if (Step == 1)
			{
				if (!Match->bVersusMatch || Match->Phase != EChaosImpactOnlinePhase::Match) { return false; }
				Test->TestEqual(TEXT("Fighters survive transition into VS"), Match->CountHumanMembers(), WatchIndex >= 0 ? 2 : 3);
				Test->TestEqual(TEXT("Only chosen member watches"), Match->CountSpectators(), WatchIndex >= 0 ? 1 : 0);
				Test->TestEqual(TEXT("Two fighters and an observer can play duel"), Match->IsDuel(), WatchIndex >= 0);
				if (World->GetNetMode() == NM_Client)
				{
					if (WatchIndex >= 0)
					{
						const ULocalPlayer* Fighter = UChaosImpactGameViewportClient::GetSingleDuelPlayer(World);
						Test->TestTrue(TEXT("The actual fighter owns the full view, including P2"), Fighter == GI->GetLocalPlayers()[1 - WatchIndex]);
						Test->TestTrue(TEXT("Fighter fills the window"), Fighter && Fighter->Size.Equals(FVector2D(1., 1.)));
						Test->TestTrue(TEXT("Spectator view is suppressed only for the duel"), GI->GetLocalPlayers()[WatchIndex]->Size.IsNearlyZero());
						const auto* Watcher = GI->GetLocalPlayers()[WatchIndex]->GetPlayerController(World);
						Test->TestTrue(TEXT("Observer retains a separate spectator connection"), Watcher && Cast<AChaosImpactSpectatorPawn>(Watcher->GetPawn()));
					}
					for (const ULocalPlayer* Local : GI->GetLocalPlayers())
					{
						const auto* PC = Cast<AChaosImpactPlayerController>(Local->GetPlayerController(World));
						Test->TestTrue(TEXT("Both participants can play in the match"), PC && PC->GetPawn() && PC->IsGameplayActive());
						Test->TestFalse(TEXT("Closed recruitment disables spectator switching"), PC && PC->CanToggleSpectating());
					}
					FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("OnlineSplitQA/GuestMatch.png")), true, false);
				}
				NextDamageAt = Now + 2.; ++Step;
			}
			// Finish actual duel rounds; ordinary VS uses -CIMatchSeconds=4.
			if (World->GetNetMode() == NM_ListenServer && Match->IsDuelLive() && Now >= NextDamageAt)
			{
				auto* Victim = Match->Duel.Sides[1] ? Cast<AChaosImpactCharacter>(Match->Duel.Sides[1]->GetPawn()) : nullptr;
				auto* Attacker = Match->Duel.Sides[0] ? Match->Duel.Sides[0]->GetPawn() : nullptr;
				if (Victim && Attacker) { FDamageEvent Hit; Victim->TakeDamage(999.f, Hit, Attacker->GetController(), Attacker); }
				NextDamageAt = Now + .5;
			}
			auto* Primary = Cast<AChaosImpactPlayerController>(GI->GetFirstLocalPlayerController());
			const auto* Results = Primary ? Primary->GetResultsView() : nullptr;
			if (Results && Results->IsActive() && Results->GetShowSeconds() >= 0.f)
			{
				if (ResultsAt == 0.) { ResultsAt = Now - Results->GetShowSeconds(); }
				Test->TestTrue(TEXT("Results expose the twenty-second countdown"), Results->GetAutoReturnRemaining() >= 0.f && Results->GetAutoReturnRemaining() <= 20.f);
				if (!bResultShot && Results->GetShowSeconds() > 3.f)
				{
					bResultShot = true;
					FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("OnlineSplitQA/ResultsCountdown.png")), true, false);
				}
				return false;
			}
			if (ResultsAt == 0. || Match->Phase != EChaosImpactOnlinePhase::Lobby) { return false; }
			if (Step == 2)
			{
				Test->TestTrue(TEXT("Results return automatically after about twenty seconds"), Now - ResultsAt >= 19.5 && Now - ResultsAt < 23.);
				Test->TestFalse(TEXT("Recruitment reopens on return"), Match->bRecruitmentClosed);
				Test->TestTrue(TEXT("Each local member can switch again in the lobby"), Primary->CanToggleSpectating());
				if (World->GetNetMode() == NM_Client)
				{
					for (const ULocalPlayer* Local : GI->GetLocalPlayers())
					{
						Test->TestTrue(TEXT("Split views restore after results"), FMath::IsNearlyEqual(Local->Size.X, .5f));
					}
				}
				EndAt = Now + (World->GetNetMode() == NM_Client ? 1. : 4.); ++Step;
			}
			return Now >= EndAt;
		}
	private:
		FAutomationTestBase* Test;
		double Started = FPlatformTime::Seconds(), EndAt = 0., RosterAt = 0., ResultsAt = 0., NextDamageAt = 0.;
		int32 Step = 0, WatchIndex = -1;
		bool bResultShot = false;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOnlineSplitTest, "ChaosImpact.Online.SplitJoin",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FOnlineSplitTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FOnlineSplitCommand(this)); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelMarkerLayoutTest, "ChaosImpact.UI.DuelMarkerClearance",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FDuelMarkerLayoutTest::RunTest(const FString&)
{
	for (const FVector2D Size : {FVector2D(1600, 900), FVector2D(1280, 720), FVector2D(2560, 1080), FVector2D(800, 900), FVector2D(640, 360)})
	{
		const float S = FMath::Clamp(static_cast<float>(FMath::Min(Size.X / 1600., Size.Y / 900.)), .42f, 1.4f);
		for (const float K : {.75f * S, 1.8f * S})
		{
			for (const FVector2D Aim : {FVector2D(0, -1), FVector2D(.4, -1), FVector2D(-2, -1), FVector2D(1, 0), FVector2D(0, 1)})
			{
				const FVector2D Direction = Aim.GetSafeNormal();
				const auto Tip = ChaosImpactPlayerMarkerLayout::EdgeTip(Size, Direction, K, 155.f * S);
				TestTrue(TEXT("Marker stays below the duel HUD"), Tip.Y >= 155.f * S);
				TestTrue(TEXT("Marker stays inside its player view"), Tip.X >= 0. && Tip.X <= Size.X && Tip.Y <= Size.Y);
				TestTrue(TEXT("Marker still points toward the target"), FVector2D::DotProduct((Tip - Size * .5).GetSafeNormal(), Direction) > .999);
				if (Direction.Y >= 0.)
				{
					TestEqual(TEXT("Side/bottom markers retain their positions"), Tip, ChaosImpactPlayerMarkerLayout::EdgeTip(Size, Direction, K, 0.f));
				}
			}
		}
	}
	return true;
}

namespace
{
	class FDuelMarkerShotCommand : public IAutomationLatentCommand
	{
	public:
		explicit FDuelMarkerShotCommand(FAutomationTestBase* InTest) : Test(InTest) {}
		bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - Started > 70.) { Test->AddError(TEXT("Duel marker screenshot timed out")); return true; }
			UWorld* World = nullptr;
			for (const auto& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld()) { World = Context.World(); break; }
			}
			auto* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
			auto* PC = World ? World->GetFirstPlayerController() : nullptr;
			auto* Own = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			if (!Match || !Match->IsDuelLive() || !PC || !Own || !PC->PlayerCameraManager) { return false; }
			const int32 Side = Match->Duel.SideOf(PC->PlayerState);
			auto* Enemy = Side != INDEX_NONE && Match->Duel.Sides[1 - Side]
				? Cast<AChaosImpactCharacter>(Match->Duel.Sides[1 - Side]->GetPawn()) : nullptr;
			if (!Enemy) { return false; }
			if (Step == 0)
			{
				Enemy->SetTrainingMenuFrozen(true);
				const FVector Up = FRotationMatrix(PC->PlayerCameraManager->GetCameraRotation()).GetScaledAxis(EAxis::Z).GetSafeNormal2D();
				Enemy->SetActorLocation(Own->GetActorLocation() + Up * 2200.f, false, nullptr, ETeleportType::TeleportPhysics);
				NextAt = Now + 1.; ++Step; return false;
			}
			if (Now < NextAt) { return false; }
			if (Step == 1)
			{
				FVector2D Position;
				PC->ProjectWorldLocationToScreen(Enemy->GetActorLocation(), Position, true);
				Test->TestTrue(TEXT("Enemy is above the screen for the HUD overlap scenario"), Position.Y < 0.);
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("OnlineSplitQA/DuelTopMarker.png")), true, false);
				NextAt = Now + 1.; ++Step; return false;
			}
			Enemy->SetTrainingMenuFrozen(false);
			return true;
		}
	private:
		FAutomationTestBase* Test;
		double Started = FPlatformTime::Seconds(), NextAt = 0.;
		int32 Step = 0;
	};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelMarkerShotTest, "ChaosImpact.UI.DuelMarkerScreenshot",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FDuelMarkerShotTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FDuelMarkerShotCommand(this)); return true;
}
#endif
