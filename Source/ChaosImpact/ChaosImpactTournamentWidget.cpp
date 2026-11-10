#include "ChaosImpactTournamentWidget.h"

#include "ChaosImpactBallTypes.h"
#include "ChaosImpactCharacterPreview.h"
#include "ChaosImpactCharacterRoster.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactPaint.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactSfx.h"
#include "Engine/LocalPlayer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
	using namespace ChaosImpactPaint;

	constexpr float TwDesignH = 900.0f;
	/** The bracket's area: below the title, above the hints. */
	constexpr float TwBracketTop = 150.0f;
	constexpr float TwBracketBottom = 812.0f;
	constexpr float TwSideMargin = 34.0f;
	const FLinearColor TwCard(0.035f, 0.05f, 0.1f, 0.94f);
	const FLinearColor TwCardDim(0.025f, 0.03f, 0.055f, 0.85f);
	const FLinearColor TwLineDim(0.32f, 0.4f, 0.55f, 0.38f);
	const FLinearColor TwLive(1.0f, 0.2f, 0.28f, 1.0f);
	const FLinearColor TwSilver(0.78f, 0.84f, 0.95f, 1.0f);

	float TwEase(const float T) { return EaseOut(FMath::Clamp(T, 0.0f, 1.0f)); }
	float TwSmooth(const float T) { const float X = FMath::Clamp(T, 0.0f, 1.0f); return X * X * (3.0f - 2.0f * X); }

	/** A line with a soft glow under it. */
	void TwGlowLine(const FPainter& P, const FVector2D& A, const FVector2D& B, const FLinearColor& Color, const float Width, const float Glow)
	{
		if (Glow > 0.0f)
		{
			P.Line(A, B, WithAlpha(Color, Color.A * 0.18f * Glow), Width * 4.0f);
		}
		FPainter{P.Geometry, P.Elements, P.Layer + 1, P.Alpha}.Line(A, B, Color, Width);
	}

	/** Part of a polyline (0..1 of its length), for lines that draw themselves in. */
	void TwPartialPath(const FPainter& P, const TArray<FVector2D>& Points, const float Amount, const FLinearColor& Color, const float Width,
		const float Glow)
	{
		float Total = 0.0f;
		for (int32 Index = 1; Index < Points.Num(); ++Index)
		{
			Total += static_cast<float>((Points[Index] - Points[Index - 1]).Size());
		}
		float Left = Total * FMath::Clamp(Amount, 0.0f, 1.0f);
		for (int32 Index = 1; Index < Points.Num() && Left > 0.0f; ++Index)
		{
			const FVector2D A = Points[Index - 1];
			const FVector2D B = Points[Index];
			const float Length = static_cast<float>((B - A).Size());
			const FVector2D End = Length <= Left ? B : A + (B - A) * (Left / FMath::Max(Length, 0.001f));
			TwGlowLine(P, A, End, Color, Width, Glow);
			Left -= Length;
		}
	}

	FSlateBrush TwMakePictureBrush(AChaosImpactCharacterPreview* Stage)
	{
		FSlateBrush Brush;
		if (Stage && Stage->GetPicture())
		{
			Brush.SetResourceObject(Stage->GetPicture());
		}
		Brush.ImageSize = FVector2D(AChaosImpactCharacterPreview::PictureWidth, AChaosImpactCharacterPreview::PictureHeight);
		Brush.DrawAs = ESlateBrushDrawType::Image;
		return Brush;
	}

	/** A trophy cup drawn from shapes, standing on Base, Size tall. */
	void TwPaintTrophy(const FPainter& P, const FVector2D& Base, const float Size, const float Glint)
	{
		const float S = Size / 100.0f;
		const FHudFill GoldFill = MakeFill(FLinearColor(1.0f, 0.93f, 0.55f, 1.0f), FLinearColor(0.82f, 0.45f, 0.04f, 1.0f));
		const auto At = [&Base, S](const float X, const float Y) { return Base + FVector2D(X * S, -Y * S); };
		// Base, stem, cup, handles.
		const TArray<FVector2D> Plinth{At(-34, 0), At(34, 0), At(30, 12), At(-30, 12)};
		const TArray<FVector2D> Stem{At(-8, 12), At(8, 12), At(5, 34), At(-5, 34)};
		TArray<FVector2D> Cup;
		for (int32 Step = 0; Step <= 12; ++Step)
		{
			const float A = UE_PI * Step / 12.0f;
			Cup.Add(At(-40.0f * FMath::Cos(A), 70.0f - 36.0f * FMath::Sin(A)));
		}
		Cup.Add(At(40, 100));
		Cup.Add(At(-40, 100));
		HudPoly(P, HudInflate(Plinth, 3.0f), SolidFill(Navy), 1);
		HudPoly(P, HudInflate(Stem, 3.0f), SolidFill(Navy), 1);
		HudPoly(P, HudInflate(Cup, 3.0f), SolidFill(Navy), 1);
		const FPainter Fill{P.Geometry, P.Elements, P.Layer + 1, P.Alpha};
		HudPoly(Fill, Plinth, GoldFill, 3);
		HudPoly(Fill, Stem, GoldFill, 3);
		HudPoly(Fill, Cup, GoldFill, 6);
		for (const float Side : {-1.0f, 1.0f})
		{
			Fill.Arc(At(Side * 42.0f, 74.0f), 15.0f * S, Side < 0 ? 90.0f : -90.0f, Side < 0 ? 270.0f : 90.0f, FLinearColor(0.95f, 0.66f, 0.12f, 1.0f),
				5.0f * S);
		}
		// A glint running over the cup.
		const float X = FMath::Lerp(-30.0f, 30.0f, Glint);
		FPainter{P.Geometry, P.Elements, P.Layer + 2, P.Alpha * (1.0f - FMath::Abs(Glint * 2.0f - 1.0f))}.Line(At(X - 8, 92), At(X + 6, 48),
			WithAlpha(Paper, 0.85f), 4.0f * S);
	}
}

// ---------------------------------------------------------------------------------------------------- reading

AChaosImpactPlayerController* UChaosImpactTournamentWidget::GetOwner() const
{
	return Cast<AChaosImpactPlayerController>(GetOwningPlayer());
}

const AChaosImpactGameState* UChaosImpactTournamentWidget::GetMatch() const
{
	return GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
}

const AChaosImpactPlayerState* UChaosImpactTournamentWidget::GetMe() const
{
	const APlayerController* Owner = GetOwningPlayer();
	return Owner ? Owner->GetPlayerState<AChaosImpactPlayerState>() : nullptr;
}

int32 UChaosImpactTournamentWidget::GetMyEntrant() const
{
	const AChaosImpactGameState* Match = GetMatch();
	return Match ? Match->Tournament.FindEntrant(GetMe()) : INDEX_NONE;
}

int32 UChaosImpactTournamentWidget::GetMyArena() const
{
	const AChaosImpactPlayerState* Me = GetMe();
	return Me && (Me->TournamentRole == EChaosImpactTournamentRole::Competitor || Me->TournamentRole == EChaosImpactTournamentRole::Spectator)
		? Me->TournamentArena : INDEX_NONE;
}

double UChaosImpactTournamentWidget::GetNow() const
{
	const AChaosImpactGameState* Match = GetMatch();
	return Match ? Match->GetServerWorldTimeSeconds() : 0.0;
}

FLinearColor UChaosImpactTournamentWidget::EntrantColor(const int32 Entrant) const
{
	const AChaosImpactGameState* Match = GetMatch();
	if (!Match || !Match->Tournament.Entrants.IsValidIndex(Entrant))
	{
		return Muted;
	}
	return ChaosImpactRoster::GetColourSwatch(Match->Tournament.Entrants[Entrant].Colour);
}

// ---------------------------------------------------------------------------------------------------- panel

void UChaosImpactTournamentWidget::OpenPanel(const EPanel InPanel)
{
	if (Panel == InPanel)
	{
		return;
	}
	Panel = InPanel;
	PanelOpenedAt = FPlatformTime::Seconds();
	MenuCursor = 0;
	WatchCursor = 0;
	if (InPanel == EPanel::Bracket && BracketCursor == INDEX_NONE)
	{
		// On this player's own match, else the first being played.
		const AChaosImpactGameState* Match = GetMatch();
		const int32 Mine = Match ? Match->Tournament.FindCurrentMatch(GetMyEntrant()) : INDEX_NONE;
		const TArray<int32> LiveMatches = GetLiveMatches();
		BracketCursor = Mine != INDEX_NONE ? Mine : !LiveMatches.IsEmpty() ? LiveMatches[0] : INDEX_NONE;
	}
	if (AChaosImpactPlayerController* Owner = GetOwner())
	{
		Owner->SetTournamentPanelOpen(Panel != EPanel::None);
	}
}

void UChaosImpactTournamentWidget::ClosePanel()
{
	Panel = EPanel::None;
	if (AChaosImpactPlayerController* Owner = GetOwner())
	{
		Owner->SetTournamentPanelOpen(false);
	}
}

void UChaosImpactTournamentWidget::NativeDestruct()
{
	for (AChaosImpactCharacterPreview* Stage : Portraits)
	{
		if (IsValid(Stage))
		{
			Stage->Destroy();
		}
	}
	Portraits.Reset();
	if (AChaosImpactPlayerController* Owner = GetOwner())
	{
		Owner->SetTournamentPanelOpen(false);
	}
	Super::NativeDestruct();
}

void UChaosImpactTournamentWidget::ChooseWait(const EChaosImpactTournamentWait Wait, const int32 WatchMatch)
{
	if (AChaosImpactPlayerController* Owner = GetOwner())
	{
		Owner->ServerSetTournamentWait(Wait, WatchMatch);
	}
}

TArray<int32> UChaosImpactTournamentWidget::GetLiveMatches() const
{
	TArray<int32> Out;
	if (const AChaosImpactGameState* Match = GetMatch())
	{
		for (int32 Index = 0; Index < Match->Tournament.Matches.Num(); ++Index)
		{
			const FChaosImpactTournamentMatch& Each = Match->Tournament.Matches[Index];
			if (Each.State == EChaosImpactTournamentMatchState::Live && Each.Arena != INDEX_NONE)
			{
				Out.Add(Index);
			}
		}
	}
	return Out;
}

TArray<int32> UChaosImpactTournamentWidget::GetPickableMatches() const
{
	TArray<int32> Out;
	if (const AChaosImpactGameState* Match = GetMatch())
	{
		for (int32 Index = 0; Index < Match->Tournament.Matches.Num(); ++Index)
		{
			const FChaosImpactTournamentMatch& Each = Match->Tournament.Matches[Index];
			if (Each.Group == 0 && (Each.Entrants[0] != INDEX_NONE || Each.Entrants[1] != INDEX_NONE))
			{
				Out.Add(Index);
			}
		}
	}
	return Out;
}

// ---------------------------------------------------------------------------------------------------- tick and input

void UChaosImpactTournamentWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	LocalTime += InDeltaTime;
	const AChaosImpactGameState* Match = GetMatch();
	if (!Match || !Match->IsTournament())
	{
		return;
	}
	const FChaosImpactTournamentState& Tournament = Match->Tournament;
	// A tournament starting opens on its bracket; so does the champion's moment.
	if (!FMath::IsNearlyEqual(Tournament.StartedAt, SeenStartedAt))
	{
		SeenStartedAt = Tournament.StartedAt;
		bChampionShown = false;
		BracketCursor = INDEX_NONE;
		OpenPanel(EPanel::Bracket);
	}
	if (Tournament.bFinished && !bChampionShown)
	{
		bChampionShown = true;
		bChampionDismissed = false;
		OpenPanel(EPanel::Bracket);
	}
	// Called: whatever was open closes for the call.
	const int32 Mine = Tournament.FindCurrentMatch(GetMyEntrant());
	// (ローカルトーナメント 1試合ずつ: this player becomes the match's player only as it begins.)
	if (Tournament.Matches.IsValidIndex(Mine) && (Tournament.Matches[Mine].State == EChaosImpactTournamentMatchState::Calling
		|| Tournament.Matches[Mine].State == EChaosImpactTournamentMatchState::Live) && SeenCalledMatch != Mine)
	{
		SeenCalledMatch = Mine;
		ClosePanel();
		ChaosImpactSfx::Play2D(GetOwningPlayer(), EChaosImpactSfx::UiStart);
	}
	HandleInput();
	UpdatePortraits(InDeltaTime);
#if !UE_BUILD_SHIPPING
	// Development (-CITourBracket): the bracket comes back up whenever nothing else is shown (screenshots of it).
	static const bool bDevBracket = FParse::Param(FCommandLine::Get(), TEXT("CITourBracket"));
	const AChaosImpactPlayerState* Me = GetMe();
	if (bDevBracket && Panel == EPanel::None && Me && Me->TournamentRole != EChaosImpactTournamentRole::Competitor
		&& !(Tournament.Matches.IsValidIndex(Mine) && Tournament.Matches[Mine].State == EChaosImpactTournamentMatchState::Calling))
	{
		OpenPanel(EPanel::Bracket);
	}
#endif
}

bool UChaosImpactTournamentWidget::WasPressed(std::initializer_list<FKey> Keys) const
{
	const APlayerController* Owner = GetOwningPlayer();
	if (!Owner)
	{
		return false;
	}
	for (const FKey& Key : Keys)
	{
		if (Owner->WasInputKeyJustPressed(Key))
		{
			return true;
		}
	}
	return false;
}

