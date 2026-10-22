#include "ChaosImpactSimaeBossFx.h"

#include "ChaosImpactBall.h"
#include "ChaosImpactBallTypes.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactIceMeshes.h"
#include "ChaosImpactLightning.h"
#include "ChaosImpactSfx.h"
#include "ChaosImpactSimaeBoss.h"

#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "ProceduralMeshComponent.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	using FMeshBuffers = ChaosImpactIceMeshes::FMeshBuffers;

	/** A flat four-cornered patch, drawn from both sides. */
	void SimaeQuad(FMeshBuffers& Mesh, const FVector& A, const FVector& B, const FVector& C, const FVector& D,
		const FVector2D& UvA = FVector2D(0, 0), const FVector2D& UvC = FVector2D(1, 1))
	{
		const int32 I = Mesh.Vertices.Num();
		Mesh.Vertices.Append({A, B, C, D});
		const FVector Normal = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
		for (int32 J = 0; J < 4; ++J)
		{
			Mesh.Normals.Add(Normal);
		}
		Mesh.UVs.Append({UvA, FVector2D(UvC.X, UvA.Y), UvC, FVector2D(UvA.X, UvC.Y)});
		Mesh.Triangles.Append({I, I + 1, I + 2, I, I + 2, I + 3, I, I + 2, I + 1, I, I + 3, I + 2});
	}

	/** A filled flat disc (a fan) on the horizontal plane. */
	void SimaeDisc(FMeshBuffers& Mesh, const FVector& Center, const float Radius, const int32 Segments = 48)
	{
		if (Radius < 1.0f)
		{
			return;
		}
		const int32 Middle = Mesh.Vertices.Num();
		Mesh.Vertices.Add(Center);
		Mesh.Normals.Add(FVector::UpVector);
		Mesh.UVs.Add(FVector2D(0.5f, 0.5f));
		for (int32 Index = 0; Index <= Segments; ++Index)
		{
			const float Angle = Index * UE_TWO_PI / Segments;
			Mesh.Vertices.Add(Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * Radius);
			Mesh.Normals.Add(FVector::UpVector);
			Mesh.UVs.Add(FVector2D(0.5f + 0.5f * FMath::Cos(Angle), 0.5f + 0.5f * FMath::Sin(Angle)));
			if (Index > 0)
			{
				const int32 A = Middle + Index;
				Mesh.Triangles.Append({Middle, A, A + 1, Middle, A + 1, A});
			}
		}
	}

	/** A ring of upright panels (a shock wall standing round a centre). */
	void SimaeWall(FMeshBuffers& Mesh, const FVector& Center, const float Radius, const float Height, const int32 Segments = 40)
	{
		for (int32 Index = 0; Index < Segments; ++Index)
		{
			const float A = Index * UE_TWO_PI / Segments;
			const float B = (Index + 1) * UE_TWO_PI / Segments;
			const FVector PA = Center + FVector(FMath::Cos(A), FMath::Sin(A), 0.0f) * Radius;
			const FVector PB = Center + FVector(FMath::Cos(B), FMath::Sin(B), 0.0f) * Radius;
			SimaeQuad(Mesh, PA, PB, PB + FVector(0, 0, Height), PA + FVector(0, 0, Height));
		}
	}

	void SimaeSetSection(UProceduralMeshComponent* Component, const int32 Section, const FMeshBuffers& Mesh)
	{
		if (!Component)
		{
			return;
		}
		if (Mesh.Vertices.IsEmpty())
		{
			Component->ClearMeshSection(Section);
			return;
		}
		Component->CreateMeshSection(Section, Mesh.Vertices, Mesh.Triangles, Mesh.Normals, Mesh.UVs, TArray<FColor>(),
			TArray<FProcMeshTangent>(), false);
	}

	UProceduralMeshComponent* SimaeMeshComponent(AActor* Owner, const TCHAR* Name, USceneComponent* Parent = nullptr)
	{
		UProceduralMeshComponent* Mesh = Owner->CreateDefaultSubobject<UProceduralMeshComponent>(Name);
		if (Parent)
		{
			Mesh->SetupAttachment(Parent);
		}
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetGenerateOverlapEvents(false);
		Mesh->SetCastShadow(false);
		Mesh->bUseAsOccluder = false;
		return Mesh;
	}

	void SimaeSetIntensity(UMaterialInstanceDynamic* Material, const float Value)
	{
		if (Material)
		{
			Material->SetScalarParameterValue(TEXT("Intensity"), FMath::Max(Value, 0.0f));
		}
	}

	/** A pale see-through tint (it shows on the white snow, unlike added light). */
	UMaterialInstanceDynamic* SimaeMakeTint(UObject* Outer, const FLinearColor& Color, const float Opacity)
	{
		UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Game/ChaosImpact/FX/M_CI_SimaeWarningFill.M_CI_SimaeWarningFill"), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!Parent)
		{
			return ChaosImpactBallTypes::MakeAdditive(Outer, Color, Opacity);
		}
		UMaterialInstanceDynamic* Tint = UMaterialInstanceDynamic::Create(Parent, Outer);
		Tint->SetVectorParameterValue(TEXT("Color"), Color);
		Tint->SetScalarParameterValue(TEXT("Opacity"), Opacity);
		return Tint;
	}

	void SimaeSetOpacity(UMaterialInstanceDynamic* Material, const float Opacity)
	{
		if (Material)
		{
			Material->SetScalarParameterValue(TEXT("Opacity"), FMath::Clamp(Opacity, 0.0f, 1.0f));
		}
	}

	void SimaeSetColor(UMaterialInstanceDynamic* Material, const FLinearColor& Color)
	{
		if (Material)
		{
			Material->SetVectorParameterValue(TEXT("Color"), Color);
			Material->SetVectorParameterValue(TEXT("Base Color"), Color);
		}
	}

	/** The shock's colour: icy white, turning to the full power's red. */
	FLinearColor SimaeShockColor(const float Heat)
	{
		return FMath::Lerp(FLinearColor(0.55f, 0.82f, 1.0f), FLinearColor(1.0f, 0.1f, 0.2f), FMath::Clamp(Heat, 0.0f, 1.0f));
	}
}

// ---------------------------------------------------------------------------------------------------- Shared

void ChaosImpactSimaeFx::PlaySnowDust(UWorld* World, const FVector& Location, const float Size, const float Seconds,
	const float Rate)
{
	using namespace ChaosImpactBallTypes;
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	UNiagaraSystem* Smoke = LoadEffect(Effects::Smoke);
	UNiagaraComponent* Cloud = Smoke ? UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, Smoke, Location) : nullptr;
	if (!Cloud)
	{
		return;
	}
	// Light: a few small puffs only. A plume's smoke lingers and swells, so more of it soon hides the whole fight.
	const float Puff = FMath::Min(Size * 0.35f, 110.0f);
	SetEffectColor(Cloud, TEXT("Smoke Color"), SnowDust);
	SetEffectSize(Cloud, TEXT("Sprite Size"), Puff);
	SetEffectSize(Cloud, TEXT("Uniform Sprite Size"), Puff);
	SetEffectFloat(Cloud, TEXT("SpawnRate"), FMath::Min(Rate, 14.0f));
	// A plume would go on rising for ever: it only puffs for a moment, then what it made drifts off and fades.
	FTimerHandle Stop;
	World->GetTimerManager().SetTimer(Stop, FTimerDelegate::CreateWeakLambda(Cloud, [Cloud]() { Cloud->Deactivate(); }),
		FMath::Clamp(Seconds, 0.05f, 0.12f), false);
}

