#include "ChaosImpactSessionSubsystem.h"

#include "ChaosImpact.h"
#include "ChaosImpactGameState.h"
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
	IOnlineSessionPtr Sessions = GetSessions();
	CreateError.Reset();
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
	// Rooms that can be joined first, then the best connection.
	Found.StableSort([](const FChaosImpactRoomListing& A, const FChaosImpactRoomListing& B)
	{
		return A.bOpen != B.bOpen ? A.bOpen : A.PingMs < B.PingMs;
	});
	UE_LOG(LogChaosImpact, Log, TEXT("Room search finished: success=%d rooms with the password=%d (of %d)"),
		bWasSuccessful, Found.Num(), Search.IsValid() ? Search->SearchResults.Num() : -1);
	Listings = MoveTemp(Found);
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
	if (!Sessions || State != EChaosImpactRoomState::Searching || !Listings.IsValidIndex(Index))
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
	State = EChaosImpactRoomState::Hosting;
	MemberCount = 1;
	SpectatorCount = 0;
	bRecruitmentOpen = true;
	FString Options = FString::Printf(TEXT("listen?CITraining=1?CIOnline=1?%s"), *LocalSetupOptions);
	// Development: -CIRoomCPU=N adds CPUs to the room so network hits can be tested without a second person.
	int32 DevRoomCPUs = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("CIRoomCPU="), DevRoomCPUs) && DevRoomCPUs > 0)
	{
		Options += FString::Printf(TEXT("?CICPUCount=%d"), FMath::Clamp(DevRoomCPUs, 1, 4));
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
	State = EChaosImpactRoomState::InRoom;
	UE_LOG(LogChaosImpact, Log, TEXT("Joining room at %s"), *ConnectString);
	// CIPlayers lets the host reserve a place for this machine's second player, who joins right after.
	// CIMachine lets the host replace this machine's old place if it is still there after a drop.
	// The local setup options also let this client pair its controllers again in the host's world.
	// CISpectator: this machine watches (mid-match or in a closed room) and never plays until it switches in the lobby.
	Controller->ClientTravel(FString::Printf(TEXT("%s?CIOnline=1?CIPlayers=%d?CIMachine=%s?%s%s"),
		*ConnectString, bJoinAsSpectator ? 1 : LocalPlayerCount, *GetMachineToken(), *LocalSetupOptions,
		bJoinAsSpectator ? TEXT("?CISpectator=1") : TEXT("")), TRAVEL_Absolute);
}

void UChaosImpactSessionSubsystem::LeaveRoom()
{
	StopSearch();
	State = EChaosImpactRoomState::None;
	if (IOnlineSessionPtr Sessions = GetSessions(); Sessions && Sessions->GetNamedSession(NAME_GameSession))
	{
		Sessions->DestroySession(NAME_GameSession);
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
