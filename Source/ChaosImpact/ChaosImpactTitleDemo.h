#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ChaosImpactTitleDemo.generated.h"

class AChaosImpactStageBase;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;

/**
 * The title screen's demo: CPUs playing a free-for-all on a VS stage far outside the level, filmed from above into a
 * texture the title draws in its dark middle, without colour and dimmed into the backdrop's navy. Only the title world
 * (no options) has one; other menus pause the world, so the match holds still while they show.
 */
UCLASS(NotPlaceable, Transient)
class AChaosImpactTitleDemo : public AActor
{
	GENERATED_BODY()

public:
	AChaosImpactTitleDemo();
	virtual void Tick(float DeltaSeconds) override;

	static constexpr int32 PictureWidth = 1280;
	static constexpr int32 PictureHeight = 766;
	static constexpr int32 CPUCount = 4;

	/** Films only while the title shows. */
	void SetFilming(bool bFilm);
	UTextureRenderTarget2D* GetPicture() const { return Picture; }
	/** 0 until filming has run a moment, then up to 1 over a second: the title fades the picture in. */
	float GetFadeIn() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void SetUpMatch();
	/** Where the camera looks: the middle of the CPUs still playing, kept well inside the stage. */
	FVector GetActionFocus() const;
	void PlaceCamera(float DeltaSeconds);

	UPROPERTY()
	TObjectPtr<USceneCaptureComponent2D> Camera;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> Picture;

	UPROPERTY(Transient)
	TObjectPtr<AChaosImpactStageBase> Stage;

	/** Everything the demo spawned (ball spawners, CPUs and their controllers), removed with it. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> Spawned;

	FVector Focus = FVector::ZeroVector;
	float OrbitDegrees = 0.0f;
	double FilmingSince = 0.0;
	bool bFilming = false;
};
