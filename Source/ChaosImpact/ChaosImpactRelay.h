#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "HAL/Runnable.h"

class FRunnableThread;
class FSocket;
class FInternetAddr;
class FChaosImpactRelayWebSocket;

/**
 * Internet play for 合言葉 rooms through the relay (Tools/OnlineRelay/ci_relay.py, run on a cloud VM): the room list
 * lives there, and every packet between the players goes through it, so no one opens ports on their router. The room's
 * host is still a player's PC (a listen server); the relay runs nothing of the game.
 *
 * Enabled when [ChaosImpact.Online] RelayServer=<address>:<port> is set (DefaultGame.ini, or GameUserSettings.ini on
 * one PC), or -CIRelay=<address>:<port> on the command line. Empty: rooms are found on the LAN as before.
 */
namespace ChaosImpactRelay
{
	/** The relay's directory address ("" when internet play is off). */
	FString GetServer();
	bool IsEnabled();
	FString GetWebSocketServer();
	bool UseWebSocket();
	FString GetRoomWebSocketUrl(int32 Port);
	/** Only rooms made by the same build are listed (players of different versions cannot play together). */
	FString GetNetVersion();

	/**
	 * One request to the directory, answered on the game thread: the reply (null when the relay did not answer),
	 * and the round trip in milliseconds. Sent and waited for on a worker thread, retried a few times.
	 */
	void Request(TSharedRef<FJsonObject> Message, TFunction<void(TSharedPtr<FJsonObject> Reply, int32 RoundTripMs)> OnReply);
}

/**
 * The host's end of a relayed room: one socket to the relay's room port, and one local socket per remote player that
 * talks to this game's own listen server on 127.0.0.1, so the engine sees each of them as a separate client. Runs on
 * its own thread, so a packet never waits for a game frame.
 */
class FChaosImpactRelayTunnel : public FRunnable
{
public:
	FChaosImpactRelayTunnel(const FString& RelayHost, int32 RoomPort, const FString& RoomKey, int32 LocalGamePort);
	virtual ~FChaosImpactRelayTunnel() override;

	bool Start();
	void Shutdown();
	/** The relay has answered the host's registration at least once in the last few seconds. */
	bool IsConnected() const;

	virtual uint32 Run() override;
	virtual void Stop() override { bStopping = true; }

private:
	struct FClient
	{
		FSocket* Socket = nullptr;
		double LastSeen = 0.0;
	};

	void SendToRelay(const uint8* Data, int32 Size);
	FClient* FindOrOpenClient(uint32 ClientId, double Now);
	void CloseClient(uint32 ClientId);

	FString RelayHost;
	int32 RoomPort = 0;
	FString RoomKey;
	int32 LocalGamePort = 7777;
	FSocket* RelaySocket = nullptr;
	TSharedPtr<FInternetAddr> RelayAddr;
	TSharedPtr<FInternetAddr> LocalGameAddr;
	TMap<uint32, FClient> Clients;
	FRunnableThread* Thread = nullptr;
	TAtomic<bool> bStopping{false};
	TAtomic<double> LastAckAt{0.0};
	TSharedPtr<FChaosImpactRelayWebSocket> WebSocket;
};

/** Guest's loopback UDP socket: the unchanged UE IP driver sends its game packets through WSS. */
class FChaosImpactRelayGuestTunnel : public FRunnable
{
public:
	explicit FChaosImpactRelayGuestTunnel(int32 InRoomPort) : RoomPort(InRoomPort) {}
	virtual ~FChaosImpactRelayGuestTunnel();
	bool Start();
	void Shutdown();
	int32 GetLocalPort() const { return LocalPort; }
	virtual uint32 Run() override;
	virtual void Stop() override { bStopping = true; }
private:
	int32 RoomPort = 0, LocalPort = 0;
	FSocket* Socket = nullptr;
	FRunnableThread* Thread = nullptr;
	TSharedPtr<FChaosImpactRelayWebSocket> WebSocket;
	TAtomic<bool> bStopping{false};
};
