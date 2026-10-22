#include "RPGPrototypeHUD.h"
#include "RPGPrototype.h"
#include "RPGPrototypeUIKit.h"
#include "RPGTuning.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactSfx.h"

// The full-screen bag menu (1 player, "全体メニュー"). Pages: 装備 / バッグ / 強化 / チューン / 設定.
// FullMenuLayout() both paints and collects the click regions, so what is drawn is exactly what can be clicked.
// Action numbers are listed in RPGPrototypeHUD.h (RPGMenuAction).

namespace RPGUI = RPGPrototypeUI;

namespace
{
using ChaosImpactPaint::ETextAlign;
using ChaosImpactPaint::WithAlpha;
using ChaosImpactPaint::HudText3D;
using ChaosImpactPaint::RoundedBox;
namespace Act = RPGMenuAction;

struct FFullMenu
{
	const ARPGPrototypeCharacter* P = nullptr;
	const ARPGPrototypeGameMode* Mode = nullptr;
	float W = 1280, H = 720;
	bool bPad = false;
	const TCHAR* Key(const TCHAR* Keyboard, const TCHAR* Pad) const { return bPad ? Pad : Keyboard; }
	// Layout of the 1280x720 design space (wider screens get wider right panels).
	static constexpr float Margin = 32, ContentTop = 96, RowH = 52, RowDraw = 46;
	float ListW() const { return FMath::Min(560.0f, W * 0.41f); }
	float ContentBottom() const { return H - 54; }
	float DetailX() const { return Margin + ListW() + 24; }
	float DetailW() const { return W - Margin - DetailX(); }
	bool IsOverview() const { return P->BagPage == ERPGBagPage::Equipment && !P->bChoosingEquipment; }
	bool ShowsList() const { return !IsOverview() && P->BagPage != ERPGBagPage::Settings; }
};

const FRPGPrototypeItem* SelectedBagItem(const FFullMenu& M)
{
	const auto& Bag = M.P->GetBag();
	return Bag.IsValidIndex(M.P->BagSelection) && M.P->FilteredBag().Contains(M.P->BagSelection) ? &Bag[M.P->BagSelection] : nullptr;
}

/** The equipped item a candidate is compared with (picker: the slot being changed; specials: the weaker one). */
int32 CompareSlot(const ARPGPrototypeCharacter* P, const FRPGPrototypeItem& Item)
{
	if (P->bChoosingEquipment) { return P->EquipmentSelection; }
	const int32 Slot = RPGUI::SlotForItem(Item);
	if (Slot != 1) { return Slot; }
	const auto& A = P->GetEquipment(1);
	const auto& B = P->GetEquipment(2);
	if (!A.IsValid()) { return 1; }
	if (!B.IsValid()) { return 2; }
	return P->GetDisplayedPower(A) <= P->GetDisplayedPower(B) ? 1 : 2;
}

// ---------------------------------------------------------------- header / footer

void FullHeader(RPGUI::FCanvas& C, const FFullMenu& M)
{
	const auto* P = M.P;
	const int32 Pages[] = {ERPGBagPage::Equipment, ERPGBagPage::Items, ERPGBagPage::Upgrade, ERPGBagPage::Tune, ERPGBagPage::Settings};
	const TCHAR* Names[] = {TEXT("装備"), TEXT("バッグ"), TEXT("強化"), TEXT("チューン"), TEXT("設定")};
	float X = FFullMenu::Margin;
	X += RPGUI::Keycap(C, 0, M.Key(TEXT("Q"), TEXT("L")), X, 30, 24) + 8;
	const int32 Current = P->BagPage == ERPGBagPage::Equipment ? 0 : P->BagPage == ERPGBagPage::Items ? 1 : P->BagPage == ERPGBagPage::Upgrade ? 2 : P->BagPage == ERPGBagPage::Tune ? 3 : 4;
	for (int32 Tab = 0; Tab < 5; ++Tab)
	{
		const float TabW = 124, Y = 18;
		const bool bEnabled = Tab < 2 || Tab == 4 || P->IsAtMerchant();
		const int32 Action = Act::PageBase + Pages[Tab];
		if (bEnabled) { C.Hit(X, Y, TabW, 46, Action); }
		const RPGUI::EStyle Style = !bEnabled ? RPGUI::EStyle::Disabled : Tab == Current ? RPGUI::EStyle::Selected : C.IsHovered(Action) ? RPGUI::EStyle::Hover : RPGUI::EStyle::Card;
		RPGUI::Panel(C, 0, X, Y, TabW, 46, Style, Tab == Current ? RPGUI::Highlight : FLinearColor::Transparent);
		if (bEnabled) { RPGUI::Label(C, 0, Names[Tab], X + TabW * 0.5f + 2, Y + 9, 21, Tab == Current ? RPGUI::TextMain : RPGUI::TextSub, ETextAlign::Center); }
		else
		{
			RPGUI::Label(C, 0, Names[Tab], X + TabW * 0.5f + 2, Y + 4, 18, RPGUI::TextDim, ETextAlign::Center);
			RPGUI::Label(C, 0, TEXT("商人で使用"), X + TabW * 0.5f + 2, Y + 27, 11, RPGUI::TextDim, ETextAlign::Center);
		}
		if (Tab == 1 && P->GetUnseenCount() > 0)
		{ RPGUI::Chip(C, 1, FString::Printf(TEXT("NEW %d"), P->GetUnseenCount()), X + TabW - 2, Y - 9, RPGUI::NewColor, 11, true, ETextAlign::Right); }
		X += TabW + 6;
	}
	RPGUI::Keycap(C, 0, M.Key(TEXT("E"), TEXT("R")), X + 2, 30, 24);
	RPGUI::Button(C, 0, M.W - FFullMenu::Margin - 190, 18, 190, 46, TEXT("閉じる"), Act::Close, false, true, M.Key(TEXT("Tab"), TEXT("－")));
	RPGUI::Rubies(C, 0, P->GetRubies(), M.W - FFullMenu::Margin - 214, 28, 24, ETextAlign::Right);
	if (C.IsPainting())
	{
		const float Mid = M.W * 0.5f;
		C.At(0, 3).Box(FFullMenu::Margin, 76, Mid - FFullMenu::Margin, 2, WithAlpha(RPGUI::Cyan, 0.5f));
		C.At(0, 3).Box(Mid, 76, Mid - FFullMenu::Margin, 2, WithAlpha(FLinearColor(1.0f, 0.3f, 0.32f, 1.0f), 0.5f));
	}
}

void FullFooter(RPGUI::FCanvas& C, const FFullMenu& M, const TArray<TPair<FString, FString>>& Hints, const FString& Right = FString())
{
	float X = FFullMenu::Margin;
	const float Y = M.H - 40;
	for (const auto& Hint : Hints)
	{
		X += RPGUI::Keycap(C, 0, Hint.Key, X, Y, 24) + 6;
		RPGUI::Label(C, 0, Hint.Value, X, Y + 2, 15, RPGUI::TextSub);
		X += RPGUI::Measure(Hint.Value, 15) + 20;
	}
	if (!Right.IsEmpty()) { RPGUI::Label(C, 0, Right, M.W - FFullMenu::Margin, Y + 2, 15, RPGUI::TextDim, ETextAlign::Right); }
}

// ---------------------------------------------------------------- item detail

/** Name, icon and tags of an item. Returns the y below it. */
float FullItemHeader(RPGUI::FCanvas& C, const FFullMenu& M, const FRPGPrototypeItem& I, const float X, const float Y, const float W)
{
	RPGUI::ItemIcon(C, 1, FVector2D(X + 40, Y + 40), 27, I);
	const FLinearColor Name = I.Rarity == ERPGPrototypeRarity::Normal ? RPGUI::TextMain : RPGUI::RarityColor(I.Rarity);
	RPGUI::Label(C, 1, RPGUI::Fit(I.Name(), W - 100, 28, TEXT("Black")), X + 84, Y + 4, 28, Name, ETextAlign::Left, TEXT("Black"));
	float ChipX = X + 84;
	const float ChipY = Y + 48;
	ChipX += RPGUI::Chip(C, 1, RPGUI::RarityName(I.Rarity), ChipX, ChipY, RPGUI::RarityColor(I.Rarity), 13, true) + 6;
	ChipX += RPGUI::Chip(C, 1, RPGEquipment::KindName(I.Kind), ChipX, ChipY, RPGUI::TextSub, 13) + 6;
	ChipX += RPGUI::Chip(C, 1, FString::Printf(TEXT("Lv.%d"), I.Level), ChipX, ChipY, RPGUI::TextSub, 13) + 6;
	ChipX += RPGUI::Chip(C, 1, FString::Printf(TEXT("+%d"), I.Enhancement), ChipX, ChipY, I.Enhancement > 0 ? RPGUI::Highlight : RPGUI::TextSub, 13) + 6;
	if (M.P->IsEquipped(I.Id)) { ChipX += RPGUI::Chip(C, 1, TEXT("装備中"), ChipX, ChipY, RPGUI::Highlight, 13) + 6; }
	if (I.bLocked) { ChipX += RPGUI::Chip(C, 1, TEXT("ロック"), ChipX, ChipY, RPGUI::Cyan, 13) + 6; }
	if (M.P->IsUnseen(I.Id)) { RPGUI::Chip(C, 1, TEXT("NEW"), ChipX, ChipY, RPGUI::NewColor, 13, true); }
	return Y + 86;
}

/** Full description of an item in the right panel, above ButtonsTop. */
void FullItemDetail(RPGUI::FCanvas& C, const FFullMenu& M, const FRPGPrototypeItem& I, const float X, const float Y, const float W, const float ButtonsTop)
{
	const auto* P = M.P;
	const float Pad = 22, Inner = W - Pad * 2;
	float At = FullItemHeader(C, M, I, X + Pad - 6, Y + 14, W - Pad * 2);
	if (I.Kind == ERPGItemKind::Ball)
	{
		// The big number is the baseline power (level, rarity, enhancement, Damage tune, equipped power bonuses).
		const FString Power = FString::Printf(TEXT("%.0f"), P->GetDisplayedPower(I));
		if (C.IsPainting()) { HudText3D(C.At(1, 6), Power, X + Pad, At - 6, 44, RPGUI::TextMain); }
		const float PowerW = RPGUI::Measure(Power, 44, TEXT("BlackItalic"));
		RPGUI::Label(C, 1, TEXT("攻撃パワー"), X + Pad + PowerW + 12, At + 4, 14, RPGUI::TextSub);
		RPGUI::Label(C, 1, FString::Printf(TEXT("再使用 %.2f秒"), I.EffectiveCooldown()), X + Pad + PowerW + 12, At + 24, 16, RPGUI::Cyan);
		RPGUI::Label(C, 1, RPGUI::Fit(RPGTuning::BallArchetype(I.Type).Role, Inner - PowerW - 150, 14), X + W - Pad, At + 26, 14, RPGUI::TextDim, ETextAlign::Right);
		At += 62;
	}
	// Against what is equipped now (the slot it would replace).
	const int32 Slot = CompareSlot(P, I);
	const auto& Current = P->GetEquipment(Slot);
	if (!P->IsEquipped(I.Id) || (P->bChoosingEquipment && Current.Id != I.Id))
	{
		const FString SlotName = ARPGPrototypeCharacter::EquipmentLabel(Slot);
		if (C.IsPainting()) { RoundedBox(C.At(1, 2), X + Pad - 6, At - 4, Inner + 12, 30, 6, FLinearColor(1, 1, 1, 0.045f)); }
		if (I.Kind == ERPGItemKind::Ball && Current.IsValid())
		{
			const float From = P->GetDisplayedPower(Current), To = P->GetDisplayedPower(I);
			const FString Text = FString::Printf(TEXT("%s の %s と比べて  パワー %.0f → %.0f"), *SlotName, *Current.Name(), From, To);
			RPGUI::Label(C, 1, RPGUI::Fit(Text, Inner - 70, 15), X + Pad, At, 15, RPGUI::TextSub);
			RPGUI::Delta(C, 1, To - From, X + W - Pad, At - 1, 17, ETextAlign::Right);
		}
		else
		{
			const FString Text = Current.IsValid() ? FString::Printf(TEXT("%s 装備中：%s（%s）"), *SlotName, *Current.Name(), *RPGUI::ShortEffect(Current))
				: FString::Printf(TEXT("%s は空いています"), *SlotName);
			RPGUI::Label(C, 1, RPGUI::Fit(Text, Inner, 15), X + Pad, At, 15, Current.IsValid() ? RPGUI::TextSub : RPGUI::Good);
		}
		At += 38;
	}
	// Effects, scrolled one section at a time (wheel / PgUp PgDn / right stick).
	const TArray<RPGUI::FDetailSection> Sections = RPGUI::DetailSections(I);
	const int32 First = FMath::Clamp(P->DetailScroll, 0, FMath::Max(0, Sections.Num() - 1));
	int32 Shown = 0;
	for (int32 Index = First; Index < Sections.Num(); ++Index)
	{
		const auto& Section = Sections[Index];
		const TArray<FString> Lines = RPGUI::WrapLines(Section.Body, Inner, 16);
		const float Needed = 24 + FMath::Min(3, Lines.Num()) * 23;
		if (Shown > 0 && At + Needed > ButtonsTop - 10) { break; }
		RPGUI::Label(C, 1, Section.Title, X + Pad, At, 15, Section.Color);
		At += 24;
		At += RPGUI::Wrap(C, 1, Section.Body, X + Pad, At, Inner, 16, Section.bEmpty ? RPGUI::TextDim : RPGUI::TextMain, 3, 23) * 23 + 12;
		++Shown;
	}
	if (First > 0 || First + Shown < Sections.Num())
	{
		RPGUI::Button(C, 1, X + W - Pad - 96, ButtonsTop - 40, 44, 30, TEXT("▲"), Act::DetailUp, false, First > 0);
		RPGUI::Button(C, 1, X + W - Pad - 46, ButtonsTop - 40, 44, 30, TEXT("▼"), Act::DetailDown, false, First + Shown < Sections.Num());
		RPGUI::Label(C, 1, FString::Printf(TEXT("効果 %d-%d / %d"), First + 1, First + Shown, Sections.Num()), X + W - Pad - 106, ButtonsTop - 35, 13, RPGUI::TextDim, ETextAlign::Right);
	}
}

// ---------------------------------------------------------------- item list

void FullItemList(RPGUI::FCanvas& C, const FFullMenu& M)
{
	const auto* P = M.P;
	const float X = FFullMenu::Margin, W = M.ListW();
	float Y = FFullMenu::ContentTop;
	if (P->bChoosingEquipment)
	{
		RPGUI::Label(C, 0, ARPGPrototypeCharacter::EquipmentLabel(P->EquipmentSelection) + TEXT(" を変更"), X + 4, Y + 4, 24, RPGUI::TextMain, ETextAlign::Left, TEXT("Black"));
		RPGUI::Button(C, 0, X + W - 150, Y, 150, 40, TEXT("戻る"), Act::CancelChange, false, true, M.Key(TEXT("Esc"), TEXT("戻る")));
	}
	else
	{
		const int32 Filters[] = {ERPGBagFilter::Ball, ERPGBagFilter::Gear, ERPGBagFilter::Badge, ERPGBagFilter::Material};
		const TCHAR* Names[] = {TEXT("ボール"), TEXT("ギア"), TEXT("バッジ"), TEXT("素材")};
		const float TabW = (W - 18) / 4;
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const float TabX = X + Index * (TabW + 6);
			const int32 Action = Act::FilterBase + Index;
			const bool bOn = P->BagFilter == Filters[Index];
			C.Hit(TabX, Y, TabW, 40, Action);
			RPGUI::Panel(C, 0, TabX, Y, TabW, 40, bOn ? RPGUI::EStyle::Selected : C.IsHovered(Action) ? RPGUI::EStyle::Hover : RPGUI::EStyle::Card, bOn ? RPGUI::Highlight : FLinearColor::Transparent);
			const FString Text = FString::Printf(TEXT("%s %d"), Names[Index], P->CountInFilter(Filters[Index]));
			RPGUI::Label(C, 0, Text, TabX + TabW * 0.5f + 2, Y + 9, 17, bOn ? RPGUI::TextMain : RPGUI::TextSub, ETextAlign::Center);
			if (P->CountInFilter(Filters[Index], true) > 0 && C.IsPainting()) { C.At(1, 4).Disc(FVector2D(TabX + TabW - 12, Y + 9), 5, RPGUI::NewColor); }
		}
	}
	Y += 50;
	// Tools: order, capacity, bulk salvage.
	if (!P->bChoosingEquipment)
	{
		RPGUI::Button(C, 0, X, Y, 210, 34, TEXT("並び：") + ARPGPrototypeCharacter::BagSortName(P->BagSort), Act::Sort, false, true, M.Key(TEXT("X"), TEXT("ZL")));
		const int32 Count = P->GetBag().Num();
		const bool bFull = Count >= RPGTuning::BagCapacity - 5;
		if (P->BagPage == ERPGBagPage::Items)
		{
			RPGUI::Button(C, 0, X + W - 196, Y, 196, 34, TEXT("まとめて分解"), Act::BulkSalvage, false, true, M.Key(TEXT("BS"), TEXT("ZR")));
			RPGUI::Label(C, 0, FString::Printf(TEXT("所持 %d/%d"), Count, RPGTuning::BagCapacity), X + W - 206, Y + 8, 15, bFull ? RPGUI::Bad : RPGUI::TextDim, ETextAlign::Right);
		}
		else { RPGUI::Label(C, 0, FString::Printf(TEXT("所持 %d/%d"), Count, RPGTuning::BagCapacity), X + W, Y + 8, 15, bFull ? RPGUI::Bad : RPGUI::TextDim, ETextAlign::Right); }
	}
	Y += 44;
	const TArray<int32> List = P->FilteredBag();
	const int32 Rows = URPGPrototypeHUD::FullMenuRows;
	const int32 Start = FMath::Max(0, List.Find(P->BagSelection)) / Rows * Rows;
	if (C.IsPainting() && List.IsEmpty())
	{
		RPGUI::Panel(C, 0, X, Y, W - 14, 120, RPGUI::EStyle::Panel);
		RPGUI::Label(C, 0, P->bChoosingEquipment ? TEXT("この枠に付けられる持ち物がありません") : TEXT("この種類の持ち物はありません"), X + (W - 14) * 0.5f, Y + 32, 18, RPGUI::TextSub, ETextAlign::Center);
		RPGUI::Label(C, 0, TEXT("敵のドロップや宝箱で手に入ります"), X + (W - 14) * 0.5f, Y + 64, 14, RPGUI::TextDim, ETextAlign::Center);
	}
	for (int32 Row = 0; Row < Rows && Start + Row < List.Num(); ++Row)
	{
		const int32 Index = List[Start + Row];
		const auto& I = P->GetBag()[Index];
		const float RowY = Y + Row * FFullMenu::RowH, RowW = W - 14;
		const int32 Action = Act::RowBase + Row;
		const bool bSelected = Index == P->BagSelection;
		C.Hit(X, RowY, RowW, FFullMenu::RowDraw, Action);
		if (!C.IsPainting()) { continue; }
		const FLinearColor Rarity = RPGUI::RarityColor(I.Rarity);
		RPGUI::Panel(C, 1, X, RowY, RowW, FFullMenu::RowDraw, bSelected ? RPGUI::EStyle::Selected : C.IsHovered(Action) ? RPGUI::EStyle::Hover : RPGUI::EStyle::Card,
			bSelected ? RPGUI::Highlight : FLinearColor::Transparent, Rarity);
		RPGUI::ItemIcon(C, 1, FVector2D(X + 34, RowY + 23), 15, I);
		float Right = X + RowW - 18;
		if (I.Kind == ERPGItemKind::Ball)
		{
			const float Power = P->GetDisplayedPower(I);
			RPGUI::Label(C, 1, FString::Printf(TEXT("%.0f"), Power), Right, RowY + 2, 22, RPGUI::Highlight, ETextAlign::Right);
			const auto& Current = P->GetEquipment(CompareSlot(P, I));
			if (!P->IsEquipped(I.Id) && Current.IsValid()) { RPGUI::Delta(C, 1, Power - P->GetDisplayedPower(Current), Right, RowY + 27, 12, ETextAlign::Right); }
			else if (!P->IsEquipped(I.Id)) { RPGUI::Label(C, 1, TEXT("空き枠"), Right, RowY + 27, 12, RPGUI::Good, ETextAlign::Right); }
			Right -= 64;
		}
		if (P->IsUnseen(I.Id)) { Right -= RPGUI::Chip(C, 1, TEXT("NEW"), Right, RowY + 13, RPGUI::NewColor, 11, true, ETextAlign::Right) + 5; }
		if (P->IsEquipped(I.Id)) { Right -= RPGUI::Chip(C, 1, TEXT("装備中"), Right, RowY + 13, RPGUI::Highlight, 11, false, ETextAlign::Right) + 5; }
		if (I.bLocked) { Right -= RPGUI::Chip(C, 1, TEXT("ロック"), Right, RowY + 13, RPGUI::Cyan, 11, false, ETextAlign::Right) + 5; }
		const float TextX = X + 60, TextW = Right - TextX - 6;
		RPGUI::Label(C, 1, RPGUI::Fit(I.Name(), TextW, 18), TextX, RowY + 3, 18, I.Rarity == ERPGPrototypeRarity::Normal ? RPGUI::TextMain : Rarity);
		const FString Sub = I.Kind == ERPGItemKind::Ball ? FString::Printf(TEXT("Lv.%d  +%d  %s"), I.Level, I.Enhancement, RPGUI::RarityName(I.Rarity))
			: FString::Printf(TEXT("Lv.%d  +%d  %s"), I.Level, I.Enhancement, *RPGUI::ShortEffect(I));
		RPGUI::Label(C, 1, RPGUI::Fit(Sub, TextW, 12), TextX, RowY + 27, 12, RPGUI::TextSub);
	}
	RPGUI::ScrollBar(C, 0, X + W - 7, Y, Rows * FFullMenu::RowH - 6, Start, Rows, List.Num());
	const float PagerY = Y + Rows * FFullMenu::RowH + 4;
	const int32 Pages = FMath::Max(1, FMath::DivideAndRoundUp(List.Num(), Rows));
	RPGUI::Button(C, 0, X, PagerY, 64, 36, TEXT("‹"), Act::ListPrev, false, Start > 0);
	RPGUI::Button(C, 0, X + W - 78, PagerY, 64, 36, TEXT("›"), Act::ListNext, false, Start + Rows < List.Num());
	RPGUI::Label(C, 0, FString::Printf(TEXT("%d / %d ページ"), Start / Rows + 1, Pages), X + (W - 14) * 0.5f, PagerY + 7, 17, RPGUI::TextSub, ETextAlign::Center);
}

