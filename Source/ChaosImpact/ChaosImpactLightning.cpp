#include "ChaosImpactLightning.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "ProceduralMeshComponent.h"

namespace
{
	FVector LightningSideAxis(const FVector& Along, const FVector& Facing)
	{
		FVector Side = FVector::CrossProduct(Along, Facing).GetSafeNormal();
		return Side.IsNearlyZero() ? FVector::CrossProduct(Along, FVector::RightVector).GetSafeNormal() : Side;
	}

	void AppendLightningRibbon(ChaosImpactIceMeshes::FMeshBuffers& Mesh, const TArray<FVector>& Points,
		const float StartWidth, const float EndWidth, const FVector& Facing)
	{
		const int32 Last = Points.Num() - 1;
		for (int32 Index = 0; Index < Last; ++Index)
		{
			const FVector& From = Points[Index];
			const FVector& To = Points[Index + 1];
			const FVector Side = LightningSideAxis(To - From, Facing);
			const FVector Side0 = Side * FMath::Lerp(StartWidth, EndWidth, static_cast<float>(Index) / Last) * 0.5f;
			const FVector Side1 = Side * FMath::Lerp(StartWidth, EndWidth, static_cast<float>(Index + 1) / Last) * 0.5f;
			const int32 First = Mesh.Vertices.Num();
			Mesh.Vertices.Append({From - Side0, From + Side0, To - Side1, To + Side1});
			for (int32 Corner = 0; Corner < 4; ++Corner)
			{
				Mesh.Normals.Add(-Facing);
				Mesh.UVs.Add(FVector2D(Corner % 2, Corner < 2 ? 0.0f : 1.0f));
			}
			// Both windings, so the ribbon shows from either side.
			Mesh.Triangles.Append({First, First + 1, First + 2, First + 2, First + 1, First + 3,
				First, First + 2, First + 1, First + 2, First + 3, First + 1});
		}
	}
}

void ChaosImpactLightning::AppendBolt(ChaosImpactIceMeshes::FMeshBuffers& Mesh, const FVector& Start, const FVector& End,
	const float Width, const FVector& Facing, FRandomStream& Stream, const int32 Forks)
{
	const FVector Span = End - Start;
	const float Length = static_cast<float>(Span.Size());
	if (Length < 1.0f)
	{
		return;
	}
	const FVector Along = Span / Length;
	const FVector Across = LightningSideAxis(Along, Facing);
	const int32 Segments = FMath::Clamp(FMath::RoundToInt(Length / 30.0f), 3, 20);
	TArray<FVector> Points;
	Points.Reserve(Segments + 1);
	float Offset = 0.0f;
	for (int32 Index = 0; Index <= Segments; ++Index)
	{
		const float Alpha = static_cast<float>(Index) / Segments;
		// Wanders sideways at random, pinned at both ends.
		Offset = FMath::Clamp(Offset + Stream.FRandRange(-1.0f, 1.0f) * Length * 0.08f, -Length * 0.15f, Length * 0.15f);
		Points.Add(Start + Span * Alpha + Across * Offset * FMath::Sin(Alpha * UE_PI));
	}
	AppendLightningRibbon(Mesh, Points, Width, Width * 0.3f, Facing);
	for (int32 Fork = 0; Fork < Forks; ++Fork)
	{
		const int32 From = Stream.RandRange(1, FMath::Max(1, Segments - 2));
		const float Remaining = Length * (1.0f - static_cast<float>(From) / Segments);
		const float Angle = FMath::DegreesToRadians(Stream.FRandRange(22.0f, 52.0f)) * (Stream.FRand() < 0.5f ? -1.0f : 1.0f);
		const FVector Direction = (Along * FMath::Cos(Angle) + Across * FMath::Sin(Angle)).GetSafeNormal();
		AppendBolt(Mesh, Points[From], Points[From] + Direction * Remaining * Stream.FRandRange(0.3f, 0.55f),
			Width * 0.5f, Facing, Stream, 0);
	}
}

void ChaosImpactLightning::AppendRing(ChaosImpactIceMeshes::FMeshBuffers& Mesh, const FVector& Center, const float Radius,
	const float Width, const int32 Segments)
{
	if (Radius < 1.0f || Width < 0.5f || Segments < 3)
	{
		return;
	}
	TArray<FVector> Points;
	Points.Reserve(Segments + 1);
	for (int32 Index = 0; Index <= Segments; ++Index)
	{
		const float Angle = Index * UE_TWO_PI / Segments;
		Points.Add(Center + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.0f));
	}
	AppendLightningRibbon(Mesh, Points, Width, Width, FVector::UpVector);
}

UProceduralMeshComponent* ChaosImpactLightning::CreateComponent(AActor* Owner, USceneComponent* Parent,
	UMaterialInterface* Material)
{
	if (!Owner)
	{
		return nullptr;
	}
	UProceduralMeshComponent* Component = NewObject<UProceduralMeshComponent>(Owner);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetCastShadow(false);
	Component->bUseComplexAsSimpleCollision = false;
	if (Parent)
	{
		Component->SetupAttachment(Parent);
	}
	Component->RegisterComponent();
	Component->SetMaterial(0, Material);
	return Component;
}

void ChaosImpactLightning::SetMesh(UProceduralMeshComponent* Component, const ChaosImpactIceMeshes::FMeshBuffers& Mesh)
{
	if (!Component)
	{
		return;
	}
	Component->ClearAllMeshSections();
	if (!Mesh.Vertices.IsEmpty())
	{
		Component->CreateMeshSection(0, Mesh.Vertices, Mesh.Triangles, Mesh.Normals, Mesh.UVs, TArray<FColor>(),
			TArray<FProcMeshTangent>(), false);
	}
}

FVector ChaosImpactLightning::GetViewDirection(const UWorld* World)
{
	if (const APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
		PlayerController && PlayerController->PlayerCameraManager)
	{
		return PlayerController->PlayerCameraManager->GetCameraRotation().Vector();
	}
	return FVector(0.5f, 0.0f, -0.86f).GetSafeNormal();
}
