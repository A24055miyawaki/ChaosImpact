#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactCharacter.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactTitleDemo.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	/**
	 * The title's demo: on the title (the level opened with no options) a CPU match plays behind it and is filmed, the
	 * world is not paused, and leaving for another menu holds the match still. Screenshots go to Saved/TitleQA.
	 */
	class FTitleDemoCommand : public IAutomationLatentCommand
	{
	public:
		explicit FTitleDemoCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			AChaosImpactPlayerController* PC = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld())
				{
					PC = PC ? PC : Cast<AChaosImpactPlayerController>(Context.World()->GetFirstPlayerController());
				}
			}
			UWorld* World = PC ? PC->GetWorld() : nullptr;
			if (Stage == 1)
			{
				// The longest frame while the demo plays (a stutter shows up here).
				const double Frame = Now - LastFrameAt;
				SlowestFrame = LastFrameAt > 0.0 ? FMath::Max(SlowestFrame, Frame) : SlowestFrame;
				SlowFrames += LastFrameAt > 0.0 && Frame > 0.05 ? 1 : 0;
			}
			LastFrameAt = Now;
			if (!World || Now < NextAt)
			{
				return Waited(Now);
			}
			switch (Stage)
			{
			case 0:
			{
				if (PC->GetCurrentScreen() != EChaosImpactScreen::Title || !PC->GetTitleDemo())
				{
					return Waited(Now);
				}
				int32 CPUs = 0;
				for (TActorIterator<AChaosImpactCPUController> It(World); It; ++It)
				{
					CPUs += Cast<AChaosImpactCharacter>(It->GetPawn()) ? 1 : 0;
				}
				Test->TestEqual(TEXT("Four CPUs play behind the title"), CPUs, AChaosImpactTitleDemo::CPUCount);
				Test->TestFalse(TEXT("The title does not pause the world"), World->IsPaused());
				Stage = 1;
				NextAt = Now + 8.0;
				StartedSeconds = World->GetTimeSeconds();
				return false;
			}
			case 1:
			{
				const double Played = World->GetTimeSeconds() - StartedSeconds;
				int32 Thrown = 0;
				for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
				{
					Thrown += Cast<AChaosImpactCPUController>(It->GetController()) && It->GetHealth() < It->GetMaxHealth() ? 1 : 0;
				}
				UE_LOG(LogTemp, Display, TEXT("TITLEDEMO %.1f s of play behind the title, fade %.2f, %d CPUs hurt, slowest frame %.0f ms, %d frames over 50 ms"),
					Played, PC->GetTitleDemo()->GetFadeIn(), Thrown, SlowestFrame * 1000.0, SlowFrames);
				Test->TestTrue(TEXT("The match runs while the title shows"), Played > 6.0);
				Test->TestTrue(TEXT("The picture has faded in"), PC->GetTitleDemo()->GetFadeIn() >= 1.0f);
				Shoot(TEXT("Title_A"));
				Stage = 2;
				NextAt = Now + 3.0;
				return false;
			}
			case 2:
				Shoot(TEXT("Title_B"));
				Stage = 6;
				NextAt = Now + 1.0;
				return false;
			case 6:
				PC->ShowMenuScreen(EChaosImpactScreen::ModeSelect);
				Stage = 3;
				NextAt = Now + 1.0;
				return false;
			case 3:
				Test->TestTrue(TEXT("Another menu pauses the world (the match holds still)"), World->IsPaused());
				Test->TestEqual(TEXT("...and nothing is filmed"), PC->GetTitleDemo()->GetFadeIn(), 0.0f);
				PC->ShowMenuScreen(EChaosImpactScreen::Title);
				Stage = 4;
				NextAt = Now + 2.5;
				return false;
			case 4:
				Test->TestFalse(TEXT("Back on the title it plays on"), World->IsPaused());
				Shoot(TEXT("Title_C"));
				Stage = 5;
				NextAt = Now + 1.0;
				return false;
			case 5:
				return true;
			default:
				return true;
			}
		}

	private:
		static void Shoot(const TCHAR* Name)
		{
			const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TitleQA"), FString(Name) + TEXT(".png"));
			FScreenshotRequest::RequestScreenshot(Path, true, false);
		}

		bool Waited(const double Now)
		{
			if (Now - StartedAt > 60.0)
			{
				Test->AddError(TEXT("The title demo never started."));
				return true;
			}
			return false;
		}

		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		double StartedSeconds = 0.0;
		double LastFrameAt = 0.0;
		double SlowestFrame = 0.0;
		int32 SlowFrames = 0;
		int32 Stage = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactTitleDemoTest, "ChaosImpact.Menu.TitleDemo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactTitleDemoTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FTitleDemoCommand(this));
	return true;
}

namespace
{
	/**
	 * Back to the title from a battle: the title world is opened again and its demo match plays. Run in training with
	 * a CPU (?CITraining=1?CICPUCount=1?CITargets=0).
	 */
	class FTitleReturnCommand : public IAutomationLatentCommand
	{
	public:
		explicit FTitleReturnCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			AChaosImpactPlayerController* PC = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld())
				{
					PC = PC ? PC : Cast<AChaosImpactPlayerController>(Context.World()->GetFirstPlayerController());
				}
			}
			UWorld* World = PC ? PC->GetWorld() : nullptr;
			if (Now - StartedAt > 90.0)
			{
				Test->AddError(TEXT("Never got back to the title's demo."));
				return true;
			}
			if (!World || Now < NextAt)
			{
				return false;
			}
			if (!bLeft)
			{
				if (!World->URL.HasOption(TEXT("CITraining=1")) || !PC->IsGameplayActive())
				{
					return false;
				}
				// The pause menu's タイトルへ.
				PC->ShowMenuScreen(EChaosImpactScreen::Title);
				bLeft = true;
				NextAt = Now + 1.0;
				return false;
			}
			if (bShot)
			{
				return true;
			}
			if (World->URL.HasOption(TEXT("CITraining=1")) || PC->GetCurrentScreen() != EChaosImpactScreen::Title || !PC->GetTitleDemo()
				|| PC->GetTitleDemo()->GetFadeIn() < 1.0f)
			{
				return false;
			}
			int32 CPUs = 0;
			for (TActorIterator<AChaosImpactCPUController> It(World); It; ++It)
			{
				CPUs += It->GetPawn() ? 1 : 0;
			}
			UE_LOG(LogTemp, Display, TEXT("TITLERETURN back in the title world (%s), demo with %d CPUs, paused %d"),
				*World->URL.ToString(), CPUs, World->IsPaused());
			Test->TestEqual(TEXT("Only the demo's CPUs (the battle's are gone with its world)"), CPUs, AChaosImpactTitleDemo::CPUCount);
			Test->TestFalse(TEXT("The demo plays"), World->IsPaused());
			FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TitleQA"), TEXT("Title_Return.png")), true, false);
			bShot = true;
			NextAt = Now + 1.0;
			return false;
		}

	private:
		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		bool bLeft = false;
		bool bShot = false;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactTitleReturnTest, "ChaosImpact.Menu.TitleReturn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactTitleReturnTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FTitleReturnCommand(this));
	return true;
}

#endif
