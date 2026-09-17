#include "ChaosImpactTornado.h"

#include "ChaosImpact.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactBallTypes.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactIceMeshes.h"
#include "ChaosImpactLightning.h"
#include "ChaosImpactTrainingTarget.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "ProceduralMeshComponent.h"

namespace
{
	constexpr float TornadoPathStep = 1.0f / 60.0f;
	/** The path is traced this high off the floor, so low steps and ramps do not turn it. */
	constexpr float TornadoTraceHeight = 70.0f;
	constexpr float TornadoTraceRadius = 60.0f;
	/** A report from a remote player's screen may be this much further off than a local contact. */
	constexpr float TornadoReportTolerance = 380.0f;

	const FLinearColor TornadoColor(0.3f, 1.0f, 0.4f);
	const FLinearColor TornadoCoreColor(0.72f, 1.0f, 0.68f);
	const FLinearColor TornadoDeepColor(0.08f, 0.7f, 0.3f);

	float TornadoEaseOut(const float T)
	{
		return 1.0f - FMath::Pow(1.0f - FMath::Clamp(T, 0.0f, 1.0f), 3.0f);
	}

	/** Funnel radius at a height fraction: a thin foot widening toward the top. */
	float TornadoFunnelRadius(const float HeightFraction)
	{
		return 34.0f + 190.0f * FMath::Pow(FMath::Clamp(HeightFraction, 0.0f, 1.0f), 1.35f);
	}

	/** A strip through Points, Width wide at each point, turned to face the camera. */
	void AppendTornadoRibbon(ChaosImpactIceMeshes::FMeshBuffers& Mesh, const TArray<FVector>& Points,
		const TArray<float>& Widths, const FVector& ViewDirection)
	{
		if (Points.Num() < 2)
		{
			return;
		}
		const int32 First = Mesh.Vertices.Num();
		for (int32 Index = 0; Index < Points.Num(); ++Index)
		{
			const FVector Along = (Points[FMath::Min(Index + 1, Points.Num() - 1)] - Points[FMath::Max(Index - 1, 0)]).GetSafeNormal();
			FVector Side = FVector::CrossProduct(Along, ViewDirection).GetSafeNormal();
			if (Side.IsNearlyZero())
			{
				Side = FVector::CrossProduct(Along, FVector::UpVector).GetSafeNormal();
			}
			const float HalfWidth = Widths[Index] * 0.5f;
			const float V = static_cast<float>(Index) / (Points.Num() - 1);
			Mesh.Vertices.Add(Points[Index] + Side * HalfWidth);
			Mesh.Vertices.Add(Points[Index] - Side * HalfWidth);
			Mesh.Normals.Add(-ViewDirection);
			Mesh.Normals.Add(-ViewDirection);
			Mesh.UVs.Add(FVector2D(0.0f, V));
			Mesh.UVs.Add(FVector2D(1.0f, V));
		}
		for (int32 Index = 0; Index + 1 < Points.Num(); ++Index)
		{
			const int32 A = First + Index * 2;
			Mesh.Triangles.Append({A, A + 2, A + 1, A + 1, A + 2, A + 3});
			// Both faces, whichever side the camera is on.
			Mesh.Triangles.Append({A, A + 1, A + 2, A + 1, A + 3, A + 2});
		}
	}
}

AChaosImpactTornado::AChaosImpactTornado()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(false);
	SetNetUpdateFrequency(10.0f);
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
}

