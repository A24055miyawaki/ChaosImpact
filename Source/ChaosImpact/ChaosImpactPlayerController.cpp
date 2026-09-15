// Copyright Epic Games, Inc. All Rights Reserved.


#include "ChaosImpactPlayerController.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactGameMode.h"
#include "ChaosImpactBallSpawner.h"
#include "ChaosImpactTrainingArena.h"
#include "ChaosImpactTrainingTarget.h"
#include "ChaosImpactWarpPad.h"
#include "ChaosImpactMenuWidget.h"
#include "ChaosImpactMatchAnnouncerWidget.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactSessionSubsystem.h"
#include "ChaosImpactBallTypes.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerState.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ConfigCacheIni.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/InputSettings.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "Blueprint/UserWidget.h"
#include "ChaosImpact.h"
#include "Widgets/Input/SVirtualJoystick.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"
#include "GameMapsSettings.h"
#include "Components/CapsuleComponent.h"
#include "Engine/ChildConnection.h"
#include "Engine/NetConnection.h"
#include "Framework/Application/SlateApplication.h"
#include "TimerManager.h"

void AChaosImpactPlayerController::BeginPlay()
{
	Super::BeginPlay();
	bTrainingMode = ChaosImpact::IsTrainingWorld(GetWorld());
	// A local VS level carries its rules, so a rematch or rule change keeps them.
	if (GetWorld() && ChaosImpactMatch::ReadOptions(GetWorld()->URL, PendingMatchRules))
	{
		PlayFlow = EChaosImpactPlayFlow::VersusLocal;
		bLocalMatchRulesChosen = true;
	}
	const UGameInstance* OwningGameInstance = GetGameInstance();
	const int32 MachinePlayers = FMath::Max(
		GetWorld() ? FCString::Atoi(GetWorld()->URL.GetOption(TEXT("CILocalPlayers="), TEXT("1"))) : 1,
		OwningGameInstance ? OwningGameInstance->GetLocalPlayers().Num() : 1);
	// A single player online follows whichever device was used last; a pair keeps its assigned devices apart.
	bOnlineAnyInput = MachinePlayers <= 1
		&& (IsOnlineRoom() || (GetWorld() && GetWorld()->URL.HasOption(TEXT("CIOnlineSearch=1"))));
	ActiveKeyboardPlayerIndex = 0;
	if (GetWorld())
	{
		const TCHAR* KeyboardValue = GetWorld()->URL.GetOption(TEXT("CIKeyboardPlayer="), nullptr);
		ActiveKeyboardPlayerIndex = KeyboardValue
			? FCString::Atoi(KeyboardValue)
			: (GetWorld()->URL.HasOption(TEXT("CIP1Gamepad=1")) ? INDEX_NONE : 0);
	}
	bPrimaryUsesGamepad = ActiveKeyboardPlayerIndex != 0;
	bRequestedPrimaryUsesGamepad = bPrimaryUsesGamepad;
	RequestedKeyboardPlayerIndex = ActiveKeyboardPlayerIndex;
	ApplyLocalInputRouting();
	// The viewport outlives levels. A training menu overlay or VS opening that held split screen off in the
	// previous level must not leave this one single-screen: two local players, but only P1's view.
	if (IsLocalController())
	{
		if (UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
			Viewport && Viewport->IsSplitscreenForceDisabled())
		{
			Viewport->SetForceDisableSplitscreen(false);
			UE_LOG(LogChaosImpact, Log, TEXT("Split screen was still held off from the previous level; restored"));
		}
	}
	if (bTrainingMode)
	{
		RequestedLocalPlayerCount = FMath::Clamp(FCString::Atoi(
			GetWorld()->URL.GetOption(TEXT("CILocalPlayers="), TEXT("1"))), 1, 4);
		bTrainingTargetsEnabled = !GetWorld()->URL.HasOption(TEXT("CITargets=0"));
		TrainingCPUCount = FMath::Clamp(FCString::Atoi(
			GetWorld()->URL.GetOption(TEXT("CICPUCount="),
				GetWorld()->URL.HasOption(TEXT("CICPU=1")) ? TEXT("1") : TEXT("0"))), 0, 4);
		const int32 ExpectedPads = RequestedLocalPlayerCount
			- (ActiveKeyboardPlayerIndex != INDEX_NONE ? 1 : 0);
		for (int32 PadIndex = 0; PadIndex < ExpectedPads; ++PadIndex)
		{
			const FString DeviceOption = FString::Printf(TEXT("CIPadDevice%d="), PadIndex);
			const FString ControllerOption = FString::Printf(TEXT("CIPadController%d="), PadIndex);
			const TCHAR* DeviceValue = GetWorld()->URL.GetOption(*DeviceOption, nullptr);
			if (DeviceValue)
			{
				JoinedInputDeviceIds.Add(FCString::Atoi(DeviceValue));
				JoinedLegacyControllerIds.Add(FCString::Atoi(
					GetWorld()->URL.GetOption(*ControllerOption, *FString::FromInt(PadIndex))));
			}
		}
	}
	BallFlightMode = GetWorld() && GetWorld()->URL.HasOption(TEXT("CIBallStraight=1"))
		? EChaosImpactBallFlightMode::Straight : EChaosImpactBallFlightMode::Arc;
	CurrentScreen = bTrainingMode ? EChaosImpactScreen::Playing : EChaosImpactScreen::Title;
	const bool bPrimaryLocalPlayer = IsPrimaryLocalPlayerController();

	bShowMouseCursor = !IsUsingGamepad();
	bEnableClickEvents = bShowMouseCursor;
	bEnableMouseOverEvents = bShowMouseCursor;
	DefaultMouseCursor = EMouseCursor::Crosshairs;

	// only spawn touch controls on local player controllers
	if (ShouldUseTouchControls() && IsLocalPlayerController() && bPrimaryLocalPlayer)
	{
		// spawn the mobile controls widget
		MobileControlsWidget = CreateWidget<UUserWidget>(this, MobileControlsWidgetClass);

		if (MobileControlsWidget)
		{
			// add the controls to the player screen
			MobileControlsWidget->AddToPlayerScreen(0);

		} else {

			UE_LOG(LogChaosImpact, Error, TEXT("Could not spawn mobile controls widget."));

		}

	}

	if (IsLocalPlayerController() && bPrimaryLocalPlayer)
	{
		MenuWidget = CreateWidget<UChaosImpactMenuWidget>(this);
		if (MenuWidget)
		{
			// Menus use the complete game viewport. Gameplay HUDs still use
			// AddToPlayerScreen, so only the pause layer spans split-screen views.
			MenuWidget->AddToViewport(100);
			MenuWidget->ShowScreen(CurrentScreen);
			ApplyScreenInput();
		}
		// Match call-outs span the whole viewport, so split screen shows them once rather than per view.
		MatchAnnouncer = CreateWidget<UChaosImpactMatchAnnouncerWidget>(this);
		if (MatchAnnouncer)
		{
			MatchAnnouncer->AddToViewport(50);
		}
	}

	if (IsLocalController() && IsOnlineRoom() && GetThisLocalPlayerIndex() == 0)
	{
		// The host names this machine's second player "<name>(2)" itself.
		if (const UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this))
		{
			ServerSetPlayerName(Sessions->GetPlayerName());
		}
	}
	if (GetNetMode() == NM_Client && !bOnlineAnyInput)
	{
		ClientAssignmentWaits = 0;
		GetWorldTimerManager().SetTimer(ClientAssignmentTimer, this,
			&AChaosImpactPlayerController::ApplyClientControllerAssignments, 0.2f, true, 0.2f);
	}
	// After being disconnected from a room the engine lands on the title map;
	// continue straight into this player's own training arena instead.
	if (!bTrainingMode && IsLocalController() && bPrimaryLocalPlayer)
	{
		// Development hook for multi-instance testing without driving the menus:
		// -CIAutoRoom=create:1234 or -CIAutoRoom=search:1234, optionally -CIAutoName=Name.
		// -CIShowScreen=MultiReady|OnlineName|OnlinePassword opens a menu page directly for screenshots.
		static bool bShowScreenConsumed = false;
		FString ScreenName;
		if (!bShowScreenConsumed && FParse::Value(FCommandLine::Get(), TEXT("CIShowScreen="), ScreenName))
		{
			bShowScreenConsumed = true;
			const UEnum* ScreenEnum = StaticEnum<EChaosImpactScreen>();
			const int64 Value = ScreenEnum ? ScreenEnum->GetValueByNameString(ScreenName) : INDEX_NONE;
			if (Value != INDEX_NONE)
			{
				ShowMenuScreen(static_cast<EChaosImpactScreen>(Value));
			}
		}
		static bool bAutoRoomConsumed = false;
		FString AutoRoom;
		if (!bAutoRoomConsumed && FParse::Value(FCommandLine::Get(), TEXT("CIAutoRoom="), AutoRoom))
		{
			bAutoRoomConsumed = true;
			FString AutoName;
			if (FParse::Value(FCommandLine::Get(), TEXT("CIAutoName="), AutoName))
			{
				if (UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this))
				{
					Sessions->SetPlayerName(AutoName);
				}
			}
			FString Mode;
			FString Password;
			if (AutoRoom.Split(TEXT(":"), &Mode, &Password))
			{
				// -CIAutoPlayers=2 joins as a split-screen pair (keyboard P1, first pad P2).
				int32 AutoPlayers = 1;
				FParse::Value(FCommandLine::Get(), TEXT("CIAutoPlayers="), AutoPlayers);
				PlayFlow = EChaosImpactPlayFlow::VersusOnline;
				RequestedLocalPlayerCount = FMath::Clamp(AutoPlayers, 1, 2);
				RequestedKeyboardPlayerIndex = 0;
				JoinedInputDeviceIds.Reset();
				JoinedLegacyControllerIds.Reset();
				bPendingCreateRoom = Mode == TEXT("create");
				SubmitRoomPassword(Password);
				if (bPendingCreateRoom)
				{
					// -CIAutoRoomName= names the room; otherwise it gets the default name.
					FString AutoRoomName;
					FParse::Value(FCommandLine::Get(), TEXT("CIAutoRoomName="), AutoRoomName);
					SubmitRoomName(AutoRoomName);
				}
				return;
			}
		}
		if (UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this);
			Sessions && Sessions->ConsumeReturnToTraining())
		{
			// Back to this machine's own training with the same players and controllers as before.
			bTravelPending = true;
			UGameplayStatics::OpenLevel(this, FName(*TrainingLevel.GetLongPackageName()), true,
				Sessions->GetOfflineTrainingOptions());
			return;
		}
	}

	EnsureTrainingArena();
	EnsureTrainingBallSpawners();
	EnsureTrainingWarpPads();
	EnsureTrainingTargets();

	if (bTrainingMode && IsLocalController() && IsPrimaryLocalPlayerController())
	{
		// Ball effects are loaded at game start; show each once now, out of sight below the arena, so the
		// first real fire or ice ball does not stall while pipeline states and GPU resources are created.
		FTimerHandle WarmUpTimer;
		GetWorldTimerManager().SetTimer(WarmUpTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			FVector Below = GetPawn() ? GetPawn()->GetActorLocation() : FVector::ZeroVector;
			Below.Z -= 1500.0f;
			ChaosImpactBallTypes::WarmUpEffects(GetWorld(), Below);
		}), 0.3f, false);
	}
}

