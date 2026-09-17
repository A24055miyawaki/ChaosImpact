#include "ChaosImpactPuppetComponent.h"

#include "ChaosImpact.h"
#include "ChaosImpactCharacterRoster.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "AnimationRuntime.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{
	// The source skeleton is the template mannequin; the model's own joints come from its Maya rig.
	const TCHAR* const SourceArmJoints[2][3] = {{TEXT("upperarm_l"), TEXT("lowerarm_l"), TEXT("hand_l")},
		{TEXT("upperarm_r"), TEXT("lowerarm_r"), TEXT("hand_r")}};
	const TCHAR* const SourceLegJoints[2][3] = {{TEXT("thigh_l"), TEXT("calf_l"), TEXT("foot_l")},
		{TEXT("thigh_r"), TEXT("calf_r"), TEXT("foot_r")}};
	const TCHAR* const ModelArmJoints[3] = {TEXT("Arm"), TEXT("ForeArm"), TEXT("Hand")};
	const TCHAR* const ModelLegJoints[3] = {TEXT("UpLeg"), TEXT("Leg"), TEXT("Foot")};
	/** From the hand joint to the middle of the palm, along the forearm, in model units. */
	constexpr float PalmDistance = 9.0f;

	/** Skinned models: the HumanIK joints that follow the template mannequin's, parents first. */
	struct FBoneMatch
	{
		const TCHAR* Model;
		const TCHAR* Source;
	};
	const FBoneMatch SkinnedBodyBones[] = {
		{TEXT("Character1_Hips"), TEXT("pelvis")},
		{TEXT("Character1_Spine"), TEXT("spine_01")},
		{TEXT("Character1_Spine1"), TEXT("spine_03")},
		{TEXT("Character1_Spine2"), TEXT("spine_05")},
		{TEXT("Character1_Neck"), TEXT("neck_01")},
		{TEXT("Character1_Head"), TEXT("head")},
		{TEXT("Character1_LeftShoulder"), TEXT("clavicle_l")},
		{TEXT("Character1_RightShoulder"), TEXT("clavicle_r")},
	};

	/** Kind is SM, SK, M or T; Part completes the name after the character's prefix. */
	FString AssetPath(const int32 CharacterIndex, const TCHAR* Kind, const TCHAR* Part)
	{
		const FChaosImpactCharacterInfo& Info = ChaosImpactRoster::Get(CharacterIndex);
		const FString Name = FCString::Strcmp(Kind, TEXT("M")) == 0 ? FString::Printf(TEXT("M_CI_%s"), Info.AssetPrefix)
			: *Part == 0 ? FString::Printf(TEXT("%s_%s"), Kind, Info.AssetPrefix)
			: FString::Printf(TEXT("%s_%s_%s"), Kind, Info.AssetPrefix, Part);
		return FString::Printf(TEXT("%s/%s.%s"), Info.AssetFolder, *Name, *Name);
	}
}

UChaosImpactPuppetComponent::UChaosImpactPuppetComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// After the source skeleton has finished animating this frame.
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	bAutoActivate = true;
}

UPoseableMeshComponent* UChaosImpactPuppetComponent::CreateLimb(const TCHAR* AssetName)
{
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *AssetPath(CharacterIndex, TEXT("SK"), AssetName));
	if (!Mesh || !GetOwner())
	{
		UE_LOG(LogChaosImpact, Warning, TEXT("Character model: missing limb %s"), AssetName);
		return nullptr;
	}
	UPoseableMeshComponent* Limb = CreatePoseable(Mesh);
	for (int32 Slot = 0; Slot < Limb->GetNumMaterials(); ++Slot)
	{
		Limb->SetMaterial(Slot, Material);
	}
	return Limb;
}

UPoseableMeshComponent* UChaosImpactPuppetComponent::CreatePoseable(USkeletalMesh* Mesh)
{
	UPoseableMeshComponent* Limb = NewObject<UPoseableMeshComponent>(GetOwner());
	Limb->SetupAttachment(this);
	Limb->SetSkinnedAssetAndUpdate(Mesh);
	Limb->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Limb->SetGenerateOverlapEvents(false);
	Limb->RegisterComponent();
	return Limb;
}

FVector UChaosImpactPuppetComponent::SourceLocation(const int32 BoneIndex) const
{
	const TArray<FTransform>& Pose = Source->GetComponentSpaceTransforms();
	if (!Pose.IsValidIndex(BoneIndex))
	{
		return FVector::ZeroVector;
	}
	return GetComponentTransform().InverseTransformPosition(
		Source->GetComponentTransform().TransformPosition(Pose[BoneIndex].GetLocation()));
}

FVector UChaosImpactPuppetComponent::SourceReferenceLocation(const int32 BoneIndex) const
{
	const USkinnedAsset* Asset = Source ? Source->GetSkinnedAsset() : nullptr;
	if (!Asset || BoneIndex == INDEX_NONE)
	{
		return FVector::ZeroVector;
	}
	const FTransform Reference = FAnimationRuntime::GetComponentSpaceTransformRefPose(Asset->GetRefSkeleton(), BoneIndex);
	return GetComponentTransform().InverseTransformPosition(
		Source->GetComponentTransform().TransformPosition(Reference.GetLocation()));
}

