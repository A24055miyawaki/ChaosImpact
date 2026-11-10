#include "BRGame.h"
#include "BRHUD.h"
#include "BRIsland.h"
#include "ChaosImpact.h"
#include "ChaosImpactCharacterRoster.h"
#include "ChaosImpactGameState.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/InputDeviceProperties.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UObject/ConstructorHelpers.h"

ABRZoneWall::ABRZoneWall()
{
	PrimaryActorTick.bCanEverTick = true;
	Wall = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Wall"));
	SetRootComponent(Wall);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Cylinder.Succeeded()) { Wall->SetStaticMesh(Cylinder.Object); }
	Wall->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Wall->SetCastShadow(false);
}

void ABRZoneWall::BeginPlay()
{
	Super::BeginPlay();
	if (UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/ChaosImpact/BattleRoyale/Materials/M_BR_ZoneWall.M_BR_ZoneWall")))
	{
		Wall->SetMaterial(0, Material);
	}
	Wall->SetVisibility(false);
}

void ABRZoneWall::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const ABRGameState* State = GetWorld()->GetGameState<ABRGameState>();
	const bool bShow = State && (State->Phase == EBRPhase::Play || State->Phase == EBRPhase::Over);
	Wall->SetVisibility(bShow);
	if (!bShow) { return; }
	FVector2D Centre;
	float Radius;
	State->GetZoneNow(Centre, Radius);
	SetActorLocation(FVector(Centre.X, Centre.Y, 0.0f));
	const float Scale = FMath::Max(Radius, 10.0f) / 50.0f;
	Wall->SetWorldScale3D(FVector(Scale, Scale, 120.0f));
}

ABRDropship::ABRDropship()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

void ABRDropship::Part(const TCHAR* Shape, const FVector& At, const FVector& Size, const FRotator& Rotation, const FLinearColor& Colour, const bool bGlow)
{
	UStaticMeshComponent* Piece = NewObject<UStaticMeshComponent>(this);
	Piece->SetupAttachment(Root);
	Piece->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), Shape, Shape)));
	Piece->SetRelativeLocationAndRotation(At, Rotation);
	Piece->SetRelativeScale3D(Size / 100.0f);
	Piece->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Piece->SetCastShadow(false);
	Piece->RegisterComponent();
	UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, bGlow ? TEXT("/Game/ChaosImpact/BattleRoyale/Materials/M_BR_LootGlow.M_BR_LootGlow") : TEXT("/Game/ChaosImpact/BattleRoyale/Materials/M_BR_VehiclePaint.M_BR_VehiclePaint"));
	if (UMaterialInstanceDynamic* Dynamic = Piece->CreateDynamicMaterialInstance(0, Material)) { Dynamic->SetVectorParameterValue(TEXT("Colour"), Colour); }
}

void ABRDropship::BeginPlay()
{
	Super::BeginPlay();
	// The president's airship: a giant ball slung with a gondola, fins and propellers.
	const FLinearColor Red(0.72f, 0.05f, 0.06f), Cream(0.9f, 0.82f, 0.62f), Dark(0.03f, 0.03f, 0.04f);
	Part(TEXT("Sphere"), FVector::ZeroVector, FVector(3600.0f, 2000.0f, 2000.0f), FRotator::ZeroRotator, Red, false);
	Part(TEXT("Sphere"), FVector::ZeroVector, FVector(3640.0f, 2030.0f, 110.0f), FRotator::ZeroRotator, Cream, false);
	Part(TEXT("Sphere"), FVector::ZeroVector, FVector(120.0f, 2030.0f, 2030.0f), FRotator::ZeroRotator, Cream, false);
	Part(TEXT("Sphere"), FVector(900.0f, 0.0f, 0.0f), FVector(120.0f, 1720.0f, 1720.0f), FRotator::ZeroRotator, Cream, false);
	Part(TEXT("Sphere"), FVector(-900.0f, 0.0f, 0.0f), FVector(120.0f, 1720.0f, 1720.0f), FRotator::ZeroRotator, Cream, false);
	Part(TEXT("Cube"), FVector(100.0f, 0.0f, -1120.0f), FVector(1400.0f, 460.0f, 320.0f), FRotator::ZeroRotator, Dark, false);
	for (const float X : {-400.0f, 100.0f, 600.0f}) { for (const float Y : {-234.0f, 234.0f}) { Part(TEXT("Cube"), FVector(X, Y, -1100.0f), FVector(300.0f, 6.0f, 110.0f), FRotator::ZeroRotator, FLinearColor(1.0f, 0.8f, 0.4f) * 3.0f, true); } }
	for (const float Sign : {-1.0f, 1.0f})
	{
		Part(TEXT("Cube"), FVector(-1500.0f, Sign * 900.0f, 0.0f), FVector(900.0f, 800.0f, 40.0f), FRotator::ZeroRotator, Dark, false);
		Part(TEXT("Cylinder"), FVector(-300.0f, Sign * 1320.0f, -240.0f), FVector(260.0f, 260.0f, 620.0f), FRotator(90.0f, 0.0f, 0.0f), Dark, false);
	}
	Part(TEXT("Cube"), FVector(-1500.0f, 0.0f, 620.0f), FVector(900.0f, 40.0f, 900.0f), FRotator::ZeroRotator, Dark, false);
	SetActorHiddenInGame(true);
}

