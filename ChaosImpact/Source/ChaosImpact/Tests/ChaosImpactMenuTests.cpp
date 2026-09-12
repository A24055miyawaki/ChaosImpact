#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactPlayerController.h"
#include "ChaosImpactMenuWidget.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactBallSpawner.h"
#include "ChaosImpactTrainingTarget.h"
#include "ChaosImpactCPUController.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/DamageEvents.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Components/SphereComponent.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/CharacterMovementComponent.h"
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
				if (!Check(EChaosImpactScreen::TrainingSetup, TEXT("Training opens local player setup")))
				{
					return true;
				}
				MenuKey(EKeys::Gamepad_DPad_Right);
				Test->TestEqual(TEXT("D-pad selects two-player training"), Menu->GetSelectedIndex(), 1);
				MenuKey(EKeys::Gamepad_DPad_Left);
				Test->TestEqual(TEXT("D-pad returns to one-player training"), Menu->GetSelectedIndex(), 0);
				MenuKey(EKeys::Enter);
				TravelStartedAt = Now;
				break;
			}
			case 9:
				if (!PC->IsGameplayActive() && Now - TravelStartedAt < 45) { return false; }
				if (!Check(EChaosImpactScreen::Playing, TEXT("Mouse click loads training, bypassing title"))) { return true; }
				Test->TestFalse(TEXT("Training is unpaused"), PC->IsPaused());
				Test->TestTrue(TEXT("Training has a playable character"), IsValid(Cast<AChaosImpactCharacter>(PC->GetPawn())));
				if (AChaosImpactCharacter* Character = Cast<AChaosImpactCharacter>(PC->GetPawn()))
				{
					Test->TestEqual(TEXT("Training movement speed uses the slower tuning"),
						Character->GetCharacterMovement()->MaxWalkSpeed, 560.0f);
					Test->TestEqual(TEXT("Short dash uses the reduced range"),
						Character->GetDashDistance(), 220.0f);
				}
				Test->TestTrue(TEXT("Training URL is recognized"), PC->IsTrainingMode());
				Test->TestTrue(TEXT("Arc trajectory is the training default"),
					PC->GetBallFlightMode() == EChaosImpactBallFlightMode::Arc);
				Test->TestEqual(TEXT("One-player selection is preserved"),
					PC->GetRequestedLocalPlayerCount(), 1);
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
					int32 TargetCount = 0;
					int32 StationaryTargets = 0;
					int32 SideTargets = 0;
					int32 ForwardTargets = 0;
					for (TActorIterator<AChaosImpactTrainingTarget> It(PC->GetWorld()); It; ++It)
					{
						++TargetCount;
						switch (It->GetMotionMode())
						{
						case EChaosImpactTargetMotion::Stationary: ++StationaryTargets; break;
						case EChaosImpactTargetMotion::SideToSide: ++SideTargets; break;
						case EChaosImpactTargetMotion::ForwardBack: ++ForwardTargets; break;
						}
					}
					Test->TestEqual(TEXT("Training creates five sandbag targets"), TargetCount, 5);
					Test->TestEqual(TEXT("Training has two stationary targets"), StationaryTargets, 2);
					Test->TestEqual(TEXT("Training has two side-moving targets"), SideTargets, 2);
					Test->TestEqual(TEXT("Training has one depth-moving target"), ForwardTargets, 1);
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
							EChaosImpactBallFlightMode::Arc, 0.0f);
						if (UProjectileMovementComponent* Movement =
							FlightTestBall->FindComponentByClass<UProjectileMovementComponent>())
						{
							Test->TestEqual(TEXT("Arc throw enables gravity"), Movement->ProjectileGravityScale, 1.0f);
							Test->TestFalse(TEXT("Arc throw is not plane constrained"), Movement->bConstrainToPlane);
							Test->TestEqual(TEXT("Arc throw leaves the hand level"), Movement->Velocity.Z, 0.0);
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
						FlightTestBall->MakeRollingPickup(FVector(700.0f, 0.0f, 0.0f));
						Test->TestTrue(TEXT("Landed ball becomes collectible"), FlightTestBall->IsPickup());
						Test->TestFalse(TEXT("A ball cannot be caught immediately after impact"),
							FlightTestBall->IsPickupAvailable());
						Test->TestFalse(TEXT("A hit ball cannot be caught again immediately"),
							FlightTestBall->IsPickupAvailable());
						if (USphereComponent* Sphere = FlightTestBall->FindComponentByClass<USphereComponent>())
						{
							Test->TestTrue(TEXT("Landed pickup keeps rolling physics"), Sphere->IsSimulatingPhysics());
							Test->TestTrue(TEXT("Landed pickup retains forward momentum"),
								Sphere->GetPhysicsLinearVelocity().X > 0.0f);
						}
						FlightTestBall->Destroy();
					}

					FActorSpawnParameters TargetTestParameters;
					TargetTestParameters.SpawnCollisionHandlingOverride =
						ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
					if (AChaosImpactTrainingTarget* TargetTest =
						PC->GetWorld()->SpawnActor<AChaosImpactTrainingTarget>(
							AChaosImpactTrainingTarget::StaticClass(),
							PC->GetPawn()->GetActorLocation() + FVector::UpVector * 1200.0f,
							FRotator::ZeroRotator, TargetTestParameters))
					{
						FDamageEvent TargetHitEvent;
						Test->TestEqual(TEXT("Training target accepts a ball-sized hit"),
							TargetTest->TakeDamage(1.0f, TargetHitEvent, PC, PC->GetPawn()), 1.0f);
						Test->TestTrue(TEXT("Training target enters defeated state"), TargetTest->IsDefeated());
						Test->TestEqual(TEXT("Defeated target cannot score twice"),
							TargetTest->TakeDamage(1.0f, TargetHitEvent, PC, PC->GetPawn()), 0.0f);
						TargetTest->Destroy();
					}
				}
				Capture(TEXT("05-Training.png"));
				PC->InputKey(FInputKeyEventArgs(nullptr, INPUTDEVICEID_NONE, EKeys::LeftMouseButton, IE_Pressed, FPlatformTime::Cycles64()));
				if (auto* Pawn = Cast<AChaosImpactCharacter>(PC->GetPawn()))
				{
					Test->TestTrue(TEXT("Mouse press immediately starts charge"),
						Pawn->GetThrowChargeAlpha() >= 0.0f && Pawn->GetCarriedBallCount() == 2);
				}
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
				Test->TestTrue(TEXT("Pause menu switches from default arc to straight"),
					PC->GetBallFlightMode() == EChaosImpactBallFlightMode::Straight);
				MenuKey(EKeys::Down);
				MenuKey(EKeys::Enter);
				if (!Check(EChaosImpactScreen::TrainingSettings,
					TEXT("Pause opens training settings"))) { return true; }
				MenuKey(EKeys::Enter);
				Test->TestEqual(TEXT("Training player count cycles"),
					PC->GetRequestedLocalPlayerCount(), 2);
				MenuKey(EKeys::Enter);
				MenuKey(EKeys::Enter);
				MenuKey(EKeys::Enter);
				Test->TestEqual(TEXT("Training player count wraps to one"),
					PC->GetRequestedLocalPlayerCount(), 1);
				MenuKey(EKeys::Down);
				MenuKey(EKeys::Enter);
				Test->TestFalse(TEXT("Training targets can be disabled"), PC->AreTrainingTargetsEnabled());
				MenuKey(EKeys::Enter);
				Test->TestTrue(TEXT("Training targets can be restored"), PC->AreTrainingTargetsEnabled());
				MenuKey(EKeys::Down);
				MenuKey(EKeys::Enter);
				Test->TestTrue(TEXT("Training CPU can be enabled"), PC->IsTrainingCPUEnabled());
				MenuKey(EKeys::Enter);
				Test->TestFalse(TEXT("Training CPU can be disabled"), PC->IsTrainingCPUEnabled());
				MenuKey(EKeys::P);
				if (!Check(EChaosImpactScreen::Pause,
					TEXT("Training settings return to pause"))) { return true; }
				MenuKey(EKeys::Down);
				MenuKey(EKeys::Down);
				MenuKey(EKeys::Down);
				MenuKey(EKeys::Enter);
				TravelStartedAt = Now;
				break;
			case 17:
				if (!PC->IsGameplayActive() && Now - TravelStartedAt < 45) { return false; }
				if (!Check(EChaosImpactScreen::Playing, TEXT("Training retry reloads the stage"))) { return true; }
				Test->TestTrue(TEXT("Retry preserves selected straight trajectory"),
					PC->GetBallFlightMode() == EChaosImpactBallFlightMode::Straight);
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
				MenuKey(EKeys::Down);
				MenuKey(EKeys::Enter);
				if (!Check(EChaosImpactScreen::ModeSelect, TEXT("Pause modes button returns to selection"))) { return true; }
				Test->TestTrue(TEXT("Gameplay stays frozen in mode selection"), PC->IsPaused());
				MenuKey(EKeys::Right);
				MenuKey(EKeys::Down);
				Test->TestEqual(TEXT("Spatial arrows reach lower-right Training"), Menu->GetSelectedIndex(), 2);
				MenuKey(EKeys::Enter);
				if (!Check(EChaosImpactScreen::TrainingSetup,
					TEXT("Training selection reopens player setup"))) { return true; }
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

