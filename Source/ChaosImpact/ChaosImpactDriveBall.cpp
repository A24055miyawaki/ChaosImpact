#include "ChaosImpactDriveBall.h"

#include "ChaosImpactBallTypes.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactLightning.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "ProceduralMeshComponent.h"

namespace
{
	const FLinearColor DriveGold(1.0f, 0.62f, 0.12f);
	const FLinearColor DrivePaleGold(1.0f, 0.86f, 0.5f);
	constexpr float TrailSeconds = 0.45f;
	constexpr float TrailWidth = 16.0f;

	/** A flat ring (a band of quads) in the plane of Rotation's X and Y axes. */
	void AppendTiltedRing(ChaosImpactIceMeshes::FMeshBuffers& Mesh, const FVector& Center, const FQuat& Rotation,
		const float Radius, const float Width, const float FromFraction, const float ToFraction, const int32 Segments)
	{
		if (ToFraction <= FromFraction || Width <= 0.0f)
		{
			return;
		}
		const int32 Steps = FMath::Max(2, FMath::CeilToInt(Segments * (ToFraction - FromFraction)));
		const FVector Normal = Rotation.GetAxisZ();
		const int32 Base = Mesh.Vertices.Num();
		for (int32 Step = 0; Step <= Steps; ++Step)
		{
			const float Angle = UE_TWO_PI * FMath::Lerp(FromFraction, ToFraction, static_cast<float>(Step) / Steps);
			const FVector Out = Rotation.RotateVector(FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f));
			Mesh.Vertices.Add(Center + Out * (Radius - Width * 0.5f));
			Mesh.Vertices.Add(Center + Out * (Radius + Width * 0.5f));
			Mesh.Normals.Add(Normal);
			Mesh.Normals.Add(Normal);
			Mesh.UVs.Add(FVector2D(Step, 0.0));
			Mesh.UVs.Add(FVector2D(Step, 1.0));
			if (Step > 0)
			{
				const int32 A = Base + (Step - 1) * 2;
				// Both faces, so it shows from above and below as it tumbles.
				Mesh.Triangles.Append({A, A + 1, A + 2, A + 2, A + 1, A + 3, A, A + 2, A + 1, A + 2, A + 3, A + 1});
			}
		}
	}
}

void ChaosImpactDrive::BuildLook(AActor* Owner, USceneComponent* Parent, FChaosImpactDriveLook& Out)
{
	if (!Owner || !Parent || Out.IsBuilt() || Owner->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	using namespace ChaosImpactBallTypes;
	// Follows the ball, but neither its spin nor its scale.
	USceneComponent* Root = NewObject<USceneComponent>(Owner);
	Root->SetupAttachment(Parent);
	Root->SetUsingAbsoluteRotation(true);
	Root->SetUsingAbsoluteScale(true);
	Root->RegisterComponent();
	Out.Root = Root;

	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	const auto MakeSphere = [Owner, Root, Sphere](UMaterialInterface* Material, const float Diameter) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(Owner);
		Mesh->SetStaticMesh(Sphere);
		Mesh->SetMaterial(0, Material);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetCastShadow(false);
		Mesh->SetupAttachment(Root);
		Mesh->SetRelativeScale3D(FVector(Diameter / 100.0f));
		Mesh->RegisterComponent();
		return Mesh;
	};
	Out.Core = MakeSphere(MakeEmissive(Owner, FLinearColor(1.0f, 0.96f, 0.85f), 4.0f), 26.0f);
	UMaterialInstanceDynamic* HaloMaterial = MakeAdditive(Owner, DriveGold, 1.2f, 0.7f);
	Out.Halo = MakeSphere(HaloMaterial, 74.0f);
	Out.HaloMaterial = HaloMaterial;
	Out.Rings = ChaosImpactLightning::CreateComponent(Owner, Root, MakeAdditive(Owner, DrivePaleGold, 2.6f));
	Out.Trail = ChaosImpactLightning::CreateComponent(Owner, Root, MakeAdditive(Owner, DriveGold, 2.2f));

	UPointLightComponent* Light = NewObject<UPointLightComponent>(Owner);
	Light->SetLightColor(DriveGold);
	Light->SetAttenuationRadius(380.0f);
	Light->SetCastShadows(false);
	Light->SetIntensity(2600.0f);
	Light->SetupAttachment(Root);
	Light->RegisterComponent();
	Out.Light = Light;

	if (UNiagaraSystem* System = LoadEffect(Effects::WindSparks))
	{
		if (UNiagaraComponent* Sparks = UNiagaraFunctionLibrary::SpawnSystemAttached(System, Root, NAME_None,
			FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset, false))
		{
			Sparks->SetRelativeScale3D(FVector(0.45f));
			SetEffectColor(Sparks, TEXT("Color"), DriveGold);
			SetEffectColor(Sparks, TEXT("Spark Color"), DriveGold);
			Out.Sparks = Sparks;
		}
	}
}

