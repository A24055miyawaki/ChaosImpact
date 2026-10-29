#include "ChaosImpactWorldMap.h"
#include "Misc/CommandLine.h"
#include "ChaosImpactEase.h"

#include "ChaosImpactBallTypes.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactLoadingScreen.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactSettings.h"
#include "ChaosImpactSfx.h"
#include "ChaosImpactSoloStages.h"
#include "ChaosImpactWorldMapWidget.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "TextureResource.h"

namespace
{
	/** Flip if the board's faces turn out inside out (one-sided materials then show the faces from below only). */
	constexpr bool bMapFlipWinding = false;

	float MapSmooth(float X)
	{
		X = FMath::Clamp(X, 0.0f, 1.0f);
		return X * X * (3.0f - 2.0f * X);
	}

	FLinearColor MapScale(const FLinearColor& C, const float S)
	{
		return FLinearColor(FMath::Min(C.R * S, 1.0f), FMath::Min(C.G * S, 1.0f), FMath::Min(C.B * S, 1.0f), C.A);
	}

	double MapCross(const FVector2D& A, const FVector2D& B) { return A.X * B.Y - A.Y * B.X; }

	double MapArea(const TArray<FVector2D>& P)
	{
		double Sum = 0.0;
		for (int32 I = 0; I < P.Num(); ++I)
		{
			Sum += MapCross(P[I], P[(I + 1) % P.Num()]);
		}
		return Sum * 0.5;
	}

	/** Distance in the plane from P to segment AB, and the closest point. */
	double MapSegDist(const FVector2D& P, const FVector2D& A, const FVector2D& B, FVector2D& Closest)
	{
		const FVector2D AB = B - A;
		const double Len = AB.SizeSquared();
		const double T = Len > 1e-6 ? FMath::Clamp(FVector2D::DotProduct(P - A, AB) / Len, 0.0, 1.0) : 0.0;
		Closest = A + AB * T;
		return FVector2D::Distance(P, Closest);
	}

	bool MapInTri(const FVector2D& P, const FVector2D& A, const FVector2D& B, const FVector2D& C)
	{
		const double D1 = MapCross(B - A, P - A);
		const double D2 = MapCross(C - B, P - B);
		const double D3 = MapCross(A - C, P - C);
		return D1 >= 0.0 && D2 >= 0.0 && D3 >= 0.0;
	}

	/** Ear clipping of a counter-clockwise polygon. */
	void MapTriangulate(const TArray<FVector2D>& Poly, TArray<int32>& Out)
	{
		TArray<int32> Left;
		for (int32 I = 0; I < Poly.Num(); ++I)
		{
			Left.Add(I);
		}
		int32 Guard = 0;
		while (Left.Num() > 3 && Guard++ < 20000)
		{
			bool bCut = false;
			const int32 Count = Left.Num();
			for (int32 I = 0; I < Count; ++I)
			{
				const int32 A = Left[(I + Count - 1) % Count];
				const int32 B = Left[I];
				const int32 C = Left[(I + 1) % Count];
				if (MapCross(Poly[B] - Poly[A], Poly[C] - Poly[B]) <= 1e-6)
				{
					continue;
				}
				bool bInside = false;
				for (const int32 J : Left)
				{
					if (J != A && J != B && J != C && MapInTri(Poly[J], Poly[A], Poly[B], Poly[C]))
					{
						bInside = true;
						break;
					}
				}
				if (bInside)
				{
					continue;
				}
				Out.Append({A, B, C});
				Left.RemoveAt(I);
				bCut = true;
				break;
			}
			if (!bCut)
			{
				break;
			}
		}
		for (int32 I = 1; I + 1 < Left.Num(); ++I)
		{
			Out.Append({Left[0], Left[I], Left[I + 1]});
		}
	}

	/** The polygon moved inward by Amount (counter-clockwise polygons). */
	TArray<FVector2D> MapInset(const TArray<FVector2D>& Poly, const float Amount)
	{
		TArray<FVector2D> Out;
		const int32 Count = Poly.Num();
		for (int32 I = 0; I < Count; ++I)
		{
			const FVector2D Prev = Poly[(I + Count - 1) % Count];
			const FVector2D Here = Poly[I];
			const FVector2D Next = Poly[(I + 1) % Count];
			const FVector2D D1 = (Here - Prev).GetSafeNormal();
			const FVector2D D2 = (Next - Here).GetSafeNormal();
			// Outward normals of a counter-clockwise polygon point right of each edge.
			const FVector2D N1(D1.Y, -D1.X);
			const FVector2D N2(D2.Y, -D2.X);
			const FVector2D Mid = (N1 + N2).GetSafeNormal();
			const double Cos = FMath::Max(FVector2D::DotProduct(Mid, N1), 0.4);
			Out.Add(Here - Mid * (Amount / Cos));
		}
		return Out;
	}

	/** Triangles with flat normals and vertex colours for a procedural mesh. */
	struct FMapMesh
	{
		TArray<FVector> V;
		TArray<int32> T;
		TArray<FVector> N;
		TArray<FVector2D> UV;
		TArray<FLinearColor> C;

		void Tri(const FVector& A, FVector B, FVector Cc, const FVector& Normal, const FLinearColor& Ca, FLinearColor Cb, FLinearColor Ccol)
		{
			// The engine draws a face whose corners (A, B, C) give a cross product pointing away from the viewer.
			bool bFront = FVector::DotProduct(FVector::CrossProduct(B - A, Cc - A), Normal) < 0.0;
			if (bMapFlipWinding)
			{
				bFront = !bFront;
			}
			if (!bFront)
			{
				Swap(B, Cc);
				Swap(Cb, Ccol);
			}
			const int32 Base = V.Num();
			const FVector Points[] = {A, B, Cc};
			const FLinearColor Colors[] = {Ca, Cb, Ccol};
			for (int32 K = 0; K < 3; ++K)
			{
				V.Add(Points[K]);
				N.Add(Normal);
				UV.Add(FVector2D(Points[K].X / 200.0, Points[K].Y / 200.0));
				C.Add(Colors[K]);
			}
			T.Append({Base, Base + 1, Base + 2});
		}
		void Tri(const FVector& A, const FVector& B, const FVector& Cc, const FVector& Normal, const FLinearColor& Col)
		{
			Tri(A, B, Cc, Normal, Col, Col, Col);
		}
		void Quad(const FVector& A, const FVector& B, const FVector& Cc, const FVector& D, const FVector& Normal,
			const FLinearColor& Ca, const FLinearColor& Cb, const FLinearColor& Ccol, const FLinearColor& Cd)
		{
			Tri(A, B, Cc, Normal, Ca, Cb, Ccol);
			Tri(A, Cc, D, Normal, Ca, Ccol, Cd);
		}
		void Quad(const FVector& A, const FVector& B, const FVector& Cc, const FVector& D, const FVector& Normal, const FLinearColor& Col)
		{
			Quad(A, B, Cc, D, Normal, Col, Col, Col, Col);
		}
	};

	/** A point a distance along a polyline, and its direction there. */
	FVector MapAlong(const TArray<FVector>& Points, double Distance, FVector& OutDirection)
	{
		for (int32 I = 0; I + 1 < Points.Num(); ++I)
		{
			const double Len = FVector::Dist2D(Points[I], Points[I + 1]);
			OutDirection = (Points[I + 1] - Points[I]).GetSafeNormal2D();
			if (Distance <= Len || I + 2 == Points.Num())
			{
				return Points[I] + OutDirection * FMath::Min(Distance, Len);
			}
			Distance -= Len;
		}
		OutDirection = FVector::ForwardVector;
		return Points.Num() ? Points.Last() : FVector::ZeroVector;
	}

	double MapLength(const TArray<FVector>& Points)
	{
		double Sum = 0.0;
		for (int32 I = 0; I + 1 < Points.Num(); ++I)
		{
			Sum += FVector::Dist2D(Points[I], Points[I + 1]);
		}
		return Sum;
	}

	/** A strip along a polyline (flat top, short sides), from Start to End along it. */
	void MapStrip(FMapMesh& M, const TArray<FVector>& Points, const double Start, const double End, const float Width,
		const float Top, const float Thick, const FLinearColor& Color)
	{
		const int32 Steps = FMath::Max(1, FMath::CeilToInt((End - Start) / 60.0));
		const FLinearColor SideColor = MapScale(Color, 0.6f);
		FVector PrevL, PrevR;
		for (int32 S = 0; S <= Steps; ++S)
		{
			FVector Dir;
			const FVector P = MapAlong(Points, FMath::Lerp(Start, End, static_cast<double>(S) / Steps), Dir);
			const FVector Side(-Dir.Y, Dir.X, 0.0);
			const FVector L = FVector(P.X, P.Y, Top) - Side * Width * 0.5f;
			const FVector R = FVector(P.X, P.Y, Top) + Side * Width * 0.5f;
			if (S > 0)
			{
				M.Quad(PrevL, PrevR, R, L, FVector::UpVector, Color);
				M.Quad(PrevL, L, L - FVector(0, 0, Thick), PrevL - FVector(0, 0, Thick), -Side, SideColor);
				M.Quad(PrevR, PrevR - FVector(0, 0, Thick), R - FVector(0, 0, Thick), R, Side, SideColor);
			}
			PrevL = L;
			PrevR = R;
		}
	}

	struct FMapLayoutNode { int32 Id; float X; float Y; };
	struct FMapLayoutEdge { int32 From; int32 To; float Bend; };

	// The camera looks along +X from behind and above, so the journey runs along +X: map 1 splits after the first stage
	// (the sea on the left, the forest on the right), meets again at the cloud path, and ends at the temple's gate.
	const FMapLayoutNode Map1Nodes[] = {
		{1, 0, 0},
		{2, 1200, -1150}, {3, 2300, -1850}, {4, 3500, -2200}, {5, 4700, -1850}, {6, 5800, -2350}, {7, 6900, -1600},
		{8, 1200, 1150}, {9, 2300, 1850}, {10, 3500, 2250}, {11, 4700, 1850}, {12, 5800, 2350}, {13, 6900, 1600},
		{14, 8100, 0}, {15, 9400, 0}, {ChaosImpactSolo::GateToMap2, 10750, 0}};
	const FMapLayoutEdge Map1Edges[] = {
		{1, 2, 180}, {2, 3, -160}, {3, 4, 150}, {4, 5, -150}, {5, 6, 160}, {6, 7, -180}, {7, 14, 220},
		{1, 8, -180}, {8, 9, 160}, {9, 10, -150}, {10, 11, 150}, {11, 12, -160}, {12, 13, 180}, {13, 14, -220},
		{14, 15, 0}, {15, ChaosImpactSolo::GateToMap2, 0}};
	const FMapLayoutNode Map2Nodes[] = {
		{ChaosImpactSolo::GateToMap1, 0, 0},
		{16, 1200, -1200}, {17, 2300, -1900}, {18, 3500, -2250}, {19, 4700, -1900}, {20, 5800, -2400}, {21, 6900, -1650},
		{22, 1200, 1200}, {23, 2300, 1900}, {24, 3500, 2250}, {25, 4700, 1900}, {26, 5800, 2400}, {27, 6900, 1650},
		{28, 8100, 0}, {29, 9300, 0}, {30, 10700, 0}};
	const FMapLayoutEdge Map2Edges[] = {
		{ChaosImpactSolo::GateToMap1, 16, 180}, {16, 17, -160}, {17, 18, 150}, {18, 19, -150}, {19, 20, 160}, {20, 21, -180},
		{21, 28, 220}, {ChaosImpactSolo::GateToMap1, 22, -180}, {22, 23, 160}, {23, 24, -150}, {24, 25, 150}, {25, 26, -160},
		{26, 27, 180}, {27, 28, -220}, {28, 29, 0}, {29, 30, 0}};

	constexpr float PedestalTop = 38.0f;
	constexpr float PathTop = 10.0f;
}

AChaosImpactWorldMap::AChaosImpactWorldMap()
{
	PrimaryActorTick.bCanEverTick = true;
	// After the player has moved: the paths hold them, then the camera follows.
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

AChaosImpactWorldMap* AChaosImpactWorldMap::Find(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AChaosImpactWorldMap> It(const_cast<UWorld*>(World)); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

void AChaosImpactWorldMap::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	// A preview in the editor (the game builds it at the start of play).
	if (GetWorld() && !GetWorld()->IsGameWorld())
	{
		Build();
	}
}

void AChaosImpactWorldMap::BeginPlay()
{
	Super::BeginPlay();
	Progress = UChaosImpactSoloProgress::Load();
	Build();
	ApplyProgress(true);
	// Everything here already cleared: no fog at all.
	FullClear = bComplete ? 1.0f : 0.0f;
	if (RevealQueue.Num())
	{
		RevealStep = ERevealStep::Wait;
		RevealTime = 0.0f;
		RevealStartedAt = FPlatformTime::Seconds();
	}
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		Widget = CreateWidget<UChaosImpactWorldMapWidget>(PC, UChaosImpactWorldMapWidget::StaticClass());
		if (Widget)
		{
			Widget->SetMap(this);
			Widget->AddToViewport(10);
		}
	}
}

