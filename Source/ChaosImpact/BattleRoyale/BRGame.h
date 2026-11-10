#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactPlayerController.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "BRGame.generated.h"

class ABRIsland;
class ABRLoot;
class ABRVehicle;
class UBRHUD;
class UBRTitleWidget;
class UStaticMeshComponent;
class ACameraActor;

/**
 * The battle royale itself (removable, see Docs/BattleRoyale/README.md): up to 8 people and CPUs to make 60, last team standing.
 * Most CPUs are numbers in ABRGameMode::Fighters; only those near a person are real characters.
 */
UENUM()
enum class EBRPhase : uint8 { Title, Ship, Play, Over };

UENUM()
enum class EBRLootKind : uint8 { Ball, Heal, Plate, Vest };

namespace BRRules
{
	inline constexpr int32 TotalFighters = 60;
	/** People in one match (the rest are CPUs): the host's machine carries them all. */
	inline constexpr int32 MaxHumans = 8;
	inline constexpr float MaxHealth = 100.0f;
	/** The airship's crossing: everyone aboard jumps somewhere along it. */
	inline constexpr float ShipSeconds = 46.0f;
	inline constexpr float ShipHeight = 30000.0f;
	inline constexpr float ShipReach = 118000.0f;
	/** How near the middle of the island the ship must be for anyone to jump. */
	inline constexpr float ShipDropRadius = 84000.0f;
	/** Ground lower than this is the sea. */
	inline constexpr float ShoreHeight = 30.0f;
	/** Falling: how fast across, and how fast down (faster with the stick held: diving). */
	inline constexpr float FallAcross = 3000.0f;
	inline constexpr float FallDown = 1900.0f;
	inline constexpr float FallDive = 2800.0f;
	inline constexpr float DownedHealth = 60.0f;
	inline constexpr float BleedSeconds = 40.0f;
	inline constexpr float ReviveSeconds = 4.5f;
	inline constexpr float HealAmount = 35.0f;
	inline constexpr float HealSeconds = 2.2f;
	inline constexpr int32 MaxHeals = 5;
	inline constexpr float PlateArmor = 25.0f;
	/** A person's nearby CPUs are real within this (cm), and return to numbers beyond the second. */
	inline constexpr float RealRange = 9000.0f;
	inline constexpr float DataRange = 13500.0f;
	inline constexpr int32 MaxRealBots = 26;

	struct FZoneStage { float Hold; float Shrink; float Radius; float Dps; };
	inline constexpr FZoneStage Stages[] = {{180.0f, 100.0f, 56000.0f, 1.0f}, {110.0f, 80.0f, 34000.0f, 2.0f}, {85.0f, 60.0f, 20000.0f, 4.0f},
		{65.0f, 50.0f, 11000.0f, 7.0f}, {45.0f, 40.0f, 5500.0f, 11.0f}, {30.0f, 30.0f, 2000.0f, 16.0f}, {20.0f, 30.0f, 0.0f, 25.0f}};
	inline constexpr int32 StageCount = UE_ARRAY_COUNT(Stages);
	inline constexpr float StartRadius = 98000.0f;
}

/** One fighter as everybody's map and team list know them. */
USTRUCT()
struct FBRRosterEntry
{
	GENERATED_BODY()
	UPROPERTY() FString Name;
	UPROPERTY() uint8 Team = 0;
	UPROPERTY() uint8 Character = 0;
	UPROPERTY() uint8 Colour = 0;
	UPROPERTY() uint8 Health = 100;
	UPROPERTY() uint8 Armor = 0;
	UPROPERTY() uint8 Kills = 0;
	UPROPERTY() bool bAlive = true;
	UPROPERTY() bool bDowned = false;
	UPROPERTY() bool bHuman = false;
	UPROPERTY() int16 X = 0;
	UPROPERTY() int16 Y = 0;
	FVector2D Where() const { return FVector2D(X * 8.0, Y * 8.0); }
};

