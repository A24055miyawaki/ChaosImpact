#include "ChaosImpactStageBase.h"
#include "ChaosImpactCrashBall.h"
#include "Components/StaticMeshComponent.h"
#include "Components/ChildActorComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"

void AChaosImpactStageBase::BeginPlay()
{
	Super::BeginPlay();
	TArray<UChildActorComponent*> PreviewChildren;
	GetComponents(PreviewChildren);
	for (auto* Child : PreviewChildren)
	{
		if (Child->ComponentHasTag(TEXT("CI.CrashGoalPreview"))) { Child->DestroyChildActor(); Child->DestroyComponent(); }
	}
	ConfigureCrashBallMode(bCrashBallModeActive);
}

void AChaosImpactStageBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AChaosImpactStageBase, bCrashBallModeActive);
}

void AChaosImpactStageBase::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildCrashRoute();
	ConfigureCrashBallMode(bCrashBallModeActive);
	// Preview the real goal geometry in the editor, never duplicate authoritative gameplay goals.
	TArray<UChildActorComponent*> PreviewChildren;
	GetComponents(PreviewChildren);
	for (auto* Child : PreviewChildren)
	{
		if (Child->ComponentHasTag(TEXT("CI.CrashGoalPreview"))) { Child->DestroyComponent(); }
	}
	if (bPreviewCrashBallLayout && GetWorld() && !GetWorld()->IsGameWorld())
	{
		for (int32 Team = 0; Team < 2; ++Team)
		{
			auto* Child = NewObject<UChildActorComponent>(this);
			Child->CreationMethod = EComponentCreationMethod::UserConstructionScript;
			Child->bIsEditorOnly = true;
			Child->ComponentTags.Add(TEXT("CI.CrashGoalPreview"));
			Child->SetupAttachment(GetRootComponent());
			Child->SetRelativeLocation(Team == 0 ? Team0GoalOffset : Team1GoalOffset);
			Child->SetRelativeRotation(Team == 0 ? Team0GoalRotation : Team1GoalRotation);
			Child->SetChildActorClass(AChaosImpactCrashGoal::StaticClass());
			Child->RegisterComponent();
			if (auto* Goal = Cast<AChaosImpactCrashGoal>(Child->GetChildActor()))
			{
				Goal->OpeningWidth = CrashGoalWidth;
				Goal->OpeningHeight = CrashGoalHeight;
				Goal->SetTeamIndex(Team);
				Goal->SetActorHiddenInGame(true);
			}
		}
	}
}

void AChaosImpactStageBase::RebuildCrashRoute()
{
	for (UStaticMeshComponent* Part : CrashRouteMeshes) { if (Part) { Part->DestroyComponent(); } }
	CrashRouteMeshes.Reset();
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	for (const auto& Spec : CrashRouteParts)
	{
		const FVector Delta = Spec.End - Spec.Start;
		if (Delta.Size2D() < 1.f || Spec.Width < 1.f) { continue; }
		auto* Part = NewObject<UStaticMeshComponent>(this);
		Part->CreationMethod = EComponentCreationMethod::UserConstructionScript;
		Part->ComponentTags.Add(TEXT("CI.CrashRoute"));
		Part->SetupAttachment(GetRootComponent());
		Part->SetStaticMesh(Cube);
		const FRotator Rotation = Delta.Rotation();
		const FVector Up = FRotationMatrix(Rotation).GetUnitAxis(EAxis::Z);
		Part->SetRelativeLocation((Spec.Start + Spec.End) * .5f - Up * Spec.Thickness * .5f);
		Part->SetRelativeRotation(Rotation);
		Part->SetRelativeScale3D(FVector(Delta.Size() / 100., Spec.Width / 100., Spec.Thickness / 100.));
		UMaterialInterface* RouteMaterial = Spec.Material ? Spec.Material.Get() : CrashRouteMaterial.LoadSynchronous();
		Part->SetMaterial(0, RouteMaterial ? RouteMaterial : LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_Gray.MI_PrototypeGrid_Gray")));
		Part->SetCollisionProfileName(TEXT("BlockAll"));
		Part->RegisterComponent();
		CrashRouteMeshes.Add(Part);
	}
}

void AChaosImpactStageBase::ConfigureCrashBallMode(const bool bEnable)
{
	bCrashBallModeActive = bEnable;
	const bool bVisible = bEnable || (bPreviewCrashBallLayout && GetWorld() && !GetWorld()->IsGameWorld());
	if (CrashRouteMeshes.IsEmpty() && !CrashRouteParts.IsEmpty()) { RebuildCrashRoute(); }
	for (UStaticMeshComponent* Part : CrashRouteMeshes)
	{
		if (Part)
		{
			Part->SetVisibility(bVisible, true);
			Part->SetHiddenInGame(!bEnable);
			Part->SetCollisionEnabled(bVisible ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
		}
	}
	TArray<UStaticMeshComponent*> Parts;
	GetComponents(Parts);
	for (auto* Part : Parts)
	{
		// Preview must never serialize disabled collision/visibility into the normal-rule map.
		if (!GetWorld() || !GetWorld()->IsGameWorld()) { continue; }
		if (Part->ComponentHasTag(TEXT("CI.CrashRoute"))) { continue; }
		bool bReplace = false;
		for (const FString& Prefix : CrashRemovedObstaclePrefixes) { bReplace |= Part->GetName().StartsWith(Prefix); }
		if (!bReplace) { continue; }
		if (!CrashOriginalObstacles.Contains(Part))
		{
			CrashOriginalObstacles.Add(Part, {Part->IsVisible(), Part->GetCollisionEnabled()});
		}
		const auto& Original = CrashOriginalObstacles.FindChecked(Part);
		Part->SetVisibility(bVisible ? false : Original.Key);
		Part->SetHiddenInGame(bVisible);
		Part->SetCollisionEnabled(bVisible ? ECollisionEnabled::NoCollision : Original.Value.GetValue());
	}
	if (HasAuthority()) { ForceNetUpdate(); }
}

void AChaosImpactStageBase::OnRep_CrashLayout()
{
	ConfigureCrashBallMode(bCrashBallModeActive);
}
