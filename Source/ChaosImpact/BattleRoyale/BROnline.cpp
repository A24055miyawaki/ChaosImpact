#include "BROnline.h"
#include "BRGame.h"
#include "ChaosImpact.h"
#include "ChaosImpactSessionSubsystem.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "IPAddress.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	constexpr double BRHeartbeatSeconds = 10.0;
	constexpr double BRSearchSeconds = 2.0;
	const TCHAR* const BRLevelName = TEXT("Lvl_BRIsland");
}

UBROnlineSubsystem* UBROnlineSubsystem::Get(const UObject* Context)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(Context, EGetWorldErrorMode::ReturnNull) : nullptr;
	const UGameInstance* Game = World ? World->GetGameInstance() : nullptr;
	return Game ? Game->GetSubsystem<UBROnlineSubsystem>() : nullptr;
}

void UBROnlineSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (GEngine) { NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &UBROnlineSubsystem::HandleNetworkFailure); }
	MapLoadedHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UBROnlineSubsystem::HandleMapLoaded);
	Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UBROnlineSubsystem::TickNet), 0.25f);
	FParse::Value(FCommandLine::Get(), TEXT("CIBRHost="), DevHost);
	FParse::Value(FCommandLine::Get(), TEXT("CIBRJoin="), DevJoin);
}

void UBROnlineSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(MapLoadedHandle);
	if (GEngine) { GEngine->OnNetworkFailure().Remove(NetworkFailureHandle); }
	CloseRoom();
	Super::Deinitialize();
}

FString UBROnlineSubsystem::RoomVersion()
{
	return TEXT("br-") + ChaosImpactRelay::GetNetVersion();
}

FString UBROnlineSubsystem::HostName() const
{
	if (const UChaosImpactSessionSubsystem* Sessions = GetGameInstance() ? GetGameInstance()->GetSubsystem<UChaosImpactSessionSubsystem>() : nullptr)
	{
		return Sessions->GetPlayerName();
	}
	return TEXT("ホスト");
}

void UBROnlineSubsystem::SetNotice(const FString& Text)
{
	Notice = Text;
	UE_LOG(LogChaosImpact, Log, TEXT("BR online: %s"), *Text);
}

bool UBROnlineSubsystem::Host(UWorld* World, const FString& Word)
{
	if (!World || World->GetNetMode() == NM_Client) { return false; }
	if (!IsRelayUp()) { SetNotice(TEXT("中継サーバーが設定されていません")); return false; }
	Password = Word;
	if (World->GetNetMode() == NM_Standalone)
	{
		FURL Url = World->URL;
		Url.AddOption(TEXT("listen"));
		if (!World->Listen(Url)) { SetNotice(TEXT("部屋を開けませんでした")); return false; }
	}
	State = EBRNetState::Hosting;
	RelayCreate();
	return true;
}

void UBROnlineSubsystem::RelayCreate()
{
	const int32 Mine = ++Generation;
	TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
	Message->SetStringField(TEXT("op"), TEXT("create"));
	Message->SetStringField(TEXT("ver"), RoomVersion());
	Message->SetStringField(TEXT("password"), Password);
	Message->SetStringField(TEXT("name"), FString::Printf(TEXT("%sの部屋"), *HostName()));
	Message->SetStringField(TEXT("host"), HostName());
	ChaosImpactRelay::Request(Message, [WeakThis = TWeakObjectPtr<UBROnlineSubsystem>(this), Mine](TSharedPtr<FJsonObject> Reply, int32)
	{
		UBROnlineSubsystem* Self = WeakThis.Get();
		if (!Self || Mine != Self->Generation || Self->State != EBRNetState::Hosting) { return; }
		if (!Reply.IsValid() || !Reply->GetBoolField(TEXT("ok")))
		{
			Self->SetNotice(!Reply.IsValid() ? TEXT("中継サーバーにつながりません") : TEXT("部屋を作れませんでした"));
			return;
		}
		Self->RoomId = Reply->GetStringField(TEXT("room"));
		Self->RoomKey = Reply->GetStringField(TEXT("key"));
		Self->RoomPort = static_cast<int32>(Reply->GetNumberField(TEXT("port")));
		Self->NextHeartbeatAt = FPlatformTime::Seconds() + BRHeartbeatSeconds;
		Self->StartTunnel();
		Self->SetNotice(FString::Printf(TEXT("部屋を開きました（合言葉 %s）"), *Self->Password));
	});
}

