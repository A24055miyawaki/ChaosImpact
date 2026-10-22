#include "RPGPrototypeHUD.h"
#include "RPGPrototype.h"
#include "RPGAdventure.h"
#include "RPGQuests.h"
#include "RPGTreasureChest.h"
#include "RPGSnowField.h"
#include "RPGPrototypeUIKit.h"
#include "RPGOnline.h"
#include "ChaosImpactCharacterRoster.h"
#include "ChaosImpactGameState.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"

// Quests on screen: the place name, the tracked-quest cards, the "new quest" banner, markers over people who have
// something for you, the [E] prompt, and the quest panel (a person / the board / the journal) with its accept step.

namespace RPGUI = RPGPrototypeUI;

namespace
{
using ChaosImpactPaint::ETextAlign;
using ChaosImpactPaint::WithAlpha;
using ChaosImpactPaint::HudText3D;
using ChaosImpactPaint::RoundedBox;
using ChaosImpactPaint::FPainter;
using ChaosImpactPaint::HudShard;
using ChaosImpactPaint::Navy;

constexpr float QuestW = 780, QuestH = 500;
/** About three quarters of a full screen, nearly the whole width of a split view. */
float QuestPanelScale(FVector2D Size) { return FMath::Min(Size.X * (Size.X > Size.Y * 1.2f ? 0.76f : 0.96f) / QuestW, Size.Y * 0.86f / QuestH); }

/** Quest panel actions (mouse). */
namespace QuestAct { enum : int32 { Close = 1, Primary = 20, Decline = 21, Track = 22, TabBase = 30, ConfirmNo = 900, ConfirmYes = 901, RowBase = 100, TravelRowBase = 300 }; }

const TCHAR* StateName(ERPGQuestState S)
{
	switch (S)
	{
	case ERPGQuestState::Offered: return TEXT("受けられる");
	case ERPGQuestState::Active: return TEXT("進行中");
	case ERPGQuestState::Ready: return TEXT("報告できる");
	case ERPGQuestState::Done: return TEXT("完了");
	case ERPGQuestState::Locked: return TEXT("まだ");
	default: return TEXT("");
	}
}
FLinearColor StateColor(ERPGQuestState S)
{
	switch (S)
	{
	case ERPGQuestState::Offered: return RPGUI::Highlight;
	case ERPGQuestState::Active: return RPGUI::Cyan;
	case ERPGQuestState::Ready: return RPGUI::Good;
	default: return RPGUI::TextDim;
	}
}

/** A bar of Count / Goal. */
void ProgressBar(const RPGUI::FCanvas& C, int32 Level, float X, float Y, float W, float H, float Ratio, const FLinearColor& Color)
{
	if (!C.IsPainting()) { return; }
	RoundedBox(C.At(Level, 3), X, Y, W, H, H * 0.5f, FLinearColor(0, 0, 0, 0.45f));
	if (Ratio > 0) { RoundedBox(C.At(Level, 4), X, Y, FMath::Max(H, W * FMath::Clamp(Ratio, 0.0f, 1.0f)), H, H * 0.5f, Color); }
}

/** The quests a person or the board has for this player right now: 1 offer, 2 report, 0 nothing. */
int32 GiverMark(const ARPGPrototypeCharacter* P, int32 Giver)
{
	int32 Mark = 0;
	for (int32 Index = 0; Index < RPGQuests::Num(); ++Index)
	{
		if (!RPGQuests::ReportsToGiver(Index) || RPGQuests::Get(Index).Giver != Giver) { continue; }
		const auto S = RPGQuests::State(P, Index);
		if (S == ERPGQuestState::Ready) { return 2; }
		if (S == ERPGQuestState::Offered) { Mark = 1; }
	}
	return Mark;
}

struct FQuestPanel
{
	const ARPGPrototypeCharacter* P = nullptr;
	bool bPad = false;
	const TCHAR* Key(const TCHAR* Keyboard, const TCHAR* Pad) const { return bPad ? Pad : Keyboard; }
};

/** Fast travel: the colony and the rest beacons, the ones not lit yet greyed out (RPGFastTravel.cpp). */
void TravelPanelLayout(RPGUI::FCanvas& C, const FQuestPanel& M)
{
	const auto* P = M.P;
	const auto List = P->TravelList();
	const auto* World = ARPGAdventureWorld::Find(P->GetWorld());
	if (C.IsPainting())
	{
		RPGUI::Panel(C, 0, 0, 0, QuestW, QuestH, RPGUI::EStyle::Panel, WithAlpha(RPGUI::Good, 0.85f));
		C.At(0, 3).Box(7, 5, QuestW - 14, QuestH - 10, FLinearColor(0.014f, 0.022f, 0.046f, 0.97f));
		C.At(0, 4).Box(18, 54, QuestW - 36, 2, WithAlpha(RPGUI::Good, 0.45f));
	}
	RPGUI::Label(C, 1, FString::Printf(TEXT("%dP"), P->GetLocalRPGIndex() + 1), 20, 12, 24, RPGUI::Cyan, ETextAlign::Left, TEXT("BlackItalic"));
	RPGUI::Label(C, 1, TEXT("ファストトラベル"), 70, 12, 24, RPGUI::TextMain, ETextAlign::Left, TEXT("Black"));
	int32 Lit = 0; for (const auto& Point : List) { Lit += Point.bVisited && !Point.bColony ? 1 : 0; }
	RPGUI::Chip(C, 1, FString::Printf(TEXT("灯り %d / %d"), Lit, FMath::Max(0, List.Num() - 1)), 80 + RPGUI::Measure(TEXT("ファストトラベル"), 24, TEXT("Black")), 18, RPGUI::Good, 13);
	RPGUI::Button(C, 1, QuestW - 60, 10, 44, 34, TEXT("×"), QuestAct::Close);
	// The list.
	const float ListX = 18, ListW = 330, ListTop = 66, RowH = 50;
	const int32 MaxRows = FMath::FloorToInt((QuestH - 50 - ListTop) / RowH);
	const int32 First = FMath::Clamp(P->TravelSelection - MaxRows + 1, 0, FMath::Max(0, List.Num() - MaxRows));
	for (int32 Row = 0; Row < MaxRows && First + Row < List.Num(); ++Row)
	{
		const int32 Index = First + Row;
		const auto& Point = List[Index];
		const float Y = ListTop + Row * RowH;
		const int32 Action = QuestAct::TravelRowBase + Index;
		const bool bOn = P->TravelSelection == Index;
		C.Hit(ListX, Y, ListW, RowH - 6, Action);
		if (!C.IsPainting()) { continue; }
		RPGUI::Panel(C, 1, ListX, Y, ListW, RowH - 6, bOn ? RPGUI::EStyle::Selected : C.IsHovered(Action) ? RPGUI::EStyle::Hover : Point.bVisited ? RPGUI::EStyle::Card : RPGUI::EStyle::Disabled,
			bOn ? RPGUI::Highlight : FLinearColor::Transparent, Point.bColony ? RPGUI::Highlight : Point.bVisited ? RPGUI::Good : RPGUI::TextDim);
		RPGUI::Label(C, 1, RPGUI::Fit(Point.bVisited ? Point.Name : FString(TEXT("？？？")), ListW - 120, 17), ListX + 18, Y + 3, 17, Point.bVisited ? RPGUI::TextMain : RPGUI::TextDim);
		const FString Sub = !Point.bVisited ? FString(TEXT("まだ灯りをともしていない")) : World ? World->RegionName(Point.At) : FString();
		RPGUI::Label(C, 1, RPGUI::Fit(Sub, ListW - 120, 12), ListX + 18, Y + 26, 12, RPGUI::TextSub);
		if (Point.bVisited) { RPGUI::Label(C, 1, FString::Printf(TEXT("%dm"), FMath::RoundToInt(FVector::Dist2D(Point.At, P->GetActorLocation()) / 100)), ListX + ListW - 14, Y + 13, 15, RPGUI::TextSub, ETextAlign::Right); }
	}
	// The chosen place.
	const float DX = 366, DW = QuestW - DX - 18;
	if (List.IsValidIndex(P->TravelSelection))
	{
		const auto& Point = List[P->TravelSelection];
		float Y = 70;
		RPGUI::Chip(C, 1, Point.bColony ? TEXT("コロニー") : TEXT("休息の灯り"), DX, Y, Point.bColony ? RPGUI::Highlight : RPGUI::Good, 13, true);
		Y += 30;
		RPGUI::Label(C, 1, RPGUI::Fit(Point.bVisited ? Point.Name : FString(TEXT("？？？")), DW, 26, TEXT("Black")), DX, Y, 26, RPGUI::TextMain, ETextAlign::Left, TEXT("Black"));
		Y += 44;
		const FString Body = !Point.bVisited ? FString(TEXT("まだ行っていない灯りです。近くまで行って灯りをともすと、ここへ移動できるようになります。"))
			: Point.bColony ? FString(TEXT("こもれびコロニーの広場へ戻ります。依頼人や商人、掲示板があります。"))
			: FString::Printf(TEXT("%s のそばに移動します。移動した場所が、倒れた時に起き上がる場所になります。"), World ? *World->RegionName(Point.At) : TEXT(""));
		const int32 Lines = FMath::Min(5, RPGUI::WrapLines(Body, DW - 32, 16).Num());
		if (C.IsPainting()) { RoundedBox(C.At(1, 2), DX, Y - 6, DW, Lines * 24 + 18, 8, FLinearColor(1, 1, 1, 0.055f)); C.At(1, 3).Box(DX, Y - 6, 3, Lines * 24 + 18, WithAlpha(RPGUI::Good, 0.9f)); }
		RPGUI::Wrap(C, 1, Body, DX + 16, Y, DW - 32, 16, RPGUI::TextMain, 5, 24);
		Y += Lines * 24 + 26;
		if (Point.bVisited)
		{
			RPGUI::Section(C, 1, TEXT("ここから"), DX, Y, DW);
			RPGUI::Label(C, 1, FString::Printf(TEXT("%dm"), FMath::RoundToInt(FVector::Dist2D(Point.At, P->GetActorLocation()) / 100)), DX, Y + 26, 20, RPGUI::Highlight);
		}
		FString Why;
		const bool bCan = Point.bVisited && P->CanFastTravel(&Why);
		if (!bCan && Point.bVisited) { RPGUI::Label(C, 1, RPGUI::Fit(Why, DW, 14), DX, QuestH - 112, 14, RPGUI::Bad); }
		RPGUI::Button(C, 1, DX, QuestH - 84, DW, 46, Point.bVisited ? TEXT("ここへ移動する") : TEXT("まだ移動できない"), QuestAct::Primary, bCan, bCan, M.Key(TEXT("Enter"), TEXT("決定")));
	}
	float FX = 20;
	const TArray<TPair<FString, FString>> Hints{{M.Key(TEXT("↑↓"), TEXT("十字")), TEXT("選択")}, {M.Key(TEXT("Enter"), TEXT("決定")), TEXT("移動")}, {M.Key(TEXT("Esc"), TEXT("戻る")), TEXT("閉じる")}};
	for (const auto& Hint : Hints)
	{
		const float KeyW = RPGUI::Keycap(C, 1, Hint.Key, FX, QuestH - 30, 22);
		RPGUI::Label(C, 1, Hint.Value, FX + KeyW + 5, QuestH - 29, 14, RPGUI::TextSub);
		FX += KeyW + RPGUI::Measure(Hint.Value, 14) + 18;
	}
}

/** The multiplayer menu: everyone in this world (online, or several on this machine), and the password. */
void MembersPanelLayout(RPGUI::FCanvas& C, const FQuestPanel& M)
{
	const auto* P = M.P;
	const UWorld* World = P->GetWorld();
	const auto* Online = URPGOnlineSubsystem::Get(P);
	const bool bOnline = World && World->GetNetMode() != NM_Standalone;
	if (C.IsPainting())
	{
		RPGUI::Panel(C, 0, 0, 0, QuestW, QuestH, RPGUI::EStyle::Panel, WithAlpha(RPGUI::Cyan, 0.85f));
		C.At(0, 3).Box(7, 5, QuestW - 14, QuestH - 10, FLinearColor(0.014f, 0.022f, 0.046f, 0.97f));
		C.At(0, 4).Box(18, 54, QuestW - 36, 2, WithAlpha(RPGUI::Cyan, 0.45f));
	}
	RPGUI::Label(C, 1, TEXT("マルチ"), 22, 12, 24, RPGUI::TextMain, ETextAlign::Left, TEXT("Black"));
	const FString Mode = !bOnline ? FString(TEXT("オフライン")) : World->GetNetMode() == NM_Client ? FString(TEXT("オンライン・ゲスト")) : FString(TEXT("オンライン・ホスト"));
	RPGUI::Chip(C, 1, Mode, 30 + RPGUI::Measure(TEXT("マルチ"), 24, TEXT("Black")), 18, bOnline ? RPGUI::Cyan : RPGUI::TextSub, 13, bOnline);
	RPGUI::Button(C, 1, QuestW - 60, 10, 44, 34, TEXT("×"), 1);
	// The members, in the order they came in.
	TArray<const AChaosImpactPlayerState*> Members;
	if (const AGameStateBase* State = World ? World->GetGameState() : nullptr)
	{ for (const APlayerState* Member : State->PlayerArray) { if (const auto* CI = Cast<AChaosImpactPlayerState>(Member); CI && CI->GetPawn()) { Members.Add(CI); } } }
	Members.StableSort([](const AChaosImpactPlayerState& A, const AChaosImpactPlayerState& B) { return A.JoinOrder < B.JoinOrder; });
	// The password and how many are here.
	float Y = 66;
	if (C.IsPainting()) { RoundedBox(C.At(1, 2), 18, Y, QuestW - 36, 50, 8, FLinearColor(1, 1, 1, 0.05f)); }
	if (bOnline && Online && !Online->GetPassword().IsEmpty())
	{
		RPGUI::Label(C, 1, TEXT("合言葉"), 36, Y + 15, 15, RPGUI::TextSub);
		RPGUI::Label(C, 1, Online->GetPassword(), 110, Y + 9, 24, RPGUI::Highlight, ETextAlign::Left, TEXT("Black"));
	}
	else { RPGUI::Label(C, 1, bOnline ? TEXT("オンラインで遊んでいます") : TEXT("この機械だけで遊んでいます（タイトルの「オンライン」で合言葉の世界を開けます）"), 36, Y + 15, 15, RPGUI::TextSub); }
	RPGUI::Label(C, 1, FString::Printf(TEXT("%d / %d人"), Members.Num(), URPGOnlineSubsystem::MaxPlayers), QuestW - 36, Y + 12, 20, RPGUI::TextMain, ETextAlign::Right, TEXT("Bold"));
	Y += 64;
	const auto* Adventure = ARPGAdventureWorld::Find(World);
	for (int32 Index = 0; Index < Members.Num() && Index < 4; ++Index)
	{
		const auto* Member = Members[Index];
		const auto* Hero = Cast<ARPGPrototypeCharacter>(Member->GetPawn());
		const float RY = Y + Index * 72;
		const bool bMe = Hero == P;
		RPGUI::Panel(C, 1, 18, RY, QuestW - 36, 64, bMe ? RPGUI::EStyle::Selected : RPGUI::EStyle::Card, bMe ? RPGUI::Highlight : FLinearColor::Transparent, ChaosImpactRoster::GetColourSwatch(FMath::Max(0, Member->ColourChoice)));
		if (!C.IsPainting() || !Hero) { continue; }
		// Name, character and where they are.
		RPGUI::Label(C, 1, RPGUI::Fit(Hero->GetHeroLabel(), 200, 20, TEXT("Black")), 40, RY + 6, 20, RPGUI::TextMain, ETextAlign::Left, TEXT("Black"));
		float ChipX = 40 + FMath::Min(200.0f, RPGUI::Measure(Hero->GetHeroLabel(), 20, TEXT("Black"))) + 10;
		if (Member->bHostMachine && bOnline) { ChipX += RPGUI::Chip(C, 1, TEXT("ホスト"), ChipX, RY + 9, RPGUI::Highlight, 11, true) + 4; }
		if (bMe) { RPGUI::Chip(C, 1, TEXT("あなた"), ChipX, RY + 9, RPGUI::Cyan, 11); }
		const FString Where = Adventure ? Adventure->RegionName(Hero->GetActorLocation()) : FString();
		RPGUI::Label(C, 1, RPGUI::Fit(FString::Printf(TEXT("%s　%s"), ChaosImpactRoster::Get(Member->CharacterIndex).Name, *Where), 290, 13), 40, RY + 38, 13, RPGUI::TextSub);
		// Level and HP.
		RPGUI::Label(C, 1, FString::Printf(TEXT("Lv.%d"), Hero->GetRPGLevel()), 350, RY + 9, 20, RPGUI::Highlight, ETextAlign::Left, TEXT("Black"));
		const float Ratio = Hero->IsDowned() || Hero->IsEliminated() ? 0.0f : FMath::Clamp(Hero->GetHealth() / FMath::Max(1.0f, Hero->GetMaxHealth()), 0.0f, 1.0f);
		ProgressBar(C, 1, 430, RY + 18, 170, 10, Ratio, Ratio < 0.3f ? RPGUI::Bad : RPGUI::Good);
		RPGUI::Label(C, 1, Hero->IsDowned() ? FString(TEXT("ダウン")) : Hero->IsEliminated() ? FString(TEXT("復活待ち")) : FString::Printf(TEXT("HP %.0f / %.0f"), Hero->GetHealth(), Hero->GetMaxHealth()),
			430, RY + 34, 13, Hero->IsDowned() || Hero->IsEliminated() ? RPGUI::Bad : RPGUI::TextSub);
		// The connection (to the host): bars and milliseconds.
		if (!bOnline || Member->bHostMachine) { RPGUI::Label(C, 1, bOnline ? TEXT("ホスト") : TEXT("この機械"), QuestW - 44, RY + 22, 14, RPGUI::TextDim, ETextAlign::Right); continue; }
		const float Ping = Member->GetPingInMilliseconds();
		const int32 Bars = Ping < 60 ? 4 : Ping < 120 ? 3 : Ping < 220 ? 2 : 1;
		for (int32 Bar = 0; Bar < 4; ++Bar) { C.At(1, 4).Box(626 + Bar * 10, RY + 40 - Bar * 7, 7, 8 + Bar * 7, Bar < Bars ? (Bars <= 1 ? RPGUI::Bad : RPGUI::Good) : RPGUI::TextDim); }
		RPGUI::Label(C, 1, FString::Printf(TEXT("%.0fms"), Ping), QuestW - 44, RY + 12, 13, RPGUI::TextSub, ETextAlign::Right);
	}
	if (Members.IsEmpty()) { RPGUI::Label(C, 1, TEXT("（メンバーを読み込んでいます）"), QuestW * 0.5f, Y + 30, 16, RPGUI::TextDim, ETextAlign::Center); }
	RPGUI::Label(C, 1, bOnline ? TEXT("同じ合言葉をタイトルで入れると、この世界に入れます（最大4人）。依頼はそれぞれのものです。") : TEXT("いっしょに遊ぶ人は、タイトルの人数か、空いたコントローラーの＋ボタンで加われます。"),
		22, QuestH - 62, 13, RPGUI::TextDim);
	float FX = 20;
	const TArray<TPair<FString, FString>> Hints{{M.Key(TEXT("Esc"), TEXT("戻る")), TEXT("閉じる")}};
	for (const auto& Hint : Hints)
	{
		const float KeyW = RPGUI::Keycap(C, 1, Hint.Key, FX, QuestH - 30, 22);
		RPGUI::Label(C, 1, Hint.Value, FX + KeyW + 5, QuestH - 29, 14, RPGUI::TextSub);
		FX += KeyW + RPGUI::Measure(Hint.Value, 14) + 18;
	}
}

void QuestPanelLayout(RPGUI::FCanvas& C, const FQuestPanel& M)
{
	if (M.P->QuestGiver == ARPGPrototypeCharacter::TravelPanel) { TravelPanelLayout(C, M); return; }
	if (M.P->QuestGiver == ARPGPrototypeCharacter::MembersPanel) { MembersPanelLayout(C, M); return; }
	const auto* P = M.P;
	const bool bJournal = P->QuestGiver < 0;
	const auto& Givers = RPGQuests::Givers();
	const auto* Giver = !bJournal && Givers.IsValidIndex(P->QuestGiver) ? &Givers[P->QuestGiver] : nullptr;
	C.bModalOpen = P->bQuestConfirm;
	if (C.IsPainting())
	{
		RPGUI::Panel(C, 0, 0, 0, QuestW, QuestH, RPGUI::EStyle::Panel, bJournal ? WithAlpha(RPGUI::Cyan, 0.85f) : RPGUI::Highlight);
		C.At(0, 3).Box(7, 5, QuestW - 14, QuestH - 10, FLinearColor(0.014f, 0.022f, 0.046f, 0.97f));
		C.At(0, 4).Box(18, 54, QuestW - 36, 2, WithAlpha(bJournal ? RPGUI::Cyan : RPGUI::Highlight, 0.45f));
	}
	RPGUI::Label(C, 1, FString::Printf(TEXT("%dP"), P->GetLocalRPGIndex() + 1), 20, 12, 24, RPGUI::Cyan, ETextAlign::Left, TEXT("BlackItalic"));
	RPGUI::Label(C, 1, bJournal ? TEXT("依頼の記録") : Giver->Name, 70, 12, 24, RPGUI::TextMain, ETextAlign::Left, TEXT("Black"));
	if (Giver) { RPGUI::Chip(C, 1, Giver->Role, 80 + RPGUI::Measure(Giver->Name, 24, TEXT("Black")), 18, RPGUI::TextSub, 13); }
	RPGUI::Button(C, 1, QuestW - 60, 10, 44, 34, TEXT("×"), QuestAct::Close);
	const TArray<int32> List = P->QuestPanelList();
	float ListTop = 66;
	if (bJournal)
	{
		// Tabs: what you are doing, what you could take on, what is done.
		const TCHAR* Tabs[] = {TEXT("進行中"), TEXT("受けられる"), TEXT("完了")};
		int32 Counts[3] = {0, 0, 0};
		for (int32 Index = 0; Index < RPGQuests::Num(); ++Index)
		{
			const auto S = RPGQuests::State(P, Index);
			if (S == ERPGQuestState::Active || S == ERPGQuestState::Ready) { ++Counts[0]; }
			else if (S == ERPGQuestState::Offered || S == ERPGQuestState::Locked) { ++Counts[1]; }
			else if (S == ERPGQuestState::Done) { ++Counts[2]; }
		}
		for (int32 Tab = 0; Tab < 3; ++Tab)
		{
			const float X = 18 + Tab * 128;
			const int32 Action = QuestAct::TabBase + Tab;
			const bool bOn = P->QuestTab == Tab;
			C.Hit(X, 64, 122, 36, Action);
			RPGUI::Panel(C, 1, X, 64, 122, 36, bOn ? RPGUI::EStyle::Selected : C.IsHovered(Action) ? RPGUI::EStyle::Hover : RPGUI::EStyle::Card, bOn ? RPGUI::Highlight : FLinearColor::Transparent);
			RPGUI::Label(C, 1, FString::Printf(TEXT("%s %d"), Tabs[Tab], Counts[Tab]), X + 63, 71, 16, bOn ? RPGUI::TextMain : RPGUI::TextSub, ETextAlign::Center);
		}
		ListTop = 110;
	}
	// The list.
	const float ListX = 18, ListW = 272, RowH = 54;
	const int32 MaxRows = FMath::FloorToInt((QuestH - 50 - ListTop) / RowH);
	const int32 At = FMath::Max(0, List.Find(P->QuestSelection)), First = FMath::Clamp(At - MaxRows + 1, 0, FMath::Max(0, List.Num() - MaxRows));
	if (List.IsEmpty())
	{
		const FString Empty = Giver ? FString(Giver->Idle) : P->QuestTab == 0 ? FString(TEXT("進行中の依頼はありません。掲示板や人に話しかけてみよう。"))
			: P->QuestTab == 1 ? FString(TEXT("受けられる依頼はありません。")) : FString(TEXT("まだ完了した依頼はありません。"));
		RPGUI::Wrap(C, 1, Empty, 40, ListTop + 30, QuestW - 80, 18, RPGUI::TextSub, 3, 28);
	}
	for (int32 Row = 0; Row < MaxRows && First + Row < List.Num(); ++Row)
	{
		const int32 Index = List[First + Row];
		const auto& D = RPGQuests::Get(Index);
		const auto S = RPGQuests::State(P, Index);
		const float Y = ListTop + Row * RowH;
		const int32 Action = QuestAct::RowBase + Index;
		const bool bOn = P->QuestSelection == Index;
		C.Hit(ListX, Y, ListW, RowH - 6, Action);
		if (!C.IsPainting()) { continue; }
		RPGUI::Panel(C, 1, ListX, Y, ListW, RowH - 6, bOn ? RPGUI::EStyle::Selected : C.IsHovered(Action) ? RPGUI::EStyle::Hover : RPGUI::EStyle::Card,
			bOn ? RPGUI::Highlight : FLinearColor::Transparent, RPGQuests::KindColor(D.Kind));
		// Name on its own line (full width); kind, progress and state below it.
		RPGUI::Label(C, 1, RPGUI::Fit(S == ERPGQuestState::Locked ? FString(TEXT("？？？")) : FString(D.Name), ListW - 32, 17), ListX + 18, Y + 2, 17,
			S == ERPGQuestState::Done || S == ERPGQuestState::Locked ? RPGUI::TextDim : RPGUI::TextMain);
		float ChipRight = ListX + ListW - 14;
		ChipRight -= RPGUI::Chip(C, 1, StateName(S), ChipRight, Y + 26, StateColor(S), 10, S == ERPGQuestState::Ready, ETextAlign::Right) + 4;
		if (P->TrackedQuest == Index) { ChipRight -= RPGUI::Chip(C, 1, TEXT("追跡"), ChipRight, Y + 26, RPGUI::Cyan, 10, false, ETextAlign::Right) + 4; }
		const FString Sub = S == ERPGQuestState::Locked ? RPGQuests::UnlockHint(Index) : S == ERPGQuestState::Active ? RPGQuests::Progress(P, Index) : RPGQuests::KindName(D.Kind);
		RPGUI::Label(C, 1, RPGUI::Fit(Sub, ChipRight - ListX - 22, 12), ListX + 18, Y + 28, 12, RPGUI::TextSub);
	}
	// The selected quest.
	const float DX = 306, DW = QuestW - DX - 18, DY = 66;
	if (List.Contains(P->QuestSelection))
	{
		const int32 Index = P->QuestSelection;
		const auto& D = RPGQuests::Get(Index);
		const auto S = RPGQuests::State(P, Index);
		float Y = DY;
		float ChipX = DX;
		ChipX += RPGUI::Chip(C, 1, RPGQuests::KindName(D.Kind), ChipX, Y, RPGQuests::KindColor(D.Kind), 13, true) + 6;
		ChipX += RPGUI::Chip(C, 1, StateName(S), ChipX, Y, StateColor(S), 13) + 8;
		RPGUI::Label(C, 1, RPGUI::Fit(RPGQuests::SourceText(Index), DX + DW - ChipX, 13), ChipX, Y + 3, 13, RPGUI::TextDim);
		Y += 30;
		RPGUI::Label(C, 1, S == ERPGQuestState::Locked ? FString(TEXT("？？？")) : FString(D.Name), DX, Y, 26, RPGUI::TextMain, ETextAlign::Left, TEXT("Black"));
		Y += 44;
		// What they seem to want, as narration (no spoken lines).
		const FString Situation = S == ERPGQuestState::Locked ? FString::Printf(TEXT("まだ話を聞ける段階ではないようだ。%s。"), *RPGQuests::UnlockHint(Index)) : FString(D.Situation);
		const int32 Lines = FMath::Min(5, RPGUI::WrapLines(Situation, DW - 32, 16).Num());
		if (C.IsPainting()) { RoundedBox(C.At(1, 2), DX, Y - 6, DW, Lines * 24 + 18, 8, FLinearColor(1, 1, 1, 0.055f)); C.At(1, 3).Box(DX, Y - 6, 3, Lines * 24 + 18, WithAlpha(RPGQuests::KindColor(D.Kind), 0.9f)); }
		RPGUI::Wrap(C, 1, Situation, DX + 16, Y, DW - 32, 16, RPGUI::TextMain, 5, 24);
		Y += Lines * 24 + 22;
		if (S != ERPGQuestState::Locked)
		{
			RPGUI::Section(C, 1, TEXT("目的"), DX, Y, DW);
			Y += 24;
			const auto& Q = P->Quests[Index];
			const int32 Count = S == ERPGQuestState::Done || S == ERPGQuestState::Ready ? D.Goal : S == ERPGQuestState::Active ? Q.Count : 0;
			RPGUI::Label(C, 1, D.Objective, DX, Y, 17, RPGUI::TextMain);
			RPGUI::Label(C, 1, FString::Printf(TEXT("%d / %d"), Count, D.Goal), DX + DW, Y, 17, S == ERPGQuestState::Ready ? RPGUI::Good : RPGUI::Highlight, ETextAlign::Right);
			ProgressBar(C, 1, DX, Y + 26, DW, 8, static_cast<float>(Count) / FMath::Max(1, D.Goal), S == ERPGQuestState::Ready || S == ERPGQuestState::Done ? RPGUI::Good : RPGQuests::KindColor(D.Kind));
			Y += 44;
			RPGUI::Section(C, 1, TEXT("報酬"), DX, Y, DW);
			Y += 26;
			float RX = DX;
			RX += RPGUI::Rubies(C, 1, D.Rubies, RX, Y, 17) + 14;
			RX += RPGUI::Chip(C, 1, FString::Printf(TEXT("XP %d"), D.XP), RX, Y, RPGUI::Cyan, 13) + 8;
			if (D.RewardKind != ERPGItemKind::Material)
			{
				const TCHAR* Rarity[] = {TEXT("ノーマル"), TEXT("レア"), TEXT("ユニーク"), TEXT("レジェンド")};
				const FString Item = D.RewardKind == ERPGItemKind::Ball ? (D.RewardBall == EChaosImpactBallType::Nova ? FString(TEXT("特殊球（ランダム）")) : FString(ChaosImpactBallTypes::GetDisplayName(D.RewardBall)))
					: RPGEquipment::KindName(D.RewardKind);
				RPGUI::Chip(C, 1, FString::Printf(TEXT("%s %s"), Rarity[FMath::Clamp(D.RewardRarity, 0, 3)], *Item), RX, Y, RPGUI::RarityColor(static_cast<ERPGPrototypeRarity>(FMath::Clamp(D.RewardRarity, 0, 3))), 13, true);
			}
		}
		// Buttons: what you can do with this quest here.
		const float BY = QuestH - 84, BW = (DW - 10) * 0.5f;
		const bool bHere = !bJournal && RPGQuests::Get(Index).Giver == P->QuestGiver;
		const FString Ok = M.Key(TEXT("Enter"), TEXT("決定")), Back = M.Key(TEXT("Esc"), TEXT("戻る")), Track = M.Key(TEXT("T"), TEXT("上"));
		if (S == ERPGQuestState::Offered && (bHere || !RPGQuests::ReportsToGiver(Index)))
		{
			// The accept step: take it on, or leave it for later.
			RPGUI::Button(C, 1, DX, BY, BW, 46, TEXT("やめておく"), QuestAct::Decline, false, true, Back);
			RPGUI::Button(C, 1, DX + BW + 10, BY, BW, 46, TEXT("受ける"), QuestAct::Primary, true, true, Ok);
		}
		else if (S == ERPGQuestState::Offered)
		{ RPGUI::Button(C, 1, DX, BY, DW, 46, FString::Printf(TEXT("%s に話しかけると受けられる"), Givers[D.Giver].Name), QuestAct::Primary, false, false); }
		else if (S == ERPGQuestState::Ready && (bHere || !RPGQuests::ReportsToGiver(Index)))
		{ RPGUI::Button(C, 1, DX, BY, DW, 46, TEXT("報告して報酬を受け取る"), QuestAct::Primary, true, true, Ok); }
		else if (S == ERPGQuestState::Active || S == ERPGQuestState::Ready)
		{
			RPGUI::Button(C, 1, DX, BY, BW, 46, P->TrackedQuest == Index ? TEXT("追跡をやめる") : TEXT("画面に追跡表示"), QuestAct::Track, false, true, Track, P->TrackedQuest == Index);
			RPGUI::Button(C, 1, DX + BW + 10, BY, BW, 46, S == ERPGQuestState::Ready ? FString::Printf(TEXT("%s へ報告"), Givers[D.Giver].Name) : FString(TEXT("進行中")), QuestAct::Primary, false, false);
		}
		else { RPGUI::Button(C, 1, DX, BY, DW, 46, S == ERPGQuestState::Done ? TEXT("完了した依頼") : TEXT("まだ受けられない"), QuestAct::Primary, false, false); }
	}
	// Footer.
	float FX = 20;
	TArray<TPair<FString, FString>> Hints{{M.Key(TEXT("↑↓"), TEXT("十字")), TEXT("選択")}, {M.Key(TEXT("Enter"), TEXT("決定")), TEXT("受ける／報告")}, {M.Key(TEXT("T"), TEXT("上")), TEXT("追跡")}};
	if (bJournal) { Hints.Add({M.Key(TEXT("Q E"), TEXT("L R")), TEXT("タブ")}); }
	Hints.Add({M.Key(TEXT("Esc"), TEXT("戻る")), TEXT("閉じる")});
	for (const auto& Hint : Hints)
	{
		const float KeyW = RPGUI::Keycap(C, 1, Hint.Key, FX, QuestH - 30, 22);
		RPGUI::Label(C, 1, Hint.Value, FX + KeyW + 5, QuestH - 29, 14, RPGUI::TextSub);
		FX += KeyW + RPGUI::Measure(Hint.Value, 14) + 18;
	}
	// The journal asks once more before taking on a quest found in the field.
	if (P->bQuestConfirm && P->Quests.IsValidIndex(P->QuestSelection))
	{
		C.bInModal = true;
		RPGUI::DialogVeil(C, 7, 5, QuestW - 14, QuestH - 10);
		RPGUI::Panel(C, 2, QuestW * 0.5f - 260, QuestH * 0.5f - 90, 520, 180, RPGUI::EStyle::Panel, RPGUI::Highlight);
		RPGUI::Label(C, 2, FString::Printf(TEXT("「%s」を受けますか？"), RPGQuests::Get(P->QuestSelection).Name), QuestW * 0.5f, QuestH * 0.5f - 66, 21, RPGUI::TextMain, ETextAlign::Center);
		RPGUI::Label(C, 2, RPGUI::Fit(FString(TEXT("報酬：")) + RPGQuests::RewardText(P->QuestSelection), 480, 15), QuestW * 0.5f, QuestH * 0.5f - 28, 15, RPGUI::TextSub, ETextAlign::Center);
		RPGUI::Button(C, 2, QuestW * 0.5f - 236, QuestH * 0.5f + 20, 220, 46, TEXT("やめておく"), QuestAct::ConfirmNo, false, true, M.Key(TEXT("Esc"), TEXT("戻る")));
		RPGUI::Button(C, 2, QuestW * 0.5f + 16, QuestH * 0.5f + 20, 220, 46, TEXT("受ける"), QuestAct::ConfirmYes, true, true, M.Key(TEXT("Enter"), TEXT("決定")));
		C.bInModal = false;
	}
}

/** "[E] 話す：門番 ロウ" over the action area. */
void AdventurePrompt(const FGeometry& Geometry, FSlateWindowElementList& Elements, int32 Layer, float W, float Y, const FString& Key, const FString& Label, const FLinearColor& Accent)
{
	RPGUI::FCanvas C; C.Geometry = &Geometry; C.Elements = &Elements; C.Layer = Layer;
	const float KeyW = FMath::Max(34.0f, RPGUI::Measure(Key, 17) + 20), Width = FMath::Max(260.0f, KeyW + RPGUI::Measure(Label, 21) + 64);
	const float X = W * 0.5f - Width * 0.5f;
	RPGUI::Panel(C, 0, X, Y, Width, 52, RPGUI::EStyle::Card, Accent, Accent);
	RPGUI::Keycap(C, 0, Key, X + 26, Y + 11, 30);
	RPGUI::Label(C, 0, Label, X + 26 + KeyW + 14, Y + 12, 21, RPGUI::TextMain);
}
}

