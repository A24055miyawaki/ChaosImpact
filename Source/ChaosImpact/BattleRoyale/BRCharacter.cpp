#include "BRGame.h"
#include "BRIsland.h"
#include "ChaosImpactCharacterRoster.h"
#include "ChaosImpactChargeWidget.h"
#include "ChaosImpactHazardZone.h"
#include "Components/CapsuleComponent.h"
#include "ChaosImpact.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Engine/DamageEvents.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"

ABRBall::ABRBall()
{
}

void ABRBall::BeginPlay()
{
	Super::BeginPlay();
	// Thrown balls stay where they land, for anyone to pick up (and throw back).
	SetLandedPickupLifetime(150.0f, 6.0f);
}

ABRCharacter::ABRCharacter()
{
	MaxHealth = BRRules::MaxHealth;
	Health = BRRules::MaxHealth;
	BallClass = ABRBall::StaticClass();
}

void ABRCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABRCharacter, Armor);
	DOREPLIFETIME(ABRCharacter, ArmorMax);
	DOREPLIFETIME(ABRCharacter, Heals);
	DOREPLIFETIME(ABRCharacter, Fighter);
	DOREPLIFETIME(ABRCharacter, TeamId);
	DOREPLIFETIME(ABRCharacter, KillCount);
	DOREPLIFETIME(ABRCharacter, FighterName);
	DOREPLIFETIME(ABRCharacter, bDowned);
	DOREPLIFETIME(ABRCharacter, DownedHealth);
	DOREPLIFETIME(ABRCharacter, BleedEndsAt);
	DOREPLIFETIME(ABRCharacter, ReviveEndsAt);
	DOREPLIFETIME(ABRCharacter, HealEndsAt);
	DOREPLIFETIME(ABRCharacter, bAboard);
	DOREPLIFETIME(ABRCharacter, bFreefall);
	DOREPLIFETIME(ABRCharacter, Vehicle);
	DOREPLIFETIME(ABRCharacter, Seat);
}

void ABRCharacter::BeginPlay()
{
	Super::BeginPlay();
	// A big island: no "left the stage" knockouts.
	SetExplorationMode(true, 1.0f);
	UpdateFlightVisuals();
}

void ABRCharacter::InitFighter(const int32 InFighter, const int32 InTeam, const FString& InName)
{
	Fighter = InFighter;
	TeamId = InTeam;
	SoloTeam = InTeam;
	FighterName = InName;
	ForceNetUpdate();
}

void ABRCharacter::BoardShip()
{
	bAboard = true;
	bFreefall = false;
	GetCharacterMovement()->SetMovementMode(MOVE_None);
	SetActorEnableCollision(false);
	UpdateFlightVisuals();
	ForceNetUpdate();
}

void ABRCharacter::DoJumpOff()
{
	const ABRGameState* State = GetWorld()->GetGameState<ABRGameState>();
	if (!bAboard || !State || !State->CanDrop()) { return; }
	if (HasAuthority()) { JumpOff(); return; }
	// Out of the door at once on this screen; the host hears of it a moment later.
	bAboard = false;
	bFreefall = true;
	SetActorEnableCollision(true);
	UpdateFlightVisuals();
	UE_LOG(LogChaosImpact, Log, TEXT("BRTEST jumped on the guest's own screen"));
	ServerJumpOff();
}

void ABRCharacter::ServerJumpOff_Implementation() { JumpOff(); }

void ABRCharacter::JumpOff()
{
	if (!HasAuthority() || !bAboard) { return; }
	bAboard = false;
	bFreefall = true;
	SetActorEnableCollision(true);
	UpdateFlightVisuals();
	ForceNetUpdate();
}

void ABRCharacter::Land()
{
	bFreefall = false;
	UpdateFlightVisuals();
	UCharacterMovementComponent* Moves = GetCharacterMovement();
	Moves->Velocity = FVector::ZeroVector;
	Moves->SetMovementMode(MOVE_Falling);
	if (HasAuthority()) { ForceNetUpdate(); }
}

void ABRCharacter::OnRep_Flight() { UpdateFlightVisuals(); }

void ABRCharacter::UpdateFlightVisuals()
{
	if (GetMesh())
	{
		GetMesh()->SetVisibility(!bAboard, true);
		// (That reached the balls in its hands too: they show only what is really held.)
		UpdateBallPresentation();
	}
	UpdateDownVisuals();
	// A whole team leaves the door at once: in the air they pass through each other.
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, bFreefall ? ECR_Ignore : GetClass()->GetDefaultObject<ACharacter>()->GetCapsuleComponent()->GetCollisionResponseToChannel(ECC_Pawn));
	UCharacterMovementComponent* Moves = GetCharacterMovement();
	if (!Moves || IsEliminated()) { return; }
	if (bAboard) { Moves->SetMovementMode(MOVE_None); }
	else if (bFreefall)
	{
		// Flying mode: the stick moves it across, and the fall is held at its own speed in TickAir.
		Moves->MaxFlySpeed = BRRules::FallAcross;
		Moves->BrakingDecelerationFlying = 1500.0f;
		Moves->MaxAcceleration = 5200.0f;
		Moves->SetMovementMode(MOVE_Flying);
	}
	else if (!bAboard && !Vehicle && !bDowned)
	{
		Moves->MaxAcceleration = GetClass()->GetDefaultObject<ACharacter>()->GetCharacterMovement()->MaxAcceleration;
		if (Moves->MovementMode == MOVE_None || Moves->MovementMode == MOVE_Flying) { Moves->SetMovementMode(MOVE_Falling); }
	}
}

