#include "ChaosImpactTutorial.h"

#include "ChaosImpactBallTypes.h"
#include "ChaosImpactBallFormComponent.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactChargeWidget.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactPaint.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactSettings.h"
#include "ChaosImpactSfx.h"
#include "ChaosImpactTackleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace
{
	using EStep = EChaosImpactTutorialStep;
	constexpr int32 TutStepCount = static_cast<int32>(EChaosImpactTutorialStep::Count);
	/** How long OK shows before the next step slides in, and how long the closing card stays. */
	constexpr double TutOkSeconds = 1.15;
	constexpr double TutFinishSeconds = 4.5;
	constexpr float TutMoveDistance = 700.0f;
	constexpr float TutAimDegrees = 100.0f;
	constexpr float TutCardWidth = 440.0f;

	/** What this player has set for an action, as its keys are named ("左クリック / G"); empty when nothing is set. */
	FString TutKeys(const APlayerController* Controller, const EChaosImpactAction Action)
	{
		const FString Keys = ChaosImpactSettings::DescribeAction(Controller, Action);
		return Keys == TEXT("－") ? FString() : Keys;
	}

	/** Only the first key of an action (for the four movement keys side by side). */
	FString TutFirstKey(const APlayerController* Controller, const EChaosImpactAction Action)
	{
		FString Keys = TutKeys(Controller, Action);
		FString First;
		return Keys.Split(TEXT(" / "), &First, nullptr) ? First : Keys;
	}

	bool TutUsesPad(const AChaosImpactPlayerController* Controller)
	{
		return Controller && Controller->IsUsingGamepad();
	}

	FString TutStick(const AChaosImpactPlayerController* Controller, const bool bAim)
	{
		const bool bSwapped = ChaosImpactSettings::GetProfile(ChaosImpactSettings::GetPlayerProfile(Controller)).bSwapSticks;
		return (bAim != bSwapped) ? TEXT("右スティック") : TEXT("左スティック");
	}

	/** One step as shown: its name, the keys to press (each in its own cap), what to do with them, and a note. */
	struct FTutText
	{
		FString Title;
		TArray<FString> Keys;
		FString Action;
		FString Note;
	};

	FTutText TutDescribe(const AChaosImpactPlayerController* Controller, const EStep Step)
	{
		using namespace ChaosImpactSettings;
		const EChaosImpactControlScheme Scheme = GetPlayerControlScheme(Controller);
		const bool bPad = TutUsesPad(Controller);
		const bool bMouseOnly = !bPad && IsMouseOnlyScheme(Scheme);
		FTutText Out;
		switch (Step)
		{
		case EStep::Move:
			Out.Title = TEXT("動いてみよう");
			if (bPad) { Out.Keys = {TutStick(Controller, false)}; Out.Action = TEXT("で移動"); }
			else if (Scheme == EChaosImpactControlScheme::MouseOnly) { Out.Keys = {TEXT("右クリック")}; Out.Action = TEXT("した場所へ移動"); }
			else if (Scheme == EChaosImpactControlScheme::MouseHold) { Out.Keys = {TEXT("右クリック長押し")}; Out.Action = TEXT("でカーソルの方へ移動"); }
			else
			{
				Out.Keys = {TutFirstKey(Controller, EChaosImpactAction::MoveUp), TutFirstKey(Controller, EChaosImpactAction::MoveLeft),
					TutFirstKey(Controller, EChaosImpactAction::MoveDown), TutFirstKey(Controller, EChaosImpactAction::MoveRight)};
				Out.Action = TEXT("で移動");
			}
			Out.Note = TEXT("あたりを少し歩きまわる");
			break;
		case EStep::PickUp:
			Out.Title = TEXT("ボールを拾う");
			Out.Action = TEXT("床のボールに近づくと拾えます");
			Out.Note = TEXT("光っている台の上にボールがあります");
			break;
		case EStep::Charge:
			Out.Title = TEXT("ためる");
			Out.Keys = {TutKeys(Controller, EChaosImpactAction::Throw)};
			Out.Action = TEXT("を長押し");
			Out.Note = TEXT("長くためるほど速い球になる");
			break;
		case EStep::Aim:
			Out.Title = TEXT("ねらう");
			Out.Keys = {bPad ? TutStick(Controller, true) : FString(TEXT("マウス"))};
			Out.Action = TEXT("で矢印の向きを変える");
			Out.Note = TEXT("ためたまま、矢印をぐるっと回す");
			break;
		case EStep::Throw:
			Out.Title = TEXT("投げる");
			Out.Keys = {TutKeys(Controller, EChaosImpactAction::Throw)};
			Out.Action = TEXT("をはなすと投げる");
			Out.Note = TEXT("矢印の向きへ飛んでいく");
			break;
		case EStep::Dash:
			Out.Title = TEXT("ダッシュ");
			Out.Keys = {TutKeys(Controller, EChaosImpactAction::Dash)};
			Out.Action = TEXT("で素早くよける");
			Out.Note = bMouseOnly && GetProfile(GetPlayerProfile(Controller)).bMouseDoubleClickDash
				? FString(TEXT("右ダブルクリックでも出せる")) : FString(TEXT("ダッシュ中は球に当たらない"));
			break;
		case EStep::Jump:
			Out.Title = TEXT("ジャンプ");
			Out.Keys = {TutKeys(Controller, EChaosImpactAction::Jump)};
			Out.Action = TEXT("で跳ぶ");
			Out.Note = TEXT("低い球を跳び越せる");
			break;
		case EStep::Tackle:
			Out.Title = TEXT("タックル");
			Out.Keys = {TutKeys(Controller, EChaosImpactAction::Tackle)};
			Out.Action = TEXT("で体当たり");
			Out.Note = TEXT("近くの相手をはじき飛ばす");
			break;
		case EStep::SwapBall:
			Out.Title = TEXT("ボールを入れ替える");
			if (bMouseOnly) { Out.Keys = {TEXT("ホイール")}; Out.Action = TEXT("で次に投げる球を変える"); }
			else
			{
				Out.Keys = {TutKeys(Controller, EChaosImpactAction::SwapBall)};
				Out.Action = Scheme == EChaosImpactControlScheme::Simple && !bPad ? TEXT("を短く押す") : TEXT("で次に投げる球を変える");
			}
			Out.Note = TEXT("ボールを2個持っているときに使える");
			break;
		case EStep::CancelThrow:
			Out.Title = TEXT("投げをやめる");
			Out.Keys = {TutKeys(Controller, EChaosImpactAction::CancelThrow)};
			Out.Action = TEXT("でためた球をしまう");
			Out.Note = TEXT("ためている間に押す");
			break;
		case EStep::DropBall:
			Out.Title = TEXT("ボールを捨てる");
			if (Scheme == EChaosImpactControlScheme::Simple && !bPad)
			{
				Out.Keys = {TutKeys(Controller, EChaosImpactAction::SwapBall)};
				Out.Action = TEXT("を長押し");
			}
			else
			{
				Out.Keys = {TutKeys(Controller, EChaosImpactAction::DropBall)};
				Out.Action = TEXT("で足もとに置く");
			}
			Out.Note = TEXT("いらない球を手放す");
			break;
		default:
			break;
		}
		Out.Keys.RemoveAll([](const FString& Key) { return Key.IsEmpty(); });
		return Out;
	}

	constexpr double TutBallTipSeconds = 6.0;

	/** What a special ball does, in a line or two (shown the first time a player holds one in training). */
	FString TutBallNote(const EChaosImpactBallType Type)
	{
		switch (Type)
		{
		case EChaosImpactBallType::Fire: return TEXT("当たると爆発して、地面をしばらく燃やす。飛んだあとにも火が残る");
		case EChaosImpactBallType::Ice: return TEXT("落ちた所が凍って滑る。近くにいた相手は氷づけになる");
		case EChaosImpactBallType::Thunder: return TEXT("まっすぐ速く飛び、壁で跳ね返るたびに速くなる。相手に当たると雷");
		case EChaosImpactBallType::Black: return TEXT("落ちた所にブラックホール。まわりの相手を真ん中へ引き寄せる");
		case EChaosImpactBallType::Wind: return TEXT("投げると竜巻になり、前へ進みながら相手を吹き飛ばす");
		case EChaosImpactBallType::Smoke: return TEXT("煙が広がり、中にいる相手は前がほとんど見えなくなる");
		case EChaosImpactBallType::Beam: return TEXT("壁も人もすり抜けるビーム。線の上の相手みんなに当たる");
		case EChaosImpactBallType::Snow: return TEXT("持って歩くほど大きくなる。大きいほど当たりやすく、重くなる");
		case EChaosImpactBallType::Nova: return TEXT("その場で長くためるほど巨大に。ゆっくり飛んで大爆発する");
		case EChaosImpactBallType::Simae: return TEXT("シマエナガの群れが相手を追いかけてつつく。ダッシュで振り落とせる");
		case EChaosImpactBallType::Drive: return TEXT("投げたあと移動の操作で曲げられる。最初に当たった相手ではじける");
		case EChaosImpactBallType::Prison: return TEXT("当たった相手を殻に閉じこめる。閉じこめた相手は投げられる");
		case EChaosImpactBallType::Hook: return TEXT("フックを飛ばし、当てた相手や床のボールを手元へ引き寄せる");
		case EChaosImpactBallType::Anchor: return TEXT("壁や床に打ちこみ、そこまで一気に飛んでいく。ダッシュで止まれる");
		default: return FString();
		}
	}

	/** The key that puts the tutorial away: X, or the d-pad's right (unless the player has set it for something). */
	FKey TutHideKey(const AChaosImpactPlayerController* Controller)
	{
		const FKey Key = TutUsesPad(Controller) ? EKeys::Gamepad_DPad_Right : EKeys::X;
		for (int32 Action = 0; Action < static_cast<int32>(EChaosImpactAction::Count); ++Action)
		{
			// (The room's team key is never pressed in training.)
			if (Action != static_cast<int32>(EChaosImpactAction::LobbyTeam) && ChaosImpactSettings::IsActionKey(Controller, static_cast<EChaosImpactAction>(Action), Key))
			{
				return FKey();
			}
		}
		return Key;
	}

	/** A key cap: a dark slanted chip with an ice rim and the key's name. Returns its width. */
	float TutKeyCap(const ChaosImpactPaint::FPainter& P, const float X, const float Y, const FString& Key, const float Size, const float Lit = 1.0f)
	{
		using namespace ChaosImpactPaint;
		const float W = HudMeasure(Key, Size, TEXT("Black")) + Size * 1.1f;
		const float H = Size * 1.55f;
		const float Lean = H * 0.22f;
		HudShard(P, {FVector2D(X + Lean, Y), FVector2D(X + W + Lean, Y), FVector2D(X + W, Y + H), FVector2D(X, Y + H)},
			MakeFill(FLinearColor(0.12f, 0.17f, 0.32f, 0.97f), FLinearColor(0.03f, 0.05f, 0.12f, 0.97f)), 2.0f, FVector2D(2.0, 3.0),
			FMath::Lerp(Navy, Ice, 0.55f * Lit));
		HudAbove(P, 3).Text(Key, X + W * 0.5f + Lean * 0.5f, Y + (H - Size * 1.38f) * 0.5f, Size, WithAlpha(Paper, 0.55f + 0.45f * Lit),
			ETextAlign::Center, TEXT("Black"));
		return W + Lean;
	}
}