// ---------------------------------------------------------------- pages

void FullOverview(RPGUI::FCanvas& C, const FFullMenu& M)
{
	const auto* P = M.P;
	const float Left = FFullMenu::Margin, Right = M.W * 0.58f, Width = Right - Left;
	const TCHAR* Groups[] = {TEXT("ボール"), TEXT("ギア"), TEXT("バッジ")};
	const float GroupY[] = {100, 256, 398}, CardH[] = {118, 104, 104};
	const int32 First[] = {0, 3, 7}, Count[] = {3, 4, 3};
	for (int32 Group = 0; Group < 3; ++Group)
	{
		RPGUI::Section(C, 0, Groups[Group], Left + 4, GroupY[Group], Width - 4);
		const float CardW = (Width - (Count[Group] - 1) * 12) / Count[Group], Y = GroupY[Group] + 24;
		for (int32 Column = 0; Column < Count[Group]; ++Column)
		{
			const int32 Slot = First[Group] + Column, Action = Act::SlotBase + Slot;
			const float X = Left + Column * (CardW + 12), H = CardH[Group];
			const auto& Item = P->GetEquipment(Slot);
			const bool bSelected = P->EquipmentSelection == Slot;
			C.Hit(X, Y, CardW, H, Action);
			if (!C.IsPainting()) { continue; }
			RPGUI::Panel(C, 0, X, Y, CardW, H, bSelected ? RPGUI::EStyle::Selected : C.IsHovered(Action) ? RPGUI::EStyle::Hover : RPGUI::EStyle::Card,
				bSelected ? RPGUI::Highlight : FLinearColor::Transparent, Item.IsValid() ? RPGUI::RarityColor(Item.Rarity) : FLinearColor::Transparent);
			RPGUI::Label(C, 0, ARPGPrototypeCharacter::EquipmentLabel(Slot), X + 18, Y + 8, 13, RPGUI::TextSub);
			if (Slot < 3)
			{
				RPGUI::Label(C, 0, Item.IsValid() ? FString::Printf(TEXT("%.0f"), P->GetDisplayedPower(Item)) : TEXT("—"), X + CardW - 16, Y + 4, 22, RPGUI::Highlight, ETextAlign::Right);
			}
			if (Item.IsValid()) { RPGUI::ItemIcon(C, 0, FVector2D(X + CardW * 0.5f, Y + 48), 20, Item); }
			else
			{
				if (C.IsPainting()) { C.At(0, 3).Disc(FVector2D(X + CardW * 0.5f, Y + 48), 26, FLinearColor(0, 0, 0, 0.35f)); }
				RPGUI::SlotGlyph(C, 0, FVector2D(X + CardW * 0.5f, Y + 48), 17, Slot, true);
			}
			const FLinearColor NameColor = !Item.IsValid() ? RPGUI::TextDim : Item.Rarity == ERPGPrototypeRarity::Normal ? RPGUI::TextMain : RPGUI::RarityColor(Item.Rarity);
			RPGUI::Label(C, 0, RPGUI::Fit(Item.IsValid() ? Item.Name() : TEXT("未装備"), CardW - 24, 15), X + CardW * 0.5f, Y + H - (Slot < 3 ? 44 : 30), 15, NameColor, ETextAlign::Center);
			if (Slot < 3 && Item.IsValid()) { RPGUI::Label(C, 0, FString::Printf(TEXT("再使用 %.1f秒"), Item.EffectiveCooldown()), X + CardW * 0.5f, Y + H - 23, 12, RPGUI::TextDim, ETextAlign::Center); }
		}
	}
	// What the whole loadout adds up to (only the always-on numbers; conditional effects are in each item).
	const float SummaryY = 540;
	RPGUI::Section(C, 0, TEXT("装備の合計（常時の効果）"), Left + 4, SummaryY, Width - 4);
	struct FStat { const TCHAR* Name; FString Value; bool bShow; };
	const float Armor = P->BadgeMod(TEXT("Armor")), Move = P->BadgeMod(TEXT("Move")), Power = P->BadgeMod(TEXT("AllPower"));
	const float SpecialCD = P->BadgeMod(TEXT("SpecialCD")), Stamina = P->BadgeMod(TEXT("Stamina")), Dash = P->BadgeMod(TEXT("DashCD"));
	const FStat Stats[] = {
		{TEXT("最大HP"), FString::Printf(TEXT("%.0f"), P->GetMaxHealth()), true},
		{TEXT("全攻撃"), FString::Printf(TEXT("+%.0f%%"), Power), true},
		{TEXT("被ダメージ"), Armor > 0 ? FString::Printf(TEXT("-%.0f%%"), FMath::Min(Armor, RPGTuning::DamageReductionCap * 100)) : FString(TEXT("±0%")), true},
		{TEXT("移動"), FString::Printf(TEXT("+%.0f%%"), Move), true},
		{TEXT("特殊球CT"), FString::Printf(TEXT("-%.0f%%"), SpecialCD), SpecialCD > 0},
		{TEXT("スタミナ回復"), FString::Printf(TEXT("+%.0f%%"), Stamina), Stamina > 0},
		{TEXT("ダッシュCT"), FString::Printf(TEXT("-%.0f%%"), Dash), Dash > 0},
	};
	float StatX = Left + 4, StatY = SummaryY + 30;
	for (const FStat& Stat : Stats)
	{
		if (!Stat.bShow) { continue; }
		const FString Text = FString::Printf(TEXT("%s  %s"), Stat.Name, *Stat.Value);
		const float ChipW = RPGUI::Measure(Text, 15) + 16.5f;
		if (StatX + ChipW > Right) { StatX = Left + 4; StatY += 32; }
		StatX += RPGUI::Chip(C, 0, Text, StatX, StatY, RPGUI::TextSub, 15) + 8;
	}
	// Right: the selected slot.
	const float X = Right + 24, W = M.W - FFullMenu::Margin - X, Y = FFullMenu::ContentTop, Bottom = M.ContentBottom();
	const auto& Item = P->GetEquipment(P->EquipmentSelection);
	RPGUI::Panel(C, 0, X, Y, W, Bottom - Y, RPGUI::EStyle::Panel, Item.IsValid() ? RPGUI::RarityColor(Item.Rarity) : WithAlpha(RPGUI::Cyan, 0.5f));
	const float ButtonY = Bottom - 66;
	if (Item.IsValid()) { FullItemDetail(C, M, Item, X, Y, W, ButtonY); }
	else
	{
		RPGUI::Label(C, 1, ARPGPrototypeCharacter::EquipmentLabel(P->EquipmentSelection), X + 22, Y + 22, 28, RPGUI::TextMain, ETextAlign::Left, TEXT("Black"));
		RPGUI::Label(C, 1, TEXT("未装備"), X + 22, Y + 64, 18, RPGUI::TextDim);
		int32 Fits = 0;
		for (const auto& Candidate : P->GetBag()) { Fits += P->FitsEquipmentSlot(Candidate, P->EquipmentSelection) ? 1 : 0; }
		RPGUI::Wrap(C, 1, Fits > 0 ? FString::Printf(TEXT("この枠に付けられる持ち物が %d個 あります。「装備変更」で選べます。"), Fits)
			: TEXT("この枠に付けられる持ち物はまだありません。敵のドロップや宝箱で集めましょう。"), X + 22, Y + 104, W - 44, 16, RPGUI::TextSub, 4, 24);
	}
	RPGUI::Button(C, 1, X + 22, ButtonY, W - 44, 46, TEXT("装備変更"), Act::BeginChange, true, true, M.Key(TEXT("Enter"), TEXT("決定")));
}

