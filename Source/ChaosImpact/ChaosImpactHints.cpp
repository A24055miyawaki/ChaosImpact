#include "ChaosImpactHints.h"

#include "ChaosImpactPlayerController.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Widgets/Input/SVirtualJoystick.h"
#include "InputCoreTypes.h"

namespace
{
	using EDevice = EChaosImpactDevice;

	/** The machine's last press. */
	EDevice LastDevice = EDevice::KeyboardMouse;
	bool bTracking = false;

	/** Watches every press before the game and the menus see it (which device it came from). */
	class FHintInputWatcher final : public IInputProcessor
	{
	public:
		virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}

		virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
		{
			Note(InKeyEvent.GetKey(), InKeyEvent.GetInputDeviceId().GetId());
			return false;
		}

		virtual bool HandleAnalogInputEvent(FSlateApplication& SlateApp, const FAnalogInputEvent& InAnalogInputEvent) override
		{
			// (A stick pushed well over, not one resting a little off centre.)
			if (InAnalogInputEvent.GetKey().IsGamepadKey() && FMath::Abs(InAnalogInputEvent.GetAnalogValue()) > 0.5f)
			{
				Note(InAnalogInputEvent.GetKey(), InAnalogInputEvent.GetInputDeviceId().GetId());
			}
			return false;
		}

		virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
		{
			if (!MouseEvent.IsTouchEvent())
			{
				LastDevice = EDevice::KeyboardMouse;
			}
			return false;
		}

		virtual bool HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
		{
			// (Moved on purpose, not nudged.)
			if (!MouseEvent.IsTouchEvent() && MouseEvent.GetCursorDelta().Size() > 4.0f)
			{
				LastDevice = EDevice::KeyboardMouse;
			}
			return false;
		}

		virtual const TCHAR* GetDebugName() const override { return TEXT("ChaosImpactHintInputWatcher"); }

	private:
		static void Note(const FKey& Key, const int32 InputDeviceId)
		{
			LastDevice = Key.IsGamepadKey() ? ChaosImpactSettings::DetectPadDeviceForInput(InputDeviceId) : EDevice::KeyboardMouse;
		}
	};

	EDevice FromChoice(const EChaosImpactPadChoice Choice, const EDevice Detected)
	{
		switch (Choice)
		{
		case EChaosImpactPadChoice::Xbox: return EDevice::Xbox;
		case EChaosImpactPadChoice::PlayStation: return EDevice::PlayStation;
		case EChaosImpactPadChoice::Switch: return EDevice::Switch;
		default: return Detected;
		}
	}

	/** What a template name means on a controller (and what the keyboard shows when the template gives nothing). */
	bool PadKeyFor(const FString& Name, FKey& OutKey)
	{
		static const TMap<FString, FKey> Keys = {
			{TEXT("A"), EKeys::Gamepad_FaceButton_Bottom}, {TEXT("B"), EKeys::Gamepad_FaceButton_Right},
			{TEXT("X"), EKeys::Gamepad_FaceButton_Left}, {TEXT("Y"), EKeys::Gamepad_FaceButton_Top},
			{TEXT("LB"), EKeys::Gamepad_LeftShoulder}, {TEXT("RB"), EKeys::Gamepad_RightShoulder},
			{TEXT("LT"), EKeys::Gamepad_LeftTrigger}, {TEXT("RT"), EKeys::Gamepad_RightTrigger},
			{TEXT("L3"), EKeys::Gamepad_LeftThumbstick}, {TEXT("R3"), EKeys::Gamepad_RightThumbstick},
			{TEXT("Start"), EKeys::Gamepad_Special_Right}, {TEXT("Select"), EKeys::Gamepad_Special_Left},
			{TEXT("Up"), EKeys::Gamepad_DPad_Up}, {TEXT("Down"), EKeys::Gamepad_DPad_Down},
			{TEXT("Left"), EKeys::Gamepad_DPad_Left}, {TEXT("Right"), EKeys::Gamepad_DPad_Right},
		};
		if (const FKey* Found = Keys.Find(Name))
		{
			OutKey = *Found;
			return true;
		}
		return false;
	}

	FString KeyboardDefault(const FString& Name)
	{
		static const TMap<FString, FString> Defaults = {
			{TEXT("Confirm"), TEXT("Enter")}, {TEXT("Back"), TEXT("Esc")}, {TEXT("DPad"), TEXT("矢印キー")},
			{TEXT("LStick"), TEXT("WASD")}, {TEXT("RStick"), TEXT("マウス")}, {TEXT("Start"), TEXT("Esc")},
			{TEXT("Up"), TEXT("↑")}, {TEXT("Down"), TEXT("↓")}, {TEXT("Left"), TEXT("←")}, {TEXT("Right"), TEXT("→")},
		};
		const FString* Found = Defaults.Find(Name);
		return Found ? *Found : FString();
	}

	FString PadName(const FString& Name, const EDevice Device)
	{
		if (Name == TEXT("Confirm"))
		{
			return ChaosImpactHints::Confirm(Device);
		}
		if (Name == TEXT("Back"))
		{
			return ChaosImpactHints::Back(Device);
		}
		if (Name == TEXT("DPad"))
		{
			return Device == EDevice::PlayStation ? TEXT("方向キー") : Device == EDevice::Switch ? TEXT("十字ボタン") : TEXT("十字キー");
		}
		if (Name == TEXT("LStick"))
		{
			return TEXT("左スティック");
		}
		if (Name == TEXT("RStick"))
		{
			return TEXT("右スティック");
		}
		FKey Key;
		if (PadKeyFor(Name, Key))
		{
			return ChaosImpactSettings::GetKeyName(Key, Device);
		}
		// (Not a button: written as it is.)
		return Name;
	}

	/** Index of the brace closing the one at Open (nested braces skipped), INDEX_NONE when there is none. */
	int32 FindClose(const FString& Text, const int32 Open)
	{
		int32 Depth = 0;
		for (int32 Index = Open; Index < Text.Len(); ++Index)
		{
			if (Text[Index] == TEXT('{'))
			{
				++Depth;
			}
			else if (Text[Index] == TEXT('}') && --Depth == 0)
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}
}

