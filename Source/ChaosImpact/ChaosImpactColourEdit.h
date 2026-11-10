#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "ChaosImpactNameEntry.h"
#include "UObject/Object.h"
#include "ChaosImpactColourEdit.generated.h"

class AChaosImpactCharacterPreview;
class AChaosImpactPlayerController;
class FSlateWindowElementList;
struct FAnalogInputEvent;
struct FGeometry;
struct FKeyEvent;

/**
 * カラーエディット: laid out like the character select screen. The characters' icons on the left, the chosen one large
 * in the middle (its colours as they are being made) with the buttons under it, and on the right its presets, its
 * parts and the colour picker: the palette of basic colours, or (T / Y to switch) a fine picker as Photoshop's colour
 * wheel works (a hue ring round a saturation x brightness square, with H S B and R G B values and the colour's code),
 * and the colours used last. Keys, a controller and the mouse all work it.
 * Presets are this machine's own, per character (UChaosImpactColourSubsystem), and chosen on the character select.
 */
UCLASS()
class UChaosImpactColourEdit : public UObject
{
	GENERATED_BODY()
public:
	/** Palette, Ring, Square, Sliders and Recent make up the colour picker (Tab / LT RT go round it). */
	enum class EArea : uint8 { Characters, Presets, Parts, Palette, Ring, Square, Sliders, Recent, Buttons };

	void Open(AChaosImpactPlayerController* InController);
	void Close();
	bool IsOpen() const { return bOpen; }
	/** このキャラにしますか？ is up (after a save, opened from a room's pause menu). */
	bool IsApplyAsking() const { return bApplyAsk; }
	void Tick(float DeltaSeconds);
	int32 Paint(const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer) const;
	bool HandleKeyDown(const FKeyEvent& Event);
	bool HandleAnalog(const FAnalogInputEvent& Event);
	bool HandleClick(const FVector2D& DesignPoint);
	/** bDragging: the left button held (the ring, the square and the sliders follow it). */
	void HandleMouseMove(const FVector2D& DesignPoint, bool bDragging = false);
	/** Letters typed on a keyboard, for a preset's name being written. */
	bool HandleCharacter(TCHAR Character);

	// For tests and the screen's own input.
	void SelectCharacter(int32 Character);
	void Move(int32 Columns, int32 Rows);
	void Confirm();
	void Back();
	void ShiftPreset(int32 Direction);
	void ToggleFine();
	EArea GetArea() const { return Area; }
	bool IsFine() const { return bFine; }
	int32 GetCharacter() const { return Character; }
	int32 GetPreset() const { return Preset; }
	const TArray<uint8>& GetWorking() const { return Working; }
	/** Saves the working colours to the preset being edited (a new one when none, or bAsNew). */
	void SavePreset(bool bAsNew, const FString& Name = FString());
	/** Opens the name entry for the preset (a new one is saved under the name written). */
	void OpenNaming();
	bool IsNaming() const { return Naming.IsOpen(); }

private:
	void LoadPreset(int32 Slot);
	/** bOn false: the part as it is. */
	void SetPart(int32 Part, bool bOn, const FColor& Colour = FColor::White);
	/** The part's colour, or (as it is) a neutral start. */
	FColor PartColour(int32 Part, bool* bOutOn = nullptr) const;
	/** The fine picker's hue, saturation and brightness taken from the part (a grey keeps the hue it had). */
	void SyncHSB();
	/** The part painted from the fine picker's hue, saturation and brightness. */
	void ApplyHSB();
	/** Into the colour picker for the part (remembering its colours for B). */
	void EnterPicker(EArea To);
	/** A colour kept: into 最近使った色. */
	void Remember(const FColor& Colour);
	void AdjustSlider(int32 Slider, int32 Steps);
	void MoveSquare(int32 Columns, int32 Rows);
	bool PickRing(const FVector2D& Point, bool bAnywhere);
	bool PickSquare(const FVector2D& Point, bool bAnywhere);
	void PickSlider(int32 Slider, const FVector2D& Point);
	/** Tab / LT RT: the next or previous control of the picker. */
	void CyclePicker(int32 Direction);
	/** Held keys and sticks speed up: how many steps this press is worth. */
	int32 Accelerated();
	void RefreshPreview();
	void Notify(const FString& Text);
	/** The name entry finished or left: the name given to the preset. */
	void FinishNaming();
	int32 PartCount() const;
	int32 PresetChips() const;
	/** The recent colours shown, then 元の色. */
	int32 RecentCount() const;
	FColor RecentColour(int32 Index) const;
	// Layout (1600 x 900 design space).
	FBox2D TileRect(int32 Tile) const;
	FBox2D ChipRect(int32 Chip) const;
	FBox2D PartRect(int32 Part) const;
	FBox2D TabRect(bool bFineTab) const;
	FBox2D SwatchRect(int32 Swatch) const;
	FVector2D WheelCentre() const;
	FBox2D SquareRect() const;
	FBox2D SliderRect(int32 Slider) const;
	FBox2D RecentRect(int32 Index) const;
	FBox2D OriginalRect() const;
	FBox2D ButtonRect(int32 Button) const;

