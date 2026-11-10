#include "BRIsland.h"
#include "BRIslandLayout.h"

// Each part of the island, built of the pieces in BRIsland.cpp. Everything is placed from fixed seeds, so every
// machine has the same island without sending any of it.

namespace
{
	const FLinearColor BRStone(0.62f, 0.59f, 0.54f);
	const FLinearColor BRDarkStone(0.3f, 0.3f, 0.32f);
	const FLinearColor BRMarble(0.86f, 0.84f, 0.8f);
	const FLinearColor BRGold(1.0f, 0.66f, 0.12f);
	const FLinearColor BRRed(0.72f, 0.07f, 0.09f);
	const FLinearColor BRBlue(0.06f, 0.22f, 0.62f);
	const FLinearColor BRSteel(0.46f, 0.48f, 0.52f);
	const FLinearColor BRWood(0.36f, 0.2f, 0.09f);
	const FLinearColor BRRust(0.42f, 0.2f, 0.1f);

	FVector2D BROnCircle(const FVector2D& Centre, const float Radius, const float Degrees)
	{
		const float A = FMath::DegreesToRadians(Degrees);
		return Centre + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Radius;
	}

	FLinearColor BRPick(FRandomStream& Random, std::initializer_list<FLinearColor> Options)
	{
		return *(Options.begin() + Random.RandHelper(static_cast<int32>(Options.size())));
	}
}

void ABRIsland::BuildCapital()
{
	using namespace BRLayout;
	FRandomStream Random(101);
	const FVector2D C = Capital.At;
	const float Z = Capital.Height;
	const auto G = [&](const float X, const float Y, const float Up = 0.0f) { return FVector(C.X + X, C.Y + Y, Z + Up); };

	// ---- The great round plaza and its paving rings.
	for (int32 Ring = 0; Ring < 4; ++Ring)
	{
		Disc(G(0.0f, 0.0f, 2.0f + Ring * 1.5f), 5200.0f - Ring * 1250.0f, 4.0f, Ring % 2 == 0 ? FLinearColor(0.46f, 0.42f, 0.36f) : FLinearColor(0.6f, 0.53f, 0.42f), EBRMat::Surface, false);
	}
	// A compass of coloured stone across it.
	for (int32 Spoke = 0; Spoke < 16; ++Spoke)
	{
		const FRotator Turn(0.0f, Spoke * 22.5f, 0.0f);
		Box(G(0.0f, 0.0f, 12.0f) + Turn.Vector() * 2900.0f, FVector(4200.0f, Spoke % 2 == 0 ? 110.0f : 50.0f, 3.0f), Spoke % 2 == 0 ? BRRed * 0.8f : BRBlue * 0.85f, Spoke * 22.5f, 0.0f, false);
	}

	// ---- The statue of President Dotch Ball, on a stepped plinth, a golden ball held high.
	for (int32 Step = 0; Step < 4; ++Step)
	{
		Disc(G(0.0f, 0.0f, 40.0f + Step * 80.0f), 1000.0f - Step * 190.0f, 80.0f, Step % 2 == 0 ? BRMarble : BRStone);
	}
	const FVector Foot = G(0.0f, 0.0f, 320.0f);
	const FLinearColor Bronze(0.28f, 0.2f, 0.12f);
	Piece(EBRShape::Cone, EBRMat::Fade, Foot + FVector(0.0f, 0.0f, 300.0f), FVector(520.0f, 520.0f, 620.0f), FRotator::ZeroRotator, Bronze);
	Piece(EBRShape::Cube, EBRMat::Fade, Foot + FVector(0.0f, 0.0f, 610.0f), FVector(260.0f, 460.0f, 210.0f), FRotator::ZeroRotator, Bronze);
	Piece(EBRShape::Sphere, EBRMat::Fade, Foot + FVector(0.0f, 0.0f, 900.0f), FVector(430.0f), FRotator::ZeroRotator, Bronze * 1.4f);
	Pole(Foot + FVector(0.0f, 210.0f, 650.0f), Foot + FVector(40.0f, 380.0f, 1120.0f), 62.0f, Bronze, EBRMat::Fade, false);
	Pole(Foot + FVector(0.0f, -210.0f, 650.0f), Foot + FVector(170.0f, -330.0f, 520.0f), 62.0f, Bronze, EBRMat::Fade, false);
	Piece(EBRShape::Sphere, EBRMat::Metal, Foot + FVector(40.0f, 395.0f, 1290.0f), FVector(360.0f), FRotator::ZeroRotator, BRGold, 0.0f, false);

	// Fountains on the diagonals, flower beds and benches between them.
	for (int32 Corner = 0; Corner < 4; ++Corner)
	{
		const FVector2D F = BROnCircle(FVector2D(0.0, 0.0), 2900.0f, 45.0f + Corner * 90.0f);
		const FVector At = G(F.X, F.Y);
		Disc(At + FVector(0.0f, 0.0f, 45.0f), 620.0f, 90.0f, BRMarble);
		Disc(At + FVector(0.0f, 0.0f, 92.0f), 560.0f, 8.0f, FLinearColor(0.1f, 0.45f, 0.62f), EBRMat::Metal, false);
		Disc(At + FVector(0.0f, 0.0f, 190.0f), 150.0f, 190.0f, BRStone);
		Piece(EBRShape::Sphere, EBRMat::Surface, At + FVector(0.0f, 0.0f, 340.0f), FVector(170.0f), FRotator::ZeroRotator, BRMarble, 0.0f, false);
		for (int32 Beds = 0; Beds < 3; ++Beds)
		{
			const FVector2D B = BROnCircle(F, 1250.0f, Corner * 90.0f + Beds * 120.0f);
			FlowerBed(G(B.X, B.Y), 300.0f, Random);
			Bench(G(B.X + 420.0, B.Y), Beds * 90.0f);
		}
	}

	// ---- The palace, across the head of the plaza.
	const float PX = 6400.0f;
	House(G(PX, 0.0f), FVector2D(1900.0, 3400.0), 620.0f, 0.0f, BRMarble, FLinearColor(0.2f, 0.42f, 0.36f), 2 | 4 | 8, false);
	House(G(PX - 300.0f, 3400.0f), FVector2D(1300.0, 1900.0), 480.0f, 0.0f, BRMarble, FLinearColor(0.2f, 0.42f, 0.36f), 2 | 8, false);
	House(G(PX - 300.0f, -3400.0f), FVector2D(1300.0, 1900.0), 480.0f, 0.0f, BRMarble, FLinearColor(0.2f, 0.42f, 0.36f), 2 | 4, false);
	Dome(G(PX, 0.0f, 620.0f), 860.0f, FLinearColor(0.72f, 0.56f, 0.16f));
	Disc(G(PX, 0.0f, 640.0f), 900.0f, 60.0f, BRMarble, EBRMat::Fade, false);
	Pole(G(PX, 0.0f, 1460.0f), G(PX, 0.0f, 1900.0f), 16.0f, BRSteel, EBRMat::Metal, false);
	Piece(EBRShape::Sphere, EBRMat::Metal, G(PX, 0.0f, 1500.0f), FVector(160.0f), FRotator::ZeroRotator, BRGold, 0.0f, false);
	// Columns along the front and the steps up to them.
	for (int32 Column = -6; Column <= 6; ++Column)
	{
		if (FMath::Abs(Column) == 0) { continue; }
		Disc(G(PX - 1130.0f, Column * 270.0f, 330.0f), 62.0f, 660.0f, BRMarble, EBRMat::Fade);
	}
	Trim(G(PX - 1130.0f, 0.0f, 700.0f), FVector(240.0f, 3700.0f, 90.0f), BRMarble);
	for (int32 Step = 0; Step < 5; ++Step)
	{
		Box(G(PX - 1250.0f - Step * 60.0f, 0.0f, 50.0f - Step * 10.0f), FVector(120.0f, 3800.0f + Step * 120.0f, 100.0f - Step * 20.0f), BRMarble);
	}
	// Banners and the guard posts.
	for (const float Y : {-3000.0f, -1500.0f, 1500.0f, 3000.0f})
	{
		Flag(G(PX - 1650.0f, Y), 860.0f, BRRed);
		Crate(G(PX - 1500.0f, Y + 420.0f), 170.0f, 15.0f, BRWood);
	}

	// ---- Ministries round the plaza, each walkable, lanes between them.
	const int32 Blocks = 22;
	for (int32 Index = 0; Index < Blocks; ++Index)
	{
		const float Angle = 200.0f / Blocks * Index + 80.0f + 0.0f;
		for (int32 Row = 0; Row < 2; ++Row)
		{
			const FVector2D At = BROnCircle(FVector2D(0.0, 0.0), 6400.0f + Row * 1500.0f, Angle + (Row == 1 ? 4.0f : 0.0f));
			if (At.X > 3500.0 && FMath::Abs(At.Y) < 6000.0 && Row == 0) { continue; }
			const float Yaw = Angle;
			const FLinearColor Wall = BRPick(Random, {FLinearColor(0.62f, 0.5f, 0.38f), FLinearColor(0.45f, 0.5f, 0.56f), FLinearColor(0.7f, 0.66f, 0.56f), FLinearColor(0.52f, 0.3f, 0.24f)});
			const FLinearColor Roof = BRPick(Random, {BRDarkStone, FLinearColor(0.34f, 0.18f, 0.1f), FLinearColor(0.1f, 0.26f, 0.3f)});
			House(G(At.X, At.Y), FVector2D(Random.FRandRange(820.0f, 1120.0f), Random.FRandRange(820.0f, 1160.0f)), Random.FRandRange(360.0f, 560.0f), Yaw, Wall, Roof, Random.RandRange(5, 15), false);
		}
	}
	// The ring of lamps, parked cars and the four gates.
	for (int32 Index = 0; Index < 28; ++Index)
	{
		const FVector2D L = BROnCircle(FVector2D(0.0, 0.0), 5400.0f, Index * 360.0f / 28.0f);
		Lamp(G(L.X, L.Y));
		if (Index % 3 == 0)
		{
			const FVector2D Park = BROnCircle(FVector2D(0.0, 0.0), 5850.0f, Index * 360.0f / 28.0f + 3.0f);
			Car(G(Park.X, Park.Y), Index * 360.0f / 28.0f + 93.0f, BRPick(Random, {BRRed, BRBlue, FLinearColor(0.85f, 0.85f, 0.88f), FLinearColor(0.08f, 0.09f, 0.1f), BRGold}));
		}
	}
	for (int32 Gate = 0; Gate < 4; ++Gate)
	{
		if (Gate == 0) { continue; }
		const FRotator Turn(0.0f, Gate * 90.0f, 0.0f);
		const FVector Mid = G(0.0f, 0.0f) + Turn.Vector() * 8800.0f;
		const FVector Side = Turn.RotateVector(FVector(0.0f, 720.0f, 0.0f));
		Piece(EBRShape::Cube, EBRMat::Fade, Mid + Side + FVector(0.0f, 0.0f, 380.0f), FVector(260.0f, 260.0f, 760.0f), Turn, BRMarble, 1.0f, true);
		Piece(EBRShape::Cube, EBRMat::Fade, Mid - Side + FVector(0.0f, 0.0f, 380.0f), FVector(260.0f, 260.0f, 760.0f), Turn, BRMarble, 1.0f, true);
		Piece(EBRShape::Cube, EBRMat::Fade, Mid + FVector(0.0f, 0.0f, 830.0f), FVector(260.0f, 1740.0f, 170.0f), Turn, BRMarble, 1.0f, false);
		Flag(Mid + Side * 1.3f, 920.0f, BRBlue);
		Flag(Mid - Side * 1.3f, 920.0f, BRRed);
	}
	// Blossom along the lawns.
	for (int32 Index = 0; Index < 44; ++Index)
	{
		const FVector2D T = BROnCircle(FVector2D(0.0, 0.0), 4750.0f, Index * 360.0f / 44.0f + 4.0f);
		if (Index % 11 == 0) { continue; }
		Tree(G(T.X, T.Y), Random.FRandRange(0.95f, 1.3f), 3, Random);
	}
}

