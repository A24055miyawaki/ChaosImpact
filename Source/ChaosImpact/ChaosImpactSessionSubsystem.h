#pragma once

#include "CoreMinimal.h"
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

/** A room found by へやをさがす. */
struct FChaosImpactRoomListing
{
	FString RoomName;
	FString HostName;
	int32 Members = 1;
	bool bOpen = true;
	/** Round trip of the LAN search reply, for the connection bars. */
	int32 PingMs = 0;
	FOnlineSessionSearchResult Result;
};

/**
 * Room lifecycle for the LAN multiplayer mode. Lives on the GameInstance so it survives map travel.
 * The password is a group password: several rooms may share it, and searching lists all of them.
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
	/** Joins a listed room. False (with a notice) when it has filled up or closed meanwhile. */
	bool JoinRoomListing(int32 Index);
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

	/**
	 * This machine's split-screen setup for VS online (CILocalPlayers / CIKeyboardPlayer / CIPadDevice options).
	 * Every level opened while online uses it: the room, the search lobby and the way back to training.
	 */
	void SetLocalSetup(const FString& Options, int32 LocalPlayers);
	int32 GetLocalPlayerCount() const { return LocalPlayerCount; }
	const FString& GetLocalSetupOptions() const { return LocalSetupOptions; }

	static FString GetTrainingMapName();
	FString GetOfflineTrainingOptions() const;

private:
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
