#include "ChaosImpactLocalTourney.h"

#include "ChaosImpactCharacterPreview.h"
#include "ChaosImpactCharacterRoster.h"
#include "ChaosImpactCustomColour.h"
#include "ChaosImpactMatchTypes.h"
#include "ChaosImpactPaint.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactScreen.h"
#include "ChaosImpactSettings.h"
#include "ChaosImpactSfx.h"
#include "Engine/GameInstance.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"

namespace
{
	using namespace ChaosImpactPaint;

	// Layout in the 1600 x 900 design space: the characters left, the person large in the middle with their colours
	// under them, how it runs and the people on the right, the buttons along the bottom.
	constexpr int32 LtColumns = 4;
	constexpr float LtTile = 92.0f;
	constexpr float LtTileGap = 8.0f;
	constexpr float LtGridLeft = 60.0f;
	constexpr float LtGridTop = 150.0f;
	constexpr float LtPreviewLeft = 500.0f;
	constexpr float LtPreviewTop = 146.0f;
	constexpr float LtPreviewWidth = 300.0f;
	constexpr float LtPreviewHeight = 400.0f;
	constexpr int32 LtColourColumns = 6;
	constexpr float LtChip = 44.0f;
	constexpr float LtChipGap = 8.0f;
	constexpr float LtChipTop = 676.0f;
	constexpr float LtRightLeft = 862.0f;
	constexpr float LtRightWidth = 676.0f;
	constexpr float LtSettingTop = 110.0f;
	constexpr float LtSettingHeight = 46.0f;
	constexpr float LtSettingGap = 8.0f;
	constexpr int32 LtSettings = 3;
	constexpr int32 LtPeopleColumns = 4;
	constexpr float LtPeopleTop = 322.0f;
	constexpr float LtCardWidth = 164.0f;
	constexpr float LtCardHeight = 52.0f;
	constexpr float LtCardGap = 6.0f;
	constexpr float LtButtonTop = 808.0f;
	constexpr int32 LtButtons = 4;

	FSlateBrush LtPictureBrush(AChaosImpactCharacterPreview* Stage)
	{
		FSlateBrush Brush;
		if (Stage && Stage->GetPicture())
		{
			Brush.SetResourceObject(Stage->GetPicture());
		}
		Brush.ImageSize = FVector2D(AChaosImpactCharacterPreview::PictureWidth, AChaosImpactCharacterPreview::PictureHeight);
		Brush.DrawAs = ESlateBrushDrawType::Image;
		return Brush;
	}

	void LtPicture(const FPainter& P, const FSlateBrush& Brush, const FBox2D& R)
	{
		if (!Brush.GetResourceObject())
		{
			return;
		}
		FSlateDrawElement::MakeBox(P.Elements, P.Layer,
			P.Geometry.ToPaintGeometry(FVector2f(R.GetSize()), FSlateLayoutTransform(FVector2f(R.Min))),
			&Brush, ESlateDrawEffect::None, WithAlpha(FLinearColor::White, P.Alpha));
	}

	/** A character's face out of its icon picture (as the colour editor crops it). */
	FSlateBrush LtFace(const FSlateBrush& Picture, const int32 Character)
	{
		FSlateBrush Face = Picture;
		const EChaosImpactModelKind Kind = ChaosImpactRoster::Get(Character).Kind;
		const bool bWhole = Kind == EChaosImpactModelKind::Authored || Kind == EChaosImpactModelKind::Handstand;
		Face.SetUVRegion(bWhole ? FBox2f(FVector2f(0.03f, 0.2f), FVector2f(0.97f, 0.8f))
			: Character == ChaosImpactRoster::CapIndex ? FBox2f(FVector2f(0.14f, 0.06f), FVector2f(0.86f, 0.62f))
			: FBox2f(FVector2f(0.2f, 0.04f), FVector2f(0.8f, 0.5f)));
		return Face;
	}

	void LtFrame(const FPainter& P, const FBox2D& R, const FLinearColor& Color, const float Width)
	{
		P.Outline(R.Min.X, R.Min.Y, R.GetSize().X, R.GetSize().Y, Color, Width);
	}

	FBox2D LtGrow(const FBox2D& R, const float By)
	{
		return FBox2D(R.Min - FVector2D(By), R.Max + FVector2D(By));
	}

	bool LtIsAny(const FKey& Key, std::initializer_list<FKey> Keys)
	{
		for (const FKey& Each : Keys)
		{
			if (Key == Each)
			{
				return true;
			}
		}
		return false;
	}

	/** A card's body as the other screens draw them: a shadow, a night-blue body shading down, a faint rim. */
	void LtCard(const FPainter& P, const FBox2D& R, const FLinearColor& Accent, const float Lit)
	{
		const float W = R.GetSize().X;
		const float H = R.GetSize().Y;
		P.Box(R.Min.X + 6.0f, R.Min.Y + 7.0f, W, H, FLinearColor(0.0f, 0.0f, 0.0f, 0.4f));
		FLinearColor Top = FMath::Lerp(FLinearColor(0.05f, 0.07f, 0.13f, 0.96f), Accent * 0.4f, Lit * 0.6f);
		Top.A = 0.96f;
		HudPoly(P, {R.Min, FVector2D(R.Max.X, R.Min.Y), R.Max, FVector2D(R.Min.X, R.Max.Y)},
			MakeFill(Top, FLinearColor(0.008f, 0.012f, 0.026f, 0.97f)));
		HudAbove(P, 1).Outline(R.Min.X, R.Min.Y, W, H, WithAlpha(Paper, 0.1f + 0.2f * Lit), 1.0f);
	}
}

// ---------------------------------------------------------------------------------------------------- The registration

UChaosImpactLocalTourneySubsystem* UChaosImpactLocalTourneySubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UChaosImpactLocalTourneySubsystem>() : nullptr;
}

int32 UChaosImpactLocalTourneySubsystem::GetMaxPeople() const
{
	return FMath::Max(0, FMath::Min(IsAllAtOnce() ? MaxPeopleAllAtOnce : MaxPeopleOneAtATime, ChaosImpactTournament::MaxEntrants - CPUs));
}

FString UChaosImpactLocalTourneySubsystem::GetStartBlocker() const
{
	if (People.IsEmpty())
	{
		return TEXT("参加者を1人以上登録してください");
	}
	if (IsAllAtOnce() && People.Num() > MaxPeopleAllAtOnce)
	{
		return FString::Printf(TEXT("同時進行で遊べるのは%d人までです"), MaxPeopleAllAtOnce);
	}
	if (People.Num() + CPUs < ChaosImpactTournament::MinEntrants)
	{
		return TEXT("CPUを入れるか、参加者を2人以上にしてください");
	}
	if (People.Num() + CPUs > ChaosImpactTournament::MaxEntrants)
	{
		return FString::Printf(TEXT("参加者とCPUは合わせて%d人までです"), ChaosImpactTournament::MaxEntrants);
	}
	return FString();
}

