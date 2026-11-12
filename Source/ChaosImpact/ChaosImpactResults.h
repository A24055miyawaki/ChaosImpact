#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Layout/SlateRect.h"
#include "Styling/SlateBrush.h"
#include "UObject/Object.h"
#include "ChaosImpactResults.generated.h"

class AChaosImpactCharacterPreview;
class AChaosImpactGameState;
class APlayerController;
class FSlateWindowElementList;
class UAnimSequenceBase;
class UMaterialInstanceDynamic;
class UPointLightComponent;
class UProceduralMeshComponent;
class USceneCaptureComponent2D;
class USpotLightComponent;
class UStaticMeshComponent;
class UTextureRenderTarget2D;
struct FGeometry;

/** One competitor in a VS match's results, taken as the results begin (so it outlives anyone leaving the room). */
struct FChaosImpactResultEntry
{
	FString Name;
	int32 Character = 0;
	int32 Colour = 0;
	/** カスタムカラー parts as the model wore them (empty: its usual colours). */
	TArray<uint8> CustomColours;
	int32 Team = INDEX_NONE;
	bool bBot = false;
	/** Which player on this machine (P1-P4), or INDEX_NONE. */
	int32 LocalIndex = INDEX_NONE;
	int32 Rank = 1;
	int32 Points = 0;
	int32 RoundsWon = 0;
	int32 Knockouts = 0;
	int32 Throws = 0;
	int32 Hits = 0;
	int32 TimesHit = 0;
	int32 Dodges = 0;
	int32 SpecialThrows = 0;
	int32 DriveHits = 0;
	/** Centimetres. */
	int32 LongestHit = 0;
	/** クラッシュボール: throws that pushed the giant ball towards the opponents' goal. */
	int32 CrashPushes = 0;
	TArray<int16> History;
	/** Their award (one each): its title, what earned it, and which picture goes with it. */
	FString AwardTitle;
	FString AwardNote;
	int32 AwardIcon = 0;
	/** Their line on the graph and their marks. */
	FLinearColor Color = FLinearColor::White;
};

/** A VS match's results: everyone in finishing order, the teams' totals, and each player's award. */
struct FChaosImpactResultsData
{
	TArray<FChaosImpactResultEntry> Entries;
	bool bTeams = false;
	bool bCrashBall = false;
	bool bDuel = false;
	bool bBattleRoyale = false;
	FString ScoreText(const FChaosImpactResultEntry& Entry) const
	{
		return bBattleRoyale ? FString::Printf(TEXT("%d位"), Entry.Rank)
			: bDuel ? FString::Printf(TEXT("%d勝"), Entry.RoundsWon) : FString::Printf(TEXT("%d pt"), Entry.Points);
	}
	bool bCrashKnockout = false;
	/** Team battle: (team, points), best first. */
	TArray<TPair<int32, int32>> TeamTotals;
	bool bTie = false;
	/** The winner's colour (their team's in a team battle), for the stage and the banner. */
	FLinearColor WinnerColor = FLinearColor::White;
	FString Headline;
	FString WinnerName;

	/** Everyone who played, as the match left them, with this machine's players marked. */
	static FChaosImpactResultsData Capture(const AChaosImpactGameState* Match, const UObject* WorldContext);
	/** One award each: the best at something they are best at, or a kind word. */
	void ChooseAwards();
};

/**
 * The stage the winner is filmed on, far outside the level, built like an awards stage: a round stage on a glossy floor,
 * one tall round pedestal with a gold rim, a curved wall of panels behind washed from below by lights in the winner's
 * colour with slim LED bars between them. Only the winner (the winning team, or everyone tied for first) stands on it:
 * they drop in out of the dark, land with a shockwave and paper confetti, and celebrate. The camera starts wide on the
 * empty stage, goes low for the landing, then settles with the winner on the right of the picture and the left open for
 * the headline. It films into a texture the results draw full screen.
 */
UCLASS(NotPlaceable, Transient)
class AChaosImpactPodiumStage : public AActor
{
	GENERATED_BODY()

public:
	AChaosImpactPodiumStage();
	static constexpr int32 PictureWidth = 1920;
	static constexpr int32 PictureHeight = 1080;

