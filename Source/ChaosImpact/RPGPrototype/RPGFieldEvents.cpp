#include "RPGFieldEvents.h"
#include "RPGPrototype.h"
#include "RPGAdventure.h"
#include "RPGPrototypeVFX.h"
#include "RPGTreasureChest.h"
#include "RPGWorldLayout.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"

// Field events (2026-11): a falling star, a caravan, a storm. One at a time, every few minutes.

namespace
{
const TCHAR* DesertPlaces = TEXT("砂丘|砂漠のオアシス|砂に眠る神殿|巡礼の石段|砂岩の隠し部屋");
const TCHAR* SnowPlaces = TEXT("雪原|白霧の峠");
bool Matches(const FString& Place, const FString& Keywords)
{
	TArray<FString> Words; Keywords.ParseIntoArray(Words, TEXT("|"));
	for (const FString& Word : Words) { if (Place.Contains(Word)) { return true; } }
	return false;
}
float ServerNow(const UWorld* World)
{
	const AGameStateBase* State = World ? World->GetGameState() : nullptr;
	return State ? static_cast<float>(State->GetServerWorldTimeSeconds()) : (World ? World->GetTimeSeconds() : 0.0f);
}
}

ARPGFieldEvents::ARPGFieldEvents()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("EventRoot")));
}

void ARPGFieldEvents::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ARPGFieldEvents, Kind);
	DOREPLIFETIME(ARPGFieldEvents, EventId);
	DOREPLIFETIME(ARPGFieldEvents, At);
	DOREPLIFETIME(ARPGFieldEvents, Region);
	DOREPLIFETIME(ARPGFieldEvents, EndsAt);
}

ARPGFieldEvents* ARPGFieldEvents::Find(const UWorld* World)
{
	if (!World) { return nullptr; }
	for (TActorIterator<ARPGFieldEvents> It(const_cast<UWorld*>(World)); It; ++It) { return *It; }
	return nullptr;
}

ARPGFieldEvents* ARPGFieldEvents::Ensure(UWorld* World)
{
	if (!World || World->GetNetMode() == NM_Client) { return Find(World); }
	if (ARPGFieldEvents* Existing = Find(World)) { return Existing; }
	FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Events = World->SpawnActor<ARPGFieldEvents>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
	if (Events) { Events->NextEventAt = World->GetTimeSeconds() + FMath::FRandRange(150.0f, 240.0f); }
	return Events;
}

FString ARPGFieldEvents::GetTitle() const
{
	switch (Kind)
	{
	case Meteor: return TEXT("流れ星");
	case Caravan: return TEXT("行商の隊商");
	case Storm: return Region.Contains(TEXT("雪原")) ? TEXT("吹雪") : TEXT("砂嵐");
	default: return FString();
	}
}

float ARPGFieldEvents::GetSecondsLeft() const { return Kind == None ? 0.0f : FMath::Max(0.0f, EndsAt - ServerNow(GetWorld())); }

bool ARPGFieldEvents::IsStormAt(const UWorld* World, const FVector& Where)
{
	const ARPGFieldEvents* Events = Find(World);
	const ARPGAdventureWorld* Adventure = ARPGAdventureWorld::Find(World);
	return Events && Adventure && Events->Kind == Storm && Matches(Adventure->RegionName(Where), Events->Region);
}

bool ARPGFieldEvents::IsCaravan(const AActor* Merchant) { return Merchant && Merchant->Tags.Contains(TEXT("Caravan")); }

