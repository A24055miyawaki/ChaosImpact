#include "RPGTitle.h"
#include "RPGPrototype.h"
#include "RPGPrototypeUIKit.h"
#include "RPGQuests.h"
#include "RPGOnline.h"
#include "GameFramework/PlayerState.h"
#include "ChaosImpactCharacterRoster.h"
#include "ChaosImpactLoadoutSubsystem.h"
#include "ChaosImpactGameState.h"
#include "EngineUtils.h"
#include "ChaosImpactSfx.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableText.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "ChaosImpactPuppetComponent.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/GameInstance.h"
#include "Framework/Application/SlateApplication.h"
#include "Kismet/GameplayStatics.h"

namespace RPGUI = RPGPrototypeUI;

// ---------------------------------------------------------------- heroes on this machine

namespace
{
FString HeroIndexSlot = TEXT("CI_RPG_Heroes");
}
void RPGHeroes::SetIndexSlotForTests(const FString& Slot) { HeroIndexSlot = Slot.IsEmpty() ? FString(TEXT("CI_RPG_Heroes")) : Slot; }
TArray<FRPGHeroEntry> RPGHeroes::Load()
{
	if (!UGameplayStatics::DoesSaveGameExist(HeroIndexSlot, 0)) { return {}; }
	const auto* Save = Cast<URPGHeroIndexSave>(UGameplayStatics::LoadGameFromSlot(HeroIndexSlot, 0));
	return Save ? Save->Heroes : TArray<FRPGHeroEntry>{};
}
bool RPGHeroes::Store(const TArray<FRPGHeroEntry>& Heroes)
{
	auto* Save = Cast<URPGHeroIndexSave>(UGameplayStatics::CreateSaveGameObject(URPGHeroIndexSave::StaticClass()));
	if (!Save) { return false; }
	Save->Heroes = Heroes;
	return UGameplayStatics::SaveGameToSlot(Save, HeroIndexSlot, 0);
}
FString RPGHeroes::SaveSlot(const FString& Id) { return TEXT("CI_RPG_Hero_") + Id; }
FRPGHeroEntry RPGHeroes::Create(const FString& Name, int32 Character, int32 Colour)
{
	auto Heroes = Load();
	if (Heroes.Num() >= MaxHeroes) { return {}; }
	FRPGHeroEntry Hero;
	Hero.Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	Hero.Name = Name.Left(MaxNameLength);
	Hero.Character = ChaosImpactRoster::ClampIndex(Character);
	Hero.Colour = FMath::Clamp(Colour, 0, ChaosImpactRoster::ColourCount - 1);
	Hero.Created = Hero.LastPlayed = FDateTime::Now();
	Heroes.Add(Hero);
	return Store(Heroes) ? Hero : FRPGHeroEntry{};
}
bool RPGHeroes::Delete(const FString& Id)
{
	auto Heroes = Load();
	if (Heroes.RemoveAll([&](const FRPGHeroEntry& H) { return H.Id == Id; }) == 0) { return false; }
	UGameplayStatics::DeleteGameInSlot(SaveSlot(Id), 0);
	return Store(Heroes);
}
bool RPGHeroes::SetLook(const FString& Id, int32 Character, int32 Colour)
{
	auto Heroes = Load();
	for (auto& Hero : Heroes)
	{
		if (Hero.Id != Id) { continue; }
		Hero.Character = ChaosImpactRoster::ClampIndex(Character); Hero.Colour = FMath::Clamp(Colour, 0, ChaosImpactRoster::ColourCount - 1);
		return Store(Heroes);
	}
	return false;
}
void RPGHeroes::Touch(const FString& Id, int32 Level, int32 QuestsDone, float AddSeconds)
{
	auto Heroes = Load();
	for (auto& Hero : Heroes)
	{
		if (Hero.Id != Id) { continue; }
		Hero.Level = Level; Hero.QuestsDone = QuestsDone; Hero.LastPlayed = FDateTime::Now(); Hero.PlaySeconds += FMath::Max(0.0f, AddSeconds);
		Store(Heroes); return;
	}
}
FString RPGHeroes::RandomName()
{
	static const TCHAR* Names[] = {TEXT("ソラ"), TEXT("ハル"), TEXT("ミオ"), TEXT("カイ"), TEXT("ルカ"), TEXT("ナギ"), TEXT("ユズ"), TEXT("リン"),
		TEXT("ヒカリ"), TEXT("アオ"), TEXT("コハク"), TEXT("ツムギ"), TEXT("レン"), TEXT("イブキ"), TEXT("マシロ"), TEXT("トワ")};
	return Names[FMath::RandRange(0, UE_ARRAY_COUNT(Names) - 1)];
}
FString RPGHeroes::TravelOptions(const TArray<FString>& HeroIds)
{
	FString Out = TEXT("?CIRPGHeroes=1");
	for (int32 Index = 0; Index < HeroIds.Num(); ++Index) { if (!HeroIds[Index].IsEmpty()) { Out += FString::Printf(TEXT("?CIRPGHero%d=%s"), Index, *HeroIds[Index]); } }
	return Out;
}

// ---------------------------------------------------------------- layout (paint + click regions)

/** 1280x720 design; a split view (narrower than 16:10) uses a smaller design so its text stays readable. */
static float TitleScale(const FVector2D& Size)
{
	const bool bNarrow = Size.X < Size.Y * 1.4f;
	return FMath::Min(Size.X / (bNarrow ? 900.0f : 1280.0f), Size.Y / (bNarrow ? 640.0f : 720.0f));
}

namespace TitleAct
{
	enum : int32 { Back = 1, Ok = 2, Delete = 3, Random = 4, PrevHero = 5, NextHero = 6, PrevLook = 7, NextLook = 8, ChangeLook = 9,
		ColourBase = 10, HeroBase = 100, Offline = 30, Online = 31, LessPlayers = 32, MorePlayers = 33, HostOnline = 34, SearchOnline = 35, RandomPassword = 36,
		RoomBase = 200, DeleteNo = 900, DeleteYes = 901 };
}

struct FRPGTitleLayout
{
	static constexpr float CardW = 210, CardH = 250;

	static void Footer(RPGUI::FCanvas& C, float H, bool bPad, const TArray<TPair<FString, FString>>& Hints)
	{
		float X = 40;
		for (const auto& Hint : Hints)
		{
			X += RPGUI::Keycap(C, 1, Hint.Key, X, H - 44, 26) + 7;
			RPGUI::Label(C, 1, Hint.Value, X, H - 42, 17, RPGUI::TextSub);
			X += RPGUI::Measure(Hint.Value, 17) + 24;
		}
	}

	static void Header(RPGUI::FCanvas& C, float W, const FString& Title, const FString& Sub)
	{
		if (C.IsPainting()) { ChaosImpactPaint::HudText3D(C.At(1, 6), Title, 48, 34, 38, RPGUI::TextMain, ChaosImpactPaint::ETextAlign::Left, TEXT("BlackItalic")); }
		RPGUI::Label(C, 1, RPGUI::Fit(Sub, W - 100, 17), 52, 88, 17, RPGUI::TextSub);
	}