void FChaosImpactTutorial::Restart(const double Now)
{
	FinishedAt = 0.0;
	BeginStep(0, Now);
}

void FChaosImpactTutorial::BeginStep(const int32 NewStep, const double Now)
{
	Step = NewStep;
	StepShownAt = Now;
	OkAt = 0.0;
	Progress = 0.0f;
	Moved = 0.0f;
	AimTurned = 0.0f;
	bChargeEndPending = false;
}

bool FChaosImpactTutorial::IsStepAvailable(const AChaosImpactPlayerController* Controller, const int32 InStep) const
{
	// A step whose control has no key set (or that this way of playing does not have) is left out.
	const FTutText Text = TutDescribe(Controller, static_cast<EStep>(InStep));
	const EStep Kind = static_cast<EStep>(InStep);
	return Kind == EStep::PickUp || !Text.Keys.IsEmpty();
}

void FChaosImpactTutorial::Tick(AChaosImpactPlayerController* Controller, const double SwappedAt)
{
	const double Now = FPlatformTime::Seconds();
	AChaosImpactCharacter* Character = Controller ? Cast<AChaosImpactCharacter>(Controller->GetPawn()) : nullptr;
	const UWorld* World = Controller ? Controller->GetWorld() : nullptr;
	const AChaosImpactGameState* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
	// Training only: never an online room's lobby (its own screens are there), a match or a tournament's world.
	// (Not in 訓練場: its lessons teach the same, one at a time.)
	// (Retired since 訓練場 teaches everything in its lessons: neither the steps nor the ball notes come up any more. The
	// button hints along the edges, drawn by the charge widget, are not this and stay.)
	static constexpr bool bRetired = true;
	const bool bPlace = !bRetired && Controller && Controller->IsTrainingMode() && !Controller->IsOnlineRoom() && !Controller->IsDojo()
		&& !(Match && (Match->bVersusMatch || Match->IsTournament() || Match->bPublicTournament));
	const bool bOn = bPlace && ChaosImpactSettings::IsTrainingTutorialOn();
	if (bOn && Revision != ChaosImpactSettings::GetTrainingTutorialRevision())
	{
		Revision = ChaosImpactSettings::GetTrainingTutorialRevision();
		bDoneThisVisit = false;
		bHidden = false;
		Restart(Now);
	}
	// Put away (and the ball notes with it) for the rest of this visit.
	if (bPlace && Controller && !bHidden)
	{
		const FKey Hide = TutHideKey(Controller);
		if (Hide.IsValid() && Controller->WasInputKeyJustPressed(Hide) && Controller->IsGameplayActive())
		{
			bHidden = true;
			BallTipQueue.Reset();
			BallTipAt = 0.0;
			ChaosImpactSfx::Play2D(Controller, EChaosImpactSfx::UiBack);
		}
	}
	// A special ball held for the first time: what it does.
	if (bPlace && Character && !bHidden)
	{
		for (int32 Slot = 0; Slot < Character->GetCarriedBallCount(); ++Slot)
		{
			const uint8 Type = static_cast<uint8>(Character->GetCarriedBallType(Slot));
			if (Type != static_cast<uint8>(EChaosImpactBallType::Normal) && !SeenBalls.Contains(Type) && !TutBallNote(static_cast<EChaosImpactBallType>(Type)).IsEmpty())
			{
				SeenBalls.Add(Type);
				BallTipQueue.Add(Type);
			}
		}
		if (BallTipAt > 0.0 && Now - BallTipAt > TutBallTipSeconds)
		{
			BallTipAt = 0.0;
		}
		if (BallTipAt <= 0.0 && !BallTipQueue.IsEmpty())
		{
			BallTip = BallTipQueue[0];
			BallTipQueue.RemoveAt(0);
			BallTipAt = Now;
			ChaosImpactSfx::Play2D(Controller, EChaosImpactSfx::UiOpen);
		}
	}
	if (FinishedAt > 0.0 && Now - FinishedAt > TutFinishSeconds)
	{
		FinishedAt = 0.0;
	}
	bActive = bOn && !bHidden && !bDoneThisVisit && FinishedAt <= 0.0 && Character && !Character->IsEliminated() && Controller->IsGameplayActive();
	if (!bActive || !Character)
	{
		bHaveLocation = false;
		return;
	}
	// Steps this player cannot do are passed over.
	while (Step < TutStepCount && OkAt <= 0.0 && !IsStepAvailable(Controller, Step))
	{
		BeginStep(Step + 1, Now);
	}
	if (Step >= TutStepCount)
	{
		FinishedAt = Now;
		bActive = false;
		bDoneThisVisit = true;
		ChaosImpactSfx::Play2D(Controller, EChaosImpactSfx::UiStart);
		return;
	}
	if (OkAt > 0.0)
	{
		if (Now - OkAt >= TutOkSeconds)
		{
			BeginStep(Step + 1, Now);
		}
		return;
	}

	// What the player is doing.
	const FVector Location = Character->GetActorLocation();
	const float Step2D = bHaveLocation ? static_cast<float>(FVector::Dist2D(Location, LastLocation)) : 0.0f;
	LastLocation = Location;
	bHaveLocation = true;
	const bool bCharging = Character->IsChargingThrow();
	const int32 Balls = Character->GetCarriedBallCount();
	const FVector Aim = Character->GetAimDirection().GetSafeNormal2D();
	if (bCharging && bWasCharging && !Aim.IsNearlyZero() && !LastAim.IsNearlyZero())
	{
		AimTurned += FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(static_cast<float>(FVector::DotProduct(Aim, LastAim)), -1.0f, 1.0f)));
	}
	if (!Aim.IsNearlyZero()) { LastAim = Aim; }
	if (bWasCharging && !bCharging)
	{
		ChargeEndedAt = Now;
		BallsAtChargeEnd = LastBalls;
		bChargeEndPending = true;
	}
	bool bDone = false;
	switch (static_cast<EStep>(Step))
	{
	case EStep::Move:
		// Walking (a teleport or a respawn does not count).
		// (Only while the player moves it: not the settling in at the start, a push, a teleport or a respawn.)
		if (Character->GetCharacterMovement() && !Character->GetCharacterMovement()->GetCurrentAcceleration().IsNearlyZero()
			&& !Character->IsDashing() && Now - StepShownAt > 0.4)
		{
			Moved += Step2D < 400.0f ? Step2D : 0.0f;
		}
		Progress = FMath::Clamp(Moved / TutMoveDistance, 0.0f, 1.0f);
		bDone = Moved >= TutMoveDistance;
		break;
	case EStep::PickUp:
		bDone = Balls > 0;
		break;
	case EStep::Charge:
		Progress = bCharging ? FMath::Clamp(Character->GetThrowChargeAlpha() / 0.75f, 0.0f, 1.0f) : 0.0f;
		bDone = bCharging && Character->GetThrowChargeAlpha() >= 0.75f;
		break;
	case EStep::Aim:
		if (!bCharging) { AimTurned = 0.0f; }
		Progress = FMath::Clamp(AimTurned / TutAimDegrees, 0.0f, 1.0f);
		bDone = AimTurned >= TutAimDegrees;
		break;
	case EStep::Throw:
		bDone = bChargeEndPending && ChargeEndedAt >= StepShownAt && Now - ChargeEndedAt < 1.5 && Balls < BallsAtChargeEnd;
		break;
	case EStep::Dash:
		bDone = Character->IsDashing();
		break;
	case EStep::Jump:
		bDone = Character->GetCharacterMovement() && Character->GetCharacterMovement()->IsFalling() && Character->GetVelocity().Z > 60.0f;
		break;
	case EStep::Tackle:
		bDone = Character->GetTackle() && Character->GetTackle()->IsActive();
		break;
	case EStep::SwapBall:
		bDone = SwappedAt > StepShownAt && Balls >= 2;
		break;
	case EStep::CancelThrow:
		// The charge ended, and a moment later the ball is still in hand: put away, not thrown.
		bDone = bChargeEndPending && ChargeEndedAt >= StepShownAt && Now - ChargeEndedAt > 0.7 && Balls >= BallsAtChargeEnd && BallsAtChargeEnd > 0;
		break;
	case EStep::DropBall:
		// One fewer while nothing was being thrown.
		bDone = !bCharging && !bWasCharging && Balls < LastBalls && !(bChargeEndPending && Now - ChargeEndedAt < 1.5);
		break;
	default:
		break;
	}
	if (bChargeEndPending && Now - ChargeEndedAt > 1.6) { bChargeEndPending = false; }
	bWasCharging = bCharging;
	LastBalls = Balls;
	if (bDone)
	{
		OkAt = Now;
		Progress = 1.0f;
		ChaosImpactSfx::Play2D(Controller, EChaosImpactSfx::UiSelect);
	}
}

