#include "ChaosImpactHook.h"

#include "ChaosImpact.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactBallTypes.h"
#include "ChaosImpactBuddyBall.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactDuelBallBalance.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactIceMeshes.h"
#include "ChaosImpactNovaParts.h"
#include "ChaosImpactLightning.h"
#include "ChaosImpactSfx.h"
#include "ChaosImpactTrainingTarget.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "ProceduralMeshComponent.h"

namespace
{
	/** The hook's colour (its ball, its cable's glow, its light). */
	const FLinearColor HookColor(0.25f, 0.95f, 0.8f);
	const FLinearColor AnchorColor(0.78f, 1.0f, 0.2f);
	const FLinearColor HookSteel(0.62f, 0.68f, 0.74f);
	/** The chain's links are this far apart. */
	constexpr float LinkSpacing = 10.0f;
	/** The head stops against walls this far before them (its sweep's radius). */
	constexpr float HeadRadius = 12.0f;

	/** Nearest point to P on the segment A-B. */
	FVector HookClosestOnSegment(const FVector& P, const FVector& A, const FVector& B)
	{
		const FVector AB = B - A;
		const double Length2 = AB.SizeSquared();
		const double T = Length2 > UE_SMALL_NUMBER ? FMath::Clamp(FVector::DotProduct(P - A, AB) / Length2, 0.0, 1.0) : 0.0;
		return A + AB * T;
	}

	/** A thin strip from A to B: two quads crossed at right angles, so it reads as a line from any side. */
	void AppendCable(ChaosImpactIceMeshes::FMeshBuffers& Mesh, const FVector& A, const FVector& B, const float Width)
	{
		const FVector Along = (B - A).GetSafeNormal();
		if (Along.IsNearlyZero())
		{
			return;
		}
		FVector Side = FVector::CrossProduct(Along, FVector::UpVector).GetSafeNormal();
		if (Side.IsNearlyZero())
		{
			Side = FVector::RightVector;
		}
		const FVector Up = FVector::CrossProduct(Side, Along).GetSafeNormal();
		for (const FVector& Across : {Side * Width * 0.5f, Up * Width * 0.5f})
		{
			const int32 First = Mesh.Vertices.Num();
			Mesh.Vertices.Append({A + Across, A - Across, B + Across, B - Across});
			const FVector Normal = FVector::CrossProduct(Along, Across).GetSafeNormal();
			Mesh.Normals.Append({Normal, Normal, Normal, Normal});
			Mesh.UVs.Append({FVector2D(0, 0), FVector2D(0, 1), FVector2D(1, 0), FVector2D(1, 1)});
			Mesh.Triangles.Append({First, First + 2, First + 1, First + 1, First + 2, First + 3});
			Mesh.Triangles.Append({First, First + 1, First + 2, First + 1, First + 3, First + 2});
		}
	}
}

AChaosImpactHook::AChaosImpactHook()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(false);
	SetNetUpdateFrequency(30.0f);
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
}

void AChaosImpactHook::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AChaosImpactHook, Origin);
	DOREPLIFETIME(AChaosImpactHook, Heading);
	DOREPLIFETIME(AChaosImpactHook, StartServerTime);
	DOREPLIFETIME(AChaosImpactHook, SourcePawn);
	DOREPLIFETIME(AChaosImpactHook, Phase);
	DOREPLIFETIME(AChaosImpactHook, PhaseServerTime);
	DOREPLIFETIME(AChaosImpactHook, PhaseFrom);
	DOREPLIFETIME(AChaosImpactHook, Caught);
	DOREPLIFETIME(AChaosImpactHook, bAnchor);
	DOREPLIFETIME(AChaosImpactHook, bAnchorHolds);
	DOREPLIFETIME(AChaosImpactHook, AnchorPoint);
	DOREPLIFETIME(AChaosImpactHook, ZipTarget);
	DOREPLIFETIME(AChaosImpactHook, ZipServerTime);
}

AChaosImpactHook* AChaosImpactHook::Release(UWorld* World, const FVector& From, const FVector& Direction, APawn* Source,
	const bool bAnchor)
{
	if (!World)
	{
		return nullptr;
	}
	FVector Flat(Direction.X, Direction.Y, 0.0f);
	if (!Flat.Normalize())
	{
		Flat = Source ? Source->GetActorForwardVector().GetSafeNormal2D() : FVector::ForwardVector;
	}
	const FTransform Where(Flat.Rotation(), From);
	AChaosImpactHook* Hook = World->SpawnActorDeferred<AChaosImpactHook>(StaticClass(), Where, nullptr, Source,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Hook)
	{
		return nullptr;
	}
	Hook->Origin = From;
	Hook->Heading = Flat;
	const AGameStateBase* GameState = World->GetGameState();
	Hook->StartServerTime = GameState ? GameState->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
	Hook->PhaseServerTime = Hook->StartServerTime;
	Hook->PhaseFrom = From;
	Hook->SourcePawn = Source;
	Hook->DuelThrowDamage = FChaosImpactDuelThrowDamage::Create(World);
	Hook->bAnchor = bAnchor;
	if (bAnchor)
	{
		Hook->PlaceAnchor();
	}
	Hook->FinishSpawning(Where);
	UE_LOG(LogChaosImpact, Log, TEXT("%s thrown by %s from %s heading %s"), bAnchor ? TEXT("Anchor") : TEXT("Hook"), *GetNameSafe(Source),
		*From.ToCompactString(), *Flat.ToCompactString());
	return Hook;
}

