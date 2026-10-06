#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Engine/StreamableManager.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ChaosImpactFxPreloadSubsystem.generated.h"

class UNiagaraSystem;

/**
 * Loads every ball-effect asset (Niagara systems, FX materials, shapes, the results' poses) once per session and keeps
 * them loaded, so none of them is loaded from disk in the middle of a throw. At the title it loads in the background
 * (the start-up loading screen can be played with meanwhile); anywhere else it loads at once (FinishNow).
 */
UCLASS()
class UChaosImpactFxPreloadSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UChaosImpactFxPreloadSubsystem* Get(const UObject* WorldContext);
	virtual void Deinitialize() override;

	/** Starts loading in the background (once). */
	void StartAsync();
	/** Loads whatever is left right now (waits for it). */
	void FinishNow();
	bool IsComplete() const { return Phase == EPhase::Done; }
	/** 0-1: files read, then effects made ready. */
	float GetProgress() const;

private:
	enum class EPhase : uint8
	{
		Idle,
		Loading,
		Compiling,
		Done
	};

	void OnLoaded();
	bool Poll(float DeltaSeconds);
	void Complete();

	/** Held here so garbage collection never unloads them between rounds. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> PreloadedAssets;

	FStreamableManager Streamable;
	TSharedPtr<FStreamableHandle> Handle;
	TArray<TWeakObjectPtr<UNiagaraSystem>> StillCompiling;
	int32 CompileTotal = 0;
	FTSTicker::FDelegateHandle Ticker;
	EPhase Phase = EPhase::Idle;
	double StartedAt = 0.0;
};
