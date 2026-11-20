#include "ChaosImpactPresidentBoss.h"

#include "AIController.h"
#include "ChaosImpact.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactBallTypes.h"
#include "ChaosImpactEase.h"
#include "ChaosImpactFxKit.h"
#include "ChaosImpactHazardZone.h"
#include "ChaosImpactNovaRig.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactPaint.h"
#include "ChaosImpactSfx.h"
#include "ChaosImpactSimaeBossFx.h"
#include "ChaosImpactSoloRoom.h"
#include "ChaosImpactWhiteHall.h"
#include "Animation/AnimSequence.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "AnimationRuntime.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	const TCHAR* const PresidentFolder = TEXT("/Game/ChaosImpact/President/Boss/");
	const FLinearColor PresWarning(1.0f, 0.18f, 0.16f, 1.0f);
	const FLinearColor NovaBlue(0.45f, 0.86f, 1.0f, 1.0f);
	const FLinearColor NovaCore(0.92f, 0.98f, 1.0f, 1.0f);
	constexpr float PresCapsuleRadius = 170.0f;
	constexpr float PresCapsuleHalfHeight = 300.0f;
	/** The game's camera drawn back for the whole fight (the President, the colonnade and the throne in view). */
	constexpr float PresidentCameraScale = 1.35f;

	template <typename T>
	T* LoadPres(const TCHAR* Name)
	{
		const FString Path = FString(PresidentFolder) + Name + TEXT(".") + Name;
		return LoadObject<T>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}

	float Smooth01(const float X)
	{
		const float T = FMath::Clamp(X, 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}

	/** Its six arms' bones, in the order of GetHand (right upper, middle, lower; left upper, middle, lower). */
	const TCHAR* const ArmBones[6] = {TEXT("RightArm"), TEXT("RightArm1"), TEXT("RightArm2"), TEXT("LeftArm"), TEXT("LeftArm1"), TEXT("LeftArm2")};

	FName FindBone(const USkeletalMeshComponent* Body, const TCHAR* Short)
	{
		if (!Body)
		{
			return NAME_None;
		}
		for (const FString& Candidate : {FString(TEXT("Character1_")) + Short, FString(Short)})
		{
			const FName Name(*Candidate);
			if (Body->GetBoneIndex(Name) != INDEX_NONE)
			{
				return Name;
			}
		}
		return NAME_None;
	}

	/** Distance from P to the segment A-B, flat. */
	float SegmentDistance2D(const FVector& P, const FVector& A, const FVector& B)
	{
		const FVector2D P2(P), A2(A), B2(B);
		const FVector2D AB = B2 - A2;
		const float Len = AB.SizeSquared();
		const float T = Len > 1.0f ? FMath::Clamp(FVector2D::DotProduct(P2 - A2, AB) / Len, 0.0f, 1.0f) : 0.0f;
		return FVector2D::Distance(P2, A2 + AB * T);
	}
}

AChaosImpactPresidentBoss::AChaosImpactPresidentBoss()
{
	bUseToonCharacter = false;
	bCanCollectBalls = false;
	AIControllerClass = AAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	SoloTeam = SoloEnemyTeam;
	bRespawnAfterElimination = false;
	MaxHealth = 20.0f;
	Health = MaxHealth;
	EliminationEffectDuration = 1.6f;
	Tags.Add(TEXT("CI.SoloBoss"));

	GetCapsuleComponent()->InitCapsuleSize(PresCapsuleRadius, PresCapsuleHalfHeight);
	USkeletalMeshComponent* Body = GetMesh();
	Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	Car = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Car"));
	Car->SetupAttachment(GetRootComponent());
	Car->SetUsingAbsoluteLocation(true);
	Car->SetUsingAbsoluteRotation(true);
	Car->SetUsingAbsoluteScale(true);
	Car->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Car->SetVisibility(false);
	Car2 = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Car2"));
	Car2->SetupAttachment(GetRootComponent());
	Car2->SetUsingAbsoluteLocation(true);
	Car2->SetUsingAbsoluteRotation(true);
	Car2->SetUsingAbsoluteScale(true);
	Car2->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Car2->SetVisibility(false);
	CarLights = CreateDefaultSubobject<UPointLightComponent>(TEXT("CarLights"));
	CarLights->SetupAttachment(Car);
	CarLights->SetIntensity(0.0f);
	CarLights->SetAttenuationRadius(900.0f);
	CarLights->SetLightColor(FLinearColor(1.0f, 0.9f, 0.7f));
	CarLights->SetCastShadows(false);
	BeamCore = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BeamCore"));
	BeamGlow = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BeamGlow"));
	for (UStaticMeshComponent* Part : {BeamCore.Get(), BeamGlow.Get()})
	{
		Part->SetupAttachment(GetRootComponent());
		Part->SetStaticMesh(Cylinder);
		Part->SetUsingAbsoluteLocation(true);
		Part->SetUsingAbsoluteRotation(true);
		Part->SetUsingAbsoluteScale(true);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCastShadow(false);
		Part->SetVisibility(false);
	}
	BeamLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("BeamLight"));
	BeamLight->SetupAttachment(GetRootComponent());
	BeamLight->SetUsingAbsoluteLocation(true);
	BeamLight->SetIntensity(0.0f);
	BeamLight->SetAttenuationRadius(1800.0f);
	BeamLight->SetLightColor(FLinearColor(1.0f, 0.55f, 0.4f));
	BeamLight->SetCastShadows(false);

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->MaxWalkSpeed = 380.0f;
	Movement->bOrientRotationToMovement = false;
	Movement->MaxAcceleration = 900.0f;
	Movement->BrakingDecelerationWalking = 900.0f;
}

void AChaosImpactPresidentBoss::BeginPlay()
{
	Super::BeginPlay();
	BodyModel = LoadPres<USkeletalMesh>(TEXT("SK_RastBoss"));
	CarFormModel = LoadPres<USkeletalMesh>(TEXT("SK_RastBoss_CarForm"));
	SitClip = LoadPres<UAnimSequence>(TEXT("A_RastBoss_Agura"));
	const TCHAR* const ThrowNames[6] = {TEXT("A_RastBoss_Throw_RightUpper"), TEXT("A_RastBoss_Throw_RightMiddle"), TEXT("A_RastBoss_Throw_RightLower"),
		TEXT("A_RastBoss_Throw_LeftUpper"), TEXT("A_RastBoss_Throw_LeftMiddle"), TEXT("A_RastBoss_Throw_LeftLower")};
	for (int32 Arm = 0; Arm < 6; ++Arm)
	{
		ThrowClips[Arm] = LoadPres<UAnimSequence>(ThrowNames[Arm]);
	}
	// (The left middle arm's clip did not come through: the left upper's stands in.)
	if (!ThrowClips[4])
	{
		ThrowClips[4] = ThrowClips[3] ? ThrowClips[3].Get() : ThrowClips[5].Get();
	}
	BeamStartClip = LoadPres<UAnimSequence>(TEXT("A_RastBoss_Beam_Start"));
	BeamFireClip = LoadPres<UAnimSequence>(TEXT("A_RastBoss_Beam_Fire"));
	BeamEndClip = LoadPres<UAnimSequence>(TEXT("A_RastBoss_Beam_End"));
	CarStartClip = LoadPres<UAnimSequence>(TEXT("A_RastBoss_CarStart"));
	CarEndClip = LoadPres<UAnimSequence>(TEXT("A_RastBoss_CarEnd"));
	if (UStaticMesh* CarMesh = LoadPres<UStaticMesh>(TEXT("SM_RastCar")))
	{
		Car->SetStaticMesh(CarMesh);
		Car2->SetStaticMesh(CarMesh);
	}

	USkeletalMeshComponent* Body = GetMesh();
	if (BodyModel)
	{
		Body->SetSkeletalMeshAsset(BodyModel);
		// Sized to sit SeatedHeight tall, standing on its origin's lowest point.
		const FBoxSphereBounds Bounds = BodyModel->GetBounds();
		const float Tall = FMath::Max(static_cast<float>(Bounds.BoxExtent.Z * 2.0), 1.0f);
		ModelScale = SeatedHeight / (Tall * 0.62f);
		ModelBottom = static_cast<float>(Bounds.Origin.Z - Bounds.BoxExtent.Z);
		Body->SetRelativeScale3D(FVector(ModelScale));
		UE_LOG(LogChaosImpact, Log, TEXT("President boss: model %.0f cm tall, scale %.2f"), Tall, ModelScale);
	}
	MeshRest = FVector(0.0f, 0.0f, -PresCapsuleHalfHeight - ModelBottom * ModelScale);
	FindHands();
	Body->SetRelativeLocationAndRotation(MeshRest + FVector(0.0f, 0.0f, Hover), FRotator(0.0f, -90.0f, 0.0f));
	PlayClip(SitClip, true, 1.0f);

	if (UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")))
	{
		for (int32 Arm = 0; Arm < 6; ++Arm)
		{
			UStaticMeshComponent* Ball = NewObject<UStaticMeshComponent>(this);
			Ball->SetStaticMesh(Sphere);
			Ball->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Ball->SetCastShadow(false);
			Ball->SetUsingAbsoluteLocation(true);
			Ball->SetUsingAbsoluteScale(true);
			Ball->SetupAttachment(GetRootComponent());
			Ball->RegisterComponent();
			Ball->SetWorldScale3D(FVector(0.62f));
			Ball->SetVisibility(false);
			HandBalls.Add(Ball);
			HandBallTypes.Add(EChaosImpactBallType::Normal);
		}
	}
	BeamCoreLook = ChaosImpactBallTypes::MakeAdditive(this, FLinearColor(1.0f, 0.9f, 0.7f), 3.2f, 0.0f);
	BeamGlowLook = ChaosImpactBallTypes::MakeAdditive(this, FLinearColor(1.0f, 0.18f, 0.25f), 1.1f, 0.0f);
	BeamCore->SetMaterial(0, BeamCoreLook);
	BeamGlow->SetMaterial(0, BeamGlowLook);
	if (UMaterialInterface* CarLook = LoadPres<UMaterialInterface>(TEXT("M_CI_RastCar")))
	{
		Car->SetMaterial(0, CarLook);
		Car2->SetMaterial(0, CarLook);
	}
	// (The car's own model is a metre long: made the size of a real one, a little bigger.)
	Car->SetWorldScale3D(FVector(4.2f));
	Car2->SetWorldScale3D(FVector(4.2f));
}

void AChaosImpactPresidentBoss::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// (Gone mid-rush, as when the room starts over after a fall: the player's camera back to the usual one.)
	if (AChaosImpactCharacter* Target = MoveTarget.Get(); Target && Target->IsStageOverheadView())
	{
		Target->SetStageOverheadView(false);
	}
	for (FNovaShot& Shot : NovaShots)
	{
		ClearNovaFall(Shot);
	}
	if (AChaosImpactFxKit* Kit = Fx.Get())
	{
		Kit->Destroy();
	}
	Super::EndPlay(EndPlayReason);
}

AChaosImpactFxKit* AChaosImpactPresidentBoss::GetFx()
{
	AChaosImpactFxKit* Kit = Fx.Get();
	if (!IsValid(Kit))
	{
		Kit = AChaosImpactFxKit::Spawn(this, FVector::ZeroVector);
		Fx = Kit;
	}
	return Kit;
}

