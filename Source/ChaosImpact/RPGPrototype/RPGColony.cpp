#include "RPGColony.h"
#include "RPGTutorial.h"
#include "RPGWorldLayout.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"

// The colony's look as it grows (2026-11). The colony stands round the adventure map's origin (RPGWorldLayout "Colony").

ARPGColonyDecor::ARPGColonyDecor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 1.0f;
	SetReplicates(false);
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("ColonyRoot")));
}

ARPGColonyDecor* ARPGColonyDecor::Ensure(UWorld* World)
{
	if (!World) { return nullptr; }
	for (TActorIterator<ARPGColonyDecor> It(World); It; ++It) { return *It; }
	FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World->SpawnActor<ARPGColonyDecor>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
}

UStaticMeshComponent* ARPGColonyDecor::Part(const TCHAR* Mesh, const TCHAR* Material, float X, float Y, float Z, const FVector& Size, float Yaw, bool bSolid)
{
	const FString MeshPath = FString(Mesh).StartsWith(TEXT("Kit"))
		? FString::Printf(TEXT("/Game/ChaosImpact/RPGPrototype/Adventure/Meshes/SM_%s.SM_%s"), Mesh, Mesh) : FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), Mesh, Mesh);
	auto* Piece = NewObject<UStaticMeshComponent>(this);
	Piece->SetupAttachment(GetRootComponent());
	Piece->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, *MeshPath));
	if (UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("/Game/ChaosImpact/RPGPrototype/Adventure/Materials/M_%s.M_%s"), Material, Material))) { Piece->SetMaterial(0, M); }
	Piece->SetRelativeLocation(FVector(X, Y, RPGWorld::Height(X, Y) + Z)); Piece->SetRelativeRotation(FRotator(0, Yaw, 0)); Piece->SetRelativeScale3D(Size / 100);
	Piece->SetCollisionEnabled(bSolid ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	Piece->SetCollisionResponseToAllChannels(ECR_Block);
	Piece->SetMobility(EComponentMobility::Movable);
	Piece->RegisterComponent();
	Pieces.Add(Piece);
	return Piece;
}

void ARPGColonyDecor::Light(float X, float Y, float Z, const FLinearColor& Color, float Intensity, float Radius)
{
	auto* Lamp = NewObject<UPointLightComponent>(this);
	Lamp->SetupAttachment(GetRootComponent());
	Lamp->SetRelativeLocation(FVector(X, Y, RPGWorld::Height(X, Y) + Z));
	Lamp->SetLightColor(Color); Lamp->SetIntensity(Intensity); Lamp->SetAttenuationRadius(Radius); Lamp->SetCastShadows(false);
	Lamp->RegisterComponent();
	Lights.Add(Lamp);
}

void ARPGColonyDecor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Clock += DeltaSeconds;
	const int32 Stage = RPGColony::WorldStage(GetWorld());
	if (Stage != Built) { Build(Stage); }
}