void ABRCharacter::TickAir(const float DeltaSeconds)
{
	const ABRGameState* State = GetWorld()->GetGameState<ABRGameState>();
	if (!State) { return; }
	if (bAboard)
	{
		// Carried along, out of sight, the view from high up.
		SetActorLocation(State->GetShipLocation() - FVector(0.0f, 0.0f, 400.0f), false, nullptr, ETeleportType::TeleportPhysics);
		if (IsLocallyControlled() && GetCameraBoom()) { GetCameraBoom()->TargetArmLength = 7000.0f; }
		return;
	}
	if (IsLocallyControlled() && GetCameraBoom())
	{
		// The view closes in as the ground comes up.
		const float Height = GetActorLocation().Z - BRMap::HeightAt(GetActorLocation().X, GetActorLocation().Y);
		GetCameraBoom()->TargetArmLength = FMath::GetMappedRangeValueClamped(FVector2f(800.0f, 12000.0f), FVector2f(1300.0f, 4600.0f), Height);
	}
	UCharacterMovementComponent* Moves = GetCharacterMovement();
	const FVector Here = GetActorLocation();
	const float Ground = BRMap::HeightAt(Here.X, Here.Y);
	const bool bOverSea = Ground < BRRules::ShoreHeight;
	if (IsLocallyControlled() && Moves)
	{
		if (Moves->MovementMode != MOVE_Flying) { Moves->SetMovementMode(MOVE_Flying); }
		const bool bDiving = !GetLastMovementInputVector().IsNearlyZero();
		Moves->Velocity.Z = -(bDiving ? BRRules::FallDive : BRRules::FallDown);
		if (bOverSea)
		{
			// The wind carries anyone over the sea in to the island, and nobody comes down in the water.
			const FVector Home = (-Here).GetSafeNormal2D() * BRRules::FallAcross;
			Moves->Velocity.X = Home.X;
			Moves->Velocity.Y = Home.Y;
			if (Here.Z < 1500.0f) { Moves->Velocity.Z = 0.0f; }
		}
		if (!GetLastMovementInputVector().IsNearlyZero())
		{
			SetActorRotation(FRotator(0.0f, GetLastMovementInputVector().Rotation().Yaw, 0.0f));
		}
	}
	FHitResult Hit;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(BRFall), false, this);
	// (By object type: someone falling just below is not the ground.)
	if (!bOverSea && (Here.Z - Ground < 240.0f || GetWorld()->LineTraceSingleByObjectType(Hit, Here, Here - FVector(0.0f, 0.0f, 260.0f), FCollisionObjectQueryParams(ECC_WorldStatic), Query)))
	{
		Land();
	}
}

bool ABRCharacter::IsMatchInputLocked() const
{
	const ABRGameState* State = GetWorld() ? GetWorld()->GetGameState<ABRGameState>() : nullptr;
	return bAboard || bDowned || (State && (State->Phase == EBRPhase::Title || State->Phase == EBRPhase::Over)) || Super::IsMatchInputLocked();
}

bool ABRCharacter::IsMoveInputIgnored() const
{
	return bAboard || bDowned || Super::IsMoveInputIgnored();
}

void ABRCharacter::FellOutOfWorld(const UDamageType& DamageType)
{
	// Never fatal: if something drops through, put them back on the ground.
	if (HasAuthority())
	{
		const FVector Where(GetActorLocation().X, GetActorLocation().Y, BRMap::HeightAt(GetActorLocation().X, GetActorLocation().Y) + 180.0f);
		SetActorLocation(Where, false, nullptr, ETeleportType::TeleportPhysics);
		GetCharacterMovement()->StopMovementImmediately();
	}
}

