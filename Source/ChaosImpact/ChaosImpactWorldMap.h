#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ChaosImpactWorldMap.generated.h"

class ACameraActor;
class AChaosImpactCharacter;
class UChaosImpactSoloProgress;
class UChaosImpactWorldMapWidget;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPointLightComponent;
class UProceduralMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UTextRenderComponent;
class UTexture2D;

/**
 * Solo mode's world map: a raised board (a diorama of the world) that the player's own character walks on, along the
 * lines between the stages; standing on a stage's pedestal and pressing the confirm button plays it.
 *
 * At first fog covers the whole board but the first stage. Each stage cleared clears the fog off the stages it opens,
 * with the camera going over to watch (once). Map 1 is the world the United States of Dett Ball has occupied (stages
 * 1-15, the temple's gate at the far end leads on); map 2 is the United States itself, dark all over (16-30).
 *
 * Everything (the board, the regions' miniatures, the paths, the pedestals, the fog) is built by this actor from the
 * layout in its .cpp, so a level only needs this actor (Map Index), its lights and a player start.
 */
UCLASS(meta=(DisplayName="Chaos Impact World Map"))
class CHAOSIMPACT_API AChaosImpactWorldMap : public AActor
{
	GENERATED_BODY()

public:
	AChaosImpactWorldMap();
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	/** 1: 占領された世界 (stages 1-15), 2: デットボール合衆国 (16-30). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|World Map", meta=(ClampMin="1", ClampMax="2"))
	int32 MapIndex = 1;

	/** The world map in the current world, if any. */
	static AChaosImpactWorldMap* Find(const UObject* WorldContext);

	// ---- For the HUD and tests

	/** The node the player stands on (a stage number or a gate), 0 when between nodes. */
	int32 GetFocusedNode() const { return FocusedNode; }
	/** How far the focused node's card has slid in (0-1). */
	float GetCardAlpha() const { return CardAlpha; }
	/** The fog is being cleared off newly opened stages (the player waits). */
	bool IsRevealing() const { return RevealStep != ERevealStep::None; }
	/** The whole map seen from above (M, the mouse wheel or the pad's Y): 0 walking - 1 overview. */
	bool IsOverview() const { return bOverview; }
	float GetOverviewBlend() const { return OverviewBlend; }
	/** Opens or closes the whole-map view. */
	void SetOverview(bool bOn);
	/** Seconds since the reveal began (for its call-out), or -1. */
	float GetRevealSeconds() const;
	/** The node the fog is clearing off now (0 while the camera travels or waits). */
	int32 GetRevealingNode() const { return RevealStep == ERevealStep::Clear && RevealQueue.Num() ? RevealQueue[0] : 0; }
	/** Seconds into the current step of the reveal. */
	float GetRevealStepSeconds() const { return RevealTime; }
	UChaosImpactSoloProgress* GetProgress() const { return Progress; }
	TArray<int32> GetNodeIds() const;
	bool HasNode(int32 Id) const;
	FVector GetNodeLocation(int32 Id) const;
	/** Every stage of this map is cleared: no fog left anywhere on it. */
	bool IsMapComplete() const;
	/** How far the fog has gone off the whole map (0 - 1, 1 once every stage of it is cleared). */
	float GetFullClear() const { return FullClear; }
	/** How clear of fog a node is (0 hidden - 1 revealed). */
	float GetNodeReveal(int32 Id) const;
	/** A point the player may stand on (on a path that is open, or a revealed node's pedestal). */
	bool IsWalkable(const FVector& Location) const;
	/** The nearest point the player may stand on. */
	FVector ClampToPaths(const FVector& Location) const;
	/** Plays the focused stage (or goes through the focused gate). Returns false when it cannot. */
	bool ConfirmFocused();
	/** Development (not in shipping builds): the focused stage counts as cleared (the C key on the map). */
	bool DevClearFocused();
	/** Walks the player along the paths to a node (as a click on it does). */
	void WalkTo(int32 Id);
	/** Straight onto a pedestal (from the whole map's view: a stage already open). */
	void WarpToNode(int32 Id);
	/** Re-reads the progress (a development unlock or reset) and shows it at once. */
	void RefreshFromProgress();