void ChaosImpactDrive::UpdateLook(AActor* Owner, FChaosImpactDriveLook& Look, const float Time, const float DeltaSeconds,
	const bool bFlying, const bool bVisible, const float ControlLeft, const FVector& Velocity)
{
	USceneComponent* Root = Look.Root.Get();
	UWorld* World = Owner ? Owner->GetWorld() : nullptr;
	if (!Root || !World)
	{
		return;
	}
	if (bVisible != Look.bShown)
	{
		Root->SetVisibility(bVisible, true);
		Look.bShown = bVisible;
		if (!bVisible)
		{
			Look.TrailPoints.Reset();
		}
	}
	if (!bVisible)
	{
		return;
	}
	const FVector Center = Root->GetComponentLocation();
	const bool bSteered = ControlLeft >= 0.0f;

	// Snapped round (sent back the way it came): a burst of sparks where it turned.
	const FVector Heading = Velocity.GetSafeNormal2D();
	if (bFlying && !Heading.IsNearlyZero())
	{
		if (!Look.LastHeading.IsNearlyZero() && FVector::DotProduct(Heading, Look.LastHeading) < -0.17f)
		{
			if (UNiagaraSystem* System = ChaosImpactBallTypes::LoadEffect(ChaosImpactBallTypes::Effects::SparkBurst))
			{
				if (UNiagaraComponent* Burst = UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, System, Center,
					FRotator::ZeroRotator, FVector(0.6f)))
				{
					ChaosImpactBallTypes::SetEffectColor(Burst, TEXT("Color"), DriveGold);
					ChaosImpactBallTypes::SetEffectColor(Burst, TEXT("Spark Color"), DriveGold);
				}
			}
		}
		Look.LastHeading = Heading;
	}
	else
	{
		Look.LastHeading = FVector::ZeroVector;
	}

	// The glow breathes; steered, it burns brighter.
	const float Pulse = 0.5f + 0.5f * FMath::Sin(Time * 9.0f);
	if (UStaticMeshComponent* Halo = Look.Halo.Get())
	{
		Halo->SetRelativeScale3D(FVector((0.7f + 0.06f * Pulse + (bSteered ? 0.08f : 0.0f))));
	}
	if (UMaterialInstanceDynamic* HaloMaterial = Look.HaloMaterial.Get())
	{
		HaloMaterial->SetScalarParameterValue(TEXT("Intensity"), (bSteered ? 1.6f : 1.0f) + 0.4f * Pulse);
	}
	if (UPointLightComponent* Light = Look.Light.Get())
	{
		Light->SetIntensity((bFlying ? 3400.0f : 2200.0f) + 600.0f * Pulse);
	}

	// Two energy rings tumbling round it; while it is steered, a third, flat one runs down with the time left.
	if (UProceduralMeshComponent* Rings = Look.Rings.Get())
	{
		ChaosImpactIceMeshes::FMeshBuffers Mesh;
		const FQuat First = FQuat(FVector::UpVector, Time * 3.1f) * FQuat(FVector::ForwardVector, FMath::DegreesToRadians(58.0f));
		const FQuat Second = FQuat(FVector::UpVector, -Time * 2.4f + 1.3f) * FQuat(FVector::RightVector, FMath::DegreesToRadians(62.0f));
		AppendTiltedRing(Mesh, FVector::ZeroVector, First, 38.0f, 3.0f, 0.0f, 1.0f, 40);
		AppendTiltedRing(Mesh, FVector::ZeroVector, Second, 44.0f, 2.5f, 0.0f, 1.0f, 40);
		if (bSteered)
		{
			// Starting from the top of the screen's view and going round as time runs out.
			AppendTiltedRing(Mesh, FVector(0.0f, 0.0f, -20.0f), FQuat::Identity, 58.0f, 5.0f,
				0.0f, FMath::Clamp(ControlLeft, 0.0f, 1.0f), 64);
		}
		ChaosImpactLightning::SetMesh(Rings, Mesh);
	}

	// A golden trail that keeps every turn it was steered through.
	if (UProceduralMeshComponent* Trail = Look.Trail.Get())
	{
		if (bFlying)
		{
			if (!Look.TrailPoints.IsEmpty() && FVector::DistSquared(Center, Look.TrailPoints.Last().Position) > FMath::Square(600.0f))
			{
				Look.TrailPoints.Reset();
			}
			if (Look.TrailPoints.IsEmpty() || Time - Look.TrailPoints.Last().Time > 0.016f)
			{
				Look.TrailPoints.Add({Center, Time});
			}
		}
		Look.TrailPoints.RemoveAll([Time](const FChaosImpactDriveLook::FTrailPoint& Point) { return Time - Point.Time > TrailSeconds; });
		ChaosImpactIceMeshes::FMeshBuffers Ribbon;
		for (int32 Index = 0; Index + 1 < Look.TrailPoints.Num(); ++Index)
		{
			const FVector Start = Look.TrailPoints[Index].Position - Center;
			const FVector End = Look.TrailPoints[Index + 1].Position - Center;
			const float Fade = FMath::Clamp(1.0f - (Time - Look.TrailPoints[Index].Time) / TrailSeconds, 0.0f, 1.0f);
			const float Width = TrailWidth * Fade * Fade;
			const FVector Side = FVector::CrossProduct(End - Start, FVector::UpVector).GetSafeNormal() * Width * 0.5f;
			if (Side.IsNearlyZero())
			{
				continue;
			}
			// Flat on the ground plane (the camera looks down), with a thinner upright copy for low views.
			for (const FVector& Across : {Side, FVector::UpVector * Width * 0.25f})
			{
				const int32 Base = Ribbon.Vertices.Num();
				for (const FVector& Corner : {Start - Across, Start + Across, End - Across, End + Across})
				{
					Ribbon.Vertices.Add(Corner);
					Ribbon.Normals.Add(FVector::UpVector);
				}
				Ribbon.UVs.Append({FVector2D(0, 0), FVector2D(1, 0), FVector2D(0, 1), FVector2D(1, 1)});
				Ribbon.Triangles.Append({Base, Base + 1, Base + 2, Base + 2, Base + 1, Base + 3,
					Base, Base + 2, Base + 1, Base + 2, Base + 3, Base + 1});
			}
		}
		ChaosImpactLightning::SetMesh(Trail, Ribbon);
	}
}

