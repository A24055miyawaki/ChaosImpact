#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ChaosImpactNameEntry.h"
#include "ChaosImpactSettings.h"
#include "Styling/SlateBrush.h"
#include "ChaosImpactSettingsScreen.generated.h"

class AChaosImpactPlayerController;
class UMediaPlayer;
class UMediaTexture;
class FSlateWindowElementList;
struct FAnalogInputEvent;
struct FGeometry;
struct FKeyEvent;

/**
 * The settings screen, opened from the mode select menu and from pause: four tabs of rows.
 *   画面: window mode (kept the same as F11), resolution, V-sync, frame rate cap, brightness.
 *   画質: an overall quality and every picture group on its own, and the rendering resolution.
 *   操作: a nickname's controls, for each device (keyboard and mouse, Xbox, PlayStation and Switch controllers):
 *         two keys or buttons for every control, which controller layout it plays with, sticks swapped, aim assist;
 *         nicknames are made, renamed and taken off here too (and picked at character select).
 *   その他: volume, rumble, camera shake, and which button decides on a Switch controller.
 * Two places for the cursor (2026-11-16): the column on the left (the tabs, then 保存 and もどる), moved up and down,
 * where A (or right) goes into the tab's settings; and the settings, where up and down pick a row, left and right change
 * it and B comes back out to the tabs. (Q/E still change tab on a keyboard.) On a control, A (Enter or a click) waits
 * for the new key, Y (Delete) takes it off. A change shows at once, and is kept when 保存 is pressed (X / S, or the
 * button above もどる); leaving with changes not saved asks whether to save them, and not saving puts everything back.
 * Drawn by the menu widget in its 1600 x 900 design space.
 */
UCLASS()
class UChaosImpactSettingsScreen : public UObject
{
	GENERATED_BODY()

public:
	enum class ETab : uint8
	{
		Display,
		Picture,
		Controls,
		Other,
		Count
	};

	enum class ERowKind : uint8
	{
		Value,
		Button,
		Binding,
		Heading,
		Info
	};

	struct FRow
	{
		ERowKind Kind = ERowKind::Value;
		/** Which setting (see the cpp); for a control row, 1000 + its action. */
		int32 Id = 0;
		FString Label;
		FString Value;
		/** A control row: the names of its two keys. */
		FString Cells[2];
		bool bDisabled = false;
	};

	void Open(AChaosImpactPlayerController* InController);
	void Close();
	bool IsOpen() const { return bOpen; }
	void Tick(float DeltaSeconds);
	int32 Paint(const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer) const;

	/** Keys and sticks from every device; true when consumed. */
	bool HandleKeyDown(const FKeyEvent& Event);
	bool HandleAnalog(const FAnalogInputEvent& Event);
	bool HandleCharacter(TCHAR Character);
	/** A mouse button in design space (any button: while waiting for a key, it is the key). */
	bool HandleMouseDown(const FVector2D& DesignPoint, const FKey& Button);
	void HandleMouseMove(const FVector2D& DesignPoint);
	/** The button let go (スマホ版: a press is decided here, so that a drag can scroll instead). */
	void HandleMouseUp(const FVector2D& DesignPoint, const FKey& Button);
	void HandleWheel(float Delta);

