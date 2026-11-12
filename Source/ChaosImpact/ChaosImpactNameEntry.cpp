#include "ChaosImpactNameEntry.h"
#include "ChaosImpactSfx.h"

#include "ChaosImpactPaint.h"
#include "ChaosImpactSettings.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericWindow.h"
#include "GenericPlatform/ITextInputMethodSystem.h"
#include "Widgets/SWindow.h"

using EEntryAction = FChaosImpactNameEntry::EAction;

namespace
{
	// The panel's keys: the letter rows from the top, the bottom row (pages, space, けす, おわり) under them.
	constexpr float KeyGridLeft = 276.0f;
	constexpr float KeyGridWidth = 1056.0f;
	constexpr float KeyGridTop = 300.0f;
	constexpr float KeyCellHeight = 70.0f;
	constexpr float KeyCellGap = 8.0f;
	constexpr float BottomRowGap = 6.0f;
	// The name as written.
	constexpr float NameLeft = 360.0f;
	constexpr float NameTop = 198.0f;
	constexpr float NameSize = 44.0f;
	/** Keys pressed this soon after the IME decided its letters were part of that (Enter, Space, Escape). */
	constexpr double ImeEcho = 0.25;

	// 五十音, as a smartphone's かな keyboard: a column per consonant, "　" an empty place; ゛ ゜ 小 change the last letter.
	const TCHAR* const KanaRows[] = {
		TEXT("あかさたなはまやらわ゛"),
		TEXT("いきしちにひみ　りを゜"),
		TEXT("うくすつぬふむゆるん小"),
		TEXT("えけせてねへめ　れー！"),
		TEXT("おこそとのほもよろ～？")};

	bool IsHiragana(const TCHAR Letter)
	{
		return Letter >= 0x3041 && Letter <= 0x3096;
	}

	bool IsKatakana(const TCHAR Letter)
	{
		return Letter >= 0x30A1 && Letter <= 0x30F6;
	}

	FChaosImpactNameEntry::FCell Key(const FString& Label, const EEntryAction Action = EEntryAction::Letter, const int32 Arg = 0, const float Units = 1.0f)
	{
		FChaosImpactNameEntry::FCell Cell;
		Cell.Label = Label;
		Cell.Action = Action;
		Cell.Arg = Arg;
		Cell.Units = Units;
		return Cell;
	}

	FChaosImpactNameEntry::FRow Letters(const FString& Run, const float Lead = 0.0f)
	{
		FChaosImpactNameEntry::FRow Row;
		Row.Lead = Lead;
		for (const TCHAR Letter : Run)
		{
			Row.Cells.Add(Key(FString(1, &Letter)));
		}
		return Row;
	}

	/** The same along the bottom of every page. */
	FChaosImpactNameEntry::FRow BottomRow()
	{
		FChaosImpactNameEntry::FRow Row;
		Row.Cells = {Key(TEXT("あいう"), EEntryAction::Page, FChaosImpactNameEntry::KanaPage),
			Key(TEXT("アイウ"), EEntryAction::Page, FChaosImpactNameEntry::KatakanaPage),
			Key(TEXT("ABC"), EEntryAction::Page, FChaosImpactNameEntry::LatinPage),
			Key(TEXT("☆123"), EEntryAction::Page, FChaosImpactNameEntry::SymbolPage),
			Key(TEXT("スペース"), EEntryAction::Space, 0, 2.0f),
			Key(TEXT("けす"), EEntryAction::Back),
			Key(TEXT("おわり"), EEntryAction::Done, 0, 1.5f)};
		return Row;
	}

	TArray<FChaosImpactNameEntry::FRow> MakeKana()
	{
		TArray<FChaosImpactNameEntry::FRow> Rows;
		for (const TCHAR* const Run : KanaRows)
		{
			FChaosImpactNameEntry::FRow Row;
			for (const TCHAR* Letter = Run; *Letter; ++Letter)
			{
				const FString One(1, Letter);
				Row.Cells.Add(*Letter == TEXT('　') ? Key(FString(), EEntryAction::None)
					: One == TEXT("゛") ? Key(One, EEntryAction::Mark, 0)
					: One == TEXT("゜") ? Key(One, EEntryAction::Mark, 1)
					: One == TEXT("小") ? Key(One, EEntryAction::Mark, 2)
					: Key(One));
			}
			Rows.Add(Row);
		}
		Rows.Add(BottomRow());
		return Rows;
	}

	/** QWERTY as on a phone: the numbers on top, each row a little in from the one above, ⇧ and ⌫ beside z to m. */
	TArray<FChaosImpactNameEntry::FRow> MakeLatin()
	{
		TArray<FChaosImpactNameEntry::FRow> Rows;
		Rows.Add(Letters(TEXT("1234567890")));
		Rows.Add(Letters(TEXT("qwertyuiop")));
		Rows.Add(Letters(TEXT("asdfghjkl"), 0.5f));
		FChaosImpactNameEntry::FRow Fourth = Letters(TEXT("zxcvbnm"));
		Fourth.Cells.Insert(Key(TEXT("⇧"), EEntryAction::Shift, 0, 1.5f), 0);
		Fourth.Cells.Add(Key(TEXT("⌫"), EEntryAction::Back, 0, 1.5f));
		Rows.Add(Fourth);
		Rows.Add(Letters(TEXT("-_.,!?&@★♪")));
		Rows.Add(BottomRow());
		return Rows;
	}

