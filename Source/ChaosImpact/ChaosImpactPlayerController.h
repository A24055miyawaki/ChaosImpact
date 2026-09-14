// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactScreen.h"
#include "ChaosImpactPlayerController.generated.h"

class UInputMappingContext;
class UUserWidget;
class UChaosImpactMenuWidget;
class AChaosImpactBallSpawner;
class AChaosImpactTrainingTarget;
class AChaosImpactTrainingArena;

/**
 *  Basic PlayerController class for a third person game
 *  Manages input mappings
 */
UCLASS(abstract)
class AChaosImpactPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category="Chaos Impact|Menu")
	void ShowMenuScreen(EChaosImpactScreen NewScreen);
	UFUNCTION(BlueprintCallable, Category="Chaos Impact|Menu")
	void StartTraining();
	UFUNCTION(BlueprintCallable, Category="Chaos Impact|Menu")
	void StartTrainingWithPlayers(int32 LocalPlayerCount);
	UFUNCTION(BlueprintCallable, Category="Chaos Impact|Training")
	void PrepareTrainingControllerAssignment(int32 LocalPlayerCount);
	UFUNCTION(BlueprintCallable, Category="Chaos Impact|Training")
	void ConfirmControllerAssignments();
	/** Called by the viewport before normal routing so an unassigned pad can join. */
	bool RegisterControllerJoin(int32 InputDeviceId, int32 LegacyControllerId);
	/** Registers the single keyboard/mouse pair into the next open player slot. */
	bool RegisterKeyboardMouseJoin();
	UFUNCTION(BlueprintCallable, Category="Chaos Impact|Menu")
	void ResumeGameplay();
	UFUNCTION(BlueprintCallable, Category="Chaos Impact|Training")
	void RetryTraining();
	UFUNCTION(BlueprintCallable, Category="Chaos Impact|Training")
	void CycleTrainingPlayerCount();
	UFUNCTION(BlueprintCallable, Category="Chaos Impact|Training")
	void ToggleTrainingTargets();
	UFUNCTION(BlueprintCallable, Category="Chaos Impact|Training")
	void ToggleTrainingCPU();
	UFUNCTION(BlueprintCallable, Category="Chaos Impact|Training")
	void TogglePrimaryInputMode();
	UFUNCTION(BlueprintCallable, Category="Chaos Impact|Training")
	void ApplyTrainingSettings();
	/** Opens/closes the live training panel. Bound to T/- and controller Minus/Create. */
	UFUNCTION(BlueprintCallable, Category="Chaos Impact|Training")
	void ToggleTrainingOverlay();
	UFUNCTION(BlueprintCallable, Category="Chaos Impact|Training")
	void CloseTrainingOverlay();
	UFUNCTION(BlueprintCallable, Category="Chaos Impact|Ball")
	void ToggleBallFlightMode();
	void TogglePauseMenu();
	bool IsGameplayActive() const { return CurrentScreen == EChaosImpactScreen::Playing && !bTravelPending; }
	bool IsTrainingMode() const { return bTrainingMode; }
	bool IsTrainingOverlayOpen() const { return CurrentScreen == EChaosImpactScreen::TrainingOverlay; }
	bool IsPrimaryLocalPlayerController() const;
	int32 GetRequestedLocalPlayerCount() const { return RequestedLocalPlayerCount; }
	bool AreTrainingTargetsEnabled() const { return bTrainingTargetsEnabled; }
	bool IsTrainingCPUEnabled() const { return TrainingCPUCount > 0; }
	int32 GetTrainingCPUCount() const { return TrainingCPUCount; }
	int32 GetJoinedControllerCount() const { return JoinedInputDeviceIds.Num(); }
	bool IsControllerJoined(int32 InputDeviceId) const
	{
		return JoinedInputDeviceIds.Contains(InputDeviceId);
	}
	bool IsKeyboardMouseJoined() const { return RequestedKeyboardPlayerIndex != INDEX_NONE; }
	bool IsKeyboardMouseAssignedToPlayer(int32 PlayerIndex) const
	{
		return RequestedKeyboardPlayerIndex == PlayerIndex;
	}
	bool IsInputAssignedToPlayer(int32 PlayerIndex) const;
	int32 GetAssignedPlayerCount() const
	{
		return JoinedInputDeviceIds.Num() + (IsKeyboardMouseJoined() ? 1 : 0);
	}
	bool AreControllerAssignmentsComplete() const;
	/** Active input mode. Changes requested in the pause menu take effect after Apply. */
	bool IsPrimaryUsingGamepad() const { return bPrimaryUsesGamepad; }
	bool WillPrimaryUseGamepad() const { return bRequestedPrimaryUsesGamepad; }
	/** True when this local player's assigned gameplay device is a controller. */
	bool IsUsingGamepad() const;
	/** Human-readable automatic device assignment for the current requested setup. */
	FString GetLocalInputAssignmentText(int32 PlayerCount = INDEX_NONE) const;
	FString GetLocalInputAssignmentForPlayer(int32 PlayerIndex) const;
	EChaosImpactScreen GetControllerAssignmentReturnScreen() const
	{
		return ControllerAssignmentReturnScreen;
	}
	EChaosImpactBallFlightMode GetBallFlightMode() const { return BallFlightMode; }
	EChaosImpactScreen GetCurrentScreen() const { return CurrentScreen; }
	UChaosImpactMenuWidget* GetMenuWidget() const { return MenuWidget; }
	virtual bool InputKey(const FInputKeyEventArgs& Params) override;
	
