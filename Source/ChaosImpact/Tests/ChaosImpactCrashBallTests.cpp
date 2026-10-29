#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ChaosImpactCrashBall.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactGameMode.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactVersusStage.h"
#include "ChaosImpactSplashStage.h"
#include "ChaosImpactTempleStage.h"
#include "ChaosImpactCyberStage.h"
#include "ChaosImpactSnowStage.h"
#include "ChaosImpactForgeStage.h"
#include "ChaosImpactResults.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "ChaosImpactCharacter.h"
#include "GameFramework/SpringArmComponent.h"
#include "ChaosImpactScreen.h"
#include "ChaosImpactStageBase.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrashRulesTest, "ChaosImpact.CrashBall.Rules",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FCrashRulesTest::RunTest(const FString&)
{
	FChaosImpactMatchRules Rules;
	Rules.bCrashBall = true;
	Rules.TeamCount = 4;
	Rules.Minutes = 3;
	Rules = ChaosImpactMatch::Sanitize(Rules, 1);
	TestEqual(TEXT("Objective is always two teams"), Rules.TeamCount, 2);
	TestEqual(TEXT("Objective is five minutes"), Rules.Minutes, 5);
	FChaosImpactMatchRules Decoded;
	const FURL URL(nullptr, *FString::Printf(TEXT("/Game/Test?%s"), *ChaosImpactMatch::ToOptions(Rules)), TRAVEL_Absolute);
	TestTrue(TEXT("Rule URL reads"), ChaosImpactMatch::ReadOptions(URL, Decoded));
	TestTrue(TEXT("Objective survives travel"), Decoded.bCrashBall);
	Rules.bCrashBall = false;
	Rules.TeamCount = 0;
	Rules.Minutes = 3;
	Rules = ChaosImpactMatch::Sanitize(Rules, 1);
	TestEqual(TEXT("Original FFA remains"), Rules.TeamCount, 0);
	TestEqual(TEXT("Original duration remains"), Rules.Minutes, 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrashRoutesTest, "ChaosImpact.CrashBall.AllStageRoutes",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FCrashRoutesTest::RunTest(const FString&)
{
	UWorld* World = nullptr;
	for (const auto& Context : GEngine->GetWorldContexts())
	{
		if (Context.World() && Context.World()->IsGameWorld()) { World = Context.World(); break; }
	}
	if (!World) { AddError(TEXT("Run this test in a game world.")); return false; }
	auto* Match = World->GetGameState<AChaosImpactGameState>();
	if (!Match) { AddError(TEXT("Requires ChaosImpact game state.")); return false; }
	const bool OldVersus = Match->bVersusMatch;
	const auto OldPhase = Match->Phase;
	const auto OldRules = Match->Rules;
	const auto OldCrash = Match->Crash;
	Match->bVersusMatch = true;
	Match->Phase = EChaosImpactOnlinePhase::Match;
	Match->Rules.bCrashBall = true;
	TArray<UClass*> Classes = {AChaosImpactVersusStage::StaticClass(), AChaosImpactSplashStage::StaticClass(),
		AChaosImpactTempleStage::StaticClass(), AChaosImpactCyberStage::StaticClass(),
		AChaosImpactSnowStage::StaticClass(), AChaosImpactForgeStage::StaticClass()};
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (int32 Index = 0; Index < Classes.Num(); ++Index)
	{
		auto* Stage = World->SpawnActor<AChaosImpactStageBase>(Classes[Index], FVector(60000 + Index * 12000, 0, 0), FRotator::ZeroRotator, Params);
		if (!Stage) { AddError(TEXT("Could not spawn stage")); continue; }
		Stage->ConfigureCrashBallMode(true);
		int32 Red, Blue; float Progress;
		Stage->ComputeCrashBallCounts(Stage->GetCrashBallStartFloorLocation(), Red, Blue, Progress);
		TestEqual(TEXT("Center red count"), Red, 100);
		TestEqual(TEXT("Center blue count"), Blue, 100);
		for (int32 Attack = 0; Attack < 2; ++Attack)
		{
			const FTransform Transform = Stage->GetCrashBallGoalTransform(1 - Attack);
			auto* Goal = World->SpawnActorDeferred<AChaosImpactCrashGoal>(AChaosImpactCrashGoal::StaticClass(), Transform,
				nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			Goal->OpeningWidth = Stage->CrashGoalWidth;
			Goal->OpeningHeight = Stage->CrashGoalHeight;
			Goal->TeamIndex = 1 - Attack;
			Goal->FinishSpawning(Transform);
			auto* Ball = World->SpawnActor<AChaosImpactCrashBall>(AChaosImpactCrashBall::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			Ball->ResetToStartLocation(Stage->GetCrashBallStartFloorLocation(), Stage);
			// Exercise actual sweeps, slopes, steps, floor recovery and physical goal geometry.
			// Repeated simulated impacts supply normal push forces; no teleport along the route.
			bool bScored = false;
			float BestDistance = MAX_flt;
			for (int32 Frame = 0; Frame < 2400; ++Frame)
			{
				const FVector Push = Stage->GetCrashBallPushDirection(Attack, Ball->GetActorLocation());
				if (Frame % 18 == 0) { Ball->ApplySpecialEffectPush(nullptr, nullptr, Ball->GetActorLocation() - Push * 500, Push, 1.f, false); }
				Ball->Tick(1.f / 60.f);
				BestDistance = FMath::Min(BestDistance, static_cast<float>(FVector::Dist2D(Ball->GetActorLocation(), Transform.GetLocation())));
				if (Goal->IsBallScored(Ball)) { bScored = true; break; }
			}
			TestTrue(FString::Printf(TEXT("Stage%d team%d physically reaches goal (best distance %.0f, ended %s)"),
				Index + 1, Attack, BestDistance, *Stage->GetActorTransform().InverseTransformPosition(Ball->GetActorLocation()).ToCompactString()), bScored);
			Ball->Destroy();
			Goal->Destroy();
		}
		Stage->ConfigureCrashBallMode(false);
		TestFalse(TEXT("Original layout restored"), Stage->IsCrashBallModeActive());
		Stage->Destroy();
	}
	Match->bVersusMatch = OldVersus;
	Match->Phase = OldPhase;
	Match->Rules = OldRules;
	Match->Crash = OldCrash;
	return true;
}

namespace
{
	class FCrashMatchFlowCommand : public IAutomationLatentCommand
	{
	public:
		explicit FCrashMatchFlowCommand(FAutomationTestBase* InTest) : Test(InTest) {}
		bool Update() override
		{
			UWorld* World = nullptr;
			for (const auto& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld()) { World = Context.World(); break; }
			}
			auto* GM = World ? World->GetAuthGameMode<AChaosImpactGameMode>() : nullptr;
			auto* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
			if (!GM || !Match) { Test->AddError(TEXT("Requires a game world")); return true; }
			if (FPlatformTime::Seconds() - Started > 100.) { Test->AddError(TEXT("Match flow timed out")); return true; }
			if (Step == 0)
			{
				FChaosImpactMatchRules Rules;
				Rules.bCrashBall = true;
				Rules.CPUCount = 3;
				GM->ConfigureVersusMatch(Rules);
				Test->TestNotNull(TEXT("Objective spawned"), Match->GetCrashBall());
				GM->ConfirmTeamsAndStart();
				++Step;
				return false;
			}
			if (Step == 1 && Match->Phase == EChaosImpactOnlinePhase::Match && Match->GetPhaseElapsedSeconds() > 3.f)
			{
				if (auto* PC = Cast<AChaosImpactPlayerController>(World->GetFirstPlayerController()))
				{
					FVector View; FRotator Rotation; PC->GetPlayerViewPoint(View, Rotation);
					Test->AddInfo(FString::Printf(TEXT("Player %s view %s rotation %s stage %s"),
						*PC->GetPawn()->GetActorLocation().ToCompactString(), *View.ToCompactString(), *Rotation.ToCompactString(), *FVector(Match->StageCenter).ToCompactString()));
				}
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("CrashBallQA/Match.png")), true, false);
				++Step;
				return false;
			}
			if (Step == 2)
			{
				Match->Crash.RedCount = 18;
				Match->Crash.BlueCount = 63;
				Test->TestEqual(TEXT("Lower count leads"), Match->GetCrashLeader(), 0);
				const auto Results = FChaosImpactResultsData::Capture(Match, GM);
				Test->TestEqual(TEXT("Results use objective rather than KO points"), Results.TeamTotals[0].Key, 0);
				Test->TestFalse(TEXT("Unequal counts are not draw"), Results.bTie);
				return true;
			}
			return false;
		}
	private:
		FAutomationTestBase* Test;
		int32 Step = 0;
		double Started = FPlatformTime::Seconds();
	};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrashFlowTest, "ChaosImpact.CrashBall.MatchFlow",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FCrashFlowTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FCrashMatchFlowCommand(this));
	return true;
}
namespace
{
	UWorld* FindCrashTestWorld()
	{
		for (const auto& Context : GEngine->GetWorldContexts())
		{
			if (Context.World() && Context.World()->IsGameWorld()) { return Context.World(); }
		}
		return nullptr;
	}

