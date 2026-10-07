#include "ChaosImpactSettings.h"

#include "AudioDevice.h"
#include "ChaosImpactLoadoutSubsystem.h"
#include "ChaosImpactPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/PlayerController.h"
#include "JoyShockBlueprintLibrary.h"
#include "JoyShockTypes.h"
#include "Engine/GameViewportClient.h"
#include "GenericPlatform/GenericApplication.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Widgets/SWindow.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"

namespace
{
	using EAction = EChaosImpactAction;
	using EDevice = EChaosImpactDevice;

	const TCHAR* const SettingsSection = TEXT("ChaosImpact.Settings");

	struct FSettingsStore
	{
		TArray<FChaosImpactProfile> Profiles;
		int32 Volume = 100;
		int32 Shake = 2;
		int32 Brightness = 5;
		bool bSwitchConfirmRight = true;
		/** The fullscreen kind F11 goes back to (0 fullscreen, 1 borderless). */
		int32 LastFullscreenMode = 1;
		FIntPoint WindowedResolution = FIntPoint::ZeroValue;
		FIntPoint FullscreenResolution = FIntPoint::ZeroValue;
		bool bLoaded = false;
	};

	FSettingsStore& Store();
	/** Goes up whenever a nickname or a player's pick changes. */
	uint32 PlayersRevision = 0;

	const TCHAR* DeviceTag(const EDevice Device)
	{
		static const TCHAR* const Tags[] = {TEXT("KBM"), TEXT("Xbox"), TEXT("PS"), TEXT("Switch")};
		return Tags[FMath::Clamp(static_cast<int32>(Device), 0, 3)];
	}

	const TCHAR* ActionTag(const EAction Action)
	{
		static const TCHAR* const Tags[] = {TEXT("MoveUp"), TEXT("MoveDown"), TEXT("MoveLeft"), TEXT("MoveRight"),
			TEXT("Throw"), TEXT("CancelThrow"), TEXT("Dash"), TEXT("Jump"), TEXT("SwapBall"), TEXT("DropBall"),
			TEXT("Pause"), TEXT("TrainingMenu"), TEXT("LobbyReady"), TEXT("LobbySpectate")};
		static_assert(UE_ARRAY_COUNT(Tags) == FChaosImpactProfile::ActionCount, "One tag per action");
		return Tags[FMath::Clamp(static_cast<int32>(Action), 0, FChaosImpactProfile::ActionCount - 1)];
	}

	FString ProfileSection(const int32 Index)
	{
		return FString::Printf(TEXT("ChaosImpact.Profile%d"), Index);
	}

	void FillDefaults(FChaosImpactProfile& Profile)
	{
		for (int32 Device = 0; Device < FChaosImpactProfile::DeviceCount; ++Device)
		{
			for (int32 Action = 0; Action < FChaosImpactProfile::ActionCount; ++Action)
			{
				for (int32 Slot = 0; Slot < FChaosImpactProfile::SlotCount; ++Slot)
				{
					Profile.Keys[Device][Action][Slot] = ChaosImpactSettings::GetDefaultKey(
						static_cast<EDevice>(Device), static_cast<EAction>(Action), Slot);
				}
			}
		}
	}

	void SaveProfile(const int32 Index)
	{
		FSettingsStore& S = Store();
		if (!GConfig || !S.Profiles.IsValidIndex(Index))
		{
			return;
		}
		const FChaosImpactProfile& Profile = S.Profiles[Index];
		const FString Section = ProfileSection(Index);
		GConfig->EmptySection(*Section, GGameUserSettingsIni);
		GConfig->SetString(*Section, TEXT("Name"), *Profile.Name, GGameUserSettingsIni);
		GConfig->SetInt(*Section, TEXT("Pad"), static_cast<int32>(Profile.Pad), GGameUserSettingsIni);
		GConfig->SetBool(*Section, TEXT("SwapSticks"), Profile.bSwapSticks, GGameUserSettingsIni);
		GConfig->SetBool(*Section, TEXT("AimAssist"), Profile.bAimAssist, GGameUserSettingsIni);
		for (int32 Device = 0; Device < FChaosImpactProfile::DeviceCount; ++Device)
		{
			for (int32 Action = 0; Action < FChaosImpactProfile::ActionCount; ++Action)
			{
				const FKey* Keys = Profile.Keys[Device][Action];
				const FString Value = FString::Printf(TEXT("%s,%s"),
					Keys[0].IsValid() ? *Keys[0].GetFName().ToString() : TEXT("None"),
					Keys[1].IsValid() ? *Keys[1].GetFName().ToString() : TEXT("None"));
				GConfig->SetString(*Section, *FString::Printf(TEXT("%s_%s"), DeviceTag(static_cast<EDevice>(Device)),
					ActionTag(static_cast<EAction>(Action))), *Value, GGameUserSettingsIni);
			}
		}
	}