	static void Layout(RPGUI::FCanvas& C, const URPGTitleWidget& T, float W, float H)
	{
		using ChaosImpactPaint::ETextAlign;
		using ChaosImpactPaint::WithAlpha;
		const bool bPad = C.bGamepad;
		const auto Key = [bPad](const TCHAR* K, const TCHAR* Pad) { return FString(bPad ? Pad : K); };
		const double Now = FPlatformTime::Seconds();
		// The world (and the hero standing in it) stays visible in the middle; only the top and bottom are shaded.
		if (C.IsPainting())
		{
			const auto Back = C.At(0, -3);
			ChaosImpactPaint::HudPoly(Back, {FVector2D(0, 0), FVector2D(W, 0), FVector2D(W, 170), FVector2D(0, 170)},
				ChaosImpactPaint::MakeFill(FLinearColor(0.01f, 0.015f, 0.04f, 0.92f), FLinearColor(0.01f, 0.015f, 0.04f, 0.0f)), 6);
			ChaosImpactPaint::HudPoly(Back, {FVector2D(0, H - 330), FVector2D(W, H - 330), FVector2D(W, H), FVector2D(0, H)},
				ChaosImpactPaint::MakeFill(FLinearColor(0.01f, 0.015f, 0.04f, 0.0f), FLinearColor(0.01f, 0.015f, 0.04f, 0.95f)), 6);
			if (T.Page == URPGTitleWidget::EPage::Press) { Back.Box(0, 0, W, H, FLinearColor(0.0f, 0.01f, 0.03f, 0.35f)); }
		}
		C.bModalOpen = T.bConfirmDelete;
		switch (T.Page)
		{
		case URPGTitleWidget::EPage::Press:
		{
			if (C.IsPainting())
			{
				ChaosImpactPaint::HudText3D(C.At(1, 6), TEXT("CHAOS IMPACT"), W * 0.5f, H * 0.2f, 92, ChaosImpactPaint::FireFill.Top, ETextAlign::Center, TEXT("BlackItalic"), 7);
				ChaosImpactPaint::HudText3D(C.At(1, 6), TEXT("ADVENTURE"), W * 0.5f, H * 0.2f + 112, 40, RPGUI::Cyan, ETextAlign::Center, TEXT("BlackItalic"), 4);
				RPGUI::Label(C, 1, TEXT("こもれびコロニーから広がる、ドッジボールの冒険"), W * 0.5f, H * 0.2f + 172, 19, RPGUI::TextSub, ETextAlign::Center);
				const float Blink = 0.55f + 0.45f * FMath::Sin(Now * 3.2);
				ChaosImpactPaint::FPainter Prompt = C.At(1, 6); Prompt.Alpha = Blink;
				Prompt.Text(TEXT("ボタンを押してはじめる"), W * 0.5f, H - 150, 26, RPGUI::TextMain, ETextAlign::Center, TEXT("Bold"));
				RPGUI::Label(C, 1, TEXT("RPG試作版 ・ セーブはキャラクターごと"), W * 0.5f, H - 60, 14, RPGUI::TextDim, ETextAlign::Center);
			}
			C.Hit(0, 0, W, H, TitleAct::Ok);
			break;
		}
		case URPGTitleWidget::EPage::Heroes:
		{
			// The heroes stand in the plaza; a name plate floats over each one (the selected one larger).
			Header(C, W, T.Mode == URPGTitleWidget::EMode::Join ? TEXT("あなたのキャラクター") : TEXT("キャラクターを選ぶ"),
				T.Mode == URPGTitleWidget::EMode::Join ? TEXT("このプレイヤーで遊ぶキャラクターを選んでください。進み具合はキャラクターごとに保存されます。")
				: TEXT("コロニーに集まっている仲間から選びます。セーブデータはキャラクターごとです。"));
			const float Scale = FMath::Max(0.01f, T.ViewScale);
			for (const auto& View : T.StatueViews)
			{
				if (!View.bOnScreen || View.Entry == INDEX_NONE) { continue; }
				const bool bNew = View.Entry >= T.Heroes.Num();
				const bool bOn = View.Entry == T.HeroCursor;
				const FVector2D Head = View.Head / Scale, Feet = View.Feet / Scale;
				const int32 Action = TitleAct::HeroBase + View.Entry;
				const float PW = bOn ? 200 : 150, PH = bOn ? 60 : 48, PX = Head.X - PW * 0.5f, PY = Head.Y - PH - 18;
				// Click the plate or the figure.
				C.Hit(PX, PY, PW, FMath::Max(PH, Feet.Y - PY), Action);
				if (!C.IsPainting()) { continue; }
				const bool bTaken = !bNew && T.IsTaken(T.Heroes[View.Entry].Id);
				RPGUI::Panel(C, 1, PX, PY, PW, PH, bTaken ? RPGUI::EStyle::Disabled : bOn ? RPGUI::EStyle::Selected : C.IsHovered(Action) ? RPGUI::EStyle::Hover : RPGUI::EStyle::Card,
					bOn ? RPGUI::Highlight : FLinearColor::Transparent, bNew ? RPGUI::Highlight : ChaosImpactRoster::GetColourSwatch(T.Heroes[View.Entry].Colour));
				if (bNew)
				{
					RPGUI::Label(C, 1, bOn ? TEXT("＋ 新しいキャラクター") : TEXT("＋ 新しく作る"), Head.X + 2, PY + (bOn ? 18 : 13), bOn ? 16 : 15, RPGUI::TextMain, ETextAlign::Center);
					continue;
				}
				const auto& Hero = T.Heroes[View.Entry];
				RPGUI::Label(C, 1, RPGUI::Fit(Hero.Name, PW - 26, bOn ? 21 : 17, TEXT("Black")), Head.X + 2, PY + 4, bOn ? 21 : 17, RPGUI::TextMain, ETextAlign::Center, TEXT("Black"));
				RPGUI::Label(C, 1, RPGUI::Fit(FString::Printf(TEXT("Lv.%d　%s"), Hero.Level, ChaosImpactRoster::Get(Hero.Character).Name), PW - 16, 12), Head.X + 2, PY + PH - 20, 12, bTaken ? RPGUI::TextDim : RPGUI::TextSub, ETextAlign::Center);
				// A small pointer from the plate down to the head.
				if (bOn) { HudPolyArrow(C, Head.X, PY + PH + 2); }
			}
			// The selected one, in a panel along the bottom.
			const float IW = 820, IX = W * 0.5f - IW * 0.5f, IY = H - 168;
			RPGUI::Panel(C, 0, IX, IY, IW, 104, RPGUI::EStyle::Panel, RPGUI::Highlight);
			if (T.Heroes.IsValidIndex(T.HeroCursor))
			{
				const auto& Hero = T.Heroes[T.HeroCursor];
				if (C.IsPainting()) { ChaosImpactPaint::HudText3D(C.At(1, 6), Hero.Name, IX + 30, IY + 14, 32, RPGUI::TextMain, ETextAlign::Left, TEXT("BlackItalic")); }
				RPGUI::Label(C, 1, FString::Printf(TEXT("%s　Lv.%d"), ChaosImpactRoster::Get(Hero.Character).Name, Hero.Level), IX + 32, IY + 62, 16, RPGUI::TextSub);
				const float SX = IX + IW - 280;
				RPGUI::Label(C, 1, FString::Printf(TEXT("依頼 %d件達成"), Hero.QuestsDone), SX, IY + 18, 15, RPGUI::TextSub, ETextAlign::Right);
				RPGUI::Label(C, 1, FString::Printf(TEXT("プレイ %d時間%02d分"), FMath::FloorToInt(Hero.PlaySeconds / 3600), FMath::FloorToInt(FMath::Fmod(Hero.PlaySeconds, 3600.0f) / 60)), SX, IY + 42, 15, RPGUI::TextSub, ETextAlign::Right);
				RPGUI::Label(C, 1, TEXT("最終 ") + Hero.LastPlayed.ToString(TEXT("%m/%d %H:%M")), SX, IY + 66, 15, RPGUI::TextDim, ETextAlign::Right);
				RPGUI::Button(C, 1, IX + IW - 254, IY + 26, 230, 52, TEXT("見た目変更"), TitleAct::ChangeLook, false, !T.IsTaken(Hero.Id), Key(TEXT("C"), TEXT("上")));
			}
			else
			{
				if (C.IsPainting()) { ChaosImpactPaint::HudText3D(C.At(1, 6), TEXT("新しいキャラクター"), IX + 30, IY + 14, 30, RPGUI::Highlight, ETextAlign::Left, TEXT("BlackItalic")); }
				RPGUI::Label(C, 1, FString::Printf(TEXT("見た目と名前を決めて仲間に加えます（%d / %d人）"), T.Heroes.Num(), RPGHeroes::MaxHeroes), IX + 32, IY + 62, 16, RPGUI::TextSub);
			}
			RPGUI::Button(C, 1, 30, H * 0.5f - 30, 50, 60, TEXT("‹"), TitleAct::PrevHero, false, T.HeroCursor > 0);
			RPGUI::Button(C, 1, W - 80, H * 0.5f - 30, 50, 60, TEXT("›"), TitleAct::NextHero, false, T.HeroCursor < T.Heroes.Num());
			TArray<TPair<FString, FString>> Hints{{Key(TEXT("←→"), TEXT("左右")), TEXT("選ぶ")}, {Key(TEXT("Enter"), TEXT("決定")), TEXT("このキャラで遊ぶ")}};
			if (T.Heroes.IsValidIndex(T.HeroCursor)) { Hints.Add({Key(TEXT("C"), TEXT("上")), TEXT("見た目")}); Hints.Add({Key(TEXT("Del"), TEXT("左")), TEXT("削除")}); }
			if (T.Mode == URPGTitleWidget::EMode::Title) { Hints.Add({Key(TEXT("Esc"), TEXT("戻る")), TEXT("戻る")}); }
			Footer(C, H, bPad, Hints);
			break;
		}
		case URPGTitleWidget::EPage::Look:
		{
			const FRPGHeroEntry* Editing = T.EditingHero.IsEmpty() ? nullptr : T.Heroes.FindByPredicate([&](const FRPGHeroEntry& H) { return H.Id == T.EditingHero; });
			Header(C, W, Editing ? TEXT("見た目を変える") : TEXT("見た目を選ぶ"), Editing ? FString::Printf(TEXT("%s の姿を選び直します。レベルや装備はそのままです。"), *Editing->Name)
				: FString(TEXT("新しいキャラクターの姿です。あとからタイトルでいつでも変えられます。")));
			const int32 Num = ChaosImpactRoster::Num();
			const float PanelW = 360, X = 40, Y = 140;
			RPGUI::Panel(C, 0, X, Y, PanelW, 64 + Num * 52, RPGUI::EStyle::Panel, WithAlpha(RPGUI::Cyan, 0.7f));
			for (int32 Index = 0; Index < Num; ++Index)
			{
				const int32 Action = TitleAct::HeroBase + Index;
				const bool bOn = Index == T.LookCursor;
				C.Hit(X + 16, Y + 16 + Index * 52, PanelW - 32, 46, Action);
				RPGUI::Panel(C, 1, X + 16, Y + 16 + Index * 52, PanelW - 32, 46, bOn ? RPGUI::EStyle::Selected : C.IsHovered(Action) ? RPGUI::EStyle::Hover : RPGUI::EStyle::Card, bOn ? RPGUI::Highlight : FLinearColor::Transparent);
				RPGUI::Label(C, 1, ChaosImpactRoster::Get(Index).Name, X + 40, Y + 26 + Index * 52, 20, bOn ? RPGUI::TextMain : RPGUI::TextSub);
			}
			if (ChaosImpactRoster::Get(T.LookCursor).bColourVariants)
			{
				const float CY = Y + 30 + Num * 52;
				RPGUI::Label(C, 1, TEXT("色"), X + 24, CY + 4, 17, RPGUI::TextSub);
				for (int32 Colour = 0; Colour < ChaosImpactRoster::ColourCount; ++Colour)
				{
					const FVector2D At(X + 90 + Colour * 62, CY + 16);
					C.Hit(At.X - 22, At.Y - 22, 44, 44, TitleAct::ColourBase + Colour);
					if (!C.IsPainting()) { continue; }
					C.At(1, 3).Disc(At, Colour == T.ColourCursor ? 22 : 18, ChaosImpactPaint::Navy);
					C.At(1, 4).Disc(At, Colour == T.ColourCursor ? 18 : 15, ChaosImpactRoster::GetColourSwatch(Colour));
					if (Colour == T.ColourCursor) { C.At(1, 5).Ring(At, 24, RPGUI::Highlight, 3); }
				}
			}
			RPGUI::Button(C, 1, W - 380, H - 120, 330, 54, Editing ? TEXT("この姿に変える") : TEXT("この姿にする"), TitleAct::Ok, true, true, Key(TEXT("Enter"), TEXT("決定")));
			Footer(C, H, bPad, {{Key(TEXT("↑↓"), TEXT("十字")), TEXT("キャラクター")}, {Key(TEXT("←→"), TEXT("左右")), TEXT("色")}, {Key(TEXT("Esc"), TEXT("戻る")), TEXT("戻る")}});
			break;
		}
		case URPGTitleWidget::EPage::Name:
		{
			Header(C, W, TEXT("名前をつける"), FString::Printf(TEXT("%d文字まで。キーボードで入力するか、「おまかせ」で決められます。"), RPGHeroes::MaxNameLength));
			const float PW = 620, PX = W * 0.5f - PW * 0.5f, PY = H - 330;
			RPGUI::Panel(C, 0, PX, PY, PW, 200, RPGUI::EStyle::Panel, RPGUI::Highlight);
			RPGUI::Label(C, 1, TEXT("なまえ"), PX + 30, PY + 22, 15, RPGUI::TextSub);
			if (C.IsPainting()) { ChaosImpactPaint::RoundedBox(C.At(1, 3), PX + 30, PY + 46, PW - 60, 62, 8, FLinearColor(0, 0, 0, 0.45f)); C.At(1, 4).Box(PX + 30, PY + 106, PW - 60, 2, RPGUI::Highlight); }
			if (T.NameDraft.IsEmpty() && C.IsPainting() && (!T.NameBox || !T.NameBox->HasKeyboardFocus())) { RPGUI::Label(C, 1, TEXT("（未入力）"), PX + 48, PY + 58, 28, RPGUI::TextDim); }
			if (bPad && !T.NameDraft.IsEmpty()) { RPGUI::Label(C, 1, T.NameDraft, PX + 48, PY + 58, 28, RPGUI::TextMain); }
			RPGUI::Button(C, 1, PX + 30, PY + 128, 200, 48, TEXT("おまかせ"), TitleAct::Random, false, true, Key(TEXT("F1"), TEXT("上")));
			RPGUI::Button(C, 1, PX + PW - 290, PY + 128, 260, 48, TEXT("この名前にする"), TitleAct::Ok, true, !T.NameDraft.TrimStartAndEnd().IsEmpty(), Key(TEXT("Enter"), TEXT("決定")));
			Footer(C, H, bPad, {{Key(TEXT("Enter"), TEXT("決定")), TEXT("決定")}, {Key(TEXT("F1"), TEXT("上")), TEXT("おまかせ")}, {Key(TEXT("Esc"), TEXT("戻る")), TEXT("戻る")}});
			break;
		}
		case URPGTitleWidget::EPage::Play:
		{
			Header(C, W, TEXT("あそびかた"), TEXT("遊ぶ人数と、ほかの人と同じ世界で遊ぶかを決めます。そのあと、それぞれがキャラクターを選びます。"));
			const float CW = 300, CY = H - 340, Gap = 24, X0 = W * 0.5f - CW - Gap * 0.5f;
			const TCHAR* Titles[] = {TEXT("オフライン"), TEXT("オンライン")};
			const TCHAR* Subs[] = {TEXT("この機械で 1～4人"), TEXT("合言葉で最大4人、同じ世界で")};
			for (int32 Index = 0; Index < 2; ++Index)
			{
				const float X = X0 + Index * (CW + Gap);
				const bool bOn = T.PlayCursor == Index, bEnabled = true;
				const int32 Action = Index == 0 ? TitleAct::Offline : TitleAct::Online;
				if (bEnabled) { C.Hit(X, CY, CW, 150, Action); }
				RPGUI::Panel(C, 0, X, CY, CW, 150, !bEnabled ? RPGUI::EStyle::Disabled : bOn ? RPGUI::EStyle::Selected : C.IsHovered(Action) ? RPGUI::EStyle::Hover : RPGUI::EStyle::Card,
					bOn ? RPGUI::Highlight : FLinearColor::Transparent, Index == 0 ? RPGUI::Highlight : RPGUI::Cyan);
				if (C.IsPainting()) { ChaosImpactPaint::HudText3D(C.At(1, 6), Titles[Index], X + 30, CY + 26, 34, bEnabled ? RPGUI::TextMain : RPGUI::TextDim, ETextAlign::Left, TEXT("BlackItalic")); }
				RPGUI::Label(C, 1, Subs[Index], X + 32, CY + 90, 16, bEnabled ? RPGUI::TextSub : RPGUI::TextDim);
			}
			// How many play (offline): 2 or more go on to the controller assignment.
			const float RY = CY + 172;
			RPGUI::Label(C, 1, TEXT("あそぶ人数"), X0, RY + 12, 19, RPGUI::TextSub);
			RPGUI::Button(C, 1, X0 + 130, RY, 54, 48, TEXT("‹"), TitleAct::LessPlayers, false, T.Players > 1);
			if (C.IsPainting()) { ChaosImpactPaint::HudText3D(C.At(1, 6), FString::Printf(TEXT("%d人"), T.Players), X0 + 228, RY + 4, 32, RPGUI::Highlight, ETextAlign::Center); }
			RPGUI::Button(C, 1, X0 + 272, RY, 54, 48, TEXT("›"), TitleAct::MorePlayers, false, T.Players < 4);
			RPGUI::Button(C, 1, X0 + CW * 2 + Gap - 280, RY, 280, 54, T.PlayCursor == 1 ? TEXT("合言葉を決める") : T.Players > 1 ? TEXT("コントローラー割り当てへ") : TEXT("キャラクター選択へ"), TitleAct::Ok, true, true, Key(TEXT("Enter"), TEXT("決定")));
			Footer(C, H, bPad, {{Key(TEXT("↑↓"), TEXT("十字")), TEXT("オフライン／オンライン")}, {Key(TEXT("←→"), TEXT("左右")), TEXT("人数")}, {Key(TEXT("Enter"), TEXT("決定")), TEXT("つぎへ")}, {Key(TEXT("Esc"), TEXT("戻る")), TEXT("タイトルへ")}});
			break;
		}
		case URPGTitleWidget::EPage::Online:
		{
			Header(C, W, TEXT("オンライン"), TEXT("同じ合言葉の人と同じ世界で遊べます（最大4人）。依頼はいっしょにも、それぞれ別にも進められます。"));
			const float PW = 660, PX = W * 0.5f - PW * 0.5f, PY = H - 420;
			RPGUI::Panel(C, 0, PX, PY, PW, 330, RPGUI::EStyle::Panel, RPGUI::Cyan);
			RPGUI::Label(C, 1, TEXT("合言葉"), PX + 30, PY + 20, 15, RPGUI::TextSub);
			if (C.IsPainting()) { ChaosImpactPaint::RoundedBox(C.At(1, 3), PX + 30, PY + 44, PW - 260, 58, 8, FLinearColor(0, 0, 0, 0.45f)); C.At(1, 4).Box(PX + 30, PY + 100, PW - 260, 2, RPGUI::Cyan); }
			if (T.PasswordDraft.IsEmpty() && C.IsPainting() && (!T.PasswordBox || !T.PasswordBox->HasKeyboardFocus())) { RPGUI::Label(C, 1, TEXT("（未入力）"), PX + 48, PY + 56, 26, RPGUI::TextDim); }
			if (bPad && !T.PasswordDraft.IsEmpty()) { RPGUI::Label(C, 1, T.PasswordDraft, PX + 48, PY + 56, 26, RPGUI::TextMain); }
			RPGUI::Button(C, 1, PX + PW - 214, PY + 46, 184, 54, TEXT("おまかせ"), TitleAct::RandomPassword, false, true, Key(TEXT("F1"), TEXT("上")));
			const bool bHasPassword = !T.PasswordDraft.TrimStartAndEnd().IsEmpty();
			const TCHAR* Labels[] = {TEXT("この合言葉で世界を開く"), TEXT("この合言葉の世界をさがす")};
			const TCHAR* Notes[] = {TEXT("あなたが遊んでいる間、同じ合言葉の人がいつでも入ってこられます。"), TEXT("見つかった世界に入り、そこで自分のキャラクターを選びます（この機械からは1人）。")};
			const int32 Actions[] = {TitleAct::HostOnline, TitleAct::SearchOnline};
			for (int32 Index = 0; Index < 2; ++Index)
			{
				const float Y = PY + 128 + Index * 96;
				const bool bOn = T.OnlineCursor == Index;
				RPGUI::Button(C, 1, PX + 30, Y, PW - 60, 54, Labels[Index], Actions[Index], bOn, bHasPassword, bOn ? Key(TEXT("Enter"), TEXT("決定")) : FString());
				RPGUI::Label(C, 1, RPGUI::Fit(Notes[Index], PW - 70, 13), PX + 36, Y + 60, 13, RPGUI::TextDim);
			}
			const FString Online = URPGOnlineSubsystem::Get(&T) ? URPGOnlineSubsystem::Get(&T)->GetNotice() : FString();
			if (!Online.IsEmpty()) { RPGUI::Label(C, 1, RPGUI::Fit(Online, PW, 16), W * 0.5f, PY - 30, 16, RPGUI::Bad, ETextAlign::Center); }
			Footer(C, H, bPad, {{Key(TEXT("↑↓"), TEXT("十字")), TEXT("選ぶ")}, {Key(TEXT("Enter"), TEXT("決定")), TEXT("決定")}, {Key(TEXT("F1"), TEXT("上")), TEXT("おまかせ")}, {Key(TEXT("Esc"), TEXT("戻る")), TEXT("戻る")}});
			break;
		}
		case URPGTitleWidget::EPage::Rooms:
		{
			const auto* Online = URPGOnlineSubsystem::Get(&T);
			Header(C, W, TEXT("世界をさがす"), FString::Printf(TEXT("合言葉「%s」で開かれている世界です。入りたい世界を選んでください。"), Online ? *Online->GetPassword() : TEXT("")));
			const float PW = 700, PX = W * 0.5f - PW * 0.5f, PY = 150;
			const auto Listings = Online ? Online->GetListings() : TArray<FRPGWorldListing>();
			RPGUI::Panel(C, 0, PX, PY, PW, H - PY - 90, RPGUI::EStyle::Panel, RPGUI::Cyan);
			if (Listings.IsEmpty())
			{
				const float Dots = FMath::Fmod(Now * 2, 4.0);
				RPGUI::Label(C, 1, Online && Online->HasSearchedOnce() ? FString(TEXT("まだ見つかりません。ホストが同じ合言葉で世界を開くと、ここに出ます"))
					: FString(TEXT("さがしています")) + FString::ChrN(FMath::FloorToInt(Dots), TEXT('・')), W * 0.5f, PY + 60, 18, RPGUI::TextSub, ETextAlign::Center);
			}
			for (int32 Index = 0; Index < Listings.Num() && Index < 6; ++Index)
			{
				const auto& Listing = Listings[Index];
				const float Y = PY + 24 + Index * 70;
				const int32 Action = TitleAct::RoomBase + Index;
				const bool bOn = T.RoomCursor == Index, bFull = Listing.Players >= URPGOnlineSubsystem::MaxPlayers;
				C.Hit(PX + 24, Y, PW - 48, 60, Action);
				RPGUI::Panel(C, 1, PX + 24, Y, PW - 48, 60, bFull ? RPGUI::EStyle::Disabled : bOn ? RPGUI::EStyle::Selected : C.IsHovered(Action) ? RPGUI::EStyle::Hover : RPGUI::EStyle::Card, bOn ? RPGUI::Highlight : FLinearColor::Transparent, RPGUI::Cyan);
				RPGUI::Label(C, 1, RPGUI::Fit(FString::Printf(TEXT("%s の世界"), *Listing.Host), PW - 300, 20), PX + 48, Y + 15, 20, bFull ? RPGUI::TextDim : RPGUI::TextMain);
				RPGUI::Label(C, 1, FString::Printf(TEXT("%d / %d人"), Listing.Players, URPGOnlineSubsystem::MaxPlayers), PX + PW - 170, Y + 17, 18, bFull ? RPGUI::Bad : RPGUI::Highlight, ETextAlign::Right);
				// Connection: three bars.
				const int32 Bars = Listing.PingMs < 60 ? 3 : Listing.PingMs < 150 ? 2 : 1;
				for (int32 Bar = 0; Bar < 3 && C.IsPainting(); ++Bar) { C.At(1, 4).Box(PX + PW - 130 + Bar * 12, Y + 38 - Bar * 8, 8, 10 + Bar * 8, Bar < Bars ? RPGUI::Good : RPGUI::TextDim); }
			}
			const FString Notice = Online ? Online->GetNotice() : FString();
			if (!Notice.IsEmpty()) { RPGUI::Label(C, 1, RPGUI::Fit(Notice, PW, 16), W * 0.5f, H - 120, 16, RPGUI::Bad, ETextAlign::Center); }
			Footer(C, H, bPad, {{Key(TEXT("↑↓"), TEXT("十字")), TEXT("選ぶ")}, {Key(TEXT("Enter"), TEXT("決定")), TEXT("この世界に入る")}, {Key(TEXT("Esc"), TEXT("戻る")), TEXT("戻る")}});
			break;
		}
		case URPGTitleWidget::EPage::Leaving:
			if (C.IsPainting()) { C.At(0, -3).Box(0, 0, W, H, FLinearColor(0.0f, 0.01f, 0.03f, 0.85f)); RPGUI::Label(C, 1, TEXT("世界に入っています…"), W * 0.5f, H * 0.5f - 14, 24, RPGUI::TextMain, ETextAlign::Center); }
			break;
		default:
			break;
		}
		const bool bCanGoBack = T.Page == URPGTitleWidget::EPage::Look || T.Page == URPGTitleWidget::EPage::Name || T.Page == URPGTitleWidget::EPage::Play
			|| T.Page == URPGTitleWidget::EPage::Online || T.Page == URPGTitleWidget::EPage::Rooms || (T.Page == URPGTitleWidget::EPage::Heroes && T.Mode == URPGTitleWidget::EMode::Title);
		if (bCanGoBack && !(T.Page == URPGTitleWidget::EPage::Look && T.Heroes.IsEmpty() && T.Mode == URPGTitleWidget::EMode::Join))
		{ RPGUI::Button(C, 1, W - 240, 36, 200, 46, TEXT("もどる"), TitleAct::Back, false, true, Key(TEXT("Esc"), TEXT("戻る"))); }
		if (T.bConfirmDelete && T.Heroes.IsValidIndex(T.HeroCursor))
		{
			C.bInModal = true;
			RPGUI::DialogVeil(C, 0, 0, W, H);
			RPGUI::Panel(C, 2, W * 0.5f - 300, H * 0.5f - 110, 600, 220, RPGUI::EStyle::Panel, RPGUI::Bad);
			RPGUI::Label(C, 2, FString::Printf(TEXT("「%s」を削除しますか？"), *T.Heroes[T.HeroCursor].Name), W * 0.5f, H * 0.5f - 86, 24, RPGUI::TextMain, ETextAlign::Center);
			RPGUI::Label(C, 2, TEXT("装備・レベル・依頼の進み具合がすべて消え、元に戻せません。"), W * 0.5f, H * 0.5f - 40, 16, RPGUI::TextSub, ETextAlign::Center);
			RPGUI::Button(C, 2, W * 0.5f - 270, H * 0.5f + 20, 250, 50, TEXT("やめる"), TitleAct::DeleteNo, false, true, Key(TEXT("Esc"), TEXT("戻る")));
			RPGUI::Button(C, 2, W * 0.5f + 20, H * 0.5f + 20, 250, 50, TEXT("削除する"), TitleAct::DeleteYes, true, true, Key(TEXT("Enter"), TEXT("決定")));
			C.bInModal = false;
		}
		if (!T.Message.IsEmpty() && FPlatformTime::Seconds() < T.MessageUntil)
		{
			const float MW = FMath::Max(360.0f, RPGUI::Measure(T.Message, 18) + 60);
			RPGUI::Panel(C, 2, W * 0.5f - MW * 0.5f, 120, MW, 44, RPGUI::EStyle::Card, RPGUI::Highlight);
			RPGUI::Label(C, 2, T.Message, W * 0.5f, 130, 18, RPGUI::TextMain, ETextAlign::Center);
		}
	}

