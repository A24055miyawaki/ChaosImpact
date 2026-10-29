#include "ChaosImpactSessionSubsystem.h"

#include "ChaosImpact.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactLoadingScreen.h"
#include "ChaosImpactRelay.h"
#include "Dom/JsonObject.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Guid.h"
#include "Misc/Parse.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "TimerManager.h"

namespace
{
	const FName PasswordKey(TEXT("CIPASS"));
	const FName OpenKey(TEXT("CIOPEN"));
	const FName CountKey(TEXT("CICOUNT"));
	const FName RoomNameKey(TEXT("CINAME"));
	const FName HostNameKey(TEXT("CIHOST"));
	const FName SpectatorsKey(TEXT("CISPEC"));
	const TCHAR* ConfigSection = TEXT("ChaosImpact.Online");
	const TCHAR* NameConfigKey = TEXT("PlayerName");
	/** How often the room list is refreshed while it is shown. */
	constexpr float RoomListRefreshSeconds = 2.0f;
	const TCHAR* TokenConfigKey = TEXT("PlayerToken");
	/** The search asks the relay this often; a matched guest asks faster, for the host to be ready. */
	constexpr double QueuePollSeconds = 1.0;
	constexpr double QueueMatchedPollSeconds = 0.5;
	/** MATCH FOUND stays on screen this long before the match's room is opened or joined. */
	constexpr double QueueFoundHoldSeconds = 1.8;
	/** A ランクマッチ result is asked about this often while the relay waits for the other player's (up to ~90 s). */
	constexpr float RankReportRetrySeconds = 2.0f;
	constexpr int32 RankReportMaxTries = 45;

	const TCHAR* QueueModeKey(const EChaosImpactQueueMode Mode)
	{
		return Mode == EChaosImpactQueueMode::Rank ? TEXT("rank") : TEXT("random");
	}

	void ReadRankInfo(const TSharedPtr<FJsonObject>& From, FChaosImpactRankInfo& Info)
	{
		if (!From.IsValid())
		{
			return;
		}
		Info.bKnown = true;
		Info.Mmr = static_cast<int32>(From->GetNumberField(TEXT("mmr")));
		Info.Games = static_cast<int32>(From->GetNumberField(TEXT("games")));
		Info.Wins = static_cast<int32>(From->GetNumberField(TEXT("wins")));
		Info.Losses = static_cast<int32>(From->GetNumberField(TEXT("losses")));
		Info.Draws = static_cast<int32>(From->GetNumberField(TEXT("draws")));
		Info.Best = static_cast<int32>(From->GetNumberField(TEXT("best")));
		Info.Placement = static_cast<int32>(From->GetNumberField(TEXT("placement")));
		Info.Recent.Reset();
		const TArray<TSharedPtr<FJsonValue>>* Recent = nullptr;
		if (From->TryGetArrayField(TEXT("recent"), Recent))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Recent)
			{
				Info.Recent.Add(Value.IsValid() ? Value->AsString() : FString());
			}
		}
	}
}

void UChaosImpactSessionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	GeneratedName = FString::Printf(TEXT("プレイヤー%04d"), FMath::RandRange(0, 9999));
	if (GEngine)
	{
		NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(
			this, &UChaosImpactSessionSubsystem::HandleNetworkFailure);
		TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(
			this, &UChaosImpactSessionSubsystem::HandleTravelFailure);
	}
}

void UChaosImpactSessionSubsystem::Deinitialize()
{
	CancelRefresh();
	CancelQueue();
	if (RankReportTicker.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(RankReportTicker);
		RankReportTicker.Reset();
	}
	StopRelayHosting();
	if (GEngine)
	{
		GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
		GEngine->OnTravelFailure().Remove(TravelFailureHandle);
	}
	if (IOnlineSessionPtr Sessions = GetSessions())
	{
		Sessions->ClearOnFindSessionsCompleteDelegates(this);
		Sessions->ClearOnCreateSessionCompleteDelegates(this);
		Sessions->ClearOnJoinSessionCompleteDelegates(this);
	}
	Super::Deinitialize();
}

UChaosImpactSessionSubsystem* UChaosImpactSessionSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UChaosImpactSessionSubsystem>() : nullptr;
}

FString UChaosImpactSessionSubsystem::GetPlayerName() const
{
	FString Saved;
	if (GConfig && GConfig->GetString(ConfigSection, NameConfigKey, Saved, GGameUserSettingsIni)
		&& !Saved.TrimStartAndEnd().IsEmpty())
	{
		return Saved;
	}
	return GeneratedName;
}

bool UChaosImpactSessionSubsystem::HasSavedPlayerName() const
{
	FString Saved;
	return GConfig && GConfig->GetString(ConfigSection, NameConfigKey, Saved, GGameUserSettingsIni)
		&& !Saved.TrimStartAndEnd().IsEmpty();
}

void UChaosImpactSessionSubsystem::SetPlayerName(const FString& Name)
{
	const FString Trimmed = Name.TrimStartAndEnd().Left(MaxNameLength);
	if (Trimmed.IsEmpty() || !GConfig)
	{
		return;
	}
	GConfig->SetString(ConfigSection, NameConfigKey, *Trimmed, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}

FString UChaosImpactSessionSubsystem::GetDefaultRoomName() const
{
	return FString::Printf(TEXT("%sのへや"), *GetPlayerName()).Left(MaxRoomNameLength);
}

FString UChaosImpactSessionSubsystem::GetMachineToken() const
{
	if (!CachedMachineToken.IsEmpty())
	{
		return CachedMachineToken;
	}
	if (FParse::Value(FCommandLine::Get(), TEXT("CIMachineToken="), CachedMachineToken) && !CachedMachineToken.IsEmpty())
	{
		return CachedMachineToken;
	}
	// One per running game: leaving and coming back keeps it, while two copies of the game on one PC differ.
	CachedMachineToken = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	return CachedMachineToken;
}

FString UChaosImpactSessionSubsystem::GetTrainingMapName()
{
	return TEXT("/Game/ThirdPerson/Lvl_ThirdPerson");
}

FString UChaosImpactSessionSubsystem::GetOfflineTrainingOptions() const
{
	return FString::Printf(TEXT("CITraining=1?%s"), *LocalSetupOptions);
}

void UChaosImpactSessionSubsystem::SetLocalSetup(const FString& Options, const int32 LocalPlayers)
{
	LocalSetupOptions = Options;
	LocalPlayerCount = FMath::Clamp(LocalPlayers, 1, 2);
	UE_LOG(LogChaosImpact, Log, TEXT("Online local setup: %d player(s), %s"), LocalPlayerCount, *LocalSetupOptions);
}

IOnlineSessionPtr UChaosImpactSessionSubsystem::GetSessions() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	IOnlineSubsystem* OnlineSubsystem = Online::GetSubsystem(GameInstance ? GameInstance->GetWorld() : nullptr);
	return OnlineSubsystem ? OnlineSubsystem->GetSessionInterface() : nullptr;
}