void FullItemsPage(RPGUI::FCanvas& C, const FFullMenu& M)
{
	const auto* P = M.P;
	FullItemList(C, M);
	const float X = M.DetailX(), W = M.DetailW(), Y = FFullMenu::ContentTop, Bottom = M.ContentBottom();
	const FRPGPrototypeItem* I = SelectedBagItem(M);
	RPGUI::Panel(C, 0, X, Y, W, Bottom - Y, RPGUI::EStyle::Panel, I ? RPGUI::RarityColor(I->Rarity) : WithAlpha(RPGUI::Cyan, 0.5f));
	if (!I)
	{
		RPGUI::Label(C, 1, TEXT("持ち物を選んでください"), X + W * 0.5f, Y + 120, 20, RPGUI::TextDim, ETextAlign::Center);
		return;
	}
	const float Inner = W - 44, Row1 = Bottom - 62, Row2 = Bottom - 114;
	FullItemDetail(C, M, *I, X, Y, W, P->bChoosingEquipment ? Row1 : Row2);
	const bool bEquipped = P->IsEquipped(I->Id);
	const FString Enter = M.Key(TEXT("Enter"), TEXT("決定"));
	if (P->bChoosingEquipment)
	{
		const bool bAlready = P->GetEquipment(P->EquipmentSelection).Id == I->Id;
		RPGUI::Button(C, 1, X + 22, Row1, Inner, 46, bAlready ? TEXT("この枠に装備中") : TEXT("この装備に変更"), Act::EquipSelected, true, !bAlready, Enter);
		return;
	}
	if (I->Kind == ERPGItemKind::Ball && I->Type != EChaosImpactBallType::Normal)
	{
		const float Half = (Inner - 10) * 0.5f;
		for (int32 Slot = 1; Slot <= 2; ++Slot)
		{
			const bool bHere = P->GetAttack(Slot).Id == I->Id;
			const bool bTarget = P->bChoosingDestination && P->EquipTargetSlot == Slot;
			RPGUI::Button(C, 1, X + 22 + (Slot - 1) * (Half + 10), Row1, Half, 46, bHere ? FString::Printf(TEXT("特殊球%d に装備中"), Slot) : FString::Printf(TEXT("特殊球%d に装備"), Slot),
				Slot == 1 ? Act::EquipSpecial1 : Act::EquipSpecial2, !P->bChoosingDestination || bTarget, !bHere, (bTarget || (!P->bChoosingDestination && Slot == 1)) ? Enter : FString());
		}
	}
	else
	{
		RPGUI::Button(C, 1, X + 22, Row1, Inner, 46, bEquipped ? TEXT("装備中") : TEXT("装備する"), Act::EquipSelected, true, !bEquipped, Enter);
	}
	const float Half = (Inner - 10) * 0.5f;
	RPGUI::Button(C, 1, X + 22, Row2, Half, 42, I->bLocked ? TEXT("ロックを外す") : TEXT("ロックする"), Act::Lock, false, true, M.Key(TEXT("F"), TEXT("上")));
	RPGUI::Button(C, 1, X + 32 + Half, Row2, Half, 42, bEquipped ? TEXT("装備中は分解不可") : I->bLocked ? TEXT("ロック中") : FString::Printf(TEXT("分解  ◆+%d"), I->SalvageValue()),
		Act::Salvage, false, !bEquipped && !I->bLocked, M.Key(TEXT("Del"), TEXT("左")));
}

