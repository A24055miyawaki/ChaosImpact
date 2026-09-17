#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "ChaosImpactPuppetComponent.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPoseableMeshComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class UTexture;

/**
 * The player's visible body: the character model from Content/ChaosImpact/Character, built from a static body and
 * four separately skinned limbs (the source file has no hips or spine joining them).
 *
 * It copies the pose of the character's own (hidden) skeletal mesh every frame. That mesh keeps running the usual
 * locomotion and throw animations, so walking, running, jumping and throwing keep exactly the timing gameplay
 * relies on; this component only reaches each hand and foot toward where that skeleton puts it, scaled to these
 * limbs, and bobs and leans the body with its hips and spine. Presentation only, on every machine.
 */
UCLASS(ClassGroup=(ChaosImpact))
class UChaosImpactPuppetComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UChaosImpactPuppetComponent();

	/**
	 * Builds the parts of roster character CharacterIndex under this component. False when an asset or a bone is
	 * missing (nothing is shown then).
	 */
	bool Initialize(USkeletalMeshComponent* InSource, int32 CharacterIndex = 0);
	int32 GetCharacterIndex() const { return CharacterIndex; }
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
	bool IsReady() const { return bReady; }

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	/**
	 * Copies the source skeleton's current pose; TickComponent does this every frame. bPushNow sends the limbs' bones
	 * to the renderer at once, for a caller posing it in a paused world where the limbs do not tick.
	 */
	void UpdatePose(bool bPushNow = false);

	/** 0 red, 1 blue, 2 yellow, 3 green: the team colours used everywhere else. */
	void SetColourIndex(int32 Index);
	/** How strictly the right hand goes to the source skeleton's own hand (1 while a thrown ball is still held). */
	void SetRightHandExactWeight(float Weight) { RightHandExactWeight = FMath::Clamp(Weight, 0.0f, 1.0f); }
	/** Shows or hides the five parts only; balls attached elsewhere keep their own visibility. */
	void SetPartsVisible(bool bShow);
	void SetPartsOverlayMaterial(UMaterialInterface* Material);
	/** Where a held ball sits in this hand, in world space. False before the first pose. */
	bool GetPalmLocation(bool bRightHand, FVector& OutLocation) const;
	/** The balls shown in the hands; each frame they are moved into these palms after the pose is solved. */
	void SetHeldBalls(USceneComponent* RightBall, USceneComponent* LeftBall);

private:
	struct FLimbChain
	{
		/** Joint names in the limb's own skeleton, from its root: shoulder/hip, elbow/knee, hand/ankle. */
		FName Joints[3];
		FTransform Reference[3];
		float UpperLength = 1.0f;
		float LowerLength = 1.0f;
		/** Source skeleton bones for the same joints. */
		int32 SourceJoints[3] = {INDEX_NONE, INDEX_NONE, INDEX_NONE};
		/** Limb length here over the source skeleton's, so a pose keeps its angles on longer or shorter limbs. */
		float LengthRatio = 1.0f;
		FVector SolvedEnd = FVector::ZeroVector;
		FVector SolvedMid = FVector::ZeroVector;
		bool bSolved = false;
	};

	/** A bone of a skinned model that turns with one of the source skeleton's bones. */
	struct FDrivenBone
	{
		FName Name;
		int32 Source = INDEX_NONE;
		/** Component-space rotations in the reference pose (the source's in this component's space). */
		FQuat Reference = FQuat::Identity;
		FQuat SourceReference = FQuat::Identity;
	};

	UPoseableMeshComponent* CreateLimb(const TCHAR* AssetName);
	UPoseableMeshComponent* CreatePoseable(USkeletalMesh* Mesh);
	bool InitializeSkinned(USkeletalMesh* Mesh);
	void UpdateSkinnedPose();
	void PlaceHeldBalls();
	FQuat SourceRotation(int32 BoneIndex) const;
	FQuat SourceReferenceRotation(int32 BoneIndex) const;
	bool SetUpChain(FLimbChain& Chain, UPoseableMeshComponent* Limb, const TCHAR* JointPrefix, bool bArm, bool bLeft);
	/** Two-bone reach from Root toward Target, the middle joint bending toward Pole. */
	static void SolveTwoBone(const FVector& Root, const FVector& Target, const FVector& Pole, float UpperLength,
		float LowerLength, FVector& OutMid, FVector& OutEnd);
	void PoseChain(FLimbChain& Chain, UPoseableMeshComponent* Limb, const FQuat& BaseRotation, const FVector& Root,
		const FVector& Mid, const FVector& End, bool bKeepEndFlat);
	FVector SourceLocation(int32 BoneIndex) const;
	FVector SourceReferenceLocation(int32 BoneIndex) const;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> Source;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY(Transient)
	TObjectPtr<UPoseableMeshComponent> ArmL;

	UPROPERTY(Transient)
	TObjectPtr<UPoseableMeshComponent> ArmR;

	UPROPERTY(Transient)
	TObjectPtr<UPoseableMeshComponent> LegL;

	UPROPERTY(Transient)
	TObjectPtr<UPoseableMeshComponent> LegR;

	/** The whole model, for a skinned character. */
	UPROPERTY(Transient)
	TObjectPtr<UPoseableMeshComponent> Skin;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> Material;

	/** A rigid model keeps each slot's own material (a head can have its own texture); each gets the colour. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> SlotMaterials;

	TWeakObjectPtr<USceneComponent> RightHeldBall;
	TWeakObjectPtr<USceneComponent> LeftHeldBall;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTexture>> ColourTextures;

	FLimbChain ChainArmL;
	FLimbChain ChainArmR;
	FLimbChain ChainLegL;
	FLimbChain ChainLegR;
	int32 SourcePelvis = INDEX_NONE;
	int32 SourceNeck = INDEX_NONE;
	/** Where the body turns when it leans: between the hips. */
	FVector BodyPivot = FVector::ZeroVector;
	/** Distance from the body's centre line a hanging hand keeps, so arms do not sink into the round body. */
	float BodyRadius = 24.0f;
	float RightHandExactWeight = 0.0f;
	int32 ColourIndex = INDEX_NONE;
	int32 CharacterIndex = 0;
	/** A single rigid mesh with no limbs (see EChaosImpactModelKind). */
	bool bRigid = false;
	bool bSkinned = false;
	/** Skinned model: hips, spine, neck, head and shoulders, parents first. */
	TArray<FDrivenBone> DrivenBody;
	FDrivenBone DrivenLegs[2];
	FVector HipsReference = FVector::ZeroVector;
	/** Hips height here over the source pelvis height. */
	float HeightRatio = 1.0f;
	/** Skinned model with knee and ankle joints (Character1_*Leg, *Foot): legs follow the source's steps like arms. */
	bool bSkinnedLegChains = false;
	/** Skinned model: from each thigh joint to the floor, in the reference pose. */
	float LegLengths[2] = {1.0f, 1.0f};
	/** From the hand joint toward the fingers, in model units, where a held ball sits. */
	float PalmOffset = 9.0f;
	bool bReady = false;
};
