#include "RPGPrototype.h"
#include "RPGTuning.h"
#include "ChaosImpactPlayerController.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
void RollIntrinsics(FRPGPrototypeItem& Item, const TArray<FRPGModDefinition>& Pool)
{
    const int32 Count=RPGTuning::IntrinsicCount[FMath::Clamp(static_cast<int32>(Item.Rarity),0,3)];
    TArray<int32> Indices; for(int32 I=0;I<Pool.Num();++I) { Indices.Add(I); }
    bool Transformed=false;
    while(Item.Intrinsics.Num()<Count && Indices.Num())
    {
        int32 At=FMath::RandRange(0,Indices.Num()-1), Index=Indices[At]; Indices.RemoveAt(At);
        const auto& D=Pool[Index];
        if(Transformed && D.bTransform) { continue; }
        Item.Intrinsics.Add({D.Key,RPGEquipment::RollValue(D),1}); Transformed|=D.bTransform;
    }
}
}
FString FRPGPrototypeItem::Name() const
{
    if(Kind==ERPGItemKind::Ball) { return Type==EChaosImpactBallType::Normal?FString(TEXT("通常球")):FString(ChaosImpactBallTypes::GetDisplayName(Type)); }
    const auto* Definition=RPGEquipment::Find(Badge.Key);
    return Definition?Definition->Name:RPGEquipment::KindName(Kind);
}
float FRPGPrototypeItem::Mod(FName Key) const
{
    float Value=Badge.Key==Key?Badge.Value():0;
    for(const auto& A:Intrinsics) { if(A.Key==Key) { Value+=A.Value(); } }
    for(const auto& A:Tunes) { if(A.Key==Key) { Value+=A.Value(); } }
    // Gear and badges grow a little with level and enhancement. Counts (pierces, extra targets) never grow this way,
    // and the survive trait's value is its cooldown, so growth shortens it instead.
    if(Kind!=ERPGItemKind::Ball && !RPGEquipment::IsCount(Key))
    {
        const float Factor=RPGTuning::GearGrowth(Enhancement,Level);
        Value=Key==TEXT("LifeSurvive")?Value/Factor:Value*Factor;
    }
    return Value;
}
float FRPGPrototypeItem::EffectivePower() const
{
    // Infinite legendary strengthening does not inflate sizes, counts or crowd-control durations.
    using namespace RPGTuning;
    return Power*(1+EnhancePowerStep*FMath::Min(Enhancement,EnhanceCap)+EnhancePowerStepOverCap*FMath::Max(0,Enhancement-EnhanceCap))*(1+Mod(TEXT("Damage"))*.01f);
}
float FRPGPrototypeItem::EffectiveCooldown() const
{
    return Cooldown*FMath::Max(RPGTuning::CooldownFloor,1-(Mod(TEXT("Cooldown"))+Mod(TEXT("SSmall")))*.01f);
}
int32 FRPGPrototypeItem::SalvageValue() const
{
    using namespace RPGTuning;
    return FMath::Clamp<int64>((SalvageBase+Level*SalvagePerLevel+Enhancement*SalvagePerEnhance)*SalvageRarityMultiplier[FMath::Clamp(static_cast<int32>(Rarity),0,3)],1,CostCap);
}
int32 FRPGPrototypeItem::UpgradeCost() const
{ using namespace RPGTuning; return FMath::Clamp<int64>(UpgradeBase+Level*UpgradePerLevel+Enhancement*UpgradePerEnhance,1,CostCap); }
int32 FRPGPrototypeItem::TuneCost() const
{ using namespace RPGTuning; return FMath::Clamp<int64>(TuneBase+Level*TunePerLevel+static_cast<int32>(Rarity)*TunePerRarity,1,CostCap); }
TArray<int32> FRPGPrototypeItem::TuneOfferIndices(int32 Socket) const
{
    TArray<int32> Candidates;
    if(Kind!=ERPGItemKind::Ball || Socket<0 || Socket>2) { return Candidates; }
    const auto& Pool=RPGEquipment::Tunes(Type);
    for(int32 I=0;I<Pool.Num();++I)
    {
        if(RPGEquipment::IsCommonTune(Pool[I].Key)!=(Socket==2)) { continue; }
        if(Tunes.ContainsByPredicate([&](const FRPGAffix& A){return A.Key==Pool[I].Key;})) { continue; }
        Candidates.Add(I);
    }
    FRandomStream Random(HashCombine(HashCombine(GetTypeHash(Id),TuneOfferGeneration),Socket+179));
    for(int32 I=Candidates.Num()-1;I>0;--I) { Candidates.Swap(I,Random.RandRange(0,I)); }
    Candidates.SetNum(FMath::Min(RPGTuning::TuneOfferCount,Candidates.Num())); return Candidates;
}
TArray<int32> ARPGPrototypeCharacter::GetTuneOffers(int32 Index,int32 Socket) const
{ return Bag.IsValidIndex(Index)?Bag[Index].TuneOfferIndices(Socket):TArray<int32>{}; }
FRPGPrototypeItem FRPGPrototypeItem::Make(EChaosImpactBallType T,int32 L,ERPGPrototypeRarity R)
{
    FRPGPrototypeItem I; if(T==EChaosImpactBallType::Nova || T==EChaosImpactBallType::Prison) { return I; }
    I.Id=FGuid::NewGuid(); I.Type=T; I.Level=FMath::Clamp(L,1,RPGTuning::MaxLevel); I.Rarity=R;
    // Base power and wait per ball: RPGTuning::BallArchetypes.
    const auto& Base=RPGTuning::BallArchetype(T);
    I.Power=RPGTuning::BallBasePower*Base.PowerScale*(1+(I.Level-1)*RPGTuning::PowerPerItemLevel)*(1+static_cast<int32>(R)*RPGTuning::PowerPerRarity);
    I.Cooldown=Base.Cooldown; I.Tunes.SetNum(3);
    RollIntrinsics(I,RPGEquipment::Intrinsics(T)); return I;
}
FRPGPrototypeItem FRPGPrototypeItem::MakeBadge(ERPGItemKind Kind,int32 L,ERPGPrototypeRarity R,int32 Archetype)
{
    FRPGPrototypeItem I;
    const auto& Pool=RPGEquipment::Badges(Kind); if(!Pool.Num()) { return I; }
    I.Id=FGuid::NewGuid(); I.Kind=Kind; I.Level=FMath::Clamp(L,1,RPGTuning::MaxLevel); I.Rarity=R; I.Power=0; I.Cooldown=0;
    const auto& D=Pool[Archetype==INDEX_NONE?FMath::RandRange(0,Pool.Num()-1):FMath::Clamp(Archetype,0,Pool.Num()-1)];
    I.Badge={D.Key,FMath::FRandRange(D.Min,D.Max),1};
    RollIntrinsics(I,RPGEquipment::BadgeIntrinsics(Kind)); return I;
}
const FRPGPrototypeItem& ARPGPrototypeCharacter::GetBadge(int32 Slot) const
{
    static const FRPGPrototypeItem Empty; return Badges.IsValidIndex(Slot)?Badges[Slot]:Empty;
}
FRPGPrototypeItem FRPGPrototypeItem::MakeGear(ERPGItemKind Kind,int32 Level,ERPGPrototypeRarity Rarity,int32 Archetype)
{
    FRPGPrototypeItem Item; const auto& Pool=RPGEquipment::Gear(Kind); if(Pool.IsEmpty()) { return Item; }
    Item.Id=FGuid::NewGuid(); Item.Kind=Kind; Item.Level=FMath::Clamp(Level,1,RPGTuning::MaxLevel); Item.Rarity=Rarity; Item.Power=Item.Cooldown=0;
    const int32 Primary=Archetype==INDEX_NONE?FMath::RandRange(0,Pool.Num()-1):FMath::Clamp(Archetype,0,Pool.Num()-1);
    Item.Badge={Pool[Primary].Key,RPGEquipment::RollValue(Pool[Primary]),1};
    TArray<FRPGModDefinition> Others; for(int32 I=0;I<Pool.Num();++I) { if(I!=Primary) { Others.Add(Pool[I]); } }
    RollIntrinsics(Item,Others); return Item;
}
const FRPGPrototypeItem& ARPGPrototypeCharacter::GetGear(int32 Slot) const
{ static const FRPGPrototypeItem Empty; return Gears.IsValidIndex(Slot)?Gears[Slot]:Empty; }
float ARPGPrototypeCharacter::BadgeMod(FName Key) const
{
    float Value=0;
    for(const auto& I:Badges)
    {
        // Badge levels are modest passive scaling, never unbounded invulnerability/CC.
        Value+=I.Mod(Key);
    }
    for(const auto& I:Gears)
    {
        Value+=I.Mod(Key);
        if(Key==TEXT("AllPower")) { Value+=I.Mod(TEXT("GearPower")); }
        if(Key==TEXT("BankPower")) { Value+=I.Mod(TEXT("GearBounce")); }
        if(Key==TEXT("DashCD")) { Value+=I.Mod(TEXT("GearDashCD")); }
        if(Key==TEXT("MoveDodge")) { Value+=I.Mod(TEXT("GearDodge")); }
        if(Key==TEXT("Armor")) { Value+=I.Mod(TEXT("GearArmor")); }
        if(Key==TEXT("Regen")) { Value+=I.Mod(TEXT("GearRegen")); }
        if(Key==TEXT("TechCycle")) { Value+=I.Mod(TEXT("GearCycle")); }
        if(Key==TEXT("Stamina")) { Value+=I.Mod(TEXT("GearStamina")); }
    }
    return Value;
}
bool ARPGPrototypeCharacter::IsEquipped(const FGuid& Id) const
{
    for(const auto& I:Attacks) { if(I.Id==Id) { return true; } }
    for(const auto& I:Badges) { if(I.Id==Id) { return true; } }
    for(const auto& I:Gears) { if(I.Id==Id) { return true; } }
    return false;
}
void ARPGPrototypeCharacter::AddRubies(int32 Amount)
{ Rubies=FMath::Clamp<int64>(static_cast<int64>(Rubies)+FMath::Max(0,Amount),0,RPGTuning::RubyCap); }
void ARPGPrototypeCharacter::PickUpRubies(int32 Amount)
{
    AddRubies(Amount);
    const double Now=GetWorld()->GetTimeSeconds();
    RubyStreak=(Now-RubyStreakAt<1.6?RubyStreak:0)+FMath::Max(0,Amount); RubyStreakAt=Now;
    Notice=FString::Printf(TEXT("ルビー +%d"),RubyStreak); NoticeUntil=Now+1.8;
}
void ARPGPrototypeCharacter::RPGTestKit()
{
#if !UE_BUILD_SHIPPING
    if(bGrunt) { return; }
    AddRubies(10000);
    for(auto Type:RPGEquipment::BallRoster()) { AddLoot(FRPGPrototypeItem::Make(Type,RPGLevel,ERPGPrototypeRarity::Legend)); }
    for(auto Kind:{ERPGItemKind::LifeBadge,ERPGItemKind::MobilityBadge,ERPGItemKind::TechniqueBadge})
    { for(int32 I=0;I<8;++I) { AddLoot(FRPGPrototypeItem::MakeBadge(Kind,RPGLevel,ERPGPrototypeRarity::Legend,I)); } }
    for(auto Kind:{ERPGItemKind::Glove,ERPGItemKind::Shoes,ERPGItemKind::Supporter,ERPGItemKind::Pouch})
    { for(int32 I=0;I<3;++I) { AddLoot(FRPGPrototypeItem::MakeGear(Kind,RPGLevel,ERPGPrototypeRarity::Legend,I)); } }
    Notice=TEXT("テスト装備・ルビーを追加しました。Rで初期化できます"); NoticeUntil=GetWorld()->GetTimeSeconds()+4;
#endif
}
void ARPGPrototypeCharacter::RPGDumpCatalog()
{
    const FString Path=FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("RPGPrototypeQA"),TEXT("RPGCatalog.csv"));
    const bool OK=FFileHelper::SaveStringToFile(RPGEquipment::CatalogCsv(),*Path,FFileHelper::EEncodingOptions::ForceUTF8);
    Notice=OK?TEXT("能力一覧を Saved/RPGPrototypeQA/RPGCatalog.csv に書き出しました"):TEXT("能力一覧を書き出せませんでした");
    NoticeUntil=GetWorld()?GetWorld()->GetTimeSeconds()+4:0;
}
bool ARPGPrototypeCharacter::SalvageItem(int32 Index)
{
    if(!bBagOpen || !Bag.IsValidIndex(Index) || IsEquipped(Bag[Index].Id))
    { Notice=TEXT("装備中のアイテムは分解できません"); NoticeUntil=GetWorld()->GetTimeSeconds()+2; return false; }
    if(Bag[Index].bLocked)
    { Notice=TEXT("ロック中のアイテムは分解できません（ロックを外すと分解できます）"); NoticeUntil=GetWorld()->GetTimeSeconds()+2.5; PendingSalvage=INDEX_NONE; return false; }
    if(PendingSalvage!=Index) { PendingSalvage=Index; return false; }
    const int32 Gain=Bag[Index].SalvageValue(); AddRubies(Gain); ItemReadyAt.Remove(Bag[Index].Id); UnseenItems.Remove(Bag[Index].Id); Bag.RemoveAt(Index);
    BagSelection=FMath::Clamp(Index,0,FMath::Max(0,Bag.Num()-1)); PendingSalvage=INDEX_NONE;
    Notice=FString::Printf(TEXT("分解してルビー +%d"),Gain); NoticeUntil=GetWorld()->GetTimeSeconds()+2; return true;
}
ARPGPrototypeMerchant* ARPGPrototypeCharacter::FindMerchant() const
{
    for(TActorIterator<ARPGPrototypeMerchant> It(GetWorld());It;++It)
    {
        if(FVector::DistSquared(GetActorLocation(),It->GetActorLocation())>FMath::Square(300.0f)) { continue; }
        FHitResult Hit; FCollisionQueryParams P(SCENE_QUERY_STAT(RPGMerchant),false,this); P.AddIgnoredActor(*It);
        if(!GetWorld()->LineTraceSingleByChannel(Hit,GetActorLocation(),It->GetActorLocation()+FVector(0,0,100),ECC_Visibility,P)) { return *It; }
    }
    return nullptr;
}
bool ARPGPrototypeCharacter::OpenMerchant(ARPGPrototypeMerchant* Merchant)
{
    if(!IsValid(Merchant) || FindMerchant()!=Merchant || IsEliminated() || IsChargingThrow() || IsThrowReleasePending()) { return false; }
    ActiveMerchant=Merchant; SetBagOpen(true); if(!bBagOpen) { ActiveMerchant.Reset(); return false; }
    QuestEvent(ERPGQuestEvent::MerchantVisited,GetActorLocation());
    PendingTune=PendingSalvage=INDEX_NONE; SwitchInventoryPage(1); return true;
}
bool ARPGPrototypeCharacter::IsAtMerchant() const
{ return bBagOpen && ActiveMerchant.IsValid() && FindMerchant()==ActiveMerchant.Get(); }
bool ARPGPrototypeCharacter::UpgradeItem(int32 Index)
{
    if(!IsAtMerchant() || !Bag.IsValidIndex(Index) || IsEliminated()) { return false; }
    auto& I=Bag[Index];
    if(I.Rarity!=ERPGPrototypeRarity::Legend && I.Enhancement>=RPGTuning::EnhanceCap)
    { Notice=FString::Printf(TEXT("この装備は +%d が上限です"),RPGTuning::EnhanceCap); NoticeUntil=GetWorld()->GetTimeSeconds()+2; return false; }
    const int32 Cost=I.UpgradeCost();
    if(Rubies<Cost) { Notice=TEXT("ルビーが足りません"); NoticeUntil=GetWorld()->GetTimeSeconds()+2; return false; }
    if(I.Enhancement==MAX_int32) { return false; }
    Rubies-=Cost; ++I.Enhancement; RebuildEquipment(); QuestEvent(ERPGQuestEvent::Upgraded,GetActorLocation());
    Notice=FString::Printf(TEXT("%s +%d に強化"),*I.Name(),I.Enhancement); NoticeUntil=GetWorld()->GetTimeSeconds()+2; return true;
}
bool ARPGPrototypeCharacter::ApplyTune(int32 Index,int32 Definition,int32 Socket)
{
    if(!IsAtMerchant() || !Bag.IsValidIndex(Index) || Bag[Index].Kind!=ERPGItemKind::Ball || Socket<0 || Socket>2) { return false; }
    auto& I=Bag[Index]; const auto& Pool=RPGEquipment::Tunes(I.Type); if(!Pool.IsValidIndex(Definition)) { return false; }
    const auto& D=Pool[Definition];
    const bool bCommonTune=RPGEquipment::IsCommonTune(D.Key);
    if(bCommonTune!=(Socket==2)) { Notice=TEXT("このボール専用の改造は「専用1・2」、どのボールにも付く改造は「共通」に付けます"); NoticeUntil=GetWorld()->GetTimeSeconds()+2; return false; }
    I.Tunes.SetNum(3);
    for(int32 S=0;S<3;++S) { if(S!=Socket && I.Tunes[S].Key==D.Key) { Notice=TEXT("同じチューンは重複できません"); NoticeUntil=GetWorld()->GetTimeSeconds()+2; return false; } }
    if(I.Tunes[Socket].Key==D.Key) { return UpgradeTune(Index,Socket); }
    if(!GetTuneOffers(Index,Socket).Contains(Definition)) { Notice=TEXT("表示された3つの候補から選んでください"); NoticeUntil=GetWorld()->GetTimeSeconds()+2; return false; }
    if(!I.Tunes[Socket].Key.IsNone() && PendingTune!=Definition)
    { PendingTune=Definition; Notice=TEXT("付け替えると現在のチューンが消えます。もう一度押して決定"); NoticeUntil=GetWorld()->GetTimeSeconds()+3; return false; }
    const int32 Cost=I.TuneCost(); if(Rubies<Cost) { Notice=TEXT("ルビーが足りません"); NoticeUntil=GetWorld()->GetTimeSeconds()+2; return false; }
    Rubies-=Cost;
    // Hidden rolls are fixed for this paid offer generation; reopening does not reroll them.
    FRandomStream Roll(HashCombine(HashCombine(GetTypeHash(I.Id),GetTypeHash(D.Key)),I.TuneOfferGeneration));
    I.Tunes[Socket]={D.Key,RPGEquipment::RollValue(D,&Roll),1}; PendingTune=INDEX_NONE;
    ++I.TuneOfferGeneration; TuneSelection=0; QuestEvent(ERPGQuestEvent::Tuned,GetActorLocation());
    RebuildEquipment(); Notice=D.Name+TEXT(" を付けました"); NoticeUntil=GetWorld()->GetTimeSeconds()+2; return true;
}
bool ARPGPrototypeCharacter::UpgradeTune(int32 Index,int32 Socket)
{
    if(!IsAtMerchant() || !Bag.IsValidIndex(Index) || !Bag[Index].Tunes.IsValidIndex(Socket)) { return false; }
    auto& I=Bag[Index]; auto& A=I.Tunes[Socket]; if(A.Key.IsNone() || A.Rank>=RPGTuning::TuneMaxRank) { return false; }
    const int32 Cost=I.TuneCost()*A.Rank;
    if(Rubies<Cost) { Notice=TEXT("ルビーが足りません"); NoticeUntil=GetWorld()->GetTimeSeconds()+2; return false; }
    Rubies-=Cost; ++A.Rank; RebuildEquipment(); Notice=TEXT("チューンのランクを上げました"); NoticeUntil=GetWorld()->GetTimeSeconds()+2; return true;
}
void ARPGPrototypeCharacter::RebuildEquipment()
{
    for(auto& I:Attacks) { if(const auto* B=Bag.FindByPredicate([&](const auto& A){return A.Id==I.Id;})) { I=*B; } }
    for(auto& I:Badges) { if(const auto* B=Bag.FindByPredicate([&](const auto& A){return A.Id==I.Id;})) { I=*B; } }
    for(auto& I:Gears) { if(const auto* B=Bag.FindByPredicate([&](const auto& A){return A.Id==I.Id;})) { I=*B; } }
    const float NewMax=(PlayerHP+(RPGLevel-1)*RPGTuning::HPPerLevel+BadgeMod(TEXT("GearHP")))*(1+FMath::Min(RPGTuning::MaxHPBonusCapPercent,BadgeMod(TEXT("HP")))*.01f);
    // Changing equipment never refills HP; prevents free healing by swapping badges.
    MaxHealth=bGrunt?MaxHealth:NewMax; Health=FMath::Min(Health,MaxHealth);
}
namespace
{
bool InBagFilter(const FRPGPrototypeItem& Item,int32 Filter)
{
    const bool Ball=Item.Kind==ERPGItemKind::Ball;
    const bool Badge=Item.Kind>=ERPGItemKind::LifeBadge && Item.Kind<=ERPGItemKind::TechniqueBadge;
    return ((Filter==0 || Filter==ERPGBagFilter::Ball) && Ball) || (Filter==ERPGBagFilter::Badge && Badge)
        || (Filter==ERPGBagFilter::Gear && RPGEquipment::IsGear(Item.Kind)) || (Filter==ERPGBagFilter::Material && Item.Kind==ERPGItemKind::Material);
}
}
TArray<int32> ARPGPrototypeCharacter::FilteredBag() const
{
    TArray<int32> Out;
    for(int32 I=0;I<Bag.Num();++I)
    {
        if(bChoosingEquipment) { if(FitsEquipmentSlot(Bag[I],EquipmentSelection)) { Out.Add(I); } continue; }
        if(InBagFilter(Bag[I],BagFilter)) { Out.Add(I); }
    }
    if(BagSort==ERPGBagSort::Acquired) { return Out; }
    // Ties always fall back to the newest first, so the order is stable while browsing.
    const auto Strength=[this](const FRPGPrototypeItem& I) { return I.Kind==ERPGItemKind::Ball?GetDisplayedPower(I):static_cast<float>(I.Level*4+I.Enhancement*6+static_cast<int32>(I.Rarity)*10); };
    Out.Sort([&](int32 A,int32 B)
    {
        const auto& X=Bag[A]; const auto& Y=Bag[B];
        if(BagSort==ERPGBagSort::Rarity && X.Rarity!=Y.Rarity) { return X.Rarity>Y.Rarity; }
        if(BagSort==ERPGBagSort::Strength) { const float SX=Strength(X),SY=Strength(Y); if(!FMath::IsNearlyEqual(SX,SY)) { return SX>SY; } }
        if(BagSort==ERPGBagSort::Kind)
        {
            if(X.Kind!=Y.Kind) { return X.Kind<Y.Kind; }
            if(X.Type!=Y.Type) { return X.Type<Y.Type; }
            if(X.Badge.Key!=Y.Badge.Key) { return X.Badge.Key.LexicalLess(Y.Badge.Key); }
            if(X.Rarity!=Y.Rarity) { return X.Rarity>Y.Rarity; }
        }
        if((BagSort==ERPGBagSort::Rarity || BagSort==ERPGBagSort::Strength) && X.Level!=Y.Level) { return X.Level>Y.Level; }
        return A>B;
    });
    return Out;
}
int32 ARPGPrototypeCharacter::CountInFilter(int32 Filter,bool bUnseenOnly) const
{
    int32 Count=0;
    for(const auto& I:Bag) { if(InBagFilter(I,Filter) && (!bUnseenOnly || UnseenItems.Contains(I.Id))) { ++Count; } }
    return Count;
}
FString ARPGPrototypeCharacter::BagSortName(int32 Sort)
{
    switch(Sort)
    {
    case ERPGBagSort::Newest: return TEXT("新しい順");
    case ERPGBagSort::Rarity: return TEXT("レア度順");
    case ERPGBagSort::Strength: return TEXT("強さ順");
    case ERPGBagSort::Kind: return TEXT("種類順");
    default: return TEXT("入手順");
    }
}
void ARPGPrototypeCharacter::CycleBagSort()
{
    if(HasConfirmation()) { return; }
    BagSort=(BagSort+1)%ERPGBagSort::Count;
    if(GConfig && GetLocalRPGIndex()>=0)
    { GConfig->SetInt(TEXT("ChaosImpact.RPGPrototype"),*FString::Printf(TEXT("BagSortP%d"),GetLocalRPGIndex()+1),BagSort,GGameUserSettingsIni); GConfig->Flush(false,GGameUserSettingsIni); }
    // The selected item stays selected; only its place in the list moves.
    const auto List=FilteredBag(); if(!List.Contains(BagSelection)) { BagSelection=List.Num()?List[0]:INDEX_NONE; }
    Notice=TEXT("並び順：")+BagSortName(BagSort); NoticeUntil=GetWorld()->GetTimeSeconds()+1.5;
}
void ARPGPrototypeCharacter::MarkSeen(int32 Index)
{ if(Bag.IsValidIndex(Index)) { UnseenItems.Remove(Bag[Index].Id); } }
bool ARPGPrototypeCharacter::ToggleItemLock(int32 Index)
{
    if(!bBagOpen || !Bag.IsValidIndex(Index) || HasConfirmation()) { return false; }
    auto& I=Bag[Index]; I.bLocked=!I.bLocked; RebuildEquipment();
    Notice=I.Name()+(I.bLocked?TEXT(" をロックしました（分解できません）"):TEXT(" のロックを外しました"));
    NoticeUntil=GetWorld()->GetTimeSeconds()+2; return true;
}
TArray<int32> ARPGPrototypeCharacter::BulkSalvageCandidates() const
{
    TArray<int32> Out;
    if(bChoosingEquipment) { return Out; }
    for(int32 I=0;I<Bag.Num();++I)
    {
        const auto& Item=Bag[I];
        if(InBagFilter(Item,BagFilter) && !Item.bLocked && !IsEquipped(Item.Id) && !UnseenItems.Contains(Item.Id)
            && Item.Rarity<=ERPGPrototypeRarity::Rare) { Out.Add(I); }
    }
    return Out;
}
bool ARPGPrototypeCharacter::RequestBulkSalvage()
{
    if(!bBagOpen || HasConfirmation()) { return false; }
    if(BulkSalvageCandidates().IsEmpty())
    { Notice=TEXT("まとめて分解できる物がありません（装備中・ロック中・NEW・ユニーク以上は対象外）"); NoticeUntil=GetWorld()->GetTimeSeconds()+3; return false; }
    bPendingBulkSalvage=true; return true;
}
bool ARPGPrototypeCharacter::ConfirmBulkSalvage()
{
    const auto List=BulkSalvageCandidates(); bPendingBulkSalvage=false;
    if(!bBagOpen || List.IsEmpty()) { return false; }
    const FGuid Selected=Bag.IsValidIndex(BagSelection)?Bag[BagSelection].Id:FGuid();
    int64 Gain=0;
    for(int32 K=List.Num()-1;K>=0;--K)
    { const int32 Index=List[K]; Gain+=Bag[Index].SalvageValue(); ItemReadyAt.Remove(Bag[Index].Id); Bag.RemoveAt(Index); }
    AddRubies(static_cast<int32>(FMath::Min<int64>(Gain,RPGTuning::RubyCap)));
    BagSelection=Bag.IndexOfByPredicate([&](const FRPGPrototypeItem& I){return I.Id==Selected;});
    if(BagSelection==INDEX_NONE) { const auto Remaining=FilteredBag(); BagSelection=Remaining.Num()?Remaining[0]:INDEX_NONE; }
    DetailScroll=0;
    Notice=FString::Printf(TEXT("%d個をまとめて分解　ルビー +%lld"),List.Num(),Gain); NoticeUntil=GetWorld()->GetTimeSeconds()+3; return true;
}
void ARPGPrototypeCharacter::NavigateBag(int32 Direction)
{
	if(HasConfirmation()) { return; }
	if(BagPage==3) { SettingsSelection=FMath::Clamp(SettingsSelection+Direction,0,5); return; }
    if(bChoosingDestination) { EquipTargetSlot=Direction<0?1:2; return; }
    if(BagPage==0 && !bChoosingEquipment)
    { const int32 Group=EquipmentSelection<3?0:EquipmentSelection<7?1:2,Col=EquipmentSelection-(Group==0?0:Group==1?3:7),Next=FMath::Clamp(Group+Direction,0,2); SelectEquipment((Next==0?0:Next==1?3:7)+FMath::Min(Col,Next==1?3:2)); return; }
    if(BagPage==2)
    {
        TuneSelection=FMath::Clamp(TuneSelection+Direction,0,FMath::Max(0,GetTuneOffers(BagSelection,TuneSocket).Num()-1));
    }
    else
    {
        const auto Items=FilteredBag(); const int32 At=Items.Find(BagSelection);
        if(Items.Num()) { BagSelection=Items[FMath::Clamp((At==INDEX_NONE?0:At)+Direction,0,Items.Num()-1)]; }
        DetailScroll=0; PendingSalvage=PendingTune=INDEX_NONE; TuneSelection=0;
    }
}
bool ARPGPrototypeCharacter::HandleBagKey(FKey Key)
{
    if(!bBagOpen) { return false; }
    // WASD moves like the arrows (the bag freezes the character, so they are free here).
    if(Key==EKeys::W) { Key=EKeys::Up; } else if(Key==EKeys::S) { Key=EKeys::Down; }
    else if(Key==EKeys::A) { Key=EKeys::Left; } else if(Key==EKeys::D) { Key=EKeys::Right; }
	if(PendingSessionAction!=0 && (Key==EKeys::Enter || Key==EKeys::Gamepad_FaceButton_Bottom)) { ConfirmSessionAction(); return true; }
    if(bPendingBulkSalvage && (Key==EKeys::Enter || Key==EKeys::Gamepad_FaceButton_Bottom)) { ConfirmBulkSalvage(); return true; }
    if((Key==EKeys::Enter || Key==EKeys::Gamepad_FaceButton_Bottom) && PendingSalvage!=INDEX_NONE)
    { SalvageItem(PendingSalvage); return true; }
    if((Key==EKeys::Enter || Key==EKeys::Gamepad_FaceButton_Bottom) && PendingTune!=INDEX_NONE)
    { ApplyTune(BagSelection,PendingTune,TuneSocket); return true; }
    if(Key==EKeys::Tab || Key==EKeys::Escape || Key==EKeys::Gamepad_Special_Left || Key==EKeys::Gamepad_FaceButton_Right)
    {
        if(HasConfirmation()) { PendingSalvage=PendingTune=INDEX_NONE; PendingSessionAction=0; bPendingBulkSalvage=false; }
        else if(bChoosingEquipment) { CancelEquipmentChange(); }
        else if(bChoosingDestination) { bChoosingDestination=false; }
        else { SetBagOpen(false); } return true;
    }
    if(HasConfirmation()) { return true; }
    if(Key==EKeys::F5) { SwitchInventoryPage(3); RequestSessionAction(1); return true; }
    if(Key==EKeys::F9) { SwitchInventoryPage(3); RequestSessionAction(2); return true; }
    if(Key==EKeys::F2 || Key==EKeys::Gamepad_LeftThumbstick) { SwitchInventoryPage(3); return true; }
    if(Key==EKeys::MouseScrollUp || Key==EKeys::MouseScrollDown)
    { const int32 Step=Key==EKeys::MouseScrollUp?-1:1; if(BagPage==0 && !bChoosingEquipment) { DetailScroll=FMath::Max(0,DetailScroll+Step); } else { NavigateBag(Step); } return true; }
    if((Key==EKeys::Enter || Key==EKeys::Gamepad_FaceButton_Bottom) && (BagPage==0 || BagPage==4))
    { if(BagPage==0 && !bChoosingEquipment) { BeginEquipmentChange(); } else { EquipSelectedItem(); } return true; }
    if(Key==EKeys::Up || Key==EKeys::Gamepad_DPad_Up) { if(bChoosingDestination) { EquipTargetSlot=1; } else { NavigateBag(-1); } return true; }
    if(Key==EKeys::Down || Key==EKeys::Gamepad_DPad_Down) { if(bChoosingDestination) { EquipTargetSlot=2; } else { NavigateBag(1); } return true; }
    if(BagPage==3)
    {
        if(Key==EKeys::Left || Key==EKeys::Right || Key==EKeys::Gamepad_DPad_Left || Key==EKeys::Gamepad_DPad_Right)
        {
            if(SettingsSelection==3 && GetLocalRPGIndex()==0)
            { if(auto* Mode=GetWorld()->GetAuthGameMode<ARPGPrototypeGameMode>()) { const bool Left=Key==EKeys::Left || Key==EKeys::Gamepad_DPad_Left; Mode->SetLocalPlayerCount(FMath::Clamp(Mode->GetLocalPlayerCount()+(Left?-1:1),1,4)); } }
            else if(SettingsSelection==4 && GetLocalRPGIndex()==0) { if(auto* Mode=GetWorld()->GetAuthGameMode<ARPGPrototypeGameMode>()) { Mode->SetSharedScreen(!Mode->IsSharedScreenRequested()); } }
            else if(SettingsSelection==2) { ToggleMenuStyle(); } return true;
        }
        if(Key==EKeys::Enter || Key==EKeys::Gamepad_FaceButton_Bottom)
        {
            if(SettingsSelection<2) { RequestSessionAction(SettingsSelection+1); }
            if(SettingsSelection==2) { ToggleMenuStyle(); }
            if(SettingsSelection==3 && GetLocalRPGIndex()==0) { if(auto* Mode=GetWorld()->GetAuthGameMode<ARPGPrototypeGameMode>()) { Mode->SetLocalPlayerCount(Mode->GetLocalPlayerCount()%4+1); } }
            if(SettingsSelection==4 && GetLocalRPGIndex()==0) { if(auto* Mode=GetWorld()->GetAuthGameMode<ARPGPrototypeGameMode>()) { Mode->SetSharedScreen(!Mode->IsSharedScreenRequested()); } }
            if(SettingsSelection==5) { if(HeroId.IsEmpty()) { BagPage=0; } else { ReturnToTitle(); } } return true;
        }
    }
    if(Key==EKeys::Left || Key==EKeys::Right || Key==EKeys::Gamepad_DPad_Left || Key==EKeys::Gamepad_DPad_Right)
    {
        const bool Left=Key==EKeys::Left || Key==EKeys::Gamepad_DPad_Left;
        if(BagPage==0 && !bChoosingEquipment) { const int32 Start=EquipmentSelection<3?0:EquipmentSelection<7?3:7,End=EquipmentSelection<3?2:EquipmentSelection<7?6:9; SelectEquipment(FMath::Clamp(EquipmentSelection+(Left?-1:1),Start,End)); }
        else if((BagPage==ERPGBagPage::Items || BagPage==ERPGBagPage::Upgrade) && !bChoosingDestination)
        { const int32 Filters[]={1,3,2,4}; int32 At=0; for(int32 I=0;I<4;++I) { if(Filters[I]==BagFilter) { At=I; } } BagFilter=Filters[(At+(Left?3:1))%4]; const auto List=FilteredBag(); BagSelection=List.Num()?List[0]:INDEX_NONE; DetailScroll=0; }
        else if(BagPage==2) { TuneSocket=(TuneSocket+(Left?2:1))%3; }
        else { EquipTargetSlot=Left?1:2; } return true;
    }
    if(Key==EKeys::Q || Key==EKeys::Gamepad_LeftShoulder)
    { const TArray<int32> Pages=IsAtMerchant()?TArray<int32>{0,4,1,2,3}:TArray<int32>{0,4,3}; SwitchInventoryPage(Pages[(FMath::Max(0,Pages.Find(BagPage))+Pages.Num()-1)%Pages.Num()]); return true; }
    if(Key==EKeys::E || Key==EKeys::Gamepad_RightShoulder)
    { const TArray<int32> Pages=IsAtMerchant()?TArray<int32>{0,4,1,2,3}:TArray<int32>{0,4,3}; SwitchInventoryPage(Pages[(FMath::Max(0,Pages.Find(BagPage))+1)%Pages.Num()]); return true; }
    if(Key==EKeys::Delete || Key==EKeys::Gamepad_FaceButton_Left) { if(!(BagPage==0 && !bChoosingEquipment)) { SalvageItem(BagSelection); } return true; }
    if((Key==EKeys::PageDown || Key==EKeys::PageUp) && BagPage==ERPGBagPage::Tune)
    {
        // The tune page uses up/down for the offers, so the page keys pick the ball.
        const auto List=FilteredBag();
        if(List.Num()) { const int32 At=FMath::Max(0,List.Find(BagSelection)); BagSelection=List[(At+(Key==EKeys::PageDown?1:List.Num()-1))%List.Num()]; TuneSelection=0; }
        return true;
    }
    if(Key==EKeys::PageDown) { ++DetailScroll; return true; }
    if(Key==EKeys::PageUp) { DetailScroll=FMath::Max(0,DetailScroll-1); return true; }
    if(Key==EKeys::X || Key==EKeys::Gamepad_LeftTrigger) { if(BagPage!=ERPGBagPage::Settings) { CycleBagSort(); } return true; }
    if(Key==EKeys::BackSpace || Key==EKeys::Gamepad_RightTrigger) { if(BagPage==ERPGBagPage::Items) { RequestBulkSalvage(); } return true; }
    if(Key==EKeys::SpaceBar || Key==EKeys::Gamepad_FaceButton_Top)
    {
        if(BagPage==ERPGBagPage::Tune) { UpgradeTune(BagSelection,TuneSocket); }
        else if(Key==EKeys::Gamepad_FaceButton_Top && BagPage!=ERPGBagPage::Settings && !(BagPage==0 && !bChoosingEquipment)) { ToggleItemLock(BagSelection); }
        return true;
    }
    if(Key==EKeys::F) { if(BagPage!=ERPGBagPage::Settings && !(BagPage==0 && !bChoosingEquipment)) { ToggleItemLock(BagSelection); } return true; }
    if(Key==EKeys::Enter || Key==EKeys::Gamepad_FaceButton_Bottom)
    {
        if(BagPage==1) { UpgradeItem(BagSelection); }
        else if(BagPage==2) { const auto Offers=GetTuneOffers(BagSelection,TuneSocket); if(Offers.IsValidIndex(TuneSelection)) { ApplyTune(BagSelection,Offers[TuneSelection],TuneSocket); } }
        else { EquipBagItem(BagSelection,EquipTargetSlot); }
        return true;
    }
    return false;
}