void ABRIsland::BuildStadium()
{
	using namespace BRLayout;
	const FVector2D C = Stadium.At;
	const float Z = Stadium.Height;
	FRandomStream Random(102);
	const auto G = [&](const float X, const float Y, const float Up = 0.0f) { return FVector(C.X + X, C.Y + Y, Z + Up); };

	// The pitch, a great dodgeball court.
	Box(G(0.0f, 0.0f, 3.0f), FVector(4200.0f, 6000.0f, 6.0f), FLinearColor(0.09f, 0.32f, 0.09f), 0.0f, 0.0f, false);
	for (int32 Strip = 0; Strip < 12; ++Strip)
	{
		Box(G(0.0f, -2750.0f + Strip * 500.0f, 7.0f), FVector(4200.0f, 250.0f, 2.0f), FLinearColor(0.12f, 0.4f, 0.11f), 0.0f, 0.0f, false);
	}
	const FLinearColor Line(0.92f, 0.92f, 0.88f);
	Box(G(1950.0f, 0.0f, 10.0f), FVector(30.0f, 5650.0f, 2.0f), Line, 0.0f, 0.0f, false);
	Box(G(-1950.0f, 0.0f, 10.0f), FVector(30.0f, 5650.0f, 2.0f), Line, 0.0f, 0.0f, false);
	Box(G(0.0f, 2825.0f, 10.0f), FVector(3930.0f, 30.0f, 2.0f), Line, 0.0f, 0.0f, false);
	Box(G(0.0f, -2825.0f, 10.0f), FVector(3930.0f, 30.0f, 2.0f), Line, 0.0f, 0.0f, false);
	Box(G(0.0f, 0.0f, 10.0f), FVector(3930.0f, 30.0f, 2.0f), Line, 0.0f, 0.0f, false);
	Disc(G(0.0f, 0.0f, 10.0f), 620.0f, 2.0f, Line, EBRMat::Surface, false);
	Disc(G(0.0f, 0.0f, 12.0f), 590.0f, 2.0f, FLinearColor(0.1f, 0.34f, 0.1f), EBRMat::Surface, false);
	// Cover on the pitch: padded blocks and giant balls, so a fight here has places to duck behind.
	for (int32 Pad = 0; Pad < 18; ++Pad)
	{
		const FVector2D At(Random.FRandRange(-1700.0f, 1700.0f), Random.FRandRange(-2500.0f, 2500.0f));
		if (At.Size() < 750.0) { continue; }
		Box(G(At.X, At.Y, 80.0f), FVector(Random.FRandRange(240.0f, 520.0f), 150.0f, 160.0f), Pad % 2 ? BRRed : BRBlue, Random.FRandRange(0.0f, 180.0f));
	}
	Dodgeball(G(1200.0f, -900.0f, 220.0f), 220.0f, BRRed, Line, 20.0f);
	Dodgeball(G(-1300.0f, 1250.0f, 220.0f), 220.0f, BRBlue, Line, 70.0f);

	// The stands: an oval of sloped tiers, four wide gates through them.
	constexpr int32 Segments = 72;
	constexpr float RX = 3350.0f, RY = 4300.0f;
	for (int32 Segment = 0; Segment < Segments; ++Segment)
	{
		const float Deg = (Segment + 0.5f) * 360.0f / Segments;
		const float A = FMath::DegreesToRadians(Deg);
		// Gates at the four compass points.
		const float FromGate = FMath::Abs(FMath::Fmod(Deg + 45.0f, 90.0f) - 45.0f);
		if (FromGate < 9.0f) { continue; }
		const FVector2D Way(FMath::Cos(A), FMath::Sin(A));
		const FVector2D Inner(Way.X * RX, Way.Y * RY);
		const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Way.Y * RX, Way.X * RY));
		const FLinearColor Seat = Way.Y > 0.0f ? BRRed : BRBlue;
		const float ArcLength = 2.0f * UE_PI * FMath::Sqrt((RX * RX + RY * RY) * 0.5f) / Segments + 30.0f;
		const FRotator Turn(0.0f, Yaw, 0.0f);
		const FVector Out = Turn.Vector();
		// The stand's bulk: a long ramp you can climb from the pitch.
		const FVector Base = G(static_cast<float>(Inner.X), static_cast<float>(Inner.Y));
		Piece(EBRShape::Cube, EBRMat::Surface, Base + Out * 800.0f + FVector(0.0f, 0.0f, 350.0f), FVector(1760.0f, ArcLength, 40.0f), Turn + FRotator(21.0f, 0.0f, 0.0f), FLinearColor(0.42f, 0.42f, 0.44f), 0.0f, true);
		// Rows of seats on it.
		for (int32 Row = 0; Row < 7; ++Row)
		{
			const float Run = 200.0f + Row * 225.0f;
			Piece(EBRShape::Cube, EBRMat::Surface, Base + Out * Run + FVector(0.0f, 0.0f, Run * 0.385f + 42.0f), FVector(100.0f, ArcLength - 26.0f, 42.0f), Turn, Seat * (Row % 2 ? 0.8f : 1.1f), 0.0f, false, false);
		}
		// The back wall, and the roof that shades the top rows.
		Piece(EBRShape::Cube, EBRMat::Fade, Base + Out * 1660.0f + FVector(0.0f, 0.0f, 480.0f), FVector(60.0f, ArcLength, 960.0f), Turn, BRDarkStone, 1.0f, true);
		if (Segment % 2 == 0)
		{
			Piece(EBRShape::Cube, EBRMat::Fade, Base + Out * 1150.0f + FVector(0.0f, 0.0f, 1090.0f), FVector(1050.0f, ArcLength * 2.0f, 22.0f), Turn + FRotator(-6.0f, 0.0f, 0.0f), FLinearColor(0.8f, 0.82f, 0.86f), 0.0f, false);
		}
		// Banners on the back wall.
		if (Segment % 6 == 2)
		{
			Trim(Base + Out * 1700.0f + FVector(0.0f, 0.0f, 560.0f), FVector(8.0f, 210.0f, 520.0f), BRGold, Yaw);
		}
	}
	// Floodlights at the corners, with big lit screens over two gates.
	for (int32 Corner = 0; Corner < 4; ++Corner)
	{
		const FVector2D At = BROnCircle(FVector2D(0.0, 0.0), 5600.0f, 45.0f + Corner * 90.0f);
		const FVector Foot = G(At.X, At.Y);
		Pole(Foot, Foot + FVector(0.0f, 0.0f, 2000.0f), 70.0f, BRSteel, EBRMat::Metal, true);
		Piece(EBRShape::Cube, EBRMat::Metal, Foot + FVector(0.0f, 0.0f, 2050.0f), FVector(420.0f, 640.0f, 240.0f), FRotator(0.0f, 45.0f + Corner * 90.0f, 0.0f), FLinearColor(0.05f, 0.05f, 0.06f), 0.0f, false);
		Glow(Foot + FVector(0.0f, 0.0f, 2050.0f) - FVector(FMath::Cos(FMath::DegreesToRadians(45.0f + Corner * 90.0f)), FMath::Sin(FMath::DegreesToRadians(45.0f + Corner * 90.0f)), 0.0f) * 216.0f, FVector(8.0f, 580.0f, 190.0f), FLinearColor(1.0f, 0.92f, 0.7f) * 6.0f, 45.0f + Corner * 90.0f);
	}
	for (const float Y : {-5900.0f, 5900.0f})
	{
		const FVector Foot = G(0.0f, Y);
		Box(Foot + FVector(0.0f, 0.0f, 260.0f), FVector(240.0f, 240.0f, 520.0f), BRDarkStone);
		Piece(EBRShape::Cube, EBRMat::Fade, Foot + FVector(0.0f, 0.0f, 880.0f), FVector(90.0f, 1500.0f, 640.0f), FRotator::ZeroRotator, FLinearColor(0.03f, 0.03f, 0.04f), 0.0f, false);
		for (int32 Lit = 0; Lit < 12; ++Lit)
		{
			Glow(Foot + FVector(Y > 0.0f ? -48.0f : 48.0f, -630.0f + Lit * 115.0f, 770.0f + (Lit % 3) * 130.0f), FVector(6.0f, 100.0f, 110.0f), (Lit % 2 ? FLinearColor(0.1f, 0.5f, 1.0f) : FLinearColor(1.0f, 0.2f, 0.25f)) * 3.0f);
		}
	}
	// The car parks and ticket booths round it.
	for (int32 Park = 0; Park < 46; ++Park)
	{
		const FVector2D At = BROnCircle(FVector2D(0.0, 0.0), Random.FRandRange(6200.0f, 7200.0f), Random.FRandRange(0.0f, 360.0f));
		if (FMath::Abs(FMath::Fmod(FMath::RadiansToDegrees(FMath::Atan2(At.Y, At.X)) + 405.0f, 90.0f) - 45.0f) < 12.0f) { continue; }
		Car(G(At.X, At.Y), Random.FRandRange(0.0f, 360.0f), BRPick(Random, {BRRed, BRBlue, BRGold, FLinearColor(0.8f, 0.8f, 0.84f), FLinearColor(0.1f, 0.1f, 0.12f)}));
	}
	for (int32 Booth = 0; Booth < 8; ++Booth)
	{
		const FVector2D At = BROnCircle(FVector2D(0.0, 0.0), 6100.0f, Booth * 90.0f + (Booth % 2 ? 14.0f : -14.0f));
		House(G(At.X, At.Y), FVector2D(420.0, 420.0), 280.0f, Booth * 90.0f, BRMarble, BRBlue, 1, false);
	}
}

