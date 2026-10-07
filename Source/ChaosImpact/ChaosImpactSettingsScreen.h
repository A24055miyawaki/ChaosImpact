#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ChaosImpactNameEntry.h"
#include "ChaosImpactSettings.h"
#include "ChaosImpactSettingsScreen.generated.h"

class AChaosImpactPlayerController;
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
 * Up and down pick a row, left and right change it, LB/RB (Q/E) change tab; on a control, A (Enter or a click) waits
 * for the new key, Y (Delete) takes it off. Everything is saved as it changes. Drawn by the menu widget in its
 * 1600 x 900 design space.
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
		Heading
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
	void HandleWheel(float Delta);

	// What the controls do; the handlers above call these, and so do the tests.
	ETab GetTab() const { return Tab; }
	void SetTab(ETab NewTab);
	const TArray<FRow>& GetRows() const { return Rows; }
	int32 GetSelectedRow() const { return SelectedRow; }
	int32 GetSelectedCell() const { return SelectedCell; }
	/** The first row with this id (INDEX_NONE when this tab has none). */
	int32 FindRow(int32 Id) const;
	void SelectRow(int32 Row, int32 Cell = 0);
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
		RowVolume = 300,
		RowRumble,
		RowShake,
		RowSwitchConfirm,
		RowMiniGames,
		RowBinding = 1000
	};

private:
	void BuildRows();
	void BuildDisplayRows();
	void BuildPictureRows();
	void BuildControlRows();
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
	/** The key and controller names the hint line uses (from the last device pressed). */
	FString HintText() const;

	TWeakObjectPtr<AChaosImpactPlayerController> Controller;
	TArray<FRow> Rows;
	ETab Tab = ETab::Display;
	int32 SelectedRow = 0;
	int32 SelectedCell = 0;
	int32 FirstVisibleRow = 0;
	int32 EditedProfile = 0;
	EChaosImpactDevice EditedDevice = EChaosImpactDevice::KeyboardMouse;
	bool bWaitingForKey = false;
	double WaitStartedAt = 0.0;
	/** The nickname being renamed (INDEX_NONE: a new one is being made). */
	int32 RenamingProfile = INDEX_NONE;
	/** Taking a nickname off needs a second press. */
	bool bDeleteArmed = false;
	FChaosImpactNameEntry NameEntry;
	FString Message;
	double MessageAt = -100.0;
	double OpenedAt = 0.0;
	double TabChangedAt = 0.0;
	double NextStickAt[2] = {0.0, 0.0};
	bool bStickHeld[2] = {false, false};
	/** The last press came from a controller (the hint line shows its buttons) and which layout it was. */
	bool bLastInputPad = false;
	EChaosImpactDevice LastPadDevice = EChaosImpactDevice::Xbox;
	bool bOpen = false;
};
