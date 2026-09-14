#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ChaosImpactSessionSubsystem.generated.h"

class FOnlineSessionSearch;
class UNetDriver;

UENUM(BlueprintType)
enum class EChaosImpactRoomState : uint8
{
	None,
	/** Checking the password is unused, then creating the LAN session. */
	Creating,
	/** This machine is the listen-server host of a room. */
	Hosting,
	/** Playing local training while repeatedly looking for a room with the password. */
	Searching,
	Joining,
	/** Connected to someone else's room as a client. */
	InRoom
};

/**
 * Room lifecycle for the LAN multiplayer mode. Lives on the GameInstance so searching keeps
 * running while the player waits in the local training arena and survives map travel.
 */
UCLASS()
class UChaosImpactSessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	static UChaosImpactSessionSubsystem* Get(const UObject* WorldContext);

	FString GetPlayerName() const;
	bool HasSavedPlayerName() const;
	void SetPlayerName(const FString& Name);
	static constexpr int32 MaxNameLength = 10;

	void CreateRoom(const FString& InPassword);
	void StartSearch(const FString& InPassword);
	void StopSearch();
	/** Abandons a room that is still being created (before travelling to it). */
	void CancelCreate();
	/** Leaves or dissolves the room and returns this machine to its own training arena. */
	void LeaveRoom();
	/** Host only: advertise whether new members may still join, and how many are inside. */
	void SetRecruitmentOpen(bool bOpen);
	void SetMemberCount(int32 Count);

	EChaosImpactRoomState GetState() const { return State; }
	const FString& GetPassword() const { return Password; }
	/** Non-empty when the last CreateRoom attempt failed (for example the password is in use). */
	const FString& GetCreateError() const { return CreateError; }
	const FString& GetNotice() const { return Notice; }
	double GetNoticeTime() const { return NoticeAt; }
	/** True once after a disconnect, so the title world can send the player back to training. */
	bool ConsumeReturnToTraining();

	static FString GetTrainingMapName();
	static FString GetOfflineTrainingOptions();

private:
	IOnlineSessionPtr GetSessions() const;
	void BindSessionDelegates();
	void BeginFind();
	void CreateSessionNow();
	void PublishRoomSettings();
	void PostNotice(const FString& Message);
	void HandleFindComplete(bool bWasSuccessful);
	void HandleCreateComplete(FName SessionName, bool bWasSuccessful);
	void HandleJoinComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result);
	void HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType,
		const FString& ErrorString);
	void HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString);
	void ReturnAfterDisconnect(const FString& ErrorString);

	EChaosImpactRoomState State = EChaosImpactRoomState::None;
	FString Password;
	FString CreateError;
	FString Notice;
	double NoticeAt = -100.0;
	FString GeneratedName;
	bool bCheckingPassword = false;
	bool bReturnToTraining = false;
	bool bSessionDelegatesBound = false;
	bool bRecruitmentOpen = true;
	int32 MemberCount = 1;
	TSharedPtr<FOnlineSessionSearch> Search;
	FTimerHandle RetryTimer;
	FDelegateHandle NetworkFailureHandle;
	FDelegateHandle TravelFailureHandle;
};
