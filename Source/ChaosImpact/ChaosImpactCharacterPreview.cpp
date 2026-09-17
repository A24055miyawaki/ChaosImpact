#include "ChaosImpactCharacterPreview.h"

#include "ChaosImpactBallTypes.h"
#include "ChaosImpactMatchTypes.h"
#include "ChaosImpactPuppetComponent.h"
#include "Animation/AnimSequenceBase.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** The model's size in play (see AChaosImpactCharacter::ToonCharacterScale). */
	constexpr float PreviewModelScale = 1.45f;
}

AChaosImpactCharacterPreview::AChaosImpactCharacterPreview()
{
	PrimaryActorTick.bCanEverTick = false;
	SetReplicates(false);

	Stage = CreateDefaultSubobject<USceneComponent>(TEXT("Stage"));
	SetRootComponent(Stage);

	static ConstructorHelpers::FObjectFinder<USkeletalMesh> MannequinMesh(
		TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
	Pose = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Pose"));
	Pose->SetupAttachment(Stage);
	if (MannequinMesh.Succeeded())
	{
		Pose->SetSkeletalMeshAsset(MannequinMesh.Object);
	}
	Pose->SetHiddenInGame(true);
	Pose->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Pose->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Pose->PrimaryComponentTick.bTickEvenWhenPaused = true;
	// Three-quarter view: the model faces +Y, toward the camera, turned a little to its right.
	Pose->SetRelativeRotation(FRotator(0.0f, -22.0f, 0.0f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	Floor = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Floor"));
	Floor->SetupAttachment(Stage);
	Floor->SetStaticMesh(Cylinder.Object);
	Floor->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Floor->SetRelativeLocation(FVector(0.0f, 0.0f, -3.0f));
	Floor->SetRelativeScale3D(FVector(2.3f, 2.3f, 0.05f));
	Floor->SetCastShadow(false);
	Floor->PrimaryComponentTick.bTickEvenWhenPaused = true;

	Backdrop = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Backdrop"));
	Backdrop->SetupAttachment(Stage);
	Backdrop->SetStaticMesh(Cube.Object);
	Backdrop->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Backdrop->SetUsingAbsoluteRotation(true);
	Backdrop->SetRelativeLocation(FVector(0.0f, -220.0f, 110.0f));
	Backdrop->SetRelativeScale3D(FVector(12.0f, 0.05f, 10.0f));
	Backdrop->SetCastShadow(false);

	KeyLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("KeyLight"));
	KeyLight->SetupAttachment(Stage);
	KeyLight->SetUsingAbsoluteRotation(true);
	KeyLight->SetRelativeLocation(FVector(-150.0f, 240.0f, 240.0f));
	KeyLight->SetIntensity(9000.0f);
	KeyLight->SetAttenuationRadius(900.0f);
	KeyLight->SetCastShadows(false);

	RimLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("RimLight"));
	RimLight->SetupAttachment(Stage);
	RimLight->SetUsingAbsoluteRotation(true);
	RimLight->SetRelativeLocation(FVector(110.0f, -120.0f, 200.0f));
	RimLight->SetIntensity(16000.0f);
	RimLight->SetAttenuationRadius(700.0f);
	RimLight->SetCastShadows(false);

	Camera = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Camera"));
	Camera->SetupAttachment(Stage);
	Camera->SetUsingAbsoluteRotation(true);
	Camera->SetRelativeLocation(FVector(0.0f, 380.0f, 104.0f));
	Camera->SetWorldRotation(FRotator(-2.0f, -90.0f, 0.0f));
	Camera->FOVAngle = 30.0f;
	// Menus pause the world, and a capture only films while its component ticks.
	Camera->PrimaryComponentTick.bTickEvenWhenPaused = true;
	Camera->bCaptureEveryFrame = true;
	Camera->bCaptureOnMovement = false;
	Camera->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	Camera->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	Camera->ShowFlags.SetAtmosphere(false);
	Camera->ShowFlags.SetFog(false);
	Camera->ShowFlags.SetMotionBlur(false);
	Camera->PostProcessSettings.bOverride_AutoExposureMethod = true;
	Camera->PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
	Camera->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	Camera->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;
	Camera->PostProcessSettings.bOverride_AutoExposureBias = true;
	Camera->PostProcessSettings.AutoExposureBias = 0.0f;

	static ConstructorHelpers::FObjectFinder<UAnimSequenceBase> Idle(
		TEXT("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle.MM_Idle"));
	static ConstructorHelpers::FObjectFinder<UAnimSequenceBase> Ready(
		TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Attack/MM_Attack_01.MM_Attack_01"));
	IdleAnimation = Idle.Succeeded() ? Idle.Object : nullptr;
	ReadyAnimation = Ready.Succeeded() ? Ready.Object : nullptr;
}

void AChaosImpactCharacterPreview::BeginPlay()
{
	Super::BeginPlay();
	Picture = NewObject<UTextureRenderTarget2D>(this);
	Picture->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8_SRGB;
	Picture->ClearColor = FLinearColor::Black;
	Picture->InitAutoFormat(PictureWidth, PictureHeight);
	Picture->UpdateResourceImmediate(true);
	Camera->TextureTarget = Picture;
	Camera->ShowOnlyActors.Add(this);

	FloorMaterial = ChaosImpactBallTypes::MakeEmissive(this, FLinearColor(0.05f, 0.06f, 0.1f), 1.0f);
	BackdropMaterial = ChaosImpactBallTypes::MakeEmissive(this, FLinearColor(0.02f, 0.025f, 0.05f), 1.0f);
	Floor->SetMaterial(0, FloorMaterial);
	Backdrop->SetMaterial(0, BackdropMaterial);
	ReturnToIdle();
}

void AChaosImpactCharacterPreview::RebuildModel(const int32 CharacterIndex)
{
	if (ModelCharacter == CharacterIndex)
	{
		return;
	}
	if (Model)
	{
		Model->DestroyComponent();
		Model = nullptr;
	}
	Model = NewObject<UChaosImpactPuppetComponent>(this);
	Model->PrimaryComponentTick.bCanEverTick = false;
	Model->SetupAttachment(Pose);
	Model->SetRelativeScale3D(FVector(PreviewModelScale));
	Model->RegisterComponent();
	if (!Model->Initialize(Pose, CharacterIndex))
	{
		Model->DestroyComponent();
		Model = nullptr;
	}
	ModelCharacter = CharacterIndex;
}

void AChaosImpactCharacterPreview::ShowLoadout(const int32 CharacterIndex, const int32 Colour, const FLinearColor& Accent)
{
	const bool bNewCharacter = ModelCharacter != CharacterIndex;
	RebuildModel(CharacterIndex);
	if (Model)
	{
		Model->SetColourIndex(Colour);
	}
	// The stage takes the colour: a dim wash behind, a lit rim on the floor and the rim light.
	const FLinearColor Swatch = ChaosImpactMatch::GetTeamColor(Colour);
	if (BackdropMaterial)
	{
		BackdropMaterial->SetVectorParameterValue(TEXT("Color"), Swatch * 0.08f + FLinearColor(0.01f, 0.012f, 0.025f));
		BackdropMaterial->SetVectorParameterValue(TEXT("Base Color"), Swatch * 0.08f + FLinearColor(0.01f, 0.012f, 0.025f));
	}
	if (FloorMaterial)
	{
		FloorMaterial->SetVectorParameterValue(TEXT("Color"), Swatch * 0.25f);
		FloorMaterial->SetVectorParameterValue(TEXT("Base Color"), Swatch * 0.25f);
	}
	RimLight->SetLightColor(FMath::Lerp(Accent, Swatch, 0.5f));
	KeyLight->SetLightColor(FLinearColor(1.0f, 0.97f, 0.92f));
	// A quick turn so the change is felt, not just seen.
	SpinKick += bNewCharacter ? 360.0f : 70.0f;
}

void AChaosImpactCharacterPreview::PlayReady()
{
	if (!ReadyAnimation || !GetWorld())
	{
		return;
	}
	Pose->PlayAnimation(ReadyAnimation, false);
	ReadyEndsAt = Age + ReadyAnimation->GetPlayLength();
	SpinKick += 40.0f;
}

void AChaosImpactCharacterPreview::ReturnToIdle()
{
	ReadyEndsAt = 0.0;
	if (IdleAnimation)
	{
		Pose->PlayAnimation(IdleAnimation, true);
	}
}

void AChaosImpactCharacterPreview::Animate(const float DeltaSeconds)
{
	Age += DeltaSeconds;
	if (ReadyEndsAt > 0.0 && Age >= ReadyEndsAt)
	{
		ReturnToIdle();
	}
	// A slow sway, plus a spin that settles after each change.
	SpinKick = FMath::FInterpTo(SpinKick, 0.0f, DeltaSeconds, 7.0f);
	const float Yaw = 14.0f * FMath::Sin(Age * 0.7f) + SpinKick;
	Pose->SetRelativeRotation(FRotator(0.0f, -22.0f + Yaw, 0.0f));
	// Evaluated here on the game thread, then copied onto the model.
	Pose->TickAnimation(DeltaSeconds, false);
	Pose->RefreshBoneTransforms();
	if (Model)
	{
		Model->UpdatePose(true);
	}
}
