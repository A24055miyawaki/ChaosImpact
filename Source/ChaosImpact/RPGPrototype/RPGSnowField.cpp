#include "RPGSnowField.h"
#include "Net/UnrealNetwork.h"
#include "RPGAdventure.h"
#include "RPGQuests.h"
#include "RPGTuning.h"
#include "RPGWorldLayout.h"
#include "ChaosImpactSimaeBoss.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

// The snowfield and the Shima-enaga's nest (2026-10-20). See Docs/RPGPrototype/雪原とシマエナガ.md.

namespace
{
const TCHAR* SnowMaterial = TEXT("/Game/ChaosImpact/solo/Stage08/Materials/MI_Snow.MI_Snow");
const TCHAR* SnowBankMaterial = TEXT("/Game/ChaosImpact/solo/Stage08/Materials/MI_SnowBank.MI_SnowBank");
const TCHAR* SnowPathMaterial = TEXT("/Game/ChaosImpact/solo/Stage08/Materials/MI_SnowPath.MI_SnowPath");
const TCHAR* IceMaterial = TEXT("/Game/ChaosImpact/FX/M_CI_IceSurface.M_CI_IceSurface");
FString Adventure(const TCHAR* Name) { return FString::Printf(TEXT("/Game/ChaosImpact/RPGPrototype/Adventure/Materials/M_%s.M_%s"), Name, Name); }
UStaticMesh* Shape(const TCHAR* Name) { return LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), Name, Name)); }
}

// ---------------------------------------------------------------- gates

ARPGWarpGate::ARPGWarpGate()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("GateRoot")));
}

void ARPGWarpGate::Build(bool bSnow)
{
	const auto Add = [&](const TCHAR* ShapeName, const FString& Material, const FVector& At, const FVector& Size, bool bSolid)
	{
		auto* Piece = NewObject<UStaticMeshComponent>(this);
		Piece->SetupAttachment(GetRootComponent()); Piece->SetStaticMesh(Shape(ShapeName));
		if (auto* M = LoadObject<UMaterialInterface>(nullptr, *Material)) { Piece->SetMaterial(0, M); }
		Piece->SetRelativeLocation(At); Piece->SetRelativeScale3D(Size / 100);
		Piece->SetCollisionEnabled(bSolid ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
		Piece->RegisterComponent(); Pieces.Add(Piece); return Piece;
	};
	const FString Stone = Adventure(bSnow ? TEXT("Stone") : TEXT("StoneDark"));
	for (const float Side : {-1.0f, 1.0f}) { Add(TEXT("Cube"), Stone, FVector(0, Side * 170, 190), FVector(70, 70, 380), true); }
	Add(TEXT("Cube"), Stone, FVector(0, 0, 400), FVector(90, 430, 60), false);
	Add(TEXT("Cube"), bSnow ? FString(SnowBankMaterial) : Adventure(TEXT("RoofTeal")), FVector(0, 0, 440), FVector(100, 450, 18), false);
	Portal = Add(TEXT("Cylinder"), Adventure(TEXT("GlowTeal")), FVector(0, 0, 4), FVector(300, 300, 6), false);
	Light = NewObject<UPointLightComponent>(this);
	Light->SetupAttachment(GetRootComponent()); Light->SetRelativeLocation(FVector(0, 0, 220));
	Light->SetLightColor(FLinearColor(0.35f, 0.85f, 1.0f)); Light->SetIntensity(30); Light->SetAttenuationRadius(500); Light->SetCastShadows(false);
	Light->RegisterComponent();
}

void ARPGWarpGate::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Portal) { const float T = GetWorld()->GetTimeSeconds(); Portal->SetRelativeScale3D(FVector(3.0f + 0.15f * FMath::Sin(T * 2.4f), 3.0f + 0.15f * FMath::Sin(T * 2.4f), 0.06f)); }
}

bool ARPGWarpGate::CanUse(const ARPGPrototypeCharacter* Player) const
{
	return Player && !Player->IsGrunt() && !Player->IsEliminated() && !Player->IsBagOpen() && !Player->IsQuestMenuOpen()
		&& FVector::DistSquared2D(Player->GetActorLocation(), GetActorLocation()) < FMath::Square(260.0f)
		&& FMath::Abs(Player->GetActorLocation().Z - GetActorLocation().Z) < 260;
}

