#include "ChaosImpactForgeStage.h"

#include "ChaosImpact.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactBallSpawner.h"
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
#include "Net/UnrealNetwork.h"
#include "NiagaraFunctionLibrary.h"
#include "UObject/ConstructorHelpers.h"

namespace ForgeStage
{
	constexpr float CoreRadius = 1520.0f;
	constexpr float CrucibleTop = 95.0f;

	const FVector QuarterSpawnOffsets[] =
	{
		FVector(2960.0f, 0.0f, 0.0f),                            // behind the container on the cardinal foundry bay
		FVector(2450.0f, 2450.0f, 0.0f),                         // inside the corner cooling chamber
		FVector(1220.0f, -520.0f, AChaosImpactForgeStage::CoreDeckZ) // on the central core deck beside the vents
	};

	const FVector QuarterBallOffsets[] =
	{
		FVector(2120.0f, 0.0f, 0.0f),                            // front of the cardinal foundry bay
		FVector(2220.0f, 2220.0f, 0.0f)                          // entrance of the corner cooling chamber
	};

	FVector Turn(const FVector& Offset, const int32 Quarter)
	{
		return FRotator(0.0f, 90.0f * Quarter, 0.0f).RotateVector(Offset);
	}

	FVector Spread(const FVector& Layout)
	{
		return FVector(Layout.X * AChaosImpactForgeStage::LayoutScale, Layout.Y * AChaosImpactForgeStage::LayoutScale, Layout.Z);
	}
}

FVector AChaosImpactForgeStage::GetVentOffset(const int32 Vent)
{
	return ForgeStage::Spread(ForgeStage::Turn(FVector(920.0f, 0.0f, CoreDeckZ), FMath::Clamp(Vent, 0, 3)));
}

