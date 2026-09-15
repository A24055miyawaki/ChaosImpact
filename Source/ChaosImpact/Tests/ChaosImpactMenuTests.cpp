#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactPlayerController.h"
#include "ChaosImpactMenuWidget.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactBallSpawner.h"
#include "ChaosImpactTrainingArena.h"
#include "ChaosImpactTrainingTarget.h"
#include "JoyShockBlueprintLibrary.h"
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

	/** UserIndex: the Slate user the pad belongs to. Real second and third pads arrive as users 1 and 2. */
	void MenuDeviceKey(FKey Key, const int32 InputDeviceId, const int32 UserIndex = 0)
	{
		const FInputDeviceId DeviceId = FInputDeviceId::CreateFromInternalId(InputDeviceId);
		FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(
			Key, FModifierKeysState(), DeviceId, false, 0, 0, TOptional<int32>(UserIndex)));
		FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(
			Key, FModifierKeysState(), DeviceId, false, 0, 0, TOptional<int32>(UserIndex)));
	}

	void PauseKey(AChaosImpactPlayerController* PC)
	{
		// Invoke the same bound action directly; synthetic INPUTDEVICEID_NONE events
		// are intentionally rejected when physical-user filtering is enabled.
		PC->TogglePauseMenu();
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
					PC->ShowMenuScreen(EChaosImpactScreen::ModeSelect);
					Menu->Navigate(EKeys::Gamepad_DPad_Right);
					Test->TestEqual(TEXT("D-pad selects VS"), Menu->GetSelectedIndex(), 1);
					Menu->ConfirmSelection();
				if (!Check(EChaosImpactScreen::VSSelect, TEXT("Gamepad A opens VS mode"))) { return true; }
				break;
			case 4:
				Capture(TEXT("03-VS.png"));
				Menu->Navigate(EKeys::Gamepad_DPad_Right);
				Test->TestEqual(TEXT("D-pad selects online VS"), Menu->GetSelectedIndex(), 1);
				Menu->ConfirmSelection();
				if (!Check(EChaosImpactScreen::OnlinePlayers, TEXT("Online VS asks for the player count"))) { return true; }
				Test->TestTrue(TEXT("Online VS flow is remembered"), PC->GetPlayFlow() == EChaosImpactPlayFlow::VersusOnline);
				break;
			case 5:
					Menu->GoBack();
				if (!Check(EChaosImpactScreen::VSSelect, TEXT("Back from player count returns to VS mode"))) { return true; }
					Menu->GoBack();
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
					Menu->Navigate(EKeys::Gamepad_DPad_Right);
					Test->TestEqual(TEXT("D-pad selects two-player training"), Menu->GetSelectedIndex(), 1);
					Menu->Navigate(EKeys::Gamepad_DPad_Left);
				Test->TestEqual(TEXT("D-pad returns to one-player training"), Menu->GetSelectedIndex(), 0);
				MenuKey(EKeys::Enter);
				if (!Check(EChaosImpactScreen::ControllerAssignment,
					TEXT("Player count opens controller assignment"))) { return true; }
					Test->TestTrue(TEXT("Assignment screen names the first player's device"),
						!PC->GetLocalInputAssignmentForPlayer(0).IsEmpty());
					// The D-pad presses above made the controller P1's device (the last device used picks it);
					// start the checks below from keyboard P1.
					if (PC->WillPrimaryUseGamepad())
					{
						PC->TogglePrimaryInputMode();
					}
					PC->TogglePrimaryInputMode();
					Test->TestFalse(TEXT("A controller player waits for a join press"),
						PC->AreControllerAssignmentsComplete());
					MenuDeviceKey(EKeys::Gamepad_FaceButton_Top, 77);
					Test->TestTrue(TEXT("The assignment UI accepts a physical controller button"),
						PC->IsControllerJoined(77));
					Test->TestTrue(TEXT("The join press completes the requested controller roster"),
						PC->AreControllerAssignmentsComplete());
					PC->TogglePrimaryInputMode();
					Test->TestTrue(TEXT("Keyboard P1 is ready without a controller"),
						PC->AreControllerAssignmentsComplete());
					MenuKey(EKeys::Enter);
				TravelStartedAt = Now;
				break;
			}
			case 9:
				if (!PC->IsGameplayActive() && Now - TravelStartedAt < 45) { return false; }
				if (!Check(EChaosImpactScreen::Playing, TEXT("Mouse click loads training, bypassing title"))) { return true; }
				{
					int32 ReadyPickupBallCount = 0;
					for (TActorIterator<AChaosImpactBallSpawner> It(PC->GetWorld()); It; ++It)
					{
						ReadyPickupBallCount += IsValid(It->GetActiveBall()) ? 1 : 0;
					}
					if (ReadyPickupBallCount < 3 && Now - TravelStartedAt < 45)
					{
						return false;
					}
				}
				Test->TestFalse(TEXT("Training is unpaused"), PC->IsPaused());
				Test->TestTrue(TEXT("Training has a playable character"), IsValid(Cast<AChaosImpactCharacter>(PC->GetPawn())));
				if (AChaosImpactCharacter* Character = Cast<AChaosImpactCharacter>(PC->GetPawn()))
				{
					Test->TestEqual(TEXT("Training movement speed uses the slower tuning"),
						Character->GetCharacterMovement()->MaxWalkSpeed, 560.0f);
					Test->TestEqual(TEXT("Short dash uses the reduced range"),
						Character->GetDashDistance(), 220.0f);
					Test->TestEqual(TEXT("Player respawn waits three seconds"),
						Character->GetEliminationResetDelay(), 3.0f);
				}
				Test->TestTrue(TEXT("Training URL is recognized"), PC->IsTrainingMode());
				Test->TestTrue(TEXT("Arc trajectory is the training default"),
					PC->GetBallFlightMode() == EChaosImpactBallFlightMode::Arc);
				Test->TestEqual(TEXT("One-player selection is preserved"),
					PC->GetRequestedLocalPlayerCount(), 1);
				{
					int32 ArenaCount = 0;
					for (TActorIterator<AChaosImpactTrainingArena> It(PC->GetWorld()); It; ++It)
					{
						++ArenaCount;
						Test->TestTrue(TEXT("Training arena has rebound walls"), It->GetWallCount() >= 7);
						Test->TestTrue(TEXT("Training arena has climbable steps"), It->GetStepCount() >= 3);
					}
					Test->TestEqual(TEXT("Training creates one dedicated arena"), ArenaCount, 1);
				}
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
					Test->TestEqual(TEXT("Large training arena creates seven ball spawners"), SpawnerCount, 7);
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
					Test->TestEqual(TEXT("Large training arena creates eight sandbag targets"), TargetCount, 8);
					Test->TestEqual(TEXT("Training has three stationary targets"), StationaryTargets, 3);
					Test->TestEqual(TEXT("Training has three side-moving targets"), SideTargets, 3);
					Test->TestEqual(TEXT("Training has two depth-moving targets"), ForwardTargets, 2);
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
					Test->TestTrue(TEXT("Throw release waits for the animation hand cue"),
						Pawn->IsThrowReleasePending());
					Test->TestTrue(TEXT("Throw release starts the character motion"),
						Pawn->IsThrowAnimationPlaying());
					bool bProjectileFollowsHand = false;
					for (TActorIterator<AChaosImpactBall> It(PC->GetWorld()); It; ++It)
					{
						if (It->WasThrownBy(Pawn) && It->GetAttachParentActor() == Pawn)
						{
							bProjectileFollowsHand = true;
							break;
						}
					}
					Test->TestTrue(TEXT("The real projectile follows the throwing hand before launch"),
						bProjectileFollowsHand);
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
			{
				MenuKey(EKeys::Down);
				MenuKey(EKeys::Enter);
				if (!Check(EChaosImpactScreen::Pause, TEXT("Trajectory toggle keeps pause open"))) { return true; }
				Test->TestTrue(TEXT("Pause menu switches from default arc to straight"),
					PC->GetBallFlightMode() == EChaosImpactBallFlightMode::Straight);
				MenuKey(EKeys::Down);
				MenuKey(EKeys::Enter);
				if (!Check(EChaosImpactScreen::TrainingOverlay,
					TEXT("Pause opens the live training overlay"))) { return true; }
				Test->TestFalse(TEXT("Live training overlay does not pause world time"), PC->IsPaused());
				if (AChaosImpactCharacter* Pawn = Cast<AChaosImpactCharacter>(PC->GetPawn()))
				{
					Test->TestTrue(TEXT("P1 movement is frozen while live settings are open"),
						Pawn->IsTrainingMenuFrozen());
					Test->TestFalse(TEXT("Character animation playback remains active"),
						Pawn->GetMesh()->bPauseAnims);
				}
				Test->TestTrue(TEXT("Live settings temporarily use only P1's full viewport"),
					GEngine && GEngine->GameViewport
					&& GEngine->GameViewport->IsSplitscreenForceDisabled());

				PC->CycleTrainingPlayerCount();
				Test->TestEqual(TEXT("Player count changes immediately without travel"),
					PC->GetGameInstance()->GetLocalPlayers().Num(), 2);
				PC->CycleTrainingPlayerCount();
				if (UJoyShockLibrary::JSL4UGetAllConnectedControllers().Num() >= 2)
				{
					const TArray<ULocalPlayer*>& LocalPlayers =
						PC->GetGameInstance()->GetLocalPlayers();
					APlayerController* PlayerTwo = LocalPlayers.IsValidIndex(1)
						? LocalPlayers[1]->GetPlayerController(PC->GetWorld()) : nullptr;
					APlayerController* PlayerThree = LocalPlayers.IsValidIndex(2)
						? LocalPlayers[2]->GetPlayerController(PC->GetWorld()) : nullptr;
					const TArray<FJSL4UControllerInfo> PlayerTwoPads =
						UJoyShockLibrary::JSL4UGetControllersAssignedToPlayer(PlayerTwo);
					const TArray<FJSL4UControllerInfo> PlayerThreePads =
						UJoyShockLibrary::JSL4UGetControllersAssignedToPlayer(PlayerThree);
					Test->TestTrue(TEXT("Live 3P gives P2 a physical controller"),
						!PlayerTwoPads.IsEmpty());
					Test->TestTrue(TEXT("Live 3P gives P3 the second physical controller"),
						!PlayerThreePads.IsEmpty());
					if (!PlayerTwoPads.IsEmpty() && !PlayerThreePads.IsEmpty())
					{
						Test->TestNotEqual(TEXT("P2 and P3 own different input devices"),
							PlayerTwoPads[0].InputDeviceId, PlayerThreePads[0].InputDeviceId);
					}
				}
				PC->CycleTrainingPlayerCount();
				PC->CycleTrainingPlayerCount();
				Test->TestEqual(TEXT("Live player count wraps back to one"),
					PC->GetGameInstance()->GetLocalPlayers().Num(), 1);

				PC->ToggleTrainingTargets();
				Test->TestFalse(TEXT("Training targets toggle off immediately"),
					PC->AreTrainingTargetsEnabled());
				for (TActorIterator<AChaosImpactTrainingTarget> It(PC->GetWorld()); It; ++It)
				{
					Test->TestTrue(TEXT("Disabled training target is hidden in the live world"), It->IsHidden());
				}
				PC->ToggleTrainingTargets();
				Test->TestTrue(TEXT("Training targets restore immediately"),
					PC->AreTrainingTargetsEnabled());

				PC->ToggleTrainingCPU();
				int32 LiveCPUCount = 0;
				for (TActorIterator<AChaosImpactCPUController> It(PC->GetWorld()); It; ++It)
				{
					++LiveCPUCount;
					if (AChaosImpactCharacter* CPU = Cast<AChaosImpactCharacter>(It->GetPawn()))
					{
						Test->TestTrue(TEXT("New live CPU remains frozen until settings close"),
							CPU->IsTrainingMenuFrozen());
					}
				}
				Test->TestEqual(TEXT("CPU is spawned immediately"), LiveCPUCount, 1);
				PC->ToggleTrainingCPU();
				PC->ToggleTrainingCPU();
				PC->ToggleTrainingCPU();
				PC->ToggleTrainingCPU();
				Test->TestFalse(TEXT("CPU cycle removes every CPU immediately"), PC->IsTrainingCPUEnabled());

				MenuKey(EKeys::T);
				if (!Check(EChaosImpactScreen::Playing,
					TEXT("Keyboard T closes live settings"))) { return true; }
				Test->TestFalse(TEXT("Closing live settings restores split-screen policy"),
					GEngine && GEngine->GameViewport
					&& GEngine->GameViewport->IsSplitscreenForceDisabled());
				PC->ToggleTrainingOverlay();
				if (!Check(EChaosImpactScreen::TrainingOverlay,
					TEXT("Keyboard T opens live settings directly"))) { return true; }
				PausedWorldTime = PC->GetWorld()->GetTimeSeconds();
				break;
			}
			case 17:
				Test->TestTrue(TEXT("World time advances while live training settings are open"),
					PC->GetWorld()->GetTimeSeconds() > PausedWorldTime + 0.25f);
				Capture(TEXT("06-Live-Training-Overlay.png"));
				break;
			case 18:
				// Flight, players, targets, CPU, summon type, summon ball, then reset.
				for (int32 Press = 0; Press < 6; ++Press)
				{
					MenuKey(EKeys::Down);
				}
				MenuKey(EKeys::Enter);
				TravelStartedAt = Now;
				break;
			case 19:
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
			case 20:
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
				if (!Check(EChaosImpactScreen::ControllerAssignment,
					TEXT("Restart also shows controller assignment"))) { return true; }
				MenuKey(EKeys::Enter);
				TravelStartedAt = Now;
				break;
			case 21:
				if (!PC->IsGameplayActive() && Now - TravelStartedAt < 45) { return false; }
				if (!Check(EChaosImpactScreen::Playing, TEXT("Training can restart from menu"))) { return true; }
				if (auto* Pawn = Cast<AChaosImpactCharacter>(PC->GetPawn()))
				{
					Test->TestEqual(TEXT("Restart resets stamina"), Pawn->GetStamina(), 5.0f);
				}
				PauseKey(PC);
				break;
			case 22:
				MenuKey(EKeys::Down);
				MenuKey(EKeys::Down);
				MenuKey(EKeys::Down);
				MenuKey(EKeys::Down);
				MenuKey(EKeys::Down);
				MenuKey(EKeys::Enter);
				if (!Check(EChaosImpactScreen::Title, TEXT("Pause title button returns to title"))) { return true; }
				break;
			case 23:
				// Regression: the title shown inside a training world (after leaving training, an online
				// room or a room search) must still lead VS online to the room screen, not back to training.
				Test->TestTrue(TEXT("Title is being shown inside the training world"), PC->IsTrainingMode());
				PC->ShowMenuScreen(EChaosImpactScreen::ModeSelect);
				PC->BeginVersusOnline();
				if (!Check(EChaosImpactScreen::OnlinePlayers, TEXT("VS online opens player count from training world"))) { return true; }
				PC->PrepareTrainingControllerAssignment(1);
				if (!Check(EChaosImpactScreen::ControllerAssignment, TEXT("Online player count opens assignment"))) { return true; }
				Test->TestTrue(TEXT("Keyboard P1 is ready for online"), PC->AreControllerAssignmentsComplete());
				PC->ConfirmControllerAssignments();
				if (!Check(EChaosImpactScreen::MultiReady, TEXT("Online assignment leads to the room screen, not training"))) { return true; }
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactControllerAimAxesTest,
	"ChaosImpact.Input.ControllerAimAxes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::EngineFilter)