ARPGWarpGate* ARPGWarpGate::Nearest(const ARPGPrototypeCharacter* Player)
{
	if (!Player || !Player->GetWorld()) { return nullptr; }
	for (TActorIterator<ARPGWarpGate> It(Player->GetWorld()); It; ++It) { if (It->CanUse(Player)) { return *It; } }
	return nullptr;
}

bool ARPGWarpGate::Use(ARPGPrototypeCharacter* Player)
{
	if (!CanUse(Player)) { return false; }
	// Arrive side by side, never on top of each other.
	const FVector Side = FRotator(0, DestinationYaw, 0).RotateVector(FVector::RightVector) * (FMath::Max(0, Player->GetLocalRPGIndex()) - 1.5f) * 120.0f;
	Player->GetCharacterMovement()->StopMovementImmediately();
	Player->TeleportHero(Destination + Side + FVector(0, 0, 120), FRotator(0, DestinationYaw, 0));
	Player->Notice = DisplayName.Contains(TEXT("雪原")) ? FString(TEXT("雪原に着いた。奥にはシマエナガの巣があるらしい")) : FString(TEXT("コロニーに戻った"));
	Player->NoticeUntil = GetWorld()->GetTimeSeconds() + 3;
	return true;
}

// ---------------------------------------------------------------- the boss's RPG shell

ARPGSimaeBody::ARPGSimaeBody()
{
	bUseToonCharacter = false;
	bRespawnAfterElimination = false;
}

void ARPGSimaeBody::BeginPlay()
{
	Super::BeginPlay();
	GetMesh()->SetHiddenInGame(true);
	SetActorEnableCollision(false);
	GetCharacterMovement()->DisableMovement();
}

void ARPGSimaeBody::Bind(AChaosImpactSimaeBoss* InBoss, int32 InLevel, bool bStrong)
{
	Boss = InBoss;
	bStrongVersion = bStrong;
	SetEnemy(true, InLevel);
	// RPG health at its level; the boss's own (small) health is kept in proportion.
	BaseMaxHealth = RPGTuning::SimaeBossHP * (1 + 0.09f * FMath::Max(0, InLevel - RPGTuning::SimaeBossLevel)) * (bStrong ? 1.7f : 1.0f);
	MaxHealth = Health = BaseMaxHealth;
}

void ARPGSimaeBody::ScaleForHeroes(int32 Count)
{
	MaxHealth = Health = BaseMaxHealth * (1 + RPGTuning::BossHPPerExtraPlayer * FMath::Max(0, Count - 1));
}

bool ARPGSimaeBody::IsAwake() const
{
	if (!HasAuthority()) { return (BossFlags & 1) != 0; }
	return Boss && !Boss->IsDormant() && !Boss->IsEliminated();
}

void ARPGSimaeBody::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ARPGSimaeBody, Boss);
	DOREPLIFETIME(ARPGSimaeBody, BossFlags);
}

void ARPGSimaeBody::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	AChaosImpactSimaeBoss* Real = Boss.Get();
	if (!Real || !HasAuthority()) { return; }
	BossFlags = (IsAwake() ? 1 : 0) | (Real->IsFullPower() ? 2 : 0) | (Real->GetMove() == EChaosImpactSimaeMove::PowerUp ? 4 : 0) | (bStrongVersion ? 8 : 0);
	// Stand where the boss stands (its knockbacks, pulls and pushes never move it: it is too big).
	SetActorLocation(Real->GetActorLocation(), false, nullptr, ETeleportType::TeleportPhysics);
	if (Real->IsEliminated() && !bRewarded)
	{
		bRewarded = true; Health = 0;
		if (auto* Mode = GetWorld()->GetAuthGameMode<ARPGPrototypeGameMode>()) { Mode->RewardEnemy(this, true); }
	}
}

