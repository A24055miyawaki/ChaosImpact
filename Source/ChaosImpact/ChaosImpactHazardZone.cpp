#include "ChaosImpactHazardZone.h"

#include "ChaosImpact.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactIceMeshes.h"
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
	return LoadObject<UNiagaraSystem>(nullptr, ObjectPath);
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
	return ZoneType == EChaosImpactBallType::Ice ? IceRadius : FireRadius;
}

float AChaosImpactHazardZone::GetActiveSeconds() const
{
	return ZoneType == EChaosImpactBallType::Ice ? IceFloorSeconds : FireBurnSeconds;
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
		ZoneType == EChaosImpactBallType::Ice ? TEXT("Ice") : TEXT("Fire"),
		*GetActorLocation().ToCompactString(), Affected.Num());
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
	if (!World)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	AController* SourceController = GetSourceController();
	const float BlastHeight = 260.0f + BurstHeight;
	auto Affect = [&](AActor* Victim)
	{
		Affected.Add(Victim);
		if (ZoneType == EChaosImpactBallType::Fire)
		{
			// The direct victim already took the ball itself; the blast does not count twice.
			if (Victim != DirectVictim.Get())
			{
				UGameplayStatics::ApplyDamage(Victim, ZoneDamage, SourceController, this, nullptr);
			}
			NextBurnAt.Add(Victim, Now + FireBurnInterval);
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
	if (ZoneType == EChaosImpactBallType::Fire)
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
		|| Affected.Contains(Victim) || !GetWorld())
	{
		return false;
	}
	Affected.Add(Victim);
	UGameplayStatics::ApplyDamage(Victim, ZoneDamage, GetSourceController(), this, nullptr);
	if (ZoneType == EChaosImpactBallType::Ice)
	{
		Victim->ApplyIceFreeze(IceFreezeSeconds);
	}
	else
	{
		NextBurnAt.Add(Victim, GetWorld()->GetTimeSeconds() + FireBurnInterval);
	}
	return true;
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
		if (*It != SourcePawn && !It->IsEliminated())
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
	if (UNiagaraSystem* Burst = LoadObject<UNiagaraSystem>(nullptr, TEXT("/Game/Variant_Combat/VFX/NS_Damage.NS_Damage")))
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
	if (ZoneType == EChaosImpactBallType::Ice)
	{
		BuildIcePresentation(Stream);
	}
	else
	{
		BuildFirePresentation(Stream);
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
