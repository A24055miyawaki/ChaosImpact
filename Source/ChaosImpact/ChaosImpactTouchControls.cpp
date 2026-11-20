#include "ChaosImpactTouchControls.h"
#include "ChaosImpactMenuWidget.h"

namespace
{
	/** Screen way (Y down) to the frame's way (Y up). */
	FVector2D Upward(const FVector2D& ScreenDelta)
	{
		return FVector2D(ScreenDelta.X, -ScreenDelta.Y);
	}
}

FString FChaosImpactTouchCustom::ToText() const
{
	TArray<FString> Parts;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		Parts.Add(FString::Printf(TEXT("%.1f,%.1f,%.2f"), Offset[Index].X, Offset[Index].Y, Size[Index]));
	}
	if (Hidden != 0)
	{
		// (The hidden ones after the places: H and their bits.)
		Parts.Add(FString::Printf(TEXT("H%u"), Hidden));
	}
	return FString::Join(Parts, TEXT(";"));
}

void FChaosImpactTouchCustom::FromText(const FString& Text)
{
	*this = FChaosImpactTouchCustom();
	TArray<FString> Parts;
	Text.ParseIntoArray(Parts, TEXT(";"));
	int32 Index = 0;
	for (const FString& Part : Parts)
	{
		if (Part.StartsWith(TEXT("H")))
		{
			Hidden = static_cast<uint32>(FCString::Strtoui64(*Part + 1, nullptr, 10));
			continue;
		}
		if (Index >= Count)
		{
			continue;
		}
		TArray<FString> Values;
		Part.ParseIntoArray(Values, TEXT(","));
		if (Values.Num() == 3)
		{
			Offset[Index] = FVector2D(FCString::Atof(*Values[0]), FCString::Atof(*Values[1]));
			Size[Index] = FMath::Clamp(FCString::Atof(*Values[2]), MinSize, MaxSize);
		}
		++Index;
	}
}

