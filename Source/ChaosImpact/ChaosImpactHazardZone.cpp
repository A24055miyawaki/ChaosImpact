#include "ChaosImpactHazardZone.h"

#include "ChaosImpact.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactIceMeshes.h"
#include "ChaosImpactLightning.h"
#include "ChaosImpactTrainingTarget.h"

#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "ProceduralMeshComponent.h"
#include "TimerManager.h"

namespace
{
	UMaterialInterface* LoadMaterialWithFallback(const TCHAR* Path, const TCHAR* FallbackPath)
	{
		if (UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, Path, nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			return Material;
		}
		return LoadObject<UMaterialInterface>(nullptr, FallbackPath);
	}

	void SetColor(UMaterialInstanceDynamic* Material, const FLinearColor& Color)
	{
		if (Material)
		{
			Material->SetVectorParameterValue(TEXT("Color"), Color);
			Material->SetVectorParameterValue(TEXT("Base Color"), Color);
		}
	}

	void SetIntensity(UMaterialInstanceDynamic* Material, const float Intensity)
	{
		if (Material)
		{
			Material->SetScalarParameterValue(TEXT("Intensity"), FMath::Max(0.0f, Intensity));
		}
	}

	// Named uniquely: unity builds merge this file with others that define their own EaseOut.
	float HazardEaseOut(const float T)
	{
		return 1.0f - FMath::Pow(1.0f - FMath::Clamp(T, 0.0f, 1.0f), 3.0f);
	}

	/**
	 * Places an effect shape by its size in centimetres (engine sphere and cylinder: 100 across, centred), and
	 * sets its glow when Glow is not negative. A zero size hides it.
	 */
	void PlaceZoneShape(const FChaosImpactZoneMesh& Part, const FVector& Center, const float Diameter, const float Height,
		const FRotator& Rotation, const float Glow)
	{
		if (UStaticMeshComponent* Shape = Part.Mesh.Get())
		{
			const bool bVisible = Diameter > 0.5f && Height > 0.05f;
			Shape->SetVisibility(bVisible);
			if (bVisible)
			{
				Shape->SetWorldLocationAndRotation(Center, Rotation);
				Shape->SetWorldScale3D(FVector(Diameter / 100.0f, Diameter / 100.0f, Height / 100.0f));
			}
		}
		if (Glow >= 0.0f)
		{
			SetIntensity(Part.Material.Get(), Glow);
		}
	}

	/** Overshoots slightly before settling, for crystals snapping into place. */
	float EaseOutBack(const float T)
	{
		const float X = FMath::Clamp(T, 0.0f, 1.0f) - 1.0f;
		constexpr float Overshoot = 2.2f;
		return 1.0f + X * X * ((Overshoot + 1.0f) * X + Overshoot);
	}

	TArray<FName, TInlineAllocator<2>> EffectParameterNames(const TCHAR* Name)
	{
		TArray<FName, TInlineAllocator<2>> Names;
		Names.Add(FName(Name));
		const FString Compact = FString(Name).Replace(TEXT(" "), TEXT(""));
		if (!Compact.Equals(Name))
		{
			Names.Add(FName(*Compact));
		}
		return Names;
	}
}

UMaterialInterface* ChaosImpactBallTypes::GetAdditiveMaterial()
{
	return LoadMaterialWithFallback(TEXT("/Game/ChaosImpact/FX/M_CI_Additive.M_CI_Additive"),
		TEXT("/Game/LevelPrototyping/Interactable/JumpPad/Assets/Materials/M_SimpleGlow.M_SimpleGlow"));
}

UMaterialInterface* ChaosImpactBallTypes::GetEmissiveMaterial()
{
	return LoadMaterialWithFallback(TEXT("/Game/ChaosImpact/FX/M_CI_Emissive.M_CI_Emissive"),
		TEXT("/Game/LevelPrototyping/Materials/M_FlatCol.M_FlatCol"));
}

UMaterialInterface* ChaosImpactBallTypes::GetIceMaterial()
{
	return LoadMaterialWithFallback(TEXT("/Game/ChaosImpact/FX/M_CI_Ice.M_CI_Ice"),
		TEXT("/Game/LevelPrototyping/Interactable/JumpPad/Assets/Materials/M_SimpleGlow.M_SimpleGlow"));
}

UMaterialInstanceDynamic* ChaosImpactBallTypes::MakeAdditive(UObject* Outer, const FLinearColor& Color,
	const float Intensity, const float RimOnly)
{
	UMaterialInterface* Parent = GetAdditiveMaterial();
	UMaterialInstanceDynamic* Material = Parent ? UMaterialInstanceDynamic::Create(Parent, Outer) : nullptr;
	SetColor(Material, Color);
	SetIntensity(Material, Intensity);
	if (Material)
	{
		Material->SetScalarParameterValue(TEXT("RimOnly"), RimOnly);
	}
	return Material;
}

UMaterialInstanceDynamic* ChaosImpactBallTypes::MakeEmissive(UObject* Outer, const FLinearColor& Color,
	const float Intensity)
{
	UMaterialInterface* Parent = GetEmissiveMaterial();
	UMaterialInstanceDynamic* Material = Parent ? UMaterialInstanceDynamic::Create(Parent, Outer) : nullptr;
	SetColor(Material, Color);
	SetIntensity(Material, Intensity);
	return Material;
}

UMaterialInstanceDynamic* ChaosImpactBallTypes::MakeIce(UObject* Outer, const FLinearColor& Color,
	const float Opacity, const float Intensity)
{
	UMaterialInterface* Parent = GetIceMaterial();
	UMaterialInstanceDynamic* Material = Parent ? UMaterialInstanceDynamic::Create(Parent, Outer) : nullptr;
	SetColor(Material, Color);
	SetIntensity(Material, Intensity);
	if (Material)
	{
		Material->SetScalarParameterValue(TEXT("Opacity"), Opacity);
	}
	return Material;
}

UMaterialInstanceDynamic* ChaosImpactBallTypes::MakeIceCrystal(UObject* Outer, const float Opacity, const float Glow,
	const FLinearColor& Tint)
{
	UMaterialInterface* Parent = LoadMaterialWithFallback(TEXT("/Game/ChaosImpact/FX/M_CI_IceCrystal.M_CI_IceCrystal"),
		TEXT("/Game/ChaosImpact/FX/M_CI_Ice.M_CI_Ice"));
	UMaterialInstanceDynamic* Material = Parent ? UMaterialInstanceDynamic::Create(Parent, Outer) : nullptr;
	if (Material)
	{
		Material->SetScalarParameterValue(TEXT("Opacity"), Opacity);
		Material->SetScalarParameterValue(TEXT("Glow"), Glow);
		Material->SetVectorParameterValue(TEXT("Tint"), Tint);
	}
	return Material;
}

UMaterialInstanceDynamic* ChaosImpactBallTypes::MakeIceSurface(UObject* Outer)
{
	UMaterialInterface* Parent = LoadMaterialWithFallback(TEXT("/Game/ChaosImpact/FX/M_CI_IceSurface.M_CI_IceSurface"),
		TEXT("/Game/ChaosImpact/FX/M_CI_Ice.M_CI_Ice"));
	return Parent ? UMaterialInstanceDynamic::Create(Parent, Outer) : nullptr;
}

