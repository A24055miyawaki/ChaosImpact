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
 * lives there, and it tells the players each other's addresses so their packets can go directly (FChaosImpactDirectPath);
 * where that does not get through their routers, every packet goes through the relay, so no one opens ports on their
 * router. The room's host is still a player's PC (a listen server); the relay runs nothing of the game.
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
 * The direct line between the room's host and one of its guests, past the relay (UDP only): the relay told each the
 * other's addresses (the one it sees, and those on the player's own network) and a token for the two of them. Both
 * probe every address; while the other end answers, the game's packets go there directly, and back through the relay
 * as soon as the answers stop. Used on a tunnel's thread only.
 */
struct FChaosImpactDirectPath
{
	uint32 ClientId = 0;
	uint8 Token[8] = {};
	/** Where the other end might be reached. */
	TArray<TSharedRef<FInternetAddr>> Candidates;
	/** Where the other end has been heard from with the token: its game packets are taken from there. */
	TArray<TSharedRef<FInternetAddr>> Heard;
	/** Where the game's packets go while it keeps answering. */
	TSharedPtr<FInternetAddr> Address;
	double LastAnswerAt = -100.0;
	double LastProbeAt = -100.0;
	double LastIntroducedAt = -100.0;
	bool bDirect = false;

	/** The relay's b"CI2PHOST"/b"CI2PPEER" + client id + token + "pub=ip:port;priv=ip:port,...". */
	void Learn(const uint8* Data, int32 Size, double Now);
	/** Probes every address (once one answers, just that one) when it is time. */
	void Probe(FSocket& Socket, double Now);
	/** A probe (answered) or an answer from the other end: true when it was one. */
	bool Receive(FSocket& Socket, const uint8* Data, int32 Size, const FInternetAddr& From, double Now);
	bool IsFrom(const FInternetAddr& From) const;
	/** Where to send the game's packets directly; null: through the relay. */
	const FInternetAddr* GetDirect(double Now);
};

/**
 * The host's end of a relayed room: one socket to the relay's room port (the guests' direct packets come to it too),
 * and one local socket per remote player that talks to this game's own listen server on 127.0.0.1, so the engine sees
 * each of them as a separate client. Runs on its own thread, so a packet never waits for a game frame.
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
	/**
	 * The room has opened its next level (its game listens afresh): every remote player's local socket starts over, so
	 * nothing left over from the last level's game (or from the moment nothing listened) stands in the way.
	 */
	void ResetClients() { bResetClients = true; }

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
	/** A packet on the relay socket from somewhere other than the relay: a guest's, directly. */
	void ReceiveDirect(const uint8* Data, int32 Size, const FInternetAddr& From, double Now);

	FString RelayHost;
	int32 RoomPort = 0;
	FString RoomKey;
	int32 LocalGamePort = 7777;
	FSocket* RelaySocket = nullptr;
	TSharedPtr<FInternetAddr> RelayAddr;
	TSharedPtr<FInternetAddr> LocalGameAddr;
	TMap<uint32, FClient> Clients;
	/** Each remote player's direct line (kept when their local sockets start over). */
	TMap<uint32, FChaosImpactDirectPath> Peers;
	FRunnableThread* Thread = nullptr;
	TAtomic<bool> bStopping{false};
	TAtomic<bool> bResetClients{false};
	TAtomic<double> LastAckAt{0.0};
	TSharedPtr<FChaosImpactRelayWebSocket> WebSocket;
};

/**
 * A guest's end: the unchanged UE IP driver connects to a loopback UDP socket here, and its packets go on to the host
 * directly when the two can reach each other (FChaosImpactDirectPath), otherwise through the relay's room port (UDP)
 * or WebSocket (WSS, never direct). One outside socket for the whole stay in the room, so the relay and the host know
 * this guest by the same address across the room's levels.
 */
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
	uint32 RunWebSocket();
	uint32 RunUdp();

	int32 RoomPort = 0, LocalPort = 0;
	FSocket* Socket = nullptr;
	/** UDP: to the relay and to the host. */
	FSocket* Outside = nullptr;
	TSharedPtr<FInternetAddr> RelayAddr;
	FChaosImpactDirectPath Host;
	FRunnableThread* Thread = nullptr;
	TSharedPtr<FChaosImpactRelayWebSocket> WebSocket;
	TAtomic<bool> bStopping{false};
};
