// ソロモードの目標と進み具合 (2026-11-19): what to do now (目標) and how far along the stage is, across the top; in a boss
// fight, the boss's health under it. (Each enemy's own health is shown over its head: ChaosImpactCombatHealth.cpp.)
// Drawn as the rest of the HUD is: slanted night-blue plates with navy outlines and a drop, gold and fire accents, 3D
// lettering; every change moves (the goal slides in, the count pops, a cleared room flashes, the boss's bar drains).
#include "ChaosImpactChargeWidget.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactEase.h"
#include "ChaosImpactPaint.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactSoloRoom.h"
#include "ChaosImpactSimaeBoss.h"
#include "ChaosImpactPresidentBoss.h"
#include "Engine/World.h"
#include "EngineUtils.h"

bool UChaosImpactChargeWidget::GetSoloObjective(FSoloObjective& Out) const
{
	Out = FSoloObjective();
	const AChaosImpactPlayerController* PC = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
	UWorld* World = GetWorld();
	if (!PC || !World || !PC->IsSoloMode() || PC->IsSoloWorldMap() || PC->IsSoloTitle() || PC->GetSoloClearedAt() >= 0.0)
	{
		return false;
	}
	TArray<const AChaosImpactSoloRoom*> Rooms;
	for (TActorIterator<AChaosImpactSoloRoom> It(World); It; ++It)
	{
		Rooms.Add(*It);
	}
	if (Rooms.IsEmpty())
	{
		return false;
	}
	Out.Rooms = Rooms.Num();
	for (const AChaosImpactSoloRoom* Room : Rooms)
	{
		const EChaosImpactSoloRoomPhase Phase = Room->GetPhase();
		if (Phase == EChaosImpactSoloRoomPhase::Cleared)
		{
			++Out.Cleared;
			continue;
		}
		if (Phase == EChaosImpactSoloRoomPhase::Minions || Phase == EChaosImpactSoloRoomPhase::Boss)
		{
			Out.Active = Room;
		}
	}
	if (const AChaosImpactSoloRoom* Room = Out.Active)
	{
		const int32 Planned = FMath::Max(Room->GetPlannedMinionCount(), 0);
		const int32 Down = FMath::Clamp(Room->GetDefeatedCount(), 0, Planned);
		const bool bBossFight = Room->GetPhase() == EChaosImpactSoloRoomPhase::Boss;
		const int32 Steps = Planned + (Room->HasBoss() ? 1 : 0);
		Out.RoomProgress = Steps > 0 ? static_cast<float>(bBossFight ? Planned : Down) / Steps : 0.0f;
		Out.Left = bBossFight ? 0 : Planned - Down;
		// The shima-enaga has its own great bar across the top: in its fight this one makes way for it.
		if (bBossFight && (Cast<AChaosImpactSimaeBoss>(Room->GetBoss()) || Cast<AChaosImpactPresidentBoss>(Room->GetBoss())))
		{
			return false;
		}
		Out.Boss = bBossFight ? Room->GetBoss() : nullptr;
		Out.bBossNext = !bBossFight && Room->HasBoss();
		Out.Kind = bBossFight ? 2 : 1;
	}
	return Out.Cleared < Out.Rooms;
}

namespace
{
	using namespace ChaosImpactPaint;

	/** A plate's outline: a bar W x H (in its own slanted space), two corners nicked like the rest of the HUD. */
	TArray<FVector2D> Plate(const float W, const float H, const float Nick = 8.0f)
	{
		return {{Nick, 0.0f}, {W, 0.0f}, {W, H - Nick}, {W - Nick, H}, {0.0f, H}, {0.0f, Nick}};
	}

	/** A slanted bar (a gauge's track or fill, a tag). */
	TArray<FVector2D> Bar(const float X, const float Y, const float W, const float H)
	{
		const float Lean = FMath::Min(H * 0.45f, W * 0.5f);
		return {{X + Lean, Y}, {X + W, Y}, {X + W - Lean, Y + H}, {X, Y + H}};
	}