	void SaveOptions()
	{
		FSettingsStore& S = Store();
		if (!GConfig)
		{
			return;
		}
		GConfig->SetInt(SettingsSection, TEXT("Volume"), S.Volume, GGameUserSettingsIni);
		GConfig->SetInt(SettingsSection, TEXT("CameraShake"), S.Shake, GGameUserSettingsIni);
		GConfig->SetInt(SettingsSection, TEXT("Brightness"), S.Brightness, GGameUserSettingsIni);
		GConfig->SetBool(SettingsSection, TEXT("SwitchConfirmRight"), S.bSwitchConfirmRight, GGameUserSettingsIni);
		GConfig->SetInt(SettingsSection, TEXT("LastFullscreenMode"), S.LastFullscreenMode, GGameUserSettingsIni);
		GConfig->SetInt(SettingsSection, TEXT("WindowedResX"), S.WindowedResolution.X, GGameUserSettingsIni);
		GConfig->SetInt(SettingsSection, TEXT("WindowedResY"), S.WindowedResolution.Y, GGameUserSettingsIni);
		GConfig->SetInt(SettingsSection, TEXT("FullscreenResX"), S.FullscreenResolution.X, GGameUserSettingsIni);
		GConfig->SetInt(SettingsSection, TEXT("FullscreenResY"), S.FullscreenResolution.Y, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}

	void SaveAllProfiles()
	{
		FSettingsStore& S = Store();
		if (!GConfig)
		{
			return;
		}
		GConfig->SetInt(SettingsSection, TEXT("ProfileCount"), S.Profiles.Num(), GGameUserSettingsIni);
		for (int32 Index = 0; Index < S.Profiles.Num(); ++Index)
		{
			SaveProfile(Index);
		}
		// A nickname taken off leaves no section behind.
		for (int32 Index = S.Profiles.Num(); Index < ChaosImpactSettings::MaxProfiles + 1; ++Index)
		{
			GConfig->EmptySection(*ProfileSection(Index), GGameUserSettingsIni);
		}
		GConfig->Flush(false, GGameUserSettingsIni);
	}

	void Load(FSettingsStore& S)
	{
		S.bLoaded = true;
		int32 Count = 0;
		if (GConfig)
		{
			GConfig->GetInt(SettingsSection, TEXT("Volume"), S.Volume, GGameUserSettingsIni);
			GConfig->GetInt(SettingsSection, TEXT("CameraShake"), S.Shake, GGameUserSettingsIni);
			GConfig->GetInt(SettingsSection, TEXT("Brightness"), S.Brightness, GGameUserSettingsIni);
			GConfig->GetBool(SettingsSection, TEXT("SwitchConfirmRight"), S.bSwitchConfirmRight, GGameUserSettingsIni);
			GConfig->GetInt(SettingsSection, TEXT("LastFullscreenMode"), S.LastFullscreenMode, GGameUserSettingsIni);
			GConfig->GetInt(SettingsSection, TEXT("WindowedResX"), S.WindowedResolution.X, GGameUserSettingsIni);
			GConfig->GetInt(SettingsSection, TEXT("WindowedResY"), S.WindowedResolution.Y, GGameUserSettingsIni);
			GConfig->GetInt(SettingsSection, TEXT("FullscreenResX"), S.FullscreenResolution.X, GGameUserSettingsIni);
			GConfig->GetInt(SettingsSection, TEXT("FullscreenResY"), S.FullscreenResolution.Y, GGameUserSettingsIni);
			GConfig->GetInt(SettingsSection, TEXT("ProfileCount"), Count, GGameUserSettingsIni);
		}
		S.Volume = FMath::Clamp(S.Volume, 0, 100);
		S.Shake = FMath::Clamp(S.Shake, 0, 2);
		S.Brightness = FMath::Clamp(S.Brightness, 1, 10);
		S.LastFullscreenMode = FMath::Clamp(S.LastFullscreenMode, 0, 1);
		Count = FMath::Clamp(Count, 0, ChaosImpactSettings::MaxProfiles);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			FChaosImpactProfile Profile;
			FillDefaults(Profile);
			const FString Section = ProfileSection(Index);
			GConfig->GetString(*Section, TEXT("Name"), Profile.Name, GGameUserSettingsIni);
			int32 Pad = 0;
			GConfig->GetInt(*Section, TEXT("Pad"), Pad, GGameUserSettingsIni);
			Profile.Pad = static_cast<EChaosImpactPadChoice>(FMath::Clamp(Pad, 0, static_cast<int32>(EChaosImpactPadChoice::Count) - 1));
			GConfig->GetBool(*Section, TEXT("SwapSticks"), Profile.bSwapSticks, GGameUserSettingsIni);
			GConfig->GetBool(*Section, TEXT("AimAssist"), Profile.bAimAssist, GGameUserSettingsIni);
			for (int32 Device = 0; Device < FChaosImpactProfile::DeviceCount; ++Device)
			{
				for (int32 Action = 0; Action < FChaosImpactProfile::ActionCount; ++Action)
				{
					FString Value;
					if (!GConfig->GetString(*Section, *FString::Printf(TEXT("%s_%s"), DeviceTag(static_cast<EDevice>(Device)),
						ActionTag(static_cast<EAction>(Action))), Value, GGameUserSettingsIni))
					{
						continue;
					}
					FString First;
					FString Second;
					if (!Value.Split(TEXT(","), &First, &Second))
					{
						First = Value;
					}
					const FKey Keys[] = {FKey(*First), FKey(*Second)};
					for (int32 Slot = 0; Slot < FChaosImpactProfile::SlotCount; ++Slot)
					{
						Profile.Keys[Device][Action][Slot] = Keys[Slot].IsValid() ? Keys[Slot] : FKey();
					}
				}
			}
			Profile.Name = Profile.Name.Left(ChaosImpactSettings::MaxNameLength).TrimStartAndEnd();
			if (Index == 0)
			{
				Profile.Name = ChaosImpactSettings::GetGuestName();
			}
			if (!Profile.Name.IsEmpty() && !S.Profiles.ContainsByPredicate(
				[&Profile](const FChaosImpactProfile& Other) { return Other.Name == Profile.Name; }))
			{
				S.Profiles.Add(Profile);
			}
		}
		if (S.Profiles.IsEmpty())
		{
			FChaosImpactProfile Guest;
			FillDefaults(Guest);
			Guest.Name = ChaosImpactSettings::GetGuestName();
			S.Profiles.Add(Guest);
		}
	}

	FSettingsStore& Store()
	{
		static FSettingsStore Settings;
		if (!Settings.bLoaded)
		{
			Load(Settings);
		}
		return Settings;
	}

	UGameUserSettings* UserSettings()
	{
		return GEngine ? GEngine->GetGameUserSettings() : nullptr;
	}

	void SaveUserSettings(UGameUserSettings* Settings, const bool bResolution)
	{
		if (!Settings)
		{
			return;
		}
		if (bResolution)
		{
			if (ChaosImpactSettings::CanApplyWindowChanges())
			{
				Settings->ApplyResolutionSettings(false);
			}
		}
		else
		{
			Settings->ApplyNonResolutionSettings();
		}
		Settings->SaveSettings();
	}

	EDevice PadDeviceFor(const EJSL4UControllerType Type)
	{
		switch (Type)
		{
		case EJSL4UControllerType::DualShock4:
		case EJSL4UControllerType::DualSense:
			return EDevice::PlayStation;
		case EJSL4UControllerType::ProController:
		case EJSL4UControllerType::ProController2:
		case EJSL4UControllerType::JoyConLeft:
		case EJSL4UControllerType::JoyConRight:
		case EJSL4UControllerType::JoyCon2Left:
		case EJSL4UControllerType::JoyCon2Right:
			return EDevice::Switch;
		default:
			return EDevice::Xbox;
		}
	}

	FVector2D ReadDigitalMove(const APlayerController* Player)
	{
		using namespace ChaosImpactSettings;
		const auto Held = [Player](const EAction Action) { return IsActionDown(Player, Action) ? 1.0f : 0.0f; };
		return FVector2D(Held(EAction::MoveRight) - Held(EAction::MoveLeft), Held(EAction::MoveUp) - Held(EAction::MoveDown));
	}

	const FKey* const PadButtons[] = {
		&EKeys::Gamepad_FaceButton_Bottom, &EKeys::Gamepad_FaceButton_Right, &EKeys::Gamepad_FaceButton_Left,
		&EKeys::Gamepad_FaceButton_Top, &EKeys::Gamepad_LeftShoulder, &EKeys::Gamepad_RightShoulder,
		&EKeys::Gamepad_LeftTrigger, &EKeys::Gamepad_RightTrigger, &EKeys::Gamepad_LeftThumbstick,
		&EKeys::Gamepad_RightThumbstick, &EKeys::Gamepad_DPad_Up, &EKeys::Gamepad_DPad_Down, &EKeys::Gamepad_DPad_Left,
		&EKeys::Gamepad_DPad_Right, &EKeys::Gamepad_Special_Left, &EKeys::Gamepad_Special_Right};
}

