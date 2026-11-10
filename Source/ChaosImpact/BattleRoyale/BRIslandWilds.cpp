#include "BRIsland.h"
#include "BRIslandLayout.h"

// The wild parts of the island, and the small places scattered between the big ones so no walk is empty.

namespace
{
	const FLinearColor BRWStone(0.62f, 0.59f, 0.54f);
	const FLinearColor BRWDark(0.3f, 0.3f, 0.32f);
	const FLinearColor BRWSteel(0.46f, 0.48f, 0.52f);
	const FLinearColor BRWWood(0.36f, 0.2f, 0.09f);
	const FLinearColor BRWRed(0.72f, 0.07f, 0.09f);
	const FLinearColor BRWBlue(0.06f, 0.22f, 0.62f);
	const FLinearColor BRWGold(1.0f, 0.66f, 0.12f);
	const FLinearColor BRWRock(0.5f, 0.2f, 0.11f);

	FVector2D BRWOnCircle(const FVector2D& Centre, const float Radius, const float Degrees)
	{
		const float A = FMath::DegreesToRadians(Degrees);
		return Centre + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Radius;
	}
}

void ABRIsland::BuildCanyon()
{
	using namespace BRLayout;
	FRandomStream Random(201);
	const FVector2D C = Canyon.At;
	// Tall stacked pillars of red rock along the channels and on the plateau.
	for (int32 Index = 0; Index < 240; ++Index)
	{
		const FVector2D At = BRWOnCircle(C, FMath::Sqrt(Random.FRand()) * (Canyon.Radius + 1800.0f), Random.FRandRange(0.0f, 360.0f));
		const FVector Foot = OnGround(At.X, At.Y, -30.0f);
		if (Foot.Z < 0.0f) { continue; }
		const int32 Stacks = Random.RandRange(2, 5);
		float Up = 0.0f;
		for (int32 Stack = 0; Stack < Stacks; ++Stack)
		{
			const float Wide = Random.FRandRange(420.0f, 880.0f) * (1.0f - Stack * 0.12f);
			const float High = Random.FRandRange(260.0f, 520.0f);
			Piece(EBRShape::Cube, EBRMat::Fade, Foot + FVector(Random.FRandRange(-60.0f, 60.0f), Random.FRandRange(-60.0f, 60.0f), Up + High * 0.5f), FVector(Wide, Wide * Random.FRandRange(0.7f, 1.1f), High),
				FRotator(Random.FRandRange(-4.0f, 4.0f), Random.FRandRange(0.0f, 90.0f), Random.FRandRange(-4.0f, 4.0f)), BRWRock * Random.FRandRange(0.8f, 1.25f), 3.0f, true);
			Up += High * 0.9f;
		}
	}
	// A mining camp in the crossing: shacks, tracks and carts of ore.
	const FVector2D M = C + FVector2D(800.0, 900.0);
	const float Z = CanyonFloor;
	for (int32 Shack = 0; Shack < 7; ++Shack)
	{
		const FVector2D At = BRWOnCircle(M, 1750.0f, Shack * 51.0f + 12.0f);
		House(FVector(At.X, At.Y, BRMap::HeightAt(At.X, At.Y)), FVector2D(620.0, 720.0), 300.0f, Shack * 51.0f, BRWWood * 1.1f, BRWRock * 0.8f, 1 | 4, true);
	}
	for (int32 Tie = -24; Tie <= 24; ++Tie)
	{
		const FVector At(M.X + Tie * 120.0f, M.Y - 240.0f, BRMap::HeightAt(M.X + Tie * 120.0f, M.Y - 240.0f) + 8.0f);
		Box(At, FVector(30.0f, 160.0f, 10.0f), BRWWood, 0.0f, 0.0f, false);
		if (Tie % 8 == 0)
		{
			Box(At + FVector(0.0f, 0.0f, 80.0f), FVector(240.0f, 150.0f, 100.0f), BRWSteel, 0.0f, 5.0f);
			for (int32 Ore = 0; Ore < 4; ++Ore)
			{
				Piece(EBRShape::Sphere, EBRMat::Metal, At + FVector(Random.FRandRange(-60.0f, 60.0f), Random.FRandRange(-40.0f, 40.0f), 148.0f), FVector(70.0f), FRotator::ZeroRotator, BRWGold, 0.0f, false);
			}
		}
	}
	(void)Z;
	// Rope bridges across the channels, up where the plateau is.
	for (const TArray<FVector2D>& Line : CanyonChannels())
	{
		for (int32 Index = 1; Index + 1 < Line.Num(); ++Index)
		{
			const FVector2D Way = (Line[Index + 1] - Line[Index]).GetSafeNormal();
			const FVector2D Across(-Way.Y, Way.X);
			const FVector2D Mid = (Line[Index] + Line[Index + 1]) * 0.5;
			const FVector A(Mid.X - Across.X * 3500.0, Mid.Y - Across.Y * 3500.0, CanyonTop - 10.0f);
			const FVector B(Mid.X + Across.X * 3500.0, Mid.Y + Across.Y * 3500.0, CanyonTop - 10.0f);
			Beam(A, B, 620.0f, 22.0f, BRWWood, EBRMat::Surface, true);
			for (const float Side : {-290.0f, 290.0f})
			{
				const FVector Off(Across.Y * Side, -Across.X * Side, 110.0f);
				Beam(A + Off, B + Off, 12.0f, 12.0f, BRWWood, EBRMat::Surface, false);
			}
		}
	}
	// Cacti and bones of old giant balls on the plateau.
	for (int32 Index = 0; Index < 140; ++Index)
	{
		const FVector2D At = BRWOnCircle(C, Random.FRandRange(1000.0f, Canyon.Radius + 5500.0f), Random.FRandRange(0.0f, 360.0f));
		const FVector Foot = OnGround(At.X, At.Y);
		if (Foot.Z < 40.0f) { continue; }
		const float H = Random.FRandRange(220.0f, 420.0f);
		const FLinearColor Green(0.16f, 0.34f, 0.12f);
		Pole(Foot, Foot + FVector(0.0f, 0.0f, H), 34.0f, Green, EBRMat::Surface, true);
		Pole(Foot + FVector(0.0f, 60.0f, H * 0.5f), Foot + FVector(0.0f, 60.0f, H * 0.75f), 18.0f, Green, EBRMat::Surface, false);
		Beam(Foot + FVector(0.0f, 0.0f, H * 0.5f), Foot + FVector(0.0f, 60.0f, H * 0.5f), 34.0f, 34.0f, Green, EBRMat::Surface, false);
	}
}

