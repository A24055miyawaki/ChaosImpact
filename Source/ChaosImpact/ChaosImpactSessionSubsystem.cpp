#include "ChaosImpactSessionSubsystem.h"

#include "ChaosImpact.h"
#include "ChaosImpactGameState.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ConfigCacheIni.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "TimerManager.h"

namespace
{
	const FName PasswordKey(TEXT("CIPASS"));
	const FName OpenKey(TEXT("CIOPEN"));
	const FName CountKey(TEXT("CICOUNT"));
	const TCHAR* ConfigSection = TEXT("ChaosImpact.Online");
	const TCHAR* NameConfigKey = TEXT("PlayerName");
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

void UChaosImpactSessionSubsystem::CreateRoom(const FString& InPassword)
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
	State = EChaosImpactRoomState::Creating;
	// Search first so two rooms never share a password on the same network.
	bCheckingPassword = true;
	BeginFind();
}

void UChaosImpactSessionSubsystem::StartSearch(const FString& InPassword)
{
	if (!GetSessions())
	{
		PostNotice(TEXT("通信の準備ができていません"));
		return;
	}
	BindSessionDelegates();
	Password = InPassword;
	State = EChaosImpactRoomState::Searching;
	BeginFind();
}

void UChaosImpactSessionSubsystem::StopSearch()
{
	if (State != EChaosImpactRoomState::Searching && State != EChaosImpactRoomState::Joining)
	{
		return;
	}
	State = EChaosImpactRoomState::None;
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		GameInstance->GetTimerManager().ClearTimer(RetryTimer);
	}
	if (IOnlineSessionPtr Sessions = GetSessions())
	{
		Sessions->CancelFindSessions();
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
	bCheckingPassword = false;
	if (IOnlineSessionPtr Sessions = GetSessions())
	{
		Sessions->CancelFindSessions();
		if (Sessions->GetNamedSession(NAME_GameSession))
		{
			Sessions->DestroySession(NAME_GameSession);
		}
	}
}

void UChaosImpactSessionSubsystem::BeginFind()
{
	IOnlineSessionPtr Sessions = GetSessions();
	if (!Sessions || (State != EChaosImpactRoomState::Creating && State != EChaosImpactRoomState::Searching))
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
	UE_LOG(LogChaosImpact, Log, TEXT("Room search finished: success=%d results=%d state=%d password=%s"),
		bWasSuccessful, Search.IsValid() ? Search->SearchResults.Num() : -1, static_cast<int32>(State), *Password);
	const FOnlineSessionSearchResult* Match = nullptr;
	if (Search.IsValid())
	{
		for (const FOnlineSessionSearchResult& Result : Search->SearchResults)
		{
			FString ResultPassword;
			if (Result.Session.SessionSettings.Get(PasswordKey, ResultPassword) && ResultPassword == Password)
			{
				Match = &Result;
				break;
			}
		}
	}

	if (State == EChaosImpactRoomState::Creating && bCheckingPassword)
	{
		bCheckingPassword = false;
		if (Match)
		{
			State = EChaosImpactRoomState::None;
			CreateError = TEXT("このあいことばは使われています");
			return;
		}
		CreateSessionNow();
		return;
	}

	if (State != EChaosImpactRoomState::Searching)
	{
		return;
	}
	UGameInstance* GameInstance = GetGameInstance();
	if (!Match)
	{
		if (GameInstance)
		{
			GameInstance->GetTimerManager().SetTimer(RetryTimer, this,
				&UChaosImpactSessionSubsystem::BeginFind, 1.5f, false);
		}
		return;
	}

	int32 Open = 1;
	int32 Count = 1;
	Match->Session.SessionSettings.Get(OpenKey, Open);
	Match->Session.SessionSettings.Get(CountKey, Count);
	if (Open == 0)
	{
		State = EChaosImpactRoomState::None;
		PostNotice(TEXT("メンバー募集が終了しています"));
		return;
	}
	// Everyone playing on this machine needs a place (a room of 7 cannot take a pair).
	if (Count + LocalPlayerCount > AChaosImpactGameState::MaxMembers)
	{
		State = EChaosImpactRoomState::None;
		PostNotice(TEXT("へやが満員です"));
		return;
	}

	IOnlineSessionPtr Sessions = GetSessions();
	if (!Sessions)
	{
		return;
	}
	if (Sessions->GetNamedSession(NAME_GameSession))
	{
		Sessions->DestroySession(NAME_GameSession);
	}
	State = EChaosImpactRoomState::Joining;
	if (!Sessions->JoinSession(0, NAME_GameSession, *Match))
	{
		HandleJoinComplete(NAME_GameSession, EOnJoinSessionCompleteResult::UnknownError);
	}
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
	Settings.NumPublicConnections = AChaosImpactGameState::MaxMembers;
	Settings.Set(PasswordKey, Password, EOnlineDataAdvertisementType::ViaOnlineService);
	Settings.Set(OpenKey, 1, EOnlineDataAdvertisementType::ViaOnlineService);
	Settings.Set(CountKey, 1, EOnlineDataAdvertisementType::ViaOnlineService);
	if (!Sessions->CreateSession(0, NAME_GameSession, Settings))
	{
		HandleCreateComplete(NAME_GameSession, false);
	}
}

void UChaosImpactSessionSubsystem::HandleCreateComplete(FName SessionName, const bool bWasSuccessful)
{
	UE_LOG(LogChaosImpact, Log, TEXT("Room create finished: success=%d state=%d"), bWasSuccessful,
		static_cast<int32>(State));
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
		State = EChaosImpactRoomState::None;
		PostNotice(TEXT("へやに入れませんでした"));
		return;
	}
	State = EChaosImpactRoomState::InRoom;
	UE_LOG(LogChaosImpact, Log, TEXT("Joining room at %s"), *ConnectString);
	// CIPlayers lets the host reserve a place for this machine's second player, who joins right after.
	// The local setup options also let this client pair its controllers again in the host's world.
	Controller->ClientTravel(FString::Printf(TEXT("%s?CIOnline=1?CIPlayers=%d?%s"),
		*ConnectString, LocalPlayerCount, *LocalSetupOptions), TRAVEL_Absolute);
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
	PostNotice(ErrorString.Contains(TEXT("CIFULL")) ? TEXT("へやが満員です")
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
