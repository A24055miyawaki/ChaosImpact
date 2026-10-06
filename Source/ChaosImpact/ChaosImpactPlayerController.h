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
class AChaosImpactTitleDemo;
class SChaosImpactVersusReveal;
class AChaosImpactCharacterPreview;
class SWidget;
struct FChaosImpactVersusCardInfo;

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
	/** Character select, right after controller assignment: every local player picks a character and a colour. */
	void OpenCharacterSelect();
	/** Everyone is ready: on to wherever the assignment was leading (training, the rules, the room choice). */
	void ConfirmCharacterSelection();
	/** Back to controller assignment. */
	void CancelCharacterSelection();
	/** Which of this machine's players (0-3) a device belongs to on the menus; INDEX_NONE when nobody holds it. */
	int32 GetLocalPlayerIndexForDevice(bool bKeyboard, int32 InputDeviceId) const;
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
	/** Every key press: pause, the training menu and the lobby's 準備OK / 観戦, by the player's own controls. */
	void HandleBoundKeyPressed(FKey Key);
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
	/** The settings screen, from the mode select menu or pause; leaving it comes back to where it was opened. */
	void OpenSettings();
	void CloseSettings();
	/** The VS results this machine shows (null before the first match's). */
	class UChaosImpactResultsView* GetResultsView() const;
	/** Online: this machine leaves its results for the lobby (the next match waits for everyone). */
	void ReturnToLobbyFromResults();
	UFUNCTION(Server, Reliable)
	void ServerReturnFromResults();
	EChaosImpactScreen GetSettingsReturnScreen() const { return SettingsReturnScreen; }
	/** The CPU match filmed behind the title (title world only; null elsewhere or before it starts). */
	AChaosImpactTitleDemo* GetTitleDemo() const { return TitleDemo; }
	UChaosImpactMenuWidget* GetMenuWidget() const { return MenuWidget; }
	virtual bool InputKey(const FInputKeyEventArgs& Params) override;

	// LAN multiplayer rooms
	/** Starts へやをつくる / へやをさがす; asks for a user name first if none was saved. */
	void BeginOnlineFlow(bool bCreateRoom);
	void BeginOnlineRename();
	/** Mode-select entries: they decide what the player-count and controller-assignment screens set up. */
	void BeginTrainingSetup();
	void BeginVersusLocal();
	/** VS local: one player watches a CPU-only match (chosen before players and characters; no split screen). */
	void BeginLocalSpectate();
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
	/** In an online room's lobby: pick another character (and colour, nickname), or another name, from the pause menu. */
	bool CanChangeLoadoutInRoom() const;
	void BeginRoomCharacterChange();
	void BeginRoomPlayerRename();
	void CancelRoomPlayerRename();
	bool IsRenamingPlayer() const { return bRenamingPlayer; }
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
	/** Online lobby: switch between playing and watching the next match (V / D-pad down). */
	void ToggleSpectating();
	bool CanToggleSpectating() const;

	// VS match
	/** Rule screen: local VS before its level opens, or inside a room or match where the rules apply at once. */
	void OpenMatchRules(EChaosImpactScreen ReturnScreen);
	/** Row 0 = minutes, 1 = free-for-all / teams, 2 = CPUs, 3 = CPU strength (only with CPUs). */
	void AdjustMatchRule(int32 Row, int32 Direction);
	const FChaosImpactMatchRules& GetPendingMatchRules() const { return PendingMatchRules; }
	int32 GetMatchHumanCount() const;
	/** Spectating: hides the whole-screen match UI (time, GO, results banner stay hidden until shown again). */
	void SetGameplayUIHidden(bool bHide);
	/** This player watches the match through a spectator camera. */
	bool IsSpectating() const;
	/**
	 * Offline spectating: freezes the whole match (characters, balls, effects, the clock) while the spectator
	 * camera still flies, for screenshots. Never online, where it would freeze everyone.
	 */
	void SetSpectateTimeStopped(bool bStop);
	bool IsSpectateTimeStopped() const { return bSpectateTimeStopped; }
	/** Starts with the pending rules and stage: stage select calls this once a stage is picked. */
	void ConfirmMatchRules();
	void CancelMatchRules();
	/** The rules screen's 決定: on to stage select (every VS flow: local, spectate, online room, rule changes). */
	void OpenStageSelect();
	/** Stage select: picks the stage and starts, the same as the rules used to. */
	void ChooseStage(int32 StageIndex);
	void CancelStageSelect();
	/** Team select: this player's own team. Other local players change theirs from their own devices. */
	void ChangeOwnTeam(int32 Direction);
	/** The local game or the room host decides when a team battle starts. */
	bool CanStartVersusMatch() const;
	void RequestStartTeamMatch();
	void RetryVersusMatch();
	bool IsVersusMatchWorld() const;
	bool CanOpenMatchRulesFromPause() const;
	/** Pause entries that only belong to the training arena. */

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

	UFUNCTION(Server, Reliable)
	void ServerSetSpectating(bool bSpectate);

	/** The character and colour this player picked on their own machine. */
	UFUNCTION(Server, Reliable)
	void ServerSetLoadout(int32 InCharacterIndex, int32 InColour);
	
	/** ソロモード中かどうか */
	UFUNCTION(BlueprintCallable, Category = "Chaos Impact|Solo")
	bool IsSoloMode() const { return bSoloMode; }

	/** 指定されたソロステージを開く */
	UFUNCTION(BlueprintCallable, Category = "Chaos Impact|Solo")
	void OpenSoloLevel(int32 StageIndex);

	/** 現在のソロステージをやり直す */
	UFUNCTION(BlueprintCallable, Category = "Chaos Impact|Solo")
	void RetrySoloLevel();

	/** 現在のソロステージ番号を取得 */
	UFUNCTION(BlueprintCallable, Category = "Chaos Impact|Solo")
	int32 GetCurrentSoloStage() const { return CurrentSoloStage; }

	// 既存の ShowsTrainingPauseEntries を変更（ソロモード中はトレーニングポーズ項目を出さない）
	bool ShowsTrainingPauseEntries() const { return bTrainingMode && !bSoloMode && !IsVersusMatchWorld(); }