void ABRDropship::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const ABRGameState* State = GetWorld()->GetGameState<ABRGameState>();
	const bool bShow = State && State->Phase == EBRPhase::Ship;
	SetActorHiddenInGame(!bShow);
	if (!bShow) { return; }
	Clock += DeltaSeconds;
	const FVector2D Way = State->ShipTo - State->ShipFrom;
	SetActorLocationAndRotation(State->GetShipLocation() + FVector(0.0f, 0.0f, FMath::Sin(Clock * 0.7f) * 60.0f), FRotator(0.0f, FMath::RadiansToDegrees(FMath::Atan2(Way.Y, Way.X)), FMath::Sin(Clock * 0.5f) * 1.5f));
}

ABRPlayerController::ABRPlayerController()
{
	PrimaryActorTick.bCanEverTick = true;
}

int32 ABRPlayerController::GetMyTeam() const
{
	if (const ABRCharacter* Me = Cast<ABRCharacter>(GetPawn())) { return Me->TeamId; }
	return 0;
}

void ABRPlayerController::EnsureHud()
{
	// (Someone joining this screen gets a controller before it knows whose screen it is: the HUD waits for that.)
	if (HUD || !GetLocalPlayer()) { return; }
	HUD = CreateWidget<UBRHUD>(this, UBRHUD::StaticClass());
	if (HUD)
	{
		HUD->Init(this);
		HUD->AddToPlayerScreen(50);
	}
}

void ABRPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController()) { return; }
	EnsureHud();
	if (IsPrimaryLocalPlayerController())
	{
		GetWorld()->SpawnActor<ABRZoneWall>(ABRZoneWall::StaticClass());
		GetWorld()->SpawnActor<ABRDropship>(ABRDropship::StaticClass());
	}
	const ABRGameState* State = GetWorld()->GetGameState<ABRGameState>();
	if (!State || State->Phase == EBRPhase::Title)
	{
		if (!FParse::Param(FCommandLine::Get(), TEXT("CIBRTour")) && !FParse::Param(FCommandLine::Get(), TEXT("CIBROverview")) && !FCString::Strifind(FCommandLine::Get(), TEXT("CIBRStop=")) && !FCString::Strifind(FCommandLine::Get(), TEXT("CIBRAuto=")))
		{
			ShowTitle();
		}
	}
}

void ABRPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HUD) { HUD->RemoveFromParent(); }
	if (Title) { Title->RemoveFromParent(); }
	Super::EndPlay(EndPlayReason);
}

void ABRPlayerController::ShowTitle()
{
	if (!IsPrimaryLocalPlayerController() || Title) { return; }
	Title = CreateWidget<UBRTitleWidget>(this, UBRTitleWidget::StaticClass());
	if (!Title) { return; }
	Title->Init(this);
	Title->AddToViewport(100);
	FInputModeUIOnly Mode;
	Mode.SetWidgetToFocus(Title->TakeWidget());
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(Mode);
	// (A pad can belong to any of Slate's users; the title listens to them all.)
	if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().SetAllUserFocus(Title->TakeWidget(), EFocusCause::SetDirectly); }
	SetShowMouseCursor(true);
	TitleCamera = GetWorld()->SpawnActor<ACameraActor>(FVector(-4000.0f, 0.0f, 2600.0f), FRotator(-24.0f, 0.0f, 0.0f));
	if (TitleCamera)
	{
		TitleCamera->GetCameraComponent()->bConstrainAspectRatio = false;
		TitleCamera->GetCameraComponent()->SetFieldOfView(62.0f);
		SetViewTarget(TitleCamera);
	}
}

