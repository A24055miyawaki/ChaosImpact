#include "ChaosImpactCrashBall.h"

#include "ChaosImpactBall.h"
#include "ChaosImpactBallTypes.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactGameMode.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactMatchTypes.h"
#include "ChaosImpactSfx.h"
#include "ChaosImpactStageBase.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	void ApplyMeshTint(UStaticMeshComponent* MeshComp, UMaterialInterface* BaseMat, const FLinearColor& Color,
		const float EmissiveIntensity = 0.0f, const float Roughness = 0.35f)
	{
		if (!MeshComp)
		{
			return;
		}
		UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(MeshComp->GetMaterial(0));
		if (!Mid)
		{
			if (BaseMat)
			{
				MeshComp->SetMaterial(0, BaseMat);
			}
			Mid = MeshComp->CreateDynamicMaterialInstance(0);
		}
		if (!Mid)
		{
			return;
		}
		Mid->SetVectorParameterValue(TEXT("Color"), Color);
		Mid->SetVectorParameterValue(TEXT("Base Color"), Color);
		Mid->SetVectorParameterValue(TEXT("BaseColor"), Color);
		Mid->SetScalarParameterValue(TEXT("Roughness"), Roughness);
		if (EmissiveIntensity > 0.0f)
		{
			Mid->SetScalarParameterValue(TEXT("Intensity"), EmissiveIntensity);
			Mid->SetVectorParameterValue(TEXT("EmissiveColor"), Color * EmissiveIntensity);
		}
	}

	int32 ResolvePawnTeamIndex(const APawn* Pawn)
	{
		if (!Pawn)
		{
			return INDEX_NONE;
		}
		if (const AChaosImpactPlayerState* PS = Pawn->GetPlayerState<AChaosImpactPlayerState>())
		{
			if (PS->TeamIndex >= 0)
			{
				return PS->TeamIndex;
			}
		}
		if (const AChaosImpactCharacter* Char = Cast<AChaosImpactCharacter>(Pawn))
		{
			if (Char->SoloTeam >= 0)
			{
				return Char->SoloTeam;
			}
		}
		return INDEX_NONE;
	}
}

// ============================================================================
// AChaosImpactCrashGoal
// ============================================================================

AChaosImpactCrashGoal::AChaosImpactCrashGoal()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.033f;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(true);
	SetNetUpdateFrequency(4.0f);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeFinder(TEXT("/Engine/BasicShapes/Cone.Cone"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FlatFinder(
		TEXT("/Game/LevelPrototyping/Materials/M_FlatCol.M_FlatCol"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GlowFinder(
		TEXT("/Game/ChaosImpact/Versus/M_StageGlow.M_StageGlow"));

	UStaticMesh* CubeMesh = CubeFinder.Object;
	UStaticMesh* CylinderMesh = CylinderFinder.Succeeded() ? CylinderFinder.Object.Get() : CubeMesh;
	UStaticMesh* ConeMesh = ConeFinder.Succeeded() ? ConeFinder.Object.Get() : CubeMesh;
	FlatMaterial = FlatFinder.Object;
	GlowMaterial = GlowFinder.Succeeded() ? GlowFinder.Object : FlatFinder.Object;

	ThresholdPad = CreateGoalPart(TEXT("ThresholdPad"), CubeMesh, false);
	GoalLineBeam = CreateGoalPart(TEXT("GoalLineBeam"), CubeMesh, false);
	LeftPost = CreateGoalPart(TEXT("LeftPost"), CylinderMesh, true);
	RightPost = CreateGoalPart(TEXT("RightPost"), CylinderMesh, true);
	LeftFunnelBumper = CreateGoalPart(TEXT("LeftFunnelBumper"), CubeMesh, true);
	RightFunnelBumper = CreateGoalPart(TEXT("RightFunnelBumper"), CubeMesh, true);
	Crossbar = CreateGoalPart(TEXT("Crossbar"), CubeMesh, true);
	BackWall = CreateGoalPart(TEXT("BackWall"), CubeMesh, true);
	LeftSideWall = CreateGoalPart(TEXT("LeftSideWall"), CubeMesh, true);
	RightSideWall = CreateGoalPart(TEXT("RightSideWall"), CubeMesh, true);
	RoofGuard = CreateGoalPart(TEXT("RoofGuard"), CubeMesh, true);
	EnergyNet = CreateGoalPart(TEXT("EnergyNet"), CubeMesh, false);
	CrestCenter = CreateGoalPart(TEXT("CrestCenter"), ConeMesh, false);
	CrestWingLeft = CreateGoalPart(TEXT("CrestWingLeft"), CubeMesh, false);
	CrestWingRight = CreateGoalPart(TEXT("CrestWingRight"), CubeMesh, false);

	GoalTriggerBox = CreateDefaultSubobject<UBoxComponent>(TEXT("GoalTriggerBox"));
	GoalTriggerBox->SetupAttachment(SceneRoot);
	GoalTriggerBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	GoalTriggerBox->SetCollisionResponseToAllChannels(ECR_Ignore);

	GoalLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("GoalLight"));
	GoalLight->SetupAttachment(SceneRoot);
	GoalLight->SetIntensity(5500.0f);
	GoalLight->SetAttenuationRadius(1100.0f);
	GoalLight->SetCastShadows(false);

	RebuildGoalGeometry();
	ApplyTeamVisuals(0.0f);
}

UStaticMeshComponent* AChaosImpactCrashGoal::CreateGoalPart(const FName& Name, UStaticMesh* Mesh, const bool bCollision)
{
	UStaticMeshComponent* Comp = CreateDefaultSubobject<UStaticMeshComponent>(Name);
	Comp->SetupAttachment(SceneRoot);
	if (Mesh)
	{
		Comp->SetStaticMesh(Mesh);
	}
	if (bCollision)
	{
		Comp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Comp->SetCollisionObjectType(ECC_WorldStatic);
		Comp->SetCollisionResponseToAllChannels(ECR_Block);
		Comp->CanCharacterStepUpOn = ECB_No;
	}
	else
	{
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetCastShadow(false);
	}
	Comp->SetGenerateOverlapEvents(false);
	Comp->bEditableWhenInherited = true;
	return Comp;
}

void AChaosImpactCrashGoal::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AChaosImpactCrashGoal, TeamIndex);
	DOREPLIFETIME(AChaosImpactCrashGoal, OpeningWidth);
	DOREPLIFETIME(AChaosImpactCrashGoal, OpeningHeight);
	DOREPLIFETIME(AChaosImpactCrashGoal, GoalDepth);
}

void AChaosImpactCrashGoal::SetTeamIndex(const int32 InTeamIndex)
{
	TeamIndex = FMath::Clamp(InTeamIndex, 0, 1);
	RebuildGoalGeometry();
	ApplyTeamVisuals(0.0f);
}

void AChaosImpactCrashGoal::OnRep_TeamIndex()
{
	RebuildGoalGeometry();
	ApplyTeamVisuals(0.0f);
}

void AChaosImpactCrashGoal::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildGoalGeometry();
	ApplyTeamVisuals(0.0f);
}

void AChaosImpactCrashGoal::BeginPlay()
{
	Super::BeginPlay();
	RebuildGoalGeometry();
	ApplyTeamVisuals(0.0f);
}