bool AChaosImpactPlayerController::IsPrimaryLocalPlayerController() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	const ULocalPlayer* ThisLocalPlayer = GetLocalPlayer();
	if (!ThisLocalPlayer)
	{
		return false;
	}
	return !GameInstance || GameInstance->GetLocalPlayers().IsEmpty()
		|| GameInstance->GetLocalPlayers()[0] == ThisLocalPlayer;
}

int32 AChaosImpactPlayerController::GetThisLocalPlayerIndex() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance && GetLocalPlayer()
		? GameInstance->GetLocalPlayers().IndexOfByKey(GetLocalPlayer()) : 0;
}

bool AChaosImpactPlayerController::IsUsingGamepad() const
{
	if (bOnlineAnyInput)
	{
		return bLastInputGamepad;
	}
	return GetThisLocalPlayerIndex() != ActiveKeyboardPlayerIndex;
}

void AChaosImpactPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	InputComponent->BindKey(EKeys::P, IE_Pressed, this,
		&AChaosImpactPlayerController::TogglePauseMenu).bExecuteWhenPaused = true;
	InputComponent->BindKey(EKeys::Gamepad_Special_Right, IE_Pressed, this,
		&AChaosImpactPlayerController::TogglePauseMenu).bExecuteWhenPaused = true;
	InputComponent->BindKey(EKeys::T, IE_Pressed, this,
		&AChaosImpactPlayerController::ToggleTrainingOverlay);
	InputComponent->BindKey(EKeys::Hyphen, IE_Pressed, this,
		&AChaosImpactPlayerController::ToggleTrainingOverlay);
	// Special_Left maps to Minus on Switch, Create on DualSense and View on Xbox pads.
	InputComponent->BindKey(EKeys::Gamepad_Special_Left, IE_Pressed, this,
		&AChaosImpactPlayerController::ToggleTrainingOverlay);
	// Online lobby 準備OK: keys nothing else uses in play (Y throws, so the D-pad).
	InputComponent->BindKey(EKeys::R, IE_Pressed, this, &AChaosImpactPlayerController::ToggleReadyForMatch);
	InputComponent->BindKey(EKeys::Gamepad_DPad_Up, IE_Pressed, this, &AChaosImpactPlayerController::ToggleReadyForMatch);

	// only add IMCs for local player controllers
	if (IsLocalPlayerController())
	{
		// Add Input Mapping Contexts
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			// Always add the template contexts as fallbacks. This avoids a completely
			// unresponsive pawn when a Blueprint loses its context array assignments.
			if (UInputMappingContext* DefaultContext = LoadObject<UInputMappingContext>(
				nullptr, TEXT("/Game/Input/IMC_Default.IMC_Default")))
			{
				Subsystem->AddMappingContext(DefaultContext, 0);
			}
			if (UInputMappingContext* MouseContext = LoadObject<UInputMappingContext>(
				nullptr, TEXT("/Game/Input/IMC_MouseLook.IMC_MouseLook")))
			{
				Subsystem->AddMappingContext(MouseContext, 0);
			}

			for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
			{
				Subsystem->AddMappingContext(CurrentContext, 0);
			}

			// only add these IMCs if we're not using mobile touch input
			if (!ShouldUseTouchControls())
			{
				for (UInputMappingContext* CurrentContext : MobileExcludedMappingContexts)
				{
					Subsystem->AddMappingContext(CurrentContext, 0);
				}
			}
		}
	}
}

bool AChaosImpactPlayerController::InputKey(const FInputKeyEventArgs& Params)
{
	if (bOnlineAnyInput && IsLocalController())
	{
		const bool bMouseAxis = Params.Key == EKeys::MouseX || Params.Key == EKeys::MouseY;
		const bool bAnalog = Params.Key.IsAnalog();
		const bool bMeaningful = bMouseAxis ? FMath::Abs(Params.AmountDepressed) > 2.0f
			: bAnalog ? FMath::Abs(Params.AmountDepressed) > 0.35f
			: Params.Event == IE_Pressed;
		if (bMeaningful && Params.Key.IsGamepadKey() != bLastInputGamepad)
		{
			bLastInputGamepad = Params.Key.IsGamepadKey();
			bShowMouseCursor = !bLastInputGamepad;
			bEnableClickEvents = bShowMouseCursor;
			bEnableMouseOverEvents = bShowMouseCursor;
		}
	}
	// Team select: each local player picks their own team with their own device; P1 goes through the menu.
	if (IsLocalController() && !IsPrimaryLocalPlayerController())
	{
		const AChaosImpactGameState* Match = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
		if (Match && Match->bVersusMatch && Match->Phase == EChaosImpactOnlinePhase::TeamSelect)
		{
			if (Params.Event == IE_Pressed && IsKeyAllowedForThisPlayer(Params.Key))
			{
				const FKey& Key = Params.Key;
				if (Key == EKeys::Gamepad_DPad_Left || Key == EKeys::Gamepad_LeftStick_Left
					|| Key == EKeys::Left || Key == EKeys::A)
				{
					ChangeOwnTeam(-1);
				}
				else if (Key == EKeys::Gamepad_DPad_Right || Key == EKeys::Gamepad_LeftStick_Right
					|| Key == EKeys::Right || Key == EKeys::D)
				{
					ChangeOwnTeam(1);
				}
			}
			return true;
		}
	}
	// Input mode is deliberately exclusive. In one-player keyboard mode the first
	// connected pad must not also move P1; in pad mode stray keyboard/mouse input
	// must not affect the match. The keyboard/mouse pair may belong to any one
	// local player, based on the order devices joined on the assignment screen.
	if (!IsKeyAllowedForThisPlayer(Params.Key))
	{
		return true;
	}
	const bool bHandledByBase = Super::InputKey(Params);
	if (Params.Key == EKeys::LeftMouseButton && IsGameplayActive())
	{
		if (Params.Event == IE_Pressed)
		{
			HandleThrowPressed();
			return true;
		}
		if (Params.Event == IE_Released)
		{
			HandleThrowReleased();
			return true;
		}
	}
	return bHandledByBase;
}

bool AChaosImpactPlayerController::IsKeyAllowedForThisPlayer(const FKey Key) const
{
	if (!IsLocalPlayerController() || bOnlineAnyInput)
	{
		return true;
	}
	return Key.IsGamepadKey() == IsUsingGamepad();
}

void AChaosImpactPlayerController::ApplyLocalInputRouting()
{
	// Devices are paired explicitly from the join screen. The legacy automatic
	// offset would move them again and is therefore always disabled.
	if (UGameMapsSettings* MapsSettings = GetMutableDefault<UGameMapsSettings>())
	{
		MapsSettings->bOffsetPlayerGamepadIds = false;
	}
	// PIE keeps the editor process alive between runs, so also update the live CDO.
	// Without platform-user filtering, every Enhanced Input local-player subsystem
	// receives every pad and P2 can inherit P1's held/stale analog state.
	if (UInputSettings* InputSettings = GetMutableDefault<UInputSettings>())
	{
		InputSettings->bFilterInputByPlatformUser = true;
	}
}

FString AChaosImpactPlayerController::GetLocalInputAssignmentText(const int32 PlayerCount) const
{
	const int32 Players = FMath::Clamp(
		PlayerCount == INDEX_NONE ? RequestedLocalPlayerCount : PlayerCount, 1, 4);
	TArray<FString> Assignments;
	Assignments.Reserve(Players);
	for (int32 PlayerIndex = 0; PlayerIndex < Players; ++PlayerIndex)
	{
		if (PlayerIndex == RequestedKeyboardPlayerIndex)
		{
			Assignments.Add(TEXT("1P  キーボード＋マウス"));
		}
		else
		{
			const int32 PadIndex = GetPadIndexForPlayer(PlayerIndex);
			Assignments.Add(FString::Printf(TEXT("%dP  %s"), PlayerIndex + 1,
				JoinedInputDeviceIds.IsValidIndex(PadIndex)
					? TEXT("コントローラー READY") : TEXT("ボタンを押して参加")));
		}
	}
	return FString::Join(Assignments, TEXT("   /   "));
}

FString AChaosImpactPlayerController::GetLocalInputAssignmentForPlayer(const int32 PlayerIndex) const
{
	if (PlayerIndex < 0 || PlayerIndex >= RequestedLocalPlayerCount)
	{
		return FString();
	}
	if (PlayerIndex == RequestedKeyboardPlayerIndex)
	{
		return TEXT("キーボード ＋ マウス");
	}
	const int32 PadIndex = GetPadIndexForPlayer(PlayerIndex);
	return JoinedInputDeviceIds.IsValidIndex(PadIndex)
		? TEXT("コントローラー  READY") : TEXT("何かボタンを押してください");
}

bool AChaosImpactPlayerController::AreControllerAssignmentsComplete() const
{
	return GetAssignedPlayerCount() >= RequestedLocalPlayerCount;
}

bool AChaosImpactPlayerController::IsInputAssignedToPlayer(const int32 PlayerIndex) const
{
	if (PlayerIndex < 0 || PlayerIndex >= RequestedLocalPlayerCount)
	{
		return false;
	}
	if (PlayerIndex == RequestedKeyboardPlayerIndex)
	{
		return true;
	}
	const int32 PadIndex = GetPadIndexForPlayer(PlayerIndex);
	return PadIndex >= 0 && JoinedInputDeviceIds.IsValidIndex(PadIndex);
}

int32 AChaosImpactPlayerController::GetPadIndexForPlayer(const int32 PlayerIndex) const
{
	return PlayerIndex - (RequestedKeyboardPlayerIndex != INDEX_NONE
		&& RequestedKeyboardPlayerIndex < PlayerIndex ? 1 : 0);
}

