#include "ChaosImpactTitleDemo.h"

#include "ChaosImpactBallSpawner.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactGameMode.h"
#include "ChaosImpactMatchTypes.h"
#include "ChaosImpactStageBase.h"
#include "ChaosImpactVersusStage.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"

namespace
{
	/** How far back and up the camera sits from what it looks at, and how fast it circles (degrees a second). */
	constexpr float CameraBack = 1550.0f;
	constexpr float CameraUp = 1150.0f;
	constexpr float OrbitSpeed = 5.0f;
	/** The camera aims this far past the action, so the action sits low in the picture, under the logo. */
	constexpr float AimPast = 450.0f;
}

AChaosImpactTitleDemo::AChaosImpactTitleDemo()
{
	PrimaryActorTick.bCanEverTick = true;
	SetReplicates(false);

	Camera = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Camera"));
	SetRootComponent(Camera);
	Camera->FOVAngle = 58.0f;
	Camera->bCaptureEveryFrame = false;
	Camera->bCaptureOnMovement = false;
	Camera->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	Camera->ShowFlags.SetMotionBlur(false);
	// No colour: the title tints it into its own navy.
	Camera->PostProcessSettings.bOverride_ColorSaturation = true;
	Camera->PostProcessSettings.ColorSaturation = FVector4(0.0f, 0.0f, 0.0f, 1.0f);
}

void AChaosImpactTitleDemo::BeginPlay()
{
	Super::BeginPlay();
	Picture = NewObject<UTextureRenderTarget2D>(this);
	Picture->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8_SRGB;
	Picture->ClearColor = FLinearColor::Black;
	Picture->InitAutoFormat(PictureWidth, PictureHeight);
	Picture->UpdateResourceImmediate(true);
	Camera->TextureTarget = Picture;
	SetUpMatch();
}

void AChaosImpactTitleDemo::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (AActor* Actor : Spawned)
	{
		if (IsValid(Actor))
		{
			Actor->Destroy();
		}
	}
	Spawned.Reset();
	if (IsValid(Stage))
	{
		Stage->Destroy();
	}
	Super::EndPlay(EndPlayReason);
}