UCLASS()
class ABRGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(Replicated) EBRPhase Phase = EBRPhase::Title;
	UPROPERTY(Replicated) int32 TeamSize = 1;
	UPROPERTY(Replicated) double PhaseEndsAt = 0.0;
	UPROPERTY(Replicated) int32 StageIndex = 0;
	UPROPERTY(Replicated) bool bShrinking = false;
	UPROPERTY(Replicated) double StageEndsAt = 0.0;
	UPROPERTY(Replicated) double ShrinkStartedAt = 0.0;
	UPROPERTY(Replicated) FVector2D ZoneFrom = FVector2D::ZeroVector;
	UPROPERTY(Replicated) float ZoneFromRadius = BRRules::StartRadius;
	UPROPERTY(Replicated) FVector2D ZoneTo = FVector2D::ZeroVector;
	UPROPERTY(Replicated) float ZoneToRadius = BRRules::StartRadius;
	UPROPERTY(Replicated) int32 AliveFighters = 0;
	UPROPERTY(Replicated) int32 AliveTeams = 0;
	UPROPERTY(Replicated) int32 WinnerTeam = -1;
	UPROPERTY(Replicated) FVector2D ShipFrom = FVector2D::ZeroVector;
	UPROPERTY(Replicated) FVector2D ShipTo = FVector2D::ZeroVector;
	UPROPERTY(Replicated) double ShipStart = 0.0;
	UPROPERTY(Replicated) float ShipSeconds = 0.0f;
	UPROPERTY(Replicated) TArray<FBRRosterEntry> Roster;

	UFUNCTION(NetMulticast, Reliable) void MulticastKill(const FString& Killer, const FString& Victim, int32 KillerTeam, int32 VictimTeam);

	/** The safe circle as it is now. */
	void GetZoneNow(FVector2D& OutCentre, float& OutRadius) const;
	float GetStageSecondsLeft() const;
	/** Where the airship is, 0 at the start of its crossing and 1 at the end. */
	float GetShipProgress() const;
	FVector GetShipLocation() const;
	bool CanDrop() const;
	bool IsTeammate(int32 RosterIndex, int32 MyTeam) const { return Roster.IsValidIndex(RosterIndex) && Roster[RosterIndex].Team == MyTeam; }

	struct FKillLine { FString Killer; FString Victim; int32 KillerTeam; int32 VictimTeam; double At; };
	TArray<FKillLine> KillLines;
};

/** Everyone on one island: a person, a real CPU or a number. */
struct FBRFighter
{
	FString Name;
	int32 Team = 0;
	int32 Character = 0;
	int32 Colour = 0;
	bool bHuman = false;
	bool bAlive = true;
	float Health = BRRules::MaxHealth;
	float Armor = 0.0f;
	/** A number CPU's kit: how much better it has armed itself (0 to 1). */
	float Gear = 0.0f;
	float Skill = 0.5f;
	int32 Kills = 0;
	int32 Place = 0;
	FVector2D Pos = FVector2D::ZeroVector;
	FVector2D Goal = FVector2D::ZeroVector;
	double NextDecide = 0.0;
	TWeakObjectPtr<class ABRCharacter> Real;
	TWeakObjectPtr<APlayerController> Person;
};

/** Things lying about the island, made real only when someone comes near. */
struct FBRLootSpot
{
	FVector At = FVector::ZeroVector;
	EBRLootKind Kind = EBRLootKind::Ball;
	EChaosImpactBallType BallType = EChaosImpactBallType::Normal;
	bool bTaken = false;
	TWeakObjectPtr<AActor> Spawned;
};

UCLASS()
class ABRBall : public AChaosImpactBall
{
	GENERATED_BODY()
public:
	ABRBall();
protected:
	virtual void BeginPlay() override;
};

UCLASS()
class ABRCharacter : public AChaosImpactCharacter
{
	GENERATED_BODY()

public:
	ABRCharacter();
	virtual void Tick(float DeltaSeconds) override;
	virtual float TakeDamage(float Amount, const FDamageEvent& Event, AController* EventInstigator, AActor* Causer) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual bool IsMatchInputLocked() const override;
	virtual bool IsMoveInputIgnored() const override;
	virtual void FellOutOfWorld(const UDamageType& DamageType) override;

	void InitFighter(int32 InFighter, int32 InTeam, const FString& InName);
	void BoardShip();
	/** Server: off the airship, into the fall. */
	void JumpOff();
	void DoJumpOff();
	UFUNCTION(Server, Reliable) void ServerJumpOff();
	float GetHealthNow() const { return Health; }
	APawn* GetLastHitSourcePawnForBR() const { return LastHitSourcePawn.Get(); }
	float GetBRMaxHealth() const { return MaxHealth; }
	/** Aboard the airship or still in the air: not yet in the fight. */
	bool IsFlying() const { return bAboard || bFreefall; }
	bool IsAboard() const { return bAboard; }
	bool IsFreefalling() const { return bFreefall; }
	bool IsDowned() const { return bDowned; }
	float GetBleedLeft() const;
	void AddHealth(float Amount);
	void SetHealthDirect(float Value);
	/** Server: takes a pickup if there is room for it. */
	bool TakeLoot(EBRLootKind Kind);
	void AddArmor(float Amount);
	void SetArmorMax(float Value) { ArmorMax = Value; Armor = FMath::Min(Armor, ArmorMax); }
	/** Server: gives a ball straight into the hand. */
	void GiveBall(EChaosImpactBallType Type);