AChaosImpactWhiteHall* AChaosImpactPresidentBoss::FindHall() const
{
	return AChaosImpactWhiteHall::Find(GetWorld());
}

// ---------------------------------------------------------------------------------------------------- State

void AChaosImpactPresidentBoss::SetDormant(const bool bNewDormant)
{
	if (bDormant == bNewDormant)
	{
		return;
	}
	bDormant = bNewDormant;
	if (!bDormant && GetWorld())
	{
		WakeTime = GetWorld()->GetTimeSeconds();
		RestLeft = 1.2f;
		SetHover(IdleHover, 0.6f);
	}
}

void AChaosImpactPresidentBoss::SetSceneHold(const bool bHold)
{
	bSceneHold = bHold;
	if (bHold)
	{
		AbortMove();
		ShakeOffMoveEffects();
	}
}

bool AChaosImpactPresidentBoss::IsMoveProof() const
{
	return Move != EChaosImpactPresidentMove::None || bSceneHold || bBeaten || bDormant;
}

void AChaosImpactPresidentBoss::FinishBeaten()
{
	bBeaten = false;
	bSceneHold = false;
	Destroy();
}

void AChaosImpactPresidentBoss::PlaySceneClip(const int32 Clip, const bool bLoop, const float Rate)
{
	if (Clip == 0)
	{
		PlayClip(SitClip, bLoop, Rate);
	}
	else if (Clip >= 1 && Clip <= 6)
	{
		PlayClip(ThrowClips[Clip - 1], bLoop, Rate);
	}
	else if (Clip == 7)
	{
		PlayClip(BeamStartClip, bLoop, Rate);
	}
	else if (Clip == 8)
	{
		PlayClip(BeamFireClip ? BeamFireClip.Get() : BeamStartClip.Get(), bLoop, Rate);
	}
	else if (Clip == 9)
	{
		PlayClip(BeamEndClip, bLoop, Rate);
	}
}

void AChaosImpactPresidentBoss::PlayClip(UAnimSequence* Clip, const bool bLoop, const float Rate)
{
	USkeletalMeshComponent* Body = GetMesh();
	if (!Clip || !Body)
	{
		return;
	}
	if (Clip == SitClip)
	{
		// Sitting cross-legged is how it always is, not a motion: the clip's seated pose (its last frame), held.
		if (PlayingClip != SitClip)
		{
			Body->PlayAnimation(SitClip, false);
			Body->SetPosition(FMath::Max(SitClip->GetPlayLength() - 0.01f, 0.0f), false);
			PlayingClip = SitClip;
			bPlayingLoop = false;
		}
		Body->SetPlayRate(0.0f);
		return;
	}
	if (!bLoop || Clip != PlayingClip || bLoop != bPlayingLoop)
	{
		Body->PlayAnimation(Clip, bLoop);
		PlayingClip = Clip;
		bPlayingLoop = bLoop;
	}
	Body->SetPlayRate(Rate);
}

void AChaosImpactPresidentBoss::SetHover(const float Height, const float Seconds)
{
	HoverFrom = Hover;
	HoverTo = Height;
	HoverTime = 0.0f;
	HoverSeconds = FMath::Max(Seconds, 0.01f);
}

void AChaosImpactPresidentBoss::UpdateHover(const float DeltaSeconds)
{
	HoverTime += DeltaSeconds;
	Hover = FMath::Lerp(HoverFrom, HoverTo, ChaosImpactEase::InOut(FMath::Clamp(HoverTime / HoverSeconds, 0.0f, 1.0f)));
	// A slow bob in the air (not when it sits on something).
	const float Time = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	const float Bob = Hover > 30.0f ? 14.0f * FMath::Sin(Time * 1.7f) : 0.0f;
	const float Shudder = HitFlash > 0.0f ? FMath::Sin(Time * 90.0f) * 6.0f * HitFlash : 0.0f;
	GetMesh()->SetRelativeLocation(MeshRest + FVector(Shudder, 0.0f, Hover + Bob));
}

void AChaosImpactPresidentBoss::FindHands()
{
	bHandsFound = false;
	const USkeletalMesh* Model = BodyModel;
	FSkeletalMeshRenderData* Render = Model ? Model->GetResourceForRendering() : nullptr;
	if (!Render || Render->LODRenderData.IsEmpty())
	{
		return;
	}
	const FSkeletalMeshLODRenderData& Lod = Render->LODRenderData[0];
	const FReferenceSkeleton& Ref = Model->GetRefSkeleton();
	TArray<FTransform> Space;
	FAnimationRuntime::FillUpComponentSpaceTransforms(Ref, Ref.GetRefBonePose(), Space);
	const uint32 Vertices = Lod.StaticVertexBuffers.PositionVertexBuffer.GetNumVertices();
	if (Vertices == 0 || Lod.SkinWeightVertexBuffer.GetNumVertices() != Vertices || !Lod.StaticVertexBuffers.PositionVertexBuffer.GetVertexData()
		|| !Lod.SkinWeightVertexBuffer.GetDataVertexBuffer() || !Lod.SkinWeightVertexBuffer.GetDataVertexBuffer()->GetWeightData())
	{
		UE_LOG(LogChaosImpact, Warning, TEXT("President boss: its model's vertices are not readable; hands guessed"));
		return;
	}
	// Each vertex's strongest bone.
	TArray<int32> Strongest;
	Strongest.Init(INDEX_NONE, Vertices);
	const int32 Influences = Lod.SkinWeightVertexBuffer.GetMaxBoneInfluences();
	for (const FSkelMeshRenderSection& Section : Lod.RenderSections)
	{
		for (uint32 V = Section.BaseVertexIndex; V < Section.BaseVertexIndex + Section.NumVertices && V < Vertices; ++V)
		{
			int32 Best = INDEX_NONE;
			uint32 BestWeight = 0;
			for (int32 I = 0; I < Influences; ++I)
			{
				const uint32 Weight = Lod.SkinWeightVertexBuffer.GetBoneWeight(V, I);
				if (Weight > BestWeight)
				{
					BestWeight = Weight;
					Best = static_cast<int32>(Lod.SkinWeightVertexBuffer.GetBoneIndex(V, I));
				}
			}
			if (Section.BoneMap.IsValidIndex(Best))
			{
				Strongest[V] = Section.BoneMap[Best];
			}
		}
	}
	// A hand: each arm (its root bone and everything under it, but not another arm hanging off it), its far end from the
	// shoulder: the palm and fingers. The ball sits there, on the bone most of those vertices follow (the hand's own).
	struct FFound { FName Bone; FVector Offset; float Height; bool bRight; };
	TArray<FFound> Found;
	int32 Roots[6];
	for (int32 Arm = 0; Arm < 6; ++Arm)
	{
		const FName Bone = FindBone(GetMesh(), ArmBones[Arm]);
		Roots[Arm] = Bone.IsNone() ? INDEX_NONE : Ref.FindBoneIndex(Bone);
	}
#if !UE_BUILD_SHIPPING
	for (int32 Bone = 0; Bone < Ref.GetNum(); ++Bone)
	{
		const int32 Parent = Ref.GetParentIndex(Bone);
		UE_LOG(LogChaosImpact, Verbose, TEXT("President boss bone %s <- %s"), *Ref.GetBoneName(Bone).ToString(),
			Parent == INDEX_NONE ? TEXT("-") : *Ref.GetBoneName(Parent).ToString());
	}
#endif
	for (int32 Arm = 0; Arm < 6; ++Arm)
	{
		const int32 Root = Roots[Arm];
		if (Root == INDEX_NONE)
		{
			continue;
		}
		const auto InArm = [&](const int32 Of)
		{
			if (Of != Root && !Ref.BoneIsChildOf(Of, Root))
			{
				return false;
			}
			for (int32 Other = 0; Other < 6; ++Other)
			{
				if (Other != Arm && Roots[Other] != INDEX_NONE && Roots[Other] != Root && Ref.BoneIsChildOf(Roots[Other], Root)
					&& (Of == Roots[Other] || Ref.BoneIsChildOf(Of, Roots[Other])))
				{
					return false;
				}
			}
			return true;
		};
		const FVector Shoulder = Space[Root].GetLocation();
		TArray<uint32> Mine;
		float Far = 0.0f;
		for (uint32 V = 0; V < Vertices; ++V)
		{
			if (Strongest[V] == INDEX_NONE || !InArm(Strongest[V]))
			{
				continue;
			}
			Mine.Add(V);
			Far = FMath::Max(Far, static_cast<float>(FVector::Dist(FVector(Lod.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(V)), Shoulder)));
		}
		if (Mine.IsEmpty())
		{
			continue;
		}
		// (The far eighth of the arm's reach: the palm with the fingers round it.)
		FVector Sum = FVector::ZeroVector;
		int32 Count = 0;
		TMap<int32, int32> Votes;
		for (const uint32 V : Mine)
		{
			const FVector P(Lod.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(V));
			if (FVector::Dist(P, Shoulder) > Far * 0.87f)
			{
				Sum += P;
				++Count;
				++Votes.FindOrAdd(Strongest[V]);
			}
		}
		int32 HandBone = Root;
		int32 Most = 0;
		for (const TPair<int32, int32>& Vote : Votes)
		{
			// (Fingers vote for their hand: the vertices' bone, or its parent when that is a finger of a hand.)
			if (Vote.Value > Most)
			{
				Most = Vote.Value;
				HandBone = Vote.Key;
			}
		}
		// A finger's bone won: the hand it hangs from instead (the bone nearest the arm's root among the far ones' parents).
		while (HandBone != Root && Ref.GetParentIndex(HandBone) != INDEX_NONE && Ref.GetBoneName(HandBone).ToString().Contains(TEXT("Hand"))
			&& Ref.GetBoneName(Ref.GetParentIndex(HandBone)).ToString().Contains(TEXT("Hand")))
		{
			HandBone = Ref.GetParentIndex(HandBone);
		}
		const FVector Palm = Sum / FMath::Max(Count, 1);
		Found.Add({Ref.GetBoneName(HandBone), Space[HandBone].InverseTransformPosition(Palm), static_cast<float>(Palm.Z), Arm < 3});
	}
	if (Found.Num() != 6)
	{
		UE_LOG(LogChaosImpact, Warning, TEXT("President boss: found %d of its six hands"), Found.Num());
		return;
	}
	// Upper, middle, lower on each side, by how high each hand is in the model's own pose.
	for (const bool bRight : {true, false})
	{
		TArray<FFound> Side = Found.FilterByPredicate([bRight](const FFound& F) { return F.bRight == bRight; });
		Side.Sort([](const FFound& A, const FFound& B) { return A.Height > B.Height; });
		for (int32 Index = 0; Index < Side.Num() && Index < 3; ++Index)
		{
			const int32 Arm = (bRight ? 0 : 3) + Index;
			HandBones[Arm] = Side[Index].Bone;
			HandOffsets[Arm] = Side[Index].Offset;
			UE_LOG(LogChaosImpact, Log, TEXT("President boss: hand %d on %s at %s"), Arm, *Side[Index].Bone.ToString(), *Side[Index].Offset.ToCompactString());
		}
	}
	bHandsFound = true;
}

