#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactBall.h"
#include "ChaosImpactBallSpawner.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactScreen.h"
#include "ChaosImpactSoloRoom.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	/**
	 * From the title's menus (run on the title level, /Game/ThirdPerson/Lvl_ThirdPerson): ソロ, then ステージ1, opens the
	 * solo room level in solo mode, with its room and the player in it. A screenshot goes to Saved/SoloRoomQA.
	 */
	class FSoloMenuEntryCommand : public IAutomationLatentCommand
	{
	public:
		explicit FSoloMenuEntryCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - StartedAt > 90.0)
			{
				Test->AddError(FString::Printf(TEXT("Stuck at step %d."), Step));
				return true;
			}
			if (Now < NextAt)
			{
				return false;
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
			if (!PC)
			{
				return false;
			}
			const auto Press = [](const FKey& Key)
			{
				FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0));
				FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0));
			};
			switch (Step)
			{
			case 0:
				if (Now - StartedAt < 3.0)
				{
					return false;
				}
				// The mode select starts on ソロ.
				PC->ShowMenuScreen(EChaosImpactScreen::ModeSelect);
				Press(EKeys::Enter);
				Step = 1;
				NextAt = Now + 0.5;
				return false;
			case 1:
				Test->TestEqual(TEXT("ソロ opens the stage select"), static_cast<int32>(PC->GetCurrentScreen()),
					static_cast<int32>(EChaosImpactScreen::SoloStageSelect));
				// It starts on ステージ1.
				Press(EKeys::Enter);
				Step = 2;
				NextAt = Now + 0.5;
				return false;
			case 2:
			{
				// On to the solo room's level, playing.
				if (!World->GetMapName().Contains(TEXT("Lvl_SoloRoomTest")) || !PC->IsGameplayActive() || !PC->GetPawn())
				{
					return false;
				}
				Test->TestTrue(TEXT("Solo mode"), PC->IsSoloMode());
				int32 Rooms = 0;
				for (TActorIterator<AChaosImpactSoloRoom> It(World); It; ++It)
				{
					++Rooms;
				}
				Test->TestEqual(TEXT("The level has its room"), Rooms, 1);
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SoloRoomQA"), TEXT("5_FromMenu.png")),
					true, false);
				UE_LOG(LogTemp, Display, TEXT("SOLOMENU arrived in %s"), *World->GetMapName());
				Step = 3;
				NextAt = Now + 1.5;
				return false;
			}
			case 3:
			{
				// The pads placed in the level put out balls here too.
				int32 Pads = 0;
				int32 Balls = 0;
				for (TActorIterator<AChaosImpactBallSpawner> It(World); It; ++It)
				{
					++Pads;
					Balls += It->GetActiveBall() ? 1 : 0;
				}
				UE_LOG(LogTemp, Display, TEXT("SOLOMENU %d pads, %d with a ball"), Pads, Balls);
				Test->TestTrue(TEXT("The level has ball pads"), Pads > 0);
				Test->TestEqual(TEXT("Every pad has put out a ball"), Balls, Pads);
				return true;
			}
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactSoloMenuEntryTest, "ChaosImpact.Solo.MenuEntry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactSoloMenuEntryTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FSoloMenuEntryCommand(this));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactBallChancesTest, "ChaosImpact.Training.BallChances",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

/** A pad's ball chances come from its weights, compared with each other. */
bool FChaosImpactBallChancesTest::RunTest(const FString& Parameters)
{
	const AChaosImpactBallSpawner* Defaults = GetDefault<AChaosImpactBallSpawner>();
	float Sum = 0.0f;
	for (int32 Index = 0; Index < ChaosImpactBallTypes::Count; ++Index)
	{
		Sum += Defaults->GetChance(static_cast<EChaosImpactBallType>(Index));
	}
	TestEqual(TEXT("The chances add up to one"), Sum, 1.0f, 0.001f);
	TestEqual(TEXT("Normal balls as often as before by default"), Defaults->GetChance(EChaosImpactBallType::Normal), 0.185f, 0.001f);
	TestEqual(TEXT("A fire ball now and then"), Defaults->GetChance(EChaosImpactBallType::Fire), 0.11f, 0.001f);

	AChaosImpactBallSpawner* Pad = NewObject<AChaosImpactBallSpawner>(GetTransientPackage());
	for (int32 Index = 0; Index < ChaosImpactBallTypes::Count; ++Index)
	{
		Pad->SetChanceWeight(static_cast<EChaosImpactBallType>(Index), 0.0f);
	}
	TestEqual(TEXT("All weights 0: only normal balls"), Pad->GetChance(EChaosImpactBallType::Normal), 1.0f);
	Pad->SetChanceWeight(EChaosImpactBallType::Normal, 3.0f);
	Pad->SetChanceWeight(EChaosImpactBallType::Fire, 1.0f);
	TestEqual(TEXT("Normal 3, Fire 1: three normal to one fire"), Pad->GetChance(EChaosImpactBallType::Normal), 0.75f, 0.001f);
	TestEqual(TEXT("...a quarter fire"), Pad->GetChance(EChaosImpactBallType::Fire), 0.25f, 0.001f);
	TestEqual(TEXT("...never ice"), Pad->GetChance(EChaosImpactBallType::Ice), 0.0f);
	return true;
}

#endif
