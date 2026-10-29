#include "ChaosImpactNovaParts.h"

#include "ChaosImpact.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactBallSpawner.h"
#include "ChaosImpactBallTypes.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactHazardZone.h"
#include "ChaosImpactSfx.h"
#include "Components/PointLightComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

namespace
{
	/** Where each part's middle is in its model (Unreal axes), to turn it about its middle. */
	const FVector PartCentres[] = {FVector(10.0f, 10.0f, 48.0f), FVector(0.0f, 0.0f, 40.0f), FVector(0.0f, 0.0f, 33.0f)};
	const FLinearColor PartColours[] = {FLinearColor(1.0f, 0.62f, 0.25f), FLinearColor(0.3f, 0.95f, 1.0f), FLinearColor(0.85f, 0.9f, 1.0f)};

	EChaosImpactNovaRig RigOf(const EChaosImpactNovaPart Part)
	{
		return Part == EChaosImpactNovaPart::Gear ? EChaosImpactNovaRig::Gear
			: Part == EChaosImpactNovaPart::Ring ? EChaosImpactNovaRig::Ring : EChaosImpactNovaRig::Core;
	}

	/** Over the ground at a place (or near it). */
	FVector OnGround(UWorld* World, const FVector& Point, const float Height)
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ChaosImpactNovaPartGround), false);
		if (World->LineTraceSingleByObjectType(Hit, Point + FVector(0.0f, 0.0f, 400.0f), Point - FVector(0.0f, 0.0f, 3000.0f),
			FCollisionObjectQueryParams(ECC_WorldStatic), Params))
		{
			return Hit.ImpactPoint + FVector(0.0f, 0.0f, Height);
		}
		return Point + FVector(0.0f, 0.0f, Height);
	}

	/** A ball pad in play, at random (or the world's middle with none). */
	FVector AnyPad(UWorld* World)
	{
		TArray<AActor*> Pads;
		for (TActorIterator<AChaosImpactBallSpawner> It(World); It; ++It)
		{
			if (!It->IsHidden())
			{
				Pads.Add(*It);
			}
		}
		return Pads.IsEmpty() ? FVector::ZeroVector : Pads[FMath::RandRange(0, Pads.Num() - 1)]->GetActorLocation();
	}
}

const TCHAR* ChaosImpactNovaParts::GetName(const EChaosImpactNovaPart Part)
{
	switch (Part)
	{
	case EChaosImpactNovaPart::Gear: return TEXT("ギア");
	case EChaosImpactNovaPart::Core: return TEXT("コア");
	default: return TEXT("リング");
	}
}

void ChaosImpactNovaParts::Drop(UWorld* World, const uint8 Parts, const FVector* Near)
{
	if (!World || World->GetNetMode() == NM_Client)
	{
		return;
	}
	for (int32 Index = 0; Index < 3; ++Index)
	{
		if (!(Parts & (1 << Index)))
		{
			continue;
		}
		// Scattered a little round where they were dropped; new (or lost) ones out of a pad (beside one, should every
		// pad have a part on it already).
		const EChaosImpactNovaPart Part = static_cast<EChaosImpactNovaPart>(Index);
		if (!Near && PutOutAtPad(World, Part))
		{
			continue;
		}
		const FVector Out = FRotator(0.0f, FMath::FRandRange(0.0f, 360.0f), 0.0f).Vector();
		const FVector Place = Near ? *Near + Out * FMath::FRandRange(70.0f, 150.0f) : AnyPad(World) + Out * 190.0f;
		AChaosImpactNovaPart::Spawn(World, Part, OnGround(World, Place, PartHoverHeight));
	}
}

bool ChaosImpactNovaParts::PutOutAtPad(UWorld* World, const EChaosImpactNovaPart Part)
{
	if (!World || World->GetNetMode() == NM_Client)
	{
		return false;
	}
	// A pad with nothing on it first; else one with a ball on it (the part takes its place); never one with a part.
	TArray<AChaosImpactBallSpawner*> Free;
	TArray<AChaosImpactBallSpawner*> WithBall;
	for (TActorIterator<AChaosImpactBallSpawner> It(World); It; ++It)
	{
		if (It->IsInPlay() && !It->GetNovaPart())
		{
			(It->GetActiveBall() ? WithBall : Free).Add(*It);
		}
	}
	const TArray<AChaosImpactBallSpawner*>& From = Free.IsEmpty() ? WithBall : Free;
	return !From.IsEmpty() && From[FMath::RandRange(0, From.Num() - 1)]->PutOutNovaPart(Part);
}

void ChaosImpactNovaParts::DropNovaBall(UWorld* World, const FVector* Near)
{
	if (!World || World->GetNetMode() == NM_Client)
	{
		return;
	}
	const FVector Place = OnGround(World, Near ? *Near : AnyPad(World) + FVector(0.0f, 0.0f, 40.0f), 60.0f);
	const FTransform Where(FRotator::ZeroRotator, Place);
	AChaosImpactBall* Ball = World->SpawnActorDeferred<AChaosImpactBall>(AChaosImpactBall::StaticClass(), Where, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Ball)
	{
		Ball->SetBallType(EChaosImpactBallType::Nova);
		Ball->FinishSpawning(Where);
		Ball->MakeRollingPickup(FVector::ZeroVector);
		UE_LOG(LogChaosImpact, Log, TEXT("The nova was lost off the stage: it is back at %s"), *Place.ToCompactString());
	}
}

