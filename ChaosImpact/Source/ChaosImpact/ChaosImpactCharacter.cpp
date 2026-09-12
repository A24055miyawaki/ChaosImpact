// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosImpactCharacter.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactChargeWidget.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactPlayerController.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
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
#include "DrawDebugHelpers.h"
#include "UObject/ConstructorHelpers.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "ChaosImpact.h"

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

	BallClass = AChaosImpactBall::StaticClass();

	// Note: The skeletal mesh and anim blueprint references on the Mesh component (inherited from Character) 
	// are set in the derived blueprint asset named ThirdPersonCharacter (to avoid direct content references in C++)
}

void AChaosImpactCharacter::BeginPlay()
{
	Super::BeginPlay();

	Health = MaxHealth;
	Stamina = MaxStamina;
	CarriedBallCount = 0;
	NextDashAvailableAtSeconds = 0.0f;
	InitialSpawnLocation = GetActorLocation();
	InitialSpawnRotation = GetActorRotation();
	AimDirection = GetActorForwardVector().GetSafeNormal2D();
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
}

void AChaosImpactCharacter::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bEliminationEffectActive)
	{
		UpdateEliminationEffect(DeltaSeconds);
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
		const bool bCanUseMouse = !MenuController || MenuController->IsPrimaryLocalPlayerController();
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

	if (!bEliminated)
	{
		UpdateAim(DeltaSeconds);

		if (bIsDashing)
		{
			UpdateDash(DeltaSeconds);
		}
		else
		{
			Stamina = FMath::Min(MaxStamina, Stamina + StaminaRegenPerSecond * DeltaSeconds);
		}
	}

	if (ChargeWidget)
	{
		ChargeWidget->SetStamina(Stamina, MaxStamina);
	}

	if (bIsChargingThrow)
	{
		const float ChargeAlpha = GetThrowChargeAlpha();
		OnThrowChargeChanged(ChargeAlpha);
		if (ChargeWidget)
		{
			ChargeWidget->SetChargeAlpha(ChargeAlpha);
		}

		const FVector GuideStart = GetActorLocation() + FVector::UpVector * ThrowSocketOffset.Z;
		const FVector GuideEnd = GuideStart + AimDirection * AimGuideLength;
		DrawDebugDirectionalArrow(GetWorld(), GuideStart, GuideEnd, 55.0f,
			FColor(70, 210, 255), false, 0.0f, 0, 8.0f);
		DrawDebugDirectionalArrow(GetWorld(), GuideStart, GuideEnd, 45.0f,
			FColor::White, false, 0.0f, 0, 2.0f);
	}
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
		ChargeWidget->SetStamina(Stamina, MaxStamina);
		ChargeWidget->SetBallInventory(CarriedBallCount, MaximumCarriedBalls);
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
	PlayerInputComponent->BindKey(EKeys::G, IE_Pressed, this, &AChaosImpactCharacter::StartChargingThrow);
	PlayerInputComponent->BindKey(EKeys::G, IE_Released, this, &AChaosImpactCharacter::ReleaseChargedThrow);
	PlayerInputComponent->BindKey(EKeys::Gamepad_FaceButton_Top, IE_Pressed, this, &AChaosImpactCharacter::StartChargingThrow);
	PlayerInputComponent->BindKey(EKeys::Gamepad_FaceButton_Top, IE_Released, this, &AChaosImpactCharacter::ReleaseChargedThrow);
	PlayerInputComponent->BindKey(EKeys::LeftShift, IE_Pressed, this, &AChaosImpactCharacter::StartDash);
	PlayerInputComponent->BindKey(EKeys::RightShift, IE_Pressed, this, &AChaosImpactCharacter::StartDash);
	PlayerInputComponent->BindKey(EKeys::Gamepad_FaceButton_Right, IE_Pressed, this, &AChaosImpactCharacter::StartDash);
}

void AChaosImpactCharacter::Move(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D MovementVector = Value.Get<FVector2D>();

	// route the input
	DoMove(MovementVector.X, MovementVector.Y);
}

void AChaosImpactCharacter::AimWithStick(const FInputActionValue& Value)
{
	StickAimInput = Value.Get<FVector2D>();
}

void AChaosImpactCharacter::StopAimingWithStick(const FInputActionValue& Value)
{
	StickAimInput = FVector2D::ZeroVector;
}

