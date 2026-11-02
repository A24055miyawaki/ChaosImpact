#include "RPGTutorial.h"
#include "RPGPrototype.h"
#include "RPGAdventure.h"
#include "RPGAbilityGates.h"
#include "RPGOnline.h"
#include "RPGFieldEvents.h"
#include "RPGTuning.h"
#include "RPGSnowField.h"
#include "RPGTreasureChest.h"
#include "RPGWorldLayout.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactTackleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

// Tutorials, the adventure menu, discovery (the map's fog, new places) and the colony's growth (2026-11).

namespace RPGTutorial
{
const TArray<FStep>& ControlsSteps()
{
	static const TArray<FStep> Steps{
		{TEXT("歩いてみよう"), TEXT("W A S D で歩く"), TEXT("左スティックで歩く"), ETry::Walk},
		{TEXT("ジャンプ"), TEXT("Space でジャンプ"), TEXT("A（下のボタン）でジャンプ"), ETry::Jump},
		{TEXT("ダッシュ"), TEXT("Shift でダッシュ。ゲージを使い、ダッシュ中は攻撃を受けない"), TEXT("B（右のボタン）でダッシュ。ゲージを使い、ダッシュ中は攻撃を受けない"), ETry::Dash},
		{TEXT("投げる"), TEXT("左クリックを長押ししてため、離して投げる"), TEXT("RT を長押ししてため、離して投げる"), ETry::Throw},
		{TEXT("タックル"), TEXT("右クリックでタックル（近くの敵を突き飛ばす）"), TEXT("Y（上のボタン）でタックル（近くの敵を突き飛ばす）"), ETry::Tackle},
		{TEXT("依頼の記録"), TEXT("J で依頼の記録を開く（閉じるのは Esc）"), TEXT("十字キー↓で依頼の記録を開く（閉じるのは B）"), ETry::Journal},
		{TEXT("マップ"), TEXT("M でまわりの地図、もう一度で全体の地図、もう一度で閉じる"), TEXT("十字キー→でまわりの地図、もう一度で全体の地図、もう一度で閉じる"), ETry::Map},
		{TEXT("案内の光"), TEXT("N で目的地への光のラインをオン／オフ"), TEXT("R3（右スティック押し込み）で目的地への光のラインをオン／オフ"), ETry::Guide},
		{TEXT("バッグ"), TEXT("Tab でバッグ（装備・強化・設定）を開く"), TEXT("−（ビュー）ボタンでバッグ（装備・強化・設定）を開く"), ETry::Bag},
		{TEXT("依頼を受ける"), TEXT("掲示板か「！」の人の前で E を押して話す"), TEXT("掲示板か「！」の人の前で Y（上のボタン）を押して話す"), ETry::TalkBoard},
	};
	return Steps;
}

const TArray<FLesson>& Lessons()
{
	static const TArray<FLesson> List{
		{ControlsId, TEXT("基本の操作"), TEXT("歩く・ジャンプ・ダッシュ・投げる・タックル、依頼の記録、マップと案内の光、バッグ、依頼の受け方。メニューからもう一度やり直せる。"), nullptr, nullptr, ETry::None},
		{TEXT("Combat"), TEXT("戦い方"), TEXT("敵は赤い円や線で攻撃を予告してくる。予告が満ちる前に外へ出よう。ダッシュ中は攻撃を受けない。爆弾役・突進役・狙撃役、ひとまわり大きい精鋭に気をつけよう。"),
			TEXT("戦いの最中にダッシュでかわしてみよう"), TEXT("戦いの最中にダッシュ（B）でかわしてみよう"), ETry::DashInFight},
		{TEXT("Map"), TEXT("マップと案内"), TEXT("M（十字→）を押すたびに、まわりの地図→全体の地図→閉じる。N（R3）で、追跡中の依頼まで一番近い道を光のラインが導く。"),
			TEXT("M を押して地図を開いてみよう"), TEXT("十字→ を押して地図を開いてみよう"), ETry::Map},
		{TEXT("Workshop"), TEXT("工房（強化とチューン）"), TEXT("ルビー商人の前で話しかけると工房が開く。強化はルビー、チューンはルビーとチューンの欠片を使う。欠片はレベルアップやボス、金・銀の宝箱で手に入る。強化は +10 に近づくほど高くなる。"),
			TEXT("装備を1回強化するか、チューンを付けてみよう"), TEXT("装備を1回強化するか、チューンを付けてみよう"), ETry::Upgrade},
		{TEXT("Shop"), TEXT("ショップ（ボールを買う）"), TEXT("商人と行商の隊商ではボールを買える。品ぞろえはときどき入れかわる（隊商は来るたびに別の品）。商人に払ったルビーが増えると商人のレベルが上がり、レアなボールが並ぶようになる。"),
			TEXT("ショップでボールを1つ買ってみよう"), TEXT("ショップでボールを1つ買ってみよう"), ETry::Buy},
		{TEXT("Beacon"), TEXT("休息の灯りとファストトラベル"), TEXT("灯りに触れるとともり、倒れたときにそこから再開できる。ともした灯りへは、灯りの前か全体の地図からファストトラベルできる。"),
			TEXT("灯りの前で E を押してファストトラベルを開いてみよう"), TEXT("灯りの前で Y を押してファストトラベルを開いてみよう"), ETry::Travel},
		{TEXT("Chest"), TEXT("宝箱"), TEXT("宝箱は話しかけるボタンで開く。中身は飛び出すので拾おう（ルビーは近づくだけで集まる）。金の宝箱にはチューンの欠片も入っている。"), nullptr, nullptr, ETry::None},
		{TEXT("Special"), TEXT("特殊球"), TEXT("炎・氷・雷などの特殊球はバッグで装備し、持ち替えて投げる。投げるとしばらく待ち時間がある。"),
			TEXT("1〜3 で特殊球に持ち替えて投げてみよう"), TEXT("LB で特殊球に持ち替えて投げてみよう"), ETry::Special},
		{TEXT("Boss"), TEXT("ボス戦"), TEXT("ボスは大技を予告してから放つ。予告の外へ逃げ、技のあとのすきに攻撃しよう。倒すと強化版に挑めるようになる。人数が多いほどボスも硬くなる。"), nullptr, nullptr, ETry::None},
		{TEXT("Down"), TEXT("ダウンと復活"), TEXT("2人以上で遊んでいるとき、倒れるとすぐには戻らずダウンになる。近くの仲間が話しかけるボタンの長押しで起こせる。待たずに戻るときも長押し。"),
			TEXT("仲間を起こすか、長押しで復活地点から戻ってみよう"), TEXT("仲間を起こすか、長押しで復活地点から戻ってみよう"), ETry::Revive},
		{TEXT("Defeat"), TEXT("倒れたとき"), TEXT("倒れて復活地点に戻ると、持っているルビーの2割を失う。ためたルビーは工房や道場で早めに使おう。2人以上なら、ダウン中に仲間に起こしてもらえば失わない。"), nullptr, nullptr, ETry::None},
		{TEXT("Multi"), TEXT("マルチプレイ"), TEXT("O（十字←）で仲間の様子と合言葉を確認できる。地図には仲間の位置が出る。マルチ専用の依頼もあり、ボスは人数に合わせて強くなる。"), nullptr, nullptr, ETry::None},
		{TEXT("Dojo"), TEXT("道場と能力"), TEXT("コロニーの道場で師範ゴウに話しかけると、タックルとダッシュを鍛えられる。鍛えるほど、行ける場所が増えていく。"),
			TEXT("修行を1つ受けてみよう"), TEXT("修行を1つ受けてみよう"), ETry::Learn},
		{TEXT("Gates"), TEXT("能力で開く道"), TEXT("ひびの入った大岩は鍛えたタックルで、氷の壁は炎の球で、谷は跳躍台からのダッシュで越えられる。奥には秘宝が眠っている。"), nullptr, nullptr, ETry::None},
		{TEXT("Elite"), TEXT("精鋭とネームド"), TEXT("ひとまわり大きい精鋭は硬く、攻撃も強い。金色の名を持つネームドはさらに手ごわいが、良い品とチューンの欠片を落とす。"), nullptr, nullptr, ETry::None},
		{TEXT("Events"), TEXT("フィールドイベント"), TEXT("ときどき流れ星が落ちたり、行商の隊商が来たり、吹雪や砂嵐が起きたりする。地図に★で出るので行ってみよう。"), nullptr, nullptr, ETry::None},
		{TEXT("Colony"), TEXT("コロニーの発展"), TEXT("依頼を達成したり掲示板で寄付をすると、コロニーが発展する。発展すると工房の値段が下がり、町もにぎやかになる。"),
			TEXT("掲示板でコロニーに寄付してみよう"), TEXT("掲示板でコロニーに寄付してみよう"), ETry::Donate},
		{TEXT("Discovery"), TEXT("探索と地図の霧"), TEXT("行ったことのない場所は地図の上で霧に隠れている。新しい土地に入ると名前と推奨レベルが出る。冒険手帳で見つけた場所を確かめられる。"), nullptr, nullptr, ETry::None},
	};
	return List;
}

const FLesson* FindLesson(FName Id) { return Lessons().FindByPredicate([Id](const FLesson& L) { return L.Id == Id; }); }

const TArray<TPair<EMenu, const TCHAR*>>& MenuEntries()
{
	static const TArray<TPair<EMenu, const TCHAR*>> List{
		{EMenu::Resume, TEXT("冒険にもどる")}, {EMenu::Map, TEXT("地図")}, {EMenu::Journal, TEXT("依頼の記録")}, {EMenu::Log, TEXT("冒険手帳")},
		{EMenu::Tutorials, TEXT("チュートリアル")}, {EMenu::Members, TEXT("マルチ")}, {EMenu::Bag, TEXT("バッグ・装備")}, {EMenu::Settings, TEXT("ゲーム設定")},
		{EMenu::Title, TEXT("保存してタイトルへ")}};
	return List;
}
}

