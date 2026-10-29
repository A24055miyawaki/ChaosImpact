#include "ChaosImpactCharacterPreview.h"

#include "ChaosImpactBallTypes.h"
#include "ChaosImpactCharacterRoster.h"
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
#include "ContentStreaming.h"
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

	FloorRing = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FloorRing"));
	FloorRing->SetupAttachment(Stage);
	FloorRing->SetStaticMesh(Cylinder.Object);
	FloorRing->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FloorRing->SetRelativeLocation(FVector(0.0f, 0.0f, -4.5f));
	FloorRing->SetRelativeScale3D(FVector(2.65f, 2.65f, 0.03f));
	FloorRing->SetCastShadow(false);
	FloorRing->PrimaryComponentTick.bTickEvenWhenPaused = true;

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
	KeyLight->SetIntensity(11000.0f);
	KeyLight->SetAttenuationRadius(900.0f);
	KeyLight->SetCastShadows(false);

	FillLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FillLight"));
	FillLight->SetupAttachment(Stage);
	FillLight->SetUsingAbsoluteRotation(true);
	FillLight->SetRelativeLocation(FVector(170.0f, 220.0f, 130.0f));
	FillLight->SetIntensity(5200.0f);
	FillLight->SetAttenuationRadius(800.0f);
	FillLight->SetCastShadows(false);

	RimLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("RimLight"));
	RimLight->SetupAttachment(Stage);
	RimLight->SetUsingAbsoluteRotation(true);
	RimLight->SetRelativeLocation(FVector(110.0f, -120.0f, 200.0f));
	RimLight->SetIntensity(18500.0f);
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

	FloorMaterial = ChaosImpactBallTypes::MakeEmissive(this, FLinearColor(0.04f, 0.055f, 0.11f), 1.0f);
	FloorRingMaterial = ChaosImpactBallTypes::MakeEmissive(this, FLinearColor(0.15f, 0.45f, 0.95f), 2.2f);
	BackdropMaterial = ChaosImpactBallTypes::MakeEmissive(this, FLinearColor(0.015f, 0.022f, 0.048f), 1.0f);
	Floor->SetMaterial(0, FloorMaterial);
	if (FloorRing)
	{
		FloorRing->SetMaterial(0, FloorRingMaterial);
	}
	Backdrop->SetMaterial(0, BackdropMaterial);
	ReturnToIdle();
}

void AChaosImpactCharacterPreview::UseAsPortrait()
{
	Picture = NewObject<UTextureRenderTarget2D>(this);
	Picture->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8_SRGB;
	Picture->ClearColor = FLinearColor::Black;
	Picture->InitAutoFormat(PictureWidth / 2, PictureHeight / 2);
	Picture->UpdateResourceImmediate(true);
	Camera->TextureTarget = Picture;
	Camera->bCaptureEveryFrame = false;
	FilmNextFrame();
}

void AChaosImpactCharacterPreview::FilmNextFrame()
{
	if (Picture && Camera)
	{
		Camera->CaptureSceneDeferred();
	}
}

void AChaosImpactCharacterPreview::FilmNow()
{
	if (Picture && Camera)
	{
		Camera->bCaptureEveryFrame = false;
		Camera->CaptureScene();
	}
}

void AChaosImpactCharacterPreview::UseAsFigure()
{
	Camera->bCaptureEveryFrame = false;
	Camera->TextureTarget = nullptr;
	Camera->Deactivate();
	Picture = nullptr;
	Floor->SetVisibility(false);
	if (FloorRing)
	{
		FloorRing->SetVisibility(false);
	}
	Backdrop->SetVisibility(false);
	KeyLight->SetVisibility(false);
	if (FillLight)
	{
		FillLight->SetVisibility(false);
	}
	RimLight->SetVisibility(false);
}

void AChaosImpactCharacterPreview::PlayFigureAnimation(UAnimSequenceBase* Animation, const bool bLoop, const float HoldAt)
{
	if (!Animation)
	{
		return;
	}
	ReadyEndsAt = 0.0;
	bHolding = false;
	HoldAnimationAt = HoldAt;
	AnimationStartedAge = Age;
	Pose->PlayAnimation(Animation, bLoop);
	Pose->SetPlayRate(1.0f);
}

void AChaosImpactCharacterPreview::SetFacing(const float Yaw, const float Sway)
{
	FacingYaw = Yaw;
	SwayAmount = Sway;
}

UTextureRenderTarget2D* AChaosImpactCharacterPreview::ReleasePicture()
{
	UTextureRenderTarget2D* Released = Picture;
	if (Released)
	{
		Camera->bCaptureEveryFrame = false;
		Camera->TextureTarget = nullptr;
		Released->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
		Picture = nullptr;
	}
	return Released;
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
	if (Model && Model->HasAuthoredAnimations())
	{
		Model->SetRelativeRotation(FRotator(0.0f, 65.0f, 0.0f));
		const bool bBird = CharacterIndex == ChaosImpactRoster::BirdIndex;
		Camera->SetRelativeLocation(bBird ? FVector(0.f, 420.f, 120.f) : FVector(0.f, 380.f, 104.f));
		Camera->SetWorldRotation(FRotator(-2.f, -90.f, 0.f));
		Camera->FOVAngle = bBird ? 38.f : 36.f;
		Model->PlaySelectionAppeal();
	}
	else if (CharacterIndex == ChaosImpactRoster::CapIndex)
	{
		// Elevated camera angle for Mug Carp so the coffee inside the mug cup is clearly visible!
		Camera->SetRelativeLocation(FVector(0.0f, 345.0f, 185.0f));
		Camera->SetWorldRotation(FRotator(-14.0f, -90.0f, 0.0f));
		Camera->FOVAngle = 30.0f;
	}
	else
	{
		Camera->SetRelativeLocation(FVector(0.0f, 380.0f, 104.0f));
		Camera->SetWorldRotation(FRotator(-2.0f, -90.0f, 0.0f));
		Camera->FOVAngle = 30.0f;
	}
}

