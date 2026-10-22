#include "ChaosImpactTempleStage.h"

#include "ChaosImpact.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactBallSpawner.h"
#include "ChaosImpactBallTypes.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactSfx.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraFunctionLibrary.h"
#include "UObject/ConstructorHelpers.h"

namespace TempleStage
{

	// Stage-relative layout. X is up the screen (the camera looks along +X), Y to the right.
	const FVector2D SideDirections[4] = {{1.0f, 0.0f}, {0.0f, 1.0f}, {-1.0f, 0.0f}, {0.0f, -1.0f}};
	const FVector2D CornerDirections[4] = {{1.0f, 1.0f}, {-1.0f, 1.0f}, {-1.0f, -1.0f}, {1.0f, -1.0f}};

	FVector SideCentre(const int32 Side)
	{
		return FVector(SideDirections[Side] * AChaosImpactTempleStage::SideDistance, 0.0f);
	}


	/** The corner islands stand at (±CornerDistance, ±CornerDistance). */
	FVector Corner(const int32 Index)
	{
		return FVector(CornerDirections[Index].X * AChaosImpactTempleStage::CornerDistance,
			CornerDirections[Index].Y * AChaosImpactTempleStage::CornerDistance, 0.0f);
	}

	float YawOf(const FVector& Direction)
	{
		return FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X));
	}
}

FVector AChaosImpactTempleStage::GetSealOffset(const int32 Seal)
{
	return TempleStage::SideCentre(FMath::Clamp(Seal, 0, 3));
}

FVector AChaosImpactTempleStage::GetPortalFacing(const int32 Portal)
{
	return (-TempleStage::Corner(FMath::Clamp(Portal, 0, 3))).GetSafeNormal2D();
}

FVector AChaosImpactTempleStage::GetPortalOffset(const int32 Portal)
{
	// The gate stands on the outer half of its island, facing the middle.
	return TempleStage::Corner(FMath::Clamp(Portal, 0, 3)) - GetPortalFacing(Portal) * 200.0f;
}

