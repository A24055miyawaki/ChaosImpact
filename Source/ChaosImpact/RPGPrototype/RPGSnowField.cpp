#include "RPGSnowField.h"
#include "Net/UnrealNetwork.h"
#include "RPGAdventure.h"
#include "RPGQuests.h"
#include "RPGTuning.h"
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
/** The snowfield's places, by how far along it (local X) they are. */
struct FSnowPlace { float FromX; const TCHAR* Name; };
const FSnowPlace SnowPlaces[] = {
	{-99999, TEXT("雪原・入口の野営地")}, {-4200, TEXT("雪原・針葉樹の森")}, {-300, TEXT("雪原・凍った湖")}, {3300, TEXT("雪原・シマエナガの巣")}};
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

void ARPGSimaeBody::Bind(AChaosImpactSimaeBoss* InBoss, int32 InLevel)
{
	Boss = InBoss;
	SetEnemy(true, InLevel);
	// RPG health at its level; the boss's own (small) health is kept in proportion.
	MaxHealth = Health = RPGTuning::SimaeBossHP * (1 + 0.09f * FMath::Max(0, InLevel - RPGTuning::SimaeBossLevel));
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
	BossFlags = (IsAwake() ? 1 : 0) | (Real->IsFullPower() ? 2 : 0) | (Real->GetMove() == EChaosImpactSimaeMove::PowerUp ? 4 : 0);
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
	if (!Find(World)) { return FString(); }
	const FVector Local = At - Origin();
	if (FMath::Abs(Local.X) > HalfSize().X + 600 || FMath::Abs(Local.Y) > HalfSize().Y + 600) { return FString(); }
	FString Name;
	for (const auto& Place : SnowPlaces) { if (Local.X >= Place.FromX) { Name = Place.Name; } }
	return Name;
}

ARPGSnowField* ARPGSnowField::Ensure(UWorld* World, const FTransform& MapOrigin)
{
	if (!World || Find(World)) { return Find(World); }
	FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Field = World->SpawnActor<ARPGSnowField>(Origin(), FRotator::ZeroRotator, Spawn);
	if (!Field) { return nullptr; }
	// The colony's gate stands beside the board; the snowfield's at its camp.
	const FVector Near = MapOrigin.TransformPosition(FVector(300, 1150, 4000));
	FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(RPGSnowGate), false);
	const FVector ColonyGround = World->LineTraceSingleByObjectType(Hit, Near, Near - FVector(0, 0, 9000), FCollisionObjectQueryParams(ECC_WorldStatic), Params)
		? Hit.ImpactPoint : MapOrigin.TransformPosition(FVector(300, 1150, 0));
	auto* ToSnow = World->SpawnActor<ARPGWarpGate>(ColonyGround, FRotator::ZeroRotator, Spawn);
	auto* ToColony = World->SpawnActor<ARPGWarpGate>(Origin() + FVector(-6600, 0, 0), FRotator::ZeroRotator, Spawn);
	if (ToSnow && ToColony)
	{
		ToSnow->DisplayName = TEXT("雪原への門"); ToSnow->Destination = Field->ArrivalPoint(); ToSnow->DestinationYaw = 0; ToSnow->Build(false);
		ToColony->DisplayName = TEXT("コロニーへ戻る門"); ToColony->Destination = ColonyGround + FVector(-320, 0, 0); ToColony->DestinationYaw = 180; ToColony->Build(true);
	}
	return Field;
}

FVector ARPGSnowField::ArrivalPoint() const { return Origin() + FVector(-6150, 0, 0); }
FVector ARPGSnowField::NestCenter() const { return Origin() + FVector(5100, 0, 0); }
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

