#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactGameState.h"
#include "ChaosImpactMenuWidget.h"
#include "ChaosImpactPlayerController.h"
#include "Framework/Application/SlateApplication.h"
#include "ChaosImpactResults.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	AChaosImpactPlayerController* FindResultsTestController()
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

	/**
	 * A short local VS match with CPUs (?CITraining=1?CIMatch=1?CIMatchCPU=3, -CIMatchSeconds=20) runs to its end; the
	 * results start by themselves: the podium show (third, second, the winner's drop and landing), the menu once it is
	 * over, then the numbers, awards and graph by themselves. Screenshots of each moment go to Saved/ResultsQA.
	 */
	class FResultsCommand : public IAutomationLatentCommand
	{
	public:
		explicit FResultsCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - StartedAt > 150.0)
			{
				Test->AddError(FString::Printf(TEXT("Stuck at shot %d."), Shot));
				return true;
			}
			AChaosImpactPlayerController* PC = FindResultsTestController();
			const UChaosImpactResultsView* Results = PC ? PC->GetResultsView() : nullptr;
			if (!Results || !Results->IsActive())
			{
				return false;
			}
			// The slowest frame from just after FINISH to the podium's first moments (before any screenshot stalls it).
			const float Show = Results->GetShowSeconds();
			if (Show > -1.5f && Show < 0.6f)
			{
				SlowestFrame = FMath::Max(SlowestFrame, static_cast<float>(FApp::GetDeltaTime()));
			}
			else if (Show >= 0.6f && !bReportedFrames)
			{
				bReportedFrames = true;
				UE_LOG(LogTemp, Display, TEXT("RESULTS slowest frame around the cut to the podium: %.1f ms"), SlowestFrame * 1000.0f);
			}
			struct FShot { float At; const TCHAR* Name; };
			static const FShot Shots[] = {{0.7f, TEXT("1_Third")}, {1.4f, TEXT("2_Second")}, {2.2f, TEXT("3_Drop")},
				{2.55f, TEXT("4_Land")}, {3.4f, TEXT("5_Banner")}, {5.6f, TEXT("6_Settled")}, {8.0f, TEXT("7_Stats")}};
			// Settled on the podium: one press goes on to the numbers.
			if (Shot == 6 && !bPressed && Results->GetShowSeconds() >= 6.2f)
			{
				bPressed = true;
				Test->TestTrue(TEXT("On the podium, its menu waits for a button"), PC->GetCurrentScreen() == EChaosImpactScreen::MatchEnd);
				Test->TestEqual(TEXT("…with no choices yet"), PC->GetMenuWidget()->GetEntryCount(), 0);
				FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(EKeys::Gamepad_FaceButton_Right, FModifierKeysState(), 0, false, 0, 0));
				Test->TestEqual(TEXT("A button goes on to the numbers"), Results->GetPage(), 1);
				// Four rows (each picks whose line the graph shows), the arrow to the awards (when they are on), and the
				// three choices.
				const int32 Arrow = UChaosImpactResultsView::bAwardsEnabled ? 1 : 0;
				Test->TestEqual(TEXT("…with the rows, the awards arrow and the rematch menu"), PC->GetMenuWidget()->GetEntryCount(), 7 + Arrow);
				Test->TestEqual(TEXT("The cursor starts on もう一度"), PC->GetMenuWidget()->GetSelectedIndex(), 4 + Arrow);
			}
			if (Shot >= UE_ARRAY_COUNT(Shots) && bChecked)
			{
				return UpdateTabs(PC, Results, Now);
			}
			if (Shot >= UE_ARRAY_COUNT(Shots))
			{
				bChecked = true;
				const FChaosImpactResultsData& Data = Results->GetData();
				int32 Throws = 0;
				int32 Awarded = 0;
				int32 Graphs = 0;
				for (const FChaosImpactResultEntry& Entry : Data.Entries)
				{
					Throws += Entry.Throws;
					Awarded += Entry.AwardTitle.IsEmpty() ? 0 : 1;
					Graphs += Entry.History.Num() >= 3 ? 1 : 0;
					UE_LOG(LogTemp, Display, TEXT("RESULTS %d. %s %d pt KO %d throws %d hits %d hit %d dodges %d long %d — %s / %s (graph %d)"),
						Entry.Rank, *Entry.Name, Entry.Points, Entry.Knockouts, Entry.Throws, Entry.Hits, Entry.TimesHit, Entry.Dodges,
						Entry.LongestHit, *Entry.AwardTitle, *Entry.AwardNote, Entry.History.Num());
				}
				Test->TestEqual(TEXT("Everyone who played is in the results"), Data.Entries.Num(), 4);
				Test->TestTrue(TEXT("Throws were counted"), Throws > 0);
				Test->TestEqual(TEXT("Everyone has an award"), Awarded, Data.Entries.Num());
				if (!UChaosImpactResultsView::bAwardsEnabled)
				{
					// Without the awards page: right on a row stays on the numbers.
					PC->GetMenuWidget()->Navigate(EKeys::Gamepad_DPad_Up);
					PC->GetMenuWidget()->Navigate(EKeys::Gamepad_DPad_Right);
					Test->TestEqual(TEXT("No awards page to go to"), Results->GetStatsTab(), 0);
				}
				Test->TestEqual(TEXT("Everyone has a graph"), Graphs, Data.Entries.Num());
				Test->TestTrue(TEXT("Still on the numbers (no way back)"), Results->GetPage() == 1);
				Test->TestTrue(TEXT("Their menu is up"), PC->GetCurrentScreen() == EChaosImpactScreen::MatchEnd);
				return UpdateTabs(PC, Results, Now);
			}
			if (Results->GetShowSeconds() >= Shots[Shot].At)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ResultsQA"),
					FString(Shots[Shot].Name) + TEXT(".png")), true, false);
				UE_LOG(LogTemp, Display, TEXT("RESULTS shot %s at %.2f s"), Shots[Shot].Name, Results->GetShowSeconds());
				++Shot;
			}
			return false;
		}

	private:
		/** Rows pick whose line the graph shows (again: everyone's); right goes to the awards, left comes back. */
		bool UpdateTabs(AChaosImpactPlayerController* PC, const UChaosImpactResultsView* Results, const double Now)
		{
			UChaosImpactMenuWidget* Menu = PC->GetMenuWidget();
			if (Now < NextAt)
			{
				return false;
			}
			const auto Snap = [](const TCHAR* Name)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ResultsQA"), FString(Name) + TEXT(".png")), true, false);
			};
			NextAt = Now + 1.4;
			switch (TabStep++)
			{
			case 0:
				// Up from the choices onto the first row, and pick it.
				Menu->Navigate(EKeys::Gamepad_DPad_Up);
				Test->TestEqual(TEXT("Up from the choices reaches the rows"), Menu->GetSelectedIndex(), 0);
				Menu->ConfirmSelection();
				Test->TestEqual(TEXT("The graph shows only the first row's line"), Results->GetGraphFocus(), 0);
				return false;
			case 1:
				// (A screenshot is taken at the end of the frame: nothing more changes in this step.)
				Snap(TEXT("8_GraphOne"));
				NextAt = Now + 0.3;
				return false;
			case 2:
				Menu->Navigate(EKeys::Gamepad_DPad_Down);
				Menu->ConfirmSelection();
				Test->TestEqual(TEXT("Another row: their line instead"), Results->GetGraphFocus(), 1);
				Menu->ConfirmSelection();
				Test->TestEqual(TEXT("The same row again: everyone's"), Results->GetGraphFocus(), static_cast<int32>(INDEX_NONE));
				NextAt = Now + 0.2;
				return false;
			case 3:
				if (!UChaosImpactResultsView::bAwardsEnabled)
				{
					// (No awards page: the rest is about it.)
					TabStep = 99;
					return false;
				}
				Menu->Navigate(EKeys::Gamepad_DPad_Right);
				Test->TestEqual(TEXT("Right from a row: the awards"), Results->GetStatsTab(), 1);
				Test->TestEqual(TEXT("…with the arrow back and the choices"), Menu->GetEntryCount(), 4);
				return false;
			case 4:
				Snap(TEXT("9_Awards"));
				NextAt = Now + 0.3;
				return false;
			case 5:
				Menu->Navigate(EKeys::Gamepad_DPad_Left);
				Test->TestEqual(TEXT("Left on the awards: back to the numbers"), Results->GetStatsTab(), 0);
				Test->TestEqual(TEXT("…on the row the cursor was on"), Menu->GetSelectedIndex(), 1);
				// The arrow works as a button too (a click or a tap).
				Menu->Navigate(EKeys::Gamepad_DPad_Down);
				Menu->Navigate(EKeys::Gamepad_DPad_Down);
				Menu->Navigate(EKeys::Gamepad_DPad_Down);
				Menu->Navigate(EKeys::Gamepad_DPad_Down);
				Test->TestTrue(TEXT("Down past the last row: the choices"), Menu->GetSelectedIndex() >= 4);
				return false;
			default:
				Snap(TEXT("10_BackToStats"));
				return true;
			}
		}

		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 TabStep = 0;
		bool bChecked = false;
		int32 Shot = 0;
		bool bPressed = false;
		bool bReportedFrames = false;
		float SlowestFrame = 0.0f;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactResultsTest, "ChaosImpact.Versus.Results",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactResultsTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FResultsCommand(this));
	return true;
}

#endif
