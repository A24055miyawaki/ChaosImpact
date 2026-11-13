// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ChaosImpactCombatHealth.h"
#include "ChaosImpactTutorial.h"
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
	void SetBallInventory(int32 CurrentBalls, int32 MaximumBalls, uint8 BallTypes = 0);
	void ShowRespawn(const FString& DefeatedBy, float TotalSeconds);
	void UpdateRespawn(float RemainingSeconds, float TotalSeconds);
	void HideRespawn();
	/** Plays the KO banner on this player's screen after they eliminate someone. */
	void ShowKnockout(const FString& VictimName);
	bool IsAimGuideVisible() const;
	/** The two ball slots trade places after the player swaps hands. */
	void PlayBallSwap();
	/** Same geometry as drawing: 1 pause, 2 training, 3 dash, 10/11 ball slots, 0 world. */
	int32 GetGameplayPointerHit() const;
	void PaintGameplayPointer(const FGeometry& Geometry, FSlateWindowElementList& Elements, int32 Layer) const;
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
	uint8 CarriedBallTypes = 0;
	double BallGainedAt = -100.0;
	int32 BallGainedSlot = INDEX_NONE;
	double BallSwappedAt = -100.0;
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
