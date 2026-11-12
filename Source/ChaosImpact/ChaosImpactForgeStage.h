#pragma once

#include "CoreMinimal.h"
#include "ChaosImpactStageBase.h"
#include "ChaosImpactForgeStage.generated.h"

class AChaosImpactBall;
class AChaosImpactCharacter;
class UBoxComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPointLightComponent;
class UPostProcessComponent;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * VS ステージ6 (溶鉱炉コア・ファクトリー): a heavy industrial foundry over a sea of magma for up to 8 players. One wide
 * steel deck (an octagon), four-fold symmetric so no side is better and with room to move everywhere:
 *  - the core in the middle: a low crucible (its ball pad only ever gives special elemental / drive balls) with four
 *    thermal vents round it and four steel baffles turned 45 deg for bank shots
 *  - four foundry bays north, south, east and west, each in its own colour, with a container and coolant tanks for cover
 *  - four corner cooling chambers behind low blast walls, and low slag barriers between them and the bays
 *  - Thermal Vents (サーマル・ベント): every 11-14 seconds a facing pair of vents turns into a pale red danger area that
 *    fills toward its edge for 2.4 s, then erupts for 1.6 s: whoever is on it (not dashing) takes 1 and is thrown up,
 *    and a flying ball through the plume speeds up by 1.35x.
 * The deck's edge holds players in (an invisible fence that stops characters only: a ball thrown off it falls in).
 */
UCLASS(Blueprintable, meta=(DisplayName="Chaos Impact Forge Stage"))
class AChaosImpactForgeStage : public AChaosImpactStageBase
{
	GENERATED_BODY()

public:
	AChaosImpactForgeStage();
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual TArray<FVector> GetSpawnPoints() const override;
	virtual TArray<FVector> GetBallPoints() const override;
	virtual float GetHalfExtent() const override { return ForgeHalfExtent; }
	virtual bool IsOutside(const FVector& Location) const override;

	static constexpr float LayoutHalfExtent = 3500.0f;
	static constexpr float LayoutScale = 1.0f;
	static constexpr float ForgeHalfExtent = LayoutHalfExtent * LayoutScale;
	static constexpr float VentRadius = 330.0f;

	/** Seconds between one Thermal Vent eruption cycle and the next (server). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Forge Stage|Gimmick", meta=(ClampMin="4.0"))
	float VentIntervalMin = 11.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Forge Stage|Gimmick", meta=(ClampMin="4.0"))
	float VentIntervalMax = 14.0f;

	/** Delay before the first Thermal Vent cycle after match GO. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Forge Stage|Gimmick", meta=(ClampMin="0.0"))
	float FirstVentAfter = 7.5f;

	/** Warning duration before the vents erupt. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Forge Stage|Gimmick", meta=(ClampMin="0.5"))
	float VentWarnSeconds = 2.4f;

	/** Duration of the thermal eruption column. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Forge Stage|Gimmick", meta=(ClampMin="0.5"))
	float VentEruptSeconds = 1.6f;

	/** Damage dealt to a non-dashing character caught on an erupting Thermal Vent. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Forge Stage|Gimmick", meta=(ClampMin="0.0"))
	float VentDamage = 1.0f;

	/** Speed multiplier applied to flying balls that pass through an erupting Thermal Vent plume. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Forge Stage|Gimmick", meta=(ClampMin="1.0", ClampMax="2.0"))
	float VentBallBoost = 1.35f;

	/** Where players appear, relative to the stage, on the floor surface. Draggable in the viewport. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Forge Stage", meta=(MakeEditWidget))
	TArray<FVector> SpawnPointOffsets;

	/** Ball pad positions, relative to the stage, on the floor surface. Draggable in the viewport. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Forge Stage", meta=(MakeEditWidget))
	TArray<FVector> BallPointOffsets;

	/** Off: blocks keep the materials assigned in the editor instead of applying the palette below. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Forge Stage|Palette")
	bool bApplyDefaultColors = true;

	/** Quarter industrial accent colours (Molten Orange, Coolant Cyan, Hazard Gold, Forge Crimson). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Forge Stage|Palette")
	TArray<FLinearColor> BayColors;

	/** Heavy steel chequer-plate floor colour. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Forge Stage|Palette")
	FLinearColor SteelFloorColor = FLinearColor(0.22f, 0.23f, 0.27f);

	/** Concrete platform & catwalk colour. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Forge Stage|Palette")
	FLinearColor ConcreteColor = FLinearColor(0.24f, 0.23f, 0.22f);

	/** Steel baffle reflector walls and rims. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Forge Stage|Palette")
	FLinearColor BaffleColor = FLinearColor(0.32f, 0.34f, 0.38f);

	/** Safety yellow stripes along edges and ramps. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Forge Stage|Palette")
	FLinearColor StripeColor = FLinearColor(1.0f, 0.68f, 0.02f);

	/** Molten seams, rings and furnace glow. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Forge Stage|Palette")
	FLinearColor MagmaGlowColor = FLinearColor(1.0f, 0.26f, 0.02f);

	/** The magma sea far below, glowing between its crust (kept deep so it does not flood the view). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Forge Stage|Palette")
	FLinearColor MagmaSeaColor = FLinearColor(0.42f, 0.055f, 0.008f);

	/** Coolant pipe and chamber accent glow. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Forge Stage|Palette")
	FLinearColor CoolantGlowColor = FLinearColor(0.05f, 0.82f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Forge Stage|Palette", meta=(ClampMin="0.0"))
	float GlowIntensity = 3.0f;

	/** Distant foundry structures and basalt rock. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Forge Stage|Palette")
	FLinearColor SceneryColor = FLinearColor(0.06f, 0.055f, 0.07f);

	static FVector GetVentOffset(int32 Vent);
	bool IsVentInPair(int32 Vent, int32 Pair) const;
	float GetVentProgress() const;
	void StartVentPair(int32 Pair);

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnRep_VentState();

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastVentErupt(int32 Pair);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastBallSuperheated(FVector_NetQuantize At);

	/** -1: idle; 0: North+South vents (0 & 2); 1: East+West vents (1 & 3). */
	UPROPERTY(ReplicatedUsing=OnRep_VentState)
	int32 ActiveVentPair = -1;
	/** The warning whose effects have been started (its start time). */
	float VentWarnFxStartedAt = -1.0f;

