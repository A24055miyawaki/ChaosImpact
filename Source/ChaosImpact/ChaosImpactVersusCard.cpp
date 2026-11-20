#include "ChaosImpactVersusCard.h"
#include "ChaosImpactSfx.h"
#include "ChaosImpactMenuWidget.h"

#include "ChaosImpactPaint.h"
#include "Engine/Texture.h"
#include "Rendering/DrawElementTypes.h"

using namespace ChaosImpactPaint;

namespace
{
	constexpr float DesignWidth = 1600.0f;
	constexpr float DesignHeight = 900.0f;
	constexpr float VersusLandsAt = SChaosImpactVersusCard::VersusLandsAt;
	/** The character previews' picture shape (600 x 800). */
	constexpr float PictureAspect = 0.75f;
	const FLinearColor LeftSide(0.0f, 0.10f, 0.40f, 1.0f);
	const FLinearColor RightSide(0.40f, 0.0f, 0.03f, 1.0f);

	/**
	 * スマホ版 held upright (2026-11-18): a 900 x 1600 page instead. Two sides face each other above and below the VS; three
	 * or more stand round it on a tall ring.
	 */
	bool IsUprightCard(const FGeometry& Allotted)
	{
		const FVector2f Size = Allotted.GetLocalSize();
		return UChaosImpactMenuWidget::IsPhoneBuild() && Size.Y > Size.X * 1.15f;
	}
	/** Where VS lands. */
	FVector2f VersusCentre(const bool bUpright)
	{
		return bUpright ? FVector2f(450.0f, 800.0f) : FVector2f(800.0f, 430.0f);
	}

	/** The 1600x900 design space (upright 900 x 1600), fitted in the middle of the widget like the menus. */
	FGeometry MakeDesign(const FGeometry& Allotted, const FVector2f Shake = FVector2f::ZeroVector)
	{
		const FVector2f Size = Allotted.GetLocalSize();
		const bool bUpright = IsUprightCard(Allotted);
		const float W = bUpright ? DesignHeight : DesignWidth;
		const float H = bUpright ? DesignWidth : DesignHeight;
		const float Scale = FMath::Min(Size.X / W, Size.Y / H);
		const FVector2f Offset = (Size - FVector2f(W, H) * Scale) * 0.5f + Shake * Scale;
		return Allotted.MakeChild(FVector2f(W, H), FSlateLayoutTransform(Scale, Offset));
	}

	/** The navy backdrop of the menus (dark bands, a little lighter at the top). */
	void PaintBackdrop(const FGeometry& Allotted, const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer)
	{
		const FPainter Full{Allotted, Elements, Layer};
		Full.Box(0.0f, 0.0f, Allotted.GetLocalSize().X, Allotted.GetLocalSize().Y, Ink);
		const FPainter Flat{Design, Elements, Layer};
		const int32 Bands = IsUprightCard(Allotted) ? 32 : 18;
		for (int32 Band = 0; Band < Bands; ++Band)
		{
			const float T = Band / static_cast<float>(Bands - 1);
			Flat.Box(-1200.0f, Band * 50.0f, 4000.0f, 51.0f, FMath::Lerp(
				FLinearColor(0.022f, 0.03f, 0.06f, 1.0f), FLinearColor(0.003f, 0.004f, 0.01f, 1.0f), T));
		}
	}

	/** "VS" with blue and red ghosts either side, centred on (0, 0) of Painter's space. */
	void PaintVersusLetters(const FPainter& Painter, const float Size)
	{
		const float Top = -Size * 0.66f;
		const FPainter Ghosts{Painter.Geometry, Painter.Elements, Painter.Layer, Painter.Alpha * 0.85f};
		Ghosts.Text(TEXT("VS"), -Size * 0.06f, Top, Size, Ice, ETextAlign::Center, TEXT("BlackItalic"));
		Ghosts.Text(TEXT("VS"), Size * 0.06f, Top, Size, Fire, ETextAlign::Center, TEXT("BlackItalic"));
		const FPainter Front{Painter.Geometry, Painter.Elements, Painter.Layer + 2, Painter.Alpha};
		Front.Text(TEXT("VS"), 0.0f, Top, Size, Paper, ETextAlign::Center, TEXT("BlackItalic"), Size * 0.04f, Ink);
	}

