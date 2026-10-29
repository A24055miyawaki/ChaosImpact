#include "ChaosImpactSimaeBossFx.h"

#include "ChaosImpactBall.h"
#include "ChaosImpactBallTypes.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactEase.h"
#include "ChaosImpactFxKit.h"
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

	/** The shock's colour: icy white, turning to the full power's red. */
	FLinearColor SimaeShockColor(const float Heat)
	{
		return FMath::Lerp(FLinearColor(0.55f, 0.82f, 1.0f), FLinearColor(1.0f, 0.1f, 0.2f), FMath::Clamp(Heat, 0.0f, 1.0f));
	}
}

// ---------------------------------------------------------------------------------------------------- Shared

namespace
{
	/** Puffs of powder snow: pale, lit from above, thinning as they drift up (never the dark dust of a dirt floor). */
	void SimaePowder(UWorld* World, const FVector& Location, const float Size, const int32 Puffs, const float Spread, const float Life)
	{
		AChaosImpactFxKit* Fx = AChaosImpactFxKit::Spawn(World, Location);
		if (!Fx)
		{
			return;
		}
		Fx->Stream.Initialize(FMath::Rand());
		for (int32 Index = 0; Index < Puffs; ++Index)
		{
			const float Angle = (Index + Fx->Stream.FRandRange(-0.4f, 0.4f)) * UE_TWO_PI / Puffs;
			const FVector Out(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f);
			FChaosImpactFxPiece Puff;
			Puff.Look = EChaosImpactFxLook::Smoke;
			Puff.Location = Out * Size * 0.15f + FVector(0, 0, Size * 0.25f);
			Puff.Velocity = Out * Spread * Fx->Stream.FRandRange(0.6f, 1.0f) + FVector(0, 0, Size * 0.3f);
			Puff.Drag = 2.5f;
			Puff.Delay = Fx->Stream.FRandRange(0.0f, 0.06f);
			Puff.Life = Life * Fx->Stream.FRandRange(0.8f, 1.15f);
			Puff.SizeFrom = Size * 0.45f;
			Puff.SizeTo = Size * 1.05f;
			Puff.Color = FLinearColor(0.74f, 0.8f, 0.9f);
			Puff.CoreColor = FLinearColor(0.98f, 0.99f, 1.0f);
			Puff.Intensity = 1.0f;
			Puff.Opacity = 0.55f;
			Puff.Attack = 0.1f;
			Puff.ErodeTo = 0.95f;
			Puff.NoiseSpeed = 0.2f;
			Puff.Spin = Fx->Stream.FRandRange(-50.0f, 50.0f);
			Fx->Add(Puff);
		}
	}
}

void ChaosImpactSimaeFx::PlaySnowDust(UWorld* World, const FVector& Location, const float Size, const float Seconds,
	const float Rate)
{
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	// Light: a few small puffs only (more soon hides the fight).
	const float Puff = FMath::Min(Size * 0.6f, 220.0f);
	SimaePowder(World, Location, Puff, FMath::Clamp(FMath::RoundToInt(Rate * Seconds * 3.0f), 2, 4), Puff * 0.8f, 0.8f);
}

