#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ChaosImpactMusic.generated.h"

class UAudioComponent;
class USoundBase;

/** Which music is playing. */
enum class EChaosImpactBgm : uint8
{
	/** Nothing (the title, the RPG, a dedicated server). */
	None,
	/** Every menu, character select included: Funky Magic. */
	Menu,
	/** カラーエディット (and the gallery of posted colours): Pappa Parappa. */
	ColourEdit,
	/** Every battle (VS, duel, tournament, party rules, crash ball, battle royale, solo): Trick style or 戦いの旅路を征く, in turn. */
	Battle,
	/** The results: the Fireworks jingle, then CLAP!. */
	Results,
	/** 訓練場, トレーニング, the online lobbies (合言葉 rooms, the ランダムマッチ wait in this machine's training, a tournament's
	 *  lobby): Tanukichi Adventure. */
	Dojo
};

/**
 * The game's BGM (/Game/ChaosImpact/Audio/BGM, from OpenTracks: see Docs/BGM.md). It looks at where the first local player is
 * a few times a second and plays that place's music. A change of music is clean: the old one fades right out first, then the
 * new one fades in (never two songs over each other). The loading screen is quiet: the music stops as it comes up (and as a
 * level starts loading), and the next place's music starts once it has gone. Its level is the settings' BGMの音量.
 */
UCLASS()
class CHAOSIMPACT_API UChaosImpactMusic : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** The settings' BGMの音量 changed: every music playing follows at once. */
	static void RefreshVolume();
	/** The results' podium has come up (after FINISH, which is quiet): the results' music starts. */
	static void NotifyResultsShown(const UObject* WorldContext);
	/** What is playing now (tests). */
	EChaosImpactBgm GetCurrent() const { return Current; }
	/** The music asset playing now, if any (tests). */
	FString GetCurrentTrackName() const;
	/** Tests: as if the battle music had just begun here (a late pick from the host still changes it). */
	void RestartBattleWindowForTests() { BattleStartedAt = FPlatformTime::Seconds(); }

private:
	bool Tick(float DeltaSeconds);
	EChaosImpactBgm Decide(UWorld* World, bool& bOutPaused) const;
	void Switch(UWorld* World, EChaosImpactBgm Next);
	UAudioComponent* Start(UWorld* World, const TCHAR* Asset, float FadeSeconds);
	float GetLevel(const UAudioComponent* Component) const;
	void ApplyVolume(float Seconds);
	void StartScene(UWorld* World);
public:
	/** How many battle songs take turns. */
	static int32 GetBattleSongCount();
	/** The host: the next battle's song (in turn; the first at random), for every machine (AChaosImpactGameState::BattleSong). */
	static int32 PickNextBattleSong();
private:
	void HandlePreLoadMap(const FString& MapName);
	bool IsLoadingShown() const;

	FTSTicker::FDelegateHandle Ticker;
	FDelegateHandle PreLoadHandle;
	/** The scene's music starts at this time (once the old music has faded out); 0: started. */
	double StartAt = 0.0;
	EChaosImpactBgm Current = EChaosImpactBgm::None;
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> Playing;
	/** The results' jingle, before CLAP!. */
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> Jingle;
	/** Music fading out, let go of once it has. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UAudioComponent>> Fading;
	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<USoundBase>> Loaded;
	/** Which battle song played last (the next battle has the next one). */
	int32 LastBattleSong = INDEX_NONE;
	/** When the battle music began here (a host's pick arriving a moment late still changes it). */
	double BattleStartedAt = 0.0;
	bool bPaused = false;
	/** The results phase: when it began here, and whether its podium is up yet. */
	double ResultsPhaseAt = 0.0;
	bool bResultsShown = false;
	float AppliedVolume = -1.0f;
	double SinceCheck = 0.0;
};