void AChaosImpactTitleDemo::SetUpMatch()
{
	UWorld* World = GetWorld();
	AChaosImpactGameMode* Mode = World ? World->GetAuthGameMode<AChaosImpactGameMode>() : nullptr;
	if (!Mode)
	{
		return;
	}
	// The VS stage's own place (no VS match is ever played in the title world), on the players' floor.
	FVector Origin = Mode->GetVersusStageLocation();
	if (const APlayerController* OwningController = Cast<APlayerController>(GetOwner()))
	{
		if (const ACharacter* Player = Cast<ACharacter>(OwningController->GetPawn()))
		{
			Origin.Z = Player->GetActorLocation().Z - Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		}
	}
	UClass* StageClass = Mode->GetVersusStageClass() ? Mode->GetVersusStageClass().Get() : AChaosImpactVersusStage::StaticClass();
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Stage = World->SpawnActor<AChaosImpactStageBase>(StageClass, Origin, FRotator::ZeroRotator, Parameters);
	if (!Stage)
	{
		return;
	}
	Focus = Stage->GetCenter();

	for (const FVector& Point : Stage->GetBallPoints())
	{
		const FTransform Where(FRotator::ZeroRotator, Point + FVector(0.0f, 0.0f, 3.0f));
		if (AChaosImpactBallSpawner* Spawner = World->SpawnActorDeferred<AChaosImpactBallSpawner>(
			AChaosImpactBallSpawner::StaticClass(), Where, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
		{
			Spawner->SetAlwaysActive(true);
			// No smoke: its cloud would grey out the picture.
			if (FFloatProperty* Smoke = FindFProperty<FFloatProperty>(AChaosImpactBallSpawner::StaticClass(), TEXT("SmokeBallChance")))
			{
				Smoke->SetPropertyValue_InContainer(Spawner, 0.0f);
			}
			Spawner->FinishSpawning(Where);
			Spawned.Add(Spawner);
		}
	}

	const TArray<FVector> Starts = Stage->GetSpawnPoints();
	for (int32 Index = 0; Index < CPUCount && !Starts.IsEmpty(); ++Index)
	{
		const FVector Where = Starts[Index * Starts.Num() / CPUCount] + FVector(0.0f, 0.0f, 110.0f);
		const FRotator Facing(0.0f, (Stage->GetCenter() - Where).GetSafeNormal2D().Rotation().Yaw, 0.0f);
		const FTransform Start(Facing, Where);
		AChaosImpactCPUController* CPU = World->SpawnActorDeferred<AChaosImpactCPUController>(
			AChaosImpactCPUController::StaticClass(), Start, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!CPU)
		{
			continue;
		}
		// Not a member of anything: no player state (no names in lists, no scores), every one for itself.
		CPU->bWantsPlayerState = false;
		CPU->MarkAsMatchCPU();
		CPU->FinishSpawning(Start);
		APawn* Pawn = Mode->SpawnDefaultPawnAtTransform(CPU, Start);
		if (!Pawn)
		{
			CPU->Destroy();
			continue;
		}
		CPU->Possess(Pawn);
		CPU->SetDifficulty(ChaosImpactMatch::CPULevelStrong);
		if (AChaosImpactCharacter* Character = Cast<AChaosImpactCharacter>(Pawn))
		{
			Character->SetTrainingStartTransform(Where, Facing);
			Character->SetCPUNumber(Index + 1);
		}
		Spawned.Add(Pawn);
		Spawned.Add(CPU);
	}
	UE_LOG(LogTemp, Log, TEXT("Title demo: %d actors (ball spawners, CPUs) on %s"), Spawned.Num(), *GetNameSafe(Stage));
}

void AChaosImpactTitleDemo::SetFilming(const bool bFilm)
{
	if (bFilm && !bFilming)
	{
		FilmingSince = FPlatformTime::Seconds();
	}
	bFilming = bFilm;
	Camera->bCaptureEveryFrame = bFilm;
}

float AChaosImpactTitleDemo::GetFadeIn() const
{
	return bFilming && Stage ? FMath::Clamp(static_cast<float>(FPlatformTime::Seconds() - FilmingSince) - 0.2f, 0.0f, 1.0f) : 0.0f;
}

FVector AChaosImpactTitleDemo::GetActionFocus() const
{
	if (!Stage)
	{
		return Focus;
	}
	FVector Sum = FVector::ZeroVector;
	int32 Count = 0;
	for (const AActor* Actor : Spawned)
	{
		const AChaosImpactCharacter* Character = Cast<AChaosImpactCharacter>(Actor);
		if (IsValid(Character) && !Character->IsEliminated())
		{
			Sum += Character->GetActorLocation();
			++Count;
		}
	}
	const FVector Center = Stage->GetCenter();
	if (Count == 0)
	{
		return Center;
	}
	// Halfway to the players: the camera follows the action without losing the stage.
	FVector Offset = (Sum / Count - Center) * 0.5f;
	Offset.Z = 0.0f;
	return Center + Offset.GetClampedToMaxSize(Stage->GetHalfExtent() * 0.4f);
}

void AChaosImpactTitleDemo::PlaceCamera(const float DeltaSeconds)
{
	Focus = FMath::VInterpTo(Focus, GetActionFocus(), DeltaSeconds, 0.8f);
	OrbitDegrees = FMath::Fmod(OrbitDegrees + OrbitSpeed * DeltaSeconds, 360.0f);
	const FVector Back = FRotator(0.0f, OrbitDegrees, 0.0f).Vector() * -CameraBack;
	const FVector Eye = Focus + Back + FVector(0.0f, 0.0f, CameraUp);
	SetActorLocationAndRotation(Eye, (Focus - Back.GetSafeNormal() * AimPast - Eye).Rotation());
}

void AChaosImpactTitleDemo::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bFilming)
	{
		PlaceCamera(DeltaSeconds);
	}
}