void AChaosImpactPlayerController::ResetControllerJoinSequence()
{
	JoinedInputDeviceIds.Reset();
	JoinedLegacyControllerIds.Reset();
	RequestedKeyboardPlayerIndex = bRequestedPrimaryUsesGamepad ? INDEX_NONE : 0;
}

bool AChaosImpactPlayerController::RegisterControllerJoin(
	const int32 InputDeviceId, const int32 LegacyControllerId)
{
	if (CurrentScreen != EChaosImpactScreen::ControllerAssignment || InputDeviceId < 0)
	{
		return false;
	}
	if (JoinedInputDeviceIds.Contains(InputDeviceId))
	{
		return true;
	}
	if (GetAssignedPlayerCount() >= RequestedLocalPlayerCount)
	{
		return true;
	}

	JoinedInputDeviceIds.Add(InputDeviceId);
	JoinedLegacyControllerIds.Add(LegacyControllerId);
	if (MenuWidget)
	{
		MenuWidget->RefreshEntries();
	}
	return true;
}

bool AChaosImpactPlayerController::RegisterKeyboardMouseJoin()
{
	if (CurrentScreen != EChaosImpactScreen::ControllerAssignment)
	{
		return false;
	}
	if (RequestedKeyboardPlayerIndex != INDEX_NONE)
	{
		return true;
	}
	if (GetAssignedPlayerCount() >= RequestedLocalPlayerCount)
	{
		return true;
	}

	RequestedKeyboardPlayerIndex = GetAssignedPlayerCount();
	bRequestedPrimaryUsesGamepad = RequestedKeyboardPlayerIndex != 0;
	if (MenuWidget)
	{
		MenuWidget->RefreshEntries();
	}
	return true;
}

void AChaosImpactPlayerController::BuildFallbackControllerAssignments()
{
	const int32 RequiredPads = RequestedLocalPlayerCount
		- (RequestedKeyboardPlayerIndex != INDEX_NONE ? 1 : 0);
	for (int32 PadIndex = JoinedInputDeviceIds.Num(); PadIndex < RequiredPads; ++PadIndex)
	{
		JoinedInputDeviceIds.Add(PadIndex);
		JoinedLegacyControllerIds.Add(PadIndex);
	}
}

void AChaosImpactPlayerController::HandleThrowPressed()
{
	if (IsGameplayActive())
	{
		if (AChaosImpactCharacter* PlayerCharacter = Cast<AChaosImpactCharacter>(GetPawn()))
		{
			PlayerCharacter->BeginThrowInput();
		}
	}
}

void AChaosImpactPlayerController::HandleThrowReleased()
{
	if (IsGameplayActive())
	{
		if (AChaosImpactCharacter* PlayerCharacter = Cast<AChaosImpactCharacter>(GetPawn()))
		{
			PlayerCharacter->EndThrowInput();
		}
	}
}

bool AChaosImpactPlayerController::ShouldUseTouchControls() const
{
	// are we on a mobile platform? Should we force touch?
	return SVirtualJoystick::ShouldDisplayTouchInterface() || bForceTouchControls;
}

void AChaosImpactPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	if (IsLocalPlayerController() && MenuWidget)
	{
		ApplyScreenInput();
	}
	EnsureTrainingArena();
	EnsureTrainingBallSpawners();
	EnsureTrainingWarpPads();
	EnsureTrainingTargets();
}