void UChaosImpactTournamentWidget::HandleInput()
{
	APlayerController* Owner = GetOwningPlayer();
	const AChaosImpactGameState* Match = GetMatch();
	const AChaosImpactPlayerState* Me = GetMe();
	if (!Owner || !Match || !Me)
	{
		return;
	}
	const FChaosImpactTournamentState& Tournament = Match->Tournament;
	// The champion's screen: Enter / A (or a click) ends it for everyone when it is the host's (or a local tournament's);
	// anyone else just puts it away.
	if (Tournament.bFinished && !bChampionDismissed && GetNow() - Tournament.FinishedAt > 1.5
		&& WasPressed({EKeys::Enter, EKeys::SpaceBar, EKeys::Gamepad_FaceButton_Bottom, EKeys::LeftMouseButton}))
	{
		bChampionDismissed = true;
		if (Me->bRoomHost || Match->bLocalTournament)
		{
			if (AChaosImpactPlayerController* Controller = GetOwner()) { Controller->ServerSkipTournamentResults(); }
		}
		ClosePanel();
		ChaosImpactSfx::Play2D(Owner, EChaosImpactSfx::UiSelect);
		return;
	}
	// The host's choice for two CPUs comes first.
	if (Me->bRoomHost && Tournament.AskingMatch != INDEX_NONE && !Tournament.bFinished)
	{
		if (WasPressed({EKeys::Left, EKeys::A, EKeys::Gamepad_DPad_Left})) { AskCursor = (AskCursor + 2) % 3; }
		if (WasPressed({EKeys::Right, EKeys::D, EKeys::Gamepad_DPad_Right})) { AskCursor = (AskCursor + 1) % 3; }
		if (WasPressed({EKeys::Enter, EKeys::SpaceBar, EKeys::Gamepad_FaceButton_Bottom}))
		{
			if (AChaosImpactPlayerController* Controller = GetOwner())
			{
				Controller->ServerAnswerTournamentCPUMatch(AskCursor);
			}
			ChaosImpactSfx::Play2D(Owner, EChaosImpactSfx::UiSelect);
		}
		return;
	}
	// Playing one's own match: no menu (it would hold the character still in the middle of the fight); once it is over,
	// it opens again. Anything open when the match was called has been closed already.
	{
		const int32 MineNow = Tournament.FindCurrentMatch(GetMyEntrant());
		if (Me->TournamentRole == EChaosImpactTournamentRole::Competitor
			|| (Tournament.Matches.IsValidIndex(MineNow) && Tournament.Matches[MineNow].IsPlaying()))
		{
			if (Panel != EPanel::None) { ClosePanel(); }
			return;
		}
	}
	const bool bToggle = WasPressed({EKeys::Tab, EKeys::Gamepad_Special_Left});
	if (Panel == EPanel::None)
	{
		if (bToggle)
		{
			OpenPanel(EPanel::Menu);
			ChaosImpactSfx::Play2D(Owner, EChaosImpactSfx::UiSelect);
		}
		return;
	}
	if (bToggle)
	{
		ClosePanel();
		ChaosImpactSfx::Play2D(Owner, EChaosImpactSfx::UiBack);
		return;
	}
	int32 Columns = 0;
	int32 Rows = 0;
	if (WasPressed({EKeys::Up, EKeys::W, EKeys::Gamepad_DPad_Up})) { Rows = -1; }
	else if (WasPressed({EKeys::Down, EKeys::S, EKeys::Gamepad_DPad_Down})) { Rows = 1; }
	else if (WasPressed({EKeys::Left, EKeys::A, EKeys::Gamepad_DPad_Left})) { Columns = -1; }
	else if (WasPressed({EKeys::Right, EKeys::D, EKeys::Gamepad_DPad_Right})) { Columns = 1; }
	// The stick: a step, then repeating while held over.
	const float StickX = Owner->GetInputAnalogKeyState(EKeys::Gamepad_LeftX);
	const float StickY = Owner->GetInputAnalogKeyState(EKeys::Gamepad_LeftY);
	if (FMath::Max(FMath::Abs(StickX), FMath::Abs(StickY)) > 0.6f)
	{
		const double Now = FPlatformTime::Seconds();
		if (!bStickHeld || Now >= NextStickAt)
		{
			NextStickAt = Now + (bStickHeld ? 0.12 : 0.38);
			bStickHeld = true;
			if (FMath::Abs(StickX) > FMath::Abs(StickY)) { Columns = StickX > 0.0f ? 1 : -1; }
			else { Rows = StickY > 0.0f ? -1 : 1; }
		}
	}
	else
	{
		bStickHeld = false;
	}
	if (Columns != 0 || Rows != 0)
	{
		MoveCursor(Columns, Rows);
	}
	if (WasPressed({EKeys::Enter, EKeys::SpaceBar, EKeys::Gamepad_FaceButton_Bottom}))
	{
		Confirm();
	}
	else if (WasPressed({EKeys::BackSpace, EKeys::Gamepad_FaceButton_Right}))
	{
		Back();
	}
}

void UChaosImpactTournamentWidget::MoveCursor(const int32 Columns, const int32 Rows)
{
	const AChaosImpactGameState* Match = GetMatch();
	if (!Match)
	{
		return;
	}
	if (Panel == EPanel::Menu)
	{
		MenuCursor = (MenuCursor + Rows + Columns + 4) % 4;
	}
	else if (Panel == EPanel::Watch)
	{
		const int32 Count = GetLiveMatches().Num() + 1;
		WatchCursor = (WatchCursor + Rows + Columns + Count) % Count;
	}
	else if (Panel == EPanel::Bracket)
	{
		// To the nearest pickable match that way, on the screen.
		const TArray<int32> Pickable = GetPickableMatches();
		if (Pickable.IsEmpty())
		{
			return;
		}
		if (!Pickable.Contains(BracketCursor))
		{
			BracketCursor = Pickable[0];
			return;
		}
		const FVector2D From = MatchBox(BracketCursor).GetCenter();
		const FVector2D Way(Columns, Rows);
		int32 Best = INDEX_NONE;
		double BestScore = TNumericLimits<double>::Max();
		for (const int32 Candidate : Pickable)
		{
			if (Candidate == BracketCursor)
			{
				continue;
			}
			const FVector2D Offset = MatchBox(Candidate).GetCenter() - From;
			const double Along = FVector2D::DotProduct(Offset, Way);
			if (Along <= 1.0)
			{
				continue;
			}
			const double Across = FMath::Abs(FVector2D::CrossProduct(Offset, Way));
			const double Score = Along + Across * 2.5;
			if (Score < BestScore)
			{
				BestScore = Score;
				Best = Candidate;
			}
		}
		BracketCursor = Best != INDEX_NONE ? Best : BracketCursor;
	}
	ChaosImpactSfx::Play2D(GetOwningPlayer(), EChaosImpactSfx::UiMove);
}

void UChaosImpactTournamentWidget::Confirm()
{
	const AChaosImpactGameState* Match = GetMatch();
	const AChaosImpactPlayerState* Me = GetMe();
	APlayerController* Owner = GetOwningPlayer();
	if (!Match || !Me || !Owner)
	{
		return;
	}
	const auto Say = [this](const TCHAR* Text) { Toast = Text; ToastAt = FPlatformTime::Seconds(); };
	if (Panel == EPanel::Menu)
	{
		if (MenuCursor == 0)
		{
			if (Me->bSpectating)
			{
				Say(TEXT("観戦で参加しているので練習はできません"));
				ChaosImpactSfx::Play2D(Owner, EChaosImpactSfx::UiDeny);
				return;
			}
			ChooseWait(EChaosImpactTournamentWait::Practice);
			ClosePanel();
		}
		else if (MenuCursor == 1)
		{
			if (GetLiveMatches().IsEmpty())
			{
				Say(TEXT("いま行われている試合はありません"));
				ChaosImpactSfx::Play2D(Owner, EChaosImpactSfx::UiDeny);
				return;
			}
			OpenPanel(EPanel::Watch);
		}
		else if (MenuCursor == 2)
		{
			OpenPanel(EPanel::Bracket);
		}
		else
		{
			ClosePanel();
		}
		ChaosImpactSfx::Play2D(Owner, EChaosImpactSfx::UiSelect);
		return;
	}
	if (Panel == EPanel::Watch)
	{
		const TArray<int32> LiveMatches = GetLiveMatches();
		ChooseWait(EChaosImpactTournamentWait::Spectate, LiveMatches.IsValidIndex(WatchCursor) ? LiveMatches[WatchCursor] : INDEX_NONE);
		ClosePanel();
		ChaosImpactSfx::Play2D(Owner, EChaosImpactSfx::UiSelect);
		return;
	}
	if (Panel == EPanel::Bracket)
	{
		if (Match->Tournament.Matches.IsValidIndex(BracketCursor)
			&& Match->Tournament.Matches[BracketCursor].State == EChaosImpactTournamentMatchState::Live
			&& Match->Tournament.Matches[BracketCursor].Arena != INDEX_NONE)
		{
			ChooseWait(EChaosImpactTournamentWait::Spectate, BracketCursor);
			ClosePanel();
			ChaosImpactSfx::Play2D(Owner, EChaosImpactSfx::UiSelect);
			return;
		}
		Say(TEXT("試合中（LIVE）の試合をえらぶと観戦できます"));
		ChaosImpactSfx::Play2D(Owner, EChaosImpactSfx::UiDeny);
	}
}

void UChaosImpactTournamentWidget::Back()
{
	if (Panel == EPanel::Watch || Panel == EPanel::Bracket)
	{
		OpenPanel(EPanel::Menu);
	}
	else
	{
		ClosePanel();
	}
	ChaosImpactSfx::Play2D(GetOwningPlayer(), EChaosImpactSfx::UiBack);
}

FReply UChaosImpactTournamentWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (Panel == EPanel::None)
	{
		return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
	}
	const FVector2D Point = ToDesign(InGeometry, InMouseEvent.GetScreenSpacePosition());
	if (Panel == EPanel::Menu)
	{
		for (int32 Index = 0; Index < 4; ++Index)
		{
			MenuCursor = MenuRect(Index).IsInside(Point) ? Index : MenuCursor;
		}
	}
	else if (Panel == EPanel::Watch)
	{
		for (int32 Index = 0; Index <= GetLiveMatches().Num(); ++Index)
		{
			WatchCursor = WatchRect(Index).IsInside(Point) ? Index : WatchCursor;
		}
	}
	else if (Panel == EPanel::Bracket)
	{
		for (const int32 Index : GetPickableMatches())
		{
			BracketCursor = MatchBox(Index).IsInside(Point) ? Index : BracketCursor;
		}
	}
	return FReply::Handled();
}

FReply UChaosImpactTournamentWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	const AChaosImpactGameState* Match = GetMatch();
	const AChaosImpactPlayerState* Me = GetMe();
	const FVector2D Point = ToDesign(InGeometry, InMouseEvent.GetScreenSpacePosition());
	if (Match && Me && Me->bRoomHost && Match->Tournament.AskingMatch != INDEX_NONE)
	{
		for (int32 Index = 0; Index < 3; ++Index)
		{
			if (AskRect(Index).IsInside(Point))
			{
				AskCursor = Index;
				if (AChaosImpactPlayerController* Owner = GetOwner())
				{
					Owner->ServerAnswerTournamentCPUMatch(Index);
				}
				return FReply::Handled();
			}
		}
	}
	if (Panel == EPanel::None)
	{
		return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
	}
	NativeOnMouseMove(InGeometry, InMouseEvent);
	if (Panel == EPanel::Bracket)
	{
		for (const int32 Index : GetPickableMatches())
		{
			if (MatchBox(Index).IsInside(Point))
			{
				BracketCursor = Index;
				Confirm();
				return FReply::Handled();
			}
		}
		return FReply::Handled();
	}
	if (Panel == EPanel::Menu)
	{
		for (int32 Index = 0; Index < 4; ++Index)
		{
			if (MenuRect(Index).IsInside(Point)) { Confirm(); break; }
		}
	}
	else if (Panel == EPanel::Watch)
	{
		for (int32 Index = 0; Index <= GetLiveMatches().Num(); ++Index)
		{
			if (WatchRect(Index).IsInside(Point)) { Confirm(); break; }
		}
	}
	return FReply::Handled();
}

// ---------------------------------------------------------------------------------------------------- portraits

FString UChaosImpactTournamentWidget::PortraitKey(const int32 Entrant) const
{
	const AChaosImpactGameState* Match = GetMatch();
	if (!Match || !Match->Tournament.Entrants.IsValidIndex(Entrant))
	{
		return FString();
	}
	const FChaosImpactTournamentEntrant& Who = Match->Tournament.Entrants[Entrant];
	FString Key = FString::Printf(TEXT("%d_%d_"), Who.Character, Who.Colour);
	for (const uint8 Byte : Who.CustomColours)
	{
		Key += FString::Printf(TEXT("%02x"), Byte);
	}
	return Key;
}

void UChaosImpactTournamentWidget::UpdatePortraits(const float DeltaSeconds)
{
	const AChaosImpactGameState* Match = GetMatch();
	UWorld* World = GetWorld();
	if (!Match || !World)
	{
		return;
	}
	++PortraitFrame;
	// Each look once (two a frame at most), filmed now and then for its first seconds while its textures come in.
	int32 Made = 0;
	const int32 LocalIndex = GetOwningLocalPlayer() ? GetOwningLocalPlayer()->GetLocalPlayerIndex() : 0;
	for (int32 Entrant = 0; Entrant < Match->Tournament.Entrants.Num() && Made < 2; ++Entrant)
	{
		const FString Key = PortraitKey(Entrant);
		if (Key.IsEmpty() || PortraitIndex.Contains(Key))
		{
			continue;
		}
		const FChaosImpactTournamentEntrant& Who = Match->Tournament.Entrants[Entrant];
		FActorSpawnParameters Parameters;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Parameters.ObjectFlags |= RF_Transient;
		const int32 Index = Portraits.Num();
		AChaosImpactCharacterPreview* Portrait = World->SpawnActor<AChaosImpactCharacterPreview>(AChaosImpactCharacterPreview::StaticClass(),
			FVector(Index * 1600.0f, -128000.0f - LocalIndex * 6000.0f, 40000.0f), FRotator::ZeroRotator, Parameters);
		if (Portrait)
		{
			Portrait->ShowLoadout(Who.Character, Who.Colour, Paper);
			if (!Who.CustomColours.IsEmpty())
			{
				Portrait->ShowCustomColours(Who.CustomColours);
			}
			Portrait->UseAsPortrait();
		}
		Portraits.Add(Portrait);
		PortraitBrushes.Add(TwMakePictureBrush(Portrait));
		PortraitBornAt.Add(FPlatformTime::Seconds());
		PortraitCharacter.Add(Who.Character);
		PortraitIndex.Add(Key, Index);
		++Made;
	}
	const double Now = FPlatformTime::Seconds();
	for (int32 Index = 0; Index < Portraits.Num(); ++Index)
	{
		AChaosImpactCharacterPreview* Stage = Portraits[Index];
		if (IsValid(Stage) && Now - PortraitBornAt[Index] < 10.0 && (PortraitFrame + Index) % 6 == 0)
		{
			Stage->Animate(0.1f);
			Stage->FilmNow();
			PortraitBrushes[Index] = TwMakePictureBrush(Stage);
		}
	}
}