FGeometry URPGPrototypeHUD::QuestPanelGeometry(const FGeometry& Geometry) const
{
	const auto Area=OwnerMenuGeometry(Geometry); const auto Size=Area.GetLocalSize(); const float Scale=QuestPanelScale(Size),U=FMath::Clamp(QuestRevealAlpha,0.0f,1.0f)-1;
	const float Reveal=1+2.2f*U*U*U+1.2f*U*U;
	return ChaosImpactPaint::MakeAnchor(Area,(Size.X-QuestW*Scale)*.5f,Size.Y-QuestH*Scale-12+(QuestH*Scale+24)*(1-Reveal),Scale);
}

void URPGPrototypeHUD::PaintAdventure(const FGeometry& Geometry,FSlateWindowElementList& Elements,int32 Layer,float Time) const
{
	const auto* Player=Cast<ARPGPrototypeCharacter>(GetOwningPlayerPawn()); const auto* World=ARPGAdventureWorld::Find(GetWorld());
	const auto* PC=Cast<AChaosImpactPlayerController>(GetOwningPlayer());
	if(!World || !Player || !PC || (Player->IsBagOpen() && !Player->UsesMiniMenu())) { return; }
	const FVector2D Size=OwnerMenuGeometry(Geometry).GetLocalSize(); const bool Split=GetGameInstance() && GetGameInstance()->GetLocalPlayers().Num()>1;
	const float Scale=FMath::Min(Size.X/(Split?960.0f:1280.0f),Size.Y/(Split?600.0f:720.0f)); if(Scale<=0) { return; }
	const float W=Size.X/Scale,H=Size.Y/Scale; const bool bPad=PC->IsUsingGamepad();
	const FGeometry Design=ChaosImpactPaint::MakeAnchor(OwnerMenuGeometry(Geometry),0,0,Scale);
	RPGUI::FCanvas C; C.Geometry=&Design; C.Elements=&Elements; C.Layer=Layer+3; C.Time=Time;
	const double Now=GetWorld()->GetTimeSeconds();
	if(!Player->IsQuestMenuOpen())
	{
		HudText3D(C.At(0,6),World->RegionName(Player->GetActorLocation()),22,18,25,RPGUI::TextMain,ETextAlign::Left,TEXT("Bold"));
		// Journal shortcut (clickable) and up to three active quests, the tracked one first.
		const float ChipW=RPGUI::Keycap(C,1,bPad?TEXT("↓"):TEXT("J"),22,58,24);
		RPGUI::Label(C,1,TEXT("依頼の記録"),22+ChipW+6,58,16,RPGUI::TextSub);
		const float TravelChipW=RPGUI::Keycap(C,1,bPad?TEXT("→"):TEXT("M"),180,58,24);
		RPGUI::Label(C,1,TEXT("ファストトラベル"),180+TravelChipW+6,58,16,RPGUI::TextSub);
		const float MembersChipW=RPGUI::Keycap(C,1,bPad?TEXT("←"):TEXT("O"),360,58,24);
		RPGUI::Label(C,1,TEXT("マルチ"),360+MembersChipW+6,58,16,RPGUI::TextSub);
		TArray<int32> Shown;
		if(Player->TrackedQuest!=INDEX_NONE) { const auto S=RPGQuests::State(Player,Player->TrackedQuest); if(S==ERPGQuestState::Active || S==ERPGQuestState::Ready) { Shown.Add(Player->TrackedQuest); } }
		for(int32 Index=0;Index<RPGQuests::Num() && Shown.Num()<3;++Index)
		{ const auto S=RPGQuests::State(Player,Index); if((S==ERPGQuestState::Active || S==ERPGQuestState::Ready) && !Shown.Contains(Index)) { Shown.Add(Index); } }
		for(int32 Row=0;Row<Shown.Num();++Row)
		{
			const int32 Index=Shown[Row]; const auto& D=RPGQuests::Get(Index); const auto S=RPGQuests::State(Player,Index);
			const float Y=90+Row*62;
			RPGUI::Panel(C,0,22,Y,310,54,RPGUI::EStyle::Card,Index==Player->TrackedQuest?RPGUI::Highlight:FLinearColor::Transparent,RPGQuests::KindColor(D.Kind));
			RPGUI::Label(C,0,RPGUI::Fit(D.Name,230,16),40,Y+3,16,RPGUI::TextMain);
			RPGUI::Chip(C,0,RPGQuests::KindName(D.Kind),318,Y+5,RPGQuests::KindColor(D.Kind),10,false,ETextAlign::Right);
			const auto& Q=Player->Quests[Index];
			if(S==ERPGQuestState::Ready) { RPGUI::Label(C,0,RPGUI::Fit(RPGQuests::Progress(Player,Index),280,13),40,Y+29,13,RPGUI::Good); }
			else
			{
				RPGUI::Label(C,0,RPGUI::Fit(FString::Printf(TEXT("%s"),D.Objective),200,13),40,Y+27,13,RPGUI::TextSub);
				RPGUI::Label(C,0,FString::Printf(TEXT("%d/%d"),Q.Count,D.Goal),318,Y+27,13,RPGUI::Highlight,ETextAlign::Right);
				ProgressBar(C,0,40,Y+45,278,4,static_cast<float>(Q.Count)/FMath::Max(1,D.Goal),RPGQuests::KindColor(D.Kind));
			}
		}
		// A short banner when a quest is found, taken on or finished. Below the boss bar, never over the middle.
		const float BannerAge=Now-Player->QuestBannerAt;
		if(Player->QuestBannerQuest!=INDEX_NONE && BannerAge>=0 && BannerAge<4)
		{
			const float Alpha=FMath::Clamp(FMath::Min(BannerAge*4,(4-BannerAge)*2),0.0f,1.0f);
			const auto& D=RPGQuests::Get(Player->QuestBannerQuest);
			RPGUI::FCanvas B=C; B.Layer=Layer+20;
			const float BW=460,BX=W*.5f-BW*.5f,BY=92-12*(1-Alpha);
			if(Alpha>0)
			{
				FPainter Fade=B.At(0,0); Fade.Alpha=Alpha;
				HudShard(Fade,{FVector2D(BX+10,BY),FVector2D(BX+BW,BY),FVector2D(BX+BW-10,BY+64),FVector2D(BX,BY+64)},RPGUI::StyleFill(RPGUI::EStyle::Card),2,FVector2D(3,4));
				FPainter Text=B.At(0,6); Text.Alpha=Alpha;
				Text.Text(Player->QuestBannerLabel,BX+28,BY+8,14,Player->QuestBannerLabel.Contains(TEXT("達成"))?RPGUI::Good:RPGUI::Highlight,ETextAlign::Left,TEXT("Bold"));
				Text.Text(RPGUI::Fit(D.Name,BW-150,22),BX+28,BY+28,22,RPGUI::TextMain,ETextAlign::Left,TEXT("Bold"));
				Text.Text(Player->QuestBannerLabel.Contains(TEXT("話"))?FString(bPad?TEXT("↓ で確認"):TEXT("J で確認")):RPGQuests::KindName(D.Kind),BX+BW-24,BY+34,15,RPGUI::TextSub,ETextAlign::Right,TEXT("Bold"));
				FPainter Edge=B.At(0,3); Edge.Alpha=Alpha; Edge.Box(BX+10,BY+1,BW-12,2,RPGQuests::KindColor(D.Kind));
			}
		}
		if(!Player->IsBagOpen())
		{
			const FString Use=bPad?TEXT("上ボタン"):TEXT("E");
			if(const auto* Downed=Player->FindReviveTarget()) { AdventurePrompt(Design,Elements,Layer+8,W,H-285,Use+TEXT(" 長押し"),FString(TEXT("起こす："))+Downed->GetHeroLabel(),RPGUI::Good); }
			else if(Player->IsDowned())
			{
				RPGUI::FCanvas Hint=C; Hint.Layer=Layer+8;
				const FString Line=TEXT("近くの仲間に長押しで起こしてもらおう");
				const float HW=RPGUI::Measure(Line,19)+60;
				RPGUI::Panel(Hint,0,W*.5f-HW*.5f,H-285,HW,46,RPGUI::EStyle::Card,RPGUI::Bad,RPGUI::Bad);
				RPGUI::Label(Hint,0,Line,W*.5f,H-275,19,RPGUI::TextMain,ETextAlign::Center);
			}
			else if(const auto* Chest=ARPGTreasureChest::Nearest(Player)) { AdventurePrompt(Design,Elements,Layer+8,W,H-285,Use,Chest->DisplayName+TEXT("を開ける"),RPGUI::Highlight); }
			else if(const auto* Giver=ARPGQuestGiver::Nearest(Player)) { AdventurePrompt(Design,Elements,Layer+8,W,H-285,Use,FString(TEXT("話す："))+RPGQuests::Givers()[FMath::Clamp(Giver->GiverId,0,RPGQuests::Givers().Num()-1)].Name,RPGUI::Cyan); }
			else if(const auto* Gate=ARPGWarpGate::Nearest(Player)) { AdventurePrompt(Design,Elements,Layer+8,W,H-285,Use,Gate->DisplayName,RPGUI::Cyan); }
			else if(const auto* Node=ARPGAdventureNode::Nearest(Player)) { AdventurePrompt(Design,Elements,Layer+8,W,H-285,Use,Node->DisplayName,RPGUI::Highlight); }
			else if(Player->NearestBeacon()) { AdventurePrompt(Design,Elements,Layer+8,W,H-285,Use,TEXT("ファストトラベル"),RPGUI::Good); }
			// Markers: "!" a person/board has a quest for you, "?" you can report; "!" on boxes and landmarks you still need.
			FPainter Marker{Geometry,Elements,Layer+2};
			const auto Mark=[&](const FVector& At,const TCHAR* Glyph,const FLinearColor& Color)
			{
				FVector2D Screen; if(!UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(GetOwningPlayer(),At+FVector(0,0,8*FMath::Sin(Time*3)),Screen,true)) { return; }
				Marker.Disc(Screen,15*Scale,Navy); Marker.Disc(Screen,12.5f*Scale,Color);
				Marker.Text(Glyph,Screen.X,Screen.Y-14*Scale,20*Scale,Navy,ETextAlign::Center,TEXT("Black"));
			};
			// Teammates' names over their heads (two or more players): name, level and a small HP bar.
			if(ARPGPrototypeCharacter::CountHeroes(GetWorld())>=2)
			{
				for(TActorIterator<ARPGPrototypeCharacter> It(GetWorld());It;++It)
				{
					if(It->IsGrunt() || *It==Player || It->IsEliminated() || It->IsDowned() || FVector::DistSquared2D(Player->GetActorLocation(),It->GetActorLocation())>FMath::Square(4500.0f)) { continue; }
					FVector2D Screen; if(!UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(GetOwningPlayer(),It->GetActorLocation()+FVector(0,0,140),Screen,true)) { continue; }
					const FString Line=FString::Printf(TEXT("%s  Lv.%d"),*It->GetHeroLabel(),It->GetRPGLevel());
					const float TW=(RPGUI::Measure(Line,15)+24)*Scale,TH=30*Scale,TX=Screen.X-TW*.5f,TY=Screen.Y-TH;
					RoundedBox(Marker,TX,TY,TW,TH,6*Scale,FLinearColor(0.01f,0.02f,0.05f,.62f));
					Marker.Text(Line,Screen.X,TY+2*Scale,15*Scale,RPGUI::TextMain,ETextAlign::Center,TEXT("Bold"));
					const float Ratio=FMath::Clamp(It->GetHealth()/FMath::Max(1.0f,It->GetMaxHealth()),0.0f,1.0f);
					Marker.Box(TX+6*Scale,TY+TH-7*Scale,TW-12*Scale,4*Scale,Navy);
					Marker.Box(TX+6*Scale,TY+TH-7*Scale,(TW-12*Scale)*Ratio,4*Scale,Ratio<.3f?RPGUI::Bad:RPGUI::Good);
				}
			}
			// Downed players: a ring over them, the time left as its arc, the help filling it green from the middle.
			for(TActorIterator<ARPGPrototypeCharacter> It(GetWorld());It;++It)
			{
				if(!It->IsDowned() || FVector::DistSquared2D(Player->GetActorLocation(),It->GetActorLocation())>FMath::Square(3200.0f)) { continue; }
				FVector2D Screen; if(!UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(GetOwningPlayer(),It->GetActorLocation()+FVector(0,0,120),Screen,true)) { continue; }
				const float Left=It->GetDownedLeft()/RPGTuning::DownSeconds,R=26*Scale;
				Marker.Disc(Screen,R+4*Scale,Navy);
				Marker.Arc(Screen,R,-90,-90+360*Left,FMath::Lerp(RPGUI::Bad,RPGUI::Highlight,Left),5*Scale);
				if(It->GetReviveProgress()>0) { Marker.Disc(Screen,(R-7*Scale)*FMath::Clamp(It->GetReviveProgress(),0.0f,1.0f),WithAlpha(RPGUI::Good,.85f)); }
				Marker.Text(FString::Printf(TEXT("%d"),FMath::CeilToInt(It->GetDownedLeft())),Screen.X,Screen.Y-11*Scale,17*Scale,RPGUI::TextMain,ETextAlign::Center,TEXT("Black"));
			}
			for(TActorIterator<ARPGQuestGiver> It(GetWorld());It;++It)
			{
				const int32 M=GiverMark(Player,It->GiverId);
				if(M && FVector::DistSquared2D(Player->GetActorLocation(),It->GetActorLocation())<FMath::Square(2600.0f)) { Mark(It->GetActorLocation()+FVector(0,0,230),M==2?TEXT("?"):TEXT("!"),M==2?RPGUI::Good:RPGUI::Highlight); }
			}
			for(TActorIterator<ARPGAdventureNode> It(GetWorld());It;++It)
			{
				if(FVector::DistSquared2D(Player->GetActorLocation(),It->GetActorLocation())>FMath::Square(2200.0f)) { continue; }
				if(It->Kind==ERPGAdventureNodeKind::Board)
				{ const int32 M=GiverMark(Player,0); if(M) { Mark(It->GetActorLocation()+FVector(0,0,330),M==2?TEXT("?"):TEXT("!"),M==2?RPGUI::Good:RPGUI::Highlight); } continue; }
				const int32 Quest=It->Kind==ERPGAdventureNodeKind::Supply?1:It->Kind==ERPGAdventureNodeKind::Survey?2:INDEX_NONE;
				if(Quest==INDEX_NONE || !Player->Quests.IsValidIndex(Quest) || Player->Quests[Quest].Stage!=1 || Player->Quests[Quest].Flags&(1<<FMath::Clamp(It->ObjectiveId,0,2))) { continue; }
				Mark(It->GetActorLocation()+FVector(0,0,210),TEXT("!"),RPGUI::Cyan);
			}
		}
	}
	if(QuestRevealAlpha<=0) { return; }
	const auto G=QuestPanelGeometry(Geometry);
	FQuestPanel M; M.P=Player; M.bPad=bPad;
	RPGUI::FCanvas Panel; Panel.Geometry=&G; Panel.Elements=&Elements; Panel.Layer=Layer+12; Panel.Time=Time;
	Panel.Hover=GetAdventurePointerAction();
	QuestPanelLayout(Panel,M);
}

