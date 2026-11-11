#include "ChaosImpactDojo.h"

#include "ChaosImpact.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactBallFormComponent.h"
#include "ChaosImpactBallSpawner.h"
#include "ChaosImpactBallTypes.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactDojoCourse.h"
#include "ChaosImpactDojoVideo.h"
#include "ChaosImpactGameMode.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactMatchTypes.h"
#include "ChaosImpactHazardZone.h"
#include "ChaosImpactHook.h"
#include "ChaosImpactIceMeshes.h"
#include "ChaosImpactLightning.h"
#include "ChaosImpactNovaParts.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactSfx.h"
#include "ChaosImpactSimaeBird.h"
#include "ChaosImpactStageBase.h"
#include "ChaosImpactSuddenDeathZone.h"
#include "ChaosImpactTackleComponent.h"
#include "ChaosImpactTornado.h"
#include "ChaosImpactTrainingArena.h"
#include "ChaosImpactTrainingTarget.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "InputCoreTypes.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "TimerManager.h"

namespace
{
	using EGoal = EChaosImpactDojoGoal;
	using ECPU = EChaosImpactDojoCPU;

	/** A glowing ring is entered within this much of its middle. */
	constexpr float DojoRingRadius = 150.0f;

	/** What hit: the ball kind behind a hit's causer (a tornado is the wind ball's, a flock the simae ball's, ...). */
	EChaosImpactBallType KindOf(const AActor* Causer)
	{
		if (const AChaosImpactBall* Ball = Cast<AChaosImpactBall>(Causer))
		{
			return Ball->GetBallType();
		}
		if (const AChaosImpactHazardZone* Zone = Cast<AChaosImpactHazardZone>(Causer))
		{
			return Zone->GetZoneType();
		}
		if (Cast<AChaosImpactTornado>(Causer))
		{
			return EChaosImpactBallType::Wind;
		}
		if (const AChaosImpactHook* Hook = Cast<AChaosImpactHook>(Causer))
		{
			return Hook->IsAnchor() ? EChaosImpactBallType::Anchor : EChaosImpactBallType::Hook;
		}
		if (Cast<AChaosImpactSimaeBird>(Causer) || Cast<AChaosImpactSimaeFeatherBurst>(Causer))
		{
			return EChaosImpactBallType::Simae;
		}
		return EChaosImpactBallType::Normal;
	}

	bool IsConfirm(const APlayerController* PC)
	{
		const bool bSwap = ChaosImpactSettings::IsSwitchConfirmRight();
		return PC->WasInputKeyJustPressed(EKeys::Enter) || PC->WasInputKeyJustPressed(EKeys::SpaceBar)
			|| PC->WasInputKeyJustPressed(EKeys::LeftMouseButton)
			|| PC->WasInputKeyJustPressed(bSwap ? EKeys::Gamepad_FaceButton_Right : EKeys::Gamepad_FaceButton_Bottom);
	}

	bool IsBack(const APlayerController* PC)
	{
		const bool bSwap = ChaosImpactSettings::IsSwitchConfirmRight();
		return PC->WasInputKeyJustPressed(EKeys::BackSpace) || PC->WasInputKeyJustPressed(EKeys::RightMouseButton)
			|| PC->WasInputKeyJustPressed(bSwap ? EKeys::Gamepad_FaceButton_Bottom : EKeys::Gamepad_FaceButton_Right);
	}
}

AChaosImpactDojo::AChaosImpactDojo()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	// (The lesson list pauses the world; its clip and its state go on.)
	PrimaryActorTick.bTickEvenWhenPaused = true;
	bReplicates = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

AChaosImpactDojo* AChaosImpactDojo::Find(const UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AChaosImpactDojo> It(const_cast<UWorld*>(World)); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

bool AChaosImpactDojo::IsDojoWorld(const UWorld* World)
{
	return World && World->URL.HasOption(TEXT("CIDojo=1"));
}

bool AChaosImpactDojo::IsLessonHere(const FChaosImpactDojoLesson& Lesson) const
{
	const UWorld* World = GetWorld();
	// (A mock match's slides are read wherever the dojo is; the match itself is opened from its start button.)
	if (Lesson.bRealMatch)
	{
		return true;
	}
	const FString Wanted = FPackageName::GetShortName(ChaosImpactDojo::GetLessonLevel(Lesson));
	return !World || World->GetMapName().EndsWith(Wanted);
}

void AChaosImpactDojo::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	AChaosImpactCharacter::OnAnyDamaged.Remove(DamageHandle);
	Super::EndPlay(EndPlayReason);
}

AChaosImpactPlayerController* AChaosImpactDojo::GetPlayer() const
{
	return GetWorld() ? Cast<AChaosImpactPlayerController>(GetWorld()->GetFirstPlayerController()) : nullptr;
}

AChaosImpactCharacter* AChaosImpactDojo::GetPlayerCharacter() const
{
	const AChaosImpactPlayerController* PC = GetPlayer();
	return PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
}

const FChaosImpactDojoLesson* AChaosImpactDojo::GetLesson() const
{
	return ChaosImpactDojo::GetLesson(Category, LessonIndex);
}

void AChaosImpactDojo::GetListFocus(int32& OutCategory, int32& OutLesson) const
{
	OutCategory = Category;
	OutLesson = LessonIndex;
}

FVector AChaosImpactDojo::OnFloor(const FVector& At) const
{
	FHitResult Floor;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(DojoFloor), false);
	if (const AChaosImpactCharacter* Player = GetPlayerCharacter())
	{
		Query.AddIgnoredActor(Player);
	}
	if (GetWorld() && GetWorld()->LineTraceSingleByObjectType(Floor, At + FVector(0.0f, 0.0f, 1500.0f), At - FVector(0.0f, 0.0f, 3000.0f),
		FCollisionObjectQueryParams(ECC_WorldStatic), Query))
	{
		return Floor.ImpactPoint;
	}
	return At;
}

