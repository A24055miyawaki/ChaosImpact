#include "ChaosImpactTouchLayoutEditor.h"

#include "ChaosImpactChargeWidget.h"
#include "Blueprint/UserWidget.h"
#include "ChaosImpactPaint.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactSettings.h"
#include "ChaosImpactSfx.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "Input/Events.h"
#include "Layout/Geometry.h"
#include "Rendering/DrawElements.h"

namespace
{
	using FC = FChaosImpactTouchCustom;

	/** The controls in the order they are looked for under a finger. */
	const int32 PadControls[] = {FC::Cancel, FC::Aim, FC::Swap, FC::Fuse, FC::Jump, FC::Dash, FC::Tackle, FC::Throw, FC::Stick};
	const int32 HudPieces[] = {FC::CornerPause, FC::CornerPractice, FC::CornerReady, FC::CornerWatch, FC::CornerForm, FC::CornerMap, FC::CornerRules,
		FC::Feed, FC::Match, FC::Standings, FC::Respawn, FC::Vitals, FC::Balls, FC::Hints};

	float UnitOf(const FVector2D& Size)
	{
		return FMath::Max(0.25f, static_cast<float>(FMath::Min(Size.X, Size.Y)) / 720.0f);
	}

	const TCHAR* ButtonWord(const int32 Button)
	{
		static const TCHAR* const Words[] = {TEXT("小さく"), TEXT("大きく"), TEXT("戻す"), TEXT("非表示"), TEXT("このモードをぜんぶ戻す"),
			TEXT("全モードに同じ配置"), TEXT("やめる"), TEXT("完了"), TEXT(""), TEXT("閉じる")};
		return Words[FMath::Clamp(Button, 0, UE_ARRAY_COUNT(Words) - 1)];
	}
}

const TArray<FString>& UChaosImpactTouchLayoutEditor::GetModes()
{
	static const TArray<FString> Modes = {TEXT("vs"), TEXT("duel"), TEXT("training"), TEXT("solo"), TEXT("tourney"), TEXT("online"), TEXT("dojo")};
	return Modes;
}

void UChaosImpactTouchLayoutEditor::Open(AChaosImpactPlayerController* InController, const int32 InProfile, const FString& StartMode)
{
	Controller = InController;
	Profile = InProfile;
	Edits.Reset();
	for (const FString& Mode : GetModes())
	{
		Edits.Add(Mode, ChaosImpactSettings::GetTouchLayout(Profile, Mode));
	}
	ModeIndex = FMath::Max(0, GetModes().IndexOfByKey(StartMode));
	const FChaosImpactProfile& Settings = ChaosImpactSettings::GetProfile(Profile);
	bButtons = Settings.TouchStyle == 0;
	bOneHand = Settings.TouchStyle == 2;
	bLeftHanded = Settings.bTouchLeftHanded;
	bAimStick = true;
	Selected = INDEX_NONE;
	bDragging = false;
	bDraggingBar = false;
	bPanelOpen = false;
	BarCenter = FVector2D::ZeroVector;
	PressedButton = INDEX_NONE;
	Note.Reset();
	if (!SampleHud && InController)
	{
		SampleHud = CreateWidget<UChaosImpactChargeWidget>(InController, UChaosImpactChargeWidget::StaticClass());
	}
	OpenedAt = FPlatformTime::Seconds();
	bOpen = true;
	ChaosImpactSfx::Play2D(InController, EChaosImpactSfx::UiOpen);
}

void UChaosImpactTouchLayoutEditor::Close(const bool bKeep)
{
	if (!bOpen)
	{
		return;
	}
	if (bKeep)
	{
		// Every kind of play changed here, kept.
		for (const TPair<FString, FChaosImpactTouchCustom>& Each : Edits)
		{
			if (ChaosImpactSettings::GetTouchLayout(Profile, Each.Key) != Each.Value)
			{
				ChaosImpactSettings::SetTouchLayout(Profile, Each.Key, Each.Value);
			}
		}
	}
	bOpen = false;
	bDragging = false;
	ChaosImpactSfx::Play2D(Controller.Get(), bKeep ? EChaosImpactSfx::UiConfirm : EChaosImpactSfx::UiBack);
}

FChaosImpactTouchCustom& UChaosImpactTouchLayoutEditor::Edited()
{
	return Edits.FindOrAdd(GetModes()[ModeIndex]);
}

const FChaosImpactTouchCustom& UChaosImpactTouchLayoutEditor::GetEdited() const
{
	static const FChaosImpactTouchCustom None;
	const FChaosImpactTouchCustom* Found = Edits.Find(GetModes()[ModeIndex]);
	return Found ? *Found : None;
}

bool UChaosImpactTouchLayoutEditor::IsShown(const int32 Control) const
{
	if (!bButtons && (Control == FC::Throw || Control == FC::Dash || (Control == FC::Tackle && !bOneHand)))
	{
		return false;
	}
	// (片手モード: no 向き stick, no やめる.)
	if (bOneHand && (Control == FC::Aim || Control == FC::Cancel))
	{
		return false;
	}
	return Control != FC::Aim || bAimStick;
}

