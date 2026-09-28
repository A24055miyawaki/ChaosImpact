#pragma once

#include "CoreMinimal.h"
#include "ChaosImpactStageBase.h"
#include "ChaosImpactSplashStage.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class UMaterialInterface;
class UStaticMesh;

/**
 * VS ステージ2 (no name yet): a harbour pier for up to 8 players, built on height differences like a Splatoon map.
 * About 7800 across (roughly 14 s to cross on foot; about 1.4 times the standard stage's floor), an octagon
 * on the water rather than a square box.
 *
 * Each quarter is the same, turned 90 degrees (four-fold symmetry, so no side is better):
 *  - a high terrace against the wall (260) where players start, a slope down to a balcony (110) and
 *    slopes from the balcony's sides to the ground; the fronts are drops you can jump down
 *  - a warehouse on the corner, a container wall in front of the balcony and low walls you can jump
 *  - in the middle a raised diamond plaza (150) with a slope on each face and a fountain on top
 *  - outside the walls, across the water, a town on piers (Cartoon City Free: buildings, palms, cars,
 *    billboards); billboards on the walls and graffiti on their inner faces; an animated ocean (Water Materials)
 * Balls thrown in the default arc fall onto lower ground, so the high places are worth taking.
 *
 * Editing: BP_SplashStage (Content/ChaosImpact/Versus) is a Blueprint of this class. Every block is an
 * editable component, meshes can be added there, spawn / ball points are draggable handles in the viewport,
 * and the colours are the Palette properties (turn off Apply Default Colors to keep materials set by hand).
 */
UCLASS(Blueprintable, meta=(DisplayName="Chaos Impact Splash Stage"))
class AChaosImpactSplashStage : public AChaosImpactStageBase
{
	GENERATED_BODY()

public:
	AChaosImpactSplashStage();
	virtual void OnConstruction(const FTransform& Transform) override;

	virtual TArray<FVector> GetSpawnPoints() const override;
	virtual TArray<FVector> GetBallPoints() const override;
	virtual float GetHalfExtent() const override { return SplashHalfExtent; }

	/**
	 * The layout is written at LayoutHalfExtent and spread out by LayoutScale across the ground (heights stay):
	 * change LayoutScale to make the whole stage bigger or smaller.
	 */
	static constexpr float LayoutHalfExtent = 3400.0f;
	static constexpr float LayoutScale = 1.15f;

	/** Half the floor width inside the outer walls. */
	static constexpr float SplashHalfExtent = LayoutHalfExtent * LayoutScale;

	/** Where players appear, relative to the stage, on the floor surface. Draggable in the viewport. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Splash Stage", meta=(MakeEditWidget))
	TArray<FVector> SpawnPointOffsets;

	/** Ball pad positions, relative to the stage, on the floor surface. Draggable in the viewport. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Splash Stage", meta=(MakeEditWidget))
	TArray<FVector> BallPointOffsets;

	/** Off: the blocks keep the materials set in the editor instead of the palette below. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Splash Stage|Palette")
	bool bApplyDefaultColors = true;

	/** One accent colour per quarter: terraces, containers and trims (the four teams' corners). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Splash Stage|Palette")
	TArray<FLinearColor> InkColors;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Splash Stage|Palette")
	FLinearColor FloorColor = FLinearColor(0.2f, 0.21f, 0.26f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Splash Stage|Palette")
	FLinearColor FloorGridColor = FLinearColor(0.1f, 0.11f, 0.15f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Splash Stage|Palette")
	FLinearColor PlazaColor = FLinearColor(0.34f, 0.35f, 0.42f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Splash Stage|Palette")
	FLinearColor WallColor = FLinearColor(0.035f, 0.04f, 0.1f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Splash Stage|Palette")
	FLinearColor TrimColor = FLinearColor(0.85f, 0.87f, 0.92f);

	/** Emissive trims (tops of the walls, rings on the tower). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Splash Stage|Palette")
	FLinearColor GlowColor = FLinearColor(0.1f, 0.75f, 1.0f);

	/** How bright every glowing part is (the ink-coloured trims too). Around 2 glows without washing out. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Splash Stage|Palette", meta=(ClampMin="0"))
	float GlowIntensity = 2.5f;

	/** The sea around the pier: its deep colour (and the sea bed under it). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Splash Stage|Palette")
	FLinearColor WaterColor = FLinearColor(0.0f, 0.12f, 0.3f);

	/** The shallow, sunlit colour of the animated ocean (Water Materials). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Splash Stage|Palette")
	FLinearColor OceanColor = FLinearColor(0.0f, 0.55f, 0.75f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Splash Stage|Palette")
	FLinearColor SceneryColor = FLinearColor(0.05f, 0.055f, 0.1f);

protected:
	virtual void BeginPlay() override;

private:
	/** How a block is painted. Ink slots add the quarter (0-3). */
	enum EPaint : uint8
	{
		Floor, OuterFloor, Plaza, Wall, Trim, Glow, Scenery, Water, Ink, InkDark = Ink + 4, InkGlow = InkDark + 4, InkSplat = InkGlow + 4
	};

