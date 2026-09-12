// Copyright Epic Games, Inc. All Rights Reserved.


#include "ChaosImpactPlayerController.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactBallSpawner.h"
#include "ChaosImpactTrainingTarget.h"
#include "ChaosImpactMenuWidget.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerInput.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "Blueprint/UserWidget.h"
#include "ChaosImpact.h"
#include "Widgets/Input/SVirtualJoystick.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"

void AChaosImpactPlayerController::BeginPlay()
{
	Super::BeginPlay();
	bTrainingMode = GetWorld() && GetWorld()->URL.HasOption(TEXT("CITraining=1"));
	if (bTrainingMode)
	{
		RequestedLocalPlayerCount = FMath::Clamp(FCString::Atoi(
			GetWorld()->URL.GetOption(TEXT("CILocalPlayers="), TEXT("1"))), 1, 4);
		bTrainingTargetsEnabled = !GetWorld()->URL.HasOption(TEXT("CITargets=0"));
		bTrainingCPUEnabled = GetWorld()->URL.HasOption(TEXT("CICPU=1"));
	}
	BallFlightMode = GetWorld() && GetWorld()->URL.HasOption(TEXT("CIBallStraight=1"))
		? EChaosImpactBallFlightMode::Straight : EChaosImpactBallFlightMode::Arc;
	CurrentScreen = bTrainingMode ? EChaosImpactScreen::Playing : EChaosImpactScreen::Title;
	const bool bPrimaryLocalPlayer = IsPrimaryLocalPlayerController();

	bShowMouseCursor = bPrimaryLocalPlayer;
	bEnableClickEvents = bPrimaryLocalPlayer;
	bEnableMouseOverEvents = bPrimaryLocalPlayer;
	DefaultMouseCursor = EMouseCursor::Crosshairs;

	// only spawn touch controls on local player controllers
	if (ShouldUseTouchControls() && IsLocalPlayerController() && bPrimaryLocalPlayer)
	{
		// spawn the mobile controls widget
		MobileControlsWidget = CreateWidget<UUserWidget>(this, MobileControlsWidgetClass);

		if (MobileControlsWidget)
		{
			// add the controls to the player screen
			MobileControlsWidget->AddToPlayerScreen(0);

		} else {

			UE_LOG(LogChaosImpact, Error, TEXT("Could not spawn mobile controls widget."));

		}

	}

	if (IsLocalPlayerController() && bPrimaryLocalPlayer)
	{
		MenuWidget = CreateWidget<UChaosImpactMenuWidget>(this);
		if (MenuWidget)
		{
			// Menus use the complete game viewport. Gameplay HUDs still use
			// AddToPlayerScreen, so only the pause layer spans split-screen views.
			MenuWidget->AddToViewport(100);
			MenuWidget->ShowScreen(CurrentScreen);
			ApplyScreenInput();
		}
	}

	EnsureTrainingBallSpawners();
	EnsureTrainingTargets();
}

bool AChaosImpactPlayerController::IsPrimaryLocalPlayerController() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	const ULocalPlayer* ThisLocalPlayer = GetLocalPlayer();
	if (!ThisLocalPlayer)
	{
		return false;
	}
	return !GameInstance || GameInstance->GetLocalPlayers().IsEmpty()
		|| GameInstance->GetLocalPlayers()[0] == ThisLocalPlayer;
}

void AChaosImpactPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	InputComponent->BindKey(EKeys::P, IE_Pressed, this,
		&AChaosImpactPlayerController::TogglePauseMenu).bExecuteWhenPaused = true;
	InputComponent->BindKey(EKeys::Gamepad_Special_Right, IE_Pressed, this,
		&AChaosImpactPlayerController::TogglePauseMenu).bExecuteWhenPaused = true;

	// only add IMCs for local player controllers
	if (IsLocalPlayerController())
	{
		// Add Input Mapping Contexts
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			// Always add the template contexts as fallbacks. This avoids a completely
			// unresponsive pawn when a Blueprint loses its context array assignments.
			if (UInputMappingContext* DefaultContext = LoadObject<UInputMappingContext>(
				nullptr, TEXT("/Game/Input/IMC_Default.IMC_Default")))
			{
				Subsystem->AddMappingContext(DefaultContext, 0);
			}
			if (UInputMappingContext* MouseContext = LoadObject<UInputMappingContext>(
				nullptr, TEXT("/Game/Input/IMC_MouseLook.IMC_MouseLook")))
			{
				Subsystem->AddMappingContext(MouseContext, 0);
			}

			for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
			{
				Subsystem->AddMappingContext(CurrentContext, 0);
			}

			// only add these IMCs if we're not using mobile touch input
			if (!ShouldUseTouchControls())
			{
				for (UInputMappingContext* CurrentContext : MobileExcludedMappingContexts)
				{
					Subsystem->AddMappingContext(CurrentContext, 0);
				}
			}
		}
	}
}

