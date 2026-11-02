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
