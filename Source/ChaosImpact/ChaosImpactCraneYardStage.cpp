#include "ChaosImpactCraneYardStage.h"

#include "AIController.h"
#include "ChaosImpact.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactBallTypes.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactHook.h"
#include "ChaosImpactPlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/CollisionProfile.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{

	// ---- Where things are (stage frame: X along the yard, Y towards the sea, Z up from the quay).
	constexpr float CYQuayEdgeY = 6400.0f;
	constexpr float CYSeaLevel = -250.0f;
	constexpr float CYDeckLevel = 520.0f;
	const FBox2D CYShipDeck(FVector2D(-2700.0f, 10900.0f), FVector2D(12900.0f, 15100.0f));
	const FVector2D CYStartSpot(-350.0f, 0.0f);
	/** Where the player stands to throw at the first post (the course's posts are laid out from here). */
	const FVector2D CYFirstThrowSpot(300.0f, 0.0f);

	FVector2D CYDir(const float Yaw)
	{
		return FVector2D(FMath::Cos(FMath::DegreesToRadians(Yaw)), FMath::Sin(FMath::DegreesToRadians(Yaw)));
	}

	/** From the middle of a Length x Width rectangle turned by Yaw, how far to its edge along Direction (unit). */
	float CYEdgeDistance(const AChaosImpactCraneYardStage::FPlatform& P, const FVector2D& Direction)
	{
		const float Angle = FMath::Atan2(Direction.Y, Direction.X) - FMath::DegreesToRadians(P.Yaw);
		const float C = FMath::Abs(FMath::Cos(Angle));
		const float S = FMath::Abs(FMath::Sin(Angle));
		const float AlongX = C > 1e-4f ? P.Length * 0.5f / C : 1e9f;
		const float AlongY = S > 1e-4f ? P.Width * 0.5f / S : 1e9f;
		return FMath::Min(AlongX, AlongY);
	}

	/** Each platform's post (stage frame, swing at rest) and the way it is approached, laid out once. */
	struct FCYCourse
	{
		TArray<FVector2D> Posts;
		TArray<FVector2D> Approach;
		TArray<FVector2D> Landings;
	};
	const FCYCourse& CYGetCourse()
	{
		static const FCYCourse Course = []
		{
			const TArray<AChaosImpactCraneYardStage::FPlatform>& Platforms = AChaosImpactCraneYardStage::GetPlatforms();
			FCYCourse C;
			C.Posts.Add(FVector2D::ZeroVector);
			C.Approach.Add(FVector2D(1.0f, 0.0f));
			C.Landings.Add(CYFirstThrowSpot);
			for (int32 Index = 1; Index < Platforms.Num(); ++Index)
			{
				const AChaosImpactCraneYardStage::FPlatform& P = Platforms[Index];
				const FVector2D Way = (P.Center - C.Landings.Last()).GetSafeNormal();
				const FVector2D Post = P.Center + Way * (CYEdgeDistance(P, Way) - 45.0f);
				C.Posts.Add(Post);
				C.Approach.Add(Way);
				C.Landings.Add(Post - Way * 150.0f);
			}
			return C;
		}();
		return Course;
	}

	/** A stable little random number from integers (the yard looks the same every time). */
	float CYHash(const int32 A, const int32 B = 0, const int32 C = 0)
	{
		uint32 H = static_cast<uint32>(A) * 73856093u ^ static_cast<uint32>(B) * 19349663u ^ static_cast<uint32>(C) * 83492791u;
		H ^= H >> 13;
		H *= 0x5bd1e995u;
		H ^= H >> 15;
		return static_cast<float>(H & 0xFFFFu) / 65535.0f;
	}

	const TCHAR* const CYMasterGloss = TEXT("/Game/ChaosImpact/Versus/Splash/M_CI_GlossPaint.M_CI_GlossPaint");
	const TCHAR* const CYMasterCorrugated = TEXT("/Game/ChaosImpact/Versus/Splash/M_CI_Corrugated.M_CI_Corrugated");
	const TCHAR* const CYMasterConcrete = TEXT("/Game/ChaosImpact/Versus/Splash/M_CI_Concrete.M_CI_Concrete");
	const TCHAR* const CYMasterSteel = TEXT("/Game/ChaosImpact/solo/Stage1/Materials/M_CI_StageWall.M_CI_StageWall");
	const TCHAR* const CYMasterNeon = TEXT("/Game/ChaosImpact/solo/Stage1/Materials/M_CI_Neon.M_CI_Neon");
	const TCHAR* const CYSeaMaterial = TEXT("/Game/ChaosImpact/solo/Stage02/Materials/MI_Sea.MI_Sea");
	const TCHAR* const CYWaterPlane = TEXT("/Game/WaterMaterials/Meshes/SM_Water_Plane.SM_Water_Plane");
	const TCHAR* const CYSniperClass = TEXT("/Game/ChaosImpact/solo/BP_SoloEnemy.BP_SoloEnemy_C");
}

namespace
{
	/** A load's frame (stage frame): its pivot over the middle of the gap, the way along X. */
	FTransform CYHazardPivot(const AChaosImpactCraneYardStage::FHazard& Hazard)
	{
		const TArray<AChaosImpactCraneYardStage::FPlatform>& Platforms = AChaosImpactCraneYardStage::GetPlatforms();
		const FVector2D From = CYGetCourse().Landings[Hazard.Platform - 1];
		const FVector2D To = CYGetCourse().Posts[Hazard.Platform];
		const FVector2D Way = (To - From).GetSafeNormal();
		const float Hand = Platforms[Hazard.Platform - 1].Top + AChaosImpactCraneYardStage::HandHeight;
		return FTransform(FRotator(0.0f, FMath::RadiansToDegrees(FMath::Atan2(Way.Y, Way.X)), 0.0f), FVector((From + To) * 0.5f, Hand + Hazard.Cable));
	}
	const FVector CYLoadHalf(600.0f, 366.0f, 130.0f);
}

// ---------------------------------------------------------------------------------------------------- The course

const TArray<AChaosImpactCraneYardStage::FPlatform>& AChaosImpactCraneYardStage::GetPlatforms()
{
	// Laid out (and checked) with Scripts/Solo/craneyard_layout.py: four rows joined by U-turns, A along the yard off the
	// crane's girder, B back along the next row, C over the berth on hanging boxes, D over the ship's deck to the goal.
	static const TArray<FPlatform> Platforms = {
		{{0.0f, 0.0f}, 2000.0f, 1400.0f, 900.0f, 0.0f, 0.0f, 0.0f},
		{{1950.0f, 0.0f}, 2000.0f, 610.0f, 488.0f, 0.0f, 0.0f, 0.0f},
		{{3750.0f, 0.0f}, 1920.0f, 610.0f, 488.0f, 0.0f, 0.0f, 0.0f},
		{{5394.0f, 599.0f}, 1860.0f, 488.0f, 488.0f, 20.0f, 0.0f, 0.0f},
		{{7181.0f, 120.0f}, 1740.0f, 610.0f, 488.0f, -15.0f, 0.0f, 0.0f},
		{{8905.0f, 424.0f}, 1780.0f, 488.0f, 488.0f, 10.0f, 0.0f, 0.0f},
		{{10805.0f, 424.0f}, 1630.0f, 610.0f, 488.0f, 0.0f, 0.0f, 0.0f},
		{{12605.0f, 424.0f}, 1570.0f, 488.0f, 488.0f, 0.0f, 0.0f, 0.0f},
		{{12605.0f, 2274.0f}, 1570.0f, 488.0f, 488.0f, 90.0f, 0.0f, 0.0f},
		{{12605.0f, 4124.0f}, 1530.0f, 488.0f, 488.0f, 90.0f, 0.0f, 0.0f},
		{{10755.0f, 4124.0f}, 1450.0f, 488.0f, 244.0f, 180.0f, 0.0f, 0.0f},
		{{9196.0f, 5024.0f}, 1450.0f, 488.0f, 244.0f, 150.0f, 0.0f, 0.0f},
		{{7565.0f, 4263.0f}, 1360.0f, 488.0f, 244.0f, 205.0f, 0.0f, 0.0f},
		{{5888.0f, 5045.0f}, 1390.0f, 488.0f, 244.0f, 155.0f, 0.0f, 0.0f},
		{{3988.0f, 5045.0f}, 1290.0f, 610.0f, 244.0f, 180.0f, 0.0f, 0.0f},
		{{2088.0f, 5045.0f}, 1290.0f, 244.0f, 1220.0f, 180.0f, 360.0f, 9.0f},
		{{238.0f, 5045.0f}, 1230.0f, 488.0f, 244.0f, 180.0f, 0.0f, 0.0f},
		{{238.0f, 6895.0f}, 1230.0f, 488.0f, 488.0f, 90.0f, 0.0f, 0.0f},
		{{238.0f, 8745.0f}, 1230.0f, 488.0f, 488.0f, 90.0f, 0.0f, 0.0f},
		{{2138.0f, 8745.0f}, 1170.0f, 244.0f, 1220.0f, 0.0f, 360.0f, 8.5f},
		{{4038.0f, 8745.0f}, 1170.0f, 244.0f, 1220.0f, 0.0f, 360.0f, 10.0f},
		{{5825.0f, 8266.0f}, 1210.0f, 488.0f, 488.0f, -15.0f, 0.0f, 0.0f},
		{{7725.0f, 8266.0f}, 1130.0f, 244.0f, 1220.0f, 0.0f, 360.0f, 8.0f},
		{{9625.0f, 8266.0f}, 1130.0f, 244.0f, 1220.0f, 0.0f, 360.0f, 9.0f},
		{{11475.0f, 8266.0f}, 1070.0f, 488.0f, 488.0f, 0.0f, 0.0f, 0.0f},
		{{11475.0f, 10116.0f}, 1070.0f, 488.0f, 244.0f, 90.0f, 0.0f, 0.0f},
		{{11475.0f, 11966.0f}, 1030.0f, 488.0f, 488.0f, 90.0f, 0.0f, 0.0f},
		{{9625.0f, 11966.0f}, 950.0f, 488.0f, 244.0f, 180.0f, 0.0f, 0.0f},
		{{8246.0f, 13123.0f}, 950.0f, 488.0f, 244.0f, 140.0f, 0.0f, 0.0f},
		{{6459.0f, 12644.0f}, 890.0f, 488.0f, 244.0f, 195.0f, 0.0f, 0.0f},
		{{4944.0f, 13705.0f}, 920.0f, 488.0f, 244.0f, 145.0f, 0.0f, 0.0f},
		{{3044.0f, 13705.0f}, 830.0f, 488.0f, 244.0f, 180.0f, 0.0f, 0.0f},
		{{1144.0f, 13705.0f}, 830.0f, 244.0f, 1220.0f, 180.0f, 360.0f, 7.5f},
		{{-706.0f, 13705.0f}, 790.0f, 732.0f, 732.0f, 180.0f, 0.0f, 0.0f},
	};
	return Platforms;
}

const TArray<AChaosImpactCraneYardStage::FSniper>& AChaosImpactCraneYardStage::GetSnipers()
{
	// On both sides of every hop, beside two stacks of nearly the same height: its drive ball flies level at its hand's
	// height, which meets a player standing on (or flying between) them. Several are always in range.
	static const TArray<FSniper> Snipers = {
		{{2850.0f, 2300.0f}, 1910.0f}, {{2850.0f, -2300.0f}, 1910.0f}, {{4570.0f, 2600.0f}, 1840.0f}, {{5360.0f, -1860.0f}, 1840.0f},
		{{6880.0f, 2580.0f}, 1750.0f}, {{7300.0f, -1820.0f}, 1750.0f}, {{8830.0f, 2430.0f}, 1710.0f}, {{9190.0f, -1720.0f}, 1710.0f},
		{{14480.0f, 2020.0f}, 1550.0f}, {{11700.0f, -1880.0f}, 1550.0f}, {{14600.0f, 200.0f}, 1520.0f}, {{11060.0f, 5880.0f}, 1500.0f},
		{{14770.0f, 3990.0f}, 1500.0f}, {{12830.0f, 6120.0f}, 1440.0f}, {{1110.0f, 2750.0f}, 1240.0f}, {{-680.0f, 3500.0f}, 1210.0f},
		{{-1610.0f, 6650.0f}, 1210.0f}, {{-1750.0f, 4820.0f}, 1180.0f}, {{-1920.0f, 8610.0f}, 1180.0f}, {{1190.0f, 11040.0f}, 1150.0f},
		{{3090.0f, 11040.0f}, 1120.0f}, {{5900.0f, 10590.0f}, 1140.0f}, {{7700.0f, 10800.0f}, 1120.0f}, {{13230.0f, 9820.0f}, 1050.0f},
		{{13470.0f, 8040.0f}, 1020.0f}, {{9920.0f, 13730.0f}, 1000.0f}, {{13640.0f, 11830.0f}, 1000.0f}, {{11700.0f, 13960.0f}, 940.0f},
		{{8520.0f, 14910.0f}, 900.0f}, {{6760.0f, 15110.0f}, 870.0f}, {{5080.0f, 15490.0f}, 855.0f}, {{3210.0f, 15870.0f}, 825.0f},
		{{-430.0f, 11580.0f}, 780.0f}, {{1170.0f, 16240.0f}, 780.0f}, {{-2210.0f, 12300.0f}, 760.0f}, {{-570.0f, 15870.0f}, 760.0f},
	};
	return Snipers;
}

