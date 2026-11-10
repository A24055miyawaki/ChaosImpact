#pragma once

#include "CoreMinimal.h"

/**
 * Where everything on the island goes (+X is the top of the screen, +Y its right; cm). About 1.6 km across. The terrain
 * and the buildings both read these, so a district that moves takes its flat ground with it.
 */
enum class EBRDistrictGround : uint8 { Grass, Paved, Concrete, Dirt };

struct FBRDistrict
{
	FVector2D At;
	float Radius;
	float Height;
	float Blend;
	EBRDistrictGround Ground;
};

namespace BRLayout
{
	inline constexpr float IslandRadius = 80000.0f;
	inline constexpr float RoadWidth = 620.0f;
	inline constexpr float RiverHalfWidth = 1150.0f;
	inline constexpr float RiverBed = -95.0f;
	inline constexpr float CanyonTop = 1500.0f;
	inline constexpr float CanyonFloor = 240.0f;
	inline constexpr float CanyonHalfWidth = 2300.0f;
	inline constexpr float MountainPeak = 5400.0f;
	inline constexpr float CraterRadius = 5200.0f;

	inline const FBRDistrict Capital{FVector2D(0.0, 0.0), 9400.0f, 520.0f, 4200.0f, EBRDistrictGround::Paved};
	inline const FBRDistrict Stadium{FVector2D(37500.0, 38900.0), 7600.0f, 240.0f, 3000.0f, EBRDistrictGround::Paved};
	inline const FBRDistrict Factory{FVector2D(-4000.0, 50500.0), 9000.0f, 200.0f, 3000.0f, EBRDistrictGround::Concrete};
	inline const FBRDistrict Port{FVector2D(-44000.0, 39000.0), 9600.0f, 110.0f, 2600.0f, EBRDistrictGround::Concrete};
	inline const FBRDistrict Town{FVector2D(-53000.0, -5400.0), 10500.0f, 190.0f, 3500.0f, EBRDistrictGround::Grass};
	inline const FBRDistrict Ridgetown{FVector2D(-26000.0, -18000.0), 7000.0f, 300.0f, 3000.0f, EBRDistrictGround::Grass};
	inline const FBRDistrict Farm{FVector2D(-39500.0, -40200.0), 10000.0f, 200.0f, 3600.0f, EBRDistrictGround::Dirt};
	inline const FBRDistrict Westfarm{FVector2D(-60000.0, 24000.0), 6000.0f, 170.0f, 2800.0f, EBRDistrictGround::Dirt};
	inline const FBRDistrict Funfair{FVector2D(-7700.0, -28000.0), 4600.0f, 320.0f, 2400.0f, EBRDistrictGround::Paved};
	inline const FBRDistrict Airfield{FVector2D(15500.0, 24500.0), 8000.0f, 260.0f, 3000.0f, EBRDistrictGround::Concrete};
	inline const FBRDistrict Lab{FVector2D(8200.0, -30500.0), 4300.0f, 460.0f, 2400.0f, EBRDistrictGround::Paved};
	// (Not levelled: their own shapes are in the terrain.)
	inline const FBRDistrict Canyon{FVector2D(-2700.0, -56500.0), 13000.0f, 0.0f, 0.0f, EBRDistrictGround::Grass};
	inline const FBRDistrict Mountain{FVector2D(41000.0, -39500.0), 24000.0f, 0.0f, 0.0f, EBRDistrictGround::Grass};
	inline const FBRDistrict Forest{FVector2D(57000.0, -1000.0), 12500.0f, 0.0f, 0.0f, EBRDistrictGround::Grass};
	inline const FBRDistrict Crater{FVector2D(-27800.0, 12400.0), 0.0f, 0.0f, 0.0f, EBRDistrictGround::Grass};

	inline const TArray<const FBRDistrict*>& Flat()
	{
		static const TArray<const FBRDistrict*> List{&Capital, &Stadium, &Factory, &Port, &Town, &Ridgetown, &Farm, &Westfarm, &Funfair, &Airfield, &Lab};
		return List;
	}

	inline const TArray<FVector2D>& RiverPoints()
	{
		// From the foot of the snow mountain, past the capital, out to sea between the forest and the stadium.
		static const TArray<FVector2D> Points{{27500.0, -18700.0}, {28800.0, -8700.0}, {27800.0, 3000.0}, {30000.0, 13000.0},
			{43500.0, 16700.0}, {58600.0, 17000.0}, {72000.0, 20800.0}, {96000.0, 28000.0}};
		return Points;
	}

	inline const TArray<TArray<FVector2D>>& CanyonChannels()
	{
		static const TArray<TArray<FVector2D>> Lines{
			{{-18000.0, -37500.0}, {-10400.0, -48200.0}, {-2700.0, -56500.0}, {2300.0, -68000.0}, {4700.0, -90000.0}},
			{{15400.0, -46000.0}, {7000.0, -52600.0}, {-2700.0, -56500.0}, {-14000.0, -62000.0}, {-25500.0, -69000.0}}};
		return Lines;
	}

	inline const TArray<TArray<FVector2D>>& RoadPoints()
	{
		static const TArray<TArray<FVector2D>> Lines{
			// The ring round the middle of the island.
			{{22800.0, 22800.0}, {4700.0, 32800.0}, {-20100.0, 30800.0}, {-37800.0, 17400.0}, {-38800.0, -5700.0}, {-30100.0, -23400.0}, {-19100.0, -39500.0},
				{2700.0, -41500.0}, {19800.0, -27500.0}, {23100.0, -6700.0}, {23100.0, 11400.0}, {22800.0, 22800.0}},
			// Out from the capital's four gates.
			{{9000.0, 0.0}, {23100.0, 700.0}, {34000.0, 1300.0}, {46000.0, 1000.0}},
			{{0.0, 9000.0}, {700.0, 24700.0}, {-1300.0, 35500.0}, {-3000.0, 42500.0}},
			{{-9000.0, 0.0}, {-27800.0, -1700.0}, {-38800.0, -4000.0}, {-43500.0, -5000.0}},
			{{0.0, -9000.0}, {-3000.0, -17000.0}, {-6500.0, -23900.0}},
			// To the outlying districts.
			{{22800.0, 22800.0}, {31000.0, 32000.0}},
			{{-30800.0, 25400.0}, {-37000.0, 32500.0}},
			{{-27500.0, -27500.0}, {-33000.0, -34000.0}},
			{{18800.0, -28500.0}, {27500.0, -32200.0}, {35500.0, -30800.0}},
			{{-38800.0, 9000.0}, {-48000.0, 16000.0}, {-55500.0, 21000.0}},
			{{-30100.0, -23400.0}, {-27000.0, -20500.0}},
			{{19800.0, -27500.0}, {12800.0, -29500.0}},
			{{-19100.0, -39500.0}, {-16500.0, -43000.0}},
			{{4700.0, 32800.0}, {11500.0, 29000.0}}};
		return Lines;
	}
}