UNiagaraSystem* ChaosImpactBallTypes::LoadEffect(const TCHAR* ObjectPath)
{
	// Resolved once per path; the assets themselves are kept loaded by UChaosImpactFxPreloadSubsystem.
	static TMap<FString, TWeakObjectPtr<UNiagaraSystem>> Resolved;
	if (const TWeakObjectPtr<UNiagaraSystem>* Cached = Resolved.Find(ObjectPath); Cached && Cached->IsValid())
	{
		return Cached->Get();
	}
	const double StartedAt = FPlatformTime::Seconds();
	const bool bAlreadyLoaded = FindObject<UNiagaraSystem>(nullptr, ObjectPath) != nullptr;
	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, ObjectPath);
	if (System)
	{
		Resolved.Add(ObjectPath, System);
	}
	if (!bAlreadyLoaded)
	{
		UE_LOG(LogChaosImpact, Warning, TEXT("Effect loaded on demand during play (not preloaded): %s in %.1f ms"),
			ObjectPath, (FPlatformTime::Seconds() - StartedAt) * 1000.0);
	}
	return System;
}

namespace
{
	const TCHAR* const AllEffectPaths[] =
	{
		ChaosImpactBallTypes::Effects::Explosion, ChaosImpactBallTypes::Effects::Fire,
		ChaosImpactBallTypes::Effects::Smoke, ChaosImpactBallTypes::Effects::Shatter,
		ChaosImpactBallTypes::Effects::FireTrail, ChaosImpactBallTypes::Effects::BallTrail,
		ChaosImpactBallTypes::Effects::Damage,
		ChaosImpactBallTypes::Effects::Electricity, ChaosImpactBallTypes::Effects::SparkBurst,
		ChaosImpactBallTypes::Effects::DarkAura,
		ChaosImpactBallTypes::Effects::WarpAura, ChaosImpactBallTypes::Effects::WarpOut,
		ChaosImpactBallTypes::Effects::WarpIn, ChaosImpactBallTypes::Effects::LastHitSmoke
	};
}

void ChaosImpactBallTypes::PreloadAssets(TArray<TObjectPtr<UObject>>& OutKeepAlive)
{
	const double StartedAt = FPlatformTime::Seconds();
	for (const TCHAR* Path : AllEffectPaths)
	{
		if (UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, Path))
		{
#if WITH_EDITOR
			// Uncooked (editor / PIE) systems compile their scripts on first use; do it now instead.
			System->WaitForCompilationComplete(true, false);
#endif
			OutKeepAlive.AddUnique(System);
		}
	}
	const UMaterialInterface* const Materials[] =
	{
		GetAdditiveMaterial(), GetEmissiveMaterial(), GetIceMaterial(),
		LoadMaterialWithFallback(TEXT("/Game/ChaosImpact/FX/M_CI_IceCrystal.M_CI_IceCrystal"), TEXT("/Game/ChaosImpact/FX/M_CI_Ice.M_CI_Ice")),
		LoadMaterialWithFallback(TEXT("/Game/ChaosImpact/FX/M_CI_IceSurface.M_CI_IceSurface"), TEXT("/Game/ChaosImpact/FX/M_CI_Ice.M_CI_Ice"))
	};
	for (const UMaterialInterface* Material : Materials)
	{
		if (Material)
		{
			OutKeepAlive.AddUnique(const_cast<UMaterialInterface*>(Material));
		}
	}
	UE_LOG(LogChaosImpact, Log, TEXT("Preloaded %d ball FX assets in %.0f ms"), OutKeepAlive.Num(),
		(FPlatformTime::Seconds() - StartedAt) * 1000.0);
}

void ChaosImpactBallTypes::WarmUpEffects(UWorld* World, const FVector& Location)
{
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const double StartedAt = FPlatformTime::Seconds();
	TArray<TWeakObjectPtr<USceneComponent>> Temporary;
	for (const TCHAR* Path : AllEffectPaths)
	{
		if (UNiagaraSystem* System = LoadEffect(Path))
		{
			// No pre-cull check: the point is to create it even though nobody can see it.
			if (UNiagaraComponent* Effect = UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, System, Location,
				FRotator::ZeroRotator, FVector::OneVector, false, true, ENCPoolMethod::None, false))
			{
				Temporary.Add(Effect);
			}
		}
	}
	// One small sphere per FX material, so its shaders and pipeline states are ready too.
	if (UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")))
	{
		UObject* Outer = World->GetWorldSettings();
		const FLinearColor Neutral(0.5f, 0.7f, 1.0f);
		UMaterialInterface* const Materials[] =
		{
			MakeAdditive(Outer, Neutral, 1.0f), MakeEmissive(Outer, Neutral, 1.0f), MakeIce(Outer, Neutral, 0.5f),
			MakeIceCrystal(Outer, 0.5f), MakeIceSurface(Outer)
		};
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Materials); ++Index)
		{
			UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(Outer);
			Mesh->SetStaticMesh(Sphere);
			Mesh->SetMaterial(0, Materials[Index]);
			Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Mesh->SetCastShadow(false);
			Mesh->SetWorldLocation(Location + FVector(Index * 120.0f, 0.0f, 0.0f));
			Mesh->RegisterComponentWithWorld(World);
			Temporary.Add(Mesh);
		}
	}
	FTimerHandle CleanupTimer;
	World->GetTimerManager().SetTimer(CleanupTimer, FTimerDelegate::CreateLambda([Temporary]()
	{
		for (const TWeakObjectPtr<USceneComponent>& Component : Temporary)
		{
			if (Component.IsValid())
			{
				Component->DestroyComponent();
			}
		}
	}), 1.5f, false);
	UE_LOG(LogChaosImpact, Log, TEXT("Warmed up %d ball FX components in %.0f ms"), Temporary.Num(),
		(FPlatformTime::Seconds() - StartedAt) * 1000.0);
}

void ChaosImpactBallTypes::SetEffectColor(UNiagaraComponent* Effect, const TCHAR* Name, const FLinearColor& Color)
{
	if (Effect)
	{
		for (const FName& Parameter : EffectParameterNames(Name))
		{
			Effect->SetVariableLinearColor(Parameter, Color);
		}
	}
}

void ChaosImpactBallTypes::SetEffectFloat(UNiagaraComponent* Effect, const TCHAR* Name, const float Value)
{
	if (Effect)
	{
		for (const FName& Parameter : EffectParameterNames(Name))
		{
			Effect->SetVariableFloat(Parameter, Value);
		}
	}
}

void ChaosImpactBallTypes::SetEffectSize(UNiagaraComponent* Effect, const TCHAR* Name, const float Value)
{
	if (Effect)
	{
		for (const FName& Parameter : EffectParameterNames(Name))
		{
			Effect->SetVariableFloat(Parameter, Value);
			Effect->SetVariableVec2(Parameter, FVector2D(Value));
		}
	}
}

void ChaosImpactBallTypes::SetEffectVector(UNiagaraComponent* Effect, const TCHAR* Name, const FVector& Value)
{
	if (Effect)
	{
		for (const FName& Parameter : EffectParameterNames(Name))
		{
			Effect->SetVariableVec3(Parameter, Value);
		}
	}
}

void ChaosImpactBallTypes::PlayIceShatter(UObject* WorldContext, const FVector& Location, const float Scale,
	const float Amount)
{
	if (UNiagaraSystem* Shatter = LoadEffect(Effects::Shatter))
	{
		if (UNiagaraComponent* Effect = UNiagaraFunctionLibrary::SpawnSystemAtLocation(WorldContext, Shatter,
			Location, FRotator::ZeroRotator, FVector(Scale)))
		{
			SetEffectColor(Effect, TEXT("Base Color"), FLinearColor(0.62f, 0.86f, 1.0f));
			SetEffectFloat(Effect, TEXT("Burst Amount"), Amount);
			SetEffectVector(Effect, TEXT("Hit Normal"), FVector::UpVector);
			SetEffectVector(Effect, TEXT("Hit Direction"), -FVector::UpVector);
		}
	}
}

