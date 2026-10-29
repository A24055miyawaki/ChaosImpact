#include "ChaosImpactSimaeBoss.h"
#include "ChaosImpactFxKit.h"

#include "ChaosImpact.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactBallTypes.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactIceMeshes.h"
#include "ChaosImpactPaint.h"
#include "ChaosImpactSfx.h"
#include "ChaosImpactSimaeBossFx.h"
#include "ChaosImpactSoloRoom.h"

#include "AIController.h"
#include "Animation/AnimSequence.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/AudioComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** The model's own proportions (it is about 1.9 m tall): the capsule round it, per unit of Boss Scale. */
	constexpr float SimaeCapsuleRadius = 46.0f;
	constexpr float SimaeCapsuleHalfHeight = 96.0f;
	/** In the clips' own seconds: when things happen (their sync markers). */
	constexpr float SlamTakeoff = 0.32f;
	constexpr float SlamImpactAt = 0.98f;
	constexpr float SlamEnd = 1.95f;
	constexpr float ThrowRelease = 1.0667f;
	constexpr float ThrowEnd = 1.85f;
	constexpr float RoarAt = 1.72f;
	constexpr float PowerUpEnd = 2.9f;
	/** The slam's fist lands this far in front of the model's feet (model centimetres). */
	constexpr float SlamFistReach = 55.0f;
	/** The walk covers this much ground per second at play rate 1 (model centimetres). */
	constexpr float WalkStridePerSecond = 94.5f;
	const FLinearColor SimaeWarningRed(1.0f, 0.04f, 0.08f);
	const FLinearColor SimaeAuraRed(1.0f, 0.06f, 0.16f);

	UMaterialInterface* LoadSimaeMaterial(const TCHAR* Path)
	{
		return LoadObject<UMaterialInterface>(nullptr, Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}

	AChaosImpactBall* SpawnPlainBall(UWorld* World, const FVector& Where)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World ? World->SpawnActor<AChaosImpactBall>(AChaosImpactBall::StaticClass(), Where, FRotator::ZeroRotator, Params) : nullptr;
	}

	/**
	 * The full power's three goukyu side by side: one from the hand, one either side of it, far enough apart that
	 * there is a safe gap between each pair of lanes (they spread a little as they go).
	 */
	constexpr int32 SimaeFullPowerLanes = 3;
	constexpr float SimaeLaneSpread = 4.0f;
	void SimaeLane(const int32 Lane, const int32 Lanes, const FVector& Start, const FVector& Direction, const float Radius,
		FVector& OutStart, FVector& OutDirection)
	{
		const float Offset = Lane - (Lanes - 1) * 0.5f;
		// Two lanes' width, the widest character and a good step between neighbours.
		const float Spacing = Radius * 2.0f + 380.0f;
		const FVector Side = FVector::CrossProduct(FVector::UpVector, Direction);
		OutStart = Start + Side * Offset * Spacing;
		OutDirection = Direction.RotateAngleAxis(Offset * SimaeLaneSpread, FVector::UpVector);
	}

	float Smooth01(const float X)
	{
		const float T = FMath::Clamp(X, 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}
}

AChaosImpactSimaeBoss::AChaosImpactSimaeBoss()
{
	// Its own model and moves: no toon model, no CPU, and it never picks balls up.
	bUseToonCharacter = false;
	bCanCollectBalls = false;
	AIControllerClass = AAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	SoloTeam = SoloEnemyTeam;
	bRespawnAfterElimination = false;
	MaxHealth = 16.0f;
	Health = MaxHealth;
	EliminationEffectDuration = 1.6f;

	GetCapsuleComponent()->InitCapsuleSize(SimaeCapsuleRadius * BossScale, SimaeCapsuleHalfHeight * BossScale);

	static ConstructorHelpers::FObjectFinder<USkeletalMesh> Normal(TEXT("/Game/ChaosImpact/Prototypes/SimaeBossMotions/SK_Boss_Normal.SK_Boss_Normal"));
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> Full(TEXT("/Game/ChaosImpact/Prototypes/SimaeBossMotions/SK_Boss_FullPower.SK_Boss_FullPower"));
	static ConstructorHelpers::FObjectFinder<UAnimSequence> Idle(TEXT("/Game/ChaosImpact/Prototypes/SimaeBossMotions/A_Simae_Idle.A_Simae_Idle"));
	static ConstructorHelpers::FObjectFinder<UAnimSequence> Walk(TEXT("/Game/ChaosImpact/Prototypes/SimaeBossMotions/A_Simae_Walk.A_Simae_Walk"));
	static ConstructorHelpers::FObjectFinder<UAnimSequence> Charge(TEXT("/Game/ChaosImpact/Prototypes/SimaeBossMotions/A_Simae_Charge.A_Simae_Charge"));
	static ConstructorHelpers::FObjectFinder<UAnimSequence> Slam(TEXT("/Game/ChaosImpact/Prototypes/SimaeBossMotions/A_Simae_OneHandSlam.A_Simae_OneHandSlam"));
	static ConstructorHelpers::FObjectFinder<UAnimSequence> Throw(TEXT("/Game/ChaosImpact/Prototypes/SimaeBossMotions/A_Simae_HeavyThrow.A_Simae_HeavyThrow"));
	static ConstructorHelpers::FObjectFinder<UAnimSequence> PowerUp(TEXT("/Game/ChaosImpact/Prototypes/SimaeBossMotions/A_Simae_PowerUp.A_Simae_PowerUp"));
	NormalModel = Normal.Object;
	FullPowerModel = Full.Object;
	IdleClip = Idle.Object;
	WalkClip = Walk.Object;
	ChargeClip = Charge.Object;
	SlamClip = Slam.Object;
	ThrowClip = Throw.Object;
	PowerUpClip = PowerUp.Object;

	USkeletalMeshComponent* Body = GetMesh();
	Body->SetSkeletalMeshAsset(NormalModel);
	// The model faces +Y and stands on its origin.
	Body->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, -SimaeCapsuleHalfHeight * BossScale), FRotator(0.0f, -90.0f, 0.0f));
	Body->SetRelativeScale3D(FVector(BossScale));
	Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->MaxWalkSpeed = WalkSpeed;
	Movement->bOrientRotationToMovement = false;

	AuraFlames = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("AuraFlames"));
	AuraFlames->SetupAttachment(GetCapsuleComponent());
	AuraFlames->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	AuraFlames->SetGenerateOverlapEvents(false);
	AuraFlames->SetCastShadow(false);
	AuraFlames->bUseAsOccluder = false;
	// Built in the world's own space round the body each frame.
	AuraFlames->SetUsingAbsoluteLocation(true);
	AuraFlames->SetUsingAbsoluteRotation(true);
	AuraFlames->SetUsingAbsoluteScale(true);
	AuraFlames->SetVisibility(false);

	AuraLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("AuraLight"));
	AuraLight->SetupAttachment(GetCapsuleComponent());
	AuraLight->SetRelativeLocation(FVector(0.0f, 0.0f, 60.0f));
	AuraLight->SetLightColor(SimaeAuraRed);
	AuraLight->SetCastShadows(false);
	AuraLight->SetIntensity(0.0f);
	AuraLight->SetAttenuationRadius(1800.0f);
}

void AChaosImpactSimaeBoss::BeginPlay()
{
	// Sized before the character stores its mesh's scale (its knockout effect restores it).
	GetCapsuleComponent()->SetCapsuleSize(SimaeCapsuleRadius * BossScale, SimaeCapsuleHalfHeight * BossScale);
	GetMesh()->SetRelativeLocation(FVector(0.0f, 0.0f, -SimaeCapsuleHalfHeight * BossScale));
	GetMesh()->SetRelativeScale3D(FVector(BossScale));
	Super::BeginPlay();
	MeshRest = GetMesh()->GetRelativeLocation();
	if (!bDormant && GetWorld())
	{
		WakeTime = GetWorld()->GetTimeSeconds();
	}
	GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
	ShownHealth = Health;
	BuildPresentation();
	PlayClip(IdleClip, true, 1.0f);
	UE_LOG(LogChaosImpact, Log, TEXT("Simae boss %s ready (health %.0f, %s)"), *GetName(), Health, bDormant ? TEXT("asleep") : TEXT("awake"));
}

void AChaosImpactSimaeBoss::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	AbortMove();
	ChaosImpactSfx::Stop(AuraSound.Get(), 0.2f);
	Super::EndPlay(EndPlayReason);
}

void AChaosImpactSimaeBoss::SetDormant(const bool bNewDormant)
{
	if (bDormant == bNewDormant)
	{
		return;
	}
	bDormant = bNewDormant;
	if (bDormant)
	{
		WakeTime = -1.0f;
	}
	else if (GetWorld())
	{
		WakeTime = GetWorld()->GetTimeSeconds();
		RestLeft = 1.0f;
		ShakeNearby(GetActorLocation(), 5000.0f, 0.35f, 0.6f);
		ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::WindTornado, GetActorLocation(), 1.0f, 0.55f);
		AChaosImpactSimaeBlast::Play(GetWorld(), ChaosImpactSimaeFx::FindGround(GetWorld(), GetActorLocation(), this), 700.0f, 0.0f, false, 0);
	}
}

// ---------------------------------------------------------------------------------------------------- Damage

float AChaosImpactSimaeBoss::TakeDamage(const float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator,
	AActor* DamageCauser)
{
	if (bDormant || Move == EChaosImpactSimaeMove::PowerUp || IsEliminated())
	{
		return 0.0f;
	}
	// The first half of its health cannot be skipped: it always powers up on the way down.
	const float Half = MaxHealth * 0.5f;
	float Amount = DamageAmount;
	if (!bFullPower)
	{
		Amount = FMath::Min(Amount, FMath::Max(Health - Half, 0.0f));
	}
	if (Amount <= 0.0f)
	{
		return 0.0f;
	}
	const float Applied = Super::TakeDamage(Amount, DamageEvent, EventInstigator, DamageCauser);
	if (Applied > 0.0f)
	{
		HitFlash = 1.0f;
		HitShake = 1.0f;
	}
	if (IsEliminated())
	{
		PlayDefeat();
	}
	else if (!bFullPower && Health <= Half + KINDA_SMALL_NUMBER)
	{
		StartPowerUp();
	}
	return Applied;
}

