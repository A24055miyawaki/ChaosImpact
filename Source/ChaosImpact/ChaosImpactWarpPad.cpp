#include "ChaosImpactWarpPad.h"

#include "ChaosImpact.h"
#include "ChaosImpactBallTypes.h"
#include "ChaosImpactCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	const TCHAR* WarpMeshPath = TEXT("/Game/ChaosImpact/Warp/Warp.Warp");
	/** A player who has just arrived may briefly read as off the pad online; they are not released before this. */
	constexpr double WarpArrivalGraceSeconds = 0.6;
	constexpr double WarpBurstSeconds = 0.85;
	/** The imported model's top surface, as a share of its height, until it is measured in play. */
	constexpr float WarpDefaultSurfaceShare = 0.54f;
	/** The imported model's raised top, in its own units: centre along Y, and radius halfway down its sloped edge. */
	constexpr float WarpModelPlatformCentreY = -3.55f;
	constexpr float WarpModelPlatformRadius = 29.0f;

	float WarpEaseOut(const float T)
	{
		return 1.0f - FMath::Pow(1.0f - FMath::Clamp(T, 0.0f, 1.0f), 3.0f);
	}

	void WarpSetGlow(UMaterialInstanceDynamic* Material, const float Intensity)
	{
		if (Material)
		{
			Material->SetScalarParameterValue(TEXT("Intensity"), FMath::Max(0.0f, Intensity));
		}
	}

	/** The engine cylinder is 100 across and 100 tall around its centre; this stands it on Base. */
	void WarpPlaceCylinder(UStaticMeshComponent* Mesh, const FVector& Base, const float Radius, const float Height)
	{
		if (Mesh)
		{
			Mesh->SetWorldLocation(Base + FVector(0.0f, 0.0f, Height * 0.5f));
			Mesh->SetWorldScale3D(FVector(Radius / 50.0f, Radius / 50.0f, FMath::Max(Height, 0.5f) / 100.0f));
		}
	}

	float WarpHalfHeight(const ACharacter* Character)
	{
		return Character && Character->GetCapsuleComponent() ? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.0f;
	}
}

AChaosImpactWarpPad::AChaosImpactWarpPad()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);
	SetNetUpdateFrequency(1.0f);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	PadMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PadMesh"));
	PadMesh->SetupAttachment(SceneRoot);
	PadMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	PadMesh->SetCollisionObjectType(ECC_WorldStatic);
	PadMesh->SetCollisionResponseToAllChannels(ECR_Block);
	PadMesh->CanCharacterStepUpOn = ECB_Yes;
	PadMesh->SetCanEverAffectNavigation(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	EffectCylinder = CylinderMesh.Object;

	StepCollision = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StepCollision"));
	StepCollision->SetupAttachment(SceneRoot);
	StepCollision->SetStaticMesh(EffectCylinder);
	StepCollision->SetHiddenInGame(true);
	StepCollision->SetCastShadow(false);
	StepCollision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	StepCollision->SetCollisionObjectType(ECC_WorldStatic);
	StepCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
	StepCollision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	StepCollision->CanCharacterStepUpOn = ECB_Yes;
	StepCollision->SetCanEverAffectNavigation(false);

	PadLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("PadLight"));
	PadLight->SetupAttachment(SceneRoot);
	PadLight->SetCastShadows(false);
	PadLight->SetAttenuationRadius(900.0f);
	PadLight->SetIntensity(1000.0f);
}

void AChaosImpactWarpPad::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AChaosImpactWarpPad, WarpGroup);
}

void AChaosImpactWarpPad::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	LayoutPad();
}

