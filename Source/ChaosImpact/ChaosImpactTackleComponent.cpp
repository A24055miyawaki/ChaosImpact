#include "ChaosImpactTackleComponent.h"
#include "ChaosImpactTackleVFX.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactBallFormComponent.h"
#include "ChaosImpactBuddyBall.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactPuppetComponent.h"
#include "ChaosImpactSettings.h"
#include "ChaosImpactSfx.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"

UChaosImpactTackleComponent::UChaosImpactTackleComponent()
{
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.TickGroup=TG_PostUpdateWork;
    SetIsReplicatedByDefault(true);
}
void UChaosImpactTackleComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{ Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(UChaosImpactTackleComponent,State); }
AChaosImpactCharacter* UChaosImpactTackleComponent::Player() const { return Cast<AChaosImpactCharacter>(GetOwner()); }
double UChaosImpactTackleComponent::Now() const
{ const auto* GS=GetWorld()?GetWorld()->GetGameState():nullptr; return GS?GS->GetServerWorldTimeSeconds():GetWorld()?GetWorld()->GetTimeSeconds():0; }
bool UChaosImpactTackleComponent::IsBusy() const
{ return Now()<GetStunDeadline() || Now()<State.Started+ActiveSeconds+RecoverySeconds; }
bool UChaosImpactTackleComponent::IsActive() const { return Now()<State.Started+ActiveSeconds; }
bool UChaosImpactTackleComponent::CanStart() const
{
    const auto* P=Player(); const auto* PC=P?Cast<AChaosImpactPlayerController>(P->GetController()):nullptr;
    return P && GetWorld() && (!PC || PC->IsGameplayActive() || P->IsRemotePlayerOnServer())
        && !P->IsEliminated() && !P->IsInBallForm() && !P->IsDashing() && !P->IsIceFrozen()
        && !P->IsTrainingMenuFrozen() && !P->IsThrowReleasePending() && !P->IsDriving() && !P->IsMatchInputLocked()
        && !P->WindCarrier && !P->bMapWalker && !IsBusy() && Now()>=State.ReadyAt
        && P->GetStamina()>=FMath::Max(0.f,StaminaCost) && P->GetCharacterMovement()->IsMovingOnGround();
}
FVector UChaosImpactTackleComponent::PickDirection() const
{
    auto* P=Player(); if(!P) { return FVector::ForwardVector; }
    if(const auto* PC=Cast<APlayerController>(P->GetController()))
    {
        bool bStick=false; auto Input=ChaosImpactSettings::GetMoveInput(PC,bStick);
        if(bStick && Input.Size()<P->MovementStickDeadZone) { Input=FVector2D::ZeroVector; }
        if(!Input.IsNearlyZero())
        {
            const FRotator Yaw(0,P->CameraBoom->GetComponentRotation().Yaw,0);
            return (FRotationMatrix(Yaw).GetUnitAxis(EAxis::X)*Input.Y+FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y)*Input.X).GetSafeNormal2D();
        }
        if(ChaosImpactSettings::IsMouseOnlyScheme(ChaosImpactSettings::GetPlayerControlScheme(PC)))
        { FVector Aim; if(P->FindMouseAimPoint(Aim)) { return (Aim-P->GetActorLocation()).GetSafeNormal2D(); } }
    }
    return P->AimDirection.GetSafeNormal2D();
}
bool UChaosImpactTackleComponent::Request()
{
    if(!CanStart() || Now()<RequestAgainAt) { return false; }
    auto* P=Player(); const FVector Direction=PickDirection();
    if(Direction.IsNearlyZero()) { return false; }
    P->RequestCancelThrow();
    P->bWaitForThrowRelease=true; P->bClickMoveActive=false; P->ClickMovePath.Reset(); P->bPointerMoveHeld=false;
    RequestAgainAt=Now()+.2;
    if(P->HasAuthority()) { ServerRequest_Implementation(Direction); }
    else if(P->IsLocallyControlled()) { ServerRequest(Direction); }
    else { return false; }
    return true;
}
void UChaosImpactTackleComponent::ServerRequest_Implementation(FVector_NetQuantizeNormal Direction)
{
    auto* P=Player(); if(!CanStart() || Direction.ContainsNaN()) { return; }
    const FVector Flat=FVector(Direction).GetSafeNormal2D(); if(Flat.IsNearlyZero()) { return; }
    P->RequestCancelThrow(); P->bClickMoveActive=false; P->ClickMovePath.Reset(); P->bPointerMoveHeld=false;
    State.Started=Now(); State.ReadyAt=Now()+FMath::Max(CooldownSeconds,ActiveSeconds+RecoverySeconds);
    State.Direction=Flat; PreviousLocation=P->GetActorLocation(); bHit=false; bStoppedRush=false; StartHealth=P->GetHealth();
    P->Stamina=FMath::Max(0.f,P->Stamina-FMath::Max(0.f,StaminaCost));
    if(P->IsRemotePlayerOnServer()) { P->ClientAddStamina(-FMath::Max(0.f,StaminaCost)); }
    ClientInterrupt(State.StunnedUntil);
    P->SetActorRotation(Flat.Rotation());
    Kick(P,Flat*RushSpeed+FVector(0,0,100));
    MulticastRush(P->GetActorLocation(),Flat);
    P->ForceNetUpdate();
}
void UChaosImpactTackleComponent::Kick(AChaosImpactCharacter* Who,const FVector& Velocity)
{
    if(Who->IsRemotePlayerOnServer())
    {
        // A 0.2-second contact window cannot wait for the owner's movement to make a round trip.
        // Simulate this short impulse on the server as well, with normal CMC correction temporarily enabled.
        Who->ServerAuthoritativeUntil=GetWorld()->GetTimeSeconds()+FMath::Max(.6f,Who->GetNetworkRoundTripSeconds()*2+.3f);
        Who->UpdateMovementAuthority();
        FVector Impulse=Velocity; Impulse.Z*=Who->GetVerticalImpulseTempo();
        Who->LaunchCharacter(Impulse,true,true);
    }
    Who->ApplyBlastKnockback(Velocity);
}
void UChaosImpactTackleComponent::ClientInterrupt_Implementation(double StunnedUntil)
{
    // Inventory/cancel RPCs can precede the ordinary replicated state by a frame.
    ClientStunUntil=FMath::Max(ClientStunUntil,StunnedUntil);
    auto* P=Player(); if(!P) { return; }
    P->CancelChargingThrow(); P->bThrowAwaitingPickup=false; P->bWaitForThrowRelease=true;
    P->bClickMoveActive=false; P->ClickMovePath.Reset(); P->bPointerMoveHeld=false;
    P->GetWorldTimerManager().ClearTimer(P->ThrowReleaseTimer);
    // Only discard disposable local predictions, never an authoritative teammate ball.
    for(auto Ball:{P->PendingThrowBall,P->PredictedThrowBall})
    { if(Ball.IsValid() && !P->HasAuthority() && Ball->GetLocalRole()==ROLE_Authority) { Ball->Destroy(); } }
    if(!P->HasAuthority()) { P->PendingThrowBall.Reset(); P->PredictedThrowBall.Reset(); P->bThrowReleasePending=false; }
}
void UChaosImpactTackleComponent::InterruptAndDrop()
{
    auto* P=Player(); if(!P || !P->HasAuthority()) { return; }
    P->RequestCancelThrow(); P->bThrowAwaitingPickup=false;
    bool bDroppedPending=false;
    if(auto* Ball=P->PendingThrowBall.Get())
    {
        P->GetWorldTimerManager().ClearTimer(P->ThrowReleaseTimer); P->PendingThrowBall.Reset(); P->bThrowReleasePending=false;
        if(auto* Buddy=Cast<AChaosImpactBuddyBall>(Ball)) { Buddy->ReleasePassenger(); }
        else { Ball->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform); Ball->MakeRollingPickup(State.Direction*500); }
        bDroppedPending=true;
    }
    if(!bDroppedPending && P->GetCarriedBallCount()>0) { P->DropFrontBall(); }
    P->UpdateBallPresentation(); P->ClientBallCountReset(P->CarriedBallCount,P->CarriedBallTypes); ClientInterrupt(State.StunnedUntil);
}
void UChaosImpactTackleComponent::ApplyContact(AChaosImpactCharacter* Victim)
{
    auto* P=Player(); if(!P || !Victim || bHit) { return; }
    bHit=true;
    auto* Other=Victim->GetTackle(); Other->ResetAction();
    Other->State.Direction=State.Direction; Other->State.StunnedUntil=Now()+.28;
    if(Victim->IsInBallForm()) { Victim->GetBallForm()->Restore(); }
    Other->InterruptAndDrop();
    Kick(Victim,FVector(State.Direction)*KnockbackSpeed+FVector(0,0,KnockbackUpwardSpeed));
    Victim->ForceNetUpdate();
    MulticastImpact((P->GetActorLocation()+Victim->GetActorLocation())*.5,State.Direction);
}
void UChaosImpactTackleComponent::CheckContact()
{
    auto* P=Player(); if(!P || !P->HasAuthority() || bHit) { return; }
    const FVector Here=P->GetActorLocation();
    // Trace along the whole travelled segment plus shoulder reach: no tunnelling through a nearby player.
    TArray<FHitResult> Hits; FCollisionQueryParams Q(SCENE_QUERY_STAT(Tackle),false,P);
    GetWorld()->SweepMultiByChannel(Hits,PreviousLocation,Here+FVector(State.Direction)*60,FQuat::Identity,
        ECC_Pawn,FCollisionShape::MakeCapsule(60,75),Q);
    for(const auto& Hit:Hits)
    {
        auto* Victim=Cast<AChaosImpactCharacter>(Hit.GetActor());
        if(!Victim || Victim==P || Victim->IsEliminated() || Victim->IsDashing() || Victim->IsTrainingMenuFrozen()
            || AChaosImpactGameState::AreTeammates(GetWorld(),P,Victim)) { continue; }
        FHitResult Wall;
        // A trace *channel* named WorldStatic can still hit pawn capsules; restrict object types for walls.
        if(GetWorld()->LineTraceSingleByObjectType(Wall,Here,Victim->GetActorLocation(),FCollisionObjectQueryParams(ECC_WorldStatic),Q)) { continue; }
        ApplyContact(Victim); break;
    }
    PreviousLocation=Here;
}
void UChaosImpactTackleComponent::MulticastImpact_Implementation(FVector_NetQuantize At,FVector_NetQuantizeNormal Direction)
{
    if(GetNetMode()==NM_DedicatedServer) { return; }
    AChaosImpactTackleVFX::Play(GetWorld(),At,Direction,true);
    using namespace ChaosImpactBallTypes;
    if(auto* Effect=LoadEffect(Effects::SparkBurst))
    { if(auto* FX=UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(),Effect,At,FVector(Direction).Rotation(),FVector(.85f)))
        { SetEffectColor(FX,TEXT("Color"),FLinearColor(1,.65f,.15f)*3); } }
    ChaosImpactSfx::PlayAt(this,EChaosImpactSfx::BallHit,At,.8f,.8f);
}
void UChaosImpactTackleComponent::MulticastRush_Implementation(FVector_NetQuantize At,FVector_NetQuantizeNormal Direction)
{
    AChaosImpactTackleVFX::Play(GetWorld(),At,Direction,false,Player());
}
bool UChaosImpactTackleComponent::ComboDown() const
{
    auto* P=Player(); const auto* PC=P?Cast<APlayerController>(P->GetController()):nullptr;
    if(!PC) { return false; }
    using namespace ChaosImpactSettings;
    const auto Scheme=GetPlayerControlScheme(PC);
    if(Scheme==EChaosImpactControlScheme::Simple) { return IsActionDown(PC,EChaosImpactAction::Throw)&&IsActionDown(PC,EChaosImpactAction::Dash); }
    return IsMouseOnlyScheme(Scheme)&&PC->IsInputKeyDown(EKeys::LeftMouseButton)&&PC->IsInputKeyDown(EKeys::RightMouseButton);
}
bool UChaosImpactTackleComponent::HandleKey(const FKey& Key,EInputEvent Event)
{
    auto* P=Player(); const auto* PC=P?Cast<APlayerController>(P->GetController()):nullptr;
    if(!PC || P->IsInBallForm() || P->bMapWalker) { return false; }
    using namespace ChaosImpactSettings;
    const auto Scheme=GetPlayerControlScheme(PC);
    const bool bPress=Event==IE_Pressed || Event==IE_DoubleClick;
    if(Scheme==EChaosImpactControlScheme::Normal)
    { if(IsActionKey(PC,EChaosImpactAction::Tackle,Key)) { if(bPress) { Request(); } return true; } return false; }
    bool bChordKey=false,bChord=false;
    if(Scheme==EChaosImpactControlScheme::Simple)
    {
        const bool T=IsActionKey(PC,EChaosImpactAction::Throw,Key),D=IsActionKey(PC,EChaosImpactAction::Dash,Key);
        if(T) { bThrowEventDown=bPress; ThrowEventAt=Now(); }
        if(D) { bDashEventDown=bPress; DashEventAt=Now(); }
        bChordKey=T||D;
        bChord=bPress&&((T&&(IsActionDown(PC,EChaosImpactAction::Dash)||(bDashEventDown&&Now()-DashEventAt<.1)))
            ||(D&&(IsActionDown(PC,EChaosImpactAction::Throw)||(bThrowEventDown&&Now()-ThrowEventAt<.1))));
    }
    else
    {
        bChordKey=Key==EKeys::LeftMouseButton || Key==EKeys::RightMouseButton;
        if(Key==EKeys::LeftMouseButton) { bThrowEventDown=bPress; ThrowEventAt=Now(); }
        if(Key==EKeys::RightMouseButton) { bDashEventDown=bPress; DashEventAt=Now(); }
        bChord=bPress&&((Key==EKeys::LeftMouseButton&&(PC->IsInputKeyDown(EKeys::RightMouseButton)||(bDashEventDown&&Now()-DashEventAt<.1)))
            ||(Key==EKeys::RightMouseButton&&(PC->IsInputKeyDown(EKeys::LeftMouseButton)||(bThrowEventDown&&Now()-ThrowEventAt<.1))));
    }
    if(bChord && !bComboLatched && !P->IsPointerOverGameplayUI())
    {
        PendingDashAt=-1; bComboLatched=true; P->RequestCancelThrow();
        P->bMouseChargeActive=false; P->bWaitForThrowRelease=true; Request(); return true;
    }
    return bComboLatched&&bChordKey;
}
bool UChaosImpactTackleComponent::HandleInput()
{
    if(auto* P=Player(); !P || P->IsInBallForm() || P->bMapWalker) { ResetInput(); return false; }
    if(!ComboDown())
    {
        auto* P=Player(); const auto* PC=P?Cast<APlayerController>(P->GetController()):nullptr;
        if(PC)
        {
            using namespace ChaosImpactSettings;
            const auto S=GetPlayerControlScheme(PC);
            const bool Down=S==EChaosImpactControlScheme::Simple?(IsActionDown(PC,EChaosImpactAction::Throw)||IsActionDown(PC,EChaosImpactAction::Dash))
                :(PC->IsInputKeyDown(EKeys::LeftMouseButton)||PC->IsInputKeyDown(EKeys::RightMouseButton));
            if(!Down) { bComboLatched=false; }
            if(S==EChaosImpactControlScheme::MouseHold && bComboLatched && !IsBusy() && !PC->IsInputKeyDown(EKeys::LeftMouseButton))
            { bComboLatched=false; P->bPointerMoveHeld=PC->IsInputKeyDown(EKeys::RightMouseButton); }
        }
    }
    else if(!bComboLatched) { PendingDashAt=-1; bComboLatched=true; Request(); }
    return IsBusy() || bComboLatched;
}
void UChaosImpactTackleComponent::QueueSimpleDash() { if(!bComboLatched) { PendingDashAt=Now()+.07; } }
void UChaosImpactTackleComponent::ResetInput()
{ PendingDashAt=-1; bComboLatched=false; bThrowEventDown=false; bDashEventDown=false; }
void UChaosImpactTackleComponent::ResetAction()
{ State.Started=-100; State.StunnedUntil=0; ClientStunUntil=0; bHit=true; bStoppedRush=true; ResetInput(); }
void UChaosImpactTackleComponent::UpdatePose(float DeltaTime)
{
    auto* P=Player(); if(!P || !P->ToonCharacter) { return; }
    const float Age=static_cast<float>(Now()-State.Started);
    const bool Active=Age>=0 && Age<ActiveSeconds+RecoverySeconds && !P->IsEliminated() && !P->IsInBallForm();
    if(Active)
    {
        P->SetActorRotation(FVector(State.Direction).Rotation());
        if(!bPoseSaved) { SavedPose=P->ToonCharacter->GetRelativeTransform(); bPoseSaved=true; }
        const float W=FMath::Clamp(Age/.055f,0.f,1.f)*(1-FMath::SmoothStep(ActiveSeconds,ActiveSeconds+RecoverySeconds,Age));
        const FTransform Neutral=SavedPose*P->ToonCharacter->GetAttachParent()->GetComponentTransform();
        const FVector Axis=FVector::CrossProduct(FVector::UpVector,FVector(State.Direction)).GetSafeNormal();
        P->ToonCharacter->SetWorldLocationAndRotation(Neutral.GetLocation()+FVector(State.Direction)*W*10-FVector::UpVector*W*8,
            FQuat(Axis,FMath::DegreesToRadians(38*W))*Neutral.GetRotation());
        P->ToonCharacter->SetTacklePose(W); P->ToonCharacter->UpdatePose(true);
    }
    else if(bPoseSaved)
    { P->ToonCharacter->SetRelativeTransform(SavedPose); P->ToonCharacter->SetTacklePose(0); bPoseSaved=false; }
}
void UChaosImpactTackleComponent::TickComponent(float Delta,ELevelTick Type,FActorComponentTickFunction* Function)
{
    Super::TickComponent(Delta,Type,Function); auto* P=Player(); if(!P) { return; }
    if(PendingDashAt>=0 && Now()>=PendingDashAt)
    { PendingDashAt=-1; if(!bComboLatched && !IsBusy()) { P->StartDash(); } }
    // Take the speed out before the recovery, without granting dash invulnerability.
    if(State.Started!=ObservedStart)
    {
        ObservedStart=State.Started; bStoppedRush=false;
        if(IsActive()) { ChaosImpactSfx::PlayAt(this,EChaosImpactSfx::CharDash,P->GetActorLocation(),.7f,.82f); }
    }
    // Include the last travelled segment even when a slow frame crosses the end of the active window.
    if(P->HasAuthority() && State.Started>=0 && !bStoppedRush)
    { if(P->IsEliminated() || P->IsTrainingMenuFrozen() || P->GetHealth()<StartHealth) { State.Started=-100; }
      else { CheckContact(); } }
    if(!bStoppedRush && State.Started>=0 && Now()>=State.Started+ActiveSeconds)
    {
        bStoppedRush=true;
        if(P->IsLocallyControlled() || P->HasAuthority()) { auto V=P->GetCharacterMovement()->Velocity; V.X=0; V.Y=0; P->GetCharacterMovement()->Velocity=V; }
    }
    UpdatePose(Delta);
}
