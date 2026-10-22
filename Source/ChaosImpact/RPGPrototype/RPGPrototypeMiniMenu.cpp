#include "RPGPrototypeHUD.h"
#include "RPGPrototype.h"
#include "RPGPrototypeUIKit.h"
#include "RPGTuning.h"
#include "ChaosImpactPlayerController.h"
#include "Framework/Application/SlateApplication.h"

// The per-player mini menu (split screen, or solo by choice): a 448x532 panel that rises from the bottom of the
// owner's own view. It is drawn at about 0.65x in a quarter of the screen, so it is one column with few, large
// elements (text 14 or more). Same pages as the full menu, drawn and hit-tested by one layout (MiniLayout).

namespace RPGUI = RPGPrototypeUI;

namespace
{
using ChaosImpactPaint::ETextAlign;
using ChaosImpactPaint::WithAlpha;
using ChaosImpactPaint::HudText3D;
using ChaosImpactPaint::RoundedBox;
namespace MiniAct = RPGMenuAction;

constexpr float MiniW = 448, MiniH = 532;
/** As large as the owner's view allows (a little larger than 1:1 on a full screen), never wider than it. */
float MiniScale(FVector2D Size) { return FMath::Min(1.15f, FMath::Min(Size.X / 470.0f, Size.Y / 556.0f)); }

struct FMiniMenu
{
	const ARPGPrototypeCharacter* P = nullptr;
	const ARPGPrototypeGameMode* Mode = nullptr;
	int32 Page = 0;
	bool bMerchant = false;
	bool bPad = false;
	bool bChoosing = false;
	const TCHAR* Key(const TCHAR* Keyboard, const TCHAR* Pad) const { return bPad ? Pad : Keyboard; }
	// One column: list rows, then a detail strip, then the buttons.
	static constexpr float X = 16, W = MiniW - 32, ListTop = 138, RowH = 46, RowDraw = 42, DetailTop = 402, ButtonY = 452, ButtonH = 40;
};

/** Slots of the compact equipment overview: 3 balls, 4 gears, 3 badges. */
FBox2D MiniSlotRect(int32 Slot)
{
	const int32 Group = Slot < 3 ? 0 : Slot < 7 ? 1 : 2, Column = Slot - (Group == 0 ? 0 : Group == 1 ? 3 : 7), Count = Group == 1 ? 4 : 3;
	const float Width = MiniW - 32, Gap = 7, Cell = (Width - (Count - 1) * Gap) / Count;
	const float X = 16 + Column * (Cell + Gap), Y = 122.0f + Group * 88.0f;
	return FBox2D(FVector2D(X, Y), FVector2D(X + Cell, Y + 61));
}

RPGUI::EStyle RowStyle(const RPGUI::FCanvas& C, const int32 Action, const bool bOn)
{ return bOn ? RPGUI::EStyle::Selected : C.IsHovered(Action) ? RPGUI::EStyle::Hover : RPGUI::EStyle::Card; }

void MiniHeader(RPGUI::FCanvas& C, const FMiniMenu& M)
{
	const auto* P = M.P;
	if (C.IsPainting())
	{
		RPGUI::Panel(C, 0, 0, 0, MiniW, MiniH, RPGUI::EStyle::Panel, WithAlpha(RPGUI::Cyan, 0.8f));
		// Only this player's panel is opaque; the world and the other players' views stay visible.
		C.At(0, 3).Box(7, 5, MiniW - 14, MiniH - 10, FLinearColor(0.014f, 0.022f, 0.046f, 0.97f));
		C.At(0, 4).Box(16, 46, MiniW - 32, 2, WithAlpha(RPGUI::Cyan, 0.4f));
	}
	const TCHAR* Titles[] = {TEXT("装備"), TEXT("強化"), TEXT("チューン"), TEXT("設定"), TEXT("バッグ")};
	RPGUI::Label(C, 1, FString::Printf(TEXT("%dP"), P->GetLocalRPGIndex() + 1), 18, 8, 24, RPGUI::Cyan, ETextAlign::Left, TEXT("BlackItalic"));
	RPGUI::Label(C, 1, M.bChoosing ? TEXT("装備変更") : M.bMerchant && (M.Page == 1 || M.Page == 2) ? TEXT("ルビー工房") : Titles[FMath::Clamp(M.Page, 0, 4)], 64, 10, 21, RPGUI::TextMain);
	RPGUI::Rubies(C, 1, P->GetRubies(), 386, 12, 19, ETextAlign::Right);
	RPGUI::Button(C, 1, 396, 8, 40, 32, TEXT("×"), MiniAct::Close);
	const int32 Pages[] = {0, 4, 1, 2, 3};
	const TCHAR* Tabs[] = {TEXT("装備"), TEXT("バッグ"), TEXT("強化"), TEXT("チューン"), TEXT("設定")};
	for (int32 Tab = 0; Tab < 5; ++Tab)
	{
		const float X = 12 + 85 * Tab, Y = 54, W = 80;
		const bool bEnabled = Tab < 2 || Tab == 4 || M.bMerchant, bOn = M.Page == Pages[Tab];
		const int32 Action = MiniAct::PageBase + Pages[Tab];
		if (bEnabled) { C.Hit(X, Y, W, 36, Action); }
		RPGUI::Panel(C, 1, X, Y, W, 36, !bEnabled ? RPGUI::EStyle::Disabled : RowStyle(C, Action, bOn), bOn ? RPGUI::Highlight : FLinearColor::Transparent);
		RPGUI::Label(C, 1, Tabs[Tab], X + W * 0.5f + 2, Y + 7, 17, !bEnabled ? RPGUI::TextDim : bOn ? RPGUI::TextMain : RPGUI::TextSub, ETextAlign::Center);
		if (Tab == 1 && P->GetUnseenCount() > 0) { RPGUI::Chip(C, 2, FString::FromInt(P->GetUnseenCount()), X + W - 2, Y - 8, RPGUI::NewColor, 11, true, ETextAlign::Right); }
	}
}

void MiniFooter(RPGUI::FCanvas& C, const TArray<TPair<FString, FString>>& Hints)
{
	float X = 16;
	for (const auto& Hint : Hints)
	{
		const float KeyW = RPGUI::Keycap(C, 1, Hint.Key, X, 500, 22);
		RPGUI::Label(C, 1, Hint.Value, X + KeyW + 5, 501, 14, RPGUI::TextSub);
		X += KeyW + RPGUI::Measure(Hint.Value, 14) + 18;
	}
}

const FRPGPrototypeItem* MiniSelected(const FMiniMenu& M)
{
	const auto& Bag = M.P->GetBag();
	return Bag.IsValidIndex(M.P->BagSelection) && M.P->FilteredBag().Contains(M.P->BagSelection) ? &Bag[M.P->BagSelection] : nullptr;
}

/** The item a ball is compared with: the slot being changed, or the special slot it would go to. */
const FRPGPrototypeItem& MiniCompare(const FMiniMenu& M, const FRPGPrototypeItem& I)
{
	const int32 Slot = M.bChoosing ? M.P->EquipmentSelection : RPGUI::SlotForItem(I) == 1 ? FMath::Clamp(M.P->EquipTargetSlot, 1, 2) : RPGUI::SlotForItem(I);
	return M.P->GetEquipment(Slot);
}

void MiniFilters(RPGUI::FCanvas& C, const FMiniMenu& M)
{
	const auto* P = M.P;
	if (M.bChoosing)
	{
		RPGUI::Label(C, 1, RPGUI::Fit(ARPGPrototypeCharacter::EquipmentLabel(P->EquipmentSelection) + TEXT(" に付ける物"), 300, 19), 20, 102, 19, RPGUI::TextMain);
		RPGUI::Button(C, 1, MiniW - 16 - 100, 98, 100, 32, TEXT("戻る"), MiniAct::CancelChange);
		return;
	}
	const int32 Filters[] = {ERPGBagFilter::Ball, ERPGBagFilter::Gear, ERPGBagFilter::Badge, ERPGBagFilter::Material};
	const TCHAR* Names[] = {TEXT("ボール"), TEXT("ギア"), TEXT("バッジ"), TEXT("素材")};
	const float TabW = (FMiniMenu::W - 18) / 4;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const float X = FMiniMenu::X + Index * (TabW + 6);
		const int32 Action = MiniAct::FilterBase + Index;
		const bool bOn = P->BagFilter == Filters[Index];
		C.Hit(X, 98, TabW, 32, Action);
		RPGUI::Panel(C, 1, X, 98, TabW, 32, RowStyle(C, Action, bOn), bOn ? RPGUI::Highlight : FLinearColor::Transparent);
		RPGUI::Label(C, 1, FString::Printf(TEXT("%s %d"), Names[Index], P->CountInFilter(Filters[Index])), X + TabW * 0.5f + 2, 104, 15, bOn ? RPGUI::TextMain : RPGUI::TextSub, ETextAlign::Center);
		if (P->CountInFilter(Filters[Index], true) > 0 && C.IsPainting()) { C.At(2, 4).Disc(FVector2D(X + TabW - 8, 101), 5, RPGUI::NewColor); }
	}
}