void ChaosImpactSimaeFx::PlaySnowBurst(UWorld* World, const FVector& Location, const float Scale)
{
	using namespace ChaosImpactBallTypes;
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	if (UNiagaraSystem* Dirt = LoadEffect(Effects::DirtBurstSmall))
	{
		if (UNiagaraComponent* Burst = UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, Dirt, Location, FRotator::ZeroRotator,
			FVector(FMath::Clamp(Scale * 0.6f, 0.4f, 1.2f))))
		{
			Burst->SetAllowScalability(false);
			SetEffectColor(Burst, TEXT("Dirt Color"), FLinearColor(0.9f, 0.94f, 1.0f));
			SetEffectColor(Burst, TEXT("Smoke Color"), SnowDust);
		}
	}
	ChaosImpactBallTypes::PlaySnowBurst(World, Location + FVector(0, 0, 30), FMath::Clamp(Scale, 0.5f, 1.6f));
}

FVector ChaosImpactSimaeFx::FindGround(const UWorld* World, const FVector& Point, const AActor* Ignore)
{
	// The scenery only: characters (whoever stands on the spot), balls lying about and effects are seen through, or a
	// warning would be laid on top of someone's head. From just above the point, so nothing overhead is taken for the floor.
	FHitResult Floor;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SimaeGround), false, Ignore);
	const FCollisionObjectQueryParams Scenery(ECC_WorldStatic);
	if (World && World->LineTraceSingleByObjectType(Floor, Point + FVector(0, 0, 250), Point - FVector(0, 0, 3000), Scenery, Params))
	{
		return Floor.ImpactPoint;
	}
	return Point;
}

// ---------------------------------------------------------------------------------------------------- Warning

AChaosImpactSimaeWarning::AChaosImpactSimaeWarning()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Outline = SimaeMeshComponent(this, TEXT("Outline"), GetRootComponent());
	Fill = SimaeMeshComponent(this, TEXT("Fill"), GetRootComponent());
	SetActorEnableCollision(false);
	SetReplicates(false);
}

TFunction<void(UWorld*, const ChaosImpactSimaeFx::FNetFx&)> ChaosImpactSimaeFx::NetHook;

namespace
{
	/** A lane is being made (its circle and first aim are sent as one lane). */
	bool GSimaeSpawningLane = false;
	int32 GSimaeNextNetFxId = 0;
	void SimaeSendFx(UWorld* World, const ChaosImpactSimaeFx::FNetFx& Fx)
	{
		if (World && World->GetNetMode() == NM_ListenServer && ChaosImpactSimaeFx::NetHook)
		{
			ChaosImpactSimaeFx::NetHook(World, Fx);
		}
	}
}

AChaosImpactSimaeWarning* AChaosImpactSimaeWarning::SpawnCircle(UWorld* World, const FVector& Center, const float Radius,
	const float Seconds, const FLinearColor& Color)
{
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return nullptr;
	}
	AChaosImpactSimaeWarning* Warning = World->SpawnActor<AChaosImpactSimaeWarning>(Center + FVector(0, 0, 4), FRotator::ZeroRotator);
	if (Warning)
	{
		Warning->Radius = Radius;
		Warning->Seconds = FMath::Max(Seconds, 0.1f);
		Warning->Color = Color;
		Warning->OutlineMaterial = ChaosImpactBallTypes::MakeEmissive(Warning, Color, 3.0f);
		Warning->FillMaterial = SimaeMakeTint(Warning, Color, 0.22f);
		Warning->ProgressMaterial = SimaeMakeTint(Warning, Color, 0.2f);
		Warning->Outline->SetMaterial(0, Warning->OutlineMaterial);
		Warning->Fill->SetMaterial(0, Warning->FillMaterial);
		Warning->Fill->SetMaterial(1, Warning->ProgressMaterial);
		Warning->Rebuild();
		if (World->GetNetMode() == NM_ListenServer)
		{
			Warning->NetFxId = ++GSimaeNextNetFxId;
			if (!GSimaeSpawningLane)
			{
				ChaosImpactSimaeFx::FNetFx Fx;
				Fx.Kind = 0; Fx.Id = Warning->NetFxId; Fx.A = Center; Fx.F0 = Radius; Fx.F1 = Seconds; Fx.Color = Color;
				SimaeSendFx(World, Fx);
			}
		}
	}
	return Warning;
}

AChaosImpactSimaeWarning* AChaosImpactSimaeWarning::SpawnLane(UWorld* World, const FVector& Start, const FVector& Direction,
	const float Length, const float HalfWidth, const float Seconds, const FLinearColor& Color)
{
	GSimaeSpawningLane = true;
	AChaosImpactSimaeWarning* Warning = SpawnCircle(World, Start, HalfWidth, Seconds, Color);
	if (Warning)
	{
		Warning->bLane = true;
		Warning->HalfWidth = HalfWidth;
		Warning->SetLane(Start, Direction, Length);
	}
	GSimaeSpawningLane = false;
	if (Warning && Warning->NetFxId > 0)
	{
		ChaosImpactSimaeFx::FNetFx Fx;
		Fx.Kind = 1; Fx.Id = Warning->NetFxId; Fx.A = Start; Fx.B = Direction; Fx.F0 = Length; Fx.F1 = HalfWidth; Fx.F2 = Seconds; Fx.Color = Color;
		SimaeSendFx(World, Fx);
	}
	return Warning;
}

void AChaosImpactSimaeWarning::SetLane(const FVector& Start, const FVector& NewDirection, const float NewLength)
{
	const FVector Flat = NewDirection.GetSafeNormal2D();
	if (!Flat.IsNearlyZero())
	{
		Direction = Flat;
	}
	Length = FMath::Max(NewLength, 50.0f);
	SetActorLocationAndRotation(ChaosImpactSimaeFx::FindGround(GetWorld(), Start, this) + FVector(0, 0, 4), Direction.Rotation());
	if (NetFxId > 0 && !GSimaeSpawningLane)
	{
		ChaosImpactSimaeFx::FNetFx Fx;
		Fx.Kind = 4; Fx.Id = NetFxId; Fx.A = Start; Fx.B = Direction; Fx.F0 = Length;
		SimaeSendFx(GetWorld(), Fx);
	}
}

void AChaosImpactSimaeWarning::Finish()
{
	if (FinishedAge < 0.0f)
	{
		FinishedAge = Age;
		if (NetFxId > 0)
		{
			ChaosImpactSimaeFx::FNetFx Fx;
			Fx.Kind = 5; Fx.Id = NetFxId;
			SimaeSendFx(GetWorld(), Fx);
		}
	}
}

void AChaosImpactSimaeWarning::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	if (FinishedAge < 0.0f && Age > Seconds + 0.6f)
	{
		Finish();
	}
	if (FinishedAge >= 0.0f && Age - FinishedAge > 0.22f)
	{
		Destroy();
		return;
	}
	Rebuild();
}

