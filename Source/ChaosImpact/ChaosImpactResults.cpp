#include "ChaosImpactResults.h"

#include "ChaosImpact.h"
#include "ChaosImpactBallTypes.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactCharacterPreview.h"
#include "ChaosImpactCharacterRoster.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactIceMeshes.h"
#include "ChaosImpactLightning.h"
#include "ChaosImpactMatchTypes.h"
#include "ChaosImpactPaint.h"
#include "ChaosImpactPuppetComponent.h"
#include "Animation/AnimSequenceBase.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"

namespace
{
	namespace ResultsPaint = ChaosImpactPaint;

	float ResultsHash(const int32 Index, const float Salt)
	{
		return FMath::Frac(FMath::Sin(Index * 12.9898f + Salt * 78.233f) * 43758.5453f);
	}

	float EaseOutPrompt(const float T)
	{
		return ChaosImpactPaint::EaseOut(T / 0.3f);
	}

	float ResultsEaseInOut(const float T)
	{
		const float X = FMath::Clamp(T, 0.0f, 1.0f);
		return X * X * (3.0f - 2.0f * X);
	}

	/** A lit, flat-coloured surface (the pedestals' bodies and the floor). */
	UMaterialInstanceDynamic* ResultsLitMaterial(UObject* Outer, const FLinearColor& Color, const float Roughness)
	{
		UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/LevelPrototyping/Materials/M_FlatCol.M_FlatCol"));
		UMaterialInstanceDynamic* Material = Parent ? UMaterialInstanceDynamic::Create(Parent, Outer) : nullptr;
		if (Material)
		{
			Material->SetVectorParameterValue(TEXT("Color"), Color);
			Material->SetVectorParameterValue(TEXT("Base Color"), Color);
			Material->SetScalarParameterValue(TEXT("Roughness"), Roughness);
		}
		return Material;
	}

	void ResultsSetIntensity(UMaterialInstanceDynamic* Material, const float Intensity)
	{
		if (Material)
		{
			Material->SetScalarParameterValue(TEXT("Intensity"), Intensity);
			Material->SetScalarParameterValue(TEXT("EmissiveIntensity"), Intensity);
		}
	}

	void AddQuad(ChaosImpactIceMeshes::FMeshBuffers& Mesh, const FVector& A, const FVector& B, const FVector& C, const FVector& D,
		const bool bBothSides)
	{
		const FVector Normal = FVector::CrossProduct(C - A, B - A).GetSafeNormal();
		const int32 Base = Mesh.Vertices.Num();
		Mesh.Vertices.Append({A, B, C, D});
		Mesh.Normals.Append({Normal, Normal, Normal, Normal});
		Mesh.UVs.Append({FVector2D(0, 0), FVector2D(1, 0), FVector2D(1, 1), FVector2D(0, 1)});
		Mesh.Triangles.Append({Base, Base + 1, Base + 2, Base, Base + 2, Base + 3});
		if (bBothSides)
		{
			Mesh.Triangles.Append({Base, Base + 2, Base + 1, Base, Base + 3, Base + 2});
		}
	}

	/** A quad seen from both sides, lit as facing Normal (a wall panel lit from the room's side). */
	void AddQuadFacing(ChaosImpactIceMeshes::FMeshBuffers& Mesh, const FVector& A, const FVector& B, const FVector& C, const FVector& D,
		const FVector& Normal)
	{
		const int32 Base = Mesh.Vertices.Num();
		Mesh.Vertices.Append({A, B, C, D});
		Mesh.Normals.Append({Normal, Normal, Normal, Normal});
		Mesh.UVs.Append({FVector2D(0, 0), FVector2D(1, 0), FVector2D(1, 1), FVector2D(0, 1)});
		Mesh.Triangles.Append({Base, Base + 1, Base + 2, Base, Base + 2, Base + 3, Base, Base + 2, Base + 1, Base, Base + 3, Base + 2});
	}

	FVector RoundPoint(const FVector& Center, const float Radius, const float Angle, const float Z)
	{
		return Center + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, Z);
	}

	/** The side of a round block (smooth, facing out) from Z0 to Z1. */
	void AppendRoundSide(ChaosImpactIceMeshes::FMeshBuffers& Mesh, const FVector& Center, const float Radius, const float Z0,
		const float Z1, const int32 Segments = 64)
	{
		const int32 First = Mesh.Vertices.Num();
		for (int32 Step = 0; Step <= Segments; ++Step)
		{
			const float Angle = 2.0f * PI * Step / Segments;
			const FVector Out(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f);
			Mesh.Vertices.Append({RoundPoint(Center, Radius, Angle, Z0), RoundPoint(Center, Radius, Angle, Z1)});
			Mesh.Normals.Append({Out, Out});
			Mesh.UVs.Append({FVector2D(Step / static_cast<float>(Segments), 0.0f), FVector2D(Step / static_cast<float>(Segments), 1.0f)});
		}
		for (int32 Step = 0; Step < Segments; ++Step)
		{
			const int32 A = First + Step * 2;
			Mesh.Triangles.Append({A, A + 1, A + 3, A, A + 3, A + 2});
		}
	}

	/** A flat round top (or floor) at height Z, facing up. */
	void AppendRoundTop(ChaosImpactIceMeshes::FMeshBuffers& Mesh, const FVector& Center, const float Radius, const float Z,
		const int32 Segments = 64)
	{
		const int32 Middle = Mesh.Vertices.Num();
		Mesh.Vertices.Add(Center + FVector(0.0f, 0.0f, Z));
		Mesh.Normals.Add(FVector::UpVector);
		Mesh.UVs.Add(FVector2D(0.5f, 0.5f));
		for (int32 Step = 0; Step <= Segments; ++Step)
		{
			const float Angle = 2.0f * PI * Step / Segments;
			Mesh.Vertices.Add(RoundPoint(Center, Radius, Angle, Z));
			Mesh.Normals.Add(FVector::UpVector);
			Mesh.UVs.Add(FVector2D(0.5f + 0.5f * FMath::Cos(Angle), 0.5f + 0.5f * FMath::Sin(Angle)));
		}
		for (int32 Step = 0; Step < Segments; ++Step)
		{
			Mesh.Triangles.Append({Middle, Middle + Step + 2, Middle + Step + 1});
		}
	}

	/** A rounded edge: a quarter turn from the side at Radius (height Z - Bevel) onto the top (radius Radius - Bevel). */
	void AppendRoundBevel(ChaosImpactIceMeshes::FMeshBuffers& Mesh, const FVector& Center, const float Radius, const float Z,
		const float Bevel, const int32 Segments = 64)
	{
		constexpr int32 Steps = 4;
		const int32 First = Mesh.Vertices.Num();
		for (int32 Step = 0; Step <= Segments; ++Step)
		{
			const float Angle = 2.0f * PI * Step / Segments;
			const FVector Out(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f);
			for (int32 Bend = 0; Bend <= Steps; ++Bend)
			{
				const float Turn = 0.5f * PI * Bend / Steps;
				const FVector Normal = (Out * FMath::Cos(Turn) + FVector::UpVector * FMath::Sin(Turn)).GetSafeNormal();
				const float R = Radius - Bevel + Bevel * FMath::Cos(Turn);
				Mesh.Vertices.Add(RoundPoint(Center, R, Angle, Z - Bevel + Bevel * FMath::Sin(Turn)));
				Mesh.Normals.Add(Normal);
				Mesh.UVs.Add(FVector2D(Step / static_cast<float>(Segments), Bend / static_cast<float>(Steps)));
			}
		}
		for (int32 Step = 0; Step < Segments; ++Step)
		{
			for (int32 Bend = 0; Bend < Steps; ++Bend)
			{
				const int32 A = First + Step * (Steps + 1) + Bend;
				const int32 B = A + Steps + 1;
				Mesh.Triangles.Append({A, A + 1, B + 1, A, B + 1, B});
			}
		}
	}

	/** Medal colours for the pedestals' rims. */
	const FLinearColor PlaceColors[] = {FLinearColor(1.0f, 0.7f, 0.24f), FLinearColor(0.7f, 0.78f, 0.92f), FLinearColor(0.92f, 0.47f, 0.22f)};

	const FLinearColor ResultsPalette[] = {
		FLinearColor(1.0f, 0.74f, 0.1f), FLinearColor(0.1f, 0.62f, 1.0f), FLinearColor(1.0f, 0.18f, 0.24f),
		FLinearColor(0.25f, 0.92f, 0.42f), FLinearColor(0.68f, 0.38f, 1.0f), FLinearColor(1.0f, 0.52f, 0.12f),
		FLinearColor(0.2f, 0.92f, 0.92f), FLinearColor(1.0f, 0.45f, 0.8f)};
}

// =====================================================================================================================
// Results data and awards
// =====================================================================================================================