void UBROnlineSubsystem::StartTunnel()
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	UNetDriver* Driver = World ? World->GetNetDriver() : nullptr;
	const TSharedPtr<const FInternetAddr> Local = Driver ? Driver->GetLocalAddr() : nullptr;
	const int32 ListenPort = Local.IsValid() && Local->GetPort() > 0 ? Local->GetPort() : FURL::UrlConfig.DefaultPort;
	FString RelayHost, PortText;
	const FString Server = ChaosImpactRelay::GetServer();
	if (!Server.Split(TEXT(":"), &RelayHost, &PortText, ESearchCase::IgnoreCase, ESearchDir::FromEnd)) { RelayHost = Server; }
	Tunnel = MakeUnique<FChaosImpactRelayTunnel>(RelayHost, RoomPort, RoomKey, ListenPort);
	if (!Tunnel->Start())
	{
		Tunnel.Reset();
		SetNotice(TEXT("中継サーバーにつながりません"));
	}
}

void UBROnlineSubsystem::RelayHeartbeat()
{
	NextHeartbeatAt = FPlatformTime::Seconds() + BRHeartbeatSeconds;
	const int32 Mine = Generation;
	TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
	Message->SetStringField(TEXT("op"), TEXT("update"));
	Message->SetStringField(TEXT("room"), RoomId);
	Message->SetStringField(TEXT("key"), RoomKey);
	Message->SetNumberField(TEXT("members"), FMath::Max(1, Players));
	Message->SetNumberField(TEXT("spectators"), 0);
	Message->SetBoolField(TEXT("open"), bOpen && Players < BRRules::MaxHumans);
	Message->SetStringField(TEXT("name"), FString::Printf(TEXT("%sの部屋"), *HostName()));
	ChaosImpactRelay::Request(Message, [WeakThis = TWeakObjectPtr<UBROnlineSubsystem>(this), Mine](TSharedPtr<FJsonObject> Reply, int32)
	{
		UBROnlineSubsystem* Self = WeakThis.Get();
		if (!Self || Mine != Self->Generation || Self->State != EBRNetState::Hosting || !Reply.IsValid()) { return; }
		if (!Reply->GetBoolField(TEXT("ok")))
		{
			// The relay forgot the room (it restarted): opened again.
			Self->Tunnel.Reset();
			Self->RoomId.Reset();
			Self->RelayCreate();
		}
	});
}

void UBROnlineSubsystem::SetPlayerCount(const int32 Count, const bool bInOpen)
{
	const bool bChanged = Count != Players || bInOpen != bOpen;
	Players = Count;
	bOpen = bInOpen;
	if (bChanged && State == EBRNetState::Hosting && !RoomId.IsEmpty()) { RelayHeartbeat(); }
}

void UBROnlineSubsystem::StartSearch(const FString& Word)
{
	if (!IsRelayUp()) { SetNotice(TEXT("中継サーバーが設定されていません")); return; }
	Password = Word;
	++Generation;
	bRequestInFlight = false;
	State = EBRNetState::Searching;
	NextSearchAt = 0.0;
	SetNotice(TEXT("部屋を探しています…"));
}

void UBROnlineSubsystem::RelaySearch()
{
	NextSearchAt = FPlatformTime::Seconds() + BRSearchSeconds;
	if (bRequestInFlight) { return; }
	bRequestInFlight = true;
	const int32 Mine = Generation;
	TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
	Message->SetStringField(TEXT("op"), TEXT("search"));
	Message->SetStringField(TEXT("ver"), RoomVersion());
	Message->SetStringField(TEXT("password"), Password);
	ChaosImpactRelay::Request(Message, [WeakThis = TWeakObjectPtr<UBROnlineSubsystem>(this), Mine](TSharedPtr<FJsonObject> Reply, int32)
	{
		UBROnlineSubsystem* Self = WeakThis.Get();
		if (!Self) { return; }
		Self->bRequestInFlight = false;
		if (Mine != Self->Generation || Self->State != EBRNetState::Searching) { return; }
		if (!Reply.IsValid()) { Self->SetNotice(TEXT("中継サーバーにつながりません")); return; }
		const TArray<TSharedPtr<FJsonValue>>* Rooms = nullptr;
		if (Reply->TryGetArrayField(TEXT("rooms"), Rooms) && !Rooms->IsEmpty())
		{
			for (const TSharedPtr<FJsonValue>& Value : *Rooms)
			{
				const TSharedPtr<FJsonObject> Room = Value.IsValid() ? Value->AsObject() : nullptr;
				if (Room.IsValid() && static_cast<int32>(Room->GetNumberField(TEXT("members"))) < BRRules::MaxHumans)
				{
					Self->JoinPort(static_cast<int32>(Room->GetNumberField(TEXT("port"))));
					return;
				}
			}
			Self->SetNotice(TEXT("この部屋は満員です"));
			return;
		}
		Self->SetNotice(TEXT("その合言葉の部屋はまだありません"));
	});
}