void AChaosImpactPlayerController::ApplyScreenInput()
{
	const bool bPlaying = IsGameplayActive();
	const bool bLiveTrainingOverlay = CurrentScreen == EChaosImpactScreen::TrainingOverlay;
	if (AChaosImpactCharacter* PlayerCharacter = Cast<AChaosImpactCharacter>(GetPawn()))
	{
		// The rematch menu sits under the results, which stay on screen.
		PlayerCharacter->SetGameplayUIVisible(bPlaying || CurrentScreen == EChaosImpactScreen::MatchEnd);
		if (!bPlaying)
		{
			PlayerCharacter->CancelChargingThrow();
		}
	}
	if (PlayerInput)
	{
		PlayerInput->FlushPressedKeys();
	}
	bShowMouseCursor = !IsUsingGamepad();
	bEnableClickEvents = bShowMouseCursor;
	bEnableMouseOverEvents = bShowMouseCursor;
	DefaultMouseCursor = bPlaying ? EMouseCursor::Crosshairs : EMouseCursor::Default;
	CurrentMouseCursor = DefaultMouseCursor;
	// The training panel deliberately leaves world time and ball physics alive.
	// Pausing an online room would freeze every member, so menus there never pause the world.
	// Team select lets the other local players keep choosing through their own controllers.
	SetPause(!bPlaying && !bLiveTrainingOverlay && !IsOnlineRoom()
		&& CurrentScreen != EChaosImpactScreen::TeamSelect && CurrentScreen != EChaosImpactScreen::MatchEnd);
	if (MobileControlsWidget)
	{
		MobileControlsWidget->SetVisibility(bPlaying ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (bPlaying)
	{
		FInputModeGameOnly GameInputMode;
		// Do not sacrifice the first throw click when the viewport captures the mouse again.
		GameInputMode.SetConsumeCaptureMouseDown(false);
		SetInputMode(GameInputMode);
		// SetInputMode only focuses the viewport for P1's Slate user. Pads owned by
		// P2-P4 are routed through their own Slate users, and a user without viewport
		// focus drops every button/stick event before UGameViewportClient::InputKey.
		// Focusing all users also becomes the default for users created later.
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().SetAllUserFocusToGameViewport();
		}
	}
	else if (MenuWidget)
	{
		FInputModeUIOnly InputMode;
		InputMode.SetWidgetToFocus(MenuWidget->TakeWidget());
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(InputMode);
		MenuWidget->SetUserFocus(this);
	}
}

void AChaosImpactPlayerController::ShowMenuScreen(const EChaosImpactScreen NewScreen)
{
	if (!MenuWidget || bTravelPending)
	{
		return;
	}
	if (NewScreen == EChaosImpactScreen::Playing)
	{
		ResumeGameplay();
		return;
	}
	// A menu over the game (a paused world may not tick) must not leave a held rumble running.
	StopRumble();
	if (NewScreen == EChaosImpactScreen::TrainingOverlay)
	{
		OpenTrainingOverlay();
		return;
	}
	if (bTrainingOverlayPresentationActive)
	{
		ExitTrainingOverlayPresentation();
	}
	// A pause screen is only reachable from an active play session.
	if (NewScreen == EChaosImpactScreen::Pause && !IsGameplayActive()
		&& CurrentScreen != EChaosImpactScreen::TrainingSettings)
	{
		return;
	}
	if (bTrainingMode && IsPrimaryLocalPlayerController()
		&& (NewScreen == EChaosImpactScreen::ModeSelect || NewScreen == EChaosImpactScreen::Title))
	{
		RemoveSecondaryLocalPlayers();
		StopRoomSearch();
	}
	CurrentScreen = NewScreen;
	MenuWidget->ShowScreen(NewScreen);
	ApplyScreenInput();
}

void AChaosImpactPlayerController::TogglePauseMenu()
{
	// Any active local controller may request pause, but the primary controller
	// owns the single full-viewport pause widget and applies the world pause.
	if (!IsPrimaryLocalPlayerController())
	{
		if (UGameInstance* GameInstance = GetGameInstance();
			GameInstance && !GameInstance->GetLocalPlayers().IsEmpty())
		{
			if (ULocalPlayer* PrimaryPlayer = GameInstance->GetLocalPlayers()[0])
			{
				if (AChaosImpactPlayerController* PrimaryController =
					Cast<AChaosImpactPlayerController>(PrimaryPlayer->GetPlayerController(GetWorld())))
				{
					PrimaryController->TogglePauseMenu();
				}
			}
		}
		return;
	}
	if (CurrentScreen == EChaosImpactScreen::TrainingSettings)
	{
		ShowMenuScreen(EChaosImpactScreen::Pause);
		return;
	}
	if (CurrentScreen == EChaosImpactScreen::TrainingOverlay)
	{
		CloseTrainingOverlay();
		ShowMenuScreen(EChaosImpactScreen::Pause);
		return;
	}
	if (CurrentScreen == EChaosImpactScreen::Pause)
	{
		ResumeGameplay();
	}
	else if (IsGameplayActive())
	{
		ShowMenuScreen(EChaosImpactScreen::Pause);
	}
}

void AChaosImpactPlayerController::ResumeGameplay()
{
	if (CurrentScreen != EChaosImpactScreen::Pause || bTravelPending)
	{
		return;
	}
	CurrentScreen = EChaosImpactScreen::Playing;
	MenuWidget->ShowScreen(CurrentScreen);
	ApplyScreenInput();
}

void AChaosImpactPlayerController::ToggleTrainingOverlay()
{
	// Unlike pause, this menu belongs only to P1. Its widget also spans the complete
	// viewport, so it remains a single global prompt/menu during local multiplayer.
	if (!IsPrimaryLocalPlayerController() || !bTrainingMode || bTravelPending || IsOnlineRoom() || IsVersusMatchWorld())
	{
		return;
	}
	if (CurrentScreen == EChaosImpactScreen::TrainingOverlay)
	{
		CloseTrainingOverlay();
	}
	else if (IsGameplayActive())
	{
		OpenTrainingOverlay();
	}
}

void AChaosImpactPlayerController::OpenTrainingOverlay()
{
	if (!IsPrimaryLocalPlayerController() || !bTrainingMode || bTravelPending || !MenuWidget || IsOnlineRoom()
		|| (CurrentScreen != EChaosImpactScreen::Playing
			&& CurrentScreen != EChaosImpactScreen::ControllerAssignment))
	{
		return;
	}

	CurrentScreen = EChaosImpactScreen::TrainingOverlay;
	bTrainingOverlayPresentationActive = true;
	if (UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
	{
		Viewport->SetForceDisableSplitscreen(true);
	}
	SetTrainingCharactersFrozen(true);
	if (AChaosImpactCharacter* PrimaryCharacter = Cast<AChaosImpactCharacter>(GetPawn()))
	{
		PrimaryCharacter->SetTrainingMenuCameraActive(true);
	}
	MenuWidget->ShowScreen(CurrentScreen);
	ApplyScreenInput();
}

void AChaosImpactPlayerController::CloseTrainingOverlay()
{
	if (CurrentScreen != EChaosImpactScreen::TrainingOverlay || bTravelPending)
	{
		return;
	}
	ExitTrainingOverlayPresentation();
	CurrentScreen = EChaosImpactScreen::Playing;
	if (MenuWidget)
	{
		MenuWidget->ShowScreen(CurrentScreen);
	}
	ApplyScreenInput();
}

void AChaosImpactPlayerController::ExitTrainingOverlayPresentation()
{
	if (!bTrainingOverlayPresentationActive)
	{
		return;
	}
	bTrainingOverlayPresentationActive = false;
	SetTrainingCharactersFrozen(false);
	for (TActorIterator<AChaosImpactCharacter> It(GetWorld()); It; ++It)
	{
		It->SetTrainingMenuCameraActive(false);
	}
	if (UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
	{
		Viewport->SetForceDisableSplitscreen(false);
	}
}

void AChaosImpactPlayerController::SetTrainingCharactersFrozen(const bool bFrozen)
{
	if (!GetWorld())
	{
		return;
	}
	for (TActorIterator<AChaosImpactCharacter> It(GetWorld()); It; ++It)
	{
		It->SetTrainingMenuFrozen(bFrozen);
		It->SetGameplayUIVisible(!bFrozen);
	}
}

void AChaosImpactPlayerController::StartTraining()
{
	StartTrainingWithPlayers(1);
}

void AChaosImpactPlayerController::StartTrainingWithPlayers(const int32 LocalPlayerCount)
{
	if (bTravelPending || IsGameplayActive() || CurrentScreen == EChaosImpactScreen::Title)
	{
		return;
	}
	RequestedLocalPlayerCount = FMath::Clamp(LocalPlayerCount, 1, 4);
	BuildFallbackControllerAssignments();
	OpenTrainingLevel(false);
}

void AChaosImpactPlayerController::PrepareTrainingControllerAssignment(const int32 LocalPlayerCount)
{
	if (bTravelPending || IsGameplayActive() || CurrentScreen == EChaosImpactScreen::Title)
	{
		return;
	}
	const bool bOnline = PlayFlow == EChaosImpactPlayFlow::VersusOnline;
	RequestedLocalPlayerCount = FMath::Clamp(LocalPlayerCount, 1, bOnline ? 2 : 4);
	ResetControllerJoinSequence();
	bControllerAssignmentKeepsFlightMode = false;
	ControllerAssignmentReturnScreen = bOnline ? EChaosImpactScreen::OnlinePlayers : EChaosImpactScreen::TrainingSetup;
	ShowMenuScreen(EChaosImpactScreen::ControllerAssignment);
}

void AChaosImpactPlayerController::ConfirmControllerAssignments()
{
	if (CurrentScreen != EChaosImpactScreen::ControllerAssignment || bTravelPending
		|| !AreControllerAssignmentsComplete())
	{
		return;
	}
	// Decide by where the assignment was opened from, not by the current world: after leaving an online
	// room or search, the title menu is shown inside a training world, and VS online must still lead to
	// へやをつくる / へやをさがす there instead of restarting training.
	if (PlayFlow == EChaosImpactPlayFlow::VersusOnline
		&& ControllerAssignmentReturnScreen == EChaosImpactScreen::OnlinePlayers)
	{
		// Matching only for now: the players are set, next comes へやをつくる / へやをさがす.
		ShowMenuScreen(EChaosImpactScreen::MultiReady);
		return;
	}
	if (PlayFlow == EChaosImpactPlayFlow::VersusLocal
		&& ControllerAssignmentReturnScreen == EChaosImpactScreen::TrainingSetup)
	{
		// Players are set; the match rules come next, then the VS level opens.
		OpenMatchRules(EChaosImpactScreen::ControllerAssignment);
		return;
	}
	OpenTrainingLevel(bControllerAssignmentKeepsFlightMode);
}

void AChaosImpactPlayerController::BeginTrainingSetup()
{
	PlayFlow = EChaosImpactPlayFlow::Training;
	bTrainingTargetsEnabled = true;
	ShowMenuScreen(EChaosImpactScreen::TrainingSetup);
}

void AChaosImpactPlayerController::BeginVersusLocal()
{
	// Proper versus rules come later; until then a local VS is the arena without targets or CPUs.
	PlayFlow = EChaosImpactPlayFlow::VersusLocal;
	bTrainingTargetsEnabled = false;
	TrainingCPUCount = 0;
	ShowMenuScreen(EChaosImpactScreen::TrainingSetup);
}

void AChaosImpactPlayerController::BeginVersusOnline()
{
	PlayFlow = EChaosImpactPlayFlow::VersusOnline;
	ShowMenuScreen(EChaosImpactScreen::OnlinePlayers);
}

FString AChaosImpactPlayerController::BuildLocalSetupOptions()
{
	BuildFallbackControllerAssignments();
	FString Options = FString::Printf(TEXT("CILocalPlayers=%d?CIKeyboardPlayer=%d"),
		FMath::Clamp(RequestedLocalPlayerCount, 1, 4), RequestedKeyboardPlayerIndex);
	for (int32 PadIndex = 0; PadIndex < JoinedInputDeviceIds.Num(); ++PadIndex)
	{
		Options += FString::Printf(TEXT("?CIPadDevice%d=%d"), PadIndex, JoinedInputDeviceIds[PadIndex]);
		if (JoinedLegacyControllerIds.IsValidIndex(PadIndex))
		{
			Options += FString::Printf(TEXT("?CIPadController%d=%d"), PadIndex, JoinedLegacyControllerIds[PadIndex]);
		}
	}
	return Options;
}

void AChaosImpactPlayerController::ApplyClientControllerAssignments()
{
	const UGameInstance* GameInstance = GetGameInstance();
	UWorld* World = GetWorld();
	if (!GameInstance || !World || !IsPrimaryLocalPlayerController())
	{
		GetWorldTimerManager().ClearTimer(ClientAssignmentTimer);
		return;
	}
	const int32 Expected = FMath::Clamp(FCString::Atoi(World->URL.GetOption(TEXT("CILocalPlayers="), TEXT("1"))), 1, 4);
	int32 Ready = 0;
	for (const ULocalPlayer* LocalPlayer : GameInstance->GetLocalPlayers())
	{
		Ready += LocalPlayer && LocalPlayer->GetPlayerController(World) ? 1 : 0;
	}
	// The second player's controller arrives a moment after the first; wait up to about five seconds.
	constexpr int32 MaxWaits = 25;
	if (Ready < Expected && ++ClientAssignmentWaits < MaxWaits)
	{
		return;
	}
	GetWorldTimerManager().ClearTimer(ClientAssignmentTimer);
	AChaosImpactGameMode::ApplyLocalControllerAssignments(World, Ready, ActiveKeyboardPlayerIndex);
	UE_LOG(LogChaosImpact, Log, TEXT("Client controller pairing applied for %d of %d local player(s)"), Ready, Expected);
}

void AChaosImpactPlayerController::RetryTraining()
{
	if (!bTrainingMode || bTravelPending)
	{
		return;
	}
	OpenTrainingLevel(true);
}

void AChaosImpactPlayerController::CycleTrainingPlayerCount()
{
	if (bTrainingMode)
	{
		RequestedLocalPlayerCount = RequestedLocalPlayerCount % 4 + 1;
		if (CurrentScreen == EChaosImpactScreen::TrainingOverlay)
		{
			if (AChaosImpactGameMode* GameMode = GetWorld()
				? GetWorld()->GetAuthGameMode<AChaosImpactGameMode>() : nullptr)
			{
				GameMode->SetTrainingLocalPlayerCountLive(RequestedLocalPlayerCount);
				SetTrainingCharactersFrozen(true);
			}
		}
	}
}

void AChaosImpactPlayerController::ToggleTrainingTargets()
{
	if (bTrainingMode)
	{
		bTrainingTargetsEnabled = !bTrainingTargetsEnabled;
		if (CurrentScreen == EChaosImpactScreen::TrainingOverlay)
		{
			EnsureTrainingTargets();
		}
	}
}

void AChaosImpactPlayerController::ToggleTrainingCPU()
{
	if (bTrainingMode)
	{
		TrainingCPUCount = (TrainingCPUCount + 1) % 5;
		if (CurrentScreen == EChaosImpactScreen::TrainingOverlay)
		{
			if (AChaosImpactGameMode* GameMode = GetWorld()
				? GetWorld()->GetAuthGameMode<AChaosImpactGameMode>() : nullptr)
			{
				GameMode->SetTrainingCPUCountLive(TrainingCPUCount);
				SetTrainingCharactersFrozen(true);
			}
		}
	}
}

void AChaosImpactPlayerController::TogglePrimaryInputMode()
{
	bRequestedPrimaryUsesGamepad = !bRequestedPrimaryUsesGamepad;
	ResetControllerJoinSequence();
}

void AChaosImpactPlayerController::ApplyTrainingSettings()
{
	if (bTrainingMode && !bTravelPending)
	{
		bControllerAssignmentKeepsFlightMode = true;
		ControllerAssignmentReturnScreen = CurrentScreen == EChaosImpactScreen::TrainingOverlay
			? EChaosImpactScreen::TrainingOverlay : EChaosImpactScreen::TrainingSettings;
		ShowMenuScreen(EChaosImpactScreen::ControllerAssignment);
	}
}

void AChaosImpactPlayerController::CycleTrainingSummonBallType()
{
	TrainingSummonBallType = ChaosImpactBallTypes::FromIndex(
		(static_cast<int32>(TrainingSummonBallType) + 1) % ChaosImpactBallTypes::Count);
}

void AChaosImpactPlayerController::SummonTrainingBall()
{
	if (bTrainingMode && !bTravelPending)
	{
		ServerSummonTrainingBall(TrainingSummonBallType);
	}
}

void AChaosImpactPlayerController::ServerSummonTrainingBall_Implementation(const EChaosImpactBallType Type)
{
	UWorld* World = GetWorld();
	const AChaosImpactCharacter* TrainingPawn = Cast<AChaosImpactCharacter>(GetPawn());
	if (!World || !TrainingPawn || TrainingPawn->IsEliminated() || !ChaosImpact::IsTrainingWorld(World))
	{
		return;
	}
	FVector Forward = TrainingPawn->GetAimDirection().GetSafeNormal2D();
	if (Forward.IsNearlyZero())
	{
		Forward = TrainingPawn->GetActorForwardVector().GetSafeNormal2D();
	}
	// One step ahead of the player, hovering at the same height as a spawn point ball.
	FVector Location = TrainingPawn->GetActorLocation() + Forward * 150.0f;
	FHitResult Ground;
	if (World->LineTraceSingleByObjectType(Ground, Location + FVector::UpVector * 100.0f,
		Location - FVector::UpVector * 600.0f, FCollisionObjectQueryParams(ECC_WorldStatic)))
	{
		Location = Ground.ImpactPoint + FVector::UpVector * 38.0f;
	}
	const FTransform SpawnTransform(FRotator::ZeroRotator, Location);
	if (AChaosImpactBall* Ball = World->SpawnActorDeferred<AChaosImpactBall>(AChaosImpactBall::StaticClass(),
		SpawnTransform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
	{
		Ball->SetBallType(Type);
		Ball->FinishSpawning(SpawnTransform);
		Ball->MakePickup();
		UE_LOG(LogChaosImpact, Log, TEXT("Training ball summoned: %s"), ChaosImpactBallTypes::GetDisplayName(Type));
	}
}

void AChaosImpactPlayerController::ToggleBallFlightMode()
{
	BallFlightMode = BallFlightMode == EChaosImpactBallFlightMode::Straight
		? EChaosImpactBallFlightMode::Arc : EChaosImpactBallFlightMode::Straight;
}

void AChaosImpactPlayerController::BeginOnlineFlow(const bool bCreateRoom)
{
	bPendingCreateRoom = bCreateRoom;
	bOnlineNameOnly = false;
	const UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this);
	ShowMenuScreen(Sessions && Sessions->HasSavedPlayerName()
		? EChaosImpactScreen::OnlinePassword : EChaosImpactScreen::OnlineName);
}

void AChaosImpactPlayerController::BeginOnlineRename()
{
	bOnlineNameOnly = true;
	ShowMenuScreen(EChaosImpactScreen::OnlineName);
}

void AChaosImpactPlayerController::SubmitOnlineName(const FString& Name)
{
	if (Name.TrimStartAndEnd().IsEmpty())
	{
		return;
	}
	if (UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this))
	{
		Sessions->SetPlayerName(Name);
	}
	ShowMenuScreen(bOnlineNameOnly ? EChaosImpactScreen::MultiReady : EChaosImpactScreen::OnlinePassword);
}

void AChaosImpactPlayerController::SubmitRoomPassword(const FString& Password)
{
	UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this);
	if (!Sessions || bTravelPending)
	{
		return;
	}
	// The players and controllers chosen on the assignment screen come along into every online level.
	Sessions->SetLocalSetup(BuildLocalSetupOptions(), RequestedLocalPlayerCount);
	if (bPendingCreateRoom)
	{
		// The room gets a name next.
		PendingRoomPassword = Password;
		bRenamingRoom = false;
		ShowMenuScreen(EChaosImpactScreen::OnlineRoomName);
		return;
	}
	// Every room using this password is listed; the player picks one.
	Sessions->StartSearch(Password);
	ShowMenuScreen(EChaosImpactScreen::RoomList);
}

void AChaosImpactPlayerController::SubmitRoomName(const FString& Name)
{
	UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this);
	if (!Sessions || bTravelPending)
	{
		return;
	}
	const FString Trimmed = Name.TrimStartAndEnd().Left(UChaosImpactSessionSubsystem::MaxRoomNameLength);
	if (bRenamingRoom)
	{
		bRenamingRoom = false;
		if (AChaosImpactGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AChaosImpactGameMode>() : nullptr;
			GameMode && CanRenameRoom() && !Trimmed.IsEmpty())
		{
			GameMode->RenameRoom(Trimmed);
		}
		ResumeGameplay();
		return;
	}
	Sessions->CreateRoom(PendingRoomPassword, Trimmed.IsEmpty() ? Sessions->GetDefaultRoomName() : Trimmed);
	ShowMenuScreen(EChaosImpactScreen::OnlineStatus);
}

void AChaosImpactPlayerController::BeginRoomRename()
{
	if (CanRenameRoom())
	{
		bRenamingRoom = true;
		ShowMenuScreen(EChaosImpactScreen::OnlineRoomName);
	}
}

void AChaosImpactPlayerController::CancelRoomRename()
{
	bRenamingRoom = false;
	ShowMenuScreen(EChaosImpactScreen::Pause);
}

bool AChaosImpactPlayerController::CanRenameRoom() const
{
	const AChaosImpactGameState* Room = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	return IsOnlineRoomHost() && Room && !Room->bVersusMatch && Room->Phase == EChaosImpactOnlinePhase::Lobby;
}

bool AChaosImpactPlayerController::CanReopenRecruitment() const
{
	const AChaosImpactGameState* Room = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	return IsOnlineRoomHost() && Room && Room->bRecruitmentClosed && !Room->bVersusMatch
		&& Room->Phase == EChaosImpactOnlinePhase::Lobby;
}

void AChaosImpactPlayerController::ReopenRecruitment()
{
	if (!CanReopenRecruitment())
	{
		return;
	}
	ResumeGameplay();
	if (AChaosImpactGameMode* GameMode = GetWorld()->GetAuthGameMode<AChaosImpactGameMode>())
	{
		GameMode->ReopenRecruitment();
	}
}

void AChaosImpactPlayerController::JoinRoomListing(const int32 Index)
{
	if (UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this); Sessions && !bTravelPending)
	{
		Sessions->JoinRoomListing(Index);
	}
}