AChaosImpactHazardZone::AChaosImpactHazardZone()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(false);
	// Short-lived and important to read: every screen gets it wherever it happens.
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(10.0f);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	ZoneLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("ZoneLight"));
	ZoneLight->SetupAttachment(SceneRoot);
	ZoneLight->SetRelativeLocation(FVector(0.0f, 0.0f, 90.0f));
	ZoneLight->SetCastShadows(false);
	ZoneLight->SetAttenuationRadius(760.0f);
	ZoneLight->SetIntensity(0.0f);
}

void AChaosImpactHazardZone::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AChaosImpactHazardZone, ZoneType);
	DOREPLIFETIME(AChaosImpactHazardZone, BurstHeight);
	DOREPLIFETIME(AChaosImpactHazardZone, SourcePawn);
	DOREPLIFETIME(AChaosImpactHazardZone, VisualSeed);
}

float AChaosImpactHazardZone::GetRadius() const
{
	switch (ZoneType)
	{
	case EChaosImpactBallType::Ice: return IceRadius;
	case EChaosImpactBallType::Thunder: return ThunderRadius;
	case EChaosImpactBallType::Black: return BlackHoleRadius;
	default: return FireRadius;
	}
}

float AChaosImpactHazardZone::GetActiveSeconds() const
{
	switch (ZoneType)
	{
	case EChaosImpactBallType::Ice: return IceFloorSeconds;
	case EChaosImpactBallType::Thunder: return ThunderActiveSeconds;
	case EChaosImpactBallType::Black: return BlackHoleSeconds;
	default: return FireBurnSeconds;
	}
}

AChaosImpactHazardZone* AChaosImpactHazardZone::Detonate(UWorld* World, const EChaosImpactBallType Type,
	const FVector& Location, APawn* Source, AActor* Victim)
{
	if (!World || Type == EChaosImpactBallType::Normal)
	{
		return nullptr;
	}
	// The zone lies on the ground below the impact, even when the ball burst against a wall.
	FVector Ground = Location - FVector::UpVector * 38.0f;
	FHitResult GroundHit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ChaosImpactHazardGround), false);
	if (World->LineTraceSingleByObjectType(GroundHit, Location + FVector::UpVector * 40.0f,
		Location - FVector::UpVector * 1500.0f, FCollisionObjectQueryParams(ECC_WorldStatic), Params))
	{
		Ground = GroundHit.ImpactPoint;
	}
	const FTransform SpawnTransform(FRotator::ZeroRotator, Ground);
	AChaosImpactHazardZone* Zone = World->SpawnActorDeferred<AChaosImpactHazardZone>(StaticClass(), SpawnTransform,
		nullptr, Source, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Zone)
	{
		return nullptr;
	}
	Zone->ZoneType = Type;
	Zone->BurstHeight = FMath::Clamp(static_cast<float>(Location.Z - Ground.Z), 0.0f, 400.0f);
	Zone->SourcePawn = Source;
	Zone->DirectVictim = Victim;
	Zone->VisualSeed = FMath::Rand();
	Zone->FinishSpawning(SpawnTransform);
	return Zone;
}

bool AChaosImpactHazardZone::IsSlipperyAt(const UWorld* World, const FVector& FeetLocation)
{
	if (!World)
	{
		return false;
	}
	const double Now = World->GetTimeSeconds();
	for (TActorIterator<AChaosImpactHazardZone> It(const_cast<UWorld*>(World)); It; ++It)
	{
		const AChaosImpactHazardZone* Zone = *It;
		if (Zone->ZoneType != EChaosImpactBallType::Ice || Now - Zone->SpawnedAt > IceFloorSeconds)
		{
			continue;
		}
		const FVector Delta = FeetLocation - Zone->GetActorLocation();
		if (FVector(Delta.X, Delta.Y, 0.0f).SizeSquared() <= FMath::Square(IceRadius)
			&& FMath::Abs(Delta.Z) <= 90.0f)
		{
			return true;
		}
	}
	return false;
}

FVector AChaosImpactHazardZone::GetBlackHolePullOffset(const UWorld* World, AActor* Character, const float DeltaSeconds)
{
	FVector Offset = FVector::ZeroVector;
	if (!World || !IsValid(Character))
	{
		return Offset;
	}
	const double Now = World->GetTimeSeconds();
	for (TActorIterator<AChaosImpactHazardZone> It(const_cast<UWorld*>(World)); It; ++It)
	{
		AChaosImpactHazardZone* Zone = *It;
		const float Age = static_cast<float>(Now - Zone->SpawnedAt);
		if (Zone->ZoneType != EChaosImpactBallType::Black || Age > BlackHoleSeconds || Character == Zone->SourcePawn
			|| AChaosImpactGameState::AreTeammates(Zone->GetWorld(), Zone->SourcePawn, Character))
		{
			continue;
		}
		const FVector ToCentre = Zone->GetActorLocation() - Character->GetActorLocation();
		const FVector Flat(ToCentre.X, ToCentre.Y, 0.0f);
		const float Distance = static_cast<float>(Flat.Size());
		if (Distance > BlackHoleRadius || Distance < 6.0f || FMath::Abs(ToCentre.Z) > 320.0f)
		{
			continue;
		}
		// Takes hold as it opens and lets go as it closes; gentler at the core so nobody shoots past the centre.
		const float Strength = FMath::Clamp(Age / 0.35f, 0.0f, 1.0f) * FMath::Clamp((BlackHoleSeconds - Age) / 0.25f, 0.0f, 1.0f);
		const float Speed = FMath::Lerp(BlackHoleCoreSpeed, BlackHolePullSpeed, Distance / BlackHoleRadius) * Strength;
		Offset += Flat / Distance * FMath::Min(Speed * DeltaSeconds, Distance);
	}
	return Offset;
}

void AChaosImpactHazardZone::BeginPlay()
{
	Super::BeginPlay();
	SpawnedAt = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (GetNetMode() != NM_DedicatedServer)
	{
		BuildPresentation();
	}
	if (HasAuthority())
	{
		SetLifeSpan(GetActiveSeconds() + FadeSeconds + EffectTailSeconds);
		ApplyDetonationEffects();
	}
	UE_LOG(LogChaosImpact, Log, TEXT("%s zone at %s (affected %d)"),
		ChaosImpactBallTypes::GetInternalName(ZoneType), *GetActorLocation().ToCompactString(), Affected.Num());
}

AController* AChaosImpactHazardZone::GetSourceController() const
{
	return SourcePawn ? SourcePawn->GetController() : nullptr;
}

bool AChaosImpactHazardZone::IsInside(const AActor* Actor, const float Padding, const float MaxHeight) const
{
	if (!IsValid(Actor))
	{
		return false;
	}
	const FVector Delta = Actor->GetActorLocation() - GetActorLocation();
	return FVector(Delta.X, Delta.Y, 0.0f).SizeSquared() <= FMath::Square(GetRadius() + Padding)
		&& Delta.Z >= -80.0f && Delta.Z <= MaxHeight;
}