AChaosImpactForgeStage::AChaosImpactForgeStage()
{
	CrashBallStartOffset = FVector(0, 0, 95);
	CrashRouteMaterial = FSoftObjectPath(TEXT("/Game/ChaosImpact/Versus/M_StageMetalFloor.M_StageMetalFloor"));
	Team0GoalOffset = FVector(-2990, 0, 0);
	Team1GoalOffset = FVector(2990, 0, 0);
	CrashRemovedObstaclePrefixes = {TEXT("BayContainer_Q0"), TEXT("BayContainer_Q2")};
	CrashBallExtraBallOffsets = {FVector(-1100, 400, 60), FVector(1100, -400, 60), FVector(-2400, 600, 0), FVector(2400, -600, 0)};
	for (const float Sign : {-1.f, 1.f})
	{
		FChaosImpactCrashRoutePart Ramp;
		Ramp.Start = FVector(Sign * 1420, 0, 60);
		Ramp.End = FVector(Sign * 2000, 0, 0);
		Ramp.Width = 900;
		CrashRouteParts.Add(Ramp);
	}
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);
	SetNetUpdateFrequency(10.0f);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	BayColors = {
		FLinearColor(1.0f, 0.32f, 0.02f),   // Molten Orange (North / +X)
		FLinearColor(0.02f, 0.72f, 1.0f),   // Coolant Cyan (East / +Y)
		FLinearColor(1.0f, 0.72f, 0.04f),   // Hazard Gold (South / -X)
		FLinearColor(0.95f, 0.08f, 0.25f)   // Forge Crimson (West / -Y)
	};

	// Central Crucible Altar pad + 2 per quarter = 9 ball pads.
	BallPointOffsets.Add(FVector(0.0f, 0.0f, ForgeStage::CrucibleTop));
	for (int32 Quarter = 0; Quarter < 4; ++Quarter)
	{
		for (const FVector& Offset : ForgeStage::QuarterSpawnOffsets)
		{
			SpawnPointOffsets.Add(ForgeStage::Spread(ForgeStage::Turn(Offset, Quarter)));
		}
		for (const FVector& Offset : ForgeStage::QuarterBallOffsets)
		{
			BallPointOffsets.Add(ForgeStage::Spread(ForgeStage::Turn(Offset, Quarter)));
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
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MetalFloor(
		TEXT("/Game/ChaosImpact/Versus/M_StageMetalFloor.M_StageMetalFloor"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ConcreteMat(
		TEXT("/Game/ChaosImpact/Versus/Splash/M_CI_Concrete.M_CI_Concrete"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Panel(
		TEXT("/Game/ChaosImpact/Versus/Splash/M_CI_PanelWall.M_CI_PanelWall"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Corrugated(
		TEXT("/Game/ChaosImpact/Versus/Splash/M_CI_Corrugated.M_CI_Corrugated"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Paint(
		TEXT("/Game/ChaosImpact/Versus/Splash/M_CI_GlossPaint.M_CI_GlossPaint"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GoldMat(
		TEXT("/Game/ChaosImpact/solo/Stage2/Materials/MI_Gold.MI_Gold"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FieldAmber(
		TEXT("/Game/ChaosImpact/solo/Stage1/Materials/MI_NeonField_Amber.MI_NeonField_Amber"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Stencil(
		TEXT("/Engine/EngineMaterials/DefaultTextMaterialOpaque.DefaultTextMaterialOpaque"));

	BoxMesh = Box.Object;
	ConeMesh = Cone.Object;
	ChamferMesh = Chamfer.Succeeded() ? Chamfer.Object : Box.Object;
	CylinderMesh = Cylinder.Object;
	RingMesh = Ring.Object;
	FlatMaterial = Flat.Object;
	GlowMaterial = Glowing.Succeeded() ? Glowing.Object : Flat.Object;
	MetalFloorMaterial = MetalFloor.Succeeded() ? MetalFloor.Object : nullptr;
	ConcreteMaterial = ConcreteMat.Succeeded() ? ConcreteMat.Object : nullptr;
	PanelMaterial = Panel.Succeeded() ? Panel.Object : nullptr;
	CorrugatedMaterial = Corrugated.Succeeded() ? Corrugated.Object : nullptr;
	PaintMaterial = Paint.Succeeded() ? Paint.Object : nullptr;
	GoldMaterial = GoldMat.Succeeded() ? GoldMat.Object : nullptr;
	FieldAmberMaterial = FieldAmber.Succeeded() ? FieldAmber.Object : GlowMaterial;
	StencilMaterial = Stencil.Object;

	BuildCoreDeck();

	for (int32 Quarter = 0; Quarter < 4; ++Quarter)
	{
		BuildQuarter(Quarter);
		BuildThermalVent(Quarter);
		BuildFoundryScenery(Quarter);
	}

	// Industrial foundry grading: warm highlights, deep shadows, molten bloom.
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
	Look.ColorSaturation = FVector4(1.14f, 1.12f, 1.08f, 1.0f);
	Look.bOverride_ColorContrast = true;
	Look.ColorContrast = FVector4(1.09f, 1.08f, 1.07f, 1.0f);
	Look.bOverride_BloomIntensity = true;
	Look.BloomIntensity = 0.8f;
	Look.bOverride_VignetteIntensity = true;
	Look.VignetteIntensity = 0.35f;
	Look.bOverride_WhiteTemp = true;
	Look.WhiteTemp = 6500.0f;
}

void AChaosImpactForgeStage::BuildCoreDeck()
{
	using ForgeStage::CoreRadius;
	using ForgeStage::CrucibleTop;
	// Glowing magma sea far below the foundry platforms (-450 cm) over a dark basalt bed.
	AddBox(TEXT("MagmaSeaSurface"), MagmaGlow, FVector(0.0f, 0.0f, -450.0f), FVector(22000.0f, 22000.0f, 20.0f), 0.0f, false);
	AddBox(TEXT("MagmaBed"), Scenery, FVector(0.0f, 0.0f, -520.0f), FVector(22000.0f, 22000.0f, 60.0f), 0.0f, false);

	// Central Circular Core Deck (+60 cm) supported by a heavy tapered furnace pedestal reaching into the magma.
	AddCylinder(TEXT("CoreDeckTop"), SteelFloor, FVector(0.0f, 0.0f, 0.0f), CoreRadius * 2.0f, CoreDeckZ, true);
	AddCylinder(TEXT("CoreDeckRim"), Concrete, FVector(0.0f, 0.0f, -65.0f), CoreRadius * 2.0f + 80.0f, 65.0f, false);
	AddPart(SceneRoot, TEXT("CoreFurnaceSub"), DarkSteel, ConeMesh.Get(), FVector(0.0f, 0.0f, -520.0f),
		FVector(CoreRadius * 1.9f, CoreRadius * 1.9f, 920.0f), FRotator(180.0f, 0.0f, 0.0f), false);
	AddRing(TEXT("CoreSubMagmaRing"), MagmaGlow, FVector(0.0f, 0.0f, -180.0f), CoreRadius * 0.86f, 18.0f);

	// Central Crucible Altar (+95 cm) where the central ball spawner sits.
	AddCylinder(TEXT("CrucibleStep1"), Concrete, FVector(0.0f, 0.0f, CoreDeckZ), 760.0f, 18.0f, true);
	AddCylinder(TEXT("CrucibleStep2"), Baffle, FVector(0.0f, 0.0f, CoreDeckZ + 18.0f), 480.0f, 17.0f, true);
	CrucibleRings.Add(AddRing(TEXT("CrucibleRingOuter"), MagmaGlow, FVector(0.0f, 0.0f, CoreDeckZ + 18.8f), 380.0f, 5.0f));
	CrucibleRings.Add(AddRing(TEXT("CrucibleRingInner"), Gold, FVector(0.0f, 0.0f, CrucibleTop + 0.8f), 235.0f, 5.0f));
	AddRing(TEXT("CorePerimeterStripe"), Stripe, FVector(0.0f, 0.0f, CoreDeckZ + 0.8f), CoreRadius - 45.0f, 4.0f);
}

void AChaosImpactForgeStage::BuildQuarter(const int32 Quarter)
{
	using ForgeStage::CoreRadius;
	const FString Q = FString::Printf(TEXT("_Q%d"), Quarter);
	const float Turned = 90.0f * Quarter;
	const uint8 BayHere = Bay + Quarter;
	const uint8 DarkHere = BayDark + Quarter;
	const uint8 GlowHere = BayGlow + Quarter;
	const uint8 NextBay = Bay + (Quarter + 1) % 4;
	const uint8 NextGlow = BayGlow + (Quarter + 1) % 4;

	const auto At = [Quarter](const float X, const float Y, const float Z) { return ForgeStage::Turn(FVector(X, Y, Z), Quarter); };
	const auto Box = [&](const TCHAR* Name, const uint8 Paint, const FVector& Location, const FVector& Size, const float Yaw,
		const bool bCollision, const bool bChamfer = false)
	{
		return AddBox(FString(Name) + Q, Paint, ForgeStage::Turn(Location, Quarter), Size, Yaw + Turned, bCollision, bChamfer);
	};
	const auto Ramp = [&](const TCHAR* Name, const uint8 Paint, const FVector& Low, const FVector& High, const float Width,
		const uint8 Edge)
	{
		return AddRamp(FString(Name) + Q, Paint, ForgeStage::Turn(Low, Quarter), ForgeStage::Turn(High, Quarter), Width, Edge);
	};

	const FVector CornerDir = FVector(1.0f, 0.0f, 0.0f).RotateAngleAxis(45.0f, FVector::UpVector);
	const FVector DiagOrth(-CornerDir.Y, CornerDir.X, 0.0f);

	// ---- Cardinal Foundry Bay Platform (X = 1950..3250, Y = -850..850, Z = 0) ----------------------------------
	Box(TEXT("FoundryBayDeck"), SteelFloor, FVector(2600.0f, 0.0f, -35.0f), FVector(1300.0f, 1700.0f, 70.0f), 0.0f, true);
	Box(TEXT("FoundryBayUnder"), DarkSteel, FVector(2600.0f, 0.0f, -260.0f), FVector(1100.0f, 1460.0f, 380.0f), 0.0f, false);
	Box(TEXT("FoundryBayBackWall"), Baffle, FVector(3280.0f, 0.0f, 220.0f), FVector(60.0f, 1720.0f, 440.0f), 0.0f, true);
	Box(TEXT("FoundryBayBackGlow"), GlowHere, FVector(3280.0f, 0.0f, 445.0f), FVector(70.0f, 1720.0f, 10.0f), 0.0f, false);
	Box(TEXT("FoundryBayBackBand"), GlowHere, FVector(3246.0f, 0.0f, 240.0f), FVector(6.0f, 1620.0f, 18.0f), 0.0f, false);
	Box(TEXT("FoundryBayStripeN"), Stripe, FVector(2600.0f, 820.0f, 0.8f), FVector(1240.0f, 22.0f, 1.4f), 0.0f, false);
	Box(TEXT("FoundryBayStripeS"), Stripe, FVector(2600.0f, -820.0f, 0.8f), FVector(1240.0f, 22.0f, 1.4f), 0.0f, false);

	// Stencilled bay number on the foundry deck.
	UTextRenderComponent* BayNumber = CreateDefaultSubobject<UTextRenderComponent>(*(TEXT("BayNumber") + Q));
	BayNumber->SetupAttachment(SceneRoot);
	BayNumber->SetText(FText::FromString(FString::Printf(TEXT("B-%d"), Quarter + 1)));
	BayNumber->SetWorldSize(300.0f);
	if (StencilMaterial)
	{
		BayNumber->SetTextMaterial(StencilMaterial);
	}
	BayNumber->SetHorizontalAlignment(EHTA_Center);
	BayNumber->SetVerticalAlignment(EVRTA_TextCenter);
	BayNumber->SetRelativeLocation(ForgeStage::Spread(At(3020.0f, -520.0f, 1.0f)));
	BayNumber->SetRelativeRotation(FRotator(90.0f, 180.0f, 0.0f));
	BayNumber->SetTextRenderColor((StripeColor * 0.7f).ToFColor(true));
	BayNumber->ComponentTags.Add(TEXT("CIBayText"));

	// Wide sloped steel ramp connecting the Cardinal Foundry Bay (1950, Z=0) to the Core Deck (1450, Z=60).
	Ramp(TEXT("CoreMainRamp"), Concrete, FVector(1980.0f, 0.0f, 0.0f), FVector(1420.0f, 0.0f, CoreDeckZ), 620.0f, Stripe);
	Box(TEXT("CoreRampSupport"), DarkSteel, FVector(1700.0f, 0.0f, -150.0f), FVector(600.0f, 520.0f, 280.0f), 0.0f, false);
	for (const float Side : {-1.0f, 1.0f})
	{
		AddFence(FString::Printf(TEXT("CoreRampFence_%d"), Side < 0 ? 0 : 1) + Q,
			At(1980.0f, Side * 320.0f, 0.0f), At(1420.0f, Side * 320.0f, CoreDeckZ));
	}

	// Corrugated industrial container in the middle of the Cardinal Foundry Bay for solid throw cover.
	AddContainer(TEXT("BayContainer") + Q, BayHere, DarkHere, At(2560.0f, 0.0f, 150.0f), FVector(230.0f, 540.0f, 300.0f), Turned);

	// ---- Corner Cooling Chamber (2450, 2450, Z = 0) -------------------------------------------------------------
	Box(TEXT("CornerChamberDeck"), Concrete, FVector(2450.0f, 2450.0f, -35.0f), FVector(1260.0f, 1260.0f, 70.0f), 45.0f, true, true);
	Box(TEXT("CornerChamberUnder"), DarkSteel, FVector(2450.0f, 2450.0f, -260.0f), FVector(1020.0f, 1020.0f, 380.0f), 45.0f, false);
	AddRing(TEXT("CornerCoolantRing") + Q, CoolantGlow, At(2450.0f, 2450.0f, 1.0f), 340.0f, 4.5f);
	// Outer blast shield wall at the back of the corner chamber so throws bank cleanly and players are sheltered.
	const FVector CornerBack = CornerDir * (2450.0f * UE_SQRT_2 + 600.0f);
	Box(TEXT("CornerBlastWall"), Baffle, FVector(CornerBack.X, CornerBack.Y, 210.0f), FVector(64.0f, 1240.0f, 420.0f), 45.0f, true);
	Box(TEXT("CornerBlastGlow"), CoolantGlow, FVector(CornerBack.X, CornerBack.Y, 424.0f), FVector(72.0f, 1240.0f, 10.0f), 45.0f, false);

	// Diagonal walkway connecting the Corner Cooling Chamber (inner edge at CornerDir * 2835) directly to the Central Core Deck (1440).
	Ramp(TEXT("DiagCoreBridge"), SteelFloor, CornerDir * 2860.0f, CornerDir * 1440.0f + FVector(0.0f, 0.0f, CoreDeckZ), 380.0f, CoolantGlow);
	const FVector BridgeMid = CornerDir * 2150.0f + FVector(0.0f, 0.0f, -155.0f);
	Box(TEXT("DiagBridgeSupport"), DarkSteel, BridgeMid, FVector(1380.0f, 300.0f, 260.0f), 45.0f, false);
	for (const float Side : {-1.0f, 1.0f})
	{
		AddFence(FString::Printf(TEXT("DiagBridgeFence_%d"), Side < 0 ? 0 : 1) + Q,
			ForgeStage::Turn(CornerDir * 2840.0f + DiagOrth * Side * 200.0f, Quarter),
			ForgeStage::Turn(CornerDir * 1480.0f + DiagOrth * Side * 200.0f, Quarter));
	}

	// Two outer catwalk bridges connecting the Corner Chamber (2450, 2450) to the adjacent Cardinal Bays.
	Box(TEXT("CatwalkToBayA"), Concrete, FVector(2520.0f, 1380.0f, -25.0f), FVector(440.0f, 1140.0f, 50.0f), 0.0f, true);
	Box(TEXT("CatwalkToBayB"), Concrete, FVector(1380.0f, 2520.0f, -25.0f), FVector(1140.0f, 440.0f, 50.0f), 0.0f, true);
	Box(TEXT("CatwalkRailA"), Baffle, FVector(2725.0f, 1380.0f, 24.0f), FVector(32.0f, 1080.0f, 48.0f), 0.0f, true, true);
	Box(TEXT("CatwalkRailB"), Baffle, FVector(1380.0f, 2725.0f, 24.0f), FVector(1080.0f, 32.0f, 48.0f), 0.0f, true, true);
	for (const float Side : {-225.0f, 225.0f})
	{
		AddFence(FString::Printf(TEXT("CatwalkFenceA_%d"), Side < 0 ? 0 : 1) + Q,
			At(2520.0f + Side, 820.0f, 0.0f), At(2520.0f + Side, 1940.0f, 0.0f));
		AddFence(FString::Printf(TEXT("CatwalkFenceB_%d"), Side < 0 ? 0 : 1) + Q,
			At(820.0f, 2520.0f + Side, 0.0f), At(1940.0f, 2520.0f + Side, 0.0f));
	}

	// ---- Angled Steel Baffle Reflector Wall & Coolant Cylinders -------------------------------------------------
	// Positioned on the outer edge of the Core Deck at (890, 890) facing 45 deg for pinball bank shots.
	Box(TEXT("CoreBaffleWall"), Baffle, FVector(890.0f, 890.0f, CoreDeckZ + 135.0f), FVector(64.0f, 480.0f, 270.0f), 45.0f, true, true);
	Box(TEXT("CoreBaffleTopGlow"), GlowHere, FVector(890.0f, 890.0f, CoreDeckZ + 274.0f), FVector(72.0f, 484.0f, 8.0f), 45.0f, false);
	Box(TEXT("CoreBaffleMidGlow"), MagmaGlow, FVector(890.0f, 890.0f, CoreDeckZ + 140.0f), FVector(72.0f, 486.0f, 16.0f), 45.0f, false);

	// Jumpable Coolant Cylinders on the foundry bay and corner chamber.
	const FVector Cylinders[] = {
		FVector(2160.0f, -580.0f, 0.0f),
		FVector(2160.0f, 580.0f, 0.0f),
		FVector(2750.0f, 2150.0f, 0.0f)
	};
	for (int32 Idx = 0; Idx < UE_ARRAY_COUNT(Cylinders); ++Idx)
	{
		const FString CylName = FString::Printf(TEXT("CoolantTank%d"), Idx) + Q;
		AddCylinder(CylName, Idx == 2 ? NextBay : BayHere, ForgeStage::Turn(Cylinders[Idx], Quarter), 150.0f, 95.0f, true);
		AddCylinder(CylName + TEXT("Cap"), Baffle, ForgeStage::Turn(Cylinders[Idx] + FVector(0.0f, 0.0f, 95.0f), Quarter), 158.0f, 8.0f, false);
		AddRing(CylName + TEXT("Glow"), CoolantGlow, ForgeStage::Turn(Cylinders[Idx] + FVector(0.0f, 0.0f, 58.0f), Quarter), 78.0f, 8.0f);
	}

	// Invisible safety fences around the remaining open edges of the Core Deck, Bay, and Corner Chamber.
	for (int32 Seg = 0; Seg < 4; ++Seg)
	{
		const float DegA = 11.5f + Seg * 6.625f;
		const float DegB = 11.5f + (Seg + 1) * 6.625f;
		const FVector P0(FMath::Cos(FMath::DegreesToRadians(DegA)) * CoreRadius, FMath::Sin(FMath::DegreesToRadians(DegA)) * CoreRadius, CoreDeckZ);
		const FVector P1(FMath::Cos(FMath::DegreesToRadians(DegB)) * CoreRadius, FMath::Sin(FMath::DegreesToRadians(DegB)) * CoreRadius, CoreDeckZ);
		AddFence(FString::Printf(TEXT("CoreArcFenceA_%d"), Seg) + Q, ForgeStage::Turn(P0, Quarter), ForgeStage::Turn(P1, Quarter));
		AddFence(FString::Printf(TEXT("CoreArcFenceB_%d"), Seg) + Q, ForgeStage::Turn(FVector(P0.X, -P0.Y, CoreDeckZ), Quarter), ForgeStage::Turn(FVector(P1.X, -P1.Y, CoreDeckZ), Quarter));
	}
	AddFence(TEXT("BayFrontFenceN") + Q, At(1950.0f, 310.0f, 0.0f), At(1950.0f, 850.0f, 0.0f));
	AddFence(TEXT("BayFrontFenceS") + Q, At(1950.0f, -310.0f, 0.0f), At(1950.0f, -850.0f, 0.0f));
	AddFence(TEXT("BayInnerFenceN") + Q, At(1950.0f, 850.0f, 0.0f), At(2300.0f, 850.0f, 0.0f));
	AddFence(TEXT("BayInnerFenceS") + Q, At(1950.0f, -850.0f, 0.0f), At(2300.0f, -850.0f, 0.0f));
	AddFence(TEXT("BayOuterFenceN") + Q, At(2740.0f, 850.0f, 0.0f), At(3250.0f, 850.0f, 0.0f));
	AddFence(TEXT("BayOuterFenceS") + Q, At(2740.0f, -850.0f, 0.0f), At(3250.0f, -850.0f, 0.0f));

	// Corner Cooling Chamber open-edge safety fences (leaving openings for DiagCoreBridge and CatwalkToBayA/B).
	const FVector ChamberInner = CornerDir * (2450.0f * UE_SQRT_2 - 630.0f);
	const FVector ChamberCenter = CornerDir * (2450.0f * UE_SQRT_2);
	AddFence(TEXT("CornerInnerFenceA") + Q,
		ForgeStage::Turn(ChamberInner - DiagOrth * 190.0f, Quarter), ForgeStage::Turn(ChamberInner - DiagOrth * 630.0f, Quarter));
	AddFence(TEXT("CornerInnerFenceB") + Q,
		ForgeStage::Turn(ChamberInner + DiagOrth * 190.0f, Quarter), ForgeStage::Turn(ChamberInner + DiagOrth * 630.0f, Quarter));
	AddFence(TEXT("CornerSideFenceA") + Q,
		ForgeStage::Turn(ChamberCenter + CornerDir * 120.0f - DiagOrth * 630.0f, Quarter),
		ForgeStage::Turn(ChamberCenter + CornerDir * 600.0f - DiagOrth * 630.0f, Quarter));
	AddFence(TEXT("CornerSideFenceB") + Q,
		ForgeStage::Turn(ChamberCenter + CornerDir * 120.0f + DiagOrth * 630.0f, Quarter),
		ForgeStage::Turn(ChamberCenter + CornerDir * 600.0f + DiagOrth * 630.0f, Quarter));
}

void AChaosImpactForgeStage::BuildThermalVent(const int32 Vent)
{
	const FString Name = FString::Printf(TEXT("ThermalVent%d"), Vent);
	const FVector At = GetVentOffset(Vent);

	// Steel grate ring and glowing hazard markings around the vent.
	VentRings.Add(AddRing(Name + TEXT("OuterRing"), Stripe, At + FVector(0.0f, 0.0f, 0.8f), VentRadius, 5.0f));
	AddRing(Name + TEXT("InnerRing"), MagmaGlow, At + FVector(0.0f, 0.0f, 1.0f), VentRadius * 0.78f, 4.0f);
	for (int32 Bar = -2; Bar <= 2; ++Bar)
	{
		const float OffsetY = Bar * 95.0f;
		const float HalfLen = FMath::Sqrt(FMath::Max(0.0f, FMath::Square(VentRadius * 0.9f) - FMath::Square(OffsetY)));
		AddPart(SceneRoot, FString::Printf(TEXT("%sGrateBar%d"), *Name, Bar + 2), DarkSteel, BoxMesh.Get(),
			At + ForgeStage::Turn(FVector(0.0f, OffsetY, 1.2f), Vent), FVector(HalfLen * 2.0f, 18.0f, 2.4f),
			FRotator(0.0f, 90.0f * Vent, 0.0f), false);
	}

	UStaticMeshComponent* Disc = AddCylinder(Name + TEXT("GlowDisc"), MagmaGlow, At + FVector(0.0f, 0.0f, 0.5f), VentRadius * 1.92f, 2.0f, false);
	Disc->SetMaterial(0, FieldAmberMaterial.Get());
	VentDiscs.Add(Disc);

	UStaticMeshComponent* WarnBeam = AddCylinder(Name + TEXT("WarnBeam"), MagmaGlow, At, 95.0f, 2800.0f, false);
	WarnBeam->SetMaterial(0, FieldAmberMaterial.Get());
	WarnBeam->SetVisibility(false);
	VentWarnBeams.Add(WarnBeam);

	UStaticMeshComponent* EruptBeam = AddCylinder(Name + TEXT("EruptBeam"), MagmaGlow, At, VentRadius * 2.0f, 2800.0f, false);
	EruptBeam->SetMaterial(0, FieldAmberMaterial.Get());
	EruptBeam->SetVisibility(false);
	VentEruptBeams.Add(EruptBeam);

	UPointLightComponent* Light = CreateDefaultSubobject<UPointLightComponent>(*(Name + TEXT("Light")));
	Light->SetupAttachment(SceneRoot);
	Light->SetRelativeLocation(At + FVector(0.0f, 0.0f, 240.0f));
	Light->SetLightColor(FLinearColor(1.0f, 0.38f, 0.05f));
	Light->SetIntensity(1800.0f);
	Light->SetAttenuationRadius(1350.0f);
	Light->SetCastShadows(false);
	VentLights.Add(Light);
}

void AChaosImpactForgeStage::BuildFoundryScenery(const int32 Quarter)
{
	// Placed beyond 6000 units so the opening camera sweep never clips into machinery.
	const FString Q = FString::Printf(TEXT("_Q%d"), Quarter);
	const float Turned = 90.0f * Quarter;
	const uint8 BayHere = Bay + Quarter;
	const uint8 GlowHere = BayGlow + Quarter;

	// Basalt crust islands floating in the magma sea.
	AddPart(SceneRoot, TEXT("BasaltCrustA") + Q, Scenery, ChamferMesh.Get(), ForgeStage::Turn(FVector(4600.0f, -1500.0f, -430.0f), Quarter),
		FVector(960.0f, 1280.0f, 90.0f), FRotator(0.0f, Turned + 18.0f, 0.0f), false);
	AddPart(SceneRoot, TEXT("BasaltCrustB") + Q, Scenery, ChamferMesh.Get(), ForgeStage::Turn(FVector(5100.0f, 2200.0f, -430.0f), Quarter),
		FVector(1100.0f, 840.0f, 110.0f), FRotator(0.0f, Turned - 24.0f, 0.0f), false);

	// Colossal Cooling Tower at the corner (7400, 7400) with glowing furnace rings.
	const FVector TowerAt = ForgeStage::Turn(FVector(7400.0f, 7400.0f, -450.0f), Quarter);
	AddPart(SceneRoot, TEXT("CoolingTowerBase") + Q, DarkSteel, CylinderMesh.Get(), TowerAt,
		FVector(2200.0f, 2200.0f, 2800.0f), FRotator::ZeroRotator, false);
	AddPart(SceneRoot, TEXT("CoolingTowerBandLow") + Q, MagmaGlow, RingMesh.Get(), TowerAt + FVector(0.0f, 0.0f, 900.0f),
		FVector(1150.0f * 100.0f / 70.0f, 1150.0f * 100.0f / 70.0f, 36.0f * 100.0f / 13.0f), FRotator::ZeroRotator, false);
	AddPart(SceneRoot, TEXT("CoolingTowerBandHigh") + Q, CoolantGlow, RingMesh.Get(), TowerAt + FVector(0.0f, 0.0f, 2100.0f),
		FVector(1150.0f * 100.0f / 70.0f, 1150.0f * 100.0f / 70.0f, 36.0f * 100.0f / 13.0f), FRotator::ZeroRotator, false);

	// Animated Heavy Smelting Piston behind the cardinal foundry bay (6400, 0).
	const FVector ForgeFrameAt = ForgeStage::Turn(FVector(6400.0f, 0.0f, -450.0f), Quarter);
	for (const float Side : {-900.0f, 900.0f})
	{
		AddPart(SceneRoot, FString::Printf(TEXT("SmelterCol_%d"), Side < 0 ? 0 : 1) + Q, DarkSteel, BoxMesh.Get(),
			ForgeFrameAt + ForgeStage::Turn(FVector(0.0f, Side, 1200.0f), Quarter), FVector(360.0f, 360.0f, 2400.0f),
			FRotator(0.0f, Turned, 0.0f), false);
	}
	AddPart(SceneRoot, TEXT("SmelterCrown") + Q, BayHere, BoxMesh.Get(), ForgeFrameAt + FVector(0.0f, 0.0f, 2450.0f),
		FVector(520.0f, 2300.0f, 320.0f), FRotator(0.0f, Turned, 0.0f), false);
	AddPart(SceneRoot, TEXT("SmelterAnvil") + Q, DarkSteel, CylinderMesh.Get(), ForgeFrameAt,
		FVector(1200.0f, 1200.0f, 520.0f), FRotator::ZeroRotator, false);
	AddPart(SceneRoot, TEXT("SmelterAnvilGlow") + Q, MagmaGlow, RingMesh.Get(), ForgeFrameAt + FVector(0.0f, 0.0f, 525.0f),
		FVector(580.0f * 100.0f / 70.0f, 580.0f * 100.0f / 70.0f, 24.0f * 100.0f / 13.0f), FRotator::ZeroRotator, false);

	USceneComponent* Piston = CreateDefaultSubobject<USceneComponent>(*(TEXT("PistonRoot") + Q));
	Piston->SetupAttachment(SceneRoot);
	Piston->SetRelativeLocation(ForgeStage::Turn(FVector(6400.0f, 0.0f, 650.0f), Quarter));
	Piston->SetRelativeRotation(FRotator(0.0f, Turned, 0.0f));
	PistonRoots.Add(Piston);
	AddPart(Piston, TEXT("PistonShaft") + Q, Baffle, CylinderMesh.Get(), FVector(0.0f, 0.0f, 200.0f),
		FVector(420.0f, 420.0f, 1200.0f), FRotator::ZeroRotator, false);
	AddPart(Piston, TEXT("PistonHead") + Q, BayHere, CylinderMesh.Get(), FVector(0.0f, 0.0f, 0.0f),
		FVector(920.0f, 920.0f, 260.0f), FRotator::ZeroRotator, false);
	AddPart(Piston, TEXT("PistonHeadGlow") + Q, GlowHere, RingMesh.Get(), FVector(0.0f, 0.0f, 40.0f),
		FVector(470.0f * 100.0f / 70.0f, 470.0f * 100.0f / 70.0f, 18.0f * 100.0f / 13.0f), FRotator::ZeroRotator, false);

	// Swinging Molten-Metal Ladle Crane arm over the magma sea.
	USceneComponent* Ladle = CreateDefaultSubobject<USceneComponent>(*(TEXT("LadleRoot") + Q));
	Ladle->SetupAttachment(SceneRoot);
	Ladle->SetRelativeLocation(ForgeStage::Turn(FVector(5800.0f, -3400.0f, 1350.0f), Quarter));
	Ladle->SetRelativeRotation(FRotator(0.0f, Turned, 0.0f));
	LadleRoots.Add(Ladle);
	AddPart(Ladle, TEXT("LadleArm") + Q, DarkSteel, BoxMesh.Get(), FVector(-450.0f, 0.0f, 0.0f),
		FVector(1400.0f, 180.0f, 140.0f), FRotator::ZeroRotator, false);
	AddPart(Ladle, TEXT("LadleCable") + Q, Baffle, CylinderMesh.Get(), FVector(-950.0f, 0.0f, -650.0f),
		FVector(36.0f, 36.0f, 650.0f), FRotator::ZeroRotator, false);
	AddPart(Ladle, TEXT("LadleBucket") + Q, DarkSteel, CylinderMesh.Get(), FVector(-950.0f, 0.0f, -1020.0f),
		FVector(520.0f, 520.0f, 380.0f), FRotator::ZeroRotator, false);
	AddPart(Ladle, TEXT("LadleMoltenTop") + Q, MagmaGlow, CylinderMesh.Get(), FVector(-950.0f, 0.0f, -660.0f),
		FVector(460.0f, 460.0f, 30.0f), FRotator::ZeroRotator, false);
}

void AChaosImpactForgeStage::AddContainer(const FString& Name, const uint8 Paint, const uint8 DarkPaint,
	const FVector& Location, const FVector& Size, const float Yaw)
{
	const FRotator Facing(0.0f, Yaw, 0.0f);
	const FVector Half = Size * 0.5f;
	UStaticMeshComponent* Body = AddBox(Name, Paint, Location, Size, Yaw, true, true);
	if (CorrugatedMaterial)
	{
		Body->SetMaterial(0, CorrugatedMaterial);
	}
	for (const float End : {-1.0f, 1.0f})
	{
		const FString Side = End < 0.0f ? TEXT("A") : TEXT("B");
		AddBox(Name + TEXT("Door") + Side, DarkPaint, Location + Facing.RotateVector(FVector(0.0f, End * (Half.Y + 2.0f), 0.0f)),
			FVector(Size.X - 30.0f, 4.0f, Size.Z - 26.0f), Yaw, false);
	}
	AddBox(Name + TEXT("TopGlow"), Stripe, Location + FVector(0.0f, 0.0f, Half.Z + 1.5f),
		FVector(Size.X - 20.0f, Size.Y - 20.0f, 3.0f), Yaw, false);
}

void AChaosImpactForgeStage::AddFence(const FString& Name, const FVector& From, const FVector& To)
{
	const FVector Span = To - From;
	const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Span.Y, Span.X));
	UStaticMeshComponent* Fence = AddPart(SceneRoot, Name, DarkSteel, BoxMesh.Get(),
		(From + To) * 0.5f + FVector(0.0f, 0.0f, 300.0f), FVector(Span.Size2D(), 40.0f, 600.0f),
		FRotator(0.0f, Yaw, 0.0f), false);
	Fence->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Fence->SetCollisionObjectType(ECC_Destructible);
	Fence->SetCollisionResponseToAllChannels(ECR_Ignore);
	Fence->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	Fence->SetVisibility(false);
	Fence->SetHiddenInGame(true);
	Fence->ComponentTags.Add(TEXT("ForgeFence"));
}

UStaticMeshComponent* AChaosImpactForgeStage::AddPart(USceneComponent* Parent, const FString& Name, const uint8 Paint,
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

	const bool bGlow = Paint == MagmaGlow || Paint == CoolantGlow || (Paint >= BayGlow && Paint < BayGlow + 4);
	UMaterialInterface* Surface = FlatMaterial.Get();
	if (bGlow)
	{
		Surface = GlowMaterial.Get();
	}
	else if (Paint == SteelFloor && MetalFloorMaterial)
	{
		Surface = MetalFloorMaterial.Get();
	}
	else if (Paint == Concrete && ConcreteMaterial)
	{
		Surface = ConcreteMaterial.Get();
	}
	else if ((Paint == Baffle || Paint == Scenery) && PanelMaterial)
	{
		Surface = PanelMaterial.Get();
	}
	else if (Paint == Gold && GoldMaterial)
	{
		Surface = GoldMaterial.Get();
	}
	else if (Paint != DarkSteel && PaintMaterial)
	{
		Surface = PaintMaterial.Get();
	}
	Block->SetMaterial(0, Surface);
	Block->ComponentTags.Add(FName(*FString::Printf(TEXT("CIPaint%d"), Paint)));
	return Block;
}

UStaticMeshComponent* AChaosImpactForgeStage::AddBox(const FString& Name, const uint8 Paint, const FVector& Location,
	const FVector& Size, const float Yaw, const bool bCollision, const bool bChamfer)
{
	return AddPart(SceneRoot, Name, Paint, bChamfer ? ChamferMesh.Get() : BoxMesh.Get(), ForgeStage::Spread(Location), ForgeStage::Spread(Size),
		FRotator(0.0f, Yaw, 0.0f), bCollision);
}

UStaticMeshComponent* AChaosImpactForgeStage::AddCylinder(const FString& Name, const uint8 Paint, const FVector& Location,
	const float Diameter, const float Height, const bool bCollision)
{
	return AddPart(SceneRoot, Name, Paint, CylinderMesh.Get(), ForgeStage::Spread(Location),
		FVector(Diameter * LayoutScale, Diameter * LayoutScale, Height), FRotator::ZeroRotator, bCollision);
}

UStaticMeshComponent* AChaosImpactForgeStage::AddRing(const FString& Name, const uint8 Paint, const FVector& Location,
	const float Radius, const float Thickness)
{
	return AddPart(SceneRoot, Name, Paint, RingMesh.Get(), ForgeStage::Spread(Location),
		FVector(Radius * LayoutScale * 100.0f / 70.0f, Radius * LayoutScale * 100.0f / 70.0f, Thickness * 100.0f / 13.0f),
		FRotator::ZeroRotator, false);
}

UStaticMeshComponent* AChaosImpactForgeStage::AddRamp(const FString& Name, const uint8 Paint, const FVector& LayoutLow,
	const FVector& LayoutHigh, const float LayoutWidth, const uint8 EdgePaint)
{
	const FVector Low = ForgeStage::Spread(LayoutLow);
	const FVector High = ForgeStage::Spread(LayoutHigh);
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

void AChaosImpactForgeStage::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyPalette();
}

void AChaosImpactForgeStage::BeginPlay()
{
	Super::BeginPlay();
	ApplyPalette();
	VentDiscLooks.Reset();
	VentWarnLooks.Reset();
	VentEruptLooks.Reset();
	for (UStaticMeshComponent* Disc : VentDiscs)
	{
		VentDiscLooks.Add(Disc ? Disc->CreateDynamicMaterialInstance(0) : nullptr);
	}
	for (UStaticMeshComponent* Beam : VentWarnBeams)
	{
		VentWarnLooks.Add(Beam ? Beam->CreateDynamicMaterialInstance(0) : nullptr);
	}
	for (UStaticMeshComponent* Beam : VentEruptBeams)
	{
		VentEruptLooks.Add(Beam ? Beam->CreateDynamicMaterialInstance(0) : nullptr);
	}
	UpdateVentsLook(0.0f);
}

void AChaosImpactForgeStage::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AChaosImpactForgeStage, ActiveVentPair);
	DOREPLIFETIME(AChaosImpactForgeStage, VentStartedAt);
}

FLinearColor AChaosImpactForgeStage::GetPaintColor(const uint8 Paint) const
{
	const auto BayAt = [this](const int32 Quarter)
	{
		return BayColors.IsValidIndex(Quarter) ? BayColors[Quarter] : FLinearColor(1.0f, 0.32f, 0.02f);
	};
	if (Paint >= BayGlow) { return BayAt(Paint - BayGlow); }
	if (Paint >= BayDark) { return BayAt(Paint - BayDark) * 0.35f; }
	if (Paint >= Bay) { return BayAt(Paint - Bay); }
	switch (Paint)
	{
	case SteelFloor: return SteelFloorColor;
	case Concrete: return ConcreteColor;
	case Baffle: return BaffleColor;
	case Stripe: return StripeColor;
	case MagmaGlow: return MagmaGlowColor;
	case CoolantGlow: return CoolantGlowColor;
	case DarkSteel: return FLinearColor(0.045f, 0.05f, 0.065f);
	case Gold: return FLinearColor(1.0f, 0.72f, 0.12f);
	default: return SceneryColor;
	}
}

bool AChaosImpactForgeStage::ReadPaint(const UStaticMeshComponent* Block, uint8& OutPaint)
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

void AChaosImpactForgeStage::ApplyPalette()
{
	if (!bApplyDefaultColors)
	{
		return;
	}
	TArray<UTextRenderComponent*> Texts;
	GetComponents(Texts);
	for (UTextRenderComponent* Text : Texts)
	{
		if (Text->ComponentHasTag(TEXT("CIBayText")))
		{
			Text->SetTextRenderColor((StripeColor * 0.7f).ToFColor(true));
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
		if (Paint == MagmaGlow || Paint == CoolantGlow || (Paint >= BayGlow && Paint < BayGlow + 4))
		{
			Material->SetVectorParameterValue(TEXT("Color"), Color);
			Material->SetScalarParameterValue(TEXT("Intensity"), GlowIntensity);
		}
		else
		{
			Material->SetVectorParameterValue(TEXT("Color"), Color);
			Material->SetVectorParameterValue(TEXT("Base Color"), Color);
			Material->SetVectorParameterValue(TEXT("BaseColor"), Color);
			Material->SetScalarParameterValue(TEXT("Roughness"), Paint == SteelFloor ? 0.35f : 0.55f);
		}
	}
}

TArray<FVector> AChaosImpactForgeStage::GetSpawnPoints() const
{
	return ToWorld(SpawnPointOffsets);
}

TArray<FVector> AChaosImpactForgeStage::GetBallPoints() const
{
	return ToWorld(BallPointOffsets);
}

bool AChaosImpactForgeStage::IsOutside(const FVector& Location) const
{
	if (Super::IsOutside(Location))
	{
		return true;
	}
	// Fallen off the platforms and bridges toward the magma sea.
	const FVector Local = GetActorTransform().InverseTransformPositionNoScale(Location);
	return Local.Z < -320.0f;
}

double AChaosImpactForgeStage::GetServerNow() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0;
	}
	const AGameStateBase* State = World->GetGameState();
	return State ? State->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
}

bool AChaosImpactForgeStage::IsMatchOn() const
{
	const AChaosImpactGameState* Match = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	return Match && Match->bVersusMatch && Match->Phase == EChaosImpactOnlinePhase::Match;
}

bool AChaosImpactForgeStage::IsVentInPair(const int32 Vent, const int32 Pair) const
{
	return Pair >= 0 && (Vent % 2) == (Pair % 2);
}

float AChaosImpactForgeStage::GetVentProgress() const
{
	if (ActiveVentPair < 0)
	{
		return 0.0f;
	}
	const float Age = static_cast<float>(GetServerNow() - VentStartedAt);
	return Age < VentWarnSeconds
		? FMath::Clamp(Age / FMath::Max(VentWarnSeconds, 0.1f), 0.0f, 1.0f)
		: 1.0f + FMath::Clamp((Age - VentWarnSeconds) / FMath::Max(VentEruptSeconds, 0.1f), 0.0f, 1.0f);
}

void AChaosImpactForgeStage::StartVentPair(const int32 Pair)
{
	if (!HasAuthority())
	{
		return;
	}
	const int32 Clamped = Pair & 1;
	ActiveVentPair = Clamped;
	LastVentPair = Clamped;
	VentStartedAt = static_cast<float>(GetServerNow());
	bVentErupted = false;
	VentVictims.Reset();
	BoostedBalls.Reset();
	ForceNetUpdate();
	OnRep_VentState();
}

void AChaosImpactForgeStage::OnRep_VentState()
{
	if (ActiveVentPair >= 0 && GetNetMode() != NM_DedicatedServer)
	{
		for (int32 Vent = 0; Vent < 4; ++Vent)
		{
			if (IsVentInPair(Vent, ActiveVentPair))
			{
				const FVector At = GetActorTransform().TransformPosition(GetVentOffset(Vent));
				ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::NovaCharge, At, 0.55f, 1.15f);
			}
		}
	}
}

void AChaosImpactForgeStage::MulticastVentErupt_Implementation(const int32 Pair)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	for (int32 Vent = 0; Vent < 4; ++Vent)
	{
		if (!IsVentInPair(Vent, Pair))
		{
			continue;
		}
		const FVector At = GetActorTransform().TransformPosition(GetVentOffset(Vent));
		ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::ThunderBurst, At, 0.9f, 0.72f);
		ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::BeamStrike, At, 0.85f, 0.68f);
		if (UNiagaraSystem* Burst = ChaosImpactBallTypes::LoadEffect(ChaosImpactBallTypes::Effects::WarpIn))
		{
			if (UNiagaraComponent* Effect = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
				this, Burst, At + FVector(0.0f, 0.0f, 24.0f), FRotator::ZeroRotator, FVector(2.8f)))
			{
				ChaosImpactBallTypes::SetEffectColor(Effect, TEXT("Base Color"), FLinearColor(1.0f, 0.35f, 0.05f));
			}
		}
		if (UNiagaraSystem* Sparks = ChaosImpactBallTypes::LoadEffect(ChaosImpactBallTypes::Effects::SparkBurst))
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, Sparks, At + FVector(0.0f, 0.0f, 65.0f), FRotator::ZeroRotator, FVector(2.4f));
		}
	}
}

void AChaosImpactForgeStage::MulticastBallSuperheated_Implementation(const FVector_NetQuantize At)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::BeamStrike, At, 0.8f, 1.4f);
	if (UNiagaraSystem* Sparks = ChaosImpactBallTypes::LoadEffect(ChaosImpactBallTypes::Effects::SparkBurst))
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, Sparks, At, FRotator::ZeroRotator, FVector(1.8f));
	}
}