	TArray<FChaosImpactNameEntry::FRow> MakeSymbols()
	{
		TArray<FChaosImpactNameEntry::FRow> Rows;
		Rows.Add(Letters(TEXT("1234567890")));
		Rows.Add(Letters(TEXT("-/:;()&@#%")));
		Rows.Add(Letters(TEXT(".,?!'\"*+=^")));
		Rows.Add(Letters(TEXT("☆★♪♡・～※◎♂♀")));
		Rows.Add(Letters(TEXT("●○■□▲△◆◇→←")));
		Rows.Add(BottomRow());
		return Rows;
	}

	float RowUnits(const FChaosImpactNameEntry::FRow& Row)
	{
		float Units = Row.Lead;
		for (const FChaosImpactNameEntry::FCell& Cell : Row.Cells)
		{
			Units += Cell.Units;
		}
		return Units;
	}
}

/** The name as the system's IME sees it: its whole text, the letters it writes added at the end. */
class FChaosImpactNameEntryIme : public ITextInputMethodContext
{
public:
	explicit FChaosImpactNameEntryIme(FChaosImpactNameEntry* InEntry) : Entry(InEntry) {}

	FChaosImpactNameEntry* Entry = nullptr;
	bool bComposing = false;
	int32 CompositionBegin = 0;
	uint32 CompositionLength = 0;
	uint32 SelectionBegin = 0;
	uint32 SelectionLength = 0;
	ECaretPosition Caret = ECaretPosition::Ending;

	virtual bool IsComposing() override { return bComposing; }
	virtual bool IsReadOnly() override { return Entry == nullptr; }
	virtual uint32 GetTextLength() override { return Entry ? static_cast<uint32>(Entry->Text.Len()) : 0; }

	virtual void GetSelectionRange(uint32& OutBeginIndex, uint32& OutLength, ECaretPosition& OutCaretPosition) override
	{
		const uint32 Length = GetTextLength();
		OutBeginIndex = FMath::Min(SelectionBegin, Length);
		OutLength = FMath::Min(SelectionLength, Length - OutBeginIndex);
		OutCaretPosition = Caret;
	}

	virtual void SetSelectionRange(const uint32 InBeginIndex, const uint32 InLength, const ECaretPosition InCaretPosition) override
	{
		SelectionBegin = InBeginIndex;
		SelectionLength = InLength;
		Caret = InCaretPosition;
	}

	virtual void GetTextInRange(const uint32 InBeginIndex, const uint32 InLength, FString& OutString) override
	{
		OutString = Entry ? Entry->Text.Mid(static_cast<int32>(InBeginIndex), static_cast<int32>(InLength)) : FString();
	}

	virtual void SetTextInRange(const uint32 InBeginIndex, const uint32 InLength, const FString& InString) override
	{
		if (Entry)
		{
			Entry->ImeReplace(static_cast<int32>(InBeginIndex), static_cast<int32>(InLength), InString, bComposing);
		}
		SelectionBegin = FMath::Min(InBeginIndex + static_cast<uint32>(InString.Len()), GetTextLength());
		SelectionLength = 0;
	}

	virtual int32 GetCharacterIndexFromPoint(const FVector2D& InPoint) override { return static_cast<int32>(GetTextLength()); }

	virtual bool GetTextBounds(const uint32 InBeginIndex, const uint32 InLength, FVector2D& OutPosition, FVector2D& OutSize) override
	{
		if (Entry)
		{
			Entry->ImeBounds(static_cast<int32>(InBeginIndex), static_cast<int32>(InLength), OutPosition, OutSize);
		}
		return false;
	}

	virtual void GetScreenBounds(FVector2D& OutPosition, FVector2D& OutSize) override
	{
		if (Entry)
		{
			Entry->ImeBounds(0, static_cast<int32>(GetTextLength()), OutPosition, OutSize);
		}
	}

	virtual TSharedPtr<FGenericWindow> GetWindow() override
	{
		TSharedPtr<SWindow> Window = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWindow() : nullptr;
		if (!Window.IsValid() && FSlateApplication::IsInitialized())
		{
			Window = FSlateApplication::Get().GetActiveTopLevelWindow();
		}
		return Window.IsValid() ? Window->GetNativeWindow() : nullptr;
	}

	virtual void BeginComposition() override
	{
		bComposing = true;
		CompositionBegin = static_cast<int32>(GetTextLength());
		CompositionLength = 0;
	}

	virtual void UpdateCompositionRange(const int32 InBeginIndex, const uint32 InLength) override
	{
		CompositionBegin = InBeginIndex;
		CompositionLength = InLength;
	}

	virtual void EndComposition() override
	{
		bComposing = false;
		CompositionLength = 0;
		if (Entry)
		{
			Entry->ImeCompositionEnded();
		}
	}
};

FChaosImpactNameEntry::~FChaosImpactNameEntry()
{
	StopIme();
}

void FChaosImpactNameEntry::Open(const FString& Initial, const FString& InTitle, const int32 InOwner)
{
	StopIme();
	Text = Initial.Left(ChaosImpactSettings::MaxNameLength);
	Title = InTitle;
	Owner = InOwner;
	Page = KanaPage;
	CursorRow = 0;
	CursorColumn = 0;
	Shift = 0;
	bArrowNavigation = false;
	OpenedAt = FPlatformTime::Seconds();
	bOpen = true;
	Result = EResult::None;
	Message.Reset();
	StartIme();
}

void FChaosImpactNameEntry::Close()
{
	bOpen = false;
	StopIme();
}

