#include "ChaosImpactSimaeBird.h"

#include "ChaosImpactBallTypes.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactGameState.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "ChaosImpactLightning.h"
#include "Engine/Texture.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundAttenuation.h"
#include "GameFramework/GameStateBase.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ObjectKey.h"

/** Server-only record shared by every bird. Retargeting cannot reset an enemy's damage cap or shake-off. */
struct FChaosImpactSimaeFlockState
{
	struct FTarget
	{
		float FirstPerchedAt = -1.0f;
		float NextPeckAt = -1.0f;
		float LastPeckAt = -1.0f;
		int32 PecksDelivered = 0;
		bool bCleared = false;
		TWeakObjectPtr<AChaosImpactSimaeBird> Leader;
	};
	TMap<FObjectKey, FTarget> Targets;
};

namespace
{
	constexpr float PopOutSeconds = 0.32f;
	constexpr float PerchReach = 45.0f;
	constexpr float BirdScale = 0.8f;
	/** A burst this big (the flock's release) gets a second ring. */
	constexpr float BigBurstScale = 1.5f;
	constexpr float HeadExtraHeight = 14.0f;
	int32 NextFlockId = 1;

	UMaterialInstanceDynamic* MakeBirdMaterial(UObject* Outer, const FLinearColor& Color, const float Roughness)
	{
		UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Game/LevelPrototyping/Materials/M_FlatCol.M_FlatCol"));
		UMaterialInstanceDynamic* Material = Base ? UMaterialInstanceDynamic::Create(Base, Outer) : nullptr;
		if (Material)
		{
			Material->SetVectorParameterValue(TEXT("Color"), Color);
			Material->SetVectorParameterValue(TEXT("Base Color"), Color);
			Material->SetScalarParameterValue(TEXT("Roughness"), Roughness);
		}
		return Material;
	}
}

AChaosImpactSimaeBird::AChaosImpactSimaeBird()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(true);
	SetNetUpdateFrequency(60.0f);
	SetMinNetUpdateFrequency(30.0f);
	SetActorEnableCollision(false);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	VisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VisualRoot"));
	VisualRoot->SetupAttachment(SceneRoot);

	const auto MakePart = [this](const TCHAR* Name) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Part->SetupAttachment(VisualRoot);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetGenerateOverlapEvents(false);
		Part->SetCastShadow(true);
		return Part;
	};
	BodyMesh = MakePart(TEXT("Body"));
	TailMesh = MakePart(TEXT("Tail"));
	WingMesh = MakePart(TEXT("Wings"));
	EyeMesh = MakePart(TEXT("Eyes"));
	CrestMesh = MakePart(TEXT("Crest"));
}

void AChaosImpactSimaeBird::BeginPlay()
{
	Super::BeginPlay();
	SpawnLocation = GetActorLocation();
	SpawnedAt = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	StateStartedAt = SpawnedAt;
	NextTargetReviewAt = SpawnedAt + PopOutSeconds + (BirdIndex % 3) * 0.04f;
	SetLifeSpan(ChaosImpactBallTypes::SimaeLifetimeSeconds);
	BuildAppearance();
	if (HasAuthority() && BirdIndex == 0)
	{
		MulticastChirp(0);
	}
}

void AChaosImpactSimaeBird::ConfigureBird(const int32 InFlockId, const int32 InBirdIndex, APawn* InThrower,
	AChaosImpactCharacter* InTarget, const FVector& InVelocity, const TSharedPtr<FChaosImpactSimaeFlockState>& InFlockState)
{
	FlockId = InFlockId;
	BirdIndex = InBirdIndex;
	SourcePawn = InThrower;
	TargetCharacter = InTarget;
	Velocity = InVelocity;
	FlockState = InFlockState;
}

void AChaosImpactSimaeBird::ReleaseFlock(UWorld* World, const FVector& Location, APawn* ThrowingPawn, AActor* DirectVictim)
{
	if (!World || World->GetNetMode() == NM_Client)
	{
		return;
	}

	TArray<AChaosImpactCharacter*> Targets;
	for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
	{
		AChaosImpactCharacter* Candidate = *It;
		if (!Candidate || Candidate == ThrowingPawn || Candidate->IsEliminated()
			|| AChaosImpactGameState::AreTeammates(World, ThrowingPawn, Candidate)
			|| FVector::DistSquared2D(Location, Candidate->GetActorLocation())
				> FMath::Square(ChaosImpactBallTypes::SimaeAcquireReach))
		{
			continue;
		}
		Targets.Add(Candidate);
	}
	Targets.Sort([Location](const AChaosImpactCharacter& A, const AChaosImpactCharacter& B)
	{
		return FVector::DistSquared2D(Location, A.GetActorLocation())
			< FVector::DistSquared2D(Location, B.GetActorLocation());
	});
	if (AChaosImpactCharacter* Direct = Cast<AChaosImpactCharacter>(DirectVictim); Targets.RemoveSingle(Direct) > 0)
	{
		Targets.Insert(Direct, 0);
	}

	const int32 NewFlockId = NextFlockId++;
	const TSharedPtr<FChaosImpactSimaeFlockState> SharedState = MakeShared<FChaosImpactSimaeFlockState>();
	for (int32 Index = 0; Index < ChaosImpactBallTypes::SimaeBirdCount; ++Index)
	{
		const float Angle = 360.0f * Index / ChaosImpactBallTypes::SimaeBirdCount + FMath::FRandRange(-10.0f, 10.0f);
		const FVector Outward = FVector::ForwardVector.RotateAngleAxis(Angle, FVector::UpVector);
		const FVector InitialVelocity = Outward * FMath::FRandRange(330.0f, 480.0f)
			+ FVector::UpVector * FMath::FRandRange(520.0f, 700.0f);
		const FVector Start = Location + Outward * 22.0f + FVector::UpVector * (10.0f + (Index % 3) * 12.0f);
		AChaosImpactSimaeBird* Bird = World->SpawnActorDeferred<AChaosImpactSimaeBird>(
			StaticClass(), FTransform(Outward.Rotation(), Start), ThrowingPawn, ThrowingPawn,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Bird)
		{
			continue;
		}
		AChaosImpactCharacter* Target = Targets.IsEmpty() ? nullptr : Targets[Index % Targets.Num()];
		Bird->ConfigureBird(NewFlockId, Index, ThrowingPawn, Target, InitialVelocity, SharedState);
		Bird->FinishSpawning(FTransform(Outward.Rotation(), Start));
	}
}