float ABRCharacter::TakeDamage(const float Amount, const FDamageEvent& Event, AController* EventInstigator, AActor* Causer)
{
	if (!HasAuthority() || IsEliminated() || IsFlying() || Amount <= 0.0f) { return 0.0f; }
	const bool bZone = Cast<ABRGameMode>(Causer) != nullptr;
	float Damage = Amount;
	if (!bZone)
	{
		// (A hit the victim reported came from their own screen, where they were not dashing: this copy of them runs late.)
		if (IsDashing() && !bApplyingReportedHit) { return 0.0f; }
		const AChaosImpactBall* Ball = Cast<AChaosImpactBall>(Causer);
		const AChaosImpactHazardZone* Hazard = Cast<AChaosImpactHazardZone>(Causer);
		APawn* Source = EventInstigator ? EventInstigator->GetPawn() : nullptr;
		if (!Source && Ball) { Source = Ball->GetThrowingPawn(); }
		if (!Source && Hazard) { Source = Hazard->GetSourcePawn(); }
		if (Source == this) { return 0.0f; }
		if (const ABRCharacter* Attacker = Cast<ABRCharacter>(Source); Attacker && Attacker->TeamId == TeamId) { return 0.0f; }
		if (Hazard)
		{
			Damage = Hazard->GetZoneType() == EChaosImpactBallType::Nova ? 95.0f : 7.0f;
		}
		else if (Ball)
		{
			// A harder throw lands harder; the kinds trade power for what else they do.
			float Base = 26.0f;
			switch (Ball->GetBallType())
			{
			case EChaosImpactBallType::Fire: Base = 22.0f; break;
			case EChaosImpactBallType::Ice: Base = 18.0f; break;
			case EChaosImpactBallType::Thunder: Base = 30.0f; break;
			case EChaosImpactBallType::Black: Base = 16.0f; break;
			case EChaosImpactBallType::Wind: Base = 16.0f; break;
			case EChaosImpactBallType::Smoke: Base = 12.0f; break;
			case EChaosImpactBallType::Beam: Base = 38.0f; break;
			case EChaosImpactBallType::Snow: Base = 30.0f; break;
			case EChaosImpactBallType::Nova: Base = 95.0f; break;
			default: break;
			}
			const float Charge = FMath::Clamp(static_cast<float>(Ball->GetVelocity().Size()) / 2600.0f, 0.5f, 1.25f);
			Damage = Base * (0.6f + 0.6f * Charge);
		}
		else
		{
			Damage = Amount * 20.0f;
		}
		// Armour takes it first.
		const float Absorbed = FMath::Min(Armor, Damage);
		Armor -= Absorbed;
		Damage -= Absorbed;
		DamagedAt = GetWorld()->GetTimeSeconds();
		HealEndsAt = 0.0;
		ForceNetUpdate();
		if (Damage <= 0.0f)
		{
			// All on the armour: the hit still shows, nothing else.
			return Absorbed;
		}
	}
	// Downed: only the bleeding pool is left.
	if (bDowned)
	{
		DownedHealth -= Damage;
		if (DownedHealth <= 0.0f) { FinishOff(); }
		ForceNetUpdate();
		return Damage;
	}
	if (Damage >= Health)
	{
		// With a teammate still up, it's a down rather than the end.
		bool bTeamUp = false;
		const ABRGameMode* Mode = GetWorld()->GetAuthGameMode<ABRGameMode>();
		if (Mode)
		{
			for (const FBRFighter& Other : Mode->GetFighters())
			{
				if (Other.Team == TeamId && Other.bAlive && Other.Real.IsValid() && Other.Real.Get() != this && !Other.Real->IsDowned()) { bTeamUp = true; break; }
			}
		}
		if (bTeamUp) { GoDown(); return Damage; }
	}
	return Super::TakeDamage(Damage, Event, EventInstigator, Causer);
}

void ABRCharacter::GoDown()
{
	bDowned = true;
	DownedHealth = BRRules::DownedHealth;
	Health = 1.0f;
	CancelChargingThrow();
	const AGameStateBase* State = GetWorld()->GetGameState();
	BleedEndsAt = State->GetServerWorldTimeSeconds() + BRRules::BleedSeconds;
	ReviveEndsAt = 0.0;
	LoseCarriedBalls();
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->SetMovementMode(MOVE_None);
	if (Vehicle) { Vehicle->Dismount(this); }
	UpdateDownVisuals();
	ForceNetUpdate();
}

void ABRCharacter::Revived()
{
	bDowned = false;
	ReviveEndsAt = 0.0;
	Health = 35.0f;
	GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	UpdateDownVisuals();
	ForceNetUpdate();
}

void ABRCharacter::FinishOff()
{
	bDowned = false;
	UpdateDownVisuals();
	Super::TakeDamage(1000.0f, FDamageEvent(), nullptr, GetWorld()->GetAuthGameMode());
}

void ABRCharacter::OnRep_Downed() { UpdateDownVisuals(); }

void ABRCharacter::UpdateDownVisuals()
{
	if (!GetMesh()) { return; }
	if (!bBaseMeshRotationSet)
	{
		bBaseMeshRotationSet = true;
		BaseMeshRotation = GetMesh()->GetRelativeRotation();
	}
	// Flat on the ground, face down.
	GetMesh()->SetRelativeRotation(bDowned ? BaseMeshRotation + FRotator(0.0f, 0.0f, 82.0f) : bFreefall ? BaseMeshRotation + FRotator(0.0f, 0.0f, -74.0f) : BaseMeshRotation);
}

float ABRCharacter::GetBleedLeft() const
{
	const AGameStateBase* State = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	return bDowned && State ? FMath::Max(0.0f, static_cast<float>(BleedEndsAt - State->GetServerWorldTimeSeconds())) : 0.0f;
}

void ABRCharacter::AddHealth(const float Amount)
{
	Health = FMath::Clamp(Health + Amount, 0.0f, MaxHealth);
	ForceNetUpdate();
}

void ABRCharacter::SetHealthDirect(const float Value)
{
	Health = FMath::Clamp(Value, 1.0f, MaxHealth);
}

void ABRCharacter::AddArmor(const float Amount)
{
	Armor = FMath::Clamp(Armor + Amount, 0.0f, ArmorMax);
	ForceNetUpdate();
}

