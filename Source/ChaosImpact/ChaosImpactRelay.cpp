#include "ChaosImpactRelay.h"
#include "ChaosImpactRelayWebSocket.h"

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
	/** A direct line is probed this often until it answers, and this often while it does. */
	constexpr double DirectSeekSeconds = 0.2;
	constexpr double DirectKeepSeconds = 0.5;
	/** No answer for this long: the game's packets go back through the relay. */
	constexpr double DirectStaleSeconds = 2.5;
	/** A guest's line the relay has not mentioned for this long is forgotten (they left). */
	constexpr double DirectForgetSeconds = 15.0;
	/** A guest tells the relay its addresses this often; that also keeps it in the room while its packets go directly. */
	constexpr double GuestIntroduceSeconds = 1.0;
	/** b"CI2P...." + client id + token. */
	constexpr int32 DirectHeaderSize = 20;
	bool bAutoWebSocket = false;

	FString RelaySetting(const TCHAR* Key, const TCHAR* Argument)
	{
		FString Value;
		if (FParse::Value(FCommandLine::Get(), Argument, Value)) { return Value.TrimStartAndEnd(); }
		if (GConfig && GConfig->GetString(RelayConfigSection, Key, Value, GGameUserSettingsIni) && !Value.TrimStartAndEnd().IsEmpty())
		{
			return Value.TrimStartAndEnd();
		}
		if (GConfig) { GConfig->GetString(RelayConfigSection, Key, Value, GGameIni); }
		return Value.TrimStartAndEnd();
	}

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

	uint32 ReadId(const uint8* Data)
	{
		return (uint32(Data[0]) << 24) | (uint32(Data[1]) << 16) | (uint32(Data[2]) << 8) | uint32(Data[3]);
	}

	void WriteId(uint8* Data, const uint32 Id)
	{
		Data[0] = uint8(Id >> 24);
		Data[1] = uint8(Id >> 16);
		Data[2] = uint8(Id >> 8);
		Data[3] = uint8(Id);
	}

	bool IsMessage(const uint8* Data, const int32 Size, const char* Magic, const int32 MinSize = 8)
	{
		return Size >= MinSize && FMemory::Memcmp(Data, Magic, 8) == 0;
	}

	/** A probe or an answer of a direct line (never the game's). */
	bool IsDirectProbe(const uint8* Data, const int32 Size)
	{
		return IsMessage(Data, Size, "CI2PPROB") || IsMessage(Data, Size, "CI2PRPLY");
	}

	/** [ChaosImpact.Online] DirectConnect=0 (or -CIDirect=0): always through the relay. */
	bool AllowDirect()
	{
		const FString Value = RelaySetting(TEXT("DirectConnect"), TEXT("CIDirect="));
		return !Value.Equals(TEXT("0")) && !Value.Equals(TEXT("false"), ESearchCase::IgnoreCase);
	}

	/**
	 * b"CI2PPRIV" + this machine's own network addresses with the socket's port ("192.168.1.5:61234,..."), for the
	 * relay to pass on: a player on the same network (or PC) reaches this one there, without going out and back in.
	 */
	TArray<uint8> MakePrivateMessage(const int32 Port)
	{
		TArray<uint8> Message;
		Message.Append(reinterpret_cast<const uint8*>("CI2PPRIV"), 8);
		TArray<TSharedPtr<FInternetAddr>> Adapters;
		if (Port <= 0 || !Sockets() || !Sockets()->GetLocalAdapterAddresses(Adapters))
		{
			return Message;
		}
		FString Text;
		int32 Count = 0;
		for (const TSharedPtr<FInternetAddr>& Adapter : Adapters)
		{
			if (!Adapter || Adapter->GetProtocolType() != FNetworkProtocolTypes::IPv4)
			{
				continue;
			}
			uint32 Ip = 0;
			Adapter->GetIp(Ip);
			// Not loopback (127.x) or link-local (169.254.x): no other machine reaches those.
			if (Ip == 0 || (Ip >> 24) == 127 || (Ip >> 16) == 0xA9FE)
			{
				continue;
			}
			Text += FString::Printf(TEXT("%s%u.%u.%u.%u:%d"), Text.IsEmpty() ? TEXT("") : TEXT(","), Ip >> 24, (Ip >> 16) & 255,
				(Ip >> 8) & 255, Ip & 255, Port);
			if (++Count == 3)
			{
				break;
			}
		}
		const FTCHARToUTF8 Utf8(*Text);
		Message.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
		return Message;
	}

	/** Binds a UDP socket to every local address on a free port (so its port is known before it sends). */
	bool BindAnyPort(FSocket& Socket)
	{
		TSharedRef<FInternetAddr> Any = Sockets()->CreateInternetAddr();
		bool bValid = false;
		Any->SetIp(TEXT("0.0.0.0"), bValid);
		Any->SetPort(0);
		return bValid && Socket.Bind(*Any);
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
	return (!GetServer().IsEmpty() || !GetWebSocketServer().IsEmpty()) && !FParse::Param(FCommandLine::Get(), TEXT("CILan"));
}

