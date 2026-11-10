#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateBrush.h"
#include "ChaosImpactTournament.h"
#include "ChaosImpactTournamentWidget.generated.h"

class AChaosImpactCharacterPreview;
class AChaosImpactGameState;
class AChaosImpactPlayerController;
class AChaosImpactPlayerState;
struct FChaosImpactDuelState;

/**
 * トーナメント: one local player's own tournament screen, drawn in their part of a split screen.
 *
 * - The bracket: the entrants on both sides, rounds closing in on the final in the middle; who went through lights
 *   up along the lines, the matches being played pulse with LIVE (pick one to watch it), the player's own path stands
 *   out, the champion is crowned in the middle at the end.
 * - The menu (Tab / View): practise, watch a match, or the bracket, whenever the player likes.
 * - The call when their match comes up, and in an arena (playing or watching) the match's own calls: the balls drawn,
 *   ROUND, FIGHT, the round's end, the winner.
 * - The host's choice for two CPUs next (one match at a time): watch it, skip it, skip them all.
 * Keys, pads and the mouse all work it; each player's own device works their own screen.
 */
UCLASS()
class UChaosImpactTournamentWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	enum class EPanel : uint8 { None, Menu, Watch, Bracket };

	EPanel GetPanel() const { return Panel; }
	/** True while one of its screens (a panel, the call to one's match, a question) lies over the play (button hints stay off then). */
	bool CoversPlay() const;
	void OpenPanel(EPanel InPanel);
	void ClosePanel();

protected:
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeDestruct() override;

private:
	// ---- Reading the tournament ----
	AChaosImpactPlayerController* GetOwner() const;
	const AChaosImpactGameState* GetMatch() const;
	const AChaosImpactPlayerState* GetMe() const;
	int32 GetMyEntrant() const;
	/** The arena this player plays or watches in (INDEX_NONE: none). */
	int32 GetMyArena() const;
	double GetNow() const;

	// ---- Input ----
	void HandleInput();
	bool WasPressed(std::initializer_list<FKey> Keys) const;
	void MoveCursor(int32 Columns, int32 Rows);
	void Confirm();
	void Back();
	/** The bracket's matches that can be picked (both sides known), for the cursor. */
	TArray<int32> GetPickableMatches() const;
	/** Matches being played (to watch). */
	TArray<int32> GetLiveMatches() const;
	void ChooseWait(EChaosImpactTournamentWait Wait, int32 WatchMatch = INDEX_NONE);

	// ---- Layout (1600 x 900 design space, fitted into this player's part of the screen) ----
	FGeometry MakeDesign(const FGeometry& Allotted) const;
	FVector2D ToDesign(const FGeometry& Allotted, const FVector2D& ScreenPoint) const;
	float CardWidth() const;
	float CardHeight() const;
	/** Where an entrant's card of a match stands (its top-left), and whether it is on the right half. */
	FVector2D CardAt(int32 InRound, int32 Slot, int32 Side, bool* bOutRight = nullptr) const;
	float CardY(int32 InRound, int32 Slot, int32 Side) const;
	FBox2D MatchBox(int32 MatchIndex) const;
	FBox2D MenuRect(int32 Index) const;
	FBox2D WatchRect(int32 Index) const;
	FBox2D AskRect(int32 Index) const;

	// ---- Painting ----
	void PaintBracket(const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer, float Alpha) const;
	void PaintCard(const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer, const FVector2D& At, int32 Entrant,
		int32 MatchIndex, int32 Side, float Alpha, float Scale = 1.0f) const;
	void PaintChampion(const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer, float Alpha) const;
	void PaintMenu(const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer) const;
	void PaintWatchList(const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer) const;
	void PaintStatus(const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer) const;
	void PaintCall(const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer, int32 MatchIndex) const;
	void PaintArena(const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer, int32 Arena) const;
	void PaintAsk(const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer) const;
	void PaintRoundBanner(const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer) const;
	/** A public tournament's room before it starts: its name, the time left, who has come. */
	void PaintPublicWait(const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer) const;
	void PaintPortrait(const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer, int32 Entrant, const FBox2D& Box,
		float Alpha) const;
	FString DescribeMyStatus() const;
	FLinearColor EntrantColor(int32 Entrant) const;

	// ---- Portraits (each look an entrant has, filmed once) ----
	void UpdatePortraits(float DeltaSeconds);
	FString PortraitKey(int32 Entrant) const;
	UPROPERTY(Transient)
	TArray<TObjectPtr<AChaosImpactCharacterPreview>> Portraits;
	TMap<FString, int32> PortraitIndex;
	TArray<FSlateBrush> PortraitBrushes;
	TArray<double> PortraitBornAt;
	TArray<int32> PortraitCharacter;
	int32 PortraitFrame = 0;

	EPanel Panel = EPanel::None;
	int32 MenuCursor = 0;
	int32 WatchCursor = 0;
	int32 AskCursor = 0;
	/** The bracket's selected match (INDEX_NONE: none). */
	int32 BracketCursor = INDEX_NONE;
	/** When the panel last opened (its own little entrance). */
	double PanelOpenedAt = -100.0;
	/** The tournament this screen last saw start (a new one opens on its bracket). */
	double SeenStartedAt = -1.0;
	/** The champion's moment was opened for (once). */
	bool bChampionShown = false;
	/** The champion's screen put away (Enter / A) on this screen. */
	bool bChampionDismissed = false;
	void PaintPlacementStrip(const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer, float Alpha) const;
	int32 SeenCalledMatch = INDEX_NONE;
	double NextStickAt = 0.0;
	bool bStickHeld = false;
	float LocalTime = 0.0f;
	/** The design space's width for this player's part of the screen (900 high; 1000 to 1600 wide). */
	mutable float DesignWidth = 1600.0f;
	FString Toast;
	double ToastAt = -100.0;
};
