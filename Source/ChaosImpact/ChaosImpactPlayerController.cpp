// Copyright Epic Games, Inc. All Rights Reserved.


#include "ChaosImpactPlayerController.h"
#include "ChaosImpactBodyPaint.h"
#include "ChaosImpactPaintCache.h"
#include "ChaosImpactPuppetComponent.h"
#include "ChaosImpactBallSwitch.h"
#include "ChaosImpactDojo.h"
#include "ChaosImpactSimaeBird.h"
#include "ChaosImpactTackleComponent.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactTitleDemo.h"
#include "ChaosImpactVersusCard.h"
#include "ChaosImpactCharacterPreview.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ChaosImpactPaint.h"
#include "MoviePlayer.h"
#include "Containers/Ticker.h"
#include "JoyShockBlueprintLibrary.h"
#include "JoyShockTypes.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "ChaosImpactGameMode.h"
#include "ChaosImpactBallSpawner.h"
#include "ChaosImpactTournamentWidget.h"
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
#include "ChaosImpactLocalTourney.h"
#include "ChaosImpactCustomColour.h"
#include "ChaosImpactStageBase.h"
#include "ChaosImpactNetStats.h"
#include "ChaosImpactSessionSubsystem.h"
#include "ChaosImpactSfx.h"
#include "UnrealClient.h"
#include "ChaosImpactRelay.h"
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

namespace
{
	/**
	 * Development: -CIShotDir=<folder> saves a picture of the screen (menus included) a moment later, named Name.png,
	 * at the online menu's moments (the 1v1 found, a matched room's results, back at the menu), for checking them.
	 */
	void RequestDevShot(const FString& Name, const float Delay)
	{
		FString Folder;
		if (!FParse::Value(FCommandLine::Get(), TEXT("CIShotDir="), Folder))
		{
			return;
		}
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Path = FPaths::Combine(Folder, Name + TEXT(".png"))](float)
		{
			FScreenshotRequest::RequestScreenshot(Path, true, false);
			return false;
		}), Delay);
	}
}

void AChaosImpactPlayerController::BeginPlay()
{
	Super::BeginPlay();
	// The saved volume and brightness (Unreal applies its own screen and picture settings).
	ChaosImpactSettings::ApplyStartupSettings();
	// Development (-CINetStats): online lag measurements in the log.
	ChaosImpactNetStats::StartIfAsked();
	// Online: keeps a slow line from clogging (less sent while its round trip climbs).
	ChaosImpactNetStats::StartLatencyGuard();
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
	// A single player on this machine (online or not: solo mode, training, a VS alone) follows whichever device was used
	// last, keyboard and mouse or a controller; two or more keep their assigned devices apart.
	bOnlineAnyInput = MachinePlayers <= 1;
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
	// (Starting with the device chosen on the way in, if one was.)
	if (bOnlineAnyInput)
	{
		bLastInputGamepad = bPrimaryUsesGamepad;
		// Every controller plugged in is this lone player's too (a keyboard player owns none otherwise, and the viewport
		// drops a controller nobody owns): now, and again every couple of seconds for one plugged in later.
		if (IsLocalController())
		{
			GetWorldTimerManager().SetTimer(SolePadTimer, this, &AChaosImpactPlayerController::ClaimEveryPadForSolePlayer, 2.0f, true, 0.6f);
		}
	}
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
#if !UE_BUILD_SHIPPING
	// Development (-CIDevPadScript=<s>:<Button>;<s>:<Axis>=<value>;...): a controller's input played into this game as a
	// real one would come (through Slate, as controller 0), for testing with no controller at hand. Buttons are pressed
	// and let go; an axis holds its value (sent again every frame) until set again. Names: FGamepadKeyNames
	// (FaceButtonBottom, LeftAnalogX, ...). Seconds count from this world's start.
	static bool bDevPadScriptStarted = false;
	FString DevPadScript;
	if (!bDevPadScriptStarted && IsLocalController() && bPrimaryLocalPlayer && FSlateApplication::IsInitialized()
		&& FParse::Value(FCommandLine::Get(), TEXT("CIDevPadScript="), DevPadScript, false))
	{
		bDevPadScriptStarted = true;
		struct FPadStep { double At; FName Key; float Value; bool bAxis; };
		TArray<FPadStep> Steps;
		TArray<FString> Parts;
		DevPadScript.ParseIntoArray(Parts, TEXT(";"));
		for (const FString& Part : Parts)
		{
			FString When, What, Name, Value;
			if (!Part.Split(TEXT(":"), &When, &What)) { continue; }
			const bool bAxis = What.Split(TEXT("="), &Name, &Value);
			Steps.Add({FCString::Atod(*When), FName(bAxis ? *Name : *What), bAxis ? FCString::Atof(*Value) : 1.0f, bAxis});
		}
		const double Start = FPlatformTime::Seconds();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Steps, Start, Next = 0, Axes = TMap<FName, float>(),
			Releases = TArray<TPair<double, FName>>()](float) mutable
		{
			if (!FSlateApplication::IsInitialized()) { return false; }
			FPlatformUserId User = PLATFORMUSERID_NONE;
			FInputDeviceId Device = INPUTDEVICEID_NONE;
			IPlatformInputDeviceMapper::Get().RemapControllerIdToPlatformUserAndDevice(0, User, Device);
			// (Into the game's viewport as a controller's input arrives there, and so to whichever local player has it.)
			UGameViewportClient* Viewport = GEngine ? GEngine->GameViewport.Get() : nullptr;
			// (-CIDevPadDirect: straight into the first player, as though their controller were theirs, past the ownership check.)
			APlayerController* Direct = nullptr;
			if (FParse::Param(FCommandLine::Get(), TEXT("CIDevPadDirect")) && GEngine)
			{
				for (const FWorldContext& Context : GEngine->GetWorldContexts())
				{
					if (Context.World() && Context.World()->IsGameWorld() && Context.World()->GetFirstPlayerController())
					{
						Direct = Context.World()->GetFirstPlayerController();
						break;
					}
				}
			}
			const auto Send = [Viewport, Direct, Device](const FName Key, const EInputEvent Event, const float Amount)
			{
				const FInputKeyEventArgs Args(Viewport ? Viewport->Viewport : nullptr, Device, FKey(Key), Event, Amount, false, FPlatformTime::Cycles64());
				if (Direct) { Direct->InputKey(Args); } else if (Viewport) { Viewport->InputKey(Args); }
			};
			const double Now = FPlatformTime::Seconds() - Start;
			while (Steps.IsValidIndex(Next) && Now >= Steps[Next].At)
			{
				const FPadStep& Step = Steps[Next++];
				if (Step.bAxis)
				{
					Axes.Add(Step.Key, Step.Value);
				}
				else
				{
					Send(Step.Key, IE_Pressed, 1.0f);
					Releases.Add({Now + 0.15, Step.Key});
				}
				UE_LOG(LogChaosImpact, Log, TEXT("DevPadScript: %s %s (to %s)"), *Step.Key.ToString(), Step.bAxis ? *FString::SanitizeFloat(Step.Value) : TEXT("pressed"),
					Direct ? *Direct->GetName() : TEXT("the viewport"));
			}
			for (int32 Index = Releases.Num() - 1; Index >= 0; --Index)
			{
				if (Now >= Releases[Index].Key)
				{
					Send(Releases[Index].Value, IE_Released, 0.0f);
					Releases.RemoveAt(Index);
				}
			}
			for (const TPair<FName, float>& Axis : Axes)
			{
				Send(Axis.Key, IE_Axis, Axis.Value);
			}
			return Steps.IsValidIndex(Next) || !Releases.IsEmpty() || Axes.Num() > 0;
		}));
	}
#endif
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
		// -CIAutoQueue=random|duel: that ランダムマッチ's search, alone on this machine (-CIAutoName= names the player).
		static bool bAutoQueueConsumed = false;
		FString AutoQueue;
		if (!bAutoQueueConsumed && FParse::Value(FCommandLine::Get(), TEXT("CIAutoQueue="), AutoQueue))
		{
			bAutoQueueConsumed = true;
			FString AutoName;
			UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this);
			if (Sessions && FParse::Value(FCommandLine::Get(), TEXT("CIAutoName="), AutoName))
			{
				Sessions->SetPlayerName(AutoName);
			}
			PlayFlow = EChaosImpactPlayFlow::VersusOnline;
			RequestedLocalPlayerCount = 1;
			RequestedKeyboardPlayerIndex = 0;
			JoinedInputDeviceIds.Reset();
			JoinedLegacyControllerIds.Reset();
			BeginMatchQueue(AutoQueue == TEXT("duel") ? EChaosImpactQueueMode::Duel : EChaosImpactQueueMode::Random);
			return;
		}
		// Development: -CIAutoTourneyCreate=<minutes>:<cpus>:<0|1 mode> makes a public tournament; -CIAutoTourneyJoin enters the
		// first one listed.
		static bool bAutoTourneyConsumed = false;
		FString AutoTourney;
		if (!bAutoTourneyConsumed && (FParse::Value(FCommandLine::Get(), TEXT("CIAutoTourneyCreate="), AutoTourney)
			|| FParse::Param(FCommandLine::Get(), TEXT("CIAutoTourneyJoin")) || FParse::Param(FCommandLine::Get(), TEXT("CIAutoTourneyBrowse"))))
		{
			bAutoTourneyConsumed = true;
			FString AutoName;
			if (FParse::Value(FCommandLine::Get(), TEXT("CIAutoName="), AutoName))
			{
				if (UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this))
				{
					Sessions->SetPlayerName(AutoName);
				}
			}
			int32 AutoPlayers = 1;
			FParse::Value(FCommandLine::Get(), TEXT("CIAutoPlayers="), AutoPlayers);
			PlayFlow = EChaosImpactPlayFlow::VersusOnline;
			RequestedLocalPlayerCount = FMath::Clamp(AutoPlayers, 1, 2);
			RequestedKeyboardPlayerIndex = 0;
			JoinedInputDeviceIds.Reset();
			JoinedLegacyControllerIds.Reset();
			if (!AutoTourney.IsEmpty())
			{
				TArray<FString> Parts;
				AutoTourney.ParseIntoArray(Parts, TEXT(":"));
				OpenTourneyCreate();
				PendingTourneyMinutes = Parts.IsValidIndex(0) ? FMath::Max(1, FCString::Atoi(*Parts[0])) : 1;
				PendingTourney->CPUs = Parts.IsValidIndex(1) ? FCString::Atoi(*Parts[1]) : 2;
				PendingTourney->Mode = Parts.IsValidIndex(2) ? FCString::Atoi(*Parts[2]) : 1;
				CreatePublicTourney();
			}
			else if (FParse::Param(FCommandLine::Get(), TEXT("CIAutoTourneyBrowse")))
			{
				OpenTourneyList();
			}
			else
			{
				OpenTourneyList();
			}
			return;
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
			if (Sessions->ConsumeReturnToOnlineMenu())
			{
				// Out of a room (left it, disbanded it, or it went away): the online menu, not the training.
				PlayFlow = EChaosImpactPlayFlow::VersusOnline;
				GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
				{
					ShowMenuScreen(EChaosImpactScreen::MultiReady);
					RequestDevShot(TEXT("online_menu_after_room"), 6.0f);
				}));
			}
			const EChaosImpactQueueMode Again = Sessions->ConsumeShowQueueMenu();
			const bool bSearchAgain = Sessions->ConsumeQueueAgain();
			if (Again != EChaosImpactQueueMode::None)
			{
				PlayFlow = EChaosImpactPlayFlow::VersusOnline;
				GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this, Again, bSearchAgain]()
				{
					if (bSearchAgain && Again != EChaosImpactQueueMode::Rank)
					{
						BeginMatchQueue(Again);
						RequestDevShot(TEXT("searching_again"), 8.0f);
					}
					else if (Again == EChaosImpactQueueMode::Rank)
					{
						OpenRankTop();
					}
					else
					{
						ShowMenuScreen(EChaosImpactScreen::MultiReady);
						RequestDevShot(TEXT("online_menu_after_match"), 8.0f);
					}
				}));
			}
		}
	}

