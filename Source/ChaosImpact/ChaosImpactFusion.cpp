#include "ChaosImpactFusion.h"
#include "ChaosImpactDuelBallBalance.h"

#include "ChaosImpact.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactDojo.h"
#include "ChaosImpactFxKit.h"
#include "ChaosImpactFusionEffects.h"
#include "ChaosImpactGadget.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactHazardZone.h"
#include "ChaosImpactHook.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactSimaeBird.h"
#include "ChaosImpactTornado.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"

namespace
{
	using EType = EChaosImpactBallType;

	/** How strongly each kind flies: of two, the higher flies and the other rides along (the plan's order, then the rest). */
	int32 FusionFlightRank(const EType Type)
	{
		switch (Type)
		{
		case EType::Drive: return 100;
		case EType::Beam: return 90;
		case EType::Thunder: return 80;
		case EType::Hook: return 70;
		case EType::Anchor: return 65;
		case EType::Wind: return 60;
		case EType::Snow: return 50;
		case EType::Simae: return 45;
		case EType::Swap: return 40;
		case EType::Fire: return 30;
		case EType::Ice: return 28;
		case EType::Black: return 26;
		case EType::Smoke: return 24;
		// (2026-11-19: プリズン, バリア and ノヴァ join too. ノヴァ goes by its own rule in PickPrimary.)
		case EType::Prison: return 22;
		case EType::Barrier: return 21;
		case EType::Mine: return 20;
		case EType::Nova: return 10;
		default: return 0;
		}
	}

	struct FFusionNameEntry
	{
		EType A;
		EType B;
		const TCHAR* Name;
	};

