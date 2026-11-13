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
	/** The foundry deck: half its width, how far back each of its corners is cut (an octagon), and how thick it is. */
	constexpr float DeckHalf = 3150.0f;
	constexpr float DeckChamfer = 1000.0f;
	constexpr float DeckThick = 80.0f;
	/** The core in the middle (a concrete ring the crucible and the four vents stand in). */
	constexpr float CoreRadius = 1500.0f;
	constexpr float CrucibleTop = 35.0f;
	/** How far out along each diagonal the corner cooling chamber's blast wall stands, and the chamber's own middle. */
	constexpr float CornerWallDistance = 2950.0f;
	constexpr float ChamberDistance = 3366.0f;

	const FVector QuarterSpawnOffsets[] =
	{
		FVector(2850.0f, 0.0f, 0.0f),                            // in the foundry bay, behind its container
		FVector(2380.0f, 2380.0f, 0.0f),                         // in the corner cooling chamber, behind its blast wall
		FVector(1220.0f, -520.0f, 0.0f)                          // on the core beside a vent
	};

	const FVector QuarterBallOffsets[] =
	{
		FVector(2050.0f, 0.0f, 0.0f),                            // in front of the foundry bay
		FVector(1700.0f, 1700.0f, 0.0f)                          // in front of the corner cooling chamber
	};

	FVector Turn(const FVector& Offset, const int32 Quarter)
	{
		return FRotator(0.0f, 90.0f * Quarter, 0.0f).RotateVector(Offset);
	}

	FVector Spread(const FVector& Layout)
	{
		return FVector(Layout.X * AChaosImpactForgeStage::LayoutScale, Layout.Y * AChaosImpactForgeStage::LayoutScale, Layout.Z);
	}

	/** The deck's outline brought in by Inset (out, if negative): its eight corners, anticlockwise from the +X side's -Y end. */
	TArray<FVector> DeckOutline(const float Inset, const float Z)
	{
		const float Side = DeckHalf - Inset;
		const float Cut = DeckHalf - DeckChamfer - Inset * (UE_SQRT_2 - 1.0f);
		return {FVector(Side, -Cut, Z), FVector(Side, Cut, Z), FVector(Cut, Side, Z), FVector(-Cut, Side, Z),
			FVector(-Side, Cut, Z), FVector(-Side, -Cut, Z), FVector(-Cut, -Side, Z), FVector(Cut, -Side, Z)};
	}
}

FVector AChaosImpactForgeStage::GetVentOffset(const int32 Vent)
{
	return ForgeStage::Spread(ForgeStage::Turn(FVector(920.0f, 0.0f, 0.0f), FMath::Clamp(Vent, 0, 3)));
}

bool AChaosImpactForgeStage::IsGlowPaint(const uint8 Paint)
{
	return Paint == MagmaGlow || Paint == CoolantGlow || Paint == MagmaSea || (Paint >= BayGlow && Paint < BayGlow + 4);
}