	// What the controls do; the handlers above call these, and so do the tests.
	ETab GetTab() const { return Tab; }
	void SetTab(ETab NewTab);
	const TArray<FRow>& GetRows() const { return Rows; }
	int32 GetSelectedRow() const { return SelectedRow; }
	int32 GetSelectedCell() const { return SelectedCell; }
	/** The first row with this id (INDEX_NONE when this tab has none). */
	int32 FindRow(int32 Id) const;
	/** Where a row is drawn now (design space; it moves as the list scrolls). */
	FBox2D GetRowRect(int32 Row) const { return RowRect(Row); }
	/** Picks a row (and puts the cursor in the settings, out of the tab column). */
	void SelectRow(int32 Row, int32 Cell = 0);
	/**
	 * 操作 in three steps (2026-11-16, as Smash's button settings): the nicknames (＋ to make one), then the chosen
	 * nickname's devices as cards, then that device's settings. B goes back a step (from the first, to the tabs).
	 */
	enum class EControlsStep : uint8 { Nicknames, Devices, Buttons };
	EControlsStep GetControlsStep() const { return ControlsStep; }
	/** B in a tab's settings: a step back in 操作, else out to the tabs. */
	void StepBack();
	/** This nickname's buttons on this device differ from the first settings. */
	static bool IsDeviceChanged(int32 Profile, EChaosImpactDevice Device);
	/** The cursor is in the column on the left (a tab, 保存 or もどる), not in a tab's settings. */
	bool IsChoosingTab() const { return bSideFocus; }
	/** Where it is in that column: 0.. the tabs, then SideSave, then SideBack. */
	int32 GetSideCursor() const { return SideCursor; }
	static constexpr int32 SideSave = static_cast<int32>(ETab::Count);
	static constexpr int32 SideBack = SideSave + 1;
	/** Up / down in the column (a tab is opened as the cursor reaches it). */
	void MoveSide(int32 Direction);
	/** A in the column: into the tab's settings, or 保存, or もどる. */
	void ActivateSide();
	/** Into the tab's settings / back out to the column. */
	void EnterRows();
	void BackToTabs();
	void MoveSelection(int32 Direction);
	/** Left or right on the selected row: its value, or which of a control's two keys. */
	void Adjust(int32 Direction);
	/** A (Enter): a button's action, a value's next choice, or waiting for a control's key. */
	void Activate();
	/** Y (Delete): the selected control key is taken off. */
	void ClearSelectedKey();
	bool IsWaitingForKey() const { return bWaitingForKey; }
	/** While waiting: the key that was pressed (a test can give one too). */
	void ReceiveKey(const FKey& Key);
	bool IsNameEntryOpen() const { return NameEntry.IsOpen(); }
	FChaosImpactNameEntry& GetNameEntry() { return NameEntry; }
	int32 GetEditedProfile() const { return EditedProfile; }
	EChaosImpactDevice GetEditedDevice() const { return EditedDevice; }
	/** Picks which nickname and device the controls tab shows. */
	void EditControls(int32 Profile, EChaosImpactDevice Device);
	void OpenControlStylePicker();
	bool IsControlStylePickerOpen() const { return bControlStylePickerOpen; }
	EChaosImpactControlScheme GetPreviewControlScheme() const { return PreviewControlScheme; }
	bool IsControlVideoPlaying() const;
	double GetControlVideoTime() const;
	virtual void BeginDestroy() override;

	// 保存: changes are kept only when saved.
	/** Something was changed since the screen opened (or was last saved). */
	bool HasUnsavedChanges() const;
	/** Keeps every change made (X / S, or the 保存 button). */
	void Save();
	/** もどる with changes not saved: the question whether to save them is up. */
	bool IsAskingToSave() const { return bAskSave; }
	/** The question's answers. */
	enum class ESaveAnswer : uint8 { SaveAndLeave, LeaveWithoutSaving, Stay, Count };
	ESaveAnswer GetSaveAnswerChoice() const { return AskChoice; }
	/** Answers the question (as a press on that choice). */
	void AnswerSave(ESaveAnswer Answer);

	// Row ids, for the tests and the cpp.
	enum ERowId : int32
	{
		RowWindowMode = 1,
		RowResolution,
		RowVSync,
		RowFrameRate,
		RowBrightness,
		RowQuality,
		RowResolutionScale,
		RowScreenOrientation,
		RowShowFps,
		RowQualityGroup = 100,
		RowProfile = 200,
		RowNewProfile,
		RowRenameProfile,
		RowDeleteProfile,
		RowDevice,
		RowPadChoice,
		RowSwapSticks,
		RowAimAssist,
		RowResetDevice,
		RowControlScheme,
		RowMouseDash,
		RowControlPreview,
		RowShoulderSensitivityX,
		RowShoulderSensitivityY,
		RowResetShoulderSensitivity,
		RowCameraDistance,
		RowThrowToMove,
		RowWheelSwap,
		RowTouchLeftHanded,
		RowTouchFlick,
		RowTouchAutoAim,
		RowTouchStyle,
		RowTouchLayout,
		RowVolume = 300,
		RowRumble,
		RowShake,
		RowSwitchConfirm,
		RowButtonHints,
		RowOthersPaint,
		RowHudSize,
		RowTutorial,
		RowMiniGames,
		RowMusicVolume,
		RowMenuRumble,
		/** 操作's first step: a nickname (RowProfileItem + its index); its second: a device (RowDeviceItem + the device). */
		RowProfileItem = 400,
		RowDeviceItem = 480,
		RowBinding = 1000
	};

private:
	void BuildRows();
	void BuildDisplayRows();
	void BuildPictureRows();
	void BuildControlRows();
	/** スマホ版: played by touch, no controller in use (the controls are the touch settings). */
	bool IsTouchScreenOnly() const;
	bool IsTouchSettings() const;
	void BuildOtherRows();
	void ChangeValue(const FRow& Row, int32 Direction);
	void Leave();
	void FinishNameEntry();
	void KeepSelectionVisible();
	bool IsSelectable(int32 Row) const;
	void ShowMessage(const FString& Text);
	FBox2D RowRect(int32 Row) const;
	FBox2D CellRect(int32 Row, int32 Cell) const;
	FBox2D TabRect(int32 Index) const;
	/** A press where it lands (the mouse at once; a finger when it lifts). */
	bool Click(const FVector2D& DesignPoint, const FKey& Button);
	/** How many rows show at once (the device cards: all). */
	int32 ShownRows() const;
	/** スマホ版: 操作's back-a-step button in the panel's head. */
	bool HasStepBackButton() const;
	FBox2D StepBackRect() const;
	/** スマホ版: a finger down (where, and the list's scroll then); moved far enough, it drags the list. */
	bool bPressing = false;
	bool bDragging = false;
	FVector2D PressPoint = FVector2D::ZeroVector;
	int32 PressFirstRow = 0;
	/** What the hints name the keys of: the device being used now (not the one being set). */
	EChaosImpactDevice HintDevice() const;
	/** The key and controller names the hint line uses (from the last device pressed). */
	FString HintText() const;
	/** What a row does, in a line (under the rows). */
	FString GetRowHelp(int32 Id) const;
	void MoveControlPreview(int32 Direction);
	void ApplyControlPreview();
	void UpdateControlVideo();
	int32 PaintControlStylePicker(const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer) const;

