#include "ChaosImpactNameEntry.h"
#include "ChaosImpactSfx.h"

#include "ChaosImpactPaint.h"
#include "ChaosImpactSettings.h"

namespace
{

	// Each page: five rows of eleven. "　" is an empty cell; ゛ ゜ 小 change the last letter.
	const TCHAR* const HiraganaRows[] = {
		TEXT("あかさたなはまやらわ゛"),
		TEXT("いきしちにひみ　りを゜"),
		TEXT("うくすつぬふむゆるん小"),
		TEXT("えけせてねへめ　れー！"),
		TEXT("おこそとのほもよろ～？")};
	const TCHAR* const UpperRows[] = {
		TEXT("ABCDEFGHIJK"),
		TEXT("LMNOPQRSTUV"),
		TEXT("WXYZ0123456"),
		TEXT("789-_.!?&#@"),
		TEXT("+=()☆★♪・♡%*")};
	const TCHAR* const LowerRows[] = {
		TEXT("abcdefghijk"),
		TEXT("lmnopqrstuv"),
		TEXT("wxyz0123456"),
		TEXT("789-_.!?&#@"),
		TEXT("+=()☆★♪・♡%*")};
	const TCHAR* const KeyNames[] = {TEXT("ひらがな"), TEXT("カタカナ"), TEXT("ABC"), TEXT("abc"), TEXT("けす"), TEXT("おわり")};

	constexpr float KeyGridLeft = 276.0f;
	constexpr float KeyGridTop = 300.0f;
	constexpr float KeyCellWidth = 88.0f;
	constexpr float KeyCellHeight = 70.0f;
	constexpr float KeyCellGap = 8.0f;

	bool IsHiragana(const TCHAR Letter)
	{
		return Letter >= 0x3041 && Letter <= 0x3096;
	}

	bool IsKatakana(const TCHAR Letter)
	{
		return Letter >= 0x30A1 && Letter <= 0x30F6;
	}

}

void FChaosImpactNameEntry::Open(const FString& Initial, const FString& InTitle, const int32 InOwner)
{
	Text = Initial.Left(ChaosImpactSettings::MaxNameLength);
	Title = InTitle;
	Owner = InOwner;
	Page = 0;
	CursorRow = 0;
	CursorColumn = 0;
	OpenedAt = FPlatformTime::Seconds();
	bOpen = true;
	Result = EResult::None;
	Message.Reset();
}

FChaosImpactNameEntry::EResult FChaosImpactNameEntry::ConsumeResult(FString& OutName)
{
	const EResult Was = Result;
	Result = EResult::None;
	OutName = Text.TrimStartAndEnd();
	return Was;
}

FString FChaosImpactNameEntry::CellText(const int32 Row, const int32 Column) const
{
	if (Row >= LetterRows)
	{
		return Column >= 0 && Column < KeyRowCellCount() ? FString(KeyNames[Column]) : FString();
	}
	if (Row < 0 || Column < 0 || Column >= Columns)
	{
		return FString();
	}
	const TCHAR* Source = Page >= 2 ? (Page == 2 ? UpperRows[Row] : LowerRows[Row]) : HiraganaRows[Row];
	if (Column >= FCString::Strlen(Source))
	{
		return FString();
	}
	TCHAR Letter = Source[Column];
	if (Page == 1 && IsHiragana(Letter))
	{
		Letter = static_cast<TCHAR>(Letter + 0x60);
	}
	return Letter == TEXT('　') ? FString() : FString(1, &Letter);
}

FBox2D FChaosImpactNameEntry::CellRect(const int32 Row, const int32 Column) const
{
	if (Row >= LetterRows)
	{
		const float Width = (Columns * (KeyCellWidth + KeyCellGap) - KeyCellGap - (KeyRowCellCount() - 1) * KeyCellGap) / KeyRowCellCount();
		const float X = KeyGridLeft + Column * (Width + KeyCellGap);
		const float Y = KeyGridTop + LetterRows * (KeyCellHeight + KeyCellGap) + 6.0f;
		return FBox2D(FVector2D(X, Y), FVector2D(X + Width, Y + KeyCellHeight));
	}
	const float X = KeyGridLeft + Column * (KeyCellWidth + KeyCellGap);
	const float Y = KeyGridTop + Row * (KeyCellHeight + KeyCellGap);
	return FBox2D(FVector2D(X, Y), FVector2D(X + KeyCellWidth, Y + KeyCellHeight));
}