void AChaosImpactHook::PlaceAnchor()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	FCollisionQueryParams Query(SCENE_QUERY_STAT(ChaosImpactAnchor), false, this);
	if (SourcePawn)
	{
		Query.AddIgnoredActor(SourcePawn);
	}
	const FVector From = Origin;
	const FVector Line = FVector(Heading);
	const FVector Tip = From + Line * Length;
	const ACharacter* Thrower = Cast<ACharacter>(SourcePawn);
	const float Radius = Thrower && Thrower->GetCapsuleComponent() ? Thrower->GetCapsuleComponent()->GetScaledCapsuleRadius() : 40.0f;
	const auto FloorUnder = [World, &Query](const FVector& At, FVector& OutFloor)
	{
		FHitResult Floor;
		if (World->LineTraceSingleByObjectType(Floor, At + FVector(0.0f, 0.0f, 120.0f), At - FVector(0.0f, 0.0f, 900.0f),
			FCollisionObjectQueryParams(ECC_WorldStatic), Query) && Floor.ImpactNormal.Z > 0.6f)
		{
			OutFloor = Floor.ImpactPoint;
			return true;
		}
		return false;
	};
	FHitResult Wall;
	FVector Floor;
	if (World->SweepSingleByObjectType(Wall, From, Tip, FQuat::Identity, FCollisionObjectQueryParams(ECC_WorldStatic),
		FCollisionShape::MakeSphere(HeadRadius), Query))
	{
		// Into the wall, where it met it; the thrower lands on the floor just in front of it.
		AnchorPoint = Wall.ImpactPoint;
		const FVector Before = Wall.Location - Line * (Radius + 35.0f);
		bAnchorHolds = FVector::Dist2D(Before, From) > 120.0f && FloorUnder(Before, Floor);
		ZipTarget = bAnchorHolds ? Floor : Before;
	}
	else if (FloorUnder(Tip, Floor))
	{
		// Its full reach out over the floor: into the floor there.
		AnchorPoint = Floor;
		ZipTarget = Floor;
		bAnchorHolds = true;
	}
	else
	{
		// Out over nothing: it finds no hold and comes back.
		AnchorPoint = Tip;
		ZipTarget = Tip;
		bAnchorHolds = false;
	}
	ZipServerTime = StartServerTime + FVector::Dist(From, FVector(AnchorPoint)) / AnchorSpeed;
}

bool AChaosImpactHook::ShouldZipNow() const
{
	return bAnchor && bAnchorHolds && Phase != EChaosImpactHookPhase::Back && ServerNow() >= ZipServerTime;
}