AChaosImpactTornado* AChaosImpactTornado::Release(UWorld* World, const FVector& From, const FVector& Direction,
	APawn* Source)
{
	if (!World)
	{
		return nullptr;
	}
	FVector Floor = From - FVector::UpVector * 90.0f;
	FHitResult GroundHit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ChaosImpactTornadoGround), false);
	if (Source)
	{
		Params.AddIgnoredActor(Source);
	}
	if (World->LineTraceSingleByObjectType(GroundHit, From + FVector::UpVector * 40.0f, From - FVector::UpVector * 1500.0f,
		FCollisionObjectQueryParams(ECC_WorldStatic), Params))
	{
		Floor = GroundHit.ImpactPoint;
	}
	FVector Flat(Direction.X, Direction.Y, 0.0f);
	if (!Flat.Normalize())
	{
		Flat = Source ? Source->GetActorForwardVector().GetSafeNormal2D() : FVector::ForwardVector;
	}
	const FTransform SpawnTransform(Flat.Rotation(), Floor);
	AChaosImpactTornado* Tornado = World->SpawnActorDeferred<AChaosImpactTornado>(StaticClass(), SpawnTransform, nullptr,
		Source, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Tornado)
	{
		return nullptr;
	}
	Tornado->Origin = Floor;
	Tornado->Heading = Flat;
	const AGameStateBase* GameState = World->GetGameState();
	Tornado->StartServerTime = GameState ? GameState->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
	Tornado->SourcePawn = Source;
	Tornado->Seed = FMath::Rand();
	Tornado->FinishSpawning(SpawnTransform);
	UE_LOG(LogChaosImpact, Log, TEXT("Wind tornado released at %s heading %s by %s"), *Floor.ToCompactString(),
		*Flat.ToCompactString(), *GetNameSafe(Source));
	return Tornado;
}

void AChaosImpactTornado::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		SetLifeSpan(ActiveSeconds + CollapseSeconds + 1.5f);
	}
	Center = Origin;
	LastCenter = Origin;
	if (GetNetMode() != NM_DedicatedServer)
	{
		BuildPresentation();
	}
}

void AChaosImpactTornado::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority())
	{
		ReleaseCarriedBalls();
	}
	Super::EndPlay(EndPlayReason);
}

void AChaosImpactTornado::CatchBalls(const float DeltaSeconds)
{
	UWorld* World = GetWorld();
	const float Delta = FMath::Clamp(DeltaSeconds, 0.0f, 0.1f);
	for (TActorIterator<AChaosImpactBall> It(World); It; ++It)
	{
		AChaosImpactBall* Ball = *It;
		const FVector Offset = Ball->GetActorLocation() - Center;
		const FVector Flat(Offset.X, Offset.Y, 0.0f);
		if (Offset.Z < -60.0f || Offset.Z > FunnelHeight)
		{
			continue;
		}
		if (Ball->IsFlyingOnServer())
		{
			// Flung round the funnel and back out, faster, in a new direction.
			if (Deflected.Contains(Ball) || Flat.SizeSquared() > FMath::Square(CatchRadius + 30.0f))
			{
				continue;
			}
			Deflected.Add(Ball);
			const FVector Outward = Flat.IsNearlyZero() ? PathHeading : Flat.GetSafeNormal();
			const FVector Around = FVector::CrossProduct(FVector::UpVector, Outward);
			const FVector Direction = (Around * 0.85f + Outward * 0.55f).GetSafeNormal2D();
			const FVector OldVelocity = Ball->GetBallVelocity();
			const float Speed = FMath::Max(static_cast<float>(OldVelocity.Size2D()), DeflectSpeed);
			FVector NewVelocity = Direction * Speed;
			NewVelocity.Z = FMath::Max(static_cast<float>(OldVelocity.Z), 0.0f) + 180.0f;
			if (Ball->DeflectByWind(NewVelocity))
			{
				UE_LOG(LogChaosImpact, Log, TEXT("Tornado turned a %s ball from %s to %s"),
					ChaosImpactBallTypes::GetInternalName(Ball->GetBallType()), *OldVelocity.ToCompactString(), *NewVelocity.ToCompactString());
				MulticastGust(Ball->GetActorLocation(), false);
			}
			continue;
		}
		if (Ball->IsPickup() && !Ball->IsCarriedByWind() && CarriedBalls.Num() < MaxCarriedBalls
			&& Flat.SizeSquared() <= FMath::Square(CatchRadius + 40.0f) && Ball->CatchInWind())
		{
			FCarriedBall& Carried = CarriedBalls.AddDefaulted_GetRef();
			Carried.Ball = Ball;
			Carried.Angle = FMath::Atan2(static_cast<float>(Flat.Y), static_cast<float>(Flat.X));
			Carried.Height = FMath::Max(static_cast<float>(Offset.Z), 20.0f);
			MulticastGust(Ball->GetActorLocation(), false);
		}
	}
	// Carried balls whirl round the funnel, rising and sinking.
	for (int32 Index = CarriedBalls.Num() - 1; Index >= 0; --Index)
	{
		FCarriedBall& Carried = CarriedBalls[Index];
		AChaosImpactBall* Ball = Carried.Ball.Get();
		if (!IsValid(Ball) || !Ball->IsCarriedByWind())
		{
			CarriedBalls.RemoveAt(Index);
			continue;
		}
		Carried.Age += Delta;
		Carried.Angle += (6.5f + Index * 0.4f) * Delta;
		const float Target = 110.0f + 90.0f * FMath::Sin(Carried.Age * 2.2f + Index * 1.3f) + Index * 14.0f;
		Carried.Height = FMath::FInterpTo(Carried.Height, Target, Delta, 3.0f);
		const float Radius = TornadoFunnelRadius(Carried.Height / FunnelHeight) * 0.8f + 20.0f;
		Ball->CarryInWind(Center + FVector(FMath::Cos(Carried.Angle) * Radius, FMath::Sin(Carried.Angle) * Radius, Carried.Height));
	}
}

