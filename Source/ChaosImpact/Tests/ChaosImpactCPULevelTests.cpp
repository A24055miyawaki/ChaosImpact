#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactBall.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactGameState.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"

namespace
{
	/**
	 * CPU strength, measured: a duel between a CPU at the level under test and a つよい CPU. Run on a local VS level
	 * where the player only watches and two CPUs play:
	 * ?CITraining=1?CIVersus=1?CIMatch=1?CIMatchCPU=2?CISpectate=1?CILocalPlayers=1?CIKeyboardPlayer=0?CIDuelLevel=N
	 * For 150 seconds every throw and every hit is counted, so the result is per throw: how often each one's balls
 * land (aim) and how often the other's balls land on it (dodging). よわい must lose the exchange.
	 */
	class FCPULevelDuelCommand : public IAutomationLatentCommand
	{
	public:
		explicit FCPULevelDuelCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				World = Context.World() && Context.World()->IsGameWorld() ? Context.World() : World;
			}
			const AChaosImpactGameState* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
			if (!Match || !Match->bVersusMatch || Match->Phase != EChaosImpactOnlinePhase::Match || Match->IsMatchInputLocked())
			{
				if (StartedAt > 0.0 || Now - CreatedAt > 150.0)
				{
					// The match ended early (it should not in 90 seconds of a 3 minute match) or never began.
					Test->AddError(TEXT("The match was not running."));
					return true;
				}
				return false;
			}
			if (StartedAt <= 0.0)
			{
				TArray<AChaosImpactCPUController*> CPUs;
				for (TActorIterator<AChaosImpactCPUController> It(World); It; ++It)
				{
					CPUs.Add(*It);
				}
				if (CPUs.Num() != 2)
				{
					Test->AddError(FString::Printf(TEXT("Expected 2 CPUs, found %d."), CPUs.Num()));
					return true;
				}
				Level = ChaosImpactMatch::SanitizeCPULevel(FCString::Atoi(World->URL.GetOption(TEXT("CIDuelLevel="), TEXT("0"))));
				CPUs[0]->SetDifficulty(Level);
				CPUs[1]->SetDifficulty(ChaosImpactMatch::CPULevelStrong);
				Tested = Cast<AChaosImpactCharacter>(CPUs[0]->GetPawn());
				Reference = Cast<AChaosImpactCharacter>(CPUs[1]->GetPawn());
				StartedAt = Now;
			}
			if (!Tested.IsValid() || !Reference.IsValid())
			{
				Test->AddError(TEXT("A CPU went missing."));
				return true;
			}
			Count(Tested.Get(), TestedHealth, TestedTaken);
			Count(Reference.Get(), ReferenceHealth, ReferenceTaken);
			for (TActorIterator<AChaosImpactBall> It(World); It; ++It)
			{
				if (It->IsPickup())
				{
					// Lying on the floor again: its next flight is a new throw.
					Seen.Remove(*It);
				}
				else if (!Seen.Contains(*It))
				{
					Seen.Add(*It);
					TestedThrows += It->WasThrownBy(Tested.Get()) ? 1 : 0;
					ReferenceThrows += It->WasThrownBy(Reference.Get()) ? 1 : 0;
				}
			}
			if (Now - StartedAt < 150.0)
			{
				return false;
			}
			// Two in the ring: what one takes, the other dealt.
			const float Aim = TestedThrows > 0 ? static_cast<float>(ReferenceTaken) / TestedThrows : 0.0f;
			const float Dodged = ReferenceThrows > 0 ? 1.0f - static_cast<float>(TestedTaken) / ReferenceThrows : 1.0f;
			const float ReferenceAim = ReferenceThrows > 0 ? static_cast<float>(TestedTaken) / ReferenceThrows : 0.0f;
			UE_LOG(LogTemp, Display, TEXT("CPUDUEL level=%s throws=%d hits=%d (%.0f%%) | つよい throws=%d hits=%d (%.0f%%) | dodged %.0f%%"),
				ChaosImpactMatch::GetCPULevelName(Level), TestedThrows, ReferenceTaken, Aim * 100.0f,
				ReferenceThrows, TestedTaken, ReferenceAim * 100.0f, Dodged * 100.0f);
			Test->TestTrue(TEXT("Both CPUs threw"), TestedThrows > 0 && ReferenceThrows > 0);
			// Few throws make these numbers noisy, so only よわい, far apart from つよい, is held to them.
			if (Level == ChaosImpactMatch::CPULevelWeak)
			{
				Test->TestTrue(TEXT("よわい lands fewer hits than it takes from つよい"), ReferenceTaken < TestedTaken);
			}
			else if (Level == ChaosImpactMatch::CPULevelStrongest)
			{
				Test->TestTrue(TEXT("さいきょう lands more hits than it takes from つよい"), ReferenceTaken > TestedTaken);
			}
			return true;
		}

	private:
		static void Count(const AChaosImpactCharacter* Character, float& LastHealth, int32& Taken)
		{
			if (Character->IsEliminated())
			{
				// It comes back at full health; that is not a hit.
				LastHealth = -1.0f;
				return;
			}
			if (LastHealth >= 0.0f && Character->GetHealth() < LastHealth)
			{
				Taken += FMath::RoundToInt(LastHealth - Character->GetHealth());
			}
			LastHealth = Character->GetHealth();
		}

		FAutomationTestBase* Test;
		double CreatedAt = FPlatformTime::Seconds();
		double StartedAt = 0.0;
		int32 Level = 0;
		TWeakObjectPtr<AChaosImpactCharacter> Tested;
		TWeakObjectPtr<AChaosImpactCharacter> Reference;
		float TestedHealth = -1.0f;
		float ReferenceHealth = -1.0f;
		int32 TestedTaken = 0;
		int32 ReferenceTaken = 0;
		int32 TestedThrows = 0;
		int32 ReferenceThrows = 0;
		TSet<TWeakObjectPtr<AChaosImpactBall>> Seen;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactCPULevelDuelTest, "ChaosImpact.Versus.CPULevelDuel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactCPULevelDuelTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FCPULevelDuelCommand(this));
	return true;
}

#endif