void AChaosImpactForgeStage::TickCrucibleServer()
{
	// Like TempleStage's blessed altar, the central Crucible pad always spawns high-impact special balls.
	if (GetWorld()->GetTimeSeconds() < NextCrucibleCheckAt)
	{
		return;
	}
	NextCrucibleCheckAt = GetWorld()->GetTimeSeconds() + 1.0;
	for (TActorIterator<AChaosImpactBallSpawner> It(GetWorld()); It; ++It)
	{
		if (FVector::Dist2D(It->GetActorLocation(), GetActorLocation()) < 220.0f && It->GetChance(EChaosImpactBallType::Normal) > 0.0f)
		{
			It->SetChanceWeight(EChaosImpactBallType::Normal, 0.0f);
			It->SetChanceWeight(EChaosImpactBallType::Fire, 0.32f);
			It->SetChanceWeight(EChaosImpactBallType::Drive, 0.18f);
			It->SetChanceWeight(EChaosImpactBallType::Black, 0.14f);
			It->SetChanceWeight(EChaosImpactBallType::Thunder, 0.14f);
			It->SetChanceWeight(EChaosImpactBallType::Beam, 0.08f);
			if (AChaosImpactBall* Lying = It->GetActiveBall(); Lying && Lying->IsPickup() && Lying->GetBallType() == EChaosImpactBallType::Normal)
			{
				Lying->Destroy();
			}
		}
	}
}