FChaosImpactNameEntry::EResult FChaosImpactNameEntry::ConsumeResult(FString& OutName)
{
	const EResult Was = Result;
	Result = EResult::None;
	OutName = Text.TrimStartAndEnd();
	return Was;
}

// ---- The keys -----------------------------------------------------------------------------------------------------------

const TArray<FChaosImpactNameEntry::FRow>& FChaosImpactNameEntry::Rows() const
{
	static const TArray<FRow> Kana = MakeKana();
	static const TArray<FRow> Latin = MakeLatin();
	static const TArray<FRow> Symbols = MakeSymbols();
	return Page == LatinPage ? Latin : Page == SymbolPage ? Symbols : Kana;
}

FString FChaosImpactNameEntry::Shown(const FCell& Cell) const
{
	if (Cell.Action != EAction::Letter || Cell.Label.Len() != 1)
	{
		return Cell.Label;
	}
	TCHAR Letter = Cell.Label[0];
	if (Page == KatakanaPage && IsHiragana(Letter))
	{
		Letter = static_cast<TCHAR>(Letter + 0x60);
	}
	else if (Page == LatinPage && Shift > 0)
	{
		Letter = FChar::ToUpper(Letter);
	}
	return FString(1, &Letter);
}

FBox2D FChaosImpactNameEntry::CellRect(const int32 Row, const int32 Column) const
{
	const TArray<FRow>& All = Rows();
	if (!All.IsValidIndex(Row) || !All[Row].Cells.IsValidIndex(Column))
	{
		return FBox2D(FVector2D::ZeroVector, FVector2D::ZeroVector);
	}
	const bool bBottom = Row == All.Num() - 1;
	// The letter rows share one key width (the widest row fills the grid); the bottom row fills it by itself.
	float Widest = 1.0f;
	for (int32 Other = 0; Other < All.Num() - 1; ++Other)
	{
		Widest = FMath::Max(Widest, RowUnits(All[Other]));
	}
	const float Unit = (KeyGridWidth + KeyCellGap) / (bBottom ? RowUnits(All[Row]) : Widest);
	float Before = All[Row].Lead;
	for (int32 Index = 0; Index < Column; ++Index)
	{
		Before += All[Row].Cells[Index].Units;
	}
	const float X = KeyGridLeft + Before * Unit;
	const float Y = KeyGridTop + Row * (KeyCellHeight + KeyCellGap) + (bBottom ? BottomRowGap : 0.0f);
	return FBox2D(FVector2D(X, Y), FVector2D(X + All[Row].Cells[Column].Units * Unit - KeyCellGap, Y + KeyCellHeight));
}

bool FChaosImpactNameEntry::IsSelectable(const int32 Row, const int32 Column) const
{
	const TArray<FRow>& All = Rows();
	return All.IsValidIndex(Row) && All[Row].Cells.IsValidIndex(Column) && All[Row].Cells[Column].Action != EAction::None;
}

const FChaosImpactNameEntry::FCell* FChaosImpactNameEntry::GetCursorCell() const
{
	return IsSelectable(CursorRow, CursorColumn) ? &Rows()[CursorRow].Cells[CursorColumn] : nullptr;
}

bool FChaosImpactNameEntry::PointAt(const FString& Label)
{
	const TArray<FRow>& All = Rows();
	for (int32 Row = 0; Row < All.Num(); ++Row)
	{
		for (int32 Column = 0; Column < All[Row].Cells.Num(); ++Column)
		{
			if (IsSelectable(Row, Column) && (All[Row].Cells[Column].Label == Label || Shown(All[Row].Cells[Column]) == Label))
			{
				CursorRow = Row;
				CursorColumn = Column;
				return true;
			}
		}
	}
	return false;
}

void FChaosImpactNameEntry::SettleCursor()
{
	const TArray<FRow>& All = Rows();
	CursorRow = FMath::Clamp(CursorRow, 0, All.Num() - 1);
	if (IsSelectable(CursorRow, CursorColumn))
	{
		return;
	}
	// The nearest key across.
	const double Wanted = CellRect(CursorRow, FMath::Clamp(CursorColumn, 0, All[CursorRow].Cells.Num() - 1)).GetCenter().X;
	double Best = TNumericLimits<double>::Max();
	int32 BestColumn = 0;
	for (int32 Column = 0; Column < All[CursorRow].Cells.Num(); ++Column)
	{
		const double Distance = FMath::Abs(CellRect(CursorRow, Column).GetCenter().X - Wanted);
		if (IsSelectable(CursorRow, Column) && Distance < Best)
		{
			Best = Distance;
			BestColumn = Column;
		}
	}
	CursorColumn = BestColumn;
}

