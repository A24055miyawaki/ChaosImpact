#include "ChaosImpactSoloRoom.h"
#include "ChaosImpactEase.h"
#include "ChaosImpactSfx.h"

#include "ChaosImpact.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactFxKit.h"
#include "ChaosImpactSoloStages.h"
#include "TimerManager.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactSimaeBoss.h"
#include "EngineUtils.h"
#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	const TCHAR* const SoloRoomMinionPath = TEXT("/Game/ChaosImpact/solo/BP_SoloEnemy.BP_SoloEnemy_C");
	const TCHAR* const SoloRoomBossPath = TEXT("/Game/ChaosImpact/solo/BP_BossEnemy.BP_BossEnemy_C");
	/** How far below its shut height the open door sinks (out of sight under the floor). */
	constexpr float SoloRoomDoorSink = 330.0f;
}

AChaosImpactSoloRoom::AChaosImpactSoloRoom()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	// A room 24 m across; its doorway on the -X side (move and resize every part to fit the level).
	RoomArea = CreateDefaultSubobject<UBoxComponent>(TEXT("RoomArea"));
	RoomArea->SetupAttachment(Root);
	RoomArea->SetBoxExtent(FVector(1200.0f, 1200.0f, 200.0f));
	RoomArea->SetRelativeLocation(FVector(0.0f, 0.0f, 200.0f));
	RoomArea->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RoomArea->ShapeColor = FColor(80, 200, 255);

	EntryTrigger = CreateDefaultSubobject<UBoxComponent>(TEXT("EntryTrigger"));
	EntryTrigger->SetupAttachment(Root);
	EntryTrigger->SetBoxExtent(FVector(60.0f, 260.0f, 150.0f));
	EntryTrigger->SetRelativeLocation(FVector(-1080.0f, 0.0f, 150.0f));
	EntryTrigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	EntryTrigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	EntryTrigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	EntryTrigger->SetGenerateOverlapEvents(true);
	EntryTrigger->ShapeColor = FColor(255, 200, 40);

	Door = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Door"));
	Door->SetupAttachment(Root);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		Door->SetStaticMesh(Cube.Object);
	}
	// A slab 40 cm thick, 6 m wide, 3 m high, standing in the doorway (its middle 1.5 m up).
	Door->SetRelativeLocation(FVector(-1220.0f, 0.0f, 150.0f));
	Door->SetRelativeScale3D(FVector(0.4f, 6.0f, 3.0f));
	Door->SetCollisionProfileName(TEXT("BlockAll"));

	// The stand-in room: a floor and five walls (see LayoutPlaceholder).
	static const TCHAR* const PartNames[] = {TEXT("PlaceholderFloor"), TEXT("PlaceholderWallFar"), TEXT("PlaceholderWallLeft"),
		TEXT("PlaceholderWallRight"), TEXT("PlaceholderWallDoorLeft"), TEXT("PlaceholderWallDoorRight")};
	for (const TCHAR* PartName : PartNames)
	{
		UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(PartName);
		Part->SetupAttachment(Root);
		if (Cube.Succeeded())
		{
			Part->SetStaticMesh(Cube.Object);
		}
		Part->SetCollisionProfileName(TEXT("BlockAll"));
		PlaceholderParts.Add(Part);
	}

	RespawnPoint = CreateDefaultSubobject<UArrowComponent>(TEXT("RespawnPoint"));
	RespawnPoint->SetupAttachment(Root);
	RespawnPoint->SetRelativeLocation(FVector(-1500.0f, 0.0f, 100.0f));
	RespawnPoint->ArrowColor = FColor(80, 255, 120);
	RespawnPoint->ArrowSize = 2.0f;
}

void AChaosImpactSoloRoom::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	LayoutPlaceholder();
}

