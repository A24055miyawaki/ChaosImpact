#include "ChaosImpactHazardZone.h"
#include "ChaosImpactFxKit.h"

#include "ChaosImpact.h"
#include "ChaosImpactCharacter.h"
#include "Engine/GameInstance.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactIceMeshes.h"
#include "ChaosImpactSfx.h"
#include "ChaosImpactLightning.h"
#include "ChaosImpactTrainingTarget.h"
#include "ChaosImpactCrashBall.h"

#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
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

UMaterialInstanceDynamic* ChaosImpactBallTypes::MakeSnow(UObject* Outer)
{
	UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/LevelPrototyping/Materials/M_FlatCol.M_FlatCol"));
	UMaterialInstanceDynamic* Material = Parent ? UMaterialInstanceDynamic::Create(Parent, Outer) : nullptr;
	if (Material)
	{
		SetColor(Material, FLinearColor(0.9f, 0.94f, 1.0f));
		Material->SetScalarParameterValue(TEXT("Roughness"), 0.82f);
	}
	return Material;
}

void ChaosImpactBallTypes::AttachSnowLumps(UObject* Owner, USceneComponent* Parent, UMaterialInterface* Look, const int32 Seed)
{
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (!Owner || !Parent || !Sphere)
	{
		return;
	}
	FRandomStream Stream(Seed);
	constexpr int32 Lumps = 7;
	for (int32 Index = 0; Index < Lumps; ++Index)
	{
		// Spread evenly over the ball, each pressed half into it.
		const float Z = 1.0f - 2.0f * (Index + 0.5f) / Lumps;
		const float Ring = FMath::Sqrt(FMath::Max(0.0f, 1.0f - Z * Z));
		const float Angle = Index * 2.39996323f + Stream.FRandRange(-0.3f, 0.3f);
		const FVector Out(FMath::Cos(Angle) * Ring, FMath::Sin(Angle) * Ring, Z);
		UStaticMeshComponent* Lump = NewObject<UStaticMeshComponent>(Owner);
		Lump->SetStaticMesh(Sphere);
		Lump->SetMaterial(0, Look);
		Lump->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Lump->SetGenerateOverlapEvents(false);
		Lump->SetCastShadow(false);
		Lump->SetupAttachment(Parent);
		const float Size = Stream.FRandRange(0.32f, 0.5f);
		Lump->SetRelativeLocation(Out * Stream.FRandRange(34.0f, 40.0f));
		Lump->SetRelativeScale3D(FVector(Size, Size, Size * Stream.FRandRange(0.7f, 1.0f)));
		Lump->SetRelativeRotation(FRotationMatrix::MakeFromZ(Out).Rotator());
		Lump->RegisterComponent();
	}
}

void ChaosImpactBallTypes::PlaySnowBurst(UObject* WorldContext, const FVector& Location, const float Scale)
{
	if (UNiagaraSystem* Shatter = LoadEffect(Effects::Shatter))
	{
		if (UNiagaraComponent* Effect = UNiagaraFunctionLibrary::SpawnSystemAtLocation(WorldContext, Shatter,
			Location, FRotator::ZeroRotator, FVector(Scale)))
		{
			SetEffectColor(Effect, TEXT("Base Color"), FLinearColor(0.95f, 0.97f, 1.0f));
			SetEffectFloat(Effect, TEXT("Burst Amount"), 3.0f * Scale);
			SetEffectVector(Effect, TEXT("Hit Normal"), FVector::UpVector);
			SetEffectVector(Effect, TEXT("Hit Direction"), -FVector::UpVector);
		}
	}
}

void ChaosImpactBallTypes::PlayBeamBurst(UObject* WorldContext, const FVector& Location, const float Scale)
{
	if (UNiagaraSystem* Sparks = LoadEffect(Effects::SparkBurst))
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(WorldContext, Sparks, Location, FRotator::ZeroRotator, FVector(Scale));
	}
	// The hit itself (FX kit, soft): a white-hot pink flash, a ring of light bursting out and sparks of it flung off.
	AChaosImpactFxKit* Fx = AChaosImpactFxKit::Spawn(WorldContext, Location);
	if (!Fx)
	{
		return;
	}
	Fx->Scale = FMath::Clamp(Scale, 0.5f, 1.6f);
	Fx->Stream.Initialize(FMath::Rand());
	const FLinearColor Pink = GetColor(EChaosImpactBallType::Beam);
	const FLinearColor White(1.0f, 0.9f, 1.0f);
	FChaosImpactFxPiece Flash;
	Flash.Life = 0.18f;
	Flash.SizeFrom = 120.0f;
	Flash.SizeTo = 290.0f;
	Flash.Color = Pink;
	Flash.CoreColor = White;
	Flash.Intensity = 6.0f;
	Flash.Attack = 0.05f;
	Flash.NoiseAmount = 0.2f;
	Fx->Add(Flash);
	// Lying flat: a ring turned to the camera this close to the floor would be cut in half by it.
	FChaosImpactFxPiece Burst;
	Burst.Look = EChaosImpactFxLook::Ring;
	Burst.Life = 0.32f;
	Burst.SizeFrom = Burst.SizeTo = 560.0f;
	Burst.RingFrom = 0.15f;
	Burst.RingTo = 0.9f;
	Burst.RingWidth = 0.06f;
	Burst.Color = Pink;
	Burst.CoreColor = White;
	Burst.Intensity = 3.5f;
	Burst.Attack = 0.05f;
	Burst.ErodeTo = 0.6f;
	Fx->Add(Burst);
	for (int32 Index = 0; Index < 12; ++Index)
	{
		FChaosImpactFxPiece Spark;
		Spark.Look = EChaosImpactFxLook::Streak;
		Spark.Velocity = FRotator(Fx->Stream.FRandRange(-20.0f, 60.0f), Fx->Stream.FRandRange(0.0f, 360.0f), 0.0f).Vector()
			* Fx->Stream.FRandRange(700.0f, 1500.0f);
		Spark.Gravity = 1800.0f;
		Spark.Drag = 2.0f;
		Spark.Life = Fx->Stream.FRandRange(0.25f, 0.5f);
		Spark.SizeFrom = 90.0f;
		Spark.SizeTo = 25.0f;
		Spark.Width = 6.0f;
		Spark.Color = Pink;
		Spark.CoreColor = White;
		Spark.Intensity = 6.0f;
		Spark.Attack = 0.02f;
		Spark.ErodeTo = 0.0f;
		Spark.Flicker = 0.4f;
		Fx->Add(Spark);
	}
}

void ChaosImpactBallTypes::BuildNovaLook(AActor* Owner, USceneComponent* Parent, FNovaLook& Out)
{
	if (!Owner || !Parent || Out.IsBuilt())
	{
		return;
	}
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (!Sphere || !Cylinder)
	{
		return;
	}
	// Each part follows the parent but is sized and turned in the world by UpdateNovaLook.
	const auto Add = [Owner, Parent](UStaticMesh* Mesh, UMaterialInterface* Look) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(Owner);
		Part->SetStaticMesh(Mesh);
		Part->SetMaterial(0, Look);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetGenerateOverlapEvents(false);
		Part->SetCastShadow(false);
		Part->SetupAttachment(Parent);
		Part->SetUsingAbsoluteScale(true);
		Part->SetUsingAbsoluteRotation(true);
		Part->RegisterComponent();
		return Part;
	};
	// The finished nova model, polished, in a faint glow of blue light.
	if (UStaticMesh* NovaModel = LoadObject<UStaticMesh>(nullptr, *ChaosImpactNova::GetMeshPath(EChaosImpactNovaRig::Nova, 0)))
	{
		UStaticMeshComponent* Model = Add(NovaModel, nullptr);
		UMaterialInstanceDynamic* Metal = ChaosImpactNova::MakeMaterial(Owner, EChaosImpactNovaRig::Nova);
		for (int32 Slot = 0; Slot < Model->GetNumMaterials(); ++Slot)
		{
			Model->SetMaterial(Slot, Metal);
		}
		Model->SetCastShadow(true);
		Out.Model = Model;
	}
	UMaterialInstanceDynamic* Halo = MakeAdditive(Owner, FLinearColor(0.15f, 0.45f, 1.0f), 0.3f, 1.0f);
	Out.Halo = Add(Sphere, Halo);
	Out.HaloMaterial = Halo;
	UPointLightComponent* Light = NewObject<UPointLightComponent>(Owner);
	Light->SetupAttachment(Parent);
	Light->SetLightColor(FLinearColor(0.5f, 0.84f, 1.0f));
	Light->SetCastShadows(false);
	Light->RegisterComponent();
	Out.Light = Light;
	if (UNiagaraSystem* System = LoadEffect(Effects::WindSparks))
	{
		UNiagaraComponent* Sparks = UNiagaraFunctionLibrary::SpawnSystemAttached(System, Parent, NAME_None,
			FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset, false);
		if (Sparks)
		{
			Sparks->SetUsingAbsoluteScale(true);
			Sparks->SetAllowScalability(false);
			SetEffectFloat(Sparks, TEXT("Spark Spawn Rate"), 90.0f);
			Out.Sparks = Sparks;
		}
	}
}

void ChaosImpactBallTypes::UpdateNovaLook(const FNovaLook& Look, const float Radius, const float Time, const float Glow)
{
	// Engine spheres are 100 across.
	const float Across = FMath::Max(Radius, 1.0f) * 2.0f / 100.0f;
	const auto Size = [](const TWeakObjectPtr<UStaticMeshComponent>& Part, const float Scale)
	{
		if (UStaticMeshComponent* Mesh = Part.Get())
		{
			Mesh->SetWorldScale3D(FVector(Scale));
		}
	};
	if (UStaticMeshComponent* Model = Look.Model.Get())
	{
		// As wide as the nova, turning slowly; its middle (114 up in the model) on the parent.
		const float Scale = FMath::Max(Radius, 1.0f) / ChaosImpactNova::ModelRadius;
		Model->SetWorldScale3D(FVector(Scale));
		Model->SetWorldRotation(FRotator(0.0f, Time * 40.0f, 0.0f));
		if (const USceneComponent* Parent = Model->GetAttachParent())
		{
			Model->SetWorldLocation(Parent->GetComponentLocation() - FVector(0.0f, 0.0f, 114.0f * Scale));
		}
	}
	Size(Look.Core, Across * 0.38f * (1.0f + 0.05f * FMath::Sin(Time * 13.0f)));
	Size(Look.Inner, Across * 0.8f * (1.0f + 0.05f * FMath::Sin(Time * 7.0f)));
	Size(Look.Shell, Across * (1.0f + 0.025f * FMath::Sin(Time * 9.0f)));
	Size(Look.Halo, Across * 1.3f * (1.0f + 0.07f * FMath::Sin(Time * 3.3f)));
	SetIntensity(Look.InnerMaterial.Get(), Glow * (0.35f + 0.1f * FMath::Sin(Time * 6.0f)));
	SetIntensity(Look.ShellMaterial.Get(), Glow * (1.1f + 0.3f * FMath::Sin(Time * 11.0f)));
	SetIntensity(Look.HaloMaterial.Get(), Glow * (0.12f + 0.05f * FMath::Sin(Time * 2.7f)));
	// Bands of energy wrapped round it at different tilts, each turning its own way.
	for (int32 Index = 0; Index < Look.Bands.Num(); ++Index)
	{
		if (UStaticMeshComponent* Band = Look.Bands[Index].Get())
		{
			const float Tilt = 35.0f + Index * 50.0f;
			Band->SetWorldRotation(FRotator(Tilt, Time * (Index % 2 ? -140.0f : 190.0f) + Index * 60.0f, Index * 25.0f));
			const float Wide = Across * (1.06f + 0.05f * Index);
			Band->SetWorldScale3D(FVector(Wide, Wide, FMath::Max(Across * 0.02f, 0.01f)));
		}
	}
	if (UPointLightComponent* Light = Look.Light.Get())
	{
		Light->SetIntensity(Glow * (5000.0f + Radius * 110.0f) * (0.9f + 0.1f * FMath::Sin(Time * 17.0f)));
		Light->SetAttenuationRadius(Radius * 3.5f + 500.0f);
	}
	if (UNiagaraComponent* Sparks = Look.Sparks.Get())
	{
		Sparks->SetWorldScale3D(FVector(FMath::Max(Radius / 40.0f, 0.4f)));
	}
}

void ChaosImpactBallTypes::SetNovaLookVisible(const FNovaLook& Look, const bool bVisible)
{
	for (const TWeakObjectPtr<UStaticMeshComponent>& Part : {Look.Model, Look.Core, Look.Inner, Look.Shell, Look.Halo})
	{
		if (UStaticMeshComponent* Mesh = Part.Get())
		{
			Mesh->SetVisibility(bVisible);
		}
	}
	for (const TWeakObjectPtr<UStaticMeshComponent>& Band : Look.Bands)
	{
		if (UStaticMeshComponent* Mesh = Band.Get())
		{
			Mesh->SetVisibility(bVisible);
		}
	}
	if (UPointLightComponent* Light = Look.Light.Get())
	{
		Light->SetVisibility(bVisible);
	}
	if (UNiagaraComponent* Sparks = Look.Sparks.Get())
	{
		if (bVisible && !Sparks->IsActive())
		{
			Sparks->Activate(true);
		}
		else if (!bVisible && Sparks->IsActive())
		{
			Sparks->Deactivate();
		}
	}
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
		ChaosImpactBallTypes::Effects::WindSparks, ChaosImpactBallTypes::Effects::DirtBurstSmall,
		ChaosImpactBallTypes::Effects::DirtBurstMedium,
		ChaosImpactBallTypes::Effects::WarpAura, ChaosImpactBallTypes::Effects::WarpOut,
		ChaosImpactBallTypes::Effects::WarpIn, ChaosImpactBallTypes::Effects::LastHitSmoke,
		ChaosImpactBallTypes::Effects::MuzzleFlash,
		ChaosImpactBallTypes::Effects::BigExplosion, ChaosImpactBallTypes::Effects::MediumExplosion,
		ChaosImpactBallTypes::Effects::DirtBurstLarge
	};
}

