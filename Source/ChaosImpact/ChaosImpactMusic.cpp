#include "ChaosImpactMusic.h"

#include "ChaosImpact.h"
#include "ChaosImpactDojo.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactLoadingScreen.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactScreen.h"
#include "ChaosImpactSettings.h"
#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWave.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	const TCHAR* const MusicFolder = TEXT("/Game/ChaosImpact/Audio/BGM/");
	const TCHAR* const MenuSong = TEXT("BGM_FunkyMagic");
	const TCHAR* const ColourSong = TEXT("BGM_PappaParappa");
	const TCHAR* const BattleSongs[] = {TEXT("BGM_TrickStyle"), TEXT("BGM_BattleJourney")};
	const TCHAR* const ResultsJingle = TEXT("BGM_Fireworks");
	const TCHAR* const ResultsSong = TEXT("BGM_Clap");
	const TCHAR* const DojoSong = TEXT("BGM_TanukichiAdventure");

	/** Each song's own level under the sound effects (the files are levelled alike; a battle's music sits lowest). */
	float SongLevel(const FString& Name)
	{
		if (Name.Contains(TEXT("TrickStyle")) || Name.Contains(TEXT("BattleJourney")))
		{
			return 0.42f;
		}
		if (Name.Contains(TEXT("Fireworks")))
		{
			return 0.55f;
		}
		return 0.5f;
	}

	/** The pause screen: the music carries on, quieter. */
	constexpr float PausedLevel = 0.45f;
	/** A change of music: the old one out over this, a short breath, then the new one in over this. */
	constexpr float ChangeOut = 0.6f;
	constexpr float ChangeGap = 0.15f;
	constexpr float ChangeIn = 0.5f;
	/** The loading screen coming up: out quickly. */
	constexpr float LoadingOut = 0.35f;

	/**
	 * The menus: the menu's music whatever world they are opened over (the mode select from the 訓練場's lesson list, the
	 * results' メニューへ and ルールを変える, a stopped search's online menu, a solo stage's pause to the mode select...).
	 */
	bool IsMenuScreen(const EChaosImpactScreen Screen)
	{
		switch (Screen)
		{
		case EChaosImpactScreen::ModeSelect:
		case EChaosImpactScreen::TrainingSetup:
		case EChaosImpactScreen::SoloReady:
		case EChaosImpactScreen::MultiReady:
		case EChaosImpactScreen::OnlineName:
		case EChaosImpactScreen::OnlinePassword:
		case EChaosImpactScreen::OnlineStatus:
		case EChaosImpactScreen::VSSelect:
		case EChaosImpactScreen::OnlinePlayers:
		case EChaosImpactScreen::MatchRules:
		case EChaosImpactScreen::TeamSelect:
		case EChaosImpactScreen::OnlineRoomName:
		case EChaosImpactScreen::RoomList:
		case EChaosImpactScreen::StageSelect:
		case EChaosImpactScreen::SoloStageSelect:
		case EChaosImpactScreen::RankTop:
		case EChaosImpactScreen::OnlineConnecting:
		case EChaosImpactScreen::OnlinePrivate:
		case EChaosImpactScreen::OnlineRecord:
		case EChaosImpactScreen::TourneyList:
		case EChaosImpactScreen::TourneyCreate:
		case EChaosImpactScreen::LineCheck:
		case EChaosImpactScreen::TourneyEntry:
		case EChaosImpactScreen::OnlineHistory:
		case EChaosImpactScreen::OnlineRanking:
			return true;
		default:
			return false;
		}
	}
}

void UChaosImpactMusic::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (IsRunningDedicatedServer() || IsRunningCommandlet())
	{
		return;
	}
	Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UChaosImpactMusic::Tick));
	PreLoadHandle = FCoreUObjectDelegates::PreLoadMap.AddUObject(this, &UChaosImpactMusic::HandlePreLoadMap);
}

