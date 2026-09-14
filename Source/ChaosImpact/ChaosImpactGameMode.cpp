// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosImpactGameMode.h"
#include "ChaosImpact.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactSessionSubsystem.h"
#include "ChaosImpactTrainingTarget.h"

#include "Engine/GameInstance.h"
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
}

void AChaosImpactGameMode::BeginPlay()
{
	Super::BeginPlay();
	if (AChaosImpactGameState* RoomState = GetGameState<AChaosImpactGameState>(); RoomState && IsOnlineRoom())
	{
		RoomState->bOnlineRoom = true;
		RoomState->Phase = EChaosImpactOnlinePhase::Lobby;
		if (const UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this))
		{
			RoomState->RoomPassword = Sessions->GetPassword();
		}
		UpdateRoomMemberCount();

		float OverrideSeconds = 0.0f;
		if (FParse::Value(FCommandLine::Get(), TEXT("CIMatchSeconds="), OverrideSeconds) && OverrideSeconds > 1.0f)
		{
			MatchDuration = OverrideSeconds;
		}
		float AutoStartSeconds = 0.0f;
		if (FParse::Value(FCommandLine::Get(), TEXT("CIAutoStartMatch="), AutoStartSeconds) && AutoStartSeconds > 0.0f)
		{
			GetWorldTimerManager().SetTimer(AutoStartMatchTimer, this,
				&AChaosImpactGameMode::CloseRecruitment, AutoStartSeconds, false);
		}
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

	const int32 CPUCount = LiveDesiredCPUCount != INDEX_NONE
		? FMath::Clamp(LiveDesiredCPUCount, 0, 4)
		: FMath::Clamp(FCString::Atoi(GetWorld()->URL.GetOption(TEXT("CICPUCount="),
			GetWorld()->URL.HasOption(TEXT("CICPU=1")) ? TEXT("1") : TEXT("0"))), 0, 4);
	SyncTrainingCPUCount(CPUCount, Anchor, Facing);
}

void AChaosImpactGameMode::ApplyTrainingControllerAssignments(
	const int32 DesiredPlayers, const int32 KeyboardPlayerIndex)
{
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance || !GetWorld())
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
		if (const TCHAR* DeviceValue = GetWorld()->URL.GetOption(*DeviceOption, nullptr))
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

		APlayerController* Controller = LocalPlayer->GetPlayerController(GetWorld());
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
	// The client's session subsystem turns these codes into a readable notice.
	if (RoomState->PlayerArray.Num() >= AChaosImpactGameState::MaxMembers)
	{
		ErrorMessage = TEXT("CIFULL");
	}
	else if (RoomState->bRecruitmentClosed || RoomState->Phase != EChaosImpactOnlinePhase::Lobby)
	{
		ErrorMessage = TEXT("CICLOSED");
	}
}

void AChaosImpactGameMode::PostLogin(APlayerController* NewPlayer)
{
	if (AChaosImpactPlayerState* Member = NewPlayer ? NewPlayer->GetPlayerState<AChaosImpactPlayerState>() : nullptr)
	{
		Member->bRoomHost = NewPlayer->IsLocalController();
		Member->JoinOrder = Member->bRoomHost ? 0 : NextJoinOrder++;
	}
	Super::PostLogin(NewPlayer);
	if (!IsOnlineRoom())
	{
		return;
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
		Sessions->SetMemberCount(RoomState->PlayerArray.Num());
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

void AChaosImpactGameMode::StartOnlineMatch()
{
	AChaosImpactGameState* RoomState = GetGameState<AChaosImpactGameState>();
	if (!IsOnlineRoom() || !RoomState || RoomState->Phase != EChaosImpactOnlinePhase::Lobby)
	{
		return;
	}
	if (UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this))
	{
		Sessions->SetRecruitmentOpen(false);
	}
	RoomState->Phase = EChaosImpactOnlinePhase::Countdown;
	RoomState->PhaseEndsAt = RoomState->GetServerWorldTimeSeconds() + MatchCountdownSeconds;
	GetWorldTimerManager().SetTimer(OnlinePhaseTimer, this,
		&AChaosImpactGameMode::BeginOnlineMatch, MatchCountdownSeconds, false);
}

void AChaosImpactGameMode::BeginOnlineMatch()
{
	AChaosImpactGameState* RoomState = GetGameState<AChaosImpactGameState>();
	if (!RoomState)
	{
		return;
	}
	SyncTrainingCPUCount(0, FVector::ZeroVector, FRotator::ZeroRotator);
	for (TActorIterator<AChaosImpactTrainingTarget> It(GetWorld()); It; ++It)
	{
		It->SetTrainingEnabled(false);
	}

	const FVector Anchor = GetRoomAnchor();
	const TArray<AChaosImpactPlayerState*> Members = RoomState->GetMembersInJoinOrder();
	for (int32 Index = 0; Index < Members.Num(); ++Index)
	{
		AChaosImpactPlayerState* Member = Members[Index];
		Member->Knockouts = 0;
		AChaosImpactCharacter* Character = Cast<AChaosImpactCharacter>(Member->GetPawn());
		if (!Character)
		{
			continue;
		}
		const float Angle = 360.0f * Index / FMath::Max(Members.Num(), 2);
		const FVector Location = Anchor + FVector(1100.0f, 0.0f, 0.0f).RotateAngleAxis(Angle, FVector::UpVector);
		const FRotator Facing(0.0f, (Anchor - Location).Rotation().Yaw, 0.0f);
		Character->ResetForOnlineMatch(Location, Facing);
	}
	RoomState->Phase = EChaosImpactOnlinePhase::Match;
	RoomState->PhaseEndsAt = RoomState->GetServerWorldTimeSeconds() + MatchDuration;
	GetWorldTimerManager().SetTimer(OnlinePhaseTimer, this,
		&AChaosImpactGameMode::EndOnlineMatch, MatchDuration, false);
}

void AChaosImpactGameMode::EndOnlineMatch()
{
	AChaosImpactGameState* RoomState = GetGameState<AChaosImpactGameState>();
	if (!RoomState)
	{
		return;
	}
	RoomState->Phase = EChaosImpactOnlinePhase::Results;
	RoomState->PhaseEndsAt = RoomState->GetServerWorldTimeSeconds() + ResultsSeconds;
	GetWorldTimerManager().SetTimer(OnlinePhaseTimer, this,
		&AChaosImpactGameMode::ReturnToOnlineLobby, ResultsSeconds, false);
}

void AChaosImpactGameMode::ReturnToOnlineLobby()
{
	AChaosImpactGameState* RoomState = GetGameState<AChaosImpactGameState>();
	if (!RoomState)
	{
		return;
	}
	RoomState->Phase = EChaosImpactOnlinePhase::Lobby;
	RoomState->PhaseEndsAt = 0.0;
	for (TActorIterator<AChaosImpactTrainingTarget> It(GetWorld()); It; ++It)
	{
		It->SetTrainingEnabled(true);
	}
	if (UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this))
	{
		Sessions->SetRecruitmentOpen(true);
	}
}

void AChaosImpactGameMode::RegisterKnockout(AController* Killer)
{
	const AChaosImpactGameState* RoomState = GetGameState<AChaosImpactGameState>();
	if (!RoomState || RoomState->Phase != EChaosImpactOnlinePhase::Match || !Killer)
	{
		return;
	}
	if (AChaosImpactPlayerState* Member = Killer->GetPlayerState<AChaosImpactPlayerState>())
	{
		++Member->Knockouts;
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
	}
}
