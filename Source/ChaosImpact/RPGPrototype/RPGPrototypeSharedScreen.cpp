#include "RPGPrototype.h"
#include "RPGPrototypeHUD.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"

void ARPGPrototypeGameMode::BeginSharedViewDraw()
{
    // Render one view only. Works with both the ordinary and PIE viewport clients.
    if(UsesSharedScreen()) { GetWorld()->GetGameViewport()->LayoutPlayers(); }
}
void ARPGPrototypeGameMode::EndSharedViewDraw()
{
    // Outside the renderer every local controller needs valid projection geometry.
    // Otherwise a keyboard/mouse assigned to P2 cannot aim on a non-split screen.
    if(!UsesSharedScreen()) { return; }
    for(auto* Local:GetGameInstance()->GetLocalPlayers())
    { Local->Origin=FVector2D::ZeroVector; Local->Size=FVector2D(1,1); }
}

void ARPGPrototypeGameMode::SetSharedScreen(bool Enabled)
{
    if(bSharedScreen==Enabled) { return; }
    bSharedScreen=Enabled;
    GetWorld()->URL.RemoveOption(TEXT("CISharedScreen=")); GetWorld()->URL.AddOption(Enabled?TEXT("CISharedScreen=1"):TEXT("CISharedScreen=0"));
    ApplyRPGScreenLayout();
}
void ARPGPrototypeGameMode::ApplyRPGScreenLayout()
{
    const bool Shared=UsesSharedScreen();
    if(auto* Viewport=GetWorld()->GetGameViewport()) { Viewport->SetForceDisableSplitscreen(Shared); Viewport->LayoutPlayers(); }
    EndSharedViewDraw();
    for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
    {
        auto* PC=It->Get(); auto* Player=Cast<ARPGPrototypeCharacter>(PC->GetPawn()); if(!Player) { continue; }
        if(!Shared && SharedCamera && PC->GetViewTarget()==SharedCamera) { PC->SetViewTarget(Player); }
        if(auto* HUD=Player->GetPrototypeHUD())
        { HUD->RemoveFromParent(); if(Shared) { HUD->AddToViewport(20+FMath::Max(0,Player->GetLocalRPGIndex())); } else { HUD->AddToPlayerScreen(20); } }
    }
    if(!Shared && SharedCamera) { SharedCamera->Destroy(); SharedCamera=nullptr; }
}
void ARPGPrototypeGameMode::TickSharedScreen(float DeltaSeconds)
{
    if(!UsesSharedScreen()) { return; }
    TArray<ARPGPrototypeCharacter*> Living;
    for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
    { if(auto* P=Cast<ARPGPrototypeCharacter>(It->Get()->GetPawn());P && !P->IsGrunt() && !P->IsEliminated()) { Living.Add(P); } }
    if(Living.IsEmpty()) { return; }
    auto* Anchor=Living[0];
    for(auto* P:Living) { if(P->GetLocalRPGIndex()==0) { Anchor=P; break; } }
    const double Now=GetWorld()->GetTimeSeconds();
    for(auto* P:Living)
    {
        if(P==Anchor || Now<SharedWarpAt.FindRef(P) || FVector::DistSquared2D(P->GetActorLocation(),Anchor->GetActorLocation())<=FMath::Square(SharedWarpDistance)) { continue; }
        SharedWarpAt.Add(P,Now+1);
        bool Found=false; FVector Destination;
        for(float Radius:{180.f,300.f,460.f})
        {
            for(int32 I=0;I<12 && !Found;++I)
            {
                const float A=(I*30.f+180)*PI/180.f;
                const FVector At=Anchor->GetActorLocation()+FVector(FMath::Cos(A)*Radius,FMath::Sin(A)*Radius,0);
                FHitResult H; FCollisionQueryParams Q(SCENE_QUERY_STAT(RPGSharedWarp),false,P); Q.AddIgnoredActor(Anchor);
                if(!GetWorld()->LineTraceSingleByObjectType(H,At+FVector(0,0,600),At-FVector(0,0,500),FCollisionObjectQueryParams(ECC_WorldStatic),Q)
                    || H.ImpactNormal.Z<.7f || FMath::Abs(H.ImpactPoint.Z-(Anchor->GetActorLocation().Z-96))>150) { continue; }
                FVector Spawn=H.ImpactPoint+FVector(0,0,100);
                if(GetWorld()->OverlapAnyTestByObjectType(Spawn,FQuat::Identity,FCollisionObjectQueryParams(ECC_WorldStatic),FCollisionShape::MakeCapsule(44,96),Q)) { continue; }
                if(!GetWorld()->FindTeleportSpot(P,Spawn,P->GetActorRotation())) { continue; }
                Destination=Spawn; Found=true;
            }
            if(Found) { break; }
        }
        if(Found)
        {
            P->RequestCancelThrow(); P->GetCharacterMovement()->StopMovementImmediately();
            P->SetActorLocation(Destination,false,nullptr,ETeleportType::TeleportPhysics); P->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
            P->Notice=TEXT("仲間の近くへ合流しました"); P->NoticeUntil=Now+2;
        }
    }
    FVector Center=FVector::ZeroVector; for(auto* P:Living) { Center+=P->GetActorLocation(); } Center/=Living.Num();
    float Radius=0; for(auto* P:Living) { Radius=FMath::Max(Radius,static_cast<float>(FVector::Dist2D(P->GetActorLocation(),Center))); }
    const bool Created=!IsValid(SharedCamera);
    if(Created) { SharedCamera=GetWorld()->SpawnActor<ACameraActor>(); }
    if(!SharedCamera) { return; }
    SharedCamera->SetOwner(Anchor);
    const FRotator Rotation=Anchor->GetCameraBoom()->GetComponentRotation();
    const float Base=Anchor->GetCameraBoom()->TargetArmLength;
    const float Distance=FMath::Clamp(Base*1.18f+Radius*.95f,Base*1.18f,Base*2.4f);
    const FVector At=Center+FVector(0,0,60)-Rotation.Vector()*Distance;
    SharedCamera->SetActorLocationAndRotation(Created?At:FMath::VInterpTo(SharedCamera->GetActorLocation(),At,DeltaSeconds,7),Rotation);
    SharedCamera->GetCameraComponent()->SetFieldOfView(70);
    SharedCamera->GetCameraComponent()->bConstrainAspectRatio=false;
    for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
    { if(It->Get()->GetViewTarget()!=SharedCamera) { It->Get()->SetViewTarget(SharedCamera); } }
}