void AChaosImpactWorldMap::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Widget)
	{
		Widget->RemoveFromParent();
		Widget = nullptr;
	}
	if (MapCamera)
	{
		MapCamera->Destroy();
		MapCamera = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

// =====================================================================================================================
// Building

void AChaosImpactWorldMap::LoadAssets()
{
	CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	CylinderMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	ConeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
	PlaneMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	const TCHAR* Root = TEXT("/Game/ChaosImpact/solo/WorldMap/Materials/");
	LitMaterial = LoadObject<UMaterialInterface>(nullptr, *(FString(Root) + TEXT("M_CI_MapLit.M_CI_MapLit")));
	GlowMaterial = LoadObject<UMaterialInterface>(nullptr, *(FString(Root) + TEXT("M_CI_MapGlow.M_CI_MapGlow")));
	WaterMaterial = LoadObject<UMaterialInterface>(nullptr, *(FString(Root) + TEXT("M_CI_MapWater.M_CI_MapWater")));
	FogMaterial = LoadObject<UMaterialInterface>(nullptr, *(FString(Root) + TEXT("M_CI_MapFog.M_CI_MapFog")));
	CloudSeaMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/ChaosImpact/solo/Stage2/Materials/M_CI_CloudSea.M_CI_CloudSea"));
	TextMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/DefaultTextMaterialOpaque.DefaultTextMaterialOpaque"));
}

void AChaosImpactWorldMap::ClearBuilt()
{
	for (USceneComponent* Part : Built)
	{
		if (IsValid(Part))
		{
			Part->DestroyComponent();
		}
	}
	Built.Reset();
	MidCache.Reset();
	FogMaterials.Reset();
	FogSheets.Reset();
	Birds.Reset();
	BirdGround.Reset();
	Nodes.Reset();
	Edges.Reset();
}

void AChaosImpactWorldMap::MakeLayout()
{
	TArrayView<const FMapLayoutNode> NodeSource = MapIndex == 2 ? MakeArrayView(Map2Nodes) : MakeArrayView(Map1Nodes);
	TArrayView<const FMapLayoutEdge> EdgeSource = MapIndex == 2 ? MakeArrayView(Map2Edges) : MakeArrayView(Map1Edges);
	for (const FMapLayoutNode& Source : NodeSource)
	{
		FNode& Node = Nodes.AddDefaulted_GetRef();
		Node.Id = Source.Id;
		Node.At = FVector2D(Source.X, Source.Y);
		Node.Spin = FMath::Frac(Source.Id * 0.618f) * 360.0f;
	}
	for (const FMapLayoutEdge& Source : EdgeSource)
	{
		const int32 A = NodeIndex(Source.From);
		const int32 B = NodeIndex(Source.To);
		if (A == INDEX_NONE || B == INDEX_NONE)
		{
			continue;
		}
		FEdge& Edge = Edges.AddDefaulted_GetRef();
		Edge.From = Source.From;
		Edge.To = Source.To;
		Edge.Points = CurveBetween(Nodes[A].At, Nodes[B].At, Source.Bend);
	}
	BoardBounds = FBox2D(ForceInit);
	for (const FNode& Node : Nodes)
	{
		BoardBounds += Node.At;
	}
	NodeBounds = BoardBounds;
	BoardBounds = BoardBounds.ExpandBy(FVector2D(2600.0, 2600.0));
}

TArray<FVector> AChaosImpactWorldMap::CurveBetween(const FVector2D& A, const FVector2D& B, const float Bend) const
{
	const FVector2D Dir = (B - A).GetSafeNormal();
	const FVector2D Side(-Dir.Y, Dir.X);
	const FVector2D Control = (A + B) * 0.5 + Side * Bend * 2.0f;
	const int32 Steps = FMath::Clamp(FMath::CeilToInt(FVector2D::Distance(A, B) / 110.0f), 4, 40);
	TArray<FVector> Points;
	for (int32 S = 0; S <= Steps; ++S)
	{
		const float T = static_cast<float>(S) / Steps;
		const FVector2D P = A * FMath::Square(1.0f - T) + Control * (2.0f * T * (1.0f - T)) + B * FMath::Square(T);
		Points.Add(FVector(P.X, P.Y, 0.0));
	}
	return Points;
}

void AChaosImpactWorldMap::Build()
{
	ClearBuilt();
	LoadAssets();
	if (!CubeMesh || !LitMaterial || !GlowMaterial)
	{
		UE_LOG(LogTemp, Warning, TEXT("World map: its meshes or materials are missing"));
		return;
	}
	MakeLayout();
	BuildBoard();
	BuildPaths();
	BuildNodes();
	if (MapIndex == 2)
	{
		BuildScenery2();
	}
	else
	{
		BuildScenery1();
	}
	BuildFog();
}

UMaterialInstanceDynamic* AChaosImpactWorldMap::Mid(UMaterialInterface* Base, const FLinearColor& Color, const float Extra)
{
	if (!Base)
	{
		return nullptr;
	}
	const FString Key = FString::Printf(TEXT("%s_%.3f_%.3f_%.3f_%.2f"), *Base->GetName(), Color.R, Color.G, Color.B, Extra);
	if (TObjectPtr<UMaterialInstanceDynamic>* Found = MidCache.Find(Key))
	{
		return *Found;
	}
	UMaterialInstanceDynamic* Made = UMaterialInstanceDynamic::Create(Base, this);
	Made->SetVectorParameterValue(TEXT("Color"), Color);
	if (Extra >= 0.0f)
	{
		Made->SetScalarParameterValue(Base == GlowMaterial ? TEXT("Intensity") : TEXT("Emission"), Extra);
	}
	MidCache.Add(Key, Made);
	return Made;
}

void AChaosImpactWorldMap::Own(USceneComponent* Part, const int32 Node)
{
	Built.Add(Part);
	if (Node != 0)
	{
		const int32 Index = NodeIndex(Node);
		if (Nodes.IsValidIndex(Index))
		{
			Nodes[Index].Parts.Add(Part);
		}
	}
}

UStaticMeshComponent* AChaosImpactWorldMap::AddMesh(UStaticMesh* Mesh, const FVector& Location, const FRotator& Rotation,
	const FVector& Scale, UMaterialInterface* Material, const int32 Node, const bool bCollide)
{
	UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
	Part->SetupAttachment(RootComponent);
	Part->SetStaticMesh(Mesh);
	Part->SetRelativeLocationAndRotation(Location, Rotation);
	Part->SetRelativeScale3D(Scale);
	if (Material)
	{
		for (int32 Slot = 0; Slot < Part->GetNumMaterials(); ++Slot)
		{
			Part->SetMaterial(Slot, Material);
		}
	}
	Part->SetCollisionProfileName(bCollide ? UCollisionProfile::BlockAll_ProfileName : UCollisionProfile::NoCollision_ProfileName);
	Part->SetCanEverAffectNavigation(false);
	Part->RegisterComponent();
	Own(Part, Node);
	return Part;
}

UStaticMeshComponent* AChaosImpactWorldMap::AddBlock(const FVector& Center, const FVector& Size, const FLinearColor& Color,
	const int32 Node, const float Yaw)
{
	return AddMesh(CubeMesh, Center, FRotator(0.0f, Yaw, 0.0f), Size / 100.0f, Mid(LitMaterial, Color), Node);
}

UStaticMeshComponent* AChaosImpactWorldMap::AddShape(UStaticMesh* Mesh, const FVector& Location, const FVector& Scale,
	const FLinearColor& Color, const int32 Node, const FRotator& Rotation, const bool bGlow, const float GlowIntensity)
{
	return AddMesh(Mesh, Location, Rotation, Scale, bGlow ? Mid(GlowMaterial, Color, GlowIntensity) : Mid(LitMaterial, Color), Node);
}

UProceduralMeshComponent* AChaosImpactWorldMap::AddProc(const FString& Name, const int32 Node, UMaterialInterface* Material,
	const bool bCollide)
{
	UProceduralMeshComponent* Part = NewObject<UProceduralMeshComponent>(this, NAME_None, RF_Transient);
	Part->SetupAttachment(RootComponent);
	Part->bUseComplexAsSimpleCollision = true;
	Part->SetCollisionProfileName(bCollide ? UCollisionProfile::BlockAll_ProfileName : UCollisionProfile::NoCollision_ProfileName);
	Part->SetCanEverAffectNavigation(false);
	Part->RegisterComponent();
	if (Material)
	{
		Part->SetMaterial(0, Material);
	}
	Own(Part, Node);
	return Part;
}

UProceduralMeshComponent* AChaosImpactWorldMap::AddSlab(const TArray<FVector2D>& Outline, const float Top, const float Thickness,
	const float Bevel, const FLinearColor& TopColor, const FLinearColor& SideColor, const int32 Node, const bool bCollide,
	UMaterialInterface* Material)
{
	TArray<FVector2D> Outer = Outline;
	if (MapArea(Outer) < 0.0)
	{
		Algo::Reverse(Outer);
	}
	const TArray<FVector2D> Inner = Bevel > 0.0f ? MapInset(Outer, Bevel) : Outer;
	FMapMesh M;
	TArray<int32> Tris;
	MapTriangulate(Inner, Tris);
	for (int32 I = 0; I + 2 < Tris.Num(); I += 3)
	{
		M.Tri(FVector(Inner[Tris[I]], Top), FVector(Inner[Tris[I + 1]], Top), FVector(Inner[Tris[I + 2]], Top), FVector::UpVector, TopColor);
	}
	const float Shoulder = Top - Bevel * 0.7f;
	const float Bottom = Top - Thickness;
	const FLinearColor Rim = MapScale(TopColor, 1.18f);
	const FLinearColor Low = MapScale(SideColor, 0.55f);
	for (int32 I = 0; I < Outer.Num(); ++I)
	{
		const int32 J = (I + 1) % Outer.Num();
		const FVector2D Along = (Outer[J] - Outer[I]).GetSafeNormal();
		const FVector Out(Along.Y, -Along.X, 0.0);
		if (Bevel > 0.0f)
		{
			M.Quad(FVector(Inner[I], Top), FVector(Inner[J], Top), FVector(Outer[J], Shoulder), FVector(Outer[I], Shoulder),
				(Out * 0.55 + FVector::UpVector * 0.85).GetSafeNormal(), Rim);
		}
		M.Quad(FVector(Outer[I], Shoulder), FVector(Outer[J], Shoulder), FVector(Outer[J], Bottom), FVector(Outer[I], Bottom), Out,
			SideColor, SideColor, Low, Low);
	}
	UProceduralMeshComponent* Part = AddProc(TEXT("Slab"), Node, Material ? Material : Mid(LitMaterial, FLinearColor::White), bCollide);
	Part->CreateMeshSection_LinearColor(0, M.V, M.T, M.N, M.UV, M.C, TArray<FProcMeshTangent>(), bCollide);
	return Part;
}

UTextRenderComponent* AChaosImpactWorldMap::AddText(const FString& Text, const FVector& Location, const FRotator& Rotation,
	const float Size, const FLinearColor& Color, const int32 Node)
{
	UTextRenderComponent* Part = NewObject<UTextRenderComponent>(this, NAME_None, RF_Transient);
	Part->SetupAttachment(RootComponent);
	Part->SetText(FText::FromString(Text));
	Part->SetWorldSize(Size);
	Part->SetHorizontalAlignment(EHTA_Center);
	Part->SetVerticalAlignment(EVRTA_TextCenter);
	Part->SetTextRenderColor(Color.ToFColor(true));
	if (TextMaterial)
	{
		Part->SetTextMaterial(TextMaterial);
	}
	Part->SetRelativeLocationAndRotation(Location, Rotation);
	Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Part->RegisterComponent();
	Own(Part, Node);
	return Part;
}

UPointLightComponent* AChaosImpactWorldMap::AddLight(const FVector& Location, const FLinearColor& Color, const float Intensity,
	const float Radius, const int32 Node)
{
	UPointLightComponent* Part = NewObject<UPointLightComponent>(this, NAME_None, RF_Transient);
	Part->SetupAttachment(RootComponent);
	Part->SetMobility(EComponentMobility::Movable);
	Part->SetRelativeLocation(Location);
	Part->SetIntensityUnits(ELightUnits::Candelas);
	Part->SetIntensity(Intensity);
	Part->SetLightColor(Color);
	Part->SetAttenuationRadius(Radius);
	Part->SetCastShadows(false);
	Part->SetSourceRadius(30.0f);
	Part->RegisterComponent();
	Own(Part, Node);
	return Part;
}

TArray<FVector2D> AChaosImpactWorldMap::Blob(const FVector2D& Center, const FVector2D& Radius, const int32 Points, const float Jitter,
	const float Seed)
{
	TArray<FVector2D> Out;
	for (int32 I = 0; I < Points; ++I)
	{
		const float Angle = 2.0f * PI * I / Points;
		const float Noise = FMath::Frac(FMath::Sin((I + 1) * 12.9898f + Seed * 78.233f) * 43758.5453f);
		const float Scale = 1.0f - Jitter * Noise;
		Out.Add(Center + FVector2D(FMath::Cos(Angle) * Radius.X, FMath::Sin(Angle) * Radius.Y) * Scale);
	}
	return Out;
}

TArray<FVector2D> AChaosImpactWorldMap::HullAround(const TArray<FVector2D>& Points, const float Radius, const float Jitter, const float Seed)
{
	TArray<FVector2D> Cloud;
	for (const FVector2D& P : Points)
	{
		for (int32 I = 0; I < 24; ++I)
		{
			const float Angle = 2.0f * PI * I / 24;
			Cloud.Add(P + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
		}
	}
	Cloud.Sort([](const FVector2D& A, const FVector2D& B) { return A.X < B.X || (A.X == B.X && A.Y < B.Y); });
	TArray<FVector2D> Hull;
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		const int32 Start = Hull.Num();
		for (int32 K = 0; K < Cloud.Num(); ++K)
		{
			const FVector2D& P = Pass == 0 ? Cloud[K] : Cloud[Cloud.Num() - 1 - K];
			while (Hull.Num() >= Start + 2 && MapCross(Hull.Last() - Hull[Hull.Num() - 2], P - Hull.Last()) <= 0.0)
			{
				Hull.Pop();
			}
			Hull.Add(P);
		}
		Hull.Pop();
	}
	// Even edges, then a broken edge (like the logo's shards).
	TArray<FVector2D> Out;
	for (int32 I = 0; I < Hull.Num(); ++I)
	{
		const FVector2D A = Hull[I];
		const FVector2D B = Hull[(I + 1) % Hull.Num()];
		const int32 Steps = FMath::Max(1, FMath::CeilToInt(FVector2D::Distance(A, B) / 240.0f));
		for (int32 S = 0; S < Steps; ++S)
		{
			const FVector2D P = FMath::Lerp(A, B, static_cast<double>(S) / Steps);
			const int32 Index = Out.Num();
			const float Noise = FMath::Frac(FMath::Sin((Index + 1) * 12.9898f + Seed * 78.233f) * 43758.5453f);
			const FVector2D Along = (B - A).GetSafeNormal();
			const FVector2D Outward(Along.Y, -Along.X);
			Out.Add(P + Outward * (Noise - 0.5f) * Jitter * 2.0f);
		}
	}
	return Out;
}

void AChaosImpactWorldMap::BuildBoard()
{
	const bool bDark = MapIndex == 2;
	TArray<FVector2D> All;
	for (const FNode& Node : Nodes)
	{
		All.Add(Node.At);
	}
	// The board: a thick slab whose broken edge drops away to the clouds (map 1) or the dark (map 2).
	const FLinearColor BoardTop = bDark ? FLinearColor(0.035f, 0.028f, 0.05f) : FLinearColor(0.72f, 0.62f, 0.44f);
	const FLinearColor BoardSide = bDark ? FLinearColor(0.02f, 0.012f, 0.03f) : FLinearColor(0.3f, 0.18f, 0.1f);
	AddSlab(HullAround(All, 1900.0f, 90.0f, 3.0f), 0.0f, 320.0f, 30.0f, BoardTop, BoardSide, 0, true);
	// A second, lower layer under the edge (rock under the paint), and pieces breaking away below.
	AddSlab(HullAround(All, 1750.0f, 140.0f, 9.0f), -320.0f, 260.0f, 0.0f, MapScale(BoardSide, 0.8f), MapScale(BoardSide, 0.6f));
	FRandomStream Random(MapIndex * 31 + 7);
	for (int32 I = 0; I < 18; ++I)
	{
		const float Angle = Random.FRandRange(0.0f, 2.0f * PI);
		const FVector2D Center = BoardBounds.GetCenter() + FVector2D(FMath::Cos(Angle) * BoardBounds.GetExtent().X * 1.05f,
			FMath::Sin(Angle) * BoardBounds.GetExtent().Y * 1.15f);
		const float Size = Random.FRandRange(140.0f, 420.0f);
		const float Height = Random.FRandRange(-1300.0f, -250.0f);
		AddSlab(Blob(Center, FVector2D(Size, Size * 0.8f), 9, 0.35f, I + MapIndex * 10.0f), Height, Size * 0.7f, 10.0f,
			MapScale(BoardTop, 0.85f), BoardSide);
	}

	// The regions painted on the board: each its own plate (a shard), a little raised.
	struct FRegion { TArray<int32> Ids; float Radius; FLinearColor Color; };
	TArray<FRegion> Regions;
	if (bDark)
	{
		Regions = {
			{{ChaosImpactSolo::GateToMap1}, 1100.0f, FLinearColor(0.07f, 0.035f, 0.1f)},
			{{16, 17, 18, 19, 20, 21}, 900.0f, FLinearColor(0.1f, 0.16f, 0.27f)},
			{{22, 23, 24, 25, 26, 27}, 900.0f, FLinearColor(0.16f, 0.045f, 0.035f)},
			{{28, 29, 30}, 1000.0f, FLinearColor(0.1f, 0.09f, 0.11f)}};
	}
	else
	{
		Regions = {
			{{1}, 1150.0f, FLinearColor(0.07f, 0.065f, 0.15f)},
			{{2, 3, 4, 5, 6, 7}, 700.0f, FLinearColor(0.86f, 0.74f, 0.5f)},
			{{8, 9, 10, 11, 12, 13}, 950.0f, FLinearColor(0.86f, 0.9f, 0.97f)},
			{{14, 15, ChaosImpactSolo::GateToMap2}, 1000.0f, FLinearColor(0.84f, 0.84f, 0.93f)}};
	}
	for (int32 R = 0; R < Regions.Num(); ++R)
	{
		TArray<FVector2D> Points;
		for (const int32 Id : Regions[R].Ids)
		{
			Points.Add(Nodes[NodeIndex(Id)].At);
		}
		AddSlab(HullAround(Points, Regions[R].Radius, 45.0f, 20.0f + R), 4.0f + R * 0.8f, 30.0f, 6.0f, Regions[R].Color,
			MapScale(Regions[R].Color, 0.6f), 0, true);
	}

	// Far below: a sea of clouds (map 1), or a dark churning void (map 2).
	if (CloudSeaMaterial && PlaneMesh)
	{
		UMaterialInstanceDynamic* Sea = UMaterialInstanceDynamic::Create(CloudSeaMaterial, this);
		Sea->SetVectorParameterValue(TEXT("Hollow"), bDark ? FLinearColor(0.02f, 0.0f, 0.035f) : FLinearColor(0.5f, 0.62f, 0.88f));
		Sea->SetVectorParameterValue(TEXT("Crest"), bDark ? FLinearColor(0.32f, 0.04f, 0.12f) : FLinearColor(1.0f, 0.98f, 0.95f));
		Sea->SetScalarParameterValue(TEXT("Intensity"), bDark ? 0.9f : 1.25f);
		AddMesh(PlaneMesh, FVector(BoardBounds.GetCenter(), -1800.0f), FRotator::ZeroRotator, FVector(600.0f, 600.0f, 1.0f), Sea);
	}
}

void AChaosImpactWorldMap::BuildPaths()
{
	const bool bDark = MapIndex == 2;
	const FLinearColor Border = bDark ? FLinearColor(0.012f, 0.008f, 0.02f) : FLinearColor(0.12f, 0.08f, 0.06f);
	const FLinearColor Dash = bDark ? FLinearColor(1.0f, 0.18f, 0.25f) : FLinearColor(1.0f, 0.82f, 0.35f);
	for (FEdge& Edge : Edges)
	{
		const double Length = MapLength(Edge.Points);
		// A dark road edged on the board, dashes of light along it (from pedestal to pedestal).
		FMapMesh Road;
		MapStrip(Road, Edge.Points, 0.0, Length, 240.0f, PathTop - 1.0f, 6.0f, Border);
		UProceduralMeshComponent* RoadPart = AddProc(TEXT("Road"), 0, Mid(LitMaterial, FLinearColor::White), false);
		RoadPart->CreateMeshSection_LinearColor(0, Road.V, Road.T, Road.N, Road.UV, Road.C, TArray<FProcMeshTangent>(), false);
		Edge.Parts.Add(RoadPart);
		FMapMesh Dashes;
		constexpr double On = 70.0;
		constexpr double Off = 55.0;
		for (double At = NodeRadius; At + On < Length - NodeRadius; At += On + Off)
		{
			MapStrip(Dashes, Edge.Points, At, At + On, 70.0f, PathTop + 0.6f, 2.0f, FLinearColor::White);
		}
		if (Dashes.V.Num())
		{
			UProceduralMeshComponent* DashPart = AddProc(TEXT("Dashes"), 0, Mid(GlowMaterial, Dash, bDark ? 6.0f : 3.0f), false);
			DashPart->CreateMeshSection_LinearColor(0, Dashes.V, Dashes.T, Dashes.N, Dashes.UV, Dashes.C, TArray<FProcMeshTangent>(), false);
			Edge.Parts.Add(DashPart);
		}
	}
}

void AChaosImpactWorldMap::BuildNodes()
{
	const bool bDark = MapIndex == 2;
	for (FNode& Node : Nodes)
	{
		const FChaosImpactSoloStageInfo* Info = ChaosImpactSolo::FindStage(Node.Id);
		const EChaosImpactSoloStageKind Kind = Info ? Info->Kind : EChaosImpactSoloStageKind::Normal;
		const bool bGate = Kind == EChaosImpactSoloStageKind::Gate;
		const bool bBig = Kind == EChaosImpactSoloStageKind::Boss || Kind == EChaosImpactSoloStageKind::Story;
		const float Radius = NodeRadius;
		const FVector At(Node.At, 0.0f);
		// The pedestal: a stepped round stone, its top coloured by the stage's state (see ApplyProgress).
		const FLinearColor Stone = bDark ? FLinearColor(0.06f, 0.05f, 0.08f) : FLinearColor(0.32f, 0.27f, 0.24f);
		AddSlab(Blob(Node.At, FVector2D(Radius + 40.0f, Radius + 40.0f), 40, 0.0f, 0.0f), 16.0f, 30.0f, 8.0f,
			MapScale(Stone, 0.8f), MapScale(Stone, 0.6f), Node.Id, true);
		Node.Top = AddSlab(Blob(Node.At, FVector2D(Radius, Radius), 40, 0.0f, 0.0f), PedestalTop, 26.0f, 10.0f, FLinearColor::White,
			MapScale(Stone, 0.9f), Node.Id, true, UMaterialInstanceDynamic::Create(LitMaterial, this));
		// A ring of light round its top.
		FMapMesh Ring;
		for (int32 I = 0; I < 48; ++I)
		{
			const float A0 = 2.0f * PI * I / 48;
			const float A1 = 2.0f * PI * (I + 1) / 48;
			const FVector2D D0(FMath::Cos(A0), FMath::Sin(A0));
			const FVector2D D1(FMath::Cos(A1), FMath::Sin(A1));
			const float Z = PedestalTop + 0.8f;
			Ring.Quad(FVector(Node.At + D0 * (Radius - 34.0f), Z), FVector(Node.At + D1 * (Radius - 34.0f), Z),
				FVector(Node.At + D1 * (Radius - 14.0f), Z), FVector(Node.At + D0 * (Radius - 14.0f), Z), FVector::UpVector, FLinearColor::White);
		}
		Node.Ring = AddProc(TEXT("Ring"), Node.Id, UMaterialInstanceDynamic::Create(GlowMaterial, this), false);
		Node.Ring->CreateMeshSection_LinearColor(0, Ring.V, Ring.T, Ring.N, Ring.UV, Ring.C, TArray<FProcMeshTangent>(), false);
		// Its number, lying on top (read from the camera behind and above).
		if (!bGate)
		{
			Node.Number = AddText(FString::FromInt(Node.Id), At + FVector(-60.0f, 0.0f, PedestalTop + 1.2f), FRotator(90.0f, 180.0f, 0.0f),
				110.0f, FLinearColor::White, Node.Id);
		}
		// A crystal floating over it: lit while the stage waits to be played.
		const float CrystalSize = bBig ? 0.9f : 0.62f;
		Node.Crystal = AddMesh(CubeMesh, At + FVector(0.0f, 0.0f, 300.0f), FRotator(45.0f, 0.0f, 35.26f),
			FVector(CrystalSize, CrystalSize, CrystalSize * 1.4f), UMaterialInstanceDynamic::Create(GlowMaterial, this), Node.Id);
		Node.Light = AddLight(At + FVector(0.0f, 0.0f, 320.0f), FLinearColor::White, 40.0f, 900.0f, Node.Id);
		// A flag at its side: the United States' (red, a black ball) until it is cleared, then ours (blue, a gold ball).
		if (!bGate)
		{
			const FVector Pole = At + FVector(-90.0f, -Radius - 30.0f, 0.0f);
			AddShape(CylinderMesh, Pole + FVector(0.0f, 0.0f, 140.0f), FVector(0.07f, 0.07f, 2.8f), FLinearColor(0.75f, 0.75f, 0.78f), Node.Id);
			AddShape(SphereMesh, Pole + FVector(0.0f, 0.0f, 285.0f), FVector(0.16f), FLinearColor(1.0f, 0.8f, 0.2f), Node.Id);
			Node.FlagCloth = AddMesh(CubeMesh, Pole + FVector(0.0f, -62.0f, 238.0f), FRotator::ZeroRotator, FVector(0.04f, 1.2f, 0.78f),
				UMaterialInstanceDynamic::Create(LitMaterial, this), Node.Id);
			Node.FlagBall = AddMesh(SphereMesh, Pole + FVector(-4.0f, -62.0f, 238.0f), FRotator::ZeroRotator, FVector(0.06f, 0.42f, 0.42f),
				UMaterialInstanceDynamic::Create(LitMaterial, this), Node.Id);
		}
		// A padlock for a stage that shows but cannot be played yet.
		Node.Lock.Add(AddShape(CubeMesh, At + FVector(0.0f, 0.0f, 120.0f), FVector(0.9f, 0.5f, 0.75f), FLinearColor(0.35f, 0.34f, 0.38f), Node.Id));
		Node.Lock.Add(AddMesh(CylinderMesh, At + FVector(0.0f, 0.0f, 185.0f), FRotator(0.0f, 0.0f, 90.0f), FVector(0.62f, 0.62f, 0.18f),
			Mid(LitMaterial, FLinearColor(0.55f, 0.55f, 0.6f)), Node.Id));
		Node.Lock.Add(AddShape(SphereMesh, At + FVector(-26.0f, 0.0f, 118.0f), FVector(0.1f, 0.14f, 0.14f), FLinearColor(0.05f, 0.05f, 0.06f), Node.Id));
		// The gates: a great ring standing over the pedestal, the player walks into it.
		if (bGate)
		{
			const FLinearColor GateColor = FLinearColor(0.55f, 0.25f, 1.0f);
			for (int32 I = 0; I < 28; ++I)
			{
				const float Angle = 2.0f * PI * I / 28;
				const FVector Spot = At + FVector(30.0f, FMath::Cos(Angle) * 330.0f, 360.0f + FMath::Sin(Angle) * 330.0f);
				AddMesh(CubeMesh, Spot, FRotator(0.0f, 0.0f, -FMath::RadiansToDegrees(Angle)), FVector(0.5f, 0.82f, 0.42f),
					Mid(LitMaterial, bDark ? FLinearColor(0.12f, 0.1f, 0.14f) : FLinearColor(0.85f, 0.78f, 0.6f)), Node.Id);
				AddMesh(CubeMesh, Spot - FVector(28.0f, 0.0f, 0.0f), FRotator(0.0f, 0.0f, -FMath::RadiansToDegrees(Angle)),
					FVector(0.08f, 0.6f, 0.2f), Mid(GlowMaterial, GateColor, 6.0f), Node.Id);
			}
			AddShape(CylinderMesh, At + FVector(40.0f, 0.0f, 360.0f), FVector(5.8f, 5.8f, 0.04f), FLinearColor(0.35f, 0.12f, 0.8f), Node.Id,
				FRotator(90.0f, 0.0f, 0.0f), true, 1.4f);
			AddLight(At + FVector(-80.0f, 0.0f, 360.0f), GateColor, 120.0f, 1400.0f, Node.Id);
		}
	}
}

void AChaosImpactWorldMap::BuildFog()
{
	if (!FogMaterial || !PlaneMesh)
	{
		return;
	}
	const bool bDark = MapIndex == 2;
	const FBox2D Cover = BoardBounds.ExpandBy(FVector2D(7000.0, 7000.0));
	const UWorld* World = GetWorld();
	const bool bGame = World && World->IsGameWorld();
	if (bGame && !FogMask)
	{
		MaskWidth = 160;
		MaskHeight = FMath::Max(32, FMath::RoundToInt(MaskWidth * BoardBounds.GetSize().Y / BoardBounds.GetSize().X));
		FogMask = UTexture2D::CreateTransient(MaskWidth, MaskHeight, PF_B8G8R8A8);
		FogMask->SRGB = true;
		FogMask->Filter = TF_Bilinear;
		FogMask->AddressX = TA_Clamp;
		FogMask->AddressY = TA_Clamp;
		FogMask->UpdateResource();
		MaskPixels.SetNumZeroed(MaskWidth * MaskHeight * 4);
		bMaskDirty = true;
	}
	struct FLayer { float Z; float Density; float Bias; float Scale; float Opacity; };
	const FLayer FogLayers[] = {{60.0f, 1.6f, 0.08f, 1.0f, 0.97f}, {230.0f, 1.4f, -0.08f, 1.35f, 0.9f}, {430.0f, 1.25f, -0.22f, 1.8f, 0.75f}};
	for (int32 L = 0; L < UE_ARRAY_COUNT(FogLayers); ++L)
	{
		UMaterialInstanceDynamic* Fog = UMaterialInstanceDynamic::Create(FogMaterial, this);
		Fog->SetVectorParameterValue(TEXT("BoardMin"), FLinearColor(BoardBounds.Min.X, BoardBounds.Min.Y, 0.0f));
		Fog->SetVectorParameterValue(TEXT("BoardSize"), FLinearColor(BoardBounds.GetSize().X, BoardBounds.GetSize().Y, 0.0f));
		Fog->SetVectorParameterValue(TEXT("FogColor"), bDark ? FLinearColor(0.2f, 0.09f, 0.26f) : FLinearColor(1.0f, 0.99f, 0.97f));
		Fog->SetVectorParameterValue(TEXT("FogShade"), bDark ? FLinearColor(0.025f, 0.008f, 0.045f) : FLinearColor(0.55f, 0.62f, 0.8f));
		Fog->SetVectorParameterValue(TEXT("Drift"), FLinearColor(30.0f + L * 12.0f, 10.0f - L * 8.0f, 5.0f, 0.0f));
		Fog->SetScalarParameterValue(TEXT("Density"), FogLayers[L].Density);
		Fog->SetScalarParameterValue(TEXT("Bias"), FogLayers[L].Bias);
		Fog->SetScalarParameterValue(TEXT("NoiseScale"), FogLayers[L].Scale);
		Fog->SetScalarParameterValue(TEXT("MaxOpacity"), FogLayers[L].Opacity);
		Fog->SetScalarParameterValue(TEXT("Glow"), bDark ? 1.0f : 1.05f);
		if (FogMask)
		{
			Fog->SetTextureParameterValue(TEXT("RevealMask"), FogMask);
		}
		UStaticMeshComponent* Sheet = AddMesh(PlaneMesh, FVector(Cover.GetCenter(), FogLayers[L].Z), FRotator::ZeroRotator,
			FVector(Cover.GetSize().X / 100.0f, Cover.GetSize().Y / 100.0f, 1.0f), Fog);
		Sheet->SetTranslucentSortPriority(10 + L);
		Sheet->SetCastShadow(false);
		// In the editor (no mask yet) the fog is left off, so the board can be seen.
		Sheet->SetVisibility(bGame);
		FogMaterials.Add(Fog);
		FogSheets.Add(Sheet);
	}
}

// =====================================================================================================================
// Scenery

void AChaosImpactWorldMap::AddTree(const FVector2D& At, const float Height, const FLinearColor& Leaves, const int32 Node, const float Seed)
{
	const FVector Base(At, 0.0f);
	AddShape(CylinderMesh, Base + FVector(0.0f, 0.0f, Height * 0.2f), FVector(0.22f, 0.22f, Height * 0.4f / 100.0f),
		FLinearColor(0.32f, 0.2f, 0.11f), Node);
	const float Turn = FMath::Frac(Seed * 0.37f) * 90.0f;
	AddShape(ConeMesh, Base + FVector(0.0f, 0.0f, Height * 0.55f), FVector(Height / 160.0f, Height / 160.0f, Height * 0.6f / 100.0f), Leaves,
		Node, FRotator(0.0f, Turn, 0.0f));
	AddShape(ConeMesh, Base + FVector(0.0f, 0.0f, Height * 0.82f), FVector(Height / 230.0f, Height / 230.0f, Height * 0.42f / 100.0f),
		MapScale(Leaves, 1.18f), Node, FRotator(0.0f, Turn + 30.0f, 0.0f));
}

void AChaosImpactWorldMap::AddBuilding(const FVector2D& At, const FVector& Size, const FLinearColor& Color, const FLinearColor& Windows,
	const int32 Node, const float Yaw)
{
	AddBlock(FVector(At, Size.Z * 0.5f), Size, Color, Node, Yaw);
	AddBlock(FVector(At, Size.Z + 8.0f), FVector(Size.X * 0.7f, Size.Y * 0.7f, 16.0f), MapScale(Color, 1.4f), Node, Yaw);
	// Rows of lit windows on the faces the camera sees (the back, -X, and both sides).
	const FRotator Turn(0.0f, Yaw, 0.0f);
	for (float Z = 60.0f; Z < Size.Z - 30.0f; Z += 70.0f)
	{
		const FVector Back = Turn.RotateVector(FVector(-Size.X * 0.5f - 1.0f, 0.0f, 0.0f));
		AddMesh(CubeMesh, FVector(At, Z) + Back, Turn, FVector(0.02f, Size.Y * 0.0075f, 0.22f), Mid(GlowMaterial, Windows, 2.2f), Node);
		for (const float Side : {-1.0f, 1.0f})
		{
			const FVector Face = Turn.RotateVector(FVector(0.0f, Side * (Size.Y * 0.5f + 1.0f), 0.0f));
			AddMesh(CubeMesh, FVector(At, Z) + Face, Turn, FVector(Size.X * 0.0075f, 0.02f, 0.22f), Mid(GlowMaterial, Windows, 1.6f), Node);
		}
	}
}

void AChaosImpactWorldMap::AddFlagPole(const FVector2D& At, const FLinearColor& Color, const int32 Node, const float Height)
{
	AddShape(CylinderMesh, FVector(At, Height * 0.5f), FVector(0.08f, 0.08f, Height / 100.0f), FLinearColor(0.2f, 0.2f, 0.22f), Node);
	AddBlock(FVector(At + FVector2D(0.0f, -70.0f), Height - 50.0f), FVector(4.0f, 130.0f, 85.0f), Color, Node);
	AddShape(SphereMesh, FVector(At + FVector2D(-4.0f, -70.0f), Height - 50.0f), FVector(0.06f, 0.45f, 0.45f), FLinearColor(0.02f, 0.02f, 0.025f), Node);
}

void AChaosImpactWorldMap::AddCloudPuff(const FVector& At, const float Size, const int32 Node, const FLinearColor& Color)
{
	AddShape(SphereMesh, At, FVector(Size / 100.0f, Size / 100.0f, Size * 0.62f / 100.0f), Color, Node);
	AddShape(SphereMesh, At + FVector(Size * 0.45f, Size * 0.2f, -Size * 0.08f), FVector(Size * 0.7f / 100.0f, Size * 0.7f / 100.0f,
		Size * 0.45f / 100.0f), Color, Node);
	AddShape(SphereMesh, At + FVector(-Size * 0.4f, -Size * 0.25f, -Size * 0.1f), FVector(Size * 0.65f / 100.0f, Size * 0.65f / 100.0f,
		Size * 0.4f / 100.0f), Color, Node);
}

void AChaosImpactWorldMap::AddPillar(const FVector2D& At, const float Height, const float Radius, const FLinearColor& Color, const int32 Node)
{
	AddShape(CylinderMesh, FVector(At, 12.0f), FVector(Radius * 2.6f / 100.0f, Radius * 2.6f / 100.0f, 0.24f), MapScale(Color, 0.85f), Node);
	AddShape(CylinderMesh, FVector(At, Height * 0.5f), FVector(Radius * 2.0f / 100.0f, Radius * 2.0f / 100.0f, Height / 100.0f), Color, Node);
	AddShape(CubeMesh, FVector(At, Height + 10.0f), FVector(Radius * 2.8f / 100.0f, Radius * 2.8f / 100.0f, 0.2f), FLinearColor(1.0f, 0.75f, 0.3f), Node);
}

void AChaosImpactWorldMap::AddSnowTree(const FVector2D& At, const float Height, const int32 Node, const float Seed)
{
	const FVector Base(At, 0.0f);
	const float Turn = FMath::Frac(Seed * 0.37f) * 90.0f;
	const FLinearColor Fir(0.1f, 0.3f, 0.2f);
	const FLinearColor Snow(0.95f, 0.97f, 1.0f);
	AddShape(CylinderMesh, Base + FVector(0.0f, 0.0f, Height * 0.15f), FVector(0.2f, 0.2f, Height * 0.3f / 100.0f), FLinearColor(0.3f, 0.2f, 0.12f), Node);
	// Three tiers of fir, snow lying on each.
	for (int32 Tier = 0; Tier < 3; ++Tier)
	{
		const float Z = Height * (0.4f + Tier * 0.22f);
		const float W = Height * (0.62f - Tier * 0.16f) / 100.0f;
		const float H = Height * (0.42f - Tier * 0.06f) / 100.0f;
		AddShape(ConeMesh, Base + FVector(0.0f, 0.0f, Z), FVector(W, W, H), Fir, Node, FRotator(0.0f, Turn + Tier * 25.0f, 0.0f));
		AddShape(ConeMesh, Base + FVector(0.0f, 0.0f, Z + H * 22.0f), FVector(W * 0.72f, W * 0.72f, H * 0.5f), Snow, Node,
			FRotator(0.0f, Turn + Tier * 25.0f, 0.0f));
	}
}

void AChaosImpactWorldMap::AddSimae(const FVector& At, const float Yaw, const float Scale, const int32 Node)
{
	using namespace ChaosImpactBallTypes;
	USceneComponent* Bird = NewObject<USceneComponent>(this, NAME_None, RF_Transient);
	Bird->SetupAttachment(RootComponent);
	Bird->SetRelativeLocationAndRotation(At, FRotator(0.0f, Yaw, 0.0f));
	Bird->RegisterComponent();
	Own(Bird, Node);
	Birds.Add(Bird);
	BirdGround.Add(At.Z);
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, SimaeAssets::TexturedMaterial);
	UTexture* Texture = LoadObject<UTexture>(nullptr, SimaeAssets::BirdTexture);
	UMaterialInstanceDynamic* Look = Base && Texture ? Mid(Base, FLinearColor::White) : nullptr;
	if (Look)
	{
		Look->SetTextureParameterValue(TEXT("BodyTexture"), Texture);
	}
	for (const TCHAR* Path : {SimaeAssets::BirdBody, SimaeAssets::BirdTail, SimaeAssets::BirdWing, SimaeAssets::BirdEye, SimaeAssets::BirdCrest})
	{
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, Path);
		if (!Mesh)
		{
			continue;
		}
		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
		Part->SetupAttachment(Bird);
		Part->SetStaticMesh(Mesh);
		Part->SetRelativeScale3D(FVector(Scale));
		Part->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		if (Look)
		{
			for (int32 Slot = 0; Slot < Part->GetNumMaterials(); ++Slot)
			{
				Part->SetMaterial(Slot, Look);
			}
		}
		Part->RegisterComponent();
		Own(Part, Node);
	}
}