	/** Size for Text in Font so it fits in Width (no bigger than Size). */
	float FitSize(const FString& Text, const TCHAR* Font, const float Size, const float Width)
	{
		const float Measured = static_cast<float>(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(
			Text, FCoreStyle::GetDefaultFontStyle(Font, Size)).X);
		return Measured > Width && Measured > 0.0f ? Size * Width / Measured : Size;
	}

	/** Where a fighter's card stands, and where it slides in from. */
	struct FCardPlace
	{
		float X = 0.0f;
		float Y = 0.0f;
		float Width = 490.0f;
		float Height = 230.0f;
		/** On the left half: its colour stripe and portrait on its outer (left) edge. */
		bool bLeft = true;
		/** The way it slides in from (it starts this far off, times 1000). */
		FVector2f From = FVector2f::ZeroVector;
	};

	/** A space centred on Center, scaled about it and leaning like the menus. */
	FGeometry MakeEmblem(const FGeometry& Design, const FVector2f Center, const float Scale)
	{
		const FSlateRenderTransform Render(TMatrix2x2<float>(Scale, 0.0f, Scale * -0.12f, Scale));
		return Design.MakeChild(FVector2f(2.0f, 2.0f), FSlateLayoutTransform(Center - FVector2f(1.0f, 1.0f)),
			Render, FVector2f(0.5f, 0.5f));
	}
}

void SChaosImpactVersusCard::Construct(const FArguments& InArgs)
{
	Info = InArgs._Info;
	if (Info.StartedAt <= 0.0)
	{
		Info.StartedAt = FPlatformTime::Seconds();
	}
	// The card slams in.
	ChaosImpactSfx::Play2D(nullptr, EChaosImpactSfx::UiReveal);
	for (const FChaosImpactVersusEntrant& Entrant : Info.Entrants)
	{
		FSlateBrush& Brush = PictureBrushes.AddDefaulted_GetRef();
		if (Entrant.Picture)
		{
			Pictures.Add(Entrant.Picture);
			Brush.SetResourceObject(Entrant.Picture);
			Brush.ImageSize = FVector2D(600.0, 800.0);
			Brush.DrawAs = ESlateBrushDrawType::Image;
		}
	}
	SetCanTick(false);
	RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateLambda([this](double, float)
	{
		Invalidate(EInvalidateWidgetReason::Paint);
		return EActiveTimerReturnType::Continue;
	}));
}

void SChaosImpactVersusCard::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObjects(Pictures);
}