AChaosImpactTempleStage::AChaosImpactTempleStage()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetNetUpdateFrequency(10.0f);
	bAlwaysRelevant = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeMesh(TEXT("/Engine/BasicShapes/Cone.Cone"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Game/LevelPrototyping/Meshes/SM_Cylinder.SM_Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ChamferMesh(TEXT("/Game/LevelPrototyping/Meshes/SM_ChamferCube.SM_ChamferCube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> BandMesh(
		TEXT("/Game/LevelPrototyping/Interactable/JumpPad/Assets/Meshes/SM_CircularBand.SM_CircularBand"));
#define TEMPLE_MATERIAL(Var, Path) static ConstructorHelpers::FObjectFinder<UMaterialInterface> Var(TEXT(Path))
	TEMPLE_MATERIAL(FloorMat, "/Game/ChaosImpact/solo/Stage2/Materials/MI_Floor_Ruins.MI_Floor_Ruins");
	TEMPLE_MATERIAL(BridgeMat, "/Game/ChaosImpact/solo/Stage2/Materials/MI_Floor_Bridge.MI_Floor_Bridge");
	TEMPLE_MATERIAL(PaleMat, "/Game/ChaosImpact/solo/Stage2/Materials/MI_Stone_Pale.MI_Stone_Pale");
	TEMPLE_MATERIAL(SandMat, "/Game/ChaosImpact/solo/Stage2/Materials/MI_Stone_Sand.MI_Stone_Sand");
	TEMPLE_MATERIAL(DarkMat, "/Game/ChaosImpact/solo/Stage2/Materials/MI_Stone_Dark.MI_Stone_Dark");
	TEMPLE_MATERIAL(RuneTealMat, "/Game/ChaosImpact/solo/Stage2/Materials/MI_Rune_Teal.MI_Rune_Teal");
	TEMPLE_MATERIAL(RuneGoldMat, "/Game/ChaosImpact/solo/Stage2/Materials/MI_Rune_Gold.MI_Rune_Gold");
	TEMPLE_MATERIAL(GoldMat, "/Game/ChaosImpact/solo/Stage2/Materials/MI_Gold.MI_Gold");
	TEMPLE_MATERIAL(MossMat, "/Game/ChaosImpact/solo/Stage2/Materials/MI_Moss.MI_Moss");
	TEMPLE_MATERIAL(FieldTealMat, "/Game/ChaosImpact/solo/Stage2/Materials/MI_Field_Teal.MI_Field_Teal");
	TEMPLE_MATERIAL(FieldSoftMat, "/Game/ChaosImpact/solo/Stage2/Materials/MI_Field_TealSoft.MI_Field_TealSoft");
	TEMPLE_MATERIAL(FieldAmberMat, "/Game/ChaosImpact/solo/Stage1/Materials/MI_NeonField_Amber.MI_NeonField_Amber");
	TEMPLE_MATERIAL(NeonAmberMat, "/Game/ChaosImpact/solo/Stage1/Materials/MI_Neon_Amber.MI_Neon_Amber");
	TEMPLE_MATERIAL(CloudMat, "/Game/ChaosImpact/solo/Stage2/Materials/M_CI_CloudSea.M_CI_CloudSea");
	TEMPLE_MATERIAL(PineMat, "/Game/ChaosImpact/solo/Stage14/Materials/MI_Pine.MI_Pine");
	TEMPLE_MATERIAL(BarkMat, "/Game/ChaosImpact/solo/Stage14/Materials/MI_PineBark.MI_PineBark");
#undef TEMPLE_MATERIAL

	FChaosImpactTempleAssets A;
	A.Cube = CubeMesh.Object; A.Cone = ConeMesh.Object; A.Plane = PlaneMesh.Object; A.Cylinder = CylinderMesh.Object;
	A.Chamfer = ChamferMesh.Object; A.Band = BandMesh.Object;
	A.Floor = FloorMat.Object; A.Bridge = BridgeMat.Object; A.Pale = PaleMat.Object; A.Sand = SandMat.Object; A.Dark = DarkMat.Object;
	A.RuneTeal = RuneTealMat.Object; A.RuneGold = RuneGoldMat.Object; A.Gold = GoldMat.Object; A.Moss = MossMat.Object;
	A.FieldTeal = FieldTealMat.Object; A.FieldSoft = FieldSoftMat.Object; A.FieldAmber = FieldAmberMat.Object;
	A.NeonAmber = NeonAmberMat.Object; A.Cloud = CloudMat.Object; A.Pine = PineMat.Object; A.Bark = BarkMat.Object;
	Assets = A;

	// ---- The nine islands and the bridges between them.
	TArray<float> CentreOpenings, CentreWidths;
	for (int32 Side = 0; Side < 4; ++Side)
	{
		CentreOpenings.Add(Side * 90.0f);
		CentreWidths.Add(700.0f);
	}
	BuildIsland(TEXT("Centre"), FVector::ZeroVector, CentreRadius, 2600.0f, CentreOpenings, CentreWidths);
	for (int32 Side = 0; Side < 4; ++Side)
	{
		const FVector Centre = TempleStage::SideCentre(Side);
		const FVector Prev = TempleStage::Corner((Side + 3) % 4);
		const FVector Next = TempleStage::Corner(Side);
		BuildIsland(FString::Printf(TEXT("Side%d"), Side), Centre, SideRadius, 1700.0f,
			{TempleStage::YawOf(-Centre), TempleStage::YawOf(Prev - Centre), TempleStage::YawOf(Next - Centre)}, {700.0f, 420.0f, 420.0f});
		const FVector Inward = -Centre.GetSafeNormal2D();
		BuildBridge(FString::Printf(TEXT("BridgeCentre%d"), Side), Centre.GetSafeNormal2D() * (CentreRadius - 120.0f),
			Centre + Inward * (SideRadius - 120.0f), 700.0f);
	}
	for (int32 Corner = 0; Corner < 4; ++Corner)
	{
		const FVector Centre = TempleStage::Corner(Corner);
		const FVector SideA = TempleStage::SideCentre(Corner);
		const FVector SideB = TempleStage::SideCentre((Corner + 1) % 4);
		BuildIsland(FString::Printf(TEXT("Corner%d"), Corner), Centre, CornerRadius, 1300.0f,
			{TempleStage::YawOf(SideA - Centre), TempleStage::YawOf(SideB - Centre)}, {420.0f, 420.0f});
		for (const FVector& Side : {SideA, SideB})
		{
			const FVector Along = (Centre - Side).GetSafeNormal2D();
			BuildBridge(FString::Printf(TEXT("BridgeCorner%d_%d"), Corner, Side == SideA ? 0 : 1),
				Side + Along * (SideRadius - 110.0f), Centre - Along * (CornerRadius - 110.0f), 420.0f);
		}
	}

	// ---- The temple in the middle: the altar of blessing in a ring of columns.
	AddCylinder(SceneRoot, TEXT("AltarStep1"), A.Pale, FVector(0.0f, 0.0f, 0.0f), 1400.0f, 35.0f);
	AddCylinder(SceneRoot, TEXT("AltarStep2"), A.Bridge, FVector(0.0f, 0.0f, 0.0f), 960.0f, 70.0f);
	AddCylinder(SceneRoot, TEXT("AltarStep3"), A.Pale, FVector(0.0f, 0.0f, 0.0f), 560.0f, 105.0f);
	AltarGlows.Add(AddRing(SceneRoot, TEXT("AltarGold1"), A.RuneGold, FVector(0.0f, 0.0f, 35.5f), 700.0f, 6.0f));
	AltarGlows.Add(AddRing(SceneRoot, TEXT("AltarGold2"), A.RuneGold, FVector(0.0f, 0.0f, 70.5f), 480.0f, 6.0f));
	AltarGlows.Add(AddRing(SceneRoot, TEXT("AltarGold3"), A.RuneGold, FVector(0.0f, 0.0f, 105.5f), 280.0f, 6.0f));
	AddRing(SceneRoot, TEXT("CourtRune"), A.RuneTeal, FVector(0.0f, 0.0f, 0.6f), 1000.0f, 4.0f);
	AddRing(SceneRoot, TEXT("CourtGold"), A.RuneGold, FVector(0.0f, 0.0f, 0.6f), 1080.0f, 3.0f);
	for (int32 Index = 0; Index < 16; ++Index)
	{
		const float Angle = FMath::DegreesToRadians(Index * 22.5f + 11.25f);
		AddBox(SceneRoot, FString::Printf(TEXT("CourtGlyph%d"), Index), A.RuneTeal,
			FVector(FMath::Cos(Angle) * 1040.0f, FMath::Sin(Angle) * 1040.0f, 0.8f), FVector(70.0f, 18.0f, 1.2f),
			FMath::RadiansToDegrees(Angle), false);
	}
	for (int32 Index = 0; Index < 8; ++Index)
	{
		const float Angle = FMath::DegreesToRadians(22.5f + Index * 45.0f);
		BuildColumn(FString::Printf(TEXT("Column%d"), Index), FVector(FMath::Cos(Angle) * 1200.0f, FMath::Sin(Angle) * 1200.0f, 0.0f), 320.0f);
	}

	// ---- The side islands' seals (the pillars of light) and their cover; the corner islands' portal gates.
	for (int32 Side = 0; Side < 4; ++Side)
	{
		BuildSeal(Side);
		const FVector Centre = TempleStage::SideCentre(Side);
		const FVector Out = Centre.GetSafeNormal2D();
		const FVector Across(-Out.Y, Out.X, 0.0f);
		for (const float Sign : {-1.0f, 1.0f})
		{
			// A fallen column lying across the outer half: cover you can jump.
			const FVector At = Centre + Out * 420.0f + Across * Sign * 330.0f;
			AddPart(SceneRoot, FString::Printf(TEXT("Fallen%d_%d"), Side, Sign > 0 ? 1 : 0), Assets.Cylinder, A.Pale,
				At + FVector(0.0f, 0.0f, 55.0f) - Across * 150.0f, FVector(1.1f, 1.1f, 3.0f),
				FRotator(0.0f, TempleStage::YawOf(Across), 90.0f), true);
			AddRing(SceneRoot, FString::Printf(TEXT("FallenBand%d_%d"), Side, Sign > 0 ? 1 : 0), A.RuneTeal, At + FVector(0.0f, 0.0f, 55.0f), 58.0f, 10.0f)
				->SetRelativeRotation(FRotator(0.0f, TempleStage::YawOf(Across), 90.0f));
		}
	}
	for (int32 Portal = 0; Portal < 4; ++Portal)
	{
		BuildPortal(Portal);
	}
	BuildScenery();

	// Where players appear: the outer side of each side island and to one side on each corner island.
	for (int32 Side = 0; Side < 4; ++Side)
	{
		const FVector Out = TempleStage::SideCentre(Side).GetSafeNormal2D();
		SpawnPointOffsets.Add(TempleStage::SideCentre(Side) + Out * 600.0f);
	}
	for (int32 Corner = 0; Corner < 4; ++Corner)
	{
		const FVector In = GetPortalFacing(Corner);
		const FVector Across(-In.Y, In.X, 0.0f);
		SpawnPointOffsets.Add(TempleStage::Corner(Corner) + In * 120.0f + Across * 280.0f);
	}
	BallPointOffsets.Add(FVector(0.0f, 0.0f, 105.0f));
	for (int32 Side = 0; Side < 4; ++Side)
	{
		BallPointOffsets.Add(TempleStage::SideCentre(Side) - TempleStage::SideCentre(Side).GetSafeNormal2D() * 540.0f);
	}
	for (int32 Corner = 0; Corner < 4; ++Corner)
	{
		BallPointOffsets.Add(TempleStage::Corner(Corner) + GetPortalFacing(Corner) * 260.0f);
	}
}

UStaticMeshComponent* AChaosImpactTempleStage::AddPart(USceneComponent* Parent, const FString& Name, UStaticMesh* Mesh,
	UMaterialInterface* Material, const FVector& Location, const FVector& Scale, const FRotator& Rotation, const bool bCollision,
	const bool bShadow)
{
	UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(FName(*Name));
	Part->SetupAttachment(Parent);
	Part->SetStaticMesh(Mesh);
	if (Material)
	{
		Part->SetMaterial(0, Material);
	}
	Part->SetRelativeLocation(Location);
	Part->SetRelativeRotation(Rotation);
	Part->SetRelativeScale3D(Scale);
	Part->SetCollisionEnabled(bCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	if (bCollision)
	{
		Part->SetCollisionObjectType(ECC_WorldStatic);
		Part->SetCollisionResponseToAllChannels(ECR_Block);
	}
	Part->SetGenerateOverlapEvents(false);
	Part->SetCanEverAffectNavigation(false);
	Part->SetCastShadow(bShadow);
	return Part;
}

UStaticMeshComponent* AChaosImpactTempleStage::AddBox(USceneComponent* Parent, const FString& Name, UMaterialInterface* Material,
	const FVector& Location, const FVector& Size, const float Yaw, const bool bCollision, const bool bChamfer)
{
	// Both meshes are a metre each way about their middle.
	return AddPart(Parent, Name, bChamfer ? Assets.Chamfer : Assets.Cube, Material, Location, Size / 100.0f,
		FRotator(0.0f, Yaw, 0.0f), bCollision);
}

UStaticMeshComponent* AChaosImpactTempleStage::AddCylinder(USceneComponent* Parent, const FString& Name, UMaterialInterface* Material,
	const FVector& Location, const float Diameter, const float Height, const bool bCollision)
{
	// SM_Cylinder is 100 across with its base at 0.
	return AddPart(Parent, Name, Assets.Cylinder, Material, Location, FVector(Diameter / 100.0f, Diameter / 100.0f, Height / 100.0f),
		FRotator::ZeroRotator, bCollision);
}

UStaticMeshComponent* AChaosImpactTempleStage::AddRing(USceneComponent* Parent, const FString& Name, UMaterialInterface* Material,
	const FVector& Location, const float Radius, const float Thickness)
{
	// SM_CircularBand is a band of radius 70 and about 13 high, centred on its pivot.
	return AddPart(Parent, Name, Assets.Band, Material, Location, FVector(Radius / 70.0f, Radius / 70.0f, Thickness / 13.0f),
		FRotator::ZeroRotator, false, false);
}

void AChaosImpactTempleStage::AddFence(const FString& Name, const FVector& From, const FVector& To)
{
	const FVector Span = To - From;
	UStaticMeshComponent* Fence = AddPart(SceneRoot, Name, Assets.Cube, nullptr, (From + To) * 0.5f + FVector(0.0f, 0.0f, 300.0f),
		FVector(Span.Size2D() / 100.0f, 0.4f, 6.0f), FRotator(0.0f, TempleStage::YawOf(Span), 0.0f), false, false);
	// Stops characters only: balls fly on over the edge, and floor traces (which look for the world's static
	// geometry) never meet it.
	Fence->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Fence->SetCollisionObjectType(ECC_Destructible);
	Fence->SetCollisionResponseToAllChannels(ECR_Ignore);
	Fence->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	Fence->SetVisibility(false);
	Fence->SetHiddenInGame(true);
	Fence->ComponentTags.Add(TEXT("TempleFence"));
}

void AChaosImpactTempleStage::BuildIsland(const FString& Name, const FVector& Centre, const float Radius, const float Depth,
	const TArray<float>& OpeningsYaw, const TArray<float>& OpeningWidths)
{
	const FChaosImpactTempleAssets& A = Assets;
	AddCylinder(SceneRoot, Name + TEXT("Top"), A.Floor, Centre - FVector(0.0f, 0.0f, 40.0f), Radius * 2.0f, 40.0f);
	AddCylinder(SceneRoot, Name + TEXT("Lip"), A.Pale, Centre - FVector(0.0f, 0.0f, 115.0f), Radius * 2.0f + 70.0f, 78.0f, false);
	// The rock it floats on, tapering away into the clouds (the engine cone points up: turned over).
	AddPart(SceneRoot, Name + TEXT("Rock"), A.Cone, A.Sand, Centre - FVector(0.0f, 0.0f, 115.0f + Depth * 0.5f),
		FVector(Radius * 2.02f / 100.0f, Radius * 2.02f / 100.0f, Depth / 100.0f), FRotator(180.0f, 0.0f, 0.0f), false);
	AddPart(SceneRoot, Name + TEXT("RockDark"), A.Cone, A.Dark, Centre - FVector(0.0f, 0.0f, 200.0f + Depth * 0.62f),
		FVector(Radius * 1.4f / 100.0f, Radius * 1.4f / 100.0f, Depth * 1.1f / 100.0f), FRotator(180.0f, 37.0f, 0.0f), false);
	FRandomStream Random(GetTypeHash(Name));
	for (int32 Index = 0; Index < 5; ++Index)
	{
		const float Angle = Random.FRandRange(0.0f, UE_TWO_PI);
		const float Size = Random.FRandRange(220.0f, 420.0f);
		AddPart(SceneRoot, FString::Printf(TEXT("%sHang%d"), *Name, Index), A.Chamfer, Index % 2 ? A.Dark : A.Sand,
			Centre + FVector(FMath::Cos(Angle) * Radius * 0.72f, FMath::Sin(Angle) * Radius * 0.72f, -260.0f - Random.FRandRange(0.0f, 300.0f)),
			FVector(Size, Size * 0.8f, Size * 1.4f) / 100.0f, FRotator(Random.FRandRange(-20.0f, 20.0f), Random.FRandRange(0.0f, 90.0f), 0.0f), false);
	}

	// The rim: low kerb stones and moss round the edge (not across the bridges), and invisible fences above them.
	const auto IsOpen = [&](const float YawDegrees, const float Margin)
	{
		for (int32 Index = 0; Index < OpeningsYaw.Num(); ++Index)
		{
			const float Half = FMath::RadiansToDegrees(FMath::Atan2(OpeningWidths[Index] * 0.5f + Margin, Radius));
			if (FMath::Abs(FMath::FindDeltaAngleDegrees(YawDegrees, OpeningsYaw[Index])) < Half)
			{
				return true;
			}
		}
		return false;
	};
	const int32 Kerbs = FMath::Max(10, FMath::RoundToInt(UE_TWO_PI * Radius / 230.0f));
	for (int32 Index = 0; Index < Kerbs; ++Index)
	{
		const float Yaw = Index * 360.0f / Kerbs;
		if (IsOpen(Yaw, 40.0f))
		{
			continue;
		}
		const FVector Out(FMath::Cos(FMath::DegreesToRadians(Yaw)), FMath::Sin(FMath::DegreesToRadians(Yaw)), 0.0f);
		AddBox(SceneRoot, FString::Printf(TEXT("%sKerb%d"), *Name, Index), Index % 3 ? A.Pale : A.Sand,
			Centre + Out * (Radius - 35.0f) + FVector(0.0f, 0.0f, 20.0f), FVector(70.0f, 200.0f, 40.0f), Yaw, true, true);
		if (Index % 4 == 1)
		{
			AddPart(SceneRoot, FString::Printf(TEXT("%sMoss%d"), *Name, Index), Assets.Cylinder, A.Moss,
				Centre + Out * (Radius - 120.0f) + FVector(0.0f, 0.0f, 0.4f), FVector(1.6f, 2.4f, 0.02f), FRotator(0.0f, Yaw, 0.0f), false, false);
		}
	}
	const int32 Segments = FMath::Max(12, FMath::RoundToInt(UE_TWO_PI * Radius / 200.0f));
	for (int32 Index = 0; Index < Segments; ++Index)
	{
		const float YawA = Index * 360.0f / Segments;
		const float YawB = (Index + 1) * 360.0f / Segments;
		if (IsOpen((YawA + YawB) * 0.5f, -10.0f))
		{
			continue;
		}
		const auto At = [&](const float Yaw)
		{ return Centre + FVector(FMath::Cos(FMath::DegreesToRadians(Yaw)), FMath::Sin(FMath::DegreesToRadians(Yaw)), 0.0f) * (Radius + 15.0f); };
		const FVector From = At(YawA), To = At(YawB);
		const FVector Extend = (To - From).GetSafeNormal() * 30.0f;
		AddFence(FString::Printf(TEXT("%sFence%d"), *Name, Index), From - Extend, To + Extend);
	}
}

void AChaosImpactTempleStage::BuildBridge(const FString& Name, const FVector& From, const FVector& To, const float Width)
{
	const FChaosImpactTempleAssets& A = Assets;
	const FVector Span = To - From;
	const float Length = Span.Size2D();
	const float Yaw = TempleStage::YawOf(Span);
	const FVector Across = FVector(-Span.Y, Span.X, 0.0f).GetSafeNormal();
	const FVector Middle = (From + To) * 0.5f;
	AddBox(SceneRoot, Name + TEXT("Deck"), A.Bridge, Middle - FVector(0.0f, 0.0f, 20.0f), FVector(Length, Width, 40.0f), Yaw);
	AddBox(SceneRoot, Name + TEXT("Under"), A.Sand, Middle - FVector(0.0f, 0.0f, 100.0f), FVector(Length, Width * 0.8f, 120.0f), Yaw, false);
	// An arch under the middle, so it reads as built rather than floating.
	AddPart(SceneRoot, Name + TEXT("Arch"), A.Cone, A.Sand, Middle - FVector(0.0f, 0.0f, 330.0f),
		FVector(Width * 0.9f / 100.0f, Width * 0.9f / 100.0f, 4.4f), FRotator(180.0f, Yaw, 0.0f), false);
	for (const float Sign : {-1.0f, 1.0f})
	{
		const FVector Edge = Across * Sign * (Width * 0.5f - 25.0f);
		AddBox(SceneRoot, FString::Printf(TEXT("%sRail%d"), *Name, Sign > 0 ? 1 : 0), A.Pale, Middle + Edge + FVector(0.0f, 0.0f, 22.0f),
			FVector(Length, 40.0f, 44.0f), Yaw, true, true);
		for (const float Along : {-0.5f, 0.5f})
		{
			const FVector Post = Middle + Span * Along * 0.92f + Edge;
			AddBox(SceneRoot, FString::Printf(TEXT("%sPost%d%d"), *Name, Sign > 0 ? 1 : 0, Along > 0 ? 1 : 0), A.Pale,
				Post + FVector(0.0f, 0.0f, 70.0f), FVector(70.0f, 70.0f, 140.0f), Yaw, true, true);
			AddBox(SceneRoot, FString::Printf(TEXT("%sLamp%d%d"), *Name, Sign > 0 ? 1 : 0, Along > 0 ? 1 : 0), A.RuneTeal,
				Post + FVector(0.0f, 0.0f, 152.0f), FVector(34.0f, 34.0f, 24.0f), Yaw + 45.0f, false);
		}
		// Invisible fences along both sides, reaching onto the islands to close the gap at each end.
		const FVector Out = Across * Sign * (Width * 0.5f + 15.0f);
		AddFence(FString::Printf(TEXT("%sFence%d"), *Name, Sign > 0 ? 1 : 0), From - Span.GetSafeNormal2D() * 140.0f + Out,
			To + Span.GetSafeNormal2D() * 140.0f + Out);
	}
}

void AChaosImpactTempleStage::BuildColumn(const FString& Name, const FVector& At, const float Height)
{
	const FChaosImpactTempleAssets& A = Assets;
	AddBox(SceneRoot, Name + TEXT("Plinth"), A.Sand, At + FVector(0.0f, 0.0f, 22.0f), FVector(210.0f, 210.0f, 44.0f), 0.0f, true, true);
	AddCylinder(SceneRoot, Name + TEXT("Shaft"), A.Pale, At + FVector(0.0f, 0.0f, 44.0f), 130.0f, Height);
	AddRing(SceneRoot, Name + TEXT("Rune"), A.RuneTeal, At + FVector(0.0f, 0.0f, 120.0f), 69.0f, 12.0f);
	AddRing(SceneRoot, Name + TEXT("Gilt"), A.Gold, At + FVector(0.0f, 0.0f, 44.0f + Height - 30.0f), 70.0f, 14.0f);
	AddBox(SceneRoot, Name + TEXT("Capital"), A.Sand, At + FVector(0.0f, 0.0f, 44.0f + Height + 20.0f), FVector(200.0f, 200.0f, 40.0f), 0.0f,
		true, true);
}

void AChaosImpactTempleStage::BuildSeal(const int32 Seal)
{
	const FChaosImpactTempleAssets& A = Assets;
	const FVector At = GetSealOffset(Seal);
	const FString Name = FString::Printf(TEXT("Seal%d"), Seal);
	SealRings.Add(AddRing(SceneRoot, Name + TEXT("Outer"), A.RuneGold, At + FVector(0.0f, 0.0f, 0.6f), SealRadius, 5.0f));
	AddRing(SceneRoot, Name + TEXT("Inner"), A.RuneTeal, At + FVector(0.0f, 0.0f, 0.7f), SealRadius * 0.82f, 4.0f);
	for (int32 Index = 0; Index < 8; ++Index)
	{
		const float Yaw = Index * 45.0f;
		const FVector Out(FMath::Cos(FMath::DegreesToRadians(Yaw)), FMath::Sin(FMath::DegreesToRadians(Yaw)), 0.0f);
		AddBox(SceneRoot, FString::Printf(TEXT("%sGlyph%d"), *Name, Index), A.RuneTeal, At + Out * SealRadius * 0.9f + FVector(0.0f, 0.0f, 0.9f),
			FVector(60.0f, 16.0f, 1.2f), Yaw + 90.0f, false);
		if (Index % 2 == 0)
		{
			// Waist-high marker stones round the seal, so its edge can be seen from anywhere.
			AddBox(SceneRoot, FString::Printf(TEXT("%sStone%d"), *Name, Index), A.Pale, At + Out * (SealRadius + 70.0f) + FVector(0.0f, 0.0f, 30.0f),
				FVector(50.0f, 50.0f, 60.0f), Yaw + 45.0f, false, true);
			AddBox(SceneRoot, FString::Printf(TEXT("%sStoneRune%d"), *Name, Index), A.RuneGold, At + Out * (SealRadius + 70.0f) + FVector(0.0f, 0.0f, 62.0f),
				FVector(30.0f, 30.0f, 6.0f), Yaw + 45.0f, false);
		}
	}
	UStaticMeshComponent* Disc = AddCylinder(SceneRoot, Name + TEXT("Disc"), A.FieldSoft, At + FVector(0.0f, 0.0f, 1.0f), SealRadius * 2.0f, 2.0f, false);
	Disc->SetCastShadow(false);
	SealDiscs.Add(Disc);
	UStaticMeshComponent* Beam = AddCylinder(SceneRoot, Name + TEXT("Pillar"), A.FieldTeal, At, SealRadius * 2.0f, 3400.0f, false);
	Beam->SetCastShadow(false);
	Beam->SetVisibility(false);
	PillarBeams.Add(Beam);
	UStaticMeshComponent* Warn = AddCylinder(SceneRoot, Name + TEXT("WarnBeam"), A.FieldSoft, At, 90.0f, 3400.0f, false);
	Warn->SetCastShadow(false);
	Warn->SetVisibility(false);
	PillarWarnBeams.Add(Warn);
	UPointLightComponent* Light = CreateDefaultSubobject<UPointLightComponent>(FName(*(Name + TEXT("Light"))));
	Light->SetupAttachment(SceneRoot);
	Light->SetRelativeLocation(At + FVector(0.0f, 0.0f, 260.0f));
	Light->SetLightColor(FLinearColor(0.35f, 1.0f, 0.9f));
	Light->SetIntensity(0.0f);
	Light->SetAttenuationRadius(1400.0f);
	Light->SetCastShadows(false);
	SealLights.Add(Light);
}

void AChaosImpactTempleStage::BuildPortal(const int32 Portal)
{
	const FChaosImpactTempleAssets& A = Assets;
	const FVector At = GetPortalOffset(Portal);
	const FVector Facing = GetPortalFacing(Portal);
	const FVector Across(-Facing.Y, Facing.X, 0.0f);
	const float Yaw = TempleStage::YawOf(Facing);
	const bool bAmber = Portal % 2 == 1;
	const FString Name = FString::Printf(TEXT("Portal%d"), Portal);
	for (const float Sign : {-1.0f, 1.0f})
	{
		const FVector Foot = At + Across * Sign * 290.0f;
		AddBox(SceneRoot, FString::Printf(TEXT("%sPillar%d"), *Name, Sign > 0 ? 1 : 0), A.Pale, Foot + FVector(0.0f, 0.0f, 230.0f),
			FVector(120.0f, 120.0f, 460.0f), Yaw, true, true);
		AddBox(SceneRoot, FString::Printf(TEXT("%sBase%d"), *Name, Sign > 0 ? 1 : 0), A.Sand, Foot + FVector(0.0f, 0.0f, 25.0f),
			FVector(170.0f, 170.0f, 50.0f), Yaw, true, true);
		AddBox(SceneRoot, FString::Printf(TEXT("%sRune%d"), *Name, Sign > 0 ? 1 : 0), bAmber ? A.NeonAmber : A.RuneTeal,
			Foot + Facing * 62.0f + FVector(0.0f, 0.0f, 260.0f), FVector(6.0f, 40.0f, 260.0f), Yaw, false);
	}
	AddBox(SceneRoot, Name + TEXT("Lintel"), A.Sand, At + FVector(0.0f, 0.0f, 505.0f), FVector(150.0f, 760.0f, 90.0f), Yaw, true, true);
	AddBox(SceneRoot, Name + TEXT("Gilt"), A.Gold, At + Facing * 76.0f + FVector(0.0f, 0.0f, 505.0f), FVector(8.0f, 700.0f, 30.0f), Yaw, false);
	// The opening: a disc of light filling the gate, and a band turning round its rim.
	UStaticMeshComponent* Disc = AddPart(SceneRoot, Name + TEXT("Field"), A.Cylinder, bAmber ? A.FieldAmber : A.FieldTeal,
		At + FVector(0.0f, 0.0f, 230.0f) - Facing * 4.0f, FVector(4.6f, 4.6f, 0.08f), FRotator(90.0f, Yaw, 0.0f), false, false);
	Disc->SetRelativeLocation(At + FVector(0.0f, 0.0f, 230.0f) - Facing * 4.0f + FVector(0.0f, 0.0f, 0.0f));
	USceneComponent* Swirl = CreateDefaultSubobject<USceneComponent>(FName(*(Name + TEXT("SwirlRoot"))));
	Swirl->SetupAttachment(SceneRoot);
	Swirl->SetRelativeLocation(At + FVector(0.0f, 0.0f, 230.0f) + Facing * 6.0f);
	Swirl->SetRelativeRotation(FRotator(0.0f, Yaw, 0.0f));
	PortalSwirls.Add(Swirl);
	AddPart(Swirl, Name + TEXT("Band"), A.Band, bAmber ? A.NeonAmber : A.RuneTeal, FVector::ZeroVector, FVector(3.4f, 3.4f, 1.2f),
		FRotator(90.0f, 0.0f, 0.0f), false, false);
	AddPart(Swirl, Name + TEXT("BandInner"), A.Band, A.RuneGold, FVector::ZeroVector, FVector(2.5f, 2.5f, 0.8f),
		FRotator(90.0f, 0.0f, 45.0f), false, false);
	// A threshold step in front, so the way in is plain.
	AddBox(SceneRoot, Name + TEXT("Threshold"), bAmber ? A.NeonAmber : A.RuneTeal, At + Facing * 120.0f + FVector(0.0f, 0.0f, 0.8f),
		FVector(40.0f, 520.0f, 1.2f), Yaw, false);
	UPointLightComponent* Light = CreateDefaultSubobject<UPointLightComponent>(FName(*(Name + TEXT("Light"))));
	Light->SetupAttachment(SceneRoot);
	Light->SetRelativeLocation(At + Facing * 160.0f + FVector(0.0f, 0.0f, 240.0f));
	Light->SetLightColor(bAmber ? FLinearColor(1.0f, 0.55f, 0.12f) : FLinearColor(0.2f, 1.0f, 0.85f));
	Light->SetIntensity(9000.0f);
	Light->SetAttenuationRadius(900.0f);
	Light->SetCastShadows(false);
}

void AChaosImpactTempleStage::BuildScenery()
{
	const FChaosImpactTempleAssets& A = Assets;
	// The sea of clouds far below.
	AddPart(SceneRoot, TEXT("CloudSea"), A.Plane, A.Cloud, FVector(0.0f, 0.0f, -2800.0f), FVector(900.0f, 900.0f, 1.0f),
		FRotator::ZeroRotator, false, false);
	// The great ring behind the temple (beyond the north edge), turning slowly.
	RingRoot = CreateDefaultSubobject<USceneComponent>(TEXT("GreatRing"));
	RingRoot->SetupAttachment(SceneRoot);
	RingRoot->SetRelativeLocation(FVector(6600.0f, 0.0f, 1300.0f));
	constexpr int32 Stones = 28;
	constexpr float RingRadius = 1900.0f;
	for (int32 Index = 0; Index < Stones; ++Index)
	{
		const float Angle = Index * UE_TWO_PI / Stones;
		const FVector At(0.0f, FMath::Cos(Angle) * RingRadius, FMath::Sin(Angle) * RingRadius);
		const FRotator Turn(0.0f, 0.0f, FMath::RadiansToDegrees(Angle));
		AddPart(RingRoot, FString::Printf(TEXT("RingStone%d"), Index), A.Chamfer, Index % 2 ? A.Pale : A.Sand, At,
			FVector(3.2f, 2.6f, 4.4f), Turn, false);
		if (Index % 2 == 0)
		{
			AddPart(RingRoot, FString::Printf(TEXT("RingRune%d"), Index), A.Cube, A.RuneTeal, At - FVector(170.0f, 0.0f, 0.0f) * 1.0f,
				FVector(0.06f, 0.5f, 0.5f), Turn, false, false);
		}
	}
	AddPart(RingRoot, TEXT("RingVeil"), A.Cylinder, A.FieldSoft, FVector(0.0f, 0.0f, 0.0f), FVector(33.0f, 33.0f, 0.05f),
		FRotator(90.0f, 0.0f, 0.0f), false, false)->SetRelativeLocation(FVector(0.0f, 0.0f, 0.0f));
	// The ring's own island under it, and drifting islets round the temple with pines and broken columns.
	AddPart(SceneRoot, TEXT("RingIslandTop"), A.Cylinder, A.Floor, FVector(6600.0f, 0.0f, -700.0f), FVector(26.0f, 26.0f, 0.5f), FRotator::ZeroRotator, false);
	AddPart(SceneRoot, TEXT("RingIslandRock"), A.Cone, A.Sand, FVector(6600.0f, 0.0f, -2100.0f), FVector(27.0f, 27.0f, 28.0f), FRotator(180.0f, 0.0f, 0.0f), false);
	const FVector Islets[] = {{5200.0f, 5600.0f, -300.0f}, {5400.0f, -5800.0f, -100.0f}, {1200.0f, 6600.0f, -800.0f}, {1500.0f, -6800.0f, -600.0f},
		{-3800.0f, 6400.0f, -1300.0f}, {-3600.0f, -6200.0f, -1200.0f}, {8800.0f, 3200.0f, 400.0f}, {9200.0f, -3600.0f, 200.0f}};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Islets); ++Index)
	{
		USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(FName(*FString::Printf(TEXT("Islet%d"), Index)));
		Root->SetupAttachment(SceneRoot);
		Root->SetRelativeLocation(Islets[Index]);
		IsletRoots.Add(Root);
		const float Size = 6.0f + (Index % 3) * 3.0f;
		AddPart(Root, FString::Printf(TEXT("Islet%dTop"), Index), A.Cylinder, Index % 2 ? A.Moss : A.Floor, FVector(0.0f, 0.0f, -40.0f),
			FVector(Size, Size, 0.4f), FRotator::ZeroRotator, false);
		AddPart(Root, FString::Printf(TEXT("Islet%dRock"), Index), A.Cone, A.Sand, FVector(0.0f, 0.0f, -40.0f - Size * 70.0f),
			FVector(Size * 1.02f, Size * 1.02f, Size * 1.4f), FRotator(180.0f, Index * 23.0f, 0.0f), false);
		for (int32 Tree = 0; Tree < 2 + Index % 2; ++Tree)
		{
			const float Angle = Tree * 2.3f + Index;
			const FVector Foot(FMath::Cos(Angle) * Size * 24.0f, FMath::Sin(Angle) * Size * 24.0f, 0.0f);
			AddPart(Root, FString::Printf(TEXT("Islet%dTrunk%d"), Index, Tree), A.Cylinder, A.Bark, Foot, FVector(0.4f, 0.4f, 1.6f),
				FRotator::ZeroRotator, false);
			for (int32 Tier = 0; Tier < 3; ++Tier)
			{
				AddPart(Root, FString::Printf(TEXT("Islet%dPine%d_%d"), Index, Tree, Tier), A.Cone, A.Pine,
					Foot + FVector(0.0f, 0.0f, 260.0f + Tier * 150.0f), FVector(3.2f - Tier * 0.8f, 3.2f - Tier * 0.8f, 2.6f - Tier * 0.4f),
					FRotator(0.0f, Tier * 20.0f, 0.0f), false);
			}
		}
		if (Index % 2 == 0)
		{
			AddPart(Root, FString::Printf(TEXT("Islet%dColumn"), Index), A.Cylinder, A.Pale, FVector(-Size * 18.0f, Size * 10.0f, 0.0f),
				FVector(1.2f, 1.2f, 3.0f + Index % 3), FRotator(0.0f, 0.0f, Index % 4 == 0 ? 12.0f : 0.0f), false);
		}
	}
}

void AChaosImpactTempleStage::BeginPlay()
{
	Super::BeginPlay();
	// Fade-able looks for the seals and their pillars (each machine its own).
	for (UStaticMeshComponent* Disc : SealDiscs)
	{
		SealDiscLooks.Add(Disc ? Disc->CreateDynamicMaterialInstance(0) : nullptr);
	}
	for (UStaticMeshComponent* Beam : PillarBeams)
	{
		PillarLooks.Add(Beam ? Beam->CreateDynamicMaterialInstance(0) : nullptr);
	}
	for (UStaticMeshComponent* Beam : PillarWarnBeams)
	{
		WarnLooks.Add(Beam ? Beam->CreateDynamicMaterialInstance(0) : nullptr);
	}
	UpdatePillarLook(0.0f);
}

void AChaosImpactTempleStage::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AChaosImpactTempleStage, PillarSeal);
	DOREPLIFETIME(AChaosImpactTempleStage, PillarStartedAt);
}