bool AChaosImpactSimaeBoss::IsFoe(const AChaosImpactCharacter* Character) const
{
	return IsValid(Character) && Character != this && !Character->IsEliminated() && !Character->IsHidden()
		&& !Cast<AChaosImpactSimaeBoss>(Character) && !AChaosImpactGameState::AreTeammates(GetWorld(), this, Character);
}

bool AChaosImpactSimaeBoss::Strike(AChaosImpactSimaeBoss* Boss, AChaosImpactCharacter* Victim, AActor* Causer, const float Damage,
	const FVector& Knockback, const bool bDropBalls)
{
	if (!Boss || !Boss->HasAuthority() || !Boss->IsFoe(Victim) || Victim->IsDashing())
	{
		// A dash slips through untouched.
		return false;
	}
	if (Damage > 0.0f && !Boss->bHarmless)
	{
		Victim->TakeDamage(Damage, FDamageEvent(), Boss->GetController(), Causer ? Causer : Boss);
	}
	if (Victim->IsEliminated())
	{
		return true;
	}
	if (bDropBalls)
	{
		Victim->LoseCarriedBalls();
	}
	Victim->ApplyBlastKnockback(Boss->KeepInArena(Victim, Knockback));
	Victim->AddCameraShake(0.7f, 0.45f);
	return true;
}

const AChaosImpactSoloRoom* AChaosImpactSimaeBoss::FindHomeRoom() const
{
	if (!bLookedForRoom && GetWorld())
	{
		bLookedForRoom = true;
		for (TActorIterator<AChaosImpactSoloRoom> It(GetWorld()); It; ++It)
		{
			const FVector Local = It->RoomArea->GetComponentTransform().InverseTransformPositionNoScale(GetActorLocation());
			const FVector Extent = It->RoomArea->GetScaledBoxExtent();
			if (FMath::Abs(Local.X) <= Extent.X && FMath::Abs(Local.Y) <= Extent.Y)
			{
				HomeRoom = *It;
				break;
			}
		}
	}
	return HomeRoom.Get();
}

FVector AChaosImpactSimaeBoss::ClampToArena(const FVector& Point, const float Margin) const
{
	const AChaosImpactSoloRoom* Arena = FindHomeRoom();
	if (!Arena)
	{
		return Point;
	}
	const FTransform& Box = Arena->RoomArea->GetComponentTransform();
	const FVector Extent = Arena->RoomArea->GetScaledBoxExtent();
	FVector Local = Box.InverseTransformPositionNoScale(Point);
	Local.X = FMath::Clamp(Local.X, -Extent.X + Margin, Extent.X - Margin);
	Local.Y = FMath::Clamp(Local.Y, -Extent.Y + Margin, Extent.Y - Margin);
	const FVector Inside = Box.TransformPositionNoScale(Local);
	return FVector(Inside.X, Inside.Y, Point.Z);
}

FVector AChaosImpactSimaeBoss::KeepInArena(const AChaosImpactCharacter* Victim, const FVector& Knockback) const
{
	UWorld* World = GetWorld();
	FVector Out = Knockback;
	// Never so high it could clear the arena's walls.
	Out.Z = FMath::Min(Out.Z, 560.0f);
	FVector Flat(Out.X, Out.Y, 0.0f);
	const float Speed = Flat.Size();
	if (!World || !Victim || Speed < 1.0f)
	{
		return Out;
	}
	const FVector Direction = Flat / Speed;
	const FVector From = Victim->GetActorLocation();
	// How far it would carry them: up and down again (gravity and launch both scaled by the jump tempo), slowing in the air.
	const float Tempo = FMath::Max(Victim->GetVerticalImpulseTempo(), 1.0f);
	const float Gravity = FMath::Abs(World->GetGravityZ()) * Victim->GetCharacterMovement()->GravityScale;
	const float Air = Gravity > 1.0f ? 2.0f * FMath::Max(Out.Z, 0.0f) * Tempo / Gravity + 0.1f : 0.6f;
	const float Braking = Victim->GetCharacterMovement()->BrakingDecelerationFalling;
	const auto Travel = [Air, Braking](const float H)
	{
		const float Stops = Braking > 1.0f ? H / Braking : Air;
		const float T = FMath::Min(Air, Stops);
		return H * T - 0.5f * Braking * T * T;
	};
	// Room left that way: to the edge of its solo room's area, else to the first wall.
	constexpr float Margin = 160.0f;
	float Room = TNumericLimits<float>::Max();
	if (const AChaosImpactSoloRoom* Arena = FindHomeRoom())
	{
		const FTransform& Box = Arena->RoomArea->GetComponentTransform();
		const FVector Extent = Arena->RoomArea->GetScaledBoxExtent();
		const FVector P = Box.InverseTransformPositionNoScale(From);
		const FVector D = Box.InverseTransformVectorNoScale(Direction);
		for (int32 Axis = 0; Axis < 2; ++Axis)
		{
			if (FMath::Abs(D[Axis]) > KINDA_SMALL_NUMBER)
			{
				const float Edge = (D[Axis] > 0.0f ? Extent[Axis] : -Extent[Axis]) - FMath::Sign(D[Axis]) * Margin;
				Room = FMath::Min(Room, FMath::Max((Edge - P[Axis]) / D[Axis], 0.0f));
			}
		}
	}
	FHitResult Wall;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SimaeKnockback), false, Victim);
	Params.AddIgnoredActor(this);
	const float Reach = Travel(Speed) + 200.0f;
	if (World->SweepSingleByChannel(Wall, From, From + Direction * Reach, FQuat::Identity, ECC_WorldStatic,
		FCollisionShape::MakeSphere(40.0f), Params) && !Wall.bStartPenetrating)
	{
		Room = FMath::Min(Room, FMath::Max(Wall.Distance - Margin * 0.5f, 0.0f));
	}
	if (Travel(Speed) <= Room)
	{
		return Out;
	}
	// The speed that just reaches the room left (searched: travel grows with speed).
	float Low = 0.0f;
	float High = Speed;
	for (int32 Step = 0; Step < 16; ++Step)
	{
		const float Mid = (Low + High) * 0.5f;
		(Travel(Mid) <= Room ? Low : High) = Mid;
	}
	Flat = Direction * Low;
	return FVector(Flat.X, Flat.Y, Out.Z);
}

void AChaosImpactSimaeBoss::ShakeNearby(const FVector& Where, const float Radius, const float Strength, const float Seconds) const
{
	for (TActorIterator<AChaosImpactCharacter> It(GetWorld()); It; ++It)
	{
		if (It->IsPlayerControlled() && It->IsLocallyControlled())
		{
			const float Near = 1.0f - FVector::Dist(It->GetActorLocation(), Where) / FMath::Max(Radius, 1.0f);
			if (Near > 0.0f)
			{
				It->AddCameraShake(Strength * (0.35f + 0.65f * Near), Seconds);
			}
		}
	}
}

// ---------------------------------------------------------------------------------------------------- The fight

void AChaosImpactSimaeBoss::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ShownHealth = ShownHealth < 0.0f ? Health : FMath::FInterpConstantTo(ShownHealth, Health, DeltaSeconds, MaxHealth * 0.25f);
	UpdateAura(DeltaSeconds);
	UpdateHitFlash(DeltaSeconds);
	if (IsEliminated() || !HasAuthority())
	{
		return;
	}
	if (bDormant || IsTrainingMenuFrozen())
	{
		// Asleep, or held still (tests): it stands there.
		if (Move != EChaosImpactSimaeMove::None && Move != EChaosImpactSimaeMove::PowerUp)
		{
			EndMove();
		}
		if (Move != EChaosImpactSimaeMove::PowerUp)
		{
			PlayClip(IdleClip, true, 1.0f);
			return;
		}
	}
	AChaosImpactCharacter* Target = FindTarget();
	switch (Move)
	{
	case EChaosImpactSimaeMove::None: ThinkIdle(DeltaSeconds, Target); break;
	case EChaosImpactSimaeMove::Slam: TickSlam(DeltaSeconds); break;
	case EChaosImpactSimaeMove::Throw: TickThrow(DeltaSeconds); break;
	case EChaosImpactSimaeMove::Avalanche: TickAvalanche(DeltaSeconds); break;
	case EChaosImpactSimaeMove::Charge: TickCharge(DeltaSeconds); break;
	case EChaosImpactSimaeMove::PowerUp: TickPowerUp(DeltaSeconds); break;
	}
}

void AChaosImpactSimaeBoss::ForceMove(const EChaosImpactSimaeMove NewMove)
{
	if (IsEliminated() || bDormant)
	{
		return;
	}
	if (NewMove == EChaosImpactSimaeMove::PowerUp)
	{
		StartPowerUp();
		return;
	}
	AbortMove();
	MoveTarget = FindTarget();
	if (NewMove == EChaosImpactSimaeMove::None)
	{
		Move = EChaosImpactSimaeMove::None;
		RestLeft = 1000.0f;
		return;
	}
	BeginMove(NewMove);
}

AChaosImpactCharacter* AChaosImpactSimaeBoss::FindTarget() const
{
	AChaosImpactCharacter* Best = nullptr;
	float BestDistance = TNumericLimits<float>::Max();
	for (TActorIterator<AChaosImpactCharacter> It(GetWorld()); It; ++It)
	{
		if (!IsFoe(*It))
		{
			continue;
		}
		// Players first: a CPU on their side only when no player is there.
		const float Distance = FVector::Dist2D(It->GetActorLocation(), GetActorLocation()) + (It->IsPlayerControlled() ? 0.0f : 3000.0f);
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = *It;
		}
	}
	return Best;
}

FVector AChaosImpactSimaeBoss::Predict(const AChaosImpactCharacter* Target, const float Seconds) const
{
	if (!Target)
	{
		return GetActorLocation() + GetActorForwardVector() * 600.0f;
	}
	const FVector Velocity = Target->GetVelocity() * FVector(1.0f, 1.0f, 0.0f);
	return Target->GetActorLocation() + Velocity.GetClampedToMaxSize(900.0f) * Seconds * 0.6f;
}