void AChaosImpactSimaeBird::BuildAppearance()
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	using namespace ChaosImpactBallTypes;
	BodyMesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, SimaeAssets::BirdBody));
	TailMesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, SimaeAssets::BirdTail));
	WingMesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, SimaeAssets::BirdWing));
	EyeMesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, SimaeAssets::BirdEye));
	CrestMesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, SimaeAssets::BirdCrest));
	UMaterialInstanceDynamic* White = MakeBirdMaterial(this, FLinearColor(0.95f, 0.96f, 0.98f), 0.82f);
	UMaterialInstanceDynamic* Dark = MakeBirdMaterial(this, FLinearColor(0.055f, 0.043f, 0.036f), 0.72f);
	BodyMesh->SetMaterial(0, White);
	TailMesh->SetMaterial(0, Dark);
	WingMesh->SetMaterial(0, Dark);
	EyeMesh->SetMaterial(0, Dark);
	CrestMesh->SetMaterial(0, Dark);
	UMaterialInterface* TexturedBase = LoadObject<UMaterialInterface>(nullptr, SimaeAssets::TexturedMaterial);
	UTexture* Texture = LoadObject<UTexture>(nullptr, SimaeAssets::BirdTexture);
	UMaterialInstanceDynamic* TexturedLook = TexturedBase && Texture
		? UMaterialInstanceDynamic::Create(TexturedBase, this) : nullptr;
	if (TexturedLook)
	{
		TexturedLook->SetTextureParameterValue(TEXT("BodyTexture"), Texture);
	}
	for (UStaticMeshComponent* Part : {BodyMesh.Get(), TailMesh.Get(), WingMesh.Get(), EyeMesh.Get(), CrestMesh.Get()})
	{
		if (Part)
		{
			Part->SetRelativeScale3D(FVector(BirdScale));
			if (TexturedLook)
			{
				for (int32 Slot = 0; Slot < Part->GetNumMaterials(); ++Slot) { Part->SetMaterial(Slot, TexturedLook); }
			}
		}
	}
	// A short bespoke ribbon avoids inheriting another ball's orange emitter colors.
	FlightTrail = ChaosImpactLightning::CreateComponent(this, SceneRoot,
		MakeAdditive(this, FLinearColor(0.8f, 0.97f, 1.0f), 2.6f));
}

void AChaosImpactSimaeBird::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority())
	{
		UpdateServer(DeltaSeconds);
	}
	UpdateAppearance(DeltaSeconds);
}

void AChaosImpactSimaeBird::UpdateServer(const float DeltaSeconds)
{
	if (!GetWorld())
	{
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now - SpawnedAt >= ChaosImpactBallTypes::SimaeLifetimeSeconds - 1.25f
		&& BirdState != EChaosImpactSimaeBirdState::Departing)
	{
		BeginDeparting((GetActorLocation() - SpawnLocation).GetSafeNormal2D());
	}

	switch (BirdState)
	{
	case EChaosImpactSimaeBirdState::PopOut:
		Velocity.Z -= 900.0f * DeltaSeconds;
		SetActorLocation(GetActorLocation() + Velocity * DeltaSeconds);
		if (Now - StateStartedAt >= PopOutSeconds)
		{
			BeginHunting();
		}
		break;

	case EChaosImpactSimaeBirdState::Hunting:
	{
		if (!IsValidTarget(TargetCharacter))
		{
			TargetCharacter = FindTarget();
		}
		if (Now >= NextTargetReviewAt)
		{
			NextTargetReviewAt = Now + 0.3f;
			// An opponent entering the area gets a share of the birds that are still in flight.
			if (AChaosImpactCharacter* LessCrowded = FindTarget(); LessCrowded && LessCrowded != TargetCharacter)
			{
				TargetCharacter = LessCrowded;
				ForceNetUpdate();
			}
		}
		if (!TargetCharacter)
		{
			// An unclaimed bird circles briefly, giving a newly approaching opponent a chance to be noticed.
			const float Age = Now - StateStartedAt;
			if (Age < 0.75f)
			{
				Velocity = Velocity.RotateAngleAxis(70.0f * DeltaSeconds, FVector::UpVector) * FMath::Exp(-0.8f * DeltaSeconds);
				Velocity.Z = 65.0f;
				SetActorLocation(GetActorLocation() + Velocity * DeltaSeconds);
			}
			else
			{
				BeginDeparting((GetActorLocation() - SpawnLocation).GetSafeNormal2D());
			}
			break;
		}
		const FVector ToTarget = GetTargetHeadLocation() - GetActorLocation();
		const FVector Desired = ToTarget.GetSafeNormal();
		FVector Current = Velocity.GetSafeNormal();
		if (Current.IsNearlyZero())
		{
			Current = Desired;
		}
		const float Dot = FMath::Clamp(FVector::DotProduct(Current, Desired), -1.0f, 1.0f);
		const float Angle = FMath::Acos(Dot);
		const float MaxStep = FMath::DegreesToRadians(ChaosImpactBallTypes::SimaeTurnDegreesPerSecond) * DeltaSeconds;
		if (Angle > KINDA_SMALL_NUMBER)
		{
			FVector Axis = FVector::CrossProduct(Current, Desired).GetSafeNormal();
			if (Axis.IsNearlyZero())
			{
				Axis = FVector::CrossProduct(Current, FMath::Abs(Current.Z) > 0.9f
					? FVector::ForwardVector : FVector::UpVector).GetSafeNormal();
			}
			Current = Current.RotateAngleAxis(FMath::RadiansToDegrees(FMath::Min(Angle, MaxStep)), Axis).GetSafeNormal();
		}
		const float Speed = FMath::Lerp(ChaosImpactBallTypes::SimaeMinSpeed, ChaosImpactBallTypes::SimaeMaxSpeed,
			static_cast<float>(BirdIndex) / FMath::Max(ChaosImpactBallTypes::SimaeBirdCount - 1, 1));
		Velocity = Current * Speed;
		const FVector Previous = GetActorLocation();
		const FVector Next = Previous + Velocity * DeltaSeconds;
		SetActorLocation(Next);
		const FVector Closest = FMath::ClosestPointOnSegment(GetTargetHeadLocation(), Previous, Next);
		if (FVector::DistSquared(Closest, GetTargetHeadLocation()) <= FMath::Square(PerchReach)
			&& !TargetCharacter->IsDashing())
		{
			BeginPerch();
		}
		break;
	}

	case EChaosImpactSimaeBirdState::Perched:
	{
		if (!IsValidTarget(TargetCharacter) || TargetCharacter->IsDashing())
		{
			ReleasePerchedGroup(false);
			break;
		}
		SetActorLocation(GetTargetHeadLocation());
		SetActorRotation(TargetCharacter->GetActorRotation());
		if (!FlockState) { BeginDeparting(FVector::ZeroVector); break; }
		FChaosImpactSimaeFlockState::FTarget& Schedule = FlockState->Targets.FindOrAdd(FObjectKey(TargetCharacter.Get()));
		if (!Schedule.Leader.IsValid() || Schedule.Leader->BirdState != EChaosImpactSimaeBirdState::Perched)
		{
			Schedule.Leader = this;
		}
		if (Schedule.Leader.Get() == this && Now >= Schedule.NextPeckAt)
		{
			// Reserve the pulse first: twelve birds still deal only ONE point at each scheduled peck.
			++Schedule.PecksDelivered;
			Schedule.LastPeckAt = Now;
			Schedule.NextPeckAt = Now + ChaosImpactBallTypes::SimaePeckIntervalSeconds;
			for (TActorIterator<AChaosImpactSimaeBird> It(GetWorld()); It; ++It)
			{
				if (It->FlockId == FlockId && It->TargetCharacter == TargetCharacter)
				{
					It->PecksDelivered = Schedule.PecksDelivered;
					It->NextPeckAt = Schedule.NextPeckAt;
					It->LastPeckAt = Now;
					It->ForceNetUpdate();
				}
			}
			const float Applied = UGameplayStatics::ApplyDamage(TargetCharacter, 1.0f,
				SourcePawn ? SourcePawn->GetController() : nullptr, this, nullptr);
			if (Applied > 0.0f)
			{
				if (AChaosImpactCharacter* Thrower = Cast<AChaosImpactCharacter>(SourcePawn); Thrower && Thrower != TargetCharacter)
				{
					Thrower->RecoverStaminaFromBallHit();
				}
				MulticastChirp(2);
			}
			if (Schedule.PecksDelivered >= ChaosImpactBallTypes::SimaeMaxPecksPerTarget || TargetCharacter->IsEliminated())
			{
				ReleasePerchedGroup(true);
			}
		}
		break;
	}

	case EChaosImpactSimaeBirdState::Departing:
	{
		const FVector Desired = (Velocity.GetSafeNormal2D() + FVector(0.0f, 0.0f, 1.4f)).GetSafeNormal();
		Velocity = FMath::VInterpTo(Velocity, Desired * 1450.0f, DeltaSeconds, 2.8f);
		SetActorLocation(GetActorLocation() + Velocity * DeltaSeconds);
		if (Now - StateStartedAt > 1.25f || GetActorLocation().Z - SpawnLocation.Z > 1300.0f)
		{
			Destroy();
		}
		break;
	}
	}
	if (BirdState != EChaosImpactSimaeBirdState::Perched && !Velocity.IsNearlyZero())
	{
		SetActorRotation(Velocity.Rotation());
	}
}

