#include "ChaosImpactPrisonBall.h"
#include "ChaosImpactBallTypes.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactPlayerController.h"
#include "ProceduralMeshComponent.h"
#include "ChaosImpactIceMeshes.h"

bool ChaosImpactBallTypes::IsPrisonMode(const UWorld* World)
{
	if (!World || World->URL.HasOption(TEXT("CISolo=1"))
		|| World->URL.HasOption(TEXT("CIRPGPrototype=1"))
		|| World->GetMapName().Contains(TEXT("RPGPrototype"))) { return false; }
	if (const auto* Match = World->GetGameState<AChaosImpactGameState>(); Match && Match->bVersusMatch)
	{
		return Match->Phase == EChaosImpactOnlinePhase::Match;
	}
	const auto* PC = Cast<AChaosImpactPlayerController>(World->GetFirstPlayerController());
	return PC && PC->IsTrainingMode() && PC->ShowsTrainingPauseEntries();
}

UProceduralMeshComponent* ChaosImpactPrison::CreateCage(AActor* Owner, USceneComponent* Parent)
{
	if (!Owner || Owner->GetNetMode() == NM_DedicatedServer) { return nullptr; }
	auto* Mesh = NewObject<UProceduralMeshComponent>(Owner);
	Mesh->SetupAttachment(Parent);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
	Mesh->SetMaterial(0, ChaosImpactBallTypes::MakeAdditive(Owner, ChaosImpactBallTypes::GetColor(EChaosImpactBallType::Prison), 2.2f));
	Mesh->RegisterComponent();
	return Mesh;
}

void ChaosImpactPrison::UpdateCage(UProceduralMeshComponent* Mesh, float Radius, float Remaining, float Time, bool bCaptured)
{
	if (!Mesh) { return; }
	ChaosImpactIceMeshes::FMeshBuffers B;
	const auto Ribbon = [&B](const FVector& A, const FVector& C, const FVector& Across)
	{
		const int32 I = B.Vertices.Num();
		B.Vertices.Append({A-Across, A+Across, C+Across, C-Across});
		B.Triangles.Append({I,I+1,I+2,I,I+2,I+3,I+2,I+1,I,I+3,I+2,I});
		for (int32 N=0; N<4; ++N) { B.Normals.Add(FVector::UpVector); B.UVs.Add(FVector2D(N%2,N/2)); }
	};
	// Three broken, orthogonal hoops form a cage, not an opaque sphere hiding the character.
	for (int32 Axis=0; Axis<3; ++Axis)
	{
		const FQuat Tilt = Axis==0 ? FQuat::Identity : FQuat(Axis==1 ? FVector::ForwardVector : FVector::RightVector, PI*.5f);
		for (int32 I=0; I<64; ++I)
		{
			if (I%16==0 || I%16==15) { continue; }
			const float A=I*2*PI/64, C=(I+1)*2*PI/64;
			Ribbon(Tilt.RotateVector(FVector(FMath::Cos(A)*Radius,FMath::Sin(A)*Radius,0)),
				Tilt.RotateVector(FVector(FMath::Cos(C)*Radius,FMath::Sin(C)*Radius,0)), Tilt.RotateVector(FVector(0,0,1.1f)));
		}
	}
	if (bCaptured)
	{
		// A separate segmented clock, readable from a top-down camera even when carried.
		const int32 Segments = FMath::CeilToInt(FMath::Clamp(Remaining/ChaosImpactBallTypes::PrisonSeconds,0.f,1.f)*48);
		for (int32 I=0; I<Segments; ++I)
		{
			const float A=-PI*.5f+I*2*PI/48, C=A+2*PI/48*.78f;
			const FVector Radial(FMath::Cos(A),FMath::Sin(A),0);
			Ribbon(Radial*(Radius+9)+FVector(0,0,Radius*.35f),
				FVector(FMath::Cos(C)*(Radius+9),FMath::Sin(C)*(Radius+9),Radius*.35f), Radial*2.3f);
		}
		if (Remaining<1)
		{
			for (int32 I=0; I<6; ++I)
			{
				const float A=I*PI/3;
				const FVector R(FMath::Cos(A),FMath::Sin(A),0), T(-R.Y,R.X,0);
				const FVector P=R*Radius*.72f+FVector::UpVector*Radius*.65f;
				Ribbon(P,P+T*Radius*.24f-FVector::UpVector*Radius*.2f,T*.9f);
				Ribbon(P+T*Radius*.24f-FVector::UpVector*Radius*.2f,P+T*Radius*.15f-FVector::UpVector*Radius*.45f,R*.9f);
			}
		}
	}
	Mesh->CreateMeshSection(0,B.Vertices,B.Triangles,B.Normals,B.UVs,TArray<FColor>(),TArray<FProcMeshTangent>(),false);
	Mesh->SetRelativeRotation(FRotator(0,bCaptured ? 0 : Time*36,0));
}
