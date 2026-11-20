#include "ChaosImpactWhiteHall.h"

#include "ChaosImpact.h"
#include "ChaosImpactCharacterRoster.h"
#include "ChaosImpactPuppetComponent.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"

namespace
{
	// ------------------------------------------------------------------------------------------------ Mesh building

	struct FHallBatch
	{
		TArray<FVector> V;
		TArray<int32> T;
		TArray<FVector> N;
		TArray<FVector2D> UV;
	};

	int32 Vert(FHallBatch& B, const FVector& P, const FVector& N, const FVector2D& UV)
	{
		B.V.Add(P);
		B.N.Add(N.GetSafeNormal());
		B.UV.Add(UV);
		return B.V.Num() - 1;
	}

	/** A quad a-b-c-d (around its edge), facing Normal whichever way round it was given. */
	void Quad(FHallBatch& B, const int32 A, const int32 Bv, const int32 C, const int32 D, const FVector& Normal)
	{
		// (Unreal draws a triangle's front where its corners run clockwise as seen: the cross product points away.)
		const FVector Face = FVector::CrossProduct(B.V[Bv] - B.V[A], B.V[C] - B.V[A]);
		if (FVector::DotProduct(Face, Normal) < 0.0f)
		{
			B.T.Append({A, Bv, C, A, C, D});
		}
		else
		{
			B.T.Append({A, C, Bv, A, D, C});
		}
	}

	void Tri(FHallBatch& B, const int32 A, const int32 Bv, const int32 C, const FVector& Normal)
	{
		const FVector Face = FVector::CrossProduct(B.V[Bv] - B.V[A], B.V[C] - B.V[A]);
		if (FVector::DotProduct(Face, Normal) < 0.0f)
		{
			B.T.Append({A, Bv, C});
		}
		else
		{
			B.T.Append({A, C, Bv});
		}
	}

