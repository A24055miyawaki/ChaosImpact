#include "ChaosImpactNetStats.h"

#include "ChaosImpact.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/App.h"

namespace
{
	void LogConnection(const TCHAR* Who, const UNetConnection* Connection)
	{
		UE_LOG(LogChaosImpact, Log,
			TEXT("NETSTATS %s rtt %.0f ms | in %d B/s %d pkt/s loss %.1f%% | out %d B/s %d pkt/s loss %.1f%% | speed %d B/s queued %d bits"),
			Who, Connection->AvgLag * 1000.0f, Connection->InBytesPerSecond, Connection->InPacketsPerSecond,
			Connection->GetInLossPercentage().GetAvgLossPercentage() * 100.0f, Connection->OutBytesPerSecond,
			Connection->OutPacketsPerSecond, Connection->GetOutLossPercentage().GetAvgLossPercentage() * 100.0f,
			Connection->CurrentNetSpeed, Connection->QueuedBits);
	}
}

namespace
{
	struct FGuardedLine
	{
		TArray<TPair<double, float>> Recent;  // (when, round trip) over the last while
		double LastChangeAt = 0.0;
		/** Checks in a row the round trip has stood high (one long frame, a level loading, is not a clogged line). */
		int32 HighChecks = 0;
	};
	/** Above the best recent round trip by this much: the line is queueing. Within this: clear again. */
	constexpr float QueueingSeconds = 0.15f;
	constexpr float ClearSeconds = 0.06f;
	constexpr double BestWindowSeconds = 30.0;
	constexpr int32 FloorRate = 12000;
	constexpr int32 HighChecksToAct = 3;
	constexpr int32 CeilingRate = 100000;
	/** While any line of a game is held back: packets this many times a second instead (half as many to queue up). */
	constexpr int32 HeldBackTickRate = 30;

	void Guard(UNetConnection* Connection, FGuardedLine& Line, const double Now)
	{
		if (!Connection || Connection->AvgLag <= 0.0f)
		{
			return;
		}
		Line.Recent.Add({Now, Connection->AvgLag});
		Line.Recent.RemoveAll([Now](const TPair<double, float>& Entry) { return Now - Entry.Key > BestWindowSeconds; });
		float Best = TNumericLimits<float>::Max();
		for (const auto& Entry : Line.Recent) { Best = FMath::Min(Best, Entry.Value); }
		if (Now - Line.LastChangeAt < 1.0)
		{
			return;
		}
		const int32 Rate = Connection->CurrentNetSpeed;
		int32 Wanted = Rate;
		Line.HighChecks = Connection->AvgLag > Best + QueueingSeconds ? Line.HighChecks + 1 : 0;
		if (Line.HighChecks >= HighChecksToAct)
		{
			// Below what is going out now, so the queue drains.
			Wanted = FMath::Max(FloorRate, FMath::RoundToInt(FMath::Min<float>(Rate, FMath::Max(Connection->OutBytesPerSecond, FloorRate)) * 0.8f));
		}
		else if (Connection->AvgLag < Best + ClearSeconds)
		{
			Wanted = FMath::Min(CeilingRate, FMath::RoundToInt(Rate * 1.3f) + 2000);
		}
		if (Wanted != Rate)
		{
			Connection->CurrentNetSpeed = Wanted;
			Line.LastChangeAt = Now;
			UE_LOG(LogChaosImpact, Log, TEXT("Line guard: rtt %.0f ms (best %.0f) -> rate %d B/s"), Connection->AvgLag * 1000.0f, Best * 1000.0f, Wanted);
		}
	}
}

