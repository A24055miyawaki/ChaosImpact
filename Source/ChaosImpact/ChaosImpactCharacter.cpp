// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosImpactCharacter.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactChargeWidget.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactGameMode.h"
#include "ChaosImpactHazardZone.h"
#include "ChaosImpactIceMeshes.h"
#include "NiagaraComponent.h"
#include "ProceduralMeshComponent.h"
#include "ChaosImpactPlayerController.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "ChaosImpactTrainingTarget.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Camera/CameraComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "Framework/Application/SlateApplication.h"
#include "EnhancedInputComponent.h"
#include "InputAction.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "ChaosImpact.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
	FVector2D ApplyRadialStickDeadZone(const FVector2D Input, const float DeadZone)
	{
		const float Magnitude = Input.Size();
		if (!FMath::IsFinite(Magnitude) || Magnitude <= DeadZone)
		{
			return FVector2D::ZeroVector;
		}
		const float RemappedMagnitude = FMath::Clamp(
			(Magnitude - DeadZone) / FMath::Max(1.0f - DeadZone, UE_SMALL_NUMBER), 0.0f, 1.0f);
		return Input.GetSafeNormal() * RemappedMagnitude;
	}
}

AChaosImpactCharacter::AChaosImpactCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);
		
	// Rotation follows the current aim direction, independently from movement.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Configure character movement
	GetCharacterMovement()->bOrientRotationToMovement = false;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 500.0f, 0.0f);

	// Note: For faster iteration times these variables, and many more, can be tweaked in the Character Blueprint
	// instead of recompiling to adjust them
	GetCharacterMovement()->JumpZVelocity = 500.f;
	GetCharacterMovement()->AirControl = 0.35f;
	GetCharacterMovement()->MaxWalkSpeed = 560.f;
	GetCharacterMovement()->MinAnalogWalkSpeed = 20.f;
	GetCharacterMovement()->MaxAcceleration = 100000.0f;
	GetCharacterMovement()->BrakingDecelerationWalking = 100000.0f;
	GetCharacterMovement()->GroundFriction = 100.0f;
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;
	// Online: draw other players closer to their latest replicated position.
	GetCharacterMovement()->NetworkSimulatedSmoothLocationTime = 0.06f;
	GetCharacterMovement()->NetworkSimulatedSmoothRotationTime = 0.04f;
	SetCanBeDamaged(true);

	// Fixed-angle top-down camera that follows the player.
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->SetUsingAbsoluteRotation(true);
	CameraBoom->TargetArmLength = 800.0f;
	CameraBoom->SetRelativeRotation(FRotator(-60.0f, 0.0f, 0.0f));
	CameraBoom->bUsePawnControlRotation = false;
	CameraBoom->bInheritPitch = false;
	CameraBoom->bInheritYaw = false;
	CameraBoom->bInheritRoll = false;
	CameraBoom->bDoCollisionTest = false;
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->CameraLagSpeed = 8.0f;

	// Create a follow camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	HeldBallMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HeldBallMesh"));
	HeldBallMesh->SetupAttachment(GetMesh(), TEXT("hand_r"));
	HeldBallMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HeldBallMesh->SetGenerateOverlapEvents(false);
	HeldBallMesh->SetRelativeScale3D(FVector(0.34f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> HeldSphereMesh(
		TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (HeldSphereMesh.Succeeded())
	{
		HeldBallMesh->SetStaticMesh(HeldSphereMesh.Object);
	}

	LeftHeldBallMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LeftHeldBallMesh"));
	LeftHeldBallMesh->SetupAttachment(GetMesh(), TEXT("hand_l"));
	LeftHeldBallMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	LeftHeldBallMesh->SetGenerateOverlapEvents(false);
	LeftHeldBallMesh->SetRelativeScale3D(FVector(0.34f));
	if (HeldSphereMesh.Succeeded())
	{
		LeftHeldBallMesh->SetStaticMesh(HeldSphereMesh.Object);
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> EliminationCubeMesh(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> EliminationMaterial(
		TEXT("/Game/LevelPrototyping/Materials/M_FlatCol.M_FlatCol"));
	for (int32 PieceIndex = 0; PieceIndex < 3; ++PieceIndex)
	{
		UStaticMeshComponent* GuidePiece = CreateDefaultSubobject<UStaticMeshComponent>(
			*FString::Printf(TEXT("AimGuidePiece_%02d"), PieceIndex));
		GuidePiece->SetupAttachment(GetCapsuleComponent());
		GuidePiece->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		GuidePiece->SetCastShadow(false);
		GuidePiece->SetOnlyOwnerSee(true);
		GuidePiece->SetOwnerNoSee(false);
		GuidePiece->SetHiddenInGame(true);
		if (EliminationCubeMesh.Succeeded())
		{
			GuidePiece->SetStaticMesh(EliminationCubeMesh.Object);
		}
		if (EliminationMaterial.Succeeded())
		{
			GuidePiece->SetMaterial(0, EliminationMaterial.Object);
		}
		AimGuidePieces.Add(GuidePiece);
	}
	for (int32 PieceIndex = 0; PieceIndex < 5; ++PieceIndex)
	{
		UStaticMeshComponent* TrailPiece = CreateDefaultSubobject<UStaticMeshComponent>(
			*FString::Printf(TEXT("DashTrailPiece_%02d"), PieceIndex));
		TrailPiece->SetupAttachment(GetCapsuleComponent());
		TrailPiece->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		TrailPiece->SetCastShadow(false);
		TrailPiece->SetHiddenInGame(true);
		if (EliminationCubeMesh.Succeeded())
		{
			TrailPiece->SetStaticMesh(EliminationCubeMesh.Object);
		}
		if (EliminationMaterial.Succeeded())
		{
			TrailPiece->SetMaterial(0, EliminationMaterial.Object);
		}
		DashTrailPieces.Add(TrailPiece);
	}
	for (int32 PieceIndex = 0; PieceIndex < 30; ++PieceIndex)
	{
		UStaticMeshComponent* Piece = CreateDefaultSubobject<UStaticMeshComponent>(
			*FString::Printf(TEXT("PlayerEliminationPiece_%02d"), PieceIndex));
		Piece->SetupAttachment(GetCapsuleComponent());
		Piece->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Piece->SetHiddenInGame(true);
		if (EliminationCubeMesh.Succeeded())
		{
			Piece->SetStaticMesh(EliminationCubeMesh.Object);
		}
		if (EliminationMaterial.Succeeded())
		{
			Piece->SetMaterial(0, EliminationMaterial.Object);
		}
		EliminationPieces.Add(Piece);
	}

	EliminationFlash = CreateDefaultSubobject<UPointLightComponent>(TEXT("PlayerEliminationFlash"));
	EliminationFlash->SetupAttachment(GetCapsuleComponent());
	EliminationFlash->SetRelativeLocation(FVector(0.0f, 0.0f, 70.0f));
	EliminationFlash->SetLightColor(FLinearColor(0.0f, 0.65f, 1.0f));
	EliminationFlash->SetAttenuationRadius(620.0f);
	EliminationFlash->SetCastShadows(false);
	EliminationFlash->SetIntensity(0.0f);

	static ConstructorHelpers::FObjectFinder<UAnimSequenceBase> ThrowAnimationAsset(
		TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Attack/MM_Attack_01.MM_Attack_01"));
	if (ThrowAnimationAsset.Succeeded())
	{
		ThrowAnimation = ThrowAnimationAsset.Object;
	}

	BallClass = AChaosImpactBall::StaticClass();

	// Note: The skeletal mesh and anim blueprint references on the Mesh component (inherited from Character) 
	// are set in the derived blueprint asset named ThirdPersonCharacter (to avoid direct content references in C++)
}

void AChaosImpactCharacter::BeginPlay()
{
	Super::BeginPlay();
	if (CameraBoom)
	{
		// Preserve Blueprint/editor camera tuning as the state restored after the menu.
		SavedCameraArmLength = CameraBoom->TargetArmLength;
		SavedCameraSocketOffset = CameraBoom->SocketOffset;
	}

	Health = MaxHealth;
	Stamina = MaxStamina;
	ClearCarriedBalls();
	NextDashAvailableAtSeconds = 0.0f;
	// Restored when leaving frozen ground.
	DefaultGroundFriction = GetCharacterMovement()->GroundFriction;
	DefaultBrakingDecelerationWalking = GetCharacterMovement()->BrakingDecelerationWalking;
	DefaultMaxAcceleration = GetCharacterMovement()->MaxAcceleration;
	InitialSpawnLocation = GetActorLocation();
	InitialSpawnRotation = GetActorRotation();
	LocomotionAnimInstanceClass = GetMesh() ? GetMesh()->GetAnimClass() : nullptr;
	AimDirection = GetActorForwardVector().GetSafeNormal2D();
	InitialMeshRelativeScale = GetMesh()->GetRelativeScale3D();
	for (UStaticMeshComponent* GuidePiece : AimGuidePieces)
	{
		if (UMaterialInstanceDynamic* Material = GuidePiece->CreateDynamicMaterialInstance(0))
		{
			const FLinearColor Color(0.0f, 0.82f, 1.0f, 1.0f);
			Material->SetVectorParameterValue(TEXT("Base Color"), Color);
			Material->SetVectorParameterValue(TEXT("BaseColor"), Color);
			Material->SetVectorParameterValue(TEXT("Color"), Color);
		}
	}
	for (UStaticMeshComponent* TrailPiece : DashTrailPieces)
	{
		if (UMaterialInstanceDynamic* Material = TrailPiece->CreateDynamicMaterialInstance(0))
		{
			const FLinearColor Color(0.04f, 0.62f, 1.0f, 1.0f);
			Material->SetVectorParameterValue(TEXT("Base Color"), Color);
			Material->SetVectorParameterValue(TEXT("BaseColor"), Color);
			Material->SetVectorParameterValue(TEXT("Color"), Color);
		}
	}
	for (int32 PieceIndex = 0; PieceIndex < EliminationPieces.Num(); ++PieceIndex)
	{
		const float GoldenAngle = PieceIndex * 2.39996323f;
		const float Height = FMath::Lerp(-0.35f, 0.95f,
			static_cast<float>(PieceIndex % 11) / 10.0f);
		const float Radius = FMath::Sqrt(FMath::Max(0.0f, 1.0f - Height * Height));
		EliminationPieceDirections.Add(FVector(FMath::Cos(GoldenAngle) * Radius,
			FMath::Sin(GoldenAngle) * Radius, Height));
		if (UMaterialInstanceDynamic* Material = EliminationPieces[PieceIndex]
			->CreateDynamicMaterialInstance(0))
		{
			const FLinearColor Color = PieceIndex % 2 == 0
				? FLinearColor(0.0f, 0.72f, 1.0f) : FLinearColor(1.0f, 0.035f, 0.13f);
			Material->SetVectorParameterValue(TEXT("Base Color"), Color);
			Material->SetVectorParameterValue(TEXT("BaseColor"), Color);
			Material->SetVectorParameterValue(TEXT("Color"), Color);
		}
	}
	UpdateBallPresentation();

	TryCreateChargeWidget();
	CachedBaseTranslationOffset = BaseTranslationOffset;
#if !UE_BUILD_SHIPPING
	bDevAutoInput = FParse::Param(FCommandLine::Get(), TEXT("CIAutoInput"));
#endif
}

void AChaosImpactCharacter::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority())
	{
		UpdateMovementAuthority();
	}
	UpdatePresentationLead(DeltaSeconds);
	UpdateIceStatus(DeltaSeconds);
	if (CameraBoom)
	{
		const float DesiredArmLength = bTrainingMenuCameraActive
			? TrainingMenuCameraArmLength : SavedCameraArmLength;
		const FVector DesiredSocketOffset = bTrainingMenuCameraActive
			? TrainingMenuCameraSocketOffset : SavedCameraSocketOffset;
		CameraBoom->TargetArmLength = FMath::FInterpTo(CameraBoom->TargetArmLength,
			DesiredArmLength, DeltaSeconds, TrainingMenuCameraBlendSpeed);
		CameraBoom->SocketOffset = FMath::VInterpTo(CameraBoom->SocketOffset,
			DesiredSocketOffset, DeltaSeconds, TrainingMenuCameraBlendSpeed);
	}
	if (bEliminationEffectActive)
	{
		UpdateEliminationEffect(DeltaSeconds);
	}
	if (bRespawnEffectActive)
	{
		UpdateRespawnEffect(DeltaSeconds);
	}
	TryCreateChargeWidget();
	if (const APlayerController* PlayerController = Cast<APlayerController>(GetController());
		PlayerController && PlayerController->IsLocalController())
	{
		const bool bMouseDown = PlayerController->IsInputKeyDown(EKeys::LeftMouseButton)
			|| (FSlateApplication::IsInitialized()
				&& FSlateApplication::Get().GetPressedMouseButtons().Contains(EKeys::LeftMouseButton));
		const AChaosImpactPlayerController* MenuController =
			Cast<AChaosImpactPlayerController>(PlayerController);
		const bool bCanUseMouse = !MenuController || !MenuController->IsUsingGamepad();
		const bool bCanReadThrow = bCanUseMouse
			&& (!MenuController || MenuController->IsGameplayActive());
		const bool bMousePressedThisTick = bMouseDown && !bWasMouseDownLastTick;
		const bool bMouseReleasedThisTick = !bMouseDown && bWasMouseDownLastTick;
		if (bCanReadThrow && bMousePressedThisTick)
		{
			if (!bIsChargingThrow)
			{
				StartChargingThrow();
			}
			bMouseChargeActive = bIsChargingThrow;
		}
		else if ((bMouseReleasedThisTick || !bCanReadThrow) && bMouseChargeActive)
		{
			if (bIsChargingThrow)
			{
				ReleaseChargedThrow();
			}
			bMouseChargeActive = false;
		}
		bWasMouseDownLastTick = bMouseDown;
	}
	if (bDevAutoInput && IsLocallyControlled() && !HasAuthority())
	{
		TickDevAutoInput();
	}

	if (!bEliminated)
	{
		if (!bTrainingMenuFrozen)
		{
			UpdateAim(DeltaSeconds);
		}

		if (bIsDashing && !bTrainingMenuFrozen)
		{
			UpdateDash(DeltaSeconds);
		}
		else
		{
			Stamina = FMath::Min(MaxStamina, Stamina + StaminaRegenPerSecond * DeltaSeconds);
		}
		if (GetLocalRole() == ROLE_SimulatedProxy)
		{
			DashDirection = ReplicatedDashDirection;
			UpdateDashTrailPresentation(bReplicatedDashing);
		}
	}

	if (ChargeWidget)
	{
		ChargeWidget->SetHealth(Health, MaxHealth);
		ChargeWidget->SetStamina(Stamina, MaxStamina);
		if (bEliminated && RespawnAtWorldSeconds > 0.0f && GetWorld())
		{
			ChargeWidget->UpdateRespawn(
				FMath::Max(0.0f, RespawnAtWorldSeconds - GetWorld()->GetTimeSeconds()),
				EliminationResetDelay);
		}
	}

	if (bIsChargingThrow)
	{
		const float ChargeAlpha = GetThrowChargeAlpha();
		OnThrowChargeChanged(ChargeAlpha);
		if (ChargeWidget)
		{
			ChargeWidget->SetChargeAlpha(ChargeAlpha);
		}

	}
	UpdateAimGuidePresentation(bIsChargingThrow && !bEliminated && !bTrainingMenuFrozen);
}

void AChaosImpactCharacter::TryCreateChargeWidget()
{
	if (ChargeWidget)
	{
		return;
	}

	APlayerController* PlayerController = Cast<APlayerController>(GetController());
	// CreatePlayer can possess the pawn one tick before its ULocalPlayer is
	// attached. Wait for that attachment so CreateWidget always has a valid
	// per-player viewport owner in split-screen play.
	if (!PlayerController || !PlayerController->IsLocalController()
		|| !PlayerController->GetLocalPlayer())
	{
		return;
	}

	ChargeWidget = CreateWidget<UChaosImpactChargeWidget>(
		PlayerController, UChaosImpactChargeWidget::StaticClass());
	if (ChargeWidget)
	{
		ChargeWidget->AddToPlayerScreen(20);
		ChargeWidget->SetCharging(false);
		ChargeWidget->SetHealth(Health, MaxHealth);
		ChargeWidget->SetStamina(Stamina, MaxStamina);
		ChargeWidget->SetBallInventory(CarriedBallCount, MaximumCarriedBalls, CarriedBallTypes);
		const AChaosImpactPlayerController* MenuController = Cast<AChaosImpactPlayerController>(PlayerController);
		ChargeWidget->SetVisibility(!MenuController || MenuController->IsGameplayActive()
			? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		UE_LOG(LogChaosImpact, Log, TEXT("Charge gauge created for %s"), *GetName());
	}
}

void AChaosImpactCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent)) {
		// Blueprint assignments can be cleared by reparenting or recompiling. Resolve
		// the template assets here as runtime fallbacks so controls always remain usable.
		UInputAction* ResolvedJumpAction = JumpAction;
		UInputAction* ResolvedMoveAction = MoveAction;
		UInputAction* ResolvedLookAction = LookAction;
		if (!ResolvedJumpAction)
		{
			ResolvedJumpAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Jump.IA_Jump"));
		}
		if (!ResolvedMoveAction)
		{
			ResolvedMoveAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Move.IA_Move"));
		}
		if (!ResolvedLookAction)
		{
			ResolvedLookAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Look.IA_Look"));
		}
		
		// Jumping
		if (ResolvedJumpAction)
		{
			EnhancedInputComponent->BindAction(ResolvedJumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
			EnhancedInputComponent->BindAction(ResolvedJumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);
		}

		// Moving
		if (ResolvedMoveAction)
		{
			EnhancedInputComponent->BindAction(ResolvedMoveAction, ETriggerEvent::Triggered, this, &AChaosImpactCharacter::Move);
		}
		// Mouse aim is calculated from the cursor against the world.

		// Right-stick aiming
		if (ResolvedLookAction)
		{
			EnhancedInputComponent->BindAction(ResolvedLookAction, ETriggerEvent::Triggered, this, &AChaosImpactCharacter::AimWithStick);
			EnhancedInputComponent->BindAction(ResolvedLookAction, ETriggerEvent::Completed, this, &AChaosImpactCharacter::StopAimingWithStick);
		}
	}
	else
	{
		UE_LOG(LogChaosImpact, Error, TEXT("'%s' Failed to find an Enhanced Input component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}

	PlayerInputComponent->BindKey(EKeys::Gamepad_RightTrigger, IE_Pressed, this, &AChaosImpactCharacter::StartChargingThrow);
	PlayerInputComponent->BindKey(EKeys::Gamepad_RightTrigger, IE_Released, this, &AChaosImpactCharacter::ReleaseChargedThrow);
	PlayerInputComponent->BindKey(EKeys::Gamepad_FaceButton_Left, IE_Pressed, this, &AChaosImpactCharacter::StartChargingThrow);
	PlayerInputComponent->BindKey(EKeys::Gamepad_FaceButton_Left, IE_Released, this, &AChaosImpactCharacter::ReleaseChargedThrow);
	PlayerInputComponent->BindKey(EKeys::G, IE_Pressed, this, &AChaosImpactCharacter::StartChargingThrow);
	PlayerInputComponent->BindKey(EKeys::G, IE_Released, this, &AChaosImpactCharacter::ReleaseChargedThrow);
	PlayerInputComponent->BindKey(EKeys::Gamepad_FaceButton_Top, IE_Pressed, this, &AChaosImpactCharacter::StartChargingThrow);
	PlayerInputComponent->BindKey(EKeys::Gamepad_FaceButton_Top, IE_Released, this, &AChaosImpactCharacter::ReleaseChargedThrow);
	PlayerInputComponent->BindKey(EKeys::LeftShift, IE_Pressed, this, &AChaosImpactCharacter::StartDash);
	PlayerInputComponent->BindKey(EKeys::RightShift, IE_Pressed, this, &AChaosImpactCharacter::StartDash);
	PlayerInputComponent->BindKey(EKeys::Gamepad_FaceButton_Right, IE_Pressed, this, &AChaosImpactCharacter::StartDash);
	PlayerInputComponent->BindKey(EKeys::Gamepad_RightShoulder, IE_Pressed, this, &AChaosImpactCharacter::StartDash);
	PlayerInputComponent->BindKey(EKeys::Gamepad_FaceButton_Bottom, IE_Pressed, this, &ACharacter::Jump);
	PlayerInputComponent->BindKey(EKeys::Gamepad_FaceButton_Bottom, IE_Released, this, &ACharacter::StopJumping);
}

void AChaosImpactCharacter::Move(const FInputActionValue& Value)
{
	FVector2D RawMovement = Value.Get<FVector2D>();
	bool bApplyGamepadDeadZone = true;
	if (const APlayerController* PlayerController = Cast<APlayerController>(GetController()))
	{
		const AChaosImpactPlayerController* InputController =
			Cast<AChaosImpactPlayerController>(PlayerController);
		const bool bGamepadPlayer = !InputController || InputController->IsUsingGamepad();
		if (bGamepadPlayer)
		{
			// IMC_Default's old dead-zone modifier can normalize tiny hardware drift to
			// a full-length vector. Read this player's physical axes before that modifier.
			RawMovement = FVector2D(
				PlayerController->GetInputAnalogKeyState(EKeys::Gamepad_LeftX),
				PlayerController->GetInputAnalogKeyState(EKeys::Gamepad_LeftY));
		}
		else
		{
			bApplyGamepadDeadZone = false;
			// IA_Move contains keyboard and gamepad mappings. Enhanced Input evaluates the
			// whole action before PlayerController::InputKey can reject the other device,
			// so using Value here lets an attached pad leak into keyboard-only P1. Rebuild
			// the digital vector from the keys that this input mode actually owns.
			RawMovement = FVector2D(
				(PlayerController->IsInputKeyDown(EKeys::D) || PlayerController->IsInputKeyDown(EKeys::Right) ? 1.0f : 0.0f)
					- (PlayerController->IsInputKeyDown(EKeys::A) || PlayerController->IsInputKeyDown(EKeys::Left) ? 1.0f : 0.0f),
				(PlayerController->IsInputKeyDown(EKeys::W) || PlayerController->IsInputKeyDown(EKeys::Up) ? 1.0f : 0.0f)
					- (PlayerController->IsInputKeyDown(EKeys::S) || PlayerController->IsInputKeyDown(EKeys::Down) ? 1.0f : 0.0f));
		}
	}
	const FVector2D MovementVector = ApplyRadialStickDeadZone(RawMovement,
		bApplyGamepadDeadZone ? MovementStickDeadZone : 0.0f);

	// route the input
	DoMove(MovementVector.X, MovementVector.Y);
}

void AChaosImpactCharacter::AimWithStick(const FInputActionValue& Value)
{
	FVector2D RawAim = Value.Get<FVector2D>();
	if (const APlayerController* PlayerController = Cast<APlayerController>(GetController()))
	{
		if (const AChaosImpactPlayerController* InputController =
			Cast<AChaosImpactPlayerController>(PlayerController);
			InputController && !InputController->IsUsingGamepad())
		{
			StickAimInput = FVector2D::ZeroVector;
			return;
		}
		RawAim = FVector2D(
			PlayerController->GetInputAnalogKeyState(EKeys::Gamepad_RightX),
			PlayerController->GetInputAnalogKeyState(EKeys::Gamepad_RightY));
	}
	StickAimInput = ApplyRadialStickDeadZone(
		ConvertRawControllerAimAxes(RawAim), StickAimDeadZone);
}

FVector2D AChaosImpactCharacter::ConvertRawControllerAimAxes(const FVector2D& RawAxes)
{
	// Engine-facing controller axes use right-positive X and down-positive Y.
	// The top-down aim basis uses right-positive X and up-positive Y, so only the
	// vertical axis is inverted. Inverting X instead preserves circular handedness
	// but rotates every cardinal direction by 180 degrees (right aims left, down up).
	return FVector2D(RawAxes.X, -RawAxes.Y);
}

void AChaosImpactCharacter::StopAimingWithStick(const FInputActionValue& Value)
{
	StickAimInput = FVector2D::ZeroVector;
}

void AChaosImpactCharacter::DoMove(float Right, float Forward)
{
	const AChaosImpactPlayerController* MenuController = Cast<AChaosImpactPlayerController>(GetController());
	if (bEliminated || bTrainingMenuFrozen || IsIceFrozen()
		|| (MenuController && !MenuController->IsGameplayActive()))
	{
		return;
	}
	if (GetController() != nullptr)
	{
		const FRotator CameraYaw(0.0f, CameraBoom->GetComponentRotation().Yaw, 0.0f);
		const FVector ForwardDirection = FRotationMatrix(CameraYaw).GetUnitAxis(EAxis::X);
		const FVector RightDirection = FRotationMatrix(CameraYaw).GetUnitAxis(EAxis::Y);

		const FVector DesiredMoveDirection =
			(ForwardDirection * Forward + RightDirection * Right).GetClampedToMaxSize(1.0f);
		if (!DesiredMoveDirection.IsNearlyZero())
		{
			LastMoveDirection = DesiredMoveDirection.GetSafeNormal2D();
		}

		if (!bIsDashing)
		{
			AddMovementInput(DesiredMoveDirection, 1.0f);
		}
	}
}

void AChaosImpactCharacter::DoLook(float Yaw, float Pitch)
{
	StickAimInput = FVector2D(Yaw, Pitch);
}

void AChaosImpactCharacter::DoJumpStart()
{
	// signal the character to jump
	Jump();
}

void AChaosImpactCharacter::DoJumpEnd()
{
	// signal the character to stop jumping
	StopJumping();
}

float AChaosImpactCharacter::GetThrowChargeAlpha() const
{
	if (!bIsChargingThrow || !GetWorld())
	{
		return 0.0f;
	}

	return FMath::Clamp((GetWorld()->GetTimeSeconds() - ThrowChargeStartedAt) /
		FMath::Max(MaxChargeSeconds, UE_SMALL_NUMBER), 0.0f, 1.0f);
}

void AChaosImpactCharacter::StartChargingThrow()
{
	const AChaosImpactPlayerController* MenuController = Cast<AChaosImpactPlayerController>(GetController());
	if (MenuController && !MenuController->IsGameplayActive())
	{
		return;
	}
	if (bEliminated || bTrainingMenuFrozen || bIsDashing || bIsChargingThrow || bThrowReleasePending
		|| CarriedBallCount <= 0 || !GetWorld() || IsEliminationPredicted() || IsIceFrozen())
	{
		return;
	}

	bIsChargingThrow = true;
	ThrowChargeStartedAt = GetWorld()->GetTimeSeconds();
	if (!HasAuthority())
	{
		ServerStartCharge();
	}
	OnThrowChargeChanged(0.0f);
	if (ChargeWidget)
	{
		ChargeWidget->SetChargeAlpha(0.0f);
		ChargeWidget->SetCharging(true);
	}
}

void AChaosImpactCharacter::StartDash()
{
	const AChaosImpactPlayerController* MenuController = Cast<AChaosImpactPlayerController>(GetController());
	if (MenuController && !MenuController->IsGameplayActive())
	{
		return;
	}
	if (!GetWorld() || bEliminated || bTrainingMenuFrozen || bIsDashing || Stamina + UE_SMALL_NUMBER < DashCost
		|| GetWorld()->GetTimeSeconds() < NextDashAvailableAtSeconds || IsIceFrozen())
	{
		return;
	}

	DashDirection = LastMoveDirection.GetSafeNormal2D();
	if (const APlayerController* PlayerController = Cast<APlayerController>(GetController()))
	{
		const AChaosImpactPlayerController* InputController =
			Cast<AChaosImpactPlayerController>(PlayerController);
		const bool bUseKeyboard = !InputController || !InputController->IsUsingGamepad();
		const bool bUseGamepad = !InputController || InputController->IsUsingGamepad();
		const float KeyboardForward = bUseKeyboard ?
			(PlayerController->IsInputKeyDown(EKeys::W) ? 1.0f : 0.0f)
			- (PlayerController->IsInputKeyDown(EKeys::S) ? 1.0f : 0.0f) : 0.0f;
		const float KeyboardRight = bUseKeyboard ?
			(PlayerController->IsInputKeyDown(EKeys::D) ? 1.0f : 0.0f)
			- (PlayerController->IsInputKeyDown(EKeys::A) ? 1.0f : 0.0f) : 0.0f;
			float ForwardInput = KeyboardForward
				+ (bUseGamepad ? PlayerController->GetInputAnalogKeyState(EKeys::Gamepad_LeftY) : 0.0f);
			float RightInput = KeyboardRight
				+ (bUseGamepad ? PlayerController->GetInputAnalogKeyState(EKeys::Gamepad_LeftX) : 0.0f);
			if (bUseGamepad && !bUseKeyboard)
			{
				const FVector2D Filtered = ApplyRadialStickDeadZone(
					FVector2D(RightInput, ForwardInput), MovementStickDeadZone);
				RightInput = Filtered.X;
				ForwardInput = Filtered.Y;
			}

		if (!FVector2D(RightInput, ForwardInput).IsNearlyZero())
		{
			const FRotator CameraYaw(0.0f, CameraBoom->GetComponentRotation().Yaw, 0.0f);
			const FVector CameraForward = FRotationMatrix(CameraYaw).GetUnitAxis(EAxis::X);
			const FVector CameraRight = FRotationMatrix(CameraYaw).GetUnitAxis(EAxis::Y);
			DashDirection = (CameraForward * ForwardInput + CameraRight * RightInput).GetSafeNormal2D();
		}
	}
	if (DashDirection.IsNearlyZero())
	{
		DashDirection = AimDirection.GetSafeNormal2D();
	}
	if (DashDirection.IsNearlyZero())
	{
		DashDirection = GetActorForwardVector().GetSafeNormal2D();
	}
	if (!HasAuthority())
	{
		ServerStartDash(DashDirection);
	}
	PerformDash(DashDirection);
}

void AChaosImpactCharacter::PerformDash(const FVector& Direction)
{
	if (!GetWorld() || bEliminated || bTrainingMenuFrozen || bIsDashing
		|| Stamina + UE_SMALL_NUMBER < DashCost || GetWorld()->GetTimeSeconds() < NextDashAvailableAtSeconds)
	{
		return;
	}
	DashDirection = Direction.GetSafeNormal2D();
	if (DashDirection.IsNearlyZero())
	{
		DashDirection = GetActorForwardVector().GetSafeNormal2D();
	}
	if (HasAuthority())
	{
		bReplicatedDashing = true;
		ReplicatedDashDirection = DashDirection;
	}

	Stamina = FMath::Clamp(Stamina - DashCost, 0.0f, MaxStamina);
	bIsDashing = true;
	DashElapsedSeconds = 0.0f;
	DashDistanceApplied = 0.0f;
	bWasFallingBeforeDash = GetCharacterMovement()->IsFalling();
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();

	if (bIsChargingThrow)
	{
		bIsChargingThrow = false;
		OnThrowChargeChanged(0.0f);
		if (ChargeWidget)
		{
			ChargeWidget->SetCharging(false);
			ChargeWidget->SetChargeAlpha(0.0f);
		}
	}

	OnDashStarted(DashDirection);
}

void AChaosImpactCharacter::UpdateDash(const float DeltaSeconds)
{
	const float SafeDuration = FMath::Max(DashDuration, UE_SMALL_NUMBER);
	DashElapsedSeconds = FMath::Min(DashElapsedSeconds + DeltaSeconds, SafeDuration);
	const float DashAlpha = DashElapsedSeconds / SafeDuration;
	const float EasedAlpha = 1.0f - FMath::Pow(1.0f - DashAlpha, 3.0f);
	const float TargetDistance = DashDistance * EasedAlpha;
	const float StepDistance = FMath::Max(0.0f, TargetDistance - DashDistanceApplied);

	FHitResult DashHit;
	// A trusted remote client moves itself; the server copy only keeps the dash state and timing.
	if (!IsRemotePlayerOnServer() || !GetCharacterMovement()->bServerAcceptClientAuthoritativePosition)
	{
		AddActorWorldOffset(DashDirection * StepDistance, true, &DashHit);
	}
	DashDistanceApplied = TargetDistance;

	UpdateDashTrailPresentation(true);

	if (DashHit.bBlockingHit || DashElapsedSeconds >= SafeDuration)
	{
		FinishDash();
	}
}

void AChaosImpactCharacter::FinishDash()
{
	if (!bIsDashing)
	{
		return;
	}

	bIsDashing = false;
	if (HasAuthority())
	{
		bReplicatedDashing = false;
	}
	UpdateDashTrailPresentation(false);
	DashElapsedSeconds = 0.0f;
	DashDistanceApplied = 0.0f;
	if (GetWorld())
	{
		NextDashAvailableAtSeconds = GetWorld()->GetTimeSeconds() + DashCooldownSeconds;
	}
	GetCharacterMovement()->SetMovementMode(bWasFallingBeforeDash ? MOVE_Falling : MOVE_Walking);
}

void AChaosImpactCharacter::UpdateAimGuidePresentation(const bool bVisible)
{
	(void)bVisible;
	// Each local player's UMG layer draws only their own guide. World primitives
	// would leak into opponent and spectator cameras, so they remain disabled.
	for (UStaticMeshComponent* Piece : AimGuidePieces)
	{
		if (Piece)
		{
			Piece->SetHiddenInGame(true);
		}
	}
}

void AChaosImpactCharacter::UpdateDashTrailPresentation(const bool bVisible)
{
	for (UStaticMeshComponent* Piece : DashTrailPieces)
	{
		if (Piece)
		{
			Piece->SetHiddenInGame(!bVisible);
		}
	}
	if (!bVisible || DashTrailPieces.IsEmpty())
	{
		return;
	}

	const FVector Direction = DashDirection.GetSafeNormal2D();
	const FVector Side = FVector::CrossProduct(FVector::UpVector, Direction).GetSafeNormal();
	const FVector Center = GetPresentationLocation() + FVector::UpVector * 48.0f;
	for (int32 PieceIndex = 0; PieceIndex < DashTrailPieces.Num(); ++PieceIndex)
	{
		const int32 LineIndex = PieceIndex - DashTrailPieces.Num() / 2;
		const float Length = 135.0f + FMath::Abs(LineIndex) * 22.0f;
		const FVector Offset = Side * (LineIndex * 22.0f) + FVector::UpVector * (LineIndex * 5.0f);
		UStaticMeshComponent* Piece = DashTrailPieces[PieceIndex];
		Piece->SetWorldLocation(Center + Offset - Direction * (40.0f + Length * 0.5f));
		Piece->SetWorldRotation(Direction.Rotation());
		Piece->SetWorldScale3D(FVector(Length / 100.0f, 0.035f, 0.025f));
	}
}

void AChaosImpactCharacter::ReleaseChargedThrow()
{
	if (!bIsChargingThrow || bEliminated)
	{
		return;
	}

	const float ChargeAlpha = GetThrowChargeAlpha();
	bIsChargingThrow = false;
	OnThrowChargeChanged(0.0f);
	if (ChargeWidget)
	{
		ChargeWidget->SetCharging(false);
		ChargeWidget->SetChargeAlpha(0.0f);
	}
	if (HasAuthority())
	{
		SpawnBall(ChargeAlpha);
	}
	else if (IsEliminationPredicted())
	{
		// This screen already reported the lethal hit; the server would refuse the throw.
	}
	else if (GetPendingPickupCount() > 0 && CarriedBallCount - GetPendingPickupCount() <= 0)
	{
		// The only ball in hand is a pickup the server has not confirmed yet: throw once it is.
		bThrowAwaitingPickup = true;
		AwaitingThrowChargeAlpha = ChargeAlpha;
	}
	else
	{
		ReleaseThrowNow(ChargeAlpha);
	}
}

void AChaosImpactCharacter::ReleaseThrowNow(const float ChargeAlpha)
{
	ServerReleaseThrow(ChargeAlpha, AimDirection, GetActorLocation());
	PendingBallActions.Add(-1);
	// Throw on this screen immediately with a cosmetic ball. The server trims its release delay
	// by this player's ping, and its ball takes over from the cosmetic one when it arrives.
	SpawnBall(ChargeAlpha);
	RefreshPredictedBallCount();
}

void AChaosImpactCharacter::UpdateAim(float DeltaSeconds)
{
	// Other players' characters take their rotation from replicated movement.
	if (GetLocalRole() == ROLE_SimulatedProxy)
	{
		return;
	}
	FVector DesiredDirection = AimDirection;
	if (!IsLocallyControlled())
	{
		// Server copy of a remote player: aim arrives through ServerUpdateAim.
	}
	else if (bDevAutoInput)
	{
		// Development auto-play sets AimDirection itself.
	}
	else if (StickAimInput.Size() >= StickAimDeadZone)
	{
		const FRotator CameraYaw(0.0f, CameraBoom->GetComponentRotation().Yaw, 0.0f);
		const FVector CameraForward = FRotationMatrix(CameraYaw).GetUnitAxis(EAxis::X);
		const FVector CameraRight = FRotationMatrix(CameraYaw).GetUnitAxis(EAxis::Y);
		const FVector RawStickDirection =
			(CameraForward * StickAimInput.Y + CameraRight * StickAimInput.X).GetSafeNormal2D();
		DesiredDirection = ApplyControllerAimAssist(RawStickDirection);
	}
	else
	{
		FVector MouseAimPoint;
		if (FindMouseAimPoint(MouseAimPoint))
		{
			DesiredDirection = (MouseAimPoint - GetActorLocation()).GetSafeNormal2D();
		}
	}

	if (!DesiredDirection.IsNearlyZero())
	{
		AimDirection = DesiredDirection;
		const FRotator TargetRotation = AimDirection.Rotation();
		const FRotator SmoothRotation = FMath::RInterpConstantTo(
			GetActorRotation(), TargetRotation, DeltaSeconds, 720.0f);
		SetActorRotation(FRotator(0.0f, SmoothRotation.Yaw, 0.0f));
	}

	if (!HasAuthority() && IsLocallyControlled() && GetWorld())
	{
		const double Now = FPlatformTime::Seconds();
		if (Now >= NextAimSendAt && FVector::DotProduct(LastSentAim, AimDirection) < 0.9995f)
		{
			ServerUpdateAim(AimDirection);
			LastSentAim = AimDirection;
			NextAimSendAt = Now + 1.0 / 30.0;
		}
	}
}

FVector AChaosImpactCharacter::ApplyControllerAimAssist(const FVector& RawDirection) const
{
	if (!GetWorld() || RawDirection.IsNearlyZero() || ControllerAimAssistStrength <= 0.0f)
	{
		return RawDirection;
	}

	const FVector Origin = GetActorLocation();
	const float MinimumDot = FMath::Cos(FMath::DegreesToRadians(ControllerAimAssistAngleDegrees));
	float BestScore = MinimumDot;
	FVector BestDirection = RawDirection;
	auto ConsiderActor = [&](const AActor* Candidate)
	{
		if (!Candidate || Candidate == this || Candidate->IsHidden())
		{
			return;
		}
		const FVector Offset = Candidate->GetActorLocation() - Origin;
		const float Distance = Offset.Size2D();
		if (Distance <= UE_SMALL_NUMBER || Distance > ControllerAimAssistDistance)
		{
			return;
		}
		const FVector CandidateDirection = Offset.GetSafeNormal2D();
		const float Dot = FVector::DotProduct(RawDirection, CandidateDirection);
		const float Score = Dot + (1.0f - Distance / ControllerAimAssistDistance) * 0.06f;
		if (Dot >= MinimumDot && Score > BestScore)
		{
			BestScore = Score;
			BestDirection = CandidateDirection;
		}
	};

	for (TActorIterator<AChaosImpactCharacter> It(GetWorld()); It; ++It)
	{
		if (!It->IsEliminated())
		{
			ConsiderActor(*It);
		}
	}
	for (TActorIterator<AChaosImpactTrainingTarget> It(GetWorld()); It; ++It)
	{
		if (!It->IsDefeated())
		{
			ConsiderActor(*It);
		}
	}

	return FMath::Lerp(RawDirection, BestDirection,
		FMath::Clamp(ControllerAimAssistStrength, 0.0f, 1.0f)).GetSafeNormal2D();
}

bool AChaosImpactCharacter::FindMouseAimPoint(FVector& OutAimPoint) const
{
	const APlayerController* PlayerController = Cast<APlayerController>(GetController());
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return false;
	}
	if (const AChaosImpactPlayerController* MenuController =
		Cast<AChaosImpactPlayerController>(PlayerController);
		MenuController && MenuController->IsUsingGamepad())
	{
		return false;
	}

	FHitResult CursorHit;
	if (PlayerController->GetHitResultUnderCursor(ECC_Visibility, false, CursorHit))
	{
		OutAimPoint = CursorHit.ImpactPoint;
		return true;
	}

	FVector RayOrigin;
	FVector RayDirection;
	if (!PlayerController->DeprojectMousePositionToWorld(RayOrigin, RayDirection))
	{
		return false;
	}

	const float Denominator = RayDirection.Z;
	if (FMath::IsNearlyZero(Denominator))
	{
		return false;
	}

	const float Distance = (GetActorLocation().Z - RayOrigin.Z) / Denominator;
	if (Distance <= 0.0f)
	{
		return false;
	}

	OutAimPoint = RayOrigin + RayDirection * Distance;
	return true;
}

bool AChaosImpactCharacter::SpawnBall(const float ChargeAlpha)
{
	if (!GetWorld() || !BallClass || AimDirection.IsNearlyZero() || CarriedBallCount <= 0
		|| bThrowReleasePending)
	{
		return false;
	}

	const FVector SpawnLocation = GetActorLocation()
		+ AimDirection * ThrowSocketOffset.X
		+ GetActorRightVector() * ThrowSocketOffset.Y
		+ FVector::UpVector * ThrowSocketOffset.Z;
	const FTransform SpawnTransform(AimDirection.Rotation(), SpawnLocation);
	// An online client spawns a purely cosmetic local ball; only the server's ball has gameplay effect.
	const bool bCosmeticPrediction = !HasAuthority();
	AChaosImpactBall* Ball = GetWorld()->SpawnActorDeferred<AChaosImpactBall>(BallClass, SpawnTransform,
		this, this, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (Ball)
	{
		Ball->SetCosmeticPrediction(bCosmeticPrediction);
		// Slot 0 (the right hand) is thrown first.
		Ball->SetBallType(GetCarriedBallType(0));
		Ball->FinishSpawning(SpawnTransform);
	}
	if (IsValid(Ball))
	{
		const float ThrowSpeed = FMath::Lerp(MinimumThrowSpeed, MaximumThrowSpeed, ChargeAlpha);
		const AChaosImpactPlayerController* PlayerController =
			Cast<AChaosImpactPlayerController>(GetController());
		const AChaosImpactCPUController* CPUController =
			Cast<AChaosImpactCPUController>(GetController());
		const EChaosImpactBallFlightMode FlightMode = PlayerController
			? PlayerController->GetBallFlightMode()
			: CPUController && !CPUController->UsesArcFlightMode()
				? EChaosImpactBallFlightMode::Straight : EChaosImpactBallFlightMode::Arc;
		float HorizontalThrowSpeed = ThrowSpeed;
		float ArcUpwardSpeed = 0.0f;
		if (FlightMode == EChaosImpactBallFlightMode::Arc)
		{
			// Top-down throw: leave the hand level and let gravity create only the downward arc.
			HorizontalThrowSpeed = ThrowSpeed * ArcThrowSpeedScale;
			ArcUpwardSpeed = 0.0f;
		}
		PendingThrowDirection = AimDirection.GetSafeNormal2D();
		PendingThrowSpeed = HorizontalThrowSpeed;
		PendingThrowFlightMode = FlightMode;
		PendingThrowArcUpwardSpeed = ArcUpwardSpeed;
		PendingThrowBall = Ball;
		bThrowReleasePending = true;
		Ball->PrepareForAnimatedThrow(GetMesh(), TEXT("hand_r"),
			HeldBallRelativeLocation, HeldBallRelativeRotation);
		PopCarriedBall();
		UpdateBallPresentation();
		// The spawned projectile itself replaces the cosmetic hand ball until the cue.
		HeldBallMesh->SetVisibility(false, true);
		LeftHeldBallMesh->SetVisibility(false, true);
		if (bCosmeticPrediction)
		{
			PlayThrowAnimation();
			bPredictedThrowAnimation = true;
			PredictedThrowBall = Ball;
			PredictedThrowSpawnedAt = GetWorld()->GetTimeSeconds();
		}
		else if (GetNetMode() == NM_Standalone)
		{
			PlayThrowAnimation();
		}
		else
		{
			MulticastPlayThrowAnimation();
		}
		// A remote thrower started the motion one round trip ago on their own screen.
		float ReleaseDelay = ThrowReleaseDelaySeconds;
		if (IsRemotePlayerOnServer())
		{
			if (const APlayerState* ThrowerState = GetPlayerState())
			{
				ReleaseDelay = FMath::Max(0.0f,
					ReleaseDelay - ThrowerState->GetPingInMilliseconds() / 1000.0f);
			}
		}
		if (ReleaseDelay <= UE_SMALL_NUMBER)
		{
			CompleteAnimatedThrow();
		}
		else
		{
			GetWorldTimerManager().SetTimer(ThrowReleaseTimer, this,
				&AChaosImpactCharacter::CompleteAnimatedThrow, ReleaseDelay, false);
		}
		return true;
	}
	return false;
}

void AChaosImpactCharacter::CompleteAnimatedThrow()
{
	AChaosImpactBall* Ball = PendingThrowBall.Get();
	bThrowReleasePending = false;
	PendingThrowBall.Reset();
	if (IsValid(Ball))
	{
		if (HasAuthority() && !ThrowOriginOffset.IsNearlyZero())
		{
			// Start from where the thrower's own screen had them, not from this slightly older copy.
			Ball->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
			Ball->SetActorLocation(Ball->GetActorLocation() + ThrowOriginOffset, false, nullptr,
				ETeleportType::TeleportPhysics);
		}
		ThrowOriginOffset = FVector::ZeroVector;
		Ball->Launch(PendingThrowDirection, PendingThrowSpeed,
			PendingThrowFlightMode, PendingThrowArcUpwardSpeed);
	}
	UpdateBallPresentation();
}

void AChaosImpactCharacter::PlayThrowAnimation()
{
	if (!ThrowAnimation || !GetMesh())
	{
		return;
	}
	if (!LocomotionAnimInstanceClass)
	{
		LocomotionAnimInstanceClass = GetMesh()->GetAnimClass();
	}
	GetWorldTimerManager().ClearTimer(ThrowAnimationResetTimer);
	GetMesh()->PlayAnimation(ThrowAnimation, false);
	if (UAnimSingleNodeInstance* SingleNode = GetMesh()->GetSingleNodeInstance())
	{
		SingleNode->SetPlayRate(ThrowAnimationPlayRate);
	}
	bThrowAnimationActive = true;
	const float AnimationSeconds = FMath::Max(ThrowReleaseDelaySeconds + 0.12f,
		ThrowAnimation->GetPlayLength() / FMath::Max(ThrowAnimationPlayRate, 0.1f));
	GetWorldTimerManager().SetTimer(ThrowAnimationResetTimer, this,
		&AChaosImpactCharacter::RestoreLocomotionAnimation, AnimationSeconds, false);
}

void AChaosImpactCharacter::RestoreLocomotionAnimation()
{
	bThrowAnimationActive = false;
	if (GetMesh() && LocomotionAnimInstanceClass)
	{
		GetMesh()->SetAnimInstanceClass(LocomotionAnimInstanceClass);
	}
}

float AChaosImpactCharacter::TakeDamage(const float DamageAmount, const FDamageEvent& DamageEvent,
	AController* EventInstigator, AActor* DamageCauser)
{
	if (const AChaosImpactBall* Ball = Cast<AChaosImpactBall>(DamageCauser);
		Ball && Ball->WasThrownBy(this))
	{
		return 0.0f;
	}
	if (!HasAuthority() || bEliminated || DamageAmount <= 0.0f)
	{
		return 0.0f;
	}
	if (bIsDashing && !bApplyingReportedHit)
	{
		return 0.0f;
	}

	const float AppliedDamage = FMath::Min(Health, DamageAmount);
	Health = FMath::Clamp(Health - AppliedDamage, 0.0f, MaxHealth);
	OnPlayerHit(Health, AppliedDamage);

	if (Health <= 0.0f)
	{
		bEliminated = true;
		RespawnAtWorldSeconds = 0.0f;
		EliminationInstigator = EventInstigator;
		bIsChargingThrow = false;
		bMouseChargeActive = false;
		bWasMouseDownLastTick = false;
		bIsDashing = false;
		GetWorldTimerManager().ClearTimer(ThrowReleaseTimer);
		if (AChaosImpactBall* PendingBall = PendingThrowBall.Get())
		{
			PendingBall->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
			PendingBall->MakeRollingPickup(FVector::ZeroVector);
		}
		PendingThrowBall.Reset();
		bThrowReleasePending = false;
		ClearCarriedBalls();
		IceFrozenUntilServerTime = 0.0;
		if (IsRemotePlayerOnServer())
		{
			ClientBallCountReset(0, 0);
		}
		UpdateBallPresentation();
		RestoreLocomotionAnimation();
		UpdateAimGuidePresentation(false);
		UpdateDashTrailPresentation(false);
		if (ChargeWidget)
		{
			ChargeWidget->SetCharging(false);
			ChargeWidget->HideRespawn();
		}
		GetCharacterMovement()->DisableMovement();
		SetActorEnableCollision(false);
		StartEliminationEffect();
		OnPlayerEliminated();

		AChaosImpactCharacter* Eliminator = EventInstigator
			? Cast<AChaosImpactCharacter>(EventInstigator->GetPawn()) : nullptr;
		if (!Eliminator)
		{
			if (const AChaosImpactBall* EliminatingBall = Cast<AChaosImpactBall>(DamageCauser))
			{
				Eliminator = Cast<AChaosImpactCharacter>(EliminatingBall->GetThrowingPawn());
			}
		}
		if (Eliminator && Eliminator != this)
		{
			Eliminator->NotifyOpponentEliminated(Eliminator->GetEliminatorDisplayName(GetController()));
			if (AChaosImpactGameMode* GameMode = GetWorld()->GetAuthGameMode<AChaosImpactGameMode>())
			{
				GameMode->RegisterKnockout(Eliminator->GetController());
			}
		}

		GetWorldTimerManager().ClearTimer(EliminationCameraHoldTimer);
		GetWorldTimerManager().ClearTimer(RespawnTimer);
		if (EliminationCameraHoldSeconds <= UE_SMALL_NUMBER)
		{
			BeginRespawnCountdown();
		}
		else
		{
			GetWorldTimerManager().SetTimer(EliminationCameraHoldTimer, this,
				&AChaosImpactCharacter::BeginRespawnCountdown,
				EliminationCameraHoldSeconds, false);
		}
	}

	return AppliedDamage;
}

void AChaosImpactCharacter::BeginRespawnCountdown()
{
	if (!bEliminated || !GetWorld())
	{
		return;
	}
	RespawnAtWorldSeconds = GetWorld()->GetTimeSeconds() + EliminationResetDelay;
	const FString DefeatedBy = GetEliminatorDisplayName(EliminationInstigator.Get());
	APawn* KillerPawn = EliminationInstigator.IsValid() ? EliminationInstigator->GetPawn() : nullptr;
	if (IsLocallyControlled())
	{
		ShowRespawnLocally(DefeatedBy, EliminationResetDelay, KillerPawn);
	}
	else if (IsPlayerControlled())
	{
		ClientShowRespawn(DefeatedBy, EliminationResetDelay, KillerPawn);
	}
	GetWorldTimerManager().SetTimer(RespawnTimer, this,
		&AChaosImpactCharacter::ResetAfterElimination, EliminationResetDelay, false);
}

void AChaosImpactCharacter::ResetAfterElimination()
{
	GetWorldTimerManager().ClearTimer(EliminationCameraHoldTimer);
	GetWorldTimerManager().ClearTimer(RespawnTimer);
	RespawnAtWorldSeconds = 0.0f;
	StopEliminationEffect();
	if (IsLocallyControlled())
	{
		EndEliminationSpectate();
	}
	else if (IsPlayerControlled())
	{
		ClientRespawned();
	}
	SetActorLocationAndRotation(InitialSpawnLocation, InitialSpawnRotation, false, nullptr,
		ETeleportType::TeleportPhysics);
	NotifyServerTeleport();
	Health = MaxHealth;
	Stamina = MaxStamina;
	ClearCarriedBalls();
	IceFrozenUntilServerTime = 0.0;
	if (IsRemotePlayerOnServer())
	{
		ClientBallCountReset(0, 0);
	}
	bIsDashing = false;
	DashElapsedSeconds = 0.0f;
	DashDistanceApplied = 0.0f;
	NextDashAvailableAtSeconds = 0.0f;
	bEliminated = false;
	EliminationInstigator.Reset();
	SetActorEnableCollision(true);
	GetCharacterMovement()->SetMovementMode(bTrainingMenuFrozen ? MOVE_None : MOVE_Walking);
	UpdateBallPresentation();
	if (ChargeWidget)
	{
		ChargeWidget->HideRespawn();
	}
	StartRespawnEffect();
}

void AChaosImpactCharacter::StartEliminationEffect()
{
	bRespawnEffectActive = false;
	GetMesh()->SetRelativeScale3D(InitialMeshRelativeScale);
	bEliminationEffectActive = true;
	EliminationEffectTime = 0.0f;
	GetMesh()->SetVisibility(false, true);
	HeldBallMesh->SetVisibility(false, true);
	LeftHeldBallMesh->SetVisibility(false, true);
	for (UStaticMeshComponent* Piece : EliminationPieces)
	{
		Piece->SetHiddenInGame(false);
	}
	if (EliminationFlash)
	{
		EliminationFlash->SetIntensity(26000.0f);
	}
	if (UNiagaraSystem* Burst = LoadObject<UNiagaraSystem>(
		nullptr, TEXT("/Game/Variant_Combat/VFX/NS_Damage.NS_Damage")))
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, Burst,
			GetActorLocation() + FVector::UpVector * 70.0f, GetActorRotation(), FVector(2.7f));
	}
}

void AChaosImpactCharacter::UpdateEliminationEffect(const float DeltaSeconds)
{
	EliminationEffectTime += DeltaSeconds;
	const float Alpha = FMath::Clamp(EliminationEffectTime /
		FMath::Max(EliminationEffectDuration, UE_SMALL_NUMBER), 0.0f, 1.0f);
	for (int32 PieceIndex = 0; PieceIndex < EliminationPieces.Num(); ++PieceIndex)
	{
		UStaticMeshComponent* Piece = EliminationPieces[PieceIndex];
		const FVector Direction = EliminationPieceDirections.IsValidIndex(PieceIndex)
			? EliminationPieceDirections[PieceIndex] : FVector::UpVector;
		const float Distance = 230.0f + (PieceIndex % 7) * 34.0f;
		FVector Position = FVector(0.0f, 0.0f, 70.0f) + Direction * Distance * Alpha;
		Position.Z -= 180.0f * Alpha * Alpha;
		Piece->SetRelativeLocation(Position);
		Piece->SetRelativeRotation(Direction.Rotation()
			+ FRotator(Alpha * 780.0f, Alpha * 620.0f, Alpha * 410.0f));
		const float Size = (0.035f + 0.009f * (PieceIndex % 4))
			* FMath::Square(1.0f - Alpha);
		Piece->SetRelativeScale3D(FVector(Size));
		Piece->SetHiddenInGame(Alpha >= 0.98f);
	}
	if (EliminationFlash)
	{
		EliminationFlash->SetIntensity(FMath::Lerp(26000.0f, 0.0f,
			FMath::Clamp(Alpha * 2.8f, 0.0f, 1.0f)));
	}
	if (Alpha >= 1.0f)
	{
		bEliminationEffectActive = false;
	}
}

void AChaosImpactCharacter::StopEliminationEffect()
{
	bEliminationEffectActive = false;
	EliminationEffectTime = 0.0f;
	for (UStaticMeshComponent* Piece : EliminationPieces)
	{
		Piece->SetHiddenInGame(true);
	}
	if (EliminationFlash)
	{
		EliminationFlash->SetIntensity(0.0f);
	}
	// Do not recursively reveal the two hand-ball components. Their visibility is
	// inventory-driven and must remain hidden after a zero-ball respawn.
	GetMesh()->SetVisibility(true, false);
	UpdateBallPresentation();
}

void AChaosImpactCharacter::StartRespawnEffect()
{
	bRespawnEffectActive = true;
	RespawnEffectTime = 0.0f;
	GetMesh()->SetVisibility(true, false);
	GetMesh()->SetRelativeScale3D(InitialMeshRelativeScale * 0.08f);
	UpdateBallPresentation();
	if (EliminationFlash)
	{
		EliminationFlash->SetLightColor(FLinearColor(0.0f, 0.8f, 1.0f));
		EliminationFlash->SetIntensity(22000.0f);
	}
	if (UNiagaraSystem* Burst = LoadObject<UNiagaraSystem>(
		nullptr, TEXT("/Game/Variant_Combat/VFX/NS_Damage.NS_Damage")))
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, Burst,
			GetActorLocation() + FVector::UpVector * 70.0f, GetActorRotation(), FVector(1.8f));
	}
}

void AChaosImpactCharacter::UpdateRespawnEffect(const float DeltaSeconds)
{
	RespawnEffectTime += DeltaSeconds;
	const float Alpha = FMath::Clamp(RespawnEffectTime /
		FMath::Max(RespawnEffectDuration, UE_SMALL_NUMBER), 0.0f, 1.0f);
	const float Ease = 1.0f - FMath::Pow(1.0f - Alpha, 3.0f);
	const float Overshoot = FMath::Sin(Alpha * UE_PI) * 0.2f;
	GetMesh()->SetRelativeScale3D(InitialMeshRelativeScale * (0.08f + Ease * 0.92f + Overshoot));
	if (EliminationFlash)
	{
		EliminationFlash->SetIntensity(FMath::Lerp(22000.0f, 0.0f, Alpha));
	}
	if (Alpha >= 1.0f)
	{
		bRespawnEffectActive = false;
		GetMesh()->SetRelativeScale3D(InitialMeshRelativeScale);
		if (EliminationFlash)
		{
			EliminationFlash->SetIntensity(0.0f);
		}
	}
}

FString AChaosImpactCharacter::GetEliminatorDisplayName(AController* EventInstigator) const
{
	if (!EventInstigator)
	{
		return TEXT("相手");
	}
	if (GetNetMode() != NM_Standalone && EventInstigator->GetPlayerState<APlayerState>()
		&& Cast<APlayerController>(EventInstigator))
	{
		return EventInstigator->GetPlayerState<APlayerState>()->GetPlayerName();
	}
	if (const UGameInstance* GameInstance = GetGameInstance())
	{
		const TArray<ULocalPlayer*>& LocalPlayers = GameInstance->GetLocalPlayers();
		for (int32 PlayerIndex = 0; PlayerIndex < LocalPlayers.Num(); ++PlayerIndex)
		{
			if (LocalPlayers[PlayerIndex]
				&& LocalPlayers[PlayerIndex]->GetPlayerController(GetWorld()) == EventInstigator)
			{
				return FString::Printf(TEXT("PLAYER %d"), PlayerIndex + 1);
			}
		}
	}
	if (Cast<AChaosImpactCPUController>(EventInstigator))
	{
		return TEXT("CPU PLAYER");
	}
	return TEXT("相手プレイヤー");
}

void AChaosImpactCharacter::BeginEliminationSpectate(APawn* KillerPawn)
{
	APlayerController* VictimController = Cast<APlayerController>(GetController());
	if (!VictimController || !VictimController->IsLocalController()
		|| !KillerPawn || KillerPawn == this)
	{
		return;
	}
	EliminationViewTarget = KillerPawn;
	VictimController->bAutoManageActiveCameraTarget = false;
	VictimController->SetViewTargetWithBlend(KillerPawn, 0.28f,
		EViewTargetBlendFunction::VTBlend_Cubic);
}

void AChaosImpactCharacter::EndEliminationSpectate()
{
	if (APlayerController* VictimController = Cast<APlayerController>(GetController());
		VictimController && VictimController->IsLocalController())
	{
		VictimController->SetViewTargetWithBlend(this, 0.24f,
			EViewTargetBlendFunction::VTBlend_Cubic);
		VictimController->bAutoManageActiveCameraTarget = true;
	}
	EliminationViewTarget.Reset();
}

bool AChaosImpactCharacter::IsPersonalAimGuideVisible() const
{
	return ChargeWidget && ChargeWidget->IsAimGuideVisible();
}

FVector AChaosImpactCharacter::GetAimGuideStartWorldLocation() const
{
	// Keep the HUD arrow anchored to the collision body rather than hand_r.
	// Skeletal locomotion moves the hand and held ball every frame, which made
	// the projected arrow shake while walking even though the actual aim was stable.
	return GetActorLocation() + FVector::UpVector * 54.0f
		+ AimDirection.GetSafeNormal2D() * 46.0f;
}

void AChaosImpactCharacter::NotifyOpponentEliminated(const FString& VictimName)
{
	if (!IsLocallyControlled())
	{
		if (IsPlayerControlled())
		{
			ClientShowKnockout(VictimName);
		}
		return;
	}
	TryCreateChargeWidget();
	if (ChargeWidget)
	{
		ChargeWidget->ShowKnockout(VictimName);
	}
}

void AChaosImpactCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	// The owner predicts its own count and reconciles through ordered answers (PendingBallActions);
	// a replicated value would overwrite predictions the server has not processed yet.
	DOREPLIFETIME_CONDITION(AChaosImpactCharacter, CarriedBallCount, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(AChaosImpactCharacter, CarriedBallTypes, COND_SkipOwner);
	DOREPLIFETIME(AChaosImpactCharacter, IceFrozenUntilServerTime);
	DOREPLIFETIME(AChaosImpactCharacter, Health);
	DOREPLIFETIME_CONDITION(AChaosImpactCharacter, bReplicatedDashing, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(AChaosImpactCharacter, ReplicatedDashDirection, COND_SkipOwner);
	DOREPLIFETIME(AChaosImpactCharacter, bEliminated);
}

void AChaosImpactCharacter::ServerStartCharge_Implementation()
{
	if (!bEliminated && !bIsDashing && CarriedBallCount > 0 && GetWorld())
	{
		bIsChargingThrow = true;
		ThrowChargeStartedAt = GetWorld()->GetTimeSeconds();
	}
}

void AChaosImpactCharacter::ServerReleaseThrow_Implementation(const float ChargeAlpha,
	FVector_NetQuantizeNormal Aim, FVector_NetQuantize ClientLocation)
{
	bIsChargingThrow = false;
	const bool bRemote = IsRemotePlayerOnServer();
	bool bThrown = false;
	// A remote owner's dash may still be finishing here only because of latency.
	if (!bEliminated && !(bIsDashing && !bRemote) && !bTrainingMenuFrozen)
	{
		const FVector Direction = FVector(Aim).GetSafeNormal2D();
		if (!Direction.IsNearlyZero())
		{
			AimDirection = Direction;
		}
		if (bRemote && bThrowReleasePending)
		{
			// The previous release is still waiting here only because of latency; let it go now.
			GetWorldTimerManager().ClearTimer(ThrowReleaseTimer);
			CompleteAnimatedThrow();
		}
		ThrowOriginOffset = bRemote
			? (FVector(ClientLocation) - GetActorLocation()).GetClampedToMaxSize(250.0f) : FVector::ZeroVector;
		bThrown = SpawnBall(FMath::Clamp(ChargeAlpha, 0.0f, 1.0f));
	}
	if (bRemote)
	{
		if (!bThrown)
		{
			UE_LOG(LogChaosImpact, Log, TEXT("Throw rejected for %s (balls=%d)"), *GetName(), CarriedBallCount);
		}
		ClientThrowResolved(bThrown, CarriedBallCount, CarriedBallTypes);
	}
}

void AChaosImpactCharacter::ServerStartDash_Implementation(FVector_NetQuantizeNormal Direction)
{
	bIsChargingThrow = false;
	if (IsRemotePlayerOnServer() && GetWorld())
	{
		// The owner already dashed; a previous dash or cooldown may still be running here only because of latency.
		if (bIsDashing)
		{
			FinishDash();
		}
		NextDashAvailableAtSeconds = FMath::Min(NextDashAvailableAtSeconds, GetWorld()->GetTimeSeconds());
		// Stamina belongs to the owner's machine, which already paid for this dash.
		Stamina = FMath::Max(Stamina, DashCost);
	}
	PerformDash(Direction);
}

void AChaosImpactCharacter::ServerUpdateAim_Implementation(FVector_NetQuantizeNormal Direction)
{
	const FVector Horizontal = FVector(Direction).GetSafeNormal2D();
	if (!Horizontal.IsNearlyZero())
	{
		AimDirection = Horizontal;
	}
}

void AChaosImpactCharacter::MulticastPlayThrowAnimation_Implementation()
{
	if (bPredictedThrowAnimation && IsLocallyControlled() && !HasAuthority())
	{
		bPredictedThrowAnimation = false;
		return;
	}
	PlayThrowAnimation();
}

bool AChaosImpactCharacter::IsRemotePlayerOnServer() const
{
	return HasAuthority() && GetNetMode() != NM_Standalone && IsPlayerControlled() && !IsLocallyControlled();
}

void AChaosImpactCharacter::ReportBallHitFromClient(AChaosImpactBall* Ball, const FVector& HitLocation)
{
	if (!HasAuthority() && IsLocallyControlled() && !bEliminated && IsValid(Ball) && GetWorld())
	{
		ServerReportBallHit(Ball, HitLocation);
		// Remember the expected health until the server's value has had a round trip to catch up,
		// so a lethal hit stops throws and pickups right away instead of being refused later.
		const double Now = GetWorld()->GetTimeSeconds();
		const float BaseHealth = Now < PredictedHealthUntil ? FMath::Min(PredictedHealth, Health) : Health;
		PredictedHealth = BaseHealth - Ball->GetDamage();
		PredictedHealthUntil = Now + GetNetworkRoundTripSeconds() + 0.35;
		if (IsEliminationPredicted())
		{
			UE_LOG(LogChaosImpact, Log, TEXT("Predicted own elimination from a reported hit"));
			bThrowAwaitingPickup = false;
			if (bIsChargingThrow)
			{
				CancelChargingThrow();
			}
		}
	}
}

void AChaosImpactCharacter::ServerReportBallHit_Implementation(AChaosImpactBall* Ball,
	FVector_NetQuantize HitLocation)
{
	if (!IsValid(Ball) || bEliminated)
	{
		UE_LOG(LogChaosImpact, Log, TEXT("Reported ball hit rejected: victim=%s reason=%s"), *GetName(),
			bEliminated ? TEXT("eliminated") : TEXT("ball gone"));
		return;
	}
	TGuardValue<bool> ReportGuard(bApplyingReportedHit, true);
	Ball->AcceptReportedHit(this, HitLocation);
}

void AChaosImpactCharacter::ClientTeleportTo_Implementation(FVector_NetQuantize Location, FRotator Rotation)
{
	SetActorLocationAndRotation(Location, Rotation, false, nullptr, ETeleportType::TeleportPhysics);
	GetCharacterMovement()->StopMovementImmediately();
}

void AChaosImpactCharacter::NotifyServerTeleport()
{
	if (!IsRemotePlayerOnServer() || !GetWorld())
	{
		return;
	}
	// Moves already in flight still carry the old position; ignore them briefly so the teleport sticks.
	ServerAuthoritativeUntil = GetWorld()->GetTimeSeconds() + ServerTeleportAuthoritySeconds;
	UpdateMovementAuthority();
	ClientTeleportTo(GetActorLocation(), GetActorRotation());
}

void AChaosImpactCharacter::UpdateMovementAuthority()
{
	if (!IsRemotePlayerOnServer() || !GetWorld())
	{
		return;
	}
	const bool bTrustClient = !bEliminated && GetWorld()->GetTimeSeconds() >= ServerAuthoritativeUntil;
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->bIgnoreClientMovementErrorChecksAndCorrection = bTrustClient;
	Movement->bServerAcceptClientAuthoritativePosition = bTrustClient;
}

AChaosImpactBall* AChaosImpactCharacter::TakePredictedThrowBall()
{
	AChaosImpactBall* Predicted = PredictedThrowBall.Get();
	PredictedThrowBall.Reset();
	if (!IsValid(Predicted))
	{
		return nullptr;
	}
	// Only a recent prediction can belong to the server ball that has just started flying.
	if (!GetWorld() || GetWorld()->GetTimeSeconds() - PredictedThrowSpawnedAt > 1.5)
	{
		Predicted->Destroy();
		return nullptr;
	}
	return Predicted;
}

void AChaosImpactCharacter::ClaimPickupFromClient(AChaosImpactBall* Ball)
{
	if (HasAuthority() || !IsLocallyControlled() || bEliminated || !IsValid(Ball)
		|| CarriedBallCount >= MaximumCarriedBalls || IsEliminationPredicted())
	{
		return;
	}
	// Predicted: the ball is in hand the moment it is touched on this screen.
	PendingBallActions.Add(static_cast<int8>(1 + static_cast<int32>(Ball->GetBallType())));
	RefreshPredictedBallCount();
	ServerClaimPickup(Ball);
}

void AChaosImpactCharacter::ServerClaimPickup_Implementation(AChaosImpactBall* Ball)
{
	// Generous reach: this copy of the player can trail the owner's own screen by a round trip.
	constexpr float MaxClaimDistance = 320.0f;
	const TCHAR* RejectReason = !IsValid(Ball) ? TEXT("ball gone")
		: bEliminated ? TEXT("eliminated")
		: CarriedBallCount >= MaximumCarriedBalls ? TEXT("hands full")
		: !Ball->IsPickupAvailable() ? TEXT("not available yet")
		: FVector::Dist2D(Ball->GetActorLocation(), GetActorLocation()) > MaxClaimDistance ? TEXT("too far")
		: nullptr;
	const bool bAccepted = !RejectReason && Ball->ConsumePickup(this);
	UE_LOG(LogChaosImpact, Log, TEXT("Pickup claim %s for %s (balls=%d%s%s)"),
		bAccepted ? TEXT("accepted") : TEXT("rejected"), *GetName(), CarriedBallCount,
		RejectReason ? TEXT(", ") : TEXT(""), RejectReason ? RejectReason : TEXT(""));
	ClientPickupResolved(bAccepted, CarriedBallCount, CarriedBallTypes, bAccepted ? nullptr : Ball);
}

void AChaosImpactCharacter::ClientPickupResolved_Implementation(const bool bAccepted,
	const int32 ServerBallCount, const uint8 ServerBallTypes, AChaosImpactBall* Ball)
{
	ResolveOldestBallAction(ServerBallCount, ServerBallTypes);
	if (!bAccepted && IsValid(Ball))
	{
		Ball->CancelLocalPickupClaim();
	}
	if (bThrowAwaitingPickup && GetPendingPickupCount() == 0)
	{
		bThrowAwaitingPickup = false;
		if (CarriedBallCount > 0 && !bEliminated && !IsEliminationPredicted() && !IsIceFrozen())
		{
			UE_LOG(LogChaosImpact, Log, TEXT("Throw released after pickup confirmation"));
			ReleaseThrowNow(AwaitingThrowChargeAlpha);
		}
		else
		{
			UE_LOG(LogChaosImpact, Log, TEXT("Throw cancelled: the ball was taken by someone else first"));
		}
	}
}

void AChaosImpactCharacter::ClientThrowResolved_Implementation(const bool bAccepted, const int32 ServerBallCount,
	const uint8 ServerBallTypes)
{
	if (!bAccepted)
	{
		GetWorldTimerManager().ClearTimer(ThrowReleaseTimer);
		bThrowReleasePending = false;
		if (AChaosImpactBall* Cosmetic = PendingThrowBall.Get())
		{
			Cosmetic->Destroy();
		}
		PendingThrowBall.Reset();
		if (AChaosImpactBall* Predicted = PredictedThrowBall.Get())
		{
			Predicted->Destroy();
		}
		PredictedThrowBall.Reset();
	}
	ResolveOldestBallAction(ServerBallCount, ServerBallTypes);
}

void AChaosImpactCharacter::ClientBallCountReset_Implementation(const int32 ServerBallCount,
	const uint8 ServerBallTypes)
{
	// Arrives in the same ordered stream as the answers, so pending changes still apply on top.
	ServerAnsweredBallCount = ServerBallCount;
	ServerAnsweredBallTypes = ServerBallTypes;
	RefreshPredictedBallCount();
}

int32 AChaosImpactCharacter::GetPendingPickupCount() const
{
	int32 Pickups = 0;
	for (const int8 Action : PendingBallActions)
	{
		Pickups += Action > 0 ? 1 : 0;
	}
	return Pickups;
}

void AChaosImpactCharacter::RefreshPredictedBallCount()
{
	// Replay the waiting pickups (append their type) and throws (take slot 0) over the last answer.
	TArray<EChaosImpactBallType, TInlineAllocator<4>> Types;
	const int32 Answered = FMath::Clamp(ServerAnsweredBallCount, 0, MaximumCarriedBalls);
	for (int32 Slot = 0; Slot < Answered; ++Slot)
	{
		Types.Add(ChaosImpactBallTypes::GetPackedSlot(ServerAnsweredBallTypes, Slot));
	}
	for (const int8 Action : PendingBallActions)
	{
		if (Action > 0)
		{
			if (Types.Num() < MaximumCarriedBalls)
			{
				Types.Add(ChaosImpactBallTypes::FromIndex(Action - 1));
			}
		}
		else if (Action < 0 && !Types.IsEmpty())
		{
			Types.RemoveAt(0);
		}
	}
	const uint8 PackedTypes = ChaosImpactBallTypes::Pack(Types);
	if (Types.Num() != CarriedBallCount || PackedTypes != CarriedBallTypes)
	{
		CarriedBallCount = Types.Num();
		CarriedBallTypes = PackedTypes;
		UpdateBallPresentation();
	}
}

void AChaosImpactCharacter::ResolveOldestBallAction(const int32 ServerBallCount, const uint8 ServerBallTypes)
{
	if (!PendingBallActions.IsEmpty())
	{
		PendingBallActions.RemoveAt(0);
	}
	ServerAnsweredBallCount = ServerBallCount;
	ServerAnsweredBallTypes = ServerBallTypes;
	RefreshPredictedBallCount();
}

void AChaosImpactCharacter::ClientAddStamina_Implementation(const float Amount)
{
	Stamina = FMath::Clamp(Stamina + Amount, 0.0f, MaxStamina);
}

void AChaosImpactCharacter::TickDevAutoInput()
{
	if (bEliminated || !GetWorld())
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	// Walk to the nearest free ball while there is room in hand, otherwise toward the nearest opponent.
	AActor* Goal = nullptr;
	float GoalDistance = TNumericLimits<float>::Max();
	if (CarriedBallCount < MaximumCarriedBalls)
	{
		for (TActorIterator<AChaosImpactBall> It(GetWorld()); It; ++It)
		{
			const float Distance = FVector::Dist2D(It->GetActorLocation(), GetActorLocation());
			if (It->IsPickup() && !It->IsHidden() && Distance < GoalDistance)
			{
				GoalDistance = Distance;
				Goal = *It;
			}
		}
	}
	AChaosImpactCharacter* Opponent = nullptr;
	float OpponentDistance = TNumericLimits<float>::Max();
	for (TActorIterator<AChaosImpactCharacter> It(GetWorld()); It; ++It)
	{
		const float Distance = FVector::Dist2D(It->GetActorLocation(), GetActorLocation());
		if (*It != this && !It->IsEliminated() && Distance < OpponentDistance)
		{
			OpponentDistance = Distance;
			Opponent = *It;
		}
	}
	if (!Goal && Opponent && OpponentDistance > 700.0f)
	{
		Goal = Opponent;
	}
	if (Goal)
	{
		const FVector Direction = (Goal->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
		AddMovementInput(Direction, 1.0f);
		LastMoveDirection = Direction;
	}
	if (Opponent)
	{
		SetAIAimDirection(Opponent->GetActorLocation() - GetActorLocation());
	}
	if (bIsChargingThrow && Now >= DevReleaseAt)
	{
		ReleaseChargedThrow();
	}
	else if (!bIsChargingThrow && Opponent && CarriedBallCount > 0 && Now >= DevNextThrowAt)
	{
		StartChargingThrow();
		DevReleaseAt = Now + 0.35;
		DevNextThrowAt = Now + 1.6;
	}
	if (Now >= DevNextDashAt && CanDashNow())
	{
		LastMoveDirection = FVector(FMath::FRandRange(-1.0f, 1.0f), FMath::FRandRange(-1.0f, 1.0f), 0.0f)
			.GetSafeNormal2D();
		StartDash();
		DevNextDashAt = Now + 2.5;
	}
}

float AChaosImpactCharacter::GetNetworkRoundTripSeconds() const
{
	const APlayerState* State = GetPlayerState();
	return State ? FMath::Clamp(State->GetPingInMilliseconds() / 1000.0f, 0.0f, 0.5f) : 0.0f;
}

bool AChaosImpactCharacter::IsEliminationPredicted() const
{
	// Expires by itself if the server refused the reported hit.
	return !HasAuthority() && !bEliminated && GetWorld()
		&& GetWorld()->GetTimeSeconds() < PredictedHealthUntil
		&& FMath::Min(PredictedHealth, Health) <= 0.0f;
}

FVector AChaosImpactCharacter::GetPresentationLocation() const
{
	const USkeletalMeshComponent* CharacterMesh = GetMesh();
	return CharacterMesh
		? CharacterMesh->GetComponentLocation() - GetActorQuat().RotateVector(CachedBaseTranslationOffset)
		: GetActorLocation();
}

void AChaosImpactCharacter::UpdatePresentationLead(const float DeltaSeconds)
{
	USkeletalMeshComponent* CharacterMesh = GetMesh();
	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!CharacterMesh || !Movement || CharacterMesh->IsSimulatingPhysics())
	{
		return;
	}
	FVector TargetLead = FVector::ZeroVector;
	const bool bDrawnFromNetwork = GetNetMode() != NM_Standalone && !IsLocallyControlled()
		&& (GetLocalRole() == ROLE_SimulatedProxy || IsRemotePlayerOnServer());
	if (bDrawnFromNetwork && !bEliminated)
	{
		// This copy trails the player's own screen by their round trip plus smoothing; draw it that far ahead.
		const float SmoothingLag = HasAuthority()
			? Movement->ListenServerNetworkSimulatedSmoothLocationTime
			: Movement->NetworkSimulatedSmoothLocationTime;
		const float LeadSeconds = FMath::Clamp(GetNetworkRoundTripSeconds() + SmoothingLag,
			0.0f, MaxPresentationLeadSeconds);
		FVector HorizontalVelocity = GetVelocity();
		HorizontalVelocity.Z = 0.0f;
		if (!IsDashingForPresentation())
		{
			HorizontalVelocity = HorizontalVelocity.GetClampedToMaxSize(Movement->MaxWalkSpeed * 1.1f);
		}
		TargetLead = (HorizontalVelocity * LeadSeconds).GetClampedToMaxSize(MaxPresentationLeadDistance);
	}
	PresentationLeadWorld = bEliminated ? FVector::ZeroVector
		: FMath::VInterpTo(PresentationLeadWorld, TargetLead, DeltaSeconds, PresentationLeadBlendSpeed);
	const FVector NewBase = CachedBaseTranslationOffset + GetActorQuat().UnrotateVector(PresentationLeadWorld);
	if (!NewBase.Equals(BaseTranslationOffset, 0.01f))
	{
		// Keep movement smoothing's current offset and only swap the lead underneath it.
		CharacterMesh->SetRelativeLocation(CharacterMesh->GetRelativeLocation() - BaseTranslationOffset + NewBase);
		BaseTranslationOffset = NewBase;
	}
}

void AChaosImpactCharacter::ClientShowRespawn_Implementation(const FString& DefeatedBy, const float Seconds,
	APawn* KillerPawn)
{
	ShowRespawnLocally(DefeatedBy, Seconds, KillerPawn);
}

void AChaosImpactCharacter::ShowRespawnLocally(const FString& DefeatedBy, const float Seconds, APawn* KillerPawn)
{
	if (GetWorld())
	{
		RespawnAtWorldSeconds = GetWorld()->GetTimeSeconds() + Seconds;
	}
	BeginEliminationSpectate(KillerPawn);
	TryCreateChargeWidget();
	if (ChargeWidget)
	{
		ChargeWidget->ShowRespawn(DefeatedBy, Seconds);
	}
}

void AChaosImpactCharacter::ClientRespawned_Implementation()
{
	RespawnAtWorldSeconds = 0.0f;
	Stamina = MaxStamina;
	EndEliminationSpectate();
	if (ChargeWidget)
	{
		ChargeWidget->HideRespawn();
	}
}

void AChaosImpactCharacter::ClientShowKnockout_Implementation(const FString& VictimName)
{
	TryCreateChargeWidget();
	if (ChargeWidget)
	{
		ChargeWidget->ShowKnockout(VictimName);
	}
}

void AChaosImpactCharacter::OnRep_CarriedBallCount()
{
	UpdateBallPresentation();
}

void AChaosImpactCharacter::OnRep_ReplicatedDashing()
{
	DashDirection = ReplicatedDashDirection;
	UpdateDashTrailPresentation(bReplicatedDashing);
}

void AChaosImpactCharacter::OnRep_Eliminated()
{
	ApplyEliminatedPresentation(bEliminated);
}

void AChaosImpactCharacter::ApplyEliminatedPresentation(const bool bNowEliminated)
{
	if (bNowEliminated)
	{
		bIsChargingThrow = false;
		bIsDashing = false;
		if (!HasAuthority())
		{
			// Drop any throw this screen predicted; the server resolves the real ball.
			GetWorldTimerManager().ClearTimer(ThrowReleaseTimer);
			bThrowReleasePending = false;
			if (AChaosImpactBall* Cosmetic = PendingThrowBall.Get())
			{
				Cosmetic->Destroy();
			}
			PendingThrowBall.Reset();
			if (AChaosImpactBall* Predicted = PredictedThrowBall.Get())
			{
				Predicted->Destroy();
			}
			PredictedThrowBall.Reset();
			bThrowAwaitingPickup = false;
		}
		UpdateDashTrailPresentation(false);
		if (ChargeWidget)
		{
			ChargeWidget->SetCharging(false);
		}
		SetActorEnableCollision(false);
		StartEliminationEffect();
		return;
	}
	StopEliminationEffect();
	SetActorEnableCollision(true);
	StartRespawnEffect();
}

void AChaosImpactCharacter::ResetForOnlineMatch(const FVector& Location, const FRotator& Rotation)
{
	if (!HasAuthority())
	{
		return;
	}
	if (bEliminated)
	{
		ResetAfterElimination();
	}
	if (bIsDashing)
	{
		FinishDash();
	}
	CancelChargingThrow();
	GetWorldTimerManager().ClearTimer(ThrowReleaseTimer);
	if (AChaosImpactBall* PendingBall = PendingThrowBall.Get())
	{
		PendingBall->Destroy();
	}
	PendingThrowBall.Reset();
	bThrowReleasePending = false;
	TeleportTo(Location, Rotation);
	NotifyServerTeleport();
	InitialSpawnLocation = GetActorLocation();
	InitialSpawnRotation = Rotation;
	Health = MaxHealth;
	Stamina = MaxStamina;
	ClearCarriedBalls();
	IceFrozenUntilServerTime = 0.0;
	NextDashAvailableAtSeconds = 0.0f;
	UpdateBallPresentation();
	if (IsRemotePlayerOnServer())
	{
		ClientAddStamina(MaxStamina);
		ClientBallCountReset(0, 0);
	}
}

void AChaosImpactCharacter::SetGameplayUIVisible(const bool bVisible)
{
	TryCreateChargeWidget();
	if (ChargeWidget)
	{
		ChargeWidget->SetVisibility(bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

void AChaosImpactCharacter::SetTrainingMenuFrozen(const bool bFrozen)
{
	if (bTrainingMenuFrozen == bFrozen)
	{
		return;
	}

	if (bFrozen)
	{
		// End transient character motion before preserving the movement state.
		// Ball actors are independent and deliberately remain simulated.
		if (bIsDashing)
		{
			FinishDash();
		}
		CancelChargingThrow();
		StopJumping();
		SavedTrainingMenuMovementMode = static_cast<uint8>(GetCharacterMovement()->MovementMode);
		SavedTrainingMenuCustomMovementMode = GetCharacterMovement()->CustomMovementMode;
		GetCharacterMovement()->StopMovementImmediately();
		GetCharacterMovement()->DisableMovement();
	}
	else
	{
		if (!bEliminated && !bIceFreezeActive)
		{
			EMovementMode RestoredMode = static_cast<EMovementMode>(SavedTrainingMenuMovementMode);
			if (RestoredMode == MOVE_None)
			{
				RestoredMode = MOVE_Walking;
			}
			GetCharacterMovement()->SetMovementMode(
				RestoredMode, SavedTrainingMenuCustomMovementMode);
		}
	}
	bTrainingMenuFrozen = bFrozen;
}

void AChaosImpactCharacter::SetTrainingMenuCameraActive(const bool bActive)
{
	if (!CameraBoom || bTrainingMenuCameraActive == bActive)
	{
		return;
	}
	if (bActive)
	{
		SavedCameraArmLength = CameraBoom->TargetArmLength;
		SavedCameraSocketOffset = CameraBoom->SocketOffset;
	}
	bTrainingMenuCameraActive = bActive;
}

void AChaosImpactCharacter::CancelChargingThrow()
{
	bIsChargingThrow = false;
	bMouseChargeActive = false;
	OnThrowChargeChanged(0.0f);
	if (ChargeWidget)
	{
		ChargeWidget->SetCharging(false);
		ChargeWidget->SetChargeAlpha(0.0f);
	}
}

void AChaosImpactCharacter::BeginThrowInput()
{
	StartChargingThrow();
}

void AChaosImpactCharacter::EndThrowInput()
{
	ReleaseChargedThrow();
	bMouseChargeActive = false;
}

void AChaosImpactCharacter::RecoverStaminaFromBallHit()
{
	if (bEliminated || StaminaRecoveredPerBallHit <= 0.0f)
	{
		return;
	}

	Stamina = FMath::Min(MaxStamina, Stamina + StaminaRecoveredPerBallHit);
	if (ChargeWidget)
	{
		ChargeWidget->SetStamina(Stamina, MaxStamina);
	}
	if (IsRemotePlayerOnServer())
	{
		ClientAddStamina(StaminaRecoveredPerBallHit);
	}
}

void AChaosImpactCharacter::SetTrainingStartTransform(
	const FVector& Location, const FRotator& Rotation)
{
	SetActorLocationAndRotation(Location, Rotation, false, nullptr,
		ETeleportType::TeleportPhysics);
	NotifyServerTeleport();
	InitialSpawnLocation = Location;
	InitialSpawnRotation = Rotation;
}

void AChaosImpactCharacter::SetAIAimDirection(const FVector& Direction)
{
	const FVector HorizontalDirection = Direction.GetSafeNormal2D();
	if (!HorizontalDirection.IsNearlyZero())
	{
		AimDirection = HorizontalDirection;
	}
}

bool AChaosImpactCharacter::CanDashNow() const
{
	return GetWorld() && !bEliminated && !bTrainingMenuFrozen && !bIsDashing && !IsIceFrozen()
		&& Stamina + UE_SMALL_NUMBER >= DashCost
		&& GetWorld()->GetTimeSeconds() >= NextDashAvailableAtSeconds;
}

void AChaosImpactCharacter::RequestAIDash(const FVector& Direction)
{
	const FVector HorizontalDirection = Direction.GetSafeNormal2D();
	if (!HorizontalDirection.IsNearlyZero())
	{
		LastMoveDirection = HorizontalDirection;
	}
	StartDash();
}

bool AChaosImpactCharacter::TryPickupBall(AChaosImpactBall* Ball)
{
	if (!HasAuthority() || !IsValid(Ball) || !Ball->IsPickupAvailable() || bEliminated
		|| CarriedBallCount >= MaximumCarriedBalls)
	{
		return false;
	}

	PushCarriedBall(Ball->GetBallType());
	UpdateBallPresentation();
	return true;
}

bool AChaosImpactCharacter::HasVisibleHeldBall() const
{
	return (HeldBallMesh && HeldBallMesh->IsVisible())
		|| (LeftHeldBallMesh && LeftHeldBallMesh->IsVisible());
}

void AChaosImpactCharacter::UpdateBallPresentation()
{
	if (HeldBallMesh)
	{
		HeldBallMesh->SetRelativeLocation(HeldBallRelativeLocation);
		HeldBallMesh->SetRelativeRotation(HeldBallRelativeRotation);
		HeldBallMesh->SetVisibility(CarriedBallCount > 0, true);
		ApplyHeldBallAppearance(HeldBallMesh, GetCarriedBallType(0));
	}
	if (LeftHeldBallMesh)
	{
		LeftHeldBallMesh->SetRelativeLocation(LeftHeldBallRelativeLocation);
		LeftHeldBallMesh->SetRelativeRotation(LeftHeldBallRelativeRotation);
		LeftHeldBallMesh->SetVisibility(CarriedBallCount > 1, true);
		ApplyHeldBallAppearance(LeftHeldBallMesh, GetCarriedBallType(1));
	}
	if (ChargeWidget)
	{
		ChargeWidget->SetBallInventory(CarriedBallCount, MaximumCarriedBalls, CarriedBallTypes);
	}
}

void AChaosImpactCharacter::PushCarriedBall(const EChaosImpactBallType Type)
{
	if (CarriedBallCount >= MaximumCarriedBalls)
	{
		return;
	}
	const int32 Shift = FMath::Max(CarriedBallCount, 0) * 2;
	CarriedBallTypes = static_cast<uint8>((CarriedBallTypes & ~(0x3 << Shift)) | (static_cast<int32>(Type) << Shift));
	++CarriedBallCount;
}

EChaosImpactBallType AChaosImpactCharacter::PopCarriedBall()
{
	const EChaosImpactBallType Type = GetCarriedBallType(0);
	CarriedBallTypes = static_cast<uint8>(CarriedBallTypes >> 2);
	CarriedBallCount = FMath::Max(0, CarriedBallCount - 1);
	return Type;
}

void AChaosImpactCharacter::ClearCarriedBalls()
{
	CarriedBallCount = 0;
	CarriedBallTypes = 0;
}

void AChaosImpactCharacter::ApplyHeldBallAppearance(UStaticMeshComponent* HandBall, const EChaosImpactBallType Type)
{
	if (!HandBall)
	{
		return;
	}
	using namespace ChaosImpactBallTypes;
	UMaterialInterface* Material = nullptr;
	if (Type == EChaosImpactBallType::Fire)
	{
		if (!HeldFireMaterial)
		{
			HeldFireMaterial = MakeEmissive(this, FLinearColor(1.0f, 0.24f, 0.01f), 1.1f);
		}
		Material = HeldFireMaterial;
	}
	else if (Type == EChaosImpactBallType::Ice)
	{
		if (!HeldIceMaterial)
		{
			HeldIceMaterial = MakeIceCrystal(this, 0.85f, 0.4f, FLinearColor(0.68f, 0.86f, 1.0f));
		}
		Material = HeldIceMaterial;
	}
	// No override for a normal ball: the mesh's own material.
	HandBall->SetMaterial(0, Material);

	// A carried fire ball keeps burning in the hand.
	TObjectPtr<UNiagaraComponent>& Flames = HandBall == LeftHeldBallMesh ? LeftHeldFire : RightHeldFire;
	const bool bWantFlames = Type == EChaosImpactBallType::Fire && HandBall->IsVisible()
		&& GetNetMode() != NM_DedicatedServer;
	if (bWantFlames && !Flames)
	{
		if (UNiagaraSystem* FireSystem = LoadEffect(Effects::Fire))
		{
			Flames = UNiagaraFunctionLibrary::SpawnSystemAttached(FireSystem, HandBall, NAME_None,
				FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset, false);
			if (Flames)
			{
				Flames->SetUsingAbsoluteScale(true);
				Flames->SetWorldScale3D(FVector::OneVector);
				SetEffectFloat(Flames, TEXT("Flame Scale"), 0.7f);
				SetEffectFloat(Flames, TEXT("Smoke Spawn Scale"), 0.1f);
				SetEffectFloat(Flames, TEXT("Base Light Intentsity"), 0.0f);
			}
		}
	}
	else if (Flames && bWantFlames != Flames->IsActive())
	{
		if (bWantFlames)
		{
			Flames->Activate(true);
		}
		else
		{
			Flames->Deactivate();
		}
	}
}

double AChaosImpactCharacter::GetSharedServerTime() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0;
	}
	const AGameStateBase* GameState = World->GetGameState();
	return GameState ? GameState->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
}

bool AChaosImpactCharacter::IsIceFrozen() const
{
	return IceFrozenUntilServerTime > 0.0 && GetSharedServerTime() < IceFrozenUntilServerTime;
}

void AChaosImpactCharacter::ApplyIceFreeze(const float Seconds)
{
	// A dodge in progress escapes the ice, like it escapes a hit.
	if (!HasAuthority() || bEliminated || Seconds <= 0.0f || (bIsDashing && !bApplyingReportedHit))
	{
		return;
	}
	IceFrozenUntilServerTime = FMath::Max(IceFrozenUntilServerTime, GetSharedServerTime() + Seconds);
	UE_LOG(LogChaosImpact, Log, TEXT("%s frozen for %.1fs"), *GetName(), Seconds);
	UpdateIceStatus(0.0f);
	ForceNetUpdate();
}

void AChaosImpactCharacter::UpdateIceStatus(const float DeltaSeconds)
{
	const bool bFrozen = !bEliminated && IsIceFrozen();
	if (bFrozen != bIceFreezeActive)
	{
		bIceFreezeActive = bFrozen;
		// Movement is changed only where this character is actually moved (its owner, the host, a CPU).
		if (IsLocallyControlled())
		{
			if (bFrozen)
			{
				if (bIsDashing)
				{
					FinishDash();
				}
				if (bIsChargingThrow)
				{
					CancelChargingThrow();
				}
				bThrowAwaitingPickup = false;
				GetCharacterMovement()->StopMovementImmediately();
				GetCharacterMovement()->DisableMovement();
			}
			else if (!bEliminated && !bTrainingMenuFrozen && !bIsDashing)
			{
				GetCharacterMovement()->SetMovementMode(MOVE_Walking);
			}
		}
		SetIceFreezePresentation(bFrozen);
	}
	UpdateIceFreezePresentation(DeltaSeconds);

	if (IsLocallyControlled())
	{
		const FVector Feet = GetActorLocation()
			- FVector::UpVector * GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		const bool bOnIce = !bEliminated && AChaosImpactHazardZone::IsSlipperyAt(GetWorld(), Feet);
		if (bOnIce != bOnSlipperyIce)
		{
			// Low friction and weak acceleration: turning and stopping take a long slide.
			bOnSlipperyIce = bOnIce;
			UCharacterMovementComponent* Movement = GetCharacterMovement();
			Movement->GroundFriction = bOnIce ? 0.45f : DefaultGroundFriction;
			Movement->BrakingDecelerationWalking = bOnIce ? 170.0f : DefaultBrakingDecelerationWalking;
			Movement->MaxAcceleration = bOnIce ? 1100.0f : DefaultMaxAcceleration;
		}
	}
}

void AChaosImpactCharacter::SetIceFreezePresentation(const bool bFrozen)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	USkeletalMeshComponent* Body = GetMesh();
	if (!bFrozen)
	{
		if (Body)
		{
			Body->SetOverlayMaterial(nullptr);
			Body->bPauseAnims = false;
		}
		// Shatter outward, unless the elimination burst is taking over.
		IceFreezeVisualSeconds = 0.0f;
		bIceThawing = IceBlockMesh && !bEliminated;
		if (bIceThawing)
		{
			ChaosImpactBallTypes::PlayIceShatter(this, GetActorLocation(), 1.1f, 2.0f);
		}
		else if (IceBlockMesh)
		{
			IceBlockMesh->SetHiddenInGame(true);
			for (UProceduralMeshComponent* Shard : IceShardMeshes)
			{
				Shard->SetHiddenInGame(true);
			}
		}
		return;
	}
	if (!IceBlockMesh)
	{
		using namespace ChaosImpactIceMeshes;
		FRandomStream Stream(static_cast<int32>(GetUniqueID()));
		const float HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		FMeshBuffers Block;
		AppendChunk(Block, FTransform::Identity, 54.0f, HalfHeight + 10.0f, Stream);
		IceBlockMesh = CreateComponent(this, GetCapsuleComponent(), Block,
			ChaosImpactBallTypes::MakeIceCrystal(this, 0.3f, 0.14f));
		UMaterialInstanceDynamic* ShardMaterial = ChaosImpactBallTypes::MakeIceCrystal(this, 0.6f, 0.18f);
		for (int32 Index = 0; Index < 9; ++Index)
		{
			// Seven crystals jut from the sides of the block and two from its top.
			const bool bTop = Index >= 7;
			const float Angle = Index * UE_TWO_PI / 7.0f + 0.4f * FMath::Sin(Index * 3.1f);
			const FVector Outward(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f);
			const float Tilt = FMath::DegreesToRadians(bTop ? 22.0f : 38.0f + 12.0f * FMath::Sin(Index * 1.7f));
			const FVector Axis = (FVector::UpVector * FMath::Cos(Tilt) + Outward * FMath::Sin(Tilt)).GetSafeNormal();
			const FVector Base = bTop
				? FVector(Outward.X * 16.0f, Outward.Y * 16.0f, HalfHeight - 6.0f)
				: FVector(Outward.X * 42.0f, Outward.Y * 42.0f, -HalfHeight + 8.0f + (Index % 3) * 26.0f);
			FMeshBuffers Shard;
			AppendCrystal(Shard, FTransform::Identity, Stream.FRandRange(8.0f, 12.0f),
				bTop ? 50.0f : Stream.FRandRange(52.0f, 80.0f), Stream);
			if (UProceduralMeshComponent* ShardMesh = CreateComponent(this, GetCapsuleComponent(), Shard, ShardMaterial))
			{
				IceShardTransforms.Add(FTransform(FRotationMatrix::MakeFromZ(Axis).Rotator(), Base));
				IceShardMeshes.Add(ShardMesh);
			}
		}
		IceOverlayMaterial = ChaosImpactBallTypes::MakeIceCrystal(this, 0.35f, 0.14f);
	}
	if (!IceBlockMesh)
	{
		return;
	}
	bIceThawing = false;
	IceFreezeVisualSeconds = 0.0f;
	if (UMaterialInstanceDynamic* BlockMaterial = Cast<UMaterialInstanceDynamic>(IceBlockMesh->GetMaterial(0)))
	{
		BlockMaterial->SetScalarParameterValue(TEXT("Opacity"), 0.3f);
	}
	if (Body)
	{
		// Tinted and stopped mid-pose inside the block.
		Body->SetOverlayMaterial(IceOverlayMaterial);
		Body->bPauseAnims = true;
	}
	ChaosImpactBallTypes::PlayIceShatter(this, GetActorLocation(), 0.7f, 1.0f);
	UpdateIceFreezePresentation(0.0f);
}

void AChaosImpactCharacter::UpdateIceFreezePresentation(const float DeltaSeconds)
{
	if (!IceBlockMesh || (!bIceFreezeActive && !bIceThawing))
	{
		return;
	}
	IceFreezeVisualSeconds += DeltaSeconds;
	if (bIceFreezeActive)
	{
		// Snap shut with a small overshoot, then tremble just before breaking free.
		const float X = FMath::Clamp(IceFreezeVisualSeconds / 0.16f, 0.0f, 1.0f) - 1.0f;
		const float Pop = 1.0f + X * X * (3.2f * X + 2.2f);
		const float Remaining = static_cast<float>(IceFrozenUntilServerTime - GetSharedServerTime());
		const FVector Tremble = Remaining < 0.45f
			? FVector(FMath::Sin(IceFreezeVisualSeconds * 90.0f) * 2.2f, FMath::Cos(IceFreezeVisualSeconds * 77.0f) * 2.2f, 0.0f)
			: FVector::ZeroVector;
		IceBlockMesh->SetHiddenInGame(false);
		IceBlockMesh->SetRelativeLocationAndRotation(Tremble, FRotator(0.0f, 15.0f, 0.0f));
		IceBlockMesh->SetRelativeScale3D(FVector(FMath::Lerp(0.55f, 1.0f, Pop)));
		for (int32 Index = 0; Index < IceShardMeshes.Num(); ++Index)
		{
			const FTransform& Shard = IceShardTransforms[Index];
			IceShardMeshes[Index]->SetHiddenInGame(false);
			IceShardMeshes[Index]->SetRelativeLocationAndRotation(Shard.GetLocation() + Tremble, Shard.Rotator());
			IceShardMeshes[Index]->SetRelativeScale3D(FVector(FMath::Max(0.01f, Pop)));
		}
		return;
	}
	const float T = IceFreezeVisualSeconds / 0.3f;
	if (T >= 1.0f)
	{
		bIceThawing = false;
		IceBlockMesh->SetHiddenInGame(true);
		for (UProceduralMeshComponent* Shard : IceShardMeshes)
		{
			Shard->SetHiddenInGame(true);
		}
		return;
	}
	IceBlockMesh->SetRelativeScale3D(FVector(1.0f + 0.25f * T));
	if (UMaterialInstanceDynamic* BlockMaterial = Cast<UMaterialInstanceDynamic>(IceBlockMesh->GetMaterial(0)))
	{
		BlockMaterial->SetScalarParameterValue(TEXT("Opacity"), 0.3f * (1.0f - T));
	}
	for (int32 Index = 0; Index < IceShardMeshes.Num(); ++Index)
	{
		const FTransform& Shard = IceShardTransforms[Index];
		const FVector Outward = FVector(Shard.GetLocation().X, Shard.GetLocation().Y, 0.0f).GetSafeNormal();
		IceShardMeshes[Index]->SetRelativeLocationAndRotation(
			Shard.GetLocation() + Outward * (T * 150.0f) + FVector::UpVector * (T * 80.0f - T * T * 120.0f),
			Shard.Rotator() + FRotator(T * 220.0f, 0.0f, T * 160.0f));
		IceShardMeshes[Index]->SetRelativeScale3D(FVector(FMath::Max(0.01f, 1.0f - T)));
	}
}