protected:
	UPROPERTY(Transient)
	TObjectPtr<UChaosImpactMenuWidget> MenuWidget;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Chaos Impact|Menu")
	EChaosImpactScreen CurrentScreen = EChaosImpactScreen::Title;

	/** The existing prototype map is the training level. */
	UPROPERTY(EditDefaultsOnly, Category="Chaos Impact|Menu", meta=(AllowedClasses="/Script/Engine.World"))
	FSoftObjectPath TrainingLevel = FSoftObjectPath(TEXT("/Game/ThirdPerson/Lvl_ThirdPerson.Lvl_ThirdPerson"));

	bool bTravelPending = false;
	bool bTrainingMode = false;
	int32 RequestedLocalPlayerCount = 1;
	bool bTrainingTargetsEnabled = true;
	int32 TrainingCPUCount = 0;
	bool bPrimaryUsesGamepad = false;
	bool bRequestedPrimaryUsesGamepad = false;
	/** Active and pending slot occupied by the one local keyboard/mouse pair. */
	int32 ActiveKeyboardPlayerIndex = 0;
	int32 RequestedKeyboardPlayerIndex = 0;
	bool bControllerAssignmentKeepsFlightMode = false;
	EChaosImpactScreen ControllerAssignmentReturnScreen = EChaosImpactScreen::TrainingSetup;
	/** Physical device ids, stored in the exact order their join button was pressed. */
	TArray<int32> JoinedInputDeviceIds;
	TArray<int32> JoinedLegacyControllerIds;
	bool bTrainingOverlayPresentationActive = false;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Chaos Impact|Ball")
	EChaosImpactBallFlightMode BallFlightMode = EChaosImpactBallFlightMode::Arc;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AChaosImpactBallSpawner>> TrainingBallSpawners;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AChaosImpactTrainingTarget>> TrainingTargets;

	UPROPERTY(Transient)
	TObjectPtr<AChaosImpactTrainingArena> TrainingArena;

	virtual void OnPossess(APawn* InPawn) override;
	void ApplyScreenInput();
	void ApplyLocalInputRouting();
	bool IsKeyAllowedForThisPlayer(FKey Key) const;
	void HandleThrowPressed();
	void HandleThrowReleased();
	void EnsureTrainingArena();
	void EnsureTrainingBallSpawners();
	void EnsureTrainingTargets();
	void OpenTrainingLevel(bool bKeepFlightMode);
	void ResetControllerJoinSequence();
	void BuildFallbackControllerAssignments();
	int32 GetPadIndexForPlayer(int32 PlayerIndex) const;
	int32 GetThisLocalPlayerIndex() const;
	void RemoveSecondaryLocalPlayers();
	void OpenTrainingOverlay();
	void ExitTrainingOverlayPresentation();
	void SetTrainingCharactersFrozen(bool bFrozen);

	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, Category ="Input|Input Mappings")
	TArray<UInputMappingContext*> DefaultMappingContexts;

	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, Category="Input|Input Mappings")
	TArray<UInputMappingContext*> MobileExcludedMappingContexts;

	/** Mobile controls widget to spawn */
	UPROPERTY(EditAnywhere, Category="Input|Touch Controls")
	TSubclassOf<UUserWidget> MobileControlsWidgetClass;

	/** Pointer to the mobile controls widget */
	UPROPERTY()
	TObjectPtr<UUserWidget> MobileControlsWidget;

	/** If true, the player will use UMG touch controls even if not playing on mobile platforms */
	UPROPERTY(EditAnywhere, Config, Category = "Input|Touch Controls")
	bool bForceTouchControls = false;

	/** Gameplay initialization */
	virtual void BeginPlay() override;

	/** Input mapping context setup */
	virtual void SetupInputComponent() override;

	/** Returns true if the player should use UMG touch controls */
	bool ShouldUseTouchControls() const;

};
