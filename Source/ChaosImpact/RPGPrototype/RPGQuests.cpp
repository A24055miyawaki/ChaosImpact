#include "RPGQuests.h"
#include "RPGPrototype.h"
#include "RPGAdventure.h"
#include "RPGTuning.h"
#include "ChaosImpactPuppetComponent.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"

// ---------------------------------------------------------------- the quest table
// Append only: a quest's place in this list is its save slot. 0-2 are the original board quests.
// Text is narration: what the person seems to want ("…してほしいらしい"), never their spoken lines.

namespace
{
const TArray<FRPGQuestDef>& QuestTable()
{
	static const TArray<FRPGQuestDef> Table{
		{TEXT("門の外の安全確認"), ERPGQuestKind::Hunt, ERPGQuestFrom::Board, 0,
			TEXT("掲示板に門番ロウの依頼が貼られている。コロニーの外をうろつく敵を4体ほど片付けて、安全を確かめてほしいらしい。とどめでもダメージの手伝いでも数に入る。"),
			TEXT("外の敵を倒す"), 4, nullptr, nullptr, -1, 1, 90, 3, ERPGItemKind::Ball, EChaosImpactBallType::Wind, 1, nullptr},
		{TEXT("流された補給箱"), ERPGQuestKind::Gather, ERPGQuestFrom::Board, 0,
			TEXT("川の増水でコロニーの補給箱が3つ流されたらしい。採石場・川沿い・奥の高台に流れ着いた青い箱を調べてきてほしいようだ。"),
			TEXT("補給箱を調べる"), 3, nullptr, nullptr, -1, 1, 120, 3, ERPGItemKind::LifeBadge, EChaosImpactBallType::Normal, 1, nullptr},
		{TEXT("古い中継塔の灯り"), ERPGQuestKind::Survey, ERPGQuestFrom::Board, 0,
			TEXT("北の中継塔の灯りが消えたままらしい。塔と、その先にある遺跡の様子を見てきてほしいと書かれている。"),
			TEXT("塔と遺跡を調べる"), 2, nullptr, nullptr, -1, 1, 150, 4, ERPGItemKind::Ball, EChaosImpactBallType::Simae, 2, nullptr},
		{TEXT("採石場の掃除"), ERPGQuestKind::AreaHunt, ERPGQuestFrom::Person, 2,
			TEXT("ガンテは、採石場の底に住みついた敵のせいで石を切り出せずに困っているらしい。採石場で5体ほど追い払ってほしそうだ。"),
			TEXT("採石場の敵を倒す"), 5, TEXT("採石場"), nullptr, 0, 1, 140, 3, ERPGItemKind::Glove, EChaosImpactBallType::Normal, 1, nullptr},
		{TEXT("宝箱めぐり"), ERPGQuestKind::Treasure, ERPGQuestFrom::Person, 1,
			TEXT("ロウは、昔の探検家が各地に宝箱を隠したという噂を気にしているらしい。どこの宝箱でもいいので、3つ開けて確かめてきてほしいようだ。"),
			TEXT("宝箱を開ける"), 3, nullptr, nullptr, -1, 1, 100, 2, ERPGItemKind::Pouch, EChaosImpactBallType::Normal, 1, nullptr},
		{TEXT("水晶洞窟の主"), ERPGQuestKind::Boss, ERPGQuestFrom::Person, 3,
			TEXT("ミナは、水晶洞窟の奥で暴れる巨大な魚を調べているらしい。遺跡の調査が済んだ今、誰かにあれを鎮めてきてほしいと考えているようだ。"),
			TEXT("暴走マグロを倒す"), 1, TEXT("水晶洞窟"), nullptr, 2, 1, 400, 6, ERPGItemKind::Ball, EChaosImpactBallType::Nova, 2, nullptr},
		{TEXT("休息の灯りをつなぐ"), ERPGQuestKind::Beacon, ERPGQuestFrom::Beacon, 0,
			TEXT("灯りの台座に、古い巡礼の書き付けが残っていた。各地の休息の灯りを3つともして、道をつないでほしいという願いが書かれているようだ。"),
			TEXT("休息の灯りをともす"), 3, nullptr, nullptr, -1, 1, 80, 2, ERPGItemKind::LifeBadge, EChaosImpactBallType::Normal, 1, TEXT("休息の灯りを初めてともす")},
		{TEXT("工房の初仕事"), ERPGQuestKind::Workshop, ERPGQuestFrom::Merchant, 0,
			TEXT("ルビー商人は、新しい客に工房の使い方を覚えてほしいらしい。装備を1回強化して、ボールにチューンを1つ付けてみてほしいようだ。"),
			TEXT("強化とチューンを試す"), 2, nullptr, nullptr, -1, 1, 120, 2, ERPGItemKind::Material, EChaosImpactBallType::Normal, 0, TEXT("ルビー商人に初めて話しかける")},
		{TEXT("峡谷の向こう側"), ERPGQuestKind::Explore, ERPGQuestFrom::Region, 0,
			TEXT("峡谷の入口の岩に、先に進んだ旅人の目印が刻まれている。見晴らし台・野営跡・大水門をたどってみてほしいと誘っているようだ。"),
			TEXT("目印の場所を訪ねる"), 3, TEXT("峡谷の見晴らし台|旅人の野営跡|忘れられた大水門"), TEXT("水脈峡谷"), -1, 1, 160, 4, ERPGItemKind::MobilityBadge, EChaosImpactBallType::Normal, 2, TEXT("水脈峡谷に足を踏み入れる")},
		{TEXT("はぐれ投手の手配書"), ERPGQuestKind::Bounty, ERPGQuestFrom::BountyDrop, 0,
			TEXT("倒した敵が手配書を落とした。遠くからボールを投げてくる投手役の敵を3体倒せば、懸賞金がもらえるらしい。"),
			TEXT("投手役の敵を倒す"), 3, nullptr, nullptr, -1, 2, 130, 3, ERPGItemKind::Ball, EChaosImpactBallType::Nova, 1, TEXT("敵が落とす手配書を拾う（Lv2から、まれ）")},
		{TEXT("特殊球の試し打ち"), ERPGQuestKind::Special, ERPGQuestFrom::FirstSpecial, 0,
			TEXT("手に入れた特殊球に、前の持ち主のメモが結ばれていた。特殊球で敵を3体倒して、使い心地を確かめてほしいらしい。"),
			TEXT("特殊球で敵を倒す"), 3, nullptr, nullptr, -1, 1, 70, 2, ERPGItemKind::TechniqueBadge, EChaosImpactBallType::Normal, 1, TEXT("特殊球を初めて手に入れる")},
		{TEXT("峡谷の見張り"), ERPGQuestKind::AreaHunt, ERPGQuestFrom::Person, 4,
			TEXT("ソラは野営跡を拠点に峡谷を旅しているらしい。道をふさぐ敵が多くて先へ進めないので、峡谷や大水門のあたりで6体ほど減らしてほしいようだ。"),
			TEXT("峡谷の敵を倒す"), 6, TEXT("峡谷|大水門|水源|野営跡"), nullptr, -1, 1, 220, 4, ERPGItemKind::Supporter, EChaosImpactBallType::Normal, 2, nullptr},
		// 2026-10-20: more hunts across the map, and the snowfield.
		{TEXT("霧の森の見回り"), ERPGQuestKind::AreaHunt, ERPGQuestFrom::Board, 0,
			TEXT("掲示板に、霧の古森で薪を集める人たちの頼みが貼られている。森にいる敵を6体ほど減らして、作業できるようにしてほしいらしい。"),
			TEXT("霧の古森の敵を倒す"), 6, TEXT("霧の古森|古森の奥"), nullptr, 0, 1, 130, 3, ERPGItemKind::Shoes, EChaosImpactBallType::Normal, 1, nullptr},
		{TEXT("川辺の見張り"), ERPGQuestKind::AreaHunt, ERPGQuestFrom::Person, 1,
			TEXT("ロウは、草原や入り江の方から敵が門に近づいてくるのを心配しているらしい。川辺の敵を6体ほど追い払ってほしいようだ。"),
			TEXT("草原・入り江の敵を倒す"), 6, TEXT("せせらぎの草原|さざなみの入り江|入り江の小島|花風の丘|風車の丘"), nullptr, -1, 1, 130, 3, ERPGItemKind::Ball, EChaosImpactBallType::Ice, 1, nullptr},
		{TEXT("遺跡を荒らす群れ"), ERPGQuestKind::AreaHunt, ERPGQuestFrom::Person, 3,
			TEXT("ミナは、遺跡や中継塔のまわりをうろつく敵のせいで調査が進まないらしい。8体ほど片付けてほしいようだ。"),
			TEXT("遺跡・中継塔の敵を倒す"), 8, TEXT("水脈の遺跡|風見の中継塔|分かれ道"), nullptr, -1, 1, 190, 4, ERPGItemKind::TechniqueBadge, EChaosImpactBallType::Normal, 2, nullptr},
		{TEXT("水晶洞窟の掃討"), ERPGQuestKind::AreaHunt, ERPGQuestFrom::Person, 3,
			TEXT("ミナは、水晶洞窟の奥まで安全に調べたいらしい。洞窟にいる敵を10体ほど倒して、道を開いてほしいようだ。"),
			TEXT("水晶洞窟の敵を倒す"), 10, TEXT("水晶洞窟"), nullptr, 14, 1, 260, 5, ERPGItemKind::Ball, EChaosImpactBallType::Nova, 2, nullptr},
		{TEXT("大討伐"), ERPGQuestKind::Hunt, ERPGQuestFrom::Board, 0,
			TEXT("掲示板の真ん中に、コロニー全体からの大きな依頼が貼られている。どこでもいいので敵を30体倒し、外の勢いを弱めてほしいらしい。"),
			TEXT("敵を倒す（どこでも）"), 30, nullptr, nullptr, -1, 4, 450, 8, ERPGItemKind::Ball, EChaosImpactBallType::Nova, 3, nullptr},
		{TEXT("雪原の狩り"), ERPGQuestKind::AreaHunt, ERPGQuestFrom::Person, 5,
			TEXT("ユキは雪原の野営地を守っているらしい。吹雪にまぎれて近づく敵を8体ほど減らしてほしいようだ。"),
			TEXT("雪原の敵を倒す"), 8, TEXT("雪原"), nullptr, -1, 1, 240, 5, ERPGItemKind::Ball, EChaosImpactBallType::Snow, 2, nullptr},
		{TEXT("粉雪の主"), ERPGQuestKind::Boss, ERPGQuestFrom::Person, 5,
			TEXT("ユキによると、雪原の奥の巣に巨大なシマエナガが棲んでいるらしい。追い詰められると炎をまとって暴れ出すので、気をつけて鎮めてほしいようだ。"),
			TEXT("シマエナガを倒す"), 1, TEXT("雪原"), nullptr, 17, 1, 600, 8, ERPGItemKind::Ball, EChaosImpactBallType::Simae, 3, nullptr},
	};
	return Table;
}

bool PlaceMatches(const FString& Place, const TCHAR* Keywords)
{
	if (!Keywords || Place.IsEmpty()) { return false; }
	TArray<FString> Words;
	FString(Keywords).ParseIntoArray(Words, TEXT("|"));
	for (const FString& Word : Words) { if (Place.Contains(Word)) { return true; } }
	return false;
}

int32 PlaceIndex(const FString& Place, const TCHAR* Keywords)
{
	if (!Keywords) { return INDEX_NONE; }
	TArray<FString> Words;
	FString(Keywords).ParseIntoArray(Words, TEXT("|"));
	for (int32 Index = 0; Index < Words.Num(); ++Index) { if (Place.Contains(Words[Index])) { return Index; } }
	return INDEX_NONE;
}
}

