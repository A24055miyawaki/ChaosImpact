#pragma once

#include "CoreMinimal.h"
#include "ChaosImpactRank.h"
#include "ChaosImpactRelay.h"
#include "Containers/Ticker.h"
#include "Engine/EngineBaseTypes.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "OnlineSessionSettings.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ChaosImpactSessionSubsystem.generated.h"

class FOnlineSessionSearch;
class UNetDriver;

UENUM(BlueprintType)
enum class EChaosImpactRoomState : uint8
{
	None,
	/** Creating the LAN session. */
	Creating,
	/** This machine is the listen-server host of a room. */
	Hosting,
	/** Listing the rooms that use the password, again and again while the list is shown. */
	Searching,
	Joining,
	/** Connected to someone else's room as a client. */
	InRoom
};

/** ランダムマッチ (up to 8 players, free-for-all) or ランクマッチ (1 v 1), matched by the relay. */
UENUM(BlueprintType)
enum class EChaosImpactQueueMode : uint8
{
	None,
	Random,
	Rank
};

/** A room found by へやをさがす. */
struct FChaosImpactRoomListing
{
	FString RoomName;
	FString HostName;
	int32 Members = 1;
	/** Watching members, up to AChaosImpactGameState::MaxSpectators. */
	int32 Spectators = 0;
	bool bOpen = true;
	/** Round trip of the search reply, for the connection bars. */
	int32 PingMs = 0;
	/** A LAN room. */
	FOnlineSessionSearchResult Result;
	/** An internet room: its port on the relay (0 for a LAN room). */
	int32 RelayPort = 0;
};

/**
 * Room lifecycle for the 合言葉 (password) rooms. Lives on the GameInstance so it survives map travel.
 * The password is a group password: several rooms may share it, and searching lists all of them.
 * With a relay server set (ChaosImpactRelay), rooms are listed and played over the internet through it; without one,
 * they are found on the LAN as before.
 */