#if !UE_BUILD_SHIPPING
	// Development: -CIDevPauseAt=<seconds> opens the pause menu that long into the first world it can be opened in
	// (screenshots of it); -CIFakeDuelOffer shows a made-up 1v1 found in training.
	if (IsLocalController() && IsPrimaryLocalPlayerController())
	{
		static bool bDevPauseDone = false;
		float PauseAt = -1.0f;
		if (!bDevPauseDone && FParse::Value(FCommandLine::Get(), TEXT("CIDevPauseAt="), PauseAt) && (bTrainingMode || IsOnlineRoom() || bSoloMode))
		{
			bDevPauseDone = true;
			FTimerHandle PauseTimer;
			GetWorldTimerManager().SetTimer(PauseTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				UE_LOG(LogChaosImpact, Log, TEXT("Development: pause menu opened"));
				TogglePauseMenu();
			}), FMath::Max(0.5f, PauseAt), false);
		}
		// -CIDevLeaveAt=<seconds>: leaves the first online room (its lobby) that long after arriving; -CIDevSurrenderAt=<seconds>:
		// surrenders the first matched match that long into its level.
		static bool bDevLeaveDone = false;
		float LeaveAt = -1.0f;
		if (!bDevLeaveDone && IsOnlineRoom() && !GetWorld()->URL.HasOption(TEXT("CIRoomMatch"))
			&& FParse::Value(FCommandLine::Get(), TEXT("CIDevLeaveAt="), LeaveAt))
		{
			bDevLeaveDone = true;
			FTimerHandle LeaveTimer;
			GetWorldTimerManager().SetTimer(LeaveTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				UE_LOG(LogChaosImpact, Log, TEXT("Development: leaving the room"));
				LeaveOnlineRoom();
			}), FMath::Max(0.5f, LeaveAt), false);
		}
		static bool bDevSurrenderDone = false;
		float SurrenderAt = -1.0f;
		if (!bDevSurrenderDone && IsOnlineRoom() && GetWorld()->URL.HasOption(TEXT("CIRoomMatch"))
			&& FParse::Value(FCommandLine::Get(), TEXT("CIDevSurrenderAt="), SurrenderAt))
		{
			bDevSurrenderDone = true;
			FTimerHandle SurrenderTimer;
			GetWorldTimerManager().SetTimer(SurrenderTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				UE_LOG(LogChaosImpact, Log, TEXT("Development: surrendering (in match %d)"), IsInMatchmadeMatch());
				Surrender();
			}), FMath::Max(0.5f, SurrenderAt), false);
		}
		// -CIDevLocalTourney=<people>:<cpus>:<0 one at a time | 1 all at once>: registers that many people from the title and
		// starts a local tournament (screenshots and runs of it).
		static bool bDevLocalTourneyDone = false;
		FString DevLocalTourney;
		if (!bDevLocalTourneyDone && !bTrainingMode && !IsOnlineRoom() && FParse::Value(FCommandLine::Get(), TEXT("CIDevLocalTourney="), DevLocalTourney))
		{
			bDevLocalTourneyDone = true;
			// (On the core ticker: the title's menus pause the world.)
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this, DevLocalTourney](float)
			{
				TArray<FString> Parts;
				DevLocalTourney.ParseIntoArray(Parts, TEXT(":"));
				if (UChaosImpactLocalTourneySubsystem* Data = UChaosImpactLocalTourneySubsystem::Get(this))
				{
					Data->People.Reset();
					Data->Mode = Parts.IsValidIndex(2) && FCString::Atoi(*Parts[2]) == 1 ? EChaosImpactTournamentMode::Simultaneous
						: EChaosImpactTournamentMode::OneAtATime;
					Data->CPUs = Parts.IsValidIndex(1) ? FCString::Atoi(*Parts[1]) : 2;
					// (A fourth part 1: with 順位決定戦.)
					Data->bPlacementMatches = Parts.IsValidIndex(3) && FCString::Atoi(*Parts[3]) == 1;
					const int32 People = Parts.IsValidIndex(0) ? FCString::Atoi(*Parts[0]) : 2;
					for (int32 Index = 0; Index < People; ++Index)
					{
						Data->AddPerson();
					}
					UE_LOG(LogChaosImpact, Log, TEXT("Development: local tournament of %d people and %d CPUs"), Data->People.Num(), Data->CPUs);
				}
				if (FParse::Param(FCommandLine::Get(), TEXT("CIDevTourneyEntryOnly")))
				{
					ShowMenuScreen(EChaosImpactScreen::TourneyEntry);
					return false;
				}
				StartLocalTourney();
				return false;
			}), 3.0f);
		}
		static bool bFakeOfferDone = false;
		if (!bFakeOfferDone && bTrainingMode && FParse::Param(FCommandLine::Get(), TEXT("CIFakeDuelOffer")))
		{
			bFakeOfferDone = true;
			FTimerHandle OfferTimer;
			GetWorldTimerManager().SetTimer(OfferTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				if (UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this))
				{
					PlayFlow = EChaosImpactPlayFlow::VersusOnline;
					Sessions->DevFakeDuelOffer();
					UE_LOG(LogChaosImpact, Log, TEXT("Development: a 1v1 found made up"));
				}
			}), 3.0f, false);
		}
	}
#endif
	// Every world: once the effects are in, they are shown once out of sight, then any loading screen goes. (Started
	// for any local controller: at the very start its player may not be set yet; the first one's carries on, see TickWarmUp.)
	if (IsLocalController())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(WarmUpTicker);
		WarmUpCheckAt = FPlatformTime::Seconds() + 0.3;
		WarmUpReadyAt = 0.0;
		WarmUpTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &AChaosImpactPlayerController::TickWarmUp), 0.1f);
	}
}

void AChaosImpactPlayerController::ReleaseLoadingWhenRoomReady()
{
	UChaosImpactLoadingSubsystem* Loading = UChaosImpactLoadingSubsystem::Get(this);
	UWorld* World = GetWorld();
	if (!Loading || !World)
	{
		return;
	}
	constexpr float MaxHoldSeconds = 25.0f;
	bool bHold = false;
	if (IsOnlineRoom() && World->GetTimeSeconds() < MaxHoldSeconds)
	{
		const AChaosImpactGameState* Room = World->GetGameState<AChaosImpactGameState>();
		const bool bMatchLevel = World->URL.HasOption(TEXT("CIRoomMatch"));
		// Not known yet (the room's state on its way): wait for it. A matched room's lobby: only passed through, the
		// match level follows by itself. A match level: until everyone is back in and it starts.
		bHold = !Room
			|| (!bMatchLevel && Room->IsMatchmade()
				&& (Room->Phase == EChaosImpactOnlinePhase::Lobby || Room->Phase == EChaosImpactOnlinePhase::Starting))
			|| (bMatchLevel && Room->Phase == EChaosImpactOnlinePhase::Starting);
	}
	if (bHold)
	{
		// (Its own never-stuck safety waits while this does.)
		Loading->KeepUpFor(1.0f);
		GetWorldTimerManager().SetTimer(LoadingReleaseTimer, this, &AChaosImpactPlayerController::ReleaseLoadingWhenRoomReady, 0.2f, false);
		return;
	}
	GetWorldTimerManager().ClearTimer(LoadingReleaseTimer);
	if (IsOnlineRoom())
	{
		const AChaosImpactGameState* Room = World->GetGameState<AChaosImpactGameState>();
		UE_LOG(LogChaosImpact, Log, TEXT("Loading screen released: phase %d matchmade %d match level %d at %.1f s"),
			Room ? static_cast<int32>(Room->Phase) : -1, Room && Room->IsMatchmade(), World->URL.HasOption(TEXT("CIRoomMatch")),
			World->GetTimeSeconds());
	}
	Loading->NotifyWorldReady();
}

bool AChaosImpactPlayerController::TickWarmUp(const float DeltaSeconds)
{
	if (!IsValid(this) || !GetWorld())
	{
		WarmUpTicker.Reset();
		return false;
	}
	if (!GetLocalPlayer())
	{
		// Not this machine's player yet (a moment at start-up): wait for it.
		return true;
	}
	if (!IsPrimaryLocalPlayerController())
	{
		// The first player's controller does this.
		WarmUpTicker.Reset();
		return false;
	}
	const double Now = FPlatformTime::Seconds();
	if (WarmUpReadyAt > 0.0)
	{
		// A few frames for the effects to be drawn, then this world is ready.
		if (Now < WarmUpReadyAt)
		{
			return true;
		}
		WarmUpTicker.Reset();
		FinishWarmUp();
		return false;
	}
	if (Now >= WarmUpCheckAt && WarmUpWhenLoaded())
	{
		WarmUpReadyAt = Now + 0.3;
	}
	return true;
}

void AChaosImpactPlayerController::FinishWarmUp()
{
	ReleaseLoadingWhenRoomReady();
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
}

bool AChaosImpactPlayerController::WarmUpWhenLoaded()
{
	if (const UChaosImpactFxPreloadSubsystem* Preload = UChaosImpactFxPreloadSubsystem::Get(this); Preload && !Preload->IsComplete())
	{
		return false;
	}
	// Every world balls fly in (training, VS, solo, and the title with its demo match): each effect shown once now,
	// out of sight below the arena, so the first real fire or ice ball does not stall while pipeline states and GPU
	// resources are created.
	if (bTrainingMode || bSoloMode || bTitleDemoWorld)
	{
		FVector Below = GetPawn() ? GetPawn()->GetActorLocation() : FVector::ZeroVector;
		Below.Z -= 6000.0f;
		ChaosImpactBallTypes::WarmUpEffects(GetWorld(), Below);
	}
	return true;
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
	if (IsActionKey(this, EChaosImpactAction::OverviewMap, Key) && IsGameplayActive())
	{
		bOverviewMapOpen = !bOverviewMapOpen;
		ChaosImpactSfx::Play2D(this, bOverviewMapOpen ? EChaosImpactSfx::UiSelect : EChaosImpactSfx::UiBack);
	}
	else if (IsActionKey(this, EChaosImpactAction::TrainingMenu, Key))
	{
		ToggleTrainingOverlay();
	}
	else if (IsActionKey(this, EChaosImpactAction::LobbyReady, Key))
	{
		ToggleReadyForMatch();
	}
	else if (IsActionKey(this, EChaosImpactAction::LobbyTeam, Key) && CanChangeLobbyTeam() && IsGameplayActive())
	{
		ServerChangeTeam(1);
	}
	// (Watching / playing switches on a held key: UpdateSpectateHold.)
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
#if !UE_BUILD_SHIPPING
	if (Params.Key.IsGamepadKey() && (Params.Event == IE_Pressed || FMath::Abs(Params.AmountDepressed) > 0.5f) && FParse::Param(FCommandLine::Get(), TEXT("CIDevPadLog")))
	{
		UE_LOG(LogChaosImpact, Log, TEXT("DevPadLog: %s %.2f reached P%d (any device %d, pad now %d)"), *Params.Key.ToString(), Params.AmountDepressed,
			GetThisLocalPlayerIndex() + 1, bOnlineAnyInput, IsUsingGamepad());
	}
#endif
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
			Assignments.Add(FString::Printf(TEXT("%dP  キーボード＋マウス"), PlayerIndex + 1));
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
	// Nobody has a device yet: on the entry screen each player takes the next place by pressing on theirs, the
	// keyboard included (RegisterKeyboardMouseJoin, RegisterControllerJoin).
	JoinedInputDeviceIds.Reset();
	JoinedLegacyControllerIds.Reset();
	RequestedKeyboardPlayerIndex = INDEX_NONE;
	bRequestedPrimaryUsesGamepad = true;
}