int32 RPGQuests::Num() { return QuestTable().Num(); }
const FRPGQuestDef& RPGQuests::Get(int32 Index) { return QuestTable()[FMath::Clamp(Index, 0, Num() - 1)]; }
const TArray<FRPGQuestGiverDef>& RPGQuests::Givers()
{
	static const TArray<FRPGQuestGiverDef> List{
		{TEXT("コロニーの依頼掲示板"), TEXT("掲示板"), TEXT("掲示板に新しい依頼は貼られていないようだ。"), FVector2D::ZeroVector, 0},
		{TEXT("門番 ロウ"), TEXT("コロニーの門番"), TEXT("ロウは門の外をじっと見張っている。今は頼みごとはないようだ。"), FVector2D(1500, -420), 0},
		{TEXT("石切り職人 ガンテ"), TEXT("採石場の職人"), TEXT("ガンテは黙々と石を磨いている。今は手が足りているらしい。"), FVector2D(8650, -5250), 1},
		{TEXT("研究者 ミナ"), TEXT("遺跡の調査員"), TEXT("ミナは古い石板の記録に夢中のようだ。"), FVector2D(15800, -1450), 2},
		{TEXT("旅人 ソラ"), TEXT("峡谷の旅人"), TEXT("ソラは焚き火のそばで地図を広げている。"), FVector2D(22350, 4950), 4},
		// Stands in the snowfield (placed by ARPGSnowField, not by position here).
		{TEXT("見張り番 ユキ"), TEXT("雪原の見張り番"), TEXT("ユキは白い息を吐きながら雪原を見渡している。"), FVector2D::ZeroVector, 1},
	};
	return List;
}
FString RPGQuests::KindName(ERPGQuestKind Kind)
{
	switch (Kind)
	{
	case ERPGQuestKind::Hunt: case ERPGQuestKind::AreaHunt: return TEXT("討伐");
	case ERPGQuestKind::Gather: return TEXT("回収");
	case ERPGQuestKind::Survey: return TEXT("調査");
	case ERPGQuestKind::Explore: return TEXT("探索");
	case ERPGQuestKind::Treasure: return TEXT("宝探し");
	case ERPGQuestKind::Boss: return TEXT("ボス");
	case ERPGQuestKind::Workshop: return TEXT("工房");
	case ERPGQuestKind::Beacon: return TEXT("巡礼");
	case ERPGQuestKind::Bounty: return TEXT("手配");
	default: return TEXT("腕試し");
	}
}
FLinearColor RPGQuests::KindColor(ERPGQuestKind Kind)
{
	switch (Kind)
	{
	case ERPGQuestKind::Hunt: case ERPGQuestKind::AreaHunt: case ERPGQuestKind::Bounty: return FLinearColor(1.0f, 0.42f, 0.32f, 1.0f);
	case ERPGQuestKind::Boss: return FLinearColor(1.0f, 0.2f, 0.45f, 1.0f);
	case ERPGQuestKind::Gather: case ERPGQuestKind::Treasure: return FLinearColor(1.0f, 0.76f, 0.2f, 1.0f);
	case ERPGQuestKind::Survey: case ERPGQuestKind::Explore: case ERPGQuestKind::Beacon: return FLinearColor(0.25f, 0.85f, 1.0f, 1.0f);
	default: return FLinearColor(0.72f, 0.5f, 1.0f, 1.0f);
	}
}
bool RPGQuests::ReportsToGiver(int32 Index) { const auto F0 = Get(Index).From; return F0 == ERPGQuestFrom::Board || F0 == ERPGQuestFrom::Person; }
int32 RPGQuests::FlagMask(int32 Index)
{
	switch (Get(Index).Kind)
	{
	case ERPGQuestKind::Gather: case ERPGQuestKind::Explore: return 7;
	case ERPGQuestKind::Survey: case ERPGQuestKind::Workshop: return 3;
	case ERPGQuestKind::Beacon: return 255;
	default: return 0;
	}
}
ERPGQuestState RPGQuests::State(const ARPGPrototypeCharacter* Player, int32 Index)
{
	if (!Player || !Player->Quests.IsValidIndex(Index)) { return ERPGQuestState::Hidden; }
	const auto& Q = Player->Quests[Index];
	if (Q.Stage >= 3) { return ERPGQuestState::Done; }
	if (Q.Stage == 2) { return ERPGQuestState::Ready; }
	if (Q.Stage == 1) { return ERPGQuestState::Active; }
	const auto& D = Get(Index);
	if (!ReportsToGiver(Index)) { return Q.bDiscovered ? ERPGQuestState::Offered : ERPGQuestState::Hidden; }
	const bool bAfter = D.AfterQuest < 0 || (Player->Quests.IsValidIndex(D.AfterQuest) && Player->Quests[D.AfterQuest].Stage >= 3);
	return bAfter && Player->GetRPGLevel() >= D.MinLevel ? ERPGQuestState::Offered : ERPGQuestState::Locked;
}
FString RPGQuests::UnlockHint(int32 Index)
{
	const auto& D = Get(Index);
	if (D.AfterQuest >= 0) { return FString::Printf(TEXT("「%s」を終えると受けられる"), Get(D.AfterQuest).Name); }
	if (D.MinLevel > 1) { return FString::Printf(TEXT("Lv.%d になると受けられる"), D.MinLevel); }
	return TEXT("");
}
FString RPGQuests::Progress(const ARPGPrototypeCharacter* Player, int32 Index)
{
	const auto S = State(Player, Index);
	const auto& D = Get(Index);
	if (S == ERPGQuestState::Done) { return TEXT("完了"); }
	if (S == ERPGQuestState::Ready) { return ReportsToGiver(Index) ? FString::Printf(TEXT("%s へ報告"), Givers()[FMath::Clamp(D.Giver, 0, Givers().Num() - 1)].Name) : FString(TEXT("報酬を受け取れる")); }
	if (S == ERPGQuestState::Active) { return FString::Printf(TEXT("%s　%d / %d"), D.Objective, Player->Quests[Index].Count, D.Goal); }
	if (S == ERPGQuestState::Locked) { return UnlockHint(Index); }
	return TEXT("まだ受けていない");
}
FString RPGQuests::RewardText(int32 Index)
{
	const auto& D = Get(Index);
	FString Text = FString::Printf(TEXT("ルビー%d・XP%d"), D.Rubies, D.XP);
	if (D.RewardKind != ERPGItemKind::Material)
	{
		const TCHAR* Rarity[] = {TEXT("ノーマル"), TEXT("レア"), TEXT("ユニーク"), TEXT("レジェンド")};
		const FString Item = D.RewardKind == ERPGItemKind::Ball ? (D.RewardBall == EChaosImpactBallType::Nova ? FString(TEXT("特殊球")) : FString(ChaosImpactBallTypes::GetDisplayName(D.RewardBall)))
			: RPGEquipment::KindName(D.RewardKind);
		Text += FString::Printf(TEXT("・%s%s"), Rarity[FMath::Clamp(D.RewardRarity, 0, 3)], *Item);
	}
	return Text;
}
FString RPGQuests::SourceText(int32 Index)
{
	const auto& D = Get(Index);
	if (ReportsToGiver(Index)) { return FString::Printf(TEXT("依頼主：%s"), Givers()[FMath::Clamp(D.Giver, 0, Givers().Num() - 1)].Name); }
	return FString::Printf(TEXT("きっかけ：%s　（達成するとその場で報酬）"), D.FoundBy ? D.FoundBy : TEXT("フィールド"));
}
FRPGPrototypeItem RPGQuests::MakeReward(int32 Index, int32 Level)
{
	const auto& D = Get(Index);
	const auto Rarity = static_cast<ERPGPrototypeRarity>(FMath::Clamp(D.RewardRarity, 0, 3));
	if (D.RewardKind == ERPGItemKind::Material) { return FRPGPrototypeItem{}; }
	if (D.RewardKind == ERPGItemKind::Ball)
	{
		EChaosImpactBallType Type = D.RewardBall;
		if (Type == EChaosImpactBallType::Nova)
		{
			auto Roster = RPGEquipment::BallRoster(); Roster.Remove(EChaosImpactBallType::Normal);
			Type = Roster[FMath::RandRange(0, Roster.Num() - 1)];
		}
		return FRPGPrototypeItem::Make(Type, Level, Rarity);
	}
	if (RPGEquipment::IsGear(D.RewardKind)) { return FRPGPrototypeItem::MakeGear(D.RewardKind, Level, Rarity); }
	return FRPGPrototypeItem::MakeBadge(D.RewardKind, Level, Rarity);
}

