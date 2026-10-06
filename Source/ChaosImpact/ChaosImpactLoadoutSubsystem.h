#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ChaosImpactLoadoutSubsystem.generated.h"

/** The character and colour one local player picked. */
struct FChaosImpactLoadout
{
	int32 Character = 0;
	/** INDEX_NONE until the player has picked one. */
	int32 Colour = INDEX_NONE;
	/** The nickname whose controls this player uses (empty: the guest's). Changed only through SetNickname. */
	FString Nickname;
};

/**
 * What each local player (P1-P4 on this machine) picked on the character select screen. Kept across level
 * travel and saved with the user settings, so the next session starts from the same choices.
 */
UCLASS()
class UChaosImpactLoadoutSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static constexpr int32 MaxLocalPlayers = 4;

	static UChaosImpactLoadoutSubsystem* Get(const UObject* WorldContext);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	FChaosImpactLoadout GetLoadout(int32 LocalPlayerIndex) const;
	/** The character and colour; the nickname is kept. */
	void SetLoadout(int32 LocalPlayerIndex, const FChaosImpactLoadout& Loadout);
	void SetNickname(int32 LocalPlayerIndex, const FString& Nickname);

private:
	FChaosImpactLoadout Loadouts[MaxLocalPlayers];
};