void UChaosImpactTournamentWidget::PaintPortrait(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer,
	const int32 Entrant, const FBox2D& Box, const float Alpha) const
{
	const int32* Index = PortraitIndex.Find(PortraitKey(Entrant));
	if (!Index || !PortraitBrushes.IsValidIndex(*Index) || !PortraitBrushes[*Index].GetResourceObject())
	{
		return;
	}
	FSlateBrush Face = PortraitBrushes[*Index];
	const int32 Character = PortraitCharacter[*Index];
	const EChaosImpactModelKind Kind = ChaosImpactRoster::Get(Character).Kind;
	const bool bWhole = Kind == EChaosImpactModelKind::Authored || Kind == EChaosImpactModelKind::Handstand;
	Face.SetUVRegion(bWhole ? FBox2f(FVector2f(0.12f, 0.16f), FVector2f(0.88f, 0.62f))
		: Character == ChaosImpactRoster::CapIndex ? FBox2f(FVector2f(0.14f, 0.06f), FVector2f(0.86f, 0.5f))
		: FBox2f(FVector2f(0.2f, 0.04f), FVector2f(0.8f, 0.44f)));
	FSlateDrawElement::MakeBox(Elements, Layer, Design.ToPaintGeometry(FVector2f(Box.GetSize()), FSlateLayoutTransform(FVector2f(Box.Min))),
		&Face, ESlateDrawEffect::None, FLinearColor(1.0f, 1.0f, 1.0f, Alpha));
}

// ---------------------------------------------------------------------------------------------------- layout

FGeometry UChaosImpactTournamentWidget::MakeDesign(const FGeometry& Allotted) const
{
	// 900 high and as wide as this player's part of the screen allows (a half of a split screen is narrower).
	const FVector2f Size = Allotted.GetLocalSize();
	DesignWidth = FMath::Clamp(Size.Y > 1.0f ? TwDesignH * Size.X / Size.Y : 1600.0f, 1000.0f, 1600.0f);
	const float Scale = FMath::Min(Size.X / DesignWidth, Size.Y / TwDesignH);
	return Allotted.MakeChild(FVector2f(DesignWidth, TwDesignH),
		FSlateLayoutTransform(Scale, FVector2f((Size.X - DesignWidth * Scale) * 0.5f, (Size.Y - TwDesignH * Scale) * 0.5f)));
}

FVector2D UChaosImpactTournamentWidget::ToDesign(const FGeometry& Allotted, const FVector2D& ScreenPoint) const
{
	MakeDesign(Allotted);
	const FVector2f Local = Allotted.AbsoluteToLocal(FVector2f(ScreenPoint));
	const FVector2f Size = Allotted.GetLocalSize();
	const float Scale = FMath::Min(Size.X / DesignWidth, Size.Y / TwDesignH);
	return FVector2D((Local.X - (Size.X - DesignWidth * Scale) * 0.5f) / Scale, (Local.Y - (Size.Y - TwDesignH * Scale) * 0.5f) / Scale);
}

float UChaosImpactTournamentWidget::CardWidth() const
{
	const AChaosImpactGameState* Match = GetMatch();
	const int32 Size = Match ? Match->Tournament.BracketSize : 8;
	const float Narrow = FMath::Clamp(DesignWidth / 1600.0f, 0.78f, 1.0f);
	return (Size >= 32 ? 148.0f : Size >= 16 ? 172.0f : Size >= 8 ? 204.0f : 230.0f) * Narrow;
}

float UChaosImpactTournamentWidget::CardHeight() const
{
	const AChaosImpactGameState* Match = GetMatch();
	const int32 Size = Match ? Match->Tournament.BracketSize : 8;
	return Size >= 32 ? 34.0f : Size >= 16 ? 40.0f : 50.0f;
}

float UChaosImpactTournamentWidget::CardY(const int32 InRound, const int32 MatchSlot, const int32 Side) const
{
	const AChaosImpactGameState* Match = GetMatch();
	if (!Match)
	{
		return TwBracketTop;
	}
	const FChaosImpactTournamentState& Tournament = Match->Tournament;
	if (InRound >= Tournament.Rounds)
	{
		// The final's two cards, side by side in the middle.
		return (TwBracketTop + TwBracketBottom) * 0.5f - CardHeight() * 0.5f + 40.0f;
	}
	if (InRound <= 1)
	{
		// The first round, evenly down each half.
		const int32 PerHalf = FMath::Max(Tournament.MatchesInRound(1) / 2, 1);
		const int32 Row = (MatchSlot % PerHalf) * 2 + Side;
		const float Step = (TwBracketBottom - TwBracketTop) / (PerHalf * 2);
		return TwBracketTop + Step * (Row + 0.5f) - CardHeight() * 0.5f;
	}
	// Later rounds: halfway between the two cards of the match that sends this one.
	const int32 Feeder = MatchSlot * 2 + Side;
	return (CardY(InRound - 1, Feeder, 0) + CardY(InRound - 1, Feeder, 1)) * 0.5f;
}

FVector2D UChaosImpactTournamentWidget::CardAt(const int32 InRound, const int32 MatchSlot, const int32 Side, bool* bOutRight) const
{
	const AChaosImpactGameState* Match = GetMatch();
	const int32 Rounds = Match ? Match->Tournament.Rounds : 1;
	const float W = CardWidth();
	if (InRound >= Rounds)
	{
		if (bOutRight) { *bOutRight = Side == 1; }
		return FVector2D(Side == 0 ? DesignWidth * 0.5f - W - 22.0f : DesignWidth * 0.5f + 22.0f, CardY(InRound, MatchSlot, Side));
	}
	const int32 Half = Match ? FMath::Max(Match->Tournament.MatchesInRound(InRound) / 2, 1) : 1;
	const bool bRight = MatchSlot >= Half;
	if (bOutRight) { *bOutRight = bRight; }
	// Rounds 1 .. Rounds-1 from the edge toward the middle.
	const float FinalLeft = DesignWidth * 0.5f - W - 22.0f;
	const float Span = Rounds > 2 ? (FinalLeft - 40.0f - W - TwSideMargin) / (Rounds - 2) : 0.0f;
	const float X = TwSideMargin + (InRound - 1) * Span;
	return FVector2D(bRight ? DesignWidth - X - W : X, CardY(InRound, MatchSlot, Side));
}

FBox2D UChaosImpactTournamentWidget::MatchBox(const int32 MatchIndex) const
{
	const AChaosImpactGameState* Match = GetMatch();
	if (!Match || !Match->Tournament.Matches.IsValidIndex(MatchIndex))
	{
		return FBox2D(FVector2D::ZeroVector, FVector2D::ZeroVector);
	}
	const FChaosImpactTournamentMatch& Each = Match->Tournament.Matches[MatchIndex];
	const FVector2D A = CardAt(Each.Round, Each.Slot, 0);
	const FVector2D B = CardAt(Each.Round, Each.Slot, 1);
	const FVector2D Size(CardWidth(), CardHeight());
	return FBox2D(FVector2D(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y)), FVector2D(FMath::Max(A.X, B.X) + Size.X, FMath::Max(A.Y, B.Y) + Size.Y));
}

FBox2D UChaosImpactTournamentWidget::MenuRect(const int32 Index) const
{
	const FVector2D At(DesignWidth * 0.5f - 240.0f, 300.0f + Index * 104.0f);
	return FBox2D(At, At + FVector2D(480.0f, Index == 3 ? 66.0f : 90.0f));
}

FBox2D UChaosImpactTournamentWidget::WatchRect(const int32 Index) const
{
	const float W = FMath::Min(660.0f, DesignWidth - 60.0f);
	const FVector2D At(DesignWidth * 0.5f - W * 0.5f, 250.0f + Index * 74.0f);
	return FBox2D(At, At + FVector2D(W, 64.0f));
}

FBox2D UChaosImpactTournamentWidget::AskRect(const int32 Index) const
{
	const FVector2D At(DesignWidth * 0.5f - 280.0f + Index * 190.0f, 520.0f);
	return FBox2D(At, At + FVector2D(176.0f, 64.0f));
}

// ---------------------------------------------------------------------------------------------------- painting

FString UChaosImpactTournamentWidget::DescribeMyStatus() const
{
	const AChaosImpactGameState* Match = GetMatch();
	const AChaosImpactPlayerState* Me = GetMe();
	if (!Match || !Me)
	{
		return FString();
	}
	const FChaosImpactTournamentState& Tournament = Match->Tournament;
	const int32 Mine = GetMyEntrant();
	if (Mine == INDEX_NONE)
	{
		return TEXT("観戦中");
	}
	const FChaosImpactTournamentEntrant& Who = Tournament.Entrants[Mine];
	if (Who.Place == 1)
	{
		return TEXT("優勝！");
	}
	if (Who.Place > 1)
	{
		// 5th to 8th are the last eight, 3rd and 4th the last four.
		return Who.Place == 2 ? FString(TEXT("準優勝")) : Tournament.Settings.bPlacementMatches ? FString::Printf(TEXT("%d位"), Who.Place)
			: FString::Printf(TEXT("ベスト%d"), (Who.Place - 1) * 2);
	}
	const int32 Next = Tournament.FindCurrentMatch(Mine);
	if (!Tournament.Matches.IsValidIndex(Next))
	{
		return FString();
	}
	const FChaosImpactTournamentMatch& Each = Tournament.Matches[Next];
	const int32 Opponent = Each.Entrants[1 - FMath::Max(Each.SideOf(Mine), 0)];
	const FString Against = Tournament.Entrants.IsValidIndex(Opponent) ? FString::Printf(TEXT("vs %s"), *Tournament.Entrants[Opponent].Name)
		: FString(TEXT("相手が決まるのを待っています"));
	if (Each.IsPlaying())
	{
		return FString::Printf(TEXT("試合中  %s"), *Against);
	}
	if (Each.Round > Tournament.Round)
	{
		return FString::Printf(TEXT("%s 進出  ・  %s"), *Tournament.GetMatchName(Next), *Against);
	}
	// One at a time: how many come before.
	int32 Before = 0;
	if (!Tournament.IsSimultaneous())
	{
		for (int32 Index = 0; Index < Next; ++Index)
		{
			Before += Tournament.Matches[Index].Round == Each.Round && !Tournament.Matches[Index].IsOver() ? 1 : 0;
		}
	}
	return Before > 0 ? FString::Printf(TEXT("つぎの試合まであと%d試合  ・  %s"), Before, *Against)
		: FString::Printf(TEXT("まもなく出番  ・  %s"), *Against);
}

int32 UChaosImpactTournamentWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, const int32 LayerId, const FWidgetStyle& InWidgetStyle, const bool bParentEnabled) const
{
	int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	const AChaosImpactGameState* Match = GetMatch();
	if (Match && !Match->IsTournament() && Match->bPublicTournament && Match->TournamentStartsAt > 0.0)
	{
		PaintPublicWait(MakeDesign(AllottedGeometry), OutDrawElements, Layer + 1);
		return Layer + 10;
	}
	if (!Match || !Match->IsTournament())
	{
		return Layer;
	}
	const FChaosImpactTournamentState& Tournament = Match->Tournament;
	const FGeometry Design = MakeDesign(AllottedGeometry);
	const FVector2f Size = AllottedGeometry.GetLocalSize();
	const int32 MyArena = GetMyArena();
	const int32 Mine = Tournament.FindCurrentMatch(GetMyEntrant());
	const bool bCalled = Tournament.Matches.IsValidIndex(Mine) && Tournament.Matches[Mine].State == EChaosImpactTournamentMatchState::Calling;

	// In an arena the duel's own screen shows the match (the match announcer); here only the way to the menu.
	const bool bInArena = MyArena != INDEX_NONE && Match->TournamentArenas.IsValidIndex(MyArena) && Match->TournamentArenas[MyArena].Match != INDEX_NONE;
	// (Not during the match's opening: like a duel's, it has the screen to itself.)
	const bool bOpening = bInArena && Match->TournamentArenas[MyArena].Duel.Step == EChaosImpactDuelStep::Opening;
	if (bInArena && Panel == EPanel::None && !bOpening && GetMe() && GetMe()->TournamentRole != EChaosImpactTournamentRole::Competitor)
	{
		const FPainter Hint{Design, OutDrawElements, Layer + 20};
		// (Top left, clear of the player's own HP and name at the bottom.)
		RoundedBox(Hint, 24.0f, 96.0f, 330.0f, 38.0f, 12.0f, FLinearColor(0.008f, 0.012f, 0.03f, 0.7f));
		Hint.Text(TEXT("Tab / View：トーナメントメニュー"), 40.0f, 104.0f, HudFit(TEXT("Tab / View：トーナメントメニュー"), 17.0f, 300.0f, TEXT("Bold")),
			WithAlpha(Paper, 0.8f), ETextAlign::Left, TEXT("Bold"));
	}
	// Not in a panel: a small strip with where things stand.
	if (Panel == EPanel::None && !bCalled && !bInArena)
	{
		PaintStatus(Design, OutDrawElements, Layer + 20);
	}
	// (In an arena the match's own opening names the round: the band would lie on top of it.)
	if (!bCalled && !bInArena)
	{
		PaintRoundBanner(Design, OutDrawElements, Layer + 30);
	}
	if (Panel != EPanel::None)
	{
		const float In = TwEase(static_cast<float>(FPlatformTime::Seconds() - PanelOpenedAt) / 0.25f);
		const FPainter Shade{AllottedGeometry, OutDrawElements, Layer + 40, In};
		Shade.Box(0.0f, 0.0f, Size.X, Size.Y, FLinearColor(0.004f, 0.007f, 0.02f, Panel == EPanel::Bracket ? 0.9f : 0.72f));
		if (Panel == EPanel::Bracket)
		{
			PaintBracket(Design, OutDrawElements, Layer + 41, In);
			if (Tournament.bFinished)
			{
				if (!bChampionDismissed) { PaintChampion(Design, OutDrawElements, Layer + 70, 1.0f); }
			}
		}
		else if (Panel == EPanel::Menu)
		{
			PaintMenu(Design, OutDrawElements, Layer + 41);
		}
		else if (Panel == EPanel::Watch)
		{
			PaintWatchList(Design, OutDrawElements, Layer + 41);
		}
	}
	if (bCalled)
	{
		PaintCall(Design, OutDrawElements, Layer + 90, Mine);
	}
	if (Tournament.AskingMatch != INDEX_NONE)
	{
		PaintAsk(Design, OutDrawElements, Layer + 100);
	}
	// A short notice.
	const double Since = FPlatformTime::Seconds() - ToastAt;
	if (Since < 2.2)
	{
		const FPainter Note{Design, OutDrawElements, Layer + 110, FMath::Clamp(static_cast<float>(2.2 - Since) / 0.3f, 0.0f, 1.0f)};
		const float W = HudMeasure(Toast, 22.0f, TEXT("Bold")) + 60.0f;
		RoundedBox(Note, (DesignWidth * 0.5f) - W * 0.5f, 770.0f, W, 50.0f, 14.0f, FLinearColor(0.01f, 0.015f, 0.04f, 0.94f));
		Note.Text(Toast, (DesignWidth * 0.5f), 780.0f, 22.0f, Gold, ETextAlign::Center, TEXT("Bold"));
	}
	return Layer + 120;
}