void AChaosImpactSoloRoom::LayoutPlaceholder()
{
	if (PlaceholderParts.Num() < 6)
	{
		return;
	}
	for (UStaticMeshComponent* Part : PlaceholderParts)
	{
		Part->SetVisibility(bPlaceholderWalls);
		Part->SetCollisionEnabled(bPlaceholderWalls ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	}
	if (!bPlaceholderWalls)
	{
		return;
	}
	// Everything in the room's own space (its doorway on the -X side, where the Door stands).
	constexpr float Thick = 40.0f;
	constexpr float Height = 300.0f;
	const FVector Middle = RoomArea->GetRelativeLocation();
	const FVector Extent = RoomArea->GetUnscaledBoxExtent() * RoomArea->GetRelativeScale3D();
	const float DoorX = Door->GetRelativeLocation().X;
	const float DoorY = Door->GetRelativeLocation().Y;
	const float DoorHalf = Door->GetRelativeScale3D().Y * 50.0f;
	const float NearX = DoorX;
	const float FarX = Middle.X + Extent.X + Thick * 0.5f;
	const float LeftY = Middle.Y - Extent.Y - Thick * 0.5f;
	const float RightY = Middle.Y + Extent.Y + Thick * 0.5f;
	const auto Place = [](UStaticMeshComponent* Part, const FVector& Center, const FVector& Size)
	{
		// The cube is a metre on each side.
		Part->SetRelativeLocation(Center);
		Part->SetRelativeScale3D(Size / 100.0f);
	};
	const float Length = FarX - NearX;
	// Floor: its top at the room's own height.
	Place(PlaceholderParts[0], FVector((NearX + FarX) * 0.5f, Middle.Y, -10.0f), FVector(Length + Thick, RightY - LeftY + Thick, 20.0f));
	Place(PlaceholderParts[1], FVector(FarX, Middle.Y, Height * 0.5f), FVector(Thick, RightY - LeftY + Thick, Height));
	Place(PlaceholderParts[2], FVector((NearX + FarX) * 0.5f, LeftY, Height * 0.5f), FVector(Length, Thick, Height));
	Place(PlaceholderParts[3], FVector((NearX + FarX) * 0.5f, RightY, Height * 0.5f), FVector(Length, Thick, Height));
	// The doorway's wall: either side of the door.
	const float LeftEnd = DoorY - DoorHalf;
	const float RightStart = DoorY + DoorHalf;
	Place(PlaceholderParts[4], FVector(NearX, (LeftY + LeftEnd) * 0.5f, Height * 0.5f), FVector(Thick, FMath::Max(LeftEnd - LeftY, 1.0f), Height));
	Place(PlaceholderParts[5], FVector(NearX, (RightStart + RightY) * 0.5f, Height * 0.5f), FVector(Thick, FMath::Max(RightY - RightStart, 1.0f), Height));
}

void AChaosImpactSoloRoom::BeginPlay()
{
	Super::BeginPlay();
	LayoutPlaceholder();
	DoorUpLocation = Door->GetRelativeLocation();
	EntryTrigger->OnComponentBeginOverlap.AddDynamic(this, &AChaosImpactSoloRoom::HandleEntryOverlap);
	// Open to begin with.
	bDoorsClosed = true;
	SetDoorsClosed(false);
	DoorRaise = 0.0f;
	Door->SetRelativeLocation(DoorUpLocation - FVector(0.0f, 0.0f, SoloRoomDoorSink));
	Door->SetVisibility(false);
	PlanDifficulty();
	if (bBossWaitsFromStart && bSpawnBoss)
	{
		SpawnWaitingBoss();
	}
}

void AChaosImpactSoloRoom::PlanDifficulty()
{
	PlannedMinions = MinionCount;
	MaxAliveMinions = FMath::Max(MinionCount, 1);
	PlannedBossLevel = BossLevel;
	MinionPlan.Init(MinionLevel, FMath::Max(MinionCount, 0));
	const UWorld* World = GetWorld();
	const int32 Stage = World ? FCString::Atoi(World->URL.GetOption(TEXT("CIStage="), TEXT("0"))) : 0;
	if (!World || Stage <= 0 || !World->URL.HasOption(TEXT("CISolo")) || MinionCount <= 0 || !MinionClass.IsNull() || !BossClass.IsNull())
	{
		return;
	}
	const ChaosImpactSolo::FDifficulty Plan = ChaosImpactSolo::GetDifficulty(Stage);
	PlannedMinions = Plan.Minions;
	MaxAliveMinions = Plan.MaxAlive;
	PlannedBossLevel = Plan.BossLevel;
	// The weak ones first, the strong ones last (the fight builds up).
	MinionPlan.Reset();
	for (int32 Index = 0; Index < Plan.Minions; ++Index)
	{
		MinionPlan.Add(Index < Plan.Weak ? EChaosImpactCPULevel::Weak
			: Index >= Plan.Minions - Plan.Strong ? EChaosImpactCPULevel::Strong : EChaosImpactCPULevel::Normal);
	}
	UE_LOG(LogChaosImpact, Log, TEXT("Solo room %s: stage %d plays %d minions (%d at once, %d weak, %d strong), boss level %d"),
		*GetName(), Stage, Plan.Minions, Plan.MaxAlive, Plan.Weak, Plan.Strong, static_cast<int32>(Plan.BossLevel));
}

void AChaosImpactSoloRoom::SpawnNextMinion()
{
	if (Phase != EChaosImpactSoloRoomPhase::Minions || SpawnedMinions >= PlannedMinions)
	{
		return;
	}
	const FVector Where = PickSpawnLocation(SpawnedMinions);
	const EChaosImpactCPULevel Level = MinionPlan.IsValidIndex(SpawnedMinions) ? MinionPlan[SpawnedMinions] : MinionLevel;
	if (AChaosImpactCharacter* Minion = SpawnEnemy(MinionClass, SoloRoomMinionPath, Where, Level))
	{
		Enemies.Add(Minion);
		if (AChaosImpactFxKit* Fx = AChaosImpactFxKit::Spawn(this, Where - FVector(0.0f, 0.0f, 96.0f)))
		{
			// Where it steps in: a flash and a ring closing over the floor, so it is seen coming.
			FChaosImpactFxPiece Flash;
			Flash.Location = FVector(0.0f, 0.0f, 90.0f);
			Flash.Life = 0.3f;
			Flash.SizeFrom = 120.0f;
			Flash.SizeTo = 300.0f;
			Flash.Color = FLinearColor(1.0f, 0.15f, 0.2f);
			Flash.CoreColor = FLinearColor(1.0f, 0.85f, 0.8f);
			Flash.Intensity = 4.0f;
			Flash.Attack = 0.06f;
			Fx->Add(Flash);
			FChaosImpactFxPiece Ring;
			Ring.Look = EChaosImpactFxLook::Ring;
			Ring.Location = FVector(0.0f, 0.0f, 4.0f);
			Ring.Life = 0.45f;
			Ring.SizeFrom = Ring.SizeTo = 420.0f;
			Ring.RingFrom = 0.9f;
			Ring.RingTo = 0.25f;
			Ring.RingWidth = 0.06f;
			Ring.Color = FLinearColor(1.0f, 0.15f, 0.2f);
			Ring.CoreColor = FLinearColor(1.0f, 0.85f, 0.8f);
			Ring.Intensity = 2.5f;
			Ring.Attack = 0.1f;
			Fx->Add(Ring);
		}
	}
	++SpawnedMinions;
}

void AChaosImpactSoloRoom::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	BindPlayer(nullptr);
	Super::EndPlay(EndPlayReason);
}

