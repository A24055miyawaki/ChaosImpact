#include "ChaosImpactColourEdit.h"

#include "ChaosImpactCharacterPreview.h"
#include "ChaosImpactCharacterRoster.h"
#include "ChaosImpactCustomColour.h"
#include "ChaosImpactPaint.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactScreen.h"
#include "ChaosImpactSettings.h"
#include "ChaosImpactSfx.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
	using namespace ChaosImpactPaint;
	using namespace ChaosImpactCustomColour;
	using EArea = UChaosImpactColourEdit::EArea;

	// Layout in the 1600 x 900 design space (as the character select): icons left, the model in the middle with the
	// buttons under it, on the right the presets, the parts and the colour picker, the guide bar along the bottom.
	constexpr int32 GridColumns = 4;
	constexpr float Tile = 104.0f;
	constexpr float TileGap = 8.0f;
	constexpr float CeGridLeft = 70.0f;
	constexpr float CeGridTop = 150.0f;
	constexpr float PreviewLeft = 560.0f;
	constexpr float PreviewTop = 118.0f;
	constexpr float PreviewWidth = 420.0f;
	constexpr float PreviewHeight = 560.0f;
	constexpr float ButtonsTop = 702.0f;
	constexpr float RightLeft = 1010.0f;
	constexpr float RightWidth = 540.0f;
	constexpr float ChipTop = 150.0f;
	constexpr float ChipSize = 52.0f;
	constexpr float ChipGap = 8.0f;
	constexpr float PartsTop = 248.0f;
	constexpr float PartHeight = 38.0f;
	constexpr float PartGap = 5.0f;
	constexpr float PickerTop = 556.0f;
	// The palette.
	constexpr float Swatch = 33.0f;
	constexpr float SwatchGap = 5.0f;
	// The fine picker (Photoshop's colour wheel): a hue ring round a saturation x brightness square.
	constexpr float RingOuter = 114.0f;
	constexpr float RingWidth = 20.0f;
	constexpr float SquareHalf = 62.0f;
	constexpr float SlidersLeft = 1262.0f;
	constexpr float SliderWidth = 288.0f;
	constexpr float SliderHeight = 28.0f;
	constexpr float SliderGap = 6.0f;
	constexpr int32 Sliders = 6;
	const TCHAR* const SliderNames[Sliders] = {TEXT("H"), TEXT("S"), TEXT("B"), TEXT("R"), TEXT("G"), TEXT("B")};
	// The colours used last, then 元の色.
	constexpr float RecentTop = 800.0f;
	constexpr float RecentSize = 28.0f;
	constexpr float RecentGap = 4.0f;
	constexpr float CeBarTop = 842.0f;
	const TCHAR* const ButtonNames[] = {TEXT("保存"), TEXT("別に保存"), TEXT("名前"), TEXT("けす"), TEXT("もどる")};
	constexpr int32 Buttons = UE_ARRAY_COUNT(ButtonNames);
	/** A drag on the model turns it. */
	constexpr int32 DragModel = 100;

	bool CeIsAny(const FKey& Key, std::initializer_list<FKey> Keys)
	{
		for (const FKey& Candidate : Keys)
		{
			if (Key == Candidate)
			{
				return true;
			}
		}
		return false;
	}

	const FSlateBrush& CeRoundBrush(const float Radius)
	{
		static const FSlateRoundedBoxBrush Small(FLinearColor::White, 8.0f);
		static const FSlateRoundedBoxBrush Medium(FLinearColor::White, 16.0f);
		static const FSlateRoundedBoxBrush Large(FLinearColor::White, 28.0f);
		return Radius <= 10.0f ? Small : Radius <= 18.0f ? Medium : Large;
	}

	void RoundBox(const FPainter& P, const FBox2D& R, const float Radius, const FLinearColor& Color)
	{
		const FVector2D Size = R.GetSize();
		if (Size.X <= 0.0 || Size.Y <= 0.0 || P.Alpha * Color.A <= 0.001f)
		{
			return;
		}
		FSlateDrawElement::MakeBox(P.Elements, P.Layer,
			P.Geometry.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(R.Min))),
			&CeRoundBrush(Radius), ESlateDrawEffect::None, WithAlpha(Color, P.Alpha));
	}

	void DrawFrame(const FPainter& P, const FBox2D& R, const FLinearColor& Color, const float Width)
	{
		const FVector2D S = R.GetSize();
		P.Box(R.Min.X, R.Min.Y, S.X, Width, Color);
		P.Box(R.Min.X, R.Max.Y - Width, S.X, Width, Color);
		P.Box(R.Min.X, R.Min.Y, Width, S.Y, Color);
		P.Box(R.Max.X - Width, R.Min.Y, Width, S.Y, Color);
	}

	FBox2D Grow(const FBox2D& R, const float By)
	{
		return FBox2D(R.Min - FVector2D(By), R.Max + FVector2D(By));
	}

	/** Screen colours: the colours are sRGB values, drawn as such. */
	FLinearColor Shown(const FColor& Colour)
	{
		return FLinearColor(Colour);
	}

	/** A smooth run of colours across R (left to right, or top to bottom). */
	void Gradient(const FPainter& P, const FBox2D& R, const TArray<FLinearColor>& Colours, const bool bAcross = true)
	{
		const FVector2D Size = R.GetSize();
		if (Colours.Num() < 2 || Size.X <= 0.0 || Size.Y <= 0.0)
		{
			return;
		}
		TArray<FSlateGradientStop> Stops;
		for (int32 Index = 0; Index < Colours.Num(); ++Index)
		{
			const float T = Index / static_cast<float>(Colours.Num() - 1);
			Stops.Add(FSlateGradientStop(FVector2f(Size.X * T, Size.Y * T), WithAlpha(Colours[Index], P.Alpha * Colours[Index].A)));
		}
		FSlateDrawElement::MakeGradient(P.Elements, P.Layer,
			P.Geometry.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(R.Min))),
			MoveTemp(Stops), bAcross ? Orient_Vertical : Orient_Horizontal);
	}

	void PaintSwatch(const FPainter& P, const FBox2D& R, const bool bOn, const FColor& Colour)
	{
		if (bOn)
		{
			RoundBox(P, R, 8.0f, Shown(Colour));
			return;
		}
		// As it is: crossed out.
		RoundBox(P, R, 8.0f, FLinearColor(0.08f, 0.1f, 0.16f, 1.0f));
		P.Line(FVector2D(R.Min.X + 6.0f, R.Max.Y - 6.0f), FVector2D(R.Max.X - 6.0f, R.Min.Y + 6.0f), WithAlpha(Paper, 0.7f), 3.0f);
	}

	FSlateBrush CeMakePictureBrush(AChaosImpactCharacterPreview* Stage)
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

	void PaintPicture(const FPainter& P, const FSlateBrush& Brush, const FBox2D& R)
	{
		if (!Brush.GetResourceObject())
		{
			return;
		}
		FSlateDrawElement::MakeBox(P.Elements, P.Layer,
			P.Geometry.ToPaintGeometry(FVector2f(R.GetSize()), FSlateLayoutTransform(FVector2f(R.Min))),
			&Brush, ESlateDrawEffect::None, WithAlpha(FLinearColor::White, P.Alpha));
	}
}

void UChaosImpactColourEdit::Open(AChaosImpactPlayerController* InController)
{
	Close();
	Controller = InController;
	UWorld* World = InController ? InController->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}
	bOpen = true;
	OpenedAt = FPlatformTime::Seconds();
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Parameters.ObjectFlags |= RF_Transient;
	Preview = World->SpawnActor<AChaosImpactCharacterPreview>(AChaosImpactCharacterPreview::StaticClass(),
		FVector(0.0f, -110000.0f, 40000.0f), FRotator::ZeroRotator, Parameters);
	PreviewBrush = CeMakePictureBrush(Preview);
	Portraits.Init(nullptr, ChaosImpactRoster::Num());
	PortraitBrushes.Init(FSlateBrush(), ChaosImpactRoster::Num());
	PortraitBornAt.Init(0.0, ChaosImpactRoster::Num());
	PortraitsMade = 0;
	ViewYaw = -22.0f;
#if !UE_BUILD_SHIPPING
	// Development: -CIDevCharacter=<index> opens on that character, -CIColourEditFine on the fine picker, and
	// -CIColourEditYaw=<degrees> turns the model (screenshots of each one's parts from all round).
	FParse::Value(FCommandLine::Get(), TEXT("CIDevCharacter="), Character);
	bFine = FParse::Param(FCommandLine::Get(), TEXT("CIColourEditFine"));
	FParse::Value(FCommandLine::Get(), TEXT("CIColourEditYaw="), ViewYaw);
