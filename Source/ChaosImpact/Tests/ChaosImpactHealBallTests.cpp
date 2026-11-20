#if WITH_DEV_AUTOMATION_TESTS
// 回復ボール (2026-11-19, solo only): the pads put it out in solo mode only (never in VS, training or the ball switch);
// thrown, it opens its spot where it lands and hurts nobody; stepping on, hurt, heals 1 at once; staying on, the arc fills
// and heals 1 more, and the spot is gone. Someone at full health does not use it up. Frames in Saved/HealQA.
// Run on solo stage 1: /Game/ChaosImpact/solo/Lvl_SoloStage1?CISolo=1?CIStage=1
#include "Misc/AutomationTest.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactBallSpawner.h"
#include "ChaosImpactBallSwitch.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactGadget.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HighResScreenshot.h"
#include "Misc/Paths.h"

namespace
{
	class FHealBallCommand : public IAutomationLatentCommand
	{
	public:
		explicit FHealBallCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		bool Update() override
		{
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld()) { World = Context.World(); }
			}
			const double Now = FPlatformTime::Seconds();
			if (Now - Started > 120.0)
			{
				Test->AddError(FString::Printf(TEXT("Heal ball test stalled at step %d"), Step));
				return true;
			}
			if (!World || Now < WaitUntil)
			{
				return false;
			}
			APlayerController* PC = World->GetFirstPlayerController();
			AChaosImpactCharacter* Own = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			if (!Own)
			{
				return false;
			}
			const auto Shot = [](const TCHAR* Name)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("HealQA"), Name), true, false);
			};
			const auto Spot = [World]() -> AChaosImpactGadget*
			{
				for (TActorIterator<AChaosImpactGadget> It(World); It; ++It)
				{
					if (It->IsHeal() && !It->HasEnded()) { return *It; }
				}
				return nullptr;
			};
			const auto Throw = [World, Own]()
			{
				const FVector Dir = Own->GetActorForwardVector().GetSafeNormal2D();
				const FTransform At(FRotator::ZeroRotator, Own->GetActorLocation() + Dir * 70.0f);
				AChaosImpactBall* Ball = World->SpawnActorDeferred<AChaosImpactBall>(AChaosImpactBall::StaticClass(), At, Own, Own,
					ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				Ball->SetBallType(EChaosImpactBallType::Heal);
				Ball->FinishSpawning(At);
				Ball->Launch(Dir, 520.0f, EChaosImpactBallFlightMode::Arc, 250.0f);
			};
			const auto StandOn = [Own](const AChaosImpactGadget* Gadget)
			{
				const float Half = Own->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
				Own->SetActorLocation(Gadget->GetActorLocation() + FVector(0.0f, 0.0f, Half + 2.0f), false, nullptr, ETeleportType::TeleportPhysics);
				Own->GetCharacterMovement()->Velocity = FVector::ZeroVector;
			};
			switch (Step)
			{
			case 0:
				WaitUntil = Now + 3.0;
				Step = 1;
				return false;
			case 1:
			{
				// Solo only: the pads here put it out; VS, training and the ball switch never know it.
				AChaosImpactBallSpawner* Pad = nullptr;
				for (TActorIterator<AChaosImpactBallSpawner> It(World); It; ++It) { Pad = *It; }
				Test->TestTrue(TEXT("A solo pad can put out a heal ball"), Pad && Pad->GetChance(EChaosImpactBallType::Heal) > 0.0f);
				Test->TestFalse(TEXT("The ball switch does not list it"), ChaosImpactBallSwitch::GetSwitchable().Contains(EChaosImpactBallType::Heal));
				Test->TestTrue(TEXT("Solo only"), ChaosImpactBallTypes::IsSoloOnly(EChaosImpactBallType::Heal));
				// Down to 1, the ball lobbed ahead.
				Own->LoseHealthToStage(Own->GetMaxHealth() - 1.0f);
				Test->TestEqual(TEXT("Hurt to 1"), Own->GetHealth(), 1.0f);
				Throw();
				WaitUntil = Now + 1.6;
				Step = 2;
				return false;
			}
			case 2:
			{
				AChaosImpactGadget* Gadget = Spot();
				Test->TestNotNull(TEXT("Its spot opened where it landed"), Gadget);
				Test->TestEqual(TEXT("Nobody on it yet: no heal"), Own->GetHealth(), 1.0f);
				if (!Gadget)
				{
					return true;
				}
				SpotPtr = Gadget;
				Shot(TEXT("Heal_Spot.png"));
				WaitUntil = Now + 0.6;
				Step = 3;
				return false;
			}
			case 3:
				if (AChaosImpactGadget* Gadget = SpotPtr.Get())
				{
					StandOn(Gadget);
				}
				WaitUntil = Now + 0.5;
				Step = 4;
				return false;
			case 4:
			{
				const AChaosImpactGadget* Gadget = SpotPtr.Get();
				Test->TestEqual(TEXT("Stepping on: 1 back at once"), Own->GetHealth(), 2.0f);
				Test->TestTrue(TEXT("…its first heal given, the spot still there"), Gadget && Gadget->GetState().Heals == 1 && !Gadget->HasEnded());
				Shot(TEXT("Heal_First.png"));
				WaitUntil = Now + 1.1;
				Step = 5;
				return false;
			}
			case 5:
			{
				const AChaosImpactGadget* Gadget = SpotPtr.Get();
				const int32 Stay = Gadget ? Gadget->GetState().StayPercent : -1;
				UE_LOG(LogTemp, Display, TEXT("HEALQA stay %d%%"), Stay);
				Test->TestTrue(TEXT("Staying on: the arc fills"), Stay > 15 && Stay < 90);
				Test->TestEqual(TEXT("…not healed again yet"), Own->GetHealth(), 2.0f);
				Shot(TEXT("Heal_Stay.png"));
				WaitUntil = Now + 1.8;
				Step = 6;
				return false;
			}
			case 6:
			{
				const AChaosImpactGadget* Gadget = SpotPtr.Get();
				Test->TestEqual(TEXT("Stayed on: 1 more"), Own->GetHealth(), 3.0f);
				Test->TestTrue(TEXT("…and the spot is used up"), !Gadget || Gadget->HasEnded());
				Shot(TEXT("Heal_Second.png"));
				// Another, stood on at full health: nothing used.
				Throw();
				WaitUntil = Now + 1.6;
				Step = 7;
				return false;
			}
			case 7:
			{
				AChaosImpactGadget* Gadget = Spot();
				Test->TestNotNull(TEXT("A second spot"), Gadget);
				if (!Gadget)
				{
					return true;
				}
				SpotPtr = Gadget;
				StandOn(Gadget);
				WaitUntil = Now + 1.0;
				Step = 8;
				return false;
			}
			case 8:
			{
				AChaosImpactGadget* Gadget = SpotPtr.Get();
				Test->TestTrue(TEXT("At full health it is not used"), Gadget && Gadget->GetState().Heals == 0 && !Gadget->HasEnded());
				// Hurt while standing on it: healed then.
				Own->LoseHealthToStage(1.0f);
				WaitUntil = Now + 0.5;
				Step = 9;
				return false;
			}
			case 9:
				Test->TestEqual(TEXT("Hurt on it: healed at once"), Own->GetHealth(), Own->GetMaxHealth());
				Shot(TEXT("Heal_Again.png"));
				WaitUntil = Now + 0.6;
				Step = 10;
				return false;
			default:
				return true;
			}
		}

	private:
		FAutomationTestBase* Test = nullptr;
		int32 Step = 0;
		double WaitUntil = 0.0;
		double Started = FPlatformTime::Seconds();
		TWeakObjectPtr<AChaosImpactGadget> SpotPtr;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactHealBallTest, "ChaosImpact.Solo.HealBall",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FChaosImpactHealBallTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FHealBallCommand(this));
	return true;
}
#endif