	// The show, in seconds from the cut to the stage.
	static constexpr float BlackoutAt = 0.7f;
	static constexpr float DropAt = 0.95f;
	static constexpr float LandAt = 1.3f;
	static constexpr float SettledAt = 3.5f;
	/** At most this many stand on the pedestal (a big tie for first shows the first of them). */
	static constexpr int32 MaxFigures = 8;

	void Build(const FChaosImpactResultsData& Data);
	/** The same a little at a time (one figure a step), so building it never holds a frame up. */
	void BeginBuild(const FChaosImpactResultsData& Data);
	/** True once everything is built (and drawn once, so its first appearance does not stall). */
	bool BuildStep();
	bool IsBuilt() const { return BuildStage < 0; }
	/** The show at Seconds since the cut; DeltaSeconds moves the figures. Works while the world is paused. */
	void UpdateShow(float Seconds, float DeltaSeconds);
	UTextureRenderTarget2D* GetPicture() const { return Picture; }
	/** Where a point in the world shows in the picture (0-1 across and down); false behind the camera. */
	bool Project(const FVector& WorldPoint, FVector2D& OutPicture) const;
	/** Above the head of the figure for results entry EntryIndex (false when they have none, or not yet); when it appeared. */
	bool GetFigureHead(int32 EntryIndex, FVector& OutWorld, float* OutAppearAt = nullptr) const;
	/** The results entries standing on the pedestal. */
	const TArray<int32>& GetWinnerEntries() const { return Winners; }
	virtual void Destroyed() override;

private:
	struct FFigure
	{
		int32 Entry = INDEX_NONE;
		FVector Spot = FVector::ZeroVector;
		bool bShown = false;
		bool bLanded = false;
		float NextCheerAt = 0.0f;
	};

	void AddPedestal(float Radius, float Height);
	void BuildArena();
	/** Paper confetti: where each piece is at Seconds since it began (falling, fluttering, then lying on the floor). */
	void UpdateConfetti(float Seconds);
	void BuildNextFigure();
	/** Half the width of the row of winners (arms and all), in centimetres. */
	float GetRowHalfWidth() const;
	FChaosImpactResultsData BuildData;
	/** The results entries on the pedestal, in finishing order. */
	TArray<int32> Winners;
	/** 0 nothing yet, 1 the figures, -1 done. */
	int32 BuildStage = 0;
	int32 BuildMember = 0;
	void SetCamera(const FVector& Location, const FVector& LookAt, float Fov);

	UPROPERTY()
	TObjectPtr<USceneComponent> Root;

	UPROPERTY()
	TObjectPtr<USceneCaptureComponent2D> Camera;

	UPROPERTY()
	TObjectPtr<USpotLightComponent> WinnerSpot;

	UPROPERTY()
	TObjectPtr<UPointLightComponent> KeyLight;

	UPROPERTY()
	TObjectPtr<UPointLightComponent> BackGlowLight;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> Picture;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AChaosImpactCharacterPreview>> FigureActors;

	/** The soft light behind the winner, so they stand off the wall. */
	UPROPERTY(Transient)
	TObjectPtr<UPointLightComponent> RimLight;