#endif
	SelectCharacter(Character);
	Area = EArea::Characters;
#if !UE_BUILD_SHIPPING
	// Development: -CIColourEditNaming opens on the name entry (a screenshot of it).
	if (FParse::Param(FCommandLine::Get(), TEXT("CIColourEditNaming")))
	{
		OpenNaming();
	}
#endif
}

void UChaosImpactColourEdit::Close()
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
	Dragging = INDEX_NONE;
	Naming.Close();
	bOpen = false;
}

int32 UChaosImpactColourEdit::PartCount() const
{
	return GetPartCount(Character);
}

int32 UChaosImpactColourEdit::PresetChips() const
{
	const UChaosImpactColourSubsystem* Colours = UChaosImpactColourSubsystem::Get(Controller.Get());
	const int32 Saved = Colours ? Colours->GetPresets(Character).Num() : 0;
	// Each saved one, then 新規 while there is room.
	return Saved + (Saved < MaxPresets ? 1 : 0);
}

int32 UChaosImpactColourEdit::RecentCount() const
{
	const UChaosImpactColourSubsystem* Colours = UChaosImpactColourSubsystem::Get(Controller.Get());
	return Colours ? Colours->GetRecent().Num() : 0;
}

FColor UChaosImpactColourEdit::RecentColour(const int32 Index) const
{
	const UChaosImpactColourSubsystem* Colours = UChaosImpactColourSubsystem::Get(Controller.Get());
	return Colours && Colours->GetRecent().IsValidIndex(Index) ? Colours->GetRecent()[Index] : FColor::White;
}

FColor UChaosImpactColourEdit::PartColour(const int32 Part, bool* bOutOn) const
{
	FColor Colour(200, 200, 200);
	const bool bOn = GetPart(Working, Part, Colour);
	if (bOutOn)
	{
		*bOutOn = bOn;
	}
	return Colour;
}

void UChaosImpactColourEdit::SelectCharacter(const int32 InCharacter)
{
	Character = ChaosImpactRoster::ClampIndex(InCharacter);
	CharacterCursor = Character;
	if (Preview)
	{
		Preview->ShowLoadout(Character, 0, Ice);
		Preview->SetFacing(ViewYaw, 0.0f);
	}
	const UChaosImpactColourSubsystem* Colours = UChaosImpactColourSubsystem::Get(Controller.Get());
	LoadPreset(Colours && Colours->GetPresets(Character).Num() > 0 ? 0 : INDEX_NONE);
	PartCursor = 0;
	SyncHSB();
#if !UE_BUILD_SHIPPING
	// Development (-CIColourTest): every part in its own strong colour (red, blue, yellow, green, purple, orange).
	if (FParse::Param(FCommandLine::Get(), TEXT("CIColourTest")))
	{
		const FColor Test[] = {FColor(255, 0, 0), FColor(0, 80, 255), FColor(255, 230, 0), FColor(0, 220, 40), FColor(170, 0, 255), FColor(255, 120, 0)};
		for (int32 Part = 0; Part < PartCount(); ++Part)
		{
			ChaosImpactCustomColour::SetPart(Working, Part, true, Test[Part % UE_ARRAY_COUNT(Test)]);
		}
		RefreshPreview();
	}
#endif
}

void UChaosImpactColourEdit::LoadPreset(const int32 Slot)
{
	const UChaosImpactColourSubsystem* Colours = UChaosImpactColourSubsystem::Get(Controller.Get());
	const TArray<uint8> Parts = Colours ? Colours->GetPresetParts(Character, Slot) : TArray<uint8>();
	Preset = Parts.IsEmpty() ? INDEX_NONE : Slot;
	Working = Clean(Character, Parts);
	ChipCursor = Preset == INDEX_NONE ? PresetChips() - 1 : Preset;
	bDirty = false;
	bLeaveArmed = false;
	RefreshPreview();
	SyncHSB();
}

void UChaosImpactColourEdit::SetPart(const int32 Part, const bool bOn, const FColor& Colour)
{
	if (Part < 0 || Part >= PartCount())
	{
		return;
	}
	bool bWasOn = false;
	const FColor Was = PartColour(Part, &bWasOn);
	if (bWasOn == bOn && (!bOn || Was == Colour))
	{
		return;
	}
	ChaosImpactCustomColour::SetPart(Working, Part, bOn, Colour);
	bDirty = true;
	bLeaveArmed = false;
	RefreshPreview();
}

void UChaosImpactColourEdit::SyncHSB()
{
	bool bOn = false;
	const FColor Colour = PartColour(PartCursor, &bOn);
	float Hue = 0.0f, Saturation = 0.0f, Brightness = 0.0f;
	ToHSV(Colour, Hue, Saturation, Brightness);
	// A grey has no hue and black no saturation either: what the picker showed stays.
	if (Saturation > 0.001f && Brightness > 0.001f)
	{
		EditHue = Hue;
	}
	if (Brightness > 0.001f)
	{
		EditSaturation = Saturation;
	}
	EditBrightness = Brightness;
}

void UChaosImpactColourEdit::ApplyHSB()
{
	SetPart(PartCursor, true, FromHSV(EditHue, EditSaturation, EditBrightness));
}

void UChaosImpactColourEdit::Remember(const FColor& Colour)
{
	if (UChaosImpactColourSubsystem* Colours = UChaosImpactColourSubsystem::Get(Controller.Get()))
	{
		Colours->AddRecent(Colour);
	}
}

void UChaosImpactColourEdit::EnterPicker(const EArea To)
{
	const bool bIn = Area == EArea::Palette || Area == EArea::Ring || Area == EArea::Square || Area == EArea::Sliders || Area == EArea::Recent;
	if (!bIn)
	{
		PickerBefore = Working;
		SyncHSB();
	}
	Area = To;
}

int32 UChaosImpactColourEdit::Accelerated()
{
	const double Now = FPlatformTime::Seconds();
	AdjustRun = Now - LastAdjustAt < 0.25 ? AdjustRun + 1 : 0;
	LastAdjustAt = Now;
	return AdjustRun > 24 ? 10 : AdjustRun > 8 ? 4 : 1;
}

void UChaosImpactColourEdit::AdjustSlider(const int32 Slider, const int32 Steps)
{
	// H in degrees, S and B in per cent, R G B one by one (all faster held down).
	if (Slider == 0)
	{
		EditHue = FMath::Fmod(EditHue + Steps + 360.0f, 360.0f);
		ApplyHSB();
		return;
	}
	if (Slider <= 2)
	{
		float& Value = Slider == 1 ? EditSaturation : EditBrightness;
		Value = FMath::Clamp(Value + Steps * 0.01f, 0.0f, 1.0f);
		ApplyHSB();
		return;
	}
	FColor Colour = PartColour(PartCursor);
	uint8& Channel = Slider == 3 ? Colour.R : Slider == 4 ? Colour.G : Colour.B;
	Channel = static_cast<uint8>(FMath::Clamp(Channel + Steps, 0, 255));
	SetPart(PartCursor, true, Colour);
	SyncHSB();
}

void UChaosImpactColourEdit::MoveSquare(const int32 Columns, const int32 Rows)
{
	// Saturation across, brightness up.
	const float Step = 0.01f * Accelerated();
	EditSaturation = FMath::Clamp(EditSaturation + Columns * Step, 0.0f, 1.0f);
	EditBrightness = FMath::Clamp(EditBrightness - Rows * Step, 0.0f, 1.0f);
	ApplyHSB();
}

FVector2D UChaosImpactColourEdit::WheelCentre() const
{
	return FVector2D(RightLeft + RingOuter + 4.0f, PickerTop + RingOuter + 4.0f);
}

FBox2D UChaosImpactColourEdit::SquareRect() const
{
	const FVector2D Centre = WheelCentre();
	return FBox2D(Centre - FVector2D(SquareHalf), Centre + FVector2D(SquareHalf));
}

bool UChaosImpactColourEdit::PickRing(const FVector2D& Point, const bool bAnywhere)
{
	const FVector2D Offset = Point - WheelCentre();
	const float Distance = static_cast<float>(Offset.Size());
	if (!bAnywhere && (Distance > RingOuter || Distance < RingOuter - RingWidth - 4.0f))
	{
		return false;
	}
	// Red to the right, round anticlockwise (as Photoshop's wheel); screen y runs down.
	EditHue = FMath::Fmod(FMath::RadiansToDegrees(static_cast<float>(FMath::Atan2(-Offset.Y, Offset.X))) + 360.0f, 360.0f);
	ApplyHSB();
	return true;
}