void UChaosImpactSessionSubsystem::BindSessionDelegates()
{
	IOnlineSessionPtr Sessions = GetSessions();
	if (bSessionDelegatesBound || !Sessions)
	{
		return;
	}
	Sessions->AddOnFindSessionsCompleteDelegate_Handle(
		FOnFindSessionsCompleteDelegate::CreateUObject(this, &UChaosImpactSessionSubsystem::HandleFindComplete));
	Sessions->AddOnCreateSessionCompleteDelegate_Handle(
		FOnCreateSessionCompleteDelegate::CreateUObject(this, &UChaosImpactSessionSubsystem::HandleCreateComplete));
	Sessions->AddOnJoinSessionCompleteDelegate_Handle(
		FOnJoinSessionCompleteDelegate::CreateUObject(this, &UChaosImpactSessionSubsystem::HandleJoinComplete));
	bSessionDelegatesBound = true;
}

void UChaosImpactSessionSubsystem::CreateRoom(const FString& InPassword, const FString& InRoomName)
{
	CreateError.Reset();
	CancelQueue();
	MatchmadeMode = EChaosImpactQueueMode::None;
	MatchmadeOptions.Reset();
	if (IsInternet())
	{
		StopRelayHosting();
		Password = InPassword;
		const FString Trimmed = InRoomName.TrimStartAndEnd().Left(MaxRoomNameLength);
		RoomName = Trimmed.IsEmpty() ? GetDefaultRoomName() : Trimmed;
		State = EChaosImpactRoomState::Creating;
		RelayCreate();
		return;
	}
	IOnlineSessionPtr Sessions = GetSessions();
	if (!Sessions)
	{
		CreateError = TEXT("通信の準備ができていません");
		return;
	}
	BindSessionDelegates();
	if (Sessions->GetNamedSession(NAME_GameSession))
	{
		Sessions->DestroySession(NAME_GameSession);
	}
	Password = InPassword;
	const FString Trimmed = InRoomName.TrimStartAndEnd().Left(MaxRoomNameLength);
	RoomName = Trimmed.IsEmpty() ? GetDefaultRoomName() : Trimmed;
	State = EChaosImpactRoomState::Creating;
	// The password is shared by a group, so several rooms may use it; searchers pick from the list.
	CreateSessionNow();
}

void UChaosImpactSessionSubsystem::StartSearch(const FString& InPassword)
{
	if (IsInternet())
	{
		if (State == EChaosImpactRoomState::Searching && Password == InPassword)
		{
			return;
		}
		Password = InPassword;
		State = EChaosImpactRoomState::Searching;
		Listings.Reset();
		bSearchedOnce = false;
		bRelayUnreachableShown = false;
		++ListingsVersion;
		++RelayGeneration;
		FString AutoRoom;
		bDevAutoJoin = FParse::Value(FCommandLine::Get(), TEXT("CIAutoRoom="), AutoRoom) && AutoRoom.StartsWith(TEXT("search"));
		DevAutoJoinAt = 0.0;
		BeginFind();
		return;
	}
	if (!GetSessions())
	{
		PostNotice(TEXT("通信の準備ができていません"));
		return;
	}
	BindSessionDelegates();
	if (State == EChaosImpactRoomState::Searching && Password == InPassword && Search.IsValid()
		&& Search->SearchState == EOnlineAsyncTaskState::InProgress)
	{
		return;
	}
	Password = InPassword;
	State = EChaosImpactRoomState::Searching;
	Listings.Reset();
	bSearchedOnce = false;
	++ListingsVersion;
	FString AutoRoom;
	bDevAutoJoin = FParse::Value(FCommandLine::Get(), TEXT("CIAutoRoom="), AutoRoom) && AutoRoom.StartsWith(TEXT("search"));
	DevAutoJoinAt = 0.0;
	BeginFind();
}

void UChaosImpactSessionSubsystem::StopSearch()
{
	if (State != EChaosImpactRoomState::Searching && State != EChaosImpactRoomState::Joining)
	{
		return;
	}
	State = EChaosImpactRoomState::None;
	CancelRefresh();
	if (IOnlineSessionPtr Sessions = GetSessions(); Sessions && Search.IsValid()
		&& Search->SearchState == EOnlineAsyncTaskState::InProgress)
	{
		Sessions->CancelFindSessions();
	}
}

void UChaosImpactSessionSubsystem::ScheduleRefresh(const float DelaySeconds)
{
	CancelRefresh();
	RefreshTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this](float)
	{
		RefreshTicker.Reset();
		BeginFind();
		return false;
	}), DelaySeconds);
}

void UChaosImpactSessionSubsystem::CancelRefresh()
{
	if (RefreshTicker.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(RefreshTicker);
		RefreshTicker.Reset();
	}
}

void UChaosImpactSessionSubsystem::CancelCreate()
{
	CreateError.Reset();
	if (State != EChaosImpactRoomState::Creating)
	{
		return;
	}
	State = EChaosImpactRoomState::None;
	// (An internet room still being made is closed on the relay when its reply comes.)
	++RelayGeneration;
	if (IOnlineSessionPtr Sessions = GetSessions())
	{
		if (Sessions->GetNamedSession(NAME_GameSession))
		{
			Sessions->DestroySession(NAME_GameSession);
		}
	}
}

void UChaosImpactSessionSubsystem::BeginFind()
{
	if (IsInternet())
	{
		RelaySearch();
		return;
	}
	IOnlineSessionPtr Sessions = GetSessions();
	if (!Sessions || State != EChaosImpactRoomState::Searching)
	{
		return;
	}
	Search = MakeShared<FOnlineSessionSearch>();
	Search->bIsLanQuery = true;
	Search->MaxSearchResults = 64;
	if (!Sessions->FindSessions(0, Search.ToSharedRef()))
	{
		HandleFindComplete(false);
	}
}

void UChaosImpactSessionSubsystem::HandleFindComplete(const bool bWasSuccessful)
{
	if (State != EChaosImpactRoomState::Searching)
	{
		return;
	}
	TArray<FChaosImpactRoomListing> Found;
	if (Search.IsValid())
	{
		for (const FOnlineSessionSearchResult& Result : Search->SearchResults)
		{
			FString ResultPassword;
			if (!Result.Session.SessionSettings.Get(PasswordKey, ResultPassword) || ResultPassword != Password)
			{
				continue;
			}
			FChaosImpactRoomListing& Listing = Found.AddDefaulted_GetRef();
			int32 Open = 1;
			Result.Session.SessionSettings.Get(RoomNameKey, Listing.RoomName);
			Result.Session.SessionSettings.Get(HostNameKey, Listing.HostName);
			Result.Session.SessionSettings.Get(CountKey, Listing.Members);
			Result.Session.SessionSettings.Get(OpenKey, Open);
			Result.Session.SessionSettings.Get(SpectatorsKey, Listing.Spectators);
			Listing.bOpen = Open != 0;
			Listing.PingMs = FMath::Max(0, Result.PingInMs);
			Listing.Result = Result;
			if (Listing.RoomName.IsEmpty())
			{
				Listing.RoomName = TEXT("へや");
			}
		}
	}
	UE_LOG(LogChaosImpact, Log, TEXT("LAN room search finished: success=%d rooms with the password=%d (of %d)"),
		bWasSuccessful, Found.Num(), Search.IsValid() ? Search->SearchResults.Num() : -1);
	FinishSearch(MoveTemp(Found), bWasSuccessful);
}