FString ChaosImpactRelay::GetWebSocketServer()
{
	FString Url = RelaySetting(TEXT("RelayWebSocketServer"), TEXT("CIRelayWS="));
	while (Url.EndsWith(TEXT("/"))) { Url.LeftChopInline(1); }
	// Unencrypted WS is only a loopback test option. Never disable certificate or hostname checks for WSS.
	const bool bLoopback = Url.StartsWith(TEXT("ws://127.0.0.1:")) || Url.StartsWith(TEXT("ws://localhost:"));
	return Url.StartsWith(TEXT("wss://")) || bLoopback ? Url : FString();
}

bool ChaosImpactRelay::UseWebSocket()
{
	const FString Mode = RelaySetting(TEXT("RelayTransport"), TEXT("CIRelayTransport="));
	return !GetWebSocketServer().IsEmpty() && (Mode.Equals(TEXT("wss"), ESearchCase::IgnoreCase)
		|| (!Mode.Equals(TEXT("udp"), ESearchCase::IgnoreCase) && bAutoWebSocket));
}

FString ChaosImpactRelay::GetRoomWebSocketUrl(int32 Port)
{
	return GetWebSocketServer() + FString::Printf(TEXT("/room/%d"), Port);
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
	static bool bLoggedConfiguration = false;
	if (!bLoggedConfiguration)
	{
		bLoggedConfiguration = true;
		UE_LOG(LogChaosImpact, Log, TEXT("Relay configuration: UDP=%s WSS=%s mode=%s gameini=%s"),
			*Server, *GetWebSocketServer(), *RelaySetting(TEXT("RelayTransport"), TEXT("CIRelayTransport=")), *GGameIni);
	}
	static TAtomic<int32> NextRequestId{1};
	const int32 RequestId = NextRequestId++;
	Message->SetNumberField(TEXT("rid"), RequestId);
	FString Body;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Body);
	FJsonSerializer::Serialize(Message, Writer);
	if (UseWebSocket() || Server.IsEmpty())
	{
		RequestWebSocket(Body, RequestId, MoveTemp(OnReply));
		return;
	}
	// A thread of its own: the shared pool can be busy for seconds (loading, shaders), and the queue's polls must not wait.
	Async(EAsyncExecution::Thread, [Server, Body, RequestId, OnReply = MoveTemp(OnReply)]() mutable
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
		AsyncTask(ENamedThreads::GameThread, [Reply, RoundTripMs, Body, RequestId, OnReply = MoveTemp(OnReply)]() mutable
		{
			const FString Mode = RelaySetting(TEXT("RelayTransport"), TEXT("CIRelayTransport="));
			if (!Reply && !Mode.Equals(TEXT("udp"), ESearchCase::IgnoreCase) && !GetWebSocketServer().IsEmpty())
			{
				bAutoWebSocket = true;
				UE_LOG(LogChaosImpact, Log, TEXT("Relay UDP unanswered: switching this session to WSS %s"), *GetWebSocketServer());
				RequestWebSocket(Body, RequestId, MoveTemp(OnReply));
				return;
			}
			OnReply(Reply, RoundTripMs);
		});
	});
}

// ---------------------------------------------------------------------------------------------------- Direct line

