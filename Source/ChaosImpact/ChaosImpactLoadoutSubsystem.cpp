#include "ChaosImpactLoadoutSubsystem.h"

#include "ChaosImpactCharacterRoster.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/ConfigCacheIni.h"

namespace
{
	const TCHAR* const LoadoutSection = TEXT("ChaosImpact.Loadout");
}

UChaosImpactLoadoutSubsystem* UChaosImpactLoadoutSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UChaosImpactLoadoutSubsystem>() : nullptr;
}

void UChaosImpactLoadoutSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	for (int32 Index = 0; Index < MaxLocalPlayers; ++Index)
	{
		// First run: P1 red, P2 blue, P3 yellow, P4 green.
		Loadouts[Index].Colour = Index % ChaosImpactRoster::ColourCount;
		if (GConfig)
		{
			GConfig->GetInt(LoadoutSection, *FString::Printf(TEXT("Character%d"), Index), Loadouts[Index].Character, GGameUserSettingsIni);
			GConfig->GetInt(LoadoutSection, *FString::Printf(TEXT("Colour%d"), Index), Loadouts[Index].Colour, GGameUserSettingsIni);
		}
		Loadouts[Index].Character = ChaosImpactRoster::ClampIndex(Loadouts[Index].Character);
		Loadouts[Index].Colour = FMath::Clamp(Loadouts[Index].Colour, 0, ChaosImpactRoster::ColourCount - 1);
	}
}

FChaosImpactLoadout UChaosImpactLoadoutSubsystem::GetLoadout(const int32 LocalPlayerIndex) const
{
	return Loadouts[FMath::Clamp(LocalPlayerIndex, 0, MaxLocalPlayers - 1)];
}

void UChaosImpactLoadoutSubsystem::SetLoadout(const int32 LocalPlayerIndex, const FChaosImpactLoadout& Loadout)
{
	if (LocalPlayerIndex < 0 || LocalPlayerIndex >= MaxLocalPlayers)
	{
		return;
	}
	FChaosImpactLoadout& Stored = Loadouts[LocalPlayerIndex];
	Stored.Character = ChaosImpactRoster::ClampIndex(Loadout.Character);
	Stored.Colour = FMath::Clamp(Loadout.Colour, 0, ChaosImpactRoster::ColourCount - 1);
	if (GConfig)
	{
		GConfig->SetInt(LoadoutSection, *FString::Printf(TEXT("Character%d"), LocalPlayerIndex), Stored.Character, GGameUserSettingsIni);
		GConfig->SetInt(LoadoutSection, *FString::Printf(TEXT("Colour%d"), LocalPlayerIndex), Stored.Colour, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}
