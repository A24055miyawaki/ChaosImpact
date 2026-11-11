#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactBall.h"
#include "ChaosImpactBallSwitch.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactNovaParts.h"
#include "ChaosImpactPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	UWorld* ShotsWorld()
	{
		UWorld* World = nullptr;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World() && Context.World()->IsGameWorld())
			{
				World = Context.World();
			}
		}
		return World;
	}

	void SwitchShot(const FString& Name)
	{
		FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SwitchQA"), Name), true, false);
	}

	/**
	 * A local duel with every special ball switched off (?CIOffBalls=32766): no slot balls drawn, the slot shown with
	 * three struck-through windows, the HUD's three small struck slots, and only normal balls on the pads.
	 */
	class FDuelAllOffCommand : public IAutomationLatentCommand
	{
	public:
		explicit FDuelAllOffCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - StartedAt > 180.0)
			{
				Test->AddError(FString::Printf(TEXT("Timed out at stage %d."), Stage));
				return true;
			}
			UWorld* World = ShotsWorld();
			const AChaosImpactGameState* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
			if (!Match || Now < NextAt)
			{
				return false;
			}
			switch (Stage)
			{
			case 0:
				if (Match->Duel.Step != EChaosImpactDuelStep::BallDraw)
				{
					return false;
				}
				Test->TestEqual(TEXT("Every special ball off in the rules"), ChaosImpactBallSwitch::CountOn(Match->Rules), 0);
				Test->TestEqual(TEXT("No slot balls drawn"), Match->Duel.BallPicks.Num(), 0);
				NextAt = Now + 1.0;
				Stage = 1;
				return false;
			case 1:
				SwitchShot(TEXT("duel_draw_turning.png"));
				NextAt = Now + 2.6;
				Stage = 2;
				return false;
			case 2:
				SwitchShot(TEXT("duel_draw_done.png"));
				Stage = 3;
				return false;
			case 3:
				if (Match->Duel.Step != EChaosImpactDuelStep::Fight)
				{
					return false;
				}
				NextAt = Now + 6.0;
				Stage = 4;
				return false;
			case 4:
			{
				SwitchShot(TEXT("duel_fight.png"));
				int32 Specials = 0;
				int32 Balls = 0;
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It)
				{
					++Balls;
					Specials += It->GetBallType() != EChaosImpactBallType::Normal ? 1 : 0;
				}
				UE_LOG(LogTemp, Log, TEXT("All off duel: %d balls, %d special"), Balls, Specials);
				Test->TestTrue(TEXT("The pads put balls out"), Balls > 0);
				Test->TestEqual(TEXT("None of them special"), Specials, 0);
				return true;
			}
			default:
				return true;
			}
		}

	private:
		FAutomationTestBase* Test = nullptr;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Stage = 0;
	};

	/** In training (one CPU): nova parts over heads, then the overview map clouded over in smoke. */
	class FSwitchHudCommand : public IAutomationLatentCommand
	{
	public:
		explicit FSwitchHudCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - StartedAt > 120.0)
			{
				Test->AddError(TEXT("Timed out."));
				return true;
			}
			UWorld* World = ShotsWorld();
			AChaosImpactPlayerController* PC = World ? Cast<AChaosImpactPlayerController>(World->GetFirstPlayerController()) : nullptr;
			AChaosImpactCharacter* Player = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			if (!Player || !PC->IsGameplayActive() || Now < NextAt)
			{
				return false;
			}
			switch (Stage)
			{
			case -1:
				// (The training level may open itself again as the run begins: wait for it to settle.)
				NextAt = Now + 5.0;
				Stage = 0;
				return false;
			case 0:
			{
				AChaosImpactCharacter* CPU = nullptr;
				for (TActorIterator<AChaosImpactCPUController> It(World); It; ++It)
				{
					It->SetActorTickEnabled(false);
					CPU = Cast<AChaosImpactCharacter>(It->GetPawn());
				}
				Player->AddNovaPart(EChaosImpactNovaPart::Gear);
				Player->AddNovaPart(EChaosImpactNovaPart::Ring);
				if (CPU)
				{
					CPU->ResetForOnlineMatch(Player->GetActorLocation() + Player->GetActorForwardVector() * 450.0f, FRotator::ZeroRotator);
					CPU->AddNovaPart(EChaosImpactNovaPart::Core);
				}
				NextAt = Now + 1.2;
				Stage = 1;
				return false;
			}
			case 1:
				Test->TestEqual(TEXT("The player carries two parts"), static_cast<int32>(Player->GetNovaParts()),
					static_cast<int32>(ChaosImpactNovaParts::Bit(EChaosImpactNovaPart::Gear) | ChaosImpactNovaParts::Bit(EChaosImpactNovaPart::Ring)));
				SwitchShot(TEXT("nova_pips.png"));
				NextAt = Now + 0.5;
				Stage = 2;
				return false;
			case 2:
				PC->SetOverviewMapOpen(true);
				NextAt = Now + 1.2;
				Stage = 3;
				return false;
			case 3:
				SwitchShot(TEXT("map_clear.png"));
				Player->ApplyBlind(6.0f);
				NextAt = Now + 1.0;
				Stage = 4;
				return false;
			case 4:
				Test->TestTrue(TEXT("In smoke"), Player->IsBlinded());
				SwitchShot(TEXT("map_smoke.png"));
				NextAt = Now + 0.5;
				Stage = 5;
				return false;
			case 5:
				PC->SetOverviewMapOpen(false);
				return true;
			default:
				return true;
			}
		}

	private:
		FAutomationTestBase* Test = nullptr;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Stage = -1;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactDuelAllOffTest, "ChaosImpact.BallSwitch.DuelAllOff",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactDuelAllOffTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FDuelAllOffCommand(this));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactSwitchHudTest, "ChaosImpact.BallSwitch.HudShots",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactSwitchHudTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FSwitchHudCommand(this));
	return true;
}

