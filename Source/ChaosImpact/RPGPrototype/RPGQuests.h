#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ChaosImpactBallTypes.h"
#include "RPGEquipment.h"
#include "RPGQuests.generated.h"

class ARPGPrototypeCharacter;
struct FRPGPrototypeItem;
class USkeletalMeshComponent;

/** What a quest asks for. */
enum class ERPGQuestKind : uint8
{
	Hunt,      // defeat enemies anywhere outside the colony
	AreaHunt,  // defeat enemies in named places
	Gather,    // check the supply boxes
	Survey,    // reach the survey landmarks
	Explore,   // set foot in named places
	Treasure,  // open treasure chests
	Boss,      // defeat the boss
	Workshop,  // strengthen and tune at the merchant
	Beacon,    // light rest beacons (checkpoints)
	Bounty,    // defeat enemies of the thrower role
	Special,   // defeat enemies with a special ball
};

/** Where a quest comes from. Board / Person are offered when talked to; the rest are found in the field. */
enum class ERPGQuestFrom : uint8 { Board, Person, Beacon, Merchant, Region, BountyDrop, FirstSpecial };

/** Things that happen in play and may move quests on. */
enum class ERPGQuestEvent : uint8 { Kill, ChestOpened, BeaconLit, MerchantVisited, Upgraded, Tuned, RegionEntered, ItemObtained };

/** How a quest looks to one player right now. */
enum class ERPGQuestState : uint8 { Hidden, Locked, Offered, Active, Ready, Done };

/** A person who gives quests (index into RPGQuests::Givers()); 0 is the colony board. */
struct FRPGQuestGiverDef
{
	const TCHAR* Name;
	const TCHAR* Role;
	/** Narration when they have nothing for you (no spoken lines in this game). */
	const TCHAR* Idle;
	/** Where they stand, relative to the adventure map (the board and merchant are placed by the map). */
	FVector2D At;
	int32 Look;
};

/**
 * One quest. The table in RPGQuests.cpp is the only place to add or tune quests (append only: the order is the
 * save format). Text is narration, never spoken lines: "ロウは…してほしいらしい。"
 */
struct FRPGQuestDef
{
	const TCHAR* Name;
	ERPGQuestKind Kind;
	ERPGQuestFrom From;
	/** Board / Person quests: who offers it and takes the report. */
	int32 Giver;
	/** What this person seems to want, in narration. */
	const TCHAR* Situation;
	/** Short objective, shown with the count ("外の敵を倒す 2/4"). */
	const TCHAR* Objective;
	int32 Goal;
	/** AreaHunt: places that count; Explore: the places to visit ("|" separated, matched against place names). */
	const TCHAR* Places;
	/** Region quests: entering a place whose name contains this finds the quest. */
	const TCHAR* Trigger;
	/** Unlock: another quest finished (-1 none) and a player level. */
	int32 AfterQuest;
	int32 MinLevel;
	int32 Rubies;
	int32 XP;
	/** Reward item: Kind Ball with Nova means "a random special ball"; Material means none. */
	ERPGItemKind RewardKind;
	EChaosImpactBallType RewardBall;
	int32 RewardRarity;
	/** How the player learns of a field quest, shown in the journal. */
	const TCHAR* FoundBy;
};

namespace RPGQuests
{
	int32 Num();
	const FRPGQuestDef& Get(int32 Index);
	const TArray<FRPGQuestGiverDef>& Givers();
	FString KindName(ERPGQuestKind Kind);
	FLinearColor KindColor(ERPGQuestKind Kind);
	/** Report goes back to the giver (Board / Person); field quests complete where they are finished. */
	bool ReportsToGiver(int32 Index);
	/** Bits a quest may keep in its progress flags (boxes, landmarks, places, beacons, workshop steps). */
	int32 FlagMask(int32 Index);
	ERPGQuestState State(const ARPGPrototypeCharacter* Player, int32 Index);
	/** "あと：…を完了" for a locked quest. */
	FString UnlockHint(int32 Index);
	/** "外の敵を倒す　2 / 4" or the state. */
	FString Progress(const ARPGPrototypeCharacter* Player, int32 Index);
	FString RewardText(int32 Index);
	/** Where it came from / where to report, as one line. */
	FString SourceText(int32 Index);
	/** The reward item, made for this player's level (invalid when the quest gives none). */
	FRPGPrototypeItem MakeReward(int32 Index, int32 Level);
}

/** A quest giver standing in the field. Removable; the map places none by default, the adventure world spawns them. */
UCLASS()
class ARPGQuestGiver : public AActor
{
	GENERATED_BODY()
public:
	ARPGQuestGiver();
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RPG Quest", meta=(ClampMin="1")) int32 GiverId = 1;
	bool CanTalk(const ARPGPrototypeCharacter* Player) const;
	static ARPGQuestGiver* Nearest(const ARPGPrototypeCharacter* Player);
	/** Spawns the standard givers onto the ground of the adventure map when the level has none. */
	static void SpawnDefaults(UWorld* World, const FTransform& MapOrigin);
	virtual void Tick(float DeltaSeconds) override;
protected:
	virtual void BeginPlay() override;
private:
	UPROPERTY() TObjectPtr<USkeletalMeshComponent> Body;
};