	/** Half the width of a path the player can walk (from its centre line). */
	static constexpr float PathHalfWidth = 95.0f;
	static constexpr float NodeRadius = 230.0f;

	enum class ERevealStep : uint8 { None, Wait, Travel, Clear, Return };

private:
	struct FNode
	{
		int32 Id = 0;
		FVector2D At = FVector2D::ZeroVector;
		/** 0-1, animated while the fog clears off it. */
		float Reveal = 0.0f;
		bool bRevealed = false;
		TObjectPtr<UStaticMeshComponent> Crystal = nullptr;
		TObjectPtr<UProceduralMeshComponent> Top = nullptr;
		TObjectPtr<UProceduralMeshComponent> Ring = nullptr;
		TObjectPtr<UTextRenderComponent> Number = nullptr;
		TObjectPtr<UStaticMeshComponent> FlagCloth = nullptr;
		TObjectPtr<UStaticMeshComponent> FlagBall = nullptr;
		TObjectPtr<UPointLightComponent> Light = nullptr;
		TArray<TObjectPtr<USceneComponent>> Lock;
		/** Scenery belonging to this node: hidden while it is in the fog. */
		TArray<TObjectPtr<USceneComponent>> Parts;
		float Spin = 0.0f;
	};
	struct FEdge
	{
		int32 From = 0;
		int32 To = 0;
		TArray<FVector> Points;
		bool bOpen = false;
		TArray<TObjectPtr<USceneComponent>> Parts;
	};

	// ---- Building
	void Build();
	void ClearBuilt();
	void LoadAssets();
	void MakeLayout();
	void BuildBoard();
	void BuildPaths();
	void BuildNodes();
	void BuildFog();
	void BuildScenery1();
	void BuildScenery2();
	/** Smooth curve from one node to another, bending sideways by Bend (cm) in the middle. */
	TArray<FVector> CurveBetween(const FVector2D& A, const FVector2D& B, float Bend) const;

	UMaterialInstanceDynamic* Mid(UMaterialInterface* Base, const FLinearColor& Color, float Extra = -1.0f);
	UStaticMeshComponent* AddMesh(UStaticMesh* Mesh, const FVector& Location, const FRotator& Rotation, const FVector& Scale,
		UMaterialInterface* Material, int32 Node = 0, bool bCollide = false);
	/** A painted block (the engine cube, a metre each way, about its middle). */
	UStaticMeshComponent* AddBlock(const FVector& Center, const FVector& Size, const FLinearColor& Color, int32 Node = 0, float Yaw = 0.0f);
	UStaticMeshComponent* AddShape(UStaticMesh* Mesh, const FVector& Location, const FVector& Scale, const FLinearColor& Color,
		int32 Node = 0, const FRotator& Rotation = FRotator::ZeroRotator, bool bGlow = false, float GlowIntensity = 4.0f);
	UProceduralMeshComponent* AddProc(const FString& Name, int32 Node, UMaterialInterface* Material, bool bCollide);
	/** A plate: Outline extruded down from Top, its top edge bevelled. */
	UProceduralMeshComponent* AddSlab(const TArray<FVector2D>& Outline, float Top, float Thickness, float Bevel,
		const FLinearColor& TopColor, const FLinearColor& SideColor, int32 Node = 0, bool bCollide = false, UMaterialInterface* Material = nullptr);
	UTextRenderComponent* AddText(const FString& Text, const FVector& Location, const FRotator& Rotation, float Size,
		const FLinearColor& Color, int32 Node = 0);
	UPointLightComponent* AddLight(const FVector& Location, const FLinearColor& Color, float Intensity, float Radius, int32 Node = 0);
	void Own(USceneComponent* Part, int32 Node);
	/** A ring of points (a circle or an ellipse), jagged by Jitter (0-1) like a broken edge. */
	static TArray<FVector2D> Blob(const FVector2D& Center, const FVector2D& Radius, int32 Points, float Jitter, float Seed);
	/** The convex hull of circles round the given points (a region's plate). */
	static TArray<FVector2D> HullAround(const TArray<FVector2D>& Points, float Radius, float Jitter, float Seed);