/** Five full-width rows: icon, name, level/effect, power and its change, tags. */
void MiniRows(RPGUI::FCanvas& C, const FMiniMenu& M)
{
	const auto* P = M.P;
	const TArray<int32> List = P->FilteredBag();
	const int32 Rows = URPGPrototypeHUD::CompactMenuRows, Start = FMath::Max(0, List.Find(P->BagSelection)) / Rows * Rows;
	if (List.IsEmpty())
	{
		RPGUI::Label(C, 1, M.bChoosing ? TEXT("この枠に付けられる持ち物がありません") : TEXT("この種類の持ち物はありません"), MiniW * 0.5f, FMiniMenu::ListTop + 40, 16, RPGUI::TextDim, ETextAlign::Center);
		RPGUI::Label(C, 1, TEXT("敵のドロップや宝箱で手に入ります"), MiniW * 0.5f, FMiniMenu::ListTop + 68, 14, RPGUI::TextDim, ETextAlign::Center);
	}
	for (int32 Row = 0; Row < Rows && Start + Row < List.Num(); ++Row)
	{
		const int32 Index = List[Start + Row];
		const auto& I = P->GetBag()[Index];
		const float Y = FMiniMenu::ListTop + Row * FMiniMenu::RowH, X = FMiniMenu::X, W = FMiniMenu::W;
		const int32 Action = MiniAct::RowBase + Row;
		C.Hit(X, Y, W, FMiniMenu::RowDraw, Action);
		if (!C.IsPainting()) { continue; }
		const bool bOn = Index == P->BagSelection;
		RPGUI::Panel(C, 1, X, Y, W, FMiniMenu::RowDraw, RowStyle(C, Action, bOn), bOn ? RPGUI::Highlight : FLinearColor::Transparent, RPGUI::RarityColor(I.Rarity));
		RPGUI::ItemIcon(C, 1, FVector2D(X + 30, Y + 21), 13, I);
		float Right = X + W - 14;
		if (I.Kind == ERPGItemKind::Ball)
		{
			const float Power = P->GetDisplayedPower(I);
			RPGUI::Label(C, 1, FString::Printf(TEXT("%.0f"), Power), Right, Y + 1, 21, RPGUI::Highlight, ETextAlign::Right);
			const auto& Current = MiniCompare(M, I);
			if (!P->IsEquipped(I.Id) && Current.IsValid()) { RPGUI::Delta(C, 1, Power - P->GetDisplayedPower(Current), Right, Y + 25, 12, ETextAlign::Right); }
			Right -= 54;
		}
		if (P->IsUnseen(I.Id)) { Right -= RPGUI::Chip(C, 1, TEXT("NEW"), Right, Y + 11, RPGUI::NewColor, 11, true, ETextAlign::Right) + 4; }
		if (P->IsEquipped(I.Id)) { Right -= RPGUI::Chip(C, 1, TEXT("装備中"), Right, Y + 11, RPGUI::Highlight, 11, false, ETextAlign::Right) + 4; }
		if (I.bLocked) { Right -= RPGUI::Chip(C, 1, TEXT("ロック"), Right, Y + 11, RPGUI::Cyan, 11, false, ETextAlign::Right) + 4; }
		const float TextX = X + 54, TextW = Right - TextX - 4;
		RPGUI::Label(C, 1, RPGUI::Fit(I.Name(), TextW, 17), TextX, Y + 1, 17, I.Rarity == ERPGPrototypeRarity::Normal ? RPGUI::TextMain : RPGUI::RarityColor(I.Rarity));
		const FString Sub = I.Kind == ERPGItemKind::Ball ? FString::Printf(TEXT("Lv.%d +%d %s"), I.Level, I.Enhancement, RPGUI::RarityName(I.Rarity))
			: FString::Printf(TEXT("Lv.%d +%d %s"), I.Level, I.Enhancement, *RPGUI::ShortEffect(I));
		RPGUI::Label(C, 1, RPGUI::Fit(Sub, TextW, 13), TextX, Y + 23, 13, RPGUI::TextSub);
	}
	const float PagerY = FMiniMenu::ListTop + Rows * FMiniMenu::RowH;
	const int32 Pages = FMath::Max(1, FMath::DivideAndRoundUp(List.Num(), Rows));
	if (Pages > 1 || C.IsPainting())
	{
		RPGUI::Button(C, 1, FMiniMenu::X, PagerY, 56, 26, TEXT("‹"), MiniAct::ListPrev, false, Start > 0);
		RPGUI::Button(C, 1, FMiniMenu::X + 62, PagerY, 56, 26, TEXT("›"), MiniAct::ListNext, false, Start + Rows < List.Num());
		RPGUI::Label(C, 1, FString::Printf(TEXT("%d / %d"), Start / Rows + 1, Pages), FMiniMenu::X + 128, PagerY + 4, 14, RPGUI::TextSub);
		RPGUI::Label(C, 1, ARPGPrototypeCharacter::BagSortName(P->BagSort), FMiniMenu::X + FMiniMenu::W, PagerY + 4, 14, RPGUI::TextDim, ETextAlign::Right);
	}
}