void ARPGSnowField::Pine(const FVector& At, float Scale)
{
	const FString Bark = Adventure(TEXT("Bark")), Needles = Adventure(TEXT("Pine"));
	Part(TEXT("Cylinder"), *Bark, At + FVector(0, 0, 90 * Scale), FVector(40, 40, 180) * Scale, FRotator::ZeroRotator, true);
	for (int32 Tier = 0; Tier < 3; ++Tier)
	{
		const float S = Scale * (1.0f - Tier * 0.22f);
		Part(TEXT("Cone"), *Needles, At + FVector(0, 0, (170 + Tier * 120) * Scale), FVector(260, 260, 220) * S);
		Part(TEXT("Cone"), SnowMaterial, At + FVector(0, 0, (215 + Tier * 120) * Scale), FVector(170, 170, 120) * S);
	}
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
	const FVector2D Half = HalfSize();
	// The plain, a little rolling, walled by snowbanks and cliffs (high enough that knockbacks stay inside).
	Part(TEXT("Cube"), SnowMaterial, FVector(0, 0, -200), FVector(Half.X * 2 + 1200, Half.Y * 2 + 1200, 400), FRotator::ZeroRotator, true);
	for (const float Side : {-1.0f, 1.0f})
	{
		Part(TEXT("Cube"), SnowBankMaterial, FVector(0, Side * (Half.Y + 300), 450), FVector(Half.X * 2 + 1200, 600, 900), FRotator::ZeroRotator, true);
		Part(TEXT("Cube"), SnowBankMaterial, FVector(Side * (Half.X + 300), 0, 450), FVector(600, Half.Y * 2 + 1200, 900), FRotator::ZeroRotator, true);
		for (int32 I = 0; I < 9; ++I)
		{
			Part(TEXT("Sphere"), SnowBankMaterial, FVector(-Half.X + I * Half.X * 0.25f, Side * (Half.Y - 120), 60), FVector(900, 520, 380));
		}
	}
	// A trodden path from the camp to the nest.
	Part(TEXT("Cube"), SnowPathMaterial, FVector(-500, 0, 2), FVector(Half.X * 2 - 1400, 520, 4));

	// Camp: tents, a fire ring, lanterns, crates.
	for (const float Y : {-900.0f, 900.0f})
	{
		Part(TEXT("Cube"), *Adventure(TEXT("Planks")), FVector(-6000, Y, 25), FVector(420, 380, 50), FRotator::ZeroRotator, true);
		for (const float S : {-1.0f, 1.0f}) { Part(TEXT("Cube"), *Adventure(TEXT("RoofTeal")), FVector(-6000, Y + S * 100, 190), FVector(440, 250, 18), FRotator(0, 0, S * 40)); }
	}
	for (int32 I = 0; I < 8; ++I) { const float A = I * 2 * PI / 8; Part(TEXT("Sphere"), *Adventure(TEXT("Stone")), FVector(-5500 + FMath::Cos(A) * 95, -350 + FMath::Sin(A) * 95, 18), FVector(55, 50, 36)); }
	Part(TEXT("Cube"), *Adventure(TEXT("GlowWarm")), FVector(-5500, -350, 30), FVector(70, 70, 40));
	for (const FVector& At : {FVector(-6500, -500, 0), FVector(-6500, 500, 0), FVector(-5000, -700, 0), FVector(-5000, 700, 0)}) { Lantern(At); }
	for (int32 I = 0; I < 3; ++I) { Part(TEXT("Cube"), *Adventure(TEXT("Planks")), FVector(-5700 + I * 110, 1250, 55), FVector(95, 95, 110), FRotator::ZeroRotator, true); }

	// Pine wood (paths left clear in the middle).
	FRandomStream Random(2026);
	for (int32 I = 0; I < 34; ++I)
	{
		const float X = Random.FRandRange(-4200, -400);
		float Y = Random.FRandRange(-Half.Y + 500, Half.Y - 500);
		if (FMath::Abs(Y) < 600) { Y += Y < 0 ? -700 : 700; }
		Pine(FVector(X, Y, 0), Random.FRandRange(1.0f, 1.7f));
	}
	for (int32 I = 0; I < 10; ++I) { Part(TEXT("Sphere"), *Adventure(TEXT("Rock")), FVector(Random.FRandRange(-4000, 3000), Random.FRandRange(-Half.Y + 600, Half.Y - 600), 30), FVector(220, 180, 140) * Random.FRandRange(0.7f, 1.4f), FRotator(0, Random.FRandRange(0, 360), 0), true); }

	// Frozen lake (walkable, slick-looking ice).
	Part(TEXT("Cylinder"), IceMaterial, FVector(1500, -1700, 3), FVector(3000, 2200, 6));
	Part(TEXT("Cylinder"), SnowBankMaterial, FVector(1500, -1700, -2), FVector(3200, 2400, 8));
	for (int32 I = 0; I < 8; ++I) { Pine(FVector(Random.FRandRange(0, 3200), Random.FRandRange(900, Half.Y - 500), 0), Random.FRandRange(1.1f, 1.6f)); }

	// The nest: a ring of snow walls and ice spires round a flat arena, open toward the camp (closed while fighting).
	const FVector Nest(5100, 0, 0);
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
	UWorld* World = GetWorld();
	FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// The watcher at the camp (quests "雪原の狩り" and "粉雪の主").
	if (auto* Giver = World->SpawnActor<ARPGQuestGiver>(Origin() + FVector(-5650, 420, 2), FRotator(0, 180, 0), Spawn)) { Giver->GiverId = 5; }
	// A rest beacon: a knockout in the snowfield starts again at the camp.
	World->SpawnActor<ARPGAdventureCheckpoint>(Origin() + FVector(-5900, -650, 0), FRotator::ZeroRotator, Spawn);
	for (TActorIterator<ARPGAdventureCheckpoint> It(World); It; ++It)
	{ if (FVector::DistSquared2D(It->GetActorLocation(), Origin() + FVector(-5900, -650, 0)) < 100.0f) { It->DisplayName = TEXT("雪原の灯り"); It->RespawnOffset = FVector(250, 0, 120); } }
	// The fights (enemy groups and the boss) are the host's; a guest's screen gets those from it.
	if (World->GetNetMode() == NM_Client) { return; }
	// Enemy groups: the wood and the lake.
	const struct { FVector At; int32 Level; int32 Chasers; int32 Throwers; } Groups[] = {
		{FVector(-2400, 1900, 0), BossLevel - 4, 3, 2}, {FVector(-1500, -2200, 0), BossLevel - 3, 2, 3}, {FVector(1800, 2300, 0), BossLevel - 2, 3, 2}, {FVector(2400, -1300, 0), BossLevel - 2, 2, 2}};
	for (const auto& Group : Groups)
	{
		auto* Point = World->SpawnActorDeferred<ARPGPrototypeSpawnPoint>(ARPGPrototypeSpawnPoint::StaticClass(), FTransform(Origin() + Group.At + FVector(0, 0, 60)), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Point) { continue; }
		Point->bEncounterGroup = true; Point->EnemyLevel = Group.Level; Point->ChaserCount = Group.Chasers; Point->ThrowerCount = Group.Throwers;
		Point->DropChance = 0.3f; Point->RespawnSeconds = 45; Point->EngageRadius = 950; Point->PursuitRadius = 1800;
		Point->FinishSpawning(FTransform(Origin() + Group.At + FVector(0, 0, 60)));
	}
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
	Boss->SetDormant(true);
	Boss->FinishSpawning(Where);
	BossActor = Boss;
	auto* Shell = World->SpawnActorDeferred<ARPGSimaeBody>(ARPGSimaeBody::StaticClass(), Where, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Shell) { Shell->SetEnemy(true, BossLevel); Shell->FinishSpawning(Where); Shell->Bind(Boss, BossLevel); Body = Shell; }
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
		if (DefeatedAt < 0) { DefeatedAt = Now; bFighting = false; }
		if (Now - DefeatedAt > 4) { SetNestClosed(false); }
		if (Near == 0 && Now - DefeatedAt > 10) { SpawnBoss(); }
		return;
	}
	if (!bFighting)
	{
		if (Inside > 0) { bFighting = true; Boss->SetDormant(false); SetNestClosed(true); EmptySince = -1; }
		return;
	}
	// Everyone knocked out or out of the nest: it starts over, asleep and whole.
	if (Inside == 0) { if (EmptySince < 0) { EmptySince = Now; } else if (Now - EmptySince > 3) { SpawnBoss(); } }
	else { EmptySince = -1; }
}