FChaosImpactResultsData FChaosImpactResultsData::Capture(const AChaosImpactGameState* Match, const UObject* WorldContext)
{
	FChaosImpactResultsData Data;
	if (!Match)
	{
		return Data;
	}
	Data.bTeams = Match->IsTeamBattle();
	Data.bCrashBall = Match->Rules.bCrashBall;
	Data.bDuel = Match->IsDuel();
	Data.bBattleRoyale = Match->IsBattleRoyale();
	Data.bCrashKnockout = Match->Crash.bKnockout;
	TArray<const APlayerState*, TInlineAllocator<4>> LocalStates;
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr)
	{
		for (const ULocalPlayer* LocalPlayer : GameInstance->GetLocalPlayers())
		{
			const APlayerController* Controller = LocalPlayer ? LocalPlayer->GetPlayerController(World) : nullptr;
			LocalStates.Add(Controller ? Controller->PlayerState.Get() : nullptr);
		}
	}
	const TArray<AChaosImpactPlayerState*> Ranking = Match->GetRanking();
	TArray<int32> TeamSeen;
	for (int32 Index = 0; Index < Ranking.Num(); ++Index)
	{
		const AChaosImpactPlayerState* Member = Ranking[Index];
		FChaosImpactResultEntry Entry;
		const AChaosImpactCharacter* Character = Cast<AChaosImpactCharacter>(Member->GetPawn());
		Entry.Name = Character ? Character->GetOverheadDisplayName() : Member->GetPlayerName();
		Entry.Character = Character ? Character->GetShownCharacter() : Member->CharacterIndex;
		Entry.Colour = Character ? Character->GetShownColour() : FMath::Max(Member->ColourChoice, 0);
		Entry.CustomColours = Character ? Character->GetShownCustomColours()
			: Member->IsABot() ? TArray<uint8>() : Member->CustomColours;
		Entry.Team = Member->TeamIndex;
		Entry.bBot = Member->IsABot();
		Entry.LocalIndex = LocalStates.IndexOfByKey(Member);
		Entry.Points = Member->Points;
		Entry.Knockouts = Member->Knockouts;
		Entry.Throws = Member->Throws;
		Entry.Hits = Member->Hits;
		Entry.TimesHit = Member->TimesHit;
		Entry.Dodges = Member->Dodges;
		Entry.SpecialThrows = Member->SpecialThrows;
		Entry.DriveHits = Member->DriveHits;
		Entry.LongestHit = Member->LongestHit;
		Entry.CrashPushes = Member->CrashPushes;
		Entry.History = Member->PointsHistory;
		Entry.Rank = 1;
		for (int32 Other = 0; Other < Index; ++Other)
		{
			Entry.Rank += Ranking[Other]->Points > Member->Points ? 1 : 0;
		}
		if (Data.bDuel)
		{
			const int32 Side = Match->Duel.SideOf(Member);
			Entry.RoundsWon = Side != INDEX_NONE ? Match->Duel.Wins[Side] : 0;
			Entry.Rank = Match->Duel.MatchWinner == INDEX_NONE || Side == Match->Duel.MatchWinner ? 1 : 2;
		}
		if (Data.bBattleRoyale)
		{
			if (const auto* Royale = Match->GetRoyaleEntry(Member))
			{
				Entry.Rank = FMath::Max(1, Royale->Place);
				Entry.Character = Royale->Character; Entry.Colour = Royale->Colour; Entry.Name = Royale->Name;
				Entry.CustomColours = Royale->CustomColours;
			}
		}
		if (Data.bTeams && Entry.Team >= 0)
		{
			// Each team in its colour, its members a little lighter or darker in turn.
			const int32 Within = TeamSeen.FilterByPredicate([&Entry](const int32 Team) { return Team == Entry.Team; }).Num();
			TeamSeen.Add(Entry.Team);
			Entry.Color = ChaosImpactMatch::GetTeamColor(Entry.Team) * (1.0f - 0.18f * Within) + FLinearColor(0.12f, 0.12f, 0.12f) * Within * 0.5f;
			Entry.Color.A = 1.0f;
		}
		else
		{
			Entry.Color = ResultsPalette[Index % UE_ARRAY_COUNT(ResultsPalette)];
		}
		Data.Entries.Add(Entry);
	}
	if (Data.bBattleRoyale)
	{
		// Keep departed entrants in the results too; their replicated snapshot survives the connection.
		for (const auto& Royale : Match->Royale.Entries)
		{
			if (!Ranking.Contains(Royale.Member.Get()))
			{
				FChaosImpactResultEntry Entry;
				Entry.Name = Royale.Name; Entry.Character = Royale.Character; Entry.Colour = Royale.Colour;
				Entry.CustomColours = Royale.CustomColours;
				Entry.Team = Royale.Team; Entry.Rank = FMath::Max(1, Royale.Place);
				Entry.Color = Data.bTeams ? ChaosImpactMatch::GetTeamColor(Entry.Team) : ResultsPalette[Data.Entries.Num() % UE_ARRAY_COUNT(ResultsPalette)];
				Data.Entries.Add(Entry);
			}
		}
		Data.Entries.StableSort([](const auto& A, const auto& B) { return A.Rank < B.Rank; });
	}
	if (Data.bTeams)
	{
		for (int32 Team = 0; Team < Match->Rules.TeamCount; ++Team)
		{
			Data.TeamTotals.Add({Team, Data.bBattleRoyale && Match->Royale.TeamPlaces.IsValidIndex(Team) ? Match->Royale.TeamPlaces[Team]
				: Data.bCrashBall ? 100 - Match->GetCrashCount(Team) : Match->GetTeamPoints(Team)});
		}
		Data.TeamTotals.StableSort([&Data](const TPair<int32, int32>& A, const TPair<int32, int32>& B) { return Data.bBattleRoyale ? A.Value < B.Value : A.Value > B.Value; });
		Data.bTie = Data.TeamTotals.Num() > 1 && Data.TeamTotals[0].Value == Data.TeamTotals[1].Value;
		const int32 Winner = Data.TeamTotals.IsEmpty() ? 0 : Data.TeamTotals[0].Key;
		Data.WinnerColor = ChaosImpactMatch::GetTeamColor(Winner);
		Data.WinnerName = FString::Printf(TEXT("%sチーム"), ChaosImpactMatch::GetTeamName(Winner));
		Data.Headline = Data.bTie ? FString(Data.bBattleRoyale ? TEXT("同率1位") : TEXT("DRAW")) : FString(TEXT("WINNER"));
	}
	else
	{
		Data.bTie = Data.bDuel ? Match->Duel.MatchWinner == INDEX_NONE
			: Data.bBattleRoyale ? Data.Entries.Num() > 1 && Data.Entries[1].Rank == 1
			: Data.Entries.Num() > 1 && Data.Entries[0].Points == Data.Entries[1].Points;
		Data.WinnerColor = Data.Entries.IsEmpty() ? FLinearColor::White : ChaosImpactRoster::GetColourSwatch(Data.Entries[0].Colour);
		Data.WinnerName = Data.Entries.IsEmpty() ? FString() : Data.Entries[0].Name;
		Data.Headline = Data.bTie ? FString(Data.bBattleRoyale ? TEXT("同率1位") : TEXT("DRAW")) : FString(TEXT("WINNER"));
	}
	return Data;
}

void FChaosImpactResultsData::ChooseAwards()
{
	using FEntry = FChaosImpactResultEntry;
	struct FAward
	{
		const TCHAR* Title;
		int32 Icon;
		TFunction<float(const FEntry&)> Value;
		TFunction<bool(const FEntry&)> Eligible;
		TFunction<FString(const FEntry&)> Note;
		bool bLowerIsBetter = false;
	};
	const auto LateGain = [](const FEntry& Entry)
	{
		// Points won over the last third of the match.
		const int32 Count = Entry.History.Num();
		if (Count < 4)
		{
			return 0;
		}
		return Entry.History.Last() - Entry.History[FMath::Clamp(Count * 2 / 3, 0, Count - 1)];
	};
	const TArray<FAward> Awards = {
		{TEXT("押し込み王"), 6, [](const FEntry& E) { return static_cast<float>(E.CrashPushes); },
			[](const FEntry& E) { return E.CrashPushes >= 3; }, [](const FEntry& E) { return FString::Printf(TEXT("巨大球を押した %d回"), E.CrashPushes); }},
		{TEXT("KOキング"), 0, [](const FEntry& E) { return static_cast<float>(E.Knockouts); },
			[](const FEntry& E) { return E.Knockouts > 0; }, [](const FEntry& E) { return FString::Printf(TEXT("KO ×%d"), E.Knockouts); }},
		{TEXT("スナイパー"), 1, [](const FEntry& E) { return static_cast<float>(E.LongestHit); },
			[](const FEntry& E) { return E.LongestHit >= 600; },
			[](const FEntry& E) { return FString::Printf(TEXT("%.1fm先の相手に命中"), E.LongestHit / 100.0f); }},
		{TEXT("回避王"), 2, [](const FEntry& E) { return static_cast<float>(E.Dodges); },
			[](const FEntry& E) { return E.Dodges > 0; }, [](const FEntry& E) { return FString::Printf(TEXT("ダッシュでよけた %d回"), E.Dodges); }},
		{TEXT("精密射撃"), 3, [](const FEntry& E) { return E.Throws > 0 ? FMath::Min(static_cast<float>(E.Hits) / E.Throws, 1.0f) : 0.0f; },
			[](const FEntry& E) { return E.Throws >= 4 && E.Hits > 0; },
			[](const FEntry& E) { return FString::Printf(TEXT("命中率 %d%%"), FMath::Min(100, FMath::RoundToInt(100.0f * E.Hits / FMath::Max(E.Throws, 1)))); }},
		{TEXT("ドライブ職人"), 4, [](const FEntry& E) { return static_cast<float>(E.DriveHits); },
			[](const FEntry& E) { return E.DriveHits > 0; }, [](const FEntry& E) { return FString::Printf(TEXT("ドライブ命中 %d回"), E.DriveHits); }},
		{TEXT("特殊ボールマスター"), 5, [](const FEntry& E) { return static_cast<float>(E.SpecialThrows); },
			[](const FEntry& E) { return E.SpecialThrows >= 2; }, [](const FEntry& E) { return FString::Printf(TEXT("特殊ボール %d回"), E.SpecialThrows); }},
		{TEXT("ラストスパート"), 6, [LateGain](const FEntry& E) { return static_cast<float>(LateGain(E)); },
			[LateGain](const FEntry& E) { return LateGain(E) >= 2; }, [LateGain](const FEntry& E) { return FString::Printf(TEXT("終盤に +%d pt"), LateGain(E)); }},
		{TEXT("鉄壁"), 7, [](const FEntry& E) { return static_cast<float>(E.TimesHit); },
			[](const FEntry& E) { return E.Throws > 0 || E.Points > 0; }, [](const FEntry& E) { return FString::Printf(TEXT("被弾 %d回"), E.TimesHit); }, true},
		{TEXT("投げまくり"), 8, [](const FEntry& E) { return static_cast<float>(E.Throws); },
			[](const FEntry& E) { return E.Throws > 0; }, [](const FEntry& E) { return FString::Printf(TEXT("投げた %d回"), E.Throws); }},
	};
	for (const FAward& Award : Awards)
	{
		// Only the best at it (or one tied for best) gets it, and only if they have no award yet.
		float Best = Award.bLowerIsBetter ? TNumericLimits<float>::Max() : -TNumericLimits<float>::Max();
		bool bAny = false;
		for (const FEntry& Entry : Entries)
		{
			if (Award.Eligible(Entry))
			{
				const float Value = Award.Value(Entry);
				Best = Award.bLowerIsBetter ? FMath::Min(Best, Value) : FMath::Max(Best, Value);
				bAny = true;
			}
		}
		if (!bAny)
		{
			continue;
		}
		for (FEntry& Entry : Entries)
		{
			if (Entry.AwardTitle.IsEmpty() && Award.Eligible(Entry) && FMath::IsNearlyEqual(Award.Value(Entry), Best))
			{
				Entry.AwardTitle = Award.Title;
				Entry.AwardNote = Award.Note(Entry);
				Entry.AwardIcon = Award.Icon;
				break;
			}
		}
	}
	static const TCHAR* const Kind[] = {TEXT("ナイスファイト"), TEXT("がんばった賞"), TEXT("次こそ！")};
	int32 Next = 0;
	for (FEntry& Entry : Entries)
	{
		if (Entry.AwardTitle.IsEmpty())
		{
			Entry.AwardTitle = Kind[Next++ % UE_ARRAY_COUNT(Kind)];
			Entry.AwardNote = FString::Printf(TEXT("%d pt・おつかれさま！"), Entry.Points);
			Entry.AwardIcon = 9;
		}
	}
}

// =====================================================================================================================
// Podium stage
// =====================================================================================================================

AChaosImpactPodiumStage::AChaosImpactPodiumStage()
{
	PrimaryActorTick.bCanEverTick = false;
	SetReplicates(false);
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Camera = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Camera"));
	Camera->SetupAttachment(Root);
	Camera->PrimaryComponentTick.bTickEvenWhenPaused = true;
	Camera->bCaptureEveryFrame = true;
	Camera->bCaptureOnMovement = false;
	Camera->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	Camera->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	Camera->ShowFlags.SetAtmosphere(false);
	Camera->ShowFlags.SetFog(false);
	Camera->ShowFlags.SetMotionBlur(false);
	Camera->PostProcessSettings.bOverride_AutoExposureMethod = true;
	Camera->PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
	Camera->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	Camera->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;
	Camera->PostProcessSettings.bOverride_AutoExposureBias = true;
	Camera->PostProcessSettings.AutoExposureBias = 0.0f;
	Camera->PostProcessSettings.bOverride_BloomIntensity = true;
	Camera->PostProcessSettings.BloomIntensity = 1.4f;
	Camera->PostProcessSettings.bOverride_VignetteIntensity = true;
	Camera->PostProcessSettings.VignetteIntensity = 0.75f;

	KeyLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("KeyLight"));
	KeyLight->SetupAttachment(Root);
	KeyLight->SetRelativeLocation(FVector(-260.0f, 620.0f, 420.0f));
	KeyLight->SetIntensity(26000.0f);
	KeyLight->SetAttenuationRadius(2400.0f);
	KeyLight->SetCastShadows(false);

	BackGlowLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("BackGlowLight"));
	BackGlowLight->SetupAttachment(Root);
	BackGlowLight->SetRelativeLocation(FVector(0.0f, -420.0f, 320.0f));
	BackGlowLight->SetIntensity(30000.0f);
	BackGlowLight->SetAttenuationRadius(1500.0f);
	BackGlowLight->SetCastShadows(false);

	WinnerSpot = CreateDefaultSubobject<USpotLightComponent>(TEXT("WinnerSpot"));
	WinnerSpot->SetupAttachment(Root);
	WinnerSpot->SetRelativeLocation(FVector(0.0f, 120.0f, 780.0f));
	WinnerSpot->SetRelativeRotation(FRotator(-82.0f, -90.0f, 0.0f));
	WinnerSpot->SetInnerConeAngle(12.0f);
	WinnerSpot->SetOuterConeAngle(22.0f);
	WinnerSpot->SetAttenuationRadius(1600.0f);
	WinnerSpot->SetIntensity(0.0f);
	WinnerSpot->SetCastShadows(true);
}

