#include "ChaosImpactBossScene.h"

#include "ChaosImpact.h"
#include "ChaosImpactBallTypes.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactEase.h"
#include "ChaosImpactFxKit.h"
#include "ChaosImpactHints.h"
#include "ChaosImpactPaint.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactSettings.h"
#include "ChaosImpactSfx.h"
#include "ChaosImpactSimaeBoss.h"
#include "ChaosImpactPresidentBoss.h"
#include "ChaosImpactWhiteHall.h"
#include "ChaosImpactSimaeBossFx.h"
#include "ChaosImpactSoloRoom.h"
#include "ChaosImpactStageDecor.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/App.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Widgets/SLeafWidget.h"

namespace
{
	using EPart = AChaosImpactBossScene::EPart;

	/** Drawn over everything while a scene plays: the scene paints itself into it. */
	class SChaosImpactSceneOverlay : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SChaosImpactSceneOverlay) {}
		SLATE_END_ARGS()

		void Construct(const FArguments&, AChaosImpactBossScene* InScene)
		{
			Scene = InScene;
			SetVisibility(EVisibility::HitTestInvisible);
		}
		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1.0, 1.0); }
		virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&, FSlateWindowElementList& Elements,
			const int32 Layer, const FWidgetStyle&, bool) const override
		{
			if (const AChaosImpactBossScene* Owner = Scene.Get())
			{
				Owner->PaintOverlay(Geometry, Elements, Layer);
			}
			return Layer + 80;
		}

	private:
		TWeakObjectPtr<AChaosImpactBossScene> Scene;
	};

	TWeakObjectPtr<AChaosImpactBossScene> GActiveScene;

	float Smooth(const float T) { return ChaosImpactEase::InOut(T); }
	float Window(const float Time, const float From, const float To) { return FMath::Clamp((Time - From) / FMath::Max(To - From, 0.001f), 0.0f, 1.0f); }

	/** One piece of a line as typed: a character, or a word with its reading over it ({base|reading}). */
	struct FToken
	{
		FString Base;
		FString Ruby;
	};

	TArray<FToken> Tokenize(const FString& Text)
	{
		TArray<FToken> Out;
		for (int32 Index = 0; Index < Text.Len(); ++Index)
		{
			if (Text[Index] == TEXT('{'))
			{
				const int32 Bar = Text.Find(TEXT("|"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Index);
				const int32 End = Text.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Index);
				if (Bar > Index && End > Bar)
				{
					Out.Add({Text.Mid(Index + 1, Bar - Index - 1), Text.Mid(Bar + 1, End - Bar - 1)});
					Index = End;
					continue;
				}
			}
			Out.Add({FString::Chr(Text[Index]), FString()});
		}
		return Out;
	}

	int32 BaseLength(const TArray<FToken>& Tokens)
	{
		int32 Length = 0;
		for (const FToken& Token : Tokens)
		{
			Length += Token.Base.Len();
		}
		return Length;
	}

	/** Never at the start of a row (Japanese line-breaking: closing marks and small kana stay with what is before). */
	bool HangsBack(const FString& Base)
	{
		static const FString Marks = TEXT("、。，．！？!?…ー）」』”・ぁぃぅぇぉっゃゅょァィゥェォッャュョ～");
		return Base.Len() == 1 && Marks.Contains(Base);
	}

	/** A pause after the mark (seconds of typing), so the words breathe. */
	float PauseAfter(const FString& Base)
	{
		if (Base == TEXT("。") || Base == TEXT("！") || Base == TEXT("？"))
		{
			return 0.22f;
		}
		if (Base == TEXT("、") || Base == TEXT("…") || Base == TEXT("."))
		{
			return 0.06f;
		}
		return 0.0f;
	}

	constexpr float TypeRate = 26.0f;

	bool IsGap(const FString& Base) { return Base == TEXT("\u3000") || Base == TEXT(" "); }
	bool EndsSentence(const FString& Base) { return Base == TEXT("。") || Base == TEXT("！") || Base == TEXT("？") || Base == TEXT("!") || Base == TEXT("?"); }
	bool EndsPhrase(const FString& Base) { return EndsSentence(Base) || Base == TEXT("、") || Base == TEXT("…") || Base == TEXT("」") || Base == TEXT("”"); }

	/**
	 * A line's rows, read naturally: a new sentence (after a full-width space or 。！？) starts a new row, phrases (up to
	 * 、 or a closing mark) are kept whole, and only a phrase too long for a row is broken (never before a closing mark).
	 * When that takes more than three rows: sentences run on (only the full-width spaces start rows), then simply filled.
	 */
	TArray<TArray<int32>> LayoutRows(const TArray<FToken>& Tokens, const float Width, const float Size)
	{
		const auto Measure = [Size](const FToken& Token) { return ChaosImpactPaint::HudMeasure(Token.Base, Size, TEXT("Bold")); };
		for (int32 Mode = 0; Mode < 3; ++Mode)
		{
			// Phrases: [From, To) with whether a new row is wanted before it.
			struct FPhrase { int32 From; int32 To; bool bNewRow; };
			TArray<FPhrase> Phrases;
			int32 From = 0;
			bool bBreakNext = false;
			for (int32 Index = 0; Index < Tokens.Num(); ++Index)
			{
				const FString& Base = Tokens[Index].Base;
				const bool bLast = Index + 1 >= Tokens.Num();
				const FString Next = bLast ? FString() : Tokens[Index + 1].Base;
				const bool bClosed = Mode < 2 && EndsPhrase(Base) && !EndsPhrase(Next);
				const bool bGap = IsGap(Base);
				if (bGap || bClosed || bLast)
				{
					Phrases.Add({From, Index + 1, bBreakNext});
					From = Index + 1;
					bBreakNext = (bGap && Mode < 2) || (Mode == 0 && EndsSentence(Base) && !EndsPhrase(Next));
				}
			}
			TArray<TArray<int32>> Rows;
			Rows.AddDefaulted();
			float X = 0.0f;
			for (const FPhrase& Phrase : Phrases)
			{
				// (The full-width space between two sentences: kept at the end of the row before, never a row of its own.)
				bool bOnlyGap = true;
				for (int32 Index = Phrase.From; Index < Phrase.To; ++Index)
				{
					bOnlyGap = bOnlyGap && IsGap(Tokens[Index].Base);
				}
				if (bOnlyGap && !Rows.Last().IsEmpty())
				{
					for (int32 Index = Phrase.From; Index < Phrase.To; ++Index)
					{
						Rows.Last().Add(Index);
					}
					continue;
				}
				float W = 0.0f;
				for (int32 Index = Phrase.From; Index < Phrase.To; ++Index)
				{
					W += IsGap(Tokens[Index].Base) ? 0.0f : Measure(Tokens[Index]);
				}
				if (!Rows.Last().IsEmpty() && (Phrase.bNewRow || X + W > Width))
				{
					Rows.AddDefaulted();
					X = 0.0f;
				}
				for (int32 Index = Phrase.From; Index < Phrase.To; ++Index)
				{
					const FToken& Token = Tokens[Index];
					const float TokenW = IsGap(Token.Base) ? 0.0f : Measure(Token);
					if (Rows.Last().IsEmpty() && IsGap(Token.Base))
					{
						Rows.Last().Add(Index);
						continue;
					}
					// (A phrase too long for a row on its own: broken where it must be, never before a closing mark.)
					if (X + TokenW > Width && !Rows.Last().IsEmpty() && !HangsBack(Token.Base))
					{
						Rows.AddDefaulted();
						X = 0.0f;
					}
					Rows.Last().Add(Index);
					X += TokenW;
				}
			}
			if (Rows.Num() <= 3 || Mode == 2)
			{
				return Rows;
			}
		}
		return {};
	}
}

AChaosImpactBossScene::AChaosImpactBossScene()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	EyeLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("EyeLight"));
	EyeLight->SetupAttachment(GetRootComponent());
	EyeLight->SetLightColor(FLinearColor(1.0f, 0.08f, 0.05f));
	EyeLight->SetIntensity(0.0f);
	EyeLight->SetAttenuationRadius(260.0f);
	EyeLight->SetCastShadows(false);
}

AChaosImpactBossScene* AChaosImpactBossScene::BeginPresident(AChaosImpactSoloRoom* InRoom, AChaosImpactCharacter* InHero, AChaosImpactPresidentBoss* InBoss,
	const bool bArrival)
{
	UWorld* World = InBoss ? InBoss->GetWorld() : nullptr;
	if (!World || !InHero)
	{
		return nullptr;
	}
	if (AChaosImpactBossScene* Old = GActiveScene.Get())
	{
		Old->Destroy();
	}
	AChaosImpactBossScene* Scene = World->SpawnActor<AChaosImpactBossScene>(InBoss->GetActorLocation(), FRotator::ZeroRotator);
	if (!Scene)
	{
		return nullptr;
	}
	GActiveScene = Scene;
	Scene->bPresident = true;
	Scene->Room = InRoom;
	Scene->Hero = InHero;
	Scene->President = InBoss;
	Scene->Hall = AChaosImpactWhiteHall::Find(World);
	Scene->BossHome = InBoss->GetActorLocation();
	Scene->BossHomeRotation = InBoss->GetActorRotation();
	InBoss->SetSceneDefeat(true);
	if (GEngine && GEngine->GameViewport)
	{
		Scene->Overlay = SNew(SChaosImpactSceneOverlay, Scene);
		GEngine->GameViewport->AddViewportWidgetContent(Scene->Overlay.ToSharedRef(), 900);
	}
	UE_LOG(LogChaosImpact, Log, TEXT("President scene: %s"), bArrival ? TEXT("arrival") : TEXT("the fight (seen before)"));
	if (bArrival)
	{
		Scene->Hold(true);
		InBoss->SetSceneHold(true);
		Scene->SetPart(EPart::Arrival);
	}
	else
	{
		// Seen before: it is down off its throne at once, in the arena.
		Scene->SkipArrivalPresident();
		const FVector Spot(AChaosImpactWhiteHall::DaisX - 1300.0f, 0.0f, InBoss->GetActorLocation().Z);
		const FVector Floor = ChaosImpactSimaeFx::FindGround(World, Spot, InBoss);
		InBoss->SetActorLocation(FVector(Spot.X, Spot.Y, Floor.Z + InBoss->GetSimpleCollisionHalfHeight() + 2.0f), false, nullptr, ETeleportType::TeleportPhysics);
		InBoss->SetActorRotation(FRotator(0.0f, 180.0f, 0.0f));
		InBoss->SetHover(120.0f, 0.3f);
		InHero->SetStageCameraScale(1.35f);
		if (AChaosImpactWhiteHall* Room30 = Scene->Hall.Get())
		{
			Room30->SetCinematic(false);
			Room30->SetChandelier(-1, 1.0f);
			Room30->SetMasterLight(1.0f);
		}
		Scene->SetPart(EPart::Fight);
	}
	return Scene;
}

