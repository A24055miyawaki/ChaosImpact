// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactScreen.h"
#include "ChaosImpactMatchTypes.h"
#include "ChaosImpactPlayerController.generated.h"

class UInputMappingContext;
class UUserWidget;
class UChaosImpactMenuWidget;
class AChaosImpactBallSpawner;
class AChaosImpactTrainingTarget;
class AChaosImpactTrainingArena;
class ACameraActor;
class AChaosImpactWarpPad;

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
	/** Training panel: the ball type ボールを呼び出す places in front of this player. */
	void CycleTrainingSummonBallType();
	void SummonTrainingBall();
	EChaosImpactBallType GetTrainingSummonBallType() const { return TrainingSummonBallType; }
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
	/**
	 * Controller rumble for this player alone: Small drives the light high-frequency motors, Big the heavy ones
	 * (0-1). Nothing happens for a keyboard/mouse player or with rumble turned off in the pause menu.
	 */
	void PlayRumble(float Small, float Big, float Seconds, float DelaySeconds = 0.0f);
	/** A rumble held for as long as something lasts (a black hole's pull, a warp charging). Set it every frame. */
	void SetSustainedRumble(float Small, float Big);
	void StopRumble();
	static bool IsRumbleEnabled();
	void ToggleRumbleEnabled();
	/** True when this device's key belongs to this player (its assigned device, or any device online). */
	bool AcceptsInputKey(const FKey Key) const { return IsKeyAllowedForThisPlayer(Key); }
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

	// LAN multiplayer rooms
	/** Starts へやをつくる / へやをさがす; asks for a user name first if none was saved. */
	void BeginOnlineFlow(bool bCreateRoom);
	void BeginOnlineRename();
	/** Mode-select entries: they decide what the player-count and controller-assignment screens set up. */
	void BeginTrainingSetup();
	void BeginVersusLocal();
	void BeginVersusOnline();
	EChaosImpactPlayFlow GetPlayFlow() const { return PlayFlow; }
	void SubmitOnlineName(const FString& Name);
	void SubmitRoomPassword(const FString& Password);
	void CancelOnlineStatus();
	/** Host only: メンバー募集終了. Closes the room and starts the match countdown. */
	void CloseRecruitment();
	void LeaveOnlineRoom();
	void StopRoomSearch();
	bool IsOnlineRoom() const { return GetNetMode() != NM_Standalone; }
	bool IsOnlineRoomHost() const { return GetNetMode() == NM_ListenServer; }
	bool IsPendingCreateRoom() const { return bPendingCreateRoom; }
	bool IsSearchingForRoom() const;
	bool CanCloseRecruitment() const;
	/** Room name screen: names a new room, or renames this room from the lobby. */
	void SubmitRoomName(const FString& Name);
	void BeginRoomRename();
	void CancelRoomRename();
	bool IsRenamingRoom() const { return bRenamingRoom; }
	bool CanRenameRoom() const;
	bool CanReopenRecruitment() const;
	void ReopenRecruitment();
	/** Room list: join the listed room, search again, or go back to the password. */
	void JoinRoomListing(int32 Index);
	void RefreshRoomList();
	void CloseRoomList();
	/** Lobby: this player's 準備OK for the decided rules (R / D-pad up); pressing again cancels it. */
	void ToggleReadyForMatch();
	bool CanToggleReady() const;

	// VS match
	/** Rule screen: local VS before its level opens, or inside a room or match where the rules apply at once. */
	void OpenMatchRules(EChaosImpactScreen ReturnScreen);
	/** Row 0 = minutes, 1 = free-for-all / teams, 2 = CPUs. */
	void AdjustMatchRule(int32 Row, int32 Direction);
	const FChaosImpactMatchRules& GetPendingMatchRules() const { return PendingMatchRules; }
	int32 GetMatchHumanCount() const;
	void ConfirmMatchRules();
	void CancelMatchRules();
	/** Team select: this player's own team. Other local players change theirs from their own devices. */
	void ChangeOwnTeam(int32 Direction);
	/** The local game or the room host decides when a team battle starts. */
	bool CanStartVersusMatch() const;
	void RequestStartTeamMatch();
	void RetryVersusMatch();
	bool IsVersusMatchWorld() const;
	bool CanOpenMatchRulesFromPause() const;
	/** Pause entries that only belong to the training arena. */
	bool ShowsTrainingPauseEntries() const { return bTrainingMode && !IsVersusMatchWorld(); }

	UFUNCTION(Server, Reliable)
	void ServerChangeTeam(int32 Direction);
	UFUNCTION(Server, Reliable)
	void ServerStartTeamMatch();
	/** This machine played the opening that started at IntroStartedAt (server time) to the end. */
	UFUNCTION(Server, Reliable)
	void ServerReportIntroFinished(double IntroStartedAt);

	UFUNCTION(Server, Reliable)
	void ServerSetPlayerName(const FString& Name);

	UFUNCTION(Server, Reliable)
	void ServerSetReadyForMatch(bool bReady);
	
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
	bool bPendingCreateRoom = true;
	bool bOnlineNameOnly = false;
	bool bRenamingRoom = false;
	/** へやをつくる: the password waits here while the room name is entered. */
	FString PendingRoomPassword;
	/** Development (-CIAutoReady=<seconds>): when this player presses 準備OK by itself. */
	double DevAutoReadyAt = 0.0;
	/** Online rooms and room search accept keyboard and pad alike, following the last one used. */
	bool bOnlineAnyInput = false;
	bool bLastInputGamepad = false;

	// Rumble: pulses waiting for their start time, and the two held channels for sustained rumble.
	struct FPendingRumble
	{
		double StartAt = 0.0;
		float Small = 0.0f;
		float Big = 0.0f;
		float Seconds = 0.0f;
	};
	TArray<FPendingRumble> PendingRumbles;
	void StartRumbleNow(float Small, float Big, float Seconds);
	void UpdateRumble();
	bool CanRumble() const;
	FDynamicForceFeedbackHandle SustainedSmallHandle = 0;
	FDynamicForceFeedbackHandle SustainedBigHandle = 0;
	float SustainedSmall = 0.0f;
	float SustainedBig = 0.0f;
	EChaosImpactPlayFlow PlayFlow = EChaosImpactPlayFlow::Training;
	/** Online clients have no game mode: pair this machine's controllers once all its players exist. */
	void ApplyClientControllerAssignments();
	FTimerHandle ClientAssignmentTimer;
	int32 ClientAssignmentWaits = 0;
	/** CILocalPlayers / CIKeyboardPlayer / CIPadDevice options for the current assignment. */
	FString BuildLocalSetupOptions();

	virtual void PlayerTick(float DeltaTime) override;
	/** Follows the match phase: team select screen, and for a local match the rematch menu after the results. */
	void UpdateMatchScreens();
	/** Opening camera: flies over the stage, then dives into this player's own view. */
	void UpdateMatchIntroCamera();
	/** Back to play from a match screen, without the pause-only checks of ResumeGameplay. */
	void EnterPlayingScreen();

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> IntroCamera;

	/** Full-viewport Ready? / GO! / countdown / FINISH, one per machine. */
	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> MatchAnnouncer;
	/** Primary player: split screen is held off so the flyover fills the whole screen. */
	bool bIntroFullScreen = false;
	/** The opening this controller already reported finished, by its server start time. */
	double ReportedIntroStartedAt = -1.0;

	FChaosImpactMatchRules PendingMatchRules;
	/** Local VS: rules were chosen, so the level opens as a match. */
	bool bLocalMatchRulesChosen = false;
	EChaosImpactScreen MatchRulesReturnScreen = EChaosImpactScreen::ControllerAssignment;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Chaos Impact|Ball")
	EChaosImpactBallFlightMode BallFlightMode = EChaosImpactBallFlightMode::Arc;

	EChaosImpactBallType TrainingSummonBallType = EChaosImpactBallType::Fire;

	UFUNCTION(Server, Reliable)
	void ServerSummonTrainingBall(EChaosImpactBallType Type);

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
	/** Four warp pads around the training arena (group 0), unless the level already has pads. */
	void EnsureTrainingWarpPads();

	UPROPERTY(Transient)
	TArray<TObjectPtr<AChaosImpactWarpPad>> TrainingWarpPads;
	void OpenTrainingLevel(bool bKeepFlightMode, bool bOnlineSearch = false);
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