void AChaosImpactHazardZone::ApplyDetonationEffects()
{
	UWorld* World = GetWorld();
	// A black hole only pulls (every machine, continuously); nothing happens at the moment it opens.
	if (!World || ZoneType == EChaosImpactBallType::Black)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	AController* SourceController = GetSourceController();
	const float BlastHeight = 260.0f + BurstHeight;
	auto Affect = [&](AActor* Victim)
	{
		if (AChaosImpactGameState::AreTeammates(World, SourcePawn, Victim))
		{
			return;
		}
		Affected.Add(Victim);
		if (ZoneType == EChaosImpactBallType::Fire || ZoneType == EChaosImpactBallType::Thunder)
		{
			// The direct victim already took the ball itself; the blast does not count twice.
			if (Victim != DirectVictim.Get())
			{
				UGameplayStatics::ApplyDamage(Victim, ZoneDamage, SourceController, this, nullptr);
			}
			if (ZoneType == EChaosImpactBallType::Fire)
			{
				NextBurnAt.Add(Victim, Now + FireBurnInterval);
			}
			else
			{
				MulticastShock(Victim);
			}
		}
		else if (AChaosImpactCharacter* Character = Cast<AChaosImpactCharacter>(Victim))
		{
			Character->ApplyIceFreeze(IceFreezeSeconds);
		}
	};
	if (AActor* Victim = DirectVictim.Get(); Victim && Victim != SourcePawn)
	{
		Affect(Victim);
	}
	for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
	{
		AChaosImpactCharacter* Character = *It;
		if (Character == SourcePawn || Character->IsEliminated() || Affected.Contains(Character)
			|| !IsInside(Character, Character->GetCapsuleComponent()->GetScaledCapsuleRadius(), BlastHeight))
		{
			continue;
		}
		Affect(Character);
	}
	if (ZoneType == EChaosImpactBallType::Fire || ZoneType == EChaosImpactBallType::Thunder)
	{
		for (TActorIterator<AChaosImpactTrainingTarget> It(World); It; ++It)
		{
			if (!It->IsDefeated() && !Affected.Contains(*It) && IsInside(*It, 50.0f, BlastHeight))
			{
				Affect(*It);
			}
		}
	}
}

bool AChaosImpactHazardZone::TryApplyLateHit(AChaosImpactCharacter* Victim)
{
	if (!HasAuthority() || !IsValid(Victim) || Victim == SourcePawn || Victim->IsEliminated()
		|| AChaosImpactGameState::AreTeammates(GetWorld(), SourcePawn, Victim)
		|| Affected.Contains(Victim) || !GetWorld())
	{
		return false;
	}
	Affected.Add(Victim);
	UGameplayStatics::ApplyDamage(Victim, ZoneDamage, GetSourceController(), this, nullptr);
	switch (ZoneType)
	{
	case EChaosImpactBallType::Ice:
		Victim->ApplyIceFreeze(IceFreezeSeconds);
		break;
	case EChaosImpactBallType::Fire:
		NextBurnAt.Add(Victim, GetWorld()->GetTimeSeconds() + FireBurnInterval);
		break;
	case EChaosImpactBallType::Thunder:
		MulticastShock(Victim);
		break;
	default:
		break;
	}
	return true;
}

void AChaosImpactHazardZone::MulticastShock_Implementation(AActor* Victim)
{
	if (GetNetMode() == NM_DedicatedServer || !IsValid(Victim) || !Victim->GetRootComponent())
	{
		return;
	}
	UNiagaraSystem* Electricity = ChaosImpactBallTypes::LoadEffect(ChaosImpactBallTypes::Effects::Electricity);
	UNiagaraComponent* Shock = Electricity ? UNiagaraFunctionLibrary::SpawnSystemAttached(Electricity,
		Victim->GetRootComponent(), NAME_None, FVector::ZeroVector, FRotator::ZeroRotator,
		EAttachLocation::KeepRelativeOffset, true) : nullptr;
	if (UNiagaraSystem* Sparks = ChaosImpactBallTypes::LoadEffect(ChaosImpactBallTypes::Effects::SparkBurst))
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, Sparks, Victim->GetActorLocation(), FRotator::ZeroRotator, FVector(1.2f));
	}
	if (Shock)
	{
		// Crackles for a moment, then dies out and removes itself.
		FTimerHandle StopTimer;
		GetWorldTimerManager().SetTimer(StopTimer, FTimerDelegate::CreateWeakLambda(Shock, [Shock]()
		{
			Shock->Deactivate();
		}), 1.1f, false);
	}
}

void AChaosImpactHazardZone::TickBurning()
{
	UWorld* World = GetWorld();
	const double Now = World->GetTimeSeconds();
	AController* SourceController = GetSourceController();
	constexpr float BurnHeight = 200.0f;
	auto Burn = [&](AActor* Victim, const float Padding)
	{
		if (!IsInside(Victim, Padding, BurnHeight))
		{
			return;
		}
		double& NextAt = NextBurnAt.FindOrAdd(Victim, 0.0);
		if (Now < NextAt)
		{
			return;
		}
		const float Applied = UGameplayStatics::ApplyDamage(Victim, ZoneDamage, SourceController, this, nullptr);
		// A dodge in progress is not burned; check again right after it ends.
		NextAt = Now + (Applied > 0.0f ? FireBurnInterval : 0.15f);
		if (Applied > 0.0f)
		{
			MulticastBurnHit(Victim->GetActorLocation());
		}
	};
	for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
	{
		if (*It != SourcePawn && !It->IsEliminated() && !AChaosImpactGameState::AreTeammates(World, SourcePawn, *It))
		{
			Burn(*It, It->GetCapsuleComponent()->GetScaledCapsuleRadius() * 0.5f);
		}
	}
	for (TActorIterator<AChaosImpactTrainingTarget> It(World); It; ++It)
	{
		if (!It->IsDefeated())
		{
			Burn(*It, 30.0f);
		}
	}
}

void AChaosImpactHazardZone::MulticastBurnHit_Implementation(FVector_NetQuantize Location)
{
	if (UNiagaraSystem* Burst = ChaosImpactBallTypes::LoadEffect(ChaosImpactBallTypes::Effects::Damage))
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, Burst, Location, FRotator::ZeroRotator, FVector(0.7f));
	}
}

void AChaosImpactHazardZone::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!GetWorld())
	{
		return;
	}
	const float Age = static_cast<float>(GetWorld()->GetTimeSeconds() - SpawnedAt);
	if (HasAuthority() && ZoneType == EChaosImpactBallType::Fire && Age < FireBurnSeconds)
	{
		TickBurning();
	}
	if (bPresentationBuilt)
	{
		UpdatePresentation(Age);
	}
}

void AChaosImpactHazardZone::BuildPresentation()
{
	FRandomStream Stream(VisualSeed);
	switch (ZoneType)
	{
	case EChaosImpactBallType::Ice:
		BuildIcePresentation(Stream);
		break;
	case EChaosImpactBallType::Thunder:
		BuildThunderPresentation();
		break;
	case EChaosImpactBallType::Black:
		BuildBlackHolePresentation(Stream);
		break;
	default:
		BuildFirePresentation(Stream);
		break;
	}
	bPresentationBuilt = true;
	UpdatePresentation(0.0f);
}

UNiagaraComponent* AChaosImpactHazardZone::AddLoopingEffect(const TCHAR* SystemPath, const FVector& RelativeLocation)
{
	UNiagaraSystem* System = ChaosImpactBallTypes::LoadEffect(SystemPath);
	UNiagaraComponent* Effect = System
		? UNiagaraFunctionLibrary::SpawnSystemAttached(System, SceneRoot, NAME_None, RelativeLocation,
			FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset, false)
		: nullptr;
	if (Effect)
	{
		LoopingEffects.Add(Effect);
	}
	return Effect;
}