UCLASS()
class UChaosImpactSessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	static UChaosImpactSessionSubsystem* Get(const UObject* WorldContext);

	FString GetPlayerName() const;
	bool HasSavedPlayerName() const;
	void SetPlayerName(const FString& Name);
	static constexpr int32 MaxNameLength = 10;
	static constexpr int32 MaxRoomNameLength = 12;

	void CreateRoom(const FString& InPassword, const FString& InRoomName);
	/** Lists the rooms using the password, refreshing until StopSearch or a join. */
	void StartSearch(const FString& InPassword);
	void StopSearch();
	/**
	 * Joins a listed room: as a player while it is recruiting and has room, otherwise (mid-match, recruitment
	 * closed or full) as a spectator. False (with a notice) when neither is possible.
	 */
	bool JoinRoomListing(int32 Index);
	/** A listed room would be joined as a spectator (see JoinRoomListing). */
	bool WouldJoinAsSpectator(const FChaosImpactRoomListing& Listing) const;
	/** Why a listed room cannot be joined at all (a short label), or empty when it can. */
	FString GetJoinBlocker(const FChaosImpactRoomListing& Listing) const;
	const TArray<FChaosImpactRoomListing>& GetRoomListings() const { return Listings; }
	/** Changes whenever the room list changes, so screens know when to rebuild. */
	int32 GetRoomListingsVersion() const { return ListingsVersion; }
	bool HasSearchedOnce() const { return bSearchedOnce; }
	/** Abandons a room that is still being created (before travelling to it). */
	void CancelCreate();
	/** Leaves or dissolves the room and returns this machine to its own training arena. */
	void LeaveRoom();
	/** Host only: advertise whether new members may still join, and how many are inside. */
	void SetRecruitmentOpen(bool bOpen);
	void SetMemberCount(int32 Count);
	void SetSpectatorCount(int32 Count);
	/** Host only: the room's name as listed to searchers. */
	void SetRoomName(const FString& Name);
	const FString& GetRoomName() const { return RoomName; }
	FString GetDefaultRoomName() const;
	/**
	 * Identifies this machine to hosts, so reconnecting after a drop replaces its old place in the room instead
	 * of appearing twice. Saved once per machine; -CIMachineToken= overrides it for tests on one PC.
	 */
	FString GetMachineToken() const;

	EChaosImpactRoomState GetState() const { return State; }
	const FString& GetPassword() const { return Password; }
	/** Non-empty when the last CreateRoom attempt failed. */
	const FString& GetCreateError() const { return CreateError; }
	const FString& GetNotice() const { return Notice; }
	double GetNoticeTime() const { return NoticeAt; }
	/** True once after a disconnect, so the title world can send the player back to training. */
	bool ConsumeReturnToTraining();
	/** Rooms go over the internet through the relay (else the LAN). */
	bool IsInternet() const;
	/** Hosting an internet room and the relay is answering. */
	bool IsRelayConnected() const;

	/**
	 * This machine's split-screen setup for VS online (CILocalPlayers / CIKeyboardPlayer / CIPadDevice options).
	 * Every level opened while online uses it: the room, the search lobby and the way back to training.
	 */
	void SetLocalSetup(const FString& Options, int32 LocalPlayers);
	int32 GetLocalPlayerCount() const { return LocalPlayerCount; }
	const FString& GetLocalSetupOptions() const { return LocalSetupOptions; }

	static FString GetTrainingMapName();
	FString GetOfflineTrainingOptions() const;

	// ---- ランダムマッチ / ランクマッチ (internet only: the relay keeps the queues and makes the matches)
	/**
	 * Joins the queue; the player then waits in their own training (the opponents are only met in the match).
	 * False, with a notice, when it cannot (no relay, a pair in ランクマッチ, already in a room).
	 */
	bool StartQueue(EChaosImpactQueueMode Mode);
	void CancelQueue();
	EChaosImpactQueueMode GetQueueMode() const { return QueueMode; }
	bool IsQueueing() const { return QueueMode != EChaosImpactQueueMode::None; }
	/** The relay has matched this machine: it is about to open (host) or join the match's room. */
	bool IsQueueMatched() const { return bQueueMatched; }
	/** FPlatformTime seconds the search began / the match was found. */
	double GetQueueStartedAt() const { return QueueStartedAt; }
	double GetQueueMatchedAt() const { return QueueMatchedAt; }
	/** Players searching in this mode now, this machine's included. */
	int32 GetQueueWaitingCount() const { return QueueWaiting; }
	/** ランダムマッチ: seconds until the players gathered so far are matched (negative while fewer than two wait). */
	float GetQueueStartsIn() const { return QueueStartsIn; }
	int32 GetQueueExpected() const { return QueueExpected; }
	const TArray<FString>& GetQueueNames() const { return QueueNames; }
	/** ランクマッチ: who was matched against this player, and their rank. */
	const FString& GetQueueOpponentName() const { return QueueOpponentName; }
	const FChaosImpactRankInfo& GetQueueOpponentRank() const { return QueueOpponentRank; }
	/** The kind of matched room this machine is in or heading to (None for a 合言葉 room or outside rooms). */
	EChaosImpactQueueMode GetMatchmadeMode() const { return MatchmadeMode; }
	/** This machine's matched room's match has reached GO (leaving it from now on forfeits a ランクマッチ). */
	void NotifyMatchmadeMatchStarted();
	/** After a matched room's results: back to this machine's own training, where the menu to search again comes up. */
	void FinishMatchmade();
	/** Once, in the training a matched room returned to: which menu to bring up there (None: none). */
	EChaosImpactQueueMode ConsumeShowQueueMenu();

	// ---- Rank (the preseason, kept by the relay)
	void RefreshRankInfo();
	const FChaosImpactRankInfo& GetRankInfo() const { return RankInfo; }
	bool IsRankInfoPending() const { return bRankInfoPending; }
	/** This machine's side of the ランクマッチ just played ("win", "lose" or "draw"), sent once; see GetRankReport. */
	void ReportRankResult(const FString& Result);
	const FChaosImpactRankReport& GetRankReport() const { return RankReport; }
	/** Names this player to the relay's ranks: made once per PC and kept secret (-CIPlayerToken= for tests on one PC). */
	FString GetPlayerToken() const;

