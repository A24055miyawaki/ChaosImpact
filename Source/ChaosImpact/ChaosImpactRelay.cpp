#include "ChaosImpactRelay.h"

#include "ChaosImpact.h"
#include "Async/Async.h"
#include "GeneralProjectSettings.h"
#include "HAL/PlatformProcess.h"
#include "HAL/RunnableThread.h"
#include "IPAddress.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

namespace
{
	const TCHAR* RelayConfigSection = TEXT("ChaosImpact.Online");
	constexpr int32 RelayMaxDatagram = 2048;
	/** The host re-registers with the relay this often (it also keeps the router's mapping open). */
	constexpr double RelayKeepAliveSeconds = 2.0;
	/** A remote player's local socket is closed after this long without traffic. */
	constexpr double RelayClientIdleSeconds = 60.0;

	ISocketSubsystem* Sockets()
	{
		return ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	}

	/** "host:port" -> an address (names are looked up; IPv4). Null when it cannot be resolved. */
	TSharedPtr<FInternetAddr> Resolve(const FString& Host, const int32 Port)
	{
		ISocketSubsystem* Subsystem = Sockets();
		if (!Subsystem || Host.IsEmpty() || Port <= 0)
		{
			return nullptr;
		}
		const FAddressInfoResult Info = Subsystem->GetAddressInfo(*Host, nullptr, EAddressInfoFlags::Default, FNetworkProtocolTypes::IPv4,
			ESocketType::SOCKTYPE_Datagram);
		if (Info.ReturnCode != SE_NO_ERROR || Info.Results.IsEmpty())
		{
			return nullptr;
		}
		TSharedRef<FInternetAddr> Address = Info.Results[0].Address->Clone();
		Address->SetPort(Port);
		return Address;
	}

	bool SplitServer(const FString& Server, FString& OutHost, int32& OutPort)
	{
		FString PortText;
		if (!Server.Split(TEXT(":"), &OutHost, &PortText, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
		{
			OutHost = Server;
			OutPort = 7700;
			return !OutHost.IsEmpty();
		}
		OutPort = FCString::Atoi(*PortText);
		return !OutHost.IsEmpty() && OutPort > 0;
	}

	FSocket* MakeUdpSocket(const TCHAR* Description)
	{
		ISocketSubsystem* Subsystem = Sockets();
		FSocket* Socket = Subsystem ? Subsystem->CreateSocket(NAME_DGram, Description, FNetworkProtocolTypes::IPv4) : nullptr;
		if (Socket)
		{
			Socket->SetNonBlocking(true);
			int32 Actual = 0;
			Socket->SetReceiveBufferSize(256 * 1024, Actual);
			Socket->SetSendBufferSize(256 * 1024, Actual);
		}
		return Socket;
	}
}

// ---------------------------------------------------------------------------------------------------- Settings

FString ChaosImpactRelay::GetServer()
{
	FString Server;
	if (FParse::Value(FCommandLine::Get(), TEXT("CIRelay="), Server))
	{
		return Server.TrimStartAndEnd();
	}
	// One PC's own setting first (GameUserSettings.ini), then the game's (DefaultGame.ini).
	if (GConfig && GConfig->GetString(RelayConfigSection, TEXT("RelayServer"), Server, GGameUserSettingsIni) && !Server.TrimStartAndEnd().IsEmpty())
	{
		return Server.TrimStartAndEnd();
	}
	if (GConfig && GConfig->GetString(RelayConfigSection, TEXT("RelayServer"), Server, GGameIni))
	{
		return Server.TrimStartAndEnd();
	}
	return FString();
}

bool ChaosImpactRelay::IsEnabled()
{
	return !GetServer().IsEmpty() && !FParse::Param(FCommandLine::Get(), TEXT("CILan"));
}

FString ChaosImpactRelay::GetNetVersion()
{
	const UGeneralProjectSettings* Project = GetDefault<UGeneralProjectSettings>();
	return FString::Printf(TEXT("ci1-%s"), Project ? *Project->ProjectVersion : TEXT("0"));
}

// ---------------------------------------------------------------------------------------------------- Directory

void ChaosImpactRelay::Request(TSharedRef<FJsonObject> Message, TFunction<void(TSharedPtr<FJsonObject>, int32)> OnReply)
{
	const FString Server = GetServer();
	static TAtomic<int32> NextRequestId{1};
	const int32 RequestId = NextRequestId++;
	Message->SetNumberField(TEXT("rid"), RequestId);
	FString Body;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Body);
	FJsonSerializer::Serialize(Message, Writer);
	Async(EAsyncExecution::ThreadPool, [Server, Body, RequestId, OnReply = MoveTemp(OnReply)]() mutable
	{
		TSharedPtr<FJsonObject> Reply;
		int32 RoundTripMs = 0;
		FString Host;
		int32 Port = 0;
		const TSharedPtr<FInternetAddr> Address = SplitServer(Server, Host, Port) ? Resolve(Host, Port) : nullptr;
		FSocket* Socket = Address ? MakeUdpSocket(TEXT("ChaosImpact relay directory")) : nullptr;
		if (Socket)
		{
			const FTCHARToUTF8 Utf8(*Body);
			// Three tries, each waiting up to a second for its answer.
			for (int32 Attempt = 0; Attempt < 3 && !Reply.IsValid(); ++Attempt)
			{
				const double SentAt = FPlatformTime::Seconds();
				int32 Sent = 0;
				Socket->SendTo(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length(), Sent, *Address);
				while (!Reply.IsValid() && FPlatformTime::Seconds() - SentAt < 1.0)
				{
					if (!Socket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromMilliseconds(100)))
					{
						continue;
					}
					uint8 Buffer[16 * 1024];
					int32 Read = 0;
					TSharedRef<FInternetAddr> From = Sockets()->CreateInternetAddr();
					while (Socket->RecvFrom(Buffer, sizeof(Buffer) - 1, Read, *From) && Read > 0)
					{
						Buffer[Read] = 0;
						const FString Text = UTF8_TO_TCHAR(reinterpret_cast<const ANSICHAR*>(Buffer));
						TSharedPtr<FJsonObject> Parsed;
						const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
						if (FJsonSerializer::Deserialize(Reader, Parsed) && Parsed.IsValid()
							&& static_cast<int32>(Parsed->GetNumberField(TEXT("rid"))) == RequestId)
						{
							Reply = Parsed;
							RoundTripMs = FMath::RoundToInt((FPlatformTime::Seconds() - SentAt) * 1000.0);
							break;
						}
					}
				}
			}
			Sockets()->DestroySocket(Socket);
		}
		AsyncTask(ENamedThreads::GameThread, [Reply, RoundTripMs, OnReply = MoveTemp(OnReply)]()
		{
			OnReply(Reply, RoundTripMs);
		});
	});
}