/** The selected item's effects, one section at a time (▲▼ / wheel / right stick). */
void MiniDetailStrip(RPGUI::FCanvas& C, const FMiniMenu& M, const FRPGPrototypeItem& I)
{
	const auto Sections = RPGUI::DetailSections(I);
	if (Sections.IsEmpty()) { return; }
	const int32 Index = FMath::Clamp(M.P->DetailScroll, 0, Sections.Num() - 1);
	const float Y = FMiniMenu::DetailTop, W = FMiniMenu::W - 84;
	if (C.IsPainting()) { RoundedBox(C.At(1, 2), FMiniMenu::X, Y - 4, FMiniMenu::W, 48, 6, FLinearColor(1, 1, 1, 0.05f)); }
	RPGUI::Label(C, 1, RPGUI::Fit(Sections[Index].Title, W, 13), FMiniMenu::X + 10, Y - 1, 13, Sections[Index].Color);
	RPGUI::Label(C, 1, RPGUI::Fit(Sections[Index].Body, W, 15), FMiniMenu::X + 10, Y + 18, 15, Sections[Index].bEmpty ? RPGUI::TextDim : RPGUI::TextMain);
	RPGUI::Label(C, 1, FString::Printf(TEXT("%d/%d"), Index + 1, Sections.Num()), FMiniMenu::X + FMiniMenu::W - 76, Y + 1, 12, RPGUI::TextDim, ETextAlign::Right);
	RPGUI::Button(C, 1, FMiniMenu::X + FMiniMenu::W - 70, Y, 32, 38, TEXT("▲"), MiniAct::DetailUp, false, Index > 0);
	RPGUI::Button(C, 1, FMiniMenu::X + FMiniMenu::W - 34, Y, 32, 38, TEXT("▼"), MiniAct::DetailDown, false, Index + 1 < Sections.Num());
}