private:
	void QueueJoin();
	void QueueTick();
	void QueuePoll();
	void HandleQueueMatched(const TSharedPtr<FJsonObject>& Reply);
	void ResetQueueMatch();
	void StopQueueTicker();
	void SendRankReport();
	EChaosImpactQueueMode QueueMode = EChaosImpactQueueMode::None;
	FString QueueTicket;
	double QueueStartedAt = 0.0;
	double QueueMatchedAt = 0.0;
	double QueueLastPollAt = 0.0;
	int32 QueueWaiting = 0;
	float QueueStartsIn = -1.0f;
	int32 QueueFailures = 0;
	bool bQueueRequestInFlight = false;
	bool bQueueMatched = false;
	bool bQueueHost = false;
	bool bQueueHostReady = false;
	int32 QueuePort = 0;
	FString QueueMatchId;
	int32 QueueExpected = 0;
	TArray<FString> QueueNames;
	FString QueueOpponentName;
	FChaosImpactRankInfo QueueOpponentRank;
	/** Bumped on every join and cancel: replies to older queue requests are ignored. */
	int32 QueueGeneration = 0;
	FTSTicker::FDelegateHandle QueueTicker;
	/** The last round trip to the relay (the host of a match is the machine with the quickest). */
	int32 LastRelayRtt = 0;
	EChaosImpactQueueMode MatchmadeMode = EChaosImpactQueueMode::None;
	FString MatchmadeId;
	/** Options the matched room's host opens it with (who and how many are coming). */
	FString MatchmadeOptions;
	bool bMatchmadeStarted = false;
	EChaosImpactQueueMode ShowQueueMenu = EChaosImpactQueueMode::None;
	FChaosImpactRankInfo RankInfo;
	bool bRankInfoPending = false;
	FChaosImpactRankReport RankReport;
	FString RankReportMatch;
	FString RankReportResult;
	int32 RankReportTries = 0;
	FTSTicker::FDelegateHandle RankReportTicker;
	mutable FString CachedPlayerToken;

	// ---- Internet rooms (the relay)
	void RelayCreate();
	void RelaySearch();
	void RelayHeartbeat();
	void StartTunnel();
	void JoinRelayRoom(int32 Port);
	void StopRelayHosting();
	/** The room is ready (made on the LAN or on the relay): opens it as its host. */
	void OpenRoomAsHost();
	/** The search has its rooms (from the LAN or the relay). */
	void FinishSearch(TArray<FChaosImpactRoomListing>&& Found, bool bWasSuccessful);
	void TravelToRoom(const FString& ConnectString);
	TUniquePtr<FChaosImpactRelayTunnel> Tunnel;
	TUniquePtr<class FChaosImpactRelayGuestTunnel> GuestTunnel;
	FString RelayRoomId;
	FString RelayRoomKey;
	int32 RelayRoomPort = 0;
	/** Bumped whenever a room is made, searched for or left: replies to older requests are ignored. */
	int32 RelayGeneration = 0;
	bool bRelaySearchInFlight = false;
	bool bRelayUnreachableShown = false;
	FTSTicker::FDelegateHandle HeartbeatTicker;

	IOnlineSessionPtr GetSessions() const;
	void BindSessionDelegates();
	void BeginFind();
	void CreateSessionNow();
	void PublishRoomSettings();
	void PostNotice(const FString& Message);
	void HandleFindComplete(bool bWasSuccessful);
	void HandleCreateComplete(FName SessionName, bool bWasSuccessful);
	void HandleJoinComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result);
	void HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType,
		const FString& ErrorString);
	void HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString);
	void ReturnAfterDisconnect(const FString& ErrorString);

	EChaosImpactRoomState State = EChaosImpactRoomState::None;
	FString Password;
	FString RoomName;
	FString CreateError;
	FString Notice;
	double NoticeAt = -100.0;
	FString GeneratedName;
	bool bReturnToTraining = false;
	bool bSessionDelegatesBound = false;
	bool bRecruitmentOpen = true;
	int32 MemberCount = 1;
	int32 SpectatorCount = 0;
	/** The room being joined is joined as a spectator (the CISpectator login option). */
	bool bJoinAsSpectator = false;
	FString LocalSetupOptions = TEXT("CILocalPlayers=1?CIKeyboardPlayer=0");
	int32 LocalPlayerCount = 1;
	TSharedPtr<FOnlineSessionSearch> Search;
	TArray<FChaosImpactRoomListing> Listings;
	int32 ListingsVersion = 0;
	bool bSearchedOnce = false;
	/** Development (-CIAutoRoom=search:...): join the first open room once it has been listed for a moment. */
	bool bDevAutoJoin = false;
	double DevAutoJoinAt = 0.0;
	mutable FString CachedMachineToken;
	/**
	 * The list refreshes on the core ticker: menus pause the game world, and a paused world's timers
	 * (the GameInstance timer manager included) do not run.
	 */
	void ScheduleRefresh(float DelaySeconds);
	void CancelRefresh();
	FTSTicker::FDelegateHandle RefreshTicker;
	FDelegateHandle NetworkFailureHandle;
	FDelegateHandle TravelFailureHandle;
};