void ChaosImpactSimaeFx::PlaySnowBurst(UWorld* World, const FVector& Location, const float Scale)
{
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	SimaePowder(World, Location, FMath::Clamp(Scale, 0.5f, 1.6f) * 200.0f, 7, FMath::Clamp(Scale, 0.5f, 1.6f) * 500.0f, 1.0f);
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
	Plane = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Plane"));
	Plane->SetupAttachment(GetRootComponent());
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
	Plane->SetStaticMesh(PlaneMesh.Object);
	Plane->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Plane->SetGenerateOverlapEvents(false);
	Plane->SetCastShadow(false);
	Plane->bUseAsOccluder = false;
	Plane->bReceivesDecals = false;
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
		if (UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/ChaosImpact/FX/Kit/M_CIFX_Warn.M_CIFX_Warn")))
		{
			Warning->Look = UMaterialInstanceDynamic::Create(Parent, Warning);
			Warning->Look->SetVectorParameterValue(TEXT("Color"), Color);
			Warning->Plane->SetMaterial(0, Warning->Look);
		}
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
	const FVector Ground = ChaosImpactSimaeFx::FindGround(GetWorld(), Start, this);
	Length = FMath::Max(TrimToWalls(Ground, NewLength), 50.0f);
	SetActorLocationAndRotation(Ground + FVector(0, 0, 4), Direction.Rotation());
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

float AChaosImpactSimaeWarning::TrimToWalls(const FVector& Start, const float WantedLength) const
{
	// Waist high over the floor, the scenery only (as the floor is found): the first thing standing in the way that is
	// not a slope underfoot ends the lane, a little short of it so its soft end shows in front of the wall.
	const UWorld* World = GetWorld();
	if (!World)
	{
		return WantedLength;
	}
	const FVector From = Start + FVector(0, 0, 90);
	TArray<FHitResult> Hits;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SimaeLane), false, this);
	World->LineTraceMultiByObjectType(Hits, From, From + Direction * WantedLength, FCollisionObjectQueryParams(ECC_WorldStatic), Params);
	for (const FHitResult& Hit : Hits)
	{
		if (Hit.ImpactNormal.Z < 0.6f)
		{
			return FMath::Max(Hit.Distance - 20.0f, 50.0f);
		}
	}
	return WantedLength;
}

void AChaosImpactSimaeWarning::Rebuild()
{
	const float Progress = FMath::Clamp(Age / Seconds, 0.0f, 1.0f);
	const float Gone = FinishedAge >= 0.0f ? FMath::Clamp((Age - FinishedAge) / 0.22f, 0.0f, 1.0f) : 0.0f;
	// Breathing faster and faster as it is about to go off; a flash as it goes, fading straight away.
	const float Blink = 0.5f + 0.5f * FMath::Sin(Age * FMath::Lerp(6.0f, 22.0f, Progress * Progress));
	// Appears with a quick ease-out swell instead of popping in.
	const float Born = ChaosImpactEase::Out(FMath::Clamp(Age / 0.18f, 0.0f, 1.0f));
	if (Plane)
	{
		if (bLane)
		{
			Plane->SetRelativeLocation(FVector(Length * 0.5f, 0, 0));
			Plane->SetRelativeScale3D(FVector(Length / 100.0f, HalfWidth * 2.0f * FMath::Lerp(0.6f, 1.0f, Born) / 100.0f, 1.0f));
		}
		else
		{
			const float Size = Radius * 2.0f * FMath::Lerp(0.75f, 1.0f, Born) / 100.0f;
			Plane->SetRelativeLocation(FVector::ZeroVector);
			Plane->SetRelativeScale3D(FVector(Size, Size, 1.0f));
		}
	}
	if (UMaterialInstanceDynamic* M = Look.Get())
	{
		M->SetScalarParameterValue(TEXT("Lane"), bLane ? 1.0f : 0.0f);
		M->SetScalarParameterValue(TEXT("Aspect"), Length / FMath::Max(HalfWidth * 2.0f, 1.0f));
		M->SetScalarParameterValue(TEXT("Progress"), FinishedAge >= 0.0f ? 1.0f : Progress);
		M->SetScalarParameterValue(TEXT("Flash"), FinishedAge >= 0.0f ? 1.0f - Gone : 0.12f * Blink * Progress);
		M->SetScalarParameterValue(TEXT("Opacity"), (FinishedAge >= 0.0f ? 1.0f - ChaosImpactEase::InOut(Gone) : 1.0f) * Born);
		M->SetScalarParameterValue(TEXT("Intensity"), 1.0f + 0.25f * Blink * Progress);
	}
}

// ---------------------------------------------------------------------------------------------------- Blast

