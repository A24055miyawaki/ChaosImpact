#include "BRIsland.h"
#include "BRIslandLayout.h"
#include "ChaosImpact.h"

// The small things: every patch of open ground gets something of its own, every square of paved yard its furniture.
// Nothing is put where something solid already stands (see ABRIsland::IsClear).

namespace
{
	const FLinearColor BRDStone(0.5f, 0.48f, 0.44f);
	const FLinearColor BRDWood(0.36f, 0.2f, 0.09f);
	const FLinearColor BRDDark(0.08f, 0.08f, 0.1f);
	const FLinearColor BRDRed(0.72f, 0.07f, 0.09f);
	const FLinearColor BRDBlue(0.06f, 0.22f, 0.62f);
	const FLinearColor BRDGold(1.0f, 0.66f, 0.12f);
	const FLinearColor BRDSteel(0.46f, 0.48f, 0.52f);
	constexpr float BRDCell = 1400.0f;
}

uint64 ABRIsland::OccupiedKey(const int32 X, const int32 Y)
{
	return (static_cast<uint64>(static_cast<uint32>(X)) << 32) | static_cast<uint32>(Y);
}

bool ABRIsland::IsClear(const FVector2D& At, const float Radius) const
{
	const int32 MinX = FMath::FloorToInt((At.X - Radius) / OccupiedCell), MaxX = FMath::FloorToInt((At.X + Radius) / OccupiedCell);
	const int32 MinY = FMath::FloorToInt((At.Y - Radius) / OccupiedCell), MaxY = FMath::FloorToInt((At.Y + Radius) / OccupiedCell);
	for (int32 X = MinX; X <= MaxX; ++X)
	{
		for (int32 Y = MinY; Y <= MaxY; ++Y)
		{
			if (Occupied.Contains(OccupiedKey(X, Y))) { return false; }
		}
	}
	return true;
}

void ABRIsland::Claim(const FVector2D& At, const float HalfX, const float HalfY)
{
	const int32 MinX = FMath::FloorToInt((At.X - HalfX) / OccupiedCell), MaxX = FMath::FloorToInt((At.X + HalfX) / OccupiedCell);
	const int32 MinY = FMath::FloorToInt((At.Y - HalfY) / OccupiedCell), MaxY = FMath::FloorToInt((At.Y + HalfY) / OccupiedCell);
	if ((MaxX - MinX + 1) * (MaxY - MinY + 1) > 40000) { return; }
	for (int32 X = MinX; X <= MaxX; ++X)
	{
		for (int32 Y = MinY; Y <= MaxY; ++Y) { Occupied.Add(OccupiedKey(X, Y)); }
	}
}

void ABRIsland::Bushes(const FVector2D& Centre, const int32 Count, const float Spread, const FLinearColor& Colour, FRandomStream& Random)
{
	for (int32 I = 0; I < Count; ++I)
	{
		const FVector2D At = Centre + FVector2D(Random.FRandRange(-Spread, Spread), Random.FRandRange(-Spread, Spread));
		const float Size = Random.FRandRange(130.0f, 290.0f);
		if (!IsClear(At, 60.0f)) { continue; }
		Piece(EBRShape::Sphere, EBRMat::Leaves, OnGround(At.X, At.Y, Size * 0.26f), FVector(Size, Size * Random.FRandRange(0.8f, 1.2f), Size * 0.66f), FRotator(0.0f, Random.FRandRange(0.0f, 180.0f), 0.0f), Colour * Random.FRandRange(0.8f, 1.2f), 0.0f, false);
	}
}

void ABRIsland::GroundCover(const FVector2D& Centre, const float Half, const BRMap::FPaint& Paint, FRandomStream& Random)
{
	// Tufts, flowers and pebbles: never in the way, never casting a shadow, there so no ground reads as bare.
	const int32 Count = Random.RandRange(9, 15);
	for (int32 I = 0; I < Count; ++I)
	{
		const FVector2D At = Centre + FVector2D(Random.FRandRange(-Half, Half), Random.FRandRange(-Half, Half));
		const FVector Foot = OnGround(At.X, At.Y);
		if (Foot.Z < 8.0f) { continue; }
		const float Roll = Random.FRand();
		if (Paint.Snow > 0.5f)
		{
			const float Size = Random.FRandRange(60.0f, 200.0f);
			Piece(EBRShape::Sphere, EBRMat::Surface, Foot + FVector(0.0f, 0.0f, Size * 0.08f), FVector(Size, Size, Size * 0.34f), FRotator::ZeroRotator, FLinearColor(0.86f, 0.9f, 0.95f), 0.0f, false, false);
		}
		else if (Paint.Dry > 0.55f || Paint.Sand > 0.6f || Paint.Ash > 0.5f)
		{
			const float Size = Random.FRandRange(14.0f, 60.0f);
			const FLinearColor Stone = Paint.Ash > 0.5f ? FLinearColor(0.12f, 0.11f, 0.11f) : Paint.Dry > 0.55f ? FLinearColor(0.42f, 0.25f, 0.15f) : FLinearColor(0.56f, 0.5f, 0.38f);
			Piece(EBRShape::Sphere, EBRMat::Surface, Foot + FVector(0.0f, 0.0f, Size * 0.2f), FVector(Size, Size * 0.8f, Size * 0.55f), FRotator(0.0f, Random.FRandRange(0.0f, 180.0f), 0.0f), Stone * Random.FRandRange(0.8f, 1.2f), 3.0f, false, false);
		}
		else if (Roll < 0.74f)
		{
			const float Size = Random.FRandRange(50.0f, 110.0f);
			const FLinearColor Blade = FMath::Lerp(FLinearColor(0.12f, 0.3f, 0.06f), FLinearColor(0.34f, 0.44f, 0.1f), Random.FRand());
			Piece(EBRShape::Cone, EBRMat::Leaves, Foot + FVector(0.0f, 0.0f, Size * 0.42f), FVector(Size * 0.6f, Size * 0.6f, Size), FRotator(Random.FRandRange(-8.0f, 8.0f), 0.0f, Random.FRandRange(-8.0f, 8.0f)), Blade, 0.0f, false, false);
		}
		else if (Roll < 0.92f)
		{
			const FLinearColor Petal = (Random.FRand() < 0.5f) ? ((Random.FRand() < 0.5f) ? FLinearColor(1.0f, 0.75f, 0.12f) : FLinearColor(0.95f, 0.92f, 0.85f)) : ((Random.FRand() < 0.5f) ? FLinearColor(1.0f, 0.3f, 0.4f) : FLinearColor(0.6f, 0.34f, 0.95f));
			Piece(EBRShape::Sphere, EBRMat::Leaves, Foot + FVector(0.0f, 0.0f, 18.0f), FVector(22.0f, 22.0f, 14.0f), FRotator::ZeroRotator, Petal, 0.0f, false, false);
		}
		else
		{
			const float Size = Random.FRandRange(20.0f, 54.0f);
			Piece(EBRShape::Sphere, EBRMat::Surface, Foot + FVector(0.0f, 0.0f, Size * 0.2f), FVector(Size, Size * 0.8f, Size * 0.55f), FRotator(0.0f, Random.FRandRange(0.0f, 180.0f), 0.0f), BRDStone * Random.FRandRange(0.8f, 1.2f), 3.0f, false, false);
		}
	}
}

