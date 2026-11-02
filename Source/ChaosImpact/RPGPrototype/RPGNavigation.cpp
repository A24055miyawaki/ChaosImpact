#include "RPGNavigation.h"
#include "RPGPrototype.h"
#include "RPGAdventure.h"
#include "RPGFieldEvents.h"
#include "RPGAbilityGates.h"
#include "RPGTuning.h"
#include "RPGQuests.h"
#include "RPGSnowField.h"
#include "RPGTreasureChest.h"
#include "RPGWorldLayout.h"
#include "RPGPrototypeUIKit.h"
#include "ChaosImpactBallTypes.h"
#include "ChaosImpactCharacterRoster.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

// The way to a quest's goal, the light line on the ground, the minimap and the whole map (2026-11-02).

namespace RPGUI = RPGPrototypeUI;

namespace
{
using ChaosImpactPaint::ETextAlign;
using ChaosImpactPaint::FPainter;
using ChaosImpactPaint::Navy;
using ChaosImpactPaint::WithAlpha;

// ---------------------------------------------------------------- the road graph

/** Points along every road (a few steps apart), the middle of every area, the snowfield's path and the gates. */
struct FNavGraph
{
	TArray<FVector> Nodes;
	TArray<TArray<TPair<int32, float>>> Links;
	TWeakObjectPtr<const UWorld> World;
	int32 Extras = -1;
	/** Where a fixed goal (a chest, a group) joins the graph, worked out once. */
	TMap<FIntVector, TArray<TPair<int32, float>>> GoalJoins;

	int32 Add(const FVector& At) { Links.AddDefaulted(); return Nodes.Add(At); }
	void Link(int32 A, int32 B, float Cost = -1)
	{
		if (A == B || !Nodes.IsValidIndex(A) || !Nodes.IsValidIndex(B)) { return; }
		for (const auto& Existing : Links[A]) { if (Existing.Key == B) { return; } }
		const float C = Cost >= 0 ? Cost : static_cast<float>(FVector::Dist(Nodes[A], Nodes[B]));
		Links[A].Emplace(B, C);
		Links[B].Emplace(A, C);
	}
};

FTransform MapTransform(const UWorld* World)
{
	const auto* Adventure = ARPGAdventureWorld::Find(World);
	return Adventure ? Adventure->GetActorTransform() : FTransform::Identity;
}

/** A straight walk on the map's ground, no gorge, cliff or water in between. */
bool Walkable(const UWorld* World, const FTransform& Map, const FVector& From, const FVector& To)
{
	const FVector A = Map.InverseTransformPosition(From), B = Map.InverseTransformPosition(To);
	const int32 Steps = FMath::Max(1, FMath::CeilToInt(FVector::Dist2D(A, B) / 250.0));
	float Last = RPGWorld::Height(A.X, A.Y);
	for (int32 Step = 1; Step <= Steps; ++Step)
	{
		const FVector At = FMath::Lerp(A, B, static_cast<float>(Step) / Steps);
		const RPGWorld::FSample Ground = RPGWorld::Sample(At.X, At.Y);
		if (!Ground.bLand || FMath::Abs(Ground.Height - Last) > 170) { return false; }
		Last = Ground.Height;
	}
	return true;
}

/** The nodes a point can walk straight to (the nearest few at about its height; else simply the nearest). */
TArray<TPair<int32, float>> Join(const UWorld* World, const FNavGraph& Graph, const FVector& At, bool bCheckGround)
{
	const FTransform Map = MapTransform(World);
	struct FNear { int32 Node; float Distance; float Score; };
	TArray<FNear> Near;
	int32 Closest = INDEX_NONE; float ClosestScore = MAX_flt;
	for (int32 Node = 0; Node < Graph.Nodes.Num(); ++Node)
	{
		const float Distance = FVector::Dist2D(Graph.Nodes[Node], At);
		const float Rise = FMath::Abs(Graph.Nodes[Node].Z - At.Z);
		const float Score = Distance + Rise * 4;
		if (Score < ClosestScore) { ClosestScore = Score; Closest = Node; }
		if (Distance < 3800 && Rise < 330) { Near.Add({Node, Distance, Score}); }
	}
	Near.Sort([](const FNear& A, const FNear& B) { return A.Score < B.Score; });
	TArray<TPair<int32, float>> Out;
	int32 Tried = 0;
	for (const FNear& Candidate : Near)
	{
		if (Out.Num() >= 3 || Tried >= 9) { break; }
		++Tried;
		if (bCheckGround && Candidate.Distance > 120 && !Walkable(World, Map, At, Graph.Nodes[Candidate.Node])) { continue; }
		Out.Emplace(Candidate.Node, Candidate.Distance);
	}
	if (Out.IsEmpty() && Closest != INDEX_NONE) { Out.Emplace(Closest, ClosestScore); }
	return Out;
}

void BuildGraph(FNavGraph& Graph, const UWorld* World, int32 Extras)
{
	Graph = FNavGraph();
	Graph.World = World;
	Graph.Extras = Extras;
	const FTransform Map = MapTransform(World);
	// Roads, a node every 7 m or so, each joined to the next.
	TArray<TArray<int32>> RoadNodes;
	for (const RPGWorld::FRoad& Road : RPGWorld::Roads())
	{
		TArray<int32>& Mine = RoadNodes.AddDefaulted_GetRef();
		for (int32 Index = 0; Index < Road.Points.Num(); ++Index)
		{
			if (Index > 0)
			{
				const FVector A = Road.Points[Index - 1], B = Road.Points[Index];
				const int32 Steps = FMath::Max(1, FMath::CeilToInt(FVector::Dist2D(A, B) / 700.0));
				for (int32 Step = 1; Step < Steps; ++Step) { Mine.Add(Graph.Add(Map.TransformPosition(FMath::Lerp(A, B, static_cast<float>(Step) / Steps)))); }
			}
			Mine.Add(Graph.Add(Map.TransformPosition(Road.Points[Index])));
		}
		for (int32 Index = 1; Index < Mine.Num(); ++Index) { Graph.Link(Mine[Index - 1], Mine[Index]); }
	}
	const int32 RoadNodeCount = Graph.Nodes.Num();
	// A road's ends lie on another road (or in an area): joined to the nearest node of every road they touch.
	for (int32 RoadIndex = 0; RoadIndex < RoadNodes.Num(); ++RoadIndex)
	{
		if (RoadNodes[RoadIndex].IsEmpty()) { continue; }
		for (const int32 End : {RoadNodes[RoadIndex][0], RoadNodes[RoadIndex].Last()})
		{
			for (int32 Other = 0; Other < RoadNodes.Num(); ++Other)
			{
				if (Other == RoadIndex) { continue; }
				int32 Best = INDEX_NONE; float BestDistance = 900;
				for (const int32 Node : RoadNodes[Other])
				{
					const float Distance = FVector::Dist2D(Graph.Nodes[Node], Graph.Nodes[End]);
					if (Distance < BestDistance && FMath::Abs(Graph.Nodes[Node].Z - Graph.Nodes[End].Z) < 300) { BestDistance = Distance; Best = Node; }
				}
				Graph.Link(End, Best);
			}
		}
	}
	// Areas: their middle, joined to the road nodes inside them.
	for (const RPGWorld::FArea& Area : RPGWorld::Areas())
	{
		const int32 Middle = Graph.Add(Map.TransformPosition(Area.At));
		for (int32 Node = 0; Node < RoadNodeCount; ++Node)
		{
			const FVector Local = Map.InverseTransformPosition(Graph.Nodes[Node]);
			const float Inside = FMath::Square((Local.X - Area.At.X) / Area.Radius.X) + FMath::Square((Local.Y - Area.At.Y) / Area.Radius.Y);
			if (Inside < 1.1f && FMath::Abs(Local.Z - Area.At.Z) < 300) { Graph.Link(Middle, Node); }
		}
	}
	// Gates: a short step from one end to the other.
	for (TActorIterator<ARPGWarpGate> It(const_cast<UWorld*>(World)); It; ++It)
	{
		const int32 Gate = Graph.Add(It->GetActorLocation());
		const int32 Far = Graph.Add(It->Destination);
		Graph.Link(Gate, Far, 600);
		for (const int32 End : {Gate, Far})
		{
			for (const auto& Joined : Join(World, Graph, Graph.Nodes[End], false)) { if (Joined.Key != Gate && Joined.Key != Far) { Graph.Link(End, Joined.Key, Joined.Value); } }
		}
	}
	// Leaps across gorges (a strong dash from the pad): the only way to some secret places. Each end joins the ground
	// it stands on, so no way walks over the gorge itself.
	for (TActorIterator<ARPGLeapPoint> It(const_cast<UWorld*>(World)); It; ++It)
	{
		const int32 Pad = Graph.Add(It->GetActorLocation());
		const int32 Far = Graph.Add(It->Partner);
		Graph.Link(Pad, Far, FVector::Dist2D(It->GetActorLocation(), It->Partner) + 300);
		for (const int32 End : {Pad, Far})
		{
			for (const auto& Joined : Join(World, Graph, Graph.Nodes[End], true)) { if (Joined.Key != Pad && Joined.Key != Far) { Graph.Link(End, Joined.Key, Joined.Value); } }
		}
	}
}

FNavGraph& GraphFor(const UWorld* World)
{
	static FNavGraph Graph;
	int32 Extras = 0;
	for (TActorIterator<ARPGWarpGate> It(const_cast<UWorld*>(World)); It; ++It) { ++Extras; }
	for (TActorIterator<ARPGLeapPoint> It(const_cast<UWorld*>(World)); It; ++It) { Extras += 100; }
	if (Graph.World.Get() != World || Graph.Extras != Extras || Graph.Nodes.IsEmpty()) { BuildGraph(Graph, World, Extras); }
	return Graph;
}

// ---------------------------------------------------------------- quest goals

bool PlaceMatches(const FString& Place, const TCHAR* Keywords)
{
	if (!Keywords || Place.IsEmpty()) { return false; }
	TArray<FString> Words;
	FString(Keywords).ParseIntoArray(Words, TEXT("|"));
	for (const FString& Word : Words) { if (Place.Contains(Word)) { return true; } }
	return false;
}

/** "門番 ロウ" → "ロウ"; the board is "掲示板". */
FString GiverShort(int32 Giver)
{
	if (Giver <= 0) { return TEXT("掲示板"); }
	const auto& Givers = RPGQuests::Givers();
	FString Name = Givers[FMath::Clamp(Giver, 0, Givers.Num() - 1)].Name;
	int32 Space = INDEX_NONE;
	if (Name.FindLastChar(TEXT(' '), Space)) { Name.RightChopInline(Space + 1); }
	return Name;
}

void AddGiver(const UWorld* World, int32 Giver, RPGNav::FGoal& Out)
{
	UWorld* Mutable = const_cast<UWorld*>(World);
	if (Giver <= 0)
	{
		for (TActorIterator<ARPGAdventureNode> It(Mutable); It; ++It) { if (It->Kind == ERPGAdventureNodeKind::Board) { Out.Candidates.Add(It->GetActorLocation()); Out.Names.Add(TEXT("掲示板")); } }
		return;
	}
	for (TActorIterator<ARPGQuestGiver> It(Mutable); It; ++It) { if (It->GiverId == Giver) { Out.Candidates.Add(It->GetActorLocation()); Out.Names.Add(GiverShort(Giver)); } }
}

/** Where a named place is: the middle of the area (or road) of that name. */
bool PlacePoint(const UWorld* World, const FString& Word, FVector& Out)
{
	const FTransform Map = MapTransform(World);
	for (const RPGWorld::FArea& Area : RPGWorld::Areas()) { if (FString(Area.Region).Contains(Word)) { Out = Map.TransformPosition(Area.At); return true; } }
	for (const RPGWorld::FRoad& Road : RPGWorld::Roads())
	{
		if (Road.Points.Num() && FString(Road.Region).Contains(Word)) { Out = Map.TransformPosition(Road.Points[Road.Points.Num() / 2]); return true; }
	}
	return false;
}
}

