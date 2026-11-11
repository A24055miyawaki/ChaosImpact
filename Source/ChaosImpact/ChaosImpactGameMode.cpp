// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosImpactGameMode.h"
#include "ChaosImpactBallSwitch.h"

#include "ChaosImpactDojo.h"
#include "ChaosImpactHook.h"
#include "ChaosImpact.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactBallSpawner.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactCharacterRoster.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactCrashBall.h"
#include "ChaosImpactHazardZone.h"
#include "ChaosImpactLoadingScreen.h"
#include "ChaosImpactSessionSubsystem.h"
#include "ChaosImpactSpectatorPawn.h"
#include "ChaosImpactTornado.h"
#include "ChaosImpactSimaeBird.h"
#include "ChaosImpactTrainingTarget.h"
#include "ChaosImpactVersusStage.h"
#include "ChaosImpactSplashStage.h"
#include "ChaosImpactTempleStage.h"
#include "ChaosImpactCyberStage.h"
#include "ChaosImpactSnowStage.h"
#include "ChaosImpactForgeStage.h"
#include "ChaosImpactDuelStage.h"
#include "ChaosImpactPlayerController.h"
#include "GameFramework/GameSession.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"

#include "Engine/ChildConnection.h"
#include "Engine/GameInstance.h"
#include "Engine/NetConnection.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "GameMapsSettings.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "JoyShockBlueprintLibrary.h"
#include "JoyShockTypes.h"
#include "TimerManager.h"

AChaosImpactGameMode::AChaosImpactGameMode()
{
	GameStateClass = AChaosImpactGameState::StaticClass();
	PlayerStateClass = AChaosImpactPlayerState::StaticClass();
	VersusStageClass = AChaosImpactVersusStage::StaticClass();
	SplashStageClass = AChaosImpactSplashStage::StaticClass();
	TempleStageClass = AChaosImpactTempleStage::StaticClass();
	CyberStageClass = AChaosImpactCyberStage::StaticClass();
	SnowStageClass = AChaosImpactSnowStage::StaticClass();
	ForgeStageClass = AChaosImpactForgeStage::StaticClass();
	DuelStageClass = AChaosImpactDuelStage::StaticClass();
}

void AChaosImpactGameMode::BeginPlay()
{
	Super::BeginPlay();
	// 訓練場: its lessons run here (offline only).
	if (AChaosImpactDojo::IsDojoWorld(GetWorld()) && GetNetMode() == NM_Standalone && !AChaosImpactDojo::Find(GetWorld()))
	{
		FActorSpawnParameters DojoParameters;
		DojoParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		GetWorld()->SpawnActor<AChaosImpactDojo>(AChaosImpactDojo::StaticClass(), FTransform::Identity, DojoParameters);
	}
#if !UE_BUILD_SHIPPING
	// Development: -CIDevBlackHole=<seconds> opens the host player's black hole beside every remote player,
	// to check online that a black hole thrown by the host draws the other machines' players in.
	float DevBlackHoleSeconds = 0.0f;
	if (IsOnlineRoom() && FParse::Value(FCommandLine::Get(), TEXT("CIDevBlackHole="), DevBlackHoleSeconds)
		&& DevBlackHoleSeconds > 0.5f)
	{
		GetWorldTimerManager().SetTimer(DevBlackHoleTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			const UGameInstance* GameInstance = GetGameInstance();
			const APlayerController* HostController = GameInstance ? GameInstance->GetFirstLocalPlayerController(GetWorld()) : nullptr;
			APawn* HostPawn = HostController ? HostController->GetPawn() : nullptr;
			for (TActorIterator<AChaosImpactCharacter> It(GetWorld()); It && HostPawn; ++It)
			{
				if (It->IsRemotePlayerOnServer() && !It->IsEliminated())
				{
					const FVector Beside = It->GetActorLocation() + FVector(420.0f, 0.0f, 0.0f);
					AChaosImpactHazardZone::Detonate(GetWorld(), EChaosImpactBallType::Black, Beside, HostPawn, nullptr);
					UE_LOG(LogChaosImpact, Log, TEXT("DevBlackHole opened by %s beside %s"),
						*HostPawn->GetName(), *GetNameSafe(It->GetPlayerState()));
				}
			}
		}), DevBlackHoleSeconds, true, DevBlackHoleSeconds);
	}
	// Development: -CIDevWind=<seconds> releases the host player's tornado toward every remote player, with a ball
	// lying on its way and another thrown into it a moment later, to check a wind ball online (see -CIWindLog).
	float DevWindSeconds = 0.0f;
	if (IsOnlineRoom() && FParse::Value(FCommandLine::Get(), TEXT("CIDevWind="), DevWindSeconds) && DevWindSeconds > 1.0f)
	{
		GetWorldTimerManager().SetTimer(DevWindTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			const UGameInstance* GameInstance = GetGameInstance();
			const APlayerController* HostController = GameInstance ? GameInstance->GetFirstLocalPlayerController(GetWorld()) : nullptr;
			APawn* HostPawn = HostController ? HostController->GetPawn() : nullptr;
			for (TActorIterator<AChaosImpactCharacter> It(GetWorld()); It && HostPawn; ++It)
			{
				if (!It->IsRemotePlayerOnServer() || It->IsEliminated())
				{
					continue;
				}
				const FVector Target = It->GetActorLocation();
				FVector Toward = (Target - HostPawn->GetActorLocation()).GetSafeNormal2D();
				if (Toward.IsNearlyZero())
				{
					Toward = FVector::ForwardVector;
				}
				const FVector From = Target - Toward * 650.0f;
				AChaosImpactTornado::Release(GetWorld(), From, Toward, HostPawn);
				const FTransform Lying(FRotator::ZeroRotator, Target - Toward * 330.0f + FVector(0.0f, 0.0f, -40.0f));
				if (AChaosImpactBall* Pickup = GetWorld()->SpawnActorDeferred<AChaosImpactBall>(AChaosImpactBall::StaticClass(),
					Lying, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
				{
					Pickup->FinishSpawning(Lying);
					Pickup->MakePickup();
				}
				UE_LOG(LogChaosImpact, Log, TEXT("DevWind released by %s toward %s"), *HostPawn->GetName(),
					*GetNameSafe(It->GetPlayerState()));
				// A ball thrown into the funnel from the side, after it has passed the player.
				FTimerHandle ThrowTimer;
				TWeakObjectPtr<APawn> WeakHost = HostPawn;
				GetWorldTimerManager().SetTimer(ThrowTimer, FTimerDelegate::CreateWeakLambda(this, [this, WeakHost]()
				{
					APawn* Thrower = WeakHost.Get();
					for (TActorIterator<AChaosImpactTornado> Tornado(GetWorld()); Tornado && Thrower; ++Tornado)
					{
						if (!Tornado->IsActive())
						{
							continue;
						}
						const FVector From = Tornado->GetCenter() + FVector(0.0f, 280.0f, 60.0f);
						const FTransform Flight(FRotator::ZeroRotator, From);
						if (AChaosImpactBall* Ball = GetWorld()->SpawnActorDeferred<AChaosImpactBall>(AChaosImpactBall::StaticClass(),
							Flight, Thrower, Thrower, ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
						{
							Ball->FinishSpawning(Flight);
							Ball->Launch((Tornado->GetCenter() - From).GetSafeNormal2D(), 2200.0f, EChaosImpactBallFlightMode::Straight, 0.0f);
							UE_LOG(LogChaosImpact, Log, TEXT("DevWind ball thrown into the tornado"));
						}
					}
				}), 2.4f, false);
			}
		}), DevWindSeconds, true, 3.0f);
	}
#endif
	if (AChaosImpactGameState* RoomState = GetGameState<AChaosImpactGameState>(); RoomState && IsOnlineRoom())
	{
		RoomState->bOnlineRoom = true;
		RoomState->Phase = EChaosImpactOnlinePhase::Lobby;
		if (const UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this))
		{
			RoomState->RoomPassword = Sessions->GetPassword();
			RoomState->RoomName = Sessions->GetRoomName();
		}
		bRoomMatchLevel = GetWorld()->URL.HasOption(TEXT("CIRoomMatch=1"));
		RoomState->bRoomMatchLevel = bRoomMatchLevel;
		// Back in the lobby from a match: the development shortcuts below do not start another one by themselves.
		const bool bBackFromMatch = GetWorld()->URL.HasOption(TEXT("CIRoomReturn=1"));
		if (bRoomMatchLevel || bBackFromMatch)
		{
			// The room's game listens afresh here: the relay's player sockets start over for everyone coming back in.
			if (UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this))
			{
				Sessions->ResetRelayClients();
			}
		}
		// A room the relay matched (ランダムマッチ / ランクマッチ): it starts by itself once everyone matched is in.
		if (const TCHAR* Kind = GetWorld() ? GetWorld()->URL.GetOption(TEXT("CIMatchmade="), nullptr) : nullptr)
		{
			RoomState->Matchmade = FCString::Stricmp(Kind, TEXT("rank")) == 0 ? EChaosImpactMatchmade::Rank
				: FCString::Stricmp(Kind, TEXT("duel")) == 0 ? EChaosImpactMatchmade::Duel : EChaosImpactMatchmade::Random;
			RoomState->MatchmadeExpected = FMath::Clamp(FCString::Atoi(GetWorld()->URL.GetOption(TEXT("CIExpect="), TEXT("2"))), 2,
				AChaosImpactGameState::MaxMembers);
			RoomState->MatchmadeDeadline = RoomState->GetServerWorldTimeSeconds() + MatchmadeWaitSeconds;
			if (bRoomMatchLevel)
			{
				// Its players were gathered in its lobby already.
				bMatchmadeMatchBegun = true;
				RoomState->MatchmadeDeadline = 0.0;
			}
			else
			{
				GetWorldTimerManager().SetTimer(MatchmadeTimer, this, &AChaosImpactGameMode::UpdateMatchmadeRoom, 0.25f, true);
				UE_LOG(LogChaosImpact, Log, TEXT("Matched room (%s): waiting for %d players"), Kind, RoomState->MatchmadeExpected);
			}
		}
		if (bRoomMatchLevel)
		{
			BeginRoomMatchLevel(RoomState);
		}
		else if (bBackFromMatch && !RoomState->IsMatchmade())
		{
			// A 合言葉 room back from its match recruits again (the host closes it and decides the rules anew).
			if (UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this))
			{
				Sessions->SetRecruitmentOpen(true);
			}
		}
		GetWorldTimerManager().SetTimer(LobbyReadyTimer, this, &AChaosImpactGameMode::UpdateLobbyReady, 0.25f, true);
		// A public tournament this machine made: its rules as set up, and its start at its time.
		if (const UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this);
			Sessions && Sessions->IsHostingTourney() && !bRoomMatchLevel && !RoomState->IsMatchmade())
		{
			const FChaosImpactTourneyNotice& Notice = Sessions->GetHostedTourney();
			FChaosImpactMatchRules Rules;
			Rules.bTournament = true;
			Rules.Tournament.Mode = Notice.Mode == 1 ? EChaosImpactTournamentMode::Simultaneous : EChaosImpactTournamentMode::OneAtATime;
			for (int32 Index = 0; Index < Notice.CPUs; ++Index)
			{
				FChaosImpactTournamentCPU CPU;
				CPU.Name = FString::Printf(TEXT("CPU%d"), Index + 1);
				CPU.Level = Notice.Level;
				Rules.Tournament.CPUs.Add(CPU);
			}
			RoomState->Rules = ChaosImpactMatch::Sanitize(Rules, 1);
			RoomState->RoomName = Notice.Title;
			PublicTourneyMaxPlayers = FMath::Clamp(Notice.MaxPlayers, 2, AChaosImpactGameState::MaxMembers);
			RoomState->PublicTournamentMaxPlayers = PublicTourneyMaxPlayers;
			const double Delay = static_cast<double>(Notice.StartsUnix - FDateTime::UtcNow().ToUnixTimestamp());
			ScheduleTournament(RoomState->GetServerWorldTimeSeconds() + FMath::Max(0.0, Delay));
			UE_LOG(LogChaosImpact, Log, TEXT("Public tournament \"%s\": starts in %.0f s, up to %d people, %d CPUs"), *Notice.Title,
				Delay, PublicTourneyMaxPlayers, Notice.CPUs);
		}
		FParse::Value(FCommandLine::Get(), TEXT("CIReadyWaitSeconds="), ReadyWaitSecondsOverride);
		FString AutoLobby;
		if (!bRoomMatchLevel && !bBackFromMatch && FParse::Value(FCommandLine::Get(), TEXT("CIAutoLobby="), AutoLobby))
		{
			TArray<FString> Parts;
			AutoLobby.ParseIntoArray(Parts, TEXT(":"));
			DevAutoLobbyRules.TeamCount = Parts.IsValidIndex(0) ? FCString::Atoi(*Parts[0]) : 0;
			DevAutoLobbyRules.CPUCount = Parts.IsValidIndex(1) ? FCString::Atoi(*Parts[1]) : 0;
			const float Delay = Parts.IsValidIndex(2) ? FMath::Max(1.0f, FCString::Atof(*Parts[2])) : 20.0f;
			GetWorldTimerManager().SetTimer(DevAutoLobbyTimer, this, &AChaosImpactGameMode::RunDevAutoLobby, Delay, false);
		}
		// Development: -CIAutoTournament=<cpus>:<0 one at a time | 1 all at once>:<delay>:<machines> decides a tournament.
		FString AutoTournament;
		if (!bRoomMatchLevel && !bBackFromMatch && FParse::Value(FCommandLine::Get(), TEXT("CIAutoTournament="), AutoTournament))
		{
			TArray<FString> Parts;
			AutoTournament.ParseIntoArray(Parts, TEXT(":"));
			DevAutoLobbyRules = FChaosImpactMatchRules();
			DevAutoLobbyRules.bTournament = true;
			const int32 CPUs = Parts.IsValidIndex(0) ? FCString::Atoi(*Parts[0]) : 3;
			for (int32 Index = 0; Index < CPUs; ++Index)
			{
				FChaosImpactTournamentCPU CPU;
				CPU.Name = FString::Printf(TEXT("CPU%d"), Index + 1);
				DevAutoLobbyRules.Tournament.CPUs.Add(CPU);
			}
			DevAutoLobbyRules.Tournament.Mode = Parts.IsValidIndex(1) && FCString::Atoi(*Parts[1]) == 1
				? EChaosImpactTournamentMode::Simultaneous : EChaosImpactTournamentMode::OneAtATime;
			DevAutoLobbyRules.Tournament.bSkipCPUMatches = Parts.IsValidIndex(4) && FCString::Atoi(*Parts[4]) == 1;
			const float Delay = Parts.IsValidIndex(2) ? FMath::Max(1.0f, FCString::Atof(*Parts[2])) : 10.0f;
			DevAutoLobbyMachines = Parts.IsValidIndex(3) ? FMath::Max(1, FCString::Atoi(*Parts[3])) : 1;
			GetWorldTimerManager().SetTimer(DevAutoLobbyTimer, this, &AChaosImpactGameMode::RunDevAutoLobby, Delay, false);
		}
		if (FParse::Value(FCommandLine::Get(), TEXT("CIReserveSlots="), DevReservedSlots))
		{
			DevReservedSlots = FMath::Clamp(DevReservedSlots, 0, AChaosImpactGameState::MaxMembers - 1);
		}
		if (FParse::Value(FCommandLine::Get(), TEXT("CIReserveSpectators="), DevReservedSpectators))
		{
			DevReservedSpectators = FMath::Clamp(DevReservedSpectators, 0, AChaosImpactGameState::MaxSpectators);
		}
		UpdateRoomMemberCount();
		GetWorldTimerManager().SetTimer(PingMirrorTimer, this,
			&AChaosImpactGameMode::MirrorSplitscreenPings, 0.25f, true);

		float AutoStartSeconds = 0.0f;
		if (!bRoomMatchLevel && !bBackFromMatch && FParse::Value(FCommandLine::Get(), TEXT("CIAutoStartMatch="), AutoStartSeconds)
			&& AutoStartSeconds > 0.0f)
		{
			GetWorldTimerManager().SetTimer(AutoStartMatchTimer, this,
				&AChaosImpactGameMode::CloseRecruitment, AutoStartSeconds, false);
		}
		// Development: -CIAutoVersus=<teams>:<cpus>:<delay seconds> starts a VS match from the host by itself.
		FString AutoVersus;
		if (FParse::Value(FCommandLine::Get(), TEXT("CIAutoVersus="), AutoVersus))
		{
			TArray<FString> Parts;
			AutoVersus.ParseIntoArray(Parts, TEXT(":"));
			DevAutoVersusRules.TeamCount = Parts.IsValidIndex(0) ? FCString::Atoi(*Parts[0]) : 0;
			DevAutoVersusRules.CPUCount = Parts.IsValidIndex(1) ? FCString::Atoi(*Parts[1]) : 1;
			// A fourth part "duel": the rules of ランクマッチ (against a CPU when alone).
			DevAutoVersusRules.bDuel = Parts.IsValidIndex(3) && Parts[3] == TEXT("duel");
			DevAutoVersusRules.bBattleRoyale = Parts.IsValidIndex(3) && Parts[3] == TEXT("royale");
			// ("crash": クラッシュボール, two teams.)
			DevAutoVersusRules.bCrashBall = Parts.IsValidIndex(3) && Parts[3] == TEXT("crash");
			if (DevAutoVersusRules.bCrashBall) { DevAutoVersusRules.TeamCount = 2; }
			// (A fifth part: the stage, 1 to 6.)
			if (Parts.IsValidIndex(4)) { DevAutoVersusRules.StageIndex = FMath::Clamp(FCString::Atoi(*Parts[4]) - 1, 0, ChaosImpactMatch::StageCount - 1); }
			const float Delay = Parts.IsValidIndex(2) ? FMath::Max(1.0f, FCString::Atof(*Parts[2])) : 20.0f;
			if (!bRoomMatchLevel && !bBackFromMatch)
			{
				GetWorldTimerManager().SetTimer(DevAutoVersusTimer, this, &AChaosImpactGameMode::RunDevAutoVersus, Delay, false);
			}
		}
	}
#if !UE_BUILD_SHIPPING
	// Development: -CIDevOffBalls=<bits> plays the automatic matches / lobbies with those special balls switched off (ボールスイッチ).
	if (int32 DevOffBalls = 0; FParse::Value(FCommandLine::Get(), TEXT("CIDevOffBalls="), DevOffBalls))
	{
		DevAutoVersusRules.OffBalls = DevOffBalls;
		DevAutoLobbyRules.OffBalls = DevOffBalls;
	}