FBox2D UChaosImpactTouchLayoutEditor::GetHudBox(const int32 Control, const FVector2D& Size) const
{
	// Where the real piece came out (the last picture), else where it stands by the layout's own reckoning.
	if (const FBox2D* Drawn = SampleHud ? SampleHud->GetPreviewBox(Control) : nullptr)
	{
		return Drawn->ExpandBy(4.0);
	}
	return FChaosImpactTouchGestures::HudBox(Control, Size, FChaosImpactTouchGestures::HudScale(Size, ChaosImpactSettings::GetHudSizeFactor()),
		bButtons, bLeftHanded, GetEdited());
}

FVector2D UChaosImpactTouchLayoutEditor::GetCenterOf(const int32 Control, const FVector2D& Size) const
{
	if (FC::IsHud(Control))
	{
		return GetHudBox(Control, Size).GetCenter();
	}
	return FChaosImpactTouchGestures::MakeLayout(Size, bLeftHanded, bButtons, &ShownCustom(), bOneHand).Center(Control);
}

FBox2D UChaosImpactTouchLayoutEditor::GetBarBox(const FVector2D& Size) const
{
	// The small bar: grip, the kind of play ▾, やめる, 完了. Where it was dragged (kept on the screen).
	const float U = UnitOf(Size);
	const float W = FMath::Min((12.0f + 44.0f + 8.0f + 200.0f + 8.0f + 112.0f + 8.0f + 124.0f + 12.0f) * U, static_cast<float>(Size.X) - 16.0f * U);
	const float H = 64.0f * U;
	const FVector2D Center = BarCenter.IsZero() ? FVector2D(Size.X * 0.5, Size.Y * 0.42) : BarCenter;
	const double X = FMath::Clamp(Center.X - W * 0.5, 8.0 * U, Size.X - W - 8.0 * U);
	const double Y = FMath::Clamp(Center.Y - H * 0.5, 8.0 * U, Size.Y - H - 8.0 * U);
	return FBox2D(FVector2D(X, Y), FVector2D(X + W, Y + H));
}

FBox2D UChaosImpactTouchLayoutEditor::GetPickedBox(const FVector2D& Size) const
{
	if (Selected == INDEX_NONE)
	{
		return FBox2D(ForceInit);
	}
	if (FC::IsHud(Selected))
	{
		return GetHudBox(Selected, Size);
	}
	const FChaosImpactTouchLayout Layout = FChaosImpactTouchGestures::MakeLayout(Size, bLeftHanded, bButtons, &ShownCustom(), bOneHand);
	const FVector2D C = Layout.Center(Selected);
	const double R = FMath::Max(static_cast<double>(Layout.Radius[Selected]), 20.0);
	return FBox2D(C - FVector2D(R, R), C + FVector2D(R, R));
}

FBox2D UChaosImpactTouchLayoutEditor::GetPickedRowBox(const FVector2D& Size) const
{
	// The picked-up one's row (its name over four buttons), beside it where it covers the fewest other things.
	if (Selected == INDEX_NONE || bDragging || bPanelOpen)
	{
		return FBox2D(ForceInit);
	}
	const float U = UnitOf(Size);
	const double W = (4.0 * 100.0 + 3.0 * 6.0 + 16.0) * U;
	const double H = (30.0 + 52.0 + 12.0) * U;
	const FBox2D Picked = GetPickedBox(Size);
	const FVector2D C = Picked.GetCenter();
	const double Gap = 12.0 * U;
	const FVector2D Tries[] = {FVector2D(C.X - W * 0.5, Picked.Min.Y - Gap - H), FVector2D(C.X - W * 0.5, Picked.Max.Y + Gap),
		FVector2D(Picked.Min.X - Gap - W, C.Y - H * 0.5), FVector2D(Picked.Max.X + Gap, C.Y - H * 0.5)};
	// Everything else that could be under it.
	TArray<FBox2D> Others;
	const FChaosImpactTouchLayout Layout = FChaosImpactTouchGestures::MakeLayout(Size, bLeftHanded, bButtons, &ShownCustom(), bOneHand);
	for (const int32 Control : PadControls)
	{
		if (Control != Selected && IsShown(Control))
		{
			const FVector2D At = Layout.Center(Control);
			const double R = Layout.Radius[Control];
			Others.Add(FBox2D(At - FVector2D(R, R), At + FVector2D(R, R)));
		}
	}
	for (const int32 Piece : HudPieces)
	{
		if (Piece != Selected && Piece != FC::Hints)
		{
			Others.Add(GetHudBox(Piece, Size));
		}
	}
	Others.Add(GetBarBox(Size));
	FBox2D Best(ForceInit);
	double BestCost = TNumericLimits<double>::Max();
	for (const FVector2D& Try : Tries)
	{
		const FVector2D At(FMath::Clamp(Try.X, 8.0 * U, Size.X - W - 8.0 * U), FMath::Clamp(Try.Y, 8.0 * U, Size.Y - H - 8.0 * U));
		const FBox2D Row(At, At + FVector2D(W, H));
		// (Over the picked-up one itself costs most: it must stay free to drag.)
		double Cost = Row.Intersect(Picked) ? 1000.0 : 0.0;
		for (const FBox2D& Other : Others)
		{
			if (Row.Intersect(Other))
			{
				const FBox2D Both(FVector2D(FMath::Max(Row.Min.X, Other.Min.X), FMath::Max(Row.Min.Y, Other.Min.Y)),
					FVector2D(FMath::Min(Row.Max.X, Other.Max.X), FMath::Min(Row.Max.Y, Other.Max.Y)));
				Cost += Both.GetArea() / FMath::Max(Other.GetArea(), 1.0);
			}
		}
		if (Cost < BestCost - 0.01)
		{
			Best = Row;
			BestCost = Cost;
		}
	}
	return Best;
}