void MiniOverview(RPGUI::FCanvas& C, const FMiniMenu& M)
{
	const auto* P = M.P;
	const TCHAR* Groups[] = {TEXT("ボール"), TEXT("ギア"), TEXT("バッジ")};
	for (int32 Group = 0; Group < 3; ++Group) { RPGUI::Section(C, 1, Groups[Group], 18, 97 + Group * 88, MiniW - 36); }
	for (int32 Slot = 0; Slot < 10; ++Slot)
	{
		const FBox2D R = MiniSlotRect(Slot);
		const float X = R.Min.X, Y = R.Min.Y, W = R.GetSize().X, H = R.GetSize().Y;
		const auto& Item = P->GetEquipment(Slot);
		const int32 Action = MiniAct::SlotBase + Slot;
		C.Hit(X, Y, W, H, Action);
		if (!C.IsPainting()) { continue; }
		const bool bOn = P->EquipmentSelection == Slot;
		RPGUI::Panel(C, 1, X, Y, W, H, RowStyle(C, Action, bOn), bOn ? RPGUI::Highlight : FLinearColor::Transparent, Item.IsValid() ? RPGUI::RarityColor(Item.Rarity) : FLinearColor::Transparent);
		if (Slot < 3)
		{
			RPGUI::Label(C, 1, ARPGPrototypeCharacter::EquipmentLabel(Slot), X + 14, Y + 3, 13, RPGUI::TextSub);
			if (Item.IsValid()) { RPGUI::ItemIcon(C, 1, FVector2D(X + 28, Y + 39), 13, Item); }
			RPGUI::Label(C, 1, Item.IsValid() ? FString::Printf(TEXT("%.0f"), P->GetDisplayedPower(Item)) : TEXT("—"), X + W - 12, Y + 24, 26, RPGUI::Highlight, ETextAlign::Right);
		}
		else
		{
			RPGUI::Label(C, 1, ARPGPrototypeCharacter::EquipmentLabel(Slot), X + W * 0.5f + 2, Y + 3, 14, Item.IsValid() ? RPGUI::TextMain : RPGUI::TextDim, ETextAlign::Center);
			if (Item.IsValid()) { RPGUI::ItemIcon(C, 1, FVector2D(X + W * 0.5f, Y + 40), 11, Item); }
			else { RPGUI::SlotGlyph(C, 1, FVector2D(X + W * 0.5f, Y + 40), 11, Slot, true); }
		}
	}
	const auto& Item = P->GetEquipment(P->EquipmentSelection);
	const FString Name = Item.IsValid() ? Item.Name() : ARPGPrototypeCharacter::EquipmentLabel(P->EquipmentSelection) + TEXT("（未装備）");
	RPGUI::Label(C, 1, RPGUI::Fit(Name, MiniW - 36, 20), 18, 368, 20, Item.IsValid() && Item.Rarity != ERPGPrototypeRarity::Normal ? RPGUI::RarityColor(Item.Rarity) : RPGUI::TextMain);
	RPGUI::Wrap(C, 1, Item.IsValid() ? RPGUI::ShortEffect(Item) : FString(TEXT("「装備変更」で付ける物を選べます")), 18, 398, MiniW - 36, 15, RPGUI::TextSub, 2, 21);
	RPGUI::Button(C, 1, 16, FMiniMenu::ButtonY, MiniW - 32, FMiniMenu::ButtonH, TEXT("装備変更"), MiniAct::BeginChange, true, true, M.Key(TEXT("Enter"), TEXT("決定")));
	MiniFooter(C, {{M.Key(TEXT("↑↓←→"), TEXT("十字")), TEXT("枠")}, {M.Key(TEXT("Q E"), TEXT("L R")), TEXT("ページ")}, {M.Key(TEXT("Esc"), TEXT("戻る")), TEXT("閉じる")}});
}

