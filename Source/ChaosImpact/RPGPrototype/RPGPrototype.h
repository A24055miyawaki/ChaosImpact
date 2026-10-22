#pragma once

#include "CoreMinimal.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactBall.h"
#include "AIController.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/SaveGame.h"
#include "ChaosImpactPlayerController.h"
#include "RPGEquipment.h"
#include "RPGAdventureTypes.h"
#include "RPGTuning.h"
#include "RPGQuests.h"
#include "RPGPrototype.generated.h"

class URPGPrototypeHUD;
class UStaticMeshComponent;
class UArrowComponent;
class ARPGPrototypeLoot;
class ARPGPrototypeSpawnPoint;
class ARPGPrototypeMerchant;
class ARPGPrototypeBoss;
class ARPGAdventureCheckpoint;
class ACameraActor;
class ARPGPrototypeGameMode;
class URPGTitleWidget;
struct FInputKeyEventArgs;
struct FRPGHeroEntry;

UENUM(BlueprintType)
enum class ERPGGruntRole : uint8 { Chaser, Thrower };

UENUM(BlueprintType)
enum class ERPGPrototypeRarity : uint8 { Normal, Rare, Unique, Legend };

/** Pages of the bag menu (ARPGPrototypeCharacter::BagPage). Values are kept for saves/tests. */
namespace ERPGBagPage { enum : int32 { Equipment = 0, Upgrade = 1, Tune = 2, Settings = 3, Items = 4 }; }
/** Item categories of the bag list (ARPGPrototypeCharacter::BagFilter). */
namespace ERPGBagFilter { enum : int32 { Ball = 1, Badge = 2, Gear = 3, Material = 4 }; }
/** Orders of the bag list (ARPGPrototypeCharacter::BagSort). */
namespace ERPGBagSort { enum : int32 { Acquired = 0, Newest, Rarity, Strength, Kind, Count }; }

/** Isolated RPG equipment. No dependency on the real game's saved loadout. */
USTRUCT(BlueprintType)
struct FRPGPrototypeItem
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FGuid Id;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) EChaosImpactBallType Type = EChaosImpactBallType::Normal;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) ERPGPrototypeRarity Rarity = ERPGPrototypeRarity::Normal;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 Level = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float Power = 18.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float Cooldown = 0.55f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) ERPGItemKind Kind = ERPGItemKind::Ball;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 Enhancement = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FRPGAffix Badge;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FRPGAffix> Intrinsics;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FRPGAffix> Tunes;
	UPROPERTY() int32 TuneOfferGeneration = 0;
	/** Player's "keep" mark: a locked item is never salvaged, one by one or in bulk. Saved with the item. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bLocked = false;
	TArray<int32> TuneOfferIndices(int32 Socket) const;
	bool IsValid() const { return Id.IsValid(); }
	FString Name() const;
	float Mod(FName Key) const;
	bool Has(FName Key) const { return Mod(Key)>0; }
	float EffectivePower() const;
	float EffectiveCooldown() const;
	int32 SalvageValue() const;
	int32 UpgradeCost() const;
	int32 TuneCost() const;
	static FRPGPrototypeItem MakeBadge(ERPGItemKind Kind, int32 Level=1, ERPGPrototypeRarity Rarity=ERPGPrototypeRarity::Normal, int32 Archetype=INDEX_NONE);
	static FRPGPrototypeItem MakeGear(ERPGItemKind Kind, int32 Level=1, ERPGPrototypeRarity Rarity=ERPGPrototypeRarity::Normal, int32 Archetype=INDEX_NONE);
	static FRPGPrototypeItem Make(EChaosImpactBallType Type, int32 Level = 1,
		ERPGPrototypeRarity Rarity = ERPGPrototypeRarity::Normal);
};

struct FRPGPrototypeDamageText
{
	FVector Location = FVector::ZeroVector;
	float Amount = 0.0f;
	double StartedAt = 0.0;
	bool bPlayer = false;
};

USTRUCT()
struct FRPGSavedCooldown
{
	GENERATED_BODY()
	UPROPERTY(SaveGame) FGuid Item;
	UPROPERTY(SaveGame) float Remaining=0;
};

/** Online: what a player's own machine tells the host about the hero it plays (the host fights with it). RPGOnline.cpp. */
USTRUCT()
struct FRPGNetHero
{
	GENERATED_BODY()
	UPROPERTY() TArray<FRPGPrototypeItem> Attacks;
	UPROPERTY() TArray<FRPGPrototypeItem> Badges;
	UPROPERTY() TArray<FRPGPrototypeItem> Gears;
	UPROPERTY() int32 SelectedAttack = 0;
	UPROPERTY() int32 Level = 1;
	UPROPERTY() FString Name;
	UPROPERTY() int32 Character = 0;
	UPROPERTY() int32 Colour = 0;
	/** First report of a hero only: its saved HP, which the host's copy starts with. Otherwise below 0. */
	UPROPERTY() float StartHP = -1;
};

/** Online: a kill credited to a player, rewarded on that player's own machine (XP, rubies, drops, quests). */
USTRUCT()
struct FRPGKillCredit
{
	GENERATED_BODY()
	UPROPERTY() int32 Level = 1;
	UPROPERTY() bool bBoss = false;
	/** For quests: 1 the boss, 2 a thrower-role enemy, 4 finished by this player's special ball. */
	UPROPERTY() uint8 Detail = 0;
	UPROPERTY() FVector_NetQuantize At = FVector::ZeroVector;
	UPROPERTY() float DropChance = 0.25f;
	/** Special-ball cooldown given back (the finisher's KillRefund). */
	UPROPERTY() float KillRefund = 0;
};