bool ARPGFieldEvents::PickPlace(uint8 InKind, FVector& Out, FString& OutRegion) const
{
	UWorld* World = GetWorld();
	const ARPGAdventureWorld* Adventure = ARPGAdventureWorld::Find(World);
	if (!Adventure) { return false; }
	TArray<ARPGPrototypeCharacter*> Heroes;
	for (TActorIterator<ARPGPrototypeCharacter> It(World); It; ++It) { if (!It->IsGrunt() && !It->IsEliminated() && !It->IsInTitle()) { Heroes.Add(*It); } }
	if (Heroes.IsEmpty()) { return false; }
	const FTransform Map = Adventure->GetActorTransform();
	if (InKind == Storm)
	{
		// Where a hero is: the snow or the desert.
		for (const ARPGPrototypeCharacter* Hero : Heroes)
		{
			const FString Place = Adventure->RegionName(Hero->GetActorLocation());
			if (Matches(Place, SnowPlaces)) { OutRegion = SnowPlaces; Out = Map.TransformPosition(RPGWorld::AreaPoint(TEXT("SnowWood"))); return true; }
			if (Matches(Place, DesertPlaces)) { OutRegion = DesertPlaces; Out = Map.TransformPosition(RPGWorld::AreaPoint(TEXT("Dunes"))); return true; }
		}
		return false;
	}
	const FVector Near = Map.InverseTransformPosition(Heroes[FMath::RandRange(0, Heroes.Num() - 1)]->GetActorLocation());
	// Open ground round a point: straight down to the terrain everywhere (no rock, tree or wall on it).
	const auto IsClear = [&](const FVector& Spot, float Radius)
	{
		FCollisionQueryParams Query(SCENE_QUERY_STAT(RPGFieldEventClear), false);
		// Nothing standing in it (a cactus, a post), and the ground in it near flat.
		const float Middle = RPGWorld::Height(Spot.X, Spot.Y);
		if (World->OverlapAnyTestByObjectType(Map.TransformPosition(FVector(Spot.X, Spot.Y, Middle + 300)), Map.GetRotation(), FCollisionObjectQueryParams(ECC_WorldStatic),
			FCollisionShape::MakeBox(FVector(Radius, Radius, 170)), Query)) { return false; }
		for (int32 I = 0; I <= 8; ++I)
		{
			const FVector2D P = FVector2D(Spot.X, Spot.Y) + (I == 0 ? FVector2D::ZeroVector : FVector2D(FMath::Cos(I * UE_PI / 4), FMath::Sin(I * UE_PI / 4)) * Radius);
			const float Floor = RPGWorld::Height(P.X, P.Y);
			FHitResult Hit;
			const FVector Top = Map.TransformPosition(FVector(P.X, P.Y, Floor + 1500)), Bottom = Map.TransformPosition(FVector(P.X, P.Y, Floor - 400));
			if (!World->LineTraceSingleByObjectType(Hit, Top, Bottom, FCollisionObjectQueryParams(ECC_WorldStatic), Query)) { return false; }
			if (FMath::Abs(Map.InverseTransformPosition(Hit.ImpactPoint).Z - Floor) > 100) { return false; }
		}
		return true;
	};
	if (InKind == Caravan)
	{
		// Beside a road a little way off.
		TArray<FVector> Spots;
		for (const RPGWorld::FRoad& Road : RPGWorld::Roads())
		{
			for (int32 I = 0; I + 1 < Road.Points.Num(); ++I)
			{
				const FVector Mid = (Road.Points[I] + Road.Points[I + 1]) * 0.5f;
				const float D = FVector::Dist2D(Mid, Near);
				if (D < 1500 || D > 4500) { continue; }
				const FVector Side = FVector::CrossProduct(FVector::UpVector, (Road.Points[I + 1] - Road.Points[I]).GetSafeNormal2D()) * (Road.Width * 0.5f + 160);
				const FVector At2 = Mid + Side;
				if (RPGWorld::Sample(At2.X, At2.Y).bLand && !Adventure->IsInColony(Map.TransformPosition(At2)) && IsClear(At2 + FVector(110, 0, 0), 260)) { Spots.Add(At2); }
			}
		}
		if (Spots.IsEmpty()) { return false; }
		const FVector Pick = Spots[FMath::RandRange(0, Spots.Num() - 1)];
		Out = Map.TransformPosition(FVector(Pick.X, Pick.Y, RPGWorld::Height(Pick.X, Pick.Y)));
		OutRegion = Adventure->RegionName(Out);
		return true;
	}
	// A falling star: open ground a little way off.
	for (int32 Try = 0; Try < 90; ++Try)
	{
		const FVector P = Near + FRotator(0, FMath::FRandRange(0.0f, 360.0f), 0).Vector() * FMath::FRandRange(1500.0f, 4000.0f);
		const RPGWorld::FSample S = RPGWorld::Sample(P.X, P.Y);
		if (!S.bLand || S.Inside < 260 || RPGWorld::IsCave(P.X, P.Y) || !IsClear(P, 260)) { continue; }
		Out = Map.TransformPosition(FVector(P.X, P.Y, S.Height));
		if (Adventure->IsInColony(Out)) { continue; }
		OutRegion = Adventure->RegionName(Out);
		return true;
	}
	return false;
}

