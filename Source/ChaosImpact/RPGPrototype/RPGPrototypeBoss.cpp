#include "RPGPrototype.h"
#include "RPGPrototypeVFX.h"
#include "ChaosImpactPuppetComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

namespace
{
bool BossSight(const AActor* Boss,const AActor* Player)
{
    FHitResult Hit; FCollisionQueryParams Q(SCENE_QUERY_STAT(RPGBossSight),false,Boss); Q.AddIgnoredActor(Player);
    return !Boss->GetWorld()->LineTraceSingleByObjectType(Hit,Boss->GetActorLocation(),Player->GetActorLocation(),FCollisionObjectQueryParams(ECC_WorldStatic),Q);
}

// The bosses (2026-11): one class, told apart by these. Moves: 1 volley, 2 charge, 4 leap and wave, 8 geysers, 16 whirlpool.
struct FBossKindDef
{
    const TCHAR* Name;
    const TCHAR* StrongName;
    int32 Look;
    float Scale;
    EChaosImpactBallType Ball;
    int32 Level;
    float HP;
    int32 Moves;
    /** What its geysers and whirlpool look like. */
    EChaosImpactBallType Splash;
};
const FBossKindDef& KindDef(ERPGBossKind Kind)
{
    static const FBossKindDef Defs[]={
        {TEXT("暴走マグロ"),TEXT("真・暴走マグロ"),5,1.0f,EChaosImpactBallType::Normal,10,1800,31,EChaosImpactBallType::Ice},
        {TEXT("ほたる沼の主"),TEXT("真・ほたる沼の主"),12,1.15f,EChaosImpactBallType::Smoke,12,2300,1|4|8|16,EChaosImpactBallType::Smoke},
        {TEXT("砂に眠る巨兵"),TEXT("真・砂に眠る巨兵"),7,1.35f,EChaosImpactBallType::Normal,15,3200,1|2|4|16,EChaosImpactBallType::Wind},
        {TEXT("火口の番人"),TEXT("真・火口の番人"),9,1.2f,EChaosImpactBallType::Fire,20,3900,1|2|4|8,EChaosImpactBallType::Fire},
        {TEXT("石柱の王"),TEXT("真・石柱の王"),11,1.2f,EChaosImpactBallType::Thunder,18,3500,1|2|8|16,EChaosImpactBallType::Thunder},
        {TEXT("伝説の投手 バードストライカー"),TEXT("真・伝説の投手 バードストライカー"),13,1.3f,EChaosImpactBallType::Black,32,8800,31,EChaosImpactBallType::Black},
    };
    return Defs[FMath::Clamp(static_cast<int32>(Kind),0,static_cast<int32>(UE_ARRAY_COUNT(Defs))-1)];
}
/** The strong version: stronger, faster, more of everything. */
constexpr float StrongHP=1.7f, StrongPower=1.35f, StrongLevels=8;
}

