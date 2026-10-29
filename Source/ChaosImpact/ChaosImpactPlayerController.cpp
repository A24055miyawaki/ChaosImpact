// Copyright Epic Games, Inc. All Rights Reserved.


#include "ChaosImpactPlayerController.h"
#include "ChaosImpactTackleComponent.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactTitleDemo.h"
#include "ChaosImpactVersusCard.h"
#include "ChaosImpactCharacterPreview.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ChaosImpactPaint.h"
#include "MoviePlayer.h"
#include "Containers/Ticker.h"
#include "ChaosImpactGameMode.h"
#include "ChaosImpactBallSpawner.h"
#include "ChaosImpactTrainingArena.h"
#include "ChaosImpactTrainingTarget.h"
#include "ChaosImpactWarpPad.h"
#include "ChaosImpactMenuWidget.h"
#include "ChaosImpactMatchAnnouncerWidget.h"
#include "ChaosImpactResults.h"
#include "ChaosImpactFxPreloadSubsystem.h"
#include "ChaosImpactLoadingScreen.h"
#include "Misc/App.h"
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
#include "ChaosImpactFxKit.h"
#include "Misc/ConfigCacheIni.h"
#include "ChaosImpactCharacterRoster.h"
#include "ChaosImpactLoadoutSubsystem.h"
#include "ChaosImpactSettings.h"
#include "ChaosImpactSoloStages.h"
#include "ChaosImpactSoloTitle.h"
#include "ChaosImpactDemoPlay.h"
#include "Misc/PackageName.h"
#include "ChaosImpactWorldMap.h"
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
	// The saved volume and brightness (Unreal applies its own screen and picture settings).
	ChaosImpactSettings::ApplyStartupSettings();
	bTrainingMode = ChaosImpact::IsTrainingWorld(GetWorld());

	// ソロモードオプション (?CISolo=1) の読み込み処理を追加
	if (GetWorld() && GetWorld()->URL.HasOption(TEXT("CISolo=1")))
	{
		bSoloMode = true;
		CurrentSoloStage = FMath::Max(1, FCString::Atoi(
			GetWorld()->URL.GetOption(TEXT("CIStage="), TEXT("1"))));
		// ソロプレイ時はトレーニングモードの共通フラグをオフにする
		bTrainingMode = false;
	}
	// Solo mode's world map (?CIWorldMap=1): walked on straight away, like a stage.
	if (GetWorld() && GetWorld()->URL.HasOption(TEXT("CIWorldMap=1")))
	{
		bSoloWorldMap = true;
		bTrainingMode = false;
	}
	// The title's demo play (?CIDemo=1): a CPUs' VS match watched (AChaosImpactDemoPlay).
	bDemoPlay = GetWorld() && GetWorld()->URL.HasOption(TEXT("CIDemo=1"));
	// Solo mode's title (?CISoloTitle=1): its own screen over the dusk temple (AChaosImpactSoloTitle).
	if (GetWorld() && GetWorld()->URL.HasOption(TEXT("CISoloTitle=1")))
	{
		bSoloTitle = true;
		bTrainingMode = false;
	}

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
		bTrainingTeams = GetWorld()->URL.HasOption(TEXT("CITrainingTeams=1"));
		for (int32 I = 0; I < 4; ++I)
		{
			TrainingHumanTeams[I] = FMath::Clamp(FCString::Atoi(GetWorld()->URL.GetOption(*FString::Printf(TEXT("CIHumanTeam%d="), I), TEXT("0"))), 0, 3);
			TrainingCPUTeams[I] = FMath::Clamp(FCString::Atoi(GetWorld()->URL.GetOption(*FString::Printf(TEXT("CICPUTeam%d="), I), TEXT("1"))), 0, 3);
		}
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
	// A solo level (?CISolo=1) is played straight away too, like training.
	// The exploration map (?CIOpenWorld=1, OpenWorld/) is walked on straight away too.
	const bool bExplorationMap = GetWorld() && GetWorld()->URL.HasOption(TEXT("CIOpenWorld=1"));
	CurrentScreen = bTrainingMode || bSoloMode || bSoloWorldMap || bSoloTitle || bExplorationMap ? EChaosImpactScreen::Playing : EChaosImpactScreen::Title;
	if (bSoloTitle && IsPrimaryLocalPlayerController() && GetWorld() && !AChaosImpactSoloTitle::Find(this))
	{
		GetWorld()->SpawnActor<AChaosImpactSoloTitle>();
	}
	if (bDemoPlay && IsPrimaryLocalPlayerController() && GetWorld() && !AChaosImpactDemoPlay::Find(this))
	{
		GetWorld()->SpawnActor<AChaosImpactDemoPlay>();
	}
	TitleShownSince = FPlatformTime::Seconds();
	// The title plays a CPU match behind it, started a moment after the title appears (-CINoTitleDemo: none).
	bTitleDemoWorld = CurrentScreen == EChaosImpactScreen::Title && !bSoloMode && !bSoloWorldMap && GetNetMode() == NM_Standalone
		&& GetWorld() && !GetWorld()->URL.HasOption(TEXT("CIVersus=1")) && !FParse::Param(FCommandLine::Get(), TEXT("CINoTitleDemo"));
	// A local VS level opens dark, behind the VS emblem the card ended on (see UpdateVersusReveal).
	if (GetNetMode() == NM_Standalone && IsPrimaryLocalPlayerController() && GetWorld()
		&& GetWorld()->URL.HasOption(TEXT("CIMatch=1")) && !bDemoPlay)
	{
		if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
		{
			VersusReveal = SNew(SChaosImpactVersusReveal);
			Viewport->AddViewportWidgetContent(VersusReveal.ToSharedRef(), 10000);
			VersusRevealDeadline = FPlatformTime::Seconds() + 8.0;
			GetWorldTimerManager().SetTimer(VersusRevealTimer, this, &AChaosImpactPlayerController::UpdateVersusReveal, 0.02f, true);
		}
	}
	// The game's effects: at the title they load in the background behind a loading screen that can be played with
	// (automated tests load them at once, unless -CIAsyncPreload); anywhere else they load now.
	if (UChaosImpactFxPreloadSubsystem* Preload = UChaosImpactFxPreloadSubsystem::Get(this); Preload && !Preload->IsComplete())
	{
		const bool bInBackground = bTitleDemoWorld && IsPrimaryLocalPlayerController()
			&& (!FApp::IsUnattended() || FParse::Param(FCommandLine::Get(), TEXT("CIAsyncPreload")));
		if (bInBackground)
		{
			Preload->StartAsync();
			bAwaitingStartupLoad = true;
			if (UChaosImpactLoadingSubsystem* Loading = UChaosImpactLoadingSubsystem::Get(this))
			{
				TWeakObjectPtr<UChaosImpactFxPreloadSubsystem> WeakPreload = Preload;
				Loading->ShowWaiting(EChaosImpactLoadingKind::Startup,
					[WeakPreload]() { return WeakPreload.IsValid() ? WeakPreload->GetProgress() : 1.0f; });
			}
		}
		else
		{
			Preload->FinishNow();
		}
	}
	if (bTitleDemoWorld && IsPrimaryLocalPlayerController() && !bAwaitingStartupLoad)
	{
		// After the logo has landed, so setting the stage up never stutters its arrival.
		GetWorldTimerManager().SetTimer(TitleDemoTimer, this, &AChaosImpactPlayerController::StartTitleDemo, 1.4f, false);
	}
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
			if (UChaosImpactLoadingSubsystem* Loading = UChaosImpactLoadingSubsystem::Get(this))
			{
				Loading->BeginTravel(EChaosImpactLoadingKind::RoomLeave);
			}
			UGameplayStatics::OpenLevel(this, FName(*TrainingLevel.GetLongPackageName()), true,
				Sessions->GetOfflineTrainingOptions());
			return;
		}
	}

	EnsureTrainingArena();
	EnsureTrainingBallSpawners();
	EnsureTrainingWarpPads();
	EnsureTrainingTargets();

	// Back from a matched room: the menu to search again comes straight up.
	if (bTrainingMode && IsLocalController() && bPrimaryLocalPlayer && GetNetMode() == NM_Standalone)
	{
		if (UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this))
		{
			const EChaosImpactQueueMode Again = Sessions->ConsumeShowQueueMenu();
			if (Again != EChaosImpactQueueMode::None)
			{
				PlayFlow = EChaosImpactPlayFlow::VersusOnline;
				GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this, Again]()
				{
					if (Again == EChaosImpactQueueMode::Rank)
					{
						OpenRankTop();
					}
					else
					{
						ShowMenuScreen(EChaosImpactScreen::MultiReady);
					}
				}));
			}
		}
	}

	// Every world: once the effects are in, they are shown once out of sight, then any loading screen goes.
	if (IsLocalController() && IsPrimaryLocalPlayerController())
	{
		GetWorldTimerManager().SetTimer(WarmUpTimer, this, &AChaosImpactPlayerController::WarmUpWhenLoaded, 0.1f, true, 0.3f);
	}
}

void AChaosImpactPlayerController::WarmUpWhenLoaded()
{
	if (const UChaosImpactFxPreloadSubsystem* Preload = UChaosImpactFxPreloadSubsystem::Get(this); Preload && !Preload->IsComplete())
	{
		return;
	}
	GetWorldTimerManager().ClearTimer(WarmUpTimer);
	// Every world balls fly in (training, VS, solo, and the title with its demo match): each effect shown once now,
	// out of sight below the arena, so the first real fire or ice ball does not stall while pipeline states and GPU
	// resources are created.
	if (bTrainingMode || bSoloMode || bTitleDemoWorld)
	{
		FVector Below = GetPawn() ? GetPawn()->GetActorLocation() : FVector::ZeroVector;
		Below.Z -= 6000.0f;
		ChaosImpactBallTypes::WarmUpEffects(GetWorld(), Below);
	}
	// A few frames for that to be drawn, then this world is ready.
	FTimerHandle ReadyTimer;
	GetWorldTimerManager().SetTimer(ReadyTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		if (UChaosImpactLoadingSubsystem* Loading = UChaosImpactLoadingSubsystem::Get(this))
		{
			Loading->NotifyWorldReady();
		}
		if (bAwaitingStartupLoad)
		{
			// The title from its start: the logo lands, then the demo match comes on behind it.
			bAwaitingStartupLoad = false;
			if (MenuWidget && CurrentScreen == EChaosImpactScreen::Title)
			{
				MenuWidget->ShowScreen(EChaosImpactScreen::Title);
			}
			GetWorldTimerManager().SetTimer(TitleDemoTimer, this, &AChaosImpactPlayerController::StartTitleDemo, 1.4f, false);
		}
	}), 0.3f, false);
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

