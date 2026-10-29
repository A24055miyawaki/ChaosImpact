#include "ChaosImpactPrisonBall.h"
#include "ChaosImpactBallTypes.h"
#include "ChaosImpactEase.h"
#include "ChaosImpactFxKit.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactPlayerController.h"
#include "ProceduralMeshComponent.h"
#include "ChaosImpactIceMeshes.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

bool ChaosImpactBallTypes::IsPrisonMode(const UWorld* World)
{
	if (!World || World->URL.HasOption(TEXT("CISolo=1"))
		|| World->URL.HasOption(TEXT("CIRPGPrototype=1"))
		|| World->GetMapName().Contains(TEXT("RPGPrototype"))) { return false; }
	if (const auto* Match = World->GetGameState<AChaosImpactGameState>(); Match && Match->bVersusMatch)
	{
		return Match->Phase == EChaosImpactOnlinePhase::Match;
	}
	const auto* PC = Cast<AChaosImpactPlayerController>(World->GetFirstPlayerController());
	return PC && PC->IsTrainingMode() && PC->ShowsTrainingPauseEntries();
}

namespace
{
	/** The prison's own orange (its rims, the lock band's light). */
	const FLinearColor PrisonHot(1.0f, 0.36f, 0.05f);
	const FLinearColor PrisonPale(1.0f, 0.82f, 0.5f);
	const FLinearColor PrisonAlarm(1.0f, 0.06f, 0.04f);

	/**
	 * A round bar along Points (on the sphere: Out is each point's outward direction), RadiusOut thick toward the
	 * outside and RadiusUp thick along the sphere's surface; real normals, so the metal catches the light.
	 */
	void AppendBar(ChaosImpactIceMeshes::FMeshBuffers& B, const TArray<FVector>& Points, const float RadiusOut, const float RadiusSide,
		const bool bClosed)
	{
		constexpr int32 Sides = 8;
		const int32 Count = Points.Num();
		if (Count < 2)
		{
			return;
		}
		const int32 First = B.Vertices.Num();
		for (int32 I = 0; I < Count; ++I)
		{
			const FVector& P = Points[I];
			const FVector Prev = Points[bClosed ? (I + Count - 1) % Count : FMath::Max(I - 1, 0)];
			const FVector Next = Points[bClosed ? (I + 1) % Count : FMath::Min(I + 1, Count - 1)];
			const FVector Along = (Next - Prev).GetSafeNormal();
			FVector Out = P.GetSafeNormal();
			Out = (Out - Along * FVector::DotProduct(Out, Along)).GetSafeNormal();
			if (Out.IsNearlyZero())
			{
				Out = FVector::UpVector;
			}
			const FVector Side = FVector::CrossProduct(Along, Out);
			for (int32 S = 0; S <= Sides; ++S)
			{
				const float A = S * UE_TWO_PI / Sides;
				B.Vertices.Add(P + Out * FMath::Cos(A) * RadiusOut + Side * FMath::Sin(A) * RadiusSide);
				B.Normals.Add((Out * FMath::Cos(A) / FMath::Max(RadiusOut, 0.01f) + Side * FMath::Sin(A) / FMath::Max(RadiusSide, 0.01f)).GetSafeNormal());
				B.UVs.Add(FVector2D(static_cast<float>(S) / Sides, static_cast<float>(I) / (Count - 1)));
			}
		}
		const int32 Rings = bClosed ? Count : Count - 1;
		for (int32 I = 0; I < Rings; ++I)
		{
			const int32 R0 = First + I * (Sides + 1);
			const int32 R1 = First + ((I + 1) % Count) * (Sides + 1);
			for (int32 S = 0; S < Sides; ++S)
			{
				B.Triangles.Append({R0 + S, R1 + S, R0 + S + 1, R0 + S + 1, R1 + S, R1 + S + 1});
			}
		}
	}

	/** A latitude circle on a sphere of Radius (Latitude in radians, 0 the equator). */
	void Latitude(TArray<FVector>& Out, const float Radius, const float Latitude, const int32 Segments, const float Turn = 0.0f)
	{
		Out.Reset();
		const float Ring = Radius * FMath::Cos(Latitude);
		const float Z = Radius * FMath::Sin(Latitude);
		for (int32 I = 0; I < Segments; ++I)
		{
			const float A = Turn + I * UE_TWO_PI / Segments;
			Out.Add(FVector(FMath::Cos(A) * Ring, FMath::Sin(A) * Ring, Z));
		}
	}