// ---------------------------------------------------------------- the colony

namespace
{
float GColonyDiscount = 0.0f;
/** Tutorials only for heroes really playing (tests run without them unless asked: -CITutorials). */
bool TutorialsWanted(const ARPGPrototypeCharacter* Hero)
{
	static const bool bForced = FParse::Param(FCommandLine::Get(), TEXT("CITutorials"));
	return Hero && !Hero->IsGrunt() && Hero->IsLocallyControlled() && (bForced || !Hero->HeroId.IsEmpty());
}
}

int32 RPGColony::StageFor(int32 Points)
{
	int32 Stage = 0;
	for (int32 Index = 1; Index <= MaxStage; ++Index) { if (Points >= StagePoints[Index]) { Stage = Index; } }
	return Stage;
}
int32 RPGColony::WorldStage(const UWorld* World) { const auto* Relay = ARPGNetRelay::Get(World); return Relay ? Relay->ColonyStage : 0; }
float RPGColony::Discount() { return GColonyDiscount; }
void RPGColony::SetDiscount(float InDiscount) { GColonyDiscount = FMath::Clamp(InDiscount, 0.0f, 0.5f); }
const TCHAR* RPGColony::StageName(int32 Stage)
{
	static const TCHAR* Names[] = {TEXT("はじまりの集落"), TEXT("花の集落"), TEXT("市の立つ村"), TEXT("にぎわう町"), TEXT("冒険者の都")};
	return Names[FMath::Clamp(Stage, 0, MaxStage)];
}
const TCHAR* RPGColony::StageGift(int32 Stage)
{
	static const TCHAR* Gifts[] = {TEXT(""), TEXT("花壇と旗が飾られた"), TEXT("市場ができ、工房の値段が1割安くなった"), TEXT("家と灯りが増え、工房の値段が2割安くなった"), TEXT("広場に像が立ち、敵が落とすルビーが増えた")};
	return Gifts[FMath::Clamp(Stage, 0, MaxStage)];
}

