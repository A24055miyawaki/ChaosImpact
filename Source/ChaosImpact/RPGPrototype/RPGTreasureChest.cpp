#include "RPGTreasureChest.h"
#include "RPGTuning.h"
#include "RPGPrototype.h"
#include "ChaosImpactBallTypes.h"
#include "ChaosImpactSfx.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"

namespace RPGTreasureDetail
{
UMaterialInterface* Material(const TCHAR* Name,const TCHAR* Fallback=TEXT("Copper"))
{
    const auto Load=[](const TCHAR* N)
    { return LoadObject<UMaterialInterface>(nullptr,*FString::Printf(TEXT("/Game/ChaosImpact/RPGPrototype/Adventure/Materials/M_%s.M_%s"),N,N),nullptr,LOAD_Quiet|LOAD_NoWarn); };
    UMaterialInterface* Found=Load(Name);
    return Found?Found:Load(Fallback);
}
/**
 * The chest faces +X: 160 wide (Y), 110 deep (X). A barrel lid (five staves round a half circle) hinges at the back.
 * Parts are tagged (Body, Trim, Gem, Glow, Glint) so the style can repaint them.
 */
USceneComponent* Build(AActor* Actor,bool Preview)
{
    UStaticMesh* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
    UStaticMesh* Sphere=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    int32 Count=0;
    const auto Part=[&](const TCHAR* Tag,USceneComponent* Parent,UStaticMesh* Mesh,const TCHAR* Mat,FVector At,FVector Size,FRotator Rotation=FRotator::ZeroRotator)
    {
        auto* C=Actor->CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("%s_%02d"),Tag,Count++)); C->SetupAttachment(Parent);
        C->SetStaticMesh(Mesh); (void)Mat;
        C->SetRelativeLocation(At); C->SetRelativeRotation(Rotation); C->SetRelativeScale3D(Size/100);
        C->SetCollisionEnabled(ECollisionEnabled::NoCollision); C->SetGenerateOverlapEvents(false);
        C->SetOnlyOwnerSee(!Preview); C->SetHiddenInGame(Preview); C->ComponentTags.Add(Tag);
        C->SetCastShadow(FCString::Strcmp(Tag,TEXT("Glint"))!=0 && FCString::Strcmp(Tag,TEXT("Glow"))!=0);
        return C;
    };
    USceneComponent* Root=Actor->GetRootComponent();
    Part(TEXT("Base"),Root,Cube,TEXT("Timber"),{0,0,6},{124,174,12});
    Part(TEXT("Body"),Root,Cube,TEXT("Planks"),{0,0,47},{108,156,70});
    for(float X:{-50.f,50.f}) { for(float Y:{-74.f,74.f}) { Part(TEXT("Trim"),Root,Cube,TEXT("Copper"),{X,Y,47},{16,16,74}); } }
    for(float Y:{-34.f,34.f}) { Part(TEXT("Trim"),Root,Cube,TEXT("Copper"),{0,Y,47},{112,10,72}); }
    Part(TEXT("Trim"),Root,Cube,TEXT("Copper"),{56,0,62},{6,32,36});
    Part(TEXT("Gem"),Root,Sphere,TEXT("GlowWarm"),{60,0,64},{12,12,12});
    // The glow inside, seen once the lid is up.
    Part(TEXT("Glow"),Root,Cube,TEXT("GlowWarm"),{0,0,80},{96,144,3});
    auto* Hinge=Actor->CreateDefaultSubobject<USceneComponent>(TEXT("LidHinge")); Hinge->SetupAttachment(Root); Hinge->SetRelativeLocation({-55,0,82});
    for(int32 I=0;I<5;++I)
    {
        const float Phi=FMath::DegreesToRadians(18.f+36.f*I);
        const FVector Arc(55+55*FMath::Cos(Phi),0,55*FMath::Sin(Phi));
        const FRotator Lay(18.f+36.f*I+90.f,0,0);
        Part(TEXT("Lid"),Hinge,Cube,TEXT("Planks"),Arc,{38,158,12},Lay);
        for(float Y:{-34.f,34.f})
        { Part(TEXT("Trim"),Hinge,Cube,TEXT("Copper"),FVector(55+58*FMath::Cos(Phi),Y,58*FMath::Sin(Phi)),{39,11,8},Lay); }
        for(float Y:{-78.f,78.f})
        { Part(TEXT("Trim"),Hinge,Cube,TEXT("Copper"),FVector(55+56*FMath::Cos(Phi),Y,56*FMath::Sin(Phi)),{39,6,14},Lay); }
    }
    Part(TEXT("Trim"),Hinge,Cube,TEXT("Copper"),{112,0,-6},{6,26,22});
    // Glints round a chest nobody has opened yet (they pulse; see the visual's tick).
    for(int32 I=0;I<3;++I)
    { const float A=I*2.1f; Part(TEXT("Glint"),Root,Sphere,TEXT("GlowWarm"),{FMath::Cos(A)*70,FMath::Sin(A)*95,95.f+I*18},{7,7,7}); }
    return Hinge;
}
FLinearColor GlowColor(ERPGChestStyle Style)
{
    return Style==ERPGChestStyle::Gold?FLinearColor(1,.62f,.12f):Style==ERPGChestStyle::Silver?FLinearColor(.35f,.8f,1):FLinearColor(1,.55f,.2f);
}
}

