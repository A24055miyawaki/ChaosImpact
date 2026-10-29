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

		/** An arc between two angles in degrees (0 = right, increasing clockwise on screen). */
		void Arc(FVector2D Center, float Radius, float FromDegrees, float ToDegrees, FLinearColor Color, float Width) const
		{
			if (Radius <= 0.0f || ToDegrees <= FromDegrees || Alpha * Color.A <= 0.001f)
			{
				return;
			}
			const int32 Steps = FMath::Max(2, FMath::CeilToInt((ToDegrees - FromDegrees) / 5.0f));
			TArray<FVector2D> Points;
			Points.Reserve(Steps + 1);
			for (int32 Step = 0; Step <= Steps; ++Step)
			{
				const float Angle = FMath::DegreesToRadians(FMath::Lerp(FromDegrees, ToDegrees, static_cast<float>(Step) / Steps));
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
			// Drawn once: an offset drop-shadow copy read as doubled text.
			FSlateDrawElement::MakeText(Elements, Layer,
				Geometry.ToPaintGeometry(Area, FSlateLayoutTransform(FVector2f(X, Y))),
				Value, Font, ESlateDrawEffect::None, WithAlpha(Color, Alpha));
		}
	};
	/** A rounded brush of about this radius, kept for the whole run (draw elements refer to it until drawn). */
	inline const FSlateBrush& RoundedBrush(const float Radius)
	{
		static TMap<int32, TSharedPtr<FSlateRoundedBoxBrush>> Brushes;
		const int32 Key = FMath::Clamp(FMath::RoundToInt(Radius), 1, 64);
		TSharedPtr<FSlateRoundedBoxBrush>& Brush = Brushes.FindOrAdd(Key);
		if (!Brush.IsValid())
		{
			Brush = MakeShared<FSlateRoundedBoxBrush>(FLinearColor::White, static_cast<float>(Key));
		}
		return *Brush;
	}

	// ---- The battle HUD's look, after the title logo: fire and ice, pieces with broken, shard-like edges, a thick navy
	// outline and a dark blue extrusion behind each one, numbers in heavy italics standing out the same way.
	// Shapes take up three layers (extrusion, outline, fill): draw what goes on them three layers up.

	inline const FLinearColor Navy(0.016f, 0.024f, 0.075f, 1.0f);
	inline const FLinearColor Extrude(0.05f, 0.09f, 0.26f, 1.0f);

	/** A gradient of three colours, top to bottom. */
	struct FHudFill
	{
		FLinearColor Top;
		FLinearColor Middle;
		FLinearColor Bottom;

		FLinearColor At(const float T) const
		{
			const float C = FMath::Clamp(T, 0.0f, 1.0f);
			return C < 0.5f ? FMath::Lerp(Top, Middle, C * 2.0f) : FMath::Lerp(Middle, Bottom, C * 2.0f - 1.0f);
		}
	};
	inline const FHudFill FireFill{FLinearColor(1.0f, 0.95f, 0.38f, 1.0f), FLinearColor(1.0f, 0.5f, 0.06f, 1.0f), FLinearColor(0.85f, 0.12f, 0.03f, 1.0f)};
	inline const FHudFill IceFill{FLinearColor(0.92f, 0.99f, 1.0f, 1.0f), FLinearColor(0.3f, 0.72f, 1.0f, 1.0f), FLinearColor(0.05f, 0.22f, 0.78f, 1.0f)};
	inline const FHudFill NightFill{FLinearColor(0.11f, 0.16f, 0.34f, 0.95f), FLinearColor(0.05f, 0.08f, 0.2f, 0.95f), FLinearColor(0.02f, 0.03f, 0.09f, 0.95f)};
	inline const FHudFill SilverFill{FLinearColor(1.0f, 1.0f, 1.0f, 1.0f), FLinearColor(0.72f, 0.8f, 0.92f, 1.0f), FLinearColor(0.4f, 0.48f, 0.62f, 1.0f)};
	inline const FHudFill BronzeFill{FLinearColor(1.0f, 0.8f, 0.5f, 1.0f), FLinearColor(0.82f, 0.45f, 0.18f, 1.0f), FLinearColor(0.48f, 0.22f, 0.08f, 1.0f)};
	inline FHudFill SolidFill(const FLinearColor& Color) { return {Color, Color, Color}; }
	inline FHudFill MakeFill(const FLinearColor& Top, const FLinearColor& Bottom) { return {Top, FMath::Lerp(Top, Bottom, 0.5f), Bottom}; }

	/** A painter for what goes on top of a shape drawn with P (Up layers above it). */
	inline FPainter HudAbove(const FPainter& P, const int32 Up = 3) { return FPainter{P.Geometry, P.Elements, P.Layer + Up, P.Alpha}; }

	/** Filled triangles: points in the painter's space, a colour per point. */
	inline void HudTriangles(const FPainter& P, const TArray<FVector2D>& Points, const TArray<FLinearColor>& Colors,
		const TArray<SlateIndex>& Indexes)
	{
		if (Points.Num() < 3 || Indexes.Num() < 3 || P.Alpha <= 0.001f || !FSlateApplication::IsInitialized())
		{
			return;
		}
		const FSlateResourceHandle Handle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*FCoreStyle::Get().GetBrush("WhiteBrush"));
		const FSlateRenderTransform& Transform = P.Geometry.GetAccumulatedRenderTransform();
		TArray<FSlateVertex> Verts;
		Verts.Reserve(Points.Num());
		for (int32 Index = 0; Index < Points.Num(); ++Index)
		{
			Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(Transform, FVector2f(Points[Index]), FVector2f(0.5f, 0.5f),
				WithAlpha(Colors[Index], P.Alpha).ToFColor(true)));
		}
		FSlateDrawElement::MakeCustomVerts(P.Elements, P.Layer, Handle, Verts, Indexes, nullptr, 0, 0);
	}

	/** The part of a convex polygon between two heights (Y0 above Y1), or left of X when bVertical. */
	inline TArray<FVector2D> HudClip(const TArray<FVector2D>& Poly, const double Limit, const bool bKeepGreater, const bool bVertical = false)
	{
		TArray<FVector2D> Out;
		for (int32 Index = 0; Index < Poly.Num(); ++Index)
		{
			const FVector2D& A = Poly[Index];
			const FVector2D& B = Poly[(Index + 1) % Poly.Num()];
			const double AV = bVertical ? A.X : A.Y;
			const double BV = bVertical ? B.X : B.Y;
			const bool bAIn = bKeepGreater ? AV >= Limit : AV <= Limit;
			const bool bBIn = bKeepGreater ? BV >= Limit : BV <= Limit;
			if (bAIn)
			{
				Out.Add(A);
			}
			if (bAIn != bBIn)
			{
				Out.Add(A + (B - A) * ((Limit - AV) / (BV - AV)));
			}
		}
		return Out;
	}

	/**
	 * A convex polygon filled with a top-to-bottom gradient, over its own height or (FromY < ToY) over that range, so
	 * the pieces of one shape share one gradient.
	 */
	inline void HudPoly(const FPainter& P, const TArray<FVector2D>& Poly, const FHudFill& Fill, const int32 Bands = 6,
		const double FromY = 0.0, const double ToY = 0.0)
	{
		if (Poly.Num() < 3)
		{
			return;
		}
		double Top = Poly[0].Y;
		double Bottom = Poly[0].Y;
		for (const FVector2D& Point : Poly)
		{
			Top = FMath::Min(Top, Point.Y);
			Bottom = FMath::Max(Bottom, Point.Y);
		}
		const double GradientTop = FromY < ToY ? FromY : Top;
		const double GradientHeight = FromY < ToY ? ToY - FromY : FMath::Max(Bottom - Top, 0.001);
		const double Height = FMath::Max(Bottom - Top, 0.001);
		TArray<FVector2D> Points;
		TArray<FLinearColor> Colors;
		TArray<SlateIndex> Indexes;
		for (int32 Band = 0; Band < Bands; ++Band)
		{
			const TArray<FVector2D> Piece = HudClip(HudClip(Poly, Top + Height * Band / Bands, true), Top + Height * (Band + 1) / Bands, false);
			if (Piece.Num() < 3)
			{
				continue;
			}
			const SlateIndex Base = static_cast<SlateIndex>(Points.Num());
			for (const FVector2D& Point : Piece)
			{
				Points.Add(Point);
				Colors.Add(Fill.At(static_cast<float>((Point.Y - GradientTop) / GradientHeight)));
			}
			for (int32 Index = 1; Index + 1 < Piece.Num(); ++Index)
			{
				Indexes.Add(Base);
				Indexes.Add(Base + Index);
				Indexes.Add(Base + Index + 1);
			}
		}
		HudTriangles(P, Points, Colors, Indexes);
	}

	/** A convex polygon grown outward by Amount all round (for its outline). */
	inline TArray<FVector2D> HudInflate(const TArray<FVector2D>& Poly, const float Amount)
	{
		const int32 Count = Poly.Num();
		if (Count < 3)
		{
			return Poly;
		}
		double Area = 0.0;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const FVector2D& A = Poly[Index];
			const FVector2D& B = Poly[(Index + 1) % Count];
			Area += A.X * B.Y - B.X * A.Y;
		}
		const double Side = Area > 0.0 ? 1.0 : -1.0;
		TArray<FVector2D> Out;
		Out.Reserve(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const FVector2D& Previous = Poly[(Index + Count - 1) % Count];
			const FVector2D& Current = Poly[Index];
			const FVector2D& Next = Poly[(Index + 1) % Count];
			const FVector2D In = (Current - Previous).GetSafeNormal();
			const FVector2D Out2 = (Next - Current).GetSafeNormal();
			const FVector2D NormalA = FVector2D(In.Y, -In.X) * Side;
			const FVector2D NormalB = FVector2D(Out2.Y, -Out2.X) * Side;
			const FVector2D Between = (NormalA + NormalB).GetSafeNormal();
			const double Cos = FMath::Max(FVector2D::DotProduct(Between, NormalA), 0.35);
			Out.Add(Current + Between * Amount / Cos);
		}
		return Out;
	}

	/** Points of a polygon moved by Offset. */
	inline TArray<FVector2D> HudMove(TArray<FVector2D> Poly, const FVector2D& Offset)
	{
		for (FVector2D& Point : Poly)
		{
			Point += Offset;
		}
		return Poly;
	}

	/** A shard: its dark extrusion behind (offset by Drop), a thick navy outline, then its fill (three layers). */
	inline void HudShard(const FPainter& P, const TArray<FVector2D>& Poly, const FHudFill& Fill, const float Outline = 3.0f,
		const FVector2D& Drop = FVector2D(5.0, 6.0), const FLinearColor& Edge = Navy, const double FromY = 0.0, const double ToY = 0.0)
	{
		const TArray<FVector2D> Outer = HudInflate(Poly, Outline);
		if (!Drop.IsNearlyZero())
		{
			HudPoly(P, HudMove(Outer, Drop), SolidFill(WithAlpha(Extrude, Fill.Bottom.A)), 1);
		}
		HudPoly(FPainter{P.Geometry, P.Elements, P.Layer + 1, P.Alpha}, Outer, SolidFill(WithAlpha(Edge, Fill.Bottom.A)), 1);
		HudPoly(FPainter{P.Geometry, P.Elements, P.Layer + 2, P.Alpha}, Poly, Fill, 6, FromY, ToY);
	}

	/** A line through several points (not closed). */
	inline void HudStroke(const FPainter& P, const TArray<FVector2D>& Points, const FLinearColor& Color, const float Width)
	{
		if (Points.Num() >= 2 && P.Alpha * Color.A > 0.001f)
		{
			FSlateDrawElement::MakeLines(P.Elements, P.Layer, P.Geometry.ToPaintGeometry(), Points, ESlateDrawEffect::None,
				WithAlpha(Color, P.Alpha), true, Width);
		}
	}

	/**
	 * The ring of points of a jagged burst round Center: Spikes points out to Outer (each a little different) with the
	 * dips between at Inner, stretched by Stretch, turned by Spin degrees.
	 */
	inline TArray<FVector2D> HudBurstRing(const FVector2D& Center, const float Inner, const float Outer, const int32 Spikes,
		const float Spin = 0.0f, const FVector2D& Stretch = FVector2D(1.0, 1.0), const float Seed = 0.0f)
	{
		TArray<FVector2D> Ring;
		for (int32 Index = 0; Index < Spikes * 2; ++Index)
		{
			const float Angle = FMath::DegreesToRadians(Spin + 180.0f * Index / Spikes);
			const float Jitter = FMath::Frac(FMath::Sin((Index + Seed * 7.0f) * 12.9898f) * 43758.5453f);
			const float Radius = Index % 2 == 0 ? Outer * (0.78f + 0.3f * Jitter) : Inner * (0.92f + 0.12f * Jitter);
			Ring.Add(Center + FVector2D(FMath::Cos(Angle) * Stretch.X, FMath::Sin(Angle) * Stretch.Y) * Radius);
		}
		return Ring;
	}

	/** A star-shaped ring filled from Center (its colours top to bottom). */
	inline void HudFan(const FPainter& P, const FVector2D& Center, const TArray<FVector2D>& Ring, const FHudFill& Fill)
	{
		if (Ring.Num() < 3)
		{
			return;
		}
		double Top = Center.Y;
		double Bottom = Center.Y;
		for (const FVector2D& Point : Ring)
		{
			Top = FMath::Min(Top, Point.Y);
			Bottom = FMath::Max(Bottom, Point.Y);
		}
		const double Height = FMath::Max(Bottom - Top, 0.001);
		TArray<FVector2D> Points{Center};
		TArray<FLinearColor> Colors{Fill.At(static_cast<float>((Center.Y - Top) / Height))};
		TArray<SlateIndex> Indexes;
		for (const FVector2D& Point : Ring)
		{
			Points.Add(Point);
			Colors.Add(Fill.At(static_cast<float>((Point.Y - Top) / Height)));
		}
		for (int32 Index = 0; Index < Ring.Num(); ++Index)
		{
			Indexes.Add(0);
			Indexes.Add(static_cast<SlateIndex>(1 + Index));
			Indexes.Add(static_cast<SlateIndex>(1 + (Index + 1) % Ring.Num()));
		}
		HudTriangles(P, Points, Colors, Indexes);
	}

	/** A jagged burst with its extrusion and outline (three layers). */
	inline void HudBurst(const FPainter& P, const FVector2D& Center, const float Inner, const float Outer, const int32 Spikes,
		const FHudFill& Fill, const float Spin = 0.0f, const FVector2D& Stretch = FVector2D(1.0, 1.0), const float Outline = 4.0f,
		const FVector2D& Drop = FVector2D(6.0, 7.0), const float Seed = 0.0f)
	{
		const TArray<FVector2D> Outer2 = HudBurstRing(Center, Inner + Outline, Outer + Outline * 1.6f, Spikes, Spin, Stretch, Seed);
		if (!Drop.IsNearlyZero())
		{
			HudFan(P, Center + Drop, HudMove(Outer2, Drop), SolidFill(Extrude));
		}
		HudFan(FPainter{P.Geometry, P.Elements, P.Layer + 1, P.Alpha}, Center, Outer2, SolidFill(Navy));
		HudFan(FPainter{P.Geometry, P.Elements, P.Layer + 2, P.Alpha}, Center, HudBurstRing(Center, Inner, Outer, Spikes, Spin, Stretch, Seed), Fill);
	}

	/** Text standing out like the logo's letters: a thick navy outline and a dark blue copy behind it (two layers). */
	inline void HudText3D(const FPainter& P, const FString& Value, const float X, const float Y, const float Size, const FLinearColor& Color,
		const ETextAlign Align = ETextAlign::Left, const FName Face = TEXT("BlackItalic"), const float Depth = 0.0f)
	{
		const float Outline = FMath::Max(2.0f, Size * 0.07f);
		const float Push = Depth > 0.0f ? Depth : FMath::Max(2.0f, Size * 0.06f);
		P.Text(Value, X + Push * 0.8f, Y + Push, Size, WithAlpha(Extrude, Color.A), Align, Face, Outline, WithAlpha(Extrude, Color.A));
		FPainter{P.Geometry, P.Elements, P.Layer + 1, P.Alpha}.Text(Value, X, Y, Size, Color, Align, Face, Outline, Navy);
	}

	/** How wide a piece of text is drawn. */
	inline float HudMeasure(const FString& Value, const float Size, const FName Face = TEXT("Black"))
	{
		if (Value.IsEmpty() || !FSlateApplication::IsInitialized())
		{
			return 0.0f;
		}
		return static_cast<float>(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()
			->Measure(Value, FCoreStyle::GetDefaultFontStyle(Face, Size)).X);
	}

	/** A few small pieces flying out from Center as T goes 0 to 1 (a shape breaking). */
	inline void HudShatter(const FPainter& P, const FVector2D& Center, const float T, const float Size, const FHudFill& Fill,
		const int32 Pieces = 6, const float Seed = 0.0f)
	{
		const float Out = 1.0f - FMath::Pow(1.0f - FMath::Clamp(T, 0.0f, 1.0f), 3.0f);
		const FPainter Fade{P.Geometry, P.Elements, P.Layer, P.Alpha * (1.0f - FMath::Clamp(T, 0.0f, 1.0f))};
		for (int32 Piece = 0; Piece < Pieces; ++Piece)
		{
			const float R = FMath::Frac(FMath::Sin((Piece + 1) * 78.233f + Seed * 3.1f) * 43758.5453f);
			const float Angle = FMath::DegreesToRadians(360.0f * Piece / Pieces + 40.0f * R);
			const FVector2D Way(FMath::Cos(Angle), FMath::Sin(Angle));
			const FVector2D At = Center + Way * Size * (0.4f + 1.6f * Out) + FVector2D(0.0, Size * 1.2f * Out * Out);
			const float Spin = Angle + T * (6.0f + 6.0f * R);
			const float L = Size * (0.35f + 0.3f * R);
			const TArray<FVector2D> Tri{At + FVector2D(FMath::Cos(Spin), FMath::Sin(Spin)) * L,
				At + FVector2D(FMath::Cos(Spin + 2.3f), FMath::Sin(Spin + 2.3f)) * L * 0.7f,
				At + FVector2D(FMath::Cos(Spin + 4.0f), FMath::Sin(Spin + 4.0f)) * L * 0.8f};
			HudPoly(Fade, HudInflate(Tri, 2.0f), SolidFill(Navy), 1);
			HudPoly(FPainter{P.Geometry, P.Elements, P.Layer + 1, Fade.Alpha}, Tri, Fill, 2);
		}
	}

	/** A filled box with rounded corners. */
	inline void RoundedBox(const FPainter& P, const float X, const float Y, const float W, const float H, const float Radius,
		const FLinearColor& Color)
	{
		if (W <= 0.0f || H <= 0.0f || P.Alpha * Color.A <= 0.001f)
		{
			return;
		}
		FSlateDrawElement::MakeBox(P.Elements, P.Layer,
			P.Geometry.ToPaintGeometry(FVector2f(W, H), FSlateLayoutTransform(FVector2f(X, Y))),
			&RoundedBrush(FMath::Min(Radius, FMath::Min(W, H) * 0.5f)), ESlateDrawEffect::None, WithAlpha(Color, P.Alpha));
	}

	/**
	 * The waiting animation (searching for a room, making one, waiting for players): three balls, ice, fire and gold,
	 * hopping in turn with an ease in and out, squashing as they land, their shadows growing and shrinking under them.
	 * Center is where they stand; Size a ball's diameter.
	 */
	inline void HudWaitBalls(const FPainter& P, const FVector2D& Center, const float Time, const float Size = 34.0f)
	{
		static const FHudFill Fills[] = {IceFill, FireFill, MakeFill(FLinearColor(1.0f, 0.93f, 0.5f, 1.0f), FLinearColor(0.9f, 0.45f, 0.02f, 1.0f))};
		constexpr float Period = 1.2f;
		for (int32 Ball = 0; Ball < 3; ++Ball)
		{
			// Each hop: up with an ease out, down with an ease in; the next ball a third of a beat later.
			const float Phase = FMath::Frac(Time / Period - Ball * 0.16f);
			const float Hop = Phase < 0.55f ? FMath::Sin(Phase / 0.55f * UE_PI) : 0.0f;
			const float Land = Phase >= 0.55f && Phase < 0.7f ? FMath::Sin((Phase - 0.55f) / 0.15f * UE_PI) : 0.0f;
			const float Height = Hop * Hop * (3.0f - 2.0f * Hop) * Size * 1.9f;
			const float SquashX = 1.0f + 0.22f * Land - 0.08f * Hop;
			const float SquashY = 1.0f - 0.24f * Land + 0.1f * Hop;
			const FVector2D Foot = Center + FVector2D((Ball - 1) * Size * 1.6f, 0.0);
			// The shadow: smaller and fainter the higher it is.
			const float ShadowW = Size * (0.95f - 0.4f * Hop / 1.0f);
			P.Box(Foot.X - ShadowW * 0.5f, Foot.Y - 3.0f, ShadowW, 6.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.35f - 0.2f * Hop));
			TArray<FVector2D> Disc;
			for (int32 Point = 0; Point < 20; ++Point)
			{
				const float A = Point * UE_TWO_PI / 20.0f;
				Disc.Add(Foot + FVector2D(FMath::Cos(A) * Size * 0.5f * SquashX, -Size * 0.5f * SquashY + FMath::Sin(A) * Size * 0.5f * SquashY - Height));
			}
			HudPoly(P, HudInflate(Disc, 2.5f), SolidFill(Navy), 1);
			HudPoly(FPainter{P.Geometry, P.Elements, P.Layer + 1, P.Alpha}, Disc, Fills[Ball], 5);
			// A glint on top.
			const FVector2D Glint = Foot + FVector2D(-Size * 0.16f * SquashX, -Size * 0.72f * SquashY - Height);
			P.Line(Glint, Glint + FVector2D(Size * 0.14f, -Size * 0.06f), WithAlpha(Paper, 0.9f), 3.0f);
		}
	}

	/** Minutes and seconds since a wait began ("0:07"). */
	inline FString HudElapsed(const float Seconds)
	{
		const int32 Whole = FMath::Max(0, FMath::FloorToInt(Seconds));
		return FString::Printf(TEXT("%d:%02d"), Whole / 60, Whole % 60);
	}
}
