#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ChaosImpactFxPreloadSubsystem.generated.h"

/**
 * Loads every ball-effect asset (Niagara systems and FX materials) when the game starts, so none of them
 * is loaded from disk in the middle of a throw, and keeps them loaded for the whole session.
 */
UCLASS()
class UChaosImpactFxPreloadSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

private:
	/** Held here so garbage collection never unloads them between rounds. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> PreloadedAssets;
};