FBox2D UChaosImpactTouchLayoutEditor::GetPanelBox(const FVector2D& Size) const
{
	// The kinds of play (opened from the bar): in the middle, over a dimmed screen.
	const float U = UnitOf(Size);
	const float W = FMath::Min(static_cast<float>(Size.X) - 32.0f * U, 900.0f * U);
	const int32 PerRow = (W - 28.0f * U) / 7.0f < 112.0f * U ? 4 : 7;
	const int32 Rows = (GetModes().Num() + PerRow - 1) / PerRow;
	const float H = (16.0f + 40.0f + 8.0f + Rows * 62.0f + 10.0f + 60.0f + 16.0f) * U;
	const float X = (static_cast<float>(Size.X) - W) * 0.5f;
	const float Y = (static_cast<float>(Size.Y) - H) * 0.5f;
	return FBox2D(FVector2D(X, Y), FVector2D(X + W, Y + H));
}

FBox2D UChaosImpactTouchLayoutEditor::GetTabBox(const int32 Tab, const FVector2D& Size) const
{
	if (!bPanelOpen)
	{
		return FBox2D(ForceInit);
	}
	const float U = UnitOf(Size);
	const FBox2D Panel = GetPanelBox(Size);
	const float Inner = static_cast<float>(Panel.GetSize().X) - 28.0f * U;
	const int32 PerRow = Inner / 7.0f < 112.0f * U ? 4 : 7;
	const float Gap = 8.0f * U;
	const float W = (Inner - Gap * (PerRow - 1)) / PerRow;
	const float X = static_cast<float>(Panel.Min.X) + 14.0f * U + (Tab % PerRow) * (W + Gap);
	const float Y = static_cast<float>(Panel.Min.Y) + (16.0f + 40.0f + 8.0f) * U + (Tab / PerRow) * 62.0f * U;
	return FBox2D(FVector2D(X, Y), FVector2D(X + W, Y + 54.0f * U));
}

FBox2D UChaosImpactTouchLayoutEditor::GetButtonBox(const int32 Button, const FVector2D& Size) const
{
	const float U = UnitOf(Size);
	switch (Button)
	{
	case Smaller:
	case Bigger:
	case ResetOne:
	case Hide:
	{
		const FBox2D Row = GetPickedRowBox(Size);
		if (!Row.bIsValid)
		{
			return FBox2D(ForceInit);
		}
		const double X = Row.Min.X + 8.0 * U + Button * 106.0 * U;
		const double Y = Row.Min.Y + 34.0 * U;
		return FBox2D(FVector2D(X, Y), FVector2D(X + 100.0 * U, Y + 52.0 * U));
	}
	case ModeMenu:
	case Cancel:
	case Done:
	{
		const FBox2D Bar = GetBarBox(Size);
		// (Grip 44, then the kind of play, やめる, 完了; squeezed alike on a narrow screen.)
		const double Fit = Bar.GetSize().X / ((12.0 + 44.0 + 8.0 + 200.0 + 8.0 + 112.0 + 8.0 + 124.0 + 12.0) * U);
		const double Left = Button == ModeMenu ? 64.0 : Button == Cancel ? 272.0 : 392.0;
		const double Width = Button == ModeMenu ? 200.0 : Button == Cancel ? 112.0 : 124.0;
		const double X = Bar.Min.X + Left * U * Fit;
		return FBox2D(FVector2D(X, Bar.Min.Y + 8.0 * U), FVector2D(X + Width * U * Fit, Bar.Max.Y - 8.0 * U));
	}
	default:
	{
		if (!bPanelOpen)
		{
			return FBox2D(ForceInit);
		}
		const FBox2D Panel = GetPanelBox(Size);
		const double Inner = Panel.GetSize().X - 28.0 * U;
		const double Gap = 10.0 * U;
		const double W = (Inner - Gap * 2.0) / 3.0;
		const int32 Column = Button == ResetAll ? 0 : Button == CopyToAll ? 1 : 2;
		const double X = Panel.Min.X + 14.0 * U + Column * (W + Gap);
		const double Y = Panel.Max.Y - 16.0 * U - 60.0 * U;
		return FBox2D(FVector2D(X, Y), FVector2D(X + W, Y + 56.0 * U));
	}
	}
}

int32 UChaosImpactTouchLayoutEditor::PickAt(const FVector2D& Point, const FVector2D& Size) const
{
	// The controls first (the nearest whose circle it is in), then the HUD (the smallest box it is in).
	const FChaosImpactTouchLayout Layout = FChaosImpactTouchGestures::MakeLayout(Size, bLeftHanded, bButtons, &ShownCustom(), bOneHand);
	int32 Best = INDEX_NONE;
	double BestDistance = TNumericLimits<double>::Max();
	for (const int32 Control : PadControls)
	{
		if (!IsShown(Control))
		{
			continue;
		}
		const double Distance = FVector2D::Distance(Point, Layout.Center(Control));
		if (Distance <= Layout.Radius[Control] * 1.25f && Distance < BestDistance)
		{
			Best = Control;
			BestDistance = Distance;
		}
	}
	if (Best != INDEX_NONE)
	{
		return Best;
	}
	double BestArea = TNumericLimits<double>::Max();
	for (const int32 Piece : HudPieces)
	{
		if (!UChaosImpactChargeWidget::IsPieceInMode(Piece, GetMode()))
		{
			continue;
		}
		const FBox2D Box = GetHudBox(Piece, Size);
		const double Area = Box.GetArea();
		if (Box.IsInside(Point) && Area < BestArea)
		{
			Best = Piece;
			BestArea = Area;
		}
	}
	return Best;
}