void FullUpgradePage(RPGUI::FCanvas& C, const FFullMenu& M)
{
	const auto* P = M.P;
	FullItemList(C, M);
	const float X = M.DetailX(), W = M.DetailW(), Y = FFullMenu::ContentTop, Bottom = M.ContentBottom();
	const FRPGPrototypeItem* I = SelectedBagItem(M);
	RPGUI::Panel(C, 0, X, Y, W, Bottom - Y, RPGUI::EStyle::Panel, RPGUI::Highlight);
	if (!I) { RPGUI::Label(C, 1, TEXT("強化する持ち物を選んでください"), X + W * 0.5f, Y + 120, 20, RPGUI::TextDim, ETextAlign::Center); return; }
	float At = FullItemHeader(C, M, *I, X + 16, Y + 14, W - 44);
	const bool bMax = I->Rarity != ERPGPrototypeRarity::Legend && I->Enhancement >= RPGTuning::EnhanceCap;
	RPGUI::Section(C, 1, TEXT("ルビー強化"), X + 22, At, W - 44, RPGUI::Highlight);
	At += 36;
	if (C.IsPainting())
	{
		HudText3D(C.At(1, 6), FString::Printf(TEXT("+%d"), I->Enhancement), X + W * 0.28f, At, 54, RPGUI::TextMain, ETextAlign::Center);
		HudText3D(C.At(1, 6), TEXT("→"), X + W * 0.5f, At + 6, 40, RPGUI::Cyan, ETextAlign::Center);
		HudText3D(C.At(1, 6), bMax ? TEXT("MAX") : FString::Printf(TEXT("+%d"), I->Enhancement + 1), X + W * 0.72f, At, 54, RPGUI::Highlight, ETextAlign::Center);
	}
	At += 84;
	auto Next = *I;
	if (!bMax && Next.Enhancement < MAX_int32) { ++Next.Enhancement; }
	if (I->Kind == ERPGItemKind::Ball)
	{
		const float From = P->GetDisplayedPower(*I), To = P->GetDisplayedPower(Next);
		RPGUI::Label(C, 1, FString::Printf(TEXT("攻撃パワー  %.0f → %.0f"), From, To), X + 22, At, 22, RPGUI::TextMain);
		RPGUI::Delta(C, 1, To - From, X + W - 22, At + 2, 20, ETextAlign::Right);
	}
	else
	{
		RPGUI::Label(C, 1, TEXT("いま"), X + 22, At, 14, RPGUI::TextDim);
		RPGUI::Wrap(C, 1, RPGUI::ActualDescription(*I, I->Badge), X + 80, At - 2, W - 102, 17, RPGUI::TextMain, 2, 22);
		RPGUI::Label(C, 1, TEXT("強化後"), X + 22, At + 48, 14, RPGUI::Good);
		RPGUI::Wrap(C, 1, RPGUI::ActualDescription(Next, Next.Badge), X + 80, At + 46, W - 102, 17, RPGUI::Good, 2, 22);
		At += 48;
	}
	At += 50;
	RPGUI::Wrap(C, 1, I->Rarity == ERPGPrototypeRarity::Legend ? TEXT("レジェンドは強化回数の上限なし（+10を越えると伸びは小さくなり、範囲・個数・足止め時間は増えません）。")
		: FString::Printf(TEXT("ノーマル・レア・ユニークは +%d まで強化できます。"), RPGTuning::EnhanceCap), X + 22, At, W - 44, 15, RPGUI::TextSub, 3, 22);
	RPGUI::Label(C, 1, TEXT("失敗・装備破壊はありません。ルビーだけを使います。"), X + 22, At + 52, 15, RPGUI::TextDim);
	const int32 Cost = I->UpgradeCost();
	const bool bAfford = P->GetRubies() >= Cost;
	const FString Text = bMax ? TEXT("強化MAX") : bAfford ? FString::Printf(TEXT("強化する  ◆%s"), *FString::FormatAsNumber(Cost))
		: FString::Printf(TEXT("ルビー不足（あと ◆%s）"), *FString::FormatAsNumber(Cost - P->GetRubies()));
	RPGUI::Button(C, 1, X + 22, Bottom - 62, W - 44, 46, Text, Act::Upgrade, true, !bMax && bAfford, M.Key(TEXT("Enter"), TEXT("決定")));
}

