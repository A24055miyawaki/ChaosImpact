#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactCharacter.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactSoloStages.h"
#include "ChaosImpactPuppetComponent.h"
#include "ChaosImpactWorldMap.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	/**
	 * Solo mode's world map, run on its level (/Game/ChaosImpact/solo/WorldMap/Lvl_SoloWorldMap1?CIWorldMap=1): from no
	 * progress the fog clears off the first stage; the paths hold the player; clearing stages opens the next ones (the
	 * fog clearing off them); the temple's gate leads to map 2; the confirm button on stage 1 plays it, and clearing it
	 * comes back to the map. Pictures of each region go to Saved/WorldMapQA. Uses the tests' own progress slot.
	 */
	class FWorldMapCommand : public IAutomationLatentCommand
	{
	public:
		explicit FWorldMapCommand(FAutomationTestBase* InTest) : Test(InTest) {}

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
			if (Now - StartedAt > 480.0)
			{
				Test->AddError(FString::Printf(TEXT("Stuck at step %d."), Step));
				return true;
			}
			if (!World)
			{
				return false;
			}
			AChaosImpactPlayerController* PC = Cast<AChaosImpactPlayerController>(World->GetFirstPlayerController());
			AChaosImpactCharacter* Player = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			AChaosImpactWorldMap* Map = AChaosImpactWorldMap::Find(World);
			const auto Shot = [Player](const FString& Name)
			{
				UE_LOG(LogTemp, Display, TEXT("WORLDMAP player at %s"), Player ? *Player->GetActorLocation().ToString() : TEXT("-"));
				if (const UChaosImpactPuppetComponent* Body = Player ? Player->FindComponentByClass<UChaosImpactPuppetComponent>() : nullptr)
				{
					UE_LOG(LogTemp, Display, TEXT("WORLDMAP body at %s visible %d ready %d hidden %d"), *Body->GetComponentLocation().ToString(),
						Body->IsVisible() ? 1 : 0, Body->IsReady() ? 1 : 0, Player->IsHidden() ? 1 : 0);
				}
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("WorldMapQA"), Name), true, false);
				UE_LOG(LogTemp, Display, TEXT("WORLDMAP shot %s"), *Name);
			};
			if (PendingStand && Map && Player && Now >= StandAt)
			{
				Player->SetActorLocation(Map->GetNodeLocation(PendingStand) + FVector(0.0f, 0.0f, 100.0f), false, nullptr, ETeleportType::TeleportPhysics);
				PendingStand = 0;
			}
			if (Now < NextAt)
			{
				return false;
			}
			// A moment after the picture just asked for (it is taken at the end of the frame).
			const auto StandOn = [this, Now](const int32 Id)
			{
				PendingStand = Id;
				StandAt = Now + 0.25;
			};
			const auto Clear = [](std::initializer_list<int32> Ids, const bool bShown)
			{
				UChaosImpactSoloProgress* Progress = UChaosImpactSoloProgress::Load();
				Progress->ResetAll();
				for (const int32 Id : Ids)
				{
					Progress->MarkCleared(Id);
				}
				// Everything that shows now has been seen, but for what the last clear opens (when not bShown).
				for (const FChaosImpactSoloStageInfo& Info : ChaosImpactSolo::GetStages())
				{
					if (bShown && Progress->IsRevealed(Info.Number))
					{
						Progress->Shown.Add(Info.Number);
					}
				}
				Progress->Save();
				return Progress;
			};
			switch (Step)
			{
			case 0:
				// From no progress at all.
				if (!PC || !Map)
				{
					return false;
				}
				UChaosImpactSoloProgress::Load()->ResetAll();
				PC->OpenSoloWorldMap(1, 1);
				Step = 1;
				NextAt = Now + 2.0;
				return false;
			case 1:
				if (!Map || !Player || !Map->IsRevealing() || Map->GetRevealingNode() != 1)
				{
					return false;
				}
				Test->TestTrue(TEXT("The player is on the world map's walker"), Player->IsMapWalker());
				Step = 2;
				NextAt = Now + 0.7;
				return false;
			case 2:
				Shot(TEXT("01_Intro_FogClearing.png"));
				Step = 3;
				return false;
			case 3:
				if (Map->IsRevealing())
				{
					return false;
				}
				Test->TestEqual(TEXT("Standing on stage 1"), Map->GetFocusedNode(), 1);
				Test->TestTrue(TEXT("Stage 1 is out of the fog"), Map->GetNodeReveal(1) > 0.99f);
				Test->TestTrue(TEXT("Stage 2 is still in the fog"), Map->GetNodeReveal(2) < 0.01f);
				Test->TestFalse(TEXT("Not complete: the fog stays"), Map->IsMapComplete() || Map->GetFullClear() > 0.0f);
				Shot(TEXT("02_Start.png"));
				// Off the paths: the map puts the player back.
				Player->SetActorLocation(Map->GetNodeLocation(1) + FVector(0.0f, 900.0f, 100.0f), false, nullptr, ETeleportType::TeleportPhysics);
				Step = 4;
				NextAt = Now + 0.3;
				return false;
			case 4:
				Test->TestTrue(TEXT("Held on the paths"), Map->IsWalkable(Player->GetActorLocation()));
				UE_LOG(LogTemp, Display, TEXT("WORLDMAP held at %s"), *Player->GetActorLocation().ToString());
				// Clearing stage 1: the sea and the forest open.
				Clear({1}, false);
				PC->OpenSoloWorldMap(1, 1);
				Step = 5;
				NextAt = Now + 2.0;
				return false;
			case 5:
				if (!Map || !Map->IsRevealing() || Map->GetRevealingNode() == 0)
				{
					return false;
				}
				Step = 6;
				NextAt = Now + 1.0;
				return false;
			case 6:
				Shot(TEXT("03_Reveal_Stage.png"));
				Step = 7;
				return false;
			case 7:
				if (Map->IsRevealing())
				{
					return false;
				}
				Test->TestTrue(TEXT("Stage 2 opened"), Map->GetNodeReveal(2) > 0.99f);
				Test->TestTrue(TEXT("Stage 8 opened"), Map->GetNodeReveal(8) > 0.99f);
				Test->TestTrue(TEXT("Stage 3 still hidden"), Map->GetNodeReveal(3) < 0.01f);
				Shot(TEXT("04_AfterStage1.png"));
				// A click's route along the paths to stage 2.
				Map->WalkTo(2);
				Step = 8;
				NextAt = Now + 6.0;
				return false;
			case 8:
				Test->TestEqual(TEXT("Walked along the path to stage 2"), Map->GetFocusedNode(), 2);
				Shot(TEXT("05_Card_Stage2.png"));
				// Halfway through: the sea and the forest partly cleared.
				Clear({1, 2, 3, 4, 5, 8, 9, 10}, true);
				PC->OpenSoloWorldMap(1, 4);
				Step = 9;
				NextAt = Now + 3.0;
				return false;
			case 9:
				if (!Map || !Player || Map->IsRevealing() || Map->GetFocusedNode() != 4)
				{
					return false;
				}
				Shot(TEXT("06_Sea_Midway.png"));
				StandOn(10);
				Step = 10;
				NextAt = Now + 1.6;
				return false;
			case 10:
				Shot(TEXT("07_Forest_Midway.png"));
				Map->SetOverview(true);
				Step = 100;
				NextAt = Now + 2.5;
				return false;
			case 100:
				Shot(TEXT("07b_Overview.png"));
				Map->SetOverview(false);
				// Everything open (the development switch): each region in turn.
				UChaosImpactSoloProgress::Load()->ResetAll();
				Clear({1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14}, true);
				Map->RefreshFromProgress();
				StandOn(7);
				Step = 11;
				NextAt = Now + 1.6;
				return false;
			case 11:
				Shot(TEXT("08_Sea_Boss.png"));
				StandOn(12);
				Step = 12;
				NextAt = Now + 1.6;
				return false;
			case 12:
				Shot(TEXT("09_Forest_Tree.png"));
				StandOn(14);
				Step = 13;
				NextAt = Now + 1.6;
				return false;
			case 13:
				Shot(TEXT("10_CloudPath.png"));
				StandOn(15);
				Step = 14;
				NextAt = Now + 1.6;
				return false;
			case 14:
				Shot(TEXT("11_Temple.png"));
				// The temple cleared: the gate opens; through it to map 2.
				Clear({1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}, true);
				Map->RefreshFromProgress();
				StandOn(ChaosImpactSolo::GateToMap2);
				Step = 15;
				NextAt = Now + 1.6;
				return false;
			case 15:
				Test->TestEqual(TEXT("On the gate"), Map->GetFocusedNode(), ChaosImpactSolo::GateToMap2);
				Test->TestTrue(TEXT("Map 1 all cleared: no fog left"), Map->IsMapComplete() && Map->GetFullClear() >= 1.0f);
				Shot(TEXT("12_Gate.png"));
				// What the temple opens on map 2 has not been seen yet (the map's own progress is what it saves).
				Map->GetProgress()->Shown.Remove(16);
				Map->GetProgress()->Shown.Remove(22);
				Test->TestTrue(TEXT("The gate opens"), Map->ConfirmFocused());
				Step = 16;
				NextAt = Now + 2.0;
				return false;
			case 16:
				if (!Map || Map->MapIndex != 2 || !Map->IsRevealing() || Map->GetRevealingNode() == 0)
				{
					return false;
				}
				Step = 17;
				NextAt = Now + 1.0;
				return false;
			case 17:
				Shot(TEXT("13_Map2_Reveal.png"));
				Step = 18;
				return false;
			case 18:
				if (Map->IsRevealing())
				{
					return false;
				}
				Test->TestEqual(TEXT("Map 2 starts on its gate"), Map->GetFocusedNode(), ChaosImpactSolo::GateToMap1);
				Shot(TEXT("14_Map2_Start.png"));
				{
					UChaosImpactSoloProgress* Progress = UChaosImpactSoloProgress::Load();
					Progress->bUnlockAll = true;
					Progress->Save();
				}
				Map->RefreshFromProgress();
				StandOn(18);
				Step = 19;
				NextAt = Now + 1.6;
				return false;
			case 19:
				Shot(TEXT("15_Map2_Ice.png"));
				StandOn(24);
				Step = 20;
				NextAt = Now + 1.6;
				return false;
			case 20:
				Shot(TEXT("16_Map2_Fire.png"));
				StandOn(28);
				Step = 21;
				NextAt = Now + 1.6;
				return false;
			case 21:
				Shot(TEXT("17_Map2_Factory.png"));
				StandOn(30);
				Step = 22;
				NextAt = Now + 1.6;
				return false;
			case 22:
				Shot(TEXT("18_Map2_WhiteBallHouse.png"));
				// Back on map 1, stage 1: the confirm button plays it.
				Clear({}, true);
				PC->OpenSoloWorldMap(1, 1);
				Step = 23;
				NextAt = Now + 2.0;
				return false;
			case 23:
				if (!Map || Map->MapIndex != 1 || Map->IsRevealing() || Map->GetFocusedNode() != 1)
				{
					return false;
				}
				Test->TestTrue(TEXT("Stage 1 opens"), Map->ConfirmFocused());
				Step = 24;
				NextAt = Now + 1.0;
				return false;
			case 24:
				if (!World->GetMapName().Contains(TEXT("Lvl_SoloStage1")) || !PC || !PC->IsSoloMode() || !Player)
				{
					return false;
				}
				Test->TestFalse(TEXT("In the stage the player is not the map's walker"), Player->IsMapWalker());
				// Cleared: back to the map a few seconds later.
				PC->NotifySoloStageCleared();
				Step = 25;
				NextAt = Now + 1.0;
				return false;
			case 25:
				if (!Map || Map->MapIndex != 1 || !Player)
				{
					return false;
				}
				Test->TestTrue(TEXT("Stage 1 saved as cleared"), UChaosImpactSoloProgress::Load()->IsCleared(1));
				Step = 26;
				NextAt = Now + 2.0;
				return false;
			case 26:
				Shot(TEXT("19_BackFromStage1.png"));
				UChaosImpactSoloProgress::Load()->ResetAll();
				Step = 27;
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
		int32 PendingStand = 0;
		double StandAt = 0.0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactWorldMapTest, "ChaosImpact.Solo.WorldMap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactWorldMapTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FWorldMapCommand(this));
	return true;
}