double AChaosImpactHook::ServerNow() const
{
	const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	return GameState ? GameState->GetServerWorldTimeSeconds() : (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
}

FVector AChaosImpactHook::GetHand() const
{
	const AChaosImpactCharacter* Thrower = Cast<AChaosImpactCharacter>(SourcePawn);
	if (!SourcePawn)
	{
		return Origin;
	}
	// The palm that holds the chain, when this screen draws the thrower's arms.
	FVector Palm;
	if (Thrower && Thrower->GetHookHandLocation(Palm))
	{
		return Palm;
	}
	// The body as this screen draws it, a little forward and at the hands' height.
	const FVector Body = Thrower && !HasAuthority() ? Thrower->GetPresentationLocation() : SourcePawn->GetActorLocation();
	return Body + SourcePawn->GetActorForwardVector().GetSafeNormal2D() * 38.0f + FVector(0.0f, 0.0f, 30.0f);
}

FVector AChaosImpactHook::OutPosition(const float TimeOut) const
{
	const float Reach = TimeOut * OutSpeed;
	const FVector Tip = FVector(Origin) + FVector(Heading) * Length;
	if (Reach <= Length)
	{
		return FVector(Origin) + FVector(Heading) * Reach;
	}
	// Its full length reached: back to the hand from the tip.
	const FVector Hand = GetHand();
	const float Back = (TimeOut - Length / OutSpeed) * BackSpeed;
	const float Span = static_cast<float>(FVector::Dist(Tip, Hand));
	return Span > 1.0f ? FMath::Lerp(Tip, Hand, FMath::Clamp(Back / Span, 0.0f, 1.0f)) : Hand;
}

FVector AChaosImpactHook::GetPullTarget(const FVector& From) const
{
	if (!SourcePawn)
	{
		return From;
	}
	const FVector Source = SourcePawn->GetActorLocation();
	FVector Toward = (From - Source).GetSafeNormal2D();
	if (Toward.IsNearlyZero())
	{
		Toward = SourcePawn->GetActorForwardVector().GetSafeNormal2D();
	}
	const FVector Target = Source + Toward * PullStopDistance;
	return FVector(Target.X, Target.Y, From.Z);
}

bool AChaosImpactHook::CanCatch(const AActor* Victim) const
{
	const AChaosImpactCharacter* Character = Cast<AChaosImpactCharacter>(Victim);
	return Character && Character != SourcePawn && !Character->IsEliminated() && !Character->IsDashingForPresentation()
		&& !AChaosImpactGameState::AreTeammates(GetWorld(), SourcePawn, Character);
}

void AChaosImpactHook::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!GetWorld())
	{
		return;
	}
	const double Now = ServerNow();
	if (AChaosImpactCharacter* Thrower = Cast<AChaosImpactCharacter>(SourcePawn))
	{
		Thrower->ShowOwnHook(this);
	}
	const FVector Hand = GetHand();
	LastHead = bHeadStarted ? Head : FVector(Origin);
	bHeadStarted = true;
	switch (Phase)
	{
	case EChaosImpactHookPhase::Out:
		if (bAnchor)
		{
			// Straight to where it sticks (worked out at the throw), or out to its reach and back when nothing holds it.
			const float Out = static_cast<float>(Now - StartServerTime) * AnchorSpeed;
			const FVector Way = FVector(AnchorPoint) - FVector(Origin);
			const float Span = static_cast<float>(Way.Size());
			if (Out < Span || bAnchorHolds)
			{
				Head = Span > 1.0f ? FVector(Origin) + Way * FMath::Clamp(Out / Span, 0.0f, 1.0f) : FVector(AnchorPoint);
			}
			else
			{
				const float Back = (Out - Span) / AnchorSpeed * BackSpeed;
				const float Home = static_cast<float>(FVector::Dist(FVector(AnchorPoint), Hand));
				Head = Home > 1.0f ? FMath::Lerp(FVector(AnchorPoint), Hand, FMath::Clamp(Back / Home, 0.0f, 1.0f)) : Hand;
			}
			if (!bLatchedHere && bAnchorHolds && Out >= Span && GetNetMode() != NM_DedicatedServer)
			{
				// Bites in (each screen when it gets there).
				bLatchedHere = true;
				SparkAt = FPlatformTime::Seconds();
				SparkLocation = FVector(AnchorPoint);
				ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::ThunderBounce, FVector(AnchorPoint), 0.7f, 0.8f);
			}
		}
		else if (bBlockedHere)
		{
			// Struck a wall on this screen: back from there (the host says the same soon).
			const float Back = static_cast<float>(Now - BlockedAt) * BackSpeed;
			const float Span = static_cast<float>(FVector::Dist(BlockedFrom, Hand));
			Head = Span > 1.0f ? FMath::Lerp(BlockedFrom, Hand, FMath::Clamp(Back / Span, 0.0f, 1.0f)) : Hand;
		}
		else
		{
			Head = OutPosition(static_cast<float>(Now - StartServerTime));
		}
		break;
	case EChaosImpactHookPhase::Pull:
		if (bAnchor)
		{
			Head = FVector(AnchorPoint);
		}
		else if (const AChaosImpactCharacter* Victim = Cast<AChaosImpactCharacter>(Caught))
		{
			const FVector Body = HasAuthority() || Victim->IsLocallyControlled() ? Victim->GetActorLocation() : Victim->GetPresentationLocation();
			Head = Body + FVector(0.0f, 0.0f, 20.0f);
		}
		else if (Caught)
		{
			Head = Caught->GetActorLocation();
		}
		break;
	case EChaosImpactHookPhase::Back:
	{
		const float Back = static_cast<float>(Now - PhaseServerTime) * BackSpeed;
		const float Span = static_cast<float>(FVector::Dist(FVector(PhaseFrom), Hand));
		Head = Span > 1.0f ? FMath::Lerp(FVector(PhaseFrom), Hand, FMath::Clamp(Back / Span, 0.0f, 1.0f)) : Hand;
		break;
	}
	}
	if (Phase == EChaosImpactHookPhase::Out && !bLatchedHere && !bBlockedHere && !bAnchor)
	{
		CatchAlong(LastHead, Head);
	}
	if (HasAuthority())
	{
		ServerTick(DeltaSeconds);
	}
	if (GetNetMode() != NM_DedicatedServer && IsValid(this))
	{
		UpdatePresentation(DeltaSeconds);
	}
}