void AChaosImpactTornado::ReleaseCarriedBalls()
{
	if (bBallsReleased && CarriedBalls.IsEmpty())
	{
		return;
	}
	bBallsReleased = true;
	UWorld* World = GetWorld();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ChaosImpactTornadoRelease), false);
	for (const FCarriedBall& Carried : CarriedBalls)
	{
		AChaosImpactBall* Ball = Carried.Ball.Get();
		if (!IsValid(Ball) || !Ball->IsCarriedByWind() || !World)
		{
			continue;
		}
		// Thrown out past the funnel's foot, but never through a wall.
		const FVector Outward(FMath::Cos(Carried.Angle), FMath::Sin(Carried.Angle), 0.0f);
		const FVector Lift(0.0f, 0.0f, TornadoTraceHeight);
		FVector Spot = Center + Outward * 230.0f;
		FHitResult Wall;
		if (World->SweepSingleByObjectType(Wall, Center + Lift, Spot + Lift, FQuat::Identity,
			FCollisionObjectQueryParams(ECC_WorldStatic), FCollisionShape::MakeSphere(40.0f), Params))
		{
			Spot = Wall.Location - Lift - Outward * 10.0f;
		}
		FVector Ground(Spot.X, Spot.Y, Origin.Z);
		FHitResult Floor;
		if (World->LineTraceSingleByObjectType(Floor, Spot + FVector(0.0f, 0.0f, 200.0f), Spot - FVector(0.0f, 0.0f, 600.0f),
			FCollisionObjectQueryParams(ECC_WorldStatic), Params))
		{
			Ground = Floor.ImpactPoint;
		}
		Ball->ReleaseFromWind(Ground, (Outward + FVector::CrossProduct(FVector::UpVector, Outward) * 0.6f) * 900.0f);
	}
	CarriedBalls.Reset();
	if (World && !IsActorBeingDestroyed())
	{
		MulticastGust(Center + FVector(0.0f, 0.0f, 60.0f), true);
	}
}

void AChaosImpactTornado::MulticastGust_Implementation(FVector_NetQuantize Location, const bool bBig)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	using namespace ChaosImpactBallTypes;
	if (UNiagaraSystem* GustSparks = LoadEffect(Effects::SparkBurst))
	{
		if (UNiagaraComponent* Burst = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, GustSparks, Location,
			FRotator::ZeroRotator, FVector(bBig ? 1.4f : 0.7f)))
		{
			SetEffectColor(Burst, TEXT("Color"), TornadoColor * 6.0f);
		}
	}
}

void AChaosImpactTornado::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(AChaosImpactTornado, Origin, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AChaosImpactTornado, Heading, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AChaosImpactTornado, StartServerTime, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AChaosImpactTornado, SourcePawn, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AChaosImpactTornado, Seed, COND_InitialOnly);
}

double AChaosImpactTornado::GetServerNow() const
{
	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	return GameState ? GameState->GetServerWorldTimeSeconds() : World ? World->GetTimeSeconds() : 0.0;
}