int32 UChaosImpactLocalTourneySubsystem::AddPerson()
{
	if (People.Num() >= GetMaxPeople())
	{
		return INDEX_NONE;
	}
	FChaosImpactLocalTourneyPerson Person;
	const int32 Number = People.Num() + 1;
	// The first name free of プレイヤーN.
	for (int32 Try = Number; Try < Number + 64; ++Try)
	{
		const FString Name = FString::Printf(TEXT("プレイヤー%d"), Try);
		if (!People.ContainsByPredicate([&Name](const FChaosImpactLocalTourneyPerson& Each) { return Each.Name == Name; }))
		{
			Person.Name = Name;
			break;
		}
	}
	Person.Character = (Number - 1) % ChaosImpactRoster::Num();
	Person.Colour = (Number - 1) % ChaosImpactRoster::ColourCount;
	return People.Add(Person);
}

TArray<uint8> UChaosImpactLocalTourneySubsystem::GetCustomColours(const FChaosImpactLocalTourneyPerson& Person) const
{
	const UChaosImpactColourSubsystem* Colours = UChaosImpactColourSubsystem::Get(this);
	return Colours && Person.Preset >= 0 ? Colours->GetPresetParts(Person.Character, Person.Preset) : TArray<uint8>();
}

TArray<FChaosImpactTournamentEntrant> UChaosImpactLocalTourneySubsystem::MakeEntrants() const
{
	TArray<FChaosImpactTournamentEntrant> Entrants;
	for (const FChaosImpactLocalTourneyPerson& Person : People)
	{
		FChaosImpactTournamentEntrant Entrant;
		Entrant.Name = Person.Name;
		Entrant.Character = ChaosImpactRoster::ClampIndex(Person.Character);
		Entrant.Colour = FMath::Clamp(Person.Colour, 0, ChaosImpactRoster::ColourCount - 1);
		Entrant.CustomColours = GetCustomColours(Person);
		Entrants.Add(Entrant);
	}
	return Entrants;
}

FChaosImpactTournamentSettings UChaosImpactLocalTourneySubsystem::MakeSettings() const
{
	FChaosImpactTournamentSettings Settings;
	Settings.Mode = Mode;
	const int32 Count = FMath::Clamp(CPUs, 0, ChaosImpactTournament::MaxEntrants - People.Num());
	for (int32 Index = 0; Index < Count; ++Index)
	{
		FChaosImpactTournamentCPU CPU;
		CPU.Name = FString::Printf(TEXT("CPU%d"), Index + 1);
		CPU.Level = CPULevel;
		Settings.CPUs.Add(CPU);
	}
	return Settings;
}

FString UChaosImpactLocalTourneySubsystem::DescribeCPULevel(const int32 Level)
{
	return Level < 0 ? FString(TEXT("おまかせ")) : FString(ChaosImpactMatch::GetCPULevelName(Level));
}

// ---------------------------------------------------------------------------------------------------- The screen

UChaosImpactLocalTourneySubsystem* UChaosImpactTourneyEntry::GetData() const
{
	return UChaosImpactLocalTourneySubsystem::Get(Controller.Get());
}

FChaosImpactLocalTourneyPerson* UChaosImpactTourneyEntry::GetCurrent() const
{
	UChaosImpactLocalTourneySubsystem* Data = GetData();
	return Data && Data->People.IsValidIndex(Current) ? &Data->People[Current] : nullptr;
}

void UChaosImpactTourneyEntry::Open(AChaosImpactPlayerController* InController)
{
	Close();
	Controller = InController;
	UWorld* World = InController ? InController->GetWorld() : nullptr;
	UChaosImpactLocalTourneySubsystem* Data = GetData();
	if (!World || !Data)
	{
		return;
	}
	bOpen = true;
	OpenedAt = FPlatformTime::Seconds();
	// Someone to set up straight away.
	if (Data->People.IsEmpty())
	{
		Data->AddPerson();
	}
	Current = FMath::Clamp(Current, 0, Data->People.Num() - 1);
	Area = EArea::Characters;
	CharacterCursor = Data->People[Current].Character;
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Parameters.ObjectFlags |= RF_Transient;
	Preview = World->SpawnActor<AChaosImpactCharacterPreview>(AChaosImpactCharacterPreview::StaticClass(),
		FVector(0.0f, -132000.0f, 40000.0f), FRotator::ZeroRotator, Parameters);
	PreviewBrush = LtPictureBrush(Preview);
	PreviewShows.Reset();
	RefreshPreview();
	Portraits.Init(nullptr, ChaosImpactRoster::Num());
	PortraitBrushes.Init(FSlateBrush(), ChaosImpactRoster::Num());
	PortraitBornAt.Init(0.0, ChaosImpactRoster::Num());
	PortraitsMade = 0;
}

void UChaosImpactTourneyEntry::Close()
{
	if (IsValid(Preview))
	{
		Preview->Destroy();
	}
	Preview = nullptr;
	for (AChaosImpactCharacterPreview* Stage : Portraits)
	{
		if (IsValid(Stage))
		{
			Stage->Destroy();
		}
	}
	Portraits.Reset();
	PortraitBrushes.Reset();
	PortraitBornAt.Reset();
	Naming.Close();
	bOpen = false;
}

void UChaosImpactTourneyEntry::RefreshPreview()
{
	const FChaosImpactLocalTourneyPerson* Person = GetCurrent();
	const UChaosImpactLocalTourneySubsystem* Data = GetData();
	if (!Person || !Data || !IsValid(Preview))
	{
		return;
	}
	const FString Shows = FString::Printf(TEXT("%d/%d/%d"), Person->Character, Person->Colour, Person->Preset);
	if (Shows == PreviewShows)
	{
		return;
	}
	PreviewShows = Shows;
	Preview->ShowLoadout(Person->Character, Person->Colour, Ice);
	Preview->ShowCustomColours(Data->GetCustomColours(*Person));
	Preview->SetFacing(-18.0f, 0.0f);
}