void AChaosImpactHook::CatchAlong(const FVector& From, const FVector& To)
{
	UWorld* World = GetWorld();
	if (!World || From.Equals(To, 0.5))
	{
		return;
	}
	// A wall (or anything solid) on the way: it stops there and comes back.
	FHitResult Wall;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(ChaosImpactHook), false, this);
	if (SourcePawn)
	{
		Query.AddIgnoredActor(SourcePawn);
	}
	FVector End = To;
	bool bWall = false;
	if (World->SweepSingleByObjectType(Wall, From, To, FQuat::Identity, FCollisionObjectQueryParams(ECC_WorldStatic),
		FCollisionShape::MakeSphere(HeadRadius), Query))
	{
		End = Wall.Location;
		bWall = true;
	}
	// An opponent along the stretch it moved (the host's own players and CPUs judged here, a guest's on their screen).
	for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
	{
		AChaosImpactCharacter* Character = *It;
		if (!CanCatch(Character))
		{
			continue;
		}
		const bool bJudgeHere = HasAuthority() ? !Character->IsRemotePlayerOnServer() : Character->IsLocallyControlled();
		if (!bJudgeHere)
		{
			continue;
		}
		const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
		const float Radius = Capsule ? Capsule->GetScaledCapsuleRadius() : 40.0f;
		const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 90.0f;
		const FVector Body = Character->GetActorLocation();
		const FVector Near = HookClosestOnSegment(Body, From, End);
		if (FVector::Dist2D(Near, Body) > Radius + CatchRadius || FMath::Abs(Near.Z - Body.Z) > HalfHeight + CatchRadius)
		{
			continue;
		}
		Head = Near;
		if (HasAuthority())
		{
			CatchCharacter(Character);
		}
		else
		{
			// This screen saw its own player hooked: reeled in here at once, the host told.
			bLatchedHere = true;
			Character->BeginHookPull(this);
			Character->ServerReportHookHit(this);
			SparkAt = FPlatformTime::Seconds();
			SparkLocation = Near;
			ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::BallHit, Near, 1.0f, 0.75f);
		}
		return;
	}
	if (HasAuthority())
	{
		// A ball lying on the floor: reeled in to the hand.
		for (TActorIterator<AChaosImpactBall> It(World); It; ++It)
		{
			AChaosImpactBall* Ball = *It;
			if (!Ball->IsPickupAvailable() || Ball->IsCarriedByWind() || Ball->IsHidden() || Cast<AChaosImpactBuddyBall>(Ball))
			{
				continue;
			}
			// Seen from above: the hook flies at hand height, a ball lies on the floor or sits on its pad below that.
			const FVector Lying = Ball->GetActorLocation();
			const FVector Near = HookClosestOnSegment(FVector(Lying.X, Lying.Y, From.Z), From, FVector(End.X, End.Y, From.Z));
			if (FVector::Dist2D(Near, Lying) <= BallCatchRadius && FMath::Abs(Lying.Z - From.Z) <= BallCatchHeight)
			{
				Head = Ball->GetActorLocation();
				CatchBall(Ball);
				return;
			}
		}
		// A nova part hovering over the floor or its pad: reeled in to the hand like a ball.
		for (TActorIterator<AChaosImpactNovaPart> It(World); It; ++It)
		{
			if (It->IsActorBeingDestroyed())
			{
				continue;
			}
			const FVector Floating = It->GetActorLocation();
			const FVector Near = HookClosestOnSegment(FVector(Floating.X, Floating.Y, From.Z), From, FVector(End.X, End.Y, From.Z));
			if (FVector::Dist2D(Near, Floating) <= BallCatchRadius + 25.0f
				&& FMath::Abs(Floating.Z - From.Z) <= BallCatchHeight + ChaosImpactNovaParts::PartHoverHeight)
			{
				Head = Floating;
				CatchPart(*It);
				return;
			}
		}
		// Training targets take the hit (and the hook comes back).
		for (TActorIterator<AChaosImpactTrainingTarget> It(World); It; ++It)
		{
			if (!It->IsDefeated() && FVector::Dist(HookClosestOnSegment(It->GetActorLocation(), From, End), It->GetActorLocation()) <= 90.0f)
			{
				UGameplayStatics::ApplyDamage(*It, 1.0f, SourcePawn ? SourcePawn->GetController() : nullptr, this, nullptr);
				MulticastLatch(It->GetActorLocation(), true);
				Head = It->GetActorLocation();
				GoBack();
				return;
			}
		}
	}
	if (bWall)
	{
		Head = End;
		bBlockedHere = true;
		BlockedAt = ServerNow();
		BlockedFrom = End;
		if (GetNetMode() != NM_DedicatedServer)
		{
			ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::ThunderBounce, End, 0.45f, 1.7f);
			SparkAt = FPlatformTime::Seconds();
			SparkLocation = End;
		}
		if (HasAuthority())
		{
			GoBack();
		}
	}
}

void AChaosImpactHook::CatchCharacter(AChaosImpactCharacter* Victim)
{
	if (!HasAuthority() || Phase != EChaosImpactHookPhase::Out || !Victim)
	{
		return;
	}
	bLatchedHere = true;
	Phase = EChaosImpactHookPhase::Pull;
	Caught = Victim;
	PhaseServerTime = ServerNow();
	PhaseFrom = Head;
	// One hit (the duel's limit on a throw's damage applies), then reeled in.
	UGameplayStatics::ApplyDamage(Victim, 1.0f, SourcePawn ? SourcePawn->GetController() : nullptr, this, nullptr);
	MulticastLatch(Head, true);
	if (Victim->IsEliminated())
	{
		GoBack();
	}
	else
	{
		Victim->BeginHookPull(this);
	}
	ForceNetUpdate();
	UE_LOG(LogChaosImpact, Log, TEXT("Hook caught %s"), *GetNameSafe(Victim->GetPlayerState()));
}