void AChaosImpactCrashGoal::RebuildGoalGeometry()
{
	const float HalfW = FMath::Max(160.0f, OpeningWidth * 0.5f);
	const float H = FMath::Max(240.0f, OpeningHeight);
	const float D = FMath::Max(160.0f, GoalDepth);
	constexpr float PostRadius = 42.0f;
	constexpr float WallThickness = 48.0f;

	// Local +X points out of the goal mouth toward the stage center.
	// Local -X is inside the goal pocket toward the backstop wall.
	if (ThresholdPad)
	{
		ThresholdPad->SetRelativeLocation(FVector(-D * 0.45f, 0.0f, 3.0f));
		ThresholdPad->SetRelativeScale3D(FVector((D + 80.0f) / 100.0f, (OpeningWidth + 140.0f) / 100.0f, 0.06f));
	}
	if (GoalLineBeam)
	{
		GoalLineBeam->SetRelativeLocation(FVector(12.0f, 0.0f, 6.5f));
		GoalLineBeam->SetRelativeScale3D(FVector(0.22f, OpeningWidth / 100.0f, 0.08f));
	}
	if (LeftPost)
	{
		LeftPost->SetRelativeLocation(FVector(0.0f, -HalfW - PostRadius * 0.7f, H * 0.5f));
		LeftPost->SetRelativeScale3D(FVector(PostRadius * 2.0f / 100.0f, PostRadius * 2.0f / 100.0f, H / 100.0f));
	}
	if (RightPost)
	{
		RightPost->SetRelativeLocation(FVector(0.0f, HalfW + PostRadius * 0.7f, H * 0.5f));
		RightPost->SetRelativeScale3D(FVector(PostRadius * 2.0f / 100.0f, PostRadius * 2.0f / 100.0f, H / 100.0f));
	}
	// Angled front funnel bumpers guide a grazing Giant Ball smoothly into the goal mouth or away from the outer post corner.
	if (LeftFunnelBumper)
	{
		LeftFunnelBumper->SetRelativeLocation(FVector(36.0f, -HalfW - 62.0f, H * 0.45f));
		LeftFunnelBumper->SetRelativeRotation(FRotator(0.0f, -28.0f, 0.0f));
		LeftFunnelBumper->SetRelativeScale3D(FVector(1.1f, 0.42f, H * 0.9f / 100.0f));
	}
	if (RightFunnelBumper)
	{
		RightFunnelBumper->SetRelativeLocation(FVector(36.0f, HalfW + 62.0f, H * 0.45f));
		RightFunnelBumper->SetRelativeRotation(FRotator(0.0f, 28.0f, 0.0f));
		RightFunnelBumper->SetRelativeScale3D(FVector(1.1f, 0.42f, H * 0.9f / 100.0f));
	}
	if (Crossbar)
	{
		Crossbar->SetRelativeLocation(FVector(-10.0f, 0.0f, H + 28.0f));
		Crossbar->SetRelativeScale3D(FVector(0.95f, (OpeningWidth + PostRadius * 4.2f) / 100.0f, 0.58f));
	}
	// Solid side walls, roof, and backstop wall prevent the Giant Ball from ever getting wedged behind the goal.
	if (BackWall)
	{
		BackWall->SetRelativeLocation(FVector(-D - WallThickness * 0.5f, 0.0f, (H + 40.0f) * 0.5f));
		BackWall->SetRelativeScale3D(FVector(WallThickness / 100.0f, (OpeningWidth + PostRadius * 4.0f) / 100.0f, (H + 40.0f) / 100.0f));
	}
	if (LeftSideWall)
	{
		LeftSideWall->SetRelativeLocation(FVector(-D * 0.5f, -HalfW - PostRadius * 0.9f, H * 0.5f));
		LeftSideWall->SetRelativeScale3D(FVector((D + WallThickness) / 100.0f, WallThickness / 100.0f, H / 100.0f));
	}
	if (RightSideWall)
	{
		RightSideWall->SetRelativeLocation(FVector(-D * 0.5f, HalfW + PostRadius * 0.9f, H * 0.5f));
		RightSideWall->SetRelativeScale3D(FVector((D + WallThickness) / 100.0f, WallThickness / 100.0f, H / 100.0f));
	}
	if (RoofGuard)
	{
		RoofGuard->SetRelativeLocation(FVector(-D * 0.5f, 0.0f, H + 26.0f));
		RoofGuard->SetRelativeScale3D(FVector((D + WallThickness) / 100.0f, (OpeningWidth + PostRadius * 3.5f) / 100.0f, 0.36f));
	}
	if (EnergyNet)
	{
		EnergyNet->SetRelativeLocation(FVector(-D * 0.32f, 0.0f, H * 0.48f));
		EnergyNet->SetRelativeScale3D(FVector(0.08f, OpeningWidth * 0.96f / 100.0f, H * 0.92f / 100.0f));
	}

	// Distinct crest silhouettes per team so teams are identifiable by shape as well as color:
	// Team 0 (Red): sharp upward striker horns & vertical spire.
	// Team 1 (Blue): wide horizontal guardian wings & angled shield diamond.
	if (TeamIndex == 0)
	{
		if (CrestCenter)
		{
			CrestCenter->SetRelativeLocation(FVector(14.0f, 0.0f, H + 96.0f));
			CrestCenter->SetRelativeRotation(FRotator(0.0f, 0.0f, 0.0f));
			CrestCenter->SetRelativeScale3D(FVector(1.15f, 1.15f, 1.35f));
		}
		if (CrestWingLeft)
		{
			CrestWingLeft->SetRelativeLocation(FVector(8.0f, -150.0f, H + 86.0f));
			CrestWingLeft->SetRelativeRotation(FRotator(0.0f, 0.0f, -34.0f));
			CrestWingLeft->SetRelativeScale3D(FVector(0.36f, 1.85f, 0.32f));
		}
		if (CrestWingRight)
		{
			CrestWingRight->SetRelativeLocation(FVector(8.0f, 150.0f, H + 86.0f));
			CrestWingRight->SetRelativeRotation(FRotator(0.0f, 0.0f, 34.0f));
			CrestWingRight->SetRelativeScale3D(FVector(0.36f, 1.85f, 0.32f));
		}
	}
	else
	{
		if (CrestCenter)
		{
			CrestCenter->SetRelativeLocation(FVector(14.0f, 0.0f, H + 88.0f));
			CrestCenter->SetRelativeRotation(FRotator(90.0f, 0.0f, 0.0f));
			CrestCenter->SetRelativeScale3D(FVector(1.30f, 1.30f, 0.85f));
		}
		if (CrestWingLeft)
		{
			CrestWingLeft->SetRelativeLocation(FVector(8.0f, -195.0f, H + 68.0f));
			CrestWingLeft->SetRelativeRotation(FRotator(0.0f, 12.0f, -10.0f));
			CrestWingLeft->SetRelativeScale3D(FVector(0.42f, 2.30f, 0.28f));
		}
		if (CrestWingRight)
		{
			CrestWingRight->SetRelativeLocation(FVector(8.0f, 195.0f, H + 68.0f));
			CrestWingRight->SetRelativeRotation(FRotator(0.0f, -12.0f, 10.0f));
			CrestWingRight->SetRelativeScale3D(FVector(0.42f, 2.30f, 0.28f));
		}
	}

	if (GoalTriggerBox)
	{
		GoalTriggerBox->SetRelativeLocation(FVector(-D * 0.45f, 0.0f, H * 0.5f));
		GoalTriggerBox->SetBoxExtent(FVector(D * 0.48f, HalfW * 0.92f, H * 0.48f));
	}
	if (GoalLight)
	{
		GoalLight->SetRelativeLocation(FVector(45.0f, 0.0f, H * 0.62f));
	}
}

