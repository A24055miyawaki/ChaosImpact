#include "ChaosImpactRecordBoards.h"

#include "ChaosImpact.h"
#include "ChaosImpactCharacterPreview.h"
#include "ChaosImpactCharacterRoster.h"
#include "ChaosImpactHints.h"
#include "ChaosImpactMatchTypes.h"
#include "ChaosImpactPaint.h"
#include "ChaosImpactParty.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactRelay.h"
#include "ChaosImpactSessionSubsystem.h"
#include "ChaosImpactSettings.h"
#include "ChaosImpactSfx.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Input/Events.h"

namespace
{
	using namespace ChaosImpactPaint;

	const FLinearColor RbBack(0.007f, 0.01f, 0.026f, 1.0f);
	const FLinearColor RbCard(0.028f, 0.04f, 0.085f, 0.96f);
	const FLinearColor RbCardOn(0.07f, 0.1f, 0.2f, 0.98f);
	const FLinearColor RbSilver(0.78f, 0.84f, 0.94f, 1.0f);
	const FLinearColor RbBronze(0.86f, 0.52f, 0.26f, 1.0f);
	constexpr float RbListLeft = 80.0f;
	constexpr float RbTop = 176.0f;

	bool RbIsAny(const FKey& Key, std::initializer_list<FKey> Keys)
	{
		for (const FKey& Each : Keys)
		{
			if (Key == Each)
			{
				return true;
			}
		}
		return false;
	}

	void RbRound(const FPainter& P, const FBox2D& R, const float Radius, const FLinearColor& Color)
	{
		RoundedBox(P, static_cast<float>(R.Min.X), static_cast<float>(R.Min.Y), static_cast<float>(R.GetSize().X), static_cast<float>(R.GetSize().Y), Radius, Color);
	}

	FBox2D RbGrow(const FBox2D& R, const float By)
	{
		return FBox2D(R.Min - FVector2D(By, By), R.Max + FVector2D(By, By));
	}

	/** Text no wider than Width (it shrinks to fit). */
	void RbText(const FPainter& P, const FString& Value, const float X, const float Y, const float Size, const float Width, const FLinearColor& Color,
		const ETextAlign Align = ETextAlign::Left, const FName Face = TEXT("Black"))
	{
		P.Text(Value, X, Y, HudFit(Value, Size, Width, Face), Color, Align, Face);
	}

	FLinearColor RbMedal(const int32 Place)
	{
		return Place == 1 ? Gold : Place == 2 ? RbSilver : Place == 3 ? RbBronze : FLinearColor(0.13f, 0.16f, 0.24f, 1.0f);
	}

	FString RbCharacterName(const int32 Index)
	{
		return ChaosImpactRoster::Get(ChaosImpactRoster::ClampIndex(Index)).Name;
	}

	FString RbStageName(const int32 Stage)
	{
		return Stage < 0 ? FString(ChaosImpactMatch::GetDuelStageName()) : ChaosImpactMatch::GetStageLabel(Stage);
	}

	FDateTime RbLocal(const int64 At)
	{
		return FDateTime::FromUnixTimestamp(At) + (FDateTime::Now() - FDateTime::UtcNow());
	}

	FString RbWhen(const int64 At, const bool bLong)
	{
		if (At <= 0)
		{
			return TEXT("");
		}
		static const TCHAR* const Days[] = {TEXT("月"), TEXT("火"), TEXT("水"), TEXT("木"), TEXT("金"), TEXT("土"), TEXT("日")};
		const FDateTime Local = RbLocal(At);
		const FString Day = Days[FMath::Clamp(static_cast<int32>(Local.GetDayOfWeek()), 0, 6)];
		return bLong ? FString::Printf(TEXT("%d/%d/%d (%s)  %d:%02d"), Local.GetYear(), Local.GetMonth(), Local.GetDay(), *Day, Local.GetHour(), Local.GetMinute())
			: FString::Printf(TEXT("%d/%d (%s)  %d:%02d"), Local.GetMonth(), Local.GetDay(), *Day, Local.GetHour(), Local.GetMinute());
	}

	FString RbDelta(const int32 Delta)
	{
		return Delta > 0 ? FString::Printf(TEXT("+%d"), Delta) : Delta < 0 ? FString::FromInt(Delta) : FString(TEXT("±0"));
	}

	FLinearColor RbDeltaColor(const int32 Delta)
	{
		return Delta > 0 ? FLinearColor(0.35f, 0.85f, 1.0f, 1.0f) : Delta < 0 ? FLinearColor(1.0f, 0.35f, 0.38f, 1.0f) : Muted;
	}

	void RbSpinner(const FPainter& P, const FVector2D& C, const float Radius, const float Time, const FLinearColor& Color)
	{
		P.Ring(C, Radius, WithAlpha(Color, 0.15f), 5.0f, 48);
		P.Arc(C, Radius, Time * 360.0f, Time * 360.0f + 250.0f, Color, 5.0f);
	}

	/** A medal: a disc in its metal with the place on it. */
	void RbMedalDisc(const FPainter& P, const FVector2D& C, const float Radius, const int32 Place, const float Shine)
	{
		const FLinearColor Metal = RbMedal(Place);
		P.Disc(C + FVector2D(2.0, 3.0), Radius, WithAlpha(Ink, 0.6f));
		P.Disc(C, Radius, FMath::Lerp(Metal * 0.55f, Metal, 0.75f));
		HudAbove(P, 1).Disc(C - FVector2D(Radius * 0.12, Radius * 0.12), Radius * 0.78f, Metal);
		HudAbove(P, 1).Arc(C, Radius * 0.66f, 200.0f, 280.0f, WithAlpha(Paper, 0.35f + 0.3f * Shine), Radius * 0.12f);
		const FString Text = FString::FromInt(Place);
		HudAbove(P, 2).Text(Text, static_cast<float>(C.X), static_cast<float>(C.Y) - Radius * 0.62f, HudFit(Text, Radius * 0.95f, Radius * 1.4f),
			Place <= 3 ? Ink : Paper, ETextAlign::Center, TEXT("BlackItalic"));
	}
}

// ---- Opening and closing -----------------------------------------------------------------------------------------------

void UChaosImpactRecordBoards::Open(AChaosImpactPlayerController* InController, const EBoard InBoard, const bool bInDuel)
{
	Close();
	Controller = InController;
	UWorld* World = InController ? InController->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}
	bOpen = true;
	bLeave = false;
	Board = InBoard;
	bDuel = bInDuel;
	OpenedAt = SwitchedAt = SelectedAt = FPlatformTime::Seconds();
	Selected = Scroll = 0;
	Hovered = INDEX_NONE;
	bFailed = false;
	bLoaded[0] = bLoaded[1] = false;
	DuelGames.Reset();
	FfaGames.Reset();
	DuelRows.Reset();
	FfaRows.Reset();
	DuelMine = FfaMine = FMine();
	DuelTotal = FfaTotal = 0;
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Parameters.ObjectFlags |= RF_Transient;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		AChaosImpactCharacterPreview* Stage = World->SpawnActor<AChaosImpactCharacterPreview>(AChaosImpactCharacterPreview::StaticClass(),
			FVector(Index * 1600.0f, -170000.0f, 40000.0f), FRotator::ZeroRotator, Parameters);
		if (Stage)
		{
			Stage->UseAsPortrait();
		}
		Portraits.Add(Stage);
		PortraitBrushes.Add(FSlateBrush());
		PortraitShownAt.Add(-100.0);
		PortraitFor.Add(FString());
	}
	// Both modes asked for at once: switching between them is then instant.
	Request(true, 0);
	Request(false, 0);
}

void UChaosImpactRecordBoards::Close()
{
	for (AChaosImpactCharacterPreview* Stage : Portraits)
	{
		if (IsValid(Stage))
		{
			Stage->Destroy();
		}
	}
	Portraits.Reset();
	PortraitBrushes.Reset();
	PortraitShownAt.Reset();
	PortraitFor.Reset();
	++Generation;
	Pending = 0;
	bOpen = false;
}

bool UChaosImpactRecordBoards::ConsumeLeave()
{
	const bool bWas = bLeave;
	bLeave = false;
	return bWas;
}

bool UChaosImpactRecordBoards::IsLoading() const
{
	return Pending > 0 && !bLoaded[bDuel ? 0 : 1];
}

// ---- The relay -----------------------------------------------------------------------------------------------------------