void FChaosImpactTutorial::Paint(const FGeometry& Geometry, FSlateWindowElementList& Elements, const int32 Layer,
	const AChaosImpactPlayerController* Controller, const float Scale, const float TopOffset, const float Alpha, TArray<FBox2D>* OutBoxes) const
{
	using namespace ChaosImpactPaint;
	if (!Controller || Alpha <= 0.01f || bHidden)
	{
		return;
	}
	// A special ball's note: under the card, or in its place once the tutorial is done.
	const bool bCard = bActive || FinishedAt > 0.0;
	PaintBallTip(Geometry, Elements, Layer + 20, Scale, TopOffset + (bCard ? 236.0f : 0.0f), Alpha);
	if (OutBoxes)
	{
		// The card (and a ball's note under it) at the top right, taken so the arrows to others keep off it.
		const float Right = Geometry.GetLocalSize().X;
		const float Bottom = (TopOffset + (bCard ? 236.0f : 0.0f) + (BallTipAt > 0.0 ? 130.0f : 0.0f)) * Scale;
		if (bCard || BallTipAt > 0.0)
		{
			OutBoxes->Add(FBox2D(FVector2D(Right - (22.0f + TutCardWidth + 20.0f) * Scale, TopOffset * Scale - 10.0f), FVector2D(Right, Bottom)));
		}
	}
	if (!bCard)
	{
		return;
	}
	const FVector2f Size = Geometry.GetLocalSize();
	const double Now = FPlatformTime::Seconds();
	const float W = TutCardWidth;
	// Its own space: the card's top right corner at the screen's, in HUD units.
	const FGeometry Corner = MakeAnchor(Geometry, Size.X - 22.0f * Scale, TopOffset * Scale, Scale);
	const float X = -W;

	// ---- The closing card.
	if (FinishedAt > 0.0)
	{
		const float T = static_cast<float>(Now - FinishedAt);
		const float In = EaseOut(T / 0.35f) * (1.0f - FMath::Clamp((T - (TutFinishSeconds - 0.5f)) / 0.5f, 0.0f, 1.0f)) * Alpha;
		const FPainter P{Corner, Elements, Layer, In};
		const float Slide = (1.0f - EaseOut(T / 0.35f)) * 70.0f;
		const float H = 112.0f;
		HudShard(P, {FVector2D(X + 18.0f + Slide, 0.0), FVector2D(Slide, 0.0), FVector2D(-18.0f + Slide, H), FVector2D(X + Slide, H)}, FireFill, 3.0f);
		HudText3D(HudAbove(P, 4), TEXT("チュートリアル完了！"), X + W * 0.5f + Slide, 16.0f, HudFit(TEXT("チュートリアル完了！"), 38.0f, W - 60.0f), Paper,
			ETextAlign::Center);
		HudAbove(P, 5).Text(TEXT("設定からいつでもやり直せます"), X + W * 0.5f + Slide, 70.0f, 17.0f, Ink, ETextAlign::Center, TEXT("Bold"));
		return;
	}

	const EStep Kind = static_cast<EStep>(Step);
	const FTutText Text = TutDescribe(Controller, Kind);
	const float Age = static_cast<float>(Now - StepShownAt);
	const float OkAge = OkAt > 0.0 ? static_cast<float>(Now - OkAt) : -1.0f;
	// In from the right as a step starts; out as its OK finishes.
	const float In = EaseOut(Age / 0.3f);
	const float Out = OkAge >= 0.0f ? FMath::Clamp((OkAge - (TutOkSeconds - 0.28f)) / 0.28f, 0.0f, 1.0f) : 0.0f;
	const float Shown = In * (1.0f - Out) * Alpha;
	const float Slide = (1.0f - In) * 80.0f + Out * 60.0f;
	const bool bOk = OkAge >= 0.0f;

	// ---- Above the card: TUTORIAL, which step of how many, and a diamond for each step.
	{
		const FPainter P{Corner, Elements, Layer, Alpha};
		const FString Count = FString::Printf(TEXT("%d / %d"), Step + 1, TutStepCount);
		HudText3D(HudAbove(P, 1), TEXT("TUTORIAL"), X + 10.0f, 0.0f, 22.0f, Gold, ETextAlign::Left);
		P.Text(Count, -6.0f, 4.0f, 18.0f, WithAlpha(Paper, 0.85f), ETextAlign::Right, TEXT("BlackItalic"), 2.0f, Ink);
		const float RowY = 36.0f;
		const float Gap = (W - 30.0f) / TutStepCount;
		for (int32 Index = 0; Index < TutStepCount; ++Index)
		{
			const FVector2D C(X + 18.0f + Gap * (Index + 0.5f), RowY);
			const bool bDoneStep = Index < Step || (Index == Step && bOk);
			const bool bNow = Index == Step && !bOk;
			const float R = bNow ? 7.0f + 1.5f * FMath::Sin(static_cast<float>(Now) * 6.0f) : 6.0f;
			const TArray<FVector2D> Diamond{C + FVector2D(0.0, -R), C + FVector2D(R, 0.0), C + FVector2D(0.0, R), C + FVector2D(-R, 0.0)};
			HudPoly(P, HudInflate(Diamond, 2.0f), SolidFill(WithAlpha(Ink, 0.85f)), 1);
			HudPoly(HudAbove(P, 1), Diamond, bDoneStep ? FireFill : bNow ? IceFill : SolidFill(WithAlpha(Muted, 0.35f)), 2);
			if (Index + 1 < TutStepCount)
			{
				P.Line(C + FVector2D(R + 3.0f, 0.0), C + FVector2D(Gap - R - 3.0f, 0.0), WithAlpha(bDoneStep ? Gold : Muted, bDoneStep ? 0.8f : 0.3f), 2.0f);
			}
		}
	}

	// ---- The card: what to do, and the keys for it.
	const float Top = 52.0f;
	const float H = 150.0f;
	const FPainter Card{Corner, Elements, Layer + 2, Shown};
	const float Lean = 18.0f;
	const TArray<FVector2D> Shape{FVector2D(X + Lean + Slide, Top), FVector2D(Slide, Top), FVector2D(-Lean + Slide, Top + H), FVector2D(X + Slide, Top + H)};
	HudShard(Card, Shape, MakeFill(FLinearColor(0.07f, 0.1f, 0.22f, 0.95f), FLinearColor(0.015f, 0.02f, 0.06f, 0.95f)), 3.0f, FVector2D(5.0, 6.0),
		bOk ? Gold : Navy);
	// The stripe down its left side: ice, gold once done.
	const FPainter Stripe = HudAbove(Card, 3);
	HudPoly(Stripe, {FVector2D(X + Lean + Slide, Top), FVector2D(X + Lean + 10.0f + Slide, Top), FVector2D(X + 10.0f + Slide, Top + H),
		FVector2D(X + Slide, Top + H)}, bOk ? FireFill : IceFill, 2);
	const float TextX = X + 34.0f + Slide;
	const float TextW = W - 70.0f;
	const FPainter Words = HudAbove(Card, 5);
	HudText3D(Words, Text.Title, TextX, Top + 10.0f, HudFit(Text.Title, 32.0f, TextW), bOk ? Gold : Paper, ETextAlign::Left);
	// The keys, then what to do with them (made smaller together if they would not fit).
	{
		float KeySize = 19.0f;
		const auto Measure = [&](const float Size)
		{
			float Total = 0.0f;
			for (const FString& Key : Text.Keys) { Total += HudMeasure(Key, Size, TEXT("Black")) + Size * 1.1f + Size * 0.34f + 8.0f; }
			return Total + HudMeasure(Text.Action, Size, TEXT("Bold")) + 6.0f;
		};
		while (KeySize > 12.0f && Measure(KeySize) > TextW) { KeySize -= 1.0f; }
		float KeyX = TextX - 2.0f;
		const float KeyY = Top + 60.0f;
		for (const FString& Key : Text.Keys)
		{
			KeyX += TutKeyCap(HudAbove(Card, 6), KeyX, KeyY, Key, KeySize) + 8.0f;
		}
		Words.Text(Text.Action, KeyX + 2.0f, KeyY + KeySize * 0.18f, HudFit(Text.Action, KeySize, FMath::Max(40.0f, TextX + TextW - KeyX)), Paper,
			ETextAlign::Left, TEXT("Bold"), 2.0f, Ink);
	}
	// A note under it, or what has to come first.
	FString Note = Text.Note;
	FLinearColor NoteColor = WithAlpha(Paper, 0.7f);
	if (const AChaosImpactCharacter* Character = Cast<AChaosImpactCharacter>(Controller->GetPawn()); Character && !bOk)
	{
		const int32 Balls = Character->GetCarriedBallCount();
		if ((Kind == EStep::Charge || Kind == EStep::Aim || Kind == EStep::Throw || Kind == EStep::CancelThrow || Kind == EStep::DropBall) && Balls == 0)
		{
			Note = TEXT("まずボールを拾おう");
			NoteColor = Gold;
		}
		else if (Kind == EStep::SwapBall && Balls < 2)
		{
			Note = TEXT("ボールをもう1個拾おう");
			NoteColor = Gold;
		}
		else if ((Kind == EStep::Aim || Kind == EStep::CancelThrow) && !Character->IsChargingThrow())
		{
			Note = FString::Printf(TEXT("%s を長押ししてためながら"), *TutKeys(Controller, EChaosImpactAction::Throw));
			NoteColor = Gold;
		}
	}
	Words.Text(Note, TextX, Top + 110.0f, HudFit(Note, 16.0f, TextW, TEXT("Bold")), NoteColor, ETextAlign::Left, TEXT("Bold"), 1.5f, Ink);
	// How far along (moving, charging, turning the arrow).
	if (Kind == EStep::Move || Kind == EStep::Charge || Kind == EStep::Aim)
	{
		const float BarX = TextX, BarY = Top + H - 9.0f, BarW = TextW - 10.0f;
		Card.Box(BarX, BarY, BarW, 4.0f, WithAlpha(Ink, 0.9f));
		HudAbove(Card, 4).Box(BarX, BarY, BarW * Progress, 4.0f, FMath::Lerp(Ice, Gold, Progress));
	}
	// ---- OK!
	if (bOk)
	{
		const float Pop = 1.0f + 0.9f * FMath::Exp(-14.0f * OkAge);
		const float OkAlpha = FMath::Clamp(OkAge / 0.06f, 0.0f, 1.0f) * (1.0f - Out) * Alpha;
		const FGeometry Stamp = MakeSkewed(Corner, -190.0f + Slide, Top + 34.0f, 170.0f, 90.0f, -0.18f, Pop);
		const FPainter OkP{Stamp, Elements, Layer + 12, OkAlpha};
		HudText3D(OkP, TEXT("OK!"), 85.0f, 0.0f, 72.0f, Gold, ETextAlign::Center);
		// A sweep of light across the card as it is done.
		const float Sweep = FMath::Clamp(OkAge / 0.35f, 0.0f, 1.0f);
		if (Sweep < 1.0f)
		{
			const float SX = X + (W + 60.0f) * Sweep + Slide;
			HudPoly(FPainter{Corner, Elements, Layer + 9, (1.0f - Sweep) * Alpha * 0.55f},
				{FVector2D(SX, Top), FVector2D(SX + 40.0f, Top), FVector2D(SX + 10.0f, Top + H), FVector2D(SX - 30.0f, Top + H)}, SolidFill(Paper), 1);
		}
	}
	// ---- What comes next, faintly under the card.
	if (Step + 1 < TutStepCount)
	{
		int32 Next = Step + 1;
		while (Next < TutStepCount && !IsStepAvailable(Controller, Next)) { ++Next; }
		if (Next < TutStepCount)
		{
			const FString Line = FString::Printf(TEXT("つぎ ▶ %s"), *TutDescribe(Controller, static_cast<EStep>(Next)).Title);
			FPainter{Corner, Elements, Layer + 2, Alpha * 0.75f}.Text(Line, -10.0f, Top + H + 10.0f, HudFit(Line, 16.0f, W * 0.55f, TEXT("Bold")),
				WithAlpha(Paper, 0.8f), ETextAlign::Right, TEXT("Bold"), 1.5f, Ink);
		}
	}
	// How to put it away.
	{
		const FKey Hide = TutHideKey(Controller);
		if (Hide.IsValid())
		{
			const FString HideKey = ChaosImpactSettings::GetKeyName(Hide, ChaosImpactSettings::GetPlayerDevice(Controller));
			const FPainter HideP{Corner, Elements, Layer + 2, Alpha * 0.8f};
			const float CapW = TutKeyCap(HideP, X + 4.0f, Top + H + 8.0f, HideKey, 13.0f);
			HideP.Text(TEXT("で非表示"), X + 10.0f + CapW, Top + H + 10.0f, 15.0f, WithAlpha(Paper, 0.75f), ETextAlign::Left, TEXT("Bold"), 1.5f, Ink);
		}
	}
}