void MiniItems(RPGUI::FCanvas& C, const FMiniMenu& M)
{
	const auto* P = M.P;
	MiniFilters(C, M);
	MiniRows(C, M);
	const FRPGPrototypeItem* I = MiniSelected(M);
	if (I) { MiniDetailStrip(C, M, *I); }
	const bool bEquipped = I && P->IsEquipped(I->Id);
	const float Y = FMiniMenu::ButtonY, H = FMiniMenu::ButtonH;
	const FString Enter = M.Key(TEXT("Enter"), TEXT("決定"));
	if (M.bChoosing)
	{
		const bool bAlready = I && P->GetEquipment(P->EquipmentSelection).Id == I->Id;
		RPGUI::Button(C, 1, 16, Y, MiniW - 32, H, bAlready ? TEXT("この枠に装備中") : TEXT("この装備に変更"), MiniAct::EquipSelected, true, I && !bAlready, Enter);
		MiniFooter(C, {{M.Key(TEXT("↑↓"), TEXT("十字")), TEXT("選択")}, {Enter, TEXT("変更")}, {M.Key(TEXT("Esc"), TEXT("戻る")), TEXT("やめる")}});
		return;
	}
	RPGUI::Button(C, 1, 16, Y, 76, H, I && I->bLocked ? TEXT("解除") : TEXT("ロック"), MiniAct::Lock, false, I != nullptr);
	RPGUI::Button(C, 1, 96, Y, 104, H, I ? FString::Printf(TEXT("分解◆%d"), I->SalvageValue()) : FString(TEXT("分解")), MiniAct::Salvage, false, I && !bEquipped && !I->bLocked);
	if (I && I->Kind == ERPGItemKind::Ball && I->Type != EChaosImpactBallType::Normal)
	{
		for (int32 Slot = 1; Slot <= 2; ++Slot)
		{
			const bool bHere = P->GetAttack(Slot).Id == I->Id;
			RPGUI::Button(C, 1, 204 + (Slot - 1) * 118, Y, 114, H, bHere ? FString::Printf(TEXT("特殊%d装備中"), Slot) : FString::Printf(TEXT("特殊%dへ"), Slot),
				Slot == 1 ? MiniAct::EquipSpecial1 : MiniAct::EquipSpecial2, !P->bChoosingDestination || P->EquipTargetSlot == Slot, !bHere);
		}
	}
	else { RPGUI::Button(C, 1, 204, Y, MiniW - 220, H, bEquipped ? TEXT("装備中") : TEXT("装備する"), MiniAct::EquipSelected, true, I && !bEquipped, Enter); }
	MiniFooter(C, {{M.Key(TEXT("←→"), TEXT("左右")), TEXT("種類")}, {M.Key(TEXT("X"), TEXT("ZL")), TEXT("並べ替え")}, {M.Key(TEXT("F"), TEXT("上")), TEXT("ロック")}, {M.Key(TEXT("Esc"), TEXT("戻る")), TEXT("閉じる")}});
}

void MiniUpgrade(RPGUI::FCanvas& C, const FMiniMenu& M)
{
	const auto* P = M.P;
	MiniFilters(C, M);
	MiniRows(C, M);
	const FRPGPrototypeItem* I = MiniSelected(M);
	if (!I) { return; }
	const bool bMax = I->Rarity != ERPGPrototypeRarity::Legend && I->Enhancement >= RPGTuning::EnhanceCap;
	auto Next = *I;
	if (!bMax && Next.Enhancement < MAX_int32) { ++Next.Enhancement; }
	const float Y = FMiniMenu::DetailTop;
	if (C.IsPainting())
	{
		RoundedBox(C.At(1, 2), FMiniMenu::X, Y - 4, FMiniMenu::W, 48, 6, FLinearColor(1, 1, 1, 0.05f));
		HudText3D(C.At(1, 6), bMax ? TEXT("MAX") : FString::Printf(TEXT("+%d → +%d"), I->Enhancement, I->Enhancement + 1), FMiniMenu::X + 12, Y + 4, 24, RPGUI::Highlight);
	}
	if (I->Kind == ERPGItemKind::Ball)
	{
		RPGUI::Label(C, 1, FString::Printf(TEXT("パワー %.0f → %.0f"), P->GetDisplayedPower(*I), P->GetDisplayedPower(Next)), FMiniMenu::X + 170, Y + 9, 17, RPGUI::TextMain);
		RPGUI::Delta(C, 1, P->GetDisplayedPower(Next) - P->GetDisplayedPower(*I), FMiniMenu::X + FMiniMenu::W - 10, Y + 10, 15, ETextAlign::Right);
	}
	else { RPGUI::Label(C, 1, RPGUI::Fit(RPGUI::ActualDescription(Next, Next.Badge), FMiniMenu::W - 180, 15), FMiniMenu::X + 170, Y + 10, 15, RPGUI::Good); }
	const int32 Cost = I->UpgradeCost();
	const bool bAfford = P->GetRubies() >= Cost;
	RPGUI::Button(C, 1, 16, FMiniMenu::ButtonY, MiniW - 32, FMiniMenu::ButtonH, bMax ? TEXT("強化MAX") : bAfford ? FString::Printf(TEXT("強化する  ◆%d"), Cost) : FString::Printf(TEXT("ルビー不足（あと◆%d）"), Cost - P->GetRubies()),
		MiniAct::Upgrade, true, !bMax && bAfford, M.Key(TEXT("Enter"), TEXT("決定")));
	MiniFooter(C, {{M.Key(TEXT("↑↓"), TEXT("十字")), TEXT("選択")}, {M.Key(TEXT("←→"), TEXT("左右")), TEXT("種類")}, {M.Key(TEXT("Q E"), TEXT("L R")), TEXT("ページ")}});
}