void AChaosImpactCrashGoal::ApplyTeamVisuals(const float PulseAlpha)
{
	const FLinearColor TeamCol = ChaosImpactMatch::GetTeamColor(TeamIndex);
	const FLinearColor AccentCol = TeamIndex == 0
		? FLinearColor(1.0f, 0.62f, 0.12f, 1.0f)
		: FLinearColor(0.18f, 0.92f, 1.0f, 1.0f);
	const FLinearColor FrameDark = FMath::Lerp(FLinearColor(0.04f, 0.05f, 0.09f, 1.0f), TeamCol, 0.22f);
	const float GlowBoost = 2.4f + PulseAlpha * 3.8f;

	ApplyMeshTint(ThresholdPad, FlatMaterial, FrameDark, 0.0f, 0.45f);
	ApplyMeshTint(GoalLineBeam, GlowMaterial, AccentCol, GlowBoost + 1.2f, 0.15f);
	ApplyMeshTint(LeftPost, FlatMaterial, TeamCol, 0.0f, 0.25f);
	ApplyMeshTint(RightPost, FlatMaterial, TeamCol, 0.0f, 0.25f);
	ApplyMeshTint(LeftFunnelBumper, GlowMaterial, AccentCol, GlowBoost * 0.7f, 0.25f);
	ApplyMeshTint(RightFunnelBumper, GlowMaterial, AccentCol, GlowBoost * 0.7f, 0.25f);
	ApplyMeshTint(Crossbar, FlatMaterial, TeamCol, 0.0f, 0.22f);
	ApplyMeshTint(BackWall, FlatMaterial, FrameDark, 0.0f, 0.5f);
	ApplyMeshTint(LeftSideWall, FlatMaterial, FrameDark, 0.0f, 0.5f);
	ApplyMeshTint(RightSideWall, FlatMaterial, FrameDark, 0.0f, 0.5f);
	ApplyMeshTint(RoofGuard, FlatMaterial, FrameDark, 0.0f, 0.5f);
	ApplyMeshTint(EnergyNet, GlowMaterial, FMath::Lerp(TeamCol, AccentCol, 0.45f), 1.4f + PulseAlpha * 3.0f, 0.1f);
	ApplyMeshTint(CrestCenter, GlowMaterial, AccentCol, GlowBoost + 1.5f, 0.15f);
	ApplyMeshTint(CrestWingLeft, GlowMaterial, TeamCol, GlowBoost, 0.2f);
	ApplyMeshTint(CrestWingRight, GlowMaterial, TeamCol, GlowBoost, 0.2f);

	if (GoalLight)
	{
		GoalLight->SetLightColor(FMath::Lerp(TeamCol, AccentCol, PulseAlpha * 0.5f));
		GoalLight->SetIntensity(5500.0f + PulseAlpha * 9500.0f);
	}
}

void AChaosImpactCrashGoal::TriggerGoalScoredEffect_Implementation()
{
	ScoredFlashAt = FPlatformTime::Seconds();
	ApplyTeamVisuals(1.0f);
}

void AChaosImpactCrashGoal::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UWorld* World = GetWorld();
	if (!World || World->IsNetMode(NM_DedicatedServer))
	{
		return;
	}

	float ProximityAlpha = 0.0f;
	if (const AChaosImpactGameState* GS = World->GetGameState<AChaosImpactGameState>())
	{
		if (const AChaosImpactCrashBall* Ball = GS->GetCrashBall())
		{
			const float Dist = FVector::Dist2D(GetActorLocation(), Ball->GetActorLocation());
			ProximityAlpha = FMath::Clamp(1.0f - (Dist - 350.0f) / 1400.0f, 0.0f, 1.0f);
		}
	}
	const double Now = FPlatformTime::Seconds();
	const float FlashAge = static_cast<float>(Now - ScoredFlashAt);
	float Pulse = 0.0f;
	if (FlashAge >= 0.0f && FlashAge < 2.0f)
	{
		Pulse = 1.0f;
	}
	else if (ProximityAlpha > 0.01f)
	{
		const float Rate = FMath::Lerp(4.0f, 14.0f, ProximityAlpha);
		Pulse = ProximityAlpha * (0.5f + 0.5f * FMath::Sin(static_cast<float>(Now) * Rate));
	}
	ApplyTeamVisuals(Pulse);
}

bool AChaosImpactCrashGoal::IsBallScored(const AChaosImpactCrashBall* Ball) const
{
	if (!IsValid(Ball) || Ball->IsRecovering())
	{
		bApproachObserved = false;
		return false;
	}

	const FVector LocalBall = GetActorTransform().InverseTransformPositionNoScale(Ball->GetActorLocation());
	const float Radius = Ball->GetBallRadius();
	const float HalfW = OpeningWidth * 0.5f;

	// Local +X points out of the goal mouth into the stage; scoring requires the ball center to cross past the front threshold line
	// into the goal pocket while laterally and vertically inside the goal opening.
	if (LocalBall.X > Radius) { bApproachObserved = true; }
	const bool bInsideWidth = FMath::Abs(LocalBall.Y) <= FMath::Max(1.0f, HalfW - Radius + 8.f);
	const bool bInsideHeight = LocalBall.Z >= Radius - 15.f && LocalBall.Z <= OpeningHeight - Radius + 8.f;
	const bool bCrossedGoalLine = LocalBall.X <= 12.0f && LocalBall.X >= -(GoalDepth + Radius * 0.6f);
	return bApproachObserved && bInsideWidth && bInsideHeight && bCrossedGoalLine;
}

// ============================================================================
// AChaosImpactCrashBall
// ============================================================================

AChaosImpactCrashBall::AChaosImpactCrashBall()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);
	SetNetUpdateFrequency(60.0f);
	SetMinNetUpdateFrequency(20.0f);

	CollisionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionSphere"));
	SetRootComponent(CollisionSphere);
	CollisionSphere->InitSphereRadius(BallRadius);
	CollisionSphere->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	CollisionSphere->SetCollisionObjectType(ECC_WorldDynamic);
	CollisionSphere->SetCollisionResponseToAllChannels(ECR_Block);
	CollisionSphere->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	CollisionSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	CollisionSphere->CanCharacterStepUpOn = ECB_No;
	CollisionSphere->SetGenerateOverlapEvents(true);

	VisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VisualRoot"));
	VisualRoot->SetupAttachment(CollisionSphere);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> RingFinder(
		TEXT("/Game/LevelPrototyping/Interactable/JumpPad/Assets/Meshes/SM_CircularBand.SM_CircularBand"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FlatFinder(
		TEXT("/Game/LevelPrototyping/Materials/M_FlatCol.M_FlatCol"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GlowFinder(
		TEXT("/Game/ChaosImpact/Versus/M_StageGlow.M_StageGlow"));

	UStaticMesh* SphereMesh = SphereFinder.Object;
	UStaticMesh* BandMesh = RingFinder.Succeeded() ? RingFinder.Object : CylinderFinder.Object;
	FlatMaterial = FlatFinder.Object;
	GlowMaterial = GlowFinder.Succeeded() ? GlowFinder.Object : FlatFinder.Object;

	const auto MakeVisualPart = [this](const FName& Name, USceneComponent* Parent, UStaticMesh* Mesh) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* Comp = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Comp->SetupAttachment(Parent);
		if (Mesh)
		{
			Comp->SetStaticMesh(Mesh);
		}
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetGenerateOverlapEvents(false);
		return Comp;
	};

	CoreSphereMesh = MakeVisualPart(TEXT("CoreSphereMesh"), VisualRoot, SphereMesh);
	EquatorialBandMesh = MakeVisualPart(TEXT("EquatorialBandMesh"), VisualRoot, BandMesh);
	MeridianBandAMesh = MakeVisualPart(TEXT("MeridianBandAMesh"), VisualRoot, BandMesh);
	MeridianBandBMesh = MakeVisualPart(TEXT("MeridianBandBMesh"), VisualRoot, BandMesh);

	// Ground-projected rings attached to CollisionSphere (do not roll with VisualRoot).
	DirectionRingMesh = MakeVisualPart(TEXT("DirectionRingMesh"), CollisionSphere, BandMesh);
	ShockwaveRingMesh = MakeVisualPart(TEXT("ShockwaveRingMesh"), CollisionSphere, BandMesh);
	ShockwaveRingMesh->SetHiddenInGame(true);

	CoreLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("CoreLight"));
	CoreLight->SetupAttachment(CollisionSphere);
	CoreLight->SetIntensity(6500.0f);
	CoreLight->SetAttenuationRadius(750.0f);
	CoreLight->SetCastShadows(false);

	const float DiameterScale = (BallRadius * 2.0f) / 100.0f;
	CoreSphereMesh->SetRelativeScale3D(FVector(DiameterScale));
	EquatorialBandMesh->SetRelativeScale3D(FVector(DiameterScale * 1.03f, DiameterScale * 1.03f, 0.28f));
	MeridianBandAMesh->SetRelativeRotation(FRotator(90.0f, 0.0f, 0.0f));
	MeridianBandAMesh->SetRelativeScale3D(FVector(DiameterScale * 1.02f, DiameterScale * 1.02f, 0.24f));
	MeridianBandBMesh->SetRelativeRotation(FRotator(0.0f, 0.0f, 90.0f));
	MeridianBandBMesh->SetRelativeScale3D(FVector(DiameterScale * 1.02f, DiameterScale * 1.02f, 0.24f));

	DirectionRingMesh->SetRelativeLocation(FVector(0.0f, 0.0f, -BallRadius + 8.0f));
	DirectionRingMesh->SetRelativeScale3D(FVector(DiameterScale * 1.22f, DiameterScale * 1.22f, 0.10f));
	ShockwaveRingMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 0.0f));
}