// ---- A part --------------------------------------------------------------------------------------------------------

AChaosImpactNovaPart::AChaosImpactNovaPart()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(false);
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	Pickup = CreateDefaultSubobject<USphereComponent>(TEXT("Pickup"));
	Pickup->SetupAttachment(SceneRoot);
	Pickup->InitSphereRadius(85.0f);
	Pickup->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	Pickup->SetGenerateOverlapEvents(true);
	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(SceneRoot);
	Light->SetIntensity(4000.0f);
	Light->SetAttenuationRadius(450.0f);
	Light->SetCastShadows(false);
}

AChaosImpactNovaPart* AChaosImpactNovaPart::Spawn(UWorld* World, const EChaosImpactNovaPart InPart, const FVector& Location)
{
	if (!World)
	{
		return nullptr;
	}
	const FTransform Where(FRotator::ZeroRotator, Location);
	AChaosImpactNovaPart* Spawned = World->SpawnActorDeferred<AChaosImpactNovaPart>(StaticClass(), Where, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Spawned)
	{
		Spawned->Part = InPart;
		Spawned->FinishSpawning(Where);
		UE_LOG(LogChaosImpact, Log, TEXT("Nova part %s out at %s"), ChaosImpactNovaParts::GetName(InPart), *Location.ToCompactString());
	}
	return Spawned;
}

void AChaosImpactNovaPart::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AChaosImpactNovaPart, Part);
}

void AChaosImpactNovaPart::BeginPlay()
{
	Super::BeginPlay();
	PickableAt = GetWorld() ? GetWorld()->GetTimeSeconds() + 0.6 : 0.0;
	Pickup->OnComponentBeginOverlap.AddDynamic(this, &AChaosImpactNovaPart::HandleOverlap);
	if (GetNetMode() != NM_DedicatedServer)
	{
		BuildLook();
		ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::StageWarp, GetActorLocation(), 0.8f, 1.2f);
	}
}

void AChaosImpactNovaPart::OnRep_Part()
{
	// Built with the first value already; only a change needs a new look (not expected).
}

void AChaosImpactNovaPart::BuildLook()
{
	Model.Build(this, SceneRoot, RigOf(Part));
	Light->SetLightColor(PartColours[static_cast<int32>(Part)]);
	// A tall, faint pillar of light: seen from anywhere on the stage.
	if (UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")))
	{
		Pillar = NewObject<UStaticMeshComponent>(this);
		Pillar->SetStaticMesh(Cylinder);
		Pillar->SetMaterial(0, ChaosImpactBallTypes::MakeAdditive(this, PartColours[static_cast<int32>(Part)], 0.35f, 1.0f));
		Pillar->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Pillar->SetCastShadow(false);
		Pillar->SetupAttachment(SceneRoot);
		Pillar->SetRelativeLocation(FVector(0.0f, 0.0f, 600.0f - ChaosImpactNovaParts::PartHoverHeight));
		Pillar->SetRelativeScale3D(FVector(0.35f, 0.35f, 12.0f));
		Pillar->RegisterComponent();
	}
}

void AChaosImpactNovaPart::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ShowTime += DeltaSeconds;
	if (USceneComponent* ModelRoot = Model.GetRoot())
	{
		// Turning slowly about its middle, bobbing, its own gears and rings turning as they were made to.
		constexpr float Scale = 0.9f;
		const FRotator Turn(0.0f, ShowTime * 50.0f, 0.0f);
		ModelRoot->SetRelativeScale3D(FVector(Scale));
		ModelRoot->SetRelativeRotation(Turn);
		ModelRoot->SetRelativeLocation(FVector(0.0f, 0.0f, 12.0f * FMath::Sin(ShowTime * 2.2f))
			- Turn.RotateVector(PartCentres[static_cast<int32>(Part)] * Scale));
		Model.Pose(FMath::Fmod(ShowTime, ChaosImpactNova::PartLoopSeconds));
	}
	if (HasAuthority() && GetWorld() && GetWorld()->GetTimeSeconds() >= PickableAt)
	{
		TArray<AActor*> Touching;
		Pickup->GetOverlappingActors(Touching, AChaosImpactCharacter::StaticClass());
		for (AActor* Actor : Touching)
		{
			if (TryGive(Cast<AChaosImpactCharacter>(Actor)))
			{
				break;
			}
		}
	}
}

void AChaosImpactNovaPart::HandleOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (HasAuthority() && GetWorld() && GetWorld()->GetTimeSeconds() >= PickableAt)
	{
		TryGive(Cast<AChaosImpactCharacter>(OtherActor));
	}
}