FVector AChaosImpactPresidentBoss::GetHand(const int32 Arm) const
{
	if (bHandsFound)
	{
		const int32 Index = FMath::Clamp(Arm, 0, 5);
		return GetMesh()->GetSocketTransform(HandBones[Index], RTS_World).TransformPosition(HandOffsets[Index]);
	}
	const USkeletalMeshComponent* Body = GetMesh();
	const int32 Index = FMath::Clamp(Arm, 0, 5);
	const bool bRight = Index < 3;
	// The main pair's hands are bones of their own; the four extra arms are one bone each, their hands at its far end.
	if (Index == 0 || Index == 3)
	{
		const FName Hand = FindBone(Body, bRight ? TEXT("RightHand") : TEXT("LeftHand"));
		if (!Hand.IsNone())
		{
			return Body->GetSocketLocation(Hand);
		}
	}
	const FName Bone = FindBone(Body, ArmBones[Index]);
	if (Bone.IsNone())
	{
		return GetActorLocation() + GetActorRightVector() * (bRight ? 1.0f : -1.0f) * 200.0f + FVector(0.0f, 0.0f, 100.0f * (1 - Index % 3));
	}
	const FTransform Joint = Body->GetSocketTransform(Bone);
	// (Along the bone, away from the body's middle.)
	FVector Along = Joint.GetUnitAxis(EAxis::X);
	const FVector Middle = GetActorLocation() + FVector(0.0f, 0.0f, Hover);
	if (FVector::DotProduct(Along, Joint.GetLocation() - Middle) < 0.0f)
	{
		Along = -Along;
	}
	return Joint.GetLocation() + Along * 95.0f * (ModelScale / 3.0f + 0.67f);
}

FVector AChaosImpactPresidentBoss::GetFace() const
{
	const USkeletalMeshComponent* Body = GetMesh();
	const FName Neck = FindBone(Body, TEXT("Neck"));
	const FVector At = Neck.IsNone() ? GetActorLocation() + FVector(0.0f, 0.0f, SeatedHeight * 0.8f) : Body->GetSocketLocation(Neck);
	return At + FVector(0.0f, 0.0f, 40.0f * ModelScale / 3.0f + 25.0f) + GetActorForwardVector() * 30.0f;
}

void AChaosImpactPresidentBoss::ShowHandBall(const int32 Arm, const EChaosImpactBallType Type)
{
	if (!HandBalls.IsValidIndex(Arm) || !HandBalls[Arm])
	{
		return;
	}
	UStaticMeshComponent* Ball = HandBalls[Arm];
	if (Type == EChaosImpactBallType::Normal)
	{
		Ball->SetVisibility(false);
		HandBallTypes[Arm] = Type;
		return;
	}
	if (HandBallTypes[Arm] != Type)
	{
		HandBallTypes[Arm] = Type;
		Ball->SetMaterial(0, ChaosImpactBallTypes::MakeEmissive(this, ChaosImpactBallTypes::GetColor(Type), Type == EChaosImpactBallType::Nova ? 3.0f : 2.2f));
	}
	Ball->SetVisibility(true);
}

void AChaosImpactPresidentBoss::HideHandBalls()
{
	for (int32 Arm = 0; Arm < HandBalls.Num(); ++Arm)
	{
		ShowHandBall(Arm, EChaosImpactBallType::Normal);
	}
}

// ---------------------------------------------------------------------------------------------------- Damage

float AChaosImpactPresidentBoss::TakeDamage(const float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator,
	AActor* DamageCauser)
{
	if (bDormant || bBeaten || Move == EChaosImpactPresidentMove::PowerUp || IsEliminated() || bSceneHold)
	{
		return 0.0f;
	}
	const float Half = MaxHealth * 0.5f;
	float Amount = DamageAmount;
	if (!bFullPower)
	{
		// The first half cannot be skipped: it always powers up on the way down.
		Amount = FMath::Min(Amount, FMath::Max(Health - Half, 0.0f));
	}
	if (Amount <= 0.0f)
	{
		return 0.0f;
	}
	if (bSceneDefeat && bFullPower && Health - Amount <= KINDA_SMALL_NUMBER)
	{
		// The last hit with a scene to follow: beaten, not knocked out.
		Super::TakeDamage(FMath::Max(Health - 0.001f, 0.0f), DamageEvent, EventInstigator, DamageCauser);
		bBeaten = true;
		bHarmless = true;
		bHoldMoves = true;
		AbortMove();
		SetCarForm(false);
		HitFlash = 1.0f;
		SetHover(0.0f, 0.5f);
		ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::PresStun, GetActorLocation());
		UE_LOG(LogChaosImpact, Log, TEXT("President boss beaten: its scene follows"));
		return Amount;
	}
	const float Applied = Super::TakeDamage(Amount, DamageEvent, EventInstigator, DamageCauser);
	if (Applied > 0.0f)
	{
		HitFlash = 1.0f;
	}
	if (!IsEliminated() && !bFullPower && Health <= Half + KINDA_SMALL_NUMBER)
	{
		StartPowerUp();
	}
	return Applied;
}

// ---------------------------------------------------------------------------------------------------- The fight

bool AChaosImpactPresidentBoss::IsFoe(const AChaosImpactCharacter* Character) const
{
	return IsValid(Character) && Character != this && !Character->IsEliminated() && !Character->IsHidden()
		&& !Cast<AChaosImpactPresidentBoss>(Character) && !AChaosImpactGameState::AreTeammates(GetWorld(), this, Character);
}

AChaosImpactCharacter* AChaosImpactPresidentBoss::FindTarget() const
{
	AChaosImpactCharacter* Best = nullptr;
	float BestDistance = TNumericLimits<float>::Max();
	for (TActorIterator<AChaosImpactCharacter> It(GetWorld()); It; ++It)
	{
		if (!IsFoe(*It))
		{
			continue;
		}
		const float Distance = FVector::Dist2D(It->GetActorLocation(), GetActorLocation()) + (It->IsPlayerControlled() ? 0.0f : 3000.0f);
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = *It;
		}
	}
	return Best;
}

const AChaosImpactSoloRoom* AChaosImpactPresidentBoss::FindHomeRoom() const
{
	if (!bLookedForRoom && GetWorld())
	{
		bLookedForRoom = true;
		for (TActorIterator<AChaosImpactSoloRoom> It(GetWorld()); It; ++It)
		{
			const FVector Local = It->RoomArea->GetComponentTransform().InverseTransformPositionNoScale(GetActorLocation());
			const FVector Extent = It->RoomArea->GetScaledBoxExtent();
			if (FMath::Abs(Local.X) <= Extent.X + 400.0f && FMath::Abs(Local.Y) <= Extent.Y + 400.0f)
			{
				HomeRoom = *It;
				break;
			}
		}
	}
	return HomeRoom.Get();
}

void AChaosImpactPresidentBoss::GetArena(FVector& OutCenter, FVector& OutExtent) const
{
	if (const AChaosImpactSoloRoom* Room = FindHomeRoom())
	{
		OutCenter = Room->RoomArea->GetComponentLocation();
		OutExtent = Room->RoomArea->GetScaledBoxExtent();
		return;
	}
	OutCenter = FVector(3000.0f, 0.0f, 0.0f);
	OutExtent = FVector(1900.0f, 1900.0f, 200.0f);
}

FVector AChaosImpactPresidentBoss::ArenaPoint(const FVector& Wanted, const float Margin) const
{
	FVector Center;
	FVector Extent;
	GetArena(Center, Extent);
	return FVector(FMath::Clamp(Wanted.X, Center.X - Extent.X + Margin, Center.X + Extent.X - Margin),
		FMath::Clamp(Wanted.Y, Center.Y - Extent.Y + Margin, Center.Y + Extent.Y - Margin), Wanted.Z);
}

FVector AChaosImpactPresidentBoss::KeepInArena(const AChaosImpactCharacter* Victim, const FVector& Knockback) const
{
	// Never thrown out of the arena: the push shortened to the room left (a fall's flight is about 0.7 s).
	FVector Out = Knockback;
	Out.Z = FMath::Min(Out.Z, 520.0f);
	FVector Flat(Out.X, Out.Y, 0.0f);
	const float Speed = Flat.Size();
	if (!Victim || Speed < 1.0f)
	{
		return Out;
	}
	FVector Center;
	FVector Extent;
	GetArena(Center, Extent);
	const FVector Direction = Flat / Speed;
	const FVector From = Victim->GetActorLocation();
	float Room = TNumericLimits<float>::Max();
	for (int32 Axis = 0; Axis < 2; ++Axis)
	{
		if (FMath::Abs(Direction[Axis]) > KINDA_SMALL_NUMBER)
		{
			const float Edge = Center[Axis] + (Direction[Axis] > 0.0f ? Extent[Axis] : -Extent[Axis]) - FMath::Sign(Direction[Axis]) * 180.0f;
			Room = FMath::Min(Room, FMath::Max((Edge - From[Axis]) / Direction[Axis], 0.0f));
		}
	}
	const float Travel = Speed * 0.7f;
	if (Travel > Room)
	{
		Flat = Direction * (Room / 0.7f);
	}
	return FVector(Flat.X, Flat.Y, Out.Z);
}

bool AChaosImpactPresidentBoss::StrikeFoe(AChaosImpactCharacter* Victim, const FVector& Knockback)
{
	if (!IsFoe(Victim) || Victim->IsDashing() || HitThisMove.Contains(Victim))
	{
		// A dash slips through untouched.
		return false;
	}
	HitThisMove.Add(Victim);
	if (!bHarmless)
	{
		Victim->TakeDamage(1.0f, FDamageEvent(), GetController(), this);
	}
	if (Victim->IsEliminated())
	{
		return true;
	}
	Victim->ApplyBlastKnockback(KeepInArena(Victim, Knockback));
	Victim->AddCameraShake(0.7f, 0.45f);
	return true;
}

void AChaosImpactPresidentBoss::ShakeNearby(const FVector& Where, const float Radius, const float Strength, const float Seconds) const
{
	for (TActorIterator<AChaosImpactCharacter> It(GetWorld()); It; ++It)
	{
		if (It->IsPlayerControlled() && It->IsLocallyControlled())
		{
			const float Near = 1.0f - FVector::Dist(It->GetActorLocation(), Where) / FMath::Max(Radius, 1.0f);
			if (Near > 0.0f)
			{
				It->AddCameraShake(Strength * (0.35f + 0.65f * Near), Seconds);
			}
		}
	}
}

void AChaosImpactPresidentBoss::TurnToward(const FVector& Direction, const float DeltaSeconds, const float DegreesPerSecond)
{
	const FVector Flat = Direction.GetSafeNormal2D();
	if (Flat.IsNearlyZero())
	{
		return;
	}
	const FRotator Now = FMath::RInterpConstantTo(GetActorRotation(), Flat.Rotation(), DeltaSeconds, DegreesPerSecond);
	SetActorRotation(FRotator(0.0f, Now.Yaw, 0.0f));
	SetAIAimDirection(Now.Vector());
}

