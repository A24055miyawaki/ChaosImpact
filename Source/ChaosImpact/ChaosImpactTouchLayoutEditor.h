#pragma once

#include "CoreMinimal.h"
#include "ChaosImpactTouchControls.h"
#include "UObject/Object.h"
#include "ChaosImpactTouchLayoutEditor.generated.h"

class AChaosImpactPlayerController;
class FSlateWindowElementList;
struct FGeometry;
struct FKeyEvent;

/**
 * スマホ版 設定 → ボタン・画面の配置 (2026-11-17): the touch controls and every piece of the battle HUD moved (dragged)
 * and sized, for each kind of play (タブ: VS, 1対1, トレーニング, ソロ, トーナメント, オンライン, 訓練場), over a still picture of a
 * match (never the game itself: it can be opened from any menu, and nothing plays under it). 完了 keeps every kind changed,
 * やめる leaves them all as they were. Laid out on the whole screen (its own pixels, landscape or upright), as play is.
 */
UCLASS()
class UChaosImpactTouchLayoutEditor : public UObject
{
	GENERATED_BODY()
public:
	/** Opened for a nickname (Profile), on a kind of play to start on (its key, ChaosImpactSettings::GetTouchModeKey). */
	void Open(AChaosImpactPlayerController* InController, int32 InProfile, const FString& StartMode);
	void Close(bool bKeep);
	bool IsOpen() const { return bOpen; }
	/** Drawn over the whole screen (Geometry: the screen, its local space the screen's own units). */
	int32 Paint(const FGeometry& Geometry, FSlateWindowElementList& Elements, int32 Layer) const;
	/** A finger (or the mouse) down, moved, up: the screen's local point. */
	void HandleDown(const FVector2D& Point, const FVector2D& ScreenSize);
	void HandleMove(const FVector2D& Point, const FVector2D& ScreenSize);
	void HandleUp(const FVector2D& Point, const FVector2D& ScreenSize);
	bool HandleKey(const FKeyEvent& Event);

	/** The kinds of play, in the tabs' order. */
	static const TArray<FString>& GetModes();
	/**
	 * The editor's own buttons, each a box on a screen of Size (empty: not shown now). Out of the way of what is being
	 * placed: a small bar (ModeMenu: the kind of play ▾, Cancel, Done) that can be dragged by its grip; the picked-up
	 * one's own row next to it (Smaller, Bigger, ResetOne, Hide); the kinds of play only when ModeMenu opens them (with
	 * ResetAll, CopyToAll, ClosePanel).
	 */
	enum EButton : int32 { Smaller, Bigger, ResetOne, Hide, ResetAll, CopyToAll, Cancel, Done, ModeMenu, ClosePanel, ButtonCount };
	FBox2D GetButtonBox(int32 Button, const FVector2D& Size) const;
	FBox2D GetTabBox(int32 Tab, const FVector2D& Size) const;
	FBox2D GetPanelBox(const FVector2D& Size) const;
	FBox2D GetBarBox(const FVector2D& Size) const;
	FBox2D GetPickedRowBox(const FVector2D& Size) const;
	bool IsPanelOpen() const { return bPanelOpen; }
	/** Tests: the one picked up, and the kind of play shown. */
	int32 GetSelected() const { return Selected; }
	const FString& GetMode() const { return GetModes()[ModeIndex]; }
	const FChaosImpactTouchCustom& GetEdited() const;
	/** What is under a point (a control or a HUD piece; INDEX_NONE: nothing). */
	int32 PickAt(const FVector2D& Point, const FVector2D& Size) const;
	/** Where a control or HUD piece is now (its centre), on a screen of Size. */
	FVector2D GetCenterOf(int32 Control, const FVector2D& Size) const;

private:
	FChaosImpactTouchCustom& Edited();
	/** The layout being changed with nothing hidden (hidden ones are still drawn here, and picked up). */
	const FChaosImpactTouchCustom& ShownCustom() const { ShownCache = GetEdited(); ShownCache.Hidden = 0; return ShownCache; }
	mutable FChaosImpactTouchCustom ShownCache;
	bool IsShown(int32 Control) const;
	void PressButton(int32 Button);
	FBox2D GetHudBox(int32 Control, const FVector2D& Size) const;
	void PaintBackdrop(const FGeometry& Geometry, FSlateWindowElementList& Elements, int32 Layer, const FVector2D& Size) const;
	/** A HUD piece's frame and name over the real piece (and 非表示 over a hidden one). */
	void PaintPieceFrame(const FGeometry& Geometry, FSlateWindowElementList& Elements, int32 Layer, int32 Control, const FBox2D& Box, float Unit) const;
	void PaintPanel(const FGeometry& Geometry, FSlateWindowElementList& Elements, int32 Layer, const FVector2D& Size) const;

	TWeakObjectPtr<AChaosImpactPlayerController> Controller;
	/** The battle HUD itself, drawn with made-up state (never shown on its own). */
	UPROPERTY(Transient)
	TObjectPtr<class UChaosImpactChargeWidget> SampleHud;
	bool bOpen = false;
	double OpenedAt = 0.0;
	int32 Profile = 0;
	int32 ModeIndex = 0;
	/** Each kind of play as it is being changed (as saved, until changed here). */
	TMap<FString, FChaosImpactTouchCustom> Edits;
	bool bButtons = true;
	/** 片手モード (the row of small buttons along the bottom). */
	bool bOneHand = false;
	bool bLeftHanded = false;
	bool bAimStick = true;
	int32 Selected = FChaosImpactTouchCustom::Throw;
	bool bDragging = false;
	FVector2D DragLast = FVector2D::ZeroVector;
	/** Where the drag began, and what was there then: the move is from there (a stray event never adds up). */
	FVector2D DragStart = FVector2D::ZeroVector;
	FVector2D DragStartOffset = FVector2D::ZeroVector;
	FVector2D BarStartCenter = FVector2D::ZeroVector;
	int32 PressedButton = INDEX_NONE;
	/** The kinds of play's panel open; the bar's middle (zero: not placed yet, the screen's middle), dragged by its grip. */
	bool bPanelOpen = false;
	FVector2D BarCenter = FVector2D::ZeroVector;
	bool bDraggingBar = false;
	/** The picked-up one's area (to set its row of buttons beside it). */
	FBox2D GetPickedBox(const FVector2D& Size) const;
	FString Note;
	double NoteAt = -100.0;
};
