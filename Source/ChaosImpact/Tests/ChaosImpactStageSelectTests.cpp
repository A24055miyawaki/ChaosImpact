#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactCPUController.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactMenuWidget.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactSplashStage.h"
#include "ChaosImpactVersusStage.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	AChaosImpactPlayerController* FindStageSelectController()
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

	void StageKey(const FKey Key)
	{
		FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0));
		FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0));
	}

	void StageShot(const TCHAR* Name)
	{
		FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("StageSelect"), Name), true, false);
	}

	/**
	 * Local VS with the keyboard: the rules' 決定 opens stage select, back returns to the rules as they were,
	 * and picking ステージ2 opens the VS level on the harbour stage.
	 */
	class FStageSelectCommand : public IAutomationLatentCommand
	{
	public:
		explicit FStageSelectCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now < NextAt)
			{
				return false;
			}
			AChaosImpactPlayerController* PC = FindStageSelectController();
			UChaosImpactMenuWidget* Menu = PC ? PC->GetMenuWidget() : nullptr;
			if ((Stage < 4 || Stage >= 10) && (!PC || !Menu))
			{
				return Waited(Now, TEXT("No menu."));
			}
			const auto Next = [this, Now](const double Seconds)
			{
				++Stage;
				NextAt = Now + Seconds;
				return false;
			};
			const auto Screen = [PC](const EChaosImpactScreen Expected)
			{
				return PC && PC->GetCurrentScreen() == Expected;
			};

			switch (Stage)
			{
			case 0:
				if (Now - StartedAt < 3.0)
				{
					return false;
				}
				PC->ShowMenuScreen(EChaosImpactScreen::ModeSelect);
				PC->BeginVersusLocal();
				PC->PrepareTrainingControllerAssignment(1);
				PC->RegisterKeyboardMouseJoin();
				PC->ConfirmControllerAssignments();
				PC->ConfirmCharacterSelection();
				if (!Screen(EChaosImpactScreen::MatchRules))
				{
					Test->AddError(TEXT("Character select did not lead to the rules."));
					return true;
				}
				// Four rule rows (time, teams, CPUs, CPU strength), then 決定 and 戻る: no stage row any more.
				Test->TestEqual(TEXT("The rules screen has no stage row"), Menu->GetEntryCount(), 6);
				StageKey(EKeys::Right);
				Minutes = PC->GetPendingMatchRules().Minutes;
				StageKey(EKeys::Down);
				StageKey(EKeys::Down);
				StageKey(EKeys::Down);
				Test->TestEqual(TEXT("The CPU strength row can be reached with a CPU in the match"), Menu->GetSelectedIndex(), 3);
				{
					const int32 Before = PC->GetPendingMatchRules().CPULevel;
					StageKey(EKeys::Right);
					CPULevel = PC->GetPendingMatchRules().CPULevel;
					Test->TestEqual(TEXT("Right changes the CPU strength"), CPULevel, (Before + 1) % ChaosImpactMatch::CPULevelCount);
				}
				// The rules screen with the strength row, once it has settled.
				Stage = 10;
				NextAt = Now + 1.2;
				return false;
			case 10:
				StageShot(TEXT("00-Rules.png"));
				Stage = 11;
				NextAt = Now + 0.3;
				return false;
			case 11:
				StageKey(EKeys::Down);
				Test->TestEqual(TEXT("Four rows down is 決定"), Menu->GetSelectedIndex(), 4);
				StageKey(EKeys::Enter);
				Test->TestTrue(TEXT("The rules' 決定 opens stage select"), Screen(EChaosImpactScreen::StageSelect));
				Test->TestEqual(TEXT("Stage select starts on the stage chosen before"), Menu->GetSelectedIndex(), 0);
				Stage = 1;
				NextAt = Now + 1.2;
				return false;
			case 1:
				StageShot(TEXT("01-Stage1.png"));
				return Next(0.3);
			case 2:
				StageKey(EKeys::Escape);
				Test->TestTrue(TEXT("Back returns to the rules"), Screen(EChaosImpactScreen::MatchRules));
				Test->TestEqual(TEXT("Back lands on 決定"), Menu->GetSelectedIndex(), 4);
				Test->TestEqual(TEXT("The rules are kept"), PC->GetPendingMatchRules().Minutes, Minutes);
				StageKey(EKeys::Enter);
				Test->TestTrue(TEXT("決定 again opens stage select"), Screen(EChaosImpactScreen::StageSelect));
				StageKey(EKeys::Right);
				Test->TestEqual(TEXT("Right moves to ステージ2"), Menu->GetSelectedIndex(), 1);
				StageKey(EKeys::Down);
				Test->TestEqual(TEXT("Down moves to the back button"), Menu->GetSelectedIndex(), 2);
				StageKey(EKeys::Up);
				Test->TestEqual(TEXT("Up returns to the cards"), Menu->GetSelectedIndex(), 0);
				StageKey(EKeys::Right);
				return Next(1.0);
			case 3:
				StageShot(TEXT("02-Stage2.png"));
				return Next(0.3);
			case 4:
				StageKey(EKeys::Enter);
				Test->TestEqual(TEXT("ステージ2 is picked"), PC->GetPendingMatchRules().StageIndex, 1);
				return Next(2.0);
			case 5:
			{
				UWorld* World = PC ? PC->GetWorld() : nullptr;
				const AChaosImpactGameState* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
				if (!World || !World->URL.HasOption(TEXT("CIMatch=1")) || !Match || !Match->bVersusMatch)
				{
					return Waited(Now, TEXT("The VS level never opened."));
				}
				int32 Harbours = 0;
				int32 Squares = 0;
				for (TActorIterator<AChaosImpactStageBase> It(World); It; ++It)
				{
					Harbours += It->IsA<AChaosImpactSplashStage>() ? 1 : 0;
					Squares += It->IsA<AChaosImpactVersusStage>() ? 1 : 0;
				}
				if (Harbours == 0)
				{
					return Waited(Now, TEXT("The picked stage never appeared."));
				}
				Test->TestEqual(TEXT("The match is on ステージ2"), Match->Rules.StageIndex, 1);
				Test->TestEqual(TEXT("The chosen minutes reached the match"), Match->Rules.Minutes, Minutes);
				Test->TestEqual(TEXT("The chosen CPU strength reached the match"), Match->Rules.CPULevel, CPULevel);
				for (TActorIterator<AChaosImpactCPUController> It(World); It; ++It)
				{
					Test->TestEqual(TEXT("Each CPU plays at the chosen strength"), It->GetDifficulty(), CPULevel);
				}
				Test->TestEqual(TEXT("One harbour stage"), Harbours, 1);
				Test->TestEqual(TEXT("No square stage"), Squares, 0);
				return Next(4.0);
			}
			case 6:
				StageShot(TEXT("03-InMatch.png"));
				return Next(0.8);
			default:
				return true;
			}
		}

	private:
		bool Waited(const double Now, const TCHAR* Error)
		{
			if (Now - StartedAt < 150.0)
			{
				return false;
			}
			Test->AddError(Error);
			return true;
		}

		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Stage = 0;
		int32 Minutes = 0;
		int32 CPULevel = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactStageSelectTest, "ChaosImpact.Versus.StageSelect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::EngineFilter)

bool FChaosImpactStageSelectTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FStageSelectCommand(this));
	return true;
}

#endif