	static void HudPolyArrow(RPGUI::FCanvas& C, float X, float Y)
	{
		ChaosImpactPaint::HudPoly(C.At(1, 4), {FVector2D(X - 10, Y), FVector2D(X + 10, Y), FVector2D(X, Y + 12)}, ChaosImpactPaint::SolidFill(RPGUI::Highlight), 1);
	}

	static void RoundedBoxBand(RPGUI::FCanvas& C, float X, float Y, float W, float H, const FLinearColor& Colour)
	{
		if (!C.IsPainting()) { return; }
		ChaosImpactPaint::RoundedBox(C.At(1, 3), X, Y, W, H, 8, ChaosImpactPaint::WithAlpha(Colour, 0.32f));
		ChaosImpactPaint::RoundedBox(C.At(1, 4), X, Y + H - 4, W, 4, 2, Colour);
	}
};

// ---------------------------------------------------------------- heroes standing in the colony

ARPGHeroStatue::ARPGHeroStatue()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("StatueRoot")));
	Body = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("StatueBody"));
	Body->SetupAttachment(GetRootComponent());
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> Mesh(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple"));
	if (Mesh.Succeeded()) { Body->SetSkeletalMesh(Mesh.Object); }
	Body->SetRelativeRotation(FRotator(0, -90, 0));
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Stand = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StatueStand"));
	Stand->SetupAttachment(GetRootComponent());
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder"));
	if (Cylinder.Succeeded()) { Stand->SetStaticMesh(Cylinder.Object); }
	Stand->SetRelativeLocation(FVector(0, 0, 2)); Stand->SetRelativeScale3D(FVector(1.3f, 1.3f, 0.04f));
	Stand->SetCollisionEnabled(ECollisionEnabled::NoCollision); Stand->SetCastShadow(false);
}
void ARPGHeroStatue::Show(int32 Character, int32 Colour)
{
	if (Character == ShownCharacter && Colour == ShownColour) { return; }
	ShownCharacter = Character; ShownColour = Colour;
	if (Puppet) { Puppet->DestroyComponent(); Puppet = nullptr; }
	const bool bEmpty = Character < 0;
	Body->SetHiddenInGame(true);
	if (!bEmpty)
	{
		if (auto* Anim = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle.MM_Idle"))) { Body->PlayAnimation(Anim, true); }
		Puppet = NewObject<UChaosImpactPuppetComponent>(this);
		Puppet->SetupAttachment(Body); Puppet->SetRelativeScale3D(FVector(1.45f)); Puppet->RegisterComponent();
		if (Puppet->Initialize(Body, Character)) { Puppet->SetColourIndex(Colour); Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones; }
			else { Puppet->DestroyComponent(); Puppet = nullptr; Body->SetHiddenInGame(false); }
		}
		// New puppet parts follow the owner-only view too.
		if (AActor* Viewer = GetOwner()) { bHasViewer = false; SetViewer(Viewer); }
	if (auto* Glowing = LoadObject<UMaterialInterface>(nullptr, bEmpty ? TEXT("/Game/ChaosImpact/RPGPrototype/Adventure/Materials/M_GlowWarm.M_GlowWarm") : TEXT("/Game/ChaosImpact/RPGPrototype/Adventure/Materials/M_Stone.M_Stone")))
	{ Stand->SetMaterial(0, Glowing); }
}
void ARPGHeroStatue::SetSelected(bool bSelected)
{
	if (bSelected && !bIsSelected && Puppet) { Puppet->PlaySelectionAppeal(); }
	bIsSelected = bSelected;
}
void ARPGHeroStatue::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// The player it was for has left (a split screen closed): nobody can see it any more.
	if (bHasViewer && !GetOwner()) { Destroy(); return; }
	// The selected one steps a little forward and its stand lights up.
	Glow = FMath::FInterpTo(Glow, bIsSelected ? 1.0f : 0.0f, DeltaSeconds, 8);
	Stand->SetRelativeScale3D(FVector(1.3f + Glow * 0.35f, 1.3f + Glow * 0.35f, 0.04f));
}
void ARPGHeroStatue::SetViewer(AActor* Viewer)
{
	if (bHasViewer && GetOwner() == Viewer) { return; }
	bHasViewer = Viewer != nullptr;
	SetOwner(Viewer);
	TInlineComponentArray<UPrimitiveComponent*> Parts(this);
	for (UPrimitiveComponent* Part : Parts) { Part->SetOnlyOwnerSee(bHasViewer); Part->MarkRenderStateDirty(); }
}
FVector ARPGHeroStatue::Head() const { return GetActorLocation() + FVector(0, 0, ShownCharacter < 0 ? 120 : 215); }

