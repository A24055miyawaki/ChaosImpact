// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ChaosImpactCombatHealth.h"
#include "ChaosImpactTutorial.h"
#include "ChaosImpactBallTypes.h"
struct FChaosImpactTouchCustom;
struct FChaosImpactTouchLayout;
struct FChaosImpactTouchFrame;
#include "ChaosImpactChargeWidget.generated.h"

class APlayerState;
enum class EChaosImpactBallType : uint8;
class AChaosImpactCharacter;
class UProgressBar;
class UTextBlock;
class UBorder;
class UCanvasPanel;
class USizeBox;

/**
 * Runtime-built gameplay HUD. No Widget Blueprint setup is required.
 * Charge, stamina and ball stock are UMG widgets; the hit flash, respawn notice and
 * KO banner are painted in NativePaint so they can animate freely.
 */
UCLASS()
class UChaosImpactChargeWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * スマホ版 観戦: the touch buttons over the 観戦中 plate (0 ◀ the player before, 1 フリーカメラ / 選手視点, 2 ▶ the next), in
	 * the HUD's own units on a screen of Size.
	 */
	static FBox2D SpectatorTouchButton(int32 Index, const FVector2D& Size);
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	/** One carried ball, shaded and marked by its type, as the game shows it everywhere. Radius is in Paint's own units. */
	static void PaintBallIcon(const ChaosImpactPaint::FPainter& Paint, const FVector2D& Center, float Radius, bool bFilled,
		EChaosImpactBallType Type, float Growth, float Time);
	void SetChargeAlpha(float ChargeAlpha);
	void SetCharging(bool bCharging);
	/** Only used to detect damage for the screen-edge hit flash. */
	void SetHealth(float CurrentHealth, float MaxHealth);
	void SetStamina(float CurrentStamina, float MaxStamina);
	/** BallTypes: packed EChaosImpactBallType per slot (ChaosImpactBallTypes::Pack), slot 0 thrown next. */
	void SetBallInventory(int32 CurrentBalls, int32 MaximumBalls, uint16 BallTypes = 0);
	void ShowRespawn(const FString& DefeatedBy, float TotalSeconds);
	void UpdateRespawn(float RemainingSeconds, float TotalSeconds);
	void HideRespawn();
	/** Plays the KO banner on this player's screen after they eliminate someone. */
	void ShowKnockout(const FString& VictimName);
	/** A hit (not a knockout): a small card at the top right, under the KO banner's place: HIT! and who was hit
	 *  (bDealt), or 被弾 and who hit this player. The same again soon counts up on the same card. */
	void ShowHitFeed(bool bDealt, const FString& Name);
	bool IsAimGuideVisible() const;
	/** The two ball slots trade places after the player swaps hands. */
	void PlayBallSwap();
	/**
	 * 合体ボール: the two balls in the holder are drawn to each other and flash into one ball of both (Front and Back as
	 * they sat; Primary flies, Secondary rides along; Name is the pair's). It stays one until PlayBallSplit.
	 */
	void PlayBallFuse(EChaosImpactBallType Front, EChaosImpactBallType Back, EChaosImpactBallType Primary,
		EChaosImpactBallType Secondary, const FString& Name);
	/** Parted: thrown (the two spring back apart into their empty places and fade), or undone (back to their places). */
	void PlayBallSplit(bool bThrown);
	bool IsBallFusionShown() const { return bFusionShown; }
	/** One ball of two kinds: a swirl of the two colours with a small ball of each in it, shaded like the others. */
	static void PaintFusedBallIcon(const ChaosImpactPaint::FPainter& Paint, const FVector2D& Center, float Radius,
		EChaosImpactBallType A, EChaosImpactBallType B, float Time, float Whiteness);
	/** Same geometry as drawing: 1 pause, 2 training, 3 dash, 10/11 ball slots, 0 world. */
	int32 GetGameplayPointerHit() const;
	/**
	 * スマホ版: what of the HUD is at a point of the screen (Fraction: 0..1 across and down): 10 / 11 the ball holders
	 * (and the swap keycap), 13 合体 / 解除. 0: nothing (the field).
	 */
	int32 GetTouchHudHit(const FVector2D& Fraction) const;
	/** スマホ版: where the bottom corners' HUD (HP, ball holder) stands (the screen's bottom, or higher by touch). */
	float GetTouchCornerY(float Height, bool bRightSide) const;
	/** スマホ版: the player's own shift (this widget's units) and size for the HP (Vitals) or the ball holder (Balls). */
	void GetTouchHudPlacement(int32 Control, FVector2D& OutShift, float& OutSize) const;
	/**
	 * スマホ版: a space a HUD piece (FChaosImpactTouchCustom::Match, Feed, …) is drawn in where the player put it (moved and
	 * sized round its own point), and a box of it moved the same way. As it is when not played by touch.
	 */
	static FGeometry MoveForTouch(const APlayerController* Player, const FGeometry& Geometry, int32 Control);
	static FBox2D MoveBoxForTouch(const APlayerController* Player, const FBox2D& Box, const FVector2D& Size, int32 Control);
	/**
	 * The touch controls drawn on a screen (in play, and in the 配置 editor): ToLocal from the screen's pixels to the
	 * painter's units; Picked: the editor's picked control (INDEX_NONE: not the editor).
	 */
	static void PaintTouchPad(const ChaosImpactPaint::FPainter& Paint, const struct FChaosImpactTouchLayout& Layout, const FVector2D& ToLocal,
		const struct FChaosImpactTouchFrame& Frame, bool bButtons, bool bAimStick, bool bSwap, bool bFuse, bool bFused, bool bBalls, bool bDashReady,
		bool bCharging, float ChargeAlpha, int32 Picked);
	static constexpr int32 NotEditing = -100;
	/**
	 * スマホ版 設定 → ボタン・画面の配置: the battle HUD itself (HP, ball holder, clock, standings, KO / HIT, respawn card,
	 * corner buttons, hints) drawn with a match's worth of made-up state, placed by the layout being changed.
	 */
	void PaintLayoutPreview(const FGeometry& Geometry, FSlateWindowElementList& Elements, int32 Layer, const FChaosImpactTouchCustom& Custom,
		bool bButtons, bool bLeftHanded, const FString& Mode);
	/** スマホ版: whether a HUD piece is there at all in a kind of play (ChaosImpactSettings::GetTouchModeKey). */
	static bool IsPieceInMode(int32 Control, const FString& Mode);
	/** Pieces of the HUD, drawn in play and in the preview. */
	void PaintRespawnCard(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 BaseLayer, double Clock) const;
	void PaintKnockoutFeed(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 BaseLayer, double Clock,
		const FLinearColor& Accent) const;
	static void PaintStandingRow(const FGeometry& TopLeft, FSlateWindowElementList& OutDrawElements, int32 BaseLayer, float RowTop, int32 Row,
		int32 Rank, const FLinearColor& Accent, const FString& Label, int32 Points, bool bOwn, float ExtraPointsWidth = 0.0f);
	static void PaintCornerButton(const ChaosImpactPaint::FPainter& Button, const FBox2D& R, const FString& Label, float S, bool bHover);
	/** Where each piece came out in the last preview (the painter's local space), to frame it and pick it up. */
	const FBox2D* GetPreviewBox(const int32 Control) const { return PreviewBoxes.Find(Control); }
