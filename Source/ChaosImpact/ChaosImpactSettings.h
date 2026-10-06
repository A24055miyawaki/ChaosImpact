#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

class APlayerController;
class UObject;
struct FKeyEvent;

/** Every control a player can set themselves: two keys or buttons each, kept separately for every device. */
enum class EChaosImpactAction : uint8
{
	MoveUp,
	MoveDown,
	MoveLeft,
	MoveRight,
	Throw,
	CancelThrow,
	Dash,
	Jump,
	SwapBall,
	DropBall,
	Pause,
	TrainingMenu,
	LobbyReady,
	LobbySpectate,
	Count
};

/** The devices controls are kept for: the keyboard and mouse, and each controller layout. */
enum class EChaosImpactDevice : uint8
{
	KeyboardMouse,
	Xbox,
	PlayStation,
	Switch,
	Count
};

/** Which controller layout a nickname plays with; Auto follows the controller actually connected. */
enum class EChaosImpactPadChoice : uint8
{
	Auto,
	Xbox,
	PlayStation,
	Switch,
	Count
};

/** One nickname's controls ("ゲスト" is the first, always there, for players who have not picked one). */
struct FChaosImpactProfile
{
	static constexpr int32 DeviceCount = static_cast<int32>(EChaosImpactDevice::Count);
	static constexpr int32 ActionCount = static_cast<int32>(EChaosImpactAction::Count);
	static constexpr int32 SlotCount = 2;

	FString Name;
	FKey Keys[DeviceCount][ActionCount][SlotCount];
	EChaosImpactPadChoice Pad = EChaosImpactPadChoice::Auto;
	/** Controller: the left stick aims and the right stick moves. */
	bool bSwapSticks = false;
	/** Controller: throws are pulled a little toward someone just off the aim. */
	bool bAimAssist = true;
};

/**
 * The player's settings, saved with the user settings (GameUserSettings.ini): nicknames and their controls, and the
 * sound, rumble, shake, brightness and Switch-button options. Screen and picture settings live in Unreal's own
 * UGameUserSettings; the helpers here change and save those too, so the settings screen has one place to call.
 */
namespace ChaosImpactSettings
{
	constexpr int32 MaxProfiles = 12;
	constexpr int32 MaxNameLength = 8;
	CHAOSIMPACT_API const FString& GetGuestName();

	// --- Nicknames (profiles) ---
	CHAOSIMPACT_API int32 GetProfileCount();
	CHAOSIMPACT_API const FChaosImpactProfile& GetProfile(int32 Index);
	/** INDEX_NONE when there is no such nickname. */
	CHAOSIMPACT_API int32 FindProfile(const FString& Name);
	/** A new nickname with the default controls (or the one already called that). INDEX_NONE when full or empty. */
	CHAOSIMPACT_API int32 AddProfile(const FString& Name);
	/** False for the guest, an empty name or one already taken. */
	CHAOSIMPACT_API bool RenameProfile(int32 Index, const FString& NewName);
	CHAOSIMPACT_API bool RemoveProfile(int32 Index);
	CHAOSIMPACT_API void SetPadChoice(int32 Profile, EChaosImpactPadChoice Choice);
	CHAOSIMPACT_API void SetSwapSticks(int32 Profile, bool bSwap);
	CHAOSIMPACT_API void SetAimAssist(int32 Profile, bool bOn);

	// --- Controls ---
	CHAOSIMPACT_API FKey GetKey(int32 Profile, EChaosImpactDevice Device, EChaosImpactAction Action, int32 Slot);
	/** Sets one slot; the same key anywhere else on that device is taken off there, so one key does one thing. */
	CHAOSIMPACT_API void SetKey(int32 Profile, EChaosImpactDevice Device, EChaosImpactAction Action, int32 Slot, const FKey& Key);
	CHAOSIMPACT_API void ResetDevice(int32 Profile, EChaosImpactDevice Device);
	CHAOSIMPACT_API FKey GetDefaultKey(EChaosImpactDevice Device, EChaosImpactAction Action, int32 Slot);
	/** Whether a key can be set for that device (Escape is kept for leaving menus). */
	CHAOSIMPACT_API bool CanBind(const FKey& Key, EChaosImpactDevice Device);
	CHAOSIMPACT_API FString GetActionName(EChaosImpactAction Action);
	CHAOSIMPACT_API FString GetDeviceName(EChaosImpactDevice Device);
	CHAOSIMPACT_API FString GetPadChoiceName(EChaosImpactPadChoice Choice);
	/** A key or button as that device names it (A / × / B for the same button on Xbox, PlayStation and Switch). */
	CHAOSIMPACT_API FString GetKeyName(const FKey& Key, EChaosImpactDevice Device);

