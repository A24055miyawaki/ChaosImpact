#include "ChaosImpactDuelStage.h"
#include "ChaosImpactGameState.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/DirectionalLight.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	const FName DuelPaintTag(TEXT("CIDuelPaint"));

	bool ReadDuelPaint(const UStaticMeshComponent* Block, uint8& OutPaint)
	{
		for (const FName& Tag : Block->ComponentTags)
		{
			const FString Text = Tag.ToString();
			if (Text.StartsWith(DuelPaintTag.ToString()))
			{
				OutPaint = static_cast<uint8>(FCString::Atoi(*Text.Mid(DuelPaintTag.ToString().Len())));
				return true;
			}
		}
		return false;
	}
}

AChaosImpactDuelStage::AChaosImpactDuelStage()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;
	// Clients move their own characters, so they need the walls themselves, not just the server.
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);
	SetNetUpdateFrequency(1.0f);
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Box(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Chamfer(TEXT("/Game/LevelPrototyping/Meshes/SM_ChamferCube.SM_ChamferCube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Game/LevelPrototyping/Meshes/SM_Cylinder.SM_Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Ring(
		TEXT("/Game/LevelPrototyping/Interactable/JumpPad/Assets/Meshes/SM_CircularBand.SM_CircularBand"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Flat(TEXT("/Game/LevelPrototyping/Materials/M_FlatCol.M_FlatCol"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Glowing(TEXT("/Game/ChaosImpact/Versus/M_StageGlow.M_StageGlow"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MetalFloor(
		TEXT("/Game/ChaosImpact/Versus/M_StageMetalFloor.M_StageMetalFloor"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Panel(TEXT("/Game/ChaosImpact/Versus/Splash/M_CI_PanelWall.M_CI_PanelWall"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Gloss(TEXT("/Game/ChaosImpact/Versus/Splash/M_CI_GlossPaint.M_CI_GlossPaint"));
	BoxMesh = Box.Object;
	ChamferMesh = Chamfer.Succeeded() ? Chamfer.Object : Box.Object;
	CylinderMesh = Cylinder.Succeeded() ? Cylinder.Object : Box.Object;
	RingMesh = Ring.Succeeded() ? Ring.Object : nullptr;
	FlatMaterial = Flat.Object;
	GlowMaterial = Glowing.Succeeded() ? Glowing.Object : Flat.Object;
	FloorMaterial = MetalFloor.Succeeded() ? MetalFloor.Object : Flat.Object;
	WallMaterial = Panel.Succeeded() ? Panel.Object : Flat.Object;
	PaintMaterial = Gloss.Succeeded() ? Gloss.Object : Flat.Object;

	// Starts first (red, then blue), then spares on each half.
	SpawnPointOffsets = {FVector(-1500.0f, 0.0f, 0.0f), FVector(1500.0f, 0.0f, 0.0f),
		FVector(-1500.0f, 850.0f, 0.0f), FVector(1500.0f, -850.0f, 0.0f), FVector(-1500.0f, -850.0f, 0.0f), FVector(1500.0f, 850.0f, 0.0f)};
	// One pad just ahead of each start, two on the centre line.
	BallPointOffsets = {FVector(-1240.0f, 0.0f, 0.0f), FVector(1240.0f, 0.0f, 0.0f), FVector(0.0f, 640.0f, 0.0f), FVector(0.0f, -640.0f, 0.0f)};

	constexpr float Half = DuelHalfExtent;
	AddBlock(TEXT("Floor"), Floor, FVector(0.0f, 0.0f, -12.0f), FVector(Half * 2.0f + 80.0f, Half * 2.0f + 80.0f, 24.0f));
	// The two halves, faintly in their side's colour, and the line between them.
	AddMirrored(TEXT("Half"), HalfRed, HalfBlue, FVector(Half * 0.5f + 6.0f, 0.0f, 0.5f), FVector(Half - 12.0f, Half * 2.0f - 24.0f, 1.0f), 0.0f, false);
	AddBlock(TEXT("CentreLine"), GlowGold, FVector(0.0f, 0.0f, 1.2f), FVector(26.0f, Half * 2.0f - 24.0f, 1.4f), 0.0f, false);
	AddRing(TEXT("CentreRing"), GlowGold, FVector(0.0f, 0.0f, 1.0f), 430.0f, 10.0f);
	AddRing(TEXT("CentreRingInner"), GlowWhite, FVector(0.0f, 0.0f, 1.0f), 120.0f, 6.0f);
	// Where each player starts: a ring, and a chevron of light pointing at the opponent.
	AddRing(TEXT("StartRingBlue"), GlowBlue, FVector(1500.0f, 0.0f, 1.0f), 170.0f, 9.0f);
	AddRing(TEXT("StartRingRed"), GlowRed, FVector(-1500.0f, 0.0f, 1.0f), 170.0f, 9.0f);
	for (const float Y : {-1.0f, 1.0f})
	{
		AddMirrored(FString::Printf(TEXT("Chevron%s"), Y < 0.0f ? TEXT("L") : TEXT("R")), GlowRed, GlowBlue,
			FVector(1720.0f, Y * 90.0f, 1.3f), FVector(150.0f, 16.0f, 1.4f), Y * 32.0f, false);
	}
	// Side lanes painted along the long walls.
	for (const float Y : {-1.0f, 1.0f})
	{
		AddMirrored(FString::Printf(TEXT("Lane%s"), Y < 0.0f ? TEXT("S") : TEXT("N")), GlowRed, GlowBlue,
			FVector(Half * 0.5f, Y * (Half - 120.0f), 1.1f), FVector(Half - 220.0f, 10.0f, 1.2f), 0.0f, false);
	}

	// The walls, each with a lit trim along its top in the colour of the half it bounds.
	constexpr float WallHeight = 300.0f;
	for (const float Y : {-1.0f, 1.0f})
	{
		const FString Side = Y < 0.0f ? TEXT("S") : TEXT("N");
		AddBlock(TEXT("Wall") + Side, Wall, FVector(0.0f, Y * (Half + 20.0f), WallHeight * 0.5f), FVector(Half * 2.0f + 80.0f, 40.0f, WallHeight));
		AddMirrored(TEXT("WallTrim") + Side, GlowRed, GlowBlue, FVector(Half * 0.5f, Y * (Half - 2.0f), WallHeight - 14.0f),
			FVector(Half, 6.0f, 12.0f), 0.0f, false);
		AddMirrored(TEXT("WallBase") + Side, GlowRed, GlowBlue, FVector(Half * 0.5f, Y * (Half - 2.0f), 8.0f),
			FVector(Half, 5.0f, 5.0f), 0.0f, false);
	}
	AddMirrored(TEXT("WallEnd"), Wall, Wall, FVector(Half + 20.0f, 0.0f, WallHeight * 0.5f), FVector(40.0f, Half * 2.0f, WallHeight));
	AddMirrored(TEXT("WallEndTrim"), GlowRed, GlowBlue, FVector(Half - 2.0f, 0.0f, WallHeight - 14.0f), FVector(6.0f, Half * 2.0f, 12.0f), 0.0f, false);
	AddMirrored(TEXT("WallEndBase"), GlowRed, GlowBlue, FVector(Half - 2.0f, 0.0f, 8.0f), FVector(5.0f, Half * 2.0f, 5.0f), 0.0f, false);

	// A low wall in front of each start (jumpable), with a lit cap.
	AddMirrored(TEXT("LowWall"), Cover, Cover, FVector(1000.0f, 0.0f, 47.5f), FVector(80.0f, 480.0f, 95.0f), 0.0f, true, ChamferMesh);
	AddMirrored(TEXT("LowWallCap"), GlowRed, GlowBlue, FVector(1000.0f, 0.0f, 96.0f), FVector(84.0f, 484.0f, 3.0f), 0.0f, false);
	// Four pillars around the middle.
	for (const float Y : {-1.0f, 1.0f})
	{
		const FString Side = Y < 0.0f ? TEXT("S") : TEXT("N");
		AddMirrored(TEXT("Pillar") + Side, Pillar, Pillar, FVector(560.0f, Y * 640.0f, 140.0f), FVector(200.0f, 200.0f, 280.0f), 0.0f, true, ChamferMesh);
		AddMirrored(TEXT("PillarBand") + Side, GlowRed, GlowBlue, FVector(560.0f, Y * 640.0f, 210.0f), FVector(206.0f, 206.0f, 10.0f), 0.0f, false);
		// Angled bank walls in the corners: cover, and a way to bounce a throw round it.
		AddMirrored(TEXT("Bank") + Side, Cover, Cover, FVector(1220.0f, Y * 1230.0f, 85.0f), FVector(560.0f, 50.0f, 170.0f), Y * -40.0f, true, ChamferMesh);
		AddMirrored(TEXT("BankCap") + Side, GlowRed, GlowBlue, FVector(1220.0f, Y * 1230.0f, 171.0f), FVector(564.0f, 54.0f, 3.0f), Y * -40.0f, false);
		// A short block on each end of the centre line.
		AddBlock(TEXT("CentreBlock") + Side, Cover, FVector(0.0f, Y * 1250.0f, 60.0f), FVector(320.0f, 80.0f, 120.0f), 0.0f, true, ChamferMesh);
		AddBlock(TEXT("CentreBlockCap") + Side, GlowGold, FVector(0.0f, Y * 1250.0f, 121.0f), FVector(324.0f, 84.0f, 3.0f), 0.0f, false);
	}

	// Each half lit in its own colour, the middle in gold.
	const struct { const TCHAR* Name; FVector At; FLinearColor Color; float Intensity; } Lights[] =
	{
		{TEXT("LightRed"), FVector(-1250.0f, 0.0f, 650.0f), FLinearColor(1.0f, 0.35f, 0.35f), 3200.0f},
		{TEXT("LightBlue"), FVector(1250.0f, 0.0f, 650.0f), FLinearColor(0.35f, 0.6f, 1.0f), 3200.0f},
		{TEXT("LightCentre"), FVector(0.0f, 0.0f, 750.0f), FLinearColor(1.0f, 0.85f, 0.6f), 2600.0f}
	};
	for (const auto& Spec : Lights)
	{
		UPointLightComponent* Light = CreateDefaultSubobject<UPointLightComponent>(Spec.Name);
		Light->SetupAttachment(SceneRoot);
		Light->SetRelativeLocation(Spec.At);
		Light->SetLightColor(Spec.Color);
		Light->SetIntensity(Spec.Intensity);
		Light->SetAttenuationRadius(2600.0f);
		Light->SetCastShadows(false);
		SideLights.Add(Light);
	}
}

UStaticMeshComponent* AChaosImpactDuelStage::AddBlock(const FString& Name, const EPaint Paint, const FVector& Location, const FVector& Size,
	const float Yaw, const bool bCollision, UStaticMesh* Mesh)
{
	UStaticMeshComponent* Block = CreateDefaultSubobject<UStaticMeshComponent>(*Name);
	Block->SetupAttachment(SceneRoot);
	Block->SetStaticMesh(Mesh ? Mesh : BoxMesh.Get());
	Block->SetRelativeLocation(Location);
	Block->SetRelativeRotation(FRotator(0.0f, Yaw, 0.0f));
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
	UMaterialInterface* Surface = IsGlow(Paint) ? GlowMaterial.Get() : Paint == Floor ? FloorMaterial.Get()
		: Paint == Wall ? WallMaterial.Get() : PaintMaterial.Get();
	Block->SetMaterial(0, Surface ? Surface : FlatMaterial.Get());
	Block->ComponentTags.Add(FName(*FString::Printf(TEXT("%s%d"), *DuelPaintTag.ToString(), static_cast<int32>(Paint))));
	Blocks.Add(Block);
	return Block;
}

UStaticMeshComponent* AChaosImpactDuelStage::AddRing(const FString& Name, const EPaint Paint, const FVector& Location, const float Radius,
	const float Thickness)
{
	if (!RingMesh)
	{
		return nullptr;
	}
	// The band mesh is 70 across its middle and 13 thick.
	UStaticMeshComponent* Ring = AddBlock(Name, Paint, Location, FVector::OneVector * 100.0f, 0.0f, false, RingMesh);
	Ring->SetRelativeScale3D(FVector(Radius / 70.0f, Radius / 70.0f, Thickness / 13.0f));
	return Ring;
}

void AChaosImpactDuelStage::AddMirrored(const FString& Name, const EPaint RedPaint, const EPaint BluePaint, const FVector& BlueLocation,
	const FVector& Size, const float BlueYaw, const bool bCollision, UStaticMesh* Mesh)
{
	AddBlock(Name + TEXT("Red"), RedPaint, FVector(-BlueLocation.X, BlueLocation.Y, BlueLocation.Z), Size, -BlueYaw, bCollision, Mesh);
	AddBlock(Name + TEXT("Blue"), BluePaint, BlueLocation, Size, BlueYaw, bCollision, Mesh);
}

void AChaosImpactDuelStage::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyStageColors();
}

void AChaosImpactDuelStage::BeginPlay()
{
	Super::BeginPlay();
	ApplyStageColors();
	if (GetNetMode() == NM_DedicatedServer) { SetActorTickEnabled(false); }
}

void AChaosImpactDuelStage::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const auto* Match = GetWorld()->GetGameState<AChaosImpactGameState>();
	UpdateArenaLighting(Match && Match->IsDuel());
}

void AChaosImpactDuelStage::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UpdateArenaLighting(false);
	Super::EndPlay(EndPlayReason);
}

void AChaosImpactDuelStage::UpdateArenaLighting(const bool bActive)
{
	if (bActive == bArenaLightingActive) { return; }
	bArenaLightingActive = bActive;
	if (bActive)
	{
		for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
		{
			auto* Light = It->FindComponentByClass<UDirectionalLightComponent>();
			if (!Light || !Light->IsVisible()) { continue; }
			FSunSettings& Saved = SavedSuns.AddDefaulted_GetRef();
			Saved.Light = Light;
			Saved.Rotation = Light->GetComponentRotation();
			Saved.SourceAngle = Light->LightSourceAngle;
			Saved.Mobility = static_cast<uint8>(Light->Mobility);
			Light->SetMobility(EComponentMobility::Movable);
			// Almost overhead: a 180-degree camera reversal barely changes the short ground shadow.
			Light->SetWorldRotation(FRotator(-88.f, 0.f, 0.f));
			Light->SetLightSourceAngle(5.f);
		}
	}
	else
	{
		for (const FSunSettings& Saved : SavedSuns)
		{
			if (auto* Light = Saved.Light.Get())
			{
				Light->SetWorldRotation(Saved.Rotation);
				Light->SetLightSourceAngle(Saved.SourceAngle);
				Light->SetMobility(static_cast<EComponentMobility::Type>(Saved.Mobility));
			}
		}
		SavedSuns.Reset();
	}
}

FLinearColor AChaosImpactDuelStage::GetPaintColor(const EPaint Paint)
{
	switch (Paint)
	{
	case Floor: return FLinearColor(0.025f, 0.03f, 0.045f);
	case HalfRed: return FLinearColor(0.045f, 0.012f, 0.018f);
	case HalfBlue: return FLinearColor(0.01f, 0.022f, 0.055f);
	case Wall: return FLinearColor(0.035f, 0.045f, 0.07f);
	case Cover: return FLinearColor(0.1f, 0.11f, 0.14f);
	case Pillar: return FLinearColor(0.32f, 0.34f, 0.4f);
	case GlowRed: return FLinearColor(1.0f, 0.09f, 0.14f);
	case GlowBlue: return FLinearColor(0.08f, 0.45f, 1.0f);
	case GlowGold: return FLinearColor(1.0f, 0.68f, 0.12f);
	default: return FLinearColor(0.8f, 0.9f, 1.0f);
	}
}

void AChaosImpactDuelStage::ApplyStageColors()
{
	if (!bApplyDefaultColors)
	{
		return;
	}
	TArray<UStaticMeshComponent*> Components;
	GetComponents(Components);
	for (UStaticMeshComponent* Block : Components)
	{
		uint8 PaintValue = 0;
		if (!ReadDuelPaint(Block, PaintValue) || PaintValue >= PaintCount)
		{
			continue;
		}
		const EPaint Paint = static_cast<EPaint>(PaintValue);
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
		Material->SetVectorParameterValue(TEXT("Color"), Color);
		if (IsGlow(Paint))
		{
			Material->SetScalarParameterValue(TEXT("Intensity"), Paint == GlowWhite ? GlowIntensity * 0.6f : GlowIntensity);
		}
		else
		{
			Material->SetVectorParameterValue(TEXT("Base Color"), Color);
			Material->SetVectorParameterValue(TEXT("BaseColor"), Color);
			Material->SetScalarParameterValue(TEXT("Roughness"), Paint == Floor ? 0.32f : 0.55f);
		}
	}
}