void AChaosImpactSimaeBoss::TurnToward(const FVector& Direction, const float DeltaSeconds, const float DegreesPerSecond)
{
	const FVector Flat = Direction.GetSafeNormal2D();
	if (Flat.IsNearlyZero())
	{
		return;
	}
	const float Rate = DegreesPerSecond > 0.0f ? DegreesPerSecond : (bFullPower ? 260.0f : 160.0f);
	const FRotator Now = FMath::RInterpConstantTo(GetActorRotation(), Flat.Rotation(), DeltaSeconds, Rate);
	SetActorRotation(FRotator(0.0f, Now.Yaw, 0.0f));
	// The character turns toward its aim by itself; keep that aim where it already looks.
	SetAIAimDirection(Now.Vector());
}

void AChaosImpactSimaeBoss::ThinkIdle(const float DeltaSeconds, AChaosImpactCharacter* Target)
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->MaxWalkSpeed = bFullPower ? FullPowerWalkSpeed : WalkSpeed;
	if (!Target)
	{
		PlayClip(IdleClip, true, 1.0f);
		return;
	}
	FVector To = Target->GetActorLocation() - GetActorLocation();
	To.Z = 0.0f;
	const float Distance = To.Size();
	const FVector Toward = To.GetSafeNormal();
	TurnToward(Toward, DeltaSeconds);
	// Keeps a fighting distance, edging round its target.
	const float Wanted = bFullPower ? 650.0f : 850.0f;
	FVector Go = FVector::ZeroVector;
	if (Distance > Wanted + 250.0f)
	{
		Go = Toward;
	}
	else if (Distance < Wanted - 400.0f)
	{
		Go = -Toward * 0.5f;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now >= StrafeChangeAt)
	{
		StrafeSign = FMath::RandBool() ? 1.0f : -1.0f;
		StrafeChangeAt = Now + FMath::FRandRange(1.2f, 2.6f);
	}
	Go += FVector::CrossProduct(FVector::UpVector, Toward) * StrafeSign * 0.45f;
	AddMovementInput(Go.GetSafeNormal(), FMath::Clamp(Go.Size(), 0.0f, 1.0f));
	const float Speed = GetVelocity().Size2D();
	if (Speed > 40.0f && WalkClip)
	{
		const float Rate = Speed / (WalkStridePerSecond * BossScale);
		PlayClip(WalkClip, true, FMath::Clamp(Rate, 0.4f, 2.2f));
		SnowFootfalls(DeltaSeconds);
	}
	else
	{
		PlayClip(IdleClip, true, 1.0f);
	}
	RestLeft -= bHoldMoves ? 0.0f : DeltaSeconds;
	if (RestLeft <= 0.0f)
	{
		ChooseMove(Target);
	}
}

void AChaosImpactSimaeBoss::ChooseMove(AChaosImpactCharacter* Target)
{
	const float Distance = FVector::Dist2D(Target->GetActorLocation(), GetActorLocation());
	const float SlamReach = (bFullPower ? SlamLeap * 1.4f : SlamLeap) + (bFullPower ? FullPowerSlamRadius : SlamRadius) * 0.6f;
	struct FOption { EChaosImpactSimaeMove Move; float Weight; };
	TArray<FOption, TInlineAllocator<4>> Options{
		{EChaosImpactSimaeMove::Slam, Distance < SlamReach ? 5.0f : 0.0f},
		{EChaosImpactSimaeMove::Throw, Distance > 900.0f ? 4.0f : 2.0f},
		{EChaosImpactSimaeMove::Charge, Distance > 700.0f ? 2.5f : 0.4f},
		{EChaosImpactSimaeMove::Avalanche, MovesSinceAvalanche >= 2 ? (bFullPower ? 3.0f : 2.2f) : 0.0f}};
	float Total = 0.0f;
	for (FOption& Option : Options)
	{
		// Seldom the same thing twice running.
		Option.Weight *= Option.Move == LastMove ? 0.3f : 1.0f;
		Total += Option.Weight;
	}
	float Pick = FMath::FRandRange(0.0f, Total);
	EChaosImpactSimaeMove Chosen = EChaosImpactSimaeMove::Throw;
	for (const FOption& Option : Options)
	{
		if (Option.Weight > 0.0f && (Pick -= Option.Weight) <= 0.0f)
		{
			Chosen = Option.Move;
			break;
		}
	}
	MoveTarget = Target;
	BeginMove(Chosen);
}

void AChaosImpactSimaeBoss::BeginMove(const EChaosImpactSimaeMove NewMove)
{
	UWorld* World = GetWorld();
	AChaosImpactCharacter* Target = MoveTarget.Get();
	Move = NewMove;
	MoveTime = 0.0f;
	MoveStage = 0;
	bMoveFired = false;
	MoveStart = GetActorLocation();
	FVector Toward = (Predict(Target, 0.5f) - MoveStart).GetSafeNormal2D();
	if (Toward.IsNearlyZero())
	{
		Toward = GetActorForwardVector();
	}
	MoveDirection = Toward;
	GetCharacterMovement()->StopMovementImmediately();
	const FVector Ground = ChaosImpactSimaeFx::FindGround(World, MoveStart, this);
	UE_LOG(LogChaosImpact, Log, TEXT("Simae boss: %s (health %.0f%s)"), *UEnum::GetValueAsString(NewMove), Health,
		bFullPower ? TEXT(", full power") : TEXT(""));
	switch (NewMove)
	{
	case EChaosImpactSimaeMove::Slam:
	{
		// Leaps at where its target will be, so its fist comes down right there.
		// Its warning lasts a little over a second either way (enough to get out of the circle).
		MoveRate = bFullPower ? 0.92f : 0.85f;
		const float Radius = bFullPower ? FullPowerSlamRadius : SlamRadius;
		const float Reach = SlamFistReach * BossScale;
		const FVector Aim = Target ? Predict(Target, 0.7f) : MoveStart + Toward * 600.0f;
		const float Leap = FMath::Clamp(FVector::Dist2D(Aim, MoveStart) - Reach, 0.0f, SlamLeap * (bFullPower ? 1.3f : 1.0f));
		const FVector Landing = MoveStart + Toward * Leap;
		MovePoint = ChaosImpactSimaeFx::FindGround(World, Landing + Toward * Reach, this);
		MoveWarning = AChaosImpactSimaeWarning::SpawnCircle(World, MovePoint, Radius, SlamImpactAt / MoveRate, SimaeWarningRed);
		PlayClip(SlamClip, false, MoveRate);
		ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::CharThrowHeavy, MoveStart, 1.0f, 0.5f);
		break;
	}
	case EChaosImpactSimaeMove::Throw:
	{
		MoveRate = bFullPower ? 1.1f : 1.0f;
		// A plain ball, in its right fist from the start of the swing.
		if (AChaosImpactBall* Ball = SpawnPlainBall(World, PalmLocation()))
		{
			Ball->PrepareForAnimatedThrow(GetMesh(), TEXT("WRIST_R"), FVector::ZeroVector, FRotator::ZeroRotator);
			HeldBall = Ball;
		}
		const float Radius = bFullPower ? FullPowerGoukyuRadius : GoukyuRadius;
		// At full power three side by side, spaced apart so there is always a gap to stand in.
		const int32 Lanes = bFullPower ? SimaeFullPowerLanes : 1;
		for (int32 Lane = 0; Lane < Lanes; ++Lane)
		{
			FVector LaneStart;
			FVector LaneDirection;
			SimaeLane(Lane, Lanes, Ground, Toward, Radius, LaneStart, LaneDirection);
			AChaosImpactSimaeWarning* Warning = AChaosImpactSimaeWarning::SpawnLane(World, LaneStart, LaneDirection, GoukyuRange,
				Radius, ThrowRelease / MoveRate, SimaeWarningRed);
			if (Lane == (Lanes - 1) / 2)
			{
				MoveWarning = Warning;
			}
			else
			{
				ExtraWarnings.Add(Warning);
			}
		}
		PlayClip(ThrowClip, false, MoveRate);
		break;
	}
	case EChaosImpactSimaeMove::Avalanche:
		MoveRate = 1.7f;
		PlayClip(PowerUpClip, false, MoveRate);
		ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::NovaCharge, MoveStart, 0.8f, 0.6f);
		break;
	case EChaosImpactSimaeMove::Charge:
	{
		MoveRate = 1.0f;
		ChargeTravelled = 0.0f;
		bChargeCrashed = false;
		ChargeHits.Reset();
		const float Width = GetCapsuleComponent()->GetScaledCapsuleRadius() + 30.0f;
		MoveWarning = AChaosImpactSimaeWarning::SpawnLane(World, Ground, Toward, ChargeDistance, Width, 0.85f, SimaeWarningRed);
		PlayClip(ChargeClip, true, 0.12f);
		break;
	}
	default:
		break;
	}
}

void AChaosImpactSimaeBoss::AbortMove()
{
	if (AChaosImpactSimaeWarning* Warning = MoveWarning.Get())
	{
		Warning->Finish();
	}
	MoveWarning.Reset();
	for (const TWeakObjectPtr<AChaosImpactSimaeWarning>& Extra : ExtraWarnings)
	{
		if (Extra.IsValid())
		{
			Extra->Finish();
		}
	}
	ExtraWarnings.Reset();
	DropHeldBall();
	ChaosImpactSfx::Stop(MoveSound.Get(), 0.2f);
	MoveSound = nullptr;
	if (GetCharacterMovement())
	{
		GetCharacterMovement()->MaxWalkSpeed = bFullPower ? FullPowerWalkSpeed : WalkSpeed;
	}
}

void AChaosImpactSimaeBoss::EndMove()
{
	AbortMove();
	if (Move != EChaosImpactSimaeMove::PowerUp)
	{
		LastMove = Move;
		MovesSinceAvalanche = Move == EChaosImpactSimaeMove::Avalanche ? 0 : MovesSinceAvalanche + 1;
	}
	Move = EChaosImpactSimaeMove::None;
	RestLeft = (bFullPower ? FullPowerRestSeconds : RestSeconds) * FMath::FRandRange(0.8f, 1.2f);
}