	/** A block with a centred mesh: Location is its middle, Size its full width / depth / height. */
	UStaticMeshComponent* AddBox(const FString& Name, uint8 Paint, const FVector& Location, const FVector& Size,
		float Yaw = 0.0f, bool bCollision = true, bool bChamfer = false);
	/** An upright cylinder: Location is the middle of its base, Diameter / Height its size. */
	UStaticMeshComponent* AddCylinder(const FString& Name, uint8 Paint, const FVector& Location, float Diameter,
		float Height, bool bCollision = true);
	/** A flat glowing ring (the jump pad band) lying on the floor or around the tower. */
	UStaticMeshComponent* AddRing(const FString& Name, uint8 Paint, const FVector& Location, float Radius, float Thickness);
	/**
	 * A walkable slope: a thick slab whose top runs from Low to High (both on its centre line), Width across.
	 * The slab reaches down into the ground, so from the side it reads as a solid ramp.
	 */
	UStaticMeshComponent* AddRamp(const FString& Name, uint8 Paint, const FVector& Low, const FVector& High, float Width);
	/**
	 * A model from the Cartoon City pack, with its own materials (never repainted by the palette).
	 * Location is in stage units (not spread by LayoutScale); nothing is added when the mesh is missing.
	 */
	UStaticMeshComponent* AddProp(const FString& Name, UStaticMesh* Mesh, const FVector& Location, float Yaw, float Scale,
		bool bCollision);
	/** The town across the water behind this quarter's wall. */
	void BuildTown(int32 Quarter);
	UStaticMeshComponent* AddPart(const FString& Name, uint8 Paint, UStaticMesh* Mesh, bool bCollision);

	/** Builds one quarter of the layout, turned Quarter * 90 degrees around the middle. */
	void BuildQuarter(int32 Quarter);
	void ApplyPalette();
	FLinearColor GetPaintColor(uint8 Paint) const;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<USceneComponent> SceneRoot;

	/**
	 * How a block is painted is kept on the block itself, as a component tag "CIPaint<N>", so it can never go
	 * out of step with the blocks (a Blueprint may save its own copy of any list kept on the actor).
	 * Meshes added in the Blueprint have no such tag and keep their own materials.
	 */
	static bool ReadPaint(const UStaticMeshComponent* Block, uint8& OutPaint);

	UPROPERTY()
	TObjectPtr<UStaticMesh> BoxMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> ChamferMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CylinderMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> RingMesh;

	/** Cartoon City Free models, by ECity. */
	enum ECity : uint8
	{
		TwistedTower, EcoSlope, EcoTerrace, Billboard, BillboardText, BillboardStand, Fountain, Graffiti, Spotlight,
		Dumpster, TrashBin, Hedge, Palm, TileA, TileB, TileC, CarA, CarB, Van, BusStop, CityCount
	};

	UPROPERTY()
	TArray<TObjectPtr<UStaticMesh>> CityMeshes;

	/** Water Materials (Fab, tharlevfx, CC BY 4.0): the animated ocean. Without it the sea is a flat glossy slab. */
	UPROPERTY()
	TObjectPtr<UStaticMesh> WaterPlaneMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> OceanMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> FlatMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> GridMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> GlowMaterial;

	/** Stylized Metallic Floor (Fab, Studio94Roots) made into M_StageMetalFloor: the raised decks and plaza. */
	UPROPERTY()
	TObjectPtr<UMaterialInterface> MetalFloorMaterial;
};