#endif
	float OverrideSeconds = 0.0f;
	if (FParse::Value(FCommandLine::Get(), TEXT("CIMatchSeconds="), OverrideSeconds) && OverrideSeconds > 1.0f)
	{
		MatchDurationOverride = OverrideSeconds;
	}
	bLocalMatchWorld = !IsOnlineRoom() && GetWorld() && GetWorld()->URL.HasOption(TEXT("CIMatch=1"));
	if (!IsOnlineRoom() && GetWorld() && GetWorld()->URL.HasOption(TEXT("CILocalTournament=1")))
	{
		// Once this machine's players are all in (their pawns spawned), the registration is played.
		LocalTournamentWaitSince = FPlatformTime::Seconds();
		GetWorldTimerManager().SetTimer(LocalTournamentTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			const UGameInstance* GameInstance = GetGameInstance();
			const int32 Wanted = FMath::Clamp(FCString::Atoi(GetWorld()->URL.GetOption(TEXT("CILocalPlayers="), TEXT("1"))), 1, 4);
			bool bReady = GameInstance && GameInstance->GetLocalPlayers().Num() >= Wanted;
			for (int32 Index = 0; bReady && Index < GameInstance->GetLocalPlayers().Num(); ++Index)
			{
				const APlayerController* Local = GameInstance->GetLocalPlayers()[Index]->GetPlayerController(GetWorld());
				bReady = Local && Local->GetPawn() && Local->PlayerState;
			}
			if (bReady || FPlatformTime::Seconds() - LocalTournamentWaitSince > 8.0)
			{
				GetWorldTimerManager().ClearTimer(LocalTournamentTimer);
				BeginLocalTournament();
			}
		}), 0.25f, true, 0.5f);
	}
	if (GetWorld() && GetWorld()->URL.HasOption(TEXT("CITraining=1")))
	{
		GetWorldTimerManager().SetTimerForNextTick(
			this, &AChaosImpactGameMode::SetupLocalTrainingPlayers);
	}
}

void AChaosImpactGameMode::SetupLocalTrainingPlayers()
{
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance || !GetWorld())
	{
		return;
	}

	const int32 DesiredPlayers = LiveDesiredPlayerCount != INDEX_NONE
		? FMath::Clamp(LiveDesiredPlayerCount, 1, 4)
		: FMath::Clamp(FCString::Atoi(
			GetWorld()->URL.GetOption(TEXT("CILocalPlayers="), TEXT("1"))), 1, 4);
	const TCHAR* KeyboardOption = GetWorld()->URL.GetOption(TEXT("CIKeyboardPlayer="), nullptr);
	const int32 KeyboardPlayerIndex = KeyboardOption
		? FCString::Atoi(KeyboardOption)
		: (GetWorld()->URL.HasOption(TEXT("CIP1Gamepad=1")) ? INDEX_NONE : 0);
	const int32 ExpectedControllerCount = DesiredPlayers
		- (KeyboardPlayerIndex != INDEX_NONE ? 1 : 0);
	const TArray<FJSL4UControllerInfo> ConnectedControllers =
		UJoyShockLibrary::JSL4UGetAllConnectedControllers();
	if (LocalPlayerSetupStartedAt <= 0.0)
	{
		LocalPlayerSetupStartedAt = FPlatformTime::Seconds();
	}
	// HID-backed pads can finish registration a few frames after a map begins.
	// Creating placeholder device IDs before that happens makes the real device
	// take another player slot and is especially visible in PIE as phantom input.
	if (ConnectedControllers.Num() < ExpectedControllerCount
		&& FPlatformTime::Seconds() - LocalPlayerSetupStartedAt < 5.0)
	{
		GetWorldTimerManager().SetTimerForNextTick(
			this, &AChaosImpactGameMode::SetupLocalTrainingPlayers);
		return;
	}
	if (UGameMapsSettings* MapsSettings = GetMutableDefault<UGameMapsSettings>())
	{
		MapsSettings->bOffsetPlayerGamepadIds = false;
	}
	while (GameInstance->GetLocalPlayers().Num() > DesiredPlayers)
	{
		GameInstance->RemoveLocalPlayer(GameInstance->GetLocalPlayers().Last());
	}
	while (GameInstance->GetLocalPlayers().Num() < DesiredPlayers)
	{
		if (!UGameplayStatics::CreatePlayer(this, -1, true))
		{
			break;
		}
	}

	const FVector FourPlayerOffsets[] =
	{
		FVector(0.0f, -135.0f, 0.0f),
		FVector(0.0f, 135.0f, 0.0f),
		FVector(-220.0f, -135.0f, 0.0f),
		FVector(-220.0f, 135.0f, 0.0f)
	};
	FVector Anchor = FVector::ZeroVector;
	FRotator Facing = FRotator::ZeroRotator;
	if (const ULocalPlayer* FirstLocalPlayer = GameInstance->GetLocalPlayers()[0])
	{
		if (const APlayerController* FirstController =
			FirstLocalPlayer->GetPlayerController(GetWorld()))
		{
			if (const APawn* FirstPawn = FirstController->GetPawn())
			{
				Anchor = FirstPawn->GetActorLocation();
				Facing = FirstPawn->GetActorRotation();
			}
		}
	}

	TrainingKeyboardPlayerIndex = KeyboardPlayerIndex;
	ApplyTrainingControllerAssignments(DesiredPlayers, KeyboardPlayerIndex);
	GetWorldTimerManager().SetTimer(ControllerReassignTimer, this,
		&AChaosImpactGameMode::ReapplyTrainingControllerAssignments, 0.5f, false);
	for (int32 PlayerIndex = 0;
		PlayerIndex < GameInstance->GetLocalPlayers().Num() && PlayerIndex < 4; ++PlayerIndex)
	{
		ULocalPlayer* LocalPlayer = GameInstance->GetLocalPlayers()[PlayerIndex];
		APlayerController* Controller = LocalPlayer
			? LocalPlayer->GetPlayerController(GetWorld()) : nullptr;
		if (Controller && !Controller->GetPawn())
		{
			RestartPlayer(Controller);
		}
		if (AChaosImpactCharacter* Character = Controller
			? Cast<AChaosImpactCharacter>(Controller->GetPawn()) : nullptr)
		{
			const FVector Offset = DesiredPlayers == 1
				? FVector::ZeroVector : FourPlayerOffsets[PlayerIndex];
			Character->SetTrainingStartTransform(Anchor + Offset, Facing);
		}
		if (LocalPlayer)
		{
			if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem =
				ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer))
			{
				InputSubsystem->RequestRebuildControlMappings(
					FModifyContextOptions(), EInputMappingRebuildType::RebuildWithFlush);
			}
		}
	}

	if (bLocalMatchWorld)
	{
		// The VS match spawns its own CPUs on the stage; give controller pairing a moment to settle first.
		GetWorldTimerManager().SetTimer(LocalMatchTimer, this, &AChaosImpactGameMode::StartLocalMatchFromURL, 0.6f, false);
		return;
	}
	const int32 CPUCount = LiveDesiredCPUCount != INDEX_NONE
		? FMath::Clamp(LiveDesiredCPUCount, 0, 4)
		: FMath::Clamp(FCString::Atoi(GetWorld()->URL.GetOption(TEXT("CICPUCount="),
			GetWorld()->URL.HasOption(TEXT("CICPU=1")) ? TEXT("1") : TEXT("0"))), 0, 4);
	SyncTrainingCPUCount(CPUCount, Anchor, Facing);
}

void AChaosImpactGameMode::ApplyTrainingControllerAssignments(
	const int32 DesiredPlayers, const int32 KeyboardPlayerIndex)
{
	ApplyLocalControllerAssignments(GetWorld(), DesiredPlayers, KeyboardPlayerIndex, bUseLocalSeatDevices ? &LocalSeatPadDevices : nullptr);
}

void AChaosImpactGameMode::ApplyLocalControllerAssignments(UWorld* World,
	const int32 DesiredPlayers, const int32 KeyboardPlayerIndex, const TArray<int32>* PadDevices)
{
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	if (!GameInstance)
	{
		return;
	}

	const TArray<FJSL4UControllerInfo> ConnectedControllers =
		UJoyShockLibrary::JSL4UGetAllConnectedControllers();
	IPlatformInputDeviceMapper& DeviceMapper = IPlatformInputDeviceMapper::Get();
	const int32 ActivePlayerCount = FMath::Min(
		FMath::Clamp(DesiredPlayers, 1, 4), GameInstance->GetLocalPlayers().Num());
	const int32 ActiveKeyboardPlayerIndex =
		KeyboardPlayerIndex >= 0 && KeyboardPlayerIndex < ActivePlayerCount
			? KeyboardPlayerIndex : INDEX_NONE;

	// Keep the platform users that Unreal created with each LocalPlayer. Moving P1
	// to a freshly allocated user after its Enhanced Input subsystem already
	// exists can leave that subsystem listening to the old user and makes a
	// controller-only P1 completely unresponsive.
	if (ActiveKeyboardPlayerIndex > 0
		&& GameInstance->GetLocalPlayers().IsValidIndex(ActiveKeyboardPlayerIndex))
	{
		ULocalPlayer* PrimaryLocalPlayer = GameInstance->GetLocalPlayers()[0];
		ULocalPlayer* KeyboardLocalPlayer =
			GameInstance->GetLocalPlayers()[ActiveKeyboardPlayerIndex];
		if (PrimaryLocalPlayer && KeyboardLocalPlayer)
		{
			const FPlatformUserId KeyboardPlayerPreviousUser =
				KeyboardLocalPlayer->GetPlatformUserId();
			PrimaryLocalPlayer->SetPlatformUserId(KeyboardPlayerPreviousUser);
			KeyboardLocalPlayer->SetPlatformUserId(DeviceMapper.GetPrimaryPlatformUser());
		}
	}

	TSet<int32> UsedInputDeviceIds;
	int32 PadIndex = 0;
	for (int32 PlayerIndex = 0; PlayerIndex < ActivePlayerCount; ++PlayerIndex)
	{
		ULocalPlayer* LocalPlayer = GameInstance->GetLocalPlayers()[PlayerIndex];
		if (!LocalPlayer)
		{
			continue;
		}

		const bool bKeyboardPlayer = PlayerIndex == ActiveKeyboardPlayerIndex;
		LocalPlayer->SetControllerId(
			DeviceMapper.GetUserIndexForPlatformUser(LocalPlayer->GetPlatformUserId()));
		if (bKeyboardPlayer)
		{
			continue;
		}

		int32 AssignedInputDeviceId = INDEX_NONE;
		const FString DeviceOption = FString::Printf(TEXT("CIPadDevice%d="), PadIndex);
		const TCHAR* DeviceValue = PadDevices ? nullptr : World->URL.GetOption(*DeviceOption, nullptr);
		if (DeviceValue || (PadDevices && PadDevices->IsValidIndex(PadIndex)))
		{
			const int32 RequestedInputDeviceId = DeviceValue ? FCString::Atoi(DeviceValue) : (*PadDevices)[PadIndex];
			const bool bRequestedDeviceIsConnected = ConnectedControllers.ContainsByPredicate(
				[RequestedInputDeviceId](const FJSL4UControllerInfo& Info)
				{
					return Info.InputDeviceId == RequestedInputDeviceId;
				});
			if (bRequestedDeviceIsConnected && !UsedInputDeviceIds.Contains(RequestedInputDeviceId))
			{
				AssignedInputDeviceId = RequestedInputDeviceId;
			}
		}

		// A live player-count increase has no CIPadDevice option for the new slot.
		// Select the first controller that is actually connected and not already
		// assigned, rather than treating PadIndex as an input-device id. On Windows
		// the first two real ids are commonly 1 and 2, so that old fallback selected
		// id 1 twice and left P3/P4 with no controller.
		if (AssignedInputDeviceId == INDEX_NONE)
		{
			if (const FJSL4UControllerInfo* AvailableController =
				ConnectedControllers.FindByPredicate(
					[&UsedInputDeviceIds](const FJSL4UControllerInfo& Info)
					{
						return Info.InputDeviceId >= 0
							&& !UsedInputDeviceIds.Contains(Info.InputDeviceId);
					}))
			{
				AssignedInputDeviceId = AvailableController->InputDeviceId;
			}
		}

		++PadIndex;
		if (AssignedInputDeviceId == INDEX_NONE)
		{
			continue;
		}
		UsedInputDeviceIds.Add(AssignedInputDeviceId);

		APlayerController* Controller = LocalPlayer->GetPlayerController(World);
		const FJSL4UControllerInfo* ControllerInfo = ConnectedControllers.FindByPredicate(
			[AssignedInputDeviceId](const FJSL4UControllerInfo& Info)
			{
				return Info.InputDeviceId == AssignedInputDeviceId;
			});
		if (!Controller || !ControllerInfo
			|| !UJoyShockLibrary::JSL4UAssignControllerToPlayer(*ControllerInfo, Controller))
		{
			// Retain a generic engine fallback for any future input plugin that is
			// not represented in JSL4U's all-controller roster.
			DeviceMapper.Internal_MapInputDeviceToUser(
				FInputDeviceId::CreateFromInternalId(AssignedInputDeviceId),
				LocalPlayer->GetPlatformUserId(), EInputDeviceConnectionState::Connected);
		}
	}
}

void AChaosImpactGameMode::SetTrainingLocalPlayerCountLive(const int32 DesiredPlayers)
{
	LiveDesiredPlayerCount = FMath::Clamp(DesiredPlayers, 1, 4);
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance || !GetWorld())
	{
		return;
	}

	while (GameInstance->GetLocalPlayers().Num() > LiveDesiredPlayerCount)
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
	FVector Anchor = FVector::ZeroVector;
	FRotator Facing = FRotator::ZeroRotator;
	if (!GameInstance->GetLocalPlayers().IsEmpty())
	{
		if (APlayerController* FirstController =
			GameInstance->GetLocalPlayers()[0]->GetPlayerController(GetWorld()))
		{
			if (APawn* FirstPawn = FirstController->GetPawn())
			{
				Anchor = FirstPawn->GetActorLocation();
				Facing = FirstPawn->GetActorRotation();
			}
		}
	}
	const FVector JoinOffsets[] =
	{
		FVector::ZeroVector,
		FVector(0.0f, 270.0f, 0.0f),
		FVector(-220.0f, -135.0f, 0.0f),
		FVector(-220.0f, 135.0f, 0.0f)
	};
	while (GameInstance->GetLocalPlayers().Num() < LiveDesiredPlayerCount)
	{
		const int32 NewPlayerIndex = GameInstance->GetLocalPlayers().Num();
		APlayerController* NewController = UGameplayStatics::CreatePlayer(this, -1, true);
		if (!NewController)
		{
			break;
		}
		if (!NewController->GetPawn())
		{
			RestartPlayer(NewController);
		}
		if (AChaosImpactCharacter* NewCharacter =
			Cast<AChaosImpactCharacter>(NewController->GetPawn()))
		{
			NewCharacter->SetTrainingStartTransform(
				Anchor + JoinOffsets[FMath::Clamp(NewPlayerIndex, 0, 3)], Facing);
		}
		if (ULocalPlayer* LocalPlayer = NewController->GetLocalPlayer())
		{
			if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem =
				ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer))
			{
				InputSubsystem->RequestRebuildControlMappings(
					FModifyContextOptions(), EInputMappingRebuildType::RebuildWithFlush);
			}
		}
	}

	const TCHAR* KeyboardOption = GetWorld()->URL.GetOption(TEXT("CIKeyboardPlayer="), nullptr);
	const int32 KeyboardPlayerIndex = bUseLocalSeatDevices ? LocalSeatKeyboard : KeyboardOption
		? FCString::Atoi(KeyboardOption)
		: (GetWorld()->URL.HasOption(TEXT("CIP1Gamepad=1")) ? INDEX_NONE : 0);
	TrainingKeyboardPlayerIndex = KeyboardPlayerIndex;
	ApplyTrainingControllerAssignments(LiveDesiredPlayerCount, KeyboardPlayerIndex);
	GetWorldTimerManager().SetTimer(ControllerReassignTimer, this,
		&AChaosImpactGameMode::ReapplyTrainingControllerAssignments, 0.5f, false);
}

void AChaosImpactGameMode::ReapplyTrainingControllerAssignments()
{
	if (const UGameInstance* GameInstance = GetGameInstance())
	{
		ApplyTrainingControllerAssignments(
			GameInstance->GetLocalPlayers().Num(), TrainingKeyboardPlayerIndex);
	}
	LogTrainingControllerRouting();
}

void AChaosImpactGameMode::LogTrainingControllerRouting() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		return;
	}
	const IPlatformInputDeviceMapper& DeviceMapper = IPlatformInputDeviceMapper::Get();
	for (int32 PlayerIndex = 0; PlayerIndex < GameInstance->GetLocalPlayers().Num(); ++PlayerIndex)
	{
		const ULocalPlayer* LocalPlayer = GameInstance->GetLocalPlayers()[PlayerIndex];
		if (!LocalPlayer)
		{
			continue;
		}
		TArray<FInputDeviceId> Devices;
		DeviceMapper.GetAllInputDevicesForUser(LocalPlayer->GetPlatformUserId(), Devices);
		TArray<FString> DeviceIds;
		for (const FInputDeviceId& Device : Devices)
		{
			DeviceIds.Add(FString::FromInt(Device.GetId()));
		}
		UE_LOG(LogChaosImpact, Log,
			TEXT("Controller routing: P%d platform user %d, controller id %d, input devices [%s]%s"),
			PlayerIndex + 1, LocalPlayer->GetPlatformUserId().GetInternalId(),
			LocalPlayer->GetControllerId(), *FString::Join(DeviceIds, TEXT(", ")),
			PlayerIndex == TrainingKeyboardPlayerIndex ? TEXT(" + keyboard/mouse") : TEXT(""));
	}
}