void ARPGPrototypeCharacter::AddColonyPoints(int32 Points, bool bShare)
{
	if (bGrunt || Points <= 0) { return; }
	const int32 Before = RPGColony::StageFor(ColonyPoints);
	ColonyPoints += Points;
	const int32 After = RPGColony::StageFor(ColonyPoints);
	if (After > Before && GetWorld())
	{
		Notice = FString::Printf(TEXT("コロニーが「%s」に発展した！ %s"), RPGColony::StageName(After), RPGColony::StageGift(After));
		NoticeUntil = GetWorld()->GetTimeSeconds() + 5;
	}
	// A guest's work also grows the host's colony (the one this world shows).
	if (bShare && !HasAuthority() && IsLocallyControlled()) { ServerAddColonyPoints(Points); }
	ShowLesson(TEXT("Colony"));
}

void ARPGPrototypeCharacter::ServerAddColonyPoints_Implementation(int32 Points)
{
	// The host's own hero keeps the world's colony.
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		auto* Host = Cast<ARPGPrototypeCharacter>(It->Get()->GetPawn());
		if (Host && Host->IsLocallyControlled() && !Host->IsGrunt()) { Host->AddColonyPoints(FMath::Clamp(Points, 0, 200), false); break; }
	}
}

bool ARPGPrototypeCharacter::DonateToColony()
{
	constexpr int32 Gift = 200;
	if (Rubies < Gift) { Notice = TEXT("寄付にはルビーが200必要です"); NoticeUntil = GetWorld()->GetTimeSeconds() + 2.5; return false; }
	Rubies -= Gift;
	AddColonyPoints(25);
	TutorialEvent(TEXT("Donate"));
	if (RPGColony::StageFor(ColonyPoints) < RPGColony::MaxStage)
	{ Notice = FString::Printf(TEXT("コロニーに寄付した（発展まであと %d）"), RPGColony::StagePoints[RPGColony::StageFor(ColonyPoints) + 1] - ColonyPoints); NoticeUntil = GetWorld()->GetTimeSeconds() + 3; }
	return true;
}