AChaosImpactForgeStage::AChaosImpactForgeStage()
{
	CrashBallStartOffset = FVector(0, 0, ForgeStage::CrucibleTop);
	CrashRouteMaterial = FSoftObjectPath(TEXT("/Game/ChaosImpact/Versus/M_StageMetalFloor.M_StageMetalFloor"));
	Team0GoalOffset = FVector(-2990, 0, 0);
	Team1GoalOffset = FVector(2990, 0, 0);
	// クラッシュボール: the goals (pedestals) at opposite corners (the ground round them is cleared), and the ways round to them.
	SetCrashDiagonalGoals(FVector(-2100, -2700, 0), FVector(2100, 2700, 0), {FVector(150, 150, 35), FVector(150, 1350, 0), FVector(900, 1500, 0)}, {FVector(-150, -150, 35), FVector(-150, -1350, 0), FVector(-900, -1500, 0)});
	CrashRemovedObstaclePrefixes = {TEXT("BayContainer_Q0"), TEXT("BayContainer_Q2")};
	CrashBallExtraBallOffsets = {FVector(-1100, 400, 0), FVector(1100, -400, 0), FVector(-2400, 600, 0), FVector(2400, -600, 0)};
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
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane.Plane"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> WarnMat(TEXT("/Game/ChaosImpact/FX/Kit/M_CIFX_Warn.M_CIFX_Warn"));

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
	PlaneMesh = Plane.Object;
	WarnMaterial = WarnMat.Succeeded() ? WarnMat.Object : nullptr;

	BuildFloor();

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

void AChaosImpactForgeStage::BuildFloor()
{
	using namespace ForgeStage;
	// The magma sea far below (-440): a deep red glow, mostly crusted over with dark basalt plates, glowing through the
	// cracks between them.
	AddBox(TEXT("MagmaSeaSurface"), MagmaSea, FVector(0.0f, 0.0f, -450.0f), FVector(22000.0f, 22000.0f, 20.0f), 0.0f, false);
	AddBox(TEXT("MagmaBed"), Scenery, FVector(0.0f, 0.0f, -520.0f), FVector(22000.0f, 22000.0f, 60.0f), 0.0f, false);
	FRandomStream Random(61);
	int32 Crust = 0;
	for (int32 Row = -5; Row < 5; ++Row)
	{
		for (int32 Column = -5; Column < 5; ++Column)
		{
			const FVector Cell((Column + 0.5f) * 2200.0f + Random.FRandRange(-380.0f, 380.0f),
				(Row + 0.5f) * 2200.0f + Random.FRandRange(-380.0f, 380.0f), -441.0f + Random.FRandRange(0.0f, 4.0f));
			if (FMath::Max(FMath::Abs(Cell.X), FMath::Abs(Cell.Y)) < 2900.0f)
			{
				continue;
			}
			AddPart(SceneRoot, FString::Printf(TEXT("MagmaCrust%d"), Crust++), DarkSteel, ChamferMesh.Get(), Cell,
				FVector(Random.FRandRange(1150.0f, 1900.0f), Random.FRandRange(950.0f, 1600.0f), 26.0f),
				FRotator(0.0f, Random.FRandRange(0.0f, 180.0f), 0.0f), false);
		}
	}

	// The deck: one wide steel floor, an octagon (a slab across, one at each end, and a square turned to fill each cut
	// corner, its top a hair lower so no two tops meet flush), on a dark skirt.
	const auto Octagon = [this](const FString& Name, const uint8 Paint, const float Half, const float Top, const float Thick,
		const bool bCollision)
	{
		const float Straight = Half - DeckChamfer;
		AddBox(Name + TEXT("Main"), Paint, FVector(0.0f, 0.0f, Top - Thick * 0.5f), FVector(Half * 2.0f, Straight * 2.0f, Thick),
			0.0f, bCollision);
		for (const float Sign : {-1.0f, 1.0f})
		{
			AddBox(Name + (Sign > 0.0f ? TEXT("End1") : TEXT("End0")), Paint,
				FVector(0.0f, Sign * (Straight + DeckChamfer * 0.5f), Top - Thick * 0.5f), FVector(Straight * 2.0f, DeckChamfer, Thick),
				0.0f, bCollision);
		}
		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			AddBox(FString::Printf(TEXT("%sCorner%d"), *Name, Corner), Paint, Turn(FVector(Straight, Straight, Top - Thick * 0.5f - 0.5f), Corner),
				FVector(DeckChamfer * UE_SQRT_2, DeckChamfer * UE_SQRT_2, Thick), 45.0f, bCollision);
		}
	};
	Octagon(TEXT("Deck"), SteelFloor, DeckHalf, 0.0f, DeckThick, true);
	Octagon(TEXT("DeckSkirt"), DarkSteel, DeckHalf + 40.0f, -DeckThick, 90.0f, false);

	// Under the deck: the furnace the core sits on, reaching down into the magma, and girder legs round it.
	AddPart(SceneRoot, TEXT("CoreFurnaceSub"), DarkSteel, ConeMesh.Get(), FVector(0.0f, 0.0f, -640.0f),
		FVector(CoreRadius * 1.9f, CoreRadius * 1.9f, 920.0f), FRotator(180.0f, 0.0f, 0.0f), false);
	AddRing(TEXT("CoreSubMagmaRing"), MagmaGlow, FVector(0.0f, 0.0f, -300.0f), CoreRadius * 0.72f, 18.0f);
	for (int32 Leg = 0; Leg < 8; ++Leg)
	{
		const float Angle = FMath::DegreesToRadians(22.5f + Leg * 45.0f);
		AddBox(FString::Printf(TEXT("DeckLeg%d"), Leg), DarkSteel, FVector(FMath::Cos(Angle) * 2550.0f, FMath::Sin(Angle) * 2550.0f, -310.0f),
			FVector(240.0f, 240.0f, 300.0f), 22.5f + Leg * 45.0f, false);
	}

	// Inlaid in the floor (none of it collides; each lies a hair above the one under it): the core's concrete ring with a
	// hazard stripe round its edge and a molten seam inside that.
	AddCylinder(TEXT("CoreInlay"), Concrete, FVector(0.0f, 0.0f, -0.4f), CoreRadius * 2.0f, 1.0f, false);
	AddRing(TEXT("CorePerimeterStripe"), Stripe, FVector(0.0f, 0.0f, 0.8f), CoreRadius - 30.0f, 4.0f);
	AddRing(TEXT("CoreMagmaSeam"), MagmaGlow, FVector(0.0f, 0.0f, 0.9f), CoreRadius - 95.0f, 2.5f);

	// The crucible in the middle, a low two-step altar (its pad only ever gives special balls).
	AddCylinder(TEXT("CrucibleStep1"), Concrete, FVector(0.0f, 0.0f, 0.0f), 760.0f, 18.0f, true);
	AddCylinder(TEXT("CrucibleStep2"), Baffle, FVector(0.0f, 0.0f, 18.0f), 480.0f, 17.0f, true);
	CrucibleRings.Add(AddRing(TEXT("CrucibleRingOuter"), MagmaGlow, FVector(0.0f, 0.0f, 18.8f), 380.0f, 5.0f));
	CrucibleRings.Add(AddRing(TEXT("CrucibleRingInner"), Gold, FVector(0.0f, 0.0f, CrucibleTop + 0.8f), 235.0f, 5.0f));

	// The edge, walked round: a steel kerb with a hazard stripe on it, a yellow guard rail on posts (neither rail nor post
	// collides or casts a shadow), and an invisible fence just outside.
	const TArray<FVector> KerbLine = DeckOutline(35.0f, 0.0f);
	const TArray<FVector> FenceLine = DeckOutline(-15.0f, 0.0f);
	int32 Post = 0;
	for (int32 Side = 0; Side < KerbLine.Num(); ++Side)
	{
		const FVector& From = KerbLine[Side];
		const FVector& To = KerbLine[(Side + 1) % KerbLine.Num()];
		const FVector Mid = (From + To) * 0.5f;
		const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(To.Y - From.Y, To.X - From.X));
		const float Length = FVector::Dist2D(From, To);
		AddBox(FString::Printf(TEXT("Kerb%d"), Side), Baffle, Mid + FVector(0.0f, 0.0f, 17.0f), FVector(Length + 40.0f, 70.0f, 34.0f), Yaw,
			true, true);
		AddBox(FString::Printf(TEXT("KerbStripe%d"), Side), Stripe, Mid + FVector(0.0f, 0.0f, 34.8f), FVector(Length + 10.0f, 28.0f, 1.4f),
			Yaw, false);
		AddBox(FString::Printf(TEXT("Rail%d"), Side), Stripe, Mid + FVector(0.0f, 0.0f, 106.0f), FVector(Length + 8.0f, 8.0f, 8.0f), Yaw, false);
		AddBox(FString::Printf(TEXT("RailLow%d"), Side), DarkSteel, Mid + FVector(0.0f, 0.0f, 70.0f), FVector(Length, 6.0f, 6.0f), Yaw, false);
		const int32 Posts = FMath::Max(2, FMath::RoundToInt(Length / 520.0f));
		for (int32 Index = 0; Index < Posts; ++Index)
		{
			AddCylinder(FString::Printf(TEXT("RailPost%d"), Post++), DarkSteel,
				FMath::Lerp(From, To, static_cast<float>(Index) / Posts) + FVector(0.0f, 0.0f, 34.0f), 10.0f, 72.0f, false);
		}
		const FVector& Out0 = FenceLine[Side];
		const FVector& Out1 = FenceLine[(Side + 1) % FenceLine.Num()];
		const FVector Extend = (Out1 - Out0).GetSafeNormal() * 30.0f;
		AddFence(FString::Printf(TEXT("Fence%d"), Side), Out0 - Extend, Out1 + Extend);
	}
}