void ChaosImpactBallTypes::GetPreloadPaths(TArray<FSoftObjectPath>& OutPaths)
{
	for (const TCHAR* Path : AllEffectPaths)
	{
		OutPaths.Add(FSoftObjectPath(Path));
	}
	const TCHAR* const OtherPaths[] =
	{
		TEXT("/Game/ChaosImpact/FX/M_CI_Additive.M_CI_Additive"), TEXT("/Game/ChaosImpact/FX/M_CI_Emissive.M_CI_Emissive"),
		TEXT("/Game/ChaosImpact/FX/M_CI_Ice.M_CI_Ice"), TEXT("/Game/ChaosImpact/FX/M_CI_IceCrystal.M_CI_IceCrystal"),
		TEXT("/Game/ChaosImpact/FX/M_CI_IceSurface.M_CI_IceSurface"), TEXT("/Game/LevelPrototyping/Materials/M_FlatCol.M_FlatCol"),
		TEXT("/Engine/BasicShapes/Sphere.Sphere"), TEXT("/Engine/BasicShapes/Cylinder.Cylinder"),
		TEXT("/Engine/BasicShapes/Cone.Cone"), TEXT("/Engine/BasicShapes/Cube.Cube"),
		SimaeAssets::BallMesh, SimaeAssets::BallTexture, SimaeAssets::BirdTexture, SimaeAssets::BirdBody, SimaeAssets::BirdTail,
		SimaeAssets::BirdWing, SimaeAssets::BirdEye, SimaeAssets::BirdCrest, SimaeAssets::Chirp01, SimaeAssets::Chirp02,
		SimaeAssets::Chirp03, SimaeAssets::TexturedMaterial,
		TEXT("/Game/Characters/Mannequins/Anims/Death/MM_Death_Front_01.MM_Death_Front_01"),
		TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Jump/MM_Fall_Loop.MM_Fall_Loop"),
		TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Jump/MM_Land.MM_Land"),
		TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Attack/MM_ChargedAttack.MM_ChargedAttack"),
		TEXT("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle.MM_Idle")
	};
	for (const TCHAR* Path : OtherPaths)
	{
		OutPaths.Add(FSoftObjectPath(Path));
	}
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
		LoadMaterialWithFallback(TEXT("/Game/ChaosImpact/FX/M_CI_IceSurface.M_CI_IceSurface"), TEXT("/Game/ChaosImpact/FX/M_CI_Ice.M_CI_Ice")),
		// Snowballs.
		LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/LevelPrototyping/Materials/M_FlatCol.M_FlatCol"))
	};
	for (const UMaterialInterface* Material : Materials)
	{
		if (Material)
		{
			OutKeepAlive.AddUnique(const_cast<UMaterialInterface*>(Material));
		}
	}
	// The shapes ball looks are built from (nova layers, beam shafts, snow lumps, tornado funnels, landing rings).
	const TCHAR* const ShapePaths[] =
	{
		TEXT("/Engine/BasicShapes/Sphere.Sphere"), TEXT("/Engine/BasicShapes/Cylinder.Cylinder"),
		TEXT("/Engine/BasicShapes/Cone.Cone"), TEXT("/Engine/BasicShapes/Cube.Cube")
	};
	for (const TCHAR* Path : ShapePaths)
	{
		if (UStaticMesh* Shape = LoadObject<UStaticMesh>(nullptr, Path))
		{
			OutKeepAlive.AddUnique(Shape);
		}
	}
	const TCHAR* const SimaePaths[] =
	{
		SimaeAssets::BallMesh, SimaeAssets::BallTexture, SimaeAssets::BirdTexture, SimaeAssets::BirdBody, SimaeAssets::BirdTail,
		SimaeAssets::BirdWing, SimaeAssets::BirdEye, SimaeAssets::BirdCrest,
		SimaeAssets::Chirp01, SimaeAssets::Chirp02, SimaeAssets::Chirp03, SimaeAssets::TexturedMaterial
	};
	for (const TCHAR* Path : SimaePaths)
	{
		if (UObject* Asset = LoadObject<UObject>(nullptr, Path))
		{
			OutKeepAlive.AddUnique(Asset);
		}
	}
	// The results podium's poses, so the show never loads one as it starts.
	const TCHAR* const PodiumPaths[] =
	{
		TEXT("/Game/Characters/Mannequins/Anims/Death/MM_Death_Front_01.MM_Death_Front_01"),
		TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Jump/MM_Fall_Loop.MM_Fall_Loop"),
		TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Jump/MM_Land.MM_Land"),
		TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Attack/MM_ChargedAttack.MM_ChargedAttack"),
		TEXT("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle.MM_Idle")
	};
	for (const TCHAR* Path : PodiumPaths)
	{
		if (UObject* Asset = LoadObject<UObject>(nullptr, Path))
		{
			OutKeepAlive.AddUnique(Asset);
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
			// No pre-cull check: the point is to create it even though nobody can see it. Tiny, so nothing it throws
			// out (sparks, smoke, a blast) can reach up into view.
			if (UNiagaraComponent* Effect = UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, System, Location,
				FRotator::ZeroRotator, FVector(0.02f), false, true, ENCPoolMethod::None, false))
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
			MakeIceCrystal(Outer, 0.5f), MakeIceSurface(Outer), MakeSnow(Outer)
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
	DOREPLIFETIME(AChaosImpactHazardZone, bFireTrail);
	DOREPLIFETIME(AChaosImpactHazardZone, BurstScale);
}

float AChaosImpactHazardZone::GetRadius() const
{
	return VisualRadius > 0.0f ? VisualRadius : GetTypeRadius(ZoneType, bFireTrail, BurstScale);
}

float AChaosImpactHazardZone::GetVisualScale() const
{
	return VisualRadius > 0.0f ? VisualRadius / FMath::Max(GetTypeRadius(ZoneType, bFireTrail, BurstScale), 1.0f) : 1.0f;
}

float AChaosImpactHazardZone::GetTypeRadius(const EChaosImpactBallType Type, const bool bTrail, const float Scale)
{
	if (bTrail)
	{
		return FireTrailRadius;
	}
	switch (Type)
	{
	case EChaosImpactBallType::Ice: return IceRadius;
	case EChaosImpactBallType::Thunder: return ThunderRadius;
	case EChaosImpactBallType::Black: return BlackHoleRadius;
	case EChaosImpactBallType::Smoke: return ChaosImpactBallTypes::SmokeRadius;
	case EChaosImpactBallType::Beam: return 120.0f;
	case EChaosImpactBallType::Snow: return 150.0f;
	case EChaosImpactBallType::Nova: return ChaosImpactBallTypes::GetNovaBlastRadius(Scale);
	default: return FireRadius;
	}
}

float AChaosImpactHazardZone::GetAge() const
{
	return GetWorld() ? static_cast<float>(GetWorld()->GetTimeSeconds() - SpawnedAt) : 0.0f;
}

float AChaosImpactHazardZone::GetActiveSeconds() const
{
	if (VisualSeconds > 0.0f)
	{
		return VisualSeconds;
	}
	if (bFireTrail)
	{
		return FireTrailSeconds;
	}
	switch (ZoneType)
	{
	case EChaosImpactBallType::Ice: return IceFloorSeconds;
	case EChaosImpactBallType::Thunder: return ThunderActiveSeconds;
	case EChaosImpactBallType::Black: return BlackHoleSeconds;
	case EChaosImpactBallType::Smoke: return ChaosImpactBallTypes::SmokeCloudSeconds;
	case EChaosImpactBallType::Beam: return BeamFadeSeconds;
	case EChaosImpactBallType::Snow: return SnowBurstSeconds;
	case EChaosImpactBallType::Nova: return NovaBlastSeconds;
	default: return FireBurnSeconds;
	}
}

AChaosImpactHazardZone* AChaosImpactHazardZone::Detonate(UWorld* World, const EChaosImpactBallType Type,
	const FVector& Location, APawn* Source, AActor* Victim, const float Scale)
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
	Zone->BurstScale = FMath::Max(Scale, 0.1f);
	Zone->FinishSpawning(SpawnTransform);
	return Zone;
}

AChaosImpactHazardZone* AChaosImpactHazardZone::SpawnVisual(UWorld* World, const EChaosImpactBallType Type,
	const FVector& Location, const float Radius, const float Seconds, APawn* Source, const float Scale)
{
	if (!World || Type == EChaosImpactBallType::Normal || Type == EChaosImpactBallType::Nova || World->GetNetMode() == NM_DedicatedServer)
	{
		return nullptr;
	}
	FVector Ground = Location - FVector::UpVector * 38.0f;
	FHitResult GroundHit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ChaosImpactVisualZoneGround), false);
	if (World->LineTraceSingleByObjectType(GroundHit, Location + FVector::UpVector * 40.0f,
		Location - FVector::UpVector * 600.0f, FCollisionObjectQueryParams(ECC_WorldStatic), Params))
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
	Zone->SetReplicates(false);
	Zone->bVisualOnly = true;
	Zone->ZoneType = Type;
	Zone->BurstHeight = FMath::Clamp(static_cast<float>(Location.Z - Ground.Z), 0.0f, 400.0f);
	Zone->SourcePawn = Source;
	Zone->VisualSeed = FMath::Rand();
	Zone->BurstScale = FMath::Max(Scale, 0.1f);
	Zone->VisualRadius = FMath::Max(Radius, 20.0f);
	Zone->VisualSeconds = Seconds;
	Zone->FinishSpawning(SpawnTransform);
	return Zone;
}

void AChaosImpactHazardZone::EndVisual()
{
	if (bVisualOnly && GetWorld() && GetAge() < GetActiveSeconds())
	{
		VisualSeconds = FMath::Max(GetAge(), 0.05f);
		SetLifeSpan(FadeSeconds + EffectTailSeconds);
	}
}

AChaosImpactHazardZone* AChaosImpactHazardZone::SpawnFireTrail(UWorld* World, const FVector& BallLocation, APawn* Source,
	AChaosImpactHazardZone* Leader)
{
	if (!World)
	{
		return nullptr;
	}
	// On the ground under the ball; over a drop (nothing near below) there is nothing to burn.
	FHitResult GroundHit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ChaosImpactFireTrailGround), false);
	if (!World->LineTraceSingleByObjectType(GroundHit, BallLocation, BallLocation - FVector::UpVector * 900.0f,
		FCollisionObjectQueryParams(ECC_WorldStatic), Params) || GroundHit.ImpactNormal.Z < 0.6f)
	{
		return nullptr;
	}
	const FTransform SpawnTransform(FRotator::ZeroRotator, GroundHit.ImpactPoint);
	AChaosImpactHazardZone* Zone = World->SpawnActorDeferred<AChaosImpactHazardZone>(StaticClass(), SpawnTransform,
		nullptr, Source, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Zone)
	{
		return nullptr;
	}
	Zone->ZoneType = EChaosImpactBallType::Fire;
	Zone->bFireTrail = true;
	Zone->BurstHeight = 0.0f;
	Zone->SourcePawn = Source;
	Zone->TrailLeader = Leader;
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
		if (Zone->bVisualOnly || Zone->ZoneType != EChaosImpactBallType::Ice || Now - Zone->SpawnedAt > IceFloorSeconds)
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
		if (Zone->bVisualOnly || Zone->ZoneType != EChaosImpactBallType::Black || Age > BlackHoleSeconds || Character == Zone->SourcePawn
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
	if (bVisualOnly)
	{
		SetLifeSpan(GetActiveSeconds() + FadeSeconds + EffectTailSeconds);
		return;
	}
	if (HasAuthority())
	{
		SetLifeSpan(GetActiveSeconds() + FadeSeconds + EffectTailSeconds);
		ApplyDetonationEffects();
	}
	if (!bFireTrail)
	{
		UE_LOG(LogChaosImpact, Log, TEXT("%s zone at %s (affected %d)"),
			ChaosImpactBallTypes::GetInternalName(ZoneType), *GetActorLocation().ToCompactString(), Affected.Num());
	}
}

AController* AChaosImpactHazardZone::GetSourceController() const
{
	return SourcePawn ? SourcePawn->GetController() : nullptr;
}

bool AChaosImpactHazardZone::IsInside(const AActor* Actor, const float Padding, const float MaxHeight, const float Radius) const
{
	if (!IsValid(Actor))
	{
		return false;
	}
	const FVector Delta = Actor->GetActorLocation() - GetActorLocation();
	return FVector(Delta.X, Delta.Y, 0.0f).SizeSquared() <= FMath::Square((Radius >= 0.0f ? Radius : GetRadius()) + Padding)
		&& Delta.Z >= -80.0f && Delta.Z <= MaxHeight;
}