/** Disposable test saves, deliberately separate from all main-game saves. */
UCLASS()
class URPGPrototypeSave : public USaveGame
{
	GENERATED_BODY()
public:
	UPROPERTY(SaveGame) int32 Version=3;
	UPROPERTY(SaveGame) TArray<FRPGPrototypeItem> Items;
	UPROPERTY(SaveGame) TArray<FGuid> AttackIds;
	UPROPERTY(SaveGame) TArray<FGuid> BadgeIds;
	UPROPERTY(SaveGame) TArray<FGuid> GearIds;
	UPROPERTY(SaveGame) TArray<FRPGSavedCooldown> Cooldowns;
	UPROPERTY(SaveGame) int32 Level=1;
	UPROPERTY(SaveGame) int32 XP=0;
	UPROPERTY(SaveGame) int32 Rubies=0;
	UPROPERTY(SaveGame) int32 SelectedAttack=0;
	UPROPERTY(SaveGame) float HP=100;
	UPROPERTY(SaveGame) float Stamina=5;
	UPROPERTY(SaveGame) FDateTime SavedAt;
	UPROPERTY(SaveGame) TArray<FRPGAdventureProgress> Quests;
	UPROPERTY(SaveGame) TArray<FName> OpenedChestIds;
	/** Rest beacons this hero has lit (ARPGPrototypeCharacter::TravelKey), for fast travel. */
	UPROPERTY(SaveGame) TArray<FString> TravelPoints;
};

UCLASS()
class ARPGPrototypePlayerController : public AChaosImpactPlayerController
{
	GENERATED_BODY()
public:
	virtual bool InputKey(const FInputKeyEventArgs& Params) override;
	void ConfigureRPGInput(int32 Count,int32 Keyboard)
	{ RequestedLocalPlayerCount=Count; ActiveKeyboardPlayerIndex=RequestedKeyboardPlayerIndex=Keyboard; bPrimaryUsesGamepad=bRequestedPrimaryUsesGamepad=Keyboard!=0; }
};