void AChaosImpactPlayerController::RefreshRoomList()
{
	if (UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this))
	{
		Sessions->StopSearch();
		Sessions->StartSearch(Sessions->GetPassword());
	}
}

void AChaosImpactPlayerController::CloseRoomList()
{
	StopRoomSearch();
	ShowMenuScreen(EChaosImpactScreen::OnlinePassword);
}

bool AChaosImpactPlayerController::CanToggleReady() const
{
	const AChaosImpactGameState* Room = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	return IsOnlineRoom() && Room && Room->bRulesDecided && !Room->bVersusMatch
		&& Room->Phase == EChaosImpactOnlinePhase::Lobby;
}

void AChaosImpactPlayerController::ToggleReadyForMatch()
{
	const AChaosImpactPlayerState* Own = GetPlayerState<AChaosImpactPlayerState>();
	if (!IsLocalController() || !Own || !CanToggleReady() || !IsGameplayActive())
	{
		return;
	}
	ServerSetReadyForMatch(!Own->bReadyForMatch);
}

namespace
{
	const TCHAR* OptionsSection = TEXT("ChaosImpact.Options");
	const TCHAR* RumbleKey = TEXT("Rumble");
}

bool AChaosImpactPlayerController::IsRumbleEnabled()
{
	bool bEnabled = true;
	if (GConfig)
	{
		GConfig->GetBool(OptionsSection, RumbleKey, bEnabled, GGameUserSettingsIni);
	}
	return bEnabled;
}

void AChaosImpactPlayerController::ToggleRumbleEnabled()
{
	const bool bEnabled = !IsRumbleEnabled();
	if (GConfig)
	{
		GConfig->SetBool(OptionsSection, RumbleKey, bEnabled, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
	if (bEnabled)
	{
		// The switch itself answers, so the player feels that it is on.
		PlayRumble(0.4f, 0.4f, 0.15f);
	}
	else
	{
		StopRumble();
	}
}

bool AChaosImpactPlayerController::CanRumble() const
{
	return IsLocalController() && IsUsingGamepad() && IsRumbleEnabled();
}

void AChaosImpactPlayerController::PlayRumble(const float Small, const float Big, const float Seconds,
	const float DelaySeconds)
{
#if !UE_BUILD_SHIPPING
	static const bool bLogRumble = FParse::Param(FCommandLine::Get(), TEXT("CIRumbleLog"));
	if (bLogRumble)
	{
		UE_LOG(LogChaosImpact, Log, TEXT("Rumble %s: small %.2f big %.2f for %.2f s after %.2f s (sent %d)"), *GetName(),
			Small, Big, Seconds, DelaySeconds, CanRumble());
	}
#endif
	if (!CanRumble() || Seconds <= 0.0f)
	{
		return;
	}
	if (DelaySeconds <= 0.0f)
	{
		StartRumbleNow(Small, Big, Seconds);
		return;
	}
	PendingRumbles.Add({FPlatformTime::Seconds() + DelaySeconds, Small, Big, Seconds});
}

void AChaosImpactPlayerController::StartRumbleNow(const float Small, const float Big, const float Seconds)
{
	// One dynamic effect per motor pair, so the heavy and light motors keep their own strength.
	if (Big > 0.0f)
	{
		PlayDynamicForceFeedback(FMath::Clamp(Big, 0.0f, 1.0f), Seconds, true, false, true, false);
	}
	if (Small > 0.0f)
	{
		PlayDynamicForceFeedback(FMath::Clamp(Small, 0.0f, 1.0f), Seconds, false, true, false, true);
	}
}

void AChaosImpactPlayerController::SetSustainedRumble(const float Small, const float Big)
{
	SustainedSmall = FMath::Max(SustainedSmall, Small);
	SustainedBig = FMath::Max(SustainedBig, Big);
}

void AChaosImpactPlayerController::StopRumble()
{
	PendingRumbles.Reset();
	if (SustainedSmallHandle != 0)
	{
		PlayDynamicForceFeedback(0.0f, -1.0f, false, true, false, true, EDynamicForceFeedbackAction::Stop, SustainedSmallHandle);
		SustainedSmallHandle = 0;
	}
	if (SustainedBigHandle != 0)
	{
		PlayDynamicForceFeedback(0.0f, -1.0f, true, false, true, false, EDynamicForceFeedbackAction::Stop, SustainedBigHandle);
		SustainedBigHandle = 0;
	}
	SustainedSmall = 0.0f;
	SustainedBig = 0.0f;
}

void AChaosImpactPlayerController::UpdateRumble()
{
	if (!IsLocalController())
	{
		return;
	}
	if (!CanRumble() || !IsGameplayActive())
	{
		StopRumble();
		return;
	}
	const double Now = FPlatformTime::Seconds();
	for (int32 Index = PendingRumbles.Num() - 1; Index >= 0; --Index)
	{
		if (Now >= PendingRumbles[Index].StartAt)
		{
			const FPendingRumble Pulse = PendingRumbles[Index];
			PendingRumbles.RemoveAtSwap(Index);
			StartRumbleNow(Pulse.Small, Pulse.Big, Pulse.Seconds);
		}
	}
	// Held rumble follows what the character asked for since the last update, and stops when nothing asks.
	const auto Drive = [this](FDynamicForceFeedbackHandle& Handle, const float Value, const bool bLarge)
	{
		if (Value <= 0.01f)
		{
			if (Handle != 0)
			{
				PlayDynamicForceFeedback(0.0f, -1.0f, bLarge, !bLarge, bLarge, !bLarge, EDynamicForceFeedbackAction::Stop, Handle);
				Handle = 0;
			}
			return;
		}
		const FDynamicForceFeedbackHandle Result = PlayDynamicForceFeedback(FMath::Clamp(Value, 0.0f, 1.0f), -1.0f,
			bLarge, !bLarge, bLarge, !bLarge,
			Handle != 0 ? EDynamicForceFeedbackAction::Update : EDynamicForceFeedbackAction::Start, Handle);
		Handle = Result != 0 ? Result : Handle;
	};
	Drive(SustainedSmallHandle, SustainedSmall, false);
	Drive(SustainedBigHandle, SustainedBig, true);
	SustainedSmall = 0.0f;
	SustainedBig = 0.0f;
}

void AChaosImpactPlayerController::ServerSetReadyForMatch_Implementation(const bool bReady)
{
	if (AChaosImpactGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AChaosImpactGameMode>() : nullptr)
	{
		GameMode->SetMemberReady(GetPlayerState<AChaosImpactPlayerState>(), bReady);
	}
}

void AChaosImpactPlayerController::CancelOnlineStatus()
{
	if (UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this))
	{
		Sessions->CancelCreate();
	}
	ShowMenuScreen(EChaosImpactScreen::OnlinePassword);
}

void AChaosImpactPlayerController::CloseRecruitment()
{
	if (!CanCloseRecruitment())
	{
		return;
	}
	ResumeGameplay();
	if (AChaosImpactGameMode* GameMode = GetWorld()->GetAuthGameMode<AChaosImpactGameMode>())
	{
		GameMode->CloseRecruitment();
		// The host decides the rules right away; everyone else sees that the rules are being chosen.
		OpenMatchRules(EChaosImpactScreen::Playing);
	}
}

void AChaosImpactPlayerController::LeaveOnlineRoom()
{
	UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this);
	if (!Sessions || bTravelPending)
	{
		return;
	}
	bTravelPending = true;
	Sessions->LeaveRoom();
}