void AChaosImpactCharacter::DoMove(float Right, float Forward)
{
	const AChaosImpactPlayerController* MenuController = Cast<AChaosImpactPlayerController>(GetController());
	if (bEliminated || (MenuController && !MenuController->IsGameplayActive()))
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
	if (bEliminated || bIsDashing || bIsChargingThrow || CarriedBallCount <= 0 || !GetWorld())
	{
		return;
	}

	bIsChargingThrow = true;
	ThrowChargeStartedAt = GetWorld()->GetTimeSeconds();
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
	if (!GetWorld() || bEliminated || bIsDashing || Stamina + UE_SMALL_NUMBER < DashCost
		|| GetWorld()->GetTimeSeconds() < NextDashAvailableAtSeconds)
	{
		return;
	}

	DashDirection = LastMoveDirection.GetSafeNormal2D();
	if (const APlayerController* PlayerController = Cast<APlayerController>(GetController()))
	{
		const float KeyboardForward =
			(PlayerController->IsInputKeyDown(EKeys::W) ? 1.0f : 0.0f)
			- (PlayerController->IsInputKeyDown(EKeys::S) ? 1.0f : 0.0f);
		const float KeyboardRight =
			(PlayerController->IsInputKeyDown(EKeys::D) ? 1.0f : 0.0f)
			- (PlayerController->IsInputKeyDown(EKeys::A) ? 1.0f : 0.0f);
		const float ForwardInput = KeyboardForward
			+ PlayerController->GetInputAnalogKeyState(EKeys::Gamepad_LeftY);
		const float RightInput = KeyboardRight
			+ PlayerController->GetInputAnalogKeyState(EKeys::Gamepad_LeftX);

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
	AddActorWorldOffset(DashDirection * StepDistance, true, &DashHit);
	DashDistanceApplied = TargetDistance;

	const FVector Center = GetActorLocation() + FVector::UpVector * 48.0f;
	const FVector Side = FVector::CrossProduct(FVector::UpVector, DashDirection).GetSafeNormal();
	for (int32 LineIndex = -2; LineIndex <= 2; ++LineIndex)
	{
		const FVector LineOffset = Side * (LineIndex * 22.0f) + FVector::UpVector * (LineIndex * 5.0f);
		const FVector LineStart = Center + LineOffset - DashDirection * 45.0f;
		const FVector LineEnd = LineStart - DashDirection * (150.0f + FMath::Abs(LineIndex) * 20.0f);
		DrawDebugLine(GetWorld(), LineStart, LineEnd, FColor(40, 220, 255), false, 0.09f, 0, 3.0f);
	}

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
	DashElapsedSeconds = 0.0f;
	DashDistanceApplied = 0.0f;
	if (GetWorld())
	{
		NextDashAvailableAtSeconds = GetWorld()->GetTimeSeconds() + DashCooldownSeconds;
	}
	GetCharacterMovement()->SetMovementMode(bWasFallingBeforeDash ? MOVE_Falling : MOVE_Walking);
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
	SpawnBall(ChargeAlpha);
}

void AChaosImpactCharacter::UpdateAim(float DeltaSeconds)
{
	FVector DesiredDirection = AimDirection;
	if (StickAimInput.Size() >= StickAimDeadZone)
	{
		const FRotator CameraYaw(0.0f, CameraBoom->GetComponentRotation().Yaw, 0.0f);
		const FVector CameraForward = FRotationMatrix(CameraYaw).GetUnitAxis(EAxis::X);
		const FVector CameraRight = FRotationMatrix(CameraYaw).GetUnitAxis(EAxis::Y);
		DesiredDirection = (CameraForward * StickAimInput.Y + CameraRight * StickAimInput.X).GetSafeNormal2D();
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
		MenuController && !MenuController->IsPrimaryLocalPlayerController())
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
	if (!GetWorld() || !BallClass || AimDirection.IsNearlyZero() || CarriedBallCount <= 0)
	{
		return false;
	}

	const FVector SpawnLocation = GetActorLocation()
		+ AimDirection * ThrowSocketOffset.X
		+ GetActorRightVector() * ThrowSocketOffset.Y
		+ FVector::UpVector * ThrowSocketOffset.Z;
	const FTransform SpawnTransform(AimDirection.Rotation(), SpawnLocation);
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.Instigator = this;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	if (AChaosImpactBall* Ball = GetWorld()->SpawnActor<AChaosImpactBall>(BallClass, SpawnTransform, SpawnParameters))
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
		Ball->Launch(AimDirection.GetSafeNormal2D(), HorizontalThrowSpeed,
			FlightMode, ArcUpwardSpeed);
		CarriedBallCount = FMath::Max(0, CarriedBallCount - 1);
		UpdateBallPresentation();
		return true;
	}
	return false;
}