void AChaosImpactHazardZone::ApplyDetonationEffects()
{
	UWorld* World = GetWorld();
	// A black hole pulls (every machine, continuously) and burns at its centre; nothing happens as it opens.
	// A trail fire only burns. A beam fading and a snowball bursting are only for show (their hits are their own).
	if (!World || ZoneType == EChaosImpactBallType::Black || bFireTrail || ZoneType == EChaosImpactBallType::Beam
		|| ZoneType == EChaosImpactBallType::Snow || ZoneType == EChaosImpactBallType::Nova)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	AController* SourceController = GetSourceController();
	// A nova's blast is a dome as tall as it is wide.
	const float BlastHeight = ZoneType == EChaosImpactBallType::Nova ? GetRadius() : 260.0f + BurstHeight;
	auto Affect = [&](AActor* Victim)
	{
		if (AChaosImpactGameState::AreTeammates(World, SourcePawn, Victim))
		{
			return;
		}
		Affected.Add(Victim);
		if (ZoneType == EChaosImpactBallType::Smoke)
		{
			// Choking smoke: a hit like the other bursts, and the eyes stinging for a good while.
			if (Victim != DirectVictim.Get())
			{
				UGameplayStatics::ApplyDamage(Victim, ZoneDamage, SourceController, this, nullptr);
			}
			if (AChaosImpactCharacter* Character = Cast<AChaosImpactCharacter>(Victim))
			{
				Character->ApplyBlind(ChaosImpactBallTypes::SmokeBlindSeconds);
			}
		}
		else if (ZoneType == EChaosImpactBallType::Fire || ZoneType == EChaosImpactBallType::Thunder)
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
		else if (ZoneType == EChaosImpactBallType::Nova)
		{
			// Everyone in reach is knocked out regardless of HP; scoring still sees one hit plus one KO bonus.
			UGameplayStatics::ApplyDamage(Victim, ChaosImpactBallTypes::GetNovaDamage(BurstScale), SourceController, this, nullptr);
			ApplyNovaKnockback(Cast<AChaosImpactCharacter>(Victim));
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
	if (ZoneType == EChaosImpactBallType::Fire || ZoneType == EChaosImpactBallType::Thunder || ZoneType == EChaosImpactBallType::Smoke
		|| ZoneType == EChaosImpactBallType::Nova)
	{
		for (TActorIterator<AChaosImpactTrainingTarget> It(World); It; ++It)
		{
			if (!It->IsDefeated() && !Affected.Contains(*It) && IsInside(*It, 50.0f, BlastHeight))
			{
				Affect(*It);
			}
		}
		for (TActorIterator<AChaosImpactCrashBall> It(World); It; ++It)
		{
			AChaosImpactCrashBall* CrashBall = *It;
			if (IsValid(CrashBall) && CrashBall != DirectVictim.Get() && !Affected.Contains(CrashBall)
				&& IsInside(CrashBall, CrashBall->GetBallRadius(), BlastHeight + CrashBall->GetBallRadius()))
			{
				Affected.Add(CrashBall);
				const float Strength = ZoneType == EChaosImpactBallType::Smoke ? 0.45f : 0.85f;
				CrashBall->ApplySpecialEffectPush(this, SourcePawn, GetActorLocation(), FVector::ZeroVector, Strength, false);
			}
		}
	}
}

bool AChaosImpactHazardZone::TryApplyLateHit(AChaosImpactCharacter* Victim)
{
	if (bVisualOnly || !HasAuthority() || !IsValid(Victim) || Victim == SourcePawn || Victim->IsEliminated()
		|| AChaosImpactGameState::AreTeammates(GetWorld(), SourcePawn, Victim)
		|| Affected.Contains(Victim) || !GetWorld())
	{
		return false;
	}
	Affected.Add(Victim);
	UGameplayStatics::ApplyDamage(Victim, ZoneType == EChaosImpactBallType::Nova
		? ChaosImpactBallTypes::GetNovaDamage(BurstScale) : ZoneDamage, GetSourceController(), this, nullptr);
	switch (ZoneType)
	{
	case EChaosImpactBallType::Nova:
		ApplyNovaKnockback(Victim);
		break;
	case EChaosImpactBallType::Ice:
		Victim->ApplyIceFreeze(IceFreezeSeconds);
		break;
	case EChaosImpactBallType::Fire:
		NextBurnAt.Add(Victim, GetWorld()->GetTimeSeconds() + FireBurnInterval);
		break;
	case EChaosImpactBallType::Thunder:
		MulticastShock(Victim);
		break;
	case EChaosImpactBallType::Smoke:
		Victim->ApplyBlind(ChaosImpactBallTypes::SmokeBlindSeconds);
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
	ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::ThunderShock, Victim->GetActorLocation());
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

void AChaosImpactHazardZone::TickBurning(const float Radius)
{
	UWorld* World = GetWorld();
	const double Now = World->GetTimeSeconds();
	AController* SourceController = GetSourceController();
	constexpr float BurnHeight = 200.0f;
	// A trail's fires share one clock (the first fire's), so two touching fires burn once a second, not twice.
	TMap<TWeakObjectPtr<AActor>, double>& Clock = TrailLeader.IsValid() ? TrailLeader->NextBurnAt : NextBurnAt;
	auto Burn = [&](AActor* Victim, const float Padding)
	{
		if (!IsInside(Victim, Padding, BurnHeight, Radius))
		{
			return;
		}
		double& NextAt = Clock.FindOrAdd(Victim, 0.0);
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
	for (TActorIterator<AChaosImpactCrashBall> It(World); It; ++It)
	{
		AChaosImpactCrashBall* CrashBall = *It;
		if (IsValid(CrashBall) && IsInside(CrashBall, CrashBall->GetBallRadius() * 0.7f, BurnHeight + CrashBall->GetBallRadius(), Radius))
		{
			AActor* CooldownKey = TrailLeader.IsValid() ? TrailLeader.Get() : this;
			const FVector PreferredDir = ZoneType == EChaosImpactBallType::Black
				? (GetActorLocation() - CrashBall->GetActorLocation()).GetSafeNormal2D()
				: (CrashBall->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
			CrashBall->ApplySpecialEffectPush(CooldownKey, SourcePawn, GetActorLocation(), PreferredDir, 0.65f, true);
		}
	}
}

void AChaosImpactHazardZone::MulticastBurnHit_Implementation(FVector_NetQuantize Location)
{
	ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::FireSizzle, Location);
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
	const bool bGameplay = HasAuthority() && !bVisualOnly;
	if (bGameplay && ZoneType == EChaosImpactBallType::Fire && Age < GetActiveSeconds())
	{
		TickBurning(GetRadius());
	}
	else if (bGameplay && ZoneType == EChaosImpactBallType::Black && Age > 0.35f && Age < BlackHoleSeconds)
	{
		// Once it has opened, whoever it draws into the very centre burns.
		TickBurning(BlackHoleBurnRadius);
	}
	else if (bGameplay && ZoneType == EChaosImpactBallType::Smoke && Age < ChaosImpactBallTypes::SmokeCloudSeconds)
	{
		TickSmoke();
	}
	else if (bGameplay && ZoneType == EChaosImpactBallType::Nova)
	{
		TickNova(Age);
	}
	if (bPresentationBuilt)
	{
		UpdatePresentation(Age);
		if (!bZoneEndSoundPlayed && Age >= GetActiveSeconds())
		{
			// The fire dies down; a black hole closes with a thump.
			bZoneEndSoundPlayed = true;
			ChaosImpactSfx::Stop(ZoneLoopSound, FadeSeconds);
			ZoneLoopSound = nullptr;
			if (ZoneType == EChaosImpactBallType::Black)
			{
				ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::BlackClose, GetActorLocation());
			}
		}
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
	case EChaosImpactBallType::Smoke:
		BuildSmokePresentation(Stream);
		break;
	case EChaosImpactBallType::Beam:
		BuildBeamPresentation();
		break;
	case EChaosImpactBallType::Snow:
		BuildSnowPresentation(Stream);
		break;
	case EChaosImpactBallType::Nova:
		BuildNovaPresentation(Stream);
		break;
	default:
		if (bFireTrail)
		{
			BuildFireTrailPresentation(Stream);
		}
		else
		{
			BuildFirePresentation(Stream);
		}
		break;
	}
	PlayZoneSound();
	bPresentationBuilt = true;
	UpdatePresentation(0.0f);
}

void AChaosImpactHazardZone::PlayZoneSound()
{
	const FVector At = GetActorLocation() + FVector(0.0f, 0.0f, 40.0f);
	switch (ZoneType)
	{
	case EChaosImpactBallType::Ice:
		ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::IceBurst, At);
		break;
	case EChaosImpactBallType::Thunder:
		ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::ThunderBurst, At);
		break;
	case EChaosImpactBallType::Black:
		ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::BlackOpen, At);
		break;
	case EChaosImpactBallType::Smoke:
		ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::SmokePoof, At);
		break;
	case EChaosImpactBallType::Beam:
		ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::BeamStrike, At, 0.8f);
		break;
	case EChaosImpactBallType::Snow:
		// A big snowball: a deeper, louder crunch.
		ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::SnowBurst, At, FMath::Clamp(0.6f + 0.25f * BurstScale, 0.6f, 1.2f),
			FMath::Clamp(1.15f - 0.12f * BurstScale, 0.8f, 1.15f));
		break;
	case EChaosImpactBallType::Nova:
		ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::NovaBlast, At);
		break;
	default:
		// The small fires a fire ball leaves along its flight are its own hum's business.
		if (!bFireTrail)
		{
			ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::FireExplode, At);
			ZoneLoopSound = ChaosImpactSfx::PlayAttached(EChaosImpactSfx::FireLoop, SceneRoot, 1.1f, 0.85f);
		}
		break;
	}
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

namespace
{
	/** Under a fire: the ground lit hot orange where the flames stand, over a dark scorch that outlasts them. */
	void AddFireGround(AChaosImpactFxKit* Fx, const FVector& At, const float Size, const float Seconds, const float Glow)
	{
		FChaosImpactFxPiece Scorch;
		Scorch.Look = EChaosImpactFxLook::GroundFlame;
		Scorch.Location = At + FVector(0.0f, 0.0f, 2.0f);
		Scorch.Life = Seconds + 1.4f;
		Scorch.SizeFrom = Size * 0.85f;
		Scorch.SizeTo = Size;
		Scorch.Color = FLinearColor(0.015f, 0.01f, 0.008f);
		Scorch.CoreColor = FLinearColor(0.06f, 0.025f, 0.01f);
		Scorch.Intensity = 1.0f;
		Scorch.Opacity = 0.6f;
		Scorch.Attack = 0.03f;
		Scorch.ErodeFrom = 0.2f;
		Scorch.ErodeTo = 0.75f;
		Scorch.NoiseScale = 2.4f;
		Scorch.NoiseSpeed = 0.02f;
		Scorch.NoiseAmount = 0.9f;
		Scorch.Softness = 0.6f;
		Fx->Add(Scorch);
		FChaosImpactFxPiece Heat;
		Heat.Look = EChaosImpactFxLook::GroundGlow;
		Heat.Location = At + FVector(0.0f, 0.0f, 4.0f);
		Heat.Life = Seconds + 0.3f;
		Heat.SizeFrom = Size * 0.7f;
		Heat.SizeTo = Size * 0.8f;
		Heat.Color = FLinearColor(1.0f, 0.2f, 0.02f);
		Heat.CoreColor = FLinearColor(1.0f, 0.55f, 0.12f);
		Heat.Intensity = Glow;
		Heat.Attack = 0.04f;
		Heat.ErodeFrom = 0.1f;
		Heat.ErodeTo = 0.5f;
		Heat.NoiseScale = 2.2f;
		Heat.NoiseSpeed = 0.6f;
		Heat.NoiseAmount = 0.7f;
		Heat.Softness = 0.75f;
		Heat.Flicker = 0.35f;
		Fx->Add(Heat);
	}
}