void AChaosImpactTornado::StepPath(const float Seconds)
{
	UWorld* World = GetWorld();
	const float T = SimulatedSeconds;
	// Weaves by turning its direction to and fro (a smooth snake). Each wave starts at its widest turn, so the
	// sideways sway is centred on the line thrown along; the side it swings to first and the pace vary.
	const float Sign = (Seed & 1) ? 1.0f : -1.0f;
	const float Pace = 4.6f + static_cast<float>(Seed % 97) / 97.0f * 0.8f;
	const float WeaveDegrees = Sign * (52.0f * FMath::Cos(T * Pace) + 12.0f * FMath::Cos(T * 1.7f));
	const FVector Direction = PathHeading.RotateAngleAxis(WeaveDegrees, FVector::UpVector);
	const FVector Move = Direction * TravelSpeed * Seconds;
	const FVector Lift(0.0f, 0.0f, TornadoTraceHeight);
	FHitResult Wall;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ChaosImpactTornadoPath), false);
	if (World && World->SweepSingleByObjectType(Wall, PathPosition + Lift, PathPosition + Move + Lift, FQuat::Identity,
		FCollisionObjectQueryParams(ECC_WorldStatic), FCollisionShape::MakeSphere(TornadoTraceRadius), Params)
		&& Wall.bBlockingHit && FMath::Abs(Wall.ImpactNormal.Z) < 0.7f)
	{
		// Rebounds off walls like a ball.
		const FVector Normal = FVector(Wall.ImpactNormal.X, Wall.ImpactNormal.Y, 0.0f).GetSafeNormal();
		PathHeading = PathHeading.MirrorByVector(Normal).GetSafeNormal2D();
		if (FVector::DotProduct(PathHeading, Normal) < 0.2f)
		{
			PathHeading = (PathHeading + Normal * 0.6f).GetSafeNormal2D();
		}
		PathPosition = Wall.Location - Lift + Normal * 2.0f;
		PathPosition.Z = Origin.Z;
		return;
	}
	PathPosition += Move;
}

bool AChaosImpactTornado::CanCatch(const AActor* Victim) const
{
	if (!IsValid(Victim) || Victim == SourcePawn || Caught.Contains(Victim)
		|| AChaosImpactGameState::AreTeammates(GetWorld(), SourcePawn, Victim))
	{
		return false;
	}
	const AChaosImpactCharacter* Character = Cast<AChaosImpactCharacter>(Victim);
	return !Character || !Character->IsEliminated();
}

void AChaosImpactTornado::CatchCharacters()
{
	UWorld* World = GetWorld();
	AController* SourceController = SourcePawn ? SourcePawn->GetController() : nullptr;
	for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
	{
		AChaosImpactCharacter* Character = *It;
		if (!CanCatch(Character) || Character->IsDashingForPresentation())
		{
			continue;
		}
		const FVector Offset = Character->GetActorLocation() - Center;
		const float Reach = CatchRadius + Character->GetCapsuleComponent()->GetScaledCapsuleRadius();
		if (FVector(Offset.X, Offset.Y, 0.0f).SizeSquared() > FMath::Square(Reach) || Offset.Z < -120.0f
			|| Offset.Z > FunnelHeight)
		{
			continue;
		}
		if (HasAuthority())
		{
			// Remote players judge their own contact (see TryApplyReportedHit).
			if (Character->IsRemotePlayerOnServer())
			{
				continue;
			}
			Caught.Add(Character);
			UGameplayStatics::ApplyDamage(Character, 1.0f, SourceController, this, nullptr);
			Character->BeginWindCarry(this);
		}
		else if (Character->IsLocallyControlled())
		{
			UE_LOG(LogChaosImpact, Log, TEXT("Tornado caught this screen's %s at sim %.2f"), *GetNameSafe(Character->GetPlayerState()),
				SimulatedSeconds);
			Caught.Add(Character);
			Character->BeginWindCarry(this);
			Character->ServerReportTornadoHit(this);
		}
	}
	if (!HasAuthority())
	{
		return;
	}
	for (TActorIterator<AChaosImpactTrainingTarget> It(World); It; ++It)
	{
		if (!It->IsDefeated() && CanCatch(*It) && FVector::DistSquared2D(It->GetActorLocation(), Center) <= FMath::Square(CatchRadius + 50.0f))
		{
			Caught.Add(*It);
			UGameplayStatics::ApplyDamage(*It, 1.0f, SourceController, this, nullptr);
		}
	}
}