// ---------------------------------------------------------------- quest givers

ARPGQuestGiver::ARPGQuestGiver()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("GiverRoot")));
	Body = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Giver"));
	Body->SetupAttachment(GetRootComponent());
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> Mesh(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple"));
	if (Mesh.Succeeded()) { Body->SetSkeletalMesh(Mesh.Object); }
	Body->SetRelativeRotation(FRotator(0, -90, 0));
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}
void ARPGQuestGiver::BeginPlay()
{
	Super::BeginPlay();
	if (auto* Anim = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle.MM_Idle"))) { Body->PlayAnimation(Anim, true); }
	const int32 Look = RPGQuests::Givers().IsValidIndex(GiverId) ? RPGQuests::Givers()[GiverId].Look : 0;
	auto* Puppet = NewObject<UChaosImpactPuppetComponent>(this);
	Puppet->SetupAttachment(Body); Puppet->SetRelativeScale3D(FVector(1.45f)); Puppet->RegisterComponent();
	if (Puppet->Initialize(Body, Look)) { Body->SetHiddenInGame(true); Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones; }
	else { Puppet->DestroyComponent(); }
}
void ARPGQuestGiver::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// Turn towards whoever is close, like the merchant.
	if (const auto* P = UGameplayStatics::GetPlayerPawn(this, 0); P && FVector::DistSquared2D(P->GetActorLocation(), GetActorLocation()) < FMath::Square(500.0f))
	{ SetActorRotation(FMath::RInterpTo(GetActorRotation(), (P->GetActorLocation() - GetActorLocation()).GetSafeNormal2D().Rotation(), DeltaSeconds, 3)); }
}
bool ARPGQuestGiver::CanTalk(const ARPGPrototypeCharacter* Player) const
{
	if (!Player || Player->IsGrunt() || Player->IsEliminated() || Player->IsBagOpen() || Player->IsQuestMenuOpen()
		|| FVector::DistSquared2D(Player->GetActorLocation(), GetActorLocation()) > FMath::Square(260.0f)
		|| FMath::Abs(Player->GetActorLocation().Z - GetActorLocation().Z) > 220) { return false; }
	FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(RPGQuestGiver), false, Player); Params.AddIgnoredActor(this);
	return !GetWorld()->LineTraceSingleByObjectType(Hit, Player->GetActorLocation(), GetActorLocation() + FVector(0, 0, 90), FCollisionObjectQueryParams(ECC_WorldStatic), Params);
}
ARPGQuestGiver* ARPGQuestGiver::Nearest(const ARPGPrototypeCharacter* Player)
{
	ARPGQuestGiver* Best = nullptr; float Distance = MAX_flt;
	if (!Player || !Player->GetWorld()) { return nullptr; }
	for (TActorIterator<ARPGQuestGiver> It(Player->GetWorld()); It; ++It)
	{ const float D = FVector::DistSquared2D(Player->GetActorLocation(), It->GetActorLocation()); if (D < Distance && It->CanTalk(Player)) { Best = *It; Distance = D; } }
	return Best;
}
void ARPGQuestGiver::SpawnDefaults(UWorld* World, const FTransform& MapOrigin)
{
	if (!World || TActorIterator<ARPGQuestGiver>(World)) { return; }
	const auto& Givers = RPGQuests::Givers();
	for (int32 Index = 1; Index < Givers.Num(); ++Index)
	{
		if (Givers[Index].At.IsNearlyZero()) { continue; }
		// Stand on whatever ground is there (the map can be re-sculpted without moving them by hand).
		const FVector Top = MapOrigin.TransformPosition(FVector(Givers[Index].At.X, Givers[Index].At.Y, 6000));
		FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(RPGGiverGround), false);
		if (!World->LineTraceSingleByObjectType(Hit, Top, Top - FVector(0, 0, 12000), FCollisionObjectQueryParams(ECC_WorldStatic), Params)) { continue; }
		FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (auto* Giver = World->SpawnActor<ARPGQuestGiver>(Hit.ImpactPoint + FVector(0, 0, 2), FRotator(0, 180, 0), Spawn)) { Giver->GiverId = Index; }
	}
}

