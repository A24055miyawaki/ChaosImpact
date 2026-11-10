#include "ChaosImpactSuddenDeathZone.h"

#include "ChaosImpactBallTypes.h"
#include "ChaosImpactIceMeshes.h"
#include "ChaosImpactLightning.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "ProceduralMeshComponent.h"

AChaosImpactSuddenDeathZone::AChaosImpactSuddenDeathZone()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(false);
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

void AChaosImpactSuddenDeathZone::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AChaosImpactSuddenDeathZone, HalfExtent);
	DOREPLIFETIME(AChaosImpactSuddenDeathZone, StartAt);
	DOREPLIFETIME(AChaosImpactSuddenDeathZone, TotalSeconds);
}

void AChaosImpactSuddenDeathZone::Begin(const float InHalfExtent, const double InStartAt, const float InTotalSeconds)
{
	HalfExtent = FMath::Max(InHalfExtent, 100.0f);
	StartAt = InStartAt;
	TotalSeconds = FMath::Max(InTotalSeconds, DelaySeconds + SweepSeconds + 1.0f);
	ForceNetUpdate();
}

double AChaosImpactSuddenDeathZone::ServerNow() const
{
	const AGameStateBase* State = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	return State ? State->GetServerWorldTimeSeconds() : 0.0;
}

float AChaosImpactSuddenDeathZone::GetRadius() const
{
	const float Age = static_cast<float>(ServerNow() - StartAt);
	const float Corners = HalfExtent * CornerReach;
	if (Age <= DelaySeconds)
	{
		return Corners;
	}
	if (Age <= DelaySeconds + SweepSeconds)
	{
		// From past the corners to the square's sides, quickly.
		const float T = (Age - DelaySeconds) / SweepSeconds;
		return FMath::Lerp(Corners, HalfExtent, T);
	}
	// Then steadily to nothing a moment before the bell.
	const float Rest = FMath::Max(TotalSeconds - DelaySeconds - SweepSeconds - 1.0f, 1.0f);
	const float T = FMath::Clamp((Age - DelaySeconds - SweepSeconds) / Rest, 0.0f, 1.0f);
	return HalfExtent * (1.0f - T);
}

bool AChaosImpactSuddenDeathZone::IsInside(const FVector& Location) const
{
	return FVector::Dist2D(Location, GetActorLocation()) <= GetRadius();
}

void AChaosImpactSuddenDeathZone::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	if (!Edge)
	{
		EdgeMaterial = ChaosImpactBallTypes::MakeAdditive(this, FLinearColor(1.0f, 0.32f, 0.12f), 3.0f);
		OutsideMaterial = ChaosImpactBallTypes::MakeAdditive(this, FLinearColor(0.9f, 0.08f, 0.06f), 0.25f);
		Edge = ChaosImpactLightning::CreateComponent(this, Root, EdgeMaterial);
		Outside = ChaosImpactLightning::CreateComponent(this, Root, OutsideMaterial);
	}
	const float Radius = GetRadius();
	const float Pulse = 0.5f + 0.5f * FMath::Sin(static_cast<float>(ServerNow()) * 6.0f);
	// The edge: a thin line on the floor that pulses, and a narrow red band just outside it (both just above the floor).
	ChaosImpactIceMeshes::FMeshBuffers Line;
	ChaosImpactLightning::AppendRing(Line, FVector(0.0f, 0.0f, 6.0f), FMath::Max(Radius, 1.0f), 26.0f, 128);
	ChaosImpactLightning::SetMesh(Edge, Line);
	// (Only a narrow band just outside the line, so the floor stays in view everywhere.)
	ChaosImpactIceMeshes::FMeshBuffers Wash;
	constexpr float Band = 100.0f;
	ChaosImpactLightning::AppendRing(Wash, FVector(0.0f, 0.0f, 4.0f), FMath::Max(Radius, 1.0f) + 13.0f + Band * 0.5f, Band, 128);
	ChaosImpactLightning::SetMesh(Outside, Wash);
	if (EdgeMaterial)
	{
		EdgeMaterial->SetScalarParameterValue(TEXT("Intensity"), 2.2f + 2.0f * Pulse);
	}
}