void AChaosImpactForgeStage::TickVentsServer(const double Now)
{
	if (!IsMatchOn())
	{
		NextVentAt = -1.0;
		if (ActiveVentPair >= 0)
		{
			ActiveVentPair = -1;
			ForceNetUpdate();
		}
		return;
	}
	if (NextVentAt < 0.0)
	{
		NextVentAt = Now + FirstVentAfter;
	}
	if (ActiveVentPair < 0)
	{
		if (Now >= NextVentAt)
		{
			int32 NextPair = FMath::RandRange(0, 1);
			if (NextPair == LastVentPair)
			{
				NextPair = 1 - NextPair;
			}
			StartVentPair(NextPair);
		}
		return;
	}

	const float Age = static_cast<float>(Now - VentStartedAt);
	if (Age >= VentWarnSeconds && !bVentErupted)
	{
		bVentErupted = true;
		MulticastVentErupt(ActiveVentPair);
	}
	if (bVentErupted && Age < VentWarnSeconds + VentEruptSeconds)
	{
		for (int32 Vent = 0; Vent < 4; ++Vent)
		{
			if (!IsVentInPair(Vent, ActiveVentPair))
			{
				continue;
			}
			const FVector At = GetActorTransform().TransformPosition(GetVentOffset(Vent));
			// 1. Characters caught on the erupting vent take damage and launch upward & outward.
			for (TActorIterator<AChaosImpactCharacter> It(GetWorld()); It; ++It)
			{
				AChaosImpactCharacter* Character = *It;
				const FVector Delta = Character->GetActorLocation() - At;
				if (Character->IsEliminated() || VentVictims.Contains(Character)
					|| Delta.Size2D() > VentRadius + 30.0f || Delta.Z < -90.0f || Delta.Z > 520.0f)
				{
					continue;
				}
				const float Applied = Character->TakeDamage(VentDamage, FDamageEvent(), nullptr, this);
				if (Applied > 0.0f)
				{
					VentVictims.Add(Character);
					if (!Character->IsEliminated())
					{
						Character->LaunchCharacter(FVector(Delta.X, Delta.Y, 0.0f).GetSafeNormal() * 380.0f + FVector(0.0f, 0.0f, 780.0f), true, true);
					}
				}
			}
			// 2. Flying balls passing through the thermal plume get superheated (1.35x speed boost).
			for (TActorIterator<AChaosImpactBall> It(GetWorld()); It; ++It)
			{
				AChaosImpactBall* Ball = *It;
				const EChaosImpactBallType Type = Ball->GetBallType();
				if (!Ball->IsFlyingOnServer() || BoostedBalls.Contains(Ball)
					|| Type == EChaosImpactBallType::Beam || Type == EChaosImpactBallType::Nova)
				{
					continue;
				}
				const FVector Delta = Ball->GetActorLocation() - At;
				if (Delta.Size2D() <= VentRadius + 65.0f && Delta.Z > -40.0f && Delta.Z < 550.0f)
				{
					BoostedBalls.Add(Ball);
					Ball->DeflectByWind(Ball->GetBallVelocity() * VentBallBoost);
					MulticastBallSuperheated(Ball->GetActorLocation());
				}
			}
		}
	}
	if (Age >= VentWarnSeconds + VentEruptSeconds)
	{
		ActiveVentPair = -1;
		ForceNetUpdate();
		NextVentAt = Now + FMath::FRandRange(VentIntervalMin, FMath::Max(VentIntervalMin, VentIntervalMax));
	}
}