// ---------------------------------------------------------------------------------------------------- Tunnel

FChaosImpactRelayTunnel::FChaosImpactRelayTunnel(const FString& InRelayHost, const int32 InRoomPort, const FString& InRoomKey,
	const int32 InLocalGamePort)
	: RelayHost(InRelayHost), RoomPort(InRoomPort), RoomKey(InRoomKey), LocalGamePort(InLocalGamePort)
{
}

FChaosImpactRelayTunnel::~FChaosImpactRelayTunnel()
{
	Shutdown();
}

bool FChaosImpactRelayTunnel::Start()
{
	RelayAddr = Resolve(RelayHost, RoomPort);
	LocalGameAddr = Sockets() ? TSharedPtr<FInternetAddr>(Sockets()->CreateInternetAddr()) : nullptr;
	if (!RelayAddr.IsValid() || !LocalGameAddr.IsValid())
	{
		UE_LOG(LogChaosImpact, Warning, TEXT("Relay tunnel: cannot resolve %s"), *RelayHost);
		return false;
	}
	bool bValid = false;
	LocalGameAddr->SetIp(TEXT("127.0.0.1"), bValid);
	LocalGameAddr->SetPort(LocalGamePort);
	RelaySocket = MakeUdpSocket(TEXT("ChaosImpact relay tunnel"));
	if (!RelaySocket)
	{
		return false;
	}
	bStopping = false;
	Thread = FRunnableThread::Create(this, TEXT("ChaosImpactRelayTunnel"), 0, TPri_AboveNormal);
	UE_LOG(LogChaosImpact, Log, TEXT("Relay tunnel: %s -> 127.0.0.1:%d"), *RelayAddr->ToString(true), LocalGamePort);
	return Thread != nullptr;
}

void FChaosImpactRelayTunnel::Shutdown()
{
	if (Thread)
	{
		bStopping = true;
		Thread->WaitForCompletion();
		delete Thread;
		Thread = nullptr;
	}
	for (TPair<uint32, FClient>& Pair : Clients)
	{
		if (Pair.Value.Socket)
		{
			Sockets()->DestroySocket(Pair.Value.Socket);
		}
	}
	Clients.Reset();
	if (RelaySocket)
	{
		Sockets()->DestroySocket(RelaySocket);
		RelaySocket = nullptr;
	}
}

bool FChaosImpactRelayTunnel::IsConnected() const
{
	return FPlatformTime::Seconds() - LastAckAt.Load() < RelayKeepAliveSeconds * 3.0;
}

void FChaosImpactRelayTunnel::SendToRelay(const uint8* Data, const int32 Size)
{
	int32 Sent = 0;
	RelaySocket->SendTo(Data, Size, Sent, *RelayAddr);
}