void ABRIsland::BuildFactory()
{
	using namespace BRLayout;
	const FVector2D C = Factory.At;
	const float Z = Factory.Height;
	FRandomStream Random(103);
	const auto G = [&](const float X, const float Y, const float Up = 0.0f) { return FVector(C.X + X, C.Y + Y, Z + Up); };
	const FLinearColor Shed(0.42f, 0.46f, 0.48f), Hazard(0.95f, 0.66f, 0.05f), Orange(0.88f, 0.34f, 0.05f);

	// Long sheds with saw-tooth roofs: the ball presses.
	for (int32 Index = 0; Index < 7; ++Index)
	{
		const FVector At = G(-6000.0f + Index * 2000.0f, -3400.0f - (Index % 2) * 500.0f);
		const FVector2D Size(1500.0, 3000.0);
		House(At, Size, 620.0f, 0.0f, Index % 3 == 0 ? FLinearColor(0.56f, 0.25f, 0.14f) : Shed, FLinearColor(0.2f, 0.22f, 0.26f), 1 | 2 | 4, false);
		for (int32 Tooth = 0; Tooth < 5; ++Tooth)
		{
			const FVector Roof = At + FVector(-600.0f + Tooth * 300.0f, 0.0f, 720.0f);
			Piece(EBRShape::Cube, EBRMat::Fade, Roof, FVector(330.0f, Size.Y - 60.0f, 20.0f), FRotator(28.0f, 0.0f, 0.0f), Tooth % 2 ? Shed * 0.72f : FLinearColor(0.34f, 0.56f, 0.7f), 5.0f, false);
		}
		// A press line inside, balls riding along it.
		Box(At + FVector(0.0f, 0.0f, 62.0f), FVector(240.0f, 2500.0f, 26.0f), FLinearColor(0.1f, 0.1f, 0.12f), 0.0f, 5.0f);
		for (int32 Leg = -4; Leg <= 4; ++Leg)
		{
			Box(At + FVector(0.0f, Leg * 290.0f, 28.0f), FVector(200.0f, 26.0f, 56.0f), BRSteel, 0.0f, 0.0f, false);
		}
		for (int32 Ball = -3; Ball <= 3; ++Ball)
		{
			Dodgeball(At + FVector(0.0f, Ball * 340.0f, 138.0f), 62.0f, BRPick(Random, {BRRed, BRBlue, BRGold}), FLinearColor::White, Ball * 22.0f, false);
		}
	}
	// Silos in a row with a walkway on legs along their tops.
	for (int32 Index = 0; Index < 9; ++Index)
	{
		const FVector At = G(-5400.0f + Index * 1200.0f, 3200.0f);
		Piece(EBRShape::Cylinder, EBRMat::Fade, At + FVector(0.0f, 0.0f, 720.0f), FVector(860.0f, 860.0f, 1440.0f), FRotator::ZeroRotator, Index % 2 ? FLinearColor(0.72f, 0.72f, 0.7f) : Orange, 5.0f, true);
		Piece(EBRShape::Cone, EBRMat::Fade, At + FVector(0.0f, 0.0f, 1540.0f), FVector(880.0f, 880.0f, 240.0f), FRotator::ZeroRotator, BRDarkStone, 0.0f, false);
		Trim(At + FVector(0.0f, 0.0f, 860.0f), FVector(880.0f, 880.0f, 10.0f), Hazard);
	}
	// Chimneys with lights on them.
	for (const FVector2D& At : {FVector2D(5400.0, -1100.0), FVector2D(6400.0, -700.0), FVector2D(7200.0, -1500.0)})
	{
		const FVector Foot = G(At.X, At.Y);
		Piece(EBRShape::Cylinder, EBRMat::Fade, Foot + FVector(0.0f, 0.0f, 1500.0f), FVector(460.0f, 460.0f, 3000.0f), FRotator::ZeroRotator, FLinearColor(0.42f, 0.2f, 0.14f), 1.0f, true);
		for (int32 Band = 0; Band < 3; ++Band)
		{
			Trim(Foot + FVector(0.0f, 0.0f, 1600.0f + Band * 500.0f), FVector(475.0f, 475.0f, 110.0f), Band % 2 ? FLinearColor(0.9f, 0.9f, 0.88f) : BRRed);
		}
		Glow(Foot + FVector(0.0f, 0.0f, 3030.0f), FVector(56.0f), FLinearColor(1.0f, 0.1f, 0.1f) * 5.0f, 0.0f, EBRShape::Sphere);
	}
	// A pipe bridge overhead between the sheds and the silos.
	for (int32 Run = 0; Run < 5; ++Run)
	{
		const float X = -5200.0f + Run * 2600.0f;
		for (const float Y : {-1300.0f, 0.0f, 1300.0f})
		{
			Pole(G(X, Y, 0.0f), G(X, Y, 420.0f), 26.0f, BRSteel);
		}
		Pole(G(X, -2400.0f, 420.0f), G(X, 2500.0f, 420.0f), 60.0f, Run % 2 ? BRRust : FLinearColor(0.12f, 0.36f, 0.42f), EBRMat::Metal, false);
	}
	// Yards: stacked balls in cages, drums, crates, parked lorries.
	for (int32 Index = 0; Index < 34; ++Index)
	{
		const FVector2D At(Random.FRandRange(-7500.0f, 7500.0f), Random.FRandRange(-500.0f, 1900.0f));
		const FVector Foot = G(At.X, At.Y);
		switch (Index % 4)
		{
		case 0: Truck(Foot, Random.FRandRange(0.0f, 360.0f), BRPick(Random, {Orange, BRBlue, FLinearColor(0.9f, 0.9f, 0.88f)})); break;
		case 1:
			for (int32 Drum = 0; Drum < 5; ++Drum) { Barrel(Foot + FVector(Random.FRandRange(-160.0f, 160.0f), Random.FRandRange(-160.0f, 160.0f), 0.0f), BRPick(Random, {Hazard, BRRust, BRBlue})); }
			break;
		case 2:
			Crate(Foot, 220.0f, Random.FRandRange(0.0f, 90.0f), BRWood);
			Crate(Foot + FVector(40.0f, 20.0f, 220.0f), 150.0f, Random.FRandRange(0.0f, 90.0f), BRWood * 1.2f);
			break;
		default:
			// A cage of finished balls.
			Box(Foot + FVector(0.0f, 0.0f, 12.0f), FVector(560.0f, 560.0f, 24.0f), BRSteel);
			for (int32 Ball = 0; Ball < 9; ++Ball)
			{
				Dodgeball(Foot + FVector((Ball % 3 - 1) * 170.0f, (Ball / 3 - 1) * 170.0f, 104.0f), 80.0f, BRPick(Random, {BRRed, BRBlue, BRGold}), FLinearColor::White, Ball * 35.0f, false);
			}
			Dodgeball(Foot + FVector(0.0f, 0.0f, 250.0f), 80.0f, BRRed, FLinearColor::White, 12.0f, false);
			for (const FVector2D& Corner : {FVector2D(-1, -1), FVector2D(1, -1), FVector2D(-1, 1), FVector2D(1, 1)})
			{
				Pole(Foot + FVector(Corner.X * 270.0, Corner.Y * 270.0, 0.0), Foot + FVector(Corner.X * 270.0, Corner.Y * 270.0, 320.0), 8.0f, BRSteel, EBRMat::Metal, false);
			}
			break;
		}
	}
	// Hazard stripes on the yard, and the perimeter fence with gaps.
	for (int32 Stripe = -10; Stripe <= 10; ++Stripe)
	{
		Box(G(Stripe * 640.0f, -1800.0f, 3.0f), FVector(300.0f, 90.0f, 4.0f), Stripe % 2 ? Hazard : FLinearColor(0.05f, 0.05f, 0.06f), 45.0f, 0.0f, false);
	}
	for (int32 Side = 0; Side < 36; ++Side)
	{
		if (Side % 4 == 0) { continue; }
		const FVector2D A = BROnCircle(FVector2D(0.0, 0.0), 8700.0f, Side * 10.0f), B = BROnCircle(FVector2D(0.0, 0.0), 8700.0f, (Side + 1) * 10.0f);
		Fence(G(A.X, A.Y), G(B.X, B.Y), BRSteel, 190.0f);
	}
	for (int32 Index = 0; Index < 20; ++Index)
	{
		const FVector2D L = BROnCircle(FVector2D(0.0, 0.0), 7800.0f, Index * 18.0f + 5.0f);
		Lamp(G(L.X, L.Y), 640.0f);
	}
}