void AChaosImpactCrashBall::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AChaosImpactCrashBall, NetState);
}

void AChaosImpactCrashBall::BeginPlay()
{
	Super::BeginPlay();
	CollisionSphere->SetSphereRadius(BallRadius, true);
	InitialSpawnLocation = GetActorLocation();
	LastSafeLocation = InitialSpawnLocation;
	bHasSafeLocation = true;
	UpdateVisuals(0.0f);
}

void AChaosImpactCrashBall::ResetToStartLocation(const FVector& FloorLocation, AChaosImpactStageBase* InStage)
{
	CachedStage = InStage;
	CollisionSphere->SetSphereRadius(BallRadius, true);
	const FVector StartCenter = FloorLocation + FVector(0.0f, 0.0f, BallRadius + 2.0f);
	SetActorLocation(StartCenter, false, nullptr, ETeleportType::TeleportPhysics);
	InitialSpawnLocation = StartCenter;
	LastSafeLocation = StartCenter;
	bHasSafeLocation = true;
	Velocity = FVector::ZeroVector;
	RollRotation = FRotator::ZeroRotator;
	LastHitTeamIndex = INDEX_NONE;
	LastHitServerTime = -100.0;
	LastTeamHitServerTime[0] = -100.0;
	LastTeamHitServerTime[1] = -100.0;
	LastHitTimeBySource.Empty();
	RecoveryFreezeRemaining = 0.0f;
	StuckAccumulatedTime = 0.0f;
	StuckPushCount = 0;
	StuckAnchorLocation = StartCenter;

	if (HasAuthority())
	{
		NetState.Location = StartCenter;
		NetState.Velocity = FVector::ZeroVector;
		NetState.RollRotation = RollRotation;
		NetState.LastHitTeam = -1;
		NetState.bLastHitCharged = false;
		NetState.bRecovering = false;
		ForceNetUpdate();
	}
	UpdateVisuals(0.0f);
}

double AChaosImpactCrashBall::GetLastTeamHitServerTime(const int32 TeamIndex) const
{
	return (TeamIndex >= 0 && TeamIndex < 2) ? LastTeamHitServerTime[TeamIndex] : -100.0;
}

bool AChaosImpactCrashBall::ApplyBallHit(AChaosImpactBall* IncomingBall, const FHitResult& Hit)
{
	if (!HasAuthority() || !IsValid(IncomingBall) || IncomingBall->IsPickup() || IsRecovering())
	{
		return false;
	}

	UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : FPlatformTime::Seconds();

	// Prevent duplicate collision events from the same ball actor within PerBallHitCooldown.
	const FObjectKey SourceKey(IncomingBall);
	if (const double* LastTime = LastHitTimeBySource.Find(SourceKey))
	{
		if (Now - *LastTime < PerBallHitCooldown)
		{
			return false;
		}
	}
	LastHitTimeBySource.Add(SourceKey, Now);

	const FVector FlightVel = IncomingBall->GetBallVelocity();
	const float FlightSpeed = FlightVel.Size();
	FVector FlightDir2D = FVector(FlightVel.X, FlightVel.Y, 0.0f).GetSafeNormal();
	const FVector CenterToCenter2D = FVector(GetActorLocation().X - IncomingBall->GetActorLocation().X,
		GetActorLocation().Y - IncomingBall->GetActorLocation().Y, 0.0f).GetSafeNormal();

	if (FlightDir2D.IsNearlyZero())
	{
		FlightDir2D = !CenterToCenter2D.IsNearlyZero() ? CenterToCenter2D : FVector(1.0f, 0.0f, 0.0f);
	}
	else if (!CenterToCenter2D.IsNearlyZero() && FVector::DotProduct(FlightDir2D, CenterToCenter2D) > 0.0f)
	{
		// Primarily push in the thrown ball's travel direction, with a slight surface-normal blend for natural banking.
		FlightDir2D = (FlightDir2D * 0.82f + CenterToCenter2D * 0.18f).GetSafeNormal();
	}

	// Charge scaling: MinimumThrowSpeed (~1330 arc) -> 0.0, MaximumThrowSpeed (~3100 arc) -> 1.0.
	const float ChargeAlpha = FMath::GetMappedRangeValueClamped(
		FVector2D(1350.0f, 3050.0f), FVector2D(0.0f, 1.0f), FlightSpeed);
	float PushSpeed = FMath::Lerp(MinThrowPushSpeed, MaxChargeThrowPushSpeed, ChargeAlpha);

	// Special ball direct-impact multipliers (balanced so none launches the Giant Ball across the map in one hit).
	const EChaosImpactBallType BallType = IncomingBall->GetBallType();
	switch (BallType)
	{
	case EChaosImpactBallType::Fire:
		PushSpeed *= 1.15f;
		break;
	case EChaosImpactBallType::Thunder:
		PushSpeed *= 1.08f;
		break;
	case EChaosImpactBallType::Snow:
		PushSpeed *= FMath::Clamp(0.9f + 0.35f * IncomingBall->GetSnowScale(), 0.9f, 1.45f);
		break;
	case EChaosImpactBallType::Nova:
		PushSpeed = FMath::Max(PushSpeed, SpecialDirectPushSpeed * 1.35f);
		break;
	case EChaosImpactBallType::Drive:
		PushSpeed = FMath::Max(PushSpeed, SpecialDirectPushSpeed * 1.15f);
		break;
	case EChaosImpactBallType::Ice:
	case EChaosImpactBallType::Smoke:
	case EChaosImpactBallType::Prison:
		// Utility/control balls still push slightly on impact, but never freeze/capture/delete the Giant Ball.
		PushSpeed *= 0.72f;
		break;
	default:
		break;
	}

	const int32 ThrowerTeam = ResolvePawnTeamIndex(IncomingBall->GetThrowingPawn());
	PushSpeed *= GetTerritoryPushScale(ThrowerTeam);
	const bool bChargedHit = ChargeAlpha >= 0.55f || BallType == EChaosImpactBallType::Nova || BallType == EChaosImpactBallType::Fire;
	RecordPushImpulse(FlightDir2D * PushSpeed, ThrowerTeam, bChargedHit);
	if (World)
	{
		if (auto* GM = World->GetAuthGameMode<AChaosImpactGameMode>())
		{
			GM->NotifyCrashBallHit(IncomingBall->GetThrowingPawn(), ThrowerTeam, bChargedHit, FlightDir2D);
		}
	}
	return true;
}

