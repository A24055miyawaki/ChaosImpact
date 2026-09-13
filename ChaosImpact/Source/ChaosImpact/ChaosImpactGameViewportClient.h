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
};