	/** A flat glowing patch on the sphere's band, facing out (one tick of the lock band's clock). */
	void AppendTick(ChaosImpactIceMeshes::FMeshBuffers& B, const float Radius, const float From, const float To, const float HalfHeight)
	{
		const int32 I = B.Vertices.Num();
		const FVector A(FMath::Cos(From), FMath::Sin(From), 0.0f);
		const FVector C(FMath::Cos(To), FMath::Sin(To), 0.0f);
		B.Vertices.Append({A * Radius - FVector(0, 0, HalfHeight), C * Radius - FVector(0, 0, HalfHeight),
			C * Radius + FVector(0, 0, HalfHeight), A * Radius + FVector(0, 0, HalfHeight)});
		B.Normals.Append({A, C, C, A});
		B.UVs.Append({FVector2D(0, 1), FVector2D(1, 1), FVector2D(1, 0), FVector2D(0, 0)});
		B.Triangles.Append({I, I + 1, I + 2, I, I + 2, I + 3, I, I + 2, I + 1, I, I + 3, I + 2});
	}
}

UProceduralMeshComponent* ChaosImpactPrison::CreateCage(AActor* Owner, USceneComponent* Parent)
{
	if (!Owner || Owner->GetNetMode() == NM_DedicatedServer) { return nullptr; }
	auto* Mesh = NewObject<UProceduralMeshComponent>(Owner);
	Mesh->SetupAttachment(Parent);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
	// Section 0, the bars: lit dark iron with glowing orange rims. Section 1, the light: the lock band's clock.
	if (UMaterialInterface* Metal = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/ChaosImpact/FX/Kit/M_CIFX_CageMetal.M_CIFX_CageMetal")))
	{
		UMaterialInstanceDynamic* Bars = UMaterialInstanceDynamic::Create(Metal, Owner);
		Bars->SetVectorParameterValue(TEXT("Color"), PrisonHot);
		Mesh->SetMaterial(0, Bars);
	}
	else
	{
		Mesh->SetMaterial(0, ChaosImpactBallTypes::MakeEmissive(Owner, PrisonHot, 1.5f));
	}
	Mesh->SetMaterial(1, ChaosImpactBallTypes::MakeAdditive(Owner, PrisonHot, 3.0f));
	Mesh->RegisterComponent();
	return Mesh;
}

void ChaosImpactPrison::UpdateCage(UProceduralMeshComponent* Mesh, float Radius, float Remaining, float Time, bool bCaptured, float FloorBelow)
{
	if (!Mesh) { return; }
	const float Total = ChaosImpactBallTypes::PrisonSeconds;
	const float Left = FMath::Clamp(Remaining / Total, 0.0f, 1.0f);
	// Captured: the bars slam shut round the captive (in from wide, a little overshoot), turn slowly, and in the last
	// second rattle and blink as the lock gives way.
	const float Since = bCaptured ? Total - Remaining : 1.0f;
	const float Slam = ChaosImpactEase::Out(FMath::Clamp(Since / 0.22f, 0.0f, 1.0f));
	const float Overshoot = FMath::Sin(FMath::Clamp(Since / 0.35f, 0.0f, 1.0f) * UE_PI) * 0.06f;
	const bool bBreaking = bCaptured && Remaining < 1.0f;
	const float Rattle = bBreaking ? (1.0f - Remaining) : 0.0f;
	const float R = Radius * (FMath::Lerp(1.6f, 1.0f, Slam) - Overshoot);
	const float Spin = bCaptured ? Time * 0.45f : 0.0f;

	ChaosImpactIceMeshes::FMeshBuffers Bars;
	ChaosImpactIceMeshes::FMeshBuffers Light;
	TArray<FVector> Line;
	const float Bar = FMath::Max(R * 0.034f, 0.9f);
	// Eight bars from pole to pole (stopping short of the poles, where the caps hold them).
	constexpr int32 Meridians = 8;
	for (int32 M = 0; M < Meridians; ++M)
	{
		const float A = Spin + M * UE_TWO_PI / Meridians;
		Line.Reset();
		for (int32 I = 0; I <= 14; ++I)
		{
			const float Lat = FMath::Lerp(-0.42f, 0.42f, I / 14.0f) * UE_PI;
			Line.Add(FVector(FMath::Cos(A) * FMath::Cos(Lat), FMath::Sin(A) * FMath::Cos(Lat), FMath::Sin(Lat)) * R);
		}
		AppendBar(Bars, Line, Bar, Bar, false);
	}
	// Rings at the caps and between, and the heavy lock band round the middle.
	for (const float Lat : {-0.42f, 0.42f})
	{
		Latitude(Line, R, Lat * UE_PI, 24, Spin);
		AppendBar(Bars, Line, Bar * 1.5f, Bar * 1.5f, true);
	}
	for (const float Lat : {-0.24f, 0.24f})
	{
		Latitude(Line, R, Lat * UE_PI, 40, Spin);
		AppendBar(Bars, Line, Bar * 0.8f, Bar * 0.8f, true);
	}
	Latitude(Line, R, 0.0f, 48);
	const float BandOut = Bar * 1.6f;
	const float BandHalf = R * 0.085f;
	AppendBar(Bars, Line, BandOut, BandHalf, true);
	// Lock plates: four studs on the band.
	for (int32 K = 0; K < 4; ++K)
	{
		const float A = K * UE_HALF_PI + UE_PI * 0.25f;
		const FVector Out(FMath::Cos(A), FMath::Sin(A), 0.0f);
		Line.Reset();
		Line.Add(Out * (R + BandOut * 0.2f) - FVector(0, 0, BandHalf * 0.9f));
		Line.Add(Out * (R + BandOut * 0.2f) + FVector(0, 0, BandHalf * 0.9f));
		AppendBar(Bars, Line, BandOut * 0.9f, BandOut * 0.9f, false);
	}

	// The lock band's clock: ticks of light on its outside, going out one by one from the top as time runs out; red
	// and blinking in the last second. (Not captured: the whole band glows.)
	constexpr int32 Ticks = 40;
	const int32 Lit = bCaptured ? FMath::CeilToInt(Left * Ticks) : Ticks;
	const bool bBlinkOff = bBreaking && FMath::Frac(Time * 7.0f) > 0.6f;
	if (!bBlinkOff)
	{
		for (int32 T = 0; T < Lit; ++T)
		{
			const float From = -UE_HALF_PI + T * UE_TWO_PI / Ticks;
			AppendTick(Light, R + BandOut + 0.6f, From, From + UE_TWO_PI / Ticks * 0.72f, BandHalf * 0.55f);
		}
	}
	// Captured, the same clock as a ring of light on the floor round it (it reads from the high camera, where the
	// cage itself is small): it shrinks round as time runs out.
	if (bCaptured && FloorBelow >= 0.0f && !bBlinkOff)
	{
		const float Inner = Radius * 1.25f;
		const float Outer = Radius * 1.42f;
		const float Floor = -FloorBelow + 3.0f;
		for (int32 T = 0; T < Lit; ++T)
		{
			const float From = -UE_HALF_PI + T * UE_TWO_PI / Ticks;
			const float To = From + UE_TWO_PI / Ticks * 0.75f;
			const FVector A(FMath::Cos(From), FMath::Sin(From), 0.0f);
			const FVector C(FMath::Cos(To), FMath::Sin(To), 0.0f);
			const int32 I = Light.Vertices.Num();
			Light.Vertices.Append({A * Inner + FVector(0, 0, Floor), C * Inner + FVector(0, 0, Floor), C * Outer + FVector(0, 0, Floor), A * Outer + FVector(0, 0, Floor)});
			Light.Normals.Append({FVector::UpVector, FVector::UpVector, FVector::UpVector, FVector::UpVector});
			Light.UVs.Append({FVector2D(0, 1), FVector2D(1, 1), FVector2D(1, 0), FVector2D(0, 0)});
			Light.Triangles.Append({I, I + 1, I + 2, I, I + 2, I + 3, I, I + 2, I + 1, I, I + 3, I + 2});
		}
	}
	// The last tick still lit flickers, the one about to go.
	if (UMaterialInstanceDynamic* Glow = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(1)))
	{
		Glow->SetVectorParameterValue(TEXT("Color"), bBreaking ? PrisonAlarm : FMath::Lerp(PrisonAlarm, PrisonHot, FMath::Clamp(Left * 2.5f, 0.0f, 1.0f)));
		Glow->SetScalarParameterValue(TEXT("Intensity"), bCaptured ? 3.0f + 4.0f * (1.0f - Slam) : 1.6f);
	}
	if (UMaterialInstanceDynamic* Metal = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0)))
	{
		// The rims flare white-hot as the bars slam shut, and pulse red as the lock fails.
		const float Pulse = bBreaking ? 0.5f + 0.5f * FMath::Sin(Time * 30.0f) : 0.0f;
		Metal->SetVectorParameterValue(TEXT("Color"), FMath::Lerp(FMath::Lerp(PrisonHot, PrisonPale, 1.0f - Slam), PrisonAlarm, Pulse * 0.7f));
		Metal->SetScalarParameterValue(TEXT("Intensity"), 1.8f + 6.0f * (1.0f - Slam) + 3.0f * Pulse);
		Metal->SetScalarParameterValue(TEXT("Glow"), 0.05f);
	}
	Mesh->CreateMeshSection(0, Bars.Vertices, Bars.Triangles, Bars.Normals, Bars.UVs, TArray<FColor>(), TArray<FProcMeshTangent>(), false);
	if (Light.Vertices.IsEmpty())
	{
		Mesh->ClearMeshSection(1);
	}
	else
	{
		Mesh->CreateMeshSection(1, Light.Vertices, Light.Triangles, Light.Normals, Light.UVs, TArray<FColor>(), TArray<FProcMeshTangent>(), false);
	}
	// Rattling as the lock fails; otherwise held still (captured: the band's clock stays readable) or turning.
	const FRotator Shake = bBreaking ? FRotator(FMath::Sin(Time * 61.0f) * 5.0f * Rattle, 0.0f, FMath::Sin(Time * 47.0f) * 5.0f * Rattle) : FRotator::ZeroRotator;
	Mesh->SetRelativeRotation(bCaptured ? Shake : FRotator(0, Time * 36, 0));
}