bool AChaosImpactNovaPart::TryGive(AChaosImpactCharacter* Character)
{
	if (!HasAuthority() || IsActorBeingDestroyed() || !IsValid(Character) || Character->IsEliminated()
		|| (Character->GetNovaParts() & ChaosImpactNovaParts::Bit(Part)))
	{
		return false;
	}
	Character->AddNovaPart(Part);
	Destroy();
	return true;
}

// ---- The director ----------------------------------------------------------------------------------------------------

UChaosImpactNovaDirector* UChaosImpactNovaDirector::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UChaosImpactNovaDirector>() : nullptr;
}

bool UChaosImpactNovaDirector::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
}

TStatId UChaosImpactNovaDirector::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UChaosImpactNovaDirector, STATGROUP_Tickables);
}

void UChaosImpactNovaDirector::Schedule(const float From, const float To)
{
	for (float& At : DueAt)
	{
		At = FMath::FRandRange(From, FMath::Max(To, From + 1.0f));
	}
}

void UChaosImpactNovaDirector::SpawnPart(const EChaosImpactNovaPart Part)
{
	ChaosImpactNovaParts::Drop(GetWorld(), ChaosImpactNovaParts::Bit(Part), nullptr);
}

void UChaosImpactNovaDirector::SpawnAllNow()
{
	for (int32 Index = 0; Index < 3; ++Index)
	{
		DueAt[Index] = -1.0f;
		SpawnPart(static_cast<EChaosImpactNovaPart>(Index));
	}
}

bool UChaosImpactNovaDirector::IsNovaInPlay() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	for (TActorIterator<AChaosImpactNovaPart> It(World); It; ++It)
	{
		if (!It->IsActorBeingDestroyed())
		{
			return true;
		}
	}
	for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
	{
		if (It->GetNovaParts() != 0)
		{
			return true;
		}
		for (int32 Slot = 0; Slot < It->GetCarriedBallCount(); ++Slot)
		{
			if (It->GetCarriedBallType(Slot) == EChaosImpactBallType::Nova)
			{
				return true;
			}
		}
	}
	for (TActorIterator<AChaosImpactBall> It(World); It; ++It)
	{
		if (It->GetBallType() == EChaosImpactBallType::Nova && !It->HasDetonated() && !It->IsActorBeingDestroyed())
		{
			return true;
		}
	}
	for (TActorIterator<AChaosImpactHazardZone> It(World); It; ++It)
	{
		if (It->GetZoneType() == EChaosImpactBallType::Nova && It->GetAge() < It->GetActiveSeconds())
		{
			return true;
		}
	}
	return false;
}

void UChaosImpactNovaDirector::Tick(const float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client || !World->HasBegunPlay())
	{
		return;
	}
	const AChaosImpactGameState* Match = World->GetGameState<AChaosImpactGameState>();
	if (Match && Match->bVersusMatch)
	{
		// One set a match: each part once, at its own random time in the first two thirds or so.
		// The duel (ランクマッチ) is played with normal balls only: no nova there.
		if (Match->Phase != EChaosImpactOnlinePhase::Match || Match->Rules.bDuel)
		{
			return;
		}
		const float Elapsed = Match->GetPhaseElapsedSeconds();
		const double Started = World->GetTimeSeconds() - Elapsed;
		if (MatchStartedAt < 0.0 || FMath::Abs(Started - MatchStartedAt) > 2.0)
		{
			MatchStartedAt = Started;
			const float Length = Elapsed + Match->GetPhaseRemainingSeconds();
			Schedule(8.0f, FMath::Max(20.0f, Length * 0.6f));
			UE_LOG(LogChaosImpact, Log, TEXT("Nova parts due at %.0f, %.0f and %.0f s"), DueAt[0], DueAt[1], DueAt[2]);
		}
		for (int32 Index = 0; Index < 3; ++Index)
		{
			if (DueAt[Index] >= 0.0f && Elapsed >= DueAt[Index])
			{
				DueAt[Index] = -1.0f;
				SpawnPart(static_cast<EChaosImpactNovaPart>(Index));
			}
		}
		return;
	}
	if (!ChaosImpact::IsTrainingWorld(World))
	{
		return;
	}
	// Training: a new set some seconds after the last nova and all its parts are gone.
	MatchStartedAt = -1.0;
	const double Now = World->GetTimeSeconds();
	bool bWaiting = false;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		if (DueAt[Index] >= 0.0f)
		{
			bWaiting = true;
			if (Now >= DueAt[Index])
			{
				DueAt[Index] = -1.0f;
				SpawnPart(static_cast<EChaosImpactNovaPart>(Index));
			}
		}
	}
	if (bWaiting)
	{
		return;
	}
	if (IsNovaInPlay())
	{
		NoneSince = -1.0;
		return;
	}
	if (NoneSince < 0.0)
	{
		NoneSince = Now;
	}
	else if (Now - NoneSince > 2.0)
	{
		NoneSince = -1.0;
		Schedule(static_cast<float>(Now) + 3.0f, static_cast<float>(Now) + 12.0f);
	}
}