	/** All 104 pairs and their names (仮) from Docs/合体ボール企画.md. */
	const FFusionNameEntry FusionNames[] = {
		{EType::Fire, EType::Ice, TEXT("蒸気爆発")}, {EType::Fire, EType::Thunder, TEXT("炎雷")}, {EType::Fire, EType::Black, TEXT("火炎の渦")},
		{EType::Fire, EType::Wind, TEXT("炎の竜巻")}, {EType::Fire, EType::Smoke, TEXT("黒煙")}, {EType::Fire, EType::Beam, TEXT("灼熱光線")},
		{EType::Fire, EType::Snow, TEXT("溶岩玉")}, {EType::Fire, EType::Simae, TEXT("火の鳥")}, {EType::Fire, EType::Drive, TEXT("操縦ファイア")},
		{EType::Fire, EType::Hook, TEXT("火炎フック")}, {EType::Fire, EType::Anchor, TEXT("炎の突進")}, {EType::Fire, EType::Swap, TEXT("入れ替え爆破")},
		{EType::Fire, EType::Mine, TEXT("焼夷地雷")},
		{EType::Ice, EType::Thunder, TEXT("氷雷")}, {EType::Ice, EType::Black, TEXT("凍てつく渦")}, {EType::Ice, EType::Wind, TEXT("吹雪")},
		{EType::Ice, EType::Smoke, TEXT("冷気の霧")}, {EType::Ice, EType::Beam, TEXT("凍結光線")}, {EType::Ice, EType::Snow, TEXT("氷塊")},
		{EType::Ice, EType::Simae, TEXT("氷の群れ")}, {EType::Ice, EType::Drive, TEXT("操縦アイス")}, {EType::Ice, EType::Hook, TEXT("氷のフック")},
		{EType::Ice, EType::Anchor, TEXT("氷の滑走")}, {EType::Ice, EType::Swap, TEXT("凍結スワップ")}, {EType::Ice, EType::Mine, TEXT("冷凍地雷")},
		{EType::Thunder, EType::Black, TEXT("雷の渦")}, {EType::Thunder, EType::Wind, TEXT("雷雲竜巻")}, {EType::Thunder, EType::Smoke, TEXT("雷雲")},
		{EType::Thunder, EType::Beam, TEXT("レールガン")}, {EType::Thunder, EType::Snow, TEXT("帯電雪玉")}, {EType::Thunder, EType::Simae, TEXT("雷鳥")},
		{EType::Thunder, EType::Drive, TEXT("操縦サンダー")}, {EType::Thunder, EType::Hook, TEXT("雷フック")}, {EType::Thunder, EType::Anchor, TEXT("電光石火")},
		{EType::Thunder, EType::Swap, TEXT("跳ね返り入れ替え")}, {EType::Thunder, EType::Mine, TEXT("連鎖地雷")},
		{EType::Black, EType::Wind, TEXT("大渦竜巻")}, {EType::Black, EType::Smoke, TEXT("闇の霧")}, {EType::Black, EType::Beam, TEXT("重力光線")},
		{EType::Black, EType::Snow, TEXT("重力雪玉")}, {EType::Black, EType::Simae, TEXT("渦の群れ")}, {EType::Black, EType::Drive, TEXT("吸引ドライブ")},
		{EType::Black, EType::Hook, TEXT("グラビティフック")}, {EType::Black, EType::Anchor, TEXT("重力アンカー")}, {EType::Black, EType::Swap, TEXT("入れ替え渦")},
		{EType::Black, EType::Mine, TEXT("吸引地雷")},
		{EType::Wind, EType::Smoke, TEXT("砂嵐")}, {EType::Wind, EType::Beam, TEXT("旋風光線")}, {EType::Wind, EType::Snow, TEXT("雪崩")},
		{EType::Wind, EType::Simae, TEXT("つむじ風の群れ")}, {EType::Wind, EType::Drive, TEXT("操縦竜巻")}, {EType::Wind, EType::Hook, TEXT("旋風フック")},
		{EType::Wind, EType::Anchor, TEXT("疾風")}, {EType::Wind, EType::Swap, TEXT("入れ替え竜巻")}, {EType::Wind, EType::Mine, TEXT("つむじ地雷")},
		{EType::Smoke, EType::Beam, TEXT("目くらまし光線")}, {EType::Smoke, EType::Snow, TEXT("霧の雪玉")}, {EType::Smoke, EType::Simae, TEXT("霧の群れ")},
		{EType::Smoke, EType::Drive, TEXT("煙幕ドライブ")}, {EType::Smoke, EType::Hook, TEXT("煙フック")}, {EType::Smoke, EType::Anchor, TEXT("煙の離脱")},
		{EType::Smoke, EType::Swap, TEXT("煙の入れ替え")}, {EType::Smoke, EType::Mine, TEXT("煙地雷")},
		{EType::Beam, EType::Snow, TEXT("極太光線")}, {EType::Beam, EType::Simae, TEXT("光の群れ")}, {EType::Beam, EType::Drive, TEXT("操縦ビーム")},
		{EType::Beam, EType::Hook, TEXT("貫通フック")}, {EType::Beam, EType::Anchor, TEXT("光の跳躍")}, {EType::Beam, EType::Swap, TEXT("光の入れ替え")},
		{EType::Beam, EType::Mine, TEXT("光の地雷原")},
		{EType::Snow, EType::Simae, TEXT("雪玉の巣")}, {EType::Snow, EType::Drive, TEXT("操縦雪玉")}, {EType::Snow, EType::Hook, TEXT("雪玉フック")},
		{EType::Snow, EType::Anchor, TEXT("雪玉アンカー")}, {EType::Snow, EType::Swap, TEXT("雪だるま入れ替え")}, {EType::Snow, EType::Mine, TEXT("雪玉地雷")},
		{EType::Simae, EType::Drive, TEXT("操縦の群れ")}, {EType::Simae, EType::Hook, TEXT("運び鳥")}, {EType::Simae, EType::Anchor, TEXT("鳥の運び")},
		{EType::Simae, EType::Swap, TEXT("鳥の入れ替え")}, {EType::Simae, EType::Mine, TEXT("鳥の巣地雷")},
		{EType::Drive, EType::Hook, TEXT("操縦フック")}, {EType::Drive, EType::Anchor, TEXT("操縦アンカー")}, {EType::Drive, EType::Swap, TEXT("操縦スワップ")},
		{EType::Drive, EType::Mine, TEXT("操縦地雷")},
		{EType::Hook, EType::Swap, TEXT("引き寄せ入れ替え")}, {EType::Hook, EType::Mine, TEXT("地雷フック")},
		{EType::Anchor, EType::Swap, TEXT("体当たり入れ替え")}, {EType::Anchor, EType::Mine, TEXT("置き去り地雷")},
		{EType::Swap, EType::Mine, TEXT("入れ替え地雷")},
		// 2026-11-19 (Docs/合体ボール追加企画.md): ノヴァ, プリズン, バリア, フック＋アンカー.
		{EType::Nova, EType::Fire, TEXT("太陽ノヴァ")}, {EType::Nova, EType::Ice, TEXT("氷河ノヴァ")}, {EType::Nova, EType::Thunder, TEXT("雷神ノヴァ")},
		{EType::Nova, EType::Black, TEXT("特異点ノヴァ")}, {EType::Nova, EType::Wind, TEXT("大竜巻ノヴァ")}, {EType::Nova, EType::Smoke, TEXT("暗雲ノヴァ")},
		{EType::Nova, EType::Beam, TEXT("ノヴァ砲")}, {EType::Nova, EType::Snow, TEXT("雪山ノヴァ")}, {EType::Nova, EType::Simae, TEXT("大群ノヴァ")},
		{EType::Nova, EType::Drive, TEXT("操縦ノヴァ")}, {EType::Nova, EType::Prison, TEXT("大監獄ノヴァ")}, {EType::Nova, EType::Hook, TEXT("地引き網ノヴァ")},
		{EType::Nova, EType::Anchor, TEXT("流星ノヴァ")}, {EType::Nova, EType::Swap, TEXT("シャッフルノヴァ")}, {EType::Nova, EType::Barrier, TEXT("守護ノヴァ")},
		{EType::Nova, EType::Mine, TEXT("機雷ノヴァ")},
		{EType::Prison, EType::Fire, TEXT("火あぶりの檻")}, {EType::Prison, EType::Ice, TEXT("氷の牢")}, {EType::Prison, EType::Thunder, TEXT("電気の檻")},
		{EType::Prison, EType::Black, TEXT("重力の檻")}, {EType::Prison, EType::Wind, TEXT("竜巻の檻")}, {EType::Prison, EType::Smoke, TEXT("目かくしの檻")},
		{EType::Prison, EType::Beam, TEXT("一列の檻")}, {EType::Prison, EType::Snow, TEXT("雪だるまの檻")}, {EType::Prison, EType::Simae, TEXT("鳥かご")},
		{EType::Prison, EType::Drive, TEXT("護送の檻")}, {EType::Prison, EType::Hook, TEXT("捕獲フック")}, {EType::Prison, EType::Anchor, TEXT("檻ジャンプ")},
		{EType::Prison, EType::Swap, TEXT("身代わりの檻")}, {EType::Prison, EType::Barrier, TEXT("牢屋の壁")}, {EType::Prison, EType::Mine, TEXT("檻の地雷")},
		{EType::Barrier, EType::Fire, TEXT("炎の壁")}, {EType::Barrier, EType::Ice, TEXT("氷の壁")}, {EType::Barrier, EType::Thunder, TEXT("電撃バリア")},
		{EType::Barrier, EType::Black, TEXT("吸引バリア")}, {EType::Barrier, EType::Wind, TEXT("風の壁")}, {EType::Barrier, EType::Smoke, TEXT("煙の壁")},
		{EType::Barrier, EType::Beam, TEXT("長い光の壁")}, {EType::Barrier, EType::Snow, TEXT("雪の壁")}, {EType::Barrier, EType::Simae, TEXT("見張りのバリア")},
		{EType::Barrier, EType::Drive, TEXT("操縦バリア")}, {EType::Barrier, EType::Hook, TEXT("守りのフック")}, {EType::Barrier, EType::Anchor, TEXT("着地バリア")},
		{EType::Barrier, EType::Swap, TEXT("入れ替え壁")}, {EType::Barrier, EType::Mine, TEXT("地雷付きバリア")},
		{EType::Hook, EType::Anchor, TEXT("グラップル")},
		// 強化ボール (超〇〇): two of the same.
		{EType::Fire, EType::Fire, TEXT("獄炎")}, {EType::Ice, EType::Ice, TEXT("絶対零度")}, {EType::Thunder, EType::Thunder, TEXT("迅雷")},
		{EType::Black, EType::Black, TEXT("特異点")}, {EType::Wind, EType::Wind, TEXT("双竜巻")}, {EType::Smoke, EType::Smoke, TEXT("濃霧")},
		{EType::Beam, EType::Beam, TEXT("三叉光線")}, {EType::Snow, EType::Snow, TEXT("大雪玉")}, {EType::Simae, EType::Simae, TEXT("大群")},
		{EType::Drive, EType::Drive, TEXT("ターボドライブ")}, {EType::Prison, EType::Prison, TEXT("連鎖の檻")}, {EType::Hook, EType::Hook, TEXT("ダブルフック")},
		{EType::Anchor, EType::Anchor, TEXT("二段アンカー")}, {EType::Swap, EType::Swap, TEXT("ぐるぐる入れ替え")}, {EType::Barrier, EType::Barrier, TEXT("要塞")},
		{EType::Mine, EType::Mine, TEXT("地雷原")}, {EType::Nova, EType::Nova, TEXT("超新星")},
		};
	static_assert(UE_ARRAY_COUNT(FusionNames) == 136 + 17, "Every pair of the 17 specials, and the 17 強化ボール");