UCLASS()
class ARPGPrototypeCharacter : public AChaosImpactCharacter
{
	GENERATED_BODY()
public:
	ARPGPrototypeCharacter();
	void ActivateAdventureCheckpoint(ARPGAdventureCheckpoint* Checkpoint);
	UPROPERTY(Transient) TWeakObjectPtr<ARPGAdventureCheckpoint> ActiveAdventureCheckpoint;
	UPROPERTY(Transient) TSet<FName> OpenedChestIds;
	virtual void Tick(float DeltaSeconds) override;
	virtual float TakeDamage(float Amount, const FDamageEvent& Event, AController* EventInstigator, AActor* Causer) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	// ---- Online (RPGOnline.cpp). The host runs the world and every hero's HP, down state and hits; each player's own
	// machine keeps that hero's bag, equipment, level, rubies and quests, and tells the host what it fights with.
	/** The host's copy of a player on another machine. */
	bool IsRemoteHero() const;
	/** This machine's own player, online as a guest. */
	bool IsNetClientHero() const;
	/** A notice on this player's own screen (sent there when this is the host's copy of a guest). */
	void ShowNotice(const FString& Text, float Seconds = 2.5f);
	/** Rewards of a kill, on the player's own machine. */
	void ReceiveKillCredit(const FRPGKillCredit& Credit);
	/** The part of a kill reward that is about HP and the fight (the host's copy). */
	void ApplyKillEffects(const FRPGKillCredit& Credit);
	UFUNCTION(Server, Reliable) void ServerSyncHero(const FRPGNetHero& Hero);
	UFUNCTION(Server, Reliable) void ServerSelectAttack(int32 Slot);
	UFUNCTION(Server, Reliable) void ServerSetReviveHeld(bool bHeld);
	/** A beacon lit or a place travelled to: where this player gets up again, and (lit) a full heal. */
	UFUNCTION(Server, Reliable) void ServerRestAt(FVector_NetQuantize Location, FRotator Rotation, bool bHeal);
	/** A guest moved itself far at once (a gate, fast travel): the host's copy goes there too. */
	UFUNCTION(Server, Reliable) void ServerTeleportTo(FVector_NetQuantize Location, FRotator Rotation);
	/** Moves this player at once; online, also the host's copy. */
	void TeleportHero(const FVector& Location, const FRotator& Rotation);
	UFUNCTION(Client, Reliable) void ClientKillCredit(const FRPGKillCredit& Credit);
	UFUNCTION(Client, Reliable) void ClientShowNotice(const FString& Text, float Seconds);
	UFUNCTION(Client, Reliable) void ClientApplyKnockback(FVector_NetQuantizeNormal Direction, float Distance);
	UFUNCTION(Client, Reliable) void ClientApplyStatus(FName Key, float Value, float Seconds);
	UFUNCTION(Client, Reliable) void ClientDropAllBalls();
	virtual void RequestBallSwap() override;
	virtual void RequestDropBall() override;
	virtual bool HandleEasyPointerKey(const FKey& Key, EInputEvent Event) override;
	virtual bool IsPointerOverGameplayUI() const override;
	bool SelectAttack(int32 Slot);
	float ConsumeAttack();
	float GetCooldownLeft(int32 Slot) const;
	const FRPGPrototypeItem& GetAttack(int32 Slot) const;
	int32 GetSelectedAttack() const { return SelectedAttack; }
	const TArray<FRPGPrototypeItem>& GetBag() const { return Bag; }
	bool AddLoot(const FRPGPrototypeItem& Item);
	bool EquipBagItem(int32 Index, int32 Slot);
	const FRPGPrototypeItem& GetBadge(int32 Slot) const;
	const FRPGPrototypeItem& GetGear(int32 Slot) const;
	const FRPGPrototypeItem& GetEquipment(int32 Slot) const;
	static FString EquipmentLabel(int32 Slot);
	bool FitsEquipmentSlot(const FRPGPrototypeItem& Item,int32 Slot) const;
	void SelectEquipment(int32 Slot);
	void BeginEquipmentChange();
	void CancelEquipmentChange();
	void SwitchInventoryPage(int32 Page);
	bool EquipSelectedItem();
	float GetDisplayedPower(const FRPGPrototypeItem& Item) const;
	int32 EquipmentSelection=0;
	bool bChoosingEquipment=false, bChoosingDestination=false;
	int32 EquipmentReturnPage=0;
	float BadgeMod(FName Key) const;
	bool IsEquipped(const FGuid& Id) const;
	bool SalvageItem(int32 Index);
	bool UpgradeItem(int32 Index);
	bool ApplyTune(int32 Index, int32 Definition, int32 Socket);
	bool UpgradeTune(int32 Index, int32 Socket);
	TArray<int32> GetTuneOffers(int32 Index,int32 Socket) const;
	bool OpenMerchant(ARPGPrototypeMerchant* Merchant);
	ARPGPrototypeMerchant* FindMerchant() const;
	bool IsAtMerchant() const;
	int32 GetRubies() const { return Rubies; }
	void AddRubies(int32 Amount);
	/** A ruby picked up off the ground: added, with one running "+N" on screen for a quick run of them. */
	void PickUpRubies(int32 Amount);
	/** Explicit, opt-in development aid; never supplies guaranteed loot in ordinary play. */
	UFUNCTION(Exec) void RPGTestKit();
	UFUNCTION(Exec) void RPGKnockbackTestKit();
	/** Writes every ability (key, name, range) to Saved/RPGPrototypeQA/RPGCatalog.csv for tuning in a spreadsheet. */
	UFUNCTION(Exec) void RPGDumpCatalog();
	void ReduceSpecialCooldowns(float Seconds);
	void RestoreHP(float Amount);
	void RegisterRPGHit(ARPGPrototypeCharacter* Target, const FRPGPrototypeItem& Item, float Applied, bool bProc);
	float OutgoingDamage(const FRPGPrototypeItem& Item, ARPGPrototypeCharacter* Target, float Amount, bool bBounce);
	void ApplyRPGStatus(FName Key, float Value, float Seconds, APawn* Source=nullptr);
	float Status(FName Key) const;
	void ClearRPGStatus(FName Key) { Statuses.Remove(Key); StatusSources.Remove(Key); }
	float GetAimError() const;
	void ApplyRPGStun(float Seconds);
	/** Swept horizontal knockback, capped and resisted; never an ice/stun status. */
	bool ApplyRPGKnockback(const FVector& Direction,float Distance);
	bool IsRPGKnockedBack() const { return KnockbackDistance>0; }
	bool IsRPGStunned() const { return Status(TEXT("Stun"))>0; }
	/** Huge bosses (the snowfield's Shima-enaga shell): no freezing, stuns or knockback. */
	virtual bool IsCrowdControlImmune() const { return false; }
	/** The Shima-enaga's slam makes everyone drop their balls: in the RPG every ball waits a moment instead. */
	virtual void LoseCarriedBalls() override;
	virtual bool IsMoveInputIgnored() const override;
	virtual bool IsMatchInputLocked() const override;
	// ---- Down and revive (RPGRevive.cpp). With two or more players a knockout is a 30-second "down" first:
	// a teammate holding the interact button (E / 上ボタン) for 3 s nearby gets them up with a third of their HP.
	bool IsDowned() const { return bDowned; }
	float GetDownedLeft() const;
	/** 0..1 while someone is holding the button over this player. */
	float GetReviveProgress() const { return ReviveProgress; }
	/** A downed teammate close enough for this player to help up. */
	ARPGPrototypeCharacter* FindReviveTarget() const;
	/** Held this frame (set from this player's input; online, sent to the host). */
	bool bReviveHeld = false;
	/** Players (not enemies) in the world, knocked out ones included; title screens not counted. */
	static int32 CountHeroes(const UWorld* World);
	/** "2P" or the hero's name. */
	FString GetHeroLabel() const;
	// ---- Falling off the field (RPGRevive.cpp): back to the last firm ground, with a little damage.
	virtual void FellOutOfWorld(const UDamageType& DamageType) override;
	void RecoverFromFall();
	// ---- Fast travel (RPGFastTravel.cpp) to the colony and to rest beacons this hero has lit.
	static constexpr int32 TravelPanel = -2;
	struct FTravelPoint { FString Key, Name; FVector At = FVector::ZeroVector; FRotator Rotation = FRotator::ZeroRotator; bool bVisited = false; bool bColony = false; };
	TArray<FTravelPoint> TravelList() const;
	static FString TravelKey(const AActor* Beacon);
	TArray<FString> TravelPoints;
	int32 TravelSelection = 0;
	bool IsTravelOpen() const { return bQuestMenuOpen && QuestGiver == TravelPanel; }
	/** Opens the list (Why gets the reason when it cannot: fighting, down, the boss's nest). */
	bool OpenFastTravel();
	bool CanFastTravel(FString* Why = nullptr) const;
	bool HandleTravelKey(FKey Key);
	bool FastTravelTo(int32 Index);
	/** A rest beacon right here (E opens fast travel). */
	ARPGAdventureCheckpoint* NearestBeacon() const;
	// ---- The multiplayer menu (RPGOnline.cpp): everyone in this world, their HP, level and connection, the password.
	static constexpr int32 MembersPanel = -3;
	bool IsMembersOpen() const { return bQuestMenuOpen && QuestGiver == MembersPanel; }
	bool OpenMembers();
	bool HandleMembersKey(FKey Key);
	float GetRPGSnowScale() const;
	void EmitDecoy(float Seconds);
	void RebuildEquipment();
	void TickEquipment(float DeltaSeconds);
	ARPGPrototypeLoot* FindNearbyLoot() const;
	bool PickupNearbyLoot();
	void SetBagOpen(bool bOpen);
	bool IsBagOpen() const { return bBagOpen; }
	bool UsesMiniMenu() const;
	void ToggleMenuStyle();
	int32 GetLocalRPGIndex() const;
	bool SaveTestProgress(const FString& SlotOverride=FString());
	bool LoadTestProgress(const FString& SlotOverride=FString());
	FString GetTestSaveSlot() const;
	void RequestSessionAction(int32 Action);
	bool HasConfirmation() const { return PendingSalvage!=INDEX_NONE || PendingTune!=INDEX_NONE || PendingSessionAction!=0 || bPendingBulkSalvage; }
	void ConfirmSessionAction();
	FString SaveSummary=TEXT("未保存");
	// ---- Heroes (RPGTitle.cpp): each player plays one hero with its own save.
	/** Empty in test sessions (they use the per-player test slots). */
	FString HeroId;
	UPROPERTY(Replicated) FString HeroName;
	/** The hero's look (roster character, colour); -1 until a hero is chosen. */
	int32 HeroCharacter = -1, HeroColour = 0;
	/** Starts playing as this hero: loads its save if it has one, shows its look, saves from now on. */
	void BeginHero(const FRPGHeroEntry& Hero);
	URPGTitleWidget* GetTitleWidget() const { return TitleWidget; }
	bool IsInTitle() const;
	/** Saves the hero now, quietly (no message); also refreshes its card on the title. */
	bool AutosaveHero();
	/** Settings → 保存してタイトルへ (1P, hero sessions only). */
	bool ReturnToTitle();
	int32 SettingsSelection=0, PendingSessionAction=0;
	URPGPrototypeHUD* GetPrototypeHUD() const { return PrototypeHUD; }
	float GetRPGRespawnLeft() const;
	int32 GetRPGLevel() const { return RPGLevel; }
	int32 GetXP() const { return XP; }
	int32 GetRequiredXP() const { return RPGTuning::RequiredXP(RPGLevel); }
	void AwardKillXP();
	void GainRPGExperience(int32 Amount);
	TArray<FRPGAdventureProgress> Quests;
	bool IsQuestMenuOpen() const { return bQuestMenuOpen; }
	void SetQuestMenuOpen(bool Open);
	bool HandleQuestKey(FKey Key);
	bool InteractAdventure();
	void ActivateQuest(int32 Index);
	void AdvanceAdventureKill();
	// ---- Quests (RPGQuests.cpp). Talking to the board or a person offers their quests; field quests are found.
	void QuestEvent(ERPGQuestEvent Event, const FVector& At=FVector::ZeroVector, int32 Detail=0, const FString& Place=FString());
	bool AcceptQuest(int32 Index);
	bool ClaimQuest(int32 Index);
	void DiscoverQuest(int32 Index);
	bool OpenQuestJournal();
	bool TalkToGiver(int32 Giver);
	void ToggleQuestTrack(int32 Index);
	void SetQuestTab(int32 Tab);
	/** Enter / A in the quest panel: accept (asking first in the journal), report, or claim. */
	void RequestQuestPrimary();
	/** The quests listed in the open panel (one giver's, or the journal tab's), in display order. */
	TArray<int32> QuestPanelList() const;
	/** -1 the journal, 0 the colony board, 1.. a person (RPGQuests::Givers). */
	int32 QuestGiver=INDEX_NONE;
	int32 QuestTab=0;
	bool bQuestConfirm=false;
	int32 TrackedQuest=INDEX_NONE;
	FString QuestBannerLabel;
	int32 QuestBannerQuest=INDEX_NONE;
	double QuestBannerAt=-100;
	void TickAdventure(float DeltaSeconds);
	int32 QuestSelection=0;
	void SetEnemy(bool bEnemy, int32 Level = 1);
	bool IsGrunt() const { return bGrunt; }
	TWeakObjectPtr<ARPGPrototypeSpawnPoint> SpawnPoint;
	UPROPERTY(Replicated) ERPGGruntRole EncounterRole = ERPGGruntRole::Thrower;
	FVector EncounterHome = FVector::ZeroVector;
	void SetPrototypeAutomatedInput(bool bOn) { bAutomatedInput = bOn; bDevAutoInput = bOn; }
	int32 BagSelection = 0;
	int32 EquipTargetSlot = 1;
	int32 BagFilter = 1;
	int32 BagPage = 0;
	int32 TuneSelection = 0;
	int32 TuneSocket = 0;
	int32 DetailScroll = 0;
	int32 PendingSalvage = INDEX_NONE;
	int32 PendingTune = INDEX_NONE;
	TArray<int32> FilteredBag() const;
	/** The items of one category (ignores the equipment picker), for counts on the category tabs. */
	int32 CountInFilter(int32 Filter,bool bUnseenOnly=false) const;
	void NavigateBag(int32 Direction);
	/** List order (ERPGBagSort). Remembered per local player. */
	int32 BagSort = 0;
	void CycleBagSort();
	static FString BagSortName(int32 Sort);
	/** Items picked up but not yet looked at in the bag ("NEW"). Not saved. */
	TSet<FGuid> UnseenItems;
	bool IsUnseen(const FGuid& Id) const { return UnseenItems.Contains(Id); }
	int32 GetUnseenCount() const { return UnseenItems.Num(); }
	void MarkSeen(int32 Index);
	bool ToggleItemLock(int32 Index);
	/** Unequipped, unlocked, already-seen Normal/Rare items of the current category. */
	TArray<int32> BulkSalvageCandidates() const;
	bool RequestBulkSalvage();
	bool ConfirmBulkSalvage();
	bool bPendingBulkSalvage = false;
	bool HandleBagKey(FKey Key);
	bool InspectEquipmentSlot(int32 Slot);
	FString Notice;
	double NoticeUntil = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RPG Prototype") float PlayerHP = 100.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RPG Prototype") float EnemyHP = 54.0f;
	/** Zero inherits the original VS Blueprint speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RPG Prototype") float WalkSpeed = 0;
	FVector DecoyLocation = FVector::ZeroVector;
	double DecoyUntil = 0;
protected:
	virtual void BeginPlay() override;
	void EnsureReadyBall();
	void UpdatePrototypeInput();
	UPROPERTY(Transient) TObjectPtr<URPGPrototypeHUD> PrototypeHUD;
	UPROPERTY(Transient) TObjectPtr<URPGTitleWidget> TitleWidget;
	double NextAutosaveAt=0, HeroStartedAt=0;
	bool bHeroChecked=false, bQuietSave=false;
	void UpdateHeroFlow();
	UPROPERTY(Transient) TArray<FRPGPrototypeItem> Bag;
	UPROPERTY(Transient) TArray<FRPGPrototypeItem> Attacks;
	UPROPERTY(Transient) TArray<FRPGPrototypeItem> Badges;
	UPROPERTY(Transient) TArray<FRPGPrototypeItem> Gears;
	TWeakObjectPtr<ARPGPrototypeMerchant> ActiveMerchant;
	int32 Rubies = 0;
	float VSBaseSpeed = 560, VSStaminaRegen = .08f, VSStaminaHit = 1, VSDashCD = .8f, VSChargeTime = 1;
	float Shield = 0, TravelDistance = 0;
	float RPGSnowGrowth = 0, PresentedSnowGrowth = 0;
	FVector LastTravelPosition = FVector::ZeroVector;
	bool bWasDashing = false, bWasEliminated = false, bCounterReady = false, bKillDashReady = false, bCrowdReady = false;
	double LastAttackAt = -100, LastKillAt = -100, LastHealAt = -100, LastDashAt = -100;
	double LastSurviveAt = -100, LastLowHealAt = -100, LastPaybackAt = -100, LastDodgeAt = -100;
	double RevivedAt = -100, LastColdRefundAt = -100;
	double StunResistanceUntil = 0;
	double KnockbackResistanceUntil = 0;
	FVector KnockbackDirection=FVector::ZeroVector;
	float KnockbackDistance=0, KnockbackApplied=0, KnockbackElapsed=0;
	void TickRPGKnockback(float DeltaSeconds);
	int32 LastAttackSlot = 0, NormalHits = 0;
	TMap<FName, FVector2D> Statuses;
	TMap<FName, TWeakObjectPtr<APawn>> StatusSources;
	TMap<uint32, FVector2D> ComboTargets;
	TSet<TWeakObjectPtr<AActor>> VolleyTargets;
	int32 SelectedAttack = 0;
	UPROPERTY(Replicated) int32 RPGLevel = 1;
	int32 XP = 0;
	double ReadyAt[3] = {0, 0, 0};
	TMap<FGuid, double> ItemReadyAt;
	UPROPERTY(Replicated) bool bGrunt = false;
	/** MaxHealth as the host has it, for everyone else's screens (enemy bars, teammates). */
	UPROPERTY(Replicated) float RepMaxHealth = 0;
	bool bBagOpen = false;
	bool bAutomatedInput = false;
	double NextBagNavigationAt = 0;
	double NextDetailScrollAt = 0;
	double NextPickupAt = 0;
	/** Rubies picked up in a row (one running total on screen). */
	int32 RubyStreak = 0;
	double RubyStreakAt = 0;
	bool bPreferMiniMenu=false, bPreferencesReady=false;
	bool bQuestMenuOpen=false;
	FString LastPlace;
	double NextPlaceCheckAt=0;
	bool OpenQuestPanel(int32 Giver);
	void AdvanceQuest(int32 Index,int32 Count);
	void ShowQuestBanner(const FString& Label,int32 Index);
	void InitializeRPGPreferences();
	// Down / revive / fall state (RPGRevive.cpp). The host decides; every screen draws it.
	UPROPERTY(ReplicatedUsing = OnRep_Downed) bool bDowned = false;
	bool bBleedingOut = false;
	/** When the down runs out, in the host's clock (AGameStateBase::GetServerWorldTimeSeconds). */
	UPROPERTY(Replicated) float DownedEndsAt = 0;
	UPROPERTY(Replicated) float ReviveProgress = 0;
	UFUNCTION() void OnRep_Downed();
	void ApplyDownedPose(bool bLying);
	// Online sync (RPGOnline.cpp).
	uint32 SentHeroSignature = 0;
	double NextHeroSyncAt = 0;
	bool bSentReviveHeld = false, bHeroSynced = false;
	void TickNetSync();
	TWeakObjectPtr<ARPGPrototypeCharacter> Reviver;
	FTransform StandingMeshTransform;
	TArray<FVector> SafeGround;
	double GroundedSince = -1, NextSafeGroundAt = 0;
	void TickDownAndCare(float DeltaSeconds);
	void EnterDowned();
	void FinishDowned(bool bRevived);
};