void ARPGColonyDecor::Build(int32 Stage)
{
	for (UStaticMeshComponent* Piece : Pieces) { if (Piece) { Piece->DestroyComponent(); } }
	for (UPointLightComponent* Lamp : Lights) { if (Lamp) { Lamp->DestroyComponent(); } }
	Pieces.Reset(); Lights.Reset();
	Built = Stage;
	if (Stage >= 1)
	{
		// Flower beds round the square, banners at the four gates.
		for (int32 I = 0; I < 8; ++I)
		{
			const float A = I * UE_TWO_PI / 8 + 0.2f;
			const float X = 250 + FMath::Cos(A) * 1050, Y = FMath::Sin(A) * 860;
			Part(TEXT("Cube"), TEXT("Soil"), X, Y, 10, FVector(160, 90, 20), FMath::RadiansToDegrees(A) + 90);
			for (int32 J = 0; J < 4; ++J) { Part(TEXT("KitGem"), J % 2 ? TEXT("FlowerPink") : TEXT("FlowerGold"), X + FMath::Cos(A + 1.57f) * (J - 1.5f) * 35, Y + FMath::Sin(A + 1.57f) * (J - 1.5f) * 35, 30, FVector(24, 24, 20)); }
		}
		for (const FVector2D& Gate : {FVector2D(1950, 450), FVector2D(-1950, 450), FVector2D(300, 1750), FVector2D(300, -1750)})
		{
			Part(TEXT("KitCylinder"), TEXT("Timber"), Gate.X, Gate.Y, 200, FVector(16, 16, 400));
			Part(TEXT("Cube"), TEXT("Banner"), Gate.X, Gate.Y + 40, 320, FVector(8, 80, 140));
		}
	}
	if (Stage >= 2)
	{
		// A market: two stalls with awnings and crates.
		for (const float Y : {-380.0f, 380.0f})
		{
			Part(TEXT("Cube"), TEXT("Planks"), 1050, Y, 45, FVector(200, 120, 90), 0, true);
			Part(TEXT("Cube"), Y < 0 ? TEXT("RoofClay") : TEXT("RoofTeal"), 1050, Y, 230, FVector(240, 170, 12), 0);
			for (const float S : {-1.0f, 1.0f}) { Part(TEXT("KitCylinder"), TEXT("Timber"), 1050 + S * 100, Y + 70, 120, FVector(10, 10, 230)); }
			Part(TEXT("KitBlob"), TEXT("LeavesLight"), 1050, Y - 20, 100, FVector(60, 50, 30));
			Part(TEXT("KitGem"), TEXT("FlowerGold"), 1080, Y + 20, 100, FVector(30, 30, 24));
		}
	}
	if (Stage >= 3)
	{
		// A new house by the east road, lanterns round the square.
		Part(TEXT("Cube"), TEXT("Plaster"), 1300, -1100, 160, FVector(420, 360, 320), 30, true);
		for (const float S : {-1.0f, 1.0f}) { Part(TEXT("Cube"), TEXT("RoofTeal"), 1300 + S * 60, -1100 - S * 100, 360, FVector(460, 230, 30), 30); }
		Part(TEXT("Cube"), TEXT("GlowWarm"), 1180, -1210, 160, FVector(60, 8, 80), 30);
		for (int32 I = 0; I < 6; ++I)
		{
			const float A = I * UE_TWO_PI / 6;
			const float X = 250 + FMath::Cos(A) * 800, Y = FMath::Sin(A) * 650;
			Part(TEXT("KitCylinder"), TEXT("Timber"), X, Y, 110, FVector(14, 14, 220));
			Part(TEXT("KitGem"), TEXT("GlowWarm"), X, Y, 230, FVector(26, 26, 36));
			Light(X, Y, 240, FLinearColor(1.0f, 0.7f, 0.4f), 1500, 600);
		}
	}
	if (Stage >= 4)
	{
		// A statue of a hero with a ball raised, and festival lights in many colours.
		Part(TEXT("KitCylinder"), TEXT("Stone"), 600, -450, 60, FVector(160, 160, 120), 0, true);
		Part(TEXT("KitFrustum"), TEXT("Gold"), 600, -450, 220, FVector(70, 70, 200), 0, true);
		Part(TEXT("Sphere"), TEXT("Gold"), 600, -450, 360, FVector(70, 70, 70));
		const TCHAR* Colours[] = {TEXT("GlowWarm"), TEXT("GlowTeal"), TEXT("FlowerPink"), TEXT("FlowerGold")};
		for (int32 I = 0; I < 12; ++I)
		{
			const float A = I * UE_TWO_PI / 12;
			Part(TEXT("Sphere"), Colours[I % 4], 250 + FMath::Cos(A) * 950, FMath::Sin(A) * 780, 300, FVector(30, 30, 30));
		}
		Light(600, -450, 420, FLinearColor(1.0f, 0.85f, 0.5f), 4000, 900);
	}
}