void AChaosImpactSimaeWarning::Rebuild()
{
	const float Progress = FMath::Clamp(Age / Seconds, 0.0f, 1.0f);
	const float Flash = FinishedAge >= 0.0f ? 1.0f - (Age - FinishedAge) / 0.22f : 0.0f;
	// Blinking faster and faster as it is about to go off.
	const float Blink = 0.75f + 0.25f * FMath::Sin(Age * FMath::Lerp(7.0f, 26.0f, Progress));
	SimaeSetIntensity(OutlineMaterial, (FinishedAge >= 0.0f ? 4.0f * Flash : 2.2f * Blink));
	// Pale red all over the area; the part filled so far deeper; a flash as it goes off.
	SimaeSetOpacity(FillMaterial, FinishedAge >= 0.0f ? 0.45f * Flash : 0.2f + 0.06f * Blink);
	SimaeSetOpacity(ProgressMaterial, FinishedAge >= 0.0f ? 0.0f : 0.18f);
	FMeshBuffers Lines;
	FMeshBuffers Area;
	FMeshBuffers Filled;
	if (!bLane)
	{
		ChaosImpactLightning::AppendRing(Lines, FVector::ZeroVector, Radius, FMath::Clamp(Radius * 0.035f, 8.0f, 22.0f), 72);
		// Rings closing in on the centre, over and over.
		for (int32 Pulse = 0; Pulse < 2; ++Pulse)
		{
			const float Phase = FMath::Frac(Age * 1.3f + Pulse * 0.5f);
			ChaosImpactLightning::AppendRing(Lines, FVector::ZeroVector, Radius * (1.0f - 0.9f * Phase), 6.0f, 60);
		}
		SimaeDisc(Area, FVector(0, 0, -2), Radius, 64);
		// Filling up from the middle: when it is full, it goes off.
		SimaeDisc(Filled, FVector(0, 0, -1), Radius * Progress, 56);
	}
	else
	{
		const float Edge = FMath::Clamp(HalfWidth * 0.05f, 4.0f, 9.0f);
		const auto Strip = [&Lines](const FVector& A, const FVector& B, const float Half)
		{
			const FVector Side = FVector::CrossProduct(FVector::UpVector, (B - A).GetSafeNormal()) * Half;
			SimaeQuad(Lines, A - Side, B - Side, B + Side, A + Side);
		};
		Strip(FVector(0, -HalfWidth, 0), FVector(Length, -HalfWidth, 0), Edge);
		Strip(FVector(0, HalfWidth, 0), FVector(Length, HalfWidth, 0), Edge);
		Strip(FVector(Length, -HalfWidth - Edge, 0), FVector(Length, HalfWidth + Edge, 0), Edge);
		// Arrows racing along it.
		const float Spacing = FMath::Max(HalfWidth * 2.5f, 300.0f);
		const float Shift = FMath::Frac(Age * 1.8f) * Spacing;
		for (float X = Shift; X < Length - Spacing * 0.3f; X += Spacing)
		{
			const float Arm = HalfWidth * 0.35f;
			Strip(FVector(X - Arm * 0.7f, -Arm, 0), FVector(X, 0, 0), Edge * 0.6f);
			Strip(FVector(X - Arm * 0.7f, Arm, 0), FVector(X, 0, 0), Edge * 0.6f);
		}
		SimaeQuad(Area, FVector(0, -HalfWidth, -2), FVector(Length, -HalfWidth, -2), FVector(Length, HalfWidth, -2), FVector(0, HalfWidth, -2));
		const float Reached = Length * Progress;
		SimaeQuad(Filled, FVector(0, -HalfWidth, -1), FVector(Reached, -HalfWidth, -1), FVector(Reached, HalfWidth, -1), FVector(0, HalfWidth, -1));
	}
	SimaeSetSection(Outline, 0, Lines);
	SimaeSetSection(Fill, 0, Area);
	SimaeSetSection(Fill, 1, Filled);
}

// ---------------------------------------------------------------------------------------------------- Blast

AChaosImpactSimaeBlast::AChaosImpactSimaeBlast()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Rings = SimaeMeshComponent(this, TEXT("Rings"), GetRootComponent());
	Cracks = SimaeMeshComponent(this, TEXT("Cracks"), GetRootComponent());
	Flash = CreateDefaultSubobject<UPointLightComponent>(TEXT("Flash"));
	Flash->SetupAttachment(GetRootComponent());
	Flash->SetRelativeLocation(FVector(0, 0, 160));
	Flash->SetCastShadows(false);
	SetActorEnableCollision(false);
	SetReplicates(false);
}

AChaosImpactSimaeBlast* AChaosImpactSimaeBlast::Play(UWorld* World, const FVector& Center, const float Radius, const float Heat,
	const bool bCracks, const int32 ClodCount)
{
	{
		ChaosImpactSimaeFx::FNetFx Fx;
		Fx.Kind = 2; Fx.A = Center; Fx.F0 = Radius; Fx.F1 = Heat; Fx.bFlag = bCracks; Fx.Count = ClodCount;
		SimaeSendFx(World, Fx);
	}
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return nullptr;
	}
	AChaosImpactSimaeBlast* Blast = World->SpawnActor<AChaosImpactSimaeBlast>(Center + FVector(0, 0, 3), FRotator::ZeroRotator);
	if (!Blast)
	{
		return nullptr;
	}
	Blast->Radius = Radius;
	Blast->Heat = Heat;
	Blast->Seed = FMath::Rand();
	FRandomStream Stream(Blast->Seed);
	const FLinearColor Shock = SimaeShockColor(Heat);
	Blast->RingMaterial = ChaosImpactBallTypes::MakeAdditive(Blast, Shock, 3.0f);
	Blast->Rings->SetMaterial(0, Blast->RingMaterial);
	Blast->Rings->SetMaterial(1, Blast->RingMaterial);
	Blast->Flash->SetLightColor(Shock);
	Blast->Flash->SetAttenuationRadius(Radius * 2.5f + 400.0f);
	if (bCracks)
	{
		// Cracks in the packed snow: dark blue-grey, or glowing red when the full power struck.
		Blast->HotMaterial = Heat > 0.5f
			? ChaosImpactBallTypes::MakeEmissive(Blast, FLinearColor(1.0f, 0.08f, 0.12f), 4.0f)
			: ChaosImpactBallTypes::MakeEmissive(Blast, FLinearColor(0.32f, 0.42f, 0.58f), 0.75f);
		Blast->Cracks->SetMaterial(0, Blast->HotMaterial);
		FMeshBuffers Lines;
		const int32 Count = 9 + Stream.RandRange(0, 4);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const float Angle = (Index + Stream.FRandRange(-0.3f, 0.3f)) * UE_TWO_PI / Count;
			const FVector Out(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f);
			ChaosImpactIceMeshes::AppendCrack(Lines, FTransform::Identity, Out * Radius * 0.08f, Out,
				Radius * Stream.FRandRange(0.45f, 0.85f), FMath::Clamp(Radius * 0.04f, 10.0f, 34.0f), Stream);
		}
		SimaeSetSection(Blast->Cracks, 0, Lines);
	}
	// Clods of snow flung out, bouncing once.
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UMaterialInstanceDynamic* SnowLook = ChaosImpactBallTypes::MakeSnow(Blast);
	for (int32 Index = 0; Sphere && Index < ClodCount; ++Index)
	{
		UStaticMeshComponent* Clod = NewObject<UStaticMeshComponent>(Blast);
		Clod->SetStaticMesh(Sphere);
		Clod->SetMaterial(0, SnowLook);
		Clod->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Clod->SetCastShadow(false);
		Clod->SetupAttachment(Blast->GetRootComponent());
		Clod->RegisterComponent();
		FClod Motion;
		const float Angle = Stream.FRandRange(0.0f, UE_TWO_PI);
		const float Out = Stream.FRandRange(0.35f, 1.0f) * FMath::Clamp(Radius, 200.0f, 900.0f) * 1.2f;
		Motion.Velocity = FVector(FMath::Cos(Angle) * Out, FMath::Sin(Angle) * Out, Stream.FRandRange(500.0f, 1200.0f));
		Motion.Spin = FVector(Stream.FRandRange(-400, 400), Stream.FRandRange(-400, 400), Stream.FRandRange(-400, 400));
		Motion.Size = Stream.FRandRange(0.1f, 0.26f) * FMath::Clamp(Radius / 500.0f, 0.6f, 1.2f);
		Clod->SetRelativeLocation(FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.3f) * Radius * 0.12f);
		Clod->SetRelativeScale3D(FVector(Motion.Size, Motion.Size * 0.8f, Motion.Size * 0.7f));
		Blast->ClodMeshes.Add(Clod);
		Blast->Clods.Add(Motion);
	}
	Blast->SetLifeSpan(2.6f);
	Blast->Rebuild();
	return Blast;
}

