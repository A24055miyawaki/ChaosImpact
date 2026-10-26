#include "ChaosImpactCyberStage.h"

#include "ChaosImpact.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactBallTypes.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactSfx.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraFunctionLibrary.h"
#include "UObject/ConstructorHelpers.h"

namespace CyberStage
{
	constexpr float CenterCourtTop = 70.0f;
	constexpr float OverpassTop = 120.0f;
	constexpr float WallHeight = 580.0f;

	const FVector QuarterSpawnOffsets[] =
	{
		FVector(2950.0f, 0.0f, OverpassTop),    // elevated highway overpass
		FVector(2450.0f, 1750.0f, 0.0f),        // corner retreat pocket behind the sound barrier
		FVector(1750.0f, -700.0f, 0.0f)         // main asphalt lane beside the reflector wall
	};

	const FVector QuarterBallOffsets[] =
	{
		FVector(2750.0f, 780.0f, OverpassTop),  // overpass flank pad
		FVector(1550.0f, 0.0f, 0.0f)            // approach lane in front of the center ramp
	};

	FVector Turn(const FVector& Offset, const int32 Quarter)
	{
		return FRotator(0.0f, 90.0f * Quarter, 0.0f).RotateVector(Offset);
	}

	FVector Spread(const FVector& Layout)
	{
		return FVector(Layout.X * AChaosImpactCyberStage::LayoutScale, Layout.Y * AChaosImpactCyberStage::LayoutScale, Layout.Z);
	}
}

FVector AChaosImpactCyberStage::GetGateOffset(const int32 Gate)
{
	const FVector Corner = FVector(1.0f, 1.0f, 0.0f).GetSafeNormal() * 1160.0f;
	return CyberStage::Spread(CyberStage::Turn(Corner, FMath::Clamp(Gate, 0, 3)));
}

FVector AChaosImpactCyberStage::GetGateNormal(const int32 Gate)
{
	// Faces inward/outward along the diagonal (45 deg + Quarter * 90 deg).
	return CyberStage::Turn(FVector(1.0f, 1.0f, 0.0f).GetSafeNormal(), FMath::Clamp(Gate, 0, 3));
}

