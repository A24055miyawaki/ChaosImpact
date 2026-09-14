#pragma once

#include "CoreMinimal.h"
#include "ChaosImpactScreen.generated.h"

UENUM(BlueprintType)
enum class EChaosImpactScreen : uint8
{
	Title,
	ModeSelect,
	TrainingSetup,
	ControllerAssignment,
	SoloReady,
	MultiReady,
	Playing,
	Pause,
	TrainingSettings,
	/** Live training overlay: the world keeps ticking while characters are frozen. */
	TrainingOverlay
};
