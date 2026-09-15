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
	TrainingOverlay,
	/** LAN multiplayer: user name entry before creating or searching for a room. */
	OnlineName,
	/** LAN multiplayer: four-digit room password. */
	OnlinePassword,
	/** LAN multiplayer: room creation progress or error. */
	OnlineStatus,
	/** VS mode: local split screen or online. */
	VSSelect,
	/** VS online: one or two players on this machine. */
	OnlinePlayers
};

/** Which mode the player-count and controller-assignment screens are setting up. */
UENUM(BlueprintType)
enum class EChaosImpactPlayFlow : uint8
{
	Training,
	VersusLocal,
	VersusOnline
};
