#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ChaosImpactWorldMapWidget.generated.h"

class AChaosImpactWorldMap;

/**
 * The world map's HUD, painted like the battle HUD (shards, outlined letters): the map's name and how much of it is
 * cleared, the card of the stage the player stands on (its number, name, region, a line about it, what the confirm
 * button does), the call-out while the fog clears, and the controls.
 */
UCLASS()
class CHAOSIMPACT_API UChaosImpactWorldMapWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetMap(AChaosImpactWorldMap* InMap) { Map = InMap; }
	void SetCardNode(const int32 Node) { CardNode = Node; }

protected:
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	TWeakObjectPtr<AChaosImpactWorldMap> Map;
	int32 CardNode = 0;
};