void AChaosImpactHazardZone::BuildFirePresentation(FRandomStream& Stream)
{
	using namespace ChaosImpactBallTypes;
	ZoneLight->SetLightColor(FLinearColor(1.0f, 0.45f, 0.12f));

	if (UNiagaraSystem* Explosion = LoadEffect(Effects::Explosion))
	{
		if (UNiagaraComponent* Blast = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, Explosion,
			GetActorLocation() + FVector(0.0f, 0.0f, BurstHeight * 0.5f), FRotator::ZeroRotator, FVector(0.6f)))
		{
			SetEffectColor(Blast, TEXT("Smoke Color"), FLinearColor(0.09f, 0.08f, 0.07f));
		}
	}

	// A large fire where the ball burst and smaller ones spread over the burning area.
	if (UNiagaraComponent* Center = AddLoopingEffect(Effects::Fire, FVector::ZeroVector))
	{
		SetEffectFloat(Center, TEXT("Flame Scale"), 2.6f);
		SetEffectFloat(Center, TEXT("Smoke Spawn Scale"), 0.6f);
	}
	constexpr int32 Around = 6;
	for (int32 Index = 0; Index < Around; ++Index)
	{
		const float Angle = (Index + Stream.FRandRange(-0.25f, 0.25f)) * UE_TWO_PI / Around;
		const float Distance = FireRadius * Stream.FRandRange(0.5f, 0.74f);
		if (UNiagaraComponent* Flames = AddLoopingEffect(Effects::Fire,
			FVector(FMath::Cos(Angle) * Distance, FMath::Sin(Angle) * Distance, 0.0f)))
		{
			SetEffectFloat(Flames, TEXT("Flame Scale"), Stream.FRandRange(1.5f, 2.1f));
			SetEffectFloat(Flames, TEXT("Smoke Spawn Scale"), 0.2f);
			// One light source (the center fire) is enough for the whole area.
			SetEffectFloat(Flames, TEXT("Base Light Intentsity"), 0.0f);
		}
	}
}

void AChaosImpactHazardZone::BuildIcePresentation(FRandomStream& Stream)
{
	using namespace ChaosImpactBallTypes;
	using namespace ChaosImpactIceMeshes;
	ZoneLight->SetLightColor(FLinearColor(0.55f, 0.8f, 1.0f));
	const FVector Burst = GetActorLocation() + FVector(0.0f, 0.0f, FMath::Max(BurstHeight, 30.0f));
	PlayIceShatter(this, Burst, 1.6f, 3.0f);

	// Frozen ground: a ragged sheet of clear ice, a frostier core and cracks running out from the impact.
	auto AddSheetLayer = [&](const FMeshBuffers& Mesh, const float Opacity, const float Glow, const FLinearColor& Tint)
	{
		UMaterialInstanceDynamic* Look = MakeIceCrystal(this, Opacity, Glow, Tint);
		if (UProceduralMeshComponent* Layer = CreateComponent(this, SceneRoot, Mesh, Look))
		{
			IceSheets.Add(Layer);
			FadingMaterials.Add({Look, Opacity});
		}
	};
	{
		FMeshBuffers Sheet;
		AppendSheet(Sheet, FTransform(FVector(0.0f, 0.0f, 1.5f)), IceRadius, Stream);
		AddSheetLayer(Sheet, 0.32f, 0.05f, FLinearColor(0.2f, 0.45f, 0.75f));
		FMeshBuffers Frost;
		AppendSheet(Frost, FTransform(FVector(Stream.FRandRange(-25.0f, 25.0f), Stream.FRandRange(-25.0f, 25.0f), 2.6f)),
			IceRadius * 0.55f, Stream);
		AddSheetLayer(Frost, 0.16f, 0.1f, FLinearColor(0.6f, 0.8f, 0.95f));
		FMeshBuffers Cracks;
		constexpr int32 CrackCount = 11;
		for (int32 Index = 0; Index < CrackCount; ++Index)
		{
			const float Angle = (Index + Stream.FRandRange(-0.35f, 0.35f)) * UE_TWO_PI / CrackCount;
			const FVector Direction(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f);
			AppendCrack(Cracks, FTransform(FVector(0.0f, 0.0f, 3.6f)), Direction * Stream.FRandRange(10.0f, 40.0f),
				Direction, IceRadius * Stream.FRandRange(0.5f, 0.9f), Stream.FRandRange(2.5f, 4.5f), Stream);
		}
		AddSheetLayer(Cracks, 0.65f, 0.3f, FLinearColor(0.85f, 0.95f, 1.0f));
	}

	UMaterialInstanceDynamic* CrystalLook = MakeIceCrystal(this, 0.5f, 0.16f);
	FadingMaterials.Add({CrystalLook, 0.5f});
	auto AddCrystalGroup = [&](const FVector& Location, const int32 CrystalCount, const float Spread,
		const float MinRadius, const float MaxRadius, const float MinHeight, const float MaxHeight,
		const float MinTilt, const float MaxTilt, const float Delay)
	{
		FMeshBuffers Mesh;
		for (int32 Index = 0; Index < CrystalCount; ++Index)
		{
			const float Angle = Stream.FRand() * UE_TWO_PI;
			const FVector Outward(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f);
			const float Tilt = FMath::DegreesToRadians(Stream.FRandRange(MinTilt, MaxTilt));
			const FVector Axis = (FVector::UpVector * FMath::Cos(Tilt) + Outward * FMath::Sin(Tilt)).GetSafeNormal();
			const FTransform Placement(FRotationMatrix::MakeFromZ(Axis).Rotator(),
				Outward * Stream.FRandRange(0.0f, Spread));
			AppendCrystal(Mesh, Placement, Stream.FRandRange(MinRadius, MaxRadius),
				Stream.FRandRange(MinHeight, MaxHeight), Stream);
		}
		if (UProceduralMeshComponent* Group = CreateComponent(this, SceneRoot, Mesh, CrystalLook))
		{
			Group->SetRelativeLocation(Location);
			Crystals.Add({Group, Delay});
		}
	};
	// A tall cluster where the ball hit, then jagged groups breaking through around the frozen area.
	AddCrystalGroup(FVector::ZeroVector, 7, 30.0f, 9.0f, 16.0f, 55.0f, 115.0f, 5.0f, 38.0f, 0.0f);
	constexpr int32 Groups = 9;
	for (int32 Index = 0; Index < Groups; ++Index)
	{
		const float Angle = (Index + Stream.FRandRange(-0.3f, 0.3f)) * UE_TWO_PI / Groups;
		const float Distance = IceRadius * Stream.FRandRange(0.45f, 0.9f);
		AddCrystalGroup(FVector(FMath::Cos(Angle) * Distance, FMath::Sin(Angle) * Distance, 0.0f),
			Stream.RandRange(2, 4), 16.0f, 5.0f, 10.0f, 22.0f, 58.0f, 15.0f, 50.0f, 0.04f + Distance / IceRadius * 0.16f);
	}

	// No smoke plume here: the pack's plume is sized for scenery and buries the arena from this camera.
}

UStaticMeshComponent* AChaosImpactHazardZone::AddZoneMesh(FChaosImpactZoneMesh& Out, const bool bSphere,
	UMaterialInstanceDynamic* Material)
{
	UStaticMeshComponent* Shape = NewObject<UStaticMeshComponent>(this);
	Shape->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, bSphere
		? TEXT("/Engine/BasicShapes/Sphere.Sphere") : TEXT("/Engine/BasicShapes/Cylinder.Cylinder")));
	Shape->SetMaterial(0, Material);
	Shape->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Shape->SetGenerateOverlapEvents(false);
	Shape->SetCastShadow(false);
	Shape->SetVisibility(false);
	Shape->RegisterComponent();
	Out.Mesh = Shape;
	Out.Material = Material;
	return Shape;
}

