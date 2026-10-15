#include "ChaosImpactSoloTitle.h"

#include "ChaosImpactCharacter.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactScreen.h"
#include "ChaosImpactSfx.h"
#include "ChaosImpactSoloStages.h"
#include "ChaosImpactSoloTitleWidget.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"

const FName AChaosImpactSoloTitle::CameraTag(TEXT("SoloTitleCamera"));

AChaosImpactSoloTitle::AChaosImpactSoloTitle()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

AChaosImpactSoloTitle* AChaosImpactSoloTitle::Find(const UObject* WorldContext)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AChaosImpactSoloTitle> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

void AChaosImpactSoloTitle::BeginPlay()
{
	Super::BeginPlay();
	// The level's camera (made with the level, tagged); a view of the temple if there is none.
	for (TActorIterator<ACameraActor> It(GetWorld()); It; ++It)
	{
		if (It->ActorHasTag(CameraTag))
		{
			Camera = *It;
			break;
		}
	}
	if (!Camera)
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		const FVector From(-1200.0f, -2600.0f, 450.0f);
		Camera = GetWorld()->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), From, (FVector(3000.0f, 0.0f, 700.0f) - From).Rotation(), Params);
		if (Camera)
		{
			Camera->GetCameraComponent()->SetFieldOfView(55.0f);
		}
	}
	if (Camera)
	{
		Camera->GetCameraComponent()->bConstrainAspectRatio = false;
		CameraHome = Camera->GetActorTransform();
	}
	ReadFiles();
	// Starts on the file last played.
	Selected = FMath::Clamp(UChaosImpactSoloProgress::GetActiveSlot() - 1, 0, 2);
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		Widget = CreateWidget<UChaosImpactSoloTitleWidget>(PC, UChaosImpactSoloTitleWidget::StaticClass());
		if (Widget)
		{
			Widget->SetTitle(this);
			Widget->AddToViewport(20);
		}
		PC->SetShowMouseCursor(true);
	}
}

void AChaosImpactSoloTitle::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Widget)
	{
		Widget->RemoveFromParent();
		Widget = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void AChaosImpactSoloTitle::ReadFiles()
{
	for (int32 I = 0; I < 3; ++I)
	{
		FChaosImpactSoloFileView& File = Files[I];
		File = FChaosImpactSoloFileView();
		File.Total = ChaosImpactSolo::StageCount;
		File.bExists = UChaosImpactSoloProgress::DoesSlotExist(I + 1);
		if (!File.bExists)
		{
			continue;
		}
		const UChaosImpactSoloProgress* Progress = UChaosImpactSoloProgress::LoadSlot(I + 1);
		File.Cleared = Progress->CountCleared(1) + Progress->CountCleared(2);
		File.bAllClear = File.Cleared >= File.Total;
		File.LastPlayed = Progress->LastPlayed;
		if (const FChaosImpactSoloStageInfo* Info = ChaosImpactSolo::FindStage(Progress->LastNode))
		{
			File.Map = Info->Map;
			File.Area = Info->Area;
			// A stage not made yet has no name to show.
			File.Place = Info->Kind == EChaosImpactSoloStageKind::Gate || !ChaosImpactSolo::GetLevelPackage(Info->Number).IsEmpty()
				? FString(Info->Name) : FString::Printf(TEXT("ステージ %d"), Info->Number);
		}
	}
}

void AChaosImpactSoloTitle::SetStep(const EChaosImpactSoloTitleStep NewStep)
{
	Step = NewStep;
	StepAge = 0.0f;
}

void AChaosImpactSoloTitle::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	StepAge += DeltaSeconds;
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC)
	{
		return;
	}
	// The character the level made stays out of sight, still, without its battle HUD.
	if (AChaosImpactCharacter* Player = Cast<AChaosImpactCharacter>(PC->GetPawn()); Player && !bPawnHidden)
	{
		Player->SetActorHiddenInGame(true);
		Player->SetActorEnableCollision(false);
		Player->SetMapWalker(true);
		Player->SetTrainingMenuFrozen(true);
		bPawnHidden = true;
	}
	if (Camera && PC->GetViewTarget() != Camera)
	{
		PC->SetViewTarget(Camera);
	}
	if (!PC->bShowMouseCursor && Step != EChaosImpactSoloTitleStep::Leaving)
	{
		PC->SetShowMouseCursor(true);
	}
	UpdateCamera(DeltaSeconds);
	if (Step == EChaosImpactSoloTitleStep::Leaving)
	{
		if (StepAge > 0.9f && LeavingTo != 0)
		{
			const int32 To = LeavingTo;
			LeavingTo = 0;
			if (AChaosImpactPlayerController* Menu = Cast<AChaosImpactPlayerController>(PC))
			{
				if (To > 0)
				{
					Menu->StartSoloFile(To);
				}
				else
				{
					Menu->ShowMenuScreen(EChaosImpactScreen::Title);
				}
			}
		}
		return;
	}
	if (Age > 0.8f)
	{
		UpdateInput(PC, DeltaSeconds);
	}
}

