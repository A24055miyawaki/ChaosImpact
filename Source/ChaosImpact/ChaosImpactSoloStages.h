#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "ChaosImpactSoloStages.generated.h"

/** What kind of stage a world map node is (its look on the map). */
enum class EChaosImpactSoloStageKind : uint8
{
	Normal,
	Challenge,
	/** One of the four 四天王 (a region's last stage). */
	Boss,
	/** The temple's gatekeeper, the factory, the president: the story's big fights. */
	Story,
	/** Not a stage: a gate between the two maps. */
	Gate
};

/** One of solo mode's 30 stages (or a gate on the world map). */
struct FChaosImpactSoloStageInfo
{
	/** 1-30; the gates are GateToMap2 and GateToMap1. */
	int32 Number = 0;
	/** 1: 占領された世界, 2: デットボール合衆国. */
	int32 Map = 1;
	const TCHAR* Name = TEXT("");
	/** The region it is in (潮の占領地 ...). */
	const TCHAR* Area = TEXT("");
	/** One line about it. */
	const TCHAR* Blurb = TEXT("");
	/** The level that plays it; empty until it is made (準備中). */
	const TCHAR* Level = TEXT("");
	EChaosImpactSoloStageKind Kind = EChaosImpactSoloStageKind::Normal;
	/** The stages that must be cleared first (all of them). */
	TArray<int32> Requires;
};

namespace ChaosImpactSolo
{
	/** The temple's gate on map 1 (open once the temple is cleared): over to map 2. */
	constexpr int32 GateToMap2 = 100;
	/** Where map 2 starts: back over to map 1. */
	constexpr int32 GateToMap1 = 200;
	constexpr int32 StageCount = 30;

	CHAOSIMPACT_API const TArray<FChaosImpactSoloStageInfo>& GetStages();
	CHAOSIMPACT_API const FChaosImpactSoloStageInfo* FindStage(int32 Number);
	/** The stage's level package (/Game/...), empty while it is not made yet. */
	CHAOSIMPACT_API FString GetLevelPackage(int32 Number);
	/** The world map level for map 1 or 2. */
	CHAOSIMPACT_API FString GetMapPackage(int32 Map);
}

/**
 * Solo mode's progress on this machine: the stages cleared, which reveals on the world map have been shown, and where
 * the player last stood. There are three files (save slots, chosen on solo mode's title); Load and Save use the one
 * chosen (the active slot). Automation tests use slots of their own, so they never touch a player's progress.
 */
UCLASS()
class CHAOSIMPACT_API UChaosImpactSoloProgress : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TArray<int32> Cleared;

	/** Nodes whose reveal (the fog clearing off them) has already been played on the map. */
	UPROPERTY()
	TArray<int32> Shown;

	/** The node the player last went from (they come back to the map standing on it). */
	UPROPERTY()
	int32 LastNode = 1;

	/** Development: every stage open (the fog gone), whatever is cleared. */
	UPROPERTY()
	bool bUnlockAll = false;

	/** When this file was last saved (shown on its card). */
	UPROPERTY()
	FDateTime LastPlayed;

	static constexpr int32 SlotCount = 3;
	/** The file being played (1-3). */
	static int32 GetActiveSlot();
	static void SetActiveSlot(int32 Slot);
	/** A file has been started in this slot (1-3). */
	static bool DoesSlotExist(int32 Slot);
	/** That slot's progress (a new one when it is empty). */
	static UChaosImpactSoloProgress* LoadSlot(int32 Slot);
	/** Erases that slot's file. */
	static void DeleteSlot(int32 Slot);

	/** The active slot's progress (a new one when there is none). */
	static UChaosImpactSoloProgress* Load();
	/** Into the active slot. */
	void Save();
	/** Forgets everything (and saves that). */
	void ResetAll();

	bool IsCleared(int32 Number) const { return Cleared.Contains(Number); }
	/** Can be played: everything it needs is cleared. */
	bool IsAvailable(int32 Number) const;
	/** Out of the fog on the map: playable, cleared, or one of the stages it needs is cleared (it shows, locked). */
	bool IsRevealed(int32 Number) const;
	void MarkCleared(int32 Number);
	int32 CountCleared(int32 Map) const;

	/** The active slot's save name. */
	static FString GetSlotName();
	static FString GetSlotName(int32 Slot);
};