// ---------------------------------------------------------------- RPGNav

int32 RPGNav::GuideQuest(const ARPGPrototypeCharacter* Player)
{
	if (!Player) { return INDEX_NONE; }
	const auto Going = [&](int32 Index)
	{
		const auto S = RPGQuests::State(Player, Index);
		return S == ERPGQuestState::Active || S == ERPGQuestState::Ready;
	};
	if (Player->TrackedQuest != INDEX_NONE && Player->Quests.IsValidIndex(Player->TrackedQuest) && Going(Player->TrackedQuest)) { return Player->TrackedQuest; }
	for (int32 Index = 0; Index < RPGQuests::Num() && Index < Player->Quests.Num(); ++Index) { if (Going(Index)) { return Index; } }
	return INDEX_NONE;
}

bool RPGNav::QuestGoals(const ARPGPrototypeCharacter* Player, int32 Quest, FGoal& Out)
{
	Out = FGoal();
	UWorld* World = Player ? Player->GetWorld() : nullptr;
	const auto* Adventure = ARPGAdventureWorld::Find(World);
	if (!World || !Adventure || !Player->Quests.IsValidIndex(Quest)) { return false; }
	const FRPGQuestDef& D = RPGQuests::Get(Quest);
	const FRPGAdventureProgress& Q = Player->Quests[Quest];
	const ERPGQuestState S = RPGQuests::State(Player, Quest);
	if (S == ERPGQuestState::Ready && RPGQuests::ReportsToGiver(Quest))
	{
		AddGiver(World, D.Giver, Out);
		Out.Label = GiverShort(D.Giver) + TEXT("に報告");
		return Out.Candidates.Num() > 0;
	}
	if (S != ERPGQuestState::Active) { return false; }
	const auto Groups = [&](const TCHAR* Label, bool bThrowersOnly, const TCHAR* Places)
	{
		for (TActorIterator<ARPGPrototypeSpawnPoint> It(World); It; ++It)
		{
			if (!It->bEnabled || Adventure->IsInColony(It->GetActorLocation())) { continue; }
			if (bThrowersOnly && It->bEncounterGroup && It->ThrowerCount <= 0) { continue; }
			if (Places && !PlaceMatches(Adventure->RegionName(It->GetActorLocation()), Places)) { continue; }
			Out.Candidates.Add(It->GetActorLocation());
			Out.Names.Add(Adventure->RegionName(It->GetActorLocation()));
		}
		Out.Label = Label;
	};
	switch (D.Kind)
	{
	case ERPGQuestKind::Hunt: case ERPGQuestKind::Special: Groups(TEXT("敵の群れ"), false, nullptr); break;
	case ERPGQuestKind::Bounty: Groups(TEXT("投手役のいる群れ"), true, nullptr); break;
	case ERPGQuestKind::Named:
		// One about now: it; else the groups strong enough to have one.
		for (TActorIterator<ARPGPrototypeCharacter> It(World); It; ++It)
		{ if (It->IsNamed() && !It->IsEliminated()) { Out.Candidates.Add(It->GetActorLocation()); Out.Names.Add(It->NamedTitle); } }
		Out.Label = TEXT("ネームド");
		if (Out.Candidates.IsEmpty())
		{
			for (TActorIterator<ARPGPrototypeSpawnPoint> It(World); It; ++It)
			{
				if (!It->bEnabled || It->EnemyLevel < RPGTuning::NamedFromLevel || Adventure->IsInColony(It->GetActorLocation())) { continue; }
				Out.Candidates.Add(It->GetActorLocation()); Out.Names.Add(Adventure->RegionName(It->GetActorLocation()));
			}
			Out.Label = TEXT("強い敵の群れ");
		}
		break;
	case ERPGQuestKind::AreaHunt: case ERPGQuestKind::TeamHunt: Groups(TEXT("敵の群れ"), false, D.Places); break;
	case ERPGQuestKind::Revive:
		for (TActorIterator<ARPGPrototypeCharacter> It(World); It; ++It)
		{ if (*It != Player && It->IsDowned()) { Out.Candidates.Add(It->GetActorLocation()); Out.Names.Add(It->GetHeroLabel()); } }
		Out.Label = TEXT("倒れた仲間");
		break;
	case ERPGQuestKind::TeamBoss:
		for (TActorIterator<ARPGPrototypeBoss> It(World); It; ++It)
		{ if (!It->IsEliminated()) { Out.Candidates.Add(It->GetActorLocation()); Out.Names.Add(Adventure->RegionName(It->GetActorLocation())); } }
		if (const ARPGSnowField* Field = ARPGSnowField::Find(World)) { Out.Candidates.Add(Field->NestCenter() + FVector(-1300, 0, 0)); Out.Names.Add(TEXT("シマエナガの巣")); }
		Out.Label = TEXT("ボスのいる場所");
		break;
	case ERPGQuestKind::Boss:
		if (D.Places && FString(D.Places).Contains(TEXT("雪原")))
		{
			if (const ARPGSnowField* Field = ARPGSnowField::Find(World)) { Out.Candidates.Add(Field->NestCenter() + FVector(-1300, 0, 0)); Out.Names.Add(TEXT("シマエナガの巣")); }
		}
		else
		{
			for (TActorIterator<ARPGPrototypeBoss> It(World); It; ++It)
			{
				if (It->IsEliminated() || (D.Places && !PlaceMatches(Adventure->RegionName(It->GetActorLocation()), D.Places))) { continue; }
				Out.Candidates.Add(It->GetActorLocation()); Out.Names.Add(Adventure->RegionName(It->GetActorLocation()));
			}
			FVector Hall;
			if (Out.Candidates.IsEmpty() && D.Places && PlacePoint(World, D.Places, Hall)) { Out.Candidates.Add(Hall); Out.Names.Add(D.Places); }
		}
		Out.Label = TEXT("ボスのいる場所");
		break;
	case ERPGQuestKind::Gather: case ERPGQuestKind::Survey:
	{
		const ERPGAdventureNodeKind Kind = D.Kind == ERPGQuestKind::Gather ? ERPGAdventureNodeKind::Supply : ERPGAdventureNodeKind::Survey;
		for (TActorIterator<ARPGAdventureNode> It(World); It; ++It)
		{
			if (It->Kind != Kind || (Q.Flags & (1 << FMath::Clamp(It->ObjectiveId, 0, 2)))) { continue; }
			Out.Candidates.Add(It->GetActorLocation()); Out.Names.Add(It->DisplayName);
		}
		Out.Label = D.Kind == ERPGQuestKind::Gather ? TEXT("補給箱") : TEXT("調査する場所");
		break;
	}
	case ERPGQuestKind::Explore:
	{
		TArray<FString> Words;
		FString(D.Places ? D.Places : TEXT("")).ParseIntoArray(Words, TEXT("|"));
		for (int32 Index = 0; Index < Words.Num(); ++Index)
		{
			FVector At;
			if (!(Q.Flags & (1 << Index)) && PlacePoint(World, Words[Index], At)) { Out.Candidates.Add(At); Out.Names.Add(Words[Index]); }
		}
		// Named by the place itself.
		Out.Label.Reset();
		break;
	}
	case ERPGQuestKind::Treasure:
		for (TActorIterator<ARPGTreasureChest> It(World); It; ++It)
		{ if (!It->IsOpenedBy(Player)) { Out.Candidates.Add(It->GetActorLocation()); Out.Names.Add(It->DisplayName); } }
		Out.Label = TEXT("まだ開けていない宝箱");
		break;
	case ERPGQuestKind::Workshop:
		if (const ARPGPrototypeMerchant* Merchant = Player->FindMerchant()) { Out.Candidates.Add(Merchant->GetActorLocation()); Out.Names.Add(TEXT("ルビー商人")); }
		Out.Label = TEXT("ルビー商人");
		break;
	case ERPGQuestKind::Beacon:
		for (TActorIterator<ARPGAdventureCheckpoint> It(World); It; ++It)
		{
			if (!Player->TravelPoints.Contains(ARPGPrototypeCharacter::TravelKey(*It))) { Out.Candidates.Add(It->GetActorLocation()); Out.Names.Add(It->DisplayName); }
		}
		Out.Label = TEXT("まだともしていない灯り");
		break;
	default: break;
	}
	return Out.Candidates.Num() > 0;
}

