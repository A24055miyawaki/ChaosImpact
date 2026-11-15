#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactHints.h"
#include "ChaosImpactMenuWidget.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactScreen.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	/**
	 * Button hints in each device's own names, one player alone (run on the title): the mode select, character select, the
	 * カラーエディット and the settings, each as if the keyboard, an Xbox, a PlayStation and a Switch controller had been used
	 * last. Screenshots Saved/HintQA/<screen>_<device>.png. Also: on a controller the mouse cursor goes; on the keyboard it
	 * is back (menus).
	 */
	class FHintShotsCommand : public IAutomationLatentCommand
	{
	public:
		explicit FHintShotsCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual ~FHintShotsCommand() override
		{
			ChaosImpactHints::SetLastDeviceForTests(EChaosImpactDevice::KeyboardMouse);
		}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - StartedAt > 150.0)
			{
				Test->AddError(FString::Printf(TEXT("Timed out at shot %d"), Shot));
				return true;
			}
			AChaosImpactPlayerController* PC = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld())
				{
					PC = PC ? PC : Cast<AChaosImpactPlayerController>(Context.World()->GetFirstPlayerController());
				}
			}
			if (!PC || !PC->GetMenuWidget() || Now < NextAt || Now - StartedAt < 4.0)
			{
				return false;
			}
			static const TCHAR* const ScreenNames[] = {TEXT("ModeSelect"), TEXT("CharacterSelect"), TEXT("ColourEdit"), TEXT("Settings")};
			static const EChaosImpactDevice Devices[] = {EChaosImpactDevice::KeyboardMouse, EChaosImpactDevice::Xbox,
				EChaosImpactDevice::PlayStation, EChaosImpactDevice::Switch};
			static const TCHAR* const DeviceNames[] = {TEXT("Keyboard"), TEXT("Xbox"), TEXT("PlayStation"), TEXT("Switch")};
			const int32 ScreenIndex = Shot / 4;
			const int32 DeviceIndex = Shot % 4;
			if (ScreenIndex >= UE_ARRAY_COUNT(ScreenNames))
			{
				return true;
			}
			if (!bTaken)
			{
				// The screen (once per screen) and the device, then a moment for the menus to repaint.
				if (DeviceIndex == 0)
				{
					PC->ShowMenuScreen(EChaosImpactScreen::ModeSelect);
					if (ScreenIndex == 1)
					{
						PC->BeginTrainingSetup();
						PC->PrepareTrainingControllerAssignment(1);
						PC->RegisterKeyboardMouseJoin();
						PC->ConfirmControllerAssignments();
					}
					else if (ScreenIndex == 2)
					{
						PC->OpenColourEdit(EChaosImpactScreen::ModeSelect);
					}
					else if (ScreenIndex == 3)
					{
						PC->OpenSettings();
					}
				}
				ChaosImpactHints::SetLastDeviceForTests(Devices[DeviceIndex]);
				bTaken = true;
				NextAt = Now + (DeviceIndex == 0 ? 1.5 : 0.6);
				return false;
			}
			const bool bPad = ChaosImpactHints::IsPad(Devices[DeviceIndex]);
			Test->TestEqual(FString::Printf(TEXT("%s on %s: the cursor %s"), ScreenNames[ScreenIndex], DeviceNames[DeviceIndex],
				bPad ? TEXT("goes") : TEXT("is there")), PC->bShowMouseCursor, !bPad);
			FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("HintQA"),
				FString::Printf(TEXT("%s_%s.png"), ScreenNames[ScreenIndex], DeviceNames[DeviceIndex])), true, false);
			++Shot;
			bTaken = false;
			NextAt = Now + 0.4;
			return false;
		}

	private:
		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Shot = 0;
		bool bTaken = false;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactHintShotsTest, "ChaosImpact.UI.HintShots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactHintShotsTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FHintShotsCommand(this));
	return true;
}

#endif