	UStaticMesh* FusionMesh(const TCHAR* Path)
	{
		return LoadObject<UStaticMesh>(nullptr, Path);
	}

	/** A part of the look: no collision, no shadow, kept to its own world scale and turn. */
	UStaticMeshComponent* FusionPart(AActor* Owner, USceneComponent* Parent, UStaticMesh* Mesh, UMaterialInterface* Look)
	{
		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(Owner);
		Part->SetStaticMesh(Mesh);
		Part->SetMaterial(0, Look);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetGenerateOverlapEvents(false);
		Part->SetCastShadow(false);
		Part->SetupAttachment(Parent);
		Part->SetUsingAbsoluteScale(true);
		Part->SetUsingAbsoluteRotation(true);
		Part->RegisterComponent();
		return Part;
	}

	/** The two colours a little apart when they are alike (two blues still read as two). */
	FLinearColor FusionColor(const EType Type)
	{
		FLinearColor Color = ChaosImpactBallTypes::GetColor(Type);
		Color.A = 1.0f;
		return Color;
	}
}

bool ChaosImpactFusion::CanFuseTypes(const EChaosImpactBallType A, const EChaosImpactBallType B)
{
	// (2026-11-19) Any two specials: two of the same make a 強化ボール (超〇〇). Never a normal ball.
	return FusionFlightRank(A) > 0 && FusionFlightRank(B) > 0;
}

