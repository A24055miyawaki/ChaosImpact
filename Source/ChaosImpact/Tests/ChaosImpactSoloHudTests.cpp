#if WITH_DEV_AUTOMATION_TESTS
// ソロモードの目標と進行ゲージ (2026-11-19): on a solo stage, the goal across the top (on the way, the room's enemies with how
// many are left, its boss with its health) and each enemy's health over its head. Frames in Saved/SoloQA/Hud_*.png.
// Run on a stage: /Game/ChaosImpact/solo/Lvl_SoloStage1?CISolo=1?CIStage=1?CITargets=0?CICPUCount=0
#include "Misc/AutomationTest.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactChargeWidget.h"
#include "ChaosImpactSoloRoom.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HighResScreenshot.h"
#include "Kismet/GameplayStatics.h"
#include "Components/BoxComponent.h"
#include "Misc/Paths.h"

namespace
{
	class FSoloHudCommand : public IAutomationLatentCommand
	{
	public:
		explicit FSoloHudCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		bool Update() override
		{
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld()) { World = Context.World(); }
			}
			const double Now = FPlatformTime::Seconds();
			if (Now - Started > 120.0) { Test->AddError(FString::Printf(TEXT("Solo HUD test stalled at step %d"), Step)); return true; }
			if (!World || Now < WaitUntil) { return false; }
			APlayerController* PC = World->GetFirstPlayerController();
			AChaosImpactCharacter* Own = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			if (!Own) { return false; }
			const UChaosImpactChargeWidget* Hud = Cast<UChaosImpactChargeWidget>(Own->GetChargeWidgetForTests());
			AChaosImpactSoloRoom* Room = nullptr;
			for (TActorIterator<AChaosImpactSoloRoom> It(World); It; ++It)
			{
				if (!Room || It->GetName() < Room->GetName()) { Room = *It; }
			}
			const auto Shot = [](const TCHAR* Name)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SoloQA"), FString(Name) + TEXT(".png")), true, false);
			};
			UChaosImpactChargeWidget::FSoloObjective Goal;
			switch (Step)
			{
			case 0:
				WaitUntil = Now + 4.0;
				Step = 1;
				return false;
			case 1:
				Test->TestNotNull(TEXT("The stage has rooms"), Room);
				Test->TestTrue(TEXT("A goal on the way"), Hud && Hud->GetSoloObjective(Goal) && Goal.Kind == 0 && Goal.Rooms > 0);
				Shot(TEXT("Hud_Way"));
				if (Room)
				{
					// (In the middle of the room, so its enemies are in view.)
					Own->TeleportTo(Room->RoomArea->GetComponentLocation() + FVector(0.0f, 0.0f, 120.0f), Own->GetActorRotation());
					Room->StartRoom(Own);
				}
				WaitUntil = Now + 2.0;
				Step = 2;
				return false;
			case 2:
			{
				Test->TestTrue(TEXT("The room's enemies to beat, how many left"), Hud && Hud->GetSoloObjective(Goal) && Goal.Kind == 1
					&& Goal.Left == Room->GetPlannedMinionCount());
				// One of them hit: its health shown over it.
				for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
				{
					if (*It != Own && !It->IsEliminated())
					{
						UGameplayStatics::ApplyDamage(*It, 1.0f, PC, Own, nullptr);
						break;
					}
				}
				WaitUntil = Now + 0.6;
				Step = 3;
				return false;
			}
			case 3:
				Shot(TEXT("Hud_Minions"));
				WaitUntil = Now + 0.4;
				Step = 4;
				return false;
			case 4:
			{
				// Every minion down, one after another, until the boss comes.
				if (Room->GetPhase() == EChaosImpactSoloRoomPhase::Minions)
				{
					for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
					{
						if (*It != Own && !It->IsEliminated() && !It->Tags.Contains(TEXT("CI.SoloBoss")))
						{
							UGameplayStatics::ApplyDamage(*It, 100.0f, PC, Own, nullptr);
						}
					}
					WaitUntil = Now + 1.0;
					if (Now - StepAt > 40.0) { Test->AddError(TEXT("The boss never came")); return true; }
					return false;
				}
				WaitUntil = Now + 2.0;
				Step = 5;
				return false;
			}
			case 5:
				Test->TestTrue(TEXT("The boss to beat, with its health"), Hud && Hud->GetSoloObjective(Goal) && Goal.Kind == 2 && Goal.Boss.IsValid());
				if (AChaosImpactCharacter* Boss = Room->GetBoss())
				{
					UGameplayStatics::ApplyDamage(Boss, 1.0f, PC, Own, nullptr);
				}
				WaitUntil = Now + 0.5;
				Step = 6;
				return false;
			case 6:
				Shot(TEXT("Hud_Boss"));
				WaitUntil = Now + 0.5;
				Step = 7;
				return false;
			default:
				return true;
			}
		}

	private:
		FAutomationTestBase* Test = nullptr;
		int32 Step = 0;
		double WaitUntil = 0.0;
		double StepAt = FPlatformTime::Seconds();
		double Started = FPlatformTime::Seconds();
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactSoloHudTest, "ChaosImpact.Solo.Hud",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FChaosImpactSoloHudTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FSoloHudCommand(this));
	return true;
}
#endif