void UChaosImpactRecordBoards::Request(const bool bForDuel, const int32 Page)
{
	const UChaosImpactSessionSubsystem* Sessions = Controller.IsValid() ? UChaosImpactSessionSubsystem::Get(Controller.Get()) : nullptr;
	if (!Sessions || !Sessions->IsInternet())
	{
		bFailed = true;
		return;
	}
	TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
	Message->SetStringField(TEXT("op"), Board == EBoard::History ? TEXT("record_history") : TEXT("ranking"));
	Message->SetStringField(TEXT("token"), Sessions->GetPlayerToken());
	Message->SetStringField(TEXT("name"), Sessions->GetPlayerName());
	Message->SetStringField(TEXT("mode"), bForDuel ? TEXT("duel") : TEXT("ffa"));
	Message->SetNumberField(TEXT("page"), Page);
	++Pending;
	const int32 Asked = Generation;
	ChaosImpactRelay::Request(Message, [WeakThis = TWeakObjectPtr<UChaosImpactRecordBoards>(this), Asked, bForDuel, Page](TSharedPtr<FJsonObject> Reply, int32)
	{
		UChaosImpactRecordBoards* Self = WeakThis.Get();
		if (!Self || Self->Generation != Asked || !Self->bOpen)
		{
			return;
		}
		Self->Pending = FMath::Max(0, Self->Pending - 1);
		if (!Reply.IsValid() || !Reply->GetBoolField(TEXT("ok")))
		{
			Self->bFailed = !Self->bLoaded[bForDuel ? 0 : 1];
			return;
		}
		const int32 Pages = static_cast<int32>(Reply->GetNumberField(TEXT("pages")));
		if (Self->Board == EBoard::History)
		{
			TArray<FGame> Games;
			ReadGames(Reply, Games);
			TArray<FGame>& Into = bForDuel ? Self->DuelGames : Self->FfaGames;
			if (Page == 0)
			{
				Into.Reset();
			}
			Into.Append(Games);
			Into.SetNum(FMath::Min(Into.Num(), HistoryKeep));
		}
		else
		{
			TArray<FRow> Rows;
			FMine Mine;
			int32 Total = 0;
			ReadRanking(Reply, Rows, Mine, Total);
			TArray<FRow>& Into = bForDuel ? Self->DuelRows : Self->FfaRows;
			if (Page == 0)
			{
				Into.Reset();
			}
			Into.Append(Rows);
			Into.SetNum(FMath::Min(Into.Num(), RankingTop));
			(bForDuel ? Self->DuelMine : Self->FfaMine) = Mine;
			(bForDuel ? Self->DuelTotal : Self->FfaTotal) = Total;
		}
		Self->bLoaded[bForDuel ? 0 : 1] = true;
		Self->ClampScroll();
		// The next page, until every one is in.
		if (Page + 1 < Pages)
		{
			Self->Request(bForDuel, Page + 1);
		}
	});
}

void UChaosImpactRecordBoards::ReadGames(const TSharedPtr<FJsonObject>& Reply, TArray<FGame>& Out)
{
	const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
	if (!Reply.IsValid() || !Reply->TryGetArrayField(TEXT("entries"), Entries))
	{
		return;
	}
	for (const TSharedPtr<FJsonValue>& Value : *Entries)
	{
		const TSharedPtr<FJsonObject> Item = Value.IsValid() ? Value->AsObject() : nullptr;
		if (!Item.IsValid())
		{
			continue;
		}
		FGame Game;
		double Number = 0.0;
		Game.At = Item->TryGetNumberField(TEXT("at"), Number) ? static_cast<int64>(Number) : 0;
		Item->TryGetStringField(TEXT("res"), Game.Result);
		Game.Place = Item->TryGetNumberField(TEXT("pl"), Number) ? static_cast<int32>(Number) : 0;
		Game.Of = Item->TryGetNumberField(TEXT("of"), Number) ? static_cast<int32>(Number) : 0;
		Game.Stage = Item->TryGetNumberField(TEXT("st"), Number) ? static_cast<int32>(Number) : -1;
		Game.Character = Item->TryGetNumberField(TEXT("ch"), Number) ? static_cast<int32>(Number) : 0;
		Game.Colour = Item->TryGetNumberField(TEXT("co"), Number) ? static_cast<int32>(Number) : -1;
		Game.Points = Item->TryGetNumberField(TEXT("pt"), Number) ? static_cast<int32>(Number) : 0;
		Game.Knockouts = Item->TryGetNumberField(TEXT("ko"), Number) ? static_cast<int32>(Number) : 0;
		const TArray<TSharedPtr<FJsonValue>>* Score = nullptr;
		if (Item->TryGetArrayField(TEXT("sc"), Score) && Score->Num() == 2)
		{
			Game.Score[0] = static_cast<int32>((*Score)[0]->AsNumber());
			Game.Score[1] = static_cast<int32>((*Score)[1]->AsNumber());
		}
		const TSharedPtr<FJsonObject>* Opponent = nullptr;
		if (Item->TryGetObjectField(TEXT("op"), Opponent) && Opponent->IsValid())
		{
			(*Opponent)->TryGetStringField(TEXT("n"), Game.Opponent);
			Game.OpponentCharacter = (*Opponent)->TryGetNumberField(TEXT("ch"), Number) ? static_cast<int32>(Number) : 0;
			Game.OpponentColour = (*Opponent)->TryGetNumberField(TEXT("co"), Number) ? static_cast<int32>(Number) : -1;
		}
		const TArray<TSharedPtr<FJsonValue>>* Seats = nullptr;
		if (Item->TryGetArrayField(TEXT("ps"), Seats))
		{
			for (const TSharedPtr<FJsonValue>& SeatValue : *Seats)
			{
				const TArray<TSharedPtr<FJsonValue>>* Row = nullptr;
				if (!SeatValue.IsValid() || !SeatValue->TryGetArray(Row) || Row->Num() < 6)
				{
					continue;
				}
				FGame::FSeat Seat;
				Seat.Name = (*Row)[0]->AsString();
				Seat.Character = static_cast<int32>((*Row)[1]->AsNumber());
				Seat.Colour = static_cast<int32>((*Row)[2]->AsNumber());
				Seat.Place = static_cast<int32>((*Row)[3]->AsNumber());
				Seat.Points = static_cast<int32>((*Row)[4]->AsNumber());
				Seat.bCPU = (*Row)[5]->AsNumber() != 0.0;
				Game.Seats.Add(Seat);
			}
			Game.Seats.StableSort([](const FGame::FSeat& A, const FGame::FSeat& B) { return A.Place < B.Place; });
		}
		if (Item->TryGetNumberField(TEXT("d"), Number))
		{
			Game.bRated = true;
			Game.Delta = static_cast<int32>(Number);
			Game.RateAfter = Item->TryGetNumberField(TEXT("r"), Number) ? static_cast<int32>(Number) : 0;
			bool bMeasured = false;
			Game.bMeasured = Item->TryGetBoolField(TEXT("ms"), bMeasured) && bMeasured;
		}
		Out.Add(MoveTemp(Game));
	}
}

void UChaosImpactRecordBoards::ReadRanking(const TSharedPtr<FJsonObject>& Reply, TArray<FRow>& OutRows, FMine& OutMine, int32& OutTotal)
{
	if (!Reply.IsValid())
	{
		return;
	}
	double Number = 0.0;
	OutTotal = Reply->TryGetNumberField(TEXT("total"), Number) ? static_cast<int32>(Number) : 0;
	const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
	if (Reply->TryGetArrayField(TEXT("entries"), Entries))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Entries)
		{
			const TSharedPtr<FJsonObject> Item = Value.IsValid() ? Value->AsObject() : nullptr;
			if (!Item.IsValid())
			{
				continue;
			}
			FRow Row;
			Row.Rank = Item->TryGetNumberField(TEXT("k"), Number) ? static_cast<int32>(Number) : 0;
			Item->TryGetStringField(TEXT("n"), Row.Name);
			Row.Rate = Item->TryGetNumberField(TEXT("r"), Number) ? static_cast<int32>(Number) : 0;
			Row.Games = Item->TryGetNumberField(TEXT("g"), Number) ? static_cast<int32>(Number) : 0;
			Row.Wins = Item->TryGetNumberField(TEXT("w"), Number) ? static_cast<int32>(Number) : 0;
			Row.Losses = Item->TryGetNumberField(TEXT("l"), Number) ? static_cast<int32>(Number) : 0;
			Row.Top3 = Item->TryGetNumberField(TEXT("t"), Number) ? static_cast<int32>(Number) : 0;
			Row.Character = Item->TryGetNumberField(TEXT("c"), Number) ? static_cast<int32>(Number) : 0;
			Row.Colour = Item->TryGetNumberField(TEXT("o"), Number) ? static_cast<int32>(Number) : -1;
			Item->TryGetBoolField(TEXT("me"), Row.bMe);
			OutRows.Add(Row);
		}
	}
	const TSharedPtr<FJsonObject>* Mine = nullptr;
	if (Reply->TryGetObjectField(TEXT("me"), Mine) && Mine->IsValid())
	{
		OutMine.bKnown = true;
		OutMine.Rank = (*Mine)->TryGetNumberField(TEXT("rank"), Number) ? static_cast<int32>(Number) : 0;
		OutMine.Rate = (*Mine)->TryGetNumberField(TEXT("rate"), Number) ? static_cast<int32>(Number) : 0;
		(*Mine)->TryGetBoolField(TEXT("measured"), OutMine.bMeasured);
		OutMine.Left = (*Mine)->TryGetNumberField(TEXT("left"), Number) ? static_cast<int32>(Number) : 0;
		OutMine.Games = (*Mine)->TryGetNumberField(TEXT("games"), Number) ? static_cast<int32>(Number) : 0;
		OutMine.Best = (*Mine)->TryGetNumberField(TEXT("best"), Number) ? static_cast<int32>(Number) : 0;
	}
}

void UChaosImpactRecordBoards::SetForTests(const bool bForDuel, const TArray<FGame>& Games, const TArray<FRow>& Rows, const FMine& Mine, const int32 Total)
{
	++Generation;
	Pending = 0;
	bFailed = false;
	(bForDuel ? DuelGames : FfaGames) = Games;
	(bForDuel ? DuelRows : FfaRows) = Rows;
	(bForDuel ? DuelMine : FfaMine) = Mine;
	(bForDuel ? DuelTotal : FfaTotal) = Total;
	bLoaded[bForDuel ? 0 : 1] = true;
	ClampScroll();
}