void AChaosImpactPlayerController::StopRoomSearch()
{
	if (UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this))
	{
		Sessions->StopSearch();
	}
}

bool AChaosImpactPlayerController::IsSearchingForRoom() const
{
	const UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this);
	return Sessions && (Sessions->GetState() == EChaosImpactRoomState::Searching
		|| Sessions->GetState() == EChaosImpactRoomState::Joining);
}

bool AChaosImpactPlayerController::CanCloseRecruitment() const
{
	const AChaosImpactGameState* RoomState = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	// A room of one machine has nobody to play with yet.
	return IsOnlineRoomHost() && RoomState && !RoomState->bRecruitmentClosed
		&& RoomState->Phase == EChaosImpactOnlinePhase::Lobby && RoomState->CountMachines() >= 2;
}

void AChaosImpactPlayerController::ServerSetPlayerName_Implementation(const FString& Name)
{
	const FString Trimmed = Name.TrimStartAndEnd().Left(UChaosImpactSessionSubsystem::MaxNameLength);
	if (PlayerState && !Trimmed.IsEmpty())
	{
		PlayerState->SetPlayerName(Trimmed);
		// This machine's second player may have joined before the name arrived.
		if (const UNetConnection* Connection = GetNetConnection())
		{
			for (const UChildConnection* Child : Connection->Children)
			{
				if (Child && Child->PlayerController && Child->PlayerController->PlayerState)
				{
					Child->PlayerController->PlayerState->SetPlayerName(
						AChaosImpactPlayerState::MakeSecondPlayerName(Trimmed));
				}
			}
		}
		UE_LOG(LogChaosImpact, Log, TEXT("Player name set: %s"), *Trimmed);
	}
}

void AChaosImpactPlayerController::OpenTrainingLevel(const bool bKeepFlightMode, const bool bOnlineSearch)
{
	const FString MapPackage = TrainingLevel.GetLongPackageName();
	if (MapPackage.IsEmpty())
	{
		UE_LOG(LogChaosImpact, Error, TEXT("TrainingLevel must reference a map."));
		return;
	}
	bTravelPending = true;
	ExitTrainingOverlayPresentation();
	SetPause(false);
	const bool bOpenWithStraight = bKeepFlightMode
		&& BallFlightMode == EChaosImpactBallFlightMode::Straight;
	FString Options = FString::Printf(TEXT("CITraining=1?%s"), *BuildLocalSetupOptions());
	if (!bTrainingTargetsEnabled)
	{
		Options += TEXT("?CITargets=0");
	}
	if (TrainingCPUCount > 0)
	{
		Options += FString::Printf(TEXT("?CICPUCount=%d"), FMath::Clamp(TrainingCPUCount, 0, 4));
	}
	if (PlayFlow == EChaosImpactPlayFlow::VersusLocal)
	{
		Options += TEXT("?CIVersus=1");
		if (bLocalMatchRulesChosen)
		{
			Options += TEXT("?CITargets=0?") + ChaosImpactMatch::ToOptions(PendingMatchRules);
		}
	}
	if (bOpenWithStraight)
	{
		Options += TEXT("?CIBallStraight=1");
	}
	// Retry / settings reloads while a search is running must stay in the search lobby.
	if (bOnlineSearch || IsSearchingForRoom())
	{
		Options += TEXT("?CIOnlineSearch=1");
	}
	// Split screen held off by the VS opening is released here too; the next level starts from a normal view.
	bIntroFullScreen = false;
	if (UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
	{
		Viewport->SetForceDisableSplitscreen(false);
	}
	// Reloading clears balls, actors, health, stamina and position.
	UGameplayStatics::OpenLevel(this, FName(*MapPackage), true, Options);
}

void AChaosImpactPlayerController::EnsureTrainingArena()
{
	if (GetWorld() && GetWorld()->URL.HasOption(TEXT("CIMatch=1")))
	{
		return;
	}
	if (!bTrainingMode || !IsPrimaryLocalPlayerController() || !HasAuthority()
		|| !GetWorld() || !GetPawn() || IsValid(TrainingArena))
	{
		return;
	}
	for (TActorIterator<AChaosImpactTrainingArena> It(GetWorld()); It; ++It)
	{
		TrainingArena = *It;
		return;
	}

	FVector ArenaOrigin = GetPawn()->GetActorLocation();
	FHitResult GroundHit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(ChaosImpactTrainingArenaGround), false,
		GetPawn());
	if (GetWorld()->LineTraceSingleByChannel(GroundHit,
		ArenaOrigin + FVector::UpVector * 150.0f,
		ArenaOrigin - FVector::UpVector * 800.0f, ECC_Visibility, QueryParams))
	{
		ArenaOrigin.Z = GroundHit.ImpactPoint.Z + 2.0f;
	}
	else if (const ACharacter* CharacterPawn = Cast<ACharacter>(GetPawn()))
	{
		ArenaOrigin.Z -= CharacterPawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	}

	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	TrainingArena = GetWorld()->SpawnActor<AChaosImpactTrainingArena>(
		AChaosImpactTrainingArena::StaticClass(), ArenaOrigin, FRotator::ZeroRotator, Parameters);
}

void AChaosImpactPlayerController::EnsureTrainingBallSpawners()
{
	if (GetWorld() && GetWorld()->URL.HasOption(TEXT("CIMatch=1")))
	{
		return;
	}
	if (!bTrainingMode || !IsPrimaryLocalPlayerController() || !HasAuthority() || !GetWorld() || !GetPawn()
		|| !TrainingBallSpawners.IsEmpty())
	{
		return;
	}
	for (TActorIterator<AChaosImpactBallSpawner> It(GetWorld()); It; ++It)
	{
		TrainingBallSpawners.Add(*It);
	}
	// Level-authored spawn points always win. Add/move these actors in the editor
	// to replace the fallback layout without changing code.
	if (!TrainingBallSpawners.IsEmpty())
	{
		return;
	}

	const FVector Origin = GetPawn()->GetActorLocation();
	const FVector Offsets[] =
	{
		FVector(420.0f, 0.0f, 0.0f),
		FVector(-620.0f, 680.0f, 0.0f),
		FVector(-620.0f, -680.0f, 0.0f),
		FVector(1450.0f, 1300.0f, 0.0f),
		FVector(1900.0f, -1550.0f, 0.0f),
		FVector(-1900.0f, 1500.0f, 0.0f),
		FVector(-2750.0f, -1100.0f, 0.0f)
	};
	for (const FVector& Offset : Offsets)
	{
		FVector SpawnerLocation = Origin + Offset - FVector::UpVector * 90.0f;
		FHitResult GroundHit;
		const FVector TraceStart = Origin + Offset + FVector::UpVector * 500.0f;
		const FVector TraceEnd = Origin + Offset - FVector::UpVector * 1600.0f;
		if (GetWorld()->LineTraceSingleByChannel(
			GroundHit, TraceStart, TraceEnd, ECC_Visibility))
		{
			SpawnerLocation = GroundHit.ImpactPoint + FVector::UpVector * 3.0f;
		}

		if (AChaosImpactBallSpawner* Spawner = GetWorld()->SpawnActor<AChaosImpactBallSpawner>(
			AChaosImpactBallSpawner::StaticClass(), SpawnerLocation, FRotator::ZeroRotator))
		{
			TrainingBallSpawners.Add(Spawner);
		}
	}
}

void AChaosImpactPlayerController::EnsureTrainingWarpPads()
{
	if (GetWorld() && GetWorld()->URL.HasOption(TEXT("CIMatch=1")))
	{
		return;
	}
	if (!bTrainingMode || !IsPrimaryLocalPlayerController() || !HasAuthority() || !GetWorld() || !GetPawn()
		|| !TrainingWarpPads.IsEmpty())
	{
		return;
	}
	for (TActorIterator<AChaosImpactWarpPad> It(GetWorld()); It; ++It)
	{
		if (It->WarpGroup == 0)
		{
			TrainingWarpPads.Add(*It);
		}
	}
	// Pads placed in the level (BP_WarpPad) win, like ball spawn points; this layout is only a fallback.
	if (!TrainingWarpPads.IsEmpty())
	{
		return;
	}
	// Open ground in each part of the training arena, clear of walls, targets and ball pads.
	// Their steps face the player's starting point.
	const FVector Origin = GetPawn()->GetActorLocation();
	const FVector Offsets[] =
	{
		FVector(0.0f, 1500.0f, 0.0f),
		FVector(0.0f, -1100.0f, 0.0f),
		FVector(2800.0f, -1500.0f, 0.0f),
		FVector(-1700.0f, 300.0f, 0.0f)
	};
	for (const FVector& Offset : Offsets)
	{
		FVector PadLocation = Origin + Offset - FVector::UpVector * 90.0f;
		FHitResult GroundHit;
		if (GetWorld()->LineTraceSingleByChannel(GroundHit, Origin + Offset + FVector::UpVector * 500.0f,
			Origin + Offset - FVector::UpVector * 1600.0f, ECC_Visibility))
		{
			PadLocation = GroundHit.ImpactPoint;
		}
		FActorSpawnParameters Parameters;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const FVector ToOrigin = -Offset.GetSafeNormal2D();
		const FRotator Facing(0.0f, FMath::RadiansToDegrees(FMath::Atan2(-ToOrigin.X, ToOrigin.Y)), 0.0f);
		if (AChaosImpactWarpPad* Pad = GetWorld()->SpawnActor<AChaosImpactWarpPad>(
			AChaosImpactWarpPad::StaticClass(), PadLocation, Facing, Parameters))
		{
			TrainingWarpPads.Add(Pad);
		}
	}
}