	void CrashShot(const TCHAR* Name)
	{
		FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("CrashBallQA"), Name), true, false);
	}

	/**
	 * Eight CPUs (four a side) play a whole クラッシュボール match on each stage in turn, sped up, while the counts
	 * are logged every ten seconds of match time: the balance check (does a match get decided, how often by
	 * knockout, does either side run away with it).
	 */
	class FCrashCPUMatchCommand : public IAutomationLatentCommand
	{
	public:
		explicit FCrashCPUMatchCommand(FAutomationTestBase* InTest) : Test(InTest)
		{
			FString List;
			if (FParse::Value(FCommandLine::Get(), TEXT("CICrashStages="), List))
			{
				TArray<FString> Parts;
				List.ParseIntoArray(Parts, TEXT(","));
				for (const FString& Part : Parts) { Stages.Add(FCString::Atoi(*Part)); }
			}
			if (Stages.IsEmpty()) { Stages = {0, 1, 2, 3, 4, 5}; }
			FParse::Value(FCommandLine::Get(), TEXT("CICrashSpeed="), Speed);
		}

		bool Update() override
		{
			UWorld* World = FindCrashTestWorld();
			auto* GM = World ? World->GetAuthGameMode<AChaosImpactGameMode>() : nullptr;
			auto* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
			if (!GM || !Match) { Test->AddError(TEXT("Requires a game world")); return true; }
			const double Now = FPlatformTime::Seconds();
			if (Now - StageStarted > 420.0)
			{
				Test->AddError(FString::Printf(TEXT("Stage %d match timed out"), Stages[StageCursor] + 1));
				UGameplayStatics::SetGlobalTimeDilation(World, 1.0f);
				return true;
			}
			if (Step == 0)
			{
				FChaosImpactMatchRules Rules;
				Rules.bCrashBall = true;
				Rules.bSpectate = true;
				Rules.CPUCount = 8;
				Rules.CPULevel = 2;
				Rules.StageIndex = Stages[StageCursor];
				GM->ConfigureVersusMatch(Rules);
				GM->ConfirmTeamsAndStart();
				StageStarted = Now;
				NextLog = 0.0f;
				bShot = false;
				Step = 1;
				return false;
			}
			if (Step == 1)
			{
				if (Match->Phase != EChaosImpactOnlinePhase::Match) { return false; }
				UGameplayStatics::SetGlobalTimeDilation(World, Speed);
				Step = 2;
			}
			if (Step == 2)
			{
				const float Elapsed = Match->GetPhaseElapsedSeconds();
				const auto* Ball = Match->GetCrashBall();
				if (Match->Phase == EChaosImpactOnlinePhase::Match && Elapsed >= NextLog && Ball)
				{
					int32 Pushes[2] = {0, 0};
					int32 Knockouts[2] = {0, 0};
					for (const AChaosImpactPlayerState* Member : Match->GetCompetitors(true))
					{
						if (Member->TeamIndex == 0 || Member->TeamIndex == 1)
						{
							Pushes[Member->TeamIndex] += Member->CrashPushes;
							Knockouts[Member->TeamIndex] += Member->Knockouts;
						}
					}
					UE_LOG(LogTemp, Display, TEXT("CRASHSIM stage%d t=%3.0f red=%3d blue=%3d progress=%+.2f pushes=%d/%d KO=%d/%d overtime=%d"),
						Stages[StageCursor] + 1, Elapsed, Match->Crash.RedCount, Match->Crash.BlueCount, Match->Crash.Progress,
						Pushes[0], Pushes[1], Knockouts[0], Knockouts[1], Match->Crash.bOvertime ? 1 : 0);
					NextLog += 10.0f;
				}
				if (!bShot && Match->Phase == EChaosImpactOnlinePhase::Match && Elapsed > 40.0f)
				{
					bShot = true;
					CrashShot(*FString::Printf(TEXT("Sim_Stage%d.png"), Stages[StageCursor] + 1));
				}
				if (Match->Phase == EChaosImpactOnlinePhase::Results)
				{
					UGameplayStatics::SetGlobalTimeDilation(World, 1.0f);
					UE_LOG(LogTemp, Display, TEXT("CRASHSIM stage%d END winner=%d knockout=%d overtime=%d time=%.0f red=%d blue=%d"),
						Stages[StageCursor] + 1, Match->Crash.Winner, Match->Crash.bKnockout ? 1 : 0, Match->Crash.bOvertime ? 1 : 0,
						Match->GetPhaseElapsedSeconds(), Match->Crash.RedCount, Match->Crash.BlueCount);
					Test->TestTrue(TEXT("The giant ball moved off the centre"), Match->Crash.RedCount < 100 || Match->Crash.BlueCount < 100);
					Step = 3;
					ResultsAt = Now;
				}
				return false;
			}
			if (Step == 3 && Now - ResultsAt > 2.0)
			{
				if (++StageCursor >= Stages.Num()) { return true; }
				Step = 0;
			}
			return false;
		}

	private:
		FAutomationTestBase* Test;
		TArray<int32> Stages;
		float Speed = 3.0f;
		int32 StageCursor = 0;
		int32 Step = 0;
		double StageStarted = FPlatformTime::Seconds();
		double ResultsAt = 0.0;
		float NextLog = 0.0f;
		bool bShot = false;
	};

	/**
	 * The rule's screens as a player sees them: the rules screen with its guide, the match HUD from a player's view
	 * (counts, gauge, goals to attack and defend, the push arrows), overtime, a knockout and the results.
	 */
	class FCrashUIShotsCommand : public IAutomationLatentCommand
	{
	public:
		explicit FCrashUIShotsCommand(FAutomationTestBase* InTest) : Test(InTest)
		{
			FParse::Value(FCommandLine::Get(), TEXT("CICrashUIStage="), StageIndex);
		}

		bool Update() override
		{
			UWorld* World = FindCrashTestWorld();
			auto* GM = World ? World->GetAuthGameMode<AChaosImpactGameMode>() : nullptr;
			auto* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
			auto* PC = World ? Cast<AChaosImpactPlayerController>(World->GetFirstPlayerController()) : nullptr;
			if (!GM || !Match || !PC) { Test->AddError(TEXT("Requires a game world")); return true; }
			const double Now = FPlatformTime::Seconds();
			if (Now - Started > 200.0) { Test->AddError(TEXT("UI shots timed out")); return true; }
			if (Now < NextAt) { return false; }
			switch (Step)
			{
			case 0:
				PC->OpenMatchRules(EChaosImpactScreen::Playing);
				for (int32 Try = 0; Try < 6 && !PC->GetPendingMatchRules().bCrashBall; ++Try) { PC->AdjustMatchRule(1, 1); }
				Test->TestTrue(TEXT("The rule row reaches クラッシュボール"), PC->GetPendingMatchRules().bCrashBall);
				Test->TestEqual(TEXT("クラッシュボール is five minutes"), PC->GetPendingMatchRules().Minutes, ChaosImpactMatch::CrashBallMinutes);
				PC->AdjustMatchRule(0, 1);
				Test->TestEqual(TEXT("The time cannot change"), PC->GetPendingMatchRules().Minutes, ChaosImpactMatch::CrashBallMinutes);
				// The menu rebuilds its rows on the next screen change; show it again with the rule chosen.
				PC->ShowMenuScreen(EChaosImpactScreen::MatchRules);
				NextAt = Now + 1.6;
				++Step;
				return false;
			case 1:
				CrashShot(TEXT("UI_Rules.png"));
				NextAt = Now + 0.5;
				++Step;
				return false;
			case 2:
			{
				PC->CancelMatchRules();
				FChaosImpactMatchRules Rules;
				Rules.bCrashBall = true;
				Rules.CPUCount = 7;
				Rules.CPULevel = 1;
				Rules.StageIndex = StageIndex;
				GM->ConfigureVersusMatch(Rules);
				GM->ConfirmTeamsAndStart();
				++Step;
				return false;
			}
			case 3:
				if (Match->Phase == EChaosImpactOnlinePhase::Match && Match->GetPhaseElapsedSeconds() > 5.0f)
				{
					CrashShot(TEXT("UI_HudStart.png"));
					NextAt = Now + 20.0;
					++Step;
				}
				return false;
			case 4:
				CrashShot(TEXT("UI_HudMid.png"));
				NextAt = Now + 0.5;
				++Step;
				return false;
			case 5:
			{
				// Overtime as the team behind sees it: its grace drains from its last hit.
				const int32 Lead = Match->GetCrashLeader();
				OvertimeTeam = Lead == INDEX_NONE ? 0 : 1 - Lead;
				Match->Crash.bOvertime = true;
				Match->Crash.OvertimeTeam = OvertimeTeam;
				Match->Crash.OvertimeStartedAt = Match->GetServerWorldTimeSeconds() - 0.4;
				GM->NotifyCrashBallHit(nullptr, OvertimeTeam, false, FVector::ZeroVector);
				Match->Crash.OvertimeHitAt = Match->GetServerWorldTimeSeconds() - 1.5;
				NextAt = Now + 0.25;
				++Step;
				return false;
			}
			case 6:
				// (The shot is taken as the frame is drawn: overtime is only ended a moment later.)
				CrashShot(TEXT("UI_Overtime.png"));
				NextAt = Now + 0.3;
				++Step;
				return false;
			case 7:
			{
				Match->Crash.bOvertime = false;
				Match->Crash.OvertimeTeam = INDEX_NONE;
				// A knockout: the giant ball rolled in front of blue's goal, then over its line.
				auto* Ball = Match->GetCrashBall();
				AChaosImpactStageBase* Stage = nullptr;
				for (TActorIterator<AChaosImpactStageBase> It(World); It; ++It) { if (It->IsCrashBallModeActive()) { Stage = *It; } }
				if (!Ball || !Stage) { Test->AddError(TEXT("No giant ball")); return true; }
				GoalTransform = Stage->GetCrashBallGoalTransform(1);
				Ball->SetActorLocation(GoalTransform.TransformPosition(FVector(420.0f, 0.0f, Ball->GetBallRadius() + 4.0f)));
				NextAt = Now + 0.3;
				++Step;
				return false;
			}
			case 8:
				if (auto* Ball = Match->GetCrashBall())
				{
					Ball->SetActorLocation(GoalTransform.TransformPosition(FVector(-120.0f, 0.0f, Ball->GetBallRadius() + 4.0f)));
				}
				++Step;
				return false;
			case 9:
				if (Match->Phase == EChaosImpactOnlinePhase::Results)
				{
					Test->TestTrue(TEXT("The goal is a knockout"), Match->Crash.bKnockout);
					Test->TestEqual(TEXT("Red scored in blue's goal"), Match->Crash.Winner, 0);
					NextAt = Now + 0.45;
					++Step;
				}
				return false;
			case 10:
				CrashShot(TEXT("UI_Knockout.png"));
				NextAt = Now + 9.0;
				++Step;
				return false;
			case 11:
				CrashShot(TEXT("UI_Results.png"));
				return true;
			default:
				return true;
			}
		}

	private:
		FAutomationTestBase* Test;
		int32 Step = 0;
		int32 StageIndex = 0;
		int32 OvertimeTeam = 0;
		FTransform GoalTransform;
		double NextAt = 0.0;
		double Started = FPlatformTime::Seconds();
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrashCPUMatchTest, "ChaosImpact.CrashBall.CPUMatch",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FCrashCPUMatchTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FCrashCPUMatchCommand(this));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrashUIShotsTest, "ChaosImpact.CrashBall.UIShots",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FCrashUIShotsTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FCrashUIShotsCommand(this));
	return true;
}
#endif