void AChaosImpactHazardZone::BuildThunderPresentation()
{
	using namespace ChaosImpactBallTypes;
	// Yellow lightning, like the ball itself.
	ZoneLight->SetLightColor(FLinearColor(1.0f, 0.88f, 0.55f));
	if (UNiagaraSystem* Sparks = LoadEffect(Effects::SparkBurst))
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, Sparks,
			GetActorLocation() + FVector(0.0f, 0.0f, FMath::Max(BurstHeight, 40.0f)), FRotator::ZeroRotator, FVector(2.4f));
	}
	AddLoopingEffect(Effects::Electricity, FVector(0.0f, 0.0f, 30.0f));
	UMaterialInstanceDynamic* StrikeLook = MakeAdditive(this, FLinearColor(1.0f, 0.95f, 0.7f), 6.0f);
	StrikeMaterial = StrikeLook;
	StrikeMesh = ChaosImpactLightning::CreateComponent(this, SceneRoot, StrikeLook);
	UMaterialInstanceDynamic* ArcLook = MakeAdditive(this, FLinearColor(1.0f, 0.8f, 0.22f), 3.5f);
	ArcMaterial = ArcLook;
	ArcMesh = ChaosImpactLightning::CreateComponent(this, SceneRoot, ArcLook);
	AddZoneMesh(FlashSphere, true, MakeAdditive(this, FLinearColor(1.0f, 0.92f, 0.65f), 0.0f));
	UMaterialInstanceDynamic* RingLook = MakeAdditive(this, FLinearColor(1.0f, 0.82f, 0.3f), 0.0f);
	RingMaterial = RingLook;
	RingMesh = ChaosImpactLightning::CreateComponent(this, SceneRoot, RingLook);
	RebuildThunderBolts(0);
}

void AChaosImpactHazardZone::RebuildThunderBolts(const int32 Flicker)
{
	FRandomStream Stream(VisualSeed + Flicker * 7919);
	const FVector Facing = ChaosImpactLightning::GetViewDirection(GetWorld());
	const float StrikeZ = FMath::Max(BurstHeight, 30.0f) * 0.3f;
	if (UProceduralMeshComponent* Strike = StrikeMesh.Get())
	{
		ChaosImpactIceMeshes::FMeshBuffers Bolts;
		// The main bolt from high above, with a thinner one or two coming down beside it.
		const FVector Sky(Stream.FRandRange(-160.0f, 160.0f), Stream.FRandRange(-160.0f, 160.0f), 1900.0f);
		ChaosImpactLightning::AppendBolt(Bolts, Sky, FVector(0.0f, 0.0f, StrikeZ), 28.0f, Facing, Stream, 3);
		const int32 Extra = Stream.RandRange(1, 2);
		for (int32 Index = 0; Index < Extra; ++Index)
		{
			const FVector Start = Sky + FVector(Stream.FRandRange(-280.0f, 280.0f), Stream.FRandRange(-280.0f, 280.0f),
				Stream.FRandRange(-400.0f, 0.0f));
			const FVector End(Stream.FRandRange(-0.6f, 0.6f) * ThunderRadius, Stream.FRandRange(-0.6f, 0.6f) * ThunderRadius, 0.0f);
			ChaosImpactLightning::AppendBolt(Bolts, Start, End, 11.0f, Facing, Stream, 2);
		}
		ChaosImpactLightning::SetMesh(Strike, Bolts);
	}
	if (UProceduralMeshComponent* Arcs = ArcMesh.Get())
	{
		ChaosImpactIceMeshes::FMeshBuffers Bolts;
		// Arcs racing out over the ground to the edge of the blast...
		constexpr int32 ArcCount = 10;
		for (int32 Index = 0; Index < ArcCount; ++Index)
		{
			const float Angle = (Index + Stream.FRandRange(-0.4f, 0.4f)) * UE_TWO_PI / ArcCount;
			const FVector Out(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f);
			ChaosImpactLightning::AppendBolt(Bolts, Out * 20.0f + FVector(0.0f, 0.0f, 6.0f),
				Out * ThunderRadius * Stream.FRandRange(0.7f, 1.1f) + FVector(0.0f, 0.0f, 6.0f), 10.0f, FVector::UpVector, Stream, 2);
		}
		// ...and a few leaping up from the ground into the strike.
		for (int32 Index = 0; Index < 3; ++Index)
		{
			const float Angle = Stream.FRand() * UE_TWO_PI;
			const FVector From = FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * Stream.FRandRange(80.0f, ThunderRadius * 0.85f);
			ChaosImpactLightning::AppendBolt(Bolts, From + FVector(0.0f, 0.0f, 4.0f),
				FVector(0.0f, 0.0f, Stream.FRandRange(90.0f, 240.0f)), 6.0f, Facing, Stream, 1);
		}
		ChaosImpactLightning::SetMesh(Arcs, Bolts);
	}
}

void AChaosImpactHazardZone::UpdateThunderPresentation(const float Age)
{
	if (Age >= NextFlickerAge && Age < 0.9f)
	{
		++FlickerIndex;
		NextFlickerAge = Age + (Age < 0.6f ? 0.045f : 0.09f);
		RebuildThunderBolts(FlickerIndex);
	}
	FRandomStream FlickerStream(VisualSeed + FlickerIndex * 31);
	const float Crackle = FlickerStream.FRandRange(0.45f, 1.0f);
	if (UProceduralMeshComponent* Strike = StrikeMesh.Get())
	{
		// On at once, then flickering: some frames it is gone.
		Strike->SetVisibility(Age < 0.45f && (Age < 0.08f || Crackle > 0.55f));
		SetIntensity(StrikeMaterial.Get(), 6.0f * Crackle);
	}
	if (UProceduralMeshComponent* Arcs = ArcMesh.Get())
	{
		Arcs->SetVisibility(Age < 0.9f && (Age < 0.3f || Crackle > 0.6f));
		SetIntensity(ArcMaterial.Get(), 3.5f * Crackle * FMath::Exp(-Age * 2.0f));
	}
	const FVector Base = GetActorLocation();
	const float FlashT = Age / 0.28f;
	const float FlashSize = FlashT < 1.0f ? 2.0f * (30.0f + ThunderRadius * 0.6f * HazardEaseOut(FlashT)) : 0.0f;
	PlaceZoneShape(FlashSphere, Base + FVector(0.0f, 0.0f, FMath::Max(BurstHeight, 40.0f)), FlashSize, FlashSize,
		FRotator::ZeroRotator, 0.9f * FMath::Square(1.0f - FMath::Clamp(FlashT, 0.0f, 1.0f)));
	const float RingT = FMath::Clamp(Age / 0.4f, 0.0f, 1.0f);
	if (UProceduralMeshComponent* Ring = RingMesh.Get())
	{
		// A shock wave racing out to the edge of the blast, thinning as it goes.
		ChaosImpactIceMeshes::FMeshBuffers Rings;
		if (RingT < 1.0f)
		{
			ChaosImpactLightning::AppendRing(Rings, FVector(0.0f, 0.0f, 12.0f),
				40.0f + ThunderRadius * 1.05f * HazardEaseOut(RingT), 18.0f * (1.0f - RingT) + 3.0f, 96);
		}
		ChaosImpactLightning::SetMesh(Ring, Rings);
		SetIntensity(RingMaterial.Get(), 3.0f * (1.0f - RingT));
	}
	const float GroundFade = FMath::Clamp(1.0f - Age / 1.6f, 0.0f, 1.0f);
	PlaceZoneShape(GroundGlow, Base + FVector(0.0f, 0.0f, 2.0f), GroundFade > 0.0f ? ThunderRadius * 1.4f : 0.0f, 2.0f,
		FRotator::ZeroRotator, 0.1f * GroundFade * GroundFade * (0.75f + 0.25f * Crackle));
	ZoneLight->SetRelativeLocation(FVector(0.0f, 0.0f, 220.0f));
	ZoneLight->SetIntensity(Age < 0.6f ? 90000.0f * Crackle * FMath::Exp(-Age * 3.0f) : 0.0f);
}