	UFUNCTION(Server, Reliable) void ServerInteract();
	UFUNCTION(Server, Reliable) void ServerUseHeal();
	/** The driver's own screen says where the vehicle is. */
	UFUNCTION(Server, Unreliable) void ServerVehicleState(FVector_NetQuantize At, float InYaw, float InPitch, float InSpeed);
	bool WouldTakeLoot(EBRLootKind Kind) const;
	void DoInteract();
	void DoUseHeal();
	/** Server: finds the nearest downed teammate or vehicle in reach and acts on it. */
	void InteractNow();
	bool CanRevive(const ABRCharacter* Other) const;
	ABRCharacter* FindReviveTarget() const;
	ABRVehicle* FindNearVehicle() const;
	/** The thing a press of the interact key would do here, for the on-screen prompt (0 nothing, 1 revive, 2 ride, 3 get off). */
	int32 GetInteractKind() const;

	UPROPERTY(Replicated) float Armor = 0.0f;
	UPROPERTY(Replicated) float ArmorMax = 0.0f;
	UPROPERTY(Replicated) int32 Heals = 1;
	UPROPERTY(Replicated) int32 Fighter = INDEX_NONE;
	UPROPERTY(Replicated) int32 TeamId = 0;
	UPROPERTY(Replicated) int32 KillCount = 0;
	UPROPERTY(Replicated) FString FighterName;
	UPROPERTY(ReplicatedUsing=OnRep_Downed) bool bDowned = false;
	UPROPERTY(Replicated) float DownedHealth = 0.0f;
	UPROPERTY(Replicated) double BleedEndsAt = 0.0;
	UPROPERTY(Replicated) double ReviveEndsAt = 0.0;
	UPROPERTY(Replicated) double HealEndsAt = 0.0;
	UPROPERTY(ReplicatedUsing=OnRep_Flight) bool bAboard = false;
	UPROPERTY(ReplicatedUsing=OnRep_Flight) bool bFreefall = false;
	UPROPERTY(ReplicatedUsing=OnRep_Vehicle) TObjectPtr<ABRVehicle> Vehicle;
	UPROPERTY(Replicated) int32 Seat = 0;

	TWeakObjectPtr<ABRCharacter> ReviveTarget;
	void GoDown();
	void Revived();
	void FinishOff();

protected:
	virtual void BeginPlay() override;

private:
	UFUNCTION() void OnRep_Downed();
	UFUNCTION() void OnRep_Flight();
	UFUNCTION() void OnRep_Vehicle();
	void UpdateFlightVisuals();
	void UpdateDownVisuals();
	void TickAir(float DeltaSeconds);
	void Land();
	void TickVehicleInput();
	double DamagedAt = -100.0;
	FRotator BaseMeshRotation = FRotator::ZeroRotator;
	bool bBaseMeshRotationSet = false;
};

UCLASS()
class ABRLoot : public AActor
{
	GENERATED_BODY()
public:
	ABRLoot();
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	void Setup(EBRLootKind InKind);
	UPROPERTY(ReplicatedUsing=OnRep_Kind) EBRLootKind Kind = EBRLootKind::Heal;
	int32 Spot = INDEX_NONE;
private:
	UFUNCTION() void OnRep_Kind();
	void ApplyLook();
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Body;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Ring;
	float Clock = 0.0f;
	FVector Base = FVector::ZeroVector;
	/** Guests: taken on this screen, until when the host has the chance to agree. */
	double PredictedUntil = 0.0;
};

UENUM()
enum class EBRVehicleKind : uint8 { Skateboard, Car };