void UChaosImpactTournamentWidget::PaintPublicWait(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer) const
{
	const AChaosImpactGameState* Match = GetMatch();
	if (!Match || Match->Phase != EChaosImpactOnlinePhase::Lobby)
	{
		return;
	}
	// A public tournament's room before it starts: what it is and when it starts (top), who has come (right), and the
	// last seconds counted down in the middle. Everyone practises meanwhile.
	const double Now = GetNow();
	const float Left = static_cast<float>(Match->TournamentStartsAt - Now);
	const TArray<AChaosImpactPlayerState*> People = Match->GetCompetitors(false);
	const int32 CPUs = Match->Rules.Tournament.CPUs.Num();
	const FPainter P{Design, Elements, Layer};
	const FPainter Over{Design, Elements, Layer + 2};

	// ---- The notice ----
	const float W = 760.0f;
	const float X = DesignWidth * 0.5f - W * 0.5f;
	const float Pulse = Left > 0.0f && Left < 60.0f ? 0.5f + 0.5f * FMath::Sin(LocalTime * 4.0f) : 0.0f;
	RoundedBox(P, X, 16.0f, W, 132.0f, 22.0f, FLinearColor(0.008f, 0.012f, 0.032f, 0.9f));
	P.Box(X + 22.0f, 16.0f, W - 44.0f, 3.0f, FMath::Lerp(Violet, Gold, Pulse));
	TwPaintTrophy(FPainter{Design, Elements, Layer + 1}, FVector2D(X + 62.0f, 128.0f), 86.0f, FMath::Frac(LocalTime * 0.25f));
	Over.Text(TEXT("TOURNAMENT"), X + 124.0f, 24.0f, 14.0f, WithAlpha(Violet, 0.95f), ETextAlign::Left, TEXT("Black"));
	Over.Text(Match->RoomName, X + 124.0f, 40.0f, 28.0f, Paper, ETextAlign::Left, TEXT("BlackItalic"));
	// The rules as chips.
	{
		const FString Chips[] = {(Match->Rules.Tournament.Mode == EChaosImpactTournamentMode::Simultaneous ? FString(TEXT("同時進行")) : FString(TEXT("1試合ずつ")))
			+ (Match->Rules.Tournament.bPlacementMatches ? TEXT("・順位決定戦") : TEXT("")),
			FString(TEXT("1対1・2本先取")), CPUs > 0 ? FString::Printf(TEXT("CPU %d人"), CPUs) : FString(TEXT("CPUなし"))};
		float ChipX = X + 124.0f;
		for (const FString& Chip : Chips)
		{
			const float ChipW = HudMeasure(Chip, 15.0f, TEXT("Bold")) + 22.0f;
			RoundedBox(FPainter{Design, Elements, Layer + 1}, ChipX, 88.0f, ChipW, 26.0f, 8.0f, FLinearColor(0.07f, 0.1f, 0.2f, 1.0f));
			Over.Text(Chip, ChipX + 11.0f, 91.0f, 15.0f, WithAlpha(Paper, 0.85f), ETextAlign::Left, TEXT("Bold"));
			ChipX += ChipW + 8.0f;
		}
	}
	// When it starts: the clock time and the time left.
	{
		const float RightX = X + W - 26.0f;
		const FDateTime Start = FDateTime::Now() + FTimespan::FromSeconds(FMath::Max(0.0f, Left));
		const int32 Whole = FMath::Max(0, FMath::CeilToInt(Left));
		if (Left > 0.0f)
		{
			Over.Text(FString::Printf(TEXT("%02d:%02d 開始"), Start.GetHour(), Start.GetMinute()), RightX, 28.0f, 17.0f, WithAlpha(Paper, 0.7f),
				ETextAlign::Right, TEXT("Bold"));
			HudText3D(FPainter{Design, Elements, Layer + 2}, FString::Printf(TEXT("%d:%02d"), Whole / 60, Whole % 60), RightX, 50.0f, 46.0f,
				Left < 60.0f ? FMath::Lerp(Paper, Gold, Pulse) : Paper, ETextAlign::Right);
			Over.Text(TEXT("開始まで"), RightX - HudMeasure(FString::Printf(TEXT("%d:%02d"), Whole / 60, Whole % 60), 46.0f, TEXT("BlackItalic")) - 12.0f,
				72.0f, 16.0f, WithAlpha(Paper, 0.7f), ETextAlign::Right, TEXT("Bold"));
		}
		else
		{
			Over.Text(People.Num() < 2 ? FString(TEXT("開始時刻になりました")) : FString(TEXT("まもなく開始")), RightX, 40.0f, 24.0f, Gold,
				ETextAlign::Right, TEXT("Black"));
			if (People.Num() < 2)
			{
				Over.Text(TEXT("あと1人そろえば始まります"), RightX, 76.0f, 17.0f, WithAlpha(Paper, 0.8f), ETextAlign::Right, TEXT("Bold"));
			}
		}
	}

	// ---- Who has come (and the places left) ----
	{
		const float PW = 300.0f;
		const float PX = DesignWidth - PW - 24.0f;
		const float RowH = 40.0f;
		const int32 Places = FMath::Max(Match->PublicTournamentMaxPlayers, People.Num());
		const float PH = 66.0f + Places * RowH + (CPUs > 0 ? 34.0f : 0.0f);
		RoundedBox(P, PX, 168.0f, PW, PH, 18.0f, FLinearColor(0.008f, 0.012f, 0.032f, 0.82f));
		Over.Text(TEXT("参加者"), PX + 20.0f, 180.0f, 20.0f, Paper, ETextAlign::Left, TEXT("Black"));
		Over.Text(FString::Printf(TEXT("%d / %d人"), People.Num(), Match->PublicTournamentMaxPlayers), PX + PW - 20.0f, 182.0f, 18.0f, Ice,
			ETextAlign::Right, TEXT("Bold"));
		const AChaosImpactPlayerState* Me = GetMe();
		for (int32 Index = 0; Index < Places; ++Index)
		{
			const float Y = 220.0f + Index * RowH;
			if (People.IsValidIndex(Index))
			{
				const AChaosImpactPlayerState* Who = People[Index];
				const bool bMe = Who == Me;
				const FLinearColor Accent = ChaosImpactRoster::GetColourSwatch(FMath::Max(Who->ColourChoice, 0));
				// Newly come: slides in.
				RoundedBox(FPainter{Design, Elements, Layer + 1}, PX + 12.0f, Y, PW - 24.0f, RowH - 6.0f, 10.0f,
					bMe ? FLinearColor(0.07f, 0.12f, 0.26f, 1.0f) : FLinearColor(0.04f, 0.055f, 0.11f, 1.0f));
				Over.Box(PX + 12.0f, Y + 6.0f, 4.0f, RowH - 18.0f, Accent);
				Over.Text(Who->GetPlayerName(), PX + 28.0f, Y + 6.0f, 18.0f, Paper, ETextAlign::Left, TEXT("Bold"));
				if (Who->bRoomHost)
				{
					Over.Text(TEXT("主催"), PX + PW - 24.0f, Y + 8.0f, 14.0f, Gold, ETextAlign::Right, TEXT("Black"));
				}
				else if (bMe)
				{
					Over.Text(TEXT("あなた"), PX + PW - 24.0f, Y + 8.0f, 14.0f, Ice, ETextAlign::Right, TEXT("Black"));
				}
			}
			else
			{
				// A place still open.
				FPainter{Design, Elements, Layer + 1}.Outline(PX + 12.0f, Y, PW - 24.0f, RowH - 6.0f, FLinearColor(0.3f, 0.36f, 0.5f, 0.35f), 1.0f);
				Over.Text(TEXT("募集中"), PX + PW * 0.5f, Y + 8.0f, 15.0f, WithAlpha(Muted, 0.7f), ETextAlign::Center, TEXT("Bold"));
			}
		}
		if (CPUs > 0)
		{
			Over.Text(FString::Printf(TEXT("＋ CPU %d人"), CPUs), PX + 20.0f, 220.0f + Places * RowH + 4.0f, 17.0f, WithAlpha(Paper, 0.75f),
				ETextAlign::Left, TEXT("Bold"));
		}
	}

	// ---- The last ten seconds, large ----
	if (Left > 0.0f && Left <= 10.0f && People.Num() >= 2)
	{
		const int32 Seconds = FMath::CeilToInt(Left);
		const float Local = static_cast<float>(Seconds) - Left;
		HudText3D(FPainter{Design, Elements, Layer + 4, 0.85f * (1.0f - 0.6f * Local)}, FString::FromInt(Seconds), DesignWidth * 0.5f,
			360.0f - 20.0f * (1.0f + 0.5f * FMath::Exp(-8.0f * Local)), 140.0f * (1.0f + 0.4f * FMath::Exp(-8.0f * Local)), Gold,
			ETextAlign::Center);
	}

	// ---- What to do meanwhile ----
	RoundedBox(P, 24.0f, 838.0f, 520.0f, 38.0f, 12.0f, FLinearColor(0.008f, 0.012f, 0.03f, 0.7f));
	Over.Text(TEXT("開始までは自由に練習できます　・　ポーズ：大会からぬける"), 40.0f, 846.0f, 17.0f, WithAlpha(Paper, 0.85f), ETextAlign::Left,
		TEXT("Bold"));
}

void UChaosImpactTournamentWidget::PaintStatus(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer) const
{
	const AChaosImpactGameState* Match = GetMatch();
	const AChaosImpactPlayerState* Me = GetMe();
	if (!Match || !Me)
	{
		return;
	}
	const FChaosImpactTournamentState& Tournament = Match->Tournament;
	const FPainter P{Design, Elements, Layer};
	const FString Round = Tournament.bFinished ? FString(TEXT("大会終了")) : Tournament.Round <= 0 ? FString(TEXT("まもなく開始"))
		: Tournament.GetRoundName(Tournament.Round);
	const FString Status = DescribeMyStatus();
	const FString Where = Me->TournamentRole == EChaosImpactTournamentRole::Spectator ? FString(TEXT("観戦中"))
		: Me->TournamentRole == EChaosImpactTournamentRole::Practice ? FString(TEXT("練習中")) : FString();
	// Top-left: a trophy mark, the round, this player's own situation.
	const float W = FMath::Max(HudMeasure(Status, 19.0f, TEXT("Bold")), HudMeasure(Round, 24.0f)) + 150.0f;
	RoundedBox(P, 24.0f, 22.0f, W, 78.0f, 16.0f, FLinearColor(0.008f, 0.012f, 0.03f, 0.84f));
	P.Box(24.0f, 30.0f, 4.0f, 62.0f, Gold);
	TwPaintTrophy(FPainter{Design, Elements, Layer + 1}, FVector2D(68.0f, 88.0f), 52.0f, FMath::Frac(LocalTime * 0.25f));
	P.Text(Round, 104.0f, 28.0f, 24.0f, Paper, ETextAlign::Left, TEXT("BlackItalic"));
	if (!Where.IsEmpty())
	{
		const float RoundW = HudMeasure(Round, 24.0f, TEXT("BlackItalic"));
		RoundedBox(P, 116.0f + RoundW, 32.0f, HudMeasure(Where, 15.0f, TEXT("Bold")) + 20.0f, 24.0f, 8.0f, WithAlpha(Ice, 0.25f));
		FPainter{Design, Elements, Layer + 1}.Text(Where, 126.0f + RoundW, 34.0f, 15.0f, Ice, ETextAlign::Left, TEXT("Bold"));
	}
	P.Text(Status, 104.0f, 62.0f, 19.0f, WithAlpha(Paper, 0.85f), ETextAlign::Left, TEXT("Bold"));
	// Bottom-left: how to open the menu.
	RoundedBox(P, 24.0f, 838.0f, 330.0f, 38.0f, 12.0f, FLinearColor(0.008f, 0.012f, 0.03f, 0.7f));
	P.Text(TEXT("Tab / View：トーナメントメニュー"), 40.0f, 846.0f, 17.0f, WithAlpha(Paper, 0.8f), ETextAlign::Left, TEXT("Bold"));
}

void UChaosImpactTournamentWidget::PaintRoundBanner(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer) const
{
	const AChaosImpactGameState* Match = GetMatch();
	if (!Match)
	{
		return;
	}
	const FChaosImpactTournamentState& Tournament = Match->Tournament;
	if (Tournament.Round <= 0 || Tournament.bFinished)
	{
		return;
	}
	// A band across the screen as a round starts.
	const float T = static_cast<float>(GetNow() - Tournament.RoundStartedAt);
	if (T < 0.0f || T > 2.6f)
	{
		return;
	}
	const float In = TwEase(T / 0.35f);
	const float Out = 1.0f - TwSmooth((T - 2.1f) / 0.5f);
	const FPainter P{Design, Elements, Layer, Out};
	const float Y = 380.0f;
	P.Box(-20.0f + (1.0f - In) * -1700.0f, Y, 1640.0f, 120.0f, FLinearColor(0.008f, 0.012f, 0.035f, 0.9f));
	P.Box(-20.0f + (1.0f - In) * 1700.0f, Y + 116.0f, 1640.0f, 4.0f, Gold);
	P.Box(-20.0f + (1.0f - In) * -1700.0f, Y, 1640.0f, 3.0f, WithAlpha(Gold, 0.6f));
	const FString Name = Tournament.GetRoundName(Tournament.Round);
	HudText3D(FPainter{Design, Elements, Layer + 1, Out * In}, Name + TEXT("  開始"), (DesignWidth * 0.5f) + (1.0f - In) * 260.0f, Y + 14.0f, 78.0f, Paper,
		ETextAlign::Center);
}