UENUM()
enum class ERPGBossState : uint8 { Idle, VolleyTell, Volley, ChargeTell, Charge, Recovery, ReturnHome, LeapTell, Leap, Wave, Defeated };

/** Removable RPG-only encounter. No dependency on VS CPU rules or persistent saves. */
UCLASS()
class ARPGPrototypeBoss : public ARPGPrototypeCharacter
{
	GENERATED_BODY()
public:
	ARPGPrototypeBoss();
	virtual void Tick(float DeltaSeconds) override;
	virtual float TakeDamage(float Amount, const FDamageEvent& Event, AController* EventInstigator, AActor* Causer) override;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RPG Prototype|Boss", meta=(ClampMin="10")) int32 StartingLevel = 10;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RPG Prototype|Boss", meta=(ClampMin="300")) float ActivationRange = 1150;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RPG Prototype|Boss", meta=(ClampMin="100")) float HPAtLevel10 = 1800;
	ERPGBossState GetBossState() const { return State; }
	bool IsBossActive() const { return bEngaged && State!=ERPGBossState::Defeated; }
	bool IsBossArmoredMove() const { return State==ERPGBossState::Charge || State==ERPGBossState::ReturnHome || State==ERPGBossState::Leap || State==ERPGBossState::Wave; }
	bool IsEnraged() const { return Health<=MaxHealth*.5f; }
	bool CanRetry(const ARPGPrototypeCharacter* Player) const;
	bool Retry(ARPGPrototypeCharacter* Player);
	void ResetEncounter();
	float GetWaveRadius() const { return WaveRadius; }
	FVector GetTellDirection() const { return LockedDirection; }
	FVector GetArenaHome() const { return Home.GetLocation(); }
	FString GetBossCue() const;
	static float HealthForLevel(int32 Level,float Base=1800) { return Base*(1+.09f*FMath::Max(0,Level-10)); }
