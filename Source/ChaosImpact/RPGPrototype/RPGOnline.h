#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "OnlineSessionSettings.h"
#include "Containers/Ticker.h"
#include "ChaosImpactRelay.h"
#include "RPGPrototype.h"
#include "RPGOnline.generated.h"

class UNetDriver;

/**
 * Online play for the RPG, found by a password like VS rooms: over the internet through the relay when one is set
 * (ChaosImpactRelay: the players talk directly where they can), else on the LAN. Up to four players share one world,
 * hosted by the machine that opened it; everyone else joins it while it is being played.
 *
 * Who decides what:
 * - The host runs the world: enemies, bosses, balls and their hits, hearts and herbs, and every hero's HP, down
 *   state, knockouts and respawns.
 * - Each player's own machine keeps that hero: bag, equipment, level, XP, rubies, quests, chests, the merchant and
 *   the save. It tells the host what it fights with (ARPGPrototypeCharacter::ServerSyncHero) and gets its rewards
 *   from the host (ClientKillCredit). Quests are each player's own, as offline: together or each their own.
 * - Movement belongs to each player's own machine (as in VS); the host moves a guest only for a respawn or a fall.
 */

/** The Shima-enaga's warnings and blasts, sent to guests (ChaosImpactSimaeFx::FNetFx). */
USTRUCT()
struct FRPGSimaeFxNet
{
	GENERATED_BODY()
	UPROPERTY() uint8 Kind = 0;
	UPROPERTY() int32 Id = 0;
	UPROPERTY() FVector_NetQuantize A = FVector::ZeroVector;
	UPROPERTY() FVector_NetQuantizeNormal B = FVector::ForwardVector;
	UPROPERTY() float F0 = 0;
	UPROPERTY() float F1 = 0;
	UPROPERTY() float F2 = 0;
	UPROPERTY() int32 Count = 0;
	UPROPERTY() bool bFlag = false;
	UPROPERTY() FLinearColor Color = FLinearColor::Red;
};

class AChaosImpactSimaeWarning;

/** Damage numbers and hit effects on every screen. One per world, made by the host. */
UCLASS(NotBlueprintable)
class ARPGNetRelay : public AActor
{
	GENERATED_BODY()
public:
	ARPGNetRelay();
	static ARPGNetRelay* Get(const UWorld* World);
	/** Host (or offline): once per world. */
	static ARPGNetRelay* Ensure(UWorld* World);
	/** A guest's damage numbers (the host keeps its own in the game mode). */
	TArray<FRPGPrototypeDamageText> DamageTexts;
	UFUNCTION(NetMulticast, Unreliable) void MulticastDamageText(FVector_NetQuantize At, float Amount, bool bPlayer);
	UFUNCTION(NetMulticast, Unreliable) void MulticastImpact(EChaosImpactBallType Type, FVector_NetQuantize At, float Scale, bool bTerminal);
	UFUNCTION(NetMulticast, Unreliable) void MulticastLightning(FVector_NetQuantize Start, FVector_NetQuantize End);
	/** The boss's warnings must arrive: reliable. */
	UFUNCTION(NetMulticast, Reliable) void MulticastSimaeFx(const FRPGSimaeFxNet& Fx);
	/** Host: from now on the boss's effects also go to guests (cleared at the end of the world). */
	static void BindSimaeFx(bool bBind);
private:
	TMap<int32, TWeakObjectPtr<AChaosImpactSimaeWarning>> GuestWarnings;
public:
	/** The colony this world shows: the host's (RPGColony). */
	UPROPERTY(Replicated) int32 ColonyStage = 0;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
public:
	virtual void Tick(float DeltaSeconds) override;
};

UENUM()
enum class ERPGOnlineState : uint8 { Offline, Hosting, Searching, Joining, Guest };

/** A world found with the password. */
struct FRPGWorldListing
{
	FString Host;
	int32 Players = 1;
	int32 PingMs = 0;
	/** LAN: the session to join. */
	FOnlineSessionSearchResult Result;
	/** Internet: the world's room on the relay. */
	int32 RelayPort = 0;
};

