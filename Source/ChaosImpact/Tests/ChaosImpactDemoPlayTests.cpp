#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactCharacter.h"
#include "ChaosImpactDemoPlay.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	/**
	 * The title's demo play (run on the title level with -CIDemoIdle=<a few seconds>, and -CIDemoFromStart or
	 * -CIDemoMidBattle): left alone, the title opens the demo; a CPU's camera is shown with no other UI (pictures in
	 * Saved/DemoPlayQA); a key goes back to the title.
	 */
	class FDemoPlayCommand : public IAutomationLatentCommand
	{
	public:
		explicit FDemoPlayCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - StartedAt > 240.0)
			{
				Test->AddError(FString::Printf(TEXT("Stuck at step %d."), Step));
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
			if (!World || Now < NextAt)
			{
				return false;
			}
			AChaosImpactPlayerController* PC = Cast<AChaosImpactPlayerController>(World->GetFirstPlayerController());
			AChaosImpactDemoPlay* Demo = AChaosImpactDemoPlay::Find(World);
			const auto Shot = [](const FString& Name)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DemoPlayQA"), Name), true, false);
			};
			switch (Step)
			{
			case 0:
				// Left alone on the title, the demo opens by itself.
				if (!PC || !PC->IsDemoPlay() || !Demo)
				{
					return false;
				}
				UE_LOG(LogTemp, Display, TEXT("DEMOTEST demo opened (%s) on %s"), Demo->IsFromStart() ? TEXT("from the start") : TEXT("mid-battle"),
					*World->GetMapName());
				Step = 1;
				return false;
			case 1:
				if (!Demo || !Demo->IsShowing() || Demo->GetAge() < 1.2f)
				{
					return false;
				}
				if (const AChaosImpactGameState* Opening = World->GetGameState<AChaosImpactGameState>())
				{
					UE_LOG(LogTemp, Display, TEXT("DEMOTEST shown: phase %d, opening %.2f s, viewing %s"), static_cast<int32>(Opening->Phase),
						Opening->GetIntroElapsedSeconds(), *GetNameSafe(PC->GetViewTarget()));
				}
				Shot(TEXT("1_Shown.png"));
				Step = 2;
				NextAt = Now + 8.0;
				return false;
			case 2:
			{
				const AChaosImpactGameState* Match = World->GetGameState<AChaosImpactGameState>();
				UE_LOG(LogTemp, Display, TEXT("DEMOTEST phase %d, %.0f s in, following %s"), Match ? static_cast<int32>(Match->Phase) : -1,
					Match ? Match->GetPhaseElapsedSeconds() : 0.0f, *GetNameSafe(Demo->GetFollowed()));
				Test->TestNotNull(TEXT("A CPU's camera"), Demo->GetFollowed());
				Test->TestTrue(TEXT("Viewing it"), PC->GetViewTarget() == Demo->GetFollowed());
				int32 Characters = 0;
				for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
				{
					++Characters;
				}
				Test->TestTrue(TEXT("CPUs playing"), Characters >= 4);
				Shot(TEXT("2_Following.png"));
				Step = 3;
				NextAt = Now + 6.0;
				return false;
			}
			case 3:
				Shot(TEXT("3_Later.png"));
				Step = 4;
				NextAt = Now + 0.5;
				return false;
			case 4:
				// A button: back to the title.
				FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(EKeys::Enter, FModifierKeysState(), 0, false, 0, 0));
				FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(EKeys::Enter, FModifierKeysState(), 0, false, 0, 0));
				Step = 5;
				NextAt = Now + 1.0;
				return false;
			case 5:
				if (!PC || PC->IsDemoPlay() || Demo)
				{
					return false;
				}
				UE_LOG(LogTemp, Display, TEXT("DEMOTEST back on %s"), *World->GetMapName());
				return true;
			default:
				return true;
			}
		}

	private:
		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Step = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactDemoPlayTest, "ChaosImpact.DemoPlay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactDemoPlayTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FDemoPlayCommand(this));
	return true;
}

#endif
