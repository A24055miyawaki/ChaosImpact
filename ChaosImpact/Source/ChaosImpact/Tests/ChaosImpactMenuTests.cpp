#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactPlayerController.h"
#include "ChaosImpactMenuWidget.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactBallSpawner.h"
#include "Engine/Engine.h"
#include "Engine/DamageEvents.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "InputKeyEventArgs.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	AChaosImpactPlayerController* FindGameController()
	{
		if (GEngine && GEngine->GameViewport && GEngine->GameViewport->GetWorld())
		{
			if (auto* PC = Cast<AChaosImpactPlayerController>(
				GEngine->GameViewport->GetWorld()->GetFirstPlayerController()))
			{
				return PC;
			}
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World() && Context.World()->IsGameWorld())
			{
				if (auto* PC = Cast<AChaosImpactPlayerController>(Context.World()->GetFirstPlayerController()))
				{
					return PC;
				}
			}
		}
		return nullptr;
	}

	void MenuKey(FKey Key)
	{
		FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0));
		FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0));
	}

	void PauseKey(AChaosImpactPlayerController* PC)
	{
		PC->InputKey(FInputKeyEventArgs(nullptr, INPUTDEVICEID_NONE, EKeys::P, IE_Pressed, FPlatformTime::Cycles64()));
		PC->InputKey(FInputKeyEventArgs(nullptr, INPUTDEVICEID_NONE, EKeys::P, IE_Released, FPlatformTime::Cycles64()));
	}

	void Capture(const TCHAR* Name)
	{
		FScreenshotRequest::RequestScreenshot(
			FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MenuQA"), Name), true, false);
	}

	class FMenuFlowCommand : public IAutomationLatentCommand
	{
	public:
		explicit FMenuFlowCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now < NextAt) { return false; }
			auto* PC = FindGameController();
			if (!PC || !PC->GetMenuWidget())
			{
				if (Now - StartedAt < 60) { return false; }
				Test->AddError(TEXT("No local game controller/menu was created."));
				return true;
			}
			auto* Menu = PC->GetMenuWidget();
			auto Check = [this, PC](EChaosImpactScreen Expected, const TCHAR* Label)
			{
				return Test->TestTrue(Label, PC->GetCurrentScreen() == Expected);
			};
			switch (Step)
			{
			case 0:
				// Command-line automation can deliver viewport activation clicks before the
				// worker starts. Reset to the same state as a clean user launch.
				PC->ShowMenuScreen(EChaosImpactScreen::Title);
				if (!Check(EChaosImpactScreen::Title, TEXT("Startup opens title"))) { return true; }
				Test->TestTrue(TEXT("Supplied logo loaded"), Menu->HasLogo());
				Test->TestTrue(TEXT("Title freezes gameplay"), PC->IsPaused());
				break;
			case 1:
				Capture(TEXT("01-Title.png"));
				break;
			case 2:
				MenuKey(EKeys::Enter);
				if (!Check(EChaosImpactScreen::ModeSelect, TEXT("Enter opens modes"))) { return true; }
				break;
			case 3:
				Capture(TEXT("02-Modes-TrainingClick.png"));
				MenuKey(EKeys::Gamepad_DPad_Right);
				Test->TestEqual(TEXT("D-pad selects Multi"), Menu->GetSelectedIndex(), 1);
				MenuKey(EKeys::Gamepad_FaceButton_Bottom);
				if (!Check(EChaosImpactScreen::MultiReady, TEXT("Gamepad A opens Multi"))) { return true; }
				break;
			case 4:
				Capture(TEXT("03-Multi.png"));
				break;
			case 5:
				MenuKey(EKeys::Gamepad_FaceButton_Right);
				if (!Check(EChaosImpactScreen::ModeSelect, TEXT("Gamepad B returns to modes"))) { return true; }
				MenuKey(EKeys::Enter);
				if (!Check(EChaosImpactScreen::SoloReady, TEXT("Solo has its own ready screen"))) { return true; }
				break;
			case 6:
				Capture(TEXT("04-Solo.png"));
				break;
			case 7:
				MenuKey(EKeys::BackSpace);
				if (!Check(EChaosImpactScreen::ModeSelect, TEXT("Backspace returns to modes"))) { return true; }
				break;
			case 8:
			{
				Capture(TEXT("02-Modes.png"));
				// Mouse uses real Slate hit testing at the training button's center.
				const FGeometry& Geometry = Menu->GetCachedGeometry();
				const float Scale = FMath::Min(Geometry.GetLocalSize().X / 1600.0f, Geometry.GetLocalSize().Y / 900.0f);
				const FVector2D Offset = (Geometry.GetLocalSize() - FVector2D(1600, 900) * Scale) * 0.5f;
				const FVector2D Position = Geometry.LocalToAbsolute(Offset + FVector2D(1250, 745) * Scale);
				const TSet<FKey> Pressed{EKeys::LeftMouseButton};
				const TSet<FKey> Released;
				FSlateApplication::Get().ProcessMouseButtonDownEvent(nullptr,
					FPointerEvent(0, Position, Position, Pressed, EKeys::LeftMouseButton, 0, FModifierKeysState()));
				FSlateApplication::Get().ProcessMouseButtonUpEvent(
					FPointerEvent(0, Position, Position, Released, EKeys::LeftMouseButton, 0, FModifierKeysState()));
				TravelStartedAt = Now;
				break;
			}
			case 9:
				if (!PC->IsGameplayActive() && Now - TravelStartedAt < 45) { return false; }
				if (!Check(EChaosImpactScreen::Playing, TEXT("Mouse click loads training, bypassing title"))) { return true; }
				Test->TestFalse(TEXT("Training is unpaused"), PC->IsPaused());
				Test->TestTrue(TEXT("Training has a playable character"), IsValid(Cast<AChaosImpactCharacter>(PC->GetPawn())));
				Test->TestTrue(TEXT("Training URL is recognized"), PC->IsTrainingMode());
				{
					int32 SpawnerCount = 0;
					TArray<AChaosImpactBall*> PickupBalls;
					for (TActorIterator<AChaosImpactBallSpawner> It(PC->GetWorld()); It; ++It)
					{
						++SpawnerCount;
						if (AChaosImpactBall* PickupBall = It->GetActiveBall())
						{
							PickupBalls.Add(PickupBall);
						}
					}
					Test->TestEqual(TEXT("Training creates three ball spawners"), SpawnerCount, 3);
					if (AChaosImpactCharacter* Pawn = Cast<AChaosImpactCharacter>(PC->GetPawn());
						Pawn && PickupBalls.Num() >= 3)
					{
						Test->TestTrue(TEXT("First pickup adds a carried ball"), Pawn->TryPickupBall(PickupBalls[0]));
						Test->TestTrue(TEXT("Second pickup fills inventory"), Pawn->TryPickupBall(PickupBalls[1]));
						Test->TestFalse(TEXT("Third pickup is rejected at capacity"), Pawn->TryPickupBall(PickupBalls[2]));
						Test->TestEqual(TEXT("Inventory is capped at two balls"), Pawn->GetCarriedBallCount(), 2);
						PickupBalls[0]->Destroy();
						PickupBalls[1]->Destroy();
					}

					FActorSpawnParameters FlightTestParameters;
					FlightTestParameters.SpawnCollisionHandlingOverride =
						ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
					FlightTestParameters.Owner = PC->GetPawn();
					FlightTestParameters.Instigator = Cast<APawn>(PC->GetPawn());
					if (AChaosImpactBall* FlightTestBall = PC->GetWorld()->SpawnActor<AChaosImpactBall>(
						AChaosImpactBall::StaticClass(), PC->GetPawn()->GetActorLocation() + FVector::UpVector * 400.0f,
						FRotator::ZeroRotator, FlightTestParameters))
					{
						Test->TestTrue(TEXT("Ball permanently remembers its thrower"),
							FlightTestBall->WasThrownBy(Cast<APawn>(PC->GetPawn())));
						Test->TestEqual(TEXT("Thrown ball has no despawn lifespan"),
							FlightTestBall->GetLifeSpan(), 0.0f);
						if (AChaosImpactCharacter* Pawn = Cast<AChaosImpactCharacter>(PC->GetPawn()))
						{
							const float HealthBeforeSelfHit = Pawn->GetHealth();
							FDamageEvent SelfHitEvent;
							Test->TestEqual(TEXT("Own ball damage is rejected by the character"),
								Pawn->TakeDamage(1.0f, SelfHitEvent, PC, FlightTestBall), 0.0f);
							Test->TestEqual(TEXT("Own ball cannot lower health"),
								Pawn->GetHealth(), HealthBeforeSelfHit);
						}
						FlightTestBall->Launch(FVector::ForwardVector, 1200.0f,
							EChaosImpactBallFlightMode::Arc, 600.0f);
						if (UProjectileMovementComponent* Movement =
							FlightTestBall->FindComponentByClass<UProjectileMovementComponent>())
						{
							Test->TestEqual(TEXT("Arc throw enables gravity"), Movement->ProjectileGravityScale, 1.0f);
							Test->TestFalse(TEXT("Arc throw is not plane constrained"), Movement->bConstrainToPlane);
							Test->TestTrue(TEXT("Arc throw has upward velocity"), Movement->Velocity.Z > 0.0f);
							Test->TestTrue(TEXT("Arc throw travels forward"), Movement->Velocity.X > 0.0f);
							Test->TestEqual(TEXT("Arc throw has no sideways drift"), Movement->Velocity.Y, 0.0);
							FlightTestBall->Launch(FVector::ForwardVector, 1200.0f,
								EChaosImpactBallFlightMode::Straight);
							Test->TestEqual(TEXT("Straight throw keeps gravity disabled"),
								Movement->ProjectileGravityScale, 0.0f);
							Test->TestTrue(TEXT("Straight throw keeps the original plane constraint"),
								Movement->bConstrainToPlane);
							Test->TestEqual(TEXT("Straight throw keeps level height"), Movement->Velocity.Z, 0.0);
						}
						FlightTestBall->Destroy();
					}
				}
				Capture(TEXT("05-Training.png"));
				PC->InputKey(FInputKeyEventArgs(nullptr, INPUTDEVICEID_NONE, EKeys::LeftMouseButton, IE_Pressed, FPlatformTime::Cycles64()));
				break;
			case 10:
				if (auto* Pawn = Cast<AChaosImpactCharacter>(PC->GetPawn()))
				{
					Test->TestTrue(TEXT("First gameplay mouse click starts charging"),
						Pawn->GetThrowChargeAlpha() > 0.0f);
					PC->InputKey(FInputKeyEventArgs(nullptr, INPUTDEVICEID_NONE,
						EKeys::LeftMouseButton, IE_Released, FPlatformTime::Cycles64()));
					Test->TestEqual(TEXT("A picked-up ball can be thrown"), Pawn->GetCarriedBallCount(), 1);
					PC->InputKey(FInputKeyEventArgs(nullptr, INPUTDEVICEID_NONE,
						EKeys::LeftMouseButton, IE_Pressed, FPlatformTime::Cycles64()));
				}
				PauseKey(PC);
				break;
			case 11:
				if (!Check(EChaosImpactScreen::Pause, TEXT("P opens pause"))) { return true; }
				Test->TestTrue(TEXT("Gameplay is paused"), PC->IsPaused());
				PausedWorldTime = PC->GetWorld()->GetTimeSeconds();
				Capture(TEXT("06-Pause.png"));
				break;
			case 12:
				Test->TestEqual(TEXT("World time does not advance behind menu"), PC->GetWorld()->GetTimeSeconds(), PausedWorldTime);
				MenuKey(EKeys::P);
				if (!Check(EChaosImpactScreen::Playing, TEXT("P closes pause"))) { return true; }
				Test->TestFalse(TEXT("Resume unpauses gameplay"), PC->IsPaused());
				if (auto* Pawn = Cast<AChaosImpactCharacter>(PC->GetPawn()))
				{
					Test->TestEqual(TEXT("Paused charge is cancelled without throwing"), Pawn->GetThrowChargeAlpha(), 0.0f);
				}
				break;
			case 13:
				PauseKey(PC);
				break;
			case 14:
				if (!Check(EChaosImpactScreen::Pause, TEXT("Pause can reopen"))) { return true; }
				MenuKey(EKeys::Enter);
				if (!Check(EChaosImpactScreen::Playing, TEXT("Resume button returns to play"))) { return true; }
				break;
			case 15:
				PauseKey(PC);
				break;
			case 16:
				MenuKey(EKeys::Down);
				MenuKey(EKeys::Enter);
				if (!Check(EChaosImpactScreen::Pause, TEXT("Trajectory toggle keeps pause open"))) { return true; }
				Test->TestTrue(TEXT("Pause menu switches to gravity arc"),
					PC->GetBallFlightMode() == EChaosImpactBallFlightMode::Arc);
				MenuKey(EKeys::Down);
				MenuKey(EKeys::Enter);
				TravelStartedAt = Now;
				break;
			case 17:
				if (!PC->IsGameplayActive() && Now - TravelStartedAt < 45) { return false; }
				if (!Check(EChaosImpactScreen::Playing, TEXT("Training retry reloads the stage"))) { return true; }
				Test->TestTrue(TEXT("Retry preserves selected arc trajectory"),
					PC->GetBallFlightMode() == EChaosImpactBallFlightMode::Arc);
				if (auto* Pawn = Cast<AChaosImpactCharacter>(PC->GetPawn()))
				{
					Test->TestEqual(TEXT("Retry clears carried balls"), Pawn->GetCarriedBallCount(), 0);
					Test->TestEqual(TEXT("Retry resets stamina"), Pawn->GetStamina(), 5.0f);
				}
				PauseKey(PC);
				break;
			case 18:
				if (!Check(EChaosImpactScreen::Pause, TEXT("Pause opens after retry"))) { return true; }
				MenuKey(EKeys::Down);
				MenuKey(EKeys::Down);
				MenuKey(EKeys::Down);
				MenuKey(EKeys::Enter);
				if (!Check(EChaosImpactScreen::ModeSelect, TEXT("Pause modes button returns to selection"))) { return true; }
				Test->TestTrue(TEXT("Gameplay stays frozen in mode selection"), PC->IsPaused());
				MenuKey(EKeys::Right);
				MenuKey(EKeys::Down);
				Test->TestEqual(TEXT("Spatial arrows reach lower-right Training"), Menu->GetSelectedIndex(), 2);
				MenuKey(EKeys::Enter);
				TravelStartedAt = Now;
				break;
			case 19:
				if (!PC->IsGameplayActive() && Now - TravelStartedAt < 45) { return false; }
				if (!Check(EChaosImpactScreen::Playing, TEXT("Training can restart from menu"))) { return true; }
				if (auto* Pawn = Cast<AChaosImpactCharacter>(PC->GetPawn()))
				{
					Test->TestEqual(TEXT("Restart resets stamina"), Pawn->GetStamina(), 5.0f);
				}
				PauseKey(PC);
				break;
			case 20:
				MenuKey(EKeys::Down);
				MenuKey(EKeys::Down);
				MenuKey(EKeys::Down);
				MenuKey(EKeys::Down);
				MenuKey(EKeys::Enter);
				if (!Check(EChaosImpactScreen::Title, TEXT("Pause title button returns to title"))) { return true; }
				break;
			case 21:
				Test->AddInfo(TEXT("Menu and ball flow complete: inventory, both trajectories, retry, spawners and navigation."));
				return true;
			}
			++Step;
			NextAt = Now + (Step == 1 ? 2.0 : 0.7);
			return false;
		}

	private:
		FAutomationTestBase* Test;
		int32 Step = 0;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0;
		double TravelStartedAt = 0;
		double PausedWorldTime = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactMenuFlowTest, "ChaosImpact.UI.MenuFlow",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactMenuFlowTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FMenuFlowCommand(this));
	return true;
}

#endif