// ---------------------------------------------------------------- the adventure menu

bool ARPGPrototypeCharacter::OpenAdventureMenu()
{
	if (bGrunt || IsInTitle()) { return false; }
	MapMode = MapClosed;
	return OpenQuestPanel(MenuPanel);
}

void ARPGPrototypeCharacter::ActivateMenuEntry(int32 Entry)
{
	using RPGTutorial::EMenu;
	const auto& Entries = RPGTutorial::MenuEntries();
	if (!Entries.IsValidIndex(Entry)) { return; }
	switch (Entries[Entry].Key)
	{
	case EMenu::Resume: SetQuestMenuOpen(false); break;
	case EMenu::Map: SetQuestMenuOpen(false); SetMapMode(MapWhole); break;
	case EMenu::Journal: SetQuestMenuOpen(false); OpenQuestJournal(); break;
	case EMenu::Log: OpenQuestPanel(LogPanel); break;
	case EMenu::Tutorials: TutorialSelection = 0; OpenQuestPanel(TutorialPanel); break;
	case EMenu::Members: SetQuestMenuOpen(false); OpenMembers(); break;
	case EMenu::Bag: SetQuestMenuOpen(false); SetBagOpen(true); break;
	case EMenu::Settings:
		SetQuestMenuOpen(false);
		if (auto* PC = Cast<AChaosImpactPlayerController>(GetController())) { PC->OpenSettings(); }
		break;
	case EMenu::Title:
		if (GetLocalRPGIndex() != 0 || HeroId.IsEmpty() || (GetWorld() && GetWorld()->GetNetMode() == NM_Client))
		{ Notice = TEXT("タイトルへ戻れるのは 1P（ホスト）だけです。セーブは自動で行われます"); NoticeUntil = GetWorld()->GetTimeSeconds() + 3; break; }
		SetQuestMenuOpen(false); ReturnToTitle();
		break;
	}
}