FQuat UChaosImpactPuppetComponent::SourceRotation(const int32 BoneIndex) const
{
	const TArray<FTransform>& Pose = Source->GetComponentSpaceTransforms();
	if (!Pose.IsValidIndex(BoneIndex))
	{
		return FQuat::Identity;
	}
	return GetComponentQuat().Inverse() * Source->GetComponentQuat() * Pose[BoneIndex].GetRotation();
}

FQuat UChaosImpactPuppetComponent::SourceReferenceRotation(const int32 BoneIndex) const
{
	const USkinnedAsset* Asset = Source ? Source->GetSkinnedAsset() : nullptr;
	if (!Asset || BoneIndex == INDEX_NONE)
	{
		return FQuat::Identity;
	}
	return GetComponentQuat().Inverse() * Source->GetComponentQuat()
		* FAnimationRuntime::GetComponentSpaceTransformRefPose(Asset->GetRefSkeleton(), BoneIndex).GetRotation();
}

bool UChaosImpactPuppetComponent::SetUpChain(FLimbChain& Chain, UPoseableMeshComponent* Limb, const TCHAR* JointPrefix,
	const bool bArm, const bool bLeft)
{
	if (!Limb)
	{
		return false;
	}
	for (int32 Joint = 0; Joint < 3; ++Joint)
	{
		Chain.Joints[Joint] = FName(FString::Printf(TEXT("Character1_%s%s"), JointPrefix,
			bArm ? ModelArmJoints[Joint] : ModelLegJoints[Joint]));
		if (Limb->GetBoneIndex(Chain.Joints[Joint]) == INDEX_NONE)
		{
			UE_LOG(LogChaosImpact, Warning, TEXT("Character model: joint %s not found"), *Chain.Joints[Joint].ToString());
			return false;
		}
		Chain.Reference[Joint] = Limb->GetBoneTransformByName(Chain.Joints[Joint], EBoneSpaces::ComponentSpace);
		const FName SourceName = bArm ? SourceArmJoints[bLeft ? 0 : 1][Joint] : SourceLegJoints[bLeft ? 0 : 1][Joint];
		Chain.SourceJoints[Joint] = Source->GetBoneIndex(SourceName);
		if (Chain.SourceJoints[Joint] == INDEX_NONE)
		{
			UE_LOG(LogChaosImpact, Warning, TEXT("Character model: source bone %s not found"), *SourceName.ToString());
			return false;
		}
	}
	Chain.UpperLength = FVector::Dist(Chain.Reference[0].GetLocation(), Chain.Reference[1].GetLocation());
	Chain.LowerLength = FVector::Dist(Chain.Reference[1].GetLocation(), Chain.Reference[2].GetLocation());
	const float SourceLength = FVector::Dist(SourceReferenceLocation(Chain.SourceJoints[0]), SourceReferenceLocation(Chain.SourceJoints[1]))
		+ FVector::Dist(SourceReferenceLocation(Chain.SourceJoints[1]), SourceReferenceLocation(Chain.SourceJoints[2]));
	Chain.LengthRatio = SourceLength > 1.0f ? (Chain.UpperLength + Chain.LowerLength) / SourceLength : 1.0f;
	return Chain.UpperLength > 1.0f && Chain.LowerLength > 1.0f;
}