	/** A box, turned Yaw about its middle; UVs in world size times UVScale on every face. */
	void Box(FHallBatch& B, const FVector& Center, const FVector& Size, const float UVScale, const float Yaw = 0.0f, const bool bBottom = false)
	{
		const FQuat Turn(FVector::UpVector, FMath::DegreesToRadians(Yaw));
		const FVector H = Size * 0.5f;
		const FVector Axes[3] = {Turn.RotateVector(FVector::ForwardVector), Turn.RotateVector(FVector::RightVector), FVector::UpVector};
		const float Half[3] = {static_cast<float>(H.X), static_cast<float>(H.Y), static_cast<float>(H.Z)};
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			for (const float Sign : {-1.0f, 1.0f})
			{
				if (Axis == 2 && Sign < 0.0f && !bBottom)
				{
					continue;
				}
				const FVector N = Axes[Axis] * Sign;
				const int32 U = (Axis + 1) % 3;
				const int32 W = (Axis + 2) % 3;
				const FVector Mid = Center + N * Half[Axis];
				const FVector Du = Axes[U] * Half[U];
				const FVector Dw = Axes[W] * Half[W];
				const float Su = Half[U] * 2.0f * UVScale;
				const float Sw = Half[W] * 2.0f * UVScale;
				const int32 A = Vert(B, Mid - Du - Dw, N, FVector2D(0.0f, Sw));
				const int32 Bv = Vert(B, Mid + Du - Dw, N, FVector2D(Su, Sw));
				const int32 C = Vert(B, Mid + Du + Dw, N, FVector2D(Su, 0.0f));
				const int32 D = Vert(B, Mid - Du + Dw, N, FVector2D(0.0f, 0.0f));
				Quad(B, A, Bv, C, D, N);
			}
		}
	}

	/** A flat rectangle: Center, its two half-sides, facing Normal; UVs 0-1 (or tiled). */
	void Panel(FHallBatch& B, const FVector& Center, const FVector& HalfU, const FVector& HalfV, const FVector& Normal,
		const FVector2D& UVFrom = FVector2D(0.0, 0.0), const FVector2D& UVTo = FVector2D(1.0, 1.0))
	{
		const int32 A = Vert(B, Center - HalfU + HalfV, Normal, FVector2D(UVFrom.X, UVFrom.Y));
		const int32 Bv = Vert(B, Center + HalfU + HalfV, Normal, FVector2D(UVTo.X, UVFrom.Y));
		const int32 C = Vert(B, Center + HalfU - HalfV, Normal, FVector2D(UVTo.X, UVTo.Y));
		const int32 D = Vert(B, Center - HalfU - HalfV, Normal, FVector2D(UVFrom.X, UVTo.Y));
		Quad(B, A, Bv, C, D, Normal);
	}

	/**
	 * A profile turned round an axis (a lathe): Profile is (radius, height) from bottom to top round the outside (so its
	 * normals point out); a bend under SmoothDegrees is smoothed, a sharper one stays a crisp edge. Flutes (count > 0)
	 * groove the parts between FluteFrom and FluteTo high.
	 */
	void Lathe(FHallBatch& B, const TArray<FVector2D>& Profile, const FTransform& Where, const int32 Segments, const float UVScale,
		const int32 Flutes = 0, const float FluteDepth = 0.0f, const float FluteFrom = 0.0f, const float FluteTo = 0.0f,
		const float SmoothDegrees = 35.0f)
	{
		const int32 Count = Profile.Num();
		if (Count < 2)
		{
			return;
		}
		// Each point's way along the profile: its segment's, or (a smooth bend) the two segments' together.
		TArray<FVector2D> SegDir;
		for (int32 K = 0; K + 1 < Count; ++K)
		{
			SegDir.Add((Profile[K + 1] - Profile[K]).GetSafeNormal());
		}
		const float CosSmooth = FMath::Cos(FMath::DegreesToRadians(SmoothDegrees));
		float Along = 0.0f;
		const auto Groove = [&](const float Theta, const float Z, float& OutM, float& OutDM)
		{
			OutM = 1.0f;
			OutDM = 0.0f;
			if (Flutes > 0 && Z >= FluteFrom && Z <= FluteTo)
			{
				// Each flute a rounded groove (cos^3 keeps the ridges between them narrow and crisp).
				const float C = 0.5f + 0.5f * FMath::Cos(Flutes * Theta);
				OutM = 1.0f - FluteDepth * C * C * C;
				OutDM = FluteDepth * 3.0f * C * C * 0.5f * Flutes * FMath::Sin(Flutes * Theta);
			}
		};
		for (int32 K = 0; K + 1 < Count; ++K)
		{
			const FVector2D P0 = Profile[K];
			const FVector2D P1 = Profile[K + 1];
			const float Length = static_cast<float>(FVector2D::Distance(P0, P1));
			if (Length < 0.01f)
			{
				continue;
			}
			const FVector2D Dir0 = K > 0 && FVector2D::DotProduct(SegDir[K - 1], SegDir[K]) > CosSmooth ? (SegDir[K - 1] + SegDir[K]).GetSafeNormal() : SegDir[K];
			const FVector2D Dir1 = K + 2 < Count && FVector2D::DotProduct(SegDir[K], SegDir[K + 1]) > CosSmooth ? (SegDir[K] + SegDir[K + 1]).GetSafeNormal() : SegDir[K];
			const int32 Base = B.V.Num();
			for (int32 Row = 0; Row < 2; ++Row)
			{
				const FVector2D P = Row == 0 ? P0 : P1;
				const FVector2D D = Row == 0 ? Dir0 : Dir1;
				for (int32 J = 0; J <= Segments; ++J)
				{
					const float Theta = UE_TWO_PI * J / Segments;
					const float Cos = FMath::Cos(Theta);
					const float Sin = FMath::Sin(Theta);
					float M;
					float DM;
					Groove(Theta, static_cast<float>(P.Y), M, DM);
					const float R = static_cast<float>(P.X) * M;
					const FVector Local(R * Cos, R * Sin, P.Y);
					// Normal: across the profile's way and round the turn.
					const FVector Ts(D.X * M * Cos, D.X * M * Sin, D.Y);
					const FVector Tt(P.X * DM * Cos - R * Sin, P.X * DM * Sin + R * Cos, 0.0f);
					FVector N = FVector::CrossProduct(Tt, Ts);
					const FVector Out(D.Y * Cos, D.Y * Sin, -D.X);
					if (N.IsNearlyZero())
					{
						N = Out;
					}
					if (FVector::DotProduct(N, Out) < 0.0f)
					{
						N = -N;
					}
					const FVector2D UV(Theta / UE_TWO_PI * UE_TWO_PI * FMath::Max(static_cast<float>(P.X), 20.0f) * UVScale,
						(Along + (Row == 0 ? 0.0f : Length)) * UVScale);
					Vert(B, Where.TransformPosition(Local), Where.TransformVectorNoScale(N), UV);
				}
			}
			for (int32 J = 0; J < Segments; ++J)
			{
				const int32 A = Base + J;
				const int32 Bv = Base + J + 1;
				const int32 C = Base + Segments + 1 + J + 1;
				const int32 D = Base + Segments + 1 + J;
				const FVector N = B.N[A] + B.N[Bv] + B.N[C] + B.N[D];
				Quad(B, A, Bv, C, D, N);
			}
			Along += Length;
		}
	}

	/**
	 * A profile (Out, Up) swept along a path: each path point has its own Out and Up (the profile's frame there). The
	 * profile runs from the wall's foot round the outside to its top (normals out); sharp bends stay crisp.
	 */
	void Sweep(FHallBatch& B, const TArray<FVector2D>& Profile, const TArray<FVector>& Path, const TArray<FVector>& Outs,
		const TArray<FVector>& Ups, const float UVScale, const float SmoothDegrees = 35.0f)
	{
		const int32 Count = Profile.Num();
		if (Count < 2 || Path.Num() < 2)
		{
			return;
		}
		TArray<FVector2D> SegDir;
		for (int32 K = 0; K + 1 < Count; ++K)
		{
			SegDir.Add((Profile[K + 1] - Profile[K]).GetSafeNormal());
		}
		const float CosSmooth = FMath::Cos(FMath::DegreesToRadians(SmoothDegrees));
		TArray<float> PathAlong;
		PathAlong.Add(0.0f);
		for (int32 I = 1; I < Path.Num(); ++I)
		{
			PathAlong.Add(PathAlong.Last() + static_cast<float>(FVector::Dist(Path[I], Path[I - 1])));
		}
		float Across = 0.0f;
		for (int32 K = 0; K + 1 < Count; ++K)
		{
			const FVector2D P0 = Profile[K];
			const FVector2D P1 = Profile[K + 1];
			const float Length = static_cast<float>(FVector2D::Distance(P0, P1));
			if (Length < 0.01f)
			{
				continue;
			}
			const FVector2D Dir0 = K > 0 && FVector2D::DotProduct(SegDir[K - 1], SegDir[K]) > CosSmooth ? (SegDir[K - 1] + SegDir[K]).GetSafeNormal() : SegDir[K];
			const FVector2D Dir1 = K + 2 < Count && FVector2D::DotProduct(SegDir[K], SegDir[K + 1]) > CosSmooth ? (SegDir[K] + SegDir[K + 1]).GetSafeNormal() : SegDir[K];
			const int32 Base = B.V.Num();
			for (int32 I = 0; I < Path.Num(); ++I)
			{
				for (int32 Row = 0; Row < 2; ++Row)
				{
					const FVector2D P = Row == 0 ? P0 : P1;
					const FVector2D D = Row == 0 ? Dir0 : Dir1;
					const FVector Pos = Path[I] + Outs[I] * P.X + Ups[I] * P.Y;
					const FVector N = Outs[I] * D.Y - Ups[I] * D.X;
					Vert(B, Pos, N, FVector2D(PathAlong[I] * UVScale, (Across + (Row == 0 ? 0.0f : Length)) * UVScale));
				}
			}
			for (int32 I = 0; I + 1 < Path.Num(); ++I)
			{
				const int32 A = Base + I * 2;
				const int32 Bv = Base + (I + 1) * 2;
				const int32 C = Base + (I + 1) * 2 + 1;
				const int32 D = Base + I * 2 + 1;
				Quad(B, A, Bv, C, D, B.N[A] + B.N[C]);
			}
			Across += Length;
		}
	}

	/** A straight moulding along From-To against a wall facing Out. */
	void Moulding(FHallBatch& B, const TArray<FVector2D>& Profile, const FVector& From, const FVector& To, const FVector& Out, const float UVScale)
	{
		Sweep(B, Profile, {From, To}, {Out, Out}, {FVector::UpVector, FVector::UpVector}, UVScale);
	}

	/** A ring (a tube round a circle) of Radius in the plane facing Axis, the tube's cross-section Tube. */
	void Ring(FHallBatch& B, const FVector& Center, const FVector& Axis, const float Radius, const float Tube, const float UVScale,
		const int32 Segments = 48, const int32 Sides = 10)
	{
		TArray<FVector2D> Circle;
		for (int32 S = 0; S <= Sides; ++S)
		{
			// (From the inside bottom round the outside: normals out.)
			const float A = -UE_HALF_PI - UE_TWO_PI * S / Sides;
			Circle.Add(FVector2D(FMath::Cos(A) * Tube, FMath::Sin(A) * Tube));
		}
		const FVector Ax = Axis.GetSafeNormal();
		FVector U = FVector::CrossProduct(Ax, FMath::Abs(Ax.Z) < 0.9f ? FVector::UpVector : FVector::ForwardVector).GetSafeNormal();
		const FVector W = FVector::CrossProduct(Ax, U);
		TArray<FVector> Path;
		TArray<FVector> Outs;
		TArray<FVector> Ups;
		for (int32 I = 0; I <= Segments; ++I)
		{
			const float A = UE_TWO_PI * I / Segments;
			const FVector Radial = U * FMath::Cos(A) + W * FMath::Sin(A);
			Path.Add(Center + Radial * Radius);
			Outs.Add(Radial);
			Ups.Add(Ax);
		}
		Sweep(B, Circle, Path, Outs, Ups, UVScale, 60.0f);
	}

	void Sphere(FHallBatch& B, const FVector& Center, const float Radius, const float UVScale, const int32 Segments = 16, const int32 Rings = 10,
		const float Squash = 1.0f)
	{
		TArray<FVector2D> Arc;
		for (int32 R = 0; R <= Rings; ++R)
		{
			const float A = -UE_HALF_PI + UE_PI * R / Rings;
			Arc.Add(FVector2D(FMath::Cos(A) * Radius, FMath::Sin(A) * Radius * Squash));
		}
		Arc[0].X = 0.0f;
		Arc.Last().X = 0.0f;
		Lathe(B, Arc, FTransform(Center), Segments, UVScale, 0, 0.0f, 0.0f, 0.0f, 80.0f);
	}

	/** A surface from a function of (u, v) in 0-1, its normals from its shape; Front: which side faces out. */
	void SurfaceGrid(FHallBatch& B, const int32 Nu, const int32 Nv, const TFunctionRef<FVector(float, float)> Shape, const FVector& Front,
		const FVector2D& UVScale = FVector2D(1.0, 1.0))
	{
		const int32 Base = B.V.Num();
		for (int32 J = 0; J <= Nv; ++J)
		{
			for (int32 I = 0; I <= Nu; ++I)
			{
				const float U = static_cast<float>(I) / Nu;
				const float V = static_cast<float>(J) / Nv;
				const float E = 0.002f;
				const FVector P = Shape(U, V);
				const FVector Du = Shape(FMath::Min(U + E, 1.0f), V) - Shape(FMath::Max(U - E, 0.0f), V);
				const FVector Dv = Shape(U, FMath::Min(V + E, 1.0f)) - Shape(U, FMath::Max(V - E, 0.0f));
				FVector N = FVector::CrossProduct(Du, Dv);
				if (FVector::DotProduct(N, Front) < 0.0f)
				{
					N = -N;
				}
				Vert(B, P, N, FVector2D(U * UVScale.X, V * UVScale.Y));
			}
		}
		for (int32 J = 0; J < Nv; ++J)
		{
			for (int32 I = 0; I < Nu; ++I)
			{
				const int32 A = Base + J * (Nu + 1) + I;
				Quad(B, A, A + 1, A + Nu + 2, A + Nu + 1, B.N[A]);
			}
		}
	}

	/** A round window's glass: a disc facing Normal, its texture's middle at its middle (a fan). */
	void Disc(FHallBatch& B, const FVector& Center, const FVector& Normal, const FVector& Up, const float Radius, const int32 Segments = 64)
	{
		const FVector Right = FVector::CrossProduct(Up, Normal).GetSafeNormal();
		const int32 Middle = Vert(B, Center, Normal, FVector2D(0.5, 0.5));
		int32 Last = INDEX_NONE;
		for (int32 I = 0; I <= Segments; ++I)
		{
			const float A = UE_TWO_PI * I / Segments;
			const float C = FMath::Cos(A);
			const float S = FMath::Sin(A);
			const int32 Here = Vert(B, Center + Right * C * Radius + Up * S * Radius, Normal, FVector2D(0.5f + 0.5f * C, 0.5f - 0.5f * S));
			if (Last != INDEX_NONE)
			{
				Tri(B, Middle, Last, Here, Normal);
			}
			Last = Here;
		}
	}

	// The profiles (cm).
	const TArray<FVector2D>& ColumnBase()
	{
		// An Attic base: plinth, a torus, a scotia (hollow), a smaller torus, then up into the shaft.
		static const TArray<FVector2D> P = {{0, 0}, {92, 0}, {92, 16}, {88, 18}, {90, 24}, {86, 31}, {80, 34}, {76, 36}, {74, 42}, {77, 46},
			{78, 50}, {74, 55}, {68, 57}, {66, 60}};
		return P;
	}

	const TArray<FVector2D>& ColumnCapital()
	{
		// A Roman Doric-like capital: astragal, neck, the echinus swelling out, the abacus.
		static const TArray<FVector2D> P = {{0, 0}, {58, 0}, {63, 4}, {63, 10}, {58, 14}, {57, 30}, {60, 34}, {66, 40}, {74, 48}, {80, 58},
			{84, 66}, {96, 66}, {96, 86}, {90, 90}, {0, 90}};
		return P;
	}

	const TArray<FVector2D>& Cornice()
	{
		// A crown cornice against a wall: fillet, cyma, corona, a cove into the wall above.
		static const TArray<FVector2D> P = {{0, 0}, {6, 0}, {6, 8}, {10, 10}, {16, 16}, {22, 26}, {30, 34}, {36, 36}, {36, 48}, {42, 50}, {42, 62},
			{32, 66}, {18, 72}, {8, 80}, {0, 86}};
		return P;
	}

	const TArray<FVector2D>& Skirting()
	{
		static const TArray<FVector2D> P = {{0, 0}, {5, 0}, {5, 22}, {7, 24}, {7, 28}, {3, 32}, {0, 34}};
		return P;
	}

	const TArray<FVector2D>& DadoRail()
	{
		static const TArray<FVector2D> P = {{0, 0}, {3, 1}, {6, 4}, {7, 8}, {6, 12}, {3, 15}, {0, 16}};
		return P;
	}

	const TArray<FVector2D>& Baluster()
	{
		static const TArray<FVector2D> P = {{0, 0}, {11, 0}, {11, 8}, {8, 10}, {7, 14}, {10, 22}, {13, 34}, {12, 44}, {7, 54}, {5, 60}, {8, 64},
			{8, 68}, {5, 70}, {0, 70}};
		return P;
	}

	const TArray<FVector2D>& ChandelierBody()
	{
		static const TArray<FVector2D> P = {{0, -40}, {8, -36}, {14, -24}, {40, -6}, {58, 8}, {60, 16}, {44, 22}, {22, 40}, {16, 70}, {24, 92},
			{20, 110}, {10, 120}, {8, 170}, {0, 172}};
		return P;
	}

	FTransform At(const FVector& P, const float Yaw = 0.0f, const float Scale = 1.0f)
	{
		return FTransform(FRotator(0.0f, Yaw, 0.0f), P, FVector(Scale));
	}

	// The batches by look and by which group they belong to (always shown, the scenes' only, a chandelier's bulbs...).
	enum class ELook : uint8 { Marble, DarkMarble, Floor, Gold, Carpet, Velvet, Wallpaper, Wood, Rose, Window, Banner, Painting0, Painting1, Painting2,
		Bulb, Crystal, Ceiling, Count };
	enum class EGroup : uint8 { Always, Cinematic, Collide, Chandelier0, Chandelier1, Chandelier2, DoorLeft, DoorRight, Shafts, Count };

	/**
	 * Every batch made up front, one per look and group (a reference to one stays good however many others are filled:
	 * a map growing would move them).
	 */
	struct FHallBuild
	{
		static constexpr int32 Looks = static_cast<int32>(ELook::Count);
		static constexpr int32 Groups = static_cast<int32>(EGroup::Count);
		TArray<FHallBatch> Batches;
		FHallBatch Shaft;
		FHallBuild() { Batches.SetNum(Looks * Groups); }
		FHallBatch& Get(const ELook Look, const EGroup Group = EGroup::Always)
		{
			return Batches[static_cast<int32>(Group) * Looks + static_cast<int32>(Look)];
		}
	};
}

// ---------------------------------------------------------------------------------------------------- The actor

AChaosImpactWhiteHall::AChaosImpactWhiteHall()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

AChaosImpactWhiteHall* AChaosImpactWhiteHall::Find(const UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AChaosImpactWhiteHall> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

void AChaosImpactWhiteHall::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	// (Built before anything begins play: the floor is there when the room puts its boss on it.)
	if (GetWorld() && GetWorld()->IsGameWorld() && !bBuilt)
	{
		Build();
	}
}

void AChaosImpactWhiteHall::BeginPlay()
{
	Super::BeginPlay();
	if (!bBuilt)
	{
		Build();
	}
	SetCinematic(false);
}

