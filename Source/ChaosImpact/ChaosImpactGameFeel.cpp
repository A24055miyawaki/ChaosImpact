#include "ChaosImpactGameFeel.h"

#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"

namespace
{
	/** How slow the held world runs (not quite zero, so nothing waiting on time divides by it). */
	constexpr float HeldSpeed = 0.03f;
	/** The longest hold one burst of hits can add up to. */
	constexpr double LongestHold = 0.12;

	/** After a hold, none again for this long: a flurry of hits is one hold, not a stutter. */
	constexpr double RestBetweenHolds = 0.4;

	TWeakObjectPtr<UWorld> HeldWorld;
	double HeldUntil = 0.0;
	double ReleasedAt = -100.0;
	FTSTicker::FDelegateHandle Release;

	bool ReleaseWhenDue(float)
	{
		UWorld* World = HeldWorld.Get();
		if (World && FPlatformTime::Seconds() < HeldUntil)
		{
			return true;
		}
		if (World && FMath::IsNearlyEqual(UGameplayStatics::GetGlobalTimeDilation(World), HeldSpeed, 0.001f))
		{
			UGameplayStatics::SetGlobalTimeDilation(World, 1.0f);
		}
		HeldWorld.Reset();
		Release.Reset();
		ReleasedAt = FPlatformTime::Seconds();
		return false;
	}
}

bool ChaosImpactFeel::HitStop(const UObject* WorldContext, const float Seconds)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	static const bool bOff = FParse::Param(FCommandLine::Get(), TEXT("CINoHitStop"));
	if (bOff || !World || !World->IsGameWorld() || World->GetNetMode() != NM_Standalone || World->IsPaused() || Seconds <= 0.0f)
	{
		return false;
	}
	const float Speed = UGameplayStatics::GetGlobalTimeDilation(World);
	const bool bHeldByUs = HeldWorld.Get() == World && FMath::IsNearlyEqual(Speed, HeldSpeed, 0.001f);
	if (!bHeldByUs && !FMath::IsNearlyEqual(Speed, 1.0f))
	{
		return false;
	}
	const double Now = FPlatformTime::Seconds();
	if (!bHeldByUs && Now - ReleasedAt < RestBetweenHolds)
	{
		return false;
	}
	// Hits close together share one hold (each can lengthen it a little, never past LongestHold from the first).
	const double Start = bHeldByUs ? HeldUntil - Seconds : Now;
	HeldUntil = FMath::Min(FMath::Max(HeldUntil, Now + Seconds), FMath::Max(Start, Now) + LongestHold);
	if (!bHeldByUs)
	{
		HeldWorld = World;
		HeldUntil = Now + Seconds;
		UGameplayStatics::SetGlobalTimeDilation(World, HeldSpeed);
	}
	if (!Release.IsValid())
	{
		Release = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&ReleaseWhenDue));
	}
	return true;
}