void FChaosImpactNameEntry::MoveCursor(const int32 InColumns, const int32 InRows)
{
	if (InColumns == 0 && InRows == 0)
	{
		return;
	}
	ChaosImpactSfx::Play2D(nullptr, EChaosImpactSfx::UiMove);
	const TArray<FRow>& All = Rows();
	SettleCursor();
	if (InRows != 0)
	{
		// To the key in the next row nearest across (the rows are staggered and their keys of different widths).
		const double Across = CellRect(CursorRow, CursorColumn).GetCenter().X;
		int32 Row = CursorRow;
		for (int32 Tries = 0; Tries < All.Num(); ++Tries)
		{
			Row = (Row + (InRows > 0 ? 1 : -1) + All.Num()) % All.Num();
			// An empty place straight across (五十音's や ゆ よ, わ を ん): on to the row after it, as a phone's かな does.
			bool bEmptyHere = false;
			for (int32 Column = 0; Column < All[Row].Cells.Num(); ++Column)
			{
				const FBox2D Place = CellRect(Row, Column);
				bEmptyHere |= !IsSelectable(Row, Column) && Across >= Place.Min.X && Across <= Place.Max.X;
			}
			if (bEmptyHere)
			{
				continue;
			}
			double Best = TNumericLimits<double>::Max();
			int32 BestColumn = INDEX_NONE;
			for (int32 Column = 0; Column < All[Row].Cells.Num(); ++Column)
			{
				const double Distance = FMath::Abs(CellRect(Row, Column).GetCenter().X - Across);
				if (IsSelectable(Row, Column) && Distance < Best)
				{
					Best = Distance;
					BestColumn = Column;
				}
			}
			if (BestColumn != INDEX_NONE)
			{
				CursorRow = Row;
				CursorColumn = BestColumn;
				break;
			}
		}
	}
	if (InColumns != 0)
	{
		const int32 Count = All[CursorRow].Cells.Num();
		for (int32 Step = 0; Step < FMath::Abs(InColumns); ++Step)
		{
			// The next key that can be pressed, round the row.
			for (int32 Tries = 0; Tries < Count; ++Tries)
			{
				CursorColumn = (CursorColumn + (InColumns > 0 ? 1 : -1) + Count) % Count;
				if (IsSelectable(CursorRow, CursorColumn))
				{
					break;
				}
			}
		}
	}
}

void FChaosImpactNameEntry::SetPage(const int32 InPage)
{
	ChaosImpactSfx::Play2D(nullptr, EChaosImpactSfx::UiTab);
	const double Across = CellRect(CursorRow, CursorColumn).GetCenter().X;
	const bool bWasBottom = CursorRow == Rows().Num() - 1;
	Page = (InPage + PageCount) % PageCount;
	Shift = 0;
	if (bWasBottom)
	{
		// (The bottom row is the same on every page.)
		CursorRow = Rows().Num() - 1;
		return;
	}
	CursorRow = FMath::Clamp(CursorRow, 0, Rows().Num() - 2);
	double Best = TNumericLimits<double>::Max();
	for (int32 Column = 0; Column < Rows()[CursorRow].Cells.Num(); ++Column)
	{
		const double Distance = FMath::Abs(CellRect(CursorRow, Column).GetCenter().X - Across);
		if (Distance < Best)
		{
			Best = Distance;
			CursorColumn = Column;
		}
	}
	SettleCursor();
}

// ---- Writing ------------------------------------------------------------------------------------------------------------

void FChaosImpactNameEntry::Say(const FString& Words)
{
	Message = Words;
	MessageAt = FPlatformTime::Seconds();
}

void FChaosImpactNameEntry::Append(const FString& Letter)
{
	if (Letter.IsEmpty())
	{
		return;
	}
	if (Text.Len() + Letter.Len() > ChaosImpactSettings::MaxNameLength)
	{
		Say(FString::Printf(TEXT("%d文字までです"), ChaosImpactSettings::MaxNameLength));
		ChaosImpactSfx::Play2D(nullptr, EChaosImpactSfx::UiDeny);
		return;
	}
	ChaosImpactSfx::Play2D(nullptr, EChaosImpactSfx::UiType);
	const int32 Was = Text.Len();
	Text += Letter;
	ImeTextChanged(Was);
}

void FChaosImpactNameEntry::ChangeLast(const int32 Mark)
{
	if (Text.IsEmpty())
	{
		return;
	}
	TCHAR& Last = Text[Text.Len() - 1];
	const bool bKatakana = IsKatakana(Last);
	// Work in hiragana; katakana is the same table 0x60 on.
	TCHAR Kana = bKatakana ? static_cast<TCHAR>(Last - 0x60) : Last;
	if (!IsHiragana(Kana))
	{
		return;
	}
	static const FString Voiceable(TEXT("かきくけこさしすせそたちつてとはひふへほ"));
	static const FString Voiced(TEXT("がぎぐげござじずぜぞだぢづでどばびぶべぼ"));
	static const FString HalfVoiceable(TEXT("はひふへほ"));
	static const FString HalfVoiced(TEXT("ぱぴぷぺぽ"));
	static const FString Smallable(TEXT("あいうえおつやゆよわ"));
	static const FString Small(TEXT("ぁぃぅぇぉっゃゅょゎ"));
	const auto Swap = [&Kana](const FString& From, const FString& To)
	{
		int32 Index = INDEX_NONE;
		if (From.FindChar(Kana, Index))
		{
			Kana = To[Index];
			return true;
		}
		if (To.FindChar(Kana, Index))
		{
			Kana = From[Index];
			return true;
		}
		return false;
	};
	if (Mark == 0)
	{
		// ゛: か→が (and back); ぱ→ば too.
		if (!Swap(Voiceable, Voiced))
		{
			int32 Index = INDEX_NONE;
			if (HalfVoiced.FindChar(Kana, Index))
			{
				Kana = Voiced[Voiceable.Find(FString(1, &HalfVoiceable[Index]))];
			}
		}
	}
	else if (Mark == 1)
	{
		// ゜: は→ぱ (and back); ば→ぱ too.
		if (!Swap(HalfVoiceable, HalfVoiced))
		{
			int32 Index = INDEX_NONE;
			if (Voiced.FindChar(Kana, Index) && Index >= 15)
			{
				Kana = HalfVoiced[Index - 15];
			}
		}
	}
	else
	{
		Swap(Smallable, Small);
	}
	Last = bKatakana ? static_cast<TCHAR>(Kana + 0x60) : Kana;
	ImeTextChanged(Text.Len());
}

