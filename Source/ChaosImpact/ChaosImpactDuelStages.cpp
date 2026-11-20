#include "ChaosImpactDuelStages.h"

#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/RotatingMovementComponent.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr float DuelHalf = AChaosImpactDuelStage::DuelHalfExtent;

	UMaterialInterface* DuelFindMaterial(const TCHAR* Path)
	{
		ConstructorHelpers::FObjectFinder<UMaterialInterface> Found(Path);
		return Found.Succeeded() ? Found.Object : nullptr;
	}

	UStaticMesh* DuelFindMesh(const TCHAR* Path)
	{
		ConstructorHelpers::FObjectFinder<UStaticMesh> Found(Path);
		return Found.Succeeded() ? Found.Object : nullptr;
	}

	UPointLightComponent* DuelAddLight(AActor* Owner, USceneComponent* Root, const FString& Name, const FVector& At, const FLinearColor& Colour,
		const float Intensity, const float Radius)
	{
		UPointLightComponent* Light = Owner->CreateDefaultSubobject<UPointLightComponent>(FName(*Name));
		Light->SetupAttachment(Root);
		Light->SetRelativeLocation(At);
		Light->SetLightColor(Colour);
		Light->SetIntensity(Intensity);
		Light->SetAttenuationRadius(Radius);
		Light->SetCastShadows(false);
		return Light;
	}

	/** The yaw of a line on the floor. */
	float DuelYawOf(const FVector& Line) { return FMath::RadiansToDegrees(FMath::Atan2(Line.Y, Line.X)); }
}

// ================================================================================================ 天空神殿