void ABRIsland::BuildMountain()
{
	using namespace BRLayout;
	FRandomStream Random(202);
	const FVector2D C = Mountain.At;
	// Pines all up the slopes, thinning near the top.
	for (int32 Index = 0; Index < 900; ++Index)
	{
		const float Rad = FMath::Sqrt(Random.FRandRange(0.03f, 1.0f)) * Mountain.Radius;
		const FVector2D At = BRWOnCircle(C, Rad, Random.FRandRange(0.0f, 360.0f));
		if (!BRMap::IsOpenGround(At.X, At.Y)) { continue; }
		if (Random.FRand() > Rad / Mountain.Radius + 0.15f) { continue; }
		Tree(OnGround(At.X, At.Y, -6.0f), Random.FRandRange(0.9f, 1.7f), 1, Random);
	}
	// The frozen lake on the shoulder, level and slick, its shore picked out in rock.
	const FVector2D Lake = C + FVector2D(-7600.0, 7200.0);
	const float LakeZ = BRMap::HeightAt(Lake.X, Lake.Y) + 44.0f;
	Disc(FVector(Lake.X, Lake.Y, LakeZ), 3300.0f, 90.0f, FLinearColor(0.58f, 0.82f, 0.96f), EBRMat::Metal, true);
	for (int32 Index = 0; Index < 60; ++Index)
	{
		const FVector2D At = BRWOnCircle(Lake, 3300.0f, Index * 6.0f + Random.FRandRange(-2.0f, 2.0f));
		Rock(OnGround(At.X, At.Y), Random.FRandRange(0.8f, 1.8f), FLinearColor(0.55f, 0.58f, 0.62f), Random);
	}
	// Snowmen who have clearly been playing dodgeball.
	for (int32 Index = 0; Index < 16; ++Index)
	{
		const FVector2D At = BRWOnCircle(Lake, Random.FRandRange(600.0f, 2700.0f), Random.FRandRange(0.0f, 360.0f));
		const FVector Foot(At.X, At.Y, LakeZ + 44.0f);
		const FLinearColor Snow(0.92f, 0.95f, 1.0f);
		Dome(Foot + FVector(0.0f, 0.0f, 100.0f), 120.0f, Snow, EBRMat::Surface, true);
		Dome(Foot + FVector(0.0f, 0.0f, 260.0f), 82.0f, Snow, EBRMat::Surface, false);
		Dome(Foot + FVector(0.0f, 0.0f, 372.0f), 58.0f, Snow, EBRMat::Surface, false);
		Piece(EBRShape::Cone, EBRMat::Surface, Foot + FVector(50.0f, 0.0f, 372.0f), FVector(26.0f, 26.0f, 80.0f), FRotator(-90.0f, 0.0f, 0.0f), FLinearColor(0.95f, 0.4f, 0.05f), 0.0f, false);
		Piece(EBRShape::Sphere, EBRMat::Surface, Foot + FVector(40.0f, 130.0f, 200.0f), FVector(84.0f), FRotator::ZeroRotator, BRWRed, 0.0f, false);
	}
	// A ski lift running up to the summit station, with cabins on the way.
	const FVector2D Low = C + FVector2D(-15000.0, 2500.0);
	const FVector2D Top = C + FVector2D(-700.0, 300.0);
	constexpr int32 Towers = 14;
	FVector Previous = FVector::ZeroVector;
	for (int32 Tower = 0; Tower <= Towers; ++Tower)
	{
		const FVector2D At = FMath::Lerp(Low, Top, static_cast<double>(Tower) / Towers);
		const FVector Foot = OnGround(At.X, At.Y);
		const FVector Head = Foot + FVector(0.0f, 0.0f, 900.0f);
		Pole(Foot, Head, 26.0f, BRWSteel);
		Beam(Head + FVector(0.0f, -240.0f, 0.0f), Head + FVector(0.0f, 240.0f, 0.0f), 28.0f, 28.0f, BRWSteel, EBRMat::Metal, false);
		if (Tower > 0)
		{
			for (const float Side : {-230.0f, 230.0f})
			{
				Pole(Previous + FVector(0.0f, Side, 0.0f), Head + FVector(0.0f, Side, 0.0f), 5.0f, FLinearColor(0.05f, 0.05f, 0.05f), EBRMat::Metal, false);
			}
			const FVector Mid = (Previous + Head) * 0.5f + FVector(0.0f, Tower % 2 ? 230.0f : -230.0f, -180.0f);
			Piece(EBRShape::Cube, EBRMat::Fade, Mid, FVector(150.0f, 110.0f, 130.0f), FRotator::ZeroRotator, BRWRed, 0.0f, false);
		}
		Previous = Head;
	}
	// Lodge at the bottom, observatory at the top, cabins dotted between.
	const FVector Lodge = OnGround(Low.X - 900.0, Low.Y);
	House(Lodge, FVector2D(1500.0, 1100.0), 420.0f, 0.0f, BRWWood * 1.1f, FLinearColor(0.72f, 0.78f, 0.82f), 1 | 2 | 8, true);
	const FVector Peak = OnGround(Top.X, Top.Y);
	Disc(Peak + FVector(0.0f, 0.0f, 90.0f), 1050.0f, 260.0f, BRWDark);
	House(Peak + FVector(0.0f, 0.0f, 220.0f), FVector2D(1100.0, 1100.0), 380.0f, 0.0f, FLinearColor(0.86f, 0.88f, 0.92f), BRWDark, 2 | 4 | 8, false);
	Dome(Peak + FVector(0.0f, 0.0f, 600.0f), 500.0f, FLinearColor(0.82f, 0.84f, 0.9f));
	for (int32 Index = 0; Index < 12; ++Index)
	{
		const FVector2D At = BRWOnCircle(C, Random.FRandRange(6000.0f, 18000.0f), Random.FRandRange(0.0f, 360.0f));
		if (!BRMap::IsOpenGround(At.X, At.Y)) { continue; }
		House(OnGround(At.X, At.Y, -20.0f), FVector2D(620.0, 620.0), 280.0f, Random.FRandRange(0.0f, 360.0f), BRWWood, FLinearColor(0.74f, 0.8f, 0.84f), 3, true);
	}
}