bool ARPGPrototypeCharacter::HandleMenuKey(FKey Key)
{
	const bool bUp = Key == EKeys::Up || Key == EKeys::W || Key == EKeys::Gamepad_DPad_Up || Key == EKeys::Gamepad_LeftStick_Up;
	const bool bDown = Key == EKeys::Down || Key == EKeys::S || Key == EKeys::Gamepad_DPad_Down || Key == EKeys::Gamepad_LeftStick_Down;
	const bool bOk = Key == EKeys::Enter || Key == EKeys::SpaceBar || Key == EKeys::Gamepad_FaceButton_Bottom;
	const bool bBack = Key == EKeys::Escape || Key == EKeys::Gamepad_FaceButton_Right || Key == EKeys::Gamepad_Special_Right || Key == EKeys::P;
	if (QuestGiver == MenuPanel)
	{
		const int32 Count = RPGTutorial::MenuEntries().Num();
		if (bBack) { SetQuestMenuOpen(false); }
		else if (bUp) { MenuSelection = (MenuSelection + Count - 1) % Count; }
		else if (bDown) { MenuSelection = (MenuSelection + 1) % Count; }
		else if (bOk) { ActivateMenuEntry(MenuSelection); }
		return true;
	}
	if (QuestGiver == LogPanel)
	{
		if (bBack) { OpenQuestPanel(MenuPanel); }
		else if (bUp) { LogScroll = FMath::Max(0, LogScroll - 1); }
		else if (bDown) { ++LogScroll; }
		return true;
	}
	if (QuestGiver == TutorialPanel)
	{
		const int32 Count = RPGTutorial::Lessons().Num();
		if (bBack) { OpenQuestPanel(MenuPanel); }
		else if (bUp) { TutorialSelection = (TutorialSelection + Count - 1) % Count; }
		else if (bDown) { TutorialSelection = (TutorialSelection + 1) % Count; }
		else if (bOk)
		{
			// The controls are done again step by step; the rest are shown again (with their thing to try).
			const FName Id = RPGTutorial::Lessons()[TutorialSelection].Id;
			// One not met yet stays hidden until it comes up in the adventure.
			if (Id != RPGTutorial::ControlsId && !TutorialsDone.Contains(Id)) { Notice = TEXT("まだ出会っていないチュートリアルです"); NoticeUntil = GetWorld()->GetTimeSeconds() + 2; return true; }
			SetQuestMenuOpen(false);
			if (Id == RPGTutorial::ControlsId) { StartControlsTutorial(); } else { ShowLesson(Id, true); }
		}
		return true;
	}
	return false;
}

// ---------------------------------------------------------------- tutorials

void ARPGPrototypeCharacter::StartControlsTutorial()
{
	ControlsStep = 0; StepStartedAt = GetWorld()->GetTimeSeconds(); StepStartLocation = GetActorLocation(); StepWalked = 0; StepEvents.Reset();
	ActiveLesson = NAME_None; LessonQueue.Reset();
}

void ARPGPrototypeCharacter::SkipControlsTutorial()
{
	ControlsStep = -1;
	TutorialsDone.AddUnique(RPGTutorial::ControlsId);
	Notice = TEXT("操作チュートリアルを飛ばしました（メニューの「チュートリアル」からいつでもできます）"); NoticeUntil = GetWorld()->GetTimeSeconds() + 4;
}

void ARPGPrototypeCharacter::ShowLesson(FName Lesson, bool bAgain)
{
	if (!RPGTutorial::FindLesson(Lesson) || (!bAgain && (TutorialsDone.Contains(Lesson) || !TutorialsWanted(this)))) { return; }
	TutorialsDone.AddUnique(Lesson);
	// One at a time; the rest wait their turn (and nothing over the controls' steps).
	if (ActiveLesson != NAME_None || ControlsStep >= 0) { LessonQueue.AddUnique(Lesson); return; }
	ActiveLesson = Lesson; bLessonTried = false; LessonShownAt = GetWorld()->GetTimeSeconds(); LessonDoneAt = -1; StepEvents.Reset();
}

void ARPGPrototypeCharacter::DismissLesson()
{
	ActiveLesson = NAME_None;
	if (LessonQueue.Num() && ControlsStep < 0) { const FName Next = LessonQueue[0]; LessonQueue.RemoveAt(0); ShowLesson(Next, true); }
}