	/** A small crown (the boss waiting at the end of a room), centred on C. */
	void Crown(const FPainter& P, const FVector2D& C, const float Size, const FHudFill& Fill)
	{
		const float H = Size;
		const float W = Size * 1.25f;
		const TArray<FVector2D> Shape{{C.X - W * 0.5f, C.Y + H * 0.45f}, {C.X - W * 0.5f, C.Y - H * 0.2f}, {C.X - W * 0.22f, C.Y + H * 0.08f},
			{C.X, C.Y - H * 0.5f}, {C.X + W * 0.22f, C.Y + H * 0.08f}, {C.X + W * 0.5f, C.Y - H * 0.2f}, {C.X + W * 0.5f, C.Y + H * 0.45f}};
		HudShard(P, Shape, Fill, 1.5f, FVector2D(1.5, 2.0));
	}
}

void UChaosImpactChargeWidget::PaintSoloObjective(const FGeometry& Geometry, FSlateWindowElementList& Elements, const int32 Layer) const
{
	FSoloObjective Goal;
	const AChaosImpactPlayerController* PC = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
	FSoloHudAnim& Anim = SoloAnim;
	const double Now = FPlatformTime::Seconds();
	if (!PC || !PC->IsGameplayActive() || bSpectatorView || !GetSoloObjective(Goal))
	{
		if (Now - Anim.LastAt > 1.0)
		{
			Anim = FSoloHudAnim();
		}
		return;
	}
	const float Step = Anim.LastAt > 0.0 ? static_cast<float>(FMath::Min(Now - Anim.LastAt, 0.1)) : 0.0f;
	Anim.LastAt = Now;
	// What changed since the last frame starts its own motion.
	if (Anim.ShownAt < 0.0)
	{
		Anim.ShownAt = Now;
		Anim.Cleared = Goal.Cleared;
		Anim.Kind = Goal.Kind;
		Anim.Left = Goal.Left;
	}
	if (Goal.Kind != Anim.Kind)
	{
		Anim.Kind = Goal.Kind;
		Anim.KindAt = Now;
		Anim.Fill = Goal.Kind == 0 ? 0.0f : Anim.Fill;
	}
	if (Goal.Left != Anim.Left)
	{
		Anim.LeftAt = Goal.Left < Anim.Left ? Now : Anim.LeftAt;
		Anim.Left = Goal.Left;
	}
	if (Goal.Cleared > Anim.Cleared)
	{
		Anim.ClearedAt = Now;
		Anim.Fill = 0.0f;
	}
	Anim.Cleared = Goal.Cleared;
	const AChaosImpactCharacter* Boss = Goal.Boss.Get();
	const float BossRatio = Boss && Boss->GetMaxHealth() > 0.0f ? FMath::Clamp(Boss->GetHealth() / Boss->GetMaxHealth(), 0.0f, 1.0f) : 0.0f;
	if (Boss != Anim.Boss.Get())
	{
		Anim.Boss = Boss;
		Anim.BossAt = Now;
		Anim.BossTrail = BossRatio;
		Anim.BossLast = BossRatio;
	}
	if (Boss && BossRatio < Anim.BossLast - KINDA_SMALL_NUMBER)
	{
		Anim.BossHitAt = Now;
	}
	Anim.BossLast = BossRatio;
	// The lost part of the boss's bar drains after a beat; the room's gauge eases up to where it is.
	if (Now - Anim.BossHitAt > 0.45)
	{
		Anim.BossTrail = FMath::FInterpTo(Anim.BossTrail, BossRatio, Step, 6.0f);
	}
	Anim.BossTrail = FMath::Max(Anim.BossTrail, BossRatio);
	Anim.Fill = FMath::FInterpTo(Anim.Fill, Goal.RoomProgress, Step, 9.0f);

	const FVector2f Size = Geometry.GetLocalSize();
	const float S = GetHudScale(Size.X, Size.Y);
	const float Time = static_cast<float>(FMath::Fmod(Now, 3600.0));
	// As wide as fits (an upright phone's screen is narrow).
	const float Width = FMath::Min(540.0f, Size.X / S - 40.0f);
	const float Shear = -0.16f;
	// The whole thing comes down into place as the stage starts.
	const float Intro = ChaosImpactEase::Out(FMath::Clamp(static_cast<float>(Now - Anim.ShownAt) / 0.45f, 0.0f, 1.0f));
	const FGeometry Anchor = MakeAnchor(Geometry, Size.X * 0.5f, (14.0f - 40.0f * (1.0f - Intro)) * S, S);
	const float Alpha = Intro;

	// ---- The goal: a slanted night-blue plate, a gold 目標 tag, the goal sliding in whenever it changes.
	const float PlateH = 58.0f;
	const FGeometry PlateSpace = MakeSkewed(Anchor, -Width * 0.5f, 0.0f, Width, PlateH, Shear);
	const FPainter PlateBack{PlateSpace, Elements, Layer, Alpha};
	const FPainter PlateTop = HudAbove(PlateBack, 3);
	HudShard(PlateBack, Plate(Width, PlateH), NightFill, 2.5f, FVector2D(4.0, 5.0));
	// A fine light along its top edge, and a rule along its foot drawn across as the goal changes (fire for a boss).
	PlateTop.Line(FVector2D(10.0, 2.0), FVector2D(Width - 2.0f, 2.0), WithAlpha(Paper, 0.18f), 1.2f);
	const float Since = static_cast<float>(Now - Anim.KindAt);
	const float Rule = ChaosImpactEase::InOut(FMath::Clamp(Since / 0.45f, 0.0f, 1.0f));
	const FLinearColor Accent = Goal.Kind == 2 ? Fire : Gold;
	PlateTop.Box(0.0f, PlateH - 4.0f, (Width - 8.0f) * Rule, 3.0f, WithAlpha(Accent, 0.95f));
	// The tag.
	const float TagW = 70.0f;
	const FHudFill TagFill = Goal.Kind == 2 ? FireFill : MakeFill(FLinearColor(1.0f, 0.93f, 0.5f), FLinearColor(0.85f, 0.45f, 0.02f));
	HudShard(PlateTop, Bar(12.0f, 12.0f, TagW, PlateH - 26.0f), TagFill, 1.5f, FVector2D(2.0, 3.0));
	HudAbove(PlateTop, 4).Text(TEXT("目標"), 12.0f + TagW * 0.5f, 16.0f, 19.0f, Ink, ETextAlign::Center, TEXT("Black"));
	// The goal's words, in from the right as it changes.
	FString Line;
	switch (Goal.Kind)
	{
	case 1: Line = TEXT("敵をすべて倒せ"); break;
	case 2: Line = TEXT("ボスを倒せ！"); break;
	default: Line = TEXT("先へ進んで 次の部屋へ"); break;
	}
	const float Slide = ChaosImpactEase::Out(FMath::Clamp(Since / 0.32f, 0.0f, 1.0f));
	const float RightW = Goal.Kind == 1 ? 118.0f : 64.0f;
	const float LineX = 12.0f + TagW + 18.0f;
	const float LineSize = HudFit(Line, 25.0f, Width - LineX - RightW - 12.0f, TEXT("BlackItalic"));
	const FPainter Words{PlateSpace, Elements, Layer + 7, Alpha * Slide};
	HudText3D(Words, Line, LineX + 34.0f * (1.0f - Slide), (PlateH - LineSize) * 0.5f - 3.0f, LineSize, Paper, ETextAlign::Left,
		TEXT("BlackItalic"));
	if (Goal.Kind == 1)
	{
		// How many are left: a big number that pops each time one goes down (the last one glinting).
		const float Pop = 1.0f + 0.55f * FMath::Exp(-11.0f * static_cast<float>(Now - Anim.LeftAt));
		const float NumberSize = 36.0f * Pop;
		const FString Number = FString::FromInt(Goal.Left);
		const FLinearColor NumberColor = Goal.Left <= 1 ? FMath::Lerp(Gold, Paper, 0.25f + 0.25f * FMath::Sin(Time * 6.0f)) : Gold;
		HudText3D(Words, Number, Width - 22.0f, (PlateH - NumberSize) * 0.5f - 4.0f, NumberSize, NumberColor, ETextAlign::Right,
			TEXT("BlackItalic"));
		Words.Text(TEXT("あと"), Width - 64.0f, 22.0f, 15.0f, WithAlpha(Paper, 0.8f), ETextAlign::Right, TEXT("Bold"), 1.5f, Ink);
	}
	else if (Goal.Kind == 0)
	{
		// On the way: chevrons running on ahead.
		for (int32 Index = 0; Index < 3; ++Index)
		{
			const float Phase = FMath::Frac(Time * 1.2f - Index * 0.22f);
			const float X = Width - 58.0f + Index * 14.0f;
			const float Glow = 0.25f + 0.75f * FMath::Square(FMath::Sin(Phase * UE_PI));
			HudPoly(Words, {{X, 18.0f}, {X + 9.0f, 18.0f}, {X + 17.0f, 29.0f}, {X + 9.0f, 40.0f}, {X, 40.0f}, {X + 8.0f, 29.0f}},
				SolidFill(WithAlpha(Gold, Glow)), 1);
		}
	}
	else
	{
		// The boss: a crown, beating.
		const float Beat = 1.0f + 0.12f * FMath::Max(0.0f, FMath::Sin(Time * 7.0f));
		Crown(Words, FVector2D(Width - 40.0f, PlateH * 0.5f), 22.0f * Beat, FireFill);
	}

	// ---- The stage's progress under it: a segment a room (cleared ones gold with a tick, the one being fought filling).
	const float StripY = PlateH + 8.0f;
	const float StripH = 16.0f;
	const float LabelW = 84.0f;
	const float StripW = Width - 26.0f - LabelW;
	const FGeometry StripSpace = MakeSkewed(Anchor, -Width * 0.5f + 16.0f, StripY, StripW, StripH, Shear);
	const FPainter StripBack{StripSpace, Elements, Layer, Alpha};
	const FPainter StripTop = HudAbove(StripBack, 3);
	HudShard(StripBack, Bar(0.0f, 0.0f, StripW, StripH), SolidFill(Navy), 2.0f, FVector2D(3.0, 4.0));
	const float Gap = 6.0f;
	const float Segment = (StripW - 8.0f - Gap * (Goal.Rooms - 1)) / FMath::Max(Goal.Rooms, 1);
	for (int32 Index = 0; Index < Goal.Rooms; ++Index)
	{
		const float X = 4.0f + Index * (Segment + Gap);
		const float Y = 3.0f;
		const float H = StripH - 6.0f;
		const bool bDone = Index < Goal.Cleared;
		const bool bHere = Index == Goal.Cleared;
		HudPoly(StripTop, Bar(X, Y, Segment, H), SolidFill(FLinearColor(0.06f, 0.09f, 0.16f, 1.0f)), 1);
		if (bDone)
		{
			HudPoly(StripTop, Bar(X, Y, Segment, H), MakeFill(FLinearColor(1.0f, 0.92f, 0.45f), Gold), 2);
			// Just cleared: a white flash fading off it.
			const float Flash = FMath::Clamp(1.0f - static_cast<float>(Now - Anim.ClearedAt) / 0.6f, 0.0f, 1.0f);
			if (Flash > 0.0f && Index == Goal.Cleared - 1)
			{
				HudPoly(HudAbove(StripTop, 2), Bar(X, Y, Segment, H), SolidFill(WithAlpha(Paper, 0.85f * Flash)), 1);
			}
			const FVector2D Tick(X + Segment * 0.5f, Y + H * 0.5f);
			HudAbove(StripTop, 3).Line(Tick + FVector2D(-5.0, 0.0), Tick + FVector2D(-1.5, 3.5), Ink, 2.2f);
			HudAbove(StripTop, 3).Line(Tick + FVector2D(-1.5, 3.5), Tick + FVector2D(5.5, -3.5), Ink, 2.2f);
		}
		else if (bHere && Goal.Active)
		{
			const float W = FMath::Max(Segment * Anim.Fill, 0.0f);
			if (W > 1.0f)
			{
				HudPoly(StripTop, Bar(X, Y, W, H), IceFill, 2);
				// A light running along what is filled.
				const float Sweep = FMath::Frac(Time * 0.7f);
				const float From = FMath::Max(X + W * Sweep - 12.0f, X);
				FPainter Shine = HudAbove(StripTop, 2);
				Shine.Alpha *= 0.55f * FMath::Sin(Sweep * UE_PI);
				HudPoly(Shine, Bar(From, Y, FMath::Max(FMath::Min(24.0f, X + W - From), 1.0f), H), SolidFill(Paper), 1);
			}
			if (Goal.bBossNext || Goal.Kind == 2)
			{
				// The boss at the end of the room.
				Crown(HudAbove(StripTop, 4), FVector2D(X + Segment - 9.0f, Y + H * 0.5f - 1.0f), 13.0f,
					Goal.Kind == 2 ? FireFill : MakeFill(FLinearColor(0.75f, 0.8f, 0.9f), FLinearColor(0.35f, 0.4f, 0.5f)));
			}
		}
	}
	// 部屋 2/3 beside it.
	const FString RoomText = FString::Printf(TEXT("部屋 %d/%d"), FMath::Min(Goal.Cleared + 1, Goal.Rooms), Goal.Rooms);
	const FGeometry LabelSpace = MakeSkewed(Anchor, Width * 0.5f - LabelW, StripY - 3.0f, LabelW, 22.0f, Shear);
	FPainter{LabelSpace, Elements, Layer + 4, Alpha}.Text(RoomText, LabelW - 6.0f, 0.0f, HudFit(RoomText, 17.0f, LabelW - 8.0f), Paper,
		ETextAlign::Right, TEXT("Black"), 2.0f, Ink);
	float Bottom = StripY + StripH;

	// ---- In a boss fight: its own bar, sliding down out from under the goal (jolting as it is hit).
	if (Boss)
	{
		const float In = ChaosImpactEase::Out(FMath::Clamp(static_cast<float>(Now - Anim.BossAt) / 0.4f, 0.0f, 1.0f));
		const float HitAge = static_cast<float>(Now - Anim.BossHitAt);
		const float Jolt = HitAge >= 0.0f && HitAge < 0.22f ? FMath::Sin(HitAge * 80.0f) * (0.22f - HitAge) * 26.0f : 0.0f;
		const float PanelY = Bottom + 10.0f - 18.0f * (1.0f - In);
		const float PanelH = 44.0f;
		const float PanelW = Width - 56.0f;
		const FGeometry BossSpace = MakeSkewed(Anchor, -Width * 0.5f + 28.0f + Jolt, PanelY, PanelW, PanelH, Shear);
		const FPainter BossBack{BossSpace, Elements, Layer, Alpha * In};
		const FPainter BossTop = HudAbove(BossBack, 3);
		HudShard(BossBack, Plate(PanelW, PanelH, 7.0f), MakeFill(FLinearColor(0.16f, 0.03f, 0.05f, 0.96f), FLinearColor(0.05f, 0.01f, 0.02f, 0.96f)),
			2.5f, FVector2D(4.0, 5.0));
		HudShard(BossTop, Bar(10.0f, 9.0f, 62.0f, PanelH - 18.0f), FireFill, 1.5f, FVector2D(2.0, 3.0));
		HudAbove(BossTop, 4).Text(TEXT("ボス"), 41.0f, 11.0f, 18.0f, Paper, ETextAlign::Center, TEXT("Black"), 1.5f, Ink);
		const float BarX = 86.0f;
		const float BarW = PanelW - BarX - 86.0f;
		const float BarY = 13.0f;
		const float BarH = PanelH - 26.0f;
		HudPoly(BossTop, Bar(BarX, BarY, BarW, BarH), SolidFill(FLinearColor(0.03f, 0.005f, 0.01f, 1.0f)), 1);
		// What it just lost, white, draining after a beat; what it has, in fire (glinting when it is low).
		if (Anim.BossTrail > BossRatio + 0.001f)
		{
			HudPoly(HudAbove(BossTop, 1), Bar(BarX, BarY, BarW * Anim.BossTrail, BarH), SolidFill(WithAlpha(Paper, 0.85f)), 1);
		}
		if (BossRatio > 0.0f)
		{
			const bool bLow = BossRatio <= 0.3f;
			const FHudFill Lit = bLow ? MakeFill(FMath::Lerp(FireFill.Top, Paper, 0.25f + 0.25f * FMath::Sin(Time * 8.0f)), FireFill.Bottom) : FireFill;
			HudPoly(HudAbove(BossTop, 2), Bar(BarX, BarY, BarW * BossRatio, BarH), Lit, 2);
		}
		// A mark for each point of its health (when there are few enough to count).
		const int32 Points = FMath::RoundToInt(Boss->GetMaxHealth());
		if (Points > 1 && Points <= 30)
		{
			for (int32 Point = 1; Point < Points; ++Point)
			{
				const float X = BarX + BarW * Point / Points;
				HudAbove(BossTop, 4).Line(FVector2D(X + BarH * 0.45f, BarY + 1.0f), FVector2D(X, BarY + BarH - 1.0f), WithAlpha(Ink, 0.7f), 1.5f);
			}
		}
		const FString HealthText = FString::Printf(TEXT("%d/%d"), FMath::Max(FMath::CeilToInt(Boss->GetHealth() - KINDA_SMALL_NUMBER), 0), Points);
		HudText3D(HudAbove(BossTop, 4), HealthText, PanelW - 14.0f, 8.0f, HudFit(HealthText, 22.0f, 70.0f, TEXT("BlackItalic")), Paper,
			ETextAlign::Right, TEXT("BlackItalic"));
		Bottom = PanelY + PanelH;
	}

	// ---- A room just cleared (not the last: STAGE CLEAR! says that): a word in gold under it for a moment.
	const float ClearAge = static_cast<float>(Now - Anim.ClearedAt);
	if (ClearAge >= 0.0f && ClearAge < 1.8f && !Boss)
	{
		const float In = ChaosImpactEase::Out(FMath::Clamp(ClearAge / 0.25f, 0.0f, 1.0f));
		const float Out = 1.0f - ChaosImpactEase::InOut(FMath::Clamp((ClearAge - 1.4f) / 0.4f, 0.0f, 1.0f));
		const FString Cleared = TEXT("部屋クリア！　体力回復");
		const float TextSize = HudFit(Cleared, 26.0f, Width - 40.0f, TEXT("BlackItalic"));
		const FGeometry Toast = MakeSkewed(Anchor, -Width * 0.5f, Bottom + 12.0f - 10.0f * (1.0f - In), Width, TextSize * 1.4f, Shear);
		HudText3D(FPainter{Toast, Elements, Layer + 8, Alpha * In * Out}, Cleared, Width * 0.5f, 0.0f, TextSize,
			FMath::Lerp(Paper, Gold, 0.65f), ETextAlign::Center, TEXT("BlackItalic"));
		Bottom += 12.0f + TextSize * 1.4f;
	}
	Occupy(Anchor, -Width * 0.5f - 8.0f, -4.0f, Width + 16.0f, Bottom + 12.0f);
}
