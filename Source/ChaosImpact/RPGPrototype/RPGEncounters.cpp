#include "RPGPrototype.h"
#include "RPGEnemyMoves.h"
#include "RPGTuning.h"
#include "RPGAdventure.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "GameFramework/CharacterMovementComponent.h"

bool ARPGPrototypeSpawnPoint::FindMemberFloor(int32 Slot, FVector& At) const
{
    const int32 CloseCount=FMath::Clamp(ChaserCount,0,8);
    const bool Close=Slot<CloseCount;
    const int32 InRow=Close?Slot:Slot-CloseCount;
    const int32 RowCount=Close?CloseCount:FMath::Clamp(ThrowerCount,0,8);
    const FVector Offset(Close?140.f:-180.f,(InRow-(RowCount-1)*.5f)*145.f,0);
    FCollisionQueryParams Q(SCENE_QUERY_STAT(RPGCampSpawn),false,this);
    for(int32 Try=0;Try<13;++Try)
    {
        const FVector Spread=Try==0?FVector::ZeroVector:FRotator(0,(Try-1)*60,0).Vector()*(Try<=6?150.f:300.f);
        const FVector Probe=GetActorTransform().TransformPosition(Offset+Spread);
        FHitResult Floor;
        if(!GetWorld()->LineTraceSingleByObjectType(Floor,Probe+FVector(0,0,100),Probe-FVector(0,0,380),
            FCollisionObjectQueryParams(ECC_WorldStatic),Q) || Floor.ImpactNormal.Z<.65f) { continue; }
        const FVector Candidate=Floor.ImpactPoint+FVector(0,0,99);
        if(FMath::Abs(Candidate.Z-GetActorLocation().Z)>180.f) { continue; }
        if(const auto* Adventure=ARPGAdventureWorld::Find(GetWorld());Adventure && Adventure->IsInColony(Candidate)) { continue; }
        if(GetWorld()->OverlapAnyTestByObjectType(Candidate,FQuat::Identity,FCollisionObjectQueryParams(ECC_WorldStatic),
            FCollisionShape::MakeCapsule(43,95),Q)) { continue; }
        bool Occupied=false;
        for(const auto& Member:Members) { if(Member.IsValid() && FVector::DistSquared2D(Member->GetActorLocation(),Candidate)<FMath::Square(100.f)) { Occupied=true; break; } }
        if(!Occupied) { At=Candidate; return true; }
    }
    return false;
}

void ARPGPrototypeSpawnPoint::AlertTo(ARPGPrototypeCharacter* Player)
{
    if(!bEncounterGroup || !Player || Player->IsGrunt() || Player->IsEliminated()) { return; }
    if(FVector::DistSquared2D(Player->GetActorLocation(),GetActorLocation())<=FMath::Square(PursuitRadius)) { EncounterTarget=Player; }
}