AChaosImpactCyberStage::AChaosImpactCyberStage()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);
	SetNetUpdateFrequency(10.0f);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	NeonColors = {
		FLinearColor(0.0f, 0.85f, 1.0f),    // cyan (North / +X)
		FLinearColor(1.0f, 0.08f, 0.55f),   // magenta (East / +Y)
		FLinearColor(1.0f, 0.58f, 0.02f),   // amber (South / -X)
		FLinearColor(0.35f, 1.0f, 0.12f)    // lime (West / -Y)
	};

	BallPointOffsets.Add(FVector(0.0f, 0.0f, CyberStage::CenterCourtTop + 30.0f));
	for (int32 Quarter = 0; Quarter < 4; ++Quarter)
	{
		for (const FVector& Offset : CyberStage::QuarterSpawnOffsets)
		{
			SpawnPointOffsets.Add(CyberStage::Spread(CyberStage::Turn(Offset, Quarter)));
		}
		for (const FVector& Offset : CyberStage::QuarterBallOffsets)
		{
			BallPointOffsets.Add(CyberStage::Spread(CyberStage::Turn(Offset, Quarter)));
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
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Glowing(TEXT("/Game/ChaosImpact/Versus/M_StageGlow.M_StageGlow"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MetalFloor(
		TEXT("/Game/ChaosImpact/Versus/M_StageMetalFloor.M_StageMetalFloor"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Concrete(TEXT("/Game/ChaosImpact/Versus/Splash/M_CI_Concrete.M_CI_Concrete"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Panel(TEXT("/Game/ChaosImpact/Versus/Splash/M_CI_PanelWall.M_CI_PanelWall"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Paint(TEXT("/Game/ChaosImpact/Versus/Splash/M_CI_GlossPaint.M_CI_GlossPaint"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FieldTeal(
		TEXT("/Game/ChaosImpact/solo/Stage2/Materials/MI_Field_Teal.MI_Field_Teal"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FieldAmber(
		TEXT("/Game/ChaosImpact/solo/Stage1/Materials/MI_NeonField_Amber.MI_NeonField_Amber"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Stencil(
		TEXT("/Engine/EngineMaterials/DefaultTextMaterialOpaque.DefaultTextMaterialOpaque"));

	BoxMesh = Box.Object;
	ChamferMesh = Chamfer.Succeeded() ? Chamfer.Object : Box.Object;
	CylinderMesh = Cylinder.Object;
	RingMesh = Ring.Object;
	FlatMaterial = Flat.Object;
	GridMaterial = Grid.Succeeded() ? Grid.Object : Flat.Object;
	GlowMaterial = Glowing.Succeeded() ? Glowing.Object : Flat.Object;
	MetalFloorMaterial = MetalFloor.Succeeded() ? MetalFloor.Object : nullptr;
	ConcreteMaterial = Concrete.Succeeded() ? Concrete.Object : nullptr;
	PanelMaterial = Panel.Succeeded() ? Panel.Object : nullptr;
	PaintMaterial = Paint.Succeeded() ? Paint.Object : nullptr;
	FieldTealMaterial = FieldTeal.Succeeded() ? FieldTeal.Object : GlowMaterial;
	FieldAmberMaterial = FieldAmber.Succeeded() ? FieldAmber.Object : GlowMaterial;
	StencilMaterial = Stencil.Object;

	static const TCHAR* const CityPaths[] =
	{
		TEXT("Buildings/SM_Regular_Building_TwistedTower_Large"),
		TEXT("Buildings/SM_Eco_Building_Slope"),
		TEXT("Buildings/SM_Eco_Building_Terrace"),
		TEXT("Billboards/SM_Billboard_4x1_03"),
		TEXT("Billboards/SM_Billboard_4x1_03_Line"),
		TEXT("Billboards/SM_Billboard_2x1_05"),
		TEXT("Props/SM_Graffiti_03"),
		TEXT("Props/SM_Spotlight_02"),
		TEXT("Props/SM_Trash_Can_04"),
		TEXT("Props/SM_Trash_03"),
		TEXT("Sidewalks/SM_Set_B_Tiles_01"),
		TEXT("Sidewalks/SM_Set_B_Tiles_04"),
		TEXT("Sidewalks/SM_Set_B_Tiles_05"),
		TEXT("Cars/SM_Car_06"),
		TEXT("Cars/SM_Car_16"),
		TEXT("Cars/SM_Van"),
		TEXT("Props/SM_Bus_Stop_02")
	};
	static_assert(UE_ARRAY_COUNT(CityPaths) == CityCount, "one path per ECity");
	for (const TCHAR* Path : CityPaths)
	{
		const FString Full = FString::Printf(TEXT("/Game/Cartoon_City_Free/Meshes/%s.%s"), Path, *FPaths::GetBaseFilename(Path));
		ConstructorHelpers::FObjectFinder<UStaticMesh> Finder(*Full);
		CityMeshes.Add(Finder.Object);
	}

	constexpr float Half = LayoutHalfExtent;
	// Main highway cross slabs and deep sub-structure.
	AddBox(TEXT("SubStructure"), Scenery, FVector(0.0f, 0.0f, -260.0f), FVector(Half * 2.4f, Half * 2.4f, 320.0f), 0.0f, false);
	AddBox(TEXT("HighwayX"), Asphalt, FVector(0.0f, 0.0f, -50.0f), FVector(Half * 2.0f, 3600.0f, 100.0f));
	AddBox(TEXT("HighwayY"), Asphalt, FVector(0.0f, 0.0f, -50.0f), FVector(3600.0f, Half * 2.0f, 100.0f));

	BuildCenterCourt();

	for (int32 Quarter = 0; Quarter < 4; ++Quarter)
	{
		BuildQuarter(Quarter);
		BuildPulseGate(Quarter);
		BuildCyberCity(Quarter);
	}

	// Cyber night grading: crisp contrast, vibrant neon bloom, cool night white balance.
	GradeVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("GradeVolume"));
	GradeVolume->SetupAttachment(SceneRoot);
	GradeVolume->SetBoxExtent(FVector(15000.0f, 15000.0f, 6500.0f));
	GradeVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GradeVolume->SetGenerateOverlapEvents(false);
	GradeVolume->SetHiddenInGame(true);

	Grade = CreateDefaultSubobject<UPostProcessComponent>(TEXT("Grade"));
	Grade->SetupAttachment(GradeVolume);
	Grade->bUnbound = false;
	FPostProcessSettings& Look = Grade->Settings;
	Look.bOverride_ColorSaturation = true;
	Look.ColorSaturation = FVector4(1.16f, 1.16f, 1.2f, 1.0f);
	Look.bOverride_ColorContrast = true;
	Look.ColorContrast = FVector4(1.08f, 1.08f, 1.1f, 1.0f);
	Look.bOverride_BloomIntensity = true;
	Look.BloomIntensity = 0.85f;
	Look.bOverride_VignetteIntensity = true;
	Look.VignetteIntensity = 0.36f;
	Look.bOverride_WhiteTemp = true;
	Look.WhiteTemp = 5400.0f;
}

void AChaosImpactCyberStage::BuildCenterCourt()
{
	using CyberStage::CenterCourtTop;
	// Raised octagonal hologram stage in the center (+70 cm), formed by two overlapping slabs and a central pedestal.
	AddBox(TEXT("CenterSlabA"), Deck, FVector(0.0f, 0.0f, CenterCourtTop * 0.5f), FVector(1640.0f, 1160.0f, CenterCourtTop));
	AddBox(TEXT("CenterSlabB"), Deck, FVector(0.0f, 0.0f, CenterCourtTop * 0.5f), FVector(1160.0f, 1640.0f, CenterCourtTop));
	AddBox(TEXT("CenterSlabDiagA"), Deck, FVector(0.0f, 0.0f, CenterCourtTop * 0.5f), FVector(1520.0f, 1280.0f, CenterCourtTop), 45.0f);
	AddBox(TEXT("CenterSlabDiagB"), Deck, FVector(0.0f, 0.0f, CenterCourtTop * 0.5f), FVector(1280.0f, 1520.0f, CenterCourtTop), 45.0f);

	// Central Holo-Core pedestal (+30 cm above the court) where the central ball pad sits.
	AddCylinder(TEXT("CenterPedestal"), Trim, FVector(0.0f, 0.0f, CenterCourtTop), 520.0f, 30.0f);
	AddRing(TEXT("CenterPedestalRing"), Glow, FVector(0.0f, 0.0f, CenterCourtTop + 31.0f), 260.0f, 5.0f);
	AddRing(TEXT("CenterOuterRing"), Glow, FVector(0.0f, 0.0f, CenterCourtTop + 1.2f), 740.0f, 4.0f);
	AddRing(TEXT("CenterInnerRing"), NeonGlow + 1, FVector(0.0f, 0.0f, CenterCourtTop + 1.4f), 460.0f, 3.5f);

	// Overhead hologram rings high above the center (purely visual, well above camera/ball arcs).
	HoloRingRoot = CreateDefaultSubobject<USceneComponent>(TEXT("HoloRingRoot"));
	HoloRingRoot->SetupAttachment(SceneRoot);
	HoloRingRoot->SetRelativeLocation(FVector(0.0f, 0.0f, 1180.0f));
	AddPart(HoloRingRoot, TEXT("OverheadHaloOuter"), Glow, RingMesh.Get(), FVector::ZeroVector,
		FVector(920.0f * 100.0f / 70.0f, 920.0f * 100.0f / 70.0f, 18.0f * 100.0f / 13.0f), FRotator::ZeroRotator, false);
	AddPart(HoloRingRoot, TEXT("OverheadHaloInner"), NeonGlow + 1, RingMesh.Get(), FVector(0.0f, 0.0f, -45.0f),
		FVector(640.0f * 100.0f / 70.0f, 640.0f * 100.0f / 70.0f, 12.0f * 100.0f / 13.0f), FRotator(8.0f, 45.0f, 0.0f), false);
}

void AChaosImpactCyberStage::BuildQuarter(const int32 Quarter)
{
	using CyberStage::CenterCourtTop;
	using CyberStage::OverpassTop;
	using CyberStage::WallHeight;
	constexpr float Half = LayoutHalfExtent;
	const FString Q = FString::Printf(TEXT("_Q%d"), Quarter);
	const float Turned = 90.0f * Quarter;
	const uint8 NeonHere = Neon + Quarter;
	const uint8 DarkHere = NeonDark + Quarter;
	const uint8 GlowHere = NeonGlow + Quarter;
	const uint8 NextNeon = Neon + (Quarter + 1) % 4;
	const uint8 NextGlow = NeonGlow + (Quarter + 1) % 4;

	const auto At = [Quarter](const float X, const float Y, const float Z) { return CyberStage::Turn(FVector(X, Y, Z), Quarter); };
	const auto Box = [&](const TCHAR* Name, const uint8 Paint, const FVector& Location, const FVector& Size, const float Yaw,
		const bool bCollision, const bool bChamfer = false)
	{
		return AddBox(FString(Name) + Q, Paint, CyberStage::Turn(Location, Quarter), Size, Yaw + Turned, bCollision, bChamfer);
	};
	const auto Ramp = [&](const TCHAR* Name, const uint8 Paint, const FVector& Low, const FVector& High, const float Width)
	{
		return AddRamp(FString(Name) + Q, Paint, CyberStage::Turn(Low, Quarter), CyberStage::Turn(High, Quarter), Width, GlowHere);
	};

	const float Diagonal = BevelLine / UE_SQRT_2;
	const FVector CornerDir = FVector(1.0f, 0.0f, 0.0f).RotateAngleAxis(45.0f, FVector::UpVector);

	// ---- Corner highway slab and outer sound-barrier walls ------------------------------------------------------
	Box(TEXT("HighwayCorner"), Asphalt, FVector(2100.0f, 2100.0f, -50.0f), FVector(1420.0f, 2540.0f, 100.0f), 45.0f, true);
	Box(TEXT("OuterWall"), Wall, FVector(Half + 30.0f, 0.0f, WallHeight * 0.5f), FVector(60.0f, 3660.0f, WallHeight), 0.0f, true);
	Box(TEXT("OuterWallCap"), GlowHere, FVector(Half + 30.0f, 0.0f, WallHeight + 6.0f), FVector(72.0f, 3660.0f, 12.0f), 0.0f, false);
	Box(TEXT("OuterWallNeonLow"), GlowHere, FVector(Half - 3.0f, 0.0f, 240.0f), FVector(6.0f, 3560.0f, 18.0f), 0.0f, false);
	Box(TEXT("OuterWallNeonHigh"), Glow, FVector(Half - 3.0f, 0.0f, 440.0f), FVector(6.0f, 3560.0f, 10.0f), 0.0f, false);

	const FVector BevelMid = CornerDir * (Diagonal + 30.0f);
	Box(TEXT("BevelWall"), Wall, FVector(BevelMid.X, BevelMid.Y, WallHeight * 0.5f), FVector(60.0f, 2320.0f, WallHeight), 45.0f, true);
	Box(TEXT("BevelWallCap"), NextGlow, FVector(BevelMid.X, BevelMid.Y, WallHeight + 6.0f), FVector(72.0f, 2320.0f, 12.0f), 45.0f, false);
	const FVector BevelFace = CornerDir * (Diagonal - 3.0f);
	Box(TEXT("BevelWallBand"), GlowHere, FVector(BevelFace.X, BevelFace.Y, 280.0f), FVector(6.0f, 2200.0f, 20.0f), 45.0f, false);

	// ---- Road markings & crosswalks on the asphalt deck ---------------------------------------------------------
	for (int32 StripeIdx = -2; StripeIdx <= 2; ++StripeIdx)
	{
		Box(*FString::Printf(TEXT("Crosswalk%d"), StripeIdx + 2), Stripe,
			FVector(1420.0f, StripeIdx * 150.0f, 0.7f), FVector(220.0f, 70.0f, 1.2f), 0.0f, false);
	}
	Box(TEXT("LaneLineL"), GlowHere, FVector(1950.0f, -920.0f, 0.7f), FVector(880.0f, 16.0f, 1.2f), 0.0f, false);
	Box(TEXT("LaneLineR"), GlowHere, FVector(1950.0f, 920.0f, 0.7f), FVector(880.0f, 16.0f, 1.2f), 0.0f, false);

	// ---- Center court cardinal ramp & corner reflector pylon ----------------------------------------------------
	Ramp(TEXT("CenterRamp"), Trim, FVector(1220.0f, 0.0f, 0.0f), FVector(820.0f, 0.0f, CenterCourtTop), 440.0f);
	Box(TEXT("CenterEdgeGlowL"), GlowHere, FVector(820.0f, 400.0f, CenterCourtTop + 1.5f), FVector(10.0f, 360.0f, 3.0f), 0.0f, false);
	Box(TEXT("CenterEdgeGlowR"), GlowHere, FVector(820.0f, -400.0f, CenterCourtTop + 1.5f), FVector(10.0f, 360.0f, 3.0f), 0.0f, false);

	// Angled hologram reflector pylon on the corner of the center court: banks shots cleanly across the middle.
	const FVector PylonAt(540.0f, 540.0f, CenterCourtTop + 95.0f);
	Box(TEXT("HoloPylon"), Trim, PylonAt, FVector(70.0f, 240.0f, 190.0f), 45.0f, true, true);
	Box(TEXT("HoloPylonNeon"), GlowHere, PylonAt + FVector(0.0f, 0.0f, 98.0f), FVector(76.0f, 244.0f, 8.0f), 45.0f, false);
	Box(TEXT("HoloPylonBand"), Glow, PylonAt, FVector(78.0f, 246.0f, 18.0f), 45.0f, false);

	// ---- Elevated Highway Overpass (+120 cm) along the outer wall -----------------------------------------------
	Box(TEXT("OverpassDeck"), Deck, FVector(2950.0f, 0.0f, OverpassTop * 0.5f), FVector(900.0f, 2500.0f, OverpassTop), 0.0f, true);
	Box(TEXT("OverpassFace"), DarkHere, FVector(2497.0f, 0.0f, OverpassTop * 0.5f), FVector(6.0f, 2500.0f, OverpassTop - 8.0f), 0.0f, false);
	Box(TEXT("OverpassEdgeGlow"), GlowHere, FVector(2504.0f, 0.0f, OverpassTop + 1.5f), FVector(10.0f, 2500.0f, 3.0f), 0.0f, false);
	AddRing(TEXT("OverpassSpawnRing") + Q, GlowHere, At(2950.0f, 0.0f, OverpassTop + 1.2f), 190.0f, 4.0f);

	// Wide ramps on both ends of the overpass so players and CPUs can ascend/descend smoothly without bottlenecks.
	Ramp(TEXT("OverpassRampN"), Trim, FVector(2900.0f, 1750.0f, 0.0f), FVector(2900.0f, 1250.0f, OverpassTop), 400.0f);
	Ramp(TEXT("OverpassRampS"), Trim, FVector(2900.0f, -1750.0f, 0.0f), FVector(2900.0f, -1250.0f, OverpassTop), 400.0f);
	// Central step block in front of the overpass so players can also hop/step up from the middle lane.
	Box(TEXT("OverpassCenterStep"), Trim, FVector(2360.0f, 0.0f, OverpassTop * 0.25f), FVector(280.0f, 520.0f, OverpassTop * 0.5f), 0.0f, true, true);

	// Sector number stencilled on the overpass deck.
	UTextRenderComponent* SectorText = CreateDefaultSubobject<UTextRenderComponent>(*(TEXT("SectorNumber") + Q));
	SectorText->SetupAttachment(SceneRoot);
	SectorText->SetText(FText::FromString(FString::Printf(TEXT("0%d"), Quarter + 1)));
	SectorText->SetWorldSize(320.0f);
	if (StencilMaterial)
	{
		SectorText->SetTextMaterial(StencilMaterial);
	}
	SectorText->SetHorizontalAlignment(EHTA_Center);
	SectorText->SetVerticalAlignment(EVRTA_TextCenter);
	SectorText->SetRelativeLocation(CyberStage::Spread(At(2960.0f, -520.0f, OverpassTop + 1.0f)));
	SectorText->SetRelativeRotation(FRotator(90.0f, 180.0f, 0.0f));
	SectorText->SetTextRenderColor((StripeColor * 0.65f).ToFColor(true));
	SectorText->ComponentTags.Add(TEXT("CISectorText"));

	// ---- Bank-shot reflector wall & corner retreat shelter ------------------------------------------------------
	// Tall 45-degree bank wall outside the Pulse Gate: bounces balls into the corner pocket or across the mid lane.
	Box(TEXT("ReflectorWall"), Trim, FVector(1620.0f, 1620.0f, 150.0f), FVector(64.0f, 640.0f, 300.0f), 45.0f, true, true);
	Box(TEXT("ReflectorGlowTop"), GlowHere, FVector(1620.0f, 1620.0f, 304.0f), FVector(70.0f, 620.0f, 8.0f), 45.0f, false);
	Box(TEXT("ReflectorGlowMid"), Glow, FVector(1620.0f, 1620.0f, 160.0f), FVector(72.0f, 644.0f, 14.0f), 45.0f, false);

	// L-shaped sound barrier sheltering the corner spawn/retreat pocket.
	Box(TEXT("CornerShelterA"), NeonHere, FVector(2220.0f, 1520.0f, 110.0f), FVector(460.0f, 64.0f, 220.0f), 0.0f, true, true);
	Box(TEXT("CornerShelterB"), NextNeon, FVector(1520.0f, 2220.0f, 110.0f), FVector(64.0f, 460.0f, 220.0f), 0.0f, true, true);
	Box(TEXT("CornerShelterCapA"), GlowHere, FVector(2220.0f, 1520.0f, 224.0f), FVector(464.0f, 70.0f, 8.0f), 0.0f, false);
	Box(TEXT("CornerShelterCapB"), NextGlow, FVector(1520.0f, 2220.0f, 224.0f), FVector(70.0f, 464.0f, 8.0f), 0.0f, false);

	// Jumpable cyber-bollard cover blocks in the mid lane.
	Box(TEXT("MidCoverL"), Trim, FVector(1920.0f, -680.0f, 47.5f), FVector(340.0f, 80.0f, 95.0f), -25.0f, true, true);
	Box(TEXT("MidCoverR"), Trim, FVector(1920.0f, 680.0f, 47.5f), FVector(340.0f, 80.0f, 95.0f), 25.0f, true, true);
	AddCylinder(TEXT("BollardA") + Q, NeonHere, At(1450.0f, -980.0f, 0.0f), 120.0f, 95.0f, true);
	AddRing(TEXT("BollardBandA") + Q, GlowHere, At(1450.0f, -980.0f, 70.0f), 64.0f, 8.0f);

	// Overhead highway gantry portal spanning the outer overpass.
	for (const float Side : {-1080.0f, 1080.0f})
	{
		const FString Leg = Side < 0.0f ? TEXT("S") : TEXT("N");
		AddCylinder(TEXT("GantryPost") + Leg + Q, Steel, At(3320.0f, Side, OverpassTop), 56.0f, 480.0f, false);
	}
	Box(TEXT("GantryBeam"), Steel, FVector(3320.0f, 0.0f, OverpassTop + 500.0f), FVector(80.0f, 2260.0f, 70.0f), 0.0f, false);
	Box(TEXT("GantrySign"), DarkHere, FVector(3270.0f, 0.0f, OverpassTop + 500.0f), FVector(20.0f, 1400.0f, 130.0f), 0.0f, false);
	Box(TEXT("GantrySignTrim"), GlowHere, FVector(3258.0f, 0.0f, OverpassTop + 500.0f), FVector(6.0f, 1360.0f, 18.0f), 0.0f, false);

	// ---- Urban props inside and along the perimeter -------------------------------------------------------------
	const auto Prop = [&](const TCHAR* Name, const uint8 Model, const FVector& LayoutLocation, const float Yaw, const float Scale,
		const bool bCollision)
	{
		return AddProp(FString(Name) + Q, CityMeshes[Model], CyberStage::Spread(CyberStage::Turn(LayoutLocation, Quarter)), Yaw + Turned, Scale, bCollision);
	};
	Prop(TEXT("WallBillboard"), Billboard, FVector(Half + 30.0f, 0.0f, WallHeight), 90.0f, 1.4f, false);
	Prop(TEXT("WallBillboardText"), BillboardText, FVector(Half + 30.0f, 0.0f, WallHeight), 90.0f, 1.4f, false);
	Prop(TEXT("BevelBillboard"), BillboardStand, FVector(BevelMid.X, BevelMid.Y, WallHeight), 135.0f, 1.15f, false);
	Prop(TEXT("WallGraffiti"), Graffiti, FVector(Half - 8.0f, -850.0f, 230.0f), 90.0f, 1.0f, false);
	Prop(TEXT("CornerBusStop"), BusStop, FVector(2820.0f, 2080.0f, 0.0f), 135.0f, 0.95f, true);
	Prop(TEXT("CornerBin"), TrashBin, FVector(2340.0f, 1390.0f, 0.0f), 0.0f, 1.0f, true);
}

void AChaosImpactCyberStage::BuildPulseGate(const int32 Gate)
{
	// Diagonal gate between the central court pillar (860, 860) and the outer reflector wall (1460, 1460).
	const FString Name = FString::Printf(TEXT("PulseGate%d"), Gate);
	const float Turned = 90.0f * Gate;
	const float Yaw = 45.0f + Turned;
	const uint8 GlowHere = NeonGlow + Gate;
	const FVector Center = GetGateOffset(Gate);
	const FVector Tangent = FRotator(0.0f, Yaw, 0.0f).RotateVector(FVector(0.0f, 1.0f, 0.0f));

	for (const float Sign : {-1.0f, 1.0f})
	{
		const FVector PostAt = Center + Tangent * Sign * (GateHalfWidth + 32.0f);
		AddPart(SceneRoot, FString::Printf(TEXT("%sPost%d"), *Name, Sign > 0 ? 1 : 0), Steel, ChamferMesh.Get(),
			PostAt + FVector(0.0f, 0.0f, GateHeight * 0.5f), FVector(70.0f, 70.0f, GateHeight), FRotator(0.0f, Yaw, 0.0f), true);
		AddPart(SceneRoot, FString::Printf(TEXT("%sEmitter%d"), *Name, Sign > 0 ? 1 : 0), GlowHere, BoxMesh.Get(),
			PostAt - Tangent * Sign * 34.0f + FVector(0.0f, 0.0f, GateHeight * 0.5f), FVector(14.0f, 10.0f, GateHeight - 40.0f),
			FRotator(0.0f, Yaw, 0.0f), false);
	}
	// Threshold strip on the asphalt so players always see where the barrier forms.
	AddPart(SceneRoot, Name + TEXT("Threshold"), GlowHere, BoxMesh.Get(), Center + FVector(0.0f, 0.0f, 1.0f),
		FVector(24.0f, GateHalfWidth * 2.0f, 2.0f), FRotator(0.0f, Yaw, 0.0f), false);

	// Overhead lintel bar.
	UStaticMeshComponent* TopBar = AddPart(SceneRoot, Name + TEXT("TopBar"), GlowHere, BoxMesh.Get(),
		Center + FVector(0.0f, 0.0f, GateHeight + 14.0f), FVector(36.0f, GateHalfWidth * 2.0f + 80.0f, 22.0f),
		FRotator(0.0f, Yaw, 0.0f), false);
	GateTopBars.Add(TopBar);

	// Translucent energy barrier plane (no static physics collision so dashing characters can pass through;
	// server handles ball reflection and non-dashing character knockback).
	UStaticMeshComponent* Field = CreateDefaultSubobject<UStaticMeshComponent>(*(Name + TEXT("Field")));
	Field->SetupAttachment(SceneRoot);
	Field->SetStaticMesh(BoxMesh.Get());
	Field->SetMaterial(0, Gate % 2 == 0 ? FieldTealMaterial.Get() : FieldAmberMaterial.Get());
	Field->SetRelativeLocation(Center + FVector(0.0f, 0.0f, GateHeight * 0.5f));
	Field->SetRelativeRotation(FRotator(0.0f, Yaw, 0.0f));
	Field->SetRelativeScale3D(FVector(10.0f, GateHalfWidth * 2.0f, GateHeight - 20.0f) / 100.0f);
	Field->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Field->SetCastShadow(false);
	Field->SetVisibility(false);
	GateFields.Add(Field);

	UPointLightComponent* Light = CreateDefaultSubobject<UPointLightComponent>(*(Name + TEXT("Light")));
	Light->SetupAttachment(SceneRoot);
	Light->SetRelativeLocation(Center + FVector(0.0f, 0.0f, 160.0f));
	Light->SetLightColor(Gate % 2 == 0 ? FLinearColor(0.0f, 0.9f, 1.0f) : FLinearColor(1.0f, 0.45f, 0.1f));
	Light->SetIntensity(1200.0f);
	Light->SetAttenuationRadius(950.0f);
	Light->SetCastShadows(false);
	GateLights.Add(Light);
}

void AChaosImpactCyberStage::BuildCyberCity(const int32 Quarter)
{
	// Placed beyond 6200 units so the opening camera sweep (radius 5600) never clips a building.
	const FString Q = FString::Printf(TEXT("_Q%d"), Quarter);
	const float Turned = 90.0f * Quarter;
	const uint8 GlowHere = NeonGlow + Quarter;
	const uint8 NextNeon = Neon + (Quarter + 1) % 4;
	const auto CityProp = [&](const FString& Name, const uint8 Model, const FVector& Location, const float Yaw, const float Scale)
	{
		return AddProp(Name + Q, CityMeshes[Model], CyberStage::Turn(Location, Quarter), Yaw + Turned, Scale, false);
	};

	// Outer elevated highway viaduct running parallel to this side wall.
	AddPart(SceneRoot, TEXT("ViaductDeck") + Q, Scenery, BoxMesh.Get(), CyberStage::Turn(FVector(6600.0f, 0.0f, -80.0f), Quarter),
		FVector(1400.0f, 11000.0f, 120.0f), FRotator(0.0f, Turned, 0.0f), false);
	AddPart(SceneRoot, TEXT("ViaductNeonIn") + Q, GlowHere, BoxMesh.Get(), CyberStage::Turn(FVector(5900.0f, 0.0f, -15.0f), Quarter),
		FVector(24.0f, 11000.0f, 18.0f), FRotator(0.0f, Turned, 0.0f), false);
	AddPart(SceneRoot, TEXT("ViaductNeonOut") + Q, Glow, BoxMesh.Get(), CyberStage::Turn(FVector(7300.0f, 0.0f, -15.0f), Quarter),
		FVector(24.0f, 11000.0f, 18.0f), FRotator(0.0f, Turned, 0.0f), false);

	const uint8 Tiles[] = {TileA, TileB, TileC};
	for (int32 Row = 0; Row < 5; ++Row)
	{
		CityProp(FString::Printf(TEXT("CityTile%d"), Row), Tiles[Row % 3],
			FVector(7800.0f, -3000.0f + Row * 1500.0f, -20.0f), 90.0f * (Row % 4), 1.0f);
	}
	CityProp(TEXT("SkyscraperMain"), TwistedTower, FVector(8200.0f, 7800.0f, -20.0f), 45.0f, 0.85f);
	CityProp(TEXT("SkyscraperNorth"), EcoTerrace, FVector(8300.0f, -2400.0f, -20.0f), 0.0f, 0.8f);
	CityProp(TEXT("SkyscraperSouth"), EcoSlope, FVector(8300.0f, 2400.0f, -20.0f), 0.0f, 0.8f);
	CityProp(TEXT("ViaductCarA"), CarA, FVector(6300.0f, -1800.0f, -18.0f), 90.0f, 1.0f);
	CityProp(TEXT("ViaductCarB"), CarB, FVector(6900.0f, 1400.0f, -18.0f), -90.0f, 1.0f);
	CityProp(TEXT("ViaductVan"), Van, FVector(6300.0f, 2600.0f, -18.0f), 90.0f, 1.0f);

	// Fallback cyber monoliths so the skyline still looks rich even if Cartoon City Free is absent.
	AddPart(SceneRoot, TEXT("CyberMonolithA") + Q, Scenery, BoxMesh.Get(), CyberStage::Turn(FVector(8800.0f, -3600.0f, 900.0f), Quarter),
		FVector(900.0f, 900.0f, 2400.0f), FRotator(0.0f, Turned, 0.0f), false);
	AddPart(SceneRoot, TEXT("CyberMonolithStripA") + Q, GlowHere, BoxMesh.Get(), CyberStage::Turn(FVector(8340.0f, -3600.0f, 1100.0f), Quarter),
		FVector(16.0f, 680.0f, 1800.0f), FRotator(0.0f, Turned, 0.0f), false);
	AddPart(SceneRoot, TEXT("CyberMonolithB") + Q, NextNeon, BoxMesh.Get(), CyberStage::Turn(FVector(9200.0f, 3800.0f, 1200.0f), Quarter),
		FVector(1100.0f, 1100.0f, 3000.0f), FRotator(0.0f, Turned + 15.0f, 0.0f), false);

	// Sweeping sky searchlight mounted on the outer viaduct.
	USceneComponent* SearchRoot = CreateDefaultSubobject<USceneComponent>(*(TEXT("SearchlightRoot") + Q));
	SearchRoot->SetupAttachment(SceneRoot);
	SearchRoot->SetRelativeLocation(CyberStage::Turn(FVector(6100.0f, 3100.0f, 80.0f), Quarter));
	SearchRoot->SetRelativeRotation(FRotator(0.0f, Turned, 0.0f));
	SearchlightRoots.Add(SearchRoot);
	AddPart(SearchRoot, TEXT("SearchBeam") + Q, GlowHere, CylinderMesh.Get(), FVector(0.0f, 0.0f, 0.0f),
		FVector(90.0f, 90.0f, 2800.0f), FRotator(22.0f, 0.0f, 14.0f), false);

	// Hovering traffic drone cruising above the outer highway.
	USceneComponent* Drone = CreateDefaultSubobject<USceneComponent>(*(TEXT("DroneRoot") + Q));
	Drone->SetupAttachment(SceneRoot);
	Drone->SetRelativeLocation(CyberStage::Turn(FVector(5100.0f, 0.0f, 520.0f), Quarter));
	Drone->SetRelativeRotation(FRotator(0.0f, Turned, 0.0f));
	DroneRoots.Add(Drone);
	AddPart(Drone, TEXT("DroneBody") + Q, Steel, ChamferMesh.Get(), FVector::ZeroVector, FVector(180.0f, 320.0f, 65.0f),
		FRotator::ZeroRotator, false);
	AddPart(Drone, TEXT("DroneHeadlight") + Q, GlowHere, BoxMesh.Get(), FVector(-92.0f, 0.0f, 0.0f), FVector(12.0f, 220.0f, 24.0f),
		FRotator::ZeroRotator, false);
}

UStaticMeshComponent* AChaosImpactCyberStage::AddPart(USceneComponent* Parent, const FString& Name, const uint8 Paint,
	UStaticMesh* Mesh, const FVector& Location, const FVector& Size, const FRotator& Rotation, const bool bCollision)
{
	UStaticMeshComponent* Block = CreateDefaultSubobject<UStaticMeshComponent>(*Name);
	Block->SetupAttachment(Parent ? Parent : SceneRoot.Get());
	Block->SetStaticMesh(Mesh ? Mesh : BoxMesh.Get());
	Block->SetRelativeLocation(Location);
	Block->SetRelativeRotation(Rotation);
	Block->SetRelativeScale3D(Size / 100.0f);
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

	const bool bGlow = Paint == Glow || (Paint >= NeonGlow && Paint < NeonGlow + 4);
	UMaterialInterface* Surface = FlatMaterial.Get();
	if (bGlow)
	{
		Surface = GlowMaterial.Get();
	}
	else if (Paint == Deck && MetalFloorMaterial)
	{
		Surface = MetalFloorMaterial.Get();
	}
	else if ((Paint == Asphalt || Paint == Trim) && ConcreteMaterial)
	{
		Surface = ConcreteMaterial.Get();
	}
	else if ((Paint == Wall || Paint == Scenery) && PanelMaterial)
	{
		Surface = PanelMaterial.Get();
	}
	else if (Paint != Steel && PaintMaterial)
	{
		Surface = PaintMaterial.Get();
	}
	Block->SetMaterial(0, Surface);
	Block->ComponentTags.Add(FName(*FString::Printf(TEXT("CIPaint%d"), Paint)));
	return Block;
}

UStaticMeshComponent* AChaosImpactCyberStage::AddBox(const FString& Name, const uint8 Paint, const FVector& Location,
	const FVector& Size, const float Yaw, const bool bCollision, const bool bChamfer)
{
	return AddPart(SceneRoot, Name, Paint, bChamfer ? ChamferMesh.Get() : BoxMesh.Get(), CyberStage::Spread(Location), CyberStage::Spread(Size),
		FRotator(0.0f, Yaw, 0.0f), bCollision);
}

UStaticMeshComponent* AChaosImpactCyberStage::AddCylinder(const FString& Name, const uint8 Paint, const FVector& Location,
	const float Diameter, const float Height, const bool bCollision)
{
	return AddPart(SceneRoot, Name, Paint, CylinderMesh.Get(), CyberStage::Spread(Location),
		FVector(Diameter * LayoutScale, Diameter * LayoutScale, Height), FRotator::ZeroRotator, bCollision);
}

UStaticMeshComponent* AChaosImpactCyberStage::AddRing(const FString& Name, const uint8 Paint, const FVector& Location,
	const float Radius, const float Thickness)
{
	UStaticMeshComponent* Block = AddPart(SceneRoot, Name, Paint, RingMesh.Get(), CyberStage::Spread(Location),
		FVector(Radius * LayoutScale * 100.0f / 70.0f, Radius * LayoutScale * 100.0f / 70.0f, Thickness * 100.0f / 13.0f),
		FRotator::ZeroRotator, false);
	return Block;
}

UStaticMeshComponent* AChaosImpactCyberStage::AddRamp(const FString& Name, const uint8 Paint, const FVector& LayoutLow,
	const FVector& LayoutHigh, const float LayoutWidth, const uint8 EdgePaint)
{
	const FVector Low = CyberStage::Spread(LayoutLow);
	const FVector High = CyberStage::Spread(LayoutHigh);
	const float Width = LayoutWidth * LayoutScale;
	const FVector Along = High - Low;
	const float Run = Along.Size2D();
	const float Length = Along.Size();
	const FRotator Slope(FMath::RadiansToDegrees(FMath::Atan2(Along.Z, Run)), Along.Rotation().Yaw, 0.0f);
	const float Thickness = FMath::Max(80.0f, Along.Z / FMath::Cos(FMath::DegreesToRadians(Slope.Pitch)) + 60.0f);
	const FVector TopCentre = (Low + High) * 0.5f;
	const FVector Normal = Slope.RotateVector(FVector::UpVector);

	UStaticMeshComponent* Block = AddPart(SceneRoot, Name, Paint, BoxMesh.Get(),
		TopCentre - Normal * Thickness * 0.5f, FVector(Length, Width, Thickness), Slope, true);
	if (EdgePaint != 255)
	{
		const FVector Across = Slope.RotateVector(FVector::RightVector);
		for (const float Side : {-1.0f, 1.0f})
		{
			AddPart(SceneRoot, Name + (Side < 0.0f ? TEXT("EdgeL") : TEXT("EdgeR")), EdgePaint, BoxMesh.Get(),
				TopCentre + Across * Side * (Width * 0.5f - 14.0f) + Normal * 0.8f,
				FVector(Length, 16.0f, 1.4f), Slope, false);
		}
	}
	return Block;
}

UStaticMeshComponent* AChaosImpactCyberStage::AddProp(const FString& Name, UStaticMesh* Mesh, const FVector& Location,
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

void AChaosImpactCyberStage::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyPalette();
}

void AChaosImpactCyberStage::BeginPlay()
{
	Super::BeginPlay();
	ApplyPalette();
	GateFieldLooks.Reset();
	for (UStaticMeshComponent* Field : GateFields)
	{
		GateFieldLooks.Add(Field ? Field->CreateDynamicMaterialInstance(0) : nullptr);
	}
	UpdateGatesLook(0.0f);
}

void AChaosImpactCyberStage::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AChaosImpactCyberStage, GatePattern);
	DOREPLIFETIME(AChaosImpactCyberStage, GateStartedAt);
}

FLinearColor AChaosImpactCyberStage::GetPaintColor(const uint8 Paint) const
{
	const auto NeonAt = [this](const int32 Quarter)
	{
		return NeonColors.IsValidIndex(Quarter) ? NeonColors[Quarter] : FLinearColor(0.0f, 0.85f, 1.0f);
	};
	if (Paint >= NeonGlow) { return NeonAt(Paint - NeonGlow); }
	if (Paint >= NeonDark) { return NeonAt(Paint - NeonDark) * 0.3f; }
	if (Paint >= Neon) { return NeonAt(Paint - Neon); }
	switch (Paint)
	{
	case Asphalt: return AsphaltColor;
	case Deck: return DeckColor;
	case Wall: return WallColor;
	case Trim: return TrimColor;
	case Glow: return GlowColor;
	case Stripe: return StripeColor;
	case Steel: return FLinearColor(0.05f, 0.06f, 0.085f);
	default: return SceneryColor;
	}
}

bool AChaosImpactCyberStage::ReadPaint(const UStaticMeshComponent* Block, uint8& OutPaint)
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

void AChaosImpactCyberStage::ApplyPalette()
{
	if (!bApplyDefaultColors)
	{
		return;
	}
	TArray<UTextRenderComponent*> Texts;
	GetComponents(Texts);
	for (UTextRenderComponent* Text : Texts)
	{
		if (Text->ComponentHasTag(TEXT("CISectorText")))
		{
			Text->SetTextRenderColor((StripeColor * 0.65f).ToFColor(true));
		}
	}
	TArray<UStaticMeshComponent*> Meshes;
	GetComponents(Meshes);
	for (UStaticMeshComponent* Block : Meshes)
	{
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
		if (Paint == Glow || (Paint >= NeonGlow && Paint < NeonGlow + 4))
		{
			Material->SetVectorParameterValue(TEXT("Color"), Color);
			Material->SetScalarParameterValue(TEXT("Intensity"), GlowIntensity);
		}
		else
		{
			Material->SetVectorParameterValue(TEXT("Color"), Color);
			Material->SetVectorParameterValue(TEXT("Base Color"), Color);
			Material->SetVectorParameterValue(TEXT("BaseColor"), Color);
			Material->SetScalarParameterValue(TEXT("Roughness"), Paint == Deck ? 0.35f : 0.58f);
		}
	}
}

TArray<FVector> AChaosImpactCyberStage::GetSpawnPoints() const
{
	return ToWorld(SpawnPointOffsets);
}

TArray<FVector> AChaosImpactCyberStage::GetBallPoints() const
{
	return ToWorld(BallPointOffsets);
}

bool AChaosImpactCyberStage::IsOutside(const FVector& Location) const
{
	if (Super::IsOutside(Location))
	{
		return true;
	}
	const FVector Local = GetActorTransform().InverseTransformPositionNoScale(Location);
	return FMath::Abs(Local.X) + FMath::Abs(Local.Y) > BevelLine * LayoutScale + (60.0f + OutsideMargin) * UE_SQRT_2;
}

double AChaosImpactCyberStage::GetServerNow() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0;
	}
	const AGameStateBase* State = World->GetGameState();
	return State ? State->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
}

bool AChaosImpactCyberStage::IsMatchOn() const
{
	const AChaosImpactGameState* Match = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	return Match && Match->bVersusMatch && Match->Phase == EChaosImpactOnlinePhase::Match;
}

bool AChaosImpactCyberStage::IsGateInPattern(const int32 Gate, const int32 Pattern) const
{
	if (Pattern < 0)
	{
		return false;
	}
	if (Pattern >= 2)
	{
		return true;
	}
	return (Gate % 2) == Pattern;
}

float AChaosImpactCyberStage::GetGateProgress() const
{
	if (GatePattern < 0)
	{
		return 0.0f;
	}
	const float Age = static_cast<float>(GetServerNow() - GateStartedAt);
	return Age < GateWarnSeconds
		? FMath::Clamp(Age / FMath::Max(GateWarnSeconds, 0.1f), 0.0f, 1.0f)
		: 1.0f + FMath::Clamp((Age - GateWarnSeconds) / FMath::Max(GateActiveSeconds, 0.1f), 0.0f, 1.0f);
}

void AChaosImpactCyberStage::StartGatePattern(const int32 Pattern)
{
	if (!HasAuthority())
	{
		return;
	}
	const int32 Clamped = FMath::Clamp(Pattern, 0, 2);
	GatePattern = Clamped;
	LastGatePattern = Clamped;
	GateStartedAt = static_cast<float>(GetServerNow());
	bGateLocked = false;
	GateHitCooldown.Reset();
	BallLastLocation.Reset();
	BallBounceCooldown.Reset();
	ForceNetUpdate();
	OnRep_GateState();
}

void AChaosImpactCyberStage::OnRep_GateState()
{
	if (GatePattern >= 0 && GetNetMode() != NM_DedicatedServer)
	{
		ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::NovaCharge, GetActorLocation() + FVector(0.0f, 0.0f, 160.0f), 0.5f, 1.35f);
	}
}

void AChaosImpactCyberStage::MulticastGateActivated_Implementation(const int32 Pattern)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	for (int32 Gate = 0; Gate < 4; ++Gate)
	{
		if (!IsGateInPattern(Gate, Pattern))
		{
			continue;
		}
		const FVector At = GetActorTransform().TransformPosition(GetGateOffset(Gate));
		ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::StageWarp, At, 0.7f, 1.25f);
		if (UNiagaraSystem* Burst = ChaosImpactBallTypes::LoadEffect(ChaosImpactBallTypes::Effects::WarpIn))
		{
			if (UNiagaraComponent* Effect = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
				this, Burst, At + FVector(0.0f, 0.0f, 120.0f), FRotator::ZeroRotator, FVector(1.5f)))
			{
				ChaosImpactBallTypes::SetEffectColor(Effect, TEXT("Base Color"),
					Gate % 2 == 0 ? FLinearColor(0.0f, 0.9f, 1.0f) : FLinearColor(1.0f, 0.5f, 0.1f));
			}
		}
	}
}

void AChaosImpactCyberStage::MulticastGateBounce_Implementation(const FVector_NetQuantize At, const int32 Gate)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::BeamStrike, At, 0.75f, 1.3f);
	if (UNiagaraSystem* Sparks = ChaosImpactBallTypes::LoadEffect(ChaosImpactBallTypes::Effects::SparkBurst))
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, Sparks, At, FRotator::ZeroRotator, FVector(1.6f));
	}
}

void AChaosImpactCyberStage::TickGatesServer(const double Now)
{
	for (auto It = GateHitCooldown.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || It.Value() <= Now)
		{
			It.RemoveCurrent();
		}
	}
	for (auto It = BallBounceCooldown.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || It.Value() <= Now)
		{
			It.RemoveCurrent();
		}
	}
	for (auto It = BallLastLocation.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}

	if (!IsMatchOn())
	{
		NextGateAt = -1.0;
		if (GatePattern >= 0)
		{
			GatePattern = -1;
			ForceNetUpdate();
		}
		return;
	}
	if (NextGateAt < 0.0)
	{
		NextGateAt = Now + FirstGateAfter;
	}
	if (GatePattern < 0)
	{
		if (Now >= NextGateAt)
		{
			int32 Next = FMath::RandRange(0, 2);
			if (Next == LastGatePattern)
			{
				Next = (Next + 1) % 3;
			}
			StartGatePattern(Next);
		}
		return;
	}

	const float Age = static_cast<float>(Now - GateStartedAt);
	if (Age >= GateWarnSeconds && !bGateLocked)
	{
		bGateLocked = true;
		MulticastGateActivated(GatePattern);
	}
	if (bGateLocked && Age < GateWarnSeconds + GateActiveSeconds)
	{
		const FTransform& StageTransform = GetActorTransform();
		// 1. Non-dashing characters who walk into an active gate take contact damage and are knocked back.
		for (TActorIterator<AChaosImpactCharacter> It(GetWorld()); It; ++It)
		{
			AChaosImpactCharacter* Character = *It;
			if (Character->IsEliminated())
			{
				continue;
			}
			if (const double* Cooldown = GateHitCooldown.Find(Character); Cooldown && *Cooldown > Now)
			{
				continue;
			}
			for (int32 Gate = 0; Gate < 4; ++Gate)
			{
				if (!IsGateInPattern(Gate, GatePattern))
				{
					continue;
				}
				const FVector Origin = StageTransform.TransformPosition(GetGateOffset(Gate));
				const FVector Normal = StageTransform.TransformVectorNoScale(GetGateNormal(Gate));
				const FVector Tangent = FVector::CrossProduct(Normal, FVector::UpVector);
				const FVector Rel = Character->GetActorLocation() - Origin;
				const float Depth = FVector::DotProduct(Rel, Normal);
				const float Across = FVector::DotProduct(Rel, Tangent);
				if (FMath::Abs(Depth) < 62.0f && FMath::Abs(Across) < GateHalfWidth && Rel.Z > -40.0f && Rel.Z < GateHeight + 60.0f)
				{
					const float Applied = Character->TakeDamage(GateContactDamage, FDamageEvent(), nullptr, this);
					if (Applied > 0.0f)
					{
						GateHitCooldown.Add(Character, Now + 0.85);
						if (!Character->IsEliminated())
						{
							const float PushSign = Depth >= 0.0f ? 1.0f : -1.0f;
							Character->LaunchCharacter(Normal * (PushSign * 560.0f) + FVector(0.0f, 0.0f, 440.0f), true, true);
						}
					}
					break;
				}
			}
		}

		// 2. Flying balls reflect off active gates with a 1.25x speed boost.
		for (TActorIterator<AChaosImpactBall> It(GetWorld()); It; ++It)
		{
			AChaosImpactBall* Ball = *It;
			const EChaosImpactBallType Type = Ball->GetBallType();
			if (!Ball->IsFlyingOnServer() || Type == EChaosImpactBallType::Beam || Type == EChaosImpactBallType::Nova)
			{
				BallLastLocation.Remove(Ball);
				continue;
			}
			const FVector CurrentPos = Ball->GetActorLocation();
			const FVector* PrevPos = BallLastLocation.Find(Ball);
			const double* BounceUntil = BallBounceCooldown.Find(Ball);
			if (PrevPos && (!BounceUntil || *BounceUntil <= Now))
			{
				for (int32 Gate = 0; Gate < 4; ++Gate)
				{
					if (!IsGateInPattern(Gate, GatePattern))
					{
						continue;
					}
					const FVector Origin = StageTransform.TransformPosition(GetGateOffset(Gate));
					const FVector Normal = StageTransform.TransformVectorNoScale(GetGateNormal(Gate));
					const float Before = FVector::DotProduct(*PrevPos - Origin, Normal);
					const float After = FVector::DotProduct(CurrentPos - Origin, Normal);
					if (Before * After > 0.0f && FMath::Abs(After) > 36.0f)
					{
						continue;
					}
					const float Alpha = FMath::Abs(Before) / FMath::Max(FMath::Abs(Before - After), KINDA_SMALL_NUMBER);
					const FVector HitPoint = FMath::Lerp(*PrevPos, CurrentPos, FMath::Clamp(Alpha, 0.0f, 1.0f));
					const FVector Rel = HitPoint - Origin;
					const FVector Tangent = FVector::CrossProduct(Normal, FVector::UpVector);
					if (FMath::Abs(FVector::DotProduct(Rel, Tangent)) < GateHalfWidth && Rel.Z > -20.0f && Rel.Z < GateHeight + 40.0f)
					{
						const FVector Vel = Ball->GetBallVelocity();
						const FVector Reflected = (Vel - 2.0f * FVector::DotProduct(Vel, Normal) * Normal) * GateBallBoost;
						const float OutSign = Before >= 0.0f ? 1.0f : -1.0f;
						Ball->SetActorLocation(HitPoint + Normal * (OutSign * 44.0f), false, nullptr, ETeleportType::TeleportPhysics);
						Ball->DeflectByWind(Reflected);
						BallBounceCooldown.Add(Ball, Now + 0.22);
						MulticastGateBounce(HitPoint, Gate);
						break;
					}
				}
			}
			BallLastLocation.Add(Ball, Ball->GetActorLocation());
		}
	}
	if (Age >= GateWarnSeconds + GateActiveSeconds)
	{
		GatePattern = -1;
		BallLastLocation.Reset();
		ForceNetUpdate();
		NextGateAt = Now + FMath::FRandRange(GateIntervalMin, FMath::Max(GateIntervalMin, GateIntervalMax));
	}
}