void AChaosImpactSoloRoom::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// The door rises out of the floor or sinks back into it.
	const float Target = bDoorsClosed ? 1.0f : 0.0f;
	if (!FMath::IsNearlyEqual(DoorRaise, Target))
	{
		DoorRaise = DoorSeconds <= 0.0f ? Target
			: FMath::FInterpConstantTo(DoorRaise, Target, DeltaSeconds, 1.0f / DoorSeconds);
		const float Eased = ChaosImpactEase::InOut(DoorRaise);
		Door->SetRelativeLocation(DoorUpLocation - FVector(0.0f, 0.0f, SoloRoomDoorSink * (1.0f - Eased)));
		Door->SetVisibility(DoorRaise > 0.01f);
	}
}

void AChaosImpactSoloRoom::HandleEntryOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	AChaosImpactCharacter* Entering = Cast<AChaosImpactCharacter>(OtherActor);
	if (Phase == EChaosImpactSoloRoomPhase::Waiting && Entering && Entering->IsPlayerControlled() && !Entering->IsEliminated())
	{
		StartRoom(Entering);
	}
}

void AChaosImpactSoloRoom::StartRoom(AChaosImpactCharacter* InPlayer)
{
	if (Phase != EChaosImpactSoloRoomPhase::Waiting || !GetWorld())
	{
		return;
	}
	BindPlayer(InPlayer);
	ClearEnemies(bBossWaitsFromStart);
	PlanDifficulty();
	Defeated = 0;
	SpawnedMinions = 0;
	Phase = EChaosImpactSoloRoomPhase::Minions;
	SetDoorsClosed(true);
	if (bReachedBoss && bSpawnBoss && PlannedMinions > 0 && !bBossWaitsFromStart)
	{
		// Knocked out by the boss before: straight back to it (the minions were already beaten).
		UE_LOG(LogChaosImpact, Log, TEXT("Solo room %s: back to the boss"), *GetName());
		OnRoomStarted.Broadcast(this);
		ReceiveRoomStarted();
		SpawnBoss();
		return;
	}
	for (int32 Index = 0; Index < FMath::Min(PlannedMinions, MaxAliveMinions); ++Index)
	{
		SpawnNextMinion();
	}
	UE_LOG(LogChaosImpact, Log, TEXT("Solo room %s started: %d minions (player at %s)"), *GetName(), Enemies.Num(),
		InPlayer ? *InPlayer->GetActorLocation().ToCompactString() : TEXT("-"));
	OnRoomStarted.Broadcast(this);
	ReceiveRoomStarted();
	if (PlannedMinions <= 0)
	{
		if (bSpawnBoss)
		{
			SpawnBoss();
		}
		else
		{
			FinishRoom();
		}
	}
}