bool RPGNav::OfferGoals(const ARPGPrototypeCharacter* Player, FGoal& Out)
{
	Out = FGoal();
	if (!Player || !Player->GetWorld()) { return false; }
	TSet<int32> Done;
	for (int32 Index = 0; Index < RPGQuests::Num() && Index < Player->Quests.Num(); ++Index)
	{
		const int32 Giver = RPGQuests::Get(Index).Giver;
		if (!RPGQuests::ReportsToGiver(Index) || Done.Contains(Giver) || RPGQuests::State(Player, Index) != ERPGQuestState::Offered) { continue; }
		Done.Add(Giver);
		AddGiver(Player->GetWorld(), Giver, Out);
	}
	Out.Label = TEXT("依頼のある人");
	return Out.Candidates.Num() > 0;
}

float RPGNav::WayLength(const TArray<FVector>& Way)
{
	float Length = 0;
	for (int32 Index = 1; Index < Way.Num(); ++Index) { Length += FVector::Dist2D(Way[Index - 1], Way[Index]); }
	return Length;
}

TArray<FVector> RPGNav::ShortestWay(const UWorld* World, const FVector& From, const TArray<FVector>& Candidates, int32* OutIndex, const FVector* Prefer)
{
	if (OutIndex) { *OutIndex = INDEX_NONE; }
	if (!World || Candidates.IsEmpty()) { return {}; }
	FNavGraph& Graph = GraphFor(World);
	const FTransform Map = MapTransform(World);
	const auto Bias = [&](int32 Candidate) { return Prefer && FVector::Dist2D(*Prefer, Candidates[Candidate]) < 60 ? 0.87f : 1.0f; };
	// Close and in plain sight: straight there.
	int32 Best = INDEX_NONE, BestNode = INDEX_NONE;
	float BestCost = MAX_flt;
	for (int32 Candidate = 0; Candidate < Candidates.Num(); ++Candidate)
	{
		const float Direct = FVector::Dist2D(From, Candidates[Candidate]);
		if (Direct < 1700 && FMath::Abs(From.Z - Candidates[Candidate].Z) < 260 && Walkable(World, Map, From, Candidates[Candidate]) && Direct * Bias(Candidate) < BestCost)
		{ BestCost = Direct * Bias(Candidate); Best = Candidate; }
	}
	// Dijkstra over the roads from where this player stands.
	const int32 Count = Graph.Nodes.Num();
	TArray<float> Cost; Cost.Init(MAX_flt, Count);
	TArray<int32> Previous; Previous.Init(INDEX_NONE, Count);
	struct FItem { float Cost; int32 Node; };
	const auto Less = [](const FItem& A, const FItem& B) { return A.Cost < B.Cost; };
	TArray<FItem> Heap;
	for (const auto& Start : Join(World, Graph, From, true))
	{
		if (Start.Value < Cost[Start.Key]) { Cost[Start.Key] = Start.Value; Heap.HeapPush({Start.Value, Start.Key}, Less); }
	}
	while (Heap.Num())
	{
		FItem Top;
		Heap.HeapPop(Top, Less, EAllowShrinking::No);
		if (Top.Cost > Cost[Top.Node]) { continue; }
		for (const auto& Next : Graph.Links[Top.Node])
		{
			const float Through = Top.Cost + Next.Value;
			if (Through < Cost[Next.Key]) { Cost[Next.Key] = Through; Previous[Next.Key] = Top.Node; Heap.HeapPush({Through, Next.Key}, Less); }
		}
	}
	for (int32 Candidate = 0; Candidate < Candidates.Num(); ++Candidate)
	{
		const FIntVector Key(FMath::RoundToInt(Candidates[Candidate].X / 20), FMath::RoundToInt(Candidates[Candidate].Y / 20), FMath::RoundToInt(Candidates[Candidate].Z / 20));
		if (!Graph.GoalJoins.Contains(Key)) { Graph.GoalJoins.Add(Key, Join(World, Graph, Candidates[Candidate], true)); }
		for (const auto& End : Graph.GoalJoins[Key])
		{
			if (Cost[End.Key] == MAX_flt) { continue; }
			const float Total = (Cost[End.Key] + End.Value) * Bias(Candidate);
			if (Total < BestCost) { BestCost = Total; Best = Candidate; BestNode = End.Key; }
		}
	}
	if (Best == INDEX_NONE) { return {}; }
	if (OutIndex) { *OutIndex = Best; }
	TArray<FVector> Way{From};
	if (BestNode != INDEX_NONE)
	{
		TArray<FVector> Back;
		for (int32 Node = BestNode; Node != INDEX_NONE; Node = Previous[Node]) { Back.Add(Graph.Nodes[Node]); }
		for (int32 Index = Back.Num() - 1; Index >= 0; --Index) { Way.Add(Back[Index]); }
	}
	Way.Add(Candidates[Best]);
	// No step back to a node behind the player or past the goal: cut the corner where the ground allows.
	while (Way.Num() > 2 && FVector::Dist2D(Way[0], Way[2]) < 2200 && Walkable(World, Map, Way[0], Way[2])) { Way.RemoveAt(1); }
	while (Way.Num() > 2 && FVector::Dist2D(Way.Last(), Way[Way.Num() - 3]) < 2200 && Walkable(World, Map, Way.Last(), Way[Way.Num() - 3])) { Way.RemoveAt(Way.Num() - 2); }
	for (int32 Index = Way.Num() - 1; Index > 0; --Index) { if (FVector::Dist2D(Way[Index], Way[Index - 1]) < 1) { Way.RemoveAt(Index); } }
	return Way;
}

// ---------------------------------------------------------------- the light line

namespace
{
constexpr int32 MaxGuideLights = 64;
constexpr float GuideSpacing = 85;
}

ARPGQuestGuide::ARPGQuestGuide()
{
	PrimaryActorTick.bCanEverTick = true;
	SetReplicates(false);
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("GuideRoot")));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Cylinder.Succeeded()) { Mesh = Cylinder.Object; }
}