bool AChaosImpactTornado::TryApplyReportedHit(AChaosImpactCharacter* Victim)
{
	const float Age = static_cast<float>(GetServerNow() - StartServerTime);
	const bool bAccepted = HasAuthority() && CanCatch(Victim) && Age <= ActiveSeconds + 0.4f
		&& FVector::DistSquared2D(Victim->GetActorLocation(), Center) <= FMath::Square(CatchRadius + TornadoReportTolerance);
	UE_LOG(LogChaosImpact, Log, TEXT("Reported tornado hit %s: victim=%s distance=%.0f age=%.2f"),
		bAccepted ? TEXT("accepted") : TEXT("rejected"), *GetNameSafe(Victim),
		Victim ? FVector::Dist2D(Victim->GetActorLocation(), Center) : -1.0f, Age);
	if (!bAccepted)
	{
		return false;
	}
	Caught.Add(Victim);
	UGameplayStatics::ApplyDamage(Victim, 1.0f, SourcePawn ? SourcePawn->GetController() : nullptr, this, nullptr);
	// Everyone else sees the owner carried (the owner already moves it).
	Victim->BeginWindCarry(this);
	return true;
}

void AChaosImpactTornado::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!GetWorld() || StartServerTime <= 0.0)
	{
		return;
	}
	if (!bPathStarted)
	{
		bPathStarted = true;
		PathPosition = Origin;
		PathHeading = FVector(Heading).GetSafeNormal2D();
		if (PathHeading.IsNearlyZero())
		{
			PathHeading = FVector::ForwardVector;
		}
	}
	const float Age = FMath::Max(0.0f, static_cast<float>(GetServerNow() - StartServerTime));
	const float Target = FMath::Min(Age, ActiveSeconds);
	while (SimulatedSeconds + TornadoPathStep <= Target)
	{
		StepPath(TornadoPathStep);
		SimulatedSeconds += TornadoPathStep;
	}
	Center = PathPosition;
	SetActorLocation(Center);
	bActive = Age < ActiveSeconds;
#if !UE_BUILD_SHIPPING
	// Development (-CIWindLog): the path at fixed points of its own time, and who and what is near, on every machine.
	static const bool bWindLog = FParse::Param(FCommandLine::Get(), TEXT("CIWindLog"));
	if (bWindLog && SimulatedSeconds >= NextWindLogAt && NextWindLogAt <= ActiveSeconds)
	{
		NextWindLogAt += 0.5f;
		FString Near;
		for (TActorIterator<AChaosImpactCharacter> It(GetWorld()); It; ++It)
		{
			if (FVector::Dist2D(It->GetActorLocation(), Center) < 900.0f && It->GetPlayerState())
			{
				Near += FString::Printf(TEXT(" [%s hp=%.0f d=%.0f at=%s]"), *It->GetPlayerState()->GetPlayerName(),
					It->GetHealth(), FVector::Dist2D(It->GetActorLocation(), Center), *It->GetActorLocation().ToCompactString());
			}
		}
		for (TActorIterator<AChaosImpactBall> It(GetWorld()); It; ++It)
		{
			if (FVector::Dist2D(It->GetActorLocation(), Center) < 900.0f && !It->IsHidden())
			{
				Near += FString::Printf(TEXT(" [ball pickup=%d d=%.0f z=%.0f]"), It->IsPickup(),
					FVector::Dist2D(It->GetActorLocation(), Center), It->GetActorLocation().Z - Center.Z);
			}
		}
		UE_LOG(LogChaosImpact, Log, TEXT("WindLog seed=%d authority=%d sim=%.2f center=%s%s"), Seed, HasAuthority(),
			SimulatedSeconds, *Center.ToCompactString(), *Near);
	}