void AChaosImpactPlayerController::ClaimEveryPadForSolePlayer()
{
	const ULocalPlayer* Local = GetLocalPlayer();
	const UGameInstance* GameInstance = GetGameInstance();
	if (!bOnlineAnyInput || !Local || !GameInstance || GameInstance->GetLocalPlayers().Num() > 1)
	{
		GetWorldTimerManager().ClearTimer(SolePadTimer);
		return;
	}
	const IPlatformInputDeviceMapper& DeviceMapper = IPlatformInputDeviceMapper::Get();
	for (const FJSL4UControllerInfo& Info : UJoyShockLibrary::JSL4UGetAllConnectedControllers())
	{
		if (Info.InputDeviceId >= 0
			&& DeviceMapper.GetUserForInputDevice(FInputDeviceId::CreateFromInternalId(Info.InputDeviceId)) != Local->GetPlatformUserId()
			&& UJoyShockLibrary::JSL4UAssignControllerToPlayer(Info, this))
		{
			UE_LOG(LogChaosImpact, Log, TEXT("Controller %d is now P1's too (one player here: keyboard and controller both)"), Info.InputDeviceId);
		}
	}
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
	if (IsPrimaryLocalPlayerController() && NewScreen != CurrentScreen)
	{
		// (Where the menus go, in the log: the online flows are checked by it.)
		UE_LOG(LogChaosImpact, Log, TEXT("Menu screen: %s"), *StaticEnum<EChaosImpactScreen>()->GetNameStringByValue(static_cast<int64>(NewScreen)));
	}
	if (NewScreen == EChaosImpactScreen::Playing)
	{
		// The RPG title's controller assignment is over the play screen (its 戻る and its confirm go back there).
		if (CurrentScreen == EChaosImpactScreen::ControllerAssignment && PlayFlow == EChaosImpactPlayFlow::RPGAdventure)
		{
			EnterPlayingScreen();
			return;
		}
		ResumeGameplay();
		return;
	}
	// A menu over the game (a paused world may not tick) must not leave a held rumble running.
	StopRumble();
	if (NewScreen == EChaosImpactScreen::MultiReady && NewScreen != CurrentScreen && IsPrimaryLocalPlayerController())
	{
		// The online menu's player card shows the ratings: asked for again each time it comes up.
		if (UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this))
		{
			Sessions->RefreshRecords();
		}
	}
	if (NewScreen == EChaosImpactScreen::TrainingOverlay)
	{
		OpenTrainingOverlay();
		return;
	}
	if (bTrainingOverlayPresentationActive)
	{
		ExitTrainingOverlayPresentation();
	}
	// A pause screen is only reachable from an active play session (or back from a screen opened from it: in a room,
	// any of its own menus, as the character and name changes are).
	if (NewScreen == EChaosImpactScreen::Pause && !IsGameplayActive()
		&& !(IsOnlineRoom() && !bTravelPending && CurrentScreen != EChaosImpactScreen::Pause)
		&& CurrentScreen != EChaosImpactScreen::TrainingSettings
		&& !(CurrentScreen == EChaosImpactScreen::Settings && SettingsReturnScreen == EChaosImpactScreen::Pause)
		&& !(CurrentScreen == EChaosImpactScreen::ColourEdit && ColourEditReturnScreen == EChaosImpactScreen::Pause))
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
		// From a room, back to its pause menu (or play): never out to the menus before it.
		if (IsOnlineRoom() && SettingsReturnScreen != EChaosImpactScreen::Pause)
		{
			UE_LOG(LogChaosImpact, Warning, TEXT("Settings closed in a room with a way back out of it; back to play instead"));
			EnterPlayingScreen();
			return;
		}
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
	if (MenuWidget && FPlatformTime::Seconds() - MenuWidget->GetScreenStartedAt() < 0.2)
	{
		UE_LOG(LogChaosImpact, Verbose, TEXT("Pause button right after a screen change: ignored"));
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
	UE_LOG(LogChaosImpact, Verbose, TEXT("Pause closed (back to play)"));
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
	if (!IsPrimaryLocalPlayerController() || !bTrainingMode || bTravelPending || IsOnlineRoom() || IsVersusMatchWorld() || IsDojo())
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
	if (!IsPrimaryLocalPlayerController() || !bTrainingMode || bTravelPending || !MenuWidget || IsOnlineRoom() || IsDojo()
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
	OpenTrainingLevel();
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
		if (bRoomCharacterChange || IsOnlineRoom())
		{
			// Opened from the room's pause menu: back there, nothing changed (never out to the menus before the room).
			bRoomCharacterChange = false;
			ShowMenuScreen(EChaosImpactScreen::Pause);
			if (CurrentScreen == EChaosImpactScreen::CharacterSelect)
			{
				EnterPlayingScreen();
			}
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
	OpenTrainingLevel();
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
	if (CurrentScreen != EChaosImpactScreen::OnlineConnecting) { OnlineConnectionReturnScreen = CurrentScreen; }
	const int32 Generation = ++OnlineConnectionGeneration;
	bCheckingOnlineConnection = true;
	OnlineConnectionError.Reset();
	ShowMenuScreen(EChaosImpactScreen::OnlineConnecting);
	if (!ChaosImpactRelay::IsEnabled())
	{
		bCheckingOnlineConnection = false;
		OnlineConnectionError = TEXT("接続先の設定を確認してください。");
		return;
	}
	TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
	Message->SetStringField(TEXT("op"), TEXT("ping"));
	ChaosImpactRelay::Request(Message, [Weak = TWeakObjectPtr<AChaosImpactPlayerController>(this), Generation](TSharedPtr<FJsonObject> Reply, int32)
	{
		auto* Self = Weak.Get();
		if (!Self || Self->OnlineConnectionGeneration != Generation
			|| Self->CurrentScreen != EChaosImpactScreen::OnlineConnecting) { return; }
		Self->bCheckingOnlineConnection = false;
		bool bOk = false;
		if (Reply && Reply->TryGetBoolField(TEXT("ok"), bOk) && bOk)
		{
			Self->ShowMenuScreen(EChaosImpactScreen::OnlinePlayers);
		}
		else { Self->OnlineConnectionError = TEXT("ネットワーク接続を確認して、もう一度お試しください。"); }
	});
}

void AChaosImpactPlayerController::CancelOnlineConnection()
{
	++OnlineConnectionGeneration;
	bCheckingOnlineConnection = false;
	OnlineConnectionError.Reset();
	ShowMenuScreen(OnlineConnectionReturnScreen);
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
	OpenTrainingLevel();
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
	if (bEditingTourneyTitle)
	{
		// A public tournament's name: back to its set-up.
		bEditingTourneyTitle = false;
		if (PendingTourney.IsValid() && !Trimmed.IsEmpty())
		{
			PendingTourney->Title = Trimmed;
		}
		ShowMenuScreen(EChaosImpactScreen::TourneyCreate);
		return;
	}
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
	// (In the lobby, its countdown to the match included: the menu closes by itself when the countdown ends.)
	return IsOnlineRoom() && IsPrimaryLocalPlayerController() && !bTravelPending && Room && !Room->IsMatchmade()
		&& (Room->Phase == EChaosImpactOnlinePhase::Lobby || (Room->Phase == EChaosImpactOnlinePhase::Starting && !Room->bRoomMatchLevel));
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
	if (Room && Room->bPublicTournament)
	{
		return false;
	}
	return IsOnlineRoomHost() && Room && !Room->bVersusMatch && Room->Phase == EChaosImpactOnlinePhase::Lobby
		&& !Room->IsMatchmade();
}

bool AChaosImpactPlayerController::CanReopenRecruitment() const
{
	const AChaosImpactGameState* Room = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	if (Room && Room->bPublicTournament)
	{
		return false;
	}
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

bool AChaosImpactPlayerController::CanChangeLobbyTeam() const
{
	const AChaosImpactGameState* Room = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	return CanToggleReady() && Room->Rules.IsTeamBattle();
}

bool AChaosImpactPlayerController::CanToggleSpectating() const
{
	const AChaosImpactGameState* Room = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	if (Room && Room->bPublicTournament)
	{
		return false;
	}
	return IsOnlineRoom() && Room && !Room->bRecruitmentClosed && !Room->bVersusMatch
		&& Room->Phase == EChaosImpactOnlinePhase::Lobby;
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

void AChaosImpactPlayerController::OpenTourneyList()
{
	UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this);
	if (!Sessions || bTravelPending)
	{
		return;
	}
	PlayFlow = EChaosImpactPlayFlow::VersusOnline;
	// The players and controllers chosen come along into the tournament's room.
	Sessions->SetLocalSetup(BuildLocalSetupOptions(), RequestedLocalPlayerCount);
	Sessions->StopSearch();
	Sessions->StartSearch(ChaosImpactTourney::Password);
	ShowMenuScreen(EChaosImpactScreen::TourneyList);
}

void AChaosImpactPlayerController::RefreshTourneyList()
{
	if (UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this))
	{
		Sessions->StopSearch();
		Sessions->StartSearch(ChaosImpactTourney::Password);
	}
}

void AChaosImpactPlayerController::CloseTourneyList()
{
	StopRoomSearch();
	ShowMenuScreen(EChaosImpactScreen::MultiReady);
}

const FChaosImpactTourneyNotice& AChaosImpactPlayerController::GetPendingTourney() const
{
	static const FChaosImpactTourneyNotice None;
	return PendingTourney.IsValid() ? *PendingTourney : None;
}

void AChaosImpactPlayerController::OpenTourneyCreate()
{
	StopRoomSearch();
	if (!PendingTourney.IsValid())
	{
		PendingTourney = MakeShared<FChaosImpactTourneyNotice>();
		const UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this);
		PendingTourney->Title = FString::Printf(TEXT("%sの大会"), Sessions ? *Sessions->GetPlayerName() : TEXT(""));
		PendingTourney->MaxPlayers = 8;
		PendingTourneyMinutes = 15;
	}
	ShowMenuScreen(EChaosImpactScreen::TourneyCreate);
}

void AChaosImpactPlayerController::AdjustTourneySetting(const int32 Row, const int32 Direction)
{
	if (!PendingTourney.IsValid())
	{
		return;
	}
	FChaosImpactTourneyNotice& Notice = *PendingTourney;
	const auto Wrap = [](const int32 Value, const int32 Count) { return (Value % Count + Count) % Count; };
	if (Row == 1)
	{
		// 5 minutes to 3 hours ahead, 5 minutes at a time.
		PendingTourneyMinutes = 5 + Wrap(PendingTourneyMinutes / 5 - 1 + Direction, 36) * 5;
	}
	else if (Row == 2)
	{
		Notice.Mode = 1 - Notice.Mode;
	}
	else if (Row == 3)
	{
		Notice.MaxPlayers = 2 + Wrap(Notice.MaxPlayers - 2 + Direction, 7);
		Notice.CPUs = FMath::Min(Notice.CPUs, ChaosImpactTournament::MaxEntrants - Notice.MaxPlayers);
	}
	else if (Row == 4)
	{
		Notice.CPUs = Wrap(Notice.CPUs + Direction, ChaosImpactTournament::MaxEntrants - Notice.MaxPlayers + 1);
	}
	else if (Row == 5 && Notice.CPUs > 0)
	{
		Notice.Level = Wrap(Notice.Level + 1 + Direction, ChaosImpactMatch::CPULevelCount + 1) - 1;
	}
}

void AChaosImpactPlayerController::EditTourneyTitle()
{
	bEditingTourneyTitle = true;
	ShowMenuScreen(EChaosImpactScreen::OnlineRoomName);
}

void AChaosImpactPlayerController::CreatePublicTourney()
{
	UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this);
	if (!Sessions || !PendingTourney.IsValid() || bTravelPending)
	{
		return;
	}
	// The start on a whole minute.
	const int64 Now = FDateTime::UtcNow().ToUnixTimestamp();
	PendingTourney->StartsUnix = (Now / 60 + 1) * 60 + static_cast<int64>(PendingTourneyMinutes - 1) * 60;
	Sessions->SetLocalSetup(BuildLocalSetupOptions(), RequestedLocalPlayerCount);
	Sessions->CreateTourneyRoom(*PendingTourney);
	ShowMenuScreen(EChaosImpactScreen::OnlineStatus);
}

void AChaosImpactPlayerController::ServerSetTournamentWait_Implementation(const EChaosImpactTournamentWait Wait, const int32 WatchMatch)
{
	if (AChaosImpactGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AChaosImpactGameMode>() : nullptr)
	{
		GameMode->SetTournamentWait(this, Wait, WatchMatch);
	}
}

void AChaosImpactPlayerController::ServerAnswerTournamentCPUMatch_Implementation(const int32 Choice)
{
	if (AChaosImpactGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AChaosImpactGameMode>() : nullptr)
	{
		GameMode->AnswerTournamentCPUMatch(this, Choice);
	}
}

void AChaosImpactPlayerController::ServerSkipTournamentResults_Implementation()
{
	if (AChaosImpactGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AChaosImpactGameMode>() : nullptr)
	{
		GameMode->SkipTournamentResults(this);
	}
}