bool AChaosImpactHook::TryApplyReportedHit(AChaosImpactCharacter* Victim)
{
	if (!HasAuthority() || !Victim || Victim == SourcePawn || Victim->IsEliminated()
		|| AChaosImpactGameState::AreTeammates(GetWorld(), SourcePawn, Victim))
	{
		return false;
	}
	// Still on its way out (or only just turned back), and the guest stood near its line when their screen saw it.
	const double Now = ServerNow();
	const bool bInTime = Phase == EChaosImpactHookPhase::Out
		|| (Phase == EChaosImpactHookPhase::Back && !Caught && Now - PhaseServerTime < 0.35);
	const FVector Tip = FVector(Origin) + FVector(Heading) * Length;
	const float Off = static_cast<float>(FVector::Dist2D(HookClosestOnSegment(Victim->GetActorLocation(), Origin, Tip), Victim->GetActorLocation()));
	const bool bAccepted = bInTime && Off <= CatchRadius + 60.0f + ReportTolerance;
	UE_LOG(LogChaosImpact, Log, TEXT("Reported hook hit %s: victim=%s off=%.0f phase=%d"), bAccepted ? TEXT("accepted") : TEXT("rejected"),
		*GetNameSafe(Victim->GetPlayerState()), Off, static_cast<int32>(Phase));
	if (!bAccepted)
	{
		return false;
	}
	bLatchedHere = true;
	Phase = EChaosImpactHookPhase::Pull;
	Caught = Victim;
	PhaseServerTime = Now;
	PhaseFrom = Victim->GetActorLocation();
	UGameplayStatics::ApplyDamage(Victim, 1.0f, SourcePawn ? SourcePawn->GetController() : nullptr, this, nullptr);
	MulticastLatch(Victim->GetActorLocation(), true);
	if (Victim->IsEliminated())
	{
		GoBack();
	}
	else
	{
		Victim->BeginHookPull(this);
	}
	ForceNetUpdate();
	return true;
}

void AChaosImpactHook::CatchBall(AChaosImpactBall* Ball)
{
	if (!HasAuthority() || Phase != EChaosImpactHookPhase::Out || !Ball || !Ball->CatchInWind())
	{
		return;
	}
	bLatchedHere = true;
	Phase = EChaosImpactHookPhase::Pull;
	Caught = Ball;
	PulledBall = Ball;
	PhaseServerTime = ServerNow();
	PhaseFrom = Ball->GetActorLocation();
	MulticastLatch(Ball->GetActorLocation(), false);
	ForceNetUpdate();
	UE_LOG(LogChaosImpact, Log, TEXT("Hook caught a %s ball"), ChaosImpactBallTypes::GetInternalName(Ball->GetBallType()));
}

void AChaosImpactHook::CatchPart(AChaosImpactNovaPart* Part)
{
	if (!HasAuthority() || Phase != EChaosImpactHookPhase::Out || !Part)
	{
		return;
	}
	bLatchedHere = true;
	Phase = EChaosImpactHookPhase::Pull;
	Caught = Part;
	PulledPart = Part;
	// (Its flight to the hand seen on every screen.)
	Part->SetReplicateMovement(true);
	PhaseServerTime = ServerNow();
	PhaseFrom = Part->GetActorLocation();
	MulticastLatch(Part->GetActorLocation(), false);
	ForceNetUpdate();
	UE_LOG(LogChaosImpact, Log, TEXT("Hook caught the nova's %s"), ChaosImpactNovaParts::GetName(Part->GetPart()));
}

void AChaosImpactHook::GoBack()
{
	if (!HasAuthority() || Phase == EChaosImpactHookPhase::Back)
	{
		return;
	}
	Phase = EChaosImpactHookPhase::Back;
	PhaseFrom = Head;
	PhaseServerTime = ServerNow();
	Caught = nullptr;
	ForceNetUpdate();
}

