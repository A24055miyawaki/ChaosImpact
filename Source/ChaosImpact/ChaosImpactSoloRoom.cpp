#include "ChaosImpactSoloRoom.h"
#include "ChaosImpactSfx.h"

#include "ChaosImpact.h"
#include "ChaosImpactCharacter.h"
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
		const float Eased = FMath::InterpEaseOut(0.0f, 1.0f, DoorRaise, 2.0f);
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
	ClearEnemies();
	Defeated = 0;
	Phase = EChaosImpactSoloRoomPhase::Minions;
	SetDoorsClosed(true);
	for (int32 Index = 0; Index < MinionCount; ++Index)
	{
		if (AChaosImpactCharacter* Minion = SpawnEnemy(MinionClass, SoloRoomMinionPath, PickSpawnLocation(Index), MinionLevel))
		{
			Enemies.Add(Minion);
		}
	}
	UE_LOG(LogChaosImpact, Log, TEXT("Solo room %s started: %d minions (player at %s)"), *GetName(), Enemies.Num(),
		InPlayer ? *InPlayer->GetActorLocation().ToCompactString() : TEXT("-"));
	OnRoomStarted.Broadcast(this);
	ReceiveRoomStarted();
	if (MinionCount <= 0)
	{
		SpawnBoss();
	}
}

void AChaosImpactSoloRoom::ResetRoom()
{
	ClearEnemies();
	Defeated = 0;
	Phase = EChaosImpactSoloRoomPhase::Waiting;
	SetDoorsClosed(false);
	BindPlayer(nullptr);
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
	if (Phase == EChaosImpactSoloRoomPhase::Minions && Defeated >= MinionCount)
	{
		SpawnBoss();
	}
	else if (Phase == EChaosImpactSoloRoomPhase::Boss && (!Boss.IsValid() || Boss.Get() == DestroyedActor))
	{
		Phase = EChaosImpactSoloRoomPhase::Cleared;
		SetDoorsClosed(false);
		BindPlayer(nullptr);
		ChaosImpactSfx::Play2D(this, EChaosImpactSfx::SoloClear);
		UE_LOG(LogChaosImpact, Log, TEXT("Solo room %s cleared"), *GetName());
		OnRoomCleared.Broadcast(this);
		ReceiveRoomCleared();
	}
}

void AChaosImpactSoloRoom::HandlePlayerEliminated(AChaosImpactCharacter* Character)
{
	if (Character != Player.Get()
		|| (Phase != EChaosImpactSoloRoomPhase::Minions && Phase != EChaosImpactSoloRoomPhase::Boss))
	{
		return;
	}
	// Back at the entrance after the knockout, and the room as it was before going in.
	Character->SetRespawnPoint(RespawnPoint->GetComponentLocation(), RespawnPoint->GetComponentRotation());
	ResetRoom();
}

AChaosImpactCharacter* AChaosImpactSoloRoom::SpawnEnemy(const TSoftClassPtr<AChaosImpactCharacter>& Class, const TCHAR* FallbackPath,
	const FVector& Location, const EChaosImpactCPULevel Level)
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
	const float Spread = FMath::Max(MinionCount - 1, 1);
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

void AChaosImpactSoloRoom::SpawnBoss()
{
	Phase = EChaosImpactSoloRoomPhase::Boss;
	const FVector Where = IsValid(BossSpawnPoint) ? OnFloor(BossSpawnPoint->GetActorLocation()) : OnFloor(RoomArea->GetComponentLocation());
	AChaosImpactCharacter* NewBoss = SpawnEnemy(BossClass, SoloRoomBossPath, Where, BossLevel);
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

void AChaosImpactSoloRoom::ClearEnemies()
{
	// Gone at once, not knocked out: none of this counts.
	bClearing = true;
	const TArray<TWeakObjectPtr<AChaosImpactCharacter>> Leaving = Enemies;
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
	Boss.Reset();
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