bool FChaosImpactControllerAimAxesTest::RunTest(const FString& Parameters)
{
	const FVector2D PhysicalRight =
		AChaosImpactCharacter::ConvertRawControllerAimAxes(FVector2D(1.0f, 0.0f));
	const FVector2D PhysicalUp =
		AChaosImpactCharacter::ConvertRawControllerAimAxes(FVector2D(0.0f, -1.0f));
	TestTrue(TEXT("Physical stick right becomes screen right"), PhysicalRight.X > 0.99f);
	TestTrue(TEXT("Physical stick up becomes screen up"), PhysicalUp.Y > 0.99f);
	TestTrue(TEXT("Up-to-right stick motion remains clockwise after conversion"),
		PhysicalUp.X * PhysicalRight.Y - PhysicalUp.Y * PhysicalRight.X < -0.99f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactMenuFlowTest, "ChaosImpact.UI.MenuFlow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::EngineFilter)

bool FChaosImpactMenuFlowTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FMenuFlowCommand(this));
	return true;
}

namespace
{
	class FControllerJoinUICommand : public IAutomationLatentCommand
	{
	public:
		explicit FControllerJoinUICommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			AChaosImpactPlayerController* PC = FindGameController();
			if (!PC || !PC->GetMenuWidget())
			{
				if (FPlatformTime::Seconds() - StartedAt < 30.0)
				{
					return false;
				}
				Test->AddError(TEXT("No menu/controller was created for controller join UI test."));
				return true;
			}