void ABRIsland::BuildForest()
{
	using namespace BRLayout;
	FRandomStream Random(203);
	const FVector2D C = Forest.At;
	// A deep wood, with glades left open.
	for (int32 Index = 0; Index < 1700; ++Index)
	{
		const FVector2D At = BRWOnCircle(C, FMath::Sqrt(Random.FRand()) * Forest.Radius, Random.FRandRange(0.0f, 360.0f));
		if (!BRMap::IsOpenGround(At.X, At.Y) || FVector2D::Distance(At, C) < 800.0f) { continue; }
		const FVector Foot = OnGround(At.X, At.Y, -6.0f);
		Tree(Foot, Random.FRandRange(1.0f, 1.9f), Random.FRand() < 0.2f ? 1 : 0, Random);
		if (Random.FRand() < 0.2f)
		{
			// Mushrooms round the roots.
			const FVector Cap = Foot + FVector(Random.FRandRange(120.0f, 260.0f), Random.FRandRange(-180.0f, 180.0f), 0.0f);
			Piece(EBRShape::Cylinder, EBRMat::Surface, Cap + FVector(0.0f, 0.0f, 45.0f), FVector(30.0f, 30.0f, 90.0f), FRotator::ZeroRotator, FLinearColor(0.88f, 0.84f, 0.72f), 0.0f, false, false);
			Piece(EBRShape::Sphere, EBRMat::Surface, Cap + FVector(0.0f, 0.0f, 92.0f), FVector(120.0f, 120.0f, 66.0f), FRotator::ZeroRotator, BRWRed, 0.0f, false, false);
		}
	}
	// The great tree of the glade with a treehouse.
	const FVector Great = OnGround(C.X, C.Y);
	Piece(EBRShape::Cylinder, EBRMat::Fade, Great + FVector(0.0f, 0.0f, 620.0f), FVector(560.0f, 560.0f, 1240.0f), FRotator::ZeroRotator, FLinearColor(0.24f, 0.14f, 0.08f), 3.0f, true);
	for (int32 Blob = 0; Blob < 12; ++Blob)
	{
		const FVector2D Off = BRWOnCircle(FVector2D(0.0, 0.0), Random.FRandRange(200.0f, 1100.0f), Blob * 30.0f);
		const float R = Random.FRandRange(900.0f, 1300.0f);
		Piece(EBRShape::Sphere, EBRMat::Leaves, Great + FVector(Off.X, Off.Y, 1500.0f + Random.FRandRange(-120.0f, 260.0f)), FVector(R, R, R * 0.7f), FRotator::ZeroRotator, FLinearColor(0.16f, 0.36f, 0.09f) * Random.FRandRange(0.85f, 1.2f), 0.0f, false);
	}
	House(Great + FVector(0.0f, 500.0f, 620.0f), FVector2D(700.0, 700.0), 260.0f, 0.0f, BRWWood, BRWWood * 0.6f, 15, true);
	// A long ramp to it, and an old temple ring.
	Piece(EBRShape::Cube, EBRMat::Surface, Great + FVector(1100.0f, 500.0f, 310.0f), FVector(1540.0f, 280.0f, 24.0f), FRotator(-27.0f, 0.0f, 0.0f) + FRotator(0.0f, 180.0f, 0.0f), BRWWood, 4.0f, true);
	const FVector2D Temple = C + FVector2D(-5400.0, 3200.0);
	const float TZ = BRMap::HeightAt(Temple.X, Temple.Y);
	Disc(FVector(Temple.X, Temple.Y, TZ + 30.0f), 1500.0f, 120.0f, BRWStone * 0.75f);
	for (int32 Column = 0; Column < 16; ++Column)
	{
		const FVector2D At = BRWOnCircle(Temple, 1320.0f, Column * 22.5f);
		const float H = Column % 3 == 0 ? 120.0f : Random.FRandRange(300.0f, 640.0f);
		Disc(FVector(At.X, At.Y, TZ + 90.0f + H * 0.5f), 96.0f, H, BRWStone, EBRMat::Fade);
	}
	Dodgeball(FVector(Temple.X, Temple.Y, TZ + 360.0f), 240.0f, FLinearColor(0.45f, 0.42f, 0.38f), BRWGold * 0.8f, 12.0f, true);
}