AChaosImpactDuelTempleStage::AChaosImpactDuelTempleStage()
	: Super(EBuild::Empty)
{
	// The solo temple's stone, runes and clouds (as VS ステージ3).
	UMaterialInterface* Floor = DuelFindMaterial(TEXT("/Game/ChaosImpact/solo/Stage2/Materials/MI_Floor_Ruins.MI_Floor_Ruins"));
	UMaterialInterface* Bridge = DuelFindMaterial(TEXT("/Game/ChaosImpact/solo/Stage2/Materials/MI_Floor_Bridge.MI_Floor_Bridge"));
	UMaterialInterface* Pale = DuelFindMaterial(TEXT("/Game/ChaosImpact/solo/Stage2/Materials/MI_Stone_Pale.MI_Stone_Pale"));
	UMaterialInterface* Sand = DuelFindMaterial(TEXT("/Game/ChaosImpact/solo/Stage2/Materials/MI_Stone_Sand.MI_Stone_Sand"));
	UMaterialInterface* Dark = DuelFindMaterial(TEXT("/Game/ChaosImpact/solo/Stage2/Materials/MI_Stone_Dark.MI_Stone_Dark"));
	UMaterialInterface* RuneTeal = DuelFindMaterial(TEXT("/Game/ChaosImpact/solo/Stage2/Materials/MI_Rune_Teal.MI_Rune_Teal"));
	UMaterialInterface* RuneGold = DuelFindMaterial(TEXT("/Game/ChaosImpact/solo/Stage2/Materials/MI_Rune_Gold.MI_Rune_Gold"));
	UMaterialInterface* Moss = DuelFindMaterial(TEXT("/Game/ChaosImpact/solo/Stage2/Materials/MI_Moss.MI_Moss"));
	UMaterialInterface* FieldSoft = DuelFindMaterial(TEXT("/Game/ChaosImpact/solo/Stage2/Materials/MI_Field_TealSoft.MI_Field_TealSoft"));
	UMaterialInterface* Cloud = DuelFindMaterial(TEXT("/Game/ChaosImpact/solo/Stage2/Materials/M_CI_CloudSea.M_CI_CloudSea"));

	SpawnPointOffsets = {FVector(-1500.0f, 0.0f, 0.0f), FVector(1500.0f, 0.0f, 0.0f),
		FVector(-1500.0f, 850.0f, 0.0f), FVector(1500.0f, -850.0f, 0.0f), FVector(-1500.0f, -850.0f, 0.0f), FVector(1500.0f, 850.0f, 0.0f)};
	// Just ahead of each start, on the altar, and on each side walkway.
	BallPointOffsets = {FVector(-1300.0f, 0.0f, 0.0f), FVector(1300.0f, 0.0f, 0.0f), FVector(0.0f, 0.0f, 0.0f),
		FVector(0.0f, 1550.0f, 0.0f), FVector(0.0f, -1550.0f, 0.0f)};

	constexpr float Thick = 60.0f;
	// ---- The floor (2026-11-16, as the user asked: one whole square, nothing standing on it): the temple's paving with a
	// paler path between the starts and the altar's circle in the middle, its runes laid flat.
	// (In slabs of the VS temple's size, so the paving keeps its own scale instead of one stretched picture.)
	constexpr int32 Slabs = 8;
	constexpr float Slab = DuelHalf * 2.0f / Slabs;
	for (int32 Row = 0; Row < Slabs; ++Row)
	{
		for (int32 Col = 0; Col < Slabs; ++Col)
		{
			AddCube(FString::Printf(TEXT("Deck%d_%d"), Row, Col), Floor,
				FVector(-DuelHalf + (Col + 0.5f) * Slab, -DuelHalf + (Row + 0.5f) * Slab, -Thick * 0.5f - ((Row + Col) % 2) * 0.2f), FVector(Slab, Slab, Thick));
		}
	}
	AddCube(TEXT("DeckPath"), Bridge, FVector(0.0f, 0.0f, 0.3f), FVector(DuelHalf * 2.0f - 120.0f, BridgeHalfWidth * 2.0f, 1.0f), 0.0f, false);
	AddCube(TEXT("DeckCross"), Bridge, FVector(0.0f, 0.0f, 0.3f), FVector(CrossHalfWidth * 2.0f, DuelHalf * 2.0f - 120.0f, 1.0f), 0.0f, false);
	AddDisc(TEXT("Altar"), Pale, FVector(0.0f, 0.0f, -0.5f), AltarRadius * 2.0f, 1.5f, false);
	AddBand(TEXT("AltarGold"), RuneGold, FVector(0.0f, 0.0f, 1.2f), AltarRadius - 40.0f, 5.0f);
	AddBand(TEXT("AltarRune"), RuneTeal, FVector(0.0f, 0.0f, 1.3f), 250.0f, 4.0f);
	AddBand(TEXT("AltarCore"), RuneGold, FVector(0.0f, 0.0f, 1.4f), 110.0f, 4.0f);
	for (int32 Index = 0; Index < 8; ++Index)
	{
		const float Angle = Index * 45.0f + 22.5f;
		const FVector Out(FMath::Cos(FMath::DegreesToRadians(Angle)), FMath::Sin(FMath::DegreesToRadians(Angle)), 0.0f);
		AddCube(FString::Printf(TEXT("AltarGlyph%d"), Index), RuneTeal, Out * 340.0f + FVector(0.0f, 0.0f, 1.5f), FVector(60.0f, 14.0f, 1.2f),
			Angle + 90.0f, false);
	}
	for (const float X : {-1.0f, 1.0f})
	{
		AddBand(X < 0.0f ? TEXT("StartRuneR") : TEXT("StartRuneB"), RuneTeal, FVector(X * 1500.0f, 0.0f, 1.2f), 170.0f, 4.0f);
	}
	// The rock the square floats on.
	for (const float X : {-1.0f, 0.0f, 1.0f})
	{
		AddPiece(nullptr, FString::Printf(TEXT("DeckRock%d"), FMath::RoundToInt(X) + 1), ConeMesh.Get(), X == 0.0f ? Sand : Dark,
			FVector(X * 1250.0f, 0.0f, -Thick - 900.0f), FVector(13.0f, 36.0f, 17.0f), FRotator(180.0f, 0.0f, 0.0f), false);
	}

	// ---- What stands on it: the temple's ruins (every one mirrored end for end).
	const auto Column = [this, Sand, Pale, RuneTeal, RuneGold](const FString& Name, const FVector& At, const float Height, const bool bBroken)
	{
		AddCube(Name + TEXT("Plinth"), Sand, At + FVector(0.0f, 0.0f, 20.0f), FVector(170.0f, 170.0f, 40.0f), 0.0f, true, true);
		AddDisc(Name + TEXT("Shaft"), Pale, At + FVector(0.0f, 0.0f, 40.0f), 110.0f, Height);
		AddBand(Name + TEXT("Rune"), RuneTeal, At + FVector(0.0f, 0.0f, 110.0f), 57.0f, 10.0f);
		if (!bBroken)
		{
			AddBand(Name + TEXT("Gilt"), RuneGold, At + FVector(0.0f, 0.0f, 40.0f + Height - 26.0f), 58.0f, 10.0f);
			AddCube(Name + TEXT("Capital"), Sand, At + FVector(0.0f, 0.0f, 40.0f + Height + 18.0f), FVector(160.0f, 160.0f, 36.0f), 0.0f, true, true);
		}
	};
	// The colonnade round the altar: eight columns, open between every pair (the straight ways in and the diagonals).
	for (int32 Index = 0; Index < 8; ++Index)
	{
		const float Angle = FMath::DegreesToRadians(22.5f + Index * 45.0f);
		Column(FString::Printf(TEXT("Colonnade%d"), Index), FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * 640.0f, 280.0f, false);
	}
	for (const float X : {-1.0f, 1.0f})
	{
		const FString End = X < 0.0f ? TEXT("R") : TEXT("B");
		// A low rune wall before the start (jumpable), as in カオスアリーナ.
		AddCube(TEXT("RuneWall") + End, Sand, FVector(X * 1080.0f, 0.0f, 47.5f), FVector(80.0f, 520.0f, 95.0f), 0.0f, true, true);
		AddCube(TEXT("RuneWallGlow") + End, RuneTeal, FVector(X * 1080.0f, 0.0f, 96.0f), FVector(40.0f, 480.0f, 3.0f), 0.0f, false);
		for (const float Y : {-1.0f, 1.0f})
		{
			const FString Name = FString::Printf(TEXT("%s%s"), *End, Y < 0.0f ? TEXT("S") : TEXT("N"));
			// A broken column either side of the start's way out.
			Column(TEXT("Broken") + Name, FVector(X * 1250.0f, Y * 820.0f, 0.0f), 130.0f, true);
			AddCube(TEXT("BrokenTop") + Name, Pale, FVector(X * 1250.0f, Y * 820.0f, 175.0f), FVector(90.0f, 90.0f, 12.0f), 25.0f * Y, false, true);
			// A column fallen toward the corner, its capital broken off beside it (cover you can jump). (Pitched over and turned,
			// the cylinder runs from its base toward the corner: its middle at X 1450, Y 1450.)
			AddPiece(nullptr, TEXT("Fallen") + Name, CylinderMesh.Get(), Pale, FVector(X * 1450.0f, Y * 1450.0f, 52.0f) - FVector(X, Y, 0.0f).GetSafeNormal() * 170.0f,
				FVector(1.04f, 1.04f, 3.4f), FRotator(90.0f, DuelYawOf(FVector(X, Y, 0.0f)) + 180.0f, 0.0f), true);
			AddBand(TEXT("FallenRune") + Name, RuneTeal, FVector(X * 1450.0f, Y * 1450.0f, 52.0f), 54.0f, 10.0f)
				->SetRelativeRotation(FRotator(90.0f, DuelYawOf(FVector(X, Y, 0.0f)), 0.0f));
			AddCube(TEXT("FallenCap") + Name, Sand, FVector(X * 1620.0f, Y * 1250.0f, 18.0f), FVector(150.0f, 150.0f, 36.0f), 35.0f * X * Y, true, true);
			AddCube(TEXT("FallenMoss") + Name, Moss, FVector(X * 1620.0f, Y * 1250.0f, 37.0f), FVector(110.0f, 80.0f, 3.0f), 35.0f * X * Y, false);
			// The ruin's old wall on the way round each side (low: cover, and jumpable).
			AddCube(TEXT("SideWall") + Name, Sand, FVector(X * 520.0f, Y * 1250.0f, 50.0f), FVector(520.0f, 80.0f, 100.0f), 0.0f, true, true);
			AddCube(TEXT("SideWallRune") + Name, RuneGold, FVector(X * 520.0f, Y * 1250.0f, 101.0f), FVector(480.0f, 30.0f, 3.0f), 0.0f, false);
			AddCube(TEXT("SideWallEnd") + Name, Pale, FVector(X * 800.0f, Y * 1250.0f, 75.0f), FVector(100.0f, 100.0f, 150.0f), 0.0f, true, true);
		}
	}

	// The outer edge: a balustrade all round, and the same fence.
	const FVector Corners[] = {{-DuelHalf, -DuelHalf, 0.0f}, {DuelHalf, -DuelHalf, 0.0f}, {DuelHalf, DuelHalf, 0.0f}, {-DuelHalf, DuelHalf, 0.0f}};
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const FVector From = Corners[Index];
		const FVector To = Corners[(Index + 1) % 4];
		AddCharacterFence(FString::Printf(TEXT("EdgeFence%d"), Index), From, To);
		const FVector Span = To - From;
		AddCube(FString::Printf(TEXT("Balustrade%d"), Index), Sand, (From + To) * 0.5f + FVector(0.0f, 0.0f, 22.0f),
			FVector(Span.Size2D() + 60.0f, 36.0f, 44.0f), DuelYawOf(Span), false, true);
		AddCube(FString::Printf(TEXT("BalustradeRune%d"), Index), RuneTeal, (From + To) * 0.5f + FVector(0.0f, 0.0f, 45.0f),
			FVector(Span.Size2D(), 10.0f, 2.0f), DuelYawOf(Span), false);
		for (int32 Post = 0; Post <= 8; ++Post)
		{
			const FVector At = From + Span * (Post / 8.0f);
			AddCube(FString::Printf(TEXT("BalustradePost%d_%d"), Index, Post), Pale, At + FVector(0.0f, 0.0f, 40.0f), FVector(60.0f, 60.0f, 80.0f),
				DuelYawOf(Span), false, true);
		}
	}

	// ---- Beyond: the sea of clouds below, the great ring turning slowly off the north side, islets with pines.
	AddPiece(nullptr, TEXT("CloudSea"), PlaneMesh.Get(), Cloud, FVector(0.0f, 0.0f, -2600.0f), FVector(86.0f, 86.0f, 1.0f),
		FRotator::ZeroRotator, false, false);
	RingRoot = CreateDefaultSubobject<USceneComponent>(TEXT("GreatRing"));
	RingRoot->SetupAttachment(SceneRoot);
	// (Clear of the opening's camera sweep, and inside the sky.)
	RingRoot->SetRelativeLocation(FVector(0.0f, 3600.0f, 800.0f));
	constexpr int32 Stones = 22;
	constexpr float RingRadius = 900.0f;
	for (int32 Index = 0; Index < Stones; ++Index)
	{
		const float Angle = Index * UE_TWO_PI / Stones;
		const FVector At(FMath::Cos(Angle) * RingRadius, 0.0f, FMath::Sin(Angle) * RingRadius);
		const FRotator Turn(-FMath::RadiansToDegrees(Angle), 0.0f, 0.0f);
		AddPiece(RingRoot, FString::Printf(TEXT("RingStone%d"), Index), ChamferMesh.Get(), Index % 2 ? Pale : Sand, At,
			FVector(1.6f, 2.0f, 2.6f), Turn, false);
		if (Index % 2 == 0)
		{
			AddPiece(RingRoot, FString::Printf(TEXT("RingRune%d"), Index), BoxMesh.Get(), RuneTeal, At - FVector(0.0f, 102.0f, 0.0f),
				FVector(0.4f, 0.04f, 0.4f), Turn, false, false);
		}
	}
	AddPiece(RingRoot, TEXT("RingVeil"), CylinderMesh.Get(), FieldSoft, FVector(0.0f, 0.0f, 0.0f), FVector(15.5f, 15.5f, 0.03f),
		FRotator(0.0f, 0.0f, 90.0f), false, false);
	RingTurn = CreateDefaultSubobject<URotatingMovementComponent>(TEXT("GreatRingTurn"));
	RingTurn->SetUpdatedComponent(RingRoot);
	// (The ring stands across the view, in its X-Z plane: it turns about its own Y.)
	RingTurn->RotationRate = FRotator(4.0f, 0.0f, 0.0f);
	RingTurn->bRotationInLocalSpace = true;
	AddDisc(TEXT("RingIsland"), Floor, FVector(0.0f, 3600.0f, -400.0f), 1300.0f, 60.0f, false);
	AddPiece(nullptr, TEXT("RingIslandRock"), ConeMesh.Get(), Sand, FVector(0.0f, 3600.0f, -1100.0f), FVector(13.0f, 13.0f, 14.0f),
		FRotator(180.0f, 0.0f, 0.0f), false);
	// Islets, mirrored (end for end) so both players see the same around them.
	const FVector Islets[] = {{2700.0f, 2300.0f, -500.0f}, {3000.0f, -1800.0f, -800.0f}, {2400.0f, -3200.0f, -300.0f}, {3300.0f, 600.0f, -1200.0f}};
	int32 Islet = 0;
	for (const FVector& Place : Islets)
	{
		for (const float X : {-1.0f, 1.0f})
		{
			const FVector At(Place.X * X, Place.Y, Place.Z);
			const float Size = 4.0f + (Islet % 3) * 2.0f;
			const FString Name = FString::Printf(TEXT("Islet%d"), Islet++);
			AddDisc(Name + TEXT("Top"), Islet % 2 ? Moss : Floor, At, Size * 100.0f, 50.0f, false);
			AddPiece(nullptr, Name + TEXT("Rock"), ConeMesh.Get(), Sand, At + FVector(0.0f, 0.0f, -Size * 60.0f), FVector(Size, Size, Size * 1.2f),
				FRotator(180.0f, 0.0f, 0.0f), false);
		}
	}

	// Warm light over each half and the altar (the same each end).
	SideLights.Add(DuelAddLight(this, SceneRoot, TEXT("LightAltar"), FVector(0.0f, 0.0f, 700.0f), FLinearColor(1.0f, 0.8f, 0.55f), 2600.0f, 2400.0f));
	SideLights.Add(DuelAddLight(this, SceneRoot, TEXT("LightRed"), FVector(-1400.0f, 0.0f, 700.0f), FLinearColor(1.0f, 0.85f, 0.7f), 2000.0f, 2400.0f));
	SideLights.Add(DuelAddLight(this, SceneRoot, TEXT("LightBlue"), FVector(1400.0f, 0.0f, 700.0f), FLinearColor(1.0f, 0.85f, 0.7f), 2000.0f, 2400.0f));
	AddSkyDome(4300.0f);
}