void AChaosImpactSimaeBird::BeginHunting()
{
	BirdState = EChaosImpactSimaeBirdState::Hunting;
	StateStartedAt = GetWorld()->GetTimeSeconds();
	if (!IsValidTarget(TargetCharacter))
	{
		TargetCharacter = FindTarget();
	}
}

void AChaosImpactSimaeBird::BeginPerch()
{
	if (!FlockState || !IsValidTarget(TargetCharacter)) { return; }
	BirdState = EChaosImpactSimaeBirdState::Perched;
	StateStartedAt = GetWorld()->GetTimeSeconds();
	FChaosImpactSimaeFlockState::FTarget& Schedule = FlockState->Targets.FindOrAdd(FObjectKey(TargetCharacter.Get()));
	if (Schedule.FirstPerchedAt < 0.0f)
	{
		Schedule.FirstPerchedAt = StateStartedAt;
		Schedule.NextPeckAt = StateStartedAt + ChaosImpactBallTypes::SimaePerchSeconds;
		Schedule.Leader = this;
		MulticastChirp(1);
	}
	FirstPerchedAt = Schedule.FirstPerchedAt;
	NextPeckAt = Schedule.NextPeckAt;
	LastPeckAt = Schedule.LastPeckAt;
	PecksDelivered = Schedule.PecksDelivered;
	ForceNetUpdate();
}

void AChaosImpactSimaeBird::BeginDeparting(const FVector& AwayDirection)
{
	BirdState = EChaosImpactSimaeBirdState::Departing;
	StateStartedAt = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	TargetCharacter = nullptr;
	FVector Away = AwayDirection.GetSafeNormal2D();
	if (Away.IsNearlyZero())
	{
		Away = FVector::ForwardVector.RotateAngleAxis(BirdIndex * 360.0f / ChaosImpactBallTypes::SimaeBirdCount, FVector::UpVector);
	}
	Velocity = Away * 700.0f + FVector::UpVector * 900.0f;
	ForceNetUpdate();
}

void AChaosImpactSimaeBird::ReleasePerchedGroup(const bool bPecked)
{
	if (!bPecked)
	{
		// Exactly one bird starts the shake-off effect, even if a later arrival ticks before the leader.
		MulticastChirp(3);
	}
	AChaosImpactCharacter* ReleasedFrom = TargetCharacter;
	if (FlockState && ReleasedFrom)
	{
		// Remember this even after every assigned bird leaves. The same flock can never reacquire a shaken-off enemy.
		FlockState->Targets.FindOrAdd(FObjectKey(ReleasedFrom)).bCleared = true;
	}
	for (TActorIterator<AChaosImpactSimaeBird> It(GetWorld()); It; ++It)
	{
		AChaosImpactSimaeBird* Bird = *It;
		if (Bird->FlockId == FlockId && Bird->TargetCharacter == ReleasedFrom)
		{
			const FVector Side = FVector::ForwardVector.RotateAngleAxis(
				Bird->BirdIndex * 360.0f / ChaosImpactBallTypes::SimaeBirdCount + (bPecked ? 18.0f : 0.0f),
				FVector::UpVector);
			Bird->BeginDeparting(Side);
		}
	}
}