void AChaosImpactForgeStage::UpdateVentsLook(const float DeltaSeconds)
{
	const float Progress = GetVentProgress();
	for (int32 Vent = 0; Vent < VentDiscs.Num(); ++Vent)
	{
		const bool bSelected = IsVentInPair(Vent, ActiveVentPair);
		const float Warn = bSelected && Progress < 1.0f ? Progress : 0.0f;
		const bool bErupt = bSelected && Progress >= 1.0f;
		const float EruptT = bErupt ? Progress - 1.0f : 0.0f;

		const float Rest = 0.7f + 0.3f * FMath::Sin(LookClock * 1.8f + Vent);
		const float Flicker = Warn > 0.0f ? (2.5f + 7.0f * Warn) * (0.6f + 0.4f * FMath::Sin(LookClock * (12.0f + 26.0f * Warn))) : 0.0f;
		if (UMaterialInstanceDynamic* Look = VentDiscLooks.IsValidIndex(Vent) ? VentDiscLooks[Vent].Get() : nullptr)
		{
			Look->SetScalarParameterValue(TEXT("Intensity"), bErupt ? 14.0f : Rest + Flicker);
		}
		if (UStaticMeshComponent* Ring = VentRings.IsValidIndex(Vent) ? VentRings[Vent].Get() : nullptr)
		{
			const float Scale = (VentRadius / 70.0f) * (1.0f + (Warn > 0.0f ? 0.05f * FMath::Sin(LookClock * 18.0f) : 0.0f));
			Ring->SetRelativeScale3D(FVector(Scale, Scale, (5.0f + Warn * 18.0f) / 13.0f));
		}
		if (UStaticMeshComponent* WarnBeam = VentWarnBeams.IsValidIndex(Vent) ? VentWarnBeams[Vent].Get() : nullptr)
		{
			WarnBeam->SetVisibility(Warn > 0.0f);
			WarnBeam->SetRelativeScale3D(FVector(0.9f + Warn * 1.6f, 0.9f + Warn * 1.6f, 28.0f));
			if (UMaterialInstanceDynamic* Look = VentWarnLooks.IsValidIndex(Vent) ? VentWarnLooks[Vent].Get() : nullptr)
			{
				Look->SetScalarParameterValue(TEXT("Intensity"), 2.2f + 6.5f * Warn);
			}
		}
		if (UStaticMeshComponent* EruptBeam = VentEruptBeams.IsValidIndex(Vent) ? VentEruptBeams[Vent].Get() : nullptr)
		{
			EruptBeam->SetVisibility(bErupt);
			const float Width = (VentRadius * 2.0f / 100.0f) * (EruptT < 0.18f ? 1.15f : 1.0f - 0.65f * FMath::Square((EruptT - 0.18f) / 0.82f));
			EruptBeam->SetRelativeScale3D(FVector(Width, Width, 28.0f));
			if (UMaterialInstanceDynamic* Look = VentEruptLooks.IsValidIndex(Vent) ? VentEruptLooks[Vent].Get() : nullptr)
			{
				Look->SetScalarParameterValue(TEXT("Intensity"), 11.0f * (1.0f - EruptT) + 2.5f);
			}
		}
		if (UPointLightComponent* Light = VentLights.IsValidIndex(Vent) ? VentLights[Vent].Get() : nullptr)
		{
			Light->SetIntensity(bErupt ? 85000.0f * (1.0f - EruptT) + 7000.0f : Warn * 28000.0f + 1800.0f);
		}
	}
}