void UChaosImpactTournamentWidget::PaintCard(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer, const FVector2D& At,
	const int32 Entrant, const int32 MatchIndex, const int32 Side, const float Alpha, const float Scale) const
{
	const AChaosImpactGameState* Match = GetMatch();
	if (!Match || Alpha <= 0.01f)
	{
		return;
	}
	const FChaosImpactTournamentState& Tournament = Match->Tournament;
	const float W = CardWidth() * Scale;
	const float H = CardHeight() * Scale;
	const FChaosImpactTournamentMatch* Each = Tournament.Matches.IsValidIndex(MatchIndex) ? &Tournament.Matches[MatchIndex] : nullptr;
	const bool bKnown = Tournament.Entrants.IsValidIndex(Entrant);
	const bool bLost = Each && Each->IsOver() && Each->Winner != Side;
	const bool bWon = Each && Each->IsOver() && Each->Winner == Side;
	const bool bMine = bKnown && Entrant == GetMyEntrant();
	const float Fade = bLost ? 0.45f : 1.0f;
	const FPainter P{Design, Elements, Layer, Alpha};
	if (!bKnown)
	{
		// Nobody yet (or a bye): a faint outline.
		const bool bBye = Each && (Each->bBye || (Each->State != EChaosImpactTournamentMatchState::Waiting && Each->Round == 1));
		RoundedBox(P, At.X, At.Y, W, H, 8.0f, FLinearColor(0.03f, 0.04f, 0.07f, 0.55f));
		P.Outline(At.X, At.Y, W, H, WithAlpha(TwLineDim, 0.5f), 1.0f);
		P.Text(bBye ? TEXT("ー") : TEXT("？"), At.X + W * 0.5f, At.Y + H * 0.5f - 11.0f * Scale, 18.0f * Scale, WithAlpha(Muted, 0.6f),
			ETextAlign::Center, TEXT("Bold"));
		return;
	}
	const FChaosImpactTournamentEntrant& Who = Tournament.Entrants[Entrant];
	const FLinearColor Accent = EntrantColor(Entrant);
	RoundedBox(P, At.X, At.Y, W, H, 8.0f, bLost ? TwCardDim : TwCard);
	if (bWon)
	{
		RoundedBox(P, At.X, At.Y, W, H, 8.0f, FLinearColor(0.16f, 0.11f, 0.02f, 0.55f));
	}
	P.Box(At.X, At.Y + 4.0f, 4.0f * Scale, H - 8.0f, WithAlpha(Accent, Fade));
	// The face.
	const float Face = H - 6.0f * Scale;
	const FBox2D FaceBox(FVector2D(At.X + 8.0f * Scale, At.Y + 3.0f * Scale), FVector2D(At.X + 8.0f * Scale + Face, At.Y + 3.0f * Scale + Face));
	RoundedBox(FPainter{Design, Elements, Layer + 1, Alpha}, FaceBox.Min.X, FaceBox.Min.Y, Face, Face, 6.0f, FLinearColor(0.08f, 0.1f, 0.18f, Fade));
	PaintPortrait(Design, Elements, Layer + 2, Entrant, FaceBox, Alpha * Fade);
	// The name, the CPU mark, the rounds won.
	const float NameSize = FMath::Clamp(H * 0.42f, 13.0f, 21.0f);
	const float TextX = FaceBox.Max.X + 8.0f * Scale;
	// (A bye shows no score: nothing was played.)
	const float WinsW = Each && !Each->bBye && (Each->IsOver() || Each->State == EChaosImpactTournamentMatchState::Live) ? 24.0f * Scale : 0.0f;
	// A narrow card shows no CPU mark (the name matters more); a name too long ends in "…".
	const bool bCpuMark = Who.bCPU && W >= 160.0f;
	FString Name = Who.Name;
	bool bCut = false;
	while (Name.Len() > 1 && HudMeasure(Name + (bCut ? TEXT("…") : TEXT("")), NameSize, TEXT("Bold")) > At.X + W - TextX - WinsW - (bCpuMark ? 34.0f : 6.0f))
	{
		Name.LeftChopInline(1);
		bCut = true;
	}
	if (bCut)
	{
		Name += TEXT("…");
	}
	const FPainter T{Design, Elements, Layer + 3, Alpha};
	T.Text(Name, TextX, At.Y + H * 0.5f - NameSize * 0.68f, NameSize, WithAlpha(bWon ? Gold : Paper, Fade), ETextAlign::Left, TEXT("Bold"));
	if (bCpuMark)
	{
		const float CpuX = TextX + HudMeasure(Name, NameSize, TEXT("Bold")) + 6.0f;
		RoundedBox(FPainter{Design, Elements, Layer + 2, Alpha}, CpuX, At.Y + H * 0.5f - 9.0f, 30.0f, 18.0f, 5.0f, WithAlpha(Muted, 0.35f * Fade));
		T.Text(TEXT("CPU"), CpuX + 15.0f, At.Y + H * 0.5f - 8.0f, 11.0f, WithAlpha(Paper, 0.8f * Fade), ETextAlign::Center, TEXT("Bold"));
	}
	if (WinsW > 0.0f)
	{
		const int32 Wins = Each->State == EChaosImpactTournamentMatchState::Live && Match->TournamentArenas.IsValidIndex(Each->Arena)
			? Match->TournamentArenas[Each->Arena].Duel.Wins[Side] : Each->Wins[Side];
		T.Text(Each->bForfeit && bWon ? FString(TEXT("不戦")) : FString::FromInt(Wins), At.X + W - 10.0f, At.Y + H * 0.5f - NameSize * 0.68f,
			Each->bForfeit && bWon ? 12.0f : NameSize, bWon ? Gold : WithAlpha(Paper, 0.7f * Fade), ETextAlign::Right, TEXT("BlackItalic"));
	}
	if (Who.bForfeited)
	{
		T.Line(FVector2D(TextX, At.Y + H * 0.5f), FVector2D(At.X + W - 8.0f, At.Y + H * 0.5f), WithAlpha(Fire, 0.7f), 2.0f);
	}
	// This player's own card stands out.
	if (bMine)
	{
		const float Pulse = 0.6f + 0.4f * FMath::Sin(LocalTime * 3.0f);
		FPainter{Design, Elements, Layer + 4, Alpha}.Outline(At.X - 2.0f, At.Y - 2.0f, W + 4.0f, H + 4.0f, WithAlpha(Ice, Pulse), 2.0f);
	}
}

void UChaosImpactTournamentWidget::PaintBracket(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer, const float Alpha) const
{
	const AChaosImpactGameState* Match = GetMatch();
	if (!Match)
	{
		return;
	}
	const FChaosImpactTournamentState& Tournament = Match->Tournament;
	const double Now = GetNow();
	const float Since = static_cast<float>(Now - Tournament.StartedAt);
	const float W = CardWidth();
	const float H = CardHeight();
	const FPainter P{Design, Elements, Layer, Alpha};

	// Behind: faint rings round the middle and a soft floor of light where the final is.
	for (int32 Ring = 0; Ring < 5; ++Ring)
	{
		P.Ring(FVector2D((DesignWidth * 0.5f), 480.0f), 120.0f + Ring * 90.0f, FLinearColor(0.2f, 0.35f, 0.7f, 0.05f - Ring * 0.008f), 2.0f, 96);
	}
	P.Box(0.0f, 132.0f, DesignWidth, 1.0f, WithAlpha(TwLineDim, 0.6f));

	// The title, and where the tournament stands.
	const float TitleIn = TwEase(Since / 0.6f);
	HudText3D(FPainter{Design, Elements, Layer + 2, Alpha * TitleIn}, Tournament.Title, 40.0f - (1.0f - TitleIn) * 60.0f, 28.0f, 44.0f, Paper,
		ETextAlign::Left);
	const FString Sub = FString::Printf(TEXT("%d人  ・  %s  ・  2本先取"), Tournament.Entrants.Num(),
		*(FString(Tournament.IsSimultaneous() ? TEXT("同時進行") : TEXT("1試合ずつ")) + (Tournament.Settings.bPlacementMatches ? TEXT("・順位決定戦あり") : TEXT(""))));
	P.Text(Sub, 44.0f, 90.0f, 18.0f, WithAlpha(Paper, 0.7f), ETextAlign::Left, TEXT("Bold"));
	{
		const FString State = Tournament.bFinished ? FString(TEXT("大会終了")) : Tournament.Round <= 0 ? FString(TEXT("まもなく開始"))
			: FString::Printf(TEXT("%s  進行中"), *Tournament.GetRoundName(Tournament.Round));
		const float SW = HudMeasure(State, 22.0f, TEXT("BlackItalic")) + 44.0f;
		RoundedBox(P, DesignWidth - 40.0f - SW, 34.0f, SW, 44.0f, 14.0f, Tournament.bFinished ? WithAlpha(Gold, 0.9f) : FLinearColor(0.05f, 0.08f, 0.18f, 0.95f));
		FPainter{Design, Elements, Layer + 1, Alpha}.Text(State, DesignWidth - 40.0f - SW * 0.5f, 42.0f, 22.0f, Tournament.bFinished ? Ink : Paper,
			ETextAlign::Center, TEXT("BlackItalic"));
	}

	// Round names over the columns (the one being played lit).
	for (int32 Round = 1; Round <= Tournament.Rounds; ++Round)
	{
		const bool bNow = Round == Tournament.Round && !Tournament.bFinished;
		const FLinearColor Color = bNow ? Gold : WithAlpha(Paper, Round < Tournament.Round ? 0.45f : 0.7f);
		const FString Name = Tournament.GetRoundName(Round);
		if (Round >= Tournament.Rounds)
		{
			P.Text(Name, (DesignWidth * 0.5f), TwBracketTop - 6.0f, 24.0f, Color, ETextAlign::Center, TEXT("BlackItalic"));
			continue;
		}
		for (const int32 Half : {0, 1})
		{
			const FVector2D At = CardAt(Round, Half == 0 ? 0 : Tournament.MatchesInRound(Round) - 1, 0);
			P.Text(Name, At.X + W * 0.5f, TwBracketTop - 2.0f, 16.0f, Color, ETextAlign::Center, TEXT("Bold"));
		}
	}

	// The lines: each match's two cards join, and run on to the card the winner takes in the next round. Whoever went
	// through lights their way in gold as their match ends.
	const FPainter Lines{Design, Elements, Layer + 1, Alpha};
	for (int32 Index = 0; Index < Tournament.Matches.Num(); ++Index)
	{
		const FChaosImpactTournamentMatch& Each = Tournament.Matches[Index];
		if (Each.Round >= Tournament.Rounds || Each.Group != 0)
		{
			continue;
		}
		bool bRight = false;
		const FVector2D A = CardAt(Each.Round, Each.Slot, 0, &bRight);
		const FVector2D B = CardAt(Each.Round, Each.Slot, 1);
		const FVector2D Next = CardAt(Each.Round + 1, Each.Slot / 2, Each.Slot % 2);
		const float Dir = bRight ? -1.0f : 1.0f;
		const float EdgeX = bRight ? A.X : A.X + W;
		const float NextEdge = bRight ? Next.X + W : Next.X;
		const float JoinX = EdgeX + (NextEdge - EdgeX) * 0.5f;
		const FVector2D From0(EdgeX, A.Y + H * 0.5f);
		const FVector2D From1(EdgeX, B.Y + H * 0.5f);
		const FVector2D Mid(JoinX, (From0.Y + From1.Y) * 0.5f);
		const FVector2D To(NextEdge, Next.Y + H * 0.5f);
		// Drawing themselves in at the start.
		const float Draw = TwEase((Since - 0.4f - Each.Round * 0.25f) / 0.6f);
		if (Draw <= 0.0f)
		{
			continue;
		}
		const TArray<FVector2D> Path0{From0, FVector2D(JoinX, From0.Y), Mid, FVector2D(JoinX, To.Y), To};
		const TArray<FVector2D> Path1{From1, FVector2D(JoinX, From1.Y), Mid, FVector2D(JoinX, To.Y), To};
		TwPartialPath(Lines, Path0, Draw, TwLineDim, 2.0f, 0.0f);
		TwPartialPath(Lines, Path1, Draw, TwLineDim, 2.0f, 0.0f);
		if (Each.IsOver() && Each.Winner != INDEX_NONE)
		{
			const float Lit = TwEase(static_cast<float>(Now - Each.EndedAt) / 0.8f);
			const bool bMineWon = Each.WinnerEntrant() == GetMyEntrant();
			TwPartialPath(FPainter{Design, Elements, Layer + 2, Alpha}, Each.Winner == 0 ? Path0 : Path1, Lit, bMineWon ? Ice : Gold, 3.0f, 1.0f);
		}
		if (Each.IsPlaying())
		{
			// Being played: the join pulses, LIVE beside it.
			const float Pulse = 0.5f + 0.5f * FMath::Sin(LocalTime * 5.0f);
			TwGlowLine(FPainter{Design, Elements, Layer + 2, Alpha}, FVector2D(JoinX, From0.Y), FVector2D(JoinX, From1.Y), WithAlpha(TwLive, 0.6f + 0.4f * Pulse),
				3.0f, Pulse);
			const bool bLive = Each.State == EChaosImpactTournamentMatchState::Live;
			const FString Tag = bLive ? FString(TEXT("LIVE")) : FString(TEXT("まもなく"));
			const float TagW = HudMeasure(Tag, 12.0f, TEXT("Black")) + 22.0f;
			const float TagX = JoinX + Dir * 6.0f - (bRight ? TagW : 0.0f);
			RoundedBox(FPainter{Design, Elements, Layer + 3, Alpha}, TagX, Mid.Y - 11.0f, TagW, 22.0f, 7.0f, bLive ? TwLive : WithAlpha(Gold, 0.9f));
			FPainter{Design, Elements, Layer + 4, Alpha}.Disc(FVector2D(TagX + 9.0f, Mid.Y), 3.0f * (bLive ? 0.6f + 0.4f * Pulse : 1.0f), Paper);
			FPainter{Design, Elements, Layer + 4, Alpha}.Text(Tag, TagX + 15.0f, Mid.Y - 9.0f, 12.0f, bLive ? Paper : Ink, ETextAlign::Left, TEXT("Black"));
		}
	}

	// The final in the middle: its two cards, a line between, the cup above.
	{
		const int32 Final = Tournament.MatchIndex(Tournament.Rounds, 0);
		const FVector2D L = CardAt(Tournament.Rounds, 0, 0);
		const FVector2D R = CardAt(Tournament.Rounds, 0, 1);
		const float Draw = TwEase((Since - 0.6f - Tournament.Rounds * 0.25f) / 0.6f);
		TwPaintTrophy(FPainter{Design, Elements, Layer + 3, Alpha * Draw}, FVector2D((DesignWidth * 0.5f), L.Y - 34.0f), 120.0f, FMath::Frac(LocalTime * 0.2f));
		P.Text(TEXT("VS"), (DesignWidth * 0.5f), L.Y + H * 0.5f - 13.0f, 22.0f, WithAlpha(Gold, Draw), ETextAlign::Center, TEXT("BlackItalic"));
		if (Tournament.Matches.IsValidIndex(Final))
		{
			const FChaosImpactTournamentMatch& Each = Tournament.Matches[Final];
			const float CardIn = TwEase((Since - 0.2f) / 0.5f);
			PaintCard(Design, Elements, Layer + 5, L, Each.Entrants[0], Final, 0, Alpha * CardIn);
			PaintCard(Design, Elements, Layer + 5, R, Each.Entrants[1], Final, 1, Alpha * CardIn);
			if (Each.IsPlaying())
			{
				const float Pulse = 0.5f + 0.5f * FMath::Sin(LocalTime * 5.0f);
				FPainter{Design, Elements, Layer + 9, Alpha}.Outline(L.X - 4.0f, L.Y - 4.0f, R.X + W - L.X + 8.0f, H + 8.0f, WithAlpha(TwLive, 0.5f + 0.5f * Pulse), 2.0f);
			}
		}
	}

	// Every other card, coming in row by row at the start.
	for (int32 Index = 0; Index < Tournament.Matches.Num(); ++Index)
	{
		const FChaosImpactTournamentMatch& Each = Tournament.Matches[Index];
		if (Each.Round >= Tournament.Rounds || Each.Group != 0)
		{
			continue;
		}
		for (int32 Side = 0; Side < 2; ++Side)
		{
			bool bRight = false;
			const FVector2D At = CardAt(Each.Round, Each.Slot, Side, &bRight);
			const float Delay = 0.15f + (Each.Round - 1) * 0.25f + (At.Y - TwBracketTop) / (TwBracketBottom - TwBracketTop) * 0.35f;
			const float In = TwEase((Since - Delay) / 0.4f);
			// A winner arriving in the next round pops in as its line reaches it.
			float Arrive = 1.0f;
			if (Each.Round > 1)
			{
				const int32 Feeder = Tournament.MatchIndex(Each.Round - 1, Each.Slot * 2 + Side);
				if (Tournament.Matches.IsValidIndex(Feeder) && Tournament.Matches[Feeder].IsOver())
				{
					Arrive = TwEase((static_cast<float>(Now - Tournament.Matches[Feeder].EndedAt) - 0.6f) / 0.3f);
				}
			}
			const float Slide = (1.0f - In) * (bRight ? 40.0f : -40.0f);
			const int32 Entrant = Arrive > 0.0f ? Each.Entrants[Side] : INDEX_NONE;
			PaintCard(Design, Elements, Layer + 5, At + FVector2D(Slide, 0.0f), Entrant, Index, Side, Alpha * In * (Entrant != INDEX_NONE ? Arrive : 1.0f));
		}
	}

	PaintPlacementStrip(Design, Elements, Layer + 10, Alpha);

	// The match the cursor is on.
	if (Tournament.Matches.IsValidIndex(BracketCursor))
	{
		const FBox2D Box = MatchBox(BracketCursor);
		const float Pulse = 0.65f + 0.35f * FMath::Sin(LocalTime * 6.0f);
		FPainter{Design, Elements, Layer + 12, Alpha}.Outline(static_cast<float>(Box.Min.X) - 6.0f, static_cast<float>(Box.Min.Y) - 6.0f,
			static_cast<float>(Box.GetSize().X) + 12.0f, static_cast<float>(Box.GetSize().Y) + 12.0f, WithAlpha(Gold, Pulse), 3.0f);
	}

	// Hints along the bottom.
	const FPainter Hint{Design, Elements, Layer + 12, Alpha};
	Hint.Box(0.0f, 842.0f, DesignWidth, 58.0f, FLinearColor(0.004f, 0.006f, 0.016f, 0.8f));
	Hint.Text(DesignWidth < 1400.0f ? TEXT("十字：えらぶ　A：LIVEを観戦　B：メニュー　Tab / View：とじる")
		: TEXT("十字 / 矢印：試合をえらぶ　　A / Enter：LIVEの試合を観戦　　B：メニュー　　Tab / View：とじる"), (DesignWidth * 0.5f), 858.0f, 18.0f,
		WithAlpha(Paper, 0.85f), ETextAlign::Center, TEXT("Bold"));
}

