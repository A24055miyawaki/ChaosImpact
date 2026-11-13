#include "ChaosImpactCharacterPreview.h"
#include "ChaosImpactVictoryMotion.h"

#include "ChaosImpactBallTypes.h"
#include "ChaosImpactCharacterRoster.h"
#include "ChaosImpactMatchTypes.h"
#include "ChaosImpactPuppetComponent.h"
#include "ChaosImpactCustomColour.h"
#include "ChaosImpactPaintCache.h"
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

void AChaosImpactCharacterPreview::SetVictoryMotion(const bool bOn)
{
	if (bOn == bVictoryMotion)
	{
		return;
	}
	bVictoryMotion = bOn;
	VictoryStartedAge = Age;
	NextVictoryAppeal = Age;
	if (!bOn && Model)
	{
		// Back to the model as it was set up.
		if (!Model->HasAuthoredAnimations())
		{
			Model->SetRelativeTransform(FTransform(FRotator::ZeroRotator, FVector::ZeroVector, FVector(PreviewModelScale)));
		}
		Model->SetArmsRaised(0.0f);
		Model->SetDrivePose(0.0f, FVector::ZeroVector, 0.0f, FVector::ZeroVector);
	}
}

void AChaosImpactCharacterPreview::SetFacing(const float Yaw, const float Sway)
{
	if (bPaintView)
	{
		return;
	}
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
	// (A new character's pictures are its own: the old ones do not fit it.)
	PaintLayers.Reset();
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

void AChaosImpactCharacterPreview::ShowCustomColours(const TArray<uint8>& Parts)
{
	if (Model)
	{
		Model->SetCustomColours(ChaosImpactCustomColour::ToPartColours(ModelCharacter, Parts));
	}
}

void AChaosImpactCharacterPreview::ShowPaint(const TArray<UTexture*>& Pictures)
{
	PaintLayers.Reset();
	for (UTexture* Layer : Pictures)
	{
		PaintLayers.Add(Layer);
	}
	if (Model)
	{
		Model->SetPaintLayers(Pictures);
	}
}

void AChaosImpactCharacterPreview::ShowPresetPaint(const int32 Character, const int32 Preset)
{
	UChaosImpactPaintCache* Cache = UChaosImpactPaintCache::Get(this);
	ShowPaint(Cache && Preset >= 0 ? Cache->GetPresetTextures(Character, Preset) : TArray<UTexture*>());
}

void AChaosImpactCharacterPreview::ShowPaintKey(const int32 Key)
{
	UChaosImpactPaintCache* Cache = UChaosImpactPaintCache::Get(this);
	TArray<UTexture*> Pictures;
	if (Cache && !Cache->GetTextures(Key, Pictures))
	{
		Cache->Want(Key);
	}
	ShowPaint(Pictures);
}

void AChaosImpactCharacterPreview::BeginPaintView(const int32 Width, const int32 Height)
{
	if (bPaintView || !Camera)
	{
		return;
	}
	bPaintView = true;
	SavedCameraLocation = Camera->GetRelativeLocation();
	SavedCameraRotation = Camera->GetComponentRotation();
	SavedFOV = Camera->FOVAngle;
	SavedFacing = FacingYaw;
	SavedLights[0] = KeyLight->GetRelativeLocation();
	SavedLights[1] = FillLight->GetRelativeLocation();
	SavedLights[2] = RimLight->GetRelativeLocation();
	if (Picture && (Picture->SizeX != Width || Picture->SizeY != Height))
	{
		Picture->ResizeTarget(Width, Height);
	}
	Floor->SetVisibility(false);
	FloorRing->SetVisibility(false);
	SavedBackdropLocation = Backdrop->GetRelativeLocation();
	SavedBackdropRotation = Backdrop->GetComponentRotation();
	SavedBackdropScale = Backdrop->GetRelativeScale3D();
	Camera->FOVAngle = 30.0f;
	// Held still, square to the camera's start: the idle's first frame, its animation stopped (the pose component
	// ticks its own animation otherwise, and the body would keep moving under the pen).
	FacingYaw = 0.0f;
	SpinKick = 0.0f;
	Pose->SetRelativeRotation(FRotator::ZeroRotator);
	if (IdleAnimation)
	{
		Pose->PlayAnimation(IdleAnimation, true);
	}
	Pose->SetPosition(0.0f, false);
	Pose->bPauseAnims = true;
	Pose->TickAnimation(0.0f, false);
	Pose->RefreshBoneTransforms();
	FBox Box(ForceInit);
	if (Model)
	{
		Model->UpdatePose(true, 0.0f);
		TArray<USceneComponent*> Parts;
		Model->GetChildrenComponents(true, Parts);
		for (USceneComponent* Part : Parts)
		{
			if (const UPrimitiveComponent* Shape = Cast<UPrimitiveComponent>(Part); Shape && Shape->IsVisible())
			{
				Box += Shape->Bounds.GetBox();
			}
		}
	}
	if (Box.IsValid)
	{
		SetPaintFocus(Box.GetCenter(), static_cast<float>(Box.GetExtent().Size()) * 0.82f);
	}
	else
	{
		SetPaintFocus(GetActorLocation() + FVector(0.0f, 0.0f, 100.0f), 100.0f);
	}
	SetPaintView(90.0f, 8.0f, 1.0f, FVector2D::ZeroVector);
}

void AChaosImpactCharacterPreview::EndPaintView()
{
	if (!bPaintView)
	{
		return;
	}
	bPaintView = false;
	// Moving again (the idle carries on from where it stood).
	Pose->bPauseAnims = false;
	if (Picture && (Picture->SizeX != PictureWidth || Picture->SizeY != PictureHeight))
	{
		Picture->ResizeTarget(PictureWidth, PictureHeight);
	}
	Floor->SetVisibility(true);
	FloorRing->SetVisibility(true);
	Backdrop->SetRelativeLocation(SavedBackdropLocation);
	Backdrop->SetWorldRotation(SavedBackdropRotation);
	Backdrop->SetRelativeScale3D(SavedBackdropScale);
	Camera->SetRelativeLocation(SavedCameraLocation);
	Camera->SetWorldRotation(SavedCameraRotation);
	Camera->FOVAngle = SavedFOV;
	KeyLight->SetRelativeLocation(SavedLights[0]);
	FillLight->SetRelativeLocation(SavedLights[1]);
	RimLight->SetRelativeLocation(SavedLights[2]);
	FacingYaw = SavedFacing;
}

void AChaosImpactCharacterPreview::SetPaintFocus(const FVector& Centre, const float Radius)
{
	PaintFocus = Centre;
	PaintRadius = FMath::Max(Radius, 10.0f);
}

void AChaosImpactCharacterPreview::SetPaintView(const float Yaw, const float Pitch, const float Zoom, const FVector2D& Pan)
{
	if (!bPaintView || !Camera)
	{
		return;
	}
	const FVector Out = FRotator(FMath::Clamp(Pitch, -85.0f, 85.0f), Yaw, 0.0f).Vector();
	const FRotator Look = (-Out).Rotation();
	const FRotationMatrix Axes(Look);
	const FVector Right = Axes.GetUnitAxis(EAxis::Y);
	const FVector Up = Axes.GetUnitAxis(EAxis::Z);
	// Zoom 1: the whole model fits the picture (with a little room round it).
	const float HalfWidth = FMath::Tan(FMath::DegreesToRadians(Camera->FOVAngle * 0.5f));
	const float Aspect = Picture ? static_cast<float>(Picture->SizeX) / FMath::Max(Picture->SizeY, 1) : 1.0f;
	const float HalfShort = FMath::Min(HalfWidth, HalfWidth / FMath::Max(Aspect, 0.1f));
	const float Distance = PaintRadius * 1.05f / FMath::Max(HalfShort, 0.05f) / FMath::Clamp(Zoom, 0.2f, 20.0f);
	const FVector Centre = PaintFocus + Right * Pan.X + Up * Pan.Y;
	Camera->SetWorldLocation(Centre + Out * FMath::Max(Distance, 8.0f));
	Camera->SetWorldRotation(Look);
	// The backdrop stays behind the model, square to the camera and wider than it sees.
	const float Behind = PaintRadius * 2.2f;
	const float Seen = 2.0f * (Distance + Behind) * HalfWidth * 1.3f / 100.0f;
	Backdrop->SetWorldLocation(PaintFocus - Out * Behind);
	Backdrop->SetWorldRotation(FRotationMatrix::MakeFromYZ(Out, FVector::UpVector).Rotator());
	Backdrop->SetRelativeScale3D(FVector(FMath::Max(Seen, 4.0f), 0.05f, FMath::Max(Seen, 4.0f)));
	// The lights go round with the camera (the side being painted is always lit).
	const FRotator Turn(0.0f, Yaw - 90.0f, 0.0f);
	KeyLight->SetRelativeLocation(Turn.RotateVector(SavedLights[0]));
	FillLight->SetRelativeLocation(Turn.RotateVector(SavedLights[1]));
	RimLight->SetRelativeLocation(Turn.RotateVector(SavedLights[2]));
}

void AChaosImpactCharacterPreview::GetPictureRay(const FVector2D& PictureUV, FVector& OutOrigin, FVector& OutDirection) const
{
	OutOrigin = Camera->GetComponentLocation();
	const FRotationMatrix Axes(Camera->GetComponentRotation());
	const float Aspect = Picture ? static_cast<float>(Picture->SizeX) / FMath::Max(Picture->SizeY, 1) : 0.75f;
	const float HalfWidth = FMath::Tan(FMath::DegreesToRadians(Camera->FOVAngle * 0.5f));
	const float X = static_cast<float>(PictureUV.X * 2.0 - 1.0) * HalfWidth;
	const float Y = static_cast<float>(1.0 - PictureUV.Y * 2.0) * HalfWidth / Aspect;
	OutDirection = (Axes.GetUnitAxis(EAxis::X) + Axes.GetUnitAxis(EAxis::Y) * X + Axes.GetUnitAxis(EAxis::Z) * Y).GetSafeNormal();
}

bool AChaosImpactCharacterPreview::ProjectToPicture(const FVector& World, FVector2D& OutPictureUV) const
{
	const FRotationMatrix Axes(Camera->GetComponentRotation());
	const FVector Local = World - Camera->GetComponentLocation();
	const double Ahead = FVector::DotProduct(Local, Axes.GetUnitAxis(EAxis::X));
	if (Ahead <= 0.1)
	{
		return false;
	}
	const double Aspect = Picture ? static_cast<double>(Picture->SizeX) / FMath::Max(Picture->SizeY, 1) : 0.75;
	const double HalfWidth = FMath::Tan(FMath::DegreesToRadians(Camera->FOVAngle * 0.5));
	const double X = FVector::DotProduct(Local, Axes.GetUnitAxis(EAxis::Y)) / Ahead / HalfWidth;
	const double Y = FVector::DotProduct(Local, Axes.GetUnitAxis(EAxis::Z)) / Ahead / (HalfWidth / Aspect);
	OutPictureUV = FVector2D((X + 1.0) * 0.5, (1.0 - Y) * 0.5);
	return true;
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
	if (bPaintView)
	{
		// Held still while it is painted (the surface drawn on is the pose as it stands).
		Pose->SetRelativeRotation(FRotator::ZeroRotator);
		if (Model)
		{
			Model->UpdatePose(true, 0.0f);
		}
		return;
	}
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
	if (Model && bVictoryMotion)
	{
		// The celebration: the body layer over the idle clip, arms up or a finger to the sky; a model with its own clips
		// plays its appeal again and again instead (only lifted by the hop).
		const FChaosImpactVictoryPose Motion = ChaosImpactVictoryMotion::Evaluate(ModelCharacter, Age - VictoryStartedAge);
		if (Model->HasAuthoredAnimations())
		{
			Model->SetRelativeLocation(Motion.Offset);
			if (Age >= NextVictoryAppeal)
			{
				Model->PlaySelectionAppeal();
				NextVictoryAppeal = Age + ChaosImpactVictoryMotion::AppealEverySeconds;
			}
		}
		else
		{
			Model->SetRelativeTransform(ChaosImpactVictoryMotion::BodyTransform(Motion, PreviewModelScale));
		}
		Model->SetArmsRaised(Motion.ArmsRaised);
		Model->SetDrivePose(0.0f, FVector::ZeroVector, Motion.Point, GetActorLocation() + GetActorForwardVector() * 60.0f + FVector(0.0f, 0.0f, 320.0f));
	}
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