void ARPGFieldEvents::StartEvent(uint8 InKind)
{
	if (!HasAuthority()) { return; }
	FVector Where; FString Place;
	if (!PickPlace(InKind, Where, Place))
	{
		UE_LOG(LogTemp, Log, TEXT("RPG field event %d: no place for it near the heroes"), InKind);
		if (InKind == Meteor || !PickPlace(Meteor, Where, Place)) { NextEventAt = GetWorld()->GetTimeSeconds() + 60; return; }
		InKind = Meteor;
	}
	Kind = InKind; At = Where; Region = Place; ++EventId;
	EndsAt = ServerNow(GetWorld()) + (Kind == Meteor ? 300.0f : Kind == Caravan ? 240.0f : 150.0f);
	Present();
	// Everyone hears of it.
	const ARPGAdventureWorld* Adventure = ARPGAdventureWorld::Find(GetWorld());
	const FString Where2 = Adventure ? Adventure->RegionName(At) : FString();
	const FString Text = Kind == Meteor ? FString::Printf(TEXT("★ 流れ星が「%s」に落ちた！ 地図の★へ"), *Where2)
		: Kind == Caravan ? FString::Printf(TEXT("★ 行商の隊商が「%s」に来ている（工房が3割引き）"), *Where2)
		: FString::Printf(TEXT("★ %sが吹き荒れている！ 敵が強くなるが、良い品を落としやすい"), *GetTitle());
	for (TActorIterator<ARPGPrototypeCharacter> It(GetWorld()); It; ++It) { if (!It->IsGrunt()) { It->ShowNotice(Text, 6); } }
	UE_LOG(LogTemp, Log, TEXT("RPG field event %d: %s at %s"), Kind, *GetTitle(), *Where2);
}

void ARPGFieldEvents::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Clock += DeltaSeconds;
	if (Presented != EventId) { Present(); }
	if (Glow) { Glow->SetIntensity((Kind == Meteor ? 9000.0f : 3000.0f) * (0.8f + 0.2f * FMath::Sin(Clock * 3))); }
	if (!HasAuthority()) { return; }
	const double Now = GetWorld()->GetTimeSeconds();
	if (Kind != None && ServerNow(GetWorld()) >= EndsAt)
	{
		Kind = None; ++EventId; Present();
		NextEventAt = Now + FMath::FRandRange(240.0f, 420.0f);
	}
	else if (Kind == None && Now >= NextEventAt)
	{
		const float Roll = FMath::FRand();
		StartEvent(Roll < 0.45f ? Meteor : Roll < 0.75f ? Caravan : Storm);
		if (Kind == None) { NextEventAt = Now + 60; }
	}
}

void ARPGFieldEvents::OnRep_Event() { Present(); }

void ARPGFieldEvents::Clear()
{
	for (UStaticMeshComponent* Piece : Pieces) { if (Piece) { Piece->DestroyComponent(); } }
	Pieces.Reset();
	if (Glow) { Glow->DestroyComponent(); Glow = nullptr; }
	if (ARPGTreasureChest* Old = Chest.Get()) { Old->Destroy(); }
	if (ARPGPrototypeMerchant* Old = Merchant.Get()) { Old->Destroy(); }
	Chest.Reset(); Merchant.Reset();
}