void AChaosImpactGameMode::PreLogin(const FString& Options, const FString& Address,
	const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
	const AChaosImpactGameState* RoomState = GetGameState<AChaosImpactGameState>();
	if (!ErrorMessage.IsEmpty() || !IsOnlineRoom() || !RoomState)
	{
		return;
	}
	if (UGameplayStatics::GetIntOption(Options, TEXT("CISpectator"), 0) != 0)
	{
		// Spectators may come in at any time, mid-match too, up to their own limit.
		const int32 Watching = RoomState->CountSpectators() + DevReservedSpectators;
		if (Watching >= AChaosImpactGameState::MaxSpectators)
		{
			ErrorMessage = TEXT("CISPECFULL");
		}
		UE_LOG(LogChaosImpact, Log, TEXT("Room spectator login check: watching=%d result=%s"), Watching,
			ErrorMessage.IsEmpty() ? TEXT("ok") : *ErrorMessage);
		return;
	}
	// The engine adds SplitscreenCount=2 when a machine already inside sends its second player;
	// that place was reserved when the machine joined, so only the hard limit applies.
	const bool bSecondPlayerOfMachine = UGameplayStatics::GetIntOption(Options, TEXT("SplitscreenCount"), 1) >= 2;
	if (bRoomMatchLevel)
	{
		// The match level takes back the room's own machines, as they arrive from the lobby; anyone else comes in to
		// watch (InitNewPlayer), while there is room for that.
		if (!bSecondPlayerOfMachine && !IsCarriedMachine(UGameplayStatics::ParseOption(Options, TEXT("CIMachine")))
			&& (RoomState->IsMatchmade() || RoomState->CountSpectators() + DevReservedSpectators >= AChaosImpactGameState::MaxSpectators))
		{
			ErrorMessage = RoomState->IsMatchmade() ? TEXT("CICLOSED") : TEXT("CISPECFULL");
		}
		UE_LOG(LogChaosImpact, Log, TEXT("Room match level login check: second=%d result=%s"), bSecondPlayerOfMachine,
			ErrorMessage.IsEmpty() ? TEXT("ok") : *ErrorMessage);
		return;
	}
	const int32 Arriving = bSecondPlayerOfMachine
		? 1 : FMath::Clamp(UGameplayStatics::GetIntOption(Options, TEXT("CIPlayers"), 1), 1, 2);
	// A machine coming back after a drop the host has not noticed yet takes over its own old places.
	const int32 StalePlaces = bSecondPlayerOfMachine
		? 0 : CountMembersOfMachine(UGameplayStatics::ParseOption(Options, TEXT("CIMachine")));
	const int32 Occupied = (bSecondPlayerOfMachine
		? RoomState->CountHumanMembers() + DevReservedSlots
		: RoomState->CountHumanMembers() + GetReservedRoomSlots() + DevReservedSlots) - StalePlaces;
	// The client's session subsystem turns these codes into a readable notice.
	if (Occupied + Arriving > AChaosImpactGameState::MaxMembers
		|| (RoomState->bPublicTournament && !RoomState->IsTournament() && Occupied + Arriving > PublicTourneyMaxPlayers))
	{
		ErrorMessage = TEXT("CIFULL");
	}
	else if (!bSecondPlayerOfMachine
		&& !RoomState->IsTournament() && ((RoomState->bRecruitmentClosed && StalePlaces == 0) || RoomState->Phase != EChaosImpactOnlinePhase::Lobby))
	{
		// Recruitment over, or a match on: in to watch (InitNewPlayer) while there is room for that; a matched room keeps
		// strangers out.
		if (RoomState->IsMatchmade())
		{
			ErrorMessage = TEXT("CICLOSED");
		}
		else if (RoomState->CountSpectators() + DevReservedSpectators >= AChaosImpactGameState::MaxSpectators)
		{
			ErrorMessage = TEXT("CISPECFULL");
		}
	}
	UE_LOG(LogChaosImpact, Log, TEXT("Room login check: arriving=%d occupied=%d second=%d result=%s"),
		Arriving, Occupied, bSecondPlayerOfMachine, ErrorMessage.IsEmpty() ? TEXT("ok") : *ErrorMessage);
}

FString AChaosImpactGameMode::InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId,
	const FString& Options, const FString& Portal)
{
	const FString Error = Super::InitNewPlayer(NewPlayerController, UniqueId, Options, Portal);
	AChaosImpactPlayerState* Member = NewPlayerController
		? NewPlayerController->GetPlayerState<AChaosImpactPlayerState>() : nullptr;
	if (Member && IsOnlineRoom() && UGameplayStatics::GetIntOption(Options, TEXT("CISpectator"), 0) != 0)
	{
		// Gets a spectator camera instead of a character (SpawnDefaultPawnFor).
		Member->bSpectating = true;
	}
	// Someone new arriving once recruitment is over or in the middle of a match (or a tournament): they watch.
	if (const AChaosImpactGameState* RoomNow = GetGameState<AChaosImpactGameState>(); Member && IsOnlineRoom() && RoomNow
		&& !RoomNow->IsMatchmade() && NewPlayerController && !NewPlayerController->IsLocalController()
		&& UGameplayStatics::GetIntOption(Options, TEXT("SplitscreenCount"), 1) < 2
		&& (RoomNow->bRecruitmentClosed || RoomNow->Phase != EChaosImpactOnlinePhase::Lobby))
	{
		const FString Machine = UGameplayStatics::ParseOption(Options, TEXT("CIMachine"));
		const bool bKnown = bRoomMatchLevel ? IsCarriedMachine(Machine) : CountMembersOfMachine(Machine) > 0;
		if (!bKnown && !Member->bSpectating)
		{
			Member->bSpectating = true;
			UE_LOG(LogChaosImpact, Log, TEXT("Room: a late arrival watches (recruitment %s, phase %d)"),
				RoomNow->bRecruitmentClosed ? TEXT("closed") : TEXT("open"), static_cast<int32>(RoomNow->Phase));
		}
	}
	if (Member && UGameplayStatics::GetIntOption(Options, TEXT("SplitscreenCount"), 1) < 2)
	{
		Member->ExpectedMachinePlayers = FMath::Clamp(UGameplayStatics::GetIntOption(Options, TEXT("CIPlayers"), 1), 1, 2);
		Member->MachinePlayersJoined = 1;
		Member->MachineLoginAt = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		Member->MachineToken = UGameplayStatics::ParseOption(Options, TEXT("CIMachine"));
		// What this machine chose before the level changed, from the first moment in this one.
		const UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this);
		if (const UChaosImpactSessionSubsystem::FRememberedMember* Known = Sessions ? Sessions->FindRememberedMember(Member->MachineToken) : nullptr)
		{
			if (!Known->Name.IsEmpty())
			{
				Member->SetPlayerName(Known->Name);
			}
			if (Known->Character >= 0)
			{
				Member->CharacterIndex = ChaosImpactRoster::ClampIndex(Known->Character);
				Member->ColourChoice = FMath::Clamp(Known->Colour, 0, ChaosImpactRoster::ColourCount - 1);
				Member->CustomColours = Known->CustomColours;
			}
		}
		// The name the machine sent (Name= holds the engine's own nickname for it, the PC's name).
		const FString SentName = UGameplayStatics::ParseOption(Options, TEXT("CIName")).TrimStartAndEnd()
			.Left(UChaosImpactSessionSubsystem::MaxNameLength);
		if (!SentName.IsEmpty())
		{
			Member->SetPlayerName(SentName);
		}
	}
	return Error;
}

int32 AChaosImpactGameMode::GetReservedRoomSlots() const
{
	const AChaosImpactGameState* RoomState = GetGameState<AChaosImpactGameState>();
	if (!RoomState || !GetWorld())
	{
		return 0;
	}
	// A reservation lapses if the second player never shows up.
	constexpr double ReservationSeconds = 15.0;
	int32 Reserved = 0;
	for (const APlayerState* State : RoomState->PlayerArray)
	{
		const AChaosImpactPlayerState* Member = Cast<AChaosImpactPlayerState>(State);
		if (Member && Member->ExpectedMachinePlayers > Member->MachinePlayersJoined
			&& GetWorld()->GetTimeSeconds() - Member->MachineLoginAt < ReservationSeconds)
		{
			Reserved += Member->ExpectedMachinePlayers - Member->MachinePlayersJoined;
		}
	}
	return Reserved;
}

void AChaosImpactGameMode::MirrorSplitscreenPings()
{
	if (!GetWorld())
	{
		return;
	}
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* Controller = It->Get();
		const UChildConnection* ChildConnection = Controller ? Cast<UChildConnection>(Controller->Player) : nullptr;
		const APlayerController* ParentController = ChildConnection && ChildConnection->Parent
			? ChildConnection->Parent->PlayerController.Get() : nullptr;
		if (Controller->PlayerState && ParentController && ParentController->PlayerState)
		{
			Controller->PlayerState->UpdatePing(ParentController->PlayerState->GetPingInMilliseconds() / 1000.0f);
		}
	}
}

void AChaosImpactGameMode::PostLogin(APlayerController* NewPlayer)
{
	AChaosImpactPlayerState* Member = NewPlayer ? NewPlayer->GetPlayerState<AChaosImpactPlayerState>() : nullptr;
	const UChildConnection* ChildConnection = NewPlayer ? Cast<UChildConnection>(NewPlayer->Player) : nullptr;
	if (Member)
	{
		const bool bLocal = NewPlayer->IsLocalController();
		const UGameInstance* GameInstance = GetGameInstance();
		const ULocalPlayer* LocalPlayer = NewPlayer->GetLocalPlayer();
		// Only the host machine's first player is "the host"; a second local player there joins like anyone else.
		const bool bPrimaryLocal = bLocal && (!GameInstance || !LocalPlayer
			|| GameInstance->GetLocalPlayers().IndexOfByKey(LocalPlayer) <= 0);
		Member->bHostMachine = bLocal;
		Member->bRoomHost = bPrimaryLocal;
		Member->JoinOrder = bPrimaryLocal ? 0 : NextJoinOrder++;
		// Back in from the room's previous level: the same place in the list, still watching if they were (before
		// their character or camera is made).
		const UChaosImpactSessionSubsystem* Sessions = IsOnlineRoom() ? UChaosImpactSessionSubsystem::Get(this) : nullptr;
		if (Sessions && Sessions->bRoomCarryValid)
		{
			if (ChildConnection && !Member->bHostMachine)
			{
				const APlayerController* Parent = ChildConnection->Parent ? ChildConnection->Parent->PlayerController.Get() : nullptr;
				if (const AChaosImpactPlayerState* ParentState = Parent ? Parent->GetPlayerState<AChaosImpactPlayerState>() : nullptr)
				{
					Member->MachineToken = ParentState->MachineToken;
				}
			}
			const bool bSecond = ChildConnection || (bLocal && !bPrimaryLocal);
			if (const FChaosImpactRoomCarryMember* Carried = Sessions->RoomCarry.Find(GetCarryKey(Member, bSecond)))
			{
				Member->JoinOrder = Carried->JoinOrder;
				// What they were doing when the room moved, not how they first came in: someone who came in to watch
				// (the room was busy then) and went on to play stays a player (their machine still sends ?CISpectator=1).
				Member->bSpectating = Carried->bSpectating;
				Member->TeamIndex = Carried->TeamIndex;
			}
			NextJoinOrder = FMath::Max(NextJoinOrder, Sessions->RoomCarryNextJoinOrder);
		}
	}
	Super::PostLogin(NewPlayer);
	if (!IsOnlineRoom())
	{
		return;
	}
	// A machine that dropped and came back before the host noticed would otherwise be in the room twice:
	// its old connection is let go now (a second player on that machine leaves with it).
	if (Member && !ChildConnection && !NewPlayer->IsLocalController() && !Member->MachineToken.IsEmpty() && GameSession)
	{
		TArray<APlayerController*> Stale;
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* Other = It->Get();
			const AChaosImpactPlayerState* OtherState = Other ? Other->GetPlayerState<AChaosImpactPlayerState>() : nullptr;
			// Both of the old places go: the machine's first player and its second (split-screen) player.
			if (Other && Other != NewPlayer && !Other->IsLocalController()
				&& OtherState && OtherState->MachineToken == Member->MachineToken)
			{
				Stale.Add(Other);
			}
		}
		for (APlayerController* Other : Stale)
		{
			UE_LOG(LogChaosImpact, Log, TEXT("Room member reconnected: dropping the stale connection of %s"),
				Other->PlayerState ? *Other->PlayerState->GetPlayerName() : TEXT("?"));
			GameSession->KickPlayer(Other, FText::FromString(TEXT("CIREJOIN")));
		}
	}
	if (Member)
	{
		// A machine's second player is named after its first player.
		if (ChildConnection)
		{
			const APlayerController* ParentController = ChildConnection->Parent
				? ChildConnection->Parent->PlayerController.Get() : nullptr;
			if (AChaosImpactPlayerState* ParentState = ParentController
				? ParentController->GetPlayerState<AChaosImpactPlayerState>() : nullptr)
			{
				++ParentState->MachinePlayersJoined;
				Member->SetPlayerName(AChaosImpactPlayerState::MakeSecondPlayerName(ParentState->GetPlayerName()));
				Member->MachineToken = ParentState->MachineToken;
			}
		}
		else if (Member->bHostMachine && !Member->bRoomHost)
		{
			if (const UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this))
			{
				Member->SetPlayerName(AChaosImpactPlayerState::MakeSecondPlayerName(Sessions->GetPlayerName()));
			}
		}
		Member->bSecondOfMachine = ChildConnection || (Member->bHostMachine && !Member->bRoomHost);
		UE_LOG(LogChaosImpact, Log, TEXT("Room member joined: order=%d host=%d hostMachine=%d secondOfMachine=%d"),
			Member->JoinOrder, Member->bRoomHost, Member->bHostMachine,
			ChildConnection || (Member->bHostMachine && !Member->bRoomHost));
	}
	if (AChaosImpactCharacter* Character = NewPlayer ? Cast<AChaosImpactCharacter>(NewPlayer->GetPawn()) : nullptr)
	{
		Character->SetTrainingStartTransform(Character->GetActorLocation(), Character->GetActorRotation());
	}
	// Into a tournament being played: watching it.
	if (const AChaosImpactGameState* Room = GetGameState<AChaosImpactGameState>(); Room && Room->IsTournament() && !Room->bLocalTournament && Member && NewPlayer)
	{
		if (Room->Tournament.FindEntrant(Member) == INDEX_NONE)
		{
			Member->bSpectating = true;
			Member->TournamentWait = EChaosImpactTournamentWait::Spectate;
		}
		GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this, Joined = TWeakObjectPtr<APlayerController>(NewPlayer)]()
		{
			if (Joined.IsValid())
			{
				PlaceTournamentMember(Joined.Get());
			}
		}));
	}
	UpdateRoomMemberCount();
}

void AChaosImpactGameMode::Logout(AController* Exiting)
{
	// A tournament: whoever leaves loses their match being played and every one still to come.
	if (const AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>(); Match && Exiting && Match->IsTournament())
	{
		ForfeitTournamentEntrant(Match->Tournament.FindEntrant(Exiting->PlayerState));
	}
	// A duellist leaving mid-match gives it to the other.
	if (const AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>(); Match && Exiting && Match->IsDuel()
		&& (Match->Phase == EChaosImpactOnlinePhase::Intro || Match->Phase == EChaosImpactOnlinePhase::Match))
	{
		const int32 Side = Match->Duel.SideOf(Exiting->PlayerState);
		if (Side != INDEX_NONE)
		{
			ForfeitDuel(Side);
		}
	}
	Super::Logout(Exiting);
	if (IsOnlineRoom())
	{
		GetWorldTimerManager().SetTimerForNextTick(this, &AChaosImpactGameMode::UpdateRoomMemberCount);
		if (bMatchmadeHostDone)
		{
			// The host is only waiting for the guests to leave their results.
			GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]() { FinishMatchmadeIfEmpty(false); }));
		}
	}
}

APawn* AChaosImpactGameMode::SpawnSpectatorCamera(AController* Viewer)
{
	const AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!GetWorld())
	{
		return nullptr;
	}
	const bool bVersus = Match && Match->bVersusMatch;
	const FVector Target = bVersus ? FVector(Match->StageCenter) : GetRoomAnchor();
	const FVector Location = Target + (bVersus ? FVector(0.0f, -2600.0f, 2000.0f) : FVector(0.0f, -1100.0f, 900.0f));
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Parameters.Owner = Viewer;
	Parameters.ObjectFlags |= RF_Transient;
	return GetWorld()->SpawnActor<AChaosImpactSpectatorPawn>(AChaosImpactSpectatorPawn::StaticClass(), Location,
		(Target - Location).Rotation(), Parameters);
}

APawn* AChaosImpactGameMode::SpawnDefaultPawnFor_Implementation(AController* NewPlayer, AActor* StartSpot)
{
	if (const AChaosImpactPlayerState* Watcher = NewPlayer ? NewPlayer->GetPlayerState<AChaosImpactPlayerState>() : nullptr;
		Watcher && (Watcher->bSpectating || (GetGameState<AChaosImpactGameState>() && GetGameState<AChaosImpactGameState>()->IsRoyaleEliminated(Watcher))))
	{
		return SpawnSpectatorCamera(NewPlayer);
	}
	if (!IsOnlineRoom() || !StartSpot || !GetWorld())
	{
		return Super::SpawnDefaultPawnFor_Implementation(NewPlayer, StartSpot);
	}
	if (!bHasRoomAnchor)
	{
		RoomAnchor = StartSpot->GetActorLocation();
		bHasRoomAnchor = true;
	}
	// Members appear in a ring around the start point instead of stacking on it.
	const AChaosImpactPlayerState* Member = NewPlayer ? NewPlayer->GetPlayerState<AChaosImpactPlayerState>() : nullptr;
	const int32 Order = Member ? Member->JoinOrder : 0;
	const FVector Offset = Order == 0 ? FVector::ZeroVector
		: FVector(260.0f, 0.0f, 0.0f).RotateAngleAxis(Order * 51.0f, FVector::UpVector);
	const FTransform SpawnTransform(FRotator(0.0f, StartSpot->GetActorRotation().Yaw, 0.0f), RoomAnchor + Offset);
	FActorSpawnParameters Parameters;
	Parameters.Instigator = GetInstigator();
	Parameters.ObjectFlags |= RF_Transient;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	return GetWorld()->SpawnActor<APawn>(GetDefaultPawnClassForController(NewPlayer), SpawnTransform, Parameters);
}

FVector AChaosImpactGameMode::GetRoomAnchor() const
{
	if (bHasRoomAnchor)
	{
		return RoomAnchor;
	}
	const UGameInstance* GameInstance = GetGameInstance();
	const APlayerController* Host = GameInstance ? GameInstance->GetFirstLocalPlayerController(GetWorld()) : nullptr;
	return Host && Host->GetPawn() ? Host->GetPawn()->GetActorLocation() : FVector::ZeroVector;
}