void FChaosImpactTutorial::PaintBallTip(const FGeometry& Geometry, FSlateWindowElementList& Elements, const int32 Layer, const float Scale,
	const float Top, const float Alpha) const
{
	using namespace ChaosImpactPaint;
	if (BallTipAt <= 0.0)
	{
		return;
	}
	const float T = static_cast<float>(FPlatformTime::Seconds() - BallTipAt);
	const float In = EaseOut(T / 0.3f) * (1.0f - FMath::Clamp((T - (static_cast<float>(TutBallTipSeconds) - 0.4f)) / 0.4f, 0.0f, 1.0f)) * Alpha;
	if (In <= 0.01f)
	{
		return;
	}
	const EChaosImpactBallType Type = static_cast<EChaosImpactBallType>(BallTip);
	const FVector2f Size = Geometry.GetLocalSize();
	const FGeometry Corner = MakeAnchor(Geometry, Size.X - 22.0f * Scale, Top * Scale, Scale);
	const float W = TutCardWidth, H = 132.0f, X = -W, Slide = (1.0f - EaseOut(T / 0.3f)) * 70.0f;
	const FLinearColor Tint = ChaosImpactBallTypes::GetColor(Type);
	const FPainter Card{Corner, Elements, Layer, In};
	HudShard(Card, {FVector2D(X + 18.0f + Slide, 0.0), FVector2D(Slide, 0.0), FVector2D(-18.0f + Slide, H), FVector2D(X + Slide, H)},
		MakeFill(FMath::Lerp(FLinearColor(0.07f, 0.1f, 0.22f, 0.95f), Tint, 0.18f), FLinearColor(0.015f, 0.02f, 0.06f, 0.95f)), 3.0f, FVector2D(5.0, 6.0),
		FMath::Lerp(Navy, Tint, 0.6f));
	// The ball itself, as it is drawn in the hand.
	UChaosImpactChargeWidget::PaintBallIcon(HudAbove(Card, 4), FVector2D(X + 58.0f + Slide, H * 0.5f), 30.0f, true, Type, 0.0f,
		static_cast<float>(FMath::Fmod(FPlatformTime::Seconds(), 3600.0)));
	const FPainter Words = HudAbove(Card, 6);
	Words.Text(TEXT("NEW BALL"), X + 104.0f + Slide, 10.0f, 15.0f, Tint, ETextAlign::Left, TEXT("BlackItalic"), 1.5f, Ink);
	const FString Name = ChaosImpactBallTypes::GetDisplayName(Type);
	HudText3D(Words, Name, X + 104.0f + Slide, 28.0f, HudFit(Name, 28.0f, W - 140.0f), Paper, ETextAlign::Left);
	// What it does: one line, or two split at the punctuation nearest the middle.
	const FString Note = TutBallNote(Type);
	const float NoteW = W - 130.0f;
	TArray<FString> Lines{Note};
	if (HudMeasure(Note, 17.0f, TEXT("Bold")) > NoteW)
	{
		int32 Best = INDEX_NONE;
		for (int32 At = 1; At < Note.Len() - 1; ++At)
		{
			if ((Note[At] == TEXT('、') || Note[At] == TEXT('。')) && (Best == INDEX_NONE || FMath::Abs(At - Note.Len() / 2) < FMath::Abs(Best - Note.Len() / 2)))
			{
				Best = At;
			}
		}
		const int32 Cut = Best != INDEX_NONE ? Best + 1 : Note.Len() / 2;
		Lines = {Note.Left(Cut), Note.Mid(Cut)};
	}
	for (int32 Line = 0; Line < Lines.Num(); ++Line)
	{
		Words.Text(Lines[Line], X + 104.0f + Slide, 68.0f + Line * 24.0f, HudFit(Lines[Line], 17.0f, NoteW, TEXT("Bold")), WithAlpha(Paper, 0.9f),
			ETextAlign::Left, TEXT("Bold"), 1.5f, Ink);
	}
}