	/** Server world time when the current vent warning began. */
	UPROPERTY(ReplicatedUsing=OnRep_VentState)
	float VentStartedAt = 0.0f;

private:
	enum EPaint : uint8
	{
		SteelFloor, Concrete, Baffle, Stripe, MagmaGlow, CoolantGlow, Scenery, DarkSteel, Gold, MagmaSea,
		Bay, BayDark = Bay + 4, BayGlow = BayDark + 4
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
	void AddFence(const FString& Name, const FVector& From, const FVector& To);
	void AddContainer(const FString& Name, uint8 Paint, uint8 DarkPaint, const FVector& Location, const FVector& Size, float Yaw);

	/** The deck (an octagon) with its skirt, legs and edge, the core and the crucible in its middle, and the magma sea. */
	void BuildFloor();
	void BuildQuarter(int32 Quarter);
	void BuildThermalVent(int32 Vent);
	void BuildFoundryScenery(int32 Quarter);

	void ApplyPalette();
	FLinearColor GetPaintColor(uint8 Paint) const;
	static bool IsGlowPaint(uint8 Paint);
	static bool ReadPaint(const UStaticMeshComponent* Block, uint8& OutPaint);

	void TickVentsServer(double Now);
	void TickCrucibleServer();
	void UpdateVentsLook(float DeltaSeconds);
	bool IsMatchOn() const;
	double GetServerNow() const;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TArray<TObjectPtr<USceneComponent>> PistonRoots;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TArray<TObjectPtr<USceneComponent>> LadleRoots;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TArray<TObjectPtr<UStaticMeshComponent>> VentDiscs;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TArray<TObjectPtr<UStaticMeshComponent>> VentRings;

	/** Each vent's danger area, shown only while it is about to erupt (the boss's ground warning). */
	UPROPERTY(VisibleAnywhere, Category="Components")
	TArray<TObjectPtr<UStaticMeshComponent>> VentWarnAreas;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TArray<TObjectPtr<UStaticMeshComponent>> VentEruptBeams;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TArray<TObjectPtr<UPointLightComponent>> VentLights;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TArray<TObjectPtr<UStaticMeshComponent>> CrucibleRings;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UBoxComponent> GradeVolume;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UPostProcessComponent> Grade;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> VentDiscLooks;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> VentEruptLooks;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> VentWarnLooks;

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
	TObjectPtr<UStaticMesh> PlaneMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> FlatMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> GlowMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> MetalFloorMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> ConcreteMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> PanelMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> CorrugatedMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> PaintMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> GoldMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> FieldAmberMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> WarnMaterial;

	double NextVentAt = -1.0;
	bool bVentErupted = false;
	int32 LastVentPair = -1;
	double NextCrucibleCheckAt = 0.0;
	float LookClock = 0.0f;
	TSet<TWeakObjectPtr<AChaosImpactCharacter>> VentVictims;
	TSet<TWeakObjectPtr<AChaosImpactBall>> BoostedBalls;
};
