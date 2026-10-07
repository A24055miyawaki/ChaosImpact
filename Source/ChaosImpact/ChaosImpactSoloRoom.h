#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ChaosImpactMatchTypes.h"
#include "ChaosImpactSoloRoom.generated.h"

class AChaosImpactCharacter;
class UArrowComponent;
class UBoxComponent;
class UPrimitiveComponent;
class UStaticMeshComponent;

/** Where a solo room is in its fight. */
UENUM(BlueprintType)
enum class EChaosImpactSoloRoomPhase : uint8
{
	/** Doors open, waiting for the player to come in. */
	Waiting UMETA(DisplayName="待機（扉が開いている）"),
	/** Doors shut: the minions. */
	Minions UMETA(DisplayName="雑魚戦"),
	/** The minions are down: the boss. */
	Boss UMETA(DisplayName="ボス戦"),
	/** The boss is down: the doors open for good. */
	Cleared UMETA(DisplayName="クリア")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FChaosImpactSoloRoomEvent, AChaosImpactSoloRoom*, Room);

/**
 * A solo mode room. Place it in a level (or a Blueprint made from it, BP_SoloRoom) and arrange its parts:
 *  - Room Area: the room's floor (enemies appear inside it when no spawn points are given);
 *  - Entry Trigger: just inside the doorway. The player crossing it shuts the Door (and any Door Actors);
 *  - Respawn Point: just outside the doorway, where the player comes back after being knocked out in here.
 * Then Minion Count minions appear; when all are down, the boss; when the boss is down, the doors open. Knocked out in
 * here, the player comes back at the entrance and the room is as it was (its enemies gone, doors open) to try again.
 */
UCLASS(Blueprintable, BlueprintType)
class AChaosImpactSoloRoom : public AActor
{
	GENERATED_BODY()

public:
	AChaosImpactSoloRoom();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void OnConstruction(const FTransform& Transform) override;

	// ---- Settings

	/** The minions (Blueprint children of the player character, like BP_SoloEnemy). None: BP_SoloEnemy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|Solo Room", meta=(DisplayName="Minion Class (雑魚)"))
	TSoftClassPtr<AChaosImpactCharacter> MinionClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|Solo Room", meta=(DisplayName="Minion Count (雑魚の数)", ClampMin="0"))
	int32 MinionCount = 5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|Solo Room", meta=(DisplayName="Minion Level (雑魚の強さ)"))
	EChaosImpactCPULevel MinionLevel = EChaosImpactCPULevel::Normal;

	/** The boss. None: BP_BossEnemy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|Solo Room", meta=(DisplayName="Boss Class (ボス)"))
	TSoftClassPtr<AChaosImpactCharacter> BossClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|Solo Room", meta=(DisplayName="Boss Level (ボスの強さ)"))
	EChaosImpactCPULevel BossLevel = EChaosImpactCPULevel::Strongest;

	/** Where the minions appear (Target Points or any actors), in turn. None: anywhere in the Room Area. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|Solo Room", meta=(DisplayName="Spawn Points (雑魚の出現位置)"))
	TArray<TObjectPtr<AActor>> SpawnPoints;

	/** Where the boss appears. None: the middle of the Room Area. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|Solo Room", meta=(DisplayName="Boss Spawn Point (ボスの出現位置)"))
	TObjectPtr<AActor> BossSpawnPoint;

	/** More doors: actors placed in the level, shown and solid while the room is shut, gone while it is open. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|Solo Room", meta=(DisplayName="Door Actors (扉にするアクター)"))
	TArray<TObjectPtr<AActor>> DoorActors;

	/**
	 * A stand-in room built around the parts: a floor over the Room Area and walls round it, open only where the Door
	 * stands (until the level has a room of its own). Rebuilt to fit whenever the parts are moved or resized.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|Solo Room", meta=(DisplayName="Placeholder Walls (仮の床と壁)"))
	bool bPlaceholderWalls = false;

	/** How long the door takes to rise or sink. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|Solo Room", meta=(DisplayName="Door Seconds (扉が動く秒数)", ClampMin="0.0"))
	float DoorSeconds = 0.35f;

	// ---- Parts

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Chaos Impact|Solo Room")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Chaos Impact|Solo Room")
	TObjectPtr<UBoxComponent> RoomArea;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Chaos Impact|Solo Room")
	TObjectPtr<UBoxComponent> EntryTrigger;

	/** The door: up and solid while the room is shut, sunk into the floor while it is open. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Chaos Impact|Solo Room")
	TObjectPtr<UStaticMeshComponent> Door;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Chaos Impact|Solo Room")
	TObjectPtr<UArrowComponent> RespawnPoint;

	/** The stand-in room (see Placeholder Walls): the floor, then the walls (two either side of the doorway). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Chaos Impact|Solo Room")
	TArray<TObjectPtr<UStaticMeshComponent>> PlaceholderParts;

	// ---- Events (bind them, or use the Blueprint events below in a BP_SoloRoom)

	UPROPERTY(BlueprintAssignable, Category="Chaos Impact|Solo Room")
	FChaosImpactSoloRoomEvent OnRoomStarted;
	UPROPERTY(BlueprintAssignable, Category="Chaos Impact|Solo Room")
	FChaosImpactSoloRoomEvent OnBossSpawned;
	UPROPERTY(BlueprintAssignable, Category="Chaos Impact|Solo Room")
	FChaosImpactSoloRoomEvent OnRoomCleared;
	UPROPERTY(BlueprintAssignable, Category="Chaos Impact|Solo Room")
	FChaosImpactSoloRoomEvent OnRoomReset;

	UFUNCTION(BlueprintImplementableEvent, Category="Chaos Impact|Solo Room", meta=(DisplayName="Room Started (部屋開始)"))
	void ReceiveRoomStarted();
	UFUNCTION(BlueprintImplementableEvent, Category="Chaos Impact|Solo Room", meta=(DisplayName="Boss Spawned (ボス出現)"))
	void ReceiveBossSpawned(AChaosImpactCharacter* SpawnedBoss);
	UFUNCTION(BlueprintImplementableEvent, Category="Chaos Impact|Solo Room", meta=(DisplayName="Room Cleared (クリア)"))
	void ReceiveRoomCleared();
	UFUNCTION(BlueprintImplementableEvent, Category="Chaos Impact|Solo Room", meta=(DisplayName="Room Reset (やり直し)"))
	void ReceiveRoomReset();

	// ---- Control

	/** Shuts the doors and brings on the minions (the player crossing the entry does this). */
	UFUNCTION(BlueprintCallable, Category="Chaos Impact|Solo Room")
	void StartRoom(AChaosImpactCharacter* Player);