bool AChaosImpactBossScene::IsEnding(const UWorld* World)
{
	const AChaosImpactBossScene* Scene = GetActive(World);
	return Scene && Scene->Part == EPart::Ending;
}

AActor* AChaosImpactBossScene::GetFoe() const
{
	return bPresident ? static_cast<AActor*>(President.Get()) : static_cast<AActor*>(Boss.Get());
}

bool AChaosImpactBossScene::IsFoeBeaten() const
{
	if (bPresident)
	{
		return President.IsValid() && President->IsBeaten();
	}
	return Boss.IsValid() && Boss->IsBeaten();
}

FString AChaosImpactBossScene::GetSpeakerName(const FLine& Line) const
{
	if (Line.Who == 1)
	{
		return TEXT("雑魚敵たち");
	}
	if (Line.Who == 2)
	{
		return TEXT("ヒーロー＆ドッチ・ボール大統領");
	}
	if (Line.bBoss)
	{
		return bPresident ? TEXT("ドッチ・ボール大統領") : TEXT("シマエナガ");
	}
	return TEXT("ヒーロー");
}

AChaosImpactBossScene* AChaosImpactBossScene::GetActive(const UWorld* World)
{
	AChaosImpactBossScene* Scene = GActiveScene.Get();
	return Scene && Scene->GetWorld() == World && Scene->Part != EPart::Done ? Scene : nullptr;
}

AChaosImpactBossScene* AChaosImpactBossScene::Begin(AChaosImpactSoloRoom* InRoom, AChaosImpactCharacter* InHero, AChaosImpactSimaeBoss* InBoss,
	const bool bArrival)
{
	UWorld* World = InBoss ? InBoss->GetWorld() : nullptr;
	if (!World || !InHero)
	{
		return nullptr;
	}
	if (AChaosImpactBossScene* Old = GActiveScene.Get())
	{
		Old->Destroy();
	}
	AChaosImpactBossScene* Scene = World->SpawnActor<AChaosImpactBossScene>(InBoss->GetActorLocation(), FRotator::ZeroRotator);
	if (!Scene)
	{
		return nullptr;
	}
	GActiveScene = Scene;
	Scene->Room = InRoom;
	Scene->Hero = InHero;
	Scene->Boss = InBoss;
	Scene->BossHome = InBoss->GetActorLocation();
	Scene->BossHomeRotation = InBoss->GetActorRotation();
	// (Its last hit leaves it beaten, for the words after the fight.)
	InBoss->SetSceneDefeat(true);
	if (GEngine && GEngine->GameViewport)
	{
		Scene->Overlay = SNew(SChaosImpactSceneOverlay, Scene);
		GEngine->GameViewport->AddViewportWidgetContent(Scene->Overlay.ToSharedRef(), 900);
	}
	UE_LOG(LogChaosImpact, Log, TEXT("Boss scene: %s"), bArrival ? TEXT("arrival") : TEXT("the fight (seen before)"));
	if (bArrival)
	{
		Scene->Hold(true);
		Scene->SetPart(EPart::Arrival);
	}
	else
	{
		Scene->SetPart(EPart::Fight);
	}
	return Scene;
}

void AChaosImpactBossScene::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Overlay.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(Overlay.ToSharedRef());
	}
	Overlay.Reset();
	if (Camera)
	{
		Camera->Destroy();
	}
	for (const TWeakObjectPtr<AChaosImpactSimaePerch>& Bird : Birds)
	{
		if (Bird.IsValid())
		{
			Bird->SetSceneHeld(false);
		}
	}
	ShowFlakes();
	ClearRunners();
	if (AChaosImpactSoloRoom* Arena = Room.Get())
	{
		Arena->SetDoorHiddenForScene(false);
	}
	if (AActor* Joke = Gag.Get())
	{
		Joke->Destroy();
	}
	if (AChaosImpactWhiteHall* Room30 = Hall.Get())
	{
		Room30->SetCinematic(false);
		Room30->SetMasterLight(1.0f);
		Room30->ShowSoldiers(true);
	}
	if (GetWorld())
	{
		UGameplayStatics::SetGlobalTimeDilation(this, 1.0f);
	}
	Super::EndPlay(EndPlayReason);
}

const TArray<AChaosImpactBossScene::FLine>& AChaosImpactBossScene::Lines() const
{
	// The script (シマエナガボス戦　前／後). {base|reading}: ruby over base.
	static const TArray<FLine> Before = {
		{true, TEXT("シーマシマシマシマシマ！　ついにここまでたどり着きやがったシマねぇ！？"), 1},
		{false, TEXT("大統領のところに行くにはここを通らなくちゃいけないんだ！　通してくれ！"), 0},
		{true, TEXT("黙れシマ！　ここはシマの縄張り……誰であろうと侵入者はシマエナガにしてやるシマ！"), 1},
		{false, TEXT("まさか……今まで戦ってきたシマエナガは……！"), 4},
		{true, TEXT("そうシマ！　それがお前のなれの果てシマァァァァ！！！"), 2},
	};
	static const TArray<FLine> After = {
		{true, TEXT("ぐわあああああ！　シマァァァァ！？　動物愛護団体が黙ってないシマよ！？"), 1},
		{false, TEXT("うるせぇ！　ボールを手にした時点で、お前はもう一人の“{避球者|カオススマッチャー}”だ。手加減するって方が失礼だろ……"), 0},
		{true, TEXT("……！　シマが、間違っていたシマ。そう、シマも一羽の“{避球者|カオススマッチャー}”……真剣な勝負、感謝するシマ"), 0},
		{true, TEXT("この先、通るといいシマ。シマが何をシマそうとしてるかはシマないシマが、シマくいくことをシマってるシマ"), 0},
		{false, TEXT("？"), 3},
	};
	// The President's (ラスボス戦　前／後).
	static const TArray<FLine> PresidentBefore = {
		{false, TEXT("ようやくたどり着いたぞ、ここが、ドッチ・ボール大統領の本拠地……“ホワイトボールハウス”か"), 0},
		{true, TEXT("来たな！　反逆者めが！　捕らえろ！"), 10},
		{false, TEXT("うおおおおお！"), 11, 1},
		{false, TEXT("邪魔だ！　今更お前らなんて敵じゃない！　くらえ！　“{混沌破壊滅殺放球|カオスデストロイスマッシュ}”ーーーーー！！！"), 12},
		{false, TEXT("きえええぇぇぇぇ"), 13, 1},
		{true, TEXT("あ、おま、なんだその技！？　そんなの撃ったことなかっただろ！"), 1},
		{false, TEXT("今、生み出した……お前を倒すために！"), 0},
		{true, TEXT("こいつ適当言いやがって！　クソ、私が直々に処刑球を渡してやる！"), 14},
		{false, TEXT("こいッ！　ドッチ・ボール大統領ッ！！！"), 15},
	};
	static const TArray<FLine> PresidentAfter = {
		{true, TEXT("見事だった……お前の、その{混沌|カオス}……いつしか私が忘れてしまった物、忘れてはいけなかったもの……それを思い出させてくれてありがとう"), 0},
		{false, TEXT("お前も……その{衝撃|インパクト}……素晴らしかった。今まで戦ってきたどの“{避球者|カオススマッチャー}”でも、ここまでの奴はいなかった"), 0},
		{true, TEXT("私はこの素晴らしい感動を、全世界に届けたい……それが、私の{原点|オリジン}だったはずなんだがな……どうして、こうなったのか……"), 0},
		{false, TEXT("――今からでも、間に合うんじゃないかな？"), 4},
		{true, TEXT("え？"), 16},
		{false, TEXT("お前はちゃんと、踏みとどまれた……サンダーボールをダッシュで避けた時みたいに、ギリギリに、な"), 0},
		{true, TEXT("――ああ、そうだ。そうだ、な！　ここから始めよう。私……いや、私たちの――"), 17},
		{false, TEXT("「{避球|カオスインパクト}を！」"), 18, 2},
	};
	if (bPresident)
	{
		return bAfter ? PresidentAfter : PresidentBefore;
	}
	return bAfter ? After : Before;
}

int32 AChaosImpactBossScene::GetLineCount() const
{
	return Lines().Num();
}

void AChaosImpactBossScene::SetPart(const EPart NewPart)
{
	Part = NewPart;
	PartTime = 0.0f;
	Cue = 0;
	UE_LOG(LogChaosImpact, Log, TEXT("Boss scene part %d"), static_cast<int32>(NewPart));
	if (NewPart == EPart::TalkBefore || NewPart == EPart::TalkAfter)
	{
		bAfter = NewPart == EPart::TalkAfter;
		PlaceTalk();
		StartLine(0);
	}
}

void AChaosImpactBossScene::Hold(const bool bHeld)
{
	if (AChaosImpactCharacter* Player = Hero.Get())
	{
		Player->SetTrainingMenuFrozen(bHeld);
	}
}