// ---------------------------------------------------------------- the widget

void URPGTitleInputLayer::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	WidgetTree->RootWidget = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("TitleInputCanvas"));
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

UEditableText* URPGTitleInputLayer::AddField(FName Name)
{
	UEditableText* Field = WidgetTree->ConstructWidget<UEditableText>(UEditableText::StaticClass(), Name);
	CastChecked<UCanvasPanel>(WidgetTree->RootWidget)->AddChild(Field);
	return Field;
}

ARPGPrototypeCharacter* URPGTitleWidget::Player() const { return Cast<ARPGPrototypeCharacter>(GetOwningPlayerPawn()); }

void URPGTitleWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RPGTitleRoot"));
		WidgetTree->RootWidget = Root;
		// SObjectWidget paints children BEFORE NativePaint. A higher layer passed to
		// Super::NativePaint cannot lift those children over our custom menu. Keep real
		// editable fields (caret, selection and IME) in a separate player-screen overlay.
		InputLayer = CreateWidget<URPGTitleInputLayer>(GetOwningPlayer());
		NameBox = InputLayer->AddField(TEXT("RPGHeroName"));
		FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 28);
		NameBox->SetFont(Font);
		FEditableTextStyle TextStyle = FCoreStyle::Get().GetWidgetStyle<FEditableTextStyle>("NormalEditableText");
		TextStyle.SetColorAndOpacity(FSlateColor(FLinearColor(0.96f, 0.975f, 1.0f, 1.0f)));
		TextStyle.SetFont(Font);
		NameBox->SetWidgetStyle(TextStyle);
		NameBox->SetClearKeyboardFocusOnCommit(false);
		NameBox->OnTextCommitted.AddDynamic(this, &URPGTitleWidget::OnNameCommitted);
		NameBox->SetVisibility(ESlateVisibility::Collapsed);
		PasswordBox = InputLayer->AddField(TEXT("RPGOnlinePassword"));
		PasswordBox->SetFont(Font); PasswordBox->SetWidgetStyle(TextStyle);
		PasswordBox->SetClearKeyboardFocusOnCommit(false);
		PasswordBox->OnTextCommitted.AddDynamic(this, &URPGTitleWidget::OnPasswordCommitted);
		PasswordBox->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void URPGTitleWidget::Setup(EMode InMode)
{
	Mode = InMode;
	Heroes = RPGHeroes::Load();
	HeroCursor = 0; bFinished = false;
	// Nobody saved yet: there is nobody to pick, so straight on to making one.
	GoTo(Mode == EMode::Title ? EPage::Press : Heroes.IsEmpty() ? EPage::Look : EPage::Heroes);
}