const TArray<AChaosImpactCraneYardStage::FHazard>& AChaosImpactCraneYardStage::GetHazards()
{
	// Over the gap before these stacks (from the second row on), each its own swing.
	static const TArray<FHazard> Hazards = {
		{11, 19.0f, 5.6f, 2600.0f},
		{14, 20.0f, 6.2f, 2600.0f},
		{18, 19.0f, 5.2f, 2600.0f},
		{21, 21.0f, 6.6f, 2600.0f},
		{24, 19.0f, 5.0f, 2600.0f},
		{27, 20.0f, 5.8f, 2600.0f},
		{29, 21.0f, 6.0f, 2600.0f},
		{31, 19.0f, 4.8f, 2600.0f},
	};
	return Hazards;
}

AChaosImpactCraneYardStage::AChaosImpactCraneYardStage()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

double AChaosImpactCraneYardStage::GetSwingTime() const
{
	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	return GameState ? GameState->GetServerWorldTimeSeconds() : World ? World->GetTimeSeconds() : 0.0;
}

FTransform AChaosImpactCraneYardStage::GetPlatformFrame(const int32 Index) const
{
	const TArray<FPlatform>& Platforms = GetPlatforms();
	if (!Platforms.IsValidIndex(Index))
	{
		return GetActorTransform();
	}
	const FPlatform& P = Platforms[Index];
	FVector2D At = P.Center;
	if (P.Swing > 0.0f && P.Period > 0.0f)
	{
		const float Phase = static_cast<float>(FMath::Fmod(GetSwingTime(), static_cast<double>(P.Period)) / P.Period) * UE_TWO_PI;
		At += CYDir(P.Yaw + 90.0f) * (P.Swing * FMath::Sin(Phase + Index * 1.7f));
	}
	const FTransform Local(FRotator(0.0f, P.Yaw, 0.0f), FVector(At, P.Top));
	return Local * GetActorTransform();
}

FVector AChaosImpactCraneYardStage::GetPostFoot(const int32 Index) const
{
	const TArray<FPlatform>& Platforms = GetPlatforms();
	if (Index <= 0 || !Platforms.IsValidIndex(Index))
	{
		return GetActorTransform().TransformPosition(FVector(Platforms[0].Center, Platforms[0].Top));
	}
	const FPlatform& P = Platforms[Index];
	// The post rides on its box: its place in the box's own frame.
	const FVector2D Offset = CYGetCourse().Posts[Index] - P.Center;
	const FVector Local = FRotator(0.0f, -P.Yaw, 0.0f).RotateVector(FVector(Offset, 0.0f));
	return GetPlatformFrame(Index).TransformPosition(Local);
}

FVector AChaosImpactCraneYardStage::GetLandingPoint(const int32 Index) const
{
	const TArray<FPlatform>& Platforms = GetPlatforms();
	if (Index <= 0 || !Platforms.IsValidIndex(Index))
	{
		return GetActorTransform().TransformPosition(FVector(CYFirstThrowSpot, Platforms[0].Top));
	}
	const FPlatform& P = Platforms[Index];
	const FVector2D Offset = CYGetCourse().Landings[Index] - P.Center;
	const FVector Local = FRotator(0.0f, -P.Yaw, 0.0f).RotateVector(FVector(Offset, 0.0f));
	return GetPlatformFrame(Index).TransformPosition(Local);
}

bool AChaosImpactCraneYardStage::IsOverPlatform(const int32 Index, const FVector& Point, const float Margin) const
{
	const TArray<FPlatform>& Platforms = GetPlatforms();
	if (!Platforms.IsValidIndex(Index))
	{
		return false;
	}
	const FPlatform& P = Platforms[Index];
	const FVector Local = GetPlatformFrame(Index).InverseTransformPosition(Point);
	return FMath::Abs(Local.X) <= P.Length * 0.5f + Margin && FMath::Abs(Local.Y) <= P.Width * 0.5f + Margin;
}

FVector AChaosImpactCraneYardStage::GetStartPoint() const
{
	return GetActorTransform().TransformPosition(FVector(CYStartSpot, GetPlatforms()[0].Top));
}

float AChaosImpactCraneYardStage::BaseUnder(const FVector2D& At) const
{
	if (At.Y < CYQuayEdgeY)
	{
		return 0.0f;
	}
	if (CYShipDeck.IsInside(At))
	{
		return CYDeckLevel;
	}
	return CYSeaLevel;
}

// ---------------------------------------------------------------------------------------------------- Building

UInstancedStaticMeshComponent* AChaosImpactCraneYardStage::Bucket(UStaticMesh* Mesh, UMaterialInterface* Material, const bool bCollide)
{
	const FBucketKey Key{Mesh, Material, bCollide};
	if (TObjectPtr<UInstancedStaticMeshComponent>* Found = Buckets.Find(Key))
	{
		return *Found;
	}
	UInstancedStaticMeshComponent* Instances = NewObject<UInstancedStaticMeshComponent>(this);
	Instances->SetMobility(EComponentMobility::Movable);
	Instances->SetupAttachment(Root);
	Instances->SetStaticMesh(Mesh);
	if (Material)
	{
		Instances->SetMaterial(0, Material);
	}
	if (bCollide && !Material)
	{
		// A solid with no material: never drawn, only there to stand on (and for anchors to bite into).
		Instances->SetHiddenInGame(true);
		Instances->SetCastShadow(false);
	}
	if (bCollide)
	{
		Instances->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Instances->SetCollisionObjectType(ECC_WorldStatic);
		Instances->SetCanEverAffectNavigation(false);
	}
	else
	{
		Instances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Instances->SetCanEverAffectNavigation(false);
		Instances->bAffectDistanceFieldLighting = false;
	}
	Instances->RegisterComponent();
	Buckets.Add(Key, Instances);
	BucketList.Add(Instances);
	return Instances;
}

void AChaosImpactCraneYardStage::Instance(UStaticMesh* Mesh, const FTransform& Where, UMaterialInterface* Material, const bool bCollide)
{
	if (Mesh)
	{
		Bucket(Mesh, Material, bCollide)->AddInstance(Where, false);
	}
}

void AChaosImpactCraneYardStage::Box(const FVector& Where, const FVector& Size, UMaterialInterface* Material, const bool bCollide,
	const float Yaw, const float Pitch, const float Roll)
{
	Instance(CubeMesh, FTransform(FRotator(Pitch, Yaw, Roll), Where, Size / 100.0f), Material, bCollide);
}

void AChaosImpactCraneYardStage::Pole(const FVector& Foot, const float Radius, const float Height, UMaterialInterface* Material, const bool bCollide)
{
	Instance(CylinderMesh, FTransform(FRotator::ZeroRotator, Foot + FVector(0.0f, 0.0f, Height * 0.5f),
		FVector(Radius / 50.0f, Radius / 50.0f, Height / 100.0f)), Material, bCollide);
}

UMaterialInstanceDynamic* AChaosImpactCraneYardStage::Paint(const TCHAR* Parent, const FLinearColor& Color)
{
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, Parent);
	UMaterialInstanceDynamic* Made = Base ? UMaterialInstanceDynamic::Create(Base, this) : nullptr;
	if (Made)
	{
		Made->SetVectorParameterValue(TEXT("Color"), Color);
	}
	return Made;
}

UMaterialInstanceDynamic* AChaosImpactCraneYardStage::Glow(const FLinearColor& Color, const float Intensity)
{
	UMaterialInstanceDynamic* Made = Paint(CYMasterNeon, Color);
	if (Made)
	{
		Made->SetScalarParameterValue(TEXT("Intensity"), Intensity);
	}
	return Made;
}

UPointLightComponent* AChaosImpactCraneYardStage::Light(const FVector& Where, const FLinearColor& Color, const float Intensity, const float Radius)
{
	UPointLightComponent* Lamp = NewObject<UPointLightComponent>(this);
	Lamp->SetMobility(EComponentMobility::Movable);
	Lamp->SetupAttachment(Root);
	Lamp->SetRelativeLocation(Where);
	Lamp->SetLightColor(Color);
	Lamp->SetIntensity(Intensity);
	Lamp->SetAttenuationRadius(Radius);
	Lamp->SetCastShadows(false);
	Lamp->RegisterComponent();
	return Lamp;
}

void AChaosImpactCraneYardStage::BuildMaterials()
{
	CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	CylinderMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	const auto Keep = [this](const TCHAR* Name, UMaterialInterface* Made) { Materials.Add(Name, Made); };
	// Containers: the faded shipping-line colours of a real yard.
	const FLinearColor Colours[] = {
		{0.42f, 0.05f, 0.035f}, {0.035f, 0.10f, 0.30f}, {0.62f, 0.22f, 0.03f}, {0.04f, 0.22f, 0.10f}, {0.02f, 0.26f, 0.30f},
		{0.52f, 0.52f, 0.50f}, {0.24f, 0.035f, 0.05f}, {0.66f, 0.46f, 0.04f}, {0.09f, 0.09f, 0.10f}, {0.30f, 0.14f, 0.38f},
	};
	for (const FLinearColor& Colour : Colours)
	{
		BoxColours.Add(Paint(CYMasterCorrugated, Colour));
	}
	Keep(TEXT("Frame"), Paint(CYMasterSteel, FLinearColor(0.035f, 0.035f, 0.04f)));
	Keep(TEXT("Bars"), Paint(CYMasterSteel, FLinearColor(0.16f, 0.16f, 0.15f)));
	Keep(TEXT("Quay"), Paint(CYMasterConcrete, FLinearColor(0.30f, 0.29f, 0.27f)));
	Keep(TEXT("QuayDark"), Paint(CYMasterConcrete, FLinearColor(0.16f, 0.155f, 0.15f)));
	Keep(TEXT("LaneYellow"), Paint(CYMasterGloss, FLinearColor(0.80f, 0.55f, 0.04f)));
	Keep(TEXT("LaneWhite"), Paint(CYMasterGloss, FLinearColor(0.70f, 0.70f, 0.66f)));
	Keep(TEXT("CraneYellow"), Paint(CYMasterGloss, FLinearColor(0.85f, 0.56f, 0.03f)));
	Keep(TEXT("CraneRed"), Paint(CYMasterGloss, FLinearColor(0.62f, 0.05f, 0.03f)));
	Keep(TEXT("CraneWhite"), Paint(CYMasterGloss, FLinearColor(0.78f, 0.78f, 0.76f)));
	Keep(TEXT("Steel"), Paint(CYMasterSteel, FLinearColor(0.10f, 0.10f, 0.11f)));
	Keep(TEXT("Cable"), Paint(CYMasterSteel, FLinearColor(0.02f, 0.02f, 0.02f)));
	Keep(TEXT("Hull"), Paint(CYMasterGloss, FLinearColor(0.025f, 0.05f, 0.12f)));
	Keep(TEXT("HullRed"), Paint(CYMasterGloss, FLinearColor(0.38f, 0.04f, 0.03f)));
	Keep(TEXT("Deck"), Paint(CYMasterGloss, FLinearColor(0.10f, 0.20f, 0.12f)));
	Keep(TEXT("Bridge"), Paint(CYMasterGloss, FLinearColor(0.80f, 0.80f, 0.78f)));
	Keep(TEXT("Barge"), Paint(CYMasterGloss, FLinearColor(0.12f, 0.12f, 0.13f)));
	Keep(TEXT("Black"), Paint(CYMasterGloss, FLinearColor(0.012f, 0.012f, 0.014f)));
	Keep(TEXT("City"), Paint(CYMasterGloss, FLinearColor(0.03f, 0.035f, 0.06f)));
	Keep(TEXT("Lamp"), Glow(FLinearColor(1.0f, 0.68f, 0.32f), 14.0f));
	Keep(TEXT("Window"), Glow(FLinearColor(1.0f, 0.78f, 0.45f), 6.0f));
	Keep(TEXT("WindowBlue"), Glow(FLinearColor(0.45f, 0.75f, 1.0f), 5.0f));
	Keep(TEXT("Aviation"), Glow(FLinearColor(1.0f, 0.06f, 0.03f), 22.0f));
	Keep(TEXT("Anchor"), Glow(FLinearColor(0.10f, 0.88f, 1.0f), 9.0f));
	Keep(TEXT("AnchorSoft"), Glow(FLinearColor(0.10f, 0.88f, 1.0f), 1.2f));
	Keep(TEXT("Warn"), Glow(FLinearColor(1.0f, 0.08f, 0.04f), 30.0f));
	Keep(TEXT("Goal"), Glow(FLinearColor(1.0f, 0.74f, 0.18f), 18.0f));
	Keep(TEXT("GoalSoft"), Glow(FLinearColor(1.0f, 0.74f, 0.18f), 2.5f));
	Keep(TEXT("SniperGlow"), Glow(FLinearColor(1.0f, 0.12f, 0.05f), 8.0f));
}

