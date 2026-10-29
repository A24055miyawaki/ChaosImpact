#if WITH_DEV_AUTOMATION_TESTS
#include "ChaosImpactBallTypes.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactGameMode.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactHazardZone.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactScreen.h"
#include "DynamicRHI.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "RenderTimer.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "UnrealClient.h"

namespace
{
	/**
	 * Frame times over a few seconds of each heavy moment: a smoke ball's cloud in front of the camera (and the
	 * frame it bursts on), and the character select screen, the first time it opens and again. Logged as PERF lines (average and worst frame, game thread, render thread,
	 * GPU) to compare before and after a change. With -CIPerfCsv the match is also recorded by the CSV profiler
	 * (Saved/Profiling/CSV) for a breakdown of where the time goes.
	 */
	class FPerfCommand : public IAutomationLatentCommand
	{
	public:
		explicit FPerfCommand(FAutomationTestBase* InTest) : Test(InTest)
		{
			bCsv = FParse::Param(FCommandLine::Get(), TEXT("CIPerfCsv"));
		}

		bool Update() override
		{
			UWorld* World = nullptr;
			for (const auto& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld()) { World = Context.World(); break; }
			}
			auto* PC = World ? Cast<AChaosImpactPlayerController>(World->GetFirstPlayerController()) : nullptr;
			auto* Player = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			auto* GM = World ? World->GetAuthGameMode<AChaosImpactGameMode>() : nullptr;
			auto* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
			if (!PC || !Player || !GM || !Match) { Test->AddError(TEXT("Requires a training world with a player")); return true; }
			const double Now = FPlatformTime::Seconds();
			if (Phase >= 0 && Now >= MeasureFrom)
			{
				const float Frame = static_cast<float>(FApp::GetDeltaTime()) * 1000.0f;
				Sum += Frame;
				GameSum += FPlatformTime::ToMilliseconds(GGameThreadTime);
				RenderSum += FPlatformTime::ToMilliseconds(GRenderThreadTime);
				GpuSum += FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles());
				Worst = FMath::Max(Worst, Frame);
				Slow += Frame > 25.0f ? 1 : 0;
				++Frames;
			}
			if (Now < PhaseEnds) { return false; }
			if (Phase >= 0 && FCString::Strcmp(Names[Phase], TEXT("-")) != 0)
			{
				TRACE_END_REGION(Names[Phase]);
				const float N = FMath::Max(Frames, 1);
				UE_LOG(LogTemp, Display, TEXT("PERF %-12s frame %5.2f ms  worst %6.2f  game %5.2f  render %5.2f  gpu %5.2f  slow %d/%d"),
					Names[Phase], Sum / N, Worst, GameSum / N, RenderSum / N, GpuSum / N, Slow, Frames);
			}
			++Phase;
			if (Phase < UE_ARRAY_COUNT(Names) && FCString::Strcmp(Names[Phase], TEXT("-")) != 0)
			{
				// Insights: each moment as a region (-trace=cpu,gpu,frame,region).
				TRACE_BEGIN_REGION(Names[Phase]);
			}
			Sum = GameSum = RenderSum = GpuSum = Worst = 0.0f;
			Slow = Frames = 0;
			MeasureFrom = Now;
			PhaseEnds = Now + 4.0;
			switch (Phase)
			{
			case 0:
				// Settle first: the training field as it is.
				MeasureFrom = Now + 1.0;
				PhaseEnds = Now + 5.0;
				return false;
			case 1:
				// A smoke ball bursts a few metres in front of the player (its first frame counts).
				AChaosImpactHazardZone::Detonate(World, EChaosImpactBallType::Smoke,
					Player->GetActorLocation() + Player->GetActorForwardVector() * 450.0f - FVector(0, 0, 90), nullptr, nullptr, 1.0f);
				return false;
			case 2:
				// The cloud again, now everything is loaded: the steady cost of the cloud alone.
				AChaosImpactHazardZone::Detonate(World, EChaosImpactBallType::Smoke,
					Player->GetActorLocation() - Player->GetActorForwardVector() * 450.0f - FVector(0, 0, 90), nullptr, nullptr, 1.0f);
				MeasureFrom = Now + 0.5;
				return false;
			case 3:
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PerfQA/Smoke.png")), true, false);
				PC->OpenCharacterSelect();
				MeasureFrom = Now + 1.0;
				PhaseEnds = Now + 5.0;
				return false;
			case 4:
				// The second time the screen opens: what is left once its shaders and models are loaded.
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PerfQA/Select.png")), true, false);
				PC->CancelCharacterSelection();
				PhaseEnds = Now + 1.0;
				MeasureFrom = Now + 10.0;
				return false;
			case 5:
				PC->OpenCharacterSelect();
				MeasureFrom = Now + 1.0;
				PhaseEnds = Now + 5.0;
				return false;
			case 6:
				PC->CancelCharacterSelection();
				PhaseEnds = Now + 0.5;
				return false;
			default:
				return true;
			}
		}

	private:
		FAutomationTestBase* Test;
		const TCHAR* Names[7] = {TEXT("field"), TEXT("smoke-burst"), TEXT("smoke-steady"), TEXT("select-first"), TEXT("-"),
			TEXT("select-again"), TEXT("-")};
		bool bCsv = false;
		int32 Phase = -1;
		double PhaseEnds = 0.0;
		double MeasureFrom = 0.0;
		float Sum = 0.0f;
		float GameSum = 0.0f;
		float RenderSum = 0.0f;
		float GpuSum = 0.0f;
		float Worst = 0.0f;
		int32 Slow = 0;
		int32 Frames = 0;
	};
	/**
	 * A VS match as played: the player and seven strong CPUs on a stage level, 20 seconds from GO (started from the
	 * URL: ?CITraining=1?CIVersus=1?CIMatch=1?CIMatchCPU=7?CILocalPlayers=1). Logged like the heavy moments; with
	 * -CIPerfCsv recorded by the CSV profiler too.
	 */
	class FPerfMatchCommand : public IAutomationLatentCommand
	{
	public:
		explicit FPerfMatchCommand(FAutomationTestBase* InTest) : Test(InTest)
		{
			bCsv = FParse::Param(FCommandLine::Get(), TEXT("CIPerfCsv"));
		}

		bool Update() override
		{
			UWorld* World = nullptr;
			for (const auto& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld()) { World = Context.World(); break; }
			}
			const auto* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
			const double Now = FPlatformTime::Seconds();
			if (Now - Started > 240.0) { Test->AddError(TEXT("The match never started")); return true; }
			if (!Match || Match->Phase != EChaosImpactOnlinePhase::Match || Match->GetPhaseElapsedSeconds() < 2.0f)
			{
				return false;
			}
			if (MeasureFrom == 0.0)
			{
				MeasureFrom = Now;
				TRACE_BEGIN_REGION(TEXT("match"));
				if (bCsv) { GEngine->Exec(World, TEXT("csvprofile start")); }
			}
			const float Frame = static_cast<float>(FApp::GetDeltaTime()) * 1000.0f;
			Sum += Frame;
			GameSum += FPlatformTime::ToMilliseconds(GGameThreadTime);
			RenderSum += FPlatformTime::ToMilliseconds(GRenderThreadTime);
			GpuSum += FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles());
			Worst = FMath::Max(Worst, Frame);
			Slow += Frame > 25.0f ? 1 : 0;
			++Frames;
			if (!bShot && Now - MeasureFrom > 10.0)
			{
				bShot = true;
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PerfQA/Match.png")), true, false);
			}
			if (Now - MeasureFrom < 20.0) { return false; }
			TRACE_END_REGION(TEXT("match"));
			if (bCsv) { GEngine->Exec(World, TEXT("csvprofile stop")); }
			const float N = FMath::Max(Frames, 1);
			UE_LOG(LogTemp, Display, TEXT("PERF %-12s frame %5.2f ms  worst %6.2f  game %5.2f  render %5.2f  gpu %5.2f  slow %d/%d"),
				TEXT("match"), Sum / N, Worst, GameSum / N, RenderSum / N, GpuSum / N, Slow, Frames);
			return true;
		}

	private:
		FAutomationTestBase* Test;
		bool bCsv = false;
		bool bShot = false;
		double Started = FPlatformTime::Seconds();
		double MeasureFrom = 0.0;
		float Sum = 0.0f;
		float GameSum = 0.0f;
		float RenderSum = 0.0f;
		float GpuSum = 0.0f;
		float Worst = 0.0f;
		int32 Slow = 0;
		int32 Frames = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactPerfTest, "ChaosImpact.Perf.HeavyMoments",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FChaosImpactPerfTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FPerfCommand(this));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactPerfMatchTest, "ChaosImpact.Perf.Match",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FChaosImpactPerfMatchTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FPerfMatchCommand(this));
	return true;
}
#endif