void AChaosImpactWorldMap::BuildScenery1()
{
	FRandomStream Random(1234);
	const auto Clear = [this](const FVector2D& P, const float Margin)
	{
		for (const FEdge& Edge : Edges)
		{
			for (int32 I = 0; I + 1 < Edge.Points.Num(); ++I)
			{
				FVector2D Closest;
				if (MapSegDist(P, FVector2D(Edge.Points[I]), FVector2D(Edge.Points[I + 1]), Closest) < Margin)
				{
					return false;
				}
			}
		}
		for (const FNode& Node : Nodes)
		{
			if (FVector2D::Distance(P, Node.At) < NodeRadius + Margin)
			{
				return false;
			}
		}
		return true;
	};

	// ---- 1: the neon district at night (the board is a dark street here): towers lit up, the president's big screen.
	const FLinearColor Night(0.07f, 0.07f, 0.13f);
	AddBuilding(FVector2D(-650, -820), FVector(320, 300, 430), Night, FLinearColor(0.1f, 0.8f, 1.0f), 1);
	AddBuilding(FVector2D(-700, 820), FVector(300, 320, 520), Night, FLinearColor(1.0f, 0.15f, 0.6f), 1);
	AddBuilding(FVector2D(-1150, -330), FVector(260, 260, 300), MapScale(Night, 1.3f), FLinearColor(1.0f, 0.6f, 0.15f), 1);
	AddBuilding(FVector2D(-1150, 380), FVector(260, 260, 340), MapScale(Night, 1.3f), FLinearColor(0.6f, 0.3f, 1.0f), 1);
	AddBuilding(FVector2D(1650, 0), FVector(340, 420, 760), Night, FLinearColor(0.1f, 0.8f, 1.0f), 1);
	// The big screen between the two roads: the president's face (a ball), declaring.
	AddBlock(FVector(960, 0, 330), FVector(80, 520, 330), FLinearColor(0.05f, 0.05f, 0.08f), 1);
	AddMesh(CubeMesh, FVector(918, 0, 360), FRotator::ZeroRotator, FVector(0.02f, 4.6f, 2.6f), Mid(GlowMaterial, FLinearColor(0.9f, 0.05f, 0.08f), 2.5f), 1);
	AddShape(SphereMesh, FVector(912, 0, 370), FVector(0.05f, 1.5f, 1.5f), FLinearColor(0.02f, 0.02f, 0.02f), 1);
	AddShape(CylinderMesh, FVector(960, -200, 80), FVector(0.25f, 0.25f, 1.6f), FLinearColor(0.12f, 0.12f, 0.15f), 1);
	AddShape(CylinderMesh, FVector(960, 200, 80), FVector(0.25f, 0.25f, 1.6f), FLinearColor(0.12f, 0.12f, 0.15f), 1);
	// Neon tubes along the street round the start, and street lamps.
	for (int32 I = 0; I < 10; ++I)
	{
		const float Angle = 2.0f * PI * (I + 0.5f) / 10;
		const FVector2D Spot = FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * 760.0f;
		if (!Clear(Spot, 140.0f))
		{
			continue;
		}
		AddShape(CylinderMesh, FVector(Spot, 110), FVector(0.1f, 0.1f, 2.2f), FLinearColor(0.15f, 0.15f, 0.2f), 1);
		AddShape(SphereMesh, FVector(Spot, 225), FVector(0.32f), I % 2 ? FLinearColor(1.0f, 0.2f, 0.7f) : FLinearColor(0.1f, 0.85f, 1.0f), 1,
			FRotator::ZeroRotator, true, 6.0f);
	}
	AddLight(FVector(-300, 0, 400), FLinearColor(0.5f, 0.4f, 1.0f), 60.0f, 1800.0f, 1);
	AddLight(FVector(900, 0, 300), FLinearColor(1.0f, 0.2f, 0.3f), 50.0f, 1000.0f, 1);

	// ---- 2-7: the sea. Water beyond the beach on the left, the harbour along it.
	{
		TArray<FVector2D> Coast;
		for (int32 Id = 2; Id <= 7; ++Id)
		{
			Coast.Add(Nodes[NodeIndex(Id)].At + FVector2D(0.0, -1100.0));
		}
		AddSlab(HullAround(Coast, 1200.0f, 60.0f, 41.0f), 3.0f, 30.0f, 0.0f, FLinearColor::White, FLinearColor(0.04f, 0.2f, 0.4f), 2, false,
			WaterMaterial ? UMaterialInstanceDynamic::Create(WaterMaterial, this) : nullptr);
	}
	const FLinearColor Wood(0.45f, 0.28f, 0.15f);
	// 2 霧の桟橋: a long pier into the water, banks of mist.
	AddBlock(FVector(1050, -1950, 16), FVector(170, 760, 12), Wood, 2);
	for (int32 I = 0; I < 5; ++I)
	{
		AddShape(CylinderMesh, FVector(980 + (I % 2) * 140, -1650 - I * 160, 0), FVector(0.18f, 0.18f, 0.5f), MapScale(Wood, 0.7f), 2);
	}
	for (int32 I = 0; I < 6; ++I)
	{
		AddCloudPuff(FVector(700 + I * 160, -2300 - (I % 3) * 120, 60), 170.0f, 2, FLinearColor(0.85f, 0.88f, 0.92f));
	}
	// 3 満ち潮の倉庫街: warehouses with arched roofs on the shore.
	for (int32 I = 0; I < 3; ++I)
	{
		const FVector2D Spot(1950 + I * 380, -2620 + (I % 2) * 60);
		const FLinearColor Shed = I == 1 ? FLinearColor(0.85f, 0.4f, 0.15f) : FLinearColor(0.2f, 0.5f, 0.55f);
		AddBlock(FVector(Spot, 110), FVector(300, 260, 220), Shed, 3);
		AddMesh(CylinderMesh, FVector(Spot, 220), FRotator(0.0f, 0.0f, 90.0f), FVector(2.6f, 1.2f, 3.0f), Mid(LitMaterial, MapScale(Shed, 0.75f)), 3);
	}
	// 4 クレーン操車場: stacked containers and a gantry crane.
	const FLinearColor Boxes[] = {{0.8f, 0.15f, 0.1f}, {0.1f, 0.35f, 0.75f}, {0.95f, 0.65f, 0.1f}, {0.15f, 0.55f, 0.3f}};
	for (int32 I = 0; I < 10; ++I)
	{
		const FVector2D Spot(3250 + (I % 4) * 130, -2900 - (I / 4) * 70);
		AddBlock(FVector(Spot, 30 + (I / 8) * 60), FVector(120, 55, 60), Boxes[I % 4], 4);
	}
	const FLinearColor Yellow(1.0f, 0.75f, 0.1f);
	for (const float Y : {-2700.0f, -3150.0f})
	{
		AddBlock(FVector(3900, Y, 260), FVector(30, 30, 520), Yellow, 4);
		AddBlock(FVector(4150, Y, 260), FVector(30, 30, 520), Yellow, 4);
	}
	AddBlock(FVector(4025, -2925, 530), FVector(320, 520, 30), Yellow, 4);
	AddBlock(FVector(4025, -3300, 530), FVector(60, 400, 26), Yellow, 4);
	AddBlock(FVector(4025, -3420, 380), FVector(110, 50, 55), Boxes[1], 4);
	// 5 沈んだ灯台: a lighthouse, red and white, its lamp lit.
	for (int32 I = 0; I < 5; ++I)
	{
		AddShape(CylinderMesh, FVector(4800, -2750, 50 + I * 100), FVector(1.3f - I * 0.12f, 1.3f - I * 0.12f, 1.0f),
			I % 2 ? FLinearColor(0.85f, 0.1f, 0.1f) : FLinearColor(0.95f, 0.95f, 0.92f), 5);
	}
	AddShape(SphereMesh, FVector(4800, -2750, 560), FVector(0.9f), FLinearColor(1.0f, 0.95f, 0.6f), 5, FRotator::ZeroRotator, true, 8.0f);
	AddShape(ConeMesh, FVector(4800, -2750, 650), FVector(1.1f, 1.1f, 0.8f), FLinearColor(0.2f, 0.2f, 0.25f), 5);
	AddLight(FVector(4800, -2750, 600), FLinearColor(1.0f, 0.9f, 0.6f), 120.0f, 1600.0f, 5);
	// 6 沖の的当て: target buoys out on the water.
	for (int32 I = 0; I < 4; ++I)
	{
		const FVector Spot(5550 + I * 180, -3050 - (I % 2) * 220, 20);
		for (int32 Band = 0; Band < 3; ++Band)
		{
			AddShape(CylinderMesh, Spot + FVector(0, 0, Band * 6), FVector(1.1f - Band * 0.35f, 1.1f - Band * 0.35f, 0.08f),
				Band % 2 ? FLinearColor::White : FLinearColor(0.9f, 0.1f, 0.1f), 6);
		}
	}
	// 7 港の司令塔: the harbour's command tower, the United States' flag on top.
	const FLinearColor Steel(0.12f, 0.15f, 0.22f);
	AddBlock(FVector(7150, -2500, 330), FVector(360, 360, 660), Steel, 7);
	AddBlock(FVector(7150, -2500, 700), FVector(460, 460, 80), FLinearColor(0.7f, 0.08f, 0.1f), 7);
	AddShape(CylinderMesh, FVector(7150, -2500, 860), FVector(0.1f, 0.1f, 2.4f), FLinearColor(0.4f, 0.4f, 0.45f), 7);
	AddShape(SphereMesh, FVector(7150, -2500, 980), FVector(0.3f), FLinearColor(1.0f, 0.1f, 0.1f), 7, FRotator::ZeroRotator, true, 10.0f);
	AddFlagPole(FVector2D(7000, -2250), FLinearColor(0.85f, 0.08f, 0.1f), 7, 420.0f);
	// Boats.
	for (const FVector2D& Spot : {FVector2D(2100, -3250), FVector2D(4500, -3350), FVector2D(6300, -2950)})
	{
		AddBlock(FVector(Spot, 18), FVector(260, 90, 40), FLinearColor(0.9f, 0.9f, 0.88f), 4);
		AddBlock(FVector(Spot + FVector2D(-30, 0), 55), FVector(90, 70, 40), FLinearColor(0.2f, 0.4f, 0.75f), 4);
	}

	// ---- 8-13: 霧氷の森 (the shima-enaga's snowy forest). Snowy firs off the paths, drifts of snow, the little white birds everywhere.
	{
		int32 Placed = 0;
		for (int32 Try = 0; Try < 900 && Placed < 100; ++Try)
		{
			const FVector2D Spot(Random.FRandRange(700.0f, 7600.0f), Random.FRandRange(500.0f, 3500.0f));
			float Nearest = MAX_flt;
			int32 NearestId = 8;
			for (const int32 Id : {8, 9, 10, 11, 12, 13})
			{
				const float D = FVector2D::Distance(Spot, Nodes[NodeIndex(Id)].At);
				if (D < Nearest)
				{
					Nearest = D;
					NearestId = Id;
				}
			}
			if (Nearest > 1050.0f || !Clear(Spot, 220.0f))
			{
				continue;
			}
			if (Random.FRand() < 0.78f)
			{
				AddSnowTree(Spot, Random.FRandRange(170.0f, 340.0f), NearestId, static_cast<float>(Try));
			}
			else
			{
				const float S = Random.FRandRange(1.2f, 2.6f);
				AddShape(SphereMesh, FVector(Spot, 0.0f), FVector(S, S * 0.8f, S * 0.45f), FLinearColor(0.93f, 0.96f, 1.0f), NearestId);
			}
			++Placed;
		}
	}
	// The birds: alone, in pairs and threes by the paths.
	for (int32 Try = 0, Placed = 0; Try < 400 && Placed < 16; ++Try)
	{
		const FVector2D Spot(Random.FRandRange(800.0f, 7400.0f), Random.FRandRange(600.0f, 3300.0f));
		int32 NearestId = 8;
		float Nearest = MAX_flt;
		for (const int32 Id : {8, 9, 10, 11, 12, 13})
		{
			const float D = FVector2D::Distance(Spot, Nodes[NodeIndex(Id)].At);
			if (D < Nearest)
			{
				Nearest = D;
				NearestId = Id;
			}
		}
		if (Nearest > 900.0f || !Clear(Spot, 140.0f))
		{
			continue;
		}
		const int32 Flock = 1 + Placed % 3;
		for (int32 B = 0; B < Flock; ++B)
		{
			AddSimae(FVector(Spot + FVector2D(B * 55.0f, (B % 2) * 40.0f), 0.0f), Random.FRandRange(120.0f, 240.0f), 2.2f, NearestId);
		}
		++Placed;
	}
	// 9 アリの巣: a mound poking out of the snow, dark holes.
	AddShape(ConeMesh, FVector(2350, 2650, 100), FVector(5.5f, 5.5f, 2.0f), FLinearColor(0.45f, 0.3f, 0.16f), 9);
	AddShape(ConeMesh, FVector(2350, 2650, 175), FVector(3.0f, 3.0f, 0.6f), FLinearColor(0.95f, 0.97f, 1.0f), 9);
	for (const FVector& Hole : {FVector(2180, 2600, 70), FVector(2300, 2480, 110), FVector(2250, 2760, 60)})
	{
		AddShape(SphereMesh, Hole, FVector(0.4f, 0.4f, 0.25f), FLinearColor(0.03f, 0.02f, 0.01f), 9);
	}
	// 10 ふかふか雪原: deep drifts and snowmen.
	for (int32 I = 0; I < 7; ++I)
	{
		const float Angle = 2.0f * PI * I / 7;
		const FVector2D Spot = FVector2D(3650, 3000) + FVector2D(FMath::Cos(Angle) * 480.0f, FMath::Sin(Angle) * 340.0f);
		if (!Clear(Spot, 140.0f))
		{
			continue;
		}
		if (I % 2)
		{
			AddShape(SphereMesh, FVector(Spot, 45), FVector(1.0f), FLinearColor(0.96f, 0.97f, 1.0f), 10);
			AddShape(SphereMesh, FVector(Spot, 120), FVector(0.7f), FLinearColor(0.96f, 0.97f, 1.0f), 10);
			AddMesh(ConeMesh, FVector(Spot + FVector2D(-38, 0), 122), FRotator(90.0f, 180.0f, 0.0f), FVector(0.12f, 0.12f, 0.35f),
				Mid(LitMaterial, FLinearColor(1.0f, 0.45f, 0.05f)), 10);
		}
		else
		{
			AddShape(SphereMesh, FVector(Spot, 0), FVector(3.2f, 2.4f, 1.1f), FLinearColor(0.93f, 0.96f, 1.0f), 10);
		}
	}
	// 11 妖精の舞踏会: a ring of glowing snowflakes.
	for (int32 I = 0; I < 12; ++I)
	{
		const float Angle = 2.0f * PI * I / 12;
		AddShape(SphereMesh, FVector(4750 + FMath::Cos(Angle) * 300, 2750 + FMath::Sin(Angle) * 300, 60 + (I % 3) * 30), FVector(0.22f),
			I % 2 ? FLinearColor(0.5f, 0.9f, 1.0f) : FLinearColor(0.9f, 0.7f, 1.0f), 11, FRotator::ZeroRotator, true, 8.0f);
	}
	AddLight(FVector(4750, 2750, 150), FLinearColor(0.5f, 0.9f, 1.0f), 60.0f, 900.0f, 11);
	// 12 大樹のぼり: the great tree, frosted white, birds on top.
	AddShape(CylinderMesh, FVector(5950, 3250, 350), FVector(2.4f, 2.4f, 7.0f), FLinearColor(0.35f, 0.24f, 0.16f), 12);
	AddShape(SphereMesh, FVector(5950, 3250, 760), FVector(7.0f, 7.0f, 4.0f), FLinearColor(0.85f, 0.92f, 0.98f), 12);
	AddShape(SphereMesh, FVector(5700, 3050, 640), FVector(4.0f, 4.0f, 2.6f), FLinearColor(0.78f, 0.88f, 0.96f), 12);
	AddShape(SphereMesh, FVector(6200, 3450, 650), FVector(4.0f, 4.0f, 2.6f), FLinearColor(0.78f, 0.88f, 0.96f), 12);
	AddSimae(FVector(5800, 3000, 930), 200.0f, 2.6f, 12);
	AddSimae(FVector(5880, 2960, 935), 160.0f, 2.6f, 12);
	// 13 森の司令塔: a log fort under snow, the United States' flag.
	for (int32 I = 0; I < 14; ++I)
	{
		const float Angle = 2.0f * PI * I / 14;
		const FVector Post(7150 + FMath::Cos(Angle) * 330, 2550 + FMath::Sin(Angle) * 330, 110);
		AddShape(CylinderMesh, Post, FVector(0.4f, 0.4f, 2.2f), FLinearColor(0.4f, 0.26f, 0.14f), 13);
		AddShape(SphereMesh, Post + FVector(0, 0, 112), FVector(0.42f, 0.42f, 0.2f), FLinearColor(0.95f, 0.97f, 1.0f), 13);
	}
	AddBlock(FVector(7150, 2550, 280), FVector(260, 260, 560), FLinearColor(0.32f, 0.2f, 0.1f), 13);
	AddBlock(FVector(7150, 2550, 590), FVector(340, 340, 60), FLinearColor(0.7f, 0.08f, 0.1f), 13);
	AddBlock(FVector(7150, 2550, 628), FVector(330, 330, 18), FLinearColor(0.95f, 0.97f, 1.0f), 13);
	AddFlagPole(FVector2D(7000, 2300), FLinearColor(0.85f, 0.08f, 0.1f), 13, 420.0f);

	// ---- 14-15 and the gate: the path through the clouds to the temple.
	for (int32 I = 0; I < 26; ++I)
	{
		const FVector2D Spot(Random.FRandRange(7300.0f, 11600.0f), Random.FRandRange(-2300.0f, 2300.0f));
		if (!Clear(Spot, 380.0f))
		{
			continue;
		}
		const int32 PartOwner = Spot.X < 8800.0f ? 14 : Spot.X < 10100.0f ? 15 : ChaosImpactSolo::GateToMap2;
		AddCloudPuff(FVector(Spot, Random.FRandRange(30.0f, 120.0f)), Random.FRandRange(150.0f, 300.0f), PartOwner, FLinearColor(0.98f, 0.97f, 1.0f));
	}
	// 14: red gates over the path, one after another.
	for (const float X : {7500.0f, 8550.0f, 8900.0f})
	{
		const FLinearColor Red(0.85f, 0.12f, 0.08f);
		AddShape(CylinderMesh, FVector(X, -300, 200), FVector(0.4f, 0.4f, 4.0f), Red, 14);
		AddShape(CylinderMesh, FVector(X, 300, 200), FVector(0.4f, 0.4f, 4.0f), Red, 14);
		AddBlock(FVector(X, 0, 410), FVector(50, 760, 36), Red, 14);
		AddBlock(FVector(X, 0, 440), FVector(60, 840, 26), FLinearColor(0.08f, 0.06f, 0.06f), 14);
	}
	// 15 空中神殿: colonnades either side, a golden ring round the altar.
	const FLinearColor Marble(0.92f, 0.88f, 0.8f);
	for (const float Side : {-1.0f, 1.0f})
	{
		for (int32 I = 0; I < 5; ++I)
		{
			AddPillar(FVector2D(9000 + I * 220, Side * 560), 440.0f, 34.0f, Marble, 15);
		}
		AddBlock(FVector(9440, Side * 560, 470), FVector(1100, 150, 40), FLinearColor(1.0f, 0.75f, 0.3f), 15);
		AddBlock(FVector(9440, Side * 620, 500), FVector(1060, 90, 30), Marble, 15);
	}
	AddSlab(Blob(FVector2D(9400, 0), FVector2D(470, 470), 48, 0.0f, 0.0f), 14.0f, 10.0f, 4.0f, FLinearColor(1.0f, 0.8f, 0.35f),
		FLinearColor(0.6f, 0.4f, 0.1f), 15);
	AddLight(FVector(9400, 0, 500), FLinearColor(1.0f, 0.8f, 0.5f), 80.0f, 1600.0f, 15);
	// Floating islands off the edges.
	for (int32 I = 0; I < 6; ++I)
	{
		const FVector2D Spot(1500 + I * 1700, (I % 2 ? 1.0f : -1.0f) * Random.FRandRange(4300.0f, 5000.0f));
		const float Size = Random.FRandRange(260.0f, 480.0f);
		const float Z = Random.FRandRange(-300.0f, 250.0f);
		AddSlab(Blob(Spot, FVector2D(Size, Size * 0.8f), 12, 0.25f, I * 3.0f), Z, Size * 0.9f, 12.0f, FLinearColor(0.3f, 0.52f, 0.2f),
			FLinearColor(0.35f, 0.24f, 0.15f));
		AddTree(Spot, Size * 0.6f, FLinearColor(0.15f, 0.45f, 0.15f), 0, I * 7.0f);
	}
}