void AChaosImpactGameMode::UpdateRoomMemberCount()
{
	const AChaosImpactGameState* RoomState = GetGameState<AChaosImpactGameState>();
	if (UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this); Sessions && RoomState)
	{
		// Advertise reserved places too, so a searching pair is turned away before it travels.
		Sessions->SetMemberCount(RoomState->CountHumanMembers() + GetReservedRoomSlots() + DevReservedSlots);
		Sessions->SetSpectatorCount(RoomState->CountSpectators() + DevReservedSpectators);
	}
}

void AChaosImpactGameMode::CloseRecruitment()
{
	AChaosImpactGameState* RoomState = GetGameState<AChaosImpactGameState>();
	if (!IsOnlineRoom() || !RoomState || RoomState->bRecruitmentClosed)
	{
		return;
	}
	RoomState->bRecruitmentClosed = true;
	if (UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this))
	{
		Sessions->SetRecruitmentOpen(false);
	}
	UE_LOG(LogChaosImpact, Log, TEXT("Room recruitment closed with %d members"), RoomState->PlayerArray.Num());
}

float AChaosImpactGameMode::GetReadyWaitSeconds() const
{
	return ReadyWaitSecondsOverride > 0.0f ? ReadyWaitSecondsOverride : AChaosImpactGameState::ReadyWaitSeconds;
}

int32 AChaosImpactGameMode::CountMembersOfMachine(const FString& Token) const
{
	const AChaosImpactGameState* RoomState = GetGameState<AChaosImpactGameState>();
	if (Token.IsEmpty() || !RoomState)
	{
		return 0;
	}
	int32 Count = 0;
	for (const AChaosImpactPlayerState* Member : RoomState->GetMembersInJoinOrder())
	{
		Count += Member->MachineToken == Token ? 1 : 0;
	}
	return Count;
}

void AChaosImpactGameMode::DecideLobbyRules(const FChaosImpactMatchRules& InRules)
{
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	// (A public tournament keeps the rules it was made with.)
	if (!IsOnlineRoom() || !Match || Match->bVersusMatch || Match->Phase != EChaosImpactOnlinePhase::Lobby || Match->bPublicTournament)
	{
		return;
	}
	if (!Match->bRecruitmentClosed)
	{
		CloseRecruitment();
	}
	Match->Rules = ChaosImpactMatch::Sanitize(InRules, Match->CountHumanMembers());
	Match->bRulesDecided = true;
	Match->ReadyDeadline = Match->GetServerWorldTimeSeconds() + GetReadyWaitSeconds();
	ClearReady();
	FitLobbyTeams();
	Match->ForceNetUpdate();
	UE_LOG(LogChaosImpact, Log, TEXT("Lobby rules decided: %d min, teams=%d, CPUs=%d; starts by itself in %.0f s"),
		Match->Rules.Minutes, Match->Rules.TeamCount, Match->Rules.CPUCount, GetReadyWaitSeconds());
}

void AChaosImpactGameMode::SetMemberReady(AChaosImpactPlayerState* Member, const bool bReady)
{
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || !Member || Member->IsABot() || Member->bSpectating || !Match->bRulesDecided || Match->bVersusMatch
		|| Match->Phase != EChaosImpactOnlinePhase::Lobby || Member->bReadyForMatch == bReady)
	{
		return;
	}
	Member->bReadyForMatch = bReady;
	Member->ForceNetUpdate();
	UE_LOG(LogChaosImpact, Log, TEXT("Lobby ready: %s %s (%d/%d)"), *Member->GetPlayerName(),
		bReady ? TEXT("ready") : TEXT("cancelled"), Match->CountReadyMembers(), Match->CountHumanMembers());
	UpdateLobbyReady();
}

void AChaosImpactGameMode::SetMemberSpectating(APlayerController* MemberController, const bool bSpectate)
{
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	AChaosImpactPlayerState* Member = MemberController ? MemberController->GetPlayerState<AChaosImpactPlayerState>() : nullptr;
	if (!IsOnlineRoom() || !Match || !Member || Member->IsABot() || Member->bSpectating == bSpectate
		|| Match->bRecruitmentClosed || Match->bVersusMatch || Match->Phase != EChaosImpactOnlinePhase::Lobby)
	{
		return;
	}
	if (bSpectate)
	{
		// Each local member may watch independently, while there is room and someone remains to play.
		if (Match->CountSpectators() + DevReservedSpectators >= AChaosImpactGameState::MaxSpectators
			|| Match->CountHumanMembers() <= 1)
		{
			UE_LOG(LogChaosImpact, Log, TEXT("Lobby: %s cannot watch (spectators=%d players=%d)"),
				*Member->GetPlayerName(), Match->CountSpectators(), Match->CountHumanMembers());
			return;
		}
	}
	else if (Match->CountHumanMembers() >= AChaosImpactGameState::MaxMembers)
	{
		return;
	}
	Member->bSpectating = bSpectate;
	Member->bReadyForMatch = false;
	Member->TeamIndex = INDEX_NONE;
	Member->ForceNetUpdate();
	if (APawn* Old = MemberController->GetPawn())
	{
		MemberController->UnPossess();
		Old->Destroy();
	}
	// A character comes back in the room's ring (SpawnDefaultPawnFor), a camera over the room otherwise.
	RestartPlayer(MemberController);
	if (AChaosImpactCharacter* Character = Cast<AChaosImpactCharacter>(MemberController->GetPawn()))
	{
		Character->SetTrainingStartTransform(Character->GetActorLocation(), Character->GetActorRotation());
	}
	// The decided rules follow the players who are left (a lone player still gets a CPU).
	if (Match->bRulesDecided)
	{
		Match->Rules = ChaosImpactMatch::Sanitize(Match->Rules, Match->CountHumanMembers());
	}
	FitLobbyTeams();
	Match->ForceNetUpdate();
	UE_LOG(LogChaosImpact, Log, TEXT("Lobby: %s now %s (players=%d spectators=%d)"), *Member->GetPlayerName(),
		bSpectate ? TEXT("watches") : TEXT("plays"), Match->CountHumanMembers(), Match->CountSpectators());
	UpdateRoomMemberCount();
	UpdateLobbyReady();
}

void AChaosImpactGameMode::ReopenRecruitment()
{
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!IsOnlineRoom() || !Match || !Match->bRecruitmentClosed || Match->bVersusMatch
		|| Match->Phase != EChaosImpactOnlinePhase::Lobby)
	{
		return;
	}
	Match->bRecruitmentClosed = false;
	Match->bRulesDecided = false;
	Match->ReadyDeadline = 0.0;
	ClearReady();
	FitLobbyTeams();
	Match->ForceNetUpdate();
	if (UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this))
	{
		Sessions->SetRecruitmentOpen(true);
	}
	UpdateRoomMemberCount();
	UE_LOG(LogChaosImpact, Log, TEXT("Room recruitment reopened"));
}

void AChaosImpactGameMode::RenameRoom(const FString& NewName)
{
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	const FString Trimmed = NewName.TrimStartAndEnd().Left(UChaosImpactSessionSubsystem::MaxRoomNameLength);
	if (!IsOnlineRoom() || !Match || Match->bVersusMatch || Trimmed.IsEmpty())
	{
		return;
	}
	Match->RoomName = Trimmed;
	Match->ForceNetUpdate();
	if (UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this))
	{
		Sessions->SetRoomName(Trimmed);
	}
	UE_LOG(LogChaosImpact, Log, TEXT("Room renamed: %s"), *Trimmed);
}

void AChaosImpactGameMode::ClearReady()
{
	if (const AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>())
	{
		for (AChaosImpactPlayerState* Member : Match->GetMembersInJoinOrder())
		{
			if (Member->bReadyForMatch)
			{
				Member->bReadyForMatch = false;
				Member->ForceNetUpdate();
			}
		}
	}
}

void AChaosImpactGameMode::UpdateLobbyReady()
{
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!IsOnlineRoom() || bRoomMatchLevel || !Match || !Match->bRulesDecided || Match->bVersusMatch
		|| Match->Phase != EChaosImpactOnlinePhase::Lobby)
	{
		return;
	}
	// Someone is still on the last match's results: nothing starts, and the wait starts again once they are back.
	for (const APlayerState* Member : Match->PlayerArray)
	{
		if (const AChaosImpactPlayerState* Viewer = Cast<AChaosImpactPlayerState>(Member); Viewer && Viewer->bViewingResults)
		{
			Match->ReadyDeadline = Match->GetServerWorldTimeSeconds() + GetReadyWaitSeconds();
			return;
		}
	}
	const int32 Members = Match->CountHumanMembers();
	const int32 Ready = Match->CountReadyMembers();
	const bool bEveryoneReady = Members > 0 && Ready >= Members;
	const bool bTimeUp = Match->ReadyDeadline > 0.0 && Match->GetServerWorldTimeSeconds() >= Match->ReadyDeadline;
	if (bEveryoneReady || bTimeUp)
	{
		UE_LOG(LogChaosImpact, Log, TEXT("Lobby match starting: %s (%d/%d ready)"),
			bEveryoneReady ? TEXT("everyone is ready") : TEXT("the wait ran out"), Ready, Members);
		BeginStartingCountdown();
	}
}

void AChaosImpactGameMode::BeginStartingCountdown()
{
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match)
	{
		return;
	}
	Match->ReadyDeadline = 0.0;
	SetMatchPhase(EChaosImpactOnlinePhase::Starting, AChaosImpactGameState::StartingSeconds);
	GetWorldTimerManager().SetTimer(StartingTimer, this, &AChaosImpactGameMode::StartDecidedMatch,
		AChaosImpactGameState::StartingSeconds, false);
}

void AChaosImpactGameMode::StartDecidedMatch()
{
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || Match->Phase != EChaosImpactOnlinePhase::Starting)
	{
		return;
	}
	ClearReady();
	if (Match->Rules.bTournament && IsOnlineRoom())
	{
		// A tournament is played here, in the lobby's own world.
		StartTournament();
		return;
	}
	if (IsOnlineRoom() && !bRoomMatchLevel)
	{
		TravelRoomToMatch();
		return;
	}
	ConfigureVersusMatch(Match->Rules);
	if (Match->Phase == EChaosImpactOnlinePhase::Starting)
	{
		// The match could not be set up (no stage): back to the lobby rather than stuck on the countdown.
		SetMatchPhase(EChaosImpactOnlinePhase::Lobby, 0.0f);
		Match->ReadyDeadline = Match->GetServerWorldTimeSeconds() + GetReadyWaitSeconds();
	}
}

void AChaosImpactGameMode::RunDevAutoLobby()
{
	const AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || Match->bVersusMatch)
	{
		return;
	}
	if (Match->CountMachines() + Match->CountSpectators() < DevAutoLobbyMachines)
	{
		GetWorldTimerManager().SetTimer(DevAutoLobbyTimer, this, &AChaosImpactGameMode::RunDevAutoLobby, 1.0f, false);
		return;
	}
	DecideLobbyRules(DevAutoLobbyRules);
}

void AChaosImpactGameMode::SetMatchPhase(const EChaosImpactOnlinePhase NewPhase, const float Seconds)
{
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match)
	{
		return;
	}
	const double Now = Match->GetServerWorldTimeSeconds();
	Match->Phase = NewPhase;
	Match->PhaseStartedAt = Now;
	Match->PhaseEndsAt = Seconds > 0.0f ? Now + Seconds : 0.0;
	Match->ForceNetUpdate();
}

void AChaosImpactGameMode::EnsureVersusStage(const int32 StageIndex, const bool bDuelStage)
{
	if (!GetWorld())
	{
		return;
	}
	const int32 Stage = ChaosImpactMatch::SanitizeStage(StageIndex);
	UClass* Wanted = bDuelStage ? (DuelStageClass ? DuelStageClass.Get() : AChaosImpactDuelStage::StaticClass())
		: Stage == 1
		? (SplashStageClass ? SplashStageClass.Get() : AChaosImpactSplashStage::StaticClass())
		: Stage == 2 ? (TempleStageClass ? TempleStageClass.Get() : AChaosImpactTempleStage::StaticClass())
		: Stage == 3 ? (CyberStageClass ? CyberStageClass.Get() : AChaosImpactCyberStage::StaticClass())
		: Stage == 4 ? (SnowStageClass ? SnowStageClass.Get() : AChaosImpactSnowStage::StaticClass())
		: Stage == 5 ? (ForgeStageClass ? ForgeStageClass.Get() : AChaosImpactForgeStage::StaticClass())
		: (VersusStageClass ? VersusStageClass.Get() : AChaosImpactVersusStage::StaticClass());
	// The stages are told apart by their C++ family, so a Blueprint of one (or one placed in the level, as in
	// each stage's own level) still counts as that stage.
	UClass* Family = bDuelStage ? AChaosImpactDuelStage::StaticClass()
		: Stage == 1 ? AChaosImpactSplashStage::StaticClass()
		: Stage == 2 ? AChaosImpactTempleStage::StaticClass()
		: Stage == 3 ? AChaosImpactCyberStage::StaticClass()
		: Stage == 4 ? AChaosImpactSnowStage::StaticClass()
		: Stage == 5 ? AChaosImpactForgeStage::StaticClass()
		: AChaosImpactVersusStage::StaticClass();
	if (IsValid(VersusStage) && VersusStage->IsA(Family))
	{
		return;
	}
	TOptional<FVector> ReplacedAt;
	if (IsValid(VersusStage))
	{
		// A rematch on the other stage: the old one (and the warp pads it carries) goes first, and the new one
		// takes its exact place (the players may be standing on a raised deck right now).
		UE_LOG(LogChaosImpact, Log, TEXT("VS stage %s removed for stage %d"), *VersusStage->GetName(), StageIndex);
		ReplacedAt = VersusStage->GetActorLocation();
		VersusStage->Destroy();
		VersusStage = nullptr;
		DestroyStageBallSpawners();
	}
	for (TActorIterator<AChaosImpactStageBase> It(GetWorld()); It; ++It)
	{
		if (It->IsA(Family))
		{
			VersusStage = *It;
			return;
		}
	}
	// Well away from the training arena that online rooms use as their lobby, at the same floor height.
	FVector Origin = VersusStageLocation;
	if (const UGameInstance* GameInstance = GetGameInstance())
	{
		if (const APlayerController* Host = GameInstance->GetFirstLocalPlayerController(GetWorld()))
		{
			if (const ACharacter* HostCharacter = Cast<ACharacter>(Host->GetPawn()))
			{
				Origin.Z = HostCharacter->GetActorLocation().Z
					- HostCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			}
		}
	}
	if (ReplacedAt.IsSet())
	{
		Origin = ReplacedAt.GetValue();
	}
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	UE_LOG(LogChaosImpact, Log, TEXT("VS stage spawned from %s"), *GetNameSafe(Wanted));
	VersusStage = GetWorld()->SpawnActor<AChaosImpactStageBase>(Wanted,
		Origin, FRotator::ZeroRotator, Parameters);
}