AChaosImpactSimaeBlast::AChaosImpactSimaeBlast()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
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
	if (AChaosImpactFxKit* Fx = AChaosImpactFxKit::Spawn(World, Center + FVector(0, 0, 3)))
	{
		Fx->Stream.Initialize(Blast->Seed);
		// Deep enough to read on white snow (see-through colour, not added light), white-hot at the front of the wave.
		const FLinearColor Deep = Heat > 0.5f ? FLinearColor(0.75f, 0.02f, 0.08f) : FLinearColor(0.16f, 0.4f, 0.95f);
		const FLinearColor Pale = FMath::Lerp(FLinearColor(0.92f, 0.97f, 1.0f), FLinearColor(1.0f, 0.78f, 0.62f), FMath::Clamp(Heat, 0.0f, 1.0f));
		const float Reach = Radius * 2.3f;
		FChaosImpactFxPiece Flash;
		Flash.Location = FVector(0, 0, 80);
		Flash.Life = 0.2f;
		Flash.SizeFrom = FMath::Clamp(Radius * 0.45f, 140.0f, 420.0f);
		Flash.SizeTo = Flash.SizeFrom * 2.0f;
		Flash.Color = Shock;
		Flash.CoreColor = Pale;
		Flash.Intensity = 5.0f;
		Flash.Attack = 0.05f;
		Flash.NoiseAmount = 0.2f;
		Fx->Add(Flash);
		FChaosImpactFxPiece Wave;
		Wave.Look = EChaosImpactFxLook::SolidRing;
		Wave.Location = FVector(0, 0, 4);
		Wave.Life = 0.55f;
		Wave.SizeFrom = Wave.SizeTo = Reach;
		Wave.RingFrom = 0.06f;
		Wave.RingTo = 0.93f;
		Wave.RingWidth = 0.06f;
		Wave.Color = Deep;
		Wave.CoreColor = Pale;
		Wave.Intensity = 1.0f;
		Wave.Opacity = 1.0f;
		Wave.Attack = 0.04f;
		Wave.ErodeTo = 0.65f;
		Wave.NoiseScale = 2.5f;
		Wave.NoiseAmount = 0.45f;
		Fx->Add(Wave);
		FChaosImpactFxPiece Edge = Wave;
		Edge.Look = EChaosImpactFxLook::Ring;
		Edge.Location.Z = 6.0f;
		Edge.Life = 0.45f;
		Edge.RingWidth = 0.03f;
		Edge.Color = Shock;
		Edge.Intensity = 2.5f;
		Fx->Add(Edge);
		FChaosImpactFxPiece After = Wave;
		After.Delay = 0.1f;
		After.Life = 0.7f;
		After.RingTo = 0.8f;
		After.RingWidth = 0.03f;
		After.Opacity = 0.55f;
		Fx->Add(After);
		// Powder thrown out low along the ground, and glints of ice (embers at full power) flung up.
		const float Puff = FMath::Clamp(Radius * 0.3f, 90.0f, 300.0f);
		for (int32 Index = 0; Index < 10; ++Index)
		{
			const float Angle = (Index + Stream.FRandRange(-0.35f, 0.35f)) * UE_TWO_PI / 10.0f;
			const FVector Out(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f);
			FChaosImpactFxPiece Powder;
			Powder.Look = EChaosImpactFxLook::Smoke;
			Powder.Location = Out * Radius * 0.15f + FVector(0, 0, Puff * 0.3f);
			Powder.Velocity = Out * Radius * Stream.FRandRange(1.6f, 2.4f);
			Powder.Drag = 3.0f;
			Powder.Gravity = -60.0f;
			Powder.Life = Stream.FRandRange(0.8f, 1.15f);
			Powder.SizeFrom = Puff * 0.6f;
			Powder.SizeTo = Puff * 1.5f;
			Powder.Color = FLinearColor(0.6f, 0.68f, 0.8f);
			Powder.CoreColor = FLinearColor(0.97f, 0.98f, 1.0f);
			Powder.Intensity = 1.0f;
			Powder.Opacity = 0.6f;
			Powder.Attack = 0.08f;
			Powder.ErodeTo = 0.9f;
			Powder.Spin = Stream.FRandRange(-40.0f, 40.0f);
			Fx->Add(Powder);
		}
		for (int32 Index = 0; Index < 12; ++Index)
		{
			FChaosImpactFxPiece Glint;
			Glint.Look = EChaosImpactFxLook::Streak;
			Glint.Location = FVector(0, 0, 40);
			Glint.Velocity = FRotator(Stream.FRandRange(20.0f, 60.0f), Stream.FRandRange(0.0f, 360.0f), 0.0f).Vector()
				* Stream.FRandRange(700.0f, 1400.0f) * FMath::Clamp(Radius / 500.0f, 0.7f, 1.4f);
			Glint.Gravity = 2400.0f;
			Glint.Drag = 1.0f;
			Glint.Life = Stream.FRandRange(0.4f, 0.7f);
			Glint.SizeFrom = 80.0f;
			Glint.SizeTo = 30.0f;
			Glint.Width = 6.0f;
			Glint.Color = Shock;
			Glint.CoreColor = Pale;
			Glint.Intensity = 5.0f;
			Glint.Attack = 0.02f;
			Glint.ErodeTo = 0.0f;
			Glint.Flicker = 0.4f;
			Fx->Add(Glint);
		}
	}
	Blast->Flash->SetLightColor(Shock);
	Blast->Flash->SetAttenuationRadius(Radius * 2.5f + 400.0f);
	if (bCracks)
	{
		// Cracks in the packed snow: dark blue-grey, or glowing red when the full power struck.
		Blast->HotMaterial = Heat > 0.5f
			? ChaosImpactBallTypes::MakeEmissive(Blast, FLinearColor(1.0f, 0.12f, 0.08f), 2.5f)
			: ChaosImpactBallTypes::MakeEmissive(Blast, FLinearColor(0.45f, 0.56f, 0.72f), 0.8f);
		Blast->Cracks->SetMaterial(0, Blast->HotMaterial);
		FMeshBuffers Lines;
		const int32 Count = 9 + Stream.RandRange(0, 4);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const float Angle = (Index + Stream.FRandRange(-0.3f, 0.3f)) * UE_TWO_PI / Count;
			const FVector Out(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f);
			ChaosImpactIceMeshes::AppendCrack(Lines, FTransform::Identity, Out * Radius * 0.08f, Out,
				Radius * Stream.FRandRange(0.3f, 0.6f), FMath::Clamp(Radius * 0.016f, 5.0f, 12.0f), Stream);
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
	Glow = CreateDefaultSubobject<UPointLightComponent>(TEXT("Glow"));
	Glow->SetupAttachment(GetRootComponent());
	Glow->SetCastShadows(false);
	Glow->SetAttenuationRadius(1500.0f);
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
	Shot->WindColor = FMath::Lerp(FLinearColor(0.45f, 0.75f, 1.0f), FLinearColor(1.0f, 0.2f, 0.32f), Heat);
	Shot->Glow->SetLightColor(Shot->WindColor);
	Shot->Glow->SetIntensity(20000.0f);
	const float Flight = Range / FMath::Max(Speed, 1.0f);
	const FLinearColor Hot = FMath::Lerp(FLinearColor(0.9f, 0.97f, 1.0f), FLinearColor(1.0f, 0.8f, 0.7f), Heat);
	if (AChaosImpactFxKit* Fx = AChaosImpactFxKit::Spawn(World, Start))
	{
		// Riding with the ball: a white-hot glow round it inside a wider haze of pressed air, and air streaming off it.
		Fx->AttachToActor(Shot, FAttachmentTransformRules::KeepWorldTransform);
		Shot->Around = Fx;
		FChaosImpactFxPiece Heart;
		Heart.Life = Flight + 0.3f;
		Heart.Attack = 0.1f / Heart.Life;
		Heart.Hold = 1.0f - 0.25f / Heart.Life;
		Heart.SizeFrom = Heart.SizeTo = HitRadius * 1.1f;
		Heart.Color = Shot->WindColor;
		Heart.CoreColor = Hot;
		Heart.Intensity = 3.0f;
		Heart.Flicker = 0.15f;
		Heart.NoiseAmount = 0.3f;
		Heart.Spin = 300.0f;
		Fx->Add(Heart);
		FChaosImpactFxPiece Haze = Heart;
		Haze.SizeFrom = Haze.SizeTo = HitRadius * 1.7f;
		Haze.Intensity = 0.45f;
		Haze.NoiseAmount = 0.7f;
		Haze.NoiseSpeed = 1.2f;
		Haze.Spin = -200.0f;
		Fx->Add(Haze);
		FChaosImpactFxPiece Air;
		Air.Look = EChaosImpactFxLook::Streak;
		Air.Location = FVector(0, 0, -20);
		Air.Velocity = -Flat * Speed * 0.55f;
		Air.Life = 0.22f;
		Air.SizeFrom = HitRadius * 1.6f;
		Air.SizeTo = HitRadius * 0.8f;
		Air.Width = 7.0f;
		Air.Color = Shot->WindColor;
		Air.CoreColor = Hot;
		Air.Intensity = 3.0f;
		Air.Attack = 0.15f;
		Air.ErodeTo = 0.0f;
		Fx->AddEmitter(Air, 45.0f, Flight, HitRadius * 0.85f, FVector(0, 0, HitRadius * 0.5f), 0.3f);
	}
	if (AChaosImpactFxKit* Fx = AChaosImpactFxKit::Spawn(World, FVector(Start.X, Start.Y, Shot->GroundZ)))
	{
		Fx->Stream.Initialize(FMath::Rand());
		Shot->Wake = Fx;
	}
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
	// Shock rings bursting off it over the snow every few metres, and the ploughed track left behind (soft blue-grey
	// hollows that run together into one groove, fading after it has passed).
	if (AChaosImpactFxKit* Fx = Wake.Get())
	{
		const FVector Base = Fx->GetActorLocation();
		while (Travelled >= NextBoomAt)
		{
			const FVector Spot = Origin + Direction * NextBoomAt;
			FChaosImpactFxPiece Boom;
			Boom.Look = EChaosImpactFxLook::SolidRing;
			Boom.Location = FVector(Spot.X - Base.X, Spot.Y - Base.Y, GroundZ - Base.Z + 5.0f);
			Boom.Life = 0.45f;
			Boom.SizeFrom = Boom.SizeTo = HitRadius * 3.0f;
			Boom.RingFrom = 0.25f;
			Boom.RingTo = 0.9f;
			Boom.RingWidth = 0.05f;
			Boom.Color = WindColor * 0.7f;
			Boom.CoreColor = FLinearColor(0.95f, 0.98f, 1.0f);
			Boom.Intensity = 1.0f;
			Boom.Opacity = 0.7f;
			Boom.Attack = 0.05f;
			Boom.ErodeTo = 0.7f;
			Fx->Add(Boom);
			FChaosImpactFxPiece Pop = Boom;
			Pop.Look = EChaosImpactFxLook::Ring;
			Pop.Location.Z += 2.0f;
			Pop.Life = 0.3f;
			Pop.RingWidth = 0.03f;
			Pop.Color = WindColor;
			Pop.Intensity = 2.2f;
			Fx->Add(Pop);
			NextBoomAt += 360.0f;
		}
		while (Travelled >= NextTrackAt)
		{
			const FVector Spot = Origin + Direction * NextTrackAt;
			FChaosImpactFxPiece Groove;
			Groove.Look = EChaosImpactFxLook::GroundFlame;
			Groove.Location = FVector(Spot.X - Base.X, Spot.Y - Base.Y, GroundZ - Base.Z + 3.0f);
			Groove.Life = 2.2f;
			Groove.Attack = 0.03f;
			Groove.Hold = 0.4f;
			Groove.SizeFrom = HitRadius * 1.1f;
			Groove.SizeTo = HitRadius * 1.3f;
			Groove.Color = FLinearColor(0.32f, 0.42f, 0.62f);
			Groove.CoreColor = FLinearColor(0.42f, 0.54f, 0.75f);
			Groove.Intensity = 1.0f;
			Groove.Opacity = 0.32f;
			Groove.NoiseScale = 1.8f;
			Groove.NoiseSpeed = 0.05f;
			Groove.NoiseAmount = 0.5f;
			Groove.ErodeTo = 0.8f;
			Groove.Softness = 0.8f;
			Fx->Add(Groove);
			NextTrackAt += HitRadius * 0.6f;
		}
	}
	// Those it rushes past feel it.
	if (Age >= NextShakeAt)
	{
		NextShakeAt = Age + 0.12f;
		if (AChaosImpactSimaeBoss* Thrower = Boss.Get())
		{
			Thrower->ShakeNearby(Next, HitRadius + 700.0f, 0.3f, 0.2f);
		}
	}
	Glow->SetIntensity(20000.0f * (0.85f + 0.15f * FMath::Sin(Age * 40.0f)));
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
	if (AChaosImpactFxKit* Fx = Around.Get())
	{
		// What rides with it stays where it stopped and fades.
		Fx->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		Fx->StopEmitters();
		Fx->SetLifeSpan(0.4f);
	}
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
				// No smoke: it must not hide the fight (and dark smoke smudges the snow).
				ChaosImpactBallTypes::SetEffectFloat(Flame, TEXT("Smoke Spawn Scale"), 0.0f);
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
					ChaosImpactBallTypes::SetEffectFloat(Flame, TEXT("Smoke Spawn Scale"), 0.0f);
					ChaosImpactBallTypes::SetEffectFloat(Flame, TEXT("Base Light Intentsity"), 0.0f);
					FTimerHandle Out;
					World->GetTimerManager().SetTimer(Out, FTimerDelegate::CreateWeakLambda(Flame, [Flame]() { Flame->Deactivate(); }), 0.9f, false);
				}
			}
		}
	}
	if (AChaosImpactFxKit* Fx = AChaosImpactFxKit::Spawn(World, Ground))
	{
		// The power's punch: a red-hot flash at the chest, a ring of fire racing out over the snow, embers flung up.
		Fx->Stream.Initialize(Stream.RandHelper(1 << 20));
		const float Big = bColumn ? 1.0f : 0.45f;
		FChaosImpactFxPiece Burn;
		Burn.Look = EChaosImpactFxLook::Flame;
		Burn.Location = FVector(0, 0, Height * 0.5f);
		Burn.Life = 0.3f;
		Burn.SizeFrom = Height * 0.8f * Big;
		Burn.SizeTo = Height * 1.7f * Big;
		Burn.Color = FLinearColor(1.0f, 0.12f, 0.1f);
		Burn.CoreColor = FLinearColor(1.0f, 0.85f, 0.6f);
		Burn.Intensity = 1.6f;
		Burn.Opacity = 0.85f;
		Burn.Attack = 0.06f;
		Burn.ErodeTo = 1.0f;
		Burn.NoiseAmount = 0.6f;
		Fx->Add(Burn);
		FChaosImpactFxPiece Wave;
		Wave.Look = EChaosImpactFxLook::SolidRing;
		Wave.Location = FVector(0, 0, 5);
		Wave.Life = 0.6f;
		Wave.SizeFrom = Wave.SizeTo = Height * 5.0f * Big;
		Wave.RingFrom = 0.08f;
		Wave.RingTo = 0.92f;
		Wave.RingWidth = 0.05f;
		Wave.Color = FLinearColor(0.85f, 0.04f, 0.06f);
		Wave.CoreColor = FLinearColor(1.0f, 0.6f, 0.3f);
		Wave.Intensity = 1.0f;
		Wave.Opacity = 0.85f;
		Wave.Attack = 0.04f;
		Wave.ErodeTo = 0.7f;
		Wave.NoiseScale = 2.5f;
		Fx->Add(Wave);
		for (int32 Index = 0; Index < (bColumn ? 18 : 6); ++Index)
		{
			FChaosImpactFxPiece Ember;
			Ember.Look = EChaosImpactFxLook::Streak;
			Ember.Location = FVector(0, 0, Height * 0.5f);
			Ember.Velocity = FRotator(Stream.FRandRange(20.0f, 75.0f), Stream.FRandRange(0.0f, 360.0f), 0.0f).Vector() * Stream.FRandRange(900.0f, 1900.0f) * Big;
			Ember.Gravity = 1800.0f;
			Ember.Drag = 1.0f;
			Ember.Life = Stream.FRandRange(0.5f, 0.9f);
			Ember.SizeFrom = 110.0f;
			Ember.SizeTo = 40.0f;
			Ember.Width = 8.0f;
			Ember.Color = FLinearColor(1.0f, 0.18f, 0.05f);
			Ember.CoreColor = FLinearColor(1.0f, 0.85f, 0.5f);
			Ember.Intensity = 6.0f;
			Ember.Attack = 0.02f;
			Ember.ErodeTo = 0.0f;
			Ember.Flicker = 0.5f;
			Fx->Add(Ember);
		}
	}
	Burst->SetLifeSpan(2.6f);
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
}