void ARPGQuestGuide::SetColor(const FLinearColor& InColor)
{
	Color = InColor;
	Look = ChaosImpactBallTypes::MakeAdditive(this, Color, 4.0f);
	for (UStaticMeshComponent* Light : Lights) { if (Light) { Light->SetMaterial(0, Look); } }
}

void ARPGQuestGuide::ShowRoute(const TArray<FVector>& Route)
{
	Path.Reset(); Yaws.Reset();
	if (Route.Num() >= 2 && RPGNav::WayLength(Route) > 320)
	{
		float Next = 70, Covered = 0;
		for (int32 Index = 0; Index + 1 < Route.Num() && Path.Num() < MaxGuideLights; ++Index)
		{
			const FVector A = Route[Index], B = Route[Index + 1];
			const float Length = FVector::Dist2D(A, B);
			const float Yaw = static_cast<float>((B - A).Rotation().Yaw);
			while (Next <= Covered + Length && Path.Num() < MaxGuideLights)
			{
				Path.Add(FMath::Lerp(A, B, (Next - Covered) / FMath::Max(1.0f, Length)));
				Yaws.Add(Yaw);
				Next += GuideSpacing;
			}
			Covered += Length;
		}
		// Down onto the ground where it really is (bridges, steps, the snow).
		FCollisionQueryParams Params(SCENE_QUERY_STAT(RPGQuestGuide), false, this);
		if (GetOwner()) { Params.AddIgnoredActor(GetOwner()); }
		for (FVector& At : Path)
		{
			FHitResult Hit;
			if (GetWorld()->LineTraceSingleByObjectType(Hit, At + FVector(0, 0, 320), At - FVector(0, 0, 700), FCollisionObjectQueryParams(ECC_WorldStatic), Params))
			{ At = Hit.ImpactPoint + FVector(0, 0, 4); }
		}
	}
	// A shared screen shows everyone's line (each in its player's colour); otherwise only its own player sees it.
	const auto* Mode = GetWorld()->GetAuthGameMode<ARPGPrototypeGameMode>();
	const bool bOwnerOnly = !(Mode && Mode->UsesSharedScreen());
	while (Lights.Num() < Path.Num())
	{
		auto* Light = NewObject<UStaticMeshComponent>(this);
		Light->SetupAttachment(GetRootComponent());
		Light->SetUsingAbsoluteLocation(true); Light->SetUsingAbsoluteRotation(true); Light->SetUsingAbsoluteScale(true);
		Light->SetStaticMesh(Mesh);
		if (!Look) { Look = ChaosImpactBallTypes::MakeAdditive(this, Color, 4.0f); }
		Light->SetMaterial(0, Look);
		Light->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Light->SetCastShadow(false);
		Light->SetCanEverAffectNavigation(false);
		Light->SetMobility(EComponentMobility::Movable);
		Light->RegisterComponent();
		Lights.Add(Light);
	}
	for (int32 Index = 0; Index < Lights.Num(); ++Index)
	{
		if (!Lights[Index]) { continue; }
		Lights[Index]->SetOnlyOwnerSee(bOwnerOnly);
		Lights[Index]->SetVisibility(Index < Path.Num());
	}
	SetActorHiddenInGame(Path.IsEmpty());
}

void ARPGQuestGuide::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// Its player is gone (left the world): nothing to show the way for.
	if (!GetOwner()) { Destroy(); return; }
	Clock += DeltaSeconds;
	if (Path.IsEmpty() || Lights.Num() < Path.Num()) { return; }
	// The way was laid a moment ago: lights the player has already passed go out.
	int32 First = 0;
	float Nearest = MAX_flt;
	for (int32 Index = 0; Index < FMath::Min(Path.Num(), 14); ++Index)
	{
		const float Distance = FVector::Dist2D(Path[Index], GetOwner()->GetActorLocation());
		if (Distance < Nearest) { Nearest = Distance; First = Index; }
	}
	const int32 Count = Path.Num();
	for (int32 Index = 0; Index < Count; ++Index)
	{
		UStaticMeshComponent* Light = Lights[Index];
		if (!Light) { continue; }
		const int32 Ahead = Index - First;
		if (Ahead < 0) { Light->SetVisibility(false); continue; }
		// A pulse running toward the goal; the near end grows in and the far end fades away.
		const float Pulse = 0.5f + 0.5f * FMath::Sin(Clock * 5.5f - Index * 0.45f);
		const float Ends = FMath::Min(FMath::Clamp((Ahead + 1) / 3.0f, 0.0f, 1.0f), FMath::Clamp((Count - Index) / 8.0f, 0.0f, 1.0f));
		const float Size = Ends * (0.75f + 0.45f * Pulse);
		Light->SetVisibility(Size > 0.02f);
		Light->SetWorldLocationAndRotation(Path[Index], FRotator(0, Yaws[Index], 0));
		Light->SetWorldScale3D(FVector(0.46f * Size, 0.15f * Size, 0.012f));
	}
}

// ---------------------------------------------------------------- the maps

