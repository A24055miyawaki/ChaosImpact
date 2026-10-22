#include "RPGPrototype.h"
#include "Net/UnrealNetwork.h"
#include "RPGPrototypeVFX.h"
#include "ChaosImpactSfx.h"
#include "ChaosImpactSimaeBird.h"
#include "ChaosImpactPuppetComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Animation/AnimSequence.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
bool HasSight(AActor* Self,ARPGPrototypeCharacter* Target)
{
    FHitResult H; FCollisionQueryParams Q(SCENE_QUERY_STAT(RPGFieldSight),false,Self); Q.AddIgnoredActor(Target);
    if(Self->GetInstigator()) { Q.AddIgnoredActor(Self->GetInstigator()); }
    return !Self->GetWorld()->LineTraceSingleByObjectType(H,Self->GetActorLocation()+FVector(0,0,70),Target->GetActorLocation(),FCollisionObjectQueryParams(ECC_WorldStatic),Q);
}
float DamageEffect(ARPGPrototypeEffect* Field,ARPGPrototypeCharacter* Target,float Scale)
{
    if(!Field->Source.IsValid() || !Target || Target->IsEliminated() || Target->IsDashing()) { return 0; }
    auto* Source=Field->Source.Get();
    const float Damage=Source->OutgoingDamage(Field->Item,Target,Field->Item.EffectivePower()*Scale,false);
    return UGameplayStatics::ApplyDamage(Target,Damage,Source->GetController(),Field,nullptr);
}
UStaticMeshComponent* Part(AActor* Caster,USceneComponent* Parent,UStaticMesh* Mesh,UMaterialInterface* Material)
{
    auto* P=NewObject<UStaticMeshComponent>(Caster); P->SetStaticMesh(Mesh); P->SetMaterial(0,Material);
    P->SetCollisionEnabled(ECollisionEnabled::NoCollision); P->SetCastShadow(false); P->SetupAttachment(Parent); P->RegisterComponent(); return P;
}
}
ARPGPrototypeEffect::ARPGPrototypeEffect()
{
    PrimaryActorTick.bCanEverTick=true;
    bReplicates=true; SetReplicatingMovement(true);
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("EffectRoot")));
}
ARPGPrototypeEffect* ARPGPrototypeEffect::Create(ARPGPrototypeCharacter* Caster,const FRPGPrototypeItem& I,FName Effect,
    FVector At,float Radius,float Duration,float PowerScale,ARPGPrototypeCharacter* Follow)
{
    if(!IsValid(Caster) || !Caster->GetWorld()) { return nullptr; }
    int32 Count=0; for(TActorIterator<ARPGPrototypeEffect> It(Caster->GetWorld());It;++It) { if(++Count>=128) { return nullptr; } }
    FTransform Where(FRotator::ZeroRotator,At);
    auto* F=Caster->GetWorld()->SpawnActorDeferred<ARPGPrototypeEffect>(StaticClass(),Where,Caster,Caster,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(F)
    {
        F->Item=I; F->Effect=Effect; F->Source=Caster; F->Follow=Follow; F->Direction=Caster->GetAimDirection();
        F->Radius=FMath::Clamp(Radius,35.0f,900.0f); F->Duration=FMath::Clamp(Duration,.1f,8.0f); F->PowerScale=PowerScale;
        F->FinishSpawning(Where);
    }
    return F;
}
void ARPGPrototypeEffect::BeginPlay()
{
    Super::BeginPlay(); StartedAt=GetWorld()->GetTimeSeconds(); NextPulseAt=StartedAt+(Effect==TEXT("Burn")?1:0);
    SetLifeSpan(Duration+2.5f);
    using namespace ChaosImpactBallTypes;
    auto* Sphere=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    if(Effect==TEXT("Birds"))
    {
        int32 NumBirds=FMath::Clamp(2+FMath::FloorToInt(Item.Mod(TEXT("BirdCount")))-(Item.Has(TEXT("BirdPower"))?1:0),1,16);
        if(Item.Has(TEXT("AParent"))) { NumBirds=FMath::Max(1,NumBirds/2); }
        auto* BirdMat=MakeEmissive(this,FLinearColor::White,.4f);
        if(auto* Base=LoadObject<UMaterialInterface>(nullptr,SimaeAssets::TexturedMaterial))
        {
            auto* MID=UMaterialInstanceDynamic::Create(Base,this);
            if(auto* Texture=LoadObject<UTexture>(nullptr,SimaeAssets::BirdTexture)) { MID->SetTextureParameterValue(TEXT("BodyTexture"),Texture); }
            BirdMat=MID;
        }
        const TCHAR* Paths[]={SimaeAssets::BirdBody,SimaeAssets::BirdTail,SimaeAssets::BirdWing,SimaeAssets::BirdEye,SimaeAssets::BirdCrest};
        for(int32 I=0;I<NumBirds;++I)
        {
            for(const auto* Path:Paths)
            {
                auto* Mesh=LoadObject<UStaticMesh>(nullptr,Path);
                auto* P=Part(this,RootComponent,Mesh?Mesh:Sphere,BirdMat); P->SetRelativeScale3D(FVector(.65f)); Pieces.Add(P);
            }
            BirdTargets.Add(nullptr); BirdLocations.Add(GetActorLocation()+FVector(FMath::Cos(I*2.4f)*40,FMath::Sin(I*2.4f)*40,60));
            BirdReadyAt.Add(StartedAt+.22f+I*.03f); BirdPecks.Add(0);
        }
        AChaosImpactSimaeFeatherBurst::Play(GetWorld(),GetActorLocation(),1);
        ChaosImpactSfx::PlayAt(this,EChaosImpactSfx::SimaeFlock,GetActorLocation());
    }
    else
    {
        // Presentation has its own render-only lifetime, including a short fade after gameplay ends.
        ARPGPrototypeVFX::PlayField(this);
    }
}
void ARPGPrototypeEffect::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME_CONDITION(ARPGPrototypeEffect,Item,COND_InitialOnly);
    DOREPLIFETIME_CONDITION(ARPGPrototypeEffect,Effect,COND_InitialOnly);
    DOREPLIFETIME_CONDITION(ARPGPrototypeEffect,Direction,COND_InitialOnly);
    DOREPLIFETIME_CONDITION(ARPGPrototypeEffect,Radius,COND_InitialOnly);
    DOREPLIFETIME_CONDITION(ARPGPrototypeEffect,Duration,COND_InitialOnly);
}
void ARPGPrototypeEffect::Tick(float Dt)
{
    Super::Tick(Dt);
    // A guest's screen shows the host's effect as it is (it moves with the host's, and goes when it goes).
    if(!HasAuthority()) { return; }
    if(!Source.IsValid()) { Destroy(); return; }
    auto* Caster=Source.Get(); const float Now=GetWorld()->GetTimeSeconds(), Age=Now-StartedAt;
    const bool Expired=Age>=Duration;
    if(Follow.IsValid() && !Follow->IsEliminated() && (Effect!=TEXT("Smoke") || Age<Item.Mod(TEXT("MFollow")))
        && (Effect!=TEXT("Cold") || Age<Item.Mod(TEXT("IFollow")))) { SetActorLocation(Follow->GetActorLocation()); }
    if(Effect==TEXT("Gravity") && Item.Has(TEXT("BMove"))) { AddActorWorldOffset(Direction*Item.Mod(TEXT("BMove"))*Dt); }
    if(Effect==TEXT("Wind") && !Item.Has(TEXT("WExpand")))
    {
        const float Sign=Item.Has(TEXT("WReturn")) && Age>Duration*.5f?-1:1;
        AddActorWorldOffset(Direction*360*Sign*Dt);
    }
    const FVector At=GetActorLocation();
    if(Effect==TEXT("Birds"))
    {
        TArray<ARPGPrototypeCharacter*> Candidates;
        const FVector Center=Item.Has(TEXT("AGuard"))?Caster->GetActorLocation():At;
        const float Search=Radius*(1+Item.Mod(TEXT("BirdRetarget"))*.01f);
        for(TActorIterator<ARPGPrototypeCharacter> It(GetWorld());It;++It)
        { if(It->IsGrunt()!=Caster->IsGrunt() && !It->IsEliminated() && FVector::DistSquared2D(Center,It->GetActorLocation())<FMath::Square(Search) && HasSight(this,*It)) { Candidates.Add(*It); } }
        TMap<ARPGPrototypeCharacter*,int32> Loads;
        for(const auto& Target:BirdTargets) { if(Target.IsValid()) { Loads.FindOrAdd(Target.Get())++; } }
        const int32 MaxPecks=FMath::Clamp(1+FMath::FloorToInt(Item.Mod(TEXT("BirdPeck"))),1,4);
        bool AnyAlive=false;
        for(int32 I=0;I<BirdTargets.Num();++I)
        {
            auto* Target=BirdTargets[I].Get();
            if(Target && Target->IsEliminated())
            {
                if(Item.Has(TEXT("ATravel")) && !bExtended) { Duration+=Item.Mod(TEXT("ATravel")); bExtended=true; }
                const float Refund=FMath::Min(1-FeatherRecovered,Item.Mod(TEXT("AFeather")));
                Caster->ApplyRPGStatus(TEXT("BirdRefund"),Refund,.2f); FeatherRecovered+=Refund;
                Target=nullptr; BirdTargets[I].Reset();
            }
            if(Target && (Target->IsDashing() || !Candidates.Contains(Target)))
            { Loads.FindOrAdd(Target)=FMath::Max(0,Loads.FindRef(Target)-1); BirdTargets[I].Reset(); Target=nullptr; BirdReadyAt[I]=Now+.7f; }
            if(!Target && !Expired && BirdPecks[I]<MaxPecks && Now>=BirdReadyAt[I])
            {
                float Best=MAX_flt;
                for(auto* C:Candidates) { const float Score=Loads.FindRef(C)*1000000.0f+FVector::DistSquared2D(BirdLocations[I],C->GetActorLocation()); if(Score<Best) { Best=Score; Target=C; } }
                BirdTargets[I]=Target; if(Target) { Loads.FindOrAdd(Target)++; }
            }
            FVector Destination=At+FVector(FMath::Cos(Age*4+I)*80,FMath::Sin(Age*4+I)*80,90);
            const bool Depart=Expired || BirdPecks[I]>=MaxPecks;
            if(Depart) { Destination=BirdLocations[I]+FVector(FMath::Cos(I*2.4f)*80,FMath::Sin(I*2.4f)*80,350); }
            else if(Target) { Destination=Target->GetActorLocation()+FVector(FMath::Cos(Age*4+I)*35,FMath::Sin(Age*4+I)*35,95); }
            const float Speed=FMath::Min(740.0f,600*(1+Item.Mod(TEXT("BirdSpeed"))*.01f));
            const FVector Delta=Destination-BirdLocations[I]; BirdLocations[I]+=Delta.GetClampedToMaxSize(Speed*Dt);
            if(Target && !Depart && Now>=BirdReadyAt[I] && Delta.Size()<95 && !Target->IsDashing())
            {
                const float Bonus=1+(Item.Mod(TEXT("AParent"))+Item.Mod(TEXT("BirdPower"))+(Candidates.Num()>1?Item.Mod(TEXT("ASpread")):0)
                    +(Target->Status(TEXT("BirdMark"))>0?Item.Mod(TEXT("BirdMark")):0))*.01f;
                DamageEffect(this,Target,PowerScale*Bonus); ++BirdPecks[I]; BirdReadyAt[I]=Now+.8f;
                ChaosImpactSfx::PlayAt(this,EChaosImpactSfx::SimaePeck,Target->GetActorLocation(),.6f);
                AChaosImpactSimaeFeatherBurst::Play(GetWorld(),BirdLocations[I],.35f);
            }
            AnyAlive|=!Depart;
            const float Fade=Expired?FMath::Clamp(1-(Age-Duration)/.5f,0.0f,1.0f):1;
            for(int32 P=0;P<5;++P)
            {
                auto* Mesh=Pieces[I*5+P].Get(); Mesh->SetWorldLocation(BirdLocations[I]);
                Mesh->SetWorldRotation(FRotator(0,Delta.Rotation().Yaw,0));
                const float Flap=P==2?1+.35f*FMath::Sin(Age*28+I):1;
                Mesh->SetWorldScale3D(FVector(.65f,.65f,.65f*Flap)*Fade*(Item.Has(TEXT("AParent"))?1.25f:1));
            }
        }
        if((Expired && Age>Duration+.5f) || (!AnyAlive && Age>1.5f)) { Destroy(); }
        return;
    }
    const float CurrentRadius=Radius*(Effect==TEXT("Wind") && Item.Has(TEXT("WExpand"))?FMath::Lerp(1.0f,Item.Mod(TEXT("WExpand"))*.01f,FMath::Clamp(Age/Duration,0.0f,1.0f)):1);
    if(Effect==TEXT("Decoy")) { if(Expired) { Destroy(); } return; }
    const bool Delayed=Effect==TEXT("Delayed");
    const bool Pulse=Now>=NextPulseAt && (!Delayed || Expired);
    if(Pulse)
    {
        NextPulseAt=Now+(Effect==TEXT("Burn")?1:.5f); ++PulseCount;
        if(Effect==TEXT("Smoke") && FVector::DistSquared2D(Caster->GetActorLocation(),At)<FMath::Square(CurrentRadius))
        {
            Caster->ApplyRPGStatus(TEXT("Smoke"),1,.7f); Caster->ApplyRPGStatus(TEXT("Hide"),Item.Mod(TEXT("SmokeHide")),.7f);
            Caster->ApplyRPGStatus(TEXT("MoveBuff"),Item.Mod(TEXT("SmokeSpeed")),.7f);
            Caster->ApplyRPGStatus(TEXT("SmokeHeal"),Item.Mod(TEXT("SmokeHeal")),.7f);
            Caster->ApplyRPGStatus(TEXT("Shadow"),Item.Mod(TEXT("MFirst")),.7f);
            Caster->ApplyRPGStatus(TEXT("SmokeExit"),Item.Mod(TEXT("MExit")),3);
        }
        if((Effect==TEXT("Fire") || Effect==TEXT("Cold") || Effect==TEXT("Snow")) && FVector::DistSquared2D(Caster->GetActorLocation(),At)<FMath::Square(CurrentRadius))
        {
            Caster->ApplyRPGStatus(TEXT("MoveBuff"),Item.Mod(TEXT("FWalk"))+Item.Mod(TEXT("IPath")),.7f);
            Caster->ApplyRPGStatus(TEXT("SnowGrowth"),Item.Mod(TEXT("SGround")),.7f);
        }
        if(Effect==TEXT("Wind") && Item.Has(TEXT("WindLoot")))
        {
            for(TActorIterator<ARPGPrototypeLoot> It(GetWorld());It;++It)
            { if(FVector::DistSquared2D(It->GetActorLocation(),At)<FMath::Square(CurrentRadius+200)) { It->SetActorLocation(FMath::VInterpTo(It->GetActorLocation(),Caster->GetActorLocation(),.5f,Item.Mod(TEXT("WindLoot"))*.01f)); } }
        }
        for(TActorIterator<ARPGPrototypeCharacter> It(GetWorld());It;++It)
        {
            auto* T=*It;
            if(T==Caster || T->IsGrunt()==Caster->IsGrunt() || T->IsEliminated() || FMath::Abs(T->GetActorLocation().Z-At.Z)>220 || !HasSight(this,T)) { continue; }
            const float Distance=FVector::Dist2D(T->GetActorLocation(),At);
            if((Effect==TEXT("Burn") || Delayed) ? T!=Follow.Get() : Distance>CurrentRadius) { continue; }
            const bool Once=Effect==TEXT("Blast") || Effect==TEXT("Spread") || Delayed;
            if((Once && OnceTargets.Contains(T)) || ((Effect==TEXT("Blast") || Effect==TEXT("Spread")) && T==Follow.Get())) { continue; }
            float Scale=PowerScale;
            if(Effect==TEXT("Pillar") && PulseCount>3) { Scale=0; }
            if(Effect==TEXT("Wind") && Item.Has(TEXT("WReturn")) && Age>Duration*.5f) { Scale*=Item.Mod(TEXT("WReturn"))*.01f; }
            if(Effect==TEXT("Cold"))
            {
                T->ApplyRPGStatus(TEXT("Slow"),20+Item.Mod(TEXT("IceSlow"))+Item.Mod(TEXT("IceSpread"))+Item.Mod(TEXT("IPrison")),.8f,Caster);
                if(T->Status(TEXT("FreezeLock"))<=0 && !T->IsCrowdControlImmune())
                {
                    float Cold=T->Status(TEXT("Cold"))+1; T->ApplyRPGStatus(TEXT("Cold"),Cold,4,Caster);
                    if(Cold>=3) { T->ApplyIceFreeze(FMath::Clamp(Item.Mod(TEXT("Freeze")),.35f,1.3f)); T->ApplyRPGStatus(TEXT("FreezeLock"),1,3,Caster); if(Item.Has(TEXT("ICycle"))) { Caster->ReduceSpecialCooldowns(Item.Mod(TEXT("ICycle"))); } }
                }
            }
            if(Effect==TEXT("Gravity"))
            {
                T->ApplyRPGStatus(TEXT("Vulnerable"),Item.Mod(TEXT("BlackWeak")),.8f,Caster);
                T->ApplyRPGStatus(TEXT("Slow"),FMath::Max(10.0f,Item.Mod(TEXT("BlackSlow"))),2,Caster);
                T->ApplyRPGStatus(TEXT("BlackNormal"),Item.Mod(TEXT("BlackNext")),.8f,Caster); T->ApplyRPGStatus(TEXT("Home"),Item.Mod(TEXT("BHome")),.8f,Caster);
                const FVector Push=(At-T->GetActorLocation()).GetSafeNormal2D()*100*(1+Item.Mod(TEXT("BlackPull"))*.01f);
                const auto* Boss=Cast<ARPGPrototypeBoss>(T);
                if(!T->IsDashing() && !(Boss && Boss->IsBossArmoredMove())) { T->AddActorWorldOffset(Push*.5f,true); }
                if(Distance<CurrentRadius*.4f) { Scale*=1+Item.Mod(TEXT("BlackCenter"))*.01f; }
                if(PulseCount>=2 && PulseCount<=4) { Scale+=Item.Mod(TEXT("BPulse"))*.01f; }
                OnceTargets.Add(T);
            }
            if(Effect==TEXT("Smoke"))
            { T->ApplyRPGStatus(TEXT("AimError"),15+Item.Mod(TEXT("SmokeAim")),.8f+Item.Mod(TEXT("SmokeLinger")),Caster); T->ApplyRPGStatus(TEXT("Vulnerable"),Item.Mod(TEXT("SmokeWeak")),.8f,Caster); }
            if(Effect==TEXT("Snow")) { T->ApplyRPGStatus(TEXT("Slow"),Item.Mod(TEXT("SnowFloor")),.8f,Caster); }
            if(Effect==TEXT("Wind") || Effect==TEXT("WindGuard"))
            {
                FVector Push=(T->GetActorLocation()-At).GetSafeNormal2D()*150*(1+Item.Mod(TEXT("WindPush"))*.01f);
                const auto* Boss=Cast<ARPGPrototypeBoss>(T);
                FHitResult Hit; if(!T->IsDashing() && !(Boss && Boss->IsBossArmoredMove())) { T->AddActorWorldOffset(Push,true,&Hit); }
                if(Hit.bBlockingHit && Item.Has(TEXT("WindWall"))) { Scale+=Item.Mod(TEXT("WindWall"))*.01f; }
                if(Item.Has(TEXT("WindLift")) && PulseCount==1) { T->LaunchCharacter(FVector(0,0,Item.Mod(TEXT("WindLift"))*2*T->GetVerticalImpulseTempo()),false,true); }
                if(Item.Has(TEXT("WCrash")))
                { for(TActorIterator<ARPGPrototypeCharacter> Other(GetWorld());Other;++Other) { if(*Other!=T && Other->IsGrunt()==T->IsGrunt() && !Other->IsEliminated() && FVector::DistSquared2D(Other->GetActorLocation(),T->GetActorLocation())<FMath::Square(100.0f)) { DamageEffect(this,*Other,Item.Mod(TEXT("WCrash"))*.01f); break; } } }
            }
            if(Effect==TEXT("Orbit")) { Scale*=.7f; }
            const float Applied=Scale>0?DamageEffect(this,T,Scale):0;
            if(Applied>0)
            {
                float Push=Effect==TEXT("Blast")?Item.Mod(TEXT("BlastKnockback")):0;
                if(Effect==TEXT("Wind") && !OnceTargets.Contains(T))
                { Push+=Item.Mod(TEXT("WindKick")); OnceTargets.Add(T); }
                if(Push>0)
                {
                    FVector Out=(T->GetActorLocation()-At).GetSafeNormal2D();
                    if(Out.IsNearlyZero()) { Out=Direction.GetSafeNormal2D(); }
                    T->ApplyRPGKnockback(Out,Push);
                }
            }
            if(Once) { OnceTargets.Add(T); }
        }
    }
    if(Expired)
    {
        if(Effect==TEXT("Gravity") && (Item.Has(TEXT("BlackFinal")) || Item.Has(TEXT("BFinal"))))
        { Create(Caster,Item,TEXT("Blast"),At,Radius,.15f,(Item.Mod(TEXT("BlackFinal"))+FMath::Min(5,OnceTargets.Num())*Item.Mod(TEXT("BFinal")))*.01f); }
        if(Effect==TEXT("Smoke") && Item.Has(TEXT("MFinal"))) { Create(Caster,Item,TEXT("Blast"),At,Radius,.15f,Item.Mod(TEXT("MFinal"))*.01f); }
        if(Effect==TEXT("Burn"))
        {
            if(Follow.IsValid() && !Follow->IsEliminated() && Item.Has(TEXT("FireFinal"))) { DamageEffect(this,Follow.Get(),Item.Mod(TEXT("FireFinal"))*.01f); }
            if(Follow.IsValid() && Follow->IsEliminated() && Item.Has(TEXT("FSpread")))
            { auto Copy=Item; Copy.Intrinsics.RemoveAll([](const FRPGAffix& A){return A.Key==TEXT("FSpread");}); Create(Caster,Copy,TEXT("Fire"),At,130,Item.Mod(TEXT("FSpread")),.1f); }
        }
        Destroy();
    }
}

