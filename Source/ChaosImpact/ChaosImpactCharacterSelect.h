#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Styling/SlateBrush.h"
#include "ChaosImpactCharacterSelect.generated.h"

class AChaosImpactCharacterPreview;
class AChaosImpactPlayerController;
class FSlateWindowElementList;
struct FAnalogInputEvent;
struct FGeometry;
struct FKeyEvent;

/**
 * The character select screen, between controller assignment and the game, laid out like a kart racer's:
 * the roster as a grid of character icons on the left, and on the right one window per local player (up to
 * four) showing the character under their cursor at full size. Each player works it with their own device at
 * the same time: move over the icons, 決定 a character, pick one of its colours from the row that opens in
 * their window, 決定 again. When everyone is done, any of them starts. Drawn by the menu widget in its
 * 1600 x 900 design space.
 */
UCLASS()
class UChaosImpactCharacterSelect : public UObject
{
	GENERATED_BODY()

public:
	static constexpr int32 GridColumns = 4;
	static constexpr int32 GridRows = 3;

	/** Where a player is: moving over the icons, choosing a colour for their character, or done. */
	enum class EStep : uint8
	{
		Character,
		Colour,
		Done
	};

	void Open(AChaosImpactPlayerController* InController);
	void Close();
	bool IsOpen() const { return bOpen; }
	void Tick(float DeltaSeconds);
	int32 Paint(const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer) const;

	/** Keys and sticks from every device; true when consumed. */
	bool HandleKeyDown(const FKeyEvent& Event);
	bool HandleAnalog(const FAnalogInputEvent& Event);
	/** A click in design space, for the keyboard and mouse player: an icon, or the start bar. */
	bool HandleClick(const FVector2D& DesignPoint);

	// What each player does; the device handlers above call these, and so do the tests.
	/** On the icons this moves the cursor; on the colour row, left and right change the colour. */
	void MoveCursor(int32 Player, int32 Columns, int32 Rows);
	void ChangeColour(int32 Player, int32 Direction);
	void PressConfirm(int32 Player);
	void PressBack(int32 Player);

	int32 GetPlayerCount() const { return Slots.Num(); }
	EStep GetStep(int32 Player) const { return Slots.IsValidIndex(Player) ? Slots[Player].Step : EStep::Character; }
	int32 GetCursorTile(int32 Player) const { return Slots.IsValidIndex(Player) ? Slots[Player].Cursor : INDEX_NONE; }
	int32 GetCharacter(int32 Player) const { return Slots.IsValidIndex(Player) ? Slots[Player].Character : INDEX_NONE; }
	int32 GetColour(int32 Player) const { return Slots.IsValidIndex(Player) ? Slots[Player].Colour : INDEX_NONE; }
	/** True once the player has picked both a character and its colour. */
	bool IsReady(int32 Player) const { return Slots.IsValidIndex(Player) && Slots[Player].Step == EStep::Done; }
	bool AreAllReady() const;
	bool IsStarting() const { return bStarting; }
	static int32 GetTileCount() { return GridColumns * GridRows; }
	static bool IsTileUnlocked(int32 Tile);

private:
	struct FSlot
	{
		EStep Step = EStep::Character;
		/** Icon under this player's cursor. */
		int32 Cursor = 0;
		/** The character shown in this player's window: the pick, or the last character the cursor was on. */
		int32 Character = 0;
		int32 Colour = 0;
		bool bKeyboard = false;
		double MovedAt = -100.0;
		double ChangedAt = -100.0;
		double StepAt = -100.0;
		int32 LastDirection = 1;
		double NextStickAt[2] = {0.0, 0.0};
		bool bStickHeld[2] = {false, false};
	};

	bool IsColourTaken(int32 Player, int32 Character, int32 Colour) const;
	/** The nearest colour from Start in Direction that no other player has on the same character. */
	int32 FindFreeColour(int32 Player, int32 Character, int32 Start, int32 Direction) const;
	void Commit(int32 Player);
	void RefreshPreview(int32 Player);
	void SetStep(int32 Player, EStep Step);
	FVector2D TileOrigin(int32 Tile) const;
	/** This player's window on the right, in design space. */
	FBox2D WindowRect(int32 Player) const;
	void PaintWindow(int32 Player, const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer, double Now) const;

	TWeakObjectPtr<AChaosImpactPlayerController> Controller;
	TArray<FSlot> Slots;

	/** One per player: the picture in their window. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<AChaosImpactCharacterPreview>> Previews;

	/** One per roster character: the face on its icon. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<AChaosImpactCharacterPreview>> Portraits;

	/** Brushes for those pictures; painting adjusts their visible part each frame. */
	mutable TArray<FSlateBrush> PictureBrushes;
	mutable TArray<FSlateBrush> PortraitBrushes;

	double OpenedAt = 0.0;
	double AllReadyAt = -100.0;
	double StartAt = 0.0;
	bool bOpen = false;
	bool bStarting = false;
};