void AChaosImpactPodiumStage::Destroyed()
{
	for (AChaosImpactCharacterPreview* Figure : FigureActors)
	{
		if (IsValid(Figure))
		{
			Figure->Destroy();
		}
	}
	FigureActors.Reset();
	Super::Destroyed();
}

void AChaosImpactPodiumStage::AddPedestal(const float Radius, const float Height)
{
	// A round pedestal: a satin body on a recessed foot, a rounded top edge, and thin gold rims of light.
	const FVector Base = FVector::ZeroVector;
	const FLinearColor Neon = PlaceColors[0];
	ChaosImpactIceMeshes::FMeshBuffers Body;
	AppendRoundSide(Body, Base, Radius - 5.0f, 0.0f, 5.0f);
	AppendRoundSide(Body, Base, Radius, 5.0f, Height - 4.0f);
	ChaosImpactIceMeshes::CreateComponent(this, Root, Body, ResultsLitMaterial(this, FLinearColor(0.055f, 0.058f, 0.068f), 0.34f));
	ChaosImpactIceMeshes::FMeshBuffers Top;
	AppendRoundBevel(Top, Base, Radius, Height, 4.0f);
	AppendRoundTop(Top, Base, Radius - 4.0f, Height);
	ChaosImpactIceMeshes::CreateComponent(this, Root, Top, ResultsLitMaterial(this, FLinearColor(0.1f, 0.102f, 0.11f), 0.3f));
	ChaosImpactIceMeshes::FMeshBuffers Rim;
	AppendRoundSide(Rim, Base, Radius + 0.4f, Height - 11.0f, Height - 8.0f);
	AppendRoundSide(Rim, Base, Radius + 0.4f, 9.0f, 10.5f);
	NeonMaterial = ChaosImpactBallTypes::MakeEmissive(this, Neon, 0.0f);
	ChaosImpactIceMeshes::CreateComponent(this, Root, Rim, NeonMaterial);

	// The spotlight from high above, a little in front, so the faces are lit and the shadow falls behind; wide enough
	// for a whole team on the pedestal.
	const FVector SpotAt = Base + FVector(0.0f, 260.0f, 820.0f);
	WinnerSpot->SetRelativeLocation(SpotAt);
	WinnerSpot->SetRelativeRotation((Base + FVector(0.0f, 0.0f, Height + 60.0f) - SpotAt).Rotation());
	const float Cone = FMath::RadiansToDegrees(FMath::Atan2(Radius + 60.0f, 820.0f));
	WinnerSpot->SetInnerConeAngle(FMath::Max(12.0f, Cone * 0.7f));
	WinnerSpot->SetOuterConeAngle(FMath::Max(22.0f, Cone + 6.0f));

	// A soft rim light behind them, so the figures stand off the wall.
	RimLight = NewObject<UPointLightComponent>(this);
	RimLight->SetupAttachment(Root);
	RimLight->SetRelativeLocation(Base + FVector(0.0f, -170.0f, Height + 230.0f));
	RimLight->SetLightColor(FMath::Lerp(Neon, FLinearColor::White, 0.5f));
	RimLight->SetIntensity(0.0f);
	RimLight->SetAttenuationRadius(600.0f + Radius);
	RimLight->SetCastShadows(false);
	RimLight->RegisterComponent();
}

void AChaosImpactPodiumStage::Build(const FChaosImpactResultsData& Data)
{
	BeginBuild(Data);
	while (!BuildStep())
	{
	}
}

void AChaosImpactPodiumStage::BeginBuild(const FChaosImpactResultsData& Data)
{
	for (AChaosImpactCharacterPreview* Figure : FigureActors)
	{
		if (IsValid(Figure))
		{
			Figure->Destroy();
		}
	}
	FigureActors.Reset();
	Figures.Reset();
	BuildData = Data;
	BuildStage = 0;
	BuildMember = 0;
}

bool AChaosImpactPodiumStage::BuildStep()
{
	UWorld* World = GetWorld();
	if (!World || BuildStage < 0)
	{
		return true;
	}
	if (BuildStage == 0)
	{
		BuildArena();
		BuildStage = 1;
		return false;
	}
	if (BuildStage == 1)
	{
		// One figure (and the pedestal, with the first) a step.
		if (BuildMember < FMath::Max(Winners.Num(), 1))
		{
			BuildNextFigure();
			return false;
		}
		// The shockwave the winner lands with.
		ShockwaveMaterial = ChaosImpactBallTypes::MakeAdditive(this, FLinearColor(1.0f, 0.95f, 0.85f), 0.0f);
		Shockwave = ChaosImpactLightning::CreateComponent(this, Root, ShockwaveMaterial);
		BuildStage = -1;
		// Everything drawn once now, before it is shown (its shaders are ready by then).
		UpdateShow(-1.0f, 0.0f);
		return true;
	}
	return true;
}

