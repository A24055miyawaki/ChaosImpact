#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RPGPrototypeHUD.generated.h"

/**
 * What a click (or the matching key) does in the bag menus: full screen, mini menu and its compact loadout.
 * The numbers are stable (automation tests click by them). Painted controls register these through
 * RPGPrototypeUI::FCanvas, so the click map always matches the drawing.
 */
namespace RPGMenuAction
{
	enum : int32
	{
		Close = 1,
		PageBase = 10,        // + ERPGBagPage
		EquipSlot1 = 40, EquipSlot2 = 41, Salvage = 42, Upgrade = 43, ApplyTune = 44, RankUp = 45,
		SocketBase = 50,      // + tune socket 0-2
		OfferBase = 200,      // + tune offer 0-2
		InspectBase = 300,    // + mini menu equipped shortcut 0-4
		SettingBase = 400,    // + settings row 0-5
		PlayersDown = 410, PlayersUp = 411,
		MiniRowBase = 500, MiniPrev = 530, MiniNext = 531,
		DetailUp = 600, DetailDown = 601,
		SlotBase = 700,       // + equipment slot 0-9
		BeginChange = 710, EquipSelected = 711, CancelChange = 712,
		FilterBase = 720,     // + 0 ball, 1 gear, 2 badge, 3 material
		RowBase = 730,        // + visible list row
		ListPrev = 740, ListNext = 741,
		SelectPrev = 742, SelectNext = 743, // previous / next item of the list (mini tune page)
		EquipSpecial1 = 745, EquipSpecial2 = 746,
		Sort = 750, Lock = 751, BulkSalvage = 752,
		DialogCancel = 900, DialogOk = 901,
	};
}

/** What the mini menu shows: the live page while open, the last one while it slides away. */
struct FMiniMenuState
{
	int32 Page = 0;
	bool bMerchant = false;
	bool bChoosing = false;
};

UCLASS()
class URPGPrototypeHUD : public UUserWidget
{
	GENERATED_BODY()
public:
	/** Rows of the item list per page: full-screen menu and the narrow mini menu. */
	static constexpr int32 FullMenuRows = 8;
	static constexpr int32 CompactMenuRows = 5;
	int32 GetPointerAction() const;
	int32 BagActionAt(FVector2D At,float W,float H) const;
	void ActivateBagAction(int32 Action);
	void PaintEquipmentBag(const FGeometry& Design,FSlateWindowElementList& Elements,int32 Layer,float W,float H,float Time) const;
	void PaintMiniMenu(const FGeometry& Geometry,FSlateWindowElementList& Elements,int32 Layer,float Time) const;
	int32 MiniActionAt(FVector2D At) const;
	int32 GetBagPointerAction() const;
	void PaintAdventure(const FGeometry& Geometry,FSlateWindowElementList& Elements,int32 Layer,float Time) const;
	int32 GetAdventurePointerAction() const;
	void ActivateAdventureAction(int32 Action);
	float GetMiniRevealAlpha() const { return MiniRevealAlpha; }
	void PaintLoadout(const FGeometry& Design,FSlateWindowElementList& Elements,int32 Layer,float W,float H,float Time,bool Compact) const;
	int32 LoadoutActionAt(FVector2D At,float W,float H,bool Compact) const;
	FGeometry OwnerMenuGeometry(const FGeometry& Geometry) const;
	void PaintSharedHUD(const FGeometry& Geometry,FSlateWindowElementList& Elements,int32 Layer,float Time) const;
protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
		FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool bEnabled) const override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
	virtual FReply NativeOnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) override;
private:
	FGeometry MiniPanelGeometry(const FGeometry& Geometry) const;
	FMiniMenuState MiniState() const;
	float MiniRevealAlpha=0;
	int32 LastMiniPage=0;
	bool LastMiniMerchant=false;
	float QuestRevealAlpha=0;
	FGeometry QuestPanelGeometry(const FGeometry& Geometry) const;
	float PreviousHP = -1;
	float TrailingHP = 100;
	float PreviousCooldown[3] = {0,0,0};
	double ReadyFlashAt[3] = {-100,-100,-100};
	double HitAt = -100;
	double SelectionAt = -100;
	int32 PreviousSelection = -1;
	/** The bag item on screen and since when; it stops being "NEW" after a short look. */
	int32 SeenSelection = INDEX_NONE;
	double SeenSince = 0;
};