bool ABRIsland::PlaceVignette(const FVector2D& P, const BRMap::FPaint& Paint, FRandomStream& Random)
{
	const auto G = [&](const float X, const float Y) { return OnGround(P.X + X, P.Y + Y); };
	const auto Free = [&](const float X, const float Y, const float R) { return IsClear(P + FVector2D(X, Y), R); };
	const FLinearColor Leaf(0.14f, 0.33f, 0.08f);

	if (Paint.Snow > 0.45f)
	{
		switch (Random.RandHelper(5))
		{
		case 0:
			for (int32 I = 0; I < 5; ++I)
			{
				const float X = Random.FRandRange(-650.0f, 650.0f), Y = Random.FRandRange(-650.0f, 650.0f);
				if (Free(X, Y, 90.0f)) { Tree(G(X, Y), Random.FRandRange(0.8f, 1.5f), 1, Random); }
			}
			return true;
		case 1:
			if (!Free(0.0f, 0.0f, 300.0f)) { return false; }
			Rock(G(0.0f, 0.0f), Random.FRandRange(1.4f, 2.6f), FLinearColor(0.56f, 0.6f, 0.66f), Random);
			Rock(G(280.0f, 120.0f), Random.FRandRange(0.7f, 1.3f), FLinearColor(0.56f, 0.6f, 0.66f), Random);
			return true;
		case 2:
			// Ice spikes.
			for (int32 I = 0; I < 6; ++I)
			{
				const float X = Random.FRandRange(-420.0f, 420.0f), Y = Random.FRandRange(-420.0f, 420.0f), H = Random.FRandRange(220.0f, 620.0f);
				if (!Free(X, Y, 80.0f)) { continue; }
				Piece(EBRShape::Cone, EBRMat::Metal, G(X, Y) + FVector(0.0f, 0.0f, H * 0.46f), FVector(H * 0.4f, H * 0.4f, H), FRotator(Random.FRandRange(-10.0f, 10.0f), 0.0f, Random.FRandRange(-10.0f, 10.0f)), FLinearColor(0.5f, 0.78f, 0.95f), 0.0f, true);
			}
			return true;
		case 3:
		{
			if (!Free(0.0f, 0.0f, 260.0f)) { return false; }
			const FVector Foot = G(0.0f, 0.0f);
			const FLinearColor Snow(0.92f, 0.95f, 1.0f);
			Dome(Foot + FVector(0.0f, 0.0f, 90.0f), 110.0f, Snow, EBRMat::Surface, true);
			Dome(Foot + FVector(0.0f, 0.0f, 235.0f), 74.0f, Snow, EBRMat::Surface, false);
			Dome(Foot + FVector(0.0f, 0.0f, 335.0f), 52.0f, Snow, EBRMat::Surface, false);
			Piece(EBRShape::Cone, EBRMat::Surface, Foot + FVector(46.0f, 0.0f, 335.0f), FVector(24.0f, 24.0f, 72.0f), FRotator(-90.0f, 0.0f, 0.0f), FLinearColor(0.95f, 0.4f, 0.05f), 0.0f, false);
			return true;
		}
		default:
			if (!Free(0.0f, 0.0f, 360.0f)) { return false; }
			Crate(G(0.0f, 0.0f), 150.0f, Random.FRandRange(0.0f, 90.0f), BRDWood);
			Beam(G(180.0f, -120.0f) + FVector(0.0f, 0.0f, 40.0f), G(420.0f, 40.0f) + FVector(0.0f, 0.0f, 40.0f), 40.0f, 40.0f, BRDWood, EBRMat::Surface, true);
			return true;
		}
	}
	if (Paint.Dry > 0.5f)
	{
		switch (Random.RandHelper(5))
		{
		case 0:
			for (int32 I = 0; I < 4; ++I)
			{
				const float X = Random.FRandRange(-600.0f, 600.0f), Y = Random.FRandRange(-600.0f, 600.0f);
				if (Free(X, Y, 200.0f)) { Rock(G(X, Y), Random.FRandRange(0.7f, 2.0f), FLinearColor(0.46f, 0.24f, 0.13f), Random); }
			}
			return true;
		case 1:
			for (int32 I = 0; I < 4; ++I)
			{
				const float X = Random.FRandRange(-650.0f, 650.0f), Y = Random.FRandRange(-650.0f, 650.0f), H = Random.FRandRange(200.0f, 430.0f);
				if (!Free(X, Y, 80.0f)) { continue; }
				const FVector Foot = G(X, Y);
				const FLinearColor Green(0.16f, 0.34f, 0.12f);
				Pole(Foot, Foot + FVector(0.0f, 0.0f, H), 32.0f, Green, EBRMat::Surface, true);
				Beam(Foot + FVector(0.0f, 0.0f, H * 0.5f), Foot + FVector(0.0f, 70.0f, H * 0.5f), 30.0f, 30.0f, Green, EBRMat::Surface, false);
				Pole(Foot + FVector(0.0f, 70.0f, H * 0.5f), Foot + FVector(0.0f, 70.0f, H * 0.78f), 16.0f, Green, EBRMat::Surface, false);
			}
			return true;
		case 2:
		{
			// The ribs of something enormous.
			if (!Free(0.0f, 0.0f, 500.0f)) { return false; }
			const FLinearColor Bone(0.82f, 0.76f, 0.62f);
			const float Yaw = Random.FRandRange(0.0f, 180.0f);
			for (int32 Rib = -3; Rib <= 3; ++Rib)
			{
				const FVector Along = FRotator(0.0f, Yaw, 0.0f).Vector() * (Rib * 120.0f);
				const FVector Foot = G(static_cast<float>(Along.X), static_cast<float>(Along.Y));
				Piece(EBRShape::Cylinder, EBRMat::Surface, Foot + FVector(0.0f, 0.0f, 110.0f), FVector(26.0f, 26.0f, 300.0f), FRotator(0.0f, Yaw, 16.0f), Bone, 0.0f, true);
				Piece(EBRShape::Cylinder, EBRMat::Surface, Foot + FVector(0.0f, 0.0f, 110.0f), FVector(26.0f, 26.0f, 300.0f), FRotator(0.0f, Yaw, -16.0f), Bone, 0.0f, false);
			}
			return true;
		}
		case 3:
		{
			if (!Free(0.0f, 0.0f, 380.0f)) { return false; }
			// An abandoned wagon.
			const FVector Foot = G(0.0f, 0.0f);
			const float Yaw = Random.FRandRange(0.0f, 360.0f);
			Box(Foot + FVector(0.0f, 0.0f, 110.0f), FVector(380.0f, 180.0f, 90.0f), BRDWood, Yaw, 4.0f);
			for (const FVector2D& W : {FVector2D(-140, -110), FVector2D(140, -110), FVector2D(-140, 110), FVector2D(140, 110)})
			{
				Piece(EBRShape::Cylinder, EBRMat::Surface, Foot + FRotator(0.0f, Yaw, 0.0f).RotateVector(FVector(W.X, W.Y, 62.0f)), FVector(120.0f, 120.0f, 16.0f), FRotator(0.0f, Yaw, 90.0f), BRDWood * 0.7f, 0.0f, false);
			}
			return true;
		}
		default:
			Rock(G(0.0f, 0.0f), Random.FRandRange(2.0f, 3.4f), FLinearColor(0.42f, 0.22f, 0.12f), Random);
			return true;
		}
	}
	if (Paint.Sand > 0.55f)
	{
		switch (Random.RandHelper(4))
		{
		case 0:
			for (int32 I = 0; I < 3; ++I)
			{
				const float X = Random.FRandRange(-600.0f, 600.0f), Y = Random.FRandRange(-600.0f, 600.0f);
				if (Free(X, Y, 90.0f)) { Tree(G(X, Y), Random.FRandRange(0.9f, 1.4f), 2, Random); }
			}
			return true;
		case 1:
		{
			if (!Free(0.0f, 0.0f, 260.0f)) { return false; }
			// A beach umbrella and two loungers.
			const FVector Foot = G(0.0f, 0.0f);
			Pole(Foot, Foot + FVector(0.0f, 0.0f, 300.0f), 8.0f, FLinearColor(0.9f, 0.9f, 0.88f), EBRMat::Metal, false);
			Piece(EBRShape::Cone, EBRMat::Fade, Foot + FVector(0.0f, 0.0f, 320.0f), FVector(460.0f, 460.0f, 100.0f), FRotator::ZeroRotator, (Random.FRand() < 0.5f) ? BRDRed : BRDBlue, 4.0f, false);
			for (const float Y : {-130.0f, 130.0f}) { Box(Foot + FVector(0.0f, Y, 26.0f), FVector(190.0f, 70.0f, 12.0f), FLinearColor(0.9f, 0.86f, 0.74f), 0.0f, 4.0f, false); }
			return true;
		}
		case 2:
		{
			if (!Free(0.0f, 0.0f, 340.0f)) { return false; }
			// A boat pulled up on the sand.
			const FVector Foot = G(0.0f, 0.0f);
			const float Yaw = Random.FRandRange(0.0f, 360.0f);
			const FLinearColor Hull = (Random.FRand() < 0.5f) ? FLinearColor(0.82f, 0.82f, 0.8f) : FLinearColor(0.1f, 0.34f, 0.5f);
			Piece(EBRShape::Sphere, EBRMat::Surface, Foot + FVector(0.0f, 0.0f, 34.0f), FVector(560.0f, 190.0f, 110.0f), FRotator(0.0f, Yaw, 6.0f), Hull, 4.0f, true);
			return true;
		}
		default:
			for (int32 I = 0; I < 3; ++I)
			{
				const float X = Random.FRandRange(-500.0f, 500.0f), Y = Random.FRandRange(-500.0f, 500.0f);
				if (Free(X, Y, 110.0f)) { Beam(G(X, Y) + FVector(0.0f, 0.0f, 16.0f), G(X + Random.FRandRange(-260.0f, 260.0f), Y + Random.FRandRange(-260.0f, 260.0f)) + FVector(0.0f, 0.0f, 16.0f), 36.0f, 36.0f, FLinearColor(0.42f, 0.34f, 0.24f), EBRMat::Surface, true); }
			}
			return true;
		}
	}
	if (Paint.Marsh > 0.25f)
	{
		// Reeds and pools by the river.
		if (Free(0.0f, 0.0f, 320.0f) && (Random.FRand() < 0.5f))
		{
			const FVector Foot = G(0.0f, 0.0f);
			Disc(Foot + FVector(0.0f, 0.0f, 5.0f), Random.FRandRange(240.0f, 420.0f), 6.0f, FLinearColor(0.1f, 0.34f, 0.42f), EBRMat::Metal, false);
			for (int32 Pad = 0; Pad < 5; ++Pad)
			{
				Disc(Foot + FVector(Random.FRandRange(-220.0f, 220.0f), Random.FRandRange(-220.0f, 220.0f), 10.0f), Random.FRandRange(24.0f, 46.0f), 2.0f, FLinearColor(0.16f, 0.42f, 0.14f), EBRMat::Surface, false);
			}
		}
		for (int32 I = 0; I < 14; ++I)
		{
			const FVector Foot = G(Random.FRandRange(-800.0f, 800.0f), Random.FRandRange(-800.0f, 800.0f));
			Piece(EBRShape::Cone, EBRMat::Leaves, Foot + FVector(0.0f, 0.0f, 90.0f), FVector(70.0f, 70.0f, 230.0f), FRotator(Random.FRandRange(-8.0f, 8.0f), 0.0f, Random.FRandRange(-8.0f, 8.0f)), FLinearColor(0.2f, 0.4f, 0.12f) * Random.FRandRange(0.85f, 1.2f), 0.0f, false, false);
		}
		return true;
	}

	// Ordinary country.
	switch (Random.RandHelper(Paint.Moss > 0.4f ? 4 : 12))
	{
	case 0:
		for (int32 I = 0; I < 4; ++I)
		{
			const float X = Random.FRandRange(-700.0f, 700.0f), Y = Random.FRandRange(-700.0f, 700.0f);
			if (Free(X, Y, 100.0f)) { Tree(G(X, Y), Random.FRandRange(0.8f, 1.6f), Random.FRand() < 0.2f ? 1 : 0, Random); }
		}
		Bushes(P, 4, 800.0f, Leaf, Random);
		return true;
	case 1:
	{
		// A fallen trunk with moss and mushrooms.
		if (!Free(0.0f, 0.0f, 320.0f)) { return false; }
		const float Yaw = Random.FRandRange(0.0f, 180.0f);
		const FVector Along = FRotator(0.0f, Yaw, 0.0f).Vector() * 300.0f;
		Pole(G(static_cast<float>(-Along.X), static_cast<float>(-Along.Y)) + FVector(0.0f, 0.0f, 44.0f), G(static_cast<float>(Along.X), static_cast<float>(Along.Y)) + FVector(0.0f, 0.0f, 44.0f), 48.0f, FLinearColor(0.2f, 0.12f, 0.07f), EBRMat::Surface, true);
		Bushes(P, 3, 320.0f, Leaf * 1.2f, Random);
		return true;
	}
	case 2:
		if (!Free(0.0f, 0.0f, 300.0f)) { return false; }
		Rock(G(0.0f, 0.0f), Random.FRandRange(1.2f, 2.8f), FLinearColor(0.42f, 0.41f, 0.39f), Random);
		Bushes(P, 5, 480.0f, Leaf, Random);
		return true;
	case 3:
	{
		// A stump with its axe, and a stack of logs.
		if (!Free(0.0f, 0.0f, 300.0f)) { return false; }
		const FVector Foot = G(0.0f, 0.0f);
		Disc(Foot + FVector(0.0f, 0.0f, 45.0f), 80.0f, 90.0f, FLinearColor(0.3f, 0.18f, 0.1f));
		for (int32 Log = 0; Log < 6; ++Log)
		{
			const float Row = Log < 3 ? 0.0f : 1.0f;
			Piece(EBRShape::Cylinder, EBRMat::Surface, Foot + FVector(220.0f, -60.0f + (Log % 3) * 58.0f + Row * 28.0f, 30.0f + Row * 52.0f), FVector(58.0f, 58.0f, 220.0f), FRotator(90.0f, 0.0f, 0.0f), BRDWood * 0.8f, 0.0f, Row < 0.5f);
		}
		return true;
	}
	case 4:
	{
		// A paddock: fence on three sides.
		if (!Free(0.0f, 0.0f, 600.0f)) { return false; }
		const float H = 520.0f;
		const FLinearColor Rail(0.55f, 0.4f, 0.24f);
		Fence(G(-H, -H), G(H, -H), Rail);
		Fence(G(H, -H), G(H, H), Rail);
		Fence(G(H, H), G(-H, H), Rail);
		Bushes(P, 2, 300.0f, Leaf, Random);
		return true;
	}
	case 5:
	{
		// A low dry-stone wall to hide behind.
		const float Yaw = Random.FRandRange(0.0f, 180.0f);
		const FVector Way = FRotator(0.0f, Yaw, 0.0f).Vector();
		for (int32 Stone = -3; Stone <= 3; ++Stone)
		{
			if (Stone == 1) { continue; }
			const float X = static_cast<float>(Way.X) * Stone * 170.0f, Y = static_cast<float>(Way.Y) * Stone * 170.0f;
			if (Free(X, Y, 90.0f)) { Box(G(X, Y) + FVector(0.0f, 0.0f, 55.0f), FVector(180.0f, 90.0f, 130.0f), BRDStone * Random.FRandRange(0.8f, 1.15f), Yaw + Random.FRandRange(-4.0f, 4.0f), 3.0f); }
		}
		return true;
	}
	case 6:
	{
		// A pond with a jetty.
		if (!Free(0.0f, 0.0f, 620.0f)) { return false; }
		const FVector Foot = G(0.0f, 0.0f);
		Disc(Foot + FVector(0.0f, 0.0f, 6.0f), 560.0f, 8.0f, FLinearColor(0.1f, 0.36f, 0.5f), EBRMat::Metal, false);
		Disc(Foot + FVector(0.0f, 0.0f, 4.0f), 620.0f, 6.0f, FLinearColor(0.5f, 0.44f, 0.3f), EBRMat::Surface, false);
		Box(Foot + FVector(380.0f, 0.0f, 30.0f), FVector(460.0f, 130.0f, 10.0f), BRDWood, 0.0f, 4.0f, true);
		for (int32 R = 0; R < 12; ++R)
		{
			const float A = FMath::DegreesToRadians(R * 30.0f + Random.FRandRange(-10.0f, 10.0f));
			Piece(EBRShape::Cone, EBRMat::Leaves, Foot + FVector(FMath::Cos(A) * 610.0f, FMath::Sin(A) * 610.0f, 70.0f), FVector(70.0f, 70.0f, 190.0f), FRotator::ZeroRotator, FLinearColor(0.2f, 0.42f, 0.12f), 0.0f, false, false);
		}
		return true;
	}
	case 7:
	{
		// A shed.
		if (!Free(0.0f, 0.0f, 480.0f)) { return false; }
		House(G(0.0f, 0.0f) - FVector(0.0f, 0.0f, 10.0f), FVector2D(560.0, 520.0), 260.0f, Random.RandHelper(4) * 90.0f, FLinearColor(0.5f, 0.34f, 0.2f), FLinearColor(0.3f, 0.3f, 0.32f), 1, true);
		Crate(G(420.0f, 80.0f), 130.0f, 12.0f, BRDWood);
		return true;
	}
	case 8:
	{
		// A half-buried stray ball of the president's.
		if (!Free(0.0f, 0.0f, 360.0f)) { return false; }
		const float R = Random.FRandRange(180.0f, 320.0f);
		Dodgeball(G(0.0f, 0.0f) + FVector(0.0f, 0.0f, R * 0.45f), R, (Random.FRand() < 0.5f) ? BRDRed : BRDBlue, FLinearColor(0.92f, 0.9f, 0.82f), Random.FRandRange(0.0f, 90.0f), true);
		Bushes(P, 3, 520.0f, Leaf, Random);
		return true;
	}
	case 9:
	{
		// A picnic: table, benches, a basket.
		if (!Free(0.0f, 0.0f, 320.0f)) { return false; }
		const FVector Foot = G(0.0f, 0.0f);
		Box(Foot + FVector(0.0f, 0.0f, 86.0f), FVector(260.0f, 110.0f, 12.0f), BRDWood, 0.0f, 4.0f, true);
		Box(Foot + FVector(0.0f, 0.0f, 40.0f), FVector(30.0f, 90.0f, 80.0f), BRDWood * 0.7f, 0.0f, 0.0f, false);
		Bench(Foot + FVector(0.0f, 130.0f, 0.0f), 180.0f);
		Bench(Foot + FVector(0.0f, -130.0f, 0.0f), 0.0f);
		Disc(Foot + FVector(-60.0f, 0.0f, 96.0f), 30.0f, 22.0f, BRDGold, EBRMat::Surface, false);
		return true;
	}
	case 10:
	{
		// A signpost at a field corner.
		if (!Free(0.0f, 0.0f, 200.0f)) { return false; }
		const FVector Foot = G(0.0f, 0.0f);
		Pole(Foot, Foot + FVector(0.0f, 0.0f, 300.0f), 11.0f, BRDWood, EBRMat::Surface, true);
		for (int32 Arm = 0; Arm < 3; ++Arm)
		{
			Box(Foot + FVector(0.0f, 0.0f, 250.0f - Arm * 42.0f) + FRotator(0.0f, Arm * 120.0f, 0.0f).Vector() * 64.0f, FVector(150.0f, 12.0f, 32.0f), BRDWood * 1.3f, Arm * 120.0f, 4.0f, false);
		}
		Bushes(P, 2, 300.0f, Leaf, Random);
		return true;
	}
	default:
		for (int32 I = 0; I < 3; ++I)
		{
			const float X = Random.FRandRange(-650.0f, 650.0f), Y = Random.FRandRange(-650.0f, 650.0f);
			if (!Free(X, Y, 140.0f)) { continue; }
			Piece(EBRShape::Cylinder, EBRMat::Surface, G(X, Y) + FVector(0.0f, 0.0f, 92.0f), FVector(190.0f, 190.0f, 200.0f), FRotator(90.0f, Random.FRandRange(0.0f, 180.0f), 0.0f), FLinearColor(0.72f, 0.56f, 0.2f), 3.0f, true);
		}
		Bushes(P, 3, 700.0f, Leaf, Random);
		return true;
	}
}