void ARPGPrototypeSpawnPoint::TickEncounter(ARPGPrototypeGameMode* Mode,double Now)
{
    if(!Mode || !bEnabled) { EncounterTarget.Reset(); return; }
    auto* Nearby=ARPGPrototypeGameMode::NearestPlayer(GetWorld(),GetActorLocation());
    const float Distance=Nearby?FVector::Dist2D(Nearby->GetActorLocation(),GetActorLocation()):FLT_MAX;
    const auto* Adventure=ARPGAdventureWorld::Find(GetWorld());
    auto Allowed=[&](const ARPGPrototypeCharacter* P)
    { return P && !P->IsEliminated() && !P->IsDowned() && (!Adventure || !Adventure->IsInColony(P->GetActorLocation())); };
    if(!Allowed(EncounterTarget.Get()) || FVector::DistSquared2D(EncounterTarget->GetActorLocation(),GetActorLocation())>FMath::Square(PursuitRadius))
    { EncounterTarget.Reset(); }
    if(!EncounterTarget.IsValid() && Allowed(Nearby) && Distance<=EngageRadius) { EncounterTarget=Nearby; }

    bool Alive=false;
    for(const auto& Member:Members) { if(Member.IsValid() && !Member->IsEliminated()) { Alive=true; break; } }
    if(bWaveSpawned && !Alive)
    {
        EncounterTarget.Reset(); bCleared=true;
        if(Now<NextSpawnAt || Distance<EngageRadius+250.f) { return; }
        Members.Reset(); Enemy.Reset(); bWaveSpawned=false;
    }
    if(bWaveSpawned && Distance>PreloadRadius+1200.f)
    {
        // Unload without KO/rewards; don't keep every camp's animated characters alive across the whole map.
        for(auto& Member:Members) { if(Member.IsValid()) { if(auto* C=Member->GetController()) { C->Destroy(); } Member->Destroy(); } }
        Members.Reset(); Enemy.Reset(); bWaveSpawned=false; EncounterTarget.Reset();
        NextSpawnAt=Now+FMath::Max(RespawnSeconds,10.f);
        return;
    }
    if(bWaveSpawned || Distance>PreloadRadius || Now<NextSpawnAt || (bCleared && Distance<EngageRadius+250.f)) { return; }
    const int32 Total=FMath::Clamp(ChaserCount,0,8)+FMath::Clamp(ThrowerCount,0,8);
    if(Total==0) { return; }
    // Who does what: the group's chasers and throwers, some of them (by its level) a charger, a bomber or a sniper.
    TArray<ERPGGruntRole> Roles;
    for(int32 Slot=0;Slot<Total;++Slot) { Roles.Add(Slot<FMath::Clamp(ChaserCount,0,8)?ERPGGruntRole::Chaser:ERPGGruntRole::Thrower); }
    const auto Swap=[&](ERPGGruntRole From,ERPGGruntRole To) { const int32 At=Roles.FindLast(From); if(At!=INDEX_NONE) { Roles[At]=To; } };
    if(EnemyLevel>=3) { Swap(ERPGGruntRole::Chaser,ERPGGruntRole::Charger); }
    if(EnemyLevel>=5) { Swap(ERPGGruntRole::Thrower,ERPGGruntRole::Bomber); }
    if(EnemyLevel>=7) { Swap(ERPGGruntRole::Thrower,ERPGGruntRole::Sniper); }
    if(EnemyLevel>=9) { Swap(ERPGGruntRole::Chaser,ERPGGruntRole::Bomber); }
    const int32 EliteSlot=EnemyLevel>=RPGTuning::EliteFromLevel && FMath::FRand()<RPGTuning::EliteChance?FMath::RandRange(0,Total-1):INDEX_NONE;
    // Now and then a named one with them (one at a time in the whole world).
    int32 NamedSlot=INDEX_NONE;
    if(EnemyLevel>=RPGTuning::NamedFromLevel && (FMath::FRand()<RPGTuning::NamedChance || FParse::Param(FCommandLine::Get(),TEXT("CINamed"))))
    {
        bool bAny=false;
        for(TActorIterator<ARPGPrototypeCharacter> It(GetWorld());It;++It) { if(It->IsNamed() && !It->IsEliminated()) { bAny=true; break; } }
        if(!bAny) { NamedSlot=FMath::RandRange(0,Total-1); }
    }
    for(int32 Slot=0;Slot<Total;++Slot)
    {
        FVector At;
        if(!FindMemberFloor(Slot,At)) { continue; }
        const FTransform Where(GetActorRotation(),At);
        auto* Grunt=GetWorld()->SpawnActorDeferred<ARPGPrototypeCharacter>(Mode->GruntClass,Where,this,nullptr,
            ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
        if(!Grunt) { continue; }
        Grunt->SetEnemy(true,EnemyLevel); Grunt->SpawnPoint=this; Grunt->EncounterHome=At;
        Grunt->EncounterRole=Roles[Slot];
        Grunt->SetCPUNumber(Slot+1); Grunt->FinishSpawning(Where);
        if(Slot==NamedSlot) { Grunt->MakeNamed(); } else if(Slot==EliteSlot) { Grunt->MakeElite(); }
        Grunt->EncounterHome=Grunt->GetActorLocation();

        auto* CPU=GetWorld()->SpawnActor<ARPGPrototypeGruntController>();
        if(CPU) { CPU->Possess(Grunt); }
        // Bind through the game mode, just like the legacy single-enemy spawner.
        FScriptDelegate D; D.BindUFunction(Mode,TEXT("HandleEliminated")); Grunt->OnEliminated.AddUnique(D);
        Members.Add(Grunt); if(!Enemy.IsValid()) { Enemy=Grunt; }
    }
    bWaveSpawned=!Members.IsEmpty(); bCleared=false;
    if(!bWaveSpawned) { NextSpawnAt=Now+3; }
}

int32 ARPGPrototypeGameMode::GetActiveGruntAttackers(const ARPGPrototypeCharacter* Target) const
{
    int32 Count=0;
    const double Now=GetWorld()->GetTimeSeconds();
    for(const auto& Slot:GruntAttackWindows)
    { if(Slot.Key.IsValid() && !Slot.Key->IsEliminated() && Slot.Value.Until>Now && Slot.Value.Target.Get()==Target) { ++Count; } }
    return Count;
}
bool ARPGPrototypeGameMode::ReserveGruntAttack(ARPGPrototypeCharacter* Grunt,ARPGPrototypeCharacter* Target,float Seconds)
{
    if(!Grunt || !Target || Grunt->IsEliminated() || Target->IsEliminated()) { return false; }
    const double Now=GetWorld()->GetTimeSeconds();
    for(auto It=GruntAttackWindows.CreateIterator();It;++It)
    { if(!It.Key().IsValid() || It.Key()->IsEliminated() || It.Value().Until<=Now) { It.RemoveCurrent(); } }
    if(GruntAttackWindows.Contains(Grunt) || GetActiveGruntAttackers(Target)>=2 || Now<NextGruntAttackAt.FindRef(Target)) { return false; }
    auto& Slot=GruntAttackWindows.Add(Grunt); Slot.Target=Target; Slot.Until=Now+FMath::Clamp(Seconds,.4f,2.f);
    NextGruntAttackAt.Add(Target,Now+.28);
    return true;
}

void ARPGPrototypeGruntController::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn);
    if(const auto* Grunt=Cast<ARPGPrototypeCharacter>(InPawn);Grunt && Grunt->SpawnPoint.IsValid() && Grunt->SpawnPoint->bEncounterGroup)
    { SetActorTickInterval(0.f); NextThrowAt=GetWorld()->GetTimeSeconds()+.4+Grunt->GetCPUNumber()*.16; }
}

