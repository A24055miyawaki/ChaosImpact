// Copyright Epic Games, Inc. All Rights Reserved.


#include "ChaosImpactPlayerController.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactBallSpawner.h"
#include "ChaosImpactMenuWidget.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerInput.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "Blueprint/UserWidget.h"
#include "ChaosImpact.h"
#include "Widgets/Input/SVirtualJoystick.h"
#include "InputCoreTypes.h"

void AChaosImpactPlayerController::BeginPlay()
{
	Super::BeginPlay();
	bTrainingMode = GetWorld() && GetWorld()->URL.HasOption(TEXT("CITraining=1"));
	BallFlightMode = GetWorld() && GetWorld()->URL.HasOption(TEXT("CIBallArc=1"))
		? EChaosImpactBallFlightMode::Arc : EChaosImpactBallFlightMode::Straight;

	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;
	DefaultMouseCursor = EMouseCursor::Crosshairs;

	// only spawn touch controls on local player controllers
	if (ShouldUseTouchControls() && IsLocalPlayerController())
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

	if (IsLocalPlayerController())
	{
		MenuWidget = CreateWidget<UChaosImpactMenuWidget>(this);
		if (MenuWidget)
		{
			MenuWidget->AddToPlayerScreen(100);
			CurrentScreen = bTrainingMode ? EChaosImpactScreen::Playing : EChaosImpactScreen::Title;
			MenuWidget->ShowScreen(CurrentScreen);
			ApplyScreenInput();
		}
	}

	EnsureTrainingBallSpawners();
}

void AChaosImpactPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	InputComponent->BindKey(EKeys::P, IE_Pressed, this,
		&AChaosImpactPlayerController::TogglePauseMenu).bExecuteWhenPaused = true;
	InputComponent->BindKey(EKeys::Gamepad_Special_Right, IE_Pressed, this,
		&AChaosImpactPlayerController::TogglePauseMenu).bExecuteWhenPaused = true;
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this,
		&AChaosImpactPlayerController::HandleThrowPressed);
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Released, this,
		&AChaosImpactPlayerController::HandleThrowReleased);

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
	bShowMouseCursor = true;
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
	if (NewScreen == EChaosImpactScreen::Pause && !IsGameplayActive())
	{
		return;
	}
	CurrentScreen = NewScreen;
	MenuWidget->ShowScreen(NewScreen);
	ApplyScreenInput();
}

void AChaosImpactPlayerController::TogglePauseMenu()
{
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
	if (bTravelPending || IsGameplayActive() || CurrentScreen == EChaosImpactScreen::Title)
	{
		return;
	}
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
	const bool bOpenWithArc = bKeepFlightMode && BallFlightMode == EChaosImpactBallFlightMode::Arc;
	const FString Options = bOpenWithArc ? TEXT("CITraining=1?CIBallArc=1") : TEXT("CITraining=1");
	// Reloading clears balls, actors, health, stamina and position.
	UGameplayStatics::OpenLevel(this, FName(*MapPackage), true, Options);
}

void AChaosImpactPlayerController::EnsureTrainingBallSpawners()
{
	if (!bTrainingMode || !HasAuthority() || !GetWorld() || !GetPawn()
		|| !TrainingBallSpawners.IsEmpty())
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
