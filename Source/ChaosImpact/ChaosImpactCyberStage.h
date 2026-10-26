#pragma once

#include "CoreMinimal.h"
#include "ChaosImpactStageBase.h"
#include "ChaosImpactCyberStage.generated.h"

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
 * VS ステージ4 (ネオン・ハイウェイ交差点): a night-time cybernetic highway junction for up to 8 players.
 * Built with strict four-fold rotational symmetry so every spawn point has identical access to the central
 * hologram court, the elevated outer highway lanes, corner retreat pockets, and ball pads.
 *
 * Layout & tactical features:
 *  - Central Hologram Court (+70 cm): an octagonal raised stage reached by four wide cardinal ramps, surrounded by
 *    four angled reflector pylons and four 45-degree bank-shot walls.
 *  - Outer Highway Overpasses (+120 cm): four perimeter lanes with two wide side ramps each, overlooking the main
 *    asphalt deck and offering flanking routes and cover behind vehicles and bus stops.
 *  - Corner Retreat Pockets (0 cm): sheltered by L-shaped neon sound barriers and urban props.
 *  - Pulse Barrier Gates (パルス・バリアゲート): four diagonal energy gates between the central court and corners,
 *    synchronized across all machines via server world time. While closed, they bounce flying balls with a 1.25x
 *    speed boost (creating dynamic bank-shot angles) and knock back non-dashing characters, while dashing characters
 *    pass through unharmed.
 *
 * Editing: make a Blueprint of this class or edit the actor placed in Lvl_VS_Stage4. Every block has
 * bEditableWhenInherited = true and a CIPaint<N> tag; turn off bApplyDefaultColors to keep custom editor materials.
 */
UCLASS(Blueprintable, meta=(DisplayName="Chaos Impact Cyber Stage"))
class AChaosImpactCyberStage : public AChaosImpactStageBase
{
	GENERATED_BODY()

public:
	AChaosImpactCyberStage();
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual TArray<FVector> GetSpawnPoints() const override;
	virtual TArray<FVector> GetBallPoints() const override;
	virtual float GetHalfExtent() const override { return CyberHalfExtent; }
	virtual bool IsOutside(const FVector& Location) const override;

	static constexpr float LayoutHalfExtent = 3400.0f;
	static constexpr float LayoutScale = 1.0f;
	static constexpr float CyberHalfExtent = LayoutHalfExtent * LayoutScale;
	static constexpr float BevelLine = 5200.0f;
	static constexpr float GateHalfWidth = 310.0f;
	static constexpr float GateHeight = 320.0f;

	/** Seconds between one Pulse Barrier Gate cycle and the next (server). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Cyber Stage|Gimmick", meta=(ClampMin="4.0"))
	float GateIntervalMin = 11.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Cyber Stage|Gimmick", meta=(ClampMin="4.0"))
	float GateIntervalMax = 14.0f;

	/** Delay before the first gate cycle after match GO. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Cyber Stage|Gimmick", meta=(ClampMin="0.0"))
	float FirstGateAfter = 7.0f;

	/** Warning duration before the barrier gates lock in. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Cyber Stage|Gimmick", meta=(ClampMin="0.5"))
	float GateWarnSeconds = 2.5f;

	/** How long the barrier gates remain closed and reflective. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Cyber Stage|Gimmick", meta=(ClampMin="1.0"))
	float GateActiveSeconds = 6.0f;

	/** Speed multiplier applied to balls that bank off an active Pulse Barrier Gate. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Cyber Stage|Gimmick", meta=(ClampMin="1.0", ClampMax="2.0"))
	float GateBallBoost = 1.25f;

	/** Damage dealt to a non-dashing character who touches an active barrier gate. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Cyber Stage|Gimmick", meta=(ClampMin="0.0"))
	float GateContactDamage = 1.0f;

	/** Where players appear, relative to the stage, on the floor surface. Draggable in the viewport. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Cyber Stage", meta=(MakeEditWidget))
	TArray<FVector> SpawnPointOffsets;

	/** Ball pad positions, relative to the stage, on the floor surface. Draggable in the viewport. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Cyber Stage", meta=(MakeEditWidget))
	TArray<FVector> BallPointOffsets;

	/** Off: blocks keep the materials assigned in the editor instead of applying the palette below. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Cyber Stage|Palette")
	bool bApplyDefaultColors = true;

	/** One neon accent colour per quarter (Cyan, Magenta, Amber, Lime). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Cyber Stage|Palette")
	TArray<FLinearColor> NeonColors;

	/** Dark asphalt road surface. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Cyber Stage|Palette")
	FLinearColor AsphaltColor = FLinearColor(0.025f, 0.032f, 0.055f);

	/** Raised central court and overpass metal deck. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Cyber Stage|Palette")
	FLinearColor DeckColor = FLinearColor(0.06f, 0.085f, 0.14f);

	/** Perimeter sound-barrier walls. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Cyber Stage|Palette")
	FLinearColor WallColor = FLinearColor(0.018f, 0.028f, 0.055f);

	/** Ramps, kerbs, and bank-shot reflector walls. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Cyber Stage|Palette")
	FLinearColor TrimColor = FLinearColor(0.14f, 0.18f, 0.28f);

	/** Road lane stripes and crosswalk markings. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Cyber Stage|Palette")
	FLinearColor StripeColor = FLinearColor(0.85f, 0.92f, 1.0f);

	/** Primary cyan hologram glow. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Cyber Stage|Palette")
	FLinearColor GlowColor = FLinearColor(0.0f, 0.82f, 1.0f);

	/** Emissive intensity for neon trims. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Cyber Stage|Palette", meta=(ClampMin="0.0"))
	float GlowIntensity = 3.2f;

	/** Distant highway and building structures. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Cyber Stage|Palette")
	FLinearColor SceneryColor = FLinearColor(0.04f, 0.055f, 0.1f);

	/** Stage-relative center of Gate (0..3: NE, SE, SW, NW) on the floor, and its inward normal. */
	static FVector GetGateOffset(int32 Gate);
	static FVector GetGateNormal(int32 Gate);
	bool IsGateInPattern(int32 Gate, int32 Pattern) const;
	float GetGateProgress() const;
	void StartGatePattern(int32 Pattern);

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnRep_GateState();

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastGateActivated(int32 Pattern);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastGateBounce(FVector_NetQuantize At, int32 Gate);