void UChaosImpactTournamentWidget::PaintPlacementStrip(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer,
	const float Alpha) const
{
	const AChaosImpactGameState* Match = GetMatch();
	if (!Match || !Match->Tournament.Settings.bPlacementMatches)
	{
		return;
	}
	const FChaosImpactTournamentState& Tournament = Match->Tournament;
	// This round's matches for places (or, before they start, the next round's), each as a chip: its name, its two
	// players and how it stands.
	const int32 ShowRound = FMath::Clamp(Tournament.bFinished ? Tournament.Rounds : FMath::Max(Tournament.Round, 1), 1, Tournament.Rounds);
	TArray<int32> Shown;
	for (int32 Index = 0; Index < Tournament.Matches.Num(); ++Index)
	{
		const FChaosImpactTournamentMatch& Each = Tournament.Matches[Index];
		if (Each.Group != 0 && Each.Round == ShowRound && !Each.bBye && (Each.Entrants[0] != INDEX_NONE || Each.Entrants[1] != INDEX_NONE))
		{
			Shown.Add(Index);
		}
	}
	const FPainter P{Design, Elements, Layer, Alpha};
	const float Y = 806.0f;
	P.Text(TEXT("順位決定戦"), 40.0f, Y + 6.0f, 18.0f, Gold, ETextAlign::Left, TEXT("BlackItalic"), 2.0f, Ink);
	if (Shown.IsEmpty())
	{
		P.Text(ShowRound <= 1 ? FString(TEXT("1回戦のあとから始まります")) : FString(TEXT("このラウンドはありません")), 150.0f, Y + 8.0f, 16.0f,
			WithAlpha(Paper, 0.6f), ETextAlign::Left, TEXT("Bold"));
		return;
	}
	const float Room = DesignWidth - 190.0f;
	const float ChipW = FMath::Min(330.0f, Room / Shown.Num() - 8.0f);
	for (int32 Row = 0; Row < Shown.Num(); ++Row)
	{
		const FChaosImpactTournamentMatch& Each = Tournament.Matches[Shown[Row]];
		const float X = 150.0f + Row * (ChipW + 8.0f);
		const bool bLive = Each.IsPlaying();
		RoundedBox(P, X, Y, ChipW, 32.0f, 9.0f, bLive ? WithAlpha(TwLive, 0.35f) : FLinearColor(0.03f, 0.045f, 0.1f, 0.92f));
		const auto Who = [&Tournament](const int32 Entrant) { return Tournament.Entrants.IsValidIndex(Entrant) ? Tournament.Entrants[Entrant].Name : FString(TEXT("？")); };
		const int32 Wins0 = bLive && Match->TournamentArenas.IsValidIndex(Each.Arena) ? Match->TournamentArenas[Each.Arena].Duel.Wins[0] : Each.Wins[0];
		const int32 Wins1 = bLive && Match->TournamentArenas.IsValidIndex(Each.Arena) ? Match->TournamentArenas[Each.Arena].Duel.Wins[1] : Each.Wins[1];
		const FString Score = Each.IsOver() || bLive ? FString::Printf(TEXT("%d-%d"), Wins0, Wins1) : FString(TEXT("vs"));
		const FString Line = FString::Printf(TEXT("%s  %s %s %s"), *Tournament.GetMatchName(Shown[Row]), *Who(Each.Entrants[0]), *Score, *Who(Each.Entrants[1]));
		HudAbove(P, 1).Text(Line, X + 12.0f, Y + 6.0f, HudFit(Line, 16.0f, ChipW - 24.0f, TEXT("Bold")), Each.IsOver() ? WithAlpha(Paper, 0.6f) : Paper,
			ETextAlign::Left, TEXT("Bold"));
	}
}

void UChaosImpactTournamentWidget::PaintChampion(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer, const float Alpha) const
{
	const AChaosImpactGameState* Match = GetMatch();
	if (!Match)
	{
		return;
	}
	const FChaosImpactTournamentState& Tournament = Match->Tournament;
	const float T = static_cast<float>(GetNow() - Tournament.FinishedAt);
	if (T < 0.6f)
	{
		return;
	}
	const float In = TwEase((T - 0.6f) / 0.6f);
	// A darker veil, light rising behind the champion, falling paper.
	const FPainter P{Design, Elements, Layer, Alpha * In};
	P.Box(0.0f, 0.0f, DesignWidth, TwDesignH, FLinearColor(0.0f, 0.0f, 0.0f, 0.55f));
	for (int32 Ray = 0; Ray < 14; ++Ray)
	{
		const float Angle = Ray * UE_TWO_PI / 14.0f + LocalTime * 0.12f;
		const FVector2D Dir(FMath::Cos(Angle), FMath::Sin(Angle));
		FPainter{Design, Elements, Layer + 1, Alpha * In * 0.6f}.Line(FVector2D((DesignWidth * 0.5f), 430.0f) + Dir * 140.0f, FVector2D((DesignWidth * 0.5f), 430.0f) + Dir * 520.0f,
			FLinearColor(1.0f, 0.8f, 0.3f, 0.12f), 46.0f);
	}
	for (int32 Piece = 0; Piece < 70; ++Piece)
	{
		const float R1 = FMath::Frac(FMath::Sin(Piece * 12.9898f) * 43758.5453f);
		const float R2 = FMath::Frac(FMath::Sin(Piece * 78.233f) * 12543.1f);
		const float Fall = FMath::Fmod((T - 0.6f) * (90.0f + 80.0f * R2) + R1 * 900.0f, 960.0f) - 40.0f;
		const float X = R1 * DesignWidth + FMath::Sin(T * (1.5f + R2) + Piece) * 24.0f;
		const float Turn = FMath::Abs(FMath::Sin(T * (3.0f + 5.0f * R1) + Piece));
		const FLinearColor Colors[] = {Gold, Paper, Ice, Fire, FLinearColor(0.4f, 0.95f, 0.5f, 1.0f)};
		FPainter{Design, Elements, Layer + 2, Alpha * In}.Box(X, Fall, 4.0f + 7.0f * Turn, 11.0f, Colors[Piece % UE_ARRAY_COUNT(Colors)]);
	}
	const int32 Champion = Tournament.Champion;
	const float Pop = 1.0f + 0.25f * FMath::Exp(-7.0f * (T - 0.6f));
	TwPaintTrophy(FPainter{Design, Elements, Layer + 3, Alpha * In}, FVector2D((DesignWidth * 0.5f), 330.0f), 170.0f * Pop, FMath::Frac(LocalTime * 0.35f));
	HudText3D(FPainter{Design, Elements, Layer + 6, Alpha * In}, TEXT("優勝"), (DesignWidth * 0.5f), 338.0f, 96.0f * Pop, Gold, ETextAlign::Center);
	if (Tournament.Entrants.IsValidIndex(Champion))
	{
		const FChaosImpactTournamentEntrant& Who = Tournament.Entrants[Champion];
		const FBox2D Face(FVector2D((DesignWidth * 0.5f) - 90.0f, 470.0f), FVector2D((DesignWidth * 0.5f) + 90.0f, 650.0f));
		RoundedBox(FPainter{Design, Elements, Layer + 4, Alpha * In}, (DesignWidth * 0.5f) - 96.0f, 464.0f, 192.0f, 192.0f, 24.0f, Gold);
		RoundedBox(FPainter{Design, Elements, Layer + 5, Alpha * In}, (DesignWidth * 0.5f) - 90.0f, 470.0f, 180.0f, 180.0f, 20.0f, FLinearColor(0.08f, 0.1f, 0.2f, 1.0f));
		PaintPortrait(Design, Elements, Layer + 6, Champion, Face, Alpha * In);
		HudText3D(FPainter{Design, Elements, Layer + 7, Alpha * In}, Who.Name, (DesignWidth * 0.5f), 670.0f, 56.0f, Paper, ETextAlign::Center);
	}
	else
	{
		P.Text(TEXT("優勝者なし"), (DesignWidth * 0.5f), 560.0f, 40.0f, Paper, ETextAlign::Center, TEXT("Black"));
	}
	const float Left = FMath::Max(0.0f, ChaosImpactTournament::ChampionSeconds - T);
	const AChaosImpactPlayerState* Me = GetMe();
	const bool bCanEnd = (Me && Me->bRoomHost) || Match->bLocalTournament;
	const FString Foot = FString::Printf(TEXT("%d秒後にロビーへ戻ります　　Enter / A：%s"), FMath::CeilToInt(Left), bCanEnd ? TEXT("すぐ戻る") : TEXT("とじる"));
	P.Text(Foot, (DesignWidth * 0.5f), 760.0f, HudFit(Foot, 20.0f, 900.0f, TEXT("Bold")), WithAlpha(Paper, 0.75f), ETextAlign::Center, TEXT("Bold"));
	// 順位決定戦: everyone's place, down the right.
	if (Tournament.Settings.bPlacementMatches)
	{
		TArray<int32> Order;
		for (int32 Index = 0; Index < Tournament.Entrants.Num(); ++Index)
		{
			if (Tournament.Entrants[Index].Place > 1) { Order.Add(Index); }
		}
		Order.Sort([&Tournament](const int32 A, const int32 B) { return Tournament.Entrants[A].Place < Tournament.Entrants[B].Place; });
		const float ListX = DesignWidth - 380.0f;
		const FPainter List{Design, Elements, Layer + 8, Alpha * In};
		RoundedBox(List, ListX, 250.0f, 340.0f, 46.0f + FMath::Min(Order.Num(), 11) * 38.0f, 16.0f, FLinearColor(0.008f, 0.012f, 0.03f, 0.88f));
		HudAbove(List, 1).Text(TEXT("最終順位"), ListX + 22.0f, 260.0f, 22.0f, Gold, ETextAlign::Left, TEXT("BlackItalic"));
		for (int32 Row = 0; Row < FMath::Min(Order.Num(), 11); ++Row)
		{
			const FChaosImpactTournamentEntrant& Who = Tournament.Entrants[Order[Row]];
			const float Y = 300.0f + Row * 38.0f;
			const FLinearColor PlaceColor = Who.Place == 2 ? TwSilver : Who.Place == 3 ? FLinearColor(0.95f, 0.6f, 0.35f) : WithAlpha(Paper, 0.8f);
			HudAbove(List, 1).Text(FString::Printf(TEXT("%d位"), Who.Place), ListX + 70.0f, Y, 22.0f, PlaceColor, ETextAlign::Right, TEXT("BlackItalic"));
			HudAbove(List, 1).Text(Who.Name, ListX + 86.0f, Y + 2.0f, HudFit(Who.Name, 20.0f, 230.0f, TEXT("Bold")), Paper, ETextAlign::Left, TEXT("Bold"));
		}
	}
}