void AChaosImpactCraneYardStage::BuildGround()
{
	UMaterialInterface* Quay = Materials.FindRef(TEXT("Quay"));
	UMaterialInterface* QuayDark = Materials.FindRef(TEXT("QuayDark"));
	// The quay: one slab (its top at 0), its edge wall down into the sea along the berth.
	Box(FVector(5500.0f, -2600.0f, -60.0f), FVector(34000.0f, 18000.0f, 120.0f), Quay, true);
	Box(FVector(5500.0f, CYQuayEdgeY - 60.0f, -200.0f), FVector(34000.0f, 120.0f, 400.0f), QuayDark, true);
	// Lane lines and bay markings (no words: lines only).
	UMaterialInterface* Yellow = Materials.FindRef(TEXT("LaneYellow"));
	UMaterialInterface* White = Materials.FindRef(TEXT("LaneWhite"));
	for (float Y : {-4200.0f, -1700.0f, 1400.0f, 2900.0f, 6000.0f})
	{
		Box(FVector(5500.0f, Y, 1.0f), FVector(32000.0f, 26.0f, 2.0f), Yellow, false);
	}
	for (int32 Row = 0; Row < 3; ++Row)
	{
		for (float X = -9000.0f; X < 20000.0f; X += 1500.0f)
		{
			Box(FVector(X, -3000.0f + Row * 3100.0f, 1.0f), FVector(18.0f, 600.0f, 2.0f), White, false);
		}
	}
	// Bollards and the yellow edge along the berth.
	for (float X = -10000.0f; X < 21000.0f; X += 1300.0f)
	{
		Pole(FVector(X, CYQuayEdgeY - 160.0f, 0.0f), 30.0f, 70.0f, Materials.FindRef(TEXT("Black")), false);
	}
	Box(FVector(5500.0f, CYQuayEdgeY - 40.0f, 6.0f), FVector(34000.0f, 60.0f, 12.0f), Yellow, false);
}

void AChaosImpactCraneYardStage::BuildSea()
{
	UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, CYWaterPlane);
	UMaterialInterface* Sea = LoadObject<UMaterialInterface>(nullptr, CYSeaMaterial);
	if (!Plane)
	{
		return;
	}
	const FVector Size = Plane->GetBoundingBox().GetSize();
	const float SX = Size.X > 1.0f ? 1.0f / Size.X : 1.0f;
	const float SY = Size.Y > 1.0f ? 1.0f / Size.Y : 1.0f;
	// The sea from the quay's edge out to the horizon (and round the quay's far ends).
	UStaticMeshComponent* Water = NewObject<UStaticMeshComponent>(this);
	Water->SetMobility(EComponentMobility::Movable);
	Water->SetupAttachment(Root);
	Water->SetStaticMesh(Plane);
	if (Sea)
	{
		Water->SetMaterial(0, Sea);
	}
	Water->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Water->SetRelativeLocation(FVector(5500.0f, 40000.0f, CYSeaLevel));
	Water->SetRelativeScale3D(FVector(140000.0f * SX, 67000.0f * SY, 1.0f));
	Water->RegisterComponent();
}

void AChaosImpactCraneYardStage::BuildContainer(const FVector2D& Center, const float Bottom, const float Yaw, const float Length,
	UMaterialInterface* Body, const bool bCollide)
{
	const FVector2D Along = CYDir(Yaw);
	const FVector2D Across = CYDir(Yaw + 90.0f);
	const float H = BoxHeight;
	const float W = 244.0f;
	// The corrugated body, a dark frame round each end, and four locking bars on the doors. (What is solid is one block
	// per stack, BuildStack: no seams between boxes for an anchor's floor check to slip through.)
	Box(FVector(Center, Bottom + H * 0.5f), FVector(Length - 8.0f, W - 6.0f, H - 4.0f), Body, false, Yaw);
	UMaterialInterface* Frame = Materials.FindRef(TEXT("Frame"));
	UMaterialInterface* Bars = Materials.FindRef(TEXT("Bars"));
	for (const float End : {-1.0f, 1.0f})
	{
		const FVector2D EndAt = Center + Along * (End * (Length * 0.5f - 6.0f));
		Box(FVector(EndAt, Bottom + H * 0.5f), FVector(14.0f, W, H), Frame, false, Yaw);
		for (const float Bar : {-0.3f, -0.1f, 0.1f, 0.3f})
		{
			Box(FVector(EndAt + Along * (End * 9.0f) + Across * (Bar * W), Bottom + H * 0.5f), FVector(5.0f, 7.0f, H - 30.0f), Bars, false, Yaw);
		}
	}
	// Top and bottom rails along the long sides.
	for (const float Side : {-1.0f, 1.0f})
	{
		Box(FVector(Center + Across * (Side * (W * 0.5f - 3.0f)), Bottom + H - 6.0f), FVector(Length, 8.0f, 12.0f), Frame, false, Yaw);
		Box(FVector(Center + Across * (Side * (W * 0.5f - 3.0f)), Bottom + 8.0f), FVector(Length, 8.0f, 16.0f), Frame, false, Yaw);
	}
}

void AChaosImpactCraneYardStage::BuildStack(const FVector2D& Center, const float Yaw, const FVector2D& Footprint, const float Base,
	const float Top, const int32 Seed, const bool bCollide)
{
	const FVector2D Along = CYDir(Yaw);
	const FVector2D Across = CYDir(Yaw + 90.0f);
	// Rows of boxes across the footprint (244 wide), each a forty or a twenty footer, tiers down from the top until
	// they go below the base.
	const int32 Rows = FMath::Max(1, FMath::RoundToInt(Footprint.Y / 244.0f));
	const bool bShort = Footprint.X < 800.0f;
	const float Length = bShort ? FMath::Max(Footprint.X, 244.0f) : 1220.0f;
	const int32 Columns = FMath::Max(1, FMath::RoundToInt(Footprint.X / Length));
	int32 Tier = 0;
	for (float Bottom = Top - BoxHeight; Bottom + BoxHeight > Base; Bottom -= BoxHeight, ++Tier)
	{
		for (int32 Row = 0; Row < Rows; ++Row)
		{
			for (int32 Column = 0; Column < Columns; ++Column)
			{
				const float Offset = (Row - (Rows - 1) * 0.5f) * 244.0f;
				const float Shift = (Column - (Columns - 1) * 0.5f) * Length;
				// A small untidiness to the lower tiers (never the top one: it is walked on).
				const float Jitter = Tier > 0 ? (CYHash(Seed, Tier, Row * 7 + Column) - 0.5f) * 30.0f : 0.0f;
				const int32 Colour = FMath::Clamp(static_cast<int32>(CYHash(Seed * 3 + 1, Tier, Row + Column * 5) * BoxColours.Num()), 0, BoxColours.Num() - 1);
				BuildContainer(Center + Across * Offset + Along * (Shift + Jitter), Bottom, Yaw + (Tier > 0 ? Jitter * 0.03f : 0.0f), Length,
					BoxColours[Colour], bCollide);
			}
		}
	}
	if (bCollide)
	{
		// The stack's solid: one block, unseen, from its base to its top.
		const float Low = FMath::Max(Base, CYSeaLevel - 200.0f);
		Box(FVector(Center, (Low + Top) * 0.5f), FVector(Footprint.X, Footprint.Y, Top - Low), nullptr, true, Yaw);
	}
	// On the water, the stack stands on a barge.
	if (Base <= CYSeaLevel + 1.0f)
	{
		UMaterialInterface* Barge = Materials.FindRef(TEXT("Barge"));
		Box(FVector(Center, CYSeaLevel - 60.0f), FVector(Footprint.X + 300.0f, Footprint.Y + 300.0f, 240.0f), Barge, bCollide, Yaw);
		Box(FVector(Center, CYSeaLevel + 75.0f), FVector(Footprint.X + 320.0f, Footprint.Y + 320.0f, 30.0f), Materials.FindRef(TEXT("LaneYellow")), false, Yaw);
	}
}

void AChaosImpactCraneYardStage::BuildPost(const int32 Index)
{
	const TArray<FPlatform>& Platforms = GetPlatforms();
	const FPlatform& P = Platforms[Index];
	const FPlatform& Before = Platforms[Index - 1];
	// Tall enough to meet an anchor thrown from the stack before (at its hand height), and a little over.
	const float Height = FMath::Max(Before.Top - P.Top, 0.0f) + HandHeight + 160.0f;
	const FVector2D Post = CYGetCourse().Posts[Index];
	const bool bSwings = P.Swing > 0.0f;
	// The shaft: on its own (so an anchor thrown point-blank past it goes by, AChaosImpactHook::PlaceAnchor), striped.
	UStaticMeshComponent* Shaft = NewObject<UStaticMeshComponent>(this);
	Shaft->SetMobility(EComponentMobility::Movable);
	USceneComponent* Parent = Root;
	FVector Where(Post, P.Top + Height * 0.5f);
	if (bSwings)
	{
		for (const FSwing& Swing : Swings)
		{
			if (Swing.Platform == Index)
			{
				Parent = Swing.Carrier;
				const FVector2D Offset = Post - P.Center;
				Where = FRotator(0.0f, -P.Yaw, 0.0f).RotateVector(FVector(Offset, Height * 0.5f));
			}
		}
	}
	Shaft->SetupAttachment(Parent);
	Shaft->SetStaticMesh(CylinderMesh);
	Shaft->SetMaterial(0, Materials.FindRef(TEXT("CraneYellow")));
	Shaft->SetRelativeLocation(Where);
	Shaft->SetRelativeScale3D(FVector(PostRadius / 50.0f, PostRadius / 50.0f, Height / 100.0f));
	Shaft->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Shaft->SetCollisionObjectType(ECC_WorldStatic);
	// Characters pass it (a player just set down before it flies on past it); anchors and balls meet it.
	Shaft->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	Shaft->ComponentTags.Add(TEXT("CI.AnchorPassNear"));
	Shaft->RegisterComponent();
	PostShafts.Add(Shaft);
	// Black bands, the cyan ring at the height an anchor meets it, and a cap.
	const float HitHeight = Before.Top + HandHeight - P.Top;
	const auto Part = [&](const FVector& Offset, const FVector& Scale, UMaterialInterface* Material, UStaticMesh* Mesh)
	{
		UStaticMeshComponent* Piece = NewObject<UStaticMeshComponent>(this);
		Piece->SetMobility(EComponentMobility::Movable);
		Piece->SetupAttachment(Shaft);
		Piece->SetStaticMesh(Mesh);
		Piece->SetMaterial(0, Material);
		Piece->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Piece->SetAbsolute(false, false, true);
		Piece->SetRelativeLocation(FVector(0.0f, 0.0f, (Offset.Z - Height * 0.5f) / (Height / 100.0f)));
		Piece->SetRelativeScale3D(Scale);
		Piece->RegisterComponent();
		return Piece;
	};
	for (float Z = 30.0f; Z < Height - 40.0f; Z += 90.0f)
	{
		Part(FVector(0.0f, 0.0f, Z), FVector((PostRadius + 1.5f) / 50.0f, (PostRadius + 1.5f) / 50.0f, 0.35f), Materials.FindRef(TEXT("Black")), CylinderMesh);
	}
	PostRings.Add(Part(FVector(0.0f, 0.0f, HitHeight), FVector((PostRadius + 5.0f) / 50.0f, (PostRadius + 5.0f) / 50.0f, 0.3f),
		Materials.FindRef(TEXT("Anchor")), CylinderMesh));
	Part(FVector(0.0f, 0.0f, Height - 6.0f), FVector((PostRadius + 8.0f) / 50.0f, (PostRadius + 8.0f) / 50.0f, 0.14f), Materials.FindRef(TEXT("Anchor")), CylinderMesh);
	Part(FVector(0.0f, 0.0f, Height + 10.0f), FVector(0.26f, 0.26f, 0.26f), Materials.FindRef(TEXT("Anchor")), SphereMesh);
	// A thin cyan ring round its foot.
	Part(FVector(0.0f, 0.0f, 1.5f), FVector(1.25f, 1.25f, 0.015f), Materials.FindRef(TEXT("AnchorSoft")), CylinderMesh);
}

