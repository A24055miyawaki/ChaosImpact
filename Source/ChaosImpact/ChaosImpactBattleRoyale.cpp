#include "ChaosImpactGameMode.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactSpectatorPawn.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "ChaosImpactMatchAnnouncerWidget.h"
#include "ChaosImpactPaint.h"

namespace
{
	int32 SideOf(const FChaosImpactRoyaleEntry& Entry, const int32 Index, const bool bTeams)
	{
		return bTeams ? Entry.Team : Index;
	}
}

void AChaosImpactGameMode::InitializeRoyale()
{
	auto* Match = GetGameState<AChaosImpactGameState>();
	GetWorldTimerManager().ClearTimer(RoyaleTimer);
	Match->Royale = FChaosImpactRoyaleState();
	if (!Match->IsBattleRoyale()) { return; }
	Match->Royale.TeamPlaces.Init(0, Match->Rules.TeamCount);
	for (auto* Member : Match->GetCompetitors(true))
	{
		FChaosImpactRoyaleEntry Entry;
		Entry.Member = Member;
		const auto* Character = Cast<AChaosImpactCharacter>(Member->GetPawn());
		Entry.Name = Character ? Character->GetOverheadDisplayName() : Member->GetPlayerName();
		Entry.Character = Character ? Character->GetShownCharacter() : Member->CharacterIndex;
		Entry.Colour = Character ? Character->GetShownColour() : FMath::Max(0, Member->ColourChoice);
		Entry.Team = Member->TeamIndex;
		Entry.Health = Character ? Character->GetHealth() : 3.f;
		Match->Royale.Entries.Add(Entry);
	}
	Match->Royale.AlivePlayers = Match->Royale.Entries.Num();
	Match->Royale.AliveSides = Match->IsTeamBattle() ? Match->Rules.TeamCount : Match->Royale.AlivePlayers;
	Match->ForceNetUpdate();
}

void AChaosImpactGameMode::WatchAfterRoyaleElimination(APlayerController* Player)
{
	const auto* Match = GetGameState<AChaosImpactGameState>();
	if (!Player || !Match || !Match->IsBattleRoyale() || Match->Phase != EChaosImpactOnlinePhase::Match
		|| !Match->IsRoyaleEliminated(Player->PlayerState) || Cast<AChaosImpactSpectatorPawn>(Player->GetPawn())) { return; }
	APawn* OldPawn = Player->GetPawn();
	Player->UnPossess();
	RestartPlayer(Player);
	// Give the defeat pieces time to finish; this pawn will never respawn in this round.
	if (OldPawn) { OldPawn->SetLifeSpan(2.f); }
}

void AChaosImpactGameMode::UpdateRoyale()
{
	auto* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || !Match->IsBattleRoyale() || Match->Phase != EChaosImpactOnlinePhase::Match || Match->Royale.bFinished) { return; }
	auto& State = Match->Royale;
	const bool bTeams = Match->IsTeamBattle();
	TArray<int32> NewlyOut;
	TSet<int32> AliveSides;
	State.AlivePlayers = 0;
	for (int32 Index = 0; Index < State.Entries.Num(); ++Index)
	{
		auto& Entry = State.Entries[Index];
		if (Entry.bOut) { continue; }
		const auto* Member = Entry.Member.Get();
		const auto* Pawn = IsValid(Member) ? Cast<AChaosImpactCharacter>(Member->GetPawn()) : nullptr;
		if (!IsValid(Member) || Member->IsInactive() || !Pawn || Pawn->IsEliminated())
		{
			Entry.bOut = true; Entry.Health = 0.f; NewlyOut.Add(Index);
			if (auto* PC = IsValid(Member) ? Cast<APlayerController>(Member->GetOwner()) : nullptr)
			{
				FTimerHandle CameraDelay;
				TWeakObjectPtr<APlayerController> Weak = PC;
				GetWorldTimerManager().SetTimer(CameraDelay, FTimerDelegate::CreateWeakLambda(this, [this, Weak]()
				{ WatchAfterRoyaleElimination(Weak.Get()); }), .8f, false);
			}
		}
		else
		{
			Entry.Health = Pawn->GetHealth();
			++State.AlivePlayers;
			AliveSides.Add(SideOf(Entry, Index, bTeams));
		}
	}
	State.AliveSides = AliveSides.Num();
	if (bTeams)
	{
		for (int32 Team = 0; Team < State.TeamPlaces.Num(); ++Team)
		{
			if (!AliveSides.Contains(Team) && State.TeamPlaces[Team] == 0) { State.TeamPlaces[Team] = State.AliveSides + 1; }
		}
		for (auto& Entry : State.Entries)
		{
			if (State.TeamPlaces.IsValidIndex(Entry.Team)) { Entry.Place = State.TeamPlaces[Entry.Team]; }
		}
	}
	else
	{
		for (const int32 Index : NewlyOut)
		{
			if (State.Entries[Index].Place <= 0) { State.Entries[Index].Place = State.AlivePlayers + 1; }
		}
	}
	Match->ForceNetUpdate();
	if (State.AliveSides <= 1)
	{
		TArray<int32> Winners = AliveSides.Array();
		if (Winners.IsEmpty())
		{
			// Same-frame mutual knockout: share the best place, independent of callback order or points.
			for (int32 I = 0; I < State.Entries.Num(); ++I)
			{
				if (State.Entries[I].Place == 1) { Winners.AddUnique(SideOf(State.Entries[I], I, bTeams)); }
			}
		}
		FinishRoyale(Winners);
	}
}