bool AChaosImpactPlayerController::InputKey(const FInputKeyEventArgs& Params)
{
	const bool bHandledByBase = Super::InputKey(Params);
	if (Params.Key == EKeys::LeftMouseButton && IsGameplayActive())
	{
		if (Params.Event == IE_Pressed)
		{
			HandleThrowPressed();
			return true;
		}
		if (Params.Event == IE_Released)
		{
			HandleThrowReleased();
			return true;
		}
	}
	return bHandledByBase;
}

void AChaosImpactPlayerController::HandleThrowPressed()
{
	if (IsGameplayActive())
	{
		if (AChaosImpactCharacter* PlayerCharacter = Cast<AChaosImpactCharacter>(GetPawn()))
		{
			PlayerCharacter->BeginThrowInput();
		}
	}
}

void AChaosImpactPlayerController::HandleThrowReleased()
{
	if (IsGameplayActive())
	{
		if (AChaosImpactCharacter* PlayerCharacter = Cast<AChaosImpactCharacter>(GetPawn()))
		{
			PlayerCharacter->EndThrowInput();
		}
	}
}

bool AChaosImpactPlayerController::ShouldUseTouchControls() const
{
	// are we on a mobile platform? Should we force touch?
	return SVirtualJoystick::ShouldDisplayTouchInterface() || bForceTouchControls;
}

void AChaosImpactPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	if (IsLocalPlayerController() && MenuWidget)
	{
		ApplyScreenInput();
	}
	EnsureTrainingBallSpawners();
	EnsureTrainingTargets();
}