TArray<FVector> AChaosImpactTempleStage::GetSpawnPoints() const
{
	return ToWorld(SpawnPointOffsets);
}

TArray<FVector> AChaosImpactTempleStage::GetBallPoints() const
{
	return ToWorld(BallPointOffsets);
}

bool AChaosImpactTempleStage::IsOutside(const FVector& Location) const
{
	if (Super::IsOutside(Location))
	{
		return true;
	}
	// Off every island and bridge, well below the floors: fallen into the clouds.
	const FVector Local = GetActorTransform().InverseTransformPositionNoScale(Location);
	return Local.Z < -400.0f;
}

double AChaosImpactTempleStage::GetServerNow() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0;
	}
	const AGameStateBase* State = World->GetGameState();
	return State ? State->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
}

bool AChaosImpactTempleStage::IsMatchOn() const
{
	const AChaosImpactGameState* Match = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	return Match && Match->bVersusMatch && Match->Phase == EChaosImpactOnlinePhase::Match;
}

float AChaosImpactTempleStage::GetPillarProgress() const
{
	if (PillarSeal < 0)
	{
		return 0.0f;
	}
	const float Age = static_cast<float>(GetServerNow() - PillarStartedAt);
	return Age < PillarWarnSeconds ? FMath::Clamp(Age / PillarWarnSeconds, 0.0f, 1.0f)
		: 1.0f + FMath::Clamp((Age - PillarWarnSeconds) / PillarStrikeSeconds, 0.0f, 1.0f);
}