UMaterialInstanceDynamic* AChaosImpactWhiteHall::Surface(UTexture* Texture, const FLinearColor& Tint, const float Rough, const float Metal, const float Glow,
	const float Spec)
{
	UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/ChaosImpact/solo/Stage30/M_CI_HallSurface.M_CI_HallSurface"));
	if (!Parent)
	{
		Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/LevelPrototyping/Materials/M_FlatCol.M_FlatCol"));
	}
	UMaterialInstanceDynamic* Look = UMaterialInstanceDynamic::Create(Parent, this);
	if (Texture)
	{
		Look->SetTextureParameterValue(TEXT("Tex"), Texture);
	}
	Look->SetVectorParameterValue(TEXT("Tint"), Tint);
	Look->SetVectorParameterValue(TEXT("Color"), Tint);
	Look->SetScalarParameterValue(TEXT("Rough"), Rough);
	Look->SetScalarParameterValue(TEXT("Metal"), Metal);
	Look->SetScalarParameterValue(TEXT("Glow"), Glow);
	Look->SetScalarParameterValue(TEXT("Spec"), Spec);
	return Look;
}

UPointLightComponent* AChaosImpactWhiteHall::AddPoint(const FVector& At, const FLinearColor& Color, const float Candela, const float Radius)
{
	UPointLightComponent* Light = NewObject<UPointLightComponent>(this);
	Light->SetupAttachment(GetRootComponent());
	Light->RegisterComponent();
	Light->SetWorldLocation(At);
	Light->SetMobility(EComponentMobility::Movable);
	Light->SetIntensityUnits(ELightUnits::Candelas);
	Light->SetIntensity(Candela);
	Light->SetLightColor(Color);
	Light->SetAttenuationRadius(Radius);
	Light->SetCastShadows(false);
	Light->SetSourceRadius(30.0f);
	return Light;
}