void UChaosImpactTouchLayoutEditor::PressButton(const int32 Button)
{
	FChaosImpactTouchCustom& Layout = Edited();
	switch (Button)
	{
	case Smaller:
	case Bigger:
		if (Selected != INDEX_NONE)
		{
			float& Scale = Layout.Size[Selected];
			Scale = FMath::Clamp(FMath::RoundToFloat((Scale + (Button == Smaller ? -0.1f : 0.1f)) * 10.0f) / 10.0f, FC::MinSize, FC::MaxSize);
		}
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiValue);
		break;
	case ResetOne:
		if (Selected != INDEX_NONE)
		{
			Layout.Offset[Selected] = FVector2D::ZeroVector;
			Layout.Size[Selected] = 1.0f;
			Layout.SetHidden(Selected, false);
		}
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiValue);
		break;
	case Hide:
		// 非表示 / 表示 (the move stick, 投げる, the HP, the ball holder and the corner buttons always stay).
		if (Selected != INDEX_NONE && FC::CanHide(Selected))
		{
			Layout.SetHidden(Selected, !Layout.IsHidden(Selected));
			ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiValue);
		}
		else
		{
			Note = TEXT("これは非表示にできません");
			NoteAt = FPlatformTime::Seconds();
			ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiDeny);
		}
		break;
	case ResetAll:
		Layout = FChaosImpactTouchCustom();
		Note = FString::Printf(TEXT("%sの配置を、はじめの形に戻しました"), *ChaosImpactSettings::GetTouchModeName(GetMode()));
		NoteAt = FPlatformTime::Seconds();
		bPanelOpen = false;
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiConfirm);
		break;
	case CopyToAll:
	{
		const FChaosImpactTouchCustom Copy = Layout;
		for (const FString& Mode : GetModes())
		{
			Edits.Add(Mode, Copy);
		}
		Note = TEXT("すべてのモードを、この配置にしました");
		NoteAt = FPlatformTime::Seconds();
		bPanelOpen = false;
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiConfirm);
		break;
	}
	case ModeMenu:
		bPanelOpen = true;
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiOpen);
		break;
	case ClosePanel:
		bPanelOpen = false;
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiBack);
		break;
	case Cancel:
		Close(false);
		break;
	default:
		Close(true);
		break;
	}
}

void UChaosImpactTouchLayoutEditor::HandleDown(const FVector2D& Point, const FVector2D& Size)
{
	if (!bOpen || FPlatformTime::Seconds() - OpenedAt < 0.2)
	{
		return;
	}
	PressedButton = INDEX_NONE;
	const auto ButtonAt = [&](const int32 Button) { const FBox2D Box = GetButtonBox(Button, Size); return Box.bIsValid && Box.IsInside(Point); };
	// The kinds of play's panel, while it is open: it alone (a tap outside it closes it).
	if (bPanelOpen)
	{
		if (!GetPanelBox(Size).IsInside(Point))
		{
			bPanelOpen = false;
			ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiBack);
			return;
		}
		for (int32 Tab = 0; Tab < GetModes().Num(); ++Tab)
		{
			if (GetTabBox(Tab, Size).IsInside(Point))
			{
				// Picked: shown, and the panel out of the way.
				ModeIndex = Tab;
				bPanelOpen = false;
				Selected = INDEX_NONE;
				ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiTab);
				return;
			}
		}
		for (const int32 Button : {ResetAll, CopyToAll, ClosePanel})
		{
			if (ButtonAt(Button))
			{
				PressedButton = Button;
			}
		}
		return;
	}
	// The bar: its buttons, or its grip (anywhere else on it) to drag it.
	if (GetBarBox(Size).IsInside(Point))
	{
		for (const int32 Button : {ModeMenu, Cancel, Done})
		{
			if (ButtonAt(Button))
			{
				PressedButton = Button;
				return;
			}
		}
		bDraggingBar = true;
		DragLast = Point;
		DragStart = Point;
		BarStartCenter = GetBarBox(Size).GetCenter();
		return;
	}
	// The picked-up one's row.
	if (GetPickedRowBox(Size).bIsValid && GetPickedRowBox(Size).IsInside(Point))
	{
		for (const int32 Button : {Smaller, Bigger, ResetOne, Hide})
		{
			if (ButtonAt(Button))
			{
				PressedButton = Button;
			}
		}
		return;
	}
	// Else what is under the finger, picked up (nothing: let go of what was).
	const int32 Picked = PickAt(Point, Size);
	if (Picked != INDEX_NONE)
	{
		if (Picked != Selected)
		{
			ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiSelect);
		}
		Selected = Picked;
		bDragging = true;
		DragLast = Point;
		DragStart = Point;
		DragStartOffset = Edited().Offset[Selected];
	}
	else
	{
		Selected = INDEX_NONE;
	}
}