void ABRIsland::BuildPort()
{
	using namespace BRLayout;
	const FVector2D C = Port.At;
	const float Z = Port.Height;
	FRandomStream Random(104);
	const auto G = [&](const float X, const float Y, const float Up = 0.0f) { return FVector(C.X + X, C.Y + Y, Z + Up); };
	const FLinearColor Tones[] = {BRRed, BRBlue, FLinearColor(0.1f, 0.42f, 0.2f), FLinearColor(0.9f, 0.5f, 0.06f), FLinearColor(0.5f, 0.1f, 0.4f), FLinearColor(0.82f, 0.82f, 0.8f), FLinearColor(0.16f, 0.46f, 0.55f)};

	// Rows of containers, stacked unevenly, lanes wide enough to fight through.
	for (int32 Row = 0; Row < 8; ++Row)
	{
		for (int32 Column = 0; Column < 7; ++Column)
		{
			const FVector Foot = G(-5400.0f + Row * 1320.0f, -5700.0f + Column * 1050.0f);
			if (Random.FRand() < 0.14f) { continue; }
			const int32 Stack = Random.RandRange(1, 3);
			for (int32 Level = 0; Level < Stack; ++Level)
			{
				Container(Foot + FVector(Random.FRandRange(-16.0f, 16.0f), Random.FRandRange(-16.0f, 16.0f), Level * 262.0f), 0.0f + Random.FRandRange(-1.5f, 1.5f), Tones[Random.RandHelper(UE_ARRAY_COUNT(Tones))]);
			}
		}
	}
	// Gantry cranes straddling the rows, their booms over the quay.
	for (int32 Crane = 0; Crane < 4; ++Crane)
	{
		const FVector Foot = G(-4600.0f + Crane * 3100.0f, 4400.0f);
		const FLinearColor Paint = Crane % 2 ? FLinearColor(0.9f, 0.45f, 0.05f) : FLinearColor(0.08f, 0.3f, 0.6f);
		for (const FVector2D& Corner : {FVector2D(-420, -520), FVector2D(420, -520), FVector2D(-420, 520), FVector2D(420, 520)})
		{
			Pole(Foot + FVector(Corner.X, Corner.Y, 0.0), Foot + FVector(Corner.X, Corner.Y, 1500.0), 48.0f, Paint);
		}
		Beam(Foot + FVector(0.0f, -1800.0f, 1560.0f), Foot + FVector(0.0f, 3600.0f, 1560.0f), 130.0f, 150.0f, Paint, EBRMat::Fade, false);
		Beam(Foot + FVector(0.0f, 0.0f, 1500.0f), Foot + FVector(0.0f, 0.0f, 2300.0f), 80.0f, 80.0f, Paint, EBRMat::Metal, false);
		Beam(Foot + FVector(0.0f, 0.0f, 2300.0f), Foot + FVector(0.0f, 3400.0f, 1640.0f), 16.0f, 16.0f, BRSteel, EBRMat::Metal, false);
		Beam(Foot + FVector(0.0f, 0.0f, 2300.0f), Foot + FVector(0.0f, -1700.0f, 1640.0f), 16.0f, 16.0f, BRSteel, EBRMat::Metal, false);
		Box(Foot + FVector(0.0f, 2100.0f, 1450.0f), FVector(300.0f, 300.0f, 130.0f), BRDarkStone, 0.0f, 0.0f, false);
		Pole(Foot + FVector(0.0f, 2100.0f, 1400.0f), Foot + FVector(0.0f, 2100.0f, 520.0f), 6.0f, BRSteel, EBRMat::Metal, false);
		Container(Foot + FVector(0.0f, 2100.0f, 262.0f), 90.0f, Tones[Crane]);
	}
	// The quay edge: a long concrete wall, bollards and a moored freighter.
	const float QuayY = 9100.0f;
	Box(G(0.0f, QuayY, -150.0f), FVector(18000.0f, 340.0f, 500.0f), BRDarkStone, 0.0f, 1.0f);
	Box(G(0.0f, QuayY, 104.0f), FVector(18000.0f, 340.0f, 8.0f), FLinearColor(0.95f, 0.66f, 0.05f), 0.0f, 0.0f, false);
	for (int32 Bollard = -9; Bollard <= 9; ++Bollard)
	{
		Disc(G(Bollard * 950.0f, QuayY - 120.0f, 62.0f), 46.0f, 120.0f, FLinearColor(0.08f, 0.08f, 0.1f), EBRMat::Metal);
	}
	// The ship: hull, deck, tall bridge, and a cargo of containers on it.
	const FVector Ship = G(1000.0f, QuayY + 1650.0f, -120.0f);
	const FLinearColor Hull(0.1f, 0.14f, 0.28f);
	Box(Ship + FVector(0.0f, 0.0f, 360.0f), FVector(9000.0f, 1900.0f, 860.0f), Hull, 0.0f, 5.0f);
	Trim(Ship + FVector(0.0f, 0.0f, 130.0f), FVector(9030.0f, 1930.0f, 110.0f), BRRed);
	Box(Ship + FVector(5100.0f, 0.0f, 360.0f), FVector(1450.0f, 1300.0f, 860.0f), Hull, 0.0f, 5.0f);
	Box(Ship + FVector(0.0f, 0.0f, 800.0f), FVector(8900.0f, 1800.0f, 24.0f), FLinearColor(0.38f, 0.26f, 0.15f), 0.0f, 4.0f);
	House(Ship + FVector(-3700.0f, 0.0f, 812.0f), FVector2D(1300.0, 1400.0), 720.0f, 0.0f, FLinearColor(0.88f, 0.88f, 0.86f), BRDarkStone, 1 | 4 | 8, false);
	Piece(EBRShape::Cylinder, EBRMat::Fade, Ship + FVector(-3700.0f, 0.0f, 2100.0f), FVector(420.0f, 420.0f, 900.0f), FRotator::ZeroRotator, BRRed, 0.0f, false);
	for (int32 Row = 0; Row < 7; ++Row)
	{
		for (int32 Column = -1; Column <= 1; ++Column)
		{
			const int32 Stack = Random.RandRange(1, 3);
			for (int32 Level = 0; Level < Stack; ++Level)
			{
				Container(Ship + FVector(-2200.0f + Row * 800.0f, Column * 480.0f, 812.0f + Level * 262.0f), 0.0f, Tones[Random.RandHelper(UE_ARRAY_COUNT(Tones))]);
			}
		}
	}
	// A wide gangway ramp up to its deck.
	Piece(EBRShape::Cube, EBRMat::Surface, G(1500.0f, QuayY + 340.0f, 390.0f), FVector(1540.0f, 900.0f, 30.0f), FRotator(0.0f, 90.0f, 0.0f) + FRotator(17.0f, 0.0f, 0.0f), BRSteel, 5.0f, true);

	// Warehouses along the land side, and the harbour offices.
	for (int32 Index = 0; Index < 5; ++Index)
	{
		House(G(-6400.0f + Index * 3200.0f, -7600.0f), FVector2D(2200.0, 1500.0), 620.0f, 0.0f, BRPick(Random, {FLinearColor(0.42f, 0.28f, 0.2f), FLinearColor(0.36f, 0.42f, 0.46f)}), BRDarkStone, 1 | 2 | 4 | 8, false);
	}
	// The lighthouse on the end of the mole.
	const FVector Light = G(-9100.0f, QuayY - 220.0f);
	Piece(EBRShape::Cylinder, EBRMat::Fade, Light + FVector(0.0f, 0.0f, 1100.0f), FVector(560.0f, 560.0f, 2200.0f), FRotator::ZeroRotator, FLinearColor(0.92f, 0.92f, 0.9f), 0.0f, true);
	for (int32 Band = 0; Band < 3; ++Band)
	{
		Trim(Light + FVector(0.0f, 0.0f, 500.0f + Band * 600.0f), FVector(575.0f, 575.0f, 240.0f), BRRed);
	}
	Glow(Light + FVector(0.0f, 0.0f, 2340.0f), FVector(300.0f, 300.0f, 260.0f), FLinearColor(1.0f, 0.86f, 0.5f) * 6.0f, 0.0f, EBRShape::Cylinder);
	for (int32 Index = 0; Index < 26; ++Index)
	{
		const FVector Foot = G(Random.FRandRange(-8000.0f, 8000.0f), Random.FRandRange(-6700.0f, 8600.0f));
		if (Index % 3 == 0) { Truck(Foot, Random.FRandRange(0.0f, 360.0f), Tones[Random.RandHelper(UE_ARRAY_COUNT(Tones))]); }
		else { for (int32 Drum = 0; Drum < 4; ++Drum) { Barrel(Foot + FVector(Random.FRandRange(-130.0f, 130.0f), Random.FRandRange(-130.0f, 130.0f), 0.0f), BRPick(Random, {BRRust, BRBlue, FLinearColor(0.2f, 0.42f, 0.2f)})); } }
	}
}