void AChaosImpactPlayerController::UpdateTournamentWidget()
{
	const AChaosImpactGameState* Match = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	// (A public tournament's room shows its countdown on it before it starts.)
	const bool bWanted = IsLocalController() && Match && (Match->IsTournament() || (Match->bPublicTournament && Match->TournamentStartsAt > 0.0));
	if (bWanted && !TournamentWidget)
	{
		// Each local player's own, in their own part of the screen.
		TournamentWidget = CreateWidget<UChaosImpactTournamentWidget>(this);
		if (TournamentWidget)
		{
			TournamentWidget->AddToPlayerScreen(40);
			UE_LOG(LogChaosImpact, Log, TEXT("Tournament screen shown for local player %d"), GetThisLocalPlayerIndex());
		}
	}
	if (TournamentWidget && !bWanted)
	{
		TournamentWidget->RemoveFromParent();
		TournamentWidget = nullptr;
		bTournamentPanelOpen = false;
	}
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

void AChaosImpactPlayerController::UpdateSpectateHold(const float DeltaSeconds)
{
	if (!IsLocalController()) { return; }
	const bool bDown = ChaosImpactSettings::IsActionDown(this, EChaosImpactAction::LobbySpectate);
	if (!bDown)
	{
		SpectateHeldSeconds = 0.f;
		bSpectateHoldNeedsRelease = false;
		return;
	}
	// Only in the lobby itself: never with a menu (characters, colours, rules) open, nor carried over from one, nor while
	// the window is in the background.
	const bool bFocused = !FSlateApplication::IsInitialized() || FSlateApplication::Get().IsActive();
	if (!bFocused || !IsGameplayActive() || !CanToggleSpectating() || bTournamentPanelOpen)
	{
		SpectateHeldSeconds = 0.f;
		bSpectateHoldNeedsRelease = true;
		return;
	}
	if (bSpectateHoldNeedsRelease) { return; }
	SpectateHeldSeconds += FMath::Clamp(DeltaSeconds, 0.f, .1f);
	if (SpectateHeldSeconds >= SpectateHoldSeconds)
	{
		SpectateHeldSeconds = 0.f;
		bSpectateHoldNeedsRelease = true;
		ToggleSpectating();
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
	if (IsOnlineRoomHost() && GetWorld())
	{
		// The host closes the room: every guest is told first (and goes to the online menu at once, instead of waiting for
		// the connection to time out), then the host leaves a moment later.
		const AChaosImpactGameState* Room = GetWorld()->GetGameState<AChaosImpactGameState>();
		const bool bMatchmade = Room && Room->IsMatchmade();
		const bool bTournament = Room && (Room->bPublicTournament || Room->IsTournament());
		int32 Told = 0;
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			AChaosImpactPlayerController* Guest = Cast<AChaosImpactPlayerController>(It->Get());
			if (Guest && !Guest->IsLocalController() && !Cast<UChildConnection>(Guest->Player))
			{
				if (bMatchmade)
				{
					Guest->ClientLeaveMatchmade(true);
				}
				else
				{
					Guest->ClientRoomDisbanded(bTournament);
				}
				++Told;
			}
		}
		if (Told > 0)
		{
			UE_LOG(LogChaosImpact, Log, TEXT("Room closed by its host: %d guest machine(s) told"), Told);
			if (UChaosImpactLoadingSubsystem* Loading = UChaosImpactLoadingSubsystem::Get(this))
			{
				Loading->BeginTravel(EChaosImpactLoadingKind::RoomLeave);
			}
			FTimerHandle LeaveTimer;
			GetWorldTimerManager().SetTimer(LeaveTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				if (UChaosImpactSessionSubsystem* Later = UChaosImpactSessionSubsystem::Get(this))
				{
					Later->LeaveRoom();
				}
			}), 0.4f, false);
			return;
		}
	}
	Sessions->LeaveRoom();
}

void AChaosImpactPlayerController::ClientRoomDisbanded_Implementation(const bool bTournament)
{
	// Every local player of the machine hears it; its first player takes the machine to the online menu.
	const UGameInstance* GameInstance = GetGameInstance();
	const ULocalPlayer* First = GameInstance && !GameInstance->GetLocalPlayers().IsEmpty() ? GameInstance->GetLocalPlayers()[0] : nullptr;
	AChaosImpactPlayerController* Primary = First ? Cast<AChaosImpactPlayerController>(First->GetPlayerController(GetWorld())) : this;
	UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this);
	if (!Primary || Primary->bTravelPending || !Sessions)
	{
		return;
	}
	UE_LOG(LogChaosImpact, Log, TEXT("The host closed the room"));
	Primary->bTravelPending = true;
	Sessions->LeaveRoom();
	Sessions->ShowNotice(bTournament ? TEXT("大会が中止されました") : TEXT("へやが解散しました"));
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
	if (RoomState && RoomState->bPublicTournament)
	{
		return false;
	}
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
	OpenTrainingLevel(true, true);
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

void AChaosImpactPlayerController::OpenOnlinePrivate()
{
	PlayFlow = EChaosImpactPlayFlow::VersusOnline;
	ShowMenuScreen(EChaosImpactScreen::OnlinePrivate);
}

void AChaosImpactPlayerController::BeginLocalTourney()
{
	PlayFlow = EChaosImpactPlayFlow::VersusLocal;
	ShowMenuScreen(EChaosImpactScreen::TourneyEntry);
}

void AChaosImpactPlayerController::StartLocalTourney()
{
	const UChaosImpactLocalTourneySubsystem* Data = UChaosImpactLocalTourneySubsystem::Get(this);
	if (!Data || !Data->GetStartBlocker().IsEmpty() || bTravelPending)
	{
		return;
	}
	if (Data->IsAllAtOnce())
	{
		// Each person takes their controller for the whole tournament (in the order they are registered).
		TourneySeats.Reset();
		const TArray<FChaosImpactTournamentEntrant> People = Data->MakeEntrants();
		for (const FChaosImpactTournamentEntrant& Person : People)
		{
			FTourneySeat& Seat = TourneySeats.AddDefaulted_GetRef();
			Seat.Name = Person.Name;
			Seat.Character = Person.Character;
			Seat.Colour = Person.Colour;
			Seat.CustomColours = Person.CustomColours;
		}
		TourneySeatTitle = TEXT("同時進行トーナメント");
		TourneySeatMatch = INDEX_NONE;
		TourneySeatsFullAt = 0.0;
		ShowMenuScreen(EChaosImpactScreen::TourneySeat);
		return;
	}
	// One match at a time: this machine has one player until a match seats its people.
	RequestedLocalPlayerCount = 1;
	RequestedKeyboardPlayerIndex = 0;
	bRequestedPrimaryUsesGamepad = false;
	JoinedInputDeviceIds.Reset();
	JoinedLegacyControllerIds.Reset();
	TravelToLocalTourney();
}

void AChaosImpactPlayerController::TravelToLocalTourney()
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
		Loading->BeginTravel(EChaosImpactLoadingKind::Training);
	}
	UE_LOG(LogChaosImpact, Log, TEXT("Local tournament: opening its level (%d local player(s))"), RequestedLocalPlayerCount);
	UGameplayStatics::OpenLevel(this, FName(*MapPackage), true,
		FString::Printf(TEXT("CITraining=1?CITargets=0?CILocalTournament=1?%s"), *BuildLocalSetupOptions()));
}

void AChaosImpactPlayerController::OpenTourneySeatsForMatch(const int32 MatchIndex)
{
	const AChaosImpactGameState* Match = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	if (!Match || !Match->Tournament.Matches.IsValidIndex(MatchIndex))
	{
		return;
	}
	const FChaosImpactTournamentState& Tournament = Match->Tournament;
	const FChaosImpactTournamentMatch& Called = Tournament.Matches[MatchIndex];
	TourneySeats.Reset();
	for (int32 Side = 0; Side < 2; ++Side)
	{
		FTourneySeat& Seat = TourneySeats.AddDefaulted_GetRef();
		if (const FChaosImpactTournamentEntrant* Who = Tournament.Entrants.IsValidIndex(Called.Entrants[Side]) ? &Tournament.Entrants[Called.Entrants[Side]] : nullptr)
		{
			Seat.Name = Who->Name;
			Seat.Character = Who->Character;
			Seat.Colour = Who->Colour;
			Seat.CustomColours = Who->CustomColours;
			Seat.bCPU = Who->bCPU;
			Seat.CPULevel = Who->CPULevel;
		}
	}
	// Which match: the round, and which of its matches really played this is.
	int32 Number = 1;
	int32 InRound = 0;
	for (const FChaosImpactTournamentMatch& Other : Tournament.Matches)
	{
		if (Other.Round == Called.Round && Other.Group == Called.Group && !Other.bBye)
		{
			++InRound;
			Number += Other.Slot < Called.Slot ? 1 : 0;
		}
	}
	TourneySeatTitle = Tournament.GetMatchName(MatchIndex) + (InRound > 1 ? FString::Printf(TEXT(" 第%d試合"), Number) : FString());
	TourneySeatMatch = MatchIndex;
	TourneySeatsFullAt = 0.0;
	bTournamentPanelOpen = false;
	UE_LOG(LogChaosImpact, Log, TEXT("Local tournament: %s, its players take their controllers"), *TourneySeatTitle);
	ShowMenuScreen(EChaosImpactScreen::TourneySeat);
}

void AChaosImpactPlayerController::PressTourneySeat(const bool bKeyboard, const int32 InputDeviceId)
{
	if (CurrentScreen != EChaosImpactScreen::TourneySeat)
	{
		return;
	}
	const int32 Device = bKeyboard ? -1 : InputDeviceId;
	if (Device < -1)
	{
		return;
	}
	// The same device again: its place given back.
	for (FTourneySeat& Seat : TourneySeats)
	{
		if (!Seat.bCPU && Seat.Device == Device)
		{
			Seat.Device = -2;
			TourneySeatsFullAt = 0.0;
			ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiBack);
			return;
		}
	}
	for (FTourneySeat& Seat : TourneySeats)
	{
		if (!Seat.bCPU && Seat.Device == -2)
		{
			Seat.Device = Device;
			ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiReady);
			break;
		}
	}
	const bool bFull = !TourneySeats.ContainsByPredicate([](const FTourneySeat& Seat) { return !Seat.bCPU && Seat.Device == -2; });
	TourneySeatsFullAt = bFull ? FPlatformTime::Seconds() : 0.0;
}

void AChaosImpactPlayerController::CancelTourneySeats()
{
	if (CurrentScreen == EChaosImpactScreen::TourneySeat && TourneySeatMatch == INDEX_NONE)
	{
		ShowMenuScreen(EChaosImpactScreen::TourneyEntry);
	}
}

void AChaosImpactPlayerController::UpdateTourneySeats()
{
	if (CurrentScreen == EChaosImpactScreen::TourneySeat && TourneySeatsFullAt > 0.0 && FPlatformTime::Seconds() - TourneySeatsFullAt >= 1.4)
	{
		FinishTourneySeats();
	}
}

void AChaosImpactPlayerController::FinishTourneySeats()
{
	TourneySeatsFullAt = 0.0;
	if (TourneySeatMatch == INDEX_NONE)
	{
		// 同時進行: this machine's players are the people, each on the device they took.
		RequestedLocalPlayerCount = FMath::Clamp(TourneySeats.Num(), 1, 4);
		RequestedKeyboardPlayerIndex = INDEX_NONE;
		JoinedInputDeviceIds.Reset();
		JoinedLegacyControllerIds.Reset();
		for (int32 Index = 0; Index < TourneySeats.Num(); ++Index)
		{
			if (TourneySeats[Index].Device == -1)
			{
				RequestedKeyboardPlayerIndex = Index;
			}
			else if (TourneySeats[Index].Device >= 0)
			{
				JoinedInputDeviceIds.Add(TourneySeats[Index].Device);
				JoinedLegacyControllerIds.Add(TourneySeats[Index].Device);
			}
		}
		bRequestedPrimaryUsesGamepad = RequestedKeyboardPlayerIndex != 0;
		TravelToLocalTourney();
		return;
	}
	// 1試合ずつ: the match's players are seated and it begins.
	TArray<int32> SideDevices;
	for (const FTourneySeat& Seat : TourneySeats)
	{
		SideDevices.Add(Seat.bCPU ? -3 : Seat.Device);
	}
	const int32 MatchIndex = TourneySeatMatch;
	TourneySeatMatch = INDEX_NONE;
	EnterPlayingScreen();
	if (AChaosImpactGameMode* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<AChaosImpactGameMode>() : nullptr)
	{
		Mode->SeatLocalTournamentMatch(MatchIndex, SideDevices);
	}
}