void AChaosImpactSimaeBlast::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	constexpr float Gravity = 2400.0f;
	for (int32 Index = 0; Index < ClodMeshes.Num(); ++Index)
	{
		UStaticMeshComponent* Clod = ClodMeshes[Index];
		FClod& Motion = Clods[Index];
		if (!Clod)
		{
			continue;
		}
		FVector At = Clod->GetRelativeLocation() + Motion.Velocity * DeltaSeconds;
		Motion.Velocity.Z -= Gravity * DeltaSeconds;
		if (At.Z < 0.0f && Motion.Velocity.Z < 0.0f)
		{
			At.Z = 0.0f;
			Motion.Velocity *= FVector(0.45f, 0.45f, -0.3f);
		}
		Clod->SetRelativeLocation(At);
		Clod->AddRelativeRotation(FRotator(Motion.Spin.X, Motion.Spin.Y, Motion.Spin.Z) * DeltaSeconds);
		// Crumbling away toward the end.
		const float Left = FMath::Clamp((2.4f - Age) / 0.8f, 0.0f, 1.0f);
		Clod->SetRelativeScale3D(FVector(Motion.Size, Motion.Size * 0.8f, Motion.Size * 0.7f) * Left);
	}
	Rebuild();
}

void AChaosImpactSimaeBlast::Rebuild()
{
	FMeshBuffers Flat;
	FMeshBuffers Walls;
	for (int32 Wave = 0; Wave < 3; ++Wave)
	{
		const float Local = (Age - Wave * 0.09f) / (0.5f + Wave * 0.12f);
		if (Local <= 0.0f || Local >= 1.0f)
		{
			continue;
		}
		const float Out = 1.0f - FMath::Pow(1.0f - Local, 3.0f);
		const float R = Radius * (0.12f + 0.95f * Out) * (Wave == 2 ? 1.12f : 1.0f);
		const float Thin = 1.0f - Local;
		ChaosImpactLightning::AppendRing(Flat, FVector::ZeroVector, R, (Wave == 0 ? 60.0f : 26.0f) * Thin + 4.0f, 80);
		if (Wave == 0)
		{
			// A wall of wind standing up round the first ring (it reads from the high camera).
			SimaeWall(Walls, FVector::ZeroVector, R, FMath::Clamp(Radius * 0.35f, 60.0f, 320.0f) * Thin, 48);
		}
	}
	SimaeSetSection(Rings, 0, Flat);
	SimaeSetSection(Rings, 1, Walls);
	const float Glow = FMath::Clamp(1.0f - Age / 0.7f, 0.0f, 1.0f);
	SimaeSetIntensity(RingMaterial, 3.5f * Glow + 0.4f);
	Flash->SetIntensity(FMath::Square(FMath::Clamp(1.0f - Age / 0.45f, 0.0f, 1.0f)) * (8000.0f + Radius * 25.0f));
	if (HotMaterial)
	{
		// The cracks close up over the last second.
		const float Left = FMath::Clamp((2.5f - Age) / 1.0f, 0.0f, 1.0f);
		Cracks->SetRelativeScale3D(FVector(1.0f, 1.0f, 1.0f) * FMath::Max(Left, 0.01f));
		SimaeSetIntensity(HotMaterial, (Heat > 0.5f ? 4.0f * (0.7f + 0.3f * FMath::Sin(Age * 20.0f)) : 0.75f) * Left);
	}
}

// ---------------------------------------------------------------------------------------------------- Goukyu

AChaosImpactSimaeGoukyu::AChaosImpactSimaeGoukyu()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Wind = SimaeMeshComponent(this, TEXT("Wind"), GetRootComponent());
	// The furrow stays on the ground where it was ploughed (it does not move with the ball).
	Furrow = SimaeMeshComponent(this, TEXT("Furrow"), GetRootComponent());
	Furrow->SetUsingAbsoluteLocation(true);
	Furrow->SetUsingAbsoluteRotation(true);
	Glow = CreateDefaultSubobject<UPointLightComponent>(TEXT("Glow"));
	Glow->SetupAttachment(GetRootComponent());
	Glow->SetCastShadows(false);
	Glow->SetAttenuationRadius(1500.0f);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	for (TObjectPtr<UStaticMeshComponent>* Part : {&Core, &Shell})
	{
		*Part = CreateDefaultSubobject<UStaticMeshComponent>(Part == &Core ? TEXT("Core") : TEXT("Shell"));
		(*Part)->SetupAttachment(GetRootComponent());
		(*Part)->SetStaticMesh(Sphere.Object);
		(*Part)->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		(*Part)->SetCastShadow(false);
	}
	SetActorEnableCollision(false);
	SetReplicates(false);
}

AChaosImpactSimaeGoukyu* AChaosImpactSimaeGoukyu::Launch(AChaosImpactSimaeBoss* Boss, AChaosImpactBall* Ball, const FVector& Start,
	const FVector& Direction, const float Speed, const float HitRadius, const float Range, const float Knockback, const float Heat)
{
	UWorld* World = Boss ? Boss->GetWorld() : nullptr;
	const FVector Flat = Direction.GetSafeNormal2D();
	if (!World || Flat.IsNearlyZero())
	{
		return nullptr;
	}
	AChaosImpactSimaeGoukyu* Shot = World->SpawnActor<AChaosImpactSimaeGoukyu>(Start, Flat.Rotation());
	if (!Shot)
	{
		return nullptr;
	}
	Shot->Boss = Boss;
	Shot->Ball = Ball;
	Shot->Origin = Start;
	Shot->Direction = Flat;
	Shot->Speed = Speed;
	Shot->HitRadius = HitRadius;
	Shot->Range = Range;
	Shot->Knockback = Knockback;
	Shot->Heat = Heat;
	Shot->GroundZ = ChaosImpactSimaeFx::FindGround(World, Start, Boss).Z;
	Shot->StartHeight = Start.Z - Shot->GroundZ;
	Shot->bFlying = true;
	if (Ball)
	{
		Ball->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		Ball->SetActorLocation(Start);
	}
	const FLinearColor WindColor = FMath::Lerp(FLinearColor(0.62f, 0.86f, 1.0f), FLinearColor(1.0f, 0.24f, 0.36f), Heat);
	Shot->WindMaterial = ChaosImpactBallTypes::MakeAdditive(Shot, WindColor, 2.4f);
	Shot->StreakMaterial = ChaosImpactBallTypes::MakeEmissive(Shot, FMath::Lerp(FLinearColor(0.85f, 0.95f, 1.0f), FLinearColor(1.0f, 0.55f, 0.6f), Heat), 1.6f);
	// The ploughed track: a shaded blue groove in the snow.
	Shot->FurrowMaterial = ChaosImpactBallTypes::MakeEmissive(Shot, FLinearColor(0.5f, 0.62f, 0.8f), 0.85f);
	Shot->Wind->SetMaterial(0, Shot->WindMaterial);
	Shot->Wind->SetMaterial(1, Shot->StreakMaterial);
	Shot->Furrow->SetMaterial(0, Shot->FurrowMaterial);
	Shot->Glow->SetLightColor(WindColor);
	Shot->Glow->SetIntensity(20000.0f);
	// A white-hot core round the ball, a rim of pressed air round that.
	Shot->CoreMaterial = ChaosImpactBallTypes::MakeAdditive(Shot, FMath::Lerp(FLinearColor(0.75f, 0.92f, 1.0f), FLinearColor(1.0f, 0.55f, 0.6f), Heat), 3.0f);
	Shot->ShellMaterial = ChaosImpactBallTypes::MakeAdditive(Shot, WindColor, 2.0f, 1.0f);
	Shot->Core->SetMaterial(0, Shot->CoreMaterial);
	Shot->Shell->SetMaterial(0, Shot->ShellMaterial);
	Shot->Core->SetRelativeScale3D(FVector(0.9f));
	Shot->Shell->SetRelativeScale3D(FVector(HitRadius * 1.1f / 100.0f, HitRadius * 0.9f / 100.0f, HitRadius * 0.9f / 100.0f));
	{
		using namespace ChaosImpactBallTypes;
		const auto Attach = [Shot](const TCHAR* Path) -> UNiagaraComponent*
		{
			UNiagaraSystem* System = LoadEffect(Path);
			UNiagaraComponent* Effect = System ? UNiagaraFunctionLibrary::SpawnSystemAttached(System, Shot->GetRootComponent(), NAME_None,
				FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset, false) : nullptr;
			if (Effect)
			{
				Effect->SetAllowScalability(false);
				Shot->Trails.Add(Effect);
			}
			return Effect;
		};
		Attach(Effects::BallTrail);
		if (UNiagaraComponent* Sparks = Attach(Effects::WindSparks))
		{
			Sparks->SetUsingAbsoluteScale(true);
			Sparks->SetWorldScale3D(FVector(FMath::Max(HitRadius / 90.0f, 1.0f)));
			SetEffectFloat(Sparks, TEXT("Spark Spawn Rate"), 260.0f);
		}
		if (Heat > 0.5f)
		{
			// Full power: it burns as it goes.
			if (UNiagaraComponent* Fire = Attach(Effects::FireTrail))
			{
				SetEffectColor(Fire, TEXT("Smoke Color"), FLinearColor(0.3f, 0.08f, 0.1f));
			}
		}
		// The flash as it leaves the hand.
		if (UNiagaraSystem* Flash = LoadEffect(Effects::MuzzleFlash))
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, Flash, Start, Flat.Rotation(), FVector(3.0f));
		}
		if (UNiagaraSystem* Sparks = LoadEffect(Effects::SparkBurst))
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, Sparks, Start, FRotator::ZeroRotator, FVector(2.5f));
		}
	}
	Shot->Roar = ChaosImpactSfx::PlayAttached(EChaosImpactSfx::WindLoop, Shot->GetRootComponent(), 1.0f, 0.8f);
	ChaosImpactSfx::PlayAt(Shot, EChaosImpactSfx::WindGust, Start, 1.0f, 0.7f);
	// A burst of powder where it leaves the hand.
	ChaosImpactSimaeFx::PlaySnowDust(World, FVector(Start.X, Start.Y, Shot->GroundZ), 200.0f, 0.08f, 12.0f);
	Shot->RebuildWind();
	return Shot;
}