void ABRIsland::BuildAirfield()
{
	using namespace BRLayout;
	const FVector2D C = Airfield.At;
	const float Z = Airfield.Height;
	FRandomStream Random(105);
	const auto G = [&](const float X, const float Y, const float Up = 0.0f) { return FVector(C.X + X, C.Y + Y, Z + Up); };
	const FLinearColor White(0.9f, 0.9f, 0.88f), Olive(0.28f, 0.32f, 0.14f);

	// The runway, its centre line and edge lights.
	Box(G(0.0f, -1500.0f, 4.0f), FVector(15200.0f, 1700.0f, 8.0f), FLinearColor(0.11f, 0.11f, 0.12f), 0.0f, 0.0f, false);
	for (int32 Dash = -14; Dash <= 14; ++Dash)
	{
		Box(G(Dash * 520.0f, -1500.0f, 9.0f), FVector(300.0f, 34.0f, 2.0f), White, 0.0f, 0.0f, false);
		for (const float Side : {-2340.0f, -660.0f})
		{
			Glow(G(Dash * 520.0f + 130.0f, Side, 22.0f), FVector(26.0f, 26.0f, 40.0f), FLinearColor(0.3f, 0.7f, 1.0f) * 4.0f, 0.0f, EBRShape::Cylinder);
		}
	}
	// Hangars you can run through, an aeroplane in each.
	const auto Plane = [&](const FVector& Foot, const float Yaw, const FLinearColor& Paint)
	{
		const FRotator Turn(0.0f, Yaw, 0.0f);
		const auto At = [&](const float X, const float Y, const float Up) { return Foot + Turn.RotateVector(FVector(X, Y, Up)); };
		Piece(EBRShape::Cylinder, EBRMat::Fade, At(0.0f, 0.0f, 210.0f), FVector(250.0f, 250.0f, 1500.0f), Turn + FRotator(90.0f, 0.0f, 0.0f), Paint, 0.0f, true);
		Piece(EBRShape::Cone, EBRMat::Fade, At(860.0f, 0.0f, 210.0f), FVector(250.0f, 250.0f, 300.0f), Turn + FRotator(-90.0f, 0.0f, 0.0f), Paint * 0.8f, 0.0f, false);
		Piece(EBRShape::Cube, EBRMat::Fade, At(60.0f, 0.0f, 200.0f), FVector(320.0f, 2200.0f, 34.0f), Turn, Paint * 0.85f, 0.0f, true);
		Piece(EBRShape::Cube, EBRMat::Fade, At(-720.0f, 0.0f, 270.0f), FVector(190.0f, 720.0f, 24.0f), Turn, Paint * 0.85f, 0.0f, false);
		Piece(EBRShape::Cube, EBRMat::Fade, At(-740.0f, 0.0f, 410.0f), FVector(190.0f, 22.0f, 300.0f), Turn, BRRed, 0.0f, false);
		Trim(At(330.0f, 0.0f, 306.0f), FVector(220.0f, 150.0f, 70.0f), FLinearColor(0.08f, 0.14f, 0.24f), Yaw);
		for (const float Y : {-290.0f, 290.0f}) { Disc(At(40.0f, Y, 42.0f), 46.0f, 84.0f, FLinearColor(0.03f, 0.03f, 0.04f), EBRMat::Surface, false); }
	};
	for (int32 Hangar = 0; Hangar < 5; ++Hangar)
	{
		const FVector At = G(-6000.0f + Hangar * 3000.0f, 3400.0f);
		House(At, FVector2D(2400.0, 2200.0), 760.0f, 0.0f, Hangar % 2 ? Olive : FLinearColor(0.46f, 0.48f, 0.5f), FLinearColor(0.24f, 0.26f, 0.28f), 4 | 8, true);
		Plane(At, Hangar * 30.0f - 60.0f, BRPick(Random, {White, FLinearColor(0.72f, 0.1f, 0.1f), FLinearColor(0.1f, 0.3f, 0.6f)}));
	}
	Plane(G(-3300.0f, -1400.0f), 12.0f, White);
	Plane(G(4700.0f, -1700.0f), 170.0f, FLinearColor(0.85f, 0.45f, 0.06f));
	// The control tower: a stair-ramped base and a lit glass top.
	const FVector Tower = G(6600.0f, 1800.0f);
	Piece(EBRShape::Cylinder, EBRMat::Fade, Tower + FVector(0.0f, 0.0f, 900.0f), FVector(520.0f, 520.0f, 1800.0f), FRotator::ZeroRotator, FLinearColor(0.82f, 0.82f, 0.8f), 1.0f, true);
	Piece(EBRShape::Cylinder, EBRMat::Fade, Tower + FVector(0.0f, 0.0f, 2010.0f), FVector(1000.0f, 1000.0f, 420.0f), FRotator::ZeroRotator, FLinearColor(0.07f, 0.16f, 0.26f), 0.0f, true);
	Disc(Tower + FVector(0.0f, 0.0f, 2240.0f), 560.0f, 40.0f, BRDarkStone, EBRMat::Fade, false);
	Glow(Tower + FVector(0.0f, 0.0f, 2360.0f), FVector(70.0f), FLinearColor(1.0f, 0.15f, 0.15f) * 6.0f, 0.0f, EBRShape::Sphere);
	// Radar, fuel tanks, sandbags and supply crates.
	for (int32 Tank = 0; Tank < 6; ++Tank)
	{
		const FVector Foot = G(-7000.0f + Tank * 620.0f, 6700.0f);
		Piece(EBRShape::Sphere, EBRMat::Fade, Foot + FVector(0.0f, 0.0f, 310.0f), FVector(580.0f), FRotator::ZeroRotator, FLinearColor(0.86f, 0.86f, 0.82f), 0.0f, true);
	}
	for (int32 Index = 0; Index < 30; ++Index)
	{
		const FVector Foot = G(Random.FRandRange(-7200.0f, 7200.0f), Random.FRandRange(300.0f, 1900.0f));
		if (Index % 5 == 0) { Truck(Foot, Random.FRandRange(0.0f, 360.0f), Olive); }
		else if (Index % 2) { Crate(Foot, Random.FRandRange(150.0f, 240.0f), Random.FRandRange(0.0f, 90.0f), Olive * 1.4f); }
		else { Box(Foot + FVector(0.0f, 0.0f, 55.0f), FVector(520.0f, 110.0f, 110.0f), FLinearColor(0.5f, 0.42f, 0.26f), Random.FRandRange(0.0f, 180.0f), 3.0f); }
	}
}