EChaosImpactBallType ChaosImpactFusion::PickPrimary(const EChaosImpactBallType A, const EChaosImpactBallType B)
{
	if (A == B)
	{
		return A;
	}
	// ノヴァ: charged over the head and dropped as ever, the other riding along; except with those that change how it goes
	// (ノヴァ砲, 操縦ノヴァ, 流星ノヴァ, 地引き網ノヴァ, 大竜巻ノヴァ, 雷神ノヴァ, 雪山ノヴァ): they fly, the nova rides.
	if (A == EType::Nova || B == EType::Nova)
	{
		const EChaosImpactBallType Other = A == EType::Nova ? B : A;
		switch (Other)
		{
		case EType::Beam: case EType::Drive: case EType::Anchor: case EType::Hook: case EType::Wind: case EType::Thunder: case EType::Snow:
			return Other;
		default:
			return EType::Nova;
		}
	}
	// グラップル: the anchor flies, the hook's catch rides along on it.
	if ((A == EType::Hook && B == EType::Anchor) || (A == EType::Anchor && B == EType::Hook))
	{
		return EType::Anchor;
	}
	// 雷雲竜巻 (サンダー×ウィンド): the tornado flies, at a thunder ball's speed (rebounding off walls, sweeping up whoever
	// it touches at that speed).
	if ((A == EType::Thunder && B == EType::Wind) || (A == EType::Wind && B == EType::Thunder))
	{
		return EType::Wind;
	}
	// 雷フック・電光石火・貫通フック・光の跳躍: the hook or anchor flies, the thunder or the light changing how it goes.
	const auto IsLine = [](const EChaosImpactBallType Kind) { return Kind == EType::Hook || Kind == EType::Anchor; };
	const auto IsShaper = [](const EChaosImpactBallType Kind) { return Kind == EType::Thunder || Kind == EType::Beam; };
	if (IsLine(A) && IsShaper(B))
	{
		return A;
	}
	if (IsLine(B) && IsShaper(A))
	{
		return B;
	}
	return FusionFlightRank(B) > FusionFlightRank(A) ? B : A;
}

FString ChaosImpactFusion::GetFusionName(const EChaosImpactBallType A, const EChaosImpactBallType B)
{
	for (const FFusionNameEntry& Entry : FusionNames)
	{
		if ((Entry.A == A && Entry.B == B) || (Entry.A == B && Entry.B == A))
		{
			return Entry.Name;
		}
	}
	return FString::Printf(TEXT("%s＋%s"), ChaosImpactBallTypes::GetDisplayName(A), ChaosImpactBallTypes::GetDisplayName(B));
}

FString ChaosImpactFusion::GetFusionLabel(const EChaosImpactBallType Primary, const EChaosImpactBallType Secondary)
{
	if (Primary == Secondary)
	{
		// 強化ボール: 超ファイア.
		return FString::Printf(TEXT("超%s"), ChaosImpactBallTypes::GetDisplayName(Primary));
	}
	return FString::Printf(TEXT("%s×%s"), ChaosImpactBallTypes::GetDisplayName(Primary), ChaosImpactBallTypes::GetDisplayName(Secondary));
}