FChaosImpactTouchLayout FChaosImpactTouchGestures::MakeLayout(const FVector2D& ViewSize, const bool bLeftHanded, const bool bButtons,
	const FChaosImpactTouchCustom* Custom, const bool bOneHand)
{
	FChaosImpactTouchLayout Layout;
	Layout.bOneHand = bOneHand && !bButtons;
	const float S = FMath::Max(0.25f, static_cast<float>(FMath::Min(ViewSize.X, ViewSize.Y)) / 720.0f);
	Layout.Scale = S;
	// The right thumb's arc round the big 投げる button at the bottom right (ダッシュ nearest the thumb's reach, then
	// ジャンプ above, タックル to its left); the stick at the bottom left. 左利き: mirrored.
	const auto Place = [&ViewSize, bLeftHanded](const float FromRight, const float FromBottom)
	{
		const double X = bLeftHanded ? FromRight : ViewSize.X - FromRight;
		return FVector2D(X, ViewSize.Y - FromBottom);
	};
	Layout.ThrowRadius = (bButtons ? 78.0f : 0.0f) * S;
	Layout.SmallRadius = 46.0f * S;
	Layout.Throw = Place(150.0f * S, 150.0f * S);
	Layout.Dash = Place(305.0f * S, 92.0f * S);
	Layout.Jump = bButtons ? Place(118.0f * S, 318.0f * S) : Place(92.0f * S, 92.0f * S);
	Layout.Tackle = Place(278.0f * S, 250.0f * S);
	Layout.Swap = bButtons ? Place(452.0f * S, 96.0f * S) : Place(240.0f * S, 92.0f * S);
	Layout.Fuse = bButtons ? Place(430.0f * S, 222.0f * S) : Place(218.0f * S, 214.0f * S);
	// The 向き stick left of the buttons, toward the middle; やめる up and in from 投げる (the throw's thumb slides up to it).
	// (Both clear of the ball holder above the buttons.)
	Layout.Aim = bButtons ? Place(600.0f * S, 200.0f * S) : Place(420.0f * S, 170.0f * S);
	Layout.Cancel = bButtons ? Place(400.0f * S, 370.0f * S) : Place(380.0f * S, 340.0f * S);
	Layout.StickRadius = StickRadius * S;
	Layout.StickHome = bLeftHanded ? FVector2D(ViewSize.X - 190.0f * S, ViewSize.Y - 170.0f * S) : FVector2D(190.0f * S, ViewSize.Y - 170.0f * S);
	Layout.Radius[FChaosImpactTouchCustom::Stick] = Layout.StickRadius;
	Layout.Radius[FChaosImpactTouchCustom::Throw] = Layout.ThrowRadius;
	for (const int32 Small : {FChaosImpactTouchCustom::Dash, FChaosImpactTouchCustom::Jump, FChaosImpactTouchCustom::Tackle,
		FChaosImpactTouchCustom::Swap, FChaosImpactTouchCustom::Fuse, FChaosImpactTouchCustom::Cancel})
	{
		Layout.Radius[Small] = Layout.SmallRadius;
	}
	Layout.Radius[FChaosImpactTouchCustom::Aim] = 58.0f * S;
	if (Layout.bOneHand)
	{
		// 片手モード: a row along the bottom edge, ジャンプ nearest the thumb's side (the right; 左利き: the left), then
		// タックル, 入れ替え, 合体; the stick's resting place in the middle above them (the thumb may go down anywhere).
		const float Low = 64.0f * S;
		const float W = static_cast<float>(ViewSize.X);
		Layout.SmallRadius = 40.0f * S;
		Layout.Jump = Place(W * 0.13f, Low);
		Layout.Tackle = Place(W * 0.37f, Low);
		Layout.Swap = Place(W * 0.63f, Low);
		Layout.Fuse = Place(W * 0.87f, Low);
		Layout.StickHome = FVector2D(ViewSize.X * 0.5, ViewSize.Y - 290.0f * S);
		for (const int32 Small : {FChaosImpactTouchCustom::Jump, FChaosImpactTouchCustom::Tackle, FChaosImpactTouchCustom::Swap,
			FChaosImpactTouchCustom::Fuse})
		{
			Layout.Radius[Small] = Layout.SmallRadius;
		}
		// (No 投げる, ダッシュ, 向き or やめる.)
		Layout.Radius[FChaosImpactTouchCustom::Throw] = 0.0f;
		Layout.Radius[FChaosImpactTouchCustom::Dash] = 0.0f;
		Layout.Radius[FChaosImpactTouchCustom::Aim] = 0.0f;
		Layout.Radius[FChaosImpactTouchCustom::Cancel] = 0.0f;
	}
	if (Custom)
	{
		// The player's own placing: shifted (mirrored with the halves for 左利き), and sized; kept on the screen.
		TPair<int32, FVector2D*> Places[] = {{FChaosImpactTouchCustom::Stick, &Layout.StickHome}, {FChaosImpactTouchCustom::Throw, &Layout.Throw},
			{FChaosImpactTouchCustom::Dash, &Layout.Dash}, {FChaosImpactTouchCustom::Jump, &Layout.Jump}, {FChaosImpactTouchCustom::Tackle, &Layout.Tackle},
			{FChaosImpactTouchCustom::Swap, &Layout.Swap}, {FChaosImpactTouchCustom::Fuse, &Layout.Fuse}, {FChaosImpactTouchCustom::Aim, &Layout.Aim},
			{FChaosImpactTouchCustom::Cancel, &Layout.Cancel}};
		for (const TPair<int32, FVector2D*>& Each : Places)
		{
			const int32 Index = Each.Key;
			FVector2D* const Spot = Each.Value;
			FVector2D Shift = Custom->Offset[Index] * S;
			Shift.X *= bLeftHanded ? -1.0 : 1.0;
			Layout.Radius[Index] *= FMath::Clamp(Custom->Size[Index], FChaosImpactTouchCustom::MinSize, FChaosImpactTouchCustom::MaxSize);
			const double Edge = Layout.Radius[Index];
			*Spot = FVector2D(FMath::Clamp(Spot->X + Shift.X, Edge, ViewSize.X - Edge),
				FMath::Clamp(Spot->Y + Shift.Y, Edge, ViewSize.Y - Edge));
		}
		Layout.StickRadius = Layout.Radius[FChaosImpactTouchCustom::Stick];
		Layout.ThrowRadius = Layout.Radius[FChaosImpactTouchCustom::Throw];
		Layout.SmallRadius = Layout.Radius[FChaosImpactTouchCustom::Jump];
		// Hidden: no size (not drawn, not touched).
		for (int32 Control = 0; Control < FChaosImpactTouchCustom::Count; ++Control)
		{
			if (Custom->IsHidden(Control) && FChaosImpactTouchCustom::CanHide(Control))
			{
				Layout.Radius[Control] = 0.0f;
			}
		}
	}
	return Layout;
}

