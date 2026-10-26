#include "ChaosImpactSnowStage.h"

#include "ChaosImpact.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactBallTypes.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactSfx.h"
#include "ChaosImpactStageDecor.h"
#include "Components/BoxComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraFunctionLibrary.h"
#include "UObject/ConstructorHelpers.h"

namespace SnowStage
{
	constexpr float PondFloorZ = -60.0f;
	constexpr float VerandaZ = 0.0f;
	constexpr float BridgeCrownZ = 28.0f;
	constexpr float WallHeight = 560.0f;

	const FVector QuarterSpawnOffsets[] =
	{
		FVector(2780.0f, 0.0f, VerandaZ),         // wooden veranda behind the Taiko bridge
		FVector(2320.0f, 1820.0f, VerandaZ),      // corner veranda sheltered by the folding screen
		FVector(1220.0f, -520.0f, PondFloorZ)     // open ice pond beside the bridge
	};

	const FVector QuarterBallOffsets[] =
	{
		FVector(2480.0f, 920.0f, VerandaZ),       // outer veranda flank pad
		FVector(960.0f, 0.0f, PondFloorZ)         // frozen pond pad in front of the bridge
	};

	FVector Turn(const FVector& Offset, const int32 Quarter)
	{
		return FRotator(0.0f, 90.0f * Quarter, 0.0f).RotateVector(Offset);
	}

	FVector Spread(const FVector& Layout)
	{
		return FVector(Layout.X * AChaosImpactSnowStage::LayoutScale, Layout.Y * AChaosImpactSnowStage::LayoutScale, Layout.Z);
	}
}

FVector AChaosImpactSnowStage::GetWindDirectionVector(const int32 WindDir)
{
	switch (WindDir & 3)
	{
	case 0: return FVector(1.0f, 0.0f, 0.0f);
	case 1: return FVector(0.0f, 1.0f, 0.0f);
	case 2: return FVector(-1.0f, 0.0f, 0.0f);
	default: return FVector(0.0f, -1.0f, 0.0f);
	}
}