void AChaosImpactGameMode::ConfigureVersusMatch(const FChaosImpactMatchRules& InRules)
{
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || !GetWorld())
	{
		return;
	}
	GetWorldTimerManager().ClearTimer(MatchPhaseTimer);
	GetWorldTimerManager().ClearTimer(DuelTimer);
	EnsureVersusStage(InRules.StageIndex, InRules.bDuel);
	GetWorldTimerManager().ClearTimer(RoyaleTimer);
	Match->Royale = FChaosImpactRoyaleState();
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		const auto* Member = PC ? PC->GetPlayerState<AChaosImpactPlayerState>() : nullptr;
		if (Member && !Member->bSpectating && Cast<AChaosImpactSpectatorPawn>(PC->GetPawn()))
		{
			APawn* Camera = PC->GetPawn(); PC->UnPossess(); Camera->Destroy(); RestartPlayer(PC);
		}
	}
	if (!VersusStage)
	{
		return;
	}
	// Players who watch leave the competitors before the rules are fitted to the players who compete. Watching
	// offline is for a lone player (no split screen); online, members choose it themselves in the lobby.
	const UGameInstance* OwningGameInstance = GetGameInstance();
	const bool bLocalWatch = InRules.bSpectate && !IsOnlineRoom()
		&& OwningGameInstance && OwningGameInstance->GetLocalPlayers().Num() == 1;
	if (!IsOnlineRoom())
	{
		Match->StageCenter = VersusStage->GetCenter();
		Match->StageHalfExtent = VersusStage->GetHalfExtent();
		SetLocalPlayersSpectating(bLocalWatch);
	}
	FChaosImpactMatchRules Wanted = InRules;
	Wanted.bSpectate = bLocalWatch;
	const FChaosImpactMatchRules Rules = ChaosImpactMatch::Sanitize(Wanted, Match->CountHumanMembers());
	if (IsOnlineRoom())
	{
		Match->bRecruitmentClosed = true;
		if (UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this))
		{
			Sessions->SetRecruitmentOpen(false);
		}
	}
	Match->bVersusMatch = true;
	Match->Rules = Rules;
	SetupCrashMatch();
	Match->StageCenter = VersusStage->GetCenter();
	Match->StageHalfExtent = VersusStage->GetHalfExtent();
	for (TActorIterator<AChaosImpactTrainingTarget> It(GetWorld()); It; ++It)
	{
		It->SetTrainingEnabled(false);
	}
	// New CPUs appear on the stage's own spawn points: the middle of a stage may be taken (the splash stage's
	// tower), and the intro moves everyone to their start point anyway.
	CPUSpawnPoints = VersusStage->GetSpawnPoints();
	SyncTrainingCPUCount(Rules.CPUCount, VersusStage->GetCenter() + FVector(0.0f, 0.0f, 110.0f), FRotator::ZeroRotator);
	CPUSpawnPoints.Reset();
	// Every CPU in the match, new or kept from before, plays at the chosen strength.
	for (TActorIterator<AChaosImpactCPUController> It(GetWorld()); It; ++It)
	{
		It->SetDifficulty(Rules.CPULevel);
	}

	// Humans start spread across the teams in join order; CPUs are placed once the teams are confirmed.
	const TArray<AChaosImpactPlayerState*> Humans = Match->GetCompetitors(false);
	// Online the teams were picked in the lobby (carried here): kept while they fit, the rest to the smallest team.
	const bool bLobbyTeams = IsOnlineRoom() && Rules.IsTeamBattle();
	TArray<int32> TeamCounts;
	TeamCounts.Init(0, FMath::Max(Rules.TeamCount, 1));
	const int32 TeamCapacity = ChaosImpactMatch::GetTeamCapacity(Rules.TeamCount, Humans.Num() + Rules.CPUCount);
	if (bLobbyTeams)
	{
		for (AChaosImpactPlayerState* Human : Humans)
		{
			if (Human->TeamIndex >= 0 && Human->TeamIndex < Rules.TeamCount && TeamCounts[Human->TeamIndex] < TeamCapacity)
			{
				++TeamCounts[Human->TeamIndex];
			}
			else
			{
				Human->TeamIndex = INDEX_NONE;
			}
		}
	}
	for (int32 Index = 0; Index < Humans.Num(); ++Index)
	{
		if (bLobbyTeams)
		{
			if (Humans[Index]->TeamIndex == INDEX_NONE)
			{
				int32 Smallest = 0;
				for (int32 Team = 1; Team < Rules.TeamCount; ++Team)
				{
					Smallest = TeamCounts[Team] < TeamCounts[Smallest] ? Team : Smallest;
				}
				Humans[Index]->TeamIndex = Smallest;
				++TeamCounts[Smallest];
			}
		}
		else
		{
			Humans[Index]->TeamIndex = Rules.IsTeamBattle() ? Index % Rules.TeamCount : INDEX_NONE;
		}
		// Local players have no room name; use the one shown above them (P1, P2 or the title name).
		if (const AChaosImpactCharacter* Character = Cast<AChaosImpactCharacter>(Humans[Index]->GetPawn());
			Character && !IsOnlineRoom())
		{
			Humans[Index]->SetPlayerName(Character->GetOverheadDisplayName());
		}
	}
	for (AChaosImpactPlayerState* Member : Match->GetCompetitors(true))
	{
		Member->ResetMatchStats();
		if (Member->IsABot())
		{
			Member->TeamIndex = INDEX_NONE;
		}
	}
	// The two sides of the duel: the host machine's player (or the first) on the red half, the other on the blue.
	Match->Duel = FChaosImpactDuelState();
	if (Rules.bDuel)
	{
		const TArray<AChaosImpactPlayerState*> Duellists = Match->GetCompetitors(true);
		for (int32 Side = 0; Side < 2 && Side < Duellists.Num(); ++Side)
		{
			Match->Duel.Sides[Side] = Duellists[Side];
		}
		// The special balls of this match, drawn now and shown by the ball slot before the first round. Every kind
		// on (ボールスイッチ) but the nova and the prison (decisive one against one) may come up; three different ones,
		// the hook and the anchor not together (see ChaosImpactBallSwitch::DrawDuelBalls).
		Match->Duel.BallPicks = ChaosImpactBallSwitch::DrawDuelBalls(Rules, ChaosImpactMatch::DuelBallPicks,
			[](const int32 Min, const int32 Max) { return FMath::RandRange(Min, Max); });
		UE_LOG(LogChaosImpact, Log, TEXT("Duel balls drawn: %s, %s, %s"),
			Match->Duel.BallPicks.IsValidIndex(0) ? ChaosImpactBallTypes::GetInternalName(static_cast<EChaosImpactBallType>(Match->Duel.BallPicks[0])) : TEXT("-"),
			Match->Duel.BallPicks.IsValidIndex(1) ? ChaosImpactBallTypes::GetInternalName(static_cast<EChaosImpactBallType>(Match->Duel.BallPicks[1])) : TEXT("-"),
			Match->Duel.BallPicks.IsValidIndex(2) ? ChaosImpactBallTypes::GetInternalName(static_cast<EChaosImpactBallType>(Match->Duel.BallPicks[2])) : TEXT("-"));
	}
	UE_LOG(LogChaosImpact, Log, TEXT("VS match configured: %d min, teams=%d, humans=%d, CPUs=%d"),
		Rules.Minutes, Rules.TeamCount, Humans.Num(), Rules.CPUCount);
	// (Online the teams were picked in the lobby: straight to the opening.)
	if (Rules.IsTeamBattle() && !IsOnlineRoom())
	{
		SetMatchPhase(EChaosImpactOnlinePhase::TeamSelect, 0.0f);
	}
	else
	{
		StartMatchIntro();
	}
}

void AChaosImpactGameMode::SetLocalPlayersSpectating(const bool bSpectate)
{
	const UGameInstance* GameInstance = GetGameInstance();
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!GameInstance || !Match || !GetWorld())
	{
		return;
	}
	for (ULocalPlayer* LocalPlayer : GameInstance->GetLocalPlayers())
	{
		APlayerController* Controller = LocalPlayer ? LocalPlayer->GetPlayerController(GetWorld()) : nullptr;
		AChaosImpactPlayerState* Member = Controller ? Controller->GetPlayerState<AChaosImpactPlayerState>() : nullptr;
		if (!Member || Member->bSpectating == bSpectate)
		{
			continue;
		}
		Member->bSpectating = bSpectate;
		Member->ResetMatchStats();
		Member->TeamIndex = INDEX_NONE;
		if (APawn* Old = Controller->GetPawn())
		{
			Controller->UnPossess();
			Old->Destroy();
		}
		if (bSpectate)
		{
			if (AChaosImpactSpectatorPawn* Camera = Cast<AChaosImpactSpectatorPawn>(SpawnSpectatorCamera(Controller)))
			{
				Controller->Possess(Camera);
				Camera->PlaceOverStage(Match->StageCenter);
			}
		}
		else
		{
			// Back into the match with a character (placed at a start point when the match begins).
			RestartPlayer(Controller);
		}
		UE_LOG(LogChaosImpact, Log, TEXT("Local player %d %s"), GameInstance->GetLocalPlayers().IndexOfByKey(LocalPlayer),
			bSpectate ? TEXT("now watches the match") : TEXT("plays again"));
	}
}

void AChaosImpactGameMode::ChangeMemberTeam(AChaosImpactPlayerState* Member, const int32 Direction)
{
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	// (Online the teams are picked in the lobby once team rules are decided; the team select screen is for local play.)
	const bool bLobbyPick = Match && IsOnlineRoom() && !Match->bVersusMatch && Match->Phase == EChaosImpactOnlinePhase::Lobby
		&& Match->bRulesDecided && Match->Rules.IsTeamBattle();
	if (!Match || !Member || Member->IsABot() || Member->bSpectating || Direction == 0
		|| !(bLobbyPick || (Match->IsTeamBattle() && Match->Phase == EChaosImpactOnlinePhase::TeamSelect)))
	{
		return;
	}
	const int32 Teams = Match->Rules.TeamCount;
	const TArray<AChaosImpactPlayerState*> Humans = Match->GetCompetitors(false);
	const int32 Capacity = ChaosImpactMatch::GetTeamCapacity(Teams, Humans.Num() + Match->Rules.CPUCount);
	TArray<int32> Counts;
	Counts.Init(0, Teams);
	for (const AChaosImpactPlayerState* Other : Humans)
	{
		if (Other != Member && Other->TeamIndex >= 0 && Other->TeamIndex < Teams)
		{
			++Counts[Other->TeamIndex];
		}
	}
	const int32 Current = FMath::Clamp(Member->TeamIndex, 0, Teams - 1);
	for (int32 Step = 1; Step < Teams; ++Step)
	{
		const int32 Candidate = ((Current + Direction * Step) % Teams + Teams) % Teams;
		if (Counts[Candidate] < Capacity)
		{
			Member->TeamIndex = Candidate;
			Member->ForceNetUpdate();
			UE_LOG(LogChaosImpact, Log, TEXT("Team change: %s to %s"), *Member->GetPlayerName(), ChaosImpactMatch::GetTeamName(Candidate));
			return;
		}
	}
	UE_LOG(LogChaosImpact, Log, TEXT("Team change: %s stays (the other teams are full)"), *Member->GetPlayerName());
}

void AChaosImpactGameMode::SetMemberTeam(AChaosImpactPlayerState* Member, const int32 Team)
{
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	// (The same times as ChangeMemberTeam: the lobby once team rules are decided online, the team select screen locally.)
	const bool bLobbyPick = Match && IsOnlineRoom() && !Match->bVersusMatch && Match->Phase == EChaosImpactOnlinePhase::Lobby
		&& Match->bRulesDecided && Match->Rules.IsTeamBattle();
	if (!Match || !Member || Member->IsABot() || Member->bSpectating
		|| !(bLobbyPick || (Match->IsTeamBattle() && Match->Phase == EChaosImpactOnlinePhase::TeamSelect)))
	{
		return;
	}
	const int32 Teams = Match->Rules.TeamCount;
	if (Team < 0 || Team >= Teams || Member->TeamIndex == Team)
	{
		return;
	}
	const TArray<AChaosImpactPlayerState*> Humans = Match->GetCompetitors(false);
	const int32 Capacity = ChaosImpactMatch::GetTeamCapacity(Teams, Humans.Num() + Match->Rules.CPUCount);
	int32 Count = 0;
	for (const AChaosImpactPlayerState* Other : Humans)
	{
		Count += Other != Member && Other->TeamIndex == Team ? 1 : 0;
	}
	if (Count >= Capacity)
	{
		UE_LOG(LogChaosImpact, Log, TEXT("Team change: %s stays (%s is full)"), *Member->GetPlayerName(), ChaosImpactMatch::GetTeamName(Team));
		return;
	}
	Member->TeamIndex = Team;
	Member->ForceNetUpdate();
	UE_LOG(LogChaosImpact, Log, TEXT("Team change: %s to %s"), *Member->GetPlayerName(), ChaosImpactMatch::GetTeamName(Team));
}

void AChaosImpactGameMode::FitLobbyTeams()
{
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!IsOnlineRoom() || !Match || Match->bVersusMatch)
	{
		return;
	}
	const bool bTeams = Match->bRulesDecided && Match->Rules.IsTeamBattle();
	const TArray<AChaosImpactPlayerState*> Players = Match->GetMembersInJoinOrder();
	const int32 Teams = bTeams ? Match->Rules.TeamCount : 0;
	const int32 Capacity = bTeams ? ChaosImpactMatch::GetTeamCapacity(Teams, Players.Num() + Match->Rules.CPUCount) : 0;
	const auto Set = [](AChaosImpactPlayerState* Member, const int32 Team)
	{
		if (Member->TeamIndex != Team)
		{
			Member->TeamIndex = Team;
			Member->ForceNetUpdate();
		}
	};
	// Watchers (and everyone, without team rules) have no team.
	for (APlayerState* State : Match->PlayerArray)
	{
		AChaosImpactPlayerState* Member = Cast<AChaosImpactPlayerState>(State);
		if (Member && !Member->IsABot() && (!bTeams || Member->bSpectating))
		{
			Set(Member, INDEX_NONE);
		}
	}
	if (!bTeams)
	{
		return;
	}
	TArray<int32> Counts;
	Counts.Init(0, Teams);
	TArray<AChaosImpactPlayerState*> Unplaced;
	for (AChaosImpactPlayerState* Member : Players)
	{
		if (Member->TeamIndex >= 0 && Member->TeamIndex < Teams && Counts[Member->TeamIndex] < Capacity)
		{
			++Counts[Member->TeamIndex];
		}
		else
		{
			Unplaced.Add(Member);
		}
	}
	for (AChaosImpactPlayerState* Member : Unplaced)
	{
		int32 Smallest = 0;
		for (int32 Team = 1; Team < Teams; ++Team)
		{
			Smallest = Counts[Team] < Counts[Smallest] ? Team : Smallest;
		}
		Set(Member, Smallest);
		++Counts[Smallest];
	}
}

void AChaosImpactGameMode::ConfirmTeamsAndStart()
{
	const AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (Match && Match->bVersusMatch && Match->Phase == EChaosImpactOnlinePhase::TeamSelect)
	{
		StartMatchIntro();
	}
}

void AChaosImpactGameMode::StartMatchIntro()
{
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || !IsValid(VersusStage) || !GetWorld())
	{
		return;
	}
	TArray<AChaosImpactPlayerState*> Competitors = Match->GetCompetitors(true);
	if (Match->Rules.IsTeamBattle())
	{
		const int32 Teams = Match->Rules.TeamCount;
		TArray<int32> Counts;
		Counts.Init(0, Teams);
		for (AChaosImpactPlayerState* Member : Competitors)
		{
			if (!Member->IsABot())
			{
				Member->TeamIndex = FMath::Clamp(Member->TeamIndex, 0, Teams - 1);
				++Counts[Member->TeamIndex];
			}
		}
		// CPUs even the teams out, smallest team first.
		for (AChaosImpactPlayerState* Member : Competitors)
		{
			if (Member->IsABot())
			{
				int32 Smallest = 0;
				for (int32 Team = 1; Team < Teams; ++Team)
				{
					Smallest = Counts[Team] < Counts[Smallest] ? Team : Smallest;
				}
				Member->TeamIndex = Smallest;
				++Counts[Smallest];
			}
		}
	}
	for (AChaosImpactPlayerState* Member : Competitors)
	{
		if (!Match->Rules.IsTeamBattle())
		{
			Member->TeamIndex = INDEX_NONE;
		}
		Member->ResetMatchStats();
		Member->ForceNetUpdate();
	}

	ClearMatchBalls();
	DestroyStageBallSpawners();
	TArray<FVector> SupplyPoints = VersusStage->GetBallPoints();
	if (Match->Rules.bCrashBall) { SupplyPoints.Append(VersusStage->GetCrashBallExtraBallPoints()); }
	for (const FVector& Point : SupplyPoints)
	{
		const FTransform SpawnTransform(FRotator::ZeroRotator, Point + FVector(0.0f, 0.0f, 3.0f));
		if (AChaosImpactBallSpawner* Spawner = GetWorld()->SpawnActorDeferred<AChaosImpactBallSpawner>(
			AChaosImpactBallSpawner::StaticClass(), SpawnTransform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
		{
			Spawner->SetAlwaysActive(true);
			// The duel: normal balls only until the ball slot has shown what this match is played with.
			for (int32 Type = 1; Match->Rules.bDuel && Type < ChaosImpactBallTypes::Count; ++Type)
			{
				Spawner->SetChanceWeight(static_cast<EChaosImpactBallType>(Type), 0.0f);
			}
			Spawner->FinishSpawning(SpawnTransform);
			StageBallSpawners.Add(Spawner);
		}
	}

	// Everyone appears at a different random point, facing the middle.
	TArray<FVector> SpawnPoints = VersusStage->GetSpawnPoints();
	for (int32 Index = SpawnPoints.Num() - 1; Index > 0; --Index)
	{
		SpawnPoints.Swap(Index, FMath::RandRange(0, Index));
	}
	// クラッシュボール: each team starts on its own half, in front of the goal it defends.
	TArray<FVector> TeamPoints[2];
	int32 TeamUsed[2] = {0, 0};
	if (Match->Rules.bCrashBall)
	{
		for (int32 Team = 0; Team < 2; ++Team)
		{
			TeamPoints[Team] = GetCrashTeamSpawnPoints(Team);
			for (int32 Index = TeamPoints[Team].Num() - 1; Index > 0; --Index)
			{
				TeamPoints[Team].Swap(Index, FMath::RandRange(0, Index));
			}
		}
	}
	for (int32 Index = 0; Index < Competitors.Num() && !SpawnPoints.IsEmpty(); ++Index)
	{
		AChaosImpactCharacter* Character = Cast<AChaosImpactCharacter>(Competitors[Index]->GetPawn());
		if (!Character)
		{
			continue;
		}
		FVector Floor = SpawnPoints[Index % SpawnPoints.Num()];
		// The duel: each side at its own start (the stage lists them first), facing the other.
		if (const int32 Side = Match->Duel.SideOf(Competitors[Index]); Match->Rules.bDuel && Side != INDEX_NONE)
		{
			const TArray<FVector> Starts = VersusStage->GetSpawnPoints();
			Floor = Starts.IsValidIndex(Side) ? Starts[Side] : Floor;
		}
		const int32 Team = Competitors[Index]->TeamIndex;
		if (Match->Rules.bCrashBall && (Team == 0 || Team == 1) && !TeamPoints[Team].IsEmpty())
		{
			// More members than points: the extra ones stand beside the first, towards the middle line.
			const int32 Used = TeamUsed[Team]++;
			Floor = TeamPoints[Team][Used % TeamPoints[Team].Num()];
			const FVector Across = FVector::CrossProduct(FVector::UpVector, (Match->StageCenter - Floor).GetSafeNormal2D());
			Floor += Across * 170.0f * (Used / TeamPoints[Team].Num()) * ((Used / TeamPoints[Team].Num()) % 2 ? 1.0f : -1.0f);
		}
		const FVector Location = Floor
			+ FVector(0.0f, 0.0f, Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 4.0f);
		Character->ResetForOnlineMatch(Location, FRotator(0.0f, (Match->StageCenter - Floor).Rotation().Yaw, 0.0f));
	}
	InitializeRoyale();
	// Ready? waits until every machine reports that its opening has played (ReportIntroFinished).
	Match->ReadyStartedAt = 0.0;
	IntroFinishedMembers.Reset();
	SetMatchPhase(EChaosImpactOnlinePhase::Intro, 0.0f);
	GetWorldTimerManager().SetTimer(MatchPhaseTimer, this, &AChaosImpactGameMode::StartReadyCountdown,
		(Match->bOnlineRoom ? ChaosImpactMatch::OnlineCardSeconds : 0.0f)
		+ ChaosImpactMatch::FlyoverSeconds + ChaosImpactMatch::DiveSeconds + ChaosImpactMatch::IntroWaitTimeoutSeconds, false);
	UE_LOG(LogChaosImpact, Log, TEXT("VS match opening with %d competitors"), Competitors.Num());
}

void AChaosImpactGameMode::ReportIntroFinished(APlayerState* Member, const double IntroStartedAt)
{
	const AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || !Member || !Match->bVersusMatch || Match->Phase != EChaosImpactOnlinePhase::Intro
		|| Match->ReadyStartedAt > 0.0 || !FMath::IsNearlyEqual(IntroStartedAt, Match->PhaseStartedAt, 0.01))
	{
		return;
	}
	IntroFinishedMembers.Add(FObjectKey(Member));
	for (const AChaosImpactPlayerState* Human : Match->GetCompetitors(false))
	{
		if (!IntroFinishedMembers.Contains(FObjectKey(Human)))
		{
			return;
		}
	}
	StartReadyCountdown();
}

