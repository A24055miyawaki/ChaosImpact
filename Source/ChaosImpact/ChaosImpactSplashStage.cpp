#include "ChaosImpactSplashStage.h"

#include "Components/BoxComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// Heights of the tiers, floor at 0.
	constexpr float TerraceTop = 260.0f;
	constexpr float BalconyTop = 110.0f;
	constexpr float PlazaTop = 150.0f;
	/** The corner walls run along X + Y = BevelLine (an octagon, not a square). */
	constexpr float BevelLine = 4900.0f;
	/** Outer walls: well above the terraces and an arcing ball, so throws bank off them. */
	constexpr float WallHeight = 640.0f;

	/** Offsets in the first quarter (towards +X); the other three are the same turned 90 degrees each. */
	const FVector QuarterSpawnOffsets[] =
	{
		FVector(3000.0f, 0.0f, TerraceTop),   // on the high terrace
		FVector(2600.0f, 1650.0f, 0.0f),      // the corner lane beside the warehouse
		FVector(1650.0f, -600.0f, 0.0f)       // mid ground behind the container wall
	};

	const FVector QuarterBallOffsets[] =
	{
		FVector(2150.0f, 650.0f, BalconyTop), // balcony
		FVector(1000.0f, -450.0f, 0.0f),      // mid ground
		FVector(430.0f, 0.0f, PlazaTop)       // the plaza, beside the fountain
	};

	FVector Turn(const FVector& Offset, const int32 Quarter)
	{
		return FRotator(0.0f, 90.0f * Quarter, 0.0f).RotateVector(Offset);
	}

	/** Layout units to stage units: spread across the ground, heights unchanged. */
	FVector Spread(const FVector& Layout)
	{
		return FVector(Layout.X * AChaosImpactSplashStage::LayoutScale, Layout.Y * AChaosImpactSplashStage::LayoutScale, Layout.Z);
	}
}