protected:
	virtual void BeginPlay() override;
private:
	ERPGBossState State = ERPGBossState::Idle;
	FTransform Home;
	FVector LockedDirection = FVector::ForwardVector, PreviousPosition = FVector::ZeroVector;
	double StateUntil=0, NextActionAt=0, NextShotAt=0, FreezeResistanceUntil=0, FreezeAcceptedUntil=0;
	float WaveRadius=0, LastWaveRadius=0;
	int32 MoveNumber=0, ShotsLeft=0, ChargesLeft=0;
	bool bContactHit=false, bWaveHit=false, bRewarded=false, bEngaged=false;
	TSet<TWeakObjectPtr<ARPGPrototypeCharacter>> ChargeVictims,WaveVictims;
	void Enter(ERPGBossState NewState,float Seconds);
	void BeginCharge(ARPGPrototypeCharacter* Player);
	void BeginReturnHome();
	void FireExtraShot(float Angle);
	void ClearEncounterAttacks();
	void ConfigureLevel();
};

UCLASS()
class ARPGPrototypeBall : public AChaosImpactBall
{
	GENERATED_BODY()
public:
	ARPGPrototypeBall();
	virtual void Launch(const FVector& Direction, float Speed,
		EChaosImpactBallFlightMode Mode = EChaosImpactBallFlightMode::Arc, float UpSpeed = 0) override;
	virtual bool IsPickupAvailable() const override { return false; }
	virtual bool ConsumePickup(AChaosImpactCharacter* Collector) override { return false; }
	virtual void Tick(float DeltaSeconds) override;
	FRPGPrototypeItem ShotItem;
	bool bSecondary = false;
	bool bProcDamage = false;
	float ShotCharge = 0;
	float DamageMultiplier = 1;
	float DealRPGDamage(ARPGPrototypeCharacter* Target, float Multiplier=1, bool bProc=false);
	void Burst(const FVector& At, ARPGPrototypeCharacter* Direct=nullptr);
	void FinishRPGDrive();
	float GetRPGDriveEnd() const { return LaunchedAt+FMath::Min(5.0f,2+ShotItem.Mod(TEXT("DriveTime"))); }
protected:
	virtual void BeginPlay() override;
	UFUNCTION() void RPGImpact(UPrimitiveComponent* Component, AActor* Other, UPrimitiveComponent* OtherComponent, FVector Impulse, const FHitResult& Hit);
	UFUNCTION() void RPGBounce(const FHitResult& Hit, const FVector& Velocity);
private:
	bool bAttackConsumed = false;
	bool bLaunched = false, bFinished = false, bReturning = false, bCurved = false, bTurned = false;
	FVector LastPosition = FVector::ZeroVector, LaunchPosition = FVector::ZeroVector, OriginalDirection = FVector::ZeroVector;
	float DistanceFlown = 0, MaxDistance = 1700, LaunchedAt = 0, NextTrailAt = 0;
	float RollingUntil = 0;
	int32 Pierces = 0, Bounces = 0;
	TSet<TWeakObjectPtr<AActor>> Struck;
	void SpawnSecondary(EChaosImpactBallType Type, FVector At, FVector Direction, float PowerScale, float Delay=0);
};