bool UChaosImpactColourEdit::PickSquare(const FVector2D& Point, const bool bAnywhere)
{
	const FBox2D R = SquareRect();
	if (!bAnywhere && !R.IsInside(Point))
	{
		return false;
	}
	EditSaturation = FMath::Clamp(static_cast<float>((Point.X - R.Min.X) / R.GetSize().X), 0.0f, 1.0f);
	EditBrightness = FMath::Clamp(static_cast<float>((R.Max.Y - Point.Y) / R.GetSize().Y), 0.0f, 1.0f);
	ApplyHSB();
	return true;
}

void UChaosImpactColourEdit::PickSlider(const int32 Slider, const FVector2D& Point)
{
	const FBox2D R = SliderRect(Slider);
	const float Track0 = R.Min.X + 30.0f, Track1 = R.Max.X - 52.0f;
	const float Amount = FMath::Clamp(static_cast<float>((Point.X - Track0) / (Track1 - Track0)), 0.0f, 1.0f);
	if (Slider == 0) { EditHue = Amount * 359.9f; ApplyHSB(); return; }
	if (Slider == 1) { EditSaturation = Amount; ApplyHSB(); return; }
	if (Slider == 2) { EditBrightness = Amount; ApplyHSB(); return; }
	FColor Colour = PartColour(PartCursor);
	(Slider == 3 ? Colour.R : Slider == 4 ? Colour.G : Colour.B) = static_cast<uint8>(FMath::RoundToInt(Amount * 255.0f));
	SetPart(PartCursor, true, Colour);
	SyncHSB();
}

void UChaosImpactColourEdit::CyclePicker(const int32 Direction)
{
	// The palette (or the ring, the square and the six values), then the colours used last; round again.
	TArray<TPair<EArea, int32>> Stops;
	if (bFine)
	{
		Stops.Add({EArea::Ring, 0});
		Stops.Add({EArea::Square, 0});
		for (int32 Slider = 0; Slider < Sliders; ++Slider)
		{
			Stops.Add({EArea::Sliders, Slider});
		}
	}
	else
	{
		Stops.Add({EArea::Palette, 0});
	}
	Stops.Add({EArea::Recent, 0});
	int32 At = INDEX_NONE;
	for (int32 Index = 0; Index < Stops.Num(); ++Index)
	{
		if (Stops[Index].Key == Area && (Area != EArea::Sliders || Stops[Index].Value == SliderCursor))
		{
			At = Index;
		}
	}
	At = At == INDEX_NONE ? 0 : (At + Direction + Stops.Num()) % Stops.Num();
	if (Stops[At].Key == EArea::Sliders)
	{
		SliderCursor = Stops[At].Value;
	}
	EnterPicker(Stops[At].Key);
	ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiMove);
}

void UChaosImpactColourEdit::ToggleFine()
{
	bFine = !bFine;
	const bool bIn = Area == EArea::Palette || Area == EArea::Ring || Area == EArea::Square || Area == EArea::Sliders;
	if (bIn)
	{
		Area = bFine ? EArea::Square : EArea::Palette;
	}
	SyncHSB();
	ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiValue);
}

void UChaosImpactColourEdit::RefreshPreview()
{
	if (Preview)
	{
		Preview->ShowCustomColours(Working);
	}
}

void UChaosImpactColourEdit::Notify(const FString& Text)
{
	NoticeText = Text;
	NoticeAt = FPlatformTime::Seconds();
}

void UChaosImpactColourEdit::OpenNaming()
{
	const UChaosImpactColourSubsystem* Colours = UChaosImpactColourSubsystem::Get(Controller.Get());
	const TArray<FChaosImpactColourPreset> NoPresets;
	const TArray<FChaosImpactColourPreset>& Presets = Colours ? Colours->GetPresets(Character) : NoPresets;
	Dragging = INDEX_NONE;
	Naming.Open(Presets.IsValidIndex(Preset) ? Presets[Preset].Name : FString(),
		FString::Printf(TEXT("%s のカラーの名前"), ChaosImpactRoster::Get(Character).Name));
	ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiSelect);
}

void UChaosImpactColourEdit::FinishNaming()
{
	FString Name;
	if (Naming.ConsumeResult(Name) != FChaosImpactNameEntry::EResult::Done)
	{
		return;
	}
	Name = Name.TrimStartAndEnd().Left(MaxNameLength);
	UChaosImpactColourSubsystem* Colours = UChaosImpactColourSubsystem::Get(Controller.Get());
	if (Name.IsEmpty() || !Colours)
	{
		return;
	}
	const TArray<FChaosImpactColourPreset>& Presets = Colours->GetPresets(Character);
	if (!Presets.IsValidIndex(Preset))
	{
		// Not saved yet: saved now, under that name.
		SavePreset(true, Name);
		return;
	}
	// Only the name changes (the preset's saved colours stay; colours being changed stay unsaved).
	FChaosImpactColourPreset Renamed = Presets[Preset];
	Renamed.Name = Name;
	Colours->SetPreset(Character, Preset, Renamed);
	ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiReady);
	Notify(FString::Printf(TEXT("名前を「%s」にしました"), *Name));
}

bool UChaosImpactColourEdit::HandleCharacter(const TCHAR Letter)
{
	if (!bOpen || !Naming.IsOpen())
	{
		return false;
	}
	const bool bUsed = Naming.HandleCharacter(Letter);
	FinishNaming();
	return bUsed;
}

void UChaosImpactColourEdit::SavePreset(const bool bAsNew, const FString& Name)
{
	UChaosImpactColourSubsystem* Colours = UChaosImpactColourSubsystem::Get(Controller.Get());
	if (!Colours)
	{
		return;
	}
	const TArray<FChaosImpactColourPreset>& Presets = Colours->GetPresets(Character);
	const bool bNew = bAsNew || !Presets.IsValidIndex(Preset);
	FChaosImpactColourPreset Saved;
	Saved.Parts = Working;
	Saved.Name = !Name.IsEmpty() ? Name : bNew ? FString::Printf(TEXT("カラー%d"), Presets.Num() + 1) : Presets[Preset].Name;
	const int32 Stored = Colours->SetPreset(Character, bNew ? INDEX_NONE : Preset, Saved);
	if (Stored == INDEX_NONE)
	{
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiDeny);
		Notify(FString::Printf(TEXT("プリセットは%dつまでです"), MaxPresets));
		return;
	}
	Preset = Stored;
	ChipCursor = Stored;
	bDirty = false;
	bLeaveArmed = false;
	ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiReady);
	Notify(FString::Printf(TEXT("「%s」に保存しました"), *Saved.Name));
}

void UChaosImpactColourEdit::ShiftPreset(const int32 Direction)
{
	const int32 Chips = PresetChips();
	if (Chips <= 1)
	{
		return;
	}
	ChipCursor = (ChipCursor + Direction + Chips) % Chips;
	LoadPreset(ChipCursor);
	ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiValue);
}