float AChaosImpactCrashBall::GetTerritoryPushScale(const int32 TeamIndex) const
{
	const AChaosImpactStageBase* Stage = CachedStage.Get();
	if (!Stage || (TeamIndex != 0 && TeamIndex != 1))
	{
		return 1.0f;
	}
	int32 Red, Blue;
	float Progress;
	Stage->ComputeCrashBallCounts(GetActorLocation(), Red, Blue, Progress);
	// Progress runs -1 (red goal) .. +1 (blue goal); positive is red's attack.
	const float Attack = FMath::Clamp(TeamIndex == 0 ? Progress : -Progress, -1.0f, 1.0f);
	return Attack >= 0.0f ? FMath::Lerp(1.0f, DeepAttackPushScale, Attack) : FMath::Lerp(1.0f, DeepDefencePushScale, -Attack);
}

bool AChaosImpactCrashBall::ApplySpecialEffectPush(AActor* SourceActor, APawn* InstigatorPawn,
	const FVector& WorldOrigin, const FVector& PreferredDirection, const float StrengthScale, const bool bIsContinuousZone)
{
	if (!HasAuthority() || IsRecovering())
	{
		return false;
	}

	UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : FPlatformTime::Seconds();

	if (SourceActor)
	{
		const FObjectKey SourceKey(SourceActor);
		const float Cooldown = bIsContinuousZone ? SpecialSourceCooldown : FMath::Max(0.25f, SpecialSourceCooldown * 0.6f);
		if (const double* LastTime = LastHitTimeBySource.Find(SourceKey))
		{
			if (Now - *LastTime < Cooldown)
			{
				return false;
			}
		}
		LastHitTimeBySource.Add(SourceKey, Now);
	}

	// Global rate limit for continuous zone pulses so overlapping zones or multi-summons never stack into runaway acceleration.
	if (bIsContinuousZone && Now - LastSpecialZonePushTime < 0.22)
	{
		return false;
	}
	if (bIsContinuousZone)
	{
		LastSpecialZonePushTime = Now;
	}

	FVector PushDir2D = FVector(PreferredDirection.X, PreferredDirection.Y, 0.0f).GetSafeNormal();
	if (PushDir2D.IsNearlyZero())
	{
		PushDir2D = FVector(GetActorLocation().X - WorldOrigin.X, GetActorLocation().Y - WorldOrigin.Y, 0.0f).GetSafeNormal();
	}
	if (PushDir2D.IsNearlyZero() && InstigatorPawn)
	{
		PushDir2D = FVector(GetActorLocation().X - InstigatorPawn->GetActorLocation().X,
			GetActorLocation().Y - InstigatorPawn->GetActorLocation().Y, 0.0f).GetSafeNormal();
	}
	if (PushDir2D.IsNearlyZero())
	{
		PushDir2D = FVector(1.0f, 0.0f, 0.0f);
	}

	const float BaseSpeed = bIsContinuousZone ? SpecialZonePushSpeed : SpecialDirectPushSpeed;
	const int32 TeamIdx = ResolvePawnTeamIndex(InstigatorPawn);
	const float PushSpeed = FMath::Clamp(BaseSpeed * FMath::Clamp(StrengthScale, 0.2f, 1.6f), 80.0f, MaxChargeThrowPushSpeed * 1.15f)
		* GetTerritoryPushScale(TeamIdx);
	RecordPushImpulse(PushDir2D * PushSpeed, TeamIdx, !bIsContinuousZone && StrengthScale >= 1.0f);
	return true;
}

void AChaosImpactCrashBall::RecordPushImpulse(const FVector& HorizontalDeltaVelocity, const int32 TeamIndex, const bool bCharged)
{
	UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : FPlatformTime::Seconds();

	// Damp opposing velocity slightly when hit from the opposite side so counter-pushes feel responsive,
	// while capping maximum speed so rapid multi-player volleys stay controllable.
	FVector CurrentHoriz(Velocity.X, Velocity.Y, 0.0f);
	const FVector PushDir = HorizontalDeltaVelocity.GetSafeNormal2D();
	if (!CurrentHoriz.IsNearlyZero() && !PushDir.IsNearlyZero())
	{
		const float Dot = FVector::DotProduct(CurrentHoriz.GetSafeNormal2D(), PushDir);
		if (Dot < -0.2f)
		{
			CurrentHoriz *= 0.45f;
		}
		else if (Dot > 0.5f && CurrentHoriz.Size() > MaxSpeed * 0.65f)
		{
			// Diminishing returns when already moving fast in the same direction.
			CurrentHoriz += HorizontalDeltaVelocity * 0.58f;
			CurrentHoriz = CurrentHoriz.GetClampedToMaxSize(MaxSpeed);
			Velocity.X = CurrentHoriz.X;
			Velocity.Y = CurrentHoriz.Y;
			goto PostVelocityUpdate;
		}
	}
	CurrentHoriz = (CurrentHoriz + HorizontalDeltaVelocity).GetClampedToMaxSize(MaxSpeed);
	Velocity.X = CurrentHoriz.X;
	Velocity.Y = CurrentHoriz.Y;

PostVelocityUpdate:
	if (TeamIndex == 0 || TeamIndex == 1)
	{
		LastHitTeamIndex = TeamIndex;
		LastHitServerTime = Now;
		LastTeamHitServerTime[TeamIndex] = Now;
	}

	if (StuckPushCount == 0)
	{
		StuckAnchorLocation = GetActorLocation();
		StuckAccumulatedTime = 0.0f;
	}
	++StuckPushCount;

	NetState.Location = GetActorLocation();
	NetState.Velocity = Velocity;
	NetState.LastHitTeam = static_cast<int8>(LastHitTeamIndex);
	NetState.bLastHitCharged = bCharged;
	++NetState.HitRevision;
	ForceNetUpdate();

	ClientHitFlashAt = FPlatformTime::Seconds();
	bClientLastHitCharged = bCharged;
	ClientSeenHitRevision = NetState.HitRevision;
	ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::BallHit, GetActorLocation(), bCharged ? 1.3f : .8f, bCharged ? .75f : .95f);

}