void FChaosImpactDirectPath::Learn(const uint8* Data, const int32 Size, const double Now)
{
	if (Size < DirectHeaderSize)
	{
		return;
	}
	const uint32 Id = ReadId(Data + 8);
	if (Id != ClientId || FMemory::Memcmp(Token, Data + 12, sizeof(Token)) != 0)
	{
		// Someone new (or the same guest come back): nothing known about them yet.
		ClientId = Id;
		FMemory::Memcpy(Token, Data + 12, sizeof(Token));
		Heard.Reset();
		Address.Reset();
		LastAnswerAt = -100.0;
		bDirect = false;
	}
	LastIntroducedAt = Now;
	const FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Data + DirectHeaderSize), Size - DirectHeaderSize);
	const FString Text(Converted.Length(), Converted.Get());
	// "pub=ip:port;priv=ip:port,ip:port": the address the relay sees, then those on their own network.
	TArray<FString> Parts;
	Text.ParseIntoArray(Parts, TEXT(";"));
	TArray<TSharedRef<FInternetAddr>> Found;
	for (const FString& Part : Parts)
	{
		FString Name, List;
		if (!Part.Split(TEXT("="), &Name, &List))
		{
			continue;
		}
		TArray<FString> Entries;
		List.ParseIntoArray(Entries, TEXT(","));
		for (const FString& Entry : Entries)
		{
			FString Ip, PortText;
			if (!Entry.Split(TEXT(":"), &Ip, &PortText, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
			{
				continue;
			}
			const int32 Port = FCString::Atoi(*PortText);
			TSharedRef<FInternetAddr> Candidate = Sockets()->CreateInternetAddr();
			bool bValid = false;
			Candidate->SetIp(*Ip, bValid);
			if (!bValid || Port <= 0 || Port > 65535)
			{
				continue;
			}
			Candidate->SetPort(Port);
			if (!Found.ContainsByPredicate([&Candidate](const TSharedRef<FInternetAddr>& Known) { return *Known == *Candidate; }))
			{
				Found.Add(Candidate);
			}
		}
	}
	Candidates = MoveTemp(Found);
}

void FChaosImpactDirectPath::Probe(FSocket& Socket, const double Now)
{
	if (Candidates.IsEmpty() || Now - LastIntroducedAt > DirectForgetSeconds)
	{
		return;
	}
	const FInternetAddr* Direct = GetDirect(Now);
	if (Now - LastProbeAt < (Direct ? DirectKeepSeconds : DirectSeekSeconds))
	{
		return;
	}
	LastProbeAt = Now;
	uint8 Message[DirectHeaderSize];
	FMemory::Memcpy(Message, "CI2PPROB", 8);
	WriteId(Message + 8, ClientId);
	FMemory::Memcpy(Message + 12, Token, sizeof(Token));
	int32 Sent = 0;
	if (Direct)
	{
		Socket.SendTo(Message, DirectHeaderSize, Sent, *Direct);
		return;
	}
	for (const TSharedRef<FInternetAddr>& Candidate : Candidates)
	{
		Socket.SendTo(Message, DirectHeaderSize, Sent, *Candidate);
	}
}

bool FChaosImpactDirectPath::Receive(FSocket& Socket, const uint8* Data, const int32 Size, const FInternetAddr& From, const double Now)
{
	const bool bProbe = IsMessage(Data, Size, "CI2PPROB", DirectHeaderSize);
	if (ClientId == 0 || (!bProbe && !IsMessage(Data, Size, "CI2PRPLY", DirectHeaderSize)) || ReadId(Data + 8) != ClientId
		|| FMemory::Memcmp(Data + 12, Token, sizeof(Token)) != 0)
	{
		return false;
	}
	if (!IsFrom(From))
	{
		if (Heard.Num() >= 4)
		{
			Heard.RemoveAt(0);
		}
		Heard.Add(From.Clone());
	}
	if (bProbe)
	{
		uint8 Answer[DirectHeaderSize];
		FMemory::Memcpy(Answer, Data, DirectHeaderSize);
		FMemory::Memcpy(Answer, "CI2PRPLY", 8);
		int32 Sent = 0;
		Socket.SendTo(Answer, DirectHeaderSize, Sent, From);
		return true;
	}
	// An answer: this way gets there and back. The first address to answer stays while it keeps answering.
	const bool bAnswering = Address.IsValid() && Now - LastAnswerAt < DirectStaleSeconds;
	if (!bAnswering || *Address == From)
	{
		if (!Address.IsValid() || !(*Address == From))
		{
			Address = From.Clone();
		}
		LastAnswerAt = Now;
	}
	return true;
}

bool FChaosImpactDirectPath::IsFrom(const FInternetAddr& From) const
{
	return Heard.ContainsByPredicate([&From](const TSharedRef<FInternetAddr>& Known) { return *Known == From; });
}

const FInternetAddr* FChaosImpactDirectPath::GetDirect(const double Now)
{
	const bool bAnswering = Address.IsValid() && Now - LastAnswerAt < DirectStaleSeconds;
	if (bAnswering != bDirect)
	{
		bDirect = bAnswering;
		if (bAnswering)
		{
			UE_LOG(LogChaosImpact, Log, TEXT("Direct connection %u: %s"), ClientId, *Address->ToString(true));
		}
		else
		{
			UE_LOG(LogChaosImpact, Log, TEXT("Direct connection %u lost: through the relay"), ClientId);
		}
	}
	return bAnswering ? Address.Get() : nullptr;
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
	const bool bWss = ChaosImpactRelay::UseWebSocket();
	if (!bWss) { RelayAddr = Resolve(RelayHost, RoomPort); }
	LocalGameAddr = Sockets() ? TSharedPtr<FInternetAddr>(Sockets()->CreateInternetAddr()) : nullptr;
	if ((!bWss && !RelayAddr.IsValid()) || !LocalGameAddr.IsValid())
	{
		UE_LOG(LogChaosImpact, Warning, TEXT("Relay tunnel: cannot resolve %s"), *RelayHost);
		return false;
	}
	bool bValid = false;
	LocalGameAddr->SetIp(TEXT("127.0.0.1"), bValid);
	LocalGameAddr->SetPort(LocalGamePort);
	if (bWss)
	{
		WebSocket = MakeShared<FChaosImpactRelayWebSocket>();
		if (!WebSocket->Start(ChaosImpactRelay::GetRoomWebSocketUrl(RoomPort))) { WebSocket.Reset(); return false; }
	}
	else
	{
		RelaySocket = MakeUdpSocket(TEXT("ChaosImpact relay tunnel"));
		// A known port, for the guests on this network (unbound, it would only get one on its first send).
		if (RelaySocket && !BindAnyPort(*RelaySocket))
		{
			UE_LOG(LogChaosImpact, Warning, TEXT("Relay tunnel: could not bind its socket (only through the relay)"));
		}
	}
	if (!bWss && !RelaySocket)
	{
		return false;
	}
	bStopping = false;
	Thread = FRunnableThread::Create(this, TEXT("ChaosImpactRelayTunnel"), 0, TPri_AboveNormal);
	UE_LOG(LogChaosImpact, Log, TEXT("Relay tunnel: %s -> 127.0.0.1:%d"),
		bWss ? *ChaosImpactRelay::GetRoomWebSocketUrl(RoomPort) : *RelayAddr->ToString(true), LocalGamePort);
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
	if (WebSocket) { WebSocket->Shutdown(); WebSocket.Reset(); }
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
	if (WebSocket) { WebSocket->Send(Data, Size); return; }
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
	// Direct lines: over UDP only (a WebSocket host has no address the guests could reach).
	const bool bDirect = RelaySocket && !WebSocket && AllowDirect();
	const TArray<uint8> Private = bDirect ? MakePrivateMessage(RelaySocket->GetPortNo()) : TArray<uint8>();
	TSharedRef<FInternetAddr> From = Sockets()->CreateInternetAddr();
	TArray<uint8> WsPacket;
	bool bFromRelay = true;
	// -CINetStats: what went each way, every five seconds.
	const bool bStats = FParse::Param(FCommandLine::Get(), TEXT("CINetStats"));
	double LastStatsAt = FPlatformTime::Seconds();
	int32 FromGuestsDirect = 0, FromGuestsRelay = 0, ToGame = 0, FromGame = 0, ToGuestsDirect = 0, ToGuestsRelay = 0;
	const auto ReceiveRelay = [&]()
	{
		if (WebSocket)
		{
			if (!WebSocket->Receive(WsPacket)) { return 0; }
			const int32 Size = FMath::Min(WsPacket.Num(), static_cast<int32>(sizeof(Buffer)));
			FMemory::Memcpy(Buffer, WsPacket.GetData(), Size); return Size;
		}
		int32 Size = 0;
		if (RelaySocket->RecvFrom(Buffer, sizeof(Buffer), Size, *From))
		{
			bFromRelay = *From == *RelayAddr;
			return Size;
		}
		return 0;
	};
	while (!bStopping)
	{
		const double Now = FPlatformTime::Seconds();
		bool bBusy = false;
		if (bStats && Now - LastStatsAt >= 5.0)
		{
			UE_LOG(LogChaosImpact, Log, TEXT("NETSTATS host tunnel: guests %d direct / %d relay -> game %d; game %d -> guests %d direct / %d relay (%d socket(s))"),
				FromGuestsDirect, FromGuestsRelay, ToGame, FromGame, ToGuestsDirect, ToGuestsRelay, Clients.Num());
			LastStatsAt = Now;
			FromGuestsDirect = FromGuestsRelay = ToGame = FromGame = ToGuestsDirect = ToGuestsRelay = 0;
		}
		if (bResetClients.Exchange(false))
		{
			TArray<uint32> Ids;
			Clients.GetKeys(Ids);
			for (const uint32 ClientId : Ids)
			{
				CloseClient(ClientId);
			}
		}
		if (Now - LastKeepAlive >= RelayKeepAliveSeconds)
		{
			LastKeepAlive = Now;
			SendToRelay(Hello.GetData(), Hello.Num());
			if (bDirect)
			{
				SendToRelay(Private.GetData(), Private.Num());
			}
		}
		// From the relay: a remote player's packet for the game, or news about them. From elsewhere: a guest, directly.
		int32 Read = 0;
		while ((Read = ReceiveRelay()) > 0)
		{
			bBusy = true;
			if (!bFromRelay)
			{
				if (bDirect)
				{
					FromGuestsDirect += IsDirectProbe(Buffer, Read) ? 0 : 1;
					ReceiveDirect(Buffer, Read, *From, Now);
				}
			}
			else if (Read >= 4 && FMemory::Memcmp(Buffer, "CIHK", 4) == 0)
			{
				LastAckAt = Now;
			}
			else if (IsMessage(Buffer, Read, "CI2PPEER", DirectHeaderSize))
			{
				if (bDirect)
				{
					Peers.FindOrAdd(ReadId(Buffer + 8)).Learn(Buffer, Read, Now);
				}
			}
			else if (Read > 8 && FMemory::Memcmp(Buffer, "CIRD", 4) == 0)
			{
				// (A guest's own message to the relay, passed on by a relay from before direct lines: not the game's.)
				if (IsMessage(Buffer + 8, Read - 8, "CI2PPRIV"))
				{
					continue;
				}
				++FromGuestsRelay;
				if (FClient* Client = FindOrOpenClient(ReadId(Buffer + 4), Now))
				{
					Client->LastSeen = Now;
					int32 Sent = 0;
					Client->Socket->SendTo(Buffer + 8, Read - 8, Sent, *LocalGameAddr);
					++ToGame;
				}
			}
			else if (Read >= 8 && FMemory::Memcmp(Buffer, "CIRX", 4) == 0)
			{
				CloseClient(ReadId(Buffer + 4));
				Peers.Remove(ReadId(Buffer + 4));
			}
		}
		// From the game: packets for each remote player, directly while their line answers, else through the relay.
		TArray<uint32, TInlineAllocator<16>> Idle;
		for (TPair<uint32, FClient>& Pair : Clients)
		{
			FClient& Client = Pair.Value;
			FChaosImpactDirectPath* Peer = Peers.Find(Pair.Key);
			FMemory::Memcpy(Buffer, "CIHD", 4);
			WriteId(Buffer + 4, Pair.Key);
			while (Client.Socket->RecvFrom(Buffer + 8, RelayMaxDatagram, Read, *From) && Read > 0)
			{
				bBusy = true;
				Client.LastSeen = Now;
				++FromGame;
				if (const FInternetAddr* Direct = Peer ? Peer->GetDirect(Now) : nullptr)
				{
					int32 Sent = 0;
					RelaySocket->SendTo(Buffer + 8, Read, Sent, *Direct);
					++ToGuestsDirect;
				}
				else
				{
					SendToRelay(Buffer, Read + 8);
					++ToGuestsRelay;
				}
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
		for (auto It = Peers.CreateIterator(); It; ++It)
		{
			if (Now - It->Value.LastIntroducedAt > DirectForgetSeconds)
			{
				It.RemoveCurrent();
				continue;
			}
			It->Value.Probe(*RelaySocket, Now);
		}
		if (!bBusy)
		{
			// Nothing waiting: a short rest (well under a frame, so packets are not held back).
			if (WebSocket) { FPlatformProcess::SleepNoStats(Clients.IsEmpty() ? .002f : .0005f); }
			else { RelaySocket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromMicroseconds(Clients.IsEmpty() ? 20000 : 500)); }
		}
	}
	return 0;
}

void FChaosImpactRelayTunnel::ReceiveDirect(const uint8* Data, const int32 Size, const FInternetAddr& From, const double Now)
{
	if (IsDirectProbe(Data, Size))
	{
		for (TPair<uint32, FChaosImpactDirectPath>& Pair : Peers)
		{
			if (Pair.Value.Receive(*RelaySocket, Data, Size, From, Now))
			{
				break;
			}
		}
		return;
	}
	// The game's packet from a guest, at an address that probed with their token (anything else is dropped).
	for (TPair<uint32, FChaosImpactDirectPath>& Pair : Peers)
	{
		if (Pair.Value.IsFrom(From))
		{
			if (FClient* Client = FindOrOpenClient(Pair.Key, Now))
			{
				Client->LastSeen = Now;
				int32 Sent = 0;
				Client->Socket->SendTo(Data, Size, Sent, *LocalGameAddr);
			}
			return;
		}
	}
}

FChaosImpactRelayGuestTunnel::~FChaosImpactRelayGuestTunnel() { Shutdown(); }

bool FChaosImpactRelayGuestTunnel::Start()
{
	Socket = MakeUdpSocket(TEXT("ChaosImpact relay guest loopback"));
	if (!Socket) { return false; }
	TSharedRef<FInternetAddr> Local = Sockets()->CreateInternetAddr();
	bool bValid = false; Local->SetIp(TEXT("127.0.0.1"), bValid); Local->SetPort(0);
	if (!Socket->Bind(*Local)) { Shutdown(); return false; }
	Socket->GetAddress(*Local);
	LocalPort = Local->GetPort();
	if (LocalPort <= 0) { Shutdown(); return false; }
	FString Target;
	if (ChaosImpactRelay::UseWebSocket())
	{
		Target = ChaosImpactRelay::GetRoomWebSocketUrl(RoomPort);
		WebSocket = MakeShared<FChaosImpactRelayWebSocket>();
		if (!WebSocket->Start(Target)) { Shutdown(); return false; }
	}
	else
	{
		FString RelayHost;
		int32 DirectoryPort = 0;
		RelayAddr = SplitServer(ChaosImpactRelay::GetServer(), RelayHost, DirectoryPort) ? Resolve(RelayHost, RoomPort) : nullptr;
		Outside = RelayAddr ? MakeUdpSocket(TEXT("ChaosImpact relay guest")) : nullptr;
		if (!Outside || !BindAnyPort(*Outside)) { Shutdown(); return false; }
		Target = RelayAddr->ToString(true);
	}
	Thread = FRunnableThread::Create(this, TEXT("ChaosImpactRelayGuest"), 0, TPri_AboveNormal);
	UE_LOG(LogChaosImpact, Log, TEXT("Relay guest tunnel: 127.0.0.1:%d -> %s"), LocalPort, *Target);
	return Thread != nullptr;
}

void FChaosImpactRelayGuestTunnel::Shutdown()
{
	if (Thread) { bStopping = true; Thread->WaitForCompletion(); delete Thread; Thread = nullptr; }
	if (WebSocket) { WebSocket->Shutdown(); WebSocket.Reset(); }
	if (Socket) { Sockets()->DestroySocket(Socket); Socket = nullptr; }
	if (Outside) { Sockets()->DestroySocket(Outside); Outside = nullptr; }
}

uint32 FChaosImpactRelayGuestTunnel::Run()
{
	return WebSocket ? RunWebSocket() : RunUdp();
}

uint32 FChaosImpactRelayGuestTunnel::RunUdp()
{
	uint8 Buffer[RelayMaxDatagram];
	TSharedRef<FInternetAddr> From = Sockets()->CreateInternetAddr();
	TSharedPtr<FInternetAddr> GameAddress;
	const bool bDirect = AllowDirect();
	const TArray<uint8> Private = MakePrivateMessage(bDirect ? Outside->GetPortNo() : 0);
	double LastIntroducedAt = -100.0;
	// -CINetStats: what went each way, every five seconds.
	const bool bStats = FParse::Param(FCommandLine::Get(), TEXT("CINetStats"));
	double LastStatsAt = FPlatformTime::Seconds();
	int32 FromGame = 0, ToHostDirect = 0, ToRelay = 0, FromHostDirect = 0, FromRelay = 0, ToGame = 0;
	// Development: -CIDevDropTravel=<seconds> loses the game's packets that long after its second move to a next level
	// (the third connection), as a stalled connection would, to see it made again.
	float DevDropSeconds = 0.0f;
	FParse::Value(FCommandLine::Get(), TEXT("CIDevDropTravel="), DevDropSeconds);
	int32 GameAddresses = 0;
	double DevDropUntil = 0.0;
	while (!bStopping)
	{
		const double Now = FPlatformTime::Seconds();
		int32 Read = 0;
		int32 Sent = 0;
		if (bStats && Now - LastStatsAt >= 5.0)
		{
			UE_LOG(LogChaosImpact, Log, TEXT("NETSTATS guest tunnel: game %d -> host %d direct / %d relay; host %d direct / %d relay -> game %d (%s)"),
				FromGame, ToHostDirect, ToRelay, FromHostDirect, FromRelay, ToGame, GameAddress ? *GameAddress->ToString(true) : TEXT("no game"));
			LastStatsAt = Now;
			FromGame = ToHostDirect = ToRelay = FromHostDirect = FromRelay = ToGame = 0;
		}
		// Every second: where this guest can be reached (the relay answers with the host's addresses).
		if (Now - LastIntroducedAt >= GuestIntroduceSeconds)
		{
			LastIntroducedAt = Now;
			Outside->SendTo(Private.GetData(), Private.Num(), Sent, *RelayAddr);
		}
		while (Socket->RecvFrom(Buffer, sizeof(Buffer), Read, *From) && Read > 0)
		{
			// The game comes from a new port each time it connects again (the room's next level); answers follow it.
			if (!GameAddress || !(*From == *GameAddress))
			{
				GameAddress = From->Clone();
				DevDropUntil = ++GameAddresses == 3 && DevDropSeconds > 0.0f ? Now + DevDropSeconds : 0.0;
			}
			if (Now < DevDropUntil)
			{
				continue;
			}
			const FInternetAddr* Direct = Host.GetDirect(Now);
			Outside->SendTo(Buffer, Read, Sent, Direct ? *Direct : *RelayAddr);
			++FromGame;
			++(Direct ? ToHostDirect : ToRelay);
		}
		while (Outside->RecvFrom(Buffer, sizeof(Buffer), Read, *From) && Read > 0)
		{
			if (*From == *RelayAddr)
			{
				if (IsMessage(Buffer, Read, "CI2PHOST", DirectHeaderSize))
				{
					if (bDirect) { Host.Learn(Buffer, Read, Now); }
					continue;
				}
			}
			// From elsewhere: the host's probe or answer, or the game's packet from where the host was heard.
			else if (Host.Receive(*Outside, Buffer, Read, *From, Now) || IsDirectProbe(Buffer, Read) || !Host.IsFrom(*From))
			{
				continue;
			}
			++(*From == *RelayAddr ? FromRelay : FromHostDirect);
			if (GameAddress) { Socket->SendTo(Buffer, Read, Sent, *GameAddress); ++ToGame; }
		}
		Host.Probe(*Outside, Now);
		Outside->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromMicroseconds(500));
	}
	return 0;
}

uint32 FChaosImpactRelayGuestTunnel::RunWebSocket()
{
	uint8 Buffer[RelayMaxDatagram];
	TSharedRef<FInternetAddr> From = Sockets()->CreateInternetAddr();
	TSharedPtr<FInternetAddr> GameAddress;
	TArray<uint8> Data;
	while (!bStopping && !WebSocket->HasFailed())
	{
		int32 Read = 0;
		while (Socket->RecvFrom(Buffer, sizeof(Buffer), Read, *From) && Read > 0)
		{
			// This machine's game is the only one sending here; it comes from a new port each time it connects again
			// (the room moving to its next level), and the answers follow it there.
			if (!GameAddress || !(*From == *GameAddress)) { GameAddress = From->Clone(); }
			WebSocket->Send(Buffer, Read);
		}
		while (WebSocket->Receive(Data))
		{
			if (GameAddress) { int32 Sent = 0; Socket->SendTo(Data.GetData(), Data.Num(), Sent, *GameAddress); }
		}
		Socket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromMicroseconds(500));
	}
	return 0;
}
