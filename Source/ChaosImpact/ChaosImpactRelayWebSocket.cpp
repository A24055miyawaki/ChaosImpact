#include "ChaosImpactRelayWebSocket.h"
#include "ChaosImpactRelay.h"
#include "ChaosImpact.h"
#include "IWebSocket.h"
#include "WebSocketsModule.h"
#include "Modules/ModuleManager.h"
#include "Misc/ScopeLock.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	constexpr int32 MaxGameMessage = 2056;
	constexpr int32 MaxQueuedPackets = 512;
	/**
	 * Not connected after this long: a fresh connection is tried. On a line that loses packets the first one can sit
	 * for many seconds while the system waits longer and longer to repeat its lost opening (1 + 2 + 4 + 8 s...); a
	 * new connection sends its opening at once.
	 */
	constexpr double ConnectRetrySeconds = 2.5;
	/**
	 * Given up after this long without a connection. Some lines hold every new TCP connection for 20 s or more now and
	 * then (measured on a school/shop-style Wi-Fi); still inside the engine's own 60 s wait for the host.
	 */
	constexpr double ConnectGiveUpSeconds = 40.0;
	/** A request sent and not answered in this long has failed. */
	constexpr double AnswerSeconds = 10.0;
	class FRelayDirectoryWebSocket : public TSharedFromThis<FRelayDirectoryWebSocket>
	{
	public:
		void Request(const FString& Body, int32 Id, TFunction<void(TSharedPtr<FJsonObject>, int32)> Reply)
		{
			if (Pending.Num() >= 64) { Reply(nullptr, 0); return; }
			FPending& Item = Pending.Add(Id);
			Item.Body = Body; Item.Reply = MoveTemp(Reply); Item.CreatedAt = FPlatformTime::Seconds();
			if (!Ticker.IsValid())
			{
				Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateSP(AsShared(), &FRelayDirectoryWebSocket::Tick));
			}
			// Connected, or connecting since only just now: the request waits for it. Else (none yet, or one that
			// failed or gave up): a connection is made.
			const double Now = FPlatformTime::Seconds();
			if (Socket && (Socket->IsConnected() || (!bConnectFailed && Now - ConnectAt < ConnectRetrySeconds))) { return; }
			FirstConnectAt = Now;
			Open();
		}
		void Open()
		{
			CloseSocket();
			FModuleManager::LoadModuleChecked<FWebSocketsModule>(TEXT("WebSockets"));
			Socket = FWebSocketsModule::Get().CreateWebSocket(ChaosImpactRelay::GetWebSocketServer() + TEXT("/directory"));
			Socket->SetTextMessageMemoryLimit(65536);
			ConnectAt = FPlatformTime::Seconds();
			bConnectFailed = false;
			TWeakPtr<FRelayDirectoryWebSocket> Weak = AsShared();
			Socket->OnConnected().AddLambda([Weak]()
			{
				if (auto Self = Weak.Pin())
				{
					UE_LOG(LogChaosImpact, Log, TEXT("Relay WSS directory connected (%.0f ms)"), (FPlatformTime::Seconds() - Self->FirstConnectAt) * 1000.0);
				}
			});
			// Before it ever connected: tried again (Tick). Once connected: the requests waiting fail.
			Socket->OnConnectionError().AddLambda([Weak](const FString& Error)
			{
				if (auto Self = Weak.Pin())
				{
					(Self->Socket && Self->Socket->IsConnected() ? Self->bBroken : Self->bConnectFailed) = true;
					UE_LOG(LogChaosImpact, Warning, TEXT("Relay WSS directory: %s"), *Error);
				}
			});
			Socket->OnClosed().AddLambda([Weak](int32 Code, const FString&, bool)
			{
				if (auto Self = Weak.Pin()) { Self->bBroken = true; UE_LOG(LogChaosImpact, Log, TEXT("Relay WSS directory closed (%d)"), Code); }
			});
			Socket->OnMessage().AddLambda([Weak](const FString& Text)
			{
				auto Self = Weak.Pin();
				if (!Self) { return; }
				TSharedPtr<FJsonObject> Json;
				double Id = 0.;
				if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json
					|| !Json->TryGetNumberField(TEXT("rid"), Id)) { return; }
				FPending* Item = Self->Pending.Find(static_cast<int32>(Id));
				if (!Item) { return; }
				const int32 Rtt = FMath::Max(1, FMath::RoundToInt((FPlatformTime::Seconds() - Item->SentAt) * 1000.));
				auto Callback = MoveTemp(Item->Reply);
				Self->Pending.Remove(static_cast<int32>(Id));
				// A reply may open a game channel. Defer it until the WS manager has finished
				// iterating its sockets (creating one inside OnMessage invalidates that iteration).
				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
					[Callback = MoveTemp(Callback), Json, Rtt](float) mutable
					{
						Callback(Json, Rtt); return false;
					}));
			});
			Socket->Connect();
		}
		void Shutdown()
		{
			if (Ticker.IsValid()) { FTSTicker::GetCoreTicker().RemoveTicker(Ticker); Ticker.Reset(); }
			CloseSocket();
			auto Failed = MoveTemp(Pending); Pending.Reset();
			for (auto& Pair : Failed) { Pair.Value.Reply(nullptr, 0); }
		}
	private:
		struct FPending
		{
			FString Body;
			TFunction<void(TSharedPtr<FJsonObject>, int32)> Reply;
			double CreatedAt = 0., SentAt = 0.;
		};
		void CloseSocket()
		{
			if (!Socket) { return; }
			Socket->OnConnected().Clear(); Socket->OnConnectionError().Clear();
			Socket->OnClosed().Clear(); Socket->OnMessage().Clear(); Socket->Close(); Socket.Reset();
		}
		bool Tick(float)
		{
			if (bBroken)
			{
				bBroken = false; CloseSocket();
				auto Failed = MoveTemp(Pending); Pending.Reset();
				for (auto& Pair : Failed) { Pair.Value.Reply(nullptr, 0); }
				return true;
			}
			const double Now = FPlatformTime::Seconds();
			if (Socket && !Socket->IsConnected() && !Pending.IsEmpty() && Now - FirstConnectAt < ConnectGiveUpSeconds
				&& (bConnectFailed || Now - ConnectAt > ConnectRetrySeconds))
			{
				UE_LOG(LogChaosImpact, Log, TEXT("Relay WSS directory: not connected after %.1f s, connecting again"), Now - ConnectAt);
				Open();
			}
			TArray<int32> Expired;
			for (auto& Pair : Pending)
			{
				// Waiting for the connection does not count against the answer's time.
				if (Pair.Value.SentAt > 0. ? Now - Pair.Value.SentAt > AnswerSeconds : Now - Pair.Value.CreatedAt > ConnectGiveUpSeconds)
				{
					Expired.Add(Pair.Key);
				}
				else if (Pair.Value.SentAt == 0. && Socket && Socket->IsConnected())
				{
					Pair.Value.SentAt = Now; Socket->Send(Pair.Value.Body);
				}
			}
			for (int32 Id : Expired)
			{
				auto Callback = MoveTemp(Pending.FindChecked(Id).Reply); Pending.Remove(Id); Callback(nullptr, 0);
			}
			return true;
		}
		TSharedPtr<IWebSocket> Socket;
		TMap<int32, FPending> Pending;
		FTSTicker::FDelegateHandle Ticker;
		bool bBroken = false;
		bool bConnectFailed = false;
		double FirstConnectAt = 0., ConnectAt = 0.;
	};
	TSharedPtr<FRelayDirectoryWebSocket> Directory;
}