void ABRIsland::BuildCrater()
{
	using namespace BRLayout;
	FRandomStream Random(204);
	const FVector2D C = Crater.At;
	const float R = CraterRadius;
	const FVector Bottom = OnGround(C.X, C.Y);
	// The president's fumbled giant ball, sunk where it fell, split and glowing inside.
	const FLinearColor Shell(0.62f, 0.1f, 0.1f);
	Dodgeball(Bottom + FVector(0.0f, 0.0f, 620.0f), 1250.0f, Shell, FLinearColor(0.9f, 0.86f, 0.74f), 26.0f, true);
	for (int32 Crack = 0; Crack < 14; ++Crack)
	{
		const FVector Dir = FRotator(Random.FRandRange(-5.0f, 70.0f), Crack * 25.7f, 0.0f).Vector();
		Piece(EBRShape::Cube, EBRMat::Glow, Bottom + FVector(0.0f, 0.0f, 620.0f) + Dir * 1248.0f, FVector(10.0f, Random.FRandRange(40.0f, 90.0f), Random.FRandRange(220.0f, 620.0f)), Dir.Rotation(), FLinearColor(1.0f, 0.34f, 0.06f) * 5.0f, 0.0f, false, false);
	}
	// Shards thrown out all round it, leaning.
	for (int32 Index = 0; Index < 90; ++Index)
	{
		const FVector2D At = BRWOnCircle(C, Random.FRandRange(1500.0f, R * 1.3f), Random.FRandRange(0.0f, 360.0f));
		const FVector Foot = OnGround(At.X, At.Y, -30.0f);
		const FVector Size(Random.FRandRange(260.0f, 720.0f), Random.FRandRange(60.0f, 180.0f), Random.FRandRange(240.0f, 860.0f));
		Piece(EBRShape::Cube, EBRMat::Fade, Foot + FVector(0.0f, 0.0f, Size.Z * 0.4f), Size, FRotator(Random.FRandRange(-22.0f, 22.0f), Random.FRandRange(0.0f, 360.0f), Random.FRandRange(-22.0f, 22.0f)), Index % 3 ? Shell * Random.FRandRange(0.5f, 1.0f) : FLinearColor(0.2f, 0.18f, 0.18f), 3.0f, true);
	}
	// A science team's camp on the rim.
	for (int32 Tent = 0; Tent < 7; ++Tent)
	{
		const FVector2D At = BRWOnCircle(C, R * 1.42f, Tent * 16.0f - 40.0f);
		const FVector Foot = OnGround(At.X, At.Y);
		Piece(EBRShape::Cone, EBRMat::Fade, Foot + FVector(0.0f, 0.0f, 160.0f), FVector(560.0f, 560.0f, 340.0f), FRotator::ZeroRotator, FLinearColor(0.82f, 0.76f, 0.5f), 4.0f, true);
		Crate(Foot + FVector(380.0f, 90.0f, 0.0f), 150.0f, 20.0f, BRWWood);
	}
}