			PC->ShowMenuScreen(EChaosImpactScreen::ModeSelect);
			PC->ShowMenuScreen(EChaosImpactScreen::TrainingSetup);
			PC->PrepareTrainingControllerAssignment(2);
			Test->TestEqual(TEXT("Two-player setup opens the join screen"),
				PC->GetCurrentScreen(), EChaosImpactScreen::ControllerAssignment);
			Test->TestFalse(TEXT("Two-player keyboard setup initially waits for P2"),
				PC->AreControllerAssignmentsComplete());

			MenuDeviceKey(EKeys::Gamepad_FaceButton_Top, 77);
			Test->TestTrue(TEXT("A Slate UI-only gamepad event registers its physical device"),
				PC->IsControllerJoined(77));
			Test->TestTrue(TEXT("The UI-only join event completes the two-player roster"),
				PC->AreControllerAssignmentsComplete());
			Test->TestEqual(TEXT("The join press does not accidentally activate a menu entry"),
				PC->GetCurrentScreen(), EChaosImpactScreen::ControllerAssignment);

			PC->ShowMenuScreen(EChaosImpactScreen::TrainingSetup);
			PC->TogglePrimaryInputMode();
			PC->PrepareTrainingControllerAssignment(3);
			MenuDeviceKey(EKeys::Gamepad_FaceButton_Top, 88);
			// The second pad is a separate Slate user whose keys never reach the menu's focus.
			MenuDeviceKey(EKeys::Gamepad_FaceButton_Top, 99, 1);
			Test->TestTrue(TEXT("A second controller on another Slate user can join"), PC->IsControllerJoined(99));
			MenuKey(EKeys::SpaceBar);
			Test->TestTrue(TEXT("Keyboard and mouse can join after two controllers"),
				PC->IsKeyboardMouseAssignedToPlayer(2));
			Test->TestTrue(TEXT("Controller, controller, keyboard fills a three-player roster"),
				PC->AreControllerAssignmentsComplete());
			Test->TestTrue(TEXT("P1 remains controller-only when keyboard joins as P3"),
				PC->WillPrimaryUseGamepad());

