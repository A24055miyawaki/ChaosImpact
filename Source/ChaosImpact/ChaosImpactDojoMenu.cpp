#include "ChaosImpactDojoMenu.h"

#include "ChaosImpactChargeWidget.h"
#include "ChaosImpactDojo.h"
#include "ChaosImpactDojoLessons.h"
#include "ChaosImpactDojoVideo.h"
#include "ChaosImpactPaint.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactScreen.h"
#include "ChaosImpactSettings.h"
#include "ChaosImpactSfx.h"
#include "Engine/World.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"

using namespace ChaosImpactPaint;

namespace
{
	constexpr int32 DojoVisibleRows = 9;
	constexpr float DojoListX = 70.0f;
	constexpr float DojoListY = 232.0f;
	constexpr float DojoListW = 690.0f;
	constexpr float DojoRowH = 60.0f;
	constexpr float DojoPreviewX = 800.0f;
	constexpr float DojoPreviewW = 730.0f;
	constexpr float DojoVideoH = DojoPreviewW * 9.0f / 16.0f;
	const FLinearColor DojoCard(0.035f, 0.055f, 0.12f, 0.96f);
	const FLinearColor DojoCardHi(0.07f, 0.13f, 0.3f, 1.0f);

	/** A key cap (dark chip, ice rim, the key's name). Returns its width. */
	float MenuKeyCap(const FPainter& P, const float X, const float Y, const FString& Key, const float Size)
	{
		const float W = HudMeasure(Key, Size, TEXT("Black")) + Size * 1.1f;
		const float H = Size * 1.6f;
		RoundedBox(P, X, Y, W, H, H * 0.28f, FMath::Lerp(Navy, Ice, 0.45f));
		RoundedBox(HudAbove(P, 1), X + 2.0f, Y + 2.0f, W - 4.0f, H - 5.0f, H * 0.25f, FLinearColor(0.08f, 0.12f, 0.24f, 1.0f));
		HudAbove(P, 2).Text(Key, X + W * 0.5f, Y + (H - Size * 1.38f) * 0.5f - 1.0f, Size, Paper, ETextAlign::Center, TEXT("Black"));
		return W;
	}

	/** The group's own sign in a round badge: a target (基本), a bolt (応用), a ball (ボール), crossed blades (対戦ルール). */
	void GroupGlyph(const FPainter& P, const int32 Group, const FVector2D& C, const float R, const FLinearColor& Color, const float Time)
	{
		P.Disc(C, R, Navy);
		P.Disc(C, R - 3.0f, WithAlpha(Color, 0.95f));
		const FPainter G = HudAbove(P, 2);
		const FLinearColor Ink2(0.02f, 0.04f, 0.1f, 1.0f);
		switch (Group)
		{
		case 0:
			G.Ring(C, R * 0.62f, Ink2, 3.0f);
			G.Ring(C, R * 0.32f, Ink2, 3.0f);
			G.Disc(C, R * 0.12f, Ink2);
			break;
		case 1:
			HudPoly(G, {C + FVector2D(R * 0.12f, -R * 0.62f), C + FVector2D(-R * 0.34f, R * 0.08f), C + FVector2D(-R * 0.02f, R * 0.08f),
				C + FVector2D(-R * 0.14f, R * 0.62f), C + FVector2D(R * 0.34f, -R * 0.1f), C + FVector2D(R * 0.02f, -R * 0.1f)}, SolidFill(Ink2), 1);
			break;
		case 2:
			UChaosImpactChargeWidget::PaintBallIcon(G, C, R * 0.5f, true, EChaosImpactBallType::Fire, 0.0f, Time);
			break;
		case 4:
		{
			// A star (the challenges).
			TArray<FVector2D> Star;
			for (int32 Point = 0; Point < 10; ++Point)
			{
				const float A = -UE_HALF_PI + Point * UE_PI / 5.0f;
				const float D = Point % 2 == 0 ? R * 0.66f : R * 0.28f;
				Star.Add(C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * D);
			}
			HudPoly(G, Star, SolidFill(Ink2), 1);
			break;
		}
		default:
			G.Line(C + FVector2D(-R * 0.5f, -R * 0.5f), C + FVector2D(R * 0.5f, R * 0.5f), Ink2, 4.0f);
			G.Line(C + FVector2D(R * 0.5f, -R * 0.5f), C + FVector2D(-R * 0.5f, R * 0.5f), Ink2, 4.0f);
			G.Disc(C + FVector2D(-R * 0.5f, R * 0.5f), R * 0.13f, Ink2);
			G.Disc(C + FVector2D(R * 0.5f, R * 0.5f), R * 0.13f, Ink2);
			break;
		}
	}
}