void AChaosImpactSimaeGoukyu::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ChaosImpactSfx::Stop(Roar.Get(), 0.2f);
	Super::EndPlay(EndPlayReason);
}

void AChaosImpactSimaeGoukyu::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (!bFlying)
	{
		// What is left: the furrow closing up and the wind blowing itself out.
		const float Since = Age - StoppedAge;
		SimaeSetIntensity(WindMaterial, 2.4f * FMath::Clamp(1.0f - Since / 0.3f, 0.0f, 1.0f));
		SimaeSetIntensity(StreakMaterial, 1.6f * FMath::Clamp(1.0f - Since / 0.3f, 0.0f, 1.0f));
		Wind->SetVisibility(Since < 0.3f);
		Core->SetVisibility(false);
		Shell->SetVisibility(false);
		Furrow->SetWorldScale3D(FVector(1.0f, 1.0f, 1.0f));
		const float Left = FMath::Clamp(1.0f - (Since - 0.6f) / 1.2f, 0.0f, 1.0f);
		SimaeSetIntensity(FurrowMaterial, 0.85f * Left);
		Glow->SetIntensity(8000.0f * FMath::Clamp(1.0f - Since / 0.25f, 0.0f, 1.0f));
		if (Since > 1.9f)
		{
			Destroy();
		}
		return;
	}
	const FVector Previous = GetActorLocation();
	float Step = Speed * DeltaSeconds;
	bool bEnd = false;
	if (Travelled + Step >= Range)
	{
		Step = FMath::Max(Range - Travelled, 0.0f);
		bEnd = true;
	}
	Travelled += Step;
	FVector Next = Previous + Direction * Step;
	// Low over the snow: from the hand down to about chest height in its first few metres.
	Next.Z = GroundZ + FMath::Lerp(StartHeight, 120.0f, FMath::Clamp(Travelled / 500.0f, 0.0f, 1.0f));
	// Nothing stops it: it tears straight through walls and cover to the end of its range.
	SetActorLocation(Next);
	if (AChaosImpactBall* Carried = Ball.Get())
	{
		Carried->SetActorLocation(Next);
		Carried->AddActorLocalRotation(FRotator(-Speed * DeltaSeconds * 0.9f, 0.0f, 0.0f));
	}
	// Everyone in the lane it tore through is hit and blown along it (and out of it).
	for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
	{
		AChaosImpactCharacter* Victim = *It;
		if (!Boss.IsValid() || !Boss->IsFoe(Victim) || AlreadyHit.Contains(Victim))
		{
			continue;
		}
		const FVector At = Victim->GetActorLocation();
		const FVector Closest = FMath::ClosestPointOnSegment(FVector(At.X, At.Y, Previous.Z), Previous, Next);
		const float Reach = HitRadius + Victim->GetCapsuleComponent()->GetScaledCapsuleRadius();
		if (FVector::Dist2D(Closest, At) > Reach || FMath::Abs(At.Z - GroundZ) > 450.0f)
		{
			continue;
		}
		AlreadyHit.Add(Victim);
		const FVector Out = FVector::CrossProduct(FVector::UpVector, Direction)
			* FMath::Sign(FVector::DotProduct(At - Closest, FVector::CrossProduct(FVector::UpVector, Direction)) + 0.01f);
		const FVector Knock = Direction * Knockback + Out * Knockback * 0.35f + FVector(0, 0, 520.0f);
		if (AChaosImpactSimaeBoss::Strike(Boss.Get(), Victim, this, 1.0f, Knock, false))
		{
			const FVector Feet = ChaosImpactSimaeFx::FindGround(World, At, Victim);
			AChaosImpactSimaeBlast::Play(World, Feet, 200.0f, Heat, false, 4);
			ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::BallHit, At, 1.0f, 0.7f);
		}
	}
	// Ploughing up the snow: powder thrown up either side of its path.
	while (Travelled >= NextDustAt)
	{
		const FVector Spot = Origin + Direction * NextDustAt;
		const FVector Ground = ChaosImpactSimaeFx::FindGround(World, FVector(Spot.X, Spot.Y, GroundZ + 50.0f), this);
		GroundZ = Ground.Z;
		// A light puff now and then, either side in turn.
		const float Which = FMath::Fmod(NextDustAt, 800.0f) < 1.0f ? 1.0f : -1.0f;
		const FVector Side = FVector::CrossProduct(FVector::UpVector, Direction) * HitRadius * 0.7f * Which;
		ChaosImpactSimaeFx::PlaySnowDust(World, Ground + Side, HitRadius * 1.2f, 0.08f, 12.0f);
		NextDustAt += 400.0f;
	}
	// The groove behind it.
	FMeshBuffers Track;
	const FVector Side = FVector::CrossProduct(FVector::UpVector, Direction);
	const FVector From(Origin.X, Origin.Y, GroundZ + 2.0f);
	const FVector To(Next.X, Next.Y, GroundZ + 2.0f);
	const float Groove = HitRadius * 0.22f;
	SimaeQuad(Track, From - Side * Groove, To - Side * Groove, To + Side * Groove, From + Side * Groove);
	for (const float Ridge : {-1.0f, 1.0f})
	{
		const FVector Off = Side * Ridge * HitRadius * 0.85f;
		SimaeQuad(Track, From + Off - Side * 6.0f, To + Off - Side * 6.0f, To + Off + Side * 6.0f, From + Off + Side * 6.0f);
	}
	SimaeSetSection(Furrow, 0, Track);
	Glow->SetIntensity(20000.0f * (0.85f + 0.15f * FMath::Sin(Age * 40.0f)));
	Core->SetRelativeScale3D(FVector(0.85f + 0.15f * FMath::Sin(Age * 50.0f)));
	Shell->AddRelativeRotation(FRotator(0.0f, 0.0f, 900.0f * DeltaSeconds));
	SimaeSetIntensity(ShellMaterial, 1.6f + 0.6f * FMath::Sin(Age * 30.0f));
	// A shock ring bursts off it every few metres and hangs in the air behind, widening.
	while (Travelled >= NextBoomAt)
	{
		Booms.Add(NextBoomAt);
		NextBoomAt += 300.0f;
	}
	Booms.RemoveAll([this](const float At) { return (Travelled - At) / Speed > 0.4f; });
	// Those it rushes past feel it.
	if (Age >= NextShakeAt)
	{
		NextShakeAt = Age + 0.12f;
		if (AChaosImpactSimaeBoss* Thrower = Boss.Get())
		{
			Thrower->ShakeNearby(Next, HitRadius + 700.0f, 0.3f, 0.2f);
		}
	}
	RebuildWind();
	if (bEnd)
	{
		Stop(Next);
	}
}