float FChaosImpactTouchGestures::HudCornerY(const float Height, const bool bRightSide, const bool bButtons, const bool bLeftHanded)
{
	// With ボタン方式 above the stick and above the buttons; with ジェスチャー方式 a little up from the bottom.
	if (!bButtons)
	{
		return Height * 0.8f;
	}
	return Height * (bRightSide != bLeftHanded ? 0.53f : 0.62f);
}

float FChaosImpactTouchGestures::HudScale(const FVector2D& Size, const float HudSizeFactor)
{
	// (A phone held upright: sized for a 900 x 1600 screen, not shrunk to fit 1600 across its narrow side.)
	const bool bUpright = Size.Y > Size.X * 1.15 && UChaosImpactMenuWidget::IsPhoneBuild();
	const double Across = bUpright ? 900.0 : 1600.0;
	const double Down = bUpright ? 1600.0 : 900.0;
	return FMath::Clamp(static_cast<float>(FMath::Min(Size.X / Across, Size.Y / Down)), 0.5f, 1.4f) * HudSizeFactor;
}

FBox2D FChaosImpactTouchGestures::HudBaseBox(const int32 Control, const FVector2D& Size, const float HudS, const bool bButtons,
	const bool bLeftHanded, FVector2D& OutPivot)
{
	const double W = Size.X;
	const double H = Size.Y;
	const double S = HudS;
	switch (Control)
	{
	case FChaosImpactTouchCustom::Vitals:
	{
		// (The HP from its bottom-left corner, the holder from its bottom-right.)
		const double Bottom = HudCornerY(static_cast<float>(H), false, bButtons, bLeftHanded);
		OutPivot = FVector2D(0.0, Bottom);
		return FBox2D(FVector2D(0.0, Bottom - 185.0 * S), FVector2D(400.0 * S, Bottom));
	}
	case FChaosImpactTouchCustom::Balls:
	{
		const double Bottom = HudCornerY(static_cast<float>(H), true, bButtons, bLeftHanded);
		OutPivot = FVector2D(W, Bottom);
		return FBox2D(FVector2D(W - 300.0 * S, Bottom - 200.0 * S), FVector2D(W, Bottom));
	}
	case FChaosImpactTouchCustom::Match:
		OutPivot = FVector2D(W * 0.5, 0.0);
		return FBox2D(FVector2D(W * 0.5 - 300.0 * S, 0.0), FVector2D(W * 0.5 + 300.0 * S, 110.0 * S));
	case FChaosImpactTouchCustom::Standings:
		OutPivot = FVector2D::ZeroVector;
		return FBox2D(FVector2D(12.0 * S, 12.0 * S), FVector2D(330.0 * S, 200.0 * S));
	case FChaosImpactTouchCustom::Feed:
	{
		const FVector2D Shift = HudDefaultShift(Control, HudS);
		OutPivot = FVector2D(W, 0.0) + Shift;
		return FBox2D(FVector2D(W - 520.0 * S, 26.0 * S) + Shift, FVector2D(W - 20.0 * S, 280.0 * S) + Shift);
	}
	case FChaosImpactTouchCustom::Menu:
		OutPivot = FVector2D(W, 0.0);
		return FBox2D(FVector2D(W - 468.0 * S, 10.0 * S), FVector2D(W - 12.0 * S, 230.0 * S));
	case FChaosImpactTouchCustom::Respawn:
		OutPivot = FVector2D(W * 0.5, H);
		return FBox2D(FVector2D(W * 0.5 - 231.0 * S, H - 278.0 * S), FVector2D(W * 0.5 + 231.0 * S, H - 150.0 * S));
	default:
		// The hints down both sides, round the middle.
		OutPivot = FVector2D(W * 0.5, H * 0.5);
		return FBox2D(FVector2D(20.0 * S, H * 0.36), FVector2D(W - 20.0 * S, H * 0.6));
	}
}