void ChaosImpactPrison::PlayLock(UObject* WorldContext, const FVector& At, const float Radius)
{
	// The bars slamming shut: a white-hot flash, a ring of orange light snapping inward onto the cage, a shock ring
	// thrown out over the floor and sparks struck off the bars.
	AChaosImpactFxKit* Fx = AChaosImpactFxKit::Spawn(WorldContext, At);
	if (!Fx)
	{
		return;
	}
	Fx->Stream.Initialize(FMath::Rand());
	FChaosImpactFxPiece Flash;
	Flash.Life = 0.2f;
	Flash.SizeFrom = Radius * 2.2f;
	Flash.SizeTo = Radius * 4.0f;
	Flash.Color = PrisonHot;
	Flash.CoreColor = PrisonPale;
	Flash.Intensity = 6.0f;
	Flash.Attack = 0.04f;
	Flash.NoiseAmount = 0.2f;
	Fx->Add(Flash);
	FChaosImpactFxPiece Close;
	Close.Look = EChaosImpactFxLook::AirRing;
	Close.Life = 0.22f;
	Close.SizeFrom = Radius * 6.0f;
	Close.SizeTo = Radius * 2.4f;
	Close.RingFrom = Close.RingTo = 0.8f;
	Close.RingWidth = 0.06f;
	Close.Color = PrisonHot;
	Close.CoreColor = PrisonPale;
	Close.Intensity = 4.0f;
	Close.Attack = 0.1f;
	Close.ErodeTo = 0.3f;
	Fx->Add(Close);
	FChaosImpactFxPiece Shock;
	Shock.Look = EChaosImpactFxLook::Ring;
	Shock.Location = FVector(0, 0, -Radius + 6.0f);
	Shock.Delay = 0.12f;
	Shock.Life = 0.45f;
	Shock.SizeFrom = Shock.SizeTo = Radius * 9.0f;
	Shock.RingFrom = 0.12f;
	Shock.RingTo = 0.92f;
	Shock.RingWidth = 0.05f;
	Shock.Color = PrisonHot;
	Shock.CoreColor = PrisonPale;
	Shock.Intensity = 3.0f;
	Shock.Attack = 0.05f;
	Shock.ErodeTo = 0.7f;
	Fx->Add(Shock);
	for (int32 Index = 0; Index < 14; ++Index)
	{
		FChaosImpactFxPiece Spark;
		Spark.Look = EChaosImpactFxLook::Streak;
		const FVector Out = FRotator(Fx->Stream.FRandRange(-20.0f, 60.0f), Fx->Stream.FRandRange(0.0f, 360.0f), 0.0f).Vector();
		Spark.Location = Out * Radius;
		Spark.Delay = 0.12f;
		Spark.Velocity = Out * Fx->Stream.FRandRange(600.0f, 1200.0f);
		Spark.Gravity = 2000.0f;
		Spark.Drag = 2.0f;
		Spark.Life = Fx->Stream.FRandRange(0.3f, 0.55f);
		Spark.SizeFrom = 70.0f;
		Spark.SizeTo = 20.0f;
		Spark.Width = 5.0f;
		Spark.Color = PrisonHot;
		Spark.CoreColor = PrisonPale;
		Spark.Intensity = 6.0f;
		Spark.Attack = 0.02f;
		Spark.ErodeTo = 0.0f;
		Spark.Flicker = 0.4f;
		Fx->Add(Spark);
	}
}