void AChaosImpactWorldMap::BuildScenery2()
{
	FRandomStream Random(4321);
	const auto Clear = [this](const FVector2D& P, const float Margin)
	{
		for (const FEdge& Edge : Edges)
		{
			for (int32 I = 0; I + 1 < Edge.Points.Num(); ++I)
			{
				FVector2D Closest;
				if (MapSegDist(P, FVector2D(Edge.Points[I]), FVector2D(Edge.Points[I + 1]), Closest) < Margin)
				{
					return false;
				}
			}
		}
		for (const FNode& Node : Nodes)
		{
			if (FVector2D::Distance(P, Node.At) < NodeRadius + Margin)
			{
				return false;
			}
		}
		return true;
	};
	const auto Nearest = [this](const FVector2D& P, std::initializer_list<int32> Ids)
	{
		int32 Best = *Ids.begin();
		float BestDistance = MAX_flt;
		for (const int32 Id : Ids)
		{
			const float D = FVector2D::Distance(P, Nodes[NodeIndex(Id)].At);
			if (D < BestDistance)
			{
				BestDistance = D;
				Best = Id;
			}
		}
		return Best;
	};
	const FLinearColor RedGlow(1.0f, 0.08f, 0.12f);

	// ---- The border: watchtowers with searchlights either side of the gate.
	for (const float Side : {-1.0f, 1.0f})
	{
		const FVector2D Spot(300, Side * 900);
		AddBlock(FVector(Spot, 260), FVector(180, 180, 520), FLinearColor(0.08f, 0.07f, 0.1f), ChaosImpactSolo::GateToMap1);
		AddBlock(FVector(Spot, 540), FVector(260, 260, 60), FLinearColor(0.25f, 0.03f, 0.05f), ChaosImpactSolo::GateToMap1);
		AddShape(SphereMesh, FVector(Spot, 600), FVector(0.5f), RedGlow, ChaosImpactSolo::GateToMap1, FRotator::ZeroRotator, true, 10.0f);
		AddLight(FVector(Spot, 640), RedGlow, 80.0f, 1300.0f, ChaosImpactSolo::GateToMap1);
	}
	// Barbed wire walls along the border.
	for (int32 I = 0; I < 7; ++I)
	{
		for (const float Side : {-1.0f, 1.0f})
		{
			const FVector2D Spot(-300 + I * 40, Side * (1300 + I * 120));
			AddBlock(FVector(Spot, 70), FVector(60, 180, 140), FLinearColor(0.1f, 0.09f, 0.12f), ChaosImpactSolo::GateToMap1, Side * 30.0f);
		}
	}

	// ---- 16-21: the ice state. Ice spikes and snow drifts, a station, mirrors, statues; the state hall.
	for (int32 Try = 0, Placed = 0; Try < 700 && Placed < 70; ++Try)
	{
		const FVector2D Spot(Random.FRandRange(500.0f, 7600.0f), Random.FRandRange(-3600.0f, -500.0f));
		const int32 PartOwner = Nearest(Spot, {16, 17, 18, 19, 20, 21});
		if (FVector2D::Distance(Spot, Nodes[NodeIndex(PartOwner)].At) > 1050.0f || !Clear(Spot, 200.0f))
		{
			continue;
		}
		if (Random.FRand() < 0.6f)
		{
			const float H = Random.FRandRange(120.0f, 380.0f);
			AddShape(ConeMesh, FVector(Spot, H * 0.5f), FVector(H / 260.0f, H / 260.0f, H / 100.0f), FLinearColor(0.35f, 0.75f, 1.0f), PartOwner,
				FRotator(Random.FRandRange(-12.0f, 12.0f), 0.0f, Random.FRandRange(-12.0f, 12.0f)), true, 0.9f);
		}
		else
		{
			AddShape(SphereMesh, FVector(Spot, 0), FVector(Random.FRandRange(2.0f, 4.0f), Random.FRandRange(2.0f, 4.0f), 1.2f),
				FLinearColor(0.75f, 0.82f, 0.95f), PartOwner);
		}
		++Placed;
	}
	// 16 雪原の駅: a platform and a train.
	AddBlock(FVector(1250, -2050, 30), FVector(700, 160, 60), FLinearColor(0.3f, 0.32f, 0.38f), 16);
	for (int32 I = 0; I < 3; ++I)
	{
		AddBlock(FVector(1000 + I * 260, -2280, 90), FVector(240, 130, 150), I == 0 ? RedGlow * 0.5f : FLinearColor(0.15f, 0.18f, 0.3f), 16);
	}
	// 18 氷鏡の回廊: tall mirror panels.
	for (int32 I = 0; I < 5; ++I)
	{
		AddMesh(CubeMesh, FVector(3200 + I * 160, -2950 - (I % 2) * 80, 200), FRotator(0.0f, 20.0f * (I % 2 ? 1 : -1), 0.0f),
			FVector(0.08f, 1.2f, 4.0f), Mid(GlowMaterial, FLinearColor(0.4f, 0.8f, 1.0f), 1.6f), 18);
	}
	// 20 氷像割り: statues of the president (a ball on a body) in ice.
	for (int32 I = 0; I < 3; ++I)
	{
		const FVector2D Spot(5550 + I * 260, -3150 + (I % 2) * 140);
		AddShape(CylinderMesh, FVector(Spot, 60), FVector(0.9f, 0.9f, 1.2f), FLinearColor(0.6f, 0.85f, 1.0f), 20);
		AddShape(SphereMesh, FVector(Spot, 170), FVector(0.95f), FLinearColor(0.75f, 0.92f, 1.0f), 20, FRotator::ZeroRotator, true, 0.8f);
	}
	// 21 氷の州庁: an ice fortress.
	AddBlock(FVector(7150, -2550, 300), FVector(420, 420, 600), FLinearColor(0.15f, 0.25f, 0.4f), 21);
	for (const FVector2D& Corner : {FVector2D(-230, -230), FVector2D(230, -230), FVector2D(-230, 230), FVector2D(230, 230)})
	{
		AddShape(ConeMesh, FVector(FVector2D(7150, -2550) + Corner, 700), FVector(1.4f, 1.4f, 2.4f), FLinearColor(0.4f, 0.8f, 1.0f), 21,
			FRotator::ZeroRotator, true, 1.4f);
	}
	AddFlagPole(FVector2D(7000, -2250), RedGlow * 0.8f, 21, 420.0f);
	AddLight(FVector(7150, -2550, 700), FLinearColor(0.3f, 0.7f, 1.0f), 90.0f, 1500.0f, 21);

	// ---- 22-27: the fire state. Rivers of lava, a volcano, hot springs, a foundry, an arena; the state hall.
	for (int32 River = 0; River < 3; ++River)
	{
		TArray<FVector> Flow;
		const float Y0 = 3500.0f - River * 400.0f;
		for (int32 S = 0; S <= 24; ++S)
		{
			const float X = 600.0f + S * 290.0f;
			Flow.Add(FVector(X, Y0 + FMath::Sin(S * 0.7f + River) * 260.0f + River * 120.0f, 0.0f));
		}
		FMapMesh Lava;
		MapStrip(Lava, Flow, 0.0, MapLength(Flow), 110.0f - River * 20.0f, 9.0f, 4.0f, FLinearColor::White);
		UProceduralMeshComponent* Part = AddProc(TEXT("Lava"), River == 0 ? 25 : River == 1 ? 23 : 22,
			Mid(GlowMaterial, FLinearColor(1.0f, 0.3f, 0.02f), 5.0f), false);
		Part->CreateMeshSection_LinearColor(0, Lava.V, Lava.T, Lava.N, Lava.UV, Lava.C, TArray<FProcMeshTangent>(), false);
	}
	// 24 噴火口: the volcano, glowing at its top.
	AddShape(ConeMesh, FVector(3550, 3150, 380), FVector(12.0f, 12.0f, 7.6f), FLinearColor(0.12f, 0.05f, 0.04f), 24);
	AddShape(CylinderMesh, FVector(3550, 3150, 745), FVector(2.2f, 2.2f, 0.12f), FLinearColor(1.0f, 0.35f, 0.02f), 24, FRotator::ZeroRotator, true, 8.0f);
	AddLight(FVector(3550, 3150, 900), FLinearColor(1.0f, 0.35f, 0.05f), 200.0f, 2600.0f, 24);
	// 22 温泉街: small houses, steam.
	for (int32 I = 0; I < 5; ++I)
	{
		const FVector2D Spot(800 + I * 240, 2000 + (I % 2) * 220);
		if (!Clear(Spot, 150.0f))
		{
			continue;
		}
		AddBlock(FVector(Spot, 70), FVector(160, 140, 140), FLinearColor(0.25f, 0.12f, 0.08f), 22);
		AddMesh(ConeMesh, FVector(Spot, 175), FRotator(0.0f, 45.0f, 0.0f), FVector(2.4f, 2.4f, 0.8f), Mid(LitMaterial, FLinearColor(0.1f, 0.06f, 0.05f)), 22);
		AddCloudPuff(FVector(Spot + FVector2D(0, 120), 240), 90.0f, 22, FLinearColor(0.5f, 0.45f, 0.48f));
	}
	// 23 鋳造所: blocks, chimneys, glowing vents.
	AddBlock(FVector(2450, 2700, 160), FVector(520, 380, 320), FLinearColor(0.12f, 0.1f, 0.1f), 23);
	for (int32 I = 0; I < 3; ++I)
	{
		AddShape(CylinderMesh, FVector(2300 + I * 150, 2780, 450), FVector(0.6f, 0.6f, 3.2f), FLinearColor(0.15f, 0.12f, 0.12f), 23);
		AddMesh(CubeMesh, FVector(2190, 2580 + I * 110, 120), FRotator::ZeroRotator, FVector(0.02f, 0.7f, 0.5f), Mid(GlowMaterial, FLinearColor(1.0f, 0.35f, 0.02f), 6.0f), 23);
	}
	// 25 闘技場: a ring of walls.
	for (int32 I = 0; I < 16; ++I)
	{
		const float Angle = 2.0f * PI * I / 16;
		AddBlock(FVector(4750 + FMath::Cos(Angle) * 420, 2850 + FMath::Sin(Angle) * 420, 90), FVector(60, 160, 180),
			FLinearColor(0.22f, 0.08f, 0.06f), 25, FMath::RadiansToDegrees(Angle));
	}
	// 26 炎と氷: half red, half blue.
	AddSlab(Blob(FVector2D(5850, 3250), FVector2D(320, 320), 24, 0.0f, 0.0f), 14.0f, 10.0f, 4.0f, FLinearColor(0.9f, 0.2f, 0.05f), FLinearColor(0.4f, 0.1f, 0.0f), 26);
	AddSlab(Blob(FVector2D(5850, 3250), FVector2D(160, 320), 24, 0.0f, 0.0f), 16.0f, 10.0f, 4.0f, FLinearColor(0.1f, 0.45f, 1.0f), FLinearColor(0.05f, 0.2f, 0.5f), 26);
	// 27 炎の州庁.
	AddBlock(FVector(7150, 2550, 300), FVector(420, 420, 600), FLinearColor(0.2f, 0.06f, 0.04f), 27);
	AddBlock(FVector(7150, 2550, 640), FVector(480, 480, 80), FLinearColor(0.08f, 0.02f, 0.02f), 27);
	AddShape(SphereMesh, FVector(7150, 2550, 760), FVector(1.6f), FLinearColor(1.0f, 0.3f, 0.02f), 27, FRotator::ZeroRotator, true, 6.0f);
	AddFlagPole(FVector2D(7000, 2250), RedGlow * 0.8f, 27, 420.0f);
	// Rocks of the fire state.
	for (int32 Try = 0, Placed = 0; Try < 500 && Placed < 40; ++Try)
	{
		const FVector2D Spot(Random.FRandRange(500.0f, 7600.0f), Random.FRandRange(500.0f, 3600.0f));
		const int32 PartOwner = Nearest(Spot, {22, 23, 24, 25, 26, 27});
		if (FVector2D::Distance(Spot, Nodes[NodeIndex(PartOwner)].At) > 1050.0f || !Clear(Spot, 200.0f))
		{
			continue;
		}
		const float S = Random.FRandRange(0.6f, 1.6f);
		AddMesh(CubeMesh, FVector(Spot, 30.0f * S), FRotator(Random.FRandRange(0.0f, 30.0f), Random.FRandRange(0.0f, 90.0f), Random.FRandRange(0.0f, 30.0f)),
			FVector(S, S * 0.8f, S * 0.7f), Mid(LitMaterial, FLinearColor(0.1f, 0.05f, 0.04f)), PartOwner);
		++Placed;
	}

	// ---- 28-30: the capital.
	// 28 ボール没収工場: the factory, and a mountain of confiscated balls.
	AddBlock(FVector(8100, -950, 250), FVector(600, 500, 500), FLinearColor(0.14f, 0.13f, 0.15f), 28);
	for (int32 I = 0; I < 3; ++I)
	{
		AddShape(CylinderMesh, FVector(7900 + I * 200, -1100, 650), FVector(0.7f, 0.7f, 4.0f), FLinearColor(0.16f, 0.14f, 0.16f), 28);
		AddShape(SphereMesh, FVector(7900 + I * 200, -1100, 880), FVector(0.5f), RedGlow, 28, FRotator::ZeroRotator, true, 6.0f);
	}
	const FLinearColor BallColors[] = {{1.0f, 0.2f, 0.1f}, {0.1f, 0.5f, 1.0f}, {1.0f, 0.8f, 0.1f}, {0.2f, 0.85f, 0.4f}, {0.95f, 0.95f, 0.95f}};
	for (int32 I = 0; I < 46; ++I)
	{
		const float Angle = Random.FRandRange(0.0f, 2.0f * PI);
		const float Dist = Random.FRandRange(0.0f, 340.0f);
		const FVector2D Spot = FVector2D(8150, 950) + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Dist;
		AddShape(SphereMesh, FVector(Spot, 30 + (340.0f - Dist) * 0.7f), FVector(0.6f), BallColors[I % 5], 28);
	}
	// 29 首都の大通り: red banners and lamps lining the avenue, ball statues.
	for (int32 I = 0; I < 5; ++I)
	{
		for (const float Side : {-1.0f, 1.0f})
		{
			const FVector2D Spot(8600 + I * 300, Side * 430);
			AddShape(CylinderMesh, FVector(Spot, 200), FVector(0.12f, 0.12f, 4.0f), FLinearColor(0.2f, 0.18f, 0.2f), 29);
			AddMesh(CubeMesh, FVector(Spot + FVector2D(0, Side * 50), 260), FRotator::ZeroRotator, FVector(0.04f, 0.8f, 2.2f),
				Mid(GlowMaterial, RedGlow, 1.6f), 29);
			AddShape(SphereMesh, FVector(Spot, 410), FVector(0.3f), FLinearColor(1.0f, 0.75f, 0.4f), 29, FRotator::ZeroRotator, true, 5.0f);
		}
	}
	for (const float Side : {-1.0f, 1.0f})
	{
		AddShape(CylinderMesh, FVector(9300, Side * 1000, 80), FVector(2.0f, 2.0f, 1.6f), FLinearColor(0.2f, 0.18f, 0.22f), 29);
		AddShape(SphereMesh, FVector(9300, Side * 1000, 280), FVector(2.6f), FLinearColor(0.85f, 0.7f, 0.3f), 29);
	}
	AddLight(FVector(9300, 0, 500), RedGlow, 70.0f, 1800.0f, 29);
	// 30 ホワイトボールハウス: the white house with a great ball for a dome, lit red from below.
	const FLinearColor White(0.92f, 0.92f, 0.95f);
	AddBlock(FVector(11350, 0, 220), FVector(700, 1600, 440), White, 30);
	AddBlock(FVector(11350, 0, 455), FVector(760, 1660, 30), MapScale(White, 0.85f), 30);
	for (int32 I = 0; I < 8; ++I)
	{
		AddPillar(FVector2D(10960, -630 + I * 180), 420.0f, 30.0f, White, 30);
	}
	AddShape(SphereMesh, FVector(11350, 0, 780), FVector(6.2f), White, 30);
	AddShape(SphereMesh, FVector(11050, 0, 780), FVector(0.2f, 3.0f, 3.0f), FLinearColor(0.85f, 0.1f, 0.12f), 30);
	AddFlagPole(FVector2D(11350, 0), RedGlow * 0.8f, 30, 1280.0f);
	AddLight(FVector(10800, -700, 120), RedGlow, 160.0f, 1800.0f, 30);
	AddLight(FVector(10800, 700, 120), RedGlow, 160.0f, 1800.0f, 30);
	// Dark spires round the edge of the land.
	for (int32 I = 0; I < 22; ++I)
	{
		const float Angle = 2.0f * PI * I / 22;
		const FVector2D Spot = BoardBounds.GetCenter() + FVector2D(FMath::Cos(Angle) * (BoardBounds.GetExtent().X - 900.0f),
			FMath::Sin(Angle) * (BoardBounds.GetExtent().Y - 700.0f));
		if (!Clear(Spot, 500.0f) || Spot.X < BoardBounds.GetCenter().X - BoardBounds.GetExtent().X * 0.6f)
		{
			continue;
		}
		const float H = Random.FRandRange(400.0f, 900.0f);
		AddShape(ConeMesh, FVector(Spot, H * 0.5f), FVector(H / 400.0f, H / 400.0f, H / 100.0f), FLinearColor(0.05f, 0.03f, 0.07f), 0);
	}
}