AChaosImpactDriveBurst::AChaosImpactDriveBurst()
{
	PrimaryActorTick.bCanEverTick = true;
	SetReplicates(false);
	BurstRoot = CreateDefaultSubobject<USceneComponent>(TEXT("BurstRoot"));
	SetRootComponent(BurstRoot);
	BurstLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("BurstLight"));
	BurstLight->SetupAttachment(BurstRoot);
	BurstLight->SetLightColor(DriveGold);
	BurstLight->SetCastShadows(false);
	BurstLight->SetAttenuationRadius(800.0f);
}

void AChaosImpactDriveBurst::Play(UWorld* World, const FVector& Location, const bool bInHit)
{
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const FTransform Where(FRotator::ZeroRotator, Location);
	if (AChaosImpactDriveBurst* Burst = World->SpawnActorDeferred<AChaosImpactDriveBurst>(StaticClass(), Where, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
	{
		Burst->bHit = bInHit;
		Burst->FinishSpawning(Where);
	}
}

void AChaosImpactDriveBurst::BeginPlay()
{
	Super::BeginPlay();
	using namespace ChaosImpactBallTypes;
	SetLifeSpan(1.2f);
	RingMaterial = MakeAdditive(this, DrivePaleGold, 3.0f);
	ShockRing = ChaosImpactLightning::CreateComponent(this, BurstRoot, RingMaterial);
	BurstLight->SetIntensity(bHit ? 16000.0f : 5000.0f);
	if (bHit)
	{
		if (UNiagaraSystem* Explosion = LoadEffect(Effects::MediumExplosion))
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, Explosion, GetActorLocation(), FRotator::ZeroRotator, FVector(0.75f));
		}
		// A jolt for whoever is close.
		for (TActorIterator<AChaosImpactCharacter> It(GetWorld()); It; ++It)
		{
			const float Distance = static_cast<float>(FVector::Dist(It->GetActorLocation(), GetActorLocation()));
			if (It->IsLocallyControlled() && Distance < 900.0f)
			{
				It->AddCameraShake(0.22f * (1.0f - Distance / 900.0f), 0.25f);
			}
		}
	}
	if (UNiagaraSystem* Sparks = LoadEffect(Effects::SparkBurst))
	{
		if (UNiagaraComponent* Effect = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, Sparks, GetActorLocation(),
			FRotator::ZeroRotator, FVector(bHit ? 1.2f : 0.7f)))
		{
			SetEffectColor(Effect, TEXT("Color"), DriveGold);
			SetEffectColor(Effect, TEXT("Spark Color"), DriveGold);
		}
	}
}

void AChaosImpactDriveBurst::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	const float Span = bHit ? 0.55f : 0.35f;
	const float Fade = FMath::Clamp(1.0f - Age / Span, 0.0f, 1.0f);
	if (ShockRing)
	{
		ChaosImpactIceMeshes::FMeshBuffers Rings;
		const float Reach = bHit ? 420.0f : 160.0f;
		const float Grow = 1.0f - FMath::Pow(1.0f - FMath::Clamp(Age / Span, 0.0f, 1.0f), 3.0f);
		ChaosImpactLightning::AppendRing(Rings, FVector::UpVector * -20.0f, 20.0f + Reach * Grow, 10.0f * Fade, 64);
		if (bHit)
		{
			ChaosImpactLightning::AppendRing(Rings, FVector::UpVector * -18.0f, 10.0f + Reach * 0.6f * Grow, 6.0f * Fade, 64);
		}
		ChaosImpactLightning::SetMesh(ShockRing, Rings);
		RingMaterial->SetScalarParameterValue(TEXT("Intensity"), 3.0f * Fade);
	}
	BurstLight->SetIntensity((bHit ? 16000.0f : 5000.0f) * FMath::Exp(-Age * 9.0f));
}