	/** Back to the start: every enemy of this room gone, doors open, waiting for the player again. */
	UFUNCTION(BlueprintCallable, Category="Chaos Impact|Solo Room")
	void ResetRoom();

	UFUNCTION(BlueprintCallable, Category="Chaos Impact|Solo Room")
	void SetDoorsClosed(bool bClosed);

	UFUNCTION(BlueprintPure, Category="Chaos Impact|Solo Room")
	EChaosImpactSoloRoomPhase GetPhase() const { return Phase; }

	/** Enemies of this room knocked out since it started. */
	UFUNCTION(BlueprintPure, Category="Chaos Impact|Solo Room")
	int32 GetDefeatedCount() const { return Defeated; }

	UFUNCTION(BlueprintPure, Category="Chaos Impact|Solo Room")
	int32 GetAliveEnemyCount() const;

	UFUNCTION(BlueprintPure, Category="Chaos Impact|Solo Room")
	bool AreDoorsClosed() const { return bDoorsClosed; }

private:
	UFUNCTION()
	void HandleEntryOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
	UFUNCTION()
	void HandleEnemyDestroyed(AActor* DestroyedActor);
	UFUNCTION()
	void HandlePlayerEliminated(AChaosImpactCharacter* Character);

	AChaosImpactCharacter* SpawnEnemy(const TSoftClassPtr<AChaosImpactCharacter>& Class, const TCHAR* FallbackPath,
		const FVector& Location, EChaosImpactCPULevel Level);
	/** A spot on the floor for the Index-th minion. */
	FVector PickSpawnLocation(int32 Index) const;
	/** On the floor under a point (where a character stands). */
	FVector OnFloor(const FVector& Point) const;
	void SpawnBoss();
	void ClearEnemies();
	void BindPlayer(AChaosImpactCharacter* NewPlayer);
	/** Fits the stand-in floor and walls to the parts (or hides them). */
	void LayoutPlaceholder();

	EChaosImpactSoloRoomPhase Phase = EChaosImpactSoloRoomPhase::Waiting;
	TArray<TWeakObjectPtr<AChaosImpactCharacter>> Enemies;
	TWeakObjectPtr<AChaosImpactCharacter> Boss;
	TWeakObjectPtr<AChaosImpactCharacter> Player;
	int32 Defeated = 0;
	/** Enemies being cleared away by this room itself (not knockouts). */
	bool bClearing = false;
	bool bDoorsClosed = false;
	/** 0 sunk (open), 1 up (shut). */
	float DoorRaise = 0.0f;
	FVector DoorUpLocation = FVector::ZeroVector;
};