	// --- What a local player plays with ---
	/** The nickname this local player picked at character select (the guest when none). */
	CHAOSIMPACT_API int32 GetPlayerProfile(const APlayerController* Player);
	/** The same, looked up now (GetPlayerProfile keeps it for the rest of the frame). */
	CHAOSIMPACT_API int32 FindPlayerProfile(const APlayerController* Player);
	/** A nickname or a player's pick changed: what GetPlayerProfile keeps for the frame is looked up again. */
	CHAOSIMPACT_API void MarkPlayersChanged();
	/** Keyboard and mouse, or the layout of the controller they hold. */
	CHAOSIMPACT_API EChaosImpactDevice GetPlayerDevice(const APlayerController* Player);
	/** The layout of the controller a player holds, as far as it can be told (XInput pads count as Xbox). */
	CHAOSIMPACT_API EChaosImpactDevice DetectPadDevice(const APlayerController* Player);
	/** The layout of the controller behind an input device (a menu press), or Xbox when it cannot be told. */
	CHAOSIMPACT_API EChaosImpactDevice DetectPadDeviceForInput(int32 InputDeviceId);
	CHAOSIMPACT_API bool IsActionDown(const APlayerController* Player, EChaosImpactAction Action);
	CHAOSIMPACT_API bool WasActionJustPressed(const APlayerController* Player, EChaosImpactAction Action);
	CHAOSIMPACT_API bool IsActionKey(const APlayerController* Player, EChaosImpactAction Action, const FKey& Key);
	/**
	 * Movement as (right, forward): the set keys, or the movement stick's raw axes plus any set buttons. bOutStick is
	 * true when a stick is in it (so the caller applies its dead zone).
	 */
	CHAOSIMPACT_API FVector2D GetMoveInput(const APlayerController* Player, bool& bOutStick);
	/** The aiming stick's raw axes (the right stick, or the left with the sticks swapped). */
	CHAOSIMPACT_API FVector2D GetAimStick(const APlayerController* Player);
	CHAOSIMPACT_API bool IsAimAssistOn(const APlayerController* Player);
	/** The names of the keys set for an action for this player, for on-screen hints ("G / 左クリック"). */
	CHAOSIMPACT_API FString DescribeAction(const APlayerController* Player, EChaosImpactAction Action);

	/**
	 * Menus: a Switch controller's A (the right button) decides and B (the bottom one) goes back, as on the console,
	 * when that option is on. Returns the key the menus should treat the press as.
	 */
	CHAOSIMPACT_API FKey ToMenuKey(const FKey& Key, int32 InputDeviceId);

	// --- Sound, rumble and the rest ---
	CHAOSIMPACT_API int32 GetMasterVolume();
	CHAOSIMPACT_API void SetMasterVolume(int32 Percent);
	/** 0 off, 1 weak, 2 normal. */
	CHAOSIMPACT_API int32 GetCameraShakeLevel();
	CHAOSIMPACT_API void SetCameraShakeLevel(int32 Level);
	CHAOSIMPACT_API float GetCameraShakeScale();
	CHAOSIMPACT_API bool IsSwitchConfirmRight();
	CHAOSIMPACT_API void SetSwitchConfirmRight(bool bRight);
	/** 1-10, 5 is the standard picture. */
	CHAOSIMPACT_API int32 GetBrightness();
	CHAOSIMPACT_API void SetBrightness(int32 Level);
	/** The sound volume and brightness, once at start (Unreal applies its own screen and picture settings). */
	CHAOSIMPACT_API void ApplyStartupSettings();

	// --- Screen and picture (UGameUserSettings) ---
	/** 0 fullscreen, 1 borderless (fullscreen window), 2 window. */
	CHAOSIMPACT_API int32 GetWindowMode();
	CHAOSIMPACT_API void SetWindowMode(int32 Mode);
	/** F11 / Alt+Enter: between a window and the last fullscreen kind, kept the same as the settings screen. */
	CHAOSIMPACT_API void ToggleFullscreen();
	/**
	 * In a window: the window's size (16:9 sizes that fit on the monitor). In fullscreen: how finely the game is drawn
	 * (720p up to 4K, or the monitor's own); the screen itself always stays at the monitor's pixels, so nothing is
	 * stretched.
	 */
	CHAOSIMPACT_API TArray<FIntPoint> GetResolutionChoices();
	CHAOSIMPACT_API FIntPoint GetResolution();
	CHAOSIMPACT_API void SetResolution(FIntPoint Resolution);
	/** The current pixels of the monitor the game's window is on. */
	CHAOSIMPACT_API FIntPoint GetMonitorResolution();
	/** Sets the screen percentage for the chosen fullscreen resolution (100% in a window). */
	CHAOSIMPACT_API void ApplyRenderScale();
	CHAOSIMPACT_API bool IsVSyncOn();
	CHAOSIMPACT_API void SetVSync(bool bOn);
	/** 0 when unlimited. */
	CHAOSIMPACT_API int32 GetFrameRateLimit();
	CHAOSIMPACT_API void SetFrameRateLimit(int32 Limit);
	/** 0-3 (low to epic), or -1 when the groups are set differently (custom). */
	CHAOSIMPACT_API int32 GetOverallQuality();
	CHAOSIMPACT_API void SetOverallQuality(int32 Level);
	/** Picture groups: 0 view distance, 1 anti-aliasing, 2 shadows, 3 lighting, 4 reflections, 5 post process, 6 textures, 7 effects, 8 foliage, 9 shading. */
	constexpr int32 QualityGroupCount = 10;
	CHAOSIMPACT_API FString GetQualityGroupName(int32 Group);
	CHAOSIMPACT_API int32 GetQualityGroup(int32 Group);
	CHAOSIMPACT_API void SetQualityGroup(int32 Group, int32 Level);
	/** Rendering resolution as a percentage of the screen's (50-100). */
	CHAOSIMPACT_API int32 GetResolutionScale();
	CHAOSIMPACT_API void SetResolutionScale(int32 Percent);
	/** Whether window and resolution changes are applied now (not in the editor, where they would resize it). */
	CHAOSIMPACT_API bool CanApplyWindowChanges();
}