void AChaosImpactHazardZone::BuildFirePresentation(FRandomStream& Stream)
{
	using namespace ChaosImpactBallTypes;
	ZoneLight->SetLightColor(FLinearColor(1.0f, 0.45f, 0.12f));
	const float Radius = GetRadius();
	const float Burning = GetActiveSeconds();
	const float Up = BurstHeight * 0.5f + 40.0f;

	if (UNiagaraSystem* Explosion = LoadEffect(Effects::Explosion))
	{
		if (UNiagaraComponent* Blast = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, Explosion,
			GetActorLocation() + FVector(0.0f, 0.0f, BurstHeight * 0.5f), FRotator::ZeroRotator, FVector(0.6f * GetVisualScale())))
		{
			SetEffectColor(Blast, TEXT("Smoke Color"), FLinearColor(0.09f, 0.08f, 0.07f));
		}
	}

	// A large fire where the ball burst and smaller ones spread over the burning area.
	TArray<FVector, TInlineAllocator<8>> FireSpots;
	FireSpots.Add(FVector::ZeroVector);
	if (UNiagaraComponent* Center = AddLoopingEffect(Effects::Fire, FVector::ZeroVector))
	{
		SetEffectFloat(Center, TEXT("Flame Scale"), 2.6f * FMath::Clamp(GetVisualScale(), 0.45f, 1.0f));
		SetEffectFloat(Center, TEXT("Smoke Spawn Scale"), 0.6f);
	}
	constexpr int32 Around = 6;
	for (int32 Index = 0; Index < Around; ++Index)
	{
		const float Angle = (Index + Stream.FRandRange(-0.25f, 0.25f)) * UE_TWO_PI / Around;
		const float Distance = Radius * Stream.FRandRange(0.5f, 0.74f);
		const FVector Spot(FMath::Cos(Angle) * Distance, FMath::Sin(Angle) * Distance, 0.0f);
		FireSpots.Add(Spot);
		if (UNiagaraComponent* Flames = AddLoopingEffect(Effects::Fire, Spot))
		{
			SetEffectFloat(Flames, TEXT("Flame Scale"), Stream.FRandRange(1.5f, 2.1f) * FMath::Clamp(GetVisualScale(), 0.5f, 1.0f));
			SetEffectFloat(Flames, TEXT("Smoke Spawn Scale"), 0.2f);
			// One light source (the center fire) is enough for the whole area.
			SetEffectFloat(Flames, TEXT("Base Light Intentsity"), 0.0f);
		}
	}

	AChaosImpactFxKit* Fx = AChaosImpactFxKit::Spawn(this, GetActorLocation());
	if (!Fx)
	{
		return;
	}
	Fx->Scale = FMath::Clamp(GetVisualScale(), 0.45f, 1.4f);
	Fx->Stream.Initialize(Stream.RandHelper(1 << 20));
	// The flames stand on burning ground: lit and scorched under each one, so nothing floats.
	AddFireGround(Fx, FVector::ZeroVector, Radius * 1.0f, Burning, 2.0f);
	for (int32 Index = 1; Index < FireSpots.Num(); ++Index)
	{
		AddFireGround(Fx, FireSpots[Index], Radius * 0.55f, Burning, 1.6f);
	}
	// The burst's punch: a hot flash, a shock ring racing over the ground, sparks thrown out and falling.
	FChaosImpactFxPiece Flash;
	Flash.Location = FVector(0.0f, 0.0f, Up);
	Flash.Life = 0.15f;
	Flash.SizeFrom = 220.0f;
	Flash.SizeTo = 400.0f;
	Flash.Color = FLinearColor(1.0f, 0.5f, 0.12f);
	Flash.CoreColor = FLinearColor(1.0f, 0.92f, 0.72f);
	Flash.Intensity = 7.0f;
	Flash.Attack = 0.04f;
	Flash.ErodeTo = 0.3f;
	Flash.NoiseAmount = 0.15f;
	Fx->Add(Flash);
	FChaosImpactFxPiece Shock;
	Shock.Look = EChaosImpactFxLook::Ring;
	Shock.Location = FVector(0.0f, 0.0f, 6.0f);
	Shock.Life = 0.45f;
	Shock.SizeFrom = Shock.SizeTo = Radius * 2.6f;
	Shock.RingFrom = 0.12f;
	Shock.RingTo = 0.95f;
	Shock.RingWidth = 0.07f;
	Shock.Color = FLinearColor(1.0f, 0.22f, 0.02f);
	Shock.CoreColor = FLinearColor(1.0f, 0.68f, 0.2f);
	Shock.Intensity = 3.5f;
	Shock.Attack = 0.05f;
	Shock.ErodeTo = 0.7f;
	Fx->Add(Shock);
	for (int32 Index = 0; Index < 16; ++Index)
	{
		FChaosImpactFxPiece Spark;
		Spark.Look = EChaosImpactFxLook::Streak;
		const FVector Out = FRotator(Stream.FRandRange(15.0f, 65.0f), Stream.FRandRange(0.0f, 360.0f), 0.0f).Vector();
		Spark.Location = FVector(0.0f, 0.0f, Up);
		Spark.Velocity = Out * Stream.FRandRange(800.0f, 1700.0f);
		Spark.Gravity = 2200.0f;
		Spark.Drag = 1.2f;
		Spark.Life = Stream.FRandRange(0.4f, 0.8f);
		Spark.SizeFrom = Stream.FRandRange(70.0f, 110.0f);
		Spark.SizeTo = 30.0f;
		Spark.Width = 7.0f;
		Spark.Color = FLinearColor(1.0f, 0.35f, 0.05f);
		Spark.CoreColor = FLinearColor(1.0f, 0.85f, 0.5f);
		Spark.Intensity = 6.0f;
		Spark.Attack = 0.02f;
		Spark.ErodeTo = 0.0f;
		Spark.Flicker = 0.5f;
		Fx->Add(Spark);
	}
}

void AChaosImpactHazardZone::BuildFireTrailPresentation(FRandomStream& Stream)
{
	using namespace ChaosImpactBallTypes;
	// The same flames as those round a burst, without its big centre fire, blast or plume of smoke: one to each fire,
	// large enough that neighbours 130 apart run into one another, so the path burns as one line. Under each, the
	// ground glows and scorches (kept low and soft: a flat mark the flames' own colour shrank them to dim tongues).
	if (UNiagaraComponent* Flames = AddLoopingEffect(Effects::Fire,
		FVector(Stream.FRandRange(-10.0f, 10.0f), Stream.FRandRange(-10.0f, 10.0f), 0.0f)))
	{
		// This fire's effect type pauses copies beyond its budget; a whole trail comes at once.
		Flames->SetAllowScalability(false);
		SetEffectFloat(Flames, TEXT("Flame Scale"), Stream.FRandRange(1.6f, 2.0f));
		SetEffectFloat(Flames, TEXT("Smoke Spawn Scale"), 0.2f);
	}
	if (AChaosImpactFxKit* Fx = AChaosImpactFxKit::Spawn(this, GetActorLocation()))
	{
		Fx->Stream.Initialize(Stream.RandHelper(1 << 20));
		AddFireGround(Fx, FVector::ZeroVector, 210.0f, GetActiveSeconds(), 1.6f);
	}
}

void AChaosImpactHazardZone::TickSmoke()
{
	UWorld* World = GetWorld();
	for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
	{
		// Walking into the cloud stings too: blinded while inside and a little after leaving it.
		if (*It != SourcePawn && !It->IsEliminated() && !AChaosImpactGameState::AreTeammates(World, SourcePawn, *It)
			&& IsInside(*It, It->GetCapsuleComponent()->GetScaledCapsuleRadius() * 0.5f, 320.0f))
		{
			It->ApplyBlind(ChaosImpactBallTypes::SmokeLingerBlindSeconds);
		}
	}
}

void AChaosImpactHazardZone::BuildSmokePresentation(FRandomStream& Stream)
{
	using namespace ChaosImpactBallTypes;
	ZoneLight->SetVisibility(false);
	const float Radius = GetRadius();
	const float Size = GetVisualScale();
	// A dull pop of grey where it burst, then the cloud: billowing plumes over the whole area, thickest in the middle.
	if (UNiagaraSystem* Pop = LoadEffect(Effects::Explosion))
	{
		if (UNiagaraComponent* Blast = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, Pop,
			GetActorLocation() + FVector(0.0f, 0.0f, BurstHeight * 0.5f), FRotator::ZeroRotator, FVector(0.7f)))
		{
			SetEffectColor(Blast, TEXT("Smoke Color"), FLinearColor(0.42f, 0.41f, 0.47f));
			SetEffectColor(Blast, TEXT("Dirt Color"), FLinearColor(0.35f, 0.34f, 0.4f));
		}
	}
	// Its thrower sees straight through it (on a screen that is theirs alone: one shared split screen shows it to all).
	const APawn* Thrower = SourcePawn;
	const UGameInstance* Game = GetGameInstance();
	const bool bThrowerView = Thrower && Thrower->IsLocallyControlled() && Thrower->IsPlayerControlled()
		&& Game && Game->GetNumLocalPlayers() <= 1;
	const FLinearColor Grey(0.5f, 0.49f, 0.56f, bThrowerView ? 0.3f : 1.0f);
	const auto Plume = [&](const FVector& Where, const float SpriteSize, const float Rate)
	{
		if (UNiagaraComponent* Cloud = AddLoopingEffect(Effects::Smoke, Where))
		{
			// Every plume counts: the effect's budget would otherwise freeze the later ones half-formed.
			Cloud->SetAllowScalability(false);
			FLinearColor Tone = Grey * Stream.FRandRange(0.85f, 1.1f);
			Tone.A = Grey.A;
			SetEffectColor(Cloud, TEXT("Smoke Color"), Tone);
			SetEffectSize(Cloud, TEXT("Sprite Size"), SpriteSize);
			SetEffectSize(Cloud, TEXT("Uniform Sprite Size"), SpriteSize);
			SetEffectFloat(Cloud, TEXT("SpawnRate"), Rate);
		}
	};
	if (bThrowerView)
	{
		// Only where it reaches, as a ring on the ground, and a couple of thin wisps.
		UMaterialInstanceDynamic* RingLook = MakeAdditive(this, FLinearColor(0.72f, 0.7f, 0.85f), 0.6f);
		RingMaterial = RingLook;
		RingMesh = ChaosImpactLightning::CreateComponent(this, SceneRoot, RingLook);
		ChaosImpactIceMeshes::FMeshBuffers Rings;
		ChaosImpactLightning::AppendRing(Rings, FVector(0.0f, 0.0f, 6.0f), Radius, 10.0f, 96);
		ChaosImpactLightning::AppendRing(Rings, FVector(0.0f, 0.0f, 6.0f), Radius * 0.96f, 4.0f, 96);
		ChaosImpactLightning::SetMesh(RingMesh.Get(), Rings);
		Plume(FVector::ZeroVector, 360.0f * Size, 3.0f);
		return;
	}
	// A heavy centre and one ring of plumes out toward the cloud's edge: a few big, slow plumes cover its whole area
	// (many small ones piled up translucent smoke far too thick to draw).
	// (Four plumes round the centre and fewer, slightly larger puffs cover the same area as five did, for about a
	// third less translucent overdraw: smoke is the heaviest thing on screen.)
	Plume(FVector::ZeroVector, 800.0f * Size, 9.0f);
	constexpr int32 Ring = 4;
	for (int32 Index = 0; Index < Ring; ++Index)
	{
		const float Angle = (Index + Stream.FRandRange(-0.2f, 0.2f)) * UE_TWO_PI / Ring;
		const float Distance = Radius * Stream.FRandRange(0.55f, 0.68f);
		Plume(FVector(FMath::Cos(Angle) * Distance, FMath::Sin(Angle) * Distance, 0.0f), Stream.FRandRange(690.0f, 780.0f) * Size, 6.0f);
	}
}

void AChaosImpactHazardZone::BuildBeamPresentation()
{
	// The beam spends itself in a last burst of pink light.
	const FVector Where = GetActorLocation() + FVector(0.0f, 0.0f, BurstHeight);
	ChaosImpactBallTypes::PlayBeamBurst(this, Where, 1.3f * GetVisualScale());
	ZoneLight->SetLightColor(ChaosImpactBallTypes::GetColor(EChaosImpactBallType::Beam));
	ZoneLight->SetRelativeLocation(FVector(0.0f, 0.0f, BurstHeight));
	ZoneLight->SetAttenuationRadius(600.0f);
}