void AChaosImpactPodiumStage::BuildArena()
{
	const FChaosImpactResultsData& Data = BuildData;
	WinnerColor = Data.WinnerColor;
	Picture = NewObject<UTextureRenderTarget2D>(this);
	Picture->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8_SRGB;
	Picture->ClearColor = FLinearColor::Black;
	Picture->InitAutoFormat(PictureWidth, PictureHeight);
	Picture->UpdateResourceImmediate(true);
	Camera->TextureTarget = Picture;
	Camera->ShowOnlyActors.Add(this);
	BackGlowLight->SetLightColor(FMath::Lerp(WinnerColor, FLinearColor::White, 0.4f));

	IdleAnimation = LoadObject<UAnimSequenceBase>(nullptr, TEXT("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle.MM_Idle"));
	FallAnimation = LoadObject<UAnimSequenceBase>(nullptr, TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Jump/MM_Fall_Loop.MM_Fall_Loop"));
	LandAnimation = LoadObject<UAnimSequenceBase>(nullptr, TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Jump/MM_Land.MM_Land"));

	// The floor: dark and glossy, with the round stage on it (a soft line of light along its edge).
	ChaosImpactIceMeshes::FMeshBuffers Floor;
	AppendRoundTop(Floor, FVector(0.0f, -250.0f, 0.0f), 3200.0f, -1.0f, 96);
	ChaosImpactIceMeshes::CreateComponent(this, Root, Floor, ResultsLitMaterial(this, FLinearColor(0.006f, 0.007f, 0.01f), 0.12f));
	const FVector StageCenter(0.0f, -230.0f, 0.0f);
	ChaosImpactIceMeshes::FMeshBuffers Stage;
	AppendRoundSide(Stage, StageCenter, 600.0f, -1.0f, 6.0f, 128);
	AppendRoundBevel(Stage, StageCenter, 600.0f, 8.0f, 2.0f, 128);
	AppendRoundTop(Stage, StageCenter, 598.0f, 8.0f, 128);
	ChaosImpactIceMeshes::CreateComponent(this, Root, Stage, ResultsLitMaterial(this, FLinearColor(0.02f, 0.021f, 0.026f), 0.22f));
	ChaosImpactIceMeshes::FMeshBuffers Edge;
	AppendRoundSide(Edge, StageCenter, 600.6f, 2.0f, 4.0f, 128);
	StageEdgeMaterial = ChaosImpactBallTypes::MakeEmissive(this, FLinearColor(1.0f, 0.9f, 0.75f), 0.0f);
	ChaosImpactIceMeshes::CreateComponent(this, Root, Edge, StageEdgeMaterial);

	// The back wall: a curve of tall panels with narrow seams, slim LED bars in some seams, washed upward from its foot.
	const FVector WallCenter(0.0f, -260.0f, 0.0f);
	constexpr float WallRadius = 1180.0f;
	constexpr float WallHeight = 760.0f;
	constexpr int32 Panels = 30;
	constexpr float WallSpan = 150.0f;
	const auto WallAngle = [](const float Along) { return FMath::DegreesToRadians(-90.0f - WallSpan * 0.5f + WallSpan * Along); };
	ChaosImpactIceMeshes::FMeshBuffers Wall;
	const FLinearColor LedColor = FMath::Lerp(WinnerColor, FLinearColor::White, 0.25f);
	for (int32 Panel = 0; Panel < Panels; ++Panel)
	{
		const float A0 = WallAngle(Panel / static_cast<float>(Panels)) + 0.0045f;
		const float A1 = WallAngle((Panel + 1) / static_cast<float>(Panels)) - 0.0045f;
		const float Mid = (A0 + A1) * 0.5f;
		AddQuadFacing(Wall, RoundPoint(WallCenter, WallRadius, A1, 0.0f), RoundPoint(WallCenter, WallRadius, A0, 0.0f),
			RoundPoint(WallCenter, WallRadius, A0, WallHeight), RoundPoint(WallCenter, WallRadius, A1, WallHeight),
			-FVector(FMath::Cos(Mid), FMath::Sin(Mid), 0.0f));
		if (Panel % 3 == 0 && Panel > 0)
		{
			// An LED bar in the seam, standing a little proud of it.
			const float Seam = WallAngle(Panel / static_cast<float>(Panels));
			const FVector Foot = RoundPoint(WallCenter, WallRadius - 3.0f, Seam, 0.0f);
			const FVector Across = FVector(-FMath::Sin(Seam), FMath::Cos(Seam), 0.0f) * 3.0f;
			ChaosImpactIceMeshes::FMeshBuffers Bar;
			AddQuad(Bar, Foot + Across + FVector(0.0f, 0.0f, 40.0f), Foot - Across + FVector(0.0f, 0.0f, 40.0f),
				Foot - Across + FVector(0.0f, 0.0f, WallHeight - 70.0f), Foot + Across + FVector(0.0f, 0.0f, WallHeight - 70.0f), true);
			UMaterialInstanceDynamic* LedMaterial = ChaosImpactBallTypes::MakeEmissive(this, LedColor, 0.0f);
			ChaosImpactIceMeshes::CreateComponent(this, Root, Bar, LedMaterial);
			LedMaterials.Add(LedMaterial);
			LedPhases.Add(Panel / static_cast<float>(Panels));
		}
	}
	ChaosImpactIceMeshes::CreateComponent(this, Root, Wall, ResultsLitMaterial(this, FLinearColor(0.11f, 0.115f, 0.13f), 0.6f));
	{
		// A thin band of light along the wall's top.
		ChaosImpactIceMeshes::FMeshBuffers Band;
		constexpr int32 Steps = 60;
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			const float A0 = WallAngle(Step / static_cast<float>(Steps));
			const float A1 = WallAngle((Step + 1) / static_cast<float>(Steps));
			AddQuad(Band, RoundPoint(WallCenter, WallRadius - 4.0f, A1, WallHeight - 40.0f), RoundPoint(WallCenter, WallRadius - 4.0f, A0, WallHeight - 40.0f),
				RoundPoint(WallCenter, WallRadius - 4.0f, A0, WallHeight - 36.0f), RoundPoint(WallCenter, WallRadius - 4.0f, A1, WallHeight - 36.0f), true);
		}
		UMaterialInstanceDynamic* BandMaterial = ChaosImpactBallTypes::MakeEmissive(this, LedColor, 0.0f);
		ChaosImpactIceMeshes::CreateComponent(this, Root, Band, BandMaterial);
		LedMaterials.Add(BandMaterial);
		LedPhases.Add(-1.0f);
	}
	// Lights at the wall's foot, aimed up it: pools of the winner's colour, with cooler white between.
	for (int32 Light = 0; Light < 7; ++Light)
	{
		const float Angle = FMath::DegreesToRadians(-90.0f + (Light - 3) * 19.0f);
		const FVector Foot = RoundPoint(WallCenter, WallRadius - 110.0f, Angle, 6.0f);
		const FVector Aim = RoundPoint(WallCenter, WallRadius, Angle, 560.0f);
		USpotLightComponent* Up = NewObject<USpotLightComponent>(this);
		Up->SetupAttachment(Root);
		Up->SetRelativeLocation(Foot);
		Up->SetRelativeRotation((Aim - Foot).Rotation());
		Up->SetInnerConeAngle(6.0f);
		Up->SetOuterConeAngle(30.0f);
		Up->SetAttenuationRadius(1100.0f);
		Up->SetCastShadows(false);
		Up->SetLightColor(Light % 2 == 0 ? WinnerColor : FLinearColor(0.72f, 0.8f, 1.0f));
		Up->SetIntensity(0.0f);
		Up->RegisterComponent();
		WallLights.Add(Up);
	}

	// Paper confetti, one mesh per colour (filled in as it falls).
	const FLinearColor Papers[] = {FLinearColor(1.0f, 0.78f, 0.25f), FLinearColor(0.95f, 0.95f, 0.97f), WinnerColor,
		FLinearColor(0.35f, 0.7f, 1.0f), FLinearColor(1.0f, 0.35f, 0.45f)};
	for (const FLinearColor& Paper : Papers)
	{
		Confetti.Add(ChaosImpactLightning::CreateComponent(this, Root, ResultsLitMaterial(this, Paper, 0.55f)));
	}

	// Who stands on it: the winner, the winning team, or everyone tied for first (no one else; the rest are on the
	// numbers page).
	Winners.Reset();
	for (int32 Entry = 0; Entry < Data.Entries.Num() && Winners.Num() < MaxFigures; ++Entry)
	{
		const FChaosImpactResultEntry& Item = Data.Entries[Entry];
		bool bWinner = Item.Rank == 1;
		if (Data.bTeams && !Data.TeamTotals.IsEmpty())
		{
			bWinner = false;
			for (const TPair<int32, int32>& Team : Data.TeamTotals)
			{
				bWinner |= Team.Value == Data.TeamTotals[0].Value && Team.Key == Item.Team;
			}
		}
		if (bWinner)
		{
			Winners.Add(Entry);
		}
	}
	if (Winners.IsEmpty() && !Data.Entries.IsEmpty())
	{
		Winners.Add(0);
	}
}

float AChaosImpactPodiumStage::GetRowHalfWidth() const
{
	return 66.0f * (FMath::Max(Winners.Num(), 1) - 1) + 110.0f;
}

void AChaosImpactPodiumStage::BuildNextFigure()
{
	const FChaosImpactResultsData& Data = BuildData;
	UWorld* World = GetWorld();
	constexpr float Height = 72.0f;
	constexpr float Spacing = 132.0f;
	const int32 Count = FMath::Max(Winners.Num(), 1);
	if (BuildMember == 0)
	{
		// Room for each figure (arms and all), whatever the team's size.
		AddPedestal(92.0f + Spacing * 0.5f * (Count - 1), Height);
		WinnerTopZ = Height;
	}
	if (Winners.IsValidIndex(BuildMember) && World)
	{
		const int32 Member = BuildMember;
		FFigure Figure;
		Figure.Entry = Winners[Member];
		Figure.Spot = FVector((Member - (Count - 1) * 0.5f) * Spacing, 0.0f, Height);
		const FChaosImpactResultEntry& Entry = Data.Entries[Figure.Entry];
		FActorSpawnParameters Parameters;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Parameters.ObjectFlags |= RF_Transient;
		if (AChaosImpactCharacterPreview* Actor = World->SpawnActor<AChaosImpactCharacterPreview>(
			AChaosImpactCharacterPreview::StaticClass(), GetActorLocation() + Figure.Spot, FRotator::ZeroRotator, Parameters))
		{
			Actor->ShowLoadout(Entry.Character, Entry.Colour, PlaceColors[0]);
			Actor->ShowCustomColours(Entry.CustomColours);
			Actor->UseAsFigure();
			// Turned a touch toward the camera, which films from the left (the open side, where the headline goes).
			Actor->SetFacing(8.0f, 7.0f);
			Actor->Animate(0.0f);
			Camera->ShowOnlyActors.Add(Actor);
			FigureActors.Add(Actor);
			Figures.Add(Figure);
		}
	}
	++BuildMember;
}

void AChaosImpactPodiumStage::SetCamera(const FVector& Location, const FVector& LookAt, const float Fov)
{
	CameraLocation = Location;
	CameraRotation = (LookAt - Location).Rotation();
	CameraFov = Fov;
	Camera->SetRelativeLocationAndRotation(Location, CameraRotation);
	Camera->FOVAngle = Fov;
}

void AChaosImpactPodiumStage::UpdateShow(const float Seconds, const float DeltaSeconds)
{
	// Before the show (negative seconds) every part is drawn once, off screen, so nothing is drawn for the first time
	// (and its shaders made) at the moment it appears.
	const bool bWarming = Seconds < 0.0f;
	ShowSeconds = FMath::Max(Seconds, 0.0f);
	const float S = bWarming ? LandAt + 0.1f : Seconds;

	// ---- Camera: wide on the empty stage, low on the winner's landing, then settled with the winner on the right ----
	// The settled shot: the winner (the whole row of them) in the right part of the picture, head to pedestal top, and
	// the left open for the headline. A wider row stands the camera further back.
	constexpr float FinalFov = 30.0f;
	const float TanHalf = FMath::Tan(FMath::DegreesToRadians(FinalFov * 0.5f));
	const float RowHalf = GetRowHalfWidth();
	const float Distance = FMath::Max(1050.0f, RowHalf / (0.25f * 2.0f * TanHalf));
	// (The row's middle at 0.7 of the picture's width, so it stays clear of the text down to 0.45.)
	const float Shift = 0.2f * 2.0f * TanHalf * Distance;
	const FVector Final(-Shift, Distance, 150.0f);
	const FVector FinalLook(-Shift, 0.0f, 205.0f);
	const float Open = FMath::Clamp(S / BlackoutAt, 0.0f, 1.0f);
	const FVector Wide(-Shift * 0.5f, Distance * 1.3f - 160.0f * Open, 250.0f);
	const FVector WideLook(0.0f, -260.0f, 150.0f);
	const FVector Low(120.0f, 300.0f + (RowHalf - 110.0f), 34.0f);
	const FVector LowLook(0.0f, 0.0f, WinnerTopZ + 120.0f);
	FVector Location;
	FVector Look;
	float Fov;
	if (S < BlackoutAt)
	{
		Location = Wide;
		Look = WideLook;
		Fov = 36.0f;
	}
	else if (S < LandAt + 0.6f)
	{
		Location = Low + FVector(-8.0f * (S - BlackoutAt), 0.0f, 0.0f);
		Look = LowLook;
		Fov = 46.0f;
	}
	else
	{
		const float Pull = ResultsEaseInOut((S - LandAt - 0.6f) / 1.5f);
		Location = FMath::Lerp(Low, Final, Pull);
		Look = FMath::Lerp(LowLook, FinalLook, Pull);
		Fov = FMath::Lerp(46.0f, FinalFov, Pull);
		// Settled, it drifts a little so the picture stays alive (sideways, and a slow breath in and out).
		const float Drift = S - LandAt - 2.1f;
		if (Drift > 0.0f)
		{
			const float In = FMath::Min(Drift / 1.5f, 1.0f);
			const FVector Sway(FMath::Sin(Drift * 0.35f) * 36.0f, -FMath::Sin(Drift * 0.22f) * 70.0f, FMath::Sin(Drift * 0.27f) * 12.0f);
			Location += Sway * In;
			Look += FVector(Sway.X * 0.4f, 0.0f, 0.0f) * In;
		}
	}
	// The landing shakes it.
	const float SinceLand = S - LandAt;
	if (SinceLand > 0.0f && SinceLand < 0.4f)
	{
		const float Amount = 14.0f * (1.0f - SinceLand / 0.4f);
		Location += FVector(FMath::Sin(SinceLand * 90.0f), 0.0f, FMath::Cos(SinceLand * 77.0f)) * Amount;
	}
	SetCamera(Location, Look, Fov);

	// ---- Lights: dark until the winner lands, then the spotlight crashes on and the wall glows up ----
	ResultsSetIntensity(NeonMaterial, SinceLand < 0.0f ? (S < BlackoutAt ? 0.6f : 0.0f) : 2.2f + 9.0f * FMath::Exp(-7.0f * SinceLand));
	if (RimLight)
	{
		RimLight->SetIntensity(SinceLand < 0.0f ? 0.0f : 16000.0f);
	}
	WinnerSpot->SetIntensity(SinceLand < 0.0f ? 0.0f : 42000.0f + 80000.0f * FMath::Exp(-5.0f * SinceLand));
	KeyLight->SetIntensity(S < BlackoutAt ? 16000.0f : SinceLand < 0.0f ? 4000.0f : 20000.0f);
	BackGlowLight->SetIntensity(SinceLand < 0.0f ? (S < BlackoutAt ? 5000.0f : 1500.0f) : 9000.0f);
	const float WallUp = S < BlackoutAt ? 0.45f : SinceLand < 0.0f ? 0.12f : 1.0f + 1.2f * FMath::Exp(-3.0f * SinceLand);
	for (int32 Index = 0; Index < WallLights.Num(); ++Index)
	{
		WallLights[Index]->SetIntensity(WallUp * (Index % 2 == 0 ? 130000.0f : 60000.0f));
	}
	ResultsSetIntensity(StageEdgeMaterial, S < BlackoutAt ? 1.4f : SinceLand < 0.0f ? 0.4f : 2.2f);
	// The LED bars: steady, with a slow, soft wave across the wall once the winner is in.
	for (int32 Index = 0; Index < LedMaterials.Num(); ++Index)
	{
		const float Phase = LedPhases.IsValidIndex(Index) ? LedPhases[Index] : 0.0f;
		const float Wave = Phase < 0.0f ? 1.0f : 0.75f + 0.25f * FMath::Sin(S * 1.6f - Phase * 9.0f);
		const float Level = S < BlackoutAt ? 1.2f : SinceLand < 0.0f ? 0.3f : 2.6f + 6.0f * FMath::Exp(-4.0f * SinceLand);
		ResultsSetIntensity(LedMaterials[Index], Level * Wave);
	}
	if (!Confetti.IsEmpty())
	{
		UpdateConfetti(bWarming ? 3.0f : SinceLand);
	}

	// ---- The shockwave ----
	if (Shockwave)
	{
		ChaosImpactIceMeshes::FMeshBuffers Ring;
		if (SinceLand > 0.0f && SinceLand < 0.7f)
		{
			const float T = SinceLand / 0.7f;
			const float Radius = 60.0f + 700.0f * ResultsPaint::EaseOut(T);
			ChaosImpactLightning::AppendRing(Ring, FVector(0.0f, 0.0f, 2.0f), Radius, 26.0f * (1.0f - T) + 3.0f, 64);
			ChaosImpactLightning::AppendRing(Ring, FVector(0.0f, 0.0f, WinnerTopZ + 2.0f), Radius * 0.45f, 10.0f * (1.0f - T) + 2.0f, 48);
			ResultsSetIntensity(ShockwaveMaterial, 3.0f * (1.0f - T));
		}
		ChaosImpactLightning::SetMesh(Shockwave, Ring);
	}

	// ---- The figures ----
	for (int32 Index = 0; Index < Figures.Num() && Index < FigureActors.Num(); ++Index)
	{
		FFigure& Figure = Figures[Index];
		AChaosImpactCharacterPreview* Actor = FigureActors[Index];
		if (!IsValid(Actor))
		{
			continue;
		}
		if (bWarming)
		{
			// Seen once in each of its poses' places, then hidden again for its entrance.
			Actor->SetActorHiddenInGame(false);
			Actor->SetActorLocation(GetActorLocation() + Figure.Spot);
			Actor->Animate(DeltaSeconds);
			continue;
		}
		if (S < DropAt)
		{
			Actor->SetActorHiddenInGame(true);
			continue;
		}
		if (!Figure.bShown)
		{
			Figure.bShown = true;
			Actor->SetActorHiddenInGame(false);
			Actor->PlayFigureAnimation(FallAnimation, true);
		}
		// The winner drops in from high above, faster and faster, and lands on the pedestal.
		FVector Spot = Figure.Spot;
		const float T = FMath::Clamp((S - DropAt) / (LandAt - DropAt), 0.0f, 1.0f);
		Spot.Z += 760.0f * (1.0f - T * T);
		if (T >= 1.0f && !Figure.bLanded)
		{
			Figure.bLanded = true;
			Actor->PlayFigureAnimation(LandAnimation, false);
			Figure.NextCheerAt = S + 0.45f;
		}
		if (Figure.bLanded && Figure.NextCheerAt > 0.0f && S >= Figure.NextCheerAt)
		{
			// Landed: the celebration (ChaosImpactVictoryMotion) over the idle clip.
			Actor->PlayFigureAnimation(IdleAnimation, true);
			Actor->SetVictoryMotion(true);
			Figure.NextCheerAt = -1.0f;
		}
		Actor->SetActorLocation(GetActorLocation() + Spot);
		Actor->Animate(DeltaSeconds);
	}
}

void AChaosImpactPodiumStage::UpdateConfetti(const float Seconds)
{
	// Pieces of paper thrown up over the podium as the winner lands: each falls at its own pace, fluttering and
	// turning, and lies flat where it lands. (No confetti for a draw.)
	constexpr int32 Pieces = 260;
	TArray<ChaosImpactIceMeshes::FMeshBuffers> Meshes;
	Meshes.SetNum(Confetti.Num());
	if (Seconds > 0.0f && !BuildData.bTie)
	{
		for (int32 Piece = 0; Piece < Pieces; ++Piece)
		{
			const float Delay = ResultsHash(Piece, 9.0f) * 1.2f;
			const float T = Seconds - Delay;
			if (T <= 0.0f)
			{
				continue;
			}
			const FVector Start(ResultsHash(Piece, 1.0f) * 1300.0f - 650.0f, ResultsHash(Piece, 2.0f) * 900.0f - 650.0f,
				700.0f + ResultsHash(Piece, 3.0f) * 300.0f);
			const float Fall = 90.0f + 70.0f * ResultsHash(Piece, 4.0f);
			const float Height = Start.Z - Fall * T;
			const bool bDown = Height <= 1.0f;
			const float Flight = bDown ? Start.Z / Fall : T;
			const float Sway = 18.0f + 22.0f * ResultsHash(Piece, 5.0f);
			const float Rate = 2.0f + 2.5f * ResultsHash(Piece, 6.0f);
			const FVector Where = Start + FVector(FMath::Sin(Flight * Rate + Piece) * Sway, FMath::Cos(Flight * Rate * 0.8f + Piece) * Sway * 0.6f,
				0.0f);
			const FVector At(Where.X, Where.Y, bDown ? 0.6f + 0.3f * ResultsHash(Piece, 7.0f) : Height);
			// Turning as it flutters; flat once down.
			const FQuat Turn = bDown ? FQuat(FVector::UpVector, ResultsHash(Piece, 8.0f) * 6.28f)
				: FQuat(FVector(ResultsHash(Piece, 10.0f) - 0.5f, ResultsHash(Piece, 11.0f) - 0.5f, 0.6f).GetSafeNormal(),
					Flight * (5.0f + 6.0f * ResultsHash(Piece, 12.0f)) + Piece);
			const FVector Across = Turn.RotateVector(FVector(3.2f, 0.0f, 0.0f));
			const FVector Along = Turn.RotateVector(FVector(0.0f, 5.5f, 0.0f));
			AddQuad(Meshes[Piece % Meshes.Num()], At - Across - Along, At + Across - Along, At + Across + Along, At - Across + Along, true);
		}
	}
	for (int32 Index = 0; Index < Confetti.Num(); ++Index)
	{
		ChaosImpactLightning::SetMesh(Confetti[Index], Meshes[Index]);
	}
}

bool AChaosImpactPodiumStage::Project(const FVector& WorldPoint, FVector2D& OutPicture) const
{
	const FVector Eye = GetActorLocation() + CameraLocation;
	const FVector Local = CameraRotation.UnrotateVector(WorldPoint - Eye);
	if (Local.X < 1.0f)
	{
		return false;
	}
	const float Half = FMath::Tan(FMath::DegreesToRadians(CameraFov * 0.5f));
	OutPicture.X = 0.5f + Local.Y / Local.X / (2.0f * Half);
	OutPicture.Y = 0.5f - Local.Z / Local.X / (2.0f * Half) * (static_cast<float>(PictureWidth) / PictureHeight);
	return true;
}

bool AChaosImpactPodiumStage::GetFigureHead(const int32 EntryIndex, FVector& OutWorld, float* OutAppearAt) const
{
	for (int32 Index = 0; Index < Figures.Num() && Index < FigureActors.Num(); ++Index)
	{
		if (Figures[Index].Entry == EntryIndex && Figures[Index].bShown && IsValid(FigureActors[Index]))
		{
			OutWorld = FigureActors[Index]->GetActorLocation() + FVector(0.0f, 0.0f, 250.0f);
			if (OutAppearAt)
			{
				*OutAppearAt = DropAt;
			}
			return true;
		}
	}
	return false;
}

// =====================================================================================================================
// Results view
// =====================================================================================================================

void UChaosImpactResultsView::Start(const AChaosImpactGameState* Match, APlayerController* Owner)
{
	Stop();
	Data = FChaosImpactResultsData::Capture(Match, Owner);
	Data.ChooseAwards();
	ResultsKey = Match->PhaseStartedAt;
	bOnline = Match->bOnlineRoom;
	bLeft = false;
	bRecaptured = false;
	Page = 0;
	bActive = true;
	// The show's clock runs from FINISH's end (1.5 s into the results), on this machine's own time.
	StartedAtReal = FPlatformTime::Seconds() - (Match->GetPhaseElapsedSeconds() - 1.5f);
	PageChangedAt = FPlatformTime::Seconds();
	if (UWorld* World = Owner ? Owner->GetWorld() : nullptr)
	{
		FActorSpawnParameters Parameters;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Parameters.ObjectFlags |= RF_Transient;
		Podium = World->SpawnActor<AChaosImpactPodiumStage>(AChaosImpactPodiumStage::StaticClass(),
			FVector(0.0f, -150000.0f, 44000.0f), FRotator::ZeroRotator, Parameters);
		if (Podium)
		{
			Podium->BeginBuild(Data);
		}
	}
	UE_LOG(LogChaosImpact, Log, TEXT("Results: %d players, %s %s"), Data.Entries.Num(), *Data.Headline, *Data.WinnerName);
	for (const FChaosImpactResultEntry& Entry : Data.Entries)
	{
		UE_LOG(LogChaosImpact, Log, TEXT("Results:  %d. %s %d pt KO %d throws %d hits %d hit %d dodges %d — %s (%s)"),
			Entry.Rank, *Entry.Name, Entry.Points, Entry.Knockouts, Entry.Throws, Entry.Hits, Entry.TimesHit, Entry.Dodges,
			*Entry.AwardTitle, *Entry.AwardNote);
	}
}

void UChaosImpactResultsView::Stop()
{
	if (IsValid(Podium))
	{
		Podium->Destroy();
	}
	Podium = nullptr;
	bActive = false;
}

void UChaosImpactResultsView::Leave()
{
	bLeft = true;
	Stop();
}

float UChaosImpactResultsView::GetAutoReturnRemaining() const
{
	return bActive && bOnline ? FMath::Max(0.f, ChaosImpactMatch::OnlineResultsReturnSeconds - FMath::Max(ShowSeconds, 0.f)) : -1.f;
}

void UChaosImpactResultsView::ShowStats()
{
	if (Page == 0 && CanAdvance())
	{
		Page = 1;
		PageChangedAt = FPlatformTime::Seconds();
		StatsTab = 0;
		GraphFocus = INDEX_NONE;
	}
}

void UChaosImpactResultsView::Tick(APlayerController* Owner, const float DeltaSeconds)
{
	const UWorld* World = Owner ? Owner->GetWorld() : nullptr;
	const AChaosImpactGameState* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
	const bool bResultsNow = Match && Match->bVersusMatch && Match->Phase == EChaosImpactOnlinePhase::Results;
	// Prepared right after FINISH is called (built a little each frame behind it), shown when it ends.
	if (bResultsNow && Match->PhaseStartedAt != ResultsKey && Match->GetPhaseElapsedSeconds() >= 0.15f)
	{
		Start(Match, Owner);
	}
	// The final numbers arrive with FINISH: taken again before the show (the podium is rebuilt only if the order changed).
	if (bActive && !bRecaptured && bResultsNow && Match->GetPhaseElapsedSeconds() >= 1.2f)
	{
		bRecaptured = true;
		FChaosImpactResultsData Fresh = FChaosImpactResultsData::Capture(Match, Owner);
		Fresh.ChooseAwards();
		bool bSameOrder = Fresh.Entries.Num() == Data.Entries.Num();
		for (int32 Index = 0; bSameOrder && Index < Fresh.Entries.Num(); ++Index)
		{
			bSameOrder = Fresh.Entries[Index].Name == Data.Entries[Index].Name && Fresh.Entries[Index].Team == Data.Entries[Index].Team;
		}
		Data = Fresh;
		if (!bSameOrder && Podium)
		{
			// Someone moved places at the whistle: a fresh podium for the right order.
			Podium->Destroy();
			FActorSpawnParameters Parameters;
			Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Parameters.ObjectFlags |= RF_Transient;
			Podium = Owner->GetWorld()->SpawnActor<AChaosImpactPodiumStage>(AChaosImpactPodiumStage::StaticClass(),
				FVector(0.0f, -150000.0f, 44000.0f), FRotator::ZeroRotator, Parameters);
			if (Podium)
			{
				Podium->BeginBuild(Data);
			}
		}
	}
	if (!bActive)
	{
		return;
	}
	if (!Match || (!bOnline && !bResultsNow))
	{
		Stop();
		return;
	}
	if (bOnline && !bResultsNow)
	{
		// Back in the lobby: these results stay until this machine leaves them (or the lobby stops waiting).
		const AChaosImpactPlayerState* Own = Owner->GetPlayerState<AChaosImpactPlayerState>();
		if (!Own || (!Own->bViewingResults && !Own->bSpectating) || Match->Phase != EChaosImpactOnlinePhase::Lobby)
		{
			Stop();
			return;
		}
	}
	ShowSeconds = static_cast<float>(FPlatformTime::Seconds() - StartedAtReal);
	if (bOnline && GetAutoReturnRemaining() <= 0.f)
	{
		if (auto* PC = Cast<AChaosImpactPlayerController>(Owner)) { PC->ReturnToLobbyFromResults(); }
		return;
	}
	if (Podium)
	{
		if (!Podium->IsBuilt())
		{
			// Still building when the show is due: finish it now rather than show it half made.
			if (ShowSeconds >= 0.0f)
			{
				while (!Podium->BuildStep())
				{
				}
			}
			else
			{
				Podium->BuildStep();
				return;
			}
		}
		Podium->UpdateShow(ShowSeconds < 0.0f ? -1.0f : ShowSeconds, DeltaSeconds);
	}
}

int32 UChaosImpactResultsView::Paint(const FGeometry& Allotted, FSlateWindowElementList& Elements, const int32 Layer) const
{
	if (!bActive || ShowSeconds < 0.0f)
	{
		return Layer;
	}
	const FVector2f Size = Allotted.GetLocalSize();
	const float Scale = FMath::Min(Size.X / 1600.0f, Size.Y / 900.0f);
	const FGeometry Design = Allotted.MakeChild(FVector2f(1600.0f, 900.0f),
		FSlateLayoutTransform(Scale, FVector2f((Size.X - 1600.0f * Scale) * 0.5f, (Size.Y - 900.0f * Scale) * 0.5f)));
	const ResultsPaint::FPainter Full{Allotted, Elements, Layer};
	Full.Box(0.0f, 0.0f, Size.X, Size.Y, ResultsPaint::Ink);

	// The podium picture, filling the screen.
	UTextureRenderTarget2D* Picture = Podium ? Podium->GetPicture() : nullptr;
	if (Picture)
	{
		if (PictureBrush.GetResourceObject() != Picture)
		{
			PictureBrush.SetResourceObject(Picture);
			PictureBrush.ImageSize = FVector2D(AChaosImpactPodiumStage::PictureWidth, AChaosImpactPodiumStage::PictureHeight);
			PictureBrush.DrawAs = ESlateBrushDrawType::Image;
		}
		const float Cover = FMath::Max(Size.X / AChaosImpactPodiumStage::PictureWidth, Size.Y / AChaosImpactPodiumStage::PictureHeight);
		const FVector2f Drawn(AChaosImpactPodiumStage::PictureWidth * Cover, AChaosImpactPodiumStage::PictureHeight * Cover);
		FSlateDrawElement::MakeBox(Elements, Layer + 1,
			Allotted.ToPaintGeometry(Drawn, FSlateLayoutTransform((Size - Drawn) * 0.5f)), &PictureBrush);
	}
	int32 Top = PaintPodiumPage(Design, Allotted, Elements, Layer + 2);
	if (Page == 1)
	{
		Top = PaintStatsPage(Design, Elements, Top + 1);
	}
	if (const float Left = GetAutoReturnRemaining(); Left >= 0.f)
	{
		const ResultsPaint::FPainter Timer{Allotted, Elements, Top + 1};
		const float X = Size.X - 258.f * Scale, Y = 18.f * Scale;
		Timer.Box(X, Y, 238.f * Scale, 36.f * Scale, ResultsPaint::WithAlpha(ResultsPaint::Ink, .88f));
		Timer.Box(X, Y, 3.f * Scale, 36.f * Scale, ResultsPaint::Ice);
		Timer.Text(FString::Printf(TEXT("あと%d秒で戻ります"), FMath::CeilToInt(Left)), X + 119.f * Scale, Y + 6.f * Scale,
			18.f * Scale, ResultsPaint::Paper, ResultsPaint::ETextAlign::Center, TEXT("Bold"), 1.f, ResultsPaint::Ink);
	}
	// Coming in: from black.
	const float In = FMath::Clamp(ShowSeconds / 0.25f, 0.0f, 1.0f);
	if (In < 1.0f)
	{
		const ResultsPaint::FPainter Fade{Allotted, Elements, Top + 1};
		Fade.Box(0.0f, 0.0f, Size.X, Size.Y, ResultsPaint::WithAlpha(FLinearColor::Black, 1.0f - In));
	}
	return Top + 2;
}

int32 UChaosImpactResultsView::PaintPodiumPage(const FGeometry& Design, const FGeometry& Allotted, FSlateWindowElementList& Elements,
	const int32 Layer) const
{
	const float S = ShowSeconds;
	const FVector2f Size = Allotted.GetLocalSize();
	const ResultsPaint::FPainter Full{Allotted, Elements, Layer};
	using Stage = AChaosImpactPodiumStage;

	// The blackout before the winner drops, and the flash as they land.
	if (S >= Stage::BlackoutAt && S < Stage::LandAt)
	{
		const float Dark = FMath::Clamp((S - Stage::BlackoutAt) / 0.12f, 0.0f, 1.0f) * 0.72f;
		Full.Box(0.0f, 0.0f, Size.X, Size.Y, ResultsPaint::WithAlpha(FLinearColor::Black, Dark));
	}
	const float SinceLand = S - Stage::LandAt;
	if (SinceLand > 0.0f && SinceLand < 0.35f)
	{
		Full.Box(0.0f, 0.0f, Size.X, Size.Y, ResultsPaint::WithAlpha(ResultsPaint::Paper, 0.85f * (1.0f - SinceLand / 0.35f)));
	}
	// Cinema bars during the show, drawn back once it has settled.
	{
		const float Bars = 1.0f - ResultsPaint::EaseOut((S - Stage::SettledAt) / 0.5f);
		const float BarHeight = 64.0f * Bars * (Size.Y / 900.0f);
		Full.Box(0.0f, 0.0f, Size.X, BarHeight, FLinearColor::Black);
		Full.Box(0.0f, Size.Y - BarHeight, Size.X, BarHeight, FLinearColor::Black);
	}

	// Only the winner is in the picture (on the right); the left is theirs too: a shade so the words read, the headline,
	// their name on a band, and the score. Everyone else's numbers are on the next page.
	if (SinceLand > 0.0f)
	{
		const float Dim = Page == 1 ? 0.0f : 1.0f;
		const float ShadeIn = ResultsPaint::EaseOut(SinceLand / 0.6f) * Dim;
		const ResultsPaint::FPainter Shade{Allotted, Elements, Layer + 1, ShadeIn};
		const float Edge = Size.X * 0.6f;
		const FLinearColor Deep = ResultsPaint::WithAlpha(FLinearColor(0.004f, 0.006f, 0.012f), 0.8f);
		const FLinearColor Clear = ResultsPaint::WithAlpha(Deep, 0.0f);
		ResultsPaint::HudTriangles(Shade, {FVector2D(0.0f, 0.0f), FVector2D(Edge, 0.0f), FVector2D(Edge, Size.Y), FVector2D(0.0f, Size.Y)},
			{Deep, Clear, Clear, Deep}, {0, 1, 2, 0, 2, 3});
		// A slash of the winner's colour from the top left corner, behind the words.
		const FGeometry Slash = ResultsPaint::MakeSkewed(Design, -120.0f, 0.0f, 1000.0f, 250.0f, -0.42f);
		const ResultsPaint::FPainter Sl{Slash, Elements, Layer + 1, ShadeIn};
		const float SlashIn = ResultsPaint::EaseOut(SinceLand / 0.4f);
		Sl.Box(0.0f, 0.0f, 860.0f * SlashIn, 250.0f, ResultsPaint::WithAlpha(Data.WinnerColor, 0.16f));
		Sl.Box(0.0f, 244.0f, 940.0f * SlashIn, 6.0f, ResultsPaint::WithAlpha(Data.WinnerColor, 0.9f));
	}

	// WINNER: slammed in over the left, the name on a band below it, the score under that.
	if (SinceLand > 0.12f)
	{
		const float T = SinceLand - 0.12f;
		const float BandIn = ResultsPaint::EaseOut(T / 0.28f);
		const float Dim = Page == 1 ? 0.0f : 1.0f;
		// (Nothing goes right of here: the winner stands there.)
		constexpr float TextRight = 700.0f;
		const FGeometry Band = ResultsPaint::MakeSkewed(Design, -60.0f, 470.0f, 1700.0f, 120.0f, -0.32f);
		const ResultsPaint::FPainter B{Band, Elements, Layer + 3, Dim};
		B.Box(-200.0f, 8.0f, 980.0f * BandIn, 92.0f, ResultsPaint::WithAlpha(FLinearColor(0.012f, 0.014f, 0.022f), 0.86f));
		B.Box(-200.0f, 8.0f, 980.0f * BandIn, 4.0f, ResultsPaint::WithAlpha(Data.WinnerColor, 0.95f));
		B.Box(-200.0f, 102.0f, 860.0f * BandIn, 3.0f, ResultsPaint::WithAlpha(ResultsPaint::Gold, 0.85f));
		const float NameIn = ResultsPaint::EaseOut((T - 0.15f) / 0.3f);
		const ResultsPaint::FPainter N{Band, Elements, Layer + 4, NameIn * Dim};
		FString Names = Data.WinnerName;
		if (Data.bTie)
		{
			TArray<FString> Tied;
			if (Data.bTeams)
			{
				for (const auto& Team : Data.TeamTotals)
				{
					if (Team.Value == Data.TeamTotals[0].Value) { Tied.Add(FString::Printf(TEXT("%sチーム"), ChaosImpactMatch::GetTeamName(Team.Key))); }
				}
			}
			else
			{
				for (const FChaosImpactResultEntry& Item : Data.Entries) { if (Item.Rank == 1) { Tied.Add(Item.Name); } }
			}
			Names = FString::Join(Tied, TEXT(" ・ "));
			if (Tied.Num() > 3) { Names = FString::Printf(TEXT("%d%sが同率1位"), Tied.Num(), Data.bTeams ? TEXT("チーム") : TEXT("人")); }
		}
		const float NameX = 120.0f + (1.0f - NameIn) * 120.0f;
		N.Text(Names, NameX, 16.0f, ResultsPaint::HudFit(Names, 62.0f, TextRight - 120.0f, TEXT("BlackItalic")), ResultsPaint::Paper,
			ResultsPaint::ETextAlign::Left, TEXT("BlackItalic"), 4.0f, ResultsPaint::Ink);
		if (!Data.Entries.IsEmpty())
		{
			const int32 Points = Data.bTeams && !Data.TeamTotals.IsEmpty() ? Data.TeamTotals[0].Value : Data.Entries[0].Points;
			const FString Line = Data.bBattleRoyale ? FString(TEXT("BATTLE ROYALE  ・  1位"))
				: Data.bDuel ? FString::Printf(TEXT("ROUND  %d - %d"), Data.Entries[0].RoundsWon, Data.Entries.Num() > 1 ? Data.Entries[1].RoundsWon : 0)
				: Data.bCrashBall && Data.TeamTotals.Num() > 1 ? FString::Printf(TEXT("%s  %d : %d"), Data.bCrashKnockout ? TEXT("KNOCKOUT") : TEXT("カウント"),
				100 - Data.TeamTotals[0].Value, 100 - Data.TeamTotals[1].Value) : Data.bTeams ? FString::Printf(TEXT("TEAM %d pt"), Points)
				: FString::Printf(TEXT("%d pt   KO %d"), Points, Data.Entries[0].Knockouts);
			N.Text(Line, 140.0f, 124.0f, ResultsPaint::HudFit(Line, 26.0f, TextRight - 180.0f, TEXT("BlackItalic")), ResultsPaint::Paper,
				ResultsPaint::ETextAlign::Left, TEXT("BlackItalic"), 2.0f, ResultsPaint::Ink);
		}
		// The headline lands big and settles, shaking a little.
		const float Slam = 1.0f + 0.7f * FMath::Exp(-11.0f * T);
		const float Shake = T < 0.3f ? FMath::Sin(T * 95.0f) * (0.3f - T) * 30.0f : 0.0f;
		const FGeometry Head = ResultsPaint::MakeSkewed(Design, 40.0f + Shake, 300.0f, 1000.0f, 170.0f, -0.18f, Slam);
		const ResultsPaint::FPainter H{Head, Elements, Layer + 5, ResultsPaint::EaseOut(T / 0.12f) * Dim};
		H.Text(Data.Headline, 30.0f, 0.0f, ResultsPaint::HudFit(Data.Headline, 132.0f, TextRight - 90.0f, TEXT("BlackItalic")), ResultsPaint::Paper,
			ResultsPaint::ETextAlign::Left, TEXT("BlackItalic"), 7.0f, Data.WinnerColor);
		if (Data.bTeams && !Data.bTie && !Data.bBattleRoyale)
		{
			// Above the headline, small.
			const FString WinLine = FString::Printf(TEXT("%sの勝ち！"), *Data.WinnerName);
			H.Text(WinLine, 44.0f, -42.0f, ResultsPaint::HudFit(WinLine, 30.0f, TextRight - 100.0f), ResultsPaint::Paper,
				ResultsPaint::ETextAlign::Left, TEXT("Black"), 3.0f, ResultsPaint::Ink);
		}

		// Settled: which button goes on (under the words, on the left).
		if (Page == 0 && S >= RevealSeconds)
		{
			const float Blink = 0.55f + 0.45f * FMath::Cos((S - RevealSeconds) * 4.0f);
			const ResultsPaint::FPainter Prompt{Design, Elements, Layer + 6, EaseOutPrompt(S - RevealSeconds) * Blink};
			const FString Go = TEXT("ボタンを押して 成績へ ▶");
			Prompt.Box(96.0f, 700.0f, 440.0f, 50.0f, ResultsPaint::WithAlpha(ResultsPaint::Ink, 0.75f));
			Prompt.Box(96.0f, 700.0f, 440.0f, 3.0f, Data.WinnerColor);
			Prompt.Text(Go, 316.0f, 709.0f, ResultsPaint::HudFit(Go, 24.0f, 410.0f, TEXT("BlackItalic")), ResultsPaint::Paper,
				ResultsPaint::ETextAlign::Center, TEXT("BlackItalic"));
		}
	}
	return Layer + 7;
}

FSlateRect UChaosImpactResultsView::GetStatsRowRect(const int32 Index, const int32 Count)
{
	const float RowHeight = FMath::Min(62.0f, 540.0f / FMath::Max(Count, 1));
	const float Top = 186.0f + Index * RowHeight;
	return FSlateRect(50.0f, Top, 790.0f, Top + RowHeight - 6.0f);
}

FSlateRect UChaosImpactResultsView::GetTabArrowRect(const int32 Tab)
{
	// The numbers' arrow at the right edge (on to the awards), the awards' at the left (back).
	return Tab == 0 ? FSlateRect(1526.0f, 330.0f, 1594.0f, 570.0f) : FSlateRect(6.0f, 330.0f, 74.0f, 570.0f);
}

void UChaosImpactResultsView::SetStatsTab(const int32 Tab)
{
	const int32 Clamped = bAwardsEnabled ? FMath::Clamp(Tab, 0, 1) : 0;
	if (Page == 1 && Clamped != StatsTab)
	{
		StatsTab = Clamped;
		TabChangedAt = FPlatformTime::Seconds();
	}
}

void UChaosImpactResultsView::ToggleGraphFocus(const int32 Entry)
{
	GraphFocus = GraphFocus == Entry || !Data.Entries.IsValidIndex(Entry) ? INDEX_NONE : Entry;
	FocusChangedAt = FPlatformTime::Seconds();
}

int32 UChaosImpactResultsView::PaintStatsPage(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer) const
{
	const double Now = FPlatformTime::Seconds();
	const float In = ResultsPaint::EaseOut(static_cast<float>(Now - PageChangedAt) / 0.3f);
	const ResultsPaint::FPainter Back{Design, Elements, Layer, In};
	Back.Box(-800.0f, -400.0f, 3200.0f, 1700.0f, ResultsPaint::WithAlpha(ResultsPaint::Ink, 0.9f));
	const FGeometry Slant = ResultsPaint::MakeSkewed(Design, 0.0f, 0.0f, 1600.0f, 900.0f, -0.3f);
	const ResultsPaint::FPainter Lines{Slant, Elements, Layer, In};
	Lines.Box(StatsTab == 0 ? 820.0f : 1240.0f, -50.0f, 6.0f, 1000.0f, ResultsPaint::WithAlpha(Data.WinnerColor, 0.3f));
	Lines.Box(StatsTab == 0 ? 840.0f : 1260.0f, -50.0f, 2.0f, 1000.0f, ResultsPaint::WithAlpha(ResultsPaint::Ice, 0.25f));

	// The tab coming in slides from the side it was reached from.
	const float TabIn = ResultsPaint::EaseOut(static_cast<float>(Now - FMath::Max(TabChangedAt, PageChangedAt)) / 0.3f);
	const float Slide = (1.0f - TabIn) * (StatsTab == 0 ? -60.0f : 60.0f);
	if (StatsTab == 0)
	{
		PaintNumbersTab(Design, Elements, Layer + 1, In * TabIn, Slide);
	}
	else
	{
		PaintAwardsTab(Design, Elements, Layer + 1, In * TabIn, Slide);
	}
	return Layer + 6;
}

int32 UChaosImpactResultsView::PaintNumbersTab(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer,
	const float Alpha, const float Slide) const
{
	const double Now = FPlatformTime::Seconds();
	const ResultsPaint::FPainter P{Design, Elements, Layer, Alpha};
	P.Text(TEXT("MATCH STATS"), 60.0f + Slide, 34.0f, 46.0f, ResultsPaint::Paper, ResultsPaint::ETextAlign::Left, TEXT("BlackItalic"), 3.0f, Data.WinnerColor);
	P.Text(Data.bBattleRoyale ? TEXT("生存順位と体力の流れ") : Data.bDuel ? TEXT("獲得ラウンドと対戦成績") : TEXT("成績とポイントの流れ"), 64.0f + Slide, 92.0f, 18.0f, ResultsPaint::Muted, ResultsPaint::ETextAlign::Left, TEXT("Bold"));

	// ---- Table (left) ----
	struct FColumn { const TCHAR* Title; float X; };
	static const FColumn Columns[] = {{TEXT("pt"), 430.0f}, {TEXT("KO"), 482.0f}, {TEXT("投げ"), 538.0f}, {TEXT("当て"), 590.0f},
		{TEXT("命中"), 670.0f}, {TEXT("被弾"), 728.0f}, {TEXT("よけ"), 782.0f}};
	const float TableTop = 150.0f;
	P.Box(50.0f + Slide, TableTop, 740.0f, 30.0f, ResultsPaint::WithAlpha(ResultsPaint::Paper, 0.07f));
	P.Text(TEXT("順位"), 82.0f + Slide, TableTop + 5.0f, 15.0f, ResultsPaint::Muted, ResultsPaint::ETextAlign::Center, TEXT("Bold"));
	P.Text(TEXT("なまえ"), 120.0f + Slide, TableTop + 5.0f, 15.0f, ResultsPaint::Muted, ResultsPaint::ETextAlign::Left, TEXT("Bold"));
	for (const FColumn& Column : Columns)
	{
		const TCHAR* Title = Column.X == 430.f ? (Data.bBattleRoyale ? TEXT("") : Data.bDuel ? TEXT("勝数") : TEXT("pt")) : Column.Title;
		P.Text(Title, Column.X + Slide, TableTop + 5.0f, 15.0f, ResultsPaint::Muted, ResultsPaint::ETextAlign::Right, TEXT("Bold"));
	}
	const int32 Count = Data.Entries.Num();
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FChaosImpactResultEntry& Item = Data.Entries[Index];
		const float RowIn = ResultsPaint::EaseOut(static_cast<float>(Now - PageChangedAt - 0.05 * Index) / 0.3f);
		// While one person's graph is shown, their row is lit and the others step back.
		const bool bFocused = GraphFocus == Index;
		const float Dim = GraphFocus == INDEX_NONE || bFocused ? 1.0f : 0.45f;
		const ResultsPaint::FPainter R{Design, Elements, Layer + 1, Alpha * RowIn};
		const ResultsPaint::FPainter RT{Design, Elements, Layer + 2, Alpha * RowIn * Dim};
		const FSlateRect Rect = GetStatsRowRect(Index, Count);
		const float X = static_cast<float>(Rect.Left) + Slide;
		const float Y = static_cast<float>(Rect.Top);
		const float H = static_cast<float>(Rect.GetSize().Y);
		R.Box(X, Y, static_cast<float>(Rect.GetSize().X), H, bFocused ? ResultsPaint::WithAlpha(Item.Color, 0.28f)
			: ResultsPaint::WithAlpha(ResultsPaint::Paper, Item.LocalIndex >= 0 ? 0.1f : 0.04f));
		R.Box(X, Y, bFocused ? 10.0f : 5.0f, H, Item.Color);
		const float TextY = Y + H * 0.5f - 13.0f;
		RT.Text(FString::FromInt(Item.Rank), 82.0f + Slide, TextY - 2.0f, 24.0f, Item.Rank == 1 ? ResultsPaint::Gold : ResultsPaint::Paper,
			ResultsPaint::ETextAlign::Center, TEXT("BlackItalic"));
		float NameX = 120.0f;
		if (Item.LocalIndex >= 0)
		{
			RT.Text(FString::Printf(TEXT("P%d"), Item.LocalIndex + 1), NameX + Slide, TextY + 3.0f, 15.0f,
				ResultsPaint::PlayerAccents[Item.LocalIndex % 4], ResultsPaint::ETextAlign::Left, TEXT("BlackItalic"));
			NameX += 34.0f;
		}
		// (Up to the first number column.)
		RT.Text(Item.Name, NameX + Slide, TextY, ResultsPaint::HudFit(Item.Name, 20.0f, Columns[0].X - 44.0f - NameX), ResultsPaint::Paper,
			ResultsPaint::ETextAlign::Left, TEXT("Black"));
		// Burns of a fire zone count as hits too, so a few throws can make many: shown up to 100%.
		const int32 Accuracy = Item.Throws > 0 ? FMath::Min(100, FMath::RoundToInt(100.0f * Item.Hits / Item.Throws)) : 0;
		const FString Values[] = {Data.bBattleRoyale ? FString() : FString::FromInt(Data.bDuel ? Item.RoundsWon : Item.Points), FString::FromInt(Item.Knockouts), FString::FromInt(Item.Throws),
			FString::FromInt(Item.Hits), Item.Throws > 0 ? FString::Printf(TEXT("%d%%"), Accuracy) : FString(TEXT("-")),
			FString::FromInt(Item.TimesHit), FString::FromInt(Item.Dodges)};
		for (int32 Column = 0; Column < UE_ARRAY_COUNT(Columns); ++Column)
		{
			RT.Text(Values[Column], Columns[Column].X + Slide, TextY, Column == 0 ? 22.0f : 19.0f, Column == 0 ? ResultsPaint::Gold : ResultsPaint::Paper,
				ResultsPaint::ETextAlign::Right, Column == 0 ? TEXT("Black") : TEXT("Bold"));
		}
	}

	// ---- Graph (right): everyone's points over the match, or one person's ----
	const float GraphLeft = 910.0f + Slide;
	const float GraphRight = 1490.0f + Slide;
	const float GraphTop = 200.0f;
	const float GraphBottom = 700.0f;
	const bool bOne = Data.Entries.IsValidIndex(GraphFocus);
	int32 Samples = 0;
	int32 Highest = 1;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FChaosImpactResultEntry& Item = Data.Entries[Index];
		Samples = FMath::Max(Samples, Item.History.Num());
		if (bOne && Index != GraphFocus)
		{
			continue;
		}
		// One person's line is drawn to their own scale.
		for (const int16 Value : Item.History)
		{
			Highest = FMath::Max(Highest, static_cast<int32>(Value));
		}
	}
	// A scale of whole numbers on each of the four lines.
	Highest = FMath::Max(4, FMath::DivideAndRoundUp(Highest, 4) * 4);
	const ResultsPaint::FPainter G{Design, Elements, Layer + 1, Alpha};
	G.Text(Data.bBattleRoyale ? TEXT("残りHPの流れ") : Data.bDuel ? TEXT("獲得ラウンドの流れ") : TEXT("ポイントの流れ"), 850.0f + Slide, 140.0f, 22.0f, ResultsPaint::Paper, ResultsPaint::ETextAlign::Left, TEXT("BlackItalic"));
	if (bOne)
	{
		const FChaosImpactResultEntry& Item = Data.Entries[GraphFocus];
		G.Box(1056.0f + Slide, 140.0f, 6.0f, 30.0f, Item.Color);
		const FString GraphName = FString::Printf(TEXT("%s  %s"), *Item.Name, *Data.ScoreText(Item));
		const float GraphRoom = GraphRight - ResultsPaint::HudMeasure(TEXT("もう一度えらぶと全員"), 15.0f, TEXT("Bold")) - 20.0f - (1072.0f + Slide);
		G.Text(GraphName, 1072.0f + Slide, 142.0f, ResultsPaint::HudFit(GraphName, 20.0f, FMath::Max(80.0f, GraphRoom)), Item.Color,
			ResultsPaint::ETextAlign::Left, TEXT("Black"));
		G.Text(TEXT("もう一度えらぶと全員"), GraphRight, 146.0f, 15.0f, ResultsPaint::Muted, ResultsPaint::ETextAlign::Right, TEXT("Bold"));
	}
	else
	{
		G.Text(TEXT("順位をえらぶと その人だけ"), GraphRight, 146.0f, 15.0f, ResultsPaint::Muted, ResultsPaint::ETextAlign::Right, TEXT("Bold"));
	}
	for (int32 Grid = 0; Grid <= 4; ++Grid)
	{
		const float Y = GraphBottom - (GraphBottom - GraphTop) * Grid / 4.0f;
		G.Box(GraphLeft, Y, GraphRight - GraphLeft, 1.0f, ResultsPaint::WithAlpha(ResultsPaint::Paper, Grid == 0 ? 0.35f : 0.08f));
		G.Text(FString::FromInt(Highest * Grid / 4), GraphLeft - 12.0f, Y - 10.0f, 14.0f, ResultsPaint::Muted,
			ResultsPaint::ETextAlign::Right, TEXT("Bold"));
	}
	const float TotalSeconds = FMath::Max(Samples - 1, 1) * ChaosImpactMatch::HistoryStepSeconds;
	for (int32 Mark = 0; Mark <= 4; ++Mark)
	{
		const float Seconds = TotalSeconds * Mark / 4.0f;
		G.Text(FString::Printf(TEXT("%d:%02d"), FMath::FloorToInt(Seconds / 60.0f), FMath::FloorToInt(Seconds) % 60),
			GraphLeft + (GraphRight - GraphLeft) * Mark / 4.0f, GraphBottom + 6.0f, 13.0f, ResultsPaint::Muted, ResultsPaint::ETextAlign::Center, TEXT("Bold"));
	}
	// Lines draw themselves from left to right as the page opens (and again when who is shown changes); the winner's on top.
	const double DrawFrom = FMath::Max(FMath::Max(PageChangedAt + 0.2, FocusChangedAt), TabChangedAt);
	const float Reveal = ResultsPaint::EaseOut(static_cast<float>(Now - DrawFrom) / (bOne ? 0.6f : 1.2f));
	for (int32 Index = Count - 1; Index >= 0; --Index)
	{
		const FChaosImpactResultEntry& Item = Data.Entries[Index];
		if (Item.History.Num() < 2 || (bOne && Index != GraphFocus))
		{
			continue;
		}
		TArray<FVector2D> Points;
		const int32 Shown = FMath::Max(2, FMath::CeilToInt(Item.History.Num() * Reveal));
		for (int32 Sample = 0; Sample < Shown; ++Sample)
		{
			const float X = GraphLeft + (GraphRight - GraphLeft) * Sample / static_cast<float>(FMath::Max(Samples - 1, 1));
			const float Y = GraphBottom - (GraphBottom - GraphTop) * FMath::Max<int32>(Item.History[Sample], 0) / static_cast<float>(Highest);
			Points.Add(FVector2D(X, Y));
		}
		const ResultsPaint::FPainter Dot{Design, Elements, Layer + 3, Alpha};
		if (bOne)
		{
			// Filled in under the line, with a point at each sample.
			for (int32 Sample = 1; Sample < Points.Num(); ++Sample)
			{
				const float Width = static_cast<float>(Points[Sample].X - Points[Sample - 1].X);
				const float Height = static_cast<float>(GraphBottom - FMath::Min(Points[Sample].Y, Points[Sample - 1].Y));
				G.Box(static_cast<float>(Points[Sample - 1].X), GraphBottom - Height, Width, Height, ResultsPaint::WithAlpha(Item.Color, 0.1f));
			}
			for (const FVector2D& Point : Points)
			{
				Dot.Disc(Point, 3.5f, Item.Color);
			}
		}
		FSlateDrawElement::MakeLines(Elements, Layer + 2, Design.ToPaintGeometry(), Points, ESlateDrawEffect::None,
			ResultsPaint::WithAlpha(Item.Color, Alpha), true, bOne ? 5.0f : Index == 0 ? 4.0f : 2.5f);
		Dot.Disc(Points.Last(), bOne ? 8.0f : Index == 0 ? 6.0f : 4.5f, Item.Color);
		if (bOne && Reveal >= 1.0f)
		{
			Dot.Text(FString::Printf(TEXT("%d"), Item.History.Last()), static_cast<float>(Points.Last().X) - 12.0f,
				static_cast<float>(Points.Last().Y) - 36.0f, 20.0f, ResultsPaint::Paper, ResultsPaint::ETextAlign::Right, TEXT("Black"), 2.0f, ResultsPaint::Ink);
		}
	}
	return Layer + 4;
}

