#include "ChaosImpactFxKit.h"

#include "ChaosImpactEase.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{
	/** The effect planes are /Engine/BasicShapes/Plane: 100 cm across, facing +Z. */
	constexpr float PlaneSize = 100.0f;

	const TCHAR* MaterialPath(const EChaosImpactFxLook Look)
	{
		switch (Look)
		{
		case EChaosImpactFxLook::Ring:
		case EChaosImpactFxLook::AirRing: return TEXT("/Game/ChaosImpact/FX/Kit/M_CIFX_Ring.M_CIFX_Ring");
		case EChaosImpactFxLook::Streak: return TEXT("/Game/ChaosImpact/FX/Kit/M_CIFX_Streak.M_CIFX_Streak");
		case EChaosImpactFxLook::Smoke: return TEXT("/Game/ChaosImpact/FX/Kit/M_CIFX_Smoke.M_CIFX_Smoke");
		case EChaosImpactFxLook::Flame:
		case EChaosImpactFxLook::GroundFlame: return TEXT("/Game/ChaosImpact/FX/Kit/M_CIFX_Flame.M_CIFX_Flame");
		case EChaosImpactFxLook::Spiral: return TEXT("/Game/ChaosImpact/FX/Kit/M_CIFX_Spiral.M_CIFX_Spiral");
		case EChaosImpactFxLook::SolidRing: return TEXT("/Game/ChaosImpact/FX/Kit/M_CIFX_RingSolid.M_CIFX_RingSolid");
		default: return TEXT("/Game/ChaosImpact/FX/Kit/M_CIFX_Glow.M_CIFX_Glow");
		}
	}

	UMaterialInterface* LoadLook(const EChaosImpactFxLook Look)
	{
		// Each path is resolved once (they are small and stay loaded).
		static TMap<FString, TWeakObjectPtr<UMaterialInterface>> Cache;
		const FString Path = MaterialPath(Look);
		if (const TWeakObjectPtr<UMaterialInterface>* Found = Cache.Find(Path); Found && Found->IsValid())
		{
			return Found->Get();
		}
		UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *Path);
		Cache.Add(Path, Material);
		return Material;
	}

	UStaticMesh* LoadPlane()
	{
		static TWeakObjectPtr<UStaticMesh> Plane;
		if (!Plane.IsValid())
		{
			Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
		}
		return Plane.Get();
	}

	bool IsFlat(const EChaosImpactFxLook Look)
	{
		return Look == EChaosImpactFxLook::GroundGlow || Look == EChaosImpactFxLook::Ring || Look == EChaosImpactFxLook::GroundFlame
			|| Look == EChaosImpactFxLook::Spiral || Look == EChaosImpactFxLook::SolidRing;
	}
}

AChaosImpactFxKit::AChaosImpactFxKit()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = false;
	bReplicates = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Stream.GenerateNewSeed();
}

AChaosImpactFxKit* AChaosImpactFxKit::Spawn(UObject* WorldContext, const FVector& Location)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return nullptr;
	}
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Parameters.ObjectFlags |= RF_Transient;
	return World->SpawnActor<AChaosImpactFxKit>(AChaosImpactFxKit::StaticClass(), Location, FRotator::ZeroRotator, Parameters);
}

void AChaosImpactFxKit::Add(const FChaosImpactFxPiece& Piece)
{
	FLive Live;
	Live.Piece = Piece;
	Live.Piece.Location *= Scale;
	Live.Piece.Velocity *= Scale;
	Live.Piece.Gravity *= Scale;
	Live.Piece.SizeFrom *= Scale;
	Live.Piece.SizeTo *= Scale;
	if (Live.Piece.Look == EChaosImpactFxLook::Streak)
	{
		Live.Piece.Width *= Scale;
	}
	Live.Seed = Stream.FRandRange(0.0f, 100.0f);
	Pieces.Add(Live);
}