void UChaosImpactMusic::HandlePreLoadMap(const FString& MapName)
{
	// A level is about to load (the loading screen is up, or will be): quiet now.
	if (Current != EChaosImpactBgm::None || IsValid(Playing) || IsValid(Jingle))
	{
		UE_LOG(LogChaosImpact, Log, TEXT("Music: stopped for loading %s"), *MapName);
	}
	for (UAudioComponent* Component : {Playing.Get(), Jingle.Get()})
	{
		if (IsValid(Component))
		{
			Component->FadeOut(LoadingOut, 0.0f);
			Fading.Add(Component);
		}
	}
	Playing = nullptr;
	Jingle = nullptr;
	Current = EChaosImpactBgm::None;
	StartAt = 0.0;
}

bool UChaosImpactMusic::IsLoadingShown() const
{
	const UChaosImpactLoadingSubsystem* Loading = GetGameInstance() ? GetGameInstance()->GetSubsystem<UChaosImpactLoadingSubsystem>() : nullptr;
	// (The little games played for their own sake from the settings are not a loading screen.)
	return Loading && Loading->IsShowing() && !Loading->IsArcade();
}

void UChaosImpactMusic::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
	FCoreUObjectDelegates::PreLoadMap.Remove(PreLoadHandle);
	for (UAudioComponent* Component : {Playing.Get(), Jingle.Get()})
	{
		if (IsValid(Component))
		{
			Component->Stop();
			Component->DestroyComponent();
		}
	}
	for (UAudioComponent* Component : Fading)
	{
		if (IsValid(Component))
		{
			Component->Stop();
			Component->DestroyComponent();
		}
	}
	Fading.Reset();
	Super::Deinitialize();
}

void UChaosImpactMusic::RefreshVolume()
{
	if (!GEngine)
	{
		return;
	}
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (UGameInstance* Instance = Context.OwningGameInstance)
		{
			if (UChaosImpactMusic* Music = Instance->GetSubsystem<UChaosImpactMusic>())
			{
				Music->ApplyVolume(0.15f);
			}
		}
	}
}

void UChaosImpactMusic::NotifyResultsShown(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (UChaosImpactMusic* Music = World && World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UChaosImpactMusic>() : nullptr)
	{
		Music->bResultsShown = true;
	}
}

FString UChaosImpactMusic::GetCurrentTrackName() const
{
	const UAudioComponent* Component = IsValid(Jingle) && Jingle->IsPlaying() ? Jingle.Get() : Playing.Get();
	return IsValid(Component) && Component->Sound ? Component->Sound->GetName() : FString();
}

