// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosImpactCharacter.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactChargeWidget.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactGameMode.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactHazardZone.h"
#include "ChaosImpactSessionSubsystem.h"
#include "ChaosImpactIceMeshes.h"
#include "ChaosImpactLightning.h"
#include "ChaosImpactPuppetComponent.h"
#include "ChaosImpactTornado.h"
#include "ChaosImpactCharacterRoster.h"
#include "ChaosImpactWarpPad.h"
#include "NiagaraComponent.h"
#include "ProceduralMeshComponent.h"
#include "ChaosImpactPlayerController.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "ChaosImpactTrainingTarget.h"
#include "Engine/DamageEvents.h"
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

	/** The roster character a player's model shows. */
	int32 ShownCharacterIndex(const AChaosImpactPlayerState* State)
	{
#if !UE_BUILD_SHIPPING
		// Development (-CIDevCharacter=<index>): every character, CPUs included, shows that model (performance checks).
		static const int32 DevCharacter = []()
		{
			int32 Value = INDEX_NONE;
			FParse::Value(FCommandLine::Get(), TEXT("CIDevCharacter="), Value);
			return Value;
		}();
		if (DevCharacter >= 0)
		{
			return ChaosImpactRoster::ClampIndex(DevCharacter);
		}
#endif
		return ChaosImpactRoster::ClampIndex(State ? State->CharacterIndex : 0);
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

void AChaosImpactCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// A player who switches to watching leaves no HP or ball stock on their screen.
	if (ChargeWidget)
	{
		ChargeWidget->RemoveFromParent();
		ChargeWidget = nullptr;
	}
	Super::EndPlay(EndPlayReason);
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
	BaseMaxWalkSpeed = GetCharacterMovement()->MaxWalkSpeed;
	InitialSpawnLocation = GetActorLocation();
	InitialSpawnRotation = GetActorRotation();
	LocomotionAnimInstanceClass = GetMesh() ? GetMesh()->GetAnimClass() : nullptr;
	AimDirection = GetActorForwardVector().GetSafeNormal2D();
	InitialMeshRelativeScale = GetMesh()->GetRelativeScale3D();
	CreateToonCharacter();
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
		CheckLeftStage();
	}
	if (HasAuthority() && GetWorld())
	{
		// Other machines learn of a charge (to show a nova swelling, a snowball lifted) from when it began.
		const bool bWasCharging = ChargeStartServerTime >= 0.0f;
		if (bIsChargingThrow != bWasCharging)
		{
			ChargeStartServerTime = bIsChargingThrow
				? static_cast<float>(GetSharedServerTime() - (GetWorld()->GetTimeSeconds() - ThrowChargeStartedAt)) : -1.0f;
		}
	}
	UpdateSnowball();
	if (GetNetMode() != NM_DedicatedServer)
	{
		UpdateSnowRollPresentation();
		UpdateNovaChargePresentation(DeltaSeconds);
		UpdateLandingPreview(DeltaSeconds);
		UpdateCameraShake();
		if (ChargeWidget && IsLocallyControlled())
		{
			ChargeWidget->SetBlindAmount(GetBlindAmount());
			ChargeWidget->SetSnowGrowth(GetCarriedBallType(0) == EChaosImpactBallType::Snow ? GetSnowGrowth(0) : 0.0f,
				GetCarriedBallType(1) == EChaosImpactBallType::Snow ? GetSnowGrowth(1) : 0.0f);
		}
	}
	UpdatePresentationLead(DeltaSeconds);
	UpdateToonCharacter();
	TraceNetPresentation(DeltaSeconds);
	UpdateIceStatus(DeltaSeconds);
	UpdateBlackHolePull(DeltaSeconds);
	UpdateWindKnockback(DeltaSeconds);
	UpdateWindCarry(DeltaSeconds);
	if (CameraSnapFrames > 0 && CameraBoom && --CameraSnapFrames == 0)
	{
		CameraBoom->bEnableCameraLag = bCameraLagBeforeSnap;
	}
	if (CameraBoom)
	{
		const float DesiredArmLength = bTrainingMenuCameraActive
			? TrainingMenuCameraArmLength : SavedCameraArmLength + NovaCameraExtra;
		const FVector DesiredSocketOffset = bTrainingMenuCameraActive
			? TrainingMenuCameraSocketOffset : SavedCameraSocketOffset;
		CameraBoom->TargetArmLength = FMath::FInterpTo(CameraBoom->TargetArmLength,
			DesiredArmLength, DeltaSeconds, TrainingMenuCameraBlendSpeed);
		CameraBoom->SocketOffset = FMath::VInterpTo(CameraBoom->SocketOffset,
			DesiredSocketOffset, DeltaSeconds, TrainingMenuCameraBlendSpeed);
		CameraBoom->TargetOffset = bTrainingMenuCameraActive ? FVector::ZeroVector : NovaCameraLead;
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
	// Ball swap. L on Switch pads and L1 on DualSense arrive as LeftShoulder too.
	PlayerInputComponent->BindKey(EKeys::Q, IE_Pressed, this, &AChaosImpactCharacter::RequestBallSwap);
	PlayerInputComponent->BindKey(EKeys::Gamepad_LeftShoulder, IE_Pressed, this, &AChaosImpactCharacter::RequestBallSwap);
	// Throw cancel: the right mouse button, ZL / L2.
	PlayerInputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &AChaosImpactCharacter::RequestCancelThrow);
	PlayerInputComponent->BindKey(EKeys::Gamepad_LeftTrigger, IE_Pressed, this, &AChaosImpactCharacter::RequestCancelThrow);
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
	if (bEliminated || bTrainingMenuFrozen || IsIceFrozen() || IsMatchInputLocked() || IsChargingNova()
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
		FMath::Max(GetChargeSecondsFor(GetCarriedBallType(0)), UE_SMALL_NUMBER), 0.0f, 1.0f);
}

void AChaosImpactCharacter::StartChargingThrow()
{
	const AChaosImpactPlayerController* MenuController = Cast<AChaosImpactPlayerController>(GetController());
	if (MenuController && !MenuController->IsGameplayActive())
	{
		return;
	}
	if (bEliminated || bTrainingMenuFrozen || bIsDashing || bIsChargingThrow || bThrowReleasePending
		|| CarriedBallCount <= 0 || !GetWorld() || IsEliminationPredicted() || IsIceFrozen() || IsMatchInputLocked())
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
		|| GetWorld()->GetTimeSeconds() < NextDashAvailableAtSeconds || IsIceFrozen() || IsMatchInputLocked()
		|| IsChargingNova())
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
	DashStartLocation = GetActorLocation();
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