int32 SChaosImpactVersusCard::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, const int32 LayerId,
	const FWidgetStyle& InWidgetStyle, const bool bParentEnabled) const
{
	const float T = static_cast<float>(FPlatformTime::Seconds() - Info.StartedAt);
	// The screen jolts as VS lands.
	const float Jolt = T > VersusLandsAt && T < VersusLandsAt + 0.28f
		? (1.0f - (T - VersusLandsAt) / 0.28f) * 14.0f : 0.0f;
	const FVector2f Shake(FMath::Sin(T * 95.0f) * Jolt, FMath::Cos(T * 80.0f) * Jolt * 0.6f);
	const FGeometry Design = MakeDesign(AllottedGeometry, Shake);
	PaintBackdrop(AllottedGeometry, Design, OutDrawElements, LayerId);
	const bool bUpright = IsUprightCard(AllottedGeometry);
	const FVector2f Hub = VersusCentre(bUpright);
	const float PageW = bUpright ? DesignHeight : DesignWidth;
	const float PageH = bUpright ? DesignWidth : DesignHeight;

	// Two sides facing each other for a 1-on-1 or two teams; anything else stands in a ring round the VS.
	const int32 Count = Info.Entrants.Num();
	const bool bSides = Count == 2 || Info.Teams == 2;
	TArray<FCardPlace> Places;
	Places.SetNum(Count);
	if (bSides)
	{
		TArray<int32> Halves[2];
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const bool bLeftHalf = Info.Teams == 2 ? Info.Entrants[Index].Team <= 0 : Index < (Count + 1) / 2;
			Halves[bLeftHalf ? 0 : 1].Add(Index);
		}
		for (int32 Half = 0; Half < 2; ++Half)
		{
			const int32 SideCount = Halves[Half].Num();
			const float CardHeight = SideCount <= 1 ? 230.0f : SideCount == 2 ? 170.0f : SideCount == 3 ? 132.0f : 108.0f;
			constexpr float Gap = 18.0f;
			for (int32 Slot = 0; Slot < SideCount; ++Slot)
			{
				FCardPlace& Place = Places[Halves[Half][Slot]];
				Place.Height = CardHeight;
				Place.bLeft = Half == 0;
				if (bUpright)
				{
					// Upright: one side above the VS, the other under it, each the width of the screen.
					Place.Width = 760.0f;
					Place.X = 70.0f;
					const float Middle = Place.bLeft ? 440.0f : 1160.0f;
					Place.Y = Middle - (SideCount * CardHeight + (SideCount - 1) * Gap) * 0.5f + Slot * (CardHeight + Gap);
					Place.From = FVector2f(Place.bLeft ? -1.0f : 1.0f, 0.0f);
					continue;
				}
				Place.Width = 490.0f;
				Place.X = Place.bLeft ? 80.0f : DesignWidth - 80.0f - Place.Width;
				Place.Y = 430.0f - (SideCount * CardHeight + (SideCount - 1) * Gap) * 0.5f + Slot * (CardHeight + Gap);
				Place.From = FVector2f(Place.bLeft ? -1.0f : 1.0f, 0.0f);
			}
		}
	}
	else
	{
		// Evenly round an ellipse about the VS, clockwise from the upper left (P1 first, where the eye starts), so
		// the ring sits level.
		const float Step = 360.0f / FMath::Max(Count, 1);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const float Angle = FMath::DegreesToRadians(-90.0f - Step * 0.5f + Index * Step);
			const FVector2f Out(FMath::Cos(Angle), FMath::Sin(Angle));
			FCardPlace& Place = Places[Index];
			Place.Height = 146.0f;
			if (bUpright)
			{
				// Upright: two across, the first rows above the VS and the rest under it (a lone last one in the middle).
				const int32 Rows = (Count + 1) / 2;
				const int32 RowsAbove = (Rows + 1) / 2;
				const int32 Row = Index / 2;
				const bool bAbove = Row < RowsAbove;
				const int32 InGroup = bAbove ? RowsAbove : Rows - RowsAbove;
				const int32 GroupRow = bAbove ? Row : Row - RowsAbove;
				const float Middle = bAbove ? 440.0f : 1160.0f;
				const bool bAlone = Index == Count - 1 && Count % 2 == 1;
				Place.Width = 370.0f;
				Place.X = bAlone ? 450.0f - Place.Width * 0.5f : Index % 2 == 0 ? 60.0f : 470.0f;
				Place.Y = Middle - (InGroup * 146.0f + (InGroup - 1) * 18.0f) * 0.5f + GroupRow * 164.0f;
				Place.bLeft = bAlone ? true : Index % 2 == 0;
				Place.From = FVector2f(Place.bLeft ? -1.0f : 1.0f, 0.0f);
				continue;
			}
			Place.Width = 300.0f;
			Place.X = 800.0f + Out.X * 560.0f - Place.Width * 0.5f;
			Place.Y = 430.0f + Out.Y * 250.0f - Place.Height * 0.5f;
			Place.bLeft = Out.X < -0.01f || (FMath::Abs(Out.X) <= 0.01f && Index % 2 == 0);
			Place.From = Out;
		}
	}

	// Behind them: for two sides, the halves slide in and meet along a slanted seam in the middle; for a ring, one
	// field with a warm glow at the middle and the ring the fighters stand on.
	const float SidesIn = EaseOut(T / 0.38f);
	const FGeometry Slant = MakeSkewed(Design, 0.0f, 0.0f, PageW, PageH, -0.36f);
	const FPainter Sides{Slant, OutDrawElements, LayerId + 1};
	if (bSides && bUpright)
	{
		// Upright: the halves come down and up, meeting across the middle.
		const FPainter Flat{Design, OutDrawElements, LayerId + 1};
		Flat.Box(-400.0f, -400.0f - (1.0f - SidesIn) * 1200.0f, 1700.0f, 1190.0f, WithAlpha(LeftSide, 0.5f));
		Flat.Box(-400.0f, 778.0f - (1.0f - SidesIn) * 1200.0f, 1700.0f, 12.0f, WithAlpha(Ice, 0.9f));
		Flat.Box(-400.0f, 810.0f + (1.0f - SidesIn) * 1200.0f, 1700.0f, 1190.0f, WithAlpha(RightSide, 0.5f));
		Flat.Box(-400.0f, 810.0f + (1.0f - SidesIn) * 1200.0f, 1700.0f, 12.0f, WithAlpha(Fire, 0.9f));
		const FPainter Seam{Design, OutDrawElements, LayerId + 1, FMath::Clamp((T - 0.4f) / 0.2f, 0.0f, 1.0f)};
		Seam.Box(-400.0f, 797.0f, 1700.0f, 6.0f, Gold);
	}
	else if (bSides)
	{
		Sides.Box(-1700.0f - (1.0f - SidesIn) * 1100.0f, -60.0f, 2490.0f, 1020.0f, WithAlpha(LeftSide, 0.5f));
		Sides.Box(778.0f - (1.0f - SidesIn) * 1100.0f, -60.0f, 12.0f, 1020.0f, WithAlpha(Ice, 0.9f));
		Sides.Box(810.0f + (1.0f - SidesIn) * 1100.0f, -60.0f, 2490.0f, 1020.0f, WithAlpha(RightSide, 0.5f));
		Sides.Box(810.0f + (1.0f - SidesIn) * 1100.0f, -60.0f, 12.0f, 1020.0f, WithAlpha(Fire, 0.9f));
		const FPainter Seam{Slant, OutDrawElements, LayerId + 1, FMath::Clamp((T - 0.4f) / 0.2f, 0.0f, 1.0f)};
		Seam.Box(797.0f, -60.0f, 6.0f, 1020.0f, Gold);
	}
	else
	{
		const FPainter Field{Design, OutDrawElements, LayerId + 1, SidesIn};
		for (int32 Glow = 0; Glow < 8; ++Glow)
		{
			Field.Ring(FVector2D(Hub), 120.0f + Glow * 70.0f, WithAlpha(Gold, 0.05f - Glow * 0.005f), 70.0f, 96);
		}
		// The ring the fighters stand on: an ellipse, drawn as a circle in a space squashed about the middle (upright:
		// narrow and tall).
		const float Across = bUpright ? 220.0f / 560.0f : 1.0f;
		const float Down = bUpright ? 1.0f : 250.0f / 560.0f;
		const FGeometry Squashed = Design.MakeChild(FVector2f(PageW, PageH),
			FSlateLayoutTransform(FVector2f(Hub.X * (1.0f - Across), Hub.Y * (1.0f - Down))),
			FSlateRenderTransform(FScale2f(Across, Down)), FVector2f::ZeroVector);
		const FPainter Orbit{Squashed, OutDrawElements, LayerId + 1, SidesIn * FMath::Clamp((T - 0.3f) / 0.3f, 0.0f, 1.0f)};
		Orbit.Ring(FVector2D(Hub), 560.0f, WithAlpha(Gold, 0.35f), 3.0f, 128);
		Orbit.Ring(FVector2D(Hub), 560.0f, WithAlpha(Gold, 0.08f), 26.0f, 128);
	}
	for (int32 Stripe = 0; Stripe < 14; ++Stripe)
	{
		const float X = FMath::Fmod(Stripe * 173.0f + T * (90.0f + Stripe * 13.0f), 2100.0f) - 250.0f;
		Sides.Box(X, -60.0f, 1.5f + Stripe % 3, PageH + 120.0f, FLinearColor(1.0f, 1.0f, 1.0f, 0.02f + (Stripe % 4) * 0.008f));
	}

	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FChaosImpactVersusEntrant& Entrant = Info.Entrants[Index];
		const FCardPlace& Place = Places[Index];
		const bool bLeft = Place.bLeft;
		const float Delay = 0.16f + Index * 0.06f;
		const float In = EaseOut((T - Delay) / 0.34f);
		if (In <= 0.0f)
		{
			continue;
		}
		const float CardWidth = Place.Width;
		const float CardHeight = Place.Height;
		const float X = Place.X + Place.From.X * (1.0f - In) * 1000.0f;
		const float Top = Place.Y + Place.From.Y * (1.0f - In) * 1000.0f;
		const FGeometry Card = MakeSkewed(Design, X, Top, CardWidth, CardHeight, -0.2f);
		const FPainter Panel{Card, OutDrawElements, LayerId + 3};
		Panel.Box(0.0f, 0.0f, CardWidth, CardHeight, FLinearColor(0.008f, 0.012f, 0.024f, 0.9f));
		Panel.Box(0.0f, 0.0f, CardWidth, CardHeight, WithAlpha(Entrant.Color, 0.16f));
		Panel.Box(bLeft ? 0.0f : CardWidth - 14.0f, 0.0f, 14.0f, CardHeight, Entrant.Color);
		Panel.Box(0.0f, CardHeight - 4.0f, CardWidth, 4.0f, WithAlpha(Entrant.Color, 0.7f));
		// One bright sweep across the card as it settles.
		const float Sweep = (T - Delay - 0.3f) / 0.32f;
		if (Sweep > 0.0f && Sweep < 1.0f)
		{
			Panel.Box(-80.0f + Sweep * (CardWidth + 160.0f), 0.0f, 46.0f, CardHeight,
				FLinearColor(1.0f, 1.0f, 1.0f, 0.22f * FMath::Sin(Sweep * UE_PI)));
		}
		const FPainter Words{Card, OutDrawElements, LayerId + 5};
		const float LabelSize = FMath::Min(CardHeight * 0.36f, 64.0f);
		const float NameSize = FMath::Min(CardHeight * 0.17f, 30.0f);
		// The fighter's portrait, in a window leaning with the card (the model itself stands upright).
		const float PortraitHeight = CardHeight - 12.0f;
		const float PortraitWidth = PortraitHeight * PictureAspect;
		const float PortraitLeft = bLeft ? 22.0f : CardWidth - 22.0f - PortraitWidth;
		const FGeometry Window = MakeSkewed(Design, X + PortraitLeft, Top + 6.0f, PortraitWidth, PortraitHeight, -0.2f);
		const FPainter Frame{Window, OutDrawElements, LayerId + 4};
		Frame.Box(0.0f, 0.0f, PortraitWidth, PortraitHeight, FLinearColor(0.02f, 0.025f, 0.05f, 1.0f));
		if (PictureBrushes.IsValidIndex(Index) && PictureBrushes[Index].GetResourceObject())
		{
			// Zoomed in on the model (the picture has room to spare round it).
			const float ImageWidth = PortraitWidth * 1.45f;
			const float ImageHeight = ImageWidth / PictureAspect;
			const FVector2f WindowCenter(X + PortraitLeft + PortraitWidth * 0.5f, Top + CardHeight * 0.5f);
			OutDrawElements.PushClip(FSlateClippingZone(Window));
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 4,
				Design.ToPaintGeometry(FVector2f(ImageWidth, ImageHeight),
					FSlateLayoutTransform(WindowCenter - FVector2f(ImageWidth * 0.5f, ImageHeight * 0.47f))),
				&PictureBrushes[Index], ESlateDrawEffect::None, FLinearColor::White);
			OutDrawElements.PopClip();
		}
		else
		{
			const FPainter Unknown{Window, OutDrawElements, LayerId + 5};
			Unknown.Text(TEXT("?"), PortraitWidth * 0.5f, PortraitHeight * 0.5f - PortraitHeight * 0.42f, PortraitHeight * 0.6f,
				WithAlpha(Entrant.Color, 0.8f), ETextAlign::Center, TEXT("BlackItalic"));
		}
		Frame.Box(0.0f, PortraitHeight - 3.0f, PortraitWidth, 3.0f, WithAlpha(Entrant.Color, 0.8f));
		const float TextLeft = bLeft ? PortraitLeft + PortraitWidth + 18.0f : 24.0f;
		const float TextRoom = bLeft ? CardWidth - TextLeft - 18.0f : PortraitLeft - TextLeft - 14.0f;
		// The label, and under it the name with its colour after it (each shrunk to fit the card if need be; the colour
		// only where it still fits).
		const float LabelTop = CardHeight * 0.5f - (LabelSize * 1.3f + NameSize * 1.3f) * 0.5f;
		Words.Text(Entrant.Label, TextLeft, LabelTop, FitSize(Entrant.Label, TEXT("BlackItalic"), LabelSize, TextRoom), Entrant.Color,
			ETextAlign::Left, TEXT("BlackItalic"), 3.0f, Ink);
		const float NameTop = LabelTop + LabelSize * 1.3f;
		const float NameFit = FitSize(Entrant.Name, TEXT("Bold"), NameSize, TextRoom - 4.0f);
		Words.Text(Entrant.Name, TextLeft + 4.0f, NameTop, NameFit, Paper, ETextAlign::Left, TEXT("Bold"));
		const float NameWidth = static_cast<float>(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(
			Entrant.Name, FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), NameFit)).X);
		const float DetailWidth = static_cast<float>(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(
			Entrant.Detail, FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), NameSize * 0.7f)).X);
		if (NameWidth + 14.0f + DetailWidth <= TextRoom - 4.0f)
		{
			Words.Text(Entrant.Detail, TextLeft + 4.0f + NameWidth + 14.0f, NameTop + NameSize * 0.32f, NameSize * 0.7f, Muted,
				ETextAlign::Left, TEXT("Bold"));
		}
	}

	// Header and the rules along the bottom.
	const float HeaderIn = EaseOut((T - 0.1f) / 0.3f);
	const FPainter Header{Design, OutDrawElements, LayerId + 5, HeaderIn};
	Header.Text(TEXT("VS MODE"), 70.0f - (1.0f - HeaderIn) * 60.0f, 52.0f, 46.0f, Paper, ETextAlign::Left, TEXT("BlackItalic"));
	Header.Box(74.0f, 116.0f, 44.0f * HeaderIn, 9.0f, Ice);
	Header.Box(130.0f, 116.0f, 200.0f * HeaderIn, 9.0f, Fire);
	const float FooterIn = EaseOut((T - 0.85f) / 0.3f);
	if (FooterIn > 0.0f && !Info.Footer.IsEmpty())
	{
		const FPainter Band{Design, OutDrawElements, LayerId + 3, FooterIn};
		// (Upright: at the foot of the page.)
		const float BandY = bUpright ? 1470.0f : 772.0f;
		Band.Box(-1200.0f, BandY, 4000.0f, 70.0f, WithAlpha(Ink, 0.82f));
		Band.Box(-1200.0f, BandY, 4000.0f, 3.0f, Gold);
		const FPainter Rules{Design, OutDrawElements, LayerId + 5, FooterIn};
		Rules.Text(Info.Footer, PageW * 0.5f, BandY + 16.0f + (1.0f - FooterIn) * 12.0f, FitSize(Info.Footer, TEXT("Bold"), 28.0f, PageW - 80.0f), Paper,
			ETextAlign::Center, TEXT("Bold"));
	}

	// VS slams down, flashes and sends rings out.
	if (T >= VersusLandsAt)
	{
		const float Since = T - VersusLandsAt;
		const float Land = EaseOut(Since / 0.16f);
		const float Scale = FMath::Lerp(2.6f, 1.0f, Land) * (1.0f + 0.025f * FMath::Sin(Since * 5.0f) * FMath::Clamp(Since - 0.3f, 0.0f, 1.0f));
		const FPainter Emblem{MakeEmblem(Design, Hub, Scale), OutDrawElements, LayerId + 7,
			FMath::Clamp(Since / 0.08f, 0.0f, 1.0f)};
		PaintVersusLetters(Emblem, 210.0f);
		const float Wave = Since / 0.75f;
		if (Wave < 1.0f)
		{
			const FPainter Rings{Design, OutDrawElements, LayerId + 6, 1.0f - Wave};
			Rings.Ring(FVector2D(Hub), 90.0f + 900.0f * EaseOut(Wave), Gold, 3.0f + 10.0f * (1.0f - Wave), 96);
			Rings.Ring(FVector2D(Hub), 50.0f + 620.0f * EaseOut(Wave), Ice, 5.0f, 96);
			Rings.Ring(FVector2D(Hub), 30.0f + 420.0f * EaseOut(Wave), Fire, 5.0f, 96);
		}
		const float Flash = 1.0f - Since / 0.3f;
		if (Flash > 0.0f)
		{
			const FPainter Full{AllottedGeometry, OutDrawElements, LayerId + 10, Flash * 0.45f};
			Full.Box(0.0f, 0.0f, AllottedGeometry.GetLocalSize().X, AllottedGeometry.GetLocalSize().Y, Paper);
		}
	}

	// Loading: three dots that keep moving even while the level loads.
	if (T > IntroSeconds - 0.2f)
	{
		const FPainter Loading{Design, OutDrawElements, LayerId + 5, FMath::Clamp((T - IntroSeconds + 0.2f) / 0.2f, 0.0f, 1.0f)};
		const float DotsX = PageW - 160.0f;
		const float DotsY = PageH - 44.0f;
		Loading.Text(TEXT("LOADING"), DotsX, DotsY - 14.0f, bUpright ? 26.0f : 20.0f, Muted, ETextAlign::Right, TEXT("BlackItalic"));
		for (int32 Dot = 0; Dot < 3; ++Dot)
		{
			const float Lit = 0.5f + 0.5f * FMath::Sin(T * 7.0f - Dot * 0.9f);
			Loading.Box(DotsX + 12.0f + Dot * 16.0f, DotsY, 9.0f, 9.0f, WithAlpha(Gold, 0.35f + 0.65f * Lit));
		}
	}
	return LayerId + 11;
}