FLinearColor ChaosImpactFusion::TintToward(const FLinearColor& Base, const EChaosImpactBallType Secondary, const float Amount)
{
	const float Bright = FMath::Max3(Base.R, Base.G, Base.B);
	if (Bright <= KINDA_SMALL_NUMBER)
	{
		return Base;
	}
	// How pale it was (its weakest channel against its strongest): kept, so a core stays a core.
	const float Pale = FMath::Min3(Base.R, Base.G, Base.B) / Bright;
	FLinearColor Hue = ChaosImpactBallTypes::GetColor(Secondary);
	const float HueBright = FMath::Max3(Hue.R, Hue.G, Hue.B);
	Hue = HueBright > KINDA_SMALL_NUMBER ? Hue * (1.0f / HueBright) : FLinearColor::White;
	// (Half as pale: a near-white core still takes the colour, a tornado's light strands going flame, not cream.)
	const FLinearColor Target = FMath::Lerp(Hue, FLinearColor::White, Pale * 0.5f) * Bright;
	FLinearColor Result = FMath::Lerp(Base, Target, FMath::Clamp(Amount, 0.0f, 1.0f));
	Result.A = Base.A;
	return Result;
}

void ChaosImpactFusion::TintActorLooks(AActor* Owner, const EChaosImpactBallType Secondary, const float Amount)
{
	if (!Owner)
	{
		return;
	}
	TInlineComponentArray<UPrimitiveComponent*> Parts(Owner);
	for (UPrimitiveComponent* Part : Parts)
	{
		for (int32 Slot = 0; Part && Slot < Part->GetNumMaterials(); ++Slot)
		{
			UMaterialInstanceDynamic* Look = Cast<UMaterialInstanceDynamic>(Part->GetMaterial(Slot));
			if (!Look)
			{
				continue;
			}
			for (const TCHAR* Name : {TEXT("Color"), TEXT("Base Color"), TEXT("CoreColor")})
			{
				FLinearColor Color;
				if (Look->GetVectorParameterValue(FHashedMaterialParameterInfo(Name), Color))
				{
					Look->SetVectorParameterValue(Name, TintToward(Color, Secondary, Amount));
				}
			}
		}
	}
	TInlineComponentArray<UPointLightComponent*> Lights(Owner);
	for (UPointLightComponent* Light : Lights)
	{
		Light->SetLightColor(TintToward(Light->GetLightColor(), Secondary, Amount));
	}
}

namespace
{
	TMap<TWeakObjectPtr<const UObject>, TPair<EChaosImpactBallType, float>>& FusionTints()
	{
		static TMap<TWeakObjectPtr<const UObject>, TPair<EChaosImpactBallType, float>> Tints;
		return Tints;
	}
}

void ChaosImpactFusion::RegisterTint(const UObject* Source, const EChaosImpactBallType Toward, const float Amount)
{
	if (!Source || Toward == EChaosImpactBallType::Normal || Amount <= 0.0f)
	{
		return;
	}
	auto& Tints = FusionTints();
	if (Tints.Num() > 48)
	{
		for (auto It = Tints.CreateIterator(); It; ++It)
		{
			if (!It.Key().IsValid())
			{
				It.RemoveCurrent();
			}
		}
	}
	Tints.Add(Source, {Toward, Amount});
}

bool ChaosImpactFusion::FindTint(const UObject* Context, EChaosImpactBallType& OutToward, float& OutAmount)
{
	auto& Tints = FusionTints();
	if (!Context || Tints.IsEmpty())
	{
		return false;
	}
	const UObject* Candidates[3] = {Context, nullptr, nullptr};
	if (const UActorComponent* Component = Cast<UActorComponent>(Context))
	{
		Candidates[1] = Component->GetOwner();
		if (const USceneComponent* Scene = Cast<USceneComponent>(Context))
		{
			Candidates[2] = Scene->GetAttachParent() ? Scene->GetAttachParent()->GetOwner() : nullptr;
		}
	}
	for (const UObject* Candidate : Candidates)
	{
		if (const TPair<EChaosImpactBallType, float>* Found = Candidate ? Tints.Find(Candidate) : nullptr)
		{
			OutToward = Found->Key;
			OutAmount = Found->Value;
			return true;
		}
	}
	return false;
}

bool ChaosImpactFusion::IsOneHitRule(const UWorld* World)
{
	return ChaosImpactDuelBallBalance::IsDuel(World);
}

