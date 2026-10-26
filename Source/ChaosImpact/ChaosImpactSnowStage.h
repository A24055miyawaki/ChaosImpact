#pragma once

#include "CoreMinimal.h"
#include "ChaosImpactStageBase.h"
#include "ChaosImpactSnowStage.generated.h"

class AChaosImpactBall;
class AChaosImpactDriftingFlakes;
class AChaosImpactSimaePerch;
class UBoxComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPointLightComponent;
class UPostProcessComponent;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * VS ステージ5 (氷雪の雪見庭園): a snow-covered Japanese castle garden and frozen pond court for up to 8 players.
 * Built with strict four-fold rotational symmetry:
 *  - Central Frozen Pond Court (-60 cm): a wide octagonal ice rink offering open sightlines for dodging, connected to
 *    the outer wooden veranda by four vermilion Taiko bridges and four wide diagonal snow-stone ramps.
 *  - Center Wind-Dial Island (0 cm): a low octagonal island in the middle of the pond holding the central ball pad
 *    and an overhead compass vane that points the direction of the Blizzard Gust.
 *  - Four Hexagonal Ice Pillars in the pond and four L-shaped Folding Screen walls (屏風壁) on the outer wooden
 *    veranda (0 cm), combining crisp bank-shot angles with sheltered retreat pockets.
 *  - Blizzard Gust (ブリザード・ガスト): every 12-15 seconds, after a 2.5 s directional warning, a gust blows across
 *    the stage for 4.5 s, curving flying balls toward the downwind side via DeflectByWind and sweeping the falling
 *    snow (AChaosImpactDriftingFlakes). Shima-enaga birds (AChaosImpactSimaePerch) perch on the corner lanterns.
 */
UCLASS(Blueprintable, meta=(DisplayName="Chaos Impact Snow Stage"))
class AChaosImpactSnowStage : public AChaosImpactStageBase
{
	GENERATED_BODY()

public:
	AChaosImpactSnowStage();
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual TArray<FVector> GetSpawnPoints() const override;
	virtual TArray<FVector> GetBallPoints() const override;
	virtual float GetHalfExtent() const override { return SnowHalfExtent; }
	virtual bool IsOutside(const FVector& Location) const override;

	static constexpr float LayoutHalfExtent = 3300.0f;
	static constexpr float LayoutScale = 1.0f;
	static constexpr float SnowHalfExtent = LayoutHalfExtent * LayoutScale;
	static constexpr float BevelLine = 5000.0f;

	/** Seconds between one Blizzard Gust and the next (server). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Snow Stage|Gimmick", meta=(ClampMin="4.0"))
	float WindIntervalMin = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Snow Stage|Gimmick", meta=(ClampMin="4.0"))
	float WindIntervalMax = 15.0f;

	/** Delay before the first Blizzard Gust after match GO. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Snow Stage|Gimmick", meta=(ClampMin="0.0"))
	float FirstWindAfter = 8.0f;

	/** Directional warning duration before the gust begins. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Snow Stage|Gimmick", meta=(ClampMin="0.5"))
	float WindWarnSeconds = 2.5f;

	/** Duration of the active Blizzard Gust. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Snow Stage|Gimmick", meta=(ClampMin="1.0"))
	float WindActiveSeconds = 4.5f;

	/** Lateral acceleration (cm/s^2) applied to flying balls by the Blizzard Gust. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Snow Stage|Gimmick", meta=(ClampMin="0.0", ClampMax="4000.0"))
	float WindBallAcceleration = 1350.0f;

	/** Spawns falling snow (AChaosImpactDriftingFlakes) and Shima-enaga birds (AChaosImpactSimaePerch) at BeginPlay. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Snow Stage|Decor")
	bool bSpawnWinterDecor = true;

	/** Where players appear, relative to the stage, on the floor surface. Draggable in the viewport. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Snow Stage", meta=(MakeEditWidget))
	TArray<FVector> SpawnPointOffsets;

	/** Ball pad positions, relative to the stage, on the floor surface. Draggable in the viewport. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Snow Stage", meta=(MakeEditWidget))
	TArray<FVector> BallPointOffsets;

	/** Off: blocks keep the materials assigned in the editor instead of applying the palette below. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Snow Stage|Palette")
	bool bApplyDefaultColors = true;

	/** Quarter accent colours (Vermilion, Ice Cyan, Gold, Pine Jade). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Snow Stage|Palette")
	TArray<FLinearColor> QuarterColors;

	/** Warm cedar veranda planks. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Snow Stage|Palette")
	FLinearColor VerandaColor = FLinearColor(0.36f, 0.22f, 0.13f);

	/** Frozen pond surface colour. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Snow Stage|Palette")
	FLinearColor IceColor = FLinearColor(0.52f, 0.82f, 0.98f);

	/** Deep ice crystal colour for the hexagonal bank pillars. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Snow Stage|Palette")
	FLinearColor CrystalColor = FLinearColor(0.18f, 0.68f, 0.96f);

	/** Pale snow-stone kerbs, ramps, and lantern pedestals. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Snow Stage|Palette")
	FLinearColor SnowStoneColor = FLinearColor(0.82f, 0.86f, 0.92f);

	/** Outer sanctuary walls (dark castle timber & plaster). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Snow Stage|Palette")
	FLinearColor WallColor = FLinearColor(0.12f, 0.15f, 0.22f);

	/** Gold fittings and lantern bands. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Snow Stage|Palette")
	FLinearColor GoldTrimColor = FLinearColor(0.95f, 0.72f, 0.18f);

	/** Warm lantern and frost rune glow. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Snow Stage|Palette")
	FLinearColor GlowColor = FLinearColor(0.35f, 0.9f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Snow Stage|Palette", meta=(ClampMin="0.0"))
	float GlowIntensity = 2.6f;

	/** Distant snow peaks and castle towers. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Snow Stage|Palette")
	FLinearColor SceneryColor = FLinearColor(0.72f, 0.78f, 0.88f);

	/** Stage-relative unit direction for WindDir (0: +X North, 1: +Y East, 2: -X South, 3: -Y West). */
	static FVector GetWindDirectionVector(int32 WindDir);
	float GetWindProgress() const;
	void StartWindGust(int32 WindDir);

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnRep_WindState();

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastWindGust(int32 WindDir);