	TWeakObjectPtr<AChaosImpactPlayerController> Controller;
	bool bOpen = false;
	bool bFine = false;
	EArea Area = EArea::Characters;
	int32 Character = 0;
	int32 CharacterCursor = 0;
	/** The preset being edited (INDEX_NONE: a new one). */
	int32 Preset = INDEX_NONE;
	int32 ChipCursor = 0;
	int32 PartCursor = 0;
	int32 SwatchCursor = 0;
	int32 SliderCursor = 0;
	int32 RecentCursor = 0;
	int32 ButtonCursor = 0;
	/** ChaosImpactCustomColour's bytes, four per part. */
	TArray<uint8> Working;
	/** The colours when the picker was entered (B puts them back). */
	TArray<uint8> PickerBefore;
	/** The fine picker: hue 0-360, saturation and brightness 0-1 (kept apart, so a grey or black keeps its hue). */
	float EditHue = 0.0f;
	float EditSaturation = 1.0f;
	float EditBrightness = 1.0f;
	double LastAdjustAt = 0.0;
	int32 AdjustRun = 0;
	/** The mouse is dragging: 0 the ring, 1 the square, 2 + n slider n; INDEX_NONE nothing. */
	int32 Dragging = INDEX_NONE;
	bool bDirty = false;
	bool bLeaveArmed = false;
	/** Opened from a room's pause menu: saving a preset asks このキャラにしますか？ (a split screen: 1P, 2P or neither). */
	bool bOfferApply = false;
	bool bApplyAsk = false;
	int32 ApplyChoice = 0;
	double ApplyAskAt = 0.0;
	/** The question's choices: このキャラにする / しない, or one per player of this machine and しない. */
	int32 ApplyPlayers() const;
	FBox2D ApplyButtonRect(int32 Choice) const;
	void ChooseApply(int32 Choice);
	void PaintApplyAsk(const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer, double Now) const;
	/** A preset's name being written (the nickname entry's letter grid). */
	FChaosImpactNameEntry Naming;
	FString NoticeText;
	double NoticeAt = -100.0;
	double OpenedAt = 0.0;
	bool bStickHeld[2] = {false, false};
	double NextStickRepeatAt = 0.0;
	/** The model's facing (turned by dragging, Z / X, the right stick) and the stick's turn now. */
	float ViewYaw = -22.0f;
	float StickTurn = 0.0f;
	FVector2D LastMouse = FVector2D::ZeroVector;

	UPROPERTY(Transient)
	TObjectPtr<AChaosImpactCharacterPreview> Preview;
	FSlateBrush PreviewBrush;
	UPROPERTY(Transient)
	TArray<TObjectPtr<AChaosImpactCharacterPreview>> Portraits;
	TArray<FSlateBrush> PortraitBrushes;
	TArray<double> PortraitBornAt;
	int32 PortraitsMade = 0;
	int32 Frame = 0;
};