AChaosImpactDuelStage::FDuelLook AChaosImpactDuelTempleStage::GetArenaLook() const
{
	FDuelLook Look;
	Look.SunColor = FLinearColor(1.0f, 0.86f, 0.74f);
	Look.SunScale = 1.0f;
	Look.SkyLightScale = 1.1f;
	Look.FogColor = FLinearColor(0.55f, 0.45f, 0.48f, 1.0f);
	Look.FogStart = 9000.0f;
	return Look;
}

AChaosImpactDuelStage::FDuelSky AChaosImpactDuelTempleStage::GetSky() const
{
	FDuelSky Sky;
	Sky.Zenith = FLinearColor(0.22f, 0.3f, 0.62f);
	Sky.Horizon = FLinearColor(1.0f, 0.6f, 0.5f);
	Sky.Ground = FLinearColor(0.9f, 0.78f, 0.82f);
	Sky.SunDir = FVector(0.0f, 1.0f, 0.06f);
	Sky.SunColor = FLinearColor(1.0f, 0.78f, 0.5f);
	Sky.SunSize = 0.25f;
	Sky.SunGlow = 0.7f;
	Sky.Brightness = 1.0f;
	Sky.Haze = 0.35f;
	return Sky;
}

// ================================================================================================ 夕焼けコート

