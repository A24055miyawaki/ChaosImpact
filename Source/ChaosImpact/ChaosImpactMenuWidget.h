#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ChaosImpactScreen.h"
#include "ChaosImpactMenuWidget.generated.h"

class UTexture2D;
class UEditableText;
class UChaosImpactCharacterSelect;
class UChaosImpactColourEdit;
class UChaosImpactGallery;
class UChaosImpactRecordBoards;
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
	/** Puts the cursor on an entry (tests, and the pad's way round checked from each one). */
	void SelectEntry(const int32 Index) { SelectedIndex = FMath::Clamp(Index, 0, FMath::Max(0, Entries.Num() - 1)); }
	bool IsEntryDisabled(const int32 Index) const { return Entries.IsValidIndex(Index) && Entries[Index].bDisabled; }
	FString GetEntryTitle(const int32 Index) const { return Entries.IsValidIndex(Index) ? Entries[Index].Title : FString(); }
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
	/** スマホ版: this screen shows the touch back button (top left). */
	bool HasTouchBackButton() const;
	/** スマホ版 ペイント: fingers go to the studio itself (each finger its own), not made into a mouse. */
	bool IsPaintStudioTouch() const;
	/** スマホ版: laid out for touch; the screens laid out upright too; the canvas (landscape or upright) chosen each frame. */
	bool IsPhoneLayout() const;
	/** The design canvas now: 1600 x 900, or 900 x 1600 (スマホ版 upright, for the screens laid out that way). */
	static bool IsUprightCanvas();
	/** スマホ版 (a phone, or -CITouch): played by touch, no keys named. */
	static bool IsPhoneBuild();
	static bool HasPortraitLayout(EChaosImpactScreen Which);
	/** The online menus (the online menu, 合言葉マッチ's screens, 戦績, 回線チェック, トーナメント): laid out for a phone too. */
	static bool IsOnlineMenuScreen(EChaosImpactScreen Which);
	/** スマホ版: the online menus' entries in their phone places (both ways). */
	void PlaceOnlineEntriesForPhone();
	void UpdateMenuCanvas(const FGeometry& Geometry);
	bool HandleAnyUserKeyUp(const FKeyEvent& InKeyEvent);
	bool HandleAnyUserAnalog(const FAnalogInputEvent& InAnalogEvent);
	UChaosImpactCharacterSelect* GetCharacterSelect() const { return CharacterSelect; }
	UChaosImpactColourEdit* GetColourEdit() const { return ColourEdit; }
	class UChaosImpactTourneyEntry* GetTourneyEntry() const { return TourneyEntry; }
	UChaosImpactGallery* GetGallery() const { return Gallery; }
	UChaosImpactRecordBoards* GetRecordBoards() const { return RecordBoards; }
	bool IsBoardScreen() const { return Screen == EChaosImpactScreen::OnlineHistory || Screen == EChaosImpactScreen::OnlineRanking; }
	UChaosImpactSettingsScreen* GetSettingsScreen() const { return SettingsScreen; }
	void OpenTrainingBallPicker();
	bool IsTrainingBallPickerOpen() const { return bTrainingBallPickerOpen; }

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	/** Takes the keyboard / pad focus back when nothing of the menus' has it (no click needed first). */
	void KeepMenuFocus(class AChaosImpactPlayerController* Player);
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
	/** PC版 モード選択's ゲーム終了 (index 7): pressed once (it asks to be pressed again within a few seconds). */
	double QuitArmedAt = -100.0;
	bool IsQuitArmed() const;
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
	/** The rules page's bottom row (戻る 5, ボールスイッチ 6, 決定 4): where the cursor was left on it (down from the rows goes back there). */
	int32 RulesBottomCursor = 4;
	/** The rules screen's ボールスイッチ: every special ball as a tile to switch on / off (over the rules page). */
	bool bBallSwitch = false;
	/** 「訓練場」で あそんでみよう！: over the mode select once a run, while 訓練場 is not all cleared. */
	bool bDojoInvite = false;
	double DojoInviteAt = 0.0;
	int32 DojoInviteChoice = 0;
	UPROPERTY(Transient)
	TObjectPtr<class UChaosImpactDojoVideo> DojoInviteVideo;
	/** The lesson list itself, small in the invitation, its cursor going round by itself. */
	UPROPERTY(Transient)
	TObjectPtr<class UChaosImpactDojoMenu> DojoInviteMenu;
	double DojoInviteStepAt = 0.0;
	int32 DojoInviteSteps = 0;
	void CloseDojoInvite(bool bGo);
	bool HandleDojoInviteKey(const FKeyEvent& InKeyEvent);
	static FBox2D DojoInviteButton(int32 Index);
	void PaintDojoInvite(const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer, float Time) const;
	double BallSwitchAt = 0.0;
	int32 LastBallSwitchTile = 0;
	/** When leaving with every kind off was refused (the note flashes). */
	double BallSwitchDeniedAt = -100.0;
	/** Ball tiles in a row on the ボールスイッチ. */
	static constexpr int32 BallSwitchColumns = 9;
	/** The list of kinds, upright: where each kind's heading stands (set as its tiles are laid out). */
	float ModePickerHeadY[4] = {};
	/** The training overlay's ball list: this many a row. */
	static constexpr int32 TrainingBallColumns = 4;
	void OpenBallSwitch(bool bOpen);
	/** Closes the ボールスイッチ unless every kind is off (then refused, false). */
	bool CloseBallSwitchIfAnyOn();
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
	/** Tests: the rules screen's list of kinds opened or closed. */
	void OpenModePickerForTests(const bool bOpen) { OpenModePicker(bOpen); }
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

	/** みんなのカラー (online). */
	UPROPERTY(Transient)
	TObjectPtr<UChaosImpactGallery> Gallery;

	/** 対戦履歴 and ランキング (online). */
	UPROPERTY(Transient)
	TObjectPtr<UChaosImpactRecordBoards> RecordBoards;

	/** 訓練場's lesson list. */
	UPROPERTY(Transient)
	TObjectPtr<class UChaosImpactDojoMenu> DojoMenu;

	/** ローカルトーナメント's registration screen. */
	UPROPERTY(Transient)
	TObjectPtr<class UChaosImpactTourneyEntry> TourneyEntry;
	/** スマホ版 設定 → ボタン・画面の配置: over the whole screen, from any menu. */
	UPROPERTY(Transient)
	TObjectPtr<class UChaosImpactTouchLayoutEditor> TouchLayoutEditor;