void AChaosImpactCrashBall::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const float ClampedDt = FMath::Clamp(DeltaSeconds, 0.0f, 0.1f);
	if (HasAuthority())
	{
		const auto* Match = GetWorld()->GetGameState<AChaosImpactGameState>();
		if (Match && Match->bVersusMatch && Match->Phase == EChaosImpactOnlinePhase::Match)
		{
			StepServerPhysics(ClampedDt);
			UpdateSafePositionAndRecovery(ClampedDt);
		}
		else { Velocity = FVector::ZeroVector; }

		NetState.Location = GetActorLocation();
		NetState.Velocity = Velocity;
		NetState.RollRotation = RollRotation;
		NetState.LastHitTeam = static_cast<int8>(LastHitTeamIndex);
		NetState.bRecovering = RecoveryFreezeRemaining > 0.0f;
	}
	else
	{
		// Smooth client prediction/interpolation between server snapshots.
		if (!NetState.bRecovering)
		{
			const FVector Predicted = GetActorLocation() + Velocity * ClampedDt;
			const FVector Target = FVector(NetState.Location);
			const float DistSq = FVector::DistSquared(Predicted, Target);
			const FVector NewLoc = DistSq > FMath::Square(320.0f)
				? Target
				: FMath::VInterpTo(Predicted, Target, ClampedDt, 16.0f);
			SetActorLocation(NewLoc, false);
		}
		else
		{
			SetActorLocation(FVector(NetState.Location), false);
		}
		RollRotation = FMath::RInterpTo(RollRotation, NetState.RollRotation, ClampedDt, 18.0f);
	}

	UpdateVisuals(ClampedDt);
}

void AChaosImpactCrashBall::OnRep_NetState()
{
	Velocity = FVector(NetState.Velocity);
	LastHitTeamIndex = NetState.LastHitTeam;
	if (NetState.HitRevision != ClientSeenHitRevision)
	{
		ClientSeenHitRevision = NetState.HitRevision;
		ClientHitFlashAt = FPlatformTime::Seconds();
		bClientLastHitCharged = NetState.bLastHitCharged;
		ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::BallHit, FVector(NetState.Location), bClientLastHitCharged ? 1.3f : .8f, .85f);
	}
	if (NetState.RecoveryRevision != ClientSeenRecoveryRevision)
	{
		ClientSeenRecoveryRevision = NetState.RecoveryRevision;
		ClientRecoveryFlashAt = FPlatformTime::Seconds();
		SetActorLocation(FVector(NetState.Location), false, nullptr, ETeleportType::TeleportPhysics);
	}
}

void AChaosImpactCrashBall::StepServerPhysics(const float DeltaSeconds)
{
	if (RecoveryFreezeRemaining > 0.0f)
	{
		RecoveryFreezeRemaining = FMath::Max(0.0f, RecoveryFreezeRemaining - DeltaSeconds);
		Velocity = FVector::ZeroVector;
		return;
	}

	// Fixed-max-timestep substepping so rolling deceleration, slope climbing, and wall bounces are framerate-independent.
	constexpr float MaxSubstepDt = 1.0f / 120.0f;
	int32 NumSubsteps = FMath::Clamp(FMath::CeilToInt(DeltaSeconds / MaxSubstepDt), 1, 12);
	const float SubDt = DeltaSeconds / static_cast<float>(NumSubsteps);

	for (int32 Step = 0; Step < NumSubsteps; ++Step)
	{
		StepSingleSubstep(SubDt);
	}

	ResolveCharacterOverlaps();

	// Periodically prune old entries from LastHitTimeBySource.
	if (LastHitTimeBySource.Num() > 32)
	{
		const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : FPlatformTime::Seconds();
		for (auto It = LastHitTimeBySource.CreateIterator(); It; ++It)
		{
			if (Now - It.Value() > 4.0)
			{
				It.RemoveCurrent();
			}
		}
	}
}

void AChaosImpactCrashBall::StepSingleSubstep(const float SubDt)
{
	UWorld* World = GetWorld();
	if (!World || SubDt <= 0.0f)
	{
		return;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(CrashBallPhysics), false, this);
	const FCollisionShape SphereShape = FCollisionShape::MakeSphere(BallRadius);

	// 1. Check ground support beneath the Giant Ball.
	FHitResult FloorHit;
	const FVector CurrentLoc = GetActorLocation();
	const bool bHitFloor = World->SweepSingleByChannel(
		FloorHit,
		CurrentLoc + FVector(0.0f, 0.0f, 12.0f),
		CurrentLoc - FVector(0.0f, 0.0f, 22.0f),
		FQuat::Identity,
		ECC_WorldStatic,
		SphereShape,
		QueryParams);

	bGrounded = bHitFloor && FloorHit.ImpactNormal.Z >= WalkableFloorZ;
	if (!bGrounded && Velocity.Z <= 60.0f)
	{
		// Rolling down a ramp the sphere leaves the surface for a moment each step; held to it, it keeps its
		// rolling friction instead of skating down on air drag. A real drop (past a step's height) still falls.
		FHitResult RampHit;
		if (World->SweepSingleByChannel(RampHit, CurrentLoc, CurrentLoc - FVector(0.0f, 0.0f, MaxStepUpHeight + 20.0f),
				FQuat::Identity, ECC_WorldStatic, SphereShape, QueryParams)
			&& !RampHit.bStartPenetrating && RampHit.ImpactNormal.Z >= WalkableFloorZ)
		{
			SetActorLocation(RampHit.Location, false);
			FloorHit = RampHit;
			bGrounded = true;
		}
	}

	if (bGrounded)
	{
		if (Velocity.Z < 0.0f)
		{
			Velocity.Z = 0.0f;
		}
		// Gently keep sphere resting on the floor surface without jittering.
		const float TargetZ = FloorHit.ImpactPoint.Z + BallRadius;
		if (CurrentLoc.Z < TargetZ + 8.0f && CurrentLoc.Z > TargetZ - MaxStepUpHeight)
		{
			SetActorLocation(FVector(CurrentLoc.X, CurrentLoc.Y, FMath::FInterpTo(CurrentLoc.Z, TargetZ, SubDt, 28.0f)), false);
		}
	}
	else
	{
		Velocity.Z = FMath::Max(Velocity.Z - GravityAcceleration * SubDt, -2200.0f);
	}

	// 2. Apply framerate-independent horizontal deceleration.
	FVector HorizVel(Velocity.X, Velocity.Y, 0.0f);
	const float HorizSpeed = HorizVel.Size();
	if (HorizSpeed > 0.0f)
	{
		const float Decel = bGrounded ? RollingDeceleration : AirDeceleration;
		const float NewSpeed = FMath::Max(0.0f, HorizSpeed - Decel * SubDt);
		HorizVel = HorizVel.GetSafeNormal() * NewSpeed;
		Velocity.X = HorizVel.X;
		Velocity.Y = HorizVel.Y;
	}

	// 3. Move with step-up & slope sliding so small ledges, ramps, and bridges never snag the Giant Ball.
	FVector RemainingDelta = Velocity * SubDt;
	for (int32 Iter = 0; Iter < 3 && !RemainingDelta.IsNearlyZero(0.0001f); ++Iter)
	{
		FHitResult MoveHit;
		SetActorLocation(GetActorLocation() + RemainingDelta, true, &MoveHit);
		if (!MoveHit.bBlockingHit)
		{
			break;
		}

		if (MoveHit.bStartPenetrating)
		{
			const FVector Depenetrate = MoveHit.Normal * (MoveHit.PenetrationDepth + 1.5f);
			SetActorLocation(GetActorLocation() + Depenetrate, false);
		}

		RemainingDelta *= (1.0f - MoveHit.Time);
		const FVector HitNormal = MoveHit.ImpactNormal;
		const float BottomZ = GetActorLocation().Z - BallRadius;
		const float ObstacleStepHeight = MoveHit.ImpactPoint.Z - BottomZ;

		// Walkable slope or low step/curb: climb smoothly!
		if (HitNormal.Z >= WalkableFloorZ)
		{
			const float HorizMag = FVector(RemainingDelta.X, RemainingDelta.Y, 0.0f).Size();
			FVector SlideDir = FVector::VectorPlaneProject(RemainingDelta, HitNormal).GetSafeNormal();
			RemainingDelta = SlideDir * HorizMag;
			bGrounded = true;
			if (Velocity.Z < 0.0f)
			{
				Velocity.Z = 0.0f;
			}
			continue;
		}
		else if (ObstacleStepHeight > 0.0f && ObstacleStepHeight <= MaxStepUpHeight && FVector(Velocity.X, Velocity.Y, 0.0f).SizeSquared() > 25.0f)
		{
			// Try stepping up over the low ledge/curb.
			const FVector PreStepLoc = GetActorLocation();
			const float LiftZ = ObstacleStepHeight + 6.0f;
			FHitResult UpHit;
			SetActorLocation(PreStepLoc + FVector(0.0f, 0.0f, LiftZ), true, &UpHit);
			if (!UpHit.bBlockingHit)
			{
				FHitResult FwdHit;
				const FVector HorizStep(RemainingDelta.X, RemainingDelta.Y, 0.0f);
				SetActorLocation(GetActorLocation() + HorizStep, true, &FwdHit);
				if (!FwdHit.bBlockingHit || FwdHit.Time > 0.25f)
				{
					bGrounded = true;
					break;
				}
			}
			// Revert if step-up was blocked by a tall wall above the ledge.
			SetActorLocation(PreStepLoc, false);
		}

		// Vertical wall reflection / slide.
		FVector WallNormal2D = FVector(HitNormal.X, HitNormal.Y, 0.0f).GetSafeNormal();
		if (!WallNormal2D.IsNearlyZero())
		{
			const float IntoWall = FVector::DotProduct(FVector(Velocity.X, Velocity.Y, 0.0f), WallNormal2D);
			if (IntoWall < 0.0f)
			{
				const FVector Reflected2D = FVector(Velocity.X, Velocity.Y, 0.0f) - (1.0f + WallBounceRestitution) * IntoWall * WallNormal2D;
				Velocity.X = Reflected2D.X;
				Velocity.Y = Reflected2D.Y;
				RemainingDelta = FVector::VectorPlaneProject(RemainingDelta, WallNormal2D);
			}
		}
		else if (HitNormal.Z < 0.0f && Velocity.Z > 0.0f)
		{
			Velocity.Z = 0.0f;
		}
	}

	// Update visual roll rotation from horizontal travel.
	const FVector HorizMoved(Velocity.X * SubDt, Velocity.Y * SubDt, 0.0f);
	const float DistMoved = HorizMoved.Size();
	if (DistMoved > 0.01f && BallRadius > 1.0f)
	{
		const FVector RollAxis = FVector::CrossProduct(FVector::UpVector, HorizMoved.GetSafeNormal());
		const float AngleRad = DistMoved / BallRadius;
		const FQuat DeltaQuat(RollAxis, AngleRad);
		RollRotation = (DeltaQuat * RollRotation.Quaternion()).Rotator();
	}
}

