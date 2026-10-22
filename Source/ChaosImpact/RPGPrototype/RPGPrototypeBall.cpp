#include "RPGPrototype.h"
#include "RPGPrototypeVFX.h"
#include "Components/SphereComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

namespace
{
bool VisibleTarget(AActor* From,ARPGPrototypeCharacter* To,const FVector& At)
{
    if(!From || !To || To->IsEliminated()) { return false; }
    FHitResult H; FCollisionQueryParams Q(SCENE_QUERY_STAT(RPGShotSight),false,From); Q.AddIgnoredActor(To);
    if(From->GetInstigator()) { Q.AddIgnoredActor(From->GetInstigator()); }
    return !From->GetWorld()->LineTraceSingleByObjectType(H,At,To->GetActorLocation(),FCollisionObjectQueryParams(ECC_WorldStatic),Q);
}
}
ARPGPrototypeBall::ARPGPrototypeBall()
{
    InitialLifeSpan=15; ProjectileMovement->bTickBeforeOwner=false;
}
void ARPGPrototypeBall::BeginPlay()
{
    if(BallType==EChaosImpactBallType::Snow) { if(auto* P=Cast<ARPGPrototypeCharacter>(GetInstigator())) { SetSnowScale(P->GetRPGSnowScale()); } }
    Super::BeginPlay();
    // Retain the original visuals, not the VS balance, global hazards, or instant-freeze rules.
    CollisionSphere->OnComponentHit.RemoveAll(this);
    CollisionSphere->OnComponentBeginOverlap.RemoveAll(this);
    ProjectileMovement->OnProjectileBounce.RemoveAll(this);
    CollisionSphere->OnComponentHit.AddDynamic(this,&ARPGPrototypeBall::RPGImpact);
    ProjectileMovement->OnProjectileBounce.AddDynamic(this,&ARPGPrototypeBall::RPGBounce);
}
void ARPGPrototypeBall::Launch(const FVector& Direction,float Speed,EChaosImpactBallFlightMode Mode,float UpSpeed)
{
    if(bLaunched) { return; }
    auto* Source=Cast<ARPGPrototypeCharacter>(GetInstigator()); if(!Source) { Destroy(); return; }
    if(!bSecondary)
    {
        ShotItem=Source->GetAttack(Source->GetSelectedAttack());
        Damage=Source->ConsumeAttack();
        float MinSpeed,MaxSpeed,U; EChaosImpactBallFlightMode M; bool Over;
        Source->GetThrowFlight(0,ShotItem.Type,1,MinSpeed,U,M,Over);
        Source->GetThrowFlight(1,ShotItem.Type,1,MaxSpeed,U,M,Over);
        ShotCharge=FMath::Clamp((Speed-MinSpeed)/FMath::Max(1.0f,MaxSpeed-MinSpeed),0.0f,1.0f);
        Damage*=1+(ShotCharge>.95f?ShotItem.Mod(TEXT("FullCharge")):ShotCharge<.25f?ShotItem.Mod(TEXT("QuickShot")):0)*.01f;
        Damage*=1+(ShotItem.Mod(TEXT("FireFocus"))+ShotItem.Mod(TEXT("IceFocus"))+ShotItem.Mod(TEXT("BeamFocus")))*.01f;
        if(ShotItem.Type==EChaosImpactBallType::Snow)
        {
            const float Max=FMath::Min(2.3f,1.2f*(1+ShotItem.Mod(TEXT("SnowMax"))*.01f));
            const float G=FMath::Clamp((Source->GetRPGSnowScale()-1)/FMath::Max(.1f,Max-1),0.0f,1.0f);
            SetSnowScale(FMath::Lerp(1.0f,Max,G)); Damage*=1+G*ShotItem.Mod(TEXT("SnowPower"))*.01f;
            Source->ApplyRPGStatus(TEXT("SnowCarry"),ShotItem.Mod(TEXT("SnowCarry")),20);
        }
    }
    else { Damage=ShotItem.EffectivePower()*DamageMultiplier; }
    if(!ShotItem.IsValid() || ShotItem.Type==EChaosImpactBallType::Nova) { Destroy(); return; }
    bAttackConsumed=bLaunched=true;
    const auto Type=ShotItem.Type;
    // Launch ordinary movement first, so VS wind/beam/special detonation code never runs.
    BallType=EChaosImpactBallType::Normal;
    const bool Straight=Type==EChaosImpactBallType::Thunder || Type==EChaosImpactBallType::Beam || Type==EChaosImpactBallType::Drive;
    const float BaseSpeed=Type==EChaosImpactBallType::Thunder?2100:Type==EChaosImpactBallType::Beam?3200:Type==EChaosImpactBallType::Drive?1000:Speed;
    Super::Launch(Direction,BaseSpeed*(1+ShotItem.Mod(TEXT("Speed"))*.01f),Straight?EChaosImpactBallFlightMode::Straight:Mode,UpSpeed);
    BallType=Type; ApplyBallTypePresentation();
    ProjectileMovement->MaxSpeed=0;
    CollisionSphere->SetCollisionResponseToChannel(ECC_Pawn,ECR_Ignore);
    LaunchedAt=GetWorld()->GetTimeSeconds(); LastPosition=LaunchPosition=GetActorLocation(); OriginalDirection=Direction.GetSafeNormal2D();
    MaxDistance=(Type==EChaosImpactBallType::Beam?2300:Type==EChaosImpactBallType::Thunder?2600:1700)
        *(1+(ShotItem.Mod(TEXT("Range"))+ShotItem.Mod(TEXT("BeamRange")))*.01f);
    Pierces=FMath::FloorToInt(ShotItem.Mod(TEXT("NormalPierce"))+ShotItem.Mod(TEXT("BeamPierce"))+ShotItem.Mod(TEXT("DrivePierce")));
    if(Type==EChaosImpactBallType::Beam) { ++Pierces; } // Even an untuned beam passes through one enemy.
    if(Type==EChaosImpactBallType::Normal) { Pierces+=FMath::FloorToInt(Source->Status(TEXT("CrowdPierce"))); Source->ClearRPGStatus(TEXT("CrowdPierce")); }
    if(ShotItem.Has(TEXT("SPush"))) { Pierces+=3; }
    if(!bSecondary && !bCosmeticPrediction)
    {
        float Twin=ShotItem.Mod(TEXT("TTwin"))+ShotItem.Mod(TEXT("LTwin"));
        if(Twin>0) { SpawnSecondary(Type,LaunchPosition,Direction,Twin*.01f,.18f); Damage*=Twin*.01f; }
        if(ShotItem.Has(TEXT("LTriple")))
        { SpawnSecondary(Type,LaunchPosition,Direction.RotateAngleAxis(-16,FVector::UpVector),ShotItem.Mod(TEXT("LTriple"))*.01f); SpawnSecondary(Type,LaunchPosition,Direction.RotateAngleAxis(16,FVector::UpVector),ShotItem.Mod(TEXT("LTriple"))*.01f); Damage*=ShotItem.Mod(TEXT("LTriple"))*.01f; }
        if(ShotItem.Has(TEXT("NormalRepeat"))) { SpawnSecondary(Type,LaunchPosition,Direction,ShotItem.Mod(TEXT("NormalRepeat"))*.01f,.2f); }
        if(ShotItem.Has(TEXT("NDouble")) && Source->Status(TEXT("NormalDouble"))>0) { SpawnSecondary(Type,LaunchPosition,Direction,ShotItem.Mod(TEXT("NDouble"))*.01f,.15f); Source->ClearRPGStatus(TEXT("NormalDouble")); }
        if(ShotItem.Has(TEXT("STrain"))) { for(int32 I=0;I<FMath::FloorToInt(ShotItem.Mod(TEXT("STrain")));++I) { SpawnSecondary(Type,LaunchPosition,Direction,.3f,.15f*(I+1)); } }
        if(ShotItem.Has(TEXT("DFollow"))) { SpawnSecondary(Type,LaunchPosition,Direction,ShotItem.Mod(TEXT("DFollow"))*.01f,.25f); }
    }
    if(Type==EChaosImpactBallType::Wind && !bCosmeticPrediction)
    { Burst(LaunchPosition); }
    SetLifeSpan(8);
}
void ARPGPrototypeBall::SpawnSecondary(EChaosImpactBallType Type,FVector At,FVector Direction,float PowerScale,float Delay)
{
    if(bSecondary) { return; }
    FRPGPrototypeItem Item=ShotItem; Item.Type=Type;
    const TWeakObjectPtr<ARPGPrototypeCharacter> Source=Cast<ARPGPrototypeCharacter>(GetInstigator());
    const float Scaled=PowerScale*Damage/FMath::Max(.01f,ShotItem.EffectivePower());
    auto Spawn=[Source,Item,At,Direction,Scaled]()
    {
        if(!Source.IsValid() || Source->IsEliminated()) { return; }
        auto* World=Source->GetWorld(); const FTransform Where(Direction.Rotation(),At);
        auto* B=World->SpawnActorDeferred<ARPGPrototypeBall>(ARPGPrototypeBall::StaticClass(),Where,Source.Get(),Source.Get(),ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!B) { return; }
        B->bSecondary=true; B->ShotItem=Item; B->DamageMultiplier=Scaled; B->SetBallType(Item.Type); B->FinishSpawning(Where);
        B->Launch(Direction,1400,EChaosImpactBallFlightMode::Straight,0);
    };
    if(Delay<=0) { Spawn(); }
    else { FTimerHandle Timer; GetWorldTimerManager().SetTimer(Timer,FTimerDelegate::CreateLambda(Spawn),Delay,false); }
}
void ARPGPrototypeBall::FinishRPGDrive()
{
    if(!bLaunched || bFinished || ShotItem.Type!=EChaosImpactBallType::Drive) { return; }
    if(ShotItem.Has(TEXT("DReturn")) && !bReturning)
    {
        bDetonated=false; bReturning=true; DriveDeadline=0; Struck.Empty();
        SetActorHiddenInGame(false); SetActorTickEnabled(true);
        CollisionSphere->SetSimulatePhysics(false); CollisionSphere->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        ProjectileMovement->Activate(true); SetLifeSpan(FMath::Max(.1f,8-(GetWorld()->GetTimeSeconds()-LaunchedAt)));
    }
    else { Burst(GetActorLocation()); }
}
float ARPGPrototypeBall::DealRPGDamage(ARPGPrototypeCharacter* Target,float Multiplier,bool Proc)
{
    auto* Source=Cast<ARPGPrototypeCharacter>(GetInstigator());
    if(!Source || !Target || Source==Target || Source->IsGrunt()==Target->IsGrunt() || Target->IsEliminated()) { return 0; }
    float Amount=Damage*Multiplier;
    if(bReturning) { Amount*=FMath::Max(ShotItem.Mod(TEXT("NReturn")),FMath::Max(ShotItem.Mod(TEXT("TReturn")),ShotItem.Mod(TEXT("DReturn"))))*.01f; }
    if(bTurned) { Amount*=1+ShotItem.Mod(TEXT("DTrick"))*.01f; }
    if(ShotItem.Has(TEXT("ThunderSolo")))
    {
        bool Alone=true;
        for(TActorIterator<ARPGPrototypeCharacter> It(GetWorld());It;++It)
        { if(*It!=Target && It->IsGrunt()==Target->IsGrunt() && !It->IsEliminated() && FVector::DistSquared2D(It->GetActorLocation(),Target->GetActorLocation())<FMath::Square(400.0f)) { Alone=false; break; } }
        if(Alone) { Amount*=1+ShotItem.Mod(TEXT("ThunderSolo"))*.01f; }
    }
    bProcDamage=Proc;
    const float Applied=UGameplayStatics::ApplyDamage(Target,Source->OutgoingDamage(ShotItem,Target,Amount,Bounces>0),Source->GetController(),this,nullptr);
    bProcDamage=false;
    if(Applied<=0) { return 0; }
    MulticastContactBurst(Target->GetActorLocation(),FRotator::ZeroRotator);
    ARPGPrototypeVFX::PlayImpact(GetWorld(),ShotItem.Type,Target->GetActorLocation(),.45f,false);
    Source->RegisterRPGHit(Target,ShotItem,Applied,Proc);
    const auto* BossTarget=Cast<ARPGPrototypeBoss>(Target);
    if(ShotItem.Type==EChaosImpactBallType::Snow && !Target->IsEliminated() && !Target->IsDashing() && !(BossTarget && BossTarget->IsBossArmoredMove()))
    { Target->AddActorWorldOffset(OriginalDirection*(50+ShotItem.Mod(TEXT("SnowPush"))+ShotItem.Mod(TEXT("SPush"))),true);
      Target->ApplyRPGStatus(TEXT("Slow"),10,1,Source); }
    if(Proc || bSecondary) { return Applied; }
    float Push=ShotItem.Mod(TEXT("Knockback"));
    if(ShotCharge>.95f) { Push+=ShotItem.Mod(TEXT("ChargeKnockback")); }
    if(ShotItem.Type==EChaosImpactBallType::Fire || ShotItem.Type==EChaosImpactBallType::Drive)
    { Push+=ShotItem.Mod(TEXT("BlastKnockback")); }
    if(Push>0)
    {
        FVector Incoming=ProjectileMovement->Velocity.GetSafeNormal2D();
        if(Incoming.IsNearlyZero()) { Incoming=(Target->GetActorLocation()-Source->GetActorLocation()).GetSafeNormal2D(); }
        Target->ApplyRPGKnockback(Incoming,Push);
    }
    if(ShotItem.Type==EChaosImpactBallType::Fire)
    {
        ARPGPrototypeEffect::Create(Source,ShotItem,TEXT("Burn"),Target->GetActorLocation(),50,3,FMath::Max(.08f,ShotItem.Mod(TEXT("Burn"))*.01f),Target);
        if(ShotItem.Has(TEXT("FHeat")) && Target->Status(TEXT("Ignition"))>=3) { ARPGPrototypeEffect::Create(Source,ShotItem,TEXT("Blast"),Target->GetActorLocation(),170,.15f,ShotItem.Mod(TEXT("FHeat"))*.01f); Target->ClearRPGStatus(TEXT("Ignition")); }
        if(ShotItem.Has(TEXT("FireSpread"))) { ARPGPrototypeEffect::Create(Source,ShotItem,TEXT("Spread"),Target->GetActorLocation(),300,.12f,ShotItem.Mod(TEXT("FireSpread"))*.01f,Target); }
    }
    if(ShotItem.Type==EChaosImpactBallType::Ice)
    {
        Target->ApplyRPGStatus(TEXT("Slow"),20+ShotItem.Mod(TEXT("IceSlow")),1.5f,Source);
        {
            const float Cold=Target->Status(TEXT("Cold"))+1; Target->ApplyRPGStatus(TEXT("Cold"),Cold,5,Source);
            if(Cold>=3 && Target->Status(TEXT("FreezeLock"))<=0 && !Target->IsCrowdControlImmune())
            {
                Target->ApplyIceFreeze(FMath::Clamp(ShotItem.Mod(TEXT("Freeze")),.35f,1.3f)); Target->ApplyRPGStatus(TEXT("FreezeLock"),1,3,Source);
                if(ShotItem.Has(TEXT("IPrison"))) { ARPGPrototypeEffect::Create(Source,ShotItem,TEXT("Cold"),Target->GetActorLocation(),230,1,0); }
                if(ShotItem.Has(TEXT("ICycle"))) { Source->ReduceSpecialCooldowns(ShotItem.Mod(TEXT("ICycle"))); }
            }
        }
        if(Target->IsIceFrozen() && ShotItem.Has(TEXT("IShards"))) { SpawnSecondary(EChaosImpactBallType::Ice,Target->GetActorLocation(),-OriginalDirection,ShotItem.Mod(TEXT("IShards"))*.01f); }
    }
    const float Root=FMath::Max(ShotItem.Type==EChaosImpactBallType::Thunder?.15f:0.0f,ShotItem.Mod(TEXT("ThunderStun")))+ShotItem.Mod(TEXT("LRoot"));
    if(Root>0) { Target->ApplyRPGStun(FMath::Min(.5f,Root)); }
    Target->ApplyRPGStatus(TEXT("NormalMark"),ShotItem.Mod(TEXT("ThunderMark"))+ShotItem.Mod(TEXT("BeamMark")),3,Source);
    if(ShotItem.Has(TEXT("ThunderEcho")) || ShotItem.Has(TEXT("TRod")))
    { ARPGPrototypeEffect::Create(Source,ShotItem,TEXT("Delayed"),Target->GetActorLocation(),90,ShotItem.Has(TEXT("TRod"))?.7f:.45f,(ShotItem.Mod(TEXT("ThunderEcho"))+ShotItem.Mod(TEXT("TRod")))*.01f,Target); }
    if(ShotItem.Type==EChaosImpactBallType::Thunder || ShotItem.Has(TEXT("Chain")))
    {
        int32 Count=0; const float Range=360*(1+ShotItem.Mod(TEXT("ChainRange"))*.01f);
        const int32 ChainCount=(ShotItem.Type==EChaosImpactBallType::Thunder?1:0)+FMath::FloorToInt(ShotItem.Mod(TEXT("Chain")));
        for(TActorIterator<ARPGPrototypeCharacter> It(GetWorld());It && Count<ChainCount;++It)
        {
            if(*It==Target || It->IsGrunt()!=Target->IsGrunt() || It->IsEliminated() || !VisibleTarget(this,*It,Target->GetActorLocation())
                || FVector::DistSquared2D(It->GetActorLocation(),Target->GetActorLocation())>FMath::Square(Range)) { continue; }
            DealRPGDamage(*It,.45f,true);
            ARPGPrototypeVFX::PlayLightning(GetWorld(),Target->GetActorLocation(),It->GetActorLocation());
            ARPGPrototypeEffect::Create(Source,ShotItem,TEXT("Spark"),It->GetActorLocation(),60,.2f,0); ++Count;
        }
        if(Count>=2 && ShotItem.Has(TEXT("TRefund"))) { Source->ReduceSpecialCooldowns(ShotItem.EffectiveCooldown()*ShotItem.Mod(TEXT("TRefund"))*.01f); }
    }
    return Applied;
}
void ARPGPrototypeBall::Tick(float Dt)
{
    // Intentionally do not call AChaosImpactBall::Tick: VS field generation stays completely isolated.
    AActor::Tick(Dt); UpdateBallTypePresentation(Dt); UpdateSoundPresentation(Dt);
    if(!bLaunched || bFinished) { return; }
    if(bCosmeticPrediction || !HasAuthority()) { return; }
    auto* Source=Cast<ARPGPrototypeCharacter>(GetInstigator()); if(!Source) { Destroy(); return; }
    const float Now=GetWorld()->GetTimeSeconds(), Age=Now-LaunchedAt;
    const FVector At=GetActorLocation(); DistanceFlown+=FVector::Dist(LastPosition,At);
    const float Radius=GetHitRadius()*(1+(ShotItem.Mod(TEXT("BeamWidth"))+ShotItem.Mod(TEXT("NormalWide")))*.01f)*(ShotItem.Has(TEXT("BeamFocus"))?.8f:1);
    ARPGPrototypeCharacter* Nearest=nullptr; float Best=600*600;
    for(TActorIterator<ARPGPrototypeCharacter> It(GetWorld());It;++It)
    {
        auto* Target=*It;
        if(Target==Source || Target->IsGrunt()==Source->IsGrunt() || Target->IsEliminated() || Target->IsDashing()) { continue; }
        const FVector Body=Target->GetActorLocation(); const FVector Close=FMath::ClosestPointOnSegment(Body,LastPosition,At);
        if(!Struck.Contains(Target) && FMath::Abs(Close.Z-Body.Z)<100+Radius && FVector::DistSquared2D(Close,Body)<FMath::Square(Radius+FMath::Max(40.0f,Target->GetCapsuleComponent()->GetScaledCapsuleRadius())) && VisibleTarget(this,Target,Close))
        {
            const bool Hit=DealRPGDamage(Target,FMath::Pow(ShotItem.Type==EChaosImpactBallType::Beam?.8f+.2f*ShotItem.Mod(TEXT("BeamFalloff"))*.01f:1,Struck.Num()))>0;
            if(Hit)
            {
                Struck.Add(Target);
                if(ShotItem.Has(TEXT("LRefract")) && !bCurved)
                {
                    ARPGPrototypeCharacter* Next=nullptr; float BestDistance=FMath::Square(650.0f);
                    for(TActorIterator<ARPGPrototypeCharacter> Other(GetWorld());Other;++Other)
                    {
                        const float D=FVector::DistSquared2D(At,Other->GetActorLocation());
                        if(*Other!=Target && Other->IsGrunt()!=Source->IsGrunt() && !Other->IsEliminated() && !Struck.Contains(*Other)
                            && D<BestDistance && VisibleTarget(this,*Other,At)) { Next=*Other; BestDistance=D; }
                    }
                    if(Next) { bCurved=true; ProjectileMovement->Velocity=FMath::Lerp(ProjectileMovement->Velocity.GetSafeNormal2D(),(Next->GetActorLocation()-At).GetSafeNormal2D(),ShotItem.Mod(TEXT("LRefract"))*.01f).GetSafeNormal()*ProjectileMovement->Velocity.Size(); ++Pierces; }
                }
                if(Struck.Num()>Pierces) { Burst(At,Target); return; }
            }
        }
        const float Dist=FVector::DistSquared2D(At,Body);
        if(Dist<Best && !Struck.Contains(Target) && VisibleTarget(this,Target,At) && FVector::DotProduct((Body-At).GetSafeNormal2D(),ProjectileMovement->Velocity.GetSafeNormal2D())>.35f)
        { Nearest=Target; Best=Dist; }
        if(ShotItem.Type==EChaosImpactBallType::Drive && Dist<FMath::Square(160.0f)) { Target->ApplyRPGStatus(TEXT("Slow"),ShotItem.Mod(TEXT("DriveGraze")),.5f,Source); }
    }
    const float Home=ShotItem.Mod(TEXT("NormalHoming"))+(Bounces>0?ShotItem.Mod(TEXT("NBank"))+ShotItem.Mod(TEXT("TBank")):0)+(Nearest && Nearest->Status(TEXT("BlackNormal"))>0?Nearest->Status(TEXT("Home")):0);
    if(Nearest && Home>0)
    { const float V=ProjectileMovement->Velocity.Size(); ProjectileMovement->Velocity=FMath::VInterpTo(ProjectileMovement->Velocity.GetSafeNormal(),(Nearest->GetActorLocation()-At).GetSafeNormal2D(),Dt,Home*.08f).GetSafeNormal()*V; }
    if(ShotItem.Type==EChaosImpactBallType::Drive && !bSecondary)
    {
        const float Time=FMath::Min(5.0f,2+ShotItem.Mod(TEXT("DriveTime")));
        if(DriveDeadline>0) { DriveDeadline=LaunchedAt+Time; }
        FVector Heading=ProjectileMovement->Velocity.GetSafeNormal2D();
        if(!DriveSteer.IsNearlyZero())
        {
            const float Angle=FVector::DotProduct(Heading,DriveSteer.GetSafeNormal2D());
            if(FVector::DotProduct(OriginalDirection,DriveSteer.GetSafeNormal2D())<.25f) { bTurned=true; }
            const float Turn=3.5f*(1+(ShotItem.Mod(TEXT("DriveTurn"))+(Bounces>0?ShotItem.Mod(TEXT("DriveWall")):0))*.01f)*(ShotItem.Has(TEXT("DriveSpeed"))?.85f:1);
            Heading=FMath::VInterpTo(Heading,DriveSteer.GetSafeNormal2D(),Dt,Turn).GetSafeNormal2D();
            const float Rush=Angle>.98f?ShotItem.Mod(TEXT("DRush"))*FMath::Clamp(Age/2,0.0f,1.0f):0;
            ProjectileMovement->Velocity=Heading*1000*(1+(ShotItem.Mod(TEXT("DriveSpeed"))+Rush)*.01f);
        }
        if(Age>=Time || (DriveDeadline<=0 && Age>.1f))
        {
            if(ShotItem.Has(TEXT("DReturn")) && !bReturning) { bReturning=true; Struck.Empty(); }
            else if(!bReturning) { Burst(At); return; }
        }
    }
    if(Now>=NextTrailAt)
    {
        NextTrailAt=Now+.22f;
        if(ShotItem.Has(TEXT("FireTrail"))) { ARPGPrototypeEffect::Create(Source,ShotItem,TEXT("Fire"),At,70,ShotItem.Mod(TEXT("FireTrail")),.08f); }
        if(ShotItem.Has(TEXT("IPath"))) { ARPGPrototypeEffect::Create(Source,ShotItem,TEXT("Cold"),At,80,1.5f,0); }
        if(ShotItem.Has(TEXT("BeamStay"))) { ARPGPrototypeEffect::Create(Source,ShotItem,TEXT("Light"),At,65,ShotItem.Mod(TEXT("BeamStay")),.08f); }
    }
    if(ShotItem.Type==EChaosImpactBallType::Beam) { UpdateBeamShaft(Dt,true); }
    if(RollingUntil>0 && Now>=RollingUntil) { Burst(At); return; }
    if(RollingUntil<=0 && (DistanceFlown>=MaxDistance || Age>6))
    {
        if(!bReturning && (ShotItem.Has(TEXT("NReturn")) || ShotItem.Has(TEXT("TReturn")))) { bReturning=true; Struck.Empty(); }
        else if(!bReturning) { Burst(At); return; }
    }
    if(bReturning)
    {
        const FVector Back=Source->GetActorLocation()+FVector(0,0,50)-At;
        ProjectileMovement->ProjectileGravityScale=0; ProjectileMovement->Velocity=Back.GetSafeNormal()*1500;
        if(Back.Size()<80 || Age>7.5f) { Destroy(); return; }
    }
    LastPosition=At;
}
void ARPGPrototypeBall::RPGImpact(UPrimitiveComponent*,AActor* Other,UPrimitiveComponent*,FVector,const FHitResult& Hit)
{
    if(!bLaunched || bFinished) { return; }
    // Pawn hits are swept manually, so dashes and piercing share exactly one damage path.
}
void ARPGPrototypeBall::RPGBounce(const FHitResult& Hit,const FVector& Velocity)
{
    if(!bLaunched || bFinished || bCosmeticPrediction || !HasAuthority()) { return; }
    ++Bounces; bHasReflected=true; ReflectionCount=Bounces;
    if(Hit.ImpactNormal.Z>.6f && ShotItem.Type==EChaosImpactBallType::Snow && (ShotItem.Has(TEXT("SnowRoll")) || ShotItem.Has(TEXT("SPush"))))
    {
        if(RollingUntil<=0) { RollingUntil=GetWorld()->GetTimeSeconds()+FMath::Clamp(ShotItem.Mod(TEXT("SnowRoll")),1.5f,3.9f); }
        ProjectileMovement->Velocity=Velocity.GetSafeNormal2D()*900; ProjectileMovement->ProjectileGravityScale=.2f; return;
    }
    const bool Reflect=ShotItem.Type==EChaosImpactBallType::Normal || ShotItem.Type==EChaosImpactBallType::Thunder
        || (ShotItem.Type==EChaosImpactBallType::Beam && Bounces<=FMath::FloorToInt(ShotItem.Mod(TEXT("BeamBounce"))))
        || (ShotItem.Type==EChaosImpactBallType::Drive && ShotItem.Has(TEXT("DriveWall")));
    if(!Reflect || Hit.ImpactNormal.Z>.6f) { Burst(Hit.ImpactPoint); return; }
    if(ShotItem.Type==EChaosImpactBallType::Thunder) { ProjectileMovement->Velocity*=1+ShotItem.Mod(TEXT("ThunderBounce"))*.01f; }
    if(ShotItem.Type==EChaosImpactBallType::Normal && Bounces==1) { ProjectileMovement->Velocity*=1+ShotItem.Mod(TEXT("NormalBounce"))*.01f; }
    ARPGPrototypeVFX::PlayImpact(GetWorld(),ShotItem.Type,Hit.ImpactPoint,.35f,false);
}
void ARPGPrototypeBall::Burst(const FVector& At,ARPGPrototypeCharacter* Direct)
{
    if(bFinished) { return; }
    bFinished=true; ProjectileMovement->Deactivate(); CollisionSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    DriveDeadline=0; auto* Source=Cast<ARPGPrototypeCharacter>(GetInstigator());
    if(!Source) { Destroy(); return; }
    MulticastContactBurst(At,FRotator::ZeroRotator);
    const auto T=ShotItem.Type;
    // Fire/drive already create a Blast field below, which supplies their terminal burst once.
    if(T!=EChaosImpactBallType::Fire && T!=EChaosImpactBallType::Drive && T!=EChaosImpactBallType::Ice)
    { ARPGPrototypeVFX::PlayImpact(GetWorld(),T,At,T==EChaosImpactBallType::Snow?GetSnowScale():1); }
    const auto Field=[&](FName Kind,float Radius,float Seconds,float Power,ARPGPrototypeCharacter* Follow=nullptr)
    { return ARPGPrototypeEffect::Create(Source,ShotItem,Kind,At,Radius,Seconds,Power*Damage/FMath::Max(.01f,ShotItem.EffectivePower()),Follow); };
    if(T==EChaosImpactBallType::Fire)
    {
        const float R=110*(1+ShotItem.Mod(TEXT("FireArea"))*.01f)*(ShotItem.Has(TEXT("FireFocus"))?.8f:1);
        auto* F=Field(TEXT("Blast"),R,.16f,.25f); if(F && Direct) { F->Follow=Direct; } // exclude the direct victim from the initial blast
        if(!bSecondary && ShotItem.Has(TEXT("FireFloor"))) { Field(TEXT("Fire"),R,ShotItem.Mod(TEXT("FireFloor")),.12f); }
        if(!bSecondary && ShotItem.Has(TEXT("FPillar"))) { Field(TEXT("Pillar"),R,1.65f,ShotItem.Mod(TEXT("FPillar"))*.01f); }
        if(!bSecondary && ShotItem.Has(TEXT("FSpark")))
        { for(int32 I=0;I<FMath::FloorToInt(ShotItem.Mod(TEXT("FSpark")));++I) { SpawnSecondary(T,At+FVector(0,0,45),OriginalDirection.RotateAngleAxis(I*360/ShotItem.Mod(TEXT("FSpark")),FVector::UpVector),.3f); } }
    }
    if(T==EChaosImpactBallType::Ice)
    {
        Field(TEXT("Cold"),110*(1+ShotItem.Mod(TEXT("IceArea"))*.01f)*(ShotItem.Has(TEXT("IceFocus"))?.8f:1),FMath::Max(1+ShotItem.Mod(TEXT("IceTime")),ShotItem.Mod(TEXT("IFollow"))),0,ShotItem.Has(TEXT("IFollow"))?Direct:nullptr);
        if(!bSecondary) { const int32 Count=FMath::FloorToInt(ShotItem.Mod(TEXT("IceShard"))); for(int32 I=0;I<Count;++I) { SpawnSecondary(T,At+FVector(0,0,50),OriginalDirection.RotateAngleAxis(360.0f*I/FMath::Max(1,Count),FVector::UpVector),.25f); } }
    }
    if(T==EChaosImpactBallType::Thunder && ShotItem.Has(TEXT("ThunderField"))) { Field(TEXT("Electric"),130,ShotItem.Mod(TEXT("ThunderField")),.12f); }
    if(T==EChaosImpactBallType::Black)
    {
        auto* F=Field(TEXT("Gravity"),180*(1+ShotItem.Mod(TEXT("BlackArea"))*.01f),2+ShotItem.Mod(TEXT("BlackTime")),.14f);
        if(F) { F->Direction=OriginalDirection; }
        if(!bSecondary && ShotItem.Has(TEXT("BTwin")))
        {
            if(F) { F->PowerScale*=ShotItem.Mod(TEXT("BTwin"))*.01f; F->SetActorLocation(At+FVector(0,170,0)); }
            auto* Other=Field(TEXT("Gravity"),160,2+ShotItem.Mod(TEXT("BlackTime")),.14f*ShotItem.Mod(TEXT("BTwin"))*.01f);
            if(Other) { Other->SetActorLocation(At-FVector(0,170,0)); }
        }
    }
    if(T==EChaosImpactBallType::Wind)
    {
        const float R=140*(1+ShotItem.Mod(TEXT("WindArea"))*.01f);
        auto* F=Field(TEXT("Wind"),R,2*(1+ShotItem.Mod(TEXT("WindRange"))*.01f),.16f); if(F) { F->Direction=OriginalDirection; }
        if(!bSecondary && ShotItem.Has(TEXT("WTwin")))
        {
            const float P=ShotItem.Mod(TEXT("WTwin"))*.01f;
            if(F) { F->PowerScale*=P; F->Direction=OriginalDirection.RotateAngleAxis(-18,FVector::UpVector); }
            auto* Other=Field(TEXT("Wind"),R,2,.16f*P); if(Other) { Other->Direction=OriginalDirection.RotateAngleAxis(18,FVector::UpVector); Other->bSecondary=true; }
        }
        if(!bSecondary && ShotItem.Has(TEXT("WindSplit")))
        {
            const float P=ShotItem.Has(TEXT("WTwin"))?ShotItem.Mod(TEXT("WTwin"))*.01f:ShotItem.Mod(TEXT("WindSplit"))*.01f;
            for(float Angle:{-30.0f,30.0f}) { auto* Other=Field(TEXT("Wind"),R*.7f,2,.16f*P); if(Other) { Other->Direction=OriginalDirection.RotateAngleAxis(Angle,FVector::UpVector); Other->bSecondary=true; } }
        }
        if(ShotItem.Has(TEXT("WGuard"))) { auto* Guard=Field(TEXT("WindGuard"),180,ShotItem.Mod(TEXT("WGuard")),0,Source); }
        Source->ApplyRPGStatus(TEXT("MoveBuff"),ShotItem.Mod(TEXT("WindTail")),3);
    }
    if(T==EChaosImpactBallType::Smoke)
    {
        Field(TEXT("Smoke"),170*(1+ShotItem.Mod(TEXT("SmokeArea"))*.01f),2+ShotItem.Mod(TEXT("SmokeTime")),0,ShotItem.Has(TEXT("MFollow"))?Source:nullptr);
        if(ShotItem.Has(TEXT("MDecoy"))) { Source->DecoyLocation=At; Source->DecoyUntil=GetWorld()->GetTimeSeconds()+ShotItem.Mod(TEXT("MDecoy")); Field(TEXT("Decoy"),70,ShotItem.Mod(TEXT("MDecoy")),0); }
    }
    if(T==EChaosImpactBallType::Beam && ShotItem.Has(TEXT("LFinal"))) { Field(TEXT("Blast"),150,.15f,ShotItem.Mod(TEXT("LFinal"))*.01f); }
    if(T==EChaosImpactBallType::Snow)
    {
        if(ShotItem.Has(TEXT("SnowFloor")) || ShotItem.Has(TEXT("SGround"))) { Field(TEXT("Snow"),150,3,0); }
        if(!bSecondary)
        {
            const int32 Count=ShotItem.Has(TEXT("SSplit"))?2:FMath::FloorToInt(ShotItem.Mod(TEXT("SnowShard")));
            for(int32 I=0;I<Count;++I) { SpawnSecondary(T,At+FVector(0,0,50),OriginalDirection.RotateAngleAxis((I-(Count-1)*.5f)*35,FVector::UpVector),ShotItem.Has(TEXT("SSplit"))?ShotItem.Mod(TEXT("SSplit"))*.01f:.25f); }
        }
    }
    if(T==EChaosImpactBallType::Simae) { Field(TEXT("Birds"),500*(1+ShotItem.Mod(TEXT("BirdRange"))*.01f),4+ShotItem.Mod(TEXT("BirdTime")),1); }
    if(T==EChaosImpactBallType::Drive)
    {
        Field(TEXT("Blast"),ShotItem.Has(TEXT("DriveEnd"))?180:110,.15f,FMath::Max(.2f,ShotItem.Mod(TEXT("DriveEnd"))*.01f));
        if(ShotItem.Has(TEXT("DriveOrbit")) || ShotItem.Has(TEXT("DOrbit"))) { Field(TEXT("Orbit"),160,FMath::Max(ShotItem.Mod(TEXT("DriveOrbit")),ShotItem.Mod(TEXT("DOrbit"))),.16f); }
    }
    SetActorHiddenInGame(true); SetLifeSpan(.1f);
}