void AChaosImpactCraneYardStage::BuildHangingBox(const int32 Index)
{
	const FPlatform& P = GetPlatforms()[Index];
	FSwing& Swing = Swings.AddDefaulted_GetRef();
	Swing.Platform = Index;
	USceneComponent* Carrier = NewObject<USceneComponent>(this);
	Carrier->SetMobility(EComponentMobility::Movable);
	Carrier->SetupAttachment(Root);
	Carrier->SetRelativeLocation(FVector(P.Center, P.Top));
	Carrier->SetRelativeRotation(FRotator(0.0f, P.Yaw, 0.0f));
	Carrier->RegisterComponent();
	Swing.Carrier = Carrier;
	SwingCarriers.Add(Carrier);
	// The box (its long side across the way, the way it swings), solid to stand on, and its spreader frame on top.
	const auto Piece = [&](const FVector& At, const FVector& Size, UMaterialInterface* Material, const bool bSolid)
	{
		UStaticMeshComponent* Made = NewObject<UStaticMeshComponent>(this);
		Made->SetMobility(EComponentMobility::Movable);
		Made->SetupAttachment(Carrier);
		Made->SetStaticMesh(CubeMesh);
		Made->SetMaterial(0, Material);
		Made->SetRelativeLocation(At);
		Made->SetRelativeScale3D(Size / 100.0f);
		if (bSolid)
		{
			Made->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
			Made->SetCollisionObjectType(ECC_WorldStatic);
		}
		else
		{
			Made->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
		Made->RegisterComponent();
		return Made;
	};
	UMaterialInterface* Body = BoxColours[(Index * 3) % BoxColours.Num()];
	Piece(FVector(0.0f, 0.0f, -BoxHeight * 0.5f), FVector(P.Length - 4.0f, P.Width - 8.0f, BoxHeight - 4.0f), Body, true);
	UMaterialInterface* Frame = Materials.FindRef(TEXT("Frame"));
	for (const float End : {-1.0f, 1.0f})
	{
		Piece(FVector(0.0f, End * (P.Width * 0.5f - 7.0f), -BoxHeight * 0.5f), FVector(P.Length + 4.0f, 14.0f, BoxHeight), Frame, false);
		Piece(FVector(End * (P.Length * 0.5f - 3.0f), 0.0f, -6.0f), FVector(8.0f, P.Width, 12.0f), Frame, false);
	}
	// The spreader: a yellow beam frame lying on it, with corner lights.
	UMaterialInterface* Yellow = Materials.FindRef(TEXT("CraneYellow"));
	Piece(FVector(0.0f, 0.0f, 22.0f), FVector(70.0f, P.Width - 60.0f, 34.0f), Yellow, false);
	for (const float End : {-1.0f, 1.0f})
	{
		Piece(FVector(0.0f, End * (P.Width * 0.5f - 40.0f), 22.0f), FVector(P.Length - 20.0f, 30.0f, 30.0f), Yellow, false);
		Piece(FVector(0.0f, End * (P.Width * 0.5f - 40.0f), 44.0f), FVector(26.0f, 26.0f, 14.0f), Materials.FindRef(TEXT("Aviation")), false);
	}
	// The trolley high above (it runs along the gantry's girders with the box), and four cables up to it (stretched each frame).
	const float TrolleyHeight = P.Top + 2800.0f;
	Piece(FVector(0.0f, 0.0f, TrolleyHeight - P.Top), FVector(300.0f, 820.0f, 120.0f), Materials.FindRef(TEXT("CraneWhite")), false);
	for (const float End : {-1.0f, 1.0f})
	{
		for (const float Side : {-1.0f, 1.0f})
		{
			UStaticMeshComponent* Cable = NewObject<UStaticMeshComponent>(this);
			Cable->SetMobility(EComponentMobility::Movable);
			Cable->SetupAttachment(Root);
			Cable->SetStaticMesh(CylinderMesh);
			Cable->SetMaterial(0, Materials.FindRef(TEXT("Cable")));
			Cable->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Cable->RegisterComponent();
			Swing.Cables.Add(Cable);
			Swing.CableFeet.Add(FVector(Side * 30.0f, End * (P.Width * 0.5f - 40.0f), 36.0f));
			Swing.CableTops.Add(FVector(Side * 60.0f, End * 120.0f, TrolleyHeight - P.Top));
		}
	}
}

void AChaosImpactCraneYardStage::BuildPlatforms()
{
	const TArray<FPlatform>& Platforms = GetPlatforms();
	for (int32 Index = 0; Index < Platforms.Num(); ++Index)
	{
		const FPlatform& P = Platforms[Index];
		if (P.Swing > 0.0f)
		{
			BuildHangingBox(Index);
			continue;
		}
		if (Index == 0)
		{
			continue;
		}
		BuildStack(P.Center, P.Yaw, FVector2D(P.Length, P.Width), BaseUnder(P.Center), P.Top, 100 + Index, true);
	}
	for (int32 Index = 1; Index < Platforms.Num(); ++Index)
	{
		BuildPost(Index);
	}
	// The start: the girder deck of a great gantry crane, with its machinery house, railings and its legs down to the quay.
	const FPlatform& Start = Platforms[0];
	UMaterialInterface* Yellow = Materials.FindRef(TEXT("CraneYellow"));
	UMaterialInterface* Steel = Materials.FindRef(TEXT("Steel"));
	Box(FVector(Start.Center, Start.Top - 60.0f), FVector(Start.Length, Start.Width, 120.0f), Steel, true);
	Box(FVector(Start.Center + FVector2D(-120.0f, 0.0f), Start.Top - 200.0f), FVector(Start.Length + 600.0f, Start.Width - 100.0f, 170.0f), Yellow, true);
	for (const float Side : {-1.0f, 1.0f})
	{
		Box(FVector(Start.Center + FVector2D(0.0f, Side * (Start.Width * 0.5f - 6.0f)), Start.Top + 55.0f), FVector(Start.Length, 6.0f, 6.0f), Yellow, false);
		for (float X = -Start.Length * 0.5f; X <= Start.Length * 0.5f; X += 175.0f)
		{
			Box(FVector(Start.Center + FVector2D(X, Side * (Start.Width * 0.5f - 6.0f)), Start.Top + 28.0f), FVector(6.0f, 6.0f, 56.0f), Yellow, false);
		}
		// The legs (a portal straddling the yard row below).
		for (const float End : {-1.0f, 1.0f})
		{
			const FVector2D Leg = Start.Center + FVector2D(End * (Start.Length * 0.5f + 160.0f), Side * 1400.0f);
			Box(FVector(Leg, (Start.Top - 280.0f) * 0.5f), FVector(140.0f, 140.0f, Start.Top - 280.0f), Yellow, true);
			Box(FVector(Leg, 60.0f), FVector(260.0f, 220.0f, 120.0f), Steel, true);
		}
		Box(FVector(Start.Center + FVector2D(0.0f, Side * 1400.0f), Start.Top - 200.0f), FVector(Start.Length + 600.0f, 160.0f, 170.0f), Yellow, true);
	}
	Box(FVector(Start.Center + FVector2D(-560.0f, 0.0f), Start.Top + 160.0f), FVector(240.0f, 380.0f, 320.0f), Materials.FindRef(TEXT("CraneWhite")), true);
	Box(FVector(Start.Center + FVector2D(-438.0f, 0.0f), Start.Top + 210.0f), FVector(6.0f, 300.0f, 70.0f), Materials.FindRef(TEXT("Window")), false);
	Box(FVector(Start.Center + FVector2D(-560.0f, 0.0f), Start.Top + 330.0f), FVector(260.0f, 400.0f, 20.0f), Yellow, false);
	Box(FVector(Start.Center + FVector2D(-560.0f, 0.0f), Start.Top + 360.0f), FVector(26.0f, 26.0f, 40.0f), Materials.FindRef(TEXT("Aviation")), false);
}

void AChaosImpactCraneYardStage::BuildSniperStacks()
{
	const TArray<FSniper>& Snipers = GetSnipers();
	UMaterialInterface* Red = Materials.FindRef(TEXT("SniperGlow"));
	for (int32 Index = 0; Index < Snipers.Num(); ++Index)
	{
		const FSniper& S = Snipers[Index];
		BuildStack(S.Where, CYHash(Index, 9) > 0.5f ? 0.0f : 90.0f, FVector2D(610.0f, 488.0f), BaseUnder(S.Where), S.Top, 400 + Index, true);
		// A red lamp on a short mast at its corner, and a red edge round its top (it is the enemy's).
		Pole(FVector(S.Where + FVector2D(230.0f, 180.0f), S.Top), 6.0f, 260.0f, Materials.FindRef(TEXT("Steel")), false);
		Box(FVector(S.Where + FVector2D(230.0f, 180.0f), S.Top + 270.0f), FVector(30.0f, 30.0f, 30.0f), Materials.FindRef(TEXT("Aviation")), false);
		const float Yaw = CYHash(Index, 9) > 0.5f ? 0.0f : 90.0f;
		const FVector2D Along = CYDir(Yaw);
		const FVector2D Across = CYDir(Yaw + 90.0f);
		for (const float Sign : {-1.0f, 1.0f})
		{
			Box(FVector(S.Where + Across * (Sign * 238.0f), S.Top + 3.0f), FVector(610.0f, 12.0f, 6.0f), Red, false, Yaw);
			Box(FVector(S.Where + Along * (Sign * 299.0f), S.Top + 3.0f), FVector(12.0f, 488.0f, 6.0f), Red, false, Yaw);
		}
	}
}

void AChaosImpactCraneYardStage::BuildFillerYard()
{
	// Rows of stacks filling the yard round the way (and on the ship's deck): no taller than the way near them, so
	// they never stop an anchor or a drive ball, and kept clear of every stack of the way and every enemy.
	const TArray<FPlatform>& Platforms = GetPlatforms();
	const TArray<FSniper>& Snipers = GetSnipers();
	const auto Allowed = [&](const FVector2D& At, float& OutTop)
	{
		float Limit = 6.0f * BoxHeight;
		for (const FPlatform& P : Platforms)
		{
			const float Distance = FVector2D::Distance(At, P.Center);
			if (Distance < FMath::Max(P.Length, P.Width) * 0.5f + P.Swing + 1100.0f)
			{
				return false;
			}
			if (Distance < 3200.0f)
			{
				Limit = FMath::Min(Limit, P.Top - 260.0f);
			}
		}
		for (const FSniper& S : Snipers)
		{
			if (FVector2D::Distance(At, S.Where) < 1100.0f)
			{
				return false;
			}
		}
		OutTop = Limit;
		return true;
	};
	int32 Seed = 0;
	// The yard: blocks of stacks in rows, lanes between them.
	for (float Y = -9000.0f; Y < CYQuayEdgeY - 600.0f; Y += 610.0f)
	{
		if (FMath::Fmod(Y + 9000.0f, 3050.0f) < 610.0f)
		{
			continue;
		}
		for (float X = -9000.0f; X < 20000.0f; X += 1340.0f)
		{
			++Seed;
			if (CYHash(Seed, 1) < 0.18f)
			{
				continue;
			}
			const FVector2D At(X + (CYHash(Seed, 2) - 0.5f) * 60.0f, Y);
			float Limit = 0.0f;
			if (!Allowed(At, Limit) || !IsFree(At, 640.0f))
			{
				continue;
			}
			const int32 Tiers = FMath::Clamp(1 + static_cast<int32>(CYHash(Seed, 3) * 5.0f), 1, FMath::Max(1, static_cast<int32>(Limit / BoxHeight)));
			BuildStack(At, 0.0f, FVector2D(1220.0f, 488.0f), 0.0f, Tiers * BoxHeight, 1000 + Seed, false);
		}
	}
	// The ship's deck: bays of stacks across it.
	for (float X = -1800.0f; X < 12400.0f; X += 1340.0f)
	{
		for (float Y = 11300.0f; Y < 14900.0f; Y += 488.0f)
		{
			++Seed;
			const FVector2D At(X, Y);
			float Limit = 0.0f;
			if (CYHash(Seed, 4) < 0.25f || !Allowed(At, Limit) || !IsFree(At, 640.0f))
			{
				continue;
			}
			const float Room = Limit - CYDeckLevel;
			if (Room < BoxHeight)
			{
				continue;
			}
			const int32 Tiers = FMath::Clamp(1 + static_cast<int32>(CYHash(Seed, 5) * 2.0f), 1, static_cast<int32>(Room / BoxHeight));
			BuildStack(At, 90.0f, FVector2D(488.0f, 1220.0f) * FVector2D(1.0f, 1.0f), CYDeckLevel, CYDeckLevel + Tiers * BoxHeight, 3000 + Seed, false);
		}
	}
}

bool AChaosImpactCraneYardStage::IsFree(const FVector2D& At, const float Radius) const
{
	for (const FVector& Spot : Occupied)
	{
		if (FVector2D::Distance(At, FVector2D(Spot.X, Spot.Y)) < Radius + Spot.Z)
		{
			return false;
		}
	}
	return true;
}

void AChaosImpactCraneYardStage::Occupy(const FVector2D& At, const float Radius)
{
	Occupied.Add(FVector(At, Radius));
}

void AChaosImpactCraneYardStage::RegisterOccupied()
{
	// The way's stacks, the room every hanging box swings through, every swinging load's sweep, the enemies' stacks.
	const TArray<FPlatform>& Platforms = GetPlatforms();
	for (const FPlatform& P : Platforms)
	{
		Occupy(P.Center, FMath::Sqrt(P.Length * P.Length + P.Width * P.Width) * 0.5f + (P.Swing > 0.0f ? P.Swing + 500.0f : 120.0f));
	}
	for (const FHazard& Hazard : GetHazards())
	{
		const FTransform Pivot = CYHazardPivot(Hazard);
		Occupy(FVector2D(Pivot.GetLocation()), Hazard.Cable * FMath::Sin(FMath::DegreesToRadians(Hazard.Amplitude)) + 750.0f);
	}
	for (const FSniper& S : GetSnipers())
	{
		Occupy(S.Where, 450.0f);
	}
}

void AChaosImpactCraneYardStage::BuildGantry(const FVector2D& Middle, const float RunYaw, const float HalfRun, const float Girder, UMaterialInterface* Paint)
{
	const FVector2D Run = CYDir(RunYaw);
	const FVector2D Side = CYDir(RunYaw + 90.0f);
	constexpr float Gauge = 350.0f;
	float Ends[2] = {HalfRun, HalfRun};
	for (int32 End = 0; End < 2; ++End)
	{
		const float Sign = End ? 1.0f : -1.0f;
		for (int32 Try = 0; Try < 40; ++Try)
		{
			const FVector2D Base = Middle + Run * (Sign * Ends[End]);
			if (IsFree(Base + Side * Gauge, 90.0f) && IsFree(Base - Side * Gauge, 90.0f))
			{
				break;
			}
			Ends[End] += 100.0f;
		}
	}
	UMaterialInterface* Barge = Materials.FindRef(TEXT("Barge"));
	UMaterialInterface* Aviation = Materials.FindRef(TEXT("Aviation"));
	for (int32 End = 0; End < 2; ++End)
	{
		const float Sign = End ? 1.0f : -1.0f;
		const FVector2D EndAt = Middle + Run * (Sign * Ends[End]);
		for (const float S : {-1.0f, 1.0f})
		{
			const FVector2D Foot = EndAt + Side * (S * Gauge);
			float Base = BaseUnder(Foot);
			if (Base <= CYSeaLevel + 1.0f)
			{
				// Over the water it stands on a pontoon.
				Box(FVector(Foot, CYSeaLevel + 10.0f), FVector(380.0f, 380.0f, 160.0f), Barge, false, RunYaw);
				Base = CYSeaLevel + 90.0f;
			}
			Box(FVector(Foot, (Base + Girder) * 0.5f), FVector(100.0f, 100.0f, Girder - Base), Paint, false, RunYaw);
			Box(FVector(Foot, Base + 40.0f), FVector(220.0f, 180.0f, 80.0f), Materials.FindRef(TEXT("Steel")), false, RunYaw);
			Occupy(Foot, 130.0f);
		}
		// The tie across each end, a light on it.
		Box(FVector(EndAt, Girder + 10.0f), FVector(130.0f, Gauge * 2.0f + 140.0f, 150.0f), Paint, false, RunYaw);
		Box(FVector(EndAt, Girder + 110.0f), FVector(34.0f, 34.0f, 34.0f), Aviation, false, RunYaw);
	}
	const float Length = Ends[0] + Ends[1];
	const FVector2D Centre = Middle + Run * ((Ends[1] - Ends[0]) * 0.5f);
	for (const float S : {-1.0f, 1.0f})
	{
		Box(FVector(Centre + Side * (S * Gauge), Girder + 10.0f), FVector(Length + 130.0f, 110.0f, 150.0f), Paint, false, RunYaw);
	}
	// The operator's cab under one end.
	Box(FVector(Middle + Run * Ends[1] - Run * 260.0f, Girder - 170.0f), FVector(260.0f, 300.0f, 220.0f), Materials.FindRef(TEXT("CraneWhite")), false, RunYaw);
	Box(FVector(Middle + Run * Ends[1] - Run * 389.0f, Girder - 150.0f), FVector(4.0f, 240.0f, 70.0f), Materials.FindRef(TEXT("Window")), false, RunYaw);
}

void AChaosImpactCraneYardStage::BuildCranes()
{
	UMaterialInterface* Yellow = Materials.FindRef(TEXT("CraneYellow"));
	UMaterialInterface* Red = Materials.FindRef(TEXT("CraneRed"));
	UMaterialInterface* White = Materials.FindRef(TEXT("CraneWhite"));
	UMaterialInterface* Steel = Materials.FindRef(TEXT("Steel"));
	UMaterialInterface* Aviation = Materials.FindRef(TEXT("Aviation"));
	const TArray<FPlatform>& Platforms = GetPlatforms();
	// Every hanging box hangs from a tall portal crane whose girders run the way it swings (high over the camera).
	for (const FSwing& Swing : Swings)
	{
		const FPlatform& P = Platforms[Swing.Platform];
		BuildGantry(P.Center, P.Yaw + 90.0f, P.Swing + 900.0f, P.Top + 2860.0f, Yellow);
	}
	// Ship-to-shore cranes along the berth, standing idle, their booms out over the water (scenery, where there is room).
	for (const float X : {-6500.0f, 15800.0f, 19500.0f})
	{
		const float BoomZ = 3400.0f;
		bool bRoom = true;
		for (const float Side : {-1.0f, 1.0f})
		{
			for (const float Leg : {CYQuayEdgeY - 2300.0f, CYQuayEdgeY - 900.0f})
			{
				bRoom &= IsFree(FVector2D(X + Side * 900.0f, Leg), 150.0f);
			}
		}
		if (!bRoom)
		{
			continue;
		}
		for (const float Side : {-1.0f, 1.0f})
		{
			for (const float Leg : {CYQuayEdgeY - 2300.0f, CYQuayEdgeY - 900.0f})
			{
				Box(FVector(X + Side * 900.0f, Leg, BoomZ * 0.5f), FVector(120.0f, 120.0f, BoomZ), Red, false);
				Occupy(FVector2D(X + Side * 900.0f, Leg), 160.0f);
			}
			Box(FVector(X + Side * 900.0f, CYQuayEdgeY - 1600.0f, BoomZ * 0.55f), FVector(80.0f, 1400.0f, 80.0f), White, false);
		}
		Box(FVector(X, CYQuayEdgeY - 1600.0f, BoomZ + 40.0f), FVector(1900.0f, 1600.0f, 120.0f), Red, false);
		const float BoomFrom = CYQuayEdgeY - 4000.0f;
		const float BoomTo = CYQuayEdgeY + 3600.0f;
		Box(FVector(X, (BoomFrom + BoomTo) * 0.5f, BoomZ + 160.0f), FVector(420.0f, BoomTo - BoomFrom, 150.0f), White, false);
		Box(FVector(X, (BoomFrom + BoomTo) * 0.5f, BoomZ + 255.0f), FVector(440.0f, BoomTo - BoomFrom, 40.0f), Red, false);
		Box(FVector(X, CYQuayEdgeY - 1600.0f, BoomZ + 1000.0f), FVector(160.0f, 160.0f, 1800.0f), White, false);
		Box(FVector(X, CYQuayEdgeY - 2800.0f, BoomZ + 560.0f), FVector(60.0f, 2700.0f, 60.0f), Steel, false, 0.0f, 0.0f, -38.0f);
		Box(FVector(X, CYQuayEdgeY + 200.0f, BoomZ + 560.0f), FVector(60.0f, 4300.0f, 60.0f), Steel, false, 0.0f, 0.0f, 24.0f);
		Box(FVector(X, BoomFrom + 500.0f, BoomZ + 420.0f), FVector(700.0f, 900.0f, 360.0f), White, false);
		Box(FVector(X, CYQuayEdgeY - 1600.0f, BoomZ + 1920.0f), FVector(40.0f, 40.0f, 40.0f), Aviation, false);
		Box(FVector(X, BoomTo - 40.0f, BoomZ + 300.0f), FVector(40.0f, 40.0f, 40.0f), Aviation, false);
	}
	// Portal cranes over the yard's rows (scenery).
	for (const FVector2D& At : {FVector2D(-5200.0f, -3600.0f), FVector2D(3000.0f, -6200.0f), FVector2D(16800.0f, -3200.0f)})
	{
		if (IsFree(At, 600.0f))
		{
			BuildGantry(At, 90.0f, 1300.0f, 2300.0f, Yellow);
		}
	}
}

void AChaosImpactCraneYardStage::BuildShip()
{
	UMaterialInterface* Hull = Materials.FindRef(TEXT("Hull"));
	UMaterialInterface* HullRed = Materials.FindRef(TEXT("HullRed"));
	UMaterialInterface* Deck = Materials.FindRef(TEXT("Deck"));
	UMaterialInterface* Bridge = Materials.FindRef(TEXT("Bridge"));
	const FVector2D Middle = CYShipDeck.GetCenter();
	const FVector2D Size = CYShipDeck.GetSize();
	// The hull: dark navy over a red bottom at the waterline, the deck on top, a rounded bow towards +X.
	Box(FVector(Middle, (CYDeckLevel + CYSeaLevel - 400.0f) * 0.5f), FVector(Size.X, Size.Y, CYDeckLevel - CYSeaLevel + 400.0f), Hull, true);
	Box(FVector(Middle, CYSeaLevel + 30.0f), FVector(Size.X + 10.0f, Size.Y + 10.0f, 120.0f), HullRed, false);
	Box(FVector(Middle, CYDeckLevel + 5.0f), FVector(Size.X - 120.0f, Size.Y - 120.0f, 10.0f), Deck, true);
	Instance(CylinderMesh, FTransform(FRotator::ZeroRotator, FVector(CYShipDeck.Max.X, Middle.Y, (CYDeckLevel + CYSeaLevel - 400.0f) * 0.5f),
		FVector(Size.Y / 100.0f * 0.9f, Size.Y / 100.0f, (CYDeckLevel - CYSeaLevel + 400.0f) / 100.0f)), Hull, true);
	Instance(CylinderMesh, FTransform(FRotator::ZeroRotator, FVector(CYShipDeck.Max.X, Middle.Y, CYDeckLevel + 5.0f),
		FVector(Size.Y / 100.0f * 0.88f, Size.Y / 100.0f * 0.98f, 0.1f)), Deck, true);
	// Bulwark rails along both sides.
	for (const float Side : {-1.0f, 1.0f})
	{
		Box(FVector(Middle.X, Middle.Y + Side * (Size.Y * 0.5f - 20.0f), CYDeckLevel + 60.0f), FVector(Size.X, 30.0f, 120.0f), Hull, false);
	}
	// The bridge at the stern (-X), white, rows of lit windows, a mast with lights; the goal stack stands before it.
	const FVector2D BridgeAt(CYShipDeck.Min.X + 420.0f, Middle.Y);
	Box(FVector(BridgeAt, CYDeckLevel + 650.0f), FVector(640.0f, 2600.0f, 1300.0f), Bridge, false);
	for (int32 Floor = 0; Floor < 3; ++Floor)
	{
		Box(FVector(BridgeAt + FVector2D(322.0f, 0.0f), CYDeckLevel + 280.0f + Floor * 340.0f), FVector(6.0f, 2300.0f, 90.0f),
			Materials.FindRef(Floor % 2 ? TEXT("Window") : TEXT("WindowBlue")), false);
	}
	Box(FVector(BridgeAt, CYDeckLevel + 1340.0f), FVector(760.0f, 3400.0f, 90.0f), Bridge, false);
	Box(FVector(BridgeAt, CYDeckLevel + 1900.0f), FVector(70.0f, 70.0f, 1000.0f), Materials.FindRef(TEXT("Steel")), false);
	Box(FVector(BridgeAt, CYDeckLevel + 2420.0f), FVector(40.0f, 40.0f, 40.0f), Materials.FindRef(TEXT("Aviation")), false);
	Box(FVector(BridgeAt + FVector2D(-120.0f, 0.0f), CYDeckLevel + 1650.0f), FVector(300.0f, 420.0f, 520.0f), HullRed, false);
	// Mooring lines from the bow and the stern to the quay's bollards (lying long and low across the berth).
	for (const float X : {CYShipDeck.Min.X + 300.0f, CYShipDeck.Max.X - 600.0f})
	{
		const FVector From(X, CYShipDeck.Min.Y, CYDeckLevel + 40.0f);
		const FVector To(X - 600.0f, CYQuayEdgeY - 160.0f, 50.0f);
		const FVector Mid = (From + To) * 0.5f;
		const FVector Line = To - From;
		Instance(CylinderMesh, FTransform(FRotationMatrix::MakeFromZ(Line.GetSafeNormal()).Rotator(), Mid, FVector(0.08f, 0.08f, Line.Size() / 100.0f)),
			Materials.FindRef(TEXT("Cable")), false);
	}
}

void AChaosImpactCraneYardStage::BuildGoal()
{
	const TArray<FPlatform>& Platforms = GetPlatforms();
	const int32 Last = Platforms.Num() - 1;
	const FPlatform& P = Platforms[Last];
	// A gold ring standing over the last stack, a beam of light up from it, and a flag mast.
	const FVector2D At = P.Center;
	const auto Piece = [&](UStaticMesh* Mesh, const FTransform& Where, UMaterialInterface* Material)
	{
		UStaticMeshComponent* Made = NewObject<UStaticMeshComponent>(this);
		Made->SetMobility(EComponentMobility::Movable);
		Made->SetupAttachment(Root);
		Made->SetStaticMesh(Mesh);
		Made->SetMaterial(0, Material);
		Made->SetRelativeTransform(Where);
		Made->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Made->RegisterComponent();
		return Made;
	};
	GoalRing = Piece(CylinderMesh, FTransform(FRotator::ZeroRotator, FVector(At, P.Top + 3.0f), FVector(5.4f, 5.4f, 0.04f)), Materials.FindRef(TEXT("GoalSoft")));
	GoalBeam = Piece(CylinderMesh, FTransform(FRotator::ZeroRotator, FVector(At, P.Top + 1500.0f), FVector(0.6f, 0.6f, 30.0f)), Materials.FindRef(TEXT("Goal")));
	for (int32 Segment = 0; Segment < 24; ++Segment)
	{
		const float Angle = Segment * 15.0f;
		const FVector2D Out = CYDir(Angle) * 260.0f;
		Box(FVector(At + Out, P.Top + 8.0f), FVector(70.0f, 12.0f, 8.0f), Materials.FindRef(TEXT("Goal")), false, Angle + 90.0f);
	}
	Pole(FVector(At + FVector2D(-300.0f, -300.0f), P.Top), 8.0f, 700.0f, Materials.FindRef(TEXT("CraneWhite")), false);
	Box(FVector(At + FVector2D(-300.0f, -180.0f), P.Top + 620.0f), FVector(6.0f, 240.0f, 150.0f), Materials.FindRef(TEXT("Goal")), false);
	Light(FVector(At, P.Top + 260.0f), FLinearColor(1.0f, 0.75f, 0.3f), 9000.0f, 1400.0f);
}

void AChaosImpactCraneYardStage::BuildSkyline()
{
	// The city across the bay (behind the yard, -Y) and a far shore (+Y): dark blocks with lit windows.
	UMaterialInterface* City = Materials.FindRef(TEXT("City"));
	UMaterialInterface* Window = Materials.FindRef(TEXT("Window"));
	UMaterialInterface* WindowBlue = Materials.FindRef(TEXT("WindowBlue"));
	UMaterialInterface* Aviation = Materials.FindRef(TEXT("Aviation"));
	int32 Seed = 7;
	for (float X = -26000.0f; X < 38000.0f; X += 900.0f)
	{
		++Seed;
		const float Height = 1500.0f + CYHash(Seed, 1) * CYHash(Seed, 2) * 9000.0f;
		const float Width = 600.0f + CYHash(Seed, 3) * 700.0f;
		const float Y = -21000.0f - CYHash(Seed, 4) * 5000.0f;
		Box(FVector(X, Y, Height * 0.5f - 300.0f), FVector(Width, 700.0f, Height), City, false);
		for (float Z = 300.0f; Z < Height - 300.0f; Z += 260.0f)
		{
			if (CYHash(Seed, static_cast<int32>(Z)) < 0.45f)
			{
				continue;
			}
			Box(FVector(X, Y + 352.0f, Z), FVector(Width * (0.3f + CYHash(Seed, static_cast<int32>(Z), 2) * 0.6f), 4.0f, 60.0f),
				CYHash(Seed, static_cast<int32>(Z), 3) < 0.7f ? Window : WindowBlue, false);
		}
		if (Height > 7000.0f)
		{
			Box(FVector(X, Y, Height - 260.0f), FVector(50.0f, 50.0f, 50.0f), Aviation, false);
		}
	}
	// The far shore over the sea: low hills and a line of lights.
	for (float X = -30000.0f; X < 40000.0f; X += 2400.0f)
	{
		++Seed;
		const float Height = 800.0f + CYHash(Seed, 5) * 1800.0f;
		Box(FVector(X, 52000.0f, Height * 0.5f + CYSeaLevel), FVector(2600.0f, 3000.0f, Height), City, false);
		Box(FVector(X, 50480.0f, CYSeaLevel + 160.0f), FVector(1800.0f, 6.0f, 40.0f), Window, false);
	}
	// A lighthouse at the end of the breakwater.
	Box(FVector(22000.0f, 20000.0f, CYSeaLevel + 60.0f), FVector(9000.0f, 500.0f, 300.0f), Materials.FindRef(TEXT("QuayDark")), false);
	Pole(FVector(26300.0f, 20000.0f, CYSeaLevel + 200.0f), 150.0f, 1500.0f, Materials.FindRef(TEXT("CraneWhite")), false);
	Pole(FVector(26300.0f, 20000.0f, CYSeaLevel + 700.0f), 155.0f, 260.0f, Materials.FindRef(TEXT("CraneRed")), false);
	Pole(FVector(26300.0f, 20000.0f, CYSeaLevel + 1700.0f), 120.0f, 160.0f, Materials.FindRef(TEXT("Lamp")), false);
}

void AChaosImpactCraneYardStage::BuildLamps()
{
	// High mast lights over the yard: tall poles, a ring of lamps at the top, real light on the way below.
	UMaterialInterface* Steel = Materials.FindRef(TEXT("Steel"));
	UMaterialInterface* Lamp = Materials.FindRef(TEXT("Lamp"));
	const FVector2D Masts[] = {
		{-2600.0f, -1800.0f}, {5200.0f, -2400.0f}, {13800.0f, -800.0f}, {9800.0f, 2300.0f}, {2900.0f, 3100.0f}, {-1800.0f, 4500.0f},
		{14400.0f, 5800.0f}, {6800.0f, 6100.0f},
	};
	for (const FVector2D& At : Masts)
	{
		if (!IsFree(At, 220.0f))
		{
			continue;
		}
		Occupy(At, 220.0f);
		Pole(FVector(At, 0.0f), 26.0f, 3000.0f, Steel, false);
		Pole(FVector(At, 2960.0f), 70.0f, 50.0f, Steel, false);
		for (int32 Head = 0; Head < 6; ++Head)
		{
			const FVector2D Out = CYDir(Head * 60.0f) * 110.0f;
			Box(FVector(At + Out * 0.5f, 2990.0f), FVector(110.0f, 16.0f, 16.0f), Steel, false, Head * 60.0f);
			Box(FVector(At + Out, 2978.0f), FVector(60.0f, 46.0f, 22.0f), Lamp, false, Head * 60.0f);
		}
		Light(FVector(At, 2900.0f), FLinearColor(1.0f, 0.72f, 0.42f), 60000.0f, 5200.0f);
	}
	// Lights on the course: a warm lamp by every few stacks, a cool one over each hanging box.
	const TArray<FPlatform>& Platforms = GetPlatforms();
	for (int32 Index = 1; Index < Platforms.Num(); Index += 3)
	{
		const FPlatform& P = Platforms[Index];
		Light(FVector(P.Center, P.Top + 420.0f), FLinearColor(1.0f, 0.78f, 0.55f), 5000.0f, 1300.0f);
	}
}

float AChaosImpactCraneYardStage::GetHazardAngle(const int32 Index) const
{
	const TArray<FHazard>& Hazards = GetHazards();
	if (!Hazards.IsValidIndex(Index))
	{
		return 0.0f;
	}
	const FHazard& Hazard = Hazards[Index];
	if (bHazardsParked)
	{
		return Hazard.Amplitude;
	}
	const float Phase = static_cast<float>(FMath::Fmod(GetSwingTime() + Index * 1.3, static_cast<double>(Hazard.Period)) / Hazard.Period) * UE_TWO_PI;
	return Hazard.Amplitude * FMath::Sin(Phase);
}


void AChaosImpactCraneYardStage::GetHazardBox(const int32 Index, FVector& OutCentre, FQuat& OutRotation, FVector& OutHalf) const
{
	const FHazard& Hazard = GetHazards()[Index];
	const FTransform Pivot = CYHazardPivot(Hazard) * GetActorTransform();
	// It swings across the way: a roll about the way's line.
	const FQuat Swing = Pivot.GetRotation() * FQuat(FVector::ForwardVector, FMath::DegreesToRadians(GetHazardAngle(Index)));
	OutRotation = Swing;
	OutCentre = Pivot.GetLocation() + Swing.RotateVector(FVector(0.0f, 0.0f, -Hazard.Cable));
	OutHalf = CYLoadHalf;
}

void AChaosImpactCraneYardStage::BuildHazards()
{
	const TArray<FHazard>& Hazards = GetHazards();
	UMaterialInterface* Yellow = Materials.FindRef(TEXT("CraneYellow"));
	UMaterialInterface* White = Materials.FindRef(TEXT("CraneWhite"));
	UMaterialInterface* Red = Materials.FindRef(TEXT("CraneRed"));
	UMaterialInterface* Aviation = Materials.FindRef(TEXT("Aviation"));
	for (int32 Index = 0; Index < Hazards.Num(); ++Index)
	{
		const FHazard& Hazard = Hazards[Index];
		const FTransform PivotFrame = CYHazardPivot(Hazard);
		FHazardParts& Parts = HazardParts.AddDefaulted_GetRef();
		USceneComponent* Pivot = NewObject<USceneComponent>(this);
		Pivot->SetMobility(EComponentMobility::Movable);
		Pivot->SetupAttachment(Root);
		Pivot->SetRelativeTransform(PivotFrame);
		Pivot->RegisterComponent();
		Parts.Pivot = Pivot;
		HazardPivots.Add(Pivot);
		const auto Piece = [&](UStaticMesh* Mesh, const FVector& At, const FVector& Size, UMaterialInterface* Material, const bool bSolid)
		{
			UStaticMeshComponent* Made = NewObject<UStaticMeshComponent>(this);
			Made->SetMobility(EComponentMobility::Movable);
			Made->SetupAttachment(Pivot);
			Made->SetStaticMesh(Mesh);
			Made->SetMaterial(0, Material);
			Made->SetRelativeLocation(At);
			Made->SetRelativeScale3D(Size / 100.0f);
			if (bSolid)
			{
				Made->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
				Made->SetCollisionObjectType(ECC_WorldStatic);
			}
			else
			{
				Made->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}
			Made->RegisterComponent();
			return Made;
		};
		// The load: three containers bundled side by side (the solid is one block round them), a lifting frame on top,
		// red and white warning bands on its ends, lights at its corners; four cables up to the pivot.
		const float Down = -Hazard.Cable;
		Parts.Load = Piece(CubeMesh, FVector(0.0f, 0.0f, Down), CYLoadHalf * 2.0f, nullptr, true);
		Parts.Load->SetHiddenInGame(true);
		Parts.Load->SetCastShadow(false);
		for (int32 Box = -1; Box <= 1; ++Box)
		{
			UMaterialInterface* Body = BoxColours[(Index * 2 + Box + 3) % BoxColours.Num()];
			Piece(CubeMesh, FVector(0.0f, Box * 244.0f, Down), FVector(1200.0f, 238.0f, 255.0f), Body, false);
		}
		for (const float End : {-1.0f, 1.0f})
		{
			for (int32 Band = 0; Band < 6; ++Band)
			{
				Piece(CubeMesh, FVector(End * 604.0f, -305.0f + Band * 122.0f, Down), FVector(8.0f, 122.0f, 262.0f), Band % 2 ? White : Red, false);
			}
			for (const float Side : {-1.0f, 1.0f})
			{
				Piece(CubeMesh, FVector(End * 560.0f, Side * 330.0f, Down + 150.0f), FVector(30.0f, 30.0f, 22.0f), Aviation, false);
			}
		}
		Piece(CubeMesh, FVector(0.0f, 0.0f, Down + 150.0f), FVector(1160.0f, 70.0f, 40.0f), Yellow, false);
		Piece(CubeMesh, FVector(0.0f, 0.0f, Down + 150.0f), FVector(70.0f, 700.0f, 40.0f), Yellow, false);
		for (const float End : {-1.0f, 1.0f})
		{
			for (const float Side : {-1.0f, 1.0f})
			{
				const FVector Foot(End * 520.0f, Side * 300.0f, Down + 160.0f);
				const FVector Top(End * 60.0f, Side * 40.0f, -30.0f);
				const FVector Line = Top - Foot;
				UStaticMeshComponent* Cable = Piece(CylinderMesh, (Top + Foot) * 0.5f, FVector(10.0f, 10.0f, Line.Size()), Materials.FindRef(TEXT("Cable")), false);
				Cable->SetRelativeRotation(FRotationMatrix::MakeFromZ(Line.GetSafeNormal()).Rotator());
			}
		}
		// The crane it hangs from: a tall portal across the way (its girders above the camera), the trolley over the pivot.
		const float Lateral = Hazard.Cable * FMath::Sin(FMath::DegreesToRadians(Hazard.Amplitude));
		BuildGantry(FVector2D(PivotFrame.GetLocation()), PivotFrame.Rotator().Yaw + 90.0f, Lateral + 900.0f, PivotFrame.GetLocation().Z + 60.0f, Yellow);
		Box(PivotFrame.GetLocation() + FVector(0.0f, 0.0f, 40.0f), FVector(320.0f, 820.0f, 120.0f), White, false, PivotFrame.Rotator().Yaw + 90.0f);
	}
}

void AChaosImpactCraneYardStage::TickHazards(const double Now)
{
	const TArray<FHazard>& Hazards = GetHazards();
	AChaosImpactCharacter* Player = GetPlayer();
	for (int32 Index = 0; Index < HazardParts.Num() && Index < Hazards.Num(); ++Index)
	{
		FHazardParts& Parts = HazardParts[Index];
		if (!Parts.Pivot)
		{
			continue;
		}
		const FTransform PivotFrame = CYHazardPivot(Hazards[Index]);
		const FQuat Swing = PivotFrame.GetRotation() * FQuat(FVector::ForwardVector, FMath::DegreesToRadians(GetHazardAngle(Index)));
		Parts.Pivot->SetRelativeLocationAndRotation(PivotFrame.GetLocation(), Swing);
		// Meeting the player (standing or in an anchor's flight): knocked off sideways, the way it swings.
		if (!Player || !HasAuthority() || Player->IsEliminated() || Now - Parts.LastKnockAt < 0.8)
		{
			continue;
		}
		FVector Centre;
		FQuat Rotation;
		FVector Half;
		GetHazardBox(Index, Centre, Rotation, Half);
		const FVector Local = Rotation.UnrotateVector(Player->GetActorLocation() - Centre);
		const float Radius = Player->GetCapsuleComponent()->GetScaledCapsuleRadius();
		const float HalfHeight = Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		if (FMath::Abs(Local.X) < Half.X + Radius && FMath::Abs(Local.Y) < Half.Y + Radius && FMath::Abs(Local.Z) < Half.Z + HalfHeight)
		{
			Parts.LastKnockAt = Now;
			const float Speed = FMath::Cos(static_cast<float>(FMath::Fmod(GetSwingTime() + Index * 1.3, static_cast<double>(Hazards[Index].Period))
				/ Hazards[Index].Period) * UE_TWO_PI);
			const FVector Side = Rotation.RotateVector(FVector(0.0f, Speed >= 0.0f ? 1.0f : -1.0f, 0.0f)).GetSafeNormal2D();
			Player->BreakAnchorZip();
			Player->LaunchCharacter(Side * 1500.0f + FVector(0.0f, 0.0f, 260.0f), true, true);
			UE_LOG(LogChaosImpact, Log, TEXT("Crane yard: the swinging load %d hit the player"), Index);
		}
	}
}

void AChaosImpactCraneYardStage::BuildMarker()
{
	// The arrow over the next post: a cone pointing down with a ring round it, and a ring on the stack where the anchor
	// will set the player down.
	Marker = NewObject<USceneComponent>(this);
	Marker->SetMobility(EComponentMobility::Movable);
	Marker->SetupAttachment(Root);
	Marker->RegisterComponent();
	UMaterialInterface* Glowing = Glow(FLinearColor(0.25f, 1.0f, 0.95f), 14.0f);
	const auto Piece = [&](USceneComponent* Parent, UStaticMesh* Mesh, const FTransform& Where, UMaterialInterface* Material)
	{
		UStaticMeshComponent* Made = NewObject<UStaticMeshComponent>(this);
		Made->SetMobility(EComponentMobility::Movable);
		Made->SetupAttachment(Parent);
		Made->SetStaticMesh(Mesh);
		Made->SetMaterial(0, Material);
		Made->SetRelativeTransform(Where);
		Made->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Made->SetCastShadow(false);
		Made->RegisterComponent();
		return Made;
	};
	UStaticMesh* Cone = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
	Piece(Marker, Cone, FTransform(FRotator(180.0f, 0.0f, 0.0f), FVector(0.0f, 0.0f, 0.0f), FVector(0.95f, 0.95f, 1.1f)), Glowing);
	Piece(Marker, CylinderMesh, FTransform(FRotator::ZeroRotator, FVector(0.0f, 0.0f, 70.0f), FVector(0.42f, 0.42f, 0.9f)), Glowing);
	MarkerRing = Piece(Root, CylinderMesh, FTransform(FRotator::ZeroRotator, FVector::ZeroVector, FVector(1.5f, 1.5f, 0.012f)),
		Glow(FLinearColor(0.25f, 1.0f, 0.95f), 3.0f));
	// The arrow at the feet: a chevron of two bars and a short shaft, lying flat.
	FootArrow = NewObject<USceneComponent>(this);
	FootArrow->SetMobility(EComponentMobility::Movable);
	FootArrow->SetupAttachment(Root);
	FootArrow->RegisterComponent();
	UMaterialInterface* ArrowGlow = Glow(FLinearColor(0.05f, 0.85f, 1.0f), 4.0f);
	for (const float Side : {-1.0f, 1.0f})
	{
		Piece(FootArrow, CubeMesh, FTransform(FRotator(0.0f, Side * 135.0f, 0.0f), FVector(-42.0f, Side * 42.0f, 0.0f), FVector(1.25f, 0.32f, 0.04f)), ArrowGlow);
	}
	Piece(FootArrow, CubeMesh, FTransform(FRotator::ZeroRotator, FVector(-95.0f, 0.0f, 0.0f), FVector(1.3f, 0.26f, 0.04f)), ArrowGlow);
}

void AChaosImpactCraneYardStage::TickMarker(const double Now)
{
	if (!Marker)
	{
		return;
	}
	const TArray<FPlatform>& Platforms = GetPlatforms();
	AChaosImpactCharacter* Player = GetPlayer();
	if (Player && !Player->IsEliminated())
	{
		// The stack they stand on now (or back at the start after a fall).
		const float Feet = static_cast<float>(Player->GetActorLocation().Z - Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		if (IsOverPlatform(0, Player->GetActorLocation(), 0.0f) && FMath::Abs(Feet - GetActorLocation().Z - Platforms[0].Top) < 60.0f)
		{
			Reached = 0;
		}
		for (int32 Index = 1; Index < Platforms.Num(); ++Index)
		{
			if (IsOverPlatform(Index, Player->GetActorLocation(), 0.0f) && FMath::Abs(Feet - GetActorLocation().Z - Platforms[Index].Top) < 60.0f)
			{
				Reached = Index;
			}
		}
	}
	const int32 Next = Reached + 1;
	const bool bShow = Platforms.IsValidIndex(Next) && !bGoalReached;
	Marker->SetVisibility(bShow, true);
	MarkerRing->SetVisibility(bShow);
	const bool bFeet = bShow && Player && !Player->IsEliminated() && !Player->IsAnchorZipping() && Player->GetCharacterMovement()
		&& Player->GetCharacterMovement()->IsMovingOnGround();
	FootArrow->SetVisibility(bFeet, true);
	if (bFeet)
	{
		// Round the player's feet, pointing at where the next anchor will set them down.
		const FVector Feet = Player->GetActorLocation() - FVector(0.0f, 0.0f, Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - 3.0f);
		const FVector Way = (GetLandingPoint(Next) - Feet).GetSafeNormal2D();
		const float Nudge = 12.0f * FMath::Sin(static_cast<float>(Now) * 6.0f);
		FootArrow->SetWorldLocationAndRotation(Feet + Way * (190.0f + Nudge), Way.Rotation());
	}
	if (!bShow)
	{
		return;
	}
	const FPlatform& Before = Platforms[Reached];
	const FPlatform& P = Platforms[Next];
	const float PostHeight = FMath::Max(Before.Top - P.Top, 0.0f) + HandHeight + 160.0f;
	const float Bob = 25.0f * FMath::Sin(static_cast<float>(Now) * 4.0f);
	Marker->SetWorldLocation(GetPostFoot(Next) + FVector(0.0f, 0.0f, PostHeight + 150.0f + Bob));
	Marker->SetWorldRotation(FRotator(0.0f, static_cast<float>(Now) * 90.0f, 0.0f));
	const float Pulse = 1.35f + 0.25f * FMath::Sin(static_cast<float>(Now) * 5.0f);
	MarkerRing->SetWorldLocation(GetLandingPoint(Next) + FVector(0.0f, 0.0f, 2.0f));
	MarkerRing->SetWorldScale3D(FVector(Pulse, Pulse, 0.012f));
}

void AChaosImpactCraneYardStage::BeginPlay()
{
	Super::BeginPlay();
	BuildMaterials();
	RegisterOccupied();
	BuildGround();
	BuildSea();
	BuildPlatforms();
	BuildSniperStacks();
	BuildHazards();
	BuildCranes();
	BuildLamps();
	BuildFillerYard();
	BuildShip();
	BuildGoal();
	BuildSkyline();
	BuildMarker();
	bBuilt = true;
	TickSwings(GetSwingTime());
	int32 Instances = 0;
	for (const UInstancedStaticMeshComponent* Bucket : BucketList)
	{
		Instances += Bucket ? Bucket->GetInstanceCount() : 0;
	}
	UE_LOG(LogChaosImpact, Log, TEXT("Crane yard built: %d platforms, %d enemies, %d pieces in %d batches"), GetPlatforms().Num(), GetSnipers().Num(),
		Instances, BucketList.Num());
	if (HasAuthority())
	{
		SpawnSnipers();
		// The player starts on the girder (whatever the level's player start says).
		FTimerHandle Place;
		GetWorldTimerManager().SetTimer(Place, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			if (AChaosImpactCharacter* Player = GetPlayer())
			{
				SendPlayerToStart(Player, false);
			}
		}), 0.05f, false);
	}
}

