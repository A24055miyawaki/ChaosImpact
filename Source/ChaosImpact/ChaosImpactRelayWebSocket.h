#pragma once

#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"

class IWebSocket;

namespace ChaosImpactRelay
{
	void RequestWebSocket(const FString& Body, int32 RequestId,
		TFunction<void(TSharedPtr<FJsonObject>, int32)> OnReply);
	void ShutdownWebSockets();
}

/** Game-thread WebSocket callbacks, bounded cross-thread datagram queues. TLS validation stays enabled. */
class FChaosImpactRelayWebSocket : public TSharedFromThis<FChaosImpactRelayWebSocket>
{
public:
	bool Start(const FString& Url);
	void Shutdown();
	bool Send(const uint8* Data, int32 Size);
	bool Receive(TArray<uint8>& Data);
	bool IsConnected() const { return bConnected.Load(); }
	bool HasFailed() const { return bFailed.Load(); }
private:
	bool Tick(float Delta);
	/** A (new) connection to the channel; its packets wait queued until one is made. */
	void Open();
	TSharedPtr<IWebSocket> Socket;
	FString ChannelUrl;
	FTSTicker::FDelegateHandle Ticker;
	TQueue<TArray<uint8>, EQueueMode::Mpsc> Outgoing, Incoming;
	TArray<uint8> Fragment;
	TAtomic<int32> OutgoingCount{0}, IncomingCount{0};
	TAtomic<bool> bConnected{false}, bFailed{false};
	TAtomic<bool> bBackgroundSendReady{false};
	FCriticalSection SendMutex;
	double StartedAt = 0.;
	double ConnectAt = 0.;
	TAtomic<bool> bConnectFailed{false};
};