float ARPGSimaeBody::TakeDamage(float Amount, const FDamageEvent& Event, AController* EventInstigator, AActor* Causer)
{
	AChaosImpactSimaeBoss* Real = Boss.Get();
	if (!HasAuthority()) { return 0; }
	if (!Real || Real->IsDormant() || Real->IsEliminated() || Real->GetMove() == EChaosImpactSimaeMove::PowerUp || Amount <= 0) { return 0; }
	// The boss decides what lands (half health always powers it up first); this side shows it in RPG numbers.
	const float Before = Real->GetHealth();
	Real->TakeDamage(Amount * Real->GetMaxHealth() / FMath::Max(1.0f, MaxHealth), Event, EventInstigator, Causer);
	const float Landed = (Before - Real->GetHealth()) * MaxHealth / FMath::Max(0.01f, Real->GetMaxHealth());
	if (Landed <= 0) { return 0; }
	// Never 0 here: the boss's own defeat gives the reward (see Tick), once.
	const float Applied = Super::TakeDamage(FMath::Min(Landed, Health - 1), Event, EventInstigator, Causer);
	Health = FMath::Max(1.0f, Real->GetHealth() / FMath::Max(0.01f, Real->GetMaxHealth()) * MaxHealth);
	return Applied;
}

// ---------------------------------------------------------------- the snowfield

ARPGSnowField::ARPGSnowField()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("SnowRoot")));
}

ARPGSnowField* ARPGSnowField::Find(const UWorld* World)
{
	if (!World) { return nullptr; }
	for (TActorIterator<ARPGSnowField> It(const_cast<UWorld*>(World)); It; ++It) { return *It; }
	return nullptr;
}

FString ARPGSnowField::RegionAt(const UWorld* World, const FVector& At)
{
	const auto* Adventure = ARPGAdventureWorld::Find(World);
	const FString Name = RPGWorld::RegionAt(Adventure ? Adventure->GetActorTransform().InverseTransformPosition(At) : At);
	return Name.StartsWith(TEXT("雪原")) ? Name : FString();
}

FVector ARPGSnowField::Origin() { return RPGWorld::AreaPoint(TEXT("Nest")); }

ARPGSnowField* ARPGSnowField::Ensure(UWorld* World, const FTransform& MapOrigin)
{
	if (!World || Find(World)) { return Find(World); }
	// A map without the snowfield (an older level): no nest.
	if (!RPGWorld::FindArea(TEXT("Nest"))) { return nullptr; }
	FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// On the nest's real floor (traced, so a hand-edited map still works).
	FVector At = MapOrigin.TransformPosition(Origin());
	FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(RPGSnowNest), false);
	if (World->LineTraceSingleByObjectType(Hit, At + FVector(0, 0, 3000), At - FVector(0, 0, 3000), FCollisionObjectQueryParams(ECC_WorldStatic), Params)) { At = Hit.ImpactPoint; }
	return World->SpawnActor<ARPGSnowField>(At, FRotator::ZeroRotator, Spawn);
}

FVector ARPGSnowField::NestCenter() const { return GetActorLocation(); }
AChaosImpactSimaeBoss* ARPGSnowField::GetBoss() const { return BossActor.Get(); }

