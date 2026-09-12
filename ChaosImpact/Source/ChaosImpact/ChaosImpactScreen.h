#pragma once

#include "CoreMinimal.h"
#include "ChaosImpactScreen.generated.h"

UENUM(BlueprintType)
enum class EChaosImpactScreen : uint8
{
	Title,
	ModeSelect,
	TrainingSetup,
	SoloReady,
	MultiReady,
	Playing,
	Pause,
	TrainingSettings
};