void ARPGPrototypeGruntController::TickEncounter(float DeltaSeconds)
{
    auto* Grunt=Cast<ARPGPrototypeCharacter>(GetPawn());
    if(!Grunt || Grunt->IsEliminated() || Grunt->IsTrainingMenuFrozen() || Grunt->IsIceFrozen()
        || Grunt->IsRPGStunned() || Grunt->IsRPGKnockedBack()) { return; }
    auto* Camp=Grunt->SpawnPoint.Get();
    auto* Mode=GetWorld()->GetAuthGameMode<ARPGPrototypeGameMode>();
    if(!Camp || !Mode) { return; }
    auto* Target=Camp->GetEncounterTarget();
    const double Now=GetWorld()->GetTimeSeconds();
    const bool Aggro=Camp->bEnabled && Target && !Target->IsEliminated();
    // Standing still for its own warned attack (the charge itself is the warning's doing).
    if(Now<HoldUntil) { return; }
    if(Grunt->IsChargingThrow())
    {
        if(!Aggro) { Grunt->CancelChargingThrow(); NextThrowAt=Now+1; }
        else if(Now>=ReleaseAt) { Grunt->EndThrowInput(); NextThrowAt=Now+(Grunt->EncounterRole==ERPGGruntRole::Chaser?2.0:2.5)+FMath::FRandRange(0,.5); }
        return;
    }
    if(Now>=NextEncounterThinkAt)
    {
        NextEncounterThinkAt=Now+.10;
        const FVector Here=Grunt->GetActorLocation();
        FVector Desired=FVector::ZeroVector;
        const bool Chaser=Grunt->EncounterRole==ERPGGruntRole::Chaser;
        const float Distance=Aggro?FVector::Dist2D(Target->GetActorLocation(),Here):FLT_MAX;
        const bool Decoy=Aggro && Now<Target->DecoyUntil && FVector::DistSquared2D(Here,Target->DecoyLocation)<FMath::Square(900.f);
        const FVector Goal=Aggro?(Decoy?Target->DecoyLocation:Target->GetActorLocation()):Grunt->EncounterHome;
        const FVector Toward=(Goal-Here).GetSafeNormal2D();
        FCollisionQueryParams Q(SCENE_QUERY_STAT(RPGEncounterMove),false,Grunt);
        FHitResult Wall;
        const bool Blocked=Aggro && GetWorld()->LineTraceSingleByObjectType(Wall,Here,Goal,FCollisionObjectQueryParams(ECC_WorldStatic),Q);
        if(Now>=NextWanderAt) { StrafeSign=FMath::RandBool()?1:-1; NextWanderAt=Now+FMath::FRandRange(1.5f,2.8f); }
        // How close each keeps: chasers in your face, chargers at a run-up, bombers and snipers well back.
        const ERPGGruntRole GruntRole=Grunt->EncounterRole;
        const float Far=Chaser?300:GruntRole==ERPGGruntRole::Charger?650:GruntRole==ERPGGruntRole::Bomber?900:GruntRole==ERPGGruntRole::Sniper?1400:780;
        const float Near=Chaser?145:GruntRole==ERPGGruntRole::Charger?300:GruntRole==ERPGGruntRole::Bomber?550:GruntRole==ERPGGruntRole::Sniper?1000:470;
        if(!Aggro)
        {
            const FVector Patrol=Grunt->EncounterHome+FVector(FMath::Cos(Now*.3+Grunt->GetCPUNumber()),FMath::Sin(Now*.3+Grunt->GetCPUNumber()),0)*55;
            if(FVector::DistSquared2D(Patrol,Here)>FMath::Square(45.f)) { Desired=(Patrol-Here).GetSafeNormal2D(); }
        }
        else if(TrySpecialAttack(Grunt,Target,Distance,Blocked,Now)) { EncounterMove=FVector::ZeroVector; return; }
        else if(Blocked || Distance>Far) { Desired=Toward; }
        else if(Distance<Near) { Desired=-Toward; }
        else { Desired=FVector::CrossProduct(FVector::UpVector,Toward)*StrafeSign*.6f; }
        FVector Separate=FVector::ZeroVector;
        for(const auto& Other:Camp->Members)
        { if(Other.IsValid() && Other.Get()!=Grunt && !Other->IsEliminated()) { const FVector Away=Here-Other->GetActorLocation(); if(Away.SizeSquared2D()<FMath::Square(145.f)) { Separate+=Away.GetSafeNormal2D()*.7f; } } }
        Desired=(Desired+Separate).GetSafeNormal2D();
        EncounterMove=FVector::ZeroVector;
        for(float Angle:{0.f,40.f,-40.f,80.f,-80.f,130.f,-130.f})
        {
            if(Desired.IsNearlyZero()) { break; }
            const FVector Move=Desired.RotateAngleAxis(Angle*StrafeSign,FVector::UpVector);
            const FVector End=Here+Move*140;
            if(FVector::DistSquared2D(End,Camp->GetActorLocation())>FMath::Square(Camp->PursuitRadius+250)) { continue; }
            if(const auto* Place=ARPGAdventureWorld::Find(GetWorld());Place && Place->IsInColony(End)) { continue; }
            FHitResult Obstacle,Floor;
            if(GetWorld()->SweepSingleByObjectType(Obstacle,Here,End,FQuat::Identity,FCollisionObjectQueryParams(ECC_WorldStatic),FCollisionShape::MakeSphere(37),Q)) { continue; }
            if(!GetWorld()->LineTraceSingleByObjectType(Floor,End,End-FVector(0,0,220),FCollisionObjectQueryParams(ECC_WorldStatic),Q) || Floor.ImpactNormal.Z<.65f) { continue; }
            EncounterMove=Move; break;
        }
        EncounterMoveScale=Aggro?1.f:.65f;
        if(Aggro && (Chaser || GruntRole==ERPGGruntRole::Thrower))
        {
            Grunt->SetAIAimDirection(Toward);
            const float Range=Chaser?420.f:1150.f;
            if(Distance<Range && !Blocked && Now>=NextThrowAt && Grunt->GetCarriedBallCount()>0 && !Grunt->IsThrowReleasePending())
            {
                const float Flight=Distance/FMath::Max(Grunt->GetThrowSpeedForCharge(.3f,true),500.f);
                if(Mode->ReserveGruntAttack(Grunt,Target,.40f+Flight+.35f))
                {
                    const FVector Lead=Decoy?FVector::ZeroVector:(Target->GetVelocity()*(.30f+Flight)*(Chaser?.6f:.8f)).GetClampedToMaxSize2D(350);
                    const float Error=(Chaser?5.f:3.f)+Grunt->GetAimError();
                    Grunt->SetAIAimDirection((Goal+Lead-Here).GetSafeNormal2D().RotateAngleAxis(FMath::FRandRange(-Error,Error),FVector::UpVector));
                    Grunt->BeginThrowInput(); ReleaseAt=Now+.32;
                    EncounterMove=FVector::ZeroVector;
                }
            }
        }
    }
    // Supply input every frame, unlike the legacy controller's 0.1s-tick movement pulses.
    if(!EncounterMove.IsNearlyZero()) { Grunt->AddMovementInput(EncounterMove,EncounterMoveScale); }
}

