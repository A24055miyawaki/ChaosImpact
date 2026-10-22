#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RPGTreasureChest.generated.h"

class ARPGPrototypeCharacter;
class ARPGTreasureChestVisual;
class UStaticMeshComponent;
class UPointLightComponent;

UENUM(BlueprintType)
enum class ERPGChestRewardKind : uint8 { Random, Ball, Gear, Badge };

/** Look only (the odds are the chest's own fields): wood by the road, silver off it, gold in secret places. */
UENUM(BlueprintType)
enum class ERPGChestStyle : uint8 { Wood, Silver, Gold };

/** A stable ID identifies a personal, saved claim; duplicates must use a new ID. */
UCLASS()
class ARPGTreasureChest : public AActor
{
    GENERATED_BODY()
public:
    ARPGTreasureChest();
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void PostDuplicate(EDuplicateMode::Type DuplicateMode) override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void BeginPlay() override;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="RPG Treasure") FName ChestId;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="RPG Treasure") FString DisplayName=TEXT("宝箱");
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="RPG Treasure") ERPGChestStyle Style=ERPGChestStyle::Wood;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="RPG Treasure",meta=(ClampMin="0",ClampMax="100000")) int32 RubyMin=30;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="RPG Treasure",meta=(ClampMin="0",ClampMax="100000")) int32 RubyMax=80;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="RPG Treasure") ERPGChestRewardKind RewardKind=ERPGChestRewardKind::Random;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="RPG Treasure") bool bSpecialBallsOnly=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="RPG Treasure",meta=(ClampMin="0",ClampMax="1")) float RareChance=.34f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="RPG Treasure",meta=(ClampMin="0",ClampMax="1")) float UniqueChance=.10f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="RPG Treasure",meta=(ClampMin="0",ClampMax="1")) float LegendChance=.01f;
    UFUNCTION(BlueprintCallable,CallInEditor,Category="RPG Treasure") void SnapToGround();
    bool IsOpenedBy(const ARPGPrototypeCharacter* Player) const;
    bool CanUse(const ARPGPrototypeCharacter* Player) const;
    /** Claims the chest for this player and throws its rubies and item out onto the ground for them to pick up. */
    bool Open(ARPGPrototypeCharacter* Player);
    static ARPGTreasureChest* Nearest(const ARPGPrototypeCharacter* Player);
    /** Paints the trim, lock and glints in the chest's style (editor preview and every player's own copy). */
    static void ApplyStyle(AActor* Chest,ERPGChestStyle Style);
private:
    TMap<TWeakObjectPtr<ARPGPrototypeCharacter>,TWeakObjectPtr<ARPGTreasureChestVisual>> Views;
};

/** Owner-only body/lid allow different split-screen players to see their own open state. */
UCLASS(NotPlaceable)
class ARPGTreasureChestVisual : public AActor
{
    GENERATED_BODY()
public:
    ARPGTreasureChestVisual();
    void Initialize(ARPGTreasureChest* Chest,ARPGPrototypeCharacter* Player);
    virtual void Tick(float DeltaSeconds) override;
    /** The lid flies open with a flash (the moment the owner opens it). */
    void PlayOpen();
private:
    UPROPERTY() TObjectPtr<USceneComponent> Hinge;
    UPROPERTY() TWeakObjectPtr<ARPGTreasureChest> Source;
    UPROPERTY() TWeakObjectPtr<ARPGPrototypeCharacter> Recipient;
    float OpenAge=-1, Clock=0;
};

/**
 * A ruby thrown out of a chest: it arcs, bounces and settles, then is drawn to its owner and collected by walking
 * near (no button). Only its owner sees and takes it (everybody sees it on a shared screen).
 */
UCLASS(NotPlaceable)
class ARPGRubyGem : public AActor
{
    GENERATED_BODY()
public:
    ARPGRubyGem();
    static ARPGRubyGem* Spawn(UWorld* World,const FVector& At,const FVector& Velocity,int32 Value,ARPGPrototypeCharacter* Recipient);
    virtual void Tick(float DeltaSeconds) override;
    bool IsFor(const ARPGPrototypeCharacter* Player) const { return Recipient.Get()==Player; }
    int32 GetValue() const { return Value; }
    bool HasLanded() const { return bLanded; }
    bool Collect(ARPGPrototypeCharacter* Player);
    /** Everything still lying about from this player's chests goes straight to them (before a save, so nothing is lost). */
    static int32 CollectAllFor(ARPGPrototypeCharacter* Player);
private:
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Upper;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Lower;
    TWeakObjectPtr<ARPGPrototypeCharacter> Recipient;
    FVector Velocity=FVector::ZeroVector, Rest=FVector::ZeroVector;
    /** The chest's floor: anything well above it (a roof, a wall top) is passed through, never landed on. */
    float FloorZ=0;
    FVector Origin=FVector::ZeroVector;
    int32 Value=1, Bounces=0;
    float Age=0, Spin=0;
    bool bLanded=false, bCollected=false;
};
