#pragma once

#include "CoreMinimal.h"

/**
 * スマホ版のタッチ操作 (2026-11-17, docs: Docs/モバイル対応企画.md §2). Two styles, chosen in the settings:
 * - ボタン方式 (standard): the left thumb a stick wherever it goes down; buttons at the bottom right: 投げる (tap: a quick
 *   throw at whoever is near; hold: charge, drag from it to aim, let go to throw), ダッシュ, ジャンプ, タックル.
 * - ジェスチャー方式: no buttons on the field; the left half moves the body (drag: move, a flick as the thumb lifts: dash),
 *   the right half uses the ball (tap: a quick throw, hold: charge, drag: aim, let go: throw, a long fast swipe: tackle);
 *   a small ジャンプ button.
 * - 片手モード (2026-11-18): one thumb anywhere: drag to move (a flick as it lifts: dash), a tap: a quick throw at whoever
 *   is near; held still a moment: the ball charges (standing), the thumb then drags to aim and lets go to throw;
 *   small ジャンプ / タックル / 入れ替え / 合体 buttons along the bottom edge.
 * Touches that start on the HUD (the ball holders, 合体, the corner buttons) are the HUD's. This reads the fingers only;
 * the character does what it says (AChaosImpactCharacter::UpdateTouchControls).
 */
struct FChaosImpactTouchFrame
{
	/** The left thumb as a stick: X right, Y up the screen, each -1..1. */
	FVector2D Move = FVector2D::ZeroVector;
	bool bJump = false;
	/** A dash / tackle: the way (on the screen, X right, Y up; zero: the way the stick points, else the way it faces). */
	bool bDash = false;
	FVector2D DashWay = FVector2D::ZeroVector;
	bool bTackle = false;
	FVector2D TackleWay = FVector2D::ZeroVector;
	/** The throw: held long enough to charge; still charging; let go (throw); a quick tap (throw at once). */
	bool bChargeStart = false;
	bool bCharging = false;
	bool bRelease = false;
	bool bQuickThrow = false;
	/** Aimed by dragging (else: at whoever is near). */
	bool bAimByDrag = false;
	FVector2D AimWay = FVector2D::ZeroVector;
	/** 入れ替え / 合体 (or 解除) pressed. */
	bool bSwap = false;
	bool bFuse = false;
	/** やめる: the charge put away without throwing (tapped, or the throw's thumb let go on it). */
	bool bCancel = false;
	/** The 向き stick: held, and pushed out of its middle that way (X right, Y up); where the thumb is on it (drawn). */
	bool bAimStickHeld = false;
	bool bAimStick = false;
	FVector2D AimStickWay = FVector2D::ZeroVector;
	FVector2D AimStickThumb = FVector2D::ZeroVector;
	/** The throw's thumb is over やめる now (drawn lit: letting go there puts the charge away). */
	bool bOverCancel = false;
	/** A tap on the HUD, and a long press on it (once): what it was on (the charge widget's touch hit, 0: none). */
	int32 HudTap = 0;
	int32 HudHold = 0;
	/** Where the left thumb's stick is drawn (anchor and thumb, screen pixels), while it is down. */
	bool bStickShown = false;
	FVector2D StickAnchor = FVector2D::ZeroVector;
	FVector2D StickThumb = FVector2D::ZeroVector;
	/** 片手モード: the thumb is on the screen (moving: not on a button or the HUD). */
	bool bThumbDown = false;
	/** The buttons held now (EButton bits), to draw them pressed; the throw's finger while it drags (to draw the aim). */
	uint8 ButtonsHeld = 0;
	FVector2D ThrowThumb = FVector2D::ZeroVector;
};

/**
 * The player's own placing of the controls and the HUD (設定 → ボタン・画面の配置), one for each kind of play: each one's
 * shift (in 720-pixel units of the screen's short side) and size.
 */