void AChaosImpactFxKit::AddEmitter(const FChaosImpactFxPiece& Template, const float Rate, const float Seconds, const float Radius,
	const FVector& VelocityJitter, const float SizeJitter)
{
	FEmitter Emitter;
	Emitter.Template = Template;
	Emitter.Rate = Rate;
	Emitter.Left = Seconds;
	Emitter.Radius = Radius;
	Emitter.VelocityJitter = VelocityJitter;
	Emitter.SizeJitter = SizeJitter;
	Emitter.Owed = 1.0f;
	Emitters.Add(Emitter);
}

void AChaosImpactFxKit::StopEmitters()
{
	Emitters.Reset();
}

int32 AChaosImpactFxKit::TakeSlot(const EChaosImpactFxLook Look)
{
	// A free plane with the same material, else a fresh one.
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		if (!Slots[Index].bBusy && Slots[Index].Look == Look && Slots[Index].Mesh)
		{
			Slots[Index].bBusy = true;
			return Index;
		}
	}
	UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(this);
	Mesh->SetStaticMesh(LoadPlane());
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
	Mesh->SetGenerateOverlapEvents(false);
	Mesh->SetReceivesDecals(false);
	Mesh->bAffectDistanceFieldLighting = false;
	Mesh->SetUsingAbsoluteLocation(true);
	Mesh->SetUsingAbsoluteRotation(true);
	Mesh->SetUsingAbsoluteScale(true);
	Mesh->SetupAttachment(GetRootComponent());
	Mesh->RegisterComponent();
	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(LoadLook(Look), Mesh);
	Mesh->SetMaterial(0, Material);
	KeepAlive.Add(Mesh);
	KeepAlive.Add(Material);
	FSlot Slot;
	Slot.Mesh = Mesh;
	Slot.Material = Material;
	Slot.Look = Look;
	Slot.bBusy = true;
	return Slots.Add(Slot);
}