void UChaosImpactSessionSubsystem::FinishSearch(TArray<FChaosImpactRoomListing>&& Found, const bool bWasSuccessful)
{
	// Rooms that can be joined first, then the best connection.
	Found.StableSort([](const FChaosImpactRoomListing& A, const FChaosImpactRoomListing& B)
	{
		return A.bOpen != B.bOpen ? A.bOpen : A.PingMs < B.PingMs;
	});
	Listings = MoveTemp(Found);
	(void)bWasSuccessful;
	bSearchedOnce = true;
	++ListingsVersion;

	if (bDevAutoJoin && !Listings.IsEmpty() && GetJoinBlocker(Listings[0]).IsEmpty() && GetGameInstance())
	{
		const double Now = FPlatformTime::Seconds();
		// Development: stay on the list for a moment (for screenshots), then take the first open room.
		if (DevAutoJoinAt <= 0.0)
		{
			DevAutoJoinAt = Now + 3.0;
		}
		else if (Now >= DevAutoJoinAt)
		{
			bDevAutoJoin = false;
			JoinRoomListing(0);
			return;
		}
	}
	ScheduleRefresh(RoomListRefreshSeconds);
}

bool UChaosImpactSessionSubsystem::JoinRoomListing(const int32 Index)
{
	IOnlineSessionPtr Sessions = GetSessions();
	if (State != EChaosImpactRoomState::Searching || !Listings.IsValidIndex(Index))
	{
		return false;
	}
	const FChaosImpactRoomListing Listing = Listings[Index];
	if (const FString Blocker = GetJoinBlocker(Listing); !Blocker.IsEmpty())
	{
		PostNotice(LocalPlayerCount > 1 && WouldJoinAsSpectator(Listing)
			? TEXT("観戦は1人で参加してください")
			: Listing.Spectators >= AChaosImpactGameState::MaxSpectators ? TEXT("観戦の枠がいっぱいです")
			: TEXT("へやが満員です"));
		return false;
	}
	bJoinAsSpectator = WouldJoinAsSpectator(Listing);
	CancelRefresh();
	if (Listing.RelayPort > 0)
	{
		// An internet room: straight to its port on the relay, which carries everything to the host.
		FString Host;
		FString PortText;
		const FString Server = ChaosImpactRelay::GetServer();
		if (!Server.Split(TEXT(":"), &Host, &PortText, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
		{
			Host = Server;
		}
		++RelayGeneration;
		RoomName = Listing.RoomName;
		State = EChaosImpactRoomState::Joining;
		UE_LOG(LogChaosImpact, Log, TEXT("Joining internet room \"%s\" hosted by %s%s"), *Listing.RoomName, *Listing.HostName,
			bJoinAsSpectator ? TEXT(" as a spectator") : TEXT(""));
		TravelToRoom(FString::Printf(TEXT("%s:%d"), *Host, Listing.RelayPort));
		return true;
	}
	if (!Sessions)
	{
		return false;
	}
	if (Search.IsValid() && Search->SearchState == EOnlineAsyncTaskState::InProgress)
	{
		Sessions->CancelFindSessions();
	}
	if (Sessions->GetNamedSession(NAME_GameSession))
	{
		Sessions->DestroySession(NAME_GameSession);
	}
	State = EChaosImpactRoomState::Joining;
	RoomName = Listing.RoomName;
	UE_LOG(LogChaosImpact, Log, TEXT("Joining listed room \"%s\" hosted by %s%s"), *Listing.RoomName, *Listing.HostName,
		bJoinAsSpectator ? TEXT(" as a spectator") : TEXT(""));
	if (!Sessions->JoinSession(0, NAME_GameSession, Listing.Result))
	{
		HandleJoinComplete(NAME_GameSession, EOnJoinSessionCompleteResult::UnknownError);
		return false;
	}
	return true;
}

bool UChaosImpactSessionSubsystem::WouldJoinAsSpectator(const FChaosImpactRoomListing& Listing) const
{
	// Everyone playing on this machine needs a place (a room of 7 cannot take a pair).
	return !Listing.bOpen || Listing.Members + LocalPlayerCount > AChaosImpactGameState::MaxMembers;
}

FString UChaosImpactSessionSubsystem::GetJoinBlocker(const FChaosImpactRoomListing& Listing) const
{
	if (!WouldJoinAsSpectator(Listing))
	{
		return FString();
	}
	// Watching is for one player on one screen, and a room takes only so many spectators.
	if (LocalPlayerCount > 1)
	{
		return Listing.bOpen ? TEXT("満員") : TEXT("締切");
	}
	return Listing.Spectators >= AChaosImpactGameState::MaxSpectators ? TEXT("満員") : FString();
}

void UChaosImpactSessionSubsystem::CreateSessionNow()
{
	IOnlineSessionPtr Sessions = GetSessions();
	if (!Sessions)
	{
		State = EChaosImpactRoomState::None;
		CreateError = TEXT("へやをつくれませんでした");
		return;
	}
	FOnlineSessionSettings Settings;
	Settings.bIsLANMatch = true;
	Settings.bUsesPresence = false;
	Settings.bShouldAdvertise = true;
	Settings.bAllowJoinInProgress = true;
	Settings.bAllowJoinViaPresence = false;
	Settings.bUseLobbiesIfAvailable = false;
	Settings.NumPublicConnections = AChaosImpactGameState::MaxMembers + AChaosImpactGameState::MaxSpectators;
	Settings.Set(PasswordKey, Password, EOnlineDataAdvertisementType::ViaOnlineService);
	Settings.Set(OpenKey, 1, EOnlineDataAdvertisementType::ViaOnlineService);
	Settings.Set(CountKey, 1, EOnlineDataAdvertisementType::ViaOnlineService);
	Settings.Set(SpectatorsKey, 0, EOnlineDataAdvertisementType::ViaOnlineService);
	Settings.Set(RoomNameKey, RoomName, EOnlineDataAdvertisementType::ViaOnlineService);
	Settings.Set(HostNameKey, GetPlayerName(), EOnlineDataAdvertisementType::ViaOnlineService);
	if (!Sessions->CreateSession(0, NAME_GameSession, Settings))
	{
		HandleCreateComplete(NAME_GameSession, false);
	}
}

void UChaosImpactSessionSubsystem::HandleCreateComplete(FName SessionName, const bool bWasSuccessful)
{
	UE_LOG(LogChaosImpact, Log, TEXT("Room create finished: success=%d state=%d name=%s"), bWasSuccessful,
		static_cast<int32>(State), *RoomName);
	if (State != EChaosImpactRoomState::Creating)
	{
		return;
	}
	if (!bWasSuccessful)
	{
		State = EChaosImpactRoomState::None;
		CreateError = TEXT("へやをつくれませんでした");
		return;
	}
	OpenRoomAsHost();
}

void UChaosImpactSessionSubsystem::OpenRoomAsHost()
{
	State = EChaosImpactRoomState::Hosting;
	MemberCount = 1;
	SpectatorCount = 0;
	bRecruitmentOpen = true;
	FString Options = FString::Printf(TEXT("listen?CITraining=1?CIOnline=1?%s%s"), *LocalSetupOptions, *MatchmadeOptions);
	// Development: -CIRoomCPU=N adds CPUs to the room so network hits can be tested without a second person.
	int32 DevRoomCPUs = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("CIRoomCPU="), DevRoomCPUs) && DevRoomCPUs > 0)
	{
		Options += FString::Printf(TEXT("?CICPUCount=%d"), FMath::Clamp(DevRoomCPUs, 1, 4));
	}
	if (UChaosImpactLoadingSubsystem* Loading = GetGameInstance()->GetSubsystem<UChaosImpactLoadingSubsystem>())
	{
		Loading->BeginTravel(EChaosImpactLoadingKind::RoomCreate);
	}
	UGameplayStatics::OpenLevel(GetGameInstance(), FName(*GetTrainingMapName()), true, Options);
}