void ABRIsland::BuildLab()
{
	using namespace BRLayout;
	const FVector2D C = Lab.At;
	const float Z = Lab.Height;
	const auto G = [&](const float X, const float Y, const float Up = 0.0f) { return FVector(C.X + X, C.Y + Y, Z + Up); };
	const FLinearColor Hull(0.82f, 0.86f, 0.9f);
	// The ball research institute: white domes joined by glowing walkways.
	const struct { FVector2D At; float R; } Domes[] = {{FVector2D(0.0, 0.0), 1500.0f}, {FVector2D(2400.0, 900.0), 900.0f}, {FVector2D(-2000.0, 1700.0), 1000.0f}, {FVector2D(600.0, -2500.0), 1050.0f}, {FVector2D(-2300.0, -1600.0), 760.0f}};
	for (const auto& D : Domes)
	{
		const FVector At = G(static_cast<float>(D.At.X), static_cast<float>(D.At.Y));
		Disc(At + FVector(0.0f, 0.0f, 80.0f), D.R, 160.0f, BRDarkStone);
		Piece(EBRShape::Sphere, EBRMat::Fade, At + FVector(0.0f, 0.0f, 160.0f), FVector(D.R * 1.96f), FRotator::ZeroRotator, Hull, 5.0f, true);
		Glow(At + FVector(0.0f, 0.0f, 164.0f), FVector(D.R * 1.94f, D.R * 1.94f, 8.0f), FLinearColor(0.15f, 0.7f, 1.0f) * 1.2f, 0.0f, EBRShape::Cylinder);
		Piece(EBRShape::Sphere, EBRMat::Metal, At + FVector(0.0f, 0.0f, 300.0f), FVector(D.R * 0.42f), FRotator::ZeroRotator, (D.R > 1000.0f ? BRGold : D.R > 800.0f ? BRRed : BRBlue), 0.0f, true);
	}
	const FVector Middle = G(0.0f, 0.0f, 90.0f);
	for (const auto& D : Domes)
	{
		if (D.At.IsNearlyZero()) { continue; }
		const FVector To = G(static_cast<float>(D.At.X), static_cast<float>(D.At.Y), 90.0f);
		Beam(Middle, To, 520.0f, 16.0f, FLinearColor(0.34f, 0.38f, 0.42f), EBRMat::Surface, true);
		Beam(Middle + FVector(0.0f, 0.0f, 10.0f), To + FVector(0.0f, 0.0f, 10.0f), 40.0f, 4.0f, FLinearColor(0.15f, 0.7f, 1.0f) * 3.0f, EBRMat::Glow, false);
	}
	// Antennae and a ring of light posts.
	for (int32 Mast = 0; Mast < 6; ++Mast)
	{
		const FVector2D At = BROnCircle(FVector2D(0.0, 0.0), 3900.0f, Mast * 60.0f + 20.0f);
		const FVector Foot = G(At.X, At.Y);
		Pole(Foot, Foot + FVector(0.0f, 0.0f, 1300.0f), 26.0f, BRSteel);
		Glow(Foot + FVector(0.0f, 0.0f, 1320.0f), FVector(60.0f), FLinearColor(0.2f, 0.8f, 1.0f) * 6.0f, 0.0f, EBRShape::Sphere);
		Dome(Foot + FVector(0.0f, 0.0f, 90.0f), 120.0f, Hull, EBRMat::Surface, true);
	}
}