void ABRIsland::BuildFunfair()
{
	using namespace BRLayout;
	FRandomStream Random(205);
	const FVector2D C = Funfair.At;
	const float Z = Funfair.Height;
	const auto G = [&](const float X, const float Y, const float Up = 0.0f) { return FVector(C.X + X, C.Y + Y, Z + Up); };
	const FLinearColor Candy[] = {BRWRed, FLinearColor(0.95f, 0.75f, 0.12f), FLinearColor(0.1f, 0.5f, 0.85f), FLinearColor(0.85f, 0.25f, 0.65f), FLinearColor(0.2f, 0.72f, 0.42f)};

	// The ferris wheel, tall enough to see from far off.
	const FVector Hub = G(1500.0f, 1100.0f, 1500.0f);
	const float R = 1280.0f;
	for (const float Y : {-180.0f, 180.0f})
	{
		for (const float X : {-500.0f, 500.0f})
		{
			Pole(G(1500.0f + X, 1100.0f + Y), Hub + FVector(0.0f, Y, 0.0f), 44.0f, FLinearColor(0.82f, 0.82f, 0.84f));
		}
	}
	constexpr int32 Spokes = 16;
	for (int32 Spoke = 0; Spoke < Spokes; ++Spoke)
	{
		const float A = Spoke * UE_TWO_PI / Spokes, B = (Spoke + 1) * UE_TWO_PI / Spokes;
		const FVector Rim(FMath::Cos(A) * R, 0.0f, FMath::Sin(A) * R), Next(FMath::Cos(B) * R, 0.0f, FMath::Sin(B) * R);
		for (const float Y : {-130.0f, 130.0f})
		{
			Pole(Hub + FVector(0.0f, Y, 0.0f), Hub + Rim + FVector(0.0f, Y, 0.0f), 9.0f, FLinearColor(0.85f, 0.85f, 0.88f), EBRMat::Metal, false);
			Pole(Hub + Rim + FVector(0.0f, Y, 0.0f), Hub + Next + FVector(0.0f, Y, 0.0f), 16.0f, Candy[Spoke % 5], EBRMat::Fade, false);
		}
		Piece(EBRShape::Cube, EBRMat::Fade, Hub + Rim - FVector(0.0f, 0.0f, 150.0f), FVector(240.0f, 220.0f, 200.0f), FRotator::ZeroRotator, Candy[Spoke % 5], 0.0f, false);
		Glow(Hub + Rim, FVector(44.0f), FLinearColor(1.0f, 0.86f, 0.5f) * 5.0f, 0.0f, EBRShape::Sphere);
	}
	// The roller coaster: a loop of track on posts, its dips and rises around the fair.
	constexpr int32 Track = 60;
	FVector Last = FVector::ZeroVector;
	for (int32 Index = 0; Index <= Track; ++Index)
	{
		const float T = Index * UE_TWO_PI / Track;
		const FVector At = G(-1300.0f + FMath::Cos(T) * 2400.0f, -500.0f + FMath::Sin(T) * 1900.0f, 560.0f + FMath::Sin(T * 3.0f) * 340.0f + FMath::Cos(T * 2.0f) * 120.0f);
		if (Index > 0)
		{
			Beam(Last + FVector(0.0f, 60.0f, 0.0f), At + FVector(0.0f, 60.0f, 0.0f), 12.0f, 14.0f, BRWRed, EBRMat::Fade, false);
			Beam(Last - FVector(0.0f, 60.0f, 0.0f), At - FVector(0.0f, 60.0f, 0.0f), 12.0f, 14.0f, BRWRed, EBRMat::Fade, false);
		}
		if (Index % 3 == 0) { Pole(FVector(At.X, At.Y, Z), At, 16.0f, FLinearColor(0.84f, 0.8f, 0.5f)); }
		Last = At;
	}
	// The carousel.
	const FVector Carousel = G(-1700.0f, 1900.0f);
	Disc(Carousel + FVector(0.0f, 0.0f, 30.0f), 880.0f, 60.0f, BRWRed);
	Piece(EBRShape::Cone, EBRMat::Fade, Carousel + FVector(0.0f, 0.0f, 560.0f), FVector(2000.0f, 2000.0f, 340.0f), FRotator::ZeroRotator, FLinearColor(0.95f, 0.75f, 0.12f), 4.0f, false);
	for (int32 Seat = 0; Seat < 12; ++Seat)
	{
		const FVector2D At = BRWOnCircle(FVector2D(Carousel), 700.0f, Seat * 30.0f);
		Pole(FVector(At.X, At.Y, Z + 60.0f), FVector(At.X, At.Y, Z + 460.0f), 8.0f, BRWGold, EBRMat::Metal, false);
		Piece(EBRShape::Sphere, EBRMat::Surface, FVector(At.X, At.Y, Z + 170.0f), FVector(150.0f, 80.0f, 110.0f), FRotator(0.0f, Seat * 30.0f + 90.0f, 0.0f), Candy[Seat % 5], 0.0f, true);
	}
	Disc(Carousel + FVector(0.0f, 0.0f, 260.0f), 190.0f, 400.0f, FLinearColor(0.9f, 0.86f, 0.78f));
	// Striped tents and stalls along the midway, with ball-throwing games.
	for (int32 Stall = 0; Stall < 22; ++Stall)
	{
		const FVector2D At = BRWOnCircle(FVector2D(0.0, 0.0), Random.FRandRange(2500.0f, 4300.0f), Stall * 16.4f);
		const FVector Foot = G(At.X, At.Y);
		const FLinearColor Tint = Candy[Stall % 5];
		if (Stall % 2)
		{
			// (High enough to walk under.)
			Piece(EBRShape::Cone, EBRMat::Fade, Foot + FVector(0.0f, 0.0f, 390.0f), FVector(720.0f, 720.0f, 260.0f), FRotator::ZeroRotator, Tint, 4.0f, true);
			Disc(Foot + FVector(0.0f, 0.0f, 140.0f), 40.0f, 280.0f, BRWWood, EBRMat::Surface);
		}
		else
		{
			House(Foot, FVector2D(560.0, 560.0), 300.0f, Random.FRandRange(0.0f, 360.0f), FLinearColor(0.9f, 0.86f, 0.76f), Tint, 1 | 2, false);
			Dodgeball(Foot + FVector(0.0f, 420.0f, 62.0f), 60.0f, Tint, FLinearColor::White, 10.0f, false);
		}
	}
	// Bulb-lit arches over the walkways.
	for (int32 Arch = 0; Arch < 10; ++Arch)
	{
		const FVector2D At = BRWOnCircle(FVector2D(0.0, 0.0), 4300.0f, Arch * 36.0f);
		const FVector Foot = G(At.X, At.Y);
		const FRotator Turn(0.0f, Arch * 36.0f, 0.0f);
		const FVector Side = Turn.RotateVector(FVector(0.0f, 420.0f, 0.0f));
		Pole(Foot + Side, Foot + Side + FVector(0.0f, 0.0f, 500.0f), 16.0f, Candy[Arch % 5]);
		Pole(Foot - Side, Foot - Side + FVector(0.0f, 0.0f, 500.0f), 16.0f, Candy[Arch % 5]);
		Beam(Foot + Side + FVector(0.0f, 0.0f, 500.0f), Foot - Side + FVector(0.0f, 0.0f, 500.0f), 26.0f, 26.0f, Candy[Arch % 5], EBRMat::Fade, false);
		for (int32 Bulb = -3; Bulb <= 3; ++Bulb)
		{
			Glow(Foot + Side * (Bulb / 3.0f) + FVector(0.0f, 0.0f, 480.0f), FVector(28.0f), FLinearColor(1.0f, 0.82f, 0.4f) * 5.0f, 0.0f, EBRShape::Sphere);
		}
	}
}