void FullTunePage(RPGUI::FCanvas& C, const FFullMenu& M)
{
	const auto* P = M.P;
	FullItemList(C, M);
	const float X = M.DetailX(), W = M.DetailW(), Y = FFullMenu::ContentTop, Bottom = M.ContentBottom(), Inner = W - 44;
	const FRPGPrototypeItem* I = SelectedBagItem(M);
	RPGUI::Panel(C, 0, X, Y, W, Bottom - Y, RPGUI::EStyle::Panel, RPGUI::Highlight);
	if (!I) { RPGUI::Label(C, 1, TEXT("チューンするボールを選んでください"), X + W * 0.5f, Y + 120, 20, RPGUI::TextDim, ETextAlign::Center); return; }
	float At = FullItemHeader(C, M, *I, X + 16, Y + 14, W - 44);
	if (I->Kind != ERPGItemKind::Ball)
	{
		RPGUI::Wrap(C, 1, TEXT("チューンはボール専用です。ギア・バッジは「強化」で効果値を伸ばせます。"), X + 22, At + 10, Inner, 17, RPGUI::TextSub, 3, 26);
		return;
	}
	// Sockets: two specific (主) and one common (副).
	const float SocketW = (Inner - 16) / 3;
	for (int32 Socket = 0; Socket < 3; ++Socket)
	{
		const float SX = X + 22 + Socket * (SocketW + 8);
		const int32 Action = Act::SocketBase + Socket;
		const bool bOn = P->TuneSocket == Socket;
		const FRPGAffix Affix = I->Tunes.IsValidIndex(Socket) ? I->Tunes[Socket] : FRPGAffix{};
		const auto* Definition = RPGEquipment::Find(Affix.Key);
		C.Hit(SX, At, SocketW, 54, Action);
		RPGUI::Panel(C, 1, SX, At, SocketW, 54, bOn ? RPGUI::EStyle::Selected : C.IsHovered(Action) ? RPGUI::EStyle::Hover : RPGUI::EStyle::Card, bOn ? RPGUI::Highlight : FLinearColor::Transparent);
		RPGUI::Label(C, 1, Socket < 2 ? FString::Printf(TEXT("専用%d"), Socket + 1) : FString(TEXT("共通")), SX + 16, At + 5, 12, Socket < 2 ? RPGUI::Highlight : RPGUI::Cyan);
		RPGUI::Label(C, 1, RPGUI::Fit(Definition ? FString::Printf(TEXT("%s  R%d"), *Definition->Name, Affix.Rank) : FString(TEXT("未装着")), SocketW - 26, 16),
			SX + 16, At + 25, 16, Definition ? RPGUI::TextMain : RPGUI::TextDim);
	}
	At += 66;
	const auto& Pool = RPGEquipment::Tunes(I->Type);
	const auto Offers = P->GetTuneOffers(P->BagSelection, P->TuneSocket);
	const int32 Choice = FMath::Clamp(P->TuneSelection, 0, FMath::Max(0, Offers.Num() - 1));
	// What the three sockets are, in one line for first-time players.
	RPGUI::Label(C, 1, RPGUI::Fit(P->TuneSocket < 2 ? TEXT("専用：このボールだけの改造（2つまで）。下の3つから選んで付けます。") : TEXT("共通：威力・速さ・射程など、どのボールにも付く改造（1つ）。"), Inner, 14), X + 22, At, 14, RPGUI::TextSub);
	At += 26;
	RPGUI::Section(C, 1, TEXT("付けられる改造（効果の数値は付けると分かる）"), X + 22, At, Inner);
	At += 26;
	for (int32 Row = 0; Row < Offers.Num(); ++Row)
	{
		const auto& Definition = Pool[Offers[Row]];
		const int32 Action = Act::OfferBase + Row;
		const bool bOn = Choice == Row;
		C.Hit(X + 22, At, Inner, 62, Action);
		RPGUI::Panel(C, 1, X + 22, At, Inner, 62, bOn ? RPGUI::EStyle::Selected : C.IsHovered(Action) ? RPGUI::EStyle::Hover : RPGUI::EStyle::Card, bOn ? RPGUI::Highlight : FLinearColor::Transparent);
		RPGUI::Label(C, 1, Definition.Name, X + 42, At + 6, 18, RPGUI::TextMain);
		RPGUI::Label(C, 1, RPGUI::Fit(RPGUI::HiddenDescription(Definition), Inner - 40, 14), X + 42, At + 34, 14, RPGUI::TextSub);
		At += 68;
	}
	if (Offers.IsEmpty()) { RPGUI::Label(C, 1, TEXT("この枠に付けられる候補はもうありません"), X + 22, At + 6, 16, RPGUI::TextDim); At += 40; }
	const FRPGAffix Current = I->Tunes.IsValidIndex(P->TuneSocket) ? I->Tunes[P->TuneSocket] : FRPGAffix{};
	const auto* Existing = RPGEquipment::Find(Current.Key);
	At += 4;
	RPGUI::Label(C, 1, Existing ? FString::Printf(TEXT("装着中：%s  ランク%d/%d"), *Existing->Name, Current.Rank, RPGTuning::TuneMaxRank) : FString(TEXT("装着中：なし")), X + 22, At, 15, Existing ? RPGUI::Highlight : RPGUI::TextDim);
	if (Existing) { RPGUI::Wrap(C, 1, RPGEquipment::Describe(Current), X + 22, At + 22, Inner, 15, RPGUI::TextSub, 2, 21); }
	const float Half = (Inner - 10) * 0.5f, ButtonY = Bottom - 62;
	const bool bCanRank = Existing && Current.Rank < RPGTuning::TuneMaxRank;
	const int32 RankCost = I->TuneCost() * FMath::Max(1, Current.Rank);
	RPGUI::Button(C, 1, X + 22, ButtonY, Half, 46, !Existing ? TEXT("ランクUP（未装着）") : !bCanRank ? TEXT("ランクMAX") : FString::Printf(TEXT("ランクUP  ◆%d"), RankCost),
		Act::RankUp, false, bCanRank && P->GetRubies() >= RankCost, M.Key(TEXT("Space"), TEXT("上")));
	RPGUI::Button(C, 1, X + 32 + Half, ButtonY, Half, 46, FString::Printf(TEXT("付ける  ◆%d"), I->TuneCost()), Act::ApplyTune, true,
		Offers.Num() > 0 && P->GetRubies() >= I->TuneCost(), M.Key(TEXT("Enter"), TEXT("決定")));
}

