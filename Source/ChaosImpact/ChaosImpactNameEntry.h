#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

class FSlateWindowElementList;
struct FGeometry;

/**
 * A nickname typed on a pad as easily as on a keyboard: a panel over the screen with the name being written and a
 * grid of letters (ひらがな, カタカナ, ABC, abc). The D-pad or stick moves over the letters, A writes one, B rubs the
 * last one out (or leaves when nothing is written) and START finishes. A keyboard just types (letters and numbers)
 * with Enter to finish and Escape to leave; the mouse clicks letters. Drawn in the menus' 1600 x 900 design space.
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

	void Open(const FString& Initial, const FString& InTitle, int32 InOwner = 0);
	void Close() { bOpen = false; }
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

	/** Writes the grid's letter under the cursor, or works its key (page, rub out, finish). Tests call it too. */
	void PressCell();
	void MoveCursor(int32 Columns, int32 Rows);
	void SetPage(int32 InPage);
	int32 GetPage() const { return Page; }

	int32 Paint(const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer) const;

	static constexpr int32 Columns = 11;
	static constexpr int32 LetterRows = 5;
	static constexpr int32 PageCount = 4;

private:
	/** The letter (or key) at a cell; the last row is the keys. */
	FString CellText(int32 Row, int32 Column) const;
	int32 KeyRowCellCount() const { return 6; }
	FBox2D CellRect(int32 Row, int32 Column) const;
	void Append(const FString& Letter);
	/** ゛ ゜ 小: turns the last letter into its voiced, half-voiced or small form (and back). */
	void ChangeLast(int32 Mark);
	void Backspace();
	void Finish();

	FString Text;
	FString Title;
	int32 Owner = 0;
	int32 Page = 0;
	int32 CursorRow = 0;
	int32 CursorColumn = 0;
	double OpenedAt = 0.0;
	double NextStickAt[2] = {0.0, 0.0};
	bool bStickHeld[2] = {false, false};
	bool bOpen = false;
	EResult Result = EResult::None;
	FString Message;
	double MessageAt = -100.0;
};