AChaosImpactCharacter* AChaosImpactSimaeBird::FindTarget() const
{
	AChaosImpactCharacter* Best = nullptr;
	int32 FewestAssigned = TNumericLimits<int32>::Max();
	float NearestDistance = TNumericLimits<float>::Max();
	for (TActorIterator<AChaosImpactCharacter> It(GetWorld()); It; ++It)
	{
		AChaosImpactCharacter* Candidate = *It;
		if (!IsValidTarget(Candidate)) { continue; }
		int32 Assigned = 0;
		for (TActorIterator<AChaosImpactSimaeBird> Other(GetWorld()); Other; ++Other)
		{
			Assigned += *Other != this && Other->FlockId == FlockId && Other->TargetCharacter == Candidate ? 1 : 0;
		}
		const float Distance = FVector::DistSquared2D(GetActorLocation(), Candidate->GetActorLocation());
		if (Assigned < FewestAssigned || (Assigned == FewestAssigned && Distance < NearestDistance))
		{
			Best = Candidate;
			FewestAssigned = Assigned;
			NearestDistance = Distance;
		}
	}
	return Best;
}

bool AChaosImpactSimaeBird::IsValidTarget(const AChaosImpactCharacter* Candidate) const
{
	if (FlockState && Candidate)
	{
		if (const FChaosImpactSimaeFlockState::FTarget* Record = FlockState->Targets.Find(FObjectKey(Candidate)); Record && Record->bCleared)
		{
			return false;
		}
	}
	return IsValid(Candidate) && Candidate != SourcePawn && !Candidate->IsEliminated()
		&& !AChaosImpactGameState::AreTeammates(GetWorld(), SourcePawn, Candidate)
		&& FVector::DistSquared2D(SpawnLocation, Candidate->GetActorLocation())
			<= FMath::Square(ChaosImpactBallTypes::SimaeAcquireReach);
}

FVector AChaosImpactSimaeBird::GetTargetHeadLocation() const
{
	if (!TargetCharacter)
	{
		return GetActorLocation();
	}
	const UCapsuleComponent* Capsule = TargetCharacter->GetCapsuleComponent();
	const float Height = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 88.0f;
	const float SideAngle = BirdIndex * 137.5f;
	const FVector PerchOffset = FVector::ForwardVector.RotateAngleAxis(SideAngle, FVector::UpVector) * (18.0f + (BirdIndex % 3) * 8.0f);
	return TargetCharacter->GetActorLocation() + FVector::UpVector * (Height + HeadExtraHeight + (BirdIndex % 3) * 5.0f) + PerchOffset;
}

bool AChaosImpactSimaeBird::HasPerchedBird(const AChaosImpactCharacter* Character, float* OutRemainingSeconds,
	float* OutCountdownSeconds, int32* OutPecksRemaining)
{
	if (OutRemainingSeconds)
	{
		*OutRemainingSeconds = 0.0f;
	}
	if (OutCountdownSeconds) { *OutCountdownSeconds = ChaosImpactBallTypes::SimaePerchSeconds; }
	if (OutPecksRemaining) { *OutPecksRemaining = 0; }
	if (!Character || !Character->GetWorld())
	{
		return false;
	}
	float Earliest = TNumericLimits<float>::Max();
	const AChaosImpactSimaeBird* NextBird = nullptr;
	for (TActorIterator<AChaosImpactSimaeBird> It(Character->GetWorld()); It; ++It)
	{
		if (It->BirdState == EChaosImpactSimaeBirdState::Perched && It->TargetCharacter == Character)
		{
			if (It->NextPeckAt < Earliest)
			{
				Earliest = It->NextPeckAt;
				NextBird = *It;
			}
		}
	}
	if (Earliest == TNumericLimits<float>::Max())
	{
		return false;
	}
	if (OutRemainingSeconds)
	{
		const AGameStateBase* State = Character->GetWorld()->GetGameState();
		const double Now = State ? State->GetServerWorldTimeSeconds() : Character->GetWorld()->GetTimeSeconds();
		*OutRemainingSeconds = FMath::Max(0.0f, static_cast<float>(Earliest - Now));
	}
	if (OutCountdownSeconds && NextBird && NextBird->PecksDelivered > 0)
	{
		*OutCountdownSeconds = ChaosImpactBallTypes::SimaePeckIntervalSeconds;
	}
	if (OutPecksRemaining && NextBird)
	{
		*OutPecksRemaining = FMath::Max(0, ChaosImpactBallTypes::SimaeMaxPecksPerTarget - NextBird->PecksDelivered);
	}
	return true;
}

void AChaosImpactSimaeBird::UpdateAppearance(const float DeltaSeconds)
{
	VisualTime += DeltaSeconds;
	if (!BodyMesh || !WingMesh)
	{
		return;
	}
	if (FlightTrail)
	{
		const bool bFlying = BirdState != EChaosImpactSimaeBirdState::Perched;
		if (!bFlying)
		{
			TrailPoints.Reset();
			FlightTrail->ClearAllMeshSections();
		}
		else
		{
			const FVector Here = GetActorLocation() + FVector::UpVector * 12.0f;
			if (!TrailPoints.IsEmpty() && FVector::DistSquared(Here, TrailPoints.Last().Position) > FMath::Square(500.0f))
			{
				TrailPoints.Reset();
			}
			if (TrailPoints.IsEmpty() || VisualTime - TrailPoints.Last().Time > 0.025f)
			{
				TrailPoints.Add({Here, VisualTime});
			}
			TrailPoints.RemoveAll([this](const FTrailPoint& Point) { return VisualTime - Point.Time > 0.32f; });
			ChaosImpactIceMeshes::FMeshBuffers Ribbon;
			const FTransform Transform = GetActorTransform();
			for (int32 Index = 0; Index + 1 < TrailPoints.Num(); ++Index)
			{
				const FVector Start = TrailPoints[Index].Position;
				const FVector End = TrailPoints[Index + 1].Position;
				const float Width = 11.0f * FMath::Clamp(1.0f - (VisualTime - TrailPoints[Index].Time) / 0.32f, 0.0f, 1.0f);
				// Crossed planes remain visible from every split-screen camera, not just the first player's view.
				for (const FVector Facing : {FVector::UpVector, FVector::RightVector})
				{
					const FVector Side = FVector::CrossProduct(End - Start, Facing).GetSafeNormal() * Width * 0.5f;
					const int32 Base = Ribbon.Vertices.Num();
					for (const FVector Corner : {Start - Side, Start + Side, End - Side, End + Side})
					{
						Ribbon.Vertices.Add(Transform.InverseTransformPosition(Corner));
						Ribbon.Normals.Add(Transform.InverseTransformVectorNoScale(Facing));
					}
					Ribbon.UVs.Append({FVector2D(0, 0), FVector2D(1, 0), FVector2D(0, 1), FVector2D(1, 1)});
					Ribbon.Triangles.Append({Base, Base + 1, Base + 2, Base + 2, Base + 1, Base + 3,
						Base, Base + 2, Base + 1, Base + 2, Base + 3, Base + 1});
				}
			}
			ChaosImpactLightning::SetMesh(FlightTrail, Ribbon);
		}
	}
	if (!HasAuthority() && BirdState == EChaosImpactSimaeBirdState::Perched && IsValid(TargetCharacter))
	{
		// Follow the client's displayed character every frame instead of hovering one network update behind its head.
		SetActorLocation(GetTargetHeadLocation());
		SetActorRotation(TargetCharacter->GetActorRotation());
	}
	const float Flap = 0.72f + 0.28f * FMath::Abs(FMath::Sin(VisualTime * 14.0f * UE_TWO_PI));
	WingMesh->SetRelativeScale3D(FVector(BirdScale, BirdScale, BirdScale * Flap));
	const float Bob = BirdState == EChaosImpactSimaeBirdState::Perched
		? 1.5f * FMath::Sin(VisualTime * 8.0f) : 3.0f * FMath::Sin(VisualTime * 11.0f);
	VisualRoot->SetRelativeLocation(FVector(0.0f, 0.0f, Bob));
	if (BirdState == EChaosImpactSimaeBirdState::Perched)
	{
		const AGameStateBase* State = GetWorld() ? GetWorld()->GetGameState() : nullptr;
		const double Now = State ? State->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
		const float Windup = FMath::Clamp(1.0f - static_cast<float>(NextPeckAt - Now) / 0.2f, 0.0f, 1.0f);
		const float HitAge = static_cast<float>(Now - LastPeckAt);
		const float Peck = LastPeckAt >= 0.0f && HitAge < 0.22f ? FMath::Sin(HitAge / 0.22f * UE_PI) : Windup * 0.6f;
		VisualRoot->SetRelativeRotation(FRotator(Peck * 28.0f, 0.0f, 0.0f));
	}
	else
	{
		VisualRoot->SetRelativeRotation(FRotator::ZeroRotator);
	}
}

