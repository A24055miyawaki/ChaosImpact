#include "ChaosImpactStageDecor.h"

#include "ChaosImpactBallTypes.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "Materials/MaterialInstanceDynamic.h"

// =====================================================================================================================
// Drifting flakes

AChaosImpactDriftingFlakes::AChaosImpactDriftingFlakes()
{
	PrimaryActorTick.bCanEverTick = true;
	Flakes = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Flakes"));
	RootComponent = Flakes;
	Flakes->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Flakes->SetCastShadow(false);
	Flakes->SetMobility(EComponentMobility::Movable);
	Flakes->SetCanEverAffectNavigation(false);
}

void AChaosImpactDriftingFlakes::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Flakes->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")));
	UMaterialInterface* Look = Material ? Material.Get()
		: LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/ChaosImpact/solo/WorldMap/Materials/M_CI_MapLit.M_CI_MapLit"));
	if (Look)
	{
		Flakes->SetMaterial(0, Look);
	}
	Seed();
}

void AChaosImpactDriftingFlakes::Seed()
{
	FRandomStream Random(Count * 7 + 3);
	Start.Reset();
	Phase.Reset();
	Flakes->ClearInstances();
	TArray<FTransform> Transforms;
	for (int32 I = 0; I < Count; ++I)
	{
		const FVector At(Random.FRandRange(-Area.X, Area.X), Random.FRandRange(-Area.Y, Area.Y), Random.FRandRange(-Area.Z, Area.Z));
		Start.Add(At);
		Phase.Add(Random.FRandRange(0.0f, 2.0f * PI));
		const float S = Size / 100.0f * Random.FRandRange(1.0f, 1.5f);
		Transforms.Add(FTransform(FRotator::ZeroRotator, At, FVector(S)));
	}
	Flakes->AddInstances(Transforms, false);
}

void AChaosImpactDriftingFlakes::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Start.Num() != Flakes->GetInstanceCount())
	{
		Seed();
	}
	Elapsed += DeltaSeconds;
	const FVector Velocity = Wind - FVector(0.0f, 0.0f, FallSpeed);
	TArray<FTransform> Transforms;
	Transforms.Reserve(Start.Num());
	const auto Wrap = [](const double Value, const double Half)
	{
		const double Span = Half * 2.0;
		return FMath::Fmod(FMath::Fmod(Value + Half, Span) + Span, Span) - Half;
	};
	for (int32 I = 0; I < Start.Num(); ++I)
	{
		const float T = Elapsed + Phase[I];
		FVector At = Start[I] + Velocity * Elapsed;
		At.X = Wrap(At.X + FMath::Sin(T * 1.3f) * Sway, Area.X);
		At.Y = Wrap(At.Y + FMath::Cos(T * 0.9f) * Sway, Area.Y);
		At.Z = Wrap(At.Z, Area.Z);
		FTransform Instance;
		Flakes->GetInstanceTransform(I, Instance, false);
		Instance.SetLocation(At);
		Transforms.Add(Instance);
	}
	Flakes->BatchUpdateInstancesTransforms(0, Transforms, false, true, true);
}

// =====================================================================================================================
// The shima-enaga

AChaosImpactSimaePerch::AChaosImpactSimaePerch()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Body = CreateDefaultSubobject<USceneComponent>(TEXT("Body"));
	Body->SetupAttachment(RootComponent);
}

void AChaosImpactSimaePerch::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	BuildBird();
}

void AChaosImpactSimaePerch::BuildBird()
{
	using namespace ChaosImpactBallTypes;
	for (UStaticMeshComponent* Part : Parts)
	{
		if (IsValid(Part))
		{
			Part->DestroyComponent();
		}
	}
	Parts.Reset();
	Wing = nullptr;
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, SimaeAssets::TexturedMaterial);
	UTexture* Texture = LoadObject<UTexture>(nullptr, SimaeAssets::BirdTexture);
	UMaterialInstanceDynamic* Feathers = Base && Texture ? UMaterialInstanceDynamic::Create(Base, this) : nullptr;
	if (Feathers)
	{
		Feathers->SetTextureParameterValue(TEXT("BodyTexture"), Texture);
	}
	for (const TCHAR* Path : {SimaeAssets::BirdBody, SimaeAssets::BirdTail, SimaeAssets::BirdWing, SimaeAssets::BirdEye, SimaeAssets::BirdCrest})
	{
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, Path);
		if (!Mesh)
		{
			continue;
		}
		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
		Part->SetupAttachment(Body);
		Part->SetStaticMesh(Mesh);
		Part->SetRelativeScale3D(FVector(BirdScale));
		Part->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		Part->SetCanEverAffectNavigation(false);
		if (Feathers)
		{
			for (int32 Slot = 0; Slot < Part->GetNumMaterials(); ++Slot)
			{
				Part->SetMaterial(Slot, Feathers);
			}
		}
		Part->RegisterComponent();
		Parts.Add(Part);
		if (Path == SimaeAssets::BirdWing)
		{
			Wing = Part;
		}
	}
}