void URPGTitleWidget::NativeDestruct()
{
	if (InputLayer) { InputLayer->RemoveFromParent(); }
	for (auto& Statue : Statues) { if (Statue.IsValid()) { Statue->Destroy(); } }
	Statues.Reset();
	if (auto* P = Player()) { P->SetActorHiddenInGame(false); }
	Super::NativeDestruct();
}

void URPGTitleWidget::PlaceStatues()
{
	auto* P = Player(); auto* PC = GetOwningPlayer();
	if (!P || !PC || !GetWorld()) { return; }
	// A row across the camera's view, in front of where the player stands, all facing the camera.
	FVector View; FRotator Rotation; PC->GetPlayerViewPoint(View, Rotation);
	StatueYaw = Rotation.Yaw + 180;
	StatueSide = FRotator(0, Rotation.Yaw, 0).RotateVector(FVector::RightVector);
	StatueCenter = P->GetActorLocation();
	while (Statues.Num() < StatueSlots + 1)
	{
		FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Statues.Add(GetWorld()->SpawnActor<ARPGHeroStatue>(StatueCenter, FRotator(0, StatueYaw, 0), Spawn));
	}
	Statues.RemoveAll([](const TWeakObjectPtr<ARPGHeroStatue>& Statue) { return !Statue.IsValid(); });
	for (auto& Statue : Statues) { Statue->SetViewer(PC->GetViewTarget()); }
}

void URPGTitleWidget::RefreshStatues()
{
	PlaceStatues();
	const int32 Count = Heroes.Num() + 1;
	const int32 Shown = FMath::Min(Count, StatueSlots);
	StatueFirst = FMath::Clamp(HeroCursor - Shown / 2, 0, FMath::Max(0, Count - Shown));
	StatueViews.SetNum(Statues.Num());
	for (int32 Spot = 0; Spot < Statues.Num(); ++Spot)
	{
		ARPGHeroStatue* Statue = Statues[Spot].Get();
		if (!Statue) { continue; }
		const int32 Entry = Spot < Shown ? StatueFirst + Spot : INDEX_NONE;
		StatueViews[Spot].Entry = Entry;
		Statue->SetActorHiddenInGame(Entry == INDEX_NONE);
		if (Entry == INDEX_NONE) { continue; }
		const FVector Ground = StatueCenter + StatueSide * (Spot - (Shown - 1) * 0.5f) * 270.0f;
		FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(RPGHeroStatue), false, Player());
		const FVector At = GetWorld()->LineTraceSingleByObjectType(Hit, Ground + FVector(0, 0, 400), Ground - FVector(0, 0, 800), FCollisionObjectQueryParams(ECC_WorldStatic), Params)
			? Hit.ImpactPoint : Ground - FVector(0, 0, 90);
		Statue->SetActorLocationAndRotation(At, FRotator(0, StatueYaw, 0));
		if (Heroes.IsValidIndex(Entry)) { Statue->Show(Heroes[Entry].Character, Heroes[Entry].Colour); }
		else { Statue->Show(-1, 0); }
		Statue->SetSelected(Entry == HeroCursor);
	}
}

void URPGTitleWidget::HideStatues()
{
	for (auto& Statue : Statues) { if (Statue.IsValid()) { Statue->SetActorHiddenInGame(true); } }
	for (auto& View : StatueViews) { View.Entry = INDEX_NONE; }
}