void UChaosImpactSessionSubsystem::HandleJoinComplete(FName SessionName,
	const EOnJoinSessionCompleteResult::Type Result)
{
	if (State != EChaosImpactRoomState::Joining)
	{
		return;
	}
	IOnlineSessionPtr Sessions = GetSessions();
	FString ConnectString;
	APlayerController* Controller = GetGameInstance() ? GetGameInstance()->GetFirstLocalPlayerController() : nullptr;
	if (Result != EOnJoinSessionCompleteResult::Success || !Sessions || !Controller
		|| !Sessions->GetResolvedConnectString(NAME_GameSession, ConnectString))
	{
		// Back to the list, which keeps refreshing.
		PostNotice(TEXT("へやに入れませんでした"));
		State = EChaosImpactRoomState::Searching;
		BeginFind();
		return;
	}
	TravelToRoom(ConnectString);
}

void UChaosImpactSessionSubsystem::TravelToRoom(const FString& ConnectString)
{
	APlayerController* Controller = GetGameInstance() ? GetGameInstance()->GetFirstLocalPlayerController() : nullptr;
	if (!Controller)
	{
		PostNotice(TEXT("へやに入れませんでした"));
		State = EChaosImpactRoomState::Searching;
		BeginFind();
		return;
	}
	State = EChaosImpactRoomState::InRoom;
	UE_LOG(LogChaosImpact, Log, TEXT("Joining room at %s"), *ConnectString);
	// CIPlayers lets the host reserve a place for this machine's second player, who joins right after.
	// CIMachine lets the host replace this machine's old place if it is still there after a drop.
	// The local setup options also let this client pair its controllers again in the host's world.
	// CISpectator: this machine watches (mid-match or in a closed room) and never plays until it switches in the lobby.
	if (UChaosImpactLoadingSubsystem* Loading = GetGameInstance()->GetSubsystem<UChaosImpactLoadingSubsystem>())
	{
		Loading->BeginTravel(EChaosImpactLoadingKind::RoomJoin);
	}
	Controller->ClientTravel(FString::Printf(TEXT("%s?CIOnline=1?CIPlayers=%d?CIMachine=%s?%s%s"),
		*ConnectString, bJoinAsSpectator ? 1 : LocalPlayerCount, *GetMachineToken(), *LocalSetupOptions,
		bJoinAsSpectator ? TEXT("?CISpectator=1") : TEXT("")), TRAVEL_Absolute);
}

void UChaosImpactSessionSubsystem::LeaveRoom()
{
	// Leaving a ランクマッチ that is under way gives it to the other player.
	if (MatchmadeMode == EChaosImpactQueueMode::Rank && bMatchmadeStarted)
	{
		ReportRankResult(TEXT("lose"));
	}
	MatchmadeMode = EChaosImpactQueueMode::None;
	MatchmadeOptions.Reset();
	bMatchmadeStarted = false;
	CancelQueue();
	StopSearch();
	StopRelayHosting();
	State = EChaosImpactRoomState::None;
	if (IOnlineSessionPtr Sessions = GetSessions(); Sessions && Sessions->GetNamedSession(NAME_GameSession))
	{
		Sessions->DestroySession(NAME_GameSession);
	}
	if (UChaosImpactLoadingSubsystem* Loading = GetGameInstance()->GetSubsystem<UChaosImpactLoadingSubsystem>())
	{
		Loading->BeginTravel(EChaosImpactLoadingKind::RoomLeave);
	}
	UGameplayStatics::OpenLevel(GetGameInstance(), FName(*GetTrainingMapName()), true,
		GetOfflineTrainingOptions());
}

void UChaosImpactSessionSubsystem::SetRecruitmentOpen(const bool bOpen)
{
	bRecruitmentOpen = bOpen;
	PublishRoomSettings();
}

void UChaosImpactSessionSubsystem::SetMemberCount(const int32 Count)
{
	MemberCount = Count;
	PublishRoomSettings();
}

void UChaosImpactSessionSubsystem::SetSpectatorCount(const int32 Count)
{
	if (SpectatorCount != Count)
	{
		SpectatorCount = Count;
		PublishRoomSettings();
	}
}

void UChaosImpactSessionSubsystem::SetRoomName(const FString& Name)
{
	const FString Trimmed = Name.TrimStartAndEnd().Left(MaxRoomNameLength);
	if (Trimmed.IsEmpty())
	{
		return;
	}
	RoomName = Trimmed;
	PublishRoomSettings();
}

void UChaosImpactSessionSubsystem::PublishRoomSettings()
{
	if (!RelayRoomId.IsEmpty())
	{
		RelayHeartbeat();
		return;
	}
	IOnlineSessionPtr Sessions = GetSessions();
	FOnlineSessionSettings* Current = Sessions ? Sessions->GetSessionSettings(NAME_GameSession) : nullptr;
	if (State != EChaosImpactRoomState::Hosting || !Current)
	{
		return;
	}
	FOnlineSessionSettings Updated = *Current;
	Updated.Set(OpenKey, bRecruitmentOpen ? 1 : 0, EOnlineDataAdvertisementType::ViaOnlineService);
	Updated.Set(CountKey, MemberCount, EOnlineDataAdvertisementType::ViaOnlineService);
	Updated.Set(SpectatorsKey, SpectatorCount, EOnlineDataAdvertisementType::ViaOnlineService);
	Updated.Set(RoomNameKey, RoomName, EOnlineDataAdvertisementType::ViaOnlineService);
	Sessions->UpdateSession(NAME_GameSession, Updated, true);
}

void UChaosImpactSessionSubsystem::PostNotice(const FString& Message)
{
	Notice = Message;
	NoticeAt = FPlatformTime::Seconds();
	UE_LOG(LogChaosImpact, Log, TEXT("Room notice: %s"), *Message);
}

bool UChaosImpactSessionSubsystem::ConsumeReturnToTraining()
{
	const bool bReturn = bReturnToTraining;
	bReturnToTraining = false;
	return bReturn;
}

void UChaosImpactSessionSubsystem::HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver,
	ENetworkFailure::Type FailureType, const FString& ErrorString)
{
	ReturnAfterDisconnect(ErrorString);
}

void UChaosImpactSessionSubsystem::HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType,
	const FString& ErrorString)
{
	ReturnAfterDisconnect(ErrorString);
}