void AChaosImpactHazardZone::BuildSnowPresentation(FRandomStream& Stream)
{
	using namespace ChaosImpactBallTypes;
	ZoneLight->SetVisibility(false);
	const float Scale = BurstScale;
	const FVector Burst = GetActorLocation() + FVector(0.0f, 0.0f, FMath::Max(BurstHeight, 20.0f));
	PlaySnowBurst(this, Burst, FMath::Clamp(Scale, 0.6f, 3.2f));
	// Chunks of packed snow flying apart, bouncing once and melting away; more and bigger for a big snowball.
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UMaterialInstanceDynamic* Snow = MakeSnow(this);
	const int32 ChunkCount = FMath::Clamp(FMath::RoundToInt(8.0f + 6.0f * Scale), 10, 28);
	for (int32 Index = 0; Index < ChunkCount && Sphere; ++Index)
	{
		UStaticMeshComponent* Chunk = NewObject<UStaticMeshComponent>(this);
		Chunk->SetStaticMesh(Sphere);
		Chunk->SetMaterial(0, Snow);
		Chunk->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Chunk->SetGenerateOverlapEvents(false);
		// No shadows: from the high camera a chunk's shadow reads as a dark fleck flung over the floor.
		Chunk->SetCastShadow(false);
		Chunk->SetupAttachment(SceneRoot);
		Chunk->RegisterComponent();
		FSnowChunk& Piece = SnowChunks.AddDefaulted_GetRef();
		Piece.Mesh = Chunk;
		const FVector Out = FVector(Stream.FRandRange(-1.0f, 1.0f), Stream.FRandRange(-1.0f, 1.0f), 0.0f).GetSafeNormal();
		Piece.Start = Burst + Out * 12.0f * Scale;
		Piece.Velocity = Out * Stream.FRandRange(260.0f, 620.0f) * FMath::Sqrt(Scale) + FVector(0.0f, 0.0f, Stream.FRandRange(260.0f, 620.0f));
		Piece.Spin = FRotator(Stream.FRandRange(-400.0f, 400.0f), Stream.FRandRange(-400.0f, 400.0f), 0.0f);
		Piece.Size = Stream.FRandRange(0.07f, 0.16f) * FMath::Clamp(Scale, 0.8f, 2.4f);
	}
	UpdateSnowChunks(0.0f);

	// Soft (FX kit): a puff of powder bursting out low, a white ring of it over the floor and glints of ice in the air.
	AChaosImpactFxKit* Fx = AChaosImpactFxKit::Spawn(this, GetActorLocation());
	if (!Fx)
	{
		return;
	}
	Fx->Scale = FMath::Clamp(Scale, 0.7f, 2.4f);
	Fx->Stream.Initialize(Stream.RandHelper(1 << 20));
	const FLinearColor White(0.96f, 0.98f, 1.0f);
	const FLinearColor Shade(0.68f, 0.78f, 0.92f);
	FChaosImpactFxPiece Ring;
	Ring.Look = EChaosImpactFxLook::SolidRing;
	Ring.Location = FVector(0.0f, 0.0f, 4.0f);
	Ring.Life = 0.4f;
	Ring.SizeFrom = Ring.SizeTo = 420.0f;
	Ring.RingFrom = 0.1f;
	Ring.RingTo = 0.9f;
	Ring.RingWidth = 0.06f;
	Ring.Color = Shade;
	Ring.CoreColor = White;
	Ring.Intensity = 1.0f;
	Ring.Opacity = 0.7f;
	Ring.Attack = 0.05f;
	Ring.ErodeTo = 0.7f;
	Fx->Add(Ring);
	for (int32 Index = 0; Index < 6; ++Index)
	{
		const float Angle = (Index + Fx->Stream.FRandRange(-0.3f, 0.3f)) * UE_TWO_PI / 6.0f;
		const FVector Out(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f);
		FChaosImpactFxPiece Puff;
		Puff.Look = EChaosImpactFxLook::Smoke;
		Puff.Location = Out * 20.0f + FVector(0.0f, 0.0f, 50.0f);
		Puff.Velocity = Out * Fx->Stream.FRandRange(250.0f, 420.0f) + FVector(0.0f, 0.0f, 60.0f);
		Puff.Drag = 2.5f;
		Puff.Life = Fx->Stream.FRandRange(0.7f, 1.0f);
		Puff.SizeFrom = 60.0f;
		Puff.SizeTo = 130.0f;
		Puff.Color = Shade;
		Puff.CoreColor = White;
		Puff.Intensity = 1.0f;
		Puff.Opacity = 0.45f;
		Puff.Attack = 0.08f;
		Puff.ErodeTo = 0.95f;
		Puff.Spin = Fx->Stream.FRandRange(-60.0f, 60.0f);
		Fx->Add(Puff);
	}
	for (int32 Index = 0; Index < 10; ++Index)
	{
		FChaosImpactFxPiece Glint;
		Glint.Location = FVector(0.0f, 0.0f, 50.0f);
		Glint.Velocity = FRotator(Fx->Stream.FRandRange(25.0f, 75.0f), Fx->Stream.FRandRange(0.0f, 360.0f), 0.0f).Vector()
			* Fx->Stream.FRandRange(300.0f, 650.0f);
		Glint.Gravity = 900.0f;
		Glint.Drag = 1.5f;
		Glint.Life = Fx->Stream.FRandRange(0.45f, 0.8f);
		Glint.SizeFrom = 45.0f;
		Glint.SizeTo = 20.0f;
		Glint.Color = FLinearColor(0.5f, 0.75f, 1.0f);
		Glint.CoreColor = White;
		Glint.Intensity = 4.0f;
		Glint.Attack = 0.05f;
		Glint.NoiseAmount = 0.0f;
		Glint.Flicker = 0.8f;
		Fx->Add(Glint);
	}
}

void AChaosImpactHazardZone::ApplyNovaKnockback(AChaosImpactCharacter* Victim) const
{
	if (!IsValid(Victim) || Victim->IsEliminated())
	{
		return;
	}
	FVector Out = (Victim->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
	if (Out.IsNearlyZero())
	{
		Out = FRotator(0.0f, FMath::FRandRange(0.0f, 360.0f), 0.0f).Vector();
	}
	Victim->ApplyBlastKnockback(Out * 750.0f + FVector(0.0f, 0.0f, 620.0f));
}

float AChaosImpactHazardZone::GetNovaFireRadius() const
{
	if (ZoneType != EChaosImpactBallType::Nova)
	{
		return 0.0f;
	}
	return GetRadius() * ChaosImpactNova::GetExplosionSize(ChaosImpactNova::FireStart + GetAge());
}

void AChaosImpactHazardZone::TickNova(const float Age)
{
	UWorld* World = GetWorld();
	const float Reach = GetNovaFireRadius();
	if (!World || Reach <= 1.0f)
	{
		return;
	}
	// The ball of fire is a dome as tall as it is wide: whoever it reaches as it grows (or walks into it) is knocked out.
	for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
	{
		AChaosImpactCharacter* Character = *It;
		if (Character != SourcePawn && !Character->IsEliminated() && !Affected.Contains(Character)
			&& IsInside(Character, Character->GetCapsuleComponent()->GetScaledCapsuleRadius(), Reach, Reach))
		{
			TryApplyLateHit(Character);
		}
	}
	for (TActorIterator<AChaosImpactTrainingTarget> It(World); It; ++It)
	{
		if (!It->IsDefeated() && !Affected.Contains(*It) && IsInside(*It, 50.0f, Reach, Reach))
		{
			Affected.Add(*It);
			UGameplayStatics::ApplyDamage(*It, ChaosImpactBallTypes::GetNovaDamage(BurstScale), GetSourceController(), this, nullptr);
		}
	}
	for (TActorIterator<AChaosImpactCrashBall> It(World); It; ++It)
	{
		AChaosImpactCrashBall* CrashBall = *It;
		if (IsValid(CrashBall) && !Affected.Contains(CrashBall)
			&& IsInside(CrashBall, CrashBall->GetBallRadius(), Reach + CrashBall->GetBallRadius(), Reach))
		{
			Affected.Add(CrashBall);
			CrashBall->ApplySpecialEffectPush(this, SourcePawn, GetActorLocation(), FVector::ZeroVector, 1.35f, false);
		}
	}
}

void AChaosImpactHazardZone::BuildNovaPresentation(FRandomStream& Stream)
{
	using namespace ChaosImpactBallTypes;
	// The nova itself, the size of any ball, standing where it landed: it spins, explodes and falls apart
	// (UpdateNovaPresentation), its ball of fire as wide as the blast (which is what the charge made bigger).
	NovaRig.Build(this, SceneRoot, EChaosImpactNovaRig::NovaFire);
	if (USceneComponent* Model = NovaRig.GetRoot())
	{
		const float ModelScale = NovaModelRadius / ChaosImpactNova::ModelRadius;
		Model->SetRelativeScale3D(FVector(ModelScale));
		// Its lowest point at the ground.
		Model->SetRelativeLocation(FVector(0.0f, 0.0f, -22.0f * ModelScale));
	}
	if (UStaticMeshComponent* Bomb = NovaRig.GetPart(NovaRig.FindNode(TEXT("Bomb"))))
	{
		NovaFireMaterial = ChaosImpactNova::MakeExplosionMaterial(this);
		Bomb->SetMaterial(0, NovaFireMaterial);
		Bomb->SetUsingAbsoluteScale(true);
		Bomb->SetCastShadow(false);
		NovaRig.SetForcedHidden(NovaRig.FindNode(TEXT("Bomb")), true);
	}
	const float Blast = GetRadius();
	// All in the nova's blue, like its ball of fire.
	ZoneLight->SetLightColor(FLinearColor(0.3f, 0.6f, 1.0f));
	ZoneLight->SetAttenuationRadius(Blast * 3.0f + 800.0f);
	ZoneLight->SetIntensity(0.0f);
	// Explosions going off all over inside the ball of fire as it grows, from the centre outward.
	const FVector Base = GetActorLocation();
	const float Size = Blast / 300.0f;
	const int32 BurstCount = FMath::Clamp(FMath::RoundToInt(Blast / 110.0f), 3, 12);
	for (int32 Index = 0; Index < BurstCount; ++Index)
	{
		const float Reach = Blast * Stream.FRandRange(0.2f, 0.85f);
		const float Angle = Stream.FRandRange(0.0f, UE_TWO_PI);
		FNovaBurst& Later = NovaBursts.AddDefaulted_GetRef();
		Later.Location = Base + FVector(FMath::Cos(Angle) * Reach, FMath::Sin(Angle) * Reach, 40.0f);
		Later.At = 0.1f + 1.2f * Reach / Blast + Stream.FRandRange(0.0f, 0.15f);
		Later.Scale = Stream.FRandRange(0.9f, 1.6f) * FMath::Clamp(Size, 0.6f, 1.8f);
	}
	// It lands spinning up.
	ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::NovaCharge, Base, 0.8f, 1.4f);
}

void AChaosImpactHazardZone::PlayNovaKit(const FVector& Center)
{
	// All soft (FX kit), in the nova's blue: a blinding flash, a pillar of light up into the sky, the ground lit under
	// the ball of fire, two shock waves racing out past the edge and sparks flung off.
	AChaosImpactFxKit* Fx = AChaosImpactFxKit::Spawn(this, GetActorLocation());
	if (!Fx)
	{
		return;
	}
	Fx->Stream.Initialize(VisualSeed);
	const float Blast = GetRadius();
	const FVector Middle = Center - GetActorLocation();
	const FLinearColor Blue(0.2f, 0.5f, 1.0f);
	const FLinearColor White(0.85f, 0.95f, 1.0f);
	FChaosImpactFxPiece Flash;
	Flash.Location = Middle;
	Flash.Life = 0.45f;
	Flash.SizeFrom = Blast * 0.5f;
	Flash.SizeTo = Blast * 1.1f;
	Flash.Color = Blue;
	Flash.CoreColor = White;
	Flash.Intensity = 6.0f;
	Flash.Attack = 0.04f;
	Flash.NoiseAmount = 0.25f;
	Fx->Add(Flash);
	FChaosImpactFxPiece Pillar;
	Pillar.Location = FVector(0.0f, 0.0f, 0.0f);
	Pillar.Life = 1.3f;
	Pillar.SizeFrom = Blast * 0.55f;
	Pillar.SizeTo = Blast * 0.3f;
	Pillar.Width = 5.0f;
	Pillar.Taper = 1.0f;
	Pillar.Color = Blue;
	Pillar.CoreColor = White;
	Pillar.Intensity = 3.5f;
	Pillar.Attack = 0.05f;
	Pillar.NoiseSpeed = 1.6f;
	Pillar.NoiseAmount = 0.5f;
	Fx->Add(Pillar);
	FChaosImpactFxPiece Lit;
	Lit.Look = EChaosImpactFxLook::GroundGlow;
	Lit.Location = FVector(0.0f, 0.0f, 4.0f);
	Lit.Life = 2.2f;
	Lit.SizeFrom = Blast * 2.3f;
	Lit.SizeTo = Blast * 1.6f;
	Lit.Color = FLinearColor(0.1f, 0.35f, 1.0f);
	Lit.CoreColor = White;
	Lit.Intensity = 1.8f;
	Lit.Attack = 0.03f;
	Lit.Hold = 0.25f;
	Lit.NoiseScale = 2.0f;
	Lit.NoiseSpeed = 0.8f;
	Lit.NoiseAmount = 0.6f;
	Lit.Flicker = 0.2f;
	Fx->Add(Lit);
	FChaosImpactFxPiece Wave;
	Wave.Look = EChaosImpactFxLook::Ring;
	Wave.Location = FVector(0.0f, 0.0f, 10.0f);
	Wave.Life = 0.9f;
	Wave.SizeFrom = Wave.SizeTo = Blast * 3.4f;
	Wave.RingFrom = 0.05f;
	Wave.RingTo = 0.95f;
	Wave.RingWidth = 0.045f;
	Wave.Color = Blue;
	Wave.CoreColor = White;
	Wave.Intensity = 3.5f;
	Wave.Attack = 0.03f;
	Wave.ErodeTo = 0.75f;
	Wave.NoiseScale = 3.0f;
	Fx->Add(Wave);
	FChaosImpactFxPiece Second = Wave;
	Second.Delay = 0.25f;
	Second.Life = 1.2f;
	Second.RingTo = 0.75f;
	Second.RingWidth = 0.03f;
	Second.Intensity = 2.0f;
	Fx->Add(Second);
	for (int32 Index = 0; Index < 22; ++Index)
	{
		FChaosImpactFxPiece Spark;
		Spark.Look = EChaosImpactFxLook::Streak;
		Spark.Location = Middle;
		Spark.Velocity = FRotator(Fx->Stream.FRandRange(-10.0f, 70.0f), Fx->Stream.FRandRange(0.0f, 360.0f), 0.0f).Vector()
			* Fx->Stream.FRandRange(1200.0f, 2600.0f) * FMath::Clamp(Blast / 400.0f, 0.7f, 1.5f);
		Spark.Gravity = 2000.0f;
		Spark.Drag = 1.5f;
		Spark.Life = Fx->Stream.FRandRange(0.4f, 0.8f);
		Spark.SizeFrom = Fx->Stream.FRandRange(110.0f, 180.0f);
		Spark.SizeTo = 40.0f;
		Spark.Width = 8.0f;
		Spark.Color = Blue;
		Spark.CoreColor = White;
		Spark.Intensity = 6.0f;
		Spark.Attack = 0.02f;
		Spark.ErodeTo = 0.0f;
		Spark.Flicker = 0.4f;
		Fx->Add(Spark);
	}
}