void ARPGPrototypeCharacter::TutorialEvent(FName What) { StepEvents.Add(What); }

void ARPGPrototypeCharacter::UpdateTutorials(float DeltaSeconds)
{
	using RPGTutorial::ETry;
	UWorld* World = GetWorld();
	auto* PC = Cast<AChaosImpactPlayerController>(GetController());
	if (!World || !PC || IsInTitle() || !TutorialsWanted(this)) { return; }
	const double Now = World->GetTimeSeconds();
	const bool bPad = PC->IsUsingGamepad();
	// Has the thing been done?
	const auto Done = [&](ETry Try)
	{
		switch (Try)
		{
		case ETry::Walk: return StepWalked > 600;
		case ETry::Jump: return GetCharacterMovement()->IsFalling() && GetVelocity().Z > 150;
		case ETry::Dash: return IsDashing();
		case ETry::Throw: return LastAttackAt > StepStartedAt;
		case ETry::Tackle: return GetTackle() && GetTackle()->IsActive();
		case ETry::Journal: return bQuestMenuOpen && QuestGiver == INDEX_NONE;
		case ETry::Map: return MapMode != MapClosed;
		case ETry::Guide: return StepEvents.Contains(TEXT("Guide"));
		case ETry::Bag: return bBagOpen;
		case ETry::TalkBoard: return bQuestMenuOpen && QuestGiver >= 0;
		case ETry::Upgrade: return StepEvents.Contains(TEXT("Upgrade"));
		case ETry::Travel: return IsTravelOpen();
		case ETry::Special: return SelectedAttack != 0 && LastAttackAt > LessonShownAt;
		case ETry::Revive: return StepEvents.Contains(TEXT("Revive"));
		case ETry::Learn: return StepEvents.Contains(TEXT("Learn"));
		case ETry::DashInFight: return IsDashing() && Now - LastDamagedAt < 6;
		case ETry::Donate: return StepEvents.Contains(TEXT("Donate"));
		case ETry::Buy: return StepEvents.Contains(TEXT("Buy"));
		default: return false;
		}
	};
	// Skip: Backspace / L3 skips this one; held a second, the whole controls tutorial.
	const bool bSkipDown = PC->IsInputKeyDown(bPad ? EKeys::Gamepad_LeftThumbstick : EKeys::BackSpace);
	const bool bSkipTap = PC->WasInputKeyJustPressed(bPad ? EKeys::Gamepad_LeftThumbstick : EKeys::BackSpace);
	if (bSkipTap) { SkipHeldSince = Now; }
	if (!bSkipDown) { SkipHeldSince = -1; }
	if (ControlsStep >= 0)
	{
		const auto& Steps = RPGTutorial::ControlsSteps();
		if (!Steps.IsValidIndex(ControlsStep)) { ControlsStep = -1; return; }
		StepWalked += FVector::Dist2D(GetActorLocation(), StepStartLocation); StepStartLocation = GetActorLocation();
		if (SkipHeldSince >= 0 && Now - SkipHeldSince > 1.0) { SkipHeldSince = -1; SkipControlsTutorial(); return; }
		const bool bNext = (LessonDoneAt > 0 && Now - LessonDoneAt > 0.9) || (bSkipTap && LessonDoneAt < 0);
		if (LessonDoneAt < 0 && Done(Steps[ControlsStep].Try)) { LessonDoneAt = Now; }
		if (bNext)
		{
			LessonDoneAt = -1; ++ControlsStep; StepStartedAt = Now; StepWalked = 0; StepEvents.Reset();
			if (!Steps.IsValidIndex(ControlsStep))
			{
				ControlsStep = -1; TutorialsDone.AddUnique(RPGTutorial::ControlsId);
				AddRubies(100);
				Notice = TEXT("操作チュートリアル完了！ ごほうびにルビー100。依頼を受けて冒険に出よう"); NoticeUntil = Now + 5;
				DismissLesson();
			}
		}
		return;
	}
	// The first time things come up.
	if (Now >= NextLessonCheckAt)
	{
		NextLessonCheckAt = Now + 0.5;
		const FVector Here = GetActorLocation();
		for (TActorIterator<ARPGPrototypeCharacter> It(World); It; ++It)
		{
			const float D = FVector::DistSquared2D(It->GetActorLocation(), Here);
			if (It->IsGrunt() && !It->IsEliminated())
			{
				if (D < FMath::Square(1300.0f) && Cast<ARPGPrototypeBoss>(*It) && Cast<ARPGPrototypeBoss>(*It)->IsBossActive()) { ShowLesson(TEXT("Boss")); }
				else if (D < FMath::Square(1100.0f)) { ShowLesson(TEXT("Combat")); }
				if (It->IsElite() && D < FMath::Square(2000.0f)) { ShowLesson(TEXT("Elite")); }
			}
			else if (!It->IsGrunt() && It->IsDowned() && D < FMath::Square(3000.0f)) { ShowLesson(TEXT("Down")); }
		}
		if (bDowned) { ShowLesson(TEXT("Down")); }
		if (ARPGPrototypeCharacter::CountHeroes(World) >= 2) { ShowLesson(TEXT("Multi")); }
		if (ARPGTreasureChest::Nearest(this)) { ShowLesson(TEXT("Chest")); }
		for (TActorIterator<ARPGSimaeBody> It(World); It; ++It) { if (It->IsAwake() && FVector::DistSquared2D(It->GetActorLocation(), Here) < FMath::Square(3000.0f)) { ShowLesson(TEXT("Boss")); } }
		for (TActorIterator<ARPGGateObstacle> It(World); It; ++It) { if (!It->IsOpen() && FVector::DistSquared2D(It->GetActorLocation(), Here) < FMath::Square(1400.0f)) { ShowLesson(TEXT("Gates")); } }
		for (TActorIterator<ARPGLeapPoint> It(World); It; ++It) { if (FVector::DistSquared2D(It->GetActorLocation(), Here) < FMath::Square(900.0f)) { ShowLesson(TEXT("Gates")); } }
		if (MapMode != MapClosed) { ShowLesson(TEXT("Map")); }
	}
	if (ActiveLesson == NAME_None) { return; }
	const RPGTutorial::FLesson* Lesson = RPGTutorial::FindLesson(ActiveLesson);
	if (!Lesson) { ActiveLesson = NAME_None; return; }
	if (LessonDoneAt < 0 && Lesson->Try != ETry::None && Done(Lesson->Try)) { LessonDoneAt = Now; bLessonTried = true; }
	// Closes: done (a moment later), skipped, or (nothing to try) after a while.
	if ((LessonDoneAt > 0 && Now - LessonDoneAt > 1.6) || bSkipTap || (Lesson->Try == ETry::None && Now - LessonShownAt > 14)) { DismissLesson(); }
}