void AChaosImpactTempleStage::StartPillar(const int32 Seal)
{
	if (!HasAuthority())
	{
		return;
	}
	PillarSeal = FMath::Clamp(Seal, 0, 3);
	PillarStartedAt = static_cast<float>(GetServerNow());
	bPillarStruck = false;
	PillarVictims.Reset();
	ForceNetUpdate();
	OnRep_Pillar();
	UE_LOG(LogChaosImpact, Log, TEXT("Temple: a pillar of light is coming down on seal %d"), PillarSeal);
}

void AChaosImpactTempleStage::OnRep_Pillar()
{
	if (PillarSeal >= 0 && GetNetMode() != NM_DedicatedServer)
	{
		// The warning: a rising hum over the seal.
		ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::NovaCharge, GetActorTransform().TransformPosition(GetSealOffset(PillarSeal)), 0.55f, 1.4f);
	}
}

void AChaosImpactTempleStage::StrikePillar(const int32 Seal)
{
	bPillarStruck = true;
	MulticastPillarStrike(Seal);
}

void AChaosImpactTempleStage::MulticastPillarStrike_Implementation(const int32 Seal)
{
	StrikeFlash = 1.0f;
	LastStrikeShown = Seal;
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const FVector At = GetActorTransform().TransformPosition(GetSealOffset(Seal));
	ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::ThunderBurst, At, 1.0f, 0.8f);
	ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::BeamStrike, At, 0.9f, 0.7f);
	if (UNiagaraSystem* Burst = ChaosImpactBallTypes::LoadEffect(ChaosImpactBallTypes::Effects::WarpIn))
	{
		if (UNiagaraComponent* Effect = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, Burst, At + FVector(0.0f, 0.0f, 20.0f),
			FRotator::ZeroRotator, FVector(3.2f)))
		{
			ChaosImpactBallTypes::SetEffectColor(Effect, TEXT("Base Color"), FLinearColor(0.4f, 1.0f, 0.9f));
		}
	}
	if (UNiagaraSystem* Sparks = ChaosImpactBallTypes::LoadEffect(ChaosImpactBallTypes::Effects::SparkBurst))
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, Sparks, At + FVector(0.0f, 0.0f, 60.0f), FRotator::ZeroRotator, FVector(2.6f));
	}
}