void AChaosImpactCyberStage::UpdateGatesLook(const float DeltaSeconds)
{
	const float Progress = GetGateProgress();
	for (int32 Gate = 0; Gate < GateFields.Num(); ++Gate)
	{
		const bool bSelected = IsGateInPattern(Gate, GatePattern);
		const float Warn = bSelected && Progress < 1.0f ? Progress : 0.0f;
		const bool bActive = bSelected && Progress >= 1.0f;
		const float ActiveT = bActive ? Progress - 1.0f : 0.0f;

		if (UStaticMeshComponent* Field = GateFields[Gate])
		{
			const bool bShow = bActive || (Warn > 0.0f && FMath::Sin(LookClock * (14.0f + 24.0f * Warn)) > -0.1f);
			Field->SetVisibility(bShow);
			const float HeightScale = bActive ? (GateHeight - 20.0f) / 100.0f
				: (GateHeight - 20.0f) * (0.2f + 0.8f * Warn) / 100.0f;
			Field->SetRelativeScale3D(FVector(0.1f, GateHalfWidth * 2.0f / 100.0f, HeightScale));
		}
		if (UMaterialInstanceDynamic* Look = GateFieldLooks.IsValidIndex(Gate) ? GateFieldLooks[Gate].Get() : nullptr)
		{
			const float Intensity = bActive ? (8.5f - 3.0f * ActiveT) : (1.5f + 5.0f * Warn);
			Look->SetScalarParameterValue(TEXT("Intensity"), Intensity);
		}
		if (UPointLightComponent* Light = GateLights.IsValidIndex(Gate) ? GateLights[Gate].Get() : nullptr)
		{
			Light->SetIntensity(bActive ? 26000.0f : Warn > 0.0f ? 9000.0f * Warn + 1500.0f : 1200.0f);
		}
	}
}