ARPGPrototypeMerchant::ARPGPrototypeMerchant()
{
    PrimaryActorTick.bCanEverTick=true;
    auto* Root=CreateDefaultSubobject<USceneComponent>(TEXT("MerchantRoot")); SetRootComponent(Root);
    auto* Body=CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Merchant")); Body->SetupAttachment(Root);
    static ConstructorHelpers::FObjectFinder<USkeletalMesh> Mesh(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple"));
    if(Mesh.Succeeded()) { Body->SetSkeletalMesh(Mesh.Object); }
    Body->SetRelativeRotation(FRotator(0,-90,0)); Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder"));
    auto* Stand=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MerchantStand")); Stand->SetupAttachment(Root);
    Stand->SetStaticMesh(Cylinder.Object); Stand->SetRelativeLocation(FVector(0,0,-5)); Stand->SetRelativeScale3D(FVector(1.25f,1.25f,.1f));
    Stand->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}
void ARPGPrototypeMerchant::BeginPlay()
{
    Super::BeginPlay();
    if(auto* Body=FindComponentByClass<USkeletalMeshComponent>())
    {
        if(auto* Anim=LoadObject<UAnimSequence>(nullptr,TEXT("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle.MM_Idle"))) { Body->PlayAnimation(Anim,true); }
        auto* Puppet=NewObject<UChaosImpactPuppetComponent>(this); Puppet->SetupAttachment(Body); Puppet->SetRelativeScale3D(FVector(1.45f)); Puppet->RegisterComponent();
        if(Puppet->Initialize(Body,3)) { Body->SetHiddenInGame(true); Body->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones; }
        else { Puppet->DestroyComponent(); }
    }
    if(auto* Stand=FindComponentByClass<UStaticMeshComponent>()) { Stand->SetMaterial(0,ChaosImpactBallTypes::MakeEmissive(this,FLinearColor(.8f,.03f,.11f),1.2f)); }
}
void ARPGPrototypeMerchant::Tick(float Dt)
{
    Super::Tick(Dt);
    auto* P=UGameplayStatics::GetPlayerPawn(this,0);
    if(P && FVector::DistSquared2D(P->GetActorLocation(),GetActorLocation())<FMath::Square(500.0f))
    { SetActorRotation(FMath::RInterpTo(GetActorRotation(),(P->GetActorLocation()-GetActorLocation()).GetSafeNormal2D().Rotation(),Dt,3)); }
}