void AChaosImpactTempleStage::TickPillarServer(const double Now)
{
	if (!IsMatchOn())
	{
		NextPillarAt = -1.0;
		if (PillarSeal >= 0)
		{
			PillarSeal = -1;
			ForceNetUpdate();
		}
		return;
	}
	if (NextPillarAt < 0.0)
	{
		NextPillarAt = Now + FirstPillarAfter;
	}
	if (PillarSeal < 0)
	{
		if (Now >= NextPillarAt)
		{
			int32 Seal = FMath::RandRange(0, 3);
			if (Seal == LastStrikeShown)
			{
				Seal = (Seal + FMath::RandRange(1, 3)) % 4;
			}
			StartPillar(Seal);
		}
		return;
	}
	const float Age = static_cast<float>(Now - PillarStartedAt);
	if (Age >= PillarWarnSeconds && !bPillarStruck)
	{
		StrikePillar(PillarSeal);
	}
	if (bPillarStruck && Age < PillarWarnSeconds + PillarStrikeSeconds)
	{
		// Whoever is in the light now (once each); a dash goes through untouched.
		const FVector At = GetActorTransform().TransformPosition(GetSealOffset(PillarSeal));
		for (TActorIterator<AChaosImpactCharacter> It(GetWorld()); It; ++It)
		{
			AChaosImpactCharacter* Character = *It;
			const FVector Delta = Character->GetActorLocation() - At;
			if (Character->IsEliminated() || PillarVictims.Contains(Character) || Delta.Size2D() > SealRadius + 30.0f
				|| Delta.Z < -100.0f || Delta.Z > 500.0f)
			{
				continue;
			}
			const float Applied = Character->TakeDamage(PillarDamage, FDamageEvent(), nullptr, this);
			if (Applied > 0.0f)
			{
				PillarVictims.Add(Character);
				if (!Character->IsEliminated())
				{
					Character->LaunchCharacter(FVector(Delta.X, Delta.Y, 0.0f).GetSafeNormal() * 350.0f + FVector(0.0f, 0.0f, 760.0f), true, true);
				}
				UE_LOG(LogChaosImpact, Log, TEXT("Temple: the pillar struck %s"), *Character->GetName());
			}
		}
	}
	if (Age >= PillarWarnSeconds + PillarStrikeSeconds)
	{
		PillarSeal = -1;
		ForceNetUpdate();
		NextPillarAt = Now + FMath::FRandRange(PillarIntervalMin, FMath::Max(PillarIntervalMin, PillarIntervalMax));
	}
}

