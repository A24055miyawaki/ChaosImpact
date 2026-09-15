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
	OnlinePlayers,
	/** VS match rules: time, free-for-all or teams, CPUs. */
	MatchRules,
	/** VS team battle: every player picks a team. */
	TeamSelect,
	/** Local VS results: rematch, change rules or leave. */
	MatchEnd,
	/** LAN multiplayer: the room's name, when creating a room or renaming it from the lobby. */
	OnlineRoomName,
	/** LAN multiplayer: the rooms found for the password; pick one to join. */
	RoomList
};

/** Which mode the player-count and controller-assignment screens are setting up. */
UENUM(BlueprintType)
enum class EChaosImpactPlayFlow : uint8
{
	Training,
	VersusLocal,
	VersusOnline
};
