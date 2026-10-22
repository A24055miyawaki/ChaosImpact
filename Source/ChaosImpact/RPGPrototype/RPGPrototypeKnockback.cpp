#include "RPGPrototype.h"
#include "RPGPrototypeVFX.h"
#include "RPGTuning.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"

bool ARPGPrototypeCharacter::ApplyRPGKnockback(const FVector& Direction,float Distance)
{
    const FVector Flat=Direction.GetSafeNormal2D();
    // A guest moves itself: the push happens on its own machine.
    if(IsRemoteHero() && !Flat.IsNearlyZero() && Distance>0 && !IsEliminated() && !IsDashing()) { ClientApplyKnockback(Flat,Distance); return true; }
    if(!GetWorld() || Distance<=0 || Flat.IsNearlyZero() || IsEliminated() || IsDashing() || IsCrowdControlImmune()
        || IsTrainingMenuFrozen() || GetWorld()->GetTimeSeconds()<KnockbackResistanceUntil) { return false; }
    const auto* Boss=Cast<ARPGPrototypeBoss>(this);
    if(Boss && Boss->IsBossArmoredMove()) { return false; }
    KnockbackDistance=FMath::Clamp(Distance,0.0f,RPGTuning::KnockbackMax)*(Boss?RPGTuning::BossKnockbackScale:1.0f);
    KnockbackDirection=Flat; KnockbackApplied=KnockbackElapsed=0;
    KnockbackResistanceUntil=GetWorld()->GetTimeSeconds()+(Boss?1.2f:.55f);
    CancelChargingThrow(); ConsumeMovementInputVector();
    // Keep gravity/falling; only remove voluntary horizontal movement.
    auto* Movement=GetCharacterMovement(); Movement->Velocity.X=Movement->Velocity.Y=0;
    ARPGPrototypeVFX::PlayImpact(GetWorld(),EChaosImpactBallType::Normal,GetActorLocation(),.3f,false);
    return true;
}

void ARPGPrototypeCharacter::TickRPGKnockback(float Dt)
{
    if(!IsRPGKnockedBack()) { return; }
    const auto* Boss=Cast<ARPGPrototypeBoss>(this);
    if(IsEliminated() || IsDashing() || IsTrainingMenuFrozen() || (Boss && Boss->IsBossArmoredMove()))
    { KnockbackDistance=0; return; }
    KnockbackElapsed+=Dt;
    const float T=FMath::Clamp(KnockbackElapsed/.22f,0.0f,1.0f);
    const float Desired=KnockbackDistance*T*(2-T);
    FHitResult Hit;
    AddActorWorldOffset(KnockbackDirection*FMath::Max(0.0f,Desired-KnockbackApplied),true,&Hit);
    KnockbackApplied=Desired;
    if(T>=1 || Hit.bBlockingHit) { KnockbackDistance=0; }
}

void ARPGPrototypeCharacter::RPGKnockbackTestKit()
{
#if !UE_BUILD_SHIPPING
    if(bGrunt) { return; }
    auto Normal=FRPGPrototypeItem::Make(EChaosImpactBallType::Normal,RPGLevel,ERPGPrototypeRarity::Rare);
    Normal.Tunes[2]={TEXT("Knockback"),100,1}; Normal.Tunes[0]={TEXT("ChargeKnockback"),160,1}; AddLoot(Normal);
    for(const auto Type:{EChaosImpactBallType::Fire,EChaosImpactBallType::Drive})
    {
        auto I=FRPGPrototypeItem::Make(Type,RPGLevel,ERPGPrototypeRarity::Rare);
        I.Tunes[0]={TEXT("BlastKnockback"),110,1}; AddLoot(I);
    }
    auto Wind=FRPGPrototypeItem::Make(EChaosImpactBallType::Wind,RPGLevel,ERPGPrototypeRarity::Rare);
    Wind.Tunes[0]={TEXT("WindKick"),150,1}; AddLoot(Wind);
    Notice=TEXT("ノックバック検証用の通常球・炎・風・ドライブをバッグへ追加しました");
    NoticeUntil=GetWorld()->GetTimeSeconds()+5;
#endif
}