void ARPGFieldEvents::Present()
{
	// Every machine builds the event's things itself (they are not sent).
	Clear();
	Presented = EventId;
	UWorld* World = GetWorld();
	if (!World || Kind == None) { return; }
	// On the ground as this machine's terrain has it.
	FVector Ground = At;
	{
		FHitResult Hit; FCollisionQueryParams Query(SCENE_QUERY_STAT(RPGFieldEventFloor), false, this);
		if (World->LineTraceSingleByObjectType(Hit, At + FVector(0, 0, 2500), At - FVector(0, 0, 3000), FCollisionObjectQueryParams(ECC_WorldStatic), Query)) { Ground = Hit.ImpactPoint; }
	}
	SetActorLocation(Ground);
	const auto Part = [&](const TCHAR* Mesh, const TCHAR* Material, const FVector& Offset, const FVector& Size, const FRotator& Rotation)
	{
		const FString MeshPath = FString(Mesh).StartsWith(TEXT("Kit")) ? FString::Printf(TEXT("/Game/ChaosImpact/RPGPrototype/Adventure/Meshes/SM_%s.SM_%s"), Mesh, Mesh) : FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), Mesh, Mesh);
		auto* Piece = NewObject<UStaticMeshComponent>(this);
		Piece->SetupAttachment(GetRootComponent());
		Piece->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, *MeshPath));
		if (UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("/Game/ChaosImpact/RPGPrototype/Adventure/Materials/M_%s.M_%s"), Material, Material))) { Piece->SetMaterial(0, M); }
		Piece->SetRelativeLocation(Offset); Piece->SetRelativeRotation(Rotation); Piece->SetRelativeScale3D(Size / 100);
		Piece->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Piece->RegisterComponent();
		Pieces.Add(Piece);
	};
	// A storm is only its land's weather (the HUD's edges); the star and the caravan glow where they are.
	if (Kind != Storm)
	{
		Glow = NewObject<UPointLightComponent>(this);
		Glow->SetupAttachment(GetRootComponent()); Glow->SetRelativeLocation(FVector(0, 0, 220)); Glow->SetCastShadows(false);
		Glow->SetLightColor(Kind == Meteor ? FLinearColor(0.6f, 0.75f, 1.0f) : FLinearColor(1.0f, 0.7f, 0.4f)); Glow->SetAttenuationRadius(1400);
		Glow->RegisterComponent();
	}
	FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (Kind == Meteor)
	{
		// A scorched crater with glowing star-stones, and its chest (each player's own to open).
		Part(TEXT("KitCylinder"), TEXT("StoneDark"), FVector(0, 0, 2), FVector(520, 520, 6), FRotator::ZeroRotator);
		for (int32 I = 0; I < 7; ++I)
		{
			const float A = I * UE_TWO_PI / 7;
			Part(TEXT("KitGem"), TEXT("Crystal"), FVector(FMath::Cos(A) * 200, FMath::Sin(A) * 200, 40), FVector(60, 60, 110), FRotator(25, FMath::RadiansToDegrees(A), 0));
		}
		Part(TEXT("KitGem"), TEXT("GlowWarm"), FVector(0, 0, 10), FVector(110, 110, 170), FRotator(0, 20, 0));
		const FTransform Where(FRotator::ZeroRotator, Ground + FVector(150, 0, 2));
		auto* Box = World->SpawnActorDeferred<ARPGTreasureChest>(ARPGTreasureChest::StaticClass(), Where, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Box)
		{
			Box->ChestId = FName(*FString::Printf(TEXT("FieldStar_%d_%d"), EventId, FMath::RoundToInt(At.X + At.Y)));
			Box->DisplayName = TEXT("流れ星の宝箱"); Box->Style = ERPGChestStyle::Gold;
			Box->RubyMin = 200; Box->RubyMax = 420; Box->RareChance = .3f; Box->UniqueChance = .5f; Box->LegendChance = .15f;
			Box->FinishSpawning(Where);
			Chest = Box;
		}
		if (HasAuthority()) { ARPGPrototypeVFX::PlayImpact(World, EChaosImpactBallType::Nova, Ground + FVector(0, 0, 60), 3.0f, true); }
	}
	else if (Kind == Caravan)
	{
		// Its workshop in front, the covered wagon behind it (cheaper while it stays).
		Part(TEXT("Cube"), TEXT("Planks"), FVector(240, 0, 72), FVector(300, 170, 30), FRotator::ZeroRotator);
		for (const float Side : {-1.0f, 1.0f})
		{
			Part(TEXT("Cube"), TEXT("Sail"), FVector(240, Side * 46, 150), FVector(290, 112, 6), FRotator(0, 0, Side * 38));
			for (const float X : {150.0f, 330.0f}) { Part(TEXT("Cylinder"), TEXT("Timber"), FVector(X, Side * 94, 44), FVector(84, 84, 12), FRotator(0, 0, 90)); }
		}
		Part(TEXT("Cylinder"), TEXT("Timber"), FVector(110, 70, 150), FVector(8, 8, 220), FRotator::ZeroRotator);
		Part(TEXT("Cube"), TEXT("Banner"), FVector(110, 104, 228), FVector(3, 64, 42), FRotator::ZeroRotator);
		auto* Shop = World->SpawnActor<ARPGPrototypeMerchant>(Ground + FVector(-30, 0, 2), FRotator(0, 180, 0), Spawn);
		if (Shop) { Shop->Tags.Add(TEXT("Caravan")); Merchant = Shop; }
	}
	// Each player here hears of it once (a lesson the first time).
	for (TActorIterator<ARPGPrototypeCharacter> It(World); It; ++It) { if (It->IsLocallyControlled() && !It->IsGrunt()) { It->ShowLesson(TEXT("Events")); } }
}