void UChaosImpactSessionSubsystem::ReturnAfterDisconnect(const FString& ErrorString)
{
	if (State != EChaosImpactRoomState::InRoom && State != EChaosImpactRoomState::Joining)
	{
		return;
	}
	UE_LOG(LogChaosImpact, Log, TEXT("Left online room: %s"), *ErrorString);
	if (MatchmadeMode != EChaosImpactQueueMode::None)
	{
		// A matched room: the other side went away. Mid-ランクマッチ that is this player's win (the host reports it too).
		if (MatchmadeMode == EChaosImpactQueueMode::Rank && bMatchmadeStarted)
		{
			ReportRankResult(TEXT("win"));
		}
		PostNotice(bMatchmadeStarted ? TEXT("対戦相手との接続が切れました") : TEXT("対戦相手と接続できませんでした"));
		ShowQueueMenu = MatchmadeMode;
		MatchmadeMode = EChaosImpactQueueMode::None;
		MatchmadeOptions.Reset();
		bMatchmadeStarted = false;
		State = EChaosImpactRoomState::None;
		bReturnToTraining = true;
		return;
	}
	PostNotice(ErrorString.Contains(TEXT("CISPECFULL")) ? TEXT("観戦の枠がいっぱいです")
		: ErrorString.Contains(TEXT("CIFULL")) ? TEXT("へやが満員です")
		: ErrorString.Contains(TEXT("CICLOSED")) ? TEXT("メンバー募集が終了しています")
		: TEXT("へやが解散しました"));
	State = EChaosImpactRoomState::None;
	if (IOnlineSessionPtr Sessions = GetSessions(); Sessions && Sessions->GetNamedSession(NAME_GameSession))
	{
		Sessions->DestroySession(NAME_GameSession);
	}
	// The engine browses to the default (title) map after a failure; the title world then
	// sends the player straight back to their own training arena.
	bReturnToTraining = true;
}

// ---------------------------------------------------------------------------------------------------- Internet rooms

bool UChaosImpactSessionSubsystem::IsInternet() const
{
	return ChaosImpactRelay::IsEnabled();
}

bool UChaosImpactSessionSubsystem::IsRelayConnected() const
{
	return Tunnel.IsValid() && Tunnel->IsConnected();
}

void UChaosImpactSessionSubsystem::RelayCreate()
{
	const int32 Generation = ++RelayGeneration;
	TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
	Message->SetStringField(TEXT("op"), TEXT("create"));
	Message->SetStringField(TEXT("ver"), ChaosImpactRelay::GetNetVersion());
	Message->SetStringField(TEXT("password"), Password);
	Message->SetStringField(TEXT("name"), RoomName);
	Message->SetStringField(TEXT("host"), GetPlayerName());
	ChaosImpactRelay::Request(Message, [WeakThis = TWeakObjectPtr<UChaosImpactSessionSubsystem>(this), Generation](
		TSharedPtr<FJsonObject> Reply, int32)
	{
		UChaosImpactSessionSubsystem* Self = WeakThis.Get();
		if (!Self)
		{
			return;
		}
		const bool bOk = Reply.IsValid() && Reply->GetBoolField(TEXT("ok"));
		if (Generation != Self->RelayGeneration || (Self->State != EChaosImpactRoomState::Creating && Self->State != EChaosImpactRoomState::Hosting))
		{
			// Given up on meanwhile: the room made for it is closed again.
			if (bOk)
			{
				TSharedRef<FJsonObject> Close = MakeShared<FJsonObject>();
				Close->SetStringField(TEXT("op"), TEXT("close"));
				Close->SetStringField(TEXT("room"), Reply->GetStringField(TEXT("room")));
				Close->SetStringField(TEXT("key"), Reply->GetStringField(TEXT("key")));
				ChaosImpactRelay::Request(Close, [](TSharedPtr<FJsonObject>, int32) {});
			}
			return;
		}
		if (!bOk)
		{
			const FString Error = Reply.IsValid() ? Reply->GetStringField(TEXT("error")) : FString();
			UE_LOG(LogChaosImpact, Warning, TEXT("Internet room not made: %s"), Reply.IsValid() ? *Error : TEXT("no answer from the relay"));
			if (Self->State == EChaosImpactRoomState::Hosting)
			{
				Self->PostNotice(TEXT("中継サーバーとの接続が切れました"));
				return;
			}
			Self->State = EChaosImpactRoomState::None;
			Self->CreateError = !Reply.IsValid() ? TEXT("中継サーバーにつながりません")
				: Error == TEXT("full") ? TEXT("へやがいっぱいでつくれません") : TEXT("へやをつくれませんでした");
			return;
		}
		Self->RelayRoomId = Reply->GetStringField(TEXT("room"));
		Self->RelayRoomKey = Reply->GetStringField(TEXT("key"));
		Self->RelayRoomPort = static_cast<int32>(Reply->GetNumberField(TEXT("port")));
		UE_LOG(LogChaosImpact, Log, TEXT("Internet room %s made on relay port %d"), *Self->RelayRoomId, Self->RelayRoomPort);
		Self->StartTunnel();
		if (!Self->HeartbeatTicker.IsValid())
		{
			Self->HeartbeatTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(Self, [Self](float)
			{
				Self->RelayHeartbeat();
				return true;
			}), 3.0f);
		}
		if (Self->State == EChaosImpactRoomState::Creating)
		{
			Self->OpenRoomAsHost();
		}
		else
		{
			// Made again after the relay lost it (restarted): the same room, at a new port.
			Self->RelayHeartbeat();
		}
	});
}

