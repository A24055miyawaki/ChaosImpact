#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/SaveGame.h"
#include "RPGTitle.generated.h"

class ARPGPrototypeCharacter;
class UEditableText;
class UCanvasPanel;
class UChaosImpactPuppetComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;

/** One hero (a character and its own save). Each player picks their own hero. */
USTRUCT()
struct FRPGHeroEntry
{
	GENERATED_BODY()
	UPROPERTY(SaveGame) FString Id;
	UPROPERTY(SaveGame) FString Name;
	/** Roster character (ChaosImpactRoster) and colour. */
	UPROPERTY(SaveGame) int32 Character = 0;
	UPROPERTY(SaveGame) int32 Colour = 0;
	UPROPERTY(SaveGame) int32 Level = 1;
	UPROPERTY(SaveGame) int32 QuestsDone = 0;
	UPROPERTY(SaveGame) FDateTime Created;
	UPROPERTY(SaveGame) FDateTime LastPlayed;
	UPROPERTY(SaveGame) float PlaySeconds = 0;
};

/** The list of heroes on this machine. Their progress is in one save slot each (RPGHeroes::SaveSlot). */
UCLASS()
class URPGHeroIndexSave : public USaveGame
{
	GENERATED_BODY()
public:
	UPROPERTY(SaveGame) int32 Version = 1;
	UPROPERTY(SaveGame) TArray<FRPGHeroEntry> Heroes;
};

namespace RPGHeroes
{
	constexpr int32 MaxHeroes = 12;
	constexpr int32 MaxNameLength = 10;
	TArray<FRPGHeroEntry> Load();
	bool Store(const TArray<FRPGHeroEntry>& Heroes);
	FString SaveSlot(const FString& Id);
	/** Adds a hero with a fresh id (no progress yet); returns it, or an empty entry when the list is full. */
	FRPGHeroEntry Create(const FString& Name, int32 Character, int32 Colour);
	bool Delete(const FString& Id);
	/** Changes a hero's look (any time, on the title). */
	bool SetLook(const FString& Id, int32 Character, int32 Colour);
	/** Level, quests and time after a save. */
	void Touch(const FString& Id, int32 Level, int32 QuestsDone, float AddSeconds);
	FString RandomName();
	/** "?CIRPGHeroes=1?CIRPGHero0=..." for the level that starts with these heroes. */
	FString TravelOptions(const TArray<FString>& HeroIds);
	/** Automation only: keep test heroes out of the player's real list. Empty restores it. */
	void SetIndexSlotForTests(const FString& Slot);
}

/** Native text/IME must paint above the title's custom NativePaint, not beneath it. */
UCLASS(Transient, NotBlueprintable)
class URPGTitleInputLayer : public UUserWidget
{
	GENERATED_BODY()
public:
	UEditableText* AddField(FName Name);
protected:
	virtual void NativeOnInitialized() override;
};

/** One saved hero standing in the colony on the title (or the empty spot that makes a new one). */
UCLASS(NotBlueprintable, Transient)
class ARPGHeroStatue : public AActor
{
	GENERATED_BODY()
public:
	ARPGHeroStatue();
	/** Character < 0: the empty, glowing spot for a new hero. */
	void Show(int32 Character, int32 Colour);
	void SetSelected(bool bSelected);
	/** Only this view (a player's view target) sees the statue, so split screens each show their own row. */
	void SetViewer(AActor* Viewer);
	virtual void Tick(float DeltaSeconds) override;
	FVector Head() const;
private:
	UPROPERTY() TObjectPtr<USkeletalMeshComponent> Body;
	UPROPERTY() TObjectPtr<UChaosImpactPuppetComponent> Puppet;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Stand;
	int32 ShownCharacter = -2;
	int32 ShownColour = -1;
	bool bIsSelected = false;
	bool bHasViewer = false;
	float Glow = 0;
};

/**
 * The RPG's title (Minecraft Dungeons-like): press a button, choose offline / online and how many play, then (2 or
 * more) the shared controller assignment; then every player picks or creates a hero (look, then name) on their own
 * screen, among the heroes standing in the colony, and starts right there. Players who join later (＋ on a free pad,
 * or the bag's player count) pick theirs the same way (Join). A hero's look can be changed on the title at any time.
 * Drawn in C++ with RPGPrototypeUIKit; the heroes stand in the world behind.
 */