			PC->ShowMenuScreen(EChaosImpactScreen::TrainingSetup);
			PC->PrepareTrainingControllerAssignment(1);
			MenuDeviceKey(EKeys::Gamepad_FaceButton_Top, 111);
			Test->TestTrue(TEXT("Controller-only P1 joins from the first button press"),
				PC->IsControllerJoined(111) && PC->AreControllerAssignmentsComplete());
			MenuDeviceKey(EKeys::Gamepad_DPad_Right, 111);
			Test->TestEqual(TEXT("After READY, P1 controller can navigate the join menu"),
				PC->GetMenuWidget()->GetSelectedIndex(), 1);
			return true;
		}

	private:
		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactControllerJoinUITest,
	"ChaosImpact.UI.ControllerJoin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::EngineFilter)

bool FChaosImpactControllerJoinUITest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FControllerJoinUICommand(this));
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
			if (Stage == 10)
			{
				if (FPlatformTime::Seconds() < NextAt)
				{
					return false;
				}
				AChaosImpactCharacter* Victim = RespawningVictim.Get();
				AChaosImpactCharacter* Killer = SpectatedKiller.Get();
				Test->TestTrue(TEXT("Secondary player's private aim guide is visible before a hit"),
					Killer && Killer->IsPersonalAimGuideVisible());
				Test->TestTrue(TEXT("Right-stick right produces a rightward world aim"),
					Killer && Killer->GetAimDirection().Y > 0.8f);
				Capture(TEXT("07-Training-2P-Private-Aim.png"));
				FDamageEvent EliminationEvent;
				AController* KillerController = Killer ? Killer->GetController() : nullptr;
				Test->TestTrue(TEXT("Opponent damage eliminates the primary player"),
					Victim && KillerController
					&& Victim->TakeDamage(999.0f, EliminationEvent, KillerController, Killer) > 0.0f
					&& Victim->IsEliminated());
				Test->TestEqual(TEXT("Elimination briefly keeps the victim's own camera"),
					Primary->GetViewTarget(), static_cast<AActor*>(Victim));
				Stage = 1;
				NextAt = FPlatformTime::Seconds() + 1.0;
				return false;
			}
			if (Stage == 1)
			{
				if (FPlatformTime::Seconds() < NextAt)
				{
					return false;
				}
				AChaosImpactCharacter* Victim = RespawningVictim.Get();
				AChaosImpactCharacter* Killer = SpectatedKiller.Get();
				Test->TestTrue(TEXT("Eliminated player remains out during the countdown"),
					Victim && Victim->IsEliminated());
				Test->TestEqual(TEXT("Victim camera follows the eliminating player"),
					Primary->GetViewTarget(), static_cast<AActor*>(Killer));
				Test->TestTrue(TEXT("Secondary player has a private aim guide in their HUD"),
					Killer && Killer->IsPersonalAimGuideVisible());
				Test->TestFalse(TEXT("Eliminated player's HUD never exposes an opponent guide"),
					Victim && Victim->IsPersonalAimGuideVisible());
				Capture(TEXT("08-Training-Respawn-Countdown.png"));
				Stage = 2;
				NextAt = FPlatformTime::Seconds() + 3.1;
				return false;
			}
			if (Stage == 2)
			{
				if (FPlatformTime::Seconds() < NextAt)
				{
					return false;
				}
				AChaosImpactCharacter* Victim = RespawningVictim.Get();
				Test->TestTrue(TEXT("Player automatically respawns after about three seconds"),
					Victim && !Victim->IsEliminated());
				Test->TestEqual(TEXT("Respawn restores the player's own camera"),
					Primary->GetViewTarget(), static_cast<AActor*>(Victim));
				Test->TestEqual(TEXT("Respawn clears carried-ball inventory"),
					Victim ? Victim->GetCarriedBallCount() : -1, 0);
				Test->TestFalse(TEXT("Respawn does not reveal phantom hand balls"),
					Victim && Victim->HasVisibleHeldBall());
				if (AChaosImpactCharacter* Killer = SpectatedKiller.Get())
				{
					Killer->CancelChargingThrow();
				}
				Capture(TEXT("08-Training-Respawned.png"));
				Primary->TogglePauseMenu();
				Stage = 3;
				NextAt = FPlatformTime::Seconds() + 0.6;
				return false;
			}
			if (Stage == 3)
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
			if (Stage == 20)
			{
				if (FPlatformTime::Seconds() < NextAt)
				{
					return false;
				}
				int32 ResponsiveCPUs = 0;
				for (int32 Index = 0; Index < ObservedCPUs.Num(); ++Index)
				{
					if (const AChaosImpactCharacter* CPUCharacter = ObservedCPUs[Index].Get();
						CPUCharacter && CPUStartLocations.IsValidIndex(Index)
						&& FVector::Dist2D(CPUCharacter->GetActorLocation(),
							CPUStartLocations[Index]) > 90.0f)
					{
						++ResponsiveCPUs;
					}
				}
				Test->TestTrue(TEXT("CPUs actively navigate the expanded terrain"),
					ResponsiveCPUs >= FMath::Max(1, ObservedCPUs.Num() - 1));
				Capture(TEXT("10-Training-CPU-Navigation.png"));
				Primary->TogglePauseMenu();
				Stage = 3;
				NextAt = FPlatformTime::Seconds() + 0.6;
				return false;
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
			const TCHAR* KeyboardOption =
				Primary->GetWorld()->URL.GetOption(TEXT("CIKeyboardPlayer="), nullptr);
			const int32 KeyboardPlayerIndex = KeyboardOption
				? FCString::Atoi(KeyboardOption)
				: (Primary->GetWorld()->URL.HasOption(TEXT("CIP1Gamepad=1")) ? INDEX_NONE : 0);
			const bool bExpectedPrimaryGamepad = KeyboardPlayerIndex != 0;
			Test->TestEqual(TEXT("P1 exclusive input mode is restored from travel options"),
				Primary->IsPrimaryUsingGamepad(), bExpectedPrimaryGamepad);
				for (int32 PlayerIndex = 0; PlayerIndex < ExpectedPlayers; ++PlayerIndex)
			{
				ULocalPlayer* LocalPlayer = GameInstance->GetLocalPlayers()[PlayerIndex];
				AChaosImpactPlayerController* Controller = LocalPlayer
					? Cast<AChaosImpactPlayerController>(LocalPlayer->GetPlayerController(Primary->GetWorld()))
					: nullptr;
				Test->TestNotNull(*FString::Printf(TEXT("Player %d controller exists"), PlayerIndex + 1), Controller);
				Test->TestNotNull(*FString::Printf(TEXT("Player %d uses the Chaos Impact character"), PlayerIndex + 1),
					Controller ? Cast<AChaosImpactCharacter>(Controller->GetPawn()) : nullptr);
					Test->TestTrue(*FString::Printf(TEXT("Player %d has a valid input slot"), PlayerIndex + 1),
						LocalPlayer && LocalPlayer->GetControllerId() >= 0);
				if (Controller)
				{
					Test->TestTrue(*FString::Printf(TEXT("Player %d is in gameplay"), PlayerIndex + 1),
						Controller->IsGameplayActive());
					const AChaosImpactCharacter* Character =
						Cast<AChaosImpactCharacter>(Controller->GetPawn());
					Test->AddInfo(FString::Printf(
						TEXT("Player %d idle input: left=(%.3f, %.3f), velocity=%.3f"),
						PlayerIndex + 1,
						Controller->GetInputAnalogKeyState(EKeys::Gamepad_LeftX),
						Controller->GetInputAnalogKeyState(EKeys::Gamepad_LeftY),
						Character ? Character->GetVelocity().Size2D() : -1.0f));
					Test->TestTrue(*FString::Printf(
						TEXT("Player %d has no uncommanded horizontal movement"), PlayerIndex + 1),
						Character && Character->GetVelocity().Size2D() < 8.0f);
					const bool bShouldOwnController = PlayerIndex != KeyboardPlayerIndex;
					const int32 AssignedControllerCount =
						UJoyShockLibrary::JSL4UGetControllersAssignedToPlayer(Controller).Num();
					Test->TestEqual(*FString::Printf(
						TEXT("Player %d controller ownership matches its selected input mode"), PlayerIndex + 1),
						AssignedControllerCount > 0, bShouldOwnController);
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

				const int32 ExpectedCPUCount = FMath::Clamp(FCString::Atoi(
					Primary->GetWorld()->URL.GetOption(TEXT("CICPUCount="),
						Primary->GetWorld()->URL.HasOption(TEXT("CICPU=1")) ? TEXT("1") : TEXT("0"))), 0, 4);
			int32 CPUCount = 0;
			for (TActorIterator<AChaosImpactCPUController> It(Primary->GetWorld()); It; ++It)
			{
				++CPUCount;
				AChaosImpactCharacter* CPUCharacter = Cast<AChaosImpactCharacter>(It->GetPawn());
				Test->TestNotNull(TEXT("CPU possesses the same playable character base"),
					CPUCharacter);
				if (CPUCharacter)
				{
					ObservedCPUs.Add(CPUCharacter);
					CPUStartLocations.Add(CPUCharacter->GetActorLocation());
				}
				Test->TestEqual(TEXT("CPU uses the player's current flight mode"),
					It->UsesArcFlightMode(),
					Primary->GetBallFlightMode() == EChaosImpactBallFlightMode::Arc);
			}
				Test->TestEqual(TEXT("Training CPU count is applied"), CPUCount, ExpectedCPUCount);
			if (ExpectedPlayers == 1 && ExpectedCPUCount > 0)
			{
				Stage = 20;
				NextAt = FPlatformTime::Seconds() + 3.5;
				return false;
			}

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
			if (ExpectedPlayers >= 2)
			{
				ULocalPlayer* KillerLocalPlayer = GameInstance->GetLocalPlayers()[1];
				AChaosImpactPlayerController* KillerController = KillerLocalPlayer
					? Cast<AChaosImpactPlayerController>(
						KillerLocalPlayer->GetPlayerController(Primary->GetWorld())) : nullptr;
				AChaosImpactCharacter* Victim = Cast<AChaosImpactCharacter>(Primary->GetPawn());
				AChaosImpactCharacter* Killer = KillerController
					? Cast<AChaosImpactCharacter>(KillerController->GetPawn()) : nullptr;
				if (Victim && KillerController && Killer)
				{
					for (TActorIterator<AChaosImpactBallSpawner> It(Primary->GetWorld()); It; ++It)
					{
						AChaosImpactBall* Ball = It->GetActiveBall();
						if (Victim->GetCarriedBallCount() < Victim->GetMaximumCarriedBalls()
							&& Victim->TryPickupBall(Ball))
						{
							Ball->Destroy();
						}
						else if (Killer->GetCarriedBallCount() == 0 && Killer->TryPickupBall(Ball))
						{
							Ball->Destroy();
							Killer->BeginThrowInput();
						}
					}
					Test->TestEqual(TEXT("Victim carries two balls before elimination"),
						Victim->GetCarriedBallCount(), 2);
					Test->TestEqual(TEXT("Secondary player can carry a ball for aim-guide testing"),
						Killer->GetCarriedBallCount(), 1);
					Killer->DoLook(1.0f, 0.0f);
					RespawningVictim = Victim;
					SpectatedKiller = Killer;
					Stage = 10;
					NextAt = FPlatformTime::Seconds() + 0.45;
					return false;
				}
				Test->AddError(TEXT("Could not create the two-player elimination test state."));
				return true;
			}
			Primary->TogglePauseMenu();
			Stage = 3;
			NextAt = FPlatformTime::Seconds() + 0.6;
			return false;
		}

	private:
		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Stage = 0;
		TWeakObjectPtr<AChaosImpactCharacter> RespawningVictim;
		TWeakObjectPtr<AChaosImpactCharacter> SpectatedKiller;
		TArray<TWeakObjectPtr<AChaosImpactCharacter>> ObservedCPUs;
		TArray<FVector> CPUStartLocations;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactLocalMultiplayerTest,
	"ChaosImpact.Training.LocalMultiplayer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::EngineFilter)

bool FChaosImpactLocalMultiplayerTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FLocalMultiplayerCommand(this));
	return true;
}

#endif