void AChaosImpactHook::ServerTick(const float DeltaSeconds)
{
	const double Now = ServerNow();
	AChaosImpactCharacter* Thrower = Cast<AChaosImpactCharacter>(SourcePawn);
	const FVector Hand = GetHand();
	// No thrower any more (knocked out, gone): it lets go of anything and is gone.
	if (!SourcePawn || (Thrower && Thrower->IsEliminated()))
	{
		if (AChaosImpactBall* Ball = PulledBall.Get(); Ball && Ball->IsCarriedByWind())
		{
			Ball->ReleaseFromWind(Ball->GetActorLocation() - FVector(0.0f, 0.0f, 60.0f), FVector::ZeroVector);
		}
		PulledBall.Reset();
		Destroy();
		return;
	}
	if (bAnchor)
	{
		switch (Phase)
		{
		case EChaosImpactHookPhase::Out:
			if (Now >= ZipServerTime)
			{
				if (bAnchorHolds)
				{
					// Stuck: its thrower is whisked here (on their own machine).
					Phase = EChaosImpactHookPhase::Pull;
					PhaseServerTime = ZipServerTime;
					Head = FVector(AnchorPoint);
					ForceNetUpdate();
				}
				else if (Now >= ZipServerTime + 0.05)
				{
					Head = FVector(AnchorPoint);
					GoBack();
				}
			}
			break;
		case EChaosImpactHookPhase::Pull:
		{
			// Done once the thrower is there, has dashed off, or it has gone on too long.
			const FVector Feet = SourcePawn->GetActorLocation();
			const bool bArrived = FVector::Dist2D(Feet, FVector(ZipTarget)) <= 110.0f;
			const bool bDashed = Thrower && Thrower->IsDashingForPresentation() && Now - PhaseServerTime > 0.05;
			if (bArrived || bDashed || Now - PhaseServerTime > ZipMaxSeconds + 0.5)
			{
				Head = FVector(AnchorPoint);
				GoBack();
			}
			break;
		}
		case EChaosImpactHookPhase::Back:
			if (FVector::Dist(Head, Hand) < 30.0f || Now - PhaseServerTime > 3.0)
			{
				Destroy();
			}
			break;
		}
		return;
	}
	switch (Phase)
	{
	case EChaosImpactHookPhase::Out:
		if (!bBlockedHere && static_cast<float>(Now - StartServerTime) * OutSpeed >= Length)
		{
			GoBack();
		}
		break;
	case EChaosImpactHookPhase::Pull:
		if (AChaosImpactNovaPart* Part = PulledPart.Get())
		{
			// The part comes in to the hand: the thrower's if they have not got it yet, else it is left at their feet.
			const FVector At = Part->GetActorLocation();
			const FVector ToHand = Hand - At;
			const float Step = PullSpeed * FMath::Min(DeltaSeconds, 0.1f);
			if (ToHand.Size() <= Step + 40.0f)
			{
				if (!(Thrower && Part->TryGive(Thrower)))
				{
					Part->SetActorLocation(SourcePawn->GetActorLocation() + SourcePawn->GetActorForwardVector() * 110.0f
						+ FVector(0.0f, 0.0f, ChaosImpactNovaParts::PartHoverHeight - 80.0f));
				}
				PulledPart.Reset();
				Head = Hand;
				GoBack();
			}
			else
			{
				Part->SetActorLocation(At + ToHand.GetSafeNormal() * Step);
			}
		}
		else if (AChaosImpactBall* Ball = PulledBall.Get())
		{
			// The ball comes in to the hand; held there if there is room, else it drops at the thrower's feet.
			const FVector At = Ball->GetActorLocation();
			const FVector ToHand = Hand - At;
			const float Step = PullSpeed * FMath::Min(DeltaSeconds, 0.1f);
			if (ToHand.Size() <= Step + 40.0f)
			{
				const EChaosImpactBallType Kind = Ball->GetBallType();
				if (Thrower && Thrower->GiveContestedPickup(Kind))
				{
					ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::CharPickup, Hand, 0.9f, 0.9f);
					Ball->Destroy();
					UE_LOG(LogChaosImpact, Log, TEXT("Hook brought a %s ball to %s's hand"), ChaosImpactBallTypes::GetInternalName(Kind),
						*GetNameSafe(Thrower->GetPlayerState()));
				}
				else
				{
					Ball->ReleaseFromWind(SourcePawn->GetActorLocation() + SourcePawn->GetActorForwardVector() * 70.0f
						- FVector(0.0f, 0.0f, 80.0f), FVector::ZeroVector);
				}
				PulledBall.Reset();
				Head = Hand;
				GoBack();
			}
			else
			{
				Ball->CarryInWind(At + ToHand.GetSafeNormal() * Step);
			}
		}
		else if (AChaosImpactCharacter* Victim = Cast<AChaosImpactCharacter>(Caught))
		{
			// Reeled in on its own machine; let go once in front of the thrower, broken free, gone down, or too long.
			const bool bArrived = FVector::Dist2D(Victim->GetActorLocation(), SourcePawn->GetActorLocation()) <= PullStopDistance + 45.0f;
			if (bArrived || Victim->IsEliminated() || Victim->IsDashingForPresentation() || Now - PhaseServerTime > PullMaxSeconds + 0.3)
			{
				GoBack();
			}
		}
		else
		{
			GoBack();
		}
		break;
	case EChaosImpactHookPhase::Back:
		if (FVector::Dist(Head, Hand) < 30.0f || Now - PhaseServerTime > 3.0)
		{
			Destroy();
		}
		break;
	}
}