void AChaosImpactSimaeBird::MulticastChirp_Implementation(const uint8 Moment)
{
	if (Moment != 3) { PlayChirp(Moment); }
	if (Moment == 0 || Moment == 2 || Moment == 3)
	{
		AChaosImpactSimaeFeatherBurst::Play(GetWorld(), GetActorLocation(), Moment == 0 ? 1.6f : Moment == 2 ? 0.85f : 0.65f);
	}
}

void AChaosImpactSimaeBird::PlayChirp(const uint8 Moment) const
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	using namespace ChaosImpactBallTypes;
	const TCHAR* Paths[] = {SimaeAssets::Chirp01, SimaeAssets::Chirp02, SimaeAssets::Chirp03};
	const int32 Choice = (FlockId + BirdIndex * 3 + Moment) % UE_ARRAY_COUNT(Paths);
	if (USoundBase* Sound = LoadObject<USoundBase>(nullptr, Paths[Choice]))
	{
		// Keep simultaneous flocks quiet and make a distant chirp fade rather than playing across the whole arena.
		static TWeakObjectPtr<UWorld> LastWorld;
		static double NextChirpAt = 0.0;
		const double Now = GetWorld()->GetTimeSeconds();
		if (LastWorld == GetWorld() && Now < NextChirpAt)
		{
			return;
		}
		LastWorld = GetWorld();
		NextChirpAt = Now + 0.15;
		USoundAttenuation* Attenuation = NewObject<USoundAttenuation>(GetTransientPackage());
		Attenuation->Attenuation.bAttenuate = true;
		Attenuation->Attenuation.bSpatialize = true;
		Attenuation->Attenuation.AttenuationShapeExtents = FVector(350.0f, 0.0f, 0.0f);
		Attenuation->Attenuation.FalloffDistance = 2200.0f;
		UGameplayStatics::PlaySoundAtLocation(this, Sound, GetActorLocation(), 0.32f,
			FMath::FRandRange(0.96f, 1.05f), 0.0f, Attenuation);
	}
}

void AChaosImpactSimaeBird::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AChaosImpactSimaeBird, BirdState);
	DOREPLIFETIME(AChaosImpactSimaeBird, TargetCharacter);
	DOREPLIFETIME(AChaosImpactSimaeBird, SourcePawn);
	DOREPLIFETIME(AChaosImpactSimaeBird, FlockId);
	DOREPLIFETIME(AChaosImpactSimaeBird, BirdIndex);
	DOREPLIFETIME(AChaosImpactSimaeBird, FirstPerchedAt);
	DOREPLIFETIME(AChaosImpactSimaeBird, NextPeckAt);
	DOREPLIFETIME(AChaosImpactSimaeBird, LastPeckAt);
	DOREPLIFETIME(AChaosImpactSimaeBird, PecksDelivered);
}

AChaosImpactSimaeFeatherBurst::AChaosImpactSimaeFeatherBurst()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;
	BurstRoot = CreateDefaultSubobject<USceneComponent>(TEXT("BurstRoot"));
	SetRootComponent(BurstRoot);
	BurstLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("BurstLight"));
	BurstLight->SetupAttachment(BurstRoot);
	BurstLight->SetLightColor(FLinearColor(0.55f, 0.86f, 1.0f));
	BurstLight->SetCastShadows(false);
	BurstLight->SetAttenuationRadius(700.0f);
}