void AChaosImpactDojo::ReadStage()
{
	const AChaosImpactPlayerController* PC = GetPlayer();
	const AChaosImpactCharacter* Player = GetPlayerCharacter();
	if (!PC || !Player || !PC->PlayerCameraManager || !GetWorld())
	{
		return;
	}
	const AChaosImpactStageBase* Stage = nullptr;
	for (TActorIterator<AChaosImpactStageBase> It(GetWorld()); It; ++It)
	{
		UE_LOG(LogChaosImpact, Log, TEXT("Dojo: stage actor %s (%s) at %s, half %.0f"), *It->GetName(), *It->GetClass()->GetName(),
			*It->GetActorLocation().ToCompactString(), It->GetHalfExtent());
		if (!Stage)
		{
			Stage = *It;
		}
	}
	const FVector Feet = Player->GetActorLocation() - FVector(0.0f, 0.0f, Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	StageActor = const_cast<AChaosImpactStageBase*>(Stage);
	// (Diagnostics: what else stands on the stage.)
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		FVector Origin;
		FVector Extent;
		It->GetActorBounds(true, Origin, Extent);
		if (Stage && *It != Stage && Extent.Size2D() > 50.0f && FVector::Dist2D(Origin, Stage->GetCenter()) < Stage->GetHalfExtent() * 1.2f)
		{
			UE_LOG(LogChaosImpact, Verbose, TEXT("Dojo: level actor %s (%s) at %s extent %s"), *It->GetName(), *It->GetClass()->GetName(),
				*Origin.ToCompactString(), *Extent.ToCompactString());
		}
	}
	Center = Stage ? OnFloor(Stage->GetCenter()) : Feet;
	HalfExtent = Stage ? FMath::Max(Stage->GetHalfExtent(), 900.0f) : 1500.0f;
	const float Yaw = PC->PlayerCameraManager->GetCameraRotation().Yaw;
	Up = FRotator(0.0f, Yaw, 0.0f).Vector();
	Right = FRotator(0.0f, Yaw + 90.0f, 0.0f).Vector();
	WideCenter = Center;
	WideHalf = HalfExtent;
	// Nothing of training's over the stage (its arena is never put up in the dojo; any there is taken away).
	for (TActorIterator<AChaosImpactTrainingArena> It(GetWorld()); It; ++It)
	{
		It->Destroy();
	}
	// The stage's own ball pads, where a match has them (quiet until a lesson plays with them).
	if (Stage && StagePads.IsEmpty() && !IsMatchWorld())
	{
		for (const FVector& Point : Stage->GetBallPoints())
		{
			const FTransform Where(FRotator::ZeroRotator, Point + FVector(0.0f, 0.0f, 3.0f));
			if (AChaosImpactBallSpawner* Pad = GetWorld()->SpawnActorDeferred<AChaosImpactBallSpawner>(AChaosImpactBallSpawner::StaticClass(), Where,
				nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
			{
				Pad->SetAlwaysActive(true);
				Pad->FinishSpawning(Where);
				StagePads.Add(Pad);
			}
		}
	}
	// An open patch of the stage for the lessons: nothing standing in a wide square round it, away from the plain middle
	// (so the stage's own floor, markings and cover are seen round the lesson and it reads as the VS stage it is).
	OpenCenter = Center;
	OpenHalf = FMath::Min(HalfExtent, 1100.0f);
	if (HalfExtent > 1600.0f)
	{
		FCollisionQueryParams Query(SCENE_QUERY_STAT(DojoOpen), false, Player);
		// The widest clear square that can be found (the stage's main floor: not a raised deck, not off its edge).
		for (const float Box : {1000.0f, 850.0f, 700.0f})
		{
			float BestScore = -1.0e9f;
			for (float U = -HalfExtent * 0.85f; U <= HalfExtent * 0.85f; U += 300.0f)
			{
				for (float R = -HalfExtent * 0.85f; R <= HalfExtent * 0.85f; R += 300.0f)
				{
					const FVector Guess = Center + Up * U + Right * R;
					FHitResult Floor;
					if (!GetWorld()->LineTraceSingleByObjectType(Floor, Guess + FVector(0.0f, 0.0f, 1500.0f), Guess - FVector(0.0f, 0.0f, 1500.0f),
						FCollisionObjectQueryParams(ECC_WorldStatic), Query) || Floor.ImpactPoint.Z > Center.Z + 20.0f
						|| Floor.ImpactPoint.Z < Center.Z - 250.0f)
					{
						continue;
					}
					const FVector Above = Floor.ImpactPoint + FVector(0.0f, 0.0f, 120.0f);
					if (GetWorld()->OverlapAnyTestByObjectType(Above, FQuat(Up.Rotation()), FCollisionObjectQueryParams(ECC_WorldStatic),
						FCollisionShape::MakeBox(FVector(Box, Box, 60.0f)), Query))
					{
						continue;
					}
					// Best about halfway out (off the middle, short of the walls).
					const float Out = static_cast<float>(FVector::Dist2D(Guess, Center));
					const float Score = -FMath::Abs(Out - HalfExtent * 0.45f);
					if (Score > BestScore)
					{
						BestScore = Score;
						OpenCenter = Floor.ImpactPoint;
					}
				}
			}
			if (BestScore > -1.0e9f)
			{
				OpenHalf = Box + 150.0f;
				break;
			}
		}
		UE_LOG(LogChaosImpact, Log, TEXT("Dojo: open patch at %s, half %.0f"), *OpenCenter.ToCompactString(), OpenHalf);
	}
	PlaceFor(FChaosImpactDojoLesson());
	bStageRead = true;
	DamageHandle = AChaosImpactCharacter::OnAnyDamaged.AddUObject(this, &AChaosImpactDojo::OnCharacterDamaged);
	QuietStage();
	UE_LOG(LogChaosImpact, Log, TEXT("Dojo: stage at %s (half %.0f), up %s"), *Center.ToCompactString(), HalfExtent, *Up.ToCompactString());
}

void AChaosImpactDojo::PlaceFor(const FChaosImpactDojoLesson& Lesson)
{
	// Lessons that range far (the anchor's) use the whole stage; the rest the open patch.
	const bool bWide = Lesson.Distance > 1400.0f;
	Center = bWide ? WideCenter : OpenCenter;
	HalfExtent = bWide ? WideHalf : OpenHalf;
	Start = OnFloor(Center - Up * FMath::Min(HalfExtent * 0.45f, 800.0f));
	// On the same level as the middle (not on a lower pad below a step: a dash would stop dead against it).
	const float Level = static_cast<float>(OnFloor(Center).Z);
	for (int32 Step = 0; Step < 12 && FMath::Abs(static_cast<float>(Start.Z) - Level) > 12.0f; ++Step)
	{
		Start = OnFloor(Start + Up * 50.0f);
	}
}

void AChaosImpactDojo::QuietStage()
{
	// The stage's own pads put out nothing here: each lesson sets out its own balls (a match-like lesson has them on).
	const FChaosImpactDojoLesson* L = GetLesson();
	const bool bPads = L && L->bStagePads && Phase != EChaosImpactDojoPhase::Idle;
	for (TActorIterator<AChaosImpactBallSpawner> It(GetWorld()); It; ++It)
	{
		GetWorldTimerManager().ClearAllTimersForObject(*It);
		if (AChaosImpactBall* Ball = It->GetActiveBall())
		{
			Ball->Destroy();
		}
		if (bPads)
		{
			It->SpawnNow();
		}
	}
}

void AChaosImpactDojo::StartLesson(const int32 InCategory, const int32 InLesson)
{
	if (!ChaosImpactDojo::GetLesson(InCategory, InLesson))
	{
		return;
	}
	if (!bStageRead)
	{
		ReadStage();
	}
	// Held on another level (the duel's arena, or back on the VS stage): opened there, straight on this lesson.
	if (!IsLessonHere(*ChaosImpactDojo::GetLesson(InCategory, InLesson)))
	{
		if (AChaosImpactPlayerController* PC = GetPlayer())
		{
			PC->OpenDojo(ChaosImpactDojo::GetLessonLevel(*ChaosImpactDojo::GetLesson(InCategory, InLesson)),
				FString::Printf(TEXT("%d_%d"), InCategory, InLesson));
		}
		return;
	}
	ClearLesson();
	Category = InCategory;
	LessonIndex = InLesson;
	bClearedBefore = ChaosImpactDojo::IsCleared(GetLesson()->Id);
	Phase = EChaosImpactDojoPhase::Intro;
	NovaPartsSeen = 0;
	bNovaHeld = false;
	Slide = 0;
	Done = 0;
	PhaseAt = SlideAt = GetWorld()->GetTimeSeconds();
	CountAt = FailAt = -100.0;
	SetUpLesson();
	FreezePlayer(true);
	if (AChaosImpactPlayerController* PC = GetPlayer())
	{
		PC->EnterDojoPlay();
	}
	ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiOpen);
	UE_LOG(LogChaosImpact, Log, TEXT("Dojo: lesson %s (%s)"), *GetLesson()->Id, *GetLesson()->Name);
}

void AChaosImpactDojo::RetryLesson()
{
	if (IsMatchWorld() && GetLesson())
	{
		if (AChaosImpactPlayerController* PC = GetPlayer())
		{
			PC->OpenDojoMatch(MatchRulesFor(*GetLesson()), FString::Printf(TEXT("%d_%d"), Category, LessonIndex));
		}
		return;
	}
	if (GetLesson())
	{
		const int32 KeepCategory = Category;
		const int32 KeepLesson = LessonIndex;
		StartLesson(KeepCategory, KeepLesson);
		// Straight to its start button (its slides were just seen).
		Slide = GetLesson()->Slides.Num();
		Phase = EChaosImpactDojoPhase::Ready;
		PhaseAt = GetWorld()->GetTimeSeconds();
	}
}

void AChaosImpactDojo::ReturnToList()
{
	if (IsMatchWorld())
	{
		LeaveMatch();
		return;
	}
	ClearLesson();
	Phase = EChaosImpactDojoPhase::Idle;
	FreezePlayer(true);
	if (AChaosImpactPlayerController* PC = GetPlayer())
	{
		PC->ShowMenuScreen(EChaosImpactScreen::DojoSelect);
	}
}

void AChaosImpactDojo::FreezePlayer(const bool bFrozen)
{
	if (AChaosImpactCharacter* Player = GetPlayerCharacter())
	{
		Player->SetTrainingMenuFrozen(bFrozen);
	}
	for (const FDojoCPU& CPU : CPUs)
	{
		if (AChaosImpactCharacter* Pawn = CPU.Pawn.Get())
		{
			Pawn->SetTrainingMenuFrozen(bFrozen);
		}
		if (AChaosImpactCPUController* Brain = CPU.Brain.Get())
		{
			const FChaosImpactDojoLesson* L = GetLesson();
			Brain->SetActorTickEnabled(!bFrozen && L && L->CPU == ECPU::Fighter);
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Setting a lesson out, and clearing it away
// ---------------------------------------------------------------------------------------------------------------------

AChaosImpactCharacter* AChaosImpactDojo::SpawnCPU(const FVector& At)
{
	AChaosImpactGameMode* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<AChaosImpactGameMode>() : nullptr;
	if (!Mode)
	{
		return nullptr;
	}
	const FVector Feet = OnFloor(At);
	const FRotator Facing = (Start - Feet).GetSafeNormal2D().Rotation();
	AChaosImpactCharacter* Pawn = Mode->SpawnDojoCPU(Feet + FVector(0.0f, 0.0f, 100.0f), Facing, CPUs.Num() + 1);
	if (!Pawn)
	{
		return nullptr;
	}
	FDojoCPU& CPU = CPUs.AddDefaulted_GetRef();
	CPU.Pawn = Pawn;
	CPU.Brain = Cast<AChaosImpactCPUController>(Pawn->GetController());
	CPU.Home = Feet;
	CPU.Phase = CPUs.Num() * 1.7f;
	CPU.LastHealth = Pawn->GetHealth();
	if (AChaosImpactCPUController* Brain = CPU.Brain.Get())
	{
		const FChaosImpactDojoLesson* L = GetLesson();
		Brain->SetDifficulty(L ? L->CPULevel : 0);
		Brain->SetActorTickEnabled(false);
	}
	return Pawn;
}

void AChaosImpactDojo::SpawnTarget(const FVector& At, const bool bMoving, const int32 Index)
{
	const FVector Feet = OnFloor(At);
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AChaosImpactTrainingTarget* Target = GetWorld()->SpawnActor<AChaosImpactTrainingTarget>(AChaosImpactTrainingTarget::StaticClass(),
		Feet, (Start - Feet).GetSafeNormal2D().Rotation(), Parameters);
	if (Target)
	{
		if (bMoving)
		{
			Target->ConfigureMotion(EChaosImpactTargetMotion::SideToSide, 320.0f, 0.3f, Index * 0.37f);
		}
		Target->SetTrainingEnabled(true);
		Targets.Add(Target);
		TargetDown.Add(false);
	}
}

void AChaosImpactDojo::SpawnPickup(const FVector& At, const EChaosImpactBallType Type)
{
	const FTransform Where(FRotator::ZeroRotator, OnFloor(At) + FVector(0.0f, 0.0f, 45.0f));
	AChaosImpactBall* Ball = GetWorld()->SpawnActorDeferred<AChaosImpactBall>(AChaosImpactBall::StaticClass(), Where, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Ball)
	{
		Ball->SetBallType(Type);
		Ball->FinishSpawning(Where);
		Ball->MakePickup();
		Pickups.Add(Ball);
	}
}

void AChaosImpactDojo::SpawnNovaParts()
{
	// The three parts in a fan up the screen (each on the floor, its pillar of light showing where).
	const float D = GetLesson() ? GetLesson()->Distance * (bRecording ? 0.62f : 1.0f) : 800.0f;
	const EChaosImpactNovaPart Parts[] = {EChaosImpactNovaPart::Gear, EChaosImpactNovaPart::Core, EChaosImpactNovaPart::Ring};
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const float Angle = FMath::DegreesToRadians(-50.0f + 50.0f * Index);
		const FVector At = OnFloor(Start + (Up * FMath::Cos(Angle) + Right * FMath::Sin(Angle)) * D * 0.55f);
		AChaosImpactNovaPart::Spawn(GetWorld(), Parts[Index], At + FVector(0.0f, 0.0f, ChaosImpactNovaParts::PartHoverHeight));
	}
}

void AChaosImpactDojo::SpawnRing(const FVector& At)
{
	Rings.Add(OnFloor(At));
}

void AChaosImpactDojo::SetUpLesson()
{
	const FChaosImpactDojoLesson* L = GetLesson();
	AChaosImpactCharacter* Player = GetPlayerCharacter();
	if (!L || !Player)
	{
		return;
	}
	PlaceFor(*L);
	QuietStage();
	if (L->bDriveCourse)
	{
		BuildCourse();
	}
	// Everything inside the stage (a little in from its edge).
	const float Room = FMath::Max(HalfExtent - 220.0f, 400.0f);
	const auto Inside = [this, Room](const FVector& At)
	{
		const FVector Local = At - Center;
		const float U = FMath::Clamp(static_cast<float>(FVector::DotProduct(Local, Up)), -Room, Room);
		const float R = FMath::Clamp(static_cast<float>(FVector::DotProduct(Local, Right)), -Room, Room);
		return Center + Up * U + Right * R;
	};
	// (Filmed for the slides, things stand closer so the picture holds them all.)
	const float D = L->Distance * (bRecording ? 0.62f : 1.0f);
	FVector PlayerAt = Start;
	if (L->Goal == EGoal::StayInZone)
	{
		PlayerAt = Center;
	}
	// A match as it is played on this stage: the player on its first spawn point, the CPUs on the others.
	TArray<FVector> MatchSpawns;
	// (Filmed on the wide VS stage, the far spawn points would leave everyone out of the picture: those clips stand close.)
	if (L->bMatchSpawns && StageActor.IsValid() && !(bRecording && !L->bDuelArena))
	{
		MatchSpawns = StageActor->GetSpawnPoints();
	}
	const bool bMatch = MatchSpawns.Num() > L->CPUCount;
	FRotator Facing = Up.Rotation();
	if (bMatch)
	{
		PlayerAt = MatchSpawns[0];
		Facing = (WideCenter - PlayerAt).GetSafeNormal2D().Rotation();
	}
	const float HalfHeight = Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Stand = OnFloor(PlayerAt) + FVector(0.0f, 0.0f, HalfHeight + 4.0f);
	Player->ResetForOnlineMatch(Stand, Facing);
	Player->SetRespawnPoint(Stand, Facing);
	// Lessons about aiming are thrown from where the player stands: no walking up to the targets / along the road.
	Player->SetDojoRooted(IsRootedLesson(*L));
	LastBalls = 0;
	LastTypes = 0;
	// (A mock match: only its slides here; the match is opened from its start button. Filmed for its slides, as before.)
	if (L->bRealMatch && !bRecording)
	{
		return;
	}

	switch (L->Goal)
	{
	case EGoal::Reach:
		if (L->Ball == EChaosImpactBallType::Anchor && L->Count >= 5)
		{
			// Round the whole stage: its corners and back through the middle.
			SpawnRing(Inside(Start + Up * D));
			SpawnRing(Inside(Start + Up * D + Right * D));
			SpawnRing(Inside(Start + Right * D));
			SpawnRing(Inside(Start - Right * D * 0.9f));
			SpawnRing(Inside(Start + Up * D * 1.6f - Right * D * 0.9f));
		}
		else if (L->Ball == EChaosImpactBallType::Anchor)
		{
			// Far apart: across the stage and back, too far to walk in good time.
			SpawnRing(Inside(Start + Up * D));
			SpawnRing(Inside(Start + Up * D + Right * D * 0.85f));
			SpawnRing(Inside(Start + Right * D * 0.85f));
		}
		else
		{
			SpawnRing(Inside(Start + Up * D * 0.55f - Right * D * 0.45f));
			SpawnRing(Inside(Start + Up * D + Right * D * 0.4f));
			SpawnRing(Inside(Start + Right * D * 0.55f));
		}
		break;
	case EGoal::PickUp:
		SpawnPickup(Inside(Start + Up * D * 0.6f - Right * D * 0.45f), EChaosImpactBallType::Normal);
		SpawnPickup(Inside(Start + Up * D * 0.6f + Right * D * 0.45f), EChaosImpactBallType::Normal);
		break;
	case EGoal::NovaBuild:
		SpawnNovaParts();
		break;
	case EGoal::StayInZone:
	{
		FActorSpawnParameters Parameters;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (AChaosImpactSuddenDeathZone* NewZone = GetWorld()->SpawnActor<AChaosImpactSuddenDeathZone>(AChaosImpactSuddenDeathZone::StaticClass(),
			Center, FRotator::ZeroRotator, Parameters))
		{
			// Held at its widest (starting far off) until the lesson starts.
			const AGameStateBase* State = GetWorld()->GetGameState();
			NewZone->Begin(HalfExtent * 0.85f, (State ? State->GetServerWorldTimeSeconds() : 0.0) + 100000.0, 60.0f);
			Zone = NewZone;
		}
		ZoneTime = 0.0f;
		OutTime = 0.0f;
		break;
	}
	default:
		break;
	}
	// Targets: in a row up the screen, or spread round the player.
	for (int32 Index = 0; Index < L->Targets; ++Index)
	{
		FVector At;
		if (L->Targets <= 3)
		{
			At = Start + Up * D + Right * (Index - (L->Targets - 1) * 0.5f) * 380.0f;
		}
		else if (L->Targets >= 6)
		{
			const int32 Row = Index / 3;
			At = Start + Up * D * (0.7f + 0.45f * Row) + Right * ((Index % 3) - 1) * 520.0f;
		}
		else
		{
			const float Angle = FMath::DegreesToRadians(-75.0f + 150.0f * Index / FMath::Max(L->Targets - 1, 1));
			At = Start + (Up * FMath::Cos(Angle) + Right * FMath::Sin(Angle)) * D;
		}
		SpawnTarget(Inside(At), L->bMovingTargets, Index);
	}
	// CPUs: up the screen (side by side, or one behind the other for the beam); in a match-like lesson, on the far spawns.
	for (int32 Index = 0; Index < L->CPUCount && bMatch; ++Index)
	{
		// (The spawn points across from the player's first: furthest first.)
		TArray<FVector> Others(MatchSpawns.GetData() + 1, MatchSpawns.Num() - 1);
		Others.Sort([&MatchSpawns](const FVector& A, const FVector& B) { return FVector::DistSquared2D(A, MatchSpawns[0]) > FVector::DistSquared2D(B, MatchSpawns[0]); });
		SpawnCPU(Others[Index % Others.Num()]);
	}
	for (int32 Index = 0; Index < L->CPUCount && !bMatch; ++Index)
	{
		// (The drive course's CPU at the far end of its road.)
		FVector At = L->bDriveCourse && !CoursePath.IsEmpty() ? CoursePath.Last() : Start + Up * D;
		if (L->CPUCount > 1)
		{
			// The beam: one behind the other. Three: a fan in front and to the sides. Else side by side.
			const float Angle = FMath::DegreesToRadians(-55.0f + 110.0f * Index / FMath::Max(L->CPUCount - 1, 1));
			At = L->Goal == EGoal::BeamBoth ? Start + Up * (D * 0.75f + Index * 420.0f)
				: L->CPUCount >= 3 ? Start + (Up * FMath::Cos(Angle) + Right * FMath::Sin(Angle)) * D
				: Start + Up * D + Right * (Index - (L->CPUCount - 1) * 0.5f) * 520.0f;
		}
		SpawnCPU(L->bDriveCourse ? At : Inside(At));
	}
	// The player's balls.
	if (L->Give > 0)
	{
		Player->GiveContestedPickup(L->Ball);
		if (L->SecondBall != EChaosImpactBallType::Normal)
		{
			Player->GiveContestedPickup(L->SecondBall);
		}
	}
	LastBalls = Player->GetCarriedBallCount();
	LastTypes = Player->GetCarriedBallTypesPacked();
	RingAt = 0;
	Incoming.Reset();
	BeamHits.Reset();
	bWasCharging = bWasDashing = bWasFalling = bWasTackling = bWasInForm = bWasMapOpen = false;
	bChargeEndPending = false;
	EmptySince = -1.0;
}

bool AChaosImpactDojo::FindNextLesson(int32& OutCategory, int32& OutLesson) const
{
	if (ChaosImpactDojo::GetLesson(Category, LessonIndex + 1))
	{
		OutCategory = Category;
		OutLesson = LessonIndex + 1;
		return true;
	}
	if (ChaosImpactDojo::GetLesson(Category + 1, 0))
	{
		OutCategory = Category + 1;
		OutLesson = 0;
		return true;
	}
	return false;
}

bool AChaosImpactDojo::IsClearMenuUp() const
{
	return Phase == EChaosImpactDojoPhase::Clear && GetWorld() && GetWorld()->GetTimeSeconds() - PhaseAt >= ClearMenuDelay;
}

FBox2D AChaosImpactDojo::ClearButton(const int32 Index, const float W, const float H)
{
	const float S = FMath::Min(W / 1920.0f, H / 1080.0f);
	const float BW = 340.0f * S;
	const float BH = 74.0f * S;
	const float Gap = 28.0f * S;
	const float Left = W * 0.5f - (3.0f * BW + 2.0f * Gap) * 0.5f + Index * (BW + Gap);
	const float Top = H * 0.46f + 178.0f * S;
	return FBox2D(FVector2D(Left, Top), FVector2D(Left + BW, Top + BH));
}

void AChaosImpactDojo::TickClearMenu()
{
	AChaosImpactPlayerController* PC = GetPlayer();
	if (!PC || !IsClearMenuUp())
	{
		LastMouse = FVector2D::ZeroVector;
		return;
	}
	int32 NextCategory = 0;
	int32 NextLesson = 0;
	const bool bNext = FindNextLesson(NextCategory, NextLesson);
	if (!bNext && ClearChoice == 0)
	{
		ClearChoice = 1;
	}
	// (The press that cleared it, or one made as the buttons came up, is not taken.)
	const bool bAnswering = GetWorld()->GetTimeSeconds() - PhaseAt >= ClearMenuDelay + 0.25;
	const auto Move = [this, bNext](const int32 Way)
	{
		const int32 Was = ClearChoice;
		ClearChoice = FMath::Clamp(ClearChoice + Way, bNext ? 0 : 1, 2);
		if (ClearChoice != Was)
		{
			ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiSelect);
		}
	};
	if (PC->WasInputKeyJustPressed(EKeys::Left) || PC->WasInputKeyJustPressed(EKeys::A) || PC->WasInputKeyJustPressed(EKeys::Gamepad_DPad_Left)
		|| PC->WasInputKeyJustPressed(EKeys::Gamepad_LeftStick_Left))
	{
		Move(-1);
	}
	if (PC->WasInputKeyJustPressed(EKeys::Right) || PC->WasInputKeyJustPressed(EKeys::D) || PC->WasInputKeyJustPressed(EKeys::Gamepad_DPad_Right)
		|| PC->WasInputKeyJustPressed(EKeys::Gamepad_LeftStick_Right))
	{
		Move(1);
	}
	// The mouse: over a button picks it; a click on one takes it.
	int32 Hovered = INDEX_NONE;
	float MouseX = 0.0f;
	float MouseY = 0.0f;
	if (PC->GetMousePosition(MouseX, MouseY))
	{
		FVector2D Viewport = LastHudSize;
		if (GEngine && GEngine->GameViewport)
		{
			GEngine->GameViewport->GetViewportSize(Viewport);
		}
		const FVector2D Mouse(MouseX * LastHudSize.X / FMath::Max(Viewport.X, 1.0), MouseY * LastHudSize.Y / FMath::Max(Viewport.Y, 1.0));
		for (int32 Index = bNext ? 0 : 1; Index < 3; ++Index)
		{
			if (ClearButton(Index, static_cast<float>(LastHudSize.X), static_cast<float>(LastHudSize.Y)).IsInside(Mouse))
			{
				Hovered = Index;
			}
		}
		if (Hovered != INDEX_NONE && !LastMouse.IsZero() && !Mouse.Equals(LastMouse, 1.0) && Hovered != ClearChoice)
		{
			ClearChoice = Hovered;
			ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiSelect);
		}
		LastMouse = Mouse;
	}
	if (!bAnswering)
	{
		return;
	}
	const bool bClick = PC->WasInputKeyJustPressed(EKeys::LeftMouseButton);
	const bool bSwap = ChaosImpactSettings::IsSwitchConfirmRight();
	const bool bKey = PC->WasInputKeyJustPressed(EKeys::Enter) || PC->WasInputKeyJustPressed(EKeys::SpaceBar)
		|| PC->WasInputKeyJustPressed(bSwap ? EKeys::Gamepad_FaceButton_Right : EKeys::Gamepad_FaceButton_Bottom);
	if (bClick && Hovered != INDEX_NONE)
	{
		ChooseAfterClear(Hovered);
	}
	else if (bKey)
	{
		ChooseAfterClear(ClearChoice);
	}
}

void AChaosImpactDojo::ChooseAfterClear(const int32 Choice)
{
	if (Phase != EChaosImpactDojoPhase::Clear)
	{
		return;
	}
	ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiConfirm);
	AChaosImpactPlayerController* PC = GetPlayer();
	int32 NextCategory = 0;
	int32 NextLesson = 0;
	if (Choice == 0 && FindNextLesson(NextCategory, NextLesson))
	{
		const FChaosImpactDojoLesson* Next = ChaosImpactDojo::GetLesson(NextCategory, NextLesson);
		if (IsMatchWorld() && PC && Next)
		{
			// From a mock match: back to the dojo on that lesson's level, straight into it.
			Phase = EChaosImpactDojoPhase::Idle;
			PC->OpenDojo(ChaosImpactDojo::GetLessonLevel(*Next), FString::Printf(TEXT("%d_%d"), NextCategory, NextLesson));
			return;
		}
		StartLesson(NextCategory, NextLesson);
	}
	else if (Choice == 1)
	{
		RetryLesson();
	}
	else
	{
		ReturnToList();
	}
}