// ---------------------------------------------------------------- the player's quests

void ARPGPrototypeCharacter::ShowQuestBanner(const FString& Label, int32 Index)
{ QuestBannerLabel = Label; QuestBannerQuest = Index; QuestBannerAt = GetWorld()->GetTimeSeconds(); }

void ARPGPrototypeCharacter::DiscoverQuest(int32 Index)
{
	if (!Quests.IsValidIndex(Index) || Quests[Index].Stage != 0 || Quests[Index].bDiscovered || RPGQuests::ReportsToGiver(Index)) { return; }
	Quests[Index].bDiscovered = true;
	ShowQuestBanner(TEXT("新しい依頼の話"), Index);
}

bool ARPGPrototypeCharacter::AcceptQuest(int32 Index)
{
	if (bGrunt || IsEliminated() || RPGQuests::State(this, Index) != ERPGQuestState::Offered) { return false; }
	auto& Q = Quests[Index]; Q.Stage = 1; Q.Count = 0; Q.Flags = 0;
	if (TrackedQuest == INDEX_NONE || RPGQuests::State(this, TrackedQuest) != ERPGQuestState::Active) { TrackedQuest = Index; }
	bQuestConfirm = false;
	ShowQuestBanner(TEXT("依頼を受けた"), Index);
	Notice = FString(RPGQuests::Get(Index).Name) + TEXT("を受けました"); NoticeUntil = GetWorld()->GetTimeSeconds() + 3;
	return true;
}

