#pragma once
#include "CoreMinimal.h"
#include "RPGAdventureTypes.generated.h"

USTRUCT()
struct FRPGAdventureProgress
{
    GENERATED_BODY()
    /** 0 available, 1 accepted, 2 ready to report, 3 rewarded. */
    UPROPERTY(SaveGame) int32 Stage=0;
    UPROPERTY(SaveGame) int32 Count=0;
    UPROPERTY(SaveGame) int32 Flags=0;
    /** A quest that starts from something found in the field (not a board or a person) has been found. */
    UPROPERTY(SaveGame) bool bDiscovered=false;
};

UENUM(BlueprintType)
enum class ERPGAdventureNodeKind : uint8 { Board, Supply, Survey, DungeonEntrance };