void AChaosImpactPlayerController::HandleBoundKeyPressed(const FKey Key)
{
	using namespace ChaosImpactSettings;
	if (IsActionKey(this, EChaosImpactAction::Pause, Key))
	{
		TogglePauseMenu();
		return;
	}
	if (GetWorld() && GetWorld()->IsPaused())
	{
		return;
	}
	if (IsActionKey(this, EChaosImpactAction::TrainingMenu, Key))
	{
		ToggleTrainingOverlay();
	}
	else if (IsActionKey(this, EChaosImpactAction::LobbyReady, Key))
	{
		ToggleReadyForMatch();
	}
	else if (IsActionKey(this, EChaosImpactAction::LobbySpectate, Key))
	{
		ToggleSpectating();
	}
}

void AChaosImpactPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	// Pause, the training menu and the online lobby's 準備OK / 観戦 are the player's own controls (the settings
	// screen): every press comes here and is matched against them. Pause also works while paused, to close it.
	FInputKeyBinding AnyKey(FInputChord(EKeys::AnyKey), IE_Pressed);
	AnyKey.bConsumeInput = false;
	AnyKey.bExecuteWhenPaused = true;
	AnyKey.KeyDelegate.BindDelegate(this, &AChaosImpactPlayerController::HandleBoundKeyPressed);
	InputComponent->KeyBindings.Add(AnyKey);

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
	if (IsGameplayActive())
	{
		if (auto* P=Cast<AChaosImpactCharacter>(GetPawn()); P && P->GetTackle()->HandleKey(Params.Key,Params.Event))
		{ Super::InputKey(Params); return true; }
	}
	if (IsGameplayActive() && !IsUsingGamepad())
	{
		if (AChaosImpactCharacter* InputCharacter = Cast<AChaosImpactCharacter>(GetPawn());
			InputCharacter && InputCharacter->HandleEasyPointerKey(Params.Key, Params.Event))
		{
			if (Params.Event == IE_Released || (Params.Key == EKeys::RightMouseButton
				&& ChaosImpactSettings::IsMouseOnlyScheme(ChaosImpactSettings::GetPlayerControlScheme(this))))
			{
				// Releases must clear PlayerInput even if the pointer moved over UI while holding a gameplay button.
				// Hold-mode RMB presses also need recording for release/focus-loss handling (no bound action uses RMB).
				Super::InputKey(Params);
			}
			return true;
		}
	}
	const bool bHandledByBase = Super::InputKey(Params);
	// The player's throw keys act at once (the character also reads them every frame, for whatever this misses).
	if (IsGameplayActive() && ChaosImpactSettings::IsActionKey(this, EChaosImpactAction::Throw, Params.Key))
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
			PlayerCharacter->BeginBoundThrowInput();
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

void AChaosImpactPlayerController::StartTitleDemo()
{
	if (TitleDemo || bTravelPending || !GetWorld())
	{
		return;
	}
	FActorSpawnParameters Parameters;
	Parameters.Owner = this;
	TitleDemo = GetWorld()->SpawnActor<AChaosImpactTitleDemo>(AChaosImpactTitleDemo::StaticClass(), FTransform::Identity, Parameters);
	if (TitleDemo)
	{
		TitleDemo->SetFilming(CurrentScreen == EChaosImpactScreen::Title);
	}
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
			PlayerCharacter->ResetEasyControls();
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
	if (!bPlaying && IsPrimaryLocalPlayerController() && GetGameInstance())
	{
		for (ULocalPlayer* Local : GetGameInstance()->GetLocalPlayers())
		{
			AChaosImpactPlayerController* LocalController = Local ? Cast<AChaosImpactPlayerController>(Local->GetPlayerController(GetWorld())) : nullptr;
			if (!LocalController) { continue; }
			if (!LocalController->IsUsingGamepad()
				&& ChaosImpactSettings::GetPlayerControlScheme(LocalController) != EChaosImpactControlScheme::Normal)
			{ bShowMouseCursor = true; bEnableClickEvents = true; bEnableMouseOverEvents = true; }
			if (LocalController != this)
			{
				if (AChaosImpactCharacter* LocalCharacter = Cast<AChaosImpactCharacter>(LocalController->GetPawn()))
				{ LocalCharacter->RequestCancelThrow(); LocalCharacter->ResetEasyControls(); }
			}
		}
	}
	CurrentMouseCursor = DefaultMouseCursor;
	// The training panel deliberately leaves world time and ball physics alive.
	// Pausing an online room would freeze every member, so menus there never pause the world.
	// Team select lets the other local players keep choosing through their own controllers.
	// A spectator's time stop holds through menus too.
	// The title's demo match plays on behind it.
	SetPause(bSpectateTimeStopped || (!bPlaying && !bLiveTrainingOverlay && !IsOnlineRoom()
		&& CurrentScreen != EChaosImpactScreen::TeamSelect && CurrentScreen != EChaosImpactScreen::MatchEnd
		&& !(bTitleDemoWorld && CurrentScreen == EChaosImpactScreen::Title)));
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
		if (const auto* CameraCharacter = Cast<AChaosImpactCharacter>(GetPawn()))
		{
			RefreshGameplayMouseCapture(CameraCharacter->IsShoulderViewActive());
		}
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
	// A pause screen is only reachable from an active play session (or back from a screen opened from it).
	if (NewScreen == EChaosImpactScreen::Pause && !IsGameplayActive()
		&& CurrentScreen != EChaosImpactScreen::TrainingSettings
		&& !(CurrentScreen == EChaosImpactScreen::Settings && SettingsReturnScreen == EChaosImpactScreen::Pause))
	{
		return;
	}
	if (bTrainingMode && IsPrimaryLocalPlayerController()
		&& (NewScreen == EChaosImpactScreen::ModeSelect || NewScreen == EChaosImpactScreen::Title))
	{
		RemoveSecondaryLocalPlayers();
		StopRoomSearch();
	}
	// The title (with its demo match) lives in its own world: from a battle or solo stage, go back there.
	if (NewScreen == EChaosImpactScreen::Title && !bTitleDemoWorld && GetNetMode() == NM_Standalone
		&& IsPrimaryLocalPlayerController())
	{
		OpenTitleLevel();
		return;
	}
	CurrentScreen = NewScreen;
	MenuWidget->ShowScreen(NewScreen);
	ApplyScreenInput();
	if (TitleDemo)
	{
		TitleDemo->SetFilming(NewScreen == EChaosImpactScreen::Title);
	}
}

UChaosImpactResultsView* AChaosImpactPlayerController::GetResultsView() const
{
	// The primary player's announcer covers the whole screen; the other local players share it.
	const AChaosImpactPlayerController* Primary = this;
	if (!IsPrimaryLocalPlayerController())
	{
		const UGameInstance* GameInstance = GetGameInstance();
		const ULocalPlayer* First = GameInstance && !GameInstance->GetLocalPlayers().IsEmpty() ? GameInstance->GetLocalPlayers()[0] : nullptr;
		Primary = First ? Cast<AChaosImpactPlayerController>(First->GetPlayerController(GetWorld())) : nullptr;
	}
	const UChaosImpactMatchAnnouncerWidget* Announcer = Primary ? Cast<UChaosImpactMatchAnnouncerWidget>(Primary->MatchAnnouncer) : nullptr;
	return Announcer ? Announcer->GetResultsView() : nullptr;
}

void AChaosImpactPlayerController::ReturnToLobbyFromResults()
{
	if (UChaosImpactResultsView* Results = GetResultsView())
	{
		Results->Leave();
	}
	// Every player on this machine is back.
	if (const UGameInstance* GameInstance = GetGameInstance())
	{
		for (const ULocalPlayer* LocalPlayer : GameInstance->GetLocalPlayers())
		{
			if (AChaosImpactPlayerController* Local = LocalPlayer ? Cast<AChaosImpactPlayerController>(LocalPlayer->GetPlayerController(GetWorld())) : nullptr)
			{
				Local->ServerReturnFromResults();
			}
		}
	}
	EnterPlayingScreen();
}

void AChaosImpactPlayerController::ServerReturnFromResults_Implementation()
{
	if (AChaosImpactGameMode* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<AChaosImpactGameMode>() : nullptr)
	{
		Mode->ReturnFromResults(PlayerState);
	}
}

void AChaosImpactPlayerController::OpenSettings()
{
	if (!IsPrimaryLocalPlayerController() || CurrentScreen == EChaosImpactScreen::Settings)
	{
		return;
	}
	SettingsReturnScreen = CurrentScreen == EChaosImpactScreen::Pause ? EChaosImpactScreen::Pause : EChaosImpactScreen::ModeSelect;
	ShowMenuScreen(EChaosImpactScreen::Settings);
}

void AChaosImpactPlayerController::CloseSettings()
{
	if (CurrentScreen == EChaosImpactScreen::Settings)
	{
		ShowMenuScreen(SettingsReturnScreen);
	}
}

