#include "ChaosImpactPuppetComponent.h"

#include "ChaosImpactCharacter.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactCharacterRoster.h"
#include "Animation/AnimSequenceBase.h"
#include "AnimationRuntime.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"

namespace
{
	int32 ClipFor(const FName State)
	{
		if (State == TEXT("Appeal")) { return 1; }
		if (State == TEXT("Ball")) { return 2; }
		if (State == TEXT("NovaCharge") || State == TEXT("NovaThrow")) { return 3; }
		if (State == TEXT("Swing")) { return 4; }
		return 0;
	}
}

bool UChaosImpactPuppetComponent::InitializeAuthored(USkeletalMesh* Mesh)
{
	const auto& Info = ChaosImpactRoster::Get(CharacterIndex);
	bBirdAuthored = CharacterIndex == ChaosImpactRoster::BirdIndex;
	AuthoredClips.Reset();
	const TCHAR* FishClips[] = {TEXT("Swim"), TEXT("Appeal"), TEXT("Ball"), TEXT("Nova"), TEXT("Swing")};
	const TCHAR* BirdClips[] = {TEXT("Idle"), TEXT("Appeal"), TEXT("Idle"), TEXT("Idle"), TEXT("Idle")};
	for (int32 ClipIndex = 0; ClipIndex < 5; ++ClipIndex)
	{
		const TCHAR* Name = bBirdAuthored ? BirdClips[ClipIndex] : FishClips[ClipIndex];
		const FString Path = FString::Printf(TEXT("%s/A_%s_%s"), Info.AssetFolder, Info.AssetPrefix, Name);
		UAnimSequenceBase* Clip = LoadObject<UAnimSequenceBase>(nullptr, *Path);
		if (!Clip || Clip->GetSkeleton() != Mesh->GetSkeleton()) { AuthoredClips.Reset(); return false; }
		AuthoredClips.Add(Clip);
	}
	AuthoredMesh = NewObject<USkeletalMeshComponent>(GetOwner());
	AuthoredMesh->SetupAttachment(this);
	// Keep the orientation carried by the imported clips; an extra half-turn reverses the fish.
	AuthoredMesh->SetRelativeRotation(FRotator::ZeroRotator);
	// A hovering bird: lift only its visible mesh; ordinary movement/collision and jump rules stay shared.
	AuthoredMesh->SetRelativeLocation(bBirdAuthored ? FVector(0.f, 0.f, 20.f) : FVector::ZeroVector);
	AuthoredMesh->SetSkeletalMeshAsset(Mesh);
	AuthoredMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	AuthoredMesh->SetGenerateOverlapEvents(false);
	AuthoredMesh->SetCanEverAffectNavigation(false);
	AuthoredMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	AuthoredMesh->RegisterComponent();
	// Preview menus explicitly animate while paused; gameplay uses this same evaluation path.
	AuthoredMesh->SetComponentTickEnabled(false);
	AddTickPrerequisiteComponent(Source);
	bReady = true;
	SetColourIndex(0);
	SetAuthoredState(TEXT("Idle"));
	UpdateAuthoredPose(0.0f);
	return true;
}

void UChaosImpactPuppetComponent::SetAuthoredState(const FName State, const bool bRestart)
{
	if (!AuthoredMesh || (AuthoredState == State && !bRestart)) { return; }
	AuthoredState = State;
	AuthoredTime = State == TEXT("NovaThrow") ? AuthoredClips[3]->GetPlayLength() * 0.75f : 0.0f;
	AuthoredMesh->PlayAnimation(AuthoredClips[ClipFor(State)], false);
	AuthoredMesh->SetPlayRate(0.0f);
}

void UChaosImpactPuppetComponent::PlaySelectionAppeal()
{
	if (!AuthoredMesh) { return; }
	AuthoredAppealRemaining = AuthoredClips[1]->GetPlayLength();
	AuthoredThrowRemaining = 0.0f;
	SetAuthoredState(TEXT("Appeal"), true);
}

void UChaosImpactPuppetComponent::PlayAuthoredThrow(const bool bNova, const float ReleaseDelaySeconds)
{
	if (!AuthoredMesh) { return; }
	bAuthoredNovaThrow = bNova;
	AuthoredThrowAge = 0.0f;
	AuthoredReleaseDelay = FMath::Max(0.01f, ReleaseDelaySeconds);
	AuthoredAppealRemaining = 0.0f;
	if (bBirdAuthored)
	{
		// Keep flapping: only the ball travels from the wing to the mouth before release.
		AuthoredThrowRemaining = AuthoredReleaseDelay;
		return;
	}
	// NOVA holds its charging pose at 75%, then finishes the same authored clip on release.
	const float Length = AuthoredClips[bNova ? 3 : 4]->GetPlayLength();
	AuthoredThrowRate = bNova ? FMath::Max(0.1f, Length * 0.15f / FMath::Max(0.01f, ReleaseDelaySeconds)) : 1.0f;
	AuthoredThrowRemaining = (bNova ? Length * 0.25f : Length) / AuthoredThrowRate;
	SetAuthoredState(bNova ? TEXT("NovaThrow") : TEXT("Swing"), true);
}

