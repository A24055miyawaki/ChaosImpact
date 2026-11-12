// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Containers/Ticker.h"
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
enum class EChaosImpactQueueMode : uint8;
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
	/** Controller assignment for the RPG title: 戻る returns to play (the RPG title), confirming sets the flag below. */
	void PrepareRPGControllerAssignment(int32 LocalPlayerCount);
	/** True once after the RPG flow's assignment was confirmed. */
	bool ConsumeRPGAssignmentConfirmed() { const bool bWas = bRPGAssignmentConfirmed; bRPGAssignmentConfirmed = false; return bWas; }
	/** The confirmed RPG assignment ("CILocalPlayers=..?CIKeyboardPlayer=..?CIPadDevice0=.."), applied live by the RPG. */
	const FString& GetRPGAssignmentOptions() const { return RPGAssignmentOptions; }
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
	/** Training panel: the ball type ボールを呼び出す places in front of this player. */
	void CycleTrainingSummonBallType();
	void SetTrainingSummonBallType(EChaosImpactBallType Type);
	void SummonTrainingBall();
	EChaosImpactBallType GetTrainingSummonBallType() const { return TrainingSummonBallType; }
	/** Session-only experiment. Mouse-only players keep their top-down view. */
	bool IsTrainingShoulderCameraEnabled() const;
	void ToggleTrainingShoulderCamera();
	/** Changes capture without flushing pressed keys (e.g. while holding a nova charge). */
	void RefreshGameplayMouseCapture(bool bShoulder);
	void TogglePauseMenu();
	/** Every key press: pause, the training menu and the lobby's 準備OK / 観戦, by the player's own controls. */
	void HandleBoundKeyPressed(FKey Key);
	bool IsGameplayActive() const { return CurrentScreen == EChaosImpactScreen::Playing && !bTravelPending; }
	/** Off to another level (the VS card is up): the menus no longer answer. */
	bool IsTravelPending() const { return bTravelPending; }
	bool IsTrainingMode() const { return bTrainingMode; }
	bool IsTrainingOverlayOpen() const { return CurrentScreen == EChaosImpactScreen::TrainingOverlay; }
	bool IsPrimaryLocalPlayerController() const;
	int32 GetRequestedLocalPlayerCount() const { return RequestedLocalPlayerCount; }
	bool AreTrainingTargetsEnabled() const { return bTrainingTargetsEnabled; }
	bool IsTrainingCPUEnabled() const { return TrainingCPUCount > 0; }
	int32 GetTrainingCPUCount() const { return TrainingCPUCount; }
	/** Training has no minimum opposing-team size; even 2v0 is valid. */
	bool AreTrainingTeamsEnabled() const { return bTrainingTeams; }
	void ToggleTrainingTeams();
	int32 GetTrainingTeam(bool bCPU, int32 Index) const;
	void CycleTrainingTeam(bool bCPU, int32 Index, int32 Direction = 1);
	void ApplyTrainingTeams();
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
	void CancelOnlineConnection();
	bool IsCheckingOnlineConnection() const { return bCheckingOnlineConnection; }
	const FString& GetOnlineConnectionError() const { return OnlineConnectionError; }
	EChaosImpactPlayFlow GetPlayFlow() const { return PlayFlow; }
	void SubmitOnlineName(const FString& Name);
	void SubmitRoomPassword(const FString& Password);
	void CancelOnlineStatus();
	/** Host only: closes recruitment and opens the rule selector. */
	void CloseRecruitment();
	/** The configured Ready key also closes recruitment when held in the password-room lobby. */
	static constexpr float RecruitmentHoldSeconds = 1.2f;
	float GetRecruitmentHoldProgress() const { return FMath::Clamp(RecruitmentHeldSeconds / RecruitmentHoldSeconds, 0.f, 1.f); }
	/** The room's lobby: watching and playing switch only on a held key, never on a press that was meant for something else. */
	static constexpr float SpectateHoldSeconds = 0.8f;
	float GetSpectateHoldProgress() const { return FMath::Clamp(SpectateHeldSeconds / SpectateHoldSeconds, 0.f, 1.f); }
	void LeaveOnlineRoom();
	void StopRoomSearch();
	bool IsOnlineRoom() const { return GetNetMode() != NM_Standalone; }
	bool IsOnlineRoomHost() const { return GetNetMode() == NM_ListenServer; }
	bool IsPendingCreateRoom() const { return bPendingCreateRoom; }
	/** Looking for a 合言葉 room, or waiting in the queue of ランダムマッチ / ランクマッチ. */
	bool IsSearchingForRoom() const;
	bool CanCloseRecruitment() const;

	// ランダムマッチ / ランクマッチ
	/** Joins the queue; the player waits in this machine's own training until the match is found. */
	void BeginMatchQueue(EChaosImpactQueueMode Mode);
	/** ランクマッチ's own page (the rank, the record, the search button); asks the relay for the latest rank. */
	void OpenRankTop();
	/** オンラインメニュー: 合言葉マッチ (make a room or find one) and 戦績 (asks the relay for the latest). */
	void OpenOnlinePrivate();
	void OpenOnlineRecord();
	/**
	 * The rules screen's kinds of match, by number: 0 個人戦, 1-3 チーム戦 2-4チーム, 4 クラッシュボール, 5 1対1,
	 * 6 バトルロイヤル 個人, 7-9 バトルロイヤル 2-4チーム, 10 トーナメント.
	 */
	static constexpr int32 MatchModeCount = 11;
	static void ApplyMatchMode(FChaosImpactMatchRules& Rules, int32 Mode);
	int32 GetMatchMode() const;
	/** Why a kind cannot be chosen here (empty: it can). */
	FString GetMatchModeBlocker(int32 Mode) const;
	void SetMatchMode(int32 Mode);
	/** The rules screen's list also offers トーナメント's two ways to run as two choices: 11 is トーナメント 同時進行. */
	static constexpr int32 TournamentAllAtOnceChoice = 11;
	int32 GetMatchModeChoice() const;
	void SetMatchModeChoice(int32 Choice);
	// ---- ローカルトーナメント (VS モードの「トーナメント」)
	/** Its registration screen. */
	void BeginLocalTourney();
	/** From the registration: 同時進行 has its people take their controllers first; 1試合ずつ starts straight away. */
	void StartLocalTourney();
	/** A place to take with a controller: a person of the match (or of the whole tournament), or a CPU (nothing to take). */
	struct FTourneySeat
	{
		FString Name;
		int32 Character = 0;
		int32 Colour = 0;
		TArray<uint8> CustomColours;
		bool bCPU = false;
		int32 CPULevel = 0;
		/** -2 nobody yet, -1 the keyboard, else the pad's input device id. */
		int32 Device = -2;
	};
	const TArray<FTourneySeat>& GetTourneySeats() const { return TourneySeats; }
	const FString& GetTourneySeatTitle() const { return TourneySeatTitle; }
	/** When every place was taken (0: not yet); it starts a moment after. */
	double GetTourneySeatsFullAt() const { return TourneySeatsFullAt; }
	bool IsTourneySeatForMatch() const { return TourneySeatMatch != INDEX_NONE; }
	/** A button on some device: it takes the next free place, or gives its own back. */
	void PressTourneySeat(bool bKeyboard, int32 InputDeviceId);
	/** Escape before the tournament has begun: back to the registration. */
	void CancelTourneySeats();
	/** From the menu (the world is paused under it): starts once every place has been taken for a moment. */
	void UpdateTourneySeats();
	/** The game mode's word: the tournament is over, back to the registration. */
	void OnLocalTournamentOver();
	/** Which of this machine's players each device works (the menus' own reading of it). */
	void SetLocalInputRouting(int32 Players, int32 KeyboardPlayer, const TArray<int32>& PadDevices);
	/** 回線チェック: measures the line to the relay and shows it. */
	void OpenLineCheck();
	/** Online: みんなのカラー (the custom colours players post). */
	void OpenGallery();
	/** ランダムマッチ 1v1 found (DuelFound): this player's answer. */
	void AnswerDuelOffer(bool bAccept);
	/** A matched room's results: leave them, to search again the same way or for the online menu. */
	void LeaveMatchmadeResults(bool bSearchAgain);
	/** A matched 1v1's results: 同じ相手と再戦 (it starts once the other player asks too). */
	void RequestRematch();
	bool HasRequestedRematch() const;
	/** The other player of this matched 1v1 is still in the room; bOutAsked: they asked for the rematch. */
	bool IsRematchOpponentHere(bool& bOutAsked) const;
	UFUNCTION(Server, Reliable)
	void ServerRequestRematch();
	/** The matched room is over for this machine: back to its own training (bOpponentLeft: told so there). */
	UFUNCTION(Client, Reliable)
	void ClientLeaveMatchmade(bool bOpponentLeft = false);
	/** The host closed the room (解散): this machine is told so and goes back to the online menu at once. */
	UFUNCTION(Client, Reliable)
	void ClientRoomDisbanded(bool bTournament);
	/** 降参: a matched 1v1 under way is given to the other player (its results follow); any other match is left. */
	void Surrender();
	UFUNCTION(Server, Reliable)
	void ServerSurrender();
	/** A matched room's match is being played (the pause menu then offers only 降参). */
	bool IsInMatchmadeMatch() const;
	/** カラーエディット from any menu: it goes back to ReturnTo when it closes. */
	void OpenColourEdit(EChaosImpactScreen ReturnTo);
	void CloseColourEdit();
	/** カラーエディット opened from a room's pause menu, while the room still lets players change: saving a preset there asks whether
	 * to play as the character edited, in its colours (ApplyRoomLoadout). */
	bool IsColourEditApplyOffered() const;
	/** In a room: local player LocalIndex plays as NewCharacter in カスタムカラー Preset (INDEX_NONE: its usual colours), sent
	 * to the room straight away. False when the room no longer lets anyone change. */
	bool ApplyRoomLoadout(int32 LocalIndex, int32 NewCharacter, int32 Preset);
	/** ランダムマッチ 1v1: the card of the opponent found comes up over the training, and goes when it is settled (the menu
	 * calls it too: the training is paused under the card). */
	void UpdateDuelOfferScreen();
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
	/** Online team rules decided, in the lobby: the team key moves this player to the next team with room. */
	bool CanChangeLobbyTeam() const;
	/** The whole-stage map over the screen (its key opens and closes it; only while playing). */
	bool IsOverviewMapOpen() const { return bOverviewMapOpen && IsGameplayActive(); }
	/** Online lobby: switch between playing and watching the next match (V / D-pad down). */
	void ToggleSpectating();
	bool CanToggleSpectating() const;

	// VS match
	/** Rule screen: local VS before its level opens, or inside a room or match where the rules apply at once. */
	void OpenMatchRules(EChaosImpactScreen ReturnScreen);
	/** Row 0 = minutes, 1 = free-for-all / teams, 2 = CPUs, 3 = CPU strength (only with CPUs). */
	void AdjustMatchRule(int32 Row, int32 Direction);
	const FChaosImpactMatchRules& GetPendingMatchRules() const { return PendingMatchRules; }
	/** ボールスイッチ on the rules being chosen: one special ball on / off, or every one back on. */
	void SetBallSwitchOn(EChaosImpactBallType Type, bool bOn);
	void SetAllBallsOn();
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
	/** Team select: this player straight onto Team (a click on its card). */
	void SetOwnTeam(int32 Team);
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
	void ServerSetTeam(int32 Team);
	UFUNCTION(Server, Reliable)
	void ServerStartTeamMatch();
	/** This machine played the opening that started at IntroStartedAt (server time) to the end. */
	UFUNCTION(Server, Reliable)
	void ServerReportIntroFinished(double IntroStartedAt);

	UFUNCTION(Server, Reliable)
	void ServerSetPlayerName(const FString& Name);
	/** A machine's second player: the nickname they picked (empty: the first player's name with (2)). */
	UFUNCTION(Server, Reliable)
	void ServerSetSecondPlayerName(const FString& Name);
	double NextSecondNameAt = 0.0;
	FString SentSecondName;
	bool bSentSecondName = false;

	UFUNCTION(Server, Reliable)
	void ServerSetReadyForMatch(bool bReady);

	UFUNCTION(Server, Reliable)
	void ServerSetSpectating(bool bSpectate);

	// ---- 公開トーナメント (the online menu's トーナメント: a list of tournaments to enter, and making one) ----
	void OpenTourneyList();
	void RefreshTourneyList();
	void CloseTourneyList();
	void OpenTourneyCreate();
	/** The set-up's rows: 0 name (opens the name entry), 1 start, 2 how it runs, 3 people, 4 CPUs, 5 CPU strength. */
	void AdjustTourneySetting(int32 Row, int32 Direction);
	void EditTourneyTitle();
	bool IsEditingTourneyTitle() const { return bEditingTourneyTitle; }
	void CreatePublicTourney();
	const struct FChaosImpactTourneyNotice& GetPendingTourney() const;
	/** How long after now the tournament being set up starts. */
	int32 GetPendingTourneyMinutes() const { return PendingTourneyMinutes; }

	// ---- トーナメント ----
	/** This player has the tournament's panel (bracket, choices) open: their character stands still meanwhile. */
	bool IsTournamentPanelOpen() const { return bTournamentPanelOpen; }
	void SetTournamentPanelOpen(const bool bOpen) { bTournamentPanelOpen = bOpen; }
	/** While not playing: practise, watch a match (INDEX_NONE: any being played) or look at the bracket. */
	UFUNCTION(Server, Reliable)
	void ServerSetTournamentWait(EChaosImpactTournamentWait Wait, int32 WatchMatch);
	/** The host's answer for two CPUs next (one match at a time): 0 watch, 1 skip, 2 skip every one. */
	UFUNCTION(Server, Reliable)
	void ServerAnswerTournamentCPUMatch(int32 Choice);
	UFUNCTION(Server, Reliable)
	void ServerSkipTournamentResults();
	class UChaosImpactTournamentWidget* GetTournamentWidget() const { return TournamentWidget; }

	/** The character and colour this player picked on their own machine. */
	UFUNCTION(Server, Reliable)
	void ServerSetLoadout(int32 InCharacterIndex, int32 InColour, const TArray<uint8>& InCustomColours);

	/**
	 * ペイント over the network, a piece at a time (each piece asked for once the last has come): this player's own
	 * pictures up to the host (offered by key; sent only when the host does not have them yet), and anyone's down
	 * from the host to a machine that shows them.
	 */
	UFUNCTION(Server, Reliable)
	void ServerOfferPaint(int32 Key, int32 Total);
	UFUNCTION(Client, Reliable)
	void ClientSendPaintFrom(int32 Key, int32 Offset);
	UFUNCTION(Server, Reliable)
	void ServerPaintPiece(int32 Key, int32 Offset, int32 Total, const TArray<uint8>& Bytes);
	UFUNCTION(Server, Reliable)
	void ServerWantPaint(int32 Key, int32 Offset);
	UFUNCTION(Client, Reliable)
	void ClientPaintPiece(int32 Key, int32 Offset, int32 Total, const TArray<uint8>& Bytes);
	
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

	/** When this solo stage was cleared (FPlatformTime seconds; negative before): the HUD's clear presentation. */
	double GetSoloClearedAt() const { return SoloClearedAt; }
	/** How long after the clear it goes back to the world map (real seconds). */
	static constexpr float SoloClearSeconds = 5.5f;

	/** Solo mode's world map (1: 占領された世界, 2: デットボール合衆国), the player standing on a node (a stage or a gate). */
	UFUNCTION(BlueprintCallable, Category = "Chaos Impact|Solo")
	/** bLoadingScreen: false for the gate between the two maps (just a warp). */
	void OpenSoloWorldMap(int32 Map, int32 AtNode, bool bLoadingScreen = true);
	/** Solo mode's own title (its three files): /Game/ChaosImpact/solo/Title/Lvl_SoloTitle?CISoloTitle=1. */
	void OpenSoloTitle();
	/** Plays a file (1-3) from solo mode's title: a new one starts on the first map, one begun goes back where it was. */
	void StartSoloFile(int32 Slot);
	/** On solo mode's title (?CISoloTitle=1). */
	bool IsSoloTitle() const { return bSoloTitle; }
	/**
	 * The title's demo play: a local VS match of CPUs only (a stage and characters at random) watched through one CPU's
	 * camera, opened with ?CIDemo=1 (AChaosImpactDemoPlay runs it). Started when the title is left alone a while.
	 */
	void StartDemoPlay();
	/** Back to the title from the demo. */
	void EndDemoPlay();
	bool IsDemoPlay() const { return bDemoPlay; }
	/** The demo: the CPU whose view the VS opening's camera dives into (as a player's would into theirs). */
	void SetDemoIntroCharacter(class AChaosImpactCharacter* InCharacter);
	/** How long the title waits untouched before its demo (seconds; -CIDemoIdle=<seconds> changes it). */
	static float GetDemoIdleSeconds();

	/** Solo mode from the menus: the world map where the player last was. */
	void OpenSoloFromMenu();

	/** On a solo world map (?CIWorldMap=1). */
	bool IsSoloWorldMap() const { return bSoloWorldMap; }

	/** Every solo room in this stage's level is cleared: saved, and back to the world map in a few seconds. */
	void NotifySoloStageCleared();

	/** The exploration map (OpenWorld/, an experiment that can be removed): its hidden way in from the title. */
	bool OpenExplorationMap(const FString& ExtraOptions = FString());
	/** The removable battle royale (BattleRoyale/): whether its level is here, and going to it. Nothing else in the core knows about it. */
	static bool HasBattleRoyaleIsland();
	bool OpenBattleRoyaleIsland();

	/** A choice from the world map's pause menu (its Detail); false when it is not one. */
	bool HandleWorldMapPauseChoice(const FString& Choice);

	// 既存の ShowsTrainingPauseEntries を変更（ソロモード中はトレーニングポーズ項目を出さない）
	bool ShowsTrainingPauseEntries() const { return bTrainingMode && !bSoloMode && !IsVersusMatchWorld() && !IsDojo(); }
	/** 訓練場: its world is open (a VS stage with ?CIDojo=1). */
	bool IsDojo() const;
	/** 訓練場's filming: the overview map shown or put away. */
	void SetOverviewMapOpen(bool bOpen) { bOverviewMapOpen = bOpen; }
	/** 訓練場: from its lesson list (or the pause menu) straight into play. */
	void EnterDojoPlay() { EnterPlayingScreen(); }
	/** From the mode select: 訓練場 on a VS stage, one player. Level / Lesson: another of its levels, opened on a lesson ("group_lesson"). */
	void OpenDojo(const FString& Level = FString(), const FString& Lesson = FString(), const FString& Focus = FString());
	/** 訓練場's mock matches: a real local match by these rules (one player), the dojo along on Lesson ("group_lesson"). */
	void OpenDojoMatch(const struct FChaosImpactMatchRules& Rules, const FString& Lesson);