void AChaosImpactGameMode::StartReadyCountdown()
{
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || Match->Phase != EChaosImpactOnlinePhase::Intro || Match->ReadyStartedAt > 0.0)
	{
		return;
	}
	const double Now = Match->GetServerWorldTimeSeconds();
	Match->ReadyStartedAt = Now;
	Match->PhaseEndsAt = Now + ChaosImpactMatch::ReadySeconds;
	Match->ForceNetUpdate();
	GetWorldTimerManager().SetTimer(MatchPhaseTimer, this, &AChaosImpactGameMode::BeginMatchPlay,
		ChaosImpactMatch::ReadySeconds, false);
	UE_LOG(LogChaosImpact, Log, TEXT("VS match Ready? (%d of %d openings reported)"),
		IntroFinishedMembers.Num(), Match->CountHumanMembers());
}

void AChaosImpactGameMode::BeginMatchPlay()
{
	const AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match)
	{
		return;
	}
	if (Match->Rules.bDuel)
	{
		// The clock of the duel is its rounds: the match phase itself has no end.
		SetMatchPhase(EChaosImpactOnlinePhase::Match, 0.0f);
		for (AChaosImpactPlayerState* Member : Match->GetCompetitors(true))
		{
			Member->PointsHistory.Reset();
		}
		SampleMatchHistory();
		GetWorldTimerManager().SetTimer(HistoryTimer, this, &AChaosImpactGameMode::SampleMatchHistory,
			ChaosImpactMatch::HistoryStepSeconds, true);
		// First the ball slot, then ROUND 1.
		SetDuelStep(EChaosImpactDuelStep::BallDraw, ChaosImpactMatch::DuelBallDrawSeconds);
		GetWorldTimerManager().SetTimer(DuelTimer, this, &AChaosImpactGameMode::FinishDuelBallDraw, ChaosImpactMatch::DuelBallDrawSeconds, false);
		UE_LOG(LogChaosImpact, Log, TEXT("Duel GO: first to %d rounds, %.0f s each"), ChaosImpactMatch::DuelRoundsToWin, GetDuelRoundSeconds());
		return;
	}
	const float Seconds = MatchDurationOverride > 0.0f ? MatchDurationOverride : Match->Rules.Minutes * 60.0f;
	SetMatchPhase(EChaosImpactOnlinePhase::Match, Seconds);
	GetWorldTimerManager().SetTimer(MatchPhaseTimer, this, &AChaosImpactGameMode::HandleMatchTimeExpired, Seconds, false);
	if (Match->IsBattleRoyale())
	{
		GetWorldTimerManager().SetTimer(RoyaleTimer, this, &AChaosImpactGameMode::UpdateRoyale, .1f, true);
	}
	if (Match->Rules.bCrashBall)
	{
		GetWorldTimerManager().SetTimer(CrashMatchTimer, this, &AChaosImpactGameMode::UpdateCrashMatch, 0.05f, true);
	}
	// The graph starts at 0 for everyone at GO.
	for (AChaosImpactPlayerState* Member : Match->GetCompetitors(true))
	{
		Member->PointsHistory.Reset();
	}
	SampleMatchHistory();
	GetWorldTimerManager().SetTimer(HistoryTimer, this, &AChaosImpactGameMode::SampleMatchHistory,
		ChaosImpactMatch::HistoryStepSeconds, true);
	UE_LOG(LogChaosImpact, Log, TEXT("VS match GO (%.0f s)"), Seconds);
}

void AChaosImpactGameMode::EndMatch()
{
	GetWorldTimerManager().ClearTimer(RoyaleTimer);
	GetWorldTimerManager().ClearTimer(DuelTimer);
	GetWorldTimerManager().ClearTimer(CrashMatchTimer);
	GetWorldTimerManager().ClearTimer(MatchPhaseTimer);
	// The last point of the graph is the final score; every number goes out now for the results.
	GetWorldTimerManager().ClearTimer(HistoryTimer);
	SampleMatchHistory();
	if (const AChaosImpactGameState* Ended = GetGameState<AChaosImpactGameState>())
	{
		for (AChaosImpactPlayerState* Member : Ended->GetCompetitors(true))
		{
			Member->ForceNetUpdate();
		}
	}
	SetMatchPhase(EChaosImpactOnlinePhase::Results, ChaosImpactMatch::ResultsSeconds);
	// Online, everyone who played is on the results until they come back to the lobby (or the wait runs out).
	if (AChaosImpactGameState* Room = GetGameState<AChaosImpactGameState>(); Room && IsOnlineRoom())
	{
		for (AChaosImpactPlayerState* Member : Room->GetCompetitors(false))
		{
			Member->bViewingResults = !Member->IsABot();
			Member->ForceNetUpdate();
		}
		GetWorldTimerManager().SetTimer(ResultsViewTimer, this, &AChaosImpactGameMode::ClearResultsViewers,
			ChaosImpactMatch::ResultsViewLimitSeconds, false);
	}
	DestroyStageBallSpawners();
	for (TActorIterator<AChaosImpactCharacter> It(GetWorld()); It; ++It)
	{
		It->CancelChargingThrow();
	}
	if (const AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>())
	{
		const TArray<AChaosImpactPlayerState*> Ranking = Match->GetRanking();
		UE_LOG(LogChaosImpact, Log, TEXT("VS match finished: winner %s with %d pt"),
			Ranking.IsEmpty() ? TEXT("-") : *Ranking[0]->GetPlayerName(), Ranking.IsEmpty() ? 0 : Ranking[0]->Points);
	}
	// A matched room ends with its results: everyone goes home from them (see ReturnFromResults).
	if (const AChaosImpactGameState* Room = GetGameState<AChaosImpactGameState>(); Room && Room->IsMatchmade() && IsOnlineRoom())
	{
		GetWorldTimerManager().SetTimer(MatchmadeCloseTimer, this, &AChaosImpactGameMode::CloseMatchmadeRoom, MatchmadeResultsSeconds, false);
		return;
	}
	// A local match stays on its results until the players choose a rematch or leave.
	if (IsOnlineRoom())
	{
		GetWorldTimerManager().SetTimer(MatchPhaseTimer, this, &AChaosImpactGameMode::ReturnToLobbyAfterMatch,
			ChaosImpactMatch::ResultsSeconds, false);
	}
}

void AChaosImpactGameMode::ReturnToLobbyAfterMatch()
{
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || !GetWorld())
	{
		return;
	}
	if (bRoomMatchLevel && IsOnlineRoom())
	{
		if (Match->IsMatchmade())
		{
			// A matched room goes home from its results (CloseMatchmadeRoom), never back to a lobby.
			return;
		}
		// Back to the room's lobby level, once nobody is still on their results (ClearResultsViewers ends that wait).
		for (const APlayerState* State : Match->PlayerArray)
		{
			if (const AChaosImpactPlayerState* Viewer = Cast<AChaosImpactPlayerState>(State); Viewer && Viewer->bViewingResults)
			{
				GetWorldTimerManager().SetTimer(RoomReturnTimer, this, &AChaosImpactGameMode::ReturnToLobbyAfterMatch, 0.5f, false);
				return;
			}
		}
		GetWorldTimerManager().ClearTimer(RoomReturnTimer);
		TravelRoom(UChaosImpactSessionSubsystem::GetTrainingMapName(), TEXT("CIRoomReturn=1"));
		return;
	}
	SyncTrainingCPUCount(0, FVector::ZeroVector, FRotator::ZeroRotator);
	ClearCrashMatch();
	DestroyStageBallSpawners();
	ClearMatchBalls();
	Match->bVersusMatch = false;
	Match->Royale = FChaosImpactRoyaleState();
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		const auto* Member = PC ? PC->GetPlayerState<AChaosImpactPlayerState>() : nullptr;
		if (Member && !Member->bSpectating && Cast<AChaosImpactSpectatorPawn>(PC->GetPawn()))
		{
			APawn* Camera = PC->GetPawn(); PC->UnPossess(); Camera->Destroy(); RestartPlayer(PC);
		}
	}
	SetMatchPhase(EChaosImpactOnlinePhase::Lobby, 0.0f);
	// Password rooms return to recruiting. Retain the last rule choices, but require the host to close recruitment
	// and confirm them again; no old ready deadline may automatically launch a rematch.
	GetWorldTimerManager().ClearTimer(StartingTimer);
	ClearReady();
	if (!Match->IsMatchmade())
	{
		Match->bRecruitmentClosed = false;
		Match->bRulesDecided = false;
		Match->ReadyDeadline = 0.0;
		Match->ForceNetUpdate();
	}
	else if (Match->bRulesDecided)
	{
		Match->ReadyDeadline = Match->GetServerWorldTimeSeconds() + GetReadyWaitSeconds();
	}
	for (TActorIterator<AChaosImpactTrainingTarget> It(GetWorld()); It; ++It)
	{
		It->SetTrainingEnabled(true);
	}
	const FVector Anchor = GetRoomAnchor();
	const TArray<AChaosImpactPlayerState*> Members = Match->GetMembersInJoinOrder();
	for (int32 Index = 0; Index < Members.Num(); ++Index)
	{
		Members[Index]->TeamIndex = INDEX_NONE;
		if (AChaosImpactCharacter* Character = Cast<AChaosImpactCharacter>(Members[Index]->GetPawn()))
		{
			const FVector Offset = Index == 0 ? FVector::ZeroVector
				: FVector(260.0f, 0.0f, 0.0f).RotateAngleAxis(Index * 51.0f, FVector::UpVector);
			Character->ResetForOnlineMatch(Anchor + Offset, FRotator::ZeroRotator);
		}
	}
	if (UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this))
	{
		Sessions->SetRecruitmentOpen(!Match->bRecruitmentClosed);
	}
	UpdateRoomMemberCount();
}

void AChaosImpactGameMode::ClearMatchBalls()
{
	for (TActorIterator<AChaosImpactSimaeBird> It(GetWorld()); It; ++It)
	{
		It->Destroy();
	}
	for (TActorIterator<AChaosImpactSimaeFeatherBurst> It(GetWorld()); It; ++It)
	{
		It->Destroy();
	}
	for (TActorIterator<AChaosImpactBall> It(GetWorld()); It; ++It)
	{
		It->Destroy();
	}
	for (TActorIterator<AChaosImpactHazardZone> It(GetWorld()); It; ++It)
	{
		It->Destroy();
	}
	for (TActorIterator<AChaosImpactTornado> It(GetWorld()); It; ++It)
	{
		It->Destroy();
	}
	for (TActorIterator<AChaosImpactHook> It(GetWorld()); It; ++It)
	{
		It->Destroy();
	}
}

void AChaosImpactGameMode::DestroyStageBallSpawners()
{
	for (AChaosImpactBallSpawner* Spawner : StageBallSpawners)
	{
		if (IsValid(Spawner))
		{
			if (AChaosImpactBall* Ball = Spawner->GetActiveBall())
			{
				Ball->Destroy();
			}
			Spawner->Destroy();
		}
	}
	StageBallSpawners.Reset();
}

void AChaosImpactGameMode::SampleMatchHistory()
{
	const AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || !Match->bVersusMatch)
	{
		return;
	}
	for (AChaosImpactPlayerState* Member : Match->GetCompetitors(true))
	{
		// A very long match keeps its first ten minutes or so in full; the final score is always the last point.
		const auto* Character = Cast<AChaosImpactCharacter>(Member->GetPawn());
		const int32 Side = Match->Duel.SideOf(Member);
		const int32 Sample = Match->IsBattleRoyale() ? (Character && !Character->IsEliminated() ? FMath::CeilToInt(Character->GetHealth()) : 0)
			: Match->IsDuel() && Side != INDEX_NONE ? Match->Duel.Wins[Side] : Member->Points;
		if (Member->PointsHistory.Num() < 400)
		{
			Member->PointsHistory.Add(static_cast<int16>(FMath::Clamp(Sample, -32000, 32000)));
		}
		else
		{
			Member->PointsHistory.Last() = static_cast<int16>(FMath::Clamp(Sample, -32000, 32000));
		}
	}
}

void AChaosImpactGameMode::RecordThrow(APawn* Thrower, const bool bSpecial)
{
	const AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	AChaosImpactPlayerState* Member = Thrower ? Thrower->GetPlayerState<AChaosImpactPlayerState>() : nullptr;
	if (!Match || !Member || !Match->bVersusMatch || Match->Phase != EChaosImpactOnlinePhase::Match)
	{
		return;
	}
	++Member->Throws;
	Member->SpecialThrows += bSpecial ? 1 : 0;
}

void AChaosImpactGameMode::RecordHit(APawn* Source, APawn* Victim, const AActor* Causer)
{
	const AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || !Match->bVersusMatch || Match->Phase != EChaosImpactOnlinePhase::Match || !Source || !Victim)
	{
		return;
	}
	if (AChaosImpactPlayerState* Hitter = Source->GetPlayerState<AChaosImpactPlayerState>())
	{
		++Hitter->Hits;
		Hitter->LongestHit = FMath::Max(Hitter->LongestHit,
			FMath::RoundToInt(static_cast<float>(FVector::Dist2D(Source->GetActorLocation(), Victim->GetActorLocation()))));
		const AChaosImpactBall* Ball = Cast<AChaosImpactBall>(Causer);
		Hitter->DriveHits += Ball && Ball->GetBallType() == EChaosImpactBallType::Drive ? 1 : 0;
	}
	if (AChaosImpactPlayerState* Hurt = Victim->GetPlayerState<AChaosImpactPlayerState>())
	{
		++Hurt->TimesHit;
	}
}

void AChaosImpactGameMode::RecordDodge(APawn* Dasher)
{
	const AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	AChaosImpactPlayerState* Member = Dasher ? Dasher->GetPlayerState<AChaosImpactPlayerState>() : nullptr;
	if (Match && Member && Match->bVersusMatch && Match->Phase == EChaosImpactOnlinePhase::Match)
	{
		++Member->Dodges;
	}
}

void AChaosImpactGameMode::RequestRematch(APlayerState* Member)
{
	AChaosImpactPlayerState* Asker = Cast<AChaosImpactPlayerState>(Member);
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Asker || !Match || Match->Matchmade != EChaosImpactMatchmade::Duel || !IsOnlineRoom()
		|| Match->Phase != EChaosImpactOnlinePhase::Results || !Asker->bViewingResults || Asker->bWantsRematch)
	{
		return;
	}
	Asker->bWantsRematch = true;
	Asker->ForceNetUpdate();
	TArray<AChaosImpactPlayerState*, TInlineAllocator<2>> Players;
	for (APlayerState* State : Match->PlayerArray)
	{
		AChaosImpactPlayerState* Player = Cast<AChaosImpactPlayerState>(State);
		if (Player && !Player->IsABot() && !Player->bSpectating)
		{
			Players.Add(Player);
		}
	}
	if (Players.Num() < 2 || Players.ContainsByPredicate([](const AChaosImpactPlayerState* Player) { return !Player->bWantsRematch; }))
	{
		UE_LOG(LogChaosImpact, Log, TEXT("%s asks for a rematch"), *Asker->GetPlayerName());
		return;
	}
	// Both asked: the same two again, in a new game (the room stays open for it).
	UE_LOG(LogChaosImpact, Log, TEXT("Matched 1v1: rematch"));
	GetWorldTimerManager().ClearTimer(MatchmadeCloseTimer);
	GetWorldTimerManager().ClearTimer(ResultsViewTimer);
	for (AChaosImpactPlayerState* Player : Players)
	{
		Player->bWantsRematch = false;
		Player->bViewingResults = false;
		Player->ForceNetUpdate();
	}
	TravelRoomToMatch();
}

void AChaosImpactGameMode::ReturnFromResults(APlayerState* Member)
{
	AChaosImpactPlayerState* Viewer = Cast<AChaosImpactPlayerState>(Member);
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Viewer || !Match || !Viewer->bViewingResults)
	{
		return;
	}
	Viewer->bViewingResults = false;
	Viewer->ForceNetUpdate();
	UE_LOG(LogChaosImpact, Log, TEXT("%s is back from the results"), *Viewer->GetPlayerName());
	if (Match->IsMatchmade() && IsOnlineRoom())
	{
		// A matched room: that machine goes home (a guest at once; the host once its guests have gone).
		APlayerController* ViewerController = Cast<APlayerController>(Viewer->GetOwner());
		if (ViewerController && !ViewerController->IsLocalController())
		{
			if (AChaosImpactPlayerController* Guest = Cast<AChaosImpactPlayerController>(ViewerController))
			{
				Guest->ClientLeaveMatchmade();
			}
		}
		else if (Viewer->bRoomHost)
		{
			// The host goes home now, not once its guests have left their results: they are sent home too (told the
			// other player left), a moment ahead so the room does not vanish under them.
			bMatchmadeHostDone = true;
			for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			{
				AChaosImpactPlayerController* Guest = Cast<AChaosImpactPlayerController>(It->Get());
				if (Guest && !Guest->IsLocalController() && !Cast<UChildConnection>(Guest->Player))
				{
					Guest->ClientLeaveMatchmade(true);
				}
			}
			if (UChaosImpactLoadingSubsystem* Loading = UChaosImpactLoadingSubsystem::Get(this))
			{
				Loading->BeginTravel(EChaosImpactLoadingKind::RoomLeave);
			}
			FTimerHandle LeaveTimer;
			GetWorldTimerManager().SetTimer(LeaveTimer, FTimerDelegate::CreateWeakLambda(this, [this]() { FinishMatchmadeIfEmpty(true); }),
				0.35f, false);
		}
		return;
	}
	// The first one back opens the lobby for everyone (the others stay on their results meanwhile).
	if (Match->Phase == EChaosImpactOnlinePhase::Results && Match->GetPhaseElapsedSeconds() >= ChaosImpactMatch::ResultsRevealSeconds)
	{
		GetWorldTimerManager().ClearTimer(MatchPhaseTimer);
		ReturnToLobbyAfterMatch();
	}
}

FString AChaosImpactGameMode::GetCarryKey(const AChaosImpactPlayerState* Member, const bool bSecondOfMachine) const
{
	// The host's own players log in without a machine token.
	const FString Machine = Member && !Member->bHostMachine ? Member->MachineToken : FString(TEXT("@host"));
	return Machine + (bSecondOfMachine ? TEXT("#2") : TEXT("#1"));
}