void UChaosImpactColourEdit::Move(const int32 Columns, const int32 Rows)
{
	const int32 Count = ChaosImpactRoster::Num();
	switch (Area)
	{
	case EArea::Characters:
	{
		const int32 Column = CharacterCursor % GridColumns;
		if (Columns > 0 && (Column == GridColumns - 1 || CharacterCursor == Count - 1))
		{
			Area = EArea::Presets;
			break;
		}
		const int32 Next = CharacterCursor + Columns + Rows * GridColumns;
		if (Next >= 0 && Next < Count && !(Columns < 0 && Column == 0))
		{
			SelectCharacter(Next);
		}
		break;
	}
	case EArea::Presets:
		if (Rows > 0)
		{
			Area = EArea::Parts;
		}
		else if (Columns < 0 && ChipCursor == 0)
		{
			Area = EArea::Characters;
		}
		else if (Columns != 0)
		{
			ShiftPreset(Columns);
			return;
		}
		break;
	case EArea::Parts:
		if (Columns != 0)
		{
			// Left/right: round the hue at once.
			SyncHSB();
			EditHue = FMath::Fmod(EditHue + Columns * 10.0f * Accelerated() + 360.0f, 360.0f);
			bool bOn = false;
			PartColour(PartCursor, &bOn);
			if (!bOn)
			{
				EditSaturation = 0.8f;
				EditBrightness = 0.9f;
			}
			ApplyHSB();
			ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiValue);
			return;
		}
		if (Rows < 0 && PartCursor == 0)
		{
			Area = EArea::Presets;
		}
		else if (Rows > 0 && PartCursor >= PartCount() - 1)
		{
			EnterPicker(bFine ? EArea::Ring : EArea::Palette);
		}
		else
		{
			PartCursor = FMath::Clamp(PartCursor + Rows, 0, PartCount() - 1);
			SyncHSB();
		}
		break;
	case EArea::Palette:
	{
		const int32 Column = SwatchCursor % PaletteColumns;
		const int32 Row = SwatchCursor / PaletteColumns;
		if (Rows < 0 && Row == 0) { Area = EArea::Parts; break; }
		if (Rows > 0 && Row == PaletteRows - 1) { Area = EArea::Recent; break; }
		if ((Columns < 0 && Column == 0) || (Columns > 0 && Column == PaletteColumns - 1)) { break; }
		SwatchCursor += Columns + Rows * PaletteColumns;
		// Tried on the model as the cursor goes.
		SetPart(PartCursor, true, GetPaletteSwatch(SwatchCursor % PaletteColumns, SwatchCursor / PaletteColumns));
		SyncHSB();
		break;
	}
	case EArea::Ring:
		if (Columns != 0)
		{
			EditHue = FMath::Fmod(EditHue - Columns * 2.0f * Accelerated() + 360.0f, 360.0f);
			ApplyHSB();
			return;
		}
		if (Rows < 0) { Area = EArea::Parts; }
		else { Area = EArea::Square; }
		break;
	case EArea::Square:
	{
		// Past an edge: on to what is that way (the ring above, the values to the right, the colours used last below).
		if (Rows < 0 && EditBrightness >= 1.0f) { Area = EArea::Ring; break; }
		if (Columns > 0 && EditSaturation >= 1.0f) { Area = EArea::Sliders; break; }
		if (Rows > 0 && EditBrightness <= 0.0f) { Area = EArea::Recent; break; }
		MoveSquare(Columns, Rows);
		return;
	}
	case EArea::Sliders:
		if (Columns != 0)
		{
			AdjustSlider(SliderCursor, Columns * Accelerated());
			return;
		}
		if (Rows < 0 && SliderCursor == 0) { Area = EArea::Ring; }
		else if (Rows > 0 && SliderCursor == Sliders - 1) { Area = EArea::Recent; }
		else { SliderCursor = FMath::Clamp(SliderCursor + Rows, 0, Sliders - 1); }
		break;
	case EArea::Recent:
		if (Rows < 0) { Area = bFine ? EArea::Square : EArea::Palette; }
		else if (Rows > 0) { Area = EArea::Buttons; }
		else
		{
			// The colours used last, then 元の色 at the end.
			RecentCursor = FMath::Clamp(RecentCursor + Columns, 0, RecentCount());
			if (RecentCursor < RecentCount())
			{
				SetPart(PartCursor, true, RecentColour(RecentCursor));
			}
			else
			{
				SetPart(PartCursor, false);
			}
			SyncHSB();
		}
		break;
	case EArea::Buttons:
		if (Rows < 0) { EnterPicker(EArea::Recent); }
		else { ButtonCursor = FMath::Clamp(ButtonCursor + Columns, 0, Buttons - 1); }
		break;
	}
	ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiMove);
}

void UChaosImpactColourEdit::Confirm()
{
	switch (Area)
	{
	case EArea::Characters:
		SelectCharacter(CharacterCursor);
		Area = EArea::Parts;
		break;
	case EArea::Presets:
		LoadPreset(ChipCursor);
		Area = EArea::Parts;
		break;
	case EArea::Parts:
		EnterPicker(bFine ? EArea::Square : EArea::Palette);
		break;
	case EArea::Palette:
	case EArea::Ring:
	case EArea::Square:
	case EArea::Sliders:
	case EArea::Recent:
	{
		if (Area == EArea::Palette)
		{
			SetPart(PartCursor, true, GetPaletteSwatch(SwatchCursor % PaletteColumns, SwatchCursor / PaletteColumns));
		}
		bool bOn = false;
		const FColor Kept = PartColour(PartCursor, &bOn);
		if (bOn)
		{
			Remember(Kept);
		}
		// Kept; on to the next part (the usual way of painting a whole model).
		PartCursor = FMath::Min(PartCursor + 1, PartCount() - 1);
		SyncHSB();
		Area = EArea::Parts;
		break;
	}
	case EArea::Buttons:
		if (ButtonCursor == 0 || ButtonCursor == 1)
		{
			SavePreset(ButtonCursor == 1);
			return;
		}
		if (ButtonCursor == 2)
		{
			OpenNaming();
			return;
		}
		if (ButtonCursor == 3)
		{
			UChaosImpactColourSubsystem* Colours = UChaosImpactColourSubsystem::Get(Controller.Get());
			if (Colours && Preset != INDEX_NONE)
			{
				Colours->RemovePreset(Character, Preset);
				Notify(TEXT("プリセットをけしました"));
				LoadPreset(INDEX_NONE);
				ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiBack);
			}
			else
			{
				ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiDeny);
			}
			return;
		}
		Area = EArea::Characters;
		Back();
		return;
	}
	ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiSelect);
}

void UChaosImpactColourEdit::Back()
{
	switch (Area)
	{
	case EArea::Palette:
	case EArea::Ring:
	case EArea::Square:
	case EArea::Sliders:
	case EArea::Recent:
		// The colours as they were before the picker was entered.
		Working = PickerBefore;
		RefreshPreview();
		SyncHSB();
		Area = EArea::Parts;
		break;
	case EArea::Parts:
	case EArea::Presets:
	case EArea::Buttons:
		Area = EArea::Characters;
		break;
	case EArea::Characters:
		if (bDirty && !bLeaveArmed)
		{
			bLeaveArmed = true;
			Notify(TEXT("保存していない色があります。もう一度でもどる"));
			ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiDeny);
			return;
		}
		if (AChaosImpactPlayerController* Owner = Controller.Get())
		{
			// Back to the menu it was opened from (the mode select, the online menu, a room's pause menu).
			Owner->CloseColourEdit();
		}
		break;
	}
	ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiBack);
}

bool UChaosImpactColourEdit::HandleKeyDown(const FKeyEvent& Event)
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
		// The name entry has every key while it is up.
		Naming.HandleKey(MenuKey, bRepeat);
		FinishNaming();
		return true;
	}
	if (!bRepeat && CeIsAny(MenuKey, {EKeys::Gamepad_Special_Left, EKeys::N})) { OpenNaming(); return true; }
	if (CeIsAny(MenuKey, {EKeys::Gamepad_DPad_Up, EKeys::Up, EKeys::W})) { Move(0, -1); }
	else if (CeIsAny(MenuKey, {EKeys::Gamepad_DPad_Down, EKeys::Down, EKeys::S})) { Move(0, 1); }
	else if (CeIsAny(MenuKey, {EKeys::Gamepad_DPad_Left, EKeys::Left, EKeys::A})) { Move(-1, 0); }
	else if (CeIsAny(MenuKey, {EKeys::Gamepad_DPad_Right, EKeys::Right, EKeys::D})) { Move(1, 0); }
	else if (!bRepeat && CeIsAny(MenuKey, {EKeys::Gamepad_FaceButton_Bottom, EKeys::Enter, EKeys::SpaceBar})) { Confirm(); }
	else if (!bRepeat && CeIsAny(MenuKey, {EKeys::Gamepad_FaceButton_Right, EKeys::Escape, EKeys::BackSpace})) { Back(); }
	else if (CeIsAny(MenuKey, {EKeys::Gamepad_LeftShoulder, EKeys::Q})) { ShiftPreset(-1); }
	else if (CeIsAny(MenuKey, {EKeys::Gamepad_RightShoulder, EKeys::E})) { ShiftPreset(1); }
	else if (!bRepeat && MenuKey == EKeys::Tab) { CyclePicker(Event.IsShiftDown() ? -1 : 1); }
	else if (!bRepeat && MenuKey == EKeys::Gamepad_RightTrigger) { CyclePicker(1); }
	else if (!bRepeat && MenuKey == EKeys::Gamepad_LeftTrigger) { CyclePicker(-1); }
	else if (!bRepeat && CeIsAny(MenuKey, {EKeys::Gamepad_FaceButton_Top, EKeys::T})) { ToggleFine(); }
	else if (!bRepeat && CeIsAny(MenuKey, {EKeys::Gamepad_FaceButton_Left, EKeys::F})) { SavePreset(false); }
	else if (!bRepeat && MenuKey == EKeys::R && Area != EArea::Characters && Area != EArea::Presets)
	{
		SetPart(PartCursor, false);
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiValue);
	}
	else if (CeIsAny(MenuKey, {EKeys::Z, EKeys::X}))
	{
		// The model turned round.
		ViewYaw += MenuKey == EKeys::Z ? -10.0f : 10.0f;
		if (Preview) { Preview->SetFacing(ViewYaw, 0.0f); }
	}
	return true;
}