bool ABRCharacter::TakeLoot(const EBRLootKind Kind)
{
	switch (Kind)
	{
	case EBRLootKind::Heal:
		if (Heals >= BRRules::MaxHeals) { return false; }
		++Heals;
		return true;
	case EBRLootKind::Plate:
		if (ArmorMax <= 0.0f) { ArmorMax = 25.0f; }
		if (Armor >= ArmorMax) { return false; }
		AddArmor(BRRules::PlateArmor);
		return true;
	case EBRLootKind::Vest:
		if (ArmorMax >= 100.0f) { return false; }
		ArmorMax = FMath::Min(ArmorMax + 25.0f, 100.0f);
		AddArmor(25.0f);
		return true;
	default:
		return false;
	}
}

void ABRCharacter::GiveBall(const EChaosImpactBallType Type)
{
	if (!HasAuthority() || GetCarriedBallCount() >= GetMaximumCarriedBalls()) { return; }
	const FTransform Where(GetActorLocation());
	ABRBall* Ball = GetWorld()->SpawnActorDeferred<ABRBall>(ABRBall::StaticClass(), Where, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Ball) { return; }
	Ball->SetBallType(Type);
	Ball->FinishSpawning(Where);
	Ball->MakePickup();
	if (!TryPickupBall(Ball)) { Ball->Destroy(); }
}

void ABRCharacter::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// This mode draws all of its own HUD (BRHUD): the VS one is never shown over it.
	if (ChargeWidget)
	{
		ChargeWidget->SetReplacedByOwnHud(true);
		if (ChargeWidget->GetVisibility() != ESlateVisibility::Collapsed) { ChargeWidget->SetVisibility(ESlateVisibility::Collapsed); }
	}
	// (Teams come over the network here; the balls' own checks read them from this.)
	SoloTeam = TeamId;
	if (IsFlying()) { TickAir(DeltaSeconds); }
	TickVehicleInput();
	if (Vehicle)
	{
		// Riders stay in their seats, whatever position their own machine last reported.
		SetActorRelativeLocation(Vehicle->GetSeatOffset(Seat));
		SetActorRelativeRotation(FRotator::ZeroRotator);
	}
	static const bool bDevDrive = FParse::Param(FCommandLine::Get(), TEXT("CIBRDevDrive"));
	if (bDevDrive && bAboard && IsLocallyControlled()) { DoJumpOff(); }
	if (!HasAuthority()) { return; }
	const AGameStateBase* State = GetWorld()->GetGameState();
	const double Now = State->GetServerWorldTimeSeconds();
	if (bDowned)
	{
		if (Now >= BleedEndsAt) { FinishOff(); return; }
		// Someone tending them finishes in time.
		if (ReviveEndsAt > 0.0)
		{
			const ABRCharacter* Helper = ReviveTarget.Get();
			if (!Helper || Helper->IsEliminated() || Helper->IsDowned() || FVector::DistSquared2D(Helper->GetActorLocation(), GetActorLocation()) > FMath::Square(360.0f)) { ReviveEndsAt = 0.0; ForceNetUpdate(); }
			else if (Now >= ReviveEndsAt) { Revived(); }
		}
	}
	if (HealEndsAt > 0.0 && Now >= HealEndsAt)
	{
		HealEndsAt = 0.0;
		if (Heals > 0)
		{
			--Heals;
			AddHealth(BRRules::HealAmount);
		}
	}
}

void ABRCharacter::DoInteract()
{
	if (HasAuthority()) { InteractNow(); } else { ServerInteract(); }
}

void ABRCharacter::ServerInteract_Implementation() { InteractNow(); }

void ABRCharacter::DoUseHeal()
{
	if (HasAuthority()) { ServerUseHeal_Implementation(); } else { ServerUseHeal(); }
}

void ABRCharacter::ServerUseHeal_Implementation()
{
	const AGameStateBase* State = GetWorld()->GetGameState();
	if (Heals <= 0 || bDowned || IsEliminated() || IsFlying() || Health >= MaxHealth || HealEndsAt > 0.0) { return; }
	HealEndsAt = State->GetServerWorldTimeSeconds() + BRRules::HealSeconds;
	ForceNetUpdate();
}

bool ABRCharacter::CanRevive(const ABRCharacter* Other) const
{
	return Other && Other != this && Other->TeamId == TeamId && Other->bDowned && !IsEliminated() && !bDowned && !IsFlying()
		&& FVector::DistSquared2D(Other->GetActorLocation(), GetActorLocation()) < FMath::Square(320.0f);
}

ABRCharacter* ABRCharacter::FindReviveTarget() const
{
	for (TActorIterator<ABRCharacter> It(GetWorld()); It; ++It)
	{
		if (CanRevive(*It)) { return *It; }
	}
	return nullptr;
}

ABRVehicle* ABRCharacter::FindNearVehicle() const
{
	ABRVehicle* Best = nullptr;
	double BestD = FMath::Square(420.0);
	for (TActorIterator<ABRVehicle> It(GetWorld()); It; ++It)
	{
		const double D = FVector::DistSquared2D(It->GetActorLocation(), GetActorLocation());
		if (D < BestD && It->Riders.Num() < It->GetSeats()) { BestD = D; Best = *It; }
	}
	return Best;
}

int32 ABRCharacter::GetInteractKind() const
{
	if (IsFlying() || bDowned || IsEliminated()) { return 0; }
	if (Vehicle) { return 3; }
	if (FindReviveTarget()) { return 1; }
	return FindNearVehicle() ? 2 : 0;
}