void AChaosImpactHazardZone::BuildBlackHolePresentation(FRandomStream& Stream)
{
	using namespace ChaosImpactBallTypes;
	ZoneLight->SetLightColor(FLinearColor(0.55f, 0.2f, 1.0f));
	if (UNiagaraSystem* Sparks = LoadEffect(Effects::SparkBurst))
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, Sparks,
			GetActorLocation() + FVector(0.0f, 0.0f, BlackHoleCoreHeight), FRotator::ZeroRotator, FVector(1.4f));
	}
	AddLoopingEffect(Effects::DarkAura, FVector(0.0f, 0.0f, 20.0f));
	// Its reach is marked by thin rings (see UpdateBlackHolePresentation); a filled pool washed out the whole area.
	UMaterialInstanceDynamic* RingLook = MakeAdditive(this, FLinearColor(0.75f, 0.35f, 1.0f), 0.0f);
	RingMaterial = RingLook;
	RingMesh = ChaosImpactLightning::CreateComponent(this, SceneRoot, RingLook);
	AddZoneMesh(HaloSphere, true, MakeAdditive(this, FLinearColor(0.42f, 0.1f, 0.9f), 0.0f, 1.0f));
	AddZoneMesh(HorizonSphere, true, MakeAdditive(this, FLinearColor(0.95f, 0.5f, 1.0f), 0.0f, 1.0f));
	AddZoneMesh(CoreSphere, true, MakeEmissive(this, FLinearColor(0.0f, 0.0f, 0.0f), 0.0f));
	AddZoneMesh(DiskRings.AddDefaulted_GetRef(), false, MakeAdditive(this, FLinearColor(1.0f, 0.4f, 0.92f), 0.0f, 1.0f));
	AddZoneMesh(DiskRings.AddDefaulted_GetRef(), false, MakeAdditive(this, FLinearColor(0.52f, 0.32f, 1.0f), 0.0f, 1.0f));
	constexpr int32 MoteCount = 32;
	for (int32 Index = 0; Index < MoteCount; ++Index)
	{
		FChaosImpactZoneMesh& Mote = Motes.AddDefaulted_GetRef();
		AddZoneMesh(Mote, true, MakeAdditive(this, Stream.FRand() < 0.5f
			? FLinearColor(0.9f, 0.55f, 1.0f) : FLinearColor(0.5f, 0.36f, 1.0f), 0.0f));
		Mote.Angle = Stream.FRand() * UE_TWO_PI;
		Mote.Radius = BlackHoleRadius * Stream.FRandRange(0.25f, 1.0f);
		Mote.Height = Stream.FRandRange(10.0f, 230.0f);
		Mote.Seed = Stream.FRandRange(0.7f, 1.4f);
	}
	AddZoneMesh(FlashSphere, true, MakeAdditive(this, FLinearColor(0.85f, 0.65f, 1.0f), 0.0f));
}

void AChaosImpactHazardZone::UpdateBlackHolePresentation(const float Age)
{
	const float Delta = FMath::Clamp(Age - LastPresentationAge, 0.0f, 0.1f);
	LastPresentationAge = Age;
	const FVector Base = GetActorLocation();
	const FVector Core = Base + FVector(0.0f, 0.0f, BlackHoleCoreHeight);
	const bool bOpen = Age < BlackHoleSeconds;
	// Bursts open with a small overshoot, then collapses faster and faster when its time is up.
	const float CollapseT = FMath::Clamp((Age - BlackHoleSeconds) / 0.4f, 0.0f, 1.0f);
	const float Size = FMath::Max(EaseOutBack(Age / 0.4f), 0.0f) * (1.0f - CollapseT * CollapseT);
	const float Pulse = 0.5f + 0.5f * FMath::Sin(Age * 9.0f);
	const float Reach = bOpen ? FMath::Clamp(Age / 0.3f, 0.0f, 1.0f) : 1.0f - CollapseT;

	PlaceZoneShape(CoreSphere, Core, 110.0f * Size * (1.0f + 0.04f * Pulse), 110.0f * Size * (1.0f + 0.04f * Pulse),
		FRotator::ZeroRotator, -1.0f);
	const float HorizonSize = 150.0f * Size * (1.0f + 0.07f * Pulse);
	PlaceZoneShape(HorizonSphere, Core, HorizonSize, HorizonSize, FRotator::ZeroRotator, 1.6f + 0.8f * Pulse);
	const float HaloSize = 300.0f * Size * (1.0f + 0.1f * FMath::Sin(Age * 4.0f));
	PlaceZoneShape(HaloSphere, Core, HaloSize, HaloSize, FRotator::ZeroRotator, 0.3f);
	for (int32 Index = 0; Index < DiskRings.Num(); ++Index)
	{
		const bool bInner = Index == 0;
		PlaceZoneShape(DiskRings[Index], Core, (bInner ? 340.0f : 480.0f) * Size, 6.0f,
			FRotator(bInner ? 14.0f : -9.0f, Age * (bInner ? 260.0f : -190.0f), bInner ? 6.0f : -12.0f),
			(bInner ? 1.8f : 1.1f) * (0.8f + 0.2f * Pulse));
	}
	PlaceZoneShape(GroundGlow, Base + FVector(0.0f, 0.0f, 3.0f), BlackHoleRadius * 2.0f * Reach, 2.0f,
		FRotator::ZeroRotator, 0.07f * Reach);
	if (UProceduralMeshComponent* Ring = RingMesh.Get())
	{
		// The edge of its reach, and three rings closing in on the centre, thin as they start and end.
		ChaosImpactIceMeshes::FMeshBuffers Rings;
		if (Reach > 0.01f)
		{
			ChaosImpactLightning::AppendRing(Rings, FVector(0.0f, 0.0f, 6.0f), BlackHoleRadius * Reach, 12.0f * (0.8f + 0.2f * Pulse));
			for (int32 Index = 0; bOpen && Index < 3; ++Index)
			{
				const float Phase = FMath::Frac(Age * 0.9f + Index / 3.0f);
				ChaosImpactLightning::AppendRing(Rings, FVector(0.0f, 0.0f, 8.0f), BlackHoleRadius * (1.0f - Phase) * Reach,
					16.0f * FMath::Sin(Phase * UE_PI));
			}
		}
		ChaosImpactLightning::SetMesh(Ring, Rings);
		SetIntensity(RingMaterial.Get(), 1.1f * Reach);
	}
	// Motes of light spiral in, faster as they near the core, and start again from the rim.
	for (FChaosImpactZoneMesh& Mote : Motes)
	{
		const float Closeness = 1.0f - FMath::Clamp(Mote.Radius / BlackHoleRadius, 0.0f, 1.0f);
		if (bOpen)
		{
			Mote.Angle += (1.2f + 5.0f * Closeness) * Mote.Seed * Delta;
			Mote.Radius -= (160.0f + 900.0f * Closeness) * Mote.Seed * Delta;
			Mote.Height = FMath::Lerp(Mote.Height, BlackHoleCoreHeight, FMath::Min(Delta * 1.8f, 1.0f));
			if (Mote.Radius < 70.0f)
			{
				Mote.Radius = BlackHoleRadius * FMath::FRandRange(0.75f, 1.0f);
				Mote.Angle = FMath::FRand() * UE_TWO_PI;
				Mote.Height = FMath::FRandRange(10.0f, 230.0f);
			}
		}
		else
		{
			Mote.Radius *= FMath::Exp(-Delta * 9.0f);
		}
		if (UStaticMeshComponent* Shape = Mote.Mesh.Get())
		{
			const bool bShow = Size > 0.02f && Mote.Radius > 8.0f;
			Shape->SetVisibility(bShow);
			if (bShow)
			{
				// A streak along its path, longer as it speeds up.
				const float Length = 16.0f + 60.0f * Closeness;
				Shape->SetWorldLocationAndRotation(
					Base + FVector(FMath::Cos(Mote.Angle) * Mote.Radius, FMath::Sin(Mote.Angle) * Mote.Radius, Mote.Height),
					FRotator(0.0f, FMath::RadiansToDegrees(Mote.Angle) + 90.0f, 0.0f));
				Shape->SetWorldScale3D(FVector(Length, 7.0f, 7.0f) / 100.0f);
			}
			SetIntensity(Mote.Material.Get(), 1.8f * Reach * FMath::Min(Closeness * 4.0f + 0.2f, 1.0f));
		}
	}
	UpdateBlackHoleTethers(Core, bOpen ? Reach : 0.0f);

	// Closing: a violet implosion flash where the core was.
	if (!bCollapseBurstPlayed && Age >= BlackHoleSeconds + 0.35f)
	{
		bCollapseBurstPlayed = true;
		if (UNiagaraSystem* Sparks = ChaosImpactBallTypes::LoadEffect(ChaosImpactBallTypes::Effects::SparkBurst))
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, Sparks, Core, FRotator::ZeroRotator, FVector(1.8f));
		}
	}
	const float FlashT = (Age - BlackHoleSeconds - 0.35f) / 0.3f;
	const float FlashSize = FlashT >= 0.0f && FlashT < 1.0f ? 40.0f + 360.0f * HazardEaseOut(FlashT) : 0.0f;
	PlaceZoneShape(FlashSphere, Core, FlashSize, FlashSize, FRotator::ZeroRotator,
		1.4f * FMath::Square(1.0f - FMath::Clamp(FlashT, 0.0f, 1.0f)));
	ZoneLight->SetRelativeLocation(FVector(0.0f, 0.0f, BlackHoleCoreHeight));
	ZoneLight->SetIntensity(bOpen
		? (1400.0f + 600.0f * Pulse) * Size
		: FlashT >= 0.0f ? 60000.0f * FMath::Exp(-FlashT * 3.0f) : 4000.0f * Size);
}