bool ARPGPrototypeGruntController::TrySpecialAttack(ARPGPrototypeCharacter* Grunt,ARPGPrototypeCharacter* Target,float Distance,bool bBlocked,double Now)
{
    const ERPGGruntRole GruntRole=Grunt->EncounterRole;
    if(GruntRole==ERPGGruntRole::Chaser || GruntRole==ERPGGruntRole::Thrower || Now<NextThrowAt) { return false; }
    auto* Mode=GetWorld()->GetAuthGameMode<ARPGPrototypeGameMode>();
    const FVector Here=Grunt->GetActorLocation();
    const FVector Toward=(Target->GetActorLocation()-Here).GetSafeNormal2D();
    // Its hits grow with its level (an elite's harder still).
    const float Power=(RPGTuning::EnemyPowerBase+(Grunt->GetRPGLevel()-1)*RPGTuning::EnemyPowerPerLevel)*(Grunt->IsElite()?RPGTuning::ElitePower:1.0f);
    if(GruntRole==ERPGGruntRole::Charger)
    {
        if(Distance>900 || bBlocked || !Mode || !Mode->ReserveGruntAttack(Grunt,Target,1.3f)) { return false; }
        Grunt->SetAIAimDirection(Toward);
        ARPGGroundStrike::Lane(Grunt,ARPGGroundStrike::Charge,Here,Toward,FMath::Min(Distance+250,1000.0f),95,.75f,Power*1.25f);
        HoldUntil=Now+.75; NextThrowAt=Now+4.2+FMath::FRandRange(0.0,1.0);
        return true;
    }
    if(GruntRole==ERPGGruntRole::Bomber)
    {
        if(Distance>1150 || !Mode || !Mode->ReserveGruntAttack(Grunt,Target,1.4f)) { return false; }
        // Where they are about to be.
        const FVector Lead=(Target->GetVelocity()*.8f).GetClampedToMaxSize2D(400);
        ARPGGroundStrike::Lob(Grunt,Target->GetActorLocation()+Lead,FMath::Min(210.0f+Grunt->GetRPGLevel()*3,280.0f),1.15f,Power*1.3f);
        HoldUntil=Now+.4; NextThrowAt=Now+4.0+FMath::FRandRange(0.0,1.0);
        return true;
    }
    // The sniper: a long thin line, then a shot straight through.
    if(Distance>1800 || bBlocked || !Mode || !Mode->ReserveGruntAttack(Grunt,Target,1.6f)) { return false; }
    const FVector Aim=(Target->GetActorLocation()+(Target->GetVelocity()*.5f).GetClampedToMaxSize2D(300)-Here).GetSafeNormal2D();
    Grunt->SetAIAimDirection(Aim);
    ARPGGroundStrike::Lane(Grunt,ARPGGroundStrike::Snipe,Here+Aim*60,Aim,1900,50,1.25f,Power*1.6f);
    HoldUntil=Now+1.25; NextThrowAt=Now+5.0+FMath::FRandRange(0.0,1.2);
    return true;
}