void AChaosImpactGameMode::HandleRoyaleTimeExpired()
{
	auto* Match = GetGameState<AChaosImpactGameState>();
	UpdateRoyale();
	if (!Match || !Match->IsBattleRoyale() || Match->Royale.bFinished || Match->Phase != EChaosImpactOnlinePhase::Match) { return; }
	auto& State = Match->Royale;
	const bool bTeams = Match->IsTeamBattle();
	TMap<int32, float> HealthBySide;
	TMap<int32, int32> CountBySide;
	for (int32 I = 0; I < State.Entries.Num(); ++I)
	{
		const auto& Entry = State.Entries[I];
		if (!Entry.bOut)
		{
			const int32 Side = SideOf(Entry, I, bTeams);
			HealthBySide.FindOrAdd(Side) += Entry.Health;
			++CountBySide.FindOrAdd(Side);
		}
	}
	TArray<int32> Leaders;
	float Best = -1.f;
	int32 BestCount = -1;
	for (const auto& Pair : HealthBySide)
	{
		const int32 Count = CountBySide[Pair.Key];
		if (Count > BestCount || (Count == BestCount && Pair.Value > Best + KINDA_SMALL_NUMBER))
		{
			BestCount = Count; Best = Pair.Value; Leaders.Reset();
		}
		if (Count == BestCount && FMath::IsNearlyEqual(Pair.Value, Best)) { Leaders.Add(Pair.Key); }
	}
	// Resolve every surviving side's place at the bell, not just the winner. Already eliminated sides keep
	// their earlier places; lower finalists retain this order when they are removed for sudden death.
	TArray<int32> OrderedSides;
	HealthBySide.GetKeys(OrderedSides);
	OrderedSides.Sort([&](const int32 A, const int32 B)
	{
		return CountBySide[A] != CountBySide[B] ? CountBySide[A] > CountBySide[B] : HealthBySide[A] > HealthBySide[B];
	});
	int32 Place = 1;
	for (int32 I = 0; I < OrderedSides.Num(); ++I)
	{
		const int32 Side = OrderedSides[I];
		if (I > 0 && (CountBySide[Side] != CountBySide[OrderedSides[I - 1]]
			|| !FMath::IsNearlyEqual(HealthBySide[Side], HealthBySide[OrderedSides[I - 1]]))) { Place = I + 1; }
		if (Leaders.Contains(Side)) { continue; }
		if (bTeams) { State.TeamPlaces[Side] = Place; }
		for (int32 E = 0; E < State.Entries.Num(); ++E)
		{
			if (SideOf(State.Entries[E], E, bTeams) == Side) { State.Entries[E].Place = Place; }
		}
	}
	// At the second bell compare the surviving finalists by the same count/HP rule. Equal leaders share first.
	if (State.bSuddenDeath || Leaders.Num() <= 1)
	{
		FinishRoyale(Leaders); return;
	}
	State.bSuddenDeath = true;
	State.SuddenDeathStartedAt = Match->GetServerWorldTimeSeconds();
	for (int32 I = 0; I < State.Entries.Num(); ++I)
	{
		auto& Entry = State.Entries[I];
		if (Entry.bOut || !Entry.Member) { continue; }
		if (auto* Character = Cast<AChaosImpactCharacter>(Entry.Member->GetPawn()))
		{
			if (Leaders.Contains(SideOf(Entry, I, bTeams))) { Character->PrepareBattleSuddenDeath(); }
			else { Character->EliminateFromBattleRoyale(); }
		}
	}
	// Keep the same phase and its start time: no pause, teleport, intro, charge cancellation or projectile clearing.
	Match->PhaseEndsAt = Match->GetServerWorldTimeSeconds() + ChaosImpactMatch::RoyaleSuddenDeathSeconds;
	GetWorldTimerManager().SetTimer(MatchPhaseTimer, this, &AChaosImpactGameMode::HandleRoyaleTimeExpired,
		ChaosImpactMatch::RoyaleSuddenDeathSeconds, false);
	UpdateRoyale();
	Match->ForceNetUpdate();
}