bool AChaosImpactDojo::IsRootedLesson(const FChaosImpactDojoLesson& Lesson)
{
	return Lesson.Goal == EGoal::HitTarget || Lesson.bDriveCourse;
}

void AChaosImpactDojo::ClearLesson()
{
	if (AChaosImpactCharacter* Player = GetPlayerCharacter())
	{
		Player->SetDojoRooted(false);
	}
	if (AChaosImpactDojoCourse* Laid = Course.Get())
	{
		Laid->Destroy();
	}
	Course.Reset();
	CoursePath.Reset();
	CourseBall.Reset();
	for (FDojoCPU& CPU : CPUs)
	{
		if (AChaosImpactCharacter* Pawn = CPU.Pawn.Get())
		{
			Pawn->Destroy();
		}
		if (AChaosImpactCPUController* Brain = CPU.Brain.Get())
		{
			Brain->Destroy();
		}
	}
	CPUs.Reset();
	for (const TWeakObjectPtr<AChaosImpactTrainingTarget>& Target : Targets)
	{
		if (Target.IsValid())
		{
			Target->Destroy();
		}
	}
	Targets.Reset();
	TargetDown.Reset();
	Pickups.Reset();
	Rings.Reset();
	RingAt = 0;
	if (AChaosImpactSuddenDeathZone* Old = Zone.Get())
	{
		Old->Destroy();
	}
	Zone.Reset();
	Incoming.Reset();
	BeamHits.Reset();
	// Whatever was thrown or left lying, and what it left behind.
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	for (TActorIterator<AChaosImpactBall> It(World); It; ++It) { It->Destroy(); }
	for (TActorIterator<AChaosImpactHazardZone> It(World); It; ++It) { It->Destroy(); }
	for (TActorIterator<AChaosImpactTornado> It(World); It; ++It) { It->Destroy(); }
	for (TActorIterator<AChaosImpactHook> It(World); It; ++It) { It->Destroy(); }
	for (TActorIterator<AChaosImpactSimaeBird> It(World); It; ++It) { It->Destroy(); }
	for (TActorIterator<AChaosImpactSimaeFeatherBurst> It(World); It; ++It) { It->Destroy(); }
	for (TActorIterator<AChaosImpactNovaPart> It(World); It; ++It) { It->Destroy(); }
	if (RingMesh)
	{
		ChaosImpactLightning::SetMesh(RingMesh, ChaosImpactIceMeshes::FMeshBuffers());
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Running it
// ---------------------------------------------------------------------------------------------------------------------

void AChaosImpactDojo::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// A real match opened for a mock-match lesson: the match runs itself; the dojo keeps only its task.
	if (IsMatchWorld())
	{
		if (Phase == EChaosImpactDojoPhase::Idle)
		{
			FString GroupText;
			FString LessonText;
			if (FString(GetWorld()->URL.GetOption(TEXT("CIDojoLesson="), TEXT(""))).Split(TEXT("_"), &GroupText, &LessonText)
				&& ChaosImpactDojo::GetLesson(FCString::Atoi(*GroupText), FCString::Atoi(*LessonText)))
			{
				BeginMatchLesson(FCString::Atoi(*GroupText), FCString::Atoi(*LessonText));
			}
			return;
		}
		TickMatchLesson();
		return;
	}
	if (!bStageRead)
	{
		ReadStage();
		// (Back from a mock match: the list on that lesson.)
		FString FocusGroup;
		FString FocusLesson;
		if (Phase == EChaosImpactDojoPhase::Idle
			&& FString(GetWorld()->URL.GetOption(TEXT("CIDojoFocus="), TEXT(""))).Split(TEXT("_"), &FocusGroup, &FocusLesson)
			&& ChaosImpactDojo::GetLesson(FCString::Atoi(*FocusGroup), FCString::Atoi(*FocusLesson)))
		{
			Category = FCString::Atoi(*FocusGroup);
			LessonIndex = FCString::Atoi(*FocusLesson);
		}
		if (bStageRead && IsRecordingRun())
		{
			BeginRecording();
			return;
		}
		// Opened on a lesson (it is held on this level): straight into it.
		FString Asked;
		int32 AskedGroup = INDEX_NONE;
		int32 AskedLesson = INDEX_NONE;
		FString GroupText;
		FString LessonText;
		if (bStageRead && GetWorld()->URL.GetOption(TEXT("CIDojoLesson="), nullptr)
			&& FString(GetWorld()->URL.GetOption(TEXT("CIDojoLesson="), TEXT(""))).Split(TEXT("_"), &GroupText, &LessonText))
		{
			AskedGroup = FCString::Atoi(*GroupText);
			AskedLesson = FCString::Atoi(*LessonText);
		}
		if (bStageRead && ChaosImpactDojo::GetLesson(AskedGroup, AskedLesson) && !bAskedStarted)
		{
			bAskedStarted = true;
			StartLesson(AskedGroup, AskedLesson);
			return;
		}
		if (bStageRead && Phase == EChaosImpactDojoPhase::Idle)
		{
			FreezePlayer(true);
			if (AChaosImpactPlayerController* PC = GetPlayer())
			{
				PC->ShowMenuScreen(EChaosImpactScreen::DojoSelect);
			}
		}
		return;
	}
	if (bRecording)
	{
		TickRecord(DeltaSeconds);
		PaintRings(DeltaSeconds);
		return;
	}
	TickVideo();
	TickInput();
	if (Phase == EChaosImpactDojoPhase::Play)
	{
		TickPlay(DeltaSeconds);
	}
	else if (Phase == EChaosImpactDojoPhase::Clear)
	{
		TickClearMenu();
	}
	UpdateCameraLead(DeltaSeconds);
	PaintRings(DeltaSeconds);
}

void AChaosImpactDojo::BuildCourse()
{
	// Up the open patch from its near edge to its far one, swinging side to side: a mouth, three bends, a straight end.
	const float Room = FMath::Max(HalfExtent - 160.0f, 500.0f);
	const float Swing = FMath::Min(Room * 0.36f, 330.0f);
	const float Ups[] = {0.0f, 0.2f, 0.37f, 0.55f, 0.73f, 0.88f, 1.0f};
	const float Sides[] = {0.0f, 0.0f, 1.0f, -1.0f, 1.0f, 0.0f, 0.0f};
	CoursePath.Reset();
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Ups); ++Index)
	{
		CoursePath.Add(Center + Up * (-Room + Ups[Index] * 2.0f * Room) + Right * (Sides[Index] * Swing));
	}
	// The stage's floor is not level under it (decks, pads): the road is raised level over the highest of it.
	float Highest = -TNumericLimits<float>::Max();
	float Lowest = TNumericLimits<float>::Max();
	for (int32 Index = 1; Index < CoursePath.Num(); ++Index)
	{
		for (int32 Step = 0; Step <= 8; ++Step)
		{
			const float Floor = static_cast<float>(OnFloor(FMath::Lerp(CoursePath[Index - 1], CoursePath[Index], Step / 8.0f)).Z);
			Highest = FMath::Max(Highest, Floor);
			Lowest = FMath::Min(Lowest, Floor);
		}
	}
	for (FVector& Point : CoursePath)
	{
		Point.Z = Highest + 6.0f;
	}
	Start = CoursePath[0];
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (AChaosImpactDojoCourse* Laid = GetWorld()->SpawnActor<AChaosImpactDojoCourse>(AChaosImpactDojoCourse::StaticClass(), FTransform::Identity, Parameters))
	{
		Laid->Build(CoursePath, CourseHalfWidth, CourseMouth, Lowest);
		Course = Laid;
	}
	CourseBall.Reset();
}