void FChaosImpactNameEntry::MoveCursor(const int32 InColumns, const int32 InRows)
{
	if (InColumns != 0 || InRows != 0)
	{
		ChaosImpactSfx::Play2D(nullptr, EChaosImpactSfx::UiMove);
	}
	if (InRows != 0)
	{
		const int32 OldRow = CursorRow;
		CursorRow = (CursorRow + InRows + LetterRows + 1) % (LetterRows + 1);
		// Between the letters and the wider keys, keep roughly the same place across.
		if (OldRow < LetterRows && CursorRow == LetterRows)
		{
			CursorColumn = FMath::Clamp(CursorColumn * KeyRowCellCount() / Columns, 0, KeyRowCellCount() - 1);
		}
		else if (OldRow == LetterRows && CursorRow < LetterRows)
		{
			CursorColumn = FMath::Clamp((CursorColumn * Columns + Columns / 2) / KeyRowCellCount(), 0, Columns - 1);
		}
	}
	if (InColumns != 0)
	{
		const int32 Count = CursorRow == LetterRows ? KeyRowCellCount() : Columns;
		CursorColumn = (CursorColumn + InColumns + Count) % Count;
	}
}

void FChaosImpactNameEntry::SetPage(const int32 InPage)
{
	ChaosImpactSfx::Play2D(nullptr, EChaosImpactSfx::UiTab);
	Page = (InPage + PageCount) % PageCount;
}

void FChaosImpactNameEntry::Append(const FString& Letter)
{
	if (Letter.IsEmpty())
	{
		return;
	}
	if (Text.Len() + Letter.Len() > ChaosImpactSettings::MaxNameLength)
	{
		Message = FString::Printf(TEXT("%d文字までです"), ChaosImpactSettings::MaxNameLength);
		MessageAt = FPlatformTime::Seconds();
		ChaosImpactSfx::Play2D(nullptr, EChaosImpactSfx::UiDeny);
		return;
	}
	ChaosImpactSfx::Play2D(nullptr, EChaosImpactSfx::UiType);
	Text += Letter;
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
}

void FChaosImpactNameEntry::Backspace()
{
	if (!Text.IsEmpty())
	{
		Text.LeftChopInline(1);
		ChaosImpactSfx::Play2D(nullptr, EChaosImpactSfx::UiErase);
	}
}

void FChaosImpactNameEntry::Finish()
{
	if (Text.TrimStartAndEnd().IsEmpty())
	{
		Message = TEXT("なまえを入れてください");
		MessageAt = FPlatformTime::Seconds();
		ChaosImpactSfx::Play2D(nullptr, EChaosImpactSfx::UiDeny);
		return;
	}
	ChaosImpactSfx::Play2D(nullptr, EChaosImpactSfx::UiConfirm);
	Result = EResult::Done;
	bOpen = false;
}

void FChaosImpactNameEntry::PressCell()
{
	if (CursorRow == LetterRows)
	{
		if (CursorColumn < PageCount)
		{
			SetPage(CursorColumn);
		}
		else if (CursorColumn == 4)
		{
			Backspace();
		}
		else
		{
			Finish();
		}
		return;
	}
	const FString Letter = CellText(CursorRow, CursorColumn);
	if (Letter == TEXT("゛") || Letter == TEXT("゜") || Letter == TEXT("小"))
	{
		ChangeLast(Letter == TEXT("゛") ? 0 : Letter == TEXT("゜") ? 1 : 2);
		return;
	}
	Append(Letter);
}

