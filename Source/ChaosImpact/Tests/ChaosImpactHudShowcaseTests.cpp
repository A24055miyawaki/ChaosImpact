#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactBall.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactNovaParts.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactPlayerController.h"
#include "Engine/DamageEvents.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	/**
	 * Pictures of the battle HUD at its big moments, for judging its look: run on a VS match with one CPU
	 * (/Game/ThirdPerson/Lvl_ThirdPerson?CITraining=1?CITargets=0?CIVersus=1?CIMatch=1?CIMatchCPU=1?CIStage=0; a match is three minutes).
	 * Ready?, GO!, down to the last HP (a point breaking), the last seconds, FINISH. The CPU is held still. Pictures go
	 * to Saved/HudShowcase.
	 */
	class FHudShowcaseCommand : public IAutomationLatentCommand
	{
	public:
		explicit FHudShowcaseCommand(FAutomationTestBase* InTest) : Test(InTest) {}

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
			const AChaosImpactGameState* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
			APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
			AChaosImpactCharacter* Player = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			if (Now - StartedAt > 320.0)
			{
				Test->AddError(FString::Printf(TEXT("Stuck at step %d."), Step));
				return true;
			}
			if (!Match || !Match->bVersusMatch || Now < NextAt)
			{
				return false;
			}
			const auto Shot = [](const TCHAR* Name)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("HudShowcase"), Name), true, false);
			};
			for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
			{
				if (*It != Player)
				{
					It->SetTrainingMenuFrozen(true);
				}
			}
			const float Elapsed = Match->GetPhaseElapsedSeconds();
			const float Remaining = Match->GetPhaseRemainingSeconds();
			switch (Step)
			{
			case 0:
				if (Match->Phase == EChaosImpactOnlinePhase::Intro && Match->ReadyStartedAt > 0.0)
				{
					Step = 1;
					NextAt = Now + 0.25;
				}
				else if (Match->Phase != EChaosImpactOnlinePhase::Intro)
				{
					// Ready? already over before the test began.
					UE_LOG(LogTemp, Display, TEXT("HUDSHOWCASE no Ready? to show (phase %d)"), static_cast<int32>(Match->Phase));
					Step = 2;
				}
				return false;
			case 1:
				Shot(TEXT("1_Ready.png"));
				Step = 2;
				return false;
			case 2:
				if (Match->Phase == EChaosImpactOnlinePhase::Match && Elapsed > 0.12f)
				{
					Shot(TEXT("2_Go.png"));
					Step = 3;
					NextAt = Now + 3.0;
				}
				return false;
			case 3:
				// Two balls in hand (a fire ball to throw next) and two of the nova's parts.
				if (Player)
				{
					for (const EChaosImpactBallType Type : {EChaosImpactBallType::Normal, EChaosImpactBallType::Fire})
					{
						const FTransform Where(FRotator::ZeroRotator, Player->GetActorLocation() + FVector(0.0f, 0.0f, 400.0f));
						if (AChaosImpactBall* Pickup = World->SpawnActorDeferred<AChaosImpactBall>(AChaosImpactBall::StaticClass(), Where,
							nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
						{
							Pickup->SetBallType(Type);
							Pickup->FinishSpawning(Where);
							Pickup->MakePickup();
							Player->TryPickupBall(Pickup);
						}
					}
					Player->AddNovaPart(EChaosImpactNovaPart::Gear);
					Player->AddNovaPart(EChaosImpactNovaPart::Core);
				}
				Step = 30;
				NextAt = Now + 1.0;
				return false;
			case 30:
				Shot(TEXT("3_Balls.png"));
				if (Player)
				{
					Player->TakeDamage(1.0f, FDamageEvent(), nullptr, nullptr);
				}
				Step = 4;
				NextAt = Now + 1.0;
				return false;
			case 4:
				if (Player)
				{
					Player->TakeDamage(1.0f, FDamageEvent(), nullptr, nullptr);
				}
				Step = 5;
				NextAt = Now + 0.12;
				return false;
			case 5:
				Shot(TEXT("4_LastHP.png"));
				Step = 55;
				return false;
			case 55:
				if (Match->Phase == EChaosImpactOnlinePhase::Match && Remaining < 59.75f)
				{
					Shot(TEXT("5_MinuteLeft.png"));
					Step = 6;
				}
				return false;
			case 6:
				if (Match->Phase == EChaosImpactOnlinePhase::Match && Remaining < 8.85f)
				{
					Shot(TEXT("6_LastSeconds.png"));
					Step = 7;
				}
				return false;
			case 7:
				if (Match->Phase == EChaosImpactOnlinePhase::Results && Elapsed > 0.3f)
				{
					Shot(TEXT("7_Finish.png"));
					Step = 8;
					NextAt = Now + 0.5;
				}
				return false;
			default:
				return true;
			}
		}

	private:
		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Step = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactHudShowcaseTest, "ChaosImpact.Tools.HudShowcase",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactHudShowcaseTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FHudShowcaseCommand(this));
	return true;
}

#endif