EChaosImpactBgm UChaosImpactMusic::Decide(UWorld* World, bool& bOutPaused) const
{
	bOutPaused = false;
	if (IsLoadingShown())
	{
		return EChaosImpactBgm::None;
	}
	UGameInstance* Instance = GetGameInstance();
	APlayerController* PC = Instance ? Instance->GetFirstLocalPlayerController(World) : nullptr;
	if (!PC)
	{
		// (Between levels: what was playing carries on.)
		return Current;
	}
	// バトルロイヤル (its own game state, a module that can be taken out): read its phase by name.
	if (const AGameStateBase* State = World->GetGameState(); State && State->GetClass()->GetName().Contains(TEXT("BRGameState")))
	{
		if (const FEnumProperty* Phase = FindFProperty<FEnumProperty>(State->GetClass(), TEXT("Phase")))
		{
			const int64 Value = Phase->GetUnderlyingProperty()->GetSignedIntPropertyValue(Phase->ContainerPtrToValuePtr<void>(State));
			// Title, Ship, Play, Over.
			return Value == 0 ? EChaosImpactBgm::Menu : Value == 3 ? EChaosImpactBgm::Results : EChaosImpactBgm::Battle;
		}
	}
	const AChaosImpactPlayerController* Player = Cast<AChaosImpactPlayerController>(PC);
	// (The RPG, whose controller is one of ours underneath (a module that can be taken out: read by name), and anything else
	// with a controller of its own: no music of ours.)
	if (!Player || Player->GetClass()->GetName().StartsWith(TEXT("RPG")))
	{
		return EChaosImpactBgm::None;
	}
	const EChaosImpactScreen Screen = Player->GetCurrentScreen();
	bOutPaused = Screen == EChaosImpactScreen::Pause;
	// The title, and the CPUs' match it plays when left alone: quiet.
	if (Screen == EChaosImpactScreen::Title || Player->IsDemoPlay())
	{
		return EChaosImpactBgm::None;
	}
	if (Screen == EChaosImpactScreen::ColourEdit || Screen == EChaosImpactScreen::Gallery)
	{
		return EChaosImpactBgm::ColourEdit;
	}
	const AChaosImpactGameState* Match = World->GetGameState<AChaosImpactGameState>();
	if (Screen == EChaosImpactScreen::MatchEnd)
	{
		return EChaosImpactBgm::Results;
	}
	// Over whatever is on, the music that was on carries on: the settings, a found 1v1's card (in the search's waiting).
	if (Screen == EChaosImpactScreen::Settings || Screen == EChaosImpactScreen::DuelFound)
	{
		return Current;
	}
	const bool bRoom = Player->IsOnlineRoom();
	// Who plays with which controller, and the characters: the menu's music on the way in from the menus (or from the title
	// or a results screen), and whatever is on when opened over training (its player count changed) and the like.
	if (!bRoom && (Screen == EChaosImpactScreen::ControllerAssignment || Screen == EChaosImpactScreen::CharacterSelect))
	{
		return Current == EChaosImpactBgm::None || Current == EChaosImpactBgm::Results ? EChaosImpactBgm::Menu : Current;
	}
	// The menus over any world but an online room's (whose own screens, its lobby's renaming and character change, are the
	// lobby's), and a local tournament's seats when called from the menus (in its own level, the tournament's below).
	if (!bRoom && (IsMenuScreen(Screen) || (Screen == EChaosImpactScreen::TourneySeat
		&& !(Match && Match->Phase == EChaosImpactOnlinePhase::Tournament))))
	{
		return EChaosImpactBgm::Menu;
	}
	if (Match && Match->bVersusMatch && Match->Phase == EChaosImpactOnlinePhase::Results)
	{
		// FINISH: quiet. The results' music once their podium is up (or a while after, should it never say so).
		const double Now = FPlatformTime::Seconds();
		UChaosImpactMusic* Self = const_cast<UChaosImpactMusic*>(this);
		if (Self->ResultsPhaseAt <= 0.0)
		{
			Self->ResultsPhaseAt = Now;
		}
		return bResultsShown || Now - ResultsPhaseAt > 8.0 ? EChaosImpactBgm::Results : EChaosImpactBgm::None;
	}
	const_cast<UChaosImpactMusic*>(this)->ResultsPhaseAt = 0.0;
	const_cast<UChaosImpactMusic*>(this)->bResultsShown = false;
	// A tournament (its phase, whatever else is set): by where this player is. In an arena, playing or watching: that match's
	// battle music (its opening to its last round), quiet from its WIN; an arena with no match on (a local tournament's
	// watcher between matches): the lobby's. In the waiting area (the practice room): the lobby's. The tournament's own menu
	// (the bracket, opened anywhere) changes nothing.
	if (Match && Match->Phase == EChaosImpactOnlinePhase::Tournament)
	{
		const AChaosImpactPlayerState* Me = PC->GetPlayerState<AChaosImpactPlayerState>();
		const bool bInArena = Me && (Me->TournamentRole == EChaosImpactTournamentRole::Competitor
			|| Me->TournamentRole == EChaosImpactTournamentRole::Spectator) && Match->TournamentArenas.IsValidIndex(Me->TournamentArena);
		if (!bInArena)
		{
			return EChaosImpactBgm::Dojo;
		}
		const EChaosImpactDuelStep Step = Match->TournamentArenas[Me->TournamentArena].Duel.Step;
		return Step == EChaosImpactDuelStep::Finished ? EChaosImpactBgm::None
			: Step == EChaosImpactDuelStep::None ? EChaosImpactBgm::Dojo : EChaosImpactBgm::Battle;
	}
	if (Match && Match->bVersusMatch)
	{
		switch (Match->Phase)
		{
		case EChaosImpactOnlinePhase::Intro:
		case EChaosImpactOnlinePhase::Countdown:
			return EChaosImpactBgm::Battle;
		case EChaosImpactOnlinePhase::Match:
			// A duel: on through its rounds; quiet from the final WIN.
			return Match->Rules.bDuel && Match->Duel.Step == EChaosImpactDuelStep::Finished ? EChaosImpactBgm::None : EChaosImpactBgm::Battle;
		case EChaosImpactOnlinePhase::TeamSelect:
			// (Picking teams: as the character select.)
			return EChaosImpactBgm::Menu;
		default:
			// An online room's lobby and its "starting" count: the lobby's music (the training arena's).
			return EChaosImpactBgm::Dojo;
		}
	}
	if (Player->IsOnlineRoom())
	{
		// An online room's lobby (合言葉 rooms, a ランダムマッチ room before its match): the lobby's music.
		return Match && Match->Phase == EChaosImpactOnlinePhase::TeamSelect ? EChaosImpactBgm::Menu : EChaosImpactBgm::Dojo;
	}
	if (Player->IsDojo())
	{
		return EChaosImpactBgm::Dojo;
	}
	if (World->URL.HasOption(TEXT("CIVersus=1")))
	{
		// A local VS stage while its match is being set up (a moment, before the game state says it is on): quiet, never a
		// moment of the menus' music before the battle's (its team and character picks are menus, above).
		return EChaosImpactBgm::None;
	}
	if (Player->IsSoloMode())
	{
		return EChaosImpactBgm::Battle;
	}
	if (Player->IsTrainingMode() && !Player->IsVersusMatchWorld())
	{
		return EChaosImpactBgm::Dojo;
	}
	return EChaosImpactBgm::Menu;
}