void ABRIsland::DressDistrict(const FBRDistrict& District, const int32 Seed)
{
	FRandomStream Random(Seed);
	constexpr float Step = 950.0f;
	const int32 Span = FMath::CeilToInt(District.Radius / Step);
	const float Z = District.Height;
	const FLinearColor Pallet(0.5f, 0.36f, 0.2f), Cone(0.95f, 0.42f, 0.05f);
	for (int32 GX = -Span; GX <= Span; ++GX)
	{
		for (int32 GY = -Span; GY <= Span; ++GY)
		{
			const FVector2D At = District.At + FVector2D(GX * Step + Random.FRandRange(-260.0f, 260.0f), GY * Step + Random.FRandRange(-260.0f, 260.0f));
			if (FVector2D::Distance(At, District.At) > District.Radius - 150.0f || !IsClear(At, 230.0f)) { continue; }
			const FVector Foot(At.X, At.Y, Z);
			const float Yaw = Random.RandHelper(4) * 90.0f + Random.FRandRange(-8.0f, 8.0f);
			Claim(At, 150.0f, 150.0f);
			const int32 Roll = Random.RandHelper(9);
			if (District.Ground == EBRDistrictGround::Paved)
			{
				switch (Roll)
				{
				case 0: Bench(Foot, Yaw); Lamp(Foot + FVector(180.0f, 0.0f, 0.0f), 420.0f); break;
				case 1: FlowerBed(Foot, 170.0f, Random); break;
				case 2:
					// A planter with a clipped tree.
					Disc(Foot + FVector(0.0f, 0.0f, 40.0f), 110.0f, 80.0f, BRDStone * 1.1f);
					Piece(EBRShape::Sphere, EBRMat::Leaves, Foot + FVector(0.0f, 0.0f, 190.0f), FVector(240.0f), FRotator::ZeroRotator, FLinearColor(0.12f, 0.36f, 0.1f) * Random.FRandRange(0.9f, 1.2f), 0.0f, false);
					break;
				case 3:
					// A ball rack.
					Box(Foot + FVector(0.0f, 0.0f, 30.0f), FVector(320.0f, 120.0f, 12.0f), BRDSteel, Yaw, 5.0f);
					for (int32 B = -1; B <= 1; ++B) { Dodgeball(Foot + FRotator(0.0f, Yaw, 0.0f).Vector() * (B * 100.0f) + FVector(0.0f, 0.0f, 86.0f), 46.0f, B == 0 ? BRDBlue : BRDRed, FLinearColor::White, B * 30.0f, false); }
					break;
				case 4:
					// A kiosk with an awning.
					Box(Foot + FVector(0.0f, 0.0f, 100.0f), FVector(220.0f, 180.0f, 200.0f), FLinearColor(0.82f, 0.78f, 0.66f), Yaw, 4.0f);
					Piece(EBRShape::Cube, EBRMat::Fade, Foot + FVector(0.0f, 0.0f, 226.0f), FVector(320.0f, 280.0f, 14.0f), FRotator(0.0f, Yaw, 0.0f), (Random.FRand() < 0.5f) ? BRDRed : BRDBlue, 4.0f, false);
					break;
				case 5:
					for (int32 B = 0; B < 4; ++B) { Disc(Foot + FRotator(0.0f, Yaw + B * 90.0f, 0.0f).Vector() * 160.0f + FVector(0.0f, 0.0f, 45.0f), 16.0f, 90.0f, BRDDark, EBRMat::Metal); }
					Glow(Foot + FVector(0.0f, 0.0f, 8.0f), FVector(260.0f, 260.0f, 3.0f), BRDGold * 0.7f, Yaw);
					break;
				case 6: Flag(Foot, 520.0f, (Random.FRand() < 0.5f) ? BRDRed : BRDBlue); break;
				case 7:
					Disc(Foot + FVector(0.0f, 0.0f, 42.0f), 44.0f, 84.0f, FLinearColor(0.12f, 0.3f, 0.16f), EBRMat::Metal);
					Piece(EBRShape::Sphere, EBRMat::Metal, Foot + FVector(0.0f, 0.0f, 92.0f), FVector(88.0f, 88.0f, 50.0f), FRotator::ZeroRotator, FLinearColor(0.12f, 0.3f, 0.16f), 0.0f, false);
					break;
				default:
					Car(Foot, Yaw, (Random.FRand() < 0.5f) ? BRDRed : FLinearColor(0.8f, 0.8f, 0.84f));
					break;
				}
			}
			else if (District.Ground == EBRDistrictGround::Concrete)
			{
				switch (Roll)
				{
				case 0: Crate(Foot, Random.FRandRange(150.0f, 230.0f), Yaw, Pallet); Crate(Foot + FVector(170.0f, 60.0f, 0.0f), 120.0f, Yaw + 20.0f, Pallet * 1.2f); break;
				case 1:
					for (int32 B = 0; B < 5; ++B) { Barrel(Foot + FVector(Random.FRandRange(-130.0f, 130.0f), Random.FRandRange(-130.0f, 130.0f), 0.0f), (Random.FRand() < 0.5f) ? BRDBlue : BRDGold); }
					break;
				case 2:
					// A pallet of sacks.
					Box(Foot + FVector(0.0f, 0.0f, 10.0f), FVector(260.0f, 220.0f, 20.0f), Pallet, Yaw, 4.0f);
					for (int32 B = 0; B < 6; ++B) { Piece(EBRShape::Sphere, EBRMat::Surface, Foot + FVector(-70.0f + (B % 3) * 70.0f, B < 3 ? -46.0f : 46.0f, 52.0f), FVector(90.0f, 76.0f, 56.0f), FRotator(0.0f, Yaw, 0.0f), FLinearColor(0.72f, 0.66f, 0.5f), 3.0f, false); }
					break;
				case 3:
					for (int32 B = 0; B < 5; ++B)
					{
						const FVector On = Foot + FRotator(0.0f, Yaw, 0.0f).Vector() * (B * 140.0f - 280.0f);
						Piece(EBRShape::Cone, EBRMat::Surface, On + FVector(0.0f, 0.0f, 34.0f), FVector(52.0f, 52.0f, 80.0f), FRotator::ZeroRotator, Cone, 0.0f, false);
						Box(On + FVector(0.0f, 0.0f, 3.0f), FVector(70.0f, 70.0f, 6.0f), Cone * 0.5f, 0.0f, 0.0f, false);
					}
					break;
				case 4:
					// A forklift.
					Box(Foot + FVector(0.0f, 0.0f, 74.0f), FVector(220.0f, 120.0f, 90.0f), BRDGold, Yaw, 0.0f);
					Box(Foot + FVector(0.0f, 0.0f, 170.0f), FVector(100.0f, 110.0f, 90.0f), BRDDark, Yaw, 0.0f, false);
					Pole(Foot + FRotator(0.0f, Yaw, 0.0f).Vector() * 130.0f, Foot + FRotator(0.0f, Yaw, 0.0f).Vector() * 130.0f + FVector(0.0f, 0.0f, 260.0f), 12.0f, BRDSteel, EBRMat::Metal, false);
					break;
				case 5: Box(Foot + FVector(0.0f, 0.0f, 55.0f), FVector(520.0f, 110.0f, 110.0f), FLinearColor(0.5f, 0.42f, 0.26f), Yaw, 3.0f); break;
				case 6:
					// An oil stain and a tool chest.
					Disc(Foot + FVector(0.0f, 0.0f, 2.0f), 170.0f, 2.0f, FLinearColor(0.05f, 0.05f, 0.06f), EBRMat::Surface, false);
					Box(Foot + FVector(120.0f, 0.0f, 62.0f), FVector(110.0f, 70.0f, 124.0f), BRDRed, Yaw, 5.0f);
					break;
				case 7: Truck(Foot, Yaw, (Random.FRand() < 0.5f) ? BRDBlue : FLinearColor(0.88f, 0.88f, 0.86f)); break;
				default: Container(Foot, Yaw, (Random.FRand() < 0.5f) ? BRDBlue : BRDRed); break;
				}
			}
			else if (District.Ground == EBRDistrictGround::Dirt)
			{
				switch (Roll)
				{
				case 0: case 1:
					Piece(EBRShape::Cylinder, EBRMat::Surface, Foot + FVector(0.0f, 0.0f, 92.0f), FVector(190.0f, 190.0f, 200.0f), FRotator(90.0f, Yaw, 0.0f), FLinearColor(0.72f, 0.56f, 0.2f), 3.0f, true);
					break;
				case 2: Box(Foot + FVector(0.0f, 0.0f, 40.0f), FVector(300.0f, 90.0f, 80.0f), BRDWood, Yaw, 4.0f); break;
				case 3:
					// A wheelbarrow of pumpkins.
					Box(Foot + FVector(0.0f, 0.0f, 62.0f), FVector(180.0f, 100.0f, 50.0f), BRDSteel, Yaw, 0.0f);
					Piece(EBRShape::Sphere, EBRMat::Surface, Foot + FVector(0.0f, 0.0f, 112.0f), FVector(90.0f), FRotator::ZeroRotator, FLinearColor(0.95f, 0.42f, 0.04f), 3.0f, false);
					break;
				case 4: Bushes(At, 4, 260.0f, FLinearColor(0.16f, 0.36f, 0.08f), Random); break;
				default: break;
				}
			}
			else
			{
				switch (Roll)
				{
				case 0: Bushes(At, 3, 220.0f, FLinearColor(0.16f, 0.38f, 0.1f), Random); break;
				case 1: FlowerBed(Foot, 150.0f, Random); break;
				case 2: Tree(Foot, Random.FRandRange(0.7f, 1.0f), 3, Random); break;
				case 3:
					// A letterbox and bins by the kerb.
					Pole(Foot, Foot + FVector(0.0f, 0.0f, 110.0f), 7.0f, BRDWood, EBRMat::Surface, false);
					Box(Foot + FVector(0.0f, 0.0f, 122.0f), FVector(60.0f, 34.0f, 30.0f), BRDRed, Yaw, 0.0f, false);
					Barrel(Foot + FVector(120.0f, 40.0f, 0.0f), FLinearColor(0.1f, 0.3f, 0.16f));
					break;
				default: break;
				}
			}
		}
	}
}