void UChaosImpactTourneyEntry::Tick(const float DeltaSeconds)
{
	if (!bOpen)
	{
		return;
	}
	++Frame;
	RefreshPreview();
	if (IsValid(Preview))
	{
		Preview->Animate(FMath::Min(DeltaSeconds, 0.1f));
		Preview->FilmNow();
		PreviewBrush = LtPictureBrush(Preview);
	}
	// The icon faces, two more each frame; each filmed now and then for its first seconds, then held still.
	UWorld* World = Controller.IsValid() ? Controller->GetWorld() : nullptr;
	for (int32 Made = 0; Made < 2 && World && PortraitsMade < Portraits.Num(); ++Made, ++PortraitsMade)
	{
		FActorSpawnParameters Parameters;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Parameters.ObjectFlags |= RF_Transient;
		AChaosImpactCharacterPreview* Portrait = World->SpawnActor<AChaosImpactCharacterPreview>(AChaosImpactCharacterPreview::StaticClass(),
			FVector(PortraitsMade * 1600.0f, -128000.0f, 40000.0f), FRotator::ZeroRotator, Parameters);
		if (Portrait)
		{
			Portrait->ShowLoadout(PortraitsMade, 0, Paper);
			Portrait->UseAsPortrait();
		}
		Portraits[PortraitsMade] = Portrait;
		PortraitBrushes[PortraitsMade] = LtPictureBrush(Portrait);
		PortraitBornAt[PortraitsMade] = FPlatformTime::Seconds();
	}
	const double Now = FPlatformTime::Seconds();
	for (int32 Index = 0; Index < PortraitsMade; ++Index)
	{
		AChaosImpactCharacterPreview* Stage = Portraits[Index];
		if (IsValid(Stage) && Now - PortraitBornAt[Index] < 12.0 && (Frame + Index) % 6 == 0)
		{
			Stage->Animate(0.1f);
			Stage->FilmNow();
		}
	}
}

// ---- Where things are

FBox2D UChaosImpactTourneyEntry::CharacterRect(const int32 Index) const
{
	const FVector2D At(LtGridLeft + (Index % LtColumns) * (LtTile + LtTileGap), LtGridTop + (Index / LtColumns) * (LtTile + LtTileGap));
	return FBox2D(At, At + FVector2D(LtTile));
}

FBox2D UChaosImpactTourneyEntry::ColourRect(const int32 Choice) const
{
	const FVector2D At(LtPreviewLeft - 2.0f + (Choice % LtColourColumns) * (LtChip + LtChipGap), LtChipTop + (Choice / LtColourColumns) * (LtChip + LtChipGap));
	return FBox2D(At, At + FVector2D(LtChip));
}

FBox2D UChaosImpactTourneyEntry::SettingRect(const int32 Row) const
{
	const FVector2D At(LtRightLeft, LtSettingTop + Row * (LtSettingHeight + LtSettingGap));
	return FBox2D(At, At + FVector2D(LtRightWidth, LtSettingHeight));
}

FBox2D UChaosImpactTourneyEntry::PersonRect(const int32 Index) const
{
	const FVector2D At(LtRightLeft + (Index % LtPeopleColumns) * (LtCardWidth + LtCardGap), LtPeopleTop + (Index / LtPeopleColumns) * (LtCardHeight + LtCardGap));
	return FBox2D(At, At + FVector2D(LtCardWidth, LtCardHeight));
}

FBox2D UChaosImpactTourneyEntry::ButtonRect(const int32 Button) const
{
	static const float Lefts[LtButtons] = {60.0f, 480.0f, 664.0f, 1238.0f};
	static const float Widths[LtButtons] = {240.0f, 172.0f, 172.0f, 300.0f};
	return FBox2D(FVector2D(Lefts[Button], LtButtonTop), FVector2D(Lefts[Button] + Widths[Button], LtButtonTop + 54.0f));
}

int32 UChaosImpactTourneyEntry::GetPeopleCards() const
{
	// Everyone, then a card to add one more while there is room.
	const UChaosImpactLocalTourneySubsystem* Data = GetData();
	return Data ? Data->People.Num() + (Data->People.Num() < Data->GetMaxPeople() ? 1 : 0) : 0;
}

int32 UChaosImpactTourneyEntry::GetColourChoiceCount() const
{
	const FChaosImpactLocalTourneyPerson* Person = GetCurrent();
	const UChaosImpactColourSubsystem* Colours = UChaosImpactColourSubsystem::Get(Controller.Get());
	return ChaosImpactRoster::ColourCount + (Person && Colours ? Colours->GetPresets(Person->Character).Num() : 0);
}

int32 UChaosImpactTourneyEntry::GetCurrentColourChoice() const
{
	const FChaosImpactLocalTourneyPerson* Person = GetCurrent();
	return !Person ? 0 : Person->Preset >= 0 ? ChaosImpactRoster::ColourCount + Person->Preset : Person->Colour;
}

// ---- Working it

void UChaosImpactTourneyEntry::Notify(const FString& InMessage)
{
	Message = InMessage;
	MessageAt = FPlatformTime::Seconds();
}

void UChaosImpactTourneyEntry::SelectPerson(const int32 Index)
{
	const UChaosImpactLocalTourneySubsystem* Data = GetData();
	if (!Data || !Data->People.IsValidIndex(Index))
	{
		return;
	}
	Current = Index;
	PersonCursor = Index;
	CharacterCursor = Data->People[Index].Character;
	ColourCursor = GetCurrentColourChoice();
}

void UChaosImpactTourneyEntry::RemoveCurrent()
{
	UChaosImpactLocalTourneySubsystem* Data = GetData();
	if (!Data || !Data->People.IsValidIndex(Current))
	{
		return;
	}
	if (Data->People.Num() <= 1)
	{
		Notify(TEXT("参加者は1人以上必要です"));
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiDeny);
		return;
	}
	Data->People.RemoveAt(Current);
	SelectPerson(FMath::Clamp(Current, 0, Data->People.Num() - 1));
	ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiBack);
}

void UChaosImpactTourneyEntry::ChangeSetting(const int32 Row, const int32 Direction)
{
	UChaosImpactLocalTourneySubsystem* Data = GetData();
	if (!Data)
	{
		return;
	}
	if (Row == 0)
	{
		Data->Mode = Data->IsAllAtOnce() ? EChaosImpactTournamentMode::OneAtATime : EChaosImpactTournamentMode::Simultaneous;
		if (Data->IsAllAtOnce() && Data->People.Num() > UChaosImpactLocalTourneySubsystem::MaxPeopleAllAtOnce)
		{
			Notify(FString::Printf(TEXT("同時進行は%d人まで：参加者を減らすか1試合ずつにしてください"), UChaosImpactLocalTourneySubsystem::MaxPeopleAllAtOnce));
		}
	}
	else if (Row == 1)
	{
		const int32 Room = FMath::Max(0, ChaosImpactTournament::MaxEntrants - Data->People.Num());
		Data->CPUs = (Data->CPUs + Direction + Room + 1) % (Room + 1);
	}
	else if (Row == 2 && Data->CPUs > 0)
	{
		// おまかせ → よわい → … → さいきょう.
		const int32 Count = ChaosImpactMatch::CPULevelCount + 1;
		Data->CPULevel = ((Data->CPULevel + 1 + Direction) % Count + Count) % Count - 1;
	}
	ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiValue);
}