UProceduralMeshComponent* AChaosImpactWhiteHall::MakePart(const FName Name, const int32 Batch, const bool bCollide)
{
	UProceduralMeshComponent* Part = NewObject<UProceduralMeshComponent>(this, Name);
	Part->SetupAttachment(GetRootComponent());
	Part->RegisterComponent();
	Part->SetCollisionEnabled(bCollide ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	if (bCollide)
	{
		Part->SetCollisionObjectType(ECC_WorldStatic);
		Part->SetCollisionResponseToAllChannels(ECR_Block);
	}
	Part->bUseAsyncCooking = false;
	(void)Batch;
	return Part;
}

void AChaosImpactWhiteHall::Build()
{
	bBuilt = true;
	FHallBuild H;
	const auto Tex = [](const TCHAR* Name) -> UTexture*
	{
		const FString Path = FString::Printf(TEXT("/Game/ChaosImpact/solo/Stage30/Textures/%s.%s"), Name, Name);
		return LoadObject<UTexture>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	};
	// Its looks.
	UMaterialInstanceDynamic* Looks[static_cast<int32>(ELook::Count)] = {};
	Looks[static_cast<int32>(ELook::Marble)] = Surface(Tex(TEXT("T_Hall_Marble")), FLinearColor(1.0f, 1.0f, 0.98f), 0.22f, 0.0f, 0.0f, 0.6f);
	Looks[static_cast<int32>(ELook::DarkMarble)] = Surface(Tex(TEXT("T_Hall_Marble")), FLinearColor(0.09f, 0.085f, 0.1f), 0.18f, 0.0f, 0.0f, 0.6f);
	Looks[static_cast<int32>(ELook::Floor)] = Surface(Tex(TEXT("T_Hall_FloorCheck")), FLinearColor(1.0f, 1.0f, 1.0f), 0.1f, 0.0f, 0.0f, 0.7f);
	Looks[static_cast<int32>(ELook::Gold)] = Surface(Tex(TEXT("T_Hall_Gold")), FLinearColor(1.0f, 0.72f, 0.3f), 0.3f, 1.0f, 0.0f);
	Looks[static_cast<int32>(ELook::Carpet)] = Surface(Tex(TEXT("T_Hall_Carpet")), FLinearColor(1.0f, 1.0f, 1.0f), 0.95f, 0.0f, 0.0f, 0.2f);
	Looks[static_cast<int32>(ELook::Velvet)] = Surface(Tex(TEXT("T_Hall_Velvet")), FLinearColor(1.0f, 1.0f, 1.0f), 0.7f, 0.0f, 0.0f, 0.3f);
	Looks[static_cast<int32>(ELook::Wallpaper)] = Surface(Tex(TEXT("T_Hall_Wallpaper")), FLinearColor(1.0f, 0.98f, 0.95f), 0.8f, 0.0f, 0.0f, 0.3f);
	Looks[static_cast<int32>(ELook::Wood)] = Surface(Tex(TEXT("T_Hall_Wood")), FLinearColor(1.0f, 1.0f, 1.0f), 0.42f, 0.0f, 0.0f, 0.5f);
	RoseLook = Surface(Tex(TEXT("T_Hall_Rose")), FLinearColor(1.0f, 1.0f, 1.0f), 0.2f, 0.0f, 5.0f);
	Looks[static_cast<int32>(ELook::Rose)] = RoseLook;
	WindowLook = Surface(Tex(TEXT("T_Hall_Window")), FLinearColor(1.0f, 1.0f, 1.0f), 0.1f, 0.0f, 2.4f);
	Looks[static_cast<int32>(ELook::Window)] = WindowLook;
	Looks[static_cast<int32>(ELook::Banner)] = Surface(Tex(TEXT("T_Hall_Banner")), FLinearColor(1.0f, 1.0f, 1.0f), 0.8f, 0.0f, 0.0f, 0.3f);
	Looks[static_cast<int32>(ELook::Painting0)] = Surface(Tex(TEXT("T_Hall_Painting0")), FLinearColor(1.0f, 1.0f, 1.0f), 0.55f, 0.0f, 0.05f);
	Looks[static_cast<int32>(ELook::Painting1)] = Surface(Tex(TEXT("T_Hall_Painting1")), FLinearColor(1.0f, 1.0f, 1.0f), 0.55f, 0.0f, 0.05f);
	Looks[static_cast<int32>(ELook::Painting2)] = Surface(Tex(TEXT("T_Hall_Painting2")), FLinearColor(1.0f, 1.0f, 1.0f), 0.55f, 0.0f, 0.05f);
	SconceLook = Surface(Tex(TEXT("T_Hall_Gold")), FLinearColor(1.0f, 0.86f, 0.62f), 0.3f, 0.0f, 9.0f);
	Looks[static_cast<int32>(ELook::Bulb)] = SconceLook;
	Looks[static_cast<int32>(ELook::Crystal)] = Surface(Tex(TEXT("T_Hall_Marble")), FLinearColor(0.85f, 0.93f, 1.0f), 0.05f, 0.0f, 0.9f, 1.0f);
	Looks[static_cast<int32>(ELook::Ceiling)] = Surface(Tex(TEXT("T_Hall_Wallpaper")), FLinearColor(0.95f, 0.9f, 0.82f), 0.8f, 0.0f, 0.0f, 0.3f);

	const float UvMarble = 1.0f / 300.0f;
	const float UvFloor = 1.0f / 200.0f;
	const float UvGold = 1.0f / 100.0f;
	const float UvPaper = 1.0f / 160.0f;
	const float UvWood = 1.0f / 150.0f;

	// ---- Floors (they carry the floor's collision).
	{
		FHallBatch& Floor = H.Get(ELook::Floor, EGroup::Collide);
		const float CorridorLength = DoorX - CorridorStartX;
		Box(Floor, FVector(CorridorStartX + CorridorLength * 0.5f, 0.0f, -30.0f), FVector(CorridorLength, CorridorHalfWidth * 2.0f, 60.0f), UvFloor);
		const float HallLength = FarWallX - DoorX;
		Box(Floor, FVector(DoorX + HallLength * 0.5f, 0.0f, -30.0f), FVector(HallLength, HallHalfWidth * 2.0f, 60.0f), UvFloor);
	}
	// The red runners: down the corridor, up the hall to the dais and over its steps.
	const auto Runner = [&H](const float X0, const float X1, const float Width, const float Z)
	{
		FHallBatch& Carpet = H.Get(ELook::Carpet);
		const float Length = X1 - X0;
		Panel(Carpet, FVector((X0 + X1) * 0.5f, 0.0f, Z), FVector(0.0f, Width * 0.5f, 0.0f), FVector(Length * 0.5f, 0.0f, 0.0f), FVector::UpVector,
			FVector2D(0.0, 0.0), FVector2D(1.0, Length / Width));
		// (Its edges a little proud of the floor.)
		Box(Carpet, FVector((X0 + X1) * 0.5f, -Width * 0.5f, Z - 0.8f), FVector(Length, 2.0f, 1.6f), 1.0f / Width);
		Box(Carpet, FVector((X0 + X1) * 0.5f, Width * 0.5f, Z - 0.8f), FVector(Length, 2.0f, 1.6f), 1.0f / Width);
	};
	Runner(CorridorStartX + 150.0f, DoorX, 380.0f, 1.6f);
	Runner(DoorX, DaisX, 560.0f, 1.6f);

	// ---- The corridor: wainscot, wallpaper, pilasters, portraits of balls, sconces, a cornice.
	for (const float Side : {-1.0f, 1.0f})
	{
		const float WallY = Side * CorridorHalfWidth;
		const FVector In(0.0f, -Side, 0.0f);
		const float X0 = CorridorStartX;
		const float X1 = DoorX - 30.0f;
		const float Length = X1 - X0;
		Box(H.Get(ELook::Wood), FVector(X0 + Length * 0.5f, WallY + Side * 10.0f, 75.0f), FVector(Length, 20.0f, 150.0f), UvWood);
		Box(H.Get(ELook::Wallpaper), FVector(X0 + Length * 0.5f, WallY + Side * 25.0f, 150.0f + (CorridorHeight - 150.0f) * 0.5f),
			FVector(Length, 10.0f, CorridorHeight - 150.0f), UvPaper);
		Moulding(H.Get(ELook::Wood), Skirting(), FVector(X0, WallY, 0.0f), FVector(X1, WallY, 0.0f), In, UvWood);
		Moulding(H.Get(ELook::Gold), DadoRail(), FVector(X0, WallY, 148.0f), FVector(X1, WallY, 148.0f), In, UvGold);
		Moulding(H.Get(ELook::Marble), Cornice(), FVector(X0, WallY + Side * 20.0f, CorridorHeight - 86.0f), FVector(X1, WallY + Side * 20.0f, CorridorHeight - 86.0f),
			In, UvMarble);
		int32 Painting = 0;
		for (float X = X0 + 300.0f; X < X1 - 100.0f; X += 600.0f)
		{
			// A pilaster: a flat column with a plinth and a capital.
			Box(H.Get(ELook::Marble), FVector(X, WallY - Side * 12.0f, CorridorHeight * 0.5f), FVector(90.0f, 24.0f, CorridorHeight), UvMarble);
			Box(H.Get(ELook::DarkMarble), FVector(X, WallY - Side * 18.0f, 50.0f), FVector(110.0f, 36.0f, 100.0f), UvMarble);
			Box(H.Get(ELook::Gold), FVector(X, WallY - Side * 20.0f, CorridorHeight - 130.0f), FVector(108.0f, 40.0f, 18.0f), UvGold);
			if (X + 300.0f < X1 - 150.0f)
			{
				// Between two: a portrait of a ball in a gold frame, a sconce either side of it.
				const float PX = X + 300.0f;
				const ELook Art = static_cast<ELook>(static_cast<int32>(ELook::Painting0) + (Painting++ % 3));
				const FVector Face(PX, WallY - Side * 34.0f, 560.0f);
				Panel(H.Get(Art), Face, FVector(Side * 120.0f, 0.0f, 0.0f) * -1.0f, FVector(0.0f, 0.0f, 160.0f), In);
				FHallBatch& Gold = H.Get(ELook::Gold);
				Box(Gold, Face + FVector(0.0f, Side * 4.0f, 172.0f), FVector(268.0f, 14.0f, 24.0f), UvGold);
				Box(Gold, Face + FVector(0.0f, Side * 4.0f, -172.0f), FVector(268.0f, 14.0f, 24.0f), UvGold);
				Box(Gold, Face + FVector(-132.0f, Side * 4.0f, 0.0f), FVector(24.0f, 14.0f, 368.0f), UvGold);
				Box(Gold, Face + FVector(132.0f, Side * 4.0f, 0.0f), FVector(24.0f, 14.0f, 368.0f), UvGold);
				for (const float Off : {-190.0f, 190.0f})
				{
					const FVector Arm(PX + Off, WallY - Side * 30.0f, 700.0f);
					Box(Gold, Arm, FVector(10.0f, 40.0f, 10.0f), UvGold);
					Lathe(Gold, {{0, 0}, {9, 0}, {12, 6}, {6, 14}, {0, 14}}, At(Arm + FVector(0.0f, -Side * 24.0f, 0.0f)), 12, UvGold);
					Sphere(H.Get(ELook::Bulb), Arm + FVector(0.0f, -Side * 24.0f, 26.0f), 10.0f, UvGold, 10, 6, 1.4f);
				}
			}
		}
	}
	for (float X = CorridorStartX + 700.0f; X < DoorX; X += 1400.0f)
	{
		OtherLights.Add(AddPoint(FVector(X, 0.0f, 820.0f), FLinearColor(1.0f, 0.8f, 0.58f), 260.0f, 1700.0f));
	}
	// Its ceiling (the scenes'): a barrel of coffers.
	{
		FHallBatch& Ceiling = H.Get(ELook::Ceiling, EGroup::Cinematic);
		const float Length = DoorX - CorridorStartX;
		Box(Ceiling, FVector(CorridorStartX + Length * 0.5f, 0.0f, CorridorHeight + 20.0f), FVector(Length, CorridorHalfWidth * 2.0f + 60.0f, 40.0f), UvPaper, 0.0f, true);
		for (float X = CorridorStartX + 150.0f; X < DoorX; X += 300.0f)
		{
			Box(H.Get(ELook::Gold, EGroup::Cinematic), FVector(X, 0.0f, CorridorHeight - 6.0f), FVector(26.0f, CorridorHalfWidth * 2.0f, 14.0f), UvGold, 0.0f, true);
		}
	}

	// ---- The hall's side walls: wainscot, wallpaper, pilasters, tall night windows above the colonnade, cornices.
	const float HallX0 = DoorX;
	const float HallX1 = FarWallX;
	const float HallLength = HallX1 - HallX0;
	for (const float Side : {-1.0f, 1.0f})
	{
		const float WallY = Side * HallHalfWidth;
		const FVector In(0.0f, -Side, 0.0f);
		Box(H.Get(ELook::Wood), FVector(HallX0 + HallLength * 0.5f, WallY + Side * 10.0f, 90.0f), FVector(HallLength, 20.0f, 180.0f), UvWood);
		Box(H.Get(ELook::Wallpaper), FVector(HallX0 + HallLength * 0.5f, WallY + Side * 30.0f, 180.0f + (HallHeight - 180.0f) * 0.5f),
			FVector(HallLength, 10.0f, HallHeight - 180.0f), UvPaper);
		Moulding(H.Get(ELook::Wood), Skirting(), FVector(HallX0, WallY, 70.0f), FVector(HallX1, WallY, 70.0f), In, UvWood);
		Moulding(H.Get(ELook::Gold), DadoRail(), FVector(HallX0, WallY, 178.0f), FVector(HallX1, WallY, 178.0f), In, UvGold);
		Moulding(H.Get(ELook::Marble), Cornice(), FVector(HallX0, WallY + Side * 26.0f, HallHeight - 86.0f), FVector(HallX1, WallY + Side * 26.0f, HallHeight - 86.0f),
			In, UvMarble);
		Moulding(H.Get(ELook::Marble), Cornice(), FVector(HallX0, WallY + Side * 26.0f, ColumnHeight + 160.0f),
			FVector(HallX1, WallY + Side * 26.0f, ColumnHeight + 160.0f), In, UvMarble);
		for (int32 Bay = 0; Bay <= 8; ++Bay)
		{
			const float X = 1180.0f + Bay * 640.0f;
			if (X > HallX1 - 200.0f)
			{
				break;
			}
			// Pilasters up the wall, in line with the columns.
			Box(H.Get(ELook::Marble), FVector(X, WallY - Side * 16.0f, HallHeight * 0.5f), FVector(120.0f, 32.0f, HallHeight), UvMarble);
			Box(H.Get(ELook::DarkMarble), FVector(X, WallY - Side * 24.0f, 120.0f), FVector(150.0f, 50.0f, 100.0f), UvMarble);
			// Between them: a tall arched window up high, a banner of the United States of Dett Ball below it, sconces.
			const float WX = X + 320.0f;
			if (WX > HallX1 - 300.0f)
			{
				continue;
			}
			const FVector Glass(WX, WallY - Side * 36.0f, 2450.0f);
			Panel(H.Get(ELook::Window), Glass, FVector(150.0f, 0.0f, 0.0f), FVector(0.0f, 0.0f, 400.0f), In, FVector2D(0.0, 0.25), FVector2D(1.0, 1.0));
			{
				// Its round head.
				FHallBatch& Top = H.Get(ELook::Window);
				const FVector Spring = Glass + FVector(0.0f, 0.0f, 400.0f);
				const int32 Middle = Vert(Top, Spring, In, FVector2D(0.5, 0.25));
				int32 Last = INDEX_NONE;
				for (int32 I = 0; I <= 16; ++I)
				{
					const float A = UE_PI * I / 16;
					const int32 Here = Vert(Top, Spring + FVector(FMath::Cos(A) * 150.0f, 0.0f, FMath::Sin(A) * 150.0f), In,
						FVector2D(0.5f + 0.5f * FMath::Cos(A), 0.25f - 0.25f * FMath::Sin(A)));
					if (Last != INDEX_NONE)
					{
						Tri(Top, Middle, Last, Here, In);
					}
					Last = Here;
				}
				// The frame round it: gold, up the sides and round the arch.
				TArray<FVector2D> FrameProfile = {{0, -9}, {8, -9}, {12, -5}, {12, 5}, {8, 9}, {0, 9}};
				TArray<FVector> Path;
				TArray<FVector> Outs;
				TArray<FVector> Ups;
				Path.Add(Glass + FVector(-150.0f, 0.0f, -400.0f));
				for (int32 I = 0; I <= 20; ++I)
				{
					const float A = UE_PI - UE_PI * I / 20;
					Path.Add(Spring + FVector(FMath::Cos(A) * 150.0f, 0.0f, FMath::Sin(A) * 150.0f));
				}
				Path.Add(Glass + FVector(150.0f, 0.0f, -400.0f));
				for (int32 I = 0; I < Path.Num(); ++I)
				{
					Outs.Add(In);
					const FVector Next = Path[FMath::Min(I + 1, Path.Num() - 1)] - Path[FMath::Max(I - 1, 0)];
					Ups.Add(FVector::CrossProduct(In, Next.GetSafeNormal()).GetSafeNormal() * -1.0f);
				}
				Sweep(H.Get(ELook::Gold), FrameProfile, Path, Outs, Ups, UvGold);
				Box(H.Get(ELook::Marble), Glass + FVector(0.0f, -Side * 6.0f, -430.0f), FVector(360.0f, 50.0f, 40.0f), UvMarble);
			}
			const FVector Flag(WX, WallY - Side * 40.0f, 1250.0f);
			Panel(H.Get(ELook::Banner), Flag, FVector(130.0f, 0.0f, 0.0f), FVector(0.0f, 0.0f, 330.0f), In);
			Box(H.Get(ELook::Gold), Flag + FVector(0.0f, -Side * 8.0f, 342.0f), FVector(300.0f, 10.0f, 10.0f), UvGold);
			for (const float Ball : {-150.0f, 150.0f})
			{
				Sphere(H.Get(ELook::Gold), Flag + FVector(Ball, -Side * 8.0f, 342.0f), 12.0f, UvGold, 10, 6);
			}
		}
	}

	// ---- The colonnade: fluted columns on pedestals down each side, an entablature over them, the gallery behind.
	for (const float Side : {-1.0f, 1.0f})
	{
		const float Y = Side * ColumnY;
		// The gallery's raised floor behind the columns (the soldiers stand on it), with a marble edge.
		const float GX0 = DoorX + 120.0f;
		const float GX1 = DaisX - 60.0f;
		Box(H.Get(ELook::Floor), FVector((GX0 + GX1) * 0.5f, Side * (ColumnY + (HallHalfWidth - ColumnY) * 0.5f - 60.0f), 35.0f),
			FVector(GX1 - GX0, HallHalfWidth - ColumnY + 120.0f, 70.0f), UvFloor);
		Moulding(H.Get(ELook::Marble), {{0, 0}, {10, 0}, {14, 4}, {14, 10}, {10, 14}, {0, 14}}, FVector(GX0, Side * (ColumnY - 180.0f), 56.0f),
			FVector(GX1, Side * (ColumnY - 180.0f), 56.0f), FVector(0.0f, -Side, 0.0f), UvMarble);
		for (int32 Index = 0; Index < 7; ++Index)
		{
			const float X = 1500.0f + Index * 640.0f;
			Box(H.Get(ELook::DarkMarble), FVector(X, Y, 60.0f), FVector(210.0f, 210.0f, 120.0f), UvMarble);
			Moulding(H.Get(ELook::Gold), {{0, 0}, {4, 0}, {6, 3}, {4, 6}, {0, 6}}, FVector(X - 105.0f, Y - 105.0f, 114.0f), FVector(X + 105.0f, Y - 105.0f, 114.0f),
				FVector(0.0f, -1.0f, 0.0f), UvGold);
			Moulding(H.Get(ELook::Gold), {{0, 0}, {4, 0}, {6, 3}, {4, 6}, {0, 6}}, FVector(X - 105.0f, Y + 105.0f, 114.0f), FVector(X + 105.0f, Y + 105.0f, 114.0f),
				FVector(0.0f, 1.0f, 0.0f), UvGold);
			Lathe(H.Get(ELook::Marble), ColumnBase(), At(FVector(X, Y, 120.0f)), 48, UvMarble);
			const float ShaftFrom = 180.0f;
			const float ShaftTo = ColumnHeight - 90.0f;
			// The shaft: twenty flutes, the slight swell (entasis) of a real column.
			TArray<FVector2D> Shaft;
			for (int32 Step = 0; Step <= 12; ++Step)
			{
				const float T = Step / 12.0f;
				const float R = 66.0f - 8.0f * T + 3.0f * FMath::Sin(T * UE_PI);
				Shaft.Add(FVector2D(R, ShaftFrom + (ShaftTo - ShaftFrom) * T));
			}
			Lathe(H.Get(ELook::Marble), Shaft, At(FVector(X, Y, 0.0f)), 120, UvMarble, 20, 0.07f, ShaftFrom + 10.0f, ShaftTo - 10.0f, 20.0f);
			Ring(H.Get(ELook::Gold), FVector(X, Y, ShaftTo - 2.0f), FVector::UpVector, 59.0f, 4.0f, UvGold, 40, 8);
			Lathe(H.Get(ELook::Marble), ColumnCapital(), At(FVector(X, Y, ShaftTo)), 48, UvMarble);
			Box(H.Get(ELook::Gold), FVector(X, Y, ShaftTo + 70.0f), FVector(196.0f, 196.0f, 8.0f), UvGold);
		}
		// The entablature: architrave, a frieze with a gold band, the cornice, running the colonnade's length.
		const float EX0 = 1180.0f;
		const float EX1 = 5600.0f;
		const float EZ = ColumnHeight;
		Box(H.Get(ELook::Marble), FVector((EX0 + EX1) * 0.5f, Y, EZ + 45.0f), FVector(EX1 - EX0, 200.0f, 90.0f), UvMarble);
		Box(H.Get(ELook::Marble), FVector((EX0 + EX1) * 0.5f, Y, EZ + 140.0f), FVector(EX1 - EX0, 184.0f, 100.0f), UvMarble);
		Box(H.Get(ELook::Gold), FVector((EX0 + EX1) * 0.5f, Y - Side * 93.0f, EZ + 140.0f), FVector(EX1 - EX0, 4.0f, 30.0f), UvGold);
		Moulding(H.Get(ELook::Marble), Cornice(), FVector(EX0, Y - Side * 92.0f, EZ + 190.0f), FVector(EX1, Y - Side * 92.0f, EZ + 190.0f),
			FVector(0.0f, -Side, 0.0f), UvMarble);
		Box(H.Get(ELook::Marble), FVector((EX0 + EX1) * 0.5f, Y, EZ + 250.0f), FVector(EX1 - EX0, 260.0f, 40.0f), UvMarble);
		// A balustrade along its top.
		for (float X = EX0 + 40.0f; X < EX1; X += 46.0f)
		{
			Lathe(H.Get(ELook::Marble), Baluster(), At(FVector(X, Y - Side * 100.0f, EZ + 270.0f), 0.0f, 1.1f), 10, UvMarble);
		}
		Box(H.Get(ELook::Marble), FVector((EX0 + EX1) * 0.5f, Y - Side * 100.0f, EZ + 355.0f), FVector(EX1 - EX0, 34.0f, 16.0f), UvMarble);
	}

	// ---- The far wall: the rose window, red velvet curtains either side of the throne, the great banners.
	{
		const FVector In(-1.0f, 0.0f, 0.0f);
		Box(H.Get(ELook::Wallpaper), FVector(FarWallX + 20.0f, 0.0f, HallHeight * 0.5f), FVector(40.0f, HallHalfWidth * 2.0f, HallHeight), UvPaper);
		Box(H.Get(ELook::Wood), FVector(FarWallX - 10.0f, 0.0f, 90.0f + DaisTop), FVector(20.0f, HallHalfWidth * 2.0f, 180.0f), UvWood);
		Moulding(H.Get(ELook::Marble), Cornice(), FVector(FarWallX, -HallHalfWidth, HallHeight - 86.0f), FVector(FarWallX, HallHalfWidth, HallHeight - 86.0f), In,
			UvMarble);
		const FVector Rose(FarWallX - 22.0f, 0.0f, RoseZ);
		Disc(H.Get(ELook::Rose), Rose, In, FVector::UpVector, RoseRadius, 96);
		Ring(H.Get(ELook::Gold), Rose + FVector(-6.0f, 0.0f, 0.0f), In, RoseRadius + 10.0f, 14.0f, UvGold, 96, 10);
		Ring(H.Get(ELook::Marble), Rose + FVector(-14.0f, 0.0f, 0.0f), In, RoseRadius + 58.0f, 34.0f, UvMarble, 96, 12);
		for (int32 Key = 0; Key < 16; ++Key)
		{
			// Gold studs round its marble frame.
			const float A = UE_TWO_PI * Key / 16;
			Sphere(H.Get(ELook::Gold), Rose + FVector(-46.0f, FMath::Cos(A) * (RoseRadius + 58.0f), FMath::Sin(A) * (RoseRadius + 58.0f)), 16.0f, UvGold, 10, 6);
		}
		for (const float Side : {-1.0f, 1.0f})
		{
			// A curtain: heavy pleats, gathered toward the wall at the bottom (tied back).
			const float Inner = Side * 330.0f;
			const float Outer = Side * 1250.0f;
			FHallBatch& Velvet = H.Get(ELook::Velvet);
			SurfaceGrid(Velvet, 60, 24, [=](const float U, const float V)
			{
				const float Z = 3050.0f - V * (3050.0f - DaisTop);
				const float Gather = FMath::Lerp(1.0f, 0.55f, FMath::Pow(FMath::Clamp((V - 0.55f) / 0.45f, 0.0f, 1.0f), 1.5f));
				const float Y = FMath::Lerp(Outer, Outer + (Inner - Outer) * Gather, U);
				const float Pleat = 28.0f * FMath::Sin(U * UE_TWO_PI * 9.0f) * (0.8f + 0.2f * V);
				return FVector(FarWallX - 90.0f + Pleat, Y, Z);
			}, In, FVector2D(4.5, 6.0));
			// Its pelmet and a gold tassel tie-back.
			Box(H.Get(ELook::Velvet), FVector(FarWallX - 70.0f, (Inner + Outer) * 0.5f, 3090.0f), FVector(70.0f, FMath::Abs(Outer - Inner) + 120.0f, 110.0f), 1.0f / 200.0f);
			Box(H.Get(ELook::Gold), FVector(FarWallX - 108.0f, (Inner + Outer) * 0.5f, 3038.0f), FVector(8.0f, FMath::Abs(Outer - Inner) + 120.0f, 12.0f), UvGold);
			Ring(H.Get(ELook::Gold), FVector(FarWallX - 90.0f, Inner + Side * 260.0f, 1150.0f), FVector::UpVector, 40.0f, 7.0f, UvGold, 24, 8);
			Sphere(H.Get(ELook::Gold), FVector(FarWallX - 120.0f, Inner + Side * 260.0f, 1100.0f), 22.0f, UvGold, 12, 8, 1.6f);
			// A great banner further out.
			const FVector Flag(FarWallX - 46.0f, Side * 1950.0f, 1700.0f);
			Panel(H.Get(ELook::Banner), Flag, FVector(0.0f, Side * 280.0f, 0.0f), FVector(0.0f, 0.0f, 900.0f), In);
			Box(H.Get(ELook::Gold), Flag + FVector(-10.0f, 0.0f, 920.0f), FVector(16.0f, 640.0f, 16.0f), UvGold);
			for (const float End : {-1.0f, 1.0f})
			{
				Sphere(H.Get(ELook::Gold), Flag + FVector(-10.0f, End * 320.0f, 920.0f), 20.0f, UvGold, 10, 6);
			}
		}
	}

	// ---- The dais and the throne of balls.
	{
		for (int32 Step = 0; Step < 4; ++Step)
		{
			const float X0 = DaisX + Step * 150.0f;
			const float Top = (Step + 1) * (DaisTop / 4.0f);
			const float Depth = FarWallX - X0;
			Box(H.Get(ELook::DarkMarble, EGroup::Collide), FVector(X0 + Depth * 0.5f, 0.0f, Top * 0.5f), FVector(Depth, 3000.0f - Step * 200.0f, Top), UvMarble);
			Moulding(H.Get(ELook::Marble), {{0, -10}, {6, -10}, {10, -5}, {10, 0}, {0, 0}}, FVector(X0, -(1500.0f - Step * 100.0f), Top),
				FVector(X0, 1500.0f - Step * 100.0f, Top), FVector(-1.0f, 0.0f, 0.0f), UvMarble);
			// The runner up each step.
			Panel(H.Get(ELook::Carpet), FVector(X0 + 75.0f, 0.0f, Top + 1.5f), FVector(0.0f, 280.0f, 0.0f), FVector(75.0f, 0.0f, 0.0f), FVector::UpVector,
				FVector2D(0.0, 0.0), FVector2D(1.0, 0.27));
			Panel(H.Get(ELook::Carpet), FVector(X0 - 1.5f, 0.0f, Top - DaisTop / 8.0f), FVector(0.0f, 280.0f, 0.0f), FVector(0.0f, 0.0f, DaisTop / 8.0f),
				FVector(-1.0f, 0.0f, 0.0f), FVector2D(0.0, 0.0), FVector2D(1.0, 0.1));
		}
		const FVector Seat(ThroneX, 0.0f, DaisTop);
		FHallBatch& Gold = H.Get(ELook::Gold);
		FHallBatch& Marble = H.Get(ELook::Marble);
		// Its plinth, the seat and its cushion.
		Box(Marble, Seat + FVector(0.0f, 0.0f, 50.0f), FVector(560.0f, 860.0f, 100.0f), UvMarble);
		Moulding(Gold, {{0, 0}, {5, 0}, {7, 4}, {5, 8}, {0, 8}}, Seat + FVector(-280.0f, -430.0f, 92.0f), Seat + FVector(-280.0f, 430.0f, 92.0f),
			FVector(-1.0f, 0.0f, 0.0f), UvGold);
		Box(Gold, Seat + FVector(0.0f, 0.0f, 150.0f), FVector(440.0f, 700.0f, 100.0f), UvGold);
		Box(H.Get(ELook::Velvet), Seat + FVector(-20.0f, 0.0f, 225.0f), FVector(400.0f, 640.0f, 50.0f), 1.0f / 200.0f);
		// The arms: gold posts with ball finials.
		for (const float Side : {-1.0f, 1.0f})
		{
			Lathe(Gold, {{0, 0}, {40, 0}, {40, 14}, {30, 20}, {24, 60}, {30, 150}, {36, 170}, {36, 186}, {0, 186}}, At(Seat + FVector(-160.0f, Side * 360.0f, 200.0f)), 24, UvGold);
			Sphere(Marble, Seat + FVector(-160.0f, Side * 360.0f, 440.0f), 52.0f, UvMarble, 20, 12);
			Box(Gold, Seat + FVector(20.0f, Side * 360.0f, 360.0f), FVector(360.0f, 50.0f, 30.0f), UvGold);
		}
		// The back: a fan of balls, white marble and gold, stacked up behind the seat; a great gold ball on top.
		const FVector Back = Seat + FVector(220.0f, 0.0f, 250.0f);
		Box(H.Get(ELook::Velvet), Back + FVector(-10.0f, 0.0f, 380.0f), FVector(40.0f, 560.0f, 760.0f), 1.0f / 200.0f);
		int32 Count = 0;
		for (int32 Row = 0; Row < 5; ++Row)
		{
			const float Radius = 340.0f + Row * 110.0f;
			const int32 Balls = 5 + Row * 2;
			for (int32 Index = 0; Index < Balls; ++Index)
			{
				const float A = FMath::DegreesToRadians(20.0f + 140.0f * Index / FMath::Max(Balls - 1, 1));
				const FVector Pos = Back + FVector(10.0f + Row * 14.0f, -FMath::Cos(A) * Radius, FMath::Sin(A) * Radius * 1.15f + 80.0f);
				Sphere((Count++ % 3 == 0) ? Gold : Marble, Pos, 54.0f - Row * 3.0f, UvMarble, 18, 10);
			}
		}
		Sphere(Gold, Back + FVector(70.0f, 0.0f, 1130.0f), 150.0f, UvGold, 32, 18);
		Ring(Gold, Back + FVector(70.0f, 0.0f, 1130.0f), FVector(1.0f, 0.0f, 0.4f), 190.0f, 12.0f, UvGold, 64, 10);
		Sphere(H.Get(ELook::Bulb), Back + FVector(-60.0f, 0.0f, 1130.0f), 26.0f, UvGold, 12, 8);
		// Tall candelabra either side of the dais.
		for (const float Side : {-1.0f, 1.0f})
		{
			const FVector Foot(DaisX + 450.0f, Side * 1050.0f, DaisTop * 0.75f);
			Lathe(Gold, {{0, 0}, {60, 0}, {60, 12}, {40, 24}, {18, 60}, {14, 400}, {24, 420}, {14, 440}, {12, 620}, {40, 640}, {50, 660}, {0, 660}}, At(Foot), 24, UvGold);
			for (int32 Candle = 0; Candle < 5; ++Candle)
			{
				const float A = UE_TWO_PI * Candle / 5;
				const FVector Wick = Foot + FVector(FMath::Cos(A) * 60.0f, FMath::Sin(A) * 60.0f, 690.0f);
				Lathe(Marble, {{0, 0}, {7, 0}, {7, 40}, {0, 40}}, At(Wick - FVector(0.0f, 0.0f, 30.0f)), 10, UvMarble);
				Sphere(H.Get(ELook::Bulb), Wick + FVector(0.0f, 0.0f, 22.0f), 7.0f, UvGold, 8, 6, 1.8f);
			}
			Ring(Gold, Foot + FVector(0.0f, 0.0f, 660.0f), FVector::UpVector, 60.0f, 6.0f, UvGold, 32, 8);
			OtherLights.Add(AddPoint(Foot + FVector(0.0f, 0.0f, 720.0f), FLinearColor(1.0f, 0.75f, 0.5f), 160.0f, 1400.0f));
		}
	}

	// ---- The ceiling (the scenes'): coffers between gold ribs, a rosette over each chandelier.
	{
		FHallBatch& Ceiling = H.Get(ELook::Ceiling, EGroup::Cinematic);
		FHallBatch& Gold = H.Get(ELook::Gold, EGroup::Cinematic);
		Box(Ceiling, FVector(HallX0 + HallLength * 0.5f, 0.0f, HallHeight + 30.0f), FVector(HallLength, HallHalfWidth * 2.0f, 60.0f), UvPaper, 0.0f, true);
		for (float X = HallX0 + 320.0f; X < HallX1; X += 640.0f)
		{
			Box(Gold, FVector(X, 0.0f, HallHeight - 20.0f), FVector(40.0f, HallHalfWidth * 2.0f, 40.0f), UvGold, 0.0f, true);
		}
		for (float Y = -HallHalfWidth + 500.0f; Y < HallHalfWidth; Y += 1000.0f)
		{
			Box(Gold, FVector(HallX0 + HallLength * 0.5f, Y, HallHeight - 20.0f), FVector(HallLength, 40.0f, 40.0f), UvGold, 0.0f, true);
		}
	}

	// ---- The chandeliers: a gold body, two rings of arms with candles, crystal drops; each its own light.
	const float ChandelierXs[3] = {2300.0f, 3650.0f, 5000.0f};
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const FVector Hang(ChandelierXs[Index], 0.0f, 2350.0f);
		ChandelierSpots.Add(Hang);
		const EGroup Group = static_cast<EGroup>(static_cast<int32>(EGroup::Chandelier0) + Index);
		FHallBatch& Gold = H.Get(ELook::Gold, EGroup::Cinematic);
		Lathe(Gold, ChandelierBody(), At(Hang, 0.0f, 1.6f), 32, UvGold);
		Lathe(Gold, {{0, 0}, {5, 0}, {5, HallHeight - Hang.Z - 270.0f}, {0, HallHeight - Hang.Z - 270.0f}}, At(Hang + FVector(0.0f, 0.0f, 270.0f)), 10, UvGold);
		for (int32 Tier = 0; Tier < 2; ++Tier)
		{
			const float Radius = Tier == 0 ? 300.0f : 190.0f;
			const float Z = Tier == 0 ? 20.0f : 150.0f;
			const int32 Arms = Tier == 0 ? 16 : 10;
			Ring(Gold, Hang + FVector(0.0f, 0.0f, Z), FVector::UpVector, Radius, 7.0f, UvGold, 64, 8);
			for (int32 Arm = 0; Arm < Arms; ++Arm)
			{
				const float A = UE_TWO_PI * (Arm + Tier * 0.5f) / Arms;
				const FVector Way(FMath::Cos(A), FMath::Sin(A), 0.0f);
				const FVector Tip = Hang + Way * Radius + FVector(0.0f, 0.0f, Z);
				Box(Gold, Hang + Way * Radius * 0.5f + FVector(0.0f, 0.0f, Z - 10.0f), FVector(Radius, 6.0f, 6.0f), UvGold, FMath::RadiansToDegrees(A));
				Lathe(Gold, {{0, 0}, {10, 0}, {14, 8}, {8, 14}, {0, 14}}, At(Tip), 10, UvGold);
				Lathe(H.Get(ELook::Marble, EGroup::Cinematic), {{0, 0}, {5, 0}, {5, 30}, {0, 30}}, At(Tip + FVector(0.0f, 0.0f, 12.0f)), 8, UvMarble);
				Sphere(H.Get(ELook::Bulb, Group), Tip + FVector(0.0f, 0.0f, 52.0f), 8.0f, UvGold, 8, 6, 1.6f);
				// Crystal drops strung under the arm.
				Sphere(H.Get(ELook::Crystal, EGroup::Cinematic), Tip - FVector(0.0f, 0.0f, 30.0f), 6.0f, UvGold, 8, 6, 1.8f);
				Sphere(H.Get(ELook::Crystal, EGroup::Cinematic), Tip - Way * Radius * 0.5f - FVector(0.0f, 0.0f, 60.0f), 5.0f, UvGold, 8, 6, 1.8f);
			}
		}
		ChandelierLights.Add(AddPoint(Hang + FVector(0.0f, 0.0f, 20.0f), FLinearColor(1.0f, 0.78f, 0.52f), 900.0f, 5200.0f));
		ChandelierLit.Add(1.0f);
	}

	// ---- The near wall over the great doors, and the doors (the scenes').
	{
		const FVector In(1.0f, 0.0f, 0.0f);
		FHallBatch& Wall = H.Get(ELook::Wallpaper, EGroup::Cinematic);
		const float DoorHalf = 520.0f;
		const float DoorTop = 1150.0f;
		for (const float Side : {-1.0f, 1.0f})
		{
			const float Y0 = Side * DoorHalf;
			const float Y1 = Side * HallHalfWidth;
			Box(Wall, FVector(DoorX - 30.0f, (Y0 + Y1) * 0.5f, HallHeight * 0.5f), FVector(40.0f, FMath::Abs(Y1 - Y0), HallHeight), UvPaper);
		}
		Box(Wall, FVector(DoorX - 30.0f, 0.0f, DoorTop + (HallHeight - DoorTop) * 0.5f), FVector(40.0f, DoorHalf * 2.0f, HallHeight - DoorTop), UvPaper);
		FHallBatch& Frame = H.Get(ELook::Marble, EGroup::Cinematic);
		for (const float Side : {-1.0f, 1.0f})
		{
			Box(Frame, FVector(DoorX - 4.0f, Side * (DoorHalf + 60.0f), DoorTop * 0.5f), FVector(60.0f, 120.0f, DoorTop), UvMarble);
		}
		Box(Frame, FVector(DoorX - 4.0f, 0.0f, DoorTop + 60.0f), FVector(60.0f, DoorHalf * 2.0f + 240.0f, 120.0f), UvMarble);
		// A pediment over it, gold edged.
		SurfaceGrid(Frame, 1, 8, [=](const float U, const float V)
		{
			const float Y = FMath::Lerp(-DoorHalf - 160.0f, DoorHalf + 160.0f, V);
			const float Peak = (1.0f - FMath::Abs(V * 2.0f - 1.0f)) * 260.0f;
			return FVector(DoorX + 30.0f, Y, DoorTop + 120.0f + U * Peak);
		}, In);
		// The doors: dark wood, each with gold panels, on hinges at the frame (they open into the hall).
		for (const float Side : {-1.0f, 1.0f})
		{
			USceneComponent* Hinge = NewObject<USceneComponent>(this);
			Hinge->SetupAttachment(GetRootComponent());
			Hinge->RegisterComponent();
			Hinge->SetRelativeLocation(FVector(DoorX, Side * DoorHalf, 0.0f));
			(Side < 0.0f ? DoorLeftHinge : DoorRightHinge) = Hinge;
			FHallBatch& Leaf = H.Get(ELook::Wood, Side < 0.0f ? EGroup::DoorLeft : EGroup::DoorRight);
			FHallBatch& Trim = H.Get(ELook::Gold, Side < 0.0f ? EGroup::DoorLeft : EGroup::DoorRight);
			Box(Leaf, FVector(0.0f, -Side * DoorHalf * 0.5f, DoorTop * 0.5f), FVector(30.0f, DoorHalf, DoorTop), UvWood, 0.0f, true);
			for (int32 Row = 0; Row < 3; ++Row)
			{
				Box(Trim, FVector(18.0f, -Side * DoorHalf * 0.5f, 220.0f + Row * 340.0f), FVector(6.0f, DoorHalf * 0.7f, 250.0f), UvGold);
				Box(Leaf, FVector(22.0f, -Side * DoorHalf * 0.5f, 220.0f + Row * 340.0f), FVector(6.0f, DoorHalf * 0.6f, 210.0f), UvWood);
			}
			Sphere(Trim, FVector(26.0f, -Side * (DoorHalf - 60.0f), 560.0f), 18.0f, UvGold, 12, 8);
		}
	}

	// ---- Light in the air: down from the rose window onto the dais, and in through the tall windows (the scenes').
	UMaterialInterface* ShaftParent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/ChaosImpact/solo/Stage30/M_CI_HallShaft.M_CI_HallShaft"));
	if (ShaftParent)
	{
		ShaftLook = UMaterialInstanceDynamic::Create(ShaftParent, this);
		ShaftLook->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.0f, 0.72f, 0.72f));
		ShaftLook->SetScalarParameterValue(TEXT("Intensity"), 0.22f);
		FHallBatch& Shaft = H.Shaft;
		const auto Beam = [&Shaft](const FVector& From, const FVector& To, const float WidthFrom, const float WidthTo)
		{
			const FVector Way = (To - From).GetSafeNormal();
			for (int32 Plane = 0; Plane < 3; ++Plane)
			{
				const FVector Across = FVector::CrossProduct(Way, FVector::UpVector).GetSafeNormal().RotateAngleAxis(Plane * 60.0f, Way);
				const int32 A = Vert(Shaft, From - Across * WidthFrom, FVector::UpVector, FVector2D(0.0, 0.0));
				const int32 Bv = Vert(Shaft, From + Across * WidthFrom, FVector::UpVector, FVector2D(1.0, 0.0));
				const int32 C = Vert(Shaft, To + Across * WidthTo, FVector::UpVector, FVector2D(1.0, 1.0));
				const int32 D = Vert(Shaft, To - Across * WidthTo, FVector::UpVector, FVector2D(0.0, 1.0));
				Shaft.T.Append({A, Bv, C, A, C, D});
			}
		};
		Beam(FVector(FarWallX - 40.0f, 0.0f, RoseZ), FVector(ThroneX - 1400.0f, 0.0f, 0.0f), RoseRadius * 0.85f, RoseRadius * 1.25f);
	}

	// ---- Make the parts: one mesh per look and group.
	for (int32 Slot = 0; Slot <= H.Batches.Num(); ++Slot)
	{
		const bool bShaft = Slot == H.Batches.Num();
		const FHallBatch& B = bShaft ? H.Shaft : H.Batches[Slot];
		if (B.V.IsEmpty())
		{
			continue;
		}
		const EGroup Group = bShaft ? EGroup::Always : static_cast<EGroup>(Slot / FHallBuild::Looks);
		const ELook Look = bShaft ? ELook::Marble : static_cast<ELook>(Slot % FHallBuild::Looks);
		UProceduralMeshComponent* Part = MakePart(*FString::Printf(TEXT("Hall_%d"), Slot), 0, Group == EGroup::Collide);
		if (Group == EGroup::DoorLeft || Group == EGroup::DoorRight)
		{
			Part->AttachToComponent(Group == EGroup::DoorLeft ? DoorLeftHinge : DoorRightHinge, FAttachmentTransformRules::KeepRelativeTransform);
			(Group == EGroup::DoorLeft ? DoorLeft : DoorRight) = Part;
			CinematicParts.Add(Part);
		}
		Part->CreateMeshSection_LinearColor(0, B.V, B.T, B.N, B.UV, TArray<FLinearColor>(), TArray<FProcMeshTangent>(), Group == EGroup::Collide);
		Part->SetCastShadow(!bShaft && Look != ELook::Rose && Look != ELook::Window && Look != ELook::Bulb);
		if (bShaft)
		{
			Part->SetMaterial(0, ShaftLook);
		}
		else if (Group >= EGroup::Chandelier0 && Group <= EGroup::Chandelier2)
		{
			// Each chandelier's bulbs their own look (they go out one by one).
			UMaterialInstanceDynamic* Bulbs = Surface(Tex(TEXT("T_Hall_Gold")), FLinearColor(1.0f, 0.86f, 0.62f), 0.3f, 0.0f, 9.0f);
			Part->SetMaterial(0, Bulbs);
			const int32 Index = static_cast<int32>(Group) - static_cast<int32>(EGroup::Chandelier0);
			ChandelierBulbs.SetNum(3);
			ChandelierLooks.SetNum(3);
			ChandelierBulbs[Index] = Part;
			ChandelierLooks[Index] = Bulbs;
			CinematicParts.Add(Part);
		}
		else
		{
			Part->SetMaterial(0, Looks[static_cast<int32>(Look)]);
		}
		if (Group == EGroup::Cinematic)
		{
			CinematicParts.Add(Part);
		}
		Parts.Add(Part);
#if !UE_BUILD_SHIPPING
		UTexture* Shown = nullptr;
		if (const UMaterialInstanceDynamic* Applied = Cast<UMaterialInstanceDynamic>(Part->GetMaterial(0)))
		{
			Applied->GetTextureParameterValue(TEXT("Tex"), Shown);
		}
		UE_LOG(LogChaosImpact, Log, TEXT("White hall part %d: look %d group %d, %d verts %d tris, material %s, tex %s"), Slot, static_cast<int32>(Look),
			static_cast<int32>(Group), B.V.Num(), B.T.Num() / 3, *GetNameSafe(Part->GetMaterial(0)), *GetNameSafe(Shown));
#endif
	}

	// ---- The lights on the throne and through the rose window.
	ThroneSpot = NewObject<USpotLightComponent>(this);
	ThroneSpot->SetupAttachment(GetRootComponent());
	ThroneSpot->RegisterComponent();
	ThroneSpot->SetMobility(EComponentMobility::Movable);
	ThroneSpot->SetWorldLocationAndRotation(FVector(ThroneX - 900.0f, 0.0f, 2900.0f), (GetThroneSeat() + FVector(0.0f, 0.0f, 300.0f) - FVector(ThroneX - 900.0f, 0.0f, 2900.0f)).Rotation());
	ThroneSpot->SetIntensityUnits(ELightUnits::Candelas);
	ThroneSpot->SetIntensity(2600.0f);
	ThroneSpot->SetLightColor(FLinearColor(1.0f, 0.9f, 0.78f));
	ThroneSpot->SetAttenuationRadius(5000.0f);
	ThroneSpot->SetInnerConeAngle(10.0f);
	ThroneSpot->SetOuterConeAngle(22.0f);
	ThroneSpot->SetCastShadows(true);
	RoseSpot = NewObject<USpotLightComponent>(this);
	RoseSpot->SetupAttachment(GetRootComponent());
	RoseSpot->RegisterComponent();
	RoseSpot->SetMobility(EComponentMobility::Movable);
	RoseSpot->SetWorldLocationAndRotation(GetRoseWindow(), (FVector(ThroneX - 1600.0f, 0.0f, 0.0f) - GetRoseWindow()).Rotation());
	RoseSpot->SetIntensityUnits(ELightUnits::Candelas);
	RoseSpot->SetIntensity(1400.0f);
	RoseSpot->SetLightColor(FLinearColor(1.0f, 0.62f, 0.7f));
	RoseSpot->SetAttenuationRadius(6000.0f);
	RoseSpot->SetInnerConeAngle(16.0f);
	RoseSpot->SetOuterConeAngle(34.0f);
	RoseSpot->SetCastShadows(false);
	RoseSpot->SetVolumetricScatteringIntensity(2.0f);
	// Soft fill down the colonnades.
	for (const float Side : {-1.0f, 1.0f})
	{
		for (const float X : {1900.0f, 3700.0f, 5300.0f})
		{
			OtherLights.Add(AddPoint(FVector(X, Side * 2650.0f, 900.0f), FLinearColor(1.0f, 0.82f, 0.62f), 200.0f, 2200.0f));
		}
	}
	for (UPointLightComponent* Light : OtherLights)
	{
		OtherLightColors.Add(Light->GetLightColor());
		OtherLightCandela.Add(Light->Intensity);
	}

	// ---- The soldiers, lined up on the galleries facing the carpet.
	USkeletalMesh* SoldierMesh = nullptr;
	TArray<UMaterialInterface*> SoldierLooks;
	if (UClass* Minion = LoadClass<ACharacter>(nullptr, TEXT("/Game/ChaosImpact/solo/BP_SoloEnemy.BP_SoloEnemy_C")))
	{
		if (const ACharacter* Default = Minion->GetDefaultObject<ACharacter>(); Default && Default->GetMesh())
		{
			SoldierMesh = Default->GetMesh()->GetSkeletalMeshAsset();
			SoldierLooks = Default->GetMesh()->GetMaterials();
		}
	}
	UAnimSequence* Idle = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle.MM_Idle"));
	SoldierIdle = Idle;
	SoldierRun = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Jog/MF_Unarmed_Jog_Fwd.MF_Unarmed_Jog_Fwd"));
	if (SoldierMesh)
	{
		for (const float Side : {-1.0f, 1.0f})
		{
			for (int32 Index = 0; Index < 8; ++Index)
			{
				const FVector Home(1460.0f + Index * 540.0f + (Side > 0.0f ? 0.0f : 30.0f), Side * GalleryY, 70.0f);
				USkeletalMeshComponent* Soldier = NewObject<USkeletalMeshComponent>(this);
				Soldier->SetupAttachment(GetRootComponent());
				Soldier->RegisterComponent();
				Soldier->SetSkeletalMeshAsset(SoldierMesh);
				for (int32 Slot = 0; Slot < SoldierLooks.Num(); ++Slot)
				{
					Soldier->SetMaterial(Slot, SoldierLooks[Slot]);
				}
				Soldier->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				const FRotator Rest(0.0f, (Side > 0.0f ? -90.0f : 90.0f) - 90.0f, 0.0f);
				Soldier->SetWorldLocationAndRotation(Home, Rest);
				if (Idle)
				{
					Soldier->SetAnimationMode(EAnimationMode::AnimationSingleNode);
					Soldier->PlayAnimation(Idle, true);
					Soldier->SetPosition(FMath::FRandRange(0.0f, Idle->GetPlayLength()), false);
				}
				// (The mannequin only animates; what is seen is the Robo, the solo mode's own foot soldier, following it.)
				UChaosImpactPuppetComponent* Robo = NewObject<UChaosImpactPuppetComponent>(this);
				Robo->SetupAttachment(Soldier);
				Robo->SetRelativeScale3D(FVector(1.45f));
				Robo->RegisterComponent();
				if (Robo->Initialize(Soldier, ChaosImpactRoster::RoboIndex))
				{
					Soldier->SetHiddenInGame(true);
					Soldier->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
				}
				else
				{
					Robo->DestroyComponent();
					Robo = nullptr;
				}
				SoldierRobos.Add(Robo);
				Soldiers.Add(Soldier);
				SoldierRest.Add(Rest);
				SoldierHome.Add(Home);
				SoldierHop.Add(0.0f);
				SoldierDriven.Add(false);
			}
		}
	}
	UE_LOG(LogChaosImpact, Log, TEXT("White hall built: %d parts, %d soldiers"), Parts.Num(), Soldiers.Num());
	UpdateLights();
}

