#pragma once
#if WITH_DEV_AUTOMATION_TESTS
#include "RPGPrototype.h"

// Explicit test fixture only. Never invoked by ordinary game startup.
namespace RPGPrototypeTest
{
inline void SupplySpecials(ARPGPrototypeCharacter* Player)
{
    const bool WasOpen=Player->IsBagOpen(); Player->SetBagOpen(true);
    for(int32 S=1;S<=2;++S)
    {
        if(Player->GetAttack(S).IsValid()) { continue; }
        const int32 Index=Player->GetBag().Num();
        Player->AddLoot(FRPGPrototypeItem::Make(S==1?EChaosImpactBallType::Fire:EChaosImpactBallType::Ice));
        Player->EquipBagItem(Index,S);
    }
    Player->SetBagOpen(WasOpen);
}
}
#endif