void URPGTitleWidget::GoTo(EPage NewPage)
{
	Page = NewPage; PageSince = FPlatformTime::Seconds(); bConfirmDelete = false;
	if (NameBox && PasswordBox)
	{
		const bool bName = Page == EPage::Name, bPassword = Page == EPage::Online;
		const auto* MenuPC = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
		const bool bNativeInput = !(MenuPC && MenuPC->IsUsingGamepad());
		if (InputLayer)
		{
			if ((bName || bPassword) && bNativeInput && !InputLayer->IsInViewport()) { InputLayer->AddToPlayerScreen(41); }
			InputLayer->SetVisibility((bName || bPassword) && bNativeInput ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
		}
		NameBox->SetVisibility(bName && bNativeInput ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		PasswordBox->SetVisibility(bPassword && bNativeInput ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		if (bName) { NameBox->SetText(FText::FromString(NameDraft)); }
		if (bPassword) { PasswordBox->SetText(FText::FromString(PasswordDraft)); }
		if (auto* PC = GetOwningPlayer())
		{
			UEditableText* Box = bName ? NameBox.Get() : bPassword ? PasswordBox.Get() : nullptr;
			if (Box && !(MenuPC && MenuPC->IsUsingGamepad()))
			{
				FInputModeGameAndUI TypeMode; TypeMode.SetWidgetToFocus(Box->TakeWidget()); TypeMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock); TypeMode.SetHideCursorDuringCapture(false);
				PC->SetInputMode(TypeMode); PC->bShowMouseCursor = true; Box->SetKeyboardFocus();
			}
			else { PC->SetInputMode(FInputModeGameOnly().SetConsumeCaptureMouseDown(false)); }
		}
	}
	if (Page == EPage::Heroes) { HeroCursor = FMath::Clamp(HeroCursor, 0, Heroes.Num()); RefreshStatues(); }
	else { HideStatues(); }
	if (auto* P = Player()) { P->SetActorHiddenInGame(Page == EPage::Heroes || Page == EPage::Press); }
	if (Page == EPage::Look) { ShowLook(LookCursor, ColourCursor); }
	SetVisibility(Page == EPage::Assigning ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
}

void URPGTitleWidget::ShowLook(int32 Character, int32 Colour) const
{
	// The hero in the world behind the menu changes to match (the roster model follows the player state).
	if (auto* State = GetOwningPlayer() ? GetOwningPlayer()->GetPlayerState<AChaosImpactPlayerState>() : nullptr)
	{ State->CharacterIndex = ChaosImpactRoster::ClampIndex(Character); State->ColourChoice = FMath::Clamp(Colour, 0, ChaosImpactRoster::ColourCount - 1); }
}

bool URPGTitleWidget::IsTaken(const FString& Id) const
{
	const auto* Self = Player();
	if (!Self || !GetWorld()) { return false; }
	for (TActorIterator<ARPGPrototypeCharacter> It(GetWorld()); It; ++It) { if (*It != Self && !It->IsGrunt() && It->HeroId == Id) { return true; } }
	return false;
}

void URPGTitleWidget::Say(const FString& Text) { Message = Text; MessageUntil = FPlatformTime::Seconds() + 2.5; }

void URPGTitleWidget::NativeTick(const FGeometry& Geometry, float DeltaSeconds)
{
	Super::NativeTick(Geometry, DeltaSeconds);
	const FVector2D Size = Geometry.GetLocalSize();
	const float Scale = TitleScale(Size);
	if (NameBox && Page == EPage::Name && Scale > 0)
	{
		// The text field sits inside the painted name box.
		const float W = Size.X / Scale, H = Size.Y / Scale, PX = W * 0.5f - 310, PY = H - 330;
		if (auto* BoxSlot = Cast<UCanvasPanelSlot>(NameBox->Slot))
		{ BoxSlot->SetPosition(FVector2D(PX + 48, PY + 56) * Scale); BoxSlot->SetSize(FVector2D(560 - 36, 50) * Scale); }
		FSlateFontInfo Font = NameBox->GetFont(); const int32 FontSize = FMath::Max(1, FMath::RoundToInt(28 * Scale));
		if (Font.Size != FontSize) { Font.Size = FontSize; NameBox->SetFont(Font); }
		NameDraft = NameBox->GetText().ToString().Left(RPGHeroes::MaxNameLength);
	}
	if (PasswordBox && Page == EPage::Online && Scale > 0)
	{
		const float W = Size.X / Scale, H = Size.Y / Scale, PX = W * 0.5f - 330, PY = H - 420;
		if (auto* BoxSlot = Cast<UCanvasPanelSlot>(PasswordBox->Slot))
		{ BoxSlot->SetPosition(FVector2D(PX + 48, PY + 52) * Scale); BoxSlot->SetSize(FVector2D(660 - 300, 48) * Scale); }
		FSlateFontInfo Font = PasswordBox->GetFont(); const int32 FontSize = FMath::Max(1, FMath::RoundToInt(26 * Scale));
		if (Font.Size != FontSize) { Font.Size = FontSize; PasswordBox->SetFont(Font); }
		PasswordDraft = PasswordBox->GetText().ToString().Left(URPGOnlineSubsystem::MaxPasswordLength);
	}
	// The world list keeps the cursor on a listed world.
	if (Page == EPage::Rooms) { if (const auto* Online = URPGOnlineSubsystem::Get(this)) { RoomCursor = FMath::Clamp(RoomCursor, 0, FMath::Max(0, FMath::Min(6, Online->GetListings().Num()) - 1)); } }
	auto* PC = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
	if (Page == EPage::Assigning && PC)
	{
		if (PC->ConsumeRPGAssignmentConfirmed())
		{
			StartWithAssignment(PC->GetRPGAssignmentOptions());
		}
		else if (PC->IsGameplayActive()) { GoTo(EPage::Play); }
	}
	// Face the camera so the chosen look is seen from the front.
	if (auto* Hero = Player(); Hero && PC && (Page == EPage::Look || Page == EPage::Name || Page == EPage::Play))
	{
		FVector View; FRotator Rotation; PC->GetPlayerViewPoint(View, Rotation);
		Hero->SetActorRotation(FRotator(0, Rotation.Yaw + 180, 0));
	}
	ViewScale = TitleScale(Size);
	if (Page == EPage::Heroes && PC)
	{
		for (auto& Statue : Statues) { if (Statue.IsValid()) { Statue->SetViewer(PC->GetViewTarget()); } }
		for (int32 Spot = 0; Spot < Statues.Num() && Spot < StatueViews.Num(); ++Spot)
		{
			auto& View = StatueViews[Spot]; const ARPGHeroStatue* Statue = Statues[Spot].Get();
			View.bOnScreen = Statue && View.Entry != INDEX_NONE
				&& UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PC, Statue->Head(), View.Head, true)
				&& UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PC, Statue->GetActorLocation(), View.Feet, true);
		}
	}
	Hover = ActionAtCursor();
}

int32 URPGTitleWidget::NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
	FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool bEnabled) const
{
	// Native editable fields live in InputLayer at player-screen Z=41, above this menu (Z=40).
	const FVector2D Size = Geometry.GetLocalSize();
	const float Scale = TitleScale(Size);
	if (Page != EPage::Assigning && Scale > 0)
	{
		const FGeometry Design = ChaosImpactPaint::MakeAnchor(Geometry, 0, 0, Scale);
		RPGUI::FCanvas C; C.Geometry = &Design; C.Elements = &Elements; C.Layer = Layer + 4; C.Hover = Hover;
		const auto* PC = Cast<AChaosImpactPlayerController>(GetOwningPlayer()); C.bGamepad = PC && PC->IsUsingGamepad();
		FRPGTitleLayout::Layout(C, *this, Size.X / Scale, Size.Y / Scale);
	}
	return Super::NativePaint(Args, Geometry, CullingRect, Elements, Layer + 44, Style, bEnabled);
}

int32 URPGTitleWidget::ActionAtCursor() const
{
	if (!FSlateApplication::IsInitialized()) { return 0; }
	const auto& Geometry = GetCachedGeometry();
	const FVector2D Size = Geometry.GetLocalSize();
	const float Scale = TitleScale(Size);
	if (Scale <= 0) { return 0; }
	RPGUI::FCanvas C;
	FRPGTitleLayout::Layout(C, *this, Size.X / Scale, Size.Y / Scale);
	return C.ActionAt(Geometry.AbsoluteToLocal(FSlateApplication::Get().GetCursorPos()) / Scale);
}

void URPGTitleWidget::HandleClick() { Act(ActionAtCursor()); }

FString URPGTitleWidget::DescribeNameBoxForTest() const
{
	if (!NameBox) { return TEXT("no box"); }
	const FGeometry& G = NameBox->GetCachedGeometry();
	return FString::Printf(TEXT("text '%s' visible %d focus %d size %s at %s opacity %.2f"), *NameBox->GetText().ToString(), static_cast<int32>(NameBox->GetVisibility()),
		NameBox->HasKeyboardFocus(), *G.GetLocalSize().ToString(), *G.GetAbsolutePosition().ToString(), NameBox->GetRenderOpacity());
}

bool URPGTitleWidget::HandleKey(const FKey& Key)
{
	if (bFinished || Page == EPage::Assigning || Page == EPage::Leaving) { return false; }
	// While typing a name or password: space is a letter, and Enter belongs to the field (it commits it).
	const bool bTyping = (Page == EPage::Name && NameBox && NameBox->HasKeyboardFocus()) || (Page == EPage::Online && PasswordBox && PasswordBox->HasKeyboardFocus());
	const bool bOk = (Key == EKeys::Enter && !bTyping) || (Key == EKeys::SpaceBar && Page != EPage::Name && Page != EPage::Online) || Key == EKeys::Gamepad_FaceButton_Bottom;
	// Backspace is text editing on BOTH input pages. Its key-down may bubble to
	// the viewport before Slate receives the separate backspace character event.
	const bool bBack = Key == EKeys::Escape || Key == EKeys::Gamepad_FaceButton_Right
		|| (Key == EKeys::BackSpace && Page != EPage::Name && Page != EPage::Online);
	const bool bLeft = Key == EKeys::Left || Key == EKeys::A || Key == EKeys::Gamepad_DPad_Left || Key == EKeys::Gamepad_LeftStick_Left;
	const bool bRight = Key == EKeys::Right || Key == EKeys::D || Key == EKeys::Gamepad_DPad_Right || Key == EKeys::Gamepad_LeftStick_Right;
	const bool bUp = Key == EKeys::Up || Key == EKeys::W || Key == EKeys::Gamepad_DPad_Up || Key == EKeys::Gamepad_LeftStick_Up;
	const bool bDown = Key == EKeys::Down || Key == EKeys::S || Key == EKeys::Gamepad_DPad_Down || Key == EKeys::Gamepad_LeftStick_Down;
	if (bConfirmDelete) { if (bOk) { Act(TitleAct::DeleteYes); } if (bBack) { Act(TitleAct::DeleteNo); } return true; }
	switch (Page)
	{
	case EPage::Press: if (!Key.IsMouseButton()) { Act(TitleAct::Ok); } return true;
	case EPage::Heroes:
		if (bLeft || bUp) { Act(TitleAct::PrevHero); }
		if (bRight || bDown) { Act(TitleAct::NextHero); }
		if (bOk) { Act(TitleAct::Ok); }
		if (Key == EKeys::Delete || Key == EKeys::Gamepad_FaceButton_Left) { Act(TitleAct::Delete); }
		if (Key == EKeys::C || Key == EKeys::Gamepad_FaceButton_Top) { Act(TitleAct::ChangeLook); }
		if (bBack) { Act(TitleAct::Back); }
		return true;
	case EPage::Look:
		if (bUp) { Act(TitleAct::PrevLook); } if (bDown) { Act(TitleAct::NextLook); }
		if ((bLeft || bRight) && ChaosImpactRoster::Get(LookCursor).bColourVariants)
		{ ColourCursor = (ColourCursor + (bLeft ? ChaosImpactRoster::ColourCount - 1 : 1)) % ChaosImpactRoster::ColourCount; ShowLook(LookCursor, ColourCursor); }
		if (bOk) { Act(TitleAct::Ok); } if (bBack) { Act(TitleAct::Back); }
		return true;
	case EPage::Name:
		if (Key == EKeys::F1 || Key == EKeys::Gamepad_FaceButton_Top) { Act(TitleAct::Random); }
		if (bOk) { Act(TitleAct::Ok); } if (bBack) { Act(TitleAct::Back); }
		return true;
	case EPage::Play:
		if (bLeft) { Act(TitleAct::LessPlayers); } if (bRight) { Act(TitleAct::MorePlayers); }
		if (bUp || bDown) { PlayCursor = 1 - PlayCursor; }
		if (bOk) { Act(TitleAct::Ok); } if (bBack) { Act(TitleAct::Back); }
		return true;
	case EPage::Online:
		// W and S are letters of the password while it is typed.
		if ((bUp || bDown) && !(bTyping && (Key == EKeys::W || Key == EKeys::S))) { OnlineCursor = 1 - OnlineCursor; }
		if (Key == EKeys::F1 || Key == EKeys::Gamepad_FaceButton_Top) { Act(TitleAct::RandomPassword); }
		if (bOk) { Act(OnlineCursor == 0 ? TitleAct::HostOnline : TitleAct::SearchOnline); }
		if (bBack) { Act(TitleAct::Back); }
		return true;
	case EPage::Rooms:
		if (bUp) { RoomCursor = FMath::Max(0, RoomCursor - 1); } if (bDown) { ++RoomCursor; }
		if (bOk) { Act(TitleAct::RoomBase + RoomCursor); }
		if (bBack) { Act(TitleAct::Back); }
		return true;
	default: return true;
	}
}