void AChaosImpactWarpPad::LayoutPad()
{
	if (!PadMesh->GetStaticMesh())
	{
		PadMesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, WarpMeshPath, nullptr, LOAD_NoWarn | LOAD_Quiet));
	}
	SurfaceHeight = ModelHeight * WarpDefaultSurfaceShare;
	PlatformOffsetY = 0.0f;
	PlatformRadius = TriggerRadius;
	if (const UStaticMesh* Mesh = PadMesh->GetStaticMesh(); Mesh && bFitModelToDiameter)
	{
		const FBoxSphereBounds Bounds = Mesh->GetBounds();
		const float Width = 2.0f * FMath::Max(Bounds.BoxExtent.X, Bounds.BoxExtent.Y);
		const float WidthScale = Width > KINDA_SMALL_NUMBER ? PadDiameter / Width : 1.0f;
		const float HeightScale = Bounds.BoxExtent.Z > KINDA_SMALL_NUMBER ? ModelHeight / (2.0f * Bounds.BoxExtent.Z) : 1.0f;
		PadMesh->SetRelativeScale3D(FVector(WidthScale, WidthScale, HeightScale));
		// Centred on the actor, standing on its origin.
		PadMesh->SetRelativeLocation(FVector(-Bounds.Origin.X * WidthScale, -Bounds.Origin.Y * WidthScale,
			-(Bounds.Origin.Z - Bounds.BoxExtent.Z) * HeightScale));
		PlatformOffsetY = (WarpModelPlatformCentreY - static_cast<float>(Bounds.Origin.Y)) * WidthScale;
		PlatformRadius = WarpModelPlatformRadius * WidthScale;
	}
	PadLight->SetRelativeLocation(FVector(0.0f, PlatformOffsetY, ModelHeight + 160.0f));
	PadLight->SetLightColor(GlowColor);
	LayoutStep();
}

void AChaosImpactWarpPad::LayoutStep()
{
	// The engine cylinder is 100 across and 100 tall around its centre.
	StepCollision->SetRelativeLocation(FVector(0.0f, PlatformOffsetY, SurfaceHeight * 0.5f));
	StepCollision->SetRelativeScale3D(FVector(PlatformRadius / 50.0f, PlatformRadius / 50.0f,
		FMath::Max(SurfaceHeight, 1.0f) / 100.0f));
}

FVector AChaosImpactWarpPad::GetPlatformCentre() const
{
	return GetActorTransform().TransformPosition(FVector(0.0f, PlatformOffsetY, 0.0f));
}

void AChaosImpactWarpPad::MeasureSurface()
{
	const FVector Location = GetActorLocation();
	FHitResult Hit;
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(WarpPadSurface), true);
	if (PadMesh->LineTraceComponent(Hit, Location + FVector(0.0f, 0.0f, ModelHeight * 2.0f + 200.0f),
		Location - FVector(0.0f, 0.0f, 10.0f), Params))
	{
		SurfaceHeight = static_cast<float>(Hit.ImpactPoint.Z - Location.Z);
		LayoutStep();
	}
}

void AChaosImpactWarpPad::BeginPlay()
{
	Super::BeginPlay();
	LayoutPad();
	MeasureSurface();
}

void AChaosImpactWarpPad::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (TPair<TWeakObjectPtr<AActor>, FChaosImpactWarpCharge>& Charge : Charges)
	{
		StopChargeVisuals(Charge.Value);
	}
	Charges.Reset();
	for (const FChaosImpactWarpBurst& Burst : Bursts)
	{
		for (UStaticMeshComponent* Mesh : {Burst.Beam.Get(), Burst.Ring.Get()})
		{
			if (Mesh)
			{
				Mesh->DestroyComponent();
			}
		}
	}
	Bursts.Reset();
	Super::EndPlay(EndPlayReason);
}

bool AChaosImpactWarpPad::IsStandingOnPad(const ACharacter* Character) const
{
	if (!Character)
	{
		return false;
	}
	const FVector Location = Character->GetActorLocation();
	const double Feet = Location.Z - WarpHalfHeight(Character);
	const double Top = GetActorLocation().Z + SurfaceHeight;
	// Only the raised middle counts: not the rim around it or the steps up to it.
	return FVector::Dist2D(Location, GetPlatformCentre()) <= TriggerRadius && Feet >= Top - 15.0 && Feet <= Top + 30.0;
}

float AChaosImpactWarpPad::FindChargeProgress(const AActor* Character)
{
	const UWorld* World = Character ? Character->GetWorld() : nullptr;
	if (!World)
	{
		return 0.0f;
	}
	const TWeakObjectPtr<AActor> Key(const_cast<AActor*>(Character));
	for (TActorIterator<AChaosImpactWarpPad> It(World); It; ++It)
	{
		if (const FChaosImpactWarpCharge* Charge = It->Charges.Find(Key))
		{
			return FMath::Clamp(static_cast<float>(World->GetTimeSeconds() - Charge->StartedAt)
				/ FMath::Max(It->WarpChargeSeconds, 0.05f), 0.0f, 1.0f);
		}
	}
	return 0.0f;
}