void AChaosImpactPresidentBoss::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ShownHealth = ShownHealth < 0.0f ? Health : FMath::FInterpConstantTo(ShownHealth, Health, DeltaSeconds, MaxHealth * 0.25f);
	HitFlash = FMath::Max(HitFlash - DeltaSeconds / 0.25f, 0.0f);
	UpdateHover(DeltaSeconds);
	// The balls in its hands follow them.
	for (int32 Arm = 0; Arm < HandBalls.Num(); ++Arm)
	{
		if (HandBalls[Arm] && HandBalls[Arm]->IsVisible())
		{
			HandBalls[Arm]->SetWorldLocation(GetHand(Arm));
		}
	}
	if (HasAuthority())
	{
		// Powering up (and beaten, waiting for its scene): balls glance off it.
		const bool bShielded = Move == EChaosImpactPresidentMove::PowerUp || bBeaten;
		if (CanBeDamaged() == bShielded)
		{
			SetCanBeDamaged(!bShielded);
		}
	}
	if (bSceneHold || bBeaten || IsEliminated() || !HasAuthority())
	{
		return;
	}
	if (bDormant)
	{
		PlayClip(SitClip, true, 1.0f);
		return;
	}
	switch (Move)
	{
	case EChaosImpactPresidentMove::None: ThinkIdle(DeltaSeconds); break;
	case EChaosImpactPresidentMove::Throws: TickThrows(DeltaSeconds); break;
	case EChaosImpactPresidentMove::Beam: TickBeam(DeltaSeconds); break;
	case EChaosImpactPresidentMove::Car: TickCar(DeltaSeconds); break;
	case EChaosImpactPresidentMove::NovaStorm: TickNovas(DeltaSeconds); break;
	case EChaosImpactPresidentMove::Stunned: TickStunned(DeltaSeconds); break;
	case EChaosImpactPresidentMove::PowerUp: TickPowerUp(DeltaSeconds); break;
	}
}

void AChaosImpactPresidentBoss::ThinkIdle(const float DeltaSeconds)
{
	PlayClip(SitClip, true, 1.0f);
	AChaosImpactCharacter* Target = FindTarget();
	if (!Target)
	{
		return;
	}
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->MaxWalkSpeed = bFullPower ? 460.0f : 380.0f;
	FVector To = Target->GetActorLocation() - GetActorLocation();
	To.Z = 0.0f;
	const float Distance = To.Size();
	const FVector Toward = To.GetSafeNormal();
	TurnToward(Toward, DeltaSeconds, 140.0f);
	// Glides about at a commanding distance, drifting round its target.
	const float Wanted = bFullPower ? 760.0f : 900.0f;
	FVector Go = FVector::ZeroVector;
	if (Distance > Wanted + 300.0f)
	{
		Go = Toward;
	}
	else if (Distance < Wanted - 400.0f)
	{
		Go = -Toward * 0.6f;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now >= NextStrafeChange)
	{
		StrafeSign = FMath::RandBool() ? 1.0f : -1.0f;
		NextStrafeChange = Now + FMath::FRandRange(1.5f, 3.0f);
	}
	Go += FVector::CrossProduct(FVector::UpVector, Toward) * StrafeSign * 0.4f;
	// (It keeps to the arena's middle part: the far end of the hall behind it.)
	const FVector Stay = ArenaPoint(GetActorLocation() + Go * 200.0f, 450.0f);
	Go = (Stay - GetActorLocation()).GetSafeNormal2D() * FMath::Min(Go.Size(), 1.0f);
	AddMovementInput(Go.GetSafeNormal(), FMath::Clamp(Go.Size(), 0.0f, 1.0f));
	RestLeft -= bHoldMoves ? 0.0f : DeltaSeconds;
	if (RestLeft <= 0.0f)
	{
		ChooseMove();
	}
}

void AChaosImpactPresidentBoss::ChooseMove()
{
	TArray<TPair<EChaosImpactPresidentMove, float>> Options = {
		{EChaosImpactPresidentMove::Throws, 0.42f}, {EChaosImpactPresidentMove::Beam, 0.32f}, {EChaosImpactPresidentMove::Car, 0.26f}};
	if (bFullPower && MovesSinceNovas >= 3)
	{
		Options.Add({EChaosImpactPresidentMove::NovaStorm, 0.6f});
	}
	float Total = 0.0f;
	for (const TPair<EChaosImpactPresidentMove, float>& Option : Options)
	{
		Total += Option.Key == LastMove ? Option.Value * 0.2f : Option.Value;
	}
	float Roll = FMath::FRand() * Total;
	for (const TPair<EChaosImpactPresidentMove, float>& Option : Options)
	{
		Roll -= Option.Key == LastMove ? Option.Value * 0.2f : Option.Value;
		if (Roll <= 0.0f)
		{
			BeginMove(Option.Key);
			return;
		}
	}
	BeginMove(EChaosImpactPresidentMove::Throws);
}

void AChaosImpactPresidentBoss::ForceMove(const EChaosImpactPresidentMove NewMove)
{
	if (IsEliminated() || bDormant)
	{
		return;
	}
	AbortMove();
	if (NewMove == EChaosImpactPresidentMove::PowerUp)
	{
		StartPowerUp();
		return;
	}
	if (NewMove == EChaosImpactPresidentMove::None)
	{
		Move = EChaosImpactPresidentMove::None;
		RestLeft = 1000.0f;
		return;
	}
	BeginMove(NewMove);
}

void AChaosImpactPresidentBoss::BeginMove(const EChaosImpactPresidentMove NewMove)
{
	Move = NewMove;
	MoveTime = 0.0f;
	MoveStage = 0;
	Shots = 0;
	HitThisMove.Reset();
	ShakeOffMoveEffects();
	GetCharacterMovement()->StopMovementImmediately();
	MoveTarget = FindTarget();
	const AChaosImpactCharacter* Target = MoveTarget.Get();
	MoveDirection = Target ? (Target->GetActorLocation() - GetActorLocation()).GetSafeNormal2D() : GetActorForwardVector();
	UE_LOG(LogChaosImpact, Log, TEXT("President boss: %s (health %.0f%s)"), *UEnum::GetValueAsString(NewMove), Health,
		bFullPower ? TEXT(", full power") : TEXT(""));
	switch (NewMove)
	{
	case EChaosImpactPresidentMove::Throws:
	{
		// Six kinds, every one different, one from each hand in turn (right and left alternating, top to bottom). (Only kinds
		// that can be thrown down in an arc: a thunder or beam ball always flies level, over the head from its hands' height.)
		ThrowKinds = {EChaosImpactBallType::Fire, EChaosImpactBallType::Ice, EChaosImpactBallType::Normal, EChaosImpactBallType::Black,
			EChaosImpactBallType::Smoke, EChaosImpactBallType::Snow, EChaosImpactBallType::Simae};
		if (bHarmless)
		{
			// (Tests: none of the kinds that hurt on their own, lying about or chasing.)
			ThrowKinds = {EChaosImpactBallType::Ice, EChaosImpactBallType::Normal, EChaosImpactBallType::Smoke, EChaosImpactBallType::Normal,
				EChaosImpactBallType::Snow, EChaosImpactBallType::Normal};
		}
		for (int32 Index = ThrowKinds.Num() - 1; Index > 0; --Index)
		{
			ThrowKinds.Swap(Index, FMath::RandRange(0, Index));
		}
		ThrowKinds.SetNum(6);
		ArmOrder = {0, 3, 1, 4, 2, 5};
		for (int32 Index = 0; Index < 6; ++Index)
		{
			ShowHandBall(ArmOrder[Index], ThrowKinds[Index]);
		}
		ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::HallBallLight, GetActorLocation());
		break;
	}
	case EChaosImpactPresidentMove::Beam:
		SetHover(40.0f, 0.4f);
		PlayClip(BeamStartClip, false, 1.0f);
		ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::PresHatch, GetActorLocation());
		break;
	case EChaosImpactPresidentMove::Car:
		break;
	case EChaosImpactPresidentMove::NovaStorm:
		MovesSinceNovas = 0;
		NovaShots.Reset();
		SetHover(360.0f, 0.9f);
		for (int32 Arm = 0; Arm < 6; ++Arm)
		{
			ShowHandBall(Arm, EChaosImpactBallType::Nova);
		}
		ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::PresNovaCharge, GetActorLocation());
		break;
	default:
		break;
	}
}

void AChaosImpactPresidentBoss::AbortMove()
{
	if (AChaosImpactSimaeWarning* Lane = Warning.Get())
	{
		Lane->Finish();
	}
	Warning.Reset();
	for (FNovaShot& Shot : NovaShots)
	{
		if (Shot.Warning.IsValid())
		{
			Shot.Warning->Finish();
		}
		ClearNovaFall(Shot);
	}
	NovaShots.Reset();
	HideHandBalls();
	UpdateBeamLook(0.0f);
	if (Move == EChaosImpactPresidentMove::Car)
	{
		SetCarForm(false);
		Car->SetVisibility(false);
		Car2->SetVisibility(false);
		CarLights->SetIntensity(0.0f);
		SetActorEnableCollision(true);
		GetMesh()->SetVisibility(true);
		for (FCarPass& Pass : CarPasses)
		{
			if (Pass.Warning.IsValid())
			{
				Pass.Warning->Finish();
			}
		}
		CarPasses.Reset();
		if (AChaosImpactCharacter* Target = MoveTarget.Get())
		{
			Target->SetStageOverheadView(false);
			Target->SetStageCameraScale(PresidentCameraScale);
		}
	}
}

void AChaosImpactPresidentBoss::EndMove(const float Rest)
{
	AbortMove();
	if (Move != EChaosImpactPresidentMove::PowerUp && Move != EChaosImpactPresidentMove::Stunned)
	{
		LastMove = Move;
		++MovesSinceNovas;
	}
	Move = EChaosImpactPresidentMove::None;
	RestLeft = Rest / Tempo() * FMath::FRandRange(0.85f, 1.15f);
	SetHover(IdleHover, 0.5f);
	PlayClip(SitClip, true, 1.0f);
}

// ---- 6連投

void AChaosImpactPresidentBoss::TickThrows(const float DeltaSeconds)
{
	MoveTime += DeltaSeconds * Tempo();
	if (AChaosImpactCharacter* Target = MoveTarget.Get())
	{
		TurnToward(Target->GetActorLocation() - GetActorLocation(), DeltaSeconds, 240.0f);
	}
	// A beat to show the six balls, then one every ThrowInterval: the wind-up (that arm's clip), its ball let go.
	constexpr float Lead = 0.55f;
	constexpr float ReleaseIn = 0.3f;
	const int32 Due = MoveTime < Lead ? -1 : static_cast<int32>((MoveTime - Lead) / ThrowInterval);
	if (Due >= 0 && Due < 6 && Shots == Due * 2)
	{
		++Shots;
		const int32 Arm = ArmOrder[Due];
		const float Length = ThrowClips[Arm] ? ThrowClips[Arm]->GetPlayLength() : 1.0f;
		PlayClip(ThrowClips[Arm], false, FMath::Max(Length / (ThrowInterval * 1.6f), 1.0f) * Tempo());
	}
	if (Due >= 0 && Due < 6 && Shots == Due * 2 + 1 && MoveTime >= Lead + Due * ThrowInterval + ReleaseIn)
	{
		++Shots;
		ThrowFrom(ArmOrder[Due]);
	}
	if (MoveTime >= Lead + 6 * ThrowInterval + 0.5f)
	{
		EndMove(1.6f);
	}
}