void AChaosImpactHazardZone::UpdateBlackHoleTethers(const FVector& Core, const float Strength)
{
	TSet<AActor*> Pulled;
	if (Strength > 0.0f && GetWorld())
	{
		for (TActorIterator<AChaosImpactCharacter> It(GetWorld()); It; ++It)
		{
			AChaosImpactCharacter* Character = *It;
			const FVector Offset = Character->GetActorLocation() - GetActorLocation();
			if (Character->IsEliminated() || Character == SourcePawn
				|| AChaosImpactGameState::AreTeammates(GetWorld(), SourcePawn, Character)
				|| FVector(Offset.X, Offset.Y, 0.0f).SizeSquared() > FMath::Square(BlackHoleRadius)
				|| FMath::Abs(Offset.Z) > 320.0f)
			{
				continue;
			}
			Pulled.Add(Character);
			FChaosImpactZoneMesh& Tether = Tethers.FindOrAdd(Character);
			if (!Tether.Mesh.IsValid())
			{
				AddZoneMesh(Tether, false, ChaosImpactBallTypes::MakeAdditive(this, FLinearColor(0.82f, 0.42f, 1.0f), 0.0f));
			}
			// A flickering thread of violet light from their body into the core.
			const FVector From = Character->GetPresentationLocation();
			const FVector Span = Core - From;
			const float Length = static_cast<float>(Span.Size());
			if (UStaticMeshComponent* Shape = Tether.Mesh.Get(); Shape && Length > 1.0f)
			{
				const float Width = 4.0f + 3.0f * FMath::FRand();
				Shape->SetVisibility(true);
				Shape->SetWorldLocationAndRotation(From + Span * 0.5f, FRotationMatrix::MakeFromZ(Span / Length).Rotator());
				Shape->SetWorldScale3D(FVector(Width / 100.0f, Width / 100.0f, Length / 100.0f));
			}
			SetIntensity(Tether.Material.Get(), 1.4f * Strength * FMath::FRandRange(0.5f, 1.0f));
		}
	}
	for (auto It = Tethers.CreateIterator(); It; ++It)
	{
		if (!Pulled.Contains(It.Key().Get()))
		{
			if (UStaticMeshComponent* Shape = It.Value().Mesh.Get())
			{
				Shape->DestroyComponent();
			}
			It.RemoveCurrent();
		}
	}
}

void AChaosImpactHazardZone::UpdatePresentation(const float Age)
{
	const bool bIce = ZoneType == EChaosImpactBallType::Ice;
	const float Active = GetActiveSeconds();
	// 1 while active, then down to 0 over the fade.
	const float Fade = FMath::Clamp((Active + FadeSeconds - Age) / FadeSeconds, 0.0f, 1.0f);
	const float Appear = HazardEaseOut(Age / 0.25f);

	if (!bLoopingStopped && Age >= Active)
	{
		// Let flames and vapor die out naturally instead of vanishing.
		bLoopingStopped = true;
		for (const TWeakObjectPtr<UNiagaraComponent>& Effect : LoopingEffects)
		{
			if (Effect.IsValid())
			{
				Effect->Deactivate();
			}
		}
	}
	if (!bMistStopped && Age >= 0.45f)
	{
		bMistStopped = true;
		if (ImpactMist.IsValid())
		{
			ImpactMist->Deactivate();
		}
	}

	if (ZoneType == EChaosImpactBallType::Thunder)
	{
		UpdateThunderPresentation(Age);
		return;
	}
	if (ZoneType == EChaosImpactBallType::Black)
	{
		UpdateBlackHolePresentation(Age);
		return;
	}

	if (bIce)
	{
		if (!bThawPlayed && Age >= Active)
		{
			bThawPlayed = true;
			ChaosImpactBallTypes::PlayIceShatter(this, GetActorLocation() + FVector(0.0f, 0.0f, 40.0f), 1.2f, 1.5f);
		}
		for (const TWeakObjectPtr<USceneComponent>& Sheet : IceSheets)
		{
			if (USceneComponent* Layer = Sheet.Get())
			{
				// Spreads out from the impact within a quarter second.
				const float Spread = 0.2f + 0.8f * Appear;
				Layer->SetHiddenInGame(Fade <= 0.0f);
				Layer->SetRelativeScale3D(FVector(Spread, Spread, 1.0f));
			}
		}
		for (const TPair<TWeakObjectPtr<UMaterialInstanceDynamic>, float>& Faded : FadingMaterials)
		{
			if (UMaterialInstanceDynamic* Look = Faded.Key.Get())
			{
				Look->SetScalarParameterValue(TEXT("Opacity"), Faded.Value * Fade);
			}
		}
		for (const FCrystal& Crystal : Crystals)
		{
			if (USceneComponent* Component = Crystal.Component.Get())
			{
				// Burst up out of the ground with a small overshoot, sink back while melting.
				const float Grow = EaseOutBack((Age - Crystal.Delay) / 0.22f) * (0.35f + 0.65f * Fade);
				Component->SetHiddenInGame(Grow <= 0.01f);
				Component->SetRelativeScale3D(FVector(FMath::Max(Grow, 0.01f)));
			}
		}
	}

	if (ZoneLight)
	{
		const float Intensity = bIce
			? 25000.0f * FMath::Exp(-Age * 6.0f) + 1500.0f * Appear * Fade
			: 60000.0f * FMath::Exp(-Age * 7.0f);
		ZoneLight->SetIntensity(Intensity);
		ZoneLight->SetRelativeLocation(FVector(0.0f, 0.0f, 90.0f + BurstHeight * FMath::Exp(-Age * 5.0f)));
	}
}