bool UChaosImpactPuppetComponent::Initialize(USkeletalMeshComponent* InSource, const int32 InCharacterIndex)
{
	bReady = false;
	Source = InSource;
	CharacterIndex = ChaosImpactRoster::ClampIndex(InCharacterIndex);
	UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, *AssetPath(CharacterIndex, TEXT("M"), TEXT("")));
	const EChaosImpactModelKind Kind = ChaosImpactRoster::Get(CharacterIndex).Kind;
	bRigid = Kind == EChaosImpactModelKind::Rigid;
	bSkinned = Kind == EChaosImpactModelKind::Skinned;
	ColourIndex = INDEX_NONE;
	if (bSkinned)
	{
		USkeletalMesh* SkinnedMesh = LoadObject<USkeletalMesh>(nullptr, *AssetPath(CharacterIndex, TEXT("SK"), TEXT("")));
		if (!Source || !Source->GetSkinnedAsset() || !Parent || !SkinnedMesh || !GetOwner())
		{
			UE_LOG(LogChaosImpact, Warning, TEXT("Character model: assets missing, keeping the template character"));
			return false;
		}
		Material = UMaterialInstanceDynamic::Create(Parent, this);
		ColourTextures.Reset();
		return InitializeSkinned(SkinnedMesh);
	}
	UStaticMesh* BodyMesh = LoadObject<UStaticMesh>(nullptr, *AssetPath(CharacterIndex, TEXT("SM"), TEXT("Body")));
	if (!Source || !Source->GetSkinnedAsset() || !Parent || !BodyMesh || !GetOwner())
	{
		UE_LOG(LogChaosImpact, Warning, TEXT("Character model: assets missing, keeping the template character"));
		return false;
	}
	Material = UMaterialInstanceDynamic::Create(Parent, this);
	ColourTextures.Reset();
	for (const TCHAR* Suffix : ChaosImpactRoster::ColourTextureSuffixes)
	{
		if (!bRigid)
		{
			ColourTextures.Add(LoadObject<UTexture>(nullptr, *AssetPath(CharacterIndex, TEXT("T"), Suffix)));
		}
	}

	Body = NewObject<UStaticMeshComponent>(GetOwner());
	Body->SetupAttachment(this);
	Body->SetStaticMesh(BodyMesh);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetGenerateOverlapEvents(false);
	Body->RegisterComponent();
	SlotMaterials.Reset();
	for (int32 Slot = 0; Slot < Body->GetNumMaterials(); ++Slot)
	{
		UMaterialInterface* Own = bRigid ? BodyMesh->GetMaterial(Slot) : nullptr;
		if (Own)
		{
			SlotMaterials.Add(Body->CreateDynamicMaterialInstance(Slot, Own));
		}
		else
		{
			Body->SetMaterial(Slot, Material);
		}
	}
	SourcePelvis = Source->GetBoneIndex(TEXT("pelvis"));
	SourceNeck = Source->GetBoneIndex(TEXT("neck_01"));
	if (bRigid)
	{
		if (SourcePelvis == INDEX_NONE || SourceNeck == INDEX_NONE)
		{
			UE_LOG(LogChaosImpact, Warning, TEXT("Character model: skeleton mismatch, keeping the template character"));
			OnComponentDestroyed(false);
			return false;
		}
		// Leans about its feet; nothing else to set up.
		BodyPivot = FVector::ZeroVector;
		AddTickPrerequisiteComponent(Source);
		SetColourIndex(0);
		bReady = true;
		UE_LOG(LogChaosImpact, Log, TEXT("Character model ready: rigid body %s"), *GetNameSafe(BodyMesh));
		return true;
	}
	ArmL = CreateLimb(TEXT("ArmL"));
	ArmR = CreateLimb(TEXT("ArmR"));
	LegL = CreateLimb(TEXT("LegL"));
	LegR = CreateLimb(TEXT("LegR"));

	const bool bChains = SetUpChain(ChainArmL, ArmL, TEXT("Left"), true, true)
		&& SetUpChain(ChainArmR, ArmR, TEXT("Right"), true, false)
		&& SetUpChain(ChainLegL, LegL, TEXT("Left"), false, true)
		&& SetUpChain(ChainLegR, LegR, TEXT("Right"), false, false);
	if (!bChains || SourcePelvis == INDEX_NONE || SourceNeck == INDEX_NONE)
	{
		UE_LOG(LogChaosImpact, Warning, TEXT("Character model: skeleton mismatch, keeping the template character"));
		OnComponentDestroyed(false);
		return false;
	}
	BodyPivot = (ChainLegL.Reference[0].GetLocation() + ChainLegR.Reference[0].GetLocation()) * 0.5f;
	const FBoxSphereBounds BodyBounds = BodyMesh->GetBounds();
	BodyRadius = FMath::Max(BodyBounds.BoxExtent.X, BodyBounds.BoxExtent.Y) + 4.0f;
	AddTickPrerequisiteComponent(Source);
	SetColourIndex(0);
	bReady = true;
	UE_LOG(LogChaosImpact, Log, TEXT("Character model ready: arm ratio %.2f, leg ratio %.2f, body radius %.0f"),
		ChainArmR.LengthRatio, ChainLegR.LengthRatio, BodyRadius);
	return true;
}