void ABRIsland::BuildRiver()
{
	using namespace BRLayout;
	FRandomStream Random(206);
	// Wooden bridges where the roads cross (the river itself can be waded anywhere), reeds and stepping stones.
	const TArray<FVector2D>& Points = RiverPoints();
	for (int32 Index = 0; Index + 1 < Points.Num() - 1; ++Index)
	{
		const FVector2D A = Points[Index], B = Points[Index + 1];
		const FVector2D Way = (B - A).GetSafeNormal();
		const FVector2D Across(-Way.Y, Way.X);
		const FVector2D Mid = (A + B) * 0.5;
		const float Bank = RiverHalfWidth + 1500.0f;
		const FVector L = OnGround(Mid.X - Across.X * Bank, Mid.Y - Across.Y * Bank), R = OnGround(Mid.X + Across.X * Bank, Mid.Y + Across.Y * Bank);
		const FVector Crown((L.X + R.X) * 0.5f, (L.Y + R.Y) * 0.5f, FMath::Max(L.Z, R.Z) + 90.0f);
		Beam(L, Crown, 900.0f, 26.0f, BRWWood, EBRMat::Surface, true);
		Beam(Crown, R, 900.0f, 26.0f, BRWWood, EBRMat::Surface, true);
		for (const float Side : {-430.0f, 430.0f})
		{
			const FVector Off(Way.X * Side, Way.Y * Side, 110.0f);
			Beam(L + Off, Crown + Off, 14.0f, 14.0f, BRWWood * 0.7f, EBRMat::Surface, false);
			Beam(Crown + Off, R + Off, 14.0f, 14.0f, BRWWood * 0.7f, EBRMat::Surface, false);
		}
		Pole(FVector(Crown.X, Crown.Y, RiverBed), Crown, 60.0f, BRWWood * 0.5f, EBRMat::Surface, false);
	}
	for (int32 Index = 0; Index + 1 < Points.Num(); ++Index)
	{
		for (int32 Reed = 0; Reed < 46; ++Reed)
		{
			const FVector2D At = FMath::Lerp(Points[Index], Points[Index + 1], Random.FRand()) + FVector2D(Random.FRandRange(-1900.0f, 1900.0f), Random.FRandRange(-1900.0f, 1900.0f));
			const FVector Foot = OnGround(At.X, At.Y);
			if (Foot.Z < -40.0f || Foot.Z > 120.0f) { continue; }
			Piece(EBRShape::Cone, EBRMat::Leaves, Foot + FVector(0.0f, 0.0f, 70.0f), FVector(70.0f, 70.0f, 200.0f), FRotator::ZeroRotator, FLinearColor(0.22f, 0.42f, 0.13f) * Random.FRandRange(0.85f, 1.2f), 0.0f, false, false);
		}
	}
}

