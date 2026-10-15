#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactPlayerController.h"
#include "ChaosImpactSoloStages.h"
#include "ChaosImpactSoloTitle.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	/**
	 * Solo mode's title (run on /Game/ChaosImpact/solo/Title/Lvl_SoloTitle?CISoloTitle=1): the logo, the three files (file 2
	 * begun, the others empty), erasing file 2 after the question, and a new file 3 started (it is saved and the world
	 * map opens on it). Pictures go to Saved/SoloTitleQA. Uses the tests' progress slots.
	 */
	class FSoloTitleCommand : public IAutomationLatentCommand
	{
	public:
		explicit FSoloTitleCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - StartedAt > 120.0)
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
			if (!World || Now < NextAt)
			{
				return false;
			}
			AChaosImpactPlayerController* PC = Cast<AChaosImpactPlayerController>(World->GetFirstPlayerController());
			AChaosImpactSoloTitle* Title = AChaosImpactSoloTitle::Find(World);
			const auto Shot = [](const TCHAR* Name)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SoloTitleQA"), Name), true, false);
			};
			switch (Step)
			{
			case 0:
			{
				if (!PC || !Title || Title->GetAge() < 4.5f)
				{
					return false;
				}
				Test->TestTrue(TEXT("On solo mode's title"), PC->IsSoloTitle());
				// File 2 begun (four stages cleared, standing on stage 8), files 1 and 3 empty.
				for (int32 Slot = 1; Slot <= 3; ++Slot)
				{
					UChaosImpactSoloProgress::DeleteSlot(Slot);
				}
				UChaosImpactSoloProgress::SetActiveSlot(2);
				UChaosImpactSoloProgress* Progress = UChaosImpactSoloProgress::Load();
				for (const int32 Id : {1, 2, 3, 8})
				{
					Progress->MarkCleared(Id);
				}
				Progress->LastNode = 8;
				Progress->Save();
				UChaosImpactSoloProgress::SetActiveSlot(1);
				Shot(TEXT("1_Press.png"));
				Step = 1;
				NextAt = Now + 0.5;
				return false;
			}
			case 1:
				Title->OpenFiles();
				Step = 2;
				NextAt = Now + 1.4;
				return false;
			case 2:
				Test->TestFalse(TEXT("File 1 empty"), Title->GetFile(0).bExists);
				Test->TestTrue(TEXT("File 2 begun"), Title->GetFile(1).bExists);
				Test->TestEqual(TEXT("File 2 has four stages cleared"), Title->GetFile(1).Cleared, 4);
				Shot(TEXT("2_Files.png"));
				Step = 3;
				NextAt = Now + 0.5;
				return false;
			case 3:
				Title->AskErase(1);
				Step = 4;
				NextAt = Now + 0.8;
				return false;
			case 4:
				Test->TestTrue(TEXT("Asked before erasing"), Title->GetStep() == EChaosImpactSoloTitleStep::ConfirmErase);
				Shot(TEXT("3_Erase.png"));
				Step = 5;
				NextAt = Now + 0.5;
				return false;
			case 5:
				Title->AnswerErase(true);
				Test->TestFalse(TEXT("File 2 erased"), UChaosImpactSoloProgress::DoesSlotExist(2));
				Test->TestFalse(TEXT("Its card empty"), Title->GetFile(1).bExists);
				Step = 6;
				NextAt = Now + 0.6;
				return false;
			case 6:
				Title->StartFile(2);
				Step = 7;
				NextAt = Now + 0.5;
				return false;
			case 7:
				if (!PC || !PC->IsSoloWorldMap())
				{
					return false;
				}
				Test->TestEqual(TEXT("Playing file 3"), UChaosImpactSoloProgress::GetActiveSlot(), 3);
				Test->TestTrue(TEXT("File 3 saved"), UChaosImpactSoloProgress::DoesSlotExist(3));
				for (int32 Slot = 1; Slot <= 3; ++Slot)
				{
					UChaosImpactSoloProgress::DeleteSlot(Slot);
				}
				UChaosImpactSoloProgress::SetActiveSlot(1);
				return true;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactSoloTitleTest, "ChaosImpact.Solo.TitleFiles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactSoloTitleTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FSoloTitleCommand(this));
	return true;
}

#endif
