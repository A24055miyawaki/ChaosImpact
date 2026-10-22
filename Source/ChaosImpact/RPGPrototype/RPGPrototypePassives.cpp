#include "RPGPrototype.h"
#include "RPGTuning.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"

void ARPGPrototypeCharacter::ApplyRPGStatus(FName Key,float Value,float Seconds,APawn* Source)
{
    if(Seconds<=0 || IsEliminated()) { return; }
    // A guest moves itself: what changes its speed is applied on its own machine too.
    if(IsRemoteHero() && (Key==TEXT("Slow") || Key==TEXT("MoveBuff"))) { ClientApplyStatus(Key,Value,Seconds); }
    const double Now=GetWorld()->GetTimeSeconds();
    auto& State=Statuses.FindOrAdd(Key);
    State.X=State.Y>Now?FMath::Max(State.X,static_cast<double>(Value)):Value;
    State.Y=FMath::Max(State.Y,Now+Seconds); StatusSources.Add(Key,Source);
}
float ARPGPrototypeCharacter::Status(FName Key) const
{
    const auto* S=Statuses.Find(Key);
    return S && GetWorld() && S->Y>GetWorld()->GetTimeSeconds()?S->X:0;
}
float ARPGPrototypeCharacter::GetAimError() const { return Status(TEXT("AimError")); }
bool ARPGPrototypeCharacter::IsMoveInputIgnored() const
{ return Super::IsMoveInputIgnored() || IsRPGStunned() || IsRPGKnockedBack() || bDowned; }
bool ARPGPrototypeCharacter::IsMatchInputLocked() const
{ return Super::IsMatchInputLocked() || IsRPGStunned() || IsRPGKnockedBack() || bDowned; }
void ARPGPrototypeCharacter::LoseCarriedBalls()
{
    if(bGrunt || IsEliminated()) { Super::LoseCarriedBalls(); return; }
    if(IsRemoteHero()) { ClientDropAllBalls(); }
    // No loose balls in the RPG (each slot makes its own): the throw is cut off and every ball waits a moment.
    CancelChargingThrow();
    const double Until=GetWorld()->GetTimeSeconds()+RPGTuning::SimaeDropBallsSeconds;
    for(int32 Slot=0;Slot<3;++Slot) { ReadyAt[Slot]=FMath::Max(ReadyAt[Slot],Until); if(Attacks.IsValidIndex(Slot) && Attacks[Slot].IsValid()) { ItemReadyAt.Add(Attacks[Slot].Id,FMath::Max(ItemReadyAt.FindRef(Attacks[Slot].Id),Until)); } }
    ClearCarriedBalls(); UpdateBallPresentation();
}
void ARPGPrototypeCharacter::ApplyRPGStun(float Seconds)
{
    if(Seconds<=0 || IsEliminated() || IsDashing() || IsCrowdControlImmune()) { return; }
    // A guest moves itself: its own machine has to stop too.
    if(IsRemoteHero()) { ClientApplyStatus(TEXT("Stun"),1,Seconds); }
    if(const auto* Boss=Cast<ARPGPrototypeBoss>(this))
    {
        if(Boss->IsBossArmoredMove() || GetWorld()->GetTimeSeconds()<StunResistanceUntil) { return; }
        Seconds=FMath::Min(Seconds,.4f); StunResistanceUntil=GetWorld()->GetTimeSeconds()+3;
    }
    ApplyRPGStatus(TEXT("Stun"),1,FMath::Min(Seconds,RPGTuning::StunMax));
    CancelChargingThrow(); GetCharacterMovement()->StopMovementImmediately();
    // Unlike ApplyIceFreeze, never builds an ice block, applies frost overlays or pauses animations.
}
float ARPGPrototypeCharacter::GetRPGSnowScale() const
{
    const auto& I=GetAttack(SelectedAttack);
    if(I.Has(TEXT("SSmall"))) { return 1; }
    const float Max=FMath::Min(2.3f,1.2f*(1+I.Mod(TEXT("SnowMax"))*.01f));
    return FMath::Lerp(1.0f,Max,RPGSnowGrowth);
}
void ARPGPrototypeCharacter::RestoreHP(float Amount)
{
    // HP is the host's (a guest's own copy follows it).
    if(Amount<=0 || IsEliminated() || !HasAuthority()) { return; }
    const float Before=Health; Health=FMath::Min(MaxHealth,Health+Amount*(1+FMath::Min(RPGTuning::HealBonusCapPercent,BadgeMod(TEXT("GearHeal")))*.01f));
    if(Health>Before) { LastHealAt=GetWorld()->GetTimeSeconds(); }
}
void ARPGPrototypeCharacter::ReduceSpecialCooldowns(float Seconds)
{
    if(Seconds<=0) { return; }
    const double Now=GetWorld()->GetTimeSeconds();
    for(int32 S=1;S<3;++S) { ReadyAt[S]=FMath::Max(Now,ReadyAt[S]-Seconds); }
    for(auto& Pair:ItemReadyAt) { Pair.Value=FMath::Max(Now,Pair.Value-Seconds); }
}
void ARPGPrototypeCharacter::EmitDecoy(float Seconds)
{
    DecoyLocation=GetActorLocation(); DecoyUntil=GetWorld()->GetTimeSeconds()+Seconds;
    ARPGPrototypeEffect::Create(this,GetAttack(0),TEXT("Decoy"),DecoyLocation,60,Seconds,0);
}
void ARPGPrototypeCharacter::TickEquipment(float Dt)
{
    const double Now=GetWorld()->GetTimeSeconds();
    if(bWasEliminated && !IsEliminated()) { RevivedAt=Now; Shield=0; Statuses.Empty(); ComboTargets.Empty(); KnockbackDistance=0; KnockbackResistanceUntil=0; }
    bWasEliminated=IsEliminated(); if(IsEliminated()) { return; }
    if(ComboTargets.Num()>128) { ComboTargets.Empty(); }
    const FVector At=GetActorLocation(); const float Moved=FVector::Dist2D(At,LastTravelPosition);
    if(Moved<300) { TravelDistance=FMath::Min(500.0f,TravelDistance+Moved); } LastTravelPosition=At;
    if(IsDashing() && !bWasDashing)
    {
        LastDashAt=Now;
        if(BadgeMod(TEXT("MoveDecoy"))>0) { EmitDecoy(BadgeMod(TEXT("MoveDecoy"))); }
        if(bKillDashReady) { Stamina=FMath::Min(MaxStamina,Stamina+BadgeMod(TEXT("MoveKill"))*.01f); bKillDashReady=false; }
    }
    bWasDashing=IsDashing();
    if(IsDashing() && BadgeMod(TEXT("MoveDodge"))>0 && Now-LastDodgeAt>1)
    {
        for(TActorIterator<ARPGPrototypeBall> It(GetWorld());It;++It)
        {
            if(It->GetInstigator()!=this && It->IsFlyingOnServer() && FVector::DistSquared(It->GetActorLocation(),At)<FMath::Square(120.0f))
            { Stamina=FMath::Min(MaxStamina,Stamina+BadgeMod(TEXT("MoveDodge"))); LastDodgeAt=Now; break; }
        }
    }
    float SpeedBonus=BadgeMod(TEXT("Move"))+Status(TEXT("MoveBuff"));
    const bool bOutOfCombat=Now-LastDamagedAt>RPGTuning::OutOfCombatSeconds && Now-LastAttackAt>RPGTuning::OutOfCombatSeconds;
    if(bOutOfCombat) { SpeedBonus+=BadgeMod(TEXT("GearWalk")); }
    if(Now-LastKillAt<4) { SpeedBonus+=BadgeMod(TEXT("KillMove")); }
    if(Now-LastDamagedAt<2.5) { SpeedBonus+=BadgeMod(TEXT("HurtMove")); }
    if(Health<MaxHealth*.3f && BadgeMod(TEXT("MoveEscape"))>0)
    {
        for(TActorIterator<ARPGPrototypeCharacter> It(GetWorld());It;++It)
        { if(It->IsGrunt() && !It->IsEliminated() && FVector::DotProduct(GetVelocity(),At-It->GetActorLocation())>0
              && FVector::DistSquared2D(At,It->GetActorLocation())<FMath::Square(900.0f)) { SpeedBonus+=BadgeMod(TEXT("MoveEscape")); break; } }
    }
    if(IsChargingThrow()) { SpeedBonus+=BadgeMod(TEXT("ChargeMove"))*.35f; }
    const float GruntSpeed=SpawnPoint.IsValid() && SpawnPoint->bEncounterGroup ? (EncounterRole==ERPGGruntRole::Chaser?330.f:270.f) : 245.f;
    BaseMaxWalkSpeed=(bGrunt?GruntSpeed:VSBaseSpeed)*FMath::Clamp(1+(SpeedBonus-Status(TEXT("Slow")))*.01f,RPGTuning::MoveSpeedMin,RPGTuning::MoveSpeedMax);
    if(!IsTrainingMenuFrozen() && !IsIceFrozen()) { GetCharacterMovement()->MaxWalkSpeed=IsRPGStunned()?0:BaseMaxWalkSpeed; }
    StaminaRegenPerSecond=VSStaminaRegen*(1+(BadgeMod(TEXT("Stamina"))+(Now-LastAttackAt>3 && Moved>0?BadgeMod(TEXT("MoveBreathe")):0))*.01f);
    StaminaRecoveredPerBallHit=VSStaminaHit*(1+BadgeMod(TEXT("HitStamina"))*.01f);
    DashCooldownSeconds=VSDashCD*FMath::Max(RPGTuning::DashCooldownFloor,1-BadgeMod(TEXT("DashCD"))*.01f);
    if(!IsChargingThrow()) { MaxChargeSeconds=VSChargeTime*FMath::Max(RPGTuning::ChargeTimeFloor,1-(GetAttack(SelectedAttack).Mod(TEXT("Charge"))+BadgeMod(TEXT("GearCharge")))*.01f); }
    if(!bGrunt && bOutOfCombat) { RestoreHP(MaxHealth*BadgeMod(TEXT("Regen"))*.01f*Dt); }
    if(Status(TEXT("Smoke"))>0 && Now-LastAttackAt>2) { RestoreHP(MaxHealth*Status(TEXT("SmokeHeal"))*.01f*Dt); }
    if(Status(TEXT("Smoke"))<=0 && Status(TEXT("SmokeExit"))>0)
    { ApplyRPGStatus(TEXT("MoveBuff"),Status(TEXT("SmokeExit")),2); Statuses.Remove(TEXT("SmokeExit")); }
    if(Status(TEXT("BirdRefund"))>0)
    { Stamina=FMath::Min(MaxStamina,Stamina+Status(TEXT("BirdRefund"))); Statuses.Remove(TEXT("BirdRefund")); }
}
float ARPGPrototypeCharacter::OutgoingDamage(const FRPGPrototypeItem& I,ARPGPrototypeCharacter* Target,float Amount,bool bBounce)
{
    if(!Target || Target==this || Target->IsGrunt()==bGrunt) { return 0; }
    float Bonus=Target->Status(TEXT("Vulnerable"));
    const float Distance=FVector::Dist2D(GetActorLocation(),Target->GetActorLocation());
    if(Distance<=400) { Bonus+=I.Mod(TEXT("Near")); }
    if(Distance>=800) { Bonus+=I.Mod(TEXT("Far")); }
    if(FVector::DotProduct(Target->GetActorForwardVector(),(GetActorLocation()-Target->GetActorLocation()).GetSafeNormal2D())<-.3f) { Bonus+=I.Mod(TEXT("Rear")); }
    if(bBounce) { Bonus+=I.Mod(TEXT("Bounce"))+BadgeMod(TEXT("BankPower")); }
    if(Target->GetHealth()<Target->GetMaxHealth()*.25f) { Bonus+=BadgeMod(TEXT("Finish")); }
    if(I.Type==EChaosImpactBallType::Normal) { Bonus+=Target->Status(TEXT("NormalMark"))+Target->Status(TEXT("BlackNormal")); }
    else { Bonus+=Target->Status(TEXT("SpecialMark")); }
    if(BadgeMod(TEXT("TechFocus"))>0)
    {
        const auto* Combo=ComboTargets.Find(Target->GetUniqueID());
        if(Combo) { Bonus+=Combo->X*BadgeMod(TEXT("TechFocus")); }
    }
    if(I.Type==EChaosImpactBallType::Fire) { Bonus+=Target->Status(TEXT("Heat"))*I.Mod(TEXT("Heat")); }
    if(Target->IsIceFrozen()) { Bonus+=I.Mod(TEXT("Shatter")); }
    return Amount*(1+FMath::Clamp(Bonus,0.0f,RPGTuning::DamageBonusCapPercent)*.01f);
}
void ARPGPrototypeCharacter::RegisterRPGHit(ARPGPrototypeCharacter* Target,const FRPGPrototypeItem& I,float Applied,bool Proc)
{
    if(Applied<=0 || !Target || Proc) { return; }
    const double Now=GetWorld()->GetTimeSeconds();
    RecoverStaminaFromBallHit(); VolleyTargets.Add(Target);
    if(VolleyTargets.Num()>=3) { bCrowdReady=true; ApplyRPGStatus(TEXT("CrowdPierce"),BadgeMod(TEXT("TechCrowd")),8); }
    if(I.Type==EChaosImpactBallType::Normal)
    {
        ++NormalHits; ReduceSpecialCooldowns(BadgeMod(TEXT("TechCycle")));
        if(NormalHits%4==0) { ApplyRPGStatus(TEXT("NormalDouble"),1,3); }
        Target->ApplyRPGStatus(TEXT("SpecialMark"),I.Mod(TEXT("NormalMark")),3,this);
        Target->ApplyRPGStatus(TEXT("BirdMark"),1,3,this);
        Target->ClearRPGStatus(TEXT("NormalMark"));
    }
    else { Target->ClearRPGStatus(TEXT("SpecialMark")); }
    if(Now-LastDamagedAt<3 && Now-LastPaybackAt>3)
    { RestoreHP(MaxHealth*BadgeMod(TEXT("LifePayback"))*.01f); LastPaybackAt=Now; }
    if(BadgeMod(TEXT("TechFocus"))>0)
    {
        auto& C=ComboTargets.FindOrAdd(Target->GetUniqueID()); const int32 Kind=I.Type==EChaosImpactBallType::Normal?1:2;
        C.X=static_cast<int32>(C.Y)!=Kind?FMath::Min(3.0,C.X+1):0; C.Y=Kind;
    }
    if(I.Type==EChaosImpactBallType::Fire)
    { Target->ApplyRPGStatus(TEXT("Heat"),FMath::Min(3.0f,Target->Status(TEXT("Heat"))+1),5,this); Target->ApplyRPGStatus(TEXT("Ignition"),Target->Status(TEXT("Ignition"))+1,5,this); }
    if(I.Type==EChaosImpactBallType::Ice && Target->IsIceFrozen() && I.Has(TEXT("ICycle")) && Now-LastColdRefundAt>1)
    { ReduceSpecialCooldowns(I.Mod(TEXT("ICycle"))); LastColdRefundAt=Now; }
}