namespace
{
/** Map space: layout points (+X north, +Y east) to screen, north up. */
struct FMapView
{
	FVector2D Center;
	FVector2D Focus;
	float Scale = 0.02f;
	FTransform Map;
	FVector2D Layout(const FVector2D& At) const { return Center + FVector2D((At.Y - Focus.Y) * Scale, -(At.X - Focus.X) * Scale); }
	FVector2D World(const FVector& At) const { const FVector L = Map.InverseTransformPosition(At); return Layout(FVector2D(L.X, L.Y)); }
};

FLinearColor SurfaceColor(RPGWorld::ESurface Surface)
{
	switch (Surface)
	{
	case RPGWorld::ESurface::Dirt: return FLinearColor(0.36f, 0.30f, 0.20f, 1.0f);
	case RPGWorld::ESurface::Pavers: return FLinearColor(0.33f, 0.35f, 0.41f, 1.0f);
	case RPGWorld::ESurface::Sand: return FLinearColor(0.55f, 0.48f, 0.32f, 1.0f);
	case RPGWorld::ESurface::Cave: return FLinearColor(0.25f, 0.20f, 0.38f, 1.0f);
	case RPGWorld::ESurface::Moss: return FLinearColor(0.15f, 0.33f, 0.29f, 1.0f);
	case RPGWorld::ESurface::Snow: return FLinearColor(0.70f, 0.76f, 0.86f, 1.0f);
	default: return FLinearColor(0.18f, 0.36f, 0.22f, 1.0f);
	}
}

void Ellipse(const FPainter& P, const FVector2D& Center, const FVector2D& Radius, const FLinearColor& Color, int32 Segments = 28)
{
	if (Radius.X < 0.5 || Radius.Y < 0.5) { return; }
	TArray<FVector2D> Ring;
	Ring.Reserve(Segments);
	for (int32 Index = 0; Index < Segments; ++Index)
	{
		const float Angle = UE_TWO_PI * Index / Segments;
		Ring.Add(Center + FVector2D(FMath::Cos(Angle) * Radius.X, FMath::Sin(Angle) * Radius.Y));
	}
	ChaosImpactPaint::HudFan(P, Center, Ring, ChaosImpactPaint::SolidFill(Color));
}

void Polyline(const FPainter& P, const TArray<FVector2D>& Points, const FLinearColor& Color, float Width)
{
	if (Points.Num() < 2 || Width <= 0) { return; }
	FSlateDrawElement::MakeLines(P.Elements, P.Layer, P.Geometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, WithAlpha(Color, P.Alpha), true, Width);
}

/** The land: rock, open areas, the brook, the roads with their worn tracks, the nest. */
void PaintLand(const FPainter& Base, const FMapView& V, const UWorld* World, float MinRoad)
{
	const FPainter Rock{Base.Geometry, Base.Elements, Base.Layer, Base.Alpha};
	const FPainter Land{Base.Geometry, Base.Elements, Base.Layer + 1, Base.Alpha};
	const FPainter Water{Base.Geometry, Base.Elements, Base.Layer + 2, Base.Alpha};
	const FPainter Road{Base.Geometry, Base.Elements, Base.Layer + 3, Base.Alpha};
	const FPainter Track{Base.Geometry, Base.Elements, Base.Layer + 4, Base.Alpha};
	for (const RPGWorld::FRock& R : RPGWorld::Rocks()) { Ellipse(Rock, V.Layout(R.At), FVector2D(R.Radius.Y, R.Radius.X) * V.Scale, FLinearColor(0.09f, 0.09f, 0.12f, 0.75f), 20); }
	for (const RPGWorld::FArea& A : RPGWorld::Areas()) { Ellipse(Land, V.Layout(FVector2D(A.At)), FVector2D(A.Radius.Y, A.Radius.X) * V.Scale, SurfaceColor(A.Fill)); }
	for (const RPGWorld::FGap& G : RPGWorld::Gaps())
	{
		TArray<FVector2D> Points;
		for (const FVector2D& At : G.Points) { Points.Add(V.Layout(At)); }
		Polyline(Water, Points, FLinearColor(0.12f, 0.30f, 0.55f, 1.0f), FMath::Max(2.0f, G.Width * V.Scale));
	}
	for (const RPGWorld::FRoad& R : RPGWorld::Roads())
	{
		TArray<FVector2D> Points;
		for (const FVector& At : R.Points) { Points.Add(V.Layout(FVector2D(At))); }
		Polyline(Road, Points, SurfaceColor(R.Path) * 1.12f, FMath::Max(MinRoad, R.Width * V.Scale));
		Polyline(Track, Points, FLinearColor(0.86f, 0.80f, 0.64f, 0.55f), FMath::Max(1.2f, R.Width * V.Scale * 0.22f));
	}
	// The Shima-enaga's nest: its ring of snow walls.
	if (const ARPGSnowField* Field = ARPGSnowField::Find(World))
	{ Track.Ring(V.World(Field->NestCenter()), Field->NestRadius() * V.Scale, FLinearColor(0.95f, 0.97f, 1.0f, 0.85f), FMath::Max(1.5f, 200 * V.Scale), 40); }
}

/** Unexplored ground under a dark veil (the cells of the map this hero has not been near), a strip per row of cells. */
void PaintFog(const FPainter& P, const FMapView& V, const ARPGPrototypeCharacter* Player, const FLinearColor& Veil, const FBox2D* Clamp)
{
	constexpr float Cell = 1000.0f;
	const int32 NX = FMath::CeilToInt((RPGWorld::MaxX - RPGWorld::MinX) / Cell), NY = FMath::CeilToInt((RPGWorld::MaxY - RPGWorld::MinY) / Cell);
	// A margin of fog round the map too, so its edge does not show where the cells stop.
	for (int32 IX = -2; IX < NX + 2; ++IX)
	{
		const float X0 = RPGWorld::MinX + IX * Cell;
		const float Top = V.Layout(FVector2D(X0 + Cell, 0)).Y, Bottom = V.Layout(FVector2D(X0, 0)).Y;
		if (Clamp && (Bottom < Clamp->Min.Y - 40 || Top > Clamp->Max.Y + 40)) { continue; }
		int32 RunStart = INDEX_NONE;
		for (int32 IY = -2; IY <= NY + 2; ++IY)
		{
			const bool bFog = IY < NY + 2 && !Player->IsExplored(IX, IY);
			if (bFog && RunStart == INDEX_NONE) { RunStart = IY; }
			if ((!bFog || IY == NY + 2) && RunStart != INDEX_NONE)
			{
				const float Left = V.Layout(FVector2D(0, RPGWorld::MinY + RunStart * Cell)).X, Right = V.Layout(FVector2D(0, RPGWorld::MinY + IY * Cell)).X;
				P.Box(Left, Top, Right - Left, Bottom - Top, Veil);
				RunStart = INDEX_NONE;
			}
		}
	}
}

bool PlaceSeen(const ARPGPrototypeCharacter* Player, const FVector2D& LayoutAt)
{
	return Player->IsExplored(FMath::FloorToInt((LayoutAt.X - RPGWorld::MinX) / 1000.0f), FMath::FloorToInt((LayoutAt.Y - RPGWorld::MinY) / 1000.0f));
}

/** Place names: on the whole map once per place (the part before "・": 水晶洞窟 ...), close up every area's own. Unseen ones are left out. */
void PaintPlaceNames(const FPainter& P, const FMapView& V, bool bEveryArea, const FBox2D* Clamp, const ARPGPrototypeCharacter* Player)
{
	TMap<FString, TPair<FVector2D, int32>> Names;
	for (const RPGWorld::FArea& A : RPGWorld::Areas())
	{
		if (A.Id == TEXT("ColonyPlaza")) { continue; }
		FString Name = A.Region;
		int32 Dot = INDEX_NONE;
		if (!bEveryArea && Name.FindChar(TEXT('・'), Dot)) { Name.LeftInline(Dot); }
		auto& Entry = Names.FindOrAdd(Name, TPair<FVector2D, int32>(FVector2D::ZeroVector, 0));
		Entry.Key += FVector2D(A.At); ++Entry.Value;
	}
	for (const auto& Entry : Names)
	{
		const FVector2D Middle = Entry.Value.Key / FMath::Max(1, Entry.Value.Value);
		if (Player && !PlaceSeen(Player, Middle)) { continue; }
		const FVector2D At = V.Layout(Middle);
		if (Clamp && !Clamp->IsInside(At)) { continue; }
		P.Text(Entry.Key, At.X, At.Y - 22, bEveryArea ? 13 : 12, WithAlpha(RPGUI::TextMain, 0.85f), ETextAlign::Center, TEXT("Bold"), 2, Navy);
	}
}

/** Quests a person or the board has for this player: 1 offer, 2 report, 0 nothing. */
int32 GiverState(const ARPGPrototypeCharacter* P, int32 Giver)
{
	int32 Mark = 0;
	for (int32 Index = 0; Index < RPGQuests::Num() && Index < P->Quests.Num(); ++Index)
	{
		if (!RPGQuests::ReportsToGiver(Index) || RPGQuests::Get(Index).Giver != Giver) { continue; }
		const auto S = RPGQuests::State(P, Index);
		if (S == ERPGQuestState::Ready) { return 2; }
		if (S == ERPGQuestState::Offered) { Mark = 1; }
	}
	return Mark;
}

void Glyph(const FPainter& P, const FVector2D& At, float R, const FLinearColor& Fill, const TCHAR* Text)
{
	P.Disc(At, R + 1.5f, Navy);
	P.Disc(At, R, Fill);
	if (Text) { P.Text(Text, At.X, At.Y - R * 1.15f, R * 1.45f, Navy, ETextAlign::Center, TEXT("Black")); }
}

void Diamond(const FPainter& P, const FVector2D& At, float R, const FLinearColor& Fill)
{
	ChaosImpactPaint::HudFan(P, At, {At + FVector2D(0, -R - 1.5f), At + FVector2D(R + 1.5f, 0), At + FVector2D(0, R + 1.5f), At + FVector2D(-R - 1.5f, 0)}, ChaosImpactPaint::SolidFill(Navy));
	const FPainter Top{P.Geometry, P.Elements, P.Layer + 1, P.Alpha};
	ChaosImpactPaint::HudFan(Top, At, {At + FVector2D(0, -R), At + FVector2D(R, 0), At + FVector2D(0, R), At + FVector2D(-R, 0)}, ChaosImpactPaint::SolidFill(Fill));
}

/** A player: their colour, a ring for this player, a point showing which way they face. */
void HeroMark(const FPainter& P, const FVector2D& At, float R, const ARPGPrototypeCharacter* Hero, bool bSelf, const FMapView& V)
{
	const FLinearColor Color = Hero->HeroCharacter >= 0 ? ChaosImpactRoster::GetColourSwatch(FMath::Max(0, Hero->HeroColour))
		: ChaosImpactPaint::PlayerAccents[FMath::Clamp(Hero->GetLocalRPGIndex(), 0, 3)];
	const FVector Facing = V.Map.InverseTransformVector(Hero->GetActorForwardVector());
	const FVector2D Dir = FVector2D(Facing.Y, -Facing.X).GetSafeNormal();
	const FVector2D Side(-Dir.Y, Dir.X);
	const float Tip = R * (bSelf ? 2.1f : 1.8f);
	ChaosImpactPaint::HudFan(P, At, {At + Dir * (Tip + 2), At + Side * (R + 2), At - Dir * (R * 0.5f + 2), At - Side * (R + 2)}, ChaosImpactPaint::SolidFill(Navy));
	const FPainter Top{P.Geometry, P.Elements, P.Layer + 1, P.Alpha};
	ChaosImpactPaint::HudFan(Top, At, {At + Dir * Tip, At + Side * R, At - Dir * R * 0.5f, At - Side * R}, ChaosImpactPaint::SolidFill(bSelf ? RPGUI::TextMain : Color));
	if (bSelf) { Top.Ring(At, R + 4, WithAlpha(RPGUI::TextMain, 0.6f), 1.5f, 20); }
	else if (Hero->IsDowned() || Hero->IsEliminated()) { Top.Ring(At, R + 4, RPGUI::Bad, 2.0f, 20); }
}

/** Everything that is not land: places of interest, the goal, the way, the players. Clamp: keep marks inside a box. */
void PaintMarks(const FPainter& Base, const FMapView& V, const ARPGPrototypeCharacter* Player, float Time, float Size, bool bFull, const FBox2D* Clamp)
{
	UWorld* World = Player->GetWorld();
	const FPainter Low{Base.Geometry, Base.Elements, Base.Layer, Base.Alpha};
	const FPainter Mid{Base.Geometry, Base.Elements, Base.Layer + 2, Base.Alpha};
	const FPainter High{Base.Geometry, Base.Elements, Base.Layer + 4, Base.Alpha};
	const auto Inside = [&](const FVector2D& At) { return !Clamp || Clamp->IsInside(At); };
	// Rest beacons: lit ones green, the rest dim.
	for (TActorIterator<ARPGAdventureCheckpoint> It(World); It; ++It)
	{
		const FVector2D At = V.World(It->GetActorLocation());
		if (!Inside(At)) { continue; }
		const bool bLit = Player->TravelPoints.Contains(ARPGPrototypeCharacter::TravelKey(*It));
		Diamond(Low, At, Size * 0.75f, bLit ? RPGUI::Good : FLinearColor(0.45f, 0.5f, 0.56f, 1.0f));
	}
	if (const ARPGPrototypeMerchant* Merchant = Player->FindMerchant())
	{ const FVector2D At = V.World(Merchant->GetActorLocation()); if (Inside(At)) { Glyph(Low, At, Size * 0.8f, RPGUI::Highlight, TEXT("商")); } }
	// People and the board with something for you.
	for (TActorIterator<ARPGQuestGiver> It(World); It; ++It)
	{
		const int32 Mark = GiverState(Player, It->GiverId);
		const FVector2D At = V.World(It->GetActorLocation());
		if (Mark && Inside(At)) { Glyph(Mid, At, Size * 0.85f, Mark == 2 ? RPGUI::Good : RPGUI::Highlight, Mark == 2 ? TEXT("?") : TEXT("!")); }
	}
	for (TActorIterator<ARPGAdventureNode> It(World); It; ++It)
	{
		if (It->Kind != ERPGAdventureNodeKind::Board) { continue; }
		const int32 Mark = GiverState(Player, 0);
		const FVector2D At = V.World(It->GetActorLocation());
		if (Mark && Inside(At)) { Glyph(Mid, At, Size * 0.85f, Mark == 2 ? RPGUI::Good : RPGUI::Highlight, Mark == 2 ? TEXT("?") : TEXT("!")); }
	}
	// Bosses (the whole map only).
	if (bFull)
	{
		for (TActorIterator<ARPGPrototypeBoss> It(World); It; ++It)
		{ if (!It->IsEliminated()) { Glyph(Mid, V.World(It->GetActorLocation()), Size * 0.85f, RPGUI::Bad, TEXT("!")); } }
		if (const ARPGSnowField* Field = ARPGSnowField::Find(World)) { Glyph(Mid, V.World(Field->NestCenter()), Size * 0.85f, RPGUI::Bad, TEXT("!")); }
	}
	// A field event and a named enemy: a gold star (it beats gently).
	const FLinearColor Star(1.0f, 0.8f, 0.18f, 1.0f);
	if (const ARPGFieldEvents* Events = ARPGFieldEvents::Find(World); Events && Events->GetKind() != ARPGFieldEvents::None)
	{
		const FVector2D At = V.World(Events->GetLocation());
		if (Inside(At)) { Glyph(High, At, Size * (0.95f + 0.12f * FMath::Sin(Time * 5)), Star, TEXT("★")); }
	}
	for (TActorIterator<ARPGPrototypeCharacter> It(World); It; ++It)
	{
		if (!It->IsNamed() || It->IsEliminated()) { continue; }
		const FVector2D At = V.World(It->GetActorLocation());
		if (Inside(At)) { Glyph(High, At, Size * 0.85f, FLinearColor(1.0f, 0.5f, 0.15f, 1.0f), TEXT("★")); }
	}
	// The way (when the light line is on) and the goal.
	const TArray<FVector>& Way = Player->GetQuestRoute();
	if (Player->IsGuideOn() && Way.Num() >= 2)
	{
		TArray<FVector2D> Points;
		for (const FVector& At : Way) { Points.Add(V.World(At)); }
		Polyline(Mid, Points, WithAlpha(Navy, 0.8f), bFull ? 4.5f : 5.0f);
		Polyline(FPainter{Base.Geometry, Base.Elements, Base.Layer + 3, Base.Alpha}, Points, RPGUI::Cyan, bFull ? 2.5f : 3.0f);
	}
	FVector Goal;
	if (Player->GetQuestTarget(Goal))
	{
		FVector2D At = V.World(Goal);
		const bool bOff = Clamp && !Clamp->IsInside(At);
		if (bOff)
		{
			// Off the minimap: an arrow on its edge pointing at it.
			const FVector2D Middle = Clamp->GetCenter();
			const FVector2D Dir = (At - Middle).GetSafeNormal();
			const FVector2D Half = Clamp->GetExtent();
			const float Reach = FMath::Min(Half.X / FMath::Max(0.001, FMath::Abs(Dir.X)), Half.Y / FMath::Max(0.001, FMath::Abs(Dir.Y)));
			At = Middle + Dir * Reach;
			const FVector2D Side(-Dir.Y, Dir.X);
			ChaosImpactPaint::HudFan(High, At, {At + Dir * 2 - Dir * 1, At - Dir * 13 + Side * 9, At - Dir * 13 - Side * 9}, ChaosImpactPaint::SolidFill(Navy));
			ChaosImpactPaint::HudFan(FPainter{Base.Geometry, Base.Elements, Base.Layer + 5, Base.Alpha}, At - Dir * 2, {At - Dir * 2, At - Dir * 11 + Side * 6.5f, At - Dir * 11 - Side * 6.5f}, ChaosImpactPaint::SolidFill(RPGUI::Highlight));
		}
		else
		{
			const float Pulse = 0.5f + 0.5f * FMath::Sin(Time * 4.0f);
			High.Ring(At, Size * (1.3f + 0.5f * Pulse), WithAlpha(RPGUI::Highlight, 0.9f - 0.5f * Pulse), 2.0f, 28);
			Diamond(High, At, Size * 0.9f, RPGUI::Highlight);
		}
	}
	// Players: teammates in their colour (at the edge when off the minimap), this player on top.
	for (TActorIterator<ARPGPrototypeCharacter> It(World); It; ++It)
	{
		if (It->IsGrunt() || *It == Player || It->IsInTitle()) { continue; }
		FVector2D At = V.World(It->GetActorLocation());
		if (Clamp && !Clamp->IsInside(At))
		{
			const FVector2D Middle = Clamp->GetCenter(), Half = Clamp->GetExtent() - FVector2D(6, 6);
			const FVector2D Dir = (At - Middle).GetSafeNormal();
			At = Middle + Dir * FMath::Min(Half.X / FMath::Max(0.001, FMath::Abs(Dir.X)), Half.Y / FMath::Max(0.001, FMath::Abs(Dir.Y)));
		}
		HeroMark(FPainter{Base.Geometry, Base.Elements, Base.Layer + 6, Base.Alpha}, At, Size * 0.7f, *It, false, V);
		if (bFull)
		{
			FPainter Name{Base.Geometry, Base.Elements, Base.Layer + 8, Base.Alpha};
			Name.Text(It->GetHeroLabel(), At.X, At.Y + Size * 1.2f, 12, RPGUI::TextMain, ETextAlign::Center, TEXT("Bold"), 2, Navy);
		}
	}
	HeroMark(FPainter{Base.Geometry, Base.Elements, Base.Layer + 8, Base.Alpha}, V.World(Player->GetActorLocation()), Size * 0.8f, Player, true, V);
}
}