void UChaosImpactTouchLayoutEditor::HandleMove(const FVector2D& Point, const FVector2D& Size)
{
	if (!bOpen)
	{
		return;
	}
	if (bDraggingBar)
	{
		BarCenter = BarStartCenter + (Point - DragStart);
		// (Kept on the screen: what GetBarBox makes of it.)
		BarCenter = GetBarBox(Size).GetCenter();
		DragLast = Point;
		return;
	}
	if (!bDragging || Selected == INDEX_NONE)
	{
		return;
	}
	// Moved with the finger (kept in the screen's 720-pixel units, so it sits the same on any screen; the controls mirror
	// with 左利き). A HUD piece is kept with its middle on the screen.
	const float U = UnitOf(Size);
	// (A finger never leaves the screen: a point off it is not one.)
	if (!FBox2D(FVector2D(-2.0, -2.0), Size + FVector2D(2.0, 2.0)).IsInside(Point))
	{
		return;
	}
	FVector2D Moved = (Point - DragStart) / U;
	if (!FC::IsHud(Selected) && bLeftHanded)
	{
		Moved.X = -Moved.X;
	}
	FChaosImpactTouchCustom& Layout = Edited();
	const FVector2D Before = Layout.Offset[Selected];
	const FVector2D After = DragStartOffset + Moved;
	// A HUD piece is kept with its middle on the screen (its box is last drawn's, moved by the change).
	if (FC::IsHud(Selected) && !FBox2D(FVector2D::ZeroVector, Size).IsInside(GetHudBox(Selected, Size).GetCenter() + (After - Before) * U))
	{
		return;
	}
	Layout.Offset[Selected] = After;
	DragLast = Point;
}

void UChaosImpactTouchLayoutEditor::HandleUp(const FVector2D& Point, const FVector2D& Size)
{
	if (!bOpen)
	{
		return;
	}
	bDragging = false;
	bDraggingBar = false;
	if (PressedButton != INDEX_NONE)
	{
		const FBox2D Box = GetButtonBox(PressedButton, Size);
		const int32 Button = PressedButton;
		PressedButton = INDEX_NONE;
		if (Box.bIsValid && Box.IsInside(Point))
		{
			PressButton(Button);
		}
	}
}

bool UChaosImpactTouchLayoutEditor::HandleKey(const FKeyEvent& Event)
{
	const FKey Key = Event.GetKey();
	if (Key == EKeys::Escape || Key == EKeys::BackSpace || Key == EKeys::Android_Back || Key == EKeys::Gamepad_FaceButton_Right)
	{
		Close(false);
	}
	else if (Key == EKeys::Enter || Key == EKeys::Gamepad_FaceButton_Bottom)
	{
		Close(true);
	}
	return true;
}

void UChaosImpactTouchLayoutEditor::PaintBackdrop(const FGeometry& Geometry, FSlateWindowElementList& Elements, const int32 Layer,
	const FVector2D& Size) const
{
	using namespace ChaosImpactPaint;
	// A still match: night sky, the court leaning away with its two halves, a few players and balls (never the game itself).
	const float W = static_cast<float>(Size.X);
	const float H = static_cast<float>(Size.Y);
	const FPainter Back{Geometry, Elements, Layer};
	for (int32 Band = 0; Band < 24; ++Band)
	{
		const float T = Band / 23.0f;
		Back.Box(0.0f, H * T, W, H / 23.0f + 1.0f, FMath::Lerp(FLinearColor(0.03f, 0.05f, 0.11f, 1.0f), FLinearColor(0.008f, 0.012f, 0.03f, 1.0f), T));
	}
	const FPainter Floor = HudAbove(Back, 1);
	const FVector2D NearL(W * 0.02f, H * 0.98f), NearR(W * 0.98f, H * 0.98f), FarR(W * 0.8f, H * 0.18f), FarL(W * 0.2f, H * 0.18f);
	const FVector2D MidL = FMath::Lerp(FarL, NearL, 0.45f), MidR = FMath::Lerp(FarR, NearR, 0.45f);
	HudPoly(Floor, {FarL, FarR, MidR, MidL}, MakeFill(FLinearColor(0.2f, 0.06f, 0.08f, 1.0f), FLinearColor(0.32f, 0.08f, 0.1f, 1.0f)));
	HudPoly(Floor, {MidL, MidR, NearR, NearL}, MakeFill(FLinearColor(0.06f, 0.14f, 0.3f, 1.0f), FLinearColor(0.09f, 0.22f, 0.45f, 1.0f)));
	const FPainter Lines = HudAbove(Back, 2);
	for (int32 Line = 1; Line < 6; ++Line)
	{
		const float T = Line / 6.0f;
		Lines.Line(FMath::Lerp(FarL, FarR, T), FMath::Lerp(NearL, NearR, T), FLinearColor(1.0f, 1.0f, 1.0f, 0.05f), 2.0f);
	}
	Lines.Line(MidL, MidR, WithAlpha(Paper, 0.6f), 4.0f);
	HudStroke(Lines, {FarL, FarR, NearR, NearL, FarL}, WithAlpha(Paper, 0.35f), 3.0f);
	// The players: this one near (blue), the others far (red); balls on the floor.
	const float U = UnitOf(Size);
	const auto Figure = [&](const FVector2D& Feet, const float Scale, const FLinearColor& Color)
	{
		Lines.Disc(Feet + FVector2D(0.0, 4.0 * Scale), 26.0f * Scale, FLinearColor(0.0f, 0.0f, 0.0f, 0.3f));
		RoundedBox(HudAbove(Lines, 1), static_cast<float>(Feet.X) - 20.0f * Scale, static_cast<float>(Feet.Y) - 56.0f * Scale, 40.0f * Scale,
			56.0f * Scale, 16.0f * Scale, Color);
		HudAbove(Lines, 1).Disc(Feet + FVector2D(0.0, -76.0 * Scale), 20.0f * Scale, Color);
	};
	Figure(FVector2D(W * 0.5f, H * 0.78f), 1.3f * U, FMath::Lerp(Ice, Paper, 0.2f));
	HudAbove(Lines, 2).Ring(FVector2D(W * 0.5f, H * 0.78f + 4.0f * U), 40.0f * U, WithAlpha(Ice, 0.8f), 3.0f * U);
	Figure(FVector2D(W * 0.36f, H * 0.33f), 0.8f * U, FMath::Lerp(Fire, Paper, 0.2f));
	Figure(FVector2D(W * 0.62f, H * 0.28f), 0.75f * U, FMath::Lerp(Fire, Paper, 0.2f));
	for (const FVector2D& Ball : {FVector2D(W * 0.42f, H * 0.6f), FVector2D(W * 0.6f, H * 0.66f), FVector2D(W * 0.5f, H * 0.42f)})
	{
		HudAbove(Lines, 1).Disc(Ball, 13.0f * U, Paper);
		HudAbove(Lines, 2).Disc(Ball - FVector2D(4.0, 4.0) * U, 4.0f * U, FLinearColor::White);
	}
	// Dimmed a little, so what is being placed stands out.
	HudAbove(Back, 3).Box(0.0f, 0.0f, W, H, FLinearColor(0.0f, 0.0f, 0.02f, 0.25f));
}

