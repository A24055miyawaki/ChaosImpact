#pragma once

// The PlayStation / Switch pads come through JoyShockLibrary4Unreal, a Windows-only plugin. スマホ版 (and any other
// platform) builds without it: the same few calls answer "no such pads" there, so the game code is the same everywhere.
#if WITH_JOYSHOCK
#include "JoyShockBlueprintLibrary.h"
#include "JoyShockTypes.h"
#else
#include "CoreMinimal.h"

class APlayerController;
class AController;

enum class EJSL4UControllerType : uint8
{
	Undefined = 0,
	XInputController = 1,
	DualShock4 = 2,
	DualSense = 3,
	ProController = 4,
	ProController2 = 5,
	JoyConLeft = 6,
	JoyConRight = 7,
	JoyCon2Left = 8,
	JoyCon2Right = 9
};

struct FJSL4UControllerInfo
{
	int32 InputDeviceId = -1;
	EJSL4UControllerType ControllerType = EJSL4UControllerType::Undefined;
};

class UJoyShockLibrary
{
public:
	static TArray<FJSL4UControllerInfo> JSL4UGetAllConnectedControllers() { return {}; }
	static bool JSL4UAssignControllerToPlayer(const FJSL4UControllerInfo&, APlayerController*) { return false; }
	static TArray<FJSL4UControllerInfo> JSL4UGetControllersAssignedToPlayer(AController*) { return {}; }
};
#endif