void AChaosImpactDojo::TickCourse()
{
	AChaosImpactCharacter* Player = GetPlayerCharacter();
	if (!Player || CoursePath.Num() < 2)
	{
		return;
	}
	const float Total = AChaosImpactDojoCourse::PathLength(CoursePath);
	const float Touch = CourseHalfWidth - 24.0f;
	// The drive ball in flight down the road.
	AChaosImpactBall* Flying = nullptr;
	for (TActorIterator<AChaosImpactBall> It(GetWorld()); It; ++It)
	{
		if (It->WasThrownBy(Player) && !It->IsPickup() && It->GetBallType() == EChaosImpactBallType::Drive && !It->IsActorBeingDestroyed())
		{
			Flying = *It;
			break;
		}
	}
	if (Flying && Flying != CourseBall.Get())
	{
		CourseBall = Flying;
		CourseBallDistance = 0.0f;
		CourseBallAlong = 0.0f;
	}
	const auto Touched = [this](AChaosImpactBall* Ball)
	{
		if (Ball)
		{
			Ball->FizzleDrive();
		}
		CourseBall.Reset();
		RestartTask(TEXT("壁にぶつかった！ はじめから"));
	};
	if (AChaosImpactBall* Ball = CourseBall.Get(); Ball && !Ball->IsPickup() && !Ball->IsActorBeingDestroyed())
	{
		float Along = 0.0f;
		const float Distance = AChaosImpactDojoCourse::DistanceToPath(CoursePath, Ball->GetActorLocation(), Along);
		CourseBallDistance = Distance;
		CourseBallAlong = Along;
		if (Along > CourseMouth && Along < Total - 90.0f && Distance > Touch)
		{
			Touched(Ball);
		}
	}
	else if (CourseBall.IsValid() || (!Flying && CourseBallAlong > 0.0f))
	{
		// It ended (on a wall it met before the line was crossed this frame): over, unless it reached the CPU's end.
		if (CourseBallAlong > CourseMouth && CourseBallAlong < Total - 90.0f && CourseBallDistance > Touch - 30.0f)
		{
			Touched(nullptr);
		}
		CourseBall.Reset();
		CourseBallAlong = 0.0f;
	}
}