void UChaosImpactTouchLayoutEditor::PaintPieceFrame(const FGeometry& Geometry, FSlateWindowElementList& Elements, const int32 Layer,
	const int32 Control, const FBox2D& Box, const float Unit) const
{
	using namespace ChaosImpactPaint;
	// The real piece is under it: here only its frame (gold when picked up), its name, and 非表示 when hidden.
	const float X = static_cast<float>(Box.Min.X);
	const float Y = static_cast<float>(Box.Min.Y);
	const float W = static_cast<float>(Box.GetSize().X);
	const float H = static_cast<float>(Box.GetSize().Y);
	const bool bPicked = Selected == Control;
	const FPainter Plate{Geometry, Elements, Layer};
	if (GetEdited().IsHidden(Control))
	{
		RoundedBox(Plate, X, Y, W, H, 10.0f * Unit, FLinearColor(0.0f, 0.0f, 0.02f, 0.72f));
		const FString Word = TEXT("非表示");
		HudAbove(Plate, 1).Text(Word, X + W * 0.5f, Y + H * 0.5f - 14.0f * Unit, HudFit(Word, 22.0f * Unit, W - 8.0f * Unit), WithAlpha(Paper, 0.8f),
			ETextAlign::Center, TEXT("Black"), 2.0f, Ink);
	}
	else if (bPicked)
	{
		RoundedBox(Plate, X, Y, W, H, 10.0f * Unit, WithAlpha(Gold, 0.12f));
	}
	const FString Name = FC::GetName(Control);
	const float NameSize = HudFit(Name, 15.0f * Unit, FMath::Max(W, 160.0f * Unit), TEXT("Bold"));
	const float NameY = Y - NameSize * 1.45f < 0.0f ? Y + H + 2.0f * Unit : Y - NameSize * 1.45f;
	HudAbove(Plate, 3).Text(Name, X + 6.0f * Unit, NameY, NameSize, bPicked ? Gold : WithAlpha(Paper, 0.8f), ETextAlign::Left, TEXT("Bold"), 2.0f, Ink);
	const FLinearColor Frame = bPicked ? Gold : WithAlpha(Paper, 0.4f);
	const float Width = (bPicked ? 4.0f : 1.5f) * Unit;
	HudStroke(HudAbove(Plate, 4), {FVector2D(X, Y), FVector2D(X + W, Y), FVector2D(X + W, Y + H), FVector2D(X, Y + H), FVector2D(X, Y)}, Frame, Width);
}