void UChaosImpactSessionSubsystem::StartTunnel()
{
	FString Host;
	FString PortText;
	const FString Server = ChaosImpactRelay::GetServer();
	if (!Server.Split(TEXT(":"), &Host, &PortText, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
	{
		Host = Server;
	}
	Tunnel.Reset();
	// This game's listen server takes the room's players on the usual game port.
	Tunnel = MakeUnique<FChaosImpactRelayTunnel>(Host, RelayRoomPort, RelayRoomKey, FURL::UrlConfig.DefaultPort);
	if (!Tunnel->Start())
	{
		Tunnel.Reset();
		PostNotice(TEXT("中継サーバーにつながりません"));
	}
}

void UChaosImpactSessionSubsystem::RelayHeartbeat()
{
	if (RelayRoomId.IsEmpty() || State != EChaosImpactRoomState::Hosting)
	{
		return;
	}
	TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
	Message->SetStringField(TEXT("op"), TEXT("update"));
	Message->SetStringField(TEXT("room"), RelayRoomId);
	Message->SetStringField(TEXT("key"), RelayRoomKey);
	Message->SetNumberField(TEXT("members"), MemberCount);
	Message->SetNumberField(TEXT("spectators"), SpectatorCount);
	Message->SetBoolField(TEXT("open"), bRecruitmentOpen);
	Message->SetStringField(TEXT("name"), RoomName);
	const FString Room = RelayRoomId;
	ChaosImpactRelay::Request(Message, [WeakThis = TWeakObjectPtr<UChaosImpactSessionSubsystem>(this), Room](
		TSharedPtr<FJsonObject> Reply, int32)
	{
		UChaosImpactSessionSubsystem* Self = WeakThis.Get();
		if (!Self || Self->RelayRoomId != Room || Self->State != EChaosImpactRoomState::Hosting || !Reply.IsValid())
		{
			return;
		}
		if (!Reply->GetBoolField(TEXT("ok")))
		{
			// The relay no longer knows the room (it restarted): make it again.
			UE_LOG(LogChaosImpact, Warning, TEXT("Internet room %s lost by the relay: making it again"), *Room);
			Self->RelayRoomId.Reset();
			Self->RelayCreate();
		}
	});
}

void UChaosImpactSessionSubsystem::StopRelayHosting()
{
	if (HeartbeatTicker.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(HeartbeatTicker);
		HeartbeatTicker.Reset();
	}
	Tunnel.Reset();
	if (!RelayRoomId.IsEmpty())
	{
		TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
		Message->SetStringField(TEXT("op"), TEXT("close"));
		Message->SetStringField(TEXT("room"), RelayRoomId);
		Message->SetStringField(TEXT("key"), RelayRoomKey);
		ChaosImpactRelay::Request(Message, [](TSharedPtr<FJsonObject>, int32) {});
		UE_LOG(LogChaosImpact, Log, TEXT("Internet room %s closed"), *RelayRoomId);
	}
	RelayRoomId.Reset();
	RelayRoomKey.Reset();
	RelayRoomPort = 0;
}

void UChaosImpactSessionSubsystem::RelaySearch()
{
	if (State != EChaosImpactRoomState::Searching || bRelaySearchInFlight)
	{
		return;
	}
	bRelaySearchInFlight = true;
	const int32 Generation = RelayGeneration;
	TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
	Message->SetStringField(TEXT("op"), TEXT("search"));
	Message->SetStringField(TEXT("ver"), ChaosImpactRelay::GetNetVersion());
	Message->SetStringField(TEXT("password"), Password);
	ChaosImpactRelay::Request(Message, [WeakThis = TWeakObjectPtr<UChaosImpactSessionSubsystem>(this), Generation](
		TSharedPtr<FJsonObject> Reply, const int32 RoundTripMs)
	{
		UChaosImpactSessionSubsystem* Self = WeakThis.Get();
		if (!Self)
		{
			return;
		}
		Self->bRelaySearchInFlight = false;
		if (Generation != Self->RelayGeneration || Self->State != EChaosImpactRoomState::Searching)
		{
			return;
		}
		TArray<FChaosImpactRoomListing> Found;
		const TArray<TSharedPtr<FJsonValue>>* Rooms = nullptr;
		if (Reply.IsValid() && Reply->TryGetArrayField(TEXT("rooms"), Rooms))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Rooms)
			{
				const TSharedPtr<FJsonObject> Room = Value.IsValid() ? Value->AsObject() : nullptr;
				if (!Room.IsValid())
				{
					continue;
				}
				FChaosImpactRoomListing& Listing = Found.AddDefaulted_GetRef();
				Listing.RoomName = Room->GetStringField(TEXT("name"));
				Listing.HostName = Room->GetStringField(TEXT("host"));
				Listing.Members = static_cast<int32>(Room->GetNumberField(TEXT("members")));
				Listing.Spectators = static_cast<int32>(Room->GetNumberField(TEXT("spectators")));
				Listing.bOpen = Room->GetBoolField(TEXT("open"));
				Listing.RelayPort = static_cast<int32>(Room->GetNumberField(TEXT("port")));
				// (The round trip to the relay; the host's own leg adds to it.)
				Listing.PingMs = RoundTripMs;
				if (Listing.RoomName.IsEmpty())
				{
					Listing.RoomName = TEXT("へや");
				}
			}
		}
		else if (!Self->bRelayUnreachableShown)
		{
			Self->bRelayUnreachableShown = true;
			Self->PostNotice(TEXT("中継サーバーにつながりません"));
		}
		UE_LOG(LogChaosImpact, Log, TEXT("Internet room search finished: answered=%d rooms=%d (%d ms)"), Reply.IsValid(), Found.Num(),
			RoundTripMs);
		Self->FinishSearch(MoveTemp(Found), Reply.IsValid());
	});
}

// ---------------------------------------------------------------------------------------------------- Matchmaking

FString UChaosImpactSessionSubsystem::GetPlayerToken() const
{
	if (!CachedPlayerToken.IsEmpty())
	{
		return CachedPlayerToken;
	}
	if (FParse::Value(FCommandLine::Get(), TEXT("CIPlayerToken="), CachedPlayerToken) && !CachedPlayerToken.IsEmpty())
	{
		return CachedPlayerToken;
	}
	if (GConfig && GConfig->GetString(ConfigSection, TokenConfigKey, CachedPlayerToken, GGameUserSettingsIni)
		&& CachedPlayerToken.Len() >= 16)
	{
		return CachedPlayerToken;
	}
	// Made once per PC: the relay keeps this player's rank under it.
	CachedPlayerToken = FGuid::NewGuid().ToString(EGuidFormats::Digits) + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	if (GConfig)
	{
		GConfig->SetString(ConfigSection, TokenConfigKey, *CachedPlayerToken, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
	return CachedPlayerToken;
}

bool UChaosImpactSessionSubsystem::StartQueue(const EChaosImpactQueueMode Mode)
{
	if (Mode == EChaosImpactQueueMode::None)
	{
		return false;
	}
	if (!IsInternet())
	{
		PostNotice(TEXT("インターネット対戦の中継サーバーが設定されていません"));
		return false;
	}
	if (Mode == EChaosImpactQueueMode::Rank && LocalPlayerCount > 1)
	{
		PostNotice(TEXT("ランクマッチは1人で参加してください"));
		return false;
	}
	if (State == EChaosImpactRoomState::Hosting || State == EChaosImpactRoomState::InRoom
		|| State == EChaosImpactRoomState::Joining || State == EChaosImpactRoomState::Creating)
	{
		return false;
	}
	StopSearch();
	CancelQueue();
	QueueMode = Mode;
	QueueStartedAt = FPlatformTime::Seconds();
	QueueWaiting = LocalPlayerCount;
	QueueStartsIn = -1.0f;
	QueueFailures = 0;
	ResetQueueMatch();
	QueueJoin();
	QueueTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this](float)
	{
		QueueTick();
		return true;
	}), 0.25f);
	UE_LOG(LogChaosImpact, Log, TEXT("Queue %s: searching (%d player(s) on this machine)"), QueueModeKey(Mode), LocalPlayerCount);
	return true;
}

void UChaosImpactSessionSubsystem::CancelQueue()
{
	if (QueueMode == EChaosImpactQueueMode::None)
	{
		return;
	}
	if (!QueueTicket.IsEmpty())
	{
		TSharedRef<FJsonObject> Leave = MakeShared<FJsonObject>();
		Leave->SetStringField(TEXT("op"), TEXT("mm_leave"));
		Leave->SetStringField(TEXT("ticket"), QueueTicket);
		ChaosImpactRelay::Request(Leave, [](TSharedPtr<FJsonObject>, int32) {});
	}
	// A match found but not yet opened as its host: the room on the relay goes too.
	if (bQueueMatched && bQueueHost && State == EChaosImpactRoomState::Creating)
	{
		StopRelayHosting();
		MatchmadeMode = EChaosImpactQueueMode::None;
		MatchmadeOptions.Reset();
		State = EChaosImpactRoomState::None;
	}
	UE_LOG(LogChaosImpact, Log, TEXT("Queue %s: stopped"), QueueModeKey(QueueMode));
	QueueMode = EChaosImpactQueueMode::None;
	QueueTicket.Reset();
	ResetQueueMatch();
	StopQueueTicker();
	++QueueGeneration;
}

void UChaosImpactSessionSubsystem::StopQueueTicker()
{
	if (QueueTicker.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(QueueTicker);
		QueueTicker.Reset();
	}
}

void UChaosImpactSessionSubsystem::ResetQueueMatch()
{
	bQueueMatched = false;
	bQueueHost = false;
	bQueueHostReady = false;
	bQueueRequestInFlight = false;
	QueuePort = 0;
	QueueMatchId.Reset();
	QueueExpected = 0;
	QueueNames.Reset();
	QueueOpponentName.Reset();
	QueueOpponentRank = FChaosImpactRankInfo();
	QueueMatchedAt = 0.0;
}

