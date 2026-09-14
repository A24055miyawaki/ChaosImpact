// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/World.h"

/** Main log category used across the project */
DECLARE_LOG_CATEGORY_EXTERN(LogChaosImpact, Log, All);

namespace ChaosImpact
{
	/**
	 * True for the training arena. A client connected to an online room does not receive the
	 * host's CITraining URL option, but every online room is hosted in the training arena.
	 */
	inline bool IsTrainingWorld(const UWorld* World)
	{
		return World && (World->URL.HasOption(TEXT("CITraining=1")) || World->GetNetMode() == NM_Client);
	}
}