// ---------------------------------------------------------------- discovery: the map's fog and new places

namespace
{
constexpr float CellSize = 1000.0f;
int32 CellsX() { return FMath::CeilToInt((RPGWorld::MaxX - RPGWorld::MinX) / CellSize); }
int32 CellsY() { return FMath::CeilToInt((RPGWorld::MaxY - RPGWorld::MinY) / CellSize); }
}

bool ARPGPrototypeCharacter::IsExplored(int32 CellX, int32 CellY) const
{
	if (CellX < 0 || CellY < 0 || CellX >= CellsX() || CellY >= CellsY()) { return false; }
	const int32 Bit = CellX * CellsY() + CellY;
	return Explored.IsValidIndex(Bit / 8) && (Explored[Bit / 8] & (1 << (Bit % 8)));
}

void ARPGPrototypeCharacter::UpdateDiscovery()
{
	UWorld* World = GetWorld();
	if (!World || bGrunt || !IsLocallyControlled() || World->GetTimeSeconds() < NextDiscoveryAt) { return; }
	NextDiscoveryAt = World->GetTimeSeconds() + 0.5;
	const auto* Adventure = ARPGAdventureWorld::Find(World);
	if (!Adventure) { return; }
	Explored.SetNumZeroed((CellsX() * CellsY() + 7) / 8);
	const FVector At = Adventure->GetActorTransform().InverseTransformPosition(GetActorLocation());
	// Everything within about 18 m is seen.
	const int32 CX = FMath::FloorToInt((At.X - RPGWorld::MinX) / CellSize), CY = FMath::FloorToInt((At.Y - RPGWorld::MinY) / CellSize);
	for (int32 X = CX - 2; X <= CX + 2; ++X)
	{
		for (int32 Y = CY - 2; Y <= CY + 2; ++Y)
		{
			if (X < 0 || Y < 0 || X >= CellsX() || Y >= CellsY()) { continue; }
			const FVector2D Middle(RPGWorld::MinX + (X + 0.5f) * CellSize, RPGWorld::MinY + (Y + 0.5f) * CellSize);
			if (FVector2D::Distance(Middle, FVector2D(At.X, At.Y)) > 1850) { continue; }
			const int32 Bit = X * CellsY() + Y;
			Explored[Bit / 8] |= 1 << (Bit % 8);
		}
	}
	// The colony this world shows: the host's first player keeps it; every screen takes its price cut from it.
	if (HasAuthority() && GetLocalRPGIndex() == 0) { if (auto* Relay = ARPGNetRelay::Get(World)) { Relay->ColonyStage = RPGColony::StageFor(ColonyPoints); } }
	const int32 Stage = RPGColony::WorldStage(World);
	// The caravan's workshop: cheaper again while a player on this machine is at it.
	bool bCaravan = false;
	for (TActorIterator<ARPGPrototypeCharacter> It(World); It; ++It) { if (It->IsLocallyControlled() && It->IsAtMerchant() && ARPGFieldEvents::IsCaravan(It->ActiveMerchant.Get())) { bCaravan = true; } }
	RPGColony::SetDiscount((Stage >= 3 ? 0.2f : Stage >= 2 ? 0.1f : 0.0f) + (bCaravan ? RPGTuning::CaravanDiscount : 0.0f));
	// Play time, for the log.
	if (PlayCountedAt <= 0) { PlayCountedAt = World->GetTimeSeconds(); }
	const int32 Whole = FMath::FloorToInt(World->GetTimeSeconds() - PlayCountedAt);
	if (Whole > 0) { PlaySeconds += Whole; PlayCountedAt += Whole; }
}