void AChaosImpactSoloTitle::UpdateCamera(const float DeltaSeconds)
{
	if (!Camera)
	{
		return;
	}
	// Drifting in from further back as the title opens, then breathing slowly; up toward the light when leaving.
	const FVector Forward = CameraHome.GetRotation().GetForwardVector();
	const FVector Right = CameraHome.GetRotation().GetRightVector();
	const float Arrive = 1.0f - FMath::InterpEaseOut(0.0f, 1.0f, FMath::Clamp(Age / 4.0f, 0.0f, 1.0f), 3.0f);
	FVector Location = CameraHome.GetLocation() - Forward * (700.0f * Arrive) + FVector(0.0f, 0.0f, 120.0f * Arrive)
		+ Right * (FMath::Sin(Age * 0.13f) * 160.0f) + FVector(0.0f, 0.0f, FMath::Sin(Age * 0.21f) * 40.0f);
	FRotator Rotation = CameraHome.Rotator();
	Rotation.Yaw += FMath::Sin(Age * 0.11f) * 1.6f;
	Rotation.Pitch += FMath::Sin(Age * 0.17f) * 0.6f;
	if (Step == EChaosImpactSoloTitleStep::Leaving)
	{
		const float Up = FMath::InterpEaseIn(0.0f, 1.0f, FMath::Clamp(StepAge / 0.9f, 0.0f, 1.0f), 2.0f);
		Location += Forward * (900.0f * Up);
		Rotation.Pitch += 8.0f * Up;
	}
	Camera->SetActorLocationAndRotation(Location, Rotation);
}

void AChaosImpactSoloTitle::Move(const int32 Direction)
{
	Selected = (Selected + Direction + 3) % 3;
	ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiMove);
}

void AChaosImpactSoloTitle::OpenFiles()
{
	if (Step != EChaosImpactSoloTitleStep::Press)
	{
		return;
	}
	ReadFiles();
	SetStep(EChaosImpactSoloTitleStep::Files);
	ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiStart);
}

void AChaosImpactSoloTitle::StartFile(const int32 Index)
{
	if (Step != EChaosImpactSoloTitleStep::Files || Index < 0 || Index > 2)
	{
		return;
	}
	Selected = Index;
	LeavingTo = Index + 1;
	SetStep(EChaosImpactSoloTitleStep::Leaving);
	ChaosImpactSfx::Play2D(this, EChaosImpactSfx::StageWarp);
}

void AChaosImpactSoloTitle::AskErase(const int32 Index)
{
	if (Step != EChaosImpactSoloTitleStep::Files || Index < 0 || Index > 2)
	{
		return;
	}
	Selected = Index;
	if (!Files[Index].bExists)
	{
		ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiDeny);
		return;
	}
	bEraseYes = false;
	SetStep(EChaosImpactSoloTitleStep::ConfirmErase);
	ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiOpen);
}

void AChaosImpactSoloTitle::AnswerErase(const bool bYes)
{
	if (Step != EChaosImpactSoloTitleStep::ConfirmErase)
	{
		return;
	}
	if (bYes)
	{
		UChaosImpactSoloProgress::DeleteSlot(Selected + 1);
		ReadFiles();
		ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiErase);
	}
	else
	{
		ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiBack);
	}
	SetStep(EChaosImpactSoloTitleStep::Files);
	StepAge = 1.0f;
}

void AChaosImpactSoloTitle::Back()
{
	switch (Step)
	{
	case EChaosImpactSoloTitleStep::ConfirmErase:
		AnswerErase(false);
		break;
	case EChaosImpactSoloTitleStep::Files:
		SetStep(EChaosImpactSoloTitleStep::Press);
		StepAge = 1.0f;
		ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiBack);
		break;
	case EChaosImpactSoloTitleStep::Press:
		LeavingTo = -1;
		SetStep(EChaosImpactSoloTitleStep::Leaving);
		ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiBack);
		break;
	default:
		break;
	}
}

