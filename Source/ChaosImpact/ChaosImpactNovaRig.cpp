#include "ChaosImpactNovaRig.h"

#include "ChaosImpactBallTypes.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{
#include "ChaosImpactNovaRigData.inl"

	const FChaosImpactNovaRigData& DataOf(const EChaosImpactNovaRig Rig)
	{
		static_assert(UE_ARRAY_COUNT(NovaRigs) == 5, "One entry per EChaosImpactNovaRig, in order");
		return NovaRigs[FMath::Clamp(static_cast<int32>(Rig), 0, 4)];
	}

	const TCHAR* const Textures[] =
	{
		TEXT("/Game/ChaosImpact/Nova/Textures/T_Nova.T_Nova"), TEXT("/Game/ChaosImpact/Nova/Textures/T_Nova.T_Nova"),
		TEXT("/Game/ChaosImpact/Nova/Textures/T_Gear.T_Gear"), TEXT("/Game/ChaosImpact/Nova/Textures/T_Ring.T_Ring"),
		TEXT("/Game/ChaosImpact/Nova/Textures/T_Core.T_Core")
	};
	const TCHAR* const ExplosionMaterial = TEXT("/Game/ChaosImpact/Nova/M_CI_NovaExplosion.M_CI_NovaExplosion");
	/** Shiny metal with the texture's cyan lines glowing (BodyTexture, Metallic, Roughness, GlowStrength). */
	const TCHAR* const MetalMaterial = TEXT("/Game/ChaosImpact/Nova/M_CI_NovaMetal.M_CI_NovaMetal");

	float EvaluateKeys(const FChaosImpactNovaKey* Keys, const int32 Count, const float Time)
	{
		if (Count <= 0)
		{
			return 0.0f;
		}
		if (Time <= Keys[0].Time)
		{
			return Keys[0].Value;
		}
		for (int32 Index = 0; Index + 1 < Count; ++Index)
		{
			const FChaosImpactNovaKey& A = Keys[Index];
			const FChaosImpactNovaKey& B = Keys[Index + 1];
			if (Time > B.Time)
			{
				continue;
			}
			const float Span = B.Time - A.Time;
			if (A.Interp == 0 || Span <= KINDA_SMALL_NUMBER)
			{
				return A.Value;
			}
			const float U = (Time - A.Time) / Span;
			if (A.Interp == 1)
			{
				return FMath::Lerp(A.Value, B.Value, U);
			}
			// Hermite, the slopes per second scaled to the span.
			const float U2 = U * U;
			const float U3 = U2 * U;
			return (2.0f * U3 - 3.0f * U2 + 1.0f) * A.Value + (U3 - 2.0f * U2 + U) * A.RightSlope * Span
				+ (-2.0f * U3 + 3.0f * U2) * B.Value + (U3 - U2) * A.NextLeftSlope * Span;
		}
		return Keys[Count - 1].Value;
	}

	/** 4x4, column vectors (p' = M p), doubles: the source's way, converted to Unreal's at the end. */
	struct FM4
	{
		double M[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
		static FM4 Translate(const FVector3f& V)
		{
			FM4 R;
			R.M[0][3] = V.X; R.M[1][3] = V.Y; R.M[2][3] = V.Z;
			return R;
		}
		static FM4 Scale(const FVector3f& V)
		{
			FM4 R;
			R.M[0][0] = V.X; R.M[1][1] = V.Y; R.M[2][2] = V.Z;
			return R;
		}
		/** Maya's XYZ order: X first, then Y, then Z (R = Rz Ry Rx). */
		static FM4 RotateXYZ(const FVector3f& Degrees)
		{
			const double X = FMath::DegreesToRadians(static_cast<double>(Degrees.X));
			const double Y = FMath::DegreesToRadians(static_cast<double>(Degrees.Y));
			const double Z = FMath::DegreesToRadians(static_cast<double>(Degrees.Z));
			FM4 RX, RY, RZ;
			RX.M[1][1] = FMath::Cos(X); RX.M[1][2] = -FMath::Sin(X); RX.M[2][1] = FMath::Sin(X); RX.M[2][2] = FMath::Cos(X);
			RY.M[0][0] = FMath::Cos(Y); RY.M[0][2] = FMath::Sin(Y); RY.M[2][0] = -FMath::Sin(Y); RY.M[2][2] = FMath::Cos(Y);
			RZ.M[0][0] = FMath::Cos(Z); RZ.M[0][1] = -FMath::Sin(Z); RZ.M[1][0] = FMath::Sin(Z); RZ.M[1][1] = FMath::Cos(Z);
			return RZ * RY * RX;
		}
		FM4 operator*(const FM4& O) const
		{
			FM4 R;
			for (int32 I = 0; I < 4; ++I)
			{
				for (int32 J = 0; J < 4; ++J)
				{
					R.M[I][J] = M[I][0] * O.M[0][J] + M[I][1] * O.M[1][J] + M[I][2] * O.M[2][J] + M[I][3] * O.M[3][J];
				}
			}
			return R;
		}
	};

	/**
	 * The meshes were imported with the source's (x, y, z) as Unreal's (x, -z, y). The same change of axes turns a
	 * transform from the source's space into Unreal's: P M P^T.
	 */
	FTransform ToUnreal(const FM4& Source, bool& bOutZeroScale)
	{
		FM4 P;
		P.M[1][1] = 0; P.M[1][2] = -1; P.M[2][1] = 1; P.M[2][2] = 0;
		FM4 PT = P;
		PT.M[1][2] = 1; PT.M[2][1] = -1;
		const FM4 U = P * Source * PT;
		// Unreal's matrices are the transpose (row vectors), the translation in the bottom row.
		FMatrix Out;
		for (int32 I = 0; I < 4; ++I)
		{
			for (int32 J = 0; J < 4; ++J)
			{
				Out.M[J][I] = U.M[I][J];
			}
		}
		bOutZeroScale = FMath::Abs(Out.Determinant()) < 1e-9;
		if (bOutZeroScale)
		{
			return FTransform(Out.GetOrigin());
		}
		FTransform Result;
		Result.SetFromMatrix(Out);
		return Result;
	}

	struct FPose
	{
		FVector3f T, R, S;
		float Visible;
	};

	FPose PoseOf(const FChaosImpactNovaRigData& Data, const int32 Node, const float Time)
	{
		const FChaosImpactNovaNode& N = Data.Nodes[Node];
		FPose Pose{N.T, N.R, N.S, N.Visibility};
		for (int32 Index = 0; Index < Data.NumCurves; ++Index)
		{
			const FChaosImpactNovaCurve& Curve = Data.Curves[Index];
			if (Curve.Node != Node || !Curve.Keys)
			{
				continue;
			}
			const float Value = EvaluateKeys(Curve.Keys, Curve.NumKeys, Time);
			switch (Curve.Channel)
			{
			case 'T': Pose.T[Curve.Axis] = Value; break;
			case 'R': Pose.R[Curve.Axis] = Value; break;
			case 'S': Pose.S[Curve.Axis] = Value; break;
			default: Pose.Visible = Value; break;
			}
		}
		return Pose;
	}
}

