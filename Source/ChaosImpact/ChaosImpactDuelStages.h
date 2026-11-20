#pragma once

#include "CoreMinimal.h"
#include "ChaosImpactDuelStage.h"
#include "ChaosImpactDuelStages.generated.h"

class UPointLightComponent;
class URotatingMovementComponent;

/**
 * The 1v1 stages beside カオスアリーナ (2026-11-16). Each the same square (DuelHalfExtent) with the starts in the same
 * places (red -X, blue +X), mirrored across the middle line so neither side has anything the other lacks, and lit from
 * almost overhead so both ends see the same light. Their shapes differ:
 *  - 天空神殿 (1): the solo temple's stone over the sea of clouds: one open square with nothing standing on it (the
 *    user's wish), its altar circle and paths laid in the paving; balls thrown off its edge fall into the clouds.
 *  - 夕焼けコート (2): a street court at sunset. A ring of low walls in a diamond round a fountain in the middle, open at
 *    its four points; planters in the corners; the city and palms beyond the fence.
 *  - 夜の屋上 (3): a factory roof at night. Two zigzag ducts run the length of the roof and split it into three lanes,
 *    crossing where they peak; air units either side of the helipad in the middle; the city's lights all round.
 */
UCLASS(meta=(DisplayName="Chaos Impact Duel Stage: Temple"))
class AChaosImpactDuelTempleStage : public AChaosImpactDuelStage
{
	GENERATED_BODY()

public:
	AChaosImpactDuelTempleStage();
	virtual int32 GetDuelStageIndex() const override { return 1; }
	/** The middle: the altar's radius, where the halves begin, and where the side walkways begin. */
	static constexpr float AltarRadius = 460.0f;
	static constexpr float HalfStart = 900.0f;
	static constexpr float WalkwayStart = 1250.0f;
	/** Half the width of the bridges along the middle line (to each half) and across it (to the walkways). */
	static constexpr float BridgeHalfWidth = 160.0f;
	static constexpr float CrossHalfWidth = 150.0f;

protected:
	virtual FDuelLook GetArenaLook() const override;
	virtual FDuelSky GetSky() const override;

	UPROPERTY(VisibleAnywhere, Category="Chaos Impact|Duel Stage")
	TObjectPtr<USceneComponent> RingRoot;
	UPROPERTY(VisibleAnywhere, Category="Chaos Impact|Duel Stage")
	TObjectPtr<URotatingMovementComponent> RingTurn;
};

UCLASS(meta=(DisplayName="Chaos Impact Duel Stage: Court"))
class AChaosImpactDuelCourtStage : public AChaosImpactDuelStage
{
	GENERATED_BODY()

public:
	AChaosImpactDuelCourtStage();
	virtual int32 GetDuelStageIndex() const override { return 2; }
	/** The diamond's points (on the middle line and on the line between the starts), and the gap left at each. */
	static constexpr float DiamondReach = 950.0f;
	static constexpr float DiamondGap = 170.0f;

protected:
	virtual FDuelLook GetArenaLook() const override;
	virtual FDuelSky GetSky() const override;
};

UCLASS(meta=(DisplayName="Chaos Impact Duel Stage: Rooftop"))
class AChaosImpactDuelRooftopStage : public AChaosImpactDuelStage
{
	GENERATED_BODY()

public:
	AChaosImpactDuelRooftopStage();
	virtual int32 GetDuelStageIndex() const override { return 3; }

protected:
	virtual FDuelLook GetArenaLook() const override;
	virtual FDuelSky GetSky() const override;
};
