#include "ChaosImpactGameViewportClient.h"

#include "ChaosImpact.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactSettings.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "InputKeyEventArgs.h"

bool UChaosImpactGameViewportClient::InputKey(const FInputKeyEventArgs& EventArgs)
{
	// F11 and Alt+Enter go through the settings, so the settings screen shows (and saves) the same window mode.
	// The editor keeps F11 for its own use.
	if (!GIsEditor && EventArgs.Event == IE_Pressed
		&& (EventArgs.Key == EKeys::F11 || (EventArgs.Key == EKeys::Enter && FSlateApplication::IsInitialized()
			&& FSlateApplication::Get().GetModifierKeys().IsAltDown())))
	{
		ChaosImpactSettings::ToggleFullscreen();
		return true;
	}
	// The normal viewport route only sends a pad to a LocalPlayer that already owns it.
	// The assignment screen needs to see every physical pad first, just like a console
	// controller-order screen.
	if (EventArgs.Event == IE_Pressed && EventArgs.Key.IsGamepadKey() && GetWorld())
	{
		if (AChaosImpactPlayerController* Primary =
			Cast<AChaosImpactPlayerController>(GetWorld()->GetFirstPlayerController()))
		{
			const bool bWasJoined = Primary->IsControllerJoined(EventArgs.InputDevice.GetId());
			if (Primary->RegisterControllerJoin(
				EventArgs.InputDevice.GetId(), EventArgs.ControllerId)
				&& !bWasJoined)
			{
				return true;
			}
		}
	}
	if (EventArgs.Event == IE_Pressed && EventArgs.Key.IsGamepadKey() && GEngine && GetWorld()
		&& !GEngine->GetLocalPlayerFromInputDevice(this, EventArgs.InputDevice))
	{
		const AChaosImpactPlayerController* Primary =
			Cast<AChaosImpactPlayerController>(GetWorld()->GetFirstPlayerController());
		if (Primary && Primary->IsGameplayActive())
		{
			UE_LOG(LogChaosImpact, Warning,
				TEXT("Gamepad input device %d (platform user %d) is not owned by any local player; %s ignored."),
				EventArgs.InputDevice.GetId(),
				IPlatformInputDeviceMapper::Get().GetUserForInputDevice(EventArgs.InputDevice).GetInternalId(),
				*EventArgs.Key.ToString());
		}
	}
	return Super::InputKey(EventArgs);
}