void SChaosImpactVersusReveal::Construct(const FArguments& InArgs)
{
	CreatedAt = FPlatformTime::Seconds();
	SetCanTick(false);
	RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateLambda([this](double, float)
	{
		Invalidate(EInvalidateWidgetReason::Paint);
		return IsFinished() ? EActiveTimerReturnType::Stop : EActiveTimerReturnType::Continue;
	}));
}

void SChaosImpactVersusReveal::Open()
{
	if (OpenedAt <= 0.0)
	{
		OpenedAt = FPlatformTime::Seconds();
		// The card parts to show the stage.
		ChaosImpactSfx::Play2D(nullptr, EChaosImpactSfx::MatchStarting);
	}
}

bool SChaosImpactVersusReveal::IsFinished() const
{
	return OpenedAt > 0.0 && FPlatformTime::Seconds() - OpenedAt >= OpenSeconds;
}

int32 SChaosImpactVersusReveal::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, const int32 LayerId,
	const FWidgetStyle& InWidgetStyle, const bool bParentEnabled) const
{
	if (IsFinished())
	{
		return LayerId;
	}
	const double Now = FPlatformTime::Seconds();
	const float Opening = OpenedAt > 0.0 ? FMath::Clamp(static_cast<float>(Now - OpenedAt) / OpenSeconds, 0.0f, 1.0f) : 0.0f;
	// Slow to start, quick through the middle, easing out as it reaches the corners.
	const float Open = Opening * Opening * (3.0f - 2.0f * Opening);
	const FVector2f Size = AllottedGeometry.GetLocalSize();
	const FGeometry Design = MakeDesign(AllottedGeometry);

	if (Open <= 0.0f)
	{
		PaintBackdrop(AllottedGeometry, Design, OutDrawElements, LayerId);
	}
	else
	{
		// The dark as a ring around a widening hole, its inner edge soft.
		const FVector2f Center = Size * 0.5f;
		const float Feather = 0.06f * Size.Y;
		const float Outer = Size.Size() * 0.5f + Feather * 2.0f + 10.0f;
		const float Inner = Open * Outer;
		const int32 Segments = 128;
		const FSlateRenderTransform& Transform = AllottedGeometry.GetAccumulatedRenderTransform();
		const FColor Clear = FLinearColor(Ink.R, Ink.G, Ink.B, 0.0f).ToFColor(true);
		const FColor Solid = FLinearColor(0.012f, 0.017f, 0.034f, 1.0f).ToFColor(true);
		TArray<FSlateVertex> Vertices;
		TArray<SlateIndex> Indices;
		Vertices.Reserve(Segments * 3);
		Indices.Reserve(Segments * 12);
		for (int32 Segment = 0; Segment < Segments; ++Segment)
		{
			const float Angle = UE_TWO_PI * Segment / Segments;
			const FVector2f Direction(FMath::Cos(Angle), FMath::Sin(Angle));
			Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(Transform, Center + Direction * Inner, FVector2f::ZeroVector, Clear));
			Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(Transform, Center + Direction * (Inner + Feather), FVector2f::ZeroVector, Solid));
			Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(Transform, Center + Direction * FMath::Max(Outer, Inner + Feather), FVector2f::ZeroVector, Solid));
			const SlateIndex A = Segment * 3;
			const SlateIndex B = ((Segment + 1) % Segments) * 3;
			Indices.Append({A, A + 1, B + 1, A, B + 1, B, A + 1, A + 2, B + 2, A + 1, B + 2, B + 1});
		}
		const FSlateResourceHandle White = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*FCoreStyle::Get().GetBrush("WhiteBrush"));
		FSlateDrawElement::MakeCustomVerts(OutDrawElements, LayerId, White, Vertices, Indices, nullptr, 0, 0);
		// A bright rim riding the opening edge.
		const FPainter Rim{AllottedGeometry, OutDrawElements, LayerId + 1, 1.0f - Open};
		Rim.Ring(FVector2D(Center), Inner + Feather * 0.5f, Gold, 4.0f, 128);
	}

	// The emblem the card ended on, rushing towards the viewer as the dark opens.
	const float EmblemAlpha = 1.0f - Open * 4.0f;
	if (EmblemAlpha > 0.0f)
	{
		const float Pulse = 1.0f + 0.025f * FMath::Sin(static_cast<float>(Now - CreatedAt) * 5.0f);
		const FPainter Emblem{MakeEmblem(Design, VersusCentre(IsUprightCard(AllottedGeometry)), Pulse * (1.0f + Open * 1.4f)), OutDrawElements,
			LayerId + 2, EmblemAlpha};
		PaintVersusLetters(Emblem, 210.0f);
	}
	return LayerId + 5;
}