bool AChaosImpactDojo::IsMatchWorld() const
{
	return GetWorld() && GetWorld()->URL.HasOption(TEXT("CIMatch=1"));
}

FChaosImpactMatchRules AChaosImpactDojo::MatchRulesFor(const FChaosImpactDojoLesson& Lesson)
{
	FChaosImpactMatchRules Rules;
	Rules.Minutes = ChaosImpactMatch::MinuteChoices[0];
	Rules.TeamCount = Lesson.MatchTeams;
	Rules.CPUCount = Lesson.MatchCPUs;
	Rules.StageIndex = 0;
	Rules.CPULevel = Lesson.CPULevel;
	Rules.bDuel = Lesson.bMatchDuel;
	return Rules;
}

void AChaosImpactDojo::BeginMatchLesson(const int32 InCategory, const int32 InLesson)
{
	Category = InCategory;
	LessonIndex = InLesson;
	bClearedBefore = ChaosImpactDojo::IsCleared(GetLesson()->Id);
	Slide = GetLesson()->Slides.Num();
	Done = 0;
	MatchOverAt = 0.0;
	CountAt = FailAt = -100.0;
	Phase = EChaosImpactDojoPhase::Play;
	PhaseAt = TaskStartAt = GetWorld()->GetTimeSeconds();
	UE_LOG(LogChaosImpact, Log, TEXT("Dojo: mock match %s"), *GetLesson()->Id);
}

void AChaosImpactDojo::TickMatchLesson()
{
	const FChaosImpactDojoLesson* L = GetLesson();
	AChaosImpactGameState* Match = GetWorld()->GetGameState<AChaosImpactGameState>();
	const AChaosImpactPlayerController* PC = GetPlayer();
	if (!L || !Match || !PC)
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	// Teams as a team match deals them out (the player's side gets a CPU): no team picking in a lesson.
	if (Match->Phase == EChaosImpactOnlinePhase::TeamSelect)
	{
		if (AChaosImpactGameMode* Mode = GetWorld()->GetAuthGameMode<AChaosImpactGameMode>())
		{
			Mode->ConfirmTeamsAndStart();
		}
	}
	if (Phase == EChaosImpactDojoPhase::Play)
	{
		// The match's own numbers: this player's knockouts, hits, or duel rounds won.
		const AChaosImpactPlayerState* Me = PC->GetPlayerState<AChaosImpactPlayerState>();
		int32 Reached = 0;
		if (L->bMatchDuel)
		{
			const int32 Side = Match->Duel.SideOf(Me);
			Reached = Side == 0 || Side == 1 ? Match->Duel.Wins[Side] : 0;
		}
		else if (Me)
		{
			Reached = L->Goal == EGoal::HitCPU ? Me->Hits : Me->Knockouts;
		}
		if (Reached > Done)
		{
			Count(Reached - Done);
		}
		else if (Match->Phase == EChaosImpactOnlinePhase::Results && MatchOverAt <= 0.0)
		{
			// The match over first: its results shown a while, then back to the list.
			MatchOverAt = Now;
			Fail(TEXT("試合終了！ もう一度ちょうせんしよう"));
		}
		if (MatchOverAt > 0.0 && Now - MatchOverAt > 6.0)
		{
			LeaveMatch();
		}
	}
	else if (Phase == EChaosImpactDojoPhase::Clear)
	{
		TickClearMenu();
	}
}

void AChaosImpactDojo::LeaveMatch()
{
	AChaosImpactPlayerController* PC = GetPlayer();
	if (!PC || Phase == EChaosImpactDojoPhase::Idle)
	{
		return;
	}
	Phase = EChaosImpactDojoPhase::Idle;
	PC->OpenDojo(FString(), FString(), FString::Printf(TEXT("%d_%d"), Category, LessonIndex));
}

FString AChaosImpactDojo::GetMissingControl() const
{
	// What this lesson asks for that the player's control type has no key for (the simple and mouse types have no jump
	// and no throw cancel).
	const FChaosImpactDojoLesson* L = GetLesson();
	const AChaosImpactPlayerController* PC = GetPlayer();
	if (!L || !PC)
	{
		return FString();
	}
	const auto Unset = [PC](const EChaosImpactAction Action)
	{
		return ChaosImpactSettings::DescribeAction(PC, Action) == TEXT("－");
	};
	if (L->Goal == EChaosImpactDojoGoal::Jump && Unset(EChaosImpactAction::Jump))
	{
		return TEXT("ジャンプ");
	}
	if (L->Goal == EChaosImpactDojoGoal::Cancel && Unset(EChaosImpactAction::CancelThrow))
	{
		return TEXT("投げのキャンセル");
	}
	return FString();
}

void AChaosImpactDojo::UpdateCameraLead(const float DeltaSeconds)
{
	AChaosImpactCharacter* Player = GetPlayerCharacter();
	if (!Player)
	{
		return;
	}
	// The camera looks a way toward what the lesson asks for (the targets, the CPUs, the ring next, the balls, the parts),
	// so they stand in the picture rather than at its top edge or under the task card.
	FVector Want = FVector::ZeroVector;
	const bool bLesson = Phase == EChaosImpactDojoPhase::Ready || Phase == EChaosImpactDojoPhase::Play || Phase == EChaosImpactDojoPhase::Clear;
	if (bLesson)
	{
		const FVector Me = Player->GetActorLocation();
		FVector Sum = FVector::ZeroVector;
		int32 Num = 0;
		const auto Add = [&Sum, &Num, &Me](const FVector& At)
		{
			Sum += FVector(At.X - Me.X, At.Y - Me.Y, 0.0f);
			++Num;
		};
		if (Rings.IsValidIndex(RingAt))
		{
			Add(Rings[RingAt]);
		}
		for (const TWeakObjectPtr<AChaosImpactTrainingTarget>& Target : Targets)
		{
			if (Target.IsValid() && !Target->IsDefeated())
			{
				Add(Target->GetActorLocation());
			}
		}
		for (const FDojoCPU& CPU : CPUs)
		{
			if (CPU.Pawn.IsValid() && !CPU.Pawn->IsEliminated())
			{
				Add(CPU.Pawn->GetActorLocation());
			}
		}
		for (const TWeakObjectPtr<AChaosImpactBall>& Ball : Pickups)
		{
			if (Ball.IsValid() && Ball->IsPickup())
			{
				Add(Ball->GetActorLocation());
			}
		}
		for (TActorIterator<AChaosImpactNovaPart> It(GetWorld()); It && GetLesson() && GetLesson()->Goal == EGoal::NovaBuild; ++It)
		{
			Add(It->GetActorLocation());
		}
		if (Num > 0)
		{
			Want = (Sum / Num * 0.4f).GetClampedToMaxSize(320.0f);
		}
	}
	CameraLead = FMath::VInterpTo(CameraLead, Want, DeltaSeconds, 2.5f);
	Player->SetDojoCameraLead(CameraLead);
}