void AChaosImpactWarpPad::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	for (auto It = ArrivedAt.CreateIterator(); It; ++It)
	{
		const ACharacter* Character = Cast<ACharacter>(It.Key().Get());
		if (!Character || (Now - It.Value() > WarpArrivalGraceSeconds && !IsStandingOnPad(Character)))
		{
			It.RemoveCurrent();
		}
	}

	// Charging runs on every machine for the effects; only the server sends anyone.
	TArray<AChaosImpactCharacter*, TInlineAllocator<8>> OnPad;
	TArray<AChaosImpactCharacter*, TInlineAllocator<4>> Ready;
	for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
	{
		AChaosImpactCharacter* Character = *It;
		if (Character->IsEliminated() || Character->IsMatchInputLocked() || ArrivedAt.Contains(Character)
			|| !IsStandingOnPad(Character))
		{
			continue;
		}
		OnPad.Add(Character);
		FChaosImpactWarpCharge* Charge = Charges.Find(Character);
		if (!Charge)
		{
			Charge = &Charges.Add(Character);
			Charge->StartedAt = Now;
			StartChargeVisuals(*Charge, Character);
		}
		if (HasAuthority() && Now - Charge->StartedAt >= WarpChargeSeconds)
		{
			Ready.Add(Character);
		}
	}
	float Strongest = 0.0f;
	for (auto It = Charges.CreateIterator(); It; ++It)
	{
		const AChaosImpactCharacter* Character = Cast<AChaosImpactCharacter>(It.Key().Get());
		if (!Character || !OnPad.Contains(Character))
		{
			StopChargeVisuals(It.Value());
			It.RemoveCurrent();
			continue;
		}
		const float Progress = FMath::Clamp(static_cast<float>(Now - It.Value().StartedAt) / FMath::Max(WarpChargeSeconds, 0.05f), 0.0f, 1.0f);
		Strongest = FMath::Max(Strongest, Progress);
		UpdateChargeVisuals(It.Value(), Character, Progress, Now);
	}
	for (AChaosImpactCharacter* Character : Ready)
	{
		TryWarp(Character);
	}

	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	UpdateBursts(Now);
	// Idle: the light breathes; it brightens while someone charges and flares after a warp.
	GlowBoost = FMath::Max(0.0f, GlowBoost - DeltaSeconds * 1.4f);
	const float Breath = 0.5f + 0.5f * FMath::Sin(static_cast<float>(Now) * 2.4f);
	PadLight->SetIntensity(700.0f + 500.0f * Breath + 7000.0f * Strongest + 16000.0f * GlowBoost);
}

UStaticMeshComponent* AChaosImpactWarpPad::MakeEffectMesh(const float RimOnly, UMaterialInstanceDynamic*& OutMaterial)
{
	OutMaterial = ChaosImpactBallTypes::MakeAdditive(this, GlowColor, 0.0f, RimOnly);
	UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(this);
	Mesh->SetStaticMesh(EffectCylinder);
	Mesh->SetMaterial(0, OutMaterial);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetGenerateOverlapEvents(false);
	Mesh->SetCastShadow(false);
	Mesh->RegisterComponent();
	return Mesh;
}

void AChaosImpactWarpPad::StartChargeVisuals(FChaosImpactWarpCharge& Charge, AChaosImpactCharacter* Character)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	if (UNiagaraSystem* Aura = ChaosImpactBallTypes::LoadEffect(ChaosImpactBallTypes::Effects::WarpAura))
	{
		Charge.Aura = UNiagaraFunctionLibrary::SpawnSystemAttached(Aura, Character->GetRootComponent(), NAME_None,
			FVector(0.0f, 0.0f, -WarpHalfHeight(Character)), FRotator::ZeroRotator, FVector(1.3f),
			EAttachLocation::KeepRelativeOffset, true, ENCPoolMethod::None, true, false);
	}
	UMaterialInstanceDynamic* Material = nullptr;
	Charge.Haze = MakeEffectMesh(1.0f, Material);
	Charge.HazeMaterial = Material;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Charge.Rings); ++Index)
	{
		Charge.Rings[Index] = MakeEffectMesh(1.0f, Material);
		Charge.RingMaterials[Index] = Material;
	}
	UpdateChargeVisuals(Charge, Character, 0.0f, GetWorld()->GetTimeSeconds());
}