void AChaosImpactPlayerController::UpdateLocalTourneySeatRequest()
{
	const AChaosImpactGameState* Match = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	if (!Match || !Match->bLocalTournament || !IsPrimaryLocalPlayerController() || bTravelPending || CurrentScreen != EChaosImpactScreen::Playing)
	{
		return;
	}
	const AChaosImpactGameMode* Mode = GetWorld()->GetAuthGameMode<AChaosImpactGameMode>();
	const int32 Waiting = Mode ? Mode->GetLocalSeatRequest() : INDEX_NONE;
	if (Waiting != INDEX_NONE)
	{
		OpenTourneySeatsForMatch(Waiting);
	}
}

void AChaosImpactPlayerController::OnLocalTournamentOver()
{
	if (!IsPrimaryLocalPlayerController())
	{
		return;
	}
	RemoveSecondaryLocalPlayers();
	SetLocalInputRouting(1, 0, TArray<int32>());
	ShowMenuScreen(EChaosImpactScreen::TourneyEntry);
}

void AChaosImpactPlayerController::SetLocalInputRouting(const int32 Players, const int32 KeyboardPlayer, const TArray<int32>& PadDevices)
{
	RequestedLocalPlayerCount = FMath::Clamp(Players, 1, 4);
	RequestedKeyboardPlayerIndex = KeyboardPlayer;
	bRequestedPrimaryUsesGamepad = KeyboardPlayer != 0;
	JoinedInputDeviceIds = PadDevices;
	JoinedLegacyControllerIds = PadDevices;
}

void AChaosImpactPlayerController::OpenGallery()
{
	PlayFlow = EChaosImpactPlayFlow::VersusOnline;
	ShowMenuScreen(EChaosImpactScreen::Gallery);
}

void AChaosImpactPlayerController::OpenLineCheck()
{
	// (Its page starts the measuring as it comes up.)
	PlayFlow = EChaosImpactPlayFlow::VersusOnline;
	ShowMenuScreen(EChaosImpactScreen::LineCheck);
}

void AChaosImpactPlayerController::OpenOnlineRecord()
{
	if (UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this))
	{
		Sessions->RefreshRecords();
	}
	PlayFlow = EChaosImpactPlayFlow::VersusOnline;
	ShowMenuScreen(EChaosImpactScreen::OnlineRecord);
}

void AChaosImpactPlayerController::AnswerDuelOffer(const bool bAccept)
{
	UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this);
	if (!Sessions || !Sessions->IsDuelOffered())
	{
		return;
	}
	Sessions->AnswerDuelOffer(bAccept);
	if (MenuWidget)
	{
		MenuWidget->RefreshEntries();
	}
	if (!bAccept)
	{
		// The search is over for this player: back to the online menu.
		ShowMenuScreen(EChaosImpactScreen::MultiReady);
	}
}

void AChaosImpactPlayerController::UpdateDuelOfferScreen()
{
	UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this);
	if (!Sessions || !IsLocalController() || !IsPrimaryLocalPlayerController() || bTravelPending || IsOnlineRoom())
	{
		return;
	}
	if (Sessions->IsDuelOffered())
	{
		if (CurrentScreen != EChaosImpactScreen::DuelFound)
		{
			ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiReveal);
			// This player's own 1v1 record, for their side of the card.
			Sessions->RefreshRecords();
			ShowMenuScreen(EChaosImpactScreen::DuelFound);
			RequestDevShot(TEXT("duel_found_") + Sessions->GetPlayerName(), 1.5f);
			// Development: -CIAutoDuelAnswer=accept|decline answers a moment (-CIAutoDuelDelay=, seconds) after the card comes up.
			FString AutoAnswer;
			if (FParse::Value(FCommandLine::Get(), TEXT("CIAutoDuelAnswer="), AutoAnswer))
			{
				float Delay = 2.5f;
				FParse::Value(FCommandLine::Get(), TEXT("CIAutoDuelDelay="), Delay);
				// (On the core ticker: the training is paused under the card.)
				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this, AutoAnswer](float)
				{
					AnswerDuelOffer(AutoAnswer == TEXT("accept"));
					return false;
				}), FMath::Max(0.1f, Delay));
			}
		}
		return;
	}
	// Settled: both said yes (the card stays, saying it starts, until the room opens), the other side said no (back
	// to the training, searching on), or this side's search ended (the online menu).
	if (CurrentScreen == EChaosImpactScreen::DuelFound && !Sessions->IsQueueMatched())
	{
		if (Sessions->IsQueueing())
		{
			EnterPlayingScreen();
		}
		else
		{
			ShowMenuScreen(EChaosImpactScreen::MultiReady);
		}
	}
}

void AChaosImpactPlayerController::LeaveMatchmadeResults(const bool bSearchAgain)
{
	if (UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this))
	{
		Sessions->SetQueueAgainAfterMatch(bSearchAgain);
	}
	ReturnToLobbyFromResults();
}

void AChaosImpactPlayerController::RequestRematch()
{
	if (HasRequestedRematch())
	{
		return;
	}
	ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiReady);
	ServerRequestRematch();
}

void AChaosImpactPlayerController::ServerRequestRematch_Implementation()
{
	if (AChaosImpactGameMode* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<AChaosImpactGameMode>() : nullptr)
	{
		Mode->RequestRematch(PlayerState);
	}
}

bool AChaosImpactPlayerController::HasRequestedRematch() const
{
	const AChaosImpactPlayerState* Own = GetPlayerState<AChaosImpactPlayerState>();
	return Own && Own->bWantsRematch;
}

bool AChaosImpactPlayerController::IsRematchOpponentHere(bool& bOutAsked) const
{
	bOutAsked = false;
	const AChaosImpactGameState* Match = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	if (!Match)
	{
		return false;
	}
	for (const APlayerState* State : Match->PlayerArray)
	{
		const AChaosImpactPlayerState* Other = Cast<AChaosImpactPlayerState>(State);
		if (Other && Other != PlayerState && !Other->IsABot() && !Other->bSpectating)
		{
			bOutAsked = Other->bWantsRematch;
			return true;
		}
	}
	return false;
}

bool AChaosImpactPlayerController::IsInMatchmadeMatch() const
{
	const AChaosImpactGameState* Match = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	return IsOnlineRoom() && Match && Match->IsMatchmade() && Match->bVersusMatch
		&& (Match->Phase == EChaosImpactOnlinePhase::Intro || Match->Phase == EChaosImpactOnlinePhase::TeamSelect
			|| Match->Phase == EChaosImpactOnlinePhase::Match);
}

void AChaosImpactPlayerController::Surrender()
{
	const AChaosImpactGameState* Match = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	if (Match && Match->IsDuel() && Match->Duel.SideOf(PlayerState) != INDEX_NONE)
	{
		// The duel is given to the other player; both then see its results as usual.
		ResumeGameplay();
		ServerSurrender();
		return;
	}
	// Any other match: this machine leaves it (a ranked or 1v1 one counts as lost).
	LeaveOnlineRoom();
}

void AChaosImpactPlayerController::ServerSurrender_Implementation()
{
	AChaosImpactGameMode* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<AChaosImpactGameMode>() : nullptr;
	const AChaosImpactGameState* Match = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	if (Mode && Match && Match->IsDuel() && (Match->Phase == EChaosImpactOnlinePhase::Intro || Match->Phase == EChaosImpactOnlinePhase::Match))
	{
		const int32 Side = Match->Duel.SideOf(PlayerState);
		if (Side != INDEX_NONE)
		{
			UE_LOG(LogChaosImpact, Log, TEXT("%s surrenders"), PlayerState ? *PlayerState->GetPlayerName() : TEXT("?"));
			Mode->ForfeitDuel(Side);
		}
	}
}

void AChaosImpactPlayerController::OpenColourEdit(const EChaosImpactScreen ReturnTo)
{
	if (!IsPrimaryLocalPlayerController())
	{
		return;
	}
	ColourEditReturnScreen = ReturnTo;
	ShowMenuScreen(EChaosImpactScreen::ColourEdit);
}

bool AChaosImpactPlayerController::IsColourEditApplyOffered() const
{
	return IsOnlineRoom() && ColourEditReturnScreen == EChaosImpactScreen::Pause && CanChangeLoadoutInRoom();
}

bool AChaosImpactPlayerController::ApplyRoomLoadout(const int32 LocalIndex, const int32 NewCharacter, const int32 Preset)
{
	UChaosImpactLoadoutSubsystem* Loadouts = UChaosImpactLoadoutSubsystem::Get(this);
	const UGameInstance* GameInstance = GetGameInstance();
	if (!CanChangeLoadoutInRoom() || !Loadouts || !GameInstance || !GameInstance->GetLocalPlayers().IsValidIndex(LocalIndex))
	{
		return false;
	}
	FChaosImpactLoadout Loadout = Loadouts->GetLoadout(LocalIndex);
	Loadout.Character = ChaosImpactRoster::ClampIndex(NewCharacter);
	Loadout.ColourPreset = Preset;
	Loadouts->SetLoadout(LocalIndex, Loadout);
	if (AChaosImpactPlayerController* Local = Cast<AChaosImpactPlayerController>(
		GameInstance->GetLocalPlayers()[LocalIndex]->GetPlayerController(GetWorld())))
	{
		// Sent again on its next tick, as after a character change in the room.
		Local->bLoadoutSent = false;
	}
	UE_LOG(LogChaosImpact, Log, TEXT("Room: P%d plays as %s (preset %d) from カラーエディット"), LocalIndex + 1,
		ChaosImpactRoster::Get(NewCharacter).Name, Preset);
	return true;
}

void AChaosImpactPlayerController::CloseColourEdit()
{
	if (CurrentScreen != EChaosImpactScreen::ColourEdit)
	{
		return;
	}
	// Back to the menu it was opened from: a room's pause menu (never the mode select or the title from a room), the
	// online menu, or the mode select. The way back is kept until it has been taken.
	EChaosImpactScreen ReturnTo = ColourEditReturnScreen;
	if (IsOnlineRoom())
	{
		ReturnTo = EChaosImpactScreen::Pause;
	}
	else if (ReturnTo == EChaosImpactScreen::Pause && !IsGameplayActive() && !bTrainingMode && !bSoloMode)
	{
		ReturnTo = EChaosImpactScreen::ModeSelect;
	}
	ColourEditReturnScreen = ReturnTo;
	ShowMenuScreen(ReturnTo);
	if (CurrentScreen != EChaosImpactScreen::ColourEdit)
	{
		ColourEditReturnScreen = EChaosImpactScreen::ModeSelect;
	}
}

void AChaosImpactPlayerController::ClientLeaveMatchmade_Implementation(const bool bOpponentLeft)
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
	if (bOpponentLeft)
	{
		Sessions->ShowNotice(TEXT("対戦相手が退出しました"));
	}
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
	if (Match->bVersusMatch && Match->Phase == EChaosImpactOnlinePhase::Results)
	{
		Sessions->NotifyMatchmadeMatchOver();
	}
	// Development: -CIAutoRematch=N asks for 同じ相手と再戦 after the first N 1v1s; -CIAutoAfterMatch=search|menu leaves the
	// results that way a few seconds in (otherwise they run out by themselves).
	if (Match->Phase == EChaosImpactOnlinePhase::Results && Match->GetPhaseElapsedSeconds() > 8.0f && !bTravelPending)
	{
		// (Once per world: the answer takes a moment to come back from the host.)
		static int32 AutoRematches = 0;
		static TWeakObjectPtr<UWorld> AnsweredIn;
		int32 RematchLimit = 0;
		FParse::Value(FCommandLine::Get(), TEXT("CIAutoRematch="), RematchLimit);
		FString AfterMatch;
		if (AnsweredIn.Get() != GetWorld() && Match->Matchmade == EChaosImpactMatchmade::Duel && AutoRematches < RematchLimit)
		{
			++AutoRematches;
			AnsweredIn = GetWorld();
			RequestDevShot(FString::Printf(TEXT("results_choices_%d"), AutoRematches), 0.0f);
			RequestRematch();
			RequestDevShot(FString::Printf(TEXT("results_rematch_asked_%d"), AutoRematches), 1.0f);
		}
		else if (AnsweredIn.Get() != GetWorld() && FParse::Value(FCommandLine::Get(), TEXT("CIAutoAfterMatch="), AfterMatch))
		{
			AnsweredIn = GetWorld();
			LeaveMatchmadeResults(AfterMatch == TEXT("search"));
		}
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
	// ランダムマッチ: into this player's 戦績 (each machine its own; once per game).
	if (Match->Matchmade == EChaosImpactMatchmade::Duel && Match->IsDuel() && Match->Phase == EChaosImpactOnlinePhase::Results
		&& Match->Duel.Step == EChaosImpactDuelStep::Finished)
	{
		const int32 Side = Match->Duel.SideOf(PlayerState);
		if (Side != INDEX_NONE)
		{
			Sessions->ReportDuelRecord(Match->Duel.MatchWinner == INDEX_NONE ? TEXT("draw")
				: Match->Duel.MatchWinner == Side ? TEXT("win") : TEXT("lose"));
		}
	}
	if (Match->Matchmade == EChaosImpactMatchmade::Random && Match->bVersusMatch && Match->Phase == EChaosImpactOnlinePhase::Results)
	{
		const AChaosImpactPlayerState* Own = GetPlayerState<AChaosImpactPlayerState>();
		const TArray<AChaosImpactPlayerState*> Ranking = Match->GetRanking();
		if (Own && Ranking.Contains(Own))
		{
			int32 Place = 1;
			for (const AChaosImpactPlayerState* Other : Ranking)
			{
				Place += Other->Points > Own->Points ? 1 : 0;
			}
			Sessions->ReportRandomRecord(Place, Ranking.Num());
		}
	}
}