void AChaosImpactPresidentBoss::ThrowFrom(const int32 Arm)
{
	UWorld* World = GetWorld();
	const int32 Index = ArmOrder.IndexOfByKey(Arm);
	const EChaosImpactBallType Type = ThrowKinds.IsValidIndex(Index) ? ThrowKinds[Index] : EChaosImpactBallType::Normal;
	ShowHandBall(Arm, EChaosImpactBallType::Normal);
	const FVector From = GetHand(Arm);
	const AChaosImpactCharacter* Target = MoveTarget.Get();
	// Every other one straight at them, the rest at where they are going (stood still, every one hits; on the move, a
	// change of way slips them).
	const bool bLead = Index % 2 == 1;
	const float FlightGuess = Target ? static_cast<float>(FVector::Dist2D(Target->GetActorLocation(), From)) / ThrowSpeed : 0.0f;
	FVector Aim = Target ? Target->GetActorLocation() + (bLead ? Target->GetVelocity() * FVector(1.0f, 1.0f, 0.0f) * FlightGuess : FVector::ZeroVector)
		: From + GetActorForwardVector() * 1000.0f;
	// Down at them from its hands, in an arc that comes to their middle where they are (or will be): a ball flies level
	// unless thrown in an arc, so its rise (or drop) is worked out from its fall over the time it takes to get there.
	// (At their waist: coming down onto the top of their head it would count as landing on the floor, not as a hit.)
	Aim.Z = Target ? Target->GetActorLocation().Z - 35.0f : From.Z - 300.0f;
	FVector Flat = Aim - From;
	Flat.Z = 0.0f;
	const float Reach = FMath::Max(static_cast<float>(Flat.Size()), 100.0f);
	const FVector Dir = Flat.GetSafeNormal().IsNearlyZero() ? GetActorForwardVector() : Flat.GetSafeNormal();
	const float Speed = ThrowSpeed * (bFullPower ? 1.12f : 1.0f);
	const FTransform At(Dir.Rotation(), From + Dir * 40.0f);
	AChaosImpactBall* Ball = World->SpawnActorDeferred<AChaosImpactBall>(AChaosImpactBall::StaticClass(), At, this, this,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Ball)
	{
		Ball->SetBallType(Type);
		Ball->FinishSpawning(At);
		const float Time = FMath::Max((Reach - 40.0f) / Speed, 0.2f);
		const float Gravity = FMath::Abs(World->GetGravityZ()) * Ball->GetFlightGravityScale();
		const float Drop = static_cast<float>(Aim.Z - At.GetLocation().Z);
		const float Rise = (Drop + 0.5f * Gravity * Time * Time) / Time;
		Ball->Launch(Dir, Speed, EChaosImpactBallFlightMode::Arc, Rise);
	}
	ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::PresThrow, From);
	if (AChaosImpactFxKit* Kit = GetFx())
	{
		FChaosImpactFxPiece Flash;
		Flash.Location = From - Kit->GetActorLocation();
		Flash.Life = 0.18f;
		Flash.SizeFrom = 140.0f;
		Flash.SizeTo = 40.0f;
		Flash.Color = ChaosImpactBallTypes::GetColor(Type);
		Flash.CoreColor = FLinearColor::White;
		Flash.Intensity = 2.0f;
		Kit->Add(Flash);
	}
}

// ---- お腹ビーム

void AChaosImpactPresidentBoss::UpdateBeamLook(const float Shown)
{
	if (Shown <= 0.01f)
	{
		BeamCore->SetVisibility(false);
		BeamGlow->SetVisibility(false);
		BeamLight->SetIntensity(0.0f);
		return;
	}
	const USkeletalMeshComponent* Body = GetMesh();
	const FName Belly = FindBone(Body, TEXT("Spine"));
	const FVector Origin = (Belly.IsNone() ? GetActorLocation() + FVector(0.0f, 0.0f, 200.0f) : Body->GetSocketLocation(Belly))
		+ GetActorForwardVector() * 70.0f * (ModelScale / 3.0f + 0.67f);
	// Down onto the floor at its far end (it sweeps across the arena at chest height of anyone in it).
	const FVector Ground = ChaosImpactSimaeFx::FindGround(GetWorld(), GetActorLocation(), this);
	FVector End = Ground + MoveDirection * BeamRange + FVector(0.0f, 0.0f, 40.0f);
	FHitResult Hit;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(PresidentBeam), false, this);
	if (GetWorld()->LineTraceSingleByChannel(Hit, Origin, End, ECC_WorldStatic, Query))
	{
		End = Hit.ImpactPoint;
	}
	const FVector Way = End - Origin;
	const float Length = static_cast<float>(Way.Size());
	const FRotator Turn = FRotationMatrix::MakeFromZ(Way.GetSafeNormal()).Rotator();
	const float Time = GetWorld()->GetTimeSeconds();
	const float Pulse = 1.0f + 0.12f * FMath::Sin(Time * 38.0f) + 0.06f * FMath::Sin(Time * 71.0f);
	BeamCore->SetVisibility(true);
	BeamGlow->SetVisibility(true);
	BeamCore->SetWorldLocationAndRotation(Origin + Way * 0.5f, Turn);
	BeamGlow->SetWorldLocationAndRotation(Origin + Way * 0.5f, Turn);
	BeamCore->SetWorldScale3D(FVector(0.75f * Shown * Pulse, 0.75f * Shown * Pulse, Length / 100.0f));
	BeamGlow->SetWorldScale3D(FVector(2.1f * Shown * Pulse, 2.1f * Shown * Pulse, Length / 100.0f));
	BeamLight->SetWorldLocation(Origin + Way * 0.35f);
	BeamLight->SetIntensity(40000.0f * Shown);
	if (AChaosImpactFxKit* Kit = GetFx(); Kit && FMath::Fmod(Time, 0.08f) < 0.02f)
	{
		// Where it strikes: a hot glow and sparks; at its mouth a glare.
		FChaosImpactFxPiece Strike;
		Strike.Location = End - Kit->GetActorLocation();
		Strike.Life = 0.16f;
		Strike.SizeFrom = 260.0f;
		Strike.SizeTo = 120.0f;
		Strike.Color = FLinearColor(1.0f, 0.3f, 0.3f);
		Strike.CoreColor = FLinearColor(1.0f, 0.95f, 0.85f);
		Strike.Intensity = 2.4f;
		Kit->Add(Strike);
		FChaosImpactFxPiece Spark;
		Spark.Look = EChaosImpactFxLook::Streak;
		Spark.Location = Strike.Location;
		Spark.Velocity = (FMath::VRand() + FVector(0.0f, 0.0f, 1.0f)) * 700.0f;
		Spark.Gravity = 1200.0f;
		Spark.Life = 0.35f;
		Spark.SizeFrom = 60.0f;
		Spark.SizeTo = 15.0f;
		Spark.Width = 4.0f;
		Spark.Color = FLinearColor(1.0f, 0.5f, 0.3f);
		Spark.CoreColor = FLinearColor(1.0f, 0.95f, 0.8f);
		Spark.Intensity = 3.0f;
		Kit->Add(Spark);
		FChaosImpactFxPiece Mouth = Strike;
		Mouth.Location = Origin - Kit->GetActorLocation();
		Mouth.SizeFrom = 320.0f;
		Mouth.SizeTo = 200.0f;
		Kit->Add(Mouth);
	}
}

void AChaosImpactPresidentBoss::TickBeam(const float DeltaSeconds)
{
	UWorld* World = GetWorld();
	MoveTime += DeltaSeconds;
	AChaosImpactCharacter* Target = MoveTarget.Get();
	const FVector Ground = ChaosImpactSimaeFx::FindGround(World, GetActorLocation(), this);
	if (MoveStage == 0)
	{
		// The hatch opens, the line shows where it will fire (following its target, then holding still for the last moment).
		if (Target && MoveTime < BeamWarnSeconds * 0.7f)
		{
			MoveDirection = (Target->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
		}
		TurnToward(MoveDirection, DeltaSeconds, 360.0f);
		if (!Warning.IsValid() && MoveTime > 0.05f)
		{
			Warning = AChaosImpactSimaeWarning::SpawnLane(World, Ground, MoveDirection, BeamRange, BeamHalfWidth, BeamWarnSeconds - 0.05f, PresWarning);
			ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::PresBeamCharge, GetActorLocation());
		}
		if (AChaosImpactSimaeWarning* Lane = Warning.Get())
		{
			Lane->SetLane(Ground, MoveDirection, BeamRange);
		}
		if (MoveTime >= BeamWarnSeconds)
		{
			MoveStage = 1;
			MoveTime = 0.0f;
			if (AChaosImpactSimaeWarning* Lane = Warning.Get())
			{
				Lane->Finish();
			}
			Warning.Reset();
			PlayClip(BeamFireClip ? BeamFireClip.Get() : BeamStartClip.Get(), BeamFireClip != nullptr, BeamFireClip ? 1.0f : 0.0f);
			ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::PresBeamFire, GetActorLocation());
			ShakeNearby(GetActorLocation(), 5000.0f, 0.5f, BeamFireSeconds);
		}
		return;
	}
	if (MoveStage == 1)
	{
		// Firing: it sweeps slowly after its target.
		if (Target)
		{
			const FVector Want = (Target->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
			const float Rate = FMath::DegreesToRadians(BeamTurnDegrees * Tempo()) * DeltaSeconds;
			const float Angle = FMath::Atan2(FVector::CrossProduct(MoveDirection, Want).Z, FVector::DotProduct(MoveDirection, Want));
			MoveDirection = MoveDirection.RotateAngleAxis(FMath::RadiansToDegrees(FMath::Clamp(Angle, -Rate, Rate)), FVector::UpVector);
			SetActorRotation(FRotator(0.0f, MoveDirection.Rotation().Yaw, 0.0f));
		}
		const float Shown = FMath::Min(MoveTime / 0.12f, 1.0f) * FMath::Clamp((BeamFireSeconds - MoveTime) / 0.2f, 0.0f, 1.0f);
		UpdateBeamLook(Shown);
		// Anyone on its line beyond its own reach is struck, once (a dash goes through).
		const FVector From = Ground + MoveDirection * 350.0f;
		const FVector To = Ground + MoveDirection * BeamRange;
		for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
		{
			AChaosImpactCharacter* Victim = *It;
			if (IsFoe(Victim) && SegmentDistance2D(Victim->GetActorLocation(), From, To) < BeamHalfWidth + 40.0f)
			{
				const FVector Side = FVector::CrossProduct(FVector::UpVector, MoveDirection);
				const float Which = FVector::DotProduct(Victim->GetActorLocation() - From, Side) >= 0.0f ? 1.0f : -1.0f;
				StrikeFoe(Victim, Side * Which * 900.0f + MoveDirection * 300.0f + FVector(0.0f, 0.0f, 420.0f));
			}
		}
		if (MoveTime >= BeamFireSeconds)
		{
			MoveStage = 2;
			MoveTime = 0.0f;
			UpdateBeamLook(0.0f);
			PlayClip(BeamEndClip, false, 1.0f);
			ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::PresBeamEnd, GetActorLocation());
		}
		return;
	}
	if (MoveTime >= 1.1f)
	{
		EndMove(1.8f);
	}
}