bool UChaosImpactMusic::Tick(const float DeltaSeconds)
{
	// The fades let go of once they are done.
	for (int32 Index = Fading.Num() - 1; Index >= 0; --Index)
	{
		UAudioComponent* Component = Fading[Index];
		if (!IsValid(Component) || !Component->IsPlaying())
		{
			if (IsValid(Component))
			{
				Component->DestroyComponent();
			}
			Fading.RemoveAt(Index);
		}
	}
	UGameInstance* Instance = GetGameInstance();
	UWorld* World = Instance ? Instance->GetWorld() : nullptr;
	if (!World || !World->IsGameWorld())
	{
		return true;
	}
	// The scene's music, once the old music has faded out.
	if (StartAt > 0.0 && FPlatformTime::Seconds() >= StartAt)
	{
		StartAt = 0.0;
		StartScene(World);
	}
	// The results: CLAP! once the jingle is over.
	if (Current == EChaosImpactBgm::Results && IsValid(Jingle) && !Jingle->IsPlaying() && !IsValid(Playing))
	{
		Jingle->DestroyComponent();
		Jingle = nullptr;
		Playing = Start(World, ResultsSong, 0.25f);
	}
	SinceCheck += DeltaSeconds;
	if (SinceCheck < 0.2)
	{
		return true;
	}
	SinceCheck = 0.0;
	bool bNowPaused = false;
	const EChaosImpactBgm Want = Decide(World, bNowPaused);
	if (Want != Current)
	{
		Switch(World, Want);
	}
	else if (Current == EChaosImpactBgm::Battle && IsValid(Playing) && FPlatformTime::Seconds() - BattleStartedAt < 6.0)
	{
		// The host's pick came in after the battle music had begun here: over to the same song as everyone else.
		const AChaosImpactGameState* Match = World->GetGameState<AChaosImpactGameState>();
		if (Match && Match->BattleSong >= 0 && Match->BattleSong % GetBattleSongCount() != LastBattleSong)
		{
			Switch(World, EChaosImpactBgm::Battle);
		}
	}
	if (bNowPaused != bPaused)
	{
		bPaused = bNowPaused;
		ApplyVolume(0.3f);
	}
	return true;
}

int32 UChaosImpactMusic::GetBattleSongCount()
{
	return UE_ARRAY_COUNT(BattleSongs);
}

int32 UChaosImpactMusic::PickNextBattleSong()
{
	static int32 Turn = INDEX_NONE;
	Turn = Turn == INDEX_NONE ? FMath::RandRange(0, GetBattleSongCount() - 1) : (Turn + 1) % GetBattleSongCount();
	return Turn;
}