void AChaosImpactFxKit::Draw(FLive& Live, const FVector& Camera)
{
	const FChaosImpactFxPiece& P = Live.Piece;
	const float Local = Live.Age - P.Delay;
	if (Local < 0.0f)
	{
		return;
	}
	if (Live.Slot == INDEX_NONE)
	{
		Live.Slot = TakeSlot(P.Look);
	}
	FSlot& Slot = Slots[Live.Slot];
	UMaterialInstanceDynamic* M = Slot.Material;
	const float U = FMath::Clamp(Local / FMath::Max(P.Life, 0.01f), 0.0f, 1.0f);
	// Size: quick out of the gate, settling (a burst opens fast and slows).
	const float Size = FMath::Lerp(P.SizeFrom, P.SizeTo, ChaosImpactEase::Out(U));
	// Brightness: up over its attack, then down with イージーイーズ; a flicker on top for flames and sparks.
	const float FadeFrom = FMath::Max(P.Attack, P.Hold);
	float Fade = U < P.Attack ? ChaosImpactEase::Out(U / FMath::Max(P.Attack, 0.001f))
		: 1.0f - ChaosImpactEase::InOut((U - FadeFrom) / FMath::Max(1.0f - FadeFrom, 0.001f));
	if (P.Flicker > 0.0f)
	{
		const float Wobble = 0.5f + 0.5f * FMath::Sin(Live.Age * 37.0f + Live.Seed) * FMath::Sin(Live.Age * 23.0f + Live.Seed * 1.7f);
		Fade *= 1.0f - P.Flicker * 0.6f * Wobble;
	}
	if (M)
	{
		M->SetVectorParameterValue(TEXT("Color"), P.Color);
		M->SetVectorParameterValue(TEXT("CoreColor"), P.CoreColor);
		M->SetScalarParameterValue(TEXT("Erode"), FMath::Lerp(P.ErodeFrom, P.ErodeTo, ChaosImpactEase::In(U)));
		M->SetScalarParameterValue(TEXT("NoiseScale"), P.NoiseScale);
		M->SetScalarParameterValue(TEXT("NoiseSpeed"), P.NoiseSpeed);
		M->SetScalarParameterValue(TEXT("NoiseAmount"), P.NoiseAmount);
		M->SetScalarParameterValue(TEXT("Softness"), P.Softness);
		M->SetScalarParameterValue(TEXT("Taper"), P.Taper);
		M->SetScalarParameterValue(TEXT("Toon"), P.Toon);
		if (P.Look == EChaosImpactFxLook::Spiral)
		{
			M->SetScalarParameterValue(TEXT("Arms"), P.Arms);
			M->SetScalarParameterValue(TEXT("Twist"), P.Twist);
			M->SetScalarParameterValue(TEXT("Swirl"), P.Swirl);
			M->SetScalarParameterValue(TEXT("Sharpness"), P.Sharpness);
		}
		if (P.Look == EChaosImpactFxLook::Smoke || P.Look == EChaosImpactFxLook::Flame || P.Look == EChaosImpactFxLook::GroundFlame
			|| P.Look == EChaosImpactFxLook::SolidRing)
		{
			M->SetScalarParameterValue(TEXT("Intensity"), P.Intensity);
			M->SetScalarParameterValue(TEXT("Opacity"), P.Opacity * Fade);
		}
		else
		{
			M->SetScalarParameterValue(TEXT("Intensity"), P.Intensity * Fade);
		}
		if (!IsFlat(P.Look) && P.Look != EChaosImpactFxLook::Streak)
		{
			// Turned to the camera, a big piece low over the floor dips into it: melt it in over a band as wide as a good
			// part of its size, so it never shows a straight cut where it meets the floor.
			M->SetScalarParameterValue(TEXT("Fade"), FMath::Max(40.0f, Size * 0.4f));
		}
		if (P.Look == EChaosImpactFxLook::Ring || P.Look == EChaosImpactFxLook::AirRing || P.Look == EChaosImpactFxLook::SolidRing)
		{
			M->SetScalarParameterValue(TEXT("Radius"), FMath::Lerp(P.RingFrom, P.RingTo, ChaosImpactEase::Out(U)));
			M->SetScalarParameterValue(TEXT("Width"), P.RingWidth * (1.0f + 0.6f * U));
		}
	}
	FVector Where = GetActorLocation() + Live.Piece.Location;
	if (P.Taper > 0.0f)
	{
		// A flame stands on its spot: Location is its foot, sunk a little into the ground (the depth fade melts it in).
		Where.Z += Size * P.Width * 0.5f - Size * 0.12f;
	}
	FQuat Facing = FQuat::Identity;
	FVector Scale3D(Size / PlaneSize, Size / PlaneSize * P.Width, 1.0f);
	if (P.Look == EChaosImpactFxLook::Streak)
	{
		// Lying along its flight, its face turned to the camera as far as it can.
		const FVector Along = P.Velocity.IsNearlyZero() ? FVector::UpVector : Live.Piece.Velocity.GetSafeNormal();
		Facing = FRotationMatrix::MakeFromXZ(Along, Camera - Where).ToQuat();
		Scale3D = FVector(Size / PlaneSize, P.Width / PlaneSize, 1.0f);
	}
	else if (IsFlat(P.Look))
	{
		Facing = FQuat(FVector::UpVector, FMath::DegreesToRadians(Live.Seed * 3.6f + P.Spin * Local));
	}
	else
	{
		// Turned to the camera, spinning slowly about the view. Drawn nearer the camera along its line of sight (the same
		// spot and size on screen), so a big piece over the floor stands in front of it instead of dipping its lower half
		// into it and showing a straight cut. (Not a tall flame: its foot stands on the ground.)
		FVector ToCamera = (Camera - Where).GetSafeNormal();
		if (FMath::IsNearlyEqual(P.Width, 1.0f) && P.Taper <= 0.0f)
		{
			const float Distance = static_cast<float>(FVector::Dist(Camera, Where));
			const float Pull = FMath::Min(Size * 0.5f, Distance * 0.5f);
			Where += ToCamera * Pull;
			Scale3D *= (Distance - Pull) / FMath::Max(Distance, 1.0f);
			Scale3D.Z = 1.0f;
		}
		const FQuat Face = FRotationMatrix::MakeFromZ(ToCamera).ToQuat();
		Facing = FQuat(ToCamera, FMath::DegreesToRadians(Live.Seed * 3.6f + P.Spin * Local)) * Face;
		// A tall flame keeps its height upright (the plane's Y as near the world's up as it can face the camera).
		if (!FMath::IsNearlyEqual(P.Width, 1.0f))
		{
			Facing = FRotationMatrix::MakeFromZY(ToCamera, FVector::UpVector).ToQuat();
		}
	}
	Slot.Mesh->SetWorldLocationAndRotation(Where, Facing);
	Slot.Mesh->SetWorldScale3D(Scale3D);
	Slot.Mesh->SetVisibility(true);
}

