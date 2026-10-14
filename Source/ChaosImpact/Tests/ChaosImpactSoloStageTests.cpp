#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactCharacter.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactSoloRoom.h"
#include "Camera/CameraActor.h"
#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"

namespace
{
	/**
	 * A solo stage, run on its own level (e.g. /Game/ChaosImpact/solo/Lvl_SoloStage1?CISolo=1?CIStage=1?CITargets=0?CICPUCount=0):
	 * pictures along the way (the game's own camera at the start, halfway, at the door; two wide views), then its room
	 * played through: in past the door, five minions, the boss, cleared. Enemies are held still and knocked out by the
	 * test. Pictures go to Saved/SoloStageQA/<level>.
	 */
	class FSoloStageCommand : public IAutomationLatentCommand
	{
	public:
		explicit FSoloStageCommand(FAutomationTestBase* InTest) : Test(InTest) {}

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
			if (Now - StartedAt > 180.0)
			{
				Test->AddError(FString::Printf(TEXT("Stuck at step %d."), Step));
				return true;
			}
			if (!World || !Player || Now < NextAt)
			{
				return false;
			}
			AChaosImpactSoloRoom* Room = RoomPtr.Get();
			const FString Folder = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SoloStageQA"), World->GetMapName().Replace(TEXT("UEDPIE_0_"), TEXT("")));
			const auto Shot = [&Folder](const TCHAR* Name)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(Folder, Name), true, false);
			};
			const auto Place = [Player](const FVector& Where)
			{
				Player->SetActorLocation(Where + FVector(0.0f, 0.0f, 20.0f), false, nullptr, ETeleportType::TeleportPhysics);
				Player->SetActorRotation(FRotator::ZeroRotator);
			};
			const auto LookAt = [World, PC, this](const FVector& Target, const FVector& From)
			{
				if (!Camera.IsValid())
				{
					Camera = World->SpawnActor<ACameraActor>(From, FRotator::ZeroRotator);
				}
				if (ACameraActor* View = Camera.Get())
				{
					View->SetActorLocation(From);
					View->SetActorRotation((Target - From).Rotation());
					PC->SetViewTarget(View);
				}
			};
			const auto Enemies = [World, Player](const bool bKnockOut)
			{
				for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
				{
					if (*It != Player && !It->IsEliminated())
					{
						It->SetTrainingMenuFrozen(true);
						if (bKnockOut)
						{
							It->TakeDamage(100.0f, FDamageEvent(), nullptr, nullptr);
						}
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
				for (TActorIterator<AChaosImpactSoloRoom> It(World); It; ++It)
				{
					Room = *It;
				}
				Test->TestNotNull(TEXT("The stage has its room"), Room);
				if (!Room)
				{
					return true;
				}
				RoomPtr = Room;
				Start = Player->GetActorLocation();
				Door = Room->RespawnPoint->GetComponentLocation();
				UE_LOG(LogTemp, Display, TEXT("SOLOSTAGE %s: start %s, door %s, room %s"), *World->GetMapName(), *Start.ToCompactString(),
					*Door.ToCompactString(), *Room->RoomArea->GetComponentLocation().ToCompactString());
				// Every material ready before any picture.
				if (GShaderCompilingManager)
				{
					GShaderCompilingManager->FinishAllCompilation();
				}
				Step = 1;
				NextAt = Now + 2.0;
				return false;
			}
			case 1:
				Shot(TEXT("1_Start.png"));
				Step = 2;
				NextAt = Now + 0.3;
				return false;
			case 2:
				Place(FMath::Lerp(Start, Door, 0.5f));
				Step = 3;
				NextAt = Now + 1.2;
				return false;
			case 3:
				Shot(TEXT("2_Halfway.png"));
				Step = 4;
				NextAt = Now + 0.3;
				return false;
			case 4:
				Place(Door);
				Step = 5;
				NextAt = Now + 1.2;
				return false;
			case 5:
				Shot(TEXT("3_Door.png"));
				Step = 6;
				NextAt = Now + 0.3;
				return false;
			case 6:
			{
				// Wide: the whole way from behind and above the start, then the room from over its doorway.
				const FVector Middle = (Start + Room->RoomArea->GetComponentLocation()) * 0.5f;
				const FVector Back = (Start - Room->RoomArea->GetComponentLocation()).GetSafeNormal2D();
				LookAt(Middle, Start + Back * 1600.0f + FVector(0.0f, 0.0f, 2600.0f));
				Step = 7;
				NextAt = Now + 1.2;
				return false;
			}
			case 7:
				Shot(TEXT("4_Wide.png"));
				Step = 8;
				NextAt = Now + 0.3;
				return false;
			case 8:
			{
				const FVector Centre = Room->RoomArea->GetComponentLocation();
				const FVector Back = (Door - Centre).GetSafeNormal2D();
				const FVector Side = FVector::CrossProduct(FVector::UpVector, Back);
				LookAt(Centre, Door + Back * 300.0f + Side * 900.0f + FVector(0.0f, 0.0f, 1300.0f));
				Step = 9;
				NextAt = Now + 1.2;
				return false;
			}
			case 9:
				Shot(TEXT("5_Room.png"));
				Step = 10;
				NextAt = Now + 0.3;
				return false;
			case 10:
				PC->SetViewTarget(Player);
				// In past the door.
				Place(Room->EntryTrigger->GetComponentLocation());
				Step = 11;
				NextAt = Now + 0.4;
				return false;
			case 11:
				Test->TestEqual(TEXT("Going in starts the room"), Room->GetPhase(), EChaosImpactSoloRoomPhase::Minions);
				Test->TestTrue(TEXT("The door shuts"), Room->AreDoorsClosed());
				Test->TestEqual(TEXT("Its minions"), Room->GetAliveEnemyCount(), Room->MinionCount);
				Enemies(false);
				// A few steps in, to see them.
				Place(FMath::Lerp(Room->EntryTrigger->GetComponentLocation(), Room->RoomArea->GetComponentLocation(), 0.55f));
				Step = 12;
				NextAt = Now + 1.4;
				return false;
			case 12:
				Shot(TEXT("6_Minions.png"));
				Step = 13;
				NextAt = Now + 0.3;
				return false;
			case 13:
				Enemies(true);
				Step = 14;
				return false;
			case 14:
				if (Room->GetPhase() == EChaosImpactSoloRoomPhase::Minions)
				{
					return false;
				}
				if (Room->bSpawnBoss)
				{
					Test->TestEqual(TEXT("Then the boss"), Room->GetPhase(), EChaosImpactSoloRoomPhase::Boss);
					Enemies(false);
					Step = 15;
					NextAt = Now + 1.4;
					return false;
				}
				Step = 17;
				return false;
			case 15:
				Shot(TEXT("7_Boss.png"));
				Step = 16;
				NextAt = Now + 0.3;
				return false;
			case 16:
				Enemies(true);
				Step = 17;
				return false;
			case 17:
				if (Room->GetPhase() != EChaosImpactSoloRoomPhase::Cleared)
				{
					return false;
				}
				Test->TestFalse(TEXT("Cleared: the door opens"), Room->AreDoorsClosed());
				Step = 18;
				NextAt = Now + 1.0;
				return false;
			case 18:
				Shot(TEXT("8_Cleared.png"));
				Step = 19;
				NextAt = Now + 0.5;
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
		FVector Start = FVector::ZeroVector;
		FVector Door = FVector::ZeroVector;
		TWeakObjectPtr<AChaosImpactSoloRoom> RoomPtr;
		TWeakObjectPtr<ACameraActor> Camera;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactSoloStageTest, "ChaosImpact.Solo.Stage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactSoloStageTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FSoloStageCommand(this));
	return true;
}

#endif