void URPGTitleWidget::Act(int32 Action)
{
	if (Action == 0 || bFinished) { return; }
	ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiConfirm, .6f);
	if (Action == TitleAct::DeleteNo) { bConfirmDelete = false; return; }
	if (Action == TitleAct::DeleteYes)
	{
		if (Heroes.IsValidIndex(HeroCursor)) { const FString Name = Heroes[HeroCursor].Name; RPGHeroes::Delete(Heroes[HeroCursor].Id); Heroes = RPGHeroes::Load(); Say(Name + TEXT(" を削除しました")); }
		bConfirmDelete = false; GoTo(Heroes.IsEmpty() ? EPage::Look : EPage::Heroes); return;
	}
	switch (Page)
	{
	case EPage::Press: GoTo(EPage::Play); return;
	case EPage::Heroes:
		if (Action == TitleAct::PrevHero || Action == TitleAct::NextHero)
		{ HeroCursor = FMath::Clamp(HeroCursor + (Action == TitleAct::PrevHero ? -1 : 1), 0, Heroes.Num()); RefreshStatues(); return; }
		if (Action >= TitleAct::HeroBase)
		{
			const int32 Index = Action - TitleAct::HeroBase;
			if (Index != HeroCursor) { HeroCursor = Index; RefreshStatues(); return; }
			Action = TitleAct::Ok;
		}
		if (Action == TitleAct::Delete && Heroes.IsValidIndex(HeroCursor)) { bConfirmDelete = true; return; }
		if (Action == TitleAct::ChangeLook && Heroes.IsValidIndex(HeroCursor))
		{
			if (IsTaken(Heroes[HeroCursor].Id)) { Say(TEXT("このキャラクターはほかのプレイヤーが使っています")); return; }
			EditingHero = Heroes[HeroCursor].Id; LookCursor = Heroes[HeroCursor].Character; ColourCursor = Heroes[HeroCursor].Colour;
			GoTo(EPage::Look); return;
		}
		if (Action == TitleAct::Back && Mode == EMode::Title)
		{
			// Back to how many play: the players added for this title leave again.
			if (auto* GameMode = GetWorld()->GetAuthGameMode<ARPGPrototypeGameMode>(); GameMode && GameMode->GetLocalPlayerCount() > 1) { GameMode->SetLocalPlayerCount(1); }
			GoTo(EPage::Play); return;
		}
		if (Action == TitleAct::Ok)
		{
			if (!Heroes.IsValidIndex(HeroCursor))
			{
				if (Heroes.Num() >= RPGHeroes::MaxHeroes) { Say(TEXT("キャラクターがいっぱいです。削除してから作ってください")); return; }
				LookCursor = 0; ColourCursor = FMath::Max(0, Player() ? Player()->GetLocalRPGIndex() : 0) % ChaosImpactRoster::ColourCount; NameDraft.Empty(); GoTo(EPage::Look); return;
			}
			if (IsTaken(Heroes[HeroCursor].Id)) { Say(TEXT("このキャラクターはほかのプレイヤーが使っています")); return; }
			Choose(Heroes[HeroCursor]);
		}
		return;
	case EPage::Look:
		if (Action == TitleAct::PrevLook || Action == TitleAct::NextLook)
		{ LookCursor = (LookCursor + (Action == TitleAct::PrevLook ? ChaosImpactRoster::Num() - 1 : 1)) % ChaosImpactRoster::Num(); ShowLook(LookCursor, ColourCursor); return; }
		if (Action >= TitleAct::HeroBase) { LookCursor = ChaosImpactRoster::ClampIndex(Action - TitleAct::HeroBase); ShowLook(LookCursor, ColourCursor); return; }
		if (Action >= TitleAct::ColourBase && Action < TitleAct::ColourBase + ChaosImpactRoster::ColourCount) { ColourCursor = Action - TitleAct::ColourBase; ShowLook(LookCursor, ColourCursor); return; }
		if (Action == TitleAct::Back)
		{
			if (!EditingHero.IsEmpty()) { EditingHero.Empty(); GoTo(EPage::Heroes); return; }
			GoTo(Heroes.IsEmpty() ? (Mode == EMode::Title ? EPage::Play : EPage::Look) : EPage::Heroes); return;
		}
		if (Action == TitleAct::Ok && !EditingHero.IsEmpty())
		{
			RPGHeroes::SetLook(EditingHero, LookCursor, ColourCursor);
			Heroes = RPGHeroes::Load(); EditingHero.Empty();
			Say(TEXT("見た目を変えました")); GoTo(EPage::Heroes); return;
		}
		if (Action == TitleAct::Ok) { GoTo(EPage::Name); }
		return;
	case EPage::Name:
		if (Action == TitleAct::Random) { NameDraft = RPGHeroes::RandomName(); if (NameBox) { NameBox->SetText(FText::FromString(NameDraft)); } return; }
		if (Action == TitleAct::Back) { GoTo(EPage::Look); return; }
		if (Action == TitleAct::Ok) { ConfirmName(); }
		return;
	case EPage::Online:
		if (Action == TitleAct::Back) { GoTo(EPage::Play); return; }
		if (Action == TitleAct::RandomPassword) { PasswordDraft = URPGOnlineSubsystem::RandomPassword(); if (PasswordBox) { PasswordBox->SetText(FText::FromString(PasswordDraft)); } return; }
		if (Action == TitleAct::HostOnline || Action == TitleAct::SearchOnline) { OnlineCursor = Action == TitleAct::HostOnline ? 0 : 1; OpenOnline(Action == TitleAct::HostOnline); }
		return;
	case EPage::Rooms:
		if (Action == TitleAct::Back) { if (auto* Online = URPGOnlineSubsystem::Get(this)) { Online->StopSearch(); } GoTo(EPage::Online); return; }
		if (Action >= TitleAct::RoomBase && Action < TitleAct::RoomBase + 6)
		{
			auto* Online = URPGOnlineSubsystem::Get(this);
			if (RoomCursor != Action - TitleAct::RoomBase) { RoomCursor = Action - TitleAct::RoomBase; return; }
			if (Online && Online->Join(Action - TitleAct::RoomBase)) { GoTo(EPage::Leaving); }
		}
		return;
	case EPage::Play:
		if (Action == TitleAct::Online) { PlayCursor = 1; return; }
		if (Action == TitleAct::Offline) { PlayCursor = 0; return; }
		if (Action == TitleAct::LessPlayers || Action == TitleAct::MorePlayers) { Players = FMath::Clamp(Players + (Action == TitleAct::LessPlayers ? -1 : 1), 1, 4); return; }
		if (Action == TitleAct::Back) { GoTo(EPage::Press); return; }
		if (Action == TitleAct::Ok)
		{
			auto* PC = Cast<ARPGPrototypePlayerController>(GetOwningPlayer());
			if (!PC) { return; }
			if (PlayCursor == 1)
			{
				// Online: the password first.
				if (PasswordDraft.IsEmpty()) { if (const auto* Online = URPGOnlineSubsystem::Get(this)) { PasswordDraft = Online->GetSavedPassword(); } }
				if (PasswordDraft.IsEmpty()) { PasswordDraft = URPGOnlineSubsystem::RandomPassword(); }
				GoTo(EPage::Online); return;
			}
			if (Players <= 1)
			{
				if (auto* GameMode = GetWorld()->GetAuthGameMode<ARPGPrototypeGameMode>(); GameMode && GameMode->GetLocalPlayerCount() > 1) { GameMode->SetLocalPlayerCount(1); }
				ToHeroes(); return;
			}
			// The same assignment screen as VS: each player presses a button on their controller.
			GoTo(EPage::Assigning); PC->PrepareRPGControllerAssignment(Players);
		}
		return;
	default: return;
	}
}