void AChaosImpactForgeStage::BuildQuarter(const int32 Quarter)
{
	using namespace ForgeStage;
	const FString Q = FString::Printf(TEXT("_Q%d"), Quarter);
	const float Turned = 90.0f * Quarter;
	const uint8 BayHere = Bay + Quarter;
	const uint8 DarkHere = BayDark + Quarter;
	const uint8 GlowHere = BayGlow + Quarter;
	const uint8 NextBay = Bay + (Quarter + 1) % 4;

	const auto At = [Quarter](const float X, const float Y, const float Z) { return Turn(FVector(X, Y, Z), Quarter); };
	const auto Box = [&](const TCHAR* Name, const uint8 Paint, const FVector& Location, const FVector& Size, const float Yaw,
		const bool bCollision, const bool bChamfer = false)
	{
		return AddBox(FString(Name) + Q, Paint, Turn(Location, Quarter), Size, Yaw + Turned, bCollision, bChamfer);
	};

	// ---- The foundry bay (this quarter's side of the deck): its floor in its colour edged with hazard stripes, its colour
	// glowing along the kerb, a container in the middle for cover and two coolant tanks.
	Box(TEXT("BayFloor"), DarkHere, FVector(2585.0f, 0.0f, 0.3f), FVector(1010.0f, 1800.0f, 1.0f), 0.0f, false);
	Box(TEXT("BayStripeN"), Stripe, FVector(2585.0f, 900.0f, 1.0f), FVector(1010.0f, 26.0f, 1.4f), 0.0f, false);
	Box(TEXT("BayStripeS"), Stripe, FVector(2585.0f, -900.0f, 1.0f), FVector(1010.0f, 26.0f, 1.4f), 0.0f, false);
	Box(TEXT("BayStripeFront"), Stripe, FVector(2080.0f, 0.0f, 1.0f), FVector(26.0f, 1826.0f, 1.4f), 0.0f, false);
	Box(TEXT("BayEdgeGlow"), GlowHere, FVector(DeckHalf - 72.0f, 0.0f, 18.0f), FVector(4.0f, 1800.0f, 12.0f), 0.0f, false);

	AddContainer(TEXT("BayContainer") + Q, BayHere, DarkHere, At(2450.0f, 0.0f, 115.0f), FVector(230.0f, 540.0f, 230.0f), Turned);

	// Coolant tanks to jump on: two in front of the bay, one by the corner chamber.
	const FVector Tanks[] = {FVector(2050.0f, -620.0f, 0.0f), FVector(2050.0f, 620.0f, 0.0f), FVector(2700.0f, 1950.0f, 0.0f)};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Tanks); ++Index)
	{
		const FString TankName = FString::Printf(TEXT("CoolantTank%d"), Index) + Q;
		AddCylinder(TankName, Index == 2 ? NextBay : BayHere, Turn(Tanks[Index], Quarter), 150.0f, 95.0f, true);
		AddCylinder(TankName + TEXT("Cap"), Baffle, Turn(Tanks[Index] + FVector(0.0f, 0.0f, 95.0f), Quarter), 158.0f, 8.0f, false);
		AddRing(TankName + TEXT("Glow"), CoolantGlow, Turn(Tanks[Index] + FVector(0.0f, 0.0f, 58.0f), Quarter), 78.0f, 8.0f);
	}

	// Low slag barriers either side, between the bay and the corners.
	for (const float Side : {-1.0f, 1.0f})
	{
		Box(Side > 0.0f ? TEXT("BarrierN") : TEXT("BarrierS"), Concrete, FVector(2350.0f, Side * 1450.0f, 55.0f),
			FVector(440.0f, 80.0f, 110.0f), 0.0f, true, true);
		Box(Side > 0.0f ? TEXT("BarrierStripeN") : TEXT("BarrierStripeS"), Stripe, FVector(2350.0f, Side * 1450.0f, 110.8f),
			FVector(400.0f, 50.0f, 1.4f), 0.0f, false);
	}

	// ---- The corner cooling chamber (anticlockwise from the bay): a coolant ring on the floor, and a low blast wall with a
	// coolant light along its top between it and the middle.
	const FVector CornerDir = FVector(1.0f, 1.0f, 0.0f).GetSafeNormal();
	AddRing(TEXT("CornerCoolantRing") + Q, CoolantGlow, Turn(CornerDir * ChamberDistance + FVector(0.0f, 0.0f, 1.0f), Quarter), 250.0f, 4.5f);
	Box(TEXT("CornerWall"), Baffle, CornerDir * CornerWallDistance + FVector(0.0f, 0.0f, 70.0f), FVector(60.0f, 760.0f, 140.0f), 45.0f,
		true, true);
	Box(TEXT("CornerWallGlow"), CoolantGlow, CornerDir * CornerWallDistance + FVector(0.0f, 0.0f, 141.0f), FVector(66.0f, 740.0f, 4.0f),
		45.0f, false);

	// ---- A steel baffle on the core's edge, turned 45 deg for bank shots.
	Box(TEXT("CoreBaffleWall"), Baffle, FVector(900.0f, 900.0f, 100.0f), FVector(64.0f, 480.0f, 200.0f), 45.0f, true, true);
	Box(TEXT("CoreBaffleTopGlow"), GlowHere, FVector(900.0f, 900.0f, 202.0f), FVector(72.0f, 484.0f, 6.0f), 45.0f, false);
	Box(TEXT("CoreBaffleMidGlow"), MagmaGlow, FVector(900.0f, 900.0f, 104.0f), FVector(72.0f, 486.0f, 14.0f), 45.0f, false);
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

	// While it is about to erupt: a pale red danger area over the grate that fills toward its edge (the boss's ground
	// warning; not one of the palette's, so it keeps its own colour).
	UStaticMeshComponent* Area = AddPart(SceneRoot, Name + TEXT("WarnArea"), Scenery, PlaneMesh.Get(), At + FVector(0.0f, 0.0f, 4.0f),
		FVector(VentRadius * 2.0f + 90.0f, VentRadius * 2.0f + 90.0f, 100.0f), FRotator::ZeroRotator, false);
	if (WarnMaterial)
	{
		Area->SetMaterial(0, WarnMaterial.Get());
	}
	Area->ComponentTags.Reset();
	Area->SetVisibility(false);
	VentWarnAreas.Add(Area);

	UStaticMeshComponent* EruptBeam = AddCylinder(Name + TEXT("EruptBeam"), MagmaGlow, At, VentRadius * 2.0f, 700.0f, false);
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

	UMaterialInterface* Surface = FlatMaterial.Get();
	if (IsGlowPaint(Paint))
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
	for (UStaticMeshComponent* Area : VentWarnAreas)
	{
		UMaterialInstanceDynamic* Look = Area ? Area->CreateDynamicMaterialInstance(0) : nullptr;
		if (Look)
		{
			Look->SetScalarParameterValue(TEXT("Lane"), 0.0f);
		}
		VentWarnLooks.Add(Look);
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
	// (Muted toward dark steel: the bays' floors only tell the quarters apart, they must not shout.)
	if (Paint >= BayDark) { return BayAt(Paint - BayDark) * 0.09f + FLinearColor(0.035f, 0.035f, 0.04f); }
	if (Paint >= Bay) { return BayAt(Paint - Bay); }
	switch (Paint)
	{
	case SteelFloor: return SteelFloorColor;
	case Concrete: return ConcreteColor;
	case Baffle: return BaffleColor;
	case Stripe: return StripeColor;
	case MagmaGlow: return MagmaGlowColor;
	case MagmaSea: return MagmaSeaColor;
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
		if (IsGlowPaint(Paint))
		{
			Material->SetVectorParameterValue(TEXT("Color"), Color);
			Material->SetScalarParameterValue(TEXT("Intensity"), Paint == MagmaSea ? 1.0f : GlowIntensity);
		}
		else
		{
			Material->SetVectorParameterValue(TEXT("Color"), Color);
			Material->SetVectorParameterValue(TEXT("Base Color"), Color);
			Material->SetVectorParameterValue(TEXT("BaseColor"), Color);
			// (The steel floor's material takes its colour as a tint over its plate pattern.)
			Material->SetVectorParameterValue(TEXT("Tint"), Color);
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
		// It erupts: a column of fire for as long as it blows, the blast at its foot, smoke after, a ring of heat spreading.
		PlayStageEffect(ChaosImpactBallTypes::Effects::Fire, At + FVector(0.0f, 0.0f, 10.0f), FVector(2.4f, 2.4f, 3.2f), VentEruptSeconds);
		PlayStageEffect(ChaosImpactBallTypes::Effects::Explosion, At + FVector(0.0f, 0.0f, 40.0f), FVector(1.3f), 0.0f);
		PlayStageEffect(ChaosImpactBallTypes::Effects::Smoke, At + FVector(0.0f, 0.0f, 20.0f), FVector(1.2f), VentEruptSeconds + 1.2f);
		PlayFloorRing(At, FLinearColor(1.0f, 0.45f, 0.06f), VentRadius * 0.9f, VentRadius * 2.6f, 0.65f, 14.0f, 14.0f);
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
	// The warning: smoke starts to rise from each coming vent and a ring of heat closes in on it.
	if (ActiveVentPair >= 0 && GetVentProgress() < 1.0f && VentWarnFxStartedAt != VentStartedAt)
	{
		VentWarnFxStartedAt = VentStartedAt;
		const float Left = VentWarnSeconds * (1.0f - GetVentProgress());
		for (int32 Vent = 0; Vent < 4; ++Vent)
		{
			if (!IsVentInPair(Vent, ActiveVentPair))
			{
				continue;
			}
			const FVector At = GetActorTransform().TransformPosition(GetVentOffset(Vent));
			PlayStageEffect(ChaosImpactBallTypes::Effects::LastHitSmoke, At + FVector(0.0f, 0.0f, 8.0f), FVector(1.6f), Left);
			PlayFloorRing(At, FLinearColor(1.0f, 0.38f, 0.08f), VentRadius * 2.2f, VentRadius * 1.02f, Left, 10.0f, 7.0f);
		}
	}
	const float Progress = GetVentProgress();
	for (int32 Vent = 0; Vent < VentDiscs.Num(); ++Vent)
	{
		const bool bSelected = IsVentInPair(Vent, ActiveVentPair);
		const float Warn = bSelected && Progress < 1.0f ? Progress : 0.0f;
		const bool bErupt = bSelected && Progress >= 1.0f;
		const float EruptT = bErupt ? Progress - 1.0f : 0.0f;

		// The grate: a slow glow at rest; dimmed while it warns (so the red danger area over it reads); blinding as it erupts.
		const float Rest = 0.7f + 0.3f * FMath::Sin(LookClock * 1.8f + Vent);
		if (UMaterialInstanceDynamic* Look = VentDiscLooks.IsValidIndex(Vent) ? VentDiscLooks[Vent].Get() : nullptr)
		{
			Look->SetScalarParameterValue(TEXT("Intensity"), bErupt ? 14.0f : Warn > 0.0f ? 0.15f : Rest);
		}
		if (UStaticMeshComponent* Ring = VentRings.IsValidIndex(Vent) ? VentRings[Vent].Get() : nullptr)
		{
			const float Scale = (VentRadius / 70.0f) * (1.0f + (Warn > 0.0f ? 0.05f * FMath::Sin(LookClock * 18.0f) : 0.0f));
			Ring->SetRelativeScale3D(FVector(Scale, Scale, (5.0f + Warn * 18.0f) / 13.0f));
		}
		if (UStaticMeshComponent* Area = VentWarnAreas.IsValidIndex(Vent) ? VentWarnAreas[Vent].Get() : nullptr)
		{
			// The danger area: fades in, fills from the middle to the edge as the eruption nears, blinking faster, then
			// flashes white as it erupts and fades with the plume.
			const bool bShown = Warn > 0.0f || (bErupt && EruptT < 1.0f);
			Area->SetVisibility(bShown);
			if (UMaterialInstanceDynamic* Look = bShown && VentWarnLooks.IsValidIndex(Vent) ? VentWarnLooks[Vent].Get() : nullptr)
			{
				const float Blink = 0.5f + 0.5f * FMath::Sin(LookClock * FMath::Lerp(6.0f, 22.0f, Warn * Warn));
				Look->SetScalarParameterValue(TEXT("Progress"), bErupt ? 1.0f : Warn);
				Look->SetScalarParameterValue(TEXT("Flash"), bErupt ? 1.0f - EruptT : 0.12f * Blink * Warn);
				Look->SetScalarParameterValue(TEXT("Opacity"), bErupt ? 1.0f - EruptT : FMath::Clamp(Warn * 8.0f, 0.0f, 1.0f));
				Look->SetScalarParameterValue(TEXT("Intensity"), 1.5f + 0.4f * Blink * Warn);
			}
		}
		if (UStaticMeshComponent* EruptBeam = VentEruptBeams.IsValidIndex(Vent) ? VentEruptBeams[Vent].Get() : nullptr)
		{
			EruptBeam->SetVisibility(bErupt);
			const float Width = (VentRadius * 2.0f / 100.0f) * (EruptT < 0.18f ? 1.15f : 1.0f - 0.65f * FMath::Square((EruptT - 0.18f) / 0.82f));
			// A short plume, see-through, so whoever stands behind it stays in sight.
			EruptBeam->SetRelativeScale3D(FVector(Width, Width, 7.0f * (EruptT < 0.18f ? EruptT / 0.18f : 1.0f)));
			if (UMaterialInstanceDynamic* Look = VentEruptLooks.IsValidIndex(Vent) ? VentEruptLooks[Vent].Get() : nullptr)
			{
				Look->SetScalarParameterValue(TEXT("Intensity"), 4.5f * (1.0f - EruptT) + 1.2f);
			}
		}
		if (UPointLightComponent* Light = VentLights.IsValidIndex(Vent) ? VentLights[Vent].Get() : nullptr)
		{
			// Red while it warns (the danger), the magma's own orange as it erupts.
			Light->SetLightColor(Warn > 0.0f ? FLinearColor(1.0f, 0.25f, 0.2f) : FLinearColor(1.0f, 0.38f, 0.05f));
			Light->SetIntensity(bErupt ? 85000.0f * (1.0f - EruptT) + 7000.0f : Warn * 14000.0f + 1800.0f);
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