void UChaosImpactTournamentWidget::PaintMenu(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer) const
{
	const AChaosImpactPlayerState* Me = GetMe();
	const float In = TwEase(static_cast<float>(FPlatformTime::Seconds() - PanelOpenedAt) / 0.3f);
	const FPainter P{Design, Elements, Layer, In};
	HudText3D(FPainter{Design, Elements, Layer + 2, In}, TEXT("トーナメント"), (DesignWidth * 0.5f), 170.0f, 54.0f, Paper, ETextAlign::Center);
	P.Text(DescribeMyStatus(), (DesignWidth * 0.5f), 240.0f, 20.0f, WithAlpha(Paper, 0.8f), ETextAlign::Center, TEXT("Bold"));
	struct FItem { const TCHAR* Title; const TCHAR* Detail; FLinearColor Accent; };
	const FItem Items[] = {
		{TEXT("練習する"), TEXT("自分の練習場で、出番まで体を動かす"), Ice},
		{TEXT("試合を観る"), TEXT("いま行われている試合をえらんで観戦"), TwLive},
		{TEXT("トーナメント表"), TEXT("勝ち上がりと、試合の進み具合"), Gold},
		{TEXT("とじる"), TEXT(""), Muted}};
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const FBox2D R = MenuRect(Index);
		const float Delay = Index * 0.05f;
		const float ItemIn = TwEase((static_cast<float>(FPlatformTime::Seconds() - PanelOpenedAt) - Delay) / 0.3f);
		const bool bOn = Index == MenuCursor;
		const bool bDisabled = Index == 0 && Me && Me->bSpectating;
		const float X = static_cast<float>(R.Min.X) + (1.0f - ItemIn) * 60.0f;
		const FPainter Item{Design, Elements, Layer + 1, ItemIn * (bDisabled ? 0.45f : 1.0f)};
		RoundedBox(Item, X, static_cast<float>(R.Min.Y), static_cast<float>(R.GetSize().X), static_cast<float>(R.GetSize().Y), 16.0f,
			bOn ? FLinearColor(0.08f, 0.12f, 0.26f, 0.98f) : FLinearColor(0.025f, 0.035f, 0.075f, 0.95f));
		Item.Box(X, static_cast<float>(R.Min.Y) + 10.0f, 5.0f, static_cast<float>(R.GetSize().Y) - 20.0f, Items[Index].Accent);
		const FPainter Text{Design, Elements, Layer + 2, ItemIn * (bDisabled ? 0.45f : 1.0f)};
		if (Index == 3)
		{
			Text.Text(Items[Index].Title, X + 240.0f, static_cast<float>(R.Min.Y) + 18.0f, 24.0f, Paper, ETextAlign::Center, TEXT("Black"));
		}
		else
		{
			Text.Text(Items[Index].Title, X + 30.0f, static_cast<float>(R.Min.Y) + 12.0f, 30.0f, Paper, ETextAlign::Left, TEXT("BlackItalic"));
			Text.Text(Items[Index].Detail, X + 32.0f, static_cast<float>(R.Min.Y) + 56.0f, 17.0f, WithAlpha(Paper, 0.7f), ETextAlign::Left, TEXT("Bold"));
		}
		if (bOn)
		{
			const float Pulse = 0.65f + 0.35f * FMath::Sin(LocalTime * 6.0f);
			FPainter{Design, Elements, Layer + 3, ItemIn}.Outline(X - 3.0f, static_cast<float>(R.Min.Y) - 3.0f, static_cast<float>(R.GetSize().X) + 6.0f,
				static_cast<float>(R.GetSize().Y) + 6.0f, WithAlpha(Gold, Pulse), 3.0f);
		}
	}
	P.Text(TEXT("十字 / 矢印：えらぶ　　A / Enter：決定　　B / Tab / View：とじる"), (DesignWidth * 0.5f), 760.0f, 18.0f, WithAlpha(Paper, 0.7f),
		ETextAlign::Center, TEXT("Bold"));
}

void UChaosImpactTournamentWidget::PaintWatchList(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer) const
{
	const AChaosImpactGameState* Match = GetMatch();
	if (!Match)
	{
		return;
	}
	const FChaosImpactTournamentState& Tournament = Match->Tournament;
	const float In = TwEase(static_cast<float>(FPlatformTime::Seconds() - PanelOpenedAt) / 0.3f);
	const FPainter P{Design, Elements, Layer, In};
	HudText3D(FPainter{Design, Elements, Layer + 2, In}, TEXT("試合を観る"), (DesignWidth * 0.5f), 150.0f, 48.0f, Paper, ETextAlign::Center);
	const TArray<int32> LiveMatches = GetLiveMatches();
	for (int32 Index = 0; Index <= LiveMatches.Num(); ++Index)
	{
		const FBox2D R = WatchRect(Index);
		const bool bOn = Index == WatchCursor;
		RoundedBox(P, static_cast<float>(R.Min.X), static_cast<float>(R.Min.Y), static_cast<float>(R.GetSize().X), static_cast<float>(R.GetSize().Y), 14.0f,
			bOn ? FLinearColor(0.08f, 0.12f, 0.26f, 0.98f) : FLinearColor(0.025f, 0.035f, 0.075f, 0.95f));
		const FPainter Text{Design, Elements, Layer + 1, In};
		if (Index == LiveMatches.Num())
		{
			Text.Text(TEXT("おまかせ（行われている試合を順に）"), static_cast<float>(R.GetCenter().X), static_cast<float>(R.Min.Y) + 18.0f, 22.0f, Paper,
				ETextAlign::Center, TEXT("Bold"));
		}
		else
		{
			const FChaosImpactTournamentMatch& Each = Tournament.Matches[LiveMatches[Index]];
			const auto Name = [&Tournament](const int32 Entrant) { return Tournament.Entrants.IsValidIndex(Entrant) ? Tournament.Entrants[Entrant].Name : FString(TEXT("？")); };
			const FBox2D FaceA(R.Min + FVector2D(12.0, 8.0), R.Min + FVector2D(60.0, 56.0));
			const FBox2D FaceB(FVector2D(R.Max.X - 60.0, R.Min.Y + 8.0), FVector2D(R.Max.X - 12.0, R.Min.Y + 56.0));
			PaintPortrait(Design, Elements, Layer + 2, Each.Entrants[0], FaceA, In);
			PaintPortrait(Design, Elements, Layer + 2, Each.Entrants[1], FaceB, In);
			Text.Text(Name(Each.Entrants[0]), static_cast<float>(R.Min.X) + 74.0f, static_cast<float>(R.Min.Y) + 16.0f, 22.0f, Paper, ETextAlign::Left, TEXT("Bold"));
			Text.Text(Name(Each.Entrants[1]), static_cast<float>(R.Max.X) - 74.0f, static_cast<float>(R.Min.Y) + 16.0f, 22.0f, Paper, ETextAlign::Right, TEXT("Bold"));
			const int32 Wins0 = Match->TournamentArenas.IsValidIndex(Each.Arena) ? Match->TournamentArenas[Each.Arena].Duel.Wins[0] : 0;
			const int32 Wins1 = Match->TournamentArenas.IsValidIndex(Each.Arena) ? Match->TournamentArenas[Each.Arena].Duel.Wins[1] : 0;
			Text.Text(FString::Printf(TEXT("%d - %d"), Wins0, Wins1), static_cast<float>(R.GetCenter().X), static_cast<float>(R.Min.Y) + 6.0f, 24.0f, Gold,
				ETextAlign::Center, TEXT("BlackItalic"));
			Text.Text(Tournament.GetMatchName(LiveMatches[Index]), static_cast<float>(R.GetCenter().X), static_cast<float>(R.Min.Y) + 38.0f, 14.0f,
				WithAlpha(Paper, 0.7f), ETextAlign::Center, TEXT("Bold"));
		}
		if (bOn)
		{
			const float Pulse = 0.65f + 0.35f * FMath::Sin(LocalTime * 6.0f);
			FPainter{Design, Elements, Layer + 3, In}.Outline(static_cast<float>(R.Min.X) - 3.0f, static_cast<float>(R.Min.Y) - 3.0f,
				static_cast<float>(R.GetSize().X) + 6.0f, static_cast<float>(R.GetSize().Y) + 6.0f, WithAlpha(Gold, Pulse), 3.0f);
		}
	}
	P.Text(TEXT("A / Enter：観戦する　　B：もどる"), (DesignWidth * 0.5f), (DesignWidth * 0.5f), 18.0f, WithAlpha(Paper, 0.7f), ETextAlign::Center, TEXT("Bold"));
}

bool UChaosImpactTournamentWidget::CoversPlay() const
{
	const AChaosImpactGameState* Match = GetMatch();
	if (!Match || !Match->IsTournament())
	{
		return false;
	}
	const FChaosImpactTournamentState& Tournament = Match->Tournament;
	const int32 Mine = Tournament.FindCurrentMatch(GetMyEntrant());
	return Panel != EPanel::None || Tournament.AskingMatch != INDEX_NONE
		|| (Tournament.Matches.IsValidIndex(Mine) && Tournament.Matches[Mine].State == EChaosImpactTournamentMatchState::Calling);
}

void UChaosImpactTournamentWidget::PaintCall(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer, const int32 MatchIndex) const
{
	const AChaosImpactGameState* Match = GetMatch();
	if (!Match || !Match->Tournament.Matches.IsValidIndex(MatchIndex))
	{
		return;
	}
	const FChaosImpactTournamentState& Tournament = Match->Tournament;
	const FChaosImpactTournamentMatch& Each = Tournament.Matches[MatchIndex];
	const float T = static_cast<float>(GetNow() - Each.CalledAt);
	const float In = TwEase(T / 0.3f);
	const FPainter P{Design, Elements, Layer, In};
	P.Box(0.0f, 0.0f, DesignWidth, TwDesignH, FLinearColor(0.0f, 0.0f, 0.0f, 0.6f));
	P.Box(0.0f, 300.0f, DesignWidth, 300.0f, FLinearColor(0.01f, 0.015f, 0.04f, 0.92f));
	P.Box((1.0f - In) * -DesignWidth, 300.0f, DesignWidth, 4.0f, Gold);
	P.Box((1.0f - In) * DesignWidth, 596.0f, DesignWidth, 4.0f, Gold);
	HudText3D(FPainter{Design, Elements, Layer + 1, In}, TEXT("出番です！"), (DesignWidth * 0.5f), 196.0f, 72.0f, Gold, ETextAlign::Center);
	P.Text(Tournament.GetMatchName(MatchIndex), (DesignWidth * 0.5f), 318.0f, 22.0f, WithAlpha(Paper, 0.8f), ETextAlign::Center, TEXT("Bold"));
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const float Slide = (1.0f - TwEase((T - 0.1f) / 0.4f)) * (Side == 0 ? -300.0f : 300.0f);
		const float CX = DesignWidth * 0.5f + (Side == 0 ? -1.0f : 1.0f) * FMath::Min(330.0f, DesignWidth * 0.3f) + Slide;
		const FBox2D Face(FVector2D(CX - 80.0f, 360.0f), FVector2D(CX + 80.0f, 520.0f));
		RoundedBox(FPainter{Design, Elements, Layer + 1, In}, CX - 84.0f, 356.0f, 168.0f, 168.0f, 20.0f, EntrantColor(Each.Entrants[Side]));
		RoundedBox(FPainter{Design, Elements, Layer + 2, In}, CX - 80.0f, 360.0f, 160.0f, 160.0f, 18.0f, FLinearColor(0.07f, 0.09f, 0.18f, 1.0f));
		PaintPortrait(Design, Elements, Layer + 3, Each.Entrants[Side], Face, In);
		const FString Name = Tournament.Entrants.IsValidIndex(Each.Entrants[Side]) ? Tournament.Entrants[Each.Entrants[Side]].Name : FString();
		FPainter{Design, Elements, Layer + 3, In}.Text(Name, CX, 532.0f, 30.0f, Paper, ETextAlign::Center, TEXT("Black"));
	}
	HudText3D(FPainter{Design, Elements, Layer + 4, In}, TEXT("VS"), (DesignWidth * 0.5f), 400.0f, 90.0f, Fire, ETextAlign::Center);
	const float Left = FMath::Clamp(1.0f - T / ChaosImpactTournament::CallSeconds, 0.0f, 1.0f);
	P.Box(DesignWidth * 0.5f - 200.0f, 640.0f, 400.0f, 6.0f, WithAlpha(Paper, 0.2f));
	FPainter{Design, Elements, Layer + 1, In}.Box(DesignWidth * 0.5f - 200.0f, 640.0f, 400.0f * Left, 6.0f, Gold);
	P.Text(TEXT("アリーナへ移動します"), (DesignWidth * 0.5f), 660.0f, 20.0f, WithAlpha(Paper, 0.8f), ETextAlign::Center, TEXT("Bold"));
}

