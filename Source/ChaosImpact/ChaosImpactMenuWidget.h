#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ChaosImpactScreen.h"
#include "ChaosImpactMenuWidget.generated.h"

class UTexture2D;

/** Native, resolution-independent front end. All hit regions and D-pad focus share one layout. */
UCLASS()
class UChaosImpactMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void ShowScreen(EChaosImpactScreen NewScreen);
	void Navigate(FKey Key);
	void ConfirmSelection();
	void GoBack();
	void RefreshEntries();
	int32 GetSelectedIndex() const { return SelectedIndex; }
	bool HasLogo() const { return LogoTexture != nullptr; }
	EChaosImpactScreen GetScreen() const { return Screen; }

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnAnalogValueChanged(const FGeometry& InGeometry,
		const FAnalogInputEvent& InAnalogEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;

private:
	struct FMenuEntry
	{
		FSlateRect Rect;
		FString Title;
		FString Detail;
		FString Number;
		FLinearColor Accent;
		bool bDisabled = false;
	};

	TArray<FMenuEntry> Entries;
	/** 0..1 eased selection state per entry, advanced in NativeTick. */
	TArray<float> SelectBlend;
	/** Per-slot join tracking so a newly connected device plays its entry animation once. */
	bool bSlotJoined[4] = {};
	double SlotJoinedAt[4] = {};
	EChaosImpactScreen Screen = EChaosImpactScreen::Title;
	int32 SelectedIndex = 0;
	int32 PressedIndex = INDEX_NONE;
	double ScreenStartedAt = 0.0;
	float AnimationSeconds = 0.0f;
	double LastAnalogNavigationAt = 0.0;
	void BuildEntries();
	int32 HitTestEntry(const FGeometry& Geometry, const FVector2D& ScreenPosition) const;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> LogoTexture;
	FSlateBrush LogoBrush;
};