void FullSettingsPage(RPGUI::FCanvas& C, const FFullMenu& M)
{
	const auto* P = M.P;
	const float X = FFullMenu::Margin, Y = FFullMenu::ContentTop, Bottom = M.ContentBottom();
	RPGUI::Panel(C, 0, X, Y, 360, Bottom - Y, RPGUI::EStyle::Panel, WithAlpha(RPGUI::Cyan, 0.6f));
	if (C.IsPainting()) { HudText3D(C.At(1, 6), FString::Printf(TEXT("%dP"), P->GetLocalRPGIndex() + 1), X + 26, Y + 18, 58, RPGUI::TextMain); }
	RPGUI::Label(C, 1, TEXT("自分のテストセーブ"), X + 26, Y + 100, 22, RPGUI::TextMain);
	RPGUI::Label(C, 1, TEXT("最終保存"), X + 26, Y + 140, 14, RPGUI::TextDim);
	RPGUI::Label(C, 1, P->SaveSummary, X + 26, Y + 160, 26, RPGUI::Highlight);
	RPGUI::Section(C, 1, TEXT("保存するもの"), X + 26, Y + 214, 308);
	RPGUI::Wrap(C, 1, TEXT("装備・所持品（ロック含む）・レベル・経験値・ルビー・HP・開けた宝箱・依頼の進み具合。"), X + 26, Y + 242, 308, 16, RPGUI::TextMain, 4, 24);
	RPGUI::Section(C, 1, TEXT("保存しないもの"), X + 26, Y + 352, 308);
	RPGUI::Wrap(C, 1, TEXT("ステージ・敵・落ちている物。他のプレイヤーのデータは変わりません。自動保存はありません。"), X + 26, Y + 380, 308, 16, RPGUI::TextSub, 4, 24);
	const int32 Count = M.Mode ? M.Mode->GetLocalPlayerCount() : 1;
	const bool bOwner = P->GetLocalRPGIndex() == 0;
	struct FRow { FString Name; FString Value; bool bEnabled; bool bToggle; };
	const FRow Rows[] = {
		{TEXT("テストセーブ"), M.Key(TEXT("F5"), TEXT("")), true, false},
		{TEXT("テストロード"), M.Key(TEXT("F9"), TEXT("")), true, false},
		{TEXT("メニュー表示"), P->UsesMiniMenu() ? TEXT("ミニメニュー") : TEXT("全体メニュー"), true, true},
		{TEXT("ローカル人数"), FString::Printf(TEXT("%d人"), Count), bOwner, true},
		{TEXT("マルチ画面"), M.Mode && M.Mode->IsSharedScreenRequested() ? TEXT("一画面共有") : TEXT("分割画面"), bOwner, true},
		{P->HeroId.IsEmpty() ? FString(TEXT("装備に戻る")) : FString(TEXT("保存してタイトルへ")), FString(), P->HeroId.IsEmpty() || bOwner, false},
	};
	const float RX = X + 384, RW = M.W - FFullMenu::Margin - RX;
	for (int32 Row = 0; Row < 6; ++Row)
	{
		const float RowY = Y + Row * 66;
		const int32 Action = Act::SettingBase + Row;
		const bool bOn = P->SettingsSelection == Row;
		const FRow& Item = Rows[Row];
		if (Item.bEnabled) { C.Hit(RX, RowY, RW, 56, Action); }
		RPGUI::Panel(C, 1, RX, RowY, RW, 56, !Item.bEnabled ? RPGUI::EStyle::Disabled : bOn ? RPGUI::EStyle::Selected : C.IsHovered(Action) ? RPGUI::EStyle::Hover : RPGUI::EStyle::Card,
			bOn ? RPGUI::Highlight : FLinearColor::Transparent, Row < 2 ? RPGUI::Highlight : WithAlpha(RPGUI::Cyan, 0.8f));
		RPGUI::Label(C, 1, Item.Name, RX + 30, RowY + 14, 20, Item.bEnabled ? RPGUI::TextMain : RPGUI::TextDim);
		if (Item.bToggle)
		{
			RPGUI::Label(C, 1, FString::Printf(TEXT("‹  %s  ›"), *Item.Value), RX + RW - 30, RowY + 14, 20, Item.bEnabled ? RPGUI::Highlight : RPGUI::TextDim, ETextAlign::Right);
			if (Row == 3 && bOwner)
			{
				// The arrows of the player count step down / up instead of cycling.
				const float ValueW = RPGUI::Measure(FString::Printf(TEXT("‹  %s  ›"), *Item.Value), 20);
				C.Hit(RX + RW - 30 - ValueW - 10, RowY, 40, 56, Act::PlayersDown);
				C.Hit(RX + RW - 60, RowY, 50, 56, Act::PlayersUp);
			}
		}
		else if (!Item.Value.IsEmpty()) { RPGUI::Keycap(C, 1, Item.Value, RX + RW - 70, RowY + 15, 26); }
		if (!Item.bEnabled) { RPGUI::Label(C, 1, TEXT("1Pのみ"), RX + RW - 30, RowY + 36, 11, RPGUI::TextDim, ETextAlign::Right); }
	}
}