// ---- Moving about --------------------------------------------------------------------------------------------------------

int32 UChaosImpactRecordBoards::GetCount() const
{
	// The ranking's list starts below the podium's three.
	return Board == EBoard::History ? GetGames().Num() : FMath::Max(0, GetRows().Num() - 3);
}

void UChaosImpactRecordBoards::ClampScroll()
{
	const int32 Count = GetCount();
	const int32 Rows = Board == EBoard::History ? HistoryRows : RankingRows;
	Selected = FMath::Clamp(Selected, 0, FMath::Max(0, Count - 1));
	Scroll = FMath::Clamp(Scroll, FMath::Max(0, Selected - Rows + 1), FMath::Min(Selected, FMath::Max(0, Count - Rows)));
}

void UChaosImpactRecordBoards::Select(const int32 Index)
{
	const int32 Before = Selected;
	Selected = Index;
	ClampScroll();
	if (Selected != Before)
	{
		SelectedAt = FPlatformTime::Seconds();
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiMove);
	}
}

void UChaosImpactRecordBoards::Move(const int32 Step)
{
	if (GetCount() > 0)
	{
		Select(FMath::Clamp(Selected + Step, 0, GetCount() - 1));
	}
}

void UChaosImpactRecordBoards::SetDuel(const bool bInDuel)
{
	if (bDuel == bInDuel)
	{
		return;
	}
	bDuel = bInDuel;
	Selected = Scroll = 0;
	SwitchedAt = SelectedAt = FPlatformTime::Seconds();
	ClampScroll();
	ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiTab);
}

void UChaosImpactRecordBoards::Back()
{
	ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiBack);
	bLeave = true;
}

bool UChaosImpactRecordBoards::HandleKeyDown(const FKeyEvent& Event)
{
	if (!bOpen)
	{
		return false;
	}
	const FKey Raw = Event.GetKey();
	if (Raw.IsMouseButton())
	{
		return false;
	}
	const FKey Key = ChaosImpactSettings::ToMenuKey(Raw, Event.GetInputDeviceId().GetId());
	const bool bRepeat = Event.IsRepeat();
	const int32 Page = Board == EBoard::History ? HistoryRows : RankingRows;
	if (RbIsAny(Key, {EKeys::Gamepad_DPad_Up, EKeys::Up, EKeys::W})) { Move(-1); }
	else if (RbIsAny(Key, {EKeys::Gamepad_DPad_Down, EKeys::Down, EKeys::S})) { Move(1); }
	else if (RbIsAny(Key, {EKeys::Gamepad_LeftTrigger, EKeys::PageUp})) { Move(-Page); }
	else if (RbIsAny(Key, {EKeys::Gamepad_RightTrigger, EKeys::PageDown})) { Move(Page); }
	else if (RbIsAny(Key, {EKeys::Home})) { Select(0); }
	else if (RbIsAny(Key, {EKeys::End})) { Select(GetCount() - 1); }
	else if (!bRepeat && RbIsAny(Key, {EKeys::Gamepad_LeftShoulder, EKeys::Q, EKeys::Gamepad_DPad_Left, EKeys::Left, EKeys::A})) { SetDuel(true); }
	else if (!bRepeat && RbIsAny(Key, {EKeys::Gamepad_RightShoulder, EKeys::E, EKeys::Gamepad_DPad_Right, EKeys::Right, EKeys::D})) { SetDuel(false); }
	else if (!bRepeat && RbIsAny(Key, {EKeys::Gamepad_FaceButton_Right, EKeys::Escape, EKeys::BackSpace})) { Back(); }
	else if (!bRepeat && Board == EBoard::Ranking && RbIsAny(Key, {EKeys::Gamepad_FaceButton_Top, EKeys::F}))
	{
		// To this player's own row (when it is in the list).
		const TArray<FRow>& Rows = GetRows();
		for (int32 Index = 3; Index < Rows.Num(); ++Index)
		{
			if (Rows[Index].bMe)
			{
				Select(Index - 3);
				break;
			}
		}
	}
	return true;
}

bool UChaosImpactRecordBoards::HandleAnalog(const FAnalogInputEvent& Event)
{
	if (!bOpen || Event.GetKey() != EKeys::Gamepad_LeftY)
	{
		return bOpen && (Event.GetKey() == EKeys::Gamepad_LeftX || Event.GetKey() == EKeys::Gamepad_RightX || Event.GetKey() == EKeys::Gamepad_RightY);
	}
	StickY = Event.GetAnalogValue();
	const double Now = FPlatformTime::Seconds();
	if (FMath::Abs(StickY) < 0.4f)
	{
		bStickHeld = false;
		return true;
	}
	if (!bStickHeld || Now >= StickRepeatAt)
	{
		StickRepeatAt = Now + (bStickHeld ? 0.09 : 0.32);
		bStickHeld = true;
		Move(StickY > 0.0f ? -1 : 1);
	}
	return true;
}

FBox2D UChaosImpactRecordBoards::RowRect(const int32 Visible) const
{
	if (Board == EBoard::History)
	{
		return FBox2D(FVector2D(RbListLeft, RbTop + Visible * 74.0f), FVector2D(860.0f, RbTop + Visible * 74.0f + 66.0f));
	}
	return FBox2D(FVector2D(830.0f, RbTop + Visible * 66.0f), FVector2D(1490.0f, RbTop + Visible * 66.0f + 58.0f));
}

FBox2D UChaosImpactRecordBoards::TabRect(const int32 Index) const
{
	return FBox2D(FVector2D(RbListLeft + Index * 176.0f, 104.0f), FVector2D(RbListLeft + Index * 176.0f + 164.0f, 150.0f));
}

FBox2D UChaosImpactRecordBoards::BackRect() const
{
	return FBox2D(FVector2D(RbListLeft, 806.0f), FVector2D(RbListLeft + 260.0f, 862.0f));
}

FBox2D UChaosImpactRecordBoards::MineRect() const
{
	return FBox2D(FVector2D(RbListLeft, 610.0f), FVector2D(790.0f, 770.0f));
}

void UChaosImpactRecordBoards::HandleMouseMove(const FVector2D& DesignPoint)
{
	Hovered = INDEX_NONE;
	const int32 Rows = Board == EBoard::History ? HistoryRows : RankingRows;
	for (int32 Visible = 0; Visible < Rows && Scroll + Visible < GetCount(); ++Visible)
	{
		if (RowRect(Visible).IsInside(DesignPoint))
		{
			Hovered = Scroll + Visible;
		}
	}
}

void UChaosImpactRecordBoards::HandleMouseDown(const FVector2D& DesignPoint, const FKey& Button)
{
	if (!bOpen)
	{
		return;
	}
	if (Button == EKeys::RightMouseButton || Button == EKeys::ThumbMouseButton || BackRect().IsInside(DesignPoint))
	{
		Back();
		return;
	}
	if (Button != EKeys::LeftMouseButton)
	{
		return;
	}
	for (int32 Index = 0; Index < 2; ++Index)
	{
		if (TabRect(Index).IsInside(DesignPoint))
		{
			SetDuel(Index == 0);
			return;
		}
	}
	HandleMouseMove(DesignPoint);
	if (Hovered != INDEX_NONE)
	{
		Select(Hovered);
	}
}

void UChaosImpactRecordBoards::HandleWheel(const FVector2D& DesignPoint, const float Delta)
{
	const int32 Rows = Board == EBoard::History ? HistoryRows : RankingRows;
	const int32 Before = Scroll;
	Scroll = FMath::Clamp(Scroll - FMath::RoundToInt(Delta) * 2, 0, FMath::Max(0, GetCount() - Rows));
	// The chosen row comes along (it stays on screen).
	Selected = FMath::Clamp(Selected, Scroll, FMath::Max(Scroll, FMath::Min(GetCount() - 1, Scroll + Rows - 1)));
	if (Scroll != Before)
	{
		SelectedAt = FPlatformTime::Seconds();
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiMove);
	}
	HandleMouseMove(DesignPoint);
}

// ---- The characters filmed --------------------------------------------------------------------------------------------------

void UChaosImpactRecordBoards::ShowPortraits()
{
	struct FWant
	{
		bool bOn = false;
		int32 Character = 0;
		int32 Colour = 0;
		FLinearColor Accent = Ice;
	};
	FWant Wants[3];
	if (Board == EBoard::History)
	{
		const TArray<FGame>& Games = GetGames();
		if (Games.IsValidIndex(Selected))
		{
			const FGame& Game = Games[Selected];
			Wants[0] = {true, Game.Character, FMath::Max(0, Game.Colour), Ice};
			if (bDuel && !Game.Opponent.IsEmpty())
			{
				Wants[1] = {true, Game.OpponentCharacter, FMath::Max(0, Game.OpponentColour), Fire};
			}
		}
	}
	else
	{
		const TArray<FRow>& Rows = GetRows();
		for (int32 Index = 0; Index < 3; ++Index)
		{
			if (Rows.IsValidIndex(Index))
			{
				Wants[Index] = {true, Rows[Index].Character, FMath::Max(0, Rows[Index].Colour), RbMedal(Index + 1)};
			}
		}
	}
	const double Now = FPlatformTime::Seconds();
	for (int32 Index = 0; Index < Portraits.Num(); ++Index)
	{
		AChaosImpactCharacterPreview* Stage = Portraits[Index];
		const FString For = Wants[Index].bOn ? FString::Printf(TEXT("%d/%d"), ChaosImpactRoster::ClampIndex(Wants[Index].Character), Wants[Index].Colour % ChaosImpactRoster::ColourCount)
			: FString();
		if (!IsValid(Stage) || PortraitFor[Index] == For)
		{
			continue;
		}
		PortraitFor[Index] = For;
		if (Wants[Index].bOn)
		{
			Stage->ShowLoadout(ChaosImpactRoster::ClampIndex(Wants[Index].Character), Wants[Index].Colour % ChaosImpactRoster::ColourCount, Wants[Index].Accent);
			Stage->ShowCustomColours(TArray<uint8>());
			// (Facing each other in a 1v1; the podium's three turned a little toward the middle.)
			Stage->SetFacing(Board == EBoard::History ? (Index == 0 ? -24.0f : 24.0f) : (Index == 1 ? -18.0f : Index == 2 ? 18.0f : 0.0f), 0.0f);
			PortraitShownAt[Index] = Now;
		}
	}
}