void ARPGTreasureChest::ApplyStyle(AActor* Chest,ERPGChestStyle Style)
{
    using namespace RPGTreasureDetail;
    UMaterialInterface* Trim=Style==ERPGChestStyle::Gold?Material(TEXT("Gold")):Style==ERPGChestStyle::Silver?Material(TEXT("Silver")):Material(TEXT("Copper"));
    UMaterialInterface* Body=Style==ERPGChestStyle::Gold?Material(TEXT("ChestRed"),TEXT("Planks")):Style==ERPGChestStyle::Silver?Material(TEXT("ChestBlue"),TEXT("Planks")):Material(TEXT("Planks"));
    const FLinearColor Glow=GlowColor(Style);
    UMaterialInterface* Shine=nullptr; UMaterialInterface* Inside=nullptr;
    if(Chest->GetWorld() && !Chest->GetWorld()->IsGameWorld())
    { Shine=Material(Style==ERPGChestStyle::Silver?TEXT("GlowTeal"):TEXT("GlowWarm")); Inside=Shine; }
    else { Shine=ChaosImpactBallTypes::MakeEmissive(Chest,Glow,6.0f); Inside=ChaosImpactBallTypes::MakeEmissive(Chest,Glow,2.5f); }
    TArray<UStaticMeshComponent*> Parts; Chest->GetComponents(Parts);
    for(auto* P:Parts)
    {
        if(P->ComponentHasTag(TEXT("Trim"))) { P->SetMaterial(0,Trim); }
        else if(P->ComponentHasTag(TEXT("Body")) || P->ComponentHasTag(TEXT("Lid"))) { P->SetMaterial(0,Body); }
        else if(P->ComponentHasTag(TEXT("Gem")) || P->ComponentHasTag(TEXT("Glint"))) { P->SetMaterial(0,Shine); }
        else if(P->ComponentHasTag(TEXT("Glow"))) { P->SetMaterial(0,Inside); }
        else if(P->ComponentHasTag(TEXT("Base"))) { P->SetMaterial(0,Material(TEXT("Timber"))); }
    }
}
ARPGTreasureChest::ARPGTreasureChest()
{
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("TreasureRoot")));
    PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.TickInterval=.1f;
    RPGTreasureDetail::Build(this,true);
}
void ARPGTreasureChest::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    if(ChestId.IsNone()) { ChestId=FName(*(TEXT("Chest_")+FGuid::NewGuid().ToString(EGuidFormats::Digits))); }
    if(GetWorld() && !GetWorld()->IsGameWorld()) { ApplyStyle(this,Style); }
}
void ARPGTreasureChest::BeginPlay() { Super::BeginPlay(); ApplyStyle(this,Style); }
void ARPGTreasureChest::PostDuplicate(EDuplicateMode::Type DuplicateMode)
{ Super::PostDuplicate(DuplicateMode); if(DuplicateMode==EDuplicateMode::Normal) { ChestId=FName(*(TEXT("Chest_")+FGuid::NewGuid().ToString(EGuidFormats::Digits))); } }
void ARPGTreasureChest::SnapToGround()
{
    if(!GetWorld()) { return; }
    FHitResult H; FCollisionQueryParams Q(SCENE_QUERY_STAT(RPGChestFloor),false,this);
    const FVector At=GetActorLocation();
    if(GetWorld()->LineTraceSingleByObjectType(H,At+FVector(0,0,2000),At-FVector(0,0,4000),FCollisionObjectQueryParams(ECC_WorldStatic),Q))
    { SetActorLocation(H.ImpactPoint+FVector(0,0,2)); }
}
bool ARPGTreasureChest::IsOpenedBy(const ARPGPrototypeCharacter* Player) const
{ return Player && !ChestId.IsNone() && Player->OpenedChestIds.Contains(ChestId); }
bool ARPGTreasureChest::CanUse(const ARPGPrototypeCharacter* Player) const
{
    if(!IsValid(Player) || Player->IsGrunt() || Player->IsEliminated() || Player->IsBagOpen() || Player->IsQuestMenuOpen()
        || ChestId.IsNone() || IsOpenedBy(Player) || FVector::DistSquared2D(Player->GetActorLocation(),GetActorLocation())>FMath::Square(260.f)
        || FMath::Abs(Player->GetActorLocation().Z-GetActorLocation().Z)>220) { return false; }
    FHitResult H; FCollisionQueryParams Q(SCENE_QUERY_STAT(RPGChestUse),false,Player); Q.AddIgnoredActor(this);
    return !GetWorld()->LineTraceSingleByObjectType(H,Player->GetActorLocation(),GetActorLocation()+FVector(0,0,90),FCollisionObjectQueryParams(ECC_WorldStatic),Q);
}
ARPGTreasureChest* ARPGTreasureChest::Nearest(const ARPGPrototypeCharacter* Player)
{
    ARPGTreasureChest* Best=nullptr; double Distance=MAX_dbl;
    if(!Player || !Player->GetWorld()) { return nullptr; }
    for(TActorIterator<ARPGTreasureChest> It(Player->GetWorld());It;++It)
    { const double D=FVector::DistSquared2D(It->GetActorLocation(),Player->GetActorLocation()); if(D<Distance && It->CanUse(Player)) { Best=*It; Distance=D; } }
    return Best;
}
bool ARPGTreasureChest::Open(ARPGPrototypeCharacter* Player)
{
    if(!CanUse(Player)) { return false; }
    // Its item has to fit in the bag when picked up; a chest is never opened only to leave its item unreachable.
    if(Player->GetBag().Num()>=RPGTuning::BagCapacity)
    { Player->Notice=TEXT("バッグがいっぱいです。整理してから宝箱を開けてください"); Player->NoticeUntil=GetWorld()->GetTimeSeconds()+3; return false; }
    const float R=FMath::FRand();
    const auto Rarity=R<FMath::Clamp(LegendChance,0.f,1.f)?ERPGPrototypeRarity::Legend:
        R<FMath::Clamp(LegendChance+UniqueChance,0.f,1.f)?ERPGPrototypeRarity::Unique:
        R<FMath::Clamp(LegendChance+UniqueChance+RareChance,0.f,1.f)?ERPGPrototypeRarity::Rare:ERPGPrototypeRarity::Normal;
    ERPGChestRewardKind Kind=RewardKind;
    if(Kind==ERPGChestRewardKind::Random) { const float Pick=FMath::FRand(); Kind=Pick<.5f?ERPGChestRewardKind::Ball:Pick<.85f?ERPGChestRewardKind::Gear:ERPGChestRewardKind::Badge; }
    const int32 Level=FMath::Clamp(Player->GetRPGLevel()+FMath::RandRange(-1,1),1,10000);
    FRPGPrototypeItem Item;
    if(Kind==ERPGChestRewardKind::Ball)
    {
        auto Roster=RPGEquipment::BallRoster(); if(bSpecialBallsOnly) { Roster.Remove(EChaosImpactBallType::Normal); }
        if(Roster.IsEmpty()) { return false; }
        Item=FRPGPrototypeItem::Make(Roster[FMath::RandRange(0,Roster.Num()-1)],Level,Rarity);
    }
    else if(Kind==ERPGChestRewardKind::Gear) { Item=FRPGPrototypeItem::MakeGear(static_cast<ERPGItemKind>(FMath::RandRange(5,8)),Level,Rarity); }
    else { Item=FRPGPrototypeItem::MakeBadge(static_cast<ERPGItemKind>(FMath::RandRange(1,3)),Level,Rarity); }
    if(!Item.IsValid()) { return false; }
    Player->OpenedChestIds.Add(ChestId);
    Player->QuestEvent(ERPGQuestEvent::ChestOpened,GetActorLocation());

    // Out they fly: the item high and towards the opener, the rubies in a spray round the front of the chest.
    const FVector Mouth=GetActorLocation()+FVector(0,0,105);
    FVector Toward=(Player->GetActorLocation()-GetActorLocation()).GetSafeNormal2D();
    if(Toward.IsNearlyZero()) { Toward=GetActorForwardVector(); }
    const FTransform ItemAt(FRotator::ZeroRotator,Mouth);
    if(auto* Loot=GetWorld()->SpawnActorDeferred<ARPGPrototypeLoot>(ARPGPrototypeLoot::StaticClass(),ItemAt,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
    {
        Loot->Item=Item; Loot->Recipient=Player; Loot->bChestLoot=true; Loot->FinishSpawning(ItemAt);
        Loot->Pop(Toward*210+FVector(0,0,760));
    }
    int32 Left=FMath::RandRange(FMath::Clamp(RubyMin,0,100000),FMath::Clamp(FMath::Max(RubyMin,RubyMax),0,100000));
    // Big rubies first, so a rich chest is still a handful of gems, not a hundred.
    TArray<int32> Gems;
    for(const int32 Worth:{50,20,5})
    { while(Left>=Worth*2 && Gems.Num()<10) { Gems.Add(Worth); Left-=Worth; } }
    while(Left>0 && Gems.Num()<16) { const int32 Worth=FMath::Min(Left,Gems.Num()<12?1:Left); Gems.Add(Worth); Left-=Worth; }
    if(Left>0 && Gems.Num()) { Gems.Last()+=Left; }
    for(int32 I=0;I<Gems.Num();++I)
    {
        const float Yaw=FMath::RandRange(-120.f,120.f)+(I%2?0:180)*(FMath::FRand()<.25f);
        const FVector Out=Toward.RotateAngleAxis(Yaw,FVector::UpVector)*FMath::FRandRange(140.f,380.f)+FVector(0,0,FMath::FRandRange(560.f,820.f));
        ARPGRubyGem::Spawn(GetWorld(),Mouth+FVector(0,0,I*2.0f),Out,Gems[I],Player);
    }
    for(auto& View:Views) { if(View.Key.Get()==Player && View.Value.IsValid()) { View.Value->PlayOpen(); } }
    Player->Notice=TEXT("宝箱を開けた！　飛び出したルビーと品物を拾おう"); Player->NoticeUntil=GetWorld()->GetTimeSeconds()+3;
    ChaosImpactSfx::PlayAt(this,EChaosImpactSfx::UiReveal,GetActorLocation(),.8f);
    ChaosImpactSfx::PlayAt(this,EChaosImpactSfx::SoloDoorOpen,GetActorLocation(),.45f,1.3f);
    return true;
}
void ARPGTreasureChest::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    for(auto It=Views.CreateIterator();It;++It)
    { if(!It.Key().IsValid()) { if(It.Value().IsValid()) { It.Value()->Destroy(); } It.RemoveCurrent(); } }
    for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
    {
        auto* PC=It->Get(); auto* Player=PC && PC->IsLocalController()?Cast<ARPGPrototypeCharacter>(PC->GetPawn()):nullptr;
        if(!Player || Views.FindRef(Player).IsValid()) { continue; }
        auto* View=GetWorld()->SpawnActor<ARPGTreasureChestVisual>(GetActorLocation(),GetActorRotation());
        if(View) { View->Initialize(this,Player); Views.Add(Player,View); }
    }
}
void ARPGTreasureChest::EndPlay(const EEndPlayReason::Type Reason)
{ for(auto& View:Views) { if(View.Value.IsValid()) { View.Value->Destroy(); } } Views.Empty(); Super::EndPlay(Reason); }

ARPGTreasureChestVisual::ARPGTreasureChestVisual()
{
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("ChestViewRoot")));
    PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.TickInterval=1.f/30;
    Hinge=RPGTreasureDetail::Build(this,false);
}
void ARPGTreasureChestVisual::Initialize(ARPGTreasureChest* Chest,ARPGPrototypeCharacter* Player)
{
    Source=Chest; Recipient=Player; SetOwner(Player);
    ARPGTreasureChest::ApplyStyle(this,Chest->Style);
    Hinge->SetRelativeRotation(FRotator(Chest->IsOpenedBy(Player)?105.f:0,0,0));
}
void ARPGTreasureChestVisual::PlayOpen()
{
    OpenAge=0;
    if(auto* System=ChaosImpactBallTypes::LoadEffect(ChaosImpactBallTypes::Effects::WarpOut))
    {
        if(auto* N=UNiagaraFunctionLibrary::SpawnSystemAttached(System,GetRootComponent(),NAME_None,FVector(0,0,90),FRotator::ZeroRotator,
            EAttachLocation::KeepRelativeOffset,true))
        {
            N->SetOnlyOwnerSee(true);
            ChaosImpactBallTypes::SetEffectColor(N,TEXT("Base Color"),RPGTreasureDetail::GlowColor(Source.IsValid()?Source->Style:ERPGChestStyle::Wood));
        }
    }
}
void ARPGTreasureChestVisual::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if(!Source.IsValid() || !Recipient.IsValid()) { Destroy(); return; }
    SetActorTransform(Source->GetActorTransform());
    Clock+=DeltaSeconds;
    const bool bOpened=Source->IsOpenedBy(Recipient.Get());
    float Pitch=Hinge->GetRelativeRotation().Pitch;
    if(OpenAge>=0)
    {
        // Flung open past its rest, then settles back (a little spring).
        OpenAge+=DeltaSeconds;
        const float T=FMath::Clamp(OpenAge/.55f,0.f,1.f);
        Pitch=105.f*(1-FMath::Exp(-7*T)*FMath::Cos(9*T));
        if(OpenAge>.8f) { OpenAge=-1; }
    }
    else { Pitch=FMath::FInterpTo(Pitch,bOpened?105.f:0,DeltaSeconds,8); }
    Hinge->SetRelativeRotation(FRotator(Pitch,0,0));
    TArray<UStaticMeshComponent*> Parts; GetComponents(Parts);
    for(auto* P:Parts)
    {
        if(P->ComponentHasTag(TEXT("Glint")))
        {
            const float Pulse=FMath::Max(0.f,FMath::Sin(Clock*3.1f+P->GetRelativeLocation().X*.05f));
            P->SetVisibility(!bOpened && Pulse>.05f); P->SetRelativeScale3D(FVector(.02f+.08f*Pulse));
        }
        if(P->ComponentHasTag(TEXT("Glow"))) { P->SetVisibility(bOpened || OpenAge>=0); }
    }
}