UCLASS()
class URPGTitleWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	enum class EMode : uint8 { Title, Join };
	enum class EPage : uint8 { Press, Heroes, Look, Name, Play, Assigning, Leaving, Online, Rooms };
	void Setup(EMode InMode);
	EMode GetMode() const { return Mode; }
	EPage GetPage() const { return Page; }
	/** Keys and pad buttons routed by the RPG player controller (Enter/A ok, Esc/B back, arrows/D-pad move). */
	bool HandleKey(const FKey& Key);
	/** A mouse click at the cursor. */
	void HandleClick();
	bool IsFinished() const { return bFinished; }
	// Test hooks.
	void CreateHeroForTest(const FString& Name, int32 Character);
	int32 GetHeroCount() const { return Heroes.Num(); }
	void SetPlayers(int32 Count) { Players = FMath::Clamp(Count, 1, 4); }
	/** After the controller assignment: adds the players here and now; everyone then picks a hero. */
	void StartWithAssignment(const FString& Options);
	/** Tests: the online page with this password (no typing). */
	void SetPasswordForTest(const FString& Password) { PasswordDraft = Password; }
	FString DescribeNameBoxForTest() const;
	UEditableText* GetNameInputForTest() const { return NameBox.Get(); }
protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
		FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool bEnabled) const override;
	UFUNCTION() void OnNameCommitted(const FText& Text, ETextCommit::Type Method);
	UFUNCTION() void OnPasswordCommitted(const FText& Text, ETextCommit::Type Method);
private:
	friend struct FRPGTitleLayout;
	EMode Mode = EMode::Title;
	EPage Page = EPage::Press;
	TArray<FRPGHeroEntry> Heroes;
	int32 HeroCursor = 0;
	int32 LookCursor = 0;
	int32 ColourCursor = 0;
	int32 PlayCursor = 0;
	int32 Players = 1;
	FString NameDraft;
	bool bConfirmDelete = false;
	bool bFinished = false;
	FString Message;
	double MessageUntil = 0;
	double PageSince = 0;
	FString ChosenHero;
	/** The hero whose look is being changed (empty: the look of a new hero). */
	FString EditingHero;
	UPROPERTY() TObjectPtr<UCanvasPanel> Root;
	UPROPERTY() TObjectPtr<URPGTitleInputLayer> InputLayer;
	UPROPERTY() TObjectPtr<UEditableText> NameBox;
	/** The online password (keyboard; a pad gets the remembered or a random one). */
	UPROPERTY() TObjectPtr<UEditableText> PasswordBox;
	FString PasswordDraft;
	/** Online page: 0 open this world, 1 search. Rooms page: the listed world. */
	int32 OnlineCursor = 0;
	int32 RoomCursor = 0;
	void OpenOnline(bool bHost);
	int32 Hover = 0;
	/** Widget-local units per design unit (for the plates over the heroes' heads). */
	float ViewScale = 1;
	/** The heroes standing in the colony (a window of them round the selection) and where they are on screen. */
	TArray<TWeakObjectPtr<ARPGHeroStatue>> Statues;
	struct FStatueView { int32 Entry = INDEX_NONE; FVector2D Head = FVector2D::ZeroVector; FVector2D Feet = FVector2D::ZeroVector; bool bOnScreen = false; };
	TArray<FStatueView> StatueViews;
	int32 StatueFirst = 0;
	static constexpr int32 StatueSlots = 5;
	FVector StatueCenter = FVector::ZeroVector;
	FVector StatueSide = FVector::RightVector;
	float StatueYaw = 0;
	void PlaceStatues();
	void RefreshStatues();
	void HideStatues();
	ARPGPrototypeCharacter* Player() const;
	void GoTo(EPage NewPage);
	void Act(int32 Action);
	void ShowLook(int32 Character, int32 Colour) const;
	void ConfirmName();
	void Choose(const FRPGHeroEntry& Hero);
	void ToHeroes();
	bool IsTaken(const FString& Id) const;
	void Say(const FString& Text);
	int32 ActionAtCursor() const;
};
