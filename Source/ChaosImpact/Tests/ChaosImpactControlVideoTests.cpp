#if WITH_DEV_AUTOMATION_TESTS
#include "ChaosImpactSettingsScreen.h"
#include "ChaosImpactMenuWidget.h"
#include "ChaosImpactPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

namespace
{
	class FControlMoviesCommand : public IAutomationLatentCommand
	{
	public:
		explicit FControlMoviesCommand(FAutomationTestBase* InTest) : Test(InTest) {}
		bool Update() override
		{
			AChaosImpactPlayerController* PC = nullptr;
			for (const FWorldContext& C : GEngine->GetWorldContexts())
			{ if (C.World() && C.World()->IsGameWorld()) { PC = Cast<AChaosImpactPlayerController>(C.World()->GetFirstPlayerController()); } }
			auto* Settings = PC && PC->GetMenuWidget() ? PC->GetMenuWidget()->GetSettingsScreen() : nullptr;
			const double Now = FPlatformTime::Seconds();
			if (Now - Started > 90)
			{ Test->AddError(FString::Printf(TEXT("Recorded movie %d did not play"),Movie)); Cleanup(PC); return true; }
			if (!Settings) { return false; }
			if (Stage == 0)
			{
				if (!PC->IsGameplayActive()) { return false; }
				PC->TogglePauseMenu(); PC->OpenSettings(); Settings->SetTab(UChaosImpactSettingsScreen::ETab::Controls);
				Stage = 1;
			}
			if (Stage == 1)
			{
				Settings->EditControls(0,Movie < 4 ? EChaosImpactDevice::KeyboardMouse : EChaosImpactDevice::Switch);
				Settings->OpenControlStylePicker();
				const auto Desired = static_cast<EChaosImpactControlScheme>(Movie < 4 ? Movie : Movie - 4);
				for (int32 I = 0; I < 4 && Settings->GetPreviewControlScheme() != Desired; ++I) { Settings->MoveSelection(1); }
				UE_LOG(LogTemp,Display,TEXT("CONTROLVIDEO checking movie %d"),Movie);
				WorldTime = PC->GetWorld()->GetTimeSeconds(); NextAt = Now + 0.5; Stage = 2; return false;
			}
			if (Stage == 2)
			{
				if (Now < NextAt) { return false; }
				if (!Settings->IsControlVideoPlaying() || Settings->GetControlVideoTime() < 0.1) { return false; }
				UE_LOG(LogTemp,Display,TEXT("CONTROLVIDEO playing movie %d"),Movie);
				VideoTime = Settings->GetControlVideoTime(); NextAt = Now + 0.4; Stage = 3; return false;
			}
			if (Now < NextAt) { return false; }
			Test->TestTrue(FString::Printf(TEXT("Movie %d advances inside paused settings"),Movie),Settings->GetControlVideoTime() > VideoTime + 0.1);
			Test->TestEqual(TEXT("Movie playback does not unpause gameplay"),PC->GetWorld()->GetTimeSeconds(),WorldTime);
			if (++Movie < 6) { Stage = 1; return false; }
			Cleanup(PC); return true;
		}
	private:
		void Cleanup(AChaosImpactPlayerController* PC)
		{
			if (!PC) { return; }
			if (PC->GetCurrentScreen() == EChaosImpactScreen::Settings) { PC->CloseSettings(); }
			if (PC->GetCurrentScreen() == EChaosImpactScreen::Pause) { PC->ResumeGameplay(); }
		}
		FAutomationTestBase* Test;
		int32 Stage = 0, Movie = 0;
		double Started = FPlatformTime::Seconds(), NextAt = 0, VideoTime = 0;
		double WorldTime = 0;
	};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactControlMoviesTest,"ChaosImpact.Controls.Video",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FChaosImpactControlMoviesTest::RunTest(const FString& Parameters)
{ ADD_LATENT_AUTOMATION_COMMAND(FControlMoviesCommand(this)); return true; }
#endif
