#include "RPGPrototype.h"
#include "Engine/World.h"

const FRPGPrototypeItem& ARPGPrototypeCharacter::GetEquipment(int32 Slot) const
{
    if(Slot<3) { return GetAttack(Slot); }
    if(Slot<7) { return GetGear(Slot-3); }
    return GetBadge(Slot-7);
}
FString ARPGPrototypeCharacter::EquipmentLabel(int32 Slot)
{
    const TCHAR* Labels[]={TEXT("通常球"),TEXT("特殊球1"),TEXT("特殊球2"),TEXT("グローブ"),TEXT("シューズ"),TEXT("サポーター"),TEXT("ポーチ"),TEXT("生命バッジ"),TEXT("機動バッジ"),TEXT("技術バッジ")};
    return Slot>=0 && Slot<10?Labels[Slot]:TEXT("");
}
bool ARPGPrototypeCharacter::FitsEquipmentSlot(const FRPGPrototypeItem& Item,int32 Slot) const
{
    if(!Item.IsValid() || Slot<0 || Slot>=10) { return false; }
    if(Slot<3) { return Item.Kind==ERPGItemKind::Ball && (Slot==0)==(Item.Type==EChaosImpactBallType::Normal); }
    if(Slot<7) { return static_cast<int32>(Item.Kind)==Slot+2; }
    return static_cast<int32>(Item.Kind)==Slot-6;
}
void ARPGPrototypeCharacter::SelectEquipment(int32 Slot)
{
    if(Slot<0 || Slot>=10 || HasConfirmation()) { return; }
    EquipmentSelection=Slot; const auto& Item=GetEquipment(Slot);
    BagSelection=Bag.IndexOfByPredicate([&](const auto& I){return I.Id==Item.Id;});
    DetailScroll=TuneSelection=0;
    if(Slot==1 || Slot==2) { EquipTargetSlot=Slot; }
}
void ARPGPrototypeCharacter::BeginEquipmentChange()
{
    if(!bBagOpen || HasConfirmation()) { return; }
    EquipmentReturnPage=BagPage; BagPage=0; bChoosingEquipment=true; bChoosingDestination=false;
    const auto List=FilteredBag();
    if(!List.Contains(BagSelection)) { BagSelection=List.Num()?List[0]:INDEX_NONE; }
    DetailScroll=0;
}
void ARPGPrototypeCharacter::CancelEquipmentChange()
{
    bChoosingEquipment=bChoosingDestination=false; BagPage=EquipmentReturnPage;
    if(BagPage==0) { SelectEquipment(EquipmentSelection); }
}
void ARPGPrototypeCharacter::SwitchInventoryPage(int32 Page)
{
    if(HasConfirmation() || Page<0 || Page>4 || ((Page==1 || Page==2) && !IsAtMerchant())) { return; }
    BagPage=Page; bChoosingEquipment=bChoosingDestination=false; DetailScroll=TuneSelection=0;
    // Tunes exist only for balls, so the tune workshop always lists balls.
    if(Page==ERPGBagPage::Tune) { BagFilter=ERPGBagFilter::Ball; }
    if(Page==0) { SelectEquipment(EquipmentSelection); }
    else { const auto List=FilteredBag(); if(!List.Contains(BagSelection)) { BagSelection=List.Num()?List[0]:INDEX_NONE; } }
}
bool ARPGPrototypeCharacter::EquipSelectedItem()
{
    if(!bBagOpen || !Bag.IsValidIndex(BagSelection) || !FilteredBag().Contains(BagSelection)) { return false; }
    const auto& Item=Bag[BagSelection];
    if(bChoosingEquipment && !FitsEquipmentSlot(Item,EquipmentSelection)) { return false; }
    if(!bChoosingEquipment && Item.Kind==ERPGItemKind::Ball && Item.Type!=EChaosImpactBallType::Normal && !bChoosingDestination)
    { bChoosingDestination=true; return false; }
    const int32 Target=bChoosingEquipment && EquipmentSelection>0 && EquipmentSelection<3?EquipmentSelection:EquipTargetSlot;
    if(!EquipBagItem(BagSelection,FMath::Clamp(Target,1,2))) { return false; }
    if(bChoosingEquipment) { const int32 Selected=EquipmentSelection; bChoosingEquipment=false; BagPage=0; SelectEquipment(Selected); }
    bChoosingDestination=false; return true;
}
float ARPGPrototypeCharacter::GetDisplayedPower(const FRPGPrototypeItem& Item) const
{ return Item.Kind==ERPGItemKind::Ball?Item.EffectivePower()*(1+BadgeMod(TEXT("AllPower"))*.01f):0; }