void ChaosImpactNetStats::StartLatencyGuard()
{
	static bool bStarted = false;
	if (bStarted || FParse::Param(FCommandLine::Get(), TEXT("CINoLineGuard")))
	{
		return;
	}
	bStarted = true;
	const TSharedRef<TMap<TWeakObjectPtr<UNetConnection>, FGuardedLine>> Lines = MakeShared<TMap<TWeakObjectPtr<UNetConnection>, FGuardedLine>>();
	const TSharedRef<double> NextAt = MakeShared<double>(0.0);
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Lines, NextAt](const float)
	{
		const double Now = FPlatformTime::Seconds();
		if (Now < *NextAt)
		{
			return true;
		}
		*NextAt = Now + 0.5;
		// This machine stalling (a level loading) makes every round trip look long: not the line's doing.
		if (FApp::GetDeltaTime() > 0.25)
		{
			return true;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			const UWorld* World = Context.World();
			UNetDriver* Driver = World && World->IsGameWorld() ? World->GetNetDriver() : nullptr;
			if (!Driver)
			{
				continue;
			}
			bool bHeldBack = false;
			if (Driver->ServerConnection)
			{
				Guard(Driver->ServerConnection, Lines->FindOrAdd(Driver->ServerConnection), Now);
				bHeldBack = Driver->ServerConnection->CurrentNetSpeed < CeilingRate;
			}
			for (UNetConnection* Connection : Driver->ClientConnections)
			{
				Guard(Connection, Lines->FindOrAdd(Connection), Now);
				bHeldBack |= Connection && Connection->CurrentNetSpeed < CeilingRate;
			}
			// Fewer, fuller packets while a line queues (a phone's line suffers from their number as much as their size);
			// back to the usual rate once every line is clear again.
			const int32 Usual = Driver->GetClass()->GetDefaultObject<UNetDriver>()->MaxNetTickRate;
			const int32 TickRate = bHeldBack && (Usual <= 0 || Usual > HeldBackTickRate) ? HeldBackTickRate : Usual;
			if (Driver->MaxNetTickRate != TickRate)
			{
				Driver->MaxNetTickRate = TickRate;
				UE_LOG(LogChaosImpact, Log, TEXT("Line guard: %d packets a second at most"), TickRate);
			}
		}
		for (auto It = Lines->CreateIterator(); It; ++It)
		{
			if (!It.Key().IsValid()) { It.RemoveCurrent(); }
		}
		return true;
	}));
}

void ChaosImpactNetStats::StartIfAsked()
{
	static bool bStarted = false;
	if (bStarted || !FParse::Param(FCommandLine::Get(), TEXT("CINetStats")))
	{
		return;
	}
	bStarted = true;
	struct FWindow
	{
		double StartedAt = 0.0;
		int32 Frames = 0;
		float LongestFrame = 0.0f;
	};
	const TSharedRef<FWindow> Window = MakeShared<FWindow>();
	Window->StartedAt = FPlatformTime::Seconds();
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Window](const float DeltaTime)
	{
		++Window->Frames;
		Window->LongestFrame = FMath::Max(Window->LongestFrame, DeltaTime);
		const double Now = FPlatformTime::Seconds();
		const double Span = Now - Window->StartedAt;
		if (Span < 2.0)
		{
			return true;
		}
		const UWorld* World = nullptr;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World() && Context.World()->IsGameWorld())
			{
				World = Context.World();
				break;
			}
		}
		const UNetDriver* Driver = World ? World->GetNetDriver() : nullptr;
		UE_LOG(LogChaosImpact, Log, TEXT("NETSTATS %s frames avg %.1f ms longest %.1f ms"),
			!Driver ? TEXT("offline") : Driver->ServerConnection ? TEXT("guest") : TEXT("host"),
			Span * 1000.0 / FMath::Max(Window->Frames, 1), Window->LongestFrame * 1000.0f);
		if (Driver && Driver->ServerConnection)
		{
			LogConnection(TEXT("server"), Driver->ServerConnection);
		}
		if (Driver)
		{
			for (const UNetConnection* Connection : Driver->ClientConnections)
			{
				const APlayerController* Owner = Connection ? Connection->PlayerController.Get() : nullptr;
				const FString Name = Owner && Owner->PlayerState ? Owner->PlayerState->GetPlayerName() : FString(TEXT("guest"));
				if (Connection)
				{
					LogConnection(*Name, Connection);
				}
			}
		}
		Window->StartedAt = Now;
		Window->Frames = 0;
		Window->LongestFrame = 0.0f;
		return true;
	}));
}
