#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ChaosImpactMatchAnnouncerWidget.generated.h"

class UChaosImpactResultsView;

/**
 * VS call-outs that belong to the whole screen rather than to one player: Ready?, GO!, the final
 * countdown and FINISH. Added to the full viewport once per machine, so split screen shows them a single time.
 */
UCLASS()
class UChaosImpactMatchAnnouncerWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** The VS results this machine shows (podium, numbers, awards). */
	UChaosImpactResultsView* GetResultsView() const { return ResultsView; }

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UChaosImpactResultsView> ResultsView;

	/** The call-outs' sounds (the countdown's ticks, GO!, the last minute, FINISH, the podium), as each arrives. */
	void UpdateSounds();
	int32 SoundPhase = -1;
	int32 SoundSecond = MAX_int32;
	double SoundReadyAt = 0.0;
	bool bSoundMinuteCalled = false;
	bool bSoundPodiumShown = false;
};