APlayerController* AChaosImpactGameMode::ProcessClientTravel(FString& URL, const bool bSeamless, const bool bAbsolute)
{
	const bool bToMatch = URL.Contains(TEXT("CIRoomMatch=1"));
	if (!bToMatch && !URL.Contains(TEXT("CIRoomReturn=1")))
	{
		return Super::ProcessClientTravel(URL, bSeamless, bAbsolute);
	}
	// The host's options are its own players and controllers: each guest keeps its own (from when it joined, carried
	// on by a relative travel) and is sent only the level and why the room is going there.
	const FURL Next(nullptr, *URL, TRAVEL_Absolute);
	const FString GuestURL = Next.Map + (bToMatch ? TEXT("?CIRoomMatch=1") : TEXT("?CIRoomReturn=1"));
	APlayerController* LocalController = nullptr;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* Controller = It->Get();
		if (!Controller)
		{
			continue;
		}
		if (Cast<UNetConnection>(Controller->Player))
		{
			Controller->ClientTravel(GuestURL, TRAVEL_Relative, bSeamless);
		}
		else
		{
			LocalController = Controller;
			Controller->PreClientTravel(URL, bAbsolute ? TRAVEL_Absolute : TRAVEL_Relative, bSeamless);
		}
	}
	return LocalController;
}

bool AChaosImpactGameMode::IsCarriedMachine(const FString& MachineToken) const
{
	const UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this);
	return Sessions && !MachineToken.IsEmpty() && Sessions->RoomCarry.Contains(MachineToken + TEXT("#1"));
}

void AChaosImpactGameMode::TravelRoom(const FString& Level, const TCHAR* Purpose)
{
	AChaosImpactGameState* Room = GetGameState<AChaosImpactGameState>();
	UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this);
	if (!Room || !Sessions || !GetWorld())
	{
		return;
	}
	Sessions->RoomCarry.Reset();
	for (const APlayerState* State : Room->PlayerArray)
	{
		const AChaosImpactPlayerState* Member = Cast<AChaosImpactPlayerState>(State);
		if (!Member || Member->IsABot())
		{
			continue;
		}
		FChaosImpactRoomCarryMember& Carried = Sessions->RoomCarry.Add(GetCarryKey(Member, Member->bSecondOfMachine));
		Carried.JoinOrder = Member->JoinOrder;
		Carried.bSpectating = Member->bSpectating;
		Carried.TeamIndex = FCString::Strstr(Purpose, TEXT("CIRoomMatch")) && !Member->bSpectating ? Member->TeamIndex : INDEX_NONE;
	}
	Sessions->RoomCarryNextJoinOrder = NextJoinOrder;
	Sessions->bRoomCarryValid = true;
	// The options the room was opened with (listen, the host's players and controllers, a matched room's kind), with
	// this move's purpose in place of the last one's.
	FString URL = Level;
	bool bListen = false;
	for (const FString& Option : GetWorld()->URL.Op)
	{
		if (Option.StartsWith(TEXT("CIRoomMatch")) || Option.StartsWith(TEXT("CIRoomReturn")))
		{
			continue;
		}
		bListen = bListen || Option.Equals(TEXT("listen"), ESearchCase::IgnoreCase);
		URL += TEXT("?") + Option;
	}
	URL += bListen ? TEXT("") : TEXT("?listen");
	URL += FString(TEXT("?")) + Purpose;
	UE_LOG(LogChaosImpact, Log, TEXT("Room moving to %s with %d member(s): %s"), *Level, Sessions->RoomCarry.Num(), *URL);
	GetWorldTimerManager().ClearAllTimersForObject(this);
	if (UChaosImpactLoadingSubsystem* Loading = UChaosImpactLoadingSubsystem::Get(this))
	{
		Loading->BeginTravel(FCString::Strstr(Purpose, TEXT("CIRoomMatch")) ? EChaosImpactLoadingKind::RoomMatch : EChaosImpactLoadingKind::RoomLobby);
	}
	GetWorld()->ServerTravel(URL, true);
}

void AChaosImpactGameMode::TravelRoomToMatch()
{
	AChaosImpactGameState* Room = GetGameState<AChaosImpactGameState>();
	UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this);
	if (!Room || !Sessions)
	{
		return;
	}
	const FString Level = ChaosImpactMatch::GetMatchLevel(Room->Rules);
	if (!FPackageName::DoesPackageExist(Level))
	{
		// (A build without the level: the match is played in the lobby, as rooms once did.)
		UE_LOG(LogChaosImpact, Warning, TEXT("Room match level %s is missing; playing in the lobby"), *Level);
		ConfigureVersusMatch(Room->Rules);
		return;
	}
	Sessions->RoomCarryRules = Room->Rules;
	TravelRoom(Level, TEXT("CIRoomMatch=1"));
}

void AChaosImpactGameMode::BeginRoomMatchLevel(AChaosImpactGameState* RoomState)
{
	const UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this);
	if (!RoomState || !Sessions || !Sessions->bRoomCarryValid)
	{
		return;
	}
	RoomState->Rules = Sessions->RoomCarryRules;
	RoomState->bRulesDecided = true;
	RoomState->bRecruitmentClosed = true;
	SetMatchPhase(EChaosImpactOnlinePhase::Starting, 0.0f);
	RoomArrivalDeadline = RoomState->GetServerWorldTimeSeconds() + RoomArrivalWaitSeconds;
	GetWorldTimerManager().SetTimer(RoomArrivalTimer, this, &AChaosImpactGameMode::UpdateRoomArrivals, 0.25f, true, 0.5f);
	UE_LOG(LogChaosImpact, Log, TEXT("Room match level: waiting for %d member(s) to come back in (%s)"), Sessions->RoomCarry.Num(),
		*ChaosImpactMatch::DescribeRule(RoomState->Rules));
}

void AChaosImpactGameMode::UpdateRoomArrivals()
{
	AChaosImpactGameState* Room = GetGameState<AChaosImpactGameState>();
	const UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this);
	if (!Room || !Sessions)
	{
		return;
	}
	int32 Arrived = 0;
	int32 ArrivedMachines = 0;
	for (const APlayerState* State : Room->PlayerArray)
	{
		const AChaosImpactPlayerState* Member = Cast<AChaosImpactPlayerState>(State);
		if (Member && !Member->IsABot() && Sessions->RoomCarry.Contains(GetCarryKey(Member, Member->bSecondOfMachine)))
		{
			++Arrived;
			ArrivedMachines += Member->bSecondOfMachine ? 0 : 1;
		}
	}
	const int32 Expected = Sessions->RoomCarry.Num();
	const bool bTimeUp = Room->GetServerWorldTimeSeconds() >= RoomArrivalDeadline;
	if ((Arrived < Expected || GetReservedRoomSlots() > 0) && !bTimeUp)
	{
		return;
	}
	GetWorldTimerManager().ClearTimer(RoomArrivalTimer);
	// (By machines: a split-screen pair back in alone is still nobody to play against.)
	if (Room->IsMatchmade() && (Arrived < 2 || ArrivedMachines < 2))
	{
		// A matched room is played by those matched: no stand-in CPU. Everyone goes home (the online menu).
		UE_LOG(LogChaosImpact, Log, TEXT("Room match level: the other player did not come back; closing the matched room"));
		CloseMatchmadeRoom();
		return;
	}
	UE_LOG(LogChaosImpact, Log, TEXT("Room match level: %d of %d back in; the match begins"), Arrived, Expected);
	ConfigureVersusMatch(Room->Rules);
	if (FString AutoVersus; FParse::Value(FCommandLine::Get(), TEXT("CIAutoVersus="), AutoVersus))
	{
		// Development: the teams are confirmed by themselves, as -CIAutoVersus does in the lobby.
		if (Room->Rules.IsTeamBattle())
		{
			FTimerHandle ConfirmTimer;
			GetWorldTimerManager().SetTimer(ConfirmTimer, this, &AChaosImpactGameMode::ConfirmTeamsAndStart, 4.0f, false);
		}
	}
}

void AChaosImpactGameMode::ClearResultsViewers()
{
	if (const AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>())
	{
		for (APlayerState* Member : Match->PlayerArray)
		{
			if (AChaosImpactPlayerState* Viewer = Cast<AChaosImpactPlayerState>(Member); Viewer && Viewer->bViewingResults)
			{
				Viewer->bViewingResults = false;
				Viewer->ForceNetUpdate();
			}
		}
	}
}

void AChaosImpactGameMode::AwardMatchPoints(APlayerState* Scorer, const int32 Points, const bool bKnockout)
{
	const AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	AChaosImpactPlayerState* Member = Cast<AChaosImpactPlayerState>(Scorer);
	if (!Match || !Member || !Match->bVersusMatch || Match->Phase != EChaosImpactOnlinePhase::Match)
	{
		return;
	}
	Member->Points += Points;
	Member->Knockouts += bKnockout ? 1 : 0;
	Member->ForceNetUpdate();
}

bool AChaosImpactGameMode::IsOutsideStage(const FVector& Location) const
{
	return VersusStage && VersusStage->IsOutside(Location);
}

bool AChaosImpactGameMode::ChooseMatchRespawn(const AActor* Character, FVector& OutLocation, FRotator& OutRotation) const
{
	const AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || !Match->bVersusMatch || Match->Phase != EChaosImpactOnlinePhase::Match
		|| !IsValid(VersusStage) || !GetWorld())
	{
		return false;
	}
	// Prefer the points farthest from living opponents, with a little randomness among the best few.
	// クラッシュボール: only points on the character's own half (the goal it defends).
	const AChaosImpactPlayerState* Member = Cast<APawn>(Character) ? Cast<APawn>(Character)->GetPlayerState<AChaosImpactPlayerState>() : nullptr;
	const TArray<FVector> Points = Match->Rules.bCrashBall && Member
		? GetCrashTeamSpawnPoints(Member->TeamIndex) : VersusStage->GetSpawnPoints();
	TArray<TPair<float, FVector>> Candidates;
	for (const FVector& Point : Points)
	{
		float Nearest = UE_BIG_NUMBER;
		for (TActorIterator<AChaosImpactCharacter> It(GetWorld()); It; ++It)
		{
			if (*It != Character && !It->IsEliminated() && !AChaosImpactGameState::AreTeammates(GetWorld(), *It, Character))
			{
				Nearest = FMath::Min(Nearest, static_cast<float>(FVector::Dist2D(It->GetActorLocation(), Point)));
			}
		}
		Candidates.Add({Nearest, Point});
	}
	if (Candidates.IsEmpty())
	{
		return false;
	}
	Candidates.Sort([](const TPair<float, FVector>& A, const TPair<float, FVector>& B) { return A.Key > B.Key; });
	const FVector Floor = Candidates[FMath::RandRange(0, FMath::Min(3, Candidates.Num() - 1))].Value;
	const ACharacter* AsCharacter = Cast<ACharacter>(Character);
	const float HalfHeight = AsCharacter ? AsCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 96.0f;
	OutLocation = Floor + FVector(0.0f, 0.0f, HalfHeight + 4.0f);
	OutRotation = FRotator(0.0f, (Match->StageCenter - Floor).Rotation().Yaw, 0.0f);
	return true;
}

void AChaosImpactGameMode::StartLocalMatchFromURL()
{
	FChaosImpactMatchRules Rules;
	if (GetWorld() && ChaosImpactMatch::ReadOptions(GetWorld()->URL, Rules))
	{
		ConfigureVersusMatch(Rules);
	}
}

void AChaosImpactGameMode::RunDevAutoVersus()
{
	if (AChaosImpactGameState* Room = GetGameState<AChaosImpactGameState>(); Room && IsOnlineRoom() && !bRoomMatchLevel)
	{
		// An online room plays it in its own level (the teams are confirmed there by itself: UpdateRoomArrivals).
		Room->Rules = ChaosImpactMatch::Sanitize(DevAutoVersusRules, Room->CountHumanMembers());
		Room->bRulesDecided = true;
		TravelRoomToMatch();
		return;
	}
	ConfigureVersusMatch(DevAutoVersusRules);
	if (DevAutoVersusRules.IsTeamBattle())
	{
		FTimerHandle ConfirmTimer;
		GetWorldTimerManager().SetTimer(ConfirmTimer, this, &AChaosImpactGameMode::ConfirmTeamsAndStart, 4.0f, false);
	}
}

void AChaosImpactGameMode::SetTrainingCPUCountLive(const int32 DesiredCPUCount)
{
	if (!GetWorld())
	{
		return;
	}
	LiveDesiredCPUCount = FMath::Clamp(DesiredCPUCount, 0, 4);
	FVector Anchor = FVector::ZeroVector;
	FRotator Facing = FRotator::ZeroRotator;
	if (UGameInstance* GameInstance = GetGameInstance();
		GameInstance && !GameInstance->GetLocalPlayers().IsEmpty())
	{
		if (APlayerController* FirstController =
			GameInstance->GetLocalPlayers()[0]->GetPlayerController(GetWorld()))
		{
			if (APawn* FirstPawn = FirstController->GetPawn())
			{
				Anchor = FirstPawn->GetActorLocation();
				Facing = FirstPawn->GetActorRotation();
			}
		}
	}
	SyncTrainingCPUCount(LiveDesiredCPUCount, Anchor, Facing);
}

void AChaosImpactGameMode::SyncTrainingCPUCount(
	const int32 DesiredCPUCount, const FVector& Anchor, const FRotator& Facing)
{
	// Only the CPUs this mode spawned: enemies placed in the level are not training CPUs.
	TArray<AChaosImpactCPUController*> CPUControllers;
	for (TActorIterator<AChaosImpactCPUController> It(GetWorld()); It; ++It)
	{
		if (It->IsMatchCPU())
		{
			CPUControllers.Add(*It);
		}
	}
	while (CPUControllers.Num() > DesiredCPUCount)
	{
		AChaosImpactCPUController* Controller = CPUControllers.Pop();
		if (APawn* Pawn = Controller ? Controller->GetPawn() : nullptr)
		{
			Pawn->Destroy();
		}
		if (Controller)
		{
			Controller->Destroy();
		}
	}
	for (int32 CPUIndex = CPUControllers.Num(); CPUIndex < DesiredCPUCount; ++CPUIndex)
	{
		SpawnTrainingCPU(Anchor, Facing, CPUIndex);
	}
}

void AChaosImpactGameMode::SpawnTrainingCPU(
	const FVector& Anchor, const FRotator& Facing, const int32 CPUIndex)
{
	if (!GetWorld())
	{
		return;
	}
	FActorSpawnParameters ControllerParameters;
	ControllerParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AChaosImpactCPUController* CPUController = GetWorld()->SpawnActor<AChaosImpactCPUController>(
		AChaosImpactCPUController::StaticClass(), Anchor, Facing, ControllerParameters);
	if (!CPUController)
	{
		return;
	}
	CPUController->MarkAsMatchCPU();

	const FVector CPUOffsets[] =
	{
		FVector(-680.0f, -390.0f, 0.0f),
		FVector(-680.0f, 390.0f, 0.0f),
		FVector(620.0f, -360.0f, 0.0f),
		FVector(620.0f, 360.0f, 0.0f)
	};
	const FVector CPULocation = CPUSpawnPoints.IsEmpty() ? Anchor + CPUOffsets[FMath::Clamp(CPUIndex, 0, 3)]
		: CPUSpawnPoints[(CPUIndex + 1) % CPUSpawnPoints.Num()] + FVector(0.0f, 0.0f, 110.0f);
	const FRotator CPURotation = CPUSpawnPoints.IsEmpty() ? (Anchor - CPULocation).Rotation()
		: (Anchor - CPULocation).GetSafeNormal2D().Rotation();
	APawn* CPUPawn = SpawnDefaultPawnAtTransform(CPUController,
		FTransform(FRotator(0.0f, CPURotation.Yaw, 0.0f), CPULocation));
	if (!CPUPawn)
	{
		CPUController->Destroy();
		return;
	}
	CPUController->Possess(CPUPawn);
	if (AChaosImpactCharacter* CPUCharacter = Cast<AChaosImpactCharacter>(CPUPawn))
	{
		CPUCharacter->SetTrainingStartTransform(CPULocation,
			FRotator(0.0f, CPURotation.Yaw, 0.0f));
		CPUCharacter->SetCPUNumber(CPUIndex + 1);
	}
	if (APlayerState* CPUState = CPUController->PlayerState)
	{
		CPUState->SetPlayerName(FString::Printf(TEXT("CPU%d"), CPUIndex + 1));
		if (AChaosImpactPlayerState* CPULoadout = Cast<AChaosImpactPlayerState>(CPUState))
		{
			CPULoadout->CharacterIndex = PickCPUCharacter(CPUState);
			CPULoadout->ForceNetUpdate();
		}
	}
}

AChaosImpactCharacter* AChaosImpactGameMode::SpawnDojoCPU(const FVector& Location, const FRotator& Rotation, const int32 Number)
{
	if (!GetWorld())
	{
		return nullptr;
	}
	FActorSpawnParameters ControllerParameters;
	ControllerParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AChaosImpactCPUController* CPUController = GetWorld()->SpawnActor<AChaosImpactCPUController>(AChaosImpactCPUController::StaticClass(),
		Location, Rotation, ControllerParameters);
	if (!CPUController)
	{
		return nullptr;
	}
	const FRotator Facing(0.0f, Rotation.Yaw, 0.0f);
	APawn* CPUPawn = SpawnDefaultPawnAtTransform(CPUController, FTransform(Facing, Location));
	if (!CPUPawn)
	{
		CPUController->Destroy();
		return nullptr;
	}
	CPUController->Possess(CPUPawn);
	AChaosImpactCharacter* CPUCharacter = Cast<AChaosImpactCharacter>(CPUPawn);
	if (CPUCharacter)
	{
		CPUCharacter->SetTrainingStartTransform(Location, Facing);
		CPUCharacter->SetRespawnPoint(Location, Facing);
		CPUCharacter->SetCPUNumber(Number);
	}
	if (APlayerState* CPUState = CPUController->PlayerState)
	{
		CPUState->SetPlayerName(FString::Printf(TEXT("CPU%d"), Number));
		if (AChaosImpactPlayerState* CPULoadout = Cast<AChaosImpactPlayerState>(CPUState))
		{
			CPULoadout->CharacterIndex = PickCPUCharacter(CPUState);
			CPULoadout->ForceNetUpdate();
		}
	}
	return CPUCharacter;
}