int32 UChaosImpactResultsView::PaintAwardsTab(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer,
	const float Alpha, const float Slide) const
{
	const double Now = FPlatformTime::Seconds();
	const ResultsPaint::FPainter P{Design, Elements, Layer, Alpha};
	P.Text(TEXT("AWARDS"), 110.0f + Slide, 34.0f, 46.0f, ResultsPaint::Paper, ResultsPaint::ETextAlign::Left, TEXT("BlackItalic"), 3.0f, Data.WinnerColor);
	P.Text(TEXT("この試合のアワード"), 114.0f + Slide, 92.0f, 18.0f, ResultsPaint::Muted, ResultsPaint::ETextAlign::Left, TEXT("Bold"));

	static const TCHAR* const Icons[] = {TEXT("KO"), TEXT("◎"), TEXT("≫"), TEXT("%"), TEXT("D"), TEXT("★"), TEXT("↑"), TEXT("盾"),
		TEXT("投"), TEXT("♥")};
	static const FLinearColor IconColors[] = {ResultsPaint::Gold, ResultsPaint::Fire, ResultsPaint::Ice, ResultsPaint::Paper, FLinearColor(1.0f, 0.62f, 0.12f), ResultsPaint::Violet,
		FLinearColor(0.25f, 0.92f, 0.42f), FLinearColor(0.6f, 0.75f, 0.9f), FLinearColor(1.0f, 0.5f, 0.2f), FLinearColor(1.0f, 0.45f, 0.7f)};
	// One card each: one column for up to four, two beside each other for more.
	const int32 Count = Data.Entries.Num();
	const int32 Columns = Count > 4 ? 2 : 1;
	const int32 PerColumn = FMath::DivideAndRoundUp(FMath::Max(Count, 1), Columns);
	const float CardWidth = Columns == 1 ? 1000.0f : 660.0f;
	const float CardHeight = FMath::Min(140.0f, 580.0f / PerColumn);
	const float Height = CardHeight - 14.0f;
	const double CardsFrom = FMath::Max(TabChangedAt, PageChangedAt);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FChaosImpactResultEntry& Item = Data.Entries[Index];
		const float CardIn = ResultsPaint::EaseOut(static_cast<float>(Now - CardsFrom - 0.08 * Index) / 0.35f);
		if (CardIn <= 0.0f)
		{
			continue;
		}
		const int32 Column = Index / PerColumn;
		const float Left = (Columns == 1 ? 300.0f : 120.0f + Column * 720.0f) + Slide + (1.0f - CardIn) * 80.0f;
		const float Y = 140.0f + (Index % PerColumn) * CardHeight;
		const FGeometry Card = ResultsPaint::MakeSkewed(Design, Left, Y, CardWidth, Height, -0.12f);
		const ResultsPaint::FPainter C{Card, Elements, Layer + 1, Alpha * CardIn};
		const ResultsPaint::FPainter CT{Card, Elements, Layer + 2, Alpha * CardIn};
		const FLinearColor IconColor = IconColors[FMath::Clamp(Item.AwardIcon, 0, 9)];
		C.Box(10.0f, 10.0f, CardWidth, Height, FLinearColor(0.0f, 0.0f, 0.0f, 0.4f));
		C.Box(0.0f, 0.0f, CardWidth, Height, FLinearColor(0.03f, 0.04f, 0.07f, 0.97f));
		C.Box(0.0f, 0.0f, 8.0f, Height, Item.Color);
		C.Box(0.0f, Height - 3.0f, CardWidth, 3.0f, ResultsPaint::WithAlpha(IconColor, 0.6f));
		const float Mid = Height * 0.5f;
		const float Icon = FMath::Min(Mid - 10.0f, 46.0f);
		C.Disc(FVector2D(24.0f + Icon, Mid), Icon, ResultsPaint::WithAlpha(IconColor, 0.92f));
		const float TextLeft = 48.0f + Icon * 2.0f;
		const float Big = FMath::Clamp(Height * 0.27f, 18.0f, 30.0f);
		CT.Text(Icons[FMath::Clamp(Item.AwardIcon, 0, 9)], 24.0f + Icon, Mid - Big * 0.75f, Big, ResultsPaint::Ink, ResultsPaint::ETextAlign::Center, TEXT("Black"));
		CT.Text(Item.AwardTitle, TextLeft, Mid - Big * 1.3f, Big, IconColor, ResultsPaint::ETextAlign::Left, TEXT("BlackItalic"));
		CT.Text(Item.AwardNote, TextLeft + 2.0f, Mid + 4.0f, Big * 0.68f, ResultsPaint::Paper, ResultsPaint::ETextAlign::Left, TEXT("Bold"));
		CT.Text(FString::Printf(TEXT("%d位"), Item.Rank), CardWidth - 20.0f, Mid - Big * 1.25f, Big * 0.62f, Item.Rank == 1 ? ResultsPaint::Gold : ResultsPaint::Muted,
			ResultsPaint::ETextAlign::Right, TEXT("BlackItalic"));
		CT.Text(Item.Name, CardWidth - 20.0f, Mid - Big * 0.4f, ResultsPaint::HudFit(Item.Name, Big * 0.75f, CardWidth * 0.34f), ResultsPaint::Paper,
			ResultsPaint::ETextAlign::Right, TEXT("Black"));
	}
	return Layer + 3;
}
