#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactMenuWidget.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactSettings.h"
#include "ChaosImpactSettingsScreen.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/GameUserSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UnrealClient.h"

namespace
{
	// Explicit opt-in only: ordinary unattended suites keep their fixed screenshot window.
	// This test actually resizes the game window, waits several seconds in fullscreen and checks Slate input.
	class FFullscreenWindowCommand : public IAutomationLatentCommand
	{
	public:
		explicit FFullscreenWindowCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			using namespace ChaosImpactSettings;
			const double Now = FPlatformTime::Seconds();
			AChaosImpactPlayerController* PC = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld())
				{
					PC = Cast<AChaosImpactPlayerController>(Context.World()->GetFirstPlayerController());
					if (PC) { break; }
				}
			}
			UChaosImpactMenuWidget* Menu = PC ? PC->GetMenuWidget() : nullptr;
			UGameUserSettings* Settings = GEngine->GetGameUserSettings();
			FViewport* Viewport = GEngine->GameViewport ? GEngine->GameViewport->Viewport : nullptr;
			if (Now - Started > 25.0)
			{
				Test->AddError(FString::Printf(TEXT("Fullscreen test timed out at stage %d"), Stage));
				Restore(PC);
				return true;
			}
			if (!Menu || !Settings || !Viewport || Now < NextAt) { return false; }
			const auto Next = [this, Now](double Delay)
			{
				++Stage;
				NextAt = Now + Delay;
				return false;
			};
			switch (Stage)
			{
			case 0:
				OriginalMode = GetWindowMode();
				OriginalResolution = GetResolution();
				OriginalScreen = PC->GetCurrentScreen();
				bSaved = true;
				PC->ShowMenuScreen(EChaosImpactScreen::Settings);
				SetWindowMode(2);
				Settings->ApplyResolutionSettings(false);
				return Next(0.8);
			case 1:
				Press(EKeys::F11);
				Settings->ApplyResolutionSettings(false);
				StableUntil = Now + 5.0;
				return Next(0.8);
			case 2:
			case 5:
				if (Viewport->GetWindowMode() != EWindowMode::WindowedFullscreen
					|| Settings->GetFullscreenMode() != EWindowMode::WindowedFullscreen
					|| Viewport->GetSizeXY() != GetMonitorResolution())
				{
					Test->AddError(TEXT("Fullscreen window changed mode or size while idle"));
					Restore(PC);
					return true;
				}
				++StableFrames;
				if (Now < StableUntil) { return false; }
				if (auto* Screen = Menu->GetSettingsScreen())
				{
					Press(EKeys::E);
					Test->TestTrue(TEXT("Keyboard still changes tabs in fullscreen"),
						Screen->GetTab() == UChaosImpactSettingsScreen::ETab::Picture);
					Press(EKeys::Q);
					Test->TestTrue(TEXT("Keyboard returns to display tab"),
						Screen->GetTab() == UChaosImpactSettingsScreen::ETab::Display);
				}
				Press(EKeys::F11);
				Settings->ApplyResolutionSettings(false);
				return Next(0.8);
			case 3:
				Test->TestTrue(TEXT("F11 restores a window"), Viewport->GetWindowMode() == EWindowMode::Windowed);
				return Next(0.2);
			case 4:
				// Test the settings row as well as the shortcut. Both directions toggle the two safe modes.
				Menu->GetSettingsScreen()->SelectRow(Menu->GetSettingsScreen()->FindRow(UChaosImpactSettingsScreen::RowWindowMode));
				Press(EKeys::Right);
				Settings->ApplyResolutionSettings(false);
				StableUntil = Now + 5.0;
				return Next(0.8);
			default:
				Test->TestTrue(TEXT("Fullscreen held stable for repeated rendered frames"), StableFrames > 30);
				Test->TestTrue(TEXT("Second fullscreen exit restores a window"), Viewport->GetWindowMode() == EWindowMode::Windowed);
				UE_LOG(LogTemp, Display, TEXT("FULLSCREEN QA: %d stable frames, two entries, keyboard input works"), StableFrames);
				Restore(PC);
				return true;
			}
		}

	private:
		void Press(const FKey Key)
		{
			FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0));
			FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0));
		}
		void Restore(AChaosImpactPlayerController* PC)
		{
			if (!bSaved) { return; }
			ChaosImpactSettings::SetWindowMode(OriginalMode);
			ChaosImpactSettings::SetResolution(OriginalResolution);
			GEngine->GetGameUserSettings()->ApplyResolutionSettings(false);
			if (PC) { PC->ShowMenuScreen(OriginalScreen); }
			bSaved = false;
		}
		FAutomationTestBase* Test;
		double Started = FPlatformTime::Seconds();
		double NextAt = 0.0;
		double StableUntil = 0.0;
		int32 Stage = 0;
		int32 StableFrames = 0;
		int32 OriginalMode = 2;
		FIntPoint OriginalResolution = FIntPoint::ZeroValue;
		EChaosImpactScreen OriginalScreen = EChaosImpactScreen::Title;
		bool bSaved = false;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactFullscreenWindowTest, "ChaosImpact.Display.FullscreenWindow",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactFullscreenWindowTest::RunTest(const FString& Parameters)
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("CIFullscreenWindowQA")))
	{
		AddWarning(TEXT("Window-resize QA skipped. Opt in with -CIFullscreenWindowQA."));
		return true;
	}
	ADD_LATENT_AUTOMATION_COMMAND(FFullscreenWindowCommand(this));
	return true;
}

#endif
