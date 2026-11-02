#pragma once

#include "CoreMinimal.h"

class ARPGPrototypeCharacter;
class UWorld;

/**
 * Learning by doing (2026-11). A new hero first goes through the controls, step by step: each step says what to
 * press and waits until it has been done. After that, the first time each thing comes up (a merchant, a beacon, a
 * boss, being downed …) a lesson card explains it, most with something to try. Everything can be skipped, and every
 * lesson can be read again from the adventure menu (チュートリアル).
 */
namespace RPGTutorial
{
	/** What a step or a lesson waits for (ARPGPrototypeCharacter::TutorialEvent / checked each frame). */
	enum class ETry : uint8 { None, Walk, Jump, Dash, Throw, Tackle, Journal, Map, Guide, Bag, TalkBoard, Upgrade, Travel, Special, Revive, Learn, DashInFight, Donate, Buy };

	struct FStep
	{
		const TCHAR* Title;
		/** What to do, with the keys for the keyboard and for a pad. */
		const TCHAR* KeyboardText;
		const TCHAR* PadText;
		ETry Try;
	};
	/** The controls, in order. */
	const TArray<FStep>& ControlsSteps();

	struct FLesson
	{
		FName Id;
		const TCHAR* Title;
		const TCHAR* Body;
		/** Something to try (keyboard / pad wording), or none. */
		const TCHAR* TryKeyboard;
		const TCHAR* TryPad;
		ETry Try;
	};
	const TArray<FLesson>& Lessons();
	const FLesson* FindLesson(FName Id);
	/** The controls tutorial's own entry in the list (replaying it starts the steps again). */
	inline const FName ControlsId(TEXT("Controls"));

	/** The adventure menu's entries (P / Esc / Start). */
	enum class EMenu : uint8 { Resume, Map, Journal, Log, Tutorials, Members, Bag, Settings, Title };
	const TArray<TPair<EMenu, const TCHAR*>>& MenuEntries();
}

/** The colony grows with what its heroes do for it (quests, gifts). Online the world shows the host's colony. */
namespace RPGColony
{
	/** Points needed for each stage (0 the start). */
	constexpr int32 StagePoints[] = {0, 120, 360, 800, 1600};
	constexpr int32 MaxStage = 4;
	int32 StageFor(int32 Points);
	/** The colony this world shows (the host's). */
	int32 WorldStage(const UWorld* World);
	/** Workshop prices are this much lower in a grown colony (0..1). */
	float Discount();
	void SetDiscount(float InDiscount);
	const TCHAR* StageName(int32 Stage);
	/** What a stage brings, for the notice and the log. */
	const TCHAR* StageGift(int32 Stage);
}