void AChaosImpactHook::MulticastLatch_Implementation(const FVector_NetQuantize Location, const bool bOnCharacter)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	SparkAt = FPlatformTime::Seconds();
	SparkLocation = Location;
	ChaosImpactSfx::PlayAt(this, bOnCharacter ? EChaosImpactSfx::BallHit : EChaosImpactSfx::CharPickup, Location, 1.0f, bOnCharacter ? 0.75f : 0.8f);
}

void AChaosImpactHook::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority())
	{
		if (AChaosImpactBall* Ball = PulledBall.Get(); Ball && Ball->IsCarriedByWind() && IsValid(Ball))
		{
			Ball->ReleaseFromWind(Ball->GetActorLocation() - FVector(0.0f, 0.0f, 60.0f), FVector::ZeroVector);
		}
	}
	Super::EndPlay(EndPlayReason);
}

// ---------------------------------------------------------------------------------------------------------------------
// The look: a three-pronged steel hook (its prongs closing as it catches), a chain of steel links back to the hand
// over a thin glowing cable, a small light on the head, and a spark where it latches on.
// ---------------------------------------------------------------------------------------------------------------------

void AChaosImpactHook::BuildPresentation()
{
	bPresentationBuilt = true;
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UStaticMesh* Cone = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
	UMaterialInstanceDynamic* Steel = ChaosImpactBallTypes::MakeEmissive(this, HookSteel, 0.9f);
	const FLinearColor Colour = bAnchor ? AnchorColor : HookColor;
	UMaterialInstanceDynamic* Accent = ChaosImpactBallTypes::MakeEmissive(this, Colour, 1.8f);
	const auto AddPart = [this](UStaticMesh* Mesh, UMaterialInterface* Look, USceneComponent* Parent)
	{
		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this);
		Part->SetStaticMesh(Mesh);
		Part->SetMaterial(0, Look);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCastShadow(false);
		Part->SetupAttachment(Parent);
		Part->RegisterComponent();
		return Part;
	};
	HeadRoot = NewObject<USceneComponent>(this);
	HeadRoot->SetupAttachment(SceneRoot);
	HeadRoot->SetUsingAbsoluteLocation(true);
	HeadRoot->SetUsingAbsoluteRotation(true);
	HeadRoot->RegisterComponent();
	Hub = AddPart(Sphere, Accent, HeadRoot);
	Hub->SetRelativeScale3D(FVector(0.15f));
	for (int32 Prong = 0; Prong < 3; ++Prong)
	{
		Prongs.Add(AddPart(Cone, Steel, HeadRoot));
	}
	// The point at its nose.
	Prongs.Add(AddPart(Cone, Steel, HeadRoot));
	Links = NewObject<UInstancedStaticMeshComponent>(this);
	Links->SetStaticMesh(Sphere);
	Links->SetMaterial(0, Steel);
	Links->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Links->SetCastShadow(false);
	Links->SetupAttachment(SceneRoot);
	Links->SetUsingAbsoluteLocation(true);
	Links->SetUsingAbsoluteRotation(true);
	Links->SetUsingAbsoluteScale(true);
	Links->RegisterComponent();
	CableMaterial = ChaosImpactBallTypes::MakeAdditive(this, Colour, 2.2f);
	Cable = ChaosImpactLightning::CreateComponent(this, SceneRoot, CableMaterial);
	SparkMaterial = ChaosImpactBallTypes::MakeAdditive(this, FLinearColor(0.85f, 1.0f, 0.95f), 4.0f);
	Spark = ChaosImpactLightning::CreateComponent(this, SceneRoot, SparkMaterial);
	for (UProceduralMeshComponent* Mesh : {Cable.Get(), Spark.Get()})
	{
		if (Mesh)
		{
			Mesh->SetUsingAbsoluteLocation(true);
			Mesh->SetUsingAbsoluteRotation(true);
			Mesh->SetUsingAbsoluteScale(true);
			Mesh->SetWorldLocationAndRotation(Origin, FRotator::ZeroRotator);
		}
	}
	HeadLight = NewObject<UPointLightComponent>(this);
	HeadLight->SetupAttachment(HeadRoot);
	HeadLight->SetLightColor(Colour);
	HeadLight->SetIntensity(1400.0f);
	HeadLight->SetAttenuationRadius(260.0f);
	HeadLight->SetCastShadows(false);
	HeadLight->RegisterComponent();
	ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::CharThrow, Origin, 0.9f, bAnchor ? 1.6f : 1.35f);
}