/** RPG-only persistent fields, delayed hits, and distributed bird swarms. */
UCLASS()
class ARPGPrototypeEffect : public AActor
{
	GENERATED_BODY()
public:
	ARPGPrototypeEffect();
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	// Online: the host's effect, shown on every screen (what it does only happens on the host).
	UPROPERTY(Replicated) FRPGPrototypeItem Item;
	UPROPERTY(Replicated) FName Effect;
	TWeakObjectPtr<ARPGPrototypeCharacter> Source, Follow;
	UPROPERTY(Replicated) FVector Direction = FVector::ForwardVector;
	UPROPERTY(Replicated) float Radius = 160;
	UPROPERTY(Replicated) float Duration = 2;
	float PowerScale = .15f;
	float StartedAt = 0, NextPulseAt = 0;
	int32 GetPeckCount() const { int32 N=0; for(int32 P:BirdPecks) { N+=P; } return N; }
	bool bSecondary = false;
	static ARPGPrototypeEffect* Create(ARPGPrototypeCharacter* Owner, const FRPGPrototypeItem& Item, FName Effect,
		FVector At, float Radius=160, float Duration=2, float PowerScale=.15f, ARPGPrototypeCharacter* Follow=nullptr);
protected:
	virtual void BeginPlay() override;
private:
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Pieces;
	TArray<TWeakObjectPtr<ARPGPrototypeCharacter>> BirdTargets;
	TArray<FVector> BirdLocations;
	TArray<float> BirdReadyAt;
	TArray<int32> BirdPecks;
	TSet<TWeakObjectPtr<AActor>> OnceTargets;
	bool bExtended = false;
	int32 PulseCount = 0;
	float FeatherRecovered = 0;
};

