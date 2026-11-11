#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactDojo.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	UWorld* DojoMatchTestWorld()
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World() && Context.World()->IsGameWorld())
			{
				return Context.World();
			}
		}
		return nullptr;
	}

	/**
	 * Run on a mock-match lesson's match, opened as the dojo opens it, e.g.
	 * /Game/ChaosImpact/Versus/Levels/Lvl_VS_Stage1?CITraining=1?CIVersus=1?CITargets=0?CIMatch=1?CIMinutes=3?CITeams=2?CIMatchCPU=3
	 * ?CIStage=0?CICPULevel=0?CIDuel=0?CIDojo=1?CIDojoLesson=3_1
	 * The match plays itself (its opening, its HUD) with the task card on it; screenshots through the opening and the
	 * play, then the task counted to CLEAR and back to the dojo's list on that lesson. -CIDojoShot=<prefix>. Saved/DojoQA.
	 */
	class FDojoMockMatchCommand : public IAutomationLatentCommand
	{
	public:
		explicit FDojoMockMatchCommand(FAutomationTestBase* InTest) : Test(InTest)
		{
			FParse::Value(FCommandLine::Get(), TEXT("CIDojoShot="), Prefix);
		}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - StartedAt > 240.0)
			{
				Test->AddError(TEXT("Timed out."));
				return true;
			}
			UWorld* World = DojoMatchTestWorld();
			AChaosImpactPlayerController* PC = World ? Cast<AChaosImpactPlayerController>(World->GetFirstPlayerController()) : nullptr;
			AChaosImpactDojo* Dojo = World ? AChaosImpactDojo::Find(World) : nullptr;
			if (!PC || !Dojo || Now < NextAt)
			{
				return false;
			}
			const AChaosImpactGameState* Match = World->GetGameState<AChaosImpactGameState>();
			const bool bMatchWorld = World->URL.HasOption(TEXT("CIMatch=1"));
			const auto Shot = [this](const TCHAR* Name)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DojoQA"),
					FString::Printf(TEXT("%s_%s"), *Prefix, Name)), true, false);
			};
			switch (Stage)
			{
			case 0:
				if (!bMatchWorld || Dojo->GetPhase() != EChaosImpactDojoPhase::Play)
				{
					return false;
				}
				Test->TestTrue(TEXT("The match is a VS match"), Match && Match->bVersusMatch);
				Shot(TEXT("1_Opening"));
				NextAt = Now + 7.0;
				++Stage;
				break;
			case 1:
				Shot(TEXT("2_Match"));
				NextAt = Now + 6.0;
				++Stage;
				break;
			case 2:
				Shot(TEXT("3_Match"));
				UE_LOG(LogTemp, Log, TEXT("MockMatch: match phase %d, competitors %d"), Match ? static_cast<int32>(Match->Phase) : -1,
					Match ? Match->GetCompetitors(true).Num() : -1);
				// The task done (as the match's numbers would make it).
				Dojo->DebugCount(10);
				NextAt = Now + 0.9;
				++Stage;
				break;
			case 3:
				Test->TestEqual(TEXT("CLEAR"), static_cast<int32>(Dojo->GetPhase()), static_cast<int32>(EChaosImpactDojoPhase::Clear));
				Shot(TEXT("4_Clear"));
				NextAt = Now + AChaosImpactDojo::ClearMenuDelay + 0.6;
				Stage = 31;
				break;
			case 31:
				Test->TestTrue(TEXT("CLEAR offers its choices"), Dojo->IsClearMenuUp());
				Shot(TEXT("4b_Choices"));
				// Back to the lesson list.
				Dojo->ChooseAfterClear(2);
				Stage = 4;
				break;
			case 4:
				// Back in the dojo's own world, on its list.
				if (bMatchWorld || PC->GetCurrentScreen() != EChaosImpactScreen::DojoSelect)
				{
					return false;
				}
				NextAt = Now + 1.5;
				++Stage;
				break;
			case 5:
				Shot(TEXT("5_List"));
				NextAt = Now + 1.0;
				++Stage;
				break;
			default:
				return true;
			}
			return false;
		}

	private:
		FAutomationTestBase* Test = nullptr;
		FString Prefix = TEXT("mock");
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Stage = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactDojoMockMatchTest, "ChaosImpact.Dojo.MockMatch",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactDojoMockMatchTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FDojoMockMatchCommand(this));
	return true;
}

#endif