void AChaosImpactPlayerController::ApplyScreenInput()
{
	const bool bPlaying = IsGameplayActive();
	if (AChaosImpactCharacter* PlayerCharacter = Cast<AChaosImpactCharacter>(GetPawn()))
	{
		PlayerCharacter->SetGameplayUIVisible(bPlaying);
		if (!bPlaying)
		{
			PlayerCharacter->CancelChargingThrow();
		}
	}
	if (PlayerInput)
	{
		PlayerInput->FlushPressedKeys();
	}
	bShowMouseCursor = IsPrimaryLocalPlayerController();
	DefaultMouseCursor = bPlaying ? EMouseCursor::Crosshairs : EMouseCursor::Default;
	CurrentMouseCursor = DefaultMouseCursor;
	SetPause(!bPlaying);
	if (MobileControlsWidget)
	{
		MobileControlsWidget->SetVisibility(bPlaying ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (bPlaying)
	{
		FInputModeGameOnly GameInputMode;
		// Do not sacrifice the first throw click when the viewport captures the mouse again.
		GameInputMode.SetConsumeCaptureMouseDown(false);
		SetInputMode(GameInputMode);
	}
	else if (MenuWidget)
	{
		FInputModeUIOnly InputMode;
		InputMode.SetWidgetToFocus(MenuWidget->TakeWidget());
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(InputMode);
		MenuWidget->SetUserFocus(this);
	}
}

void AChaosImpactPlayerController::ShowMenuScreen(const EChaosImpactScreen NewScreen)
{
	if (!MenuWidget || bTravelPending)
	{
		return;
	}
	if (NewScreen == EChaosImpactScreen::Playing)
	{
		ResumeGameplay();
		return;
	}
	// A pause screen is only reachable from an active play session.
	if (NewScreen == EChaosImpactScreen::Pause && !IsGameplayActive()
		&& CurrentScreen != EChaosImpactScreen::TrainingSettings)
	{
		return;
	}
	if (bTrainingMode && IsPrimaryLocalPlayerController()
		&& (NewScreen == EChaosImpactScreen::ModeSelect || NewScreen == EChaosImpactScreen::Title))
	{
		RemoveSecondaryLocalPlayers();
	}
	CurrentScreen = NewScreen;
	MenuWidget->ShowScreen(NewScreen);
	ApplyScreenInput();
}

void AChaosImpactPlayerController::TogglePauseMenu()
{
	if (CurrentScreen == EChaosImpactScreen::TrainingSettings)
	{
		ShowMenuScreen(EChaosImpactScreen::Pause);
		return;
	}
	if (CurrentScreen == EChaosImpactScreen::Pause)
	{
		ResumeGameplay();
	}
	else if (IsGameplayActive())
	{
		ShowMenuScreen(EChaosImpactScreen::Pause);
	}
}

void AChaosImpactPlayerController::ResumeGameplay()
{
	if (CurrentScreen != EChaosImpactScreen::Pause || bTravelPending)
	{
		return;
	}
	CurrentScreen = EChaosImpactScreen::Playing;
	MenuWidget->ShowScreen(CurrentScreen);
	ApplyScreenInput();
}

void AChaosImpactPlayerController::StartTraining()
{
	StartTrainingWithPlayers(1);
}

void AChaosImpactPlayerController::StartTrainingWithPlayers(const int32 LocalPlayerCount)
{
	if (bTravelPending || IsGameplayActive() || CurrentScreen == EChaosImpactScreen::Title)
	{
		return;
	}
	RequestedLocalPlayerCount = FMath::Clamp(LocalPlayerCount, 1, 4);
	OpenTrainingLevel(false);
}

void AChaosImpactPlayerController::RetryTraining()
{
	if (!bTrainingMode || bTravelPending)
	{
		return;
	}
	OpenTrainingLevel(true);
}

void AChaosImpactPlayerController::CycleTrainingPlayerCount()
{
	if (bTrainingMode)
	{
		RequestedLocalPlayerCount = RequestedLocalPlayerCount % 4 + 1;
	}
}

void AChaosImpactPlayerController::ToggleTrainingTargets()
{
	if (bTrainingMode)
	{
		bTrainingTargetsEnabled = !bTrainingTargetsEnabled;
	}
}

void AChaosImpactPlayerController::ToggleTrainingCPU()
{
	if (bTrainingMode)
	{
		bTrainingCPUEnabled = !bTrainingCPUEnabled;
	}
}

void AChaosImpactPlayerController::ApplyTrainingSettings()
{
	if (bTrainingMode && !bTravelPending)
	{
		OpenTrainingLevel(true);
	}
}

void AChaosImpactPlayerController::ToggleBallFlightMode()
{
	BallFlightMode = BallFlightMode == EChaosImpactBallFlightMode::Straight
		? EChaosImpactBallFlightMode::Arc : EChaosImpactBallFlightMode::Straight;
}

void AChaosImpactPlayerController::OpenTrainingLevel(const bool bKeepFlightMode)
{
	const FString MapPackage = TrainingLevel.GetLongPackageName();
	if (MapPackage.IsEmpty())
	{
		UE_LOG(LogChaosImpact, Error, TEXT("TrainingLevel must reference a map."));
		return;
	}
	bTravelPending = true;
	SetPause(false);
	const bool bOpenWithStraight = bKeepFlightMode
		&& BallFlightMode == EChaosImpactBallFlightMode::Straight;
	FString Options = FString::Printf(TEXT("CITraining=1?CILocalPlayers=%d"),
		FMath::Clamp(RequestedLocalPlayerCount, 1, 4));
	if (!bTrainingTargetsEnabled)
	{
		Options += TEXT("?CITargets=0");
	}
	if (bTrainingCPUEnabled)
	{
		Options += TEXT("?CICPU=1");
	}
	if (bOpenWithStraight)
	{
		Options += TEXT("?CIBallStraight=1");
	}
	// Reloading clears balls, actors, health, stamina and position.
	UGameplayStatics::OpenLevel(this, FName(*MapPackage), true, Options);
}

void AChaosImpactPlayerController::EnsureTrainingBallSpawners()
{
	if (!bTrainingMode || !IsPrimaryLocalPlayerController() || !HasAuthority() || !GetWorld() || !GetPawn()
		|| !TrainingBallSpawners.IsEmpty())
	{
		return;
	}
	for (TActorIterator<AChaosImpactBallSpawner> It(GetWorld()); It; ++It)
	{
		TrainingBallSpawners.Add(*It);
	}
	// Level-authored spawn points always win. Add/move these actors in the editor
	// to replace the fallback layout without changing code.
	if (!TrainingBallSpawners.IsEmpty())
	{
		return;
	}

	const FVector Origin = GetPawn()->GetActorLocation();
	const FVector Offsets[] =
	{
		FVector(430.0f, 0.0f, 0.0f),
		FVector(-330.0f, 370.0f, 0.0f),
		FVector(-330.0f, -370.0f, 0.0f)
	};
	for (const FVector& Offset : Offsets)
	{
		FVector SpawnerLocation = Origin + Offset - FVector::UpVector * 90.0f;
		FHitResult GroundHit;
		const FVector TraceStart = Origin + Offset + FVector::UpVector * 500.0f;
		const FVector TraceEnd = Origin + Offset - FVector::UpVector * 1600.0f;
		if (GetWorld()->LineTraceSingleByChannel(
			GroundHit, TraceStart, TraceEnd, ECC_Visibility))
		{
			SpawnerLocation = GroundHit.ImpactPoint + FVector::UpVector * 3.0f;
		}

		if (AChaosImpactBallSpawner* Spawner = GetWorld()->SpawnActor<AChaosImpactBallSpawner>(
			AChaosImpactBallSpawner::StaticClass(), SpawnerLocation, FRotator::ZeroRotator))
		{
			TrainingBallSpawners.Add(Spawner);
		}
	}
}

void AChaosImpactPlayerController::EnsureTrainingTargets()
{
	if (!bTrainingMode || !IsPrimaryLocalPlayerController() || !HasAuthority() || !GetWorld() || !GetPawn())
	{
		return;
	}
	if (!bTrainingTargetsEnabled)
	{
		for (TActorIterator<AChaosImpactTrainingTarget> It(GetWorld()); It; ++It)
		{
			It->Destroy();
		}
		TrainingTargets.Reset();
		return;
	}
	if (!TrainingTargets.IsEmpty())
	{
		return;
	}
	for (TActorIterator<AChaosImpactTrainingTarget> It(GetWorld()); It; ++It)
	{
		TrainingTargets.Add(*It);
	}
	// If the designer placed any targets in the map, use that authored set as-is.
	if (!TrainingTargets.IsEmpty())
	{
		return;
	}

	struct FTargetSetup
	{
		FVector Offset;
		EChaosImpactTargetMotion Motion;
		float Distance;
		float Speed;
		float Phase;
	};

	const FTargetSetup Setups[] =
	{
		{FVector(680.0f, 0.0f, 0.0f), EChaosImpactTargetMotion::Stationary, 0.0f, 0.3f, 0.0f},
		{FVector(420.0f, -620.0f, 0.0f), EChaosImpactTargetMotion::Stationary, 0.0f, 0.3f, 0.0f},
		{FVector(420.0f, 620.0f, 0.0f), EChaosImpactTargetMotion::SideToSide, 120.0f, 0.28f, 0.0f},
		{FVector(-100.0f, -700.0f, 0.0f), EChaosImpactTargetMotion::SideToSide, 110.0f, 0.38f, 0.35f},
		{FVector(-100.0f, 700.0f, 0.0f), EChaosImpactTargetMotion::ForwardBack, 120.0f, 0.34f, 0.65f}
	};

	const FVector Origin = GetPawn()->GetActorLocation();
	for (const FTargetSetup& Setup : Setups)
	{
		FVector TargetLocation = Origin + Setup.Offset - FVector::UpVector * 90.0f;
		FHitResult GroundHit;
		const FVector TraceStart = Origin + Setup.Offset + FVector::UpVector * 700.0f;
		const FVector TraceEnd = Origin + Setup.Offset - FVector::UpVector * 1800.0f;
		if (GetWorld()->LineTraceSingleByChannel(
			GroundHit, TraceStart, TraceEnd, ECC_Visibility))
		{
			TargetLocation = GroundHit.ImpactPoint + FVector::UpVector * 2.0f;
		}

		const FRotator FacingRotation = (Origin - TargetLocation).Rotation();
		FActorSpawnParameters Parameters;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (AChaosImpactTrainingTarget* Target = GetWorld()->SpawnActor<AChaosImpactTrainingTarget>(
			AChaosImpactTrainingTarget::StaticClass(), TargetLocation,
			FRotator(0.0f, FacingRotation.Yaw, 0.0f), Parameters))
		{
			Target->ConfigureMotion(Setup.Motion, Setup.Distance, Setup.Speed, Setup.Phase);
			TrainingTargets.Add(Target);
		}
	}
}

void AChaosImpactPlayerController::RemoveSecondaryLocalPlayers()
{
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		return;
	}
	while (GameInstance->GetLocalPlayers().Num() > 1)
	{
		GameInstance->RemoveLocalPlayer(GameInstance->GetLocalPlayers().Last());
	}
	RequestedLocalPlayerCount = 1;
}
