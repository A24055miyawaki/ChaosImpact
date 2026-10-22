#include "RPGAdventure.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"

ARPGAdventureSceneryPiece::ARPGAdventureSceneryPiece()
{
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("SceneryRoot")));
    GetRootComponent()->SetMobility(EComponentMobility::Static);
}

int32 ARPGAdventureWorld::BakeEditableScenery()
{
#if WITH_EDITOR
    if(!GetWorld() || GetWorld()->IsGameWorld() || bUseEditableScenery) { return 0; }
    // Convert scenery only, preserving all gameplay actors and their placements.
    Modify(); Rebuild();
    TMap<FString,ARPGAdventureSceneryPiece*> Groups;
    for(const auto& PartPtr:Parts)
    {
        auto* Part=PartPtr.Get();
        if(!IsValid(Part)) { continue; }
        const auto* Names=EditingGroups.Find(Part);
        for(int32 I=0;I<Part->GetInstanceCount();++I)
        {
            FTransform At; Part->GetInstanceTransform(I,At,true);
            const FString Key=Names && Names->IsValidIndex(I)?(*Names)[I]:Part->GetName();
            auto*& Actor=Groups.FindOrAdd(Key);
            if(!Actor)
            {
                FActorSpawnParameters Params; Params.ObjectFlags|=RF_Transactional;
                Actor=GetWorld()->SpawnActor<ARPGAdventureSceneryPiece>(At.GetLocation(),FRotator::ZeroRotator,Params);
                if(!Actor)
                {
                    // Never discard the source geometry after a partial conversion.
                    for(const auto& Existing:Groups) { if(IsValid(Existing.Value)) { Existing.Value->Destroy(); } }
                    return 0;
                }
                Actor->SetActorLabel(TEXT("RPG_")+Key);
                const TCHAR* Category=Key.StartsWith(TEXT("地形_"))?TEXT("地形タイル"):
                    Key.StartsWith(TEXT("樹木_"))?TEXT("樹木"):
                    Key.StartsWith(TEXT("街灯_"))?TEXT("街灯"):TEXT("建物と装飾");
                FVector RegionProbe=At.GetLocation(); RegionProbe.Z=GetActorLocation().Z+120;
                Actor->SetFolderPath(FName(*(TEXT("RPGAdventure/Editable/")+RegionName(RegionProbe)+TEXT("/")+Category)));
                Actor->Tags.Add(TEXT("RPGEditableScenery"));
            }
            auto* Mesh=NewObject<UStaticMeshComponent>(Actor,
                MakeUniqueObjectName(Actor,UStaticMeshComponent::StaticClass(),FName(*Part->GetName())),RF_Transactional);
            Mesh->SetupAttachment(Actor->GetRootComponent());
            Mesh->SetMobility(EComponentMobility::Static);
            Mesh->SetStaticMesh(Part->GetStaticMesh()); Mesh->SetMaterial(0,Part->GetMaterial(0));
            Mesh->SetCollisionEnabled(Part->GetCollisionEnabled()); Mesh->SetCollisionObjectType(ECC_WorldStatic);
            Mesh->SetGenerateOverlapEvents(false); Mesh->SetCanEverAffectNavigation(false);
            Actor->AddInstanceComponent(Mesh); Mesh->RegisterComponent(); Mesh->SetWorldTransform(At);
            Actor->MarkPackageDirty();
        }
    }
    if(Groups.IsEmpty()) { return 0; }
    bUseEditableScenery=true;
    for(const auto& PartPtr:Parts)
    { auto* Part=PartPtr.Get(); if(IsValid(Part)) { RemoveInstanceComponent(Part); Part->DestroyComponent(); } }
    Parts.Empty(); Batches.Empty(); EditingGroups.Empty(); EditingGroup.Empty(); MarkPackageDirty();
    return Groups.Num();
#else
    return 0;
#endif
}