namespace
{
	/**
	 * A tournament (listen host, -CIAutoTournament=1:0:5:1 -CIDevOffBalls=32766: the host against one CPU) with every
	 * special ball off: its match's slot draws nothing and shows three struck chips. Screenshot in Saved/SwitchQA.
	 */
	class FTourneyAllOffCommand : public IAutomationLatentCommand
	{
	public:
		explicit FTourneyAllOffCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - StartedAt > 240.0)
			{
				Test->AddError(FString::Printf(TEXT("Timed out at stage %d."), Stage));
				return true;
			}
			UWorld* World = ShotsWorld();
			const AChaosImpactGameState* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
			if (!Match || !Match->IsTournament() || Now < NextAt)
			{
				return false;
			}
			const FChaosImpactTournamentArena* Drawing = nullptr;
			for (const FChaosImpactTournamentArena& Arena : Match->TournamentArenas)
			{
				if (Arena.Duel.Step == EChaosImpactDuelStep::BallDraw)
				{
					Drawing = &Arena;
				}
			}
			switch (Stage)
			{
			case 0:
				if (!Drawing)
				{
					return false;
				}
				Test->TestEqual(TEXT("Every ball off in the tournament's rules"), ChaosImpactBallSwitch::CountOn(Match->Rules), 0);
				Test->TestEqual(TEXT("The tournament match draws no slot balls"), Drawing->Duel.BallPicks.Num(), 0);
				NextAt = Now + 2.6;
				Stage = 1;
				return false;
			case 1:
				SwitchShot(TEXT("tourney_draw.png"));
				return true;
			default:
				return true;
			}
		}

	private:
		FAutomationTestBase* Test = nullptr;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Stage = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactTourneyAllOffTest, "ChaosImpact.BallSwitch.TourneyAllOff",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactTourneyAllOffTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FTourneyAllOffCommand(this));
	return true;
}

#endif