void AChaosImpactSimaeBoss::DropHeldBall()
{
	if (AChaosImpactBall* Ball = HeldBall.Get())
	{
		Ball->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		Ball->MakeRollingPickup(GetActorForwardVector() * 200.0f);
	}
	HeldBall.Reset();
}

FVector AChaosImpactSimaeBoss::Bone(const FName Name) const
{
	return GetMesh()->GetSocketLocation(Name);
}

FVector AChaosImpactSimaeBoss::PalmLocation() const
{
	const FVector Wrist = Bone(TEXT("WRIST_R"));
	const FVector Hand = (Wrist - Bone(TEXT("ELBOW_R"))).GetSafeNormal();
	return Wrist + Hand * 13.0f * BossScale;
}

// ---- マッスル叩きつけ

void AChaosImpactSimaeBoss::TickSlam(const float DeltaSeconds)
{
	const float Before = MoveTime;
	MoveTime += DeltaSeconds * MoveRate;
	const float Reach = SlamFistReach * BossScale;
	const FVector Landing = MovePoint - MoveDirection * Reach;
	if (MoveTime < SlamTakeoff)
	{
		TurnToward(MoveDirection, DeltaSeconds, 400.0f);
	}
	else if (MoveTime < SlamImpactAt)
	{
		if (Before < SlamTakeoff)
		{
			// Up it goes: snow blown off the ground where it jumped.
			ChaosImpactSimaeFx::PlaySnowDust(GetWorld(), ChaosImpactSimaeFx::FindGround(GetWorld(), GetActorLocation(), this),
				380.0f, 0.2f, 60.0f);
			ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::WindGust, GetActorLocation(), 1.0f, 0.6f);
		}
		const float Air = Smooth01((MoveTime - SlamTakeoff) / (SlamImpactAt - SlamTakeoff));
		FVector At = FMath::Lerp(MoveStart, Landing, Air);
		At.Z = GetActorLocation().Z;
		SetActorLocation(At, true);
		SetActorRotation(MoveDirection.Rotation());
	}
	if (!bMoveFired && MoveTime >= SlamImpactAt)
	{
		SlamImpact();
	}
	if (MoveTime >= SlamEnd)
	{
		// At full power a target still close gets a second slam straight away (with its own full warning).
		AChaosImpactCharacter* Target = MoveTarget.Get();
		if (bFullPower && MoveStage == 0 && Target && IsFoe(Target)
			&& FVector::Dist2D(Target->GetActorLocation(), GetActorLocation()) < SlamLeap * 1.3f + FullPowerSlamRadius * 0.5f
			&& FMath::FRand() < 0.55f)
		{
			AbortMove();
			BeginMove(EChaosImpactSimaeMove::Slam);
			MoveStage = 1;
			return;
		}
		EndMove();
	}
}

void AChaosImpactSimaeBoss::SlamImpact()
{
	bMoveFired = true;
	UWorld* World = GetWorld();
	if (AChaosImpactSimaeWarning* Warning = MoveWarning.Get())
	{
		Warning->Finish();
	}
	MoveWarning.Reset();
	const float Radius = bFullPower ? FullPowerSlamRadius : SlamRadius;
	const float Heat = bFullPower ? 1.0f : 0.0f;
	// Everyone in reach is hit, thrown clear and loses every ball they had.
	for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
	{
		AChaosImpactCharacter* Victim = *It;
		const FVector At = Victim->GetActorLocation();
		if (!IsFoe(Victim) || FVector::Dist2D(At, MovePoint) > Radius + 40.0f || At.Z - MovePoint.Z > 330.0f)
		{
			continue;
		}
		FVector Out = (At - MovePoint).GetSafeNormal2D();
		if (Out.IsNearlyZero())
		{
			Out = MoveDirection;
		}
		const float Power = bFullPower ? 1.25f : 1.0f;
		Strike(this, Victim, this, 1.0f, Out * SlamKnockback * Power + FVector(0.0f, 0.0f, SlamLift * Power), true);
	}
	// The fist's own spot (where it really came down).
	const FVector Fist = Bone(TEXT("WRIST_R"));
	const FVector Ground = ChaosImpactSimaeFx::FindGround(World, FVector(Fist.X, Fist.Y, MovePoint.Z + 60.0f), this);
	AChaosImpactSimaeBlast::Play(World, Ground, Radius, Heat, true, bFullPower ? 10 : 8);
	if (bFullPower)
	{
		// A second, wider shock ring right behind the first (rings and sparks, no smoke).
		AChaosImpactSimaeBlast::Play(World, Ground, Radius * 1.3f, 1.0f, false, 0);
		if (UNiagaraSystem* Sparks = ChaosImpactBallTypes::LoadEffect(ChaosImpactBallTypes::Effects::SparkBurst))
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, Sparks, Ground + FVector(0, 0, 40), FRotator::ZeroRotator, FVector(2.0f));
		}
	}
	ChaosImpactSimaeFx::PlaySnowBurst(World, Ground, 1.0f);
	ShakeNearby(Ground, Radius * 3.5f, 1.0f, 0.7f);
	ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::NovaBlast, Ground, 1.0f, bFullPower ? 0.6f : 0.75f);
	ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::SnowBurst, Ground, 1.0f, 0.6f);
}

// ---- 剛球

void AChaosImpactSimaeBoss::TickThrow(const float DeltaSeconds)
{
	MoveTime += DeltaSeconds * MoveRate;
	if (AChaosImpactBall* Ball = HeldBall.Get())
	{
		Ball->SetActorLocation(PalmLocation());
	}
	// Aims through the first half of the swing, then the lane holds still: time enough to step out of it.
	if (MoveTime < ThrowRelease * 0.5f)
	{
		// Aims until late in the swing, then the lanes hold still: the moment to get out of them.
		if (const AChaosImpactCharacter* Target = MoveTarget.Get())
		{
			const FVector Toward = (Predict(Target, 0.45f) - GetActorLocation()).GetSafeNormal2D();
			if (!Toward.IsNearlyZero())
			{
				MoveDirection = Toward;
			}
		}
		TurnToward(MoveDirection, DeltaSeconds, 300.0f);
		const FVector Ground = ChaosImpactSimaeFx::FindGround(GetWorld(), GetActorLocation(), this);
		const int32 Lanes = 1 + ExtraWarnings.Num();
		const float Radius = bFullPower ? FullPowerGoukyuRadius : GoukyuRadius;
		int32 Extra = 0;
		for (int32 Lane = 0; Lane < Lanes; ++Lane)
		{
			FVector LaneStart;
			FVector LaneDirection;
			SimaeLane(Lane, Lanes, Ground, MoveDirection, Radius, LaneStart, LaneDirection);
			AChaosImpactSimaeWarning* Warning = Lane == (Lanes - 1) / 2 ? MoveWarning.Get()
				: ExtraWarnings.IsValidIndex(Extra) ? ExtraWarnings[Extra++].Get() : nullptr;
			if (Warning)
			{
				Warning->SetLane(LaneStart, LaneDirection, GoukyuRange);
			}
		}
	}
	else
	{
		SetActorRotation(MoveDirection.Rotation());
	}
	if (!bMoveFired && MoveTime >= ThrowRelease)
	{
		ReleaseGoukyu();
	}
	if (MoveTime >= ThrowEnd)
	{
		EndMove();
	}
}

void AChaosImpactSimaeBoss::ReleaseGoukyu()
{
	bMoveFired = true;
	UWorld* World = GetWorld();
	const FVector Hand = PalmLocation();
	const FVector Start(Hand.X, Hand.Y, FMath::Max(Hand.Z, GetActorLocation().Z - 40.0f));
	const float Radius = bFullPower ? FullPowerGoukyuRadius : GoukyuRadius;
	const float Speed = GoukyuSpeed * (bFullPower ? 1.15f : 1.0f);
	const float Heat = bFullPower ? 1.0f : 0.0f;
	const int32 Lanes = bFullPower ? SimaeFullPowerLanes : 1;
	for (int32 Lane = 0; Lane < Lanes; ++Lane)
	{
		FVector LaneStart;
		FVector LaneDirection;
		SimaeLane(Lane, Lanes, Start, MoveDirection, Radius, LaneStart, LaneDirection);
		AChaosImpactBall* Ball = Lane == (Lanes - 1) / 2 ? HeldBall.Get() : nullptr;
		if (!Ball)
		{
			// The two beside it: plain balls too, flung out from either side of the swing.
			Ball = SpawnPlainBall(World, LaneStart);
			if (Ball)
			{
				Ball->PrepareForAnimatedThrow(GetMesh(), TEXT("WRIST_R"), FVector::ZeroVector, FRotator::ZeroRotator);
			}
		}
		AChaosImpactSimaeGoukyu::Launch(this, Ball, LaneStart, LaneDirection, Speed, Radius, GoukyuRange,
			GoukyuKnockback * (bFullPower ? 1.15f : 1.0f), Heat);
	}
	HeldBall.Reset();
	if (AChaosImpactSimaeWarning* Warning = MoveWarning.Get())
	{
		Warning->Finish();
	}
	for (const TWeakObjectPtr<AChaosImpactSimaeWarning>& Extra : ExtraWarnings)
	{
		if (Extra.IsValid())
		{
			Extra->Finish();
		}
	}
	MoveWarning.Reset();
	ExtraWarnings.Reset();
	const FVector Ground = ChaosImpactSimaeFx::FindGround(World, GetActorLocation(), this);
	AChaosImpactSimaeBlast::Play(World, Ground + MoveDirection * 150.0f, 260.0f, Heat, false, 0);
	ShakeNearby(Start, 2500.0f, 0.6f, 0.4f);
	ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::CharThrowHeavy, Start, 1.0f, 0.55f);
}

// ---- 雪崩落とし