void ChaosImpactRelay::RequestWebSocket(const FString& Body, int32 Id, TFunction<void(TSharedPtr<FJsonObject>, int32)> Reply)
{
	check(IsInGameThread());
	if (GetWebSocketServer().IsEmpty()) { Reply(nullptr, 0); return; }
	if (!Directory) { Directory = MakeShared<FRelayDirectoryWebSocket>(); }
	Directory->Request(Body, Id, MoveTemp(Reply));
}

void ChaosImpactRelay::ShutdownWebSockets()
{
	if (Directory) { Directory->Shutdown(); Directory.Reset(); }
}

bool FChaosImpactRelayWebSocket::Start(const FString& Url)
{
	check(IsInGameThread());
	if (Url.IsEmpty()) { return false; }
	ChannelUrl = Url;
	StartedAt = FPlatformTime::Seconds();
	Open();
	Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateSP(AsShared(), &FChaosImpactRelayWebSocket::Tick));
	return true;
}

void FChaosImpactRelayWebSocket::Open()
{
	FScopeLock Guard(&SendMutex);
	if (Socket)
	{
		Socket->OnConnected().Clear(); Socket->OnConnectionError().Clear(); Socket->OnClosed().Clear();
		Socket->OnBinaryMessage().Clear(); Socket->Close(); Socket.Reset();
	}
	Fragment.Reset();
	FModuleManager::LoadModuleChecked<FWebSocketsModule>(TEXT("WebSockets"));
	Socket = FWebSocketsModule::Get().CreateWebSocket(ChannelUrl);
	ConnectAt = FPlatformTime::Seconds();
	bConnectFailed = false;
	TWeakPtr<FChaosImpactRelayWebSocket> Weak = AsShared();
	Socket->OnConnected().AddLambda([Weak]()
	{
		if (auto Self = Weak.Pin())
		{
			Self->bConnected = true;
			UE_LOG(LogChaosImpact, Log, TEXT("Relay WSS game channel connected (%.0f ms)"), (FPlatformTime::Seconds() - Self->StartedAt) * 1000.0);
		}
	});
	Socket->OnClosed().AddLambda([Weak](int32 Code, const FString&, bool)
	{
		if (auto Self = Weak.Pin()) { Self->bConnected = false; Self->bFailed = true; UE_LOG(LogChaosImpact, Log, TEXT("Relay WSS game channel closed (%d)"), Code); }
	});
	// Before it ever connected: tried again (Tick). Once connected: the channel has failed.
	Socket->OnConnectionError().AddLambda([Weak](const FString& Error)
	{
		if (auto Self = Weak.Pin())
		{
			(Self->bConnected ? Self->bFailed : Self->bConnectFailed) = true;
			UE_LOG(LogChaosImpact, Warning, TEXT("Relay WSS game channel: %s"), *Error);
		}
	});
	Socket->OnBinaryMessage().AddLambda([Weak](const void* Data, SIZE_T Size, bool bLast)
	{
		auto Self = Weak.Pin();
		if (!Self || Self->bFailed) { return; }
		if (Size + Self->Fragment.Num() > MaxGameMessage) { Self->bFailed = true; return; }
		Self->Fragment.Append(static_cast<const uint8*>(Data), static_cast<int32>(Size));
		if (bLast)
		{
			if (++Self->IncomingCount > MaxQueuedPackets) { --Self->IncomingCount; Self->bFailed = true; return; }
			Self->Incoming.Enqueue(MoveTemp(Self->Fragment)); Self->Fragment.Reset();
		}
	});
	Socket->Connect();
}