int32 URPGPrototypeHUD::GetAdventurePointerAction() const
{
	const auto* Player=Cast<ARPGPrototypeCharacter>(GetOwningPlayerPawn()); const auto* PC=Cast<AChaosImpactPlayerController>(GetOwningPlayer());
	if(!Player || !PC || PC->IsUsingGamepad() || !Player->IsQuestMenuOpen() || !FSlateApplication::IsInitialized()) { return 0; }
	const FVector2D At=QuestPanelGeometry(GetCachedGeometry()).AbsoluteToLocal(FSlateApplication::Get().GetCursorPos());
	if(At.X<0 || At.X>QuestW || At.Y<0 || At.Y>QuestH) { return 0; }
	FQuestPanel M; M.P=Player;
	RPGUI::FCanvas C; QuestPanelLayout(C,M);
	return C.ActionAt(At);
}

void URPGPrototypeHUD::ActivateAdventureAction(int32 Action)
{
	auto* Player=Cast<ARPGPrototypeCharacter>(GetOwningPlayerPawn()); if(!Player || !Player->IsQuestMenuOpen() || Action==0) { return; }
	if(Player->IsMembersOpen()) { if(Action==QuestAct::Close) { Player->SetQuestMenuOpen(false); } return; }
	if(Player->IsTravelOpen())
	{
		if(Action==QuestAct::Close) { Player->SetQuestMenuOpen(false); return; }
		if(Action>=QuestAct::TravelRowBase && Action<QuestAct::TravelRowBase+100)
		{
			// A second click on the chosen place goes there.
			if(Player->TravelSelection==Action-QuestAct::TravelRowBase) { Player->FastTravelTo(Player->TravelSelection); }
			else { Player->TravelSelection=Action-QuestAct::TravelRowBase; }
			return;
		}
		if(Action==QuestAct::Primary) { Player->FastTravelTo(Player->TravelSelection); }
		return;
	}
	if(Player->bQuestConfirm)
	{
		if(Action==QuestAct::ConfirmYes) { Player->AcceptQuest(Player->QuestSelection); }
		if(Action==QuestAct::ConfirmNo) { Player->bQuestConfirm=false; }
		return;
	}
	if(Action==QuestAct::Close || Action==QuestAct::Decline) { Player->SetQuestMenuOpen(false); return; }
	if(Action>=QuestAct::RowBase && Action<QuestAct::RowBase+RPGQuests::Num())
	{
		// A second click on the selected quest does its main action.
		if(Player->QuestSelection==Action-QuestAct::RowBase) { Player->RequestQuestPrimary(); }
		else { Player->QuestSelection=Action-QuestAct::RowBase; }
		return;
	}
	if(Action>=QuestAct::TabBase && Action<QuestAct::TabBase+3) { Player->SetQuestTab(Action-QuestAct::TabBase); return; }
	if(Action==QuestAct::Primary) { Player->RequestQuestPrimary(); return; }
	if(Action==QuestAct::Track) { Player->ToggleQuestTrack(Player->QuestSelection); }
}