void AChaosImpactSimaeFeatherBurst::Play(UWorld* World, const FVector& Location, const float Scale)
{
	if (!World || World->GetNetMode() == NM_DedicatedServer) { return; }
	const FTransform Where(FRotator::ZeroRotator, Location);
	if (AChaosImpactSimaeFeatherBurst* Burst = World->SpawnActorDeferred<AChaosImpactSimaeFeatherBurst>(StaticClass(),
		Where, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
	{
		Burst->BurstScale = Scale;
		Burst->FinishSpawning(Where);
	}
}

void AChaosImpactSimaeFeatherBurst::BeginPlay()
{
	Super::BeginPlay();
	using namespace ChaosImpactBallTypes;
	SetLifeSpan(1.35f);
	UStaticMesh* Shape = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UMaterialInstanceDynamic* White = MakeEmissive(this, FLinearColor(0.93f, 0.98f, 1.0f), 0.7f);
	UMaterialInstanceDynamic* Blue = MakeEmissive(this, FLinearColor(0.12f, 0.82f, 1.0f), 1.2f);
	FRandomStream Stream(static_cast<int32>(GetUniqueID()));
	const int32 Amount = FMath::RoundToInt(40.0f * BurstScale);
	for (int32 Index = 0; Shape && Index < Amount; ++Index)
	{
		FFeather& Feather = Feathers.AddDefaulted_GetRef();
		UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(this);
		Mesh->SetStaticMesh(Shape);
		Mesh->SetMaterial(0, Index % 5 == 0 ? Blue : White);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetCastShadow(false);
		Mesh->SetupAttachment(BurstRoot);
		Mesh->RegisterComponent();
		Feather.Mesh = Mesh;
		Feather.Size = Stream.FRandRange(0.8f, 1.4f) * BurstScale;
		const float Angle = Index * 2.39996323f;
		const FVector Outward(FMath::Cos(Angle), FMath::Sin(Angle), Stream.FRandRange(0.15f, 1.15f));
		Feather.Velocity = Outward.GetSafeNormal() * Stream.FRandRange(360.0f, 900.0f) * BurstScale;
		Feather.Spin = Stream.VRand() * Stream.FRandRange(160.0f, 500.0f);
		Mesh->SetRelativeRotation(Stream.VRand().Rotation());
		Mesh->SetRelativeScale3D(FVector(0.25f, 0.065f, 0.025f) * Feather.Size);
	}
	RingMaterial = MakeAdditive(this, FLinearColor(0.72f, 0.95f, 1.0f), 3.5f);
	ShockRing = ChaosImpactLightning::CreateComponent(this, BurstRoot, RingMaterial);
	BurstLight->SetIntensity(9000.0f * BurstScale);
	if (UNiagaraSystem* Shatter = LoadEffect(Effects::Shatter))
	{
		if (UNiagaraComponent* Effect = UNiagaraFunctionLibrary::SpawnSystemAttached(Shatter, BurstRoot, NAME_None,
			FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset, false))
		{
			Effect->SetRelativeScale3D(FVector(BurstScale));
			SetEffectColor(Effect, TEXT("Base Color"), FLinearColor(0.95f, 0.97f, 1.0f));
			SetEffectFloat(Effect, TEXT("Burst Amount"), 3.0f * BurstScale);
			SetEffectVector(Effect, TEXT("Hit Normal"), FVector::UpVector);
			SetEffectVector(Effect, TEXT("Hit Direction"), -FVector::UpVector);
		}
	}
	if (UNiagaraSystem* System = LoadEffect(Effects::SparkBurst))
	{
		if (UNiagaraComponent* Sparks = UNiagaraFunctionLibrary::SpawnSystemAttached(System, BurstRoot, NAME_None,
			FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset, false))
		{
			Sparks->SetRelativeScale3D(FVector(BurstScale));
			SetEffectColor(Sparks, TEXT("Color"), FLinearColor(0.7f, 0.94f, 1.0f));
			SetEffectColor(Sparks, TEXT("Spark Color"), FLinearColor(0.7f, 0.94f, 1.0f));
		}
	}
}

void AChaosImpactSimaeFeatherBurst::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	for (FFeather& Feather : Feathers)
	{
		if (UStaticMeshComponent* Mesh = Feather.Mesh.Get())
		{
			Feather.Velocity *= FMath::Exp(-2.0f * DeltaSeconds);
			Feather.Velocity.Z -= 130.0f * DeltaSeconds;
			Mesh->AddLocalRotation(FRotator(Feather.Spin.Y, Feather.Spin.Z, Feather.Spin.X) * DeltaSeconds);
			Mesh->AddRelativeLocation(Feather.Velocity * DeltaSeconds);
			const float Fade = FMath::Clamp((1.25f - Age) / 0.5f, 0.0f, 1.0f);
			Mesh->SetRelativeScale3D(FVector(0.25f, 0.065f, 0.025f) * Feather.Size * Fade);
		}
	}
	if (ShockRing)
	{
		ChaosImpactIceMeshes::FMeshBuffers Rings;
		const float Fade = FMath::Clamp(1.0f - Age / 0.5f, 0.0f, 1.0f);
		ChaosImpactLightning::AppendRing(Rings, FVector::UpVector * 5.0f,
			(35.0f + 540.0f * Age) * BurstScale, 12.0f * Fade, 64);
		if (BurstScale >= BigBurstScale && Age > 0.08f)
		{
			// A second, faster ring right behind the first.
			const float Second = Age - 0.08f;
			ChaosImpactLightning::AppendRing(Rings, FVector::UpVector * 9.0f, (20.0f + 420.0f * Second) * BurstScale,
				7.0f * FMath::Clamp(1.0f - Second / 0.5f, 0.0f, 1.0f), 64);
		}
		ChaosImpactLightning::SetMesh(ShockRing, Rings);
		RingMaterial->SetScalarParameterValue(TEXT("Intensity"), 3.5f * FMath::Clamp(1.0f - Age / 0.7f, 0.0f, 1.0f));
	}
	BurstLight->SetIntensity(9000.0f * BurstScale * FMath::Exp(-Age * 15.0f));
}

namespace
{
	/** One little bird (the flock's own model and look) under Parent at Scale; returns its wings, for flapping. */
	UStaticMeshComponent* BuildBirdModel(AActor* Owner, USceneComponent* Parent, const float Scale)
	{
		using namespace ChaosImpactBallTypes;
		UMaterialInterface* TexturedBase = LoadObject<UMaterialInterface>(nullptr, SimaeAssets::TexturedMaterial);
		UTexture* Texture = LoadObject<UTexture>(nullptr, SimaeAssets::BirdTexture);
		UMaterialInstanceDynamic* Look = TexturedBase && Texture ? UMaterialInstanceDynamic::Create(TexturedBase, Owner) : nullptr;
		if (Look)
		{
			Look->SetTextureParameterValue(TEXT("BodyTexture"), Texture);
		}
		UMaterialInstanceDynamic* White = Look ? nullptr : MakeBirdMaterial(Owner, FLinearColor(0.95f, 0.96f, 0.98f), 0.82f);
		UMaterialInstanceDynamic* Dark = Look ? nullptr : MakeBirdMaterial(Owner, FLinearColor(0.055f, 0.043f, 0.036f), 0.72f);
		const TCHAR* const Parts[] = {SimaeAssets::BirdBody, SimaeAssets::BirdTail, SimaeAssets::BirdWing, SimaeAssets::BirdEye,
			SimaeAssets::BirdCrest};
		UStaticMeshComponent* Wings = nullptr;
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Parts); ++Index)
		{
			UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(Owner);
			Part->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, Parts[Index]));
			for (int32 Slot = 0; Slot < FMath::Max(Part->GetNumMaterials(), 1); ++Slot)
			{
				Part->SetMaterial(Slot, Look ? Look : (Index == 0 ? White : Dark));
			}
			Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Part->SetCastShadow(false);
			Part->SetupAttachment(Parent);
			Part->SetRelativeScale3D(FVector(Scale));
			Part->RegisterComponent();
			Wings = Index == 2 ? Part : Wings;
		}
		return Wings;
	}

	void PlayChirpAt(UWorld* World, const FVector& Location, const int32 Choice, const float Volume)
	{
		if (!World || World->GetNetMode() == NM_DedicatedServer)
		{
			return;
		}
		using namespace ChaosImpactBallTypes;
		const TCHAR* Paths[] = {SimaeAssets::Chirp01, SimaeAssets::Chirp02, SimaeAssets::Chirp03};
		if (USoundBase* Sound = LoadObject<USoundBase>(nullptr, Paths[FMath::Abs(Choice) % UE_ARRAY_COUNT(Paths)]))
		{
			USoundAttenuation* Attenuation = NewObject<USoundAttenuation>(GetTransientPackage());
			Attenuation->Attenuation.bAttenuate = true;
			Attenuation->Attenuation.bSpatialize = true;
			Attenuation->Attenuation.AttenuationShapeExtents = FVector(400.0f, 0.0f, 0.0f);
			Attenuation->Attenuation.FalloffDistance = 2600.0f;
			UGameplayStatics::PlaySoundAtLocation(World, Sound, Location, Volume, FMath::FRandRange(0.97f, 1.06f), 0.0f, Attenuation);
		}
	}

	constexpr int32 SparkleCount = 8;
	constexpr int32 FeatherCount = 18;
	constexpr float FeatherSeconds = 0.9f;
	const FVector FeatherShape(0.13f, 0.04f, 0.012f);
}

