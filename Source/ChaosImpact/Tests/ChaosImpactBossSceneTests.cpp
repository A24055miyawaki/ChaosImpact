#if WITH_DEV_AUTOMATION_TESTS
// シマエナガ戦の演出 (2026-11-19): the arena entered, the arrival film plays (frames along it), the talk before (each line
// typed out), the fight; the boss beaten, the talk after, it flies off and the stage is cleared. Frames in
// Saved/SoloQA/Scene_*.png. Run on stage 8: /Game/ChaosImpact/solo/Lvl_Solo_08_SnowForest?CISolo=1?CIStage=8
#include "Misc/AutomationTest.h"
#include "ChaosImpactBossScene.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactSimaeBoss.h"
#include "ChaosImpactSoloRoom.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HighResScreenshot.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"

namespace
{
	class FBossSceneCommand : public IAutomationLatentCommand
	{
	public:
		explicit FBossSceneCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		bool Update() override
		{
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld()) { World = Context.World(); }
			}
			const double Now = FPlatformTime::Seconds();
			if (Now - Started > 240.0) { Test->AddError(FString::Printf(TEXT("Boss scene test stalled at step %d"), Step)); return true; }
			if (!World || Now < WaitUntil) { return false; }
			APlayerController* PC = World->GetFirstPlayerController();
			AChaosImpactCharacter* Own = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			if (!Own) { return false; }
			AChaosImpactSoloRoom* Room = nullptr;
			for (TActorIterator<AChaosImpactSoloRoom> It(World); It; ++It) { Room = *It; }
			AChaosImpactSimaeBoss* Boss = nullptr;
			for (TActorIterator<AChaosImpactSimaeBoss> It(World); It; ++It) { Boss = *It; }
			AChaosImpactBossScene* Scene = AChaosImpactBossScene::GetActive(World);
			const auto Shot = [](const FString& Name)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SoloQA"), Name + TEXT(".png")), true, false);
			};
			switch (Step)
			{
			case 0:
				WaitUntil = Now + 4.0;
				Step = 1;
				return false;
			case 1:
				if (!Room || !Boss) { Test->AddError(TEXT("No arena or boss on stage 8")); return true; }
				Test->TestTrue(TEXT("The boss waits asleep"), Boss->IsDormant());
				// Into the arena (as walking through its doorway does).
				Own->TeleportTo(Room->EntryTrigger->GetComponentLocation() + FVector(0.0f, 0.0f, 60.0f), Own->GetActorRotation());
				Room->StartRoom(Own);
				SceneStarted = Now;
				Step = 2;
				return false;
			case 2:
			{
				// Frames along the film.
				static const float Times[] = {1.6f, 3.6f, 4.6f, 5.2f, 5.6f, 6.0f, 6.4f, 7.6f, 9.4f, 10.8f, 12.2f, 13.9f, 15.6f, 16.6f, 17.8f, 19.6f};
				const float T = static_cast<float>(Now - SceneStarted);
				if (Frame < UE_ARRAY_COUNT(Times) && T >= Times[Frame])
				{
					Shot(FString::Printf(TEXT("Scene_Film_%02d"), Frame));
					++Frame;
				}
				if (Scene && Scene->GetPart() == AChaosImpactBossScene::EPart::TalkBefore)
				{
					Test->TestTrue(TEXT("The film plays out into the talk"), T > 20.0f);
					Step = 3;
					LineShot = -1;
					WaitUntil = Now + 0.3;
				}
				else if (T > 40.0f)
				{
					Test->AddError(TEXT("The film never reached the talk"));
					return true;
				}
				return false;
			}
			case 3:
			case 7:
			{
				if (!Scene) { Test->AddError(TEXT("The scene went away in the talk")); return true; }
				const bool bAfter = Step == 7;
				const AChaosImpactBossScene::EPart Talk = bAfter ? AChaosImpactBossScene::EPart::TalkAfter : AChaosImpactBossScene::EPart::TalkBefore;
				if (Scene->GetPart() != Talk)
				{
					Step = bAfter ? 8 : 4;
					WaitUntil = Now + (bAfter ? 0.2 : 1.6);
					return false;
				}
				// Each line: wait for it to type out, a frame, then on.
				if (LineShot != Scene->GetLineIndex())
				{
					LineShot = Scene->GetLineIndex();
					WaitUntil = Now + 3.0;
					return false;
				}
				// (The frame taken a moment after it is asked for: on only once it is in.)
				if (!bLineShot)
				{
					bLineShot = true;
					Shot(FString::Printf(TEXT("Scene_%s_%d"), bAfter ? TEXT("After") : TEXT("Before"), LineShot));
					WaitUntil = Now + 1.2;
					return false;
				}
				bLineShot = false;
				Scene->RequestNext();
				WaitUntil = Now + 0.3;
				return false;
			}
			case 4:
				Test->TestTrue(TEXT("After the talk: the fight (the boss awake, the player free)"), Boss && !Boss->IsDormant() && !Own->IsTrainingMenuFrozen()
					&& Scene && Scene->GetPart() == AChaosImpactBossScene::EPart::Fight);
				Shot(TEXT("Scene_Fight"));
				// Down to half: it powers up.
				if (Boss)
				{
					Boss->bHarmless = true;
					UGameplayStatics::ApplyDamage(Boss, Boss->GetMaxHealth() * 0.5f, PC, Own, nullptr);
				}
				WaitUntil = Now + 6.0;
				Step = 5;
				return false;
			case 5:
				if (Boss)
				{
					UGameplayStatics::ApplyDamage(Boss, Boss->GetMaxHealth(), PC, Own, nullptr);
				}
				WaitUntil = Now + 0.4;
				Step = 6;
				return false;
			case 6:
				Test->TestTrue(TEXT("Beaten, not gone: its scene follows"), Boss && Boss->IsBeaten() && !Boss->IsEliminated());
				Shot(TEXT("Scene_Beaten"));
				WaitUntil = Now + 2.6;
				Step = 7;
				LineShot = -1;
				return false;
			case 8:
				if (Scene && Scene->GetPart() == AChaosImpactBossScene::EPart::Leave)
				{
					if (Scene->GetPartTime() > 1.0f && !bLeaveShot)
					{
						bLeaveShot = true;
						Shot(TEXT("Scene_Leave"));
					}
					return false;
				}
				WaitUntil = Now + 1.5;
				Step = 9;
				return false;
			case 9:
			{
				const AChaosImpactPlayerController* Player = Cast<AChaosImpactPlayerController>(PC);
				Test->TestTrue(TEXT("Gone, and the stage cleared"), Boss == nullptr && Player && Player->GetSoloClearedAt() >= 0.0);
				Shot(TEXT("Scene_Clear"));
				WaitUntil = Now + 0.5;
				Step = 10;
				return false;
			}
			default:
				return true;
			}
		}

	private:
		FAutomationTestBase* Test = nullptr;
		int32 Step = 0;
		int32 Frame = 0;
		int32 LineShot = -1;
		bool bLeaveShot = false;
		bool bLineShot = false;
		double SceneStarted = 0.0;
		double WaitUntil = 0.0;
		double Started = FPlatformTime::Seconds();
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactBossSceneTest, "ChaosImpact.Solo.BossScene",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FChaosImpactBossSceneTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FBossSceneCommand(this));
	return true;
}
#endif