void AChaosImpactHazardZone::UpdateNovaPresentation(const float Age)
{
	using namespace ChaosImpactBallTypes;
	const float AnimTime = ChaosImpactNova::FireStart + Age;
	const float Blast = GetRadius();
	const FVector Base = GetActorLocation();
	const float ModelScale = NovaModelRadius / ChaosImpactNova::ModelRadius;
	const FVector Center = Base + FVector(0.0f, 0.0f, (113.9f - 22.0f) * ModelScale);

	// The model: its own animation, but its gears and rings spin up as soon as it lands (until the spinning set takes
	// over as the ball of fire shrinks).
	const float Spin = Age < ChaosImpactNova::FireShrinkAt - ChaosImpactNova::FireStart
		? -720.0f * (Age - 0.4f * (1.0f - FMath::Exp(-Age / 0.4f))) : 0.0f;
	NovaRig.SetExtraRotation(NovaRig.FindNode(TEXT("polySurface25")), FVector3f(0.0f, Spin, 0.0f));
	NovaRig.SetExtraRotation(NovaRig.FindNode(TEXT("polySurface29")), FVector3f(0.0f, 0.0f, Spin * 0.8f));
	NovaRig.SetExtraRotation(NovaRig.FindNode(TEXT("polySurface30")), FVector3f(0.0f, Spin * 1.2f, 0.0f));
	// Once fallen apart, the pieces lie where they flew and turn see-through until gone (they keep their size).
	NovaRig.SetFade(1.0f - FMath::Clamp((Age - NovaBlastSeconds) / NovaPieceFadeSeconds, 0.0f, 1.0f));
	NovaRig.Pose(FMath::Min(AnimTime, ChaosImpactNova::FireEnd));

	// The ball of fire: grows, holds, shrinks away (its edge is where it hurts: GetNovaFireRadius).
	const float FireSize = ChaosImpactNova::GetExplosionSize(AnimTime);
	if (UStaticMeshComponent* Bomb = NovaRig.GetPart(NovaRig.FindNode(TEXT("Bomb"))))
	{
		const bool bFire = FireSize > 0.002f;
		if (Bomb->IsVisible() != bFire)
		{
			Bomb->SetVisibility(bFire);
		}
		if (bFire)
		{
			Bomb->SetWorldLocation(Center);
			Bomb->SetWorldScale3D(FVector(Blast * FireSize * (1.0f + 0.012f * FMath::Sin(Age * 31.0f))));
			if (UMaterialInstanceDynamic* FireLook = NovaFireMaterial.Get())
			{
				// Yellow-hot at the heart as it bursts, deepening to orange and red (and smoke) as it shrinks and fades.
				const float SinceBurst = Age - NovaExplodeAfter;
				FireLook->SetScalarParameterValue(TEXT("Heat"), FMath::Clamp(0.75f - SinceBurst * 0.1f, 0.1f, 0.75f));
				FireLook->SetScalarParameterValue(TEXT("Glow"), 3.5f);
				FireLook->SetScalarParameterValue(TEXT("Fade"), AnimTime < ChaosImpactNova::FireShrinkAt ? 1.0f : FMath::Sqrt(FireSize));
			}
		}
	}

	// The moment it goes off: a flash, the ground lit up, shock waves, explosions all over inside, the camera shaking.
	const float Burst = Age - NovaExplodeAfter;
	if (Burst >= 0.0f && !bNovaExploded)
	{
		bNovaExploded = true;
		const float Size = Blast / 300.0f;
		const auto Play = [this](const TCHAR* Path, const FVector& Where, const float Scale)
		{
			if (UNiagaraSystem* System = LoadEffect(Path))
			{
				UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, System, Where, FRotator::ZeroRotator, FVector(Scale), true, true,
					ENCPoolMethod::None, true);
			}
		};
		// (Its smoke is big: kept small enough not to bury the arena.)
		Play(Effects::BigExplosion, Center, Size * 0.85f);
		Play(Effects::SparkBurst, Center, Size * 2.5f);
		PlayNovaKit(Center);
		ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::NovaBlast, Center);
		if (UWorld* World = GetWorld())
		{
			for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
			{
				const APlayerController* Player = It->Get();
				AChaosImpactCharacter* Character = Player && Player->IsLocalController()
					? Cast<AChaosImpactCharacter>(Player->GetPawn()) : nullptr;
				if (Character)
				{
					const float Distance = static_cast<float>(FVector::Dist(Character->GetActorLocation(), Base));
					const float Strength = FMath::Clamp(1.0f - Distance / (Blast * 3.5f + 1500.0f), 0.0f, 1.0f)
						* FMath::Clamp(Blast / 800.0f, 0.35f, 1.4f);
					Character->AddCameraShake(Strength, 1.6f);
				}
			}
		}
	}
	if (Burst >= 0.0f)
	{
		for (FNovaBurst& Later : NovaBursts)
		{
			if (!Later.bPlayed && Burst >= Later.At)
			{
				Later.bPlayed = true;
				if (UNiagaraSystem* System = LoadEffect(Effects::MediumExplosion))
				{
					UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, System, Later.Location, FRotator::ZeroRotator,
						FVector(Later.Scale), true, true, ENCPoolMethod::None, true);
				}
			}
		}
	}
	ZoneLight->SetRelativeLocation(FVector(0.0f, 0.0f, (113.9f - 22.0f) * ModelScale + 150.0f));
	ZoneLight->SetIntensity(Burst >= 0.0f ? 300000.0f * FMath::Exp(-Burst * 3.0f) + 60000.0f * FireSize : 0.0f);

	// It falls apart with a clatter (no smoke: the pieces are seen flying).
	if (AnimTime >= ChaosImpactNova::FireBreakAt && !bNovaBroken)
	{
		bNovaBroken = true;
		ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::CharIceShatter, Center, 1.0f, 0.6f);
	}
}

void AChaosImpactHazardZone::UpdateSnowChunks(const float Age)
{
	const float Gravity = GetWorld() ? GetWorld()->GetGravityZ() : -980.0f;
	const float Ground = GetActorLocation().Z;
	// Melting away over the last part.
	const float Melt = FMath::Clamp((SnowBurstSeconds - Age) / 0.45f, 0.0f, 1.0f);
	for (const FSnowChunk& Piece : SnowChunks)
	{
		UStaticMeshComponent* Chunk = Piece.Mesh.Get();
		if (!Chunk)
		{
			continue;
		}
		FVector At = Piece.Start + Piece.Velocity * Age + FVector(0.0f, 0.0f, 0.5f * Gravity * Age * Age);
		const float Radius = 50.0f * Piece.Size;
		if (At.Z < Ground + Radius)
		{
			// Lands and slides to a stop instead of sinking into the floor.
			At.Z = Ground + Radius;
		}
		Chunk->SetWorldLocationAndRotation(At, Piece.Spin * Age);
		Chunk->SetWorldScale3D(FVector(Piece.Size * FMath::Max(Melt, 0.01f)));
		Chunk->SetVisibility(Melt > 0.01f);
	}
}