void AChaosImpactCharacterPreview::KeepTexturesSharp()
{
	TArray<UMeshComponent*> Meshes;
	GetComponents(Meshes, true);
	for (UMeshComponent* Mesh : Meshes)
	{
		if (Mesh && !Mesh->bForceMipStreaming)
		{
			Mesh->bForceMipStreaming = true;
			IStreamingManager::Get().NotifyPrimitiveUpdated(Mesh);
		}
		if (Mesh)
		{
			Mesh->PrestreamTextures(5.0f, true);
		}
	}
}

void AChaosImpactCharacterPreview::ShowLoadout(const int32 CharacterIndex, const int32 Colour, const FLinearColor& Accent)
{
	const bool bNewCharacter = ModelCharacter != CharacterIndex;
	RebuildModel(CharacterIndex);
	if (Model)
	{
		Model->SetColourIndex(Colour);
	}
	KeepTexturesSharp();
	// The stage takes the colour: a dim wash behind, a lit rim on the floor and the rim light.
	const FLinearColor Swatch = ChaosImpactMatch::GetTeamColor(Colour);
	const FLinearColor StageTint = FMath::Lerp(Accent, Swatch, 0.55f);
	if (BackdropMaterial)
	{
		const FLinearColor BackCol = StageTint * 0.075f + FLinearColor(0.012f, 0.016f, 0.035f);
		BackdropMaterial->SetVectorParameterValue(TEXT("Color"), BackCol);
		BackdropMaterial->SetVectorParameterValue(TEXT("Base Color"), BackCol);
	}
	if (FloorMaterial)
	{
		const FLinearColor FloorCol = StageTint * 0.16f + FLinearColor(0.02f, 0.028f, 0.055f);
		FloorMaterial->SetVectorParameterValue(TEXT("Color"), FloorCol);
		FloorMaterial->SetVectorParameterValue(TEXT("Base Color"), FloorCol);
	}
	if (FloorRingMaterial)
	{
		const FLinearColor RingCol = StageTint * 0.85f + FLinearColor(0.1f, 0.12f, 0.18f);
		FloorRingMaterial->SetVectorParameterValue(TEXT("Color"), RingCol);
		FloorRingMaterial->SetVectorParameterValue(TEXT("Base Color"), RingCol);
	}
	RimLight->SetLightColor(FMath::Lerp(Accent, Swatch, 0.5f));
	if (FillLight)
	{
		FillLight->SetLightColor(FMath::Lerp(FLinearColor(0.55f, 0.75f, 1.0f), StageTint, 0.35f));
	}
	KeyLight->SetLightColor(FLinearColor(1.0f, 0.97f, 0.92f));
	// A quick turn so the change is felt, not just seen.
	SpinKick += bNewCharacter ? 360.0f : 70.0f;
}

void AChaosImpactCharacterPreview::PlayReady()
{
	if (Model && Model->HasAuthoredAnimations())
	{
		Model->PlaySelectionAppeal();
		SpinKick += 40.0f;
		return;
	}
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
	const float Yaw = SwayAmount * FMath::Sin(Age * 0.7f) + SpinKick;
	Pose->SetRelativeRotation(FRotator(0.0f, FacingYaw + Yaw, 0.0f));
	// A held pose stops where it was asked to (and stays there).
	if (HoldAnimationAt >= 0.0f && !bHolding && Age - AnimationStartedAge >= HoldAnimationAt)
	{
		bHolding = true;
		Pose->SetPosition(HoldAnimationAt, false);
		Pose->SetPlayRate(0.0f);
	}
	// Evaluated here on the game thread, then copied onto the model.
	Pose->TickAnimation(bHolding ? 0.0f : DeltaSeconds, false);
	Pose->RefreshBoneTransforms();
	if (Model)
	{
		Model->UpdatePose(true, DeltaSeconds);
		if (USkeletalMeshComponent* Fish = Model->GetAuthoredMesh(); Fish && Picture)
		{
			// The appeal lifts and turns the whole fish. Keep it inside the portrait,
			// including its tail, rather than keeping a human-sized fixed framing.
			FBox PoseBox(ForceInit);
			for (int32 Bone = 0; Bone < Fish->GetNumBones(); ++Bone)
			{
				PoseBox += Fish->GetSocketLocation(Fish->GetBoneName(Bone));
			}
			// Imported animation bounds include every clip's travel and are much
			// larger than this frame's fish. Frame its current skeleton instead.
			PoseBox = PoseBox.ExpandBy(18.0f);
			if (ModelCharacter == ChaosImpactRoster::BirdIndex)
			{
				// Rigid-part bones sit at pivots: they do not cover the round body or tall head feathers.
				// Unlike the travelling fish clips, this short hovering loop has tight useful mesh bounds.
				PoseBox = Fish->Bounds.GetBox().ExpandBy(12.f);
			}
			const FVector Focus = PoseBox.GetCenter();
			const float Distance = ModelCharacter == ChaosImpactRoster::BirdIndex ? 330.f
				: FMath::Max(270.0f, static_cast<float>(PoseBox.GetExtent().Size()) * 3.0f);
			Camera->SetWorldLocation(Focus + FVector(0.0f, Distance, Distance * 0.14f));
			Camera->SetWorldRotation((Focus-Camera->GetComponentLocation()).Rotation());
		}
	}
}