void ABRIsland::BuildTown(const FBRDistrict& District, const int32 Seed)
{
	FRandomStream Random(Seed);
	const FVector2D C = District.At;
	const float Z = District.Height;
	const float R = District.Radius;
	const auto G = [&](const float X, const float Y, const float Up = 0.0f) { return FVector(C.X + X, C.Y + Y, Z + Up); };
	const FLinearColor Asphalt(0.11f, 0.11f, 0.12f), Kerb(0.5f, 0.48f, 0.45f);
	const float Block = 2500.0f;
	const int32 Span = FMath::FloorToInt(R / Block);

	// The streets: a grid with kerbs and dashes.
	for (int32 Street = -Span; Street <= Span; ++Street)
	{
		const float Off = Street * Block;
		const float Reach = FMath::Sqrt(FMath::Max(R * R - Off * Off, 0.0f));
		for (const bool bAlongX : {true, false})
		{
			const FVector Mid = bAlongX ? G(0.0f, Off, 4.0f) : G(Off, 0.0f, 4.0f);
			Box(Mid, bAlongX ? FVector(Reach * 2.0f, 560.0f, 8.0f) : FVector(560.0f, Reach * 2.0f, 8.0f), Asphalt, 0.0f, 0.0f, false);
			Box(Mid + (bAlongX ? FVector(0.0f, 320.0f, 4.0f) : FVector(320.0f, 0.0f, 4.0f)), bAlongX ? FVector(Reach * 2.0f, 80.0f, 16.0f) : FVector(80.0f, Reach * 2.0f, 16.0f), Kerb, 0.0f, 0.0f, false);
			Box(Mid - (bAlongX ? FVector(0.0f, 320.0f, -4.0f) : FVector(320.0f, 0.0f, -4.0f)), bAlongX ? FVector(Reach * 2.0f, 80.0f, 16.0f) : FVector(80.0f, Reach * 2.0f, 16.0f), Kerb, 0.0f, 0.0f, false);
			for (float Along = -Reach + 200.0f; Along < Reach; Along += 520.0f)
			{
				Box(bAlongX ? G(Along, Off, 9.0f) : G(Off, Along, 9.0f), bAlongX ? FVector(240.0f, 18.0f, 2.0f) : FVector(18.0f, 240.0f, 2.0f), FLinearColor(0.85f, 0.8f, 0.5f), 0.0f, 0.0f, false);
			}
		}
	}
	// The blocks: houses, gardens, a shop row or a school.
	const FLinearColor Walls[] = {{0.82f, 0.74f, 0.6f}, {0.72f, 0.5f, 0.38f}, {0.56f, 0.66f, 0.72f}, {0.86f, 0.82f, 0.74f}, {0.74f, 0.42f, 0.32f}, {0.6f, 0.7f, 0.5f}};
	const FLinearColor Roofs[] = {{0.5f, 0.13f, 0.08f}, {0.12f, 0.26f, 0.42f}, {0.3f, 0.2f, 0.14f}, {0.2f, 0.34f, 0.26f}};
	for (int32 BX = -Span; BX < Span; ++BX)
	{
		for (int32 BY = -Span; BY < Span; ++BY)
		{
			const FVector2D Mid((BX + 0.5f) * Block, (BY + 0.5f) * Block);
			if (Mid.Size() > R - 700.0f) { continue; }
			const int32 Kind = Random.RandRange(0, 9);
			const FVector Centre = G(static_cast<float>(Mid.X), static_cast<float>(Mid.Y));
			if (Kind == 0)
			{
				// A little park: lawn, trees, a pond and a play area.
				Disc(Centre + FVector(0.0f, 0.0f, 8.0f), 560.0f, 12.0f, FLinearColor(0.1f, 0.36f, 0.62f), EBRMat::Metal, false);
				for (int32 Tree2 = 0; Tree2 < 7; ++Tree2)
				{
					const FVector2D At = BROnCircle(FVector2D(Centre), 850.0f, Tree2 * 51.0f + Random.FRandRange(0.0f, 20.0f));
					Tree(FVector(At.X, At.Y, Z), Random.FRandRange(0.8f, 1.2f), Tree2 % 3 == 0 ? 3 : 0, Random);
				}
				Bench(Centre + FVector(620.0f, 0.0f, 0.0f), 90.0f);
				Bench(Centre + FVector(-620.0f, 0.0f, 0.0f), -90.0f);
				Pole(Centre + FVector(0.0f, 650.0f, 0.0f), Centre + FVector(0.0f, 650.0f, 330.0f), 12.0f, BRRed);
				Pole(Centre + FVector(320.0f, 650.0f, 0.0f), Centre + FVector(320.0f, 650.0f, 330.0f), 12.0f, BRRed);
				Beam(Centre + FVector(0.0f, 650.0f, 330.0f), Centre + FVector(320.0f, 650.0f, 330.0f), 12.0f, 12.0f, BRGold, EBRMat::Metal, false);
			}
			else if (Kind <= 2)
			{
				// A shop row with awnings and lit signs (colour only).
				House(Centre, FVector2D(1650.0, 820.0), 400.0f, 0.0f, Walls[Random.RandHelper(6)], BRDarkStone, 1 | 2 | 8, false);
				for (int32 Awning = -2; Awning <= 2; ++Awning)
				{
					const FLinearColor Tint = BRPick(Random, {BRRed, BRBlue, BRGold, FLinearColor(0.1f, 0.6f, 0.3f)});
					Piece(EBRShape::Cube, EBRMat::Fade, Centre + FVector(Awning * 320.0f, -570.0f, 270.0f), FVector(290.0f, 340.0f, 12.0f), FRotator(0.0f, 0.0f, 14.0f), Tint, 4.0f, false);
					Glow(Centre + FVector(Awning * 320.0f, -432.0f, 350.0f), FVector(220.0f, 8.0f, 50.0f), Tint * 3.0f);
				}
			}
			else
			{
				// Houses with gardens.
				const int32 Count = Random.RandRange(2, 3);
				for (int32 Index = 0; Index < Count; ++Index)
				{
					const FVector At = Centre + FVector((Index - (Count - 1) * 0.5f) * 880.0f, Random.FRandRange(-180.0f, 180.0f), 0.0f);
					const float Yaw = Random.RandHelper(4) * 90.0f;
					House(At, FVector2D(Random.FRandRange(620.0f, 780.0f), Random.FRandRange(620.0f, 780.0f)), 300.0f, Yaw, Walls[Random.RandHelper(6)], Roofs[Random.RandHelper(4)], Random.RandRange(3, 15), Random.FRand() < 0.78f);
				}
				const float H = 880.0f;
				Fence(Centre + FVector(-H, -H, 0.0f), Centre + FVector(H, -H, 0.0f), FLinearColor(0.8f, 0.8f, 0.76f));
				if (Random.FRand() < 0.5f) { Tree(Centre + FVector(Random.FRandRange(-700.0f, 700.0f), 720.0f, 0.0f), Random.FRandRange(0.7f, 1.0f), 0, Random); }
				if (Random.FRand() < 0.35f) { Car(Centre + FVector(Random.FRandRange(-700.0f, 700.0f), -780.0f, 0.0f), Random.RandHelper(2) * 180.0f, BRPick(Random, {BRRed, BRBlue, BRGold, FLinearColor(0.8f, 0.8f, 0.84f)})); }
			}
		}
	}
	// A church in the middle: a town has somewhere tall to climb to.
	const FVector Church = G(0.0f, 0.0f);
	for (int32 Wipe = 0; Wipe < 1; ++Wipe)
	{
		House(Church + FVector(420.0f, 420.0f, 20.0f), FVector2D(1500.0, 800.0), 520.0f, 0.0f, FLinearColor(0.88f, 0.86f, 0.8f), FLinearColor(0.32f, 0.16f, 0.12f), 1 | 2 | 8, true);
		Piece(EBRShape::Cube, EBRMat::Fade, Church + FVector(-540.0f, 420.0f, 620.0f), FVector(460.0f, 460.0f, 1200.0f), FRotator::ZeroRotator, FLinearColor(0.82f, 0.8f, 0.74f), 1.0f, true);
		Piece(EBRShape::Cone, EBRMat::Fade, Church + FVector(-540.0f, 420.0f, 1500.0f), FVector(600.0f, 600.0f, 560.0f), FRotator::ZeroRotator, BRDarkStone, 0.0f, false);
	}
	// Street lamps along the crossings.
	for (int32 BX = -Span; BX <= Span; ++BX)
	{
		for (int32 BY = -Span; BY <= Span; ++BY)
		{
			const FVector2D At(BX * Block + 380.0f, BY * Block + 380.0f);
			if (At.Size() < R - 300.0f) { Lamp(G(At.X, At.Y), 470.0f); }
		}
	}
}