void UChaosImpactTournamentWidget::PaintArena(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer, const int32 Arena) const
{
	const AChaosImpactGameState* Match = GetMatch();
	if (!Match || !Match->TournamentArenas.IsValidIndex(Arena))
	{
		return;
	}
	const FChaosImpactTournamentState& Tournament = Match->Tournament;
	const FChaosImpactTournamentArena& Place = Match->TournamentArenas[Arena];
	if (!Tournament.Matches.IsValidIndex(Place.Match))
	{
		return;
	}
	const FChaosImpactTournamentMatch& Each = Tournament.Matches[Place.Match];
	const FChaosImpactDuelState& Duel = Place.Duel;
	const float StepT = Match->GetDuelStepElapsed(Duel);
	const float Remaining = Match->GetDuelStepRemaining(Duel);
	const FPainter P{Design, Elements, Layer};
	const auto Name = [&Tournament, &Each](const int32 Side)
	{
		const int32 Entrant = Each.Entrants[Side];
		return Tournament.Entrants.IsValidIndex(Entrant) ? Tournament.Entrants[Entrant].Name : FString();
	};
	// Top: both names, their rounds won, the round's clock.
	RoundedBox(P, DesignWidth * 0.5f - 330.0f, 16.0f, 660.0f, 74.0f, 18.0f, FLinearColor(0.008f, 0.012f, 0.03f, 0.82f));
	const FPainter T{Design, Elements, Layer + 1};
	// (Each name within its half of the plate.)
	T.Text(Name(0), 760.0f - 46.0f, 26.0f, HudFit(Name(0), 24.0f, 220.0f), FLinearColor(1.0f, 0.42f, 0.4f, 1.0f), ETextAlign::Right, TEXT("Black"));
	T.Text(Name(1), 840.0f + 46.0f, 26.0f, HudFit(Name(1), 24.0f, 220.0f), FLinearColor(0.45f, 0.75f, 1.0f, 1.0f), ETextAlign::Left, TEXT("Black"));
	for (int32 Side = 0; Side < 2; ++Side)
	{
		for (int32 Pip = 0; Pip < ChaosImpactMatch::DuelRoundsToWin; ++Pip)
		{
			const FVector2D At(Side == 0 ? 700.0f - Pip * 24.0f : 900.0f + Pip * 24.0f, 72.0f);
			T.Ring(At, 8.0f, WithAlpha(Paper, 0.5f), 2.0f, 24);
			if (Pip < Duel.Wins[Side])
			{
				FPainter{Design, Elements, Layer + 2}.Disc(At, 6.0f, Gold);
			}
		}
	}
	const bool bClock = Duel.Step == EChaosImpactDuelStep::Fight || Duel.Step == EChaosImpactDuelStep::SuddenDeath;
	T.Text(bClock ? FString::FromInt(FMath::CeilToInt(Remaining)) : FString(TEXT("-")), (DesignWidth * 0.5f), 26.0f, 34.0f,
		Duel.Step == EChaosImpactDuelStep::SuddenDeath ? TwLive : Paper, ETextAlign::Center, TEXT("BlackItalic"));
	T.Text(FString::Printf(TEXT("%s  ・  ROUND %d"), *Tournament.GetRoundName(Each.Round), FMath::Max(Duel.Round, 1)), (DesignWidth * 0.5f), 96.0f, 15.0f,
		WithAlpha(Paper, 0.7f), ETextAlign::Center, TEXT("Bold"));

	// The match's moments in the middle (clean text, no bursts).
	const FPainter Call{Design, Elements, Layer + 3};
	switch (Duel.Step)
	{
	case EChaosImpactDuelStep::BallDraw:
	{
		const float In = TwEase(StepT / 0.35f);
		RoundedBox(FPainter{Design, Elements, Layer + 2, In}, DesignWidth * 0.5f - 350.0f, 300.0f, 700.0f, 220.0f, 24.0f, FLinearColor(0.01f, 0.015f, 0.04f, 0.9f));
		FPainter{Design, Elements, Layer + 3, In}.Text(TEXT("この試合のボール"), (DesignWidth * 0.5f), 316.0f, 26.0f, Paper, ETextAlign::Center, TEXT("Black"));
		for (int32 Pick = 0; Pick < Duel.BallPicks.Num(); ++Pick)
		{
			const float Shown = TwEase((StepT - ChaosImpactMatch::DuelReelStopSeconds[FMath::Min(Pick, ChaosImpactMatch::DuelBallPicks - 1)]) / 0.25f);
			const EChaosImpactBallType Kind = static_cast<EChaosImpactBallType>(Duel.BallPicks[Pick]);
			const FVector2D At(570.0f + Pick * 230.0f, 410.0f);
			const FPainter Chip{Design, Elements, Layer + 3, In};
			Chip.Disc(At, 34.0f, WithAlpha(Navy, 1.0f));
			FPainter{Design, Elements, Layer + 4, In * Shown}.Disc(At, 30.0f * (0.6f + 0.4f * Shown), ChaosImpactBallTypes::GetColor(Kind));
			if (Shown <= 0.0f)
			{
				// Still turning.
				Chip.Text(TEXT("？"), At.X, At.Y - 18.0f, 28.0f, WithAlpha(Paper, 0.6f + 0.4f * FMath::Sin(LocalTime * 20.0f + Pick)), ETextAlign::Center, TEXT("Black"));
			}
			FPainter{Design, Elements, Layer + 4, In * Shown}.Text(ChaosImpactBallTypes::GetDisplayName(Kind), At.X, At.Y + 44.0f, 18.0f, Paper,
				ETextAlign::Center, TEXT("Bold"));
		}
		break;
	}
	case EChaosImpactDuelStep::RoundIntro:
	{
		const bool bFight = Remaining < 0.55f;
		const float Pop = bFight ? 1.0f + 0.3f * FMath::Exp(-14.0f * (0.55f - Remaining)) : 1.0f + 0.2f * FMath::Exp(-10.0f * StepT);
		HudText3D(Call, bFight ? FString(TEXT("FIGHT!")) : FString::Printf(TEXT("ROUND %d"), Duel.Round), (DesignWidth * 0.5f), 360.0f, 110.0f * Pop,
			bFight ? Gold : Paper, ETextAlign::Center);
		break;
	}
	case EChaosImpactDuelStep::SuddenDeath:
		if (StepT < 2.0f)
		{
			HudText3D(FPainter{Design, Elements, Layer + 3, 1.0f - TwSmooth((StepT - 1.5f) / 0.5f)}, TEXT("SUDDEN DEATH"), (DesignWidth * 0.5f), 380.0f, 74.0f, TwLive,
				ETextAlign::Center);
		}
		break;
	case EChaosImpactDuelStep::RoundOver:
	{
		const FString How = Duel.RoundEnd == EChaosImpactDuelEnd::Knockout ? FString(TEXT("K.O."))
			: Duel.RoundEnd == EChaosImpactDuelEnd::Time ? FString(TEXT("TIME"))
			: Duel.RoundEnd == EChaosImpactDuelEnd::DoubleKnockout ? FString(TEXT("DOUBLE K.O."))
			: Duel.RoundEnd == EChaosImpactDuelEnd::SuddenDeath ? FString(TEXT("K.O."))
			: FString(TEXT("DRAW"));
		const float Pop = 1.0f + 0.3f * FMath::Exp(-12.0f * StepT);
		HudText3D(Call, How, (DesignWidth * 0.5f), 330.0f, 104.0f * Pop, Paper, ETextAlign::Center);
		if (Duel.RoundWinner == 0 || Duel.RoundWinner == 1)
		{
			FPainter{Design, Elements, Layer + 3, TwEase((StepT - 0.4f) / 0.3f)}.Text(FString::Printf(TEXT("%s がラウンドを取った"), *Name(Duel.RoundWinner)),
				(DesignWidth * 0.5f), 470.0f, 28.0f, Gold, ETextAlign::Center, TEXT("Black"), 2.0f, Ink);
		}
		break;
	}
	case EChaosImpactDuelStep::Finished:
	{
		const int32 Winner = Duel.MatchWinner;
		const AChaosImpactPlayerState* Me = GetMe();
		const bool bPlaying = Me && Me->TournamentRole == EChaosImpactTournamentRole::Competitor;
		const bool bIWon = bPlaying && (Winner == 0 || Winner == 1) && Duel.Sides[Winner] == Me;
		const float Pop = 1.0f + 0.35f * FMath::Exp(-10.0f * StepT);
		const FString Big = bPlaying ? FString(bIWon ? TEXT("WIN!") : TEXT("LOSE")) : FString(TEXT("GAME"));
		HudText3D(Call, Big, (DesignWidth * 0.5f), 300.0f, 130.0f * Pop, bPlaying && !bIWon ? Muted : Gold, ETextAlign::Center);
		if (Winner == 0 || Winner == 1)
		{
			const bool bLast = Each.Group == 0 && Each.Round >= Tournament.Rounds;
			const int32 NextMatch = Tournament.GroupMatchIndex(Each.Group, Each.GroupRound + 1, Each.Slot / 2);
			const int32 PlaceWon = Tournament.Groups.IsValidIndex(Each.Group) ? Tournament.Groups[Each.Group].TopPlace : 1;
			const FString Line = bLast ? FString::Printf(TEXT("%s が優勝！"), *Name(Winner))
				: !Tournament.Matches.IsValidIndex(NextMatch) ? FString::Printf(TEXT("%s が %d位！"), *Name(Winner), PlaceWon)
				: FString::Printf(TEXT("%s が %s へ"), *Name(Winner), *Tournament.GetMatchName(NextMatch));
			FPainter{Design, Elements, Layer + 3, TwEase((StepT - 0.5f) / 0.3f)}.Text(Line, (DesignWidth * 0.5f), 460.0f, 32.0f, Paper, ETextAlign::Center, TEXT("Black"), 2.0f, Ink);
		}
		break;
	}
	default:
		break;
	}
	// Watching: whose match it is and how to change.
	if (const AChaosImpactPlayerState* Me = GetMe(); Me && Me->TournamentRole == EChaosImpactTournamentRole::Spectator)
	{
		RoundedBox(P, DesignWidth - 420.0f, 838.0f, 396.0f, 38.0f, 12.0f, FLinearColor(0.008f, 0.012f, 0.03f, 0.7f));
		T.Text(TEXT("観戦中　・　Tab / View：ほかの試合・練習"), DesignWidth - 404.0f, 846.0f, 17.0f, WithAlpha(Paper, 0.85f), ETextAlign::Left, TEXT("Bold"));
	}
}

void UChaosImpactTournamentWidget::PaintAsk(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer) const
{
	const AChaosImpactGameState* Match = GetMatch();
	const AChaosImpactPlayerState* Me = GetMe();
	if (!Match || !Me || !Match->Tournament.Matches.IsValidIndex(Match->Tournament.AskingMatch))
	{
		return;
	}
	const FChaosImpactTournamentState& Tournament = Match->Tournament;
	const FChaosImpactTournamentMatch& Each = Tournament.Matches[Tournament.AskingMatch];
	const auto Name = [&Tournament](const int32 Entrant) { return Tournament.Entrants.IsValidIndex(Entrant) ? Tournament.Entrants[Entrant].Name : FString(); };
	const FPainter P{Design, Elements, Layer};
	if (!Me->bRoomHost)
	{
		RoundedBox(P, DesignWidth * 0.5f - 280.0f, 120.0f, 560.0f, 46.0f, 14.0f, FLinearColor(0.008f, 0.012f, 0.03f, 0.85f));
		FPainter{Design, Elements, Layer + 1}.Text(TEXT("ホストがCPU同士の試合を観るか選んでいます"), (DesignWidth * 0.5f), 130.0f, 19.0f, Paper, ETextAlign::Center,
			TEXT("Bold"));
		return;
	}
	RoundedBox(P, DesignWidth * 0.5f - 360.0f, 330.0f, 720.0f, 290.0f, 24.0f, FLinearColor(0.01f, 0.015f, 0.04f, 0.96f));
	P.Box(DesignWidth * 0.5f - 360.0f, 330.0f, 720.0f, 4.0f, Gold);
	const FPainter T{Design, Elements, Layer + 1};
	T.Text(TEXT("つぎはCPU同士の試合です"), (DesignWidth * 0.5f), 352.0f, 28.0f, Paper, ETextAlign::Center, TEXT("Black"));
	T.Text(FString::Printf(TEXT("%s  vs  %s"), *Name(Each.Entrants[0]), *Name(Each.Entrants[1])), (DesignWidth * 0.5f), 404.0f, 24.0f, Gold, ETextAlign::Center,
		TEXT("BlackItalic"));
	const float Left = FMath::Max(0.0f, ChaosImpactTournament::AskSeconds - static_cast<float>(GetNow() - Tournament.RoundStartedAt));
	T.Text(TEXT("えらばないと観戦になります"), (DesignWidth * 0.5f), 456.0f, 17.0f, WithAlpha(Paper, 0.65f), ETextAlign::Center, TEXT("Bold"));
	(void)Left;
	const TCHAR* Choices[] = {TEXT("観る"), TEXT("とばす"), TEXT("全部とばす")};
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const FBox2D R = AskRect(Index);
		const bool bOn = Index == AskCursor;
		RoundedBox(T, static_cast<float>(R.Min.X), static_cast<float>(R.Min.Y), static_cast<float>(R.GetSize().X), static_cast<float>(R.GetSize().Y), 14.0f,
			bOn ? Gold : FLinearColor(0.05f, 0.07f, 0.15f, 1.0f));
		FPainter{Design, Elements, Layer + 2}.Text(Choices[Index], static_cast<float>(R.GetCenter().X), static_cast<float>(R.Min.Y) + 16.0f, 24.0f,
			bOn ? Ink : Paper, ETextAlign::Center, TEXT("Black"));
	}
}
