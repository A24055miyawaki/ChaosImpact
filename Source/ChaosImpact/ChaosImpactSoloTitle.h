#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ChaosImpactSoloTitle.generated.h"

class ACameraActor;
class UChaosImpactSoloTitleWidget;

/** What solo mode's title shows: the logo waiting for a press, the three files, a file about to be erased. */
UENUM()
enum class EChaosImpactSoloTitleStep : uint8
{
	Press,
	Files,
	ConfirmErase,
	Leaving
};

/** One file's card on the title. */
struct FChaosImpactSoloFileView
{
	bool bExists = false;
	int32 Cleared = 0;
	int32 Total = 30;
	/** 1 or 2: the map the file is on. */
	int32 Map = 1;
	/** Where the player last stood (its region and name). */
	FString Area;
	FString Place;
	FDateTime LastPlayed;
	bool bAllClear = false;
};

/**
 * Solo mode's own title (its level is /Game/ChaosImpact/solo/Title/Lvl_SoloTitle, opened with ?CISoloTitle=1; the
 * player controller makes one of these there): stage 15's sky temple at dusk behind the logo, the camera drifting
 * slowly; a press opens the three files (each with how far it has come), a file is started or carried on, or erased
 * after asking; back returns to the game's title.
 */
UCLASS(NotPlaceable)
class CHAOSIMPACT_API AChaosImpactSoloTitle : public AActor
{
	GENERATED_BODY()

public:
	AChaosImpactSoloTitle();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	static AChaosImpactSoloTitle* Find(const UObject* WorldContext);
	/** The camera's tag in the title level (its view; the title drifts it). */
	static const FName CameraTag;

	EChaosImpactSoloTitleStep GetStep() const { return Step; }
	/** Seconds since the title opened, and since the step changed. */
	float GetAge() const { return Age; }
	float GetStepAge() const { return StepAge; }
	/** The file picked (0-2). */
	int32 GetSelected() const { return Selected; }
	/** On the erase question: true when "はい" is picked. */
	bool IsEraseYes() const { return bEraseYes; }
	/** The mouse is over the picked file's erase button. */
	bool IsEraseHovered() const { return bEraseHovered; }
	const FChaosImpactSoloFileView& GetFile(int32 Index) const { return Files[FMath::Clamp(Index, 0, 2)]; }

	// ---- Actions (the input calls these; tests too)
	/** From the logo to the files. */
	void OpenFiles();
	/** Starts (or carries on) a file: 0-2. */
	void StartFile(int32 Index);
	/** Asks before erasing a file (0-2) that exists. */
	void AskErase(int32 Index);
	/** Answers the question. */
	void AnswerErase(bool bYes);
	/** Back a step (from the logo: the game's title). */
	void Back();

private:
	void UpdateInput(APlayerController* PC, float DeltaSeconds);
	void UpdateCamera(float DeltaSeconds);
	void ReadFiles();
	void SetStep(EChaosImpactSoloTitleStep NewStep);
	void Move(int32 Direction);

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> Camera;
	UPROPERTY(Transient)
	TObjectPtr<UChaosImpactSoloTitleWidget> Widget;

	FTransform CameraHome;
	EChaosImpactSoloTitleStep Step = EChaosImpactSoloTitleStep::Press;
	float Age = 0.0f;
	float StepAge = 0.0f;
	int32 Selected = 0;
	bool bEraseYes = false;
	bool bEraseHovered = false;
	int32 LeavingTo = 0;
	FChaosImpactSoloFileView Files[3];
	float StickX = 0.0f;
	FVector2D LastMouse = FVector2D(-1.0, -1.0);
	bool bPawnHidden = false;
};
