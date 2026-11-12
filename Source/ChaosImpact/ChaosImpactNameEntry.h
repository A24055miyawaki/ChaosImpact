#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Layout/Geometry.h"

class FSlateWindowElementList;
class FChaosImpactNameEntryIme;
class ITextInputMethodChangeNotifier;
struct FGeometry;

/**
 * A nickname typed on a pad as easily as on a keyboard: a panel over the screen with the name being written and a
 * keyboard laid out as a smartphone's: ひらがな / カタカナ (五十音), 英字 (QWERTY with ⇧, the numbers on top) and 記号.
 * The D-pad or stick moves over the keys, A writes one, B rubs the last one out (or leaves when nothing is written),
 * START finishes. A real keyboard types straight in, through the system's IME too (半角 / 全角, かな and 変換), with
 * Enter to finish and Escape to leave; the mouse clicks keys. Drawn in the menus' 1600 x 900 design space.
 */
class CHAOSIMPACT_API FChaosImpactNameEntry
{
public:
	enum class EResult : uint8
	{
		None,
		Done,
		Cancelled
	};
	/** What a key of the on-screen keyboard does. */
	enum class EAction : uint8
	{
		None,
		Letter,
		/** ゛ ゜ 小 (Arg 0, 1, 2): the last letter voiced, half-voiced or small. */
		Mark,
		Shift,
		/** Arg: the page. */
		Page,
		Space,
		Back,
		Done
	};
	struct FCell
	{
		FString Label;
		EAction Action = EAction::None;
		int32 Arg = 0;
		/** Its width in keys. */
		float Units = 1.0f;
	};
	struct FRow
	{
		TArray<FCell> Cells;
		/** Keys of space before the first (the staggered rows of QWERTY). */
		float Lead = 0.0f;
	};

	FChaosImpactNameEntry() = default;
	~FChaosImpactNameEntry();
	FChaosImpactNameEntry(const FChaosImpactNameEntry&) = delete;
	FChaosImpactNameEntry& operator=(const FChaosImpactNameEntry&) = delete;

	void Open(const FString& Initial, const FString& InTitle, int32 InOwner = 0);
	void Close();
	bool IsOpen() const { return bOpen; }
	/** The player (character select) this is open for. */
	int32 GetOwner() const { return Owner; }
	const FString& GetText() const { return Text; }

	/** A key (menu-translated: A decides, B goes back). True when it was used. */
	bool HandleKey(const FKey& Key, bool bRepeat);
	/** A typed character from a keyboard. */
	bool HandleCharacter(TCHAR Character);
	bool HandleAnalog(const FKey& Key, float Value);
	bool HandleClick(const FVector2D& DesignPoint);
	void HandleMouseMove(const FVector2D& DesignPoint);
	/** What happened since the last call: Done (with the name) or Cancelled, once. */
	EResult ConsumeResult(FString& OutName);

	/** Works the key under the cursor (writes its letter, changes the page, rubs out, finishes). Tests call it too. */
	void PressCell();
	void MoveCursor(int32 Columns, int32 Rows);
	void SetPage(int32 InPage);
	int32 GetPage() const { return Page; }
	/** The key under the cursor (for tests). */
	const FCell* GetCursorCell() const;
	/** Puts the cursor on the first key showing Label on this page (for tests); false when there is none. */
	bool PointAt(const FString& Label);
	/** ⇧ on the 英字 page: 0 off, 1 the next letter, 2 locked. */
	int32 GetShift() const { return Shift; }
	/** The system's IME is writing (変換 not yet decided). */
	bool IsComposing() const;
	/** Tests: the system's IME as the OS drives it (letters being converted, then decided). False without an IME. */
	bool ImeComposeForTests(const FString& Composing);
	bool ImeDecideForTests(const FString& Decided);

	int32 Paint(const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer) const;

	static constexpr int32 PageCount = 4;
	static constexpr int32 KanaPage = 0;
	static constexpr int32 KatakanaPage = 1;
	static constexpr int32 LatinPage = 2;
	static constexpr int32 SymbolPage = 3;

private:
	friend class FChaosImpactNameEntryIme;

	/** The page's rows, the bottom row (pages, space, けす, おわり) last. */
	const TArray<FRow>& Rows() const;
	/** A key as shown now (the 英字 page in capitals while ⇧ is on; カタカナ from ひらがな). */
	FString Shown(const FCell& Cell) const;
	FBox2D CellRect(int32 Row, int32 Column) const;
	bool IsSelectable(int32 Row, int32 Column) const;
	/** The cursor back on a key that can be pressed (after a page change). */
	void SettleCursor();
	void Append(const FString& Letter);
	/** ゛ ゜ 小: turns the last letter into its voiced, half-voiced or small form (and back). */
	void ChangeLast(int32 Mark);
	void Backspace();
	void Finish();
	void Cancel();
	void Say(const FString& Words);

	// The system's IME (a real keyboard's かな / 変換 / 全角): the name is its text, written at the end.
	void StartIme();
	void StopIme();
	/** The IME replacing part of the name (bComposing: still being converted, so the length is not held to yet). */
	void ImeReplace(int32 Begin, int32 Length, const FString& With, bool bComposing);
	void ImeCompositionEnded();
	/** Where letters of the name are on the screen (desktop pixels), for the IME's candidate list. */
	void ImeBounds(int32 Begin, int32 Length, FVector2D& OutPosition, FVector2D& OutSize) const;
	/** The text changed here (not by the IME): tell it. */
	void ImeTextChanged(int32 OldLength);

	FString Text;
	FString Title;
	int32 Owner = 0;
	int32 Page = 0;
	int32 CursorRow = 0;
	int32 CursorColumn = 0;
	int32 Shift = 0;
	/** The arrow keys moved the cursor since the last letter typed: Enter presses the key under it. */
	bool bArrowNavigation = false;
	double OpenedAt = 0.0;
	double NextStickAt[2] = {0.0, 0.0};
	bool bStickHeld[2] = {false, false};
	bool bOpen = false;
	EResult Result = EResult::None;
	FString Message;
	double MessageAt = -100.0;

	TSharedPtr<FChaosImpactNameEntryIme> Ime;
	TSharedPtr<ITextInputMethodChangeNotifier> ImeNotifier;
	double ImeEndedAt = -100.0;
	/** Where it was last drawn (the IME's candidate list goes under the name). */
	mutable FGeometry PaintedDesign;
	mutable float PaintedLift = 0.0f;
	mutable bool bPainted = false;
};