// ---------------------------------------------------------------------------------------------------- Its controls

FVector AChaosImpactWhiteHall::GetThroneSeat() const
{
	return FVector(ThroneX - 20.0f, 0.0f, DaisTop + 250.0f);
}

FVector AChaosImpactWhiteHall::GetChandelier(const int32 Index) const
{
	return ChandelierSpots.IsValidIndex(Index) ? ChandelierSpots[Index] : FVector::ZeroVector;
}

USkeletalMeshComponent* AChaosImpactWhiteHall::GetSoldier(const int32 Index) const
{
	return Soldiers.IsValidIndex(Index) ? Soldiers[Index].Get() : nullptr;
}

void AChaosImpactWhiteHall::SetCinematic(const bool bOn)
{
	bCinematic = bOn;
	for (UProceduralMeshComponent* Part : CinematicParts)
	{
		if (Part)
		{
			Part->SetVisibility(bOn, true);
		}
	}
}

void AChaosImpactWhiteHall::SetChandelier(const int32 Index, const float Lit)
{
	for (int32 I = 0; I < ChandelierLit.Num(); ++I)
	{
		if (Index < 0 || I == Index)
		{
			ChandelierLit[I] = FMath::Clamp(Lit, 0.0f, 1.0f);
		}
	}
	UpdateLights();
}