void AChaosImpactPlayerController::ServerSetPlayerName_Implementation(const FString& Name)
{
	const FString Trimmed = Name.TrimStartAndEnd().Left(UChaosImpactSessionSubsystem::MaxNameLength);
	if (PlayerState && !Trimmed.IsEmpty())
	{
		PlayerState->SetPlayerName(Trimmed);
		const AChaosImpactPlayerState* Member = Cast<AChaosImpactPlayerState>(PlayerState);
		if (UChaosImpactSessionSubsystem* Sessions = Member ? UChaosImpactSessionSubsystem::Get(this) : nullptr)
		{
			Sessions->RememberMember(Member->MachineToken, Trimmed);
		}
		// This machine's second player may have joined before the name arrived.
		if (const UNetConnection* Connection = GetNetConnection())
		{
			for (const UChildConnection* Child : Connection->Children)
			{
				const AChaosImpactPlayerState* ChildState = Child && Child->PlayerController
					? Child->PlayerController->GetPlayerState<AChaosImpactPlayerState>() : nullptr;
				// (Not over a name the second player chose for themselves.)
				if (Child && Child->PlayerController && Child->PlayerController->PlayerState && !(ChildState && ChildState->bOwnName))
				{
					Child->PlayerController->PlayerState->SetPlayerName(
						AChaosImpactPlayerState::MakeSecondPlayerName(Trimmed));
				}
			}
		}
		UE_LOG(LogChaosImpact, Log, TEXT("Player name set: %s"), *Trimmed);
	}
}

