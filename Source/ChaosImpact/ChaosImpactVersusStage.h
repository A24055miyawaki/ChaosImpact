#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ChaosImpactVersusStage.generated.h"

class USceneComponent;
class UStaticMeshComponent;

/**
 * Provisional VS stage: a square arena with symmetric cover, a low centre deck and corner shelters.
 * The server spawns it once and it replicates, so every online member stands on the same geometry.
 *
 * Editing: make a Blueprint of this class and set it as Versus Stage Class on the game mode, or place the
 * stage (or that Blueprint) in the level, which is then used as-is. Every block is an editable component,
 * extra meshes can be added in the Blueprint, and the spawn / ball points are draggable handles.
 */
UCLASS(Blueprintable, meta=(DisplayName="Chaos Impact Versus Stage"))
class AChaosImpactVersusStage : public AActor
{
	GENERATED_BODY()

public:
	AChaosImpactVersusStage();
	virtual void OnConstruction(const FTransform& Transform) override;

	/** World positions on the floor surface where players may appear. */
	TArray<FVector> GetSpawnPoints() const;
	/** World positions on the floor surface for ball pads. */
	TArray<FVector> GetBallPoints() const;
	FVector GetCenter() const { return GetActorLocation(); }

	/** Half the floor width; the intro camera flies around this. */
	static constexpr float HalfExtent = 3000.0f;

	/**
	 * Where players appear, relative to the stage, on the floor surface. With the stage (or a Blueprint of it)
	 * selected, each point has its own handle in the level viewport and can be dragged into place.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Versus Stage", meta=(MakeEditWidget))
	TArray<FVector> SpawnPointOffsets;

	/** Ball pad positions, relative to the stage, on the floor surface. Also draggable in the viewport. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Versus Stage", meta=(MakeEditWidget))
	TArray<FVector> BallPointOffsets;

	/** Off: the blocks keep the materials set in the editor instead of being tinted with the built-in colours. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Versus Stage")
	bool bApplyDefaultColors = true;

protected:
	virtual void BeginPlay() override;

private:
	UStaticMeshComponent* CreateBlock(const FName& Name, const FVector& Location, const FVector& Size,
		const FRotator& Rotation = FRotator::ZeroRotator, bool bCollision = true);
	void ApplyStageColors();

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category="Chaos Impact|Versus Stage")
	TObjectPtr<UStaticMeshComponent> FloorBlock;

	UPROPERTY(VisibleAnywhere, Category="Chaos Impact|Versus Stage")
	TArray<TObjectPtr<UStaticMeshComponent>> MarkingBlocks;

	UPROPERTY(VisibleAnywhere, Category="Chaos Impact|Versus Stage")
	TArray<TObjectPtr<UStaticMeshComponent>> BoundaryBlocks;

	UPROPERTY(VisibleAnywhere, Category="Chaos Impact|Versus Stage")
	TArray<TObjectPtr<UStaticMeshComponent>> DeckBlocks;

	UPROPERTY(VisibleAnywhere, Category="Chaos Impact|Versus Stage")
	TArray<TObjectPtr<UStaticMeshComponent>> BankWallBlocks;

	UPROPERTY(VisibleAnywhere, Category="Chaos Impact|Versus Stage")
	TArray<TObjectPtr<UStaticMeshComponent>> PillarBlocks;

	UPROPERTY(VisibleAnywhere, Category="Chaos Impact|Versus Stage")
	TArray<TObjectPtr<UStaticMeshComponent>> ShelterBlocks;

	UPROPERTY(VisibleAnywhere, Category="Chaos Impact|Versus Stage")
	TArray<TObjectPtr<UStaticMeshComponent>> LowCoverBlocks;
};