void ChaosImpactBallTypes::BuildSimaeBallLook(AActor* Owner, USceneComponent* Parent, FSimaeBallLook& Out)
{
	if (!Owner || !Parent || Out.IsBuilt())
	{
		return;
	}
	const auto Register = [](USceneComponent* Component, USceneComponent* AttachTo)
	{
		Component->SetupAttachment(AttachTo);
		Component->RegisterComponent();
	};
	// Follows the ball, but neither its spin nor its scale.
	USceneComponent* Root = NewObject<USceneComponent>(Owner);
	Root->SetUsingAbsoluteRotation(true);
	Root->SetUsingAbsoluteScale(true);
	Register(Root, Parent);
	Out.Root = Root;

	UPointLightComponent* Light = NewObject<UPointLightComponent>(Owner);
	Light->SetLightColor(FLinearColor(0.72f, 0.92f, 1.0f));
	Light->SetAttenuationRadius(360.0f);
	Light->SetCastShadows(false);
	Light->SetIntensity(0.0f);
	Register(Light, Root);
	Out.Light = Light;

	const auto MakeShape = [Owner, Register, Root](const TCHAR* Path, UMaterialInterface* Material) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(Owner);
		Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, Path));
		Mesh->SetMaterial(0, Material);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetCastShadow(false);
		Register(Mesh, Root);
		return Mesh;
	};
	Out.Ripples = ChaosImpactLightning::CreateComponent(Owner, Root, MakeAdditive(Owner, FLinearColor(0.75f, 0.95f, 1.0f), 1.6f));

	for (int32 Index = 0; Index < 3; ++Index)
	{
		USceneComponent* Pivot = NewObject<USceneComponent>(Owner);
		Register(Pivot, Root);
		Out.Escorts.Add(Pivot);
		Out.EscortWings.Add(BuildBirdModel(Owner, Pivot, 0.27f));
	}
	UMaterialInstanceDynamic* Glint = MakeEmissive(Owner, FLinearColor(0.9f, 0.98f, 1.0f), 2.0f);
	UMaterialInstanceDynamic* BlueGlint = MakeEmissive(Owner, FLinearColor(0.35f, 0.85f, 1.0f), 2.0f);
	for (int32 Index = 0; Index < SparkleCount; ++Index)
	{
		Out.Sparkles.Add(MakeShape(TEXT("/Engine/BasicShapes/Sphere.Sphere"), Index % 3 == 0 ? BlueGlint : Glint));
	}
	UMaterialInstanceDynamic* Down = MakeEmissive(Owner, FLinearColor(0.93f, 0.96f, 1.0f), 0.7f);
	for (int32 Index = 0; Index < FeatherCount; ++Index)
	{
		UStaticMeshComponent* Feather = MakeShape(TEXT("/Engine/BasicShapes/Sphere.Sphere"), Down);
		// Shed feathers stay where they fell, whatever the ball does next.
		Feather->SetUsingAbsoluteLocation(true);
		Feather->SetUsingAbsoluteRotation(true);
		Feather->SetUsingAbsoluteScale(true);
		Feather->SetVisibility(false);
		FSimaeBallLook::FDrift& Drift = Out.Feathers.AddDefaulted_GetRef();
		Drift.Mesh = Feather;
	}
}