void MiniTune(RPGUI::FCanvas& C, const FMiniMenu& M)
{
	const auto* P = M.P;
	const FRPGPrototypeItem* I = MiniSelected(M);
	// Which ball: a selector instead of a list, so the offers get the room.
	RPGUI::Button(C, 1, 16, 98, 40, 34, TEXT("‹"), MiniAct::SelectPrev);
	RPGUI::Button(C, 1, MiniW - 56, 98, 40, 34, TEXT("›"), MiniAct::SelectNext);
	if (!I) { RPGUI::Label(C, 1, TEXT("チューンできるボールがありません"), MiniW * 0.5f, 104, 16, RPGUI::TextDim, ETextAlign::Center); return; }
	RPGUI::ItemIcon(C, 1, FVector2D(84, 115), 12, *I);
	RPGUI::Label(C, 1, RPGUI::Fit(FString::Printf(TEXT("%s  Lv.%d +%d"), *I->Name(), I->Level, I->Enhancement), 280, 18), 106, 104, 18, I->Rarity == ERPGPrototypeRarity::Normal ? RPGUI::TextMain : RPGUI::RarityColor(I->Rarity));
	const float SocketW = (FMiniMenu::W - 12) / 3;
	for (int32 Socket = 0; Socket < 3; ++Socket)
	{
		const int32 Action = MiniAct::SocketBase + Socket;
		const FRPGAffix Affix = I->Tunes.IsValidIndex(Socket) ? I->Tunes[Socket] : FRPGAffix{};
		const auto* Definition = RPGEquipment::Find(Affix.Key);
		const float SX = FMiniMenu::X + Socket * (SocketW + 6);
		C.Hit(SX, 140, SocketW, 44, Action);
		RPGUI::Panel(C, 1, SX, 140, SocketW, 44, RowStyle(C, Action, P->TuneSocket == Socket), P->TuneSocket == Socket ? RPGUI::Highlight : FLinearColor::Transparent);
		RPGUI::Label(C, 1, Socket == 0 ? TEXT("専用1") : Socket == 1 ? TEXT("専用2") : TEXT("共通"), SX + 14, 142, 12, Socket < 2 ? RPGUI::Highlight : RPGUI::Cyan);
		RPGUI::Label(C, 1, RPGUI::Fit(Definition ? FString::Printf(TEXT("%s R%d"), *Definition->Name, Affix.Rank) : FString(TEXT("未装着")), SocketW - 22, 15),
			SX + 14, 160, 15, Definition ? RPGUI::Highlight : RPGUI::TextDim);
	}
	const auto Offers = P->GetTuneOffers(P->BagSelection, P->TuneSocket);
	const auto& Pool = RPGEquipment::Tunes(I->Type);
	for (int32 Row = 0; Row < Offers.Num(); ++Row)
	{
		const auto& Definition = Pool[Offers[Row]];
		const float Y = 192 + Row * 52;
		const int32 Action = MiniAct::OfferBase + Row;
		const bool bOn = P->TuneSelection == Row;
		C.Hit(FMiniMenu::X, Y, FMiniMenu::W, 48, Action);
		RPGUI::Panel(C, 1, FMiniMenu::X, Y, FMiniMenu::W, 48, RowStyle(C, Action, bOn), bOn ? RPGUI::Highlight : FLinearColor::Transparent);
		RPGUI::Label(C, 1, Definition.Name, FMiniMenu::X + 18, Y + 2, 17, RPGUI::TextMain);
		RPGUI::Label(C, 1, RPGUI::Fit(RPGUI::HiddenDescription(Definition), FMiniMenu::W - 30, 13), FMiniMenu::X + 18, Y + 26, 13, RPGUI::TextSub);
	}
	if (Offers.IsEmpty()) { RPGUI::Label(C, 1, TEXT("この枠の候補はもうありません"), MiniW * 0.5f, 210, 15, RPGUI::TextDim, ETextAlign::Center); }
	const FRPGAffix Current = I->Tunes.IsValidIndex(P->TuneSocket) ? I->Tunes[P->TuneSocket] : FRPGAffix{};
	const auto* Existing = RPGEquipment::Find(Current.Key);
	if (C.IsPainting()) { RoundedBox(C.At(1, 2), FMiniMenu::X, 352, FMiniMenu::W, 90, 6, FLinearColor(1, 1, 1, 0.05f)); }
	RPGUI::Label(C, 1, Existing ? FString::Printf(TEXT("装着中：%s  ランク%d/%d"), *Existing->Name, Current.Rank, RPGTuning::TuneMaxRank) : FString(TEXT("装着中：なし（候補の数値は付けると判明）")),
		FMiniMenu::X + 10, 358, 15, Existing ? RPGUI::Highlight : RPGUI::TextDim);
	if (Existing) { RPGUI::Wrap(C, 1, RPGEquipment::Describe(Current), FMiniMenu::X + 10, 382, FMiniMenu::W - 20, 14, RPGUI::TextSub, 2, 20); }
	const float Half = (MiniW - 40) * 0.5f;
	const int32 RankCost = I->TuneCost() * FMath::Max(1, Current.Rank);
	const bool bCanRank = Existing && Current.Rank < RPGTuning::TuneMaxRank;
	RPGUI::Button(C, 1, 16, FMiniMenu::ButtonY, Half, FMiniMenu::ButtonH, bCanRank ? FString::Printf(TEXT("ランクUP ◆%d"), RankCost) : FString(TEXT("ランクUP")), MiniAct::RankUp, false,
		bCanRank && P->GetRubies() >= RankCost);
	RPGUI::Button(C, 1, 24 + Half, FMiniMenu::ButtonY, Half, FMiniMenu::ButtonH, FString::Printf(TEXT("付ける ◆%d"), I->TuneCost()), MiniAct::ApplyTune, true, Offers.Num() > 0 && P->GetRubies() >= I->TuneCost());
	MiniFooter(C, {{M.Key(TEXT("↑↓"), TEXT("十字")), TEXT("候補")}, {M.Key(TEXT("←→"), TEXT("左右")), TEXT("枠")}, {M.Key(TEXT("PgUp/Dn"), TEXT("Rスティック")), TEXT("ボール")}});
}

