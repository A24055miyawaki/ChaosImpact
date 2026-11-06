#pragma once

#include "CoreMinimal.h"
#include "ChaosImpactNameEntry.h"
#include "ChaosImpactTournament.h"
#include "Styling/SlateBrush.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "UObject/Object.h"
#include "ChaosImpactLocalTourney.generated.h"

class AChaosImpactCharacterPreview;
class AChaosImpactPlayerController;
class FSlateWindowElementList;
struct FAnalogInputEvent;
struct FGeometry;
struct FKeyEvent;

/**
 * ローカルトーナメント (VS モードの「トーナメント」): a tournament on this machine alone, 1 v 1 duels in a bracket.
 *  - 1試合ずつ: 1 to 32 people (and CPUs, 32 entrants at most). Each person is registered once (name, character, colour,
 *    kept to the end); when a match with people in it comes up, they take a controller (or the keyboard) by pressing on
 *    it, as a party game passes the pads round, and play it on this screen.
 *  - 同時進行: 1 to 4 people, each with their own controller for the whole tournament (split screen), all of a round's
 *    matches at once.
 * The registration is kept here (it outlives the levels) until the game ends.
 */
USTRUCT()
struct FChaosImpactLocalTourneyPerson
{
	GENERATED_BODY()
	UPROPERTY() FString Name;
	UPROPERTY() int32 Character = 0;
	/** The ordinary colour (0-3), and a カスタムカラー preset of the character's (INDEX_NONE: none). */
	UPROPERTY() int32 Colour = 0;
	UPROPERTY() int32 Preset = INDEX_NONE;
};

UCLASS()
class UChaosImpactLocalTourneySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	static UChaosImpactLocalTourneySubsystem* Get(const UObject* WorldContext);

	static constexpr int32 MaxPeopleOneAtATime = 32;
	static constexpr int32 MaxPeopleAllAtOnce = 4;
	static constexpr int32 MaxNameLength = 8;

	UPROPERTY() TArray<FChaosImpactLocalTourneyPerson> People;
	UPROPERTY() EChaosImpactTournamentMode Mode = EChaosImpactTournamentMode::OneAtATime;
	UPROPERTY() int32 CPUs = 0;
	/** INDEX_NONE: おまかせ (each CPU at random). */
	UPROPERTY() int32 CPULevel = INDEX_NONE;

	bool IsAllAtOnce() const { return Mode == EChaosImpactTournamentMode::Simultaneous; }
	int32 GetMaxPeople() const;
	/** Why it cannot start as it is (empty: it can). */
	FString GetStartBlocker() const;
	/** A new person, named and dressed after how many there are. Returns their index (INDEX_NONE: full). */
	int32 AddPerson();
	/** The people as the tournament's entrants (their カスタムカラー taken from their presets), and its set-up. */
	TArray<FChaosImpactTournamentEntrant> MakeEntrants() const;
	FChaosImpactTournamentSettings MakeSettings() const;
	/** Every person's colours as they are drawn: the ordinary colour, or the preset's parts. */
	TArray<uint8> GetCustomColours(const FChaosImpactLocalTourneyPerson& Person) const;
	static FString DescribeCPULevel(int32 Level);
};

/**
 * The registration screen of ローカルトーナメント, laid out as the character select: the characters' icons on the left,
 * the person being set up large in the middle (name, character, colour), on the right how the tournament runs and the
 * people taking part (a card each, up to 32, and one to add another). Keys, a controller and the mouse all work it.
 */
UCLASS()
class UChaosImpactTourneyEntry : public UObject
{
	GENERATED_BODY()
public:
	enum class EArea : uint8 { Characters, Colours, Settings, People, Buttons };

	void Open(AChaosImpactPlayerController* InController);
	void Close();
	bool IsOpen() const { return bOpen; }
	void Tick(float DeltaSeconds);
	int32 Paint(const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer) const;
	bool HandleKeyDown(const FKeyEvent& Event);
	bool HandleAnalog(const FAnalogInputEvent& Event);
	bool HandleClick(const FVector2D& DesignPoint);
	void HandleMouseMove(const FVector2D& DesignPoint);
	bool HandleCharacter(TCHAR Character);

private:
	UChaosImpactLocalTourneySubsystem* GetData() const;
	FChaosImpactLocalTourneyPerson* GetCurrent() const;
	void Move(int32 Columns, int32 Rows);
	void Confirm();
	void Back();
	void ChangeSetting(int32 Row, int32 Direction);
	void SelectPerson(int32 Index);
	void RemoveCurrent();
	void OpenNaming();
	void FinishNaming();
	void RefreshPreview();
	/** The colours offered for the current person's character: the four ordinary ones, then its presets. */
	int32 GetColourChoiceCount() const;
	int32 GetCurrentColourChoice() const;
	void Notify(const FString& Message);

	FBox2D CharacterRect(int32 Index) const;
	FBox2D ColourRect(int32 Choice) const;
	FBox2D SettingRect(int32 Row) const;
	FBox2D PersonRect(int32 Index) const;
	FBox2D ButtonRect(int32 Button) const;
	int32 GetPeopleCards() const;

	TWeakObjectPtr<AChaosImpactPlayerController> Controller;
	bool bOpen = false;
	double OpenedAt = 0.0;
	EArea Area = EArea::Characters;
	int32 Current = 0;
	int32 CharacterCursor = 0;
	int32 ColourCursor = 0;
	int32 SettingCursor = 0;
	int32 PersonCursor = 0;
	int32 ButtonCursor = 3;
	int32 Frame = 0;
	double NextStickAt[2] = {0.0, 0.0};
	FString Message;
	double MessageAt = -100.0;
	FChaosImpactNameEntry Naming;

	/** The current person's model, and an icon face for each character (filmed a few at a time). */
	UPROPERTY(Transient) TObjectPtr<AChaosImpactCharacterPreview> Preview;
	FSlateBrush PreviewBrush;
	FString PreviewShows;
	UPROPERTY(Transient) TArray<TObjectPtr<AChaosImpactCharacterPreview>> Portraits;
	TArray<FSlateBrush> PortraitBrushes;
	TArray<double> PortraitBornAt;
	int32 PortraitsMade = 0;
};