void AChaosImpactBossScene::SetCamera(const FVector& At, const FVector& LookAt, const float Fov, const float FocusBlur)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (!Camera)
	{
		Camera = World->SpawnActor<ACameraActor>(At, (LookAt - At).Rotation());
		if (!Camera)
		{
			return;
		}
		Camera->GetCameraComponent()->bConstrainAspectRatio = false;
		// (Its shots are framed across: on an upright phone the whole width of each shows, in a band between the bars.)
		Camera->GetCameraComponent()->bOverrideAspectRatioAxisConstraint = true;
		Camera->GetCameraComponent()->AspectRatioAxisConstraint = EAspectRatioAxisConstraint::AspectRatio_MaintainXFOV;
		if (APlayerController* PC = Hero.IsValid() ? Cast<APlayerController>(Hero->GetController()) : nullptr)
		{
			PC->SetViewTarget(Camera);
		}
	}
	CamAt = At;
	CamLook = LookAt;
	CamFov = Fov;
	FVector Jolt = FVector::ZeroVector;
	if (ShakeLeft > 0.0f)
	{
		const float Amount = ShakeStrength * (ShakeLeft / FMath::Max(ShakeTotal, 0.01f)) * 34.0f;
		const float Clock = static_cast<float>(FPlatformTime::Seconds());
		Jolt = FVector(FMath::Sin(Clock * 71.0f), FMath::Sin(Clock * 89.0f + 1.3f), FMath::Sin(Clock * 97.0f + 2.1f)) * Amount;
	}
	Camera->SetActorLocationAndRotation(At + Jolt, (LookAt - At).Rotation());
	UCameraComponent* Lens = Camera->GetCameraComponent();
	Lens->SetFieldOfView(Fov);
	// The far snow out of focus behind the two (a story game's talk).
	FPostProcessSettings& Post = Lens->PostProcessSettings;
	Lens->PostProcessBlendWeight = 1.0f;
	Post.bOverride_DepthOfFieldFocalDistance = FocusBlur > 0.0f;
	Post.bOverride_DepthOfFieldFstop = FocusBlur > 0.0f;
	Post.DepthOfFieldFocalDistance = static_cast<float>(FVector::Dist(At, LookAt));
	Post.DepthOfFieldFstop = FMath::Lerp(8.0f, 1.4f, FMath::Clamp(FocusBlur, 0.0f, 1.0f));
}

void AChaosImpactBossScene::Shake(const float Strength, const float Seconds)
{
	if (Strength >= ShakeStrength * (ShakeLeft / FMath::Max(ShakeTotal, 0.01f)))
	{
		ShakeStrength = Strength;
		ShakeLeft = Seconds;
		ShakeTotal = Seconds;
	}
}

void AChaosImpactBossScene::PlayFog(const FVector& Center, const FVector& CameraAt)
{
	UWorld* World = GetWorld();
	AChaosImpactFxKit* Fx = AChaosImpactFxKit::Spawn(this, Center);
	if (!Fx)
	{
		return;
	}
	FRandomStream Stream(1919);
	const FLinearColor Snow(0.84f, 0.89f, 0.97f);
	// The snow it threw up hangs as a fog round where it came down, a long while (it thins as it drifts out).
	for (int32 Index = 0; Index < 18; ++Index)
	{
		const float Angle = Stream.FRandRange(0.0f, UE_TWO_PI);
		const float Out = FMath::Sqrt(Stream.FRand()) * 600.0f;
		const FVector Way(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f);
		FChaosImpactFxPiece Puff;
		Puff.Look = EChaosImpactFxLook::Smoke;
		Puff.Location = Way * Out + FVector(0.0f, 0.0f, Stream.FRandRange(60.0f, 380.0f));
		Puff.Velocity = Way * Stream.FRandRange(40.0f, 120.0f) + FVector(0.0f, 0.0f, Stream.FRandRange(5.0f, 25.0f));
		Puff.Drag = 0.4f;
		Puff.Delay = Stream.FRandRange(0.0f, 0.2f);
		Puff.Life = Stream.FRandRange(9.0f, 10.5f);
		Puff.SizeFrom = Stream.FRandRange(500.0f, 800.0f);
		Puff.SizeTo = Stream.FRandRange(900.0f, 1250.0f);
		Puff.Color = Snow;
		Puff.CoreColor = FLinearColor(0.98f, 0.99f, 1.0f);
		Puff.Opacity = 0.4f;
		Puff.Attack = 0.02f;
		Puff.Hold = 0.55f;
		Puff.Spin = Stream.FRandRange(-0.15f, 0.15f);
		Fx->Add(Puff);
	}
	// A wall of it rushing out at the camera (it whites the screen out as it passes).
	const FVector Toward = (CameraAt - Center).GetSafeNormal();
	for (int32 Index = 0; Index < 12; ++Index)
	{
		const FVector Spread = Toward.RotateAngleAxis(Stream.FRandRange(-28.0f, 28.0f), FVector::UpVector);
		FChaosImpactFxPiece Rush;
		Rush.Look = EChaosImpactFxLook::Smoke;
		Rush.Location = FVector(0.0f, 0.0f, Stream.FRandRange(80.0f, 300.0f));
		Rush.Velocity = Spread * Stream.FRandRange(2200.0f, 3200.0f) + FVector(0.0f, 0.0f, Stream.FRandRange(0.0f, 200.0f));
		Rush.Drag = 1.1f;
		Rush.Delay = Stream.FRandRange(0.0f, 0.12f);
		Rush.Life = 2.4f;
		Rush.SizeFrom = 500.0f;
		Rush.SizeTo = 2200.0f;
		Rush.Color = Snow;
		Rush.CoreColor = FLinearColor::White;
		Rush.Opacity = 0.8f;
		Rush.Attack = 0.03f;
		Fx->Add(Rush);
	}
	for (int32 Index = 0; Index < 6; ++Index)
	{
		const float Angle = UE_TWO_PI * Index / 6.0f;
		ChaosImpactSimaeFx::PlaySnowDust(World, Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * 480.0f, 3.4f, 1.4f, 70.0f);
	}
}

void AChaosImpactBossScene::PlayFaceWisps(const FVector& Face, const FVector& Forward, const FVector& Across)
{
	// Wisps of the fog drifting across its face, in front of the camera.
	AChaosImpactFxKit* Fx = AChaosImpactFxKit::Spawn(this, Face + Forward * 170.0f);
	if (!Fx)
	{
		return;
	}
	FRandomStream Stream(77);
	for (int32 Index = 0; Index < 5; ++Index)
	{
		const float Side = Index % 2 == 0 ? 1.0f : -1.0f;
		FChaosImpactFxPiece Wisp;
		Wisp.Look = EChaosImpactFxLook::Smoke;
		Wisp.Location = Across * Side * Stream.FRandRange(40.0f, 160.0f) + FVector(0.0f, 0.0f, Stream.FRandRange(-60.0f, 40.0f))
			+ Forward * Stream.FRandRange(-30.0f, 60.0f);
		Wisp.Velocity = -Across * Side * Stream.FRandRange(60.0f, 130.0f);
		Wisp.Life = Stream.FRandRange(1.6f, 2.2f);
		Wisp.SizeFrom = Stream.FRandRange(180.0f, 260.0f);
		Wisp.SizeTo = Stream.FRandRange(380.0f, 520.0f);
		Wisp.Color = FLinearColor(0.84f, 0.89f, 0.97f);
		Wisp.CoreColor = FLinearColor::White;
		Wisp.Opacity = 0.34f;
		Wisp.Attack = 0.15f;
		Wisp.Delay = Index * 0.08f;
		Fx->Add(Wisp);
	}
}

void AChaosImpactBossScene::PlayFogParting(const FVector& At, const FVector& Dir, const FVector& Side)
{
	// The fog pushed aside to its left and right as it walks out of it.
	AChaosImpactFxKit* Fx = AChaosImpactFxKit::Spawn(this, At);
	if (!Fx)
	{
		return;
	}
	for (int32 Index = 0; Index < 12; ++Index)
	{
		const float Sign = Index < 6 ? -1.0f : 1.0f;
		const FVector Out = (Side * Sign + (-Dir) * (0.15f + 0.08f * (Index % 6))).GetSafeNormal();
		FChaosImpactFxPiece Puff;
		Puff.Look = EChaosImpactFxLook::Smoke;
		Puff.Location = FVector(0.0f, 0.0f, 100.0f + 45.0f * (Index % 4)) + Side * Sign * 60.0f;
		Puff.Velocity = Out * (380.0f + 30.0f * (Index % 5));
		Puff.Drag = 1.1f;
		Puff.Life = 3.0f;
		Puff.SizeFrom = 600.0f;
		Puff.SizeTo = 1150.0f;
		Puff.Color = FLinearColor(0.84f, 0.89f, 0.97f);
		Puff.CoreColor = FLinearColor::White;
		Puff.Opacity = 0.62f;
		Puff.Attack = 0.05f;
		Puff.Delay = (Index % 6) * 0.06f;
		Fx->Add(Puff);
	}
}

void AChaosImpactBossScene::ShowFlakes()
{
	for (const TWeakObjectPtr<AActor>& Flakes : HiddenFlakes)
	{
		if (Flakes.IsValid())
		{
			Flakes->SetActorHiddenInGame(false);
		}
	}
	HiddenFlakes.Reset();
}

void AChaosImpactBossScene::UpdateEyes(const float Glow)
{
	const AChaosImpactSimaeBoss* Monster = Boss.Get();
	if (!Monster)
	{
		return;
	}
	FVector Middle;
	FVector Across;
	Monster->GetEyes(Middle, Across);
	const FVector Half = Across.Size() > 1.5f ? Across : Across.GetSafeNormal() * 13.0f * Monster->BossScale;
	const FVector Seen = Camera ? Camera->GetActorLocation() : Middle + Monster->GetActorForwardVector() * 1000.0f;
	EyeLight->SetWorldLocation(Middle + (Seen - Middle).GetSafeNormal() * 40.0f);
	EyeLight->SetIntensity(Glow * 1600.0f);
	for (int32 Index = 0; Index < 2; ++Index)
	{
		AChaosImpactFxKit* Fx = EyeFx[Index].Get();
		if (Glow <= 0.01f)
		{
			if (Fx)
			{
				Fx->Destroy();
			}
			continue;
		}
		const FVector Eye = Middle + Half * (Index == 0 ? -1.0f : 1.0f);
		if (!Fx)
		{
			// A red point, a red glow round it and a thin red streak across it, glaring out of the fog.
			Fx = AChaosImpactFxKit::Spawn(this, Eye);
			EyeFx[Index] = Fx;
			if (!Fx)
			{
				continue;
			}
			const float EyeSize = FMath::Clamp(static_cast<float>(Half.Size()) * 0.55f, 9.0f, 20.0f);
			FChaosImpactFxPiece Point;
			Point.Look = EChaosImpactFxLook::Glow;
			Point.Life = 40.0f;
			Point.SizeFrom = Point.SizeTo = EyeSize;
			Point.Color = FLinearColor(1.0f, 0.04f, 0.03f);
			Point.CoreColor = FLinearColor(1.0f, 0.35f, 0.25f);
			Point.Intensity = 22.0f;
			Point.Attack = 0.004f;
			Point.Hold = 0.99f;
			Fx->Add(Point);
			FChaosImpactFxPiece Halo = Point;
			Halo.SizeFrom = Halo.SizeTo = EyeSize * 1.9f;
			Halo.Intensity = 1.8f;
			Halo.CoreColor = FLinearColor(1.0f, 0.2f, 0.1f);
			Fx->Add(Halo);
			FChaosImpactFxPiece Glare;
			Glare.Look = EChaosImpactFxLook::Streak;
			Glare.Velocity = Half.GetSafeNormal();
			Glare.Life = 40.0f;
			Glare.SizeFrom = Glare.SizeTo = EyeSize * 4.5f;
			Glare.Width = 0.05f;
			Glare.Color = FLinearColor(1.0f, 0.06f, 0.04f);
			Glare.CoreColor = FLinearColor(1.0f, 0.7f, 0.6f);
			Glare.Intensity = 6.0f;
			Glare.Attack = 0.004f;
			Glare.Hold = 0.99f;
			Fx->Add(Glare);
		}
		// (Toward the camera from the eye: drawn over its face, not hidden in its head.)
		Fx->SetActorLocation(Eye + (Seen - Eye).GetSafeNormal() * 14.0f * Monster->BossScale);
	}
}