void UChaosImpactTourneyEntry::OpenNaming()
{
	if (const FChaosImpactLocalTourneyPerson* Person = GetCurrent())
	{
		Naming.Open(Person->Name, TEXT("参加者の名前"));
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiOpen);
	}
}

void UChaosImpactTourneyEntry::FinishNaming()
{
	FString Name;
	if (Naming.ConsumeResult(Name) != FChaosImpactNameEntry::EResult::Done)
	{
		return;
	}
	Name = Name.TrimStartAndEnd().Left(UChaosImpactLocalTourneySubsystem::MaxNameLength);
	if (FChaosImpactLocalTourneyPerson* Person = GetCurrent(); Person && !Name.IsEmpty())
	{
		Person->Name = Name;
	}
}

void UChaosImpactTourneyEntry::Move(const int32 Columns, const int32 Rows)
{
	const UChaosImpactLocalTourneySubsystem* Data = GetData();
	if (!Data)
	{
		return;
	}
	const int32 Characters = ChaosImpactRoster::Num();
	switch (Area)
	{
	case EArea::Characters:
	{
		const int32 Column = CharacterCursor % LtColumns;
		if (Columns > 0 && (Column == LtColumns - 1 || CharacterCursor + 1 >= Characters))
		{
			Area = EArea::Colours;
			ColourCursor = GetCurrentColourChoice();
		}
		else if (Rows > 0 && CharacterCursor + LtColumns >= Characters)
		{
			Area = EArea::Buttons;
			ButtonCursor = 0;
		}
		else
		{
			CharacterCursor = FMath::Clamp(CharacterCursor + Columns + Rows * LtColumns, 0, Characters - 1);
		}
		break;
	}
	case EArea::Colours:
	{
		const int32 Count = GetColourChoiceCount();
		const int32 Column = ColourCursor % LtColourColumns;
		if (Columns < 0 && Column == 0)
		{
			Area = EArea::Characters;
		}
		else if (Columns > 0 && (Column == LtColourColumns - 1 || ColourCursor + 1 >= Count))
		{
			Area = EArea::People;
			PersonCursor = FMath::Clamp(Current, 0, GetPeopleCards() - 1);
		}
		else if (Rows > 0 && ColourCursor + LtColourColumns >= Count)
		{
			Area = EArea::Buttons;
			ButtonCursor = 1;
		}
		else if (Rows < 0 && ColourCursor < LtColourColumns)
		{
			// (The model is above: nothing to go to.)
		}
		else
		{
			ColourCursor = FMath::Clamp(ColourCursor + Columns + Rows * LtColourColumns, 0, Count - 1);
		}
		break;
	}
	case EArea::Settings:
		if (Columns != 0)
		{
			ChangeSetting(SettingCursor, Columns);
		}
		else if (Rows > 0 && SettingCursor == LtSettings - 1)
		{
			Area = EArea::People;
			PersonCursor = FMath::Min(PersonCursor, GetPeopleCards() - 1);
		}
		else
		{
			SettingCursor = FMath::Clamp(SettingCursor + Rows, 0, LtSettings - 1);
		}
		break;
	case EArea::People:
	{
		const int32 Cards = GetPeopleCards();
		const int32 Column = PersonCursor % LtPeopleColumns;
		if (Columns < 0 && Column == 0)
		{
			Area = EArea::Colours;
		}
		else if (Rows < 0 && PersonCursor < LtPeopleColumns)
		{
			Area = EArea::Settings;
			SettingCursor = LtSettings - 1;
		}
		else if (Rows > 0 && PersonCursor + LtPeopleColumns >= Cards)
		{
			Area = EArea::Buttons;
			ButtonCursor = 3;
		}
		else
		{
			PersonCursor = FMath::Clamp(PersonCursor + Columns + Rows * LtPeopleColumns, 0, Cards - 1);
		}
		break;
	}
	case EArea::Buttons:
		if (Columns != 0)
		{
			ButtonCursor = (ButtonCursor + Columns + LtButtons) % LtButtons;
		}
		else if (Rows < 0)
		{
			Area = ButtonCursor == 0 ? EArea::Characters : ButtonCursor == 3 ? EArea::People : EArea::Colours;
		}
		break;
	}
	ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiMove);
}

void UChaosImpactTourneyEntry::Confirm()
{
	UChaosImpactLocalTourneySubsystem* Data = GetData();
	FChaosImpactLocalTourneyPerson* Person = GetCurrent();
	if (!Data)
	{
		return;
	}
	switch (Area)
	{
	case EArea::Characters:
		if (Person)
		{
			// A preset belongs to its character: a new character starts on its ordinary colour.
			if (Person->Character != CharacterCursor)
			{
				Person->Preset = INDEX_NONE;
			}
			Person->Character = ChaosImpactRoster::ClampIndex(CharacterCursor);
			ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiSelect);
		}
		break;
	case EArea::Colours:
		if (Person)
		{
			if (ColourCursor < ChaosImpactRoster::ColourCount)
			{
				Person->Colour = ColourCursor;
				Person->Preset = INDEX_NONE;
			}
			else
			{
				Person->Preset = ColourCursor - ChaosImpactRoster::ColourCount;
			}
			ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiSelect);
		}
		break;
	case EArea::Settings:
		ChangeSetting(SettingCursor, 1);
		break;
	case EArea::People:
		if (Data->People.IsValidIndex(PersonCursor))
		{
			SelectPerson(PersonCursor);
			Area = EArea::Characters;
			ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiSelect);
		}
		else
		{
			// The card to add one: the new person is set up next.
			const int32 Added = Data->AddPerson();
			if (Added == INDEX_NONE)
			{
				Notify(TEXT("これ以上は追加できません"));
				ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiDeny);
				break;
			}
			SelectPerson(Added);
			Area = EArea::Characters;
			ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiConfirm);
		}
		break;
	case EArea::Buttons:
		if (ButtonCursor == 0)
		{
			Back();
		}
		else if (ButtonCursor == 1)
		{
			OpenNaming();
		}
		else if (ButtonCursor == 2)
		{
			RemoveCurrent();
		}
		else if (AChaosImpactPlayerController* Owner = Controller.Get())
		{
			const FString Blocker = Data->GetStartBlocker();
			if (!Blocker.IsEmpty())
			{
				Notify(Blocker);
				ChaosImpactSfx::Play2D(Owner, EChaosImpactSfx::UiDeny);
				break;
			}
			ChaosImpactSfx::Play2D(Owner, EChaosImpactSfx::UiStart);
			Owner->StartLocalTourney();
		}
		break;
	}
}