void ChaosImpactBallTypes::UpdateSimaeBallLook(AActor* Owner, FSimaeBallLook& Look, const float Time, const float DeltaSeconds,
	const bool bPickup, const bool bFlying, const bool bVisible)
{
	USceneComponent* Root = Look.Root.Get();
	UWorld* World = Owner ? Owner->GetWorld() : nullptr;
	if (!Root || !World)
	{
		return;
	}
	const bool bShow = bVisible && (bPickup || bFlying);
	if (bShow != Look.bShown)
	{
		Root->SetVisibility(bShow, true);
		Look.bShown = bShow;
	}
	if (!bShow)
	{
		return;
	}
	const FVector Center = Root->GetComponentLocation();

	// A freshly spawned one announces itself: a puff of feathers and a chirp.
	if (!Look.bArrivalChecked)
	{
		Look.bArrivalChecked = true;
		if (bPickup && Owner->GetGameTimeSinceCreation() < 1.0f)
		{
			Look.ArrivedAt = Time;
			AChaosImpactSimaeFeatherBurst::Play(World, Center, 0.7f);
			PlayChirpAt(World, Center, FMath::RandRange(0, 2), 0.3f);
		}
	}
	const float SinceArrival = Time - Look.ArrivedAt;
	const float ArrivalGlow = FMath::Exp(-FMath::Max(SinceArrival, 0.0f) * 2.2f);

	float Ground = -36.0f;
	if (bPickup)
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ChaosImpactSimaeLookGround), false, Owner);
		if (World->LineTraceSingleByObjectType(Hit, Center, Center - FVector::UpVector * 300.0f,
			FCollisionObjectQueryParams(ECC_WorldStatic), Params))
		{
			Ground = static_cast<float>(Hit.ImpactPoint.Z - Center.Z) + 1.5f;
		}
	}

	if (UPointLightComponent* Light = Look.Light.Get())
	{
		Light->SetIntensity(bFlying ? 2200.0f : 1500.0f + 400.0f * FMath::Sin(Time * 3.0f) + 3000.0f * ArrivalGlow);
	}

	// Ripples spreading over the ground round it, and one ring that stays.
	if (UProceduralMeshComponent* Ripples = Look.Ripples.Get())
	{
		if (bPickup)
		{
			ChaosImpactIceMeshes::FMeshBuffers Rings;
			for (int32 Wave = 0; Wave < 2; ++Wave)
			{
				const float Phase = FMath::Frac(Time / 1.1f + Wave * 0.5f);
				ChaosImpactLightning::AppendRing(Rings, FVector(0.0f, 0.0f, Ground), 34.0f + 90.0f * Phase, 4.5f * (1.0f - Phase), 56);
			}
			ChaosImpactLightning::AppendRing(Rings, FVector(0.0f, 0.0f, Ground), 52.0f, 2.0f + 0.8f * FMath::Sin(Time * 4.0f), 56);
			ChaosImpactLightning::SetMesh(Ripples, Rings);
		}
		else
		{
			Ripples->ClearAllMeshSections();
		}
	}

	// Three little birds: circling over it while it waits, closing in to escort it once thrown.
	Look.EscortBlend = FMath::FInterpTo(Look.EscortBlend, bFlying ? 1.0f : 0.0f, DeltaSeconds, 6.0f);
	const float Blend = Look.EscortBlend;
	for (int32 Index = 0; Index < Look.Escorts.Num(); ++Index)
	{
		USceneComponent* Pivot = Look.Escorts[Index].Get();
		if (!Pivot)
		{
			continue;
		}
		const float Angle = Time * FMath::Lerp(1.7f, 7.5f, Blend) + Index * UE_TWO_PI / 3.0f;
		const float Radius = FMath::Lerp(64.0f, 42.0f, Blend);
		const float Height = FMath::Lerp(56.0f + 7.0f * FMath::Sin(Time * 3.1f + Index * 2.0f),
			12.0f * FMath::Sin(Time * 9.0f + Index), Blend);
		Pivot->SetRelativeLocation(FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, Height));
		Pivot->SetRelativeRotation(FRotator(0.0f, FMath::RadiansToDegrees(Angle) + 90.0f, 0.0f));
		if (UStaticMeshComponent* Wings = Look.EscortWings.IsValidIndex(Index) ? Look.EscortWings[Index].Get() : nullptr)
		{
			const float Flap = 0.55f + 0.45f * FMath::Abs(FMath::Sin(Time * FMath::Lerp(11.0f, 16.0f, Blend) * UE_PI + Index));
			Wings->SetRelativeScale3D(FVector(0.27f, 0.27f, 0.27f * Flap));
		}
	}

	// Sparkles spiralling up round the waiting ball.
	for (int32 Index = 0; Index < Look.Sparkles.Num(); ++Index)
	{
		UStaticMeshComponent* Sparkle = Look.Sparkles[Index].Get();
		if (!Sparkle)
		{
			continue;
		}
		Sparkle->SetVisibility(bPickup);
		if (!bPickup)
		{
			continue;
		}
		const float Angle = Time * 1.4f + Index * UE_TWO_PI / SparkleCount;
		const float Rise = FMath::Fmod(Time * 38.0f + Index * 17.0f, 130.0f);
		const float Radius = 46.0f + 12.0f * FMath::Sin(Time * 2.0f + Index);
		Sparkle->SetRelativeLocation(FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, Ground + 10.0f + Rise));
		const float Twinkle = 0.5f + 0.5f * FMath::Sin(Time * 9.0f + Index * 1.7f);
		Sparkle->SetRelativeScale3D(FVector((0.022f + 0.03f * Twinkle) * (1.0f - Rise / 130.0f)));
	}

	// In flight it sheds a trail of down.
	if (bFlying && Look.Feathers.Num() > 0)
	{
		Look.FeatherClock -= DeltaSeconds;
		while (Look.FeatherClock <= 0.0f)
		{
			Look.FeatherClock += 0.045f;
			FSimaeBallLook::FDrift& Drift = Look.Feathers[Look.NextFeather++ % Look.Feathers.Num()];
			if (UStaticMeshComponent* Mesh = Drift.Mesh.Get())
			{
				Mesh->SetWorldLocation(Center + FMath::VRand() * 12.0f);
				Mesh->SetWorldRotation(FMath::VRand().Rotation());
				Drift.Velocity = FMath::VRand() * 70.0f - FVector::UpVector * 25.0f;
				Drift.Spin = FRotator(FMath::FRandRange(-400.0f, 400.0f), FMath::FRandRange(-300.0f, 300.0f),
					FMath::FRandRange(-500.0f, 500.0f));
				Drift.Age = 0.0f;
			}
		}
	}
	for (FSimaeBallLook::FDrift& Drift : Look.Feathers)
	{
		UStaticMeshComponent* Mesh = Drift.Mesh.Get();
		if (!Mesh)
		{
			continue;
		}
		Drift.Age += DeltaSeconds;
		const bool bAlive = Drift.Age < FeatherSeconds;
		Mesh->SetVisibility(bAlive);
		if (!bAlive)
		{
			continue;
		}
		Drift.Velocity *= FMath::Exp(-2.5f * DeltaSeconds);
		Drift.Velocity.Z -= 40.0f * DeltaSeconds;
		Mesh->AddWorldOffset(Drift.Velocity * DeltaSeconds);
		Mesh->AddWorldRotation(Drift.Spin * DeltaSeconds);
		Mesh->SetWorldScale3D(FeatherShape * (1.0f - Drift.Age / FeatherSeconds));
	}
}

