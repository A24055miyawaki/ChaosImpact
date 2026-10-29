#pragma once

#include "CoreMinimal.h"
#include "ChaosImpactStageBase.h"
#include "ChaosImpactDuelStage.generated.h"

class UMaterialInterface;
class UDirectionalLightComponent;
class UPointLightComponent;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * ランクマッチ's stage (デュエルアリーナ): a small square arena for one against one, mirrored left to right so neither
 * side has anything the other lacks. Each player starts at the back of their own half (red on -X, blue on +X) behind
 * a low jumpable wall with a ball pad just ahead; the halves meet at a glowing centre line with two more pads on it,
 * four pillars around the middle and angled bank walls in the corners give cover without anywhere to hide for long.
 * Small on purpose: nobody can run away from a duel for long.
 *
 * Editing: like the other stages, every block is an editable component (bEditableWhenInherited) and the points are
 * draggable handles; turn off bApplyDefaultColors to keep materials set in the editor.
 */
UCLASS(Blueprintable, meta=(DisplayName="Chaos Impact Duel Stage"))
class AChaosImpactDuelStage : public AChaosImpactStageBase
{
	GENERATED_BODY()

public:
	AChaosImpactDuelStage();
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	static constexpr float DuelHalfExtent = 1850.0f;
	virtual float GetHalfExtent() const override { return DuelHalfExtent; }
	/** The first two are the duel's starts: side 0 (red, -X) then side 1 (blue, +X). The rest are spares. */
	virtual TArray<FVector> GetSpawnPoints() const override { return ToWorld(SpawnPointOffsets); }
	virtual TArray<FVector> GetBallPoints() const override { return ToWorld(BallPointOffsets); }

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Duel Stage", meta=(MakeEditWidget))
	TArray<FVector> SpawnPointOffsets;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Duel Stage", meta=(MakeEditWidget))
	TArray<FVector> BallPointOffsets;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Duel Stage")
	bool bApplyDefaultColors = true;

	/** How bright the neon trims and the centre line glow. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Duel Stage", meta=(ClampMin="0"))
	float GlowIntensity = 3.0f;

protected:
	virtual void BeginPlay() override;

private:
	// Runtime-only override: the lobby and other stages keep their original sun and shadow settings.
	struct FSunSettings
	{
		TWeakObjectPtr<UDirectionalLightComponent> Light;
		FRotator Rotation;
		float SourceAngle = 0.f;
		uint8 Mobility = 0;
	};
	TArray<FSunSettings> SavedSuns;
	bool bArenaLightingActive = false;
	void UpdateArenaLighting(bool bActive);

	enum EPaint : uint8
	{
		Floor,
		HalfRed,
		HalfBlue,
		Wall,
		Cover,
		Pillar,
		GlowRed,
		GlowBlue,
		GlowGold,
		GlowWhite,
		PaintCount
	};
	UStaticMeshComponent* AddBlock(const FString& Name, EPaint Paint, const FVector& Location, const FVector& Size,
		float Yaw = 0.0f, bool bCollision = true, UStaticMesh* Mesh = nullptr);
	/** A flat glowing ring lying on the floor (no collision). */
	UStaticMeshComponent* AddRing(const FString& Name, EPaint Paint, const FVector& Location, float Radius, float Thickness);
	/** The same block on both halves: at Location and mirrored to -X (red side first). */
	void AddMirrored(const FString& Name, EPaint RedPaint, EPaint BluePaint, const FVector& BlueLocation, const FVector& Size,
		float BlueYaw = 0.0f, bool bCollision = true, UStaticMesh* Mesh = nullptr);
	void ApplyStageColors();
	static FLinearColor GetPaintColor(EPaint Paint);
	static bool IsGlow(EPaint Paint) { return Paint >= GlowRed; }

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category="Chaos Impact|Duel Stage")
	TArray<TObjectPtr<UStaticMeshComponent>> Blocks;

	UPROPERTY(VisibleAnywhere, Category="Chaos Impact|Duel Stage")
	TArray<TObjectPtr<UPointLightComponent>> SideLights;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> BoxMesh;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> ChamferMesh;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> CylinderMesh;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> RingMesh;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> FlatMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> GlowMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> FloorMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> WallMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> PaintMaterial;
};
