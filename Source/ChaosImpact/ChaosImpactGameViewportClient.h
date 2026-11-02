#pragma once

#include "CoreMinimal.h"
#include "Engine/GameViewportClient.h"
#include "ChaosImpactGameViewportClient.generated.h"

/** Captures join-button presses before Unreal routes them to an existing local player. */
UCLASS()
class UChaosImpactGameViewportClient : public UGameViewportClient
{
	GENERATED_BODY()

public:
	virtual bool InputKey(const FInputKeyEventArgs& EventArgs) override;
	virtual void LayoutPlayers() override;
	virtual void Tick(float DeltaTime) override;
	/** Duel: one fighter and one spectator on this machine share the fighter's full view until the results end. */
	static ULocalPlayer* GetSingleDuelPlayer(const UWorld* World);
	/** Optional: a pad press that no local player owns (the RPG prototype shows "join" for it). True when used. */
	static TFunction<bool(const FInputKeyEventArgs&)> UnownedGamepadHandler;

private:
	bool bSingleDuelLayout = false;
};