void UChaosImpactSessionSubsystem::QueueJoin()
{
	const int32 Generation = ++QueueGeneration;
	bQueueRequestInFlight = true;
	QueueLastPollAt = FPlatformTime::Seconds();
	TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
	Message->SetStringField(TEXT("op"), TEXT("mm_join"));
	Message->SetStringField(TEXT("ver"), ChaosImpactRelay::GetNetVersion());
	Message->SetStringField(TEXT("mode"), QueueModeKey(QueueMode));
	Message->SetStringField(TEXT("token"), GetPlayerToken());
	Message->SetStringField(TEXT("name"), GetPlayerName());
	Message->SetNumberField(TEXT("players"), LocalPlayerCount);
	Message->SetNumberField(TEXT("rtt"), LastRelayRtt > 0 ? LastRelayRtt : 999);
	ChaosImpactRelay::Request(Message, [WeakThis = TWeakObjectPtr<UChaosImpactSessionSubsystem>(this), Generation](
		TSharedPtr<FJsonObject> Reply, const int32 RoundTripMs)
	{
		UChaosImpactSessionSubsystem* Self = WeakThis.Get();
		if (!Self || Generation != Self->QueueGeneration)
		{
			return;
		}
		Self->bQueueRequestInFlight = false;
		if (!Reply.IsValid() || !Reply->GetBoolField(TEXT("ok")))
		{
			const FString Error = Reply.IsValid() ? Reply->GetStringField(TEXT("error")) : FString();
			Self->PostNotice(!Reply.IsValid() ? TEXT("中継サーバーにつながりません")
				: Error == TEXT("solo only") ? TEXT("ランクマッチは1人で参加してください") : TEXT("マッチングを始められませんでした"));
			Self->CancelQueue();
			return;
		}
		Self->LastRelayRtt = RoundTripMs;
		Self->QueueTicket = Reply->GetStringField(TEXT("ticket"));
	});
}

void UChaosImpactSessionSubsystem::QueueTick()
{
	if (QueueMode == EChaosImpactQueueMode::None)
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	if (bQueueMatched && Now - QueueMatchedAt >= QueueFoundHoldSeconds)
	{
		if (bQueueHost && State == EChaosImpactRoomState::Creating)
		{
			// MATCH FOUND has been shown: this machine opens the match's room and the others come to it.
			UE_LOG(LogChaosImpact, Log, TEXT("Match %s: opening its room as the host (%d expected)"), *QueueMatchId, QueueExpected);
			QueueMode = EChaosImpactQueueMode::None;
			QueueTicket.Reset();
			StopQueueTicker();
			OpenRoomAsHost();
			return;
		}
		if (!bQueueHost && bQueueHostReady)
		{
			FString Host;
			FString PortText;
			const FString Server = ChaosImpactRelay::GetServer();
			if (!Server.Split(TEXT(":"), &Host, &PortText, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
			{
				Host = Server;
			}
			UE_LOG(LogChaosImpact, Log, TEXT("Match %s: joining the host's room on relay port %d"), *QueueMatchId, QueuePort);
			MatchmadeMode = QueueMode;
			MatchmadeId = QueueMatchId;
			bMatchmadeStarted = false;
			const int32 Port = QueuePort;
			QueueMode = EChaosImpactQueueMode::None;
			QueueTicket.Reset();
			StopQueueTicker();
			bJoinAsSpectator = false;
			RoomName = MatchmadeMode == EChaosImpactQueueMode::Rank ? TEXT("ランクマッチ") : TEXT("ランダムマッチ");
			State = EChaosImpactRoomState::Joining;
			++RelayGeneration;
			TravelToRoom(FString::Printf(TEXT("%s:%d"), *Host, Port));
			return;
		}
	}
	const double Interval = bQueueMatched ? QueueMatchedPollSeconds : QueuePollSeconds;
	if (!bQueueRequestInFlight && !QueueTicket.IsEmpty() && Now - QueueLastPollAt >= Interval
		&& !(bQueueMatched && bQueueHost))
	{
		QueuePoll();
	}
}

void UChaosImpactSessionSubsystem::QueuePoll()
{
	const int32 Generation = QueueGeneration;
	bQueueRequestInFlight = true;
	QueueLastPollAt = FPlatformTime::Seconds();
	TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
	Message->SetStringField(TEXT("op"), TEXT("mm_poll"));
	Message->SetStringField(TEXT("ticket"), QueueTicket);
	if (LastRelayRtt > 0)
	{
		Message->SetNumberField(TEXT("rtt"), LastRelayRtt);
	}
	ChaosImpactRelay::Request(Message, [WeakThis = TWeakObjectPtr<UChaosImpactSessionSubsystem>(this), Generation](
		TSharedPtr<FJsonObject> Reply, const int32 RoundTripMs)
	{
		UChaosImpactSessionSubsystem* Self = WeakThis.Get();
		if (!Self || Generation != Self->QueueGeneration || Self->QueueMode == EChaosImpactQueueMode::None)
		{
			return;
		}
		Self->bQueueRequestInFlight = false;
		if (!Reply.IsValid())
		{
			// A lost answer or two is nothing; a relay gone for good ends the search.
			if (++Self->QueueFailures >= 4)
			{
				Self->PostNotice(TEXT("中継サーバーとの接続が切れました"));
				Self->CancelQueue();
			}
			return;
		}
		Self->QueueFailures = 0;
		Self->LastRelayRtt = RoundTripMs;
		const FString Step = Reply->GetStringField(TEXT("state"));
		if (Step == TEXT("waiting"))
		{
			Self->QueueWaiting = static_cast<int32>(Reply->GetNumberField(TEXT("waiting")));
			Self->QueueStartsIn = static_cast<float>(Reply->GetNumberField(TEXT("starts_in")));
		}
		else if (Step == TEXT("matched"))
		{
			if (!Self->bQueueMatched)
			{
				Self->HandleQueueMatched(Reply);
			}
			Self->bQueueHostReady = Reply->GetBoolField(TEXT("host_ready"));
		}
		else
		{
			// Gone: the ticket expired, or the match fell through before it began. Search again.
			const EChaosImpactQueueMode Mode = Self->QueueMode;
			const bool bWasMatched = Self->bQueueMatched;
			UE_LOG(LogChaosImpact, Log, TEXT("Queue %s: the relay dropped this search (%s); searching again"), QueueModeKey(Mode),
				bWasMatched ? TEXT("the match fell through") : TEXT("expired"));
			if (bWasMatched)
			{
				Self->PostNotice(TEXT("マッチングをやりなおします"));
			}
			Self->QueueTicket.Reset();
			Self->ResetQueueMatch();
			Self->QueueMode = Mode;
			Self->QueueJoin();
		}
	});
}

void UChaosImpactSessionSubsystem::HandleQueueMatched(const TSharedPtr<FJsonObject>& Reply)
{
	bQueueMatched = true;
	QueueMatchedAt = FPlatformTime::Seconds();
	QueueMatchId = Reply->GetStringField(TEXT("match"));
	QueuePort = static_cast<int32>(Reply->GetNumberField(TEXT("port")));
	QueueExpected = static_cast<int32>(Reply->GetNumberField(TEXT("expect")));
	bQueueHost = Reply->GetStringField(TEXT("role")) == TEXT("host");
	QueueNames.Reset();
	const TArray<TSharedPtr<FJsonValue>>* Names = nullptr;
	if (Reply->TryGetArrayField(TEXT("names"), Names))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Names)
		{
			QueueNames.Add(Value.IsValid() ? Value->AsString() : FString());
		}
	}
	const TSharedPtr<FJsonObject>* Opponent = nullptr;
	if (Reply->TryGetObjectField(TEXT("opponent"), Opponent) && Opponent && Opponent->IsValid())
	{
		QueueOpponentName = (*Opponent)->GetStringField(TEXT("name"));
		ReadRankInfo(*Opponent, QueueOpponentRank);
	}
	UE_LOG(LogChaosImpact, Log, TEXT("Match %s found (%s): %d player(s), this machine is the %s"), *QueueMatchId,
		QueueModeKey(QueueMode), QueueExpected, bQueueHost ? TEXT("host") : TEXT("guest"));
	if (!bQueueHost)
	{
		return;
	}
	// The host takes the match's room on the relay now and opens it once MATCH FOUND has been shown.
	StopRelayHosting();
	RelayRoomId = Reply->GetStringField(TEXT("room"));
	RelayRoomKey = Reply->GetStringField(TEXT("key"));
	RelayRoomPort = QueuePort;
	MatchmadeMode = QueueMode;
	MatchmadeId = QueueMatchId;
	bMatchmadeStarted = false;
	MatchmadeOptions = FString::Printf(TEXT("?CIMatchmade=%s?CIExpect=%d"), QueueModeKey(QueueMode), FMath::Max(2, QueueExpected));
	Password.Reset();
	RoomName = QueueMode == EChaosImpactQueueMode::Rank ? TEXT("ランクマッチ") : TEXT("ランダムマッチ");
	State = EChaosImpactRoomState::Creating;
	StartTunnel();
	if (!HeartbeatTicker.IsValid())
	{
		HeartbeatTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this](float)
		{
			RelayHeartbeat();
			return true;
		}), 3.0f);
	}
}