AChaosImpactSplashStage::AChaosImpactSplashStage()
{
	PrimaryActorTick.bCanEverTick = false;
	// Clients move their own characters, so they need the walls themselves, not just the server.
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);
	SetNetUpdateFrequency(1.0f);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	TideRoot = CreateDefaultSubobject<USceneComponent>(TEXT("TideRoot"));
	TideRoot->SetupAttachment(SceneRoot);

	InkColors = {
		FLinearColor(1.0f, 0.05f, 0.42f),   // pink
		FLinearColor(0.0f, 0.6f, 1.0f),     // cyan
		FLinearColor(0.5f, 1.0f, 0.02f),    // lime
		FLinearColor(1.0f, 0.33f, 0.0f)     // orange
	};
	for (int32 Quarter = 0; Quarter < 4; ++Quarter)
	{
		for (const FVector& Offset : QuarterSpawnOffsets)
		{
			SpawnPointOffsets.Add(Spread(Turn(Offset, Quarter)));
		}
		for (const FVector& Offset : QuarterBallOffsets)
		{
			BallPointOffsets.Add(Spread(Turn(Offset, Quarter)));
		}
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Box(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Chamfer(TEXT("/Game/LevelPrototyping/Meshes/SM_ChamferCube.SM_ChamferCube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Game/LevelPrototyping/Meshes/SM_Cylinder.SM_Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Ring(
		TEXT("/Game/LevelPrototyping/Interactable/JumpPad/Assets/Meshes/SM_CircularBand.SM_CircularBand"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Flat(TEXT("/Game/LevelPrototyping/Materials/M_FlatCol.M_FlatCol"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Grid(
		TEXT("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_Gray.MI_PrototypeGrid_Gray"));
	// Unlit, Emissive = Color x Intensity (made for this stage).
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Glowing(TEXT("/Game/ChaosImpact/Versus/M_StageGlow.M_StageGlow"));
	BoxMesh = Box.Object;
	ChamferMesh = Chamfer.Succeeded() ? Chamfer.Object : Box.Object;
	CylinderMesh = Cylinder.Object;
	RingMesh = Ring.Object;
	FlatMaterial = Flat.Object;
	GridMaterial = Grid.Succeeded() ? Grid.Object : Flat.Object;
	GlowMaterial = Glowing.Succeeded() ? Glowing.Object : Flat.Object;
	// Chequer-plate metal for the raised decks and plaza (world-aligned, so it never stretches).
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MetalFloor(
		TEXT("/Game/ChaosImpact/Versus/M_StageMetalFloor.M_StageMetalFloor"));
	MetalFloorMaterial = MetalFloor.Object;
	// The harbour's own (world-aligned, coloured by "Color"): deck boards, concrete, painted steel, containers, gloss paint.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Planks(TEXT("/Game/ChaosImpact/Versus/Splash/M_CI_Planks.M_CI_Planks"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Concrete(TEXT("/Game/ChaosImpact/Versus/Splash/M_CI_Concrete.M_CI_Concrete"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Panel(TEXT("/Game/ChaosImpact/Versus/Splash/M_CI_PanelWall.M_CI_PanelWall"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Corrugated(
		TEXT("/Game/ChaosImpact/Versus/Splash/M_CI_Corrugated.M_CI_Corrugated"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Paint(TEXT("/Game/ChaosImpact/Versus/Splash/M_CI_GlossPaint.M_CI_GlossPaint"));
	PlanksMaterial = Planks.Object;
	ConcreteMaterial = Concrete.Object;
	PanelMaterial = Panel.Object;
	CorrugatedMaterial = Corrugated.Object;
	PaintMaterial = Paint.Object;
	// The engine's opaque text material: each letter cut out of the font, painted in the text's colour.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Stencil(
		TEXT("/Engine/EngineMaterials/DefaultTextMaterialOpaque.DefaultTextMaterialOpaque"));
	StencilMaterial = Stencil.Object;

	// Cartoon City Free (Fab, ithappy). Anything missing is simply left out.
	static const TCHAR* const CityPaths[] =
	{
		TEXT("Buildings/SM_Regular_Building_TwistedTower_Large"), TEXT("Buildings/SM_Eco_Building_Slope"),
		TEXT("Buildings/SM_Eco_Building_Terrace"), TEXT("Billboards/SM_Billboard_4x1_03"), TEXT("Billboards/SM_Billboard_4x1_03_Line"),
		TEXT("Billboards/SM_Billboard_2x1_05"), TEXT("Props/SM_Fountain_03"), TEXT("Props/SM_Graffiti_03"),
		TEXT("Props/SM_Spotlight_02"), TEXT("Props/SM_Trash_Can_04"), TEXT("Props/SM_Trash_03"), TEXT("Vegetation/SM_Bush_10"),
		TEXT("Vegetation/SM_Palm_03"), TEXT("Sidewalks/SM_Set_B_Tiles_01"), TEXT("Sidewalks/SM_Set_B_Tiles_04"),
		TEXT("Sidewalks/SM_Set_B_Tiles_05"), TEXT("Cars/SM_Car_06"), TEXT("Cars/SM_Car_16"), TEXT("Cars/SM_Van"),
		TEXT("Props/SM_Bus_Stop_02")
	};
	static_assert(UE_ARRAY_COUNT(CityPaths) == CityCount, "one path per ECity");
	// Water Materials (Fab, tharlevfx, CC BY 4.0 - credited in Docs/Credits.md).
	static ConstructorHelpers::FObjectFinder<UStaticMesh> WaterPlane(TEXT("/Game/WaterMaterials/Meshes/SM_Water_Plane.SM_Water_Plane"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Ocean(TEXT("/Game/WaterMaterials/Materials/M_Ocean.M_Ocean"));
	WaterPlaneMesh = WaterPlane.Object;
	OceanMaterial = Ocean.Object;
	for (const TCHAR* Path : CityPaths)
	{
		const FString Full = FString::Printf(TEXT("/Game/Cartoon_City_Free/Meshes/%s.%s"), Path, *FPaths::GetBaseFilename(Path));
		ConstructorHelpers::FObjectFinder<UStaticMesh> Finder(*Full);
		CityMeshes.Add(Finder.Object);
	}

	constexpr float Half = LayoutHalfExtent;

	// The sea, and the pier: an octagon made of a cross of two slabs and a corner piece per quarter (below).
	// An animated ocean (see-through, foaming where it meets the pier) over a dark sea bed; a flat slab without it.
	if (UStaticMeshComponent* Sea = AddProp(TEXT("Sea"), OceanMaterial ? WaterPlaneMesh.Get() : nullptr,
		FVector(0.0f, 0.0f, -85.0f), 0.0f, 20000.0f / 1500.0f, false))
	{
		Sea->SetMaterial(0, OceanMaterial);
		Sea->SetCastShadow(false);
		// ApplyPalette tints it with Water Color.
		Sea->ComponentTags.Add(TEXT("CIOcean"));
		Sea->SetupAttachment(TideRoot);
		AddBox(TEXT("SeaBed"), Water, FVector(0.0f, 0.0f, -700.0f), FVector(20000.0f, 20000.0f, 20.0f), 0.0f, false)->SetupAttachment(TideRoot);
	}
	else
	{
		AddBox(TEXT("Sea"), Water, FVector(0.0f, 0.0f, -95.0f), FVector(20000.0f, 20000.0f, 20.0f), 0.0f, false)->SetupAttachment(TideRoot);
	}
	AddBox(TEXT("PierX"), Floor, FVector(0.0f, 0.0f, -50.0f), FVector(Half * 2.0f, 3000.0f, 100.0f));
	AddBox(TEXT("PierY"), Floor, FVector(0.0f, 0.0f, -50.0f), FVector(3000.0f, Half * 2.0f, 100.0f));

	// Middle: a raised diamond plaza (a slope on each face, built per quarter) with a tower on top.
	AddBox(TEXT("Plaza"), Plaza, FVector(0.0f, 0.0f, PlazaTop * 0.5f), FVector(1000.0f, 1000.0f, PlazaTop), 45.0f);
	// The fountain is the bank-shot centre; without the pack a tower stands there instead.
	if (!AddProp(TEXT("Fountain"), CityMeshes[Fountain], FVector(0.0f, 0.0f, PlazaTop), 0.0f, 1.0f, true))
	{
		AddCylinder(TEXT("Tower"), Trim, FVector(0.0f, 0.0f, PlazaTop), 280.0f, 320.0f);
		AddRing(TEXT("TowerRingLow"), Glow, FVector(0.0f, 0.0f, PlazaTop + 110.0f), 146.0f, 14.0f);
		AddRing(TEXT("TowerRingHigh"), Glow, FVector(0.0f, 0.0f, PlazaTop + 230.0f), 146.0f, 14.0f);
	}
	AddRing(TEXT("PlazaRing"), Glow, FVector(0.0f, 0.0f, PlazaTop + 0.5f), 330.0f, 3.0f);

	for (int32 Quarter = 0; Quarter < 4; ++Quarter)
	{
		BuildQuarter(Quarter);
		BuildCrane(Quarter);
		BuildTown(Quarter);
	}

	// A sunny harbour: a touch more colour and contrast, a soft glow and a slight vignette, only over this stage.
	GradeVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("GradeVolume"));
	GradeVolume->SetupAttachment(SceneRoot);
	GradeVolume->SetBoxExtent(FVector(14000.0f, 14000.0f, 6000.0f));
	GradeVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GradeVolume->SetGenerateOverlapEvents(false);
	GradeVolume->SetHiddenInGame(true);
	Grade = CreateDefaultSubobject<UPostProcessComponent>(TEXT("Grade"));
	Grade->SetupAttachment(GradeVolume);
	Grade->bUnbound = false;
	FPostProcessSettings& Look = Grade->Settings;
	Look.bOverride_ColorSaturation = true;
	Look.ColorSaturation = FVector4(1.12f, 1.12f, 1.1f, 1.0f);
	Look.bOverride_ColorContrast = true;
	Look.ColorContrast = FVector4(1.06f, 1.06f, 1.06f, 1.0f);
	Look.bOverride_BloomIntensity = true;
	Look.BloomIntensity = 0.55f;
	Look.bOverride_VignetteIntensity = true;
	Look.VignetteIntensity = 0.3f;
	Look.bOverride_WhiteTemp = true;
	Look.WhiteTemp = 6200.0f;
}

UStaticMeshComponent* AChaosImpactSplashStage::AddPartAt(USceneComponent* Parent, const FString& Name, const uint8 Paint,
	UStaticMesh* Mesh, const FVector& Location, const FVector& Size, const FRotator& Rotation)
{
	UStaticMeshComponent* Part = AddPart(Name, Paint, Mesh, false);
	Part->SetupAttachment(Parent ? Parent : SceneRoot.Get());
	Part->SetRelativeLocation(Location);
	Part->SetRelativeRotation(Rotation);
	Part->SetRelativeScale3D(Size / 100.0f);
	Part->SetCastShadow(true);
	return Part;
}

void AChaosImpactSplashStage::AddContainer(const FString& Name, const uint8 Paint, const uint8 DarkPaint, const FVector& Location,
	const FVector& Size, const float Yaw)
{
	// Layout units like AddBox; the parts placed about its middle, turned with it.
	const FRotator Facing(0.0f, Yaw, 0.0f);
	const FVector Half = Size * 0.5f;
	UStaticMeshComponent* Body = AddBox(Name, Paint, Location, Size, Yaw, true, true);
	if (CorrugatedMaterial)
	{
		Body->SetMaterial(0, CorrugatedMaterial);
	}
	// Doors at both ends, each with its locking bars.
	for (const float End : {-1.0f, 1.0f})
	{
		const FString Side = End < 0.0f ? TEXT("A") : TEXT("B");
		AddBox(Name + TEXT("Door") + Side, DarkPaint, Location + Facing.RotateVector(FVector(0.0f, End * (Half.Y + 2.0f), 0.0f)),
			FVector(Size.X - 30.0f, 4.0f, Size.Z - 26.0f), Yaw, false);
		for (int32 Bar = 0; Bar < 4; ++Bar)
		{
			const float Across = (-0.3f + 0.2f * Bar) * Size.X;
			AddBox(Name + FString::Printf(TEXT("Bar%s%d"), *Side, Bar), Steel,
				Location + Facing.RotateVector(FVector(Across, End * (Half.Y + 6.0f), 0.0f)), FVector(6.0f, 6.0f, Size.Z - 40.0f), Yaw, false);
		}
	}
	// The corner fittings.
	int32 Fitting = 0;
	for (const float SX : {-1.0f, 1.0f})
	{
		for (const float SY : {-1.0f, 1.0f})
		{
			for (const float SZ : {-1.0f, 1.0f})
			{
				AddBox(Name + FString::Printf(TEXT("Corner%d"), Fitting++), Steel,
					Location + Facing.RotateVector(FVector(SX * (Half.X - 10.0f), SY * (Half.Y - 10.0f), SZ * (Half.Z - 10.0f))),
					FVector(26.0f, 26.0f, 26.0f), Yaw, false);
			}
		}
	}
}

void AChaosImpactSplashStage::BuildCrane(const int32 Quarter)
{
	// Over the water behind this quarter's terrace, in stage units about its own root (CraneRoot_Q<n>), the arm reaching
	// toward the arena over the wall; the trolley (CraneTrolley_Q<n>) carries a container on its cable.
	const FString Q = FString::Printf(TEXT("_Q%d"), Quarter);
	const uint8 GlowHere = InkGlow + Quarter;
	const uint8 DarkHere = InkDark + Quarter;
	const uint8 NextInk = Ink + (Quarter + 1) % 4;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(*(TEXT("CraneRoot") + Q));
	Root->SetupAttachment(SceneRoot);
	Root->SetRelativeLocation(Spread(Turn(FVector(LayoutHalfExtent + 700.0f, 0.0f, 0.0f), Quarter)));
	Root->SetRelativeRotation(FRotator(0.0f, 90.0f * Quarter, 0.0f));
	CraneRoots.Add(Root);
	const float Gap = 1000.0f * LayoutScale;
	for (const float Side : {-1.0f, 1.0f})
	{
		const FString Leg = Side < 0.0f ? TEXT("S") : TEXT("N");
		AddPartAt(Root, TEXT("CraneLeg") + Leg + Q, NextInk, BoxMesh.Get(), FVector(0.0f, Side * Gap, 500.0f), FVector(130.0f, 130.0f, 1240.0f));
		AddPartAt(Root, TEXT("CraneFoot") + Leg + Q, Steel, BoxMesh.Get(), FVector(0.0f, Side * Gap, -70.0f), FVector(280.0f, 280.0f, 90.0f));
	}
	// Braced between the legs, a tie low down, the beam over them with its glowing trim, the arm and the driver's cab.
	const float Brace = FVector2D(2.0f * Gap, 760.0f).Size();
	const float Tilt = FMath::RadiansToDegrees(FMath::Atan2(760.0f, 2.0f * Gap));
	AddPartAt(Root, TEXT("CraneBraceA") + Q, Steel, BoxMesh.Get(), FVector(0.0f, 0.0f, 520.0f), FVector(40.0f, Brace, 40.0f), FRotator(0.0f, 0.0f, Tilt));
	AddPartAt(Root, TEXT("CraneBraceB") + Q, Steel, BoxMesh.Get(), FVector(0.0f, 0.0f, 520.0f), FVector(40.0f, Brace, 40.0f), FRotator(0.0f, 0.0f, -Tilt));
	AddPartAt(Root, TEXT("CraneTie") + Q, Steel, BoxMesh.Get(), FVector(0.0f, 0.0f, 120.0f), FVector(50.0f, 2.0f * Gap, 50.0f));
	AddPartAt(Root, TEXT("CraneBeam") + Q, NextInk, BoxMesh.Get(), FVector(0.0f, 0.0f, 1120.0f), FVector(180.0f, 2.0f * Gap + 260.0f, 130.0f));
	AddPartAt(Root, TEXT("CraneGlow") + Q, GlowHere, BoxMesh.Get(), FVector(0.0f, 0.0f, 1188.0f), FVector(190.0f, 2.0f * Gap + 260.0f, 6.0f));
	AddPartAt(Root, TEXT("CraneArm") + Q, DarkHere, BoxMesh.Get(), FVector(-380.0f * LayoutScale, 0.0f, 1230.0f),
		FVector(1050.0f * LayoutScale, 150.0f, 90.0f));
	AddPartAt(Root, TEXT("CraneCab") + Q, Trim, BoxMesh.Get(), FVector(140.0f, Gap * 0.55f, 1270.0f), FVector(220.0f, 240.0f, 170.0f));
	AddPartAt(Root, TEXT("CraneCabWindow") + Q, Glow, BoxMesh.Get(), FVector(28.0f, Gap * 0.55f, 1295.0f), FVector(6.0f, 200.0f, 50.0f));
	USceneComponent* Trolley = CreateDefaultSubobject<USceneComponent>(*(TEXT("CraneTrolley") + Q));
	Trolley->SetupAttachment(Root);
	Trolley->SetRelativeLocation(FVector(-560.0f, 0.0f, 1160.0f));
	CraneTrolleys.Add(Trolley);
	AddPartAt(Trolley, TEXT("CraneCarriage") + Q, Steel, BoxMesh.Get(), FVector(0.0f, 0.0f, 0.0f), FVector(200.0f, 190.0f, 70.0f));
	AddPartAt(Trolley, TEXT("CraneCable") + Q, Steel, BoxMesh.Get(), FVector(0.0f, 0.0f, -205.0f), FVector(8.0f, 8.0f, 340.0f));
	AddPartAt(Trolley, TEXT("CraneSpreader") + Q, Steel, BoxMesh.Get(), FVector(0.0f, 0.0f, -385.0f), FVector(230.0f, 520.0f, 24.0f));
	UStaticMeshComponent* Load = AddPartAt(Trolley, TEXT("CraneLoad") + Q, Ink + (Quarter + 2) % 4, BoxMesh.Get(),
		FVector(0.0f, 0.0f, -517.0f), FVector(230.0f, 500.0f, 240.0f));
	if (CorrugatedMaterial)
	{
		Load->SetMaterial(0, CorrugatedMaterial);
	}
}

void AChaosImpactSplashStage::BuildQuarter(const int32 Quarter)
{
	constexpr float Half = LayoutHalfExtent;
	const FString Q = FString::Printf(TEXT("_Q%d"), Quarter);
	const float Turned = 90.0f * Quarter;
	const uint8 InkHere = Ink + Quarter;
	const uint8 DarkHere = InkDark + Quarter;
	const uint8 GlowHere = InkGlow + Quarter;
	const uint8 NextInk = Ink + (Quarter + 1) % 4;
	const auto At = [Quarter](const float X, const float Y, const float Z) { return Turn(FVector(X, Y, Z), Quarter); };
	const auto Box = [&](const TCHAR* Name, const uint8 Paint, const FVector& Location, const FVector& Size, const float Yaw,
		const bool bCollision, const bool bChamfer = false)
	{
		return AddBox(FString(Name) + Q, Paint, Turn(Location, Quarter), Size, Yaw + Turned, bCollision, bChamfer);
	};
	// Every slope has safety lines painted down its sides.
	const auto Ramp = [&](const TCHAR* Name, const uint8 Paint, const FVector& Low, const FVector& High, const float Width)
	{
		return AddRamp(FString(Name) + Q, Paint, Turn(Low, Quarter), Turn(High, Quarter), Width, Stripe);
	};
	// The corner of this quarter lies towards +45 degrees; along it the pier and wall follow X + Y = BevelLine.
	const float Diagonal = BevelLine / UE_SQRT_2;
	const FVector Corner = FVector(1.0f, 0.0f, 0.0f).RotateAngleAxis(45.0f, FVector::UpVector);

	// ---- Pier corner and outer walls -------------------------------------------------------------------------
	Box(TEXT("PierCorner"), Floor, FVector(1975.0f, 1975.0f, -50.0f), FVector(1344.0f, 2686.0f, 100.0f), 45.0f, true);
	// Tall walls all round (well above an arcing ball), so throws bank off them instead of sailing out.
	Box(TEXT("SideWall"), Wall, FVector(Half + 30.0f, 0.0f, WallHeight * 0.5f), FVector(60.0f, 3060.0f, WallHeight), 0.0f, true);
	Box(TEXT("SideWallGlow"), Glow, FVector(Half + 30.0f, 0.0f, WallHeight + 6.0f), FVector(70.0f, 3060.0f, 12.0f), 0.0f, false);
	Box(TEXT("SideWallBand"), GlowHere, FVector(Half - 3.0f, 0.0f, 420.0f), FVector(6.0f, 3000.0f, 22.0f), 0.0f, false);
	Box(TEXT("SideWallBandHigh"), Glow, FVector(Half - 3.0f, 0.0f, 560.0f), FVector(6.0f, 3000.0f, 10.0f), 0.0f, false);
	const FVector BevelMid = Corner * (Diagonal + 30.0f);
	Box(TEXT("BevelWall"), Wall, FVector(BevelMid.X, BevelMid.Y, WallHeight * 0.5f), FVector(60.0f, 2740.0f, WallHeight), 45.0f, true);
	Box(TEXT("BevelWallGlow"), GlowHere, FVector(BevelMid.X, BevelMid.Y, WallHeight + 6.0f), FVector(70.0f, 2740.0f, 12.0f), 45.0f, false);
	const FVector BevelFace = Corner * (Diagonal - 3.0f);
	Box(TEXT("BevelWallBand"), GlowHere, FVector(BevelFace.X, BevelFace.Y, 300.0f), FVector(6.0f, 2600.0f, 22.0f), 45.0f, false);
	// Safety lines painted on the deck along the walls.
	Box(TEXT("WallStripe"), Stripe, FVector(Half - 75.0f, 0.0f, 0.6f), FVector(22.0f, 3000.0f, 1.2f), 0.0f, false);
	const FVector BevelLineAt = Corner * (Diagonal - 75.0f);
	Box(TEXT("BevelStripe"), Stripe, FVector(BevelLineAt.X, BevelLineAt.Y, 0.6f), FVector(22.0f, 2600.0f, 1.2f), 45.0f, false);

	// ---- Home: a high terrace, a slope to the balcony, slopes from the balcony to the ground ---------------------
	Box(TEXT("Terrace"), Plaza, FVector(3000.0f, 0.0f, TerraceTop * 0.5f), FVector(800.0f, 1300.0f, TerraceTop), 0.0f, true);
	Box(TEXT("TerraceFace"), InkHere, FVector(2597.0f, 0.0f, 175.0f), FVector(6.0f, 1300.0f, 150.0f), 0.0f, false);
	Box(TEXT("TerraceSideN"), DarkHere, FVector(3000.0f, 653.0f, TerraceTop * 0.5f), FVector(800.0f, 6.0f, TerraceTop - 6.0f), 0.0f, false);
	Box(TEXT("TerraceSideS"), DarkHere, FVector(3000.0f, -653.0f, TerraceTop * 0.5f), FVector(800.0f, 6.0f, TerraceTop - 6.0f), 0.0f, false);
	Box(TEXT("TerraceEdge"), GlowHere, FVector(2604.0f, 0.0f, TerraceTop + 1.5f), FVector(8.0f, 1300.0f, 3.0f), 0.0f, false);
	AddRing(TEXT("TerraceRing") + Q, GlowHere, At(3000.0f, 0.0f, TerraceTop + 1.0f), 200.0f, 4.0f);
	// The berth's number painted on the terrace, readable from the camera (its top toward +X) in every quarter.
	UTextRenderComponent* Berth = CreateDefaultSubobject<UTextRenderComponent>(*(TEXT("BerthNumber") + Q));
	Berth->SetupAttachment(SceneRoot);
	Berth->SetText(FText::AsNumber(Quarter + 1));
	Berth->SetWorldSize(340.0f);
	if (StencilMaterial)
	{
		Berth->SetTextMaterial(StencilMaterial);
	}
	Berth->SetHorizontalAlignment(EHTA_Center);
	Berth->SetVerticalAlignment(EVRTA_TextCenter);
	Berth->SetRelativeLocation(Spread(At(3000.0f, -420.0f, TerraceTop + 0.8f)));
	Berth->SetRelativeRotation(FRotator(90.0f, 180.0f, 0.0f));
	Berth->SetTextRenderColor((WallColor * 0.45f).ToFColor(true));
	Berth->ComponentTags.Add(TEXT("CIBerthText"));
	// A safety line at the foot of the balcony's drop, and old tyres hung on its face as fenders.
	Box(TEXT("BalconyStripe"), Stripe, FVector(1880.0f, 0.0f, 0.6f), FVector(20.0f, 1900.0f, 1.2f), 0.0f, false);
	for (int32 Tyre = 0; Tyre < 3; ++Tyre)
	{
		UStaticMeshComponent* Fender = AddRing(FString::Printf(TEXT("Fender%d"), Tyre) + Q, Steel, At(1883.0f, (Tyre - 1) * 620.0f, 58.0f), 36.0f, 24.0f);
		Fender->SetRelativeRotation(FRotator(90.0f, Turned, 0.0f));
	}
	// Steel hatches let into the deck.
	Box(TEXT("HatchA"), Steel, FVector(900.0f, 900.0f, 0.6f), FVector(170.0f, 170.0f, 1.2f), 0.0f, false);
	Box(TEXT("HatchB"), Steel, FVector(1150.0f, -1450.0f, 0.6f), FVector(130.0f, 220.0f, 1.2f), 0.0f, false);
	// Lamp posts along the top of the side wall, their lamps leaning in over the deck.
	for (const float Along : {-1050.0f, 1050.0f})
	{
		const FString Post = FString::Printf(TEXT("WallLamp%d"), Along < 0.0f ? 0 : 1);
		AddCylinder(Post + TEXT("Post") + Q, Steel, At(Half + 30.0f, Along, WallHeight), 22.0f, 280.0f, false);
		Box(*(Post + TEXT("Arm")), Steel, FVector(Half - 25.0f, Along, WallHeight + 286.0f), FVector(130.0f, 30.0f, 18.0f), 0.0f, false);
		Box(*(Post + TEXT("Light")), Glow, FVector(Half - 70.0f, Along, WallHeight + 274.0f), FVector(46.0f, 34.0f, 6.0f), 0.0f, false);
	}

	Box(TEXT("Balcony"), Plaza, FVector(2250.0f, 0.0f, BalconyTop * 0.5f), FVector(700.0f, 1900.0f, BalconyTop), 0.0f, true);
	Box(TEXT("BalconyFace"), DarkHere, FVector(1897.0f, 0.0f, BalconyTop * 0.5f), FVector(6.0f, 1900.0f, BalconyTop - 10.0f), 0.0f, false);
	Box(TEXT("BalconyEdge"), GlowHere, FVector(1904.0f, 0.0f, BalconyTop + 1.5f), FVector(8.0f, 1900.0f, 3.0f), 0.0f, false);

	Ramp(TEXT("TerraceRamp"), InkHere, FVector(2200.0f, 0.0f, BalconyTop), FVector(2600.0f, 0.0f, TerraceTop), 380.0f);
	Ramp(TEXT("BalconyRampN"), Trim, FVector(2225.0f, 1400.0f, 0.0f), FVector(2225.0f, 950.0f, BalconyTop), 350.0f);
	Ramp(TEXT("BalconyRampS"), Trim, FVector(2225.0f, -1400.0f, 0.0f), FVector(2225.0f, -950.0f, BalconyTop), 350.0f);

	// ---- The corner warehouse, flush against the corner wall ---------------------------------------------------
	const FVector House = Corner * (Diagonal - 250.0f);
	Box(TEXT("Warehouse"), Scenery, FVector(House.X, House.Y, 210.0f), FVector(500.0f, 900.0f, 420.0f), 45.0f, true);
	Box(TEXT("WarehouseRoof"), NextInk, FVector(House.X, House.Y, 432.0f), FVector(540.0f, 940.0f, 24.0f), 45.0f, true);
	// Vents on the roof.
	const FVector RoofAlong = FRotator(0.0f, 45.0f + Turned, 0.0f).RotateVector(FVector(0.0f, 1.0f, 0.0f));
	for (const float Along : {-260.0f, 260.0f})
	{
		const FVector Vent = Turn(FVector(House.X, House.Y, 0.0f), Quarter) + RoofAlong * Along;
		AddBox(FString::Printf(TEXT("WarehouseVent%d"), Along < 0.0f ? 0 : 1) + Q, Trim, FVector(Vent.X, Vent.Y, 474.0f),
			FVector(110.0f, 110.0f, 60.0f), 45.0f + Turned, false);
	}
	const FVector HouseFront = House - Corner * 252.0f;
	for (int32 Storey = 0; Storey < 3; ++Storey)
	{
		Box(*FString::Printf(TEXT("WarehouseWindow%d"), Storey), Storey == 1 ? GlowHere : Glow,
			FVector(HouseFront.X, HouseFront.Y, 110.0f + Storey * 110.0f), FVector(6.0f, 760.0f, 16.0f), 45.0f, false);
	}

	// ---- Mid ground: a container wall in front of the balcony, low walls and barrels to jump -------------------
	const FVector Container(1500.0f, 0.0f, 160.0f);
	AddContainer(TEXT("Container") + Q, InkHere, DarkHere, Turn(Container, Quarter), FVector(240.0f, 560.0f, 320.0f), Turned);
	// A tall bank wall across the open ground between the plaza and the corner: shots off it reach round cover.
	const FVector Bank = Corner * 1850.0f;
	Box(TEXT("BankWall"), Trim, FVector(Bank.X, Bank.Y, 150.0f), FVector(50.0f, 520.0f, 300.0f), 45.0f, true, true);
	Box(TEXT("BankWallGlow"), GlowHere, FVector(Bank.X, Bank.Y, 303.0f), FVector(56.0f, 500.0f, 6.0f), 45.0f, false);
	Box(TEXT("LowWallN"), Trim, FVector(1250.0f, 850.0f, 47.5f), FVector(360.0f, 70.0f, 95.0f), 35.0f, true, true);
	Box(TEXT("LowWallS"), Trim, FVector(1250.0f, -850.0f, 47.5f), FVector(360.0f, 70.0f, 95.0f), -35.0f, true, true);
	const FVector Barrels[] = {FVector(1700.0f, -1000.0f, 0.0f), FVector(1830.0f, -1090.0f, 0.0f)};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Barrels); ++Index)
	{
		const FString Name = FString::Printf(TEXT("Barrel%d"), Index) + Q;
		AddCylinder(Name, Index == 0 ? InkHere : NextInk, Turn(Barrels[Index], Quarter), 150.0f, 95.0f);
		AddCylinder(Name + TEXT("Lid"), Trim, Turn(Barrels[Index] + FVector(0.0f, 0.0f, 95.0f), Quarter), 158.0f, 6.0f, false);
		AddRing(Name + TEXT("Band"), GlowHere, Turn(Barrels[Index] + FVector(0.0f, 0.0f, 60.0f), Quarter), 77.0f, 8.0f);
	}

	// ---- The plaza face towards this corner: its slope and glowing edge -----------------------------------------
	Ramp(TEXT("PlazaRamp"), Trim, Corner * 1000.0f, Corner * 500.0f + FVector(0.0f, 0.0f, PlazaTop), 360.0f);
	const FVector Edge = Corner * 502.0f;
	Box(TEXT("PlazaEdge"), GlowHere, FVector(Edge.X, Edge.Y, PlazaTop + 1.5f), FVector(8.0f, 1000.0f, 3.0f), 45.0f, false);

	// ---- Town props inside the arena (placed where they block no path, spawn or pad) ---------------------------
	const auto Prop = [&](const TCHAR* Name, const uint8 Model, const FVector& LayoutLocation, const float Yaw, const float Scale,
		const bool bCollision)
	{
		return AddProp(FString(Name) + Q, CityMeshes[Model], Spread(Turn(LayoutLocation, Quarter)), Yaw + Turned, Scale, bCollision);
	};
	// A billboard with running text on top of the side wall, facing the arena.
	Prop(TEXT("WallBillboard"), Billboard, FVector(Half + 30.0f, 0.0f, WallHeight), 90.0f, 1.4f, false);
	Prop(TEXT("WallBillboardText"), BillboardText, FVector(Half + 30.0f, 0.0f, WallHeight), 90.0f, 1.4f, false);
	// Graffiti on the inside of the side wall.
	Prop(TEXT("WallGraffiti"), Graffiti, FVector(Half - 9.0f, 1000.0f, 230.0f), 90.0f, 1.0f, false);
	// Hedges along the terrace sides: a railing you cannot fall past by accident.
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const float X = 2760.0f + Index * 240.0f;
		Prop(*FString::Printf(TEXT("HedgeN%d"), Index), Hedge, FVector(X, 615.0f, TerraceTop), 0.0f, 1.0f, true);
		Prop(*FString::Printf(TEXT("HedgeS%d"), Index), Hedge, FVector(X, -615.0f, TerraceTop), 0.0f, 1.0f, true);
	}
	// Dumpsters in front of the warehouse, and bins by the barrels: more cover in the corner lanes.
	Prop(TEXT("DumpsterA"), Dumpster, FVector(2227.0f, 1803.0f, 0.0f), 45.0f, 1.0f, true);
	Prop(TEXT("DumpsterB"), Dumpster, FVector(1803.0f, 2227.0f, 0.0f), 45.0f, 1.0f, true);
	Prop(TEXT("BinA"), TrashBin, FVector(1600.0f, -1150.0f, 0.0f), 0.0f, 1.0f, true);
	Prop(TEXT("BinB"), TrashBin, FVector(1950.0f, -990.0f, 0.0f), 30.0f, 1.0f, true);
}

UStaticMeshComponent* AChaosImpactSplashStage::AddProp(const FString& Name, UStaticMesh* Mesh, const FVector& Location,
	const float Yaw, const float Scale, const bool bCollision)
{
	if (!Mesh)
	{
		return nullptr;
	}
	UStaticMeshComponent* Prop = CreateDefaultSubobject<UStaticMeshComponent>(*Name);
	Prop->SetupAttachment(SceneRoot);
	Prop->SetStaticMesh(Mesh);
	Prop->SetRelativeLocation(Location);
	Prop->SetRelativeRotation(FRotator(0.0f, Yaw, 0.0f));
	Prop->SetRelativeScale3D(FVector(Scale));
	if (bCollision)
	{
		Prop->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Prop->SetCollisionObjectType(ECC_WorldStatic);
		Prop->SetCollisionResponseToAllChannels(ECR_Block);
	}
	else
	{
		Prop->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	Prop->SetGenerateOverlapEvents(false);
	Prop->bEditableWhenInherited = true;
	return Prop;
}

void AChaosImpactSplashStage::BuildTown(const int32 Quarter)
{
	// Stage units (not spread): the town sits beyond the opening camera's widest circle (5600) so it never
	// flies into a building, on a pier of pavement tiles across the water from this quarter's wall.
	const FString Q = FString::Printf(TEXT("_Q%d"), Quarter);
	const float Turned = 90.0f * Quarter;
	const auto Town = [&](const FString& Name, const uint8 Model, const FVector& Location, const float Yaw, const float Scale)
	{
		return AddProp(Name + Q, CityMeshes[Model], Turn(Location, Quarter), Yaw + Turned, Scale, false);
	};
	const uint8 Tiles[] = {TileA, TileB, TileC};
	int32 TileIndex = 0;
	for (const float X : {6950.0f, 8450.0f})
	{
		for (int32 Row = 0; Row < 6; ++Row)
		{
			Town(FString::Printf(TEXT("TownTile%d"), TileIndex), Tiles[TileIndex % 3], FVector(X, -3750.0f + Row * 1500.0f, 0.0f),
				90.0f * (TileIndex % 4), 1.0f);
			++TileIndex;
		}
	}
	// The corner block with the tower.
	for (const float X : {6850.0f, 8350.0f})
	{
		for (const float Y : {6850.0f, 8350.0f})
		{
			Town(FString::Printf(TEXT("TownTile%d"), TileIndex), Tiles[TileIndex % 3], FVector(X, Y, 0.0f), 0.0f, 1.0f);
			++TileIndex;
		}
	}
	Town(TEXT("TownTower"), TwistedTower, FVector(7600.0f, 7600.0f, 10.0f), 45.0f, 0.7f);
	Town(TEXT("TownTerrace"), EcoTerrace, FVector(7700.0f, -2300.0f, 10.0f), 0.0f, 0.7f);
	Town(TEXT("TownSlope"), EcoSlope, FVector(7700.0f, 1900.0f, 10.0f), 0.0f, 0.7f);
	Town(TEXT("TownPalmA"), Palm, FVector(6450.0f, -600.0f, 10.0f), 0.0f, 0.45f);
	Town(TEXT("TownPalmB"), Palm, FVector(6450.0f, 700.0f, 10.0f), 70.0f, 0.4f);
	Town(TEXT("TownPalmC"), Palm, FVector(6450.0f, -3900.0f, 10.0f), 140.0f, 0.45f);
	Town(TEXT("TownLightA"), Spotlight, FVector(6350.0f, -1500.0f, 10.0f), 0.0f, 1.0f);
	Town(TEXT("TownLightB"), Spotlight, FVector(6350.0f, 3000.0f, 10.0f), 0.0f, 1.0f);
	Town(TEXT("TownSign"), BillboardStand, FVector(6600.0f, 400.0f, 10.0f), 90.0f, 1.0f);
	Town(TEXT("TownCarA"), CarA, FVector(7250.0f, -500.0f, 10.0f), 0.0f, 1.0f);
	Town(TEXT("TownCarB"), CarB, FVector(7200.0f, 4100.0f, 10.0f), 90.0f, 1.0f);
	Town(TEXT("TownVan"), Van, FVector(8350.0f, 300.0f, 10.0f), 180.0f, 1.0f);
	Town(TEXT("TownBusStop"), BusStop, FVector(6450.0f, -4300.0f, 10.0f), 90.0f, 1.0f);

	// On the water between the pier and the town: a moored boat and two buoys (they ride with the tide).
	const FVector BoatAt = Turn(FVector(5350.0f, -2650.0f, 0.0f), Quarter);
	const FRotator BoatFacing(0.0f, Turned + 18.0f, 0.0f);
	const uint8 Hull = Ink + (Quarter + 3) % 4;
	const auto BoatPart = [&](const TCHAR* Part, const uint8 Paint, UStaticMesh* Mesh, const FVector& Offset, const FVector& Size)
	{
		return AddPartAt(TideRoot, FString(TEXT("Boat")) + Part + Q, Paint, Mesh, BoatAt + BoatFacing.RotateVector(Offset), Size, BoatFacing);
	};
	BoatPart(TEXT("Hull"), Hull, ChamferMesh.Get(), FVector(0.0f, 0.0f, -70.0f), FVector(560.0f, 210.0f, 110.0f));
	BoatPart(TEXT("Band"), Trim, BoxMesh.Get(), FVector(0.0f, 0.0f, -22.0f), FVector(540.0f, 214.0f, 14.0f));
	BoatPart(TEXT("Cabin"), Trim, BoxMesh.Get(), FVector(-90.0f, 0.0f, 45.0f), FVector(170.0f, 150.0f, 120.0f));
	BoatPart(TEXT("Roof"), Hull, BoxMesh.Get(), FVector(-90.0f, 0.0f, 112.0f), FVector(190.0f, 170.0f, 14.0f));
	BoatPart(TEXT("Window"), Glow, BoxMesh.Get(), FVector(-4.0f, 0.0f, 62.0f), FVector(4.0f, 120.0f, 30.0f));
	BoatPart(TEXT("Mast"), Steel, CylinderMesh.Get(), FVector(70.0f, 0.0f, -15.0f), FVector(12.0f, 12.0f, 340.0f));
	int32 BuoyIndex = 0;
	for (const FVector& At : {FVector(4650.0f, 2450.0f, 0.0f), FVector(5050.0f, -1650.0f, 0.0f)})
	{
		const FVector Buoy = Turn(At, Quarter);
		const FString Name = FString::Printf(TEXT("Buoy%d"), BuoyIndex++) + Q;
		AddPartAt(TideRoot, Name, Stripe, CylinderMesh.Get(), Buoy + FVector(0.0f, 0.0f, -130.0f), FVector(80.0f, 80.0f, 100.0f));
		AddPartAt(TideRoot, Name + TEXT("Post"), Steel, CylinderMesh.Get(), Buoy + FVector(0.0f, 0.0f, -30.0f), FVector(16.0f, 16.0f, 90.0f));
		AddPartAt(TideRoot, Name + TEXT("Lamp"), Glow, BoxMesh.Get(), Buoy + FVector(0.0f, 0.0f, 68.0f), FVector(18.0f, 18.0f, 16.0f));
	}
}

UStaticMeshComponent* AChaosImpactSplashStage::AddRamp(const FString& Name, const uint8 Paint, const FVector& LayoutLow,
	const FVector& LayoutHigh, const float LayoutWidth, const uint8 EdgePaint)
{
	const FVector Low = Spread(LayoutLow);
	const FVector High = Spread(LayoutHigh);
	const float Width = LayoutWidth * LayoutScale;
	const FVector Along = High - Low;
	const float Run = Along.Size2D();
	const float Length = Along.Size();
	const FRotator Slope(FMath::RadiansToDegrees(FMath::Atan2(Along.Z, Run)), Along.Rotation().Yaw, 0.0f);
	// Thick enough to reach the ground under its high end, so from the side it reads as a solid wedge.
	const float Thickness = FMath::Max(80.0f, Along.Z / FMath::Cos(FMath::DegreesToRadians(Slope.Pitch)) + 60.0f);
	const FVector TopCentre = (Low + High) * 0.5f;
	const FVector Normal = Slope.RotateVector(FVector::UpVector);
	UStaticMeshComponent* Block = AddPart(Name, Paint, BoxMesh.Get(), true);
	Block->SetRelativeLocation(TopCentre - Normal * Thickness * 0.5f);
	Block->SetRelativeRotation(Slope);
	Block->SetRelativeScale3D(FVector(Length, Width, Thickness) / 100.0f);
	if (EdgePaint != 255)
	{
		// Painted lines down both sides of the slope.
		const FVector Across = Slope.RotateVector(FVector::RightVector);
		for (const float Side : {-1.0f, 1.0f})
		{
			UStaticMeshComponent* Line = AddPart(Name + (Side < 0.0f ? TEXT("EdgeL") : TEXT("EdgeR")), EdgePaint, BoxMesh.Get(), false);
			Line->SetRelativeLocation(TopCentre + Across * Side * (Width * 0.5f - 16.0f) + Normal * 0.8f);
			Line->SetRelativeRotation(Slope);
			Line->SetRelativeScale3D(FVector(Length, 18.0f, 1.2f) / 100.0f);
		}
	}
	return Block;
}

UStaticMeshComponent* AChaosImpactSplashStage::AddPart(const FString& Name, const uint8 Paint, UStaticMesh* Mesh,
	const bool bCollision)
{
	UStaticMeshComponent* Block = CreateDefaultSubobject<UStaticMeshComponent>(*Name);
	Block->SetupAttachment(SceneRoot);
	Block->SetStaticMesh(Mesh);
	if (bCollision)
	{
		Block->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Block->SetCollisionObjectType(ECC_WorldStatic);
		Block->SetCollisionResponseToAllChannels(ECR_Block);
		Block->CanCharacterStepUpOn = ECB_Yes;
	}
	else
	{
		Block->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Block->SetCastShadow(false);
	}
	Block->SetGenerateOverlapEvents(false);
	Block->bEditableWhenInherited = true;
	// Each kind of surface its material (the plain flat one wherever one is missing).
	const bool bGlow = Paint == Glow || (Paint >= InkGlow && Paint < InkGlow + 4);
	UMaterialInterface* Surface = FlatMaterial.Get();
	if (bGlow)
	{
		Surface = GlowMaterial.Get();
	}
	else if (Paint == Floor || Paint == OuterFloor)
	{
		Surface = PlanksMaterial ? PlanksMaterial.Get() : GridMaterial.Get();
	}
	else if ((Paint == Plaza || Paint == Trim) && ConcreteMaterial)
	{
		Surface = ConcreteMaterial.Get();
	}
	else if ((Paint == Wall || Paint == Scenery) && PanelMaterial)
	{
		Surface = PanelMaterial.Get();
	}
	else if (Paint != Water && Paint != Steel && PaintMaterial)
	{
		Surface = PaintMaterial.Get();
	}
	Block->SetMaterial(0, Surface);
	Block->ComponentTags.Add(FName(*FString::Printf(TEXT("CIPaint%d"), Paint)));
	return Block;
}

UStaticMeshComponent* AChaosImpactSplashStage::AddBox(const FString& Name, const uint8 Paint, const FVector& Location,
	const FVector& Size, const float Yaw, const bool bCollision, const bool bChamfer)
{
	UStaticMeshComponent* Block = AddPart(Name, Paint, bChamfer ? ChamferMesh.Get() : BoxMesh.Get(), bCollision);
	Block->SetRelativeLocation(Spread(Location));
	Block->SetRelativeRotation(FRotator(0.0f, Yaw, 0.0f));
	Block->SetRelativeScale3D(Spread(Size) / 100.0f);
	return Block;
}

UStaticMeshComponent* AChaosImpactSplashStage::AddCylinder(const FString& Name, const uint8 Paint, const FVector& Location,
	const float Diameter, const float Height, const bool bCollision)
{
	// SM_Cylinder is 100 across with its base at 0.
	UStaticMeshComponent* Block = AddPart(Name, Paint, CylinderMesh.Get(), bCollision);
	Block->SetRelativeLocation(Spread(Location));
	Block->SetRelativeScale3D(FVector(Diameter * LayoutScale / 100.0f, Diameter * LayoutScale / 100.0f, Height / 100.0f));
	return Block;
}

UStaticMeshComponent* AChaosImpactSplashStage::AddRing(const FString& Name, const uint8 Paint, const FVector& Location,
	const float Radius, const float Thickness)
{
	// SM_CircularBand is a band of radius 70 and about 13 high, centred on its pivot.
	UStaticMeshComponent* Block = AddPart(Name, Paint, RingMesh.Get(), false);
	Block->SetRelativeLocation(Spread(Location));
	Block->SetRelativeScale3D(FVector(Radius * LayoutScale / 70.0f, Radius * LayoutScale / 70.0f, Thickness / 13.0f));
	return Block;
}

void AChaosImpactSplashStage::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyPalette();
}

void AChaosImpactSplashStage::BeginPlay()
{
	Super::BeginPlay();
	ApplyPalette();
}

FLinearColor AChaosImpactSplashStage::GetPaintColor(const uint8 Paint) const
{
	const auto InkColor = [this](const int32 Quarter)
	{
		return InkColors.IsValidIndex(Quarter) ? InkColors[Quarter] : FLinearColor(1.0f, 0.1f, 0.5f);
	};
	// The plain paints after the ink range first (they are numbered past it).
	if (Paint == Stripe) { return StripeColor; }
	if (Paint == Steel) { return FLinearColor(0.06f, 0.065f, 0.08f); }
	if (Paint >= InkSplat) { return InkColor(Paint - InkSplat); }
	if (Paint >= InkGlow) { return InkColor(Paint - InkGlow); }
	if (Paint >= InkDark) { return InkColor(Paint - InkDark) * 0.35f; }
	if (Paint >= Ink) { return InkColor(Paint - Ink); }
	switch (Paint)
	{
	case Floor: return FloorColor;
	case OuterFloor: return SceneryColor;
	case Water: return WaterColor;
	case Plaza: return PlazaColor;
	case Wall: return WallColor;
	case Trim: return TrimColor;
	case Glow: return GlowColor;
	default: return SceneryColor;
	}
}

void AChaosImpactSplashStage::ApplyPalette()
{
	if (!bApplyDefaultColors)
	{
		return;
	}
	TArray<UTextRenderComponent*> Texts;
	GetComponents(Texts);
	for (UTextRenderComponent* Text : Texts)
	{
		if (Text->ComponentHasTag(TEXT("CIBerthText")))
		{
			// Stencilled in the walls' dark paint.
			Text->SetTextRenderColor((WallColor * 0.45f).ToFColor(true));
		}
	}
	TArray<UStaticMeshComponent*> Meshes;
	GetComponents(Meshes);
	for (UStaticMeshComponent* Block : Meshes)
	{
		if (Block->ComponentHasTag(TEXT("CIOcean")))
		{
			// The Water Materials ocean: a bright shallow colour and a deep one, both from Water Color.
			UMaterialInstanceDynamic* Ocean = Cast<UMaterialInstanceDynamic>(Block->GetMaterial(0));
			Ocean = Ocean ? Ocean : Block->CreateDynamicMaterialInstance(0);
			if (Ocean)
			{
				Ocean->SetVectorParameterValue(TEXT("Colour"), OceanColor);
				Ocean->SetVectorParameterValue(TEXT("ColourDeep"), WaterColor);
			}
			continue;
		}
		uint8 Paint = 0;
		if (!ReadPaint(Block, Paint))
		{
			continue;
		}
		UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Block->GetMaterial(0));
		if (!Material)
		{
			Material = Block->CreateDynamicMaterialInstance(0);
		}
		if (!Material)
		{
			continue;
		}
		const FLinearColor Color = GetPaintColor(Paint);
		if (Paint == Glow || (Paint >= InkGlow && Paint < InkSplat))
		{
			Material->SetVectorParameterValue(TEXT("Color"), Color);
			Material->SetScalarParameterValue(TEXT("Intensity"), GlowIntensity);
		}
		else
		{
			// The harbour's materials take "Color"; the plain flat one (where one is missing) "Base Color" and "Roughness".
			Material->SetVectorParameterValue(TEXT("Color"), Color);
			Material->SetVectorParameterValue(TEXT("Base Color"), Color);
			Material->SetScalarParameterValue(TEXT("Roughness"), Paint == Water ? 0.04f : Paint == Steel ? 0.4f : 0.55f);
		}
	}
}

bool AChaosImpactSplashStage::ReadPaint(const UStaticMeshComponent* Block, uint8& OutPaint)
{
	for (const FName& Tag : Block->ComponentTags)
	{
		const FString Text = Tag.ToString();
		if (Text.StartsWith(TEXT("CIPaint")))
		{
			OutPaint = static_cast<uint8>(FCString::Atoi(*Text + 7));
			return true;
		}
	}
	return false;
}

TArray<FVector> AChaosImpactSplashStage::GetSpawnPoints() const
{
	return ToWorld(SpawnPointOffsets);
}

TArray<FVector> AChaosImpactSplashStage::GetBallPoints() const
{
	return ToWorld(BallPointOffsets);
}

bool AChaosImpactSplashStage::IsOutside(const FVector& Location) const
{
	if (Super::IsOutside(Location))
	{
		return true;
	}
	// Past a corner wall (X + Y = BevelLine, spread like the rest, the wall 60 thick) by the same margin.
	const FVector Local = GetActorTransform().InverseTransformPositionNoScale(Location);
	return FMath::Abs(Local.X) + FMath::Abs(Local.Y) > BevelLine * LayoutScale + (60.0f + OutsideMargin) * UE_SQRT_2;
}