float ChaosImpactNova::Evaluate(const EChaosImpactNovaRig Rig, const int32 Node, const TCHAR Channel, const int32 Axis, const float Time,
	const float Default)
{
	const FChaosImpactNovaRigData& Data = DataOf(Rig);
	for (int32 Index = 0; Index < Data.NumCurves; ++Index)
	{
		const FChaosImpactNovaCurve& Curve = Data.Curves[Index];
		if (Curve.Node == Node && Curve.Channel == Channel && Curve.Axis == Axis && Curve.Keys)
		{
			return EvaluateKeys(Curve.Keys, Curve.NumKeys, Time);
		}
	}
	return Default;
}

float ChaosImpactNova::GetExplosionSize(const float AnimTime)
{
	static const int32 Bomb = FindNode(EChaosImpactNovaRig::NovaFire, TEXT("Bomb"));
	constexpr float Largest = 1800.0f;
	const float Scale = Evaluate(EChaosImpactNovaRig::NovaFire, Bomb, TEXT('S'), 0, AnimTime, 1.0f);
	return FMath::Clamp((Scale - 1.0f) / (Largest - 1.0f), 0.0f, 1.0f);
}

int32 ChaosImpactNova::FindNode(const EChaosImpactNovaRig Rig, const TCHAR* Name)
{
	const FChaosImpactNovaRigData& Data = DataOf(Rig);
	for (int32 Index = 0; Index < Data.NumNodes; ++Index)
	{
		if (FCString::Strcmp(Data.Nodes[Index].Name, Name) == 0)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

int32 ChaosImpactNova::GetNodeCount(const EChaosImpactNovaRig Rig)
{
	return DataOf(Rig).NumNodes;
}

FString ChaosImpactNova::GetMeshPath(const EChaosImpactNovaRig Rig, const int32 Node)
{
	const FChaosImpactNovaRigData& Data = DataOf(Rig);
	if (Node < 0 || Node >= Data.NumNodes || !Data.Nodes[Node].Mesh[0])
	{
		return FString();
	}
	return FString::Printf(TEXT("/Game/ChaosImpact/Nova/Meshes/%s.%s"), Data.Nodes[Node].Mesh, Data.Nodes[Node].Mesh);
}

UMaterialInstanceDynamic* ChaosImpactNova::MakeMaterial(UObject* Outer, const EChaosImpactNovaRig Rig)
{
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, MetalMaterial);
	UTexture* Texture = LoadObject<UTexture>(nullptr, Textures[FMath::Clamp(static_cast<int32>(Rig), 0, 4)]);
	UMaterialInstanceDynamic* Look = Base ? UMaterialInstanceDynamic::Create(Base, Outer) : nullptr;
	if (!Look)
	{
		return nullptr;
	}
	if (Texture)
	{
		Look->SetTextureParameterValue(TEXT("BodyTexture"), Texture);
	}
	// A polished shine, a little different for each: bronze gears, silver rings, a glassy black core.
	float Metallic = 0.85f;
	float Roughness = 0.24f;
	switch (Rig)
	{
	case EChaosImpactNovaRig::Gear: Metallic = 0.95f; Roughness = 0.28f; break;
	case EChaosImpactNovaRig::Ring: Metallic = 1.0f; Roughness = 0.16f; break;
	case EChaosImpactNovaRig::Core: Metallic = 0.55f; Roughness = 0.12f; break;
	default: break;
	}
	Look->SetScalarParameterValue(TEXT("Metallic"), Metallic);
	Look->SetScalarParameterValue(TEXT("Roughness"), Roughness);
	return Look;
}

UMaterialInstanceDynamic* ChaosImpactNova::MakeExplosionMaterial(UObject* Outer)
{
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, ExplosionMaterial);
	return Base ? UMaterialInstanceDynamic::Create(Base, Outer) : nullptr;
}

