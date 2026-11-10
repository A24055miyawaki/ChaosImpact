#if WITH_DEV_AUTOMATION_TESTS
#include "ChaosImpactColourEdit.h"
#include "ChaosImpactCustomColour.h"
#include "ChaosImpactLoadoutSubsystem.h"
#include "ChaosImpactMenuWidget.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactScreen.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	/**
	 * カラーエディット from a room's pause menu: saving a preset asks このキャラにしますか？, and yes makes P1 play as that
	 * character in it (still in カラーエディット); from anywhere else, saving asks nothing. The preset made here and P1's
	 * loadout are put back afterwards. Run in a room (Lvl_ThirdPerson?listen?CITraining=1?CITargets=0) and offline.
	 */
	class FColourEditApplyCommand : public IAutomationLatentCommand
	{
	public:
		explicit FColourEditApplyCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld()) { World = Context.World(); break; }
			}
			AChaosImpactPlayerController* PC = World ? Cast<AChaosImpactPlayerController>(World->GetFirstPlayerController()) : nullptr;
			UChaosImpactColourEdit* Edit = PC && PC->GetMenuWidget() ? PC->GetMenuWidget()->GetColourEdit() : nullptr;
			const double Now = FPlatformTime::Seconds();
			if (Now - Started > 60.0)
			{
				Test->AddError(FString::Printf(TEXT("Timed out at step %d"), Step));
				Restore(PC, Edit);
				return true;
			}
			if (!PC || Now < NextAt)
			{
				return false;
			}
			const auto Wait = [this, Now](const double Seconds) { ++Step; NextAt = Now + Seconds; return false; };
			switch (Step)
			{
			case 0:
				if (Now - Started < 4.0) { return false; }
				bRoom = PC->IsOnlineRoom();
				Before = UChaosImpactLoadoutSubsystem::Get(PC)->GetLoadout(0);
				PC->ShowMenuScreen(EChaosImpactScreen::Pause);
				PC->OpenColourEdit(EChaosImpactScreen::Pause);
				return Wait(1.5);
			case 1:
				if (!Edit || PC->GetCurrentScreen() != EChaosImpactScreen::ColourEdit)
				{
					Test->AddError(TEXT("カラーエディット did not open"));
					return true;
				}
				Edit->SavePreset(true, TEXT("QA_TEST"));
				SavedCharacter = Edit->GetCharacter();
				SavedPreset = Edit->GetPreset();
				Test->TestEqual(TEXT("Saving asks このキャラにしますか？ only from a room's pause menu"), Edit->IsApplyAsking(), bRoom);
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ColourApplyQA"),
					bRoom ? TEXT("Ask.png") : TEXT("NoAsk.png")), true, false);
				return Wait(0.6);
			case 2:
				if (bRoom)
				{
					// Yes (the first choice): P1 plays as it, and カラーエディット stays open.
					FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(EKeys::Enter, FModifierKeysState(), 0, false, 0, 0));
					FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(EKeys::Enter, FModifierKeysState(), 0, false, 0, 0));
				}
				return Wait(0.5);
			case 3:
			{
				const FChaosImpactLoadout After = UChaosImpactLoadoutSubsystem::Get(PC)->GetLoadout(0);
				if (bRoom)
				{
					Test->TestFalse(TEXT("The question has been answered"), Edit->IsApplyAsking());
					Test->TestEqual(TEXT("P1 plays as the character edited"), After.Character, SavedCharacter);
					Test->TestEqual(TEXT("P1 wears the colours saved"), After.ColourPreset, SavedPreset);
					Test->TestTrue(TEXT("Still in カラーエディット"), PC->GetCurrentScreen() == EChaosImpactScreen::ColourEdit);
					Test->TestTrue(TEXT("Still in the room"), PC->IsOnlineRoom());
				}
				else
				{
					Test->TestEqual(TEXT("Offline, saving changes no one's character"), After.Character, Before.Character);
				}
				UE_LOG(LogTemp, Display, TEXT("COLOURAPPLY room=%d character %d preset %d -> loadout %d/%d"), bRoom ? 1 : 0,
					SavedCharacter, SavedPreset, After.Character, After.ColourPreset);
				Restore(PC, Edit);
				return true;
			}
			default:
				return true;
			}
		}

	private:
		void Restore(AChaosImpactPlayerController* PC, UChaosImpactColourEdit* Edit)
		{
			if (!PC)
			{
				return;
			}
			if (UChaosImpactColourSubsystem* Colours = UChaosImpactColourSubsystem::Get(PC); Colours && SavedPreset != INDEX_NONE)
			{
				Colours->RemovePreset(SavedCharacter, SavedPreset);
			}
			UChaosImpactLoadoutSubsystem::Get(PC)->SetLoadout(0, Before);
			(void)Edit;
		}

		FAutomationTestBase* Test;
		FChaosImpactLoadout Before;
		double Started = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Step = 0;
		int32 SavedCharacter = 0;
		int32 SavedPreset = INDEX_NONE;
		bool bRoom = false;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactColourEditApplyTest, "ChaosImpact.Online.ColourEditApply",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactColourEditApplyTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FColourEditApplyCommand(this));
	return true;
}
#endif