void UChaosImpactRecordBoards::Tick(const float DeltaSeconds)
{
	if (!bOpen)
	{
		return;
	}
	++Frame;
	ShowPortraits();
	const float Step = FMath::Min(DeltaSeconds, 0.1f);
	const double Now = FPlatformTime::Seconds();
	for (int32 Index = 0; Index < Portraits.Num(); ++Index)
	{
		AChaosImpactCharacterPreview* Stage = Portraits[Index];
		if (!IsValid(Stage) || PortraitFor[Index].IsEmpty())
		{
			continue;
		}
		// Idling (filmed every other frame; every frame for their first moments, while their textures come in).
		Stage->Animate(Step);
		if (Now - PortraitShownAt[Index] < 2.5 || (Frame + Index) % 2 == 0)
		{
			Stage->FilmNow();
			if (UTextureRenderTarget2D* Picture = Stage->GetPicture())
			{
				FSlateBrush& Brush = PortraitBrushes[Index];
				Brush.SetResourceObject(Picture);
				Brush.ImageSize = FVector2D(Picture->SizeX, Picture->SizeY);
				Brush.DrawAs = ESlateBrushDrawType::Image;
			}
		}
	}
}

void UChaosImpactRecordBoards::PaintPortrait(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer, const int32 Index,
	const FBox2D& Box, const float Alpha) const
{
	if (!PortraitBrushes.IsValidIndex(Index) || !PortraitBrushes[Index].GetResourceObject() || PortraitFor[Index].IsEmpty())
	{
		return;
	}
	FSlateBrush Face = PortraitBrushes[Index];
	// The middle of the picture, as wide as the box wants (the picture is 3:4).
	const float Across = FMath::Clamp(static_cast<float>(Box.GetSize().X / Box.GetSize().Y) * 0.75f, 0.2f, 1.0f);
	Face.SetUVRegion(FBox2f(FVector2f(0.5f - Across * 0.5f, 0.0f), FVector2f(0.5f + Across * 0.5f, 1.0f)));
	const float Fade = FMath::Clamp(static_cast<float>(FPlatformTime::Seconds() - PortraitShownAt[Index]) / 0.35f, 0.0f, 1.0f);
	FSlateDrawElement::MakeBox(Elements, Layer, Design.ToPaintGeometry(FVector2f(Box.GetSize()), FSlateLayoutTransform(FVector2f(Box.Min))),
		&Face, ESlateDrawEffect::None, WithAlpha(FLinearColor::White, Alpha * Fade));
}

// ---- Drawing ----------------------------------------------------------------------------------------------------------------

int32 UChaosImpactRecordBoards::Paint(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer) const
{
	if (!bOpen)
	{
		return Layer;
	}
	const double Now = FPlatformTime::Seconds();
	const float Time = static_cast<float>(Now - OpenedAt);
	const FLinearColor Accent = bDuel ? Gold : Fire;
	const FPainter Back{Design, Elements, Layer};
	const FPainter P{Design, Elements, Layer + 2};
	Back.Box(-1400.0f, 0.0f, 4400.0f, 900.0f, RbBack);
	// Slow light behind, in the mode's colour.
	for (int32 Blob = 0; Blob < 3; ++Blob)
	{
		const FLinearColor Wash = Blob == 1 ? Ice : Accent;
		const FVector2D C(260.0 + Blob * 560.0 + 70.0 * FMath::Sin(Time * 0.28f + Blob), 480.0 + 90.0 * FMath::Cos(Time * 0.22f + Blob * 2.0f));
		Back.Disc(C, 320.0f, WithAlpha(Wash, 0.035f));
	}
	// Faint diagonal speed lines.
	for (int32 Line = 0; Line < 9; ++Line)
	{
		const float X = FMath::Fmod(Line * 230.0f + Time * 24.0f, 2100.0f) - 260.0f;
		Back.Line(FVector2D(X, 900.0), FVector2D(X + 260.0, 0.0), WithAlpha(Paper, 0.018f), 18.0f);
	}

	// The heading.
	const bool bHistory = Board == EBoard::History;
	const FString English = bHistory ? TEXT("HISTORY") : TEXT("RANKING");
	const float In = EaseOut(Time / 0.35f);
	HudText3D(P, English, RbListLeft - (1.0f - In) * 60.0f, 20.0f, 56.0f, WithAlpha(Paper, In), ETextAlign::Left, TEXT("BlackItalic"));
	const float HeadRight = RbListLeft + HudMeasure(English, 56.0f, TEXT("BlackItalic")) + 26.0f;
	P.Text(bHistory ? TEXT("対戦履歴") : TEXT("ランキング"), HeadRight, 40.0f, 32.0f, Accent, ETextAlign::Left, TEXT("Black"));
	const FString Sub = bHistory ? FString::Printf(TEXT("ランダムマッチ%sの最近%d戦"), bDuel ? TEXT("1v1") : TEXT("8人"), HistoryKeep)
		: FString::Printf(TEXT("ランダムマッチ%sのレート上位%d人"), bDuel ? TEXT("1v1") : TEXT("8人"), RankingTop);
	RbText(P, Sub, 1520.0f, 50.0f, 18.0f, 520.0f, Muted, ETextAlign::Right, TEXT("Bold"));

	// The tabs: 1v1 / 8人.
	const float Pulse = 0.6f + 0.4f * FMath::Sin(Time * 5.0f);
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const FBox2D R = TabRect(Index);
		const bool bOn = (Index == 0) == bDuel;
		const FLinearColor TabColor = Index == 0 ? Gold : Fire;
		if (bOn)
		{
			RbRound(Back, RbGrow(R, 4.0f), 14.0f, WithAlpha(TabColor, 0.25f + 0.15f * Pulse));
		}
		RbRound(HudAbove(Back, 1), R, 12.0f, bOn ? TabColor : RbCard);
		RbText(P, Index == 0 ? TEXT("1v1") : TEXT("8人"), static_cast<float>(R.GetCenter().X), static_cast<float>(R.Min.Y) + 7.0f, 24.0f,
			static_cast<float>(R.GetSize().X) - 20.0f, bOn ? Ink : Paper, ETextAlign::Center, TEXT("BlackItalic"));
	}
	// Hints in the names of what is being played with right now.
	const EChaosImpactDevice Device = ChaosImpactHints::GetDevice(Controller.Get());
	P.Text(ChaosImpactHints::Text(Device, TEXT("{LB|Q} / {RB|E} ・ ← →")), static_cast<float>(TabRect(0).Min.X), static_cast<float>(TabRect(0).Max.Y) + 3.0f, 12.0f,
		WithAlpha(Muted, 0.85f), ETextAlign::Left, TEXT("Bold"));

	const int32 BodyLayer = Layer + 4;
	if (bHistory)
	{
		PaintHistory(Design, Elements, BodyLayer, Time);
	}
	else
	{
		PaintRanking(Design, Elements, BodyLayer, Time);
	}

	// 戻る and the keys.
	const FPainter Bottom{Design, Elements, Layer + 40};
	const FBox2D BackBox = BackRect();
	RbRound(Bottom, BackBox, 14.0f, WithAlpha(Muted, 0.3f));
	RbRound(HudAbove(Bottom, 1), RbGrow(BackBox, -3.0f), 12.0f, RbCard);
	HudAbove(Bottom, 2).Text(TEXT("戻る"), static_cast<float>(BackBox.GetCenter().X), static_cast<float>(BackBox.Min.Y) + 12.0f, 24.0f, Paper,
		ETextAlign::Center, TEXT("Black"));
	const FString Keys = ChaosImpactHints::Text(Device, bHistory ? TEXT("↑↓ 試合を選ぶ　　{LB|Q} / {RB|E} 1v1・8人　　{Back} 戻る")
		: TEXT("↑↓ 選ぶ　　{LB|Q} / {RB|E} 1v1・8人　　{Y|F} 自分の順位へ　　{Back} 戻る"));
	RbText(Bottom, Keys, 1520.0f, 826.0f, 17.0f, 1100.0f, WithAlpha(Paper, 0.7f), ETextAlign::Right, TEXT("Bold"));
	return Layer + 46;
}