void AChaosImpactSimaeBoss::TickAvalanche(const float DeltaSeconds)
{
	const float Before = MoveTime;
	MoveTime += DeltaSeconds * MoveRate;
	if (const AChaosImpactCharacter* Target = MoveTarget.Get())
	{
		TurnToward(Target->GetActorLocation() - GetActorLocation(), DeltaSeconds);
	}
	// Gathering itself: the snow shivers up round its feet.
	if (MoveTime > 0.5f && MoveTime < RoarAt && FMath::FloorToInt(Before * 5.0f) != FMath::FloorToInt(MoveTime * 5.0f))
	{
		const FVector Out = FRotator(0.0f, FMath::FRandRange(0.0f, 360.0f), 0.0f).Vector();
		ChaosImpactSimaeFx::PlaySnowDust(GetWorld(), ChaosImpactSimaeFx::FindGround(GetWorld(), GetActorLocation() + Out * 220.0f, this),
			260.0f, 0.15f, 40.0f);
	}
	if (!bMoveFired && MoveTime >= RoarAt)
	{
		DropAvalanche();
	}
	if (MoveTime >= PowerUpEnd)
	{
		EndMove();
	}
}

void AChaosImpactSimaeBoss::DropAvalanche()
{
	bMoveFired = true;
	UWorld* World = GetWorld();
	const AChaosImpactCharacter* Target = MoveTarget.Get();
	const FVector Middle = Target ? Predict(Target, 1.0f) : GetActorLocation() + GetActorForwardVector() * 800.0f;
	const FVector Floor = ChaosImpactSimaeFx::FindGround(World, GetActorLocation(), this);
	const int32 Count = bFullPower ? FullPowerAvalancheCount : AvalancheCount;
	const float Size = bFullPower ? 110.0f : 90.0f;
	const float Reach = bFullPower ? 200.0f : 170.0f;
	TArray<FVector, TInlineAllocator<12>> Spots;
	for (int32 Try = 0; Spots.Num() < Count && Try < Count * 8; ++Try)
	{
		// The first right on its target; the rest scattered round them, none on top of another.
		const FVector Offset = Spots.IsEmpty() ? FVector::ZeroVector
			: FRotator(0.0f, FMath::FRandRange(0.0f, 360.0f), 0.0f).Vector() * FMath::FRandRange(Reach * 2.4f, 1400.0f);
		const FVector Ground = ChaosImpactSimaeFx::FindGround(World, FVector(Middle.X, Middle.Y, Floor.Z + 100.0f) + Offset, this);
		if (FMath::Abs(Ground.Z - Floor.Z) > 250.0f
			|| Spots.ContainsByPredicate([&](const FVector& Spot) { return FVector::Dist2D(Spot, Ground) < Reach * 2.4f; }))
		{
			continue;
		}
		Spots.Add(Ground);
	}
	for (int32 Index = 0; Index < Spots.Num(); ++Index)
	{
		AChaosImpactSimaeBoulder::Drop(this, Spots[Index], Size * FMath::FRandRange(0.85f, 1.15f), Reach,
			AvalancheWarningSeconds + Index * 0.18f, 1000.0f, bFullPower ? 1.0f : 0.0f);
	}
	// The roar: a shock round it, and the ground shakes.
	AChaosImpactSimaeBlast::Play(World, Floor, 700.0f, bFullPower ? 1.0f : 0.0f, false, 0);
	ShakeNearby(Floor, 6000.0f, 0.55f, 1.1f);
	ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::WindTornado, Floor, 1.0f, 0.5f);
}

// ---- 突進

void AChaosImpactSimaeBoss::TickCharge(const float DeltaSeconds)
{
	UWorld* World = GetWorld();
	MoveTime += DeltaSeconds;
	const float Windup = 0.85f;
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (MoveStage == 0)
	{
		// Pawing the snow, lining up on its target (the lane holds still for the last moment).
		if (MoveTime < Windup * 0.55f)
		{
			if (const AChaosImpactCharacter* Target = MoveTarget.Get())
			{
				const FVector Toward = (Predict(Target, 0.3f) - GetActorLocation()).GetSafeNormal2D();
				if (!Toward.IsNearlyZero())
				{
					MoveDirection = Toward;
				}
			}
			if (AChaosImpactSimaeWarning* Warning = MoveWarning.Get())
			{
				Warning->SetLane(GetActorLocation(), MoveDirection, ChargeDistance);
			}
		}
		TurnToward(MoveDirection, DeltaSeconds, 500.0f);
		if (MoveTime >= Windup)
		{
			MoveStage = 1;
			NextDustAt = 0.0f;
			SetActorRotation(MoveDirection.Rotation());
			if (AChaosImpactSimaeWarning* Warning = MoveWarning.Get())
			{
				Warning->Finish();
			}
			MoveWarning.Reset();
			Movement->MaxWalkSpeed = ChargeSpeed * Tempo();
			PlayClip(ChargeClip, true, 1.25f * Tempo());
			ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::DriveBurst, GetActorLocation(), 1.0f, 0.6f);
			MoveSound = ChaosImpactSfx::PlayAttached(EChaosImpactSfx::WindLoop, GetRootComponent(), 1.0f, 0.7f);
			ChaosImpactSimaeFx::PlaySnowBurst(World, ChaosImpactSimaeFx::FindGround(World, GetActorLocation(), this), 0.7f);
		}
		return;
	}
	if (MoveStage == 1)
	{
		const float Running = MoveTime - Windup;
		AddMovementInput(MoveDirection, 1.0f);
		SetActorRotation(MoveDirection.Rotation());
		const float Speed = GetVelocity().Size2D();
		ChargeTravelled += Speed * DeltaSeconds;
		// Whoever it runs into is bowled over.
		const float Reach = GetCapsuleComponent()->GetScaledCapsuleRadius() + 45.0f;
		for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
		{
			AChaosImpactCharacter* Victim = *It;
			if (!IsFoe(Victim) || ChargeHits.Contains(Victim)
				|| FVector::Dist2D(Victim->GetActorLocation(), GetActorLocation()) > Reach)
			{
				continue;
			}
			ChargeHits.Add(Victim);
			const FVector Side = FVector::CrossProduct(FVector::UpVector, MoveDirection);
			const float Which = FVector::DotProduct(Victim->GetActorLocation() - GetActorLocation(), Side) >= 0.0f ? 1.0f : -1.0f;
			if (Strike(this, Victim, this, 1.0f, MoveDirection * 1100.0f + Side * Which * 500.0f + FVector(0, 0, 520.0f), false))
			{
				AChaosImpactSimaeBlast::Play(World, ChaosImpactSimaeFx::FindGround(World, Victim->GetActorLocation(), Victim), 200.0f,
					bFullPower ? 1.0f : 0.0f, false, 3);
				ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::BallHit, Victim->GetActorLocation(), 1.0f, 0.6f);
			}
		}
		// Snow sprayed up behind its feet.
		if (ChargeTravelled >= NextDustAt)
		{
			NextDustAt = ChargeTravelled + 400.0f;
			const FVector Ground = ChaosImpactSimaeFx::FindGround(World, GetActorLocation(), this);
			ChaosImpactSimaeFx::PlaySnowDust(World, Ground - MoveDirection * 80.0f, 200.0f, 0.08f, 12.0f);
			ShakeNearby(Ground, 1800.0f, 0.18f, 0.25f);
		}
		const bool bBlocked = Running > 0.3f && Speed < ChargeSpeed * Tempo() * 0.3f;
		if (ChargeTravelled >= ChargeDistance || bBlocked || Running > 3.0f)
		{
			// Skids to a stop (or crashes into a wall).
			MoveStage = 2;
			MoveTime = 0.0f;
			bChargeCrashed = bBlocked;
			Movement->StopMovementImmediately();
			Movement->MaxWalkSpeed = bFullPower ? FullPowerWalkSpeed : WalkSpeed;
			ChaosImpactSfx::Stop(MoveSound.Get(), 0.2f);
			MoveSound = nullptr;
			const FVector Front = ChaosImpactSimaeFx::FindGround(World, GetActorLocation() + MoveDirection * 150.0f, this);
			AChaosImpactSimaeBlast::Play(World, Front, bBlocked ? 450.0f : 260.0f, bFullPower ? 1.0f : 0.0f, bBlocked, bBlocked ? 6 : 0);
			ChaosImpactSimaeFx::PlaySnowBurst(World, Front, bBlocked ? 1.0f : 0.6f);
			ShakeNearby(Front, 3000.0f, bBlocked ? 0.8f : 0.3f, 0.5f);
			ChaosImpactSfx::PlayAt(this, bBlocked ? EChaosImpactSfx::NovaBlast : EChaosImpactSfx::SnowBurst, Front, 0.9f, 0.6f);
			PlayClip(IdleClip, true, 1.0f);
		}
		return;
	}
	// Getting its breath back (longer after running into a wall).
	if (MoveTime >= (bChargeCrashed ? 1.1f : 0.6f) / Tempo())
	{
		EndMove();
	}
}

// ---- Full power

void AChaosImpactSimaeBoss::StartPowerUp()
{
	AbortMove();
	GetCharacterMovement()->StopMovementImmediately();
	Move = EChaosImpactSimaeMove::PowerUp;
	MoveTime = 0.0f;
	MoveRate = 1.0f;
	bMoveFired = false;
	PlayClip(PowerUpClip, false, 1.0f);
	AuraTarget = 0.12f;
	ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::NovaCharge, GetActorLocation(), 1.0f, 0.5f);
	ShakeNearby(GetActorLocation(), 6000.0f, 0.3f, 0.6f);
	UE_LOG(LogChaosImpact, Log, TEXT("Simae boss: powering up"));
}

