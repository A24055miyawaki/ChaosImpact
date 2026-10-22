#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ChaosImpactSoloTitleWidget.generated.h"

class AChaosImpactSoloTitle;

/**
 * Solo mode's title screen, drawn over the dusk temple (AChaosImpactSoloTitle): the logo with its light, the three
 * files as cards, the question before erasing one. Laid out on a 1600 x 900 plan scaled to the screen; the title
 * uses the same rectangles for the mouse.
 */
UCLASS()
class CHAOSIMPACT_API UChaosImpactSoloTitleWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetTitle(AChaosImpactSoloTitle* InTitle);

	/** The plan's scale and its top-left corner on a screen this size. */
	static float PlanScale(const FVector2D& Size);
	static FVector2D PlanOrigin(const FVector2D& Size);
	/** Rectangles on the screen (widget space): a file's card, its erase button, the question's answers. */
	static FBox2D CardRect(const FVector2D& Size, int32 Index);
	static FBox2D EraseRect(const FVector2D& Size, int32 Index);
	static FBox2D AnswerRect(const FVector2D& Size, bool bYes);

protected:
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	TWeakObjectPtr<AChaosImpactSoloTitle> Title;
};