	/** -1: all gates open; 0: NE+SW pair; 1: SE+NW pair; 2: all four gates. */
	UPROPERTY(ReplicatedUsing=OnRep_GateState)
	int32 GatePattern = -1;

	/** Server world time when the current gate warning began. */
	UPROPERTY(ReplicatedUsing=OnRep_GateState)
	float GateStartedAt = 0.0f;

private:
	enum EPaint : uint8
	{
		Asphalt, Deck, Wall, Trim, Glow, Scenery, Stripe, Steel,
		Neon, NeonDark = Neon + 4, NeonGlow = NeonDark + 4
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
	UStaticMeshComponent* AddProp(const FString& Name, UStaticMesh* Mesh, const FVector& Location, float Yaw, float Scale,
		bool bCollision);

	void BuildCenterCourt();
	void BuildQuarter(int32 Quarter);
	void BuildPulseGate(int32 Gate);
	void BuildCyberCity(int32 Quarter);

	void ApplyPalette();
	FLinearColor GetPaintColor(uint8 Paint) const;
	static bool ReadPaint(const UStaticMeshComponent* Block, uint8& OutPaint);

	void TickGatesServer(double Now);
	void UpdateGatesLook(float DeltaSeconds);
	bool IsMatchOn() const;
	double GetServerNow() const;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<USceneComponent> HoloRingRoot;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TArray<TObjectPtr<USceneComponent>> SearchlightRoots;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TArray<TObjectPtr<USceneComponent>> DroneRoots;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TArray<TObjectPtr<UStaticMeshComponent>> GateFields;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TArray<TObjectPtr<UStaticMeshComponent>> GateTopBars;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TArray<TObjectPtr<UPointLightComponent>> GateLights;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UBoxComponent> GradeVolume;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UPostProcessComponent> Grade;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> GateFieldLooks;

	enum ECity : uint8
	{
		TwistedTower, EcoSlope, EcoTerrace, Billboard, BillboardText, BillboardStand, Graffiti, Spotlight,
		Dumpster, TrashBin, TileA, TileB, TileC, CarA, CarB, Van, BusStop, CityCount
	};

	UPROPERTY()
	TArray<TObjectPtr<UStaticMesh>> CityMeshes;

	UPROPERTY()
	TObjectPtr<UStaticMesh> BoxMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> ChamferMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> CylinderMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> RingMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> FlatMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> GridMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> GlowMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> MetalFloorMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> ConcreteMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> PanelMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> PaintMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> FieldTealMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> FieldAmberMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> StencilMaterial;

	double NextGateAt = -1.0;
	bool bGateLocked = false;
	int32 LastGatePattern = -1;
	float LookClock = 0.0f;
	TMap<TWeakObjectPtr<AChaosImpactCharacter>, double> GateHitCooldown;
	TMap<TWeakObjectPtr<AChaosImpactBall>, FVector> BallLastLocation;
	TMap<TWeakObjectPtr<AChaosImpactBall>, double> BallBounceCooldown;
};