// ---------------------------------------------------------------------------------------------------- Playing

AChaosImpactCharacter* AChaosImpactCraneYardStage::GetPlayer() const
{
	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	return PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
}

TArray<AChaosImpactCharacter*> AChaosImpactCraneYardStage::GetSniperCharacters() const
{
	TArray<AChaosImpactCharacter*> Out;
	for (const FSniperState& State : SniperStates)
	{
		Out.Add(State.Character.Get());
	}
	return Out;
}

void AChaosImpactCraneYardStage::SpawnSnipers()
{
	UWorld* World = GetWorld();
	UClass* EnemyClass = LoadClass<AChaosImpactCharacter>(nullptr, CYSniperClass);
	if (!World || !EnemyClass)
	{
		return;
	}
	const TArray<FSniper>& Snipers = GetSnipers();
	for (int32 Index = 0; Index < Snipers.Num(); ++Index)
	{
		const FSniper& S = Snipers[Index];
		FSniperState& State = SniperStates.AddDefaulted_GetRef();
		const FVector Foot = GetActorTransform().TransformPosition(FVector(S.Where, S.Top));
		const FTransform Where(FRotator::ZeroRotator, Foot + FVector(0.0f, 0.0f, 100.0f));
		AChaosImpactCharacter* Enemy = World->SpawnActorDeferred<AChaosImpactCharacter>(EnemyClass, Where, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
		if (Enemy)
		{
			Enemy->bRespawnAfterElimination = true;
			Enemy->CPULevel = EChaosImpactCPULevel::Strongest;
			Enemy->Tags.AddUnique(TEXT("CI.CraneYardSniper"));
			Enemy->FinishSpawning(Where);
			// Played by nothing but this stage: a plain controller holds it, it never walks.
			AAIController* Holder = World->SpawnActor<AAIController>(AAIController::StaticClass());
			if (Holder)
			{
				Holder->Possess(Enemy);
			}
			Enemy->SetCPULevel(EChaosImpactCPULevel::Strongest);
			State.Character = Enemy;
		}
		// Staggered so they do not all fire together.
		State.NextFireAt = 2.0 + Index * 0.37;
		// The warning: a red beam from its hand towards the player, a red ring at its feet, a red flash.
		const auto Part = [&](UStaticMesh* Mesh, UMaterialInterface* Material)
		{
			UStaticMeshComponent* Made = NewObject<UStaticMeshComponent>(this);
			Made->SetMobility(EComponentMobility::Movable);
			Made->SetupAttachment(Root);
			Made->SetStaticMesh(Mesh);
			Made->SetMaterial(0, Material);
			Made->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Made->SetVisibility(false);
			Made->RegisterComponent();
			SniperParts.Add(Made);
			return Made;
		};
		State.Beam = Part(CylinderMesh, Materials.FindRef(TEXT("Warn")));
		State.Ring = Part(CylinderMesh, Materials.FindRef(TEXT("Warn")));
		State.Flash = Light(FVector(S.Where, S.Top + 260.0f), FLinearColor(1.0f, 0.1f, 0.05f), 0.0f, 900.0f);
		SniperParts.Add(State.Flash);
	}
}

bool AChaosImpactCraneYardStage::IsAmongNearest(const int32 Index, const int32 Count) const
{
	// Among the Count enemies nearest the player (only these open fire: several at once, never all).
	const AChaosImpactCharacter* Player = GetPlayer();
	if (!Player || !SniperStates.IsValidIndex(Index) || !SniperStates[Index].Character.IsValid())
	{
		return false;
	}
	const FVector At = Player->GetActorLocation();
	const double Mine = FVector::DistSquared2D(SniperStates[Index].Character->GetActorLocation(), At);
	int32 Closer = 0;
	for (int32 Other = 0; Other < SniperStates.Num(); ++Other)
	{
		const AChaosImpactCharacter* Enemy = SniperStates[Other].Character.Get();
		if (Other != Index && Enemy && FVector::DistSquared2D(Enemy->GetActorLocation(), At) < Mine)
		{
			++Closer;
		}
	}
	return Closer < Count;
}

void AChaosImpactCraneYardStage::TickSnipers(const double Now, const float DeltaSeconds)
{
	AChaosImpactCharacter* Player = GetPlayer();
	// Not at the player on the girder at the start (somewhere to look the way over in peace).
	const bool bOnGirder = Player && IsOverPlatform(0, Player->GetActorLocation(), 300.0f);
	const bool bPlayerOk = Player && !Player->IsEliminated() && !bGoalReached && !bOnGirder;
	const TArray<FSniper>& Snipers = GetSnipers();
	constexpr float Range = 4600.0f;
	constexpr double WarnSeconds = 0.6;
	constexpr double ChargeSeconds = 0.25;
	for (int32 Index = 0; Index < SniperStates.Num(); ++Index)
	{
		FSniperState& State = SniperStates[Index];
		AChaosImpactCharacter* Enemy = State.Character.Get();
		const bool bWarning = Now < State.WarnUntil;
		if (State.Beam)
		{
			State.Beam->SetVisibility(bWarning);
			State.Ring->SetVisibility(bWarning);
		}
		if (State.Flash)
		{
			State.Flash->SetIntensity(bWarning ? 9000.0f * (0.6f + 0.4f * FMath::Sin(static_cast<float>(Now) * 30.0f)) : 0.0f);
		}
		if (!Enemy || Enemy->IsEliminated())
		{
			continue;
		}
		// Back on its stack if anything moved it.
		const FVector Home = GetActorTransform().TransformPosition(FVector(Snipers[Index].Where, Snipers[Index].Top + 98.0f));
		if (FVector::Dist2D(Enemy->GetActorLocation(), Home) > 160.0f)
		{
			Enemy->SetActorLocation(Home, false, nullptr, ETeleportType::TeleportPhysics);
		}
		if (Enemy->IsDriving())
		{
			// Steering its drive ball after the player (where they will be when it gets there).
			if (AChaosImpactBall* Driven = Enemy->GetDrivenBall(); Driven && bPlayerOk)
			{
				const FVector BallAt = Driven->GetActorLocation();
				const float Speed = FMath::Max(static_cast<float>(Driven->GetVelocity().Size2D()), 1.0f);
				const float Arrival = FMath::Min(static_cast<float>(FVector::Dist2D(BallAt, Player->GetActorLocation())) / Speed, 0.8f);
				const FVector Aim = Player->GetActorLocation() + Player->GetVelocity() * Arrival * 0.55f;
				Enemy->SetAIDriveSteer((Aim - BallAt).GetSafeNormal2D());
			}
			State.NextFireAt = Now + 1.1 + CYHash(Index, static_cast<int32>(Now)) * 1.2;
			continue;
		}
		if (!bPlayerOk || bSnipersQuiet)
		{
			State.WarnUntil = -1.0;
			State.ReleaseAt = -1.0;
			if (Enemy->IsChargingThrow())
			{
				Enemy->CancelChargingThrow();
			}
			continue;
		}
		const FVector ToPlayer = Player->GetActorLocation() - Enemy->GetActorLocation();
		Enemy->SetActorRotation(FRotator(0.0f, ToPlayer.Rotation().Yaw, 0.0f));
		Enemy->SetAIAimDirection(ToPlayer.GetSafeNormal2D());
		if (State.ReleaseAt > 0.0 && Now >= State.ReleaseAt)
		{
			Enemy->EndThrowInput();
			State.ReleaseAt = -1.0;
			continue;
		}
		if (State.WarnUntil > 0.0 && Now >= State.WarnUntil)
		{
			State.WarnUntil = -1.0;
			if (Enemy->GetCarriedBallCount() == 0)
			{
				Enemy->GiveContestedPickup(EChaosImpactBallType::Drive);
			}
			Enemy->BeginThrowInput();
			State.ReleaseAt = Now + ChargeSeconds;
			continue;
		}
		if (State.WarnUntil < 0.0 && State.ReleaseAt < 0.0 && Now >= State.NextFireAt && ToPlayer.Size2D() < Range && IsAmongNearest(Index, 3))
		{
			State.WarnUntil = Now + WarnSeconds;
			State.NextFireAt = Now + 30.0;
		}
		if (bWarning && State.Beam)
		{
			// The beam from its hand to the player, and the ring round its feet.
			const FVector From = Enemy->GetActorLocation() + FVector(0.0f, 0.0f, 55.0f);
			const FVector To = Player->GetActorLocation();
			const FVector Line = To - From;
			const float Pulse = 0.5f + 0.5f * FMath::Sin(static_cast<float>(Now) * 26.0f);
			State.Beam->SetWorldTransform(FTransform(FRotationMatrix::MakeFromZ(Line.GetSafeNormal()).Rotator(), (From + To) * 0.5f,
				FVector(0.05f + 0.04f * Pulse, 0.05f + 0.04f * Pulse, Line.Size() / 100.0f)));
			const float Grow = 1.0f - static_cast<float>((State.WarnUntil - Now) / WarnSeconds);
			State.Ring->SetWorldTransform(FTransform(FRotator::ZeroRotator, Enemy->GetActorLocation() - FVector(0.0f, 0.0f, 94.0f),
				FVector(1.6f + Grow * 2.2f, 1.6f + Grow * 2.2f, 0.02f)));
		}
	}
}

void AChaosImpactCraneYardStage::SendPlayerToStart(AChaosImpactCharacter* Player, const bool bFell)
{
	if (!Player)
	{
		return;
	}
	// Any anchor of theirs still out goes (no carrying on to where it was thrown).
	for (TActorIterator<AChaosImpactHook> It(GetWorld()); It; ++It)
	{
		if (It->GetSourcePawn() == Player)
		{
			It->Destroy();
		}
	}
	const FVector Start = GetStartPoint() + FVector(0.0f, 0.0f, Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.0f);
	Player->SetRespawnPoint(Start, GetActorRotation());
	Player->SetActorLocationAndRotation(Start, GetActorRotation(), false, nullptr, ETeleportType::TeleportPhysics);
	if (UCharacterMovementComponent* Movement = Player->GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->SetMovementMode(MOVE_Walking);
	}
	if (APlayerController* PC = Cast<APlayerController>(Player->GetController()); PC && bFell && PC->PlayerCameraManager)
	{
		PC->PlayerCameraManager->StartCameraFade(1.0f, 0.0f, 0.45f, FLinearColor::Black, false, false);
	}
}

void AChaosImpactCraneYardStage::HandleFall(AChaosImpactCharacter* Player)
{
	++Falls;
	Player->LoseHealthToStage(1.0f);
	if (Player->GetHealth() <= 0.0f || Reached <= 0)
	{
		// No health left (or not yet off the girder): from the start, health full.
		UE_LOG(LogChaosImpact, Log, TEXT("Crane yard: fell (%d), back to the start"), Falls);
		Player->RefillHealth();
		SendPlayerToStart(Player, false);
		Reached = 0;
	}
	else
	{
		// Back on the stack they threw from, where its anchor had put them down.
		UE_LOG(LogChaosImpact, Log, TEXT("Crane yard: fell (%d), back on stack %d, health %.0f"), Falls, Reached, Player->GetHealth());
		for (TActorIterator<AChaosImpactHook> It(GetWorld()); It; ++It)
		{
			if (It->GetSourcePawn() == Player)
			{
				It->Destroy();
			}
		}
		Player->BreakAnchorZip();
		const FVector Back = GetLandingPoint(Reached) + FVector(0.0f, 0.0f, Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 4.0f);
		Player->SetActorLocation(Back, false, nullptr, ETeleportType::TeleportPhysics);
		if (UCharacterMovementComponent* Movement = Player->GetCharacterMovement())
		{
			Movement->StopMovementImmediately();
			Movement->SetMovementMode(MOVE_Walking);
		}
	}
	if (APlayerController* PC = Cast<APlayerController>(Player->GetController()); PC && PC->PlayerCameraManager)
	{
		PC->PlayerCameraManager->StartCameraFade(1.0f, 0.0f, 0.45f, FLinearColor::Black, false, false);
	}
}

void AChaosImpactCraneYardStage::TickPlayer(const double Now)
{
	AChaosImpactCharacter* Player = GetPlayer();
	if (!Player || !HasAuthority() || bGoalReached)
	{
		return;
	}
	// Hit by a ball in the middle of an anchor's flight: the chain breaks and down they go.
	if (Player->GetHealth() < LastPlayerHealth && Player->IsAnchorZipping())
	{
		Player->BreakAnchorZip();
		UE_LOG(LogChaosImpact, Log, TEXT("Crane yard: hit in flight, the anchor let go"));
	}
	LastPlayerHealth = Player->GetHealth();
	// A wider view here: the next stack is far off (and the towers of boxes do not pull the camera in).
	Player->SetStageCameraScale(1.55f);
	if (USpringArmComponent* Boom = Player->GetCameraBoom())
	{
		Boom->bDoCollisionTest = false;
	}
	// Always an anchor in hand: a new one the moment one is thrown.
	if (!Player->IsEliminated() && Player->GetCarriedBallCount() == 0)
	{
		Player->GiveContestedPickup(EChaosImpactBallType::Anchor);
	}
	// Fallen to the quay, the deck or the sea.
	const float Feet = static_cast<float>(GetActorTransform().InverseTransformPosition(Player->GetActorLocation()).Z)
		- Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	if (!Player->IsEliminated() && Feet < FallHeight)
	{
		HandleFall(Player);
		return;
	}
	// Knocked out (by the drive balls): they come back where the respawn point says, the start.
	// The goal: standing on the last stack.
	const int32 Last = GetPlatforms().Num() - 1;
	if (!Player->IsEliminated() && IsOverPlatform(Last, Player->GetActorLocation(), -40.0f)
		&& Feet > GetPlatforms()[Last].Top - 30.0f && Player->GetCharacterMovement() && Player->GetCharacterMovement()->IsMovingOnGround())
	{
		bGoalReached = true;
		UE_LOG(LogChaosImpact, Log, TEXT("Crane yard: goal reached (%d falls)"), Falls);
		for (const FSniperState& State : SniperStates)
		{
			if (AChaosImpactCharacter* Enemy = State.Character.Get(); Enemy && Enemy->IsChargingThrow())
			{
				Enemy->CancelChargingThrow();
			}
		}
		if (AChaosImpactPlayerController* PC = Cast<AChaosImpactPlayerController>(Player->GetController()))
		{
			PC->NotifySoloStageCleared();
		}
	}
}

void AChaosImpactCraneYardStage::TickSwings(const double Now)
{
	for (FSwing& Swing : Swings)
	{
		if (!Swing.Carrier)
		{
			continue;
		}
		const FTransform Frame = GetPlatformFrame(Swing.Platform);
		Swing.Carrier->SetWorldLocationAndRotation(Frame.GetLocation(), Frame.GetRotation());
		// The cables from the trolley (which runs along with the box) down to the spreader.
		for (int32 Cable = 0; Cable < Swing.Cables.Num(); ++Cable)
		{
			const FVector Foot = Frame.TransformPosition(Swing.CableFeet[Cable]);
			const FVector Top = Frame.TransformPosition(Swing.CableTops[Cable]);
			const FVector Line = Top - Foot;
			Swing.Cables[Cable]->SetWorldTransform(FTransform(FRotationMatrix::MakeFromZ(Line.GetSafeNormal()).Rotator(), (Top + Foot) * 0.5f,
				FVector(0.05f, 0.05f, Line.Size() / 100.0f)));
		}
	}
}

void AChaosImpactCraneYardStage::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bBuilt)
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	TickSwings(GetSwingTime());
	TickHazards(Now);
	TickMarker(Now);
	// The anchor rings breathe; the goal's beam shimmers.
	const float Breath = 0.75f + 0.25f * FMath::Sin(static_cast<float>(Now) * 3.0f);
	for (UStaticMeshComponent* Ring : PostRings)
	{
		if (Ring)
		{
			const FVector Scale = Ring->GetRelativeScale3D();
			Ring->SetRelativeScale3D(FVector(Scale.X, Scale.Y, 0.22f + 0.1f * Breath));
		}
	}
	// A post between the camera and the player standing right by it is not drawn (it stays there to throw at).
	if (const AChaosImpactCharacter* Player = GetPlayer())
	{
		const APlayerController* PC = Cast<APlayerController>(Player->GetController());
		const FVector Look = PC && PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraRotation().Vector().GetSafeNormal2D() : FVector::ForwardVector;
		for (UStaticMeshComponent* Shaft : PostShafts)
		{
			if (!Shaft)
			{
				continue;
			}
			const FVector ToPost = Shaft->GetComponentLocation() - Player->GetActorLocation();
			const bool bInTheWay = ToPost.Size2D() < 330.0f && FVector::DotProduct(ToPost.GetSafeNormal2D(), Look) < 0.2f;
			if (Shaft->IsVisible() == bInTheWay)
			{
				Shaft->SetVisibility(!bInTheWay, true);
			}
		}
	}
	if (GoalBeam)
	{
		GoalBeam->SetRelativeScale3D(FVector(0.5f + 0.15f * Breath, 0.5f + 0.15f * Breath, 30.0f));
	}
	if (HasAuthority())
	{
		TickSnipers(Now, DeltaSeconds);
		TickPlayer(Now);
	}
}
