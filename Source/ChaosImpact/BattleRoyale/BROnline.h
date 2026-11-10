#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "ChaosImpactRelay.h"
#include "Engine/EngineBaseTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "BROnline.generated.h"

class UNetDriver;

/**
 * Online play for the battle royale: a four-digit word finds the room on the same relay as every other online mode
 * (the Oracle server; players talk directly where they can). The relay keeps rooms of each version apart, so
 * nothing on the server has to change for this. One machine hosts; up to 8 people in all.
 */
enum class EBRNetState : uint8 { Offline, Hosting, Searching, Guest };

UCLASS()
class UBROnlineSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UBROnlineSubsystem* Get(const UObject* Context);
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Opens this world to the room's word. */
	bool Host(UWorld* World, const FString& Word);
	/** Looks for the word, and goes in as soon as it is found. */
	void StartSearch(const FString& Word);
	void Stop();
	void SetPlayerCount(int32 Count, bool bOpen);

	EBRNetState GetState() const { return State; }
	const FString& GetWord() const { return Password; }
	const FString& GetNotice() const { return Notice; }
	bool IsRelayUp() const { return ChaosImpactRelay::IsEnabled(); }

private:
	bool TickNet(float DeltaSeconds);
	void RelayCreate();
	void StartTunnel();
	void RelayHeartbeat();
	void RelaySearch();
	void JoinPort(int32 Port);
	void CloseRoom();
	void HandleMapLoaded(UWorld* World);
	void HandleNetworkFailure(UWorld* World, UNetDriver* Driver, ENetworkFailure::Type Type, const FString& Error);
	void SetNotice(const FString& Text);
	FString HostName() const;
	static FString RoomVersion();

	FTSTicker::FDelegateHandle Ticker;
	FDelegateHandle MapLoadedHandle, NetworkFailureHandle;
	EBRNetState State = EBRNetState::Offline;
	FString Password, Notice;
	FString RoomId, RoomKey;
	int32 RoomPort = 0;
	int32 Players = 1;
	bool bOpen = true;
	int32 Generation = 0;
	bool bRequestInFlight = false;
	double NextHeartbeatAt = 0.0, NextSearchAt = 0.0;
	TUniquePtr<FChaosImpactRelayTunnel> Tunnel;
	TUniquePtr<FChaosImpactRelayGuestTunnel> GuestTunnel;
	FString DevHost, DevJoin;
	bool bDevDone = false;
};