void FChaosImpactNameEntry::Backspace()
{
	if (!Text.IsEmpty())
	{
		const int32 Was = Text.Len();
		Text.LeftChopInline(1);
		ChaosImpactSfx::Play2D(nullptr, EChaosImpactSfx::UiErase);
		ImeTextChanged(Was);
	}
}

void FChaosImpactNameEntry::Finish()
{
	if (Text.TrimStartAndEnd().IsEmpty())
	{
		Say(TEXT("なまえを入れてください"));
		ChaosImpactSfx::Play2D(nullptr, EChaosImpactSfx::UiDeny);
		return;
	}
	ChaosImpactSfx::Play2D(nullptr, EChaosImpactSfx::UiConfirm);
	Result = EResult::Done;
	Close();
}

void FChaosImpactNameEntry::Cancel()
{
	Result = EResult::Cancelled;
	Close();
}

void FChaosImpactNameEntry::PressCell()
{
	const FCell* Cell = GetCursorCell();
	if (!Cell)
	{
		return;
	}
	switch (Cell->Action)
	{
	case EAction::Letter:
		Append(Shown(*Cell));
		if (Shift == 1)
		{
			// ⇧ once: one capital, as a phone does.
			Shift = 0;
		}
		break;
	case EAction::Mark:
		ChangeLast(Cell->Arg);
		break;
	case EAction::Shift:
		// Off, the next letter, locked, off again.
		Shift = (Shift + 1) % 3;
		ChaosImpactSfx::Play2D(nullptr, EChaosImpactSfx::UiValue);
		break;
	case EAction::Page:
		SetPage(Cell->Arg);
		break;
	case EAction::Space:
		Append(TEXT(" "));
		break;
	case EAction::Back:
		Backspace();
		break;
	case EAction::Done:
		Finish();
		break;
	default:
		break;
	}
}

// ---- Input --------------------------------------------------------------------------------------------------------------

bool FChaosImpactNameEntry::IsComposing() const
{
	return Ime.IsValid() && Ime->bComposing;
}

bool FChaosImpactNameEntry::HandleKey(const FKey& Key, const bool bRepeat)
{
	if (!bOpen)
	{
		return false;
	}
	if (IsComposing())
	{
		// The IME has the keys while it converts (Enter decides, Escape drops, the arrows choose).
		return true;
	}
	const bool bJustDecided = FPlatformTime::Seconds() - ImeEndedAt < ImeEcho;
	const bool bArrowKey = Key == EKeys::Up || Key == EKeys::Down || Key == EKeys::Left || Key == EKeys::Right;
	if (Key == EKeys::Up || Key == EKeys::Gamepad_DPad_Up)
	{
		MoveCursor(0, -1);
	}
	else if (Key == EKeys::Down || Key == EKeys::Gamepad_DPad_Down)
	{
		MoveCursor(0, 1);
	}
	else if (Key == EKeys::Left || Key == EKeys::Gamepad_DPad_Left)
	{
		MoveCursor(-1, 0);
	}
	else if (Key == EKeys::Right || Key == EKeys::Gamepad_DPad_Right)
	{
		MoveCursor(1, 0);
	}
	else if (bRepeat)
	{
		// Only moving and rubbing out repeat.
		if (Key == EKeys::BackSpace)
		{
			Backspace();
		}
		return true;
	}
	else if (Key == EKeys::Gamepad_FaceButton_Bottom)
	{
		PressCell();
	}
	else if (Key == EKeys::Enter)
	{
		if (!bJustDecided)
		{
			// After the arrow keys moved over the keys: that key. Otherwise the name is done.
			if (bArrowNavigation) { PressCell(); } else { Finish(); }
		}
	}
	else if (Key == EKeys::Gamepad_Special_Right)
	{
		Finish();
	}
	else if ((Key == EKeys::Escape && !bJustDecided) || (Key == EKeys::Gamepad_FaceButton_Right && Text.IsEmpty()))
	{
		Cancel();
	}
	else if (Key == EKeys::BackSpace || Key == EKeys::Gamepad_FaceButton_Right || Key == EKeys::Gamepad_FaceButton_Left)
	{
		Backspace();
	}
	else if (Key == EKeys::Gamepad_LeftShoulder)
	{
		SetPage(Page - 1);
	}
	else if (Key == EKeys::Gamepad_RightShoulder || Key == EKeys::Gamepad_FaceButton_Top || Key == EKeys::Tab)
	{
		SetPage(Page + 1);
	}
	else if (Key == EKeys::Gamepad_RightTrigger || Key == EKeys::Gamepad_LeftTrigger)
	{
		// ⇧ from the controller (to the 英字 page first).
		if (Page != LatinPage)
		{
			SetPage(LatinPage);
		}
		Shift = (Shift + 1) % 3;
		ChaosImpactSfx::Play2D(nullptr, EChaosImpactSfx::UiValue);
	}
	if (bArrowKey)
	{
		bArrowNavigation = true;
	}
	// Everything else (letters typed on a keyboard arrive as characters, Space too) is kept here.
	return true;
}