	// ---- Scenery pieces
	void AddTree(const FVector2D& At, float Height, const FLinearColor& Leaves, int32 Node, float Seed);
	void AddBuilding(const FVector2D& At, const FVector& Size, const FLinearColor& Color, const FLinearColor& Windows, int32 Node, float Yaw = 0.0f);
	void AddFlagPole(const FVector2D& At, const FLinearColor& Color, int32 Node, float Height = 260.0f);
	void AddCloudPuff(const FVector& At, float Size, int32 Node, const FLinearColor& Color);
	void AddPillar(const FVector2D& At, float Height, float Radius, const FLinearColor& Color, int32 Node);
	void AddSnowTree(const FVector2D& At, float Height, int32 Node, float Seed);
	/** A shima-enaga (the ball's bird) sitting on the board, hopping now and then. */
	void AddSimae(const FVector& At, float Yaw, float Scale, int32 Node);

	// ---- Play
	void PlacePlayer();
	AChaosImpactCharacter* GetPlayer() const;
	void UpdateConstraint(AChaosImpactCharacter* Player);
	void UpdateFocus(AChaosImpactCharacter* Player, float DeltaSeconds);
	void UpdateInput(AChaosImpactCharacter* Player);
	void UpdateAutoWalk(AChaosImpactCharacter* Player);
	void UpdateCamera(AChaosImpactCharacter* Player, float DeltaSeconds);
	void UpdateReveal(float DeltaSeconds);
	void UpdateNodeLook(float DeltaSeconds);
	/** Applies the progress to every node and path (Animate: newly opened ones start in the fog). */
	void ApplyProgress(bool bAnimate);
	void UpdateEdgesOpen();
	void PaintFogMask();
	/** The open path network's nearest point to Location (and which edge, and how far along it). */
	FVector NearestOnNetwork(const FVector& Location, int32* OutEdge = nullptr, int32* OutSegment = nullptr) const;
	int32 NodeIndex(int32 Id) const;
	/** A route along open paths from where the player stands to a point on the network. */
	bool PlanRoute(const FVector& From, const FVector& To, TArray<FVector>& OutRoute) const;
	void SetPlayerFrozen(bool bFrozen);

	TArray<FNode> Nodes;
	TArray<FEdge> Edges;
	/** Built parts (cleared before a rebuild). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneComponent>> Built;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> FogMaterials;
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> FogMask;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> FogSheets;
	/** The fog going off the whole map once it is all cleared (0 - 1). */
	float FullClear = 0.0f;
	bool bComplete = false;
	UPROPERTY(Transient)
	TObjectPtr<UChaosImpactSoloProgress> Progress;
	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> MapCamera;
	UPROPERTY(Transient)
	TObjectPtr<UChaosImpactWorldMapWidget> Widget;
	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UMaterialInstanceDynamic>> MidCache;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> SphereMesh;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> CylinderMesh;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> ConeMesh;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> PlaneMesh;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> LitMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> GlowMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> WaterMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> FogMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> CloudSeaMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> TextMaterial;

	FBox2D BoardBounds = FBox2D(ForceInit);
	TArray<uint8> MaskPixels;
	int32 MaskWidth = 0;
	int32 MaskHeight = 0;
	bool bMaskDirty = true;

	int32 FocusedNode = 0;
	int32 CardNode = 0;
	float CardAlpha = 0.0f;
	bool bPlaced = false;
	bool bLeaving = false;
	FVector CameraFocus = FVector::ZeroVector;
	bool bCameraFocusSet = false;

	/** Reveal: nodes that come out of the fog this time, in order. */
	TArray<int32> RevealQueue;
	ERevealStep RevealStep = ERevealStep::None;
	float RevealTime = 0.0f;
	double RevealStartedAt = 0.0;
	FVector RevealTarget = FVector::ZeroVector;

	/** A click's route along the paths. */
	TArray<FVector> AutoRoute;
	int32 AutoIndex = 0;
	int32 AutoGoalNode = 0;
	bool bConfirmHeld = true;
	bool bClickHeld = true;
	bool bOverview = false;
	float OverviewBlend = 0.0f;
	bool bOverviewKeyHeld = true;
	FBox2D NodeBounds = FBox2D(ForceInit);
	/** The birds (their roots), hopping in Tick. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneComponent>> Birds;
	TArray<float> BirdGround;
	double ArrivedAt = 0.0;
};
