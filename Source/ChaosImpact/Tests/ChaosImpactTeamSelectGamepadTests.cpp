#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactGameState.h"
#include "ChaosImpactPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericApplication.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	/** A real gamepad press through Slate, the way a controller button reaches the focused menu. */
	void PressGamepadKey(const FKey& Key)
	{
		if (!FSlateApplication::IsInitialized())
		{
			return;
		}
		const FKeyEvent Down(Key, FModifierKeysState(), FInputDeviceId::CreateFromInternalId(0), false, 0, 0, TOptional<int32>(0));
		FSlateApplication::Get().ProcessKeyDownEvent(Down);
		FSlateApplication::Get().ProcessKeyUpEvent(Down);
	}

	/** Run in a local team battle with one keyboard player (CITeams=2): team select must work from a controller. */
	class FTeamSelectGamepadCommand : public IAutomationLatentCommand
	{
	public:
		explicit FTeamSelectGamepadCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
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
					break;
				}
			}
			const AChaosImpactPlayerController* PC = World ? Cast<AChaosImpactPlayerController>(World->GetFirstPlayerController()) : nullptr;
			const AChaosImpactGameState* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
			const AChaosImpactPlayerState* Own = PC ? PC->GetPlayerState<AChaosImpactPlayerState>() : nullptr;
			const bool bTeamSelect = Match && Match->bVersusMatch && Match->Phase == EChaosImpactOnlinePhase::TeamSelect;
			switch (Stage)
			{
			case 0:
				if (!bTeamSelect || !Own)
				{
					if (Now - StartedAt < 150.0)
					{
						return false;
					}
					Test->AddError(TEXT("Team select never appeared (run with ?CIMatch=1?CITeams=2)."));
					return true;
				}
				TeamBefore = Own->TeamIndex;
				PressGamepadKey(EKeys::Gamepad_DPad_Right);
				Stage = 1;
				NextAt = Now + 0.6;
				return false;
			case 1:
				UE_LOG(LogTemp, Display, TEXT("TEAMPADTEST team %d -> %d"), TeamBefore, Own ? Own->TeamIndex : -1);
				Test->TestTrue(TEXT("D-pad right on a controller changes team"), Own && Own->TeamIndex != TeamBefore);
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("VersusQA"),
					TEXT("TeamSelect-Gamepad.png")), true, false);
				Stage = 2;
				NextAt = Now + 0.5;
				return false;
			case 2:
				// A on the focused 試合開始 starts the match.
				PressGamepadKey(EKeys::Gamepad_FaceButton_Bottom);
				Stage = 3;
				NextAt = Now + 2.0;
				return false;
			default:
				Test->TestFalse(TEXT("A on a controller starts the team battle"), bTeamSelect);
				return true;
			}
		}

	private:
		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Stage = 0;
		int32 TeamBefore = INDEX_NONE;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactTeamSelectGamepadTest, "ChaosImpact.Versus.TeamSelectGamepad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::EngineFilter)

bool FChaosImpactTeamSelectGamepadTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FTeamSelectGamepadCommand(this));
	return true;
}

#endif