void MiniSettings(RPGUI::FCanvas& C, const FMiniMenu& M)
{
	const auto* P = M.P;
	const int32 Count = M.Mode ? M.Mode->GetLocalPlayerCount() : 1;
	const bool bOwner = P->GetLocalRPGIndex() == 0;
	const FString Names[] = {TEXT("テストセーブ"), TEXT("テストロード"), TEXT("1人時のメニュー"), TEXT("ローカル人数"), TEXT("マルチ画面"), P->HeroId.IsEmpty() ? TEXT("装備に戻る") : TEXT("保存してタイトルへ")};
	const FString Values[] = {P->SaveSummary, TEXT("自分のデータ"), Count > 1 ? TEXT("マルチ中は固定") : TEXT("ミニメニュー"), FString::Printf(TEXT("‹ %d人 ›"), Count),
		M.Mode && M.Mode->IsSharedScreenRequested() ? TEXT("‹ 一画面共有 ›") : TEXT("‹ 分割画面 ›"), TEXT("")};
	for (int32 Row = 0; Row < 6; ++Row)
	{
		const float Y = 108 + Row * 46;
		const bool bEnabled = (Row != 3 && Row != 4 && !(Row == 5 && !P->HeroId.IsEmpty())) || bOwner, bOn = P->SettingsSelection == Row;
		const int32 Action = MiniAct::SettingBase + Row;
		if (bEnabled)
		{
			if (Row == 3) { C.Hit(16, Y, 344, 40, MiniAct::PlayersDown); C.Hit(360, Y, 72, 40, MiniAct::PlayersUp); }
			else { C.Hit(16, Y, MiniW - 32, 40, Action); }
		}
		RPGUI::Panel(C, 1, 16, Y, MiniW - 32, 40, !bEnabled ? RPGUI::EStyle::Disabled : RowStyle(C, Action, bOn),
			bOn ? RPGUI::Highlight : FLinearColor::Transparent, Row < 2 ? RPGUI::Highlight : WithAlpha(RPGUI::Cyan, 0.8f));
		RPGUI::Label(C, 1, Names[Row], 34, Y + 8, 18, bEnabled ? RPGUI::TextMain : RPGUI::TextDim);
		RPGUI::Label(C, 1, bEnabled ? Values[Row] : FString(TEXT("1Pのみ")), MiniW - 30, Y + 10, 16, bEnabled ? RPGUI::Highlight : RPGUI::TextDim, ETextAlign::Right);
	}
	RPGUI::Wrap(C, 1, TEXT("保存：装備・所持品・Lv・経験値・ルビー・宝箱・依頼"), 20, 392, MiniW - 40, 15, RPGUI::TextSub, 2, 21);
	RPGUI::Wrap(C, 1, TEXT("ステージ・敵・落ちている物は保存しません"), 20, 438, MiniW - 40, 14, RPGUI::TextDim, 2, 20);
	MiniFooter(C, {{M.Key(TEXT("↑↓"), TEXT("十字")), TEXT("選択")}, {M.Key(TEXT("Enter"), TEXT("決定")), TEXT("実行")}, {M.Key(TEXT("Esc"), TEXT("戻る")), TEXT("閉じる")}});
}

void MiniDialog(RPGUI::FCanvas& C, const FMiniMenu& M)
{
	const auto* P = M.P;
	FString Title, Body;
	if (P->PendingSessionAction == 1) { Title = TEXT("上書き保存しますか？"); Body = TEXT("前のテストセーブを置き換えます。"); }
	else if (P->PendingSessionAction == 2) { Title = TEXT("自分のデータをロード？"); Body = TEXT("未保存の変更は戻ります。他の人と世界は変わりません。"); }
	else if (P->bPendingBulkSalvage) { Title = FString::Printf(TEXT("%d個をまとめて分解？"), P->BulkSalvageCandidates().Num()); Body = TEXT("装備中・ロック中・NEW・ユニーク以上は残します。"); }
	else if (P->PendingSalvage != INDEX_NONE && P->GetBag().IsValidIndex(P->PendingSalvage))
	{ Title = TEXT("この装備を分解しますか？"); Body = FString::Printf(TEXT("%s → ルビー +%d。分解した装備は戻りません。"), *P->GetBag()[P->PendingSalvage].Name(), P->GetBag()[P->PendingSalvage].SalvageValue()); }
	else { Title = TEXT("チューンを付け替えますか？"); Body = TEXT("前のチューンとランクは失われます。"); }
	C.bInModal = true;
	RPGUI::DialogVeil(C, 7, 5, MiniW - 14, MiniH - 10);
	RPGUI::Panel(C, 2, 22, 171, 404, 176, RPGUI::EStyle::Panel, RPGUI::Highlight);
	RPGUI::Label(C, 2, Title, MiniW * 0.5f, 190, 21, RPGUI::TextMain, ETextAlign::Center);
	RPGUI::Wrap(C, 2, Body, 42, 228, 364, 15, RPGUI::TextSub, 2, 21);
	RPGUI::Button(C, 2, 43, 292, 170, 40, TEXT("キャンセル"), MiniAct::DialogCancel);
	RPGUI::Button(C, 2, 235, 292, 170, 40, TEXT("決定"), MiniAct::DialogOk, true);
	C.bInModal = false;
}