float AChaosImpactCharacter::TakeDamage(const float DamageAmount, const FDamageEvent& DamageEvent,
	AController* EventInstigator, AActor* DamageCauser)
{
	if (const AChaosImpactBall* Ball = Cast<AChaosImpactBall>(DamageCauser);
		Ball && Ball->WasThrownBy(this))
	{
		return 0.0f;
	}
	if (bEliminated || DamageAmount <= 0.0f)
	{
		return 0.0f;
	}
	if (bIsDashing)
	{
		return 0.0f;
	}

	const float AppliedDamage = FMath::Min(Health, DamageAmount);
	Health = FMath::Clamp(Health - AppliedDamage, 0.0f, MaxHealth);
	OnPlayerHit(Health, AppliedDamage);

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 1.5f, FColor::Red,
			FString::Printf(TEXT("HIT  Health: %.0f / %.0f"), Health, MaxHealth));
	}

	if (Health <= 0.0f)
	{
		bEliminated = true;
		bIsChargingThrow = false;
		if (ChargeWidget)
		{
			ChargeWidget->SetCharging(false);
		}
		GetCharacterMovement()->DisableMovement();
		SetActorEnableCollision(false);
		StartEliminationEffect();
		OnPlayerEliminated();

		FTimerHandle ResetTimer;
		GetWorldTimerManager().SetTimer(ResetTimer, this,
			&AChaosImpactCharacter::ResetAfterElimination, EliminationResetDelay, false);
	}

	return AppliedDamage;
}

void AChaosImpactCharacter::ResetAfterElimination()
{
	StopEliminationEffect();
	SetActorLocationAndRotation(InitialSpawnLocation, InitialSpawnRotation, false, nullptr,
		ETeleportType::TeleportPhysics);
	Health = MaxHealth;
	Stamina = MaxStamina;
	CarriedBallCount = 0;
	bIsDashing = false;
	DashElapsedSeconds = 0.0f;
	DashDistanceApplied = 0.0f;
	NextDashAvailableAtSeconds = 0.0f;
	bEliminated = false;
	SetActorEnableCollision(true);
	GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	UpdateBallPresentation();
}

void AChaosImpactCharacter::StartEliminationEffect()
{
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
	GetMesh()->SetVisibility(true, true);
}

void AChaosImpactCharacter::SetGameplayUIVisible(const bool bVisible)
{
	TryCreateChargeWidget();
	if (ChargeWidget)
	{
		ChargeWidget->SetVisibility(bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
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
}

void AChaosImpactCharacter::SetTrainingStartTransform(
	const FVector& Location, const FRotator& Rotation)
{
	SetActorLocationAndRotation(Location, Rotation, false, nullptr,
		ETeleportType::TeleportPhysics);
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
	if (!IsValid(Ball) || !Ball->IsPickupAvailable() || bEliminated
		|| CarriedBallCount >= MaximumCarriedBalls)
	{
		return false;
	}

	++CarriedBallCount;
	UpdateBallPresentation();
	return true;
}

void AChaosImpactCharacter::UpdateBallPresentation()
{
	if (HeldBallMesh)
	{
		HeldBallMesh->SetRelativeLocation(HeldBallRelativeLocation);
		HeldBallMesh->SetRelativeRotation(HeldBallRelativeRotation);
		HeldBallMesh->SetVisibility(CarriedBallCount > 0, true);
	}
	if (LeftHeldBallMesh)
	{
		LeftHeldBallMesh->SetRelativeLocation(LeftHeldBallRelativeLocation);
		LeftHeldBallMesh->SetRelativeRotation(LeftHeldBallRelativeRotation);
		LeftHeldBallMesh->SetVisibility(CarriedBallCount > 1, true);
	}
	if (ChargeWidget)
	{
		ChargeWidget->SetBallInventory(CarriedBallCount, MaximumCarriedBalls);
	}
}