	/** -1: calm; 0..3: wind blowing toward N, E, S, W. */
	UPROPERTY(ReplicatedUsing=OnRep_WindState)
	int32 WindDirectionIndex = -1;

	/** Server world time when the current wind warning started. */
	UPROPERTY(ReplicatedUsing=OnRep_WindState)
	float WindStartedAt = 0.0f;

private:
	enum EPaint : uint8
	{
		Veranda, Ice, Crystal, SnowStone, Wall, GoldTrim, Glow, Scenery, DarkTimber, PineGreen,
		Accent, AccentDark = Accent + 4, AccentGlow = AccentDark + 4
	};

	UStaticMeshComponent* AddPart(USceneComponent* Parent, const FString& Name, uint8 Paint, UStaticMesh* Mesh,
		const FVector& Location, const FVector& Size, const FRotator& Rotation = FRotator::ZeroRotator, bool bCollision = true);
	UStaticMeshComponent* AddBox(const FString& Name, uint8 Paint, const FVector& Location, const FVector& Size,
		float Yaw = 0.0f, bool bCollision = true, bool bChamfer = false);
	UStaticMeshComponent* AddCylinder(const FString& Name, uint8 Paint, const FVector& Location, float Diameter,
		float Height, bool bCollision = true);
	UStaticMeshComponent* AddRing(const FString& Name, uint8 Paint, const FVector& Location, float Radius, float Thickness);
	UStaticMeshComponent* AddRamp(const FString& Name, uint8 Paint, const FVector& Low, const FVector& High, float Width,
		uint8 EdgePaint = 255);

	void BuildFrozenPondAndCenter();
	void BuildQuarter(int32 Quarter);
	void BuildStoneLantern(const FString& Name, const FVector& Location, uint8 GlowPaint);
	void BuildWinterScenery(int32 Quarter);

	void ApplyPalette();
	FLinearColor GetPaintColor(uint8 Paint) const;
	static bool ReadPaint(const UStaticMeshComponent* Block, uint8& OutPaint);

	void TickWindServer(double Now, float DeltaSeconds);
	void UpdateWindLook(float DeltaSeconds);
	bool IsMatchOn() const;
	double GetServerNow() const;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<USceneComponent> WindVaneRoot;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<USceneComponent> WindStreamerGroup;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TArray<TObjectPtr<UStaticMeshComponent>> WindStreamers;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TArray<TObjectPtr<UStaticMeshComponent>> CompassArrows;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TArray<TObjectPtr<UStaticMeshComponent>> SteamRings;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UBoxComponent> GradeVolume;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UPostProcessComponent> Grade;

	UPROPERTY(Transient)
	TObjectPtr<AChaosImpactDriftingFlakes> SpawnedFlakes;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AChaosImpactSimaePerch>> SpawnedBirds;

	UPROPERTY()
	TObjectPtr<UStaticMesh> BoxMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> ConeMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> ChamferMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> CylinderMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> RingMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> FlatMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> GlowMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> PlanksMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> PaleStoneMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> DarkStoneMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> GoldMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> GlossPaintMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> FieldSoftMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> PineMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> BarkMaterial;

	double NextWindAt = -1.0;
	bool bWindBlowing = false;
	int32 LastWindDir = -1;
	float LookClock = 0.0f;
	float CurrentVaneYaw = 0.0f;
};