FVector2D FChaosImpactTouchGestures::HudDefaultShift(const int32 Control, const float HudS)
{
	(void)Control;
	(void)HudS;
	return FVector2D::ZeroVector;
}

FBox2D FChaosImpactTouchGestures::HudBox(const int32 Control, const FVector2D& Size, const float HudS, const bool bButtons, const bool bLeftHanded,
	const FChaosImpactTouchCustom& Custom)
{
	const double TouchScale = FMath::Max(0.25, FMath::Min(Size.X, Size.Y) / 720.0);
	const FVector2D Shift = Custom.Offset[Control] * TouchScale;
	const double Scale = FMath::Clamp(Custom.Size[Control], FChaosImpactTouchCustom::MinSize, FChaosImpactTouchCustom::MaxSize);
	FVector2D Pivot;
	const FBox2D Base = HudBaseBox(Control, Size, HudS, bButtons, bLeftHanded, Pivot);
	return FBox2D(Pivot + (Base.Min - Pivot) * Scale + Shift, Pivot + (Base.Max - Pivot) * Scale + Shift);
}

const TCHAR* FChaosImpactTouchCustom::GetName(const int32 Control)
{
	static const TCHAR* const Names[] = {TEXT("移動スティック"), TEXT("投げる"), TEXT("ダッシュ"), TEXT("ジャンプ"), TEXT("タックル"),
		TEXT("入れ替え"), TEXT("合体"), TEXT("HP"), TEXT("ボール枠"), TEXT("向きスティック"), TEXT("やめる"), TEXT("時間・スコア"),
		TEXT("順位"), TEXT("KO・HIT表示"), TEXT("ポーズなどのボタン"), TEXT("復活・ボール化の表示"), TEXT("操作ヒント"), TEXT("ポーズ"),
		TEXT("練習設定"), TEXT("準備OK"), TEXT("観戦"), TEXT("ボール化"), TEXT("全体マップ"), TEXT("ルール")};
	static_assert(UE_ARRAY_COUNT(Names) == Count, "a name for each");
	return Control >= 0 && Control < Count ? Names[Control] : TEXT("");
}

void FChaosImpactTouchGestures::Reset()
{
	for (FFinger& Finger : Fingers)
	{
		Finger = FFinger();
	}
	BodyFinger = INDEX_NONE;
	BallFinger = INDEX_NONE;
}

bool FChaosImpactTouchGestures::IsAnyFingerDown() const
{
	for (const FFinger& Finger : Fingers)
	{
		if (Finger.bDown)
		{
			return true;
		}
	}
	return false;
}

