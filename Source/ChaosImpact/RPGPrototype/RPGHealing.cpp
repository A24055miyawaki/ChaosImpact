#include "RPGHealing.h"
#include "RPGPrototype.h"
#include "RPGAdventure.h"
#include "ChaosImpactSfx.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"

namespace
{
UStaticMeshComponent* HealShape(AActor* Owner, USceneComponent* Parent, const TCHAR* Name, const TCHAR* Shape, const TCHAR* Material, const FVector& At, const FVector& Size, const FRotator& Rotation = FRotator::ZeroRotator)
{
	auto* Mesh = Owner->CreateDefaultSubobject<UStaticMeshComponent>(Name);
	Mesh->SetupAttachment(Parent);
	Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), Shape, Shape)));
	Mesh->SetMaterial(0, LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("/Game/ChaosImpact/RPGPrototype/Adventure/Materials/M_%s.M_%s"), Material, Material)));
	Mesh->SetRelativeLocation(At); Mesh->SetRelativeRotation(Rotation); Mesh->SetRelativeScale3D(Size / 100);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
	return Mesh;
}

/** The hurt player within Radius who is closest, or nobody. */
ARPGPrototypeCharacter* HealHurtPlayerNear(UWorld* World, const FVector& At, float Radius)
{
	ARPGPrototypeCharacter* Best = nullptr;
	float BestDistance = FMath::Square(Radius);
	for (TActorIterator<ARPGPrototypeCharacter> It(World); It; ++It)
	{
		if (It->IsGrunt() || It->IsEliminated() || It->IsDowned() || It->GetHealth() >= It->GetMaxHealth() - 0.5f) { continue; }
		const float Distance = FVector::DistSquared(It->GetActorLocation() - FVector(0, 0, 60), At);
		if (Distance < BestDistance) { Best = *It; BestDistance = Distance; }
	}
	return Best;
}

/** Ground under At (or At itself). */
FVector HealGround(UWorld* World, const FVector& At)
{
	FHitResult Hit;
	return World->LineTraceSingleByObjectType(Hit, At + FVector(0, 0, 600), At - FVector(0, 0, 1200), FCollisionObjectQueryParams(ECC_WorldStatic)) ? Hit.ImpactPoint : At;
}
}

// ---------------------------------------------------------------- the heart

ARPGHealPickup::ARPGHealPickup()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicatingMovement(false);
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("HeartRoot")));
	Spinner = CreateDefaultSubobject<USceneComponent>(TEXT("HeartSpinner"));
	Spinner->SetupAttachment(GetRootComponent());
	// A heart from two balls and a turned box.
	HealShape(this, Spinner, TEXT("HeartLeft"), TEXT("Sphere"), TEXT("CrystalPink"), FVector(0, -11, 8), FVector(30, 30, 30));
	HealShape(this, Spinner, TEXT("HeartRight"), TEXT("Sphere"), TEXT("CrystalPink"), FVector(0, 11, 8), FVector(30, 30, 30));
	HealShape(this, Spinner, TEXT("HeartPoint"), TEXT("Cube"), TEXT("CrystalPink"), FVector(0, 0, -6), FVector(12, 30, 30), FRotator(0, 0, 45));
	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("HeartLight"));
	Light->SetupAttachment(GetRootComponent());
	Light->SetLightColor(FLinearColor(1.0f, 0.35f, 0.55f)); Light->SetIntensity(1800); Light->SetAttenuationRadius(260); Light->SetCastShadows(false);
}

void ARPGHealPickup::BeginPlay()
{
	Super::BeginPlay();
	Rest = GetActorLocation(); BornAt = GetWorld()->GetTimeSeconds();
	if (HasAuthority()) { SetLifeSpan(RPGTuning::HeartLifetime); }
}

ARPGHealPickup* ARPGHealPickup::Drop(UWorld* World, const FVector& At)
{
	if (!World || World->GetNetMode() == NM_Client) { return nullptr; }
	const FVector Spot = HealGround(World, At + FVector(FMath::FRandRange(-60.f, 60.f), FMath::FRandRange(-60.f, 60.f), 0)) + FVector(0, 0, 55);
	FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World->SpawnActor<ARPGHealPickup>(Spot, FRotator::ZeroRotator, Spawn);
}