// =====================================================================================================================
// Progress on the map

int32 AChaosImpactWorldMap::NodeIndex(const int32 Id) const
{
	return Nodes.IndexOfByPredicate([Id](const FNode& Node) { return Node.Id == Id; });
}

TArray<int32> AChaosImpactWorldMap::GetNodeIds() const
{
	TArray<int32> Ids;
	for (const FNode& Node : Nodes)
	{
		Ids.Add(Node.Id);
	}
	return Ids;
}

bool AChaosImpactWorldMap::HasNode(const int32 Id) const
{
	return NodeIndex(Id) != INDEX_NONE;
}

FVector AChaosImpactWorldMap::GetNodeLocation(const int32 Id) const
{
	const int32 Index = NodeIndex(Id);
	return Index == INDEX_NONE ? GetActorLocation() : GetActorTransform().TransformPosition(FVector(Nodes[Index].At, PedestalTop));
}

float AChaosImpactWorldMap::GetNodeReveal(const int32 Id) const
{
	const int32 Index = NodeIndex(Id);
	return Index == INDEX_NONE ? 0.0f : Nodes[Index].Reveal;
}

float AChaosImpactWorldMap::GetRevealSeconds() const
{
	return IsRevealing() ? static_cast<float>(FPlatformTime::Seconds() - RevealStartedAt) : -1.0f;
}

