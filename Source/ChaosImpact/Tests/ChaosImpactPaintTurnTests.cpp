#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactColourEdit.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactMenuWidget.h"
#include "ChaosImpactPaintStudio.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactScreen.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	/**
	 * ペイント's turn goes with the hand, as on the カラーエディット (2026-11-14: it went the other way): dragging right with the
	 * right button held turns the camera round so the model's near side follows the mouse (the camera's yaw goes up), and the
	 * right stick to the right does the same. Screenshots Saved/PaintQA/turn_*.png (before, after the drag).
	 */
	class FPaintTurnCommand : public IAutomationLatentCommand
	{
	public:
		explicit FPaintTurnCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - StartedAt > 90.0)
			{
				Test->AddError(FString::Printf(TEXT("Timed out at stage %d"), Stage));
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
			UChaosImpactMenuWidget* Menu = PC ? PC->GetMenuWidget() : nullptr;
			UChaosImpactColourEdit* Edit = Menu ? Menu->GetColourEdit() : nullptr;
			UChaosImpactPaintStudio* Studio = Edit ? Edit->GetStudio() : nullptr;
			AChaosImpactPlayerState* State = PC ? PC->GetPlayerState<AChaosImpactPlayerState>() : nullptr;
			if (!PC || !Menu || !PC->GetPawn() || !State || Now < NextAt)
			{
				return false;
			}
			const auto Shot = [](const FString& Name)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PaintQA"), Name), true, false);
			};
			const auto Next = [this, Now](const double Seconds) { ++Stage; NextAt = Now + Seconds; return false; };
			const FVector2D Middle(800.0, 450.0);
			switch (Stage)
			{
			case 0:
				if (Now - StartedAt < 4.0) { return false; }
				PC->ShowMenuScreen(EChaosImpactScreen::Pause);
				PC->OpenColourEdit(EChaosImpactScreen::Pause);
				return Next(2.0);
			case 1:
				if (!Edit || !Edit->IsOpen())
				{
					Test->AddError(TEXT("カラーエディット did not open"));
					return true;
				}
				Edit->SelectCharacter(State->CharacterIndex);
				Edit->OpenPaint();
				return Next(1.5);
			case 2:
				if (!Studio || !Studio->IsOpen())
				{
					Test->AddError(TEXT("ペイント did not open"));
					return true;
				}
				YawBefore = Studio->GetViewYaw();
				Shot(TEXT("turn_0_before.png"));
				return Next(0.6);
			case 3:
				// Right button down in the middle, then the mouse 150 px to the right.
				Studio->HandleMouseMove(Middle);
				Studio->HandleMouseDown(Middle, EKeys::RightMouseButton, false);
				Studio->HandleMouseMove(Middle + FVector2D(75.0, 0.0));
				Studio->HandleMouseMove(Middle + FVector2D(150.0, 0.0));
				Studio->HandleMouseUp(Middle + FVector2D(150.0, 0.0), EKeys::RightMouseButton);
				return Next(0.8);
			case 4:
			{
				const float Turned = FMath::FindDeltaAngleDegrees(YawBefore, Studio->GetViewYaw());
				UE_LOG(LogTemp, Display, TEXT("PAINTTURN drag right 150 px: camera yaw %+.1f degrees"), Turned);
				Test->TestTrue(TEXT("Dragging right turns the camera's yaw up (the model's near side follows the mouse)"), Turned > 30.0f);
				Shot(TEXT("turn_1_dragged_right.png"));
				Edit->Close();
				return Next(0.5);
			}
			default:
				return true;
			}
		}

	private:
		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Stage = 0;
		float YawBefore = 0.0f;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactPaintTurnTest, "ChaosImpact.Paint.TurnDirection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactPaintTurnTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FPaintTurnCommand(this));
	return true;
}

#endif
