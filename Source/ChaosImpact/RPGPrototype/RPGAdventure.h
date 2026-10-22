#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RPGAdventureTypes.h"
#include "RPGAdventure.generated.h"

class ARPGPrototypeCharacter;
class UInstancedStaticMeshComponent;
class UStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;

/** Editor-visible, removable diorama terrain. All coordinates are relative to this actor. */
UCLASS()
class ARPGAdventureWorld : public AActor
{
    GENERATED_BODY()
public:
    ARPGAdventureWorld();
    virtual void OnConstruction(const FTransform& Transform) override;
    UFUNCTION(BlueprintCallable,CallInEditor,Category="RPG Adventure") void Rebuild();
    /** One-way editor conversion. Rebuild never overwrites the resulting hand-edited actors. */
    UFUNCTION(BlueprintCallable,CallInEditor,Category="RPG Adventure|Editing") int32 BakeEditableScenery();
    UPROPERTY(VisibleAnywhere,Category="RPG Adventure|Editing") bool bUseEditableScenery=false;
    UPROPERTY(EditAnywhere,Category="RPG Adventure") int32 Seed=42016;
    UPROPERTY(EditAnywhere,Category="RPG Adventure",meta=(ClampMin="1",ClampMax="1.5")) float CameraDistanceMultiplier=1.18f;
    bool IsInColony(const FVector& At) const;
    FVector PlayerSpawn(int32 Index=0) const;
    FString RegionName(const FVector& At) const;
    static ARPGAdventureWorld* Find(const UWorld* World);
    int32 GetTerrainTileCount() const { return TerrainTileCount; }
protected:
    virtual void BeginPlay() override;
private:
    UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> Parts;
    TMap<FString,UInstancedStaticMeshComponent*> Batches;
    TMap<UInstancedStaticMeshComponent*,TArray<FString>> EditingGroups;
    FString EditingGroup;
    UPROPERTY(VisibleAnywhere,Category="RPG Adventure") int32 TerrainTileCount=0;
    void Piece(const TCHAR* Shape,const TCHAR* Material,FVector At,FVector Dimensions,FRotator Rotation=FRotator::ZeroRotator,bool Collision=false);
    void Tree(FVector At,float Scale,FRandomStream& Random);
    void House(FVector At,FRotator Facing,const TCHAR* Roof);
    void Lantern(FVector At,float Height=230);
    void ContinuationScenery(FRandomStream& Random);
};

/** Saved, ordinary editable mesh components; no construction script regenerates these. */
UCLASS()
class ARPGAdventureSceneryPiece : public AActor
{
    GENERATED_BODY()
public:
    ARPGAdventureSceneryPiece();
};

/** Session-only per-player respawn landmark. Moving it in the editor moves its spawn. */
UCLASS()
class ARPGAdventureCheckpoint : public AActor
{
    GENERATED_BODY()
public:
    ARPGAdventureCheckpoint();
    UPROPERTY(EditAnywhere,Category="RPG Adventure|Checkpoint") FString DisplayName=TEXT("休息の灯り");
    UPROPERTY(EditAnywhere,Category="RPG Adventure|Checkpoint",meta=(ClampMin="100",ClampMax="500")) float ActivationRadius=260;
    UPROPERTY(EditAnywhere,Category="RPG Adventure|Checkpoint",meta=(MakeEditWidget="true")) FVector RespawnOffset=FVector(150,0,120);
    FVector RespawnFor(int32 PlayerIndex) const;
    bool TryActivate(ARPGPrototypeCharacter* Player);
    virtual void OnConstruction(const FTransform& Transform) override;
protected:
    virtual void BeginPlay() override;
private:
    void ApplyLook();
};

/** Shared physical object; quest state and collected flags belong exclusively to each player. */
UCLASS()
class ARPGAdventureNode : public AActor
{
    GENERATED_BODY()
public:
    ARPGAdventureNode();
    virtual void OnConstruction(const FTransform& Transform) override;
    UFUNCTION(BlueprintCallable,CallInEditor,Category="RPG Adventure") void RebuildNode();
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="RPG Adventure") ERPGAdventureNodeKind Kind=ERPGAdventureNodeKind::Board;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="RPG Adventure",meta=(ClampMin="0",ClampMax="2")) int32 ObjectiveId=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="RPG Adventure") FString DisplayName=TEXT("依頼掲示板");
    bool CanUse(const ARPGPrototypeCharacter* Player) const;
    bool Use(ARPGPrototypeCharacter* Player);
    static ARPGAdventureNode* Nearest(const ARPGPrototypeCharacter* Player);
private:
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Pieces;
};

namespace RPGAdventure
{
    FString QuestName(int32 Index);
    FString QuestDescription(int32 Index);
    int32 Goal(int32 Index);
    FString Objective(const ARPGPrototypeCharacter* Player,int32 Index);
}
