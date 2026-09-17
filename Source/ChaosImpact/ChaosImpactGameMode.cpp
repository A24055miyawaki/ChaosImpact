// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosImpactGameMode.h"
#include "ChaosImpact.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactBallSpawner.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactHazardZone.h"
#include "ChaosImpactSessionSubsystem.h"
#include "ChaosImpactTrainingTarget.h"
#include "ChaosImpactVersusStage.h"
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
#include "JoyShockBlueprintLibrary.h"
#include "JoyShockTypes.h"
#include "TimerManager.h"

AChaosImpactGameMode::AChaosImpactGameMode()
{
	GameStateClass = AChaosImpactGameState::StaticClass();
	PlayerStateClass = AChaosImpactPlayerState::StaticClass();
	VersusStageClass = AChaosImpactVersusStage::StaticClass();
}

void AChaosImpactGameMode::BeginPlay()
{
	Super::BeginPlay();
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
		GetWorldTimerManager().SetTimer(LobbyReadyTimer, this, &AChaosImpactGameMode::UpdateLobbyReady, 0.25f, true);
		FParse::Value(FCommandLine::Get(), TEXT("CIReadyWaitSeconds="), ReadyWaitSecondsOverride);
		FString AutoLobby;
		if (FParse::Value(FCommandLine::Get(), TEXT("CIAutoLobby="), AutoLobby))
		{
			TArray<FString> Parts;
			AutoLobby.ParseIntoArray(Parts, TEXT(":"));
			DevAutoLobbyRules.TeamCount = Parts.IsValidIndex(0) ? FCString::Atoi(*Parts[0]) : 0;
			DevAutoLobbyRules.CPUCount = Parts.IsValidIndex(1) ? FCString::Atoi(*Parts[1]) : 0;
			const float Delay = Parts.IsValidIndex(2) ? FMath::Max(1.0f, FCString::Atof(*Parts[2])) : 20.0f;
			GetWorldTimerManager().SetTimer(DevAutoLobbyTimer, this, &AChaosImpactGameMode::RunDevAutoLobby, Delay, false);
		}
		if (FParse::Value(FCommandLine::Get(), TEXT("CIReserveSlots="), DevReservedSlots))
		{
			DevReservedSlots = FMath::Clamp(DevReservedSlots, 0, AChaosImpactGameState::MaxMembers - 1);
		}
		UpdateRoomMemberCount();
		GetWorldTimerManager().SetTimer(PingMirrorTimer, this,
			&AChaosImpactGameMode::MirrorSplitscreenPings, 0.25f, true);

		float AutoStartSeconds = 0.0f;
		if (FParse::Value(FCommandLine::Get(), TEXT("CIAutoStartMatch="), AutoStartSeconds) && AutoStartSeconds > 0.0f)
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
			const float Delay = Parts.IsValidIndex(2) ? FMath::Max(1.0f, FCString::Atof(*Parts[2])) : 20.0f;
			GetWorldTimerManager().SetTimer(DevAutoVersusTimer, this, &AChaosImpactGameMode::RunDevAutoVersus, Delay, false);
		}
	}
	float OverrideSeconds = 0.0f;
	if (FParse::Value(FCommandLine::Get(), TEXT("CIMatchSeconds="), OverrideSeconds) && OverrideSeconds > 1.0f)
	{
		MatchDurationOverride = OverrideSeconds;
	}
	bLocalMatchWorld = !IsOnlineRoom() && GetWorld() && GetWorld()->URL.HasOption(TEXT("CIMatch=1"));
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
	ApplyLocalControllerAssignments(GetWorld(), DesiredPlayers, KeyboardPlayerIndex);
}

