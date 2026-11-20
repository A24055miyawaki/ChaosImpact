#pragma once

#include "CoreMinimal.h"
#include "ChaosImpactStageBase.h"
#include "ChaosImpactDuelStage.generated.h"

class UMaterialInterface;
class UMaterialInstanceDynamic;
class UDirectionalLightComponent;
class USkyLightComponent;
class UExponentialHeightFogComponent;
class UPointLightComponent;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * ランクマッチ's stage (カオスアリーナ): a small square arena for one against one, mirrored left to right so neither
 * side has anything the other lacks. Each player starts at the back of their own half (red on -X, blue on +X) behind
 * a low jumpable wall with a ball pad just ahead; the halves meet at a glowing centre line with two more pads on it,
 * four pillars around the middle and angled bank walls in the corners give cover without anywhere to hide for long.
 * Small on purpose: nobody can run away from a duel for long.
 *
 * Editing: like the other stages, every block is an editable component (bEditableWhenInherited) and the points are
 * draggable handles; turn off bApplyDefaultColors to keep materials set in the editor.
 */
UCLASS(Blueprintable, meta=(DisplayName="Chaos Impact Duel Stage"))
class AChaosImpactDuelStage : public AChaosImpactStageBase
{
	GENERATED_BODY()

public:
	AChaosImpactDuelStage();
	virtual void OnConstruction(const FTransform& Transform) override;
	/** Which of the 1v1 stages this is (0 カオスアリーナ .. ChaosImpactMatch::DuelStageCount - 1): each its own class. */
	virtual int32 GetDuelStageIndex() const { return 0; }
	/** The class of each 1v1 stage (GetDuelStageIndex order; Index clamped). */
	static UClass* GetDuelStageClass(int32 Index);
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	static constexpr float DuelHalfExtent = 1850.0f;
	virtual float GetHalfExtent() const override { return DuelHalfExtent; }
	/** The first two are the duel's starts: side 0 (red, -X) then side 1 (blue, +X). The rest are spares. */
	virtual TArray<FVector> GetSpawnPoints() const override { return ToWorld(SpawnPointOffsets); }
	virtual TArray<FVector> GetBallPoints() const override { return ToWorld(BallPointOffsets); }

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Duel Stage", meta=(MakeEditWidget))
	TArray<FVector> SpawnPointOffsets;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Duel Stage", meta=(MakeEditWidget))
	TArray<FVector> BallPointOffsets;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Duel Stage")
	bool bApplyDefaultColors = true;

	/** How bright the neon trims and the centre line glow. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Duel Stage", meta=(ClampMin="0"))
	float GlowIntensity = 3.0f;

protected:
	virtual void BeginPlay() override;

	/** What a stage's own constructor builds: カオスアリーナ's arena, or nothing (another 1v1 stage builds its own). */
	enum class EBuild : uint8 { ChaosArena, Empty };
	explicit AChaosImpactDuelStage(EBuild Build);

	/** How the stage lights itself while it is played on. The sun is always almost overhead (both ends see the same light,
	 *  whichever way the camera faces); its colour and strength, the sky light's and the fog's colour are the stage's own. */
	struct FDuelLook
	{
		FLinearColor SunColor = FLinearColor::White;
		float SunScale = 1.0f;
		float SkyLightScale = 1.0f;
		/** A = 0: the level's own fog colour is left alone. */
		FLinearColor FogColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
		/** Where the fog begins (< 0: the level's own): far enough that the stage, its town and its sky keep their colours. */
		float FogStart = -1.0f;
	};
	virtual FDuelLook GetArenaLook() const { return FDuelLook(); }
	/** The sky dome round the stage (M_CI_DuelSky): its colours. Brightness 0: no dome. */
	struct FDuelSky
	{
		FLinearColor Zenith = FLinearColor(0.02f, 0.03f, 0.08f);
		FLinearColor Horizon = FLinearColor(0.2f, 0.12f, 0.3f);
		FLinearColor Ground = FLinearColor(0.01f, 0.01f, 0.02f);
		FVector SunDir = FVector(0.0f, 1.0f, 0.05f);
		FLinearColor SunColor = FLinearColor::Black;
		float SunSize = 0.0f;
		float SunGlow = 0.0f;
		float Brightness = 1.0f;
		float Haze = 0.3f;
	};
	virtual FDuelSky GetSky() const { return FDuelSky(); }