void AChaosImpactBossScene::RequestSkip()
{
	switch (Part)
	{
	case EPart::Arrival:
		if (bPresident)
		{
			SkipArrivalPresident();
			SetPart(EPart::TalkBefore);
			break;
		}
		// The film skipped: everything where it ends (the boss stepped out of the fog), on to the talk.
		if (AChaosImpactSimaeBoss* Monster = Boss.Get())
		{
			Monster->SetSceneHold(true);
			Monster->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
			Monster->SetActorHiddenInGame(false);
			Monster->SetActorLocation(BossHome);
			Monster->PlaySceneClip(0, true, 1.0f);
			Monster->SetNetAuraTarget(0.0f);
		}
		for (const TWeakObjectPtr<AChaosImpactSimaePerch>& Bird : Birds)
		{
			if (Bird.IsValid())
			{
				Bird->SceneFlee(BossHome);
			}
		}
		UpdateEyes(0.0f);
		TitleTime = -1.0f;
		Black = 0.0f;
		White = 0.0f;
		Flash = 0.6f;
		Letterbox = 1.0f;
		SetPart(EPart::TalkBefore);
		break;
	case EPart::TalkBefore:
		SetPart(EPart::ToFight);
		break;
	case EPart::TalkAfter:
		SetPart(EPart::Leave);
		break;
	default:
		break;
	}
}

void AChaosImpactBossScene::RequestNext()
{
	if (Part != EPart::TalkBefore && Part != EPart::TalkAfter)
	{
		return;
	}
	if (LineIndex < 0 || LineIndex >= Lines().Num())
	{
		return;
	}
	const int32 Length = BaseLength(Tokenize(Lines()[LineIndex].Text));
	if (Typed < Length)
	{
		// The rest of the line at once.
		Typed = static_cast<float>(Length);
		return;
	}
	ChaosImpactSfx::Play2D(this, EChaosImpactSfx::TalkNext);
	if (LineIndex + 1 < Lines().Num())
	{
		StartLine(LineIndex + 1);
	}
	else
	{
		SetPart(Part == EPart::TalkBefore ? EPart::ToFight : EPart::Leave);
	}
}

void AChaosImpactBossScene::StartLine(const int32 Index)
{
	LineIndex = Index;
	LineTime = 0.0f;
	Typed = 0.0f;
	BlipAt = 0;
	EmoteTime = 0.0f;
	if (!Lines().IsValidIndex(Index))
	{
		return;
	}
	const FLine& Line = Lines()[Index];
	if (bPresident)
	{
		StartLinePresident(Line);
		return;
	}
	AChaosImpactSimaeBoss* Monster = Boss.Get();
	switch (Line.Emote)
	{
	case 1:
		Shake(0.35f, 0.45f);
		break;
	case 2:
		// The war cry: it roars, burning, the screen shaking white.
		Shake(1.0f, 1.4f);
		Flash = FMath::Max(Flash, 0.55f);
		ChaosImpactSfx::Play2D(this, EChaosImpactSfx::SceneRoar);
		if (Monster)
		{
			Monster->PlaySceneClip(5, false, 1.0f);
			Monster->SetNetAuraTarget(1.0f);
			ChaosImpactSimaeFx::PlaySnowBurst(GetWorld(), ChaosImpactSimaeFx::FindGround(GetWorld(), Monster->GetActorLocation(), Monster), 1.4f);
		}
		break;
	default:
		break;
	}
}

void AChaosImpactBossScene::PlaceTalk()
{
	AChaosImpactCharacter* Player = Hero.Get();
	AActor* Monster = GetFoe();
	if (!Player || !Monster)
	{
		return;
	}
	if (bPresident && !bAfter)
	{
		// (The words before the President's fight have their own shots: the hero on the carpet, it on its throne.)
		Letterbox = 1.0f;
		return;
	}
	FVector B = Monster->GetActorLocation();
	FVector H = Player->GetActorLocation();
	FVector Dir = (B - H).GetSafeNormal2D();
	if (Dir.IsNearlyZero())
	{
		Dir = Monster->GetActorForwardVector() * -1.0f;
	}
	// The two facing each other, a good talking distance apart (the hero brought there if not).
	const float Apart = FMath::Clamp(static_cast<float>(FVector::Dist2D(B, H)), 650.0f, 950.0f);
	if (FMath::Abs(FVector::Dist2D(B, H) - Apart) > 5.0f)
	{
		const FVector Ground = ChaosImpactSimaeFx::FindGround(GetWorld(), B - Dir * Apart, Monster);
		H = FVector(Ground.X, Ground.Y, Ground.Z + Player->GetDefaultHalfHeight() + 2.0f);
		Player->TeleportTo(H, Dir.Rotation());
	}
	Player->SetActorRotation(FRotator(0.0f, Dir.Rotation().Yaw, 0.0f));
	Monster->SetActorRotation(FRotator(0.0f, (-Dir).Rotation().Yaw, 0.0f));
	if (AChaosImpactSimaeBoss* Simae = Boss.Get())
	{
		Simae->PlaySceneClip(0, true, bAfter ? 0.6f : 1.0f);
	}
	if (AChaosImpactPresidentBoss* Pres = President.Get())
	{
		Pres->PlaySceneClip(0, true, 0.6f);
	}
	// The camera at their side (the side the game's camera was on), level with them; the hero to the left, the boss right.
	FVector Side = FVector::CrossProduct(FVector::UpVector, Dir).GetSafeNormal();
	if (const APlayerController* PC = Cast<APlayerController>(Player->GetController()); PC && PC->PlayerCameraManager)
	{
		const FVector Seen = PC->PlayerCameraManager->GetCameraLocation();
		if (FVector::DotProduct(Seen - H, Side) < 0.0f)
		{
			Side = -Side;
		}
	}
	TalkSide = Side;
	const FVector Mid = H + Dir * (Apart * 0.49f);
	// Far enough out to the side that both are whole in the frame (the boss is more than twice the hero's height), on the
	// side with the more room, and never inside the snow banks or the trees: short of whatever is in the way.
	TalkLook = Mid + FVector::UpVector * 105.0f;
	const float Want = Apart * 1.6f + 820.0f;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(BossSceneTalkCamera), false);
	Query.AddIgnoredActor(Player);
	Query.AddIgnoredActor(Monster);
	const auto FreeRoom = [&](const FVector& Way)
	{
		FHitResult Hit;
		const FVector To = TalkLook + (Way * Want - Dir * 120.0f + FVector::UpVector * -30.0f);
		if (GetWorld()->SweepSingleByChannel(Hit, TalkLook, To, FQuat::Identity, ECC_Camera, FCollisionShape::MakeSphere(40.0f), Query))
		{
			return FMath::Max(static_cast<float>(Hit.Distance) - 80.0f, 300.0f);
		}
		return Want;
	};
	const float Here = FreeRoom(Side);
	const float There = FreeRoom(-Side);
	if (There > Here + 50.0f)
	{
		Side = -Side;
		TalkSide = Side;
	}
	const float Out = FMath::Max(Here, There);
	TalkCamera = TalkLook + Side * Out - Dir * 120.0f * (Out / Want) - FVector::UpVector * 30.0f;
	HeroAt = H;
	Letterbox = 1.0f;
	SetCamera(TalkCamera, TalkLook, 52.0f, 1.0f);
}

void AChaosImpactBossScene::ReadInput()
{
	APlayerController* PC = Hero.IsValid() ? Cast<APlayerController>(Hero->GetController()) : nullptr;
	if (!PC || !IsTakingInput())
	{
		return;
	}
	const EChaosImpactDevice Device = ChaosImpactHints::GetDevice(PC);
	const bool bNext = PC->WasInputKeyJustPressed(EKeys::Enter) || PC->WasInputKeyJustPressed(EKeys::SpaceBar)
		|| PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Bottom) || PC->WasInputKeyJustPressed(ChaosImpactHints::ConfirmKey(Device));
	// A click or a tap: on the skip button skips, anywhere else goes on.
	FVector2D Viewport(1.0, 1.0);
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->GetViewportSize(Viewport);
	}
	bool bPress = false;
	FVector2D At = FVector2D::ZeroVector;
	float X = 0.0f;
	float Y = 0.0f;
	bool bDown = false;
	PC->GetInputTouchState(ETouchIndex::Touch1, X, Y, bDown);
	// (A quick tap can go down and up between two frames: seen only as pressed.)
	if ((bDown && !bTouchWasDown) || (!bDown && !bTouchWasDown && PC->WasInputKeyJustPressed(EKeys::TouchKeys[ETouchIndex::Touch1])))
	{
		bPress = true;
		At = FVector2D(X, Y) / Viewport;
	}
	bTouchWasDown = bDown;
	if (!bPress && PC->WasInputKeyJustPressed(EKeys::LeftMouseButton) && PC->GetMousePosition(X, Y))
	{
		bPress = true;
		At = FVector2D(X, Y) / Viewport;
	}
	if (bPress && SkipButton.bIsValid && SkipButton.IsInside(At))
	{
		RequestSkip();
		return;
	}
	if (bNext || bPress)
	{
		RequestNext();
	}
}