FChaosImpactRelayTunnel::FClient* FChaosImpactRelayTunnel::FindOrOpenClient(const uint32 ClientId, const double Now)
{
	if (FClient* Found = Clients.Find(ClientId))
	{
		return Found;
	}
	// A local socket of its own for each remote player: the listen server then sees them as separate clients.
	FSocket* Socket = MakeUdpSocket(TEXT("ChaosImpact relay client"));
	TSharedRef<FInternetAddr> Any = Sockets()->CreateInternetAddr();
	bool bValid = false;
	Any->SetIp(TEXT("127.0.0.1"), bValid);
	Any->SetPort(0);
	if (!Socket || !Socket->Bind(*Any))
	{
		if (Socket)
		{
			Sockets()->DestroySocket(Socket);
		}
		return nullptr;
	}
	FClient& Client = Clients.Add(ClientId);
	Client.Socket = Socket;
	Client.LastSeen = Now;
	UE_LOG(LogChaosImpact, Log, TEXT("Relay tunnel: remote player %u"), ClientId);
	return &Client;
}

void FChaosImpactRelayTunnel::CloseClient(const uint32 ClientId)
{
	if (FClient* Client = Clients.Find(ClientId))
	{
		if (Client->Socket)
		{
			Sockets()->DestroySocket(Client->Socket);
		}
		Clients.Remove(ClientId);
		UE_LOG(LogChaosImpact, Log, TEXT("Relay tunnel: remote player %u closed"), ClientId);
	}
}

uint32 FChaosImpactRelayTunnel::Run()
{
	uint8 Buffer[RelayMaxDatagram + 8];
	double LastKeepAlive = -100.0;
	const FTCHARToUTF8 Key(*RoomKey);
	TArray<uint8> Hello;
	Hello.Append(reinterpret_cast<const uint8*>("CIH1"), 4);
	Hello.Append(reinterpret_cast<const uint8*>(Key.Get()), Key.Length());
	TSharedRef<FInternetAddr> From = Sockets()->CreateInternetAddr();
	while (!bStopping)
	{
		const double Now = FPlatformTime::Seconds();
		bool bBusy = false;
		if (Now - LastKeepAlive >= RelayKeepAliveSeconds)
		{
			LastKeepAlive = Now;
			SendToRelay(Hello.GetData(), Hello.Num());
		}
		// From the relay: a remote player's packet for the game, or news about them.
		int32 Read = 0;
		while (RelaySocket->RecvFrom(Buffer, RelayMaxDatagram, Read, *From) && Read > 0)
		{
			bBusy = true;
			if (Read >= 4 && FMemory::Memcmp(Buffer, "CIHK", 4) == 0)
			{
				LastAckAt = Now;
			}
			else if (Read > 8 && FMemory::Memcmp(Buffer, "CIRD", 4) == 0)
			{
				const uint32 ClientId = (uint32(Buffer[4]) << 24) | (uint32(Buffer[5]) << 16) | (uint32(Buffer[6]) << 8) | uint32(Buffer[7]);
				if (FClient* Client = FindOrOpenClient(ClientId, Now))
				{
					Client->LastSeen = Now;
					int32 Sent = 0;
					Client->Socket->SendTo(Buffer + 8, Read - 8, Sent, *LocalGameAddr);
				}
			}
			else if (Read >= 8 && FMemory::Memcmp(Buffer, "CIRX", 4) == 0)
			{
				CloseClient((uint32(Buffer[4]) << 24) | (uint32(Buffer[5]) << 16) | (uint32(Buffer[6]) << 8) | uint32(Buffer[7]));
			}
		}
		// From the game: packets for each remote player, back through the relay.
		TArray<uint32, TInlineAllocator<16>> Idle;
		for (TPair<uint32, FClient>& Pair : Clients)
		{
			FClient& Client = Pair.Value;
			Buffer[0] = 'C';
			Buffer[1] = 'I';
			Buffer[2] = 'H';
			Buffer[3] = 'D';
			Buffer[4] = uint8(Pair.Key >> 24);
			Buffer[5] = uint8(Pair.Key >> 16);
			Buffer[6] = uint8(Pair.Key >> 8);
			Buffer[7] = uint8(Pair.Key);
			while (Client.Socket->RecvFrom(Buffer + 8, RelayMaxDatagram, Read, *From) && Read > 0)
			{
				bBusy = true;
				Client.LastSeen = Now;
				SendToRelay(Buffer, Read + 8);
			}
			if (Now - Client.LastSeen > RelayClientIdleSeconds)
			{
				Idle.Add(Pair.Key);
			}
		}
		for (const uint32 ClientId : Idle)
		{
			CloseClient(ClientId);
		}
		if (!bBusy)
		{
			// Nothing waiting: a short rest (well under a frame, so packets are not held back).
			RelaySocket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromMicroseconds(Clients.IsEmpty() ? 20000 : 500));
		}
	}
	return 0;
}