UCLASS()
class ARPGPrototypeMerchant : public AActor
{
	GENERATED_BODY()
public:
	ARPGPrototypeMerchant();
	virtual void Tick(float DeltaSeconds) override;
protected:
	virtual void BeginPlay() override;
};

/** Not an AChaosImpactBall: walking over loot cannot accidentally collect it. */
UCLASS()
class ARPGPrototypeLoot : public AActor
{
	GENERATED_BODY()
public:
	ARPGPrototypeLoot();
	virtual void Tick(float DeltaSeconds) override;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> Mesh;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="RPG Prototype") FRPGPrototypeItem Item;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RPG Prototype", meta=(ClampMin="1")) float LifetimeSeconds=60;
	UPROPERTY(Transient) TObjectPtr<ARPGPrototypeCharacter> Recipient;
	bool IsVisibleTo(const ARPGPrototypeCharacter* Player) const { return Recipient==Player; }
	bool Collect(ARPGPrototypeCharacter* Player);
	/** Thrown out of a chest: flies, bounces once or twice, then floats where it landed. Never expires. */
	void Pop(const FVector& Velocity);
	/** From a chest: kept until picked up (LifetimeSeconds is ignored). */
	bool bChestLoot = false;
protected:
	virtual void BeginPlay() override;
private:
	FVector RestLocation;
	bool bCollected = false;
	FVector BaseScale;
	FVector PopVelocity = FVector::ZeroVector;
	FVector PopOrigin = FVector::ZeroVector;
	bool bFlying = false;
	int32 PopBounces = 0;
};

