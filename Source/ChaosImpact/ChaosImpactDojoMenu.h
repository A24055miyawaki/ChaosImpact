#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ChaosImpactDojoMenu.generated.h"

class AChaosImpactPlayerController;
class FSlateWindowElementList;
struct FAnalogInputEvent;
struct FGeometry;
struct FKeyEvent;

/**
 * 訓練場's lesson list (the menu's DojoSelect screen): the four groups as tabs with how many are cleared, the group's
 * lessons down the left with their checks, and on the right the selected lesson's moving picture, what it teaches and
 * its task. Start one with the confirm button or a click; back leaves the dojo for the mode select.
 */
UCLASS()
class UChaosImpactDojoMenu : public UObject
{
	GENERATED_BODY()

public:
	void Open(AChaosImpactPlayerController* InController);
	void Close() { bOpen = false; }
	bool IsOpen() const { return bOpen; }
	int32 Paint(const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer) const;
	bool HandleKeyDown(const FKeyEvent& Event);
	bool HandleAnalog(const FAnalogInputEvent& Event);
	bool HandleClick(const FVector2D& DesignPoint);
	void HandleMouseMove(const FVector2D& DesignPoint);

	// For tests and the screen's own input.
	void SetTab(int32 Tab);
	void MoveSelection(int32 Rows);
	void StartSelected();
	void Leave();
	int32 GetTab() const { return Tab; }
	int32 GetSelected() const { return Selected; }
	/** Shown as a demo (the 訓練場 invitation): silent, its preview from this player rather than the dojo's. */
	void SetDemo(class UChaosImpactDojoVideo* Video) { DemoVideo = Video; }
	/** The clip of the lesson picked now (its first slide's). */
	FString GetSelectedClip() const;

private:
	FBox2D TabRect(int32 Index) const;
	FBox2D RowRect(int32 Row) const;
	FBox2D StartRect() const;
	FBox2D BackRect() const;
	int32 FirstVisible() const;
	/** The dojo told which lesson is picked (its clip plays in the preview). */
	void TellPreview() const;

	TWeakObjectPtr<AChaosImpactPlayerController> Controller;
	UPROPERTY(Transient)
	TObjectPtr<class UChaosImpactDojoVideo> DemoVideo;
	bool bOpen = false;
	int32 Tab = 0;
	int32 Selected = 0;
	double OpenedAt = 0.0;
	double MovedAt = 0.0;
	double StickAt = 0.0;
	FVector2D Mouse = FVector2D(-1.0, -1.0);
	/** Where the highlight and the tab underline are drawn now (they glide to the choice). */
	mutable float SelShown = -1.0f;
	mutable float TabShown = -1.0f;
	mutable double LastPaint = 0.0;
};