#endif
	if (bActive)
	{
		CatchCharacters();
	}
	if (HasAuthority())
	{
		if (bActive)
		{
			CatchBalls(DeltaSeconds);
		}
		else
		{
			ReleaseCarriedBalls();
		}
	}
	if (bPresentationBuilt)
	{
		UpdatePresentation(Age, DeltaSeconds);
	}
}

UStaticMeshComponent* AChaosImpactTornado::AddShape(UStaticMesh* Mesh, UMaterialInstanceDynamic* Material)
{
	UStaticMeshComponent* Shape = NewObject<UStaticMeshComponent>(this);
	Shape->SetupAttachment(SceneRoot);
	Shape->SetStaticMesh(Mesh);
	Shape->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Shape->SetCastShadow(false);
	Shape->SetGenerateOverlapEvents(false);
	Shape->RegisterComponent();
	Shape->SetMaterial(0, Material);
	return Shape;
}

void AChaosImpactTornado::BuildPresentation()
{
	using namespace ChaosImpactBallTypes;
	FRandomStream Stream(Seed);
	UMaterialInstanceDynamic* StrandLook = MakeAdditive(this, FLinearColor(0.16f, 1.0f, 0.26f), 1.0f);
	FunnelMaterial = StrandLook;
	Funnel = ChaosImpactLightning::CreateComponent(this, SceneRoot, StrandLook);
	UMaterialInstanceDynamic* RingLook = MakeAdditive(this, TornadoCoreColor, 1.2f);
	RingMaterial = RingLook;
	GroundRings = ChaosImpactLightning::CreateComponent(this, SceneRoot, RingLook);

	UStaticMesh* Cone = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	for (int32 Index = 0; Index < 2 && Cone; ++Index)
	{
		UMaterialInstanceDynamic* ShellLook = MakeAdditive(this, Index == 0 ? TornadoColor : TornadoDeepColor, 0.6f, 1.0f);
		ShellMaterials.Add(ShellLook);
		Shells.Add(AddShape(Cone, ShellLook));
	}
	constexpr int32 DebrisCount = 28;
	for (int32 Index = 0; Index < DebrisCount && Cube; ++Index)
	{
		FDebris& Piece = Debris.AddDefaulted_GetRef();
		const bool bBright = Stream.FRand() < 0.45f;
		Piece.Mesh = AddShape(Cube, bBright ? MakeAdditive(this, TornadoCoreColor, 2.2f)
			: MakeEmissive(this, FLinearColor(0.1f, 0.34f, 0.14f), 0.6f));
		Piece.Angle = Stream.FRand() * UE_TWO_PI;
		Piece.Height = Stream.FRand() * FunnelHeight;
		Piece.Speed = Stream.FRandRange(0.7f, 1.4f);
		Piece.Size = Stream.FRandRange(0.06f, 0.15f);
	}

	UPointLightComponent* Glow = NewObject<UPointLightComponent>(this);
	Glow->SetupAttachment(SceneRoot);
	Glow->SetRelativeLocation(FVector(0.0f, 0.0f, 160.0f));
	Glow->SetLightColor(TornadoColor);
	Glow->SetIntensity(0.0f);
	Glow->SetAttenuationRadius(700.0f);
	Glow->SetCastShadows(false);
	Glow->RegisterComponent();
	Light = Glow;

	if (UNiagaraSystem* Smoke = LoadEffect(Effects::Smoke))
	{
		if (UNiagaraComponent* Dust = UNiagaraFunctionLibrary::SpawnSystemAttached(Smoke, SceneRoot, NAME_None,
			FVector(0.0f, 0.0f, 10.0f), FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset, false))
		{
			SetEffectColor(Dust, TEXT("Smoke Color"), FLinearColor(0.34f, 0.62f, 0.36f));
			BaseDust = Dust;
		}
	}
	if (UNiagaraSystem* Continuous = LoadEffect(Effects::WindSparks))
	{
		if (UNiagaraComponent* Glints = UNiagaraFunctionLibrary::SpawnSystemAttached(Continuous, SceneRoot, NAME_None,
			FVector(0.0f, 0.0f, 120.0f), FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset, false))
		{
			SetEffectColor(Glints, TEXT("Color"), TornadoColor * 4.0f);
			Sparks = Glints;
		}
	}
	// The release: a burst of dirt and a flash where it forms.
	if (UNiagaraSystem* Burst = LoadEffect(Effects::DirtBurstMedium))
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, Burst, Origin, FRotator::ZeroRotator, FVector(0.9f));
	}
	bPresentationBuilt = true;
	UpdatePresentation(0.0f, 0.0f);
}