UCLASS()
class ARPGPrototypeSpawnPoint : public AActor
{
	GENERATED_BODY()
public:
	ARPGPrototypeSpawnPoint();
	UPROPERTY(VisibleAnywhere) TObjectPtr<UArrowComponent> Arrow;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RPG Prototype", meta=(ClampMin="1")) int32 EnemyLevel = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RPG Prototype", meta=(ClampMin="0", ClampMax="1")) float DropChance = 0.25f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RPG Prototype", meta=(ClampMin="1")) float RespawnSeconds = 10.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RPG Prototype") bool bEnabled = true;
	/** Opt-in: legacy combat-test maps keep their original single enemy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RPG Prototype|Encounter") bool bEncounterGroup = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RPG Prototype|Encounter", meta=(ClampMin="0",ClampMax="8")) int32 ChaserCount = 3;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RPG Prototype|Encounter", meta=(ClampMin="0",ClampMax="8")) int32 ThrowerCount = 2;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RPG Prototype|Encounter", meta=(ClampMin="300")) float EngageRadius = 850.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RPG Prototype|Encounter", meta=(ClampMin="600")) float PursuitRadius = 1550.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RPG Prototype|Encounter", meta=(ClampMin="1000")) float PreloadRadius = 2200.f;
	TArray<TWeakObjectPtr<ARPGPrototypeCharacter>> Members;
	ARPGPrototypeCharacter* GetEncounterTarget() const { return EncounterTarget.Get(); }
	bool IsEngaged() const { return EncounterTarget.IsValid(); }
	void TickEncounter(ARPGPrototypeGameMode* Mode, double Now);
	void AlertTo(ARPGPrototypeCharacter* Player);
	TWeakObjectPtr<ARPGPrototypeCharacter> Enemy;
	double NextSpawnAt = 0;
private:
	TWeakObjectPtr<ARPGPrototypeCharacter> EncounterTarget;
	bool bWaveSpawned = false;
	bool bCleared = false;
	bool FindMemberFloor(int32 Slot, FVector& At) const;
};

UCLASS()
class ARPGPrototypeGruntController : public AAIController
{
	GENERATED_BODY()
public:
	ARPGPrototypeGruntController();
	virtual void Tick(float DeltaSeconds) override;
protected:
	virtual void OnPossess(APawn* InPawn) override;
private:
	void TickEncounter(float DeltaSeconds);
	FVector EncounterMove = FVector::ZeroVector;
	float EncounterMoveScale = 0;
	double NextEncounterThinkAt = 0;
	double NextThrowAt = 0;
	double ReleaseAt = 0;
	double AvoidUntil = 0;
	FVector AvoidDirection = FVector::ZeroVector;
	FVector LastLocation = FVector::ZeroVector;
	double NextWanderAt = 0;
	float StrafeSign = 1;
};

UCLASS()
class ARPGPrototypeGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	ARPGPrototypeGameMode();
	virtual void Tick(float DeltaSeconds) override;
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	UPROPERTY(EditDefaultsOnly, Category="RPG Prototype") TSubclassOf<ARPGPrototypeCharacter> GruntClass;
	UPROPERTY(EditAnywhere, Category="RPG Prototype", meta=(ClampMin="1")) float LootLifetimeSeconds=60;
	UPROPERTY(EditAnywhere, Category="RPG Prototype", meta=(ClampMin="1")) float AssistSeconds=20;
	TArray<FRPGPrototypeDamageText> DamageTexts;
	void AddDamageText(ARPGPrototypeCharacter* Victim, float Amount);
	ARPGPrototypeLoot* SpawnLoot(const FVector& Location, const FRPGPrototypeItem& Item, ARPGPrototypeCharacter* Recipient=nullptr);
	/** A drop for one player, on that player's own machine (also a guest's, which has no game mode). */
	static ARPGPrototypeLoot* SpawnLootIn(UWorld* World, const FVector& Location, const FRPGPrototypeItem& Item, ARPGPrototypeCharacter* Recipient, float Lifetime);
	void RewardEnemy(ARPGPrototypeCharacter* Enemy,bool bBoss=false);
	void ClearEnemyContributions(ARPGPrototypeCharacter* Enemy) { Contributions.Remove(Enemy); }
	void SetLocalPlayerCount(int32 Count);
	int32 GetLocalPlayerCount() const;
	/** Applies a controller assignment ("CILocalPlayers=2?CIKeyboardPlayer=0?CIPadDevice0=..") to this world live. */
	void ApplyLocalSetup(const FString& Options);
	/** Mid-game join: any button on a pad nobody holds shows a small "＋ボタンで参加"; ＋ (Start) adds a player with it. */
	bool HandleUnownedPad(const FInputKeyEventArgs& EventArgs);
	/** Adds a local player who uses this pad (they then pick a hero on their own screen). */
	bool JoinWithPad(int32 InputDeviceId);
	bool IsJoinPromptShown() const;
	void SetSharedScreen(bool Enabled);
	bool IsSharedScreenRequested() const { return bSharedScreen; }
	bool UsesSharedScreen() const { return bSharedScreen && GetLocalPlayerCount()>1; }
	void ApplyRPGScreenLayout();
	// Online (RPGOnline.cpp): up to four players in all; a guest starts in the colony.
	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
	UPROPERTY(EditAnywhere,Category="RPG Prototype|Shared Camera",meta=(ClampMin="600",ClampMax="5000")) float SharedWarpDistance=1700;
	static ARPGPrototypeCharacter* NearestPlayer(UWorld* World,const FVector& Location);
	/** Per-player attack windows; different groups cannot all fire at that player at once. */
	bool ReserveGruntAttack(ARPGPrototypeCharacter* Grunt, ARPGPrototypeCharacter* Target, float Seconds);
	int32 GetActiveGruntAttackers(const ARPGPrototypeCharacter* Target) const;
	void ResetPrototype();
protected:
	virtual void BeginPlay() override;
	UFUNCTION() void HandleEliminated(AChaosImpactCharacter* Character);
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
	bool bResetPending = false;
	int32 DesiredLocalPlayers=1;
	double JoinPromptUntil=0;
	bool bSharedScreen=false;
	UPROPERTY(Transient) TObjectPtr<ACameraActor> SharedCamera;
	TMap<TWeakObjectPtr<ARPGPrototypeCharacter>,double> SharedWarpAt;
	void TickSharedScreen(float DeltaSeconds);
    void BeginSharedViewDraw();
    void EndSharedViewDraw();
    FDelegateHandle SharedBeginDrawHandle,SharedEndDrawHandle;
	TMap<TWeakObjectPtr<ARPGPrototypeCharacter>,TMap<TWeakObjectPtr<ARPGPrototypeCharacter>,double>> Contributions;
	struct FGruntAttackWindow { TWeakObjectPtr<ARPGPrototypeCharacter> Target; double Until = 0; };
	TMap<TWeakObjectPtr<ARPGPrototypeCharacter>,FGruntAttackWindow> GruntAttackWindows;
	TMap<TWeakObjectPtr<ARPGPrototypeCharacter>,double> NextGruntAttackAt;
};