void AChaosImpactCyberStage::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!GetWorld())
	{
		return;
	}
	if (HasAuthority())
	{
		TickGatesServer(GetServerNow());
	}
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	LookClock += DeltaSeconds;
	UpdateGatesLook(DeltaSeconds);

	if (HoloRingRoot)
	{
		HoloRingRoot->AddLocalRotation(FRotator(0.0f, 18.0f * DeltaSeconds, 0.0f));
	}
	for (int32 Index = 0; Index < SearchlightRoots.Num(); ++Index)
	{
		if (USceneComponent* Search = SearchlightRoots[Index])
		{
			const float Sweep = FMath::Sin(LookClock * 0.7f + Index * 1.57f) * 28.0f;
			Search->SetRelativeRotation(FRotator(0.0f, 90.0f * Index + Sweep, 0.0f));
		}
	}
	for (int32 Index = 0; Index < DroneRoots.Num(); ++Index)
	{
		if (USceneComponent* Drone = DroneRoots[Index])
		{
			const float Travel = FMath::Sin(LookClock * 0.55f + Index * 1.57f) * 2400.0f;
			const float Bob = FMath::Cos(LookClock * 2.1f + Index) * 35.0f;
			Drone->SetRelativeLocation(CyberStage::Turn(FVector(5100.0f, Travel, 520.0f + Bob), Index));
		}
	}
}