void ChaosImpactHints::StartTracking()
{
	if (bTracking || !FSlateApplication::IsInitialized())
	{
		return;
	}
	bTracking = true;
	// Ahead of the game's own (the menus answer and keep their keys in theirs): it only watches, and must see every press,
	// or a controller working a menu was still taken for the mouse (and a cursor sitting still took the choice back).
	FSlateApplication::Get().RegisterInputPreProcessor(MakeShared<FHintInputWatcher>(), EInputPreProcessorType::PreGame);
}

void ChaosImpactHints::SetLastDeviceForTests(const EChaosImpactDevice Device)
{
	LastDevice = Device;
}

EChaosImpactDevice ChaosImpactHints::GetLastDevice()
{
	StartTracking();
	return LastDevice;
}

EChaosImpactDevice ChaosImpactHints::GetDevice(const APlayerController* Player)
{
	StartTracking();
	const AChaosImpactPlayerController* Controller = Cast<AChaosImpactPlayerController>(Player);
	if (!Controller)
	{
		return LastDevice;
	}
	if (Controller->IsPhoneSolo())
	{
		// スマホ版: the touch screen, whatever else the phone reports.
		return EDevice::KeyboardMouse;
	}
	if (!Controller->FollowsLastDevice())
	{
		// Several on one machine: each their own device.
		return ChaosImpactSettings::GetPlayerDevice(Player);
	}
	if (LastDevice == EDevice::KeyboardMouse)
	{
		return EDevice::KeyboardMouse;
	}
	return FromChoice(ChaosImpactSettings::GetProfile(ChaosImpactSettings::GetPlayerProfile(Player)).Pad, LastDevice);
}

FKey ChaosImpactHints::ConfirmKey(const EChaosImpactDevice Device)
{
	if (Device == EDevice::KeyboardMouse)
	{
		return EKeys::Enter;
	}
	return Device == EDevice::Switch && ChaosImpactSettings::IsSwitchConfirmRight() ? EKeys::Gamepad_FaceButton_Right
		: EKeys::Gamepad_FaceButton_Bottom;
}

FKey ChaosImpactHints::BackKey(const EChaosImpactDevice Device)
{
	if (Device == EDevice::KeyboardMouse)
	{
		return EKeys::Escape;
	}
	return ConfirmKey(Device) == EKeys::Gamepad_FaceButton_Right ? EKeys::Gamepad_FaceButton_Bottom : EKeys::Gamepad_FaceButton_Right;
}

FString ChaosImpactHints::Confirm(const EChaosImpactDevice Device)
{
	return Device == EDevice::KeyboardMouse ? FString(TEXT("Enter")) : ChaosImpactSettings::GetKeyName(ConfirmKey(Device), Device);
}

FString ChaosImpactHints::Back(const EChaosImpactDevice Device)
{
	return Device == EDevice::KeyboardMouse ? FString(TEXT("Esc")) : ChaosImpactSettings::GetKeyName(BackKey(Device), Device);
}

FString ChaosImpactHints::Key(const EChaosImpactDevice Device, const FKey& PadKey, const FString& KeyboardText)
{
	return Device == EDevice::KeyboardMouse ? KeyboardText : ChaosImpactSettings::GetKeyName(PadKey, Device);
}

FString ChaosImpactHints::Text(const EChaosImpactDevice Device, const FString& Template)
{
	const bool bPad = IsPad(Device);
	// スマホ版 (no controller in use): neither the keys' nor the pad's lines; the screen is tapped (the menus' back button).
	static const bool bTouchScreen = SVirtualJoystick::ShouldDisplayTouchInterface() || FParse::Param(FCommandLine::Get(), TEXT("CITouch"));
	const bool bTouch = bTouchScreen && !bPad;
	FString Out;
	Out.Reserve(Template.Len() + 16);
	for (int32 Index = 0; Index < Template.Len(); ++Index)
	{
		if (Template[Index] != TEXT('{'))
		{
			Out.AppendChar(Template[Index]);
			continue;
		}
		const int32 Close = FindClose(Template, Index);
		if (Close == INDEX_NONE)
		{
			Out.Append(Template.Mid(Index));
			break;
		}
		const FString Inside = Template.Mid(Index + 1, Close - Index - 1);
		Index = Close;
		if (Inside.StartsWith(TEXT("touch:")))
		{
			// (スマホ版 by touch only: how a finger does it.)
			if (bTouch)
			{
				Out.Append(Inside.Mid(6));
			}
			continue;
		}
		if (Inside.StartsWith(TEXT("pad:")) || Inside.StartsWith(TEXT("kb:")))
		{
			const bool bForPad = Inside.StartsWith(TEXT("pad:"));
			if (bForPad == bPad && !bTouch)
			{
				Out.Append(Text(Device, Inside.Mid(bForPad ? 4 : 3)));
			}
			continue;
		}
		FString Name = Inside, Keyboard;
		const bool bGiven = Inside.Split(TEXT("|"), &Name, &Keyboard);
		Out.Append(bPad ? PadName(Name, Device) : bGiven ? Keyboard : KeyboardDefault(Name));
	}
	return Out;
}

FString ChaosImpactHints::Text(const APlayerController* Player, const FString& Template)
{
	return Text(GetDevice(Player), Template);
}