void AChaosImpactTornado::UpdatePresentation(const float Age, const float DeltaSeconds)
{
	using namespace ChaosImpactBallTypes;
	const float Delta = FMath::Clamp(DeltaSeconds, 0.0f, 0.1f);
	PresentationAge += Delta;
	// Grows up out of the ground, whirls, then collapses into itself.
	const float Form = TornadoEaseOut(Age / 0.35f);
	const float CollapseT = FMath::Clamp((Age - ActiveSeconds) / CollapseSeconds, 0.0f, 1.0f);
	const float Size = Form * (1.0f - CollapseT * CollapseT);
	const bool bVisible = Size > 0.01f;
	const float Spin = PresentationAge * 7.5f;
	// The top trails behind as it moves, and sways.
	const FVector Moved = Center - LastCenter;
	LastCenter = Center;
	const FVector Trail = (Delta > 0.0f ? -Moved / Delta * 0.08f : FVector::ZeroVector).GetClampedToMaxSize(60.0f);
	const FVector Sway(FMath::Sin(PresentationAge * 2.3f) * 22.0f, FMath::Cos(PresentationAge * 1.9f) * 22.0f, 0.0f);
	const float Height = FunnelHeight * Size;
	const auto Axis = [&](const float Fraction)
	{
		return (Trail + Sway) * Fraction * Fraction + FVector(0.0f, 0.0f, Height * Fraction);
	};

	if (UProceduralMeshComponent* Strands = Funnel.Get())
	{
		ChaosImpactIceMeshes::FMeshBuffers Mesh;
		if (bVisible)
		{
			const FVector View = ChaosImpactLightning::GetViewDirection(GetWorld());
			constexpr int32 StrandCount = 7;
			constexpr int32 Points = 18;
			TArray<FVector> Line;
			TArray<float> Widths;
			for (int32 Strand = 0; Strand < StrandCount; ++Strand)
			{
				Line.Reset();
				Widths.Reset();
				const bool bInner = Strand >= 5;
				const float Offset = Strand * UE_TWO_PI / 5.0f + (bInner ? 0.7f : 0.0f);
				for (int32 Index = 0; Index < Points; ++Index)
				{
					const float Fraction = static_cast<float>(Index) / (Points - 1);
					// Winds round faster near the foot, and the whole funnel twists.
					const float Angle = Offset + Spin * (bInner ? 1.4f : 1.0f) * (1.25f - 0.35f * Fraction) + Fraction * 4.2f;
					const float Radius = TornadoFunnelRadius(Fraction) * Size * (bInner ? 0.45f : 1.0f)
						* (1.0f + 0.12f * FMath::Sin(PresentationAge * 6.0f + Strand + Fraction * 5.0f));
					Line.Add(Axis(Fraction) + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.0f));
					Widths.Add((bInner ? 8.0f : 12.0f) + (bInner ? 14.0f : 30.0f) * Fraction);
				}
				AppendTornadoRibbon(Mesh, Line, Widths, View);
			}
		}
		ChaosImpactLightning::SetMesh(Strands, Mesh);
		if (UMaterialInstanceDynamic* Look = FunnelMaterial.Get())
		{
			Look->SetScalarParameterValue(TEXT("Intensity"), 0.95f * (0.85f + 0.15f * FMath::Sin(PresentationAge * 13.0f)));
		}
	}

	for (int32 Index = 0; Index < Shells.Num(); ++Index)
	{
		if (UStaticMeshComponent* Shell = Shells[Index].Get())
		{
			Shell->SetVisibility(bVisible);
			const bool bOuter = Index == 0;
			const float Top = TornadoFunnelRadius(1.0f) * Size * (bOuter ? 1.02f : 0.62f) / 50.0f;
			// The engine cone points up; turned over it is wide at the top.
			Shell->SetRelativeLocation(Axis(0.5f));
			Shell->SetRelativeRotation(FRotator(180.0f, FMath::RadiansToDegrees(Spin) * (bOuter ? 0.6f : -0.9f), 0.0f));
			Shell->SetRelativeScale3D(FVector(Top, Top, FMath::Max(Height / 100.0f, 0.01f)));
			if (UMaterialInstanceDynamic* Look = ShellMaterials.IsValidIndex(Index) ? ShellMaterials[Index].Get() : nullptr)
			{
				Look->SetScalarParameterValue(TEXT("Intensity"), (bOuter ? 0.28f : 0.5f) * (0.8f + 0.2f * FMath::Sin(PresentationAge * 9.0f + Index)));
			}
		}
	}

	for (FDebris& Piece : Debris)
	{
		if (UStaticMeshComponent* Mesh = Piece.Mesh.Get())
		{
			// Swept up and round, flung out wider as they rise, and dropped back in at the foot.
			Piece.Height += 150.0f * Piece.Speed * Delta;
			if (Piece.Height > FunnelHeight)
			{
				Piece.Height = 0.0f;
			}
			const float Fraction = Piece.Height / FunnelHeight;
			Piece.Angle += (8.5f - 3.0f * Fraction) * Piece.Speed * Delta;
			const float Radius = (TornadoFunnelRadius(Fraction) + 26.0f) * Size;
			Mesh->SetVisibility(bVisible);
			Mesh->SetRelativeLocation(Axis(Fraction) + FVector(FMath::Cos(Piece.Angle) * Radius, FMath::Sin(Piece.Angle) * Radius, 0.0f));
			Mesh->SetRelativeRotation(FRotator(Piece.Angle * 90.0f, Piece.Angle * 140.0f, 0.0f));
			Mesh->SetRelativeScale3D(FVector(Piece.Size * Size));
		}
	}

	if (UProceduralMeshComponent* Rings = GroundRings.Get())
	{
		ChaosImpactIceMeshes::FMeshBuffers Mesh;
		if (bVisible)
		{
			for (int32 Index = 0; Index < 3; ++Index)
			{
				const float Wave = FMath::Frac(PresentationAge * 1.7f + Index / 3.0f);
				ChaosImpactLightning::AppendRing(Mesh, FVector(0.0f, 0.0f, 6.0f), (60.0f + 230.0f * Wave) * Size,
					18.0f * FMath::Sin(Wave * UE_PI), 40);
			}
			// The flash of forming.
			if (Age < 0.5f)
			{
				const float Burst = Age / 0.5f;
				ChaosImpactLightning::AppendRing(Mesh, FVector(0.0f, 0.0f, 8.0f), 60.0f + 520.0f * TornadoEaseOut(Burst),
					40.0f * (1.0f - Burst), 56);
			}
		}
		ChaosImpactLightning::SetMesh(Rings, Mesh);
	}

	if (UPointLightComponent* Glow = Light.Get())
	{
		Glow->SetIntensity(Size * (7000.0f + 2500.0f * FMath::Sin(PresentationAge * 11.0f)) + 40000.0f * FMath::Exp(-Age * 8.0f));
	}

	const bool bLooping = Age < ActiveSeconds;
	for (const TWeakObjectPtr<UNiagaraComponent>& Effect : {BaseDust, Sparks})
	{
		if (UNiagaraComponent* System = Effect.Get(); System && !bLooping && System->IsActive())
		{
			System->Deactivate();
		}
	}
	// Dirt kicked up along its way.
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (bLooping && Size > 0.5f && Now >= NextDustAt)
	{
		NextDustAt = Now + 0.3;
		if (UNiagaraSystem* Dirt = LoadEffect(Effects::DirtBurstSmall))
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, Dirt, Center, FRotator::ZeroRotator, FVector(0.55f));
		}
	}
	if (!bLooping && CollapseT >= 1.0f && Funnel.IsValid() && Funnel->IsVisible())
	{
		Funnel->SetVisibility(false);
		if (UNiagaraSystem* Puff = LoadEffect(Effects::DirtBurstMedium))
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, Puff, Center, FRotator::ZeroRotator, FVector(0.7f));
		}
	}
}