void RPGMap::PaintMini(const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer, float W, float H, const ARPGPrototypeCharacter* Player, float Time, bool bPad)
{
	UWorld* World = Player ? Player->GetWorld() : nullptr;
	if (!World || !ARPGAdventureWorld::Find(World)) { return; }
	const bool bSmall = H < 650;
	const float Size = bSmall ? 170 : 206, X = W - Size - 22, Y = bSmall ? 104 : 114;
	RPGUI::FCanvas C; C.Geometry = &Design; C.Elements = &Elements; C.Layer = Layer; C.Time = Time;
	ChaosImpactPaint::RoundedBox(C.At(0, 0), X - 3, Y - 3, Size + 6, Size + 6, 12, WithAlpha(Navy, 0.9f));
	ChaosImpactPaint::RoundedBox(C.At(0, 1), X, Y, Size, Size, 10, FLinearColor(0.035f, 0.06f, 0.12f, 0.82f));
	FMapView V;
	V.Map = MapTransform(World);
	V.Center = FVector2D(X + Size * 0.5f, Y + Size * 0.5f);
	const FVector Here = V.Map.InverseTransformPosition(Player->GetActorLocation());
	V.Focus = FVector2D(Here.X, Here.Y);
	V.Scale = Size / 9000.0f;
	const FGeometry Clip = Design.MakeChild(FVector2f(Size - 4, Size - 4), FSlateLayoutTransform(FVector2f(X + 2, Y + 2)));
	Elements.PushClip(FSlateClippingZone(Clip));
	PaintLand(C.At(0, 2), V, World, 2.5f);
	const FBox2D Inner(FVector2D(X + 10, Y + 10), FVector2D(X + Size - 10, Y + Size - 10));
	{ const FBox2D Whole(FVector2D(X, Y), FVector2D(X + Size, Y + Size)); PaintFog(C.At(0, 8), V, Player, FLinearColor(0.02f, 0.035f, 0.07f, 0.78f), &Whole); }
	PaintMarks(C.At(1, 0), V, Player, Time, 7.0f, false, &Inner);
	Elements.PopClip();
	// North, and the key for the whole map.
	FPainter Text = C.At(2, 6);
	Text.Text(TEXT("N"), X + Size * 0.5f, Y - 1, 12, RPGUI::TextSub, ETextAlign::Center, TEXT("Black"), 2, Navy);
	const float Key = RPGUI::Keycap(C, 2, bPad ? TEXT("→") : TEXT("M"), X + 6, Y + Size - 28, 22);
	RPGUI::Label(C, 2, TEXT("マップ"), X + 12 + Key, Y + Size - 27, 13, RPGUI::TextMain);
	// The light line: on or off, and what it leads to.
	const float LineY = Y + Size + 8;
	const float GuideKey = RPGUI::Keycap(C, 1, bPad ? TEXT("R3") : TEXT("N"), X, LineY, 22);
	FVector Goal; FString Label;
	const bool bGoal = Player->GetQuestTarget(Goal, &Label);
	FString Line = Player->IsGuideOn() ? FString(TEXT("案内 オン")) : FString(TEXT("案内 オフ"));
	if (Player->IsGuideOn() && bGoal)
	{ Line = FString::Printf(TEXT("%s  %dm"), *Label, FMath::RoundToInt(RPGNav::WayLength(Player->GetQuestRoute()) / 100)); }
	else if (Player->IsGuideOn()) { Line = TEXT("案内 オン（行き先なし）"); }
	// Under the map, on two lines when the place's name is long.
	RPGUI::Wrap(C, 1, Line, X + GuideKey + 6, LineY + 2, Size - GuideKey - 8, 13, Player->IsGuideOn() ? RPGUI::Cyan : RPGUI::TextDim, 2, 17);
}