void AChaosImpactTempleStage::UpdatePillarLook(const float DeltaSeconds)
{
	const float Progress = GetPillarProgress();
	StrikeFlash = FMath::Max(0.0f, StrikeFlash - DeltaSeconds * 2.5f);
	for (int32 Seal = 0; Seal < SealDiscs.Num(); ++Seal)
	{
		const bool bThis = Seal == PillarSeal;
		const float Warn = bThis && Progress < 1.0f ? Progress : 0.0f;
		const bool bStrike = bThis && Progress >= 1.0f;
		const float StrikeT = bStrike ? Progress - 1.0f : 0.0f;
		// The seal: a slow breath at rest; a quickening flicker while it warns; blinding while it strikes.
		const float Rest = 0.6f + 0.25f * FMath::Sin(LookClock * 1.4f + Seal);
		const float Flicker = Warn > 0.0f ? (2.0f + 6.0f * Warn) * (0.6f + 0.4f * FMath::Sin(LookClock * (10.0f + 25.0f * Warn))) : 0.0f;
		if (UMaterialInstanceDynamic* Look = SealDiscLooks.IsValidIndex(Seal) ? SealDiscLooks[Seal].Get() : nullptr)
		{
			Look->SetScalarParameterValue(TEXT("Intensity"), bStrike ? 14.0f : Rest + Flicker);
		}
		if (UStaticMeshComponent* Ring = SealRings.IsValidIndex(Seal) ? SealRings[Seal].Get() : nullptr)
		{
			const float Scale = (SealRadius / 70.0f) * (1.0f + (Warn > 0.0f ? 0.05f * FMath::Sin(LookClock * 18.0f) : 0.0f));
			Ring->SetRelativeScale3D(FVector(Scale, Scale, (5.0f + Warn * 20.0f) / 13.0f));
		}
		if (UStaticMeshComponent* Warning = PillarWarnBeams.IsValidIndex(Seal) ? PillarWarnBeams[Seal].Get() : nullptr)
		{
			// A thin shaft feels its way down from the sky during the warning.
			Warning->SetVisibility(Warn > 0.0f);
			Warning->SetRelativeScale3D(FVector(0.9f + Warn * 1.5f, 0.9f + Warn * 1.5f, 34.0f));
			if (UMaterialInstanceDynamic* Look = WarnLooks.IsValidIndex(Seal) ? WarnLooks[Seal].Get() : nullptr)
			{
				Look->SetScalarParameterValue(TEXT("Intensity"), 2.0f + 6.0f * Warn);
			}
		}
		if (UStaticMeshComponent* Beam = PillarBeams.IsValidIndex(Seal) ? PillarBeams[Seal].Get() : nullptr)
		{
			// The pillar slams down full width, then thins away.
			Beam->SetVisibility(bStrike);
			const float Width = SealRadius * 2.0f / 100.0f * (StrikeT < 0.15f ? 1.15f : 1.0f - 0.65f * FMath::Square((StrikeT - 0.15f) / 0.85f));
			Beam->SetRelativeScale3D(FVector(Width, Width, 34.0f));
			if (UMaterialInstanceDynamic* Look = PillarLooks.IsValidIndex(Seal) ? PillarLooks[Seal].Get() : nullptr)
			{
				Look->SetScalarParameterValue(TEXT("Intensity"), 10.0f * (1.0f - StrikeT) + 2.0f);
			}
		}
		if (UPointLightComponent* Light = SealLights.IsValidIndex(Seal) ? SealLights[Seal].Get() : nullptr)
		{
			Light->SetIntensity(bStrike ? 90000.0f * (1.0f - StrikeT) + 8000.0f : Warn * 30000.0f + 1500.0f);
		}
	}
}