bool ARPGPrototypeCharacter::ClaimQuest(int32 Index)
{
	if (!Quests.IsValidIndex(Index) || Quests[Index].Stage != 2 || IsEliminated()) { return false; }
	const auto& D = RPGQuests::Get(Index);
	const FRPGPrototypeItem Item = RPGQuests::MakeReward(Index, RPGLevel);
	if (Item.IsValid() && !AddLoot(Item))
	{ Notice = TEXT("バッグがいっぱいです。分解してから報酬を受け取ってください"); NoticeUntil = GetWorld()->GetTimeSeconds() + 3; return false; }
	Quests[Index].Stage = 3;
	AddRubies(D.Rubies); GainRPGExperience(D.XP);
	if (TrackedQuest == Index) { TrackedQuest = INDEX_NONE; }
	ShowQuestBanner(TEXT("依頼達成！"), Index);
	Notice = FString(D.Name) + TEXT(" 達成！ 報酬を受け取りました"); NoticeUntil = GetWorld()->GetTimeSeconds() + 4;
	return true;
}

void ARPGPrototypeCharacter::AdvanceQuest(int32 Index, int32 Count)
{
	auto& Q = Quests[Index]; const auto& D = RPGQuests::Get(Index);
	Q.Count = FMath::Clamp(Count, 0, D.Goal);
	if (Q.Count < D.Goal) { return; }
	Q.Stage = 2;
	// Field quests pay out where they are finished; the others wait for the report.
	if (!RPGQuests::ReportsToGiver(Index)) { ClaimQuest(Index); return; }
	ShowQuestBanner(TEXT("報告できる"), Index);
	Notice = FString::Printf(TEXT("%s：%s"), D.Name, *RPGQuests::Progress(this, Index)); NoticeUntil = GetWorld()->GetTimeSeconds() + 3;
}

