#pragma once

#include "CoreMinimal.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/Geometry.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"

/** Immediate-mode Slate drawing helpers for the native in-game HUD. */
namespace ChaosImpactPaint
{
	inline const FLinearColor Ink(0.006f, 0.009f, 0.018f, 1.0f);
	inline const FLinearColor Fire(1.0f, 0.055f, 0.12f, 1.0f);
	inline const FLinearColor Ice(0.0f, 0.55f, 1.0f, 1.0f);
	inline const FLinearColor Gold(1.0f, 0.68f, 0.06f, 1.0f);
	inline const FLinearColor Paper(0.96f, 0.975f, 1.0f, 1.0f);
	inline const FLinearColor Muted(0.48f, 0.54f, 0.64f, 1.0f);
	inline const FLinearColor Violet(0.55f, 0.28f, 1.0f, 1.0f);
	inline const FLinearColor PlayerAccents[] = {Ice, Fire, Gold, Violet};

	inline FLinearColor WithAlpha(FLinearColor Color, const float Alpha)
	{
		Color.A *= FMath::Clamp(Alpha, 0.0f, 1.0f);
		return Color;
	}

	inline float EaseOut(const float T)
	{
		return 1.0f - FMath::Pow(1.0f - FMath::Clamp(T, 0.0f, 1.0f), 3.0f);
	}

	/** A child space anchored at (X, Y) of Parent, scaled uniformly for split-screen sizes. */
	inline FGeometry MakeAnchor(const FGeometry& Parent, const float X, const float Y, const float Scale)
	{
		return Parent.MakeChild(FVector2f(4000.0f, 4000.0f), FSlateLayoutTransform(Scale, FVector2f(X, Y)));
	}

	/** A child space that leans forward ("/") and optionally scales about its center. */
	inline FGeometry MakeSkewed(const FGeometry& Parent, const float X, const float Y, const float W,
		const float H, const float Shear, const float Scale = 1.0f)
	{
		const FSlateRenderTransform Render(TMatrix2x2<float>(Scale, 0.0f, Scale * Shear, Scale));
		return Parent.MakeChild(FVector2f(W, H), FSlateLayoutTransform(FVector2f(X, Y)),
			Render, FVector2f(0.5f, 0.5f));
	}

	enum class ETextAlign : uint8 { Left, Center, Right };

	struct FPainter
	{
		const FGeometry& Geometry;
		FSlateWindowElementList& Elements;
		int32 Layer;
		float Alpha = 1.0f;

		void Box(float X, float Y, float W, float H, FLinearColor Color) const
		{
			if (W <= 0.0f || H <= 0.0f || Alpha * Color.A <= 0.001f)
			{
				return;
			}
			FSlateDrawElement::MakeBox(Elements, Layer,
				Geometry.ToPaintGeometry(FVector2f(W, H), FSlateLayoutTransform(FVector2f(X, Y))),
				FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, WithAlpha(Color, Alpha));
		}

		void Line(FVector2D From, FVector2D To, FLinearColor Color, float Width = 1.0f) const
		{
			const TArray<FVector2D> Points{From, To};
			FSlateDrawElement::MakeLines(Elements, Layer, Geometry.ToPaintGeometry(),
				Points, ESlateDrawEffect::None, WithAlpha(Color, Alpha), true, Width);
		}

		void Ring(FVector2D Center, float Radius, FLinearColor Color, float Width, int32 Segments = 48) const
		{
			if (Radius <= 0.0f || Alpha * Color.A <= 0.001f)
			{
				return;
			}
			TArray<FVector2D> Points;
			Points.Reserve(Segments + 1);
			for (int32 Segment = 0; Segment <= Segments; ++Segment)
			{
				const float Angle = UE_TWO_PI * Segment / Segments;
				Points.Add(Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
			}
			FSlateDrawElement::MakeLines(Elements, Layer, Geometry.ToPaintGeometry(),
				Points, ESlateDrawEffect::None, WithAlpha(Color, Alpha), true, Width);
		}

		/** A filled circle. A fully rounded box stays smooth where a thick line strip would spike. */
		void Disc(FVector2D Center, float Radius, FLinearColor Color) const
		{
			if (Radius <= 0.0f || Alpha * Color.A <= 0.001f)
			{
				return;
			}
			static const FSlateRoundedBoxBrush DiscBrush(FLinearColor::White);
			FSlateDrawElement::MakeBox(Elements, Layer,
				Geometry.ToPaintGeometry(FVector2f(Radius * 2.0f, Radius * 2.0f),
					FSlateLayoutTransform(FVector2f(Center.X - Radius, Center.Y - Radius))),
				&DiscBrush, ESlateDrawEffect::None, WithAlpha(Color, Alpha));
		}

		void Outline(float X, float Y, float W, float H, FLinearColor Color, float Width) const
		{
			Box(X, Y, W, Width, Color);
			Box(X, Y + H - Width, W, Width, Color);
			Box(X, Y, Width, H, Color);
			Box(X + W - Width, Y, Width, H, Color);
		}

		void Text(const FString& Value, float X, float Y, float Size, FLinearColor Color,
			ETextAlign Align = ETextAlign::Left, FName Face = TEXT("Black"),
			float OutlineSize = 0.0f, FLinearColor OutlineColor = Ink) const
		{
			if (Value.IsEmpty() || Alpha * Color.A <= 0.001f)
			{
				return;
			}
			const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(Face, Size,
				FFontOutlineSettings(FMath::RoundToInt(OutlineSize), WithAlpha(OutlineColor, Alpha * Color.A)));
			if (Align != ETextAlign::Left)
			{
				const float Width = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()
					->Measure(Value, Font).X;
				X -= Align == ETextAlign::Center ? Width * 0.5f : Width;
			}
			const FVector2f Area(2400.0f, Size * 2.4f);
			FSlateDrawElement::MakeText(Elements, Layer,
				Geometry.ToPaintGeometry(Area, FSlateLayoutTransform(FVector2f(X + 4.0f, Y + 5.0f))),
				Value, FCoreStyle::GetDefaultFontStyle(Face, Size), ESlateDrawEffect::None,
				FLinearColor(0.0f, 0.0f, 0.0f, 0.6f * Alpha * Color.A));
			FSlateDrawElement::MakeText(Elements, Layer,
				Geometry.ToPaintGeometry(Area, FSlateLayoutTransform(FVector2f(X, Y))),
				Value, Font, ESlateDrawEffect::None, WithAlpha(Color, Alpha));
		}
	};
}