void AChaosImpactSoloTitle::UpdateInput(APlayerController* PC, const float DeltaSeconds)
{
	const auto Pressed = [PC](std::initializer_list<FKey> Keys)
	{
		for (const FKey& Key : Keys)
		{
			if (PC->WasInputKeyJustPressed(Key))
			{
				return true;
			}
		}
		return false;
	};
	const bool bConfirm = Pressed({EKeys::Enter, EKeys::SpaceBar, EKeys::Gamepad_FaceButton_Bottom, EKeys::Gamepad_Special_Right});
	const bool bBack = Pressed({EKeys::Escape, EKeys::BackSpace, EKeys::Gamepad_FaceButton_Right, EKeys::RightMouseButton});
	const bool bErase = Pressed({EKeys::Delete, EKeys::Gamepad_FaceButton_Top});
	const bool bClick = Pressed({EKeys::LeftMouseButton});
	int32 Direction = Pressed({EKeys::Left, EKeys::A, EKeys::Gamepad_DPad_Left}) ? -1 : Pressed({EKeys::Right, EKeys::D, EKeys::Gamepad_DPad_Right}) ? 1 : 0;
	// The stick: one step each time it is pushed over.
	const float Stick = PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftX);
	if (Direction == 0 && FMath::Abs(Stick) > 0.6f && FMath::Abs(StickX) <= 0.6f)
	{
		Direction = Stick > 0.0f ? 1 : -1;
	}
	StickX = Stick;

	// The mouse, in the widget's space.
	const FVector2D Viewport = UWidgetLayoutLibrary::GetViewportSize(this) / FMath::Max(UWidgetLayoutLibrary::GetViewportScale(this), 0.01f);
	const FVector2D Mouse = UWidgetLayoutLibrary::GetMousePositionOnViewport(this);
	const bool bMouseMoved = FVector2D::DistSquared(Mouse, LastMouse) > 4.0;
	LastMouse = Mouse;
	int32 Over = INDEX_NONE;
	for (int32 I = 0; I < 3; ++I)
	{
		if (UChaosImpactSoloTitleWidget::CardRect(Viewport, I).IsInside(Mouse))
		{
			Over = I;
		}
	}
	bEraseHovered = Step == EChaosImpactSoloTitleStep::Files && Files[Selected].bExists
		&& UChaosImpactSoloTitleWidget::EraseRect(Viewport, Selected).IsInside(Mouse);

	switch (Step)
	{
	case EChaosImpactSoloTitleStep::Press:
		if (bConfirm || bClick)
		{
			OpenFiles();
		}
		else if (bBack)
		{
			Back();
		}
		break;
	case EChaosImpactSoloTitleStep::Files:
		if (StepAge < 0.25f)
		{
			break;
		}
		if (bMouseMoved && Over != INDEX_NONE && Over != Selected)
		{
			Selected = Over;
			ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiMove);
		}
		if (Direction != 0)
		{
			Move(Direction);
		}
		else if (bClick && bEraseHovered)
		{
			AskErase(Selected);
		}
		else if (bClick && Over != INDEX_NONE)
		{
			StartFile(Over);
		}
		else if (bConfirm)
		{
			StartFile(Selected);
		}
		else if (bErase)
		{
			AskErase(Selected);
		}
		else if (bBack)
		{
			Back();
		}
		break;
	case EChaosImpactSoloTitleStep::ConfirmErase:
	{
		if (StepAge < 0.2f)
		{
			break;
		}
		if (bMouseMoved)
		{
			if (UChaosImpactSoloTitleWidget::AnswerRect(Viewport, true).IsInside(Mouse))
			{
				bEraseYes = true;
			}
			else if (UChaosImpactSoloTitleWidget::AnswerRect(Viewport, false).IsInside(Mouse))
			{
				bEraseYes = false;
			}
		}
		if (Direction != 0)
		{
			bEraseYes = !bEraseYes;
			ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiMove);
		}
		else if (bClick)
		{
			if (UChaosImpactSoloTitleWidget::AnswerRect(Viewport, true).IsInside(Mouse))
			{
				AnswerErase(true);
			}
			else if (UChaosImpactSoloTitleWidget::AnswerRect(Viewport, false).IsInside(Mouse))
			{
				AnswerErase(false);
			}
		}
		else if (bConfirm)
		{
			AnswerErase(bEraseYes);
		}
		else if (bBack)
		{
			AnswerErase(false);
		}
		break;
	}
	default:
		break;
	}
	(void)DeltaSeconds;
}