void AChaosImpactDojo::TickVideo()
{
	if (!Video)
	{
		Video = NewObject<UChaosImpactDojoVideo>(this);
	}
	// The slide being read; in the list, the lesson picked; else nothing.
	FString Clip;
	const AChaosImpactPlayerController* PC = GetPlayer();
	if (Phase == EChaosImpactDojoPhase::Intro && GetLesson() && GetLesson()->Slides.IsValidIndex(Slide))
	{
		Clip = GetClipName(GetLesson()->Slides[Slide].Art, GetLesson()->Ball);
	}
	else if (PC && PC->GetCurrentScreen() == EChaosImpactScreen::DojoSelect)
	{
		if (const FChaosImpactDojoLesson* Picked = ChaosImpactDojo::GetLesson(PreviewCategory, PreviewLesson); Picked && !Picked->Slides.IsEmpty())
		{
			Clip = GetClipName(Picked->Slides[0].Art, Picked->Ball);
		}
	}
	Video->Want(Clip);
	Video->Tick();
}

void AChaosImpactDojo::TickInput()
{
	AChaosImpactPlayerController* PC = GetPlayer();
	const FChaosImpactDojoLesson* L = GetLesson();
	if (!PC || !L || PC->GetCurrentScreen() != EChaosImpactScreen::Playing)
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	// (A press that opened the slide is not taken for the next.)
	if (Now - SlideAt < 0.25)
	{
		return;
	}
	if (Phase == EChaosImpactDojoPhase::Intro)
	{
		if (IsConfirm(PC))
		{
			++Slide;
			SlideAt = Now;
			if (Slide >= L->Slides.Num())
			{
				Phase = EChaosImpactDojoPhase::Ready;
				PhaseAt = Now;
				ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiReveal);
			}
			else
			{
				ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiConfirm);
			}
		}
		else if (IsBack(PC) && Slide > 0)
		{
			--Slide;
			SlideAt = Now;
			ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiBack);
		}
	}
	else if (Phase == EChaosImpactDojoPhase::Ready)
	{
		if (IsConfirm(PC) && !GetMissingControl().IsEmpty())
		{
			// Not in this control type: on to the next lesson held here (or back to the list).
			ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiConfirm);
			const int32 Next = LessonIndex + 1;
			if (ChaosImpactDojo::GetLesson(Category, Next) && IsLessonHere(*ChaosImpactDojo::GetLesson(Category, Next)))
			{
				StartLesson(Category, Next);
			}
			else
			{
				ReturnToList();
			}
		}
		else if (IsConfirm(PC) && L->bRealMatch)
		{
			ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiConfirm);
			PC->OpenDojoMatch(MatchRulesFor(*L), FString::Printf(TEXT("%d_%d"), Category, LessonIndex));
		}
		else if (IsConfirm(PC))
		{
			BeginPlayPhase();
		}
		else if (IsBack(PC) && !L->Slides.IsEmpty())
		{
			Phase = EChaosImpactDojoPhase::Intro;
			Slide = L->Slides.Num() - 1;
			SlideAt = Now;
			ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiBack);
		}
	}
}

void AChaosImpactDojo::DebugBegin()
{
	if (Phase == EChaosImpactDojoPhase::Intro || Phase == EChaosImpactDojoPhase::Ready)
	{
		if (const FChaosImpactDojoLesson* L = GetLesson())
		{
			Slide = L->Slides.Num();
		}
		BeginPlayPhase();
	}
}

void AChaosImpactDojo::DebugCount(const int32 Steps)
{
	if (Phase == EChaosImpactDojoPhase::Play)
	{
		Count(Steps);
	}
}

void AChaosImpactDojo::BeginPlayPhase()
{
	const FChaosImpactDojoLesson* L = GetLesson();
	if (!L)
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	Phase = EChaosImpactDojoPhase::Play;
	PhaseAt = SlideAt = Now;
	FreezePlayer(false);
	for (FDojoCPU& CPU : CPUs)
	{
		// (Several throwers take turns rather than all at once.)
		CPU.NextActAt = Now + 1.0 + (L->ThrowEvery / FMath::Max(CPUs.Num(), 1)) * (&CPU - CPUs.GetData());
	}
	TaskStartAt = Now;
	bRestartPending = false;
	if (L->Goal == EGoal::StayInZone)
	{
		if (AChaosImpactSuddenDeathZone* Circle = Zone.Get())
		{
			const AGameStateBase* State = GetWorld()->GetGameState();
			const double ServerNow = State ? State->GetServerWorldTimeSeconds() : Now;
			Circle->Begin(HalfExtent * 0.85f, ServerNow, AChaosImpactSuddenDeathZone::DelaySeconds + AChaosImpactSuddenDeathZone::SweepSeconds
				+ L->Count + 6.0f);
		}
		ZoneTime = 0.0f;
		OutTime = 0.0f;
	}
	ChaosImpactSfx::Play2D(this, EChaosImpactSfx::MatchGo);
}

void AChaosImpactDojo::Count(const int32 Steps)
{
	const FChaosImpactDojoLesson* L = GetLesson();
	if (!L || Phase != EChaosImpactDojoPhase::Play)
	{
		return;
	}
	Done = FMath::Min(Done + Steps, L->Count);
	CountAt = GetWorld()->GetTimeSeconds();
	UE_CLOG(bRecording, LogChaosImpact, Log, TEXT("Dojo record: counted %d / %d"), Done, L->Count);
	if (Done >= L->Count)
	{
		OnCleared();
	}
	else
	{
		ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiSelect, 1.0f, 1.0f + 0.08f * Done);
	}
}

void AChaosImpactDojo::Fail(const FString& Why)
{
	FailAt = GetWorld()->GetTimeSeconds();
	FailText = Why;
	ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiDeny);
}

void AChaosImpactDojo::OnCleared()
{
	if (bRecording)
	{
		// (Filming: the task goes on, counted from nothing again.)
		UE_LOG(LogChaosImpact, Log, TEXT("Dojo record: the task was done on film"));
		Done = 0;
		return;
	}
	Phase = EChaosImpactDojoPhase::Clear;
	PhaseAt = GetWorld()->GetTimeSeconds();
	FreezePlayer(true);
	ClearChoice = 0;
	LastMouse = FVector2D::ZeroVector;
	if (const FChaosImpactDojoLesson* L = GetLesson())
	{
		ChaosImpactDojo::MarkCleared(L->Id);
		UE_LOG(LogChaosImpact, Log, TEXT("Dojo: lesson %s CLEAR"), *L->Id);
	}
	ChaosImpactSfx::Play2D(this, EChaosImpactSfx::SoloClear);
}

void AChaosImpactDojo::OnCharacterDamaged(AChaosImpactCharacter* Victim, const float Damage, AActor* Causer, APawn* Source)
{
	const FChaosImpactDojoLesson* L = GetLesson();
	AChaosImpactCharacter* Player = GetPlayerCharacter();
	// (In a mock match the task is counted from the match's own numbers.)
	if (!L || Phase != EChaosImpactDojoPhase::Play || !Victim || Damage <= 0.0f || IsMatchWorld())
	{
		return;
	}
	if (Victim == Player)
	{
		PlayerHitAt = GetWorld()->GetTimeSeconds();
		// (A no-damage lesson starts over: done in the next tick, not inside the hit.)
		bRestartPending |= L->bNoDamage && L->Goal != EGoal::Dodge && L->Goal != EGoal::DashThrough;
		return;
	}
	const bool bLessonCPU = CPUs.ContainsByPredicate([Victim](const FDojoCPU& CPU) { return CPU.Pawn.Get() == Victim; });
	if (!bLessonCPU || !Player || (Source && Source != Player))
	{
		return;
	}
	const EChaosImpactBallType Kind = KindOf(Causer);
	if (L->Goal == EGoal::NovaBuild && Kind == EChaosImpactBallType::Nova && Done >= 3)
	{
		Count(L->Count - Done);
		return;
	}
	if (L->Goal == EGoal::HitCPU && (L->HitKind == EChaosImpactBallType::Normal || Kind == L->HitKind))
	{
		Count();
	}
	else if (L->Goal == EGoal::HitCPU && Kind != L->HitKind)
	{
		Fail(FString::Printf(TEXT("%sで当てよう"), ChaosImpactBallTypes::GetDisplayName(L->HitKind)));
	}
	else if (L->Goal == EGoal::BeamBoth && Kind == EChaosImpactBallType::Beam && Causer)
	{
		int32& Hits = BeamHits.FindOrAdd(Causer);
		if (++Hits >= 2)
		{
			Count();
		}
	}
}

void AChaosImpactDojo::TickPlay(const float DeltaSeconds)
{
	TickCPUs(DeltaSeconds);
	TickPlayerBalls();
	TickGoal(DeltaSeconds);
	const FChaosImpactDojoLesson* L = GetLesson();
	if (!L || Phase != EChaosImpactDojoPhase::Play || bRecording)
	{
		return;
	}
	if (L->bDriveCourse)
	{
		TickCourse();
	}
	if (bRestartPending)
	{
		RestartTask(TEXT("当たった！ はじめから"));
	}
	else if (L->TimeLimit > 0.0f && GetWorld()->GetTimeSeconds() - TaskStartAt > L->TimeLimit)
	{
		RestartTask(TEXT("時間切れ！ はじめから"));
	}
}