	/** Lights at the foot of the back wall, washing it upward. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<USpotLightComponent>> WallLights;

	/** The LED bars between the wall's panels (a slow wave runs across them). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> LedMaterials;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> StageEdgeMaterial;

	/** Confetti, one mesh per paper colour. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UProceduralMeshComponent>> Confetti;

	/** The pedestal's gold rims, lit as the winner lands. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> NeonMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> Shockwave;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ShockwaveMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequenceBase> IdleAnimation;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequenceBase> FallAnimation;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequenceBase> LandAnimation;

	TArray<FFigure> Figures;
	TArray<float> LedPhases;
	FLinearColor WinnerColor = FLinearColor::White;
	float ShowSeconds = 0.0f;
	float WinnerTopZ = 0.0f;
	FVector CameraLocation = FVector::ZeroVector;
	FRotator CameraRotation = FRotator::ZeroRotator;
	float CameraFov = 32.0f;
};

/**
 * The results of a VS match, drawn over the whole screen once FINISH has been called: page one is the podium show
 * (with WINNER and the names cut in over it), page two everyone's numbers, awards and how the points went over the
 * match. Local: the rematch menu sits under it. Online: each player goes back to the lobby when they choose, and the
 * next match waits for everyone. The match announcer widget owns and draws it.
 */
UCLASS()
class UChaosImpactResultsView : public UObject
{
	GENERATED_BODY()

public:
	/** Starts with the results, and ends when they do (or, online, when this machine goes back to the lobby). */
	void Tick(APlayerController* Owner, float DeltaSeconds);
	bool IsActive() const { return bActive; }
	/** The show is over: the prompt to go on comes up. */
	bool IsRevealed() const { return bActive && ShowSeconds >= RevealSeconds; }
	/** The winner has landed: any button (or a click) goes on to the numbers. */
	bool CanAdvance() const { return bActive && ShowSeconds >= AChaosImpactPodiumStage::LandAt + 0.5f; }
	/** 0 the podium, 1 the numbers (with the menu under them). There is no way back to the podium. */
	int32 GetPage() const { return Page; }
	void ShowStats();
	/** The awards page (and the arrow to it). Off for now: the numbers and the graph only. */
	static constexpr bool bAwardsEnabled = false;
	/** Page two: 0 the numbers and the graph, 1 the awards (an arrow at the screen's edge goes between them). */
	int32 GetStatsTab() const { return StatsTab; }
	void SetStatsTab(int32 Tab);
	/** The graph shows only this entry's line; the same entry again shows everyone's (INDEX_NONE). */
	void ToggleGraphFocus(int32 Entry);
	int32 GetGraphFocus() const { return GraphFocus; }
	/** Where a row of the numbers is (in the 1600 x 900 design space), and the arrow from a tab to the other. */
	static FSlateRect GetStatsRowRect(int32 Index, int32 Count);
	static FSlateRect GetTabArrowRect(int32 Tab);
	/** Online: this machine leaves its results for the lobby. */
	void Leave();
	float GetShowSeconds() const { return ShowSeconds; }
	/** Seconds until an online results screen returns; negative for local matches/inactive screens. */
	float GetAutoReturnRemaining() const;
	const FChaosImpactResultsData& GetData() const { return Data; }
	AChaosImpactPodiumStage* GetPodium() const { return Podium; }
	int32 Paint(const FGeometry& Allotted, FSlateWindowElementList& Elements, int32 Layer) const;
	void Stop();
	/** When the rating card's own show started (FPlatformTime; 0 while it has nothing new to show yet). */
	double GetRatingAnimStart() const;

	/** Seconds into the show when it has settled and asks for a button to go on. */
	static constexpr float RevealSeconds = 3.9f;

private:
	void Start(const AChaosImpactGameState* Match, APlayerController* Owner);
	int32 PaintPodiumPage(const FGeometry& Design, const FGeometry& Allotted, FSlateWindowElementList& Elements, int32 Layer) const;
	int32 PaintStatsPage(const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer) const;
	int32 PaintNumbersTab(const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer, float Alpha, float Slide) const;
	int32 PaintAwardsTab(const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer, float Alpha, float Slide) const;
	/**
	 * ランダムマッチ: this player's rating for the game (集計中 / the count from before to after with the change / 計測中 /
	 * the measuring just over: the rating shown for the first time). Top left of the podium page, top right of the numbers.
	 */
	int32 PaintRating(const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer) const;

	FChaosImpactResultsData Data;

	UPROPERTY(Transient)
	TObjectPtr<AChaosImpactPodiumStage> Podium;

	mutable FSlateBrush PictureBrush;
	double ResultsKey = -1.0;
	double StartedAtReal = 0.0;
	double PageChangedAt = 0.0;
	float ShowSeconds = 0.0f;
	int32 Page = 0;
	int32 StatsTab = 0;
	int32 GraphFocus = INDEX_NONE;
	double TabChangedAt = 0.0;
	double FocusChangedAt = 0.0;
	bool bActive = false;
	bool bOnline = false;
	/** Online: this machine went back to the lobby (the results stay closed until the next match). */
	bool bLeft = false;
	/** The final numbers have been taken again (they arrive with FINISH). */
	bool bRecaptured = false;
	/** A ランダムマッチ (1v1 or 8 players): the rating card is shown. */
	bool bRated = false;
	TWeakObjectPtr<APlayerController> RatedOwner;
	/** When the rating card first came up, and whether its sound has been played. */
	mutable double RatingShownAt = 0.0;
	bool bRatingSoundPlayed = false;
	/** The rating's digits that have stopped turning (a tick each). */
	int32 RatingDigitTicks = 0;
};