struct FChaosImpactTouchCustom
{
	/**
	 * (Kept in the settings in this order: new ones only at the end.) Aim: the 向き stick; Cancel: やめる. The HUD: Vitals
	 * (HP), Balls (the ball holder), Match (the clock and scoreboards at the top middle), Standings (the ranking rows at
	 * the top left), Feed (KO / HIT / 被弾 at the top right), Menu (ポーズ and the other corner buttons), Respawn (the cards
	 * at the bottom middle), Hints (the hints down the sides).
	 */
	enum EControl : int32 { Stick, Throw, Dash, Jump, Tackle, Swap, Fuse, Vitals, Balls, Aim, Cancel, Match, Standings, Feed, Menu, Respawn,
		Hints, CornerPause, CornerPractice, CornerReady, CornerWatch, CornerForm, CornerMap, CornerRules, Count };
	static bool IsHud(const int32 Control) { return Control == Vitals || Control == Balls || (Control >= Match && Control < Count && Control != Aim && Control != Cancel); }
	/** The corner buttons each on their own (Menu: all of them at once, kept for older placings): by their HUD number. */
	static bool IsCorner(const int32 Control) { return Control >= CornerPause && Control <= CornerRules; }
	static int32 CornerOf(const int32 Hit)
	{
		switch (Hit)
		{
		case 1: return CornerPause;
		case 2: return CornerPractice;
		case 4: return CornerReady;
		case 5: return CornerWatch;
		case 6: return CornerForm;
		case 7: return CornerMap;
		case 8: return CornerRules;
		default: return INDEX_NONE;
		}
	}
	/** Its name in the editor. */
	static const TCHAR* GetName(int32 Control);
	FVector2D Offset[Count];
	float Size[Count];
	/** Hidden (not drawn, not touched): a bit for each (CanHide). */
	uint32 Hidden = 0;
	bool IsHidden(const int32 Control) const { return Control >= 0 && Control < Count && ((Hidden >> Control) & 1u) != 0; }
	void SetHidden(const int32 Control, const bool bHide) { Hidden = bHide ? (Hidden | (1u << Control)) : (Hidden & ~(1u << Control)); }
	/** All but the move stick, 投げる, the HP, the ball holder and the corner buttons (ポーズ is among them) may be hidden. */
	static bool CanHide(const int32 Control)
	{
		// (ポーズ, 練習設定, 準備OK and 観戦 are the only ways to what they do.)
		return Control != Stick && Control != Throw && Control != Vitals && Control != Balls && Control != Menu && Control != CornerPause
			&& Control != CornerPractice && Control != CornerReady && Control != CornerWatch && Control >= 0 && Control < Count;
	}
	FChaosImpactTouchCustom()
	{
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Offset[Index] = FVector2D::ZeroVector;
			Size[Index] = 1.0f;
		}
	}
	bool operator==(const FChaosImpactTouchCustom& Other) const
	{
		if (Hidden != Other.Hidden)
		{
			return false;
		}
		for (int32 Index = 0; Index < Count; ++Index)
		{
			if (!Offset[Index].Equals(Other.Offset[Index], 0.01) || !FMath::IsNearlyEqual(Size[Index], Other.Size[Index], 0.001f))
			{
				return false;
			}
		}
		return true;
	}
	bool operator!=(const FChaosImpactTouchCustom& Other) const { return !(*this == Other); }
	/** Kept as text in the settings (x,y,size for each, in order). */
	FString ToText() const;
	void FromText(const FString& Text);
	static constexpr float MinSize = 0.6f;
	static constexpr float MaxSize = 1.8f;
};

/** Where the buttons are on a screen (pixels): centres and radii. */
struct FChaosImpactTouchLayout
{
	FVector2D Throw = FVector2D::ZeroVector;
	float ThrowRadius = 0.0f;
	FVector2D Dash = FVector2D::ZeroVector;
	FVector2D Jump = FVector2D::ZeroVector;
	FVector2D Tackle = FVector2D::ZeroVector;
	float SmallRadius = 0.0f;
	/** 入れ替え and 合体 (shown only when they can be used). */
	FVector2D Swap = FVector2D::ZeroVector;
	FVector2D Fuse = FVector2D::ZeroVector;
	/** Each control's own radius (the player may have made one bigger). */
	float Radius[FChaosImpactTouchCustom::Count] = {};
	FVector2D Center(const int32 Control) const
	{
		switch (Control)
		{
		case FChaosImpactTouchCustom::Stick: return StickHome;
		case FChaosImpactTouchCustom::Throw: return Throw;
		case FChaosImpactTouchCustom::Dash: return Dash;
		case FChaosImpactTouchCustom::Jump: return Jump;
		case FChaosImpactTouchCustom::Tackle: return Tackle;
		case FChaosImpactTouchCustom::Swap: return Swap;
		case FChaosImpactTouchCustom::Aim: return Aim;
		case FChaosImpactTouchCustom::Cancel: return Cancel;
		default: return Fuse;
		}
	}
	/** The 向き stick (fixed where it is: pushed from its middle), and やめる (shown while a throw is charging). */
	FVector2D Aim = FVector2D::ZeroVector;
	FVector2D Cancel = FVector2D::ZeroVector;
	/** Where the stick rests when no thumb is on it (drawn faintly, to show where to put it). */
	FVector2D StickHome = FVector2D::ZeroVector;
	float StickRadius = 0.0f;
	/** The screen's scale (its short side against 720 pixels). */
	float Scale = 1.0f;
	/** 片手モード: the small buttons in a row along the bottom edge (タックル among them), the thumb anywhere. */
	bool bOneHand = false;
};

class CHAOSIMPACT_API FChaosImpactTouchGestures
{
public:
	static constexpr int32 MaxFingers = 10;
	enum EButton : uint8 { ButtonThrow = 1, ButtonDash = 2, ButtonJump = 4, ButtonTackle = 8, ButtonSwap = 16, ButtonFuse = 32, ButtonCancel = 64,
		ButtonAim = 128 };

	/**
	 * The buttons' places on a screen of ViewSize (bButtons: all of them; else only ジャンプ, 入れ替え and 合体). 左利き:
	 * mirrored. Custom: the player's own placing of them.
	 */
	static FChaosImpactTouchLayout MakeLayout(const FVector2D& ViewSize, bool bLeftHanded, bool bButtons,
		const FChaosImpactTouchCustom* Custom = nullptr, bool bOneHand = false);