void FullDialog(RPGUI::FCanvas& C, const FFullMenu& M)
{
	const auto* P = M.P;
	FString Title, Body, Ok = TEXT("決定");
	bool bDanger = false;
	if (P->PendingSessionAction == 1) { Title = TEXT("上書き保存しますか？"); Body = TEXT("前のテストセーブを置き換えます。"); Ok = TEXT("保存する"); }
	else if (P->PendingSessionAction == 2) { Title = TEXT("自分のデータをロードしますか？"); Body = TEXT("保存してからの変更は元に戻ります。他の人と世界はそのままです。"); Ok = TEXT("ロードする"); }
	else if (P->bPendingBulkSalvage)
	{
		const auto List = P->BulkSalvageCandidates();
		int64 Gain = 0; int32 Rare = 0;
		for (int32 Index : List) { Gain += P->GetBag()[Index].SalvageValue(); Rare += P->GetBag()[Index].Rarity == ERPGPrototypeRarity::Rare ? 1 : 0; }
		Title = FString::Printf(TEXT("%d個をまとめて分解しますか？"), List.Num());
		Body = FString::Printf(TEXT("ノーマル %d個・レア %d個 → ルビー +%lld\n対象外：装備中・ロック中・NEW・ユニーク以上"), List.Num() - Rare, Rare, Gain);
		Ok = TEXT("分解する"); bDanger = true;
	}
	else if (P->PendingSalvage != INDEX_NONE && P->GetBag().IsValidIndex(P->PendingSalvage))
	{
		const auto& Item = P->GetBag()[P->PendingSalvage];
		Title = FString::Printf(TEXT("%s を分解しますか？"), *Item.Name());
		Body = FString::Printf(TEXT("%s  Lv.%d  +%d　→　ルビー +%d\n分解した装備は戻りません。"), RPGUI::RarityName(Item.Rarity), Item.Level, Item.Enhancement, Item.SalvageValue());
		Ok = TEXT("分解する"); bDanger = true;
	}
	else
	{
		const auto* Item = P->GetBag().IsValidIndex(P->BagSelection) ? &P->GetBag()[P->BagSelection] : nullptr;
		const FRPGAffix Current = Item && Item->Tunes.IsValidIndex(P->TuneSocket) ? Item->Tunes[P->TuneSocket] : FRPGAffix{};
		const auto* Definition = RPGEquipment::Find(Current.Key);
		Title = TEXT("チューンを付け替えますか？");
		Body = Definition ? FString::Printf(TEXT("いまの「%s」（ランク%d）は失われます。"), *Definition->Name, Current.Rank) : TEXT("いまのチューンは失われます。");
	}
	C.bInModal = true;
	RPGUI::DialogVeil(C, 0, 0, M.W, M.H);
	const float DW = 640, DX = (M.W - DW) * 0.5f, DY = M.H * 0.5f - 120, DH = 236;
	RPGUI::Panel(C, 2, DX, DY, DW, DH, RPGUI::EStyle::Panel, bDanger ? RPGUI::Bad : RPGUI::Highlight);
	RPGUI::Label(C, 2, Title, M.W * 0.5f, DY + 24, 26, RPGUI::TextMain, ETextAlign::Center, TEXT("Black"));
	TArray<FString> Lines;
	Body.ParseIntoArray(Lines, TEXT("\n"));
	for (int32 Line = 0; Line < Lines.Num(); ++Line) { RPGUI::Label(C, 2, Lines[Line], M.W * 0.5f, DY + 76 + Line * 28, 17, Line == 0 ? RPGUI::TextSub : RPGUI::TextDim, ETextAlign::Center); }
	RPGUI::Button(C, 2, DX + 30, DY + DH - 70, 270, 48, TEXT("キャンセル"), Act::DialogCancel, false, true, M.Key(TEXT("Esc"), TEXT("戻る")));
	RPGUI::Button(C, 2, DX + DW - 300, DY + DH - 70, 270, 48, Ok, Act::DialogOk, true, true, M.Key(TEXT("Enter"), TEXT("決定")));
	C.bInModal = false;
}

void FullMenuLayout(RPGUI::FCanvas& C, const FFullMenu& M)
{
	const auto* P = M.P;
	C.bModalOpen = P->HasConfirmation();
	RPGUI::Backdrop(C, M.W, M.H);
	FullHeader(C, M);
	const FString Enter = M.Key(TEXT("Enter"), TEXT("決定")), Move = M.Key(TEXT("↑↓"), TEXT("十字")), Pages = M.Key(TEXT("Q E"), TEXT("L R"));
	const FString Capacity = FString::Printf(TEXT("バッグ %d/%d"), P->GetBag().Num(), RPGTuning::BagCapacity);
	if (P->BagPage == ERPGBagPage::Settings)
	{
		FullSettingsPage(C, M);
		FullFooter(C, M, {{Move, TEXT("選択")}, {M.Key(TEXT("←→"), TEXT("左右")), TEXT("切替")}, {Enter, TEXT("決定")}, {Pages, TEXT("ページ")}, {M.Key(TEXT("Tab"), TEXT("－")), TEXT("閉じる")}});
	}
	else if (M.IsOverview())
	{
		FullOverview(C, M);
		FullFooter(C, M, {{M.Key(TEXT("↑↓←→"), TEXT("十字")), TEXT("枠を選ぶ")}, {Enter, TEXT("装備変更")}, {Pages, TEXT("ページ")}, {M.Key(TEXT("Tab"), TEXT("－")), TEXT("閉じる")}}, Capacity);
	}
	else if (P->BagPage == ERPGBagPage::Upgrade)
	{
		FullUpgradePage(C, M);
		FullFooter(C, M, {{Move, TEXT("選択")}, {Enter, TEXT("強化")}, {M.Key(TEXT("X"), TEXT("ZL")), TEXT("並べ替え")}, {Pages, TEXT("ページ")}, {M.Key(TEXT("Tab"), TEXT("－")), TEXT("閉じる")}}, TEXT("ルビー工房"));
	}
	else if (P->BagPage == ERPGBagPage::Tune)
	{
		FullTunePage(C, M);
		FullFooter(C, M, {{Move, TEXT("候補")}, {M.Key(TEXT("←→"), TEXT("左右")), TEXT("枠")}, {M.Key(TEXT("PgUp/Dn"), TEXT("右スティック")), TEXT("ボール")}, {Enter, TEXT("付ける")}, {M.Key(TEXT("Space"), TEXT("上")), TEXT("ランクUP")}}, TEXT("ルビー工房"));
	}
	else if (P->bChoosingEquipment)
	{
		FullItemsPage(C, M);
		FullFooter(C, M, {{Move, TEXT("選択")}, {Enter, TEXT("変更")}, {M.Key(TEXT("PgUp/Dn"), TEXT("右スティック")), TEXT("説明")}, {M.Key(TEXT("Esc"), TEXT("戻る")), TEXT("やめる")}});
	}
	else
	{
		FullItemsPage(C, M);
		FullFooter(C, M, {{Move, TEXT("選択")}, {M.Key(TEXT("←→"), TEXT("左右")), TEXT("種類")}, {Enter, TEXT("装備")}, {M.Key(TEXT("F"), TEXT("上")), TEXT("ロック")},
			{M.Key(TEXT("Del"), TEXT("左")), TEXT("分解")}, {M.Key(TEXT("PgUp/Dn"), TEXT("右スティック")), TEXT("説明")}, {Pages, TEXT("ページ")}});
	}
	if (!P->IsAtMerchant() && P->BagPage != ERPGBagPage::Settings && C.IsPainting() && M.IsOverview())
	{ RPGUI::Label(C, 0, TEXT("強化・チューンは開始地点近くのルビー商人へ"), M.W * 0.58f, M.H - 76, 13, RPGUI::TextDim, ETextAlign::Right); }
	if (C.bModalOpen) { FullDialog(C, M); }
}

FFullMenu MakeFullMenu(const URPGPrototypeHUD* HUD, const ARPGPrototypeCharacter* P, const float W, const float H)
{
	FFullMenu M;
	M.P = P; M.W = W; M.H = H;
	M.Mode = HUD->GetWorld() ? HUD->GetWorld()->GetAuthGameMode<ARPGPrototypeGameMode>() : nullptr;
	const auto* PC = Cast<AChaosImpactPlayerController>(HUD->GetOwningPlayer());
	M.bPad = PC && PC->IsUsingGamepad();
	return M;
}
}