void AChaosImpactPlayerController::TogglePauseMenu()
{
	// Any active local controller may request pause, but the primary controller
	// owns the single full-viewport pause widget and applies the world pause.
	// Solo mode's title takes its own back button; in the demo any button goes back to the title.
	if (bSoloTitle || bDemoPlay)
	{
		return;
	}
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
	if (CurrentScreen == EChaosImpactScreen::Settings)
	{
		if (SettingsReturnScreen == EChaosImpactScreen::Pause)
		{
			CloseSettings();
		}
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

bool AChaosImpactPlayerController::IsTrainingShoulderCameraEnabled() const
{
	if (!ShowsTrainingPauseEntries() || GetNetMode() != NM_Standalone) { return false; }
	const auto* Primary = GetWorld() ? Cast<AChaosImpactPlayerController>(GetWorld()->GetFirstPlayerController()) : nullptr;
	return Primary && Primary->bTrainingShoulderCamera;
}

void AChaosImpactPlayerController::ToggleTrainingShoulderCamera()
{
	if (IsPrimaryLocalPlayerController() && ShowsTrainingPauseEntries() && GetNetMode() == NM_Standalone)
	{
		bTrainingShoulderCamera = !bTrainingShoulderCamera;
	}
}

void AChaosImpactPlayerController::RefreshGameplayMouseCapture(bool bShoulder)
{
	// A pad player must not steal the one shared mouse's capture from another local player.
	if (!IsGameplayActive() || IsUsingGamepad()) { return; }
	const bool bWantCursor = !bShoulder;
	if (bShowMouseCursor == bWantCursor) { return; }
	bShowMouseCursor = bWantCursor;
	bEnableClickEvents = bWantCursor;
	bEnableMouseOverEvents = bWantCursor;
	FInputModeGameOnly Mode;
	Mode.SetConsumeCaptureMouseDown(false);
	SetInputMode(Mode);
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
	// Players who pick characters play; watching is chosen with 観戦 instead.
	PendingMatchRules.bSpectate = false;
	ResetControllerJoinSequence();
	bControllerAssignmentKeepsFlightMode = false;
	ControllerAssignmentReturnScreen = bOnline ? EChaosImpactScreen::OnlinePlayers : EChaosImpactScreen::TrainingSetup;
	ShowMenuScreen(EChaosImpactScreen::ControllerAssignment);
}

void AChaosImpactPlayerController::PrepareRPGControllerAssignment(const int32 LocalPlayerCount)
{
	if (bTravelPending)
	{
		return;
	}
	PlayFlow = EChaosImpactPlayFlow::RPGAdventure;
	RequestedLocalPlayerCount = FMath::Clamp(LocalPlayerCount, 1, 4);
	ResetControllerJoinSequence();
	bControllerAssignmentKeepsFlightMode = false;
	bRPGAssignmentConfirmed = false;
	ControllerAssignmentReturnScreen = EChaosImpactScreen::Playing;
	ShowMenuScreen(EChaosImpactScreen::ControllerAssignment);
}

void AChaosImpactPlayerController::ConfirmControllerAssignments()
{
	if (CurrentScreen != EChaosImpactScreen::ControllerAssignment || bTravelPending
		|| !AreControllerAssignmentsComplete())
	{
		return;
	}
	if (PlayFlow == EChaosImpactPlayFlow::RPGAdventure)
	{
		// The RPG title takes it from here (each player then picks their own hero).
		RPGAssignmentOptions = BuildLocalSetupOptions();
		bRPGAssignmentConfirmed = true;
		ShowMenuScreen(EChaosImpactScreen::Playing);
		return;
	}
	// The players and their devices are known now, so each can pick a character with their own device.
	OpenCharacterSelect();
}

void AChaosImpactPlayerController::OpenCharacterSelect()
{
	if (bTravelPending || !AreControllerAssignmentsComplete())
	{
		return;
	}
	ShowMenuScreen(EChaosImpactScreen::CharacterSelect);
}

void AChaosImpactPlayerController::CancelCharacterSelection()
{
	if (CurrentScreen == EChaosImpactScreen::CharacterSelect && !bTravelPending)
	{
		if (bRoomCharacterChange)
		{
			// Opened from the room's pause menu: back there, nothing changed.
			bRoomCharacterChange = false;
			ShowMenuScreen(EChaosImpactScreen::Pause);
			return;
		}
		ShowMenuScreen(EChaosImpactScreen::ControllerAssignment);
	}
}

int32 AChaosImpactPlayerController::GetLocalPlayerIndexForDevice(const bool bKeyboard, const int32 InputDeviceId) const
{
	const int32 Players = FMath::Clamp(RequestedLocalPlayerCount, 1, 4);
	if (Players == 1)
	{
		// A player alone may use any device.
		return 0;
	}
	if (bKeyboard)
	{
		return RequestedKeyboardPlayerIndex >= 0 && RequestedKeyboardPlayerIndex < Players
			? RequestedKeyboardPlayerIndex : INDEX_NONE;
	}
	const int32 PadIndex = JoinedInputDeviceIds.IndexOfByKey(InputDeviceId);
	if (PadIndex == INDEX_NONE)
	{
		return INDEX_NONE;
	}
	for (int32 PlayerIndex = 0; PlayerIndex < Players; ++PlayerIndex)
	{
		if (PlayerIndex != RequestedKeyboardPlayerIndex && GetPadIndexForPlayer(PlayerIndex) == PadIndex)
		{
			return PlayerIndex;
		}
	}
	return INDEX_NONE;
}

void AChaosImpactPlayerController::ConfirmCharacterSelection()
{
	if (CurrentScreen != EChaosImpactScreen::CharacterSelect || bTravelPending)
	{
		return;
	}
	if (bRoomCharacterChange)
	{
		// In a room: every player of this machine sends their new choice again, and play carries on.
		bRoomCharacterChange = false;
		if (const UGameInstance* GameInstance = GetGameInstance())
		{
			for (const ULocalPlayer* Local : GameInstance->GetLocalPlayers())
			{
				if (AChaosImpactPlayerController* LocalController = Local
					? Cast<AChaosImpactPlayerController>(Local->GetPlayerController(GetWorld())) : nullptr)
				{
					LocalController->bLoadoutSent = false;
				}
			}
		}
		EnterPlayingScreen();
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
		OpenMatchRules(EChaosImpactScreen::CharacterSelect);
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
	PendingMatchRules.bSpectate = false;
	ShowMenuScreen(EChaosImpactScreen::TrainingSetup);
}

void AChaosImpactPlayerController::BeginLocalSpectate()
{
	if (bTravelPending || IsGameplayActive() || CurrentScreen == EChaosImpactScreen::Title)
	{
		return;
	}
	// One viewer on one screen, with any device: no controller assignment and no characters to pick.
	PlayFlow = EChaosImpactPlayFlow::VersusLocal;
	bTrainingTargetsEnabled = false;
	TrainingCPUCount = 0;
	RequestedLocalPlayerCount = 1;
	RequestedKeyboardPlayerIndex = 0;
	JoinedInputDeviceIds.Reset();
	JoinedLegacyControllerIds.Reset();
	PendingMatchRules.bSpectate = true;
	PendingMatchRules.CPUCount = FMath::Max(PendingMatchRules.CPUCount, 4);
	OpenMatchRules(EChaosImpactScreen::TrainingSetup);
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

int32 AChaosImpactPlayerController::GetTrainingTeam(const bool bCPU, const int32 Index) const
{
	return (bCPU ? TrainingCPUTeams : TrainingHumanTeams)[FMath::Clamp(Index, 0, 3)];
}

void AChaosImpactPlayerController::ToggleTrainingTeams()
{
	if (!ShowsTrainingPauseEntries() || !IsPrimaryLocalPlayerController()) { return; }
	bTrainingTeams = !bTrainingTeams;
	ApplyTrainingTeams();
}

void AChaosImpactPlayerController::CycleTrainingTeam(const bool bCPU, const int32 Index, const int32 Direction)
{
	if (!ShowsTrainingPauseEntries() || !IsPrimaryLocalPlayerController() || Index < 0 || Index > 3) { return; }
	int32& Team = (bCPU ? TrainingCPUTeams : TrainingHumanTeams)[Index];
	Team = (Team + (Direction >= 0 ? 1 : 3)) % 4;
	ApplyTrainingTeams();
}

void AChaosImpactPlayerController::ApplyTrainingTeams()
{
	if (!GetWorld() || !ShowsTrainingPauseEntries() || !IsPrimaryLocalPlayerController()) { return; }
	const UGameInstance* GI = GetGameInstance();
	for (TActorIterator<AChaosImpactCharacter> It(GetWorld()); It; ++It)
	{
		int32 Team = INDEX_NONE;
		if (It->GetCPUNumber() > 0 && It->GetCPUNumber() <= 4)
		{ Team = bTrainingTeams ? TrainingCPUTeams[It->GetCPUNumber()-1] : INDEX_NONE; }
		else if (const auto* LocalPC = Cast<APlayerController>(It->GetController()); LocalPC && GI && LocalPC->GetLocalPlayer())
		{
			const int32 Slot = GI->GetLocalPlayers().IndexOfByKey(LocalPC->GetLocalPlayer());
			if (Slot < 0 || Slot > 3) { continue; }
			Team = bTrainingTeams ? TrainingHumanTeams[Slot] : INDEX_NONE;
		}
		else { continue; } // Placed solo enemies keep the editor's own team settings.
		It->SoloTeam = Team;
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

void AChaosImpactPlayerController::SetTrainingSummonBallType(const EChaosImpactBallType Type)
{
	if (bTrainingMode && !bTravelPending && static_cast<int32>(Type) < ChaosImpactBallTypes::Count)
	{ TrainingSummonBallType = Type; }
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
		if (bRenamingPlayer)
		{
			// In a room: the new name goes to everyone (this machine's second player becomes "<name>(2)" too).
			bRenamingPlayer = false;
			ServerSetPlayerName(Sessions->GetPlayerName());
			EnterPlayingScreen();
			return;
		}
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

bool AChaosImpactPlayerController::CanChangeLoadoutInRoom() const
{
	const AChaosImpactGameState* Room = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	return IsOnlineRoom() && IsPrimaryLocalPlayerController() && !bTravelPending && Room
		&& Room->Phase == EChaosImpactOnlinePhase::Lobby && !Room->IsMatchmade();
}

void AChaosImpactPlayerController::BeginRoomCharacterChange()
{
	if (CanChangeLoadoutInRoom())
	{
		bRoomCharacterChange = true;
		ShowMenuScreen(EChaosImpactScreen::CharacterSelect);
	}
}

void AChaosImpactPlayerController::BeginRoomPlayerRename()
{
	if (CanChangeLoadoutInRoom())
	{
		bRenamingPlayer = true;
		ShowMenuScreen(EChaosImpactScreen::OnlineName);
	}
}

void AChaosImpactPlayerController::CancelRoomPlayerRename()
{
	bRenamingPlayer = false;
	ShowMenuScreen(EChaosImpactScreen::Pause);
}

bool AChaosImpactPlayerController::CanRenameRoom() const
{
	const AChaosImpactGameState* Room = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	return IsOnlineRoomHost() && Room && !Room->bVersusMatch && Room->Phase == EChaosImpactOnlinePhase::Lobby
		&& !Room->IsMatchmade();
}

bool AChaosImpactPlayerController::CanReopenRecruitment() const
{
	const AChaosImpactGameState* Room = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	return IsOnlineRoomHost() && Room && Room->bRecruitmentClosed && !Room->bVersusMatch
		&& Room->Phase == EChaosImpactOnlinePhase::Lobby && !Room->IsMatchmade();
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
		&& Room->Phase == EChaosImpactOnlinePhase::Lobby && !IsSpectating();
}

bool AChaosImpactPlayerController::CanToggleSpectating() const
{
	const AChaosImpactGameState* Room = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	return IsOnlineRoom() && Room && !Room->bVersusMatch && Room->Phase == EChaosImpactOnlinePhase::Lobby;
}

void AChaosImpactPlayerController::ToggleSpectating()
{
	if (!IsLocalController() || !CanToggleSpectating() || !IsGameplayActive())
	{
		return;
	}
	ServerSetSpectating(!IsSpectating());
}

void AChaosImpactPlayerController::ServerSetSpectating_Implementation(const bool bSpectate)
{
	if (AChaosImpactGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AChaosImpactGameMode>() : nullptr)
	{
		GameMode->SetMemberSpectating(this, bSpectate);
	}
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

void AChaosImpactPlayerController::UpdateRecruitmentHold(const float DeltaSeconds)
{
	if (!IsLocalController() || !IsPrimaryLocalPlayerController()) { return; }
	const bool bDown = ChaosImpactSettings::IsActionDown(this, EChaosImpactAction::LobbyReady);
	if (!bDown)
	{
		RecruitmentHeldSeconds = 0.f;
		bRecruitmentHoldNeedsRelease = false;
		return;
	}
	const bool bFocused = !FSlateApplication::IsInitialized() || FSlateApplication::Get().IsActive();
	if (!bFocused || !IsGameplayActive() || !CanCloseRecruitment())
	{
		RecruitmentHeldSeconds = 0.f;
		bRecruitmentHoldNeedsRelease = true;
		return;
	}
	if (bRecruitmentHoldNeedsRelease) { return; }
	// A frame hitch or returning from the background must not complete a hold instantly.
	RecruitmentHeldSeconds += FMath::Clamp(DeltaSeconds, 0.f, .1f);
	if (RecruitmentHeldSeconds >= RecruitmentHoldSeconds)
	{
		RecruitmentHeldSeconds = 0.f;
		bRecruitmentHoldNeedsRelease = true;
		CloseRecruitment();
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
		// A matched room already being opened is left alone; a search is stopped.
		if (!IsOnlineRoom() && !(Sessions->IsQueueMatched() && Sessions->GetState() == EChaosImpactRoomState::Creating))
		{
			Sessions->CancelQueue();
		}
	}
}

bool AChaosImpactPlayerController::IsSearchingForRoom() const
{
	const UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this);
	return Sessions && (Sessions->GetState() == EChaosImpactRoomState::Searching
		|| Sessions->GetState() == EChaosImpactRoomState::Joining || (Sessions->IsQueueing() && !IsOnlineRoom()));
}

bool AChaosImpactPlayerController::CanCloseRecruitment() const
{
	const AChaosImpactGameState* RoomState = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	// A room of one machine has nobody to play with (or watch) yet.
	return IsOnlineRoomHost() && RoomState && !RoomState->bRecruitmentClosed && !RoomState->IsMatchmade()
		&& RoomState->Phase == EChaosImpactOnlinePhase::Lobby && RoomState->CountMachines() + RoomState->CountSpectators() >= 2;
}

void AChaosImpactPlayerController::BeginMatchQueue(const EChaosImpactQueueMode Mode)
{
	UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this);
	if (!Sessions || bTravelPending || IsOnlineRoom())
	{
		return;
	}
	// The players and controllers chosen on the assignment screen come along into the match.
	Sessions->SetLocalSetup(BuildLocalSetupOptions(), RequestedLocalPlayerCount);
	if (!Sessions->StartQueue(Mode))
	{
		// (The notice says why; the menu stays.)
		if (MenuWidget)
		{
			MenuWidget->RefreshEntries();
		}
		return;
	}
	PlayFlow = EChaosImpactPlayFlow::VersusOnline;
	// The wait is spent in this machine's own training: the opponents are met only in the match.
	if (bTrainingMode && GetWorld() && GetWorld()->URL.HasOption(TEXT("CIOnlineSearch=1")))
	{
		EnterPlayingScreen();
		return;
	}
	bTrainingTargetsEnabled = true;
	TrainingCPUCount = 0;
	OpenTrainingLevel(false, true, true);
}

void AChaosImpactPlayerController::OpenRankTop()
{
	if (UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this))
	{
		Sessions->RefreshRankInfo();
	}
	PlayFlow = EChaosImpactPlayFlow::VersusOnline;
	ShowMenuScreen(EChaosImpactScreen::RankTop);
}

void AChaosImpactPlayerController::ClientLeaveMatchmade_Implementation()
{
	// Every local player of the machine hears it; its first player takes the machine home.
	const UGameInstance* GameInstance = GetGameInstance();
	const ULocalPlayer* First = GameInstance && !GameInstance->GetLocalPlayers().IsEmpty() ? GameInstance->GetLocalPlayers()[0] : nullptr;
	AChaosImpactPlayerController* Primary = First ? Cast<AChaosImpactPlayerController>(First->GetPlayerController(GetWorld())) : this;
	UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this);
	if (!Primary || Primary->bTravelPending || !Sessions)
	{
		return;
	}
	Primary->bTravelPending = true;
	Sessions->FinishMatchmade();
}

void AChaosImpactPlayerController::UpdateMatchmadeProgress()
{
	const AChaosImpactGameState* Match = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this);
	if (!Match || !Sessions || !Match->IsMatchmade() || !IsLocalController() || !IsPrimaryLocalPlayerController())
	{
		return;
	}
	if (Match->bVersusMatch && (Match->Phase == EChaosImpactOnlinePhase::Intro || Match->Phase == EChaosImpactOnlinePhase::Match))
	{
		Sessions->NotifyMatchmadeMatchStarted();
	}
	// The duel decided: this machine says how it went for its player (once; the relay checks both sides agree).
	if (Match->Matchmade == EChaosImpactMatchmade::Rank && Match->IsDuel() && Match->Phase == EChaosImpactOnlinePhase::Results
		&& Match->Duel.Step == EChaosImpactDuelStep::Finished)
	{
		const int32 Side = Match->Duel.SideOf(PlayerState);
		if (Side != INDEX_NONE)
		{
			Sessions->ReportRankResult(Match->Duel.MatchWinner == INDEX_NONE ? TEXT("draw")
				: Match->Duel.MatchWinner == Side ? TEXT("win") : TEXT("lose"));
		}
	}
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

void AChaosImpactPlayerController::OpenTitleLevel()
{
	const FString MapPackage = TrainingLevel.GetLongPackageName();
	if (MapPackage.IsEmpty() || bTravelPending)
	{
		return;
	}
	bTravelPending = true;
	ExitTrainingOverlayPresentation();
	SetPause(false);
	bIntroFullScreen = false;
	if (UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
	{
		Viewport->SetForceDisableSplitscreen(false);
	}
	if (UChaosImpactLoadingSubsystem* Loading = UChaosImpactLoadingSubsystem::Get(this))
	{
		Loading->BeginTravel(EChaosImpactLoadingKind::Title);
	}
	// No options: the same title world the game starts in.
	UGameplayStatics::OpenLevel(this, FName(*MapPackage), true);
}

void AChaosImpactPlayerController::OpenTrainingLevel(const bool bKeepFlightMode, const bool bOnlineSearch, const bool bLoadingScreen)
{
	FString MapPackage = TrainingLevel.GetLongPackageName();
	// A local VS match goes to its stage's own level (the training level raises a stage only while that is missing).
	if (PlayFlow == EChaosImpactPlayFlow::VersusLocal && bLocalMatchRulesChosen && !bOnlineSearch && !PendingMatchRules.bDuel)
	{
		const FString StageLevel = ChaosImpactMatch::GetStageLevel(PendingMatchRules.StageIndex);
		if (FPackageName::DoesPackageExist(StageLevel))
		{
			MapPackage = StageLevel;
		}
	}
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
	if (bTrainingTeams && PlayFlow != EChaosImpactPlayFlow::VersusLocal)
	{
		Options += TEXT("?CITrainingTeams=1");
		for (int32 I = 0; I < 4; ++I)
		{ Options += FString::Printf(TEXT("?CIHumanTeam%d=%d?CICPUTeam%d=%d"), I, TrainingHumanTeams[I], I, TrainingCPUTeams[I]); }
	}
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
		if (bDemoTravel)
		{
			Options += TEXT("?CIDemo=1");
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
	// From the title, the load is covered (a retry or a settings change in training just reloads).
	if (bLoadingScreen && bTitleDemoWorld)
	{
		if (UChaosImpactLoadingSubsystem* Loading = UChaosImpactLoadingSubsystem::Get(this))
		{
			Loading->BeginTravel(EChaosImpactLoadingKind::Training);
		}
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
		&& Match->bRecruitmentClosed && !Match->IsMatchmade();
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
	if (IsVersusMatchWorld() && MatchRulesReturnScreen != EChaosImpactScreen::ControllerAssignment
		&& MatchRulesReturnScreen != EChaosImpactScreen::CharacterSelect)
	{
		const UGameInstance* GameInstance = GetGameInstance();
		return GameInstance ? FMath::Max(1, GameInstance->GetLocalPlayers().Num()) : 1;
	}
	return FMath::Clamp(RequestedLocalPlayerCount, 1, 4);
}

void AChaosImpactPlayerController::SetSpectateTimeStopped(const bool bStop)
{
	if (bStop && (IsOnlineRoom() || !IsSpectating() || !IsLocalController()))
	{
		return;
	}
	if (bSpectateTimeStopped == bStop)
	{
		return;
	}
	bSpectateTimeStopped = bStop;
	// The camera manager keeps updating while the world is paused, so the camera still flies.
	bShouldPerformFullTickWhenPaused = bStop;
	if (IsGameplayActive())
	{
		SetPause(bStop);
	}
	UE_LOG(LogChaosImpact, Log, TEXT("Spectator time %s"), bStop ? TEXT("stopped") : TEXT("running again"));
}

void AChaosImpactPlayerController::SetGameplayUIHidden(const bool bHide)
{
	if (IsPrimaryLocalPlayerController() && MatchAnnouncer)
	{
		MatchAnnouncer->SetVisibility(bHide ? ESlateVisibility::Hidden : ESlateVisibility::HitTestInvisible);
	}
}

bool AChaosImpactPlayerController::IsSpectating() const
{
	const AChaosImpactPlayerState* Member = GetPlayerState<AChaosImpactPlayerState>();
	return Member && Member->bSpectating;
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
	if (Row == 0 && !Rules.bCrashBall && !Rules.bDuel)
	{
		// 3 → 4 → 5 minutes (クラッシュボール is always five).
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
		// Existing rules plus the objective team battle and the duel (ランクマッチ's rules), with the same
		// left/right selector. The duel is offered to one or two players only.
		static constexpr int32 Modes[] = {0, 2, 3, 4, 2, 0};
		const int32 Count = ChaosImpactMatch::GetCompetingHumans(Rules, Humans) <= 2 ? 6 : 5;
		const int32 Current = Rules.bDuel ? 5 : Rules.bCrashBall ? 4 : (Rules.TeamCount >= 2 ? Rules.TeamCount - 1 : 0);
		const int32 Next = Wrap(Current + Direction, Count);
		Rules.bCrashBall = Next == 4;
		Rules.bDuel = Next == 5;
		Rules.TeamCount = Modes[Next];
	}
	else if (Row == 2 && !Rules.bDuel)
	{
		const int32 Competing = ChaosImpactMatch::GetCompetingHumans(Rules, Humans);
		const int32 Minimum = ChaosImpactMatch::GetMinCPUCount(Competing, Rules.TeamCount);
		const int32 Span = ChaosImpactMatch::GetMaxCPUCount(Competing) - Minimum + 1;
		Rules.CPUCount = Minimum + Wrap(Rules.CPUCount - Minimum + Direction, FMath::Max(Span, 1));
	}
	else if (Row == 3 && Rules.CPUCount > 0)
	{
		// よわい → ふつう → つよい.
		Rules.CPULevel = Wrap(Rules.CPULevel + Direction, ChaosImpactMatch::CPULevelCount);
	}

	Rules = ChaosImpactMatch::Sanitize(Rules, Humans);
}

void AChaosImpactPlayerController::OpenStageSelect()
{
	PendingMatchRules = ChaosImpactMatch::Sanitize(PendingMatchRules, GetMatchHumanCount());
	// The duel has its own arena: straight to the match.
	if (PendingMatchRules.bDuel)
	{
		ConfirmMatchRules();
		return;
	}
	ShowMenuScreen(EChaosImpactScreen::StageSelect);
}

void AChaosImpactPlayerController::ChooseStage(const int32 StageIndex)
{
	PendingMatchRules.StageIndex = ChaosImpactMatch::SanitizeStage(StageIndex);
	ConfirmMatchRules();
}

void AChaosImpactPlayerController::CancelStageSelect()
{
	// Back to the rules as they were left; where the rules return to is unchanged.
	ShowMenuScreen(EChaosImpactScreen::MatchRules);
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
	const bool bNewLocalEntry = MatchRulesReturnScreen == EChaosImpactScreen::ControllerAssignment
		|| MatchRulesReturnScreen == EChaosImpactScreen::CharacterSelect
		|| MatchRulesReturnScreen == EChaosImpactScreen::TrainingSetup;
	if (GameMode && !IsOnlineRoomHost() && IsVersusMatchWorld() && !bNewLocalEntry && PlayFlow == EChaosImpactPlayFlow::VersusLocal)
	{
		// A rematch on another stage: over to that stage's own level (the same stage restarts in place below).
		// The duel's arena is raised in the training level, so a duel goes there from a stage's own level.
		const FString StageLevel = PendingMatchRules.bDuel ? TrainingLevel.GetLongPackageName()
			: ChaosImpactMatch::GetStageLevel(PendingMatchRules.StageIndex);
		if (FPackageName::DoesPackageExist(StageLevel)
			&& UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName()) != StageLevel)
		{
			bLocalMatchRulesChosen = true;
			OpenTrainingLevel(false);
			return;
		}
	}
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
		PlayVersusCardThenOpen();
	}
}

void AChaosImpactPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
	{
		if (VersusCard.IsValid())
		{
			Viewport->RemoveViewportWidgetContent(VersusCard.ToSharedRef());
		}
		if (VersusReveal.IsValid())
		{
			Viewport->RemoveViewportWidgetContent(VersusReveal.ToSharedRef());
		}
	}
	VersusCard.Reset();
	VersusReveal.Reset();
	Super::EndPlay(EndPlayReason);
}

FChaosImpactVersusCardInfo AChaosImpactPlayerController::BuildVersusCardInfo() const
{
	FChaosImpactVersusCardInfo Info;
	const FChaosImpactMatchRules& Rules = PendingMatchRules;
	const UChaosImpactLoadoutSubsystem* Loadouts = UChaosImpactLoadoutSubsystem::Get(this);
	if (!Rules.bSpectate)
	{
		for (int32 Human = 0; Human < GetMatchHumanCount(); ++Human)
		{
			const FChaosImpactLoadout Loadout = Loadouts ? Loadouts->GetLoadout(Human) : FChaosImpactLoadout();
			FChaosImpactVersusEntrant Entrant;
			Entrant.Label = FString::Printf(TEXT("P%d"), Human + 1);
			Entrant.Name = ChaosImpactRoster::Get(Loadout.Character).Name;
			const bool bColour = Loadout.Colour >= 0;
			Entrant.Detail = bColour ? ChaosImpactRoster::ColourNames[FMath::Clamp(Loadout.Colour, 0, ChaosImpactRoster::ColourCount - 1)] : TEXT("");
			Entrant.Color = bColour ? ChaosImpactRoster::GetColourSwatch(Loadout.Colour) : ChaosImpactPaint::PlayerAccents[Human % 4];
			Info.Entrants.Add(Entrant);
		}
	}
	for (int32 CPU = 0; CPU < Rules.CPUCount; ++CPU)
	{
		FChaosImpactVersusEntrant Entrant;
		Entrant.Label = FString::Printf(TEXT("CPU%d"), CPU + 1);
		Entrant.Name = ChaosImpactMatch::GetCPULevelName(Rules.CPULevel);
		Entrant.Color = Rules.CPULevel == ChaosImpactMatch::CPULevelStrongest ? ChaosImpactPaint::Violet : FLinearColor(0.7f, 0.74f, 0.82f);
		Entrant.bCPU = true;
		Info.Entrants.Add(Entrant);
	}
	Info.Footer = FString::Printf(TEXT("%s ・ %s ・ %d分%s"), *ChaosImpactMatch::GetStageLabel(Rules.StageIndex),
		*ChaosImpactMatch::DescribeRule(Rules), Rules.Minutes, Rules.bSpectate ? TEXT(" ・ 観戦") : TEXT(""));
	return Info;
}

void AChaosImpactPlayerController::PlayVersusCardThenOpen()
{
	UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
	if (VersusCard.IsValid())
	{
		return;
	}
	if (!Viewport)
	{
		OpenTrainingLevel(false);
		return;
	}
	FChaosImpactVersusCardInfo Info = BuildVersusCardInfo();
	Info.StartedAt = FPlatformTime::Seconds();
	// Each player's character in their colour, filmed far outside the level for their card (CPUs pick theirs in the
	// VS level, so theirs shows a "?").
	const UChaosImpactLoadoutSubsystem* Loadouts = UChaosImpactLoadoutSubsystem::Get(this);
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Parameters.ObjectFlags |= RF_Transient;
	int32 Human = 0;
	for (FChaosImpactVersusEntrant& Entrant : Info.Entrants)
	{
		if (Entrant.bCPU)
		{
			continue;
		}
		if (AChaosImpactCharacterPreview* Preview = GetWorld()->SpawnActor<AChaosImpactCharacterPreview>(
			AChaosImpactCharacterPreview::StaticClass(), FVector(Human * 1600.0f, -84000.0f, 40000.0f), FRotator::ZeroRotator, Parameters))
		{
			const FChaosImpactLoadout Loadout = Loadouts ? Loadouts->GetLoadout(Human) : FChaosImpactLoadout();
			Preview->ShowLoadout(Loadout.Character, FMath::Max(Loadout.Colour, 0), Entrant.Color);
			Entrant.Picture = Preview->GetPicture();
			VersusCardPreviews.Add(Preview);
		}
		++Human;
	}
	// The menus stop answering while the card plays.
	bTravelPending = true;
	VersusCard = SNew(SChaosImpactVersusCard).Info(Info);
	Viewport->AddViewportWidgetContent(VersusCard.ToSharedRef(), 10000);
	// Menus pause the world (and its timers), so the card runs on the core ticker, every frame.
	const TSharedRef<bool> bStruckPose = MakeShared<bool>(false);
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this, Info, bStruckPose](const float DeltaTime)
	{
		const float Elapsed = static_cast<float>(FPlatformTime::Seconds() - Info.StartedAt);
		for (AChaosImpactCharacterPreview* Preview : VersusCardPreviews)
		{
			if (IsValid(Preview))
			{
				// A paused world does not animate the models by itself.
				Preview->Animate(DeltaTime);
				if (!*bStruckPose && Elapsed >= SChaosImpactVersusCard::VersusLandsAt)
				{
					Preview->PlayReady();
				}
			}
		}
		*bStruckPose = *bStruckPose || Elapsed >= SChaosImpactVersusCard::VersusLandsAt;
		if (Elapsed < SChaosImpactVersusCard::IntroSeconds)
		{
			return true;
		}
		// The pictures keep their last frame and leave with the card (the previews go with this level).
		for (AChaosImpactCharacterPreview* Preview : VersusCardPreviews)
		{
			if (IsValid(Preview))
			{
				Preview->ReleasePicture();
			}
		}
		VersusCardPreviews.Reset();
		// The same card, carrying on from where it is, is the VS level's loading screen. (Playing in the editor
		// has no loading screen: the card's last frame simply stays on screen while the level loads.)
		FLoadingScreenAttributes Loading;
		Loading.WidgetLoadingScreen = SNew(SChaosImpactVersusCard).Info(Info);
		Loading.bAutoCompleteWhenLoadingCompletes = true;
		Loading.MinimumLoadingScreenDisplayTime = 0.0f;
		GetMoviePlayer()->SetupLoadingScreen(Loading);
		// Off the viewport now (it would outlive the level); its last frame stays up while the level loads.
		if (UGameViewportClient* CardViewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr; CardViewport && VersusCard.IsValid())
		{
			CardViewport->RemoveViewportWidgetContent(VersusCard.ToSharedRef());
		}
		VersusCard.Reset();
		bTravelPending = false;
		OpenTrainingLevel(false, false, false);
		return false;
	}));
}

FChaosImpactVersusCardInfo AChaosImpactPlayerController::BuildOnlineVersusCardInfo() const
{
	FChaosImpactVersusCardInfo Info;
	const AChaosImpactGameState* Match = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	if (!Match)
	{
		return Info;
	}
	TArray<AChaosImpactPlayerState*> Fighters = Match->GetCompetitors(true);
	if (Match->IsTeamBattle())
	{
		// Team by team, so each side of the card is one team.
		Fighters.StableSort([](const AChaosImpactPlayerState& A, const AChaosImpactPlayerState& B) { return A.TeamIndex < B.TeamIndex; });
	}
	for (const AChaosImpactPlayerState* Member : Fighters)
	{
		FChaosImpactVersusEntrant Entrant;
		const bool bColour = Member->ColourChoice >= 0;
		Entrant.Label = Member->GetPlayerName();
		Entrant.Name = ChaosImpactRoster::Get(Member->CharacterIndex).Name;
		Entrant.Detail = Member->IsABot() ? FString(TEXT("CPU"))
			: bColour ? FString(ChaosImpactRoster::ColourNames[FMath::Clamp(Member->ColourChoice, 0, ChaosImpactRoster::ColourCount - 1)])
			: FString();
		Entrant.Color = bColour ? ChaosImpactRoster::GetColourSwatch(Member->ColourChoice)
			: ChaosImpactPaint::PlayerAccents[Info.Entrants.Num() % 4];
		Info.Entrants.Add(Entrant);
	}
	const FChaosImpactMatchRules& Rules = Match->Rules;
	Info.Footer = FString::Printf(TEXT("%s ・ %s ・ %d分 ・ %s"), *ChaosImpactMatch::GetStageLabel(Rules.StageIndex),
		*ChaosImpactMatch::DescribeRule(Rules), Rules.Minutes, *Match->RoomName);
	return Info;
}

void AChaosImpactPlayerController::UpdateOnlineVersusCard(const float DeltaSeconds)
{
	const AChaosImpactGameState* Match = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
	if (!IsPrimaryLocalPlayerController() || !Match || !Viewport)
	{
		return;
	}
	const bool bOpening = Match->bOnlineRoom && Match->bVersusMatch && Match->Phase == EChaosImpactOnlinePhase::Intro;
	// How far into the card this screen is (the opening's own clock runs from the card's end).
	const float CardElapsed = bOpening ? Match->GetIntroElapsedSeconds() + ChaosImpactMatch::OnlineCardSeconds : 0.0f;
	if (bOpening && !bOnlineCardUp && OnlineCardShownFor != Match->PhaseStartedAt
		&& CardElapsed < ChaosImpactMatch::OnlineCardSeconds - 0.3f && !VersusCard.IsValid())
	{
		OnlineCardShownFor = Match->PhaseStartedAt;
		FChaosImpactVersusCardInfo Info = BuildOnlineVersusCardInfo();
		// Every copy plays from the same start (a late arrival joins it part-way).
		Info.StartedAt = FPlatformTime::Seconds() - CardElapsed;
		FActorSpawnParameters Parameters;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Parameters.ObjectFlags |= RF_Transient;
		const TArray<AChaosImpactPlayerState*> Fighters = Match->GetCompetitors(true);
		for (int32 Index = 0; Index < Info.Entrants.Num(); ++Index)
		{
			// Each fighter's character in their colour, filmed far outside the stage.
			FChaosImpactVersusEntrant& Entrant = Info.Entrants[Index];
			const AChaosImpactPlayerState* Member = nullptr;
			for (const AChaosImpactPlayerState* Candidate : Fighters)
			{
				Member = !Member && Candidate->GetPlayerName() == Entrant.Label ? Candidate : Member;
			}
			if (!Member)
			{
				continue;
			}
			if (AChaosImpactCharacterPreview* Preview = GetWorld()->SpawnActor<AChaosImpactCharacterPreview>(
				AChaosImpactCharacterPreview::StaticClass(), FVector(Index * 1600.0f, -84000.0f, 40000.0f), FRotator::ZeroRotator, Parameters))
			{
				Preview->ShowLoadout(Member->CharacterIndex, FMath::Max(Member->ColourChoice, 0), Entrant.Color);
				Entrant.Picture = Preview->GetPicture();
				VersusCardPreviews.Add(Preview);
			}
		}
		VersusCard = SNew(SChaosImpactVersusCard).Info(Info);
		Viewport->AddViewportWidgetContent(VersusCard.ToSharedRef(), 10000);
		bOnlineCardUp = true;
		bOnlineCardPosed = false;
		UE_LOG(LogChaosImpact, Log, TEXT("VS card shown online (%d fighters, %.2f s in)"), Info.Entrants.Num(), CardElapsed);
	}
	if (!bOnlineCardUp)
	{
		return;
	}
	for (AChaosImpactCharacterPreview* Preview : VersusCardPreviews)
	{
		if (IsValid(Preview))
		{
			Preview->Animate(DeltaSeconds);
			if (!bOnlineCardPosed && CardElapsed >= SChaosImpactVersusCard::VersusLandsAt)
			{
				Preview->PlayReady();
			}
		}
	}
	bOnlineCardPosed = bOnlineCardPosed || CardElapsed >= SChaosImpactVersusCard::VersusLandsAt;
	if (bOpening && CardElapsed < ChaosImpactMatch::OnlineCardSeconds)
	{
		return;
	}
	// The card is over (or the match went): the stage shows through the VS emblem as it opens from the middle.
	bOnlineCardUp = false;
	for (AChaosImpactCharacterPreview* Preview : VersusCardPreviews)
	{
		if (IsValid(Preview))
		{
			Preview->ReleasePicture();
			Preview->Destroy();
		}
	}
	VersusCardPreviews.Reset();
	if (VersusCard.IsValid())
	{
		Viewport->RemoveViewportWidgetContent(VersusCard.ToSharedRef());
		VersusCard.Reset();
	}
	if (bOpening && !VersusReveal.IsValid())
	{
		VersusReveal = SNew(SChaosImpactVersusReveal);
		Viewport->AddViewportWidgetContent(VersusReveal.ToSharedRef(), 10000);
		VersusRevealDeadline = FPlatformTime::Seconds();
		GetWorldTimerManager().SetTimer(VersusRevealTimer, this, &AChaosImpactPlayerController::UpdateVersusReveal, 0.02f, true);
	}
}

void AChaosImpactPlayerController::UpdateVersusReveal()
{
	if (!VersusReveal.IsValid())
	{
		GetWorldTimerManager().ClearTimer(VersusRevealTimer);
		return;
	}
	// Open as the match's opening (or team select) begins, so the training arena underneath never shows.
	const AChaosImpactGameState* Match = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	const bool bStarted = Match && Match->bVersusMatch && (Match->Phase == EChaosImpactOnlinePhase::Intro
		|| Match->Phase == EChaosImpactOnlinePhase::TeamSelect || Match->Phase == EChaosImpactOnlinePhase::Match);
	if (!VersusReveal->IsOpening() && (bStarted || FPlatformTime::Seconds() > VersusRevealDeadline))
	{
		VersusReveal->Open();
	}
	if (VersusReveal->IsFinished())
	{
		if (UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
		{
			Viewport->RemoveViewportWidgetContent(VersusReveal.ToSharedRef());
		}
		VersusReveal.Reset();
		GetWorldTimerManager().ClearTimer(VersusRevealTimer);
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

void AChaosImpactPlayerController::SendLoadout()
{
	AChaosImpactPlayerState* State = GetPlayerState<AChaosImpactPlayerState>();
	const UChaosImpactLoadoutSubsystem* Loadouts = UChaosImpactLoadoutSubsystem::Get(this);
	const UGameInstance* GameInstance = GetGameInstance();
	if (bLoadoutSent || !IsLocalController() || !State || !Loadouts || !GameInstance || !GetLocalPlayer())
	{
		return;
	}
	const int32 LocalIndex = FMath::Max(0, GameInstance->GetLocalPlayers().IndexOfByKey(GetLocalPlayer()));
	const FChaosImpactLoadout Loadout = Loadouts->GetLoadout(LocalIndex);
	bLoadoutSent = true;
	if (HasAuthority())
	{
		ServerSetLoadout_Implementation(Loadout.Character, Loadout.Colour);
	}
	else
	{
		ServerSetLoadout(Loadout.Character, Loadout.Colour);
	}
}

void AChaosImpactPlayerController::ServerSetLoadout_Implementation(const int32 InCharacterIndex, const int32 InColour)
{
	if (AChaosImpactPlayerState* State = GetPlayerState<AChaosImpactPlayerState>())
	{
		State->CharacterIndex = ChaosImpactRoster::ClampIndex(InCharacterIndex);
		State->ColourChoice = FMath::Clamp(InColour, 0, ChaosImpactRoster::ColourCount - 1);
		State->ForceNetUpdate();
		UE_LOG(LogChaosImpact, Log, TEXT("Loadout set for %s: character %d, colour %d"), *State->GetPlayerName(),
			State->CharacterIndex, State->ColourChoice);
	}
}

float AChaosImpactPlayerController::GetDemoIdleSeconds()
{
	static const float Seconds = []()
	{
		float Value = 30.0f;
		FParse::Value(FCommandLine::Get(), TEXT("CIDemoIdle="), Value);
		return Value;
	}();
	return Seconds;
}

void AChaosImpactPlayerController::StartDemoPlay()
{
	if (bTravelPending || !bTitleDemoWorld || !IsPrimaryLocalPlayerController())
	{
		return;
	}
	// A CPUs-only free-for-all to watch, on a stage at random (the characters are picked at random as CPUs join).
	PlayFlow = EChaosImpactPlayFlow::VersusLocal;
	bTrainingTargetsEnabled = false;
	TrainingCPUCount = 0;
	RequestedLocalPlayerCount = 1;
	RequestedKeyboardPlayerIndex = 0;
	JoinedInputDeviceIds.Reset();
	JoinedLegacyControllerIds.Reset();
	FChaosImpactMatchRules Rules;
	Rules.Minutes = 5;
	Rules.TeamCount = 0;
	Rules.bSpectate = true;
	Rules.CPUCount = FMath::RandRange(4, 6);
	Rules.StageIndex = FMath::RandRange(0, ChaosImpactMatch::StageCount - 1);
	Rules.CPULevel = 2;
	PendingMatchRules = ChaosImpactMatch::Sanitize(Rules, 1);
	bLocalMatchRulesChosen = true;
	bDemoTravel = true;
	UE_LOG(LogChaosImpact, Log, TEXT("Title left alone: demo play (stage %d, %d CPUs)"), PendingMatchRules.StageIndex, PendingMatchRules.CPUCount);
	OpenTrainingLevel(false, false, false);
}

void AChaosImpactPlayerController::SetDemoIntroCharacter(AChaosImpactCharacter* InCharacter)
{
	DemoIntroCharacter = InCharacter;
}

void AChaosImpactPlayerController::EndDemoPlay()
{
	if (bTravelPending)
	{
		return;
	}
	OpenTitleLevel();
}

void AChaosImpactPlayerController::PlayerTick(const float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	// The title left alone a while (no key, button, stick or mouse): its demo play.
	if (bTitleDemoWorld && CurrentScreen == EChaosImpactScreen::Title && !bTravelPending && IsPrimaryLocalPlayerController()
		&& FSlateApplication::IsInitialized() && GetDemoIdleSeconds() > 0.0f)
	{
		const UChaosImpactFxPreloadSubsystem* Preload = UChaosImpactFxPreloadSubsystem::Get(this);
		const double Now = FSlateApplication::Get().GetCurrentTime();
		const double Idle = FMath::Min(Now - FSlateApplication::Get().GetLastUserInteractionTime(), FPlatformTime::Seconds() - TitleShownSince);
		if ((!Preload || Preload->IsComplete()) && Idle > GetDemoIdleSeconds())
		{
			StartDemoPlay();
		}
	}
	if (bTrainingTeams) { ApplyTrainingTeams(); }
	SendLoadout();
	UpdateMatchmadeProgress();
	UpdateMatchScreens();
	UpdateRecruitmentHold(DeltaTime);
	UpdateOnlineVersusCard(DeltaTime);
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
	// Development (-CIDevRoomChange=<seconds>): in a room's lobby, that long after joining, rename this player and
	// pick another character through the pause menu's own steps (online tests).
	static const float DevRoomChangeDelay = []()
	{
		float Value = -1.0f;
		FParse::Value(FCommandLine::Get(), TEXT("CIDevRoomChange="), Value);
		return Value;
	}();
	if (DevRoomChangeDelay >= 0.0f && GetWorld() && DevRoomChangeStep < 4 && IsPrimaryLocalPlayerController())
	{
		const double Now = GetWorld()->GetTimeSeconds();
		if (DevRoomChangeAt <= 0.0)
		{
			DevRoomChangeAt = Now + DevRoomChangeDelay;
		}
		else if (Now >= DevRoomChangeAt && CanChangeLoadoutInRoom())
		{
			DevRoomChangeAt = Now + 1.5;
			switch (DevRoomChangeStep++)
			{
			case 0:
				ShowMenuScreen(EChaosImpactScreen::Pause);
				BeginRoomPlayerRename();
				UE_LOG(LogChaosImpact, Log, TEXT("DevRoomChange: name entry open %d"), CurrentScreen == EChaosImpactScreen::OnlineName);
				break;
			case 1:
				SubmitOnlineName(TEXT("RENAMED"));
				break;
			case 2:
				ShowMenuScreen(EChaosImpactScreen::Pause);
				BeginRoomCharacterChange();
				UE_LOG(LogChaosImpact, Log, TEXT("DevRoomChange: character select open %d"), CurrentScreen == EChaosImpactScreen::CharacterSelect);
				break;
			default:
				if (UChaosImpactLoadoutSubsystem* Loadouts = UChaosImpactLoadoutSubsystem::Get(this))
				{
					FChaosImpactLoadout Loadout = Loadouts->GetLoadout(0);
					Loadout.Character = (Loadout.Character + 1) % ChaosImpactRoster::Num();
					Loadout.Colour = 3;
					Loadouts->SetLoadout(0, Loadout);
				}
				ConfirmCharacterSelection();
				UE_LOG(LogChaosImpact, Log, TEXT("DevRoomChange: back to play %d"), CurrentScreen == EChaosImpactScreen::Playing);
				break;
			}
		}
	}
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
	// Development (-CIDevToggleSpectate=<seconds>): switch to watching that long into the room's lobby, once.
	static const float DevWatchDelay = []()
	{
		float Value = -1.0f;
		FParse::Value(FCommandLine::Get(), TEXT("CIDevToggleSpectate="), Value);
		return Value;
	}();
	static bool bDevWatchDone = false;
	if (DevWatchDelay >= 0.0f && !bDevWatchDone && IsLocalController() && IsPrimaryLocalPlayerController()
		&& CanToggleSpectating() && GetWorld() && GetWorld()->GetTimeSeconds() >= DevWatchDelay)
	{
		bDevWatchDone = true;
		ServerSetSpectating(true);
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
	if ((bRoomCharacterChange || bRenamingPlayer) && Match->Phase != EChaosImpactOnlinePhase::Lobby)
	{
		bRoomCharacterChange = false;
		bRenamingPlayer = false;
		EnterPlayingScreen();
	}
	// Spectators have no team to pick; they keep watching.
	const bool bTeamSelect = Match->bVersusMatch && Match->Phase == EChaosImpactOnlinePhase::TeamSelect && !IsSpectating();
	if (bTeamSelect && CurrentScreen == EChaosImpactScreen::Playing)
	{
		ShowMenuScreen(EChaosImpactScreen::TeamSelect);
	}
	else if (!bTeamSelect && CurrentScreen == EChaosImpactScreen::TeamSelect)
	{
		EnterPlayingScreen();
	}
	// Once the results' show is over, their menu comes up: the pages, and a rematch (local) or the lobby (online).
	const UChaosImpactResultsView* Results = GetResultsView();
	const bool bResultsMenu = Results && Results->CanAdvance() && IsPrimaryLocalPlayerController();
	if (bResultsMenu && CurrentScreen == EChaosImpactScreen::Playing)
	{
		ShowMenuScreen(EChaosImpactScreen::MatchEnd);
	}
	else if (!bResultsMenu && CurrentScreen == EChaosImpactScreen::MatchEnd)
	{
		EnterPlayingScreen();
	}
}

void AChaosImpactPlayerController::UpdateMatchIntroCamera()
{
	const AChaosImpactGameState* Match = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	// The demo watches a CPU: the opening plays as that CPU's player would see it.
	const AChaosImpactCharacter* PlayerCharacter = bDemoPlay && DemoIntroCharacter.IsValid() ? DemoIntroCharacter.Get()
		: Cast<AChaosImpactCharacter>(GetPawn());
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
			if (GetViewTarget() == IntroCamera && bDemoPlay && DemoIntroCharacter.IsValid())
			{
				SetViewTarget(DemoIntroCharacter.Get());
			}
			else if (GetViewTarget() == IntroCamera && GetPawn())
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
	const float DuelViewYaw = Match->IsDuel() ? PlayerCharacter->GetTopDownCameraRotation().Yaw : 0.f;
	const auto FlyoverPose = [&StageCenter, DuelViewYaw](const float Alpha, FVector& OutLocation, FRotator& OutRotation)
	{
		const float Ease = FMath::InterpEaseInOut(0.0f, 1.0f, FMath::Clamp(Alpha, 0.0f, 1.0f), 2.0f);
		const FVector Direction = FRotator(0.0f, FMath::Lerp(-200.0f, 0.0f, Ease) + DuelViewYaw, 0.0f).Vector();
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

// --- 【2. ソロモード用レベル遷移関数の追加】 ---

void AChaosImpactPlayerController::OpenSoloLevel(const int32 StageIndex)
{
	CurrentSoloStage = FMath::Max(1, StageIndex);

	// ステージ番号ごとのレベルはステージ表（ChaosImpactSoloStages.cpp）にある。1は夜のネオン街、15は空中神殿
	// （夕焼けの空中遺跡のレベル）。まだ作っていないステージは専用のマップを作る前提の名前。
	FString MapPackage = ChaosImpactSolo::GetLevelPackage(CurrentSoloStage);
	if (MapPackage.IsEmpty())
	{
		MapPackage = FString::Printf(TEXT("/Game/Solo/Lvl_Solo_Stage%d"), CurrentSoloStage);
	}

	bTravelPending = true;
	ExitTrainingOverlayPresentation();
	SetPause(false);

	// オプションパラメータの作成: ?CISolo=1?CIStage=X を付与し、CPUやマトは出さない
	FString Options = FString::Printf(TEXT("CISolo=1?CIStage=%d?CITargets=0?CICPUCount=0?%s"),
		CurrentSoloStage, *BuildLocalSetupOptions());

	if (BallFlightMode == EChaosImpactBallFlightMode::Straight)
	{
		Options += TEXT("?CIBallStraight=1");
	}

	bIntroFullScreen = false;
	if (UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
	{
		Viewport->SetForceDisableSplitscreen(false);
	}

	// ロード画面を出してからマップを開く
	if (UChaosImpactLoadingSubsystem* Loading = UChaosImpactLoadingSubsystem::Get(this))
	{
		Loading->BeginTravel(EChaosImpactLoadingKind::SoloStage);
	}
	UGameplayStatics::OpenLevel(this, FName(*MapPackage), true, Options);
}

void AChaosImpactPlayerController::OpenSoloWorldMap(const int32 Map, const int32 AtNode, const bool bLoadingScreen)
{
	GetWorldTimerManager().ClearTimer(SoloReturnTimer);
	if (bLoadingScreen)
	{
		if (UChaosImpactLoadingSubsystem* Loading = UChaosImpactLoadingSubsystem::Get(this))
		{
			Loading->BeginTravel(EChaosImpactLoadingKind::SoloMap);
		}
	}
	bTravelPending = true;
	ExitTrainingOverlayPresentation();
	SetPause(false);
	FString Options = FString::Printf(TEXT("CIWorldMap=1?CIMapAt=%d?CITargets=0?CICPUCount=0?%s"), AtNode, *BuildLocalSetupOptions());
	if (BallFlightMode == EChaosImpactBallFlightMode::Straight)
	{
		Options += TEXT("?CIBallStraight=1");
	}
	bIntroFullScreen = false;
	if (UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
	{
		Viewport->SetForceDisableSplitscreen(false);
	}
	UGameplayStatics::OpenLevel(this, FName(*ChaosImpactSolo::GetMapPackage(Map)), true, Options);
}

void AChaosImpactPlayerController::OpenSoloTitle()
{
	const TCHAR* Map = TEXT("/Game/ChaosImpact/solo/Title/Lvl_SoloTitle");
	if (bTravelPending)
	{
		return;
	}
	if (!FPackageName::DoesPackageExist(Map))
	{
		// No title level: straight on with the file last played.
		StartSoloFile(UChaosImpactSoloProgress::GetActiveSlot());
		return;
	}
	GetWorldTimerManager().ClearTimer(SoloReturnTimer);
	bTravelPending = true;
	ExitTrainingOverlayPresentation();
	SetPause(false);
	bIntroFullScreen = false;
	if (UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
	{
		Viewport->SetForceDisableSplitscreen(false);
	}
	if (UChaosImpactLoadingSubsystem* Loading = UChaosImpactLoadingSubsystem::Get(this))
	{
		Loading->BeginTravel(EChaosImpactLoadingKind::SoloTitle);
	}
	UGameplayStatics::OpenLevel(this, FName(Map), true, FString::Printf(TEXT("CISoloTitle=1?CITargets=0?CICPUCount=0?%s"), *BuildLocalSetupOptions()));
}

void AChaosImpactPlayerController::StartSoloFile(const int32 Slot)
{
	UChaosImpactSoloProgress::SetActiveSlot(Slot);
	UChaosImpactSoloProgress* Progress = UChaosImpactSoloProgress::Load();
	if (!UChaosImpactSoloProgress::DoesSlotExist(Slot))
	{
		// A new file: saved at once, so its card shows it begun.
		Progress->Save();
	}
	const int32 Node = Progress ? Progress->LastNode : 1;
	const FChaosImpactSoloStageInfo* Info = ChaosImpactSolo::FindStage(Node);
	OpenSoloWorldMap(Info ? Info->Map : 1, Info ? Node : 1);
}

void AChaosImpactPlayerController::OpenSoloFromMenu()
{
	// Solo mode starts on its own title (the files).
	OpenSoloTitle();
}

void AChaosImpactPlayerController::NotifySoloStageCleared()
{
	const FChaosImpactSoloStageInfo* Info = ChaosImpactSolo::FindStage(CurrentSoloStage);
	if (!bSoloMode || !Info || GetWorldTimerManager().IsTimerActive(SoloReturnTimer))
	{
		return;
	}
	UChaosImpactSoloProgress* Progress = UChaosImpactSoloProgress::Load();
	Progress->MarkCleared(CurrentSoloStage);
	Progress->LastNode = CurrentSoloStage;
	Progress->Save();
	// The clear: a flash and a moment of slow motion, STAGE CLEAR! on the HUD (UChaosImpactChargeWidget::PaintSoloClear)
	// over fireworks round the player, then back to the map (where the way on opens).
	SoloClearedAt = FPlatformTime::Seconds();
	PlaySoloClearFireworks();
	constexpr float SlowSpeed = 0.35f;
	constexpr float SlowRealSeconds = 0.7f;
	UGameplayStatics::SetGlobalTimeDilation(this, SlowSpeed);
	FTimerHandle Restore;
	GetWorldTimerManager().SetTimer(Restore, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		UGameplayStatics::SetGlobalTimeDilation(this, 1.0f);
	}), SlowRealSeconds * SlowSpeed, false);
	const int32 Map = Info->Map;
	const int32 Stage = CurrentSoloStage;
	if (!GetWorld()->URL.HasOption(TEXT("CIStage")))
	{
		// A room played on its own (not a stage of the map): nowhere to go back to.
		return;
	}
	// (Game seconds: the slow part counts for less.)
	GetWorldTimerManager().SetTimer(SoloReturnTimer, FTimerDelegate::CreateWeakLambda(this, [this, Map, Stage]()
	{
		if (!bTravelPending)
		{
			OpenSoloWorldMap(Map, Stage);
		}
	}), SoloClearSeconds - SlowRealSeconds * (1.0f - SlowSpeed), false);
}

void AChaosImpactPlayerController::PlaySoloClearFireworks()
{
	const APawn* Me = GetPawn();
	AChaosImpactFxKit* Fx = Me ? AChaosImpactFxKit::Spawn(this, Me->GetActorLocation() - FVector(0.0f, 0.0f, 88.0f)) : nullptr;
	if (!Fx)
	{
		return;
	}
	Fx->Stream.Initialize(FMath::Rand());
	const FLinearColor FireworksGold(1.0f, 0.62f, 0.08f);
	const FLinearColor White(1.0f, 0.95f, 0.8f);
	FChaosImpactFxPiece Pillar;
	Pillar.Life = 1.8f;
	Pillar.SizeFrom = 300.0f;
	Pillar.SizeTo = 160.0f;
	Pillar.Width = 5.0f;
	Pillar.Taper = 1.0f;
	Pillar.Color = FireworksGold;
	Pillar.CoreColor = White;
	Pillar.Intensity = 3.0f;
	Pillar.Attack = 0.05f;
	Pillar.NoiseSpeed = 1.4f;
	Pillar.NoiseAmount = 0.45f;
	Fx->Add(Pillar);
	FChaosImpactFxPiece Ring;
	Ring.Look = EChaosImpactFxLook::Ring;
	Ring.Location = FVector(0.0f, 0.0f, 4.0f);
	Ring.Life = 0.8f;
	Ring.SizeFrom = Ring.SizeTo = 1000.0f;
	Ring.RingFrom = 0.08f;
	Ring.RingTo = 0.92f;
	Ring.RingWidth = 0.04f;
	Ring.Color = FireworksGold;
	Ring.CoreColor = White;
	Ring.Intensity = 3.0f;
	Ring.Attack = 0.04f;
	Ring.ErodeTo = 0.7f;
	Fx->Add(Ring);
	// Fireworks bursting overhead, one after another, in the game's colours.
	const FLinearColor Colors[] = {FireworksGold, FLinearColor(0.1f, 0.6f, 1.0f), FLinearColor(1.0f, 0.12f, 0.2f), FLinearColor(0.6f, 0.3f, 1.0f)};
	const FVector Spots[] = {FVector(120.0f, -260.0f, 520.0f), FVector(-60.0f, 280.0f, 600.0f), FVector(260.0f, 60.0f, 680.0f), FVector(-220.0f, -60.0f, 560.0f)};
	for (int32 Burst = 0; Burst < 4; ++Burst)
	{
		const float Delay = 0.12f + Burst * 0.38f;
		const FLinearColor Color = Colors[Burst];
		FChaosImpactFxPiece Pop;
		Pop.Location = Spots[Burst];
		Pop.Delay = Delay;
		Pop.Life = 0.3f;
		Pop.SizeFrom = 140.0f;
		Pop.SizeTo = 380.0f;
		Pop.Color = Color;
		Pop.CoreColor = White;
		Pop.Intensity = 5.0f;
		Pop.Attack = 0.05f;
		Pop.NoiseAmount = 0.2f;
		Fx->Add(Pop);
		for (int32 Index = 0; Index < 18; ++Index)
		{
			FChaosImpactFxPiece Star;
			Star.Look = EChaosImpactFxLook::Streak;
			Star.Location = Spots[Burst];
			Star.Delay = Delay;
			Star.Velocity = FRotator(Fx->Stream.FRandRange(-60.0f, 70.0f), Index * 20.0f + Fx->Stream.FRandRange(-6.0f, 6.0f), 0.0f).Vector()
				* Fx->Stream.FRandRange(650.0f, 850.0f);
			Star.Gravity = 500.0f;
			Star.Drag = 1.6f;
			Star.Life = Fx->Stream.FRandRange(0.9f, 1.3f);
			Star.SizeFrom = 110.0f;
			Star.SizeTo = 30.0f;
			Star.Width = 7.0f;
			Star.Color = Color;
			Star.CoreColor = White;
			Star.Intensity = 6.0f;
			Star.Attack = 0.03f;
			Star.ErodeTo = 0.0f;
			Star.Flicker = 0.6f;
			Fx->Add(Star);
		}
	}
}

bool AChaosImpactPlayerController::OpenExplorationMap(const FString& ExtraOptions)
{
	const TCHAR* Map = TEXT("/Game/ChaosImpact/OpenWorld/Lvl_OpenWorld");
	// Removable RPG prototype: a package-only hook, with the original exploration map as fallback.
	const bool bRPGPrototype = FPackageName::DoesPackageExist(TEXT("/Game/ChaosImpact/RPGPrototype/Lvl_RPGVSStage1"));
	if (bRPGPrototype) { Map = TEXT("/Game/ChaosImpact/RPGPrototype/Lvl_RPGVSStage1"); }
	if (bRPGPrototype && FPackageName::DoesPackageExist(TEXT("/Game/ChaosImpact/RPGPrototype/Adventure/Lvl_RPGColonyFrontier")))
	{ Map = TEXT("/Game/ChaosImpact/RPGPrototype/Adventure/Lvl_RPGColonyFrontier"); }
	if (bTravelPending || !FPackageName::DoesPackageExist(Map))
	{
		return false;
	}
	bTravelPending = true;
	SetPause(false);
	bIntroFullScreen = false;
	UGameplayStatics::OpenLevel(this, FName(Map), true, FString::Printf(TEXT("CIOpenWorld=1?CITargets=0?CICPUCount=0?%s%s%s"),
		*BuildLocalSetupOptions(), bRPGPrototype ? TEXT("?CIRPGPrototype=1") : TEXT(""), bRPGPrototype ? *ExtraOptions : TEXT("")));
	return true;
}

bool AChaosImpactPlayerController::HandleWorldMapPauseChoice(const FString& Choice)
{
	if (Choice == TEXT("solo_world_map"))
	{
		const FChaosImpactSoloStageInfo* Info = ChaosImpactSolo::FindStage(CurrentSoloStage);
		OpenSoloWorldMap(Info ? Info->Map : 1, CurrentSoloStage);
		return true;
	}
	if (!bSoloWorldMap)
	{
		return false;
	}
	if (Choice == TEXT("goto_mode"))
	{
		ShowMenuScreen(EChaosImpactScreen::ModeSelect);
		return true;
	}
	if (Choice == TEXT("goto_solo_title"))
	{
		OpenSoloTitle();
		return true;
	}
	if (Choice == TEXT("goto_title"))
	{
		ShowMenuScreen(EChaosImpactScreen::Title);
		return true;
	}
	if (Choice == TEXT("map_unlock"))
	{
		UChaosImpactSoloProgress* Progress = UChaosImpactSoloProgress::Load();
		Progress->bUnlockAll = !Progress->bUnlockAll;
		Progress->Save();
		if (AChaosImpactWorldMap* Map = AChaosImpactWorldMap::Find(this))
		{
			Map->RefreshFromProgress();
		}
		ResumeGameplay();
		return true;
	}
	if (Choice == TEXT("map_reset"))
	{
		UChaosImpactSoloProgress::Load()->ResetAll();
		OpenSoloWorldMap(1, 1);
		return true;
	}
	return false;
}

void AChaosImpactPlayerController::RetrySoloLevel()
{
	if (!bSoloMode || bTravelPending)
	{
		return;
	}
	OpenSoloLevel(CurrentSoloStage);
}