void ChaosImpactNova::GetAssetPaths(TArray<FSoftObjectPath>& OutPaths)
{
	for (int32 Rig = 0; Rig < 5; ++Rig)
	{
		for (int32 Node = 0; Node < GetNodeCount(static_cast<EChaosImpactNovaRig>(Rig)); ++Node)
		{
			const FString Path = GetMeshPath(static_cast<EChaosImpactNovaRig>(Rig), Node);
			if (!Path.IsEmpty())
			{
				OutPaths.AddUnique(FSoftObjectPath(Path));
			}
		}
	}
	for (const TCHAR* Texture : Textures)
	{
		OutPaths.AddUnique(FSoftObjectPath(Texture));
	}
	OutPaths.AddUnique(FSoftObjectPath(ExplosionMaterial));
	OutPaths.AddUnique(FSoftObjectPath(MetalMaterial));
}

void FChaosImpactNovaRig::Build(AActor* Owner, USceneComponent* Parent, const EChaosImpactNovaRig InRig)
{
	if (!Owner || !Parent || IsBuilt())
	{
		return;
	}
	Rig = InRig;
	const FChaosImpactNovaRigData& Data = DataOf(Rig);
	USceneComponent* NewRoot = NewObject<USceneComponent>(Owner);
	NewRoot->SetupAttachment(Parent);
	NewRoot->RegisterComponent();
	Root = NewRoot;
	UMaterialInstanceDynamic* Material = ChaosImpactNova::MakeMaterial(Owner, Rig);
	Look = Material;
	Parts.SetNum(Data.NumNodes);
	ExtraRotation.Init(FVector3f::ZeroVector, Data.NumNodes);
	ForcedHidden.Init(false, Data.NumNodes);
	for (int32 Node = 0; Node < Data.NumNodes; ++Node)
	{
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *ChaosImpactNova::GetMeshPath(Rig, Node));
		if (!Mesh)
		{
			continue;
		}
		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(Owner);
		Part->SetStaticMesh(Mesh);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetGenerateOverlapEvents(false);
		Part->SetupAttachment(NewRoot);
		for (int32 Slot = 0; Slot < Part->GetNumMaterials(); ++Slot)
		{
			Part->SetMaterial(Slot, Material);
		}
		Part->RegisterComponent();
		Parts[Node] = Part;
	}
	Pose(0.0f);
}

