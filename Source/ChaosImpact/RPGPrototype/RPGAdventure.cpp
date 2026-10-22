#include "RPGAdventure.h"
#include "RPGPrototype.h"
#include "RPGTreasureChest.h"
#include "RPGQuests.h"
#include "RPGSnowField.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Materials/MaterialInterface.h"

// The quest data lives in RPGQuests.cpp; these keep the original names working.
FString RPGAdventure::QuestName(int32 Index) { return Index>=0 && Index<RPGQuests::Num()?FString(RPGQuests::Get(Index).Name):FString(); }
FString RPGAdventure::QuestDescription(int32 Index) { return Index>=0 && Index<RPGQuests::Num()?FString(RPGQuests::Get(Index).Situation):FString(); }
int32 RPGAdventure::Goal(int32 Index) { return RPGQuests::Get(Index).Goal; }
FString RPGAdventure::Objective(const ARPGPrototypeCharacter* Player,int32 Index) { return RPGQuests::Progress(Player,Index); }

ARPGAdventureNode::ARPGAdventureNode()
{
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("QuestNodeRoot")));
    GetRootComponent()->SetMobility(EComponentMobility::Static);
}
void ARPGAdventureNode::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    RebuildNode();
}
void ARPGAdventureNode::RebuildNode()
{
    for(auto P:Pieces) { if(IsValid(P)) { P->DestroyComponent(); } } Pieces.Empty();
    const auto Part=[&](const TCHAR* Shape,const TCHAR* Material,FVector At,FVector Size,FRotator R=FRotator::ZeroRotator)
    {
        auto* P=NewObject<UStaticMeshComponent>(this); P->SetupAttachment(GetRootComponent()); AddInstanceComponent(P); Pieces.Add(P);
        P->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,*FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"),Shape,Shape)));
        P->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,*FString::Printf(TEXT("/Game/ChaosImpact/RPGPrototype/Adventure/Materials/M_%s.M_%s"),Material,Material)));
        P->SetRelativeLocation(At); P->SetRelativeScale3D(Size/100); P->SetRelativeRotation(R);
        P->SetCollisionEnabled(ECollisionEnabled::NoCollision); P->SetMobility(EComponentMobility::Static); P->RegisterComponent();
    };
    if(Kind==ERPGAdventureNodeKind::Board)
    {
        for(float Y:{-85.0f,85.0f}) { Part(TEXT("Cube"),TEXT("Timber"),{0,Y,100},{25,25,200}); }
        Part(TEXT("Cube"),TEXT("Timber"),{0,0,185},{40,230,150});
        Part(TEXT("Cube"),TEXT("Plaster"),{-24,0,185},{8,190,118});
        for(int32 I=0;I<3;++I) { Part(TEXT("Cube"),TEXT("Paper"),{-30,I*55.0f-55,185},{6,42,76}); }
        Part(TEXT("Cube"),TEXT("RoofTeal"),{0,0,280},{110,280,30},{0,0,12});
    }
    else if(Kind==ERPGAdventureNodeKind::Supply)
    {
        Part(TEXT("Cube"),TEXT("Planks"),{0,0,48},{110,90,96});
        for(float Y:{-35.0f,35.0f}) { Part(TEXT("Cube"),TEXT("Copper"),{0,Y,48},{116,12,102}); }
        Part(TEXT("Cube"),TEXT("GlowTeal"),{-58,0,55},{8,35,35});
        Part(TEXT("Cube"),TEXT("RoofTeal"),{0,0,103},{116,96,16});
    }
    else
    {
        Part(TEXT("Cylinder"),TEXT("Stone"),{0,0,28},{150,150,56});
        Part(TEXT("Cube"),TEXT("StoneDark"),{0,0,95},{64,64,140},{0,45,0});
        Part(TEXT("Sphere"),TEXT("GlowTeal"),{0,0,180},{60,60,60});
    }
}
bool ARPGAdventureNode::CanUse(const ARPGPrototypeCharacter* Player) const
{
    if(!Player || Player->IsEliminated() || Player->IsBagOpen() || Player->IsQuestMenuOpen() || Player->IsGrunt()
        || FVector::DistSquared2D(Player->GetActorLocation(),GetActorLocation())>FMath::Square(260.0f)
        || FMath::Abs(Player->GetActorLocation().Z-GetActorLocation().Z)>220) { return false; }
    FHitResult H; FCollisionQueryParams Q(SCENE_QUERY_STAT(RPGAdventureUse),false,Player); Q.AddIgnoredActor(this);
    if(GetWorld()->LineTraceSingleByObjectType(H,Player->GetActorLocation(),GetActorLocation()+FVector(0,0,90),FCollisionObjectQueryParams(ECC_WorldStatic),Q)) { return false; }
    if(Kind==ERPGAdventureNodeKind::Supply)
    { return Player->Quests.IsValidIndex(1) && Player->Quests[1].Stage==1 && !(Player->Quests[1].Flags&(1<<ObjectiveId)); }
    return true;
}
ARPGAdventureNode* ARPGAdventureNode::Nearest(const ARPGPrototypeCharacter* Player)
{
    ARPGAdventureNode* Best=nullptr; float Distance=MAX_flt; if(!Player || !Player->GetWorld()) { return nullptr; }
    for(TActorIterator<ARPGAdventureNode> It(Player->GetWorld());It;++It)
    { const float D=FVector::DistSquared2D(Player->GetActorLocation(),It->GetActorLocation()); if(D<Distance && It->CanUse(Player)) { Best=*It; Distance=D; } }
    return Best;
}
bool ARPGAdventureNode::Use(ARPGPrototypeCharacter* Player)
{
    if(!CanUse(Player)) { return false; }
    if(Kind==ERPGAdventureNodeKind::Board) { Player->SetQuestMenuOpen(true); return true; }
    if(Kind==ERPGAdventureNodeKind::Supply)
    {
        auto& Q=Player->Quests[1]; Q.Flags|=1<<ObjectiveId; Q.Count=FMath::CountBits(static_cast<uint64>(Q.Flags)); if(Q.Count>=3) { Q.Stage=2; }
        Player->Notice=Q.Stage==2?TEXT("補給箱がそろった。掲示板へ報告しよう"):FString::Printf(TEXT("補給箱を回収　%d / 3"),Q.Count);
    }
    else { Player->Notice=Kind==ERPGAdventureNodeKind::DungeonEntrance?TEXT("水脈峡谷 — この門の先に上層回廊と苔の水路が続いている"):TEXT("風見の中継塔 — 遠くに遺跡の灯りが見える"); }
    Player->NoticeUntil=GetWorld()->GetTimeSeconds()+3; return true;
}
bool ARPGPrototypeCharacter::InteractAdventure()
{
    if(auto* Chest=ARPGTreasureChest::Nearest(this)) { Chest->Open(this); return true; }
    if(auto* Giver=ARPGQuestGiver::Nearest(this)) { return TalkToGiver(Giver->GiverId); }
    if(auto* Gate=ARPGWarpGate::Nearest(this)) { return Gate->Use(this); }
    if(auto* Node=ARPGAdventureNode::Nearest(this)) { return Node->Use(this); }
    if(NearestBeacon()) { return OpenFastTravel(); }
    return false;
}
void ARPGPrototypeCharacter::TickAdventure(float DeltaSeconds)
{
    auto* Adventure=ARPGAdventureWorld::Find(GetWorld()); if(!Adventure || bGrunt) { return; }
    // The host's copy of a guest: its quests, beacons and places are its own machine's; only the colony's rest is here.
    if(IsRemoteHero())
    {
        if(!IsEliminated() && Adventure->IsInColony(GetActorLocation())) { RestoreHP(MaxHealth*.3f*DeltaSeconds); }
        if(GetActorLocation().Z<Adventure->GetActorLocation().Z-850) { RecoverFromFall(); }
        return;
    }
    if(Quests.Num()<RPGQuests::Num()) { Quests.SetNum(RPGQuests::Num()); }
    // A conversation ends when you walk away; the journal stays open wherever you are.
    if(bQuestMenuOpen && QuestGiver>=0)
    {
        // (Nearest() skips players with a menu open, so plain distance here.)
        bool bNear=false;
        if(QuestGiver==0) { for(TActorIterator<ARPGAdventureNode> It(GetWorld());It;++It) { if(It->Kind==ERPGAdventureNodeKind::Board && FVector::DistSquared2D(GetActorLocation(),It->GetActorLocation())<FMath::Square(420.0f)) { bNear=true; } } }
        else { for(TActorIterator<ARPGQuestGiver> It(GetWorld());It;++It) { if(It->GiverId==QuestGiver && FVector::DistSquared2D(GetActorLocation(),It->GetActorLocation())<FMath::Square(420.0f)) { bNear=true; } } }
        if(!bNear) { SetQuestMenuOpen(false); }
    }
    if(bQuestMenuOpen && IsEliminated()) { SetQuestMenuOpen(false); }
    // Accepting or finishing moves a quest to another tab: keep a quest of this tab selected.
    if(bQuestMenuOpen && !bQuestConfirm) { const auto List=QuestPanelList(); if(List.Num() && !List.Contains(QuestSelection)) { QuestSelection=List[0]; } }
    if(IsEliminated()) { return; }
    if(bQuestMenuOpen && GetWorld()->GetTimeSeconds()>=NextBagNavigationAt)
    {
        if(auto* PC=Cast<AChaosImpactPlayerController>(GetController());PC && PC->IsUsingGamepad())
        { float X=0,Y=0; PC->GetInputAnalogStickState(EControllerAnalogStick::CAS_LeftStick,X,Y); if(FMath::Abs(Y)>.65f) { HandleQuestKey(Y>0?EKeys::Up:EKeys::Down); NextBagNavigationAt=GetWorld()->GetTimeSeconds()+.2; } }
    }
    // Below the map: back on the last firm ground, with a little damage (RPGRevive.cpp).
    if(GetActorLocation().Z<Adventure->GetActorLocation().Z-850 && HasAuthority()) { RecoverFromFall(); }
    for(TActorIterator<ARPGAdventureCheckpoint> It(GetWorld());It;++It) { It->TryActivate(this); }
    // Entering a named place (explore quests and quests found by going somewhere).
    if(GetWorld()->GetTimeSeconds()>=NextPlaceCheckAt)
    {
        NextPlaceCheckAt=GetWorld()->GetTimeSeconds()+.4;
        const FString Place=Adventure->RegionName(GetActorLocation());
        if(Place!=LastPlace) { LastPlace=Place; if(!Place.IsEmpty()) { QuestEvent(ERPGQuestEvent::RegionEntered,GetActorLocation(),0,Place); } }
    }
    if(Adventure->IsInColony(GetActorLocation()))
    { RestoreHP(MaxHealth*.3f*DeltaSeconds); Stamina=FMath::Min(MaxStamina,Stamina+MaxStamina*.4f*DeltaSeconds); }
    if(Quests[2].Stage==1)
    {
        for(TActorIterator<ARPGAdventureNode> It(GetWorld());It;++It)
        {
            if(It->Kind!=ERPGAdventureNodeKind::Survey || FVector::DistSquared2D(GetActorLocation(),It->GetActorLocation())>FMath::Square(260.0f)
                || FMath::Abs(GetActorLocation().Z-It->GetActorLocation().Z)>220) { continue; }
            const int32 Bit=1<<FMath::Clamp(It->ObjectiveId,0,1); auto& Q=Quests[2]; if(Q.Flags&Bit) { continue; }
            Q.Flags|=Bit; Q.Count=FMath::CountBits(static_cast<uint64>(Q.Flags)); if(Q.Count>=2) { Q.Stage=2; }
            Notice=Q.Stage==2?TEXT("塔と遺跡の調査完了。掲示板へ報告しよう"):TEXT("中継塔を調査した。次は奥の遺跡へ"); NoticeUntil=GetWorld()->GetTimeSeconds()+3;
        }
    }
}
