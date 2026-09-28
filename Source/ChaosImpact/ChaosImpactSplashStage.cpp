#include "ChaosImpactSplashStage.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
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
		AddBox(TEXT("SeaBed"), Water, FVector(0.0f, 0.0f, -700.0f), FVector(20000.0f, 20000.0f, 20.0f), 0.0f, false);
	}
	else
	{
		AddBox(TEXT("Sea"), Water, FVector(0.0f, 0.0f, -95.0f), FVector(20000.0f, 20000.0f, 20.0f), 0.0f, false);
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
		BuildTown(Quarter);
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
	const auto Ramp = [&](const TCHAR* Name, const uint8 Paint, const FVector& Low, const FVector& High, const float Width)
	{
		return AddRamp(FString(Name) + Q, Paint, Turn(Low, Quarter), Turn(High, Quarter), Width);
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

	// ---- Home: a high terrace, a slope to the balcony, slopes from the balcony to the ground ---------------------
	Box(TEXT("Terrace"), Plaza, FVector(3000.0f, 0.0f, TerraceTop * 0.5f), FVector(800.0f, 1300.0f, TerraceTop), 0.0f, true);
	Box(TEXT("TerraceFace"), InkHere, FVector(2597.0f, 0.0f, 175.0f), FVector(6.0f, 1300.0f, 150.0f), 0.0f, false);
	Box(TEXT("TerraceSideN"), DarkHere, FVector(3000.0f, 653.0f, TerraceTop * 0.5f), FVector(800.0f, 6.0f, TerraceTop - 6.0f), 0.0f, false);
	Box(TEXT("TerraceSideS"), DarkHere, FVector(3000.0f, -653.0f, TerraceTop * 0.5f), FVector(800.0f, 6.0f, TerraceTop - 6.0f), 0.0f, false);
	Box(TEXT("TerraceEdge"), GlowHere, FVector(2604.0f, 0.0f, TerraceTop + 1.5f), FVector(8.0f, 1300.0f, 3.0f), 0.0f, false);
	AddRing(TEXT("TerraceRing") + Q, GlowHere, At(3000.0f, 0.0f, TerraceTop + 1.0f), 200.0f, 4.0f);

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
	const FVector HouseFront = House - Corner * 252.0f;
	for (int32 Storey = 0; Storey < 3; ++Storey)
	{
		Box(*FString::Printf(TEXT("WarehouseWindow%d"), Storey), Storey == 1 ? GlowHere : Glow,
			FVector(HouseFront.X, HouseFront.Y, 110.0f + Storey * 110.0f), FVector(6.0f, 760.0f, 16.0f), 45.0f, false);
	}

	// ---- Mid ground: a container wall in front of the balcony, low walls and barrels to jump -------------------
	const FVector Container(1500.0f, 0.0f, 160.0f);
	Box(TEXT("Container"), InkHere, Container, FVector(240.0f, 560.0f, 320.0f), 0.0f, true, true);
	for (int32 Rib = -2; Rib <= 2; ++Rib)
	{
		Box(*FString::Printf(TEXT("ContainerRib%d"), Rib + 2), DarkHere, Container + FVector(0.0f, Rib * 105.0f, 0.0f),
			FVector(250.0f, 20.0f, 330.0f), 0.0f, false);
	}
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

	// ---- Scenery over the water: a gantry crane behind the terrace ------------------------------------------------
	Box(TEXT("CraneLegN"), NextInk, FVector(Half + 700.0f, 1000.0f, 500.0f), FVector(120.0f, 120.0f, 1100.0f), 0.0f, false);
	Box(TEXT("CraneLegS"), NextInk, FVector(Half + 700.0f, -1000.0f, 500.0f), FVector(120.0f, 120.0f, 1100.0f), 0.0f, false);
	Box(TEXT("CraneBeam"), NextInk, FVector(Half + 700.0f, 0.0f, 1080.0f), FVector(160.0f, 2240.0f, 120.0f), 0.0f, false);
	Box(TEXT("CraneArm"), DarkHere, FVector(Half + 350.0f, 0.0f, 1180.0f), FVector(900.0f, 140.0f, 80.0f), 0.0f, false);
	Box(TEXT("CraneGlow"), GlowHere, FVector(Half + 700.0f, 0.0f, 1142.0f), FVector(170.0f, 2240.0f, 6.0f), 0.0f, false);
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
}

UStaticMeshComponent* AChaosImpactSplashStage::AddRamp(const FString& Name, const uint8 Paint, const FVector& LayoutLow,
	const FVector& LayoutHigh, const float LayoutWidth)
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
	const bool bGlow = Paint == Glow || (Paint >= InkGlow && Paint < InkGlow + 4);
	const bool bGrid = Paint == Floor || Paint == OuterFloor || Paint == Plaza;
	UMaterialInterface* Surface = bGlow ? GlowMaterial.Get() : bGrid ? GridMaterial.Get() : FlatMaterial.Get();
	if (Paint == Plaza && MetalFloorMaterial)
	{
		Surface = MetalFloorMaterial.Get();
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
		if (Paint == Plaza && MetalFloorMaterial)
		{
			// The material lifts the dark source plate to a light metal; Tint colours it with the plaza colour.
			Material->SetVectorParameterValue(TEXT("Tint"), Color * 2.4f);
		}
		else if (Paint == Floor || Paint == OuterFloor || Paint == Plaza)
		{
			// The prototype grid: a surface colour with darker lines every metre.
			const FLinearColor Lines = Paint == Floor ? FloorGridColor : Color * 0.6f;
			Material->SetVectorParameterValue(TEXT("SurfaceColor"), Color);
			Material->SetVectorParameterValue(TEXT("TopSurfaceColor"), Color);
			Material->SetVectorParameterValue(TEXT("GridColor"), Lines);
			Material->SetVectorParameterValue(TEXT("TopGridColor"), Lines);
			Material->SetVectorParameterValue(TEXT("SubGridColor"), FMath::Lerp(Color, Lines, 0.45f));
			Material->SetVectorParameterValue(TEXT("TopSubGridGridColor"), FMath::Lerp(Color, Lines, 0.45f));
			Material->SetScalarParameterValue(TEXT("Grid Size"), 200.0f);
		}
		else if (Paint == Glow || (Paint >= InkGlow && Paint < InkSplat))
		{
			Material->SetVectorParameterValue(TEXT("Color"), Color);
			Material->SetScalarParameterValue(TEXT("Intensity"), GlowIntensity);
		}
		else
		{
			Material->SetVectorParameterValue(TEXT("Base Color"), Color);
			// Ink is wet and shiny; the rest is painted metal.
			// Glossy accents and a mirror-like sea; the rest is painted concrete and metal.
			const bool bGlossy = Paint >= Ink && Paint < InkDark;
			Material->SetScalarParameterValue(TEXT("Roughness"), Paint == Water ? 0.04f : bGlossy ? 0.3f : 0.55f);
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