void AChaosImpactPlayerController::EnsureTrainingTargets()
{
	if (GetWorld() && GetWorld()->URL.HasOption(TEXT("CIMatch=1")))
	{
		return;
	}
	if (!bTrainingMode || !IsPrimaryLocalPlayerController() || !HasAuthority() || !GetWorld() || !GetPawn())
	{
		return;
	}
	if (!bTrainingTargetsEnabled)
	{
		for (TActorIterator<AChaosImpactTrainingTarget> It(GetWorld()); It; ++It)
		{
			It->SetTrainingEnabled(false);
			TrainingTargets.AddUnique(*It);
		}
		return;
	}
	TrainingTargets.RemoveAll([](const TObjectPtr<AChaosImpactTrainingTarget>& Target)
	{
		return !IsValid(Target);
	});
	if (!TrainingTargets.IsEmpty())
	{
		for (AChaosImpactTrainingTarget* Target : TrainingTargets)
		{
			if (IsValid(Target))
			{
				Target->SetTrainingEnabled(true);
			}
		}
		return;
	}
	for (TActorIterator<AChaosImpactTrainingTarget> It(GetWorld()); It; ++It)
	{
		It->SetTrainingEnabled(true);
		TrainingTargets.Add(*It);
	}
	// If the designer placed any targets in the map, use that authored set as-is.
	if (!TrainingTargets.IsEmpty())
	{
		return;
	}

	struct FTargetSetup
	{
		FVector Offset;
		EChaosImpactTargetMotion Motion;
		float Distance;
		float Speed;
		float Phase;
	};

	const FTargetSetup Setups[] =
	{
		{FVector(3000.0f, 0.0f, 0.0f), EChaosImpactTargetMotion::Stationary, 0.0f, 0.3f, 0.0f},
		{FVector(2250.0f, 1660.0f, 0.0f), EChaosImpactTargetMotion::Stationary, 0.0f, 0.3f, 0.0f},
		{FVector(-2450.0f, -1660.0f, 0.0f), EChaosImpactTargetMotion::Stationary, 0.0f, 0.3f, 0.0f},
		{FVector(2200.0f, -1900.0f, 0.0f), EChaosImpactTargetMotion::SideToSide, 250.0f, 0.26f, 0.0f},
		{FVector(650.0f, 2050.0f, 0.0f), EChaosImpactTargetMotion::SideToSide, 260.0f, 0.31f, 0.3f},
		{FVector(-1250.0f, 1800.0f, 0.0f), EChaosImpactTargetMotion::SideToSide, 220.0f, 0.36f, 0.58f},
		{FVector(-2250.0f, 1050.0f, 0.0f), EChaosImpactTargetMotion::ForwardBack, 240.0f, 0.3f, 0.15f},
		{FVector(1500.0f, 800.0f, 0.0f), EChaosImpactTargetMotion::ForwardBack, 190.0f, 0.4f, 0.72f}
	};

	const FVector Origin = GetPawn()->GetActorLocation();
	for (const FTargetSetup& Setup : Setups)
	{
		FVector TargetLocation = Origin + Setup.Offset - FVector::UpVector * 90.0f;
		FHitResult GroundHit;
		const FVector TraceStart = Origin + Setup.Offset + FVector::UpVector * 700.0f;
		const FVector TraceEnd = Origin + Setup.Offset - FVector::UpVector * 1800.0f;
		if (GetWorld()->LineTraceSingleByChannel(
			GroundHit, TraceStart, TraceEnd, ECC_Visibility))
		{
			TargetLocation = GroundHit.ImpactPoint + FVector::UpVector * 2.0f;
		}

		const FRotator FacingRotation = (Origin - TargetLocation).Rotation();
		FActorSpawnParameters Parameters;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (AChaosImpactTrainingTarget* Target = GetWorld()->SpawnActor<AChaosImpactTrainingTarget>(
			AChaosImpactTrainingTarget::StaticClass(), TargetLocation,
			FRotator(0.0f, FacingRotation.Yaw, 0.0f), Parameters))
		{
			Target->ConfigureMotion(Setup.Motion, Setup.Distance, Setup.Speed, Setup.Phase);
			TrainingTargets.Add(Target);
		}
	}
}

void AChaosImpactPlayerController::RemoveSecondaryLocalPlayers()
{
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		return;
	}
	while (GameInstance->GetLocalPlayers().Num() > 1)
	{
		ULocalPlayer* RemovedPlayer = GameInstance->GetLocalPlayers().Last();
		if (APlayerController* RemovedController = RemovedPlayer
			? RemovedPlayer->GetPlayerController(GetWorld()) : nullptr)
		{
			UGameplayStatics::RemovePlayer(RemovedController, true);
		}
		else
		{
			GameInstance->RemoveLocalPlayer(RemovedPlayer);
		}
	}
	RequestedLocalPlayerCount = 1;
}

// ---------------------------------------------------------------------------------------------
// VS match

bool AChaosImpactPlayerController::IsVersusMatchWorld() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	const AChaosImpactGameState* Match = World->GetGameState<AChaosImpactGameState>();
	return World->URL.HasOption(TEXT("CIMatch=1")) || (Match && Match->bVersusMatch);
}

bool AChaosImpactPlayerController::CanOpenMatchRulesFromPause() const
{
	const AChaosImpactGameState* Match = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	// Rules are decided once recruitment is closed (closing it opens the rule screen straight away).
	return IsOnlineRoomHost() && Match && !Match->bVersusMatch && Match->Phase == EChaosImpactOnlinePhase::Lobby
		&& Match->bRecruitmentClosed;
}

int32 AChaosImpactPlayerController::GetMatchHumanCount() const
{
	if (const AChaosImpactGameState* Match = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
		Match && IsOnlineRoom())
	{
		return FMath::Max(1, Match->CountHumanMembers());
	}
	// Rules reached from a new player entry are for the players just entered, even when chosen from inside a
	// finished VS level where the extra players have already been removed for the menu.
	if (IsVersusMatchWorld() && MatchRulesReturnScreen != EChaosImpactScreen::ControllerAssignment)
	{
		const UGameInstance* GameInstance = GetGameInstance();
		return GameInstance ? FMath::Max(1, GameInstance->GetLocalPlayers().Num()) : 1;
	}
	return FMath::Clamp(RequestedLocalPlayerCount, 1, 4);
}

void AChaosImpactPlayerController::OpenMatchRules(const EChaosImpactScreen ReturnScreen)
{
	MatchRulesReturnScreen = ReturnScreen;
	PendingMatchRules = ChaosImpactMatch::Sanitize(PendingMatchRules, GetMatchHumanCount());
	ShowMenuScreen(EChaosImpactScreen::MatchRules);
}

void AChaosImpactPlayerController::AdjustMatchRule(const int32 Row, const int32 Direction)
{
	const int32 Humans = GetMatchHumanCount();
	FChaosImpactMatchRules& Rules = PendingMatchRules;
	const auto Wrap = [](const int32 Value, const int32 Count) { return (Value % Count + Count) % Count; };
	if (Row == 0)
	{
		// 3 → 4 → 5 minutes.
		const int32 ChoiceCount = UE_ARRAY_COUNT(ChaosImpactMatch::MinuteChoices);
		const int32 CurrentMinutes = ChaosImpactMatch::SanitizeMinutes(Rules.Minutes);
		int32 Current = 0;
		for (int32 Index = 0; Index < ChoiceCount; ++Index)
		{
			Current = ChaosImpactMatch::MinuteChoices[Index] == CurrentMinutes ? Index : Current;
		}
		Rules.Minutes = ChaosImpactMatch::MinuteChoices[Wrap(Current + Direction, ChoiceCount)];
	}
	else if (Row == 1)
	{
		// 個人戦 → 2 → 3 → 4 teams.
		static constexpr int32 Modes[] = {0, 2, 3, 4};
		const int32 Current = Rules.TeamCount >= 2 ? Rules.TeamCount - 1 : 0;
		Rules.TeamCount = Modes[Wrap(Current + Direction, UE_ARRAY_COUNT(Modes))];
	}
	else if (Row == 2)
	{
		const int32 Minimum = ChaosImpactMatch::GetMinCPUCount(Humans, Rules.TeamCount);
		const int32 Span = ChaosImpactMatch::GetMaxCPUCount(Humans) - Minimum + 1;
		Rules.CPUCount = Minimum + Wrap(Rules.CPUCount - Minimum + Direction, FMath::Max(Span, 1));
	}
	Rules = ChaosImpactMatch::Sanitize(Rules, Humans);
}

void AChaosImpactPlayerController::ConfirmMatchRules()
{
	PendingMatchRules = ChaosImpactMatch::Sanitize(PendingMatchRules, GetMatchHumanCount());
	AChaosImpactGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AChaosImpactGameMode>() : nullptr;
	if (const AChaosImpactGameState* Room = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
		GameMode && IsOnlineRoomHost() && Room && !Room->bVersusMatch && Room->Phase == EChaosImpactOnlinePhase::Lobby)
	{
		// The lobby now waits for everyone's 準備OK before the match begins.
		EnterPlayingScreen();
		GameMode->DecideLobbyRules(PendingMatchRules);
		return;
	}
	// A new player entry always opens a fresh VS level with those players; only a rematch or rule change
	// from inside the match restarts it in place.
	const bool bNewLocalEntry = MatchRulesReturnScreen == EChaosImpactScreen::ControllerAssignment;
	if (GameMode && (IsOnlineRoomHost() || (IsVersusMatchWorld() && !bNewLocalEntry)))
	{
		// Players are already here: the match (re)starts in place.
		EnterPlayingScreen();
		GameMode->ConfigureVersusMatch(PendingMatchRules);
		return;
	}
	if (PlayFlow == EChaosImpactPlayFlow::VersusLocal)
	{
		bLocalMatchRulesChosen = true;
		OpenTrainingLevel(false);
	}
}