void ARPGPrototypeCharacter::QuestEvent(ERPGQuestEvent Event, const FVector& At, int32 Detail, const FString& Place)
{
	auto* Adventure = ARPGAdventureWorld::Find(GetWorld());
	if (bGrunt || !Adventure) { return; }
	if (Quests.Num() < RPGQuests::Num()) { Quests.SetNum(RPGQuests::Num()); }
	// 1. Finding field quests.
	for (int32 Index = 0; Index < RPGQuests::Num(); ++Index)
	{
		const auto& D = RPGQuests::Get(Index);
		const bool bFound = (Event == ERPGQuestEvent::BeaconLit && D.From == ERPGQuestFrom::Beacon)
			|| (Event == ERPGQuestEvent::MerchantVisited && D.From == ERPGQuestFrom::Merchant)
			|| (Event == ERPGQuestEvent::RegionEntered && D.From == ERPGQuestFrom::Region && PlaceMatches(Place, D.Trigger))
			|| (Event == ERPGQuestEvent::ItemObtained && D.From == ERPGQuestFrom::FirstSpecial && (Detail & 1))
			|| (Event == ERPGQuestEvent::Kill && D.From == ERPGQuestFrom::BountyDrop && RPGLevel >= D.MinLevel && FMath::FRand() < .2f);
		if (bFound) { DiscoverQuest(Index); }
	}
	// 2. Progress of accepted quests.
	const FString KillPlace = Event == ERPGQuestEvent::Kill ? Adventure->RegionName(At) : FString();
	for (int32 Index = 0; Index < RPGQuests::Num(); ++Index)
	{
		auto& Q = Quests[Index];
		if (Q.Stage != 1) { continue; }
		const auto& D = RPGQuests::Get(Index);
		int32 Bit = INDEX_NONE;
		switch (D.Kind)
		{
		case ERPGQuestKind::Hunt: if (Event == ERPGQuestEvent::Kill) { AdvanceQuest(Index, Q.Count + 1); } break;
		case ERPGQuestKind::AreaHunt: if (Event == ERPGQuestEvent::Kill && PlaceMatches(KillPlace, D.Places)) { AdvanceQuest(Index, Q.Count + 1); } break;
		case ERPGQuestKind::Boss: if (Event == ERPGQuestEvent::Kill && (Detail & 1) && (!D.Places || PlaceMatches(KillPlace, D.Places))) { AdvanceQuest(Index, Q.Count + 1); } break;
		case ERPGQuestKind::Bounty: if (Event == ERPGQuestEvent::Kill && (Detail & 2)) { AdvanceQuest(Index, Q.Count + 1); } break;
		case ERPGQuestKind::Special: if (Event == ERPGQuestEvent::Kill && (Detail & 4)) { AdvanceQuest(Index, Q.Count + 1); } break;
		case ERPGQuestKind::Treasure: if (Event == ERPGQuestEvent::ChestOpened) { AdvanceQuest(Index, Q.Count + 1); } break;
		case ERPGQuestKind::Workshop: Bit = Event == ERPGQuestEvent::Upgraded ? 0 : Event == ERPGQuestEvent::Tuned ? 1 : INDEX_NONE; break;
		case ERPGQuestKind::Beacon: Bit = Event == ERPGQuestEvent::BeaconLit ? FMath::Clamp(Detail, 0, 7) : INDEX_NONE; break;
		case ERPGQuestKind::Explore: Bit = Event == ERPGQuestEvent::RegionEntered ? PlaceIndex(Place, D.Places) : INDEX_NONE; break;
		default: break;
		}
		if (Bit != INDEX_NONE && !(Q.Flags & (1 << Bit)))
		{
			Q.Flags |= 1 << Bit;
			if (Q.Stage == 1 && FMath::CountBits(static_cast<uint64>(Q.Flags)) < D.Goal)
			{ Notice = FString::Printf(TEXT("%s　%d / %d"), D.Objective, FMath::CountBits(static_cast<uint64>(Q.Flags)), D.Goal); NoticeUntil = GetWorld()->GetTimeSeconds() + 2.5; }
			AdvanceQuest(Index, FMath::CountBits(static_cast<uint64>(Q.Flags)));
		}
	}
}