void ABRCharacter::InteractNow()
{
	if (bDowned || IsEliminated() || IsFlying()) { return; }
	if (Vehicle) { Vehicle->Dismount(this); return; }
	if (ABRCharacter* Down = FindReviveTarget())
	{
		const AGameStateBase* State = GetWorld()->GetGameState();
		Down->ReviveEndsAt = State->GetServerWorldTimeSeconds() + BRRules::ReviveSeconds;
		Down->ReviveTarget = this;
		Down->ForceNetUpdate();
		return;
	}
	if (ABRVehicle* Near = FindNearVehicle()) { Near->Mount(this); }
}

void ABRCharacter::OnRep_Vehicle()
{
	// (The host attached it; here it just must not walk or bump while riding.)
	GetCapsuleComponent()->SetCollisionEnabled(Vehicle ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
	GetCharacterMovement()->SetMovementMode(Vehicle ? MOVE_None : MOVE_Falling);
}

void ABRCharacter::TickVehicleInput()
{
	if (!Vehicle || !IsLocallyControlled() || !Vehicle->IsDriver(this) || Vehicle->bScriptedDriver) { return; }
	// Up on the stick is the way the camera looks, so a person drives where they push.
	static const bool bDevDrive = FParse::Param(FCommandLine::Get(), TEXT("CIBRDevDrive"));
	const FVector Wanted = bDevDrive ? GetActorForwardVector() : GetLastMovementInputVector();
	Vehicle->SetDriverInput(FVector2D(Wanted.X, Wanted.Y));
}

void ABRCharacter::ServerVehicleState_Implementation(const FVector_NetQuantize At, const float InYaw, const float InPitch, const float InSpeed)
{
	if (Vehicle && Vehicle->IsDriver(this)) { Vehicle->ApplyDriverState(At, InYaw, InPitch, InSpeed); }
}

bool ABRCharacter::WouldTakeLoot(const EBRLootKind Kind) const
{
	switch (Kind)
	{
	case EBRLootKind::Heal: return Heals < BRRules::MaxHeals;
	case EBRLootKind::Plate: return Armor < (ArmorMax <= 0.0f ? 25.0f : ArmorMax);
	case EBRLootKind::Vest: return ArmorMax < 100.0f;
	default: return false;
	}
}

// ---- Loot lying about ----

ABRLoot::ABRLoot()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(false);
	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	SetRootComponent(Body);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Ring = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Ring"));
	Ring->SetupAttachment(Body);
	Ring->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Ring->SetCastShadow(false);
}

void ABRLoot::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABRLoot, Kind);
}

void ABRLoot::Setup(const EBRLootKind InKind)
{
	Kind = InKind;
	Base = GetActorLocation();
	ApplyLook();
}

void ABRLoot::OnRep_Kind()
{
	Base = GetActorLocation();
	ApplyLook();
}

void ABRLoot::ApplyLook()
{
	const TCHAR* Shape = Kind == EBRLootKind::Heal ? TEXT("/Engine/BasicShapes/Cylinder.Cylinder") : TEXT("/Engine/BasicShapes/Cube.Cube");
	Body->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, Shape));
	Ring->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")));
	const FLinearColor Tint = Kind == EBRLootKind::Heal ? FLinearColor(0.2f, 1.0f, 0.4f) : Kind == EBRLootKind::Plate ? FLinearColor(0.2f, 0.65f, 1.0f) : FLinearColor(0.75f, 0.4f, 1.0f);
	if (UMaterialInterface* Glow = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/ChaosImpact/BattleRoyale/Materials/M_BR_LootGlow.M_BR_LootGlow")))
	{
		UMaterialInstanceDynamic* BodyMat = Body->CreateDynamicMaterialInstance(0, Glow);
		UMaterialInstanceDynamic* RingMat = Ring->CreateDynamicMaterialInstance(0, Glow);
		if (BodyMat) { BodyMat->SetVectorParameterValue(TEXT("Colour"), Tint * 2.2f); }
		if (RingMat) { RingMat->SetVectorParameterValue(TEXT("Colour"), Tint * 1.2f); }
	}
	Body->SetRelativeScale3D(Kind == EBRLootKind::Heal ? FVector(0.34f, 0.34f, 0.6f) : Kind == EBRLootKind::Plate ? FVector(0.55f, 0.42f, 0.1f) : FVector(0.6f, 0.46f, 0.7f));
	Ring->SetRelativeScale3D(FVector(1.6f, 2.1f, 0.05f) / Body->GetRelativeScale3D());
	Ring->SetRelativeLocation(FVector(0.0f, 0.0f, -60.0f) / Body->GetRelativeScale3D().Z);
}