void RPGMap::PaintNearby(const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer, float W, float H, const ARPGPrototypeCharacter* Player, float Time, bool bPad)
{
	UWorld* World = Player ? Player->GetWorld() : nullptr;
	if (!World || !ARPGAdventureWorld::Find(World)) { return; }
	RPGUI::FCanvas C; C.Geometry = &Design; C.Elements = &Elements; C.Layer = Layer; C.Time = Time;
	// The surroundings over the middle of the screen, this player in the middle of it (where the game shows them).
	const float X0 = 70, Y0 = 92, X1 = W - 70, Y1 = H - 64;
	ChaosImpactPaint::RoundedBox(C.At(0, 0), X0, Y0, X1 - X0, Y1 - Y0, 18, FLinearColor(0.01f, 0.025f, 0.06f, 0.38f));
	FMapView V;
	V.Map = MapTransform(World);
	V.Center = FVector2D((X0 + X1) * 0.5f, (Y0 + Y1) * 0.5f);
	const FVector Here = V.Map.InverseTransformPosition(Player->GetActorLocation());
	V.Focus = FVector2D(Here.X, Here.Y);
	V.Scale = (X1 - X0) / 26000.0f;
	const FGeometry Clip = Design.MakeChild(FVector2f(X1 - X0, Y1 - Y0), FSlateLayoutTransform(FVector2f(X0, Y0)));
	Elements.PushClip(FSlateClippingZone(Clip));
	FPainter LandPaint = C.At(0, 1); LandPaint.Alpha = 0.55f;
	PaintLand(LandPaint, V, World, 2.0f);
	const FBox2D Inner(FVector2D(X0 + 16, Y0 + 16), FVector2D(X1 - 16, Y1 - 16));
	PaintFog(C.At(0, 8), V, Player, FLinearColor(0.01f, 0.02f, 0.05f, 0.6f), &Inner);
	PaintPlaceNames(C.At(1, 6), V, true, &Inner, Player);
	PaintMarks(C.At(2, 0), V, Player, Time, 8.0f, true, &Inner);
	Elements.PopClip();
	// The keys, small, under it.
	float KeyX = X0 + 6;
	const float KeyY = Y1 + 10;
	const auto Hint = [&](const TCHAR* Keyboard, const TCHAR* Pad, const FString& Text, const FLinearColor& Color)
	{
		KeyX += RPGUI::Keycap(C, 3, bPad ? Pad : Keyboard, KeyX, KeyY, 22) + 6;
		RPGUI::Label(C, 3, Text, KeyX, KeyY + 2, 14, Color);
		KeyX += RPGUI::Measure(Text, 14) + 20;
	};
	Hint(TEXT("M"), TEXT("→"), TEXT("全体マップ"), RPGUI::TextMain);
	if (!bPad) { Hint(TEXT("Esc"), TEXT(""), TEXT("閉じる"), RPGUI::TextMain); }
	Hint(TEXT("N"), TEXT("R3"), Player->IsGuideOn() ? FString(TEXT("案内ライン オン")) : FString(TEXT("案内ライン オフ")), Player->IsGuideOn() ? RPGUI::Cyan : RPGUI::TextSub);
}

void RPGMap::PaintFull(const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer, float W, float H, const ARPGPrototypeCharacter* Player, float Time, bool bPad)
{
	UWorld* World = Player ? Player->GetWorld() : nullptr;
	if (!World || !ARPGAdventureWorld::Find(World)) { return; }
	RPGUI::FCanvas C; C.Geometry = &Design; C.Elements = &Elements; C.Layer = Layer; C.Time = Time;
	// Half see-through over the game.
	C.At(0, 0).Box(0, 0, W, H, FLinearColor(0.005f, 0.012f, 0.03f, 0.72f));
	FMapView V;
	V.Map = MapTransform(World);
	// The whole map: every area and road, and the snowfield.
	FBox2D Bounds(ForceInit);
	for (const RPGWorld::FArea& A : RPGWorld::Areas()) { Bounds += FVector2D(A.At) - A.Radius; Bounds += FVector2D(A.At) + A.Radius; }
	for (const RPGWorld::FRoad& R : RPGWorld::Roads()) { for (const FVector& At : R.Points) { Bounds += FVector2D(At); } }
	Bounds = Bounds.ExpandBy(900);
	const float Top = 70, Bottom = 64, Side = 40;
	const FVector2D Span = Bounds.GetSize();
	V.Scale = FMath::Min((W - Side * 2) / Span.Y, (H - Top - Bottom) / Span.X);
	V.Focus = Bounds.GetCenter();
	V.Center = FVector2D(W * 0.5f, Top + (H - Top - Bottom) * 0.5f);
	// A darker sheet under the map itself, so the land reads clearly over whatever is behind.
	const FVector2D SheetA = V.Layout(FVector2D(Bounds.Max.X, Bounds.Min.Y)), SheetB = V.Layout(FVector2D(Bounds.Min.X, Bounds.Max.Y));
	ChaosImpactPaint::RoundedBox(C.At(0, 1), SheetA.X, SheetA.Y, SheetB.X - SheetA.X, SheetB.Y - SheetA.Y, 14, FLinearColor(0.02f, 0.05f, 0.11f, 0.6f));
	FPainter LandPaint = C.At(0, 2); LandPaint.Alpha = 0.95f;
	PaintLand(LandPaint, V, World, 1.5f);
	PaintFog(C.At(0, 8), V, Player, FLinearColor(0.015f, 0.025f, 0.055f, 0.86f), nullptr);
	PaintPlaceNames(C.At(1, 6), V, false, nullptr, Player);
	PaintMarks(C.At(2, 0), V, Player, Time, 7.5f, true, nullptr);
	// Title, what the line leads to, the keys.
	FPainter Title = C.At(3, 6);
	Title.Text(TEXT("マップ"), 34, 18, 26, RPGUI::TextMain, ETextAlign::Left, TEXT("Black"), 2, Navy);
	FVector Goal; FString Label;
	if (Player->GetQuestTarget(Goal, &Label))
	{
		const int32 Quest = RPGNav::GuideQuest(Player);
		const FString Line = Quest != INDEX_NONE ? FString::Printf(TEXT("%s ― %s  %dm"), RPGQuests::Get(Quest).Name, *Label, FMath::RoundToInt(RPGNav::WayLength(Player->GetQuestRoute()) / 100))
			: FString::Printf(TEXT("%s  %dm"), *Label, FMath::RoundToInt(RPGNav::WayLength(Player->GetQuestRoute()) / 100));
		const float Size = RPGUI::FitSize(Line, W - 300, 16);
		Title.Text(RPGUI::Fit(Line, W - 300, Size), 150, 26 + (16 - Size) * 0.45f, Size, RPGUI::Highlight, ETextAlign::Left, TEXT("Bold"), 2, Navy);
	}
	float KeyX = 34;
	const float KeyY = H - 46;
	const auto Hint = [&](const TCHAR* Keyboard, const TCHAR* Pad, const FString& Text, const FLinearColor& Color)
	{
		KeyX += RPGUI::Keycap(C, 3, bPad ? Pad : Keyboard, KeyX, KeyY, 24) + 6;
		RPGUI::Label(C, 3, Text, KeyX, KeyY + 2, 15, Color);
		KeyX += RPGUI::Measure(Text, 15) + 22;
	};
	Hint(TEXT("M"), TEXT("→"), TEXT("閉じる"), RPGUI::TextMain);
	FString Why;
	const bool bTravel = Player->CanFastTravel(&Why);
	Hint(TEXT("Enter"), TEXT("A"), bTravel ? FString(TEXT("ファストトラベル")) : FString::Printf(TEXT("ファストトラベル（%s）"), *Why), bTravel ? RPGUI::TextMain : RPGUI::TextDim);
	Hint(TEXT("N"), TEXT("R3"), Player->IsGuideOn() ? FString(TEXT("案内ライン オン")) : FString(TEXT("案内ライン オフ")), Player->IsGuideOn() ? RPGUI::Cyan : RPGUI::TextSub);
	// Legend.
	FPainter Legend = C.At(3, 2);
	float LX = W - 34;
	const auto Item = [&](const FString& Text, TFunctionRef<void(const FVector2D&)> Draw)
	{
		LX -= RPGUI::Measure(Text, 13);
		RPGUI::Label(C, 3, Text, LX, KeyY + 4, 13, RPGUI::TextSub);
		LX -= 16;
		Draw(FVector2D(LX, KeyY + 13));
		LX -= 22;
	};
	Item(TEXT("イベント・ネームド"), [&](const FVector2D& At) { Glyph(Legend, At, 7, FLinearColor(1.0f, 0.8f, 0.18f, 1.0f), TEXT("★")); });
	Item(TEXT("ボス"), [&](const FVector2D& At) { Glyph(Legend, At, 7, RPGUI::Bad, TEXT("!")); });
	Item(TEXT("依頼"), [&](const FVector2D& At) { Glyph(Legend, At, 7, RPGUI::Highlight, TEXT("!")); });
	Item(TEXT("休息の灯り"), [&](const FVector2D& At) { Diamond(Legend, At, 6, RPGUI::Good); });
	Item(TEXT("目的地"), [&](const FVector2D& At) { Diamond(Legend, At, 7, RPGUI::Highlight); });
}