void UChaosImpactDojoMenu::Open(AChaosImpactPlayerController* InController)
{
	Controller = InController;
	bOpen = true;
	OpenedAt = FPlatformTime::Seconds();
	Mouse = FVector2D(-1.0, -1.0);
	// On the lesson just played, else the first in its group not yet cleared.
	const AChaosImpactDojo* Dojo = InController ? AChaosImpactDojo::Find(InController->GetWorld()) : nullptr;
	int32 FocusTab = 0;
	int32 FocusLesson = 0;
	if (Dojo)
	{
		Dojo->GetListFocus(FocusTab, FocusLesson);
	}
	Tab = FMath::Clamp(FocusTab, 0, ChaosImpactDojo::GetCategories().Num() - 1);
	const TArray<FChaosImpactDojoLesson>& Lessons = ChaosImpactDojo::GetCategories()[Tab].Lessons;
	Selected = FMath::Clamp(FocusLesson, 0, Lessons.Num() - 1);
	if (ChaosImpactDojo::IsCleared(Lessons[Selected].Id))
	{
		for (int32 Index = 0; Index < Lessons.Num(); ++Index)
		{
			if (!ChaosImpactDojo::IsCleared(Lessons[Index].Id))
			{
				Selected = Index;
				break;
			}
		}
	}
	SelShown = -1.0f;
	TabShown = -1.0f;
	TellPreview();
}

FBox2D UChaosImpactDojoMenu::TabRect(const int32 Index) const
{
	// The groups share the width (list to preview's right edge).
	const int32 Count = FMath::Max(ChaosImpactDojo::GetCategories().Num(), 1);
	const float W = (DojoPreviewX + DojoPreviewW - DojoListX - (Count - 1) * 14.0f) / Count;
	const float X = DojoListX + Index * (W + 14.0f);
	return FBox2D(FVector2D(X, 138.0f), FVector2D(X + W, 208.0f));
}

int32 UChaosImpactDojoMenu::FirstVisible() const
{
	const int32 Count = ChaosImpactDojo::GetCategories()[Tab].Lessons.Num();
	return FMath::Clamp(Selected - DojoVisibleRows / 2, 0, FMath::Max(Count - DojoVisibleRows, 0));
}

FBox2D UChaosImpactDojoMenu::RowRect(const int32 Row) const
{
	const float Y = DojoListY + Row * DojoRowH;
	return FBox2D(FVector2D(DojoListX, Y), FVector2D(DojoListX + DojoListW, Y + DojoRowH - 8.0f));
}

FBox2D UChaosImpactDojoMenu::StartRect() const
{
	return FBox2D(FVector2D(DojoPreviewX + DojoPreviewW - 250.0f, 778.0f), FVector2D(DojoPreviewX + DojoPreviewW, 840.0f));
}

FBox2D UChaosImpactDojoMenu::BackRect() const
{
	return FBox2D(FVector2D(DojoListX, 790.0f), FVector2D(DojoListX + 250.0f, 840.0f));
}

FString UChaosImpactDojoMenu::GetSelectedClip() const
{
	const TArray<FChaosImpactDojoCategory>& All = ChaosImpactDojo::GetCategories();
	if (!All.IsValidIndex(Tab) || !All[Tab].Lessons.IsValidIndex(Selected) || All[Tab].Lessons[Selected].Slides.IsEmpty())
	{
		return FString();
	}
	const FChaosImpactDojoLesson& L = All[Tab].Lessons[Selected];
	return AChaosImpactDojo::GetClipName(L.Slides[0].Art, L.Ball);
}

void UChaosImpactDojoMenu::TellPreview() const
{
	if (AChaosImpactDojo* Dojo = Controller.IsValid() ? AChaosImpactDojo::Find(Controller->GetWorld()) : nullptr)
	{
		Dojo->SetPreview(Tab, Selected);
	}
}

void UChaosImpactDojoMenu::SetTab(const int32 NewTab)
{
	const int32 Count = ChaosImpactDojo::GetCategories().Num();
	const int32 Wrapped = (NewTab % Count + Count) % Count;
	if (Wrapped != Tab)
	{
		Tab = Wrapped;
		Selected = 0;
		SelShown = -1.0f;
		MovedAt = FPlatformTime::Seconds();
		if (!DemoVideo)
		{
			ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiTab);
			TellPreview();
		}
	}
}

void UChaosImpactDojoMenu::MoveSelection(const int32 Rows)
{
	const int32 Count = ChaosImpactDojo::GetCategories()[Tab].Lessons.Num();
	const int32 Next = (Selected + Rows % Count + Count) % Count;
	if (Next != Selected)
	{
		Selected = Next;
		MovedAt = FPlatformTime::Seconds();
		if (!DemoVideo)
		{
			ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiMove);
			TellPreview();
		}
	}
}

void UChaosImpactDojoMenu::StartSelected()
{
	AChaosImpactPlayerController* PC = Controller.Get();
	AChaosImpactDojo* Dojo = PC ? AChaosImpactDojo::Find(PC->GetWorld()) : nullptr;
	if (!Dojo || FPlatformTime::Seconds() - OpenedAt < 0.25)
	{
		return;
	}
	ChaosImpactSfx::Play2D(PC, EChaosImpactSfx::UiStart);
	bOpen = false;
	Dojo->StartLesson(Tab, Selected);
}

void UChaosImpactDojoMenu::Leave()
{
	if (AChaosImpactPlayerController* PC = Controller.Get(); PC && FPlatformTime::Seconds() - OpenedAt >= 0.25)
	{
		ChaosImpactSfx::Play2D(PC, EChaosImpactSfx::UiBack);
		bOpen = false;
		PC->ShowMenuScreen(EChaosImpactScreen::ModeSelect);
	}
}

