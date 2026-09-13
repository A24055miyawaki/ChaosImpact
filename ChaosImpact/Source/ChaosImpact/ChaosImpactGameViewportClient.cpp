#include "ChaosImpactGameViewportClient.h"

#include "ChaosImpact.h"
#include "ChaosImpactPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "InputKeyEventArgs.h"

bool UChaosImpactGameViewportClient::InputKey(const FInputKeyEventArgs& EventArgs)
{
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