bool UChaosImpactPuppetComponent::GetAuthoredMouthLocation(FVector& OutLocation) const
{
	if (!AuthoredMesh || !bReady) { return false; }
	if (bBirdAuthored)
	{
		if (AuthoredMesh->GetBoneIndex(TEXT("Mouth")) == INDEX_NONE) { return false; }
		OutLocation = AuthoredMesh->GetSocketLocation(TEXT("Mouth"));
		return true;
	}
	const int32 Head = AuthoredMesh->GetBoneIndex(TEXT("joint7"));
	if (Head == INDEX_NONE) { return false; }
	const FTransform Reference = FAnimationRuntime::GetComponentSpaceTransformRefPose(
		AuthoredMesh->GetSkeletalMeshAsset()->GetRefSkeleton(), Head);
	const FVector ForwardInHead = Reference.InverseTransformVectorNoScale(FVector(0.0f, -1.0f, 0.0f));
	const FVector Forward = AuthoredMesh->GetSocketTransform(TEXT("joint7"), RTS_World)
		.TransformVectorNoScale(ForwardInHead).GetSafeNormal();
	// joint8 and joint10 are the authored upper/lower lip tips, not side fins.
	OutLocation = (AuthoredMesh->GetSocketLocation(TEXT("joint8"))
		+ AuthoredMesh->GetSocketLocation(TEXT("joint10"))) * 0.5f + Forward * 17.0f;
	return true;
}

void UChaosImpactPuppetComponent::UpdateAuthoredPose(const float DeltaSeconds)
{
	if (!AuthoredMesh || AuthoredClips.Num() != 5) { return; }
	const AChaosImpactCharacter* Character = Cast<AChaosImpactCharacter>(GetOwner());
	FName Desired;
	if (bBallPose)
	{
		Desired = TEXT("Ball");
		AuthoredThrowRemaining = AuthoredAppealRemaining = 0.0f;
	}
	else if (AuthoredThrowRemaining > 0.0f) { Desired = bBirdAuthored ? TEXT("Idle") : bAuthoredNovaThrow ? TEXT("NovaThrow") : TEXT("Swing"); }
	else if (AuthoredAppealRemaining > 0.0f) { Desired = TEXT("Appeal"); }
	else if (Character && Character->IsChargingNova()) { Desired = TEXT("NovaCharge"); }
	else { Desired = Character && Character->GetVelocity().SizeSquared2D() > 9.0f ? TEXT("Swim") : TEXT("Idle"); }
	SetAuthoredState(Desired);
	const float Length = AuthoredClips[ClipFor(Desired)]->GetPlayLength();
	const float Rate = Desired == TEXT("Idle") ? (bBirdAuthored ? 1.f : 0.22f)
		: Desired == TEXT("NovaThrow") || Desired == TEXT("Swing") ? AuthoredThrowRate : 1.0f;
	AuthoredTime += FMath::Max(0.0f, DeltaSeconds) * Rate;
	if (AuthoredThrowRemaining > 0.0f) { AuthoredThrowAge += FMath::Max(0.0f, DeltaSeconds); }
	if (Desired == TEXT("Idle") || Desired == TEXT("Swim")) { AuthoredTime = FMath::Fmod(AuthoredTime, FMath::Max(Length, 0.01f)); }
	else { AuthoredTime = FMath::Min(AuthoredTime, Desired == TEXT("NovaCharge") ? Length * 0.75f : Length); }
	AuthoredMesh->SetPosition(AuthoredTime, false);
	AuthoredMesh->TickAnimation(0.0f, false);
	AuthoredMesh->RefreshBoneTransforms();
	AuthoredMesh->UpdateBounds();
	if (Character)
	{
		AChaosImpactBall* Pending = nullptr;
		FVector Direction;
		float Speed = 0.0f, UpSpeed = 0.0f, SecondsLeft = 0.0f;
		bool bArc = false;
		FVector Palm, Mouth;
		if (Character->GetPendingThrow(Pending, Direction, Speed, UpSpeed, bArc, SecondsLeft) && Pending
			&& GetPalmLocation(true, Palm) && GetAuthoredMouthLocation(Mouth))
		{
			const float Transfer = FMath::SmoothStep(0.0f, 1.0f,
				FMath::Clamp(AuthoredThrowAge / (AuthoredReleaseDelay * 0.72f), 0.0f, 1.0f));
			Pending->SetActorLocation(FMath::Lerp(Palm, Mouth, Transfer));
		}
	}
	AuthoredAppealRemaining = FMath::Max(0.0f, AuthoredAppealRemaining - DeltaSeconds);
	AuthoredThrowRemaining = FMath::Max(0.0f, AuthoredThrowRemaining - DeltaSeconds);
}
