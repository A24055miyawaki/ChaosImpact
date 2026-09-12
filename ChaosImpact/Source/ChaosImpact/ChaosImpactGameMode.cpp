// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosImpactGameMode.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactCPUController.h"

#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

AChaosImpactGameMode::AChaosImpactGameMode()
{
	// stub
}

void AChaosImpactGameMode::BeginPlay()
{
	Super::BeginPlay();
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

	const int32 DesiredPlayers = FMath::Clamp(FCString::Atoi(
		GetWorld()->URL.GetOption(TEXT("CILocalPlayers="), TEXT("1"))), 1, 4);
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
	}

	if (GetWorld()->URL.HasOption(TEXT("CICPU=1")))
	{
		SpawnTrainingCPU(Anchor, Facing);
	}
}

void AChaosImpactGameMode::SpawnTrainingCPU(const FVector& Anchor, const FRotator& Facing)
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

	const FVector CPULocation = Anchor + FVector(620.0f, 0.0f, 0.0f);
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