void UChaosImpactRecordBoards::PaintHistory(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer, const float Time) const
{
	const FLinearColor Accent = bDuel ? Gold : Fire;
	const FPainter Back{Design, Elements, Layer};
	const FPainter P{Design, Elements, Layer + 2};
	const FPainter Top{Design, Elements, Layer + 4};
	const TArray<FGame>& Games = GetGames();
	const float Switch = EaseOut(static_cast<float>(FPlatformTime::Seconds() - SwitchedAt) / 0.3f);

	// The summary over the list: the games, the wins (or the 1st places), the rating moved in them.
	if (!Games.IsEmpty())
	{
		int32 Wins = 0, Losses = 0, Draws = 0, Firsts = 0, Top3 = 0, Places = 0, Moved = 0;
		for (const FGame& Game : Games)
		{
			Wins += Game.Result == TEXT("W") ? 1 : 0;
			Losses += Game.Result == TEXT("L") ? 1 : 0;
			Draws += Game.Result == TEXT("D") ? 1 : 0;
			Firsts += Game.Place == 1 ? 1 : 0;
			Top3 += Game.Place >= 1 && Game.Place <= 3 ? 1 : 0;
			Places += Game.Place;
			Moved += Game.bRated && Game.bMeasured ? Game.Delta : 0;
		}
		TArray<TPair<FString, FString>> Chips;
		Chips.Add({TEXT("試合"), FString::FromInt(Games.Num())});
		if (bDuel)
		{
			Chips.Add({TEXT("勝ち・負け"), FString::Printf(TEXT("%d勝 %d敗%s"), Wins, Losses, Draws > 0 ? *FString::Printf(TEXT(" %d分"), Draws) : TEXT(""))});
			Chips.Add({TEXT("勝率"), FString::Printf(TEXT("%d%%"), FMath::RoundToInt(100.0f * Wins / FMath::Max(1, Games.Num())))});
		}
		else
		{
			Chips.Add({TEXT("1位"), FString::Printf(TEXT("%d回"), Firsts)});
			Chips.Add({TEXT("3位以内"), FString::Printf(TEXT("%d%%"), FMath::RoundToInt(100.0f * Top3 / FMath::Max(1, Games.Num())))});
			Chips.Add({TEXT("平均"), FString::Printf(TEXT("%.1f位"), static_cast<float>(Places) / FMath::Max(1, Games.Num()))});
		}
		Chips.Add({TEXT("レートの増減"), RbDelta(Moved)});
		float X = 460.0f;
		for (int32 Index = 0; Index < Chips.Num(); ++Index)
		{
			const float W = FMath::Max(120.0f, HudMeasure(Chips[Index].Value, 22.0f, TEXT("BlackItalic")) + 36.0f);
			RbRound(Back, FBox2D(FVector2D(X, 104.0), FVector2D(X + W, 150.0)), 10.0f, RbCard);
			P.Text(Chips[Index].Key, X + 14.0f, 106.0f, 11.0f, Muted, ETextAlign::Left, TEXT("Bold"));
			const bool bDeltaChip = Index == Chips.Num() - 1;
			RbText(P, Chips[Index].Value, X + 14.0f, 120.0f, 20.0f, W - 24.0f, bDeltaChip ? RbDeltaColor(Moved) : Paper, ETextAlign::Left, TEXT("BlackItalic"));
			X += W + 10.0f;
		}
	}

	// Loading, nothing to show, or no line to the relay.
	const FBox2D ListArea(FVector2D(RbListLeft, RbTop), FVector2D(1520.0, 770.0));
	if (Games.IsEmpty())
	{
		RbRound(Back, ListArea, 20.0f, WithAlpha(RbCard, 0.75f));
		const FVector2D C = ListArea.GetCenter();
		if (IsLoading())
		{
			RbSpinner(P, C - FVector2D(0.0, 30.0), 26.0f, Time, Accent);
			P.Text(TEXT("履歴を読みこんでいます"), static_cast<float>(C.X), static_cast<float>(C.Y) + 20.0f, 24.0f, Paper, ETextAlign::Center, TEXT("Bold"));
		}
		else if (bFailed && !bLoaded[bDuel ? 0 : 1])
		{
			P.Text(TEXT("中継サーバーにつながりません"), static_cast<float>(C.X), static_cast<float>(C.Y) - 20.0f, 30.0f, Paper, ETextAlign::Center, TEXT("Black"));
		}
		else
		{
			P.Text(TEXT("まだ対戦していません"), static_cast<float>(C.X), static_cast<float>(C.Y) - 34.0f, 32.0f, Paper, ETextAlign::Center, TEXT("Black"));
			const FString Hint = FString::Printf(TEXT("ランダムマッチ%sで遊ぶと、最近%d戦がここに記録されます"), bDuel ? TEXT("1v1") : TEXT("8人"), HistoryKeep);
			RbText(P, Hint, static_cast<float>(C.X), static_cast<float>(C.Y) + 16.0f, 20.0f, 1100.0f, Muted, ETextAlign::Center, TEXT("Bold"));
		}
		return;
	}

	// The list: the newest first.
	const double Now = FPlatformTime::Seconds();
	const float Pick = EaseOut(static_cast<float>(Now - SelectedAt) / 0.18f);
	for (int32 Visible = 0; Visible < HistoryRows && Scroll + Visible < Games.Num(); ++Visible)
	{
		const int32 Index = Scroll + Visible;
		const FGame& Game = Games[Index];
		const bool bOn = Index == Selected;
		const float Enter = EaseOut((Switch * 1.6f) - Visible * 0.06f);
		const float Slide = (bOn ? 14.0f * Pick : 0.0f) + (1.0f - Enter) * 50.0f;
		FBox2D R = RowRect(Visible);
		R = FBox2D(R.Min + FVector2D(Slide, 0.0), R.Max + FVector2D(Slide, 0.0));
		const FPainter Row{Design, Elements, Layer, Enter};
		const FPainter RowP = HudAbove(Row, 2);
		const FPainter RowTop = HudAbove(Row, 4);
		if (bOn)
		{
			RbRound(Row, RbGrow(R, 3.0f), 14.0f, WithAlpha(Accent, 0.55f + 0.25f * FMath::Sin(Time * 5.0f)));
		}
		else if (Index == Hovered)
		{
			RbRound(Row, RbGrow(R, 2.0f), 13.0f, WithAlpha(Paper, 0.2f));
		}
		RbRound(HudAbove(Row, 1), R, 12.0f, bOn ? RbCardOn : RbCard);
		const float X = static_cast<float>(R.Min.X);
		const float Y = static_cast<float>(R.Min.Y);
		const float W = static_cast<float>(R.GetSize().X);
		// The result: ○ × △ on its colour, or the place as a medal.
		const FBox2D Badge(FVector2D(X + 10.0f, Y + 9.0f), FVector2D(X + 58.0f, Y + 57.0f));
		if (bDuel)
		{
			const bool bWin = Game.Result == TEXT("W");
			const bool bDraw = Game.Result == TEXT("D");
			RbRound(RowP, Badge, 10.0f, bWin ? Gold : bDraw ? WithAlpha(Ice, 0.75f) : FLinearColor(0.16f, 0.18f, 0.26f, 1.0f));
			RowTop.Text(bWin ? TEXT("○") : bDraw ? TEXT("△") : TEXT("×"), static_cast<float>(Badge.GetCenter().X), Y + 13.0f, 28.0f, bWin ? Ink : Paper,
				ETextAlign::Center, TEXT("Black"));
		}
		else
		{
			RbMedalDisc(RowP, Badge.GetCenter(), 24.0f, FMath::Max(1, Game.Place), bOn ? 1.0f : 0.4f);
		}
		// What happened: the opponent (1v1) or the place and points (8人); when and where under it.
		const FString Main = bDuel ? FString::Printf(TEXT("vs  %s"), Game.Opponent.IsEmpty() ? TEXT("？？？") : *Game.Opponent)
			: FString::Printf(TEXT("%d人中 %d位　%dpt"), Game.Of, Game.Place, Game.Points);
		RbText(RowP, Main, X + 74.0f, Y + 7.0f, 23.0f, W - (bDuel ? 350.0f : 250.0f), Paper, ETextAlign::Left, TEXT("Black"));
		const FString When = FString::Printf(TEXT("%s　%s"), *RbWhen(Game.At, false), *RbStageName(Game.Stage));
		RbText(RowP, When, X + 76.0f, Y + 39.0f, 14.0f, W - 290.0f, Muted, ETextAlign::Left, TEXT("Bold"));
		// 1v1: the rounds.
		if (bDuel && Game.Score[0] >= 0)
		{
			const FString Score = FString::Printf(TEXT("%d - %d"), Game.Score[0], Game.Score[1]);
			RowP.Text(Score, X + W - 200.0f, Y + 16.0f, 22.0f, WithAlpha(Paper, 0.85f), ETextAlign::Center, TEXT("BlackItalic"));
		}
		// The rating it moved.
		const FString Rate = !Game.bRated ? FString(TEXT("―")) : !Game.bMeasured ? FString(TEXT("計測中")) : RbDelta(Game.Delta);
		const FLinearColor RateColor = Game.bRated && Game.bMeasured ? RbDeltaColor(Game.Delta) : Muted;
		RbText(RowTop, Rate, X + W - 22.0f, Y + (Game.bRated && Game.bMeasured ? 12.0f : 20.0f), Game.bRated && Game.bMeasured ? 28.0f : 17.0f, 110.0f,
			RateColor, ETextAlign::Right, TEXT("BlackItalic"));
	}
	// The scroll bar.
	if (Games.Num() > HistoryRows)
	{
		const float BarTop = RbTop;
		const float BarH = HistoryRows * 74.0f - 8.0f;
		RoundedBox(Back, 872.0f, BarTop, 6.0f, BarH, 3.0f, WithAlpha(Paper, 0.08f));
		const float Share = static_cast<float>(HistoryRows) / Games.Num();
		const float Start = static_cast<float>(Scroll) / Games.Num();
		RoundedBox(P, 872.0f, BarTop + BarH * Start, 6.0f, FMath::Max(24.0f, BarH * Share), 3.0f, WithAlpha(Accent, 0.85f));
	}
	P.Text(FString::Printf(TEXT("%d / %d"), Selected + 1, Games.Num()), 860.0f, RbTop + HistoryRows * 74.0f + 2.0f, 14.0f, Muted,
		ETextAlign::Right, TEXT("Bold"));

	// The chosen game in detail.
	if (!Games.IsValidIndex(Selected))
	{
		return;
	}
	const FGame& Game = Games[Selected];
	const FBox2D Panel(FVector2D(900.0, RbTop), FVector2D(1520.0, 770.0));
	const float Show = EaseOut(static_cast<float>(Now - SelectedAt) / 0.25f);
	const FPainter D{Design, Elements, Layer, Show};
	const FPainter DP = HudAbove(D, 2);
	const FPainter DT = HudAbove(D, 6);
	RoundedBox(D, static_cast<float>(Panel.Min.X) + 10.0f, static_cast<float>(Panel.Min.Y) + 12.0f, static_cast<float>(Panel.GetSize().X),
		static_cast<float>(Panel.GetSize().Y), 20.0f, WithAlpha(FLinearColor::Black, 0.45f));
	RbRound(HudAbove(D, 1), Panel, 20.0f, RbCard);
	const float PX = static_cast<float>(Panel.Min.X);
	const float PY = static_cast<float>(Panel.Min.Y);
	const float PW = static_cast<float>(Panel.GetSize().X);
	// The result, big, in its colour; a band across the top.
	const bool bWin = bDuel ? Game.Result == TEXT("W") : Game.Place == 1;
	const bool bDraw = bDuel && Game.Result == TEXT("D");
	const FLinearColor ResultColor = bDuel ? (bWin ? Gold : bDraw ? Ice : FLinearColor(0.62f, 0.68f, 0.8f, 1.0f)) : Game.Place <= 3 ? RbMedal(Game.Place) : FLinearColor(0.78f, 0.83f, 0.93f, 1.0f);
	RoundedBox(DP, PX, PY, PW, 8.0f, 4.0f, ResultColor);
	const FString Big = bDuel ? (bWin ? TEXT("WIN") : bDraw ? TEXT("DRAW") : TEXT("LOSE")) : FString::Printf(TEXT("%d位"), Game.Place);
	HudText3D(DT, Big, PX + 30.0f - (1.0f - Show) * 30.0f, PY + 22.0f, HudFit(Big, 64.0f, 260.0f, TEXT("BlackItalic")), ResultColor, ETextAlign::Left, TEXT("BlackItalic"));
	if (!bDuel)
	{
		DP.Text(FString::Printf(TEXT("/ %d人"), Game.Of), PX + 34.0f + HudMeasure(Big, 64.0f, TEXT("BlackItalic")), PY + 56.0f, 24.0f, Muted, ETextAlign::Left, TEXT("Black"));
	}
	RbText(DP, RbWhen(Game.At, true), PX + PW - 30.0f, PY + 28.0f, 18.0f, 300.0f, Paper, ETextAlign::Right, TEXT("Bold"));
	RbText(DP, RbStageName(Game.Stage), PX + PW - 30.0f, PY + 56.0f, 16.0f, 300.0f, Muted, ETextAlign::Right, TEXT("Bold"));

	const float BodyTop = PY + 126.0f;
	if (bDuel)
	{
		// The two characters facing each other, the rounds between them.
		const FBox2D Left(FVector2D(PX + 24.0f, BodyTop), FVector2D(PX + 224.0f, BodyTop + 250.0f));
		const FBox2D Right(FVector2D(PX + PW - 224.0f, BodyTop), FVector2D(PX + PW - 24.0f, BodyTop + 250.0f));
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const FBox2D& Box = Side == 0 ? Left : Right;
			const FLinearColor SideColor = Side == 0 ? Ice : Fire;
			RbRound(HudAbove(D, 1), RbGrow(Box, 3.0f), 16.0f, WithAlpha(SideColor, 0.55f));
			RbRound(HudAbove(D, 2), Box, 14.0f, FLinearColor(0.012f, 0.02f, 0.05f, 1.0f));
			PaintPortrait(Design, Elements, Layer + 3, Side, Box, Show);
			const FString Name = Side == 0 ? (Controller.IsValid() && UChaosImpactSessionSubsystem::Get(Controller.Get())
				? UChaosImpactSessionSubsystem::Get(Controller.Get())->GetPlayerName() : FString(TEXT("あなた"))) : Game.Opponent;
			const float CX = static_cast<float>(Box.GetCenter().X);
			RbText(DT, Name.IsEmpty() ? TEXT("？？？") : Name, CX, static_cast<float>(Box.Max.Y) + 10.0f, 22.0f, 200.0f, Paper, ETextAlign::Center, TEXT("Black"));
			RbText(DT, RbCharacterName(Side == 0 ? Game.Character : Game.OpponentCharacter), CX, static_cast<float>(Box.Max.Y) + 40.0f, 14.0f, 200.0f, Muted,
				ETextAlign::Center, TEXT("Bold"));
			DT.Text(Side == 0 ? TEXT("YOU") : TEXT("RIVAL"), static_cast<float>(Box.Min.X) + 12.0f, static_cast<float>(Box.Min.Y) + 8.0f, 13.0f, SideColor,
				ETextAlign::Left, TEXT("BlackItalic"));
		}
		const float MidX = PX + PW * 0.5f;
		HudText3D(DT, TEXT("VS"), MidX, BodyTop + 36.0f, 40.0f, WithAlpha(Paper, 0.9f), ETextAlign::Center, TEXT("BlackItalic"));
		if (Game.Score[0] >= 0)
		{
			const FString Score = FString::Printf(TEXT("%d - %d"), Game.Score[0], Game.Score[1]);
			HudText3D(DT, Score, MidX, BodyTop + 110.0f, 46.0f, bWin ? Gold : Paper, ETextAlign::Center, TEXT("BlackItalic"));
			DP.Text(TEXT("ラウンド"), MidX, BodyTop + 172.0f, 14.0f, Muted, ETextAlign::Center, TEXT("Bold"));
		}
	}
	else
	{
		// This player's character, and everyone's places beside it.
		const FBox2D Box(FVector2D(PX + 24.0f, BodyTop), FVector2D(PX + 204.0f, BodyTop + 240.0f));
		RbRound(HudAbove(D, 1), RbGrow(Box, 3.0f), 16.0f, WithAlpha(RbMedal(Game.Place), 0.6f));
		RbRound(HudAbove(D, 2), Box, 14.0f, FLinearColor(0.012f, 0.02f, 0.05f, 1.0f));
		PaintPortrait(Design, Elements, Layer + 3, 0, Box, Show);
		const float CX = static_cast<float>(Box.GetCenter().X);
		RbText(DT, RbCharacterName(Game.Character), CX, static_cast<float>(Box.Max.Y) + 10.0f, 16.0f, 180.0f, Paper, ETextAlign::Center, TEXT("Black"));
		RbText(DT, FString::Printf(TEXT("%dpt　%dKO"), Game.Points, Game.Knockouts), CX, static_cast<float>(Box.Max.Y) + 36.0f, 15.0f, 180.0f, Muted,
			ETextAlign::Center, TEXT("Bold"));
		const float TX = PX + 224.0f;
		const float TW = PW - 224.0f - 22.0f;
		const FString MyName = Controller.IsValid() && UChaosImpactSessionSubsystem::Get(Controller.Get())
			? UChaosImpactSessionSubsystem::Get(Controller.Get())->GetPlayerName() : FString();
		bool bMeShown = false;
		for (int32 Seat = 0; Seat < Game.Seats.Num() && Seat < 8; ++Seat)
		{
			const FGame::FSeat& Who = Game.Seats[Seat];
			const float Y = BodyTop + Seat * 37.0f;
			const bool bMe = !bMeShown && Who.Place == Game.Place && !Who.bCPU && (MyName.IsEmpty() || Who.Name == MyName);
			bMeShown |= bMe;
			RoundedBox(HudAbove(D, 1), TX, Y, TW, 32.0f, 8.0f, bMe ? WithAlpha(Accent, 0.32f) : WithAlpha(Paper, Seat % 2 ? 0.03f : 0.06f));
			RbMedalDisc(DP, FVector2D(TX + 18.0f, Y + 16.0f), 13.0f, Who.Place, 0.5f);
			// Name (a CPU marked after it), character, points: each in its own column.
			const float NameW = TW * 0.44f - (Who.bCPU ? 44.0f : 0.0f);
			RbText(DT, Who.Name, TX + 40.0f, Y + 5.0f, 17.0f, NameW, bMe ? Gold : Paper, ETextAlign::Left, TEXT("Black"));
			if (Who.bCPU)
			{
				const float After = TX + 40.0f + FMath::Min(NameW, HudMeasure(Who.Name, 17.0f)) + 6.0f;
				RoundedBox(DP, After, Y + 8.0f, 36.0f, 17.0f, 5.0f, WithAlpha(Muted, 0.4f));
				DT.Text(TEXT("CPU"), After + 18.0f, Y + 9.0f, 10.0f, Paper, ETextAlign::Center, TEXT("Black"));
			}
			RbText(DT, RbCharacterName(Who.Character), TX + TW * 0.56f, Y + 8.0f, 13.0f, TW * 0.22f, Muted, ETextAlign::Left, TEXT("Bold"));
			RbText(DT, FString::Printf(TEXT("%dpt"), Who.Points), TX + TW - 10.0f, Y + 6.0f, 16.0f, TW * 0.2f, WithAlpha(Paper, 0.85f), ETextAlign::Right,
				TEXT("BlackItalic"));
		}
		if (Game.Seats.IsEmpty())
		{
			RbText(DP, TEXT("この試合の順位表はありません"), TX + TW * 0.5f, BodyTop + 110.0f, 18.0f, TW, Muted, ETextAlign::Center, TEXT("Bold"));
		}
	}

	// The rating this game moved.
	const FBox2D RateBox(FVector2D(PX + 24.0f, PY + 470.0f), FVector2D(PX + PW - 24.0f, PY + 570.0f));
	RbRound(HudAbove(D, 1), RateBox, 14.0f, FLinearColor(0.015f, 0.022f, 0.05f, 1.0f));
	const float RX = static_cast<float>(RateBox.Min.X);
	const float RY = static_cast<float>(RateBox.Min.Y);
	const float RW = static_cast<float>(RateBox.GetSize().X);
	DP.Text(TEXT("レート"), RX + 22.0f, RY + 12.0f, 14.0f, Muted, ETextAlign::Left, TEXT("Bold"));
	if (!Game.bRated)
	{
		RbText(DT, TEXT("この試合のレートの記録はありません"), RX + RW * 0.5f, RY + 38.0f, 20.0f, RW - 40.0f, Muted, ETextAlign::Center, TEXT("Bold"));
	}
	else if (!Game.bMeasured)
	{
		RbText(DT, TEXT("計測中の試合（レートが決まる前）"), RX + RW * 0.5f, RY + 38.0f, 22.0f, RW - 40.0f, Paper, ETextAlign::Center, TEXT("Black"));
	}
	else
	{
		const int32 Before = Game.RateAfter - Game.Delta;
		const FString Line = FString::Printf(TEXT("%d  →  %d"), Before, Game.RateAfter);
		HudText3D(DT, Line, RX + 28.0f, RY + 34.0f, HudFit(Line, 40.0f, RW * 0.6f, TEXT("BlackItalic")), Paper, ETextAlign::Left, TEXT("BlackItalic"));
		HudText3D(DT, RbDelta(Game.Delta), RX + RW - 28.0f, RY + 30.0f, 46.0f, RbDeltaColor(Game.Delta), ETextAlign::Right, TEXT("BlackItalic"));
	}
}