void ABRLoot::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Clock += DeltaSeconds;
	SetActorLocationAndRotation(Base + FVector(0.0f, 0.0f, FMath::Sin(Clock * 2.2f) * 12.0f), FRotator(0.0f, Clock * 70.0f, 0.0f));
	if (!HasAuthority())
	{
		// Walked over: gone at once on that person's own screen. (Should the host disagree, it comes back.)
		const APlayerController* Local = GetWorld()->GetFirstPlayerController();
		const ABRCharacter* Me = Local ? Cast<ABRCharacter>(Local->GetPawn()) : nullptr;
		const double Now = GetWorld()->GetTimeSeconds();
		if (Me && !Me->IsEliminated() && !Me->IsDowned() && !Me->IsFlying() && FVector::DistSquared2D(Me->GetActorLocation(), Base) <= FMath::Square(150.0f) && Me->WouldTakeLoot(Kind))
		{
			SetActorHiddenInGame(true);
			PredictedUntil = Now + 1.5;
		}
		else if (IsHidden() && Now >= PredictedUntil)
		{
			SetActorHiddenInGame(false);
		}
		return;
	}
	// Walking over it takes it.
	for (TActorIterator<ABRCharacter> It(GetWorld()); It; ++It)
	{
		ABRCharacter* Who = *It;
		if (Who->IsEliminated() || Who->IsDowned() || Who->IsFlying() || FVector::DistSquared2D(Who->GetActorLocation(), Base) > FMath::Square(150.0f)) { continue; }
		if (Who->TakeLoot(Kind))
		{
			if (ABRGameMode* Mode = GetWorld()->GetAuthGameMode<ABRGameMode>()) { Mode->NotifyLootTaken(Spot); }
			Destroy();
			return;
		}
	}
}

// ---- The electric skateboard and the car ----

ABRVehicle::ABRVehicle()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(true);
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	SurfaceMaterial = nullptr;
}

void ABRVehicle::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABRVehicle, Kind);
	DOREPLIFETIME(ABRVehicle, Paint);
	DOREPLIFETIME(ABRVehicle, Riders);
	DOREPLIFETIME(ABRVehicle, Speed);
}

void ABRVehicle::Setup(const EBRVehicleKind InKind, const FLinearColor& InPaint)
{
	Kind = InKind;
	Paint = InPaint;
	Yaw = GetActorRotation().Yaw;
	BuildLook();
}

void ABRVehicle::OnRep_Look() { BuildLook(); }

void ABRVehicle::AddPart(const TCHAR* Shape, const FVector& At, const FVector& Size, const FRotator& Rotation, const FLinearColor& Colour, const bool bGlow)
{
	UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this);
	Part->SetupAttachment(Root);
	Part->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), Shape, Shape)));
	Part->SetRelativeLocationAndRotation(At, Rotation);
	Part->SetRelativeScale3D(Size / 100.0f);
	Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Part->SetCastShadow(!bGlow);
	Part->RegisterComponent();
	UMaterialInstanceDynamic* Material = Part->CreateDynamicMaterialInstance(0, bGlow ? GlowMaterial : SurfaceMaterial);
	if (Material) { Material->SetVectorParameterValue(TEXT("Colour"), Colour); }
	Parts.Add(Part);
}

void ABRVehicle::BuildLook()
{
	if (bBuilt) { return; }
	bBuilt = true;
	SurfaceMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/ChaosImpact/BattleRoyale/Materials/M_BR_VehiclePaint.M_BR_VehiclePaint"));
	GlowMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/ChaosImpact/BattleRoyale/Materials/M_BR_LootGlow.M_BR_LootGlow"));
	const FLinearColor Dark(0.03f, 0.03f, 0.04f);
	if (Kind == EBRVehicleKind::Skateboard)
	{
		// A long hover-bright deck with glowing wheels and a light strip along it: room for two.
		AddPart(TEXT("Cube"), FVector(0.0f, 0.0f, 20.0f), FVector(240.0f, 62.0f, 8.0f), FRotator::ZeroRotator, Paint, false);
		AddPart(TEXT("Cube"), FVector(0.0f, 0.0f, 26.0f), FVector(226.0f, 52.0f, 2.0f), FRotator::ZeroRotator, Dark, false);
		AddPart(TEXT("Cube"), FVector(0.0f, 0.0f, 28.0f), FVector(216.0f, 6.0f, 2.0f), FRotator::ZeroRotator, Paint * 3.0f, true);
		for (const float X : {-84.0f, 84.0f})
		{
			AddPart(TEXT("Cube"), FVector(X, 0.0f, 12.0f), FVector(26.0f, 74.0f, 6.0f), FRotator::ZeroRotator, FLinearColor(0.2f, 0.2f, 0.24f), false);
			for (const float Y : {-34.0f, 34.0f})
			{
				AddPart(TEXT("Sphere"), FVector(X, Y, 10.0f), FVector(22.0f), FRotator::ZeroRotator, FLinearColor(0.1f, 0.9f, 1.0f) * 4.0f, true);
			}
		}
		// The tail and nose kick up a little.
		AddPart(TEXT("Cube"), FVector(130.0f, 0.0f, 28.0f), FVector(40.0f, 58.0f, 8.0f), FRotator(16.0f, 0.0f, 0.0f), Paint, false);
		AddPart(TEXT("Cube"), FVector(-130.0f, 0.0f, 28.0f), FVector(40.0f, 58.0f, 8.0f), FRotator(-16.0f, 0.0f, 0.0f), Paint, false);
		return;
	}
	AddPart(TEXT("Cube"), FVector(0.0f, 0.0f, 62.0f), FVector(450.0f, 200.0f, 72.0f), FRotator::ZeroRotator, Paint, false);
	AddPart(TEXT("Cube"), FVector(-30.0f, 0.0f, 128.0f), FVector(240.0f, 176.0f, 62.0f), FRotator::ZeroRotator, FLinearColor(0.08f, 0.12f, 0.18f), false);
	AddPart(TEXT("Cube"), FVector(-30.0f, 0.0f, 162.0f), FVector(222.0f, 168.0f, 8.0f), FRotator::ZeroRotator, Paint, false);
	for (const float X : {-150.0f, 150.0f})
	{
		for (const float Y : {-96.0f, 96.0f})
		{
			AddPart(TEXT("Cylinder"), FVector(X, Y, 36.0f), FVector(74.0f, 74.0f, 26.0f), FRotator(0.0f, 0.0f, 90.0f), Dark, false);
		}
	}
	for (const float Y : {-66.0f, 66.0f})
	{
		AddPart(TEXT("Cube"), FVector(226.0f, Y, 70.0f), FVector(4.0f, 38.0f, 18.0f), FRotator::ZeroRotator, FLinearColor(1.0f, 0.92f, 0.7f) * 4.0f, true);
		AddPart(TEXT("Cube"), FVector(-226.0f, Y, 70.0f), FVector(4.0f, 38.0f, 18.0f), FRotator::ZeroRotator, FLinearColor(1.0f, 0.1f, 0.1f) * 3.0f, true);
	}
}