void ChaosImpactPrison::PlayBreak(UObject* WorldContext, const FVector& At, const float Radius)
{
	// The lock giving way: a flash, the bars flung apart as glowing shards, a puff of hot dust and a ring over the floor.
	AChaosImpactFxKit* Fx = AChaosImpactFxKit::Spawn(WorldContext, At);
	if (!Fx)
	{
		return;
	}
	Fx->Stream.Initialize(FMath::Rand());
	FChaosImpactFxPiece Flash;
	Flash.Life = 0.18f;
	Flash.SizeFrom = Radius * 2.0f;
	Flash.SizeTo = Radius * 3.6f;
	Flash.Color = PrisonHot;
	Flash.CoreColor = PrisonPale;
	Flash.Intensity = 5.0f;
	Flash.Attack = 0.04f;
	Flash.NoiseAmount = 0.2f;
	Fx->Add(Flash);
	FChaosImpactFxPiece Shock;
	Shock.Look = EChaosImpactFxLook::Ring;
	Shock.Location = FVector(0, 0, -Radius + 6.0f);
	Shock.Life = 0.4f;
	Shock.SizeFrom = Shock.SizeTo = Radius * 7.0f;
	Shock.RingFrom = 0.12f;
	Shock.RingTo = 0.92f;
	Shock.RingWidth = 0.05f;
	Shock.Color = PrisonHot;
	Shock.CoreColor = PrisonPale;
	Shock.Intensity = 2.5f;
	Shock.Attack = 0.05f;
	Shock.ErodeTo = 0.7f;
	Fx->Add(Shock);
	for (int32 Index = 0; Index < 18; ++Index)
	{
		FChaosImpactFxPiece Shard;
		Shard.Look = EChaosImpactFxLook::Streak;
		const FVector Out = FRotator(Fx->Stream.FRandRange(-10.0f, 70.0f), Fx->Stream.FRandRange(0.0f, 360.0f), 0.0f).Vector();
		Shard.Location = Out * Radius;
		Shard.Velocity = Out * Fx->Stream.FRandRange(500.0f, 1100.0f);
		Shard.Gravity = 2400.0f;
		Shard.Drag = 1.2f;
		Shard.Life = Fx->Stream.FRandRange(0.4f, 0.8f);
		Shard.SizeFrom = Fx->Stream.FRandRange(50.0f, 90.0f);
		Shard.SizeTo = 25.0f;
		Shard.Width = 7.0f;
		Shard.Color = PrisonHot;
		Shard.CoreColor = PrisonPale;
		Shard.Intensity = 5.0f;
		Shard.Attack = 0.02f;
		Shard.ErodeTo = 0.0f;
		Shard.Flicker = 0.3f;
		Fx->Add(Shard);
	}
	for (int32 Index = 0; Index < 5; ++Index)
	{
		const float Angle = (Index + Fx->Stream.FRandRange(-0.3f, 0.3f)) * UE_TWO_PI / 5.0f;
		const FVector Out(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f);
		FChaosImpactFxPiece Dust;
		Dust.Look = EChaosImpactFxLook::Smoke;
		Dust.Location = Out * Radius * 0.5f;
		Dust.Velocity = Out * 260.0f + FVector(0, 0, 80.0f);
		Dust.Drag = 2.5f;
		Dust.Life = Fx->Stream.FRandRange(0.6f, 0.9f);
		Dust.SizeFrom = Radius * 1.2f;
		Dust.SizeTo = Radius * 2.6f;
		Dust.Color = FLinearColor(0.35f, 0.22f, 0.16f);
		Dust.CoreColor = FLinearColor(0.85f, 0.55f, 0.35f);
		Dust.Intensity = 1.0f;
		Dust.Opacity = 0.35f;
		Dust.Attack = 0.08f;
		Dust.ErodeTo = 0.95f;
		Fx->Add(Dust);
	}
}