bool FChaosImpactNameEntry::HandleKey(const FKey& Key, const bool bRepeat)
{
	if (!bOpen)
	{
		return false;
	}
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
	else if (Key == EKeys::Gamepad_FaceButton_Bottom || Key == EKeys::SpaceBar)
	{
		PressCell();
	}
	else if (Key == EKeys::Enter || Key == EKeys::Gamepad_Special_Right)
	{
		Finish();
	}
	else if (Key == EKeys::Escape || (Key == EKeys::Gamepad_FaceButton_Right && Text.IsEmpty()))
	{
		Result = EResult::Cancelled;
		bOpen = false;
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
	// Everything else (letters typed on a keyboard arrive as characters) is kept here too.
	return true;
}

bool FChaosImpactNameEntry::HandleCharacter(const TCHAR Character)
{
	if (!bOpen)
	{
		return false;
	}
	// Space presses the cell under the cursor (handled as a key); other printable letters are written.
	if (Character > 32 && Character != 127)
	{
		Append(FString(1, &Character));
	}
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
	for (int32 Row = 0; Row <= LetterRows; ++Row)
	{
		const int32 Count = Row == LetterRows ? KeyRowCellCount() : Columns;
		for (int32 Column = 0; Column < Count; ++Column)
		{
			if (CellRect(Row, Column).IsInside(DesignPoint))
			{
				CursorRow = Row;
				CursorColumn = Column;
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
	for (int32 Row = 0; Row <= LetterRows; ++Row)
	{
		const int32 Count = Row == LetterRows ? KeyRowCellCount() : Columns;
		for (int32 Column = 0; Column < Count; ++Column)
		{
			if (CellRect(Row, Column).IsInside(DesignPoint) && !CellText(Row, Column).IsEmpty())
			{
				CursorRow = Row;
				CursorColumn = Column;
				return;
			}
		}
	}
}

int32 FChaosImpactNameEntry::Paint(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer) const
{
	if (!bOpen)
	{
		return Layer;
	}
	const double Now = FPlatformTime::Seconds();
	const float In = ChaosImpactPaint::EaseOut(static_cast<float>(Now - OpenedAt) / 0.22f);
	const ChaosImpactPaint::FPainter Shade{Design, Elements, Layer, In};
	Shade.Box(-400.0f, -200.0f, 2400.0f, 1300.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.62f));
	const ChaosImpactPaint::FPainter P{Design, Elements, Layer + 1, In};
	const float Lift = (1.0f - In) * 30.0f;
	ChaosImpactPaint::RoundedBox(P, 230.0f, 112.0f + Lift, 1140.0f, 690.0f, 28.0f, FLinearColor(0.016f, 0.024f, 0.05f, 0.98f));
	P.Box(260.0f, 112.0f + Lift, 160.0f, 6.0f, ChaosImpactPaint::Ice);
	P.Box(420.0f, 112.0f + Lift, 160.0f, 6.0f, ChaosImpactPaint::Fire);
	const ChaosImpactPaint::FPainter T{Design, Elements, Layer + 2, In};
	T.Text(Title, 800.0f, 128.0f + Lift, 30.0f, ChaosImpactPaint::Paper, ChaosImpactPaint::ETextAlign::Center, TEXT("Black"));

	// The name so far, with a blinking caret, and how many letters are left.
	ChaosImpactPaint::RoundedBox(T, 330.0f, 186.0f + Lift, 940.0f, 86.0f, 18.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.55f));
	const bool bCaret = FMath::Fmod(Now - OpenedAt, 1.0) < 0.6;
	const ChaosImpactPaint::FPainter V{Design, Elements, Layer + 3, In};
	V.Text(Text + (bCaret ? TEXT("｜") : TEXT("　")), 360.0f, 198.0f + Lift, 44.0f, ChaosImpactPaint::Paper, ChaosImpactPaint::ETextAlign::Left, TEXT("Black"));
	V.Text(FString::Printf(TEXT("%d / %d"), Text.Len(), ChaosImpactSettings::MaxNameLength), 1250.0f, 220.0f + Lift, 22.0f,
		ChaosImpactPaint::Muted, ChaosImpactPaint::ETextAlign::Right, TEXT("Bold"));
	if (!Message.IsEmpty() && Now - MessageAt < 1.8)
	{
		V.Text(Message, 800.0f, 274.0f + Lift, 20.0f, ChaosImpactPaint::Fire, ChaosImpactPaint::ETextAlign::Center, TEXT("Bold"));
	}

	// The letters, and the keys below them.
	for (int32 Row = 0; Row <= LetterRows; ++Row)
	{
		const int32 Count = Row == LetterRows ? KeyRowCellCount() : Columns;
		for (int32 Column = 0; Column < Count; ++Column)
		{
			const FString Cell = CellText(Row, Column);
			if (Cell.IsEmpty())
			{
				continue;
			}
			const FBox2D Rect = CellRect(Row, Column);
			const float X = static_cast<float>(Rect.Min.X);
			const float Y = static_cast<float>(Rect.Min.Y) + Lift;
			const float W = static_cast<float>(Rect.GetSize().X);
			const float H = static_cast<float>(Rect.GetSize().Y);
			const bool bSelected = Row == CursorRow && Column == CursorColumn;
			const bool bPageKey = Row == LetterRows && Column < PageCount;
			const bool bCurrentPage = bPageKey && Column == Page;
			const FLinearColor Fill = bSelected ? FLinearColor(0.07f, 0.2f, 0.42f, 1.0f)
				: bCurrentPage ? FLinearColor(0.06f, 0.1f, 0.2f, 1.0f) : FLinearColor(0.04f, 0.055f, 0.09f, 1.0f);
			ChaosImpactPaint::RoundedBox(T, X, Y, W, H, 14.0f, Fill);
			if (bSelected)
			{
				ChaosImpactPaint::RoundedBox(V, X - 3.0f, Y + H - 5.0f, W + 6.0f, 5.0f, 2.0f, Row == LetterRows && Column == 5 ? ChaosImpactPaint::Gold : ChaosImpactPaint::Ice);
			}
			const bool bKey = Row == LetterRows;
			V.Text(Cell, X + W * 0.5f, Y + (bKey ? 18.0f : 10.0f), bKey ? 24.0f : 36.0f,
				bKey && Column == 5 ? ChaosImpactPaint::Gold : bCurrentPage ? ChaosImpactPaint::Ice : ChaosImpactPaint::Paper, ChaosImpactPaint::ETextAlign::Center, TEXT("Black"));
		}
	}
	V.Text(TEXT("A：えらぶ　B：けす（空ならやめる）　LB/RB：もじの種類　START：おわり　／　キーボード：そのまま入力・Enter：おわり・Esc：やめる"),
		800.0f, 818.0f, 17.0f, ChaosImpactPaint::WithAlpha(ChaosImpactPaint::Paper, 0.8f), ChaosImpactPaint::ETextAlign::Center, TEXT("Bold"));
	return Layer + 4;
}