void AChaosImpactDojo::RestartTask(const FString& Why)
{
	const FChaosImpactDojoLesson* L = GetLesson();
	AChaosImpactCharacter* Player = GetPlayerCharacter();
	if (!L || !Player)
	{
		return;
	}
	bRestartPending = false;
	Done = 0;
	RingAt = 0;
	Fail(Why);
	// Back to the start whole (the CPUs too); the time from now.
	const float HalfHeight = Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector From = L->Goal == EGoal::StayInZone ? Center : Start;
	Player->ResetForOnlineMatch(OnFloor(From) + FVector(0.0f, 0.0f, HalfHeight + 4.0f), Up.Rotation());
	for (FDojoCPU& CPU : CPUs)
	{
		if (AChaosImpactCharacter* Pawn = CPU.Pawn.Get(); Pawn && !Pawn->IsEliminated())
		{
			Pawn->ResetForOnlineMatch(CPU.Home + FVector(0.0f, 0.0f, HalfHeight + 4.0f), (Start - CPU.Home).GetSafeNormal2D().Rotation());
		}
		CPU.bCharging = false;
	}
	Incoming.Reset();
	if (L->Give > 0)
	{
		Player->GiveContestedPickup(L->Ball);
	}
	if (L->Goal == EGoal::StayInZone)
	{
		BeginPlayPhase();
	}
	TaskStartAt = GetWorld()->GetTimeSeconds();
}

void AChaosImpactDojo::TickPlayerBalls()
{
	const FChaosImpactDojoLesson* L = GetLesson();
	AChaosImpactCharacter* Player = GetPlayerCharacter();
	if (!L || !Player || L->Give <= 0 || !L->bRefill || Player->IsEliminated())
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	const int32 Want = L->SecondBall != EChaosImpactBallType::Normal ? 2 : 1;
	// The drop lesson leaves the floor to be picked from; the rest are topped up a moment after the hands are empty.
	if (Player->GetCarriedBallCount() >= Want || Player->IsChargingThrow() || Player->IsThrowReleasePending())
	{
		EmptySince = -1.0;
		return;
	}
	if (EmptySince < 0.0)
	{
		EmptySince = Now;
	}
	if (Now - EmptySince > (L->Goal == EGoal::Drop ? 1.2 : 0.55))
	{
		const bool bHasFirst = Player->GetCarriedBallCount() > 0 && Player->GetCarriedBallType(0) == L->Ball;
		Player->GiveContestedPickup(bHasFirst && Want == 2 ? L->SecondBall : L->Ball);
		EmptySince = -1.0;
		// (The drop lesson: whatever was put down goes, so the floor does not fill up.)
		if (L->Goal == EGoal::Drop)
		{
			for (TActorIterator<AChaosImpactBall> It(GetWorld()); It; ++It)
			{
				if (It->IsPickup())
				{
					It->Destroy();
				}
			}
		}
	}
}

void AChaosImpactDojo::TickCPUs(const float DeltaSeconds)
{
	const FChaosImpactDojoLesson* L = GetLesson();
	AChaosImpactCharacter* Player = GetPlayerCharacter();
	if (!L || !Player)
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	for (FDojoCPU& CPU : CPUs)
	{
		AChaosImpactCharacter* Pawn = CPU.Pawn.Get();
		if (!Pawn || Pawn->IsEliminated())
		{
			CPU.bCharging = false;
			continue;
		}
		const FVector ToPlayer = (Player->GetActorLocation() - Pawn->GetActorLocation()).GetSafeNormal2D();
		// A fighting CPU is kept in balls as the player is (the stage's own pads are quiet here).
		if (L->CPU == ECPU::Fighter && Pawn->GetCarriedBallCount() == 0 && !Pawn->IsChargingThrow() && !Pawn->IsThrowReleasePending())
		{
			if (CPU.NextActAt <= 0.0 || Now < CPU.NextActAt - 100.0)
			{
				CPU.NextActAt = Now + 0.9;
			}
			else if (Now >= CPU.NextActAt)
			{
				Pawn->GiveContestedPickup(L->Ball);
				CPU.NextActAt = 0.0;
			}
		}
		switch (L->CPU)
		{
		case ECPU::Walk:
		{
			// To and fro across the screen, a little faster the further the lesson.
			CPU.Phase += DeltaSeconds;
			const FVector Goal = CPU.Home + Right * FMath::Sin(CPU.Phase * 0.85f) * 520.0f;
			const FVector Way = (Goal - Pawn->GetActorLocation()).GetSafeNormal2D();
			if (FVector::Dist2D(Goal, Pawn->GetActorLocation()) > 40.0f)
			{
				Pawn->AddMovementInput(Way, 1.0f);
			}
			break;
		}
		case ECPU::Stand:
			Pawn->SetActorRotation(ToPlayer.Rotation());
			break;
		case ECPU::Thrower:
			Pawn->SetActorRotation(ToPlayer.Rotation());
			if (!CPU.bCharging && Now >= CPU.NextActAt)
			{
				if (Pawn->GetCarriedBallCount() == 0)
				{
					Pawn->GiveContestedPickup(L->CPUBall);
				}
				Pawn->SetAIAimDirection(ToPlayer);
				Pawn->BeginThrowInput();
				CPU.bCharging = true;
				CPU.ChargeAt = Now;
			}
			else if (CPU.bCharging && Now - CPU.ChargeAt >= 0.6)
			{
				Pawn->SetAIAimDirection(ToPlayer);
				CPU.NextActAt = Now + L->ThrowEvery * (CPUs.Num() > 1 ? 1.0f + 0.35f * (CPUs.Num() - 1) : 1.0f);
				UE_LOG(LogChaosImpact, Verbose, TEXT("Dojo: CPU throws from %s aiming %s (aim now %s)"), *Pawn->GetActorLocation().ToCompactString(),
					*ToPlayer.ToCompactString(), *Pawn->GetAimDirection().ToCompactString());
				Pawn->EndThrowInput();
				CPU.bCharging = false;
			}
			break;
		case ECPU::Charger:
		{
			Pawn->SetActorRotation(ToPlayer.Rotation());
			const bool bChargingNow = Pawn->IsChargingThrow();
			if (CPU.bCharging && !bChargingNow)
			{
				// Its charge was stopped: by the player's tackle, if one was just made close by.
				const UChaosImpactTackleComponent* Tackle = Player->GetTackle();
				if (L->Goal == EGoal::Interrupt && Tackle && Tackle->IsBusy()
					&& FVector::Dist2D(Player->GetActorLocation(), Pawn->GetActorLocation()) < 420.0f)
				{
					Count();
				}
				CPU.bCharging = false;
				CPU.NextActAt = Now + 1.3;
			}
			else if (!CPU.bCharging && Now >= CPU.NextActAt)
			{
				if (Pawn->GetCarriedBallCount() == 0)
				{
					Pawn->GiveContestedPickup(EChaosImpactBallType::Normal);
				}
				Pawn->SetAIAimDirection(ToPlayer);
				Pawn->BeginThrowInput();
				CPU.bCharging = true;
				CPU.ChargeAt = Now;
			}
			else if (CPU.bCharging && Now - CPU.ChargeAt > 3.2)
			{
				// It never throws: put away, and again in a moment.
				Pawn->CancelChargingThrow();
				CPU.bCharging = false;
				CPU.NextActAt = Now + 0.7;
			}
			break;
		}
		default:
			break;
		}
	}
}