void AChaosImpactCrashBall::ResolveCharacterOverlaps()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FVector BallLoc = GetActorLocation();
	for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
	{
		AChaosImpactCharacter* Character = *It;
		if (!IsValid(Character) || Character->IsEliminated())
		{
			continue;
		}

		const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
		const float CharRadius = Capsule ? Capsule->GetScaledCapsuleRadius() : 42.0f;
		const float CharHalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 96.0f;
		const FVector CharLoc = Character->GetActorLocation();

		if (FMath::Abs(CharLoc.Z - BallLoc.Z) > BallRadius + CharHalfHeight)
		{
			continue;
		}

		const FVector Delta2D(CharLoc.X - BallLoc.X, CharLoc.Y - BallLoc.Y, 0.0f);
		const float Dist2D = Delta2D.Size();
		const float MinSeparation = BallRadius + CharRadius + 4.0f;
		if (Dist2D < MinSeparation)
		{
			// Gently push the character aside horizontally without dealing any contact damage or letting body contact push the Giant Ball.
			const FVector PushOutDir = Dist2D > 1.0f
				? Delta2D / Dist2D
				: (!Velocity.IsNearlyZero() ? FVector(-Velocity.Y, Velocity.X, 0.0f).GetSafeNormal() : FVector(0.0f, 1.0f, 0.0f));
			const float PushDist = (MinSeparation - Dist2D) + 2.0f;
			FHitResult PushHit;
			Character->AddActorWorldOffset(PushOutDir * PushDist, true, &PushHit);
		}
	}
}

bool AChaosImpactCrashBall::IsLocationSafeOnFloor(const FVector& CandidateCenter, FVector& OutSnappedCenter) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const AChaosImpactStageBase* Stage = CachedStage.Get();
	if (!Stage)
	{
		for (TActorIterator<AChaosImpactStageBase> It(World); It; ++It)
		{
			Stage = *It;
			break;
		}
	}
	if (Stage && Stage->IsOutside(CandidateCenter))
	{
		return false;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(CrashBallSafeCheck), false, this);
	for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
	{
		Params.AddIgnoredActor(*It);
	}

	// Must have solid walkable floor directly underneath.
	FHitResult CenterFloorHit;
	if (!World->LineTraceSingleByChannel(
			CenterFloorHit,
			CandidateCenter + FVector(0.0f, 0.0f, 30.0f),
			CandidateCenter - FVector(0.0f, 0.0f, BallRadius + 45.0f),
			ECC_WorldStatic,
			Params)
		|| CenterFloorHit.ImpactNormal.Z < WalkableFloorZ)
	{
		return false;
	}

	const FVector Snapped = FVector(CandidateCenter.X, CandidateCenter.Y, CenterFloorHit.ImpactPoint.Z + BallRadius + 2.0f);

	// Must not overlap any static wall or obstacle at the snapped center.
	if (World->OverlapBlockingTestByChannel(
			Snapped,
			FQuat::Identity,
			ECC_WorldStatic,
			FCollisionShape::MakeSphere(BallRadius * 0.92f),
			Params))
	{
		return false;
	}

	// Verify 4 compass points around the ball's footprint have floor support so we never record a precarious cliff edge.
	const float ProbeRadius = BallRadius * 0.72f;
	static const FVector CompassDirs[4] = {
		FVector(1.0f, 0.0f, 0.0f),
		FVector(-1.0f, 0.0f, 0.0f),
		FVector(0.0f, 1.0f, 0.0f),
		FVector(0.0f, -1.0f, 0.0f)};
	for (const FVector& Dir : CompassDirs)
	{
		FHitResult ProbeHit;
		const FVector ProbeStart = Snapped + Dir * ProbeRadius + FVector(0.0f, 0.0f, 25.0f);
		const FVector ProbeEnd = Snapped + Dir * ProbeRadius - FVector(0.0f, 0.0f, BallRadius + MaxStepUpHeight + 35.0f);
		if (!World->LineTraceSingleByChannel(ProbeHit, ProbeStart, ProbeEnd, ECC_WorldStatic, Params)
			|| ProbeHit.ImpactNormal.Z < 0.45f)
		{
			return false;
		}
	}

	OutSnappedCenter = Snapped;
	return true;
}