void URPGPrototypeHUD::PaintEquipmentBag(const FGeometry& Design,FSlateWindowElementList& Elements,int32 Layer,float W,float H,float Time) const
{
	const auto* P=Cast<ARPGPrototypeCharacter>(GetOwningPlayerPawn()); if(!P) { return; }
	const FFullMenu M=MakeFullMenu(this,P,W,H);
	RPGUI::FCanvas C; C.Geometry=&Design; C.Elements=&Elements; C.Layer=Layer+3; C.Time=Time; C.bGamepad=M.bPad;
	C.Hover=GetBagPointerAction();
	FullMenuLayout(C,M);
}
int32 URPGPrototypeHUD::BagActionAt(FVector2D At,float W,float H) const
{
	const auto* P=Cast<ARPGPrototypeCharacter>(GetOwningPlayerPawn()); if(!P) { return 0; }
	RPGUI::FCanvas C;
	FullMenuLayout(C,MakeFullMenu(this,P,W,H));
	return C.ActionAt(At);
}
void URPGPrototypeHUD::ActivateBagAction(int32 Action)
{
	auto* P=Cast<ARPGPrototypeCharacter>(GetOwningPlayerPawn()); if(!P || !P->IsBagOpen() || Action==0) { return; }
	ChaosImpactSfx::Play2D(this,EChaosImpactSfx::UiConfirm,.65f);
	namespace A=RPGMenuAction;
	if(Action==A::DialogCancel) { P->PendingSalvage=P->PendingTune=INDEX_NONE; P->PendingSessionAction=0; P->bPendingBulkSalvage=false; return; }
	if(Action==A::DialogOk)
	{
		if(P->PendingSessionAction) { P->ConfirmSessionAction(); }
		else if(P->bPendingBulkSalvage) { P->ConfirmBulkSalvage(); }
		else if(P->PendingSalvage!=INDEX_NONE) { P->SalvageItem(P->PendingSalvage); }
		else { P->ApplyTune(P->BagSelection,P->PendingTune,P->TuneSocket); }
		return;
	}
	if(P->HasConfirmation()) { return; }
	const int32 Rows=P->UsesMiniMenu()?CompactMenuRows:FullMenuRows;
	if(Action>=A::SlotBase && Action<A::SlotBase+10)
	{
		// Clicking the slot that is already selected opens its picker (a second click = "change this").
		if(P->BagPage==ERPGBagPage::Equipment && !P->bChoosingEquipment && P->EquipmentSelection==Action-A::SlotBase) { P->BeginEquipmentChange(); }
		else { P->SelectEquipment(Action-A::SlotBase); }
		return;
	}
	if(Action==A::BeginChange) { P->BeginEquipmentChange(); return; }
	if(Action==A::EquipSelected) { P->EquipSelectedItem(); return; }
	if(Action==A::CancelChange) { P->CancelEquipmentChange(); return; }
	if(Action>=A::FilterBase && Action<A::FilterBase+4)
	{ const int32 Filters[]={1,3,2,4}; P->BagFilter=Filters[Action-A::FilterBase]; const auto List=P->FilteredBag(); P->BagSelection=List.Num()?List[0]:INDEX_NONE; P->DetailScroll=P->TuneSelection=0; P->bChoosingDestination=false; return; }
	if(Action>=A::RowBase && Action<A::RowBase+10)
	{
		const auto List=P->FilteredBag(); const int32 At=FMath::Max(0,List.Find(P->BagSelection)),Index=At/Rows*Rows+Action-A::RowBase;
		if(!List.IsValidIndex(Index)) { return; }
		// A second click on the selected row equips it (bag page and the equipment picker).
		if(List[Index]==P->BagSelection && (P->BagPage==ERPGBagPage::Items || P->bChoosingEquipment)) { P->EquipSelectedItem(); return; }
		P->BagSelection=List[Index]; P->DetailScroll=P->TuneSelection=0; P->bChoosingDestination=false; return;
	}
	if(Action==A::ListPrev || Action==A::ListNext)
	{ const auto List=P->FilteredBag(); const int32 At=FMath::Max(0,List.Find(P->BagSelection)); if(List.Num()) { P->BagSelection=List[FMath::Clamp(At/Rows*Rows+(Action==A::ListPrev?-Rows:Rows),0,List.Num()-1)]; P->DetailScroll=0; } return; }
	if(Action==A::SelectPrev || Action==A::SelectNext)
	{ const auto List=P->FilteredBag(); if(List.Num()) { const int32 At=FMath::Max(0,List.Find(P->BagSelection)); P->BagSelection=List[(At+(Action==A::SelectNext?1:List.Num()-1))%List.Num()]; P->DetailScroll=P->TuneSelection=0; } return; }
	if(Action==A::EquipSpecial1 || Action==A::EquipSpecial2) { P->EquipTargetSlot=Action==A::EquipSpecial1?1:2; P->bChoosingDestination=true; P->EquipSelectedItem(); return; }
	if(Action==A::Sort) { P->CycleBagSort(); return; }
	if(Action==A::Lock) { P->ToggleItemLock(P->BagSelection); return; }
	if(Action==A::BulkSalvage) { P->RequestBulkSalvage(); return; }
	if(Action>=A::SettingBase && Action<A::SettingBase+6)
	{
		const int32 Row=Action-A::SettingBase; P->SettingsSelection=Row;
		if(Row<=1) { P->RequestSessionAction(Row+1); }
		if(Row==2) { P->ToggleMenuStyle(); }
		if(Row==3 && P->GetLocalRPGIndex()==0) { if(auto* Mode=GetWorld()->GetAuthGameMode<ARPGPrototypeGameMode>()) { Mode->SetLocalPlayerCount(Mode->GetLocalPlayerCount()%4+1); } }
		if(Row==4 && P->GetLocalRPGIndex()==0) { if(auto* Mode=GetWorld()->GetAuthGameMode<ARPGPrototypeGameMode>()) { Mode->SetSharedScreen(!Mode->IsSharedScreenRequested()); } }
		if(Row==5) { if(P->HeroId.IsEmpty()) { P->SwitchInventoryPage(ERPGBagPage::Equipment); } else { P->ReturnToTitle(); } } return;
	}
	if((Action==A::PlayersDown || Action==A::PlayersUp) && P->GetLocalRPGIndex()==0)
	{ if(auto* Mode=GetWorld()->GetAuthGameMode<ARPGPrototypeGameMode>()) { Mode->SetLocalPlayerCount(FMath::Clamp(Mode->GetLocalPlayerCount()+(Action==A::PlayersDown?-1:1),1,4)); } return; }
	if(Action==A::Close) { P->SetBagOpen(false); return; }
	if(Action>=A::InspectBase && Action<A::InspectBase+5) { P->InspectEquipmentSlot(Action-A::InspectBase); return; }
	if(Action>=A::PageBase && Action<=A::PageBase+4) { P->SwitchInventoryPage(Action-A::PageBase); return; }
	if(Action==A::Salvage) { P->SalvageItem(P->BagSelection); return; }
	if(Action==A::EquipSlot1 || Action==A::EquipSlot2) { P->EquipTargetSlot=Action==A::EquipSlot1?1:2; P->EquipBagItem(P->BagSelection,P->EquipTargetSlot); return; }
	if(Action==A::Upgrade) { P->UpgradeItem(P->BagSelection); return; }
	if(Action==A::ApplyTune) { const auto Offers=P->GetTuneOffers(P->BagSelection,P->TuneSocket); if(Offers.IsValidIndex(P->TuneSelection)) { P->ApplyTune(P->BagSelection,Offers[P->TuneSelection],P->TuneSocket); } return; }
	if(Action==A::RankUp) { P->UpgradeTune(P->BagSelection,P->TuneSocket); return; }
	if(Action>=A::SocketBase && Action<A::SocketBase+3) { P->TuneSocket=Action-A::SocketBase; P->TuneSelection=0; return; }
	if(Action>=A::OfferBase && Action<A::OfferBase+3) { P->TuneSelection=Action-A::OfferBase; return; }
	if(Action>=A::MiniRowBase && Action<A::MiniRowBase+5)
	{ const auto Items=P->FilteredBag(); const int32 At=FMath::Max(0,Items.Find(P->BagSelection)),Index=At/5*5+Action-A::MiniRowBase; if(Items.IsValidIndex(Index)) { P->BagSelection=Items[Index]; P->DetailScroll=P->TuneSelection=0; } return; }
	if(Action==A::MiniPrev || Action==A::MiniNext)
	{ const auto Items=P->FilteredBag(); const int32 At=FMath::Max(0,Items.Find(P->BagSelection)); if(Items.Num()) { P->BagSelection=Items[FMath::Clamp(At+(Action==A::MiniPrev?-5:5),0,Items.Num()-1)]; } return; }
	if(Action==A::DetailUp) { P->DetailScroll=FMath::Max(0,P->DetailScroll-1); return; }
	if(Action==A::DetailDown) { ++P->DetailScroll; return; }
}