void ABRIsland::BuildDressing()
{
	using namespace BRLayout;
	FRandomStream Random(301);
	// ---- The built-up ground: furniture in every gap between the buildings.
	int32 Seed = 400;
	for (const FBRDistrict* D : Flat()) { DressDistrict(*D, ++Seed); }

	// ---- Open country, a square at a time: every square must end up with something on it.
	int32 Squares = 0, Filled = 0, Bare = 0;
	const int32 Span = FMath::CeilToInt(84000.0f / BRDCell);
	for (int32 GX = -Span; GX <= Span; ++GX)
	{
		for (int32 GY = -Span; GY <= Span; ++GY)
		{
			const FVector2D Cell(GX * BRDCell, GY * BRDCell);
			if (BRMap::ShoreDistance(Cell.X, Cell.Y) < 200.0f) { continue; }
			const float Z = BRMap::HeightAt(Cell.X, Cell.Y);
			if (Z < 4.0f) { continue; }
			bool bInDistrict = false;
			for (const FBRDistrict* D : Flat()) { bInDistrict = bInDistrict || FVector2D::Distance(Cell, D->At) < D->Radius; }
			if (bInDistrict) { continue; }
			const BRMap::FPaint Paint = BRMap::PaintAt(Cell.X, Cell.Y, Z);
			++Squares;
			GroundCover(Cell, BRDCell * 0.5f, Paint, Random);
			if (!BRMap::IsOpenGround(Cell.X, Cell.Y))
			{
				// By a road or the shore: small things on its verges rather than a whole scene.
				const FVector2D Off = Cell + FVector2D(Random.FRandRange(-900.0f, 900.0f), Random.FRandRange(-900.0f, 900.0f));
				if (BRMap::IsOpenGround(Off.X, Off.Y) && IsClear(Off, 120.0f)) { Rock(OnGround(Off.X, Off.Y), Random.FRandRange(0.5f, 1.2f), FLinearColor(0.42f, 0.41f, 0.39f), Random); }
				continue;
			}
			// Try a few spots in the square; if the first choice has no room, another will.
			bool bDone = false;
			for (int32 Try = 0; Try < 6 && !bDone; ++Try)
			{
				const FVector2D P = Cell + FVector2D(Random.FRandRange(-BRDCell * 0.36f, BRDCell * 0.36f), Random.FRandRange(-BRDCell * 0.36f, BRDCell * 0.36f));
				if (BRMap::IsOpenGround(P.X, P.Y)) { bDone = PlaceVignette(P, BRMap::PaintAt(P.X, P.Y, BRMap::HeightAt(P.X, P.Y)), Random); }
			}
			if (bDone) { ++Filled; } else { ++Bare; Bushes(Cell, 6, BRDCell * 0.42f, FLinearColor(0.14f, 0.33f, 0.08f), Random); }
		}
	}
	UE_LOG(LogChaosImpact, Log, TEXT("BRIsland dressing: %d squares, %d with a scene, %d with bushes only"), Squares, Filled, Bare);

	// ---- The canyon floors: rubble and old mining gear all along them.
	for (const TArray<FVector2D>& Line : CanyonChannels())
	{
		for (int32 Index = 0; Index + 1 < Line.Num(); ++Index)
		{
			const int32 Steps = FMath::CeilToInt(FVector2D::Distance(Line[Index], Line[Index + 1]) / 800.0f);
			for (int32 Step = 0; Step < Steps; ++Step)
			{
				const FVector2D On = FMath::Lerp(Line[Index], Line[Index + 1], static_cast<double>(Step) / Steps) + FVector2D(Random.FRandRange(-1700.0f, 1700.0f), Random.FRandRange(-1700.0f, 1700.0f));
				if (!IsClear(On, 240.0f)) { continue; }
				const int32 Roll = Random.RandHelper(5);
				if (Roll <= 2) { Rock(OnGround(On.X, On.Y), Random.FRandRange(0.7f, 2.3f), FLinearColor(0.46f, 0.22f, 0.12f), Random); }
				else if (Roll == 3) { Crate(OnGround(On.X, On.Y), 170.0f, Random.FRandRange(0.0f, 90.0f), BRDWood); }
				else { Barrel(OnGround(On.X, On.Y), FLinearColor(0.42f, 0.2f, 0.1f)); }
			}
		}
	}
	// ---- Round the crater: scorched scrap.
	for (int32 Index = 0; Index < 160; ++Index)
	{
		const float Angle = Random.FRandRange(0.0f, UE_TWO_PI), Rad = Random.FRandRange(1500.0f, CraterRadius * 1.7f);
		const FVector2D On = Crater.At + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Rad;
		if (!IsClear(On, 160.0f)) { continue; }
		Piece(EBRShape::Cube, EBRMat::Surface, OnGround(On.X, On.Y, 30.0f), FVector(Random.FRandRange(80.0f, 300.0f), Random.FRandRange(60.0f, 180.0f), Random.FRandRange(30.0f, 120.0f)), FRotator(Random.FRandRange(-24.0f, 24.0f), Random.FRandRange(0.0f, 360.0f), Random.FRandRange(-24.0f, 24.0f)), FLinearColor(0.22f, 0.2f, 0.2f) * Random.FRandRange(0.8f, 1.4f), 3.0f, true);
	}
	// ---- Along the roads: guard posts on the bends, stones and signs on the verges.
	for (const TArray<FVector2D>& Road : BRMap::Roads())
	{
		for (int32 Index = 0; Index + 1 < Road.Num(); ++Index)
		{
			const FVector2D Way = (Road[Index + 1] - Road[Index]).GetSafeNormal();
			const FVector2D Side(-Way.Y, Way.X);
			for (const float Dir : {-1.0f, 1.0f})
			{
				const FVector2D On = Road[Index] + Side * Dir * (RoadWidth + 120.0f);
				if (BRMap::ShoreDistance(On.X, On.Y) < 1200.0f || !IsClear(On, 70.0f)) { continue; }
				if (Index % 2 == 0)
				{
					const FVector Foot = OnGround(On.X, On.Y);
					Piece(EBRShape::Cube, EBRMat::Surface, Foot + FVector(0.0f, 0.0f, 46.0f), FVector(18.0f, 18.0f, 92.0f), FRotator(0.0f, FMath::RadiansToDegrees(FMath::Atan2(Way.Y, Way.X)), 0.0f), FLinearColor(0.9f, 0.9f, 0.86f), 0.0f, false);
					Piece(EBRShape::Cube, EBRMat::Glow, Foot + FVector(0.0f, 0.0f, 82.0f), FVector(20.0f, 20.0f, 12.0f), FRotator::ZeroRotator, BRDRed * 1.5f, 0.0f, false, false);
				}
			}
		}
	}
}