private:
	TMap<int32, FBox2D> PreviewBoxes;
public:
	/** The corner buttons (1 ポーズ, 2 練習設定, 3 ダッシュ, 4 準備OK, 5 観戦, 6 ボール化, 7 全体マップ, 8 ルール): shown now, and where. */
	bool IsCornerButtonShown(int32 Hit, bool bTouch) const;
	/** By touch: placed (and sized: OutScale) as the player put that button. */
	/** The room the top-left cards have before the corner buttons (HUD units; plenty off touch). */
	float TopLeftRoom(const FVector2D& Size, float S) const;
	FBox2D CornerButtonRect(const FVector2D& Size, float S, int32 Hit, bool bTouch, float* OutScale = nullptr) const;
	/** A corner button's own placing: its box moved and grown round its middle (TouchScale: the custom's units in the box's). */
	static FBox2D PlaceCornerButton(const FChaosImpactTouchCustom& Custom, int32 Hit, const FBox2D& Box, double TouchScale, float& OutScale);
	/** The placing in use now (the 配置 editor's picture, else this player's by touch), and its units in a widget of Size. */
	const FChaosImpactTouchCustom* GetCurrentTouchCustom(const FVector2D& Size, double& OutTouchScale) const;
	/** Where a corner button stands by touch before the player moves it (as now, or as a kind of play has its buttons). */
	FBox2D TouchCornerBase(const FVector2D& Size, float S, int32 Hit) const;
	static FBox2D TouchCornerBaseFor(const FVector2D& Size, float S, int32 Hit, const FString& Mode);
	static FBox2D CornerBaseFromShown(const FVector2D& Size, float S, int32 Hit, const bool (&bShown)[9]);
	void PaintGameplayPointer(const FGeometry& Geometry, FSlateWindowElementList& Elements, int32 Layer) const;
	/** スマホ版: the touch controls (the stick, and with ボタン方式 the buttons), as the fingers hold them. */
	void PaintTouchControls(const FGeometry& Geometry, FSlateWindowElementList& Elements, int32 Layer) const;
	/** A spectator's HUD: no HP or ball stock, a 観戦中 bar, and the standings seen from the followed player. */
	void SetSpectatorView(bool bSpectator) { bSpectatorView = bSpectator; }
	/** A mode with its own HUD (the removable battle royale) turns this whole HUD off: nothing here draws or takes the pointer. */
	void SetReplacedByOwnHud(bool bOff) { bReplacedByOwnHud = bOff; }
	/** Smoke over this player's view, 0-1 (a smoke ball caught them). */
	void SetBlindAmount(float Amount);
	/** How far each carried snowball has grown (0-1), shown on its icon. */
	void SetSnowGrowth(const float Right, const float Left) { SnowGrowth[0] = Right; SnowGrowth[1] = Left; }
	/** The nova parts carried (ChaosImpactNovaParts bits), shown over the ball stock. */
	void SetNovaParts(const uint8 Parts) { NovaParts = Parts; }
	/** Per-view combat readout, also usable by UI automation without accessing gameplay internals. */
	bool GetCombatHealthPresentation(const AChaosImpactCharacter* Character, ChaosImpactCombatHealth::FPresentation& Out) const;
	bool IsLastHealthWarningActive() const;
	/** Shared full-window room HUD; personal HUDs keep their own combat panels. */
	void PaintOnlineOverlay(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 BaseLayer) const;
	/** ルール確認: the room's decided rules in full over the whole window (Opener: whose key opened it). */
	void PaintRuleDetails(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 Layer,
		const class AChaosImpactPlayerController* Opener) const;