	// ---- Pieces for the 1v1 stages (in the constructor). Size: full size about the middle (cubes); a cylinder stands on
	// its base. Tint: a colour for the material's Color / Base Color (A = 0: none); Glow: its Intensity (< 0: none).
	UStaticMeshComponent* AddPiece(USceneComponent* Parent, const FString& Name, UStaticMesh* Mesh, UMaterialInterface* Material,
		const FVector& Location, const FVector& Scale, const FRotator& Rotation = FRotator::ZeroRotator, bool bCollision = false,
		bool bShadow = true);
	UStaticMeshComponent* AddCube(const FString& Name, UMaterialInterface* Material, const FVector& Location, const FVector& Size,
		float Yaw = 0.0f, bool bCollision = true, bool bChamfer = false);
	UStaticMeshComponent* AddDisc(const FString& Name, UMaterialInterface* Material, const FVector& Base, float Diameter, float Height,
		bool bCollision = true);
	UStaticMeshComponent* AddBand(const FString& Name, UMaterialInterface* Material, const FVector& Location, float Radius, float Thickness);
	/** A see-nothing wall that stops characters only (a gap's edge: balls fly on over it). The CPUs route round it. */
	void AddCharacterFence(const FString& Name, const FVector& From, const FVector& To, float Height = 600.0f);
	/** A see-nothing wall that stops characters and balls (the edge of a stage whose own edge is low). */
	void AddInvisibleWall(const FString& Name, const FVector& From, const FVector& To, float Height = 300.0f);
	void Tint(UStaticMeshComponent* Piece, const FLinearColor& Colour, float Glow = -1.0f);
	/** The sky dome (call once): a sphere of this radius about the stage's middle. */
	void AddSkyDome(float Radius);
	/** Tints, glows and the sky dome's colours onto the pieces (OnConstruction and BeginPlay). */
	void ApplyPieceLooks();

	UPROPERTY()
	TMap<FName, FLinearColor> PieceColours;
	UPROPERTY()
	TMap<FName, float> PieceGlows;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> SphereMesh;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> PlaneMesh;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> ConeMesh;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> SkyMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> SkyDome;

protected:
	// Runtime-only override: the lobby and other stages keep their original sun and shadow settings.
	struct FSunSettings
	{
		TWeakObjectPtr<UDirectionalLightComponent> Light;
		FRotator Rotation;
		float SourceAngle = 0.f;
		uint8 Mobility = 0;
	};
	TArray<FSunSettings> SavedSuns;
	struct FSunLook
	{
		TWeakObjectPtr<UDirectionalLightComponent> Light;
		FLinearColor Colour;
		float Intensity = 0.0f;
	};
	TArray<FSunLook> SavedSunLooks;
	TArray<TPair<TWeakObjectPtr<USkyLightComponent>, float>> SavedSkyLights;
	TArray<TPair<TWeakObjectPtr<UExponentialHeightFogComponent>, FLinearColor>> SavedFogs;
	TArray<TPair<TWeakObjectPtr<UExponentialHeightFogComponent>, float>> SavedFogStarts;
	void BuildChaosArena();
	bool bArenaLightingActive = false;
	void UpdateArenaLighting(bool bActive);

	enum EPaint : uint8
	{
		Floor,
		HalfRed,
		HalfBlue,
		Wall,
		Cover,
		Pillar,
		GlowRed,
		GlowBlue,
		GlowGold,
		GlowWhite,
		PaintCount
	};
	UStaticMeshComponent* AddBlock(const FString& Name, EPaint Paint, const FVector& Location, const FVector& Size,
		float Yaw = 0.0f, bool bCollision = true, UStaticMesh* Mesh = nullptr);
	/** A flat glowing ring lying on the floor (no collision). */
	UStaticMeshComponent* AddRing(const FString& Name, EPaint Paint, const FVector& Location, float Radius, float Thickness);
	/** The same block on both halves: at Location and mirrored to -X (red side first). */
	void AddMirrored(const FString& Name, EPaint RedPaint, EPaint BluePaint, const FVector& BlueLocation, const FVector& Size,
		float BlueYaw = 0.0f, bool bCollision = true, UStaticMesh* Mesh = nullptr);
	void ApplyStageColors();
	static FLinearColor GetPaintColor(EPaint Paint);
	static bool IsGlow(EPaint Paint) { return Paint >= GlowRed; }

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category="Chaos Impact|Duel Stage")
	TArray<TObjectPtr<UStaticMeshComponent>> Blocks;

	UPROPERTY(VisibleAnywhere, Category="Chaos Impact|Duel Stage")
	TArray<TObjectPtr<UPointLightComponent>> SideLights;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> BoxMesh;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> ChamferMesh;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> CylinderMesh;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> RingMesh;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> FlatMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> GlowMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> FloorMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> WallMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> PaintMaterial;
};