void AChaosImpactSimaePerch::BeginPlay()
{
	Super::BeginPlay();
	if (Parts.Num() == 0)
	{
		BuildBird();
	}
	Home = Body->GetRelativeLocation();
	HomeRotation = Body->GetRelativeRotation();
	// Each bird keeps its own time.
	Clock = FMath::Frac(GetActorLocation().X * 0.013f + GetActorLocation().Y * 0.007f) * 5.0f;
	NextHop = Clock + 1.0f;
}

void AChaosImpactSimaePerch::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Clock += DeltaSeconds;
	const FVector Here = GetActorLocation();
	bool bSomeoneNear = false;
	if (ShyDistance > 0.0f)
	{
		for (TActorIterator<ACharacter> It(GetWorld()); It; ++It)
		{
			if (FVector::DistSquared2D(It->GetActorLocation(), Here) < FMath::Square(ShyDistance)
				&& FMath::Abs(It->GetActorLocation().Z - Here.Z) < 400.0f)
			{
				bSomeoneNear = true;
				break;
			}
		}
	}
	if (FlightTime < 0.0f && bSomeoneNear)
	{
		// Away: up and out, from the one coming.
		FlightTime = 0.0f;
		const float Angle = FMath::Frac(Clock * 0.37f) * 2.0f * PI;
		FlightWay = FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f);
	}
	if (FlightTime >= 0.0f)
	{
		FlightTime += DeltaSeconds;
		constexpr float Away = 1.6f;
		constexpr float Gone = 7.0f;
		constexpr float Land = 0.9f;
		FVector Offset = FVector::ZeroVector;
		if (FlightTime < Away)
		{
			const float T = FlightTime / Away;
			Offset = FlightWay * (T * 700.0f) + FVector(0.0f, 0.0f, T * T * 600.0f + T * 150.0f);
			SetActorHiddenInGame(false);
		}
		else if (FlightTime < Away + Gone || (bSomeoneNear && FlightTime < Away + Gone + 0.1f))
		{
			SetActorHiddenInGame(true);
			if (bSomeoneNear)
			{
				FlightTime = Away + 0.5f;
			}
		}
		else if (FlightTime < Away + Gone + Land)
		{
			const float T = 1.0f - (FlightTime - Away - Gone) / Land;
			Offset = -FlightWay * (T * 300.0f) + FVector(0.0f, 0.0f, T * 260.0f);
			SetActorHiddenInGame(false);
		}
		else
		{
			FlightTime = -1.0f;
		}
		Body->SetRelativeLocationAndRotation(Home + Offset, FRotator(0.0f, FlightWay.Rotation().Yaw, 0.0f));
		if (Wing)
		{
			Wing->SetRelativeScale3D(FVector(BirdScale, BirdScale, BirdScale * (0.35f + 0.65f * FMath::Abs(FMath::Sin(Clock * 30.0f)))));
		}
		return;
	}
	// Sitting: a hop now and then, looking about.
	if (Clock >= NextHop)
	{
		HopTime = 0.0f;
		NextHop = Clock + 1.4f + FMath::Frac(Clock * 0.71f) * 2.6f;
		LookTarget = (FMath::Frac(Clock * 1.37f) - 0.5f) * 80.0f;
	}
	float Lift = 0.0f;
	if (HopTime >= 0.0f)
	{
		HopTime += DeltaSeconds;
		const float T = HopTime / 0.28f;
		Lift = T < 1.0f ? FMath::Sin(T * PI) * 9.0f * BirdScale : 0.0f;
		if (T >= 1.0f)
		{
			HopTime = -1.0f;
		}
	}
	Look = FMath::FInterpTo(Look, LookTarget, DeltaSeconds, 8.0f);
	Body->SetRelativeLocationAndRotation(Home + FVector(0.0f, 0.0f, Lift), HomeRotation + FRotator(0.0f, Look, 0.0f));
	if (Wing)
	{
		Wing->SetRelativeScale3D(FVector(BirdScale));
	}
}