AChaosImpactDuelCourtStage::AChaosImpactDuelCourtStage()
	: Super(EBuild::Empty)
{
	UMaterialInterface* Concrete = DuelFindMaterial(TEXT("/Game/ChaosImpact/Versus/Splash/M_CI_Concrete.M_CI_Concrete"));
	UMaterialInterface* Planks = DuelFindMaterial(TEXT("/Game/ChaosImpact/Versus/Splash/M_CI_Planks.M_CI_Planks"));
	UStaticMesh* Fountain = DuelFindMesh(TEXT("/Game/Cartoon_City_Free/Meshes/Props/SM_Fountain_03.SM_Fountain_03"));
	UStaticMesh* Lamp = DuelFindMesh(TEXT("/Game/Cartoon_City_Free/Meshes/Props/SM_Spotlight_01.SM_Spotlight_01"));
	UStaticMesh* Palm = DuelFindMesh(TEXT("/Game/Cartoon_City_Free/Meshes/Vegetation/SM_Palm_03.SM_Palm_03"));
	UStaticMesh* Bush = DuelFindMesh(TEXT("/Game/Cartoon_City_Free/Meshes/Vegetation/SM_Bush_06.SM_Bush_06"));
	UStaticMesh* Terrace = DuelFindMesh(TEXT("/Game/Cartoon_City_Free/Meshes/Buildings/SM_Eco_Building_Terrace.SM_Eco_Building_Terrace"));
	UStaticMesh* Slope = DuelFindMesh(TEXT("/Game/Cartoon_City_Free/Meshes/Buildings/SM_Eco_Building_Slope.SM_Eco_Building_Slope"));
	UStaticMesh* Grid = DuelFindMesh(TEXT("/Game/Cartoon_City_Free/Meshes/Buildings/SM_Eco_Building_Grid.SM_Eco_Building_Grid"));
	UStaticMesh* Billboard = DuelFindMesh(TEXT("/Game/Cartoon_City_Free/Meshes/Billboards/SM_Billboard_4x1_03.SM_Billboard_4x1_03"));

	SpawnPointOffsets = {FVector(-1500.0f, 0.0f, 0.0f), FVector(1500.0f, 0.0f, 0.0f),
		FVector(-1500.0f, 850.0f, 0.0f), FVector(1500.0f, -850.0f, 0.0f), FVector(-1500.0f, -850.0f, 0.0f), FVector(1500.0f, 850.0f, 0.0f)};
	// Before each start, and inside the diamond either side of the fountain.
	BallPointOffsets = {FVector(-1300.0f, 0.0f, 0.0f), FVector(1300.0f, 0.0f, 0.0f), FVector(0.0f, 600.0f, 0.0f), FVector(0.0f, -600.0f, 0.0f)};

	const FLinearColor Court(0.05f, 0.16f, 0.15f);
	const FLinearColor Apron(0.2f, 0.07f, 0.05f);
	const FLinearColor Line(0.85f, 0.82f, 0.74f);
	const FLinearColor Warm(0.5f, 0.38f, 0.3f);
	// ---- The ground: the court in its colours, the street round it.
	Tint(AddCube(TEXT("Court"), PaintMaterial, FVector(0.0f, 0.0f, -10.0f), FVector(DuelHalf * 2.0f, DuelHalf * 2.0f, 20.0f)), Court);
	Tint(AddCube(TEXT("CourtKeyR"), PaintMaterial, FVector(-DuelHalf + 330.0f, 0.0f, 0.4f), FVector(660.0f, 1100.0f, 1.0f), 0.0f, false), Apron);
	Tint(AddCube(TEXT("CourtKeyB"), PaintMaterial, FVector(DuelHalf - 330.0f, 0.0f, 0.4f), FVector(660.0f, 1100.0f, 1.0f), 0.0f, false), Apron);
	Tint(AddCube(TEXT("Street"), Concrete, FVector(0.0f, 0.0f, -32.0f), FVector(DuelHalf * 2.0f + 1600.0f, DuelHalf * 2.0f + 1600.0f, 20.0f), 0.0f, false),
		FLinearColor(0.32f, 0.3f, 0.3f));
	Tint(AddCube(TEXT("Road"), Concrete, FVector(0.0f, 0.0f, -48.0f), FVector(8000.0f, 8000.0f, 20.0f), 0.0f, false), FLinearColor(0.07f, 0.07f, 0.08f));
	// The lines: round the court, across the middle, the centre circle, each end's key.
	int32 Lines = 0;
	const auto Paint = [this, &Lines, &Line](const FVector& Middle, const FVector& Size)
	{
		UStaticMeshComponent* Mark = AddCube(FString::Printf(TEXT("CourtLine%d"), Lines++), FlatMaterial, Middle + FVector(0.0f, 0.0f, 0.8f), Size, 0.0f, false);
		Mark->SetCastShadow(false);
		Tint(Mark, Line);
	};
	Paint(FVector(0.0f, DuelHalf - 60.0f, 0.0f), FVector(DuelHalf * 2.0f - 120.0f, 18.0f, 1.0f));
	Paint(FVector(0.0f, -DuelHalf + 60.0f, 0.0f), FVector(DuelHalf * 2.0f - 120.0f, 18.0f, 1.0f));
	Paint(FVector(DuelHalf - 60.0f, 0.0f, 0.0f), FVector(18.0f, DuelHalf * 2.0f - 120.0f, 1.0f));
	Paint(FVector(-DuelHalf + 60.0f, 0.0f, 0.0f), FVector(18.0f, DuelHalf * 2.0f - 120.0f, 1.0f));
	Paint(FVector(0.0f, 0.0f, 0.0f), FVector(18.0f, DuelHalf * 2.0f - 120.0f, 1.0f));
	for (const float X : {-1.0f, 1.0f})
	{
		Paint(FVector(X * (DuelHalf - 660.0f), 0.0f, 0.0f), FVector(18.0f, 1100.0f, 1.0f));
		Paint(FVector(X * (DuelHalf - 360.0f), 550.0f, 0.0f), FVector(600.0f, 18.0f, 1.0f));
		Paint(FVector(X * (DuelHalf - 360.0f), -550.0f, 0.0f), FVector(600.0f, 18.0f, 1.0f));
	}

	// ---- The diamond of low walls round the fountain, open at its four points.
	int32 Walls = 0;
	const FVector Points[] = {{DiamondReach, 0.0f, 0.0f}, {0.0f, DiamondReach, 0.0f}, {-DiamondReach, 0.0f, 0.0f}, {0.0f, -DiamondReach, 0.0f}};
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const FVector A = Points[Index];
		const FVector B = Points[(Index + 1) % 4];
		const FVector Along = (B - A).GetSafeNormal();
		const FVector From = A + Along * DiamondGap;
		const FVector To = B - Along * DiamondGap;
		const FVector Middle = (From + To) * 0.5f;
		const float Length = FVector::Dist(From, To);
		const FString Name = FString::Printf(TEXT("DiamondWall%d"), Walls++);
		Tint(AddCube(Name, Concrete, Middle + FVector(0.0f, 0.0f, 55.0f), FVector(Length, 70.0f, 110.0f), DuelYawOf(Along), true, true), Warm);
		// A coping stone along its top, a shade paler.
		Tint(AddCube(Name + TEXT("Cap"), Concrete, Middle + FVector(0.0f, 0.0f, 114.0f), FVector(Length + 10.0f, 86.0f, 10.0f), DuelYawOf(Along), false, true),
			FLinearColor(0.62f, 0.55f, 0.5f));
		// End posts at its ends.
		for (const FVector& End : {From, To})
		{
			Tint(AddCube(Name + (End == From ? TEXT("PostA") : TEXT("PostB")), Concrete, End + FVector(0.0f, 0.0f, 70.0f), FVector(90.0f, 90.0f, 140.0f),
				DuelYawOf(Along), true, true), FLinearColor(0.35f, 0.27f, 0.22f));
		}
	}
	// The fountain (its basin is the wall here).
	AddPiece(nullptr, TEXT("Fountain"), Fountain, nullptr, FVector(0.0f, 0.0f, 0.0f), FVector(0.8f), FRotator::ZeroRotator, false);
	UStaticMeshComponent* Basin = AddDisc(TEXT("FountainBody"), FlatMaterial, FVector(0.0f, 0.0f, 0.0f), 460.0f, 220.0f, true);
	Basin->SetVisibility(false);
	Basin->SetHiddenInGame(true);

	// ---- Each half: planters with bushes by the corners, benches along the side lines, a bin by each.
	for (const float X : {-1.0f, 1.0f})
	{
		for (const float Y : {-1.0f, 1.0f})
		{
			const FString Name = FString::Printf(TEXT("Planter%s%s"), X < 0.0f ? TEXT("R") : TEXT("B"), Y < 0.0f ? TEXT("S") : TEXT("N"));
			const FVector At(X * 1250.0f, Y * 1180.0f, 0.0f);
			Tint(AddCube(Name, Concrete, At + FVector(0.0f, 0.0f, 40.0f), FVector(380.0f, 140.0f, 80.0f), 0.0f, true, true), FLinearColor(0.42f, 0.36f, 0.32f));
			// Three round shrubs in the soil (set in it, not on top of it).
			for (int32 Leaf = 0; Leaf < 3; ++Leaf)
			{
				AddPiece(nullptr, FString::Printf(TEXT("%sBush%d"), *Name, Leaf), Bush, nullptr, At + FVector((Leaf - 1) * 115.0f, 0.0f, 62.0f),
					FVector(0.62f, 0.55f, Leaf == 1 ? 0.62f : 0.5f), FRotator(0.0f, Leaf * 70.0f + 20.0f, 0.0f), false);
			}
			Tint(AddCube(Name + TEXT("Soil"), FlatMaterial, At + FVector(0.0f, 0.0f, 79.0f), FVector(350.0f, 112.0f, 4.0f), 0.0f, false),
				FLinearColor(0.09f, 0.06f, 0.04f));
			// A bench by the side line (low: cover only when crouching behind it... and something to jump over).
			const FVector Bench(X * 600.0f, Y * 1600.0f, 0.0f);
			Tint(AddCube(Name + TEXT("Bench"), Planks, Bench + FVector(0.0f, 0.0f, 45.0f), FVector(330.0f, 80.0f, 12.0f), 0.0f, true), FLinearColor(0.55f, 0.32f, 0.16f));
			for (const float Leg : {-1.0f, 1.0f})
			{
				Tint(AddCube(FString::Printf(TEXT("%sBenchLeg%d"), *Name, Leg > 0.0f ? 1 : 0), FlatMaterial, Bench + FVector(Leg * 130.0f, 0.0f, 20.0f),
					FVector(14.0f, 70.0f, 40.0f), 0.0f, true), FLinearColor(0.1f, 0.1f, 0.11f));
			}
		}
	}

	// ---- The fence round the court: posts and rails to look at, an unseen wall that stops players and balls.
	const FVector Corners[] = {{-DuelHalf, -DuelHalf, 0.0f}, {DuelHalf, -DuelHalf, 0.0f}, {DuelHalf, DuelHalf, 0.0f}, {-DuelHalf, DuelHalf, 0.0f}};
	const FLinearColor Iron(0.05f, 0.055f, 0.06f);
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const FVector From = Corners[Index];
		const FVector To = Corners[(Index + 1) % 4];
		const FVector Span = To - From;
		AddInvisibleWall(FString::Printf(TEXT("FenceWall%d"), Index), From, To, 400.0f);
		for (const float Height : {90.0f, 300.0f})
		{
			Tint(AddCube(FString::Printf(TEXT("FenceRail%d_%d"), Index, FMath::RoundToInt(Height)), FlatMaterial, (From + To) * 0.5f + FVector(0.0f, 0.0f, Height),
				FVector(Span.Size2D(), 8.0f, 8.0f), DuelYawOf(Span), false), Iron);
		}
		for (int32 Post = 0; Post < 8; ++Post)
		{
			const FVector At = From + Span * (Post / 8.0f);
			Tint(AddDisc(FString::Printf(TEXT("FencePost%d_%d"), Index, Post), FlatMaterial, At, 14.0f, 310.0f, false), Iron);
		}
	}
	// A hoop behind each end.
	for (const float X : {-1.0f, 1.0f})
	{
		const FString End = X < 0.0f ? TEXT("R") : TEXT("B");
		const FVector Base(X * (DuelHalf + 140.0f), 0.0f, 0.0f);
		Tint(AddDisc(TEXT("HoopPole") + End, FlatMaterial, Base, 24.0f, 360.0f, false), Iron);
		Tint(AddCube(TEXT("HoopArm") + End, FlatMaterial, Base + FVector(-X * 60.0f, 0.0f, 350.0f), FVector(120.0f, 10.0f, 10.0f), 0.0f, false), Iron);
		Tint(AddCube(TEXT("HoopBoard") + End, FlatMaterial, Base + FVector(-X * 120.0f, 0.0f, 390.0f), FVector(8.0f, 180.0f, 110.0f), 0.0f, false),
			FLinearColor(0.85f, 0.85f, 0.82f));
		Tint(AddBand(TEXT("HoopRim") + End, FlatMaterial, Base + FVector(-X * 160.0f, 0.0f, 345.0f), 26.0f, 3.0f), FLinearColor(0.9f, 0.3f, 0.05f));
	}

	// ---- The street round it: lamps at the corners, palms, and the town beyond (the same at both ends).
	for (const float X : {-1.0f, 1.0f})
	{
		for (const float Y : {-1.0f, 1.0f})
		{
			const FString Name = FString::Printf(TEXT("Lamp%s%s"), X < 0.0f ? TEXT("R") : TEXT("B"), Y < 0.0f ? TEXT("S") : TEXT("N"));
			const FVector At(X * (DuelHalf + 260.0f), Y * (DuelHalf + 260.0f), -22.0f);
			// The lamp's arm reaches over the court's corner.
			AddPiece(nullptr, Name, Lamp, nullptr, At, FVector(1.0f), FRotator(0.0f, DuelYawOf(-At) - 90.0f, 0.0f), false);
			SideLights.Add(DuelAddLight(this, SceneRoot, Name + TEXT("Light"), At + (-At).GetSafeNormal2D() * 240.0f + FVector(0.0f, 0.0f, 780.0f),
				FLinearColor(1.0f, 0.72f, 0.42f), 3500.0f, 2600.0f));
			AddPiece(nullptr, Name + TEXT("Palm"), Palm, nullptr, FVector(X * (DuelHalf + 560.0f), Y * 700.0f, -22.0f), FVector(0.38f),
				FRotator(0.0f, 40.0f * X + 25.0f * Y, 0.0f), false);
			AddPiece(nullptr, Name + TEXT("Palm2"), Palm, nullptr, FVector(X * 900.0f, Y * (DuelHalf + 560.0f), -22.0f), FVector(0.34f),
				FRotator(0.0f, 70.0f * Y - 20.0f * X, 0.0f), false);
		}
	}
	// The town: buildings along both long sides and behind both ends.
	for (const float Y : {-1.0f, 1.0f})
	{
		const FString Side = Y < 0.0f ? TEXT("S") : TEXT("N");
		AddPiece(nullptr, TEXT("TownMid") + Side, Terrace, nullptr, FVector(0.0f, Y * 3300.0f, -48.0f), FVector(0.75f), FRotator(0.0f, 90.0f, 0.0f), false);
		for (const float X : {-1.0f, 1.0f})
		{
			AddPiece(nullptr, FString::Printf(TEXT("TownSide%s%s"), *Side, X < 0.0f ? TEXT("R") : TEXT("B")), Slope, nullptr,
				FVector(X * 2700.0f, Y * 3300.0f, -48.0f), FVector(0.55f), FRotator(0.0f, 90.0f * X, 0.0f), false);
		}
		// A sign on two posts over the street.
		AddPiece(nullptr, TEXT("TownSign") + Side, Billboard, nullptr, FVector(0.0f, Y * (DuelHalf + 900.0f), 330.0f), FVector(0.9f),
			FRotator(0.0f, 90.0f + 90.0f * Y, 0.0f), false);
		for (const float Leg : {-1.0f, 1.0f})
		{
			Tint(AddDisc(FString::Printf(TEXT("TownSignPost%s%d"), *Side, Leg > 0.0f ? 1 : 0), FlatMaterial,
				FVector(Leg * 330.0f, Y * (DuelHalf + 900.0f + 34.0f), -22.0f), 26.0f, 360.0f, false), FLinearColor(0.08f, 0.085f, 0.09f));
		}
	}
	for (const float X : {-1.0f, 1.0f})
	{
		AddPiece(nullptr, X < 0.0f ? TEXT("TownEndR") : TEXT("TownEndB"), Grid, nullptr, FVector(X * 3500.0f, 0.0f, -48.0f), FVector(0.5f),
			FRotator(0.0f, X < 0.0f ? 0.0f : 180.0f, 0.0f), false);
	}
	AddSkyDome(4300.0f);
}