void AChaosImpactCharacter::PreventClientMoveCombining()
{
	// A client folds similar moves into one before sending them, and rewinds to the first move's start to do it.
	// That rewind erased every move made directly on the actor since then: a joiner in a black hole was never
	// drawn in, and a joiner's dash came up short. The host's own character never takes that path.
	if (HasAuthority() || !IsLocallyControlled())
	{
		return;
	}
	if (FNetworkPredictionData_Client_Character* ClientData = GetCharacterMovement()->GetPredictionData_Client_Character();
		ClientData && ClientData->PendingMove.IsValid())
	{
		ClientData->PendingMove->bForceNoCombine = true;
	}
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
		PreventClientMoveCombining();
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
	DashEndedAtSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
#if !UE_BUILD_SHIPPING
	static const bool bDashLog = FParse::Param(FCommandLine::Get(), TEXT("CIDashLog"));
	if (bDashLog && GetPlayerState())
	{
		UE_LOG(LogChaosImpact, Log, TEXT("DashLog who=%s local=%d authority=%d distance=%.0f"),
			*GetPlayerState()->GetPlayerName(), IsLocallyControlled(), HasAuthority(),
			FVector::Dist2D(DashStartLocation, GetActorLocation()));
	}
#endif
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
	if (IsLocallyControlled() && !bDevAutoInput)
	{
		// Read the right stick itself every frame. The look action's trigger state can report the stick as
		// released in parts of its range, and one round dead zone here treats every direction the same.
		if (const AChaosImpactPlayerController* PadController = Cast<AChaosImpactPlayerController>(GetController());
			PadController && PadController->IsUsingGamepad())
		{
			StickAimInput = ApplyRadialStickDeadZone(ConvertRawControllerAimAxes(FVector2D(
				PadController->GetInputAnalogKeyState(EKeys::Gamepad_RightX),
				PadController->GetInputAnalogKeyState(EKeys::Gamepad_RightY))), StickAimDeadZone);
		}
	}
	if (!IsLocallyControlled())
	{
		// Server copy of a remote player: aim arrives through ServerUpdateAim.
	}
	else if (bDevAutoInput)
	{
		// Development auto-play sets AimDirection itself.
	}
	else if (!StickAimInput.IsNearlyZero())
	{
		const FRotator CameraYaw(0.0f, CameraBoom->GetComponentRotation().Yaw, 0.0f);
		const FVector CameraForward = FRotationMatrix(CameraYaw).GetUnitAxis(EAxis::X);
		const FVector CameraRight = FRotationMatrix(CameraYaw).GetUnitAxis(EAxis::Y);
		const FVector RawStickDirection =
			(CameraForward * StickAimInput.Y + CameraRight * StickAimInput.X).GetSafeNormal2D();
		// While charging, the charge magnet below takes the place of the stick's own assist.
		DesiredDirection = bIsChargingThrow ? RawStickDirection : ApplyControllerAimAssist(RawStickDirection);
	}
	else
	{
		FVector MouseAimPoint;
		if (FindMouseAimPoint(MouseAimPoint))
		{
			DesiredDirection = (MouseAimPoint - GetActorLocation()).GetSafeNormal2D();
		}
	}
	if (bIsChargingThrow && IsLocallyControlled() && !bDevAutoInput && Cast<APlayerController>(GetController()))
	{
		DesiredDirection = ApplyChargeAimMagnet(DesiredDirection);
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

bool AChaosImpactCharacter::FindAimAssistTarget(const FVector& Direction, const float AngleDegrees, FVector& OutToward,
	float& OutDot) const
{
	if (!GetWorld() || Direction.IsNearlyZero())
	{
		return false;
	}
	const FVector Origin = GetActorLocation();
	const float MinimumDot = FMath::Cos(FMath::DegreesToRadians(AngleDegrees));
	// How long a throw now takes to get there, to meet a runner where they will be.
	const AChaosImpactPlayerController* PlayerController = Cast<AChaosImpactPlayerController>(GetController());
	const bool bArc = !PlayerController || PlayerController->GetBallFlightMode() == EChaosImpactBallFlightMode::Arc;
	const float ThrowSpeed = FMath::Max(GetThrowSpeedForCharge(bIsChargingThrow ? GetThrowChargeAlpha() : 1.0f, bArc), 1.0f);
	float BestScore = -TNumericLimits<float>::Max();
	bool bFound = false;
	auto Consider = [&](const AActor* Candidate, const FVector& Velocity)
	{
		const FVector Offset = Candidate->GetActorLocation() - Origin;
		const float Distance = static_cast<float>(Offset.Size2D());
		if (Distance <= UE_SMALL_NUMBER || Distance > ControllerAimAssistDistance)
		{
			return;
		}
		const float Dot = static_cast<float>(FVector::DotProduct(Direction, Offset.GetSafeNormal2D()));
		if (Dot < MinimumDot)
		{
			return;
		}
		// Nearest the aim wins; nearness only breaks near ties, so someone far off in the aim is still chosen.
		const float Score = Dot + (1.0f - Distance / ControllerAimAssistDistance) * 0.02f;
		if (Score <= BestScore)
		{
			return;
		}
		const float Flight = FMath::Min(ThrowReleaseDelaySeconds + Distance / ThrowSpeed, 1.2f);
		const FVector Lead = FVector(Velocity.X, Velocity.Y, 0.0f).GetClampedToMaxSize(1200.0f) * Flight;
		const FVector Toward = (Offset + Lead).GetSafeNormal2D();
		if (Toward.IsNearlyZero())
		{
			return;
		}
		BestScore = Score;
		OutToward = Toward;
		OutDot = Dot;
		bFound = true;
	};
	for (TActorIterator<AChaosImpactCharacter> It(GetWorld()); It; ++It)
	{
		if (*It != this && !It->IsEliminated() && !It->IsHidden() && !AChaosImpactGameState::AreTeammates(GetWorld(), this, *It))
		{
			Consider(*It, It->GetVelocity());
		}
	}
	for (TActorIterator<AChaosImpactTrainingTarget> It(GetWorld()); It; ++It)
	{
		if (!It->IsDefeated() && !It->IsHidden())
		{
			Consider(*It, FVector::ZeroVector);
		}
	}
	return bFound;
}

FVector AChaosImpactCharacter::ApplyControllerAimAssist(const FVector& RawDirection) const
{
	FVector Toward;
	float Dot = 1.0f;
	if (ControllerAimAssistStrength <= 0.0f || !FindAimAssistTarget(RawDirection, ControllerAimAssistAngleDegrees, Toward, Dot))
	{
		return RawDirection;
	}
	return FMath::Lerp(RawDirection, Toward, FMath::Clamp(ControllerAimAssistStrength, 0.0f, 1.0f)).GetSafeNormal2D();
}

FVector AChaosImpactCharacter::ApplyChargeAimMagnet(const FVector& Direction) const
{
	if (!GetWorld() || Direction.IsNearlyZero())
	{
		return Direction;
	}
	const float Charged = GetWorld()->GetTimeSeconds() - ThrowChargeStartedAt;
	const float Strength = FMath::Lerp(ChargeAimMagnetStartStrength, ChargeAimMagnetHoldStrength,
		FMath::Clamp(Charged / ChargeAimMagnetEaseSeconds, 0.0f, 1.0f));
	FVector Toward;
	float Dot = 1.0f;
	if (Strength <= 0.0f || !FindAimAssistTarget(Direction, ChargeAimMagnetAngleDegrees, Toward, Dot))
	{
		return Direction;
	}
	// Fades out toward the edge of the angle, so aiming away lets go smoothly instead of snapping free.
	const float MinimumDot = FMath::Cos(FMath::DegreesToRadians(ChargeAimMagnetAngleDegrees));
	const float EdgeFade = FMath::Clamp((Dot - MinimumDot) / FMath::Max(1.0f - MinimumDot, UE_SMALL_NUMBER) * 3.0f, 0.0f, 1.0f);
	return FMath::Lerp(Direction, Toward, Strength * EdgeFade).GetSafeNormal2D();
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
		// Slot 0 (the right hand) is thrown first, at whatever size a snowball has grown to.
		Ball->SetBallType(GetCarriedBallType(0));
		if (GetCarriedBallType(0) == EChaosImpactBallType::Snow)
		{
			Ball->SetSnowScale(ChaosImpactBallTypes::GetSnowScale(GetSnowGrowth(0)));
		}
		else if (GetCarriedBallType(0) == EChaosImpactBallType::Nova)
		{
			// As big as it was charged.
			Ball->SetSnowScale(ChaosImpactBallTypes::GetNovaScale(ChargeAlpha));
			// The thrower's camera stays out while it flies and bursts.
			NovaCameraHoldUntil = GetWorld()->GetTimeSeconds() + 2.8;
		}
		Ball->FinishSpawning(SpawnTransform);
	}
	if (IsValid(Ball))
	{
		float HorizontalThrowSpeed = 0.0f;
		float ArcUpwardSpeed = 0.0f;
		EChaosImpactBallFlightMode UsedFlightMode = EChaosImpactBallFlightMode::Arc;
		bool bOverhead = false;
		GetThrowFlight(ChargeAlpha, Ball->GetBallType(), Ball->GetSnowScale(), HorizontalThrowSpeed, ArcUpwardSpeed,
			UsedFlightMode, bOverhead);
		PendingThrowDirection = AimDirection.GetSafeNormal2D();
		PendingThrowSpeed = HorizontalThrowSpeed;
		PendingThrowFlightMode = UsedFlightMode;
		PendingThrowArcUpwardSpeed = ArcUpwardSpeed;
		PendingThrowBall = Ball;
		bThrowReleasePending = true;
		if (bOverhead)
		{
			Ball->PrepareForAnimatedThrow(GetRootComponent(), NAME_None,
				GetOverheadHoldOffset(Ball->GetBallType(), Ball->GetSnowScale()), FRotator::ZeroRotator);
		}
		else
		{
			Ball->PrepareForAnimatedThrow(GetMesh(), TEXT("hand_r"),
				HeldBallRelativeLocation, HeldBallRelativeRotation);
		}
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
	ThrowAnimationStartedAt = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
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
	// Who the damage is credited to: the instigating player, else the ball's thrower or the zone's source.
	const AChaosImpactGameState* Match = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	APawn* SourcePawn = EventInstigator ? EventInstigator->GetPawn() : nullptr;
	const AChaosImpactHazardZone* SourceZone = Cast<AChaosImpactHazardZone>(DamageCauser);
	if (const AChaosImpactBall* SourceBall = Cast<AChaosImpactBall>(DamageCauser); !SourcePawn && SourceBall)
	{
		SourcePawn = SourceBall->GetThrowingPawn();
	}
	if (!SourcePawn && SourceZone)
	{
		SourcePawn = SourceZone->GetSourcePawn();
	}
	// VS: nobody is hurt outside the match itself, and teammates never hurt each other.
	if (Match && Match->bVersusMatch && (Match->Phase != EChaosImpactOnlinePhase::Match
		|| AChaosImpactGameState::AreTeammates(GetWorld(), SourcePawn, this)))
	{
		return 0.0f;
	}
	if (bIsDashing && !bApplyingReportedHit)
	{
		return 0.0f;
	}

	const float AppliedDamage = FMath::Min(Health, DamageAmount);
	Health = FMath::Clamp(Health - AppliedDamage, 0.0f, MaxHealth);
	LastDamageCauser = DamageCauser;
	LastDamagedAt = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	OnPlayerHit(Health, AppliedDamage);
	if (Match && Match->bVersusMatch && SourcePawn && SourcePawn != this)
	{
		if (AChaosImpactGameMode* ScoringMode = GetWorld()->GetAuthGameMode<AChaosImpactGameMode>())
		{
			// Every hit scores, each burn of the fire zone included, and a knockout adds its bonus.
			const bool bKnockout = Health <= 0.0f;
			const int32 Points = ChaosImpactMatch::HitPoints + (bKnockout ? ChaosImpactMatch::KnockoutBonusPoints : 0);
			if (Points > 0)
			{
				ScoringMode->AwardMatchPoints(SourcePawn->GetPlayerState(), Points, bKnockout);
			}
		}
	}

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
		// What they carried falls where they went down (nothing to pick up off the stage, though).
		if (!bKnockedOutLeavingStage)
		{
			DropCarriedBalls();
		}
		ClearCarriedBalls();
		IceFrozenUntilServerTime = 0.0;
		BlindedUntilServerTime = 0.0;
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
	// No name for leaving the stage: the respawn panel then says ステージの外に出た.
	const FString DefeatedBy = bKnockedOutLeavingStage ? FString() : GetEliminatorDisplayName(EliminationInstigator.Get());
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
	if (const AChaosImpactGameMode* MatchMode = GetWorld() ? GetWorld()->GetAuthGameMode<AChaosImpactGameMode>() : nullptr)
	{
		// During a VS match, come back somewhere away from opponents instead of the last start point.
		FVector RespawnLocation;
		FRotator RespawnRotation;
		if (MatchMode->ChooseMatchRespawn(this, RespawnLocation, RespawnRotation))
		{
			InitialSpawnLocation = RespawnLocation;
			InitialSpawnRotation = RespawnRotation;
		}
	}
	SetActorLocationAndRotation(InitialSpawnLocation, InitialSpawnRotation, false, nullptr,
		ETeleportType::TeleportPhysics);
	NotifyServerTeleport();
	Health = MaxHealth;
	Stamina = MaxStamina;
	ClearCarriedBalls();
	IceFrozenUntilServerTime = 0.0;
	BlindedUntilServerTime = 0.0;
	if (IsRemotePlayerOnServer())
	{
		ClientBallCountReset(0, 0);
	}
	bIsDashing = false;
	DashElapsedSeconds = 0.0f;
	DashDistanceApplied = 0.0f;
	NextDashAvailableAtSeconds = 0.0f;
	bEliminated = false;
	bKnockedOutLeavingStage = false;
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

void AChaosImpactCharacter::CheckLeftStage()
{
	UWorld* World = GetWorld();
	if (!World || bEliminated || World->GetTimeSeconds() < NextLeftStageCheckAt)
	{
		return;
	}
	NextLeftStageCheckAt = World->GetTimeSeconds() + 0.25;
	const AChaosImpactGameState* Match = World->GetGameState<AChaosImpactGameState>();
	const AChaosImpactGameMode* Mode = World->GetAuthGameMode<AChaosImpactGameMode>();
	const FVector At = GetActorLocation();
	bool bLeft = false;
	if (Match && Match->bVersusMatch)
	{
		// Only after GO: until then the opening itself moves everyone onto the stage.
		bLeft = Match->Phase == EChaosImpactOnlinePhase::Match && !Match->IsMatchInputLocked()
			&& Mode && Mode->IsOutsideStage(At);
	}
	else
	{
		// Training and the online lobby have no stage bounds; only a long fall counts.
		bLeft = At.Z < InitialSpawnLocation.Z - TrainingFallKnockoutDepth;
	}
	if (!bLeft)
	{
		return;
	}
	UE_LOG(LogChaosImpact, Warning, TEXT("%s left the stage at %s and is knocked out"), *GetName(), *At.ToString());
	// The usual knockout (the effect, the respawn countdown, back on a free spot), with nobody credited.
	bKnockedOutLeavingStage = true;
	bIsDashing = false;
	TakeDamage(FMath::Max(Health, 1.0f), FDamageEvent(), nullptr, this);
	if (!bEliminated)
	{
		// Refused (nothing should refuse it, but a character must never stay stuck out there): straight back.
		bKnockedOutLeavingStage = false;
		ResetAfterElimination();
	}
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
	if (UNiagaraSystem* Burst = ChaosImpactBallTypes::LoadEffect(ChaosImpactBallTypes::Effects::Damage))
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
	if (ToonCharacter)
	{
		ToonCharacter->SetPartsVisible(true);
	}
	UpdateBallPresentation();
}

void AChaosImpactCharacter::StartRespawnEffect()
{
	bRespawnEffectActive = true;
	RespawnEffectTime = 0.0f;
	GetMesh()->SetVisibility(true, false);
	if (ToonCharacter)
	{
		ToonCharacter->SetPartsVisible(true);
	}
	GetMesh()->SetRelativeScale3D(InitialMeshRelativeScale * 0.08f);
	UpdateBallPresentation();
	if (EliminationFlash)
	{
		EliminationFlash->SetLightColor(FLinearColor(0.0f, 0.8f, 1.0f));
		EliminationFlash->SetIntensity(22000.0f);
	}
	if (UNiagaraSystem* Burst = ChaosImpactBallTypes::LoadEffect(ChaosImpactBallTypes::Effects::Damage))
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
	// The same name that floats above that character (CPU1, P2, a room name...).
	if (const AChaosImpactCharacter* InstigatorCharacter = Cast<AChaosImpactCharacter>(EventInstigator->GetPawn()))
	{
		const FString OverheadName = InstigatorCharacter->GetOverheadDisplayName();
		if (!OverheadName.IsEmpty())
		{
			return OverheadName;
		}
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
	// Back in play at the normal view, even if knocked out while charging a nova.
	NovaCameraExtra = 0.0f;
	NovaCameraLead = FVector::ZeroVector;
	NovaCameraHoldUntil = 0.0;
	if (CameraBoom)
	{
		CameraBoom->TargetArmLength = SavedCameraArmLength;
		CameraBoom->TargetOffset = FVector::ZeroVector;
	}
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
	// A knockout: two light taps.
	PlayControllerRumble(0.25f, 0.6f, 0.09f);
	PlayControllerRumble(0.25f, 0.6f, 0.09f, 0.17f);
}

void AChaosImpactCharacter::PlayControllerRumble(const float Small, const float Big, const float Seconds,
	const float DelaySeconds) const
{
	if (AChaosImpactPlayerController* RumbleController = Cast<AChaosImpactPlayerController>(GetController());
		RumbleController && IsLocallyControlled())
	{
		RumbleController->PlayRumble(Small, Big, Seconds, DelaySeconds);
	}
}

void AChaosImpactCharacter::UpdateControllerRumble()
{
	AChaosImpactPlayerController* RumbleController = Cast<AChaosImpactPlayerController>(GetController());
	if (!RumbleController || !IsLocallyControlled())
	{
		bRumbleBlackHolePull = false;
		return;
	}

	// Knocked out: a long heavy shake. Hit: a short hard knock, or a crackling buzz from a thunder burst.
	if (bEliminated && !bRumbleWasEliminated)
	{
		PlayControllerRumble(0.6f, 1.0f, 0.55f);
	}
	else if (!bEliminated && RumbleLastHealth >= 0.0f && Health < RumbleLastHealth)
	{
		bool bThunder = false;
		for (TActorIterator<AChaosImpactHazardZone> It(GetWorld()); It; ++It)
		{
			bThunder |= It->GetZoneType() == EChaosImpactBallType::Thunder
				&& FVector::Dist2D(It->GetActorLocation(), GetActorLocation()) < AChaosImpactHazardZone::ThunderRadius + 120.0f;
		}
		if (bThunder)
		{
			PlayControllerRumble(0.9f, 0.25f, 0.38f);
		}
		else
		{
			PlayControllerRumble(0.35f, 0.85f, 0.2f);
		}
	}
	bRumbleWasEliminated = bEliminated;
	RumbleLastHealth = Health;

	// The throw is fully charged: one short fine tick.
	const bool bChargeFull = bIsChargingThrow && GetThrowChargeAlpha() >= 0.999f;
	if (bChargeFull && !bRumbleChargeFull)
	{
		PlayControllerRumble(0.45f, 0.0f, 0.07f);
	}
	bRumbleChargeFull = bChargeFull;

	// A ball picked up: the faintest tick.
	if (RumbleLastBallCount >= 0 && CarriedBallCount > RumbleLastBallCount && !bEliminated)
	{
		PlayControllerRumble(0.25f, 0.0f, 0.05f);
	}
	RumbleLastBallCount = CarriedBallCount;

	// Held: a black hole's pull, and a warp pad charging up under the player.
	float HeldSmall = 0.0f;
	float HeldBig = 0.0f;
	if (bRumbleBlackHolePull && !bEliminated)
	{
		HeldSmall = 0.12f;
		HeldBig = 0.22f;
	}
	const float WarpCharge = AChaosImpactWarpPad::FindChargeProgress(this);
	if (WarpCharge > 0.0f && !bEliminated)
	{
		HeldSmall = FMath::Max(HeldSmall, 0.08f + 0.25f * WarpCharge);
	}
	RumbleController->SetSustainedRumble(HeldSmall, HeldBig);
	bRumbleBlackHolePull = false;
}

void AChaosImpactCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	// The owner predicts its own count and reconciles through ordered answers (PendingBallActions);
	// a replicated value would overwrite predictions the server has not processed yet.
	DOREPLIFETIME_CONDITION(AChaosImpactCharacter, CarriedBallCount, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(AChaosImpactCharacter, CarriedBallTypes, COND_SkipOwner);
	DOREPLIFETIME(AChaosImpactCharacter, IceFrozenUntilServerTime);
	DOREPLIFETIME(AChaosImpactCharacter, BlindedUntilServerTime);
	DOREPLIFETIME(AChaosImpactCharacter, SnowGrowthRight);
	DOREPLIFETIME(AChaosImpactCharacter, SnowGrowthLeft);
	DOREPLIFETIME_CONDITION(AChaosImpactCharacter, ChargeStartServerTime, COND_SkipOwner);
	// The owner starts its own carry the moment its screen sees the catch.
	DOREPLIFETIME_CONDITION(AChaosImpactCharacter, WindCarrier, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(AChaosImpactCharacter, WindCarryStartServerTime, COND_SkipOwner);
	DOREPLIFETIME(AChaosImpactCharacter, Health);
	DOREPLIFETIME_CONDITION(AChaosImpactCharacter, bReplicatedDashing, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(AChaosImpactCharacter, ReplicatedDashDirection, COND_SkipOwner);
	DOREPLIFETIME(AChaosImpactCharacter, bEliminated);
	DOREPLIFETIME_CONDITION(AChaosImpactCharacter, CPUNumber, COND_InitialOnly);
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

void AChaosImpactCharacter::ServerReportTornadoHit_Implementation(AChaosImpactTornado* Tornado)
{
	if (!IsValid(Tornado) || bEliminated)
	{
		return;
	}
	TGuardValue<bool> ReportGuard(bApplyingReportedHit, true);
	Tornado->TryApplyReportedHit(this);
}

void AChaosImpactCharacter::StartWindKnockback(const FVector& Velocity, const float Seconds)
{
	if (!IsLocallyControlled() || bEliminated || !GetWorld())
	{
		return;
	}
	WindKnockbackVelocity = FVector(Velocity.X, Velocity.Y, 0.0f);
	WindKnockbackSeconds = FMath::Max(Seconds, 0.05f);
	WindKnockbackStartedAt = GetWorld()->GetTimeSeconds();
	UE_LOG(LogChaosImpact, Log, TEXT("Wind knockback on %s (authority=%d) from %s"), *GetNameSafe(GetPlayerState()), HasAuthority(),
		*GetActorLocation().ToCompactString());
	// Blown off the ground a little too.
	if (GetCharacterMovement() && GetCharacterMovement()->IsMovingOnGround())
	{
		LaunchCharacter(FVector(0.0f, 0.0f, 420.0f), false, false);
	}
	PlayControllerRumble(0.6f, 0.9f, 0.25f);
}

void AChaosImpactCharacter::BeginWindCarry(AChaosImpactTornado* Tornado)
{
	if (!IsValid(Tornado) || bEliminated || !GetWorld() || WindCarrier == Tornado)
	{
		return;
	}
	WindCarrier = Tornado;
	WindCarryStartServerTime = GetSharedServerTime();
	const FVector Offset = GetActorLocation() - Tornado->GetCenter();
	WindCarryAngle = FMath::Atan2(static_cast<float>(Offset.Y), static_cast<float>(Offset.X));
	WindCarryRadius = FMath::Max(static_cast<float>(Offset.Size2D()), 40.0f);
	if (IsLocallyControlled())
	{
		if (bIsChargingThrow)
		{
			CancelChargingThrow();
		}
		GetCharacterMovement()->StopMovementImmediately();
		PlayControllerRumble(0.5f, 0.8f, WindCarrySeconds);
	}
	if (HasAuthority())
	{
		ForceNetUpdate();
	}
	UE_LOG(LogChaosImpact, Log, TEXT("Wind carry on %s (authority=%d local=%d)"), *GetNameSafe(GetPlayerState()), HasAuthority(),
		IsLocallyControlled());
}

bool AChaosImpactCharacter::IsMoveInputIgnored() const
{
	return Super::IsMoveInputIgnored() || WindCarrier != nullptr;
}

void AChaosImpactCharacter::UpdateWindCarry(const float DeltaSeconds)
{
	const auto RestMesh = [this]()
	{
		if (bWindCarryPosed && GetMesh())
		{
			GetMesh()->SetRelativeLocationAndRotation(MeshRestLocation, MeshRestRotation);
		}
		bWindCarryPosed = false;
	};
	AChaosImpactTornado* Tornado = WindCarrier.Get();
	if (!Tornado)
	{
		RestMesh();
		return;
	}
	const float Elapsed = FMath::Max(0.0f, static_cast<float>(GetSharedServerTime() - WindCarryStartServerTime));
	if (bEliminated || !IsValid(Tornado) || Elapsed >= WindCarrySeconds || !GetWorld())
	{
		if (IsLocallyControlled() && IsValid(Tornado) && !bEliminated)
		{
			// Thrown out of the funnel, along its whirl.
			FVector Outward = (GetActorLocation() - Tornado->GetCenter()).GetSafeNormal2D();
			if (Outward.IsNearlyZero())
			{
				Outward = GetActorForwardVector().GetSafeNormal2D();
			}
			StartWindKnockback((Outward + FVector::CrossProduct(FVector::UpVector, Outward) * 0.5f).GetSafeNormal2D() * 1200.0f, 0.4f);
		}
		WindCarrier = nullptr;
		RestMesh();
		return;
	}
	const float Progress = Elapsed / WindCarrySeconds;
	if (IsLocallyControlled())
	{
		// Drawn in toward the funnel and whirled round it, faster as it goes on.
		const float Delta = FMath::Clamp(DeltaSeconds, 0.0f, 0.1f);
		WindCarryAngle += (7.0f + 6.0f * Progress) * Delta;
		WindCarryRadius = FMath::FInterpTo(WindCarryRadius, 55.0f, Delta, 6.0f);
		const FVector Target = Tornado->GetCenter()
			+ FVector(FMath::Cos(WindCarryAngle) * WindCarryRadius, FMath::Sin(WindCarryAngle) * WindCarryRadius, 0.0f);
		const FVector Move(Target.X - GetActorLocation().X, Target.Y - GetActorLocation().Y, 0.0f);
		AddActorWorldOffset(Move, true);
		bRumbleBlackHolePull = true;
		PreventClientMoveCombining();
	}
	// Everyone sees the body lifted and spinning in the wind.
	if (USkeletalMeshComponent* Body = GetMesh())
	{
		if (!bWindCarryPosed)
		{
			MeshRestLocation = Body->GetRelativeLocation();
			MeshRestRotation = Body->GetRelativeRotation();
			bWindCarryPosed = true;
		}
		const float Lift = 160.0f * FMath::Sin(FMath::Min(Progress * 1.15f, 1.0f) * UE_PI * 0.5f)
			* (Progress > 0.8f ? 1.0f - (Progress - 0.8f) / 0.2f * 0.6f : 1.0f);
		Body->SetRelativeLocationAndRotation(MeshRestLocation + FVector(0.0f, 0.0f, Lift),
			MeshRestRotation + FRotator(0.0f, Elapsed * 1300.0f, 0.0f));
	}
}

void AChaosImpactCharacter::UpdateWindKnockback(const float DeltaSeconds)
{
	if (!IsLocallyControlled() || DeltaSeconds <= 0.0f || !GetWorld() || WindKnockbackSeconds <= 0.0f)
	{
		return;
	}
	const float Elapsed = static_cast<float>(GetWorld()->GetTimeSeconds() - WindKnockbackStartedAt);
	if (bEliminated || Elapsed >= WindKnockbackSeconds)
	{
		WindKnockbackSeconds = 0.0f;
		return;
	}
	// Strongest at once, easing out.
	const float Remaining = 1.0f - Elapsed / WindKnockbackSeconds;
	AddActorWorldOffset(WindKnockbackVelocity * (Remaining * Remaining * 1.6f) * DeltaSeconds, true);
	bRumbleBlackHolePull = true;
	PreventClientMoveCombining();
}

void AChaosImpactCharacter::ClientTeleportTo_Implementation(FVector_NetQuantize Location, FRotator Rotation)
{
	SetActorLocationAndRotation(Location, Rotation, false, nullptr, ETeleportType::TeleportPhysics);
	GetCharacterMovement()->StopMovementImmediately();
	SnapCameraToCharacter();
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

void AChaosImpactCharacter::ServerSwapBalls_Implementation()
{
	if (!bEliminated && CarriedBallCount >= 2)
	{
		SwapCarriedBalls();
		UpdateBallPresentation();
	}
	// Always answered, even when nothing changed, so the client's queued swap is resolved in order.
	ClientSwapResolved(CarriedBallCount, CarriedBallTypes);
}

void AChaosImpactCharacter::ClientSwapResolved_Implementation(const int32 ServerBallCount,
	const uint8 ServerBallTypes)
{
	ResolveOldestBallAction(ServerBallCount, ServerBallTypes);
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
		else if (Action == -2)
		{
			if (Types.Num() >= 2)
			{
				Types.Swap(0, 1);
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
	// Held by the VS opening and results like real input, or the test player walks through them.
	if (bEliminated || !GetWorld() || IsMatchInputLocked())
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

void AChaosImpactCharacter::TraceNetPresentation(const float DeltaSeconds)
{
#if !UE_BUILD_SHIPPING
	// Development (-CINetTrace): every machine logs where it draws each character against the shared server
	// clock. A player's own machine (and the host, for CPUs) gives the true position, so comparing the logs
	// shows how far behind or off each remote player looks on every other screen.
	static const bool bTraceEnabled = FParse::Param(FCommandLine::Get(), TEXT("CINetTrace"));
	if (!bTraceEnabled || GetNetMode() == NM_Standalone || !GetWorld())
	{
		return;
	}
	NetTraceSeconds += DeltaSeconds;
	if (NetTraceSeconds < 0.05f)
	{
		return;
	}
	NetTraceSeconds = 0.0f;
	const APlayerState* State = GetPlayerState();
	if (!State || bEliminated)
	{
		return;
	}
	const bool bOwnTruth = IsLocallyControlled();
	const FVector Drawn = bOwnTruth ? GetActorLocation() : GetPresentationLocation();
	UE_LOG(LogChaosImpact, Log, TEXT("NetTrace t=%.3f who=%s own=%d x=%.1f y=%.1f"), GetSharedServerTime(),
		*State->GetPlayerName(), bOwnTruth ? 1 : 0, Drawn.X, Drawn.Y);
#endif
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
		// Measured at ~250 ms ping (NetTrace): leading by the whole delay overshoots every turn and stop, and
		// leading by the smoothing time only pushes host-moved characters (the host, CPUs) ahead of where they
		// are. About half the player's round trip gave the smallest error.
		float LeadScale = PresentationLeadShareOfPing;
		float SmoothingShare = 0.0f;
#if !UE_BUILD_SHIPPING
		// Development (-CILeadScale=, -CISmoothLead=): tries other shares when measuring online lag.
		static const float DevLeadScale = []()
		{
			float Value = PresentationLeadShareOfPing;
			FParse::Value(FCommandLine::Get(), TEXT("CILeadScale="), Value);
			return Value;
		}();
		static const float DevSmoothingShare = []()
		{
			float Value = 0.0f;
			FParse::Value(FCommandLine::Get(), TEXT("CISmoothLead="), Value);
			return Value;
		}();
		LeadScale = DevLeadScale;
		SmoothingShare = DevSmoothingShare;
#endif
		const float LeadSeconds = FMath::Clamp(GetNetworkRoundTripSeconds() * LeadScale + SmoothingLag * SmoothingShare,
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
	PlayControllerRumble(0.25f, 0.6f, 0.09f);
	PlayControllerRumble(0.25f, 0.6f, 0.09f, 0.17f);
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
	BlindedUntilServerTime = 0.0;
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
	// The normal view to come back to is the one saved at BeginPlay: taking it from the camera now would keep a
	// nova's zoom (or a half-finished blend) for good.
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
		// A snowball in the left hand is carried at (up to a hand's worth of) its size; the right one is rolled.
		const bool bLeftSnow = GetCarriedBallType(1) == EChaosImpactBallType::Snow;
		LeftHeldBallMesh->SetRelativeScale3D(FVector(0.34f * (bLeftSnow
			? FMath::Min(ChaosImpactBallTypes::GetSnowScale(GetSnowGrowth(1)), 1.5f) : 1.0f)));
	}
	// The right hand's size follows the ball there: a snowball at its size (hidden once it goes over the head).
	if (HeldBallMesh)
	{
		const bool bRightSnow = CarriedBallCount > 0 && GetCarriedBallType(0) == EChaosImpactBallType::Snow;
		const float RightScale = bRightSnow ? ChaosImpactBallTypes::GetSnowScale(GetSnowGrowth(0)) : 1.0f;
		HeldBallMesh->SetRelativeScale3D(FVector(0.34f * RightScale));
		if (bRightSnow)
		{
			HeldBallMesh->SetVisibility(false, true);
		}
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
	CarriedBallTypes = ChaosImpactBallTypes::SetPackedSlot(CarriedBallTypes, FMath::Max(CarriedBallCount, 0), Type);
	// A snowball starts small whoever last carried it.
	SetSlotSnowGrowth(FMath::Max(CarriedBallCount, 0), 0.0f);
	++CarriedBallCount;
}

EChaosImpactBallType AChaosImpactCharacter::PopCarriedBall()
{
	const EChaosImpactBallType Type = GetCarriedBallType(0);
	CarriedBallTypes = static_cast<uint8>(CarriedBallTypes >> ChaosImpactBallTypes::PackedBitsPerSlot);
	CarriedBallCount = FMath::Max(0, CarriedBallCount - 1);
	SetSlotSnowGrowth(0, GetSnowGrowth(1));
	SetSlotSnowGrowth(1, 0.0f);
	return Type;
}

void AChaosImpactCharacter::ClearCarriedBalls()
{
	CarriedBallCount = 0;
	CarriedBallTypes = 0;
	SetSlotSnowGrowth(0, 0.0f);
	SetSlotSnowGrowth(1, 0.0f);
}

void AChaosImpactCharacter::SwapCarriedBalls()
{
	if (CarriedBallCount < 2)
	{
		return;
	}
	const EChaosImpactBallType Swapped[] = {GetCarriedBallType(1), GetCarriedBallType(0)};
	CarriedBallTypes = ChaosImpactBallTypes::Pack(Swapped);
	const float Growth[] = {GetSnowGrowth(1), GetSnowGrowth(0)};
	SetSlotSnowGrowth(0, Growth[0]);
	SetSlotSnowGrowth(1, Growth[1]);
}

void AChaosImpactCharacter::RequestBallSwap()
{
	const AChaosImpactPlayerController* MenuController = Cast<AChaosImpactPlayerController>(GetController());
	if ((MenuController && !MenuController->IsGameplayActive()) || bEliminated || bTrainingMenuFrozen
		|| CarriedBallCount < 2 || !GetWorld() || IsEliminationPredicted() || IsMatchInputLocked())
	{
		return;
	}
	if (HasAuthority())
	{
		SwapCarriedBalls();
		UpdateBallPresentation();
	}
	else
	{
		// Predicted like pickups and throws: it waits in the same ordered queue for the server's answer,
		// so a throw released right after the swap uses the swapped ball on both machines.
		PendingBallActions.Add(-2);
		ServerSwapBalls();
		RefreshPredictedBallCount();
	}
	if (ChargeWidget)
	{
		ChargeWidget->PlayBallSwap();
	}
}

FString AChaosImpactCharacter::GetOverheadDisplayName() const
{
	if (CPUNumber > 0)
	{
		return FString::Printf(TEXT("CPU%d"), CPUNumber);
	}
	const APlayerState* State = GetPlayerState();
	const AChaosImpactGameState* Room = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	if (Room && Room->bOnlineRoom)
	{
		return State ? State->GetPlayerName() : FString();
	}
	// Offline: split-screen players are P1-P4; a player alone on the machine uses the title-screen name.
	const APlayerController* OwningController = Cast<APlayerController>(GetController());
	const UGameInstance* GameInstance = GetGameInstance();
	if (OwningController && OwningController->GetLocalPlayer() && GameInstance)
	{
		const TArray<ULocalPlayer*>& LocalPlayers = GameInstance->GetLocalPlayers();
		if (LocalPlayers.Num() <= 1)
		{
			if (const UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this);
				Sessions && !Sessions->GetPlayerName().IsEmpty())
			{
				return Sessions->GetPlayerName();
			}
		}
		return FString::Printf(TEXT("P%d"), FMath::Max(LocalPlayers.IndexOfByKey(OwningController->GetLocalPlayer()), 0) + 1);
	}
	if (Cast<AChaosImpactCPUController>(GetController()))
	{
		return TEXT("CPU");
	}
	return State ? State->GetPlayerName() : FString();
}

void AChaosImpactCharacter::ApplyHeldBallAppearance(UStaticMeshComponent* HandBall, const EChaosImpactBallType Type)
{
	if (!HandBall)
	{
		return;
	}
	using namespace ChaosImpactBallTypes;
	UMaterialInterface* Material = nullptr;
	switch (Type)
	{
	case EChaosImpactBallType::Fire:
		if (!HeldFireMaterial)
		{
			HeldFireMaterial = MakeEmissive(this, FLinearColor(1.0f, 0.24f, 0.01f), 1.1f);
		}
		Material = HeldFireMaterial;
		break;
	case EChaosImpactBallType::Ice:
		if (!HeldIceMaterial)
		{
			HeldIceMaterial = MakeIceCrystal(this, 0.85f, 0.4f, FLinearColor(0.68f, 0.86f, 1.0f));
		}
		Material = HeldIceMaterial;
		break;
	case EChaosImpactBallType::Thunder:
		if (!HeldThunderMaterial)
		{
			HeldThunderMaterial = MakeEmissive(this, FLinearColor(1.0f, 0.9f, 0.4f), 2.4f);
		}
		Material = HeldThunderMaterial;
		break;
	case EChaosImpactBallType::Black:
		if (!HeldBlackMaterial)
		{
			HeldBlackMaterial = MakeEmissive(this, FLinearColor(0.012f, 0.0f, 0.025f), 1.0f);
		}
		Material = HeldBlackMaterial;
		break;
	case EChaosImpactBallType::Wind:
		if (!HeldWindMaterial)
		{
			HeldWindMaterial = MakeEmissive(this, FLinearColor(0.2f, 0.9f, 0.35f), 1.4f);
		}
		Material = HeldWindMaterial;
		break;
	case EChaosImpactBallType::Smoke:
		if (!HeldSmokeMaterial)
		{
			HeldSmokeMaterial = MakeEmissive(this, FLinearColor(0.2f, 0.19f, 0.26f), 1.0f);
		}
		Material = HeldSmokeMaterial;
		break;
	case EChaosImpactBallType::Beam:
		if (!HeldBeamMaterial)
		{
			HeldBeamMaterial = MakeEmissive(this, FLinearColor(1.0f, 0.3f, 0.82f), 3.0f);
		}
		Material = HeldBeamMaterial;
		break;
	case EChaosImpactBallType::Snow:
		if (!HeldSnowMaterial)
		{
			HeldSnowMaterial = MakeSnow(this);
		}
		Material = HeldSnowMaterial;
		break;
	case EChaosImpactBallType::Nova:
		if (!HeldNovaMaterial)
		{
			HeldNovaMaterial = MakeEmissive(this, FLinearColor(0.55f, 0.88f, 1.0f), 4.0f);
		}
		Material = HeldNovaMaterial;
		break;
	default:
		// No override for a normal ball: the mesh's own material.
		break;
	}
	HandBall->SetMaterial(0, Material);

	// A carried fire ball keeps burning in the hand, a thunder ball crackles, a black ball smoulders, a smoke ball
	// leaks wisps of smoke and a beam ball throws off sparks.
	TObjectPtr<UNiagaraComponent>& HandEffect = HandBall == LeftHeldBallMesh ? LeftHeldFire : RightHeldFire;
	EChaosImpactBallType& HandEffectType = HandBall == LeftHeldBallMesh ? LeftHeldEffectType : RightHeldEffectType;
	const TCHAR* WantedSystem = Type == EChaosImpactBallType::Fire ? Effects::Fire
		: Type == EChaosImpactBallType::Thunder ? Effects::Electricity
		: Type == EChaosImpactBallType::Black ? Effects::DarkAura
		: Type == EChaosImpactBallType::Smoke ? Effects::LastHitSmoke
		: Type == EChaosImpactBallType::Beam || Type == EChaosImpactBallType::Nova ? Effects::WindSparks : nullptr;
	const bool bWantEffect = WantedSystem && HandBall->IsVisible() && GetNetMode() != NM_DedicatedServer;
	if (HandEffect && WantedSystem && HandEffectType != Type)
	{
		HandEffect->DestroyComponent();
		HandEffect = nullptr;
	}
	if (bWantEffect && !HandEffect)
	{
		if (UNiagaraSystem* System = LoadEffect(WantedSystem))
		{
			HandEffect = UNiagaraFunctionLibrary::SpawnSystemAttached(System, HandBall, NAME_None,
				FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset, false);
			if (HandEffect)
			{
				HandEffectType = Type;
				HandEffect->SetUsingAbsoluteScale(true);
				HandEffect->SetWorldScale3D(FVector(Type == EChaosImpactBallType::Fire ? 1.0f
					: Type == EChaosImpactBallType::Smoke ? 0.5f : 0.4f));
				if (Type == EChaosImpactBallType::Smoke)
				{
					SetEffectColor(HandEffect, TEXT("Smoke Color"), FLinearColor(0.42f, 0.4f, 0.5f));
					SetEffectSize(HandEffect, TEXT("Sprite Size"), 40.0f);
				}
				else if (Type == EChaosImpactBallType::Beam)
				{
					SetEffectFloat(HandEffect, TEXT("Spark Spawn Rate"), 30.0f);
				}
				if (Type == EChaosImpactBallType::Fire)
				{
					SetEffectFloat(HandEffect, TEXT("Flame Scale"), 0.7f);
					SetEffectFloat(HandEffect, TEXT("Smoke Spawn Scale"), 0.1f);
					SetEffectFloat(HandEffect, TEXT("Base Light Intentsity"), 0.0f);
				}
			}
		}
	}
	else if (HandEffect && bWantEffect != HandEffect->IsActive())
	{
		if (bWantEffect)
		{
			HandEffect->Activate(true);
		}
		else
		{
			HandEffect->Deactivate();
		}
	}
}

void AChaosImpactCharacter::UpdateBlackHolePull(const float DeltaSeconds)
{
#if !UE_BUILD_SHIPPING
	// Development (-CIZoneLog): says on every machine whether this character is being drawn in, and why not.
	static const bool bZoneLog = FParse::Param(FCommandLine::Get(), TEXT("CIZoneLog"));
	if (bZoneLog && GetWorld() && GetPlayerState())
	{
		const double LogNow = FPlatformTime::Seconds();
		const FVector Probe = AChaosImpactHazardZone::GetBlackHolePullOffset(GetWorld(), this, DeltaSeconds);
		float Nearest = -1.0f;
		FString Source;
		for (TActorIterator<AChaosImpactHazardZone> It(GetWorld()); It; ++It)
		{
			const float Distance = static_cast<float>(FVector::Dist2D(It->GetActorLocation(), GetActorLocation()));
			if (It->GetZoneType() == EChaosImpactBallType::Black && (Nearest < 0.0f || Distance < Nearest))
			{
				Nearest = Distance;
				Source = GetNameSafe(It->GetSourcePawn());
			}
		}
		// One line per character per half second while a black hole is near.
		static TMap<FString, double> NextLogByName;
		double& NextAt = NextLogByName.FindOrAdd(GetPlayerState()->GetPlayerName());
		if (Nearest >= 0.0f && Nearest < AChaosImpactHazardZone::BlackHoleRadius + 200.0f && LogNow >= NextAt)
		{
			NextAt = LogNow + 0.5;
			UE_LOG(LogChaosImpact, Log,
				TEXT("ZoneLog black who=%s local=%d authority=%d dist=%.0f source=%s pull=%.1f eliminated=%d"),
				*GetPlayerState()->GetPlayerName(), IsLocallyControlled(), HasAuthority(), Nearest, *Source,
				Probe.Size2D(), bEliminated);
		}
	}
#endif
	// Moved only where this character is moved (its owner, the host, a CPU); online the new position follows.
	if (!IsLocallyControlled() || bEliminated || DeltaSeconds <= 0.0f || !GetWorld())
	{
		return;
	}
	// A short dash breaks free for its length and a moment after, so enough stamina gets a player out.
	if (bIsDashing || GetWorld()->GetTimeSeconds() - DashEndedAtSeconds < AChaosImpactHazardZone::BlackHoleDashGraceSeconds)
	{
		return;
	}
	const FVector Pull = AChaosImpactHazardZone::GetBlackHolePullOffset(GetWorld(), this, DeltaSeconds);
	if (!Pull.IsNearlyZero())
	{
		AddActorWorldOffset(Pull, true);
		bRumbleBlackHolePull = true;
		PreventClientMoveCombining();
	}
}

bool AChaosImpactCharacter::IsMatchInputLocked() const
{
	const AChaosImpactGameState* Match = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	return Match && Match->IsMatchInputLocked();
}

void AChaosImpactCharacter::WarpTo(const FVector& Location)
{
	if (!HasAuthority())
	{
		return;
	}
	if (bIsDashing)
	{
		FinishDash();
	}
	SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
	GetCharacterMovement()->StopMovementImmediately();
	// A remote player's own client moves them; this also sends them the new position.
	NotifyServerTeleport();
	SnapCameraToCharacter();
}

void AChaosImpactCharacter::SnapCameraToCharacter()
{
	if (!CameraBoom || !IsLocallyControlled())
	{
		return;
	}
	if (CameraSnapFrames == 0)
	{
		bCameraLagBeforeSnap = CameraBoom->bEnableCameraLag;
	}
	// Lag off for a couple of frames lets the spring arm land on the new spot, then it smooths again.
	CameraBoom->bEnableCameraLag = false;
	CameraSnapFrames = 2;
}

bool AChaosImpactCharacter::CanJumpInternal_Implementation() const
{
	return !IsMatchInputLocked() && !IsChargingNova() && Super::CanJumpInternal_Implementation();
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
	UpdateLastHitPresentation();
	UpdateControllerRumble();

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
		if (ToonCharacter)
		{
			ToonCharacter->SetPartsOverlayMaterial(nullptr);
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
		if (ToonCharacter)
		{
			ToonCharacter->SetPartsOverlayMaterial(IceOverlayMaterial);
		}
	}
	ChaosImpactBallTypes::PlayIceShatter(this, GetActorLocation(), 0.7f, 1.0f);
	UpdateIceFreezePresentation(0.0f);
}

void AChaosImpactCharacter::UpdateLastHitPresentation()
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const UWorld* World = GetWorld();
	const AChaosImpactGameState* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
	// Everywhere HP can run low: training, the room lobby, and a VS match while it is being played.
	const bool bMatchNotInPlay = Match && Match->bVersusMatch && Match->Phase != EChaosImpactOnlinePhase::Match;
	bLastHitSmokeOn = World && !bMatchNotInPlay && !bEliminated && !bEliminationEffectActive
		&& Health > 0.0f && Health <= 1.0f;
	if (!bLastHitSmokeOn)
	{
		// Let a puff already in the air drift away instead of vanishing.
		if (LastHitSmoke && LastHitSmoke->IsActive())
		{
			LastHitSmoke->Deactivate();
		}
		NextLastHitPuffAt = 0.0;
		return;
	}
	if (!LastHitSmoke)
	{
		UNiagaraSystem* Smoke = ChaosImpactBallTypes::LoadEffect(ChaosImpactBallTypes::Effects::LastHitSmoke);
		LastHitSmoke = Smoke ? UNiagaraFunctionLibrary::SpawnSystemAttached(Smoke, GetMesh(), NAME_None,
			FVector(0.0f, 0.0f, 125.0f), FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset, false,
			false) : nullptr;
		if (LastHitSmoke)
		{
			LastHitSmoke->SetUsingAbsoluteScale(true);
			LastHitSmoke->SetWorldScale3D(FVector(0.13f));
		}
	}
	if (!LastHitSmoke)
	{
		return;
	}
	// Like a fighter on high damage: a small puff every second or so, not a steady column.
	const double Now = World->GetTimeSeconds();
	if (Now >= NextLastHitPuffAt)
	{
		LastHitSmoke->Activate(true);
		LastHitPuffEndsAt = Now + 0.1;
		NextLastHitPuffAt = Now + FMath::FRandRange(1.0f, 1.5f);
	}
	else if (LastHitSmoke->IsActive() && Now >= LastHitPuffEndsAt)
	{
		LastHitSmoke->Deactivate();
	}
}

void AChaosImpactCharacter::CreateToonCharacter()
{
	if (!bUseToonCharacter || ToonCharacter || GetNetMode() == NM_DedicatedServer || !GetMesh())
	{
		return;
	}
	const AChaosImpactPlayerState* PickState = GetPlayerState<AChaosImpactPlayerState>();
	ToonCharacter = NewObject<UChaosImpactPuppetComponent>(this);
	ToonCharacter->SetupAttachment(GetMesh());
	ToonCharacter->SetRelativeScale3D(FVector(ToonCharacterScale));
	ToonCharacter->RegisterComponent();
	if (!ToonCharacter->Initialize(GetMesh(), ShownCharacterIndex(PickState)))
	{
		ToonCharacter->DestroyComponent();
		ToonCharacter = nullptr;
		return;
	}
	ToonCharacter->SetHeldBalls(HeldBallMesh, LeftHeldBallMesh);
	// The mannequin is still what animates and what the balls attach to; it just is not drawn.
	GetMesh()->SetHiddenInGame(true);
	GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	UpdateToonCharacter();
}

void AChaosImpactCharacter::UpdateToonCharacter()
{
	if (!bUseToonCharacter || GetNetMode() == NM_DedicatedServer || !GetWorld())
	{
		return;
	}
	const AChaosImpactPlayerState* State = GetPlayerState<AChaosImpactPlayerState>();
	// The picked character can arrive after the pawn (a remote player's choice replicates in a moment later).
	if (State && ToonCharacter && ToonCharacter->GetCharacterIndex() != ShownCharacterIndex(State))
	{
		ToonCharacter->DestroyComponent();
		ToonCharacter = nullptr;
		GetMesh()->SetHiddenInGame(false);
		CreateToonCharacter();
	}
	if (!ToonCharacter)
	{
		return;
	}
	// A team battle shows the team's colour, so the sides read at a glance. Otherwise players wear the colour they
	// picked; CPUs take the colours nobody picked, and a player with no pick yet falls back to their slot.
	int32 Colour = 0;
	const AChaosImpactGameState* Match = GetWorld()->GetGameState<AChaosImpactGameState>();
	if (State && Match && Match->IsTeamBattle() && State->TeamIndex >= 0)
	{
		Colour = State->TeamIndex;
	}
	else if (CPUNumber > 0)
	{
		TArray<int32, TInlineAllocator<4>> Free{0, 1, 2, 3};
		if (Match)
		{
			for (const APlayerState* Member : Match->PlayerArray)
			{
				const AChaosImpactPlayerState* Human = Cast<AChaosImpactPlayerState>(Member);
				if (Human && !Human->IsABot() && Human->ColourChoice >= 0)
				{
					Free.Remove(Human->ColourChoice);
				}
			}
		}
		Colour = Free.IsEmpty() ? CPUNumber % 4 : Free[(CPUNumber - 1) % Free.Num()];
	}
	else if (State && State->ColourChoice >= 0)
	{
		Colour = State->ColourChoice;
	}
	else if (State && Match && Match->bOnlineRoom)
	{
		Colour = FMath::Abs(State->JoinOrder) % 4;
	}
	else if (const APlayerController* OwningController = Cast<APlayerController>(GetController());
		OwningController && OwningController->GetLocalPlayer() && GetGameInstance())
	{
		Colour = FMath::Max(GetGameInstance()->GetLocalPlayers().IndexOfByKey(OwningController->GetLocalPlayer()), 0) % 4;
	}
	ToonCharacter->SetColourIndex(Colour);
	// From the throw until just after release the thrown ball is in the mannequin's hand, so the model's hand is
	// put exactly there; the rest of the time the arm keeps its own proportions.
	const float SinceThrow = static_cast<float>(GetWorld()->GetTimeSeconds() - ThrowAnimationStartedAt);
	const float Release = ThrowReleaseDelaySeconds + 0.05f;
	const float Weight = bThrowAnimationActive
		? FMath::Clamp(SinceThrow / 0.08f, 0.0f, 1.0f) * (1.0f - FMath::Clamp((SinceThrow - Release) / 0.15f, 0.0f, 1.0f))
		: 0.0f;
	ToonCharacter->SetRightHandExactWeight(Weight);
	// Both arms go up over the head to hold a nova being charged, or a big snowball being lifted to throw.
	float ChargeSeconds = 0.0f;
	const bool bCharging = !bThrowReleasePending && CarriedBallCount > 0 && GetPresentedCharge(ChargeSeconds);
	const EChaosImpactBallType Held = CarriedBallCount > 0 ? GetCarriedBallType(0) : EChaosImpactBallType::Normal;
	const bool bRaise = bCharging && !bEliminated && (Held == EChaosImpactBallType::Nova
		|| (Held == EChaosImpactBallType::Snow && ChaosImpactBallTypes::IsSnowOverhead(ChaosImpactBallTypes::GetSnowScale(GetSnowGrowth(0)))));
	ArmsRaisedWeight = FMath::FInterpTo(ArmsRaisedWeight, bRaise ? 1.0f : 0.0f, GetWorld()->GetDeltaSeconds(), 10.0f);
	ToonCharacter->SetArmsRaised(ArmsRaisedWeight * (1.0f - Weight));
}

bool AChaosImpactCharacter::IsShowingLastHitSmoke() const
{
	return bLastHitSmokeOn && LastHitSmoke != nullptr;
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

float AChaosImpactCharacter::GetSnowGrowth(const int32 Slot) const
{
	if (Slot < 0 || Slot > 1)
	{
		return 0.0f;
	}
	// Kept per slot for whatever ball is there (0 for anything but a snowball), so shifting slots keeps it right.
	return HasAuthority() ? SnowGrowthExact[Slot] : (Slot == 0 ? SnowGrowthRight : SnowGrowthLeft) / 255.0f;
}

void AChaosImpactCharacter::SetSlotSnowGrowth(const int32 Slot, const float Growth)
{
	if (Slot < 0 || Slot > 1)
	{
		return;
	}
	const float Clamped = FMath::Clamp(Growth, 0.0f, 1.0f);
	SnowGrowthExact[Slot] = Clamped;
	(Slot == 0 ? SnowGrowthRight : SnowGrowthLeft) = static_cast<uint8>(FMath::RoundToInt(Clamped * 255.0f));
}

void AChaosImpactCharacter::UpdateSnowball()
{
	const int32 Slots = FMath::Clamp(CarriedBallCount, 0, 2);
	if (HasAuthority())
	{
		const FVector Here = GetActorLocation();
		const float Moved = bSnowWalkTracked ? static_cast<float>(FVector::Dist2D(Here, LastSnowWalkLocation)) : 0.0f;
		LastSnowWalkLocation = Here;
		bSnowWalkTracked = true;
		// Walking rolls it bigger; a respawn or a warp is no walk.
		if (Moved > 0.0f && Moved < 400.0f && !bEliminated)
		{
			for (int32 Slot = 0; Slot < Slots; ++Slot)
			{
				if (GetCarriedBallType(Slot) == EChaosImpactBallType::Snow)
				{
					SetSlotSnowGrowth(Slot, GetSnowGrowth(Slot) + Moved / ChaosImpactBallTypes::SnowGrowDistance);
				}
			}
		}
	}
	// A big snowball is heavy: its carrier walks a little slower (wherever this character is moved).
	float Heaviest = 0.0f;
	for (int32 Slot = 0; Slot < Slots; ++Slot)
	{
		if (GetCarriedBallType(Slot) == EChaosImpactBallType::Snow)
		{
			Heaviest = FMath::Max(Heaviest, GetSnowGrowth(Slot));
		}
	}
	if (BaseMaxWalkSpeed > 0.0f)
	{
		// Charging a nova roots its thrower to the spot.
		GetCharacterMovement()->MaxWalkSpeed = IsChargingNova() ? 0.0f
			: BaseMaxWalkSpeed * (1.0f - ChaosImpactBallTypes::SnowMaxSlowdown * Heaviest);
	}
}

void AChaosImpactCharacter::UpdateSnowRollPresentation()
{
	// A snowball carried in the left hand grows in the hand as it grows (up to a hand's worth).
	if (LeftHeldBallMesh && CarriedBallCount > 1 && GetCarriedBallType(1) == EChaosImpactBallType::Snow)
	{
		LeftHeldBallMesh->SetRelativeScale3D(FVector(0.34f * FMath::Min(ChaosImpactBallTypes::GetSnowScale(GetSnowGrowth(1)), 1.5f)));
	}
	// The right-hand snowball is rolled along the ground in front, growing as it goes. Charging a throw, a small one
	// is picked up into the hand and thrown like any ball; a big one is lifted up over the head to be hurled.
	const bool bRightSnow = CarriedBallCount > 0 && GetCarriedBallType(0) == EChaosImpactBallType::Snow;
	const float RightScale = bRightSnow ? ChaosImpactBallTypes::GetSnowScale(GetSnowGrowth(0)) : 1.0f;
	const bool bOverhead = bRightSnow && ChaosImpactBallTypes::IsSnowOverhead(RightScale);
	float ChargeSeconds = 0.0f;
	const bool bCharging = bRightSnow && GetPresentedCharge(ChargeSeconds);
	const bool bInHand = bCharging && !bOverhead;
	if (HeldBallMesh && bRightSnow && !bThrowReleasePending && !bEliminated)
	{
		HeldBallMesh->SetRelativeScale3D(FVector(0.34f * RightScale));
		HeldBallMesh->SetVisibility(bInHand, true);
	}
	const float DeltaSeconds = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.0f;
	const bool bShown = !bEliminated && bRightSnow && !bInHand && !bThrowReleasePending && !IsHidden();
	if (!bShown)
	{
		if (SnowRollMesh)
		{
			SnowRollMesh->SetVisibility(false, true);
		}
		SnowLift = 0.0f;
		return;
	}
	const bool bJustShown = !SnowRollMesh || !SnowRollMesh->IsVisible();
	if (!SnowRollMesh)
	{
		UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		if (!Sphere)
		{
			return;
		}
		if (!HeldSnowMaterial)
		{
			HeldSnowMaterial = ChaosImpactBallTypes::MakeSnow(this);
		}
		SnowRollMesh = NewObject<UStaticMeshComponent>(this);
		SnowRollMesh->SetStaticMesh(Sphere);
		SnowRollMesh->SetMaterial(0, HeldSnowMaterial);
		SnowRollMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		SnowRollMesh->SetGenerateOverlapEvents(false);
		SnowRollMesh->SetupAttachment(GetRootComponent());
		SnowRollMesh->SetUsingAbsoluteLocation(true);
		SnowRollMesh->SetUsingAbsoluteRotation(true);
		SnowRollMesh->SetUsingAbsoluteScale(true);
		SnowRollMesh->RegisterComponent();
		ChaosImpactBallTypes::AttachSnowLumps(this, SnowRollMesh, HeldSnowMaterial, static_cast<int32>(GetUniqueID()));
	}
	const float Radius = 24.0f * RightScale;
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	const FVector Here = GetPresentationLocation();
	const FVector Forward = GetActorForwardVector().GetSafeNormal2D();
	// On the ground just in front of the feet, pushed along.
	const FVector Rolled = Here + Forward * (Capsule->GetScaledCapsuleRadius() + Radius + 28.0f)
		+ FVector(0.0f, 0.0f, Radius - Capsule->GetScaledCapsuleHalfHeight());
	const FVector Held = Here + GetOverheadHoldOffset(EChaosImpactBallType::Snow, RightScale);
	// Lifted in about a third of a second, swinging up and over; set down again if the throw is called off.
	SnowLift = FMath::FInterpConstantTo(SnowLift, bCharging && bOverhead ? 1.0f : 0.0f, DeltaSeconds, 3.2f);
	const float Lift = FMath::SmoothStep(0.0f, 1.0f, SnowLift);
	const FVector Center = FMath::Lerp(Rolled, Held, Lift) + FVector(0.0f, 0.0f, FMath::Sin(Lift * UE_PI) * Radius * 0.35f);
	if (bJustShown)
	{
		LastSnowRollLocation = Center;
	}
	const FVector Moved = Center - LastSnowRollLocation;
	LastSnowRollLocation = Center;
	const float Travel = static_cast<float>(FVector(Moved.X, Moved.Y, 0.0f).Size());
	if (Lift < 0.02f && Travel > 0.05f && Travel < 300.0f)
	{
		// Rolls without slipping: turned about the axis across its way, by the distance over its radius.
		const FVector Axis = FVector::CrossProduct(FVector::UpVector, FVector(Moved.X, Moved.Y, 0.0f) / Travel);
		SnowRollSpin = FQuat(Axis, Travel / FMath::Max(Radius, 1.0f)) * SnowRollSpin;
	}
	else if (Lift > 0.5f)
	{
		// Held up, it turns slowly.
		SnowRollSpin = FQuat(FVector::UpVector, DeltaSeconds * 0.9f) * SnowRollSpin;
	}
	SnowRollSpin.Normalize();
	SnowRollMesh->SetWorldLocationAndRotation(Center, SnowRollSpin);
	SnowRollMesh->SetWorldScale3D(FVector(0.48f * RightScale));
	SnowRollMesh->SetVisibility(true, true);
}

void AChaosImpactCharacter::DropCarriedBalls()
{
	UWorld* World = GetWorld();
	if (!HasAuthority() || !World || !BallClass)
	{
		return;
	}
	const int32 Count = FMath::Clamp(CarriedBallCount, 0, 2);
	for (int32 Slot = 0; Slot < Count; ++Slot)
	{
		const FVector Out = FRotator(0.0f, FMath::FRandRange(0.0f, 360.0f), 0.0f).Vector();
		const FTransform SpawnTransform(FRotator::ZeroRotator, GetActorLocation() + Out * 40.0f);
		AChaosImpactBall* Ball = World->SpawnActorDeferred<AChaosImpactBall>(BallClass, SpawnTransform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Ball)
		{
			continue;
		}
		Ball->SetBallType(GetCarriedBallType(Slot));
		Ball->FinishSpawning(SpawnTransform);
		// Tumbles a little way off, then lies there like any landed ball (and goes if nobody takes it).
		Ball->MakeRollingPickup(Out * 700.0f);
	}
	if (Count > 0)
	{
		UE_LOG(LogChaosImpact, Log, TEXT("%s dropped %d ball(s) on going down"), *GetName(), Count);
	}
}

void AChaosImpactCharacter::RequestCancelThrow()
{
	if (!bIsChargingThrow)
	{
		return;
	}
	CancelChargingThrow();
	if (!HasAuthority())
	{
		ServerCancelCharge();
	}
}

void AChaosImpactCharacter::ServerCancelCharge_Implementation()
{
	if (bIsChargingThrow)
	{
		CancelChargingThrow();
	}
}

void AChaosImpactCharacter::ApplyBlind(const float Seconds)
{
	if (!HasAuthority() || bEliminated || Seconds <= 0.0f)
	{
		return;
	}
	BlindedUntilServerTime = FMath::Max(BlindedUntilServerTime, GetSharedServerTime() + Seconds);
}

bool AChaosImpactCharacter::IsBlinded() const
{
	return BlindedUntilServerTime > 0.0 && GetSharedServerTime() < BlindedUntilServerTime;
}

float AChaosImpactCharacter::GetBlindAmount() const
{
	// Going down clears the smoke from the view at once (also the moment this screen sees its own knockout).
	if (!IsBlinded() || bEliminated || IsEliminationPredicted())
	{
		return 0.0f;
	}
	return FMath::Clamp(static_cast<float>(BlindedUntilServerTime - GetSharedServerTime()), 0.0f, 1.0f);
}

bool AChaosImpactCharacter::GetPendingThrow(AChaosImpactBall*& OutBall, FVector& OutDirection, float& OutSpeed,
	float& OutUpSpeed, bool& bOutArc, float& OutSecondsLeft) const
{
	OutBall = PendingThrowBall.Get();
	if (!bThrowReleasePending || !OutBall)
	{
		return false;
	}
	OutDirection = PendingThrowDirection;
	OutSpeed = PendingThrowSpeed;
	OutUpSpeed = PendingThrowArcUpwardSpeed;
	bOutArc = PendingThrowFlightMode == EChaosImpactBallFlightMode::Arc;
	OutSecondsLeft = GetWorldTimerManager().IsTimerActive(ThrowReleaseTimer)
		? GetWorldTimerManager().GetTimerRemaining(ThrowReleaseTimer) : 0.0f;
	return true;
}

float AChaosImpactCharacter::GetDashRemainingSeconds() const
{
	return bIsDashing ? FMath::Max(0.0f, DashDuration - DashElapsedSeconds) : 0.0f;
}

float AChaosImpactCharacter::GetDashReadyInSeconds() const
{
	if (!GetWorld() || bEliminated || IsIceFrozen() || IsChargingNova())
	{
		// Frozen or rooted: not before that is over (charging a nova can be called off, which takes a moment).
		return IsChargingNova() ? 0.35f : 10.0f;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	float Ready = FMath::Max(0.0f, NextDashAvailableAtSeconds - Now);
	if (bIsDashing)
	{
		Ready = FMath::Max(Ready, GetDashRemainingSeconds() + DashCooldownSeconds);
	}
	if (Stamina + UE_SMALL_NUMBER < DashCost)
	{
		Ready = FMath::Max(Ready, StaminaRegenPerSecond > 0.0f ? (DashCost - Stamina) / StaminaRegenPerSecond : 100.0f);
	}
	return Ready;
}

float AChaosImpactCharacter::GetChargeSecondsFor(const EChaosImpactBallType Type) const
{
	return Type == EChaosImpactBallType::Nova ? ChaosImpactBallTypes::NovaChargeSeconds : MaxChargeSeconds;
}

bool AChaosImpactCharacter::GetPresentedCharge(float& OutSeconds) const
{
	OutSeconds = 0.0f;
	if (!GetWorld() || bEliminated)
	{
		return false;
	}
	if (IsLocallyControlled() || HasAuthority())
	{
		if (!bIsChargingThrow)
		{
			return false;
		}
		OutSeconds = FMath::Max(0.0f, static_cast<float>(GetWorld()->GetTimeSeconds() - ThrowChargeStartedAt));
		return true;
	}
	if (ChargeStartServerTime < 0.0f)
	{
		return false;
	}
	OutSeconds = FMath::Max(0.0f, static_cast<float>(GetSharedServerTime() - ChargeStartServerTime));
	return true;
}

bool AChaosImpactCharacter::IsChargingNova() const
{
	float Seconds = 0.0f;
	return CarriedBallCount > 0 && GetCarriedBallType(0) == EChaosImpactBallType::Nova && GetPresentedCharge(Seconds);
}

void AChaosImpactCharacter::GetThrowFlight(const float ChargeAlpha, const EChaosImpactBallType Type, const float Scale,
	float& OutHorizontalSpeed, float& OutUpSpeed, EChaosImpactBallFlightMode& OutMode, bool& bOutOverhead) const
{
	const float ThrowSpeed = FMath::Lerp(MinimumThrowSpeed, MaximumThrowSpeed, FMath::Clamp(ChargeAlpha, 0.0f, 1.0f));
	const AChaosImpactPlayerController* PlayerController = Cast<AChaosImpactPlayerController>(GetController());
	const AChaosImpactCPUController* CPUController = Cast<AChaosImpactCPUController>(GetController());
	OutMode = PlayerController
		? PlayerController->GetBallFlightMode()
		: CPUController && !CPUController->UsesArcFlightMode()
			? EChaosImpactBallFlightMode::Straight : EChaosImpactBallFlightMode::Arc;
	OutHorizontalSpeed = ThrowSpeed;
	OutUpSpeed = 0.0f;
	bOutOverhead = false;
	if (OutMode == EChaosImpactBallFlightMode::Arc)
	{
		// Top-down throw: leave the hand level and let gravity create only the downward arc.
		OutHorizontalSpeed = ThrowSpeed * ArcThrowSpeedScale;
	}
	if (Type == EChaosImpactBallType::Snow && ChaosImpactBallTypes::IsSnowOverhead(Scale))
	{
		// A big snowball is hurled from over the head: it falls onto whoever is ahead rather than sailing over them.
		OutHorizontalSpeed = ThrowSpeed * ArcThrowSpeedScale;
		OutUpSpeed = ChaosImpactBallTypes::SnowThrowUpSpeed;
		OutMode = EChaosImpactBallFlightMode::Arc;
		bOutOverhead = true;
	}
	else if (Type == EChaosImpactBallType::Nova)
	{
		// A nova is heaved slowly forward from over the head, however it was charged.
		OutHorizontalSpeed = ChaosImpactBallTypes::NovaThrowSpeed;
		OutUpSpeed = ChaosImpactBallTypes::NovaThrowUpSpeed;
		OutMode = EChaosImpactBallFlightMode::Arc;
		bOutOverhead = true;
	}
}

FVector AChaosImpactCharacter::GetOverheadHoldOffset(const EChaosImpactBallType Type, const float Scale) const
{
	const float HalfHeight = GetCapsuleComponent() ? GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 96.0f;
	const float Radius = 24.0f * Scale;
	return FVector(0.0f, 0.0f, Type == EChaosImpactBallType::Nova
		? HalfHeight + ChaosImpactBallTypes::NovaHoldGap + Radius : HalfHeight + Radius * 0.85f);
}

bool AChaosImpactCharacter::PredictThrowLanding(const float ChargeAlpha, FVector& OutGround, float& OutRadius) const
{
	UWorld* World = GetWorld();
	if (!World || CarriedBallCount <= 0)
	{
		return false;
	}
	const EChaosImpactBallType Type = GetCarriedBallType(0);
	const float Scale = Type == EChaosImpactBallType::Nova ? ChaosImpactBallTypes::GetNovaScale(ChargeAlpha)
		: Type == EChaosImpactBallType::Snow ? ChaosImpactBallTypes::GetSnowScale(GetSnowGrowth(0)) : 1.0f;
	float Horizontal = 0.0f;
	float Upward = 0.0f;
	EChaosImpactBallFlightMode Mode = EChaosImpactBallFlightMode::Arc;
	bool bOverhead = false;
	GetThrowFlight(ChargeAlpha, Type, Scale, Horizontal, Upward, Mode, bOverhead);
	// The thrower aims with its own direction; everyone else sees it turned that way.
	const FVector Direction = (IsLocallyControlled() || HasAuthority() ? AimDirection : GetActorForwardVector()).GetSafeNormal2D();
	if (Direction.IsNearlyZero())
	{
		return false;
	}
	const FVector Here = GetPresentationLocation();
	FVector Position = bOverhead ? Here + GetOverheadHoldOffset(Type, Scale)
		: Here + Direction * ThrowSocketOffset.X + GetActorRightVector() * ThrowSocketOffset.Y + FVector::UpVector * ThrowSocketOffset.Z;
	const bool bArc = Mode == EChaosImpactBallFlightMode::Arc;
	FVector Velocity = Direction * Horizontal + FVector(0.0f, 0.0f, bArc ? Upward : 0.0f);
	const float Gravity = bArc ? World->GetGravityZ() * (Type == EChaosImpactBallType::Nova ? ChaosImpactBallTypes::NovaGravityScale : 1.0f) : 0.0f;
	const float Radius = 24.0f * Scale;
	// The same flight the ball will make, stepped until it first touches the stage itself (a nova meets nothing else).
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ChaosImpactThrowPreview), false, this);
	const FCollisionObjectQueryParams Stage(ECC_WorldStatic);
	const FCollisionShape Shape = FCollisionShape::MakeSphere(Radius);
	constexpr float Step = 1.0f / 30.0f;
	FVector Landing = FVector::ZeroVector;
	bool bLands = false;
	for (float Time = 0.0f; Time < 4.0f; Time += Step)
	{
		const FVector Next = Position + Velocity * Step + FVector(0.0f, 0.0f, 0.5f * Gravity * Step * Step);
		Velocity.Z += Gravity * Step;
		FHitResult Hit;
		if (World->SweepSingleByObjectType(Hit, Position, Next, FQuat::Identity, Stage, Shape, Params))
		{
			Landing = Hit.bStartPenetrating ? Position : Hit.Location;
			bLands = true;
			break;
		}
		Position = Next;
	}
	FHitResult Ground;
	if (!bLands || !World->LineTraceSingleByObjectType(Ground, Landing + FVector(0.0f, 0.0f, 10.0f),
		Landing - FVector(0.0f, 0.0f, 3000.0f), Stage, Params))
	{
		return false;
	}
	OutGround = Ground.ImpactPoint;
	OutRadius = Type == EChaosImpactBallType::Nova ? ChaosImpactBallTypes::GetNovaBlastRadius(Scale) : FMath::Max(Radius, 45.0f);
	return true;
}

void AChaosImpactCharacter::UpdateLandingPreview(const float DeltaSeconds)
{
	float Seconds = 0.0f;
	const bool bCharging = CarriedBallCount > 0 && !bThrowReleasePending && !bTrainingMenuFrozen && !IsHidden()
		&& GetPresentedCharge(Seconds);
	const EChaosImpactBallType Type = CarriedBallCount > 0 ? GetCarriedBallType(0) : EChaosImpactBallType::Normal;
	// Drawn on the ground for everyone to see: a warning of what is coming.
	const bool bNova = bCharging && Type == EChaosImpactBallType::Nova;
	FVector Ground = FVector::ZeroVector;
	float Radius = 0.0f;
	const float Alpha = FMath::Clamp(Seconds / FMath::Max(GetChargeSecondsFor(Type), 0.01f), 0.0f, 1.0f);
	if (!bNova || !PredictThrowLanding(Alpha, Ground, Radius))
	{
		if (LandingRing)
		{
			LandingRing->SetVisibility(false);
		}
		if (LandingFill)
		{
			LandingFill->SetVisibility(false);
		}
		LandingPreviewRadius = 0.0f;
		return;
	}
	using namespace ChaosImpactBallTypes;
	if (!LandingRing)
	{
		LandingRingMaterial = MakeAdditive(this, FLinearColor::White, 0.0f);
		LandingRing = ChaosImpactLightning::CreateComponent(this, GetRootComponent(), LandingRingMaterial);
		LandingFillMaterial = MakeAdditive(this, FLinearColor::White, 0.0f);
		LandingFill = NewObject<UStaticMeshComponent>(this);
		LandingFill->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")));
		LandingFill->SetMaterial(0, LandingFillMaterial);
		LandingFill->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		LandingFill->SetGenerateOverlapEvents(false);
		LandingFill->SetCastShadow(false);
		LandingFill->SetupAttachment(GetRootComponent());
		LandingFill->RegisterComponent();
		for (USceneComponent* Part : {static_cast<USceneComponent*>(LandingRing), static_cast<USceneComponent*>(LandingFill)})
		{
			if (Part)
			{
				Part->SetUsingAbsoluteLocation(true);
				Part->SetUsingAbsoluteRotation(true);
				Part->SetUsingAbsoluteScale(true);
			}
		}
		LandingPreviewType = EChaosImpactBallType::Normal;
	}
	if (!LandingRing || !LandingFill)
	{
		return;
	}
	if (LandingPreviewType != Type)
	{
		// A warning in hot orange.
		LandingPreviewType = Type;
		const FLinearColor Edge(1.0f, 0.42f, 0.1f);
		const FLinearColor Fill(1.0f, 0.3f, 0.06f);
		LandingRingMaterial->SetVectorParameterValue(TEXT("Color"), Edge);
		LandingRingMaterial->SetVectorParameterValue(TEXT("Base Color"), Edge);
		LandingFillMaterial->SetVectorParameterValue(TEXT("Color"), Fill);
		LandingFillMaterial->SetVectorParameterValue(TEXT("Base Color"), Fill);
		LandingPreviewRadius = 0.0f;
	}
	LandingPreviewRadius = LandingPreviewRadius <= 0.0f ? Radius : FMath::FInterpTo(LandingPreviewRadius, Radius, DeltaSeconds, 14.0f);
	LandingPreviewGround = Ground;
	if (IsLocallyControlled())
	{
		// This player's camera moves over toward where it will land, so the area is in view.
		FVector Lead = Ground - GetPresentationLocation();
		Lead.Z = 0.0f;
		NovaCameraLead = FMath::VInterpTo(NovaCameraLead, (Lead * 0.3f).GetClampedToMaxSize(900.0f), DeltaSeconds, 2.5f);
	}
	const float Area = LandingPreviewRadius;
	const float Time = static_cast<float>(GetWorld()->GetTimeSeconds());
	// An outer edge, rings closing in on the spot over and over, and a small ring at its centre.
	ChaosImpactIceMeshes::FMeshBuffers Rings;
	const float Width = FMath::Clamp(Area * 0.03f, 5.0f, 30.0f);
	ChaosImpactLightning::AppendRing(Rings, FVector::ZeroVector, Area, Width, 96);
	for (int32 Pulse = 0; Pulse < 2; ++Pulse)
	{
		const float Phase = FMath::Frac(Time * 0.9f + Pulse * 0.5f);
		ChaosImpactLightning::AppendRing(Rings, FVector::ZeroVector, Area * (1.0f - 0.85f * Phase), Width * 0.55f, 72);
	}
	ChaosImpactLightning::AppendRing(Rings, FVector::ZeroVector, FMath::Max(Area * 0.07f, 12.0f), Width * 0.8f, 24);
	ChaosImpactLightning::SetMesh(LandingRing, Rings);
	LandingRing->SetWorldLocationAndRotation(Ground + FVector(0.0f, 0.0f, 5.0f), FRotator::ZeroRotator);
	LandingRing->SetWorldScale3D(FVector::OneVector);
	LandingRing->SetVisibility(true);
	LandingRingMaterial->SetScalarParameterValue(TEXT("Intensity"), 3.0f * (0.8f + 0.2f * FMath::Sin(Time * 9.0f)));
	LandingFill->SetWorldLocationAndRotation(Ground + FVector(0.0f, 0.0f, 3.0f), FRotator::ZeroRotator);
	LandingFill->SetWorldScale3D(FVector(Area * 2.0f / 100.0f, Area * 2.0f / 100.0f, 0.02f));
	LandingFill->SetVisibility(true);
	// A full nova's area throbs brighter.
	LandingFillMaterial->SetScalarParameterValue(TEXT("Intensity"), (0.08f + 0.08f * Alpha)
		* (0.8f + 0.2f * FMath::Sin(Time * (Alpha >= 1.0f ? 14.0f : 6.0f))));
}

void AChaosImpactCharacter::UpdateNovaChargePresentation(const float DeltaSeconds)
{
	float Seconds = 0.0f;
	const bool bCharging = !bEliminated && !IsHidden() && !bThrowReleasePending && CarriedBallCount > 0
		&& GetCarriedBallType(0) == EChaosImpactBallType::Nova && GetPresentedCharge(Seconds);
	if (!bCharging)
	{
		if (bNovaChargeShown)
		{
			bNovaChargeShown = false;
			ChaosImpactBallTypes::SetNovaLookVisible(HeldNovaLook, false);
			for (FNovaMote& Mote : NovaMotes)
			{
				if (UStaticMeshComponent* MoteMesh = Mote.Mesh.Get())
				{
					MoteMesh->SetVisibility(false);
				}
				Mote.bFlying = false;
			}
			if (NovaAura)
			{
				NovaAura->Deactivate();
			}
			// Called off with the nova still in hand: back into the hand (a throw shows its own ball instead).
			if (!bThrowReleasePending)
			{
				UpdateBallPresentation();
			}
		}
		// After a throw the camera stays out while the nova flies and bursts; otherwise (called off, knocked out) it
		// comes straight back in.
		if (!GetWorld() || GetWorld()->GetTimeSeconds() >= NovaCameraHoldUntil)
		{
			NovaCameraExtra = FMath::FInterpTo(NovaCameraExtra, 0.0f, DeltaSeconds, 3.0f);
			NovaCameraLead = FMath::VInterpTo(NovaCameraLead, FVector::ZeroVector, DeltaSeconds, 3.0f);
			if (NovaCameraExtra < 1.0f && NovaCameraLead.SizeSquared() < 1.0f)
			{
				NovaCameraExtra = 0.0f;
				NovaCameraLead = FVector::ZeroVector;
			}
		}
		return;
	}
	using namespace ChaosImpactBallTypes;
	const float Alpha = FMath::Clamp(Seconds / NovaChargeSeconds, 0.0f, 1.0f);
	const float Scale = GetNovaScale(Alpha);
	const float Radius = 24.0f * Scale;
	const double Now = GetWorld()->GetTimeSeconds();
	if (!NovaAnchor)
	{
		NovaAnchor = NewObject<USceneComponent>(this);
		NovaAnchor->SetupAttachment(GetRootComponent());
		NovaAnchor->SetUsingAbsoluteLocation(true);
		NovaAnchor->RegisterComponent();
		BuildNovaLook(this, NovaAnchor, HeldNovaLook);
		UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		NovaMoteMaterial = MakeAdditive(this, FLinearColor(0.4f, 0.78f, 1.0f), 1.5f);
		constexpr int32 MoteCount = 56;
		for (int32 Index = 0; Index < MoteCount && Sphere; ++Index)
		{
			UStaticMeshComponent* MoteMesh = NewObject<UStaticMeshComponent>(this);
			MoteMesh->SetStaticMesh(Sphere);
			MoteMesh->SetMaterial(0, NovaMoteMaterial);
			MoteMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			MoteMesh->SetGenerateOverlapEvents(false);
			MoteMesh->SetCastShadow(false);
			MoteMesh->SetupAttachment(GetRootComponent());
			MoteMesh->SetUsingAbsoluteLocation(true);
			MoteMesh->SetUsingAbsoluteRotation(true);
			MoteMesh->SetUsingAbsoluteScale(true);
			MoteMesh->SetVisibility(false);
			MoteMesh->RegisterComponent();
			NovaMotes.AddDefaulted_GetRef().Mesh = MoteMesh;
		}
		if (UNiagaraSystem* Aura = LoadEffect(Effects::WarpAura))
		{
			NovaAura = UNiagaraFunctionLibrary::SpawnSystemAttached(Aura, GetRootComponent(), NAME_None,
				FVector(0.0f, 0.0f, -GetCapsuleComponent()->GetScaledCapsuleHalfHeight()), FRotator::ZeroRotator,
				EAttachLocation::KeepRelativeOffset, false);
			if (NovaAura)
			{
				NovaAura->SetAllowScalability(false);
				NovaAura->SetRelativeScale3D(FVector(1.6f));
			}
		}
	}
	if (!bNovaChargeShown)
	{
		bNovaChargeShown = true;
		if (HeldBallMesh)
		{
			HeldBallMesh->SetVisibility(false, true);
		}
		if (NovaAura)
		{
			NovaAura->Activate(true);
		}
	}
	const FVector Here = GetPresentationLocation();
	const FVector Center = Here + GetOverheadHoldOffset(EChaosImpactBallType::Nova, Scale)
		+ FVector(0.0f, 0.0f, 6.0f * FMath::Sin(static_cast<float>(Now) * 2.2f));
	NovaAnchor->SetWorldLocation(Center);
	SetNovaLookVisible(HeldNovaLook, true);
	// Brighter as it fills; fully charged it throbs.
	const float Throb = Alpha >= 1.0f ? 0.25f * FMath::Sin(static_cast<float>(Now) * 12.0f) : 0.0f;
	UpdateNovaLook(HeldNovaLook, Radius, static_cast<float>(Now), 0.75f + 0.45f * Alpha + Throb);

	// Energy streams in from all around (the ground, the air), faster and thicker the longer it is charged.
	const float Ground = Here.Z - GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const int32 ActiveMotes = Alpha >= 1.0f ? NovaMotes.Num() : FMath::RoundToInt(14.0f + (NovaMotes.Num() - 14.0f) * Alpha);
	for (int32 Index = 0; Index < NovaMotes.Num(); ++Index)
	{
		FNovaMote& Mote = NovaMotes[Index];
		UStaticMeshComponent* MoteMesh = Mote.Mesh.Get();
		if (!MoteMesh)
		{
			continue;
		}
		float T = Mote.bFlying ? static_cast<float>((Now - Mote.StartedAt) / Mote.Seconds) : 1.0f;
		if (T >= 1.0f)
		{
			if (Index >= ActiveMotes)
			{
				Mote.bFlying = false;
				MoteMesh->SetVisibility(false);
				continue;
			}
			// A new stream from somewhere around: mostly level and low, some from above.
			const float Angle = FMath::FRandRange(0.0f, UE_TWO_PI);
			const float Rise = FMath::FRandRange(-0.25f, 0.7f);
			const FVector Out = FVector(FMath::Cos(Angle), FMath::Sin(Angle), Rise).GetSafeNormal();
			Mote.Start = Center + Out * (Radius * 1.3f + FMath::FRandRange(700.0f, 2100.0f));
			Mote.Start.Z = FMath::Max(Mote.Start.Z, Ground + 20.0f);
			Mote.Seconds = FMath::FRandRange(0.55f, 1.0f) * (1.15f - 0.45f * Alpha);
			// The first ones set off already on their way, so the flow is there from the start.
			Mote.StartedAt = Mote.bFlying ? Now : Now - FMath::FRandRange(0.0f, Mote.Seconds);
			Mote.bFlying = true;
			T = static_cast<float>((Now - Mote.StartedAt) / Mote.Seconds);
		}
		// Accelerating inward, stretching into a streak as it goes.
		const FVector Toward = Center - Mote.Start;
		const float Along = T * T;
		const FVector Direction = Toward.GetSafeNormal();
		const float Length = 40.0f + 320.0f * T;
		const float Thick = 9.0f + 7.0f * Alpha;
		MoteMesh->SetWorldLocationAndRotation(Mote.Start + Toward * Along - Direction * Length * 0.5f, Direction.Rotation());
		MoteMesh->SetWorldScale3D(FVector(Length / 100.0f, Thick / 100.0f, Thick / 100.0f));
		MoteMesh->SetVisibility(Along < 0.97f);
	}
	if (NovaMoteMaterial)
	{
		NovaMoteMaterial->SetScalarParameterValue(TEXT("Intensity"), 1.2f + 1.3f * Alpha);
	}
	// This player's own camera draws back to fit the swelling nova in, and over toward where it will land.
	if (IsLocallyControlled())
	{
		NovaCameraExtra = FMath::FInterpTo(NovaCameraExtra, Radius * 2.6f + 400.0f, DeltaSeconds, 3.0f);
	}
}

void AChaosImpactCharacter::ApplyBlastKnockback(const FVector& Velocity)
{
	if (bEliminated)
	{
		return;
	}
	if (IsLocallyControlled())
	{
		LaunchCharacter(Velocity, true, true);
		PlayControllerRumble(0.8f, 1.0f, 0.4f);
	}
	else if (HasAuthority())
	{
		// A remote player moves themselves.
		ClientBlastKnockback(Velocity);
	}
}

void AChaosImpactCharacter::ClientBlastKnockback_Implementation(FVector_NetQuantize Velocity)
{
	if (!bEliminated)
	{
		LaunchCharacter(Velocity, true, true);
		PlayControllerRumble(0.8f, 1.0f, 0.4f);
	}
}

void AChaosImpactCharacter::AddCameraShake(const float Strength, const float Seconds)
{
	if (!IsLocallyControlled() || !GetWorld() || Strength <= 0.01f)
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	const float Remaining = CameraShakeStrength * FMath::Clamp(1.0f - static_cast<float>(Now - CameraShakeStartedAt) / CameraShakeSeconds, 0.0f, 1.0f);
	if (Strength >= Remaining)
	{
		CameraShakeStrength = Strength;
		CameraShakeSeconds = FMath::Max(Seconds, 0.1f);
		CameraShakeStartedAt = Now;
	}
	PlayControllerRumble(0.6f * Strength, Strength, 0.5f);
}

void AChaosImpactCharacter::UpdateCameraShake()
{
	if (!FollowCamera || !GetWorld())
	{
		return;
	}
	const float Left = 1.0f - static_cast<float>(GetWorld()->GetTimeSeconds() - CameraShakeStartedAt) / CameraShakeSeconds;
	if (Left <= 0.0f)
	{
		if (bCameraShaking)
		{
			bCameraShaking = false;
			FollowCamera->SetRelativeLocation(CameraRestLocation);
		}
		return;
	}
	if (!bCameraShaking)
	{
		bCameraShaking = true;
		CameraRestLocation = FollowCamera->GetRelativeLocation();
	}
	const float Amount = 55.0f * CameraShakeStrength * Left * Left;
	FollowCamera->SetRelativeLocation(CameraRestLocation
		+ FVector(0.0f, FMath::FRandRange(-1.0f, 1.0f) * Amount, FMath::FRandRange(-1.0f, 1.0f) * Amount));
}