bool UChaosImpactDojoMenu::HandleKeyDown(const FKeyEvent& Event)
{
	const FKey Key = Event.GetKey();
	const bool bSwap = ChaosImpactSettings::IsSwitchConfirmRight();
	if (Key == EKeys::Up || Key == EKeys::W || Key == EKeys::Gamepad_DPad_Up)
	{
		MoveSelection(-1);
	}
	else if (Key == EKeys::Down || Key == EKeys::S || Key == EKeys::Gamepad_DPad_Down)
	{
		MoveSelection(1);
	}
	else if (Key == EKeys::Left || Key == EKeys::A || Key == EKeys::Gamepad_DPad_Left || Key == EKeys::Gamepad_LeftShoulder || Key == EKeys::Q)
	{
		SetTab(Tab - 1);
	}
	else if (Key == EKeys::Right || Key == EKeys::D || Key == EKeys::Gamepad_DPad_Right || Key == EKeys::Gamepad_RightShoulder || Key == EKeys::E)
	{
		SetTab(Tab + 1);
	}
	else if (Key == EKeys::Enter || Key == EKeys::SpaceBar || Key == (bSwap ? EKeys::Gamepad_FaceButton_Right : EKeys::Gamepad_FaceButton_Bottom))
	{
		StartSelected();
	}
	else if (Key == EKeys::Escape || Key == EKeys::BackSpace || Key == (bSwap ? EKeys::Gamepad_FaceButton_Bottom : EKeys::Gamepad_FaceButton_Right))
	{
		Leave();
	}
	return true;
}

bool UChaosImpactDojoMenu::HandleAnalog(const FAnalogInputEvent& Event)
{
	const float Value = Event.GetAnalogValue();
	const double Now = FPlatformTime::Seconds();
	if (Event.GetKey() == EKeys::Gamepad_LeftY && FMath::Abs(Value) > 0.6f && Now - StickAt > 0.2)
	{
		StickAt = Now;
		MoveSelection(Value > 0.0f ? -1 : 1);
	}
	else if (Event.GetKey() == EKeys::Gamepad_LeftX && FMath::Abs(Value) > 0.75f && Now - StickAt > 0.3)
	{
		StickAt = Now;
		SetTab(Tab + (Value > 0.0f ? 1 : -1));
	}
	return true;
}

void UChaosImpactDojoMenu::HandleMouseMove(const FVector2D& DesignPoint)
{
	if (DesignPoint.Equals(Mouse, 0.5))
	{
		return;
	}
	Mouse = DesignPoint;
	const int32 Count = ChaosImpactDojo::GetCategories()[Tab].Lessons.Num();
	const int32 First = FirstVisible();
	for (int32 Row = 0; Row < DojoVisibleRows && First + Row < Count; ++Row)
	{
		if (RowRect(Row).IsInside(DesignPoint) && Selected != First + Row)
		{
			// (Only the rows on screen are picked: the list does not scroll under a resting mouse.)
			Selected = First + Row;
			MovedAt = FPlatformTime::Seconds();
			ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiMove);
			TellPreview();
			break;
		}
	}
}

bool UChaosImpactDojoMenu::HandleClick(const FVector2D& DesignPoint)
{
	for (int32 Index = 0; Index < ChaosImpactDojo::GetCategories().Num(); ++Index)
	{
		if (TabRect(Index).IsInside(DesignPoint))
		{
			SetTab(Index);
			return true;
		}
	}
	const int32 Count = ChaosImpactDojo::GetCategories()[Tab].Lessons.Num();
	const int32 First = FirstVisible();
	for (int32 Row = 0; Row < DojoVisibleRows && First + Row < Count; ++Row)
	{
		if (RowRect(Row).IsInside(DesignPoint))
		{
			if (Selected == First + Row)
			{
				StartSelected();
			}
			else
			{
				Selected = First + Row;
				MovedAt = FPlatformTime::Seconds();
				ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiMove);
				TellPreview();
			}
			return true;
		}
	}
	if (StartRect().IsInside(DesignPoint))
	{
		StartSelected();
		return true;
	}
	if (BackRect().IsInside(DesignPoint))
	{
		Leave();
		return true;
	}
	return false;
}

