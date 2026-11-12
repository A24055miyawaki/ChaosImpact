#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactCharacter.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactPaintCache.h"
#include "ChaosImpactSettings.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	/**
	 * ペイント online (run on a guest joined to a host, and on the host too, both started with -CIDevPaint=<n>): every
	 * player's pictures reach everyone, and each player's character in the match wears them on this machine. With
	 * -CIPaintExpect=duel / tournament / versus the match must be of that kind (1v1, a tournament's match, a VS match).
	 * Screenshots Saved/PaintQA/online_<kind>_<host|guest>.png.
	 */
	class FPaintOnlineCommand : public IAutomationLatentCommand
	{
	public:
		explicit FPaintOnlineCommand(FAutomationTestBase* InTest) : Test(InTest)
		{
			FParse::Value(FCommandLine::Get(), TEXT("CIPaintExpect="), Expect);
		}

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
			if (Now - StartedAt > 300.0)
			{
				Test->AddError(FString::Printf(TEXT("Timed out: %s"), *LastState));
				return true;
			}
			const AChaosImpactGameState* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
			UChaosImpactPaintCache* Cache = World ? UChaosImpactPaintCache::Get(World) : nullptr;
			if (!Match || !Cache || World->GetNetMode() == NM_Standalone || Now < NextAt)
			{
				return false;
			}
			const FString Kind = Match->IsTournament() ? TEXT("tournament") : Match->IsDuel() ? TEXT("duel") : Match->bVersusMatch ? TEXT("versus") : TEXT("lobby");
			const FString Role = World->GetNetMode() == NM_Client ? TEXT("guest") : TEXT("host");
			int32 Humans = 0;
			int32 Arrived = 0;
			int32 Worn = 0;
			TArray<FString> Keys;
			for (const APlayerState* Member : Match->PlayerArray)
			{
				const AChaosImpactPlayerState* State = Cast<AChaosImpactPlayerState>(Member);
				if (!State || State->IsABot())
				{
					continue;
				}
				++Humans;
				const bool bHere = State->PaintKey != 0 && Cache->Has(State->PaintKey);
				Arrived += bHere ? 1 : 0;
				// Worn: the character this player plays now in the match shows those pictures on this machine.
				const AChaosImpactCharacter* Body = Cast<AChaosImpactCharacter>(State->GetPawn());
				const bool bWorn = bHere && Body && Body->GetShownPaintKey() == State->PaintKey;
				Worn += bWorn ? 1 : 0;
				Keys.Add(FString::Printf(TEXT("%s=%d%s%s"), *State->GetPlayerName(), State->PaintKey, bHere ? TEXT("") : TEXT("(not here)"),
					!Body ? TEXT("(no body)") : bWorn ? TEXT("(worn)") : TEXT("(not worn)")));
			}
			// (A tournament's matches are played in its own phase, in the world's arenas.)
			const bool bPlaying = Match->Phase == EChaosImpactOnlinePhase::Match || (Match->IsTournament() && Match->Phase == EChaosImpactOnlinePhase::Tournament);
			LastState = FString::Printf(TEXT("%s %s phase %d: %s"), *Role, *Kind, static_cast<int32>(Match->Phase), *FString::Join(Keys, TEXT(", ")));
			if (Humans >= 2 && Worn == Humans && bPlaying && (Expect.IsEmpty() || Expect == Kind))
			{
				if (!bShot)
				{
					UE_LOG(LogTemp, Display, TEXT("PAINTNET everyone wears their pictures after %.1f s: %s"), Now - StartedAt, *LastState);
					Test->TestTrue(TEXT("Others' paint is shown in the settings (the check needs it)"), ChaosImpactSettings::IsOthersPaintShown());
					FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PaintQA"),
						FString::Printf(TEXT("online_%s_%s.png"), *Kind, *Role)), true, false);
					bShot = true;
					NextAt = Now + 3.0;
					return false;
				}
				return true;
			}
			if (!Expect.IsEmpty() && bPlaying && Humans >= 2 && Kind != Expect && Now >= NextLogAt)
			{
				UE_LOG(LogTemp, Warning, TEXT("PAINTNET a %s match, waiting for a %s one"), *Kind, *Expect);
			}
			if (Now >= NextLogAt)
			{
				NextLogAt = Now + 5.0;
				UE_LOG(LogTemp, Display, TEXT("PAINTNET waiting: %s"), *LastState);
			}
			return false;
		}

	private:
		FAutomationTestBase* Test = nullptr;
		FString Expect;
		FString LastState;
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