void MiniLayout(RPGUI::FCanvas& C, const FMiniMenu& M)
{
	C.bModalOpen = M.P->IsBagOpen() && M.P->HasConfirmation();
	MiniHeader(C, M);
	if (M.Page == ERPGBagPage::Settings) { MiniSettings(C, M); }
	else if (M.Page == ERPGBagPage::Upgrade) { MiniUpgrade(C, M); }
	else if (M.Page == ERPGBagPage::Tune) { MiniTune(C, M); }
	else if (M.Page == ERPGBagPage::Items || M.bChoosing) { MiniItems(C, M); }
	else { MiniOverview(C, M); }
	if (C.bModalOpen) { MiniDialog(C, M); }
}
}

FMiniMenuState URPGPrototypeHUD::MiniState() const
{
	FMiniMenuState S;
	const auto* P = Cast<ARPGPrototypeCharacter>(GetOwningPlayerPawn());
	if (!P) { return S; }
	S.Page = P->IsBagOpen() ? P->BagPage : LastMiniPage;
	S.bMerchant = P->IsBagOpen() ? P->IsAtMerchant() : LastMiniMerchant;
	S.bChoosing = P->IsBagOpen() && P->bChoosingEquipment;
	return S;
}
FGeometry URPGPrototypeHUD::MiniPanelGeometry(const FGeometry& Geometry) const
{
	const FGeometry Area=OwnerMenuGeometry(Geometry);
	const FVector2D Size=Area.GetLocalSize(); const float Scale=MiniScale(Size);
	// A short, gently overshooting rise from below the owner's viewport, then pull back down.
	const float T=FMath::Clamp(MiniRevealAlpha,0.0f,1.0f);
	const float U=T-1;
	const float Reveal=1+2.2f*U*U*U+1.2f*U*U;
	const float Y=Size.Y-MiniH*Scale-12+(MiniH*Scale+24)*(1-Reveal);
	return ChaosImpactPaint::MakeAnchor(Area,(Size.X-MiniW*Scale)*.5f,Y,Scale);
}
void URPGPrototypeHUD::PaintMiniMenu(const FGeometry& Geometry,FSlateWindowElementList& Elements,int32 Layer,float Time) const
{
	const auto* Player=Cast<ARPGPrototypeCharacter>(GetOwningPlayerPawn()); if(!Player) { return; }
	if(MiniScale(OwnerMenuGeometry(Geometry).GetLocalSize())<=0) { return; }
	const FGeometry PanelGeometry=MiniPanelGeometry(Geometry);
	PaintLoadout(PanelGeometry,Elements,Layer,MiniW,MiniH,Time,true);
}
void URPGPrototypeHUD::PaintLoadout(const FGeometry& Design,FSlateWindowElementList& Elements,int32 Layer,float W,float H,float Time,bool Compact) const
{
	const auto* Player=Cast<ARPGPrototypeCharacter>(GetOwningPlayerPawn()); if(!Player) { return; }
	const FMiniMenuState S=MiniState();
	FMiniMenu M; M.P=Player; M.Page=S.Page; M.bMerchant=S.bMerchant; M.bChoosing=S.bChoosing;
	M.Mode=GetWorld()->GetAuthGameMode<ARPGPrototypeGameMode>();
	const auto* PC=Cast<AChaosImpactPlayerController>(GetOwningPlayer()); M.bPad=PC && PC->IsUsingGamepad();
	RPGUI::FCanvas C; C.Geometry=&Design; C.Elements=&Elements; C.Layer=Layer+3; C.Time=Time; C.bGamepad=M.bPad;
	C.Hover=Player->IsBagOpen()?GetBagPointerAction():0;
	MiniLayout(C,M);
}
int32 URPGPrototypeHUD::LoadoutActionAt(FVector2D At,float W,float H,bool Compact) const
{ return MiniActionAt(At); }
int32 URPGPrototypeHUD::MiniActionAt(FVector2D At) const
{
	const auto* P=Cast<ARPGPrototypeCharacter>(GetOwningPlayerPawn()); if(!P) { return 0; }
	if(At.X<0 || At.Y<0 || At.X>MiniW || At.Y>MiniH) { return 0; }
	const FMiniMenuState S=MiniState();
	FMiniMenu M; M.P=P; M.Page=S.Page; M.bMerchant=S.bMerchant; M.bChoosing=S.bChoosing;
	M.Mode=GetWorld()?GetWorld()->GetAuthGameMode<ARPGPrototypeGameMode>():nullptr;
	RPGUI::FCanvas C; MiniLayout(C,M);
	return C.ActionAt(At);
}
int32 URPGPrototypeHUD::GetBagPointerAction() const
{
	const auto* P=Cast<ARPGPrototypeCharacter>(GetOwningPlayerPawn()); const auto* PC=Cast<AChaosImpactPlayerController>(GetOwningPlayer());
	if(!P || !P->IsBagOpen() || !PC || PC->IsUsingGamepad() || !FSlateApplication::IsInitialized()) { return 0; }
	const auto& G=GetCachedGeometry(); const FVector2D Size=G.GetLocalSize(),At=G.AbsoluteToLocal(FSlateApplication::Get().GetCursorPos());
	if(P->UsesMiniMenu()) { return MiniScale(Size)>0?MiniActionAt(MiniPanelGeometry(G).AbsoluteToLocal(FSlateApplication::Get().GetCursorPos())):0; }
	const float S=FMath::Min(Size.X/1280.0,Size.Y/720.0); return S>0?BagActionAt(At/S,Size.X/S,Size.Y/S):0;
}