bool UChaosImpactColourEdit::HandleAnalog(const FAnalogInputEvent& Event)
{
	const FKey Key = Event.GetKey();
	if (!bOpen)
	{
		return false;
	}
	if (Naming.IsOpen())
	{
		Naming.HandleAnalog(Key, Event.GetAnalogValue());
		return true;
	}
	if (Key == EKeys::Gamepad_RightX)
	{
		// The right stick turns the model (Tick, as long as it is held over).
		StickTurn = FMath::Abs(Event.GetAnalogValue()) > 0.25f ? Event.GetAnalogValue() : 0.0f;
		return true;
	}
	if (Key != EKeys::Gamepad_LeftX && Key != EKeys::Gamepad_LeftY)
	{
		return false;
	}
	const bool bY = Key == EKeys::Gamepad_LeftY;
	const float Value = Event.GetAnalogValue();
	bool& bHeld = bStickHeld[bY ? 1 : 0];
	if (FMath::Abs(Value) < 0.35f)
	{
		bHeld = false;
		return true;
	}
	// In the picker the values glide with the stick; elsewhere a step, repeating while held over.
	const bool bGlide = Area == EArea::Ring || Area == EArea::Square || Area == EArea::Sliders;
	if (FMath::Abs(Value) >= (bGlide ? 0.4f : 0.65f) && (!bHeld || FPlatformTime::Seconds() >= NextStickRepeatAt))
	{
		NextStickRepeatAt = FPlatformTime::Seconds() + (bGlide ? 0.03 : bHeld ? 0.09 : 0.35);
		bHeld = true;
		// Up on the stick is positive.
		Move(bY ? 0 : (Value > 0.0f ? 1 : -1), bY ? (Value > 0.0f ? -1 : 1) : 0);
	}
	return true;
}

FBox2D UChaosImpactColourEdit::TileRect(const int32 Index) const
{
	const FVector2D At(CeGridLeft + (Index % GridColumns) * (Tile + TileGap), CeGridTop + (Index / GridColumns) * (Tile + TileGap));
	return FBox2D(At, At + FVector2D(Tile));
}

FBox2D UChaosImpactColourEdit::ChipRect(const int32 Chip) const
{
	const FVector2D At(RightLeft + Chip * (ChipSize + ChipGap), ChipTop);
	return FBox2D(At, At + FVector2D(ChipSize, ChipSize + 12.0f));
}

FBox2D UChaosImpactColourEdit::PartRect(const int32 Part) const
{
	const FVector2D At(RightLeft, PartsTop + Part * (PartHeight + PartGap));
	return FBox2D(At, At + FVector2D(RightWidth, PartHeight));
}

FBox2D UChaosImpactColourEdit::TabRect(const bool bFineTab) const
{
	const FVector2D At(RightLeft + RightWidth - (bFineTab ? 130.0f : 264.0f), PickerTop - 40.0f);
	return FBox2D(At, At + FVector2D(126.0f, 32.0f));
}

FBox2D UChaosImpactColourEdit::SwatchRect(const int32 Index) const
{
	const FVector2D At(RightLeft + (Index % PaletteColumns) * (Swatch + SwatchGap), PickerTop + (Index / PaletteColumns) * (Swatch + SwatchGap));
	return FBox2D(At, At + FVector2D(Swatch));
}

FBox2D UChaosImpactColourEdit::SliderRect(const int32 Slider) const
{
	const FVector2D At(SlidersLeft, PickerTop + Slider * (SliderHeight + SliderGap));
	return FBox2D(At, At + FVector2D(SliderWidth, SliderHeight));
}

FBox2D UChaosImpactColourEdit::RecentRect(const int32 Index) const
{
	const FVector2D At(RightLeft + 56.0f + Index * (RecentSize + RecentGap), RecentTop + 1.0f);
	return FBox2D(At, At + FVector2D(RecentSize));
}

FBox2D UChaosImpactColourEdit::OriginalRect() const
{
	return FBox2D(FVector2D(RightLeft + RightWidth - 82.0f, RecentTop), FVector2D(RightLeft + RightWidth, RecentTop + 30.0f));
}

FBox2D UChaosImpactColourEdit::ButtonRect(const int32 Button) const
{
	const float Width = (PreviewWidth - (Buttons - 1) * 8.0f) / Buttons;
	const FVector2D At(PreviewLeft + Button * (Width + 8.0f), ButtonsTop);
	return FBox2D(At, At + FVector2D(Width, 54.0f));
}

void UChaosImpactColourEdit::HandleMouseMove(const FVector2D& Point, const bool bDraggingNow)
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
	if (!bDraggingNow)
	{
		if (Dragging != INDEX_NONE && Dragging != DragModel)
		{
			// Let go of the ring, the square or a slider: that colour into the recent ones.
			bool bOn = false;
			const FColor Kept = PartColour(PartCursor, &bOn);
			if (bOn) { Remember(Kept); }
		}
		Dragging = INDEX_NONE;
	}
	else if (Dragging == DragModel)
	{
		ViewYaw -= static_cast<float>(Point.X - LastMouse.X) * 0.6f;
		if (Preview) { Preview->SetFacing(ViewYaw, 0.0f); }
		LastMouse = Point;
		return;
	}
	else if (Dragging == 0) { PickRing(Point, true); return; }
	else if (Dragging == 1) { PickSquare(Point, true); return; }
	else if (Dragging >= 2) { PickSlider(Dragging - 2, Point); return; }
	for (int32 Index = 0; Index < ChaosImpactRoster::Num(); ++Index)
	{
		if (TileRect(Index).IsInside(Point)) { CharacterCursor = Index; Area = EArea::Characters; return; }
	}
	for (int32 Chip = 0; Chip < PresetChips(); ++Chip)
	{
		if (ChipRect(Chip).IsInside(Point)) { ChipCursor = Chip; Area = EArea::Presets; return; }
	}
	for (int32 Button = 0; Button < Buttons; ++Button)
	{
		if (ButtonRect(Button).IsInside(Point)) { ButtonCursor = Button; Area = EArea::Buttons; return; }
	}
}

bool UChaosImpactColourEdit::HandleClick(const FVector2D& Point)
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
	LastMouse = Point;
	for (int32 Index = 0; Index < ChaosImpactRoster::Num(); ++Index)
	{
		if (TileRect(Index).IsInside(Point)) { CharacterCursor = Index; Area = EArea::Characters; Confirm(); return true; }
	}
	if (FBox2D(FVector2D(PreviewLeft, PreviewTop), FVector2D(PreviewLeft + PreviewWidth, PreviewTop + PreviewHeight)).IsInside(Point))
	{
		// Dragged across: the model turns.
		Dragging = DragModel;
		return true;
	}
	for (int32 Chip = 0; Chip < PresetChips(); ++Chip)
	{
		if (ChipRect(Chip).IsInside(Point)) { ChipCursor = Chip; Area = EArea::Presets; Confirm(); return true; }
	}
	for (int32 Part = 0; Part < PartCount(); ++Part)
	{
		if (PartRect(Part).IsInside(Point))
		{
			// A part clicked: the one the picker paints.
			PartCursor = Part;
			Area = EArea::Parts;
			SyncHSB();
			ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiSelect);
			return true;
		}
	}
	for (const bool bFineTab : {false, true})
	{
		if (TabRect(bFineTab).IsInside(Point))
		{
			if (bFine != bFineTab) { ToggleFine(); }
			return true;
		}
	}
	if (bFine)
	{
		if (PickSquare(Point, false)) { EnterPicker(EArea::Square); Dragging = 1; return true; }
		if (PickRing(Point, false)) { EnterPicker(EArea::Ring); Dragging = 0; return true; }
		for (int32 Slider = 0; Slider < Sliders; ++Slider)
		{
			if (SliderRect(Slider).IsInside(Point))
			{
				SliderCursor = Slider;
				EnterPicker(EArea::Sliders);
				Dragging = 2 + Slider;
				PickSlider(Slider, Point);
				return true;
			}
		}
	}
	else
	{
		for (int32 Index = 0; Index < PaletteColumns * PaletteRows; ++Index)
		{
			if (SwatchRect(Index).IsInside(Point))
			{
				SwatchCursor = Index;
				EnterPicker(EArea::Palette);
				const FColor Colour = GetPaletteSwatch(Index % PaletteColumns, Index / PaletteColumns);
				SetPart(PartCursor, true, Colour);
				Remember(Colour);
				SyncHSB();
				ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiValue);
				return true;
			}
		}
	}
	for (int32 Index = 0; Index < RecentCount(); ++Index)
	{
		if (RecentRect(Index).IsInside(Point))
		{
			RecentCursor = Index;
			EnterPicker(EArea::Recent);
			SetPart(PartCursor, true, RecentColour(Index));
			SyncHSB();
			ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiValue);
			return true;
		}
	}
	if (OriginalRect().IsInside(Point))
	{
		SetPart(PartCursor, false);
		SyncHSB();
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiValue);
		return true;
	}
	for (int32 Button = 0; Button < Buttons; ++Button)
	{
		if (ButtonRect(Button).IsInside(Point)) { ButtonCursor = Button; Area = EArea::Buttons; Confirm(); return true; }
	}
	return true;
}