void ARPGPrototypeCharacter::AdvanceAdventureKill() { QuestEvent(ERPGQuestEvent::Kill, GetActorLocation()); }

TArray<int32> ARPGPrototypeCharacter::QuestPanelList() const
{
	TArray<int32> Out;
	if (QuestGiver == TravelPanel || QuestGiver == MembersPanel) { return Out; }
	const auto Order = [this](int32 Index)
	{
		switch (RPGQuests::State(this, Index))
		{
		case ERPGQuestState::Ready: return 0;
		case ERPGQuestState::Offered: return 1;
		case ERPGQuestState::Active: return 2;
		case ERPGQuestState::Locked: return 3;
		default: return 4;
		}
	};
	for (int32 Index = 0; Index < RPGQuests::Num() && Quests.IsValidIndex(Index); ++Index)
	{
		const auto S = RPGQuests::State(this, Index);
		if (S == ERPGQuestState::Hidden) { continue; }
		if (QuestGiver >= 0)
		{
			// Talking to someone: their own quests only.
			if (RPGQuests::ReportsToGiver(Index) && RPGQuests::Get(Index).Giver == QuestGiver) { Out.Add(Index); }
			continue;
		}
		const int32 Tab = S == ERPGQuestState::Active || S == ERPGQuestState::Ready ? 0 : S == ERPGQuestState::Offered || S == ERPGQuestState::Locked ? 1 : 2;
		if (Tab == QuestTab) { Out.Add(Index); }
	}
	Out.StableSort([&](int32 A, int32 B) { return Order(A) < Order(B); });
	return Out;
}

bool ARPGPrototypeCharacter::OpenQuestPanel(int32 Giver)
{
	if (bGrunt || IsEliminated() || IsThrowReleasePending() || IsDriving()) { return false; }
	if (bBagOpen) { SetBagOpen(false); }
	QuestGiver = Giver; bQuestConfirm = false;
	if (!bQuestMenuOpen)
	{
		bQuestMenuOpen = true; SetTrainingMenuFrozen(true);
		if (auto* PC = Cast<APlayerController>(GetController())) { PC->SetInputMode(FInputModeGameOnly().SetConsumeCaptureMouseDown(false)); }
	}
	const auto List = QuestPanelList();
	if (!List.Contains(QuestSelection)) { QuestSelection = List.Num() ? List[0] : 0; }
	return true;
}
bool ARPGPrototypeCharacter::OpenQuestJournal()
{
	if (!ARPGAdventureWorld::Find(GetWorld())) { return false; }
	// Open on the tab that has something to look at.
	QuestTab = 0; QuestGiver = INDEX_NONE;
	if (QuestPanelList().IsEmpty()) { QuestTab = 1; }
	return OpenQuestPanel(INDEX_NONE);
}
bool ARPGPrototypeCharacter::TalkToGiver(int32 Giver) { return OpenQuestPanel(Giver); }

void ARPGPrototypeCharacter::SetQuestMenuOpen(bool Open)
{
	if (bQuestMenuOpen == Open) { return; }
	if (Open)
	{
		const auto* Node = ARPGAdventureNode::Nearest(this);
		if (!Node || Node->Kind != ERPGAdventureNodeKind::Board) { return; }
		OpenQuestPanel(0); return;
	}
	bQuestMenuOpen = false; bQuestConfirm = false; SetTrainingMenuFrozen(false);
	if (auto* PC = Cast<APlayerController>(GetController())) { PC->SetInputMode(FInputModeGameOnly().SetConsumeCaptureMouseDown(false)); }
}