void AChaosImpactSimaeGoukyu::Stop(const FVector& Where)
{
	bFlying = false;
	StoppedAge = Age;
	ChaosImpactSfx::Stop(Roar.Get(), 0.3f);
	UWorld* World = GetWorld();
	const FVector Ground = ChaosImpactSimaeFx::FindGround(World, Where, this);
	if (AChaosImpactBall* Carried = Ball.Get())
	{
		// Spent: it is just a ball again, lying where it stopped for anyone to pick up (back inside the arena if it flew
		// out through a wall).
		const FVector Spot = Boss.IsValid() ? Boss->ClampToArena(Where - Direction * 40.0f, 150.0f) : Where - Direction * 40.0f;
		const FVector Floor = ChaosImpactSimaeFx::FindGround(World, Spot, this);
		Carried->SetActorLocation(FVector(Spot.X, Spot.Y, Floor.Z + 60.0f));
		Carried->MakeRollingPickup(-Direction * 300.0f);
	}
	Ball.Reset();
	for (UNiagaraComponent* Trail : Trails)
	{
		if (Trail)
		{
			// Lets what it already made fade on its own.
			Trail->Deactivate();
		}
	}
	if (UNiagaraSystem* Sparks = ChaosImpactBallTypes::LoadEffect(ChaosImpactBallTypes::Effects::SparkBurst))
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, Sparks, Where, FRotator::ZeroRotator, FVector(2.5f));
	}
	AChaosImpactSimaeBlast::Play(World, Ground, HitRadius * 1.6f, Heat, false, 5);
	ChaosImpactSimaeFx::PlaySnowBurst(World, Ground, 0.9f);
	ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::SnowBurst, Where, 1.0f, 0.8f);
	if (AChaosImpactSimaeBoss* Thrower = Boss.Get())
	{
		Thrower->ShakeNearby(Where, 1400.0f, 0.35f, 0.4f);
	}
}

void AChaosImpactSimaeGoukyu::RebuildWind()
{
	// In the shot's own space: +X ahead, the ball at the origin.
	FMeshBuffers Swirl;
	FMeshBuffers Lines;
	const float R = HitRadius;
	// Three wind helices round the line of flight, widening behind the ball.
	for (int32 Curl = 0; Curl < 3; ++Curl)
	{
		constexpr int32 Steps = 36;
		for (int32 I = 0; I < Steps; ++I)
		{
			const auto Point = [&](const float U, const float Edge)
			{
				const float Angle = Curl * UE_TWO_PI / 3.0f + U * UE_PI * 2.2f - Age * 22.0f;
				const float Radius = R * (0.35f + 0.6f * U);
				const float Width = FMath::Sin(U * UE_PI) * R * 0.09f;
				return FVector(40.0f - U * R * 4.2f, FMath::Cos(Angle) * (Radius + Edge * Width),
					FMath::Sin(Angle) * (Radius + Edge * Width) * 0.75f);
			};
			const float A = I / static_cast<float>(Steps);
			const float B = (I + 1) / static_cast<float>(Steps);
			SimaeQuad(Swirl, Point(A, -1), Point(B, -1), Point(B, 1), Point(A, 1));
		}
	}
	// The bow wave: a cone of pressed air just ahead.
	constexpr int32 Ribs = 20;
	for (int32 I = 0; I < Ribs; ++I)
	{
		const float A = I * UE_TWO_PI / Ribs;
		const float B = (I + 1) * UE_TWO_PI / Ribs;
		const float Back = R * 0.9f;
		const FVector Tip(55.0f, 0.0f, 0.0f);
		const FVector PA(-Back * 0.6f, FMath::Cos(A) * R * 0.55f, FMath::Sin(A) * R * 0.45f);
		const FVector PB(-Back * 0.6f, FMath::Cos(B) * R * 0.55f, FMath::Sin(B) * R * 0.45f);
		SimaeQuad(Swirl, Tip, Tip, PB, PA);
	}
	// The shock rings left hanging behind, standing across the line of flight, widening and thinning out.
	for (const float At : Booms)
	{
		const float Since = (Travelled - At) / Speed;
		const float Grow = 1.0f - FMath::Square(1.0f - FMath::Clamp(Since / 0.4f, 0.0f, 1.0f));
		const float RingRadius = R * (0.55f + 0.9f * Grow);
		const float Thick = FMath::Max(R * 0.07f * (1.0f - Grow), 1.5f);
		const float X = At - Travelled;
		constexpr int32 Segments = 28;
		for (int32 I = 0; I < Segments; ++I)
		{
			const float A = I * UE_TWO_PI / Segments;
			const float B = (I + 1) * UE_TWO_PI / Segments;
			const auto P = [&](const float Angle, const float Radius)
			{
				return FVector(X, FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius * 0.8f);
			};
			SimaeQuad(Lines, P(A, RingRadius - Thick), P(B, RingRadius - Thick), P(B, RingRadius + Thick), P(A, RingRadius + Thick));
		}
	}
	// Speed lines streaming off behind.
	FRandomStream Stream(1234);
	for (int32 I = 0; I < 14; ++I)
	{
		const float Angle = Stream.FRandRange(0.0f, UE_TWO_PI);
		const float Radius = R * Stream.FRandRange(0.3f, 1.05f);
		const float Run = FMath::Frac(Age * 2.5f + Stream.FRand());
		const float Length = R * Stream.FRandRange(1.5f, 3.5f);
		const FVector Head(-R * 0.4f - Run * R * 2.0f, FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius * 0.7f);
		const FVector Tail = Head - FVector(Length, 0, 0);
		const FVector Across = FVector(0, -FMath::Sin(Angle), FMath::Cos(Angle)) * 3.5f;
		SimaeQuad(Lines, Tail - Across * 0.3f, Head - Across, Head + Across, Tail + Across * 0.3f);
	}
	SimaeSetSection(Wind, 0, Swirl);
	SimaeSetSection(Wind, 1, Lines);
}

// ---------------------------------------------------------------------------------------------------- Boulder

AChaosImpactSimaeBoulder::AChaosImpactSimaeBoulder()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	Ball = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Snowball"));
	Ball->SetupAttachment(Root);
	Ball->SetStaticMesh(Sphere.Object);
	Ball->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Ball->SetHiddenInGame(true);
	Shadow = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Shadow"));
	Shadow->SetupAttachment(Root);
	Shadow->SetStaticMesh(Cylinder.Object);
	Shadow->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Shadow->SetCastShadow(false);
	Shadow->SetHiddenInGame(true);
	SetActorEnableCollision(false);
	SetReplicates(false);
}