void AChaosImpactSoloRoom::ResetRoom()
{
	ClearEnemies();
	GetWorldTimerManager().ClearTimer(WaveTimer);
	bReachedBoss = false;
	Defeated = 0;
	SpawnedMinions = 0;
	Phase = EChaosImpactSoloRoomPhase::Waiting;
	SetDoorsClosed(false);
	BindPlayer(nullptr);
	if (bBossWaitsFromStart && bSpawnBoss)
	{
		SpawnWaitingBoss();
	}
	UE_LOG(LogChaosImpact, Log, TEXT("Solo room %s reset"), *GetName());
	OnRoomReset.Broadcast(this);
	ReceiveRoomReset();
}

void AChaosImpactSoloRoom::SetDoorsClosed(const bool bClosed)
{
	if (HasActorBegunPlay() && bClosed != bDoorsClosed)
	{
		ChaosImpactSfx::PlayAt(this, bClosed ? EChaosImpactSfx::SoloDoorClose : EChaosImpactSfx::SoloDoorOpen, Door->GetComponentLocation());
	}
	bDoorsClosed = bClosed;
	Door->SetCollisionEnabled(bClosed ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	if (bClosed)
	{
		Door->SetVisibility(true);
	}
	for (AActor* Extra : DoorActors)
	{
		if (IsValid(Extra))
		{
			Extra->SetActorHiddenInGame(!bClosed);
			Extra->SetActorEnableCollision(bClosed);
		}
	}
}

int32 AChaosImpactSoloRoom::GetAliveEnemyCount() const
{
	int32 Alive = 0;
	for (const TWeakObjectPtr<AChaosImpactCharacter>& Enemy : Enemies)
	{
		Alive += Enemy.IsValid() && !Enemy->IsEliminated() ? 1 : 0;
	}
	return Alive;
}

void AChaosImpactSoloRoom::HandleEnemyDestroyed(AActor* DestroyedActor)
{
	Enemies.RemoveAll([DestroyedActor](const TWeakObjectPtr<AChaosImpactCharacter>& Enemy)
	{
		return !Enemy.IsValid() || Enemy.Get() == DestroyedActor;
	});
	if (bClearing)
	{
		return;
	}
	++Defeated;
	if (Phase == EChaosImpactSoloRoomPhase::Minions && Defeated < PlannedMinions && SpawnedMinions < PlannedMinions)
	{
		// The next of the wave steps in a moment later.
		GetWorldTimerManager().SetTimer(WaveTimer, FTimerDelegate::CreateWeakLambda(this, [this]() { SpawnNextMinion(); }), 0.7f, false);
	}
	if (Phase == EChaosImpactSoloRoomPhase::Minions && Defeated >= PlannedMinions)
	{
		if (bSpawnBoss)
		{
			SpawnBoss();
		}
		else
		{
			FinishRoom();
		}
	}
	else if (Phase == EChaosImpactSoloRoomPhase::Boss && (!Boss.IsValid() || Boss.Get() == DestroyedActor))
	{
		FinishRoom();
	}
}

void AChaosImpactSoloRoom::FinishRoom()
{
	Phase = EChaosImpactSoloRoomPhase::Cleared;
	SetDoorsClosed(false);
	BindPlayer(nullptr);
	ChaosImpactSfx::Play2D(this, EChaosImpactSfx::SoloClear);
	UE_LOG(LogChaosImpact, Log, TEXT("Solo room %s cleared"), *GetName());
	OnRoomCleared.Broadcast(this);
	ReceiveRoomCleared();
	// The stage is cleared once every room in its level is: solo mode saves it and goes back to the world map.
	bool bAllCleared = true;
	for (TActorIterator<AChaosImpactSoloRoom> It(GetWorld()); It; ++It)
	{
		bAllCleared &= It->GetPhase() == EChaosImpactSoloRoomPhase::Cleared;
	}
	if (AChaosImpactPlayerController* PC = bAllCleared ? Cast<AChaosImpactPlayerController>(GetWorld()->GetFirstPlayerController()) : nullptr)
	{
		PC->NotifySoloStageCleared();
	}
}

void AChaosImpactSoloRoom::HandlePlayerEliminated(AChaosImpactCharacter* Character)
{
	if (Character != Player.Get()
		|| (Phase != EChaosImpactSoloRoomPhase::Minions && Phase != EChaosImpactSoloRoomPhase::Boss))
	{
		return;
	}
	// Back at the entrance after the knockout, and the room as it was before going in (but past the minions if the
	// boss was reached).
	Character->SetRespawnPoint(RespawnPoint->GetComponentLocation(), RespawnPoint->GetComponentRotation());
	const bool bCheckpoint = bReachedBoss;
	ResetRoom();
	bReachedBoss = bCheckpoint;
}

AChaosImpactCharacter* AChaosImpactSoloRoom::SpawnEnemy(const TSoftClassPtr<AChaosImpactCharacter>& Class, const TCHAR* FallbackPath,
	const FVector& Location, const EChaosImpactCPULevel Level, const bool bBossEnemy)
{
	UWorld* World = GetWorld();
	UClass* EnemyClass = Class.IsNull() ? LoadClass<AChaosImpactCharacter>(nullptr, FallbackPath) : Class.LoadSynchronous();
	if (!World || !EnemyClass)
	{
		UE_LOG(LogChaosImpact, Warning, TEXT("Solo room %s: no enemy class to spawn"), *GetName());
		return nullptr;
	}
	// Facing the doorway it was entered through.
	const FVector Toward = (RespawnPoint->GetComponentLocation() - Location).GetSafeNormal2D();
	const FTransform Where(FRotator(0.0f, Toward.Rotation().Yaw, 0.0f), Location);
	AChaosImpactCharacter* Enemy = World->SpawnActorDeferred<AChaosImpactCharacter>(EnemyClass, Where, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (!Enemy)
	{
		return nullptr;
	}
	// Knocked out for good (it is then destroyed: that is what the room counts).
	Enemy->bRespawnAfterElimination = false;
	if (bBossEnemy) { Enemy->Tags.AddUnique(TEXT("CI.SoloBoss")); }
	Enemy->CPULevel = Level;
	Enemy->FinishSpawning(Where);
	// What Spawn AI From Class does: a CPU to play it.
	if (!Enemy->GetController())
	{
		Enemy->SpawnDefaultController();
	}
	Enemy->SetCPULevel(Level);
	Enemy->OnDestroyed.AddDynamic(this, &AChaosImpactSoloRoom::HandleEnemyDestroyed);
	return Enemy;
}

FVector AChaosImpactSoloRoom::PickSpawnLocation(const int32 Index) const
{
	const TArray<AActor*> Points = SpawnPoints.FilterByPredicate([](const AActor* Point) { return IsValid(Point); });
	if (!Points.IsEmpty())
	{
		return OnFloor(Points[Index % Points.Num()]->GetActorLocation());
	}
	// Spread round the far side of the room (away from the doorway), in a fan.
	const FVector Center = RoomArea->GetComponentLocation();
	const FVector Extent = RoomArea->GetScaledBoxExtent();
	const FVector Inward = (Center - RespawnPoint->GetComponentLocation()).GetSafeNormal2D();
	const FVector Across = FVector::CrossProduct(FVector::UpVector, Inward);
	const float Depth = FMath::Min(Extent.X, Extent.Y);
	const float Spread = FMath::Max(PlannedMinions - 1, 1);
	const float Side = (Index / Spread - 0.5f) * 2.0f;
	const FVector Point = Center + Inward * Depth * (0.25f + 0.25f * (Index % 2)) + Across * Side * Depth * 0.7f;
	return OnFloor(Point);
}

FVector AChaosImpactSoloRoom::OnFloor(const FVector& Point) const
{
	FHitResult Floor;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ChaosImpactSoloRoomFloor), false, this);
	const FVector Top(Point.X, Point.Y, RoomArea->GetComponentLocation().Z + RoomArea->GetScaledBoxExtent().Z + 300.0f);
	if (GetWorld() && GetWorld()->LineTraceSingleByChannel(Floor, Top, Top - FVector(0.0f, 0.0f, 3000.0f), ECC_WorldStatic, Params))
	{
		// A character's middle stands about a metre up.
		return Floor.ImpactPoint + FVector(0.0f, 0.0f, 100.0f);
	}
	return Point;
}

