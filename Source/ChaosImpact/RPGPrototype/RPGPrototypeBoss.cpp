#include "RPGPrototype.h"
#include "ChaosImpactPuppetComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

namespace
{
bool BossSight(const AActor* Boss,const AActor* Player)
{
    FHitResult Hit; FCollisionQueryParams Q(SCENE_QUERY_STAT(RPGBossSight),false,Boss); Q.AddIgnoredActor(Player);
    return !Boss->GetWorld()->LineTraceSingleByObjectType(Hit,Boss->GetActorLocation(),Player->GetActorLocation(),FCollisionObjectQueryParams(ECC_WorldStatic),Q);
}
}

ARPGPrototypeBoss::ARPGPrototypeBoss()
{
    SetEnemy(true,10); SetCPUNumber(900);
    AIControllerClass=AAIController::StaticClass(); AutoPossessAI=EAutoPossessAI::Disabled;
}
void ARPGPrototypeBoss::ConfigureLevel()
{
    MaxHealth=HealthForLevel(RPGLevel,FMath::Max(100.0f,HPAtLevel10)); Health=MaxHealth;
    if(Attacks.Num()==3)
    {
        Attacks[0]=FRPGPrototypeItem::Make(EChaosImpactBallType::Normal,RPGLevel);
        Attacks[0].Power=38+1.6f*(RPGLevel-10); Attacks[0].Cooldown=.6f;
    }
}
void ARPGPrototypeBoss::BeginPlay()
{
    SetEnemy(true,FMath::Max(10,StartingLevel)); SetCPUNumber(900);
    Super::BeginPlay(); Home=GetActorTransform(); ConfigureLevel();
    if(!GetController()) { if(auto* CPU=GetWorld()->SpawnActor<AAIController>()) { CPU->Possess(this); } }
    // The copied RPG pawn provides the hidden source skeleton, materials and animation timing.
    if(ToonCharacter) { ToonCharacter->DestroyComponent(); }
    ToonCharacter=NewObject<UChaosImpactPuppetComponent>(this);
    ToonCharacter->SetupAttachment(GetMesh()); ToonCharacter->SetRelativeScale3D(FVector(ToonCharacterScale)); ToonCharacter->RegisterComponent();
    if(ToonCharacter->Initialize(GetMesh(),5)) { ToonCharacter->SetHeldBalls(HeldBallMesh,LeftHeldBallMesh); }
    Enter(ERPGBossState::Idle,0);
}
void ARPGPrototypeBoss::Enter(ERPGBossState NewState,float Seconds)
{
    State=NewState; StateUntil=GetWorld()->GetTimeSeconds()+Seconds;
    GetCharacterMovement()->StopMovementImmediately(); ConsumeMovementInputVector();
    if(NewState==ERPGBossState::Recovery)
    { if(ToonCharacter) { ToonCharacter->SetBallPose(true); } }
    else if(ToonCharacter) { ToonCharacter->SetBallPose(false); }
}
FString ARPGPrototypeBoss::GetBossCue() const
{
    switch(State)
    {
    case ERPGBossState::VolleyTell: case ERPGBossState::Volley: return IsEnraged()?TEXT("5連投 — 弾の間を抜ける"):TEXT("3連投 — 横にかわす");
    case ERPGBossState::ChargeTell: return TEXT("突進予告 — 横へ回避");
    case ERPGBossState::Charge: return ChargesLeft>1?TEXT("2連突進"):TEXT("突進");
    case ERPGBossState::Recovery: return TEXT("攻撃チャンス！ ダメージ ×1.5");
    case ERPGBossState::ReturnHome: return TEXT("中央へ戻る");
    case ERPGBossState::LeapTell: case ERPGBossState::Leap: return TEXT("衝撃波 — ジャンプか範囲外へ");
    case ERPGBossState::Wave: return TEXT("衝撃波");
    case ERPGBossState::Defeated: return TEXT("撃破！ 近づいて再挑戦");
    default: return TEXT("近づくと戦闘開始");
    }
}
void ARPGPrototypeBoss::ClearEncounterAttacks()
{
    RequestCancelThrow();
    GetWorldTimerManager().ClearTimer(ThrowReleaseTimer);
    if(auto* Pending=PendingThrowBall.Get()) { Pending->Destroy(); }
    PendingThrowBall.Reset(); bThrowReleasePending=false;
    for(TActorIterator<ARPGPrototypeBall> It(GetWorld());It;++It) { if(It->GetInstigator()==this) { It->Destroy(); } }
    for(TActorIterator<ARPGPrototypeEffect> It(GetWorld());It;++It)
    { if(It->Source.Get()==this || It->Follow.Get()==this || FVector::DistSquared2D(It->GetActorLocation(),Home.GetLocation())<FMath::Square(1600.0f)) { It->Destroy(); } }
    WaveRadius=LastWaveRadius=0; ShotsLeft=ChargesLeft=0;
}
void ARPGPrototypeBoss::ResetEncounter()
{
    if(State==ERPGBossState::Defeated) { return; }
    if(auto* Mode=GetWorld()->GetAuthGameMode<ARPGPrototypeGameMode>()) { Mode->ClearEnemyContributions(this); }
    ClearEncounterAttacks(); Statuses.Empty(); StatusSources.Empty();
    for(TActorIterator<ARPGPrototypeBall> It(GetWorld());It;++It)
	{ const auto* P=Cast<ARPGPrototypeCharacter>(It->GetInstigator()); if(P && !P->IsGrunt()) { It->Destroy(); } }
    IceFrozenUntilServerTime=0; Health=MaxHealth; LastDamagedAt=-100;
    FreezeResistanceUntil=FreezeAcceptedUntil=0;
    KnockbackDistance=0; KnockbackResistanceUntil=0;
    SetActorTransform(Home,false,nullptr,ETeleportType::TeleportPhysics);
    GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    GetCharacterMovement()->MaxAcceleration=DefaultMaxAcceleration;
    MoveNumber=0; bEngaged=false; NextActionAt=GetWorld()->GetTimeSeconds()+1;
    Enter(ERPGBossState::Idle,0);
}
bool ARPGPrototypeBoss::CanRetry(const ARPGPrototypeCharacter* Player) const
{
    return State==ERPGBossState::Defeated && IsValid(Player) && !Player->IsEliminated() && !Player->IsBagOpen()
        && FVector::DistSquared2D(Player->GetActorLocation(),Home.GetLocation())<FMath::Square(850.0f) && BossSight(this,Player);
}
bool ARPGPrototypeBoss::Retry(ARPGPrototypeCharacter* Player)
{
    if(!CanRetry(Player)) { return false; }
    ConfigureLevel(); ResetAfterElimination(); Health=MaxHealth;
    bRewarded=false; State=ERPGBossState::Idle; ResetEncounter();
    Player->Notice=FString::Printf(TEXT("暴走マグロ Lv.%d に再挑戦"),RPGLevel); Player->NoticeUntil=GetWorld()->GetTimeSeconds()+3;
    return true;
}
float ARPGPrototypeBoss::TakeDamage(float Amount,const FDamageEvent& Event,AController* EventInstigator,AActor* Causer)
{
    const auto* Player=ARPGPrototypeGameMode::NearestPlayer(GetWorld(),GetActorLocation());
    if(State==ERPGBossState::Defeated || !Player) { return 0; }
    const float Applied=Super::TakeDamage(Amount*(State==ERPGBossState::Recovery?1.5f:1.0f),Event,EventInstigator,Causer);
    if(IsEliminated() && !bRewarded)
    {
        bRewarded=true; bEngaged=false; Enter(ERPGBossState::Defeated,0);
        if(auto* Mode=GetWorld()->GetAuthGameMode<ARPGPrototypeGameMode>()) { Mode->RewardEnemy(this,true); }
        ClearEncounterAttacks();
        // Retain the defeated encounter for an explicit retry instead of the shared CPU removal/respawn timer.
        GetWorldTimerManager().ClearTimer(EliminationCameraHoldTimer); GetWorldTimerManager().ClearTimer(RespawnTimer);
        APawn* Source=nullptr;
        if(EventInstigator) { Source=EventInstigator->GetPawn(); } else if(Causer) { Source=Causer->GetInstigator(); }
        auto* Winner=Cast<ARPGPrototypeCharacter>(Source);
        const int32 DefeatedLevel=RPGLevel;
        if(Winner && Source==Winner && !Winner->IsEliminated())
        {
            Winner->Notice=FString::Printf(TEXT("暴走マグロ Lv.%d 撃破！ 次は Lv.%d"),DefeatedLevel,DefeatedLevel+5);
            Winner->NoticeUntil=GetWorld()->GetTimeSeconds()+5;
        }
        RPGLevel=FMath::Min(MAX_int32-5,RPGLevel)+5;
    }
    return Applied;
}
void ARPGPrototypeBoss::BeginCharge(ARPGPrototypeCharacter* Player)
{
    LockedDirection=(Player->GetActorLocation()-GetActorLocation()).GetSafeNormal2D();
    SetAIAimDirection(LockedDirection); bContactHit=false; ChargeVictims.Empty();
    Enter(ERPGBossState::ChargeTell,.9f); PreviousPosition=GetActorLocation();
}
void ARPGPrototypeBoss::FireExtraShot(float Angle)
{
    // Ordinary follow-up shots track again; continuously walking one way no longer dodges the entire volley.
    FVector ShotDirection=LockedDirection;
    if(!IsEnraged())
    {
        if(const auto* Player=ARPGPrototypeGameMode::NearestPlayer(GetWorld(),GetActorLocation()))
        { ShotDirection=(Player->GetActorLocation()+Player->GetVelocity()*.12f-GetActorLocation()).GetSafeNormal2D(); }
    }
    ShotDirection=ShotDirection.RotateAngleAxis(FMath::FRandRange(-GetAimError(),GetAimError()),FVector::UpVector);
    FVector Mouth=GetActorLocation()+LockedDirection*100;
    if(ToonCharacter) { ToonCharacter->GetAuthoredMouthLocation(Mouth); }
    const FTransform Where(LockedDirection.Rotation(),Mouth);
    auto* Ball=GetWorld()->SpawnActorDeferred<ARPGPrototypeBall>(ARPGPrototypeBall::StaticClass(),Where,this,this,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!Ball) { return; }
    Ball->bSecondary=true; Ball->ShotItem=GetAttack(0); Ball->FinishSpawning(Where);
    Ball->Launch(ShotDirection.RotateAngleAxis(Angle,FVector::UpVector),1750,EChaosImpactBallFlightMode::Straight,0);
}
void ARPGPrototypeBoss::BeginReturnHome()
{
    const float FlightSeconds=1.35f/GetVerticalImpulseTempo();
    Enter(ERPGBossState::ReturnHome,FlightSeconds);
    const FVector Delta=Home.GetLocation()-GetActorLocation();
    // Ballistic return clears the low bank walls, rather than walking back through them or resetting the fight.
    FVector Velocity=Delta/FlightSeconds;
    Velocity.Z+=-.5f*GetCharacterMovement()->GetGravityZ()*FlightSeconds;
    SetAIAimDirection(Delta.GetSafeNormal2D());
    LaunchCharacter(Velocity,true,true);
}
void ARPGPrototypeBoss::Tick(float Dt)
{
    // Online: the host runs the boss; a guest's screen shows where it is.
    if(!HasAuthority()) { ARPGPrototypeCharacter::Tick(Dt); return; }
    auto* Player=ARPGPrototypeGameMode::NearestPlayer(GetWorld(),GetActorLocation());
    const double Now=GetWorld()->GetTimeSeconds();
    if(!Player && State!=ERPGBossState::Defeated && (IsBossActive() || Health<MaxHealth)) { ResetEncounter(); }
    // One short freeze is effective, followed by resistance; repeated fields cannot perma-lock the boss.
    if(IsBossArmoredMove()) { IceFrozenUntilServerTime=0; ClearRPGStatus(TEXT("Stun")); }
    else if(Now<FreezeResistanceUntil) { IceFrozenUntilServerTime=Now<FreezeAcceptedUntil?FMath::Min(IceFrozenUntilServerTime,FreezeAcceptedUntil):0; }
    else if(IsIceFrozen()) { FreezeAcceptedUntil=Now+.4; IceFrozenUntilServerTime=FMath::Min(IceFrozenUntilServerTime,FreezeAcceptedUntil); FreezeResistanceUntil=Now+3; }
    Super::Tick(Dt);
    if(!Player || Player->IsEliminated() || IsEliminated() || IsTrainingMenuFrozen() || IsIceFrozen() || IsRPGStunned() || IsRPGKnockedBack()) { return; }
    const float Distance=FVector::Dist2D(Player->GetActorLocation(),GetActorLocation());
    if(IsBossActive() && (Distance>2300 || FVector::Dist2D(GetActorLocation(),Home.GetLocation())>1700)) { ResetEncounter(); return; }
    const FVector Toward=(Player->GetActorLocation()-GetActorLocation()).GetSafeNormal2D();
    if(State==ERPGBossState::Idle)
    {
        GetCharacterMovement()->StopMovementImmediately();
        if(Distance>(bEngaged?1650:ActivationRange) || !BossSight(this,Player) || Now<NextActionAt) { return; }
        bEngaged=true;
        SetAIAimDirection(Toward); LockedDirection=Toward;
        const int32 Move=MoveNumber++%3;
        if(Move==0) { Enter(ERPGBossState::VolleyTell,.85f); }
        else if(Move==1) { ChargesLeft=IsEnraged()?2:1; BeginCharge(Player); }
        else { Enter(ERPGBossState::LeapTell,1.0f); }
    }
    else if(State==ERPGBossState::VolleyTell)
    {
        if(Now>=StateUntil)
        {
            LockedDirection=Toward.RotateAngleAxis(FMath::FRandRange(-GetAimError(),GetAimError()),FVector::UpVector);
            SetAIAimDirection(LockedDirection); BeginThrowInput(); Enter(ERPGBossState::Volley,.3f); ShotsLeft=IsEnraged()?4:2; NextShotAt=0;
        }
    }
    else if(State==ERPGBossState::Volley)
    {
        if(IsChargingThrow() && Now>=StateUntil) { EndThrowInput(); NextShotAt=Now+.36; StateUntil=Now+1.35; }
        if(NextShotAt>0 && Now>=NextShotAt && ShotsLeft>0)
        { const float Angles[]={30,-15,15,-30}; FireExtraShot(IsEnraged()?Angles[FMath::Clamp(ShotsLeft-1,0,3)]:0); --ShotsLeft; NextShotAt=Now+.22; }
        if(!IsChargingThrow() && Now>=StateUntil) { Enter(ERPGBossState::Idle,0); NextActionAt=Now+.55; }
    }
    else if(State==ERPGBossState::ChargeTell)
    { if(Now>=StateUntil) { Enter(ERPGBossState::Charge,.8f); PreviousPosition=GetActorLocation(); } }
    else if(State==ERPGBossState::Charge)
    {
        auto* Movement=GetCharacterMovement(); Movement->MaxWalkSpeed=1500; Movement->MaxAcceleration=18000;
        AddMovementInput(LockedDirection);
        for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
        {
            auto* Victim=Cast<ARPGPrototypeCharacter>(It->Get()->GetPawn()); if(!Victim || Victim->IsEliminated() || ChargeVictims.Contains(Victim)) { continue; }
            const FVector Closest=FMath::ClosestPointOnSegment(Victim->GetActorLocation(),PreviousPosition,GetActorLocation());
            if(FMath::Abs(Victim->GetActorLocation().Z-GetActorLocation().Z)<150
                && FVector::DistSquared2D(Closest,Victim->GetActorLocation())<FMath::Square(115.0f) && BossSight(this,Victim))
            { ChargeVictims.Add(Victim); UGameplayStatics::ApplyDamage(Victim,GetAttack(0).Power*1.25f,GetController(),this,nullptr); }
        }
        PreviousPosition=GetActorLocation();
        // Walls simply block movement. No crash damage, rebound, or wall-dependent down state.
        if(Now>=StateUntil)
        { Movement->MaxAcceleration=DefaultMaxAcceleration; if(--ChargesLeft>0) { BeginCharge(Player); } else { Enter(ERPGBossState::Recovery,2.2f); } }
    }
    else if(State==ERPGBossState::Recovery)
    { if(Now>=StateUntil) { BeginReturnHome(); } }
    else if(State==ERPGBossState::ReturnHome)
    {
        if(Now>=StateUntil)
        {
            // Correct the final few units / a blocked landing to the authored safe home, without healing or clearing attacks.
            FVector Landing=Home.GetLocation(); FRotator Rotation=Home.Rotator();
            if(GetWorld()->FindTeleportSpot(this,Landing,Rotation)) { SetActorLocation(Landing,false,nullptr,ETeleportType::TeleportPhysics); }
            GetCharacterMovement()->SetMovementMode(MOVE_Walking);
            Enter(ERPGBossState::Idle,0); NextActionAt=Now+.35;
        }
    }
    else if(State==ERPGBossState::LeapTell)
    {
        if(Now>=StateUntil) { Enter(ERPGBossState::Leap,1.5f/GetVerticalImpulseTempo()); LaunchCharacter(FVector(0,0,600*GetVerticalImpulseTempo()),false,true); }
    }
    else if(State==ERPGBossState::Leap)
    {
        if(GetCharacterMovement()->IsMovingOnGround() || Now>=StateUntil)
        { Enter(ERPGBossState::Wave,1.2f); WaveRadius=LastWaveRadius=0; bWaveHit=false; WaveVictims.Empty(); }
    }
    else if(State==ERPGBossState::Wave)
    {
        LastWaveRadius=WaveRadius; WaveRadius=1000*(1-FMath::Clamp(static_cast<float>((StateUntil-Now)/1.2),0.0f,1.0f));
        const float FloorZ=GetActorLocation().Z-GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
        {
            auto* Victim=Cast<ARPGPrototypeCharacter>(It->Get()->GetPawn()); if(!Victim || Victim->IsEliminated() || WaveVictims.Contains(Victim)) { continue; }
            const float Range=FVector::Dist2D(Victim->GetActorLocation(),GetActorLocation());
            const float Feet=Victim->GetActorLocation().Z-Victim->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
            if(Range>=LastWaveRadius-65 && Range<=WaveRadius+65 && Feet<FloorZ+65 && BossSight(this,Victim))
            { WaveVictims.Add(Victim); UGameplayStatics::ApplyDamage(Victim,GetAttack(0).Power,GetController(),this,nullptr); }
        }
        if(Now>=StateUntil) { WaveRadius=0; Enter(ERPGBossState::Idle,0); NextActionAt=Now+.65; }
    }
}
