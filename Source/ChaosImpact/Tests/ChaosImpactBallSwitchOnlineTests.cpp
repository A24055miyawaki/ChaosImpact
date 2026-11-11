#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactBall.h"
#include "ChaosImpactBallSwitch.h"
#include "ChaosImpactGameState.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	/**
	 * Run on a guest joined to a host's room (the host: a listen server with -CIAutoVersus=0:0:<s>:duel -CIDevOffBalls=<bits>;
	 * the guest given the same -CIDevOffBalls): the room's ボールスイッチ reaches the guest with the rules, the duel's slot
	 * draws only kinds switched on, and the pads put out none switched off. Screenshots in Saved/SwitchQA.
	 */
	class FOnlineSwitchCommand : public IAutomationLatentCommand
	{
	public:
		explicit FOnlineSwitchCommand(FAutomationTestBase* InTest) : Test(InTest)
		{
			FParse::Value(FCommandLine::Get(), TEXT("CIDevOffBalls="), Expected);
		}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - StartedAt > 200.0)
			{
				Test->AddError(FString::Printf(TEXT("Timed out at stage %d."), Stage));
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
			const AChaosImpactGameState* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
			if (!Match || World->GetNetMode() != NM_Client || Now < NextAt)
			{
				return false;
			}
			const auto Shot = [](const TCHAR* Name)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SwitchQA"), Name), true, false);
			};
			switch (Stage)
			{
			case 0:
				if (!Match->bVersusMatch || Match->Duel.Step != EChaosImpactDuelStep::BallDraw || Match->Duel.BallPicks.IsEmpty())
				{
					return false;
				}
			{
				Test->TestEqual(TEXT("The room's ボールスイッチ reached the guest"), Match->Rules.OffBalls, Expected);
				bool bOnlyOn = true;
				FString Drawn;
				for (const uint8 Pick : Match->Duel.BallPicks)
				{
					const EChaosImpactBallType Kind = static_cast<EChaosImpactBallType>(Pick);
					bOnlyOn &= ChaosImpactBallSwitch::IsOn(Match->Rules, Kind);
					Drawn += FString(ChaosImpactBallTypes::GetInternalName(Kind)) + TEXT(" ");
				}
				UE_LOG(LogTemp, Log, TEXT("Online switch: off=%d drawn=%s"), Match->Rules.OffBalls, *Drawn);
				Test->TestTrue(TEXT("Only kinds switched on are drawn"), bOnlyOn);
				Test->TestEqual(TEXT("Three slots"), Match->Duel.BallPicks.Num(), 3);
				NextAt = Now + 2.2;
				Stage = 1;
				return false;
			}
			case 1:
				Shot(TEXT("online_slot.png"));
				Stage = 2;
				return false;
			case 2:
				if (Match->Duel.Step != EChaosImpactDuelStep::Fight)
				{
					return false;
				}
				NextAt = Now + 12.0;
				Stage = 3;
				return false;
			case 3:
			{
				Shot(TEXT("online_fight.png"));
				int32 Off = 0;
				int32 Seen = 0;
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It)
				{
					++Seen;
					Off += ChaosImpactBallSwitch::IsOn(Match->Rules, It->GetBallType()) ? 0 : 1;
				}
				UE_LOG(LogTemp, Log, TEXT("Online switch: %d balls seen, %d switched off"), Seen, Off);
				Test->TestEqual(TEXT("No ball switched off comes out"), Off, 0);
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
		int32 Expected = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactOnlineSwitchTest, "ChaosImpact.BallSwitch.Online",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactOnlineSwitchTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FOnlineSwitchCommand(this));
	return true;
}

#endif