void UChaosImpactColourEdit::Tick(const float DeltaSeconds)
{
	if (!bOpen)
	{
		return;
	}
	const float Step = FMath::Min(DeltaSeconds, 0.1f);
	++Frame;
	if (StickTurn != 0.0f)
	{
		ViewYaw -= StickTurn * 200.0f * Step;
	}
	if (IsValid(Preview))
	{
		Preview->SetFacing(ViewYaw, 0.0f);
		Preview->Animate(Step);
		Preview->FilmNow();
		PreviewBrush = CeMakePictureBrush(Preview);
	}
	// The icon faces, two more each frame; each filmed now and then for its first seconds (until its textures are
	// in), then held still.
	UWorld* World = Controller.IsValid() ? Controller->GetWorld() : nullptr;
	for (int32 Made = 0; Made < 2 && World && PortraitsMade < Portraits.Num(); ++Made, ++PortraitsMade)
	{
		FActorSpawnParameters Parameters;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Parameters.ObjectFlags |= RF_Transient;
		AChaosImpactCharacterPreview* Portrait = World->SpawnActor<AChaosImpactCharacterPreview>(AChaosImpactCharacterPreview::StaticClass(),
			FVector(PortraitsMade * 1600.0f, -116000.0f, 40000.0f), FRotator::ZeroRotator, Parameters);
		if (Portrait)
		{
			Portrait->ShowLoadout(PortraitsMade, 0, Paper);
			Portrait->UseAsPortrait();
		}
		Portraits[PortraitsMade] = Portrait;
		PortraitBrushes[PortraitsMade] = CeMakePictureBrush(Portrait);
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

int32 UChaosImpactColourEdit::Paint(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer) const
{
	if (!bOpen)
	{
		return Layer;
	}
	const double Now = FPlatformTime::Seconds();
	const float Pulse = 0.65f + 0.35f * FMath::Sin(static_cast<float>(Now - OpenedAt) * 6.0f);
	const FLinearColor Card(0.03f, 0.07f, 0.18f, 1.0f);
	const FLinearColor CardOn(0.06f, 0.2f, 0.5f, 1.0f);
	const FPainter Back{Design, Elements, Layer};
	const FPainter P{Design, Elements, Layer + 1};
	const FPainter Over{Design, Elements, Layer + 2};
	const FPainter Top{Design, Elements, Layer + 3};
	Back.Box(0.0f, 0.0f, 1600.0f, 900.0f, FLinearColor(0.008f, 0.012f, 0.03f, 1.0f));
	Back.Box(PreviewLeft - 18.0f, 96.0f, PreviewWidth + 36.0f, 740.0f, FLinearColor(0.02f, 0.035f, 0.09f, 1.0f));
	P.Text(TEXT("カラーエディット"), 70.0f, 24.0f, 42.0f, Paper, ETextAlign::Left, TEXT("BlackItalic"));
	P.Text(TEXT("キャラクターを選んで、部位ごとに色をえらぼう"), 74.0f, 96.0f, 18.0f, Muted, ETextAlign::Left, TEXT("Bold"));

	// Characters.
	for (int32 Index = 0; Index < ChaosImpactRoster::Num(); ++Index)
	{
		const FBox2D R = TileRect(Index);
		RoundBox(Back, R, 16.0f, Index == Character ? CardOn : Card);
		if (PortraitBrushes.IsValidIndex(Index) && PortraitBrushes[Index].GetResourceObject())
		{
			FSlateBrush Face = PortraitBrushes[Index];
			const EChaosImpactModelKind Kind = ChaosImpactRoster::Get(Index).Kind;
			const bool bWhole = Kind == EChaosImpactModelKind::Authored || Kind == EChaosImpactModelKind::Handstand;
			Face.SetUVRegion(bWhole ? FBox2f(FVector2f(0.03f, 0.2f), FVector2f(0.97f, 0.8f))
				: Index == ChaosImpactRoster::CapIndex ? FBox2f(FVector2f(0.14f, 0.06f), FVector2f(0.86f, 0.62f))
				: FBox2f(FVector2f(0.2f, 0.04f), FVector2f(0.8f, 0.5f)));
			PaintPicture(P, Face, FBox2D(R.Min + FVector2D(6.0f, 4.0f), R.Max - FVector2D(6.0f, 24.0f)));
		}
		Over.Box(R.Min.X + 4.0f, R.Max.Y - 24.0f, Tile - 8.0f, 20.0f, FLinearColor(0.005f, 0.01f, 0.025f, 0.9f));
		const int32 NameLength = FCString::Strlen(ChaosImpactRoster::Get(Index).Name);
		Over.Text(ChaosImpactRoster::Get(Index).Name, R.GetCenter().X, R.Max.Y - 23.0f, NameLength >= 7 ? 10.0f : 13.0f, Paper,
			ETextAlign::Center, TEXT("Bold"));
		if (Area == EArea::Characters && Index == CharacterCursor)
		{
			DrawFrame(Top, Grow(R, 3.0f), WithAlpha(Gold, Pulse), 4.0f);
		}
		else if (Index == Character)
		{
			DrawFrame(Top, Grow(R, 1.0f), WithAlpha(Ice, 0.8f), 2.0f);
		}
	}

	// The model, as it is being coloured (dragged across, Z / X or the right stick: it turns).
	PaintPicture(P, PreviewBrush, FBox2D(FVector2D(PreviewLeft, PreviewTop), FVector2D(PreviewLeft + PreviewWidth, PreviewTop + PreviewHeight)));
	P.Text(ChaosImpactRoster::Get(Character).Name, PreviewLeft + PreviewWidth * 0.5f, PreviewTop + 8.0f, 28.0f, Paper,
		ETextAlign::Center, TEXT("Black"), 2.0f, Ink);
	P.Text(TEXT("ドラッグ・Z / X・右スティックで回転"), PreviewLeft + PreviewWidth * 0.5f, PreviewTop + 46.0f, 14.0f, WithAlpha(Paper, 0.6f),
		ETextAlign::Center, TEXT("Bold"));
	const UChaosImpactColourSubsystem* Colours = UChaosImpactColourSubsystem::Get(Controller.Get());
	const TArray<FChaosImpactColourPreset> NoPresets;
	const TArray<FChaosImpactColourPreset>& Presets = Colours ? Colours->GetPresets(Character) : NoPresets;
	const FString PresetName = Presets.IsValidIndex(Preset) ? Presets[Preset].Name : FString(TEXT("新しいカラー"));
	P.Text(PresetName + (bDirty ? TEXT("（未保存）") : TEXT("")), PreviewLeft + PreviewWidth * 0.5f, PreviewTop + PreviewHeight - 36.0f, 20.0f,
		bDirty ? Gold : Paper, ETextAlign::Center, TEXT("Bold"), 2.0f, Ink);

	// Buttons.
	for (int32 Button = 0; Button < Buttons; ++Button)
	{
		const FBox2D R = ButtonRect(Button);
		const bool bCursor = Area == EArea::Buttons && Button == ButtonCursor;
		const FLinearColor Accent = Button == 0 ? Gold : Button == 1 ? Ice : Button == 2 ? Paper : Button == 3 ? Fire : Muted;
		RoundBox(Back, R, 8.0f, bCursor ? Accent : Card);
		P.Box(R.Min.X + 8.0f, R.Max.Y - 6.0f, R.GetSize().X - 16.0f, 3.0f, Accent);
		P.Text(ButtonNames[Button], R.GetCenter().X, R.Min.Y + 16.0f, 16.0f, bCursor ? Ink : Paper, ETextAlign::Center, TEXT("Black"));
	}

	// Presets.
	P.Text(TEXT("プリセット"), RightLeft, ChipTop - 34.0f, 22.0f, Paper, ETextAlign::Left, TEXT("Black"));
	P.Text(TEXT("LB / RB ・ Q / E"), RightLeft + RightWidth, ChipTop - 30.0f, 15.0f, Muted, ETextAlign::Right, TEXT("Bold"));
	for (int32 Chip = 0; Chip < PresetChips(); ++Chip)
	{
		const FBox2D R = ChipRect(Chip);
		const bool bNewChip = !Presets.IsValidIndex(Chip);
		const bool bCurrent = bNewChip ? Preset == INDEX_NONE : Chip == Preset;
		RoundBox(Back, R, 8.0f, bCurrent ? CardOn : Card);
		if (bNewChip)
		{
			P.Text(TEXT("＋"), R.GetCenter().X, R.Min.Y + 6.0f, 26.0f, Paper, ETextAlign::Center, TEXT("Black"));
			P.Text(TEXT("新規"), R.GetCenter().X, R.Max.Y - 20.0f, 12.0f, Muted, ETextAlign::Center, TEXT("Bold"));
		}
		else
		{
			const int32 Stripes = FMath::Max(1, FMath::Min(GetPartCount(Character), 4));
			for (int32 Stripe = 0; Stripe < Stripes; ++Stripe)
			{
				FColor Colour;
				const bool bOn = GetPart(Presets[Chip].Parts, Stripe, Colour);
				P.Box(R.Min.X + 6.0f + Stripe * (ChipSize - 12.0f) / Stripes, R.Min.Y + 6.0f, (ChipSize - 12.0f) / Stripes, 28.0f,
					bOn ? Shown(Colour) : FLinearColor(0.2f, 0.22f, 0.28f, 1.0f));
			}
			P.Text(FString::FromInt(Chip + 1), R.GetCenter().X, R.Max.Y - 22.0f, 14.0f, Paper, ETextAlign::Center, TEXT("Black"));
		}
		if (Area == EArea::Presets && Chip == ChipCursor)
		{
			DrawFrame(Top, Grow(R, 3.0f), WithAlpha(Gold, Pulse), 3.0f);
		}
	}

	// Parts.
	const bool bPicking = Area == EArea::Palette || Area == EArea::Ring || Area == EArea::Square || Area == EArea::Sliders || Area == EArea::Recent;
	P.Text(TEXT("部位"), RightLeft, PartsTop - 34.0f, 22.0f, Paper, ETextAlign::Left, TEXT("Black"));
	P.Text(TEXT("← → で色相をまわす"), RightLeft + RightWidth, PartsTop - 30.0f, 15.0f, Muted, ETextAlign::Right, TEXT("Bold"));
	const TArray<FString>& Names = GetPartNames(Character);
	for (int32 Part = 0; Part < PartCount(); ++Part)
	{
		const FBox2D R = PartRect(Part);
		const bool bPainting = Part == PartCursor && (Area == EArea::Parts || bPicking);
		RoundBox(Back, R, 8.0f, bPainting ? CardOn : Card);
		P.Text(Names.IsValidIndex(Part) ? Names[Part] : FString(), R.Min.X + 16.0f, R.Min.Y + 6.0f, 20.0f, Paper, ETextAlign::Left, TEXT("Bold"));
		bool bOn = false;
		const FColor Colour = PartColour(Part, &bOn);
		const FBox2D SwatchBox(FVector2D(R.Max.X - 150.0f, R.Min.Y + 5.0f), FVector2D(R.Max.X - 40.0f, R.Max.Y - 5.0f));
		PaintSwatch(P, SwatchBox, bOn, Colour);
		if (!bOn)
		{
			P.Text(TEXT("元の色"), SwatchBox.GetCenter().X, R.Min.Y + 9.0f, 14.0f, Paper, ETextAlign::Center, TEXT("Bold"));
		}
		if (bPainting && Area == EArea::Parts)
		{
			P.Text(TEXT("◀"), R.Max.X - 168.0f, R.Min.Y + 7.0f, 18.0f, WithAlpha(Gold, Pulse), ETextAlign::Center, TEXT("Black"));
			P.Text(TEXT("▶"), R.Max.X - 20.0f, R.Min.Y + 7.0f, 18.0f, WithAlpha(Gold, Pulse), ETextAlign::Center, TEXT("Black"));
			DrawFrame(Top, Grow(R, 2.0f), WithAlpha(Gold, Pulse), 3.0f);
		}
		else if (bPainting)
		{
			DrawFrame(Top, Grow(R, 1.0f), WithAlpha(Ice, 0.9f), 2.0f);
		}
	}

	// The picker's heading and its two ways (T / Y).
	bool bPartOn = false;
	const FColor Current = PartColour(PartCursor, &bPartOn);
	P.Text(FString::Printf(TEXT("「%s」の色"), Names.IsValidIndex(PartCursor) ? *Names[PartCursor] : TEXT("")), RightLeft, PickerTop - 38.0f, 20.0f,
		Paper, ETextAlign::Left, TEXT("Black"));
	for (const bool bFineTab : {false, true})
	{
		const FBox2D R = TabRect(bFineTab);
		const bool bChosen = bFine == bFineTab;
		RoundBox(Back, R, 8.0f, bChosen ? Gold : Card);
		P.Text(bFineTab ? TEXT("こまかく") : TEXT("パレット"), R.GetCenter().X, R.Min.Y + 6.0f, 16.0f, bChosen ? Ink : Paper, ETextAlign::Center,
			TEXT("Black"));
	}
	P.Text(TEXT("T / Y"), RightLeft + RightWidth - 270.0f, PickerTop - 34.0f, 14.0f, Muted, ETextAlign::Right, TEXT("Bold"));

	if (!bFine)
	{
		// The palette: hues across, pale to deep down, the greys at the end.
		for (int32 Index = 0; Index < PaletteColumns * PaletteRows; ++Index)
		{
			const FBox2D R = SwatchRect(Index);
			const FColor Colour = GetPaletteSwatch(Index % PaletteColumns, Index / PaletteColumns);
			RoundBox(P, R, 8.0f, Shown(Colour));
			if (bPartOn && Colour == Current)
			{
				DrawFrame(Over, Grow(R, 2.0f), WithAlpha(Paper, 0.9f), 3.0f);
			}
			if (Area == EArea::Palette && Index == SwatchCursor)
			{
				DrawFrame(Top, Grow(R, 4.0f), WithAlpha(Gold, Pulse), 3.0f);
			}
		}
	}
	else
	{
		// Photoshop's colour wheel: the hue ring, smooth all round; inside it the square of saturation (across)
		// and brightness (up) for that hue.
		const FVector2D Centre = WheelCentre();
		TArray<FVector2f> RingPoints;
		TArray<FLinearColor> RingColours;
		constexpr int32 RingSteps = 180;
		const float RingMid = RingOuter - RingWidth * 0.5f;
		for (int32 Step = 0; Step <= RingSteps; ++Step)
		{
			const float Angle = 360.0f * Step / RingSteps;
			const float Radians = FMath::DegreesToRadians(Angle);
			RingPoints.Add(FVector2f(Centre + FVector2D(FMath::Cos(Radians), -FMath::Sin(Radians)) * RingMid));
			RingColours.Add(Shown(FromHSV(Angle, 1.0f, 1.0f)));
		}
		FSlateDrawElement::MakeLines(P.Elements, P.Layer, P.Geometry.ToPaintGeometry(), MoveTemp(RingPoints), MoveTemp(RingColours),
			ESlateDrawEffect::None, FLinearColor::White, true, RingWidth);
		if (Area == EArea::Ring)
		{
			Over.Ring(Centre, RingOuter + 3.0f, WithAlpha(Gold, Pulse), 3.0f, 96);
		}
		const float HueRadians = FMath::DegreesToRadians(EditHue);
		const FVector2D HueKnob = Centre + FVector2D(FMath::Cos(HueRadians), -FMath::Sin(HueRadians)) * RingMid;
		Top.Ring(HueKnob, RingWidth * 0.5f + 2.0f, Ink, 4.0f);
		Top.Ring(HueKnob, RingWidth * 0.5f + 2.0f, Paper, 2.0f);
		const FBox2D Square = SquareRect();
		Gradient(P, Square, {FLinearColor::White, Shown(FromHSV(EditHue, 1.0f, 1.0f))});
		Gradient(Over, Square, {FLinearColor(0.0f, 0.0f, 0.0f, 0.0f), FLinearColor(0.0f, 0.0f, 0.0f, 1.0f)}, false);
		if (Area == EArea::Square)
		{
			DrawFrame(Top, Grow(Square, 3.0f), WithAlpha(Gold, Pulse), 3.0f);
		}
		const FVector2D SquareKnob(Square.Min.X + Square.GetSize().X * EditSaturation, Square.Max.Y - Square.GetSize().Y * EditBrightness);
		Top.Ring(SquareKnob, 8.0f, EditBrightness > 0.6f && EditSaturation < 0.4f ? Ink : Paper, 2.5f);

		// H S B and R G B: each track is what moving along it gives; the number at the end.
		const FColor Fine = bPartOn ? Current : FromHSV(EditHue, EditSaturation, EditBrightness);
		for (int32 Slider = 0; Slider < Sliders; ++Slider)
		{
			const FBox2D R = SliderRect(Slider);
			const bool bCursor = Area == EArea::Sliders && Slider == SliderCursor;
			RoundBox(Back, R, 8.0f, bCursor ? CardOn : Card);
			P.Text(SliderNames[Slider], R.Min.X + 10.0f, R.Min.Y + 4.0f, 16.0f, Slider == 3 ? FLinearColor(1.0f, 0.4f, 0.4f) : Slider == 4
				? FLinearColor(0.4f, 1.0f, 0.5f) : Slider == 5 ? FLinearColor(0.5f, 0.65f, 1.0f) : Paper, ETextAlign::Left, TEXT("Black"));
			const FBox2D Track(FVector2D(R.Min.X + 30.0f, R.Min.Y + 9.0f), FVector2D(R.Max.X - 52.0f, R.Max.Y - 9.0f));
			TArray<FLinearColor> Run;
			float Amount = 0.0f;
			FString Number;
			if (Slider == 0)
			{
				for (int32 Stop = 0; Stop <= 6; ++Stop) { Run.Add(Shown(FromHSV(60.0f * Stop, 1.0f, 1.0f))); }
				Amount = EditHue / 360.0f;
				Number = FString::Printf(TEXT("%d°"), FMath::RoundToInt(EditHue));
			}
			else if (Slider <= 2)
			{
				const bool bSat = Slider == 1;
				Run = {Shown(FromHSV(EditHue, bSat ? 0.0f : EditSaturation, bSat ? EditBrightness : 0.0f)),
					Shown(FromHSV(EditHue, bSat ? 1.0f : EditSaturation, bSat ? EditBrightness : 1.0f))};
				Amount = bSat ? EditSaturation : EditBrightness;
				Number = FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Amount * 100.0f));
			}
			else
			{
				FColor Low = Fine, High = Fine;
				(Slider == 3 ? Low.R : Slider == 4 ? Low.G : Low.B) = 0;
				(Slider == 3 ? High.R : Slider == 4 ? High.G : High.B) = 255;
				Run = {Shown(Low), Shown(High)};
				const uint8 Channel = Slider == 3 ? Fine.R : Slider == 4 ? Fine.G : Fine.B;
				Amount = Channel / 255.0f;
				Number = FString::FromInt(Channel);
			}
			Gradient(P, Track, Run);
			Over.Box(static_cast<float>(Track.Min.X + Track.GetSize().X * Amount) - 2.5f, R.Min.Y + 4.0f, 5.0f, SliderHeight - 8.0f, Paper);
			P.Text(Number, R.Max.X - 8.0f, R.Min.Y + 5.0f, 15.0f, Paper, ETextAlign::Right, TEXT("Bold"));
			if (bCursor)
			{
				DrawFrame(Top, Grow(R, 2.0f), WithAlpha(Gold, Pulse), 3.0f);
			}
		}
		// The colour itself, with its code.
		const float SampleTop = PickerTop + Sliders * (SliderHeight + SliderGap);
		PaintSwatch(P, FBox2D(FVector2D(SlidersLeft, SampleTop), FVector2D(SlidersLeft + 70.0f, SampleTop + 28.0f)), bPartOn, Current);
		P.Text(bPartOn ? FString::Printf(TEXT("#%02X%02X%02X"), Current.R, Current.G, Current.B) : FString(TEXT("元の色のまま")),
			SlidersLeft + 82.0f, SampleTop + 3.0f, 17.0f, Paper, ETextAlign::Left, TEXT("Bold"));
	}

	// The colours used last (newest first), then 元の色.
	P.Text(TEXT("最近"), RightLeft, RecentTop + 4.0f, 17.0f, Muted, ETextAlign::Left, TEXT("Bold"));
	for (int32 Index = 0; Index < MaxRecent; ++Index)
	{
		const FBox2D R = RecentRect(Index);
		if (Index < RecentCount())
		{
			RoundBox(P, R, 8.0f, Shown(RecentColour(Index)));
		}
		else
		{
			RoundBox(Back, R, 8.0f, FLinearColor(0.05f, 0.08f, 0.16f, 1.0f));
		}
		if (Area == EArea::Recent && Index == RecentCursor && Index < RecentCount())
		{
			DrawFrame(Top, Grow(R, 3.0f), WithAlpha(Gold, Pulse), 3.0f);
		}
	}
	const FBox2D Original = OriginalRect();
	RoundBox(Back, Original, 8.0f, !bPartOn ? CardOn : Card);
	P.Text(TEXT("元の色"), Original.GetCenter().X, Original.Min.Y + 6.0f, 15.0f, Paper, ETextAlign::Center, TEXT("Bold"));
	if (Area == EArea::Recent && RecentCursor >= RecentCount())
	{
		DrawFrame(Top, Grow(Original, 3.0f), WithAlpha(Gold, Pulse), 3.0f);
	}

	// Guide bar and notices.
	Back.Box(0.0f, CeBarTop, 1600.0f, 58.0f, FLinearColor(0.004f, 0.006f, 0.016f, 0.95f));
	const TCHAR* Guide = Area == EArea::Characters ? TEXT("A / Enter：このキャラにする　　B / Esc：もどる")
		: Area == EArea::Palette ? TEXT("十字 / 矢印：色をえらぶ（すぐ反映）　　A / Enter：決定して次の部位へ　　T / Y：こまかく　　B / Esc：やめる")
		: Area == EArea::Ring ? TEXT("←→：色相　　↓：四角へ　　Tab / LT RT：次へ　　A / Enter：決定　　B / Esc：やめる")
		: Area == EArea::Square ? TEXT("十字 / スティック：彩度（横）・明るさ（縦）　　端の先：となりへ　　A / Enter：決定　　B / Esc：やめる")
		: Area == EArea::Sliders ? TEXT("←→：調整（長押しで速く）　　↑↓：H S B R G B　　A / Enter：決定　　B / Esc：やめる")
		: Area == EArea::Recent ? TEXT("←→：最近の色・元の色　　A / Enter：決定　　↑：もどる　　B / Esc：やめる")
		: Area == EArea::Parts ? TEXT("↑↓：部位　　←→：色相　　A / Enter：色をえらぶ　　R：元の色　　X / F：保存　　View / N：名前")
		: Area == EArea::Presets ? TEXT("←→：プリセット　　A / Enter：よびだす　　B / Esc：キャラへ")
		: TEXT("←→：ボタン　　A / Enter：決定　　↑：最近の色へ　　View / N：名前");
	// (The name entry shows its own keys.)
	if (!Naming.IsOpen()) P.Text(Guide, 800.0f, CeBarTop + 15.0f, 18.0f, Paper, ETextAlign::Center, TEXT("Bold"));
	if (Now - NoticeAt < 2.5)
	{
		const float Fade = FMath::Clamp(static_cast<float>(2.5 - (Now - NoticeAt)) / 0.4f, 0.0f, 1.0f);
		const FPainter Note{Design, Elements, Layer + 4, Fade};
		Note.Box(PreviewLeft - 18.0f, PreviewTop + PreviewHeight - 90.0f, PreviewWidth + 36.0f, 44.0f, FLinearColor(0.004f, 0.006f, 0.016f, 0.92f));
		Note.Text(NoticeText, PreviewLeft + PreviewWidth * 0.5f, PreviewTop + PreviewHeight - 80.0f, 19.0f, Gold, ETextAlign::Center, TEXT("Bold"));
	}
	if (Naming.IsOpen())
	{
		return Naming.Paint(Design, Elements, Layer + 6);
	}
	return Layer + 4;
}