bool UChaosImpactPuppetComponent::InitializeSkinned(USkeletalMesh* Mesh)
{
	Skin = CreatePoseable(Mesh);
	SlotMaterials.Reset();
	for (int32 Slot = 0; Slot < Skin->GetNumMaterials(); ++Slot)
	{
		if (UMaterialInterface* Own = Skin->GetMaterial(Slot))
		{
			SlotMaterials.Add(Skin->CreateDynamicMaterialInstance(Slot, Own));
		}
	}
	const auto Fail = [this](const FString& Missing)
	{
		UE_LOG(LogChaosImpact, Warning, TEXT("Character model: %s not found, keeping the template character"), *Missing);
		OnComponentDestroyed(false);
		return false;
	};
	DrivenBody.Reset();
	for (const FBoneMatch& Match : SkinnedBodyBones)
	{
		FDrivenBone Bone;
		Bone.Name = Match.Model;
		Bone.Source = Source->GetBoneIndex(Match.Source);
		if (Skin->GetBoneIndex(Bone.Name) == INDEX_NONE || Bone.Source == INDEX_NONE)
		{
			return Fail(FString::Printf(TEXT("joint %s / %s"), Match.Model, Match.Source));
		}
		Bone.Reference = Skin->GetBoneTransformByName(Bone.Name, EBoneSpaces::ComponentSpace).GetRotation();
		Bone.SourceReference = SourceReferenceRotation(Bone.Source);
		DrivenBody.Add(Bone);
	}
	// The legs are one joint each (no knee or ankle): they swing toward where the source foot is.
	const FName LegNames[2] = {TEXT("Character1_LeftUpLeg"), TEXT("Character1_RightUpLeg")};
	for (int32 Side = 0; Side < 2; ++Side)
	{
		FDrivenBone& Leg = DrivenLegs[Side];
		Leg.Name = LegNames[Side];
		if (Skin->GetBoneIndex(Leg.Name) == INDEX_NONE)
		{
			return Fail(Leg.Name.ToString());
		}
		const FTransform LegReference = Skin->GetBoneTransformByName(Leg.Name, EBoneSpaces::ComponentSpace);
		Leg.Reference = LegReference.GetRotation();
		LegLengths[Side] = FMath::Max(10.0f, static_cast<float>(LegReference.GetLocation().Z));
	}
	ChainLegL.SourceJoints[0] = Source->GetBoneIndex(TEXT("thigh_l"));
	ChainLegL.SourceJoints[2] = Source->GetBoneIndex(TEXT("foot_l"));
	ChainLegR.SourceJoints[0] = Source->GetBoneIndex(TEXT("thigh_r"));
	ChainLegR.SourceJoints[2] = Source->GetBoneIndex(TEXT("foot_r"));
	if (ChainLegL.SourceJoints[0] == INDEX_NONE || ChainLegL.SourceJoints[2] == INDEX_NONE
		|| ChainLegR.SourceJoints[0] == INDEX_NONE || ChainLegR.SourceJoints[2] == INDEX_NONE)
	{
		return Fail(TEXT("source leg joints"));
	}
	if (!SetUpChain(ChainArmL, Skin, TEXT("Left"), true, true) || !SetUpChain(ChainArmR, Skin, TEXT("Right"), true, false))
	{
		return Fail(TEXT("arm joints"));
	}
	bSkinnedLegChains = true;
	for (const TCHAR* Joint : {TEXT("Character1_LeftLeg"), TEXT("Character1_LeftFoot"), TEXT("Character1_RightLeg"), TEXT("Character1_RightFoot")})
	{
		bSkinnedLegChains &= Skin->GetBoneIndex(Joint) != INDEX_NONE;
	}
	bSkinnedLegChains = bSkinnedLegChains && SetUpChain(ChainLegL, Skin, TEXT("Left"), false, true)
		&& SetUpChain(ChainLegR, Skin, TEXT("Right"), false, false);
	SourcePelvis = DrivenBody[0].Source;
	HipsReference = Skin->GetBoneTransformByName(DrivenBody[0].Name, EBoneSpaces::ComponentSpace).GetLocation();
	const float PelvisHeight = static_cast<float>(SourceReferenceLocation(SourcePelvis).Z);
	HeightRatio = PelvisHeight > 1.0f ? static_cast<float>(HipsReference.Z) / PelvisHeight : 1.0f;
	const FName Finger(TEXT("Character1_RightHandMiddle1"));
	PalmOffset = Skin->GetBoneIndex(Finger) != INDEX_NONE
		? 0.6f * static_cast<float>(FVector::Dist(Skin->GetBoneTransformByName(Finger, EBoneSpaces::ComponentSpace).GetLocation(),
			ChainArmR.Reference[2].GetLocation()))
		: PalmDistance;
	AddTickPrerequisiteComponent(Source);
	SetColourIndex(0);
	bReady = true;
	UE_LOG(LogChaosImpact, Log, TEXT("Character model ready: skinned %s, arm ratio %.2f, height ratio %.2f"),
		*GetNameSafe(Mesh), ChainArmR.LengthRatio, HeightRatio);
	return true;
}