void FChaosImpactTutorial::PaintHints(const FGeometry& Geometry, FSlateWindowElementList& Elements, const int32 Layer,
	const AChaosImpactPlayerController* Controller, const float Scale, TArray<FBox2D>* OutBoxes)
{
	using namespace ChaosImpactPaint;
	const AChaosImpactCharacter* Character = Controller ? Cast<AChaosImpactCharacter>(Controller->GetPawn()) : nullptr;
	if (!Character || Character->IsEliminated())
	{
		return;
	}
	const FVector2f Size = Geometry.GetLocalSize();
	const bool bPad = TutUsesPad(Controller);
	const EChaosImpactControlScheme Scheme = ChaosImpactSettings::GetPlayerControlScheme(Controller);
	const int32 Balls = Character->GetCarriedBallCount();
	const bool bCharging = Character->IsChargingThrow();
	struct FHint { FString Key; FString Label; float Lit; bool bSpare; };
	TArray<FHint> Right, Left;
	// Only what can be done now (the first key set for it); dash stays, dimmed while it recovers. Spare ones (pause,
	// the training menu) go first when a small screen has no room for them all.
	const auto Add = [](TArray<FHint>& Into, const FString& Keys, const FString& Label, const bool bShow, const float Lit = 1.0f,
		const bool bSpare = false)
	{
		FString First = Keys;
		Keys.Split(TEXT(" / "), &First, nullptr);
		if (bShow && !First.IsEmpty()) { Into.Add({First, Label, Lit, bSpare}); }
	};
	const bool bSimple = Scheme == EChaosImpactControlScheme::Simple;
	const bool bMouseOnly = ChaosImpactSettings::IsMouseOnlyScheme(Scheme);
	// Ball form, and with it how a ball is put down: one key, a press to drop and a hold to turn into a ball (by layout).
	const UChaosImpactBallFormComponent* Form = Character->GetBallForm();
	const bool bFormReady = Form && Form->IsAvailable() && !Form->IsCaptured();
	// (Caught by someone is not this player's own ball form: nothing to undo then.)
	const bool bInForm = Form && Form->IsInBallForm() && !Form->IsCaptured();
	const bool bCaught = Form && Form->IsCaptured();
	const FString FormKeys = TutKeys(Controller, bSimple ? EChaosImpactAction::SwapBall : bMouseOnly ? EChaosImpactAction::Dash : EChaosImpactAction::DropBall);
	// Pause and the training menu: their own corner buttons when played with the mouse styles.
	const bool bCornerButtons = !bPad && Scheme != EChaosImpactControlScheme::Normal;
	const UWorld* World = Controller->GetWorld();
	const AChaosImpactGameState* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
	// (The training menu opens only in training on this machine: never online or in a VS match.)
	const bool bTrainingMenu = Controller->IsTrainingMode() && Controller->IsPrimaryLocalPlayerController() && !Controller->IsOnlineRoom()
		&& !Controller->IsVersusMatchWorld() && !(Match && (Match->IsTournament() || Match->bPublicTournament));
	if (bInForm)
	{
		Add(Right, FormKeys, TEXT("元に戻る"), true);
	}
	else if (bCharging)
	{
		Add(Right, TutKeys(Controller, EChaosImpactAction::Throw), TEXT("投げる"), true);
		Add(Left, bPad ? TutStick(Controller, true) : FString(TEXT("マウス")), TEXT("ねらう"), true);
		Add(Left, TutKeys(Controller, EChaosImpactAction::CancelThrow), TEXT("キャンセル"), true);
	}
	else if (!bCaught)
	{
		Add(Right, TutKeys(Controller, EChaosImpactAction::Throw), TEXT("投げる"), Balls > 0);
	}
	if (!bInForm && !bCaught)
	{
		Add(Right, TutKeys(Controller, EChaosImpactAction::Dash), TEXT("ダッシュ"), true, Character->CanDashNow() ? 1.0f : 0.45f);
		Add(Right, TutKeys(Controller, EChaosImpactAction::Jump), TEXT("ジャンプ"), !bCharging);
	}
	if (!bCharging && !bInForm && !bCaught)
	{
		Add(Left, TutKeys(Controller, EChaosImpactAction::Tackle), TEXT("タックル"), true);
		Add(Left, bMouseOnly && !bPad ? FString(TEXT("ホイール")) : TutKeys(Controller, EChaosImpactAction::SwapBall), TEXT("入れ替え"), Balls >= 2);
		// Putting a ball down: its own key (a press); the simple layout holds swap; the mouse layouts hold on the ball frame.
		if (bMouseOnly && !bPad)
		{
			Add(Left, FString(TEXT("ボール枠")), TEXT("右長押しで捨てる"), Balls > 0);
		}
		else if (bSimple && !bPad)
		{
			Add(Left, TutKeys(Controller, EChaosImpactAction::SwapBall), TEXT("長押しで捨てる"), Balls > 0 && !bFormReady);
		}
		else
		{
			Add(Left, TutKeys(Controller, EChaosImpactAction::DropBall), TEXT("捨てる"), Balls > 0);
		}
		Add(Left, FormKeys, TEXT("長押しでボール化"), bFormReady);
	}
	if (!bCornerButtons)
	{
		Add(Left, TutKeys(Controller, EChaosImpactAction::TrainingMenu), TEXT("練習設定"), bTrainingMenu, 1.0f, true);
		Add(Left, TutKeys(Controller, EChaosImpactAction::Pause), TEXT("ポーズ"), true, 1.0f, true);
		Add(Left, TutKeys(Controller, EChaosImpactAction::OverviewMap), TEXT("全体マップ"), true, 1.0f, true);
	}

	// One hint: the button drawn as it looks (a trigger's tab, a face button's disc, a key's cap), then what it does on a
	// faint dark backing, as consoles show them; no panel round them.
	// (Small and quiet: readable at a glance, never in the way. GlyphH: a button's height; the rest follow from it.)
	constexpr float GlyphH = 25.0f;
	constexpr float KeySize = 14.0f;
	constexpr float LabelSize = 16.0f;
	constexpr float Gap = 7.0f;
	const auto Glyph = [&](const FPainter& P, const float X, const float Y, const FString& Key, const float Lit) -> float
	{
		const float H = GlyphH;
		const FLinearColor Body = FLinearColor(0.13f, 0.13f, 0.15f, 0.88f);
		const FLinearColor Rim = FLinearColor(0.62f, 0.64f, 0.7f, 0.85f);
		const FLinearColor Face = WithAlpha(Paper, 0.55f + 0.45f * Lit);
		static const TCHAR* const Triggers[] = {TEXT("LT"), TEXT("RT"), TEXT("LB"), TEXT("RB"), TEXT("ZL"), TEXT("ZR"), TEXT("L"), TEXT("R"),
			TEXT("L1"), TEXT("R1"), TEXT("L2"), TEXT("R2")};
		static const TCHAR* const Buttons[] = {TEXT("A"), TEXT("B"), TEXT("X"), TEXT("Y"), TEXT("○"), TEXT("×"), TEXT("△"), TEXT("□")};
		bool bTrigger = false, bButton = false;
		for (const TCHAR* Name : Triggers) { bTrigger |= Key == Name; }
		for (const TCHAR* Name : Buttons) { bButton |= Key == Name; }
		if (bButton)
		{
			const FVector2D C(X + H * 0.5f, Y + H * 0.5f);
			P.Disc(C + FVector2D(1.0, 1.5), H * 0.5f + 1.2f, FLinearColor(0.0f, 0.0f, 0.0f, 0.4f));
			P.Disc(C, H * 0.5f + 1.2f, Rim);
			HudAbove(P, 1).Disc(C, H * 0.5f - 0.5f, Body);
			HudAbove(P, 2).Text(Key, C.X, Y + 1.5f, 16.0f, Face, ETextAlign::Center, TEXT("Black"));
			return H;
		}
		const float TextSize = HudFit(Key, KeySize, 140.0f);
		const float W = FMath::Max(H * 1.3f, HudMeasure(Key, TextSize, TEXT("Black")) + 14.0f);
		// A trigger: a tab, its outer corner cut away (left on the left hand's, right on the right's).
		const bool bLeftHand = Key.StartsWith(TEXT("L")) || Key.StartsWith(TEXT("ZL"));
		const float Cut = bTrigger ? 7.0f : 0.0f;
		const TArray<FVector2D> Shape = bTrigger
			? (bLeftHand ? TArray<FVector2D>{FVector2D(X + Cut, Y), FVector2D(X + W, Y), FVector2D(X + W, Y + H), FVector2D(X, Y + H), FVector2D(X, Y + Cut)}
				: TArray<FVector2D>{FVector2D(X, Y), FVector2D(X + W - Cut, Y), FVector2D(X + W, Y + Cut), FVector2D(X + W, Y + H), FVector2D(X, Y + H)})
			: TArray<FVector2D>{FVector2D(X + 3.0f, Y), FVector2D(X + W - 3.0f, Y), FVector2D(X + W, Y + 3.0f), FVector2D(X + W, Y + H - 3.0f),
				FVector2D(X + W - 3.0f, Y + H), FVector2D(X + 3.0f, Y + H), FVector2D(X, Y + H - 3.0f), FVector2D(X, Y + 3.0f)};
		HudPoly(P, HudMove(HudInflate(Shape, 1.2f), FVector2D(1.0, 1.5)), SolidFill(FLinearColor(0.0f, 0.0f, 0.0f, 0.4f)), 1);
		HudPoly(HudAbove(P, 1), HudInflate(Shape, 1.2f), SolidFill(Rim), 1);
		HudPoly(HudAbove(P, 2), Shape, MakeFill(FLinearColor(0.2f, 0.2f, 0.23f, 0.92f), Body), 2);
		// (A keyboard key shows its bottom edge, as a key does.)
		if (!bTrigger) { HudAbove(P, 3).Box(X + 3.0f, Y + H - 3.5f, W - 6.0f, 1.5f, FLinearColor(0.0f, 0.0f, 0.0f, 0.35f)); }
		HudAbove(P, 4).Text(Key, X + W * 0.5f, Y + (H - TextSize * 1.38f) * 0.5f, TextSize, Face, ETextAlign::Center, TEXT("Black"));
		return W;
	};
	// How wide a button and a label come out (to set the right-hand column against the screen's edge).
	const auto GlyphWidth = [](const FString& Key) -> float
	{
		static const TCHAR* const Buttons[] = {TEXT("A"), TEXT("B"), TEXT("X"), TEXT("Y"), TEXT("○"), TEXT("×"), TEXT("△"), TEXT("□")};
		for (const TCHAR* Name : Buttons) { if (Key == Name) { return GlyphH; } }
		return FMath::Max(GlyphH * 1.3f, HudMeasure(Key, HudFit(Key, KeySize, 140.0f), TEXT("Black")) + 14.0f);
	};
	const auto LabelWidth = [](const FString& Text, const float MaxWidth)
	{
		return FMath::Min(HudMeasure(Text, LabelSize, TEXT("Bold")), MaxWidth - 12.0f) + 12.0f;
	};
	// The words on a faint dark strip, outlined so they read over anything.
	const auto Label = [&](const FPainter& P, const float X, const float Y, const FString& Text, const float Lit, const float MaxWidth)
	{
		const float TextSize = HudFit(Text, LabelSize, MaxWidth - 12.0f, TEXT("Bold"));
		const float W = HudMeasure(Text, TextSize, TEXT("Bold")) + 12.0f;
		P.Box(X, Y + 2.0f, W, GlyphH - 4.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.28f));
		HudAbove(P, 1).Text(Text, X + 6.0f, Y + 2.0f + (LabelSize - TextSize) * 0.6f, TextSize, WithAlpha(Paper, 0.62f + 0.38f * Lit), ETextAlign::Left,
			TEXT("Bold"), 1.5f, FLinearColor(0.0f, 0.0f, 0.0f, 0.65f));
	};
	const float RowH = 32.0f;
	const float Width = Size.X / Scale;
	const float Height = Size.Y / Scale;
	// Right: over the ball holder (and over the corner buttons the mouse styles have there); left: over the health.
	const float RightBottom = Height - (bCornerButtons ? 340.0f : 290.0f);
	// (Over the health and, in a match, the place badge above it.)
	const float LeftBottom = Height - 300.0f;
	const FGeometry Space = MakeAnchor(Geometry, 0.0f, 0.0f, Scale);
	// A column never climbs into the top of the screen (the scores, the timer): the spare hints go first, then the top ones.
	const auto Trim = [RowH](TArray<FHint>& Column, const float Bottom)
	{
		const int32 Room = FMath::Max(1, FMath::FloorToInt((Bottom - 170.0f) / RowH));
		for (int32 Index = Column.Num() - 1; Index >= 0 && Column.Num() > Room; --Index)
		{
			if (Column[Index].bSpare) { Column.RemoveAt(Index); }
		}
		while (Column.Num() > Room) { Column.RemoveAt(0); }
	};
	Trim(Right, RightBottom);
	Trim(Left, LeftBottom);
	// Each column keeps to its own half of the screen, in from its edge: a long key name or label is set smaller,
	// never past the edge. (The rows are registered as taken too, so the arrows to others keep off them.)
	constexpr float Margin = 30.0f;
	const float ColumnMax = FMath::Max(120.0f, Width * 0.5f - Margin - 20.0f);
	const auto Taken = [&](const float X, const float Y, const float W)
	{
		if (OutBoxes) { OutBoxes->Add(FBox2D(FVector2D(X - 4.0f, Y - 4.0f) * Scale, FVector2D(X + W + 4.0f, Y + GlyphH + 4.0f) * Scale)); }
	};
	for (int32 Index = 0; Index < Right.Num(); ++Index)
	{
		const FHint& Hint = Right[Right.Num() - 1 - Index];
		const float Y = RightBottom - (Index + 1) * RowH;
		const float GlyphW = GlyphWidth(Hint.Key);
		const float LabelMax = FMath::Max(60.0f, ColumnMax - GlyphW - Gap);
		const float RowW = GlyphW + Gap + LabelWidth(Hint.Label, LabelMax);
		const float X = Width - Margin - RowW;
		const FPainter P{Space, Elements, Layer, 0.92f};
		Glyph(P, X, Y, Hint.Key, Hint.Lit);
		Label(HudAbove(P, 6), X + GlyphW + Gap, Y, Hint.Label, Hint.Lit, LabelMax);
		Taken(X, Y, RowW);
	}
	for (int32 Index = 0; Index < Left.Num(); ++Index)
	{
		const FHint& Hint = Left[Left.Num() - 1 - Index];
		const float Y = LeftBottom - (Index + 1) * RowH;
		const FPainter P{Space, Elements, Layer, 0.92f};
		const float GlyphW = Glyph(P, Margin, Y, Hint.Key, Hint.Lit);
		const float LabelMax = FMath::Max(60.0f, ColumnMax - GlyphW - Gap);
		Label(HudAbove(P, 6), Margin + GlyphW + Gap, Y, Hint.Label, Hint.Lit, LabelMax);
		Taken(Margin, Y, GlyphW + Gap + LabelWidth(Hint.Label, LabelMax));
	}
}