void AChaosImpactSoloRoom::SpawnWaitingBoss()
{
	if (Boss.IsValid() || !GetWorld())
	{
		return;
	}
	const FVector Where = IsValid(BossSpawnPoint) ? OnFloor(BossSpawnPoint->GetActorLocation()) : OnFloor(RoomArea->GetComponentLocation());
	AChaosImpactCharacter* NewBoss = SpawnEnemy(BossClass, SoloRoomBossPath, Where, PlannedBossLevel, true);
	Boss = NewBoss;
	if (NewBoss)
	{
		Enemies.Add(NewBoss);
		if (AChaosImpactSimaeBoss* Simae = Cast<AChaosImpactSimaeBoss>(NewBoss))
		{
			Simae->SetDormant(true);
		}
	}
	UE_LOG(LogChaosImpact, Log, TEXT("Solo room %s: boss %s waiting"), *GetName(), *GetNameSafe(NewBoss));
}

void AChaosImpactSoloRoom::SpawnBoss()
{
	if (AChaosImpactCharacter* Waiting = Boss.Get(); Waiting && bBossWaitsFromStart)
	{
		// It was there all along: it wakes and the fight is on.
		Phase = EChaosImpactSoloRoomPhase::Boss;
		if (AChaosImpactSimaeBoss* Simae = Cast<AChaosImpactSimaeBoss>(Waiting))
		{
			Simae->SetDormant(false);
		}
		UE_LOG(LogChaosImpact, Log, TEXT("Solo room %s: boss %s wakes"), *GetName(), *GetNameSafe(Waiting));
		ChaosImpactSfx::Play2D(this, EChaosImpactSfx::SoloBoss);
		OnBossSpawned.Broadcast(this);
		ReceiveBossSpawned(Waiting);
		return;
	}
	Phase = EChaosImpactSoloRoomPhase::Boss;
	bReachedBoss = true;
	const FVector Where = IsValid(BossSpawnPoint) ? OnFloor(BossSpawnPoint->GetActorLocation()) : OnFloor(RoomArea->GetComponentLocation());
	AChaosImpactCharacter* NewBoss = SpawnEnemy(BossClass, SoloRoomBossPath, Where, PlannedBossLevel, true);
	Boss = NewBoss;
	if (NewBoss)
	{
		Enemies.Add(NewBoss);
	}
	UE_LOG(LogChaosImpact, Log, TEXT("Solo room %s: boss %s"), *GetName(), *GetNameSafe(NewBoss));
	if (NewBoss)
	{
		ChaosImpactSfx::Play2D(this, EChaosImpactSfx::SoloBoss);
	}
	OnBossSpawned.Broadcast(this);
	ReceiveBossSpawned(NewBoss);
	if (!NewBoss)
	{
		// Nothing to fight: the room is done.
		Phase = EChaosImpactSoloRoomPhase::Cleared;
		SetDoorsClosed(false);
		OnRoomCleared.Broadcast(this);
		ReceiveRoomCleared();
	}
}