bool ChaosImpactFusion::IsAllowedIn(const UWorld* World, const AChaosImpactCharacter* Character)
{
	const AChaosImpactGameState* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
	if (!Match || Match->IsMatchmade() || Match->bPublicTournament || Match->bLocalTournament)
	{
		return false;
	}
	if (Match->bVersusMatch || Match->IsTournament())
	{
		// A match: when its rules have 合体ボール on, in a 合言葉 room or a local match (every rule).
		if (!Match->Rules.bFusionBalls)
		{
			return false;
		}
		if (Match->bOnlineRoom)
		{
			return true;
		}
		// (A CPU too: it has no player controller of its own.)
		const AChaosImpactPlayerController* Local = Character ? Cast<AChaosImpactPlayerController>(Character->GetController()) : nullptr;
		return World->GetNetMode() == NM_Standalone && (!Local || (!Local->IsDojo() && !Local->IsSoloMode()));
	}
	if (Match->bOnlineRoom)
	{
		// An online room: only while everyone waits in its lobby (never in the match, nor on the match's own level).
		return !Match->bRoomMatchLevel && !Match->IsMatchmade() && Match->Phase == EChaosImpactOnlinePhase::Lobby;
	}
	if (World->GetNetMode() == NM_Standalone && AChaosImpactDojo::AllowsFusion(World))
	{
		// 訓練場: its own 合体ボール lesson.
		return true;
	}
	if (World->GetNetMode() != NM_Standalone || !ChaosImpact::IsTrainingWorld(World))
	{
		return false;
	}
	const AChaosImpactPlayerController* Player = Character ? Cast<AChaosImpactPlayerController>(Character->GetController()) : nullptr;
	return Player && !Player->IsDojo() && !Player->IsSoloMode() && !Player->IsVersusMatchWorld();
}