void ABRPlayerController::CloseTitle()
{
	if (Title) { Title->RemoveFromParent(); Title = nullptr; }
	if (TitleCamera) { TitleCamera->Destroy(); TitleCamera = nullptr; }
	if (GetPawn()) { SetViewTarget(GetPawn()); }
	FInputModeGameAndUI Mode;
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	Mode.SetHideCursorDuringCapture(false);
	SetInputMode(Mode);
}

void ABRPlayerController::UpdateTitleCamera(const float DeltaSeconds)
{
	if (!TitleCamera) { return; }
	TitleClock += DeltaSeconds;
	// A slow circle round the statue in the plaza.
	const float Angle = TitleClock * 0.09f;
	const FVector Target(0.0f, 0.0f, 1100.0f);
	const FVector At = Target + FVector(FMath::Cos(Angle) * 5200.0f, FMath::Sin(Angle) * 5200.0f, 1500.0f + FMath::Sin(TitleClock * 0.3f) * 240.0f);
	TitleCamera->SetActorLocationAndRotation(At, (Target - At).Rotation());
	SetViewTarget(TitleCamera);
}

void ABRPlayerController::ServerStartMatch_Implementation(const int32 TeamSize, const int32 CharacterIndex)
{
	if (!IsLocalController()) { return; }
	if (AChaosImpactPlayerState* Loadout = GetPlayerState<AChaosImpactPlayerState>())
	{
		Loadout->CharacterIndex = FMath::Clamp(CharacterIndex, 0, ChaosImpactRoster::Num() - 1);
		Loadout->ForceNetUpdate();
	}
	if (ABRGameMode* Mode = GetWorld()->GetAuthGameMode<ABRGameMode>()) { Mode->StartMatch(TeamSize); }
}

void ABRPlayerController::ServerSetCharacter_Implementation(const int32 CharacterIndex)
{
	if (AChaosImpactPlayerState* Loadout = GetPlayerState<AChaosImpactPlayerState>())
	{
		Loadout->CharacterIndex = FMath::Clamp(CharacterIndex, 0, ChaosImpactRoster::Num() - 1);
		Loadout->ForceNetUpdate();
	}
}

void ABRPlayerController::ServerSetTeamSize_Implementation(const int32 TeamSize)
{
	if (ABRGameMode* Mode = GetWorld()->GetAuthGameMode<ABRGameMode>(); Mode && IsLocalController()) { Mode->SetTeamSize(TeamSize); }
}

void ABRPlayerController::ServerChooseLanding_Implementation(const FVector2D Where)
{
	if (ABRGameMode* Mode = GetWorld()->GetAuthGameMode<ABRGameMode>()) { Mode->SetLanding(this, Where); }
}

void ABRPlayerController::ChooseLanding(const FVector2D& Where)
{
	ChosenLanding = Where;
	bChosenLanding = true;
	ServerChooseLanding(Where);
}

void ABRPlayerController::ServerAgain_Implementation()
{
	if (!IsLocalController()) { return; }
	if (ABRGameMode* Mode = GetWorld()->GetAuthGameMode<ABRGameMode>()) { Mode->ResetToTitle(); }
}

void ABRPlayerController::ReturnToBRTitle()
{
	ServerAgain();
}

void ABRPlayerController::LeaveToMainTitle()
{
	UGameplayStatics::OpenLevel(this, FName(TEXT("/Game/ThirdPerson/Lvl_ThirdPerson")), true);
}

