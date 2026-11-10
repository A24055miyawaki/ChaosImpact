#include "BRIsland.h"
#include "BRIslandLayout.h"

namespace
{
	float BRStep(const float Edge0, const float Edge1, const float X)
	{
		const float T = FMath::Clamp((X - Edge0) / (Edge1 - Edge0), 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}

	float BRHash(const int32 X, const int32 Y)
	{
		uint32 H = static_cast<uint32>(X) * 374761393u + static_cast<uint32>(Y) * 668265263u;
		H = (H ^ (H >> 13)) * 1274126177u;
		return static_cast<float>((H ^ (H >> 16)) & 0xFFFFu) / 65535.0f;
	}

	/** Smooth value noise, -1 to 1. */
	float BRNoise(const float X, const float Y)
	{
		const int32 IX = FMath::FloorToInt(X), IY = FMath::FloorToInt(Y);
		const float FX = X - IX, FY = Y - IY;
		const float U = FX * FX * (3.0f - 2.0f * FX), V = FY * FY * (3.0f - 2.0f * FY);
		const float A = FMath::Lerp(BRHash(IX, IY), BRHash(IX + 1, IY), U);
		const float B = FMath::Lerp(BRHash(IX, IY + 1), BRHash(IX + 1, IY + 1), U);
		return FMath::Lerp(A, B, V) * 2.0f - 1.0f;
	}

	float BRFbm(const float X, const float Y, const int32 Octaves = 3)
	{
		float Sum = 0.0f, Amp = 0.5f, Freq = 1.0f;
		for (int32 I = 0; I < Octaves; ++I)
		{
			Sum += BRNoise(X * Freq + I * 17.3f, Y * Freq - I * 9.1f) * Amp;
			Amp *= 0.5f;
			Freq *= 2.05f;
		}
		return Sum;
	}

	float BRSegmentDistance(const FVector2D& P, const FVector2D& A, const FVector2D& B)
	{
		const FVector2D AB = B - A;
		const double Len = AB.SizeSquared();
		const double T = Len > 0.0 ? FMath::Clamp(FVector2D::DotProduct(P - A, AB) / Len, 0.0, 1.0) : 0.0;
		return static_cast<float>(FVector2D::Distance(P, A + AB * T));
	}

	float BRLineDistance(const FVector2D& P, const TArray<FVector2D>& Line)
	{
		float Best = TNumericLimits<float>::Max();
		for (int32 I = 0; I + 1 < Line.Num(); ++I)
		{
			Best = FMath::Min(Best, BRSegmentDistance(P, Line[I], Line[I + 1]));
		}
		return Best;
	}

	/** Catmull-Rom through the points, so a road or river never bends sharply. */
	TArray<FVector2D> BRCatmull(const TArray<FVector2D>& Points, const int32 Steps = 8)
	{
		TArray<FVector2D> Out;
		for (int32 I = 0; I + 1 < Points.Num(); ++I)
		{
			const FVector2D P0 = Points[FMath::Max(I - 1, 0)], P1 = Points[I], P2 = Points[I + 1], P3 = Points[FMath::Min(I + 2, Points.Num() - 1)];
			for (int32 Step = 0; Step < Steps; ++Step)
			{
				const double T = static_cast<double>(Step) / Steps, T2 = T * T, T3 = T2 * T;
				Out.Add(0.5 * (2.0 * P1 + (P2 - P0) * T + (2.0 * P0 - 5.0 * P1 + 4.0 * P2 - P3) * T2 + (3.0 * P1 - P0 - 3.0 * P2 + P3) * T3));
			}
		}
		Out.Add(Points.Last());
		return Out;
	}

	const TArray<FVector2D>& BRRiverLine()
	{
		static const TArray<FVector2D> Line = BRCatmull(BRLayout::RiverPoints());
		return Line;
	}

	const TArray<TArray<FVector2D>>& BRCanyonLines()
	{
		static const TArray<TArray<FVector2D>> Lines = []
		{
			TArray<TArray<FVector2D>> Out;
			for (const TArray<FVector2D>& Points : BRLayout::CanyonChannels()) { Out.Add(BRCatmull(Points)); }
			return Out;
		}();
		return Lines;
	}

	float BRRoadDistance(const FVector2D& P)
	{
		float Best = TNumericLimits<float>::Max();
		for (const TArray<FVector2D>& Line : BRMap::Roads())
		{
			Best = FMath::Min(Best, BRLineDistance(P, Line));
		}
		return Best;
	}

	float BRCoastRadius(const float Angle)
	{
		// A big island with bays and headlands, not a circle.
		return BRLayout::IslandRadius * (1.0f + 0.075f * FMath::Sin(3.0f * Angle + 0.9f) + 0.05f * FMath::Sin(5.0f * Angle + 2.4f)
			+ 0.028f * FMath::Sin(9.0f * Angle + 0.3f) + 0.014f * FMath::Sin(17.0f * Angle + 4.0f));
	}
}

const TArray<TArray<FVector2D>>& BRMap::Roads()
{
	static const TArray<TArray<FVector2D>> Lines = []
	{
		TArray<TArray<FVector2D>> Out;
		for (const TArray<FVector2D>& Points : BRLayout::RoadPoints()) { Out.Add(BRCatmull(Points)); }
		return Out;
	}();
	return Lines;
}

float BRMap::ShoreDistance(const float X, const float Y)
{
	const float Radius = FMath::Sqrt(X * X + Y * Y);
	return BRCoastRadius(FMath::Atan2(Y, X)) + BRFbm(X / 5200.0f, Y / 5200.0f, 4) * 2400.0f - Radius;
}

const TArray<FVector2D>& BRMap::Places()
{
	static const TArray<FVector2D> List = []
	{
		using namespace BRLayout;
		FRandomStream Random(207);
		TArray<FVector2D> Out;
		for (int32 Try = 0; Try < 8000 && Out.Num() < 74; ++Try)
		{
			const FVector2D At(Random.FRandRange(-74000.0f, 74000.0f), Random.FRandRange(-74000.0f, 74000.0f));
			if (!BRMap::IsOpenGround(At.X, At.Y) || BRMap::ShoreDistance(At.X, At.Y) < 5000.0f) { continue; }
			bool bClear = FVector2D::Distance(At, Mountain.At) > Mountain.Radius * 0.55f && FVector2D::Distance(At, Canyon.At) > Canyon.Radius + 2000.0f && FVector2D::Distance(At, Forest.At) > Forest.Radius * 0.6f;
			for (const FBRDistrict* D : Flat()) { bClear = bClear && FVector2D::Distance(At, D->At) > D->Radius + D->Blend + 800.0f; }
			for (const FVector2D& Other : Out) { bClear = bClear && FVector2D::Distance(At, Other) > 8500.0; }
			if (bClear) { Out.Add(At); }
		}
		return Out;
	}();
	return List;
}

static float BRHeightBase(const float X, const float Y);

float BRMap::HeightAt(const float X, const float Y)
{
	static const TArray<float> PlaceHeights = []
	{
		TArray<float> Out;
		for (const FVector2D& P : BRMap::Places()) { Out.Add(BRHeightBase(P.X, P.Y)); }
		return Out;
	}();
	float H = BRHeightBase(X, Y);
	const TArray<FVector2D>& List = Places();
	const FVector2D Here(X, Y);
	for (int32 I = 0; I < List.Num(); ++I)
	{
		const float D = static_cast<float>(FVector2D::Distance(Here, List[I]));
		if (D < 4600.0f) { H = FMath::Lerp(H, PlaceHeights[I], BRStep(4600.0f, 2600.0f, D)); }
	}
	return H;
}

static float BRHeightBase(const float X, const float Y)
{
	using namespace BRLayout;
	const FVector2D P(X, Y);
	const float Shore = BRMap::ShoreDistance(X, Y);
	// Rolling land, higher inland; the fine bumps are left off the roads.
	const float Road = BRStep(RoadWidth + 700.0f, RoadWidth * 0.6f, BRRoadDistance(P));
	float Land = 260.0f + FMath::Max(0.0f, (BRFbm(X / 15000.0f, Y / 15000.0f, 4) + 0.42f) * 1250.0f);
	// Rolling everywhere: mounds and hollows at a walk's scale, finer bumps on top. No stretch of it is level.
	Land += BRFbm(X / 4300.0f, Y / 4300.0f, 3) * 330.0f * (1.0f - Road * 0.75f);
	Land += BRFbm(X / 1500.0f, Y / 1500.0f, 2) * 90.0f * (1.0f - Road);
	float H = FMath::Lerp(-240.0f, Land, BRStep(-1000.0f, 3400.0f, Shore));
	if (Shore < -1000.0f) { H = -240.0f + (Shore + 1000.0f) * 0.02f; }

	// The snow mountain: ridged, gentle enough to climb anywhere.
	const float MountainD = FVector2D::Distance(P, Mountain.At);
	if (MountainD < Mountain.Radius)
	{
		const float T = 1.0f - MountainD / Mountain.Radius;
		const float Ridge = 1.0f - FMath::Abs(BRNoise(X / 5200.0f, Y / 5200.0f));
		H += MountainPeak * FMath::Pow(T, 1.35f) * (0.72f + 0.28f * Ridge);
	}

	// The red canyon: a plateau cut by wide channels.
	const float CanyonD = FVector2D::Distance(P, Canyon.At);
	const float Plateau = BRStep(Canyon.Radius + 4200.0f, Canyon.Radius + 300.0f, CanyonD);
	if (Plateau > 0.0f)
	{
		H = FMath::Lerp(H, CanyonTop + BRFbm(X / 2600.0f, Y / 2600.0f, 2) * 130.0f, Plateau);
		float Cut = 0.0f;
		for (const TArray<FVector2D>& Line : BRCanyonLines())
		{
			Cut = FMath::Max(Cut, BRStep(CanyonHalfWidth + 950.0f, CanyonHalfWidth, BRLineDistance(P, Line)));
		}
		H = FMath::Lerp(H, CanyonFloor, Cut * Plateau);
	}

	// The crater left by the president's fumbled giant ball.
	const float CraterD = FVector2D::Distance(P, Crater.At);
	if (CraterD < CraterRadius * 1.9f)
	{
		const float R = CraterD / CraterRadius;
		H += 420.0f * FMath::Exp(-FMath::Square((R - 1.0f) / 0.2f));
		if (R < 1.0f) { H = FMath::Max(H - 620.0f * (1.0f - R * R), 90.0f); }
	}

	// The built-up parts are level.
	for (const FBRDistrict* D : Flat())
	{
		H = FMath::Lerp(H, D->Height, BRStep(D->Radius + D->Blend, D->Radius, FVector2D::Distance(P, D->At)));
	}

	// The river: shallow enough to wade anywhere along it.
	const float RiverD = BRLineDistance(P, BRRiverLine());
	H = FMath::Lerp(H, RiverBed, BRStep(RiverHalfWidth + 2600.0f, RiverHalfWidth, RiverD));
	return H;
}

BRMap::FPaint BRMap::PaintAt(const float X, const float Y, const float Height)
{
	using namespace BRLayout;
	const FVector2D P(X, Y);
	FPaint Out;
	const float Shore = BRMap::ShoreDistance(X, Y);
	const float RiverD = BRLineDistance(P, BRRiverLine());
	const float RoadD = BRRoadDistance(P);
	Out.Dirt = BRStep(RoadWidth + 90.0f, RoadWidth - 110.0f, RoadD + BRNoise(X / 420.0f, Y / 420.0f) * 90.0f);
	Out.Sand = FMath::Max(BRStep(3200.0f, 1400.0f, Shore) * BRStep(520.0f, 190.0f, Height), BRStep(RiverHalfWidth + 1200.0f, RiverHalfWidth + 300.0f, RiverD) * 0.9f);
	Out.Marsh = BRStep(RiverHalfWidth + 6200.0f, RiverHalfWidth + 2000.0f, RiverD) * 0.55f;
	Out.Moss = BRStep(Forest.Radius + 1800.0f, Forest.Radius - 500.0f, FVector2D::Distance(P, Forest.At) + BRFbm(X / 2400.0f, Y / 2400.0f, 2) * 2200.0f);
	Out.Snow = BRStep(Mountain.Radius * 0.98f, Mountain.Radius * 0.72f, FVector2D::Distance(P, Mountain.At) + BRFbm(X / 2400.0f, Y / 2400.0f, 2) * 2200.0f);
	Out.Ash = BRStep(CraterRadius * 1.55f, CraterRadius * 0.85f, FVector2D::Distance(P, Crater.At) + BRFbm(X / 500.0f, Y / 500.0f, 2) * 500.0f);
	Out.Dry = FMath::Max(BRStep(Canyon.Radius + 9000.0f, Canyon.Radius + 2200.0f, FVector2D::Distance(P, Canyon.At) + BRFbm(X / 3200.0f, Y / 3200.0f, 2) * 2600.0f),
		BRStep(Farm.Radius + 5000.0f, Farm.Radius - 300.0f, FVector2D::Distance(P, Farm.At)) * 0.55f);
	for (const FBRDistrict* D : Flat())
	{
		const float K = BRStep(D->Radius + 60.0f, D->Radius - 240.0f, FVector2D::Distance(P, D->At) + BRNoise(X / 300.0f, Y / 300.0f) * 120.0f);
		if (D->Ground == EBRDistrictGround::Paved) { Out.Paved = FMath::Max(Out.Paved, K); }
		else if (D->Ground == EBRDistrictGround::Concrete) { Out.Concrete = FMath::Max(Out.Concrete, K); }
		else if (D->Ground == EBRDistrictGround::Dirt) { Out.Dirt = FMath::Max(Out.Dirt, K * 0.85f); }
	}
	return Out;
}

bool BRMap::IsOpenGround(const float X, const float Y)
{
	using namespace BRLayout;
	const FVector2D P(X, Y);
	if (BRMap::ShoreDistance(X, Y) < 3000.0f || BRLineDistance(P, BRRiverLine()) < RiverHalfWidth + 500.0f || BRRoadDistance(P) < RoadWidth + 260.0f)
	{
		return false;
	}
	for (const FBRDistrict* D : Flat())
	{
		if (FVector2D::Distance(P, D->At) < D->Radius + 200.0f) { return false; }
	}
	if (FVector2D::Distance(P, Crater.At) < CraterRadius * 1.25f) { return false; }
	for (const TArray<FVector2D>& Line : BRCanyonLines())
	{
		if (BRLineDistance(P, Line) < CanyonHalfWidth + 800.0f) { return false; }
	}
	return true;
}