void AChaosImpactGameMode::ApplyLocalControllerAssignments(UWorld* World,
	const int32 DesiredPlayers, const int32 KeyboardPlayerIndex)
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
		if (const TCHAR* DeviceValue = World->URL.GetOption(*DeviceOption, nullptr))
		{
			const int32 RequestedInputDeviceId = FCString::Atoi(DeviceValue);
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
	const int32 KeyboardPlayerIndex = KeyboardOption
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
	// The engine adds SplitscreenCount=2 when a machine already inside sends its second player;
	// that place was reserved when the machine joined, so only the hard limit applies.
	const bool bSecondPlayerOfMachine = UGameplayStatics::GetIntOption(Options, TEXT("SplitscreenCount"), 1) >= 2;
	const int32 Arriving = bSecondPlayerOfMachine
		? 1 : FMath::Clamp(UGameplayStatics::GetIntOption(Options, TEXT("CIPlayers"), 1), 1, 2);
	// A machine coming back after a drop the host has not noticed yet takes over its own old places.
	const int32 StalePlaces = bSecondPlayerOfMachine
		? 0 : CountMembersOfMachine(UGameplayStatics::ParseOption(Options, TEXT("CIMachine")));
	const int32 Occupied = (bSecondPlayerOfMachine
		? RoomState->CountHumanMembers() + DevReservedSlots
		: RoomState->CountHumanMembers() + GetReservedRoomSlots() + DevReservedSlots) - StalePlaces;
	// The client's session subsystem turns these codes into a readable notice.
	if (Occupied + Arriving > AChaosImpactGameState::MaxMembers)
	{
		ErrorMessage = TEXT("CIFULL");
	}
	else if (!bSecondPlayerOfMachine
		&& ((RoomState->bRecruitmentClosed && StalePlaces == 0) || RoomState->Phase != EChaosImpactOnlinePhase::Lobby))
	{
		ErrorMessage = TEXT("CICLOSED");
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
	if (Member && UGameplayStatics::GetIntOption(Options, TEXT("SplitscreenCount"), 1) < 2)
	{
		Member->ExpectedMachinePlayers = FMath::Clamp(UGameplayStatics::GetIntOption(Options, TEXT("CIPlayers"), 1), 1, 2);
		Member->MachinePlayersJoined = 1;
		Member->MachineLoginAt = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		Member->MachineToken = UGameplayStatics::ParseOption(Options, TEXT("CIMachine"));
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
	UpdateRoomMemberCount();
}

void AChaosImpactGameMode::Logout(AController* Exiting)
{
	Super::Logout(Exiting);
	if (IsOnlineRoom())
	{
		GetWorldTimerManager().SetTimerForNextTick(this, &AChaosImpactGameMode::UpdateRoomMemberCount);
	}
}

APawn* AChaosImpactGameMode::SpawnDefaultPawnFor_Implementation(AController* NewPlayer, AActor* StartSpot)
{
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
	if (!IsOnlineRoom() || !Match || Match->bVersusMatch || Match->Phase != EChaosImpactOnlinePhase::Lobby)
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
	Match->ForceNetUpdate();
	UE_LOG(LogChaosImpact, Log, TEXT("Lobby rules decided: %d min, teams=%d, CPUs=%d; starts by itself in %.0f s"),
		Match->Rules.Minutes, Match->Rules.TeamCount, Match->Rules.CPUCount, GetReadyWaitSeconds());
}

void AChaosImpactGameMode::SetMemberReady(AChaosImpactPlayerState* Member, const bool bReady)
{
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || !Member || Member->IsABot() || !Match->bRulesDecided || Match->bVersusMatch
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
	if (!IsOnlineRoom() || !Match || !Match->bRulesDecided || Match->bVersusMatch
		|| Match->Phase != EChaosImpactOnlinePhase::Lobby)
	{
		return;
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
	if (Match->CountMachines() < 2)
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

void AChaosImpactGameMode::EnsureVersusStage()
{
	if (IsValid(VersusStage) || !GetWorld())
	{
		return;
	}
	for (TActorIterator<AChaosImpactVersusStage> It(GetWorld()); It; ++It)
	{
		VersusStage = *It;
		return;
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
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	UE_LOG(LogChaosImpact, Log, TEXT("VS stage spawned from %s"),
		*GetNameSafe(VersusStageClass ? VersusStageClass.Get() : AChaosImpactVersusStage::StaticClass()));
	VersusStage = GetWorld()->SpawnActor<AChaosImpactVersusStage>(
		VersusStageClass ? VersusStageClass.Get() : AChaosImpactVersusStage::StaticClass(),
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
	const FChaosImpactMatchRules Rules = ChaosImpactMatch::Sanitize(InRules, Match->CountHumanMembers());
	EnsureVersusStage();
	if (!VersusStage)
	{
		return;
	}
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
	Match->StageCenter = VersusStage->GetCenter();
	for (TActorIterator<AChaosImpactTrainingTarget> It(GetWorld()); It; ++It)
	{
		It->SetTrainingEnabled(false);
	}
	SyncTrainingCPUCount(Rules.CPUCount, VersusStage->GetCenter() + FVector(0.0f, 0.0f, 110.0f), FRotator::ZeroRotator);

	// Humans start spread across the teams in join order; CPUs are placed once the teams are confirmed.
	const TArray<AChaosImpactPlayerState*> Humans = Match->GetCompetitors(false);
	for (int32 Index = 0; Index < Humans.Num(); ++Index)
	{
		Humans[Index]->TeamIndex = Rules.IsTeamBattle() ? Index % Rules.TeamCount : INDEX_NONE;
		// Local players have no room name; use the one shown above them (P1, P2 or the title name).
		if (const AChaosImpactCharacter* Character = Cast<AChaosImpactCharacter>(Humans[Index]->GetPawn());
			Character && !IsOnlineRoom())
		{
			Humans[Index]->SetPlayerName(Character->GetOverheadDisplayName());
		}
	}
	for (AChaosImpactPlayerState* Member : Match->GetCompetitors(true))
	{
		Member->Points = 0;
		Member->Knockouts = 0;
		if (Member->IsABot())
		{
			Member->TeamIndex = INDEX_NONE;
		}
	}
	UE_LOG(LogChaosImpact, Log, TEXT("VS match configured: %d min, teams=%d, humans=%d, CPUs=%d"),
		Rules.Minutes, Rules.TeamCount, Humans.Num(), Rules.CPUCount);
	if (Rules.IsTeamBattle())
	{
		SetMatchPhase(EChaosImpactOnlinePhase::TeamSelect, 0.0f);
	}
	else
	{
		StartMatchIntro();
	}
}

void AChaosImpactGameMode::ChangeMemberTeam(AChaosImpactPlayerState* Member, const int32 Direction)
{
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || !Member || Member->IsABot() || Direction == 0 || !Match->IsTeamBattle()
		|| Match->Phase != EChaosImpactOnlinePhase::TeamSelect)
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
			return;
		}
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
		Member->Points = 0;
		Member->Knockouts = 0;
		Member->ForceNetUpdate();
	}

	ClearMatchBalls();
	DestroyStageBallSpawners();
	for (const FVector& Point : VersusStage->GetBallPoints())
	{
		const FTransform SpawnTransform(FRotator::ZeroRotator, Point + FVector(0.0f, 0.0f, 3.0f));
		if (AChaosImpactBallSpawner* Spawner = GetWorld()->SpawnActorDeferred<AChaosImpactBallSpawner>(
			AChaosImpactBallSpawner::StaticClass(), SpawnTransform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
		{
			Spawner->SetAlwaysActive(true);
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
	for (int32 Index = 0; Index < Competitors.Num() && !SpawnPoints.IsEmpty(); ++Index)
	{
		AChaosImpactCharacter* Character = Cast<AChaosImpactCharacter>(Competitors[Index]->GetPawn());
		if (!Character)
		{
			continue;
		}
		const FVector Floor = SpawnPoints[Index % SpawnPoints.Num()];
		const FVector Location = Floor
			+ FVector(0.0f, 0.0f, Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 4.0f);
		Character->ResetForOnlineMatch(Location, FRotator(0.0f, (Match->StageCenter - Floor).Rotation().Yaw, 0.0f));
	}
	// Ready? waits until every machine reports that its opening has played (ReportIntroFinished).
	Match->ReadyStartedAt = 0.0;
	IntroFinishedMembers.Reset();
	SetMatchPhase(EChaosImpactOnlinePhase::Intro, 0.0f);
	GetWorldTimerManager().SetTimer(MatchPhaseTimer, this, &AChaosImpactGameMode::StartReadyCountdown,
		ChaosImpactMatch::FlyoverSeconds + ChaosImpactMatch::DiveSeconds + ChaosImpactMatch::IntroWaitTimeoutSeconds, false);
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
	const float Seconds = MatchDurationOverride > 0.0f ? MatchDurationOverride : Match->Rules.Minutes * 60.0f;
	SetMatchPhase(EChaosImpactOnlinePhase::Match, Seconds);
	GetWorldTimerManager().SetTimer(MatchPhaseTimer, this, &AChaosImpactGameMode::EndMatch, Seconds, false);
	UE_LOG(LogChaosImpact, Log, TEXT("VS match GO (%.0f s)"), Seconds);
}

void AChaosImpactGameMode::EndMatch()
{
	SetMatchPhase(EChaosImpactOnlinePhase::Results, ChaosImpactMatch::ResultsSeconds);
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
	SyncTrainingCPUCount(0, FVector::ZeroVector, FRotator::ZeroRotator);
	DestroyStageBallSpawners();
	ClearMatchBalls();
	Match->bVersusMatch = false;
	SetMatchPhase(EChaosImpactOnlinePhase::Lobby, 0.0f);
	// The same group plays on: recruitment stays as it was, the last rules stand, and everyone readies up again.
	ClearReady();
	if (Match->bRulesDecided)
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
	for (TActorIterator<AChaosImpactBall> It(GetWorld()); It; ++It)
	{
		It->Destroy();
	}
	for (TActorIterator<AChaosImpactHazardZone> It(GetWorld()); It; ++It)
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

bool AChaosImpactGameMode::ChooseMatchRespawn(const AActor* Character, FVector& OutLocation, FRotator& OutRotation) const
{
	const AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || !Match->bVersusMatch || Match->Phase != EChaosImpactOnlinePhase::Match
		|| !IsValid(VersusStage) || !GetWorld())
	{
		return false;
	}
	// Prefer the points farthest from living opponents, with a little randomness among the best few.
	TArray<TPair<float, FVector>> Candidates;
	for (const FVector& Point : VersusStage->GetSpawnPoints())
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
	TArray<AChaosImpactCPUController*> CPUControllers;
	for (TActorIterator<AChaosImpactCPUController> It(GetWorld()); It; ++It)
	{
		CPUControllers.Add(*It);
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

	const FVector CPUOffsets[] =
	{
		FVector(-680.0f, -390.0f, 0.0f),
		FVector(-680.0f, 390.0f, 0.0f),
		FVector(620.0f, -360.0f, 0.0f),
		FVector(620.0f, 360.0f, 0.0f)
	};
	const FVector CPULocation = Anchor + CPUOffsets[FMath::Clamp(CPUIndex, 0, 3)];
	const FRotator CPURotation = (Anchor - CPULocation).Rotation();
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
	}
}