// ---- 車突進 (2026-11-19, the user's: seen from straight above, car after car down the hall, dodged to the side)

void AChaosImpactPresidentBoss::SetCarForm(const bool bCar)
{
	USkeletalMeshComponent* Body = GetMesh();
	USkeletalMesh* Want = bCar && CarFormModel ? CarFormModel.Get() : BodyModel.Get();
	if (Want && Body->GetSkeletalMeshAsset() != Want)
	{
		Body->SetSkeletalMeshAsset(Want);
		PlayingClip = nullptr;
		if (!bCar)
		{
			PlayClip(SitClip, true, 1.0f);
		}
	}
}

void AChaosImpactPresidentBoss::PlaceCar(UStaticMeshComponent* Which, const FVector& At, const FVector& Facing) const
{
	const float FloorZ = ChaosImpactSimaeFx::FindGround(GetWorld(), At + FVector(0.0f, 0.0f, 300.0f), this).Z;
	Which->SetWorldLocationAndRotation(FVector(At.X, At.Y, FloorZ + 3.0f), FRotator(0.0f, Facing.Rotation().Yaw, 0.0f));
}

void AChaosImpactPresidentBoss::CarHits(UStaticMeshComponent* Which, FCarPass& Pass)
{
	// Whoever the car runs into is bowled aside (once a car; a dash goes through).
	const FVector Forward = Which->GetForwardVector().GetSafeNormal2D();
	const FVector At = Which->GetComponentLocation();
	for (TActorIterator<AChaosImpactCharacter> It(GetWorld()); It; ++It)
	{
		AChaosImpactCharacter* Victim = *It;
		if (!IsFoe(Victim) || Pass.Hit.Contains(Victim)
			|| SegmentDistance2D(Victim->GetActorLocation(), At - Forward * 200.0f, At + Forward * 200.0f) > CarHalfWidth + 40.0f)
		{
			continue;
		}
		Pass.Hit.Add(Victim);
		HitThisMove.Remove(Victim);
		const FVector Side = FVector::CrossProduct(FVector::UpVector, Forward);
		const float Which2 = FVector::DotProduct(Victim->GetActorLocation() - At, Side) >= 0.0f ? 1.0f : -1.0f;
		if (StrikeFoe(Victim, Forward * 500.0f + Side * Which2 * 900.0f + FVector(0.0f, 0.0f, 450.0f)))
		{
			ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::PresCarHorn, At);
		}
	}
}

void AChaosImpactPresidentBoss::TickCar(const float DeltaSeconds)
{
	UWorld* World = GetWorld();
	MoveTime += DeltaSeconds;
	FVector Center;
	FVector Extent;
	GetArena(Center, Extent);
	AChaosImpactCharacter* Target = MoveTarget.Get();
	// The cars come down the hall (from the throne's end toward the doors), the player dodging to the side.
	const FVector Down(-1.0f, 0.0f, 0.0f);
	const float TopX = Center.X + Extent.X + 500.0f;
	switch (MoveStage)
	{
	case 0:
	{
		// The change: the boss folds into its car; the view goes straight overhead (as a nova's aim shows the stage).
		if (MoveTime <= DeltaSeconds + KINDA_SMALL_NUMBER)
		{
			SetHover(0.0f, 0.3f);
			SetCarForm(true);
			const float Length = CarStartClip ? CarStartClip->GetPlayLength() : 1.0f;
			PlayClip(CarStartClip, false, FMath::Max(Length / 1.4f, 0.1f));
			ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::PresCarTransform, GetActorLocation());
			if (Target)
			{
				Target->SetStageOverheadView(true, Center, FMath::Max(Extent.X, Extent.Y) + 250.0f);
			}
		}
		if (MoveTime >= 1.4f)
		{
			// Off it drives, up the hall and out of sight.
			MoveStage = 1;
			MoveTime = 0.0f;
			GetMesh()->SetVisibility(false);
			SetActorEnableCollision(false);
			Car->SetVisibility(true);
			CarLights->SetIntensity(9000.0f);
			CarStart = GetActorLocation();
			CarEnd = FVector(TopX, GetActorLocation().Y, GetActorLocation().Z);
			PlaceCar(Car, CarStart, -Down);
			ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::PresCarRev, GetActorLocation());
		}
		break;
	}
	case 1:
	{
		const float U = FMath::Clamp(MoveTime / 0.6f, 0.0f, 1.0f);
		PlaceCar(Car, FMath::Lerp(CarStart, CarEnd, U * U), -Down);
		if (U >= 1.0f)
		{
			// Its runs: each along a lane through where the player is (a step to the side gets clear), one after another. The
			// lanes' ways by this move's pattern (never the same pattern twice running): down the hall, across it, slantwise,
			// any way, or two at once crossing (step off slantwise).
			MoveStage = 2;
			MoveTime = 0.0f;
			Car->SetVisibility(false);
			CarLights->SetIntensity(0.0f);
			CarPasses.Reset();
			do
			{
				CarPattern = FMath::RandRange(0, 4);
			}
			while (CarPattern == LastCarPattern);
			LastCarPattern = CarPattern;
			const bool bPairs = CarPattern == 4;
			const int32 Count = bFullPower ? FullPowerCarPassCount : CarPassCount;
			const float Turn = FMath::RandBool() ? 1.0f : -1.0f;
			float LastYaw = 1000.0f;
			for (int32 Index = 0; Index < Count; ++Index)
			{
				FCarPass Pass;
				const int32 Beat = bPairs ? Index / 2 : Index;
				Pass.WarnAt = 0.2f + Beat * (bPairs ? 1.6f : 1.0f) * CarPassInterval / Tempo();
				Pass.RushAt = Pass.WarnAt + CarWarnSeconds / Tempo();
				Pass.CarIndex = Index % 2;
				float Yaw = 180.0f;
				switch (CarPattern)
				{
				case 0:
					// Down the hall (now and then back up it).
					Yaw = Index % 3 == 2 ? 0.0f : 180.0f;
					break;
				case 1:
					// Across, from either side in turn.
					Yaw = Index % 2 == 0 ? 90.0f : -90.0f;
					break;
				case 2:
					// Slantwise, the slant turning.
					Yaw = (Index % 2 == 0 ? 135.0f : -135.0f) + (Index % 4 >= 2 ? 90.0f * Turn : 0.0f);
					break;
				case 3:
					// Any of the eight ways (never the last one's again).
					do
					{
						Yaw = FMath::RandRange(0, 7) * 45.0f;
					}
					while (FMath::Abs(FRotator::NormalizeAxis(Yaw - LastYaw)) < 1.0f);
					break;
				default:
					// Two at once: one down the hall, one across it.
					Yaw = Index % 2 == 0 ? 180.0f : (Beat % 2 == 0 ? 90.0f : -90.0f);
					break;
				}
				LastYaw = Yaw;
				Pass.Dir = FRotator(0.0f, Yaw, 0.0f).Vector();
				CarPasses.Add(Pass);
			}
		}
		break;
	}
	case 2:
	{
		bool bAllDone = true;
		for (FCarPass& Pass : CarPasses)
		{
			UStaticMeshComponent* Which = Pass.CarIndex == 0 ? Car.Get() : Car2.Get();
			if (!Pass.bWarned && MoveTime >= Pass.WarnAt)
			{
				// Its lane: through where the player is now (a little either way), never just where another running the same
				// way is; on the floor from one side of the arena to the other, the car coming in from out of sight.
				Pass.bWarned = true;
				const FVector Side = FVector::CrossProduct(FVector::UpVector, Pass.Dir);
				FVector Through = Target ? Target->GetActorLocation() + Side * FMath::FRandRange(-110.0f, 110.0f) : Center;
				for (const FCarPass& Other : CarPasses)
				{
					if (&Other != &Pass && Other.bWarned && !Other.bDone && FMath::Abs(FVector::DotProduct(Other.Dir, Pass.Dir)) > 0.9f)
					{
						const float Apart = FVector::DotProduct(Through - Other.Through, Side);
						if (FMath::Abs(Apart) < CarHalfWidth * 2.0f + 60.0f)
						{
							Through += Side * ((Apart >= 0.0f ? 1.0f : -1.0f) * (CarHalfWidth * 2.0f + 80.0f) - Apart);
						}
					}
				}
				Through.X = FMath::Clamp(Through.X, Center.X - Extent.X + CarHalfWidth, Center.X + Extent.X - CarHalfWidth);
				Through.Y = FMath::Clamp(Through.Y, Center.Y - Extent.Y + CarHalfWidth, Center.Y + Extent.Y - CarHalfWidth);
				Through.Z = GetActorLocation().Z;
				Pass.Through = Through;
				// (Where its line meets the arena's sides.)
				float Enter = -1.0e6f;
				float Leave = 1.0e6f;
				for (int32 Axis = 0; Axis < 2; ++Axis)
				{
					const float D = static_cast<float>(Pass.Dir[Axis]);
					const float Off = static_cast<float>(Through[Axis] - Center[Axis]);
					if (FMath::Abs(D) > KINDA_SMALL_NUMBER)
					{
						const float T1 = (static_cast<float>(-Extent[Axis]) - Off) / D;
						const float T2 = (static_cast<float>(Extent[Axis]) - Off) / D;
						Enter = FMath::Max(Enter, FMath::Min(T1, T2));
						Leave = FMath::Min(Leave, FMath::Max(T1, T2));
					}
				}
				Pass.From = Through + Pass.Dir * (Enter - 500.0f);
				Pass.To = Through + Pass.Dir * (Leave + 500.0f);
				const FVector Floor = ChaosImpactSimaeFx::FindGround(World, Through, this);
				const FVector Edge = Through + Pass.Dir * Enter;
				Pass.Warning = AChaosImpactSimaeWarning::SpawnLane(World, FVector(Edge.X, Edge.Y, Floor.Z), Pass.Dir, Leave - Enter,
					CarHalfWidth, CarWarnSeconds / Tempo(), PresWarning);
				ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::PresCarRev, FVector(Edge.X, Edge.Y, Floor.Z), 0.7f);
			}
			if (Pass.bWarned && !Pass.bRushing && !Pass.bDone && MoveTime >= Pass.RushAt)
			{
				Pass.bRushing = true;
				if (Pass.Warning.IsValid())
				{
					Pass.Warning->Finish();
				}
				Which->SetVisibility(true);
				PlaceCar(Which, Pass.From, Pass.Dir);
				ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::PresCarRush, Pass.Through);
			}
			if (Pass.bRushing && !Pass.bDone)
			{
				const float U = (MoveTime - Pass.RushAt) * CarSpeed * Tempo() / FMath::Max(static_cast<float>(FVector::Dist2D(Pass.From, Pass.To)), 1.0f);
				PlaceCar(Which, FMath::Lerp(Pass.From, Pass.To, FMath::Min(U, 1.0f)), Pass.Dir);
				CarHits(Which, Pass);
				if (AChaosImpactFxKit* Kit = GetFx(); Kit && FMath::Fmod(MoveTime, 0.05f) < DeltaSeconds)
				{
					// Speed: streaks behind it.
					FChaosImpactFxPiece Trail;
					Trail.Look = EChaosImpactFxLook::Streak;
					Trail.Location = Which->GetComponentLocation() - Kit->GetActorLocation() - Pass.Dir * FMath::FRandRange(150.0f, 300.0f)
						+ FVector::CrossProduct(FVector::UpVector, Pass.Dir) * FMath::FRandRange(-90.0f, 90.0f) + FVector(0.0f, 0.0f, FMath::FRandRange(20.0f, 120.0f));
					Trail.Velocity = -Pass.Dir * 300.0f;
					Trail.Life = 0.22f;
					Trail.SizeFrom = 380.0f;
					Trail.SizeTo = 120.0f;
					Trail.Width = 5.0f;
					Trail.Color = FLinearColor(1.0f, 0.85f, 0.6f);
					Trail.CoreColor = FLinearColor::White;
					Trail.Intensity = 1.6f;
					Kit->Add(Trail);
				}
				if (U >= 1.0f)
				{
					Pass.bDone = true;
					Which->SetVisibility(false);
				}
			}
			bAllDone &= Pass.bDone;
		}
		if (bAllDone)
		{
			// Back in down the hall, slowing to a stop, and out of the car.
			MoveStage = 3;
			MoveTime = 0.0f;
			CarStart = FVector(TopX, Center.Y, GetActorLocation().Z);
			CarComeBack = ArenaPoint(FVector(Center.X + Extent.X * 0.45f, Center.Y + FMath::FRandRange(-600.0f, 600.0f), 0.0f), 500.0f);
			Car->SetVisibility(true);
			CarLights->SetIntensity(9000.0f);
		}
		break;
	}
	case 3:
	{
		const float U = FMath::Clamp(MoveTime / 0.8f, 0.0f, 1.0f);
		const float Eased = 1.0f - FMath::Square(1.0f - U);
		const FVector Way = (CarComeBack - CarStart).GetSafeNormal2D();
		PlaceCar(Car, FMath::Lerp(CarStart, CarComeBack, Eased), Way.IsNearlyZero() ? Down : Way);
		if (U >= 1.0f)
		{
			MoveStage = 4;
			MoveTime = 0.0f;
			SetActorLocation(FVector(CarComeBack.X, CarComeBack.Y, GetActorLocation().Z), false, nullptr, ETeleportType::TeleportPhysics);
			SetActorRotation(FRotator(0.0f, 180.0f, 0.0f));
			Car->SetVisibility(false);
			CarLights->SetIntensity(0.0f);
			GetMesh()->SetVisibility(true);
			SetActorEnableCollision(true);
			const float Length = CarEndClip ? CarEndClip->GetPlayLength() : 1.0f;
			PlayClip(CarEndClip, false, FMath::Max(Length / 1.2f, 0.1f));
			ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::PresCarTransform, GetActorLocation(), 0.8f, 1.1f);
		}
		break;
	}
	default:
		if (MoveTime >= 1.25f)
		{
			if (Target)
			{
				Target->SetStageOverheadView(false);
				Target->SetStageCameraScale(PresidentCameraScale);
			}
			CarPasses.Reset();
			SetCarForm(false);
			EndMove(1.8f);
		}
		break;
	}
}