void AChaosImpactBossScene::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// Real seconds (the scene's own slow motion does not slow its clock).
	const float Step = FMath::Min(static_cast<float>(FApp::GetDeltaTime()), 0.1f);
	PartTime += Step;
	ShakeLeft = FMath::Max(ShakeLeft - Step, 0.0f);
	Flash = FMath::Max(Flash - Step * 2.6f, 0.0f);
	if (TitleTime >= 0.0f)
	{
		TitleTime += Step;
	}
	AActor* Monster = GetFoe();
	if (!Monster && Part != EPart::Done && Part != EPart::Ending)
	{
		// The boss gone under it (the room started over after a knockout): nothing left to show.
		if (Part != EPart::Fight)
		{
			Hold(false);
			if (AChaosImpactCharacter* Player = Hero.Get())
			{
				Player->SetSceneHudShown(1.0f);
				if (APlayerController* PC = Cast<APlayerController>(Player->GetController()))
				{
					PC->SetViewTarget(Player);
				}
			}
		}
		Finish();
		return;
	}
	ReadInput();
	switch (Part)
	{
	case EPart::Arrival:
		if (bPresident)
		{
			TickArrivalPresident(PartTime);
		}
		else
		{
			TickArrival(PartTime);
		}
		break;
	case EPart::TalkBefore: case EPart::TalkAfter: TickTalk(Step); break;
	case EPart::ToFight:
		if (bPresident)
		{
			TickToFightPresident(PartTime);
		}
		else
		{
			TickToFight(PartTime);
		}
		break;
	case EPart::Ending: TickEnding(Step); break;
	case EPart::Fight:
		if (IsFoeBeaten())
		{
			SetPart(EPart::Beaten);
		}
		break;
	case EPart::Beaten: TickBeaten(PartTime); break;
	case EPart::Leave: TickLeave(PartTime); break;
	default: break;
	}
	if (AChaosImpactCharacter* Player = Hero.Get())
	{
		Player->SetSceneHudShown(HudShown);
	}
}

void AChaosImpactBossScene::TickArrival(const float T)
{
	AChaosImpactSimaeBoss* Monster = Boss.Get();
	AChaosImpactCharacter* Player = Hero.Get();
	UWorld* World = GetWorld();
	if (!Monster || !Player || !World)
	{
		return;
	}
#if !UE_BUILD_SHIPPING
	static float LastLog = -1.0f;
	if (FMath::FloorToInt(T * 2.0f) != FMath::FloorToInt(LastLog * 2.0f))
	{
		const APlayerController* PC = Cast<APlayerController>(Player->GetController());
		UE_LOG(LogChaosImpact, Log, TEXT("SCENEDBG t=%.2f cam=%s look=%s view=%s viewtarget=%s boss=%s hidden=%d"), T, *CamAt.ToCompactString(),
			*CamLook.ToCompactString(), PC && PC->PlayerCameraManager ? *PC->PlayerCameraManager->GetCameraLocation().ToCompactString() : TEXT("-"),
			PC ? *GetNameSafe(PC->GetViewTarget()) : TEXT("-"), *Monster->GetActorLocation().ToCompactString(), Monster->IsHidden() ? 1 : 0);
	}
	LastLog = T;
#endif
	// The film, as the reference's pacing (赤鬼登場シーン): a long calm, the little ones startled, it drops out of the sky, the
	// snow it throws up whites everything out; a long wait in the fog until red eyes show far off; its eyes up close; it
	// steps out; it roars at the sky, and only then its name.
	const FVector Ground = ChaosImpactSimaeFx::FindGround(World, BossHome, Monster);
	FVector Dir = (BossHome - HeroAt).GetSafeNormal2D();
	if (Dir.IsNearlyZero())
	{
		Dir = FVector::ForwardVector;
	}
	const FVector Side = FVector::CrossProduct(FVector::UpVector, Dir).GetSafeNormal();
	const float Scale = Monster->BossScale / 2.25f;
	const FVector Face = BossHome - Dir * 380.0f + FVector(0.0f, 0.0f, 330.0f * Scale);
	Black = T < 0.4f ? Smooth(T / 0.4f) : 1.0f - Smooth(Window(T, 0.4f, 1.6f));
	HudShown = 1.0f - Smooth(T / 0.4f);
	if (Cue == 0 && T >= 0.4f)
	{
		++Cue;
		// In the dark: the boss taken up out of sight (held there, no falling), the little ones on the snow held to the film,
		// the drifting flakes (dark specks against the sky up close) put away.
		HeroAt = Player->GetActorLocation();
		Dir = (BossHome - HeroAt).GetSafeNormal2D();
		Monster->SetSceneHold(true);
		Monster->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
		Monster->GetCharacterMovement()->StopMovementImmediately();
		Monster->SetActorHiddenInGame(true);
		Monster->SetActorLocation(BossHome + FVector(0.0f, 0.0f, 5200.0f));
		Monster->SetActorRotation(FRotator(0.0f, (-Dir).Rotation().Yaw, 0.0f));
		Player->SetActorRotation(FRotator(0.0f, Dir.Rotation().Yaw, 0.0f));
		Birds.Reset();
		for (TActorIterator<AChaosImpactSimaePerch> It(World); It; ++It)
		{
			if (FVector::Dist2D(It->GetActorLocation(), BossHome) < 2600.0f && FMath::Abs(It->GetActorLocation().Z - BossHome.Z) < 700.0f)
			{
				It->SetSceneHeld(true);
				Birds.Add(*It);
			}
		}
		Birds.Sort([this](const TWeakObjectPtr<AChaosImpactSimaePerch>& A, const TWeakObjectPtr<AChaosImpactSimaePerch>& B)
		{
			return A.IsValid() && B.IsValid()
				&& FVector::DistSquared2D(A->GetActorLocation(), BossHome) < FVector::DistSquared2D(B->GetActorLocation(), BossHome);
		});
		for (TActorIterator<AChaosImpactDriftingFlakes> It(World); It; ++It)
		{
			if (!It->IsHidden())
			{
				It->SetActorHiddenInGame(true);
				HiddenFlakes.Add(*It);
			}
		}
		Letterbox = 1.0f;
	}
	// ---- Shot A (0.4-5.4): a calm, low on the snow by the little ones; they look up, the camera with them, at a speck coming.
	if (T >= 0.4f && T < 5.4f)
	{
		FVector Spot = BossHome - Dir * 1300.0f;
		if (Birds.Num() > 0 && Birds[0].IsValid())
		{
			Spot = Birds[0]->GetActorLocation();
		}
		const FVector Back = (Spot - BossHome).GetSafeNormal2D();
		const FVector Across = FVector::CrossProduct(FVector::UpVector, Back).GetSafeNormal();
		const float Calm = Smooth(Window(T, 0.4f, 5.4f));
		const FVector From = Spot + Back * 260.0f + Across * FMath::Lerp(-120.0f, 60.0f, Calm) + FVector(0.0f, 0.0f, 30.0f);
		const float Up = Smooth(Window(T, 4.2f, 5.1f));
		// Up at the sky, then onto it as it comes (closing in through a long lens).
		const FVector Sky = T >= 4.5f ? Monster->GetActorLocation() : BossHome + FVector(0.0f, 0.0f, 4200.0f);
		const FVector Look = FMath::Lerp(Spot + FVector(0.0f, 0.0f, 25.0f) - Back * 420.0f, Sky, Up);
		SetCamera(From, Look, FMath::Lerp(50.0f, 20.0f, Smooth(Window(T, 4.6f, 5.3f))));
	}
	if (Cue == 1 && T >= 3.2f)
	{
		++Cue;
		ChaosImpactSfx::Play2D(this, EChaosImpactSfx::SceneRumble);
		Shake(0.1f, 2.4f);
	}
	if (Cue == 2 && T >= 4.2f)
	{
		++Cue;
		// They have seen it.
		for (const TWeakObjectPtr<AChaosImpactSimaePerch>& Bird : Birds)
		{
			if (Bird.IsValid())
			{
				Bird->SceneAlert();
			}
		}
	}
	if (Cue == 3 && T >= 4.5f)
	{
		++Cue;
		Monster->SetActorHiddenInGame(false);
		Monster->PlaySceneClip(2, true, 0.7f);
	}
	if (T >= 4.5f && T < 5.4f)
	{
		// A speck high up, coming down.
		const float U = Window(T, 4.5f, 5.4f);
		Monster->SetActorLocation(BossHome + FVector(0.0f, 0.0f, FMath::Lerp(5200.0f, 3300.0f, U)));
	}
	if (Cue == 4 && T >= 4.9f)
	{
		++Cue;
		ChaosImpactSfx::Play2D(this, EChaosImpactSfx::SceneFall);
	}
	if (Cue == 5 && T >= 5.15f)
	{
		++Cue;
		// Away, all of them, before it comes down.
		for (const TWeakObjectPtr<AChaosImpactSimaePerch>& Bird : Birds)
		{
			if (Bird.IsValid())
			{
				Bird->SceneFlee(BossHome);
			}
		}
	}
	if (!bSlamStarted && T >= 5.34f)
	{
		// Its fist-slam (the one it fights with), timed so the fist meets the snow as it lands.
		bSlamStarted = true;
		Monster->PlaySceneClip(3, false, 1.6f);
	}
	// ---- Shot B (5.4-6.7): wide and low; it plunges down and crashes; the snow it throws up rushes at the camera.
	if (T >= 5.4f && T < 6.7f)
	{
		if (Cue == 6)
		{
			++Cue;
			ChaosImpactSfx::Play2D(this, EChaosImpactSfx::SceneWhoosh);
		}
		const float U = Window(T, 5.4f, 5.95f);
		if (T < 5.95f)
		{
			Monster->SetActorLocation(BossHome + FVector(0.0f, 0.0f, 3300.0f * (1.0f - FMath::Pow(U, 2.4f))));
		}
		const float Settle = Smooth(Window(T, 5.95f, 6.6f));
		// (Inside the arena: further back the camera would be in the snow banks round it.)
		// (A long lens on it while it is high up, opening wide as it comes down.)
		SetCamera(BossHome - Dir * 1350.0f + Side * 420.0f + FVector(0.0f, 0.0f, 50.0f),
			FMath::Lerp(Monster->GetActorLocation() + FVector(0.0f, 0.0f, 200.0f), BossHome + FVector(0.0f, 0.0f, 260.0f), Settle),
			FMath::Lerp(22.0f, 70.0f, FMath::Pow(U, 1.6f)));
	}
	if (Cue == 7 && T >= 5.95f)
	{
		++Cue;
		Monster->SetActorLocation(BossHome);
		Monster->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		ChaosImpactSfx::Play2D(this, EChaosImpactSfx::SceneLand);
		AChaosImpactSimaeBlast::Play(World, Ground, 1300.0f, 0.0f, true, 20);
		ChaosImpactSimaeFx::PlaySnowBurst(World, Ground, 2.6f);
		PlayFog(Ground, BossHome - Dir * 1350.0f);
		Shake(1.0f, 1.2f);
		Flash = 0.9f;
	}
	// The snow cloud reaching the camera: all white for a moment.
	White = FMath::Max(Smooth(Window(T, 6.15f, 6.6f)) * (1.0f - Smooth(Window(T, 6.75f, 7.9f))), 0.0f);
	// ---- Shot C (6.7-11.4): from far off, the fog settling over the arena; quiet; red eyes light far off in it.
	if (T >= 6.7f && T < 11.4f)
	{
		if (Cue == 8)
		{
			++Cue;
			Monster->PlaySceneClip(0, true, 0.7f);
			Monster->SetActorLocation(BossHome);
		}
		const float Drift = Smooth(Window(T, 6.7f, 11.4f));
		SetCamera(BossHome - Dir * FMath::Lerp(1550.0f, 1380.0f, Drift) + Side * 140.0f + FVector(0.0f, 0.0f, 170.0f),
			BossHome + FVector(0.0f, 0.0f, 250.0f * Scale), 40.0f);
	}
	if (Cue == 9 && T >= 8.6f)
	{
		++Cue;
		ChaosImpactSfx::Play2D(this, EChaosImpactSfx::SceneGlint);
	}
	if (T >= 8.6f)
	{
		// (A slow throb, as something breathing.)
		const float Throb = 0.85f + 0.15f * FMath::Sin((T - 8.6f) * 3.2f);
		UpdateEyes(FMath::Min(Window(T, 8.6f, 8.8f), 1.0f - Window(T, 19.6f, 20.6f)) * Throb);
	}
	// ---- Shot D (11.4-13.2): its eyes up close, fog drifting across its face, thinning.
	if (T >= 11.4f && T < 13.2f)
	{
		if (Cue == 10)
		{
			++Cue;
			ChaosImpactSfx::Play2D(this, EChaosImpactSfx::SceneWhoosh);
			FVector Middle;
			FVector Across;
			Monster->GetEyes(Middle, Across);
			PlayFaceWisps(Middle, Monster->GetActorForwardVector(), Across);
		}
		FVector Middle;
		FVector Across;
		Monster->GetEyes(Middle, Across);
		const float Push = Smooth(Window(T, 11.4f, 13.2f));
		const FVector Out = Monster->GetActorForwardVector();
		SetCamera(Middle + Out * FMath::Lerp(420.0f, 330.0f, Push) + FVector(0.0f, 0.0f, -20.0f), Middle, 34.0f);
	}
	// ---- Shot E (13.2-15.4): it steps out of the fog toward the camera.
	if (Cue == 11 && T >= 13.2f)
	{
		++Cue;
		Monster->PlaySceneClip(1, true, 0.9f);
		PlayFogParting(BossHome - Dir * 250.0f, Dir, Side);
	}
	if (T >= 13.2f && T < 15.4f)
	{
		const float Walk = Smooth(Window(T, 13.2f, 15.1f));
		Monster->SetActorLocation(BossHome - Dir * 380.0f * Walk);
		const FVector Now = Monster->GetActorLocation();
		SetCamera(Now - Dir * 1150.0f + Side * 260.0f + FVector(0.0f, 0.0f, 150.0f), Now + FVector(0.0f, 0.0f, 330.0f * Scale), 46.0f);
	}
	// ---- Shot F (15.4-20.6): looking up at it against the sky; it roars at the sky; the speed lines and its name with the roar.
	if (T >= 15.4f)
	{
		const FVector Now = BossHome - Dir * 380.0f;
		if (Cue == 12)
		{
			++Cue;
			Monster->SetActorLocation(Now);
			Monster->PlaySceneClip(0, true, 1.0f);
			ChaosImpactSfx::Play2D(this, EChaosImpactSfx::SceneWhoosh);
		}
		const float Drift = Smooth(Window(T, 15.4f, 20.6f));
		// (Low, close to the snow, looking up at the whole of it with the sky behind.)
		// (In close: its roaring upper half filling the screen, the sky behind; its name under it.)
		SetCamera(Now - Dir * FMath::Lerp(470.0f, 400.0f, Drift) + Side * FMath::Lerp(150.0f, 100.0f, Drift) + FVector(0.0f, 0.0f, -60.0f),
			Now + FVector(0.0f, 0.0f, FMath::Lerp(125.0f, 160.0f, Drift) * Scale), 64.0f);
	}
	if (Cue == 13 && T >= 15.7f)
	{
		++Cue;
		Monster->PlaySceneClip(5, false, 1.0f);
		Monster->SetNetAuraTarget(1.0f);
	}
	if (Cue == 14 && T >= 16.2f)
	{
		++Cue;
		// The roar: the speed lines and its name with it.
		ChaosImpactSfx::Play2D(this, EChaosImpactSfx::SceneRoar);
		Shake(1.2f, 2.0f);
		ChaosImpactSimaeFx::PlaySnowBurst(World, ChaosImpactSimaeFx::FindGround(World, Monster->GetActorLocation(), Monster), 1.8f);
		AChaosImpactSimaeBlast::Play(World, ChaosImpactSimaeFx::FindGround(World, Monster->GetActorLocation(), Monster), 900.0f, 0.35f, false, 8);
		TitleTime = 0.0f;
		Flash = FMath::Max(Flash, 0.7f);
		ChaosImpactSfx::Play2D(this, EChaosImpactSfx::SceneTitle);
	}
	if (T >= 20.6f)
	{
		Monster->SetNetAuraTarget(0.0f);
		BossHome = Monster->GetActorLocation();
		TitleTime = -1.0f;
		White = 0.0f;
		UpdateEyes(0.0f);
		ChaosImpactSfx::Play2D(this, EChaosImpactSfx::SceneWhoosh);
		SetPart(EPart::TalkBefore);
	}
}