protected:
	UPROPERTY(Transient)
	TObjectPtr<UChaosImpactMenuWidget> MenuWidget;

	/** The title world (opened with no mode options): the title shows a CPU match and does not pause the world. */
	bool bTitleDemoWorld = false;
	UPROPERTY(Transient)
	TObjectPtr<AChaosImpactTitleDemo> TitleDemo;
	FTimerHandle TitleDemoTimer;
	void StartTitleDemo();
	/**
	 * Effects shown once out of sight (when they are in), then any loading screen told this world is ready. Checked on
	 * the core ticker, not a world timer: a menu that pauses the world (or the start-up screen) must not hold it up.
	 */
	FTSTicker::FDelegateHandle WarmUpTicker;
	double WarmUpCheckAt = 0.0;
	double WarmUpReadyAt = 0.0;
	bool TickWarmUp(float DeltaSeconds);
	/** True once the effects were in and have been shown. */
	bool WarmUpWhenLoaded();
	void FinishWarmUp();
	/**
	 * The loading screen goes once this world is ready, but online not before its match is under way: a matched room's
	 * own lobby (passed through on the way to the match) and a match level still waiting for everyone to come back in
	 * keep it up (at most a while, should the match never start).
	 */
	void ReleaseLoadingWhenRoomReady();
	FTimerHandle LoadingReleaseTimer;
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
	/** The room's countdown to a match was on last frame (its end closes the room's menus: UpdateMatchScreens). */
	bool bSawRoomCountdown = false;

	/** The existing prototype map is the training level. */
	UPROPERTY(EditDefaultsOnly, Category="Chaos Impact|Menu", meta=(AllowedClasses="/Script/Engine.World"))
	FSoftObjectPath TrainingLevel = FSoftObjectPath(TEXT("/Game/ThirdPerson/Lvl_ThirdPerson.Lvl_ThirdPerson"));

	bool bTravelPending = false;
	bool bRPGAssignmentConfirmed = false;
	FString RPGAssignmentOptions;
	bool bSpectateTimeStopped = false;
	bool bTrainingMode = false;
	bool bTrainingShoulderCamera = false;
	int32 RequestedLocalPlayerCount = 1;
	bool bTrainingTargetsEnabled = true;
	int32 TrainingCPUCount = 0;
	bool bTrainingTeams = false;
	int32 TrainingHumanTeams[4] = {0,0,0,0};
	int32 TrainingCPUTeams[4] = {1,1,1,1};
	bool bPrimaryUsesGamepad = false;
	bool bRequestedPrimaryUsesGamepad = false;
	/** Active and pending slot occupied by the one local keyboard/mouse pair. */
	int32 ActiveKeyboardPlayerIndex = 0;
	int32 RequestedKeyboardPlayerIndex = 0;
	EChaosImpactScreen ControllerAssignmentReturnScreen = EChaosImpactScreen::TrainingSetup;
	/** Physical device ids, stored in the exact order their join button was pressed. */
	TArray<int32> JoinedInputDeviceIds;
	TArray<int32> JoinedLegacyControllerIds;
	bool bTrainingOverlayPresentationActive = false;
	bool bPendingCreateRoom = true;
	bool bOnlineNameOnly = false;
	bool bCheckingOnlineConnection = false;
	FString OnlineConnectionError;
	int32 OnlineConnectionGeneration = 0;
	EChaosImpactScreen OnlineConnectionReturnScreen = EChaosImpactScreen::VSSelect;
	bool bRenamingRoom = false;
	/** The character select / name entry was opened from the room's pause menu (it goes back to the room). */
	bool bRoomCharacterChange = false;
	/** A matched room: tells the session when its match is under way, and sends a ランクマッチ's result once. */
	void UpdateMatchmadeProgress();
	EChaosImpactScreen ColourEditReturnScreen = EChaosImpactScreen::ModeSelect;
	TArray<FTourneySeat> TourneySeats;
	FString TourneySeatTitle;
	int32 TourneySeatMatch = INDEX_NONE;
	double TourneySeatsFullAt = 0.0;
	void OpenTourneySeatsForMatch(int32 MatchIndex);
	void FinishTourneySeats();
	void TravelToLocalTourney();
	/** In a local tournament's level: a match waiting for its players brings up the seats. */
	void UpdateLocalTourneySeatRequest();
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
	void UpdateRecruitmentHold(float DeltaSeconds);
	void UpdateSpectateHold(float DeltaSeconds);
	float SpectateHeldSeconds = 0.f;
	/** Held since before it could count (a menu was open, the window was in the background): it must be let go first. */
	bool bSpectateHoldNeedsRelease = true;
	float RecruitmentHeldSeconds = 0.f;
	/** Do not carry a held button across menus, loss of focus, or the next lobby. */
	bool bRecruitmentHoldNeedsRelease = false;

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
	virtual void PreClientTravel(const FString& PendingURL, ETravelType TravelType, bool bIsSeamlessTravel) override;
	/** Hands this player's character select pick to their player state once it exists. */
	void SendLoadout();
	bool bLoadoutSent = false;
	/** This player's ペイント (its key on the player state; to the host first when this is not it). */
	void SendPaint(int32 PaintCharacter, int32 Preset);
	/** The pictures this machine is fetching from the host now (0: none). */
	int32 FetchingPaint = 0;
	/** Follows the match phase: team select screen, and for a local match the rematch menu after the results. */
	void UpdateMatchScreens();
	/** Opening camera: flies over the stage, then dives into this player's own view. */
	void UpdateMatchIntroCamera();
	/**
	 * Drops the VS stage's props up on its walls (billboards, graffiti) from this player's own view while this player's
	 * camera is close to them, so they never stand between the camera and the play (the walls fade by their material).
	 */
	void UpdateNearCameraParts();
	TWeakObjectPtr<class AChaosImpactStageBase> NearPartsStage;
	double NextNearPartsSearchAt = 0.0;
	/** Back to play from a match screen, without the pause-only checks of ResumeGameplay. */
	void EnterPlayingScreen();

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> IntroCamera;

	/** Full-viewport Ready? / GO! / countdown / FINISH, one per machine. */
	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> MatchAnnouncer;

	/** Each local player's own tournament panel (in their part of a split screen). */
	UPROPERTY(Transient)
	TObjectPtr<class UChaosImpactTournamentWidget> TournamentWidget;
	bool bOverviewMapOpen = false;
	/** One player on this machine: every connected controller is given to them too (see ClaimEveryPadForSolePlayer). */
	FTimerHandle SolePadTimer;
	void ClaimEveryPadForSolePlayer();
	/** Development: flocks let loose so far (-CIDevSimaePerch). */
	int32 DevPerchCount = 0;
	bool bTournamentPanelOpen = false;
	/** The public tournament being set up (a pointer: its type lives with the session). */
	TSharedPtr<struct FChaosImpactTourneyNotice> PendingTourney;
	int32 PendingTourneyMinutes = 15;
	bool bEditingTourneyTitle = false;
	void UpdateTournamentWidget();
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
	void OpenTrainingLevel(bool bOnlineSearch = false, bool bLoadingScreen = true);
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

	double SoloClearedAt = -1.0;
	/** The clear's celebration round the player: a pillar of light, a ring and fireworks bursting overhead. */
	void PlaySoloClearFireworks();

	/** On a solo world map. */
	bool bSoloWorldMap = false;
	bool bSoloTitle = false;
	bool bDemoPlay = false;
	TWeakObjectPtr<class AChaosImpactCharacter> DemoIntroCharacter;
	/** Set while travelling to the demo (its options carry ?CIDemo=1). */
	bool bDemoTravel = false;
	double TitleShownSince = 0.0;
	FTimerHandle SoloReturnTimer;

};