void UChaosImpactTourneyEntry::Back()
{
	if (Area != EArea::Characters)
	{
		Area = EArea::Characters;
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiBack);
		return;
	}
	ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiBack);
	if (AChaosImpactPlayerController* Owner = Controller.Get())
	{
		Owner->ShowMenuScreen(EChaosImpactScreen::VSSelect);
	}
}

bool UChaosImpactTourneyEntry::HandleKeyDown(const FKeyEvent& Event)
{
	const FKey Key = Event.GetKey();
	if (!bOpen || Key.IsMouseButton())
	{
		return false;
	}
	const FKey MenuKey = ChaosImpactSettings::ToMenuKey(Key, Event.GetInputDeviceId().GetId());
	const bool bRepeat = Event.IsRepeat();
	if (Naming.IsOpen())
	{
		Naming.HandleKey(MenuKey, bRepeat);
		FinishNaming();
		return true;
	}
	UChaosImpactLocalTourneySubsystem* Data = GetData();
	if (LtIsAny(MenuKey, {EKeys::Gamepad_DPad_Up, EKeys::Up, EKeys::W})) { Move(0, -1); }
	else if (LtIsAny(MenuKey, {EKeys::Gamepad_DPad_Down, EKeys::Down, EKeys::S})) { Move(0, 1); }
	else if (LtIsAny(MenuKey, {EKeys::Gamepad_DPad_Left, EKeys::Left, EKeys::A})) { Move(-1, 0); }
	else if (LtIsAny(MenuKey, {EKeys::Gamepad_DPad_Right, EKeys::Right, EKeys::D})) { Move(1, 0); }
	else if (!bRepeat && LtIsAny(MenuKey, {EKeys::Gamepad_FaceButton_Bottom, EKeys::Enter, EKeys::SpaceBar})) { Confirm(); }
	else if (!bRepeat && LtIsAny(MenuKey, {EKeys::Gamepad_FaceButton_Right, EKeys::Escape, EKeys::BackSpace})) { Back(); }
	else if (!bRepeat && LtIsAny(MenuKey, {EKeys::Gamepad_Special_Left, EKeys::N})) { OpenNaming(); }
	else if (!bRepeat && LtIsAny(MenuKey, {EKeys::Gamepad_FaceButton_Top, EKeys::Delete})) { RemoveCurrent(); }
	else if (!bRepeat && LtIsAny(MenuKey, {EKeys::Gamepad_FaceButton_Left, EKeys::Insert, EKeys::Add, EKeys::Equals}) && Data)
	{
		const int32 Added = Data->AddPerson();
		if (Added != INDEX_NONE)
		{
			SelectPerson(Added);
			Area = EArea::Characters;
			ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiConfirm);
		}
		else
		{
			Notify(TEXT("これ以上は追加できません"));
			ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiDeny);
		}
	}
	else if (LtIsAny(MenuKey, {EKeys::Gamepad_LeftShoulder, EKeys::Q}) && Data && !Data->People.IsEmpty())
	{
		SelectPerson((Current - 1 + Data->People.Num()) % Data->People.Num());
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiMove);
	}
	else if (LtIsAny(MenuKey, {EKeys::Gamepad_RightShoulder, EKeys::E}) && Data && !Data->People.IsEmpty())
	{
		SelectPerson((Current + 1) % Data->People.Num());
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiMove);
	}
	else if (!bRepeat && (MenuKey == EKeys::Tab || MenuKey == EKeys::Gamepad_RightTrigger || MenuKey == EKeys::Gamepad_LeftTrigger))
	{
		const int32 Step = MenuKey == EKeys::Gamepad_LeftTrigger || Event.IsShiftDown() ? -1 : 1;
		Area = static_cast<EArea>((static_cast<int32>(Area) + Step + 5) % 5);
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiMove);
	}
	else if (!bRepeat && MenuKey == EKeys::Gamepad_Special_Right)
	{
		// START: to the start button (pressing it again starts).
		if (Area == EArea::Buttons && ButtonCursor == 3)
		{
			Confirm();
		}
		Area = EArea::Buttons;
		ButtonCursor = 3;
	}
	return true;
}

bool UChaosImpactTourneyEntry::HandleAnalog(const FAnalogInputEvent& Event)
{
	if (!bOpen)
	{
		return false;
	}
	const FKey Key = Event.GetKey();
	if (Naming.IsOpen())
	{
		return Naming.HandleAnalog(Key, Event.GetAnalogValue());
	}
	const bool bY = Key == EKeys::Gamepad_LeftY;
	if (!bY && Key != EKeys::Gamepad_LeftX)
	{
		return false;
	}
	const float Value = Event.GetAnalogValue();
	const int32 Axis = bY ? 1 : 0;
	const double Now = FPlatformTime::Seconds();
	if (FMath::Abs(Value) < 0.55f)
	{
		NextStickAt[Axis] = 0.0;
		return true;
	}
	if (Now >= NextStickAt[Axis])
	{
		NextStickAt[Axis] = Now + (NextStickAt[Axis] <= 0.0 ? 0.32 : 0.12);
		Move(bY ? 0 : (Value > 0.0f ? 1 : -1), bY ? (Value > 0.0f ? -1 : 1) : 0);
	}
	return true;
}

void UChaosImpactTourneyEntry::HandleMouseMove(const FVector2D& Point)
{
	if (!bOpen)
	{
		return;
	}
	if (Naming.IsOpen())
	{
		Naming.HandleMouseMove(Point);
		return;
	}
	for (int32 Index = 0; Index < ChaosImpactRoster::Num(); ++Index)
	{
		if (CharacterRect(Index).IsInside(Point)) { CharacterCursor = Index; Area = EArea::Characters; return; }
	}
	for (int32 Choice = 0; Choice < GetColourChoiceCount(); ++Choice)
	{
		if (ColourRect(Choice).IsInside(Point)) { ColourCursor = Choice; Area = EArea::Colours; return; }
	}
	for (int32 Row = 0; Row < LtSettings; ++Row)
	{
		if (SettingRect(Row).IsInside(Point)) { SettingCursor = Row; Area = EArea::Settings; return; }
	}
	for (int32 Index = 0; Index < GetPeopleCards(); ++Index)
	{
		if (PersonRect(Index).IsInside(Point)) { PersonCursor = Index; Area = EArea::People; return; }
	}
	for (int32 Button = 0; Button < LtButtons; ++Button)
	{
		if (ButtonRect(Button).IsInside(Point)) { ButtonCursor = Button; Area = EArea::Buttons; return; }
	}
}