/** The electric skateboard (two riders) and the car (four). */
UCLASS()
class ABRVehicle : public AActor
{
	GENERATED_BODY()
public:
	ABRVehicle();
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	void Setup(EBRVehicleKind InKind, const FLinearColor& Paint);
	int32 GetSeats() const { return Kind == EBRVehicleKind::Car ? 4 : 2; }
	bool Mount(ABRCharacter* Rider);
	void Dismount(ABRCharacter* Rider);
	void SetDriverInput(const FVector2D& Input) { DriverInput = Input; }
	void ApplyDriverState(const FVector& At, float InYaw, float InPitch, float InSpeed);
	virtual void PostNetReceiveLocationAndRotation() override;
	/** Checks only: the input set above stays, rather than coming from the driver's stick. */
	bool bScriptedDriver = false;
	bool IsDriver(const ABRCharacter* Rider) const { return Riders.Num() > 0 && Riders[0] == Rider; }
	FVector GetSeatOffset(int32 Index) const;
	UPROPERTY(ReplicatedUsing=OnRep_Look) EBRVehicleKind Kind = EBRVehicleKind::Skateboard;
	UPROPERTY(ReplicatedUsing=OnRep_Look) FLinearColor Paint = FLinearColor::Red;
	UPROPERTY(Replicated) TArray<TObjectPtr<ABRCharacter>> Riders;
	UPROPERTY(Replicated) float Speed = 0.0f;
private:
	UFUNCTION() void OnRep_Look();
	void BuildLook();
	void AddPart(const TCHAR* Shape, const FVector& At, const FVector& Size, const FRotator& Rotation, const FLinearColor& Colour, bool bGlow);
	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Parts;
	UPROPERTY() TObjectPtr<UMaterialInterface> SurfaceMaterial;
	UPROPERTY() TObjectPtr<UMaterialInterface> GlowMaterial;
	FVector2D DriverInput = FVector2D::ZeroVector;
	float Yaw = 0.0f;
	/** The speed being simulated here (Speed is its replicated copy, for the wheels and for others to read). */
	float SimSpeed = 0.0f;
	/** A guest at the wheel: this machine moves it, and what the host sends back is ignored. */
	bool bSimulatingHere = false;
	double NextStateSendAt = 0.0;
	double DevNextLogAt = 0.0;
	double DevStartedAt = 0.0;
	int32 DevCorrections = 0;
	FVector DevLast = FVector::ZeroVector;
	float GroundZ = 0.0f;
	float WheelClock = 0.0f;
	bool bBuilt = false;
};

UCLASS()
class ABRBotController : public AAIController
{
	GENERATED_BODY()
public:
	ABRBotController();
	virtual void Tick(float DeltaSeconds) override;
	float Skill = 0.5f;
private:
	ABRCharacter* FindEnemy(ABRCharacter* Self, float Range, bool& bOutVisible) const;
	AChaosImpactBall* FindBallPickup(ABRCharacter* Self, float Range) const;
	bool DodgeIncoming(ABRCharacter* Self, double Now);
	void MoveToward(ABRCharacter* Self, const FVector& Where, float Rate, double Now);
	TWeakObjectPtr<ABRCharacter> Target;
	double ReleaseAt = 0.0;
	double NextThrowAt = 0.0;
	double NextDashAt = 0.0;
	double NextWanderAt = 0.0;
	double AvoidUntil = 0.0;
	FVector AvoidDirection = FVector::ForwardVector;
	FVector Wander = FVector::ZeroVector;
	FVector LastLocation = FVector::ZeroVector;
	float StrafeSign = 1.0f;
};

/** The airship everyone drops from, drawn on each machine from the game state. */
UCLASS()
class ABRDropship : public AActor
{
	GENERATED_BODY()
public:
	ABRDropship();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
private:
	void Part(const TCHAR* Shape, const FVector& At, const FVector& Size, const FRotator& Rotation, const FLinearColor& Colour, bool bGlow);
	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Prop;
	float Clock = 0.0f;
};

/** The wall of the safe circle, drawn on each machine from the game state. */
UCLASS()
class ABRZoneWall : public AActor
{
	GENERATED_BODY()
public:
	ABRZoneWall();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
private:
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Wall;
};