UStaticMeshComponent* ARPGSnowField::Part(const TCHAR* ShapeName, const TCHAR* Material, const FVector& At, const FVector& Size, const FRotator& Rotation, bool bSolid)
{
	auto* Piece = NewObject<UStaticMeshComponent>(this);
	Piece->SetupAttachment(GetRootComponent());
	Piece->SetStaticMesh(Shape(ShapeName));
	if (auto* M = LoadObject<UMaterialInterface>(nullptr, Material)) { Piece->SetMaterial(0, M); }
	Piece->SetRelativeLocation(At); Piece->SetRelativeRotation(Rotation); Piece->SetRelativeScale3D(Size / 100);
	Piece->SetCollisionEnabled(bSolid ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	Piece->SetCollisionResponseToAllChannels(ECR_Block);
	Piece->SetMobility(EComponentMobility::Movable);
	Piece->RegisterComponent();
	Pieces.Add(Piece);
	return Piece;
}

void ARPGSnowField::Lantern(const FVector& At)
{
	Part(TEXT("Cylinder"), *Adventure(TEXT("Timber")), At + FVector(0, 0, 110), FVector(18, 18, 220), FRotator::ZeroRotator, true);
	Part(TEXT("Cube"), *Adventure(TEXT("GlowWarm")), At + FVector(0, 0, 230), FVector(40, 40, 46));
	auto* Light = NewObject<UPointLightComponent>(this);
	Light->SetupAttachment(GetRootComponent()); Light->SetRelativeLocation(At + FVector(0, 0, 240));
	Light->SetLightColor(FLinearColor(1.0f, 0.7f, 0.4f)); Light->SetIntensity(40); Light->SetAttenuationRadius(650); Light->SetCastShadows(false);
	Light->RegisterComponent();
}

void ARPGSnowField::Build()
{
	// The nest: a ring of snow walls and ice spires round a flat arena, open to the south where the path comes in
	// (closed while fighting). The actor stands in its middle, on the floor.
	const FVector Nest(0, 0, 0);
	// Hard snow under the arena, so it is level whatever the ground does at the edge.
	Part(TEXT("Cylinder"), SnowMaterial, FVector(0, 0, -95), FVector(NestRadius() * 2 + 200, NestRadius() * 2 + 200, 200), FRotator::ZeroRotator, true);
	const float Ring = NestRadius() + 150;
	for (int32 I = 0; I < 28; ++I)
	{
		const float Angle = 360.0f * I / 28;
		if (FMath::Abs(FMath::FindDeltaAngleDegrees(Angle, 180.0f)) < 17) { continue; }
		const FVector Dir = FRotator(0, Angle, 0).Vector();
		Part(TEXT("Cube"), SnowBankMaterial, Nest + Dir * Ring + FVector(0, 0, 330), FVector(200, 420, 660), FRotator(0, Angle, 0), true);
		if (I % 2 == 0) { Part(TEXT("Cone"), IceMaterial, Nest + Dir * (Ring + 60) + FVector(0, 0, 820), FVector(170, 170, 520)); }
	}
	// An invisible fence above the walls, so a big knockback cannot throw anyone over.
	for (int32 I = 0; I < 28; ++I)
	{
		const float Angle = 360.0f * I / 28;
		if (FMath::Abs(FMath::FindDeltaAngleDegrees(Angle, 180.0f)) < 17) { continue; }
		auto* Fence = Part(TEXT("Cube"), SnowBankMaterial, Nest + FRotator(0, Angle, 0).Vector() * Ring + FVector(0, 0, 2000), FVector(200, 420, 2800), FRotator(0, Angle, 0), true);
		Fence->SetHiddenInGame(true);
	}
	// The nest itself: a heap of twigs and down at the back.
	for (int32 I = 0; I < 14; ++I)
	{
		const float A = I * 2 * PI / 14;
		Part(TEXT("Cylinder"), *Adventure(TEXT("Bark")), Nest + FVector(700 + FMath::Cos(A) * 380, FMath::Sin(A) * 380, 45), FVector(30, 30, 420), FRotator(80, I * 26.0f, 0));
	}
	Part(TEXT("Sphere"), SnowMaterial, Nest + FVector(700, 0, 10), FVector(560, 560, 90));
	// The door that freezes shut (hidden while open).
	for (int32 I = -2; I <= 2; ++I)
	{
		const float Angle = 180 + I * 6.5f;
		auto* Door = Part(TEXT("Cube"), IceMaterial, Nest + FRotator(0, Angle, 0).Vector() * Ring + FVector(0, 0, 1400), FVector(160, 240, 2800), FRotator(0, Angle, 0), true);
		NestDoor.Add(Door);
	}
	SetNestClosed(false);
	for (const FVector& At : {Nest + FVector(-Ring - 250, -330, 0), Nest + FVector(-Ring - 250, 330, 0)}) { Lantern(At); }
}

void ARPGSnowField::SetNestClosed(bool bClosed)
{
	for (UStaticMeshComponent* Door : NestDoor)
	{
		if (!Door) { continue; }
		Door->SetHiddenInGame(!bClosed);
		Door->SetCollisionEnabled(bClosed ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	}
}

void ARPGSnowField::BeginPlay()
{
	Super::BeginPlay();
	Build();
	// The boss is the host's; a guest's screen gets it from there.
	if (GetWorld()->GetNetMode() == NM_Client) { return; }
	SpawnBoss();
}

void ARPGSnowField::SpawnBoss()
{
	UWorld* World = GetWorld();
	if (auto* Old = BossActor.Get()) { Old->Destroy(); }
	if (auto* Old = Body.Get()) { Old->Destroy(); }
	FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	const FTransform Where(FRotator(0, 180, 0), NestCenter() + FVector(300, 0, 260));
	// The solo boss as it is: asleep in its nest until someone walks in.
	auto* Boss = World->SpawnActorDeferred<AChaosImpactSimaeBoss>(AChaosImpactSimaeBoss::StaticClass(), Where, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (!Boss) { return; }
	// Once beaten it comes back as its strong version: quicker between moves, faster throws, more snowballs.
	const bool bStrong = Defeats > 0;
	if (bStrong)
	{
		Boss->RestSeconds *= 0.65f; Boss->FullPowerRestSeconds *= 0.65f; Boss->FullPowerTempo = 1.3f;
		Boss->WalkSpeed *= 1.2f; Boss->FullPowerWalkSpeed *= 1.2f; Boss->GoukyuSpeed *= 1.15f;
		Boss->AvalancheCount += 2; Boss->FullPowerAvalancheCount += 3;
	}
	Boss->SetDormant(true);
	Boss->FinishSpawning(Where);
	BossActor = Boss;
	auto* Shell = World->SpawnActorDeferred<ARPGSimaeBody>(ARPGSimaeBody::StaticClass(), Where, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	const int32 Level = BossLevel + (bStrong ? 8 : 0);
	if (Shell) { Shell->SetEnemy(true, Level); Shell->FinishSpawning(Where); Shell->Bind(Boss, Level, bStrong); Body = Shell; }
	bFighting = false; EmptySince = -1; DefeatedAt = -1;
	SetNestClosed(false);
}

void ARPGSnowField::ResetBoss() { SpawnBoss(); }

void ARPGSnowField::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (GetWorld()->GetNetMode() == NM_Client)
	{
		// A guest: the nest door follows the host's boss.
		if (!Body.IsValid()) { for (TActorIterator<ARPGSimaeBody> It(GetWorld()); It; ++It) { Body = *It; BossActor = It->GetBoss(); break; } }
		const bool bShut = Body.IsValid() && Body->IsAwake();
		if (bShut != bFighting) { bFighting = bShut; SetNestClosed(bShut); }
		return;
	}
	AChaosImpactSimaeBoss* Boss = BossActor.Get();
	const double Now = GetWorld()->GetTimeSeconds();
	int32 Inside = 0, Near = 0;
	for (TActorIterator<ARPGPrototypeCharacter> It(GetWorld()); It; ++It)
	{
		if (It->IsGrunt() || It->IsEliminated()) { continue; }
		const float D = FVector::Dist2D(It->GetActorLocation(), NestCenter());
		Inside += D < NestRadius() - 150 ? 1 : 0;
		Near += D < NestRadius() + 2500 ? 1 : 0;
	}
	if (!Boss)
	{
		// Beaten (or gone): back again once everyone has left the nest for a while.
		if (DefeatedAt > 0 && Near == 0 && Now - DefeatedAt > 10) { SpawnBoss(); }
		return;
	}
	if (Boss->IsEliminated())
	{
		if (DefeatedAt < 0) { DefeatedAt = Now; bFighting = false; ++Defeats; }
		if (Now - DefeatedAt > 4) { SetNestClosed(false); }
		if (Near == 0 && Now - DefeatedAt > 10) { SpawnBoss(); }
		return;
	}
	if (!bFighting)
	{
		if (Inside > 0)
		{
			bFighting = true; Boss->SetDormant(false); SetNestClosed(true); EmptySince = -1;
			// More health for more players in the nest (and near its door).
			if (auto* Shell = Body.Get()) { Shell->ScaleForHeroes(Near); }
		}
		return;
	}
	// Everyone knocked out or out of the nest: it starts over, asleep and whole.
	if (Inside == 0) { if (EmptySince < 0) { EmptySince = Now; } else if (Now - EmptySince > 3) { SpawnBoss(); } }
	else { EmptySince = -1; }
}