AChaosImpactSimaeBoulder* AChaosImpactSimaeBoulder::Drop(AChaosImpactSimaeBoss* Boss, const FVector& Ground, const float Size,
	const float HitRadius, const float WarningSeconds, const float Knockback, const float Heat)
{
	UWorld* World = Boss ? Boss->GetWorld() : nullptr;
	AChaosImpactSimaeBoulder* Rock = World ? World->SpawnActor<AChaosImpactSimaeBoulder>(Ground, FRotator::ZeroRotator) : nullptr;
	if (!Rock)
	{
		return nullptr;
	}
	Rock->Boss = Boss;
	Rock->Ground = Ground;
	Rock->Size = Size;
	Rock->HitRadius = HitRadius;
	Rock->WarningSeconds = WarningSeconds;
	Rock->Knockback = Knockback;
	Rock->Heat = Heat;
	Rock->Warning = AChaosImpactSimaeWarning::SpawnCircle(World, Ground, HitRadius, WarningSeconds + FallSeconds,
		FLinearColor(1.0f, 0.06f, 0.1f));
	UMaterialInstanceDynamic* Snow = ChaosImpactBallTypes::MakeSnow(Rock);
	Rock->Ball->SetMaterial(0, Snow);
	Rock->Ball->SetRelativeScale3D(FVector(Size * 2.0f / 100.0f));
	ChaosImpactBallTypes::AttachSnowLumps(Rock, Rock->Ball, Snow, FMath::Rand());
	// Its shadow, a dark spot growing on the snow as it comes down.
	Rock->ShadowMaterial = ChaosImpactBallTypes::MakeEmissive(Rock, FLinearColor(0.16f, 0.2f, 0.3f), 1.0f);
	Rock->Shadow->SetMaterial(0, Rock->ShadowMaterial);
	return Rock;
}

void AChaosImpactSimaeBoulder::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	if (bLanded)
	{
		return;
	}
	const float Fall = (Age - WarningSeconds) / FallSeconds;
	const float Shade = FMath::Clamp((Age - WarningSeconds * 0.4f) / (WarningSeconds * 0.6f + FallSeconds), 0.0f, 1.0f);
	Shadow->SetHiddenInGame(Shade <= 0.0f);
	Shadow->SetRelativeLocation(FVector(0, 0, 1.5f));
	Shadow->SetRelativeScale3D(FVector(Size * 2.0f / 100.0f * (0.3f + 0.8f * Shade), Size * 2.0f / 100.0f * (0.3f + 0.8f * Shade), 0.01f));
	if (Fall < 0.0f)
	{
		return;
	}
	Ball->SetHiddenInGame(false);
	const float Drop = FMath::Clamp(Fall, 0.0f, 1.0f);
	Ball->SetRelativeLocation(FVector(0, 0, Size * 0.85f + FallHeight * (1.0f - Drop * Drop)));
	Ball->AddRelativeRotation(FRotator(420.0f * DeltaSeconds, 160.0f * DeltaSeconds, 0.0f));
	if (Fall >= 1.0f)
	{
		Land();
	}
}

void AChaosImpactSimaeBoulder::Land()
{
	bLanded = true;
	UWorld* World = GetWorld();
	if (AChaosImpactSimaeWarning* Shown = Warning.Get())
	{
		Shown->Finish();
	}
	for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
	{
		AChaosImpactCharacter* Victim = *It;
		const FVector At = Victim->GetActorLocation();
		if (!Boss.IsValid() || !Boss->IsFoe(Victim) || FVector::Dist2D(At, Ground) > HitRadius + 40.0f
			|| FMath::Abs(At.Z - Ground.Z) > 500.0f)
		{
			continue;
		}
		FVector Out = (At - Ground).GetSafeNormal2D();
		if (Out.IsNearlyZero())
		{
			Out = FVector::ForwardVector;
		}
		AChaosImpactSimaeBoss::Strike(Boss.Get(), Victim, this, 1.0f, Out * Knockback + FVector(0, 0, 520.0f), false);
	}
	AChaosImpactSimaeBlast::Play(World, Ground, HitRadius * 1.3f, Heat, true, 6);
	ChaosImpactSimaeFx::PlaySnowBurst(World, Ground + FVector(0, 0, 40), 1.0f);
	ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::SnowBurst, Ground, 1.0f, 0.65f);
	ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::IceBurst, Ground, 0.8f, 0.6f);
	if (AChaosImpactSimaeBoss* Thrower = Boss.Get())
	{
		Thrower->ShakeNearby(Ground, 1600.0f, 0.45f, 0.45f);
	}
	Destroy();
}

// ---------------------------------------------------------------------------------------------------- Flame burst

AChaosImpactSimaeFlameBurst::AChaosImpactSimaeFlameBurst()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Flames = SimaeMeshComponent(this, TEXT("Flames"), GetRootComponent());
	Flash = CreateDefaultSubobject<UPointLightComponent>(TEXT("Flash"));
	Flash->SetupAttachment(GetRootComponent());
	Flash->SetRelativeLocation(FVector(0, 0, 250));
	Flash->SetLightColor(FLinearColor(1.0f, 0.15f, 0.2f));
	Flash->SetCastShadows(false);
	Flash->SetAttenuationRadius(3000.0f);
	SetActorEnableCollision(false);
	SetReplicates(false);
}

AChaosImpactSimaeFlameBurst* AChaosImpactSimaeFlameBurst::Play(UWorld* World, const FVector& Ground, const float Height, const int32 Count,
	const bool bColumn)
{
	{
		ChaosImpactSimaeFx::FNetFx Fx;
		Fx.Kind = 3; Fx.A = Ground; Fx.F0 = Height; Fx.Count = Count; Fx.bFlag = bColumn;
		SimaeSendFx(World, Fx);
	}
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return nullptr;
	}
	AChaosImpactSimaeFlameBurst* Burst = World->SpawnActor<AChaosImpactSimaeFlameBurst>(Ground, FRotator::ZeroRotator);
	if (!Burst)
	{
		return nullptr;
	}
	Burst->Ground = Ground;
	Burst->Height = Height;
	Burst->bColumn = bColumn;
	// The aura's own smoky flame, brighter and solid at its heart.
	if (UMaterialInterface* Flame = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Game/ChaosImpact/FX/M_CI_SimaeAuraFlame.M_CI_SimaeAuraFlame"), nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		Burst->FlameMaterial = UMaterialInstanceDynamic::Create(Flame, Burst);
		Burst->FlameMaterial->SetScalarParameterValue(TEXT("Intensity"), 6.0f);
		Burst->FlameMaterial->SetScalarParameterValue(TEXT("Speed"), 2.6f);
		Burst->FlameMaterial->SetScalarParameterValue(TEXT("Opacity"), 1.0f);
	}
	else
	{
		Burst->FlameMaterial = ChaosImpactBallTypes::MakeAdditive(Burst, FLinearColor(1.0f, 0.1f, 0.2f), 3.0f);
	}
	Burst->Flames->SetMaterial(0, Burst->FlameMaterial);
	FRandomStream Stream(FMath::Rand());
	for (int32 Index = 0; Index < Count; ++Index)
	{
		// Out from the chest in every direction, mostly up and out, some low and fast along the ground.
		FBlob Blob;
		const float Angle = (Index + Stream.FRandRange(-0.4f, 0.4f)) * UE_TWO_PI / Count;
		const float Out = Stream.FRandRange(500.0f, 1300.0f) * (bColumn ? 1.0f : 0.45f);
		Blob.Location = Ground + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * Height * 0.12f + FVector(0, 0, Height * Stream.FRandRange(0.35f, 0.7f));
		Blob.Velocity = FVector(FMath::Cos(Angle) * Out, FMath::Sin(Angle) * Out, Stream.FRandRange(350.0f, 1300.0f));
		Blob.Size = Stream.FRandRange(70.0f, 150.0f) * (bColumn ? 1.0f : 0.6f);
		Blob.Seed = Stream.FRand();
		Blob.Life = Stream.FRandRange(1.0f, 1.6f);
		if (UNiagaraSystem* Fire = ChaosImpactBallTypes::LoadEffect(ChaosImpactBallTypes::Effects::Fire))
		{
			if (UNiagaraComponent* Flame = UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, Fire, Blob.Location, FRotator::ZeroRotator,
				FVector::OneVector, true, true, ENCPoolMethod::None, true))
			{
				Flame->SetAllowScalability(false);
				ChaosImpactBallTypes::SetEffectFloat(Flame, TEXT("Flame Scale"), Blob.Size / (bColumn ? 70.0f : 90.0f));
				// Barely any smoke: it must not hide the fight.
				ChaosImpactBallTypes::SetEffectFloat(Flame, TEXT("Smoke Spawn Scale"), 0.05f);
				ChaosImpactBallTypes::SetEffectFloat(Flame, TEXT("Base Light Intentsity"), 0.0f);
				Blob.Fire = Flame;
			}
		}
		Burst->Blobs.Add(Blob);
	}
	if (bColumn)
	{
		// A ring of great fires bursting up round the body for a moment.
		if (UNiagaraSystem* Fire = ChaosImpactBallTypes::LoadEffect(ChaosImpactBallTypes::Effects::Fire))
		{
			for (int32 Index = 0; Index < 8; ++Index)
			{
				const float Angle = Index * UE_TWO_PI / 8.0f;
				const FVector Spot = Ground + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * Height * 0.25f;
				if (UNiagaraComponent* Flame = UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, Fire, Spot))
				{
					Flame->SetAllowScalability(false);
					ChaosImpactBallTypes::SetEffectFloat(Flame, TEXT("Flame Scale"), 3.2f);
					ChaosImpactBallTypes::SetEffectFloat(Flame, TEXT("Smoke Spawn Scale"), 0.05f);
					ChaosImpactBallTypes::SetEffectFloat(Flame, TEXT("Base Light Intentsity"), 0.0f);
					FTimerHandle Out;
					World->GetTimerManager().SetTimer(Out, FTimerDelegate::CreateWeakLambda(Flame, [Flame]() { Flame->Deactivate(); }), 0.9f, false);
				}
			}
		}
	}
	Burst->SetLifeSpan(2.6f);
	Burst->Rebuild();
	return Burst;
}