void AChaosImpactPlayerController::ServerSetSecondPlayerName_Implementation(const FString& Name)
{
	AChaosImpactPlayerState* Member = GetPlayerState<AChaosImpactPlayerState>();
	if (!Member || !Member->bSecondOfMachine)
	{
		return;
	}
	FString Chosen = Name.TrimStartAndEnd().Left(UChaosImpactSessionSubsystem::MaxNameLength);
	Member->bOwnName = !Chosen.IsEmpty();
	if (Chosen.IsEmpty())
	{
		// No name of their own: their machine's first player's, with (2).
		FString First;
		if (const UChildConnection* Child = Cast<UChildConnection>(GetNetConnection()))
		{
			const APlayerController* Parent = Child->Parent ? Child->Parent->PlayerController.Get() : nullptr;
			First = Parent && Parent->PlayerState ? Parent->PlayerState->GetPlayerName() : FString();
		}
		else if (const UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this))
		{
			First = Sessions->GetPlayerName();
		}
		if (First.IsEmpty())
		{
			return;
		}
		Chosen = AChaosImpactPlayerState::MakeSecondPlayerName(First);
	}
	if (Member->GetPlayerName() != Chosen)
	{
		Member->SetPlayerName(Chosen);
		Member->ForceNetUpdate();
		UE_LOG(LogChaosImpact, Log, TEXT("Second player's name: %s"), *Chosen);
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

void AChaosImpactPlayerController::OpenTrainingLevel(const bool bOnlineSearch, const bool bLoadingScreen)
{
	FString MapPackage = TrainingLevel.GetLongPackageName();
	// A local VS match goes to its own level, the stage's or the duel arena's (the training level raises a stage only
	// while that is missing).
	if (PlayFlow == EChaosImpactPlayFlow::VersusLocal && bLocalMatchRulesChosen && !bOnlineSearch)
	{
		const FString StageLevel = ChaosImpactMatch::GetMatchLevel(PendingMatchRules);
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
	// (Not in a match, and never in 訓練場: it is held on the VS stage / duel arena themselves, nothing laid over them.)
	if (GetWorld() && (GetWorld()->URL.HasOption(TEXT("CIMatch=1")) || GetWorld()->URL.HasOption(TEXT("CIRoomMatch=1")) || IsDojo()))
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
	if (GetWorld() && (GetWorld()->URL.HasOption(TEXT("CIMatch=1")) || GetWorld()->URL.HasOption(TEXT("CIRoomMatch=1")) || IsDojo()))
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
	// (Not in 訓練場 either: its stages stay as they are in a match, with only the warp pads the level itself has.)
	if (GetWorld() && (GetWorld()->URL.HasOption(TEXT("CIMatch=1")) || GetWorld()->URL.HasOption(TEXT("CIRoomMatch=1")) || IsDojo()))
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
	if (GetWorld() && (GetWorld()->URL.HasOption(TEXT("CIMatch=1")) || GetWorld()->URL.HasOption(TEXT("CIRoomMatch=1"))))
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
	if (Match && Match->bPublicTournament)
	{
		return false;
	}
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
	// In split screen H/Y hides only the spectator's own HUD, never the other player's shared information.
	if (IsPrimaryLocalPlayerController() && MatchAnnouncer && (!GetGameInstance() || GetGameInstance()->GetNumLocalPlayers() <= 1))
	{
		MatchAnnouncer->SetVisibility(bHide ? ESlateVisibility::Hidden : ESlateVisibility::HitTestInvisible);
	}
}

bool AChaosImpactPlayerController::IsSpectating() const
{
	const AChaosImpactPlayerState* Member = GetPlayerState<AChaosImpactPlayerState>();
	const auto* Match = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	return Member && (Member->bSpectating || (Match && Match->IsRoyaleEliminated(Member)));
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
	// トーナメント (a 合言葉 room's host only): its own rows: how it runs, its CPUs, their strength.
	if (Rules.bTournament && Row != 1)
	{
		FChaosImpactTournamentSettings& Tournament = Rules.Tournament;
		if (Row == 0)
		{
			// Four ways round: one at a time / with 順位決定戦 / all at once / with 順位決定戦.
			const int32 Way = (Tournament.Mode == EChaosImpactTournamentMode::Simultaneous ? 2 : 0) + (Tournament.bPlacementMatches ? 1 : 0);
			const int32 NextWay = Wrap(Way + (Direction < 0 ? -1 : 1), 4);
			Tournament.Mode = NextWay >= 2 ? EChaosImpactTournamentMode::Simultaneous : EChaosImpactTournamentMode::OneAtATime;
			Tournament.bPlacementMatches = NextWay % 2 == 1;
		}
		else if (Row == 2)
		{
			const int32 MaxCPUs = FMath::Max(0, ChaosImpactTournament::MaxEntrants - Humans);
			const int32 Count = Wrap(Tournament.CPUs.Num() + Direction, MaxCPUs + 1);
			const int32 Level = Tournament.CPUs.IsEmpty() ? INDEX_NONE : Tournament.CPUs.Last().Level;
			while (Tournament.CPUs.Num() < Count)
			{
				FChaosImpactTournamentCPU CPU;
				CPU.Name = FString::Printf(TEXT("CPU%d"), Tournament.CPUs.Num() + 1);
				CPU.Level = Level;
				Tournament.CPUs.Add(CPU);
			}
			Tournament.CPUs.SetNum(Count);
		}
		else if (Row == 3 && !Tournament.CPUs.IsEmpty())
		{
			// おまかせ (each one at random) → よわい → … → さいきょう, for every CPU at once.
			const int32 Current = Tournament.CPUs[0].Level;
			const int32 Next = Wrap(Current + 1 + Direction, ChaosImpactMatch::CPULevelCount + 1) - 1;
			for (FChaosImpactTournamentCPU& CPU : Tournament.CPUs)
			{
				CPU.Level = Next;
			}
		}
		Rules = ChaosImpactMatch::Sanitize(Rules, Humans);
		return;
	}
	if (Row == 0 && !Rules.bCrashBall && !Rules.bDuel && !Rules.bBattleRoyale)
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
		// Every kind of match in turn, sideways (the ones that cannot be chosen here passed over); the rules screen also
		// lists them all to pick from (SetMatchMode).
		int32 Next = GetMatchMode();
		for (int32 Step = 0; Step < MatchModeCount; ++Step)
		{
			Next = Wrap(Next + Direction, MatchModeCount);
			if (GetMatchModeBlocker(Next).IsEmpty())
			{
				break;
			}
		}
		SetMatchMode(Next);
		return;
	}
	else if (Row == 2 && !Rules.bDuel)
	{
		const int32 Competing = ChaosImpactMatch::GetCompetingHumans(Rules, Humans);
		if (Rules.bBattleRoyale && Rules.IsTeamBattle())
		{
			const int32 MinimumTotal = FMath::DivideAndRoundUp(FMath::Max(Competing, Rules.TeamCount), Rules.TeamCount) * Rules.TeamCount;
			const int32 MaxTotal = ChaosImpactMatch::MaxCompetitors / Rules.TeamCount * Rules.TeamCount;
			const int32 Choices = (MaxTotal - MinimumTotal) / Rules.TeamCount + 1;
			const int32 Current = (Competing + Rules.CPUCount - MinimumTotal) / Rules.TeamCount;
			Rules.CPUCount = MinimumTotal + Wrap(Current + Direction, Choices) * Rules.TeamCount - Competing;
			Rules = ChaosImpactMatch::Sanitize(Rules, Humans);
			return;
		}
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

void AChaosImpactPlayerController::ApplyMatchMode(FChaosImpactMatchRules& Rules, const int32 Mode)
{
	static constexpr int32 Teams[] = {0, 2, 3, 4, 2, 0, 0, 2, 3, 4, 0};
	const int32 Kind = FMath::Clamp(Mode, 0, MatchModeCount - 1);
	Rules.bTournament = Kind == 10;
	Rules.bCrashBall = Kind == 4;
	Rules.bDuel = Kind == 5 || Kind == 10;
	Rules.bBattleRoyale = Kind >= 6 && Kind < 10;
	Rules.TeamCount = Teams[Kind];
}

int32 AChaosImpactPlayerController::GetMatchMode() const
{
	const FChaosImpactMatchRules& Rules = PendingMatchRules;
	return Rules.bTournament ? 10 : Rules.bBattleRoyale ? (Rules.TeamCount >= 2 ? Rules.TeamCount + 5 : 6)
		: Rules.bDuel ? 5 : Rules.bCrashBall ? 4 : (Rules.TeamCount >= 2 ? Rules.TeamCount - 1 : 0);
}

FString AChaosImpactPlayerController::GetMatchModeBlocker(const int32 Mode) const
{
	const int32 Humans = GetMatchHumanCount();
	const AChaosImpactGameState* Room = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	if (Mode == 5 && Humans > 2)
	{
		return TEXT("3人以上では選べません");
	}
	if (Mode == 8 && Humans > 6)
	{
		return TEXT("7人以上では選べません");
	}
	if (Mode == 10 && !(IsOnlineRoomHost() && Room && !Room->IsMatchmade()))
	{
		return TEXT("合言葉の部屋のホストのみ");
	}
	return FString();
}

void AChaosImpactPlayerController::SetMatchMode(const int32 Mode)
{
	if (Mode < 0 || Mode >= MatchModeCount || !GetMatchModeBlocker(Mode).IsEmpty())
	{
		return;
	}
	const int32 Humans = GetMatchHumanCount();
	FChaosImpactMatchRules& Rules = PendingMatchRules;
	ApplyMatchMode(Rules, Mode);
	if (Rules.bTournament && Rules.Tournament.CPUs.IsEmpty() && Humans < 2)
	{
		// A lone person: a CPU to face at least.
		FChaosImpactTournamentCPU CPU;
		CPU.Name = TEXT("CPU1");
		Rules.Tournament.CPUs.Add(CPU);
	}
	Rules = ChaosImpactMatch::Sanitize(Rules, Humans);
}

int32 AChaosImpactPlayerController::GetMatchModeChoice() const
{
	const int32 Mode = GetMatchMode();
	return Mode == 10 && PendingMatchRules.Tournament.Mode == EChaosImpactTournamentMode::Simultaneous ? TournamentAllAtOnceChoice : Mode;
}

void AChaosImpactPlayerController::SetMatchModeChoice(const int32 Choice)
{
	const bool bTournament = Choice == 10 || Choice == TournamentAllAtOnceChoice;
	SetMatchMode(bTournament ? 10 : Choice);
	if (bTournament && PendingMatchRules.bTournament)
	{
		PendingMatchRules.Tournament.Mode = Choice == TournamentAllAtOnceChoice ? EChaosImpactTournamentMode::Simultaneous
			: EChaosImpactTournamentMode::OneAtATime;
	}
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

void AChaosImpactPlayerController::SetBallSwitchOn(const EChaosImpactBallType Type, const bool bOn)
{
	ChaosImpactBallSwitch::SetOn(PendingMatchRules, Type, bOn);
}

void AChaosImpactPlayerController::SetAllBallsOn()
{
	PendingMatchRules.OffBalls = 0;
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
		// A rematch on another stage (or a duel after a stage, or back): over to that match's own level (the same one
		// restarts in place below).
		const FString StageLevel = ChaosImpactMatch::GetMatchLevel(PendingMatchRules);
		if (FPackageName::DoesPackageExist(StageLevel)
			&& UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName()) != StageLevel)
		{
			bLocalMatchRulesChosen = true;
			OpenTrainingLevel();
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
	FTSTicker::GetCoreTicker().RemoveTicker(WarmUpTicker);
	WarmUpTicker.Reset();
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
	if (FetchingPaint != 0)
	{
		// (Fetched again by the next level's controller.)
		if (UChaosImpactPaintCache* Cache = UChaosImpactPaintCache::Get(this))
		{
			Cache->FinishFetch(FetchingPaint, true);
		}
		FetchingPaint = 0;
	}
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
		OpenTrainingLevel();
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
			// カスタムカラー as the match will show it.
			const UChaosImpactColourSubsystem* Colours = UChaosImpactColourSubsystem::Get(this);
			if (Colours)
			{
				Preview->ShowCustomColours(Colours->GetPresetParts(Loadout.Character, Loadout.ColourPreset));
			}
			Preview->ShowPresetPaint(Loadout.Character, Loadout.ColourPreset);
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
		OpenTrainingLevel(false, false);
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
		Entrant.Team = Match->IsTeamBattle() ? Member->TeamIndex : -1;
		Info.Entrants.Add(Entrant);
	}
	const FChaosImpactMatchRules& Rules = Match->Rules;
	Info.Teams = Match->IsTeamBattle() ? Rules.TeamCount : 0;
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
				// Each fighter's カスタムカラー, as everyone receives it.
				if (!Member->IsABot())
				{
					Preview->ShowCustomColours(Member->CustomColours);
					// (Someone else's pictures only when the settings show them.)
					const bool bOwn = Member->GetPlayerController() && Member->GetPlayerController()->IsLocalController();
					if (Preview->GetModel() && Member->CharacterIndex == Preview->GetModel()->GetCharacterIndex() && (bOwn || ChaosImpactSettings::IsOthersPaintShown()))
					{
						Preview->ShowPaintKey(Member->PaintKey);
					}
				}
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
	// (In a room the rules only go back to the room itself, whatever was left as the way back.)
	const bool bInRoomScreen = MatchRulesReturnScreen == EChaosImpactScreen::Playing || MatchRulesReturnScreen == EChaosImpactScreen::Pause
		|| MatchRulesReturnScreen == EChaosImpactScreen::TeamSelect;
	if (MatchRulesReturnScreen == EChaosImpactScreen::Playing || (IsOnlineRoom() && !bInRoomScreen))
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

void AChaosImpactPlayerController::SetOwnTeam(const int32 Team)
{
	ServerSetTeam(Team);
}

void AChaosImpactPlayerController::ServerSetTeam_Implementation(const int32 Team)
{
	if (AChaosImpactGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AChaosImpactGameMode>() : nullptr)
	{
		GameMode->SetMemberTeam(GetPlayerState<AChaosImpactPlayerState>(), Team);
	}
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
	if (IsPrimaryLocalPlayerController() && CurrentScreen != EChaosImpactScreen::Playing)
	{
		UE_LOG(LogChaosImpact, Log, TEXT("Menu screen: Playing"));
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
	if (GetWorld() && GetWorld()->URL.HasOption(TEXT("CILocalTournament=1")))
	{
		// ローカルトーナメント: the registration dresses this machine's players (not the select screen's choices).
		return;
	}
	// The preset's colours themselves (everyone else's machine has no such preset).
	const UChaosImpactColourSubsystem* Colours = UChaosImpactColourSubsystem::Get(this);
	const TArray<uint8> Custom = Colours ? Colours->GetPresetParts(Loadout.Character, Loadout.ColourPreset) : TArray<uint8>();
	if (HasAuthority())
	{
		ServerSetLoadout_Implementation(Loadout.Character, Loadout.Colour, Custom);
	}
	else
	{
		ServerSetLoadout(Loadout.Character, Loadout.Colour, Custom);
	}
	SendPaint(Loadout.Character, Custom.IsEmpty() ? INDEX_NONE : Loadout.ColourPreset);
}

void AChaosImpactPlayerController::SendPaint(const int32 PaintCharacter, const int32 Preset)
{
	UChaosImpactPaintCache* Cache = UChaosImpactPaintCache::Get(this);
	int32 Key = Cache && Preset >= 0 ? Cache->RegisterPreset(PaintCharacter, Preset) : 0;
#if !UE_BUILD_SHIPPING
	// Development: -CIDevPaint=<n> wears a striped picture of its own (n picks the colour), whatever the preset (online checks).
	if (int32 DevPaint = 0; Cache && FParse::Value(FCommandLine::Get(), TEXT("CIDevPaint="), DevPaint) && DevPaint > 0)
	{
		UChaosImpactPaintLayer* Layer = NewObject<UChaosImpactPaintLayer>(this);
		Layer->Init();
		const FColor Stripe = FLinearColor::MakeFromHSV8(static_cast<uint8>(DevPaint * 70), 230, 255).ToFColor(true);
		TArray<FColor>& Pixels = Layer->GetPixels();
		for (int32 Y = 0; Y < ChaosImpactBodyPaint::Size; ++Y)
		{
			for (int32 X = 0; X < ChaosImpactBodyPaint::Size; ++X)
			{
				if (((Y / 24) % 3) == 0)
				{
					Pixels[Y * ChaosImpactBodyPaint::Size + X] = Stripe;
				}
			}
		}
		TArray<FChaosImpactPaintLayerData> DevLayers;
		DevLayers.Add({0, Layer->ToPng()});
		Key = Cache->Register(DevLayers);
		UE_LOG(LogChaosImpact, Log, TEXT("Dev paint %d: key %d"), DevPaint, Key);
	}
#endif
	if (HasAuthority())
	{
		if (AChaosImpactPlayerState* State = GetPlayerState<AChaosImpactPlayerState>())
		{
			State->PaintKey = Key;
			State->ForceNetUpdate();
		}
		return;
	}
	const TArray<uint8>* Bytes = Cache ? Cache->GetPacked(Key) : nullptr;
	ServerOfferPaint(Key, Bytes ? Bytes->Num() : 0);
}

void AChaosImpactPlayerController::ServerOfferPaint_Implementation(const int32 Key, const int32 Total)
{
	AChaosImpactPlayerState* State = GetPlayerState<AChaosImpactPlayerState>();
	UChaosImpactPaintCache* Cache = UChaosImpactPaintCache::Get(this);
	if (!State || !Cache)
	{
		return;
	}
	if (Key == 0 || Cache->Has(Key) || Total <= 0 || Total > UChaosImpactPaintCache::MostBytes)
	{
		State->PaintKey = Key != 0 && Cache->Has(Key) ? Key : 0;
		State->ForceNetUpdate();
		return;
	}
	// Not here yet: none shown until it has all come.
	State->PaintKey = 0;
	ClientSendPaintFrom(Key, 0);
}

void AChaosImpactPlayerController::ClientSendPaintFrom_Implementation(const int32 Key, const int32 Offset)
{
	const UChaosImpactPaintCache* Cache = UChaosImpactPaintCache::Get(this);
	const TArray<uint8>* Bytes = Cache ? Cache->GetPacked(Key) : nullptr;
	if (Bytes && Offset >= 0 && Offset < Bytes->Num())
	{
		ServerPaintPiece(Key, Offset, Bytes->Num(), Cache->GetPiece(Key, Offset));
	}
}

void AChaosImpactPlayerController::ServerPaintPiece_Implementation(const int32 Key, const int32 Offset, const int32 Total,
	const TArray<uint8>& Bytes)
{
	AChaosImpactPlayerState* State = GetPlayerState<AChaosImpactPlayerState>();
	UChaosImpactPaintCache* Cache = UChaosImpactPaintCache::Get(this);
	if (!State || !Cache || Bytes.Num() > UChaosImpactPaintCache::PieceBytes)
	{
		return;
	}
	int32 Next = INDEX_NONE;
	if (Cache->Receive(Key, Offset, Total, Bytes, Next))
	{
		State->PaintKey = Key;
		State->ForceNetUpdate();
		UE_LOG(LogChaosImpact, Log, TEXT("Paint %d came from %s (%d bytes)"), Key, *State->GetPlayerName(), Total);
	}
	else if (Next != INDEX_NONE)
	{
		ClientSendPaintFrom(Key, Next);
	}
}

void AChaosImpactPlayerController::ServerWantPaint_Implementation(const int32 Key, const int32 Offset)
{
	const UChaosImpactPaintCache* Cache = UChaosImpactPaintCache::Get(this);
	const TArray<uint8>* Bytes = Cache ? Cache->GetPacked(Key) : nullptr;
	if (!Bytes || Offset < 0 || Offset >= Bytes->Num())
	{
		ClientPaintPiece(Key, Offset, 0, TArray<uint8>());
		return;
	}
	ClientPaintPiece(Key, Offset, Bytes->Num(), Cache->GetPiece(Key, Offset));
}

void AChaosImpactPlayerController::ClientPaintPiece_Implementation(const int32 Key, const int32 Offset, const int32 Total,
	const TArray<uint8>& Bytes)
{
	UChaosImpactPaintCache* Cache = UChaosImpactPaintCache::Get(this);
	if (!Cache || Key != FetchingPaint)
	{
		return;
	}
	int32 Next = INDEX_NONE;
	if (Total > 0 && Bytes.Num() <= UChaosImpactPaintCache::PieceBytes && Cache->Receive(Key, Offset, Total, Bytes, Next))
	{
		Cache->FinishFetch(Key, false);
		FetchingPaint = 0;
		UE_LOG(LogChaosImpact, Log, TEXT("Paint %d fetched from the host (%d bytes)"), Key, Total);
	}
	else if (Total > 0 && Next != INDEX_NONE)
	{
		ServerWantPaint(Key, Next);
	}
	else
	{
		// The host has none of it (yet), or it came wrong: asked again a little later.
		Cache->FinishFetch(Key, true);
		FetchingPaint = 0;
	}
}

void AChaosImpactPlayerController::ServerSetLoadout_Implementation(const int32 InCharacterIndex, const int32 InColour,
	const TArray<uint8>& InCustomColours)
{
	if (AChaosImpactPlayerState* State = GetPlayerState<AChaosImpactPlayerState>())
	{
		State->CharacterIndex = ChaosImpactRoster::ClampIndex(InCharacterIndex);
		State->ColourChoice = FMath::Clamp(InColour, 0, ChaosImpactRoster::ColourCount - 1);
		State->CustomColours = ChaosImpactCustomColour::Clean(State->CharacterIndex, InCustomColours);
		State->ForceNetUpdate();
		if (UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this))
		{
			Sessions->RememberMember(State->MachineToken, FString(), State->CharacterIndex, State->ColourChoice, State->CustomColours);
		}
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
	OpenTrainingLevel(false, false);
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

void AChaosImpactPlayerController::UpdateNearCameraParts()
{
	// How near this player's camera a wall prop may come before it leaves this player's view (the camera hangs 800 cm
	// from its player, so anything this near stands between the two).
	constexpr float HideWithin = 760.0f;
	if (!IsLocalController() || !PlayerCameraManager || !GetWorld())
	{
		return;
	}
	if (!NearPartsStage.IsValid())
	{
		const double Now = GetWorld()->GetTimeSeconds();
		if (Now < NextNearPartsSearchAt)
		{
			return;
		}
		NextNearPartsSearchAt = Now + 1.0;
		for (TActorIterator<AChaosImpactStageBase> It(GetWorld()); It; ++It)
		{
			if (!It->GetNearCameraParts().IsEmpty())
			{
				NearPartsStage = *It;
				break;
			}
		}
		if (!NearPartsStage.IsValid())
		{
			return;
		}
	}
	const FVector Eye = PlayerCameraManager->GetCameraLocation();
	for (const TWeakObjectPtr<UPrimitiveComponent>& Part : NearPartsStage->GetNearCameraParts())
	{
		if (!Part.IsValid())
		{
			continue;
		}
		if (Part->Bounds.GetBox().ComputeSquaredDistanceToPoint(Eye) < FMath::Square(HideWithin))
		{
			HiddenPrimitiveComponents.AddUnique(Part);
		}
		else
		{
			HiddenPrimitiveComponents.Remove(Part);
		}
	}
}

void AChaosImpactPlayerController::PreClientTravel(const FString& PendingURL, const ETravelType TravelType, const bool bIsSeamlessTravel)
{
	Super::PreClientTravel(PendingURL, TravelType, bIsSeamlessTravel);
	// The room's host is taking everyone to the match's own level, or back to the lobby: this machine loads it and comes
	// back in, under the loading screen.
	if (GetNetMode() == NM_Client && IsPrimaryLocalPlayerController()
		&& (PendingURL.Contains(TEXT("CIRoomMatch=1")) || PendingURL.Contains(TEXT("CIRoomReturn=1"))))
	{
		if (UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this))
		{
			Sessions->NoteRoomTravel();
		}
		if (UChaosImpactLoadingSubsystem* Loading = UChaosImpactLoadingSubsystem::Get(this))
		{
			Loading->BeginTravel(PendingURL.Contains(TEXT("CIRoomMatch=1")) ? EChaosImpactLoadingKind::RoomMatch
				: EChaosImpactLoadingKind::RoomLobby);
		}
	}
}

void AChaosImpactPlayerController::PlayerTick(const float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	// ペイント someone here is shown with but this machine does not have: fetched from the host, one set at a time.
	if (FetchingPaint == 0 && !HasAuthority() && IsLocalController() && IsPrimaryLocalPlayerController())
	{
		if (UChaosImpactPaintCache* Cache = UChaosImpactPaintCache::Get(this))
		{
			FetchingPaint = Cache->TakeWanted();
			if (FetchingPaint != 0)
			{
				ServerWantPaint(FetchingPaint, 0);
			}
		}
	}
	// Online, a machine's second player goes by the nickname they picked (or the first player's name with (2)).
	if (IsLocalController() && IsOnlineRoom() && GetThisLocalPlayerIndex() > 0 && PlayerState && FPlatformTime::Seconds() >= NextSecondNameAt)
	{
		NextSecondNameAt = FPlatformTime::Seconds() + 1.0;
		const int32 Profile = ChaosImpactSettings::GetPlayerProfile(this);
		const FString Wanted = Profile > 0 ? ChaosImpactSettings::GetProfile(Profile).Name : FString();
		if (!bSentSecondName || Wanted != SentSecondName)
		{
			bSentSecondName = true;
			SentSecondName = Wanted;
			ServerSetSecondPlayerName(Wanted);
		}
	}
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
	UpdateTournamentWidget();
	UpdateLocalTourneySeatRequest();

	UpdateMatchmadeProgress();
	UpdateDuelOfferScreen();
	UpdateMatchScreens();
	UpdateRecruitmentHold(DeltaTime);
	UpdateSpectateHold(DeltaTime);
	UpdateOnlineVersusCard(DeltaTime);
	UpdateMatchIntroCamera();
	UpdateNearCameraParts();
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
	// Development (-CIDevSimaePerch=<seconds>): a flock of simaenaga let loose on this player that long in, and again
	// every 12 s (screenshots of the perched-bird card).
	static const float DevPerchDelay = []()
	{
		float Value = -1.0f;
		FParse::Value(FCommandLine::Get(), TEXT("CIDevSimaePerch="), Value);
		return Value;
	}();
	if (DevPerchDelay >= 0.0f && HasAuthority() && IsLocalController() && IsPrimaryLocalPlayerController() && GetPawn() && GetWorld()
		&& GetWorld()->GetTimeSeconds() >= DevPerchDelay + DevPerchCount * 12.0f)
	{
		++DevPerchCount;
		AChaosImpactSimaeBird::ReleaseFlock(GetWorld(), GetPawn()->GetActorLocation() + GetPawn()->GetActorForwardVector() * 60.0f, nullptr, GetPawn());
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
	// The room's own menus stay open through the countdown to a match; when it ends (the match, a tournament, or the
	// lobby's own match begins), whatever is open closes and play goes on: nothing chosen there makes anyone watch or
	// leave. (A pick not yet confirmed is simply not taken.)
	const bool bLobbyNow = Match->Phase == EChaosImpactOnlinePhase::Lobby
		|| (Match->Phase == EChaosImpactOnlinePhase::Starting && !Match->bRoomMatchLevel);
	const bool bRoomMenu = CurrentScreen == EChaosImpactScreen::CharacterSelect || CurrentScreen == EChaosImpactScreen::OnlineName
		|| CurrentScreen == EChaosImpactScreen::OnlineRoomName || CurrentScreen == EChaosImpactScreen::Settings
		|| CurrentScreen == EChaosImpactScreen::ColourEdit || CurrentScreen == EChaosImpactScreen::Pause;
	if (IsOnlineRoom() && !bLobbyNow && bSawRoomCountdown && bRoomMenu)
	{
		UE_LOG(LogChaosImpact, Log, TEXT("The countdown ended with screen %d open: closing it for the match"), static_cast<int32>(CurrentScreen));
		bRoomCharacterChange = false;
		bRenamingPlayer = false;
		bRenamingRoom = false;
		EnterPlayingScreen();
	}
	else if ((bRoomCharacterChange || bRenamingPlayer) && !bLobbyNow)
	{
		bRoomCharacterChange = false;
		bRenamingPlayer = false;
		EnterPlayingScreen();
	}
	bSawRoomCountdown = Match->Phase == EChaosImpactOnlinePhase::Starting && !Match->bRoomMatchLevel;
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
	float Elapsed = Match ? Match->GetIntroElapsedSeconds() : 0.0f;
	const float CameraSeconds = ChaosImpactMatch::FlyoverSeconds + ChaosImpactMatch::DiveSeconds;
	// A tournament's match opens in its arena as a duel opens: the same sweep around the arena and dive into the player.
	FVector StageCenter = Match ? FVector(Match->StageCenter) : FVector::ZeroVector;
	bool bTournamentOpening = false;
	if (Match && Match->IsTournament() && PlayerCharacter && !bDemoPlay)
	{
		const AChaosImpactPlayerState* Member = GetPlayerState<AChaosImpactPlayerState>();
		const FChaosImpactDuelState* Duel = Member ? Match->FindDuelFor(Member) : nullptr;
		if (Duel && Duel->Step == EChaosImpactDuelStep::Opening && Match->TournamentArenas.IsValidIndex(Member->TournamentArena))
		{
			bTournamentOpening = true;
			Elapsed = Match->GetDuelStepElapsed(*Duel);
			StageCenter = Match->TournamentArenas[Member->TournamentArena].Center;
		}
	}
	const bool bActive = IsLocalController() && PlayerCharacter && Match && Elapsed < CameraSeconds
		&& (bTournamentOpening || (Match->bVersusMatch && Match->Phase == EChaosImpactOnlinePhase::Intro));
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
		// (Not in a tournament: the views may be in different arenas.)
		const bool bFullScreen = bActive && !bTournamentOpening && Elapsed < ChaosImpactMatch::FlyoverSeconds;
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
	const float DuelViewYaw = Match->IsDuel() || bTournamentOpening ? PlayerCharacter->GetTopDownCameraRotation().Yaw : 0.f;
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

bool AChaosImpactPlayerController::IsDojo() const
{
	return AChaosImpactDojo::IsDojoWorld(GetWorld());
}

void AChaosImpactPlayerController::OpenDojo(const FString& Level, const FString& Lesson, const FString& Focus)
{
	if (bTravelPending)
	{
		return;
	}
	// One player, on the first VS stage's own level; the dojo sets everything else out itself, all through its own options
	// (training's settings, its CPUs, targets and players, are left as they were).
	RequestedLocalPlayerCount = 1;
	BuildFallbackControllerAssignments();
	bTravelPending = true;
	ExitTrainingOverlayPresentation();
	SetPause(false);
	FString Options = FString::Printf(TEXT("CITraining=1?CIDojo=1?CITargets=0?CICPUCount=0?%s"), *BuildLocalSetupOptions());
	if (!Lesson.IsEmpty())
	{
		Options += FString::Printf(TEXT("?CIDojoLesson=%s"), *Lesson);
	}
	if (!Focus.IsEmpty())
	{
		Options += FString::Printf(TEXT("?CIDojoFocus=%s"), *Focus);
	}
	bIntroFullScreen = false;
	if (UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
	{
		Viewport->SetForceDisableSplitscreen(false);
	}
	if (UChaosImpactLoadingSubsystem* Loading = UChaosImpactLoadingSubsystem::Get(this))
	{
		Loading->BeginTravel(EChaosImpactLoadingKind::Training);
	}
	UGameplayStatics::OpenLevel(this, FName(Level.IsEmpty() ? *ChaosImpactMatch::GetStageLevel(0) : *Level), true, Options);
}

void AChaosImpactPlayerController::OpenDojoMatch(const FChaosImpactMatchRules& Rules, const FString& Lesson)
{
	if (bTravelPending)
	{
		return;
	}
	// Opened as a local VS match is (the stage's own level, its rules in the options), with the dojo along.
	RequestedLocalPlayerCount = 1;
	BuildFallbackControllerAssignments();
	bTravelPending = true;
	ExitTrainingOverlayPresentation();
	SetPause(false);
	const FString Options = FString::Printf(TEXT("CITraining=1?%s?CIVersus=1?CITargets=0?%s?CIDojo=1?CIDojoLesson=%s"),
		*BuildLocalSetupOptions(), *ChaosImpactMatch::ToOptions(Rules), *Lesson);
	bIntroFullScreen = false;
	if (UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
	{
		Viewport->SetForceDisableSplitscreen(false);
	}
	if (UChaosImpactLoadingSubsystem* Loading = UChaosImpactLoadingSubsystem::Get(this))
	{
		Loading->BeginTravel(EChaosImpactLoadingKind::Training);
	}
	UGameplayStatics::OpenLevel(this, FName(*ChaosImpactMatch::GetMatchLevel(Rules)), true, Options);
}

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

bool AChaosImpactPlayerController::HasBattleRoyaleIsland()
{
	return FPackageName::DoesPackageExist(TEXT("/Game/ChaosImpact/BattleRoyale/Lvl_BRIsland"));
}

bool AChaosImpactPlayerController::OpenBattleRoyaleIsland()
{
	if (bTravelPending || !HasBattleRoyaleIsland())
	{
		return false;
	}
	bTravelPending = true;
	SetPause(false);
	bIntroFullScreen = false;
	UGameplayStatics::OpenLevel(this, FName(TEXT("/Game/ChaosImpact/BattleRoyale/Lvl_BRIsland")), true, TEXT("CIOpenWorld=1?CITargets=0?CICPUCount=0"));
	return true;
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