void ChaosImpactFusion::ApplySecondary(UWorld* World, const EChaosImpactBallType Secondary, const FVector& Location,
	const FVector& Heading, APawn* Source, AActor* DirectVictim, TSharedPtr<FChaosImpactDuelThrowDamage> ThrowDamage, UClass* BallClass,
	const EChaosImpactBallType Primary, const float Reach)
{
	if (!World || World->GetNetMode() == NM_Client)
	{
		return;
	}
	FVector Way = Heading.GetSafeNormal2D();
	if (Way.IsNearlyZero())
	{
		Way = Source ? (Location - Source->GetActorLocation()).GetSafeNormal2D() : FVector::ForwardVector;
	}
	if (Way.IsNearlyZero())
	{
		Way = FVector::ForwardVector;
	}
	UE_LOG(LogChaosImpact, Log, TEXT("Fusion: %s rides along at %s"), ChaosImpactBallTypes::GetInternalName(Secondary), *Location.ToCompactString());
	// (What the ball struck takes this burst too, except under the one-hit rule.)
	AActor* const Spared = SparedVictim(World, DirectVictim);
	// The pairs added on 2026-11-19 (ノヴァ, プリズン, バリア, グラップル) and the 強化ボール.
	if (ApplyPlusEnd(World, Primary, Secondary, Location, Way, Source, DirectVictim, ThrowDamage, BallClass, Reach))
	{
		return;
	}
	// The pair's own end (蒸気爆発, 火炎の渦, 雷雲, a swap's effect where the other landed…), where it has one.
	if (ApplyEndVariant(World, Primary, Secondary, Location, Source, DirectVictim, ThrowDamage, Reach))
	{
		return;
	}
	switch (Secondary)
	{
	case EType::Fire: case EType::Ice: case EType::Thunder: case EType::Black: case EType::Smoke: case EType::Snow:
		// Its own burst, coloured a little by the one it flew with: one effect of the two.
		AChaosImpactHazardZone::Detonate(World, Secondary, Location, Source, Spared, Reach, ThrowDamage, Primary,
			Secondary == EType::Thunder ? 0.25f : TintSide, Reach);
		break;
	case EType::Simae:
		AChaosImpactSimaeBird::ReleaseFlock(World, Location, Source, DirectVictim, ThrowDamage);
		break;
	case EType::Wind:
		// A tornado sets off from there, the way the ball was going.
		AChaosImpactTornado::Release(World, Location + FVector::UpVector * 10.0f, Way, Source, EChaosImpactBallType::Normal, Primary, TintSide);
		break;
	case EType::Swap:
		if (AChaosImpactCharacter* Thrower = Cast<AChaosImpactCharacter>(Source))
		{
			if (AChaosImpactCharacter* Victim = Cast<AChaosImpactCharacter>(DirectVictim); Victim && Victim != Thrower)
			{
				Thrower->SwapPlacesWith(Victim);
			}
		}
		break;
	case EType::Barrier: case EType::Mine:
	{
		FVector Floor;
		const FVector Near = DirectVictim ? DirectVictim->GetActorLocation() : Location;
		if (AChaosImpactGadget::FindFloor(World, Near, Floor))
		{
			if (AChaosImpactGadget* Gadget = AChaosImpactGadget::Place(World, Secondary, Floor, Way, Source, Primary, TintSide))
			{
				Gadget->SetFusedWith(Primary);
			}
		}
		break;
	}
	case EType::Hook: case EType::Anchor:
		// From the thrower to where it ended: a hook reels in what is there; an anchor takes the thrower there.
		if (Source)
		{
			const FVector From = Source->GetActorLocation();
			const FVector Toward = (Location - From).GetSafeNormal2D();
			if (!Toward.IsNearlyZero())
			{
				AChaosImpactHook::Release(World, From, Toward, Source, Secondary == EType::Anchor);
			}
		}
		break;
	case EType::Beam:
		// A beam shot on from there, the way the ball was going.
		if (BallClass)
		{
			const FTransform At(Way.Rotation(), Location + Way * 30.0f);
			if (AChaosImpactBall* Beam = World->SpawnActorDeferred<AChaosImpactBall>(BallClass, At, Source, Source,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
			{
				Beam->SetBallType(EType::Beam);
				Beam->FinishSpawning(At);
				Beam->Launch(Way, ChaosImpactBallTypes::BeamSpeed, EChaosImpactBallFlightMode::Straight, 0.0f);
			}
		}
		break;
	default:
		break;
	}
}

void ChaosImpactFusion::PlayJoinFlash(const UObject* WorldContext, const FVector& At, const EChaosImpactBallType A,
	const EChaosImpactBallType B)
{
	AChaosImpactFxKit* Fx = AChaosImpactFxKit::Spawn(const_cast<UObject*>(WorldContext), At);
	if (!Fx)
	{
		return;
	}
	const FLinearColor ColorA = FusionColor(A);
	const FLinearColor ColorB = FusionColor(B);
	const FLinearColor White(1.0f, 0.97f, 0.92f);
	// The flash (ピカッ): a white core opening fast and fading, a wider soft glow of the two colours under it.
	FChaosImpactFxPiece Flash;
	Flash.Look = EChaosImpactFxLook::Glow;
	Flash.Life = 0.32f;
	Flash.SizeFrom = 30.0f;
	Flash.SizeTo = 190.0f;
	Flash.Color = White;
	Flash.CoreColor = FLinearColor::White;
	Flash.Intensity = 9.0f;
	Flash.Attack = 0.12f;
	Flash.ErodeTo = 0.4f;
	Fx->Add(Flash);
	FChaosImpactFxPiece Bloom = Flash;
	Bloom.Life = 0.55f;
	Bloom.SizeFrom = 80.0f;
	Bloom.SizeTo = 260.0f;
	Bloom.Color = FMath::Lerp(ColorA, ColorB, 0.5f);
	Bloom.CoreColor = White;
	Bloom.Intensity = 3.0f;
	Fx->Add(Bloom);
	// Two clean rings in the air, one of each colour, the second a beat later.
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FChaosImpactFxPiece Ring;
		Ring.Look = EChaosImpactFxLook::AirRing;
		Ring.Delay = Index * 0.07f;
		Ring.Life = 0.45f;
		Ring.SizeFrom = 50.0f;
		Ring.SizeTo = 240.0f + Index * 50.0f;
		Ring.Color = Index == 0 ? ColorA : ColorB;
		Ring.CoreColor = White;
		Ring.Intensity = 5.0f;
		Ring.RingWidth = 0.06f;
		Fx->Add(Ring);
	}
	// Soft motes of both colours drawn in from all round as the two meet.
	for (int32 Index = 0; Index < 12; ++Index)
	{
		const float Angle = UE_TWO_PI * Index / 12.0f;
		const FVector Out(FMath::Cos(Angle), FMath::Sin(Angle), 0.35f * FMath::Sin(Angle * 3.0f));
		FChaosImpactFxPiece Mote;
		Mote.Look = EChaosImpactFxLook::Glow;
		Mote.Location = Out * 120.0f;
		Mote.Velocity = -Out * 420.0f;
		Mote.Drag = 3.0f;
		Mote.Life = 0.3f;
		Mote.SizeFrom = 18.0f;
		Mote.SizeTo = 6.0f;
		Mote.Color = Index % 2 == 0 ? ColorA : ColorB;
		Mote.CoreColor = White;
		Mote.Intensity = 6.0f;
		Mote.Attack = 0.3f;
		Fx->Add(Mote);
	}
}

void ChaosImpactFusion::PlayFusedBurst(const UObject* WorldContext, const FVector& At, const EChaosImpactBallType A,
	const EChaosImpactBallType B)
{
	AChaosImpactFxKit* Fx = AChaosImpactFxKit::Spawn(const_cast<UObject*>(WorldContext), At);
	if (!Fx)
	{
		return;
	}
	const FLinearColor ColorA = FusionColor(A);
	const FLinearColor ColorB = FusionColor(B);
	const FLinearColor White(1.0f, 0.97f, 0.92f);
	FChaosImpactFxPiece Flash;
	Flash.Look = EChaosImpactFxLook::Glow;
	Flash.Life = 0.28f;
	Flash.SizeFrom = 60.0f;
	Flash.SizeTo = 240.0f;
	Flash.Color = White;
	Flash.CoreColor = FLinearColor::White;
	Flash.Intensity = 7.0f;
	Flash.Attack = 0.1f;
	Fx->Add(Flash);
	// A ring of each colour on the ground, the two going out together, and a whirl of both colours for a moment.
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FChaosImpactFxPiece Ring;
		Ring.Look = EChaosImpactFxLook::Ring;
		Ring.Location = FVector(0.0f, 0.0f, -20.0f);
		Ring.Delay = Index * 0.05f;
		Ring.Life = 0.55f;
		Ring.SizeFrom = 80.0f;
		Ring.SizeTo = 520.0f - Index * 90.0f;
		Ring.Color = Index == 0 ? ColorA : ColorB;
		Ring.CoreColor = White;
		Ring.Intensity = 4.0f;
		Ring.RingWidth = 0.07f;
		Fx->Add(Ring);
	}
	FChaosImpactFxPiece Whirl;
	Whirl.Look = EChaosImpactFxLook::Spiral;
	Whirl.Location = FVector(0.0f, 0.0f, -15.0f);
	Whirl.Life = 0.6f;
	Whirl.SizeFrom = 160.0f;
	Whirl.SizeTo = 380.0f;
	Whirl.Color = ColorB;
	Whirl.CoreColor = ColorA;
	Whirl.Intensity = 2.5f;
	Whirl.Arms = 2.0f;
	Whirl.Twist = 1.2f;
	Whirl.Swirl = -0.9f;
	Fx->Add(Whirl);
}

