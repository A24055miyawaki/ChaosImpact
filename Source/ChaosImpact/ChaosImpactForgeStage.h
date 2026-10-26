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
class UTextRenderComponent;

/**
 * VS ステージ6 (溶鉱炉コア・ファクトリー): a heavy industrial foundry suspended over a sea of magma for up to 8 players.
 * Built with strict four-fold rotational symmetry:
 *  - Central Circular Core Deck (+60 cm): a wide steel turntable deck holding a central Crucible Altar (whose ball
 *    spawner always produces special elemental / drive balls) and four Thermal Vent grates.
 *  - Four Cardinal Foundry Platforms (0 cm) behind corrugated container cover, connected to the Core Deck by wide
 *    sloped steel ramps.
 *  - Four Corner Cooling Chambers (0 cm) connected by eight outer catwalk bridges and four diagonal walkways, with
 *    angled Steel Baffle reflector walls for pinball-style bank shots.
 *  - Invisible character-only safety fences around the platform edges over the magma sea (balls thrown off fall in).
 *  - Thermal Vents (サーマル・ベント): every 11-14 seconds, a diagonal pair of vents warns for 2.4 s and erupts for
 *    1.6 s, launching non-dashing characters on the grate and boosting any flying ball passing through by 1.35x.
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
	static constexpr float CoreDeckZ = 60.0f;
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
	FLinearColor SteelFloorColor = FLinearColor(0.11f, 0.12f, 0.15f);

	/** Concrete platform & catwalk colour. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Forge Stage|Palette")
	FLinearColor ConcreteColor = FLinearColor(0.24f, 0.23f, 0.22f);

	/** Steel baffle reflector walls and rims. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Forge Stage|Palette")
	FLinearColor BaffleColor = FLinearColor(0.32f, 0.34f, 0.38f);

	/** Safety yellow stripes along edges and ramps. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Forge Stage|Palette")
	FLinearColor StripeColor = FLinearColor(1.0f, 0.68f, 0.02f);

	/** Molten magma and furnace core glow. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Forge Stage|Palette")
	FLinearColor MagmaGlowColor = FLinearColor(1.0f, 0.26f, 0.02f);

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

	/** Server world time when the current vent warning began. */
	UPROPERTY(ReplicatedUsing=OnRep_VentState)
	float VentStartedAt = 0.0f;

private:
	enum EPaint : uint8
	{
		SteelFloor, Concrete, Baffle, Stripe, MagmaGlow, CoolantGlow, Scenery, DarkSteel, Gold,
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

	void BuildCoreDeck();
	void BuildQuarter(int32 Quarter);
	void BuildThermalVent(int32 Vent);
	void BuildFoundryScenery(int32 Quarter);

	void ApplyPalette();
	FLinearColor GetPaintColor(uint8 Paint) const;
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

	UPROPERTY(VisibleAnywhere, Category="Components")
	TArray<TObjectPtr<UStaticMeshComponent>> VentWarnBeams;

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
	TObjectPtr<UMaterialInterface> StencilMaterial;

	double NextVentAt = -1.0;
	bool bVentErupted = false;
	int32 LastVentPair = -1;
	double NextCrucibleCheckAt = 0.0;
	float LookClock = 0.0f;
	TSet<TWeakObjectPtr<AChaosImpactCharacter>> VentVictims;
	TSet<TWeakObjectPtr<AChaosImpactBall>> BoostedBalls;
};