const FString& ChaosImpactSettings::GetGuestName()
{
	static const FString Guest(TEXT("ゲスト"));
	return Guest;
}

int32 ChaosImpactSettings::GetProfileCount()
{
	return Store().Profiles.Num();
}

const FChaosImpactProfile& ChaosImpactSettings::GetProfile(const int32 Index)
{
	const FSettingsStore& S = Store();
	return S.Profiles[S.Profiles.IsValidIndex(Index) ? Index : 0];
}

int32 ChaosImpactSettings::FindProfile(const FString& Name)
{
	const FString Wanted = Name.TrimStartAndEnd();
	return Wanted.IsEmpty() ? INDEX_NONE
		: Store().Profiles.IndexOfByPredicate([&Wanted](const FChaosImpactProfile& Profile) { return Profile.Name == Wanted; });
}

int32 ChaosImpactSettings::AddProfile(const FString& Name)
{
	MarkPlayersChanged();
	const FString Clean = Name.TrimStartAndEnd().Left(MaxNameLength);
	if (Clean.IsEmpty())
	{
		return INDEX_NONE;
	}
	if (const int32 Existing = FindProfile(Clean); Existing != INDEX_NONE)
	{
		return Existing;
	}
	FSettingsStore& S = Store();
	if (S.Profiles.Num() >= MaxProfiles)
	{
		return INDEX_NONE;
	}
	FChaosImpactProfile Profile;
	FillDefaults(Profile);
	Profile.Name = Clean;
	const int32 Index = S.Profiles.Add(Profile);
	SaveAllProfiles();
	return Index;
}

bool ChaosImpactSettings::RenameProfile(const int32 Index, const FString& NewName)
{
	MarkPlayersChanged();
	FSettingsStore& S = Store();
	const FString Clean = NewName.TrimStartAndEnd().Left(MaxNameLength);
	if (Index <= 0 || !S.Profiles.IsValidIndex(Index) || Clean.IsEmpty() || FindProfile(Clean) != INDEX_NONE)
	{
		return false;
	}
	S.Profiles[Index].Name = Clean;
	SaveProfile(Index);
	GConfig->Flush(false, GGameUserSettingsIni);
	return true;
}

bool ChaosImpactSettings::RemoveProfile(const int32 Index)
{
	MarkPlayersChanged();
	FSettingsStore& S = Store();
	if (Index <= 0 || !S.Profiles.IsValidIndex(Index))
	{
		return false;
	}
	S.Profiles.RemoveAt(Index);
	SaveAllProfiles();
	return true;
}