FVector ABRVehicle::GetSeatOffset(const int32 Index) const
{
	if (Kind == EBRVehicleKind::Skateboard)
	{
		return FVector(Index == 0 ? 48.0f : -56.0f, 0.0f, 118.0f);
	}
	const FVector Seats[] = {FVector(38.0f, -50.0f, 110.0f), FVector(38.0f, 50.0f, 110.0f), FVector(-86.0f, -50.0f, 110.0f), FVector(-86.0f, 50.0f, 110.0f)};
	return Seats[FMath::Clamp(Index, 0, 3)];
}

bool ABRVehicle::Mount(ABRCharacter* Rider)
{
	if (!HasAuthority() || !Rider || Riders.Contains(Rider) || Riders.Num() >= GetSeats()) { return false; }
	const int32 Seat = Riders.Num();
	Riders.Add(Rider);
	Rider->Vehicle = this;
	Rider->Seat = Seat;
	Rider->GetCharacterMovement()->StopMovementImmediately();
	Rider->GetCharacterMovement()->SetMovementMode(MOVE_None);
	Rider->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Rider->AttachToActor(this, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	Rider->SetActorRelativeLocation(GetSeatOffset(Seat));
	Rider->SetActorRelativeRotation(FRotator::ZeroRotator);
	Rider->ForceNetUpdate();
	ForceNetUpdate();
	return true;
}

void ABRVehicle::Dismount(ABRCharacter* Rider)
{
	if (!HasAuthority() || !Rider || !Riders.Contains(Rider)) { return; }
	Riders.Remove(Rider);
	Rider->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	Rider->Vehicle = nullptr;
	Rider->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	const FVector Side = GetActorRightVector() * (Rider->Seat % 2 ? 220.0f : -220.0f);
	const FVector Off = GetActorLocation() + Side + FVector(0.0f, 0.0f, 130.0f);
	Rider->SetActorLocationAndRotation(Off, FRotator(0.0f, GetActorRotation().Yaw, 0.0f), false, nullptr, ETeleportType::TeleportPhysics);
	Rider->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
	for (int32 Index = 0; Index < Riders.Num(); ++Index)
	{
		Riders[Index]->Seat = Index;
		Riders[Index]->SetActorRelativeLocation(GetSeatOffset(Index));
	}
	Rider->ForceNetUpdate();
	ForceNetUpdate();
}

void ABRVehicle::PostNetReceiveLocationAndRotation()
{
	if (!bSimulatingHere) { Super::PostNetReceiveLocationAndRotation(); }
}

void ABRVehicle::ApplyDriverState(const FVector& At, const float InYaw, const float InPitch, const float InSpeed)
{
	Yaw = InYaw;
	SimSpeed = InSpeed;
	Speed = InSpeed;
	SetActorLocationAndRotation(At, FRotator(InPitch, InYaw, 0.0f));
}

void ABRVehicle::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority())
	{
		// Riders knocked out or gone fall off.
		for (int32 Index = Riders.Num() - 1; Index >= 0; --Index)
		{
			if (!IsValid(Riders[Index]) || Riders[Index]->IsEliminated() || Riders[Index]->IsDowned())
			{
				if (IsValid(Riders[Index])) { Dismount(Riders[Index]); }
				else { Riders.RemoveAt(Index); }
			}
		}
	}
	// Whoever is at the wheel moves it on their own screen; everyone else is told where it went.
	ABRCharacter* Driver = Riders.Num() > 0 ? Riders[0].Get() : nullptr;
	const bool bDriverIsGuest = Driver && Driver->IsPlayerControlled() && !Driver->IsLocallyControlled();
	const bool bSimulate = HasAuthority() ? !bDriverIsGuest : (Driver && Driver->IsLocallyControlled());
	if (!bSimulate)
	{
		bSimulatingHere = false;
		return;
	}
	if (!HasAuthority() && !bSimulatingHere)
	{
		bSimulatingHere = true;
		Yaw = GetActorRotation().Yaw;
		SimSpeed = Speed;
		DevStartedAt = FPlatformTime::Seconds();
		DevLast = GetActorLocation();
	}
	if (HasAuthority() && !bDriverIsGuest && Driver == nullptr) { Yaw = GetActorRotation().Yaw; }
	static const bool bDevDrive = FParse::Param(FCommandLine::Get(), TEXT("CIBRDevDrive"));
	if (bDevDrive && bSimulatingHere && FVector::Dist2D(GetActorLocation(), DevLast) > 1.0) { ++DevCorrections; }
	const bool bBoard = Kind == EBRVehicleKind::Skateboard;
	const float TopSpeed = bBoard ? 1500.0f : 2400.0f;
	const float Throttle = Riders.Num() > 0 ? FMath::Clamp(static_cast<float>(DriverInput.Size()), 0.0f, 1.0f) : 0.0f;
	const FVector2D Wanted = DriverInput.GetSafeNormal();
	if (Throttle > 0.05f)
	{
		// It turns toward the way being pushed, more slowly the faster it goes.
		const float WantYaw = FMath::RadiansToDegrees(FMath::Atan2(Wanted.Y, Wanted.X));
		const float TurnRate = (bBoard ? 260.0f : 150.0f) * FMath::GetMappedRangeValueClamped(FVector2f(0.0f, TopSpeed), FVector2f(1.3f, 0.6f), FMath::Abs(SimSpeed));
		Yaw = FMath::FixedTurn(Yaw, WantYaw, TurnRate * DeltaSeconds);
		SimSpeed = FMath::FInterpTo(SimSpeed, TopSpeed * Throttle, DeltaSeconds, bBoard ? 2.4f : 1.5f);
	}
	else
	{
		SimSpeed = FMath::FInterpTo(SimSpeed, 0.0f, DeltaSeconds, 1.4f);
	}
	if (FMath::Abs(SimSpeed) < 5.0f && Riders.IsEmpty())
	{
		Speed = SimSpeed;
		return;
	}
	const FVector Forward = FRotator(0.0f, Yaw, 0.0f).Vector();
	const FVector Here = GetActorLocation();
	FVector Next = Here + Forward * SimSpeed * DeltaSeconds;
	// Something solid ahead stops it dead; the ground under it sets its height and tilt.
	FCollisionQueryParams Query(SCENE_QUERY_STAT(BRVehicle), false, this);
	for (const ABRCharacter* Rider : Riders) { if (Rider) { Query.AddIgnoredActor(Rider); } }
	FHitResult Wall;
	const float Reach = bBoard ? 150.0f : 260.0f;
	if (GetWorld()->LineTraceSingleByChannel(Wall, Here + FVector(0.0f, 0.0f, 90.0f), Here + FVector(0.0f, 0.0f, 90.0f) + Forward * Reach, ECC_WorldStatic, Query)
		&& Wall.ImpactNormal.Z < 0.5f)
	{
		SimSpeed = -SimSpeed * 0.15f;
		Next = Here;
	}
	const auto GroundAt = [&](const FVector& At)
	{
		FHitResult Down;
		if (GetWorld()->LineTraceSingleByChannel(Down, At + FVector(0.0f, 0.0f, 400.0f), At - FVector(0.0f, 0.0f, 600.0f), ECC_WorldStatic, Query))
		{
			return Down.ImpactPoint.Z;
		}
		return static_cast<double>(BRMap::HeightAt(At.X, At.Y));
	};
	const float Front = GroundAt(Next + Forward * (bBoard ? 100.0f : 180.0f));
	const float Back = GroundAt(Next - Forward * (bBoard ? 100.0f : 180.0f));
	const float Ground = (Front + Back) * 0.5f;
	const float Pitch = FMath::RadiansToDegrees(FMath::Atan2(Front - Back, bBoard ? 200.0f : 360.0f));
	Next.Z = FMath::FInterpTo(Here.Z, Ground, DeltaSeconds, 14.0f);
	// The sea is not for driving on.
	if (BRMap::ShoreDistance(Next.X, Next.Y) < 200.0f) { SimSpeed = -SimSpeed * 0.3f; Next = Here; }
	SetActorLocationAndRotation(Next, FRotator(Pitch, Yaw, 0.0f));
	Speed = SimSpeed;
	if (bSimulatingHere)
	{
		const double Clock2 = FPlatformTime::Seconds();
		if (Clock2 >= NextStateSendAt)
		{
			NextStateSendAt = Clock2 + 1.0 / 30.0;
			Driver->ServerVehicleState(Next, Yaw, Pitch, SimSpeed);
		}
		if (bDevDrive)
		{
			DevLast = Next;
			if (Clock2 >= DevNextLogAt)
			{
				DevNextLogAt = Clock2 + 0.5;
				UE_LOG(LogChaosImpact, Log, TEXT("BRTEST guest drives: t=%.2f speed=%.0f x=%.0f y=%.0f pushed back=%d seat gap=%.0f"), Clock2 - DevStartedAt, SimSpeed, Next.X, Next.Y, DevCorrections,
					FVector::Dist(Driver->GetActorLocation(), Next + GetActorRotation().RotateVector(GetSeatOffset(0))));
			}
		}
	}
}