void AChaosImpactBossScene::TickTalk(const float DeltaSeconds)
{
	BoxShown = FMath::Min(BoxShown + DeltaSeconds / 0.25f, 1.0f);
	if (Part == EPart::TalkBefore || Part == EPart::TalkAfter)
	{
		Black = FMath::Max(Black - DeltaSeconds / 0.4f, 0.0f);
	}
	if (!Lines().IsValidIndex(LineIndex))
	{
		return;
	}
	LineTime += DeltaSeconds;
	EmoteTime += DeltaSeconds;
	const FLine& Line = Lines()[LineIndex];
	const TArray<FToken> Tokens = Tokenize(Line.Text);
	const int32 Length = BaseLength(Tokens);
	// Typed out, pausing a moment after each mark; a soft tick every other letter.
	if (Typed < Length && BoxShown >= 1.0f)
	{
		// (The last letter out a mark: the next comes after a beat.)
		int32 End = 0;
		float Pause = 0.0f;
		for (const FToken& Token : Tokens)
		{
			End += Token.Base.Len();
			if (End == static_cast<int32>(Typed))
			{
				Pause = PauseAfter(Token.Base);
				break;
			}
			if (End > static_cast<int32>(Typed))
			{
				break;
			}
		}
		const float Rate = Pause > 0.0f ? 1.0f / (Pause + 1.0f / TypeRate) : TypeRate;
		Typed = FMath::Min(Typed + DeltaSeconds * Rate, static_cast<float>(Length));
		const int32 Shown = static_cast<int32>(Typed);
		if (Shown >= BlipAt + 2)
		{
			BlipAt = Shown;
			ChaosImpactSfx::Play2D(this, EChaosImpactSfx::TalkBlip, 0.6f);
		}
	}
	if (bPresident)
	{
		TickTalkPresident(DeltaSeconds);
		return;
	}
	// The camera leans a little toward whoever speaks; a puzzled ? pushes in on the hero; a shocked one closes in slowly.
	const AChaosImpactSimaeBoss* Monster = Boss.Get();
	const AChaosImpactCharacter* Player = Hero.Get();
	if (!Monster || !Player)
	{
		return;
	}
	const FVector Speaker = Line.bBoss ? Monster->GetActorLocation() + FVector(0.0f, 0.0f, 120.0f) : Player->GetActorLocation();
	const float Lean = Smooth(FMath::Min(LineTime / 0.6f, 1.0f));
	FVector Look = FMath::Lerp(TalkLook, FMath::Lerp(TalkLook, Speaker + FVector(0.0f, 0.0f, 60.0f), 0.22f), Lean);
	FVector At = TalkCamera;
	float Fov = 52.0f;
	if (Line.Emote == 3)
	{
		// ?: a quick push onto the hero.
		const float Push = ChaosImpactEase::Out(FMath::Min(EmoteTime / 0.18f, 1.0f));
		At = FMath::Lerp(TalkCamera, Player->GetActorLocation() + TalkSide * 420.0f + FVector(0.0f, 0.0f, 60.0f), 0.55f * Push);
		Look = FMath::Lerp(Look, Player->GetActorLocation() + FVector(0.0f, 0.0f, 50.0f), Push);
		Fov = FMath::Lerp(52.0f, 32.0f, Push);
	}
	else if (Line.Emote == 4)
	{
		// まさか……: slowly closing in on the hero.
		const float Push = Smooth(FMath::Min(EmoteTime / 3.0f, 1.0f));
		At = FMath::Lerp(TalkCamera, Player->GetActorLocation() + TalkSide * 520.0f + FVector(0.0f, 0.0f, 80.0f), 0.4f * Push);
		Look = FMath::Lerp(Look, Player->GetActorLocation() + FVector(0.0f, 0.0f, 60.0f), 0.6f * Push);
	}
	else if (Line.Emote == 2)
	{
		// The war cry: a hard push in on it, a little from below, the whole of it burning in the frame.
		const float Push = ChaosImpactEase::Out(FMath::Min(EmoteTime / 0.35f, 1.0f));
		const float Tall = Monster->GetSimpleCollisionHalfHeight();
		const FVector Body = Monster->GetActorLocation() + FVector(0.0f, 0.0f, Tall * 0.25f);
		const FVector Toward = (TalkCamera - Body).GetSafeNormal2D();
		At = FMath::Lerp(TalkCamera, Body + Toward * (Tall * 4.2f + 380.0f) + FVector(0.0f, 0.0f, -Tall * 0.45f), Push);
		Look = FMath::Lerp(Look, Body, Push);
		Fov = FMath::Lerp(52.0f, 50.0f, Push);
	}
	SetCamera(At, Look, Fov, 1.0f);
}