void AChaosImpactFxKit::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	// Emitters make their pieces (each a little different).
	for (int32 Index = Emitters.Num() - 1; Index >= 0; --Index)
	{
		FEmitter& E = Emitters[Index];
		E.Owed += E.Rate * DeltaSeconds;
		while (E.Owed >= 1.0f)
		{
			E.Owed -= 1.0f;
			FChaosImpactFxPiece Piece = E.Template;
			const float Angle = Stream.FRandRange(0.0f, UE_TWO_PI);
			const float Reach = FMath::Sqrt(Stream.FRand()) * E.Radius;
			Piece.Location += FVector(FMath::Cos(Angle) * Reach, FMath::Sin(Angle) * Reach, 0.0f);
			Piece.Velocity += FVector(Stream.FRandRange(-1.0f, 1.0f) * E.VelocityJitter.X, Stream.FRandRange(-1.0f, 1.0f) * E.VelocityJitter.Y,
				Stream.FRandRange(-1.0f, 1.0f) * E.VelocityJitter.Z);
			const float Grow = 1.0f + Stream.FRandRange(-1.0f, 1.0f) * E.SizeJitter;
			Piece.SizeFrom *= Grow;
			Piece.SizeTo *= Grow;
			Piece.Delay = 0.0f;
			FLive Live;
			Live.Piece = Piece;
			Live.Seed = Stream.FRandRange(0.0f, 100.0f);
			Pieces.Add(Live);
		}
		E.Left -= DeltaSeconds;
		if (E.Left <= 0.0f)
		{
			Emitters.RemoveAtSwap(Index);
		}
	}
	FVector Camera = GetActorLocation() + FVector(-800.0f, 0.0f, 1400.0f);
	if (const APlayerController* Viewer = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		FRotator Unused;
		Viewer->GetPlayerViewPoint(Camera, Unused);
	}
	for (int32 Index = Pieces.Num() - 1; Index >= 0; --Index)
	{
		FLive& Live = Pieces[Index];
		Live.Age += DeltaSeconds;
		const float Local = Live.Age - Live.Piece.Delay;
		if (Local > 0.0f)
		{
			// Moves: gravity pulls, drag slows.
			FChaosImpactFxPiece& P = Live.Piece;
			P.Velocity.Z -= P.Gravity * DeltaSeconds;
			P.Velocity *= FMath::Max(0.0f, 1.0f - P.Drag * DeltaSeconds);
			P.Location += P.Velocity * DeltaSeconds;
		}
		if (Local >= Live.Piece.Life)
		{
			if (Live.Slot != INDEX_NONE)
			{
				Slots[Live.Slot].bBusy = false;
				Slots[Live.Slot].Mesh->SetVisibility(false);
			}
			Pieces.RemoveAtSwap(Index);
			continue;
		}
		Draw(Live, Camera);
	}
	if (Pieces.IsEmpty() && Emitters.IsEmpty() && Age > 0.1f)
	{
		Destroy();
	}
}