void AChaosImpactWhiteHall::SetDoors(const float Open)
{
	const float Angle = 92.0f * FMath::Clamp(Open, 0.0f, 1.0f);
	if (DoorLeftHinge)
	{
		DoorLeftHinge->SetRelativeRotation(FRotator(0.0f, -Angle, 0.0f));
	}
	if (DoorRightHinge)
	{
		DoorRightHinge->SetRelativeRotation(FRotator(0.0f, Angle, 0.0f));
	}
}

void AChaosImpactWhiteHall::SetThroneLight(const float Lit)
{
	ThroneLit = FMath::Clamp(Lit, 0.0f, 1.0f);
	UpdateLights();
}

void AChaosImpactWhiteHall::SetRoseGlow(const float Glow)
{
	RoseGlow = FMath::Clamp(Glow, 0.0f, 1.5f);
	UpdateLights();
}

void AChaosImpactWhiteHall::SetAlarm(const float Amount)
{
	Alarm = FMath::Clamp(Amount, 0.0f, 1.0f);
	UpdateLights();
}

void AChaosImpactWhiteHall::SetMasterLight(const float Amount)
{
	Master = FMath::Clamp(Amount, 0.0f, 1.0f);
	UpdateLights();
}

void AChaosImpactWhiteHall::UpdateLights()
{
	const FLinearColor Red(1.0f, 0.14f, 0.12f);
	// (Half the President's health gone: the lights burn red and flicker.)
	const float Flicker = Alarm > 0.0f ? 1.0f - Alarm * 0.25f * (0.5f + 0.5f * FMath::Sin(Clock * 13.0f) * FMath::Sin(Clock * 5.3f)) : 1.0f;
	for (int32 Index = 0; Index < ChandelierLights.Num(); ++Index)
	{
		if (UPointLightComponent* Light = ChandelierLights[Index])
		{
			Light->SetIntensity(900.0f * ChandelierLit[Index] * Master * Flicker);
			Light->SetLightColor(FMath::Lerp(FLinearColor(1.0f, 0.78f, 0.52f), Red, Alarm));
		}
		if (ChandelierLooks.IsValidIndex(Index) && ChandelierLooks[Index])
		{
			ChandelierLooks[Index]->SetScalarParameterValue(TEXT("Glow"), 9.0f * ChandelierLit[Index] * Master * Flicker);
			ChandelierLooks[Index]->SetVectorParameterValue(TEXT("Tint"), FMath::Lerp(FLinearColor(1.0f, 0.86f, 0.62f), Red, Alarm));
		}
	}
	for (int32 Index = 0; Index < OtherLights.Num(); ++Index)
	{
		if (UPointLightComponent* Light = OtherLights[Index])
		{
			Light->SetIntensity(OtherLightCandela[Index] * Master * Flicker);
			Light->SetLightColor(FMath::Lerp(OtherLightColors[Index], Red, Alarm));
		}
	}
	if (ThroneSpot)
	{
		ThroneSpot->SetIntensity(2600.0f * ThroneLit * FMath::Max(Master, 0.0f));
		ThroneSpot->SetLightColor(FMath::Lerp(FLinearColor(1.0f, 0.9f, 0.78f), FLinearColor(1.0f, 0.3f, 0.25f), Alarm));
	}
	if (RoseSpot)
	{
		RoseSpot->SetIntensity(1400.0f * RoseGlow * Master);
		RoseSpot->SetLightColor(FMath::Lerp(FLinearColor(1.0f, 0.62f, 0.7f), Red, Alarm));
	}
	if (RoseLook)
	{
		RoseLook->SetScalarParameterValue(TEXT("Glow"), 5.0f * RoseGlow * FMath::Max(Master, 0.35f));
		RoseLook->SetVectorParameterValue(TEXT("Tint"), FMath::Lerp(FLinearColor::White, FLinearColor(1.0f, 0.35f, 0.3f), Alarm));
	}
	if (SconceLook)
	{
		SconceLook->SetScalarParameterValue(TEXT("Glow"), 9.0f * Master * Flicker);
		SconceLook->SetVectorParameterValue(TEXT("Tint"), FMath::Lerp(FLinearColor(1.0f, 0.86f, 0.62f), Red, Alarm));
	}
	if (WindowLook)
	{
		WindowLook->SetVectorParameterValue(TEXT("Tint"), FMath::Lerp(FLinearColor::White, FLinearColor(1.0f, 0.4f, 0.4f), Alarm));
	}
	if (ShaftLook)
	{
		ShaftLook->SetScalarParameterValue(TEXT("Intensity"), 0.22f * RoseGlow * FMath::Max(Master, 0.3f));
		ShaftLook->SetVectorParameterValue(TEXT("Color"), FMath::Lerp(FLinearColor(1.0f, 0.72f, 0.72f), Red, Alarm));
	}
}

