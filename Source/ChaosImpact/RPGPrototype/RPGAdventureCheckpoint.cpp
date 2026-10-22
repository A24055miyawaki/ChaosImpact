#include "RPGAdventure.h"
#include "EngineUtils.h"
#include "RPGPrototype.h"
#include "Components/StaticMeshComponent.h"
#include "Components/ArrowComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"

ARPGAdventureCheckpoint::ARPGAdventureCheckpoint()
{
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("CheckpointRoot")));
    const auto Add=[&](const TCHAR* Name,const TCHAR* Shape,const TCHAR* Mat,FVector At,FVector Size)
    {
        auto* Mesh=CreateDefaultSubobject<UStaticMeshComponent>(Name); Mesh->SetupAttachment(RootComponent);
        Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,*FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"),Shape,Shape)));
        Mesh->ComponentTags.Add(FName(*(FString(TEXT("Look_"))+Mat)));
        Mesh->SetRelativeLocation(At); Mesh->SetRelativeScale3D(Size/100);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    };
    Add(TEXT("StoneBase"),TEXT("Cylinder"),TEXT("StoneDark"),{0,0,20},{160,160,40});
    Add(TEXT("CopperBowl"),TEXT("Cylinder"),TEXT("Copper"),{0,0,55},{120,120,30});
    Add(TEXT("Beacon"),TEXT("Sphere"),TEXT("GlowTeal"),{0,0,120},{65,65,100});
    auto* Arrow=CreateDefaultSubobject<UArrowComponent>(TEXT("RespawnDirection"));
    Arrow->SetupAttachment(RootComponent); Arrow->SetRelativeLocation({150,0,120});
    Arrow->SetHiddenInGame(true); Arrow->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}
void ARPGAdventureCheckpoint::ApplyLook()
{
    TArray<UStaticMeshComponent*> Parts; GetComponents(Parts);
    for(auto* Part:Parts)
    {
        for(const FName& Tag:Part->ComponentTags)
        {
            FString Name=Tag.ToString();
            if(!Name.RemoveFromStart(TEXT("Look_"))) { continue; }
            Part->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,*FString::Printf(TEXT("/Game/ChaosImpact/RPGPrototype/Adventure/Materials/M_%s.M_%s"),*Name,*Name)));
        }
    }
}
void ARPGAdventureCheckpoint::OnConstruction(const FTransform& Transform) { Super::OnConstruction(Transform); ApplyLook(); }
void ARPGAdventureCheckpoint::BeginPlay() { Super::BeginPlay(); ApplyLook(); }
FVector ARPGAdventureCheckpoint::RespawnFor(int32 PlayerIndex) const
{
    return GetActorTransform().TransformPosition(RespawnOffset+FVector(0,(FMath::Clamp(PlayerIndex,0,3)-1.5f)*80,0));
}
bool ARPGAdventureCheckpoint::TryActivate(ARPGPrototypeCharacter* Player)
{
    if(!IsValid(Player) || Player->IsGrunt() || Player->IsEliminated() || Player->ActiveAdventureCheckpoint.Get()==this
        || FVector::DistSquared2D(Player->GetActorLocation(),GetActorLocation())>FMath::Square(ActivationRadius)
        || FMath::Abs(Player->GetActorLocation().Z-GetActorLocation().Z)>220) { return false; }
    FHitResult Hit; FCollisionQueryParams Q(SCENE_QUERY_STAT(RPGCheckpoint),false,this); Q.AddIgnoredActor(Player);
    if(GetWorld()->LineTraceSingleByObjectType(Hit,GetActorLocation()+FVector(0,0,100),Player->GetActorLocation(),FCollisionObjectQueryParams(ECC_WorldStatic),Q)) { return false; }
    Player->ActivateAdventureCheckpoint(this); return true;
}
void ARPGPrototypeCharacter::ActivateAdventureCheckpoint(ARPGAdventureCheckpoint* Checkpoint)
{
    if(!IsValid(Checkpoint) || bGrunt || IsEliminated()) { return; }
    ActiveAdventureCheckpoint=Checkpoint;
    SetRespawnPoint(Checkpoint->RespawnFor(GetLocalRPGIndex()),Checkpoint->GetActorRotation());
    // Online guest: the host keeps the respawn point and the HP.
    if(IsNetClientHero()) { ServerRestAt(Checkpoint->RespawnFor(GetLocalRPGIndex()),Checkpoint->GetActorRotation(),true); }
    RestoreHP(MaxHealth); Stamina=MaxStamina;
    // Every lit beacon can be travelled back to (RPGFastTravel.cpp).
    const bool bNewPlace=!TravelPoints.Contains(TravelKey(Checkpoint)); TravelPoints.AddUnique(TravelKey(Checkpoint));
    Notice=Checkpoint->DisplayName+(bNewPlace?TEXT(" — チェックポイント更新・ファストトラベルできるようになった"):TEXT(" — チェックポイント更新")); NoticeUntil=GetWorld()->GetTimeSeconds()+3;
    // Each beacon has a stable bit for the beacon quest: its place among all beacons ordered by name.
    TArray<FString> Names; for(TActorIterator<ARPGAdventureCheckpoint> It(GetWorld());It;++It) { Names.AddUnique(It->DisplayName); }
    Names.Sort();
    QuestEvent(ERPGQuestEvent::BeaconLit,Checkpoint->GetActorLocation(),FMath::Clamp(Names.IndexOfByKey(Checkpoint->DisplayName),0,7));
}