UCLASS()
class ABRPlayerController : public AChaosImpactPlayerController
{
	GENERATED_BODY()
public:
	ABRPlayerController();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool InputKey(const FInputKeyEventArgs& Params) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(Server, Reliable) void ServerStartMatch(int32 TeamSize, int32 CharacterIndex);
	UFUNCTION(Server, Reliable) void ServerSetCharacter(int32 CharacterIndex);
	UFUNCTION(Server, Reliable) void ServerSetTeamSize(int32 TeamSize);
	UFUNCTION(Server, Reliable) void ServerChooseLanding(FVector2D Where);
	UFUNCTION(Server, Reliable) void ServerAgain();
	void ReturnToBRTitle();
	void LeaveToMainTitle();
	void ToggleMap() { bMapOpen = !bMapOpen; }
	bool IsMapOpen() const { return bMapOpen; }
	void SetMapOpen(bool bOpen) { bMapOpen = bOpen; }
	FVector2D GetChosenLanding() const { return ChosenLanding; }
	bool HasChosenLanding() const { return bChosenLanding; }
	void ChooseLanding(const FVector2D& Where);
	int32 GetSpectated() const { return Spectated; }
	void CycleSpectated(int32 Step);
	UBRHUD* GetHUDWidget() const { return HUD; }
	int32 GetMyTeam() const;
private:
	void ShowTitle();
	void EnsureHud();
	void CloseTitle();
	void UpdateSpectating();
	void UpdateTitleCamera(float DeltaSeconds);
	UPROPERTY() TObjectPtr<UBRHUD> HUD;
	UPROPERTY() TObjectPtr<UBRTitleWidget> Title;
	UPROPERTY() TObjectPtr<ACameraActor> TitleCamera;
	bool bMapOpen = false;
	bool bChosenLanding = false;
	FVector2D ChosenLanding = FVector2D::ZeroVector;
	int32 Spectated = INDEX_NONE;
	float TitleClock = 0.0f;
	EBRPhase LastPhase = EBRPhase::Title;
};

UCLASS()
class ABRGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ABRGameMode();
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
	virtual FString InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal = TEXT("")) override;

	void StartMatch(int32 TeamSize);
	void SetTeamSize(int32 TeamSize);
	/** People sharing this screen (split screen), 1 to 4. */
	void SetLocalPlayers(int32 Count);
	void ReportPlayersToRelay();
	void SetLanding(APlayerController* Who, const FVector2D& Where);
	void ResetToTitle();
	void NotifyLootTaken(int32 Spot);
	void NotifyBallTaken(AChaosImpactBall* Ball);
	ABRCharacter* FindNearestAlly(ABRCharacter* Self) const;
	/** Where a CPU in the fight should head: toward the next circle, or someone's side. */
	FVector GetBotGoal(ABRCharacter* Bot);
	/** Where a CPU falling from the airship steers for. */
	FVector GetBotLanding(ABRCharacter* Bot) const;
	const TArray<FBRFighter>& GetFighters() const { return Fighters; }
	ABRGameState* GetBRState() const;

private:
	UFUNCTION() void HandleEliminated(AChaosImpactCharacter* Character);
	void BuildLootSpots();
	void BuildVehicles();
	void SetPhase(EBRPhase Phase, double Seconds);
	void BoardEveryone();
	void TickShip();
	void StartZone();
	void NextStage(int32 Index);
	void TickZone();
	void TickFighters(float DeltaSeconds);
	void TickFights(float DeltaSeconds);
	void TickLoot();
	void UpdateRoster();
	void KillNumber(FBRFighter& Victim, FBRFighter* Killer);
	void FighterDown(int32 Index, int32 KillerIndex);
	void CheckEnd();
	void TickSelfTest();
	double DevEndAt = -1.0;
	int32 SelfTestStep = -1;
	FVector SelfTestFrom = FVector::ZeroVector;
	double SelfTestNext = 0.0;
	FVector SelfFrom = FVector::ZeroVector;
	UPROPERTY() TObjectPtr<ABRVehicle> SelfVehicle;
	void FinishMatch(int32 WinnerTeam);
	int32 CountAliveTeams() const;
	void Realize(int32 Index);
	void Unrealize(int32 Index);
	ABRCharacter* SpawnReal(int32 Index, const FVector& At, AController* Person);
	void CollectPeople(TArray<FVector2D>& Out) const;
	FVector2D PickLanding(FRandomStream& Random) const;
	EChaosImpactBallType PickBallType(FRandomStream& Random, float Rarity) const;

	UPROPERTY() TObjectPtr<ABRIsland> Island;
	UPROPERTY() TSubclassOf<ACharacter> CharacterClass;
	TArray<FBRFighter> Fighters;
	TArray<FBRLootSpot> LootSpots;
	TMap<int32, FVector2D> TeamLandings;
	TMap<APlayerController*, FVector2D> ChosenLandings;
	double SimClock = 0.0, RosterClock = 0.0, ZoneClock = 0.0, LootClock = 0.0, FightClock = 0.0;
	double ZoneDamageClock = 0.0;
	double NextAutoStart = 0.0;
	int32 AutoTeamSize = 0;
	int32 AutoWaitPeople = 1;
	FRandomStream SimRandom;
	UPROPERTY() TArray<TObjectPtr<ABRVehicle>> Vehicles;
};