void AChaosImpactPlayerController::CancelMatchRules()
{
	if (MatchRulesReturnScreen == EChaosImpactScreen::Playing)
	{
		EnterPlayingScreen();
	}
	else
	{
		ShowMenuScreen(MatchRulesReturnScreen);
	}
}

void AChaosImpactPlayerController::ChangeOwnTeam(const int32 Direction)
{
	ServerChangeTeam(Direction);
}

void AChaosImpactPlayerController::ServerChangeTeam_Implementation(const int32 Direction)
{
	if (AChaosImpactGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AChaosImpactGameMode>() : nullptr)
	{
		GameMode->ChangeMemberTeam(GetPlayerState<AChaosImpactPlayerState>(), FMath::Clamp(Direction, -1, 1));
	}
}

bool AChaosImpactPlayerController::CanStartVersusMatch() const
{
	return IsLocalController() && HasAuthority() && IsPrimaryLocalPlayerController();
}

void AChaosImpactPlayerController::RequestStartTeamMatch()
{
	if (CanStartVersusMatch())
	{
		ServerStartTeamMatch();
	}
}

void AChaosImpactPlayerController::ServerStartTeamMatch_Implementation()
{
	if (AChaosImpactGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AChaosImpactGameMode>() : nullptr;
		GameMode && IsLocalController())
	{
		GameMode->ConfirmTeamsAndStart();
	}
}

void AChaosImpactPlayerController::ServerReportIntroFinished_Implementation(const double IntroStartedAt)
{
	if (AChaosImpactGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AChaosImpactGameMode>() : nullptr)
	{
		GameMode->ReportIntroFinished(PlayerState, IntroStartedAt);
	}
}

void AChaosImpactPlayerController::RetryVersusMatch()
{
	AChaosImpactGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AChaosImpactGameMode>() : nullptr;
	if (!GameMode || !CanStartVersusMatch())
	{
		return;
	}
	EnterPlayingScreen();
	GameMode->ConfigureVersusMatch(ChaosImpactMatch::Sanitize(PendingMatchRules, GetMatchHumanCount()));
}

void AChaosImpactPlayerController::EnterPlayingScreen()
{
	if (!MenuWidget || bTravelPending)
	{
		return;
	}
	CurrentScreen = EChaosImpactScreen::Playing;
	MenuWidget->ShowScreen(CurrentScreen);
	ApplyScreenInput();
}

void AChaosImpactPlayerController::PlayerTick(const float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	UpdateMatchScreens();
	UpdateMatchIntroCamera();
	UpdateRumble();
#if !UE_BUILD_SHIPPING
	// Development (-CIAutoReady=<seconds>): press 準備OK by itself that long after the rules are decided.
	static const float AutoReadyDelay = []()
	{
		float Value = -1.0f;
		FParse::Value(FCommandLine::Get(), TEXT("CIAutoReady="), Value);
		return Value;
	}();
	const AChaosImpactPlayerState* Own = GetPlayerState<AChaosImpactPlayerState>();
	if (AutoReadyDelay >= 0.0f && IsLocalController() && Own && !Own->bReadyForMatch && CanToggleReady() && GetWorld())
	{
		if (DevAutoReadyAt <= 0.0)
		{
			DevAutoReadyAt = GetWorld()->GetTimeSeconds() + AutoReadyDelay;
		}
		else if (GetWorld()->GetTimeSeconds() >= DevAutoReadyAt)
		{
			DevAutoReadyAt = 0.0;
			ServerSetReadyForMatch(true);
		}
	}
	else
	{
		DevAutoReadyAt = 0.0;
	}
#endif
}

void AChaosImpactPlayerController::UpdateMatchScreens()
{
	if (!IsPrimaryLocalPlayerController() || !MenuWidget || bTravelPending)
	{
		return;
	}
	const AChaosImpactGameState* Match = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	if (!Match)
	{
		return;
	}
	const bool bTeamSelect = Match->bVersusMatch && Match->Phase == EChaosImpactOnlinePhase::TeamSelect;
	if (bTeamSelect && CurrentScreen == EChaosImpactScreen::Playing)
	{
		ShowMenuScreen(EChaosImpactScreen::TeamSelect);
	}
	else if (!bTeamSelect && CurrentScreen == EChaosImpactScreen::TeamSelect)
	{
		EnterPlayingScreen();
	}
	// A local match waits on its results for the players to choose what comes next.
	const bool bLocalResults = Match->bVersusMatch && Match->Phase == EChaosImpactOnlinePhase::Results && !IsOnlineRoom();
	if (bLocalResults && CurrentScreen == EChaosImpactScreen::Playing
		&& Match->GetPhaseElapsedSeconds() > ChaosImpactMatch::ResultsRevealSeconds)
	{
		ShowMenuScreen(EChaosImpactScreen::MatchEnd);
	}
	else if (!bLocalResults && CurrentScreen == EChaosImpactScreen::MatchEnd)
	{
		EnterPlayingScreen();
	}
}

void AChaosImpactPlayerController::UpdateMatchIntroCamera()
{
	const AChaosImpactGameState* Match = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	const AChaosImpactCharacter* PlayerCharacter = Cast<AChaosImpactCharacter>(GetPawn());
	const float Elapsed = Match ? Match->GetIntroElapsedSeconds() : 0.0f;
	const float CameraSeconds = ChaosImpactMatch::FlyoverSeconds + ChaosImpactMatch::DiveSeconds;
	const bool bActive = IsLocalController() && PlayerCharacter && Match && Match->bVersusMatch
		&& Match->Phase == EChaosImpactOnlinePhase::Intro && Elapsed < CameraSeconds;
	// Tell the host this screen has shown the whole opening; Ready? waits for every player's report.
	if (IsLocalController() && Match && Match->bVersusMatch && Match->Phase == EChaosImpactOnlinePhase::Intro
		&& Elapsed >= CameraSeconds && ReportedIntroStartedAt != Match->PhaseStartedAt)
	{
		ReportedIntroStartedAt = Match->PhaseStartedAt;
		ServerReportIntroFinished(Match->PhaseStartedAt);
	}
	// Split screen: the flyover fills the whole screen through P1's camera; the screen splits as the dive
	// begins, and every view then dives from the same spot into its own player.
	if (IsPrimaryLocalPlayerController())
	{
		const bool bFullScreen = bActive && Elapsed < ChaosImpactMatch::FlyoverSeconds;
		if (bFullScreen != bIntroFullScreen)
		{
			bIntroFullScreen = bFullScreen;
			UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
			if (Viewport)
			{
				Viewport->SetForceDisableSplitscreen(bFullScreen);
			}
			UE_LOG(LogChaosImpact, Log, TEXT("VS opening split screen %s (elapsed %.2f, viewport %s)"),
				bFullScreen ? TEXT("off: full-screen flyover") : TEXT("restored"), Elapsed,
				Viewport ? TEXT("found") : TEXT("missing"));
		}
	}
	if (!bActive)
	{
		if (IsValid(IntroCamera))
		{
			if (GetViewTarget() == IntroCamera && GetPawn())
			{
				SetViewTarget(GetPawn());
			}
			IntroCamera->Destroy();
		}
		IntroCamera = nullptr;
		return;
	}
	if (!IsValid(IntroCamera))
	{
		FActorSpawnParameters Parameters;
		Parameters.ObjectFlags |= RF_Transient;
		IntroCamera = GetWorld()->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity, Parameters);
		if (!IntroCamera)
		{
			return;
		}
		// Local to this screen: every player, in split screen or online, gets their own camera.
		IntroCamera->SetReplicates(false);
		IntroCamera->GetCameraComponent()->bConstrainAspectRatio = false;
		if (const UCameraComponent* Follow = PlayerCharacter->GetFollowCamera())
		{
			IntroCamera->GetCameraComponent()->SetFieldOfView(Follow->FieldOfView);
		}
	}

	// A wide sweep around the stage that ends facing the same way as the player's own camera.
	const FVector StageCenter = Match->StageCenter;
	const auto FlyoverPose = [&StageCenter](const float Alpha, FVector& OutLocation, FRotator& OutRotation)
	{
		const float Ease = FMath::InterpEaseInOut(0.0f, 1.0f, FMath::Clamp(Alpha, 0.0f, 1.0f), 2.0f);
		const FVector Direction = FRotator(0.0f, FMath::Lerp(-200.0f, 0.0f, Ease), 0.0f).Vector();
		OutLocation = StageCenter - Direction * FMath::Lerp(5600.0f, 2600.0f, Ease)
			+ FVector(0.0f, 0.0f, FMath::Lerp(3400.0f, 1500.0f, Ease));
		const FVector LookAt = StageCenter + Direction * FMath::Lerp(0.0f, 900.0f, Ease);
		OutRotation = (LookAt - OutLocation).Rotation();
	};
	FVector Location;
	FRotator Rotation;
	if (Elapsed < ChaosImpactMatch::FlyoverSeconds)
	{
		FlyoverPose(Elapsed / ChaosImpactMatch::FlyoverSeconds, Location, Rotation);
	}
	else
	{
		// Dive from the end of the sweep into the top-down view the player will play with.
		FVector From;
		FRotator FromRotation;
		FlyoverPose(1.0f, From, FromRotation);
		const UCameraComponent* Follow = PlayerCharacter->GetFollowCamera();
		const FVector To = Follow ? Follow->GetComponentLocation() : PlayerCharacter->GetActorLocation();
		const FRotator ToRotation = Follow ? Follow->GetComponentRotation() : FRotator(-60.0f, 0.0f, 0.0f);
		const float Alpha = FMath::InterpEaseInOut(0.0f, 1.0f,
			FMath::Clamp((Elapsed - ChaosImpactMatch::FlyoverSeconds) / ChaosImpactMatch::DiveSeconds, 0.0f, 1.0f), 3.0f);
		Location = FMath::Lerp(From, To, Alpha);
		Rotation = FQuat::Slerp(FromRotation.Quaternion(), ToRotation.Quaternion(), Alpha).Rotator();
	}
	IntroCamera->SetActorLocationAndRotation(Location, Rotation);
	if (GetViewTarget() != IntroCamera)
	{
		SetViewTarget(IntroCamera);
	}
}