void ABRIsland::BuildFarm(const FBRDistrict& District, const int32 Seed)
{
	FRandomStream Random(Seed);
	const FVector2D C = District.At;
	const float Z = District.Height;
	const float R = District.Radius;
	const auto G = [&](const float X, const float Y, const float Up = 0.0f) { return FVector(C.X + X, C.Y + Y, Z + Up); };
	const FLinearColor Crops[] = {{0.72f, 0.56f, 0.1f}, {0.18f, 0.42f, 0.08f}, {0.54f, 0.32f, 0.08f}, {0.42f, 0.55f, 0.12f}, {0.38f, 0.14f, 0.34f}};

	// Patchwork fields of raised rows, tall enough to crouch among.
	const float Plot = 2300.0f;
	const int32 Span = FMath::FloorToInt(R / Plot);
	for (int32 PX = -Span; PX < Span; ++PX)
	{
		for (int32 PY = -Span; PY < Span; ++PY)
		{
			const FVector2D Mid((PX + 0.5f) * Plot, (PY + 0.5f) * Plot);
			if (Mid.Size() > R - 900.0f) { continue; }
			const FLinearColor Crop = Crops[Random.RandHelper(UE_ARRAY_COUNT(Crops))];
			const bool bAlongX = Random.FRand() < 0.5f;
			const float H = 1020.0f;
			for (int32 Row = -9; Row <= 9; ++Row)
			{
				const float Off = Row * 112.0f;
				const FVector At = G(static_cast<float>(Mid.X) + (bAlongX ? 0.0f : Off), static_cast<float>(Mid.Y) + (bAlongX ? Off : 0.0f), 40.0f);
				Piece(EBRShape::Cube, EBRMat::Leaves, At, bAlongX ? FVector(H * 2.0f, 70.0f, 92.0f) : FVector(70.0f, H * 2.0f, 92.0f), FRotator::ZeroRotator, Crop * Random.FRandRange(0.85f, 1.15f), 0.0f, false, false);
			}
			// Now and then something in the field: a hay roll, a scarecrow, or a giant pumpkin.
			for (int32 Extra = 0; Extra < Random.RandRange(1, 3); ++Extra)
			{
				const FVector Foot = G(static_cast<float>(Mid.X) + Random.FRandRange(-800.0f, 800.0f), static_cast<float>(Mid.Y) + Random.FRandRange(-800.0f, 800.0f));
				const int32 Kind = Random.RandHelper(4);
				if (Kind == 0)
				{
					Piece(EBRShape::Cylinder, EBRMat::Surface, Foot + FVector(0.0f, 0.0f, 100.0f), FVector(200.0f, 200.0f, 210.0f), FRotator(90.0f, Random.FRandRange(0.0f, 180.0f), 0.0f), FLinearColor(0.72f, 0.56f, 0.2f), 3.0f, true);
				}
				else if (Kind == 1)
				{
					Pole(Foot, Foot + FVector(0.0f, 0.0f, 240.0f), 7.0f, BRWood, EBRMat::Surface, false);
					Beam(Foot + FVector(0.0f, -100.0f, 180.0f), Foot + FVector(0.0f, 100.0f, 180.0f), 8.0f, 8.0f, BRWood, EBRMat::Surface, false);
					Piece(EBRShape::Sphere, EBRMat::Surface, Foot + FVector(0.0f, 0.0f, 250.0f), FVector(56.0f), FRotator::ZeroRotator, FLinearColor(0.75f, 0.65f, 0.42f), 0.0f, false);
					Piece(EBRShape::Cone, EBRMat::Surface, Foot + FVector(0.0f, 0.0f, 290.0f), FVector(90.0f, 90.0f, 60.0f), FRotator::ZeroRotator, BRRed, 0.0f, false);
				}
				else
				{
					const float Size = Random.FRandRange(170.0f, 360.0f);
					Piece(EBRShape::Sphere, EBRMat::Surface, Foot + FVector(0.0f, 0.0f, Size * 0.4f), FVector(Size, Size, Size * 0.8f), FRotator::ZeroRotator, FLinearColor(0.95f, 0.42f, 0.04f), 3.0f, true);
					Piece(EBRShape::Cylinder, EBRMat::Surface, Foot + FVector(0.0f, 0.0f, Size * 0.84f), FVector(24.0f, 24.0f, 50.0f), FRotator::ZeroRotator, FLinearColor(0.2f, 0.32f, 0.08f), 0.0f, false);
				}
			}
		}
	}
	// The farmstead: a big red barn, a silo, sheds and a windmill or two.
	const FLinearColor Barn(0.56f, 0.09f, 0.07f);
	House(G(-500.0f, 300.0f), FVector2D(1700.0, 1100.0), 560.0f, 20.0f, Barn, FLinearColor(0.22f, 0.2f, 0.22f), 1 | 2 | 8, true);
	House(G(1600.0f, -900.0f), FVector2D(800.0, 900.0), 320.0f, 80.0f, FLinearColor(0.82f, 0.76f, 0.6f), FLinearColor(0.42f, 0.18f, 0.1f), 1 | 8, true);
	Piece(EBRShape::Cylinder, EBRMat::Fade, G(-1800.0f, 1200.0f, 620.0f), FVector(620.0f, 620.0f, 1240.0f), FRotator::ZeroRotator, FLinearColor(0.72f, 0.74f, 0.74f), 5.0f, true);
	Dome(G(-1800.0f, 1200.0f, 1240.0f), 310.0f, FLinearColor(0.45f, 0.48f, 0.5f), EBRMat::Fade, false);
	for (int32 Mill = 0; Mill < 4; ++Mill)
	{
		const FVector2D At = BROnCircle(FVector2D(0.0, 0.0), R * 0.72f, Mill * 90.0f + 30.0f + Seed * 7.0f);
		const FVector Foot = G(At.X, At.Y);
		Piece(EBRShape::Cone, EBRMat::Fade, Foot + FVector(0.0f, 0.0f, 620.0f), FVector(640.0f, 640.0f, 1240.0f), FRotator::ZeroRotator, FLinearColor(0.86f, 0.82f, 0.72f), 1.0f, true);
		const FVector Hub = Foot + FVector(-280.0f, 0.0f, 1020.0f);
		Piece(EBRShape::Cylinder, EBRMat::Metal, Hub, FVector(90.0f, 90.0f, 140.0f), FRotator(90.0f, 0.0f, 0.0f), BRDarkStone, 0.0f, false);
		for (int32 Blade = 0; Blade < 4; ++Blade)
		{
			const float Angle = Blade * 90.0f + Mill * 17.0f;
			const FVector Tip = FRotator(Angle, 0.0f, 0.0f).RotateVector(FVector(0.0f, 0.0f, 720.0f)) * 1.0f;
			const FVector Dir = FVector(0.0f, FMath::Sin(FMath::DegreesToRadians(Angle)), FMath::Cos(FMath::DegreesToRadians(Angle)));
			(void)Tip;
			Piece(EBRShape::Cube, EBRMat::Fade, Hub + FVector(-72.0f, 0.0f, 0.0f) + Dir * 380.0f, FVector(8.0f, 110.0f, 720.0f), FRotator(0.0f, 0.0f, -Angle), BRWood * 1.3f, 4.0f, false);
		}
	}
	// A tractor by the barn.
	const FVector Tractor = G(400.0f, -1100.0f);
	Box(Tractor + FVector(0.0f, 0.0f, 140.0f), FVector(330.0f, 150.0f, 130.0f), FLinearColor(0.1f, 0.42f, 0.14f));
	Box(Tractor + FVector(-110.0f, 0.0f, 250.0f), FVector(150.0f, 160.0f, 140.0f), FLinearColor(0.06f, 0.1f, 0.14f), 0.0f, 0.0f, false);
	for (const float Y : {-100.0f, 100.0f})
	{
		Piece(EBRShape::Cylinder, EBRMat::Surface, Tractor + FVector(-120.0f, Y, 92.0f), FVector(190.0f, 190.0f, 60.0f), FRotator(0.0f, 0.0f, 90.0f), FLinearColor(0.03f, 0.03f, 0.03f), 0.0f, false);
		Piece(EBRShape::Cylinder, EBRMat::Surface, Tractor + FVector(150.0f, Y, 56.0f), FVector(112.0f, 112.0f, 40.0f), FRotator(0.0f, 0.0f, 90.0f), FLinearColor(0.03f, 0.03f, 0.03f), 0.0f, false);
	}
}