AChaosImpactDuelStage::FDuelLook AChaosImpactDuelCourtStage::GetArenaLook() const
{
	FDuelLook Look;
	Look.SunColor = FLinearColor(1.0f, 0.76f, 0.55f);
	Look.SunScale = 0.95f;
	Look.SkyLightScale = 1.0f;
	// (No haze over the court and its town: they keep their colours under the sunset sky.)
	Look.FogColor = FLinearColor(0.32f, 0.2f, 0.16f, 1.0f);
	Look.FogStart = 9000.0f;
	return Look;
}

AChaosImpactDuelStage::FDuelSky AChaosImpactDuelCourtStage::GetSky() const
{
	FDuelSky Sky;
	Sky.Zenith = FLinearColor(0.16f, 0.17f, 0.42f);
	Sky.Horizon = FLinearColor(1.0f, 0.42f, 0.16f);
	Sky.Ground = FLinearColor(0.2f, 0.12f, 0.1f);
	Sky.SunDir = FVector(0.0f, 1.0f, 0.045f);
	Sky.SunColor = FLinearColor(1.0f, 0.62f, 0.25f);
	Sky.SunSize = 0.35f;
	Sky.SunGlow = 0.9f;
	Sky.Brightness = 1.0f;
	Sky.Haze = 0.5f;
	return Sky;
}