void FChaosImpactRelayWebSocket::Shutdown()
{
	check(IsInGameThread());
	if (Ticker.IsValid()) { FTSTicker::GetCoreTicker().RemoveTicker(Ticker); Ticker.Reset(); }
	if (Socket)
	{
		Socket->OnConnected().Clear(); Socket->OnConnectionError().Clear(); Socket->OnClosed().Clear();
		Socket->OnBinaryMessage().Clear(); Socket->Close(); Socket.Reset();
	}
	bConnected = false;
}

bool FChaosImpactRelayWebSocket::Send(const uint8* Data, int32 Size)
{
	if (Size <= 0 || Size > MaxGameMessage || bFailed) { return false; }
	FScopeLock Guard(&SendMutex);
#if WITH_LIBWEBSOCKETS
	// UE's LWS backend enqueues Send() and wakes its network thread. One serialized producer,
	// with Shutdown only after joining the bridge thread: don't wait for another rendered frame.
	if (bBackgroundSendReady)
	{
		if (bBackgroundSendReady && Socket && !bFailed)
		{
			Socket->Send(Data, Size, true); return true;
		}
	}
#endif
	if (++OutgoingCount > MaxQueuedPackets) { --OutgoingCount; bFailed = true; return false; }
	TArray<uint8> Copy; Copy.Append(Data, Size); Outgoing.Enqueue(MoveTemp(Copy)); return true;
}

bool FChaosImpactRelayWebSocket::Receive(TArray<uint8>& Data)
{
	if (!Incoming.Dequeue(Data)) { return false; }
	--IncomingCount; return true;
}

bool FChaosImpactRelayWebSocket::Tick(float)
{
	const double Now = FPlatformTime::Seconds();
	if (!bConnected && !bFailed)
	{
		if (Now - StartedAt > ConnectGiveUpSeconds) { bFailed = true; }
		else if (bConnectFailed || Now - ConnectAt > ConnectRetrySeconds)
		{
			UE_LOG(LogChaosImpact, Log, TEXT("Relay WSS game channel: not connected after %.1f s, connecting again"), Now - ConnectAt);
			Open();
		}
	}
	if (bFailed)
	{
		bBackgroundSendReady = false;
		bConnected = false;
		if (Socket) { Socket->Close(1000, TEXT("relay transport stopped")); }
		return false;
	}
	if (Socket && bConnected)
	{
		FScopeLock Guard(&SendMutex);
		TArray<uint8> Data;
		for (int32 Count = 0; Count < 512 && Outgoing.Dequeue(Data); ++Count)
		{
			--OutgoingCount; Socket->Send(Data.GetData(), Data.Num(), true);
		}
		bBackgroundSendReady = true;
	}
	return true;
}