void AChaosImpactSimaeBoss::TickPowerUp(const float DeltaSeconds)
{
	UWorld* World = GetWorld();
	const float Before = MoveTime;
	MoveTime += DeltaSeconds;
	if (const AChaosImpactCharacter* Target = FindTarget())
	{
		TurnToward(Target->GetActorLocation() - GetActorLocation(), DeltaSeconds, 90.0f);
	}
	if (MoveTime < RoarAt)
	{
		// Power welling up: the aura flickering into life, pulses of red running out over the snow, the snow shivering.
		AuraTarget = FMath::Lerp(0.12f, 0.55f, Smooth01((MoveTime - 0.55f) / (RoarAt - 0.55f)));
		if (FMath::FloorToInt(Before * 4.0f) != FMath::FloorToInt(MoveTime * 4.0f))
		{
			const FVector Ground = ChaosImpactSimaeFx::FindGround(World, GetActorLocation(), this);
			AChaosImpactSimaeBlast::Play(World, Ground, 350.0f + MoveTime * 120.0f, 1.0f, false, 0);
			// Sparks of fire spitting off it as the power wells up.
			if (MoveTime > 0.5f)
			{
				AChaosImpactSimaeFlameBurst::Play(World, Ground, SimaeCapsuleHalfHeight * 2.0f * BossScale, 5, false);
			}
			ChaosImpactSimaeFx::PlaySnowDust(World, Ground + FRotator(0.0f, FMath::FRandRange(0.0f, 360.0f), 0.0f).Vector() * 250.0f,
				200.0f, 0.08f, 12.0f);
			ShakeNearby(Ground, 5000.0f, 0.25f + MoveTime * 0.1f, 0.3f);
		}
	}
	if (!bMoveFired && MoveTime >= RoarAt)
	{
		Transform();
	}
	if (MoveTime >= PowerUpEnd)
	{
		EndMove();
		RestLeft = 0.4f;
	}
}

void AChaosImpactSimaeBoss::Transform()
{
	bMoveFired = true;
	bFullPower = true;
	UWorld* World = GetWorld();
	// The full-power body, carrying on from the same moment of the clip.
	if (FullPowerModel)
	{
		const float At = GetMesh()->GetPosition();
		GetMesh()->SetSkeletalMeshAsset(FullPowerModel);
		PlayingClip = nullptr;
		PlayClip(PowerUpClip, false, 1.0f);
		GetMesh()->SetPosition(At, false);
	}
	AuraTarget = 1.0f;
	AuraBurst = 1.0f;
	GetCharacterMovement()->MaxWalkSpeed = FullPowerWalkSpeed;
	const FVector Ground = ChaosImpactSimaeFx::FindGround(World, GetActorLocation(), this);
	// The release: a gust pushes everyone near back a little along the ground (no harm, feet down), a red shock races out.
	for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
	{
		AChaosImpactCharacter* Near = *It;
		const float Distance = FVector::Dist2D(Near->GetActorLocation(), Ground);
		if (IsFoe(Near) && Distance < 1500.0f)
		{
			const float Push = FMath::Lerp(520.0f, 220.0f, Distance / 1500.0f);
			Near->StartWindKnockback((Near->GetActorLocation() - Ground).GetSafeNormal2D() * Push, 0.8f, false);
			Near->AddCameraShake(0.5f, 0.5f);
		}
	}
	// It bursts into flame: a column of fire round it and blobs of fire flung far and wide, burning where they land.
	AChaosImpactSimaeFlameBurst::Play(World, Ground, SimaeCapsuleHalfHeight * 2.0f * BossScale, 30);
	AChaosImpactSimaeBlast::Play(World, Ground, 1200.0f, 1.0f, true, 10);
	AChaosImpactSimaeBlast::Play(World, Ground, 700.0f, 1.0f, false, 0);
	if (UNiagaraSystem* Sparks = ChaosImpactBallTypes::LoadEffect(ChaosImpactBallTypes::Effects::SparkBurst))
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, Sparks, GetActorLocation(), FRotator::ZeroRotator, FVector(4.0f));
	}
	ChaosImpactSimaeFx::PlaySnowBurst(World, Ground, 1.4f);
	ShakeNearby(Ground, 8000.0f, 1.0f, 1.2f);
	ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::NovaBlast, Ground, 1.0f, 0.55f);
	ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::FireExplode, Ground, 1.0f, 0.6f);
	UE_LOG(LogChaosImpact, Log, TEXT("Simae boss: FULL POWER"));
}

void AChaosImpactSimaeBoss::PlayDefeat()
{
	AbortMove();
	Move = EChaosImpactSimaeMove::None;
	UWorld* World = GetWorld();
	const FVector Ground = ChaosImpactSimaeFx::FindGround(World, GetActorLocation(), this);
	AChaosImpactSimaeBlast::Play(World, Ground, 900.0f, bFullPower ? 1.0f : 0.0f, true, 12);
	ChaosImpactSimaeFx::PlaySnowBurst(World, Ground, 1.6f);
	ShakeNearby(Ground, 8000.0f, 1.0f, 1.0f);
	ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::NovaBlast, Ground, 1.0f, 0.5f);
	// The aura goes out with it.
	AuraTarget = 0.0f;
	AuraStrength = 0.0f;
	AuraFlames->SetVisibility(false);
	UpdateAuraShell(false, 0.0f);
	AuraLight->SetIntensity(0.0f);
	GetMesh()->SetOverlayMaterial(nullptr);
	if (Embers)
	{
		Embers->Deactivate();
	}
	ChaosImpactSfx::Stop(AuraSound.Get(), 0.3f);
	AuraSound = nullptr;
	UE_LOG(LogChaosImpact, Log, TEXT("Simae boss defeated"));
}

// ---------------------------------------------------------------------------------------------------- Presentation

void AChaosImpactSimaeBoss::PlayClip(UAnimSequence* Clip, const bool bLoop, const float Rate)
{
	USkeletalMeshComponent* Body = GetMesh();
	if (!Clip || !Body)
	{
		return;
	}
	// A loop carries on if it is already playing; a one-shot always starts from its beginning.
	if (!bLoop || Clip != PlayingClip || bLoop != bPlayingLoop)
	{
		Body->PlayAnimation(Clip, bLoop);
		PlayingClip = Clip;
		bPlayingLoop = bLoop;
	}
	Body->SetPlayRate(Rate);
}

void AChaosImpactSimaeBoss::SnowFootfalls(const float DeltaSeconds)
{
	if (PlayingClip != WalkClip || !WalkClip)
	{
		return;
	}
	// A foot comes down at the start and the middle of the stride.
	const float Length = WalkClip->GetPlayLength();
	const float Phase = FMath::Fmod(GetMesh()->GetPosition(), Length) / Length;
	const bool bStep = (WalkPhase < 0.5f && Phase >= 0.5f) || Phase < WalkPhase;
	WalkPhase = Phase;
	if (!bStep)
	{
		return;
	}
	const FVector Foot = Bone(Phase >= 0.5f ? TEXT("TOE1_L") : TEXT("TOE1_R"));
	ChaosImpactSimaeFx::PlaySnowDust(GetWorld(), ChaosImpactSimaeFx::FindGround(GetWorld(), Foot, this), 160.0f, 0.06f, 10.0f);
	ShakeNearby(Foot, 1500.0f, bFullPower ? 0.2f : 0.12f, 0.2f);
	ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::CharLand, Foot, 0.8f, 0.5f);
}

void AChaosImpactSimaeBoss::BuildPresentation()
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	// The aura's flame cards: smoky tongues of red rising from the ground round the feet and wrapping the whole body
	// (M_CIFX_AuraFlame: every card edge fades out, so none is ever cut off).
	if (UMaterialInterface* Flame = LoadSimaeMaterial(TEXT("/Game/ChaosImpact/FX/Kit/M_CIFX_AuraFlame.M_CIFX_AuraFlame")))
	{
		FlameMaterial = UMaterialInstanceDynamic::Create(Flame, this);
	}
	else
	{
		FlameMaterial = ChaosImpactBallTypes::MakeAdditive(this, SimaeAuraRed, 0.0f);
	}
	AuraFlames->SetMaterial(0, FlameMaterial);
	if (UMaterialInterface* Rim = LoadSimaeMaterial(TEXT("/Game/ChaosImpact/FX/M_CI_SimaeAuraRim.M_CI_SimaeAuraRim")))
	{
		RimMaterial = UMaterialInstanceDynamic::Create(Rim, this);
		// The same glow in white, flashed over the normal body when it is hit.
		FlashMaterial = UMaterialInstanceDynamic::Create(Rim, this);
		FlashMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.0f, 0.95f, 0.95f));
		FlashMaterial->SetScalarParameterValue(TEXT("Base"), 0.6f);
		FlashMaterial->SetScalarParameterValue(TEXT("Intensity"), 0.0f);
	}
	if (UNiagaraSystem* Sparks = ChaosImpactBallTypes::LoadEffect(ChaosImpactBallTypes::Effects::WindSparks))
	{
		Embers = UNiagaraFunctionLibrary::SpawnSystemAttached(Sparks, GetCapsuleComponent(), NAME_None, FVector(0, 0, 40.0f),
			FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset, false, false);
		if (Embers)
		{
			Embers->SetUsingAbsoluteScale(true);
			Embers->SetWorldScale3D(FVector(BossScale * 1.2f));
			ChaosImpactBallTypes::SetEffectFloat(Embers, TEXT("Spark Spawn Rate"), 140.0f);
		}
	}

	// Where the flames burn (in model centimetres, scaled with it): a great blaze rising behind the body, tongues licking
	// out at its silhouette (shoulders, arms, legs; X is pushed outward from the body), a few thin ones in front.
	FRandomStream Stream(777);
	const float K = BossScale / 2.25f;
	const auto Add = [&](const TCHAR* BoneName, const FVector& Offset, const float Width, const float Height)
	{
		FlameCards.Add({FName(BoneName), Offset * K, Width * K, Height * K, Stream.FRand()});
	};
	Add(TEXT("SPINE2"), FVector(0, 70, -60), 320, 560);
	Add(TEXT("SPINE2"), FVector(-120, 60, -40), 250, 470);
	Add(TEXT("SPINE2"), FVector(120, 60, -40), 250, 470);
	Add(TEXT("SPINE3"), FVector(-200, 50, -80), 210, 380);
	Add(TEXT("SPINE3"), FVector(200, 50, -80), 210, 380);
	Add(TEXT("NECK"), FVector(0, 50, 20), 240, 360);
	Add(TEXT("WAIST"), FVector(-170, 50, -80), 200, 320);
	Add(TEXT("WAIST"), FVector(170, 50, -80), 200, 320);
	Add(TEXT("WAIST"), FVector(0, 60, -150), 280, 320);
	Add(TEXT("SHOULDER_L"), FVector(70, 10, -10), 160, 280);
	Add(TEXT("SHOULDER_R"), FVector(70, 10, -10), 160, 280);
	Add(TEXT("ELBOW_L"), FVector(60, 0, -30), 130, 230);
	Add(TEXT("ELBOW_R"), FVector(60, 0, -30), 130, 230);
	Add(TEXT("WRIST_L"), FVector(45, 0, -30), 120, 210);
	Add(TEXT("WRIST_R"), FVector(45, 0, -30), 120, 210);
	Add(TEXT("THIGH_L"), FVector(60, 0, -40), 140, 230);
	Add(TEXT("THIGH_R"), FVector(60, 0, -40), 140, 230);
	Add(TEXT("CLANK_L"), FVector(55, 0, -50), 130, 210);
	Add(TEXT("CLANK_R"), FVector(55, 0, -50), 130, 210);
	Add(TEXT("SPINE2"), FVector(-50, -50, -50), 130, 210);
	Add(TEXT("SPINE2"), FVector(50, -50, -50), 130, 210);
	Add(TEXT("HEAD"), FVector(0, 30, 20), 180, 260);
}