void AChaosImpactTempleStage::TickAltarServer()
{
	// The altar's pad gives special balls only (the pads are made by the game mode when the match is set up).
	if (GetWorld()->GetTimeSeconds() < NextAltarCheckAt)
	{
		return;
	}
	NextAltarCheckAt = GetWorld()->GetTimeSeconds() + 1.0;
	for (TActorIterator<AChaosImpactBallSpawner> It(GetWorld()); It; ++It)
	{
		if (FVector::Dist2D(It->GetActorLocation(), GetActorLocation()) < 200.0f && It->GetChance(EChaosImpactBallType::Normal) > 0.0f)
		{
			It->SetChanceWeight(EChaosImpactBallType::Normal, 0.0f);
			// The rarer ones turn up a little more often here.
			It->SetChanceWeight(EChaosImpactBallType::Simae, 0.08f);
			It->SetChanceWeight(EChaosImpactBallType::Drive, 0.08f);
			if (AChaosImpactBall* Lying = It->GetActiveBall(); Lying && Lying->IsPickup() && Lying->GetBallType() == EChaosImpactBallType::Normal)
			{
				Lying->Destroy();
			}
			bAltarBlessed = true;
			UE_LOG(LogChaosImpact, Log, TEXT("Temple: the altar's pad %s now gives special balls only"), *It->GetName());
		}
	}
}

void AChaosImpactTempleStage::WarpCharacter(AChaosImpactCharacter* Character, const int32 Portal)
{
	const int32 Exit = GetLinkedPortal(Portal);
	const FVector Facing = GetActorTransform().TransformVectorNoScale(GetPortalFacing(Exit));
	const FVector From = Character->GetActorLocation();
	const FVector ExitAt = GetActorTransform().TransformPosition(GetPortalOffset(Exit));
	const float HalfHeight = Character->GetCapsuleComponent() ? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 96.0f;
	const FVector To = ExitAt + Facing * 230.0f + FVector(0.0f, 0.0f, HalfHeight + 5.0f);
	const float Speed = FMath::Max(static_cast<float>(Character->GetVelocity().Size2D()), 350.0f);
	PortalCooldown.Add(Character, GetWorld()->GetTimeSeconds() + 0.8);
	Character->WarpTo(To);
	Character->SetActorRotation(FRotator(0.0f, TempleStage::YawOf(Facing), 0.0f));
	// Out the far side at the pace it went in.
	Character->GetCharacterMovement()->Velocity = Facing * Speed;
	MulticastPortal(Portal, From, To);
	UE_LOG(LogChaosImpact, Log, TEXT("Temple: %s went through portal %d and came out of %d"), *Character->GetName(), Portal, Exit);
}