void AChaosImpactWarpPad::UpdateChargeVisuals(const FChaosImpactWarpCharge& Charge, const AChaosImpactCharacter* Character,
	const float Progress, const double Now) const
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const FVector Feet = Character->GetPresentationLocation() - FVector(0.0f, 0.0f, WarpHalfHeight(Character));
	const float Time = static_cast<float>(Now);
	// A haze column that swells and wobbles, faster and brighter as the warp nears.
	const float Wobble = FMath::Sin(Time * (6.0f + 12.0f * Progress));
	WarpPlaceCylinder(Charge.Haze.Get(), Feet, 58.0f + 10.0f * Wobble + 32.0f * Progress,
		190.0f + 25.0f * FMath::Sin(Time * 4.3f) + 50.0f * Progress);
	WarpSetGlow(Charge.HazeMaterial.Get(), 0.6f + 2.2f * Progress + 0.4f * Wobble * (0.3f + Progress));
	// Rings well up from the feet and close in, more often as the warp nears.
	const float Cycle = Time * (0.8f + 2.4f * Progress);
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Charge.Rings); ++Index)
	{
		const float Phase = FMath::Frac(Cycle + Index * 0.5f);
		WarpPlaceCylinder(Charge.Rings[Index].Get(), Feet + FVector(0.0f, 0.0f, Phase * (170.0f + 60.0f * Progress)),
			(105.0f + 25.0f * Progress) * (1.0f - 0.55f * Phase), 16.0f);
		WarpSetGlow(Charge.RingMaterials[Index].Get(), (1.0f - Phase) * FMath::Min(1.0f, Phase * 6.0f) * (1.2f + 2.6f * Progress));
	}
}

void AChaosImpactWarpPad::StopChargeVisuals(FChaosImpactWarpCharge& Charge) const
{
	if (UNiagaraComponent* Aura = Charge.Aura.Get())
	{
		// Fades out, then removes itself.
		Aura->Deactivate();
	}
	for (UStaticMeshComponent* Mesh : {Charge.Haze.Get(), Charge.Rings[0].Get(), Charge.Rings[1].Get()})
	{
		if (Mesh)
		{
			Mesh->DestroyComponent();
		}
	}
	Charge = FChaosImpactWarpCharge();
}

void AChaosImpactWarpPad::SpawnBurst(const FVector& Base, const bool bArrival)
{
	if (GetNetMode() == NM_DedicatedServer || !GetWorld())
	{
		return;
	}
	FChaosImpactWarpBurst& Burst = Bursts.AddDefaulted_GetRef();
	Burst.StartedAt = GetWorld()->GetTimeSeconds();
	Burst.Base = Base;
	Burst.bArrival = bArrival;
	UMaterialInstanceDynamic* Material = nullptr;
	Burst.Beam = MakeEffectMesh(0.35f, Material);
	Burst.BeamMaterial = Material;
	Burst.Ring = MakeEffectMesh(1.0f, Material);
	Burst.RingMaterial = Material;
	UpdateBursts(Burst.StartedAt);
}