void AChaosImpactHazardZone::BuildIcePresentation(FRandomStream& Stream)
{
	using namespace ChaosImpactBallTypes;
	using namespace ChaosImpactIceMeshes;
	ZoneLight->SetLightColor(FLinearColor(0.55f, 0.8f, 1.0f));
	// (Not the pack's shatter here: its shards lie flat and, this big, read as a hard-edged white star over the area.)

	// Frozen ground: soft frost over the area (FX kit, below), and cracks running out from the impact.
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
		FMeshBuffers Cracks;
		constexpr int32 CrackCount = 11;
		for (int32 Index = 0; Index < CrackCount; ++Index)
		{
			const float Angle = (Index + Stream.FRandRange(-0.35f, 0.35f)) * UE_TWO_PI / CrackCount;
			const FVector Direction(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f);
			AppendCrack(Cracks, FTransform(FVector(0.0f, 0.0f, 3.6f)), Direction * Stream.FRandRange(10.0f, 40.0f),
				Direction, GetRadius() * Stream.FRandRange(0.45f, 0.8f), Stream.FRandRange(2.0f, 3.5f), Stream);
		}
		AddSheetLayer(Cracks, 0.55f, 0.3f, FLinearColor(0.85f, 0.95f, 1.0f));
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
	const float Small = FMath::Clamp(GetVisualScale(), 0.45f, 1.0f);
	AddCrystalGroup(FVector::ZeroVector, 7, 30.0f * Small, 9.0f * Small, 16.0f * Small, 55.0f * Small, 115.0f * Small, 5.0f, 38.0f, 0.0f);
	constexpr int32 Groups = 9;
	for (int32 Index = 0; Index < Groups; ++Index)
	{
		const float Angle = (Index + Stream.FRandRange(-0.3f, 0.3f)) * UE_TWO_PI / Groups;
		const float Distance = GetRadius() * Stream.FRandRange(0.45f, 0.9f);
		AddCrystalGroup(FVector(FMath::Cos(Angle) * Distance, FMath::Sin(Angle) * Distance, 0.0f),
			Stream.RandRange(2, 4), 16.0f * Small, 5.0f * Small, 10.0f * Small, 22.0f * Small, 58.0f * Small, 15.0f, 50.0f, 0.04f + Distance / GetRadius() * 0.16f);
	}

	// No smoke plume here: the pack's plume is sized for scenery and buries the arena from this camera.

	// Soft (FX kit): the frost over the whole slippery area, glinting, its edge marked by a faint cold rim; the burst
	// itself a cold flash, a shock ring, shards of ice flung out and a low mist rolling off.
	AChaosImpactFxKit* Fx = AChaosImpactFxKit::Spawn(this, GetActorLocation());
	if (!Fx)
	{
		return;
	}
	Fx->Stream.Initialize(Stream.RandHelper(1 << 20));
	const float Radius = GetRadius();
	const float Lasting = GetActiveSeconds() + 0.6f;
	const FLinearColor Cold(0.3f, 0.65f, 1.0f);
	const FLinearColor White(0.88f, 0.97f, 1.0f);
	const auto Hold = [Lasting](FChaosImpactFxPiece& Piece)
	{
		Piece.Life = Lasting;
		Piece.Attack = 0.18f / Lasting;
		Piece.Hold = FMath::Max(0.0f, 1.0f - 0.7f / Lasting);
	};
	FChaosImpactFxPiece Frost;
	Frost.Look = EChaosImpactFxLook::GroundFlame;
	Frost.Location = FVector(0.0f, 0.0f, 2.0f);
	Hold(Frost);
	Frost.SizeFrom = Radius * 1.75f;
	Frost.SizeTo = Radius * 2.05f;
	Frost.Color = FLinearColor(0.42f, 0.7f, 0.95f);
	Frost.CoreColor = FLinearColor(0.86f, 0.95f, 1.0f);
	Frost.Intensity = 1.0f;
	Frost.Opacity = 0.5f;
	Frost.NoiseScale = 2.4f;
	Frost.NoiseSpeed = 0.02f;
	Frost.NoiseAmount = 0.55f;
	Frost.Softness = 0.45f;
	Frost.ErodeTo = 0.9f;
	Fx->Add(Frost);
	FChaosImpactFxPiece Sheen;
	Sheen.Look = EChaosImpactFxLook::GroundGlow;
	Sheen.Location = FVector(0.0f, 0.0f, 3.0f);
	Hold(Sheen);
	Sheen.SizeFrom = Sheen.SizeTo = Radius * 1.9f;
	Sheen.Color = Cold;
	Sheen.CoreColor = White;
	Sheen.Intensity = 0.7f;
	Sheen.NoiseScale = 3.0f;
	Sheen.NoiseSpeed = 0.25f;
	Sheen.NoiseAmount = 0.85f;
	Sheen.Softness = 0.6f;
	Sheen.ErodeTo = 0.9f;
	Fx->Add(Sheen);
	FChaosImpactFxPiece Rim;
	Rim.Look = EChaosImpactFxLook::Ring;
	Rim.Location = FVector(0.0f, 0.0f, 4.0f);
	Hold(Rim);
	Rim.SizeFrom = Rim.SizeTo = Radius * 2.0f;
	Rim.RingFrom = Rim.RingTo = 0.94f;
	Rim.RingWidth = 0.035f;
	Rim.Color = Cold;
	Rim.CoreColor = White;
	Rim.Intensity = 1.1f;
	Rim.NoiseScale = 3.0f;
	Rim.NoiseAmount = 0.55f;
	Rim.ErodeTo = 0.5f;
	Fx->Add(Rim);
	FChaosImpactFxPiece Flash;
	Flash.Location = FVector(0.0f, 0.0f, 60.0f);
	Flash.Life = 0.2f;
	Flash.SizeFrom = 160.0f;
	Flash.SizeTo = 360.0f;
	Flash.Color = Cold;
	Flash.CoreColor = White;
	Flash.Intensity = 6.0f;
	Flash.Attack = 0.05f;
	Flash.NoiseAmount = 0.2f;
	Fx->Add(Flash);
	FChaosImpactFxPiece Shock;
	Shock.Look = EChaosImpactFxLook::Ring;
	Shock.Location = FVector(0.0f, 0.0f, 7.0f);
	Shock.Life = 0.42f;
	Shock.SizeFrom = Shock.SizeTo = Radius * 2.4f;
	Shock.RingFrom = 0.1f;
	Shock.RingTo = 0.93f;
	Shock.RingWidth = 0.05f;
	Shock.Color = Cold;
	Shock.CoreColor = White;
	Shock.Intensity = 3.5f;
	Shock.Attack = 0.04f;
	Shock.ErodeTo = 0.7f;
	Fx->Add(Shock);
	for (int32 Index = 0; Index < 16; ++Index)
	{
		FChaosImpactFxPiece Shard;
		Shard.Look = EChaosImpactFxLook::Streak;
		Shard.Location = FVector(0.0f, 0.0f, 50.0f);
		Shard.Velocity = FRotator(Fx->Stream.FRandRange(25.0f, 70.0f), Fx->Stream.FRandRange(0.0f, 360.0f), 0.0f).Vector()
			* Fx->Stream.FRandRange(700.0f, 1400.0f);
		Shard.Gravity = 2400.0f;
		Shard.Drag = 1.2f;
		Shard.Life = Fx->Stream.FRandRange(0.4f, 0.75f);
		Shard.SizeFrom = Fx->Stream.FRandRange(60.0f, 100.0f);
		Shard.SizeTo = 30.0f;
		Shard.Width = 7.0f;
		Shard.Color = Cold;
		Shard.CoreColor = White;
		Shard.Intensity = 5.0f;
		Shard.Attack = 0.02f;
		Shard.ErodeTo = 0.0f;
		Shard.Flicker = 0.3f;
		Fx->Add(Shard);
	}
	for (int32 Index = 0; Index < 6; ++Index)
	{
		const float Angle = (Index + Fx->Stream.FRandRange(-0.3f, 0.3f)) * UE_TWO_PI / 6.0f;
		const FVector Out(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f);
		FChaosImpactFxPiece Mist;
		Mist.Look = EChaosImpactFxLook::Smoke;
		Mist.Location = Out * Radius * 0.2f + FVector(0.0f, 0.0f, 40.0f);
		Mist.Velocity = Out * Radius * 0.9f;
		Mist.Drag = 1.6f;
		Mist.Life = Fx->Stream.FRandRange(1.2f, 1.6f);
		Mist.SizeFrom = Radius * 0.3f;
		Mist.SizeTo = Radius * 0.65f;
		Mist.Color = FLinearColor(0.55f, 0.72f, 0.9f);
		Mist.CoreColor = White;
		Mist.Intensity = 1.0f;
		Mist.Opacity = 0.2f;
		Mist.Attack = 0.1f;
		Mist.ErodeTo = 0.95f;
		Mist.Spin = Fx->Stream.FRandRange(-30.0f, 30.0f);
		Fx->Add(Mist);
	}
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
			GetActorLocation() + FVector(0.0f, 0.0f, FMath::Max(BurstHeight, 40.0f)), FRotator::ZeroRotator, FVector(2.4f * GetVisualScale()));
	}
	AddLoopingEffect(Effects::Electricity, FVector(0.0f, 0.0f, 30.0f));
	UMaterialInstanceDynamic* StrikeLook = MakeAdditive(this, FLinearColor(1.0f, 0.95f, 0.7f), 6.0f);
	StrikeMaterial = StrikeLook;
	StrikeMesh = ChaosImpactLightning::CreateComponent(this, SceneRoot, StrikeLook);
	UMaterialInstanceDynamic* ArcLook = MakeAdditive(this, FLinearColor(1.0f, 0.8f, 0.22f), 3.5f);
	ArcMaterial = ArcLook;
	ArcMesh = ChaosImpactLightning::CreateComponent(this, SceneRoot, ArcLook);
	RebuildThunderBolts(0);

	// Round the bolts, all soft (FX kit; no glass dome or hard ring): the strike's white flash, the ground lit where it
	// struck and scorched after, a shock wave racing out, sparks thrown off and the area crackling for a moment after.
	AChaosImpactFxKit* Fx = AChaosImpactFxKit::Spawn(this, GetActorLocation());
	if (!Fx)
	{
		return;
	}
	Fx->Stream.Initialize(VisualSeed);
	const float Radius = GetRadius();
	const FLinearColor Yellow(1.0f, 0.72f, 0.12f);
	const FLinearColor White(1.0f, 0.97f, 0.85f);
	const FVector Strike(0.0f, 0.0f, FMath::Max(BurstHeight, 30.0f) * 0.3f + 40.0f);
	FChaosImpactFxPiece Flash;
	Flash.Location = Strike;
	Flash.Life = 0.14f;
	Flash.SizeFrom = 180.0f;
	Flash.SizeTo = 400.0f;
	Flash.Color = Yellow;
	Flash.CoreColor = White;
	Flash.Intensity = 6.0f;
	Flash.Attack = 0.04f;
	Flash.NoiseAmount = 0.2f;
	Fx->Add(Flash);
	FChaosImpactFxPiece Lit;
	Lit.Look = EChaosImpactFxLook::GroundGlow;
	Lit.Location = FVector(0.0f, 0.0f, 4.0f);
	Lit.Life = 1.1f;
	Lit.SizeFrom = Radius * 1.3f;
	Lit.SizeTo = Radius * 1.6f;
	Lit.Color = FLinearColor(1.0f, 0.55f, 0.05f);
	Lit.CoreColor = White;
	Lit.Intensity = 2.6f;
	Lit.Attack = 0.03f;
	Lit.ErodeFrom = 0.1f;
	Lit.ErodeTo = 0.8f;
	Lit.NoiseScale = 2.6f;
	Lit.NoiseSpeed = 1.5f;
	Lit.NoiseAmount = 0.8f;
	Lit.Flicker = 0.6f;
	Fx->Add(Lit);
	FChaosImpactFxPiece Scorch;
	Scorch.Look = EChaosImpactFxLook::GroundFlame;
	Scorch.Location = FVector(0.0f, 0.0f, 2.0f);
	Scorch.Life = 2.6f;
	Scorch.SizeFrom = Radius * 0.55f;
	Scorch.SizeTo = Radius * 0.65f;
	Scorch.Color = FLinearColor(0.02f, 0.015f, 0.01f);
	Scorch.CoreColor = FLinearColor(0.08f, 0.05f, 0.02f);
	Scorch.Intensity = 1.0f;
	Scorch.Opacity = 0.5f;
	Scorch.Attack = 0.02f;
	Scorch.ErodeFrom = 0.25f;
	Scorch.ErodeTo = 0.8f;
	Scorch.NoiseScale = 3.0f;
	Scorch.NoiseSpeed = 0.02f;
	Scorch.NoiseAmount = 0.9f;
	Scorch.Softness = 0.6f;
	Fx->Add(Scorch);
	FChaosImpactFxPiece Shock;
	Shock.Look = EChaosImpactFxLook::Ring;
	Shock.Location = FVector(0.0f, 0.0f, 8.0f);
	Shock.Life = 0.4f;
	Shock.SizeFrom = Shock.SizeTo = Radius * 2.3f;
	Shock.RingFrom = 0.08f;
	Shock.RingTo = 0.94f;
	Shock.RingWidth = 0.05f;
	Shock.Color = Yellow;
	Shock.CoreColor = White;
	Shock.Intensity = 4.0f;
	Shock.Attack = 0.04f;
	Shock.ErodeTo = 0.7f;
	Shock.NoiseScale = 3.0f;
	Fx->Add(Shock);
	FChaosImpactFxPiece Echo = Shock;
	Echo.Delay = 0.12f;
	Echo.Life = 0.45f;
	Echo.RingTo = 0.7f;
	Echo.RingWidth = 0.03f;
	Echo.Intensity = 2.0f;
	Fx->Add(Echo);
	for (int32 Index = 0; Index < 18; ++Index)
	{
		FChaosImpactFxPiece Spark;
		Spark.Look = EChaosImpactFxLook::Streak;
		Spark.Location = Strike;
		Spark.Velocity = FRotator(Fx->Stream.FRandRange(10.0f, 70.0f), Fx->Stream.FRandRange(0.0f, 360.0f), 0.0f).Vector()
			* Fx->Stream.FRandRange(900.0f, 2000.0f);
		Spark.Gravity = 2600.0f;
		Spark.Drag = 2.0f;
		Spark.Life = Fx->Stream.FRandRange(0.25f, 0.55f);
		Spark.SizeFrom = Fx->Stream.FRandRange(80.0f, 140.0f);
		Spark.SizeTo = 20.0f;
		Spark.Width = 6.0f;
		Spark.Color = Yellow;
		Spark.CoreColor = White;
		Spark.Intensity = 7.0f;
		Spark.Attack = 0.02f;
		Spark.ErodeTo = 0.0f;
		Spark.Flicker = 0.7f;
		Fx->Add(Spark);
	}
	// The ground still crackling: brief pinpoints of light jumping about the area.
	FChaosImpactFxPiece Crackle;
	Crackle.Location = FVector(0.0f, 0.0f, 12.0f);
	Crackle.Life = 0.09f;
	Crackle.SizeFrom = 40.0f;
	Crackle.SizeTo = 90.0f;
	Crackle.Color = Yellow;
	Crackle.CoreColor = White;
	Crackle.Intensity = 5.0f;
	Crackle.Attack = 0.1f;
	Crackle.NoiseAmount = 0.6f;
	Crackle.Flicker = 0.8f;
	Fx->AddEmitter(Crackle, 22.0f, 0.9f, Radius * 0.85f, FVector::ZeroVector, 0.4f);
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
			const FVector End(Stream.FRandRange(-0.6f, 0.6f) * GetRadius(), Stream.FRandRange(-0.6f, 0.6f) * GetRadius(), 0.0f);
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
				Out * GetRadius() * Stream.FRandRange(0.7f, 1.1f) + FVector(0.0f, 0.0f, 6.0f), 10.0f, FVector::UpVector, Stream, 2);
		}
		// ...and a few leaping up from the ground into the strike.
		for (int32 Index = 0; Index < 3; ++Index)
		{
			const float Angle = Stream.FRand() * UE_TWO_PI;
			const FVector From = FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * Stream.FRandRange(80.0f, GetRadius() * 0.85f);
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
	ZoneLight->SetRelativeLocation(FVector(0.0f, 0.0f, 220.0f));
	ZoneLight->SetIntensity(Age < 0.6f ? 90000.0f * Crackle * FMath::Exp(-Age * 3.0f) : 0.0f);
}