void AChaosImpactGameMode::FinishRoyale(const TArray<int32>& Winners)
{
	auto* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || Match->Royale.bFinished) { return; }
	Match->Royale.bFinished = true;
	for (int32 I = 0; I < Match->Royale.Entries.Num(); ++I)
	{
		auto& Entry = Match->Royale.Entries[I];
		if (Winners.Contains(SideOf(Entry, I, Match->IsTeamBattle()))) { Entry.Place = 1; }
		else if (Entry.Place <= 0) { Entry.Place = Winners.Num() + 1; }
	}
	if (Match->IsTeamBattle())
	{
		for (int32 Team = 0; Team < Match->Royale.TeamPlaces.Num(); ++Team)
		{
			if (Winners.Contains(Team)) { Match->Royale.TeamPlaces[Team] = 1; }
			else if (Match->Royale.TeamPlaces[Team] == 0) { Match->Royale.TeamPlaces[Team] = Winners.Num() + 1; }
		}
	}
	GetWorldTimerManager().ClearTimer(RoyaleTimer);
	Match->ForceNetUpdate();
	EndMatch();
}

void UChaosImpactMatchAnnouncerWidget::PaintRoyaleHUD(const FGeometry& Geometry, FSlateWindowElementList& Elements,
	const int32 Layer, const AChaosImpactGameState& Match) const
{
	using namespace ChaosImpactPaint;
	const FVector2D Size = Geometry.GetLocalSize();
	const float S = FMath::Clamp(static_cast<float>(FMath::Min(Size.X / 1600., Size.Y / 900.)), .42f, 1.4f);
	const FGeometry Top = MakeAnchor(Geometry, Size.X * .5, 0.f, S);
	const auto At = [](const float X, const float Y) { return FVector2D(X, Y); };
	const auto& State = Match.Royale;
	const bool bTeams = Match.IsTeamBattle();
	const double Now = FPlatformTime::Seconds();
	const float Time = static_cast<float>(FMath::Fmod(Now, 3600.0));

	// What was shown last frame, so a change can be played out: a survivor gone, the count dropping.
	if (RoyaleTrackedStart != Match.PhaseStartedAt || RoyaleOutSeenAt.Num() != State.Entries.Num())
	{
		RoyaleTrackedStart = Match.PhaseStartedAt;
		RoyaleOutSeenAt.Init(0.0, State.Entries.Num());
		for (int32 I = 0; I < State.Entries.Num(); ++I)
		{
			RoyaleOutSeenAt[I] = State.Entries[I].bOut ? Now - 10.0 : 0.0;
		}
		RoyaleShownAlive = State.AlivePlayers;
		RoyaleAliveChangedAt = -100.0;
	}
	for (int32 I = 0; I < State.Entries.Num(); ++I)
	{
		if (State.Entries[I].bOut && RoyaleOutSeenAt[I] <= 0.0)
		{
			RoyaleOutSeenAt[I] = Now;
		}
	}
	if (State.AlivePlayers != RoyaleShownAlive)
	{
		RoyaleAliveChangedAt = State.AlivePlayers < RoyaleShownAlive ? Now : RoyaleAliveChangedAt;
		RoyaleShownAlive = State.AlivePlayers;
	}
	const float Dropped = static_cast<float>(Now - RoyaleAliveChangedAt);
	const float Punch = Dropped >= 0.f && Dropped < 1.f ? FMath::Exp(-8.f * Dropped) : 0.f;

	// ---- The clock: red-hot in the last ten seconds, burning orange through sudden death.
	const float Remaining = Match.GetPhaseRemainingSeconds();
	const int32 Seconds = FMath::CeilToInt(Remaining);
	const bool bSudden = State.bSuddenDeath;
	const bool bHurry = !bSudden && Remaining <= 10.f;
	const FPainter ClockBase{Top, Elements, Layer + 1};
	const FPainter Slab = HudAbove(ClockBase);
	HudShard(Slab, {At(-112.f, 4.f), At(114.f, 4.f), At(130.f, 34.f), At(102.f, 92.f), At(-98.f, 94.f),
		At(-128.f, 38.f)}, bSudden ? MakeFill(FLinearColor(.62f, .16f, .02f, .97f), FLinearColor(.2f, .03f, .01f, .97f))
		: bHurry ? MakeFill(FLinearColor(.5f, .06f, .04f, .96f), FLinearColor(.12f, .01f, .02f, .96f)) : NightFill, 4.f, FVector2D(7.0, 8.0));
	const FPainter OnSlab = HudAbove(Slab);
	HudStroke(OnSlab, {At(-109.f, 8.f), At(111.f, 8.f), At(126.f, 34.f)}, bSudden || bHurry ? FireFill.Top : IceFill.Middle, 3.f);
	OnSlab.Text(TEXT("BATTLE ROYALE"), 0.f, 11.f, 13.f, bSudden ? FireFill.Top : WithAlpha(IceFill.Top, .9f), ETextAlign::Center, TEXT("BlackItalic"),
		1.5f, Navy);
	const FPainter Digits{MakeSkewed(Top, -120.f, 24.f, 240.f, 52.f, -.12f), Elements, Layer + 9};
	HudText3D(Digits, FString::Printf(TEXT("%d:%02d"), Seconds / 60, Seconds % 60), 120.f, -4.f, 44.f,
		bSudden ? FMath::Lerp(Gold, Paper, .5f + .5f * FMath::Sin(Time * 9.f)) : bHurry ? FireFill.Top : Paper, ETextAlign::Center);
	// Under the digits, the blade of the time left (of the bell, then of sudden death).
	const float Total = bSudden ? ChaosImpactMatch::RoyaleSuddenDeathSeconds : FMath::Max(Remaining + Match.GetPhaseElapsedSeconds(), 1.f);
	const TArray<FVector2D> Blade{At(-80.f, 76.f), At(76.f, 76.f), At(88.f, 80.f), At(76.f, 84.f), At(-84.f, 84.f)};
	HudPoly(HudAbove(OnSlab, 1), HudInflate(Blade, 2.f), SolidFill(Navy), 1);
	HudPoly(HudAbove(OnSlab, 2), HudClip(Blade, -84.f + 172.f * FMath::Clamp(Remaining / Total, 0.f, 1.f), false, true),
		bSudden || bHurry ? FireFill : IceFill, 2);

	// ---- Left of the clock: the survivors (a figure and how many are still standing, out of how many began).
	{
		const FPainter Plate{Top, Elements, Layer + 1};
		const float X0 = -152.f, X1 = -372.f;
		HudShard(Plate, {At(X0, 12.f), At(X1, 12.f), At(X1 + 16.f, 84.f), At(X0 - 12.f, 84.f)},
			MakeFill(FMath::Lerp(NightFill.Top, Fire, .25f * Punch), NightFill.Bottom), 3.f, FVector2D(4.0, 5.0), FMath::Lerp(Navy, Paper, Punch * .8f));
		const FPainter Face = HudAbove(Plate);
		HudStroke(Face, {At(X0 - 10.f, 81.f), At(X1 + 18.f, 81.f)}, FMath::Lerp(Gold, Paper, Punch), 4.f);
		// The figure: a head and shoulders.
		const FVector2D Head(X1 + 46.f, 34.f);
		Face.Disc(Head + FVector2D(2., 3.), 11.f, WithAlpha(Extrude, .9f));
		Face.Disc(Head, 11.f, Navy);
		HudAbove(Face, 1).Disc(Head, 8.5f, Paper);
		HudShard(Face, {At(X1 + 30.f, 50.f), At(X1 + 62.f, 50.f), At(X1 + 68.f, 70.f), At(X1 + 24.f, 70.f)}, SolidFill(Paper), 2.f, FVector2D(2.0, 3.0));
		HudText3D(HudAbove(Face, 2), FString::FromInt(State.AlivePlayers), X1 + 128.f, 12.f, 50.f,
			FMath::Lerp(Paper, FireFill.Top, Punch), ETextAlign::Center);
		HudText3D(HudAbove(Face, 2), FString::Printf(TEXT("/%d"), State.Entries.Num()), X1 + 162.f, 38.f, 20.f, WithAlpha(Paper, .8f), ETextAlign::Left);
	}

	const APlayerState* Own = GetOwningPlayer() ? GetOwningPlayer()->PlayerState.Get() : nullptr;
	const UGameInstance* GameInstance = GetGameInstance();

	// ---- Under the clock: everyone in the battle, team by team, as standing tiles with their health; the fallen go dark
	// with their place, breaking apart as they fall.
	TArray<int32> Order;
	for (int32 I = 0; I < State.Entries.Num(); ++I)
	{
		Order.Add(I);
	}
	if (bTeams)
	{
		Order.StableSort([&State](const int32 A, const int32 B) { return State.Entries[A].Team < State.Entries[B].Team; });
	}
	constexpr float TileW = 70.f, TileH = 44.f, Gap = 8.f, TeamGap = 20.f;
	float Width = 0.f;
	for (int32 K = 0; K < Order.Num(); ++K)
	{
		Width += TileW + (K > 0 ? (bTeams && State.Entries[Order[K]].Team != State.Entries[Order[K - 1]].Team ? TeamGap : Gap) : 0.f);
	}
	float X = -Width * .5f;
	constexpr float Y = 108.f;
	for (int32 K = 0; K < Order.Num(); ++K)
	{
		const int32 I = Order[K];
		const auto& Entry = State.Entries[I];
		if (K > 0)
		{
			X += bTeams && Entry.Team != State.Entries[Order[K - 1]].Team ? TeamGap : Gap;
		}
		const AChaosImpactPlayerState* Member = Entry.Member.Get();
		const bool bMine = Member && Member == Own;
		FLinearColor Color = bTeams && Entry.Team >= 0 ? ChaosImpactMatch::GetTeamColor(Entry.Team) : Gold;
		if (!bTeams && Member)
		{
			const APlayerController* Controller = Cast<APlayerController>(Member->GetOwner());
			const ULocalPlayer* Local = Controller && Controller->IsLocalController() ? Controller->GetLocalPlayer() : nullptr;
			Color = Member->IsABot() ? FLinearColor(.74f, .78f, .86f) : Local && GameInstance
				? PlayerAccents[FMath::Max(GameInstance->GetLocalPlayers().IndexOfByKey(Local), 0) % 4] : Gold;
		}
		const float OutAge = Entry.bOut && RoyaleOutSeenAt.IsValidIndex(I) ? static_cast<float>(Now - RoyaleOutSeenAt[I]) : -1.f;
		const float Breaking = OutAge >= 0.f && OutAge < .7f ? 1.f - OutAge / .7f : 0.f;
		const float Jolt = Breaking > 0.f ? FMath::Sin(OutAge * 70.f) * Breaking * 5.f : 0.f;
		const float TX = X + Jolt;
		const TArray<FVector2D> Tile{At(TX + 7.f, Y), At(TX + TileW, Y), At(TX + TileW - 7.f, Y + TileH), At(TX, Y + TileH)};
		const FPainter TileBack{Top, Elements, Layer + 2};
		if (Entry.bOut)
		{
			// Fallen: dark, a red cut across, the place it fell at.
			HudShard(TileBack, Tile, MakeFill(FLinearColor(.07f, .08f, .14f, .9f), FLinearColor(.03f, .03f, .07f, .9f)), 2.f, FVector2D(3.0, 4.0),
				FMath::Lerp(Navy, Paper, Breaking));
			const FPainter Front = HudAbove(TileBack);
			Front.Text(Entry.Name, TX + TileW * .5f, Y + 4.f, FMath::Clamp(13.f * 56.f / FMath::Max(HudMeasure(Entry.Name, 13.f), 1.f), 8.f, 13.f),
				WithAlpha(Paper, .35f), ETextAlign::Center, TEXT("Black"));
			HudStroke(HudAbove(Front, 1), {At(TX + 6.f, Y + TileH - 6.f), At(TX + TileW - 4.f, Y + 6.f)}, WithAlpha(Fire, .85f), 3.f);
			if (Entry.Place > 0)
			{
				const FHudFill Medal = Entry.Place == 1 ? FireFill : Entry.Place == 2 ? SilverFill : Entry.Place == 3 ? BronzeFill
					: MakeFill(FLinearColor(.62f, .68f, .8f), FLinearColor(.32f, .36f, .48f));
				HudText3D(HudAbove(Front, 2), FString::Printf(TEXT("%d"), Entry.Place), TX + TileW * .5f, Y + 15.f, 22.f, Medal.Top, ETextAlign::Center);
			}
			if (Breaking > 0.f)
			{
				// The moment it falls: a flash and its pieces.
				HudShard(HudAbove(Front, 3), Tile, SolidFill(WithAlpha(Paper, .6f * Breaking * Breaking)), 0.f, FVector2D::ZeroVector);
				HudShatter(HudAbove(Front, 4), At(TX + TileW * .5f, Y + TileH * .5f), 1.f - Breaking, 16.f,
					MakeFill(FMath::Lerp(Color, Paper, .4f), Color), 7, static_cast<float>(I));
			}
		}
		else
		{
			HudShard(TileBack, Tile, MakeFill(FMath::Lerp(NightFill.Top, Color, .55f), NightFill.Bottom), 2.f, FVector2D(3.0, 4.0),
				bMine ? FMath::Lerp(Navy, Paper, .55f + .45f * FMath::Sin(Time * 5.f)) : Navy);
			const FPainter Front = HudAbove(TileBack);
			HudStroke(Front, {At(TX + 4.f, Y + TileH - 3.f), At(TX + TileW - 8.f, Y + TileH - 3.f)}, Color, 3.f);
			Front.Text(Entry.Name, TX + TileW * .5f + 2.f, Y + 3.f, FMath::Clamp(13.f * 58.f / FMath::Max(HudMeasure(Entry.Name, 13.f), 1.f), 8.f, 13.f),
				Paper, ETextAlign::Center, TEXT("Black"), 1.5f, Navy);
			// Health: three slanted cells; the last one throbs red.
			const int32 Cells = 3;
			const int32 Left = FMath::Clamp(FMath::CeilToInt(Entry.Health - KINDA_SMALL_NUMBER), 0, Cells);
			for (int32 Cell = 0; Cell < Cells; ++Cell)
			{
				const float CX = TX + 12.f + Cell * 17.f;
				const bool bFull = Cell < Left;
				const FLinearColor CellColor = bFull && Left == 1 ? FMath::Lerp(FireFill.Top, Paper, .5f + .5f * FMath::Sin(Time * 12.f)) : Paper;
				HudShard(HudAbove(Front, 1), {At(CX + 3.f, Y + 24.f), At(CX + 16.f, Y + 24.f), At(CX + 13.f, Y + 34.f), At(CX, Y + 34.f)},
					bFull ? SolidFill(CellColor) : SolidFill(WithAlpha(Navy, .9f)), 1.f, FVector2D(1.0, 1.5));
			}
			if (bMine)
			{
				// This player's own tile: a marker over it.
				HudShard(HudAbove(Front, 2), {At(TX + TileW * .5f - 7.f, Y - 10.f), At(TX + TileW * .5f + 9.f, Y - 10.f), At(TX + TileW * .5f + 1.f, Y - 2.f)},
					SolidFill(Paper), 1.5f, FVector2D(1.0, 2.0));
			}
		}
		X += TileW;
	}
}