void ABRPlayerController::CycleSpectated(const int32 Step)
{
	const ABRGameState* State = GetWorld()->GetGameState<ABRGameState>();
	if (!State || State->Roster.IsEmpty()) { return; }
	TArray<ABRCharacter*> Living;
	for (TActorIterator<ABRCharacter> It(GetWorld()); It; ++It)
	{
		if (!It->IsEliminated()) { Living.Add(*It); }
	}
	if (Living.IsEmpty()) { return; }
	// The team first, then whoever else is about.
	const int32 Mine = GetMyTeam();
	Living.Sort([Mine](const ABRCharacter& A, const ABRCharacter& B) { return (A.TeamId == Mine) > (B.TeamId == Mine); });
	int32 Index = Living.IndexOfByPredicate([this](const ABRCharacter* C) { return C->Fighter == Spectated; });
	Index = Index == INDEX_NONE ? 0 : (Index + Step + Living.Num()) % Living.Num();
	Spectated = Living[Index]->Fighter;
	SetViewTarget(Living[Index]);
}

void ABRPlayerController::UpdateSpectating()
{
	const ABRCharacter* Me = Cast<ABRCharacter>(GetPawn());
	if (!Me || !Me->IsEliminated()) { return; }
	AActor* Viewing = GetViewTarget();
	const ABRCharacter* Watched = Cast<ABRCharacter>(Viewing);
	if (!Watched || Watched == Me || Watched->IsEliminated()) { CycleSpectated(1); }
}

void ABRPlayerController::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!IsLocalController()) { return; }
	EnsureHud();
	if (IsPrimaryLocalPlayerController())
	{
		// The title fills the window; the panes come when the match does.
		if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
		{
			const bool bWhole = Title != nullptr;
			if (Viewport->IsSplitscreenForceDisabled() != bWhole) { Viewport->SetForceDisableSplitscreen(bWhole); }
		}
	}
	if (Title)
	{
		UpdateTitleCamera(DeltaSeconds);
	}
	const ABRGameState* State = GetWorld()->GetGameState<ABRGameState>();
	if (!State) { return; }
	if (State->Phase != LastPhase)
	{
		LastPhase = State->Phase;
		if (State->Phase != EBRPhase::Title && Title) { CloseTitle(); }
		bMapOpen = State->Phase != EBRPhase::Over && FParse::Param(FCommandLine::Get(), TEXT("CIBRMapOpen"));
	}
	if (State->Phase == EBRPhase::Play) { UpdateSpectating(); }
}

bool ABRPlayerController::InputKey(const FInputKeyEventArgs& Params)
{
	const FKey Key = Params.Key;
	const ABRGameState* State = GetWorld()->GetGameState<ABRGameState>();
	if (State && HUD && State->Phase != EBRPhase::Title && !Title)
	{
		const bool bPressed = Params.Event == IE_Pressed;
		if (bPressed && Key == EKeys::LeftMouseButton && (State->Phase == EBRPhase::Over || bMapOpen))
		{
			if (FSlateApplication::IsInitialized() && HUD->HandleClick(FSlateApplication::Get().GetCursorPos())) { return true; }
		}
		if (bPressed && (Key == EKeys::Gamepad_FaceButton_Bottom) && State->Phase == EBRPhase::Over)
		{
			HUD->HandlePadConfirm();
			return true;
		}
		if (Key == EKeys::MouseWheelAxis && bMapOpen && HUD->HandleWheel(Params.AmountDepressed)) { return true; }
		if (bPressed && (Key == EKeys::M || Key == EKeys::Gamepad_Special_Left) && (State->Phase == EBRPhase::Play || State->Phase == EBRPhase::Ship)) { ToggleMap(); return true; }
		if (bPressed && (State->Phase == EBRPhase::Play || State->Phase == EBRPhase::Ship))
		{
			ABRCharacter* Me = Cast<ABRCharacter>(GetPawn());
			if (Me && Me->IsAboard())
			{
				if (Key == EKeys::SpaceBar || Key == EKeys::Gamepad_FaceButton_Bottom) { Me->DoJumpOff(); return true; }
				return true;
			}
			if (Me && !Me->IsEliminated())
			{
				if (Key == EKeys::G || Key == EKeys::Gamepad_FaceButton_Top) { Me->DoInteract(); return true; }
				if (Key == EKeys::H || Key == EKeys::Gamepad_DPad_Up) { Me->DoUseHeal(); return true; }
			}
			else if (Me && (Key == EKeys::LeftMouseButton || Key == EKeys::Gamepad_FaceButton_Bottom))
			{
				CycleSpectated(1);
				return true;
			}
		}
	}
	return Super::InputKey(Params);
}