void UBROnlineSubsystem::JoinPort(const int32 Port)
{
	APlayerController* Controller = GetGameInstance() ? GetGameInstance()->GetFirstLocalPlayerController() : nullptr;
	if (!Controller) { return; }
	GuestTunnel = MakeUnique<FChaosImpactRelayGuestTunnel>(Port);
	FString Connect;
	if (GuestTunnel->Start())
	{
		Connect = FString::Printf(TEXT("127.0.0.1:%d"), GuestTunnel->GetLocalPort());
	}
	else
	{
		GuestTunnel.Reset();
		FString RelayHost, PortText;
		if (ChaosImpactRelay::UseWebSocket() || !ChaosImpactRelay::GetServer().Split(TEXT(":"), &RelayHost, &PortText, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
		{
			SetNotice(TEXT("中継サーバーにつながりません"));
			return;
		}
		Connect = FString::Printf(TEXT("%s:%d"), *RelayHost, Port);
	}
	State = EBRNetState::Guest;
	SetNotice(TEXT("部屋に入っています…"));
	const UChaosImpactSessionSubsystem* Sessions = GetGameInstance()->GetSubsystem<UChaosImpactSessionSubsystem>();
	Controller->ClientTravel(Connect + TEXT("?CIOpenWorld=1?CIBattleIsland=1?CILocalPlayers=1?CIKeyboardPlayer=0") + (Sessions ? Sessions->GetNameOption() : FString()), TRAVEL_Absolute);
}

void UBROnlineSubsystem::CloseRoom()
{
	if (!RoomId.IsEmpty())
	{
		TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
		Message->SetStringField(TEXT("op"), TEXT("close"));
		Message->SetStringField(TEXT("room"), RoomId);
		Message->SetStringField(TEXT("key"), RoomKey);
		ChaosImpactRelay::Request(Message, [](TSharedPtr<FJsonObject>, int32) {});
	}
	++Generation;
	RoomId.Reset();
	RoomKey.Reset();
	RoomPort = 0;
	Tunnel.Reset();
	GuestTunnel.Reset();
}

void UBROnlineSubsystem::Stop()
{
	CloseRoom();
	State = EBRNetState::Offline;
	Notice.Reset();
}

void UBROnlineSubsystem::HandleMapLoaded(UWorld* World)
{
	// Anywhere other than the island, the room is over.
	if (World && !World->GetMapName().Contains(BRLevelName) && State != EBRNetState::Offline && State != EBRNetState::Searching) { Stop(); }
}

void UBROnlineSubsystem::HandleNetworkFailure(UWorld*, UNetDriver*, ENetworkFailure::Type, const FString& Error)
{
	if (State == EBRNetState::Guest)
	{
		UE_LOG(LogChaosImpact, Warning, TEXT("BR online: connection lost (%s)"), *Error);
		Stop();
	}
}

bool UBROnlineSubsystem::TickNet(float)
{
	const double Now = FPlatformTime::Seconds();
	if (State == EBRNetState::Searching && Now >= NextSearchAt) { RelaySearch(); }
	if (State == EBRNetState::Hosting && !RoomId.IsEmpty() && Now >= NextHeartbeatAt) { RelayHeartbeat(); }
	// Development: -CIBRHost=<word> / -CIBRJoin=<word> open or find a room by themselves once the island is up.
	if (!bDevDone && (!DevHost.IsEmpty() || !DevJoin.IsEmpty()))
	{
		UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
		if (World && World->GetMapName().Contains(BRLevelName) && World->GetFirstPlayerController())
		{
			bDevDone = true;
			if (!DevHost.IsEmpty()) { Host(World, DevHost); } else { StartSearch(DevJoin); }
		}
	}
	return true;
}