// ---- 6連ノヴァ

void AChaosImpactPresidentBoss::ClearNovaFall(FNovaShot& Shot)
{
	if (USceneComponent* Fall = Shot.Fall.Get())
	{
		TArray<USceneComponent*> Parts;
		Fall->GetChildrenComponents(true, Parts);
		for (USceneComponent* Part : Parts)
		{
			Part->DestroyComponent();
		}
		Fall->DestroyComponent();
	}
	Shot.Fall.Reset();
	Shot.FallLook = ChaosImpactBallTypes::FNovaLook();
}

void AChaosImpactPresidentBoss::DropNova(const FVector& Spot)
{
	ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::PresNovaBlast, Spot);
	ShakeNearby(Spot, NovaRadius * 6.0f, 0.55f, 0.4f);
	for (TActorIterator<AChaosImpactCharacter> It(GetWorld()); It; ++It)
	{
		AChaosImpactCharacter* Victim = *It;
		if (IsFoe(Victim) && FVector::Dist2D(Victim->GetActorLocation(), Spot) < NovaRadius + 35.0f
			&& FMath::Abs(Victim->GetActorLocation().Z - Spot.Z) < 400.0f)
		{
			HitThisMove.Remove(Victim);
			FVector Out = (Victim->GetActorLocation() - Spot).GetSafeNormal2D();
			if (Out.IsNearlyZero())
			{
				Out = FVector::ForwardVector;
			}
			StrikeFoe(Victim, Out * 650.0f + FVector(0.0f, 0.0f, 380.0f));
		}
	}
	AChaosImpactFxKit* Kit = AChaosImpactFxKit::Spawn(this, Spot);
	if (!Kit)
	{
		return;
	}
	// A small nova's burst: a quick ball of blue light no wider than its reach, rings out to its edge, sparks.
	FChaosImpactFxPiece Ball;
	Ball.Location = FVector(0.0f, 0.0f, 60.0f);
	Ball.Life = 0.3f;
	Ball.SizeFrom = 180.0f;
	Ball.SizeTo = NovaRadius * 2.1f;
	Ball.Color = NovaBlue;
	Ball.CoreColor = NovaCore;
	Ball.Intensity = 2.6f;
	Kit->Add(Ball);
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FChaosImpactFxPiece Ring;
		Ring.Look = EChaosImpactFxLook::SolidRing;
		Ring.Location = FVector(0.0f, 0.0f, 5.0f);
		Ring.Delay = Index * 0.06f;
		Ring.Life = 0.35f;
		Ring.SizeFrom = 80.0f;
		Ring.SizeTo = NovaRadius * 2.0f / 0.92f;
		Ring.RingFrom = 0.92f;
		Ring.RingTo = 0.92f;
		Ring.RingWidth = 0.05f;
		Ring.Color = NovaBlue;
		Ring.CoreColor = NovaCore;
		Ring.Intensity = 1.6f;
		Ring.Opacity = 0.9f;
		Ring.NoiseAmount = 0.0f;
		Kit->Add(Ring);
	}
	for (int32 Index = 0; Index < 14; ++Index)
	{
		const float Angle = UE_TWO_PI * Index / 14.0f;
		FChaosImpactFxPiece Spark;
		Spark.Look = EChaosImpactFxLook::Streak;
		Spark.Location = FVector(0.0f, 0.0f, 30.0f);
		Spark.Velocity = FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * FMath::FRandRange(500.0f, 800.0f) + FVector(0.0f, 0.0f, 300.0f);
		Spark.Gravity = 1000.0f;
		Spark.Drag = 2.0f;
		Spark.Life = 0.35f;
		Spark.SizeFrom = 70.0f;
		Spark.SizeTo = 20.0f;
		Spark.Width = 4.0f;
		Spark.Color = NovaBlue;
		Spark.CoreColor = NovaCore;
		Spark.Intensity = 3.0f;
		Kit->Add(Spark);
	}
	Kit->SetLifeSpan(2.0f);
}

void AChaosImpactPresidentBoss::TickNovas(const float DeltaSeconds)
{
	UWorld* World = GetWorld();
	MoveTime += DeltaSeconds;
	AChaosImpactCharacter* Target = MoveTarget.Get();
	if (Target)
	{
		TurnToward(Target->GetActorLocation() - GetActorLocation(), DeltaSeconds, 120.0f);
	}
	constexpr float Charge = 1.2f;
	const int32 Launch = MoveTime < Charge ? -1 : static_cast<int32>((MoveTime - Charge) / NovaInterval);
	if (Launch >= 0 && Launch < NovaCount && Shots == Launch)
	{
		++Shots;
		// Its next spot: where its target will be, never on top of a warning still showing (they never overlap).
		FVector Spot = Target ? Target->GetActorLocation() + Target->GetVelocity() * FVector(1.0f, 1.0f, 0.0f) * 0.45f : GetActorLocation();
		Spot = ArenaPoint(Spot, NovaRadius + 60.0f);
		for (int32 Try = 0; Try < 8; ++Try)
		{
			bool bClear = true;
			for (const FNovaShot& Shot : NovaShots)
			{
				if (!Shot.bDone && FVector::Dist2D(Shot.Spot, Spot) < NovaRadius * 2.0f + 80.0f)
				{
					const FVector Away = (Spot - Shot.Spot).GetSafeNormal2D();
					Spot = ArenaPoint(Shot.Spot + (Away.IsNearlyZero() ? FVector(0.0f, 1.0f, 0.0f) : Away) * (NovaRadius * 2.0f + 100.0f), NovaRadius + 60.0f);
					bClear = false;
				}
			}
			if (bClear)
			{
				break;
			}
		}
		Spot = ChaosImpactSimaeFx::FindGround(World, Spot, this);
		FNovaShot Shot;
		Shot.Spot = Spot;
		Shot.DueAt = MoveTime + NovaWarnSeconds;
		Shot.ExplodeAt = Shot.DueAt + ChaosImpactNova::FireGrowAge;
		Shot.Warning = AChaosImpactSimaeWarning::SpawnCircle(World, Spot, NovaRadius, Shot.ExplodeAt - MoveTime, NovaBlue);
		NovaShots.Add(Shot);
		// The nova leaves its hand, up out of sight.
		const int32 Arm = Launch % 6;
		const FVector From = GetHand(Arm);
		ShowHandBall(Arm, EChaosImpactBallType::Normal);
		ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::PresNovaLaunch, From);
		if (AChaosImpactFxKit* Kit = GetFx())
		{
			FChaosImpactFxPiece Up;
			Up.Look = EChaosImpactFxLook::Streak;
			Up.Location = From - Kit->GetActorLocation();
			Up.Velocity = FVector(0.0f, 0.0f, 3200.0f);
			Up.Life = 0.4f;
			Up.SizeFrom = 160.0f;
			Up.SizeTo = 300.0f;
			Up.Width = 26.0f;
			Up.Color = NovaBlue;
			Up.CoreColor = NovaCore;
			Up.Intensity = 3.0f;
			Kit->Add(Up);
		}
	}
	for (FNovaShot& Shot : NovaShots)
	{
		if (Shot.bDone)
		{
			continue;
		}
		// Coming down, as a VS nova does: from high over its spot, straight down at the nova's own speed, its own look.
		constexpr float FallSeconds = ChaosImpactBallTypes::NovaDropHeight / ChaosImpactBallTypes::NovaDropSpeed;
		if (!Shot.bLanded && MoveTime >= Shot.DueAt - FallSeconds)
		{
			if (!Shot.Fall.IsValid())
			{
				USceneComponent* Fall = NewObject<USceneComponent>(this);
				Fall->SetupAttachment(GetRootComponent());
				Fall->SetUsingAbsoluteLocation(true);
				Fall->SetUsingAbsoluteRotation(true);
				Fall->SetUsingAbsoluteScale(true);
				Fall->RegisterComponent();
				ChaosImpactBallTypes::BuildNovaLook(this, Fall, Shot.FallLook);
				Shot.Fall = Fall;
				ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::PresNovaFall, Shot.Spot);
			}
			const float Left = FMath::Max(Shot.DueAt - MoveTime, 0.0f);
			Shot.Fall->SetWorldLocation(Shot.Spot + FVector(0.0f, 0.0f, 24.0f * ChaosImpactBallTypes::NovaShownScale + Left * ChaosImpactBallTypes::NovaDropSpeed));
			ChaosImpactBallTypes::UpdateNovaLook(Shot.FallLook, 24.0f * ChaosImpactBallTypes::NovaShownScale, MoveTime);
		}
		if (!Shot.bLanded && MoveTime >= Shot.DueAt)
		{
			// Down: the VS nova's landing, spin and ball of fire (only its look: who it catches is the boss's to say).
			Shot.bLanded = true;
			ClearNovaFall(Shot);
			AChaosImpactHazardZone::SpawnVisual(World, EChaosImpactBallType::Nova, Shot.Spot + FVector(0.0f, 0.0f, 30.0f), NovaRadius, 0.0f, this);
		}
		if (Shot.bLanded && MoveTime >= Shot.ExplodeAt)
		{
			if (Shot.Warning.IsValid())
			{
				Shot.Warning->Finish();
			}
			// Its ball of fire spreading out: whoever it reaches is hit (once).
			const float Age = Shot.ExplodeAt - Shot.DueAt + (MoveTime - Shot.ExplodeAt);
			const float Reach = NovaRadius * FMath::Max(ChaosImpactNova::GetExplosionSize(ChaosImpactNova::GetFireAnimTime(Age)), 0.25f);
			if (MoveTime - DeltaSeconds < Shot.ExplodeAt)
			{
				ShakeNearby(Shot.Spot, NovaRadius * 6.0f, 0.55f, 0.4f);
			}
			for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
			{
				AChaosImpactCharacter* Victim = *It;
				if (IsFoe(Victim) && !Shot.Hit.Contains(Victim) && FVector::Dist2D(Victim->GetActorLocation(), Shot.Spot) < Reach + 35.0f
					&& FMath::Abs(Victim->GetActorLocation().Z - Shot.Spot.Z) < NovaRadius + 200.0f)
				{
					Shot.Hit.Add(Victim);
					HitThisMove.Remove(Victim);
					FVector Out = (Victim->GetActorLocation() - Shot.Spot).GetSafeNormal2D();
					if (Out.IsNearlyZero())
					{
						Out = FVector::ForwardVector;
					}
					StrikeFoe(Victim, Out * 650.0f + FVector(0.0f, 0.0f, 380.0f));
				}
			}
			if (MoveTime >= Shot.ExplodeAt + (ChaosImpactNova::FireFullAge - ChaosImpactNova::FireGrowAge) + 0.2f)
			{
				Shot.bDone = true;
			}
		}
	}
	const bool bAllLanded = NovaShots.Num() == NovaCount && !NovaShots.ContainsByPredicate([](const FNovaShot& Shot) { return !Shot.bDone; });
	if (bAllLanded && MoveTime >= NovaShots.Last().ExplodeAt + 0.6f)
	{
		// Spent: down it comes, wide open.
		NovaShots.Reset();
		HideHandBalls();
		Move = EChaosImpactPresidentMove::Stunned;
		MoveTime = 0.0f;
		SetHover(0.0f, 0.35f);
		ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::PresStun, GetActorLocation());
		ShakeNearby(GetActorLocation(), 4000.0f, 0.6f, 0.5f);
	}
}