void UChaosImpactTouchLayoutEditor::PaintPanel(const FGeometry& Geometry, FSlateWindowElementList& Elements, const int32 Layer,
	const FVector2D& Size) const
{
	using namespace ChaosImpactPaint;
	const float U = UnitOf(Size);
	const FPainter P{Geometry, Elements, Layer};
	const auto Plate = [&](const FPainter& On, const FBox2D& Box, const float Radius)
	{
		const float X = static_cast<float>(Box.Min.X);
		const float Y = static_cast<float>(Box.Min.Y);
		const float W = static_cast<float>(Box.GetSize().X);
		const float H = static_cast<float>(Box.GetSize().Y);
		RoundedBox(On, X + 5.0f * U, Y + 7.0f * U, W, H, Radius, FLinearColor(0.0f, 0.0f, 0.0f, 0.45f));
		RoundedBox(HudAbove(On, 1), X, Y, W, H, Radius, FLinearColor(0.025f, 0.04f, 0.1f, 0.95f));
		HudAbove(On, 2).Box(X + Radius, Y, W - Radius * 2.0f, 3.0f * U, WithAlpha(Ice, 0.8f));
	};
	const auto Button = [&](const FPainter& On, const int32 Which, const FString& Word, const FHudFill& Fill)
	{
		const FBox2D Box = GetButtonBox(Which, Size);
		if (!Box.bIsValid)
		{
			return;
		}
		const float X = static_cast<float>(Box.Min.X);
		const float Y = static_cast<float>(Box.Min.Y) + (PressedButton == Which ? 2.0f * U : 0.0f);
		const float W = static_cast<float>(Box.GetSize().X);
		const float H = static_cast<float>(Box.GetSize().Y);
		HudShard(On, {FVector2D(X + 7.0f * U, Y), FVector2D(X + W, Y), FVector2D(X + W - 7.0f * U, Y + H), FVector2D(X, Y + H)}, Fill, 2.0f * U,
			FVector2D(2.0, 3.0) * U);
		const float TextSize = HudFit(Word, 20.0f * U, W - 16.0f * U);
		HudAbove(On, 4).Text(Word, X + W * 0.5f, Y + (H - TextSize * 1.38f) * 0.5f, TextSize, Paper, ETextAlign::Center);
	};

	// The picked-up one's row: its name and size, then 小さく, 大きく, 戻す, 非表示.
	const FBox2D Row = GetPickedRowBox(Size);
	if (Row.bIsValid)
	{
		const FPainter On = HudAbove(P, 0);
		Plate(On, Row, 12.0f * U);
		const FString Name = FString::Printf(TEXT("%s　%d%%%s"), FC::GetName(Selected), FMath::RoundToInt(GetEdited().Size[Selected] * 100.0f),
			GetEdited().IsHidden(Selected) ? TEXT("（非表示）") : TEXT(""));
		const float NameSize = HudFit(Name, 18.0f * U, static_cast<float>(Row.GetSize().X) - 20.0f * U);
		HudAbove(On, 3).Text(Name, static_cast<float>(Row.Min.X) + 12.0f * U, static_cast<float>(Row.Min.Y) + 6.0f * U, NameSize, Gold);
		const FPainter Keys = HudAbove(On, 3);
		Button(Keys, Smaller, ButtonWord(Smaller), NightFill);
		Button(Keys, Bigger, ButtonWord(Bigger), NightFill);
		Button(Keys, ResetOne, ButtonWord(ResetOne), NightFill);
		Button(Keys, Hide, GetEdited().IsHidden(Selected) ? FString(TEXT("表示")) : FString(ButtonWord(Hide)),
			FC::CanHide(Selected) ? NightFill : SolidFill(FLinearColor(0.12f, 0.13f, 0.17f, 0.9f)));
	}

	// The bar: its grip, the kind of play ▾, やめる, 完了.
	{
		const FPainter On = HudAbove(P, 10);
		const FBox2D Bar = GetBarBox(Size);
		Plate(On, Bar, 16.0f * U);
		const float GX = static_cast<float>(Bar.Min.X) + 34.0f * U;
		const float GY = static_cast<float>(Bar.GetCenter().Y);
		for (int32 Dot = 0; Dot < 6; ++Dot)
		{
			HudAbove(On, 3).Disc(FVector2D(GX + (Dot % 2) * 10.0f * U - 5.0f * U, GY + (Dot / 2 - 1) * 10.0f * U), 2.6f * U, WithAlpha(Paper, 0.7f));
		}
		const FPainter Keys = HudAbove(On, 3);
		Button(Keys, ModeMenu, ChaosImpactSettings::GetTouchModeName(GetMode()) + TEXT("　▼"), IceFill);
		Button(Keys, Cancel, ButtonWord(Cancel), SolidFill(FLinearColor(0.14f, 0.16f, 0.22f, 1.0f)));
		Button(Keys, Done, ButtonWord(Done), FireFill);
		// A word under it now and then (what was just done; at first, how it works).
		const bool bNote = FPlatformTime::Seconds() - NoteAt < 2.5;
		const bool bFirst = FPlatformTime::Seconds() - OpenedAt < 6.0 && Selected == INDEX_NONE;
		if (bNote || bFirst)
		{
			const FString Line = bNote ? Note : FString::Printf(TEXT("%s用の配置です　ドラッグで動かす・タップで大きさや非表示"), ChaosImpactSettings::IsScreenUpright() ? TEXT("縦画面") : TEXT("横画面"));
			const float LineSize = HudFit(Line, 18.0f * U, static_cast<float>(Size.X) - 40.0f * U);
			const float LineY = Bar.Max.Y + 8.0f * U + 40.0f * U < Size.Y ? static_cast<float>(Bar.Max.Y) + 8.0f * U : static_cast<float>(Bar.Min.Y) - 34.0f * U;
			HudAbove(On, 5).Text(Line, static_cast<float>(Bar.GetCenter().X), LineY, LineSize, bNote ? Gold : Paper, ETextAlign::Center, TEXT("Black"),
				2.0f, Ink);
		}
	}

	// The kinds of play, when opened from the bar: over a dimmed screen.
	if (bPanelOpen)
	{
		const FPainter Dim = HudAbove(P, 20);
		Dim.Box(0.0f, 0.0f, static_cast<float>(Size.X), static_cast<float>(Size.Y), FLinearColor(0.0f, 0.0f, 0.02f, 0.55f));
		const FPainter On = HudAbove(Dim, 1);
		const FBox2D Panel = GetPanelBox(Size);
		Plate(On, Panel, 18.0f * U);
		const FString Title = ChaosImpactSettings::IsScreenUpright() ? TEXT("配置を変えるモード（縦画面）") : TEXT("配置を変えるモード（横画面）");
		HudAbove(On, 3).Text(Title, static_cast<float>(Panel.Min.X) + 20.0f * U, static_cast<float>(Panel.Min.Y) + 14.0f * U,
			HudFit(Title, 24.0f * U, static_cast<float>(Panel.GetSize().X) - 40.0f * U), Paper);
		for (int32 Tab = 0; Tab < GetModes().Num(); ++Tab)
		{
			const FBox2D Box = GetTabBox(Tab, Size);
			const bool bOn = Tab == ModeIndex;
			const float X = static_cast<float>(Box.Min.X);
			const float Y = static_cast<float>(Box.Min.Y);
			const float W = static_cast<float>(Box.GetSize().X);
			const float H = static_cast<float>(Box.GetSize().Y);
			RoundedBox(HudAbove(On, 3), X, Y, W, H, 10.0f * U, bOn ? Ice : FLinearColor(0.08f, 0.12f, 0.24f, 1.0f));
			const FString Name = ChaosImpactSettings::GetTouchModeName(GetModes()[Tab]);
			const float TextSize = HudFit(Name, 21.0f * U, W - 12.0f * U);
			HudAbove(On, 4).Text(Name, X + W * 0.5f, Y + (H - TextSize * 1.38f) * 0.5f, TextSize, bOn ? Navy : Paper, ETextAlign::Center);
		}
		const FPainter Keys = HudAbove(On, 3);
		Button(Keys, ResetAll, ButtonWord(ResetAll), NightFill);
		Button(Keys, CopyToAll, ButtonWord(CopyToAll), NightFill);
		Button(Keys, ClosePanel, ButtonWord(ClosePanel), SolidFill(FLinearColor(0.14f, 0.16f, 0.22f, 1.0f)));
	}
}

