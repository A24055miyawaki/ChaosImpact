#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BRHUD.generated.h"

class ABRPlayerController;
class ABRCharacter;
class ABRGameState;
class UTexture2D;
struct FSlateBrush;

/** Everything the battle royale draws over play: vitals, team, minimap, big map, zone timer, kill feed, the landing choice and results. */
UCLASS()
class UBRHUD : public UUserWidget
{
	GENERATED_BODY()

public:
	void Init(ABRPlayerController* InOwner);
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	/** A mouse press, screen position. True if the HUD used it. */
	bool HandleClick(const FVector2D& ScreenPosition);
	bool HandleWheel(float Delta);
	void HandlePadConfirm();
	void MovePadCursor(const FVector2D& Stick, float DeltaSeconds);
	int32 GetHoveredResult() const { return HoveredResult; }

	/** The picture of the island the maps are drawn on (made once). */
	static UTexture2D* GetMapTexture();

private:
	void PaintVitals(const FGeometry& G, FSlateWindowElementList& Elements, int32 Layer, const ABRCharacter* Me, const ABRGameState* State, float S) const;
	void PaintTeam(const FGeometry& G, FSlateWindowElementList& Elements, int32 Layer, const ABRGameState* State, float S) const;
	void PaintTopBar(const FGeometry& G, FSlateWindowElementList& Elements, int32 Layer, const ABRCharacter* Me, const ABRGameState* State, float S, const FVector2D& Size) const;
	void PaintKillFeed(const FGeometry& G, FSlateWindowElementList& Elements, int32 Layer, const ABRGameState* State, float S, const FVector2D& Size) const;
	void PaintPrompt(const FGeometry& G, FSlateWindowElementList& Elements, int32 Layer, const ABRCharacter* Me, const ABRGameState* State, float S, const FVector2D& Size) const;
	void PaintZoneWarning(const FGeometry& G, FSlateWindowElementList& Elements, int32 Layer, const ABRCharacter* Me, const ABRGameState* State, const FVector2D& Size) const;
	void PaintMapBody(const FGeometry& G, FSlateWindowElementList& Elements, int32 Layer, const FVector2D& Min, float Side, const FVector2D& Centre, float Zoom, const ABRCharacter* Me, const ABRGameState* State, bool bLabels, float Rounding) const;
	void PaintMinimap(const FGeometry& G, FSlateWindowElementList& Elements, int32 Layer, const ABRCharacter* Me, const ABRGameState* State, float S) const;
	void PaintBigMap(const FGeometry& G, FSlateWindowElementList& Elements, int32 Layer, const ABRCharacter* Me, const ABRGameState* State, float S, const FVector2D& Size) const;
	void PaintShip(const FGeometry& G, FSlateWindowElementList& Elements, int32 Layer, const ABRCharacter* Me, const ABRGameState* State, float S, const FVector2D& Size) const;
	void PaintResults(const FGeometry& G, FSlateWindowElementList& Elements, int32 Layer, const ABRGameState* State, float S, const FVector2D& Size) const;
	void PaintReticle(const FGeometry& G, FSlateWindowElementList& Elements, int32 Layer, const ABRCharacter* Me, float S) const;
	void PaintThrowGuide(const FGeometry& G, FSlateWindowElementList& Elements, int32 Layer, const ABRCharacter* Me, float S, const FVector2D& Size) const;
	void PaintOverheads(const FGeometry& G, FSlateWindowElementList& Elements, int32 Layer, const ABRCharacter* Me, float S) const;
	void PaintSpectating(const FGeometry& G, FSlateWindowElementList& Elements, int32 Layer, const ABRCharacter* Me, const ABRGameState* State, float S, const FVector2D& Size) const;
	FVector2D WorldToMap(const FVector2D& World, const FVector2D& Min, float Side, const FVector2D& Centre, float Zoom) const;
	FVector2D MapToWorld(const FVector2D& Local, const FVector2D& Min, float Side, const FVector2D& Centre, float Zoom) const;
	void GetBigMapRect(const FVector2D& Size, float S, FVector2D& OutMin, float& OutSide) const;
	void GetResultButtons(const FVector2D& Size, float S, FBox2D OutButtons[2]) const;

	TWeakObjectPtr<ABRPlayerController> Owner;
	mutable FSlateBrush MapBrush;
	float Clock = 0.0f;
	float GhostHealth = 100.0f;
	float GhostArmor = 0.0f;
	float MapZoom = 1.0f;
	FVector2D MapCentre = FVector2D::ZeroVector;
	FVector2D PadCursor = FVector2D::ZeroVector;
	int32 HoveredResult = INDEX_NONE;
	FVector2D LastSize = FVector2D(1600.0, 900.0);
};

/** The battle royale's own title: the island behind, the team size to choose, and the way into a match. */
UCLASS()
class UBRTitleWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void Init(ABRPlayerController* InOwner);
	virtual bool NativeSupportsKeyboardFocus() const override { return true; }
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnFocusReceived(const FGeometry& InGeometry, const FFocusEvent& InFocusEvent) override { return FReply::Handled(); }

private:
	void Choose();
	void Back();
	void StepCharacter(int32 Step);
	void ChooseOnline();
	void PaintOnline(const FGeometry& D, FSlateWindowElementList& Elements, int32 Layer) const;
	FBox2D OnlineRect(int32 Index) const;
	FString OnlineWord() const;
	bool IsGuest() const;
	void UpdatePreview(float DeltaSeconds);
	FBox2D ArrowRect(int32 Index) const;
	int32 HitAt(const FGeometry& Geometry, const FVector2D& ScreenPosition) const;
	FBox2D TileRect(int32 Index) const;
	FBox2D ButtonRect(int32 Index) const;
	TWeakObjectPtr<ABRPlayerController> Owner;
	float Clock = 0.0f;
	/** 0..3 the team sizes, 4 start, 5 how to play, 6 back. */
	int32 Focused = 4;
	int32 TeamChoice = 0;
	bool bHowTo = false;
	bool bOnline = false;
	/** The word's four digits; which of them, or which button, is picked (0..3 digits, 4 make, 5 find, 6 close). */
	int32 Digits[4] = {1, 2, 3, 4};
	int32 OnlineFocus = 4;
	int32 CharacterChoice = 0;
	int32 PreviewShown = INDEX_NONE;
	UPROPERTY() TObjectPtr<class AChaosImpactCharacterPreview> Preview;
	mutable FSlateBrush PreviewBrush;
};