int32 AChaosImpactGameMode::PickCPUCharacter(const APlayerState* NewCPU) const
{
	// Counted over everyone already here, so CPUs spread over the roster (and a roster that grows needs no change).
	TArray<int32> Uses;
	Uses.Init(0, ChaosImpactRoster::Num());
	if (const AGameStateBase* State = GetGameState<AGameStateBase>())
	{
		for (const APlayerState* Member : State->PlayerArray)
		{
			if (const AChaosImpactPlayerState* Loadout = Cast<AChaosImpactPlayerState>(Member);
				Loadout && Loadout != NewCPU && !Loadout->IsPendingKillPending())
			{
				++Uses[ChaosImpactRoster::ClampIndex(Loadout->CharacterIndex)];
			}
		}
	}
	const int32 Fewest = FMath::Min(Uses);
	TArray<int32, TInlineAllocator<8>> Candidates;
	for (int32 Character = 0; Character < Uses.Num(); ++Character)
	{
		if (Uses[Character] == Fewest)
		{
			Candidates.Add(Character);
		}
	}
	return Candidates.IsEmpty() ? 0 : Candidates[FMath::RandRange(0, Candidates.Num() - 1)];
}

// ---------------------------------------------------------------------------------------------------- Matched rooms

void AChaosImpactGameMode::UpdateMatchmadeRoom()
{
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || !Match->IsMatchmade() || bMatchmadeMatchBegun || Match->Phase != EChaosImpactOnlinePhase::Lobby)
	{
		return;
	}
	const int32 Humans = Match->CountHumanMembers();
	// Counted by machines too: a split-screen pair is two players but one machine, and never a match by itself.
	const int32 Machines = Match->CountMachines();
	const bool bEveryone = Humans >= Match->MatchmadeExpected && Machines >= 2 && GetReservedRoomSlots() == 0;
	const bool bTimeUp = Match->GetServerWorldTimeSeconds() >= Match->MatchmadeDeadline;
	if (!bEveryone && !bTimeUp)
	{
		return;
	}
	if (Humans < 2 || Machines < 2)
	{
		// Nobody else came: back to this machine's training, where the search can start again.
		UE_LOG(LogChaosImpact, Log, TEXT("Matched room: no other machine arrived in %.0f s (%d player(s) here); closing it"),
			MatchmadeWaitSeconds, Humans);
		bMatchmadeMatchBegun = true;
		GetWorldTimerManager().ClearTimer(MatchmadeTimer);
		FinishMatchmadeIfEmpty(true);
		return;
	}
	bMatchmadeMatchBegun = true;
	GetWorldTimerManager().ClearTimer(MatchmadeTimer);
	Match->Rules = MakeMatchmadeRules(Humans);
	Match->bRulesDecided = true;
	Match->MatchmadeDeadline = 0.0;
	CloseRecruitment();
	UE_LOG(LogChaosImpact, Log, TEXT("Matched room starting with %d of %d players (%s)"), Humans, Match->MatchmadeExpected,
		*ChaosImpactMatch::DescribeRule(Match->Rules));
	BeginStartingCountdown();
}

FChaosImpactMatchRules AChaosImpactGameMode::MakeMatchmadeRules(const int32 Humans) const
{
	const AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	FChaosImpactMatchRules Rules;
	Rules.CPULevel = ChaosImpactMatch::CPULevelNormal;
	if (Match && (Match->Matchmade == EChaosImpactMatchmade::Rank || Match->Matchmade == EChaosImpactMatchmade::Duel))
	{
		Rules.bDuel = true;
		return ChaosImpactMatch::Sanitize(Rules, Humans);
	}
	// ランダムマッチ: everyone for themselves on a stage picked at random, three minutes, CPUs making up the eight.
	Rules.Minutes = 3;
	Rules.TeamCount = 0;
	Rules.StageIndex = FMath::RandRange(0, ChaosImpactMatch::StageCount - 1);
	Rules.CPUCount = ChaosImpactMatch::MaxCompetitors - Humans;
	return ChaosImpactMatch::Sanitize(Rules, Humans);
}

void AChaosImpactGameMode::CloseMatchmadeRoom()
{
	if (!GetWorld())
	{
		return;
	}
	UE_LOG(LogChaosImpact, Log, TEXT("Matched room closing: everyone goes home"));
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		AChaosImpactPlayerController* Member = Cast<AChaosImpactPlayerController>(It->Get());
		if (Member && !Member->IsLocalController() && !Cast<UChildConnection>(Member->Player))
		{
			Member->ClientLeaveMatchmade();
		}
	}
	bMatchmadeHostDone = true;
	// A moment for the guests to go first, so they leave rather than see the room vanish.
	FTimerHandle LeaveTimer;
	GetWorldTimerManager().SetTimer(LeaveTimer, FTimerDelegate::CreateWeakLambda(this, [this]() { FinishMatchmadeIfEmpty(true); }), 1.5f, false);
}

void AChaosImpactGameMode::FinishMatchmadeIfEmpty(const bool bForce)
{
	if (!GetWorld())
	{
		return;
	}
	if (!bForce)
	{
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			if (const APlayerController* Member = It->Get(); Member && !Member->IsLocalController())
			{
				return;
			}
		}
	}
	GetWorldTimerManager().ClearTimer(MatchmadeCloseTimer);
	if (UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this))
	{
		UE_LOG(LogChaosImpact, Log, TEXT("Matched room: the host goes home"));
		Sessions->FinishMatchmade();
	}
}

// ---------------------------------------------------------------------------------------------------- The duel

void AChaosImpactGameMode::FinishDuelBallDraw()
{
	const AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || Match->Duel.Step != EChaosImpactDuelStep::BallDraw)
	{
		return;
	}
	ApplyDuelBallChances(true);
	StartDuelRound(1);
}

void AChaosImpactGameMode::ApplyDuelBallChances(const bool bWithPicks)
{
	const AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match)
	{
		return;
	}
	// The drawn kinds come out as often as in a VS match (the pads' own weights), the rest never.
	const AChaosImpactBallSpawner* Defaults = GetDefault<AChaosImpactBallSpawner>();
	for (AChaosImpactBallSpawner* Spawner : StageBallSpawners)
	{
		if (!IsValid(Spawner))
		{
			continue;
		}
		for (int32 Type = 1; Type < ChaosImpactBallTypes::Count; ++Type)
		{
			const bool bDrawn = bWithPicks && Match->Duel.BallPicks.Contains(static_cast<uint8>(Type));
			const float Weight = Defaults->GetChanceWeight(static_cast<EChaosImpactBallType>(Type));
			Spawner->SetChanceWeight(static_cast<EChaosImpactBallType>(Type), bDrawn ? FMath::Max(Weight, 0.05f) : 0.0f);
		}
	}
}

float AChaosImpactGameMode::GetDuelRoundSeconds() const
{
	return MatchDurationOverride > 0.0f ? MatchDurationOverride : ChaosImpactMatch::DuelRoundSeconds;
}

APawn* AChaosImpactGameMode::GetDuelPawn(const int32 Side) const
{
	const AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	const APlayerState* Member = Match && (Side == 0 || Side == 1) ? Match->Duel.Sides[Side].Get() : nullptr;
	return Member ? Member->GetPawn() : nullptr;
}

void AChaosImpactGameMode::SetDuelStep(const EChaosImpactDuelStep Step, const float Seconds)
{
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match)
	{
		return;
	}
	const double Now = Match->GetServerWorldTimeSeconds();
	Match->Duel.Step = Step;
	Match->Duel.StepStartedAt = Now;
	Match->Duel.StepEndsAt = Seconds > 0.0f ? Now + Seconds : 0.0;
	Match->ForceNetUpdate();
}

void AChaosImpactGameMode::StartDuelRound(const int32 Round)
{
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || !Match->IsDuel())
	{
		return;
	}
	Match->Duel.Round = Round;
	Match->Duel.RoundWinner = INDEX_NONE;
	Match->Duel.RoundEnd = EChaosImpactDuelEnd::None;
	if (Round > 1)
	{
		// Every round starts the same: the field cleared, both back at their starts with full health and no balls,
		// and a fresh ball on every pad (none missing because one was taken just before the last round ended).
		ClearMatchBalls();
		PlaceDuelists();
		for (TActorIterator<AChaosImpactBallSpawner> It(GetWorld()); It; ++It)
		{
			It->SpawnNow();
		}
	}
	SetDuelStep(EChaosImpactDuelStep::RoundIntro, ChaosImpactMatch::DuelRoundIntroSeconds);
	GetWorldTimerManager().SetTimer(DuelTimer, this, &AChaosImpactGameMode::BeginDuelFight, ChaosImpactMatch::DuelRoundIntroSeconds, false);
	UE_LOG(LogChaosImpact, Log, TEXT("Duel round %d (%d - %d)"), Round, Match->Duel.Wins[0], Match->Duel.Wins[1]);
}

void AChaosImpactGameMode::PlaceDuelists()
{
	const AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || !IsValid(VersusStage))
	{
		return;
	}
	const TArray<FVector> Starts = VersusStage->GetSpawnPoints();
	for (int32 Side = 0; Side < 2; ++Side)
	{
		AChaosImpactCharacter* Character = Cast<AChaosImpactCharacter>(GetDuelPawn(Side));
		if (!Character || !Starts.IsValidIndex(Side))
		{
			continue;
		}
		const FVector Floor = Starts[Side];
		const FVector Location = Floor + FVector(0.0f, 0.0f, Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 4.0f);
		Character->ResetForOnlineMatch(Location, FRotator(0.0f, (Match->StageCenter - Floor).Rotation().Yaw, 0.0f));
	}
}

void AChaosImpactGameMode::BeginDuelFight()
{
	const AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || Match->Duel.Step != EChaosImpactDuelStep::RoundIntro)
	{
		return;
	}
	SetDuelStep(EChaosImpactDuelStep::Fight, GetDuelRoundSeconds());
	GetWorldTimerManager().SetTimer(DuelTimer, this, &AChaosImpactGameMode::HandleDuelTimeUp, GetDuelRoundSeconds(), false);
}

void AChaosImpactGameMode::HandleDuelTimeUp()
{
	const AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || Match->Duel.Step != EChaosImpactDuelStep::Fight)
	{
		return;
	}
	// The moment the time runs out decides it: more health left wins, level goes to sudden death.
	float Health[2] = {0.0f, 0.0f};
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const AChaosImpactCharacter* Character = Cast<AChaosImpactCharacter>(GetDuelPawn(Side));
		Health[Side] = Character && !Character->IsEliminated() ? Character->GetHealth() : 0.0f;
	}
	if (!FMath::IsNearlyEqual(Health[0], Health[1]))
	{
		EndDuelRound(Health[0] > Health[1] ? 0 : 1, EChaosImpactDuelEnd::Time);
		return;
	}
	SetDuelStep(EChaosImpactDuelStep::SuddenDeath, ChaosImpactMatch::DuelSuddenDeathSeconds);
	BeginSuddenDeath(INDEX_NONE, Match->StageCenter, Match->StageHalfExtent);
	GetWorldTimerManager().SetTimer(DuelTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		// The bell: the one nearer the middle (the circle has all but closed).
		const int32 Winner = JudgeSuddenDeathByMiddle(INDEX_NONE);
		EndDuelRound(Winner, Winner == INDEX_NONE ? EChaosImpactDuelEnd::Draw : EChaosImpactDuelEnd::SuddenDeath);
	}), ChaosImpactMatch::DuelSuddenDeathSeconds, false);
	UE_LOG(LogChaosImpact, Log, TEXT("Duel round %d: level at the bell, sudden death"), Match->Duel.Round);
}

void AChaosImpactGameMode::NotifyDuelKnockout(APawn* Victim)
{
	const AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (Match && Match->IsTournament())
	{
		// Judged on the arena's next update, once this frame's hits are all in (both may go down at once).
		const int32 Arena = Victim ? Match->GetTournamentArenaAt(Victim->GetActorLocation()) : INDEX_NONE;
		if (TournamentKnockoutPending.IsValidIndex(Arena))
		{
			TournamentKnockoutPending[Arena] = true;
		}
		return;
	}
	if (!Match || !Match->IsDuelLive() || bDuelKnockoutCheckPending)
	{
		return;
	}
	// Both may go down in the same moment: judged once this frame's hits are all in.
	bDuelKnockoutCheckPending = true;
	GetWorldTimerManager().SetTimerForNextTick(this, &AChaosImpactGameMode::EvaluateDuelKnockouts);
}

void AChaosImpactGameMode::EvaluateDuelKnockouts()
{
	bDuelKnockoutCheckPending = false;
	const AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || !Match->IsDuelLive())
	{
		return;
	}
	bool bDown[2] = {false, false};
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const AChaosImpactCharacter* Character = Cast<AChaosImpactCharacter>(GetDuelPawn(Side));
		bDown[Side] = !Character || Character->IsEliminated();
	}
	if (bDown[0] && bDown[1])
	{
		EndDuelRound(INDEX_NONE, EChaosImpactDuelEnd::DoubleKnockout);
	}
	else if (bDown[0] || bDown[1])
	{
		EndDuelRound(bDown[0] ? 1 : 0, EChaosImpactDuelEnd::Knockout);
	}
}

void AChaosImpactGameMode::NotifyDuelHit(APawn* Victim)
{
	const AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (Match && Match->IsTournament())
	{
		// Sudden death in an arena: the first hit takes the round.
		const int32 Arena = Victim ? Match->GetTournamentArenaAt(Victim->GetActorLocation()) : INDEX_NONE;
		if (Match->TournamentArenas.IsValidIndex(Arena) && Match->TournamentArenas[Arena].Duel.Step == EChaosImpactDuelStep::SuddenDeath)
		{
			const int32 Side = Match->TournamentArenas[Arena].Duel.SideOf(Victim->GetPlayerState());
			if (Side != INDEX_NONE)
			{
				EndArenaRound(Arena, 1 - Side, EChaosImpactDuelEnd::SuddenDeath);
			}
		}
		return;
	}
	if (!Match || Match->Duel.Step != EChaosImpactDuelStep::SuddenDeath || !Victim)
	{
		return;
	}
	const int32 Side = Match->Duel.SideOf(Victim->GetPlayerState());
	if (Side != INDEX_NONE)
	{
		EndDuelRound(1 - Side, EChaosImpactDuelEnd::SuddenDeath);
	}
}

void AChaosImpactGameMode::EndDuelRound(const int32 WinnerSide, const EChaosImpactDuelEnd How)
{
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || !Match->IsDuelLive())
	{
		return;
	}
	GetWorldTimerManager().ClearTimer(DuelTimer);
	EndSuddenDeath(INDEX_NONE);
	FChaosImpactDuelState& Duel = Match->Duel;
	Duel.RoundWinner = WinnerSide == 0 || WinnerSide == 1 ? WinnerSide : INDEX_NONE;
	Duel.RoundEnd = How;
	if (Duel.RoundWinner != INDEX_NONE)
	{
		++Duel.Wins[Duel.RoundWinner];
	}
	Duel.RoundResults.Add(static_cast<int8>(Duel.RoundWinner != INDEX_NONE ? Duel.RoundWinner : -1));
	SetDuelStep(EChaosImpactDuelStep::RoundOver, ChaosImpactMatch::DuelRoundOverSeconds);
	for (TActorIterator<AChaosImpactCharacter> It(GetWorld()); It; ++It)
	{
		It->CancelChargingThrow();
	}
	GetWorldTimerManager().SetTimer(DuelTimer, this, &AChaosImpactGameMode::AfterDuelRound, ChaosImpactMatch::DuelRoundOverSeconds, false);
	UE_LOG(LogChaosImpact, Log, TEXT("Duel round %d to %s (%s): %d - %d"), Duel.Round,
		Duel.RoundWinner == INDEX_NONE ? TEXT("nobody") : Duel.RoundWinner == 0 ? TEXT("side 0") : TEXT("side 1"),
		*StaticEnum<EChaosImpactDuelEnd>()->GetNameStringByValue(static_cast<int64>(How)), Duel.Wins[0], Duel.Wins[1]);
}

void AChaosImpactGameMode::AfterDuelRound()
{
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || Match->Duel.Step != EChaosImpactDuelStep::RoundOver)
	{
		return;
	}
	FChaosImpactDuelState& Duel = Match->Duel;
	const bool bDecided = Duel.Wins[0] >= ChaosImpactMatch::DuelRoundsToWin || Duel.Wins[1] >= ChaosImpactMatch::DuelRoundsToWin
		|| Duel.Round >= ChaosImpactMatch::DuelMaxRounds;
	if (!bDecided)
	{
		StartDuelRound(Duel.Round + 1);
		return;
	}
	Duel.MatchWinner = Duel.Wins[0] > Duel.Wins[1] ? 0 : Duel.Wins[1] > Duel.Wins[0] ? 1 : INDEX_NONE;
	SetDuelStep(EChaosImpactDuelStep::Finished, 0.0f);
	UE_LOG(LogChaosImpact, Log, TEXT("Duel over: %d - %d, %s"), Duel.Wins[0], Duel.Wins[1],
		Duel.MatchWinner == INDEX_NONE ? TEXT("drawn") : Duel.MatchWinner == 0 ? TEXT("side 0 wins") : TEXT("side 1 wins"));
	EndMatch();
}

void AChaosImpactGameMode::ForfeitDuel(const int32 LeavingSide)
{
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || !Match->IsDuel() || Match->Duel.Step == EChaosImpactDuelStep::Finished || (LeavingSide != 0 && LeavingSide != 1))
	{
		return;
	}
	GetWorldTimerManager().ClearTimer(DuelTimer);
	FChaosImpactDuelState& Duel = Match->Duel;
	const int32 Winner = 1 - LeavingSide;
	Duel.Wins[Winner] = FMath::Max(Duel.Wins[Winner], ChaosImpactMatch::DuelRoundsToWin);
	Duel.RoundWinner = Winner;
	Duel.RoundEnd = EChaosImpactDuelEnd::Forfeit;
	Duel.MatchWinner = Winner;
	SetDuelStep(EChaosImpactDuelStep::Finished, 0.0f);
	UE_LOG(LogChaosImpact, Log, TEXT("Duel: side %d left, side %d wins"), LeavingSide, Winner);
	// The results are shown once this frame is over (the leaver is still being removed).
	GetWorldTimerManager().SetTimerForNextTick(this, &AChaosImpactGameMode::EndMatch);
}