void UChaosImpactRecordBoards::PaintRanking(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer, const float Time) const
{
	const FLinearColor Accent = bDuel ? Gold : Fire;
	const FPainter Back{Design, Elements, Layer};
	const FPainter P{Design, Elements, Layer + 2};
	const FPainter Top{Design, Elements, Layer + 6};
	const TArray<FRow>& Rows = GetRows();
	const FMine& Mine = GetMine();
	const double Now = FPlatformTime::Seconds();
	const float Switch = static_cast<float>(Now - SwitchedAt);

	// How many are ranked.
	if (GetRankedTotal() > 0)
	{
		const FString Count = FString::Printf(TEXT("全%d人がランクイン"), GetRankedTotal());
		const float W = HudMeasure(Count, 18.0f, TEXT("Black")) + 34.0f;
		RbRound(Back, FBox2D(FVector2D(460.0, 104.0), FVector2D(460.0 + W, 150.0)), 10.0f, RbCard);
		P.Text(Count, 477.0f, 116.0f, 18.0f, Paper, ETextAlign::Left, TEXT("Black"));
	}

	// The podium: 2nd, 1st, 3rd, rising in turn.
	const FBox2D Stage(FVector2D(RbListLeft, RbTop), FVector2D(790.0, 590.0));
	RbRound(Back, Stage, 22.0f, WithAlpha(RbCard, 0.7f));
	if (Rows.IsEmpty())
	{
		const FVector2D C = Stage.GetCenter();
		if (IsLoading())
		{
			RbSpinner(P, C - FVector2D(0.0, 30.0), 26.0f, Time, Accent);
			P.Text(TEXT("ランキングを読みこんでいます"), static_cast<float>(C.X), static_cast<float>(C.Y) + 20.0f, 22.0f, Paper, ETextAlign::Center, TEXT("Bold"));
		}
		else if (bFailed && !bLoaded[bDuel ? 0 : 1])
		{
			P.Text(TEXT("中継サーバーにつながりません"), static_cast<float>(C.X), static_cast<float>(C.Y) - 16.0f, 28.0f, Paper, ETextAlign::Center, TEXT("Black"));
		}
		else
		{
			P.Text(TEXT("まだランクインした人がいません"), static_cast<float>(C.X), static_cast<float>(C.Y) - 30.0f, 28.0f, Paper, ETextAlign::Center, TEXT("Black"));
			RbText(P, TEXT("レートが決まった人から順に、ここに並びます"), static_cast<float>(C.X), static_cast<float>(C.Y) + 14.0f, 18.0f, 600.0f, Muted,
				ETextAlign::Center, TEXT("Bold"));
		}
	}
	else
	{
		// A spotlight down onto the 1st place's block.
		{
			const float Glow = 0.07f + 0.02f * FMath::Sin(Time * 2.0f);
			const FPainter Beam{Design, Elements, Layer, 1.0f};
			HudPoly(Beam, {FVector2D(395.0, RbTop + 6.0), FVector2D(475.0, RbTop + 6.0), FVector2D(560.0, 438.0), FVector2D(310.0, 438.0)},
				MakeFill(WithAlpha(Accent, 0.0f), WithAlpha(Accent, Glow)));
		}
		struct FPlace
		{
			int32 Index;
			float Left;
			float Height;
		};
		const FPlace Places[] = {{1, 104.0f, 124.0f}, {0, 330.0f, 154.0f}, {2, 556.0f, 104.0f}};
		const float Floor = 570.0f;
		for (int32 Order = 0; Order < 3; ++Order)
		{
			const FPlace& Place = Places[Order];
			const float Rise = EaseOut((Switch - 0.08f * (Order == 1 ? 2 : Order == 0 ? 1 : 0)) / 0.45f);
			const float W = 210.0f;
			const float H = Place.Height * Rise;
			const float X = Place.Left;
			const int32 Rank = Place.Index + 1;
			const FLinearColor Metal = RbMedal(Rank);
			const FPainter Pd{Design, Elements, Layer + 1, Rise};
			// The block: lit from above in its metal.
			HudPoly(Pd, {FVector2D(X, Floor - H), FVector2D(X + W, Floor - H), FVector2D(X + W, Floor), FVector2D(X, Floor)},
				MakeFill(FMath::Lerp(Metal, Paper, 0.15f) * 0.75f, Metal * 0.22f));
			Pd.Box(X, Floor - H, W, 5.0f, FMath::Lerp(Metal, Paper, 0.4f));
			const bool bThere = Rows.IsValidIndex(Place.Index);
			// The medal sits on the block's top edge (at the character's feet).
			RbMedalDisc(HudAbove(Pd, 5), FVector2D(X + W * 0.5f, Floor - H), 22.0f, bThere ? Rows[Place.Index].Rank : Rank, 0.8f);
			if (!bThere)
			{
				RbText(HudAbove(Pd, 3), TEXT("―"), X + W * 0.5f, Floor - H + 34.0f, 26.0f, W, Muted, ETextAlign::Center, TEXT("Black"));
				continue;
			}
			const FRow& Row = Rows[Place.Index];
			// The character standing on it.
			const FBox2D Picture(FVector2D(X + 14.0f, Floor - H - 226.0f), FVector2D(X + W - 14.0f, Floor - H - 4.0f));
			PaintPortrait(Design, Elements, Layer + 2, Place.Index, Picture, Rise);
			if (Rank == 1)
			{
				ChaosImpactParty::PaintCrown(HudAbove(Pd, 6), FVector2D(X + W * 0.5f, Floor - H - 238.0f + FMath::Sin(Time * 2.4f) * 3.0f), 20.0f, Time, 1.0f);
			}
			// Name and rating on the block's face (or just over a short one).
			const FPainter Words = HudAbove(Pd, 4);
			const float NameY = Floor - H + 28.0f;
			RbText(Words, Row.Name, X + W * 0.5f, NameY, 19.0f, W - 24.0f, Row.bMe ? Gold : Paper, ETextAlign::Center, TEXT("Black"));
			const FString Rate = FString::FromInt(Row.Rate);
			HudText3D(Words, Rate, X + W * 0.5f, NameY + 28.0f, HudFit(Rate, 28.0f, W - 40.0f, TEXT("BlackItalic")), FMath::Lerp(Metal, Paper, 0.25f),
				ETextAlign::Center, TEXT("BlackItalic"));
		}
		Top.Text(TEXT("TOP 3"), RbListLeft + 22.0f, RbTop + 14.0f, 20.0f, WithAlpha(Accent, 0.9f), ETextAlign::Left, TEXT("BlackItalic"));
	}

	// This player's own place.
	{
		const FBox2D R = MineRect();
		const float X = static_cast<float>(R.Min.X);
		const float Y = static_cast<float>(R.Min.Y);
		const float W = static_cast<float>(R.GetSize().X);
		RbRound(Back, RbGrow(R, 3.0f), 20.0f, WithAlpha(Accent, 0.45f));
		RbRound(HudAbove(Back, 1), R, 18.0f, RbCardOn);
		P.Text(TEXT("あなたの順位"), X + 26.0f, Y + 16.0f, 16.0f, Muted, ETextAlign::Left, TEXT("Bold"));
		if (!Mine.bKnown)
		{
			RbText(P, IsLoading() ? TEXT("読みこんでいます") : TEXT("―"), X + 26.0f, Y + 60.0f, 26.0f, W - 52.0f, Paper, ETextAlign::Left, TEXT("Black"));
		}
		else if (!Mine.bMeasured)
		{
			const FString Line = Mine.Games > 0 ? FString::Printf(TEXT("計測中…　あと%d戦でランクイン"), Mine.Left)
				: FString::Printf(TEXT("ランダムマッチ%sを%d試合遊ぶとランクイン"), bDuel ? TEXT("1v1") : TEXT("8人"), Mine.Left);
			RbText(P, Line, X + 26.0f, Y + 52.0f, 28.0f, W - 52.0f, Paper, ETextAlign::Left, TEXT("Black"));
			// The measuring games as pips.
			const int32 Played = FMath::Max(0, 5 - Mine.Left);
			for (int32 Pip = 0; Pip < 5; ++Pip)
			{
				const FVector2D C(X + 40.0f + Pip * 34.0f, Y + 122.0f);
				P.Ring(C, 11.0f, WithAlpha(Paper, 0.35f), 2.5f, 24);
				if (Pip < Played)
				{
					HudAbove(P, 1).Disc(C, 8.0f, Accent);
				}
			}
		}
		else
		{
			const FString Rank = Mine.Rank > 0 ? FString::Printf(TEXT("%d位"), Mine.Rank) : FString(TEXT("―"));
			HudText3D(Top, Rank, X + 26.0f, Y + 42.0f, HudFit(Rank, 64.0f, 230.0f, TEXT("BlackItalic")), Mine.Rank > 0 && Mine.Rank <= 3 ? RbMedal(Mine.Rank) : Paper,
				ETextAlign::Left, TEXT("BlackItalic"));
			const float After = X + 44.0f + FMath::Min(230.0f, HudMeasure(Rank, 64.0f, TEXT("BlackItalic")));
			P.Text(FString::Printf(TEXT("/ %d人"), GetRankedTotal()), After, Y + 64.0f, 20.0f, Muted, ETextAlign::Left, TEXT("Black"));
			if (Mine.Rank > 0 && GetRankedTotal() > 0)
			{
				const int32 Percent = FMath::Max(1, FMath::CeilToInt(100.0f * Mine.Rank / GetRankedTotal()));
				const FString Share = FString::Printf(TEXT("上位 %d%%"), Percent);
				const float ShareW = HudMeasure(Share, 16.0f, TEXT("Black")) + 20.0f;
				RoundedBox(P, After, Y + 100.0f, ShareW, 28.0f, 8.0f, WithAlpha(Accent, 0.9f));
				HudAbove(P, 1).Text(Share, After + 10.0f, Y + 103.0f, 16.0f, Ink, ETextAlign::Left, TEXT("Black"));
			}
			P.Text(TEXT("レート"), X + W - 30.0f, Y + 16.0f, 14.0f, Muted, ETextAlign::Right, TEXT("Bold"));
			const FString Rate = FString::FromInt(Mine.Rate);
			HudText3D(Top, Rate, X + W - 30.0f, Y + 38.0f, 52.0f, Accent, ETextAlign::Right, TEXT("BlackItalic"));
			P.Text(FString::Printf(TEXT("最高 %d　・　%d試合"), Mine.Best, Mine.Games), X + W - 30.0f, Y + 112.0f, 15.0f, Muted, ETextAlign::Right, TEXT("Bold"));
		}
	}

	// The list from 4th on.
	const FBox2D ListBox(FVector2D(820.0, RbTop - 6.0), FVector2D(1520.0, RbTop + RankingRows * 66.0f));
	RbRound(Back, ListBox, 18.0f, WithAlpha(RbCard, 0.55f));
	const int32 Count = GetCount();
	if (Count == 0 && !Rows.IsEmpty())
	{
		RbText(P, TEXT("4位から下は、まだいません"), static_cast<float>(ListBox.GetCenter().X), static_cast<float>(ListBox.GetCenter().Y) - 14.0f, 22.0f, 600.0f,
			Muted, ETextAlign::Center, TEXT("Bold"));
	}
	const float Pick = EaseOut(static_cast<float>(Now - SelectedAt) / 0.18f);
	for (int32 Visible = 0; Visible < RankingRows && Scroll + Visible < Count; ++Visible)
	{
		const int32 Index = Scroll + Visible;
		const FRow& Row = Rows[Index + 3];
		const bool bOn = Index == Selected;
		const float Enter = EaseOut(Switch / 0.3f * 1.6f - Visible * 0.05f);
		FBox2D R = RowRect(Visible);
		const float Slide = (bOn ? -10.0f * Pick : 0.0f) + (1.0f - Enter) * 50.0f;
		R = FBox2D(R.Min + FVector2D(Slide, 0.0), R.Max + FVector2D(Slide, 0.0));
		const FPainter Row0{Design, Elements, Layer + 1, Enter};
		const FPainter RowP = HudAbove(Row0, 2);
		const FPainter RowTop = HudAbove(Row0, 4);
		if (bOn)
		{
			RbRound(Row0, RbGrow(R, 3.0f), 13.0f, WithAlpha(Accent, 0.55f + 0.25f * FMath::Sin(Time * 5.0f)));
		}
		else if (Row.bMe)
		{
			RbRound(Row0, RbGrow(R, 2.0f), 12.0f, WithAlpha(Gold, 0.5f));
		}
		else if (Index == Hovered)
		{
			RbRound(Row0, RbGrow(R, 2.0f), 12.0f, WithAlpha(Paper, 0.2f));
		}
		RbRound(HudAbove(Row0, 1), R, 11.0f, bOn ? RbCardOn : Row.bMe ? FLinearColor(0.1f, 0.08f, 0.03f, 0.98f) : RbCard);
		const float X = static_cast<float>(R.Min.X);
		const float Y = static_cast<float>(R.Min.Y);
		const float W = static_cast<float>(R.GetSize().X);
		const FString RankText = FString::FromInt(Row.Rank);
		HudText3D(RowTop, RankText, X + 46.0f, Y + 9.0f, HudFit(RankText, 30.0f, 70.0f, TEXT("BlackItalic")), Row.Rank <= 10 ? Accent : Paper, ETextAlign::Center,
			TEXT("BlackItalic"));
		RowP.Box(X + 90.0f, Y + 12.0f, 2.0f, 34.0f, WithAlpha(Paper, 0.12f));
		const float NameW = W * 0.36f - (Row.bMe ? 56.0f : 0.0f);
		RbText(RowP, Row.Name, X + 108.0f, Y + 5.0f, 22.0f, NameW, Row.bMe ? Gold : Paper, ETextAlign::Left, TEXT("Black"));
		RbText(RowP, RbCharacterName(Row.Character), X + 110.0f, Y + 34.0f, 13.0f, W * 0.36f, Muted, ETextAlign::Left, TEXT("Bold"));
		if (Row.bMe)
		{
			const float After = X + 108.0f + FMath::Min(NameW, HudMeasure(Row.Name, 22.0f)) + 10.0f;
			RoundedBox(RowP, After, Y + 10.0f, 46.0f, 20.0f, 6.0f, Gold);
			RowTop.Text(TEXT("YOU"), After + 23.0f, Y + 11.0f, 12.0f, Ink, ETextAlign::Center, TEXT("BlackItalic"));
		}
		const FString Stats = bDuel ? FString::Printf(TEXT("%d試合　勝率%d%%"), Row.Games, FMath::RoundToInt(100.0f * Row.Wins / FMath::Max(1, Row.Wins + Row.Losses)))
			: FString::Printf(TEXT("%d試合　1位%d回"), Row.Games, Row.Wins);
		RbText(RowP, Stats, X + W - 150.0f, Y + 20.0f, 14.0f, 140.0f, Muted, ETextAlign::Right, TEXT("Bold"));
		const FString Rate = FString::FromInt(Row.Rate);
		HudText3D(RowTop, Rate, X + W - 18.0f, Y + 10.0f, HudFit(Rate, 30.0f, 110.0f, TEXT("BlackItalic")), Paper, ETextAlign::Right, TEXT("BlackItalic"));
	}
	if (Count > RankingRows)
	{
		const float BarH = RankingRows * 66.0f - 8.0f;
		RoundedBox(Back, 1500.0f, RbTop, 6.0f, BarH, 3.0f, WithAlpha(Paper, 0.08f));
		RoundedBox(P, 1500.0f, RbTop + BarH * Scroll / Count, 6.0f, FMath::Max(24.0f, BarH * RankingRows / Count), 3.0f, WithAlpha(Accent, 0.85f));
	}
}