void ARPGPrototypeCharacter::ToggleQuestTrack(int32 Index)
{
	const auto S = RPGQuests::State(this, Index);
	if (S != ERPGQuestState::Active && S != ERPGQuestState::Ready) { return; }
	TrackedQuest = TrackedQuest == Index ? INDEX_NONE : Index;
}

void ARPGPrototypeCharacter::ActivateQuest(int32 Index)
{
	if (!bQuestMenuOpen || !Quests.IsValidIndex(Index) || IsEliminated()) { return; }
	QuestSelection = Index;
	const auto S = RPGQuests::State(this, Index);
	const bool bHere = QuestGiver >= 0 && RPGQuests::Get(Index).Giver == QuestGiver;
	if (S == ERPGQuestState::Offered)
	{
		// From a person or the board the panel itself asks "受ける / やめておく"; in the journal a field quest asks once more.
		if (RPGQuests::ReportsToGiver(Index) && !bHere)
		{ Notice = FString::Printf(TEXT("%s に話しかけると受けられます"), RPGQuests::Givers()[RPGQuests::Get(Index).Giver].Name); NoticeUntil = GetWorld()->GetTimeSeconds() + 3; return; }
		AcceptQuest(Index); return;
	}
	if (S == ERPGQuestState::Ready)
	{
		if (RPGQuests::ReportsToGiver(Index) && !bHere)
		{ Notice = FString::Printf(TEXT("%s へ報告しよう"), RPGQuests::Givers()[RPGQuests::Get(Index).Giver].Name); NoticeUntil = GetWorld()->GetTimeSeconds() + 3; return; }
		ClaimQuest(Index);
	}
}

bool ARPGPrototypeCharacter::HandleQuestKey(FKey Key)
{
	if (!bQuestMenuOpen) { return false; }
	if (QuestGiver == TravelPanel) { return HandleTravelKey(Key); }
	if (QuestGiver == MembersPanel) { return HandleMembersKey(Key); }
	if (Key == EKeys::W) { Key = EKeys::Up; } else if (Key == EKeys::S) { Key = EKeys::Down; } else if (Key == EKeys::A) { Key = EKeys::Left; } else if (Key == EKeys::D) { Key = EKeys::Right; }
	const bool bBack = Key == EKeys::Escape || Key == EKeys::Tab || Key == EKeys::Gamepad_FaceButton_Right || Key == EKeys::Gamepad_Special_Left;
	const bool bOk = Key == EKeys::Enter || Key == EKeys::SpaceBar || Key == EKeys::Gamepad_FaceButton_Bottom;
	if (bQuestConfirm)
	{
		if (bBack) { bQuestConfirm = false; }
		if (bOk) { AcceptQuest(QuestSelection); }
		return true;
	}
	if (bBack || (Key == EKeys::J && QuestGiver < 0)) { SetQuestMenuOpen(false); return true; }
	const auto List = QuestPanelList();
	const int32 At = List.Find(QuestSelection);
	if (Key == EKeys::Up || Key == EKeys::Gamepad_DPad_Up || Key == EKeys::MouseScrollUp || Key == EKeys::Down || Key == EKeys::Gamepad_DPad_Down || Key == EKeys::MouseScrollDown)
	{
		const int32 Step = Key == EKeys::Up || Key == EKeys::Gamepad_DPad_Up || Key == EKeys::MouseScrollUp ? -1 : 1;
		if (List.Num()) { QuestSelection = List[FMath::Clamp((At < 0 ? 0 : At) + Step, 0, List.Num() - 1)]; }
		return true;
	}
	if (QuestGiver < 0 && (Key == EKeys::Left || Key == EKeys::Right || Key == EKeys::Gamepad_DPad_Left || Key == EKeys::Gamepad_DPad_Right
		|| Key == EKeys::Q || Key == EKeys::E || Key == EKeys::Gamepad_LeftShoulder || Key == EKeys::Gamepad_RightShoulder))
	{
		const bool bLeft = Key == EKeys::Left || Key == EKeys::Gamepad_DPad_Left || Key == EKeys::Q || Key == EKeys::Gamepad_LeftShoulder;
		SetQuestTab((QuestTab + (bLeft ? 2 : 1)) % 3); return true;
	}
	if (Key == EKeys::T || Key == EKeys::Gamepad_FaceButton_Top) { ToggleQuestTrack(QuestSelection); return true; }
	if (bOk) { RequestQuestPrimary(); }
	return true;
}

void ARPGPrototypeCharacter::SetQuestTab(int32 Tab)
{
	QuestTab = FMath::Clamp(Tab, 0, 2); bQuestConfirm = false;
	const auto List = QuestPanelList();
	if (!List.Contains(QuestSelection)) { QuestSelection = List.Num() ? List[0] : 0; }
}

void ARPGPrototypeCharacter::RequestQuestPrimary()
{
	if (!bQuestMenuOpen || !Quests.IsValidIndex(QuestSelection)) { return; }
	const auto S = RPGQuests::State(this, QuestSelection);
	// A field quest found in the journal: ask before taking it on.
	if (QuestGiver < 0 && S == ERPGQuestState::Offered && !RPGQuests::ReportsToGiver(QuestSelection)) { bQuestConfirm = true; return; }
	ActivateQuest(QuestSelection);
}