// ---------------------------------------------------------------- the character's map and light line

void ARPGPrototypeCharacter::SetMapMode(int32 Mode)
{
	if (Mode != MapClosed && (bGrunt || bBagOpen || bQuestMenuOpen || IsEliminated() || IsInTitle())) { return; }
	MapMode = FMath::Clamp(Mode, 0, 2);
}

void ARPGPrototypeCharacter::CycleMap() { SetMapMode((MapMode + 1) % 3); }

bool ARPGPrototypeCharacter::HandleMapKey(FKey Key)
{
	if (MapMode == MapClosed) { return false; }
	if (Key == EKeys::M || Key == EKeys::Gamepad_DPad_Right) { CycleMap(); return true; }
	if (Key == EKeys::Escape) { SetMapMode(MapClosed); return true; }
	if (Key == EKeys::N || Key == EKeys::Gamepad_RightThumbstick) { SetGuideOn(!bGuideOn); return true; }
	// The surroundings stay out of the way: every other key plays on (B still dashes, A still jumps).
	if (MapMode != MapWhole) { return false; }
	if (Key == EKeys::Gamepad_FaceButton_Right) { SetMapMode(MapClosed); return true; }
	if (Key == EKeys::Enter || Key == EKeys::Gamepad_FaceButton_Bottom)
	{
		// Fast travel from the map (it stays open when travelling is not possible now; the reason is shown).
		FString Why;
		if (!CanFastTravel(&Why)) { Notice = Why; NoticeUntil = GetWorld()->GetTimeSeconds() + 2.5; return true; }
		SetMapMode(MapClosed);
		OpenFastTravel();
		return true;
	}
	return false;
}

void ARPGPrototypeCharacter::SetGuideOn(bool bOn)
{
	bGuideOn = bOn;
	TutorialEvent(TEXT("Guide"));
	if (GConfig && GetLocalRPGIndex() >= 0)
	{
		GConfig->SetBool(TEXT("ChaosImpact.RPGPrototype"), *FString::Printf(TEXT("GuideP%d"), GetLocalRPGIndex() + 1), bGuideOn, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
	// The shortest way from right here, now.
	NextRouteAt = 0;
	UpdateQuestGuide(0);
	if (!GetWorld()) { return; }
	Notice = !bGuideOn ? FString(TEXT("案内ライン オフ"))
		: bHasQuestTarget ? FString::Printf(TEXT("案内ライン オン：%s まで %dm"), *QuestTargetLabel, FMath::RoundToInt(RPGNav::WayLength(QuestRoute) / 100))
		: FString(TEXT("案内ライン オン（いまは行き先がありません）"));
	NoticeUntil = GetWorld()->GetTimeSeconds() + 2.5;
}

bool ARPGPrototypeCharacter::GetQuestTarget(FVector& OutAt, FString* OutLabel) const
{
	if (!bHasQuestTarget) { return false; }
	OutAt = QuestTargetAt;
	if (OutLabel) { *OutLabel = QuestTargetLabel; }
	return true;
}

void ARPGPrototypeCharacter::UpdateQuestGuide(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	const bool bActive = World && IsPlayerControlled() && IsLocallyControlled() && !bGrunt && !IsInTitle() && ARPGAdventureWorld::Find(World);
	if (MapMode != MapClosed && (!bActive || bBagOpen || bQuestMenuOpen || IsEliminated())) { MapMode = MapClosed; }
	ARPGQuestGuide* Guide = QuestGuide.Get();
	if (!bActive)
	{
		if (Guide) { Guide->ShowRoute({}); }
		bHasQuestTarget = false; QuestRoute.Reset();
		return;
	}
	const double Now = World->GetTimeSeconds();
	if (Now < NextRouteAt) { return; }
	NextRouteAt = Now + (bGuideOn ? 0.3 : 0.8);
	// The followed quest's goals (none under way: whoever has one to offer); the nearest of them by road.
	RPGNav::FGoal Goal;
	const int32 Quest = RPGNav::GuideQuest(this);
	const bool bAny = Quest != INDEX_NONE ? RPGNav::QuestGoals(this, Quest, Goal) : RPGNav::OfferGoals(this, Goal);
	const FVector Feet = GetActorLocation() - FVector(0, 0, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	const FVector Previous = QuestTargetAt;
	int32 Chosen = INDEX_NONE;
	QuestRoute = bAny ? RPGNav::ShortestWay(World, Feet, Goal.Candidates, &Chosen, bHasQuestTarget ? &Previous : nullptr) : TArray<FVector>();
	bHasQuestTarget = Goal.Candidates.IsValidIndex(Chosen);
	if (bHasQuestTarget)
	{
		QuestTargetAt = Goal.Candidates[Chosen];
		QuestTargetLabel = Goal.Label.IsEmpty() && Goal.Names.IsValidIndex(Chosen) ? Goal.Names[Chosen] : Goal.Label;
	}
	if (bGuideOn && !Guide)
	{
		FActorSpawnParameters Spawn;
		Spawn.Owner = this;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Spawn.ObjectFlags |= RF_Transient;
		Guide = World->SpawnActor<ARPGQuestGuide>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
		if (Guide)
		{
			const auto* Mode = World->GetAuthGameMode<ARPGPrototypeGameMode>();
			const bool bShared = Mode && Mode->UsesSharedScreen();
			Guide->SetColor(bShared ? ChaosImpactPaint::PlayerAccents[FMath::Clamp(GetLocalRPGIndex(), 0, 3)] : FLinearColor(0.25f, 0.9f, 1.0f, 1.0f));
			QuestGuide = Guide;
		}
	}
	// Nothing on the ground while down or knocked out, or almost there.
	const bool bShow = bGuideOn && !bDowned && !IsEliminated() && bHasQuestTarget;
	if (Guide) { Guide->ShowRoute(bShow ? QuestRoute : TArray<FVector>()); }
}
