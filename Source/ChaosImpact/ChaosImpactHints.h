#pragma once

#include "CoreMinimal.h"
#include "ChaosImpactSettings.h"

class APlayerController;
struct FKey;

/**
 * On-screen button hints in the notation of what is being played with: the keyboard and mouse, an Xbox, a PlayStation or a
 * Switch controller (never "A / Enter" for everyone). The machine's last press is watched (which kind of controller it came
 * from too), so a player alone on the machine sees the names of whatever they last used; several players on one machine
 * each see their own device's.
 *
 * Hint lines are written as templates; Text() puts in the names:
 *   {Confirm} {Back}       the menus' decide / back (Enter / Esc on the keyboard; A / B, × / ○, and on a Switch the right
 *                          button decides when that option is on)
 *   {A} {B} {X} {Y}        the controller's bottom / right / left / top face buttons (by where they are, in Xbox's names)
 *   {LB} {RB} {LT} {RT} {L3} {R3} {Start} {Select}
 *   {DPad} {LStick} {RStick}
 *   {Up} {Down} {Left} {Right}   the d-pad's directions (十字キー↑ / 方向キー↑ / 十字ボタン↑)
 *   {Name|Keyboard}        the controller's name on a controller, "Keyboard" on the keyboard: {LB|Q}, {Y|N}, {DPad|矢印}
 *                          (without a bar the keyboard shows: {Confirm} Enter, {Back} Esc, {DPad} 矢印キー, {LStick} WASD,
 *                          {RStick} マウス, {Up}↑ {Down}↓ {Left}← {Right}→, {Start} Esc; anything else nothing)
 *   {pad:...} {kb:...}     only on a controller / only on the keyboard (may hold names: {pad:{LB} / {RB}：切りかえ　})
 */
namespace ChaosImpactHints
{
	/**
	 * What this player is playing with, for hints. Alone on the machine: what was pressed last (keyboard and mouse, or a
	 * controller of the kind pressed, or the nickname's chosen kind). Several on one machine: their own assigned device.
	 * No player: the machine's last press.
	 */
	CHAOSIMPACT_API EChaosImpactDevice GetDevice(const APlayerController* Player);
	/** The machine's last press: keyboard and mouse, or the kind of controller it came from. */
	CHAOSIMPACT_API EChaosImpactDevice GetLastDevice();
	inline bool IsPad(const EChaosImpactDevice Device) { return Device != EChaosImpactDevice::KeyboardMouse; }

	/** The menus' decide and back buttons' names for a device. */
	CHAOSIMPACT_API FString Confirm(EChaosImpactDevice Device);
	CHAOSIMPACT_API FString Back(EChaosImpactDevice Device);
	/** The menus' decide and back keys on a device (a Switch's swap included), e.g. for drawing them in a key cap. */
	CHAOSIMPACT_API FKey ConfirmKey(EChaosImpactDevice Device);
	CHAOSIMPACT_API FKey BackKey(EChaosImpactDevice Device);
	/** A controller button's name on a controller, KeyboardText on the keyboard. */
	CHAOSIMPACT_API FString Key(EChaosImpactDevice Device, const FKey& PadKey, const FString& KeyboardText);

	/** A hint line from a template (above) for a device, or for a player's device. */
	CHAOSIMPACT_API FString Text(EChaosImpactDevice Device, const FString& Template);
	CHAOSIMPACT_API FString Text(const APlayerController* Player, const FString& Template);

	/** Starts watching the machine's presses (done by itself on first use; safe to call again). */
	CHAOSIMPACT_API void StartTracking();
	/** Tests: as if this were pressed last. */
	CHAOSIMPACT_API void SetLastDeviceForTests(EChaosImpactDevice Device);
}