AChaosImpactSnowStage::AChaosImpactSnowStage()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);
	SetNetUpdateFrequency(10.0f);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	QuarterColors = {
		FLinearColor(0.92f, 0.16f, 0.14f),   // Vermilion (North / +X)
		FLinearColor(0.12f, 0.72f, 0.98f),   // Ice Cyan (East / +Y)
		FLinearColor(0.98f, 0.72f, 0.14f),   // Amber Gold (South / -X)
		FLinearColor(0.18f, 0.85f, 0.52f)    // Pine Jade (West / -Y)
	};

	// Central Wind-Dial Island ball pad + 2 per quarter = 9 ball pads.
	BallPointOffsets.Add(FVector(0.0f, 0.0f, 16.0f));
	for (int32 Quarter = 0; Quarter < 4; ++Quarter)
	{
		for (const FVector& Offset : SnowStage::QuarterSpawnOffsets)
		{
			SpawnPointOffsets.Add(SnowStage::Spread(SnowStage::Turn(Offset, Quarter)));
		}
		for (const FVector& Offset : SnowStage::QuarterBallOffsets)
		{
			BallPointOffsets.Add(SnowStage::Spread(SnowStage::Turn(Offset, Quarter)));
		}
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Box(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(TEXT("/Engine/BasicShapes/Cone.Cone"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Chamfer(TEXT("/Game/LevelPrototyping/Meshes/SM_ChamferCube.SM_ChamferCube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Game/LevelPrototyping/Meshes/SM_Cylinder.SM_Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Ring(
		TEXT("/Game/LevelPrototyping/Interactable/JumpPad/Assets/Meshes/SM_CircularBand.SM_CircularBand"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Flat(TEXT("/Game/LevelPrototyping/Materials/M_FlatCol.M_FlatCol"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Glowing(TEXT("/Game/ChaosImpact/Versus/M_StageGlow.M_StageGlow"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Planks(TEXT("/Game/ChaosImpact/Versus/Splash/M_CI_Planks.M_CI_Planks"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> PaleStone(
		TEXT("/Game/ChaosImpact/solo/Stage2/Materials/MI_Stone_Pale.MI_Stone_Pale"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> DarkStone(
		TEXT("/Game/ChaosImpact/solo/Stage2/Materials/MI_Stone_Dark.MI_Stone_Dark"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Gold(
		TEXT("/Game/ChaosImpact/solo/Stage2/Materials/MI_Gold.MI_Gold"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Paint(TEXT("/Game/ChaosImpact/Versus/Splash/M_CI_GlossPaint.M_CI_GlossPaint"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FieldSoft(
		TEXT("/Game/ChaosImpact/solo/Stage2/Materials/MI_Field_TealSoft.MI_Field_TealSoft"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Pine(
		TEXT("/Game/ChaosImpact/solo/Stage14/Materials/MI_Pine.MI_Pine"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Bark(
		TEXT("/Game/ChaosImpact/solo/Stage14/Materials/MI_PineBark.MI_PineBark"));

	BoxMesh = Box.Object;
	ConeMesh = Cone.Object;
	ChamferMesh = Chamfer.Succeeded() ? Chamfer.Object : Box.Object;
	CylinderMesh = Cylinder.Object;
	RingMesh = Ring.Object;
	FlatMaterial = Flat.Object;
	GlowMaterial = Glowing.Succeeded() ? Glowing.Object : Flat.Object;
	PlanksMaterial = Planks.Succeeded() ? Planks.Object : nullptr;
	PaleStoneMaterial = PaleStone.Succeeded() ? PaleStone.Object : nullptr;
	DarkStoneMaterial = DarkStone.Succeeded() ? DarkStone.Object : nullptr;
	GoldMaterial = Gold.Succeeded() ? Gold.Object : nullptr;
	GlossPaintMaterial = Paint.Succeeded() ? Paint.Object : nullptr;
	FieldSoftMaterial = FieldSoft.Succeeded() ? FieldSoft.Object : GlowMaterial;
	PineMaterial = Pine.Succeeded() ? Pine.Object : nullptr;
	BarkMaterial = Bark.Succeeded() ? Bark.Object : nullptr;

	BuildFrozenPondAndCenter();

	for (int32 Quarter = 0; Quarter < 4; ++Quarter)
	{
		BuildQuarter(Quarter);
		BuildWinterScenery(Quarter);
	}

	// Crisp winter atmosphere grading.
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
	Look.ColorSaturation = FVector4(1.08f, 1.1f, 1.14f, 1.0f);
	Look.bOverride_ColorContrast = true;
	Look.ColorContrast = FVector4(1.05f, 1.05f, 1.07f, 1.0f);
	Look.bOverride_BloomIntensity = true;
	Look.BloomIntensity = 0.6f;
	Look.bOverride_VignetteIntensity = true;
	Look.VignetteIntensity = 0.26f;
	Look.bOverride_WhiteTemp = true;
	Look.WhiteTemp = 5800.0f;
}

void AChaosImpactSnowStage::BuildFrozenPondAndCenter()
{
	using SnowStage::PondFloorZ;
	// Deep dark-blue lake bed beneath the ice and the main frozen pond slab at Z = PondFloorZ (-60).
	AddBox(TEXT("LakeBed"), DarkTimber, FVector(0.0f, 0.0f, PondFloorZ - 95.0f), FVector(4400.0f, 4400.0f, 50.0f), 0.0f, false);
	AddBox(TEXT("FrozenPondMain"), Ice, FVector(0.0f, 0.0f, PondFloorZ - 30.0f), FVector(3800.0f, 3800.0f, 60.0f), 0.0f, true);

	// Frost rings embedded in the ice around the center.
	AddRing(TEXT("PondFrostOuter"), Glow, FVector(0.0f, 0.0f, PondFloorZ + 0.8f), 1320.0f, 4.0f);
	AddRing(TEXT("PondFrostInner"), Glow, FVector(0.0f, 0.0f, PondFloorZ + 1.0f), 780.0f, 3.5f);

	// Central Wind-Dial Island (0 cm, 60 cm above the pond) with 4 wide step-ramps and a central dais (16 cm).
	AddCylinder(TEXT("CenterIslandBase"), SnowStone, FVector(0.0f, 0.0f, PondFloorZ), 940.0f, 60.0f, true);
	AddCylinder(TEXT("CenterIslandDais"), GoldTrim, FVector(0.0f, 0.0f, 0.0f), 460.0f, 16.0f, true);
	AddRing(TEXT("CenterIslandGoldRing"), GoldTrim, FVector(0.0f, 0.0f, 16.8f), 225.0f, 4.5f);
	AddRing(TEXT("CenterIslandRuneRing"), Glow, FVector(0.0f, 0.0f, 1.0f), 450.0f, 4.0f);

	// Four directional compass arrows inlaid on the central island floor (N, E, S, W).
	for (int32 Dir = 0; Dir < 4; ++Dir)
	{
		const FVector Out = GetWindDirectionVector(Dir);
		UStaticMeshComponent* Arrow = AddBox(FString::Printf(TEXT("CompassArrow%d"), Dir), AccentGlow + Dir,
			Out * 330.0f + FVector(0.0f, 0.0f, 1.8f), FVector(160.0f, 34.0f, 2.4f), 90.0f * Dir, false);
		CompassArrows.Add(Arrow);
	}

	// Overhead Wind-Vane ring floating high above the central island (Z = 1080) so it never blocks balls or sightlines.
	WindVaneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("WindVaneRoot"));
	WindVaneRoot->SetupAttachment(SceneRoot);
	WindVaneRoot->SetRelativeLocation(FVector(0.0f, 0.0f, 1080.0f));
	AddPart(WindVaneRoot, TEXT("WindVaneRing"), GoldTrim, RingMesh.Get(), FVector::ZeroVector,
		FVector(420.0f * 100.0f / 70.0f, 420.0f * 100.0f / 70.0f, 16.0f * 100.0f / 13.0f), FRotator::ZeroRotator, false);
	AddPart(WindVaneRoot, TEXT("WindVaneShaft"), Glow, BoxMesh.Get(), FVector(120.0f, 0.0f, 0.0f),
		FVector(620.0f, 28.0f, 18.0f), FRotator::ZeroRotator, false);
	AddPart(WindVaneRoot, TEXT("WindVaneTip"), AccentGlow, ConeMesh.Get(), FVector(450.0f, 0.0f, 0.0f),
		FVector(90.0f, 90.0f, 160.0f), FRotator(-90.0f, 0.0f, 0.0f), false);

	// Group of translucent wind-ribbon streamers that orient with WindDirectionIndex and surge across the pond.
	WindStreamerGroup = CreateDefaultSubobject<USceneComponent>(TEXT("WindStreamerGroup"));
	WindStreamerGroup->SetupAttachment(SceneRoot);
	WindStreamerGroup->SetRelativeLocation(FVector(0.0f, 0.0f, 110.0f));
	for (int32 Index = 0; Index < 8; ++Index)
	{
		const float Y = -1400.0f + Index * 400.0f;
		const float X = (Index % 2 == 0) ? -600.0f : 600.0f;
		UStaticMeshComponent* Streamer = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("WindStreamer%d"), Index));
		Streamer->SetupAttachment(WindStreamerGroup);
		Streamer->SetStaticMesh(BoxMesh.Get());
		Streamer->SetMaterial(0, FieldSoftMaterial.Get());
		Streamer->SetRelativeLocation(FVector(X, Y, (Index % 3) * 45.0f));
		Streamer->SetRelativeScale3D(FVector(780.0f, 18.0f, 3.0f) / 100.0f);
		Streamer->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Streamer->SetCastShadow(false);
		Streamer->SetVisibility(false);
		WindStreamers.Add(Streamer);
	}
}

void AChaosImpactSnowStage::BuildStoneLantern(const FString& Name, const FVector& Location, const uint8 GlowPaint)
{
	// Traditional Yukimi stone lantern (雪見灯籠): pedestal, shaft, fire-box, and broad snow-laden roof cap.
	AddCylinder(Name + TEXT("Base"), SnowStone, Location, 150.0f, 36.0f, true);
	AddCylinder(Name + TEXT("Shaft"), DarkTimber, Location + FVector(0.0f, 0.0f, 36.0f), 86.0f, 84.0f, true);
	AddBox(Name + TEXT("FireBox"), GlowPaint, Location + FVector(0.0f, 0.0f, 148.0f), FVector(84.0f, 84.0f, 56.0f), 0.0f, true);
	AddCylinder(Name + TEXT("Roof"), DarkTimber, Location + FVector(0.0f, 0.0f, 176.0f), 210.0f, 24.0f, true);
	AddCylinder(Name + TEXT("SnowCap"), SnowStone, Location + FVector(0.0f, 0.0f, 200.0f), 196.0f, 18.0f, false);
	AddCylinder(Name + TEXT("Finial"), GoldTrim, Location + FVector(0.0f, 0.0f, 218.0f), 36.0f, 22.0f, false);
}

void AChaosImpactSnowStage::BuildQuarter(const int32 Quarter)
{
	using SnowStage::PondFloorZ;
	using SnowStage::VerandaZ;
	using SnowStage::BridgeCrownZ;
	using SnowStage::WallHeight;
	constexpr float Half = LayoutHalfExtent;
	const FString Q = FString::Printf(TEXT("_Q%d"), Quarter);
	const float Turned = 90.0f * Quarter;
	const uint8 AccentHere = Accent + Quarter;
	const uint8 DarkHere = AccentDark + Quarter;
	const uint8 GlowHere = AccentGlow + Quarter;
	const uint8 NextAccent = Accent + (Quarter + 1) % 4;
	const uint8 NextGlow = AccentGlow + (Quarter + 1) % 4;

	const auto At = [Quarter](const float X, const float Y, const float Z) { return SnowStage::Turn(FVector(X, Y, Z), Quarter); };
	const auto Box = [&](const TCHAR* Name, const uint8 Paint, const FVector& Location, const FVector& Size, const float Yaw,
		const bool bCollision, const bool bChamfer = false)
	{
		return AddBox(FString(Name) + Q, Paint, SnowStage::Turn(Location, Quarter), Size, Yaw + Turned, bCollision, bChamfer);
	};
	const auto Ramp = [&](const TCHAR* Name, const uint8 Paint, const FVector& Low, const FVector& High, const float Width,
		const uint8 Edge)
	{
		return AddRamp(FString(Name) + Q, Paint, SnowStage::Turn(Low, Quarter), SnowStage::Turn(High, Quarter), Width, Edge);
	};

	const float Diagonal = BevelLine / UE_SQRT_2;
	const FVector CornerDir = FVector(1.0f, 0.0f, 0.0f).RotateAngleAxis(45.0f, FVector::UpVector);

	// ---- Outer wooden Engawa veranda (Z = 0) & octagonal sanctuary walls ----------------------------------------
	Box(TEXT("VerandaSide"), Veranda, FVector(2550.0f, 0.0f, -40.0f), FVector(1500.0f, 3600.0f, 80.0f), 0.0f, true);
	Box(TEXT("VerandaCorner"), Veranda, FVector(2000.0f, 2000.0f, -40.0f), FVector(1440.0f, 2600.0f, 80.0f), 45.0f, true);
	// Snow-stone kerb along the inner edge of the veranda (leaving openings for the Taiko bridge and corner ramp).
	Box(TEXT("VerandaKerbL"), SnowStone, FVector(1810.0f, -820.0f, 6.0f), FVector(50.0f, 1080.0f, 12.0f), 0.0f, false);
	Box(TEXT("VerandaKerbR"), SnowStone, FVector(1810.0f, 820.0f, 6.0f), FVector(50.0f, 1080.0f, 12.0f), 0.0f, false);

	// Outer castle-garden walls with snow-topped roof tiles and warm glowing paper-screen windows.
	Box(TEXT("OuterWall"), Wall, FVector(Half + 30.0f, 0.0f, WallHeight * 0.5f), FVector(60.0f, 3460.0f, WallHeight), 0.0f, true);
	Box(TEXT("OuterWallSnow"), SnowStone, FVector(Half + 30.0f, 0.0f, WallHeight + 10.0f), FVector(96.0f, 3500.0f, 20.0f), 0.0f, false);
	Box(TEXT("OuterWallBeamLow"), DarkTimber, FVector(Half - 4.0f, 0.0f, 180.0f), FVector(10.0f, 3400.0f, 24.0f), 0.0f, false);
	Box(TEXT("OuterWallGlowBand"), GlowHere, FVector(Half - 3.0f, 0.0f, 340.0f), FVector(6.0f, 3200.0f, 16.0f), 0.0f, false);

	const FVector BevelMid = CornerDir * (Diagonal + 30.0f);
	Box(TEXT("BevelWall"), Wall, FVector(BevelMid.X, BevelMid.Y, WallHeight * 0.5f), FVector(60.0f, 2320.0f, WallHeight), 45.0f, true);
	Box(TEXT("BevelWallSnow"), SnowStone, FVector(BevelMid.X, BevelMid.Y, WallHeight + 10.0f), FVector(96.0f, 2360.0f, 20.0f), 45.0f, false);
	const FVector BevelFace = CornerDir * (Diagonal - 3.0f);
	Box(TEXT("BevelWallGlow"), GlowHere, FVector(BevelFace.X, BevelFace.Y, 340.0f), FVector(6.0f, 2100.0f, 16.0f), 45.0f, false);

	// ---- Center Wind-Dial Island step-ramp ----------------------------------------------------------------------
	Ramp(TEXT("CenterStepRamp"), SnowStone, FVector(560.0f, 0.0f, PondFloorZ), FVector(420.0f, 0.0f, 0.0f), 340.0f, GoldTrim);

	// ---- Arched Vermilion Taiko Bridge (太鼓橋) connecting the pond (-60) and veranda (0) -----------------------
	Ramp(TEXT("TaikoBridgeInner"), AccentHere, FVector(1280.0f, 0.0f, PondFloorZ), FVector(1560.0f, 0.0f, BridgeCrownZ), 460.0f, GoldTrim);
	Ramp(TEXT("TaikoBridgeOuter"), AccentHere, FVector(1840.0f, 0.0f, VerandaZ), FVector(1560.0f, 0.0f, BridgeCrownZ), 460.0f, GoldTrim);
	Box(TEXT("TaikoBridgeCrown"), AccentHere, FVector(1560.0f, 0.0f, BridgeCrownZ - 15.0f), FVector(120.0f, 460.0f, 30.0f), 0.0f, true);
	// Low vermilion railings and gold Giboshi finials on both sides of the bridge crown.
	for (const float Side : {-1.0f, 1.0f})
	{
		const FString SideTag = Side < 0.0f ? TEXT("L") : TEXT("R");
		const float RailY = Side * 238.0f;
		Box(*(TEXT("BridgeRail") + SideTag), AccentHere, FVector(1560.0f, RailY, BridgeCrownZ + 32.0f), FVector(540.0f, 28.0f, 44.0f), 0.0f, true, true);
		for (const float PostX : {1300.0f, 1560.0f, 1820.0f})
		{
			const float PostZ = (FMath::Abs(PostX - 1560.0f) < 20.0f) ? BridgeCrownZ : 0.0f;
			AddCylinder(FString::Printf(TEXT("BridgePost%s_%d"), *SideTag, FMath::RoundToInt(PostX)) + Q, DarkHere,
				At(PostX, RailY, PostZ), 44.0f, 76.0f, true);
			AddCylinder(FString::Printf(TEXT("BridgeGiboshi%s_%d"), *SideTag, FMath::RoundToInt(PostX)) + Q, GoldTrim,
				At(PostX, RailY, PostZ + 76.0f), 50.0f, 24.0f, false);
		}
	}

	// ---- Diagonal snow-stone ramp at the corner of the pond -----------------------------------------------------
	Ramp(TEXT("CornerPondRamp"), SnowStone, CornerDir * 1660.0f + FVector(0.0f, 0.0f, PondFloorZ),
		CornerDir * 2130.0f + FVector(0.0f, 0.0f, VerandaZ), 420.0f, GlowHere);

	// ---- Hexagonal Ice Crystal Pillar in the pond (bank-shot reflector) -----------------------------------------
	const FVector IcePillarAt = CornerDir * 1020.0f + FVector(0.0f, 0.0f, PondFloorZ);
	Box(TEXT("IcePillarA"), Crystal, IcePillarAt + FVector(0.0f, 0.0f, 145.0f), FVector(210.0f, 210.0f, 290.0f), 15.0f, true, true);
	Box(TEXT("IcePillarB"), Crystal, IcePillarAt + FVector(0.0f, 0.0f, 145.0f), FVector(210.0f, 210.0f, 290.0f), 60.0f, true, true);
	AddRing(TEXT("IcePillarGlowLow") + Q, Glow, IcePillarAt + FVector(0.0f, 0.0f, 80.0f), 118.0f, 10.0f);
	AddRing(TEXT("IcePillarGlowHigh") + Q, GlowHere, IcePillarAt + FVector(0.0f, 0.0f, 220.0f), 118.0f, 10.0f);
	Box(TEXT("IcePillarCap"), SnowStone, IcePillarAt + FVector(0.0f, 0.0f, 298.0f), FVector(190.0f, 190.0f, 16.0f), 45.0f, false, true);

	// ---- L-shaped Folding Screen Walls (屏風壁) on the outer veranda ---------------------------------------------
	Box(TEXT("ByobuScreenA"), DarkTimber, FVector(2180.0f, 1480.0f, 115.0f), FVector(520.0f, 56.0f, 230.0f), 0.0f, true, true);
	Box(TEXT("ByobuScreenB"), DarkTimber, FVector(1480.0f, 2180.0f, 115.0f), FVector(56.0f, 520.0f, 230.0f), 0.0f, true, true);
	Box(TEXT("ByobuPanelA"), AccentHere, FVector(2180.0f, 1480.0f, 115.0f), FVector(460.0f, 62.0f, 160.0f), 0.0f, false);
	Box(TEXT("ByobuPanelB"), NextAccent, FVector(1480.0f, 2180.0f, 115.0f), FVector(62.0f, 460.0f, 160.0f), 0.0f, false);
	Box(TEXT("ByobuGoldTopA"), GoldTrim, FVector(2180.0f, 1480.0f, 233.0f), FVector(526.0f, 64.0f, 8.0f), 0.0f, false);
	Box(TEXT("ByobuGoldTopB"), GoldTrim, FVector(1480.0f, 2180.0f, 233.0f), FVector(64.0f, 526.0f, 8.0f), 0.0f, false);
	Box(TEXT("ByobuGlowA"), GlowHere, FVector(2180.0f, 1480.0f, 115.0f), FVector(440.0f, 66.0f, 18.0f), 0.0f, false);
	Box(TEXT("ByobuGlowB"), NextGlow, FVector(1480.0f, 2180.0f, 115.0f), FVector(66.0f, 440.0f, 18.0f), 0.0f, false);

	// Jumpable snow-mound cover blocks and stone lanterns along the veranda.
	Box(TEXT("SnowMoundL"), SnowStone, FVector(2260.0f, -840.0f, 45.0f), FVector(340.0f, 90.0f, 90.0f), -25.0f, true, true);
	Box(TEXT("SnowMoundR"), SnowStone, FVector(2260.0f, 840.0f, 45.0f), FVector(340.0f, 90.0f, 90.0f), 25.0f, true, true);
	BuildStoneLantern(TEXT("VerandaLanternA") + Q, At(2650.0f, -1350.0f, 0.0f), GlowHere);
	BuildStoneLantern(TEXT("VerandaLanternB") + Q, At(2650.0f, 1350.0f, 0.0f), GlowHere);
}

void AChaosImpactSnowStage::BuildWinterScenery(const int32 Quarter)
{
	// Beyond 5900 units so the opening camera sweep never clips into trees or castle towers.
	const FString Q = FString::Printf(TEXT("_Q%d"), Quarter);
	const float Turned = 90.0f * Quarter;
	const uint8 AccentHere = Accent + Quarter;
	const uint8 GlowHere = AccentGlow + Quarter;

	// Snow-covered terrace outside the wall.
	AddPart(SceneRoot, TEXT("OuterSnowField") + Q, SnowStone, BoxMesh.Get(), SnowStage::Turn(FVector(7200.0f, 0.0f, -60.0f), Quarter),
		FVector(3200.0f, 12000.0f, 100.0f), FRotator(0.0f, Turned, 0.0f), false);

	// Three-tiered Japanese Castle Yagura Pagoda on the outer corner (7400, 7400).
	const FVector PagodaAt = SnowStage::Turn(FVector(7400.0f, 7400.0f, 0.0f), Quarter);
	AddPart(SceneRoot, TEXT("PagodaStoneBase") + Q, DarkTimber, BoxMesh.Get(), PagodaAt + FVector(0.0f, 0.0f, 180.0f),
		FVector(1500.0f, 1500.0f, 360.0f), FRotator(0.0f, Turned + 45.0f, 0.0f), false);
	for (int32 Tier = 0; Tier < 3; ++Tier)
	{
		const float Width = 1220.0f - Tier * 240.0f;
		const float Z = 360.0f + Tier * 380.0f;
		AddPart(SceneRoot, FString::Printf(TEXT("PagodaWall%d"), Tier) + Q, SnowStone, BoxMesh.Get(),
			PagodaAt + FVector(0.0f, 0.0f, Z + 150.0f), FVector(Width, Width, 300.0f), FRotator(0.0f, Turned + 45.0f, 0.0f), false);
		AddPart(SceneRoot, FString::Printf(TEXT("PagodaRoof%d"), Tier) + Q, DarkTimber, BoxMesh.Get(),
			PagodaAt + FVector(0.0f, 0.0f, Z + 320.0f), FVector(Width + 320.0f, Width + 320.0f, 50.0f), FRotator(0.0f, Turned + 45.0f, 0.0f), false);
		AddPart(SceneRoot, FString::Printf(TEXT("PagodaSnow%d"), Tier) + Q, SnowStone, BoxMesh.Get(),
			PagodaAt + FVector(0.0f, 0.0f, Z + 352.0f), FVector(Width + 280.0f, Width + 280.0f, 20.0f), FRotator(0.0f, Turned + 45.0f, 0.0f), false);
		AddPart(SceneRoot, FString::Printf(TEXT("PagodaWindow%d"), Tier) + Q, GlowHere, BoxMesh.Get(),
			PagodaAt + FVector(0.0f, 0.0f, Z + 160.0f), FVector(Width + 12.0f, Width * 0.55f, 44.0f), FRotator(0.0f, Turned + 45.0f, 0.0f), false);
	}
	AddPart(SceneRoot, TEXT("PagodaSpire") + Q, GoldTrim, CylinderMesh.Get(), PagodaAt + FVector(0.0f, 0.0f, 1500.0f),
		FVector(60.0f, 60.0f, 360.0f), FRotator::ZeroRotator, false);

	// Vermilion Torii gate and steaming outdoor hot-spring bath (露天風呂) behind the side wall.
	const FVector ToriiAt = SnowStage::Turn(FVector(6300.0f, 0.0f, 0.0f), Quarter);
	for (const float Side : {-420.0f, 420.0f})
	{
		AddPart(SceneRoot, FString::Printf(TEXT("ToriiPillar_%d"), Side < 0 ? 0 : 1) + Q, AccentHere, CylinderMesh.Get(),
			ToriiAt + SnowStage::Turn(FVector(0.0f, Side, 0.0f), Quarter), FVector(84.0f, 84.0f, 680.0f), FRotator::ZeroRotator, false);
	}
	AddPart(SceneRoot, TEXT("ToriiKasagi") + Q, AccentHere, BoxMesh.Get(), ToriiAt + FVector(0.0f, 0.0f, 700.0f),
		FVector(120.0f, 1180.0f, 60.0f), FRotator(0.0f, Turned, 0.0f), false);
	AddPart(SceneRoot, TEXT("ToriiSnow") + Q, SnowStone, BoxMesh.Get(), ToriiAt + FVector(0.0f, 0.0f, 738.0f),
		FVector(110.0f, 1160.0f, 18.0f), FRotator(0.0f, Turned, 0.0f), false);
	AddPart(SceneRoot, TEXT("ToriiNuki") + Q, AccentHere, BoxMesh.Get(), ToriiAt + FVector(0.0f, 0.0f, 540.0f),
		FVector(70.0f, 980.0f, 44.0f), FRotator(0.0f, Turned, 0.0f), false);

	// Outdoor hot-spring pool with rising steam ring.
	const FVector OnsenAt = SnowStage::Turn(FVector(6500.0f, -2200.0f, 0.0f), Quarter);
	AddCylinder(TEXT("OnsenRockRim") + Q, DarkTimber, FVector(6500.0f, -2200.0f, -10.0f), 960.0f, 55.0f, false)
		->SetRelativeLocation(OnsenAt + FVector(0.0f, 0.0f, -10.0f));
	AddCylinder(TEXT("OnsenWater") + Q, Glow, FVector(6500.0f, -2200.0f, 20.0f), 820.0f, 30.0f, false)
		->SetRelativeLocation(OnsenAt + FVector(0.0f, 0.0f, 20.0f));
	UStaticMeshComponent* Steam = AddRing(TEXT("OnsenSteam") + Q, Glow, FVector(6500.0f, -2200.0f, 120.0f), 320.0f, 10.0f);
	Steam->SetRelativeLocation(OnsenAt + FVector(0.0f, 0.0f, 120.0f));
	SteamRings.Add(Steam);

	// Snow-covered pine grove and distant white mountain peaks.
	const FVector PineLocations[] = {
		FVector(6200.0f, 1600.0f, 0.0f), FVector(6800.0f, 2500.0f, 0.0f),
		FVector(6400.0f, -3400.0f, 0.0f), FVector(7600.0f, -1400.0f, 0.0f)
	};
	for (int32 Tree = 0; Tree < UE_ARRAY_COUNT(PineLocations); ++Tree)
	{
		const FVector Foot = SnowStage::Turn(PineLocations[Tree], Quarter);
		AddPart(SceneRoot, FString::Printf(TEXT("PineTrunk%d"), Tree) + Q, DarkTimber, CylinderMesh.Get(),
			Foot, FVector(65.0f, 65.0f, 240.0f), FRotator::ZeroRotator, false);
		for (int32 Tier = 0; Tier < 3; ++Tier)
		{
			const float ConeW = 460.0f - Tier * 105.0f;
			UStaticMeshComponent* Foliage = AddPart(SceneRoot, FString::Printf(TEXT("PineCone%d_%d"), Tree, Tier) + Q,
				PineGreen, ConeMesh.Get(), Foot + FVector(0.0f, 0.0f, 320.0f + Tier * 190.0f),
				FVector(ConeW, ConeW, 310.0f - Tier * 40.0f), FRotator::ZeroRotator, false);
			if (PineMaterial)
			{
				Foliage->SetMaterial(0, PineMaterial);
			}
			AddPart(SceneRoot, FString::Printf(TEXT("PineSnow%d_%d"), Tree, Tier) + Q,
				SnowStone, ConeMesh.Get(), Foot + FVector(0.0f, 0.0f, 355.0f + Tier * 190.0f),
				FVector(ConeW * 0.88f, ConeW * 0.88f, 220.0f - Tier * 30.0f), FRotator::ZeroRotator, false);
		}
	}

	// Distant snow mountain peak on the horizon.
	AddPart(SceneRoot, TEXT("DistantPeak") + Q, Scenery, ConeMesh.Get(), SnowStage::Turn(FVector(10800.0f, -2600.0f, 1100.0f), Quarter),
		FVector(4600.0f, 4600.0f, 2600.0f), FRotator(0.0f, Turned + 20.0f, 0.0f), false);
}

UStaticMeshComponent* AChaosImpactSnowStage::AddPart(USceneComponent* Parent, const FString& Name, const uint8 Paint,
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

	const bool bGlow = Paint == Glow || (Paint >= AccentGlow && Paint < AccentGlow + 4);
	UMaterialInterface* Surface = FlatMaterial.Get();
	if (bGlow)
	{
		Surface = GlowMaterial.Get();
	}
	else if (Paint == Veranda && PlanksMaterial)
	{
		Surface = PlanksMaterial.Get();
	}
	else if ((Paint == SnowStone || Paint == Scenery) && PaleStoneMaterial)
	{
		Surface = PaleStoneMaterial.Get();
	}
	else if ((Paint == Wall || Paint == DarkTimber) && DarkStoneMaterial)
	{
		Surface = DarkStoneMaterial.Get();
	}
	else if (Paint == GoldTrim && GoldMaterial)
	{
		Surface = GoldMaterial.Get();
	}
	else if (Paint == PineGreen && PineMaterial)
	{
		Surface = PineMaterial.Get();
	}
	else if (GlossPaintMaterial)
	{
		Surface = GlossPaintMaterial.Get();
	}
	Block->SetMaterial(0, Surface);
	Block->ComponentTags.Add(FName(*FString::Printf(TEXT("CIPaint%d"), Paint)));
	return Block;
}

UStaticMeshComponent* AChaosImpactSnowStage::AddBox(const FString& Name, const uint8 Paint, const FVector& Location,
	const FVector& Size, const float Yaw, const bool bCollision, const bool bChamfer)
{
	return AddPart(SceneRoot, Name, Paint, bChamfer ? ChamferMesh.Get() : BoxMesh.Get(), SnowStage::Spread(Location), SnowStage::Spread(Size),
		FRotator(0.0f, Yaw, 0.0f), bCollision);
}

UStaticMeshComponent* AChaosImpactSnowStage::AddCylinder(const FString& Name, const uint8 Paint, const FVector& Location,
	const float Diameter, const float Height, const bool bCollision)
{
	return AddPart(SceneRoot, Name, Paint, CylinderMesh.Get(), SnowStage::Spread(Location),
		FVector(Diameter * LayoutScale, Diameter * LayoutScale, Height), FRotator::ZeroRotator, bCollision);
}

UStaticMeshComponent* AChaosImpactSnowStage::AddRing(const FString& Name, const uint8 Paint, const FVector& Location,
	const float Radius, const float Thickness)
{
	return AddPart(SceneRoot, Name, Paint, RingMesh.Get(), SnowStage::Spread(Location),
		FVector(Radius * LayoutScale * 100.0f / 70.0f, Radius * LayoutScale * 100.0f / 70.0f, Thickness * 100.0f / 13.0f),
		FRotator::ZeroRotator, false);
}

UStaticMeshComponent* AChaosImpactSnowStage::AddRamp(const FString& Name, const uint8 Paint, const FVector& LayoutLow,
	const FVector& LayoutHigh, const float LayoutWidth, const uint8 EdgePaint)
{
	const FVector Low = SnowStage::Spread(LayoutLow);
	const FVector High = SnowStage::Spread(LayoutHigh);
	const float Width = LayoutWidth * LayoutScale;
	const FVector Along = High - Low;
	const float Run = Along.Size2D();
	const float Length = Along.Size();
	const FRotator Slope(FMath::RadiansToDegrees(FMath::Atan2(Along.Z, Run)), Along.Rotation().Yaw, 0.0f);
	const float Thickness = FMath::Max(70.0f, FMath::Abs(Along.Z) / FMath::Cos(FMath::DegreesToRadians(Slope.Pitch)) + 55.0f);
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

void AChaosImpactSnowStage::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyPalette();
}

void AChaosImpactSnowStage::BeginPlay()
{
	Super::BeginPlay();
	ApplyPalette();

	if (bSpawnWinterDecor && GetWorld() && GetNetMode() != NM_DedicatedServer)
	{
		bool bHasFlakes = false;
		for (TActorIterator<AChaosImpactDriftingFlakes> It(GetWorld()); It; ++It)
		{
			SpawnedFlakes = *It;
			bHasFlakes = true;
			break;
		}
		if (!bHasFlakes)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Params.ObjectFlags |= RF_Transient;
			SpawnedFlakes = GetWorld()->SpawnActor<AChaosImpactDriftingFlakes>(
				AChaosImpactDriftingFlakes::StaticClass(), GetActorLocation() + FVector(0.0f, 0.0f, 650.0f), FRotator::ZeroRotator, Params);
			if (SpawnedFlakes)
			{
				SpawnedFlakes->Area = FVector(3800.0f, 3800.0f, 950.0f);
				SpawnedFlakes->Count = 1000;
			}
		}

		// Spawn Shima-enaga birds on the 4 veranda lanterns if none were placed in the level.
		bool bHasBirds = false;
		for (TActorIterator<AChaosImpactSimaePerch> It(GetWorld()); It; ++It)
		{
			bHasBirds = true;
			break;
		}
		if (!bHasBirds)
		{
			for (int32 Quarter = 0; Quarter < 4; ++Quarter)
			{
				FActorSpawnParameters Params;
				Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				Params.ObjectFlags |= RF_Transient;
				const FVector PerchAt = GetActorTransform().TransformPosition(SnowStage::Spread(SnowStage::Turn(FVector(2650.0f, 1350.0f, 240.0f), Quarter)));
				if (AChaosImpactSimaePerch* Bird = GetWorld()->SpawnActor<AChaosImpactSimaePerch>(
					AChaosImpactSimaePerch::StaticClass(), PerchAt, FRotator(0.0f, 90.0f * Quarter + 180.0f, 0.0f), Params))
				{
					SpawnedBirds.Add(Bird);
				}
			}
		}
	}
	UpdateWindLook(0.0f);
}

void AChaosImpactSnowStage::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (IsValid(SpawnedFlakes) && SpawnedFlakes->HasAnyFlags(RF_Transient))
	{
		SpawnedFlakes->Destroy();
	}
	for (AChaosImpactSimaePerch* Bird : SpawnedBirds)
	{
		if (IsValid(Bird))
		{
			Bird->Destroy();
		}
	}
	SpawnedBirds.Reset();
	Super::EndPlay(EndPlayReason);
}

void AChaosImpactSnowStage::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AChaosImpactSnowStage, WindDirectionIndex);
	DOREPLIFETIME(AChaosImpactSnowStage, WindStartedAt);
}

FLinearColor AChaosImpactSnowStage::GetPaintColor(const uint8 Paint) const
{
	const auto QuarterAt = [this](const int32 Quarter)
	{
		return QuarterColors.IsValidIndex(Quarter) ? QuarterColors[Quarter] : FLinearColor(0.92f, 0.16f, 0.14f);
	};
	if (Paint >= AccentGlow) { return QuarterAt(Paint - AccentGlow); }
	if (Paint >= AccentDark) { return QuarterAt(Paint - AccentDark) * 0.35f; }
	if (Paint >= Accent) { return QuarterAt(Paint - Accent); }
	switch (Paint)
	{
	case Veranda: return VerandaColor;
	case Ice: return IceColor;
	case Crystal: return CrystalColor;
	case SnowStone: return SnowStoneColor;
	case Wall: return WallColor;
	case GoldTrim: return GoldTrimColor;
	case Glow: return GlowColor;
	case DarkTimber: return FLinearColor(0.07f, 0.06f, 0.08f);
	case PineGreen: return FLinearColor(0.08f, 0.28f, 0.18f);
	default: return SceneryColor;
	}
}

bool AChaosImpactSnowStage::ReadPaint(const UStaticMeshComponent* Block, uint8& OutPaint)
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

void AChaosImpactSnowStage::ApplyPalette()
{
	if (!bApplyDefaultColors)
	{
		return;
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
		if (Paint == Glow || (Paint >= AccentGlow && Paint < AccentGlow + 4))
		{
			Material->SetVectorParameterValue(TEXT("Color"), Color);
			Material->SetScalarParameterValue(TEXT("Intensity"), GlowIntensity);
		}
		else
		{
			Material->SetVectorParameterValue(TEXT("Color"), Color);
			Material->SetVectorParameterValue(TEXT("Base Color"), Color);
			Material->SetVectorParameterValue(TEXT("BaseColor"), Color);
			const float Roughness = (Paint == Ice || Paint == Crystal) ? 0.08f : Paint == GoldTrim ? 0.25f : 0.6f;
			Material->SetScalarParameterValue(TEXT("Roughness"), Roughness);
		}
	}
}

TArray<FVector> AChaosImpactSnowStage::GetSpawnPoints() const
{
	return ToWorld(SpawnPointOffsets);
}

TArray<FVector> AChaosImpactSnowStage::GetBallPoints() const
{
	return ToWorld(BallPointOffsets);
}

bool AChaosImpactSnowStage::IsOutside(const FVector& Location) const
{
	if (Super::IsOutside(Location))
	{
		return true;
	}
	const FVector Local = GetActorTransform().InverseTransformPositionNoScale(Location);
	return FMath::Abs(Local.X) + FMath::Abs(Local.Y) > BevelLine * LayoutScale + (60.0f + OutsideMargin) * UE_SQRT_2;
}

double AChaosImpactSnowStage::GetServerNow() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0;
	}
	const AGameStateBase* State = World->GetGameState();
	return State ? State->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
}

bool AChaosImpactSnowStage::IsMatchOn() const
{
	const AChaosImpactGameState* Match = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	return Match && Match->bVersusMatch && Match->Phase == EChaosImpactOnlinePhase::Match;
}

float AChaosImpactSnowStage::GetWindProgress() const
{
	if (WindDirectionIndex < 0)
	{
		return 0.0f;
	}
	const float Age = static_cast<float>(GetServerNow() - WindStartedAt);
	return Age < WindWarnSeconds
		? FMath::Clamp(Age / FMath::Max(WindWarnSeconds, 0.1f), 0.0f, 1.0f)
		: 1.0f + FMath::Clamp((Age - WindWarnSeconds) / FMath::Max(WindActiveSeconds, 0.1f), 0.0f, 1.0f);
}

void AChaosImpactSnowStage::StartWindGust(const int32 WindDir)
{
	if (!HasAuthority())
	{
		return;
	}
	const int32 Clamped = WindDir & 3;
	WindDirectionIndex = Clamped;
	LastWindDir = Clamped;
	WindStartedAt = static_cast<float>(GetServerNow());
	bWindBlowing = false;
	ForceNetUpdate();
	OnRep_WindState();
}

void AChaosImpactSnowStage::OnRep_WindState()
{
	if (WindDirectionIndex >= 0 && GetNetMode() != NM_DedicatedServer)
	{
		ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::NovaCharge, GetActorLocation() + FVector(0.0f, 0.0f, 180.0f), 0.5f, 1.55f);
	}
}

void AChaosImpactSnowStage::MulticastWindGust_Implementation(const int32 WindDir)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::StageWarp, GetActorLocation() + FVector(0.0f, 0.0f, 180.0f), 0.8f, 0.9f);
}

void AChaosImpactSnowStage::TickWindServer(const double Now, const float DeltaSeconds)
{
	if (!IsMatchOn())
	{
		NextWindAt = -1.0;
		if (WindDirectionIndex >= 0)
		{
			WindDirectionIndex = -1;
			ForceNetUpdate();
		}
		return;
	}
	if (NextWindAt < 0.0)
	{
		NextWindAt = Now + FirstWindAfter;
	}
	if (WindDirectionIndex < 0)
	{
		if (Now >= NextWindAt)
		{
			int32 NextDir = FMath::RandRange(0, 3);
			if (NextDir == LastWindDir)
			{
				NextDir = (NextDir + FMath::RandRange(1, 3)) % 4;
			}
			StartWindGust(NextDir);
		}
		return;
	}

	const float Age = static_cast<float>(Now - WindStartedAt);
	if (Age >= WindWarnSeconds && !bWindBlowing)
	{
		bWindBlowing = true;
		MulticastWindGust(WindDirectionIndex);
	}
	if (bWindBlowing && Age < WindWarnSeconds + WindActiveSeconds)
	{
		const FVector WorldWindDir = GetActorTransform().TransformVectorNoScale(GetWindDirectionVector(WindDirectionIndex)).GetSafeNormal2D();
		for (TActorIterator<AChaosImpactBall> It(GetWorld()); It; ++It)
		{
			AChaosImpactBall* Ball = *It;
			const EChaosImpactBallType Type = Ball->GetBallType();
			if (!Ball->IsFlyingOnServer() || Type == EChaosImpactBallType::Beam || Type == EChaosImpactBallType::Nova)
			{
				continue;
			}
			const FVector Vel = Ball->GetBallVelocity();
			Ball->DeflectByWind(Vel + WorldWindDir * (WindBallAcceleration * DeltaSeconds));
		}
	}
	if (Age >= WindWarnSeconds + WindActiveSeconds)
	{
		WindDirectionIndex = -1;
		ForceNetUpdate();
		NextWindAt = Now + FMath::FRandRange(WindIntervalMin, FMath::Max(WindIntervalMin, WindIntervalMax));
	}
}

void AChaosImpactSnowStage::UpdateWindLook(const float DeltaSeconds)
{
	const float Progress = GetWindProgress();
	const bool bWarning = WindDirectionIndex >= 0 && Progress < 1.0f;
	const bool bActive = WindDirectionIndex >= 0 && Progress >= 1.0f;
	const float TargetYaw = WindDirectionIndex >= 0 ? 90.0f * WindDirectionIndex : CurrentVaneYaw + 18.0f * DeltaSeconds;
	CurrentVaneYaw = FMath::FInterpTo(CurrentVaneYaw, CurrentVaneYaw + FMath::FindDeltaAngleDegrees(CurrentVaneYaw, TargetYaw), DeltaSeconds, WindDirectionIndex >= 0 ? 6.0f : 1.0f);

	if (WindVaneRoot)
	{
		WindVaneRoot->SetRelativeRotation(FRotator(0.0f, CurrentVaneYaw, 0.0f));
	}
	if (WindStreamerGroup)
	{
		WindStreamerGroup->SetRelativeRotation(FRotator(0.0f, CurrentVaneYaw, 0.0f));
	}
	for (int32 Index = 0; Index < WindStreamers.Num(); ++Index)
	{
		if (UStaticMeshComponent* Streamer = WindStreamers[Index])
		{
			Streamer->SetVisibility(bWarning || bActive);
			if (bWarning || bActive)
			{
				const float Speed = bActive ? 2200.0f : 750.0f;
				const float Slide = FMath::Fmod(LookClock * Speed + Index * 430.0f, 3400.0f) - 1700.0f;
				const float Y = -1400.0f + Index * 400.0f;
				Streamer->SetRelativeLocation(FVector(Slide, Y, (Index % 3) * 45.0f));
				Streamer->SetRelativeScale3D(FVector(bActive ? 9.5f : 4.2f, 0.18f, 0.03f));
			}
		}
	}
	for (int32 Dir = 0; Dir < CompassArrows.Num(); ++Dir)
	{
		if (UStaticMeshComponent* Arrow = CompassArrows[Dir])
		{
			const bool bChosen = Dir == WindDirectionIndex;
			const float Scale = bChosen ? (1.25f + 0.2f * FMath::Sin(LookClock * 12.0f)) : 0.85f;
			Arrow->SetRelativeScale3D(FVector(1.6f * Scale, 0.34f * Scale, 0.025f));
		}
	}
	if (IsValid(SpawnedFlakes))
	{
		if (bActive)
		{
			const FVector Dir = GetActorTransform().TransformVectorNoScale(GetWindDirectionVector(WindDirectionIndex));
			SpawnedFlakes->Wind = Dir * 420.0f;
		}
		else if (bWarning)
		{
			const FVector Dir = GetActorTransform().TransformVectorNoScale(GetWindDirectionVector(WindDirectionIndex));
			SpawnedFlakes->Wind = Dir * 140.0f;
		}
		else
		{
			SpawnedFlakes->Wind = FVector(20.0f, 30.0f, 0.0f);
		}
	}
}

void AChaosImpactSnowStage::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!GetWorld())
	{
		return;
	}
	if (HasAuthority())
	{
		TickWindServer(GetServerNow(), DeltaSeconds);
	}
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	LookClock += DeltaSeconds;
	UpdateWindLook(DeltaSeconds);

	for (int32 Index = 0; Index < SteamRings.Num(); ++Index)
	{
		if (UStaticMeshComponent* Steam = SteamRings[Index])
		{
			const float Phase = FMath::Frac(LookClock * 0.32f + Index * 0.25f);
			const float Radius = FMath::Lerp(220.0f, 440.0f, Phase);
			Steam->SetRelativeScale3D(FVector(Radius / 70.0f, Radius / 70.0f, (12.0f * (1.0f - Phase)) / 13.0f));
		}
	}
}