bool FChaosImpactNameEntry::HandleCharacter(const TCHAR Character)
{
	if (!bOpen)
	{
		return false;
	}
	if (IsComposing() || Character < 32 || Character == 127)
	{
		return true;
	}
	if (Character == TEXT(' ') && FPlatformTime::Seconds() - ImeEndedAt < ImeEcho)
	{
		return true;
	}
	bArrowNavigation = false;
	Append(FString(1, &Character));
	return true;
}

bool FChaosImpactNameEntry::HandleAnalog(const FKey& Key, const float Value)
{
	if (!bOpen || (Key != EKeys::Gamepad_LeftX && Key != EKeys::Gamepad_LeftY))
	{
		return false;
	}
	const int32 Axis = Key == EKeys::Gamepad_LeftX ? 0 : 1;
	const double Now = FPlatformTime::Seconds();
	if (FMath::Abs(Value) < 0.3f)
	{
		bStickHeld[Axis] = false;
		return true;
	}
	if (FMath::Abs(Value) >= 0.65f && (!bStickHeld[Axis] || Now >= NextStickAt[Axis]))
	{
		NextStickAt[Axis] = Now + (bStickHeld[Axis] ? 0.12 : 0.36);
		bStickHeld[Axis] = true;
		if (Axis == 0)
		{
			MoveCursor(Value > 0.0f ? 1 : -1, 0);
		}
		else
		{
			MoveCursor(0, Value > 0.0f ? -1 : 1);
		}
	}
	return true;
}

bool FChaosImpactNameEntry::HandleClick(const FVector2D& DesignPoint)
{
	if (!bOpen)
	{
		return false;
	}
	const TArray<FRow>& All = Rows();
	for (int32 Row = 0; Row < All.Num(); ++Row)
	{
		for (int32 Column = 0; Column < All[Row].Cells.Num(); ++Column)
		{
			if (IsSelectable(Row, Column) && CellRect(Row, Column).IsInside(DesignPoint))
			{
				CursorRow = Row;
				CursorColumn = Column;
				bArrowNavigation = false;
				PressCell();
				return true;
			}
		}
	}
	return true;
}

void FChaosImpactNameEntry::HandleMouseMove(const FVector2D& DesignPoint)
{
	if (!bOpen)
	{
		return;
	}
	const TArray<FRow>& All = Rows();
	for (int32 Row = 0; Row < All.Num(); ++Row)
	{
		for (int32 Column = 0; Column < All[Row].Cells.Num(); ++Column)
		{
			if (IsSelectable(Row, Column) && CellRect(Row, Column).IsInside(DesignPoint))
			{
				CursorRow = Row;
				CursorColumn = Column;
				return;
			}
		}
	}
}

// ---- The system's IME ---------------------------------------------------------------------------------------------------

bool FChaosImpactNameEntry::ImeComposeForTests(const FString& Composing)
{
	if (!Ime.IsValid())
	{
		return false;
	}
	if (!Ime->bComposing)
	{
		Ime->BeginComposition();
	}
	const uint32 Begin = static_cast<uint32>(Ime->CompositionBegin);
	Ime->SetTextInRange(Begin, Ime->CompositionLength, Composing);
	Ime->UpdateCompositionRange(static_cast<int32>(Begin), static_cast<uint32>(Composing.Len()));
	return true;
}

bool FChaosImpactNameEntry::ImeDecideForTests(const FString& Decided)
{
	if (!Ime.IsValid() || !Ime->bComposing)
	{
		return false;
	}
	Ime->SetTextInRange(static_cast<uint32>(Ime->CompositionBegin), Ime->CompositionLength, Decided);
	Ime->EndComposition();
	return true;
}

void FChaosImpactNameEntry::StartIme()
{
	ITextInputMethodSystem* System = FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetTextInputMethodSystem() : nullptr;
	if (!System || Ime.IsValid())
	{
		return;
	}
	Ime = MakeShared<FChaosImpactNameEntryIme>(this);
	Ime->SelectionBegin = static_cast<uint32>(Text.Len());
	ImeNotifier = System->RegisterContext(Ime.ToSharedRef());
	System->ActivateContext(Ime.ToSharedRef());
}

void FChaosImpactNameEntry::StopIme()
{
	if (!Ime.IsValid())
	{
		return;
	}
	if (ITextInputMethodSystem* System = FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetTextInputMethodSystem() : nullptr)
	{
		if (Ime->bComposing && ImeNotifier.IsValid())
		{
			ImeNotifier->CancelComposition();
		}
		System->DeactivateContext(Ime.ToSharedRef());
		System->UnregisterContext(Ime.ToSharedRef());
	}
	Ime->Entry = nullptr;
	Ime.Reset();
	ImeNotifier.Reset();
}

void FChaosImpactNameEntry::ImeReplace(const int32 Begin, const int32 Length, const FString& With, const bool bComposing)
{
	const int32 At = FMath::Clamp(Begin, 0, Text.Len());
	const int32 Gone = FMath::Clamp(Length, 0, Text.Len() - At);
	// (No line breaks or other controls in a name.)
	FString Clean;
	for (const TCHAR Letter : With)
	{
		if (Letter >= 32 && Letter != 127)
		{
			Clean.AppendChar(Letter);
		}
	}
	Text = Text.Left(At) + Clean + Text.Mid(At + Gone);
	bArrowNavigation = false;
	if (!bComposing)
	{
		ImeCompositionEnded();
	}
}