void FChaosImpactTouchGestures::Update(const bool (&bDown)[MaxFingers], const FVector2D (&At)[MaxFingers], const double Now,
	const FVector2D& ViewSize, TFunctionRef<int32(const FVector2D&)> HudHit, FChaosImpactTouchFrame& Out)
{
	Out = FChaosImpactTouchFrame();
	const FChaosImpactTouchLayout Layout = MakeLayout(ViewSize, bLeftHanded, bButtons, &Custom, bOneHand);
	const bool bOneThumb = Layout.bOneHand;
	const float S = Layout.Scale;
	const float StickR = Layout.StickRadius;
	const auto OnButton = [](const FVector2D& Point, const FVector2D& Center, const float Radius)
	{
		// (A finger is wide: a little room round each.)
		return Radius > 0.0f && FVector2D::Distance(Point, Center) <= Radius * 1.3f;
	};
	for (int32 Index = 0; Index < MaxFingers; ++Index)
	{
		FFinger& Finger = Fingers[Index];
		const FVector2D Here = At[Index];
		if (bDown[Index] && !Finger.bDown)
		{
			// Down: whose it is, by where it went down.
			Finger = FFinger();
			Finger.bDown = true;
			Finger.Start = Here;
			Finger.Anchor = Here;
			Finger.Last = Here;
			Finger.StartAt = Now;
			Finger.Trail.Add({Now, Here});
			Finger.Hud = HudHit(Here);
			// (片手モード: the thumb anywhere.)
			const bool bBodySide = bOneThumb || (Here.X < ViewSize.X * 0.5) != bLeftHanded;
			if (Finger.Hud != 0)
			{
				Finger.Role = ERole::Hud;
			}
			else if (bCancelShown && OnButton(Here, Layout.Cancel, Layout.Radius[FChaosImpactTouchCustom::Cancel]))
			{
				Finger.Role = ERole::CancelButton;
				Out.bCancel = true;
			}
			else if (bAimStickShown && OnButton(Here, Layout.Aim, Layout.Radius[FChaosImpactTouchCustom::Aim]))
			{
				Finger.Role = ERole::AimStick;
			}
			else if (OnButton(Here, Layout.Throw, Layout.ThrowRadius) && BallFinger == INDEX_NONE)
			{
				Finger.Role = ERole::Ball;
				BallFinger = Index;
			}
			else if (bSwapShown && OnButton(Here, Layout.Swap, Layout.Radius[FChaosImpactTouchCustom::Swap]))
			{
				Finger.Role = ERole::SwapButton;
				Out.bSwap = true;
			}
			else if (bFuseShown && OnButton(Here, Layout.Fuse, Layout.Radius[FChaosImpactTouchCustom::Fuse]))
			{
				Finger.Role = ERole::FuseButton;
				Out.bFuse = true;
			}
			else if (OnButton(Here, Layout.Jump, Layout.Radius[FChaosImpactTouchCustom::Jump]))
			{
				Finger.Role = ERole::JumpButton;
				Out.bJump = true;
			}
			else if (bButtons && OnButton(Here, Layout.Dash, Layout.Radius[FChaosImpactTouchCustom::Dash]))
			{
				Finger.Role = ERole::DashButton;
				if (Now - DashedAt > DashAgainAfter)
				{
					// The way the stick points (zero: the way it faces).
					Out.bDash = true;
					Out.DashWay = LastMove.GetSafeNormal();
					DashedAt = Now;
				}
			}
			else if ((bButtons || bOneThumb) && OnButton(Here, Layout.Tackle, Layout.Radius[FChaosImpactTouchCustom::Tackle]))
			{
				Finger.Role = ERole::TackleButton;
				Out.bTackle = true;
				Out.TackleWay = LastMove.GetSafeNormal();
			}
			else if (bBodySide && BodyFinger == INDEX_NONE)
			{
				Finger.Role = ERole::Body;
				BodyFinger = Index;
			}
			else if (!bButtons && !bBodySide && BallFinger == INDEX_NONE)
			{
				// ジェスチャー方式: anywhere on the ball's half.
				Finger.Role = ERole::Ball;
				BallFinger = Index;
			}
		}
		if (bDown[Index] && Finger.bDown)
		{
			Finger.Last = Here;
			Finger.Farthest = FMath::Max(Finger.Farthest, static_cast<float>(FVector2D::Distance(Here, Finger.Start)));
			if (Finger.Trail.Last().Key < Now)
			{
				Finger.Trail.Add({Now, Here});
			}
			while (Finger.Trail.Num() > 2 && Now - Finger.Trail[1].Key > FlickWindow)
			{
				Finger.Trail.RemoveAt(0, EAllowShrinking::No);
			}
			switch (Finger.Role)
			{
			case ERole::Body:
			{
				if (bOneThumb)
				{
					// 片手モード: held still a moment, the ball charges and the thumb aims it (a drag from where it went
					// down); moved first, it walks.
					const FVector2D Drag = Here - Finger.Start;
					if (Finger.Phase == EBallPhase::Pending && Finger.Farthest < TapSlop * 2.0f * S && Now - Finger.StartAt > OneHandHoldSeconds)
					{
						Out.bChargeStart = true;
						Finger.Phase = EBallPhase::Charging;
					}
					else if (Finger.Phase == EBallPhase::Pending && Finger.Farthest >= TapSlop * 2.0f * S)
					{
						Finger.Phase = EBallPhase::Done;
					}
					if (Finger.Phase == EBallPhase::Charging)
					{
						Out.bCharging = true;
						Out.ButtonsHeld |= ButtonThrow;
						Out.ThrowThumb = Here;
						Out.bThumbDown = true;
						if (Drag.Size() > AimDrag * S)
						{
							Out.bAimByDrag = true;
							Out.AimWay = Upward(Drag).GetSafeNormal();
						}
						break;
					}
				}
				// A stick wherever the thumb went down, following it when it goes past its edge.
				FVector2D Delta = Here - Finger.Anchor;
				const float Length = static_cast<float>(Delta.Size());
				if (Length > StickR)
				{
					Finger.Anchor += Delta.GetSafeNormal() * (Length - StickR);
					Delta = Here - Finger.Anchor;
				}
				if (Delta.Size() > StickDeadZone * S)
				{
					Out.Move = Upward(Delta / StickR).ClampAxes(-1.0, 1.0);
					LastMove = Out.Move;
				}
				Out.bStickShown = true;
				Out.StickAnchor = Finger.Anchor;
				Out.StickThumb = Here;
				Out.bThumbDown = bOneThumb;
				break;
			}
			case ERole::Ball:
			{
				const FVector2D Drag = Here - Finger.Start;
				Out.ButtonsHeld |= ButtonThrow;
				Out.ThrowThumb = Here;
				if (Finger.Phase == EBallPhase::Pending)
				{
					if (!bButtons && Drag.Size() > TackleTravel * S * FlickScale && Now - Finger.StartAt <= TackleSeconds)
					{
						// ジェスチャー方式: a long fast swipe before it charges: a tackle that way.
						Out.bTackle = true;
						Out.TackleWay = Upward(Drag).GetSafeNormal();
						Finger.Phase = EBallPhase::Done;
					}
					else if (Now - Finger.StartAt > HoldSeconds || (bButtons && Drag.Size() > AimDrag * S))
					{
						Out.bChargeStart = true;
						Finger.Phase = EBallPhase::Charging;
					}
				}
				if (Finger.Phase == EBallPhase::Charging)
				{
					Out.bCharging = true;
					if (Drag.Size() > AimDrag * S)
					{
						Out.bAimByDrag = true;
						Out.AimWay = Upward(Drag).GetSafeNormal();
					}
					Out.bOverCancel = bCancelShown && OnButton(Here, Layout.Cancel, Layout.Radius[FChaosImpactTouchCustom::Cancel]);
				}
				break;
			}
			case ERole::AimStick:
			{
				// Pushed from its middle: the way to face (and to throw, while it is held).
				const FVector2D Delta = Here - Layout.Aim;
				const float Radius = Layout.Radius[FChaosImpactTouchCustom::Aim];
				Out.bAimStickHeld = true;
				Out.ButtonsHeld |= ButtonAim;
				Out.AimStickThumb = Layout.Aim + (Delta.Size() > Radius ? Delta.GetSafeNormal() * Radius : Delta);
				if (Delta.Size() > StickDeadZone * S)
				{
					Out.bAimStick = true;
					Out.AimStickWay = Upward(Delta).GetSafeNormal();
				}
				break;
			}
			case ERole::CancelButton:
				Out.ButtonsHeld |= ButtonCancel;
				break;
			case ERole::DashButton:
				Out.ButtonsHeld |= ButtonDash;
				break;
			case ERole::JumpButton:
				Out.ButtonsHeld |= ButtonJump;
				break;
			case ERole::TackleButton:
				Out.ButtonsHeld |= ButtonTackle;
				break;
			case ERole::SwapButton:
				Out.ButtonsHeld |= ButtonSwap;
				break;
			case ERole::FuseButton:
				Out.ButtonsHeld |= ButtonFuse;
				break;
			case ERole::Hud:
				if (!Finger.bHudHeld && Now - Finger.StartAt > HudHoldSeconds && Finger.Farthest < TapSlop * 2.0f * S)
				{
					Out.HudHold = Finger.Hud;
					Finger.bHudHeld = true;
				}
				break;
			default:
				break;
			}
		}
		if (!bDown[Index] && Finger.bDown)
		{
			// Let go.
			const double Held = Now - Finger.StartAt;
			switch (Finger.Role)
			{
			case ERole::Body:
				if (bOneThumb && Finger.Phase == EBallPhase::Charging)
				{
					// 片手モード: let go after charging: thrown, along the drag (else at whoever is near).
					Out.bRelease = true;
					const FVector2D Drag = Finger.Last - Finger.Start;
					if (Drag.Size() > AimDrag * S)
					{
						Out.bAimByDrag = true;
						Out.AimWay = Upward(Drag).GetSafeNormal();
					}
				}
				else if (bOneThumb && Held < TapSeconds + 0.1 && Finger.Farthest < TapSlop * 2.0f * S)
				{
					// 片手モード: a tap: a quick throw at whoever is near.
					Out.bQuickThrow = true;
				}
				else if (!bButtons && Now - DashedAt > DashAgainAfter && Finger.Trail.Num() >= 2)
				{
					// ジェスチャー方式: a flick as the thumb lifts: a dash the way it went.
					const TPair<double, FVector2D>& Oldest = Finger.Trail[0];
					const double Span = Now - Oldest.Key;
					const FVector2D Velocity = Span > 0.02 ? (Finger.Last - Oldest.Value) / Span : FVector2D::ZeroVector;
					if (Velocity.Size() > FlickSpeed * S * FlickScale)
					{
						Out.bDash = true;
						Out.DashWay = Upward(Velocity).GetSafeNormal();
						DashedAt = Now;
					}
				}
				BodyFinger = INDEX_NONE;
				LastMove = FVector2D::ZeroVector;
				break;
			case ERole::Ball:
				if (Finger.Phase == EBallPhase::Pending && Held < TapSeconds + 0.1)
				{
					Out.bQuickThrow = true;
				}
				else if (Finger.Phase == EBallPhase::Charging && bCancelShown
					&& OnButton(Finger.Last, Layout.Cancel, Layout.Radius[FChaosImpactTouchCustom::Cancel]))
				{
					// Let go on やめる: the charge put away.
					Out.bCancel = true;
				}
				else if (Finger.Phase == EBallPhase::Charging)
				{
					Out.bRelease = true;
					const FVector2D Drag = Finger.Last - Finger.Start;
					if (Drag.Size() > AimDrag * S)
					{
						Out.bAimByDrag = true;
						Out.AimWay = Upward(Drag).GetSafeNormal();
					}
				}
				BallFinger = INDEX_NONE;
				break;
			case ERole::Hud:
				if (!Finger.bHudHeld && Held < 0.4 && Finger.Farthest < TapSlop * 2.0f * S)
				{
					Out.HudTap = Finger.Hud;
				}
				break;
			default:
				break;
			}
			Finger = FFinger();
		}
	}
}
