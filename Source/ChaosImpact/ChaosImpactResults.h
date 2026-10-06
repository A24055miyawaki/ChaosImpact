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
	int32 Team = INDEX_NONE;
	bool bBot = false;
	/** Which player on this machine (P1-P4), or INDEX_NONE. */
	int32 LocalIndex = INDEX_NONE;
	int32 Rank = 1;
	int32 Points = 0;
	int32 Knockouts = 0;
	int32 Throws = 0;
	int32 Hits = 0;
	int32 TimesHit = 0;
	int32 Dodges = 0;
	int32 SpecialThrows = 0;
	int32 DriveHits = 0;
	/** Centimetres. */
	int32 LongestHit = 0;
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
 * The podium the results are filmed on, far outside the level: a dark arena, the winner on a tall neon pedestal in
 * front, second and third kneeling on lower ones behind, slashes of the winner's colour across the back wall, a glow,
 * drifting haze and floor rings; spotlights, a shockwave when the winner lands. Its camera moves through the show
 * (low on the winner, then pulling back to the whole podium) into a texture the results draw full screen.
 */
UCLASS(NotPlaceable, Transient)
class AChaosImpactPodiumStage : public AActor
{
	GENERATED_BODY()

public:
	AChaosImpactPodiumStage();
	static constexpr int32 PictureWidth = 1920;
	static constexpr int32 PictureHeight = 1080;

	// The show, in seconds from the cut to the podium.
	static constexpr float ThirdAt = 0.45f;
	static constexpr float SecondAt = 1.05f;
	static constexpr float BlackoutAt = 1.75f;
	static constexpr float DropAt = 2.05f;
	static constexpr float LandAt = 2.4f;
	static constexpr float SettledAt = 4.6f;

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
	virtual void Destroyed() override;

private:
	struct FFigure
	{
		int32 Entry = INDEX_NONE;
		/** 0 a winner, 1 second, 2 third. */
		int32 Place = 0;
		FVector Spot = FVector::ZeroVector;
		float AppearAt = 0.0f;
		bool bShown = false;
		bool bLanded = false;
		float NextCheerAt = 0.0f;
		float IdleAt = -1.0f;
	};

	void AddPedestal(const FVector& Base, float Radius, float Height, const FLinearColor& Neon, int32 Place);
	void BuildArena();
	void BuildNextFigure();
	FChaosImpactResultsData BuildData;
	/** Entries per place (0 the winner or winning team, 1 and 2 behind). */
	TArray<TArray<int32>> Places;
	/** 0 nothing yet, 1 the figures, -1 done. */
	int32 BuildStage = 0;
	int32 BuildPlace = 0;
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
	TArray<TObjectPtr<UPointLightComponent>> RimLights;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> Picture;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AChaosImpactCharacterPreview>> FigureActors;

	/** Back-wall slashes, haze puffs and appearance pillars, animated through the show. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Slashes;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Haze;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Pillars;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> PillarMaterials;

	/** Pedestal neon per place (0-2), lit as each one appears. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> NeonMaterials;

	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> Shockwave;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ShockwaveMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequenceBase> IdleAnimation;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequenceBase> KneelAnimation;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequenceBase> FallAnimation;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequenceBase> LandAnimation;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequenceBase> CheerAnimation;

	TArray<FFigure> Figures;
	TArray<float> SlashPhases;
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
	const FChaosImpactResultsData& GetData() const { return Data; }
	AChaosImpactPodiumStage* GetPodium() const { return Podium; }
	int32 Paint(const FGeometry& Allotted, FSlateWindowElementList& Elements, int32 Layer) const;
	void Stop();

	/** Seconds into the show when it has settled and asks for a button to go on. */
	static constexpr float RevealSeconds = 5.0f;

private:
	void Start(const AChaosImpactGameState* Match, APlayerController* Owner);
	int32 PaintPodiumPage(const FGeometry& Design, const FGeometry& Allotted, FSlateWindowElementList& Elements, int32 Layer) const;
	int32 PaintStatsPage(const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer) const;
	int32 PaintNumbersTab(const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer, float Alpha, float Slide) const;
	int32 PaintAwardsTab(const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer, float Alpha, float Slide) const;

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
};
