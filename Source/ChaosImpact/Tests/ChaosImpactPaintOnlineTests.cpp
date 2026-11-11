#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactGameState.h"
#include "ChaosImpactPaintCache.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	/**
	 * ペイント online (run on a guest joined to a host, both started with -CIDevPaint=<n>): every player's pictures reach
	 * everyone. The guest's own go up to the host (its key on its player state once they have all come), the host's come
	 * down to the guest, and both are kept here by key. A screenshot of the match in Saved/PaintQA.
	 */
	class FPaintOnlineCommand : public IAutomationLatentCommand
	{
	public:
		explicit FPaintOnlineCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld())
				{
					World = Context.World();
				}
			}
			if (Now - StartedAt > 120.0)
			{
				Test->AddError(TEXT("Timed out: not every player's pictures arrived"));
				return true;
			}
			const AGameStateBase* Game = World ? World->GetGameState() : nullptr;
			UChaosImpactPaintCache* Cache = World ? UChaosImpactPaintCache::Get(World) : nullptr;
			if (!Game || !Cache || World->GetNetMode() != NM_Client || Now < NextAt)
			{
				return false;
			}
			int32 Humans = 0;
			int32 Arrived = 0;
			TArray<FString> Keys;
			for (const APlayerState* Member : Game->PlayerArray)
			{
				const AChaosImpactPlayerState* State = Cast<AChaosImpactPlayerState>(Member);
				if (!State || State->IsABot())
				{
					continue;
				}
				++Humans;
				if (State->PaintKey != 0 && Cache->Has(State->PaintKey))
				{
					++Arrived;
				}
				Keys.Add(FString::Printf(TEXT("%s=%d%s"), *State->GetPlayerName(), State->PaintKey, Cache->Has(State->PaintKey) ? TEXT("") : TEXT("(not here)")));
			}
			if (Humans >= 2 && Arrived == Humans)
			{
				if (!bShot)
				{
					UE_LOG(LogTemp, Display, TEXT("PAINTNET every player's pictures here after %.1f s: %s"), Now - StartedAt, *FString::Join(Keys, TEXT(", ")));
					FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PaintQA"), TEXT("online_guest.png")), true, false);
					bShot = true;
					NextAt = Now + 6.0;
					return false;
				}
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PaintQA"), TEXT("online_guest_later.png")), true, false);
				return true;
			}
			if (Now >= NextLogAt)
			{
				NextLogAt = Now + 5.0;
				UE_LOG(LogTemp, Display, TEXT("PAINTNET waiting: %s"), *FString::Join(Keys, TEXT(", ")));
			}
			return false;
		}

	private:
		FAutomationTestBase* Test = nullptr;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		double NextLogAt = 0.0;
		bool bShot = false;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactPaintOnlineTest, "ChaosImpact.Paint.Online",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactPaintOnlineTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FPaintOnlineCommand(this));
	return true;
}

#endif