namespace
{
	/** The whole map's view: an open stage picked there is warped to (WarpToNode), not walked to. */
	class FWorldMapWarpCommand : public IAutomationLatentCommand
	{
	public:
		explicit FWorldMapWarpCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - StartedAt > 240.0)
			{
				Test->AddError(FString::Printf(TEXT("Stuck at step %d."), Step));
				return true;
			}
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
			AChaosImpactWorldMap* Map = World ? AChaosImpactWorldMap::Find(World) : nullptr;
			if (!PC || !Map || !Player || Now < NextAt)
			{
				return false;
			}
			switch (Step)
			{
			case 0:
			{
				// Stages 1-3 cleared and seen (no fog to clear), the map opened afresh on stage 1.
				UChaosImpactSoloProgress* Progress = UChaosImpactSoloProgress::Load();
				Progress->ResetAll();
				for (const int32 Id : {1, 2, 3})
				{
					Progress->MarkCleared(Id);
				}
				for (const FChaosImpactSoloStageInfo& Info : ChaosImpactSolo::GetStages())
				{
					if (Progress->IsRevealed(Info.Number))
					{
						Progress->Shown.Add(Info.Number);
					}
				}
				Progress->Save();
				PC->OpenSoloWorldMap(1, 1);
				Step = 1;
				NextAt = Now + 6.0;
				return false;
			}
			case 1:
				if (Map->IsRevealing())
				{
					return false;
				}
				Map->SetOverview(true);
				Step = 2;
				NextAt = Now + 2.0;
				return false;
			case 2:
				Start = Player->GetActorLocation();
				Map->SetOverview(false);
				Map->WarpToNode(3);
				Step = 3;
				NextAt = Now + 1.5;
				return false;
			default:
			{
				const float Off = FVector::Dist2D(Player->GetActorLocation(), Map->GetNodeLocation(3));
				UE_LOG(LogTemp, Display, TEXT("WORLDMAPWARP from %s to %s (%.0f cm off stage 3), focused %d"), *Start.ToCompactString(),
					*Player->GetActorLocation().ToCompactString(), Off, Map->GetFocusedNode());
				Test->TestTrue(TEXT("On stage 3"), Off < 150.0f);
				Test->TestEqual(TEXT("Stage 3 focused"), Map->GetFocusedNode(), 3);
				UChaosImpactSoloProgress::Load()->ResetAll();
				return true;
			}
			}
		}

	private:
		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Step = 0;
		FVector Start = FVector::ZeroVector;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactWorldMapWarpTest, "ChaosImpact.Solo.MapWarp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactWorldMapWarpTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FWorldMapWarpCommand(this));
	return true;
}

#endif