protected:
	UPROPERTY(Transient)
	TObjectPtr<UChaosImpactMenuWidget> MenuWidget;

	/** The title world (opened with no mode options): the title shows a CPU match and does not pause the world. */
	bool bTitleDemoWorld = false;
	UPROPERTY(Transient)
	TObjectPtr<AChaosImpactTitleDemo> TitleDemo;
	FTimerHandle TitleDemoTimer;
	void StartTitleDemo();
	/** Effects shown once out of sight (when they are in), then any loading screen told this world is ready. */
	FTimerHandle WarmUpTimer;
	void WarmUpWhenLoaded();
	/** The title waits behind the start-up loading screen for the game's effects to load. */
	bool bAwaitingStartupLoad = false;

	/** Local VS: the VS card plays over the menu, then the VS level opens with the card as its loading screen. */
	void PlayVersusCardThenOpen();
	/** The fighters and rules the VS card shows (PendingMatchRules and the local players' picks). */
	FChaosImpactVersusCardInfo BuildVersusCardInfo() const;
	TSharedPtr<SWidget> VersusCard;
	/** Each player's character, filmed for their place on the VS card. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<AChaosImpactCharacterPreview>> VersusCardPreviews;
	/** A local VS level opens dark and opens from the middle as the match's opening starts. */
	TSharedPtr<SChaosImpactVersusReveal> VersusReveal;
	FTimerHandle VersusRevealTimer;
	double VersusRevealDeadline = 0.0;
	void UpdateVersusReveal();
	/** Online: the VS card on this screen as the match's opening begins (the fighters from the room), then the reveal. */
	void UpdateOnlineVersusCard(float DeltaSeconds);
	FChaosImpactVersusCardInfo BuildOnlineVersusCardInfo() const;
	/** The opening (its start time) the online card was shown for. */
	double OnlineCardShownFor = -1.0;
	bool bOnlineCardUp = false;
	bool bOnlineCardPosed = false;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Chaos Impact|Menu")
	EChaosImpactScreen CurrentScreen = EChaosImpactScreen::Title;
	EChaosImpactScreen SettingsReturnScreen = EChaosImpactScreen::ModeSelect;

	/** The existing prototype map is the training level. */
	UPROPERTY(EditDefaultsOnly, Category="Chaos Impact|Menu", meta=(AllowedClasses="/Script/Engine.World"))
	FSoftObjectPath TrainingLevel = FSoftObjectPath(TEXT("/Game/ThirdPerson/Lvl_ThirdPerson.Lvl_ThirdPerson"));

	bool bTravelPending = false;
	bool bSpectateTimeStopped = false;
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
	/** The character select / name entry was opened from the room's pause menu (it goes back to the room). */
	bool bRoomCharacterChange = false;
	bool bRenamingPlayer = false;
	/** へやをつくる: the password waits here while the room name is entered. */
	FString PendingRoomPassword;
	/** Development (-CIAutoReady=<seconds>): when this player presses 準備OK by itself. */
	double DevAutoReadyAt = 0.0;
	/** Development (-CIDevRoomChange=<seconds>): the in-room name and character change, done by itself (tests). */
	int32 DevRoomChangeStep = 0;
	double DevRoomChangeAt = 0.0;
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
	/** Hands this player's character select pick to their player state once it exists. */
	void SendLoadout();
	bool bLoadoutSent = false;
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
	/** bLoadingScreen: from the title, a loading screen covers the load (the VS card has its own). */
	void OpenTrainingLevel(bool bKeepFlightMode, bool bOnlineSearch = false, bool bLoadingScreen = true);
	/** Back to the title world (the level with no options), where the title plays its demo match. */
	void OpenTitleLevel();
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
	/** Takes the VS card and reveal off the screen (raw Slate content outlives the level). */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Input mapping context setup */
	virtual void SetupInputComponent() override;

	/** Returns true if the player should use UMG touch controls */
	bool ShouldUseTouchControls() const;

	/** ソロモード実行中フラグ */
	bool bSoloMode = false;

	/** 現在プレイ中のソロステージ番号 */
	int32 CurrentSoloStage = 1;

};