	/**
	 * The HUD's corners on a touch screen (HP bottom left, ball holder bottom right) sit higher, out from under the thumbs:
	 * the height of the corner's bottom on a screen this high.
	 */
	static float HudCornerY(float Height, bool bRightSide, bool bButtons, bool bLeftHanded);
	/** The in-play HUD's scale on a screen of this size (HudSizeFactor: the 画面 setting's 3 sizes). */
	static float HudScale(const FVector2D& Size, float HudSizeFactor);
	/**
	 * The box the HP (Vitals) or the ball holder (Balls) takes on a screen of Size, at the HUD's scale HudS, shifted and
	 * sized as Custom says (its shifts in 720-pixel units of the screen's short side).
	 */
	static FBox2D HudBox(int32 Control, const FVector2D& Size, float HudS, bool bButtons, bool bLeftHanded, const FChaosImpactTouchCustom& Custom);
	/** A HUD piece as it is by default (at the HUD's scale), and the point it grows and shrinks round (OutPivot). */
	static FBox2D HudBaseBox(int32 Control, const FVector2D& Size, float HudS, bool bButtons, bool bLeftHanded, FVector2D& OutPivot);
	/**
	 * Where a HUD piece stands by touch before the player moves it, against where it is drawn otherwise (the KO / HIT
	 * feed left of the corner buttons, not under them).
	 */
	static FVector2D HudDefaultShift(int32 Control, float HudS);

	/**
	 * One frame. Down/At: each finger, held or not and where (screen pixels). ViewSize: the screen. HudHit: what HUD is at a
	 * point (0: none; a finger starting there is the HUD's).
	 */
	void Update(const bool (&bDown)[MaxFingers], const FVector2D (&At)[MaxFingers], double Now, const FVector2D& ViewSize,
		TFunctionRef<int32(const FVector2D&)> HudHit, FChaosImpactTouchFrame& Out);
	void Reset();
	bool IsAnyFingerDown() const;

	/** ボタン方式 (else ジェスチャー方式, or 片手モード). */
	bool bButtons = true;
	bool bOneHand = false;
	/** 入れ替え / 合体 can be used now (their buttons show and take taps). */
	bool bSwapShown = false;
	bool bFuseShown = false;
	/** The 向き stick is shown (the setting); やめる is shown (a throw is charging). */
	bool bAimStickShown = true;
	bool bCancelShown = false;
	/** The player's own placing of the controls (for this kind of play). */
	FChaosImpactTouchCustom Custom;
	/** 左利き: the halves (and the buttons) swapped. */
	bool bLeftHanded = false;
	/** How fast a flick must be (and a tackle's swipe how long): 1 ふつう, more かため, less やわらかめ. */
	float FlickScale = 1.0f;

	/** Sizes at a 720-pixel-high screen (scaled by the screen's short side). */
	static constexpr float StickRadius = 70.0f;
	static constexpr float StickDeadZone = 12.0f;
	/** ジェスチャー方式: a dash is a flick as the thumb lifts: this fast (pixels a second) over its last FlickWindow seconds. */
	static constexpr float FlickSpeed = 1700.0f;
	static constexpr double FlickWindow = 0.08;
	static constexpr double DashAgainAfter = 0.35;
	/** A tap: let go this soon, having moved this little. */
	static constexpr double TapSeconds = 0.22;
	static constexpr float TapSlop = 18.0f;
	/** The throw: held this long before it charges. */
	static constexpr double HoldSeconds = 0.16;
	/** ジェスチャー方式: a tackle is a swipe this long, fast (within TackleSeconds), before it charges. */
	static constexpr float TackleTravel = 110.0f;
	static constexpr double TackleSeconds = 0.14;
	/** Dragged this far from where it went down, the charge aims along the drag. */
	static constexpr float AimDrag = 24.0f;
	static constexpr double HudHoldSeconds = 0.45;
	/** 片手モード: the thumb held this still this long charges (moved first, it walks instead). */
	static constexpr double OneHandHoldSeconds = 0.22;

private:
	enum class ERole : uint8 { None, Body, Ball, Hud, DashButton, JumpButton, TackleButton, SwapButton, FuseButton, AimStick, CancelButton };
	enum class EBallPhase : uint8 { Pending, Charging, Done };
	struct FFinger
	{
		bool bDown = false;
		ERole Role = ERole::None;
		FVector2D Start = FVector2D::ZeroVector;
		FVector2D Anchor = FVector2D::ZeroVector;
		FVector2D Last = FVector2D::ZeroVector;
		double StartAt = 0.0;
		EBallPhase Phase = EBallPhase::Pending;
		int32 Hud = 0;
		bool bHudHeld = false;
		float Farthest = 0.0f;
		TArray<TPair<double, FVector2D>, TInlineAllocator<16>> Trail;
	};
	FFinger Fingers[MaxFingers];
	int32 BodyFinger = INDEX_NONE;
	int32 BallFinger = INDEX_NONE;
	double DashedAt = -10.0;
	/** The stick's last way (for the ダッシュ button). */
	FVector2D LastMove = FVector2D::ZeroVector;
};