int32 UChaosImpactDojoMenu::Paint(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer) const
{
	if (!bOpen)
	{
		return Layer;
	}
	const TArray<FChaosImpactDojoCategory>& All = ChaosImpactDojo::GetCategories();
	if (!All.IsValidIndex(Tab))
	{
		return Layer;
	}
	const FChaosImpactDojoCategory& Group = All[Tab];
	const double Now = FPlatformTime::Seconds();
	const float Time = static_cast<float>(FMath::Fmod(Now, 3600.0));
	const float Dt = LastPaint > 0.0 ? FMath::Clamp(static_cast<float>(Now - LastPaint), 0.0f, 0.1f) : 0.0f;
	LastPaint = Now;
	const float In = EaseOut(FMath::Clamp(static_cast<float>(Now - OpenedAt) / 0.35f, 0.0f, 1.0f));
	const FPainter Back{Design, Elements, Layer};
	const FPainter P{Design, Elements, Layer + 4, In};
	const bool bPad = Controller.IsValid() && Controller->IsUsingGamepad();
	const FLinearColor Accent = Group.Color;

	// ---- The backdrop: deep navy over the stage, the group's colour washing in from the right, slow stripes, its name huge.
	HudPoly(Back, {FVector2D(-1400.0, 0.0), FVector2D(3000.0, 0.0), FVector2D(3000.0, 900.0), FVector2D(-1400.0, 900.0)},
		MakeFill(FLinearColor(0.012f, 0.024f, 0.06f, 0.94f), FLinearColor(0.004f, 0.008f, 0.02f, 0.97f)), 1);
	HudPoly(HudAbove(Back, 1), {FVector2D(900.0, 0.0), FVector2D(3000.0, 0.0), FVector2D(3000.0, 900.0), FVector2D(600.0, 900.0)},
		SolidFill(WithAlpha(Accent, 0.07f)), 1);
	for (int32 Stripe = 0; Stripe < 14; ++Stripe)
	{
		const float X = FMath::Fmod(Stripe * 170.0f + Time * 18.0f, 2380.0f) - 400.0f;
		HudPoly(HudAbove(Back, 1), {FVector2D(X, 0.0), FVector2D(X + 36.0f, 0.0), FVector2D(X - 264.0f, 900.0), FVector2D(X - 300.0f, 900.0)},
			SolidFill(WithAlpha(Paper, 0.018f)), 1);
	}
	HudAbove(Back, 2).Text(Group.English, 1560.0f, 620.0f, 150.0f, WithAlpha(Accent, 0.07f), ETextAlign::Right, TEXT("BlackItalic"));
	Back.Box(-1400.0f, 0.0f, 4400.0f, 5.0f, Accent);

	// ---- The title: an emblem (a target, ticking round), 訓練場, the line under it.
	{
		const FVector2D E(112.0f, 70.0f);
		P.Disc(E, 44.0f, Navy);
		P.Disc(E, 40.0f, FLinearColor(0.05f, 0.09f, 0.2f, 1.0f));
		for (int32 Tick = 0; Tick < 12; ++Tick)
		{
			const float A = Tick * UE_TWO_PI / 12.0f + Time * 0.6f;
			const FVector2D Way(FMath::Cos(A), FMath::Sin(A));
			HudAbove(P, 1).Line(E + Way * 32.0f, E + Way * 38.0f, WithAlpha(Accent, Tick % 3 == 0 ? 1.0f : 0.45f), 3.0f);
		}
		HudAbove(P, 1).Ring(E, 24.0f, Paper, 3.0f);
		HudAbove(P, 1).Ring(E, 13.0f, Accent, 4.0f);
		HudAbove(P, 1).Disc(E, 5.0f, Paper);
		HudText3D(HudAbove(P, 2), TEXT("訓練場"), 172.0f, 14.0f, 50.0f, Paper, ETextAlign::Left);
		const float TitleW = HudMeasure(TEXT("訓練場"), 50.0f, TEXT("BlackItalic"));
		HudAbove(P, 2).Text(TEXT("DOJO"), 180.0f + TitleW + 14.0f, 44.0f, 18.0f, WithAlpha(Accent, 0.9f), ETextAlign::Left, TEXT("BlackItalic"));
		HudAbove(P, 2).Text(TEXT("ゲームのしくみを、ひとつずつ体でおぼえよう"), 176.0f, 90.0f, 16.0f, WithAlpha(Paper, 0.6f), ETextAlign::Left, TEXT("Bold"));
	}
	// ---- Everything cleared: a ring filling round its percentage.
	{
		int32 Cleared = 0;
		int32 Total = 0;
		for (int32 Index = 0; Index < All.Num(); ++Index)
		{
			Cleared += ChaosImpactDojo::CountCleared(Index);
			Total += All[Index].Lessons.Num();
		}
		const float Share = static_cast<float>(Cleared) / FMath::Max(Total, 1);
		const FVector2D C(1486.0f, 70.0f);
		P.Disc(C, 46.0f, Navy);
		P.Ring(C, 38.0f, FLinearColor(0.08f, 0.12f, 0.24f, 1.0f), 9.0f);
		HudAbove(P, 1).Arc(C, 38.0f, -90.0f, -90.0f + 360.0f * Share * In, Gold, 9.0f);
		HudAbove(P, 2).Text(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Share * 100.0f)), C.X, C.Y - 16.0f, 22.0f, Paper, ETextAlign::Center, TEXT("Black"));
		HudAbove(P, 2).Text(TEXT("クリア"), 1424.0f, 40.0f, 16.0f, WithAlpha(Paper, 0.6f), ETextAlign::Right, TEXT("Bold"));
		HudAbove(P, 2).Text(FString::Printf(TEXT("%d / %d"), Cleared, Total), 1424.0f, 64.0f, 26.0f, Paper, ETextAlign::Right, TEXT("Black"));
	}

	// ---- The groups: slanted tabs with their sign, name, English and progress; the chosen one raised and lit.
	if (TabShown < 0.0f)
	{
		TabShown = static_cast<float>(Tab);
	}
	TabShown = FMath::FInterpTo(TabShown, static_cast<float>(Tab), Dt, 14.0f);
	for (int32 Index = 0; Index < All.Num(); ++Index)
	{
		const FBox2D R = TabRect(Index);
		const bool bOn = Index == Tab;
		const FLinearColor Color = All[Index].Color;
		const float Lift = bOn ? 8.0f : 0.0f;
		const float Top = R.Min.Y - Lift;
		const TArray<FVector2D> Shape = {FVector2D(R.Min.X + 14.0f, Top), FVector2D(R.Max.X, Top), FVector2D(R.Max.X - 14.0f, R.Max.Y),
			FVector2D(R.Min.X, R.Max.Y)};
		if (bOn)
		{
			HudPoly(P, HudInflate(Shape, 6.0f + 2.0f * FMath::Sin(Time * 4.0f)), SolidFill(WithAlpha(Color, 0.25f)), 1);
		}
		HudShard(HudAbove(P, 1), Shape, bOn ? MakeFill(Color * 1.05f, Color * 0.5f) : MakeFill(FLinearColor(0.07f, 0.1f, 0.2f, 0.97f),
			FLinearColor(0.035f, 0.05f, 0.11f, 0.97f)), 2.5f, FVector2D(4.0, 5.0), bOn ? Paper : WithAlpha(Color, 0.55f));
		GroupGlyph(HudAbove(P, 5), Index, FVector2D(R.Min.X + 44.0f, R.GetCenter().Y - Lift * 0.5f), 22.0f, bOn ? Paper : Color, Time);
		const FPainter Label = HudAbove(P, 8);
		Label.Text(All[Index].Name, R.Min.X + 78.0f, Top + 8.0f, HudFit(All[Index].Name, 24.0f, R.GetSize().X - 180.0f), Paper, ETextAlign::Left, TEXT("Black"));
		Label.Text(All[Index].English, R.Min.X + 80.0f, Top + 42.0f, 11.0f, WithAlpha(Paper, bOn ? 0.85f : 0.5f), ETextAlign::Left, TEXT("BlackItalic"));
		const int32 Done = ChaosImpactDojo::CountCleared(Index);
		const int32 Count = All[Index].Lessons.Num();
		Label.Text(FString::Printf(TEXT("%d/%d"), Done, Count), R.Max.X - 30.0f, Top + 10.0f, 22.0f, Done >= Count ? Gold : WithAlpha(Paper, 0.9f),
			ETextAlign::Right, TEXT("Black"));
		// Its progress as a thin bar along the bottom.
		const float BarX = R.Max.X - 112.0f;
		RoundedBox(HudAbove(P, 6), BarX, Top + 46.0f, 80.0f, 6.0f, 3.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.4f));
		RoundedBox(HudAbove(P, 7), BarX, Top + 46.0f, 80.0f * Done / FMath::Max(Count, 1), 6.0f, 3.0f, Done >= Count ? Gold : Paper);
	}
	{
		// The underline gliding under the chosen tab.
		const FBox2D A = TabRect(FMath::FloorToInt(TabShown));
		const FBox2D B = TabRect(FMath::Min(FMath::FloorToInt(TabShown) + 1, All.Num() - 1));
		const float F = FMath::Frac(TabShown);
		const float X = FMath::Lerp(A.Min.X, B.Min.X, F);
		RoundedBox(HudAbove(P, 9), X + 18.0f, 214.0f, A.GetSize().X - 36.0f, 5.0f, 2.5f, Accent);
		// The keys that change it, at either end.
		MenuKeyCap(HudAbove(P, 9), DojoListX - 54.0f, 158.0f, bPad ? TEXT("LB") : TEXT("Q"), 14.0f);
		MenuKeyCap(HudAbove(P, 9), TabRect(All.Num() - 1).Max.X + 12.0f, 158.0f, bPad ? TEXT("RB") : TEXT("E"), 14.0f);
	}

	// ---- The lessons: a card each, the chosen one lit with a highlight that glides to it.
	const int32 Count = Group.Lessons.Num();
	const int32 First = FirstVisible();
	const int32 SelRow = Selected - First;
	if (SelShown < 0.0f)
	{
		SelShown = static_cast<float>(SelRow);
	}
	SelShown = FMath::FInterpTo(SelShown, static_cast<float>(SelRow), Dt, 18.0f);
	{
		const float Y = DojoListY + SelShown * DojoRowH;
		const float Glow = 0.5f + 0.5f * FMath::Sin(Time * 5.0f);
		RoundedBox(P, DojoListX - 6.0f, Y - 6.0f, DojoListW + 12.0f, DojoRowH + 4.0f, 14.0f, WithAlpha(Accent, 0.18f + 0.1f * Glow));
		RoundedBox(HudAbove(P, 1), DojoListX, Y, DojoListW, DojoRowH - 8.0f, 10.0f, DojoCardHi);
		RoundedBox(HudAbove(P, 2), DojoListX, Y, 8.0f, DojoRowH - 8.0f, 4.0f, Accent);
	}
	int32 FirstOpen = INDEX_NONE;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		if (!ChaosImpactDojo::IsCleared(Group.Lessons[Index].Id))
		{
			FirstOpen = Index;
			break;
		}
	}
	for (int32 Row = 0; Row < DojoVisibleRows && First + Row < Count; ++Row)
	{
		const int32 Index = First + Row;
		const FChaosImpactDojoLesson& L = Group.Lessons[Index];
		const FBox2D R = RowRect(Row);
		const bool bOn = Index == Selected;
		const bool bCleared = ChaosImpactDojo::IsCleared(L.Id);
		// (Each card slides in a little after the one above it.)
		const float Enter = EaseOut(FMath::Clamp(static_cast<float>(Now - OpenedAt) / 0.3f - Row * 0.06f, 0.0f, 1.0f));
		const float Shift = (1.0f - Enter) * 40.0f;
		const FPainter Card{Design, Elements, Layer + 4, In * Enter};
		if (!bOn)
		{
			RoundedBox(Card, R.Min.X + Shift, R.Min.Y, R.GetSize().X, R.GetSize().Y, 10.0f, DojoCard);
		}
		// The number in a round badge: gold with a check once cleared.
		const FVector2D Badge(R.Min.X + Shift + 40.0f, R.GetCenter().Y);
		HudAbove(Card, 4).Disc(Badge, 19.0f, bCleared ? Gold : FLinearColor(0.08f, 0.12f, 0.24f, 1.0f));
		HudAbove(Card, 4).Ring(Badge, 19.0f, bCleared ? FLinearColor(1.0f, 0.9f, 0.5f, 1.0f) : WithAlpha(Accent, bOn ? 1.0f : 0.5f), 2.5f);
		if (bCleared)
		{
			HudAbove(Card, 5).Line(Badge + FVector2D(-8.0, 0.0), Badge + FVector2D(-2.0, 7.0), Navy, 4.0f);
			HudAbove(Card, 5).Line(Badge + FVector2D(-2.0, 7.0), Badge + FVector2D(9.0, -8.0), Navy, 4.0f);
		}
		else
		{
			HudAbove(Card, 5).Text(FString::FromInt(Index + 1), Badge.X, Badge.Y - 13.0f, 18.0f, Paper, ETextAlign::Center, TEXT("Black"));
		}
		const float TextX = R.Min.X + Shift + 76.0f;
		const float TextW = DojoListW - 76.0f - 120.0f;
		HudAbove(Card, 5).Text(L.Name, TextX, R.Min.Y + 2.0f, HudFit(L.Name, 20.0f, TextW), bOn ? Paper : WithAlpha(Paper, 0.88f), ETextAlign::Left,
			TEXT("Black"));
		HudAbove(Card, 5).Text(L.Summary, TextX, R.Min.Y + 31.0f, HudFit(L.Summary, 12.0f, TextW, TEXT("Bold")), WithAlpha(Paper, bOn ? 0.75f : 0.5f),
			ETextAlign::Left, TEXT("Bold"));
		// Its state at the right: CLEAR, or NEXT on the first not yet cleared; a chevron on the chosen one.
		const float ChipX = R.Max.X + Shift - 108.0f;
		if (bCleared || Index == FirstOpen)
		{
			const FString Chip = bCleared ? TEXT("CLEAR") : TEXT("NEXT");
			RoundedBox(HudAbove(Card, 4), ChipX, R.GetCenter().Y - 12.0f, 70.0f, 24.0f, 12.0f, bCleared ? WithAlpha(Gold, 0.9f) : WithAlpha(Ice, 0.9f));
			HudAbove(Card, 5).Text(Chip, ChipX + 35.0f, R.GetCenter().Y - 10.0f, 12.0f, Navy, ETextAlign::Center, TEXT("BlackItalic"));
		}
		if (bOn)
		{
			const float Nudge = 3.0f * FMath::Sin(Time * 6.0f);
			const FVector2D Tip(R.Max.X - 18.0f + Nudge, R.GetCenter().Y);
			HudPoly(HudAbove(Card, 5), {Tip, Tip + FVector2D(-12.0, -10.0), Tip + FVector2D(-12.0, 10.0)}, SolidFill(Paper), 1);
		}
	}
	if (First > 0)
	{
		HudPoly(P, {FVector2D(DojoListX + DojoListW * 0.5f, DojoListY - 22.0f), FVector2D(DojoListX + DojoListW * 0.5f - 10.0f, DojoListY - 12.0f),
			FVector2D(DojoListX + DojoListW * 0.5f + 10.0f, DojoListY - 12.0f)}, SolidFill(WithAlpha(Paper, 0.6f)), 1);
	}
	if (First + DojoVisibleRows < Count)
	{
		const float Y = DojoListY + DojoVisibleRows * DojoRowH - 4.0f;
		HudPoly(P, {FVector2D(DojoListX + DojoListW * 0.5f, Y + 10.0f), FVector2D(DojoListX + DojoListW * 0.5f - 10.0f, Y),
			FVector2D(DojoListX + DojoListW * 0.5f + 10.0f, Y)}, SolidFill(WithAlpha(Paper, 0.6f)), 1);
	}

	// ---- The chosen lesson: its clip, filmed from the game, in a frame; its name, its points and its task.
	if (Group.Lessons.IsValidIndex(Selected))
	{
		const FChaosImpactDojoLesson& L = Group.Lessons[Selected];
		const float Since = static_cast<float>(Now - MovedAt);
		const float Swap = EaseOut(FMath::Clamp(Since / 0.22f, 0.0f, 1.0f));
		const FPainter Card{Design, Elements, Layer + 4, In * Swap};
		const float AX = DojoPreviewX + (1.0f - Swap) * 24.0f;
		const float AY = DojoListY;
		// The frame: a dark bezel, a rim in the group's colour, the picture inside.
		RoundedBox(Card, AX - 10.0f, AY - 10.0f, DojoPreviewW + 20.0f, DojoVideoH + 20.0f, 18.0f, Navy);
		RoundedBox(HudAbove(Card, 1), AX - 6.0f, AY - 6.0f, DojoPreviewW + 12.0f, DojoVideoH + 12.0f, 16.0f, WithAlpha(Accent, 0.85f));
		RoundedBox(HudAbove(Card, 2), AX - 2.0f, AY - 2.0f, DojoPreviewW + 4.0f, DojoVideoH + 4.0f, 12.0f, FLinearColor(0.0f, 0.0f, 0.0f, 1.0f));
		const AChaosImpactDojo* Dojo = Controller.IsValid() ? AChaosImpactDojo::Find(Controller->GetWorld()) : nullptr;
		const UChaosImpactDojoVideo* Video = DemoVideo ? DemoVideo.Get() : Dojo ? Dojo->GetVideo() : nullptr;
		const FSlateBrush* Picture = Video ? Video->GetBrush() : nullptr;
		if (Picture)
		{
			FSlateDrawElement::MakeBox(Elements, Layer + 8, Design.ToPaintGeometry(FVector2f(DojoPreviewW, DojoVideoH), FSlateLayoutTransform(FVector2f(AX, AY))),
				Picture, ESlateDrawEffect::None, FLinearColor(1.0f, 1.0f, 1.0f, Card.Alpha));
		}
		else
		{
			const EChaosImpactDojoArt Art = L.Slides.IsEmpty() ? EChaosImpactDojoArt::Review : L.Slides[0].Art;
			AChaosImpactDojo::PaintArt(HudAbove(Card, 4), Art, L.Ball, AX, AY, DojoPreviewW, DojoVideoH, Time);
		}
		// The clip's progress, and a tag in its corner.
		RoundedBox(HudAbove(Card, 12), AX, AY + DojoVideoH - 6.0f, DojoPreviewW, 6.0f, 0.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.55f));
		HudAbove(Card, 13).Box(AX, AY + DojoVideoH - 6.0f, DojoPreviewW * (Video ? Video->GetProgress() : FMath::Frac(Time / 4.0f)), 6.0f,
			FLinearColor(1.0f, 0.2f, 0.22f, 1.0f));
		RoundedBox(HudAbove(Card, 12), AX + 14.0f, AY + 14.0f, 132.0f, 30.0f, 15.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.55f));
		HudAbove(Card, 13).Disc(FVector2D(AX + 32.0f, AY + 29.0f), 6.0f, FLinearColor(1.0f, 0.2f, 0.22f, 0.6f + 0.4f * FMath::Sin(Time * 5.0f)));
		HudAbove(Card, 13).Text(TEXT("PREVIEW"), AX + 44.0f, AY + 18.0f, 14.0f, Paper, ETextAlign::Left, TEXT("BlackItalic"));
		if (ChaosImpactDojo::IsCleared(L.Id))
		{
			// The CLEAR stamp, tilted in the corner.
			const FGeometry Stamp = MakeSkewed(Design, AX + DojoPreviewW - 190.0f, AY + 16.0f, 170.0f, 54.0f, -0.2f);
			const FPainter SP{Stamp, Elements, Layer + 20, Card.Alpha};
			RoundedBox(SP, 0.0f, 0.0f, 170.0f, 54.0f, 10.0f, Navy);
			RoundedBox(HudAbove(SP, 1), 4.0f, 4.0f, 162.0f, 46.0f, 8.0f, Gold);
			HudAbove(SP, 2).Text(TEXT("CLEAR"), 85.0f, 10.0f, 26.0f, Navy, ETextAlign::Center, TEXT("BlackItalic"));
		}
		// Its name with its number.
		const float TY = AY + DojoVideoH + 22.0f;
		const FString Number = FString::Printf(TEXT("LESSON %d"), Selected + 1);
		const float NumberW = HudMeasure(Number, 14.0f, TEXT("BlackItalic")) + 22.0f;
		RoundedBox(HudAbove(Card, 2), AX, TY + 8.0f, NumberW, 26.0f, 13.0f, Accent);
		HudAbove(Card, 3).Text(Number, AX + NumberW * 0.5f, TY + 11.0f, 14.0f, Paper, ETextAlign::Center, TEXT("BlackItalic"));
		HudAbove(Card, 3).Text(L.Name, AX + NumberW + 14.0f, TY, HudFit(L.Name, 30.0f, DojoPreviewW - NumberW - 20.0f), Paper, ETextAlign::Left,
			TEXT("Black"));
		// Its points, from its slides.
		float PY = TY + 50.0f;
		for (int32 Point = 0; Point < FMath::Min(L.Slides.Num(), 2); ++Point)
		{
			HudAbove(Card, 3).Disc(FVector2D(AX + 9.0f, PY + 13.0f), 4.0f, Accent);
			HudAbove(Card, 3).Text(L.Slides[Point].Text, AX + 24.0f, PY, HudFit(L.Slides[Point].Text, 17.0f, DojoPreviewW - 30.0f, TEXT("Bold")),
				WithAlpha(Paper, 0.82f), ETextAlign::Left, TEXT("Bold"));
			PY += 27.0f;
		}
		// The task, with what it counts.
		const float KY = 778.0f;
		const float KW = DojoPreviewW - 270.0f;
		RoundedBox(HudAbove(Card, 2), AX, KY, KW, 62.0f, 12.0f, FLinearColor(0.05f, 0.08f, 0.18f, 1.0f));
		RoundedBox(HudAbove(Card, 3), AX, KY, 70.0f, 62.0f, 12.0f, Accent);
		HudAbove(Card, 4).Text(TEXT("課題"), AX + 35.0f, KY + 17.0f, 19.0f, Paper, ETextAlign::Center, TEXT("Black"));
		const FString Task = FString::Printf(TEXT("%s"), *L.Task);
		HudAbove(Card, 4).Text(Task, AX + 84.0f, KY + 8.0f, HudFit(Task, 19.0f, KW - 100.0f), Paper, ETextAlign::Left, TEXT("Black"));
		const FString Goal = FString::Printf(TEXT("目標 %d%s"), L.Count, *L.Unit);
		HudAbove(Card, 4).Text(Goal, AX + 84.0f, KY + 34.0f, 14.0f, WithAlpha(Gold, 0.95f), ETextAlign::Left, TEXT("Black"));
		// Start: big, in the group's colour, breathing.
		const FBox2D S = StartRect();
		const float Pulse = 0.5f + 0.5f * FMath::Sin(Time * 4.0f);
		const TArray<FVector2D> Button = {FVector2D(S.Min.X + 16.0f, S.Min.Y), FVector2D(S.Max.X, S.Min.Y), FVector2D(S.Max.X - 16.0f, S.Max.Y),
			FVector2D(S.Min.X, S.Max.Y)};
		HudPoly(HudAbove(Card, 2), HudInflate(Button, 5.0f + 5.0f * Pulse), SolidFill(WithAlpha(Accent, 0.25f + 0.2f * Pulse)), 1);
		HudShard(HudAbove(Card, 3), Button, MakeFill(Accent * 1.1f, Accent * 0.55f), 2.5f, FVector2D(4.0, 5.0), Paper);
		const FString StartKey = bPad ? TEXT("A") : TEXT("Enter");
		const float CapW = MenuKeyCap(HudAbove(Card, 8), S.Min.X + 30.0f, S.Min.Y + 17.0f, StartKey, 15.0f);
		const float StartSize = HudFit(TEXT("はじめる"), 24.0f, S.GetSize().X - CapW - 74.0f);
		HudAbove(Card, 8).Text(TEXT("はじめる"), S.Min.X + 42.0f + CapW, S.Min.Y + (62.0f - StartSize * 1.38f) * 0.5f, StartSize, Paper, ETextAlign::Left,
			TEXT("Black"));
	}

	// ---- Back, and the keys along the bottom.
	{
		const FBox2D B = BackRect();
		RoundedBox(P, B.Min.X, B.Min.Y, B.GetSize().X, B.GetSize().Y, 12.0f, DojoCard);
		const float CapW = MenuKeyCap(HudAbove(P, 2), B.Min.X + 16.0f, B.Min.Y + 12.0f, bPad ? TEXT("B") : TEXT("Esc"), 14.0f);
		HudAbove(P, 2).Text(TEXT("モード選択へ"), B.Min.X + 28.0f + CapW, B.Min.Y + 12.0f, 18.0f, WithAlpha(Paper, 0.85f), ETextAlign::Left, TEXT("Black"));
		float X = B.Max.X + 30.0f;
		const auto Guide = [&](const FString& Key, const FString& What)
		{
			X += MenuKeyCap(HudAbove(P, 2), X, B.Min.Y + 12.0f, Key, 13.0f) + 8.0f;
			HudAbove(P, 2).Text(What, X, B.Min.Y + 13.0f, 15.0f, WithAlpha(Paper, 0.6f), ETextAlign::Left, TEXT("Bold"));
			X += HudMeasure(What, 15.0f, TEXT("Bold")) + 22.0f;
		};
		Guide(bPad ? TEXT("↑↓") : TEXT("W/S"), TEXT("えらぶ"));
		Guide(bPad ? TEXT("LB/RB") : TEXT("Q/E"), TEXT("グループ"));
	}
	return Layer + 40;
}