void URPGTitleWidget::OnPasswordCommitted(const FText& Text, ETextCommit::Type Method)
{
	if (Page != EPage::Online) { return; }
	PasswordDraft = Text.ToString().Left(URPGOnlineSubsystem::MaxPasswordLength);
	if (Method == ETextCommit::OnEnter) { OpenOnline(OnlineCursor == 0); }
}

void URPGTitleWidget::OpenOnline(bool bHost)
{
	auto* Online = URPGOnlineSubsystem::Get(this);
	const FString Password = PasswordDraft.TrimStartAndEnd();
	if (!Online || Password.IsEmpty()) { Say(TEXT("合言葉を入れてください")); return; }
	if (bHost)
	{
		if (!Online->Host(GetWorld(), Password)) { Say(Online->GetNotice().IsEmpty() ? FString(TEXT("世界を開けませんでした")) : Online->GetNotice()); return; }
		Say(FString::Printf(TEXT("合言葉「%s」で世界を開きました。同じ合言葉の人が入ってこられます"), *Password));
		// Then as offline: how many play here, the assignment, the heroes.
		PlayCursor = 0;
		auto* PC = Cast<ARPGPrototypePlayerController>(GetOwningPlayer());
		if (Players > 1 && PC) { GoTo(EPage::Assigning); PC->PrepareRPGControllerAssignment(Players); return; }
		ToHeroes(); return;
	}
	// Searching: this machine joins with one player.
	if (auto* GameMode = GetWorld()->GetAuthGameMode<ARPGPrototypeGameMode>(); GameMode && GameMode->GetLocalPlayerCount() > 1) { GameMode->SetLocalPlayerCount(1); }
	Online->StartSearch(Password); RoomCursor = 0;
	GoTo(EPage::Rooms);
}

void URPGTitleWidget::OnNameCommitted(const FText& Text, ETextCommit::Type Method)
{
	if (Page != EPage::Name) { return; }
	NameDraft = Text.ToString().Left(RPGHeroes::MaxNameLength);
	if (Method == ETextCommit::OnEnter) { ConfirmName(); }
}

void URPGTitleWidget::ConfirmName()
{
	const FString Name = NameDraft.TrimStartAndEnd().Left(RPGHeroes::MaxNameLength);
	if (Name.IsEmpty()) { Say(TEXT("名前を入れてください（「おまかせ」でも決められます）")); return; }
	const FRPGHeroEntry Hero = RPGHeroes::Create(Name, LookCursor, ColourCursor);
	if (Hero.Id.IsEmpty()) { Say(TEXT("キャラクターを作れませんでした")); return; }
	Heroes = RPGHeroes::Load();
	HeroCursor = Heroes.IndexOfByPredicate([&](const FRPGHeroEntry& H) { return H.Id == Hero.Id; });
	Say(Name + TEXT(" が生まれました"));
	Choose(Hero);
}

void URPGTitleWidget::CreateHeroForTest(const FString& Name, int32 Character)
{ LookCursor = Character; ColourCursor = 0; NameDraft = Name; ConfirmName(); }

void URPGTitleWidget::StartWithAssignment(const FString& Options)
{
	if (auto* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ARPGPrototypeGameMode>() : nullptr) { GameMode->ApplyLocalSetup(Options); }
	ToHeroes();
	Say(FString::Printf(TEXT("%d人で遊びます。それぞれ自分の画面でキャラクターを選んでください"), Players));
}

void URPGTitleWidget::ToHeroes()
{
	Heroes = RPGHeroes::Load();
	// Nobody saved yet: there is nobody to pick, so straight on to making one.
	GoTo(Heroes.IsEmpty() ? EPage::Look : EPage::Heroes);
}

void URPGTitleWidget::Choose(const FRPGHeroEntry& Hero)
{
	// The adventure starts right here, in the colony behind the title (no loading).
	ChosenHero = Hero.Id;
	ShowLook(Hero.Character, Hero.Colour);
	bFinished = true;
	if (auto* P = Player()) { P->BeginHero(Hero); }
}

// ---------------------------------------------------------------- the player's hero

bool ARPGPrototypeCharacter::IsInTitle() const { return TitleWidget && !TitleWidget->IsFinished(); }

void ARPGPrototypeCharacter::UpdateHeroFlow()
{
	if (bGrunt || !IsLocallyControlled() || !GetWorld()) { return; }
	auto* PC = Cast<APlayerController>(GetController());
	const int32 Local = GetLocalRPGIndex();
	if (!PC || Local < 0) { return; }
	const FURL& URL = GetWorld()->URL;
	if (!bHeroChecked)
	{
		bHeroChecked = true;
		URPGTitleWidget::EMode Mode = URPGTitleWidget::EMode::Title;
		bool bShow = false;
		const bool bTitle = URL.HasOption(TEXT("CIRPGTitle=1")), bHeroes = URL.HasOption(TEXT("CIRPGHeroes=1"));
		if (bTitle && Local == 0) { bShow = true; }
		else if (bTitle || bHeroes)
		{
			// Players added later (after the title's assignment, ＋ on a free pad, the bag's player count) pick here.
			const FString Id = bHeroes ? URL.GetOption(*FString::Printf(TEXT("CIRPGHero%d="), Local), TEXT("")) : FString();
			const auto Heroes = RPGHeroes::Load();
			if (const auto* Hero = Heroes.FindByPredicate([&](const FRPGHeroEntry& H) { return !Id.IsEmpty() && H.Id == Id; })) { BeginHero(*Hero); }
			// Players 2-4 (or a hero that was deleted) pick theirs on their own screen.
			else { bShow = true; Mode = URPGTitleWidget::EMode::Join; }
		}
		if (bShow && (TitleWidget = CreateWidget<URPGTitleWidget>(PC)) != nullptr)
		{
			TitleWidget->AddToPlayerScreen(40);
			TitleWidget->Setup(Mode);
			SetTrainingMenuFrozen(true);
		}
	}
	if (TitleWidget && TitleWidget->IsFinished())
	{
		TitleWidget->RemoveFromParent(); TitleWidget = nullptr;
		SetTrainingMenuFrozen(false);
		PC->SetInputMode(FInputModeGameOnly().SetConsumeCaptureMouseDown(false));
	}
	const double Now = GetWorld()->GetTimeSeconds();
	if (!HeroId.IsEmpty() && !IsInTitle() && Now >= NextAutosaveAt) { NextAutosaveAt = Now + 60; AutosaveHero(); }
}

void ARPGPrototypeCharacter::BeginHero(const FRPGHeroEntry& Hero)
{
	HeroId = Hero.Id; HeroName = Hero.Name;
	HeroCharacter = ChaosImpactRoster::ClampIndex(Hero.Character); HeroColour = FMath::Clamp(Hero.Colour, 0, ChaosImpactRoster::ColourCount - 1);
	// The name everyone else sees (the multiplayer menu); a guest's is set by the host from its report.
	if (HasAuthority()) { if (auto* NameState = GetPlayerState()) { NameState->SetPlayerName(HeroName); } }
	// The world now plays heroes: player 1 is not sent to the title again, and a player slot that is left and
	// joined again picks a hero rather than taking the one that was there.
	FURL& URL = GetWorld()->URL;
	const int32 HeroSlot = FMath::Max(0, GetLocalRPGIndex());
	URL.RemoveOption(TEXT("CIRPGTitle=")); URL.RemoveOption(*FString::Printf(TEXT("CIRPGHero%d="), HeroSlot));
	if (!URL.HasOption(TEXT("CIRPGHeroes=1"))) { URL.AddOption(TEXT("CIRPGHeroes=1")); }
	if (HeroSlot == 0) { URL.AddOption(*FString::Printf(TEXT("CIRPGHero0=%s"), *HeroId)); }
	HeroStartedAt = GetWorld()->GetTimeSeconds(); NextAutosaveAt = HeroStartedAt + 60;
	if (auto* State = GetPlayerState<AChaosImpactPlayerState>())
	{ State->CharacterIndex = ChaosImpactRoster::ClampIndex(Hero.Character); State->ColourChoice = FMath::Clamp(Hero.Colour, 0, ChaosImpactRoster::ColourCount - 1); }
	if (auto* Loadouts = UChaosImpactLoadoutSubsystem::Get(this))
	{
		FChaosImpactLoadout Loadout; Loadout.Character = Hero.Character; Loadout.Colour = Hero.Colour;
		const int32 Local = FMath::Max(0, GetLocalRPGIndex());
		Loadouts->SetLoadout(Local, Loadout); Loadouts->SetNickname(Local, Hero.Name);
	}
	if (UGameplayStatics::DoesSaveGameExist(RPGHeroes::SaveSlot(HeroId), 0) && LoadTestProgress())
	{ Notice = FString::Printf(TEXT("おかえり、%s（Lv.%d）"), *HeroName, RPGLevel); }
	else
	{ AutosaveHero(); Notice = FString::Printf(TEXT("%s の冒険がはじまる"), *HeroName); }
	NoticeUntil = GetWorld()->GetTimeSeconds() + 4;
}

bool ARPGPrototypeCharacter::AutosaveHero()
{
	if (HeroId.IsEmpty()) { return false; }
	bQuietSave = true;
	const bool bOK = SaveTestProgress();
	bQuietSave = false;
	return bOK;
}

bool ARPGPrototypeCharacter::ReturnToTitle()
{
	auto* PC = Cast<ARPGPrototypePlayerController>(GetController());
	if (!PC || GetLocalRPGIndex() != 0 || HeroId.IsEmpty()) { return false; }
	AutosaveHero();
	// The title is for one player at a time; the others join again after it.
	PC->ConfigureRPGInput(1, 0);
	return PC->OpenExplorationMap(TEXT("?CIRPGTitle=1"));
}