void ARPGHealPickup::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float T = GetWorld()->GetTimeSeconds() - BornAt;
	Spinner->SetRelativeLocationAndRotation(FVector(0, 0, 8 * FMath::Sin(T * 3)), FRotator(0, T * 120, 0));
	// Blinks before it goes.
	const float Left = GetLifeSpan() > 0 ? GetLifeSpan() : RPGTuning::HeartLifetime - T;
	Spinner->SetVisibility(Left > 5 || FMath::Frac(T * 4) < 0.6f, true);
	if (!HasAuthority()) { return; }
	// Drifts to a hurt player close by, and heals them when it reaches them.
	if (auto* Player = HealHurtPlayerNear(GetWorld(), GetActorLocation(), 300))
	{
		const FVector To = Player->GetActorLocation() + FVector(0, 0, 20);
		SetActorLocation(FMath::VInterpTo(GetActorLocation(), To, DeltaSeconds, 6));
		if (FVector::DistSquared(GetActorLocation(), To) < FMath::Square(90.0f))
		{
			Player->RestoreHP(Player->GetMaxHealth() * RPGTuning::HeartHealPercent * 0.01f);
			Player->ShowNotice(TEXT("ハートで回復した"), 1.5f);
			if (Player->IsLocallyControlled()) { ChaosImpactSfx::Play2D(Player, EChaosImpactSfx::UiConfirm, .5f); }
			Destroy();
		}
	}
	else if (!GetActorLocation().Equals(Rest, 1)) { SetActorLocation(FMath::VInterpTo(GetActorLocation(), Rest, DeltaSeconds, 2)); }
}

// ---------------------------------------------------------------- the healing herb

ARPGHealHerb::ARPGHealHerb()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.15f;
	bReplicates = true;
	SetReplicatingMovement(false);
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("HerbRoot")));
	HealShape(this, GetRootComponent(), TEXT("HerbBush"), TEXT("Sphere"), TEXT("Leaves"), FVector(0, 0, 28), FVector(95, 95, 62));
	HealShape(this, GetRootComponent(), TEXT("HerbBushSide"), TEXT("Sphere"), TEXT("LeavesLight"), FVector(26, 18, 22), FVector(60, 60, 44));
	const FVector Spots[] = {FVector(-30, -18, 52), FVector(10, -36, 46), FVector(34, 8, 50), FVector(-6, 30, 56), FVector(0, 0, 62)};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Spots); ++Index)
	{ Berries.Add(HealShape(this, GetRootComponent(), *FString::Printf(TEXT("HerbBerry%d"), Index), TEXT("Sphere"), TEXT("FlowerPink"), Spots[Index], FVector(17, 17, 17))); }
}

void ARPGHealHerb::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ARPGHealHerb, bRipe);
}

void ARPGHealHerb::OnRep_Ripe()
{
	for (UStaticMeshComponent* Berry : Berries) { if (Berry) { Berry->SetVisibility(bRipe); } }
}

void ARPGHealHerb::Regrow() { bRipe = true; OnRep_Ripe(); }

void ARPGHealHerb::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority()) { return; }
	const double Now = GetWorld()->GetTimeSeconds();
	if (!bRipe) { if (Now >= RipeAt) { Regrow(); } return; }
	if (auto* Player = HealHurtPlayerNear(GetWorld(), GetActorLocation() + FVector(0, 0, 30), 140))
	{
		Player->RestoreHP(Player->GetMaxHealth() * RPGTuning::HerbHealPercent * 0.01f);
		Player->ShowNotice(TEXT("いやし草の実を食べて回復した"), 2);
		if (Player->IsLocallyControlled()) { ChaosImpactSfx::Play2D(Player, EChaosImpactSfx::UiConfirm, .5f); }
		bRipe = false; RipeAt = Now + RPGTuning::HerbRegrowSeconds; OnRep_Ripe();
	}
}

void ARPGHealHerb::SpawnField(UWorld* World)
{
	if (!World || World->GetNetMode() == NM_Client) { return; }
	for (TActorIterator<ARPGHealHerb> It(World); It; ++It) { return; }
	TArray<FVector> Spots;
	// Two by every rest beacon, one beside every enemy camp (a little way off, to come back to after the fight).
	for (TActorIterator<ARPGAdventureCheckpoint> It(World); It; ++It)
	{
		const FTransform& T = It->GetActorTransform();
		Spots.Add(T.TransformPosition(FVector(-60, 330, 0))); Spots.Add(T.TransformPosition(FVector(-60, -330, 0)));
	}
	for (TActorIterator<ARPGPrototypeSpawnPoint> It(World); It; ++It)
	{
		if (It->bEncounterGroup) { Spots.Add(It->GetActorLocation() + FRotator(0, (It->GetUniqueID() % 8) * 45.0f, 0).RotateVector(FVector(820, 0, 0))); }
	}
	const auto* Adventure = ARPGAdventureWorld::Find(World);
	FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	TArray<FVector> Placed;
	for (const FVector& Spot : Spots)
	{
		// Beacons and camps close together would put two bushes in one place.
		if ((Adventure && Adventure->IsInColony(Spot)) || Placed.ContainsByPredicate([&](const FVector& P) { return FVector::DistSquared2D(P, Spot) < FMath::Square(300.0f); })) { continue; }
		Placed.Add(Spot);
		World->SpawnActor<ARPGHealHerb>(HealGround(World, Spot), FRotator(0, FMath::FRandRange(0.f, 360.f), 0), Spawn);
	}
}