void AChaosImpactSimaeFlameBurst::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	UWorld* World = GetWorld();
	constexpr float Gravity = 1700.0f;
	for (FBlob& Blob : Blobs)
	{
		if (Blob.bLanded || Age > Blob.Life)
		{
			// Burnt out in the air: its fire goes out with it.
			if (!Blob.bLanded && Blob.Fire.IsValid())
			{
				Blob.Fire->Deactivate();
				Blob.Fire.Reset();
			}
			continue;
		}
		Blob.Location += Blob.Velocity * DeltaSeconds;
		Blob.Velocity.Z -= Gravity * DeltaSeconds;
		Blob.Velocity *= FMath::Max(0.0f, 1.0f - 0.6f * DeltaSeconds);
		if (UNiagaraComponent* Flame = Blob.Fire.Get())
		{
			Flame->SetWorldLocation(Blob.Location);
		}
		if (Blob.Location.Z <= Ground.Z + 10.0f && Blob.Velocity.Z < 0.0f)
		{
			// Lands and burns there a moment, then goes out.
			Blob.bLanded = true;
			Blob.Life = Age;
			if (UNiagaraComponent* Flame = Blob.Fire.Get())
			{
				Flame->SetWorldLocation(FVector(Blob.Location.X, Blob.Location.Y, Ground.Z));
				FTimerHandle Out;
				World->GetTimerManager().SetTimer(Out, FTimerDelegate::CreateWeakLambda(Flame, [Flame]() { Flame->Deactivate(); }),
					FMath::FRandRange(0.6f, 1.2f), false);
			}
		}
	}
	Flash->SetIntensity(FMath::Square(FMath::Clamp(1.0f - Age / 0.8f, 0.0f, 1.0f)) * (bColumn ? 60000.0f : 8000.0f));
	Rebuild();
}

void AChaosImpactSimaeFlameBurst::Rebuild()
{
	const UWorld* World = GetWorld();
	const APlayerController* Viewer = World ? World->GetFirstPlayerController() : nullptr;
	const FRotator View = Viewer && Viewer->PlayerCameraManager ? Viewer->PlayerCameraManager->GetCameraRotation() : FRotator(-60, 0, 0);
	const FRotationMatrix Axes(View);
	const FVector Ahead = Axes.GetUnitAxis(EAxis::X);
	const FVector ScreenUp = Axes.GetUnitAxis(EAxis::Z);
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colors;
	// A card from Root reaching along Up (flat to the camera), Width across.
	const auto Card = [&](const FVector& Root, const FVector& Up, const float Width, const float Length, const float Seed, const float Strength)
	{
		FVector Along = Up - Ahead * FVector::DotProduct(Up, Ahead);
		Along = Along.IsNearlyZero() ? ScreenUp : Along.GetSafeNormal();
		const FVector Across = FVector::CrossProduct(Ahead, Along).GetSafeNormal();
		const int32 Base = Vertices.Num();
		for (int32 Row = 0; Row < 3; ++Row)
		{
			const float V = Row * 0.5f;
			const float Half = Width * 0.5f * (1.0f - 0.35f * V);
			for (int32 Column = 0; Column < 3; ++Column)
			{
				const float U = Column * 0.5f;
				Vertices.Add(Root + Along * Length * V + Across * (U * 2.0f - 1.0f) * Half - GetActorLocation());
				Normals.Add(-Ahead);
				UVs.Add(FVector2D(U, V));
				Colors.Add(FLinearColor(Seed, Strength, 0.0f, 1.0f));
			}
		}
		for (int32 Row = 0; Row < 2; ++Row)
		{
			for (int32 Column = 0; Column < 2; ++Column)
			{
				const int32 A = Base + Row * 3 + Column;
				Triangles.Append({A, A + 3, A + 4, A, A + 4, A + 1});
			}
		}
	};
	// The column: a ring of tall flames shooting up round the body, then sinking away.
	const float Column = FMath::Clamp(1.0f - (Age - 0.25f) / 0.9f, 0.0f, 1.0f) * FMath::Clamp(Age / 0.12f, 0.0f, 1.0f);
	if (bColumn && Column > 0.01f)
	{
		for (int32 Index = 0; Index < 12; ++Index)
		{
			const float Angle = Index * UE_TWO_PI / 12.0f;
			const FVector Root = Ground + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * Height * 0.28f;
			const float Lick = 0.8f + 0.25f * FMath::Sin(Age * 14.0f + Index * 1.7f);
			Card(Root, FVector::UpVector, Height * 0.5f, Height * 2.1f * Column * Lick, Index / 12.0f, Column);
		}
	}
	// The blobs: a ball of fire streaming flame behind it, shrinking as it burns out.
	for (const FBlob& Blob : Blobs)
	{
		const float Left = Blob.bLanded ? 0.0f : FMath::Clamp(1.0f - Age / Blob.Life, 0.0f, 1.0f);
		if (Left <= 0.01f)
		{
			continue;
		}
		const float Size = Blob.Size * (0.4f + 0.6f * Left);
		const FVector Behind = -Blob.Velocity.GetSafeNormal();
		const float Tail = Size * 1.4f + Blob.Velocity.Size() * 0.12f;
		Card(Blob.Location - Behind * Size * 0.35f, Behind, Size, Tail, Blob.Seed, FMath::Min(1.0f, Left * 1.5f));
	}
	Flames->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, Colors, TArray<FProcMeshTangent>(), false);
}