bool UChaosImpactTourneyEntry::HandleClick(const FVector2D& Point)
{
	if (!bOpen)
	{
		return false;
	}
	if (Naming.IsOpen())
	{
		Naming.HandleClick(Point);
		FinishNaming();
		return true;
	}
	HandleMouseMove(Point);
	if (Area == EArea::Settings && SettingRect(SettingCursor).IsInside(Point))
	{
		// A setting's left half goes back a step, its right half on.
		const FBox2D R = SettingRect(SettingCursor);
		ChangeSetting(SettingCursor, Point.X < R.Min.X + R.GetSize().X * 0.66f ? -1 : 1);
		return true;
	}
	const bool bOnSomething = CharacterRect(CharacterCursor).IsInside(Point) || ColourRect(ColourCursor).IsInside(Point)
		|| PersonRect(PersonCursor).IsInside(Point) || ButtonRect(ButtonCursor).IsInside(Point);
	if (bOnSomething)
	{
		Confirm();
	}
	return true;
}

bool UChaosImpactTourneyEntry::HandleCharacter(const TCHAR Character)
{
	return bOpen && Naming.IsOpen() && Naming.HandleCharacter(Character) && (FinishNaming(), true);
}

// ---- Drawing it

int32 UChaosImpactTourneyEntry::Paint(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer) const
{
	const UChaosImpactLocalTourneySubsystem* Data = GetData();
	if (!bOpen || !Data)
	{
		return Layer;
	}
	const double Now = FPlatformTime::Seconds();
	const float Time = static_cast<float>(Now - OpenedAt);
	const float Pulse = 0.65f + 0.35f * FMath::Sin(Time * 6.0f);
	const FPainter Back{Design, Elements, Layer};
	const FPainter P{Design, Elements, Layer + 1};
	const FPainter Over{Design, Elements, Layer + 3};
	const FPainter Top{Design, Elements, Layer + 5};
	const FChaosImpactLocalTourneyPerson* Person = GetCurrent();

	// Ground: deep night, the middle lit behind the model, a gold band under the title.
	Back.Box(-400.0f, 0.0f, 2400.0f, 900.0f, FLinearColor(0.008f, 0.012f, 0.03f, 1.0f));
	HudPoly(Back, {FVector2D(LtPreviewLeft - 36.0f, 136.0f), FVector2D(LtPreviewLeft + LtPreviewWidth + 36.0f, 136.0f),
		FVector2D(LtPreviewLeft + LtPreviewWidth + 20.0f, 790.0f), FVector2D(LtPreviewLeft - 52.0f, 790.0f)},
		MakeFill(FLinearColor(0.03f, 0.06f, 0.15f, 1.0f), FLinearColor(0.01f, 0.015f, 0.04f, 1.0f)));
	Back.Box(-400.0f, 0.0f, 2400.0f, 4.0f, Gold);
	P.Text(TEXT("TOURNAMENT"), 62.0f, 16.0f, 16.0f, Gold, ETextAlign::Left, TEXT("BlackItalic"));
	HudText3D(P, TEXT("参加者とキャラをえらぶ"), 60.0f, 36.0f, 40.0f, Paper, ETextAlign::Left, TEXT("BlackItalic"), 3.0f);
	P.Text(TEXT("キャラ・カラー・名前はトーナメントが終わるまでそのまま"), 62.0f, 104.0f, 16.0f, Muted, ETextAlign::Left, TEXT("Bold"));

	// ---- The characters.
	for (int32 Index = 0; Index < ChaosImpactRoster::Num(); ++Index)
	{
		const FBox2D R = CharacterRect(Index);
		const bool bChosen = Person && Index == Person->Character;
		RoundedBox(Back, R.Min.X, R.Min.Y, LtTile, LtTile, 14.0f, bChosen ? FLinearColor(0.06f, 0.2f, 0.5f, 1.0f) : FLinearColor(0.03f, 0.07f, 0.18f, 1.0f));
		if (PortraitBrushes.IsValidIndex(Index) && PortraitBrushes[Index].GetResourceObject())
		{
			LtPicture(P, LtFace(PortraitBrushes[Index], Index), FBox2D(R.Min + FVector2D(5.0f, 4.0f), R.Max - FVector2D(5.0f, 22.0f)));
		}
		Over.Box(R.Min.X + 4.0f, R.Max.Y - 22.0f, LtTile - 8.0f, 18.0f, FLinearColor(0.005f, 0.01f, 0.025f, 0.9f));
		const FString Name = ChaosImpactRoster::Get(Index).Name;
		Over.Text(Name, R.GetCenter().X, R.Max.Y - 21.0f, HudFit(Name, 12.0f, LtTile - 12.0f, TEXT("Bold")), Paper, ETextAlign::Center, TEXT("Bold"));
		if (Area == EArea::Characters && Index == CharacterCursor)
		{
			LtFrame(Top, LtGrow(R, 3.0f), WithAlpha(Gold, Pulse), 4.0f);
		}
		else if (bChosen)
		{
			LtFrame(Top, LtGrow(R, 1.0f), WithAlpha(Ice, 0.85f), 2.0f);
		}
	}

	// ---- The person being set up: the model, their name and character, their colours.
	LtPicture(P, PreviewBrush, FBox2D(FVector2D(LtPreviewLeft, LtPreviewTop), FVector2D(LtPreviewLeft + LtPreviewWidth, LtPreviewTop + LtPreviewHeight)));
	if (Person)
	{
		const FString Number = FString::Printf(TEXT("%d人目"), Current + 1);
		const float TagW = HudMeasure(Number, 14.0f, TEXT("Black")) + 22.0f;
		RoundedBox(Over, LtPreviewLeft + 10.0f, LtPreviewTop + 8.0f, TagW, 26.0f, 13.0f, Ice);
		HudAbove(Over, 1).Text(Number, LtPreviewLeft + 10.0f + TagW * 0.5f, LtPreviewTop + 11.0f, 14.0f, Paper, ETextAlign::Center, TEXT("Black"));
		const float NameSize = HudFit(Person->Name, 38.0f, LtPreviewWidth - 20.0f);
		HudText3D(Over, Person->Name, LtPreviewLeft + LtPreviewWidth * 0.5f, 552.0f + (38.0f - NameSize) * 0.5f, NameSize, Paper, ETextAlign::Center,
			TEXT("Black"), 3.0f);
		const FString CharacterName = ChaosImpactRoster::Get(Person->Character).Name;
		Over.Text(CharacterName, LtPreviewLeft + LtPreviewWidth * 0.5f, 612.0f, HudFit(CharacterName, 20.0f, LtPreviewWidth - 20.0f, TEXT("Bold")),
			WithAlpha(Paper, 0.75f), ETextAlign::Center, TEXT("Bold"));
	}
	// The colours: the four ordinary ones, then this character's カスタムカラー.
	const UChaosImpactColourSubsystem* Colours = UChaosImpactColourSubsystem::Get(Controller.Get());
	static const TArray<FChaosImpactColourPreset> NoPresets;
	const TArray<FChaosImpactColourPreset>& Presets = Colours && Person ? Colours->GetPresets(Person->Character) : NoPresets;
	const int32 Chosen = GetCurrentColourChoice();
	for (int32 Choice = 0; Choice < GetColourChoiceCount(); ++Choice)
	{
		const FBox2D R = ColourRect(Choice);
		RoundedBox(P, R.Min.X, R.Min.Y, LtChip, LtChip, 10.0f, FLinearColor(0.02f, 0.03f, 0.07f, 1.0f));
		if (Choice < ChaosImpactRoster::ColourCount)
		{
			RoundedBox(Over, R.Min.X + 6.0f, R.Min.Y + 6.0f, LtChip - 12.0f, LtChip - 12.0f, 8.0f, ChaosImpactRoster::GetColourSwatch(Choice));
		}
		else
		{
			// A preset: its first colours in stripes, a star on it.
			const int32 PresetIndex = Choice - ChaosImpactRoster::ColourCount;
			const TArray<uint8> Parts = Colours && Person ? Colours->GetPresetParts(Person->Character, PresetIndex) : TArray<uint8>();
			const TArray<FLinearColor> PartColours = Person ? ChaosImpactCustomColour::ToPartColours(Person->Character, Parts) : TArray<FLinearColor>();
			for (int32 Stripe = 0; Stripe < 3; ++Stripe)
			{
				const FLinearColor Shade = PartColours.IsValidIndex(Stripe) ? PartColours[Stripe] : Muted;
				Over.Box(R.Min.X + 6.0f + Stripe * (LtChip - 12.0f) / 3.0f, R.Min.Y + 6.0f, (LtChip - 12.0f) / 3.0f, LtChip - 12.0f, WithAlpha(Shade, 1.0f));
			}
			HudAbove(Over, 1).Text(TEXT("★"), R.Max.X - 12.0f, R.Min.Y + 2.0f, 12.0f, Gold, ETextAlign::Center, TEXT("Black"));
		}
		if (Area == EArea::Colours && Choice == ColourCursor)
		{
			LtFrame(Top, LtGrow(R, 3.0f), WithAlpha(Gold, Pulse), 3.0f);
		}
		else if (Choice == Chosen)
		{
			LtFrame(Top, LtGrow(R, 1.0f), Paper, 2.0f);
		}
	}
	{
		const bool bOnPreset = Chosen >= ChaosImpactRoster::ColourCount && Presets.IsValidIndex(Chosen - ChaosImpactRoster::ColourCount);
		const FString ColourName = bOnPreset ? Presets[Chosen - ChaosImpactRoster::ColourCount].Name
			: FString(ChaosImpactRoster::ColourNames[FMath::Clamp(Chosen, 0, ChaosImpactRoster::ColourCount - 1)]);
		P.Text(FString::Printf(TEXT("カラー：%s"), *ColourName), LtPreviewLeft - 2.0f, LtChipTop - 24.0f,
			HudFit(ColourName, 16.0f, LtPreviewWidth - 80.0f, TEXT("Bold")), Muted, ETextAlign::Left, TEXT("Bold"));
	}

	// ---- How it runs.
	const FString SettingNames[LtSettings] = {TEXT("進め方"), TEXT("CPU"), TEXT("CPUの強さ")};
	const FString SettingValues[LtSettings] = {Data->IsAllAtOnce() ? TEXT("同時進行（4人まで）") : TEXT("1試合ずつ（32人まで）"),
		FString::Printf(TEXT("%d人"), Data->CPUs), Data->CPUs > 0 ? UChaosImpactLocalTourneySubsystem::DescribeCPULevel(Data->CPULevel) : FString(TEXT("CPUなし"))};
	for (int32 Row = 0; Row < LtSettings; ++Row)
	{
		const FBox2D R = SettingRect(Row);
		const bool bOn = Area == EArea::Settings && Row == SettingCursor;
		const bool bOff = Row == 2 && Data->CPUs <= 0;
		LtCard(P, R, Row == 0 ? Violet : Fire, bOn ? 1.0f : 0.0f);
		HudAbove(P, 1).Box(R.Min.X, R.Min.Y, 6.0f, R.GetSize().Y, bOff ? WithAlpha(Muted, 0.5f) : Row == 0 ? Violet : Fire);
		Over.Text(SettingNames[Row], R.Min.X + 22.0f, R.Min.Y + 11.0f, 19.0f, bOff ? Muted : Paper, ETextAlign::Left, TEXT("Black"));
		const float Mid = R.Min.X + 430.0f;
		const float ValueSize = HudFit(SettingValues[Row], 22.0f, 300.0f);
		Over.Text(SettingValues[Row], Mid, R.Min.Y + 10.0f + (22.0f - ValueSize) * 0.5f, ValueSize, bOff ? Muted : Paper, ETextAlign::Center, TEXT("Black"));
		if (!bOff)
		{
			const FLinearColor Arrow = bOn ? Gold : WithAlpha(Paper, 0.45f);
			const float Y = R.Min.Y + R.GetSize().Y * 0.5f;
			const float Nudge = bOn ? 3.0f * FMath::Sin(Time * 6.0f) : 0.0f;
			Over.Line(FVector2D(Mid - 172.0f - Nudge, Y - 9.0f), FVector2D(Mid - 182.0f - Nudge, Y), Arrow, 4.0f);
			Over.Line(FVector2D(Mid - 182.0f - Nudge, Y), FVector2D(Mid - 172.0f - Nudge, Y + 9.0f), Arrow, 4.0f);
			Over.Line(FVector2D(Mid + 172.0f + Nudge, Y - 9.0f), FVector2D(Mid + 182.0f + Nudge, Y), Arrow, 4.0f);
			Over.Line(FVector2D(Mid + 182.0f + Nudge, Y), FVector2D(Mid + 172.0f + Nudge, Y + 9.0f), Arrow, 4.0f);
		}
		if (bOn)
		{
			LtFrame(Top, LtGrow(R, 2.0f), WithAlpha(Gold, Pulse), 3.0f);
		}
	}

	// ---- The people: who takes part, as many as there are.
	{
		const int32 People = Data->People.Num();
		const FString Count = FString::Printf(TEXT("参加者 %d人　＋　CPU %d人　＝　%d人"), People, Data->CPUs, People + Data->CPUs);
		P.Text(Count, LtRightLeft, 284.0f, HudFit(Count, 20.0f, 470.0f), Paper, ETextAlign::Left, TEXT("Black"));
		const FString Room = FString::Printf(TEXT("あと%d人まで"), FMath::Max(0, Data->GetMaxPeople() - People));
		P.Text(Room, LtRightLeft + LtRightWidth, 288.0f, 15.0f, Muted, ETextAlign::Right, TEXT("Bold"));
	}
	for (int32 Index = 0; Index < GetPeopleCards(); ++Index)
	{
		const FBox2D R = PersonRect(Index);
		const bool bCursor = Area == EArea::People && Index == PersonCursor;
		if (!Data->People.IsValidIndex(Index))
		{
			// The card to add one more.
			RoundedBox(P, R.Min.X, R.Min.Y, LtCardWidth, LtCardHeight, 8.0f, FLinearColor(0.02f, 0.03f, 0.06f, 0.7f));
			for (float X = R.Min.X + 6.0f; X < R.Max.X - 6.0f; X += 14.0f)
			{
				Over.Box(X, R.Min.Y, 8.0f, 2.0f, WithAlpha(Paper, 0.3f));
				Over.Box(X, R.Max.Y - 2.0f, 8.0f, 2.0f, WithAlpha(Paper, 0.3f));
			}
			Over.Text(TEXT("＋ 追加"), R.GetCenter().X, R.Min.Y + 14.0f, 19.0f, bCursor ? Gold : WithAlpha(Paper, 0.75f), ETextAlign::Center, TEXT("Black"));
		}
		else
		{
			const FChaosImpactLocalTourneyPerson& Each = Data->People[Index];
			const bool bCurrent = Index == Current;
			LtCard(P, R, Ice, bCurrent ? 1.0f : 0.0f);
			HudAbove(P, 1).Box(R.Min.X, R.Min.Y, 5.0f, LtCardHeight, ChaosImpactRoster::GetColourSwatch(Each.Colour));
			// The character's face, the name, the character's name.
			if (PortraitBrushes.IsValidIndex(Each.Character) && PortraitBrushes[Each.Character].GetResourceObject())
			{
				LtPicture(HudAbove(P, 1), LtFace(PortraitBrushes[Each.Character], Each.Character), FBox2D(R.Min + FVector2D(9.0f, 4.0f),
					R.Min + FVector2D(9.0f + 44.0f, 48.0f)));
			}
			Over.Text(FString::Printf(TEXT("#%d"), Index + 1), R.Max.X - 7.0f, R.Max.Y - 19.0f, 11.0f, WithAlpha(Paper, 0.4f), ETextAlign::Right, TEXT("BlackItalic"));
			Over.Text(Each.Name, R.Min.X + 60.0f, R.Min.Y + 6.0f, HudFit(Each.Name, 17.0f, LtCardWidth - 70.0f), Paper, ETextAlign::Left, TEXT("Black"));
			const FString CharacterName = ChaosImpactRoster::Get(Each.Character).Name;
			Over.Text(CharacterName, R.Min.X + 60.0f, R.Min.Y + 29.0f, HudFit(CharacterName, 12.0f, LtCardWidth - 98.0f, TEXT("Bold")),
				WithAlpha(Paper, 0.6f), ETextAlign::Left, TEXT("Bold"));
			if (bCurrent)
			{
				LtFrame(Top, LtGrow(R, 1.0f), Ice, 2.0f);
			}
			if (Data->IsAllAtOnce() && Index >= UChaosImpactLocalTourneySubsystem::MaxPeopleAllAtOnce)
			{
				// Past 同時進行's four: greyed.
				Top.Box(R.Min.X, R.Min.Y, LtCardWidth, LtCardHeight, FLinearColor(0.0f, 0.0f, 0.0f, 0.55f));
			}
		}
		if (bCursor)
		{
			LtFrame(HudAbove(Top, 1), LtGrow(R, 3.0f), WithAlpha(Gold, Pulse), 3.0f);
		}
	}

	// ---- The buttons.
	const FString ButtonNames[LtButtons] = {TEXT("もどる"), TEXT("名前を変える"), TEXT("この人を消す"), TEXT("トーナメント開始")};
	const FLinearColor ButtonColors[LtButtons] = {Muted, Ice, Fire, Gold};
	const FString Blocker = Data->GetStartBlocker();
	for (int32 Button = 0; Button < LtButtons; ++Button)
	{
		const FBox2D R = ButtonRect(Button);
		const bool bOn = Area == EArea::Buttons && Button == ButtonCursor;
		const bool bStart = Button == 3;
		const bool bReady = bStart && Blocker.IsEmpty();
		if (bStart && bReady)
		{
			HudPoly(P, {R.Min, FVector2D(R.Max.X, R.Min.Y), R.Max, FVector2D(R.Min.X, R.Max.Y)}, MakeFill(FMath::Lerp(Gold, Paper, 0.3f), Gold));
		}
		else
		{
			LtCard(P, R, ButtonColors[Button], bOn ? 1.0f : 0.0f);
			HudAbove(P, 1).Box(R.Min.X, R.Min.Y, 6.0f, R.GetSize().Y, ButtonColors[Button]);
		}
		const float Size = HudFit(ButtonNames[Button], bStart ? 24.0f : 18.0f, R.GetSize().X - 30.0f);
		Over.Text(ButtonNames[Button], R.GetCenter().X + 3.0f, R.Min.Y + 27.0f - Size * 0.72f, Size, bReady ? Ink : bStart ? Muted : Paper,
			ETextAlign::Center, TEXT("Black"));
		if (bOn)
		{
			LtFrame(Top, LtGrow(R, 3.0f), WithAlpha(Paper, Pulse), 3.0f);
		}
	}

	// ---- A word (why it cannot start, …), and the keys.
	const bool bMessage = Now - MessageAt < 3.0;
	const FString Line = bMessage ? Message : !Blocker.IsEmpty() ? Blocker : FString(TEXT("A：決定　B：もどる　X：追加　Y：消す　SELECT / N：名前　LB RB：参加者を切りかえ　START：開始へ"));
	P.Text(Line, 800.0f, 872.0f, HudFit(Line, 16.0f, 1480.0f, TEXT("Bold")), bMessage || !Blocker.IsEmpty() ? Gold : Muted, ETextAlign::Center, TEXT("Bold"));

	if (Naming.IsOpen())
	{
		return Naming.Paint(Design, Elements, Layer + 8);
	}
	return Layer + 7;
}