void FChaosImpactNameEntry::ImeCompositionEnded()
{
	ImeEndedAt = FPlatformTime::Seconds();
	if (Text.Len() > ChaosImpactSettings::MaxNameLength)
	{
		Text.LeftInline(ChaosImpactSettings::MaxNameLength);
		Say(FString::Printf(TEXT("%d文字までです"), ChaosImpactSettings::MaxNameLength));
		ChaosImpactSfx::Play2D(nullptr, EChaosImpactSfx::UiDeny);
		return;
	}
	ChaosImpactSfx::Play2D(nullptr, EChaosImpactSfx::UiType);
}

void FChaosImpactNameEntry::ImeTextChanged(const int32 OldLength)
{
	if (!Ime.IsValid() || Ime->bComposing)
	{
		return;
	}
	Ime->SelectionBegin = static_cast<uint32>(Text.Len());
	Ime->SelectionLength = 0;
	if (ImeNotifier.IsValid())
	{
		ImeNotifier->NotifyTextChanged(0, static_cast<uint32>(OldLength), static_cast<uint32>(Text.Len()));
		ImeNotifier->NotifySelectionChanged();
	}
}

void FChaosImpactNameEntry::ImeBounds(const int32 Begin, const int32 Length, FVector2D& OutPosition, FVector2D& OutSize) const
{
	if (!bPainted)
	{
		OutPosition = FVector2D::ZeroVector;
		OutSize = FVector2D::ZeroVector;
		return;
	}
	const int32 At = FMath::Clamp(Begin, 0, Text.Len());
	const float Left = NameLeft + ChaosImpactPaint::HudMeasure(Text.Left(At), NameSize, TEXT("Black"));
	const float Width = FMath::Max(ChaosImpactPaint::HudMeasure(Text.Mid(At, FMath::Max(Length, 0)), NameSize, TEXT("Black")), 4.0f);
	const FVector2D TopLeft = PaintedDesign.LocalToAbsolute(FVector2D(Left, NameTop + PaintedLift));
	const FVector2D BottomRight = PaintedDesign.LocalToAbsolute(FVector2D(Left + Width, NameTop + PaintedLift + NameSize * 1.3f));
	OutPosition = TopLeft;
	OutSize = BottomRight - TopLeft;
}

// ---- Drawing ------------------------------------------------------------------------------------------------------------

