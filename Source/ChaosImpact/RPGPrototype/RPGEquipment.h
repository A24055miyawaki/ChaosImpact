#pragma once

#include "CoreMinimal.h"
#include "ChaosImpactBallTypes.h"
#include "RPGEquipment.generated.h"

UENUM(BlueprintType)
enum class ERPGItemKind : uint8 { Ball, LifeBadge, MobilityBadge, TechniqueBadge, Material, Glove, Shoes, Supporter, Pouch };

USTRUCT(BlueprintType)
struct FRPGAffix
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName Key;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float Roll = 0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 Rank = 1;
    float Value() const;
};

/** One authoritative catalog drives rolling, gameplay, and the bag descriptions. */
struct FRPGModDefinition
{
    FName Key;
    FString Name;
    FString Description;
    float Min = 0, Max = 0;
    bool bTransform = false;
};

namespace RPGEquipment
{
    const TArray<FRPGModDefinition>& Tunes(EChaosImpactBallType Type);
    const TArray<FRPGModDefinition>& Intrinsics(EChaosImpactBallType Type);
    const TArray<FRPGModDefinition>& Badges(ERPGItemKind Kind);
    const TArray<FRPGModDefinition>& BadgeIntrinsics(ERPGItemKind Kind);
    const TArray<FRPGModDefinition>& Gear(ERPGItemKind Kind);
    bool IsGear(ERPGItemKind Kind);
    const FRPGModDefinition* Find(FName Key);
    bool IsCount(FName Key);
    bool IsCommonTune(FName Key);
    float RollValue(const FRPGModDefinition& Definition,FRandomStream* Random=nullptr);
    /** Moves a saved roll into the current range after the catalog was re-tuned (keeps the old relative position when it can). */
    void MigrateRoll(FRPGAffix& Affix);
    /** Every definition once, with where it can appear. For the RPGDumpCatalog console command and the docs. */
    FString CatalogCsv();
    FString Describe(const FRPGAffix& Affix);
    FString KindName(ERPGItemKind Kind);
    FString BaseEffectDescription(EChaosImpactBallType Type);
    const TArray<EChaosImpactBallType>& BallRoster();
}