void AChaosImpactHook::UpdatePresentation(const float DeltaSeconds)
{
	if (!bPresentationBuilt)
	{
		BuildPresentation();
	}
	const FVector Hand = GetHand();
	FVector Along = (Head - Hand).GetSafeNormal();
	if (Along.IsNearlyZero())
	{
		Along = FVector(Heading);
	}
	// The head faces the way it goes out (and away from the hand coming back), prongs open in flight, shut on a catch.
	const bool bHolding = Phase == EChaosImpactHookPhase::Pull || bLatchedHere;
	Grip = FMath::FInterpTo(Grip, bHolding ? 1.0f : 0.0f, DeltaSeconds, 14.0f);
	// (An anchor stuck keeps facing the way it went in.)
	const FRotator Facing = bAnchor ? (FVector(AnchorPoint) - FVector(Origin)).GetSafeNormal().Rotation() : Along.Rotation();
	HeadRoot->SetWorldLocationAndRotation(Head, Facing);
	const float Spin = static_cast<float>(FPlatformTime::Seconds()) * (bHolding || bAnchor ? 0.0f : 540.0f);
	for (int32 Prong = 0; Prong < 3; ++Prong)
	{
		const float Angle = FMath::DegreesToRadians(Prong * 120.0f + Spin);
		const FVector Out(0.0f, FMath::Cos(Angle), FMath::Sin(Angle));
		if (bAnchor)
		{
			// Flukes: swept back from the point like an anchor's, flaring a little once it bites in.
			const FVector Point = (-FVector::ForwardVector * 0.75f + Out * FMath::Lerp(0.75f, 1.0f, Grip)).GetSafeNormal();
			Prongs[Prong]->SetRelativeLocationAndRotation(FVector(6.0f, 0.0f, 0.0f) + Out * 5.0f + Point * 8.0f,
				FRotationMatrix::MakeFromZ(Point).Rotator());
			Prongs[Prong]->SetRelativeScale3D(FVector(0.06f, 0.06f, 0.19f));
			continue;
		}
		const float Spread = FMath::Lerp(0.6f, 0.12f, Grip);
		// Each prong leans forward and out from the hub (a cone's point is its +Z).
		const FVector Point = (FVector::ForwardVector + Out * Spread).GetSafeNormal();
		Prongs[Prong]->SetRelativeLocationAndRotation(Out * 7.0f + Point * 9.0f, FRotationMatrix::MakeFromZ(Point).Rotator());
		Prongs[Prong]->SetRelativeScale3D(FVector(0.06f, 0.06f, 0.2f));
	}
	Prongs[3]->SetRelativeLocationAndRotation(FVector(bAnchor ? 16.0f : 12.0f, 0.0f, 0.0f), FRotationMatrix::MakeFromZ(FVector::ForwardVector).Rotator());
	Prongs[3]->SetRelativeScale3D(bAnchor ? FVector(0.1f, 0.1f, 0.26f) : FVector(0.07f, 0.07f, 0.16f));

	// The chain: links every LinkSpacing from the hand to the head, each turned a quarter from the last and overlapping
	// the next (so it reads as one chain, not beads).
	Links->ClearInstances();
	const float Span = static_cast<float>(FVector::Dist(Hand, Head));
	const int32 Count = FMath::Clamp(FMath::FloorToInt(Span / LinkSpacing), 0, 260);
	const FRotator LinkFacing = Along.Rotation();
	for (int32 Link = 1; Link <= Count; ++Link)
	{
		const FVector At = Hand + Along * (Link * LinkSpacing);
		const FRotator Turn = LinkFacing + FRotator(0.0f, 0.0f, Link % 2 == 0 ? 0.0f : 90.0f);
		Links->AddInstance(FTransform(Turn, At, FVector(0.13f, 0.06f, 0.028f)), true);
	}
	// (The cable's mesh is built round the hand and placed there each frame, so nothing it hangs from can move it.)
	ChaosImpactIceMeshes::FMeshBuffers Line;
	AppendCable(Line, FVector::ZeroVector, Head - Hand, 3.0f);
	Cable->SetWorldLocationAndRotation(Hand, FRotator::ZeroRotator);
	Cable->SetWorldScale3D(FVector::OneVector);
	ChaosImpactLightning::SetMesh(Cable, Line);
	if (CableMaterial)
	{
		CableMaterial->SetScalarParameterValue(TEXT("Intensity"), bHolding ? 3.4f : 2.0f);
	}
	if (HeadLight)
	{
		HeadLight->SetIntensity(bHolding ? 2600.0f : 1400.0f);
	}
	// The latch's spark: a quick ring opening where it caught.
	ChaosImpactIceMeshes::FMeshBuffers Burst;
	const float SparkAge = static_cast<float>(FPlatformTime::Seconds() - SparkAt);
	if (SparkAge >= 0.0f && SparkAge < 0.25f)
	{
		const float T = SparkAge / 0.25f;
		ChaosImpactLightning::AppendRing(Burst, FVector::ZeroVector, 18.0f + 70.0f * T, 8.0f * (1.0f - T) + 2.0f, 32);
		Spark->SetWorldLocationAndRotation(SparkLocation, FRotator::ZeroRotator);
		Spark->SetWorldScale3D(FVector::OneVector);
		if (SparkMaterial)
		{
			SparkMaterial->SetScalarParameterValue(TEXT("Intensity"), 4.0f * (1.0f - T));
		}
	}
	ChaosImpactLightning::SetMesh(Spark, Burst);
}