void AChaosImpactSimaeBoss::UpdateAura(const float DeltaSeconds)
{
	if (GetNetMode() == NM_DedicatedServer || !GetWorld())
	{
		return;
	}
	if (IsEliminated())
	{
		return;
	}
	AuraBurst = FMath::Max(0.0f, AuraBurst - DeltaSeconds * 0.9f);
	AuraStrength = FMath::FInterpTo(AuraStrength, AuraTarget, DeltaSeconds, 5.0f);
	const float Shown = AuraStrength + AuraBurst;
	const bool bVisible = Shown > 0.02f;
	const float Time = GetWorld()->GetTimeSeconds();
	AuraFlames->SetVisibility(bVisible);
	UpdateAuraShell(bVisible, Shown);
	if (bVisible)
	{
		RebuildAuraFlames(Time);
		if (FlameMaterial)
		{
			FlameMaterial->SetScalarParameterValue(TEXT("Intensity"), 2.2f + 1.0f * AuraBurst);
			FlameMaterial->SetScalarParameterValue(TEXT("Opacity"), 0.9f * FMath::Min(Shown, 1.0f));
			FlameMaterial->SetScalarParameterValue(TEXT("Speed"), 1.7f + 0.8f * AuraBurst);
		}
		FeedAura(DeltaSeconds, Shown);
	}
	const float Flicker = 0.85f + 0.1f * FMath::Sin(Time * 17.0f) + 0.05f * FMath::Sin(Time * 41.0f);
	AuraLight->SetIntensity(bVisible ? 26000.0f * Shown * Flicker : 0.0f);
	if (RimMaterial && bFullPower)
	{
		RimMaterial->SetScalarParameterValue(TEXT("Intensity"), (1.5f * Shown + HitFlash * 3.0f) * Flicker);
	}
	if (Embers)
	{
		if (bFullPower && !Embers->IsActive())
		{
			Embers->Activate(true);
		}
	}
	if (bFullPower && !AuraSound && Move != EChaosImpactSimaeMove::PowerUp)
	{
		AuraSound = ChaosImpactSfx::PlayAttached(EChaosImpactSfx::FireLoop, GetRootComponent(), 0.6f, 0.55f);
	}
}

void AChaosImpactSimaeBoss::UpdateAuraShell(const bool bVisible, const float Shown)
{
	USkeletalMeshComponent* Body = GetMesh();
	if (!bVisible)
	{
		if (AuraShell)
		{
			AuraShell->SetVisibility(false);
		}
		return;
	}
	if (!AuraShell && Body)
	{
		UMaterialInterface* Look = LoadSimaeMaterial(TEXT("/Game/ChaosImpact/FX/Kit/M_CIFX_AuraShell.M_CIFX_AuraShell"));
		if (!Look)
		{
			return;
		}
		AuraShellMaterial = UMaterialInstanceDynamic::Create(Look, this);
		AuraShell = NewObject<USkeletalMeshComponent>(this, TEXT("AuraShell"));
		AuraShell->SetupAttachment(Body);
		AuraShell->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		AuraShell->SetGenerateOverlapEvents(false);
		AuraShell->SetCastShadow(false);
		AuraShell->bReceivesDecals = false;
		AuraShell->RegisterComponent();
	}
	if (!AuraShell)
	{
		return;
	}
	// Always the body it wraps (it changes model as it powers up), in the body's pose.
	if (AuraShell->GetSkeletalMeshAsset() != Body->GetSkeletalMeshAsset())
	{
		AuraShell->SetSkeletalMeshAsset(Body->GetSkeletalMeshAsset());
		for (int32 Slot = 0; Slot < AuraShell->GetNumMaterials(); ++Slot)
		{
			AuraShell->SetMaterial(Slot, AuraShellMaterial);
		}
		AuraShell->SetLeaderPoseComponent(Body);
	}
	AuraShell->SetVisibility(true);
	const float Time = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	const float Flicker = 0.88f + 0.08f * FMath::Sin(Time * 17.0f) + 0.04f * FMath::Sin(Time * 41.0f);
	AuraShellMaterial->SetScalarParameterValue(TEXT("Opacity"), 0.55f * FMath::Min(Shown, 1.0f) * Flicker);
	AuraShellMaterial->SetScalarParameterValue(TEXT("Intensity"), 1.8f + 0.8f * AuraBurst);
	AuraShellMaterial->SetScalarParameterValue(TEXT("Inflate"), (6.0f + 4.0f * AuraBurst) * BossScale);
	AuraShellMaterial->SetScalarParameterValue(TEXT("NoiseScale"), 0.006f / FMath::Max(BossScale / 2.25f, 0.3f));
}

bool AChaosImpactSimaeBoss::IsAuraDrawn() const
{
	const AChaosImpactFxKit* Fx = AuraFx.Get();
	return Fx && Fx->GetPieceCount() > 0;
}

void AChaosImpactSimaeBoss::FeedAura(const float DeltaSeconds, const float Shown)
{
	AChaosImpactFxKit* Fx = AuraFx.Get();
	if (!Fx && !FlameCards.IsEmpty())
	{
		Fx = AChaosImpactFxKit::Spawn(this, GetActorLocation());
		if (Fx)
		{
			Fx->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);
			AuraStream.Initialize(FMath::Rand());
			AuraFx = Fx;
		}
	}
	if (!Fx)
	{
		return;
	}
	const FVector Base = Fx->GetActorLocation();
	const float Strength = FMath::Min(Shown, 1.6f);
	const FLinearColor Deep(0.78f, 0.02f, 0.07f);
	const FLinearColor Hot(1.0f, 0.42f, 0.36f);
	// Its pressure on the floor: the ground glowing red under it and waves pulsing out, slower than the flames.
	AuraPulseOwed += DeltaSeconds * 1.2f * Strength;
	if (AuraPulseOwed >= 1.0f)
	{
		AuraPulseOwed -= 1.0f;
		const FVector Floor = ChaosImpactSimaeFx::FindGround(GetWorld(), GetActorLocation(), this) + FVector(0.0f, 0.0f, 4.0f) - Base;
		FChaosImpactFxPiece Wave;
		Wave.Look = EChaosImpactFxLook::SolidRing;
		Wave.Location = Floor;
		Wave.Life = 0.95f;
		Wave.SizeFrom = Wave.SizeTo = 260.0f * BossScale;
		Wave.RingFrom = 0.25f;
		Wave.RingTo = 0.95f;
		Wave.RingWidth = 0.05f;
		Wave.Color = Deep;
		Wave.CoreColor = Hot;
		Wave.Intensity = 1.0f;
		Wave.Opacity = 0.5f * FMath::Min(Strength, 1.0f);
		Wave.Attack = 0.15f;
		Wave.ErodeTo = 0.8f;
		Wave.NoiseScale = 2.5f;
		Fx->Add(Wave);
		FChaosImpactFxPiece Lit;
		Lit.Look = EChaosImpactFxLook::GroundFlame;
		Lit.Location = Floor + FVector(0.0f, 0.0f, -1.0f);
		Lit.Life = 1.4f;
		Lit.SizeFrom = 150.0f * BossScale;
		Lit.SizeTo = 175.0f * BossScale;
		Lit.Color = Deep;
		Lit.CoreColor = FLinearColor(1.0f, 0.3f, 0.2f);
		Lit.Intensity = 1.0f;
		Lit.Opacity = 0.3f * FMath::Min(Strength, 1.0f);
		Lit.Attack = 0.3f;
		Lit.NoiseScale = 2.0f;
		Lit.NoiseSpeed = 0.6f;
		Lit.NoiseAmount = 0.6f;
		Lit.Softness = 0.8f;
		Lit.Spin = 30.0f;
		Fx->Add(Lit);
	}
}

