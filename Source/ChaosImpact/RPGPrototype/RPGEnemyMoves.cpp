#include "RPGEnemyMoves.h"
#include "RPGPrototype.h"
#include "RPGPrototypeVFX.h"
#include "ChaosImpactBallTypes.h"
#include "ChaosImpactSimaeBossFx.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

// The field enemies' warned attacks (bomber, charger, sniper). See RPGEncounters.cpp for who uses which, and when.

ARPGGroundStrike::ARPGGroundStrike()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(false);
	bAlwaysRelevant = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("StrikeRoot")));
	Shell = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Bomb"));
	Shell->SetupAttachment(GetRootComponent());
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (Sphere.Succeeded()) { Shell->SetStaticMesh(Sphere.Object); }
	Shell->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Shell->SetCastShadow(true);
	Shell->SetHiddenInGame(true);
	Shell->SetUsingAbsoluteLocation(true);
	Shell->SetRelativeScale3D(FVector(0.55f));
}

void ARPGGroundStrike::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(ARPGGroundStrike, Kind, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(ARPGGroundStrike, Start, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(ARPGGroundStrike, ThrownFrom, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(ARPGGroundStrike, Direction, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(ARPGGroundStrike, Length, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(ARPGGroundStrike, Radius, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(ARPGGroundStrike, Delay, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(ARPGGroundStrike, Source, COND_InitialOnly);
}

ARPGGroundStrike* ARPGGroundStrike::Lob(ARPGPrototypeCharacter* InSource, const FVector& Center, float InRadius, float InDelay, float InDamage)
{
	UWorld* World = InSource ? InSource->GetWorld() : nullptr;
	if (!World) { return nullptr; }
	const FVector Ground = ChaosImpactSimaeFx::FindGround(World, Center, InSource);
	const FTransform Where(Ground);
	auto* Strike = World->SpawnActorDeferred<ARPGGroundStrike>(StaticClass(), Where, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Strike) { return nullptr; }
	Strike->Kind = Bomb; Strike->Start = Ground; Strike->ThrownFrom = InSource->GetActorLocation() + FVector(0, 0, 60);
	Strike->Radius = InRadius; Strike->Delay = InDelay; Strike->Damage = InDamage; Strike->Source = InSource;
	Strike->FinishSpawning(Where);
	return Strike;
}

ARPGGroundStrike* ARPGGroundStrike::Lane(ARPGPrototypeCharacter* InSource, uint8 InKind, const FVector& From, const FVector& Toward, float InLength, float HalfWidth, float InDelay, float InDamage)
{
	UWorld* World = InSource ? InSource->GetWorld() : nullptr;
	const FVector Flat = Toward.GetSafeNormal2D();
	if (!World || Flat.IsNearlyZero()) { return nullptr; }
	const FVector Ground = ChaosImpactSimaeFx::FindGround(World, From, InSource);
	const FTransform Where(Flat.Rotation(), Ground);
	auto* Strike = World->SpawnActorDeferred<ARPGGroundStrike>(StaticClass(), Where, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Strike) { return nullptr; }
	Strike->Kind = InKind; Strike->Start = Ground; Strike->Direction = Flat; Strike->Length = InLength;
	Strike->Radius = HalfWidth; Strike->Delay = InDelay; Strike->Damage = InDamage; Strike->Source = InSource;
	Strike->FinishSpawning(Where);
	return Strike;
}

void ARPGGroundStrike::BeginPlay()
{
	Super::BeginPlay();
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer) { return; }
	// Every screen draws its own warning (nothing of it is sent on: this actor itself reaches every screen).
	ChaosImpactSimaeFx::FNetFxMute Mute;
	const FLinearColor Color = Kind == Bomb ? FLinearColor(1.0f, 0.12f, 0.02f) : Kind == Charge ? FLinearColor(1.0f, 0.08f, 0.1f) : FLinearColor(0.75f, 0.2f, 1.0f);
	Warning = Kind == Bomb ? AChaosImpactSimaeWarning::SpawnCircle(World, Start, Radius, Delay, Color)
		: AChaosImpactSimaeWarning::SpawnLane(World, Start, Direction, Length, Radius, Delay, Color);
	if (Kind == Bomb)
	{
		// The bomb itself, dark with a hot core, flying the whole time the warning fills.
		Shell->SetMaterial(0, ChaosImpactBallTypes::MakeEmissive(this, FLinearColor(1.0f, 0.3f, 0.05f), 2.5f));
		Shell->SetHiddenInGame(false);
		Shell->SetWorldLocation(ThrownFrom);
	}
}

void ARPGGroundStrike::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	if (Kind == Bomb && !bFired)
	{
		// A high lob: from the thrower's hands to the middle of the circle.
		const float T = FMath::Clamp(Age / FMath::Max(Delay, 0.1f), 0.0f, 1.0f);
		const FVector At = FMath::Lerp(ThrownFrom, Start + FVector(0, 0, 30), T) + FVector(0, 0, 4 * T * (1 - T) * 650);
		Shell->SetWorldLocation(At);
		Shell->AddRelativeRotation(FRotator(360 * DeltaSeconds, 200 * DeltaSeconds, 0));
	}
	if (!bFired && Age >= Delay)
	{
		bFired = true;
		Shell->SetHiddenInGame(true);
		if (AChaosImpactSimaeWarning* Shown = Warning.Get()) { Shown->Finish(); }
		if (HasAuthority()) { Fire(); }
	}
	if (HasAuthority() && Age >= Delay + 0.4f) { Destroy(); }
}

void ARPGGroundStrike::Fire()
{
	UWorld* World = GetWorld();
	ARPGPrototypeCharacter* From = Source.Get();
	// A charger that has been knocked out or frozen in the meantime does not charge.
	if (Kind == Charge && (!From || From->IsEliminated() || From->IsIceFrozen() || From->IsRPGStunned())) { return; }
	const FVector End = Start + Direction * Length;
	if (Kind == Bomb) { ARPGPrototypeVFX::PlayImpact(World, EChaosImpactBallType::Fire, Start + FVector(0, 0, 30), Radius / 140.0f, true); }
	else if (Kind == Snipe) { ARPGPrototypeVFX::PlayLightning(World, Start + FVector(0, 0, 90), End + FVector(0, 0, 90)); }
	else if (From)
	{
		// Off it goes, down its lane.
		From->LaunchCharacter(Direction * 2600, true, false);
		ARPGPrototypeVFX::PlayImpact(World, EChaosImpactBallType::Wind, From->GetActorLocation(), 0.8f, false);
	}
	for (TActorIterator<ARPGPrototypeCharacter> It(World); It; ++It)
	{
		ARPGPrototypeCharacter* Hero = *It;
		if (Hero->IsGrunt() || Hero->IsEliminated() || Hero->IsDowned() || Hero->IsDashing()) { continue; }
		const FVector At = Hero->GetActorLocation();
		if (FMath::Abs(At.Z - Start.Z) > 260) { continue; }
		FVector Away;
		if (Kind == Bomb)
		{
			if (FVector::Dist2D(At, Start) > Radius + 35) { continue; }
			Away = (At - Start).GetSafeNormal2D();
		}
		else
		{
			const FVector Closest = FMath::ClosestPointOnSegment(At, Start, End);
			if (FVector::Dist2D(At, Closest) > Radius + 40) { continue; }
			Away = FVector::CrossProduct(FVector::UpVector, Direction) * (FVector::DotProduct(At - Closest, FVector::CrossProduct(FVector::UpVector, Direction)) >= 0 ? 1 : -1);
		}
		UGameplayStatics::ApplyDamage(Hero, Damage, From ? From->GetController() : nullptr, From ? static_cast<AActor*>(From) : static_cast<AActor*>(this), nullptr);
		if (!Away.IsNearlyZero()) { Hero->ApplyRPGKnockback(Away, Kind == Snipe ? 120 : 260); }
	}
}