void UChaosImpactSessionSubsystem::NotifyMatchmadeMatchStarted()
{
	if (MatchmadeMode != EChaosImpactQueueMode::None && !bMatchmadeStarted)
	{
		bMatchmadeStarted = true;
		UE_LOG(LogChaosImpact, Log, TEXT("Match %s under way"), *MatchmadeId);
	}
}

void UChaosImpactSessionSubsystem::FinishMatchmade()
{
	if (MatchmadeMode == EChaosImpactQueueMode::None)
	{
		return;
	}
	// The match is over: leaving now forfeits nothing.
	bMatchmadeStarted = false;
	ShowQueueMenu = MatchmadeMode;
	LeaveRoom();
}

EChaosImpactQueueMode UChaosImpactSessionSubsystem::ConsumeShowQueueMenu()
{
	const EChaosImpactQueueMode Mode = ShowQueueMenu;
	ShowQueueMenu = EChaosImpactQueueMode::None;
	return Mode;
}

// ---------------------------------------------------------------------------------------------------- Rank

void UChaosImpactSessionSubsystem::RefreshRankInfo()
{
	if (!IsInternet() || bRankInfoPending)
	{
		return;
	}
	bRankInfoPending = true;
	TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
	Message->SetStringField(TEXT("op"), TEXT("rank_info"));
	Message->SetStringField(TEXT("token"), GetPlayerToken());
	Message->SetStringField(TEXT("name"), GetPlayerName());
	ChaosImpactRelay::Request(Message, [WeakThis = TWeakObjectPtr<UChaosImpactSessionSubsystem>(this)](
		TSharedPtr<FJsonObject> Reply, const int32 RoundTripMs)
	{
		UChaosImpactSessionSubsystem* Self = WeakThis.Get();
		if (!Self)
		{
			return;
		}
		Self->bRankInfoPending = false;
		if (Reply.IsValid() && Reply->GetBoolField(TEXT("ok")))
		{
			Self->LastRelayRtt = RoundTripMs;
			ReadRankInfo(Reply, Self->RankInfo);
		}
	});
}

void UChaosImpactSessionSubsystem::ReportRankResult(const FString& Result)
{
	if (MatchmadeId.IsEmpty() || RankReportMatch == MatchmadeId)
	{
		return;
	}
	RankReportMatch = MatchmadeId;
	RankReportResult = Result;
	RankReportTries = 0;
	RankReport = FChaosImpactRankReport();
	RankReport.State = EChaosImpactRankReportState::Sending;
	RankReport.Result = Result;
	RankReport.ChangedAt = FPlatformTime::Seconds();
	UE_LOG(LogChaosImpact, Log, TEXT("Rank match %s: reporting %s"), *RankReportMatch, *Result);
	SendRankReport();
}

void UChaosImpactSessionSubsystem::SendRankReport()
{
	++RankReportTries;
	TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
	Message->SetStringField(TEXT("op"), TEXT("rank_report"));
	Message->SetStringField(TEXT("match"), RankReportMatch);
	Message->SetStringField(TEXT("token"), GetPlayerToken());
	Message->SetStringField(TEXT("result"), RankReportResult);
	const FString Match = RankReportMatch;
	ChaosImpactRelay::Request(Message, [WeakThis = TWeakObjectPtr<UChaosImpactSessionSubsystem>(this), Match](
		TSharedPtr<FJsonObject> Reply, int32)
	{
		UChaosImpactSessionSubsystem* Self = WeakThis.Get();
		if (!Self || Self->RankReportMatch != Match)
		{
			return;
		}
		const FString Step = Reply.IsValid() && Reply->GetBoolField(TEXT("ok")) ? Reply->GetStringField(TEXT("state")) : FString();
		FChaosImpactRankReport& Report = Self->RankReport;
		if (Step == TEXT("done"))
		{
			Report.State = EChaosImpactRankReportState::Done;
			Report.Before = static_cast<int32>(Reply->GetNumberField(TEXT("before")));
			Report.After = static_cast<int32>(Reply->GetNumberField(TEXT("after")));
			Report.Delta = static_cast<int32>(Reply->GetNumberField(TEXT("delta")));
			Report.ChangedAt = FPlatformTime::Seconds();
			ReadRankInfo(Reply, Self->RankInfo);
			UE_LOG(LogChaosImpact, Log, TEXT("Rank match %s recorded: %d -> %d"), *Match, Report.Before, Report.After);
			return;
		}
		if (Step == TEXT("void"))
		{
			Report.State = EChaosImpactRankReportState::Void;
			Report.ChangedAt = FPlatformTime::Seconds();
			UE_LOG(LogChaosImpact, Warning, TEXT("Rank match %s not counted (the reports disagreed)"), *Match);
			return;
		}
		if (Step == TEXT("pending") && Report.State != EChaosImpactRankReportState::Pending)
		{
			Report.State = EChaosImpactRankReportState::Pending;
			Report.ChangedAt = FPlatformTime::Seconds();
		}
		if (Self->RankReportTries >= (Step.IsEmpty() ? 6 : RankReportMaxTries))
		{
			Report.State = EChaosImpactRankReportState::Failed;
			Report.ChangedAt = FPlatformTime::Seconds();
			UE_LOG(LogChaosImpact, Warning, TEXT("Rank match %s: the relay never settled it"), *Match);
			return;
		}
		// Asked again a little later: the other player's report, or the relay's own wait, settles it.
		Self->RankReportTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(Self, [Self, Match](float)
		{
			Self->RankReportTicker.Reset();
			if (Self->RankReportMatch == Match)
			{
				Self->SendRankReport();
			}
			return false;
		}), RankReportRetrySeconds);
	});
}