void AChaosImpactSimaeBoss::RebuildAuraFlames(const float Time)
{
	// Flame cards turned to the camera, rooted on the ground in a ring round the feet and rising past the head: the
	// aura wraps the whole body from below. Two rings: a wide outer one, deep red, tall behind the body and low in front
	// of it (the body stays in view), and a narrow inner one, hotter. Each licks, sways and leans on its own, the
	// tops streaming back when it runs.
	FVector CameraLocation = GetActorLocation() - GetActorForwardVector() * 1000.0f + FVector(0, 0, 800.0f);
	FRotator CameraRotation = (GetActorLocation() - CameraLocation).Rotation();
	if (const APlayerController* Viewer = GetWorld()->GetFirstPlayerController(); Viewer && Viewer->PlayerCameraManager)
	{
		CameraLocation = Viewer->PlayerCameraManager->GetCameraLocation();
		CameraRotation = Viewer->PlayerCameraManager->GetCameraRotation();
	}
	const FRotationMatrix Axes(CameraRotation);
	const FVector Ahead = Axes.GetUnitAxis(EAxis::X);
	const FVector Right = Axes.GetUnitAxis(EAxis::Y);
	// World up, turned flat to the camera (up the screen, as near world up as a card facing the camera can be).
	FVector Up = FVector::UpVector - Ahead * FVector::DotProduct(FVector::UpVector, Ahead);
	Up = Up.IsNearlyZero() ? Axes.GetUnitAxis(EAxis::Z) : Up.GetSafeNormal();
	const FVector Feet = ChaosImpactSimaeFx::FindGround(GetWorld(), GetActorLocation(), this);
	const float Tall = FMath::Max(Bone(TEXT("HEAD")).Z - Feet.Z, 150.0f);
	const float K = BossScale / 2.25f;
	const FVector Trail = -GetVelocity() * FVector(1.0f, 1.0f, 0.0f) * 0.15f;
	const float Shown = AuraStrength + AuraBurst;
	const float Grow = FMath::Clamp(0.6f + 0.4f * Shown + 0.35f * AuraBurst, 0.3f, 1.6f);
	const FVector ToCamera2D = (-Ahead * FVector(1.0f, 1.0f, 0.0f)).GetSafeNormal();

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colors;
	constexpr int32 Rows = 6;
	constexpr int32 Columns = 3;
	const auto Card = [&](const FVector& Root, const float Width, const float Height, const float Seed, const float Strength, const float Lean)
	{
		const int32 Base = Vertices.Num();
		for (int32 Row = 0; Row < Rows; ++Row)
		{
			const float V = static_cast<float>(Row) / (Rows - 1);
			// Leaning and swaying more the higher it goes; narrowing a little toward its tip.
			const FVector Centre = Root + Up * Height * V + (Right * Lean * Width + Trail) * V * V;
			const float Half = Width * 0.5f * (1.0f - 0.4f * V);
			for (int32 Column = 0; Column < Columns; ++Column)
			{
				const float U = static_cast<float>(Column) / (Columns - 1);
				Vertices.Add(Centre + Right * (U * 2.0f - 1.0f) * Half - Feet);
				Normals.Add(-Ahead);
				UVs.Add(FVector2D(U, V));
				Colors.Add(FLinearColor(Seed, Strength, 0.0f, 1.0f));
			}
		}
		for (int32 Row = 0; Row < Rows - 1; ++Row)
		{
			for (int32 Column = 0; Column < Columns - 1; ++Column)
			{
				const int32 A = Base + Row * Columns + Column;
				Triangles.Append({A, A + Columns, A + Columns + 1, A, A + Columns + 1, A + 1});
			}
		}
	};
	const auto Ring = [&](const int32 Count, const float Radius, const float Width, const float Height, const float Strength, const float SeedBase)
	{
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const float Seed = FMath::Frac(SeedBase + Index * 0.618f);
			const float Angle = (Index + 0.5f * Seed) * UE_TWO_PI / Count + Time * 0.35f;
			const FVector Out(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f);
			// In front of the body (toward the camera) lower and fainter, behind it full height.
			const float Front = FMath::Clamp(FVector::DotProduct(Out, ToCamera2D), 0.0f, 1.0f);
			// Each its own height (some reaching higher), licking up and down: the tops never line up.
			const float Lick = 0.8f + 0.2f * FMath::Sin(Time * 6.1f + Seed * 37.0f) + 0.1f * FMath::Sin(Time * 14.3f + Seed * 11.0f);
			const float H = Height * Grow * Lick * FMath::Lerp(0.7f, 1.2f, FMath::Frac(Seed * 7.31f)) * FMath::Lerp(1.0f, 0.6f, Front);
			const float W = Width * (0.9f + 0.15f * FMath::Sin(Time * 4.3f + Seed * 23.0f));
			const float Lean = 0.22f * FMath::Sin(Time * 2.3f + Seed * 17.0f) + FVector::DotProduct(Out, Right) * 0.18f;
			Card(Feet + Out * Radius * K, W * K, H, Seed, Strength * FMath::Lerp(1.0f, 0.75f, Front), Lean);
		}
	};
	Ring(14, 115.0f, 210.0f, Tall * 1.55f, 1.0f, 0.13f);
	Ring(9, 55.0f, 120.0f, Tall * 1.3f, 0.85f, 0.71f);
	AuraFlames->SetWorldLocationAndRotation(Feet, FRotator::ZeroRotator);
	AuraFlames->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, Colors, TArray<FProcMeshTangent>(), false);
}

void AChaosImpactSimaeBoss::UpdateHitFlash(const float DeltaSeconds)
{
	HitFlash = FMath::Max(0.0f, HitFlash - DeltaSeconds / 0.2f);
	HitShake = FMath::Max(0.0f, HitShake - DeltaSeconds / 0.18f);
	USkeletalMeshComponent* Body = GetMesh();
	if (!Body || IsEliminated())
	{
		return;
	}
	// A shudder where the ball struck.
	Body->SetRelativeLocation(MeshRest + (HitShake > 0.0f ? FMath::VRand() * 7.0f * BossScale * HitShake : FVector::ZeroVector));
	if (bFullPower)
	{
		if (RimMaterial && Body->GetOverlayMaterial() != RimMaterial)
		{
			Body->SetOverlayMaterial(RimMaterial);
		}
		return;
	}
	if (FlashMaterial)
	{
		FlashMaterial->SetScalarParameterValue(TEXT("Intensity"), 2.2f * HitFlash);
		UMaterialInterface* Want = HitFlash > 0.0f ? FlashMaterial.Get() : nullptr;
		if (Body->GetOverlayMaterial() != Want)
		{
			Body->SetOverlayMaterial(Want);
		}
	}
}

// ---------------------------------------------------------------------------------------------------- Boss bar

void AChaosImpactSimaeBoss::PaintHud(const UWorld* World, const FGeometry& Geometry, FSlateWindowElementList& Elements, const int32 Layer,
	const float Scale)
{
	using namespace ChaosImpactPaint;
	const AChaosImpactSimaeBoss* Boss = nullptr;
	for (TActorIterator<AChaosImpactSimaeBoss> It(World); It; ++It)
	{
		if (!It->IsDormant() && It->WakeTime >= 0.0f)
		{
			Boss = *It;
			break;
		}
	}
	if (!Boss)
	{
		return;
	}
	const float Time = World->GetTimeSeconds();
	const float In = EaseOut((Time - Boss->WakeTime) / 0.5f);
	const FVector2f Size = Geometry.GetLocalSize();
	const FGeometry Top = MakeAnchor(Geometry, Size.X * 0.5f, 0.0f, Scale);
	// Only its name and the health it has left.
	const FGeometry Plate = MakeSkewed(Top, -390.0f, 16.0f - (1.0f - In) * 80.0f, 780.0f, 70.0f, -0.22f);
	const FPainter P{Plate, Elements, Layer, In * (Boss->IsEliminated() ? 0.5f : 1.0f)};
	P.Box(6.0f, 6.0f, 780.0f, 70.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.45f));
	P.Box(0.0f, 0.0f, 780.0f, 70.0f, FLinearColor(0.012f, 0.016f, 0.03f, 0.9f));
	P.Text(TEXT("シマエナガ"), 22.0f, 4.0f, 22.0f, Paper, ETextAlign::Left, TEXT("Black"), 2.0f, Ink);
	// The health just lost shows a moment in white before it drains away.
	const float Max = FMath::Max(Boss->GetMaxHealth(), 1.0f);
	const float Ratio = FMath::Clamp(Boss->GetHealth() / Max, 0.0f, 1.0f);
	const float Trail = FMath::Clamp(FMath::Max(Boss->ShownHealth, Boss->GetHealth()) / Max, 0.0f, 1.0f);
	constexpr float BarX = 22.0f;
	constexpr float BarY = 40.0f;
	constexpr float BarW = 736.0f;
	constexpr float BarH = 18.0f;
	P.Box(BarX - 3.0f, BarY - 3.0f, BarW + 6.0f, BarH + 6.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.8f));
	P.Box(BarX, BarY, BarW, BarH, FLinearColor(0.06f, 0.07f, 0.1f, 1.0f));
	P.Box(BarX, BarY, BarW * Trail, BarH, FLinearColor(1.0f, 0.95f, 0.85f, 0.9f));
	const bool bFull = Boss->IsFullPower();
	FLinearColor FillTop = bFull ? FLinearColor(1.0f, 0.5f, 0.6f) : FLinearColor(0.85f, 0.97f, 1.0f);
	FLinearColor FillBottom = bFull ? FLinearColor(0.8f, 0.02f, 0.08f) : FLinearColor(0.15f, 0.5f, 0.95f);
	if (Boss->GetMove() == EChaosImpactSimaeMove::PowerUp)
	{
		// Powering up: the bar throbs red, faster and redder as the change comes.
		const float Throb = 0.5f + 0.5f * FMath::Sin(Time * 12.0f);
		FillTop = FMath::Lerp(FillTop, FLinearColor(1.0f, 0.45f, 0.55f), 0.4f + 0.6f * Throb);
		FillBottom = FMath::Lerp(FillBottom, FLinearColor(0.9f, 0.02f, 0.08f), 0.4f + 0.6f * Throb);
	}
	P.Box(BarX, BarY, BarW * Ratio, BarH * 0.5f, FillTop);
	P.Box(BarX, BarY + BarH * 0.5f, BarW * Ratio, BarH * 0.5f, FillBottom);
	if (bFull)
	{
		// Full power: flames licking along the top of what is left.
		for (int32 Index = 0; Index < 28; ++Index)
		{
			const float X = BarX + (Index + 0.5f) / 28.0f * BarW;
			if (X > BarX + BarW * Ratio)
			{
				break;
			}
			const float Lick = 3.0f + 8.0f * (0.5f + 0.5f * FMath::Sin(Time * 11.0f + Index * 2.3f))
				* (0.7f + 0.3f * FMath::Sin(Time * 4.3f + Index * 0.9f));
			P.Box(X - 5.0f, BarY - Lick, 10.0f, Lick, WithAlpha(FLinearColor(1.0f, 0.18f, 0.3f), 0.75f));
			P.Box(X - 2.0f, BarY - Lick * 0.55f, 4.0f, Lick * 0.55f, WithAlpha(FLinearColor(1.0f, 0.75f, 0.85f), 0.8f));
		}
	}
}