void ABRIsland::BuildScatter()
{
	using namespace BRLayout;
	FRandomStream Random(207);
	// ---- Small places in the open, spaced apart: a settled island rather than empty hills.
	const TArray<FVector2D>& Places = BRMap::Places();
	const FLinearColor Walls[] = {{0.8f, 0.72f, 0.58f}, {0.66f, 0.46f, 0.34f}, {0.55f, 0.62f, 0.66f}, {0.72f, 0.38f, 0.3f}};
	const FLinearColor Roofs[] = {{0.46f, 0.13f, 0.08f}, {0.12f, 0.26f, 0.4f}, {0.28f, 0.2f, 0.14f}};
	for (int32 Index = 0; Index < Places.Num(); ++Index)
	{
		const FVector2D P = Places[Index];
		const float Z = BRMap::HeightAt(P.X, P.Y);
		const auto G = [&](const float X, const float Y) { return OnGround(P.X + X, P.Y + Y); };
		switch (Index % 8)
		{
		case 0: // A hamlet round a well.
		{
			Disc(FVector(P.X, P.Y, Z + 60.0f), 120.0f, 120.0f, BRWStone);
			for (int32 Home = 0; Home < 5; ++Home)
			{
				const FVector2D At = BRWOnCircle(FVector2D(0.0, 0.0), Random.FRandRange(900.0f, 1500.0f), Home * 72.0f + Random.FRandRange(-15.0f, 15.0f));
				House(G(At.X, At.Y), FVector2D(Random.FRandRange(620.0f, 800.0f), Random.FRandRange(620.0f, 780.0f)), 300.0f, Random.FRandRange(0.0f, 360.0f), Walls[Random.RandHelper(4)], Roofs[Random.RandHelper(3)], Random.RandRange(3, 15), true);
			}
			break;
		}
		case 1: // A filling station for skateboards and cars.
		{
			const FVector At = G(0.0f, 0.0f);
			Box(At + FVector(0.0f, 0.0f, 4.0f), FVector(2300.0f, 1500.0f, 8.0f), FLinearColor(0.14f, 0.14f, 0.15f), 0.0f, 0.0f, false);
			for (const FVector2D& Leg : {FVector2D(-420, -300), FVector2D(420, -300), FVector2D(-420, 300), FVector2D(420, 300)}) { Pole(At + FVector(Leg.X, Leg.Y, 0.0), At + FVector(Leg.X, Leg.Y, 420.0), 22.0f, BRWRed); }
			Piece(EBRShape::Cube, EBRMat::Fade, At + FVector(0.0f, 0.0f, 440.0f), FVector(1240.0f, 980.0f, 50.0f), FRotator::ZeroRotator, BRWRed, 0.0f, false);
			Glow(At + FVector(0.0f, 0.0f, 410.0f), FVector(1100.0f, 16.0f, 16.0f), FLinearColor(1.0f, 0.9f, 0.7f) * 4.0f);
			for (const float X : {-240.0f, 240.0f}) { Box(At + FVector(X, 0.0f, 90.0f), FVector(120.0f, 90.0f, 180.0f), FLinearColor(0.85f, 0.85f, 0.82f)); }
			House(At + FVector(-1500.0f, 0.0f, 0.0f), FVector2D(900.0, 900.0), 310.0f, 0.0f, FLinearColor(0.88f, 0.86f, 0.8f), BRWRed, 1 | 4, false);
			Car(At + FVector(700.0f, 520.0f, 0.0f), 90.0f, BRWBlue);
			break;
		}
		case 2: // A watchtower over sandbags.
		{
			const FVector At = G(0.0f, 0.0f);
			for (const FVector2D& Leg : {FVector2D(-300, -300), FVector2D(300, -300), FVector2D(-300, 300), FVector2D(300, 300)}) { Pole(At + FVector(Leg.X, Leg.Y, -40.0), At + FVector(Leg.X, Leg.Y, 760.0), 26.0f, BRWWood, EBRMat::Surface); }
			Box(At + FVector(0.0f, 0.0f, 770.0f), FVector(860.0f, 860.0f, 24.0f), BRWWood, 0.0f, 4.0f);
			Piece(EBRShape::Cone, EBRMat::Fade, At + FVector(0.0f, 0.0f, 1080.0f), FVector(1040.0f, 1040.0f, 300.0f), FRotator(0.0f, 45.0f, 0.0f), FLinearColor(0.3f, 0.34f, 0.16f), 0.0f, false);
			Piece(EBRShape::Cube, EBRMat::Surface, At + FVector(760.0f, 0.0f, 380.0f), FVector(1760.0f, 300.0f, 20.0f), FRotator(27.0f, 180.0f, 0.0f), BRWWood, 4.0f, true);
			for (int32 Bag = 0; Bag < 12; ++Bag)
			{
				const FVector2D On = BRWOnCircle(FVector2D(0.0, 0.0), 1050.0f, Bag * 30.0f);
				if (Bag % 4 == 0) { continue; }
				Box(G(On.X, On.Y) + FVector(0.0f, 0.0f, 50.0f), FVector(420.0f, 110.0f, 110.0f), FLinearColor(0.5f, 0.42f, 0.26f), Bag * 30.0f + 90.0f, 3.0f);
			}
			break;
		}
		case 3: // Ruins of an older court.
		{
			for (int32 Bit = 0; Bit < 14; ++Bit)
			{
				const FVector2D At = BRWOnCircle(FVector2D(0.0, 0.0), Random.FRandRange(500.0f, 1500.0f), Bit * 26.0f);
				const float High = Random.FRandRange(120.0f, 560.0f);
				Piece(EBRShape::Cube, EBRMat::Fade, G(At.X, At.Y) + FVector(0.0f, 0.0f, High * 0.45f), FVector(Random.FRandRange(180.0f, 760.0f), 110.0f, High), FRotator(Random.FRandRange(-5.0f, 5.0f), Bit * 26.0f + 90.0f, 0.0f), BRWStone * Random.FRandRange(0.7f, 1.0f), 1.0f, true);
			}
			break;
		}
		case 4: // A ball depot: giant stacked balls behind a fence.
		{
			for (int32 Ball = 0; Ball < 7; ++Ball)
			{
				const FVector2D At = BRWOnCircle(FVector2D(0.0, 0.0), Ball == 0 ? 0.0f : 620.0f, Ball * 60.0f);
				const float R = Ball == 0 ? 340.0f : Random.FRandRange(220.0f, 320.0f);
				Dodgeball(G(At.X, At.Y) + FVector(0.0f, 0.0f, Ball == 0 ? 500.0f : R * 0.92f), R, Ball % 2 ? BRWRed : BRWBlue, FLinearColor(0.95f, 0.92f, 0.82f), Ball * 40.0f, true);
			}
			for (int32 Side = 0; Side < 8; ++Side)
			{
				if (Side % 3 == 0) { continue; }
				const FVector2D A = BRWOnCircle(FVector2D(0.0, 0.0), 1500.0f, Side * 45.0f), B = BRWOnCircle(FVector2D(0.0, 0.0), 1500.0f, (Side + 1) * 45.0f);
				Fence(G(A.X, A.Y), G(B.X, B.Y), BRWSteel, 170.0f);
			}
			break;
		}
		case 5: // A campsite with a bonfire.
		{
			Disc(FVector(P.X, P.Y, Z + 12.0f), 300.0f, 24.0f, FLinearColor(0.08f, 0.06f, 0.05f), EBRMat::Surface, false);
			Glow(FVector(P.X, P.Y, Z + 90.0f), FVector(140.0f, 140.0f, 180.0f), FLinearColor(1.0f, 0.42f, 0.08f) * 5.0f, 0.0f, EBRShape::Cone);
			for (int32 Tent = 0; Tent < 6; ++Tent)
			{
				const FVector2D At = BRWOnCircle(FVector2D(0.0, 0.0), 1000.0f, Tent * 60.0f);
				Piece(EBRShape::Cone, EBRMat::Fade, G(At.X, At.Y) + FVector(0.0f, 0.0f, 120.0f), FVector(460.0f, 460.0f, 260.0f), FRotator(0.0f, Tent * 19.0f, 0.0f), Tent % 2 ? BRWBlue : FLinearColor(0.82f, 0.52f, 0.12f), 4.0f, true);
			}
			Truck(G(1200.0f, 600.0f), 40.0f, FLinearColor(0.3f, 0.34f, 0.16f));
			break;
		}
		case 6: // A wind turbine and its hut.
		{
			const FVector At = G(0.0f, 0.0f);
			Piece(EBRShape::Cylinder, EBRMat::Fade, At + FVector(0.0f, 0.0f, 1500.0f), FVector(190.0f, 190.0f, 3000.0f), FRotator::ZeroRotator, FLinearColor(0.9f, 0.9f, 0.9f), 0.0f, true);
			for (int32 Blade = 0; Blade < 3; ++Blade)
			{
				const float Angle = Blade * 120.0f + Index * 20.0f;
				const FVector Dir(0.0f, FMath::Sin(FMath::DegreesToRadians(Angle)), FMath::Cos(FMath::DegreesToRadians(Angle)));
				Piece(EBRShape::Cube, EBRMat::Fade, At + FVector(-110.0f, 0.0f, 3000.0f) + Dir * 760.0f, FVector(20.0f, 120.0f, 1500.0f), FRotator(0.0f, 0.0f, -Angle), FLinearColor(0.92f, 0.92f, 0.92f), 0.0f, false);
			}
			House(At + FVector(420.0f, 620.0f, 0.0f), FVector2D(620.0, 620.0), 280.0f, 0.0f, BRWStone, BRWDark, 1 | 8, false);
			break;
		}
		default: // A wrecked bus and a dropped cargo.
		{
			const FVector At = G(0.0f, 0.0f);
			const FLinearColor Paint(0.82f, 0.56f, 0.08f);
			Piece(EBRShape::Cube, EBRMat::Fade, At + FVector(0.0f, 0.0f, 170.0f), FVector(1000.0f, 260.0f, 300.0f), FRotator(4.0f, 25.0f, 12.0f), Paint, 5.0f, true);
			for (int32 Box2 = 0; Box2 < 8; ++Box2)
			{
				Crate(G(Random.FRandRange(-1300.0f, 1300.0f), Random.FRandRange(-1300.0f, 1300.0f)), Random.FRandRange(140.0f, 240.0f), Random.FRandRange(0.0f, 90.0f), BRWWood * Random.FRandRange(0.9f, 1.3f));
			}
			break;
		}
		}
	}

	// ---- Trees, rocks and bushes over everything else.
	for (int32 Index = 0; Index < 9500; ++Index)
	{
		const FVector2D At(Random.FRandRange(-82000.0f, 82000.0f), Random.FRandRange(-82000.0f, 82000.0f));
		if (!BRMap::IsOpenGround(At.X, At.Y)) { continue; }
		// In clumps: broad patches of noise-free chance by a coarse grid.
		const int32 GX = FMath::FloorToInt(At.X / 5200.0), GY = FMath::FloorToInt(At.Y / 5200.0);
		const float Patch = FMath::Frac(FMath::Sin(GX * 12.9898f + GY * 78.233f) * 43758.5453f);
		const bool bWoods = Patch > 0.5f;
		if (Random.FRand() > (bWoods ? 0.8f : 0.12f)) { continue; }
		if (!IsClear(At, 170.0f)) { continue; }
		const FVector Foot = OnGround(At.X, At.Y, -6.0f);
		const BRMap::FPaint Paint = BRMap::PaintAt(At.X, At.Y, Foot.Z);
		const float Roll = Random.FRand();
		if (Paint.Dry > 0.55f)
		{
			if (Roll < 0.6f) { Rock(Foot, Random.FRandRange(0.6f, 1.9f), FLinearColor(0.42f, 0.24f, 0.14f), Random); }
			continue;
		}
		if (Paint.Sand > 0.6f)
		{
			if (Roll < 0.3f) { Tree(Foot, Random.FRandRange(0.9f, 1.3f), 2, Random); }
			continue;
		}
		if (Roll < 0.74f) { Tree(Foot, Random.FRandRange(0.8f, 1.6f), Paint.Snow > 0.3f || Random.FRand() < 0.12f ? 1 : 0, Random); }
		else if (Roll < 0.9f) { Rock(Foot, Random.FRandRange(0.5f, 1.7f), FLinearColor(0.42f, 0.41f, 0.39f), Random); }
		else
		{
			const float Size = Random.FRandRange(150.0f, 300.0f);
			Piece(EBRShape::Sphere, EBRMat::Leaves, Foot + FVector(0.0f, 0.0f, Size * 0.28f), FVector(Size, Size, Size * 0.66f), FRotator::ZeroRotator, FLinearColor(0.14f, 0.32f, 0.08f) * Random.FRandRange(0.8f, 1.2f), 0.0f, false);
		}
	}

	// ---- Along the roads: lamp posts and verge stones, a pylon line.
	for (const TArray<FVector2D>& Line : BRMap::Roads())
	{
		for (int32 Index = 0; Index + 1 < Line.Num(); Index += 3)
		{
			const FVector2D Way = (Line[Index + 1] - Line[Index]).GetSafeNormal();
			const FVector2D Side(-Way.Y, Way.X);
			const FVector2D At = Line[Index] + Side * (RoadWidth + 160.0f) * (Index % 2 ? 1.0 : -1.0);
			if (BRMap::ShoreDistance(At.X, At.Y) < 2000.0f) { continue; }
			Lamp(OnGround(At.X, At.Y), 500.0f);
		}
	}
}