void AChaosImpactWorldMap::ApplyProgress(const bool bAnimate)
{
	if (!Progress)
	{
		return;
	}
	RevealQueue.Reset();
	for (FNode& Node : Nodes)
	{
		const bool bGateHome = Node.Id == ChaosImpactSolo::GateToMap1;
		Node.bRevealed = bGateHome || Progress->IsRevealed(Node.Id);
		const bool bShown = !bAnimate || bGateHome || Progress->Shown.Contains(Node.Id);
		if (!Node.bRevealed)
		{
			Node.Reveal = 0.0f;
		}
		else if (bShown)
		{
			Node.Reveal = 1.0f;
		}
		else if (Node.Reveal < 1.0f)
		{
			Node.Reveal = 0.0f;
			RevealQueue.Add(Node.Id);
		}
		if (!bAnimate && Node.bRevealed)
		{
			Progress->Shown.AddUnique(Node.Id);
		}
	}
	bComplete = IsMapComplete();
	if (!bAnimate || !bComplete)
	{
		FullClear = bComplete ? 1.0f : 0.0f;
	}
	bMaskDirty = true;
	UpdateNodeLook(0.0f);
}

bool AChaosImpactWorldMap::IsMapComplete() const
{
	if (!Progress)
	{
		return false;
	}
	int32 Stages = 0;
	for (const FNode& Node : Nodes)
	{
		const FChaosImpactSoloStageInfo* Info = ChaosImpactSolo::FindStage(Node.Id);
		if (!Info || Info->Kind == EChaosImpactSoloStageKind::Gate)
		{
			continue;
		}
		++Stages;
		if (!Progress->IsCleared(Node.Id))
		{
			return false;
		}
	}
	return Stages > 0;
}

void AChaosImpactWorldMap::UpdateEdgesOpen()
{
	for (FEdge& Edge : Edges)
	{
		const int32 A = NodeIndex(Edge.From);
		const int32 B = NodeIndex(Edge.To);
		const float Shown = FMath::Min(Nodes[A].Reveal, Nodes[B].Reveal);
		Edge.bOpen = Nodes[A].bRevealed && Nodes[B].bRevealed && Shown > 0.5f;
		for (USceneComponent* Part : Edge.Parts)
		{
			if (Part)
			{
				Part->SetVisibility(Shown > 0.2f);
			}
		}
	}
}

void AChaosImpactWorldMap::UpdateNodeLook(const float DeltaSeconds)
{
	const float Time = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	const bool bDark = MapIndex == 2;
	for (FNode& Node : Nodes)
	{
		const bool bVisible = Node.Reveal > 0.01f;
		for (USceneComponent* Part : Node.Parts)
		{
			if (Part)
			{
				Part->SetVisibility(bVisible);
			}
		}
		if (!bVisible || !Progress)
		{
			continue;
		}
		const FChaosImpactSoloStageInfo* Info = ChaosImpactSolo::FindStage(Node.Id);
		const bool bGate = Info && Info->Kind == EChaosImpactSoloStageKind::Gate;
		const bool bCleared = Progress->IsCleared(Node.Id);
		const bool bAvailable = Node.Id == ChaosImpactSolo::GateToMap1 || Progress->IsAvailable(Node.Id);
		const bool bReady = bGate || !ChaosImpactSolo::GetLevelPackage(Node.Id).IsEmpty();
		const bool bBoss = Info && (Info->Kind == EChaosImpactSoloStageKind::Boss || Info->Kind == EChaosImpactSoloStageKind::Story);
		const bool bChallenge = Info && Info->Kind == EChaosImpactSoloStageKind::Challenge;
		// Colours by state: waiting to be played (fire), cleared (ice and gold), locked (grey), not made yet (dim).
		FLinearColor Glow = bGate ? (bAvailable ? FLinearColor(0.6f, 0.3f, 1.0f) : FLinearColor(0.3f, 0.3f, 0.34f))
			: bCleared ? FLinearColor(0.3f, 0.75f, 1.0f)
			: !bAvailable ? FLinearColor(0.3f, 0.3f, 0.34f)
			: bBoss ? FLinearColor(1.0f, 0.12f, 0.1f)
			: bChallenge ? FLinearColor(0.75f, 0.35f, 1.0f)
			: FLinearColor(1.0f, 0.55f, 0.08f);
		if (bAvailable && !bCleared && !bReady)
		{
			Glow = MapScale(Glow, 0.5f);
		}
		const float Pulse = bAvailable && !bCleared ? 0.75f + 0.25f * FMath::Sin(Time * 3.2f + Node.Id) : 1.0f;
		const FLinearColor TopColor = bGate ? FLinearColor(0.32f, 0.2f, 0.5f)
			: bCleared ? FLinearColor(0.82f, 0.9f, 1.0f)
			: bAvailable ? (bDark ? FLinearColor(0.35f, 0.08f, 0.08f) : FLinearColor(0.95f, 0.85f, 0.6f))
			: FLinearColor(0.32f, 0.31f, 0.33f);
		if (Node.Top)
		{
			if (UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(Node.Top->GetMaterial(0)))
			{
				M->SetVectorParameterValue(TEXT("Color"), TopColor);
			}
		}
		if (Node.Ring)
		{
			if (UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(Node.Ring->GetMaterial(0)))
			{
				M->SetVectorParameterValue(TEXT("Color"), Glow);
				M->SetScalarParameterValue(TEXT("Intensity"), (bAvailable && !bCleared ? 5.0f : 2.0f) * Pulse);
			}
		}
		if (Node.Number)
		{
			Node.Number->SetTextRenderColor((bCleared ? FLinearColor(0.1f, 0.25f, 0.55f) : bAvailable ? FLinearColor(0.35f, 0.12f, 0.03f)
				: FLinearColor(0.15f, 0.15f, 0.17f)).ToFColor(true));
		}
		if (Node.Crystal)
		{
			const bool bShowCrystal = (bAvailable || bCleared) && !bGate;
			Node.Crystal->SetVisibility(bShowCrystal);
			Node.Spin += DeltaSeconds * (bCleared ? 30.0f : 70.0f);
			const float Bob = FMath::Sin(Time * 2.0f + Node.Id) * 18.0f;
			Node.Crystal->SetRelativeLocationAndRotation(FVector(Node.At, (bCleared ? 380.0f : 440.0f) + Bob),
				FRotator(45.0f, Node.Spin, 35.26f));
			const float Size = (bBoss ? 0.75f : 0.5f) * (bCleared ? 0.7f : 1.0f);
			Node.Crystal->SetRelativeScale3D(FVector(Size, Size, Size * 1.4f));
			if (UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(Node.Crystal->GetMaterial(0)))
			{
				M->SetVectorParameterValue(TEXT("Color"), bCleared ? FLinearColor(1.0f, 0.78f, 0.3f) : Glow);
				M->SetScalarParameterValue(TEXT("Intensity"), (bCleared ? 3.0f : 6.0f) * Pulse);
			}
		}
		if (Node.Light)
		{
			Node.Light->SetVisibility(bAvailable || bCleared);
			Node.Light->SetLightColor(bCleared ? FLinearColor(1.0f, 0.85f, 0.55f) : Glow);
			Node.Light->SetIntensity((bAvailable && !bCleared ? 70.0f : 30.0f) * Pulse);
		}
		if (Node.FlagCloth)
		{
			if (UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(Node.FlagCloth->GetMaterial(0)))
			{
				M->SetVectorParameterValue(TEXT("Color"), bCleared ? FLinearColor(0.1f, 0.35f, 0.9f) : FLinearColor(0.8f, 0.06f, 0.08f));
			}
			const float Wave = FMath::Sin(Time * 4.0f + Node.Id) * 6.0f;
			Node.FlagCloth->SetRelativeRotation(FRotator(0.0f, Wave, 0.0f));
		}
		if (Node.FlagBall)
		{
			if (UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(Node.FlagBall->GetMaterial(0)))
			{
				M->SetVectorParameterValue(TEXT("Color"), bCleared ? FLinearColor(1.0f, 0.78f, 0.2f) : FLinearColor(0.02f, 0.02f, 0.025f));
			}
		}
		for (USceneComponent* Part : Node.Lock)
		{
			if (Part)
			{
				Part->SetVisibility(!bAvailable && !bCleared);
			}
		}
	}
	UpdateEdgesOpen();
}

void AChaosImpactWorldMap::PaintFogMask()
{
	if (!FogMask || !bMaskDirty || MaskWidth <= 0)
	{
		return;
	}
	bMaskDirty = false;
	const FVector2D Size = BoardBounds.GetSize();
	struct FCircle { FVector2D At; float Inner; float Outer; float Amount; };
	TArray<FCircle> Circles;
	struct FBand { FVector2D A; FVector2D B; float Amount; };
	TArray<FBand> Bands;
	for (const FNode& Node : Nodes)
	{
		if (Node.Reveal > 0.0f)
		{
			const float Grow = FMath::InterpEaseOut(0.0f, 1.0f, Node.Reveal, 2.0f);
			Circles.Add({Node.At, 480.0f * Grow, 1050.0f * FMath::Max(Grow, 0.2f), FMath::Min(1.0f, Node.Reveal * 3.0f)});
		}
	}
	for (const FEdge& Edge : Edges)
	{
		const float Amount = FMath::Min(Nodes[NodeIndex(Edge.From)].Reveal, Nodes[NodeIndex(Edge.To)].Reveal);
		if (Amount <= 0.0f)
		{
			continue;
		}
		for (int32 I = 0; I + 1 < Edge.Points.Num(); I += 2)
		{
			Bands.Add({FVector2D(Edge.Points[I]), FVector2D(Edge.Points[FMath::Min(I + 2, Edge.Points.Num() - 1)]), Amount});
		}
	}
	for (int32 Y = 0; Y < MaskHeight; ++Y)
	{
		for (int32 X = 0; X < MaskWidth; ++X)
		{
			const FVector2D P = BoardBounds.Min + FVector2D((X + 0.5) / MaskWidth * Size.X, (Y + 0.5) / MaskHeight * Size.Y);
			float Clear = 0.0f;
			for (const FCircle& C : Circles)
			{
				const float D = FVector2D::Distance(P, C.At);
				if (D < C.Outer)
				{
					Clear = FMath::Max(Clear, (1.0f - MapSmooth((D - C.Inner) / FMath::Max(C.Outer - C.Inner, 1.0f))) * C.Amount);
				}
			}
			for (const FBand& B : Bands)
			{
				FVector2D Closest;
				const float D = MapSegDist(P, B.A, B.B, Closest);
				if (D < 620.0f)
				{
					Clear = FMath::Max(Clear, (1.0f - MapSmooth((D - 280.0f) / 340.0f)) * B.Amount);
				}
			}
			Clear = FMath::Max(Clear, MapSmooth(FullClear));
			// The material reads the texture as sRGB: store the value so it reads back linear.
			const uint8 Value = static_cast<uint8>(FMath::RoundToInt(255.0f * FMath::Pow(FMath::Clamp(Clear, 0.0f, 1.0f), 1.0f / 2.2f)));
			uint8* Pixel = &MaskPixels[(Y * MaskWidth + X) * 4];
			Pixel[0] = Value;
			Pixel[1] = Value;
			Pixel[2] = Value;
			Pixel[3] = 255;
		}
	}
	// Texture rows run along +V (the material's U is X, V is Y).
	FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, MaskWidth, MaskHeight);
	uint8* Copy = static_cast<uint8*>(FMemory::Malloc(MaskPixels.Num()));
	FMemory::Memcpy(Copy, MaskPixels.GetData(), MaskPixels.Num());
	FogMask->UpdateTextureRegions(0, 1, Region, MaskWidth * 4, 4, Copy,
		[](uint8* Data, const FUpdateTextureRegion2D* Regions)
		{
			FMemory::Free(Data);
			delete Regions;
		});
}

void AChaosImpactWorldMap::RefreshFromProgress()
{
	Progress = UChaosImpactSoloProgress::Load();
	ApplyProgress(false);
	RevealQueue.Reset();
	RevealStep = ERevealStep::None;
	SetPlayerFrozen(false);
	Progress->Save();
}

// =====================================================================================================================
// Play

AChaosImpactCharacter* AChaosImpactWorldMap::GetPlayer() const
{
	const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	return PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
}

void AChaosImpactWorldMap::SetPlayerFrozen(const bool bFrozen)
{
	if (AChaosImpactCharacter* Player = GetPlayer())
	{
		Player->SetTrainingMenuFrozen(bFrozen);
	}
}

