#pragma once
#include "ChaosImpactPaint.h"
#include "ChaosImpactBallTypes.h"

/** The VS HUD's ball drawing, kept inside the removable experiment. Main HUD unchanged. */
namespace RPGPrototypePaint
{
using namespace ChaosImpactPaint;
/** One shared layout for painted controls and pointer hit regions, at any resolution. */
struct FSharedDockLayout
{
    float Scale,W,H,Col,X;
    FSharedDockLayout(FVector2D Size,int32 Count,int32 Index)
    {
        Scale=FMath::Max(.01f,static_cast<float>(FMath::Min(Size.X/1280.,Size.Y/720.)));
        W=Size.X/Scale; H=Size.Y/Scale;
        const float Region=W/FMath::Max(1,Count);
        Col=FMath::Min(400.f,Region-8); X=Region*Index+(Region-Col)*.5f;
    }
    FVector2D BallCenter(int32 Slot) const { return FVector2D(X+Col*.5f+(Slot-1)*66,H-43); }
    FBox2D BagBounds() const { return FBox2D(FVector2D(X+12,H-188),FVector2D(X+108,H-160)); }
    FBox2D DashBounds() const { return FBox2D(FVector2D(X+9,H-92),FVector2D(X+68,H-66)); }
    FBox2D InteractionBounds() const { return FBox2D(FVector2D(X+12,H-240),FVector2D(X+Col-12,H-200)); }
};
inline void Ball(const FPainter& Paint, const FVector2D& Center, float Radius,
    EChaosImpactBallType Type, float Time, bool bFilled = true, float Growth = 0)
{
		if (!bFilled)
		{
			// An empty socket: a dark hollow with a dashed rim.
			Paint.Disc(Center, Radius, FLinearColor(0.0f, 0.0f, 0.0f, 0.45f));
			for (int32 Dash = 0; Dash < 12; ++Dash)
			{
				const float From = Dash * 30.0f + 4.0f;
				Paint.Arc(Center, Radius - 1.0f, From, From + 17.0f, WithAlpha(Muted, 0.55f), 2.0f);
			}
			return;
		}
		FLinearColor Body(0.0f, 0.62f, 1.0f, 1.0f);
		switch (Type)
		{
		case EChaosImpactBallType::Fire: Body = FLinearColor(1.0f, 0.34f, 0.04f, 1.0f); break;
		case EChaosImpactBallType::Ice: Body = FLinearColor(0.58f, 0.88f, 1.0f, 1.0f); break;
		case EChaosImpactBallType::Thunder: Body = FLinearColor(1.0f, 0.8f, 0.1f, 1.0f); break;
		case EChaosImpactBallType::Black: Body = FLinearColor(0.2f, 0.06f, 0.34f, 1.0f); break;
		case EChaosImpactBallType::Wind: Body = FLinearColor(0.2f, 0.8f, 0.32f, 1.0f); break;
		case EChaosImpactBallType::Smoke: Body = FLinearColor(0.3f, 0.28f, 0.36f, 1.0f); break;
		case EChaosImpactBallType::Beam: Body = FLinearColor(1.0f, 0.24f, 0.74f, 1.0f); break;
		case EChaosImpactBallType::Snow: Body = FLinearColor(0.9f, 0.95f, 1.0f, 1.0f); break;
		case EChaosImpactBallType::Nova: Body = FLinearColor(0.3f, 0.66f, 1.0f, 1.0f); break;
		case EChaosImpactBallType::Simae: Body = FLinearColor(0.95f, 0.97f, 1.0f, 1.0f); break;
		case EChaosImpactBallType::Drive: Body = FLinearColor(1.0f, 0.56f, 0.1f, 1.0f); break;
		default: break;
		}
		const FLinearColor Glow = ChaosImpactBallTypes::GetColor(Type);
		const bool bSpecial = Type != EChaosImpactBallType::Normal;
		FLinearColor Shade = Body * 0.5f;
		Shade.A = 1.0f;
		// Glow, drop shadow, a darker body with the lit side on top, then a glossy highlight.
		Paint.Disc(Center, Radius * (bSpecial ? 1.45f + 0.07f * FMath::Sin(Time * 5.0f) : 1.25f),
			WithAlpha(Glow, bSpecial ? 0.2f : 0.1f));
		Paint.Disc(Center + FVector2D(2.5f, 3.5f), Radius, FLinearColor(0.0f, 0.0f, 0.0f, 0.5f));
		Paint.Disc(Center, Radius, Shade);
		Paint.Disc(Center - FVector2D(Radius * 0.1f, Radius * 0.12f), Radius * 0.86f, Body);
		switch (Type)
		{
		case EChaosImpactBallType::Fire:
			// Flame tongues licking upward.
			for (int32 Tongue = 0; Tongue < 3; ++Tongue)
			{
				const float Flicker = 0.5f + 0.5f * FMath::Sin(Time * 9.0f + Tongue * 2.1f);
				Paint.Arc(Center + FVector2D((Tongue - 1) * Radius * 0.3f, Radius * (0.28f - 0.08f * Flicker)),
					Radius * 0.34f, 215.0f, 325.0f, FLinearColor(1.0f, 0.88f, 0.3f, 0.75f + 0.25f * Flicker), 2.5f);
			}
			break;
		case EChaosImpactBallType::Ice:
			// A slowly turning frost star.
			for (int32 Arm = 0; Arm < 3; ++Arm)
			{
				const float Angle = FMath::DegreesToRadians(Arm * 60.0f + Time * 25.0f);
				const FVector2D Direction(FMath::Cos(Angle), FMath::Sin(Angle));
				Paint.Line(Center - Direction * Radius * 0.55f, Center + Direction * Radius * 0.55f,
					FLinearColor(1.0f, 1.0f, 1.0f, 0.9f), 2.0f);
			}
			break;
		case EChaosImpactBallType::Thunder:
		{
			// A jagged bolt that flickers.
			const float Flicker = FMath::Frac(Time * 11.0f) < 0.8f ? 1.0f : 0.45f;
			const FVector2D Points[] = {FVector2D(-0.12f, -0.66f), FVector2D(0.2f, -0.12f), FVector2D(-0.1f, 0.0f),
				FVector2D(0.14f, 0.62f)};
			for (int32 Index = 0; Index + 1 < UE_ARRAY_COUNT(Points); ++Index)
			{
				Paint.Line(Center + Points[Index] * Radius, Center + Points[Index + 1] * Radius,
					FLinearColor(1.0f, 1.0f, 0.86f, Flicker), 3.0f);
			}
			break;
		}
		case EChaosImpactBallType::Wind:
		{
			// Three gusts curling round the middle.
			const float Swirl = FMath::Fmod(Time * 260.0f, 360.0f);
			for (int32 Gust = 0; Gust < 3; ++Gust)
			{
				const float Start = Swirl + Gust * 120.0f;
				Paint.Arc(Center, Radius * (0.28f + 0.16f * Gust), Start, Start + 150.0f,
					FLinearColor(0.88f, 1.0f, 0.84f, 0.95f - 0.15f * Gust), 2.5f);
			}
			break;
		}
		case EChaosImpactBallType::Smoke:
		{
			// Puffs of smoke rolling over each other.
			for (int32 Puff = 0; Puff < 3; ++Puff)
			{
				const float Angle = Time * 1.6f + Puff * 2.1f;
				const FVector2D Offset(FMath::Cos(Angle) * Radius * 0.28f, FMath::Sin(Angle) * Radius * 0.2f);
				Paint.Disc(Center + Offset, Radius * (0.34f + 0.05f * Puff), FLinearColor(0.7f, 0.68f, 0.78f, 0.55f));
			}
			break;
		}
		case EChaosImpactBallType::Beam:
		{
			// A shaft of light straight through it, with a four-point glint that pulses.
			const float Pulse = 0.75f + 0.25f * FMath::Sin(Time * 12.0f);
			Paint.Line(Center - FVector2D(Radius * 0.9f, 0.0f), Center + FVector2D(Radius * 0.9f, 0.0f),
				FLinearColor(1.0f, 0.5f, 0.88f, 0.7f), 7.0f);
			Paint.Line(Center - FVector2D(Radius * 0.9f, 0.0f), Center + FVector2D(Radius * 0.9f, 0.0f),
				FLinearColor(1.0f, 0.96f, 1.0f, Pulse), 2.5f);
			Paint.Line(Center - FVector2D(0.0f, Radius * 0.42f * Pulse), Center + FVector2D(0.0f, Radius * 0.42f * Pulse),
				FLinearColor(1.0f, 0.96f, 1.0f, Pulse), 2.0f);
			break;
		}
		case EChaosImpactBallType::Snow:
		{
			// Clumps of snow, and a gauge round it of how big it has grown.
			const FLinearColor Shadow(0.62f, 0.72f, 0.86f, 0.9f);
			Paint.Disc(Center + FVector2D(Radius * 0.3f, Radius * 0.28f), Radius * 0.26f, Shadow);
			Paint.Disc(Center + FVector2D(-Radius * 0.34f, Radius * 0.2f), Radius * 0.18f, Shadow);
			Paint.Disc(Center + FVector2D(Radius * 0.1f, -Radius * 0.36f), Radius * 0.14f, Shadow);
			if (Growth > 0.0f)
			{
				Paint.Arc(Center, Radius + 5.0f, -90.0f, -90.0f + 360.0f * FMath::Clamp(Growth, 0.0f, 1.0f),
					FLinearColor(1.0f, 1.0f, 1.0f, 0.95f), 3.5f);
			}
			break;
		}
		case EChaosImpactBallType::Nova:
		{
			// A white-hot heart, with light streaming into it from all round.
			const float Pulse = 0.85f + 0.15f * FMath::Sin(Time * 6.0f);
			Paint.Disc(Center, Radius * 0.62f * Pulse, FLinearColor(0.72f, 0.92f, 1.0f, 0.65f));
			Paint.Disc(Center, Radius * 0.36f * Pulse, FLinearColor(1.0f, 1.0f, 1.0f, 0.95f));
			for (int32 Ray = 0; Ray < 6; ++Ray)
			{
				const float Angle = Ray * UE_TWO_PI / 6.0f + Time * 0.8f;
				const float Phase = FMath::Frac(Time * 1.4f + Ray * 0.37f);
				const FVector2D Way(FMath::Cos(Angle), FMath::Sin(Angle));
				Paint.Line(Center + Way * Radius * (1.3f - 0.6f * Phase), Center + Way * Radius * (1.08f - 0.6f * Phase),
					FLinearColor(0.8f, 0.95f, 1.0f, 1.0f - Phase), 2.0f);
			}
			break;
		}
		case EChaosImpactBallType::Drive:
		{
			// A white-hot core with a ring of energy swinging round it, the steering in it.
			Paint.Disc(Center, Radius * 0.3f, FLinearColor(1.0f, 0.97f, 0.85f, 1.0f));
			const float Swing = FMath::Fmod(Time * 220.0f, 360.0f);
			Paint.Arc(Center, Radius * 0.62f, Swing, Swing + 200.0f, FLinearColor(1.0f, 0.86f, 0.4f, 0.95f), 2.5f);
			const float Angle = FMath::DegreesToRadians(Swing + 200.0f);
			Paint.Disc(Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius * 0.62f, Radius * 0.12f,
				FLinearColor(1.0f, 0.95f, 0.75f, 1.0f));
			break;
		}
		case EChaosImpactBallType::Simae:
		{
			// Two bead-black eyes and a tiny warm beak make the white ball readable even in split screen.
			const float Blink = FMath::Frac(Time * 0.37f) > 0.94f ? 0.18f : 1.0f;
			for (const float Side : {-1.0f, 1.0f})
			{
				Paint.Disc(Center + FVector2D(Side * Radius * 0.32f, -Radius * 0.12f),
					Radius * FVector2D(0.11f, 0.15f * Blink).Size(), FLinearColor(0.015f, 0.02f, 0.028f, 1.0f));
			}
			Paint.Line(Center + FVector2D(-Radius * 0.12f, Radius * 0.17f), Center + FVector2D(0.0f, Radius * 0.31f),
				FLinearColor(1.0f, 0.63f, 0.08f, 1.0f), 3.0f);
			Paint.Line(Center + FVector2D(0.0f, Radius * 0.31f), Center + FVector2D(Radius * 0.12f, Radius * 0.17f),
				FLinearColor(1.0f, 0.63f, 0.08f, 1.0f), 3.0f);
			break;
		}
		case EChaosImpactBallType::Black:
		{
			// A lightless core ringed in violet, with a sweep circling it.
			const float Sweep = FMath::Fmod(Time * 150.0f, 360.0f);
			Paint.Disc(Center, Radius * 0.46f, Ink);
			Paint.Ring(Center, Radius * 0.52f, FLinearColor(0.82f, 0.48f, 1.0f, 0.95f), 2.0f);
			Paint.Arc(Center, Radius * 0.72f, Sweep, Sweep + 110.0f, FLinearColor(0.92f, 0.62f, 1.0f, 0.8f), 2.0f);
			break;
		}
		default:
			// Dodgeball seams.
			Paint.Arc(Center + FVector2D(Radius * 1.05f, 0.0f), Radius * 0.8f, 140.0f, 220.0f,
				FLinearColor(0.0f, 0.1f, 0.24f, 0.85f), 2.5f);
			Paint.Arc(Center - FVector2D(Radius * 1.05f, 0.0f), Radius * 0.8f, -40.0f, 40.0f,
				FLinearColor(0.0f, 0.1f, 0.24f, 0.85f), 2.5f);
			break;
		}
		Paint.Disc(Center - FVector2D(Radius * 0.36f, Radius * 0.4f), Radius * 0.24f, FLinearColor(1.0f, 1.0f, 1.0f, 0.5f));
		Paint.Disc(Center - FVector2D(Radius * 0.5f, Radius * 0.18f), Radius * 0.08f, FLinearColor(1.0f, 1.0f, 1.0f, 0.7f));
		Paint.Ring(Center, Radius, WithAlpha(Glow, 0.95f), 2.0f);
		if (bSpecial && Type != EChaosImpactBallType::Snow && Type != EChaosImpactBallType::Simae)
		{
			// Special balls: two arcs orbiting just outside (a snowball shows its growth there instead).
			const float Spin = FMath::Fmod(Time * 160.0f, 360.0f);
			Paint.Arc(Center, Radius + 5.0f, Spin, Spin + 70.0f, Glow, 2.5f);
			Paint.Arc(Center, Radius + 5.0f, Spin + 180.0f, Spin + 250.0f, Glow, 2.5f);
		}
}
}