void AChaosImpactWhiteHall::TurnSoldiers(const FVector& Toward, const float Amount)
{
	for (int32 Index = 0; Index < Soldiers.Num(); ++Index)
	{
		if (USkeletalMeshComponent* Soldier = Soldiers[Index]; Soldier && !SoldierDriven[Index])
		{
			const FVector Way = (Toward - SoldierHome[Index]).GetSafeNormal2D();
			const FRotator Look(0.0f, Way.Rotation().Yaw - 90.0f, 0.0f);
			Soldier->SetWorldRotation(FMath::Lerp(SoldierRest[Index], Look, FMath::Clamp(Amount, 0.0f, 1.0f)));
		}
	}
}

void AChaosImpactWhiteHall::StampSoldiers(const float Height)
{
	for (float& Hop : SoldierHop)
	{
		Hop = Height;
	}
}

void AChaosImpactWhiteHall::SetSoldierShown(const int32 Index, const bool bShown)
{
	if (USkeletalMeshComponent* Soldier = GetSoldier(Index))
	{
		Soldier->SetVisibility(bShown);
	}
	if (SoldierRobos.IsValidIndex(Index) && SoldierRobos[Index])
	{
		SoldierRobos[Index]->SetPartsVisible(bShown);
	}
}

void AChaosImpactWhiteHall::ShowSoldiers(const bool bShown)
{
	for (int32 Index = 0; Index < Soldiers.Num(); ++Index)
	{
		SetSoldierShown(Index, bShown);
	}
}