void AChaosImpactHazardZone::BuildBlackHolePresentation(FRandomStream& Stream)
{
	using namespace ChaosImpactBallTypes;
	ZoneLight->SetLightColor(FLinearColor(0.55f, 0.2f, 1.0f));
	// The event horizon: a black sphere. Everything round it is drawn soft (FX kit): no hard rings or glass spheres.
	AddZoneMesh(CoreSphere, true, MakeEmissive(this, FLinearColor(0.0f, 0.0f, 0.0f), 0.0f));
	constexpr int32 MoteCount = 16;
	for (int32 Index = 0; Index < MoteCount; ++Index)
	{
		FChaosImpactZoneMesh& Mote = Motes.AddDefaulted_GetRef();
		AddZoneMesh(Mote, true, MakeAdditive(this, Stream.FRand() < 0.5f
			? FLinearColor(0.85f, 0.25f, 1.0f) : FLinearColor(0.45f, 0.2f, 1.0f), 0.0f));
		Mote.Angle = Stream.FRand() * UE_TWO_PI;
		Mote.Radius = GetRadius() * Stream.FRandRange(0.25f, 1.0f);
		Mote.Height = Stream.FRandRange(10.0f, 230.0f);
		Mote.Seed = Stream.FRandRange(0.7f, 1.4f);
	}

	AChaosImpactFxKit* Fx = AChaosImpactFxKit::Spawn(this, GetActorLocation());
	if (!Fx)
	{
		return;
	}
	Fx->Stream.Initialize(Stream.RandHelper(1 << 20));
	const float Radius = GetRadius();
	const float Open = GetActiveSeconds();
	const FVector Core(0.0f, 0.0f, BlackHoleCoreHeight);
	const FLinearColor Magenta(0.95f, 0.25f, 1.0f);
	const FLinearColor Violet(0.38f, 0.12f, 1.0f);
	const FLinearColor Pale(0.95f, 0.8f, 1.0f);
	// Every lasting piece holds steady while it is open and fades in its last moments.
	const auto Lasting = [&](FChaosImpactFxPiece& Piece)
	{
		Piece.Life = Open + 0.35f;
		Piece.Attack = 0.25f / Piece.Life;
		Piece.Hold = FMath::Max(0.0f, 1.0f - 0.45f / Piece.Life);
	};

	// Opening: a violet flash and a shock ring thrown out over the ground.
	FChaosImpactFxPiece Flash;
	Flash.Location = Core;
	Flash.Life = 0.22f;
	Flash.SizeFrom = 200.0f;
	Flash.SizeTo = 520.0f;
	Flash.Color = Violet;
	Flash.CoreColor = Pale;
	Flash.Intensity = 7.0f;
	Flash.Attack = 0.05f;
	Flash.NoiseAmount = 0.2f;
	Fx->Add(Flash);
	FChaosImpactFxPiece Shock;
	Shock.Look = EChaosImpactFxLook::Ring;
	Shock.Location = FVector(0.0f, 0.0f, 6.0f);
	Shock.Life = 0.5f;
	Shock.SizeFrom = Shock.SizeTo = Radius * 2.2f;
	Shock.RingFrom = 0.1f;
	Shock.RingTo = 0.92f;
	Shock.RingWidth = 0.06f;
	Shock.Color = Violet;
	Shock.CoreColor = Pale;
	Shock.Intensity = 3.0f;
	Shock.Attack = 0.05f;
	Shock.ErodeTo = 0.7f;
	Fx->Add(Shock);

	// The well: the ground inside its reach sinks into a dark violet hollow, deepest in the middle.
	FChaosImpactFxPiece Well;
	Well.Look = EChaosImpactFxLook::GroundFlame;
	Well.Location = FVector(0.0f, 0.0f, 2.0f);
	Lasting(Well);
	Well.SizeFrom = Radius * 1.4f;
	Well.SizeTo = Radius * 2.0f;
	Well.Color = FLinearColor(0.05f, 0.0f, 0.1f);
	Well.CoreColor = FLinearColor(0.0f, 0.0f, 0.0f);
	Well.Intensity = 1.0f;
	Well.Opacity = 0.5f;
	Well.Softness = 1.0f;
	Well.NoiseAmount = 0.35f;
	Well.NoiseSpeed = 0.15f;
	Well.Spin = 40.0f;
	Fx->Add(Well);
	// (No ring at the edge of its reach: wider than the view, it ran off the screen's edges. The whirl fades out
	// softly at its rim instead.)
	// The pull: a whirlpool of light arms twisting into the core over the whole reach, streaming inward, and a
	// tighter, brighter one round the core itself turning faster (a pinwheel being sucked down).
	FChaosImpactFxPiece Whirl;
	Whirl.Look = EChaosImpactFxLook::Spiral;
	Whirl.Location = FVector(0.0f, 0.0f, 8.0f);
	Lasting(Whirl);
	Whirl.SizeFrom = Radius * 1.2f;
	Whirl.SizeTo = Radius * 2.0f;
	Whirl.Color = Violet;
	Whirl.CoreColor = Magenta;
	Whirl.Intensity = 1.6f;
	Whirl.Arms = 5.0f;
	Whirl.Twist = 1.8f;
	Whirl.Swirl = 0.55f;
	Whirl.Sharpness = 3.0f;
	Whirl.NoiseScale = 1.6f;
	Whirl.NoiseAmount = 0.6f;
	Whirl.ErodeFrom = 0.1f;
	Whirl.ErodeTo = 0.25f;
	Fx->Add(Whirl);
	FChaosImpactFxPiece Vortex = Whirl;
	Vortex.Location = Core - FVector(0.0f, 0.0f, 20.0f);
	Vortex.SizeFrom = 260.0f;
	Vortex.SizeTo = 680.0f;
	Vortex.Color = Magenta;
	Vortex.CoreColor = FLinearColor(1.0f, 0.75f, 1.0f);
	Vortex.Intensity = 2.6f;
	Vortex.Arms = 3.0f;
	Vortex.Twist = 2.4f;
	Vortex.Swirl = 1.1f;
	Vortex.Sharpness = 2.2f;
	Fx->Add(Vortex);
	// Round the black core: a thin hot ring of light (it bends the light round it) and a soft violet halo.
	FChaosImpactFxPiece Photon;
	Photon.Look = EChaosImpactFxLook::AirRing;
	Photon.Location = Core;
	Lasting(Photon);
	Photon.SizeFrom = 120.0f;
	Photon.SizeTo = 300.0f;
	Photon.RingFrom = Photon.RingTo = 0.72f;
	Photon.RingWidth = 0.07f;
	Photon.Color = Magenta;
	Photon.CoreColor = Pale;
	Photon.Intensity = 6.0f;
	Photon.NoiseScale = 3.0f;
	Photon.NoiseSpeed = 1.2f;
	Photon.NoiseAmount = 0.35f;
	Photon.Spin = 200.0f;
	Photon.Flicker = 0.15f;
	Fx->Add(Photon);
	FChaosImpactFxPiece Halo;
	Halo.Location = Core;
	Lasting(Halo);
	Halo.SizeFrom = 260.0f;
	Halo.SizeTo = 620.0f;
	Halo.Color = Violet;
	Halo.CoreColor = Magenta;
	Halo.Intensity = 1.4f;
	Halo.Softness = 1.0f;
	Halo.NoiseAmount = 0.5f;
	Halo.NoiseSpeed = 0.6f;
	Halo.Spin = -60.0f;
	Fx->Add(Halo);
	// Sparks streaming in from the rim.
	FChaosImpactFxPiece Drawn;
	Drawn.Look = EChaosImpactFxLook::Streak;
	Drawn.Life = 0.7f;
	Drawn.SizeFrom = 40.0f;
	Drawn.SizeTo = 110.0f;
	Drawn.Width = 6.0f;
	Drawn.Color = Magenta;
	Drawn.CoreColor = Pale;
	Drawn.Intensity = 4.0f;
	Drawn.Attack = 0.3f;
	Drawn.ErodeTo = 0.0f;
	for (int32 Index = 0; Index < 14; ++Index)
	{
		const float Angle = Stream.FRandRange(0.0f, UE_TWO_PI);
		const FVector Out(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f);
		FChaosImpactFxPiece Spark = Drawn;
		Spark.Location = Out * Radius * Stream.FRandRange(0.7f, 1.0f) + FVector(0.0f, 0.0f, Stream.FRandRange(20.0f, 160.0f));
		Spark.Velocity = (Core - Spark.Location) / Spark.Life;
		Spark.Delay = Stream.FRandRange(0.1f, Open * 0.8f);
		Fx->Add(Spark);
	}
}

void AChaosImpactHazardZone::UpdateBlackHolePresentation(const float Age)
{
	const float Delta = FMath::Clamp(Age - LastPresentationAge, 0.0f, 0.1f);
	LastPresentationAge = Age;
	const FVector Base = GetActorLocation();
	const FVector Core = Base + FVector(0.0f, 0.0f, BlackHoleCoreHeight);
	const bool bOpen = Age < GetActiveSeconds();
	// Bursts open with a small overshoot, then collapses faster and faster when its time is up.
	const float CollapseT = FMath::Clamp((Age - GetActiveSeconds()) / 0.4f, 0.0f, 1.0f);
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
	if (UProceduralMeshComponent* Ring = RingMesh.Get())
	{
		// The edge of its reach, and three rings closing in on the centre, thin as they start and end.
		ChaosImpactIceMeshes::FMeshBuffers Rings;
		if (Reach > 0.01f)
		{
			ChaosImpactLightning::AppendRing(Rings, FVector(0.0f, 0.0f, 6.0f), GetRadius() * Reach, 12.0f * (0.8f + 0.2f * Pulse));
			for (int32 Index = 0; bOpen && Index < 3; ++Index)
			{
				const float Phase = FMath::Frac(Age * 0.9f + Index / 3.0f);
				ChaosImpactLightning::AppendRing(Rings, FVector(0.0f, 0.0f, 8.0f), GetRadius() * (1.0f - Phase) * Reach,
					16.0f * FMath::Sin(Phase * UE_PI));
			}
		}
		ChaosImpactLightning::SetMesh(Ring, Rings);
		SetIntensity(RingMaterial.Get(), 1.1f * Reach);
	}
	// Motes of light spiral in, faster as they near the core, and start again from the rim.
	for (FChaosImpactZoneMesh& Mote : Motes)
	{
		const float Closeness = 1.0f - FMath::Clamp(Mote.Radius / GetRadius(), 0.0f, 1.0f);
		if (bOpen)
		{
			Mote.Angle += (1.2f + 5.0f * Closeness) * Mote.Seed * Delta;
			Mote.Radius -= (160.0f + 900.0f * Closeness) * Mote.Seed * Delta;
			Mote.Height = FMath::Lerp(Mote.Height, BlackHoleCoreHeight, FMath::Min(Delta * 1.8f, 1.0f));
			if (Mote.Radius < 70.0f)
			{
				Mote.Radius = GetRadius() * FMath::FRandRange(0.75f, 1.0f);
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
				Shape->SetWorldScale3D(FVector(Length, 4.0f, 4.0f) / 100.0f);
			}
			SetIntensity(Mote.Material.Get(), 0.9f * Reach * FMath::Min(Closeness * 4.0f + 0.2f, 1.0f));
		}
	}
	UpdateBlackHoleTethers(Core, bOpen ? Reach : 0.0f);

	// Closing: it snaps shut in a white-violet flash, a ring of light thrown out and sparks flung from the point.
	if (!bCollapseBurstPlayed && Age >= GetActiveSeconds() + 0.35f)
	{
		bCollapseBurstPlayed = true;
		if (AChaosImpactFxKit* Fx = AChaosImpactFxKit::Spawn(this, Core))
		{
			FChaosImpactFxPiece Snap;
			Snap.Life = 0.25f;
			Snap.SizeFrom = 120.0f;
			Snap.SizeTo = 560.0f;
			Snap.Color = FLinearColor(0.5f, 0.2f, 1.0f);
			Snap.CoreColor = FLinearColor(1.0f, 0.9f, 1.0f);
			Snap.Intensity = 8.0f;
			Snap.Attack = 0.05f;
			Snap.NoiseAmount = 0.2f;
			Fx->Add(Snap);
			// Lying flat: turned to the camera, this wide a ring would be cut in half by the floor.
			FChaosImpactFxPiece Wave;
			Wave.Look = EChaosImpactFxLook::Ring;
			Wave.Life = 0.4f;
			Wave.SizeFrom = Wave.SizeTo = 900.0f;
			Wave.RingFrom = 0.05f;
			Wave.RingTo = 0.95f;
			Wave.RingWidth = 0.05f;
			Wave.Color = FLinearColor(0.6f, 0.25f, 1.0f);
			Wave.CoreColor = FLinearColor(1.0f, 0.85f, 1.0f);
			Wave.Intensity = 4.0f;
			Wave.Attack = 0.05f;
			Wave.ErodeTo = 0.8f;
			Fx->Add(Wave);
			for (int32 Index = 0; Index < 12; ++Index)
			{
				FChaosImpactFxPiece Spark;
				Spark.Look = EChaosImpactFxLook::Streak;
				Spark.Velocity = FRotator(FMath::FRandRange(-40.0f, 60.0f), FMath::FRandRange(0.0f, 360.0f), 0.0f).Vector() * FMath::FRandRange(700.0f, 1400.0f);
				Spark.Drag = 2.0f;
				Spark.Life = FMath::FRandRange(0.3f, 0.55f);
				Spark.SizeFrom = 90.0f;
				Spark.SizeTo = 20.0f;
				Spark.Width = 6.0f;
				Spark.Color = FLinearColor(0.75f, 0.3f, 1.0f);
				Spark.CoreColor = FLinearColor(1.0f, 0.9f, 1.0f);
				Spark.Intensity = 6.0f;
				Spark.Attack = 0.02f;
				Spark.ErodeTo = 0.0f;
				Fx->Add(Spark);
			}
		}
	}
	const float FlashT = (Age - GetActiveSeconds() - 0.35f) / 0.3f;
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
			// Look only (RPG): threads only to the other side (a player's hole draws enemies, not the other players).
			const bool bSameSide = bVisualOnly && SourcePawn && Character->IsPlayerControlled() == SourcePawn->IsPlayerControlled();
			if (Character->IsEliminated() || Character == SourcePawn || bSameSide
				|| AChaosImpactGameState::AreTeammates(GetWorld(), SourcePawn, Character)
				|| FVector(Offset.X, Offset.Y, 0.0f).SizeSquared() > FMath::Square(GetRadius())
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

	if (bFireTrail || ZoneType == EChaosImpactBallType::Smoke)
	{
		// Only looping effects, stopped above once it is over; no light of its own to animate. (The ring a smoke's
		// thrower sees in place of the cloud fades with it.)
		SetIntensity(RingMaterial.Get(), 0.6f * Fade);
		return;
	}
	if (ZoneType == EChaosImpactBallType::Snow)
	{
		UpdateSnowChunks(Age);
		return;
	}
	if (ZoneType == EChaosImpactBallType::Nova)
	{
		UpdateNovaPresentation(Age);
		return;
	}
	if (ZoneType == EChaosImpactBallType::Beam)
	{
		ZoneLight->SetIntensity(40000.0f * FMath::Exp(-Age * 12.0f));
		return;
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
			ChaosImpactBallTypes::PlayIceShatter(this, GetActorLocation() + FVector(0.0f, 0.0f, 40.0f), 0.6f, 1.0f);
			ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::CharIceShatter, GetActorLocation(), 0.55f, 1.15f);
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

	if (ZoneLight && !bFireTrail)
	{
		const float Intensity = bIce
			? 25000.0f * FMath::Exp(-Age * 6.0f) + 1500.0f * Appear * Fade
			: 60000.0f * FMath::Exp(-Age * 7.0f);
		ZoneLight->SetIntensity(Intensity);
		ZoneLight->SetRelativeLocation(FVector(0.0f, 0.0f, 90.0f + BurstHeight * FMath::Exp(-Age * 5.0f)));
	}
}
