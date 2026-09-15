#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactCharacter.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactGameState.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	/**
	 * Run in a local VS match level with a CPU: brings player 1 down to the last hit and checks that the
	 * danger presentation (the glow over the body) appears, with screenshots of it.
	 */
	class FLastHitCommand : public IAutomationLatentCommand
	{
	public:
		explicit FLastHitCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld())
				{
					World = Context.World();
					break;
				}
			}
			const AChaosImpactGameState* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
			const APlayerController* First = World ? World->GetFirstPlayerController() : nullptr;
			AChaosImpactCharacter* Character = First ? Cast<AChaosImpactCharacter>(First->GetPawn()) : nullptr;
			const bool bPlaying = Match && Match->bVersusMatch && Match->Phase == EChaosImpactOnlinePhase::Match
				&& Match->GetPhaseElapsedSeconds() > 1.5f;
			if (!bPlaying || !Character || Character->IsEliminated())
			{
				DamagedAt = 0.0;
				if (Now - StartedAt < 200.0)
				{
					return false;
				}
				Test->AddError(TEXT("The match never started with a living player."));
				return true;
			}
			if (Character->GetHealth() > 1.0f)
			{
				AController* Instigator = nullptr;
				for (TActorIterator<AChaosImpactCPUController> It(World); It; ++It)
				{
					Instigator = *It;
					break;
				}
				UGameplayStatics::ApplyDamage(Character, 1.0f, Instigator, Instigator ? Instigator->GetPawn() : nullptr,
					UDamageType::StaticClass());
				DamagedAt = Now;
				return false;
			}
			if (DamagedAt <= 0.0)
			{
				DamagedAt = Now;
			}
			if (Now - DamagedAt < 1.0)
			{
				return false;
			}
			if (Shots == 0)
			{
				const bool bSmoke = Character->IsShowingLastHitSmoke();
				UE_LOG(LogTemp, Display, TEXT("LASTHITTEST health %.1f smoke %d"), Character->GetHealth(), bSmoke);
				Test->TestTrue(TEXT("A character on its last hit gives off smoke"), bSmoke);
			}
			if (Shots >= 3)
			{
				return Now >= NextShotAt;
			}
			if (Now >= NextShotAt)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("VersusQA"),
					FString::Printf(TEXT("LastHit-%d.png"), Shots)), true, false);
				++Shots;
				NextShotAt = Now + 0.27;
			}
			return false;
		}

	private:
		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double DamagedAt = 0.0;
		double NextShotAt = 0.0;
		int32 Shots = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactLastHitTest, "ChaosImpact.Versus.LastHitPresentation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::EngineFilter)

bool FChaosImpactLastHitTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FLastHitCommand(this));
	return true;
}

#endif