void UChaosImpactPuppetComponent::OnComponentDestroyed(const bool bDestroyingHierarchy)
{
	// The parts belong to the owning actor, so they would outlive this component unless removed with it.
	bReady = false;
	for (USceneComponent* Part : TArray<USceneComponent*>{Body, ArmL, ArmR, LegL, LegR, Skin})
	{
		if (IsValid(Part) && !Part->IsBeingDestroyed())
		{
			Part->DestroyComponent();
		}
	}
	Skin = nullptr;
	Body = nullptr;
	ArmL = nullptr;
	ArmR = nullptr;
	LegL = nullptr;
	LegR = nullptr;
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

void UChaosImpactPuppetComponent::SetColourIndex(const int32 Index)
{
	const int32 Clamped = FMath::Clamp(Index, 0, ChaosImpactRoster::ColourCount - 1);
	if (bRigid || bSkinned)
	{
		if (Clamped != ColourIndex && Material)
		{
			ColourIndex = Clamped;
			const FLinearColor Swatch = ChaosImpactRoster::GetColourSwatch(Clamped);
			Material->SetVectorParameterValue(TEXT("BodyColor"), Swatch);
			for (UMaterialInstanceDynamic* SlotMaterial : SlotMaterials)
			{
				if (SlotMaterial)
				{
					SlotMaterial->SetVectorParameterValue(TEXT("BodyColor"), Swatch);
				}
			}
		}
		return;
	}
	if (Clamped == ColourIndex || !Material || !ColourTextures.IsValidIndex(Clamped) || !ColourTextures[Clamped])
	{
		return;
	}
	ColourIndex = Clamped;
	Material->SetTextureParameterValue(TEXT("BodyTexture"), ColourTextures[Clamped]);
	UE_LOG(LogChaosImpact, Verbose, TEXT("Character model colour %d (%s) on %s"), Clamped,
		*GetNameSafe(ColourTextures[Clamped]), *GetNameSafe(GetOwner()));
}

void UChaosImpactPuppetComponent::SetPartsVisible(const bool bShow)
{
	SetVisibility(bShow, false);
	for (USceneComponent* Part : TArray<USceneComponent*>{Body, ArmL, ArmR, LegL, LegR, Skin})
	{
		if (Part)
		{
			Part->SetVisibility(bShow, false);
		}
	}
}

void UChaosImpactPuppetComponent::SetPartsOverlayMaterial(UMaterialInterface* Overlay)
{
	for (UMeshComponent* Part : TArray<UMeshComponent*>{Body, ArmL, ArmR, LegL, LegR, Skin})
	{
		if (Part)
		{
			Part->SetOverlayMaterial(Overlay);
		}
	}
}

bool UChaosImpactPuppetComponent::GetPalmLocation(const bool bRightHand, FVector& OutLocation) const
{
	const FLimbChain& Chain = bRightHand ? ChainArmR : ChainArmL;
	if (!bReady || !Chain.bSolved)
	{
		return false;
	}
	const FVector Along = (Chain.SolvedEnd - Chain.SolvedMid).GetSafeNormal();
	OutLocation = GetComponentTransform().TransformPosition(Chain.SolvedEnd + Along * (bSkinned ? PalmOffset : PalmDistance));
	return true;
}

void UChaosImpactPuppetComponent::SetHeldBalls(USceneComponent* RightBall, USceneComponent* LeftBall)
{
	RightHeldBall = RightBall;
	LeftHeldBall = LeftBall;
}

void UChaosImpactPuppetComponent::SolveTwoBone(const FVector& Root, const FVector& Target, const FVector& Pole,
	const float UpperLength, const float LowerLength, FVector& OutMid, FVector& OutEnd)
{
	const FVector ToTarget = Target - Root;
	const float Distance = ToTarget.Size();
	const FVector Direction = Distance > KINDA_SMALL_NUMBER ? ToTarget / Distance : FVector(0.0f, 0.0f, -1.0f);
	const float Reach = FMath::Clamp(Distance, FMath::Abs(UpperLength - LowerLength) + 0.5f,
		UpperLength + LowerLength - 0.01f);
	FVector Bend = Pole - Direction * FVector::DotProduct(Pole, Direction);
	if (Bend.SizeSquared() < KINDA_SMALL_NUMBER)
	{
		Bend = FVector::CrossProduct(Direction, FVector::UpVector);
		if (Bend.SizeSquared() < KINDA_SMALL_NUMBER)
		{
			Bend = FVector::ForwardVector;
		}
	}
	Bend.Normalize();
	const float CosAngle = FMath::Clamp((UpperLength * UpperLength + Reach * Reach - LowerLength * LowerLength)
		/ (2.0f * UpperLength * Reach), -1.0f, 1.0f);
	const float SinAngle = FMath::Sqrt(FMath::Max(0.0f, 1.0f - CosAngle * CosAngle));
	OutMid = Root + Direction * (UpperLength * CosAngle) + Bend * (UpperLength * SinAngle);
	OutEnd = Root + Direction * Reach;
}

void UChaosImpactPuppetComponent::PoseChain(FLimbChain& Chain, UPoseableMeshComponent* Limb, const FQuat& BaseRotation,
	const FVector& Root, const FVector& Mid, const FVector& End, const bool bKeepEndFlat)
{
	// Turn each segment from its reference direction onto the solved one; joints below follow their parent.
	const FVector ReferenceUpper = BaseRotation.RotateVector(Chain.Reference[1].GetLocation() - Chain.Reference[0].GetLocation());
	const FQuat Upper = FQuat::FindBetweenVectors(ReferenceUpper, Mid - Root) * BaseRotation;
	const FVector ReferenceLower = Upper.RotateVector(Chain.Reference[2].GetLocation() - Chain.Reference[1].GetLocation());
	const FQuat Lower = FQuat::FindBetweenVectors(ReferenceLower, End - Mid) * Upper;
	Limb->SetBoneTransformByName(Chain.Joints[0], FTransform(Upper * Chain.Reference[0].GetRotation(), Root,
		Chain.Reference[0].GetScale3D()), EBoneSpaces::ComponentSpace);
	Limb->SetBoneTransformByName(Chain.Joints[1], FTransform(Lower * Chain.Reference[1].GetRotation(), Mid,
		Chain.Reference[1].GetScale3D()), EBoneSpaces::ComponentSpace);
	const FQuat EndRotation = (bKeepEndFlat ? BaseRotation : Lower) * Chain.Reference[2].GetRotation();
	Limb->SetBoneTransformByName(Chain.Joints[2], FTransform(EndRotation, End, Chain.Reference[2].GetScale3D()),
		EBoneSpaces::ComponentSpace);
	Chain.SolvedMid = Mid;
	Chain.SolvedEnd = End;
	Chain.bSolved = true;
}

void UChaosImpactPuppetComponent::TickComponent(const float DeltaTime, const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdatePose();
}

void UChaosImpactPuppetComponent::UpdatePose(const bool bPushNow)
{
	if (!bReady || !IsValid(Source) || !Source->GetSkinnedAsset() || !IsVisible())
	{
		return;
	}
	if (bSkinned)
	{
		UpdateSkinnedPose();
		if (bPushNow)
		{
			Skin->RefreshBoneTransforms();
		}
		PlaceHeldBalls();
		return;
	}

	// The body bobs with the hips and leans with the spine, scaled down to this model's shorter legs.
	const float LegRatio = bRigid ? 0.7f : (ChainLegL.LengthRatio + ChainLegR.LengthRatio) * 0.5f;
	const FVector Pelvis = SourceLocation(SourcePelvis);
	const FVector PelvisReference = SourceReferenceLocation(SourcePelvis);
	const FVector BodyOffset = (Pelvis - PelvisReference) * LegRatio;
	// Only part of the spine's lean: the round body tips over far more visibly than a slim torso does.
	const FQuat Lean = FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenVectors(
		SourceReferenceLocation(SourceNeck) - PelvisReference, SourceLocation(SourceNeck) - Pelvis), 0.45f);
	const auto BodyPoint = [this, &Lean, &BodyOffset](const FVector& Point)
	{
		return BodyPivot + Lean.RotateVector(Point - BodyPivot) + BodyOffset;
	};
	Body->SetRelativeLocationAndRotation(BodyPivot - Lean.RotateVector(BodyPivot) + BodyOffset, Lean);
	if (bRigid)
	{
		// No hands: held balls stay in the template skeleton's hands.
		return;
	}

	// Arms: the hand goes where the source hand is relative to its shoulder, stretched to these arms.
	const FVector BodyCentre = BodyPoint(FVector(BodyPivot.X, BodyPivot.Y, BodyPivot.Z + 30.0f));
	const auto PoseArm = [&](FLimbChain& Chain, UPoseableMeshComponent* Limb, const bool bRight)
	{
		const FVector Shoulder = BodyPoint(Chain.Reference[0].GetLocation());
		const FVector SourceShoulder = SourceLocation(Chain.SourceJoints[0]);
		const FVector SourceElbow = SourceLocation(Chain.SourceJoints[1]);
		const FVector SourceHand = SourceLocation(Chain.SourceJoints[2]);
		const float Exact = bRight ? RightHandExactWeight : 0.0f;
		FVector Target = FMath::Lerp(Shoulder + (SourceHand - SourceShoulder) * Chain.LengthRatio, SourceHand, Exact);
		// A hanging hand stays outside the round body instead of sinking into it.
		const float KeepOut = BodyRadius * (1.0f - Exact);
		FVector Flat(Target.X - BodyCentre.X, Target.Y - BodyCentre.Y, 0.0f);
		if (KeepOut > 0.0f && Flat.Size() < KeepOut && Target.Z < Shoulder.Z)
		{
			const FVector Outward = Flat.IsNearlyZero(1.0f)
				? FVector(Shoulder.X - BodyCentre.X, Shoulder.Y - BodyCentre.Y, 0.0f).GetSafeNormal() : Flat.GetSafeNormal();
			Target.X = BodyCentre.X + Outward.X * KeepOut;
			Target.Y = BodyCentre.Y + Outward.Y * KeepOut;
		}
		FVector Pole = SourceElbow - (SourceShoulder + SourceHand) * 0.5f;
		if (Pole.SizeSquared() < 1.0f)
		{
			Pole = FVector(0.0f, -1.0f, -0.4f);
		}
		FVector Mid;
		FVector End;
		SolveTwoBone(Shoulder, Target, Pole.GetSafeNormal(), Chain.UpperLength, Chain.LowerLength, Mid, End);
		PoseChain(Chain, Limb, Lean, Shoulder, Mid, End, false);
	};
	PoseArm(ChainArmL, ArmL, false);
	PoseArm(ChainArmR, ArmR, true);

	// Legs: the foot keeps its step relative to the hip, and a planted foot stays on the floor.
	const auto PoseLeg = [&](FLimbChain& Chain, UPoseableMeshComponent* Limb)
	{
		const FVector Hip = Chain.Reference[0].GetLocation() + BodyOffset;
		const FVector SourceHip = SourceLocation(Chain.SourceJoints[0]);
		const FVector SourceKnee = SourceLocation(Chain.SourceJoints[1]);
		const FVector SourceFoot = SourceLocation(Chain.SourceJoints[2]);
		FVector Target = Hip + (SourceFoot - SourceHip) * Chain.LengthRatio;
		// A runner's feet land near the centre line; on these short, wide-set legs that crosses them, so each
		// foot keeps to its own side (model +X is the character's left).
		const float MinSide = FMath::Abs(Chain.Reference[2].GetLocation().X - BodyPivot.X) * 0.7f;
		const float Side = Target.X - BodyPivot.X;
		const float Sign = Chain.Reference[2].GetLocation().X >= BodyPivot.X ? 1.0f : -1.0f;
		if (Side * Sign < MinSide)
		{
			Target.X = BodyPivot.X + Sign * MinSide;
		}
		const float Lift = FMath::Max(0.0f, SourceFoot.Z - SourceReferenceLocation(Chain.SourceJoints[2]).Z);
		Target.Z = Chain.Reference[2].GetLocation().Z + Lift * Chain.LengthRatio;
		FVector Pole = SourceKnee - (SourceHip + SourceFoot) * 0.5f;
		if (Pole.SizeSquared() < 1.0f)
		{
			Pole = FVector(0.0f, 1.0f, 0.0f);
		}
		FVector Mid;
		FVector End;
		SolveTwoBone(Hip, Target, Pole.GetSafeNormal(), Chain.UpperLength, Chain.LowerLength, Mid, End);
		PoseChain(Chain, Limb, FQuat::Identity, Hip, Mid, End, true);
	};
	PoseLeg(ChainLegL, LegL);
	PoseLeg(ChainLegR, LegR);
	if (bPushNow)
	{
		for (UPoseableMeshComponent* Limb : {ArmL.Get(), ArmR.Get(), LegL.Get(), LegR.Get()})
		{
			Limb->RefreshBoneTransforms();
		}
	}

	PlaceHeldBalls();
}