void ARPGPrototypeCharacter::RevealFirstLands()
{
	Explored.SetNumZeroed((CellsX() * CellsY() + 7) / 8);
	for (int32 X = 0; X < CellsX(); ++X)
	{
		for (int32 Y = 0; Y < CellsY(); ++Y)
		{
			const float PX = RPGWorld::MinX + (X + 0.5f) * CellSize, PY = RPGWorld::MinY + (Y + 0.5f) * CellSize;
			if (RPGWorld::PartAt(PX, PY) != 0 || RPGWorld::RoadDistance(PX, PY) > 4000) { continue; }
			const int32 Bit = X * CellsY() + Y;
			Explored[Bit / 8] |= 1 << (Bit % 8);
		}
	}
}

void ARPGPrototypeCharacter::NoteVisited(const FString& Place)
{
	if (bGrunt || Place.IsEmpty() || VisitedPlaces.Contains(Place)) { return; }
	VisitedPlaces.Add(Place);
	if (!IsLocallyControlled() || !GetWorld()) { return; }
	// A card for the new place: its name and how strong its enemies are.
	PlaceCardName = Place; PlaceCardAt = GetWorld()->GetTimeSeconds(); PlaceCardLevel = 0;
	TArray<int32> Levels;
	for (TActorIterator<ARPGPrototypeSpawnPoint> It(GetWorld()); It; ++It)
	{ if (It->bEnabled && FVector::DistSquared2D(It->GetActorLocation(), GetActorLocation()) < FMath::Square(3500.0f)) { Levels.Add(It->EnemyLevel); } }
	if (Levels.Num()) { Levels.Sort(); PlaceCardLevel = Levels[Levels.Num() / 2]; }
	if (VisitedPlaces.Num() > 1) { ShowLesson(TEXT("Discovery")); }
}