void AChaosImpactCrashBall::UpdateSafePositionAndRecovery(const float DeltaSeconds)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	AChaosImpactStageBase* Stage = CachedStage.Get();
	if (!Stage)
	{
		for (TActorIterator<AChaosImpactStageBase> It(World); It; ++It)
		{
			Stage = *It;
			CachedStage = Stage;
			break;
		}
	}

	const FVector CurrentLoc = GetActorLocation();

	// 1. Out-of-bounds / pit / hazard fall check -> immediate recovery to last safe location.
	const float StageFloorRefZ = Stage ? Stage->GetCenter().Z : InitialSpawnLocation.Z - BallRadius;
	if ((Stage && Stage->IsOutside(CurrentLoc)) || CurrentLoc.Z < StageFloorRefZ - 240.0f)
	{
		RecoverToSafeLocation();
		return;
	}

	// 2. Stuck detection: if players have pushed the Giant Ball repeatedly while it barely moved from StuckAnchorLocation.
	if (StuckPushCount > 0)
	{
		const float MovedFromAnchor = FVector::Dist(CurrentLoc, StuckAnchorLocation);
		if (MovedFromAnchor >= StuckMoveThreshold)
		{
			// Ball moved freely; reset stuck tracking.
			StuckAnchorLocation = CurrentLoc;
			StuckAccumulatedTime = 0.0f;
			StuckPushCount = 0;
		}
		else
		{
			StuckAccumulatedTime += DeltaSeconds;
			if (StuckPushCount >= 3 && StuckAccumulatedTime >= StuckTimeThreshold)
			{
				RecoverToSafeLocation();
				return;
			}
			// Expire old single push if no follow-up push occurred for a while.
			if (StuckAccumulatedTime > StuckTimeThreshold * 1.8f && StuckPushCount < 3)
			{
				StuckAccumulatedTime = 0.0f;
				StuckPushCount = 0;
			}
		}
	}

	// 3. Periodically record verified safe ground positions.
	SafeCheckAccumulator += DeltaSeconds;
	if (SafeCheckAccumulator >= SafeRecordInterval)
	{
		SafeCheckAccumulator = 0.0f;
		if (bGrounded && RecoveryFreezeRemaining <= 0.0f)
		{
			FVector VerifiedSafe = CurrentLoc;
			if (IsLocationSafeOnFloor(CurrentLoc, VerifiedSafe))
			{
				LastSafeLocation = VerifiedSafe;
				bHasSafeLocation = true;
			}
		}
	}
}

void AChaosImpactCrashBall::RecoverToSafeLocation()
{
	FVector TargetLoc = bHasSafeLocation ? LastSafeLocation : InitialSpawnLocation;
	FVector Verified = TargetLoc;
	if (!IsLocationSafeOnFloor(TargetLoc, Verified))
	{
		Verified = InitialSpawnLocation;
	}

	Velocity = FVector::ZeroVector;
	RecoveryFreezeRemaining = 0.65f;
	StuckAccumulatedTime = 0.0f;
	StuckPushCount = 0;
	SetActorLocation(Verified, false, nullptr, ETeleportType::TeleportPhysics);
	StuckAnchorLocation = Verified;

	if (HasAuthority())
	{
		NetState.Location = Verified;
		NetState.Velocity = FVector::ZeroVector;
		NetState.bRecovering = true;
		++NetState.RecoveryRevision;
		ForceNetUpdate();

		if (UWorld* World = GetWorld())
		{
			if (AChaosImpactGameMode* GM = World->GetAuthGameMode<AChaosImpactGameMode>())
			{
				GM->NotifyCrashBallRecovered();
			}
		}
	}

	ClientRecoveryFlashAt = FPlatformTime::Seconds();
	ClientSeenRecoveryRevision = NetState.RecoveryRevision;
}

void AChaosImpactCrashBall::UpdateVisuals(const float DeltaSeconds)
{
	UWorld* World = GetWorld();
	if (!World || World->IsNetMode(NM_DedicatedServer))
	{
		return;
	}

	if (VisualRoot)
	{
		VisualRoot->SetRelativeRotation(RollRotation);
	}

	const double Now = FPlatformTime::Seconds();
	const float HitAge = static_cast<float>(Now - ClientHitFlashAt);
	const float RecoveryAge = static_cast<float>(Now - ClientRecoveryFlashAt);
	const float HitFlash = HitAge >= 0.0f && HitAge < 0.35f ? (1.0f - HitAge / 0.35f) : 0.0f;
	const float RecoveryFlash = RecoveryAge >= 0.0f && RecoveryAge < 0.7f ? (1.0f - RecoveryAge / 0.7f) : 0.0f;

	// Hit scale punch on VisualRoot
	if (VisualRoot)
	{
		const float Punch = 1.0f + (bClientLastHitCharged ? 0.16f : 0.09f) * FMath::Sin(HitFlash * UE_PI);
		VisualRoot->SetRelativeScale3D(FVector(Punch));
	}

	// Team color accent from last hitter, or golden neutral energy when untouched.
	const FLinearColor NeutralGold(1.0f, 0.78f, 0.15f, 1.0f);
	const FLinearColor TeamColor = (LastHitTeamIndex == 0 || LastHitTeamIndex == 1)
		? ChaosImpactMatch::GetTeamColor(LastHitTeamIndex)
		: NeutralGold;
	const FLinearColor BandColor = FMath::Lerp(TeamColor, FLinearColor::White, FMath::Max(HitFlash * 0.75f, RecoveryFlash * 0.85f));
	const FLinearColor CoreColor = FMath::Lerp(FLinearColor(0.08f, 0.10f, 0.18f, 1.0f), TeamColor, 0.28f + HitFlash * 0.45f);
	const float GlowPower = 2.8f + HitFlash * (bClientLastHitCharged ? 6.5f : 3.5f) + RecoveryFlash * 5.0f;

	ApplyMeshTint(CoreSphereMesh, FlatMaterial, CoreColor, 0.0f, 0.22f);
	ApplyMeshTint(EquatorialBandMesh, GlowMaterial, BandColor, GlowPower, 0.15f);
	ApplyMeshTint(MeridianBandAMesh, GlowMaterial, BandColor, GlowPower * 0.85f, 0.15f);
	ApplyMeshTint(MeridianBandBMesh, GlowMaterial, NeutralGold, 2.2f + HitFlash * 3.0f, 0.2f);
	ApplyMeshTint(DirectionRingMesh, GlowMaterial, BandColor, 2.5f + HitFlash * 3.0f, 0.15f);

	if (DirectionRingMesh)
	{
		DirectionRingMesh->SetRelativeLocation(FVector(0.0f, 0.0f, -BallRadius + 8.0f));
	}

	if (ShockwaveRingMesh)
	{
		if (HitFlash > 0.01f || RecoveryFlash > 0.01f)
		{
			ShockwaveRingMesh->SetHiddenInGame(false);
			const float T = RecoveryFlash > 0.01f ? (1.0f - RecoveryFlash) : (1.0f - HitFlash);
			const float MaxRingScale = RecoveryFlash > 0.01f ? 4.8f : (bClientLastHitCharged ? 4.4f : 3.1f);
			const float RingScale = FMath::Lerp((BallRadius * 2.0f) / 100.0f, MaxRingScale, T);
			ShockwaveRingMesh->SetRelativeScale3D(FVector(RingScale, RingScale, 0.18f * (1.0f - T * 0.7f)));
			const FLinearColor RingCol = RecoveryFlash > 0.01f ? FLinearColor(0.35f, 0.95f, 1.0f, 1.0f) : BandColor;
			ApplyMeshTint(ShockwaveRingMesh, GlowMaterial, RingCol, 5.0f * (1.0f - T), 0.1f);
		}
		else
		{
			ShockwaveRingMesh->SetHiddenInGame(true);
		}
	}

	if (CoreLight)
	{
		CoreLight->SetLightColor(BandColor);
		CoreLight->SetIntensity(6500.0f + HitFlash * 11000.0f + RecoveryFlash * 9000.0f);
	}
}