int32 UChaosImpactTouchLayoutEditor::Paint(const FGeometry& Geometry, FSlateWindowElementList& Elements, const int32 Layer) const
{
	using namespace ChaosImpactPaint;
	const FVector2D Size = Geometry.GetLocalSize();
	if (!bOpen || Size.X < 1.0 || Size.Y < 1.0)
	{
		return Layer;
	}
	const float U = UnitOf(Size);
	PaintBackdrop(Geometry, Elements, Layer, Size);
	// The battle HUD itself (made-up state), placed as it is being placed; hidden ones too, under their veil.
	FChaosImpactTouchCustom Shown = GetEdited();
	Shown.Hidden = 0;
	if (SampleHud)
	{
		SampleHud->PaintLayoutPreview(Geometry, Elements, Layer + 10, Shown, bButtons, bLeftHanded, GetMode());
	}
	int32 PieceLayer = Layer + 200;
	for (const int32 Piece : {FC::Hints, FC::Standings, FC::Match, FC::Feed, FC::Respawn, FC::Vitals, FC::Balls, FC::CornerPause, FC::CornerPractice,
		FC::CornerReady, FC::CornerWatch, FC::CornerForm, FC::CornerMap, FC::CornerRules})
	{
		// (Only what this kind of play has.)
		if (!UChaosImpactChargeWidget::IsPieceInMode(Piece, GetMode()))
		{
			continue;
		}
		PaintPieceFrame(Geometry, Elements, PieceLayer, Piece, GetHudBox(Piece, Size), U);
		PieceLayer += 8;
	}
	// The controls, as in play (all of them, やめる and 入れ替え / 合体 included; hidden ones under a veil).
	const FChaosImpactTouchLayout Layout = FChaosImpactTouchGestures::MakeLayout(Size, bLeftHanded, bButtons, &Shown, bOneHand);
	FChaosImpactTouchFrame Still;
	UChaosImpactChargeWidget::PaintTouchPad(FPainter{Geometry, Elements, PieceLayer + 4}, Layout, FVector2D(1.0, 1.0), Still, bButtons, bAimStick,
		true, true, false, true, true, true, 0.0f, FC::IsHud(Selected) ? INDEX_NONE : Selected);
	for (const int32 Control : PadControls)
	{
		if (IsShown(Control) && GetEdited().IsHidden(Control))
		{
			const FVector2D C = Layout.Center(Control);
			const float R = Layout.Radius[Control];
			FPainter{Geometry, Elements, PieceLayer + 20}.Disc(C, R + 4.0f * U, FLinearColor(0.0f, 0.0f, 0.02f, 0.72f));
			const FString Word = TEXT("非表示");
			FPainter{Geometry, Elements, PieceLayer + 21}.Text(Word, static_cast<float>(C.X), static_cast<float>(C.Y) - 10.0f * U,
				HudFit(Word, 18.0f * U, R * 1.8f), WithAlpha(Paper, 0.85f), ETextAlign::Center, TEXT("Black"), 2.0f, Ink);
		}
	}
	PaintPanel(Geometry, Elements, PieceLayer + 40, Size);
	return PieceLayer + 100;
}