void AChaosImpactForgeStage::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!GetWorld())
	{
		return;
	}
	if (HasAuthority())
	{
		TickVentsServer(GetServerNow());
		TickCrucibleServer();
	}
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	LookClock += DeltaSeconds;
	UpdateVentsLook(DeltaSeconds);

	for (int32 Index = 0; Index < PistonRoots.Num(); ++Index)
	{
		if (USceneComponent* Piston = PistonRoots[Index])
		{
			const float Cycle = FMath::Sin(LookClock * 1.6f + Index * 1.57f);
			const float Height = 320.0f + (Cycle > 0.0f ? FMath::Pow(Cycle, 0.6f) : Cycle * 0.4f) * 340.0f;
			Piston->SetRelativeLocation(ForgeStage::Turn(FVector(6400.0f, 0.0f, Height), Index));
		}
	}
	for (int32 Index = 0; Index < LadleRoots.Num(); ++Index)
	{
		if (USceneComponent* Ladle = LadleRoots[Index])
		{
			const float Swing = FMath::Sin(LookClock * 0.55f + Index * 1.57f) * 18.0f;
			Ladle->SetRelativeRotation(FRotator(0.0f, 90.0f * Index + Swing, 0.0f));
		}
	}
	for (int32 Index = 0; Index < CrucibleRings.Num(); ++Index)
	{
		if (UStaticMeshComponent* Ring = CrucibleRings[Index])
		{
			const float Wave = 0.5f + 0.5f * FMath::Sin(LookClock * 2.4f - Index * 1.2f);
			const float Base = Index == 0 ? 380.0f : 235.0f;
			Ring->SetRelativeScale3D(FVector(Base / 70.0f, Base / 70.0f, (5.0f + 9.0f * Wave) / 13.0f));
		}
	}
}