void AChaosImpactBossScene::TickToFight(const float T)
{
	BoxShown = FMath::Max(1.0f - T / 0.25f, 0.0f);
	Letterbox = 1.0f - Smooth(Window(T, 0.25f, 0.75f));
	HudShown = Smooth(Window(T, 0.6f, 1.2f));
	if (AChaosImpactSimaeBoss* Monster = Boss.Get())
	{
		Monster->SetNetAuraTarget(0.0f);
	}
	if (Cue == 0 && T >= 0.3f)
	{
		++Cue;
		if (AChaosImpactCharacter* Player = Hero.Get())
		{
			if (APlayerController* PC = Cast<APlayerController>(Player->GetController()))
			{
				PC->SetViewTargetWithBlend(Player, 0.8f, VTBlend_Cubic);
			}
		}
	}
	if (Cue == 1 && T >= 1.1f)
	{
		++Cue;
		// The fight.
		Hold(false);
		if (AChaosImpactSimaeBoss* Monster = Boss.Get())
		{
			Monster->SetSceneHold(false);
		}
		if (AChaosImpactSoloRoom* Arena = Room.Get())
		{
			Arena->WakeBossAfterScene();
		}
		if (Camera)
		{
			Camera->Destroy();
			Camera = nullptr;
		}
		LineIndex = -1;
		ShowFlakes();
		SetPart(EPart::Fight);
	}
}

void AChaosImpactBossScene::TickBeaten(const float T)
{
	AChaosImpactSimaeBoss* Monster = Boss.Get();
	if (Cue == 0)
	{
		++Cue;
		// The last hit: everything slows for a moment, the screen flashing white.
		Hold(true);
		Flash = 0.9f;
		UGameplayStatics::SetGlobalTimeDilation(this, 0.25f);
	}
	HudShown = 1.0f - Smooth(Window(T, 0.3f, 0.8f));
	if (Cue == 1 && T >= 1.1f)
	{
		++Cue;
		UGameplayStatics::SetGlobalTimeDilation(this, 1.0f);
	}
	Black = Smooth(Window(T, 1.5f, 1.85f));
	if (Cue == 2 && T >= 1.85f)
	{
		++Cue;
		if (Monster)
		{
			Monster->SetSceneHold(true);
		}
		if (AChaosImpactPresidentBoss* Pres = President.Get())
		{
			Pres->SetSceneHold(true);
		}
		SetPart(EPart::TalkAfter);
	}
}

void AChaosImpactBossScene::TickLeave(const float T)
{
	if (bPresident)
	{
		// The President stays: the ending.
		SetPart(EPart::Ending);
		return;
	}
	AChaosImpactSimaeBoss* Monster = Boss.Get();
	BoxShown = FMath::Max(1.0f - T / 0.25f, 0.0f);
	if (!Monster)
	{
		return;
	}
	if (Cue == 0 && T >= 0.35f)
	{
		++Cue;
		// Off it flies.
		ChaosImpactSfx::Play2D(this, EChaosImpactSfx::SceneFlee);
		Monster->PlaySceneClip(2, true, 1.2f);
		ChaosImpactSimaeFx::PlaySnowBurst(GetWorld(), ChaosImpactSimaeFx::FindGround(GetWorld(), Monster->GetActorLocation(), Monster), 1.6f);
		BossHome = Monster->GetActorLocation();
	}
	if (T >= 0.35f)
	{
		const float U = Window(T, 0.35f, 1.9f);
		Monster->SetActorLocation(BossHome + FVector(0.0f, 0.0f, 4200.0f * FMath::Pow(U, 2.0f)));
		SetCamera(TalkCamera, FMath::Lerp(TalkLook, Monster->GetActorLocation() + FVector(0.0f, 0.0f, 300.0f), Smooth(Window(T, 0.35f, 1.2f))),
			40.0f, 1.0f);
	}
	if (Cue == 1 && T >= 1.9f)
	{
		++Cue;
		Monster->SetActorHiddenInGame(true);
	}
	if (Cue == 2 && T >= 2.4f)
	{
		++Cue;
		if (AChaosImpactCharacter* Player = Hero.Get())
		{
			if (APlayerController* PC = Cast<APlayerController>(Player->GetController()))
			{
				PC->SetViewTargetWithBlend(Player, 0.8f, VTBlend_Cubic);
			}
		}
	}
	Letterbox = 1.0f - Smooth(Window(T, 2.4f, 2.9f));
	HudShown = Smooth(Window(T, 2.7f, 3.2f));
	if (Cue == 3 && T >= 3.3f)
	{
		++Cue;
		Hold(false);
		LineIndex = -1;
		// Gone: the room counts it down and the stage is cleared.
		Monster->FinishBeaten();
		Finish();
	}
}

void AChaosImpactBossScene::Finish()
{
	Part = EPart::Done;
	if (AChaosImpactCharacter* Player = Hero.Get())
	{
		Player->SetSceneHudShown(1.0f);
	}
	UGameplayStatics::SetGlobalTimeDilation(this, 1.0f);
	Destroy();
}