void ChaosImpactSettings::SetPadChoice(const int32 Profile, const EChaosImpactPadChoice Choice)
{
	FSettingsStore& S = Store();
	if (S.Profiles.IsValidIndex(Profile))
	{
		S.Profiles[Profile].Pad = Choice;
		SaveProfile(Profile);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

void ChaosImpactSettings::SetSwapSticks(const int32 Profile, const bool bSwap)
{
	FSettingsStore& S = Store();
	if (S.Profiles.IsValidIndex(Profile))
	{
		S.Profiles[Profile].bSwapSticks = bSwap;
		SaveProfile(Profile);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

void ChaosImpactSettings::SetAimAssist(const int32 Profile, const bool bOn)
{
	FSettingsStore& S = Store();
	if (S.Profiles.IsValidIndex(Profile))
	{
		S.Profiles[Profile].bAimAssist = bOn;
		SaveProfile(Profile);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

FKey ChaosImpactSettings::GetKey(const int32 Profile, const EChaosImpactDevice Device, const EChaosImpactAction Action,
	const int32 Slot)
{
	const int32 D = static_cast<int32>(Device);
	const int32 A = static_cast<int32>(Action);
	if (D < 0 || D >= FChaosImpactProfile::DeviceCount || A < 0 || A >= FChaosImpactProfile::ActionCount
		|| Slot < 0 || Slot >= FChaosImpactProfile::SlotCount)
	{
		return FKey();
	}
	return GetProfile(Profile).Keys[D][A][Slot];
}

void ChaosImpactSettings::SetKey(const int32 Profile, const EChaosImpactDevice Device, const EChaosImpactAction Action,
	const int32 Slot, const FKey& Key)
{
	FSettingsStore& S = Store();
	const int32 D = static_cast<int32>(Device);
	const int32 A = static_cast<int32>(Action);
	if (!S.Profiles.IsValidIndex(Profile) || D < 0 || D >= FChaosImpactProfile::DeviceCount || A < 0
		|| A >= FChaosImpactProfile::ActionCount || Slot < 0 || Slot >= FChaosImpactProfile::SlotCount)
	{
		return;
	}
	FChaosImpactProfile& Edited = S.Profiles[Profile];
	if (Key.IsValid())
	{
		// One key does one thing: wherever else it was on this device, it comes off there.
		for (int32 Other = 0; Other < FChaosImpactProfile::ActionCount; ++Other)
		{
			for (int32 OtherSlot = 0; OtherSlot < FChaosImpactProfile::SlotCount; ++OtherSlot)
			{
				if (Edited.Keys[D][Other][OtherSlot] == Key)
				{
					Edited.Keys[D][Other][OtherSlot] = FKey();
				}
			}
		}
	}
	Edited.Keys[D][A][Slot] = Key;
	SaveProfile(Profile);
	GConfig->Flush(false, GGameUserSettingsIni);
}

void ChaosImpactSettings::ResetDevice(const int32 Profile, const EChaosImpactDevice Device)
{
	FSettingsStore& S = Store();
	const int32 D = static_cast<int32>(Device);
	if (!S.Profiles.IsValidIndex(Profile) || D < 0 || D >= FChaosImpactProfile::DeviceCount)
	{
		return;
	}
	for (int32 Action = 0; Action < FChaosImpactProfile::ActionCount; ++Action)
	{
		for (int32 Slot = 0; Slot < FChaosImpactProfile::SlotCount; ++Slot)
		{
			S.Profiles[Profile].Keys[D][Action][Slot] = GetDefaultKey(Device, static_cast<EAction>(Action), Slot);
		}
	}
	if (Device != EDevice::KeyboardMouse)
	{
		S.Profiles[Profile].bSwapSticks = false;
		S.Profiles[Profile].bAimAssist = true;
	}
	SaveProfile(Profile);
	GConfig->Flush(false, GGameUserSettingsIni);
}

FKey ChaosImpactSettings::GetDefaultKey(const EChaosImpactDevice Device, const EChaosImpactAction Action, const int32 Slot)
{
	if (Device == EDevice::KeyboardMouse)
	{
		switch (Action)
		{
		case EAction::MoveUp: return Slot == 0 ? EKeys::W : EKeys::Up;
		case EAction::MoveDown: return Slot == 0 ? EKeys::S : EKeys::Down;
		case EAction::MoveLeft: return Slot == 0 ? EKeys::A : EKeys::Left;
		case EAction::MoveRight: return Slot == 0 ? EKeys::D : EKeys::Right;
		case EAction::Throw: return Slot == 0 ? EKeys::LeftMouseButton : EKeys::G;
		case EAction::CancelThrow: return Slot == 0 ? EKeys::RightMouseButton : FKey();
		case EAction::Dash: return Slot == 0 ? EKeys::LeftShift : EKeys::RightShift;
		case EAction::Jump: return Slot == 0 ? EKeys::SpaceBar : FKey();
		case EAction::SwapBall: return Slot == 0 ? EKeys::Q : FKey();
		case EAction::DropBall: return Slot == 0 ? EKeys::F : FKey();
		case EAction::Pause: return Slot == 0 ? EKeys::P : FKey();
		case EAction::TrainingMenu: return Slot == 0 ? EKeys::T : EKeys::Hyphen;
		case EAction::LobbyReady: return Slot == 0 ? EKeys::R : FKey();
		case EAction::LobbySpectate: return Slot == 0 ? EKeys::V : FKey();
		default: return FKey();
		}
	}
	// Controllers by position, except that a Switch controller jumps with A and dashes with B as its labels say
	// (its A is the right button, where an Xbox pad has B).
	const bool bSwitch = Device == EDevice::Switch;
	switch (Action)
	{
	case EAction::Throw: return Slot == 0 ? EKeys::Gamepad_RightTrigger : EKeys::Gamepad_FaceButton_Left;
	case EAction::CancelThrow: return Slot == 0 ? EKeys::Gamepad_LeftTrigger : FKey();
	case EAction::Dash:
		return Slot == 0 ? (bSwitch ? EKeys::Gamepad_FaceButton_Bottom : EKeys::Gamepad_FaceButton_Right)
			: EKeys::Gamepad_RightShoulder;
	case EAction::Jump:
		return Slot == 0 ? (bSwitch ? EKeys::Gamepad_FaceButton_Right : EKeys::Gamepad_FaceButton_Bottom) : FKey();
	case EAction::SwapBall: return Slot == 0 ? EKeys::Gamepad_LeftShoulder : FKey();
	case EAction::DropBall: return Slot == 0 ? EKeys::Gamepad_DPad_Left : FKey();
	case EAction::Pause: return Slot == 0 ? EKeys::Gamepad_Special_Right : FKey();
	case EAction::TrainingMenu: return Slot == 0 ? EKeys::Gamepad_Special_Left : FKey();
	case EAction::LobbyReady: return Slot == 0 ? EKeys::Gamepad_DPad_Up : FKey();
	case EAction::LobbySpectate: return Slot == 0 ? EKeys::Gamepad_DPad_Down : FKey();
	default: return FKey();
	}
}

bool ChaosImpactSettings::CanBind(const FKey& Key, const EChaosImpactDevice Device)
{
	if (!Key.IsValid() || Key.IsAxis1D() || Key.IsAxis2D() || Key.IsAxis3D() || Key.IsTouch() || Key.IsGesture())
	{
		return false;
	}
	if (Device == EDevice::KeyboardMouse)
	{
		if (Key == EKeys::Escape || Key == EKeys::MouseScrollUp || Key == EKeys::MouseScrollDown
			|| Key == EKeys::MouseWheelAxis || Key.IsGamepadKey())
		{
			return false;
		}
		return true;
	}
	if (!Key.IsGamepadKey())
	{
		return false;
	}
	for (const FKey* Button : PadButtons)
	{
		if (*Button == Key)
		{
			return true;
		}
	}
	// The extra buttons some controllers have (the PS button, the touch pad, Switch 2's C and grips).
	const FString Name = Key.GetFName().ToString();
	return Name.StartsWith(TEXT("JoyShock")) && !Name.Contains(TEXT("Touched"));
}

FString ChaosImpactSettings::GetActionName(const EChaosImpactAction Action)
{
	switch (Action)
	{
	case EAction::MoveUp: return TEXT("移動（上）");
	case EAction::MoveDown: return TEXT("移動（下）");
	case EAction::MoveLeft: return TEXT("移動（左）");
	case EAction::MoveRight: return TEXT("移動（右）");
	case EAction::Throw: return TEXT("投げる（長押しでため）");
	case EAction::CancelThrow: return TEXT("投げをやめる");
	case EAction::Dash: return TEXT("ダッシュ");
	case EAction::Jump: return TEXT("ジャンプ");
	case EAction::SwapBall: return TEXT("ボール入れ替え");
	case EAction::DropBall: return TEXT("ボールを捨てる");
	case EAction::Pause: return TEXT("ポーズ");
	case EAction::TrainingMenu: return TEXT("トレーニングメニュー");
	case EAction::LobbyReady: return TEXT("準備OK（通信の部屋）");
	case EAction::LobbySpectate: return TEXT("観戦する／やめる（部屋）");
	default: return FString();
	}
}

FString ChaosImpactSettings::GetDeviceName(const EChaosImpactDevice Device)
{
	switch (Device)
	{
	case EDevice::KeyboardMouse: return TEXT("キーボード・マウス");
	case EDevice::Xbox: return TEXT("Xbox コントローラー");
	case EDevice::PlayStation: return TEXT("PlayStation コントローラー");
	case EDevice::Switch: return TEXT("Switch コントローラー");
	default: return FString();
	}
}

FString ChaosImpactSettings::GetPadChoiceName(const EChaosImpactPadChoice Choice)
{
	switch (Choice)
	{
	case EChaosImpactPadChoice::Auto: return TEXT("自動");
	case EChaosImpactPadChoice::Xbox: return TEXT("Xbox");
	case EChaosImpactPadChoice::PlayStation: return TEXT("PlayStation");
	case EChaosImpactPadChoice::Switch: return TEXT("Switch");
	default: return FString();
	}
}

FString ChaosImpactSettings::GetKeyName(const FKey& Key, const EChaosImpactDevice Device)
{
	if (!Key.IsValid())
	{
		return TEXT("－");
	}
	if (Key.IsGamepadKey())
	{
		const int32 Layout = Device == EDevice::PlayStation ? 1 : Device == EDevice::Switch ? 2 : 0;
		struct FPadName
		{
			const FKey* Key;
			const TCHAR* Names[3];
		};
		static const FPadName Names[] = {
			{&EKeys::Gamepad_FaceButton_Bottom, {TEXT("A"), TEXT("×"), TEXT("B")}},
			{&EKeys::Gamepad_FaceButton_Right, {TEXT("B"), TEXT("○"), TEXT("A")}},
			{&EKeys::Gamepad_FaceButton_Left, {TEXT("X"), TEXT("□"), TEXT("Y")}},
			{&EKeys::Gamepad_FaceButton_Top, {TEXT("Y"), TEXT("△"), TEXT("X")}},
			{&EKeys::Gamepad_LeftShoulder, {TEXT("LB"), TEXT("L1"), TEXT("L")}},
			{&EKeys::Gamepad_RightShoulder, {TEXT("RB"), TEXT("R1"), TEXT("R")}},
			{&EKeys::Gamepad_LeftTrigger, {TEXT("LT"), TEXT("L2"), TEXT("ZL")}},
			{&EKeys::Gamepad_RightTrigger, {TEXT("RT"), TEXT("R2"), TEXT("ZR")}},
			{&EKeys::Gamepad_LeftThumbstick, {TEXT("Lスティック押し込み"), TEXT("L3"), TEXT("Lスティック押し込み")}},
			{&EKeys::Gamepad_RightThumbstick, {TEXT("Rスティック押し込み"), TEXT("R3"), TEXT("Rスティック押し込み")}},
			{&EKeys::Gamepad_DPad_Up, {TEXT("十字キー↑"), TEXT("方向キー↑"), TEXT("十字ボタン↑")}},
			{&EKeys::Gamepad_DPad_Down, {TEXT("十字キー↓"), TEXT("方向キー↓"), TEXT("十字ボタン↓")}},
			{&EKeys::Gamepad_DPad_Left, {TEXT("十字キー←"), TEXT("方向キー←"), TEXT("十字ボタン←")}},
			{&EKeys::Gamepad_DPad_Right, {TEXT("十字キー→"), TEXT("方向キー→"), TEXT("十字ボタン→")}},
			{&EKeys::Gamepad_Special_Left, {TEXT("ビュー"), TEXT("クリエイト"), TEXT("－ボタン")}},
			{&EKeys::Gamepad_Special_Right, {TEXT("メニュー"), TEXT("オプション"), TEXT("＋ボタン")}},
		};
		for (const FPadName& Name : Names)
		{
			if (*Name.Key == Key)
			{
				return Name.Names[Layout];
			}
		}
		FString Display = Key.GetDisplayName(false).ToString();
		Display.RemoveFromStart(TEXT("JoyShock "));
		return Display;
	}
	struct FKeyName
	{
		const FKey* Key;
		const TCHAR* Name;
	};
	static const FKeyName SpecialKeyNames[] = {
		{&EKeys::LeftMouseButton, TEXT("左クリック")}, {&EKeys::RightMouseButton, TEXT("右クリック")},
		{&EKeys::MiddleMouseButton, TEXT("ホイールクリック")}, {&EKeys::ThumbMouseButton, TEXT("マウス4")},
		{&EKeys::ThumbMouseButton2, TEXT("マウス5")}, {&EKeys::SpaceBar, TEXT("スペース")},
		{&EKeys::LeftShift, TEXT("左Shift")}, {&EKeys::RightShift, TEXT("右Shift")},
		{&EKeys::LeftControl, TEXT("左Ctrl")}, {&EKeys::RightControl, TEXT("右Ctrl")},
		{&EKeys::LeftAlt, TEXT("左Alt")}, {&EKeys::RightAlt, TEXT("右Alt")}, {&EKeys::Up, TEXT("↑")},
		{&EKeys::Down, TEXT("↓")}, {&EKeys::Left, TEXT("←")}, {&EKeys::Right, TEXT("→")},
		{&EKeys::Hyphen, TEXT("－")}, {&EKeys::Enter, TEXT("Enter")}, {&EKeys::BackSpace, TEXT("BackSpace")},
		{&EKeys::Tab, TEXT("Tab")}, {&EKeys::CapsLock, TEXT("CapsLock")},
	};
	for (const FKeyName& Name : SpecialKeyNames)
	{
		if (*Name.Key == Key)
		{
			return Name.Name;
		}
	}
	return Key.GetDisplayName(false).ToString();
}

int32 ChaosImpactSettings::GetPlayerProfile(const APlayerController* Player)
{
	// Asked many times a frame (every control, every player): worked out once a frame per player.
	struct FCached
	{
		uint64 Frame = 0;
		uint32 Revision = 0;
		int32 Profile = 0;
	};
	static TMap<const APlayerController*, FCached> Cache;
	if (const FCached* Hit = Cache.Find(Player); Hit && Player && Hit->Frame == GFrameCounter && Hit->Revision == PlayersRevision)
	{
		return Hit->Profile;
	}
	if (Cache.Num() > 16)
	{
		Cache.Reset();
	}
	const int32 Found = FindPlayerProfile(Player);
	Cache.Add(Player, {GFrameCounter, PlayersRevision, Found});
	return Found;
}

void ChaosImpactSettings::MarkPlayersChanged()
{
	++PlayersRevision;
}

int32 ChaosImpactSettings::FindPlayerProfile(const APlayerController* Player)
{
	const ULocalPlayer* Local = Player ? Player->GetLocalPlayer() : nullptr;
	const UGameInstance* GameInstance = Player ? Player->GetGameInstance() : nullptr;
	const UChaosImpactLoadoutSubsystem* Loadouts = Player ? UChaosImpactLoadoutSubsystem::Get(Player) : nullptr;
	if (!Local || !GameInstance || !Loadouts)
	{
		return 0;
	}
	const int32 Index = GameInstance->GetLocalPlayers().IndexOfByKey(Local);
	const int32 Profile = FindProfile(Loadouts->GetLoadout(FMath::Max(Index, 0)).Nickname);
	return Profile == INDEX_NONE ? 0 : Profile;
}

EChaosImpactDevice ChaosImpactSettings::DetectPadDevice(const APlayerController* Player)
{
	if (!Player)
	{
		return EDevice::Xbox;
	}
	struct FCached
	{
		uint64 Frame = 0;
		EDevice Device = EDevice::Xbox;
	};
	static TMap<const APlayerController*, FCached> Cache;
	if (const FCached* Hit = Cache.Find(Player); Hit && Hit->Frame == GFrameCounter)
	{
		return Hit->Device;
	}
	if (Cache.Num() > 16)
	{
		Cache.Reset();
	}
	const TArray<FJSL4UControllerInfo> Pads =
		UJoyShockLibrary::JSL4UGetControllersAssignedToPlayer(const_cast<APlayerController*>(Player));
	const EDevice Device = Pads.IsEmpty() ? EDevice::Xbox : PadDeviceFor(Pads[0].ControllerType);
	Cache.Add(Player, {GFrameCounter, Device});
	return Device;
}

EChaosImpactDevice ChaosImpactSettings::DetectPadDeviceForInput(const int32 InputDeviceId)
{
	for (const FJSL4UControllerInfo& Info : UJoyShockLibrary::JSL4UGetAllConnectedControllers())
	{
		if (Info.InputDeviceId == InputDeviceId)
		{
			return PadDeviceFor(Info.ControllerType);
		}
	}
	return EDevice::Xbox;
}

EChaosImpactDevice ChaosImpactSettings::GetPlayerDevice(const APlayerController* Player)
{
	const AChaosImpactPlayerController* Controller = Cast<AChaosImpactPlayerController>(Player);
	if (!Controller || !Controller->IsUsingGamepad())
	{
		return EDevice::KeyboardMouse;
	}
	switch (GetProfile(GetPlayerProfile(Player)).Pad)
	{
	case EChaosImpactPadChoice::Xbox: return EDevice::Xbox;
	case EChaosImpactPadChoice::PlayStation: return EDevice::PlayStation;
	case EChaosImpactPadChoice::Switch: return EDevice::Switch;
	default: return DetectPadDevice(Player);
	}
}

bool ChaosImpactSettings::IsActionDown(const APlayerController* Player, const EChaosImpactAction Action)
{
	if (!Player)
	{
		return false;
	}
	const int32 Profile = GetPlayerProfile(Player);
	const EDevice Device = GetPlayerDevice(Player);
	for (int32 Slot = 0; Slot < FChaosImpactProfile::SlotCount; ++Slot)
	{
		const FKey Key = GetKey(Profile, Device, Action, Slot);
		if (!Key.IsValid())
		{
			continue;
		}
		if (Player->IsInputKeyDown(Key))
		{
			return true;
		}
		// A click on the menu's own widget can leave the player's input without the button; Slate always has it.
		if (Key.IsMouseButton() && FSlateApplication::IsInitialized()
			&& FSlateApplication::Get().GetPressedMouseButtons().Contains(Key))
		{
			return true;
		}
	}
	return false;
}

bool ChaosImpactSettings::WasActionJustPressed(const APlayerController* Player, const EChaosImpactAction Action)
{
	if (!Player)
	{
		return false;
	}
	const int32 Profile = GetPlayerProfile(Player);
	const EDevice Device = GetPlayerDevice(Player);
	for (int32 Slot = 0; Slot < FChaosImpactProfile::SlotCount; ++Slot)
	{
		const FKey Key = GetKey(Profile, Device, Action, Slot);
		if (Key.IsValid() && Player->WasInputKeyJustPressed(Key))
		{
			return true;
		}
	}
	return false;
}

bool ChaosImpactSettings::IsActionKey(const APlayerController* Player, const EChaosImpactAction Action, const FKey& Key)
{
	if (!Key.IsValid())
	{
		return false;
	}
	const int32 Profile = GetPlayerProfile(Player);
	const EDevice Device = Key.IsGamepadKey()
		? (GetPlayerDevice(Player) == EDevice::KeyboardMouse ? DetectPadDevice(Player) : GetPlayerDevice(Player))
		: EDevice::KeyboardMouse;
	return GetKey(Profile, Device, Action, 0) == Key || GetKey(Profile, Device, Action, 1) == Key;
}

FVector2D ChaosImpactSettings::GetMoveInput(const APlayerController* Player, bool& bOutStick)
{
	bOutStick = false;
	if (!Player)
	{
		return FVector2D::ZeroVector;
	}
	const FVector2D Digital = ReadDigitalMove(Player);
	if (GetPlayerDevice(Player) == EDevice::KeyboardMouse)
	{
		return Digital;
	}
	const bool bSwap = GetProfile(GetPlayerProfile(Player)).bSwapSticks;
	const FVector2D Stick(Player->GetInputAnalogKeyState(bSwap ? EKeys::Gamepad_RightX : EKeys::Gamepad_LeftX),
		Player->GetInputAnalogKeyState(bSwap ? EKeys::Gamepad_RightY : EKeys::Gamepad_LeftY));
	if (!Digital.IsNearlyZero())
	{
		// Buttons set for moving win over a resting stick.
		return Digital;
	}
	bOutStick = true;
	return Stick;
}

FVector2D ChaosImpactSettings::GetAimStick(const APlayerController* Player)
{
	if (!Player)
	{
		return FVector2D::ZeroVector;
	}
	const bool bSwap = GetProfile(GetPlayerProfile(Player)).bSwapSticks;
	return FVector2D(Player->GetInputAnalogKeyState(bSwap ? EKeys::Gamepad_LeftX : EKeys::Gamepad_RightX),
		Player->GetInputAnalogKeyState(bSwap ? EKeys::Gamepad_LeftY : EKeys::Gamepad_RightY));
}

bool ChaosImpactSettings::IsAimAssistOn(const APlayerController* Player)
{
	return GetProfile(GetPlayerProfile(Player)).bAimAssist;
}

FString ChaosImpactSettings::DescribeAction(const APlayerController* Player, const EChaosImpactAction Action)
{
	const int32 Profile = GetPlayerProfile(Player);
	const EDevice Device = GetPlayerDevice(Player);
	TArray<FString> Names;
	for (int32 Slot = 0; Slot < FChaosImpactProfile::SlotCount; ++Slot)
	{
		const FKey Key = GetKey(Profile, Device, Action, Slot);
		if (Key.IsValid())
		{
			Names.Add(GetKeyName(Key, Device));
		}
	}
	return Names.IsEmpty() ? FString(TEXT("－")) : FString::Join(Names, TEXT(" / "));
}

FKey ChaosImpactSettings::ToMenuKey(const FKey& Key, const int32 InputDeviceId)
{
	if (!IsSwitchConfirmRight()
		|| (Key != EKeys::Gamepad_FaceButton_Bottom && Key != EKeys::Gamepad_FaceButton_Right)
		|| DetectPadDeviceForInput(InputDeviceId) != EDevice::Switch)
	{
		return Key;
	}
	return Key == EKeys::Gamepad_FaceButton_Bottom ? EKeys::Gamepad_FaceButton_Right : EKeys::Gamepad_FaceButton_Bottom;
}

int32 ChaosImpactSettings::GetMasterVolume()
{
	return Store().Volume;
}

void ChaosImpactSettings::SetMasterVolume(const int32 Percent)
{
	Store().Volume = FMath::Clamp(Percent, 0, 100);
	SaveOptions();
	if (GEngine)
	{
		if (FAudioDeviceHandle Audio = GEngine->GetMainAudioDevice())
		{
			Audio->SetTransientPrimaryVolume(Store().Volume / 100.0f);
		}
	}
}

int32 ChaosImpactSettings::GetCameraShakeLevel()
{
	return Store().Shake;
}

void ChaosImpactSettings::SetCameraShakeLevel(const int32 Level)
{
	Store().Shake = FMath::Clamp(Level, 0, 2);
	SaveOptions();
}

float ChaosImpactSettings::GetCameraShakeScale()
{
	static const float Scales[] = {0.0f, 0.45f, 1.0f};
	return Scales[FMath::Clamp(Store().Shake, 0, 2)];
}

bool ChaosImpactSettings::IsSwitchConfirmRight()
{
	return Store().bSwitchConfirmRight;
}

void ChaosImpactSettings::SetSwitchConfirmRight(const bool bRight)
{
	Store().bSwitchConfirmRight = bRight;
	SaveOptions();
}

int32 ChaosImpactSettings::GetBrightness()
{
	return Store().Brightness;
}

void ChaosImpactSettings::SetBrightness(const int32 Level)
{
	Store().Brightness = FMath::Clamp(Level, 1, 10);
	SaveOptions();
	if (GEngine)
	{
		// 5 is Unreal's standard 2.2; each step a little brighter or darker.
		GEngine->DisplayGamma = 2.2f + (Store().Brightness - 5) * 0.12f;
	}
}

void ChaosImpactSettings::ApplyStartupSettings()
{
	static bool bApplied = false;
	if (bApplied || !GEngine)
	{
		return;
	}
	bApplied = true;
	if (FAudioDeviceHandle Audio = GEngine->GetMainAudioDevice())
	{
		Audio->SetTransientPrimaryVolume(Store().Volume / 100.0f);
	}
	GEngine->DisplayGamma = 2.2f + (Store().Brightness - 5) * 0.12f;
	if (UGameUserSettings* Settings = UserSettings(); Settings && GetWindowMode() != 2 && CanApplyWindowChanges()
		&& Settings->GetScreenResolution() != GetMonitorResolution())
	{
		Settings->SetScreenResolution(GetMonitorResolution());
		SaveUserSettings(Settings, true);
	}
	ApplyRenderScale();
}

bool ChaosImpactSettings::CanApplyWindowChanges()
{
	// The editor's own window, and the automated tests' fixed one, are left as they are.
	return !GIsEditor && !FApp::IsUnattended();
}

int32 ChaosImpactSettings::GetWindowMode()
{
	const UGameUserSettings* Settings = UserSettings();
	const EWindowMode::Type Mode = Settings ? Settings->GetFullscreenMode() : EWindowMode::Windowed;
	return Mode == EWindowMode::Fullscreen ? 0 : Mode == EWindowMode::WindowedFullscreen ? 1 : 2;
}

void ChaosImpactSettings::SetWindowMode(const int32 Mode)
{
	UGameUserSettings* Settings = UserSettings();
	if (!Settings)
	{
		return;
	}
	FSettingsStore& S = Store();
	const int32 Clamped = FMath::Clamp(Mode, 0, 2);
	const FIntPoint Monitor = GetMonitorResolution();
	// Fullscreen (either kind) always shows the monitor's own pixels, so the picture is never stretched; how finely
	// it is drawn is the resolution setting (ApplyRenderScale).
	FIntPoint Resolution = Monitor;
	if (Clamped < 2)
	{
		S.LastFullscreenMode = Clamped;
	}
	else
	{
		// A window comes back at its own size, smaller than the screen so its frame fits.
		Resolution = S.WindowedResolution.X > 0 ? S.WindowedResolution : FIntPoint(1600, 900);
		if (Monitor.X > 0 && (Resolution.X >= Monitor.X || Resolution.Y >= Monitor.Y))
		{
			Resolution = FIntPoint(Monitor.X * 4 / 5, Monitor.Y * 4 / 5);
		}
	}
	Settings->SetFullscreenMode(Clamped == 0 ? EWindowMode::Fullscreen : Clamped == 1 ? EWindowMode::WindowedFullscreen
		: EWindowMode::Windowed);
	if (Resolution.X > 0 && Resolution.Y > 0)
	{
		Settings->SetScreenResolution(Resolution);
	}
	SaveOptions();
	SaveUserSettings(Settings, true);
	ApplyRenderScale();
}

void ChaosImpactSettings::ToggleFullscreen()
{
	SetWindowMode(GetWindowMode() == 2 ? Store().LastFullscreenMode : 2);
}

FIntPoint ChaosImpactSettings::GetMonitorResolution()
{
	const UGameUserSettings* Settings = UserSettings();
	FIntPoint Result = Settings ? Settings->GetDesktopResolution() : FIntPoint(1920, 1080);
	// The monitor the game's window is on (not always the main one): its current pixels.
	const TSharedPtr<SWindow> Window = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWindow() : nullptr;
	if (!Window.IsValid() || !FSlateApplication::IsInitialized())
	{
		return Result;
	}
	const FVector2D Centre = FVector2D(Window->GetPositionInScreen()) + FVector2D(Window->GetSizeInScreen()) * 0.5;
	FDisplayMetrics Metrics;
	FDisplayMetrics::RebuildDisplayMetrics(Metrics);
	for (const FMonitorInfo& Monitor : Metrics.MonitorInfo)
	{
		const FPlatformRect& Rect = Monitor.DisplayRect;
		if (Centre.X >= Rect.Left && Centre.X < Rect.Right && Centre.Y >= Rect.Top && Centre.Y < Rect.Bottom
			&& Rect.Right > Rect.Left && Rect.Bottom > Rect.Top)
		{
			return FIntPoint(Rect.Right - Rect.Left, Rect.Bottom - Rect.Top);
		}
	}
	return Result;
}

TArray<FIntPoint> ChaosImpactSettings::GetResolutionChoices()
{
	// 16:9, from 720p to 4K.
	static const FIntPoint Presets[] = {{1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3200, 1800}, {3840, 2160}};
	const FIntPoint Monitor = GetMonitorResolution();
	TArray<FIntPoint> Choices;
	if (GetWindowMode() == 2)
	{
		// A window: the sizes that fit on this monitor with their frame.
		for (const FIntPoint& Preset : Presets)
		{
			if (Preset.X < Monitor.X && Preset.Y < Monitor.Y)
			{
				Choices.Add(Preset);
			}
		}
	}
	else
	{
		// Fullscreen: how finely it is drawn, up to 4K on any monitor (above the monitor's own, drawn finer and
		// shrunk), and the monitor's own.
		Choices.Append(Presets, UE_ARRAY_COUNT(Presets));
		Choices.Add(Monitor);
	}
	const FIntPoint Current = GetResolution();
	if (Current.X > 0)
	{
		Choices.AddUnique(Current);
	}
	// One per height; the monitor's own wins its height.
	TArray<FIntPoint> Unique;
	for (const FIntPoint& Choice : Choices)
	{
		const int32 Same = Unique.IndexOfByPredicate([&Choice](const FIntPoint& Other) { return Other.Y == Choice.Y; });
		if (Same == INDEX_NONE)
		{
			Unique.Add(Choice);
		}
		else if (Choice == Monitor)
		{
			Unique[Same] = Choice;
		}
	}
	Unique.Sort([](const FIntPoint& A, const FIntPoint& B) { return A.Y != B.Y ? A.Y < B.Y : A.X < B.X; });
	return Unique;
}

FIntPoint ChaosImpactSettings::GetResolution()
{
	if (GetWindowMode() != 2)
	{
		const FIntPoint Chosen = Store().FullscreenResolution;
		return Chosen.Y > 0 ? Chosen : GetMonitorResolution();
	}
	const UGameUserSettings* Settings = UserSettings();
	return Settings ? Settings->GetScreenResolution() : FIntPoint::ZeroValue;
}

void ChaosImpactSettings::SetResolution(const FIntPoint Resolution)
{
	UGameUserSettings* Settings = UserSettings();
	if (!Settings || Resolution.X <= 0 || Resolution.Y <= 0)
	{
		return;
	}
	FSettingsStore& S = Store();
	if (GetWindowMode() != 2)
	{
		// Fullscreen stays at the monitor's pixels; this is how finely the game is drawn.
		S.FullscreenResolution = Resolution == GetMonitorResolution() ? FIntPoint::ZeroValue : Resolution;
		SaveOptions();
		ApplyRenderScale();
		return;
	}
	S.WindowedResolution = Resolution;
	Settings->SetScreenResolution(Resolution);
	SaveOptions();
	SaveUserSettings(Settings, true);
	ApplyRenderScale();
}

void ChaosImpactSettings::ApplyRenderScale()
{
	// In a window the game is drawn at the window's size; in fullscreen at the chosen resolution, the monitor's
	// pixels being the screen percentage's 100%.
	float Percent = 100.0f;
	const FIntPoint Chosen = Store().FullscreenResolution;
	const FIntPoint Monitor = GetMonitorResolution();
	if (GetWindowMode() != 2 && Chosen.Y > 0 && Monitor.Y > 0)
	{
		Percent = FMath::Clamp(100.0f * Chosen.Y / Monitor.Y, 25.0f, 200.0f);
	}
	if (IConsoleVariable* ScreenPercentage = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage")))
	{
		ScreenPercentage->Set(Percent, ECVF_SetByCode);
	}
}

bool ChaosImpactSettings::IsVSyncOn()
{
	const UGameUserSettings* Settings = UserSettings();
	return Settings && Settings->IsVSyncEnabled();
}

void ChaosImpactSettings::SetVSync(const bool bOn)
{
	if (UGameUserSettings* Settings = UserSettings())
	{
		Settings->SetVSyncEnabled(bOn);
		SaveUserSettings(Settings, false);
	}
}

int32 ChaosImpactSettings::GetFrameRateLimit()
{
	const UGameUserSettings* Settings = UserSettings();
	return Settings ? FMath::RoundToInt(Settings->GetFrameRateLimit()) : 0;
}

void ChaosImpactSettings::SetFrameRateLimit(const int32 Limit)
{
	if (UGameUserSettings* Settings = UserSettings())
	{
		Settings->SetFrameRateLimit(static_cast<float>(FMath::Max(Limit, 0)));
		SaveUserSettings(Settings, false);
	}
}

int32 ChaosImpactSettings::GetOverallQuality()
{
	const UGameUserSettings* Settings = UserSettings();
	const int32 Level = Settings ? Settings->GetOverallScalabilityLevel() : -1;
	return Level > 3 ? 3 : Level;
}

void ChaosImpactSettings::SetOverallQuality(const int32 Level)
{
	if (UGameUserSettings* Settings = UserSettings())
	{
		Settings->SetOverallScalabilityLevel(FMath::Clamp(Level, 0, 3));
		SaveUserSettings(Settings, false);
	}
}

FString ChaosImpactSettings::GetQualityGroupName(const int32 Group)
{
	static const TCHAR* const Names[] = {TEXT("描画距離"), TEXT("アンチエイリアス"), TEXT("影"), TEXT("ライティング"),
		TEXT("反射"), TEXT("ポストプロセス"), TEXT("テクスチャ"), TEXT("エフェクト"), TEXT("草木"), TEXT("質感（シェーディング）")};
	return Names[FMath::Clamp(Group, 0, QualityGroupCount - 1)];
}

int32 ChaosImpactSettings::GetQualityGroup(const int32 Group)
{
	const UGameUserSettings* Settings = UserSettings();
	if (!Settings)
	{
		return 3;
	}
	int32 Level = 3;
	switch (Group)
	{
	case 0: Level = Settings->GetViewDistanceQuality(); break;
	case 1: Level = Settings->GetAntiAliasingQuality(); break;
	case 2: Level = Settings->GetShadowQuality(); break;
	case 3: Level = Settings->GetGlobalIlluminationQuality(); break;
	case 4: Level = Settings->GetReflectionQuality(); break;
	case 5: Level = Settings->GetPostProcessingQuality(); break;
	case 6: Level = Settings->GetTextureQuality(); break;
	case 7: Level = Settings->GetVisualEffectQuality(); break;
	case 8: Level = Settings->GetFoliageQuality(); break;
	case 9: Level = Settings->GetShadingQuality(); break;
	default: break;
	}
	return FMath::Clamp(Level, 0, 3);
}

void ChaosImpactSettings::SetQualityGroup(const int32 Group, const int32 Level)
{
	UGameUserSettings* Settings = UserSettings();
	if (!Settings)
	{
		return;
	}
	const int32 Value = FMath::Clamp(Level, 0, 3);
	switch (Group)
	{
	case 0: Settings->SetViewDistanceQuality(Value); break;
	case 1: Settings->SetAntiAliasingQuality(Value); break;
	case 2: Settings->SetShadowQuality(Value); break;
	case 3: Settings->SetGlobalIlluminationQuality(Value); break;
	case 4: Settings->SetReflectionQuality(Value); break;
	case 5: Settings->SetPostProcessingQuality(Value); break;
	case 6: Settings->SetTextureQuality(Value); break;
	case 7: Settings->SetVisualEffectQuality(Value); break;
	case 8: Settings->SetFoliageQuality(Value); break;
	case 9: Settings->SetShadingQuality(Value); break;
	default: return;
	}
	SaveUserSettings(Settings, false);
}

int32 ChaosImpactSettings::GetResolutionScale()
{
	const UGameUserSettings* Settings = UserSettings();
	if (!Settings)
	{
		return 100;
	}
	float Normalized = 1.0f;
	float Value = 100.0f;
	float Min = 0.0f;
	float Max = 100.0f;
	Settings->GetResolutionScaleInformationEx(Normalized, Value, Min, Max);
	return Value <= 0.0f ? 100 : FMath::Clamp(FMath::RoundToInt(Value), 10, 100);
}

void ChaosImpactSettings::SetResolutionScale(const int32 Percent)
{
	if (UGameUserSettings* Settings = UserSettings())
	{
		Settings->SetResolutionScaleValueEx(static_cast<float>(FMath::Clamp(Percent, 50, 100)));
		SaveUserSettings(Settings, false);
	}
}