namespace
{
	class FLocalMultiplayerCommand : public IAutomationLatentCommand
	{
	public:
		explicit FLocalMultiplayerCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			AChaosImpactPlayerController* Primary = FindGameController();
			if (!Primary || !Primary->GetWorld())
			{
				return FPlatformTime::Seconds() - StartedAt > 30.0;
			}
			if (!Primary->GetWorld()->URL.HasOption(TEXT("CITraining=1")))
			{
				Test->AddInfo(TEXT("Local multiplayer test requires a CITraining map URL."));
				return true;
			}
			if (Stage == 1)
			{
				if (FPlatformTime::Seconds() < NextAt)
				{
					return false;
				}
				Test->TestTrue(TEXT("Primary pause freezes the complete split-screen game"),
					Primary->GetCurrentScreen() == EChaosImpactScreen::Pause && Primary->IsPaused());
				if (UChaosImpactMenuWidget* Menu = Primary->GetMenuWidget();
					Menu && GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
				{
					const FVector2D MenuSize = Menu->GetCachedGeometry().GetLocalSize();
					const FIntPoint ViewportSize = GEngine->GameViewport->Viewport->GetSizeXY();
					Test->TestTrue(TEXT("Pause menu covers the entire split-screen viewport"),
						MenuSize.X >= ViewportSize.X * 0.95f && MenuSize.Y >= ViewportSize.Y * 0.95f);
				}
				Capture(TEXT("09-Training-Split-Pause.png"));
				return true;
			}

			UGameInstance* GameInstance = Primary->GetGameInstance();
			const int32 ExpectedPlayers = Primary->GetRequestedLocalPlayerCount();
			if (!GameInstance || GameInstance->GetNumLocalPlayers() < ExpectedPlayers)
			{
				if (FPlatformTime::Seconds() - StartedAt < 30.0) { return false; }
				Test->AddError(TEXT("Timed out while creating local players."));
				return true;
			}

			Test->TestEqual(TEXT("Requested local player count is created"),
				GameInstance->GetNumLocalPlayers(), ExpectedPlayers);
			for (int32 PlayerIndex = 0; PlayerIndex < ExpectedPlayers; ++PlayerIndex)
			{
				ULocalPlayer* LocalPlayer = GameInstance->GetLocalPlayers()[PlayerIndex];
				AChaosImpactPlayerController* Controller = LocalPlayer
					? Cast<AChaosImpactPlayerController>(LocalPlayer->GetPlayerController(Primary->GetWorld()))
					: nullptr;
				Test->TestNotNull(*FString::Printf(TEXT("Player %d controller exists"), PlayerIndex + 1), Controller);
				Test->TestNotNull(*FString::Printf(TEXT("Player %d uses the Chaos Impact character"), PlayerIndex + 1),
					Controller ? Cast<AChaosImpactCharacter>(Controller->GetPawn()) : nullptr);
				if (Controller)
				{
					Test->TestTrue(*FString::Printf(TEXT("Player %d is in gameplay"), PlayerIndex + 1),
						Controller->IsGameplayActive());
				}
			}

			const bool bExpectTargets = !Primary->GetWorld()->URL.HasOption(TEXT("CITargets=0"));
			int32 TargetCount = 0;
			for (TActorIterator<AChaosImpactTrainingTarget> It(Primary->GetWorld()); It; ++It)
			{
				++TargetCount;
			}
			Test->TestTrue(TEXT("Training target setting is applied"),
				bExpectTargets ? TargetCount > 0 : TargetCount == 0);

			const bool bExpectCPU = Primary->GetWorld()->URL.HasOption(TEXT("CICPU=1"));
			int32 CPUCount = 0;
			for (TActorIterator<AChaosImpactCPUController> It(Primary->GetWorld()); It; ++It)
			{
				++CPUCount;
				Test->TestNotNull(TEXT("CPU possesses the same playable character base"),
					Cast<AChaosImpactCharacter>(It->GetPawn()));
				Test->TestEqual(TEXT("CPU uses the player's current flight mode"),
					It->UsesArcFlightMode(),
					Primary->GetBallFlightMode() == EChaosImpactBallFlightMode::Arc);
			}
			Test->TestEqual(TEXT("Training CPU setting is applied"), CPUCount, bExpectCPU ? 1 : 0);

			if (GEngine && GEngine->GameViewport)
			{
				if (ExpectedPlayers == 2)
				{
					Test->TestEqual(TEXT("Two-player training uses a vertical divider"),
						GEngine->GameViewport->GetCurrentSplitscreenConfiguration(),
						ESplitScreenType::TwoPlayer_Vertical);
				}
				else if (ExpectedPlayers == 4)
				{
					Test->TestEqual(TEXT("Four-player training uses a grid"),
						GEngine->GameViewport->GetCurrentSplitscreenConfiguration(),
						ESplitScreenType::FourPlayer_Grid);
				}
			}
			Capture(ExpectedPlayers == 2 ? TEXT("07-Training-2P.png")
				: ExpectedPlayers == 4 ? TEXT("08-Training-4P.png") : TEXT("07-Training-Local.png"));
			Primary->TogglePauseMenu();
			Stage = 1;
			NextAt = FPlatformTime::Seconds() + 0.6;
			return false;
		}

	private:
		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Stage = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactLocalMultiplayerTest,
	"ChaosImpact.Training.LocalMultiplayer",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactLocalMultiplayerTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FLocalMultiplayerCommand(this));
	return true;
}

#endif