public:
	/** Opens ボタン・画面の配置 for a nickname, on a kind of play (GetTouchModeKey). */
	void OpenTouchLayoutEditor(int32 Profile, const FString& StartMode);
	bool IsTouchLayoutEditorOpen() const;
	/** (The 配置 editor's fingers: the first one alone, taken as itself, not as a mouse.) */
	virtual FReply NativeOnTouchStarted(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent) override;
	virtual FReply NativeOnTouchMoved(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent) override;
	virtual FReply NativeOnTouchEnded(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent) override;
	int32 EditorTouch = INDEX_NONE;
	/**
	 * スマホ版: the finger the menus follow (the first one down); others are left out until it lifts. A second finger's
	 * press and moves landed on the lists as well, which then jumped between the two or took no scroll at all.
	 * (Forgotten when nothing has come from it for a few seconds: a lift that never arrived does not lock the menus.)
	 */
	int32 MenuPointer = INDEX_NONE;
	double MenuPointerAt = 0.0;
	/** True when a finger's mouse event is from another finger than MenuPointer (and is to be left out). */
	bool IsOtherFinger(const FPointerEvent& Event, bool bDown, bool bUp);
	/** A mouse press (or a finger taken for one) that began in the 配置 editor: its release is the editor's too. */
	bool bEditorMousePress = false;
	class UChaosImpactTouchLayoutEditor* GetTouchLayoutEditor() const { return TouchLayoutEditor; }
	/** Tests: the rules page's list of kinds and its ボールスイッチ opened / closed as a tap does. */
	void SetModePickerForTests(const bool bOpen) { OpenModePicker(bOpen); }
	void SetBallSwitchForTests(const bool bOpen) { OpenBallSwitch(bOpen); }
private:
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
	/** The 1v1 stages' pictures (Content/UI/StageSelect/Duel<N>.png). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTexture2D>> DuelShots;
	TArray<FSlateBrush> DuelShotBrushes;
	/** The stage select shows the 1v1 stages (a duel's or a tournament's rules). */
	bool IsDuelStageSelect() const;

	/** The 1v1 found's card: both players' characters, filmed far from the training (this player first). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<class AChaosImpactCharacterPreview>> DuelPreviews;
	TArray<FSlateBrush> DuelPreviewBrushes;
	/** Who the opponent's preview shows (made again when the offer changes). */
	FString DuelPreviewFor;
	void UpdateDuelPreviews(float DeltaSeconds);
	void CloseDuelPreviews();
};