ARPGRubyGem::ARPGRubyGem()
{
    PrimaryActorTick.bCanEverTick=true;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("GemRoot")));
    UStaticMesh* Cone=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cone.Cone"));
    for(int32 I=0;I<2;++I)
    {
        auto* C=CreateDefaultSubobject<UStaticMeshComponent>(I?TEXT("GemLower"):TEXT("GemUpper")); C->SetupAttachment(GetRootComponent());
        C->SetStaticMesh(Cone); C->SetCollisionEnabled(ECollisionEnabled::NoCollision); C->SetGenerateOverlapEvents(false); C->SetCastShadow(false);
        // A cut gem: a short crown over a deep pavilion, point down.
        C->SetRelativeLocation(FVector(0,0,I?-9.f:5.f)); C->SetRelativeRotation(FRotator(0,0,I?180.f:0)); C->SetRelativeScale3D(I?FVector(.24f,.24f,.2f):FVector(.24f,.24f,.1f));
        (I?Lower:Upper)=C;
    }
}
ARPGRubyGem* ARPGRubyGem::Spawn(UWorld* World,const FVector& At,const FVector& InVelocity,int32 InValue,ARPGPrototypeCharacter* Player)
{
    if(!World || !Player) { return nullptr; }
    auto* Gem=World->SpawnActorDeferred<ARPGRubyGem>(StaticClass(),FTransform(At),nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!Gem) { return nullptr; }
    Gem->Velocity=InVelocity; Gem->Value=FMath::Max(1,InValue); Gem->Recipient=Player; Gem->SetOwner(Player); Gem->FloorZ=At.Z-105; Gem->Origin=At;
    Gem->FinishSpawning(FTransform(At));
    const float Size=Gem->Value>=50?1.7f:Gem->Value>=20?1.4f:Gem->Value>=5?1.15f:.9f;
    const FLinearColor Color=Gem->Value>=50?FLinearColor(1,.08f,.55f):Gem->Value>=20?FLinearColor(1,.02f,.08f):Gem->Value>=5?FLinearColor(1,.12f,.1f):FLinearColor(1,.35f,.4f);
    for(auto* C:{Gem->Upper.Get(),Gem->Lower.Get()})
    { C->SetMaterial(0,ChaosImpactBallTypes::MakeEmissive(Gem,Color,2.4f)); C->SetRelativeScale3D(C->GetRelativeScale3D()*Size); C->SetOnlyOwnerSee(true); }
    return Gem;
}
bool ARPGRubyGem::Collect(ARPGPrototypeCharacter* Player)
{
    if(bCollected || !IsValid(Player) || !IsFor(Player)) { return false; }
    bCollected=true; Player->PickUpRubies(Value);
    ChaosImpactSfx::PlayAt(this,EChaosImpactSfx::CharPickup,GetActorLocation(),.32f,1.25f+FMath::Min(Value,50)*.008f);
    Destroy(); return true;
}
int32 ARPGRubyGem::CollectAllFor(ARPGPrototypeCharacter* Player)
{
    if(!Player || !Player->GetWorld()) { return 0; }
    int32 Taken=0;
    for(TActorIterator<ARPGRubyGem> It(Player->GetWorld());It;++It) { if(It->IsFor(Player)) { Player->AddRubies(It->Value); It->bCollected=true; It->Destroy(); ++Taken; } }
    for(TActorIterator<ARPGPrototypeLoot> It(Player->GetWorld());It;++It)
    { if(It->bChestLoot && It->IsVisibleTo(Player) && Player->AddLoot(It->Item)) { It->Destroy(); ++Taken; } }
    return Taken;
}
void ARPGRubyGem::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    auto* Player=Recipient.Get();
    if(!Player) { Destroy(); return; }
    Age+=DeltaSeconds;
    const auto* Mode=GetWorld()->GetAuthGameMode<ARPGPrototypeGameMode>();
    const bool bShared=Mode && Mode->UsesSharedScreen();
    Upper->SetOnlyOwnerSee(!bShared); Lower->SetOnlyOwnerSee(!bShared);
    Spin+=DeltaSeconds*(bLanded?140:600); SetActorRotation(FRotator(0,Spin,0));
    const FVector At=GetActorLocation(), Body=Player->GetActorLocation();
    const float Near=FVector::Dist2D(At,Body);
    if(!bLanded)
    {
        Velocity.Z-=1900*DeltaSeconds;
        FVector To=At+Velocity*DeltaSeconds;
        FHitResult Hit; FCollisionQueryParams Q(SCENE_QUERY_STAT(RPGRubyFlight),false,this); Q.AddIgnoredActor(Player);
        if(GetWorld()->LineTraceSingleByObjectType(Hit,At,To-FVector(0,0,22),FCollisionObjectQueryParams(ECC_WorldStatic),Q) && Hit.ImpactPoint.Z<FloorZ+110)
        {
            if(Hit.ImpactNormal.Z<.6f)
            { Velocity=FVector::VectorPlaneProject(Velocity,Hit.ImpactNormal)*.45f+Hit.ImpactNormal*120; To=Hit.Location+Hit.ImpactNormal*8; }
            else if(++Bounces>=2 || Velocity.Z>-300) { bLanded=true; Rest=Hit.ImpactPoint+FVector(0,0,24); To=Rest; }
            else { To=Hit.ImpactPoint+FVector(0,0,22); Velocity.Z*=-.42f; Velocity.X*=.55f; Velocity.Y*=.55f; }
        }
        // Thrown off a ledge: it lands back beside the chest rather than being lost below.
        if(To.Z<FloorZ-60 || Age>2.5f)
        {
            const FVector Beside=FRotator(0,FMath::FRandRange(0.f,360.f),0).Vector()*FMath::FRandRange(60.f,120.f);
            bLanded=true; Rest=FVector(Origin.X+Beside.X,Origin.Y+Beside.Y,FloorZ+24); To=Rest;
        }
        SetActorLocation(To);
    }
    else if(Age>.7f && Near<420 && FMath::Abs(At.Z-Body.Z)<260 && !Player->IsEliminated())
    {
        // Drawn in, faster the closer it gets.
        const FVector Goal=Body+FVector(0,0,10);
        SetActorLocation(At+(Goal-At).GetClampedToMaxSize(FMath::Lerp(1500.f,500.f,Near/420)*DeltaSeconds));
    }
    else { SetActorLocation(Rest+FVector(0,0,6*FMath::Sin(Age*3+Value))); }
    if(bLanded && Age>.45f && FVector::Dist2D(GetActorLocation(),Body)<95 && FMath::Abs(GetActorLocation().Z-Body.Z)<170) { Collect(Player); }
}