void AChaosImpactPresidentBoss::TickStunned(const float DeltaSeconds)
{
	MoveTime += DeltaSeconds;
	PlayClip(SitClip, true, 0.4f);
	if (AChaosImpactFxKit* Kit = GetFx(); Kit && FMath::Fmod(MoveTime, 0.4f) < DeltaSeconds)
	{
		// Seeing stars: little sparks circling over its head.
		const FVector Head = GetFace() + FVector(0.0f, 0.0f, 80.0f);
		for (int32 Index = 0; Index < 3; ++Index)
		{
			const float Angle = MoveTime * 5.0f + Index * UE_TWO_PI / 3.0f;
			FChaosImpactFxPiece Star;
			Star.Location = Head - Kit->GetActorLocation() + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * 90.0f;
			Star.Velocity = FVector(-FMath::Sin(Angle), FMath::Cos(Angle), 0.0f) * 220.0f;
			Star.Life = 0.45f;
			Star.SizeFrom = 46.0f;
			Star.SizeTo = 20.0f;
			Star.Color = FLinearColor(1.0f, 0.88f, 0.3f);
			Star.CoreColor = FLinearColor::White;
			Star.Intensity = 2.6f;
			Kit->Add(Star);
		}
	}
	if (MoveTime >= StunSeconds)
	{
		EndMove(1.2f);
	}
}

// ---- Half health

void AChaosImpactPresidentBoss::StartPowerUp()
{
	AbortMove();
	ShakeOffMoveEffects();
	SetCarForm(false);
	GetMesh()->SetVisibility(true);
	SetActorEnableCollision(true);
	GetCharacterMovement()->StopMovementImmediately();
	Move = EChaosImpactPresidentMove::PowerUp;
	MoveTime = 0.0f;
	SetHover(260.0f, 0.8f);
	PlayClip(BeamStartClip ? BeamStartClip.Get() : SitClip.Get(), false, 0.6f);
	ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::PresPowerUp, GetActorLocation());
	ShakeNearby(GetActorLocation(), 6000.0f, 0.4f, 1.2f);
	UE_LOG(LogChaosImpact, Log, TEXT("President boss: powering up"));
}

void AChaosImpactPresidentBoss::TickPowerUp(const float DeltaSeconds)
{
	MoveTime += DeltaSeconds;
	if (AChaosImpactWhiteHall* Hall = FindHall())
	{
		Hall->SetAlarm(Smooth01(MoveTime / (PowerUpSeconds * 0.8f)));
	}
	if (AChaosImpactFxKit* Kit = GetFx(); Kit && FMath::Fmod(MoveTime, 0.25f) < DeltaSeconds)
	{
		FChaosImpactFxPiece Pulse;
		Pulse.Look = EChaosImpactFxLook::SolidRing;
		Pulse.Location = ChaosImpactSimaeFx::FindGround(GetWorld(), GetActorLocation(), this) - Kit->GetActorLocation() + FVector(0.0f, 0.0f, 5.0f);
		Pulse.Life = 0.6f;
		Pulse.SizeFrom = 300.0f;
		Pulse.SizeTo = 1600.0f;
		Pulse.RingFrom = Pulse.RingTo = 0.9f;
		Pulse.RingWidth = 0.04f;
		Pulse.Color = FLinearColor(1.0f, 0.1f, 0.15f);
		Pulse.CoreColor = FLinearColor(1.0f, 0.7f, 0.6f);
		Pulse.Intensity = 1.2f;
		Pulse.Opacity = 0.6f;
		Pulse.NoiseAmount = 0.0f;
		Kit->Add(Pulse);
	}
	if (MoveTime >= PowerUpSeconds)
	{
		bFullPower = true;
		Move = EChaosImpactPresidentMove::None;
		UE_LOG(LogChaosImpact, Log, TEXT("President boss: FULL POWER"));
		// It opens its second half with its six novas.
		BeginMove(EChaosImpactPresidentMove::NovaStorm);
	}
}

// ---------------------------------------------------------------------------------------------------- Its bar

void AChaosImpactPresidentBoss::PaintHud(const UWorld* World, const FGeometry& Geometry, FSlateWindowElementList& Elements, const int32 Layer,
	const float Scale)
{
	using namespace ChaosImpactPaint;
	const AChaosImpactPresidentBoss* Boss = nullptr;
	for (TActorIterator<AChaosImpactPresidentBoss> It(World); It; ++It)
	{
		if (!It->IsDormant() && It->WakeTime >= 0.0f && !It->bSceneHold)
		{
			Boss = *It;
			break;
		}
	}
	if (!Boss)
	{
		return;
	}
	const float Time = World->GetTimeSeconds();
	const float In = ChaosImpactEase::Out(FMath::Clamp((Time - Boss->WakeTime) / 0.5f, 0.0f, 1.0f));
	const FVector2f Size = Geometry.GetLocalSize();
	const FGeometry Top = MakeAnchor(Geometry, Size.X * 0.5f, 0.0f, Scale);
	// Only its name and the health it has left (the same plate as the other bosses').
	const FGeometry Plate = MakeSkewed(Top, -390.0f, 16.0f - (1.0f - In) * 80.0f, 780.0f, 70.0f, -0.22f);
	const FPainter P{Plate, Elements, Layer, In * (Boss->IsEliminated() ? 0.5f : 1.0f)};
	P.Box(6.0f, 6.0f, 780.0f, 70.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.45f));
	P.Box(0.0f, 0.0f, 780.0f, 70.0f, FLinearColor(0.012f, 0.016f, 0.03f, 0.9f));
	P.Box(0.0f, 0.0f, 780.0f, 3.0f, FLinearColor(0.85f, 0.68f, 0.3f, 0.9f));
	P.Text(TEXT("ドッチ・ボール大統領"), 22.0f, 4.0f, 22.0f, Paper, ETextAlign::Left, TEXT("Black"), 2.0f, Ink);
	const float Max = FMath::Max(Boss->GetMaxHealth(), 1.0f);
	const float Ratio = FMath::Clamp(Boss->GetHealth() / Max, 0.0f, 1.0f);
	const float Trail = FMath::Clamp(FMath::Max(Boss->ShownHealth, Boss->GetHealth()) / Max, 0.0f, 1.0f);
	constexpr float BarX = 22.0f;
	constexpr float BarY = 40.0f;
	constexpr float BarW = 736.0f;
	constexpr float BarH = 18.0f;
	P.Box(BarX - 3.0f, BarY - 3.0f, BarW + 6.0f, BarH + 6.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.8f));
	P.Box(BarX, BarY, BarW, BarH, FLinearColor(0.06f, 0.07f, 0.1f, 1.0f));
	P.Box(BarX, BarY, BarW * Trail, BarH, FLinearColor(1.0f, 0.95f, 0.85f, 0.9f));
	const bool bFull = Boss->IsFullPower();
	FLinearColor FillTop = bFull ? FLinearColor(1.0f, 0.5f, 0.55f) : FLinearColor(1.0f, 0.92f, 0.62f);
	FLinearColor FillBottom = bFull ? FLinearColor(0.8f, 0.03f, 0.08f) : FLinearColor(0.85f, 0.55f, 0.1f);
	if (Boss->GetMove() == EChaosImpactPresidentMove::PowerUp)
	{
		const float Throb = 0.5f + 0.5f * FMath::Sin(Time * 12.0f);
		FillTop = FMath::Lerp(FillTop, FLinearColor(1.0f, 0.45f, 0.5f), 0.4f + 0.6f * Throb);
		FillBottom = FMath::Lerp(FillBottom, FLinearColor(0.9f, 0.02f, 0.08f), 0.4f + 0.6f * Throb);
	}
	P.Box(BarX, BarY, BarW * Ratio, BarH * 0.5f, FillTop);
	P.Box(BarX, BarY + BarH * 0.5f, BarW * Ratio, BarH * 0.5f, FillBottom);
	// The half-way mark (where it changes).
	P.Box(BarX + BarW * 0.5f - 1.5f, BarY - 4.0f, 3.0f, BarH + 8.0f, FLinearColor(0.85f, 0.68f, 0.3f, bFull ? 0.35f : 0.9f));
}