void AChaosImpactWhiteHall::DriveSoldier(const int32 Index, const FVector& At, const float Yaw, const bool bRunning)
{
	USkeletalMeshComponent* Soldier = GetSoldier(Index);
	if (!Soldier)
	{
		return;
	}
	const bool bWasRunning = SoldierDriven[Index];
	SoldierDriven[Index] = true;
	SetSoldierShown(Index, true);
	Soldier->SetWorldLocationAndRotation(At, FRotator(0.0f, Yaw - 90.0f, 0.0f));
	UAnimSequence* Want = bRunning && SoldierRun ? SoldierRun.Get() : SoldierIdle.Get();
	if (Want && (Soldier->GetSingleNodeInstance() == nullptr || Soldier->GetSingleNodeInstance()->GetAnimationAsset() != Want || !bWasRunning))
	{
		Soldier->PlayAnimation(Want, true);
	}
}

void AChaosImpactWhiteHall::HideSoldier(const int32 Index)
{
	if (GetSoldier(Index))
	{
		SetSoldierShown(Index, false);
		SoldierDriven[Index] = true;
	}
}

void AChaosImpactWhiteHall::ResetSoldiers(const TArray<int32>& Gone)
{
	for (int32 Index = 0; Index < Soldiers.Num(); ++Index)
	{
		USkeletalMeshComponent* Soldier = Soldiers[Index];
		if (!Soldier)
		{
			continue;
		}
		SoldierDriven[Index] = false;
		SoldierHop[Index] = 0.0f;
		Soldier->SetWorldLocationAndRotation(SoldierHome[Index], SoldierRest[Index]);
		SetSoldierShown(Index, !Gone.Contains(Index));
		if (SoldierIdle)
		{
			Soldier->PlayAnimation(SoldierIdle, true);
			Soldier->SetPosition(FMath::FRandRange(0.0f, SoldierIdle->GetPlayLength()), false);
		}
	}
}

void AChaosImpactWhiteHall::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Clock += DeltaSeconds;
	if (Alarm > 0.0f)
	{
		UpdateLights();
	}
	for (int32 Index = 0; Index < Soldiers.Num(); ++Index)
	{
		// A stamp: a quick hop and down (a little apart from each other, as real people are).
		if (SoldierHop[Index] > 0.0f && Soldiers[Index] && !SoldierDriven[Index])
		{
			SoldierHop[Index] = FMath::Max(SoldierHop[Index] - DeltaSeconds * 220.0f, 0.0f);
			const float Up = FMath::Sin(FMath::Clamp(SoldierHop[Index] / 40.0f, 0.0f, 1.0f) * UE_PI) * 18.0f;
			Soldiers[Index]->SetWorldLocation(SoldierHome[Index] + FVector(0.0f, 0.0f, Up));
		}
	}
}