UCLASS()
class URPGOnlineSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	static URPGOnlineSubsystem* Get(const UObject* Context);
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	static constexpr int32 MaxPlayers = 4;
	static constexpr int32 MaxPasswordLength = 12;
	/** The password used last time (remembered on this machine). */
	FString GetSavedPassword() const;
	void SavePassword(const FString& Password) const;
	static FString RandomPassword();
	/** Opens this world (it keeps running, nothing reloads) to anyone who searches with the password. */
	bool Host(UWorld* World, const FString& Password);
	void StartSearch(const FString& Password);
	void StopSearch();
	/** Into a listed world (this machine plays one player there). */
	bool Join(int32 Index);
	/** Stops hosting / leaves the host's world. The caller then opens the title. */
	void Leave();
	/** Host: how many play in the world now (shown in the list). */
	void SetPlayerCount(int32 Count);
	ERPGOnlineState GetState() const { return State; }
	bool IsHosting() const { return State == ERPGOnlineState::Hosting; }
	bool IsGuest() const { return State == ERPGOnlineState::Guest || State == ERPGOnlineState::Joining; }
	const TArray<FRPGWorldListing>& GetListings() const { return Listings; }
	bool HasSearchedOnce() const { return bSearchedOnce; }
	const FString& GetPassword() const { return Password; }
	/** A short message for the title (a failed join, the host gone). Empty when none is fresh. */
	FString GetNotice() const { return FPlatformTime::Seconds() - NoticeAt < 6 ? Notice : FString(); }
	/** Name shown to searchers: this machine's player name (the VS one). */
	FString GetHostName() const;
private:
	IOnlineSessionPtr GetSessions() const;
	void BindDelegates();
	void BeginFind();
	void PostNotice(const FString& Text);
	void HandleCreateComplete(FName SessionName, bool bSuccess);
	void HandleFindComplete(bool bSuccess);
	void HandleJoinComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result);
	void HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type Type, const FString& Error);
	void HandleTravelFailure(UWorld* World, ETravelFailure::Type Type, const FString& Error);
	void HandleMapLoaded(UWorld* World);
	void LostConnection(const FString& Error);
	bool TickRefresh(float DeltaSeconds);

	// ---- Over the internet (the relay's rooms, versioned apart from VS: see RelayVersion)
	static FString RelayVersion();
	void RelayCreate();
	void RelayHeartbeat();
	void RelaySearch();
	void StartRelayTunnel();
	void StopRelay();
	bool bRelay = false;
	FString RelayRoomId;
	FString RelayRoomKey;
	int32 RelayRoomPort = 0;
	/** Bumped on every new world or search: answers to older requests are ignored. */
	int32 RelayGeneration = 0;
	bool bRelayRequestInFlight = false;
	double NextHeartbeatAt = 0;
	TUniquePtr<FChaosImpactRelayTunnel> Tunnel;
	TUniquePtr<FChaosImpactRelayGuestTunnel> GuestTunnel;
	/** Development: two copies of the game on one PC play together (-CIRPGNet=host|guest -CIRPGNetPass=...). */
	void TickSmoke();
	FString SmokeRole, SmokePassword;
	int32 SmokeStep = 0;
	double SmokeAt = 0, SmokeT0 = 0;
	bool bSmokeShotDowned = false;
	int32 Money = 0, SmokeTicks = 0;
	bool bSmokeEventSeen = false;
	ERPGOnlineState State = ERPGOnlineState::Offline;
	FString Password;
	FString Notice;
	double NoticeAt = -100;
	bool bDelegatesBound = false;
	bool bSearchedOnce = false;
	/** After a lost connection the engine opens its default map; from there, straight back to the RPG title. */
	bool bReturnToTitle = false;
	int32 PlayerCount = 1;
	TSharedPtr<FOnlineSessionSearch> Search;
	TArray<FRPGWorldListing> Listings;
	double NextFindAt = 0;
	FTSTicker::FDelegateHandle Ticker;
	FDelegateHandle NetworkFailureHandle, TravelFailureHandle, MapLoadedHandle;
};