void AChaosImpactTempleStage::WarpBall(AChaosImpactBall* Ball, const int32 Portal)
{
	const int32 Exit = GetLinkedPortal(Portal);
	const FTransform& Stage = GetActorTransform();
	const FVector InFacing = Stage.TransformVectorNoScale(GetPortalFacing(Portal));
	const FVector OutFacing = Stage.TransformVectorNoScale(GetPortalFacing(Exit));
	const FVector InAt = Stage.TransformPosition(GetPortalOffset(Portal));
	const FVector OutAt = Stage.TransformPosition(GetPortalOffset(Exit));
	// The way it was going and its sideways drift carry over: it went in along -InFacing, it comes out along OutFacing.
	const FVector InTravel = -InFacing, InSide = FVector::CrossProduct(InTravel, FVector::UpVector);
	const FVector OutSide = FVector::CrossProduct(OutFacing, FVector::UpVector);
	const FVector Velocity = Ball->GetBallVelocity();
	const FVector NewVelocity = OutFacing * FVector::DotProduct(Velocity, InTravel) + OutSide * FVector::DotProduct(Velocity, InSide)
		+ FVector(0.0f, 0.0f, Velocity.Z);
	const FVector Offset = Ball->GetActorLocation() - InAt;
	const FVector From = Ball->GetActorLocation();
	const FVector To = OutAt + OutSide * FMath::Clamp(static_cast<float>(FVector::DotProduct(Offset, InSide)), -PortalAperture, PortalAperture)
		+ OutFacing * 70.0f + FVector(0.0f, 0.0f, Offset.Z);
	Ball->SetActorLocation(To, false, nullptr, ETeleportType::TeleportPhysics);
	Ball->DeflectByWind(NewVelocity);
	PortalCooldown.Add(Ball, GetWorld()->GetTimeSeconds() + 0.35);
	BallLastLocation.Add(Ball, To);
	MulticastPortal(Portal, From, To);
	UE_LOG(LogChaosImpact, Log, TEXT("Temple: a ball went through portal %d and came out of %d at %s"), Portal, Exit, *NewVelocity.ToCompactString());
}

void AChaosImpactTempleStage::TickPortalsServer()
{
	UWorld* World = GetWorld();
	const double Now = World->GetTimeSeconds();
	for (auto It = PortalCooldown.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || It.Value() < Now)
		{
			It.RemoveCurrent();
		}
	}
	const FTransform& Stage = GetActorTransform();
	for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
	{
		AChaosImpactCharacter* Character = *It;
		if (Character->IsEliminated() || PortalCooldown.Contains(Character))
		{
			continue;
		}
		for (int32 Portal = 0; Portal < 4; ++Portal)
		{
			const FVector Facing = Stage.TransformVectorNoScale(GetPortalFacing(Portal));
			const FVector Local = Character->GetActorLocation() - Stage.TransformPosition(GetPortalOffset(Portal));
			const FVector Side = FVector::CrossProduct(Facing, FVector::UpVector);
			const float Depth = FVector::DotProduct(Local, Facing);
			// Stepping into the opening from the front (not from round the back).
			if (Depth < 55.0f && Depth > -60.0f && FMath::Abs(FVector::DotProduct(Local, Side)) < PortalAperture
				&& Local.Z < 450.0f && FVector::DotProduct(Character->GetVelocity(), Facing) <= 10.0f)
			{
				WarpCharacter(Character, Portal);
				break;
			}
		}
	}
	for (TActorIterator<AChaosImpactBall> It(World); It; ++It)
	{
		AChaosImpactBall* Ball = *It;
		const EChaosImpactBallType Type = Ball->GetBallType();
		if (!Ball->IsFlyingOnServer() || Type == EChaosImpactBallType::Beam || Type == EChaosImpactBallType::Nova)
		{
			BallLastLocation.Remove(Ball);
			continue;
		}
		const FVector Now3 = Ball->GetActorLocation();
		const FVector* Last = BallLastLocation.Find(Ball);
		if (Last && !PortalCooldown.Contains(Ball))
		{
			for (int32 Portal = 0; Portal < 4; ++Portal)
			{
				const FVector Facing = Stage.TransformVectorNoScale(GetPortalFacing(Portal));
				const FVector Origin = Stage.TransformPosition(GetPortalOffset(Portal));
				const float Before = FVector::DotProduct(*Last - Origin, Facing);
				const float After = FVector::DotProduct(Now3 - Origin, Facing);
				if (Before <= 0.0f || After > 0.0f)
				{
					continue;
				}
				const FVector Cross = FMath::Lerp(*Last, Now3, Before / FMath::Max(Before - After, KINDA_SMALL_NUMBER)) - Origin;
				const FVector Side = FVector::CrossProduct(Facing, FVector::UpVector);
				if (FMath::Abs(FVector::DotProduct(Cross, Side)) < PortalAperture && Cross.Z > -20.0f && Cross.Z < 470.0f)
				{
					WarpBall(Ball, Portal);
					break;
				}
			}
		}
		if (!PortalCooldown.Contains(Ball) || !Last)
		{
			BallLastLocation.Add(Ball, Ball->GetActorLocation());
		}
	}
	for (auto It = BallLastLocation.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

void AChaosImpactTempleStage::MulticastPortal_Implementation(const int32 FromPortal, const FVector_NetQuantize From,
	const FVector_NetQuantize To)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::StageWarp, From, 0.8f, 1.2f);
	ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::StageWarp, To, 0.8f, 1.35f);
	const FLinearColor Colour = FromPortal % 2 == 1 ? FLinearColor(1.0f, 0.6f, 0.15f) : FLinearColor(0.3f, 1.0f, 0.9f);
	if (UNiagaraSystem* Out = ChaosImpactBallTypes::LoadEffect(ChaosImpactBallTypes::Effects::WarpOut))
	{
		if (UNiagaraComponent* Effect = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, Out, From, FRotator::ZeroRotator, FVector(1.2f)))
		{
			ChaosImpactBallTypes::SetEffectColor(Effect, TEXT("Base Color"), Colour);
		}
	}
	if (UNiagaraSystem* In = ChaosImpactBallTypes::LoadEffect(ChaosImpactBallTypes::Effects::WarpIn))
	{
		if (UNiagaraComponent* Effect = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, In, To, FRotator::ZeroRotator, FVector(1.2f)))
		{
			ChaosImpactBallTypes::SetEffectColor(Effect, TEXT("Base Color"), Colour);
		}
	}
}

void AChaosImpactTempleStage::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!GetWorld())
	{
		return;
	}
	if (HasAuthority())
	{
		TickPillarServer(GetServerNow());
		TickPortalsServer();
		TickAltarServer();
	}
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	LookClock += DeltaSeconds;
	UpdatePillarLook(DeltaSeconds);
	if (RingRoot)
	{
		RingRoot->AddLocalRotation(FRotator(0.0f, 0.0f, 6.0f * DeltaSeconds));
	}
	for (int32 Index = 0; Index < PortalSwirls.Num(); ++Index)
	{
		if (USceneComponent* Swirl = PortalSwirls[Index])
		{
			Swirl->AddLocalRotation(FRotator(0.0f, 0.0f, (Index % 2 ? -70.0f : 70.0f) * DeltaSeconds));
		}
	}
	for (int32 Index = 0; Index < IsletRoots.Num(); ++Index)
	{
		if (USceneComponent* Islet = IsletRoots[Index])
		{
			const FVector Base = Islet->GetRelativeLocation();
			Islet->SetRelativeLocation(FVector(Base.X, Base.Y, Base.Z + FMath::Cos(LookClock * 0.5f + Index) * 18.0f * DeltaSeconds));
		}
	}
	for (int32 Index = 0; Index < AltarGlows.Num(); ++Index)
	{
		if (UStaticMeshComponent* Glow = AltarGlows[Index])
		{
			// The altar's gold rings swell in turn (outer to inner), round and round.
			const float Wave = 0.5f + 0.5f * FMath::Sin(LookClock * 2.2f - Index * 1.1f);
			const float Base = Index == 0 ? 700.0f : Index == 1 ? 480.0f : 280.0f;
			Glow->SetRelativeScale3D(FVector(Base / 70.0f, Base / 70.0f, (6.0f + 10.0f * Wave) / 13.0f));
		}
	}
}
