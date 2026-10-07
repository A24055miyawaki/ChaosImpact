#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactBall.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactSoloRoom.h"
#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
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
	 * A solo room, in the level made for it (/Game/ChaosImpact/solo/Lvl_SoloRoomTest?CISolo=1?CITargets=0?CICPUCount=0): crossing the
	 * entry shuts the door and brings five minions; five down bring the boss; the boss down opens the door. Then once more,
	 * knocked out in the middle of it: the room's enemies are gone, the door opens, and the player comes back at the
	 * entrance. The enemies are held still (they would otherwise knock the player out on their own). Screenshots go to
	 * Saved/SoloRoomQA.
	 */
	class FSoloRoomCommand : public IAutomationLatentCommand
	{
	public:
		explicit FSoloRoomCommand(FAutomationTestBase* InTest) : Test(InTest) {}

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
			AChaosImpactPlayerController* PC = World ? Cast<AChaosImpactPlayerController>(World->GetFirstPlayerController()) : nullptr;
			AChaosImpactCharacter* Player = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			if (Now - StartedAt > 150.0)
			{
				Test->AddError(FString::Printf(TEXT("Stuck at step %d."), Step));
				return true;
			}
			if (!World || !Player || Now < NextAt)
			{
				return false;
			}
			AChaosImpactSoloRoom* Room = RoomPtr.Get();
			const auto Shot = [](const TCHAR* Name)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SoloRoomQA"), Name), true, false);
			};
			const auto HoldEnemiesStill = [World, Player]()
			{
				for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
				{
					if (*It != Player)
					{
						It->SetTrainingMenuFrozen(true);
					}
				}
			};
			const auto KnockOutEnemies = [World, Player]()
			{
				for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
				{
					if (*It != Player && !It->IsEliminated())
					{
						It->TakeDamage(100.0f, FDamageEvent(), nullptr, nullptr);
					}
				}
			};
			switch (Step)
			{
			case 0:
			{
				if (!PC->IsGameplayActive())
				{
					return false;
				}
				UE_LOG(LogTemp, Display, TEXT("SOLOROOM player starts at %s"), *Player->GetActorLocation().ToCompactString());
				// The room placed in the solo test level (BP_SoloRoom, with its stand-in floor and walls).
				for (TActorIterator<AChaosImpactSoloRoom> It(World); It; ++It)
				{
					Room = *It;
				}
				Test->TestNotNull(TEXT("The level has its room"), Room);
				if (!Room)
				{
					return true;
				}
				Test->TestTrue(TEXT("BP_SoloRoom has its stand-in walls on"), Room->bPlaceholderWalls);
				Shot(TEXT("0_Level.png"));
				Room->MinionLevel = EChaosImpactCPULevel::Weak;
				RoomPtr = Room;
				Test->TestEqual(TEXT("Waiting, doors open"), Room->GetPhase(), EChaosImpactSoloRoomPhase::Waiting);
				Test->TestFalse(TEXT("The door is open"), Room->AreDoorsClosed());
				Step = 1;
				NextAt = Now + 0.5;
				return false;
			}
			case 1:
				// Into the room, across the entry.
				Player->SetActorLocation(Room->EntryTrigger->GetComponentLocation() + FVector(0.0f, 0.0f, 10.0f), false, nullptr,
					ETeleportType::TeleportPhysics);
				Step = 2;
				NextAt = Now + 0.3;
				return false;
			case 2:
				Test->TestEqual(TEXT("Crossing the entry starts the room"), Room->GetPhase(), EChaosImpactSoloRoomPhase::Minions);
				Test->TestTrue(TEXT("The door shuts"), Room->AreDoorsClosed());
				Test->TestEqual(TEXT("Five minions"), Room->GetAliveEnemyCount(), 5);
				HoldEnemiesStill();
				Step = 3;
				NextAt = Now + 1.0;
				return false;
			case 3:
				Shot(TEXT("1_Minions.png"));
				Step = 4;
				NextAt = Now + 0.5;
				return false;
			case 4:
				KnockOutEnemies();
				Step = 5;
				return false;
			case 5:
				if (Room->GetPhase() != EChaosImpactSoloRoomPhase::Boss)
				{
					return false;
				}
				Test->TestEqual(TEXT("Five down"), Room->GetDefeatedCount(), 5);
				Test->TestTrue(TEXT("Still shut for the boss"), Room->AreDoorsClosed());
				Test->TestEqual(TEXT("The boss"), Room->GetAliveEnemyCount(), 1);
				HoldEnemiesStill();
				Step = 6;
				NextAt = Now + 1.0;
				return false;
			case 6:
				Shot(TEXT("2_Boss.png"));
				Step = 7;
				NextAt = Now + 0.5;
				return false;
			case 7:
				KnockOutEnemies();
				Step = 8;
				return false;
			case 8:
				if (Room->GetPhase() != EChaosImpactSoloRoomPhase::Cleared)
				{
					return false;
				}
				Test->TestFalse(TEXT("Cleared: the door opens"), Room->AreDoorsClosed());
				Step = 9;
				NextAt = Now + 1.0;
				return false;
			case 9:
				Shot(TEXT("3_Cleared.png"));
				// Once more, to be knocked out halfway through.
				Room->ResetRoom();
				// Out through the doorway first (still standing on the entry, stepping onto it again would not count).
				Player->SetActorLocation(Room->RespawnPoint->GetComponentLocation() + FVector(0.0f, 0.0f, 100.0f), false, nullptr,
					ETeleportType::TeleportPhysics);
				Step = 15;
				NextAt = Now + 0.3;
				return false;
			case 15:
				Player->SetActorLocation(Room->EntryTrigger->GetComponentLocation() + FVector(0.0f, 0.0f, 10.0f), false, nullptr,
					ETeleportType::TeleportPhysics);
				Step = 10;
				NextAt = Now + 0.5;
				return false;
			case 10:
				Test->TestEqual(TEXT("Again: five minions"), Room->GetAliveEnemyCount(), 5);
				HoldEnemiesStill();
				Player->TakeDamage(100.0f, FDamageEvent(), nullptr, nullptr);
				Step = 11;
				NextAt = Now + 0.3;
				return false;
			case 11:
				Test->TestEqual(TEXT("Knocked out: back to waiting"), Room->GetPhase(), EChaosImpactSoloRoomPhase::Waiting);
				Test->TestEqual(TEXT("…its enemies gone"), Room->GetAliveEnemyCount(), 0);
				Test->TestFalse(TEXT("…the door open"), Room->AreDoorsClosed());
				Step = 12;
				return false;
			case 12:
				if (Player->IsEliminated())
				{
					return false;
				}
				{
					const float FromEntrance = static_cast<float>(FVector::Dist2D(Player->GetActorLocation(), Room->RespawnPoint->GetComponentLocation()));
					UE_LOG(LogTemp, Display, TEXT("SOLOROOM back %.0f cm from the entrance"), FromEntrance);
					Test->TestTrue(TEXT("Back at the room's entrance"), FromEntrance < 150.0f);
				}
				Step = 13;
				NextAt = Now + 1.0;
				return false;
			case 13:
				Shot(TEXT("4_BackAtEntrance.png"));
				// In again: from the start.
				Player->SetActorLocation(Room->EntryTrigger->GetComponentLocation() + FVector(0.0f, 0.0f, 10.0f), false, nullptr,
					ETeleportType::TeleportPhysics);
				Step = 14;
				NextAt = Now + 0.5;
				return false;
			case 14:
			default:
				Test->TestEqual(TEXT("In again: from the start"), Room->GetPhase(), EChaosImpactSoloRoomPhase::Minions);
				Test->TestEqual(TEXT("…five minions again"), Room->GetAliveEnemyCount(), 5);
				Test->TestEqual(TEXT("…none counted yet"), Room->GetDefeatedCount(), 0);
				return true;
			}
		}

	private:
		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Step = 0;
		TWeakObjectPtr<AChaosImpactSoloRoom> RoomPtr;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactSoloRoomTest, "ChaosImpact.Solo.Room",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactSoloRoomTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FSoloRoomCommand(this));
	return true;
}

#endif