void AChaosImpactWorldMap::PlacePlayer()
{
	AChaosImpactCharacter* Player = GetPlayer();
	if (!Player || bPlaced)
	{
		return;
	}
	bPlaced = true;
	int32 At = FCString::Atoi(GetWorld()->URL.GetOption(TEXT("CIMapAt="), TEXT("0")));
	if (!HasNode(At) && Progress)
	{
		At = Progress->LastNode;
	}
	if (!HasNode(At) || !Nodes[NodeIndex(At)].bRevealed)
	{
		At = MapIndex == 2 ? ChaosImpactSolo::GateToMap1 : 1;
	}
	const float HalfHeight = Player->GetCapsuleComponent() ? Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.0f;
	const FVector Spot = GetNodeLocation(At) + FVector(0.0f, 0.0f, HalfHeight + 4.0f);
	Player->SetActorLocationAndRotation(Spot, FRotator::ZeroRotator, false, nullptr, ETeleportType::TeleportPhysics);
	Player->SetMapWalker(true);
	UE_LOG(LogTemp, Display, TEXT("World map %d: the player starts on %d at %s"), MapIndex, At, *Spot.ToString());
	CameraFocus = Spot;
	bCameraFocusSet = false;
	if (IsRevealing())
	{
		SetPlayerFrozen(true);
	}
	if (!MapCamera)
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		MapCamera = GetWorld()->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), Spot, FRotator(-52.0f, 0.0f, 0.0f), Params);
		if (MapCamera)
		{
			MapCamera->GetCameraComponent()->SetFieldOfView(45.0f);
			MapCamera->GetCameraComponent()->bConstrainAspectRatio = false;
		}
	}
}

void AChaosImpactWorldMap::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!GetWorld() || !GetWorld()->IsGameWorld())
	{
		return;
	}
	PlacePlayer();
	AChaosImpactCharacter* Player = GetPlayer();
	UpdateReveal(DeltaSeconds);
	UpdateNodeLook(DeltaSeconds);
	if (bComplete && FullClear < 1.0f && !IsRevealing())
	{
		FullClear = FMath::Min(1.0f, FullClear + DeltaSeconds / 2.0f);
		bMaskDirty = true;
	}
	for (UStaticMeshComponent* Sheet : FogSheets)
	{
		if (Sheet && Sheet->IsVisible() == (FullClear >= 1.0f))
		{
			Sheet->SetVisibility(FullClear < 1.0f);
		}
	}
	PaintFogMask();
	if (Player && bPlaced)
	{
		UpdateConstraint(Player);
		UpdateInput(Player);
		UpdateAutoWalk(Player);
		UpdateFocus(Player, DeltaSeconds);
	}
	// Over a second into the whole-map view and back, easing out of one view and into the other.
	ChaosImpactEase::Toggle(OverviewLinear, bOverview, DeltaSeconds, 1.3f);
	OverviewBlend = ChaosImpactEase::InOutStrong(OverviewLinear);
	// The birds hop now and then, turning a little.
	const float Time = GetWorld()->GetTimeSeconds();
	for (int32 I = 0; I < Birds.Num(); ++I)
	{
		if (USceneComponent* Bird = Birds[I])
		{
			const float Phase = FMath::Fmod(Time * 0.7f + I * 0.618f * 3.0f, 3.0f);
			const float Hop = Phase < 0.5f ? FMath::Sin(Phase / 0.5f * PI) * 22.0f : 0.0f;
			FVector At = Bird->GetRelativeLocation();
			At.Z = (BirdGround.IsValidIndex(I) ? BirdGround[I] : At.Z) + Hop;
			Bird->SetRelativeLocation(At);
		}
	}
	UpdateCamera(Player, DeltaSeconds);
}

void AChaosImpactWorldMap::SetOverview(const bool bOn)
{
	if (bOn == bOverview || (bOn && IsRevealing()))
	{
		return;
	}
	bOverview = bOn;
	AutoRoute.Reset();
	SetPlayerFrozen(bOn);
	ChaosImpactSfx::Play2D(this, bOn ? EChaosImpactSfx::UiOpen : EChaosImpactSfx::UiBack);
}

FVector AChaosImpactWorldMap::NearestOnNetwork(const FVector& Location, int32* OutEdge, int32* OutSegment) const
{
	const FVector Local = GetActorTransform().InverseTransformPosition(Location);
	const FVector2D P(Local);
	double Best = MAX_dbl;
	FVector2D BestPoint = P;
	for (int32 E = 0; E < Edges.Num(); ++E)
	{
		if (!Edges[E].bOpen)
		{
			continue;
		}
		const TArray<FVector>& Points = Edges[E].Points;
		for (int32 I = 0; I + 1 < Points.Num(); ++I)
		{
			FVector2D Closest;
			const double D = MapSegDist(P, FVector2D(Points[I]), FVector2D(Points[I + 1]), Closest);
			if (D < Best)
			{
				Best = D;
				BestPoint = Closest;
				if (OutEdge)
				{
					*OutEdge = E;
				}
				if (OutSegment)
				{
					*OutSegment = I;
				}
			}
		}
	}
	return GetActorTransform().TransformPosition(FVector(BestPoint, Local.Z));
}

bool AChaosImpactWorldMap::IsWalkable(const FVector& Location) const
{
	const FVector2D P(GetActorTransform().InverseTransformPosition(Location));
	bool bAnything = false;
	for (const FNode& Node : Nodes)
	{
		if (Node.bRevealed && Node.Reveal > 0.5f)
		{
			bAnything = true;
			if (FVector2D::Distance(P, Node.At) <= NodeRadius - 20.0f)
			{
				return true;
			}
		}
	}
	for (const FEdge& Edge : Edges)
	{
		if (!Edge.bOpen)
		{
			continue;
		}
		bAnything = true;
		for (int32 I = 0; I + 1 < Edge.Points.Num(); ++I)
		{
			FVector2D Closest;
			if (MapSegDist(P, FVector2D(Edge.Points[I]), FVector2D(Edge.Points[I + 1]), Closest) <= PathHalfWidth)
			{
				return true;
			}
		}
	}
	return !bAnything;
}

FVector AChaosImpactWorldMap::ClampToPaths(const FVector& Location) const
{
	const FVector Local = GetActorTransform().InverseTransformPosition(Location);
	const FVector2D P(Local);
	double Best = MAX_dbl;
	FVector2D BestPoint = P;
	const auto Consider = [&](const FVector2D& Candidate)
	{
		const double D = FVector2D::DistSquared(P, Candidate);
		if (D < Best)
		{
			Best = D;
			BestPoint = Candidate;
		}
	};
	for (const FNode& Node : Nodes)
	{
		if (Node.bRevealed && Node.Reveal > 0.5f)
		{
			const FVector2D Away = (P - Node.At).GetSafeNormal();
			Consider(Node.At + Away * (NodeRadius - 21.0f));
		}
	}
	for (const FEdge& Edge : Edges)
	{
		if (!Edge.bOpen)
		{
			continue;
		}
		for (int32 I = 0; I + 1 < Edge.Points.Num(); ++I)
		{
			FVector2D Closest;
			MapSegDist(P, FVector2D(Edge.Points[I]), FVector2D(Edge.Points[I + 1]), Closest);
			Consider(Closest + (P - Closest).GetSafeNormal() * (PathHalfWidth - 1.0f));
		}
	}
	return Best == MAX_dbl ? Location : GetActorTransform().TransformPosition(FVector(BestPoint, Local.Z));
}

void AChaosImpactWorldMap::UpdateConstraint(AChaosImpactCharacter* Player)
{
	const FVector Here = Player->GetActorLocation();
	if (IsWalkable(Here))
	{
		return;
	}
	const FVector Held = ClampToPaths(Here);
	const FVector Out = FVector(Here.X - Held.X, Here.Y - Held.Y, 0.0).GetSafeNormal();
	Player->SetActorLocation(FVector(Held.X, Held.Y, Here.Z), false, nullptr, ETeleportType::None);
	if (UCharacterMovementComponent* Movement = Player->GetCharacterMovement())
	{
		const double Away = FVector::DotProduct(Movement->Velocity, Out);
		if (Away > 0.0)
		{
			Movement->Velocity -= Out * Away;
		}
	}
}

void AChaosImpactWorldMap::UpdateFocus(AChaosImpactCharacter* Player, const float DeltaSeconds)
{
	const FVector2D P(GetActorTransform().InverseTransformPosition(Player->GetActorLocation()));
	int32 Found = 0;
	for (const FNode& Node : Nodes)
	{
		if (Node.bRevealed && Node.Reveal >= 0.99f && FVector2D::Distance(P, Node.At) < NodeRadius)
		{
			Found = Node.Id;
			break;
		}
	}
	if (Found != FocusedNode)
	{
		if (Found)
		{
			ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiMove);
			ArrivedAt = FPlatformTime::Seconds();
		}
		FocusedNode = Found;
	}
	if (FocusedNode)
	{
		CardNode = FocusedNode;
	}
	CardAlpha = FMath::FInterpConstantTo(CardAlpha, FocusedNode && !IsRevealing() ? 1.0f : 0.0f, DeltaSeconds, 4.0f);
	if (Widget)
	{
		Widget->SetCardNode(CardNode);
	}
}

void AChaosImpactWorldMap::UpdateInput(AChaosImpactCharacter* Player)
{
	AChaosImpactPlayerController* PC = Cast<AChaosImpactPlayerController>(Player->GetController());
	if (!PC || !PC->IsGameplayActive() || bLeaving)
	{
		bConfirmHeld = true;
		bClickHeld = true;
		return;
	}
	if (!PC->IsUsingGamepad() && !PC->bShowMouseCursor)
	{
		PC->SetShowMouseCursor(true);
	}
	// Just arrived (or the loading screen still fading out): nothing counts until the map has been up a moment and
	// every confirm button has been let go once.
	const UChaosImpactLoadingSubsystem* Loading = UChaosImpactLoadingSubsystem::Get(this);
	const double Now = FPlatformTime::Seconds();
	if (Loading && Loading->IsShowing())
	{
		InputReadyAt = -1.0;
	}
	else if (InputReadyAt < 0.0)
	{
		InputReadyAt = Now + 0.5;
		bInputReleasedSinceArrival = false;
	}
	if (!bInputReleasedSinceArrival)
	{
		const bool bAnyDown = PC->IsInputKeyDown(EKeys::LeftMouseButton) || PC->IsInputKeyDown(EKeys::Enter) || PC->IsInputKeyDown(EKeys::SpaceBar)
			|| PC->IsInputKeyDown(EKeys::Gamepad_FaceButton_Bottom) || ChaosImpactSettings::IsActionDown(PC, EChaosImpactAction::Throw);
		bInputReleasedSinceArrival = InputReadyAt >= 0.0 && Now >= InputReadyAt && !bAnyDown;
		bConfirmHeld = true;
		bClickHeld = true;
		return;
	}
	const bool bClick = PC->IsInputKeyDown(EKeys::LeftMouseButton);
	const bool bClickEdge = (bClick && !bClickHeld) || PC->WasInputKeyJustPressed(EKeys::LeftMouseButton);
	bClickHeld = bClick;
	const bool bConfirm = PC->IsInputKeyDown(EKeys::Enter) || PC->IsInputKeyDown(EKeys::SpaceBar)
		|| PC->IsInputKeyDown(EKeys::Gamepad_FaceButton_Bottom)
		|| (!bClick && ChaosImpactSettings::IsActionDown(PC, EChaosImpactAction::Throw));
	const bool bConfirmEdge = (bConfirm && !bConfirmHeld) || PC->WasInputKeyJustPressed(EKeys::Enter)
		|| PC->WasInputKeyJustPressed(EKeys::SpaceBar) || PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Bottom);
	bConfirmHeld = bConfirm;
	if (IsRevealing())
	{
		// The confirm button skips the reveal.
		if (bConfirmEdge || bClickEdge)
		{
			for (FNode& Node : Nodes)
			{
				if (Node.bRevealed)
				{
					Node.Reveal = 1.0f;
					Progress->Shown.AddUnique(Node.Id);
				}
			}
			RevealQueue.Reset();
			RevealStep = ERevealStep::Return;
			RevealTime = 0.5f;
			bMaskDirty = true;
		}
		return;
	}
	// The whole map: M or the pad's Y open and close it, the wheel pulls out to it and back in.
	const bool bOverviewKey = PC->IsInputKeyDown(EKeys::M) || PC->IsInputKeyDown(EKeys::Gamepad_FaceButton_Top);
	const bool bOverviewEdge = (bOverviewKey && !bOverviewKeyHeld) || PC->WasInputKeyJustPressed(EKeys::M)
		|| PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Top);
	bOverviewKeyHeld = bOverviewKey;
	if (bOverviewEdge)
	{
		SetOverview(!bOverview);
		return;
	}
	if (PC->WasInputKeyJustPressed(EKeys::MouseScrollDown))
	{
		SetOverview(true);
	}
	else if (PC->WasInputKeyJustPressed(EKeys::MouseScrollUp))
	{
		SetOverview(false);
	}
	if (bOverview)
	{
		if (bConfirmEdge || PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Right))
		{
			SetOverview(false);
		}
		else if (bClickEdge)
		{
			// A click on a stage seen from above: back down onto it at once if it is open, else walk toward it.
			FVector Origin;
			FVector Direction;
			if (PC->DeprojectMousePositionToWorld(Origin, Direction) && FMath::Abs(Direction.Z) > 0.05)
			{
				const FVector Hit = Origin + Direction * ((GetActorLocation().Z + PathTop - Origin.Z) / Direction.Z);
				const FVector2D P(GetActorTransform().InverseTransformPosition(Hit));
				for (const FNode& Node : Nodes)
				{
					if (Node.bRevealed && Node.Reveal >= 0.99f && FVector2D::Distance(P, Node.At) < NodeRadius * 2.2f)
					{
						SetOverview(false);
						const bool bOpen = Node.Id == ChaosImpactSolo::GateToMap1 || Progress->IsAvailable(Node.Id) || Progress->IsCleared(Node.Id);
						if (bOpen)
						{
							WarpToNode(Node.Id);
						}
						else
						{
							WalkTo(Node.Id);
						}
						break;
					}
				}
			}
		}
		return;
	}
	if (bConfirmEdge && FocusedNode)
	{
		ConfirmFocused();
		return;
	}
	// Development: C on a pedestal counts that stage as cleared.
	if (FocusedNode && PC->WasInputKeyJustPressed(EKeys::C))
	{
		DevClearFocused();
		return;
	}
	if (bClickEdge)
	{
		// A click on the board: a pedestal walks there (or plays it, if already standing on it), a path walks to that point.
		FVector Origin;
		FVector Direction;
		if (!PC->DeprojectMousePositionToWorld(Origin, Direction) || FMath::Abs(Direction.Z) < 0.05)
		{
			return;
		}
		const double Along = (GetActorLocation().Z + PathTop - Origin.Z) / Direction.Z;
		const FVector Hit = Origin + Direction * Along;
		const FVector2D P(GetActorTransform().InverseTransformPosition(Hit));
		for (const FNode& Node : Nodes)
		{
			if (Node.bRevealed && Node.Reveal >= 0.99f && FVector2D::Distance(P, Node.At) < NodeRadius * 1.25f)
			{
				if (Node.Id == FocusedNode)
				{
					ConfirmFocused();
				}
				else
				{
					WalkTo(Node.Id);
				}
				return;
			}
		}
		TArray<FVector> Route;
		const FVector Goal = ClampToPaths(Hit);
		if (IsWalkable(Hit) || FVector::Dist2D(Goal, Hit) < 400.0f)
		{
			if (PlanRoute(Player->GetActorLocation(), IsWalkable(Hit) ? Hit : Goal, Route))
			{
				AutoRoute = MoveTemp(Route);
				AutoIndex = 0;
				AutoWalkTime = 0.0f;
				AutoGoalNode = 0;
			}
		}
	}
}