	TWeakObjectPtr<AChaosImpactPlayerController> Controller;
	TArray<FRow> Rows;
	ETab Tab = ETab::Display;
	int32 SelectedRow = 0;
	int32 SelectedCell = 0;
	int32 FirstVisibleRow = 0;
	/**
	 * スマホ版: the list scrolled by pixels (a finger drags it smoothly and it glides on): how far past FirstVisibleRow's
	 * top (0 up to a row's step), and the glide's speed (design units a second).
	 */
	float ScrollFrac = 0.0f;
	float ScrollVelocity = 0.0f;
	float PressScroll = 0.0f;
	float LastDragY = 0.0f;
	/** Where the finger was as it began to scroll (the list follows from there, not jumping the slop it moved first). */
	float DragOriginY = 0.0f;
	double LastDragAt = 0.0;
	float GetScrollPixels() const;
	float MaxScrollPixels() const;
	void SetScrollPixels(float Pixels);
	/** The rows area (drawn clipped to it while scrolled part way). */
	FBox2D RowsArea() const;
	int32 EditedProfile = 0;
	EChaosImpactDevice EditedDevice = EChaosImpactDevice::KeyboardMouse;
	bool bWaitingForKey = false;
	bool bSideFocus = true;
	int32 SideCursor = 0;
	double WaitStartedAt = 0.0;
	/** The nickname being renamed (INDEX_NONE: a new one is being made). */
	int32 RenamingProfile = INDEX_NONE;
	EControlsStep ControlsStep = EControlsStep::Nicknames;
	void SetControlsStep(EControlsStep Step);
	bool IsDeviceCardRow(int32 Row) const;
	void PaintDeviceCard(const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer, int32 Row, float Alpha, double Now) const;
	/** Taking a nickname off needs a second press. */
	bool bDeleteArmed = false;
	FChaosImpactNameEntry NameEntry;
	FString Message;
	double MessageAt = -100.0;
	double OpenedAt = 0.0;
	double TabChangedAt = 0.0;
	double NextStickAt[2] = {0.0, 0.0};
	bool bStickHeld[2] = {false, false};
	bool bOpen = false;
	bool bControlStylePickerOpen = false;
	EChaosImpactControlScheme PreviewControlScheme = EChaosImpactControlScheme::Normal;
	double PreviewStartedAt = 0;
	UPROPERTY(Transient) TObjectPtr<UMediaPlayer> ControlVideoPlayer;
	UPROPERTY(Transient) TObjectPtr<UMediaTexture> ControlVideoTexture;
	FSlateBrush ControlVideoBrush;
	int32 OpenVideoId = INDEX_NONE;
	bool bVideoOpenFailed = false;
	/** The question whether to save, over the screen. */
	bool bAskSave = false;
	ESaveAnswer AskChoice = ESaveAnswer::SaveAndLeave;
	double AskOpenedAt = 0.0;
	/** Has changes not saved (refreshed each tick: comparing every setting is not done while drawing). */
	bool bUnsaved = false;
	/** The question's buttons, in design space. */
	FBox2D AskButtonRect(int32 Index) const;
	int32 PaintAskSave(const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer) const;
	void LeaveNow();
};