void AChaosImpactWarpPad::UpdateBursts(const double Now)
{
	for (int32 Index = Bursts.Num() - 1; Index >= 0; --Index)
	{
		const FChaosImpactWarpBurst& Burst = Bursts[Index];
		const float T = static_cast<float>((Now - Burst.StartedAt) / WarpBurstSeconds);
		if (T >= 1.0f)
		{
			for (UStaticMeshComponent* Mesh : {Burst.Beam.Get(), Burst.Ring.Get()})
			{
				if (Mesh)
				{
					Mesh->DestroyComponent();
				}
			}
			Bursts.RemoveAtSwap(Index);
			continue;
		}
		const float Ease = WarpEaseOut(T);
		const float Fade = FMath::Square(1.0f - T);
		if (Burst.bArrival)
		{
			// A pillar of light drops onto the pad and spreads out.
			WarpPlaceCylinder(Burst.Beam.Get(), Burst.Base, 20.0f + 95.0f * Ease, 1800.0f * (1.0f - Ease) + 220.0f);
		}
		else
		{
			// The player shoots up in a narrowing pillar.
			WarpPlaceCylinder(Burst.Beam.Get(), Burst.Base, 110.0f * (1.0f - Ease) + 14.0f, 250.0f + 1750.0f * Ease);
		}
		WarpSetGlow(Burst.BeamMaterial.Get(), 3.5f * Fade);
		WarpPlaceCylinder(Burst.Ring.Get(), Burst.Base, 90.0f + 480.0f * Ease, 20.0f * (1.0f - T) + 2.0f);
		WarpSetGlow(Burst.RingMaterial.Get(), 2.6f * Fade);
	}
}

AChaosImpactWarpPad* AChaosImpactWarpPad::ChooseDestination() const
{
	TArray<AChaosImpactWarpPad*> Candidates;
	for (TActorIterator<AChaosImpactWarpPad> It(GetWorld()); It; ++It)
	{
		if (*It != this && It->WarpGroup == WarpGroup && It->GetAttachParentActor() == GetAttachParentActor()
			&& !It->IsActorBeingDestroyed())
		{
			Candidates.Add(*It);
		}
	}
	return Candidates.IsEmpty() ? nullptr : Candidates[FMath::RandRange(0, Candidates.Num() - 1)];
}

void AChaosImpactWarpPad::TryWarp(AChaosImpactCharacter* Character)
{
	if (!Character || Character->IsEliminated() || Character->IsMatchInputLocked())
	{
		return;
	}
	AChaosImpactWarpPad* Destination = ChooseDestination();
	if (!Destination)
	{
		return;
	}
	const FVector From = Character->GetActorLocation();
	// A spread-out spot on top, so players arriving together do not land inside each other.
	const FVector2D Spread = FMath::RandPointInCircle(Destination->TriggerRadius * 0.35f);
	const FVector To = Destination->GetPlatformCentre() + FVector(Spread.X, Spread.Y,
		Destination->SurfaceHeight + WarpHalfHeight(Character) + 5.0f);
	// Marked before the move: arriving on the destination must not start a charge there.
	Destination->ArrivedAt.Add(Character, GetWorld()->GetTimeSeconds());
	Character->WarpTo(To);
	MulticastWarpEffects(Character, From, To, Destination);
	UE_LOG(LogChaosImpact, Log, TEXT("%s warped from %s to %s"), *Character->GetName(), *GetName(), *Destination->GetName());
}

void AChaosImpactWarpPad::MulticastWarpEffects_Implementation(AChaosImpactCharacter* Character, FVector_NetQuantize From,
	FVector_NetQuantize To, AChaosImpactWarpPad* Destination)
{
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (Character)
	{
		if (FChaosImpactWarpCharge* Charge = Charges.Find(Character))
		{
			StopChargeVisuals(*Charge);
			Charges.Remove(Character);
		}
		// The warp itself: a thump in the warping player's own controller.
		Character->PlayControllerRumble(0.3f, 0.85f, 0.25f);
		// Here the move may reach this machine a moment later; there they have just arrived.
		ArrivedAt.Add(Character, Now);
		if (IsValid(Destination))
		{
			Destination->ArrivedAt.Add(Character, Now);
		}
	}
	Flare();
	if (IsValid(Destination))
	{
		Destination->Flare();
	}
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const FVector ToFeet(0.0f, 0.0f, WarpHalfHeight(Character));
	SpawnBurst(From - ToFeet, false);
	(IsValid(Destination) ? Destination : this)->SpawnBurst(To - ToFeet, true);
	if (UNiagaraSystem* Out = ChaosImpactBallTypes::LoadEffect(ChaosImpactBallTypes::Effects::WarpOut))
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, Out, From, FRotator::ZeroRotator, FVector(1.4f));
	}
	if (UNiagaraSystem* In = ChaosImpactBallTypes::LoadEffect(ChaosImpactBallTypes::Effects::WarpIn))
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, In, To, FRotator::ZeroRotator, FVector(1.4f));
	}
}
