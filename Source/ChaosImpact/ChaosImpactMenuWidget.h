#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ChaosImpactScreen.h"
#include "ChaosImpactMenuWidget.generated.h"

class UTexture2D;
class UEditableText;
class UChaosImpactCharacterSelect;
class UChaosImpactColourEdit;
class UChaosImpactSettingsScreen;

/** Native, resolution-independent front end. All hit regions and D-pad focus share one layout. */
UCLASS()
class UChaosImpactMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void ShowScreen(EChaosImpactScreen NewScreen);
	void Navigate(FKey Key);
	void ConfirmSelection();
	void GoBack();
	void RefreshEntries();
	int32 GetSelectedIndex() const { return SelectedIndex; }
	int32 GetEntryCount() const { return Entries.Num(); }
	bool HasLogo() const { return LogoTexture != nullptr; }
	EChaosImpactScreen GetScreen() const { return Screen; }
	/**
	 * Player entry: a controller button from any controller joins it. Slate sends each controller's keys only
	 * to its own user's focus, and this menu has only the first user's, so a second or third pad never reached
	 * NativeOnKeyDown. Returns true for the press that joined, so it does not also press a menu entry.
	 */
	bool TryJoinControllerFromAnyUser(const FKeyEvent& InKeyEvent);
	/** Every device's key and stick events, before Slate focus decides who gets them. True when consumed. */
	bool HandleAnyUserKeyDown(const FKeyEvent& InKeyEvent);
	bool HandleAnyUserAnalog(const FAnalogInputEvent& InAnalogEvent);
	UChaosImpactCharacterSelect* GetCharacterSelect() const { return CharacterSelect; }
	UChaosImpactColourEdit* GetColourEdit() const { return ColourEdit; }
	UChaosImpactSettingsScreen* GetSettingsScreen() const { return SettingsScreen; }
	void OpenTrainingBallPicker();
	bool IsTrainingBallPickerOpen() const { return bTrainingBallPickerOpen; }

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnKeyChar(const FGeometry& InGeometry, const FCharacterEvent& InCharEvent) override;
	virtual FReply NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnAnalogValueChanged(const FGeometry& InGeometry,
		const FAnalogInputEvent& InAnalogEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;

private:
	struct FMenuEntry
	{
		FSlateRect Rect;
		FString Title;
		FString Detail;
		FString Number;
		FLinearColor Accent;
		bool bDisabled = false;
	};

	TArray<FMenuEntry> Entries;
	/** The public tournaments' list: each row's listing (sorted by start). */
	TArray<int32> TourneyRowListing;
	/** 0..1 eased selection state per entry, advanced in NativeTick. */
	TArray<float> SelectBlend;
	/** Per-slot join tracking so a newly connected device plays its entry animation once. */
	bool bSlotJoined[4] = {};
	double SlotJoinedAt[4] = {};
	EChaosImpactScreen Screen = EChaosImpactScreen::Title;
	int32 SelectedIndex = 0;
	/** This menu turned the level's drawing off (the select screen); it turns it back on. */
	bool bLevelHiddenByMenu = false;
	/** Stage select: the card the cursor came down from (going back up returns to it). */
	int32 LastStageCard = 0;
	/** Mode select: the bottom button last left by going up, so going down comes back to it. */
	int32 LastModeBottom = INDEX_NONE;
	/** What the cursor was on last frame (a move to another entry clicks). */
	EChaosImpactScreen SoundScreen = EChaosImpactScreen::Title;
	int32 SoundSelectedIndex = INDEX_NONE;
	int32 PressedIndex = INDEX_NONE;
	/** Pause entries that leave the room/search need a second confirm on the same entry. */
	int32 ArmedIndex = INDEX_NONE;
	/** The pause menu's yes/no question (本当に部屋から抜けてもよろしいですか？ ...): the choice it is about, empty while none. */
	FString PauseConfirm;
	/** The rules screen's list of every kind of match (opened from its ルール row). */
	bool bModePicker = false;
	/** The title's hidden command, when the battle royale is installed: a card choosing it or the RPG (0 RPG, 1 battle royale; -1 closed). */
	/** Team select: each team's card (design space), drawn by PaintTeamCards; a click on one moves the mouse player there. */
	static FSlateRect TeamCardRect(int32 Team, int32 Teams, int32 Places, bool bCrashBall);
	/** The same for the match now on (an empty rect when it is not a team battle). */
	FSlateRect TeamCardRectNow(int32 Team) const;
	int32 PaintTeamCards(const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer, float T) const;
	/** The local player the keyboard and mouse belong to (null when none does). */
	class AChaosImpactPlayerController* GetTeamMouseController() const;
	FVector2D TeamMousePoint = FVector2D(-1000.0, -1000.0);
	/** Team select's motion: where each person's row is shown (it glides to where it belongs), their team, and when it changed. */
	mutable TMap<const APlayerState*, FVector2D> TeamRowShown;
	mutable TMap<const APlayerState*, TPair<int32, double>> TeamLastChange;
	mutable double TeamPaintedAt = 0.0;
	int32 SecretChoice = INDEX_NONE;
	double SecretChoiceAt = 0.0;
	void ConfirmSecretChoice();
	FBox2D SecretChoiceRect(int32 Index) const;
	double ModePickerAt = 0.0;
	void OpenModePicker(bool bOpen);
	double PauseConfirmAt = 0.0;
	/** The question and the line under it for PauseConfirm. */
	void GetPauseConfirmText(FString& OutQuestion, FString& OutNote) const;
	/** Where the pause menu was opened: the mode, and the room or match (the line under PAUSE). */
	FString GetPauseContext() const;
	void DisarmSelection();
	/** The results' numbers: the cursor among their rows, the tab arrows and the choices below. */
	void NavigateResults(FKey Key);
	void SwitchResultsTab(int32 Tab);
	bool IsResultsChoice(int32 Index) const;
	/** The results row the cursor was last on (it comes back there from the choices or the awards). */
	int32 ResultsRowCursor = 0;
	double ScreenStartedAt = 0.0;
public:
	/** When the screen shown now came up (a back or pause press right after is the same press arriving another way). */
	double GetScreenStartedAt() const { return ScreenStartedAt; }
private:
	/** When the last back was taken: a second one right after (the same press by another way, or a double press) waits. */
	double LastBackAt = -100.0;
	float AnimationSeconds = 0.0f;
	double LastAnalogNavigationAt = 0.0;
	void BuildEntries();
	void CloseTrainingBallPicker();
	bool bTrainingBallPickerOpen = false;
	bool bTrainingTeamsOpen = false;
	int32 HitTestEntry(const FGeometry& Geometry, const FVector2D& ScreenPosition) const;

	UFUNCTION()
	void HandleNameCommitted(const FText& Text, ETextCommit::Type CommitMethod);
	UFUNCTION()
	void HandleNameChanged(const FText& Text);
	void SubmitName();
	void SubmitPassword();
	void SubmitRoomNameEntry();
	FString GetPasswordString() const;
	/** Digit editing on the password screen. Returns true when the key was consumed. */
	bool HandlePasswordKey(const FKey& Key);

	UPROPERTY(Transient)
	TObjectPtr<UEditableText> NameInput;
	int32 PasswordDigits[4] = {0, 0, 0, 0};
	int32 PasswordCursor = 0;
	bool bNameFocusPending = false;
	float NameInputFontScale = 0.0f;
	FString LastCreateError;
	/** Sees every controller's presses before focus routing, for player entry. */
	TSharedPtr<class IInputProcessor> JoinInputProcessor;
	/** The room list is rebuilt when the session subsystem's list changes. */
	int32 LastRoomListingsVersion = -1;
	/** What a live screen's choices last showed (the 1v1 found, a matched 1v1's rematch): rebuilt when it changes. */
	FString LastLiveEntriesKey;
	FString GetLiveEntriesKey() const;
	/** オンラインメニュー: the 戦績 page, and the card of a 1v1 opponent found. */
	void PaintOnlineRecord(const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer, float Time) const;
	void PaintDuelFound(const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer, float Time) const;
	/** The online menu's top-right card (who is playing, the line to the relay) and the 回線チェック page. */
	void PaintOnlinePlayerCard(const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer, float Time) const;
	void PaintLineCheck(const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer, float Time) const;

	UPROPERTY(Transient)
	TObjectPtr<UChaosImpactCharacterSelect> CharacterSelect;

	UPROPERTY(Transient)
	TObjectPtr<UChaosImpactColourEdit> ColourEdit;

	/** ローカルトーナメント's registration screen. */
	UPROPERTY(Transient)
	TObjectPtr<class UChaosImpactTourneyEntry> TourneyEntry;
	/** Its controller-taking screen: the places, each person's model on it. */
	void PaintTourneySeat(const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer, float Time) const;
	void UpdateSeatPreviews(float DeltaSeconds);

	UPROPERTY(Transient)
	TObjectPtr<UChaosImpactSettingsScreen> SettingsScreen;
	bool IsSettingsActive() const;
	/** The results' podium: any button or click goes on to the numbers (and their menu). */
	void GoOnFromPodium();

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> LogoTexture;
	FSlateBrush LogoBrush;
	/** The title demo's picture (see AChaosImpactTitleDemo). */
	mutable FSlateBrush DemoBrush;

	/** Stage select: an in-game picture of each stage (Content/UI/StageSelect/Stage<N>.png), in StageIndex order. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTexture2D>> StageShots;
	TArray<FSlateBrush> StageShotBrushes;

	/** The 1v1 found's card: both players' characters, filmed far from the training (this player first). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<class AChaosImpactCharacterPreview>> DuelPreviews;
	TArray<FSlateBrush> DuelPreviewBrushes;
	/** Who the opponent's preview shows (made again when the offer changes). */
	FString DuelPreviewFor;
	void UpdateDuelPreviews(float DeltaSeconds);
	void CloseDuelPreviews();
};