void AChaosImpactBossScene::PaintOverlay(const FGeometry& Geometry, FSlateWindowElementList& Elements, const int32 Layer) const
{
	using namespace ChaosImpactPaint;
	const FVector2f Size = Geometry.GetLocalSize();
	if (Size.X < 2.0f || Size.Y < 2.0f || Part == EPart::Done || Part == EPart::Fight)
	{
		SkipButton = FBox2D(ForceInit);
		return;
	}
	// Laid out for 1920 wide (an upright phone takes its height's measure instead, so the words stay readable).
	const float S = FMath::Max(Size.X / 1920.0f, Size.Y / 1920.0f);
	const FPainter Screen{Geometry, Elements, Layer, 1.0f};
	const double Clock = FPlatformTime::Seconds();

	// ---- Cinema bars.
	if (Letterbox > 0.0f)
	{
		// (Upright, the bars leave a band as wide as the screen and as tall as a 16:9 picture: the shot as framed.)
		const float Band = Size.Y > Size.X ? FMath::Max((Size.Y - Size.X * 9.0f / 16.0f) * 0.5f, Size.Y * 0.105f) : Size.Y * 0.105f;
		const float Bar = Band * Smooth(Letterbox);
		Screen.Box(0.0f, 0.0f, Size.X, Bar, FLinearColor(0.0f, 0.0f, 0.0f, 1.0f));
		Screen.Box(0.0f, Size.Y - Bar, Size.X, Bar, FLinearColor(0.0f, 0.0f, 0.0f, 1.0f));
	}

	// ---- The title: speed lines rushing in on it and its name slammed onto the screen.
	if (TitleTime >= 0.0f)
	{
		const float In = FMath::Clamp(TitleTime / 0.12f, 0.0f, 1.0f);
		const float Out = 1.0f - Smooth(Window(TitleTime, 3.95f, 4.4f));
		const float Alpha = In * Out;
		const FVector2D Center(Size.X * 0.5f, Size.Y * 0.44f);
		const float Reach = FVector2D(Size.X, Size.Y).Size() * 0.62f;
		// Redrawn twenty times a second, every line its own length and width (anime speed lines).
		const int32 Frame = static_cast<int32>(TitleTime * 20.0f);
		FRandomStream Stream(4242 + Frame);
		const FPainter Lines{Geometry, Elements, Layer + 1, Alpha};
		for (int32 Index = 0; Index < 90; ++Index)
		{
			const float Angle = Stream.FRandRange(0.0f, UE_TWO_PI);
			const FVector2D Way(FMath::Cos(Angle), FMath::Sin(Angle) * 0.85f);
			const float Inner = FMath::Min(Size.X, Size.Y) * Stream.FRandRange(0.24f, 0.44f);
			const float Width = Stream.FRandRange(2.0f, 15.0f) * S;
			const FVector2D Across(-Way.Y, Way.X);
			const FVector2D Tip = Center + Way * Inner;
			const FVector2D Root = Center + Way * Reach;
			const FLinearColor Color = Index % 5 == 0 ? FLinearColor(0.02f, 0.03f, 0.08f, 0.55f) : FLinearColor(1.0f, 1.0f, 1.0f, 0.82f);
			HudPoly(Lines, {Tip, Root + Across * Width, Root - Across * Width}, SolidFill(Color), 0);
		}
		// Its name alone, under it (low on the screen, above the cinema bar).
		const FGeometry Anchor = MakeAnchor(Geometry, Center.X, Size.Y * 0.80f, S);
		// シマエナガ: slammed in large, landing with a jolt, a glint running along it.
		const float Age = TitleTime - 0.05f;
		if (Age > 0.0f)
		{
			const float TextSize = 128.0f;
			const float Scale = 1.0f + 1.4f * FMath::Exp(-13.0f * Age);
			const float Jolt = Age < 0.25f ? FMath::Sin(Age * 90.0f) * (0.25f - Age) * 40.0f : 0.0f;
			const FString Name = bPresident ? TEXT("ドッチ・ボール大統領") : TEXT("シマエナガ");
			const FGeometry Words = MakeSkewed(Anchor, -900.0f + Jolt, -TextSize * 0.62f, 1800.0f, TextSize * 1.3f, -0.12f, Scale);
			const float Glint = FMath::Frac(Age * 0.6f);
			const FLinearColor Color = FMath::Lerp(Paper, FLinearColor(1.0f, 0.9f, 0.55f), 0.18f + 0.22f * FMath::Sin(Glint * UE_TWO_PI));
			HudText3D(FPainter{Words, Elements, Layer + 5, Alpha * FMath::Clamp(Age / 0.06f, 0.0f, 1.0f)}, Name, 900.0f, 0.0f, TextSize, Color,
				ETextAlign::Center, TEXT("BlackItalic"), TextSize * 0.08f);
		}
	}

	// ---- The talk: a slanted box along the bottom, the speaker's name on a tab, the words typed out.
	if (BoxShown > 0.0f && Lines().IsValidIndex(LineIndex))
	{
		const FLine& Line = Lines()[LineIndex];
		const float In = ChaosImpactEase::Out(BoxShown);
		const float BoxW = FMath::Min(1560.0f, Size.X / S - 60.0f);
		// The words, wrapped to the box first (never starting a row with a closing mark): a narrow (upright phone) screen
		// takes more rows, and the box grows up to hold them all.
		const TArray<FToken> Tokens = Tokenize(Line.Text);
		const float TextSize = 36.0f;
		const float RubySize = 15.0f;
		const float Left = 52.0f;
		const float Width = BoxW - Left - 120.0f;
		const float RowStep = 52.0f;
		const TArray<TArray<int32>> Rows = LayoutRows(Tokens, Width, TextSize);
		const int32 MaxRows = 6;
		const float BoxH = 214.0f + FMath::Max(FMath::Min(Rows.Num(), MaxRows) - 3, 0) * (RowStep + 8.0f);
		const FGeometry Anchor = MakeAnchor(Geometry, Size.X * 0.5f, Size.Y - (38.0f * S) + (1.0f - In) * 40.0f * S, S);
		const FGeometry BoxSpace = MakeSkewed(Anchor, -BoxW * 0.5f, -BoxH, BoxW, BoxH, -0.07f);
		const FPainter Back{BoxSpace, Elements, Layer + 10, In};
		const FPainter Top = HudAbove(Back, 3);
		const TArray<FVector2D> Plate{{14.0f, 0.0f}, {BoxW, 0.0f}, {BoxW, BoxH - 14.0f}, {BoxW - 14.0f, BoxH}, {0.0f, BoxH}, {0.0f, 14.0f}};
		HudShard(Back, Plate, MakeFill(FLinearColor(0.07f, 0.1f, 0.22f, 0.94f), FLinearColor(0.015f, 0.025f, 0.07f, 0.94f)), 3.0f, FVector2D(5.0, 6.0));
		Top.Line(FVector2D(16.0, 3.0), FVector2D(BoxW - 2.0f, 3.0), WithAlpha(Paper, 0.25f), 1.5f);
		// The speaker's colour along the box's top edge.
		const FLinearColor Accent = Line.Who == 1 ? FLinearColor(0.55f, 0.85f, 0.45f) : Line.Who == 2 ? Gold
			: Line.bBoss ? (bPresident ? FLinearColor(1.0f, 0.78f, 0.35f) : FLinearColor(1.0f, 0.45f, 0.62f)) : Ice;
		Top.Box(14.0f, 0.0f, BoxW * 0.42f, 4.0f, WithAlpha(Accent, 0.95f));
		// The name tab.
		const FString Name = GetSpeakerName(Line);
		const float NameSize = 28.0f;
		const float TabW = HudMeasure(Name, NameSize, TEXT("Black")) + 70.0f;
		const float Flip = ChaosImpactEase::Out(FMath::Min(LineTime / 0.18f, 1.0f));
		const FGeometry TabSpace = MakeSkewed(Anchor, -BoxW * 0.5f + 34.0f, -BoxH - 34.0f, TabW, 52.0f, -0.18f, 0.6f + 0.4f * Flip);
		const FPainter Tab{TabSpace, Elements, Layer + 16, In};
		const FHudFill TabFill = Line.Who == 1 ? MakeFill(FLinearColor(0.85f, 1.0f, 0.8f), FLinearColor(0.25f, 0.6f, 0.2f))
			: Line.Who == 2 ? MakeFill(FLinearColor(1.0f, 0.97f, 0.8f), FLinearColor(0.95f, 0.65f, 0.15f))
			: Line.bBoss ? (bPresident ? MakeFill(FLinearColor(1.0f, 0.95f, 0.8f), FLinearColor(0.85f, 0.55f, 0.12f))
				: MakeFill(FLinearColor(1.0f, 0.97f, 0.99f), FLinearColor(1.0f, 0.62f, 0.75f)))
			: MakeFill(FLinearColor(0.75f, 0.93f, 1.0f), FLinearColor(0.08f, 0.38f, 0.95f));
		HudShard(Tab, {{8.0f, 0.0f}, {TabW, 0.0f}, {TabW - 8.0f, 52.0f}, {0.0f, 52.0f}}, TabFill, 2.5f, FVector2D(3.0, 4.0));
		HudText3D(HudAbove(Tab, 4), Name, TabW * 0.5f, 7.0f, NameSize, Line.Who != 0 || Line.bBoss ? FLinearColor(0.25f, 0.05f, 0.12f) : Paper, ETextAlign::Center,
			TEXT("Black"), 2.0f);
		// The words typed as far as Typed.
		const FPainter Words{BoxSpace, Elements, Layer + 20, In};
		int32 Count = 0;
		const int32 Shown = static_cast<int32>(Typed);
		float RowY = 40.0f - RowStep;
		for (int32 Row = 0; Row < Rows.Num() && Row < MaxRows; ++Row)
		{
			float RowX = Left;
			// (A row with ruby over it a little lower, so the ruby never sits on the row above.)
			bool bRuby = false;
			for (const int32 Index : Rows[Row])
			{
				bRuby = bRuby || !Tokens[Index].Ruby.IsEmpty();
			}
			RowY += RowStep + (bRuby && Row > 0 ? 14.0f : 0.0f);
			for (const int32 Index : Rows[Row])
			{
				const FToken& Token = Tokens[Index];
				const int32 Visible = FMath::Clamp(Shown - Count, 0, Token.Base.Len());
				const float W = IsGap(Token.Base) ? 0.0f : HudMeasure(Token.Base, TextSize, TEXT("Bold"));
				if (Visible > 0 && W > 0.0f)
				{
					Words.Text(Token.Base.Left(Visible), RowX, RowY, TextSize, Paper, ETextAlign::Left, TEXT("Bold"), 2.0f, Ink);
					if (!Token.Ruby.IsEmpty() && Visible == Token.Base.Len())
					{
						const float RubyFit = HudFit(Token.Ruby, RubySize, FMath::Max(W + 30.0f, 40.0f), TEXT("Bold"));
						Words.Text(Token.Ruby, RowX + W * 0.5f, RowY - RubyFit - 1.0f, RubyFit, FLinearColor(1.0f, 0.9f, 0.55f), ETextAlign::Center,
							TEXT("Bold"), 1.5f, Ink);
					}
				}
				Count += Token.Base.Len();
				RowX += W;
			}
		}
		// ▼ when the line is all there.
		if (Typed >= BaseLength(Tokens))
		{
			const float Bob = 4.0f * FMath::Sin(static_cast<float>(Clock) * 6.0f);
			const FVector2D Arrow(BoxW - 100.0f, BoxH - 40.0f + Bob);
			HudShard(HudAbove(Top, 6), {Arrow + FVector2D(-11, -8), Arrow + FVector2D(11, -8), Arrow + FVector2D(0, 8)},
				MakeFill(FLinearColor(1.0f, 0.95f, 0.6f), Gold), 1.5f, FVector2D(1.5, 2.0));
		}
	}

	// ---- Skip (top right, inside the bar): the pause button, or a tap on it.
	if (IsTakingInput() && Part != EPart::Beaten && Part != EPart::Leave && Part != EPart::ToFight && Part != EPart::Ending)
	{
		const AChaosImpactPlayerController* PC = Hero.IsValid() ? Cast<AChaosImpactPlayerController>(Hero->GetController()) : nullptr;
		const bool bTouch = PC && PC->ShouldUseTouchControls();
		const EChaosImpactDevice Device = ChaosImpactHints::GetDevice(PC);
		const FString Key = bTouch ? FString() : ChaosImpactHints::Key(Device, EKeys::Gamepad_Special_Right, TEXT("Esc"));
		const FString Label = Key.IsEmpty() ? FString(TEXT("スキップ ▶▶")) : FString::Printf(TEXT("スキップ ▶▶  %s"), *Key);
		const float LabelSize = 22.0f;
		const float W = HudMeasure(Label, LabelSize, TEXT("Bold")) + 36.0f;
		const float H = 42.0f;
		const float X = Size.X / S - W - 26.0f;
		const float Y = 20.0f;
		const FGeometry Space = MakeAnchor(Geometry, 0.0f, 0.0f, S);
		const FPainter Chip{Space, Elements, Layer + 30, 0.9f};
		HudShard(Chip, {{X + 8.0f, Y}, {X + W, Y}, {X + W - 8.0f, Y + H}, {X, Y + H}}, MakeFill(FLinearColor(0.1f, 0.13f, 0.26f), FLinearColor(0.03f, 0.04f, 0.1f)),
			2.0f, FVector2D(2.0, 3.0));
		HudAbove(Chip, 3).Text(Label, X + W * 0.5f, Y + 8.0f, LabelSize, Paper, ETextAlign::Center, TEXT("Bold"), 1.5f, Ink);
		SkipButton = FBox2D(FVector2D((X - 10.0f) * S / Size.X, (Y - 10.0f) * S / Size.Y), FVector2D((X + W + 10.0f) * S / Size.X, (Y + H + 10.0f) * S / Size.Y));
	}
	else
	{
		SkipButton = FBox2D(ForceInit);
	}

	if (Part == EPart::Ending)
	{
		PaintEnding(Geometry, Elements, Layer);
	}

	// ---- White flash, then the dark over all.
	if (Flash > 0.0f)
	{
		FPainter{Geometry, Elements, Layer + 40, 1.0f}.Box(0.0f, 0.0f, Size.X, Size.Y, WithAlpha(Paper, FMath::Clamp(Flash, 0.0f, 1.0f) * 0.85f));
	}
	if (White > 0.0f)
	{
		FPainter{Geometry, Elements, Layer + 42, 1.0f}.Box(0.0f, 0.0f, Size.X, Size.Y, FLinearColor(0.9f, 0.93f, 0.98f, FMath::Clamp(White, 0.0f, 1.0f) * 0.96f));
	}
	if (Black > 0.0f)
	{
		FPainter{Geometry, Elements, Layer + 44, 1.0f}.Box(0.0f, 0.0f, Size.X, Size.Y, FLinearColor(0.0f, 0.0f, 0.0f, FMath::Clamp(Black, 0.0f, 1.0f)));
	}
}