void AChaosImpactDojo::TickGoal(const float DeltaSeconds)
{
	const FChaosImpactDojoLesson* L = GetLesson();
	AChaosImpactPlayerController* PC = GetPlayer();
	AChaosImpactCharacter* Player = GetPlayerCharacter();
	if (!L || !PC || !Player)
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	const bool bCharging = Player->IsChargingThrow();
	const int32 Balls = Player->GetCarriedBallCount();
	const uint8 Types = Player->GetCarriedBallTypesPacked();
	if (bCharging)
	{
		MaxCharge = FMath::Max(MaxCharge, Player->GetThrowChargeAlpha());
	}
	if (bWasCharging && !bCharging)
	{
		ChargeEndedAt = Now;
		BallsAtChargeEnd = LastBalls;
		ChargeAtEnd = MaxCharge;
		bChargeEndPending = true;
	}
	if (!bCharging)
	{
		MaxCharge = bWasCharging ? MaxCharge : 0.0f;
	}
	const bool bDashing = Player->IsDashing();
	const UCharacterMovementComponent* Movement = Player->GetCharacterMovement();
	const bool bJumping = Movement && Movement->IsFalling() && Player->GetVelocity().Z > 60.0f;
	const bool bTackling = Player->GetTackle() && Player->GetTackle()->IsActive();
	const bool bInForm = Player->IsInBallForm();
	const bool bMap = PC->IsOverviewMapOpen();
	const bool bThrown = bChargeEndPending && Balls < BallsAtChargeEnd && Now - ChargeEndedAt < 1.2;

	switch (L->Goal)
	{
	case EGoal::Reach:
		if (Rings.IsValidIndex(RingAt) && FVector::Dist2D(Player->GetActorLocation(), Rings[RingAt]) < DojoRingRadius)
		{
			++RingAt;
			ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::StageTargetHit, Player->GetActorLocation(), 0.8f, 1.2f);
			Count();
		}
		break;
	case EGoal::PickUp:
		if (Balls > LastBalls)
		{
			Count(Balls - LastBalls);
		}
		break;
	case EGoal::Throw:
		if (bThrown)
		{
			Count();
		}
		break;
	case EGoal::ChargedThrow:
		if (bThrown)
		{
			if (ChargeAtEnd >= 0.97f)
			{
				Count();
			}
			else
			{
				Fail(TEXT("もっとためてから投げよう"));
			}
		}
		break;
	case EGoal::HitTarget:
		for (int32 Index = 0; Index < Targets.Num(); ++Index)
		{
			const bool bDown = Targets[Index].IsValid() && Targets[Index]->IsDefeated();
			if (bDown && !TargetDown[Index])
			{
				Count();
			}
			TargetDown[Index] = bDown;
		}
		break;
	case EGoal::KnockOut:
		for (FDojoCPU& CPU : CPUs)
		{
			const bool bOut = CPU.Pawn.IsValid() && CPU.Pawn->IsEliminated();
			if (bOut && !CPU.bWasEliminated)
			{
				Count();
			}
			CPU.bWasEliminated = bOut;
		}
		break;
	case EGoal::Dash:
		if (bDashing && !bWasDashing) { Count(); }
		break;
	case EGoal::Jump:
		if (bJumping && !bWasFalling) { Count(); }
		break;
	case EGoal::Tackle:
		if (bTackling && !bWasTackling)
		{
			for (const FDojoCPU& CPU : CPUs)
			{
				if (CPU.Pawn.IsValid() && FVector::Dist2D(CPU.Pawn->GetActorLocation(), Player->GetActorLocation()) < 380.0f)
				{
					Count();
					break;
				}
			}
			if (Phase == EChaosImpactDojoPhase::Play && !CPUs.IsEmpty() && CPUs[0].Pawn.IsValid()
				&& FVector::Dist2D(CPUs[0].Pawn->GetActorLocation(), Player->GetActorLocation()) >= 380.0f)
			{
				Fail(TEXT("もっと近づいてからタックル"));
			}
		}
		break;
	case EGoal::Swap:
		if (Balls == 2 && LastBalls == 2 && Types != LastTypes && !bCharging)
		{
			Count();
		}
		break;
	case EGoal::Cancel:
		if (bChargeEndPending && ChargeEndedAt >= PhaseAt && Now - ChargeEndedAt > 0.7 && Balls >= BallsAtChargeEnd && BallsAtChargeEnd > 0)
		{
			bChargeEndPending = false;
			Count();
		}
		else if (bThrown)
		{
			bChargeEndPending = false;
			Fail(TEXT("投げずにキャンセルしよう"));
		}
		break;
	case EGoal::Drop:
		if (!bCharging && !bWasCharging && Balls < LastBalls && !(bChargeEndPending && Now - ChargeEndedAt < 1.5))
		{
			Count();
		}
		break;
	case EGoal::BallForm:
		if (bInForm && !bWasInForm) { Count(); }
		break;
	case EGoal::OpenMap:
		if (bMap && !bWasMapOpen) { Count(); }
		break;
	case EGoal::Freeze:
	case EGoal::Blind:
	case EGoal::Trap:
	case EGoal::WindCarry:
	case EGoal::HookPull:
		for (FDojoCPU& CPU : CPUs)
		{
			const AChaosImpactCharacter* Pawn = CPU.Pawn.Get();
			const bool bFlag = Pawn && (L->Goal == EGoal::Freeze ? Pawn->IsIceFrozen()
				: L->Goal == EGoal::Blind ? Pawn->IsBlinded()
				: L->Goal == EGoal::Trap ? (Pawn->GetBallForm() && Pawn->GetBallForm()->IsCaptured())
				: L->Goal == EGoal::WindCarry ? Pawn->IsCarriedByWind()
				: Pawn->IsPulledByHook());
			if (bFlag && !CPU.bWasFlag)
			{
				Count();
			}
			CPU.bWasFlag = bFlag;
		}
		break;
	case EGoal::Dodge:
	case EGoal::DashThrough:
	{
		// The CPU's balls on their way: how close each comes, and whether the player was dashing then.
		for (TActorIterator<AChaosImpactBall> It(GetWorld()); It; ++It)
		{
			AChaosImpactBall* Ball = *It;
			if (Ball->IsPickup() || Ball->WasThrownBy(Player) || Incoming.ContainsByPredicate([Ball](const FIncoming& In) { return In.Ball.Get() == Ball; }))
			{
				continue;
			}
			FIncoming& In = Incoming.AddDefaulted_GetRef();
			In.Ball = Ball;
			In.ThrownAt = Now;
			UE_LOG(LogChaosImpact, Verbose, TEXT("Dojo: incoming ball at %s, velocity %s, player at %s"), *Ball->GetActorLocation().ToCompactString(),
				*Ball->GetVelocity().ToCompactString(), *Player->GetActorLocation().ToCompactString());
		}
		for (int32 Index = Incoming.Num() - 1; Index >= 0; --Index)
		{
			FIncoming& In = Incoming[Index];
			AChaosImpactBall* Ball = In.Ball.Get();
			if (Ball && !Ball->IsPickup() && !Ball->HasDetonated())
			{
				const float Near = static_cast<float>(FVector::Dist(Ball->GetActorLocation(), Player->GetActorLocation()));
				In.Closest = FMath::Min(In.Closest, Near);
				In.bDashedPast |= bDashing && Near < 260.0f;
			}
			const bool bOver = !Ball || Ball->IsPickup() || Ball->HasDetonated() || Now - In.ThrownAt > 2.6;
			if (!bOver)
			{
				continue;
			}
			if (PlayerHitAt >= In.ThrownAt)
			{
				Done = 0;
				if (!bRecording)
				{
					Fail(TEXT("当たった！ はじめから"));
					Player->ResetForOnlineMatch(Player->GetActorLocation(), Player->GetActorRotation());
				}
			}
			else if (L->Goal == EGoal::Dodge)
			{
				Count();
			}
			else if (In.bDashedPast)
			{
				Count();
			}
			else if (In.Closest < 400.0f)
			{
				Fail(TEXT("ダッシュですり抜けよう"));
			}
			Incoming.RemoveAt(Index);
		}
		break;
	}
	case EGoal::NovaBuild:
	{
		// Each part picked up counts; the nova they make, held, then thrown onto the CPU, is the last.
		const uint8 Parts = Player->GetNovaParts();
		const bool bHolding = Player->GetCarriedBallCount() > 0 && Player->GetCarriedBallType(0) == EChaosImpactBallType::Nova;
		const uint8 New = static_cast<uint8>(Parts & ~NovaPartsSeen);
		for (int32 Bit = 0; Bit < 3; ++Bit)
		{
			if (New & (1 << Bit))
			{
				Count();
			}
		}
		NovaPartsSeen |= Parts;
		if (bHolding && !bNovaHeld && Done < 3)
		{
			// (The last part made the nova at once: the parts were all counted.)
			Count(3 - Done);
		}
		// Thrown and missed: the parts are set out again.
		if (bNovaHeld && !bHolding && !Player->IsChargingThrow() && !Player->IsThrowReleasePending() && Done < L->Count)
		{
			bool bAny = false;
			for (TActorIterator<AChaosImpactNovaPart> It(GetWorld()); It; ++It)
			{
				bAny = true;
			}
			bool bFlying = false;
			for (TActorIterator<AChaosImpactBall> It(GetWorld()); It; ++It)
			{
				bFlying |= It->GetBallType() == EChaosImpactBallType::Nova;
			}
			if (!bAny && !bFlying)
			{
				Done = 0;
				NovaPartsSeen = 0;
				Fail(TEXT("外れた！ パーツをもう一度"));
				SpawnNovaParts();
			}
		}
		bNovaHeld = bHolding || (bNovaHeld && (Player->IsChargingThrow() || Player->IsThrowReleasePending()));
		break;
	}
	case EGoal::StayInZone:
		if (const AChaosImpactSuddenDeathZone* Circle = Zone.Get())
		{
			if (Circle->IsInside(Player->GetActorLocation()))
			{
				OutTime = 0.0f;
				const int32 Before = FMath::FloorToInt(ZoneTime);
				ZoneTime += DeltaSeconds;
				if (FMath::FloorToInt(ZoneTime) > Before)
				{
					Count();
				}
			}
			else
			{
				OutTime += DeltaSeconds;
				if (OutTime >= AChaosImpactSuddenDeathZone::OutSeconds)
				{
					// Back to the middle, the circle opened out again.
					RestartTask(TEXT("円の外に出た！ はじめから"));
				}
			}
		}
		break;
	default:
		break;
	}
	if (bChargeEndPending && Now - ChargeEndedAt > 1.6)
	{
		bChargeEndPending = false;
	}
	if (bThrown)
	{
		bChargeEndPending = false;
	}
	bWasCharging = bCharging;
	bWasDashing = bDashing;
	bWasFalling = bJumping;
	bWasTackling = bTackling;
	bWasInForm = bInForm;
	bWasMapOpen = bMap;
	LastBalls = Balls;
	LastTypes = Types;
}

void AChaosImpactDojo::PaintRings(const float DeltaSeconds)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	if (!RingMesh)
	{
		RingMaterial = ChaosImpactBallTypes::MakeAdditive(this, FLinearColor(0.15f, 0.85f, 1.0f), 3.0f);
		RingMesh = ChaosImpactLightning::CreateComponent(this, GetRootComponent(), RingMaterial);
		if (RingMesh)
		{
			RingMesh->SetUsingAbsoluteLocation(true);
			RingMesh->SetUsingAbsoluteRotation(true);
			RingMesh->SetUsingAbsoluteScale(true);
			RingMesh->SetWorldLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
		}
	}
	if (!RingMesh)
	{
		return;
	}
	// The next ring glows and pulses, a tall column of light rising from it; the rest wait faintly.
	const float Time = static_cast<float>(GetWorld()->GetTimeSeconds());
	ChaosImpactIceMeshes::FMeshBuffers Mesh;
	for (int32 Index = RingAt; Index < Rings.Num(); ++Index)
	{
		const bool bNext = Index == RingAt;
		const float Pulse = bNext ? 1.0f + 0.08f * FMath::Sin(Time * 6.0f) : 0.7f;
		ChaosImpactLightning::AppendRing(Mesh, Rings[Index] + FVector(0.0f, 0.0f, 6.0f), DojoRingRadius * Pulse, bNext ? 16.0f : 7.0f, 64);
		if (bNext)
		{
			ChaosImpactLightning::AppendRing(Mesh, Rings[Index] + FVector(0.0f, 0.0f, 6.0f), DojoRingRadius * 0.55f, 5.0f, 48);
			for (int32 Spoke = 0; Spoke < 6; ++Spoke)
			{
				const float Angle = Spoke * UE_TWO_PI / 6.0f + Time * 1.5f;
				const FVector Edge = Rings[Index] + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * DojoRingRadius * Pulse;
				ChaosImpactLightning::AppendRing(Mesh, Edge + FVector(0.0f, 0.0f, 60.0f + 40.0f * FMath::Sin(Time * 3.0f + Spoke)), 6.0f, 6.0f, 8);
			}
		}
	}
	ChaosImpactLightning::SetMesh(RingMesh, Mesh);
	if (RingMaterial)
	{
		RingMaterial->SetScalarParameterValue(TEXT("Intensity"), 2.5f + 1.5f * FMath::Sin(Time * 6.0f));
	}
}