UAudioComponent* UChaosImpactMusic::Start(UWorld* World, const TCHAR* Asset, const float FadeSeconds)
{
	const FString Name(Asset);
	TObjectPtr<USoundBase>& Sound = Loaded.FindOrAdd(Name);
	if (!Sound)
	{
		Sound = LoadObject<USoundBase>(nullptr, *(FString(MusicFolder) + Name + TEXT(".") + Name));
	}
	if (!Sound)
	{
		UE_LOG(LogChaosImpact, Warning, TEXT("Music %s is missing"), *Name);
		return nullptr;
	}
	// (Kept across level loads; freed by this subsystem.)
	UAudioComponent* Component = UGameplayStatics::CreateSound2D(World, Sound, 1.0f, 1.0f, 0.0f, nullptr, true, false);
	if (!Component)
	{
		return nullptr;
	}
	Component->bIsUISound = true;
	Component->SetVolumeMultiplier(1.0f);
	Component->FadeIn(FadeSeconds, GetLevel(Component));
	UE_LOG(LogChaosImpact, Log, TEXT("Music: %s"), *Name);
	return Component;
}

float UChaosImpactMusic::GetLevel(const UAudioComponent* Component) const
{
	const float Setting = FMath::Clamp(ChaosImpactSettings::GetMusicVolume() / 100.0f, 0.0f, 1.0f);
	const float Song = Component && Component->Sound ? SongLevel(Component->Sound->GetName()) : 0.5f;
	return Song * Setting * (bPaused ? PausedLevel : 1.0f);
}

void UChaosImpactMusic::ApplyVolume(const float Seconds)
{
	for (UAudioComponent* Component : {Playing.Get(), Jingle.Get()})
	{
		if (IsValid(Component) && Component->IsPlaying())
		{
			Component->AdjustVolume(Seconds, GetLevel(Component));
		}
	}
}

void UChaosImpactMusic::Switch(UWorld* World, const EChaosImpactBgm Next)
{
	UE_LOG(LogChaosImpact, Log, TEXT("Music: scene %d -> %d"), static_cast<int32>(Current), static_cast<int32>(Next));
	// The old music right out first (quicker into the results' jingle and the loading screen's quiet).
	const float Out = Next == EChaosImpactBgm::None && IsLoadingShown() ? LoadingOut : Next == EChaosImpactBgm::Results ? 0.3f : ChangeOut;
	bool bWasPlaying = false;
	for (UAudioComponent* Component : {Playing.Get(), Jingle.Get()})
	{
		if (IsValid(Component))
		{
			bWasPlaying |= Component->IsPlaying();
			Component->FadeOut(Out, 0.0f);
			Fading.Add(Component);
		}
	}
	Playing = nullptr;
	Jingle = nullptr;
	Current = Next;
	StartAt = 0.0;
	if (Next == EChaosImpactBgm::None)
	{
		return;
	}
	if (bWasPlaying)
	{
		// (The new one only once the old one has gone: never two songs over each other.)
		StartAt = FPlatformTime::Seconds() + Out + (Next == EChaosImpactBgm::Results ? 0.0f : ChangeGap);
		return;
	}
	StartScene(World);
}

void UChaosImpactMusic::StartScene(UWorld* World)
{
	switch (Current)
	{
	case EChaosImpactBgm::Menu:
		Playing = Start(World, MenuSong, ChangeIn);
		break;
	case EChaosImpactBgm::ColourEdit:
		Playing = Start(World, ColourSong, ChangeIn);
		break;
	case EChaosImpactBgm::Dojo:
		Playing = Start(World, DojoSong, ChangeIn);
		break;
	case EChaosImpactBgm::Battle:
	{
		// The one the host picked (every machine of an online room the same); else this machine's own turn.
		const AChaosImpactGameState* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
		const int32 Count = UE_ARRAY_COUNT(BattleSongs);
		LastBattleSong = Match && Match->BattleSong >= 0 ? Match->BattleSong % Count
			: LastBattleSong == INDEX_NONE ? FMath::RandRange(0, Count - 1) : (LastBattleSong + 1) % Count;
		Playing = Start(World, BattleSongs[LastBattleSong], 0.4f);
		BattleStartedAt = FPlatformTime::Seconds();
		break;
	}
	case EChaosImpactBgm::Results:
		Jingle = Start(World, ResultsJingle, 0.02f);
		if (!Jingle)
		{
			Playing = Start(World, ResultsSong, 0.3f);
		}
		break;
	default:
		break;
	}
}