void AChaosImpactSoloRoom::ClearEnemies(const bool bKeepBoss)
{
	// Gone at once, not knocked out: none of this counts.
	bClearing = true;
	const TWeakObjectPtr<AChaosImpactCharacter> Kept = bKeepBoss ? Boss : nullptr;
	const TArray<TWeakObjectPtr<AChaosImpactCharacter>> Leaving = Enemies.FilterByPredicate(
		[&Kept](const TWeakObjectPtr<AChaosImpactCharacter>& Enemy) { return !Kept.IsValid() || Enemy != Kept; });
	for (const TWeakObjectPtr<AChaosImpactCharacter>& Enemy : Leaving)
	{
		if (AChaosImpactCharacter* Character = Enemy.Get())
		{
			if (AController* Controller = Character->GetController())
			{
				Controller->UnPossess();
				Controller->Destroy();
			}
			Character->Destroy();
		}
	}
	Enemies.Reset();
	Boss = Kept;
	if (Kept.IsValid())
	{
		Enemies.Add(Kept);
	}
	bClearing = false;
}

void AChaosImpactSoloRoom::BindPlayer(AChaosImpactCharacter* NewPlayer)
{
	if (AChaosImpactCharacter* Old = Player.Get())
	{
		Old->OnEliminated.RemoveDynamic(this, &AChaosImpactSoloRoom::HandlePlayerEliminated);
	}
	Player = NewPlayer;
	if (NewPlayer)
	{
		NewPlayer->OnEliminated.AddUniqueDynamic(this, &AChaosImpactSoloRoom::HandlePlayerEliminated);
	}
}