int32 FChaosImpactNameEntry::Paint(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer) const
{
	if (!bOpen)
	{
		return Layer;
	}
	using namespace ChaosImpactPaint;
	const double Now = FPlatformTime::Seconds();
	const float In = EaseOut(static_cast<float>(Now - OpenedAt) / 0.22f);
	const FPainter Shade{Design, Elements, Layer, In};
	Shade.Box(-1400.0f, -200.0f, 4400.0f, 1300.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.62f));
	const FPainter P{Design, Elements, Layer + 1, In};
	const float Lift = (1.0f - In) * 30.0f;
	PaintedDesign = Design;
	PaintedLift = Lift;
	bPainted = true;
	RoundedBox(P, 230.0f, 112.0f + Lift, 1140.0f, 722.0f, 28.0f, FLinearColor(0.016f, 0.024f, 0.05f, 0.98f));
	P.Box(260.0f, 112.0f + Lift, 160.0f, 6.0f, Ice);
	P.Box(420.0f, 112.0f + Lift, 160.0f, 6.0f, Fire);
	const FPainter T{Design, Elements, Layer + 2, In};
	T.Text(Title, 800.0f, 128.0f + Lift, 30.0f, Paper, ETextAlign::Center, TEXT("Black"));

	// The name so far, with a blinking caret (the IME's letters not yet decided underlined), and how many letters.
	RoundedBox(T, 330.0f, 186.0f + Lift, 940.0f, 86.0f, 18.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.55f));
	const bool bCaret = FMath::Fmod(Now - OpenedAt, 1.0) < 0.6;
	const FPainter V{Design, Elements, Layer + 3, In};
	V.Text(Text + (bCaret && !IsComposing() ? TEXT("｜") : TEXT("　")), NameLeft, NameTop + Lift, NameSize, Paper, ETextAlign::Left, TEXT("Black"));
	if (IsComposing() && Ime->CompositionLength > 0)
	{
		const int32 From = FMath::Clamp(Ime->CompositionBegin, 0, Text.Len());
		const float X0 = NameLeft + HudMeasure(Text.Left(From), NameSize, TEXT("Black"));
		const float W = HudMeasure(Text.Mid(From, static_cast<int32>(Ime->CompositionLength)), NameSize, TEXT("Black"));
		V.Box(X0, NameTop + Lift + NameSize + 12.0f, W, 3.0f, Ice);
	}
	V.Text(FString::Printf(TEXT("%d / %d"), Text.Len(), ChaosImpactSettings::MaxNameLength), 1250.0f, 220.0f + Lift, 22.0f,
		Muted, ETextAlign::Right, TEXT("Bold"));
	if (!Message.IsEmpty() && Now - MessageAt < 1.8)
	{
		V.Text(Message, 800.0f, 274.0f + Lift, 20.0f, Fire, ETextAlign::Center, TEXT("Bold"));
	}

	// The keys.
	const TArray<FRow>& All = Rows();
	for (int32 Row = 0; Row < All.Num(); ++Row)
	{
		const bool bBottom = Row == All.Num() - 1;
		for (int32 Column = 0; Column < All[Row].Cells.Num(); ++Column)
		{
			const FCell& Cell = All[Row].Cells[Column];
			if (Cell.Action == EAction::None)
			{
				continue;
			}
			const FBox2D Rect = CellRect(Row, Column);
			const float X = static_cast<float>(Rect.Min.X);
			const float Y = static_cast<float>(Rect.Min.Y) + Lift;
			const float W = static_cast<float>(Rect.GetSize().X);
			const float H = static_cast<float>(Rect.GetSize().Y);
			const bool bSelected = Row == CursorRow && Column == CursorColumn;
			const bool bCurrentPage = Cell.Action == EAction::Page && Cell.Arg == Page;
			const bool bShiftOn = Cell.Action == EAction::Shift && Shift > 0;
			const bool bFunction = Cell.Action == EAction::Shift || Cell.Action == EAction::Back || Cell.Action == EAction::Mark;
			const FLinearColor Fill = bSelected ? FLinearColor(0.07f, 0.2f, 0.42f, 1.0f)
				: bShiftOn ? FLinearColor(0.1f, 0.24f, 0.4f, 1.0f)
				: bCurrentPage ? FLinearColor(0.06f, 0.1f, 0.2f, 1.0f)
				: bFunction || bBottom ? FLinearColor(0.03f, 0.042f, 0.07f, 1.0f) : FLinearColor(0.045f, 0.06f, 0.1f, 1.0f);
			RoundedBox(T, X, Y, W, H, 14.0f, Fill);
			if (bSelected)
			{
				RoundedBox(V, X - 3.0f, Y + H - 5.0f, W + 6.0f, 5.0f, 2.0f, Cell.Action == EAction::Done ? Gold : Ice);
			}
			const FVector2D Middle(X + W * 0.5f, Y + H * 0.5f);
			if (Cell.Action == EAction::Shift)
			{
				// ⇧: an arrow up (filled while on, with a bar under it when locked).
				const FLinearColor ShiftInk = bShiftOn ? Ice : Paper;
				const FVector2D Tip = Middle + FVector2D(0.0, -18.0);
				V.Line(Tip, Middle + FVector2D(-17.0, 0.0), ShiftInk, 3.0f);
				V.Line(Tip, Middle + FVector2D(17.0, 0.0), ShiftInk, 3.0f);
				V.Line(Middle + FVector2D(-17.0, 0.0), Middle + FVector2D(-8.0, 0.0), ShiftInk, 3.0f);
				V.Line(Middle + FVector2D(17.0, 0.0), Middle + FVector2D(8.0, 0.0), ShiftInk, 3.0f);
				V.Line(Middle + FVector2D(-8.0, 0.0), Middle + FVector2D(-8.0, 13.0), ShiftInk, 3.0f);
				V.Line(Middle + FVector2D(8.0, 0.0), Middle + FVector2D(8.0, 13.0), ShiftInk, 3.0f);
				V.Line(Middle + FVector2D(-8.0, 13.0), Middle + FVector2D(8.0, 13.0), ShiftInk, 3.0f);
				if (Shift > 0)
				{
					V.Box(static_cast<float>(Middle.X) - 7.0f, static_cast<float>(Middle.Y) - 8.0f, 14.0f, 20.0f, ShiftInk);
				}
				if (Shift == 2)
				{
					V.Box(static_cast<float>(Middle.X) - 10.0f, static_cast<float>(Middle.Y) + 19.0f, 20.0f, 3.0f, ShiftInk);
				}
				continue;
			}
			if (Cell.Action == EAction::Back && !bBottom)
			{
				// ⌫: a key pointing left with a cross in it.
				const FVector2D A = Middle + FVector2D(-22.0, 0.0), B = Middle + FVector2D(-10.0, -14.0), C = Middle + FVector2D(20.0, -14.0);
				const FVector2D D = Middle + FVector2D(20.0, 14.0), E = Middle + FVector2D(-10.0, 14.0);
				V.Line(A, B, Paper, 3.0f);
				V.Line(B, C, Paper, 3.0f);
				V.Line(C, D, Paper, 3.0f);
				V.Line(D, E, Paper, 3.0f);
				V.Line(E, A, Paper, 3.0f);
				V.Line(Middle + FVector2D(-2.0, -6.0), Middle + FVector2D(10.0, 6.0), Paper, 3.0f);
				V.Line(Middle + FVector2D(10.0, -6.0), Middle + FVector2D(-2.0, 6.0), Paper, 3.0f);
				continue;
			}
			const FString Label = Shown(Cell);
			const float Size = bBottom ? 24.0f : 34.0f;
			const FLinearColor LabelInk = Cell.Action == EAction::Done ? Gold : bCurrentPage ? Ice : Paper;
			V.Text(Label, static_cast<float>(Middle.X), static_cast<float>(Middle.Y) - Size * 0.68f, HudFit(Label, Size, W - 12.0f), LabelInk,
				ETextAlign::Center, TEXT("Black"));
		}
	}
	const FString Help = TEXT("A：えらぶ　B：けす（空ならやめる）　LB/RB：もじの種類　RT：大文字　START：おわり　／　キーボード：そのまま入力（半角/全角キーで日本語・変換）　Enter：おわり　Esc：やめる");
	// (Inside the panel, under the keys: the screen behind never shows through it.)
	V.Text(Help, 800.0f, 780.0f + Lift, HudFit(Help, 16.0f, 1100.0f, TEXT("Bold")), WithAlpha(Paper, 0.75f), ETextAlign::Center, TEXT("Bold"));
	return Layer + 4;
}
