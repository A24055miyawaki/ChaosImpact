#include "ChaosImpactGameViewportClient.h"

#include "ChaosImpact.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactGameState.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "ChaosImpactSettings.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "InputKeyEventArgs.h"

TFunction<bool(const FInputKeyEventArgs&)> UChaosImpactGameViewportClient::UnownedGamepadHandler;

ULocalPlayer* UChaosImpactGameViewportClient::GetSingleDuelPlayer(const UWorld* World)
{
	const auto* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
	const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	if (!Match || !Match->bOnlineRoom || !Match->IsDuel() || !GI || GI->GetNumLocalPlayers() != 2) { return nullptr; }
	ULocalPlayer* Fighter = nullptr;
	int32 Playing = 0, Watching = 0;
	for (ULocalPlayer* Local : GI->GetLocalPlayers())
	{
		const auto* PC = Local ? Local->GetPlayerController(World) : nullptr;
		const auto* Member = PC ? PC->GetPlayerState<AChaosImpactPlayerState>() : nullptr;
		if (!Member) { return nullptr; }
		if (Member->bSpectating) { ++Watching; }
		else { ++Playing; Fighter = Local; }
	}
	return Playing == 1 && Watching == 1 ? Fighter : nullptr;
}

void UChaosImpactGameViewportClient::LayoutPlayers()
{
	Super::LayoutPlayers();
	if (ULocalPlayer* Fighter = GetSingleDuelPlayer(GetWorld()))
	{
		// Preserve both network connections, input assignments and spectator state; only change rendering.
		for (ULocalPlayer* Local : GetWorld()->GetGameInstance()->GetLocalPlayers())
		{
			Local->Origin = FVector2D::ZeroVector;
			Local->Size = Local == Fighter ? FVector2D(1., 1.) : FVector2D::ZeroVector;
		}
	}
}

void UChaosImpactGameViewportClient::Tick(const float DeltaTime)
{
	Super::Tick(DeltaTime);
	const bool bSingleNow = GetSingleDuelPlayer(GetWorld()) != nullptr;
	// UE normally relayouts only when players join/leave or split settings change. Spectating keeps the same
	// LocalPlayers, so explicitly update through replication/phase changes and restore the usual layout on exit.
	if (bSingleNow || bSingleDuelLayout) { LayoutPlayers(); }
	bSingleDuelLayout = bSingleNow;
	// Online rooms: everyone at no more than 60 fps (the player's own limit comes back after).
	ChaosImpactSettings::UpdateOnlineFrameCap(GetWorld());
}

bool UChaosImpactGameViewportClient::InputKey(const FInputKeyEventArgs& EventArgs)
{
	// F11 and Alt+Enter go through the settings, so the settings screen shows (and saves) the same window mode.
	// The editor keeps F11 for its own use.
	if (!GIsEditor && (EventArgs.Event == IE_Pressed || EventArgs.Event == IE_Repeat)
		&& (EventArgs.Key == EKeys::F11 || (EventArgs.Key == EKeys::Enter && FSlateApplication::IsInitialized()
			&& FSlateApplication::Get().GetModifierKeys().IsAltDown())))
	{
		if (EventArgs.Event == IE_Pressed)
		{
			ChaosImpactSettings::ToggleFullscreen();
		}
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
		if (UnownedGamepadHandler && UnownedGamepadHandler(EventArgs))
		{
			return true;
		}
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