// ================================================================================================ 夜の屋上

AChaosImpactDuelRooftopStage::AChaosImpactDuelRooftopStage()
	: Super(EBuild::Empty)
{
	UMaterialInterface* Concrete = DuelFindMaterial(TEXT("/Game/ChaosImpact/Versus/Splash/M_CI_Concrete.M_CI_Concrete"));
	UMaterialInterface* Corrugated = DuelFindMaterial(TEXT("/Game/ChaosImpact/Versus/Splash/M_CI_Corrugated.M_CI_Corrugated"));
	UStaticMesh* Flood = DuelFindMesh(TEXT("/Game/Cartoon_City_Free/Meshes/Props/SM_Spotlight_02.SM_Spotlight_02"));
	UStaticMesh* Tower = DuelFindMesh(TEXT("/Game/Cartoon_City_Free/Meshes/Buildings/SM_Regular_Building_TwistedTower_Large_NightLight.SM_Regular_Building_TwistedTower_Large_NightLight"));
	UStaticMesh* GridLit = DuelFindMesh(TEXT("/Game/Cartoon_City_Free/Meshes/Buildings/SM_Eco_Building_Grid_NightLight.SM_Eco_Building_Grid_NightLight"));
	// (The NightLight meshes are only a building's lit windows: each stands in its building.)
	UStaticMesh* TowerBody = DuelFindMesh(TEXT("/Game/Cartoon_City_Free/Meshes/Buildings/SM_Regular_Building_TwistedTower_Large.SM_Regular_Building_TwistedTower_Large"));
	UStaticMesh* GridBody = DuelFindMesh(TEXT("/Game/Cartoon_City_Free/Meshes/Buildings/SM_Eco_Building_Grid.SM_Eco_Building_Grid"));

	SpawnPointOffsets = {FVector(-1500.0f, 0.0f, 0.0f), FVector(1500.0f, 0.0f, 0.0f),
		FVector(-1500.0f, 850.0f, 0.0f), FVector(1500.0f, -850.0f, 0.0f), FVector(-1500.0f, -850.0f, 0.0f), FVector(1500.0f, 850.0f, 0.0f)};
	// Before each start, on the helipad, and down each side lane.
	BallPointOffsets = {FVector(-1300.0f, 0.0f, 0.0f), FVector(1300.0f, 0.0f, 0.0f), FVector(0.0f, 0.0f, 0.0f),
		FVector(0.0f, 1350.0f, 0.0f), FVector(0.0f, -1350.0f, 0.0f)};

	const FLinearColor Cyan(0.1f, 0.85f, 1.0f);
	const FLinearColor Magenta(1.0f, 0.15f, 0.65f);
	const FLinearColor Amber(1.0f, 0.62f, 0.1f);
	// ---- The roof: concrete, the helipad in the middle, hazard stripes along the lanes' ends.
	Tint(AddCube(TEXT("Roof"), Concrete, FVector(0.0f, 0.0f, -15.0f), FVector(DuelHalf * 2.0f + 400.0f, DuelHalf * 2.0f + 400.0f, 30.0f)),
		FLinearColor(0.16f, 0.17f, 0.2f));
	AddPiece(nullptr, TEXT("RoofBlock"), BoxMesh.Get(), Concrete, FVector(0.0f, 0.0f, -1800.0f), FVector(43.0f, 43.0f, 35.0f), FRotator::ZeroRotator, false);
	Tint(AddDisc(TEXT("Helipad"), PaintMaterial, FVector(0.0f, 0.0f, 0.2f), 1000.0f, 1.0f, false), FLinearColor(0.05f, 0.06f, 0.08f));
	Tint(AddBand(TEXT("HelipadRing"), PaintMaterial, FVector(0.0f, 0.0f, 0.8f), 470.0f, 1.0f), Amber);
	const FLinearColor HMark(0.85f, 0.85f, 0.8f);
	Tint(AddCube(TEXT("HelipadH1"), FlatMaterial, FVector(0.0f, -130.0f, 1.0f), FVector(320.0f, 40.0f, 1.0f), 0.0f, false), HMark);
	Tint(AddCube(TEXT("HelipadH2"), FlatMaterial, FVector(0.0f, 130.0f, 1.0f), FVector(320.0f, 40.0f, 1.0f), 0.0f, false), HMark);
	Tint(AddCube(TEXT("HelipadH3"), FlatMaterial, FVector(0.0f, 0.0f, 1.0f), FVector(40.0f, 220.0f, 1.0f), 0.0f, false), HMark);
	for (int32 Index = 0; Index < 12; ++Index)
	{
		const float Angle = FMath::DegreesToRadians(Index * 30.0f + 15.0f);
		UStaticMeshComponent* Light = AddCube(FString::Printf(TEXT("PadLight%d"), Index), GlowMaterial,
			FVector(FMath::Cos(Angle) * 520.0f, FMath::Sin(Angle) * 520.0f, 4.0f), FVector(26.0f, 26.0f, 8.0f), 0.0f, false);
		Light->SetCastShadow(false);
		Tint(Light, Index % 2 ? Cyan : Amber, 3.0f);
	}

	// ---- The two zigzag ducts: from end to end along both sides of the middle lane, crossing over at their peaks.
	const FVector Zig[] = {{-1450.0f, 650.0f, 0.0f}, {-725.0f, 980.0f, 0.0f}, {0.0f, 650.0f, 0.0f}, {725.0f, 980.0f, 0.0f}, {1450.0f, 650.0f, 0.0f}};
	constexpr float PeakGap = 170.0f;
	for (const float Y : {-1.0f, 1.0f})
	{
		const FString Side = Y < 0.0f ? TEXT("S") : TEXT("N");
		for (int32 Leg = 0; Leg < 4; ++Leg)
		{
			FVector From = Zig[Leg] * FVector(1.0f, Y, 1.0f);
			FVector To = Zig[Leg + 1] * FVector(1.0f, Y, 1.0f);
			const FVector Along = (To - From).GetSafeNormal();
			// Open at the peaks (the lanes cross there); joined in the valley at the middle.
			if (Leg == 0 || Leg == 2) { To -= Along * PeakGap; }
			if (Leg == 1 || Leg == 3) { From += Along * PeakGap; }
			const FVector Middle = (From + To) * 0.5f;
			const float Length = FVector::Dist(From, To) + (Leg == 1 || Leg == 2 ? 45.0f : 0.0f);
			const float Yaw = DuelYawOf(Along);
			const FString Name = FString::Printf(TEXT("Duct%s%d"), *Side, Leg);
			Tint(AddCube(Name, Corrugated, Middle + FVector(0.0f, 0.0f, 75.0f), FVector(Length, 90.0f, 150.0f), Yaw, true), FLinearColor(0.42f, 0.46f, 0.52f));
			Tint(AddCube(Name + TEXT("Lip"), FlatMaterial, Middle + FVector(0.0f, 0.0f, 152.0f), FVector(Length + 8.0f, 98.0f, 6.0f), Yaw, false),
				FLinearColor(0.12f, 0.13f, 0.15f));
			// A pipe along its top, and a neon strip low on both faces.
			// A pipe along its top on its saddles (galvanised, as the duct).
			Tint(AddPiece(nullptr, Name + TEXT("Pipe"), CylinderMesh.Get(), PaintMaterial, Middle + FVector(0.0f, 0.0f, 178.0f) - Along * (Length * 0.5f - 20.0f),
				FVector(0.3f, 0.3f, (Length - 40.0f) / 100.0f), FRotator(-90.0f, Yaw, 0.0f), false), FLinearColor(0.3f, 0.32f, 0.35f));
			for (const float At : {-0.35f, 0.35f})
			{
				Tint(AddCube(FString::Printf(TEXT("%sSaddle%d"), *Name, At > 0.0f ? 1 : 0), FlatMaterial, Middle + Along * Length * At + FVector(0.0f, 0.0f, 165.0f),
					FVector(14.0f, 44.0f, 22.0f), Yaw, false), FLinearColor(0.12f, 0.12f, 0.13f));
			}
			UStaticMeshComponent* Neon = AddCube(Name + TEXT("Neon"), GlowMaterial, Middle + FVector(0.0f, 0.0f, 22.0f), FVector(Length - 30.0f, 94.0f, 5.0f), Yaw, false);
			Neon->SetCastShadow(false);
			Tint(Neon, Cyan, 2.4f);
		}
		// The duct ends against each end of the roof: a junction box.
		for (const float X : {-1.0f, 1.0f})
		{
			Tint(AddCube(FString::Printf(TEXT("DuctBox%s%s"), *Side, X < 0.0f ? TEXT("R") : TEXT("B")), Corrugated,
				FVector(X * 1450.0f, Y * 650.0f, 90.0f), FVector(200.0f, 200.0f, 180.0f), 0.0f, true, true), FLinearColor(0.3f, 0.33f, 0.38f));
		}
	}
	// The air units either side of the helipad (cover on the straight line between the starts), and vents on each half.
	for (const float X : {-1.0f, 1.0f})
	{
		const FString End = X < 0.0f ? TEXT("R") : TEXT("B");
		const FVector Unit(X * 760.0f, 0.0f, 0.0f);
		Tint(AddCube(TEXT("AirUnit") + End, Corrugated, Unit + FVector(0.0f, 0.0f, 70.0f), FVector(220.0f, 300.0f, 140.0f), 0.0f, true, true),
			FLinearColor(0.62f, 0.64f, 0.66f));
		for (const float Fan : {-1.0f, 1.0f})
		{
			Tint(AddDisc(FString::Printf(TEXT("AirFan%s%d"), *End, Fan > 0.0f ? 1 : 0), FlatMaterial, Unit + FVector(0.0f, Fan * 70.0f, 140.0f), 110.0f, 6.0f, false),
				FLinearColor(0.08f, 0.08f, 0.09f));
		}
		UStaticMeshComponent* Stripe = AddCube(TEXT("AirStripe") + End, GlowMaterial, Unit + FVector(-X * 112.0f, 0.0f, 40.0f), FVector(4.0f, 260.0f, 10.0f), 0.0f, false);
		Stripe->SetCastShadow(false);
		Tint(Stripe, Magenta, 2.6f);
		for (const float Y : {-1.0f, 1.0f})
		{
			const FString Name = FString::Printf(TEXT("Vent%s%s"), *End, Y < 0.0f ? TEXT("S") : TEXT("N"));
			const FVector At(X * 1250.0f, Y * 1500.0f, 0.0f);
			Tint(AddDisc(Name, Corrugated, At, 150.0f, 130.0f), FLinearColor(0.5f, 0.52f, 0.55f));
			Tint(AddDisc(Name + TEXT("Cap"), FlatMaterial, At + FVector(0.0f, 0.0f, 150.0f), 190.0f, 18.0f, false), FLinearColor(0.2f, 0.21f, 0.23f));
			Tint(AddDisc(Name + TEXT("Neck"), FlatMaterial, At + FVector(0.0f, 0.0f, 130.0f), 60.0f, 20.0f, false), FLinearColor(0.2f, 0.21f, 0.23f));
		}
	}
	// Hazard stripes where each lane meets the ends.
	for (const float X : {-1.0f, 1.0f})
	{
		for (int32 Stripe = 0; Stripe < 7; ++Stripe)
		{
			Tint(AddCube(FString::Printf(TEXT("Hazard%s%d"), X < 0.0f ? TEXT("R") : TEXT("B"), Stripe), PaintMaterial,
				FVector(X * (DuelHalf - 50.0f), -900.0f + Stripe * 300.0f, 0.6f), FVector(60.0f, 140.0f, 1.0f), 45.0f, false),
				Stripe % 2 ? FLinearColor(0.05f, 0.05f, 0.05f) : Amber);
		}
	}

	// ---- The parapet: low walls to look at with a neon rail, an unseen wall up to ball height.
	const FVector Corners[] = {{-DuelHalf, -DuelHalf, 0.0f}, {DuelHalf, -DuelHalf, 0.0f}, {DuelHalf, DuelHalf, 0.0f}, {-DuelHalf, DuelHalf, 0.0f}};
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const FVector From = Corners[Index];
		const FVector To = Corners[(Index + 1) % 4];
		const FVector Span = To - From;
		const FVector Out = ((From + To) * 0.5f).GetSafeNormal2D();
		Tint(AddCube(FString::Printf(TEXT("WallParapet%d"), Index), Concrete, (From + To) * 0.5f + Out * 40.0f + FVector(0.0f, 0.0f, 45.0f),
			FVector(Span.Size2D() + 160.0f, 80.0f, 90.0f), DuelYawOf(Span), true), FLinearColor(0.22f, 0.23f, 0.26f));
		AddInvisibleWall(FString::Printf(TEXT("ParapetWall%d"), Index), From, To, 400.0f);
		UStaticMeshComponent* Rail = AddCube(FString::Printf(TEXT("ParapetNeon%d"), Index), GlowMaterial, (From + To) * 0.5f + Out * 4.0f + FVector(0.0f, 0.0f, 92.0f),
			FVector(Span.Size2D(), 6.0f, 6.0f), DuelYawOf(Span), false);
		Rail->SetCastShadow(false);
		Tint(Rail, Index % 2 ? Magenta : Cyan, 2.6f);
	}
	// Floodlights in the corners.
	for (const float X : {-1.0f, 1.0f})
	{
		for (const float Y : {-1.0f, 1.0f})
		{
			const FString Name = FString::Printf(TEXT("Flood%s%s"), X < 0.0f ? TEXT("R") : TEXT("B"), Y < 0.0f ? TEXT("S") : TEXT("N"));
			const FVector At(X * (DuelHalf + 150.0f), Y * (DuelHalf + 150.0f), 0.0f);
			AddPiece(nullptr, Name, Flood, nullptr, At, FVector(1.0f), FRotator(0.0f, DuelYawOf(-At), 0.0f), false);
			SideLights.Add(DuelAddLight(this, SceneRoot, Name + TEXT("Light"), At * 0.8f + FVector(0.0f, 0.0f, 650.0f), FLinearColor(0.85f, 0.9f, 1.0f),
				4200.0f, 2800.0f));
		}
	}
	SideLights.Add(DuelAddLight(this, SceneRoot, TEXT("LightPad"), FVector(0.0f, 0.0f, 500.0f), FLinearColor(1.0f, 0.75f, 0.45f), 1800.0f, 1500.0f));
	for (const float Y : {-1.0f, 1.0f})
	{
		SideLights.Add(DuelAddLight(this, SceneRoot, Y < 0.0f ? TEXT("LightLaneS") : TEXT("LightLaneN"), FVector(0.0f, Y * 1350.0f, 400.0f), Cyan, 1400.0f, 1400.0f));
	}

	// ---- The city at night all round, its towers rising past the roof, a lit sign on each side, and stars.
	for (const float Y : {-1.0f, 1.0f})
	{
		const FString Side = Y < 0.0f ? TEXT("S") : TEXT("N");
		AddPiece(nullptr, TEXT("CityTower") + Side, TowerBody, nullptr, FVector(0.0f, Y * 3500.0f, -3200.0f), FVector(0.9f), FRotator(0.0f, 45.0f * Y, 0.0f), false);
		AddPiece(nullptr, TEXT("CityTowerLit") + Side, Tower, nullptr, FVector(0.0f, Y * 3500.0f, -3200.0f), FVector(0.9f), FRotator(0.0f, 45.0f * Y, 0.0f), false, false);
		for (const float X : {-1.0f, 1.0f})
		{
			const FString Name = FString::Printf(TEXT("CityGrid%s%s"), *Side, X < 0.0f ? TEXT("R") : TEXT("B"));
			AddPiece(nullptr, Name, GridBody, nullptr, FVector(X * 2700.0f, Y * 3000.0f, -3300.0f), FVector(0.8f), FRotator(0.0f, 90.0f * X, 0.0f), false);
			AddPiece(nullptr, Name + TEXT("Lit"), GridLit, nullptr, FVector(X * 2700.0f, Y * 3000.0f, -3300.0f), FVector(0.8f), FRotator(0.0f, 90.0f * X, 0.0f),
				false, false);
		}
	}
	for (const float X : {-1.0f, 1.0f})
	{
		// Behind each end, a lower block whose lit windows show below the roof's edge.
		AddPiece(nullptr, X < 0.0f ? TEXT("CityEndR") : TEXT("CityEndB"), GridBody, nullptr, FVector(X * 3300.0f, 0.0f, -4600.0f), FVector(0.85f),
			FRotator(0.0f, X < 0.0f ? 90.0f : -90.0f, 0.0f), false);
		AddPiece(nullptr, X < 0.0f ? TEXT("CityEndLitR") : TEXT("CityEndLitB"), GridLit, nullptr, FVector(X * 3300.0f, 0.0f, -4600.0f), FVector(0.85f),
			FRotator(0.0f, X < 0.0f ? 90.0f : -90.0f, 0.0f), false, false);
	}
	// Stars: the same pattern over both ends (mirrored), high in the dome.
	FRandomStream Stars(1116);
	for (int32 Index = 0; Index < 40; ++Index)
	{
		const float Yaw = Stars.FRandRange(-80.0f, 80.0f);
		const float Pitch = Stars.FRandRange(18.0f, 70.0f);
		const FVector Dir = FRotator(Pitch, Yaw, 0.0f).Vector();
		for (const float X : {-1.0f, 1.0f})
		{
			const FVector At = FVector(Dir.X * X, Dir.Y, Dir.Z) * 3900.0f;
			UStaticMeshComponent* Star = AddCube(FString::Printf(TEXT("Star%d%s"), Index, X < 0.0f ? TEXT("R") : TEXT("B")), GlowMaterial, At,
				FVector(10.0f + (Index % 3) * 6.0f), 0.0f, false);
			Star->SetCastShadow(false);
			Star->bHiddenInSceneCapture = true;
			Tint(Star, FLinearColor(0.85f, 0.9f, 1.0f), 4.0f);
		}
	}
	AddSkyDome(4300.0f);
}

AChaosImpactDuelStage::FDuelLook AChaosImpactDuelRooftopStage::GetArenaLook() const
{
	FDuelLook Look;
	Look.SunColor = FLinearColor(0.55f, 0.66f, 1.0f);
	Look.SunScale = 0.32f;
	Look.SkyLightScale = 0.45f;
	Look.FogColor = FLinearColor(0.09f, 0.07f, 0.16f, 1.0f);
	Look.FogStart = 9000.0f;
	return Look;
}

AChaosImpactDuelStage::FDuelSky AChaosImpactDuelRooftopStage::GetSky() const
{
	FDuelSky Sky;
	Sky.Zenith = FLinearColor(0.002f, 0.003f, 0.01f);
	Sky.Horizon = FLinearColor(0.045f, 0.022f, 0.065f);
	Sky.Ground = FLinearColor(0.01f, 0.008f, 0.015f);
	Sky.SunDir = FVector(0.0f, 1.0f, 0.4f);
	Sky.SunColor = FLinearColor(0.7f, 0.78f, 0.95f);
	Sky.SunSize = 0.18f;
	Sky.SunGlow = 0.08f;
	Sky.Brightness = 1.0f;
	Sky.Haze = 0.35f;
	return Sky;
}