ARPGPrototypeBoss::ARPGPrototypeBoss()
{
    SetEnemy(true,10); SetCPUNumber(900);
    AIControllerClass=AAIController::StaticClass(); AutoPossessAI=EAutoPossessAI::Disabled;
}
void ARPGPrototypeBoss::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ARPGPrototypeBoss,Kind);
    DOREPLIFETIME(ARPGPrototypeBoss,State);
    DOREPLIFETIME(ARPGPrototypeBoss,LockedDirection);
    DOREPLIFETIME(ARPGPrototypeBoss,WaveRadius);
    DOREPLIFETIME(ARPGPrototypeBoss,bEngaged);
    DOREPLIFETIME(ARPGPrototypeBoss,ChargesLeft);
    DOREPLIFETIME(ARPGPrototypeBoss,Tier);
    DOREPLIFETIME(ARPGPrototypeBoss,GeyserSpots);
    DOREPLIFETIME(ARPGPrototypeBoss,bAsleep);
    DOREPLIFETIME(ARPGPrototypeBoss,StrongLeft);
}
FString ARPGPrototypeBoss::GetBossName() const { const auto& D=KindDef(Kind); return Tier>0?D.StrongName:D.Name; }
int32 ARPGPrototypeBoss::GetBossId() const { return Kind==ERPGBossKind::Maguro?0:static_cast<int32>(Kind)+1; }
void ARPGPrototypeBoss::ConfigureLevel()
{
    const auto& D=KindDef(Kind);
    // Maguro keeps its own tuning (its level and health are set on the actor); the others take theirs from the table.
    const float Base=Kind==ERPGBossKind::Maguro?FMath::Max(100.0f,HPAtLevel10):D.HP;
    BaseMaxHealth=HealthForLevel(RPGLevel,Base)*(Tier>0?StrongHP:1.0f);
    MaxHealth=BaseMaxHealth; Health=MaxHealth;
    if(Attacks.Num()==3)
    {
        Attacks[0]=FRPGPrototypeItem::Make(D.Ball,RPGLevel);
        Attacks[0].Power=(38+1.6f*(RPGLevel-10))*(Tier>0?StrongPower:1.0f); Attacks[0].Cooldown=.6f;
    }
}
void ARPGPrototypeBoss::BeginPlay()
{
    const auto& D=KindDef(Kind);
    if(Kind!=ERPGBossKind::Maguro) { StartingLevel=FMath::Max(StartingLevel,D.Level); }
    // Its own model: the base character would otherwise put the default one back (a boss has no player state).
    LookOverride=D.Look;
    SetEnemy(true,FMath::Max(10,StartingLevel)); SetCPUNumber(900);
    Super::BeginPlay(); Home=GetActorTransform(); ConfigureLevel();
    if(HasAuthority() && !GetController()) { if(auto* CPU=GetWorld()->SpawnActor<AAIController>()) { CPU->Possess(this); } }
    // The copied RPG pawn provides the hidden source skeleton, materials and animation timing.
    if(ToonCharacter) { ToonCharacter->DestroyComponent(); }
    ToonCharacter=NewObject<UChaosImpactPuppetComponent>(this);
    ToonCharacter->SetupAttachment(GetMesh()); ToonCharacter->SetRelativeScale3D(FVector(ToonCharacterScale*D.Scale)); ToonCharacter->RegisterComponent();
    if(ToonCharacter->Initialize(GetMesh(),D.Look)) { ToonCharacter->SetHeldBalls(HeldBallMesh,LeftHeldBallMesh); }
    // The legend waits until someone here has beaten every other boss.
    bAsleep=Kind==ERPGBossKind::Legend;
    Enter(ERPGBossState::Idle,0);
}
void ARPGPrototypeBoss::Enter(ERPGBossState NewState,float Seconds)
{
    State=NewState; StateUntil=GetWorld()->GetTimeSeconds()+Seconds;
    GetCharacterMovement()->StopMovementImmediately(); ConsumeMovementInputVector();
    OnRep_BossState();
}
void ARPGPrototypeBoss::OnRep_BossState()
{
    // Worn out after a charge: it holds its ball up (every screen).
    if(ToonCharacter) { ToonCharacter->SetBallPose(State==ERPGBossState::Recovery); }
}
FString ARPGPrototypeBoss::GetBossCue() const
{
    switch(State)
    {
    case ERPGBossState::VolleyTell: case ERPGBossState::Volley: return IsEnraged()?TEXT("5連投 — 弾の間を抜ける"):TEXT("3連投 — 横にかわす");
    case ERPGBossState::ChargeTell: return TEXT("突進予告 — 横へ回避");
    case ERPGBossState::Charge: return ChargesLeft>1?TEXT("連続突進"):TEXT("突進");
    case ERPGBossState::Recovery: return TEXT("攻撃チャンス！ ダメージ ×1.5");
    case ERPGBossState::ReturnHome: return TEXT("中央へ戻る");
    case ERPGBossState::LeapTell: case ERPGBossState::Leap: return TEXT("衝撃波 — ジャンプか範囲外へ");
    case ERPGBossState::Wave: return TEXT("衝撃波");
    case ERPGBossState::GeyserTell: case ERPGBossState::Geyser: return TEXT("足元が噴き上がる — 円から出る");
    case ERPGBossState::VortexTell: case ERPGBossState::Vortex: return TEXT("渦 — 引き寄せに逆らって外へ");
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
    WaveRadius=LastWaveRadius=0; ShotsLeft=ChargesLeft=0; GeyserSpots.Reset();
}
void ARPGPrototypeBoss::ResetEncounter()
{
    if(State==ERPGBossState::Defeated) { return; }
    if(auto* Mode=GetWorld()->GetAuthGameMode<ARPGPrototypeGameMode>()) { Mode->ClearEnemyContributions(this); }
    ClearEncounterAttacks(); Statuses.Empty(); StatusSources.Empty();
    for(TActorIterator<ARPGPrototypeBall> It(GetWorld());It;++It)
	{ const auto* P=Cast<ARPGPrototypeCharacter>(It->GetInstigator()); if(P && !P->IsGrunt()) { It->Destroy(); } }
    IceFrozenUntilServerTime=0; MaxHealth=BaseMaxHealth; Health=MaxHealth; LastDamagedAt=-100;
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
    // A guest asks the host (the boss is the host's).
    if(!HasAuthority()) { Player->ServerRetryBoss(this); return true; }
    // Once beaten it comes back as its strong version.
    Tier=Defeats>0?1:0;
    ConfigureLevel(); ResetAfterElimination(); Health=MaxHealth;
    bRewarded=false; State=ERPGBossState::Idle; ResetEncounter();
    Player->ShowNotice(FString::Printf(TEXT("%s Lv.%d に挑戦"),*GetBossName(),RPGLevel),3);
    return true;
}
float ARPGPrototypeBoss::TakeDamage(float Amount,const FDamageEvent& Event,AController* EventInstigator,AActor* Causer)
{
    const auto* Player=ARPGPrototypeGameMode::NearestPlayer(GetWorld(),GetActorLocation());
    if(State==ERPGBossState::Defeated || !Player || bAsleep) { return 0; }
    const float Applied=Super::TakeDamage(Amount*(State==ERPGBossState::Recovery?1.5f:1.0f),Event,EventInstigator,Causer);
    if(IsEliminated() && !bRewarded)
    {
        bRewarded=true; bEngaged=false; ++Defeats; Enter(ERPGBossState::Defeated,0);
        if(auto* Mode=GetWorld()->GetAuthGameMode<ARPGPrototypeGameMode>()) { Mode->RewardEnemy(this,true); }
        ClearEncounterAttacks();
        // Retain the defeated encounter for an explicit retry instead of the shared CPU removal/respawn timer.
        GetWorldTimerManager().ClearTimer(EliminationCameraHoldTimer); GetWorldTimerManager().ClearTimer(RespawnTimer);
        const FString Beaten=GetBossName();
        const int32 DefeatedLevel=RPGLevel;
        RPGLevel=FMath::Min(MAX_int32-20,RPGLevel)+(Tier>0?5:static_cast<int32>(StrongLevels));
        // Everyone who fought sees it (the next one is its strong version).
        for(TActorIterator<ARPGPrototypeCharacter> It(GetWorld());It;++It)
        {
            if(It->IsGrunt() || It->IsEliminated() || FVector::DistSquared2D(It->GetActorLocation(),GetActorLocation())>FMath::Square(4000.0f)) { continue; }
            It->ShowNotice(FString::Printf(TEXT("%s Lv.%d 撃破！ 次は「%s」Lv.%d"),*Beaten,DefeatedLevel,KindDef(Kind).StrongName,RPGLevel),5);
        }
    }
    return Applied;
}
void ARPGPrototypeBoss::BeginCharge(ARPGPrototypeCharacter* Player)
{
    LockedDirection=(Player->GetActorLocation()-GetActorLocation()).GetSafeNormal2D();
    SetAIAimDirection(LockedDirection); bContactHit=false; ChargeVictims.Empty();
    Enter(ERPGBossState::ChargeTell,Tier>0?.7f:.9f); PreviousPosition=GetActorLocation();
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
    Ball->Launch(ShotDirection.RotateAngleAxis(Angle,FVector::UpVector),Tier>0?2000:1750,EChaosImpactBallFlightMode::Straight,0);
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
TArray<ARPGPrototypeCharacter*> ARPGPrototypeBoss::HeroesNear(float Range) const
{
    TArray<ARPGPrototypeCharacter*> Out;
    for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
    {
        auto* Hero=Cast<ARPGPrototypeCharacter>(It->Get()->GetPawn());
        if(Hero && !Hero->IsEliminated() && FVector::DistSquared2D(Hero->GetActorLocation(),GetActorLocation())<FMath::Square(Range)) { Out.Add(Hero); }
    }
    return Out;
}
void ARPGPrototypeBoss::BeginGeysers()
{
    // Under everyone in reach (where they are about to be), and a few more round it.
    GeyserSpots.Reset();
    const float Floor=GetActorLocation().Z-GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    for(const ARPGPrototypeCharacter* Hero:HeroesNear(2600))
    { const FVector At=Hero->GetActorLocation()+Hero->GetVelocity()*.35f; GeyserSpots.Add(FVector(At.X,At.Y,Floor)); }
    const int32 Extra=(Tier>0?4:2)+(IsEnraged()?1:0);
    for(int32 I=0;I<Extra && GeyserSpots.Num()<8;++I)
    {
        const FVector At=Home.GetLocation()+FRotator(0,FMath::FRandRange(0.0f,360.0f),0).Vector()*FMath::FRandRange(300.0f,1300.0f);
        GeyserSpots.Add(FVector(At.X,At.Y,Floor));
    }
    Enter(ERPGBossState::GeyserTell,Tier>0?.85f:1.05f);
}
void ARPGPrototypeBoss::EruptGeysers()
{
    const auto& D=KindDef(Kind);
    for(const FVector& Spot:GeyserSpots)
    {
        ARPGPrototypeVFX::PlayImpact(GetWorld(),D.Splash,Spot+FVector(0,0,40),1.6f,true);
        // Each hero where their own screen had them when it erupted there (ResolveOnOwnScreen).
        const TWeakObjectPtr<ARPGPrototypeBoss> Boss(this); const float Reach=GeyserRadius, Power=GetAttack(0).Power*1.15f;
        for(ARPGPrototypeCharacter* Hero:HeroesNear(4000))
        {
            Hero->ResolveOnOwnScreen([Boss,Spot,Reach,Power](ARPGPrototypeCharacter& Victim)
            {
                if(!Boss.IsValid() || Victim.IsEliminated() || FVector::DistSquared2D(Victim.GetActorLocation(),Spot)>FMath::Square(Reach)) { return; }
                UGameplayStatics::ApplyDamage(&Victim,Power,Boss->GetController(),Boss.Get(),nullptr);
                Victim.ApplyRPGKnockback((Victim.GetActorLocation()-Spot).GetSafeNormal2D(),260);
            });
        }
    }
}
void ARPGPrototypeBoss::Tick(float Dt)
{
    // Online: the host runs the boss; a guest's screen shows where it is (and its state, for the warnings).
    if(!HasAuthority()) { ARPGPrototypeCharacter::Tick(Dt); return; }
    auto* Player=ARPGPrototypeGameMode::NearestPlayer(GetWorld(),GetActorLocation());
    const double Now=GetWorld()->GetTimeSeconds();
    if(!Player && State!=ERPGBossState::Defeated && (IsBossActive() || Health<MaxHealth)) { ResetEncounter(); }
    // One short freeze is effective, followed by resistance; repeated fields cannot perma-lock the boss.
    if(IsBossArmoredMove()) { IceFrozenUntilServerTime=0; ClearRPGStatus(TEXT("Stun")); }
    else if(Now<FreezeResistanceUntil) { IceFrozenUntilServerTime=Now<FreezeAcceptedUntil?FMath::Min(IceFrozenUntilServerTime,FreezeAcceptedUntil):0; }
    else if(IsIceFrozen()) { FreezeAcceptedUntil=Now+.4; IceFrozenUntilServerTime=FMath::Min(IceFrozenUntilServerTime,FreezeAcceptedUntil); FreezeResistanceUntil=Now+3; }
    Super::Tick(Dt);
    // The legend: asleep until someone near has beaten every other boss (they say how many are left).
    if(bAsleep)
    {
        int32 Left=6;
        for(const ARPGPrototypeCharacter* Hero:HeroesNear(2600)) { Left=FMath::Min(Left,6-FMath::CountBits(static_cast<uint64>(Hero->GetBossesBeaten()&0x3F))); }
        StrongLeft=Left;
        if(Left>0) { return; }
        bAsleep=false;
        for(ARPGPrototypeCharacter* Hero:HeroesNear(4000)) { Hero->ShowNotice(TEXT("伝説の投手が目を覚ました！"),4); }
    }
    if(!Player || Player->IsEliminated() || IsEliminated() || IsTrainingMenuFrozen() || IsIceFrozen() || IsRPGStunned() || IsRPGKnockedBack()) { return; }
    const float Distance=FVector::Dist2D(Player->GetActorLocation(),GetActorLocation());
    if(IsBossActive() && (Distance>2300 || FVector::Dist2D(GetActorLocation(),Home.GetLocation())>1700)) { ResetEncounter(); return; }
    const FVector Toward=(Player->GetActorLocation()-GetActorLocation()).GetSafeNormal2D();
    const float Rest=Tier>0?.65f:1.0f;
    if(State==ERPGBossState::Idle)
    {
        GetCharacterMovement()->StopMovementImmediately();
        if(Distance>(bEngaged?1650:ActivationRange) || !BossSight(this,Player) || Now<NextActionAt) { return; }
        if(!bEngaged)
        {
            // Stronger for more players: everyone near when it starts.
            const int32 Heroes=FMath::Max(1,HeroesNear(3500).Num());
            MaxHealth=BaseMaxHealth*(1+RPGTuning::BossHPPerExtraPlayer*(Heroes-1)); Health=MaxHealth;
        }
        bEngaged=true;
        SetAIAimDirection(Toward); LockedDirection=Toward;
        // Its own moves in turn (more of them once enraged or strong).
        const int32 Moves=KindDef(Kind).Moves&((IsEnraged()||Tier>0)?31:15);
        int32 Pick=1;
        for(int32 Try=0;Try<5;++Try) { Pick=1<<(MoveNumber++%5); if(Moves&Pick) { break; } }
        if(Pick==1) { Enter(ERPGBossState::VolleyTell,Tier>0?.7f:.85f); }
        else if(Pick==2) { ChargesLeft=(IsEnraged()?2:1)+(Tier>0?1:0); BeginCharge(Player); }
        else if(Pick==4) { Enter(ERPGBossState::LeapTell,Tier>0?.85f:1.0f); }
        else if(Pick==8) { BeginGeysers(); }
        else { Enter(ERPGBossState::VortexTell,Tier>0?.7f:.9f); }
    }
    else if(State==ERPGBossState::VolleyTell)
    {
        if(Now>=StateUntil)
        {
            LockedDirection=Toward.RotateAngleAxis(FMath::FRandRange(-GetAimError(),GetAimError()),FVector::UpVector);
            SetAIAimDirection(LockedDirection); BeginThrowInput(); Enter(ERPGBossState::Volley,.3f); ShotsLeft=(IsEnraged()?4:2)+(Tier>0?2:0); NextShotAt=0;
        }
    }
    else if(State==ERPGBossState::Volley)
    {
        if(IsChargingThrow() && Now>=StateUntil) { EndThrowInput(); NextShotAt=Now+.36; StateUntil=Now+1.35; }
        if(NextShotAt>0 && Now>=NextShotAt && ShotsLeft>0)
        { const float Angles[]={30,-15,15,-30,45,-45}; FireExtraShot(IsEnraged()||Tier>0?Angles[FMath::Clamp(ShotsLeft-1,0,5)]:0); --ShotsLeft; NextShotAt=Now+.22; }
        if(!IsChargingThrow() && Now>=StateUntil) { Enter(ERPGBossState::Idle,0); NextActionAt=Now+.55*Rest; }
    }
    else if(State==ERPGBossState::ChargeTell)
    { if(Now>=StateUntil) { Enter(ERPGBossState::Charge,.8f); PreviousPosition=GetActorLocation(); } }
    else if(State==ERPGBossState::Charge)
    {
        auto* Movement=GetCharacterMovement(); Movement->MaxWalkSpeed=Tier>0?1800:1500; Movement->MaxAcceleration=18000;
        AddMovementInput(LockedDirection);
        for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
        {
            auto* Victim=Cast<ARPGPrototypeCharacter>(It->Get()->GetPawn()); if(!Victim || Victim->IsEliminated() || ChargeVictims.Contains(Victim)) { continue; }
            // This stretch of the charge, checked where the hero's own screen had them as it passed there.
            const TWeakObjectPtr<ARPGPrototypeBoss> Boss(this); const FVector From=PreviousPosition, To=GetActorLocation(); const float Power=GetAttack(0).Power*1.25f;
            Victim->ResolveOnOwnScreen([Boss,From,To,Power](ARPGPrototypeCharacter& Hero)
            {
                ARPGPrototypeBoss* B=Boss.Get(); if(!B || Hero.IsEliminated() || B->ChargeVictims.Contains(&Hero)) { return; }
                const FVector Closest=FMath::ClosestPointOnSegment(Hero.GetActorLocation(),From,To);
                if(FMath::Abs(Hero.GetActorLocation().Z-To.Z)<150 && FVector::DistSquared2D(Closest,Hero.GetActorLocation())<FMath::Square(115.0f) && BossSight(B,&Hero))
                { B->ChargeVictims.Add(&Hero); UGameplayStatics::ApplyDamage(&Hero,Power,B->GetController(),B,nullptr); }
            });
        }
        PreviousPosition=GetActorLocation();
        // Walls simply block movement. No crash damage, rebound, or wall-dependent down state.
        if(Now>=StateUntil)
        { Movement->MaxAcceleration=DefaultMaxAcceleration; if(--ChargesLeft>0) { BeginCharge(Player); } else { Enter(ERPGBossState::Recovery,Tier>0?1.7f:2.2f); } }
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
        LastWaveRadius=WaveRadius; WaveRadius=(Tier>0?1250:1000)*(1-FMath::Clamp(static_cast<float>((StateUntil-Now)/1.2),0.0f,1.0f));
        const float FloorZ=GetActorLocation().Z-GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
        {
            auto* Victim=Cast<ARPGPrototypeCharacter>(It->Get()->GetPawn()); if(!Victim || Victim->IsEliminated() || WaveVictims.Contains(Victim)) { continue; }
            // This ring of the wave, checked where the hero's own screen had them as it went by.
            const TWeakObjectPtr<ARPGPrototypeBoss> Boss(this); const FVector Center=GetActorLocation(); const float Inner=LastWaveRadius, Outer=WaveRadius, Floor=FloorZ, Power=GetAttack(0).Power;
            Victim->ResolveOnOwnScreen([Boss,Center,Inner,Outer,Floor,Power](ARPGPrototypeCharacter& Hero)
            {
                ARPGPrototypeBoss* B=Boss.Get(); if(!B || Hero.IsEliminated() || B->WaveVictims.Contains(&Hero)) { return; }
                const float Range=FVector::Dist2D(Hero.GetActorLocation(),Center);
                const float Feet=Hero.GetActorLocation().Z-Hero.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
                if(Range>=Inner-65 && Range<=Outer+65 && Feet<Floor+65 && BossSight(B,&Hero))
                { B->WaveVictims.Add(&Hero); UGameplayStatics::ApplyDamage(&Hero,Power,B->GetController(),B,nullptr); }
            });
        }
        if(Now>=StateUntil) { WaveRadius=0; Enter(ERPGBossState::Idle,0); NextActionAt=Now+.65*Rest; }
    }
    else if(State==ERPGBossState::GeyserTell)
    { if(Now>=StateUntil) { EruptGeysers(); Enter(ERPGBossState::Geyser,.6f); } }
    else if(State==ERPGBossState::Geyser)
    { if(Now>=StateUntil) { GeyserSpots.Reset(); Enter(ERPGBossState::Idle,0); NextActionAt=Now+.6*Rest; } }
    else if(State==ERPGBossState::VortexTell)
    { if(Now>=StateUntil) { Enter(ERPGBossState::Vortex,Tier>0?2.6f:2.2f); NextPullAt=0; } }
    else if(State==ERPGBossState::Vortex)
    {
        // Everyone near is dragged in; at the end the middle bursts.
        if(Now>=NextPullAt)
        {
            NextPullAt=Now+.15;
            for(ARPGPrototypeCharacter* Hero:HeroesNear(VortexRadius))
            {
                const FVector In=(GetActorLocation()-Hero->GetActorLocation()).GetSafeNormal2D();
                if(FVector::Dist2D(Hero->GetActorLocation(),GetActorLocation())>VortexCore*.6f && !Hero->IsDashing()) { Hero->ApplyRPGKnockback(In,Tier>0?70:55); }
            }
            if(FMath::FRand()<.5f) { ARPGPrototypeVFX::PlayImpact(GetWorld(),KindDef(Kind).Splash,GetActorLocation()+FRotator(0,FMath::FRandRange(0.0f,360.0f),0).Vector()*FMath::FRandRange(300.0f,VortexRadius*.8f),.8f,false); }
        }
        if(Now>=StateUntil)
        {
            ARPGPrototypeVFX::PlayImpact(GetWorld(),KindDef(Kind).Splash,GetActorLocation(),2.6f,true);
            // Checked where each hero's own screen had them when it burst (asked of everyone a little further out).
            const TWeakObjectPtr<ARPGPrototypeBoss> Boss(this); const FVector Center=GetActorLocation(); const float Reach=VortexCore, Power=GetAttack(0).Power*1.5f;
            for(ARPGPrototypeCharacter* Hero:HeroesNear(VortexCore*1.5f))
            {
                Hero->ResolveOnOwnScreen([Boss,Center,Reach,Power](ARPGPrototypeCharacter& Victim)
                {
                    if(!Boss.IsValid() || Victim.IsEliminated() || FVector::DistSquared2D(Victim.GetActorLocation(),Center)>FMath::Square(Reach)) { return; }
                    UGameplayStatics::ApplyDamage(&Victim,Power,Boss->GetController(),Boss.Get(),nullptr);
                    Victim.ApplyRPGKnockback((Victim.GetActorLocation()-Center).GetSafeNormal2D(),420);
                });
            }
            Enter(ERPGBossState::Idle,0); NextActionAt=Now+.7*Rest;
        }
    }
}