void ChaosImpactFusion::PlayTrailPuff(const UObject* WorldContext, const FVector& At, const FVector& Velocity,
	const EChaosImpactBallType A, const EChaosImpactBallType B, const float Size)
{
	AChaosImpactFxKit* Fx = AChaosImpactFxKit::Spawn(const_cast<UObject*>(WorldContext), At);
	if (!Fx)
	{
		return;
	}
	// Two soft glows side by side, one of each colour, twisting round the line of flight as the ball goes on.
	const FVector Way = Velocity.GetSafeNormal();
	const FVector Side = FVector::CrossProduct(Way.IsNearlyZero() ? FVector::ForwardVector : Way, FVector::UpVector).GetSafeNormal();
	const float Turn = static_cast<float>(FMath::Fmod(Fx->GetWorld() ? Fx->GetWorld()->GetTimeSeconds() * 9.0 : 0.0, UE_TWO_PI));
	const FVector Offset = (Side * FMath::Cos(Turn) + FVector::UpVector * FMath::Sin(Turn)) * 16.0f * Size;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FChaosImpactFxPiece Puff;
		Puff.Look = EChaosImpactFxLook::Glow;
		Puff.Location = Index == 0 ? Offset : -Offset;
		Puff.Velocity = -Velocity * 0.04f;
		Puff.Drag = 2.0f;
		Puff.Life = 0.42f;
		Puff.SizeFrom = 34.0f * Size;
		Puff.SizeTo = 8.0f * Size;
		Puff.Color = Index == 0 ? FusionColor(A) : FusionColor(B);
		Puff.CoreColor = FLinearColor(1.0f, 0.97f, 0.92f);
		Puff.Intensity = 3.5f;
		Puff.Attack = 0.05f;
		Fx->Add(Puff);
	}
}