public:
	/** ソロモード (2026-11-19): the goal now and the stage's progress across the top, the boss's health in its fight (ChaosImpactSoloHud.cpp). */
	struct FSoloObjective
	{
		/** 0: on the way to the next room, 1: its enemies, 2: its boss. */
		int32 Kind = 0;
		int32 Rooms = 0;
		int32 Cleared = 0;
		int32 Left = 0;
		float RoomProgress = 0.0f;
		bool bBossNext = false;
		const class AChaosImpactSoloRoom* Active = nullptr;
		TWeakObjectPtr<const AChaosImpactCharacter> Boss;
	};
	bool GetSoloObjective(FSoloObjective& Out) const;
	/** A boss scene (AChaosImpactBossScene) over the game: how much of the HUD shows (0 none). */
	void SetSceneShown(const float Shown) { SceneShown = FMath::Clamp(Shown, 0.0f, 1.0f); SetRenderOpacity(GetRenderOpacity() > 0.0f ? SceneShown : 0.0f); }
	float SceneShown = 1.0f;

protected:
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	bool SupportsCombatHealth() const;
	void UpdateCombatHealth();
	void PaintCombatHealth(const FGeometry& Geometry,FSlateWindowElementList& Elements,int32 Layer) const;
	void PaintLastHealthWarning(const FGeometry& Geometry,FSlateWindowElementList& Elements,int32 Layer) const;
	TMap<TWeakObjectPtr<const AChaosImpactCharacter>,ChaosImpactCombatHealth::FState> CombatHealth;
	void RefreshSplitScreenDividers();
	uint8 NovaParts = 0;
	void RefreshPersonalAimGuide();
	void RefreshBallPickupAnimation();

	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanel> RootCanvas;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UBorder>> PersonalAimGuideBars;

	UPROPERTY(Transient)
	TObjectPtr<UProgressBar> ChargeBar;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ChargeLabel;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> ChargeFrame;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UProgressBar>> StaminaSegments;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UBorder>> BallSlots;

	/** Outer box of each slot; moved as a whole by the swap animation. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<USizeBox>> BallSlotBoxes;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UBorder>> BallSlotAccents;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> BallIcons;

	/** Small ファイア / アイス caption inside each filled slot. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> BallTypeLabels;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> BallSeams;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> InventoryCountLabel;

	/** Hidden outside the match phase of a VS match. */
	UPROPERTY(Transient)
	TObjectPtr<UBorder> StaminaPanel;

	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanel> InventoryPanel;

	/** VS: the "+N" pop-up next to this player's points. */
	int32 LastOwnPoints = 0;
	/** VS: the place shown in the rank badge, and when it last changed (tracked while painting). */
	mutable int32 ShownOwnRank = 0;
	/** バトルロイヤル: when this player was first seen knocked out (its place card plays from then), and in which match. */
	mutable double RoyaleOwnOutSeenAt = 0.0;
	mutable double RoyaleOwnOutMatch = -1.0;
	mutable int32 OwnRankDelta = 0;
	mutable double OwnRankChangedAt = -100.0;
	int32 PointsGained = 0;
	double PointsGainedAt = -100.0;
	/** クラッシュボール: the stage with the giant ball's route and the two goals (found once while painting). */
	mutable TWeakObjectPtr<class AChaosImpactStageBase> CrashStage;
	mutable TWeakObjectPtr<class AChaosImpactCrashGoal> CrashGoals[2];
	/** クラッシュボール: paints the giant ball's marker, the way this player's team pushes it and the goals. */
	void PaintCrashMarkers(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 BaseLayer,
		const class AChaosImpactGameState& Match, float S) const;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> VerticalDivider;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> VerticalDividerAccent;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> HorizontalDivider;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> HorizontalDividerAccent;

	float Health = 3.0f;
	float PointerChargeAlpha = 0.0f;
	bool bPointerCharging = false;
	bool bHealthKnown = false;
	double HitAt = -100.0;

	/** Bottom-left dial: HP arcs around the number, stamina as an open outer ring. */
	float MaxHealthValue = 3.0f;
	float StaminaValue = 5.0f;
	float MaxStaminaValue = 5.0f;
	/** Eases toward StaminaValue so a dash visibly drains its arc. */
	float DisplayedStamina = 5.0f;
	int32 LostHealthSegment = INDEX_NONE;
	/** A point won back (the heal ball): when, and which chevron lights up again. */
	double HealAt = -100.0;
	int32 GainedHealthSegment = INDEX_NONE;
	int32 SpentStaminaSegment = INDEX_NONE;
	double StaminaSpentAt = -100.0;
	/** Hidden with the rest of the gameplay HUD outside a VS match's play phase. */
	bool bVitalsHidden = false;
	bool bReplacedByOwnHud = false;
	bool bSpectatorView = false;

	float BlindAmount = 0.0f;
	double BlindShownAt = -100.0;
	float SnowGrowth[2] = {0.0f, 0.0f};
	/** Thick, drifting smoke over the whole view, under this player's own HUD but over everyone else's markers. */
	void PaintSmokeVeil(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 Layer) const;
	/** 0-1: how thick the smoke over the view is right now (billowing in, thinning out as it ends). */
	float GetSmokeCover() const;

	int32 CarriedBalls = 0;
	uint16 CarriedBallTypes = 0;
	double BallGainedAt = -100.0;
	int32 BallGainedSlot = INDEX_NONE;
	double BallSwappedAt = -100.0;
	bool bFusionShown = false;
	double FusionStartedAt = -100.0;
	double FusionSplitAt = -100.0;
	bool bFusionSplitThrown = false;
	EChaosImpactBallType FusionFront = EChaosImpactBallType::Normal;
	EChaosImpactBallType FusionBack = EChaosImpactBallType::Normal;
	EChaosImpactBallType FusionPrimary = EChaosImpactBallType::Normal;
	EChaosImpactBallType FusionSecondary = EChaosImpactBallType::Normal;
	FString FusionName;
	/** The fusion's own clock: seconds of shown frames, each counted at most a 30th (a hitch never skips the joining). */
	double FusionClock = 0.0;
	/** The joining, the one ball, the parting, over the holder (PaintBallInventory). */
	void PaintBallFusion(const FGeometry& Corner, FSlateWindowElementList& OutDrawElements, int32 BaseLayer, double Clock,
		float Time) const;
	/** Training's tutorial for this player (and the button hints it draws). */
	FChaosImpactTutorial Tutorial;

	/**
	 * The HUD's own areas this frame, in this widget's pixels (each mode's pieces register what they draw). Off-screen
	 * arrows, painted first, keep clear of the last frame's.
	 */
	mutable TArray<FBox2D> HudBoxes;
	mutable TArray<FBox2D> HudBoxesLast;
	mutable FGeometry HudPaintGeometry;
	/**
	 * The whole-stage map (its key): the stage from straight above, filmed into MapTarget all the while it is open (so
	 * what moves on the stage moves on it too), shown over the whole screen with everyone and the balls on it.
	 */
	UPROPERTY(Transient)
	TObjectPtr<class UTextureRenderTarget2D> MapTarget;
	/** The camera filming it, kept while the map is open. */
	UPROPERTY(Transient)
	TObjectPtr<class ASceneCapture2D> MapCamera;
	double MapShotAt = -100.0;
	double MapHiddenAt = -100.0;
	/** Where the map's middle is shown (it glides after a moving area, as the ground round this player). */
	FVector MapGlideCenter = FVector::ZeroVector;
	FSlateBrush MapBrush;
	FVector MapCenter = FVector::ZeroVector;
	float MapHalf = 0.0f;
	float MapYaw = 0.0f;
	bool bMapTaken = false;
	double MapOpenedAt = -100.0;
	bool bMapWasOpen = false;
	/** The square the map covers: this player's tournament arena, the match's stage, or the stage in the level. */
	bool FindMapArea(FVector& OutCenter, float& OutHalf) const;
	void TakeMapPicture(const FVector& Center, float Half, float Yaw);
	void PaintOverviewMap(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 Layer) const;
	/** Balls already announced as coming at this player from off screen (its sound plays once each). */
	mutable TSet<TWeakObjectPtr<class AChaosImpactBall>> WarnedBalls;
	/** Over the shoulder: what the crosshair is on, and since when (its red eases in). */
	mutable TWeakObjectPtr<const AActor> LastShoulderTarget;
	mutable double ShoulderTargetSince = 0.0;
	/** The perched-bird card's own area (left out when it looks for room under the top HUD). */
	mutable FBox2D SimaeCardBox = FBox2D(ForceInit);
	/** Sudden death: since when this player has stood outside the closing circle (its card's gauge), and its card's area. */
	mutable double ZoneOutSince = -1.0;
	mutable FBox2D ZoneCardBox = FBox2D(ForceInit);
	/** A rectangle of Space's own units (any anchor or slant) registered as taken. */
	void Occupy(const FGeometry& Space, float X, float Y, float W, float H) const;
	/** The same for what is drawn once over the whole window (the online room's panels): taken in every player's view. */
	void OccupyShared(const FGeometry& Space, float X, float Y, float W, float H) const;

	bool bRespawnVisible = false;
	double RespawnShownAt = -100.0;
	float RespawnRemaining = 0.0f;
	float RespawnTotal = 3.0f;
	int32 CountdownNumber = INDEX_NONE;
	double CountdownChangedAt = -100.0;
	FString DefeatedByName;

	double KnockoutAt = -100.0;
	int32 KnockoutStreak = 0;
	FString KnockoutVictim;

	struct FHitFeedEntry
	{
		double At = 0.0;
		bool bDealt = true;
		FString Name;
		int32 Count = 1;
	};
	/** Newest first; each shows for HitFeedSeconds. */
	TArray<FHitFeedEntry> HitFeed;
	static constexpr float HitFeedSeconds = 1.8f;

	/** When each online room member first appeared, for the member list slide-in. */
	TMap<TWeakObjectPtr<APlayerState>, double> MemberSeenAt;

	/** A small note in the top-right corner: someone joined, left, or switched between playing and watching. */
	struct FRoomToast
	{
		/** Named from the member while they are here (their name arrives a moment after they do). */
		TWeakObjectPtr<APlayerState> Member;
		FString Name;
		FString Message;
		FLinearColor Accent = FLinearColor::White;
		double At = 0.0;
	};
	struct FRoomMemberSeen
	{
		FString Name;
		bool bSpectating = false;
		double FirstSeenAt = 0.0;
		/** Arrived just now: announced a moment later, once their chosen name has come through. */
		bool bJoinPending = false;
	};
	TArray<FRoomToast> RoomToasts;
	TMap<TWeakObjectPtr<APlayerState>, FRoomMemberSeen> RoomMembersSeen;
	/** Members already in the room when this screen arrived are learned quietly, without notes. */
	double RoomWatchStartedAt = 0.0;
	void UpdateRoomToasts(const class AChaosImpactGameState* Room);
	void AddRoomToast(APlayerState* Member, const FString& Name, const TCHAR* Message, const FLinearColor& Accent);
	/** Solo mode: STAGE CLEAR! slamming in over a burst, the stage's name, and the way back to the map filling. */
	void PaintSoloClear(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 BaseLayer) const;
	void PaintSoloObjective(const FGeometry& Geometry, FSlateWindowElementList& Elements, int32 Layer) const;
	/** The solo HUD's motion: when what is shown last changed (slides, pops, the room-clear flash, the boss bar's trail). */
	struct FSoloHudAnim
	{
		double ShownAt = -1.0;
		int32 Kind = -1;
		double KindAt = -10.0;
		int32 Left = -1;
		double LeftAt = -10.0;
		int32 Cleared = -1;
		double ClearedAt = -10.0;
		float Fill = 0.0f;
		TWeakObjectPtr<const AChaosImpactCharacter> Boss;
		double BossAt = -10.0;
		float BossTrail = 1.0f;
		float BossLast = 1.0f;
		double BossHitAt = -10.0;
		double LastAt = 0.0;
	};
	mutable FSoloHudAnim SoloAnim;
	void PaintRoomToasts(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 BaseLayer,
		float Top) const;

	/** Names above every character in view, and edge arrows toward nearby characters outside it. */
	void PaintPlayerMarkers(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements,
		int32 BaseLayer) const;
	/** HUD size for a view of this size: larger in split screen, where each view is only part of the screen. */
	float GetHudScale(double Width, double Height) const;

	/** Ball stock in the bottom-right corner: shaded balls per type on a slanted plate, with pickup and swap motion. */
	void PaintBallInventory(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements,
		int32 BaseLayer) const;
	/** HP and stamina dial in the bottom-left corner of this player's view. */
	void PaintVitals(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements,
		int32 BaseLayer) const;
	/** Spectating: what is shown (a player's camera or the free camera) and the camera keys. */
	void PaintSpectatorBar(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements,
		int32 BaseLayer) const;
	/** VS opening banners, match timer and standings, and the results. Drawn in every split-screen view. */
	void PaintVersusMatch(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements,
		int32 BaseLayer) const;
};