bool AChaosImpactWorldMap::PlanRoute(const FVector& From, const FVector& To, TArray<FVector>& OutRoute) const
{
	OutRoute.Reset();
	int32 EdgeFrom = INDEX_NONE;
	int32 SegFrom = 0;
	int32 EdgeTo = INDEX_NONE;
	int32 SegTo = 0;
	const FVector StartOn = NearestOnNetwork(From, &EdgeFrom, &SegFrom);
	const FVector EndOn = NearestOnNetwork(To, &EdgeTo, &SegTo);
	const FVector2D Start2(GetActorTransform().InverseTransformPosition(From));
	const FVector2D End2(GetActorTransform().InverseTransformPosition(To));
	// Standing on a pedestal, that node is where the route starts.
	int32 StartNode = 0;
	int32 EndNode = 0;
	for (const FNode& Node : Nodes)
	{
		if (Node.bRevealed && FVector2D::Distance(Start2, Node.At) < NodeRadius)
		{
			StartNode = Node.Id;
		}
		if (Node.bRevealed && FVector2D::Distance(End2, Node.At) < NodeRadius)
		{
			EndNode = Node.Id;
		}
	}
	const auto LocalPoint = [this](const FVector2D& P) { return GetActorTransform().TransformPosition(FVector(P, PathTop)); };
	// Same path, no node between: straight along it.
	if (!StartNode && !EndNode && EdgeFrom != INDEX_NONE && EdgeFrom == EdgeTo)
	{
		const TArray<FVector>& Points = Edges[EdgeFrom].Points;
		OutRoute.Add(StartOn);
		if (SegFrom < SegTo)
		{
			for (int32 I = SegFrom + 1; I <= SegTo; ++I) { OutRoute.Add(GetActorTransform().TransformPosition(Points[I])); }
		}
		else
		{
			for (int32 I = SegFrom; I > SegTo; --I) { OutRoute.Add(GetActorTransform().TransformPosition(Points[I])); }
		}
		OutRoute.Add(EndOn);
		return true;
	}
	// Otherwise through the nodes: Dijkstra from the start's ends to the goal's ends.
	TMap<int32, double> Cost;
	TMap<int32, int32> Came;
	TArray<int32> Open;
	const auto Seed = [&](const int32 Node, const double C)
	{
		if (!Cost.Contains(Node) || Cost[Node] > C)
		{
			Cost.Add(Node, C);
			Came.Add(Node, 0);
			Open.AddUnique(Node);
		}
	};
	if (StartNode)
	{
		Seed(StartNode, 0.0);
	}
	else if (EdgeFrom != INDEX_NONE)
	{
		const FEdge& E = Edges[EdgeFrom];
		Seed(E.From, FVector::Dist2D(StartOn, GetNodeLocation(E.From)));
		Seed(E.To, FVector::Dist2D(StartOn, GetNodeLocation(E.To)));
	}
	while (Open.Num())
	{
		int32 Best = 0;
		for (const int32 Node : Open)
		{
			if (!Best || Cost[Node] < Cost[Best])
			{
				Best = Node;
			}
		}
		Open.Remove(Best);
		for (const FEdge& E : Edges)
		{
			if (!E.bOpen || (E.From != Best && E.To != Best))
			{
				continue;
			}
			const int32 Other = E.From == Best ? E.To : E.From;
			const double C = Cost[Best] + MapLength(E.Points);
			if (!Cost.Contains(Other) || Cost[Other] > C)
			{
				Cost.Add(Other, C);
				Came.Add(Other, Best);
				Open.AddUnique(Other);
			}
		}
	}
	// The goal's end reached most cheaply.
	int32 Goal = 0;
	double GoalCost = MAX_dbl;
	const auto TryGoal = [&](const int32 Node, const double Extra)
	{
		if (Cost.Contains(Node) && Cost[Node] + Extra < GoalCost)
		{
			GoalCost = Cost[Node] + Extra;
			Goal = Node;
		}
	};
	if (EndNode)
	{
		TryGoal(EndNode, 0.0);
	}
	else if (EdgeTo != INDEX_NONE)
	{
		TryGoal(Edges[EdgeTo].From, FVector::Dist2D(EndOn, GetNodeLocation(Edges[EdgeTo].From)));
		TryGoal(Edges[EdgeTo].To, FVector::Dist2D(EndOn, GetNodeLocation(Edges[EdgeTo].To)));
	}
	if (!Goal)
	{
		return false;
	}
	TArray<int32> Chain;
	for (int32 Node = Goal; Node; Node = Came.FindRef(Node))
	{
		Chain.Insert(Node, 0);
		if (Chain.Num() > Nodes.Num())
		{
			break;
		}
	}
	// From where the player stands onto the first node, then along each path's points, then off to the goal.
	const auto AppendEdge = [&](const int32 A, const int32 B)
	{
		for (const FEdge& E : Edges)
		{
			if (E.From == A && E.To == B)
			{
				for (const FVector& P : E.Points) { OutRoute.Add(GetActorTransform().TransformPosition(P)); }
				return;
			}
			if (E.From == B && E.To == A)
			{
				for (int32 I = E.Points.Num() - 1; I >= 0; --I) { OutRoute.Add(GetActorTransform().TransformPosition(E.Points[I])); }
				return;
			}
		}
	};
	if (!StartNode && EdgeFrom != INDEX_NONE)
	{
		// Along the path the player stands on, toward the chain's first node.
		const FEdge& E = Edges[EdgeFrom];
		OutRoute.Add(StartOn);
		if (Chain[0] == E.To)
		{
			for (int32 I = SegFrom + 1; I < E.Points.Num(); ++I) { OutRoute.Add(GetActorTransform().TransformPosition(E.Points[I])); }
		}
		else
		{
			for (int32 I = SegFrom; I >= 0; --I) { OutRoute.Add(GetActorTransform().TransformPosition(E.Points[I])); }
		}
	}
	else
	{
		OutRoute.Add(LocalPoint(Nodes[NodeIndex(Chain[0])].At));
	}
	for (int32 I = 0; I + 1 < Chain.Num(); ++I)
	{
		AppendEdge(Chain[I], Chain[I + 1]);
	}
	if (!EndNode && EdgeTo != INDEX_NONE)
	{
		const FEdge& E = Edges[EdgeTo];
		if (Goal == E.From)
		{
			for (int32 I = 1; I <= SegTo; ++I) { OutRoute.Add(GetActorTransform().TransformPosition(E.Points[I])); }
		}
		else
		{
			for (int32 I = E.Points.Num() - 2; I > SegTo; --I) { OutRoute.Add(GetActorTransform().TransformPosition(E.Points[I])); }
		}
		OutRoute.Add(EndOn);
	}
	return OutRoute.Num() > 0;
}

void AChaosImpactWorldMap::WalkTo(const int32 Id)
{
	AChaosImpactCharacter* Player = GetPlayer();
	if (!Player || !HasNode(Id))
	{
		return;
	}
	TArray<FVector> Route;
	if (PlanRoute(Player->GetActorLocation(), GetNodeLocation(Id), Route))
	{
		Route.Add(GetNodeLocation(Id));
		AutoRoute = MoveTemp(Route);
		AutoIndex = 0;
		AutoWalkTime = 0.0f;
		AutoGoalNode = Id;
	}
}

void AChaosImpactWorldMap::WarpToNode(const int32 Id)
{
	AChaosImpactCharacter* Player = GetPlayer();
	if (!Player || !HasNode(Id))
	{
		return;
	}
	AutoRoute.Reset();
	const float HalfHeight = Player->GetCapsuleComponent() ? Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.0f;
	const FVector Spot = GetNodeLocation(Id) + FVector(0.0f, 0.0f, HalfHeight + 4.0f);
	Player->SetActorLocation(Spot, false, nullptr, ETeleportType::TeleportPhysics);
	if (UCharacterMovementComponent* Movement = Player->GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
	}
	// (The camera glides over to it: UpdateCamera sees its target jump.)
	ChaosImpactSfx::Play2D(this, EChaosImpactSfx::StageWarp);
}

void AChaosImpactWorldMap::UpdateAutoWalk(AChaosImpactCharacter* Player)
{
	if (!AutoRoute.IsValidIndex(AutoIndex) || IsRevealing())
	{
		return;
	}
	// The player's own steering takes over from a click's route.
	if (const APlayerController* PC = Cast<APlayerController>(Player->GetController()))
	{
		bool bStick = false;
		if (ChaosImpactSettings::GetMoveInput(PC, bStick).SizeSquared() > 0.09)
		{
			AutoRoute.Reset();
			return;
		}
	}
	const FVector Here = Player->GetActorLocation();
	while (AutoRoute.IsValidIndex(AutoIndex) && FVector::Dist2D(Here, AutoRoute[AutoIndex]) < (AutoIndex + 1 == AutoRoute.Num() ? 30.0f : 70.0f))
	{
		++AutoIndex;
	}
	if (!AutoRoute.IsValidIndex(AutoIndex))
	{
		AutoRoute.Reset();
		return;
	}
	// The walk eases too: gathering speed as it sets off, slowing as it comes up to where it is going (the camera
	// following it then eases in and out with it instead of sliding along at one speed).
	AutoWalkTime += GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.0f;
	float Left = static_cast<float>(FVector::Dist2D(Here, AutoRoute[AutoIndex]));
	for (int32 Index = AutoIndex + 1; Index < AutoRoute.Num(); ++Index)
	{
		Left += static_cast<float>(FVector::Dist2D(AutoRoute[Index - 1], AutoRoute[Index]));
	}
	const float SetOff = FMath::Lerp(0.3f, 1.0f, ChaosImpactEase::InOut(AutoWalkTime / 0.5f));
	const float Arrive = FMath::Lerp(0.3f, 1.0f, ChaosImpactEase::InOut(Left / 420.0f));
	Player->AddMovementInput((AutoRoute[AutoIndex] - Here).GetSafeNormal2D(), FMath::Min(SetOff, Arrive));
}

void AChaosImpactWorldMap::UpdateCamera(AChaosImpactCharacter* Player, const float DeltaSeconds)
{
	if (!MapCamera)
	{
		return;
	}
	// Following the player the focus rides a soft spring (it lags as they set off and settles after they stop); a long
	// move is a clear イージーイーズ glide instead (never a constant-speed slide).
	FVector Want = CameraFocus;
	if (RevealStep == ERevealStep::Travel || RevealStep == ERevealStep::Clear)
	{
		Want = RevealTarget;
	}
	else if (Player && RevealStep != ERevealStep::Wait)
	{
		Want = Player->GetActorLocation() + FVector(450.0f, 0.0f, 0.0f);
	}
	Want.Z = GetActorLocation().Z + 60.0f;
	if (bCameraFocusSet && !IsCameraGliding() && FVector::Dist2D(Want, LastWant) > 500.0f && FVector::Dist2D(Want, CameraFocus) > 500.0f)
	{
		// The target jumped (a new stage to show, back to the player, a warp): glide there, longer the further it is.
		TweenFrom = CameraFocus;
		TweenTime = 0.0f;
		TweenSeconds = FMath::Clamp(static_cast<float>(FVector::Dist2D(Want, CameraFocus)) / 2600.0f, 0.8f, 1.6f);
	}
	LastWant = Want;
	if (bCameraFocusSet && IsCameraGliding())
	{
		TweenTime += DeltaSeconds;
		CameraFocus = FMath::Lerp(TweenFrom, Want, ChaosImpactEase::InOutStrong(TweenTime / TweenSeconds));
		CameraFocusVelocity = FVector::ZeroVector;
	}
	else if (bCameraFocusSet)
	{
		CameraFocus = ChaosImpactEase::SpringTo(CameraFocus, Want, CameraFocusVelocity, DeltaSeconds, 0.45f);
	}
	else
	{
		CameraFocus = Want;
		CameraFocusVelocity = FVector::ZeroVector;
		LastWant = Want;
		TweenTime = -1.0f;
	}
	bCameraFocusSet = true;
	const FRotator View(-55.0f, 0.0f, 0.0f);
	const float Distance = 4300.0f;
	FVector Location = CameraFocus - View.Vector() * Distance;
	FRotator Rotation = View;
	if (OverviewBlend > 0.0f)
	{
		// The whole map from high above, turned so the journey runs left to right across the screen.
		FVector2D ViewportSize(16.0, 9.0);
		if (GEngine && GEngine->GameViewport)
		{
			GEngine->GameViewport->GetViewportSize(ViewportSize);
		}
		const float Aspect = FMath::Max(1.0f, static_cast<float>(ViewportSize.X / FMath::Max(ViewportSize.Y, 1.0)));
		const FRotator Above(-68.0f, -90.0f, 0.0f);
		const float HalfTan = FMath::Tan(FMath::DegreesToRadians(45.0f * 0.5f));
		const float HalfWide = (NodeBounds.GetExtent().X + 1500.0f) / HalfTan;
		const float HalfTall = (NodeBounds.GetExtent().Y + 1500.0f) * FMath::Sin(FMath::DegreesToRadians(68.0f)) / (HalfTan / Aspect);
		const FVector Center = GetActorTransform().TransformPosition(FVector(NodeBounds.GetCenter(), 0.0f));
		// OverviewBlend is already eased. The look-at point and the turn go across with it; the distance changes by
		// the same ratio each moment, so the pull-back reads as one even zoom instead of rushing off and crawling in.
		const float A = OverviewBlend;
		const float AboveDistance = FMath::Max(FMath::Max(HalfWide, HalfTall), Distance);
		const FVector Focus = FMath::Lerp(CameraFocus, Center, A);
		Rotation = FQuat::Slerp(View.Quaternion(), Above.Quaternion(), A).Rotator();
		Location = Focus - Rotation.Vector() * (Distance * FMath::Pow(AboveDistance / Distance, A));
	}
	MapCamera->SetActorLocationAndRotation(Location, Rotation);
	// Development: -CIMapCamLog writes the camera's path each frame (to check its easing).
	static const bool bLogCamera = FParse::Param(FCommandLine::Get(), TEXT("CIMapCamLog"));
	if (bLogCamera)
	{
		UE_LOG(LogTemp, Display, TEXT("MAPCAM t=%.4f x=%.1f y=%.1f z=%.1f glide=%d over=%.3f"), GetWorld()->GetTimeSeconds(), Location.X, Location.Y,
			Location.Z, IsCameraGliding() ? 1 : 0, OverviewBlend);
	}
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		const AChaosImpactPlayerController* Menu = Cast<AChaosImpactPlayerController>(PC);
		if (PC->GetViewTarget() != MapCamera && (!Menu || Menu->IsGameplayActive()))
		{
			PC->SetViewTarget(MapCamera);
		}
	}
}

void AChaosImpactWorldMap::UpdateReveal(const float DeltaSeconds)
{
	if (RevealStep == ERevealStep::None)
	{
		return;
	}
	RevealTime += DeltaSeconds;
	switch (RevealStep)
	{
	case ERevealStep::Wait:
		if (bPlaced && RevealTime > 0.8f)
		{
			if (!RevealQueue.Num())
			{
				RevealStep = ERevealStep::Return;
				RevealTime = 0.0f;
				break;
			}
			RevealTarget = GetNodeLocation(RevealQueue[0]);
			RevealStep = ERevealStep::Travel;
			RevealTime = 0.0f;
		}
		break;
	case ERevealStep::Travel:
		if (RevealTime > 0.25f && !IsCameraGliding() && FVector::Dist2D(CameraFocus, RevealTarget) < 300.0f)
		{
			RevealStep = ERevealStep::Clear;
			RevealTime = 0.0f;
			ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiReveal);
		}
		break;
	case ERevealStep::Clear:
	{
		const int32 Index = RevealQueue.Num() ? NodeIndex(RevealQueue[0]) : INDEX_NONE;
		if (Nodes.IsValidIndex(Index))
		{
			Nodes[Index].Reveal = FMath::Clamp(RevealTime / 1.6f, 0.0f, 1.0f);
			bMaskDirty = true;
		}
		if (RevealTime > 2.0f)
		{
			if (Nodes.IsValidIndex(Index))
			{
				Nodes[Index].Reveal = 1.0f;
				Progress->Shown.AddUnique(Nodes[Index].Id);
			}
			RevealQueue.RemoveAt(0);
			RevealTime = 0.0f;
			if (RevealQueue.Num())
			{
				RevealTarget = GetNodeLocation(RevealQueue[0]);
				RevealStep = ERevealStep::Travel;
			}
			else
			{
				RevealStep = ERevealStep::Return;
			}
		}
		break;
	}
	case ERevealStep::Return:
		if (RevealTime > 0.6f && !IsCameraGliding())
		{
			RevealStep = ERevealStep::None;
			SetPlayerFrozen(false);
			if (Progress)
			{
				Progress->Save();
			}
		}
		break;
	default:
		break;
	}
}

bool AChaosImpactWorldMap::ConfirmFocused()
{
	if (!FocusedNode || IsRevealing() || bLeaving || !Progress)
	{
		return false;
	}
	AChaosImpactPlayerController* PC = Cast<AChaosImpactPlayerController>(GetWorld()->GetFirstPlayerController());
	const FChaosImpactSoloStageInfo* Info = ChaosImpactSolo::FindStage(FocusedNode);
	if (!PC || !Info)
	{
		return false;
	}
	const bool bAvailable = FocusedNode == ChaosImpactSolo::GateToMap1 || Progress->IsAvailable(FocusedNode);
	if (!bAvailable)
	{
		ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiDeny);
		return false;
	}
	if (Info->Kind == EChaosImpactSoloStageKind::Gate)
	{
		const bool bToMap2 = FocusedNode == ChaosImpactSolo::GateToMap2;
		bLeaving = true;
		Progress->LastNode = bToMap2 ? ChaosImpactSolo::GateToMap1 : ChaosImpactSolo::GateToMap2;
		Progress->Save();
		ChaosImpactSfx::Play2D(this, EChaosImpactSfx::StageWarp);
		// Through the gate: just a warp, no loading screen.
		PC->OpenSoloWorldMap(bToMap2 ? 2 : 1, Progress->LastNode, false);
		return true;
	}
	if (ChaosImpactSolo::GetLevelPackage(FocusedNode).IsEmpty())
	{
		// Not made yet (準備中).
		ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiDeny);
		return false;
	}
	bLeaving = true;
	Progress->LastNode = FocusedNode;
	Progress->Save();
	ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiStart);
	PC->OpenSoloLevel(FocusedNode);
	return true;
}

bool AChaosImpactWorldMap::DevClearFocused()
{
#if UE_BUILD_SHIPPING
	return false;
#else
	const FChaosImpactSoloStageInfo* Info = FocusedNode ? ChaosImpactSolo::FindStage(FocusedNode) : nullptr;
	if (!Info || Info->Kind == EChaosImpactSoloStageKind::Gate || IsRevealing() || bLeaving || !Progress)
	{
		return false;
	}
	if (Progress->IsCleared(FocusedNode))
	{
		ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiDeny);
		return false;
	}
	// Development: this stage counts as cleared, and what it opens comes out of the fog as after a real clear.
	Progress->MarkCleared(FocusedNode);
	Progress->LastNode = FocusedNode;
	ApplyProgress(true);
	if (RevealQueue.Num())
	{
		RevealStep = ERevealStep::Wait;
		RevealTime = 0.3f;
		RevealStartedAt = FPlatformTime::Seconds();
		SetPlayerFrozen(true);
	}
	Progress->Save();
	ChaosImpactSfx::Play2D(this, EChaosImpactSfx::SoloClear);
	UE_LOG(LogTemp, Display, TEXT("World map: stage %d counted as cleared (development key)"), Info->Number);
	return true;
#endif
}
