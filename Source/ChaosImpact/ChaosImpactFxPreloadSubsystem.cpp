#include "ChaosImpactFxPreloadSubsystem.h"

#include "ChaosImpact.h"
#include "ChaosImpactBallTypes.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "NiagaraSystem.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

UChaosImpactFxPreloadSubsystem* UChaosImpactFxPreloadSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UChaosImpactFxPreloadSubsystem>() : nullptr;
}

void UChaosImpactFxPreloadSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
	Ticker.Reset();
	Super::Deinitialize();
}

void UChaosImpactFxPreloadSubsystem::StartAsync()
{
	if (Phase != EPhase::Idle)
	{
		return;
	}
	TArray<FSoftObjectPath> Paths;
	ChaosImpactBallTypes::GetPreloadPaths(Paths);
	StartedAt = FPlatformTime::Seconds();
	Phase = EPhase::Loading;
	Handle = Streamable.RequestAsyncLoad(Paths, FStreamableDelegate::CreateUObject(this, &UChaosImpactFxPreloadSubsystem::OnLoaded),
		FStreamableManager::AsyncLoadHighPriority);
	Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UChaosImpactFxPreloadSubsystem::Poll));
	UE_LOG(LogChaosImpact, Log, TEXT("Preloading %d ball FX assets in the background"), Paths.Num());
}

void UChaosImpactFxPreloadSubsystem::OnLoaded()
{
	if (Phase != EPhase::Loading)
	{
		return;
	}
	TArray<UObject*> Loaded;
	if (Handle.IsValid())
	{
		Handle->GetLoadedAssets(Loaded);
	}
	for (UObject* Asset : Loaded)
	{
		if (!Asset)
		{
			continue;
		}
		PreloadedAssets.AddUnique(Asset);
#if WITH_EDITOR
		// Uncooked (editor / PIE) systems compile their scripts on first use: let that finish in the background too.
		if (UNiagaraSystem* System = Cast<UNiagaraSystem>(Asset))
		{
			StillCompiling.Add(System);
		}
#endif
	}
	CompileTotal = StillCompiling.Num();
	Phase = EPhase::Compiling;
}

bool UChaosImpactFxPreloadSubsystem::Poll(const float DeltaSeconds)
{
	if (Phase == EPhase::Compiling)
	{
#if WITH_EDITOR
		StillCompiling.RemoveAll([](const TWeakObjectPtr<UNiagaraSystem>& System)
		{
			return !System.IsValid() || (System->PollForCompilationComplete(true) && !System->HasOutstandingCompilationRequests(true));
		});
#else
		StillCompiling.Reset();
#endif
#if WITH_EDITOR
		// Uncooked, their materials' shaders are compiled in the background as well: drawing an effect whose
		// shaders are only half there can bring the renderer down, so they are waited for too.
		// (Never longer than half a minute in all: other compiling in the editor must not hold the game up.)
		const bool bShadersBusy = GShaderCompilingManager && GShaderCompilingManager->IsCompiling()
			&& FPlatformTime::Seconds() - StartedAt < 30.0;
#else
		const bool bShadersBusy = false;
#endif
		if (StillCompiling.IsEmpty() && !bShadersBusy)
		{
			Complete();
		}
	}
	return Phase != EPhase::Done;
}

void UChaosImpactFxPreloadSubsystem::Complete()
{
	// Everything is in memory now; this only resolves the cached materials and keeps them (and anything missed).
	ChaosImpactBallTypes::PreloadAssets(PreloadedAssets);
	Phase = EPhase::Done;
	Handle.Reset();
	UE_LOG(LogChaosImpact, Log, TEXT("Ball FX ready (%.0f ms since the start of loading)"), (FPlatformTime::Seconds() - StartedAt) * 1000.0);
}

void UChaosImpactFxPreloadSubsystem::FinishNow()
{
	if (Phase == EPhase::Done)
	{
		return;
	}
	if (StartedAt <= 0.0)
	{
		StartedAt = FPlatformTime::Seconds();
	}
	if (Handle.IsValid() && Phase == EPhase::Loading)
	{
		Handle->WaitUntilComplete();
	}
	FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
	Ticker.Reset();
	StillCompiling.Reset();
	Complete();
#if WITH_EDITOR
	// (See Poll.) Everything is loaded and its scripts compiled; its shaders too before anything is drawn with them.
	if (GShaderCompilingManager)
	{
		GShaderCompilingManager->FinishAllCompilation();
	}
#endif
}

float UChaosImpactFxPreloadSubsystem::GetProgress() const
{
	switch (Phase)
	{
	case EPhase::Idle: return 0.0f;
	case EPhase::Loading: return Handle.IsValid() ? 0.6f * Handle->GetProgress() : 0.0f;
	case EPhase::Compiling: return CompileTotal > 0 ? 0.6f + 0.4f * (1.0f - static_cast<float>(StillCompiling.Num()) / CompileTotal) : 1.0f;
	default: return 1.0f;
	}
}