void UChaosImpactPuppetComponent::UpdateSkinnedPose()
{
	// Legs first. They are one straight joint each (no knee), so copying the source's bent-knee steps swings them up
	// stiffly. Instead each leg only swings forward and back with the source stride, pulled in under the hips (the
	// model stands with its feet wide apart), and the hips sit as low as the planted leg allows.
	// This model faces +Y; +X is its left.
	constexpr float StrideScale = 0.75f;
	constexpr float MaxSwingDegrees = 38.0f;
	/** The bind pose spreads each leg about 15 degrees out from its hip; this stands the legs straight down. */
	constexpr float InwardDegrees = 15.0f;
	FVector LegDirections[2];
	float PlantedDrop = TNumericLimits<float>::Max();
	for (int32 Side = 0; Side < (bSkinnedLegChains ? 0 : 2); ++Side)
	{
		const FLimbChain& SourceLeg = Side == 0 ? ChainLegL : ChainLegR;
		const FVector Step = SourceLocation(SourceLeg.SourceJoints[2]) - SourceLocation(SourceLeg.SourceJoints[0]);
		const float SourceLength = FMath::Max(1.0f, static_cast<float>(FVector::Dist(
			SourceReferenceLocation(SourceLeg.SourceJoints[0]), SourceReferenceLocation(SourceLeg.SourceJoints[2]))));
		const float Swing = FMath::Clamp(FMath::Asin(FMath::Clamp(static_cast<float>(Step.Y) / SourceLength, -1.0f, 1.0f))
			* StrideScale, FMath::DegreesToRadians(-MaxSwingDegrees), FMath::DegreesToRadians(MaxSwingDegrees));
		const float Inward = FMath::DegreesToRadians(InwardDegrees) * (Side == 0 ? -1.0f : 1.0f);
		LegDirections[Side] = FVector(FMath::Sin(Inward), FMath::Sin(Swing) * FMath::Cos(Inward),
			-FMath::Cos(Swing) * FMath::Cos(Inward)).GetSafeNormal();
		PlantedDrop = FMath::Min(PlantedDrop, LegLengths[Side] * (1.0f - static_cast<float>(-LegDirections[Side].Z)));
	}
	const FVector PelvisMove = (SourceLocation(SourcePelvis) - SourceReferenceLocation(SourcePelvis)) * HeightRatio;
	// Only rises above the standing height come from the source (jumps); crouching knees have no match here.
	// With knees the hips simply follow the source pelvis. Without, only rises above the standing height come from the
	// source (jumps); crouching knees have no match there.
	const FVector HipsLocation = bSkinnedLegChains ? HipsReference + PelvisMove
		: FVector(HipsReference.X + PelvisMove.X, HipsReference.Y + PelvisMove.Y,
			HipsReference.Z - PlantedDrop + FMath::Max(0.0f, static_cast<float>(PelvisMove.Z)));

	// Hips, spine, neck, head and shoulders turn exactly as the source's do, measured from each skeleton's own
	// reference pose.
	FQuat HipsTurn = FQuat::Identity;
	FQuat ShoulderTurns[2] = {FQuat::Identity, FQuat::Identity};
	for (int32 Index = 0; Index < DrivenBody.Num(); ++Index)
	{
		const FDrivenBone& Bone = DrivenBody[Index];
		FQuat Turn = SourceRotation(Bone.Source) * Bone.SourceReference.Inverse();
		FTransform Transform = Skin->GetBoneTransformByName(Bone.Name, EBoneSpaces::ComponentSpace);
		if (Index == 0)
		{
			HipsTurn = Turn;
			Transform.SetLocation(HipsLocation);
		}
		Transform.SetRotation(Turn * Bone.Reference);
		Skin->SetBoneTransformByName(Bone.Name, Transform, EBoneSpaces::ComponentSpace);
		// The last two are the left and right shoulders.
		if (Index >= DrivenBody.Num() - 2)
		{
			ShoulderTurns[Index - (DrivenBody.Num() - 2)] = Turn;
		}
	}

	// Legs with knees: each foot goes where the source foot is relative to its hip, stretched to these legs, the knee
	// bending the way the source knee does; the foot stays level with the hips.
	for (int32 Side = 0; Side < (bSkinnedLegChains ? 2 : 0); ++Side)
	{
		FLimbChain& Chain = Side == 0 ? ChainLegL : ChainLegR;
		const FVector Hip = Skin->GetBoneTransformByName(Chain.Joints[0], EBoneSpaces::ComponentSpace).GetLocation();
		const FVector SourceHip = SourceLocation(Chain.SourceJoints[0]);
		const FVector SourceKnee = SourceLocation(Chain.SourceJoints[1]);
		const FVector SourceFoot = SourceLocation(Chain.SourceJoints[2]);
		FVector Pole = SourceKnee - (SourceHip + SourceFoot) * 0.5f;
		if (Pole.SizeSquared() < 1.0f)
		{
			Pole = FVector(0.0f, 1.0f, 0.0f);
		}
		FVector Mid;
		FVector End;
		SolveTwoBone(Hip, Hip + (SourceFoot - SourceHip) * Chain.LengthRatio, Pole.GetSafeNormal(), Chain.UpperLength,
			Chain.LowerLength, Mid, End);
		PoseChain(Chain, Skin, HipsTurn, Hip, Mid, End, true);
	}

	const FVector Down = HipsTurn.RotateVector(FVector(0.0f, 0.0f, -1.0f));
	for (int32 Side = 0; Side < (bSkinnedLegChains ? 0 : 2); ++Side)
	{
		const FDrivenBone& Leg = DrivenLegs[Side];
		const FQuat Swing = FQuat::FindBetweenNormals(Down, LegDirections[Side]);
		FTransform Transform = Skin->GetBoneTransformByName(Leg.Name, EBoneSpaces::ComponentSpace);
		Transform.SetRotation(Swing * HipsTurn * Leg.Reference);
		Skin->SetBoneTransformByName(Leg.Name, Transform, EBoneSpaces::ComponentSpace);
	}

	// Arms: the hand goes where the source hand is relative to its shoulder, stretched to these arms, and exactly
	// into the source hand while a throw is released.
	const auto PoseArm = [&](FLimbChain& Chain, const bool bRight)
	{
		const FVector Shoulder = Skin->GetBoneTransformByName(Chain.Joints[0], EBoneSpaces::ComponentSpace).GetLocation();
		const FVector SourceShoulder = SourceLocation(Chain.SourceJoints[0]);
		const FVector SourceElbow = SourceLocation(Chain.SourceJoints[1]);
		const FVector SourceHand = SourceLocation(Chain.SourceJoints[2]);
		const float Exact = bRight ? RightHandExactWeight : 0.0f;
		const FVector Target = FMath::Lerp(Shoulder + (SourceHand - SourceShoulder) * Chain.LengthRatio, SourceHand, Exact);
		FVector Pole = SourceElbow - (SourceShoulder + SourceHand) * 0.5f;
		if (Pole.SizeSquared() < 1.0f)
		{
			Pole = FVector(0.0f, -1.0f, -0.4f);
		}
		FVector Mid;
		FVector End;
		SolveTwoBone(Shoulder, Target, Pole.GetSafeNormal(), Chain.UpperLength, Chain.LowerLength, Mid, End);
		PoseChain(Chain, Skin, ShoulderTurns[bRight ? 1 : 0], Shoulder, Mid, End, false);
	};
	PoseArm(ChainArmL, false);
	PoseArm(ChainArmR, true);
}

void UChaosImpactPuppetComponent::PlaceHeldBalls()
{
	// Held balls sit in these palms; they are attached to the source hands, so they are moved here every frame.
	for (const bool bRight : {true, false})
	{
		USceneComponent* Ball = (bRight ? RightHeldBall : LeftHeldBall).Get();
		FVector Palm;
		if (Ball && Ball->IsVisible() && GetPalmLocation(bRight, Palm))
		{
			Ball->SetWorldLocation(Palm);
		}
	}
}