UStaticMeshComponent* FChaosImpactNovaRig::GetPart(const int32 Node) const
{
	return Parts.IsValidIndex(Node) ? Parts[Node].Get() : nullptr;
}

int32 FChaosImpactNovaRig::FindNode(const TCHAR* Name) const
{
	return ChaosImpactNova::FindNode(Rig, Name);
}

void FChaosImpactNovaRig::SetExtraRotation(const int32 Node, const FVector3f& Degrees)
{
	if (ExtraRotation.IsValidIndex(Node))
	{
		ExtraRotation[Node] = Degrees;
	}
}

void FChaosImpactNovaRig::SetForcedHidden(const int32 Node, const bool bHidden)
{
	if (ForcedHidden.IsValidIndex(Node))
	{
		ForcedHidden[Node] = bHidden;
	}
}

void FChaosImpactNovaRig::SetFade(const float InFade)
{
	const float NewFade = FMath::Clamp(InFade, 0.0f, 1.0f);
	if (NewFade == Fade)
	{
		return;
	}
	Fade = NewFade;
	if (UMaterialInstanceDynamic* Material = Look.Get())
	{
		Material->SetScalarParameterValue(TEXT("Fade"), Fade);
	}
}

void FChaosImpactNovaRig::SetVisible(const bool bInVisible)
{
	bVisible = bInVisible;
	if (USceneComponent* RootComponent = Root.Get())
	{
		RootComponent->SetVisibility(bInVisible, true);
	}
}

void FChaosImpactNovaRig::Pose(const float Time)
{
	if (!IsBuilt())
	{
		return;
	}
	const FChaosImpactNovaRigData& Data = DataOf(Rig);
	// Each part's place within the rig: its own transform (about its pivots) under its parents'.
	TArray<FM4, TInlineAllocator<32>> World;
	TArray<float, TInlineAllocator<32>> Visible;
	World.SetNum(Data.NumNodes);
	Visible.SetNum(Data.NumNodes);
	for (int32 Node = 0; Node < Data.NumNodes; ++Node)
	{
		const FChaosImpactNovaNode& N = Data.Nodes[Node];
		const FPose P = PoseOf(Data, Node, Time);
		const FM4 Local = FM4::Translate(P.T) * FM4::Translate(N.RotationPivot) * FM4::RotateXYZ(P.R + ExtraRotation[Node])
			* FM4::Translate(-N.RotationPivot) * FM4::Translate(N.ScalingPivot) * FM4::Scale(P.S) * FM4::Translate(-N.ScalingPivot);
		// Parents come first in the source's order except for the odd one: walk up rather than rely on it.
		FM4 Chain = Local;
		float Shown = P.Visible;
		for (int32 Up = N.Parent; Up >= 0; Up = Data.Nodes[Up].Parent)
		{
			const FChaosImpactNovaNode& UpNode = Data.Nodes[Up];
			const FPose UpPose = PoseOf(Data, Up, Time);
			const FM4 UpLocal = FM4::Translate(UpPose.T) * FM4::Translate(UpNode.RotationPivot)
				* FM4::RotateXYZ(UpPose.R + ExtraRotation[Up]) * FM4::Translate(-UpNode.RotationPivot)
				* FM4::Translate(UpNode.ScalingPivot) * FM4::Scale(UpPose.S) * FM4::Translate(-UpNode.ScalingPivot);
			Chain = UpLocal * Chain;
			Shown = FMath::Min(Shown, UpPose.Visible);
		}
		World[Node] = Chain;
		Visible[Node] = Shown;
	}
	for (int32 Node = 0; Node < Data.NumNodes; ++Node)
	{
		UStaticMeshComponent* Part = Parts[Node].Get();
		if (!Part)
		{
			continue;
		}
		bool bZeroScale = false;
		const FTransform Placed = ToUnreal(World[Node], bZeroScale);
		const bool bShow = bVisible && Fade > 0.0f && !ForcedHidden[Node] && !bZeroScale && Visible[Node] > 0.5f;
		if (bShow)
		{
			Part->SetRelativeTransform(Placed);
		}
		if (Part->IsVisible() != bShow)
		{
			Part->SetVisibility(bShow);
		}
	}
}
