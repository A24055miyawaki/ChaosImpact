#include "ChaosImpactResults.h"

#include "ChaosImpact.h"
#include "ChaosImpactBallTypes.h"
#include "ChaosImpactCharacter.h"
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

	FVector HexCorner(const FVector& Base, const float Radius, const int32 Corner, const float Z)
	{
		// A flat side toward the camera (+Y).
		const float Angle = FMath::DegreesToRadians(60.0f * Corner);
		return Base + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, Z);
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

	/** A six-sided block standing on Base. */
	void AppendHexPrism(ChaosImpactIceMeshes::FMeshBuffers& Mesh, const FVector& Base, const float Radius, const float Height)
	{
		for (int32 Corner = 0; Corner < 6; ++Corner)
		{
			AddQuad(Mesh, HexCorner(Base, Radius, Corner, 0.0f), HexCorner(Base, Radius, Corner + 1, 0.0f),
				HexCorner(Base, Radius, Corner + 1, Height), HexCorner(Base, Radius, Corner, Height), true);
		}
		const FVector Top = Base + FVector(0.0f, 0.0f, Height);
		for (int32 Corner = 0; Corner < 6; ++Corner)
		{
			AddQuad(Mesh, Top, Top, HexCorner(Base, Radius, Corner + 1, Height), HexCorner(Base, Radius, Corner, Height), true);
		}
	}

	/** Glowing edges round a six-sided block: its top and bottom rims and its corners. */
	void AppendHexNeon(ChaosImpactIceMeshes::FMeshBuffers& Mesh, const FVector& Base, const float Radius, const float Height,
		const float Width)
	{
		const float Out = Radius + 1.5f;
		for (const float Z : {Height + 0.5f, 2.0f})
		{
			for (int32 Corner = 0; Corner < 6; ++Corner)
			{
				// A band on the side, just below (or above) the rim.
				const float Low = Z > 3.0f ? Z - Width : Z;
				AddQuad(Mesh, HexCorner(Base, Out, Corner, Low), HexCorner(Base, Out, Corner + 1, Low),
					HexCorner(Base, Out, Corner + 1, Low + Width), HexCorner(Base, Out, Corner, Low + Width), true);
			}
		}
		for (int32 Corner = 0; Corner < 6; ++Corner)
		{
			const FVector Edge = HexCorner(Base, Out, Corner, 0.0f);
			const FVector Across = FVector::CrossProduct(FVector::UpVector, (Edge - Base).GetSafeNormal2D()) * Width * 0.35f;
			AddQuad(Mesh, Edge - Across, Edge + Across, Edge + Across + FVector(0.0f, 0.0f, Height),
				Edge - Across + FVector(0.0f, 0.0f, Height), true);
		}
	}

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
		Entry.History = Member->PointsHistory;
		Entry.Rank = 1;
		for (int32 Other = 0; Other < Index; ++Other)
		{
			Entry.Rank += Ranking[Other]->Points > Member->Points ? 1 : 0;
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
	if (Data.bTeams)
	{
		for (int32 Team = 0; Team < Match->Rules.TeamCount; ++Team)
		{
			Data.TeamTotals.Add({Team, Match->GetTeamPoints(Team)});
		}
		Data.TeamTotals.StableSort([](const TPair<int32, int32>& A, const TPair<int32, int32>& B) { return A.Value > B.Value; });
		Data.bTie = Data.TeamTotals.Num() > 1 && Data.TeamTotals[0].Value == Data.TeamTotals[1].Value;
		const int32 Winner = Data.TeamTotals.IsEmpty() ? 0 : Data.TeamTotals[0].Key;
		Data.WinnerColor = ChaosImpactMatch::GetTeamColor(Winner);
		Data.WinnerName = FString::Printf(TEXT("%sチーム"), ChaosImpactMatch::GetTeamName(Winner));
		Data.Headline = Data.bTie ? FString(TEXT("DRAW")) : FString(TEXT("WINNER"));
	}
	else
	{
		Data.bTie = Data.Entries.Num() > 1 && Data.Entries[0].Points == Data.Entries[1].Points;
		Data.WinnerColor = Data.Entries.IsEmpty() ? FLinearColor::White : ChaosImpactRoster::GetColourSwatch(Data.Entries[0].Colour);
		Data.WinnerName = Data.Entries.IsEmpty() ? FString() : Data.Entries[0].Name;
		Data.Headline = Data.bTie ? FString(TEXT("DRAW")) : FString(TEXT("WINNER"));
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

void AChaosImpactPodiumStage::AddPedestal(const FVector& Base, const float Radius, const float Height, const FLinearColor& Neon,
	const int32 Place)
{
	ChaosImpactIceMeshes::FMeshBuffers Body;
	AppendHexPrism(Body, Base, Radius, Height);
	ChaosImpactIceMeshes::CreateComponent(this, Root, Body, ResultsLitMaterial(this, FLinearColor(0.02f, 0.022f, 0.03f), 0.25f));
	ChaosImpactIceMeshes::FMeshBuffers Edges;
	AppendHexNeon(Edges, Base, Radius, Height, Place == 0 ? 7.0f : 5.0f);
	UMaterialInstanceDynamic* NeonMaterial = ChaosImpactBallTypes::MakeEmissive(this, Neon, 0.0f);
	ChaosImpactIceMeshes::CreateComponent(this, Root, Edges, NeonMaterial);
	NeonMaterials.SetNum(FMath::Max(NeonMaterials.Num(), Place + 1));
	NeonMaterials[Place] = NeonMaterial;

	// The floor ring round its foot, and the pillar of light it appears in.
	ChaosImpactIceMeshes::FMeshBuffers Ring;
	ChaosImpactLightning::AppendRing(Ring, Base + FVector(0.0f, 0.0f, 1.0f), Radius + 34.0f, 4.0f, 6);
	ChaosImpactLightning::AppendRing(Ring, Base + FVector(0.0f, 0.0f, 1.0f), Radius + 70.0f, 2.0f, 48);
	UProceduralMeshComponent* RingMesh = ChaosImpactLightning::CreateComponent(this, Root, ChaosImpactBallTypes::MakeAdditive(this, Neon, 1.2f));
	ChaosImpactLightning::SetMesh(RingMesh, Ring);

	if (UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")))
	{
		UStaticMeshComponent* Pillar = NewObject<UStaticMeshComponent>(this);
		Pillar->SetStaticMesh(Cylinder);
		Pillar->SetupAttachment(Root);
		Pillar->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Pillar->SetCastShadow(false);
		Pillar->SetRelativeLocation(Base + FVector(0.0f, 0.0f, 600.0f));
		Pillar->SetRelativeScale3D(FVector(Radius / 50.0f, Radius / 50.0f, 12.0f));
		UMaterialInstanceDynamic* PillarMaterial = ChaosImpactBallTypes::MakeAdditive(this, Neon, 0.0f, 0.6f);
		Pillar->SetMaterial(0, PillarMaterial);
		Pillar->RegisterComponent();
		Pillars.SetNum(FMath::Max(Pillars.Num(), Place + 1));
		PillarMaterials.SetNum(FMath::Max(PillarMaterials.Num(), Place + 1));
		Pillars[Place] = Pillar;
		PillarMaterials[Place] = PillarMaterial;
	}

	// A rim light behind it in its colour.
	UPointLightComponent* Rim = NewObject<UPointLightComponent>(this);
	Rim->SetupAttachment(Root);
	Rim->SetRelativeLocation(Base + FVector(0.0f, -160.0f, Height + 260.0f));
	Rim->SetLightColor(Neon);
	Rim->SetIntensity(0.0f);
	Rim->SetAttenuationRadius(700.0f);
	Rim->SetCastShadows(false);
	Rim->RegisterComponent();
	RimLights.SetNum(FMath::Max(RimLights.Num(), Place + 1));
	RimLights[Place] = Rim;
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
	BuildPlace = 0;
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
		// One figure (and its pedestal, with the first of its place) a step.
		if (BuildPlace < Places.Num())
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
	BackGlowLight->SetLightColor(WinnerColor);

	IdleAnimation = LoadObject<UAnimSequenceBase>(nullptr, TEXT("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle.MM_Idle"));
	KneelAnimation = LoadObject<UAnimSequenceBase>(nullptr, TEXT("/Game/Characters/Mannequins/Anims/Death/MM_Death_Front_01.MM_Death_Front_01"));
	FallAnimation = LoadObject<UAnimSequenceBase>(nullptr, TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Jump/MM_Fall_Loop.MM_Fall_Loop"));
	LandAnimation = LoadObject<UAnimSequenceBase>(nullptr, TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Jump/MM_Land.MM_Land"));
	CheerAnimation = LoadObject<UAnimSequenceBase>(nullptr, TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Attack/MM_ChargedAttack.MM_ChargedAttack"));

	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	const auto AddMesh = [this](UStaticMesh* Mesh, const FVector& Location, const FRotator& Rotation, const FVector& Scale,
		UMaterialInterface* Material)
	{
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(this);
		Component->SetStaticMesh(Mesh);
		Component->SetupAttachment(Root);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetCastShadow(false);
		Component->SetRelativeLocationAndRotation(Location, Rotation);
		Component->SetRelativeScale3D(Scale);
		Component->SetMaterial(0, Material);
		Component->RegisterComponent();
		return Component;
	};

	// The arena: a dark floor, a dark wall, a glow of the winner's colour behind them, slashes of it across the wall.
	AddMesh(Cylinder, FVector(0.0f, -200.0f, -2.0f), FRotator::ZeroRotator, FVector(40.0f, 40.0f, 0.02f),
		ResultsLitMaterial(this, FLinearColor(0.012f, 0.014f, 0.022f), 0.18f));
	AddMesh(Cube, FVector(0.0f, -1400.0f, 600.0f), FRotator::ZeroRotator, FVector(70.0f, 0.2f, 24.0f),
		ChaosImpactBallTypes::MakeEmissive(this, WinnerColor * 0.012f + FLinearColor(0.004f, 0.005f, 0.01f), 1.0f));
	AddMesh(Sphere, FVector(0.0f, -1250.0f, 220.0f), FRotator::ZeroRotator, FVector(11.0f, 0.5f, 6.5f),
		ChaosImpactBallTypes::MakeAdditive(this, WinnerColor, 0.16f, 0.0f));
	FRandomStream Stream(1234);
	for (int32 Index = 0; Index < 11; ++Index)
	{
		const float Angle = Stream.FRandRange(22.0f, 36.0f);
		const float Thick = Index % 3 == 0 ? 0.22f : Stream.FRandRange(0.04f, 0.1f);
		const FLinearColor Color = Index % 4 == 0 ? FLinearColor(1.0f, 0.95f, 0.85f) : WinnerColor;
		Slashes.Add(AddMesh(Cube, FVector(Stream.FRandRange(-900.0f, 900.0f), -1180.0f - Index * 6.0f, Stream.FRandRange(-100.0f, 1100.0f)),
			FRotator(Angle, 0.0f, 0.0f), FVector(28.0f, 0.03f, Thick),
			ChaosImpactBallTypes::MakeEmissive(this, Color, Index % 4 == 0 ? 3.0f : Stream.FRandRange(4.0f, 9.0f))));
		SlashPhases.Add(Stream.FRand());
	}
	// Haze along the floor.
	for (int32 Index = 0; Index < 12; ++Index)
	{
		const float Size = Stream.FRandRange(3.5f, 6.5f);
		Haze.Add(AddMesh(Sphere, FVector(Stream.FRandRange(-900.0f, 900.0f), Stream.FRandRange(-1000.0f, -60.0f), Stream.FRandRange(10.0f, 50.0f)),
			FRotator::ZeroRotator, FVector(Size, Size * 0.8f, Stream.FRandRange(0.5f, 1.1f)),
			ChaosImpactBallTypes::MakeAdditive(this, FLinearColor(0.55f, 0.6f, 0.75f) * 0.5f + WinnerColor * 0.15f, 0.05f, 0.6f)));
	}

	// Who stands where: the winner (or winning team) in front, the next two behind on either side.
	Places.Reset();
	if (Data.bTeams)
	{
		for (int32 Rank = 0; Rank < FMath::Min(Data.TeamTotals.Num(), 3); ++Rank)
		{
			TArray<int32> Members;
			for (int32 Entry = 0; Entry < Data.Entries.Num(); ++Entry)
			{
				if (Data.Entries[Entry].Team == Data.TeamTotals[Rank].Key)
				{
					Members.Add(Entry);
				}
			}
			Places.Add(Members);
		}
	}
	else
	{
		for (int32 Entry = 0; Entry < FMath::Min(Data.Entries.Num(), 3); ++Entry)
		{
			Places.Add({Entry});
		}
	}
}

void AChaosImpactPodiumStage::BuildNextFigure()
{
	const FChaosImpactResultsData& Data = BuildData;
	UWorld* World = GetWorld();
	static const float Heights[] = {72.0f, 34.0f, 20.0f};
	static const FVector Bases[] = {FVector(0.0f, 0.0f, 0.0f), FVector(-290.0f, -360.0f, 0.0f), FVector(290.0f, -360.0f, 0.0f)};
	const int32 Place = BuildPlace;
	const int32 Count = FMath::Max(Places[Place].Num(), 1);
	const float Spacing = Place == 0 ? 96.0f : 82.0f;
	const FLinearColor Neon = Place == 0 ? WinnerColor : Data.bTeams && !Places[Place].IsEmpty()
		? ChaosImpactMatch::GetTeamColor(Data.Entries[Places[Place][0]].Team)
		: Place == 1 ? FLinearColor(0.75f, 0.85f, 1.0f) : FLinearColor(1.0f, 0.55f, 0.25f);
	if (BuildMember == 0)
	{
		const float Radius = (Place == 0 ? 92.0f : 74.0f) + Spacing * 0.5f * (Count - 1);
		AddPedestal(Bases[Place], Radius, Heights[Place], Neon, Place);
		if (Place == 0)
		{
			WinnerTopZ = Heights[0];
		}
	}
	if (Places[Place].IsValidIndex(BuildMember) && World)
	{
		const int32 Member = BuildMember;
		FFigure Figure;
		Figure.Entry = Places[Place][Member];
		Figure.Place = Place;
		Figure.Spot = Bases[Place] + FVector((Member - (Count - 1) * 0.5f) * Spacing, 0.0f, Heights[Place]);
		Figure.AppearAt = Place == 0 ? DropAt : Place == 1 ? SecondAt : ThirdAt;
		const FChaosImpactResultEntry& Entry = Data.Entries[Figure.Entry];
		FActorSpawnParameters Parameters;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Parameters.ObjectFlags |= RF_Transient;
		if (AChaosImpactCharacterPreview* Actor = World->SpawnActor<AChaosImpactCharacterPreview>(
			AChaosImpactCharacterPreview::StaticClass(), GetActorLocation() + Figure.Spot, FRotator::ZeroRotator, Parameters))
		{
			Actor->ShowLoadout(Entry.Character, Entry.Colour, Neon);
			Actor->UseAsFigure();
			// The winner faces the camera square; the others are turned a little in toward the winner.
			Actor->SetFacing(Place == 0 ? 0.0f : Place == 1 ? 18.0f : -18.0f, Place == 0 ? 7.0f : 0.0f);
			Actor->Animate(0.0f);
			Camera->ShowOnlyActors.Add(Actor);
			FigureActors.Add(Actor);
			Figures.Add(Figure);
		}
	}
	++BuildMember;
	if (BuildMember >= Count)
	{
		BuildMember = 0;
		++BuildPlace;
	}
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

	// ---- Camera: wide on the losers' entrance, low on the winner's landing, then back to take in the podium ----
	const FVector Wide(0.0f, 1150.0f - 140.0f * FMath::Clamp(S / BlackoutAt, 0.0f, 1.0f), 190.0f);
	const FVector WideLook(0.0f, -260.0f, 110.0f);
	const FVector Low(120.0f, 300.0f, 34.0f);
	const FVector LowLook(0.0f, 0.0f, WinnerTopZ + 120.0f);
	const FVector Final(0.0f, 900.0f, 155.0f);
	const FVector FinalLook(0.0f, -90.0f, 128.0f);
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
		const float Pull = ResultsEaseInOut((S - LandAt - 0.6f) / 1.6f);
		Location = FMath::Lerp(Low, Final, Pull);
		Look = FMath::Lerp(LowLook, FinalLook, Pull);
		Fov = FMath::Lerp(46.0f, 33.0f, Pull);
		// Settled, it drifts a little so the picture stays alive.
		const float Drift = S - LandAt - 2.2f;
		if (Drift > 0.0f)
		{
			const float In = FMath::Min(Drift / 1.5f, 1.0f);
			Location += FVector(FMath::Sin(Drift * 0.35f) * 60.0f, 0.0f, FMath::Sin(Drift * 0.27f) * 14.0f) * In;
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

	// ---- Lights and neon ----
	for (int32 Place = 0; Place < NeonMaterials.Num(); ++Place)
	{
		const float At = Place == 0 ? LandAt : Place == 1 ? SecondAt : ThirdAt;
		const float Since = S - At;
		const float Neon = Since < 0.0f ? 0.0f : 3.5f + 10.0f * FMath::Exp(-6.0f * Since) + 0.6f * FMath::Sin(S * 3.0f + Place);
		ResultsSetIntensity(NeonMaterials[Place], Neon);
		if (RimLights.IsValidIndex(Place) && RimLights[Place])
		{
			RimLights[Place]->SetIntensity(Since < 0.0f ? 0.0f : Place == 0 ? 42000.0f : 26000.0f);
		}
		if (PillarMaterials.IsValidIndex(Place) && PillarMaterials[Place])
		{
			const float Pillar = bWarming ? 1.0f : Since < 0.0f || Since > 0.7f ? 0.0f : 2.5f * (1.0f - Since / 0.7f);
			ResultsSetIntensity(PillarMaterials[Place], Pillar);
			Pillars[Place]->SetVisibility(Pillar > 0.0f);
			const float Wide2 = 1.0f + 1.5f * FMath::Clamp(Since / 0.7f, 0.0f, 1.0f);
			const FVector Scale = Pillars[Place]->GetRelativeScale3D();
			const float Base = Place == 0 ? 1.84f : 1.48f;
			Pillars[Place]->SetRelativeScale3D(FVector(Base * Wide2 * 0.5f, Base * Wide2 * 0.5f, Scale.Z));
		}
	}
	// Dark until the winner lands, then the spotlight crashes on.
	WinnerSpot->SetIntensity(SinceLand < 0.0f ? 0.0f : 52000.0f + 90000.0f * FMath::Exp(-5.0f * SinceLand));
	KeyLight->SetIntensity(S < BlackoutAt ? 22000.0f : SinceLand < 0.0f ? 4000.0f : 24000.0f);
	BackGlowLight->SetIntensity(SinceLand < 0.0f ? (S < BlackoutAt ? 18000.0f : 6000.0f) : 34000.0f);

	// ---- The back wall's slashes slide, the haze drifts ----
	for (int32 Index = 0; Index < Slashes.Num(); ++Index)
	{
		UStaticMeshComponent* Slash = Slashes[Index];
		const float Speed = 260.0f + 180.0f * SlashPhases[Index];
		const float Travel = FMath::Fmod(S * Speed + SlashPhases[Index] * 4000.0f, 4000.0f) - 2000.0f;
		const FVector Location2 = Slash->GetRelativeLocation();
		const FRotator Rotation = Slash->GetRelativeRotation();
		const FVector Along = Rotation.RotateVector(FVector::ForwardVector);
		const float BaseZ = -100.0f + 1200.0f * FMath::Frac(SlashPhases[Index] * 7.31f);
		Slash->SetRelativeLocation(FVector(Along.X * Travel, Location2.Y, BaseZ + Along.Z * Travel));
	}
	for (int32 Index = 0; Index < Haze.Num(); ++Index)
	{
		UStaticMeshComponent* Puff = Haze[Index];
		FVector Where = Puff->GetRelativeLocation();
		Where.X = FMath::Fmod(Where.X + DeltaSeconds * (25.0f + 10.0f * (Index % 3)) + 1000.0f, 2000.0f) - 1000.0f;
		Puff->SetRelativeLocation(Where);
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
			ResultsSetIntensity(ShockwaveMaterial, 6.0f * (1.0f - T));
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
		if (S < Figure.AppearAt)
		{
			Actor->SetActorHiddenInGame(true);
			continue;
		}
		if (!Figure.bShown)
		{
			Figure.bShown = true;
			Actor->SetActorHiddenInGame(false);
			if (Figure.Place == 0)
			{
				Actor->PlayFigureAnimation(FallAnimation, true);
			}
			else
			{
				// Down on one knee, and held there.
				Actor->PlayFigureAnimation(KneelAnimation, false, 0.62f);
			}
		}
		FVector Spot = Figure.Spot;
		if (Figure.Place == 0)
		{
			// The winner drops in from high above, faster and faster, and lands on the pedestal.
			const float T = FMath::Clamp((S - DropAt) / (LandAt - DropAt), 0.0f, 1.0f);
			Spot.Z += 760.0f * (1.0f - T * T);
			if (T >= 1.0f && !Figure.bLanded)
			{
				Figure.bLanded = true;
				Actor->PlayFigureAnimation(LandAnimation, false);
				Figure.NextCheerAt = S + 0.45f;
			}
			if (Figure.bLanded && S >= Figure.NextCheerAt)
			{
				// A victory punch, then idling, again every few seconds.
				Actor->PlayFigureAnimation(CheerAnimation, false);
				Figure.NextCheerAt = S + 7.0f;
				Figure.IdleAt = S + 2.2f;
			}
			else if (Figure.IdleAt > 0.0f && S >= Figure.IdleAt)
			{
				Actor->PlayFigureAnimation(IdleAnimation, true);
				Figure.IdleAt = -1.0f;
			}
		}
		Actor->SetActorLocation(GetActorLocation() + Spot);
		Actor->Animate(DeltaSeconds);
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
			OutWorld = FigureActors[Index]->GetActorLocation() + FVector(0.0f, 0.0f, Figures[Index].Place == 0 ? 250.0f : 175.0f);
			if (OutAppearAt)
			{
				*OutAppearAt = Figures[Index].AppearAt;
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

	const ResultsPaint::FPainter P{Design, Elements, Layer + 1};
	// Second and third (and their teams): a tag over each head as they appear.
	if (Podium && Page == 0)
	{
		for (int32 Entry = 0; Entry < Data.Entries.Num(); ++Entry)
		{
			FVector Head;
			FVector2D Spot;
			float AppearAt = 0.0f;
			const FChaosImpactResultEntry& Item = Data.Entries[Entry];
			const bool bWinnerSide = Data.bTeams ? (!Data.TeamTotals.IsEmpty() && Item.Team == Data.TeamTotals[0].Key) : Entry == 0;
			if (bWinnerSide || !Podium->GetFigureHead(Entry, Head, &AppearAt) || !Podium->Project(Head, Spot))
			{
				continue;
			}
			const float TagIn = ResultsPaint::EaseOut((S - AppearAt - 0.25f) / 0.3f);
			if (TagIn <= 0.0f)
			{
				continue;
			}
			// Picture position to design space: the picture covers the screen, design space is fitted inside it.
			const float Cover = FMath::Max(Size.X / AChaosImpactPodiumStage::PictureWidth, Size.Y / AChaosImpactPodiumStage::PictureHeight);
			const float Fit = FMath::Min(Size.X / 1600.0f, Size.Y / 900.0f);
			const FVector2D Pixel(Size.X * 0.5f + (Spot.X - 0.5f) * AChaosImpactPodiumStage::PictureWidth * Cover,
				Size.Y * 0.5f + (Spot.Y - 0.5f) * AChaosImpactPodiumStage::PictureHeight * Cover);
			const FVector2D Point((Pixel.X - Size.X * 0.5f) / Fit + 800.0f, (Pixel.Y - Size.Y * 0.5f) / Fit + 450.0f);
			const FGeometry TagSpace = ResultsPaint::MakeSkewed(Design, static_cast<float>(Point.X) - 110.0f, static_cast<float>(Point.Y) - 40.0f,
				220.0f, 56.0f, -0.2f);
			const ResultsPaint::FPainter Tag{TagSpace, Elements, Layer + 2, TagIn};
			ResultsPaint::RoundedBox(Tag, 0.0f, 0.0f, 220.0f, 56.0f, 8.0f, ResultsPaint::WithAlpha(ResultsPaint::Ink, 0.86f));
			Tag.Box(0.0f, 0.0f, 6.0f, 56.0f, Item.Color);
			const FString Place = Item.Rank == 1 ? TEXT("1ST") : Item.Rank == 2 ? TEXT("2ND") : Item.Rank == 3 ? TEXT("3RD") : FString::Printf(TEXT("%dTH"), Item.Rank);
			Tag.Text(Place, 18.0f, 4.0f, 18.0f, Item.Rank == 2 ? FLinearColor(0.8f, 0.88f, 1.0f) : FLinearColor(1.0f, 0.62f, 0.3f),
				ResultsPaint::ETextAlign::Left, TEXT("BlackItalic"));
			Tag.Text(FString::Printf(TEXT("%d pt"), Item.Points), 206.0f, 6.0f, 16.0f, ResultsPaint::Muted, ResultsPaint::ETextAlign::Right, TEXT("Bold"));
			Tag.Text(Item.Name, 18.0f, 26.0f, 20.0f, ResultsPaint::Paper, ResultsPaint::ETextAlign::Left, TEXT("Black"));
		}
	}

	// Fourth place and below: a column at the top right.
	if (Page == 0)
	{
		int32 Row = 0;
		for (int32 Entry = Data.bTeams ? Data.Entries.Num() : 3; Entry < Data.Entries.Num(); ++Entry, ++Row)
		{
			const float RowIn = ResultsPaint::EaseOut((S - Stage::SecondAt - 0.6f - Row * 0.08f) / 0.3f);
			if (RowIn <= 0.0f)
			{
				continue;
			}
			const FChaosImpactResultEntry& Item = Data.Entries[Entry];
			const float Y = 86.0f + Row * 40.0f;
			const ResultsPaint::FPainter R{Design, Elements, Layer + 2, RowIn};
			R.Box(1200.0f + (1.0f - RowIn) * 40.0f, Y, 360.0f, 34.0f, ResultsPaint::WithAlpha(ResultsPaint::Ink, 0.72f));
			R.Box(1200.0f + (1.0f - RowIn) * 40.0f, Y, 4.0f, 34.0f, Item.Color);
			R.Text(FString::FromInt(Item.Rank), 1226.0f, Y + 4.0f, 20.0f, ResultsPaint::Muted, ResultsPaint::ETextAlign::Center, TEXT("BlackItalic"));
			R.Text(Item.Name, 1250.0f, Y + 5.0f, 18.0f, ResultsPaint::Paper, ResultsPaint::ETextAlign::Left, TEXT("Bold"));
			R.Text(FString::Printf(TEXT("%d pt"), Item.Points), 1548.0f, Y + 6.0f, 17.0f, ResultsPaint::Paper, ResultsPaint::ETextAlign::Right, TEXT("Bold"));
		}
	}

	// WINNER: slammed in over a slash of the winner's colour, the name on a band below.
	if (SinceLand > 0.12f)
	{
		const float T = SinceLand - 0.12f;
		const float BandIn = ResultsPaint::EaseOut(T / 0.28f);
		const float Dim = Page == 1 ? 0.0f : 1.0f;
		const FGeometry Band = ResultsPaint::MakeSkewed(Design, -60.0f, 590.0f, 1700.0f, 120.0f, -0.32f);
		const ResultsPaint::FPainter B{Band, Elements, Layer + 3, Dim};
		B.Box(-200.0f, 8.0f, 1260.0f * BandIn, 92.0f, ResultsPaint::WithAlpha(Data.WinnerColor, 0.9f));
		B.Box(-200.0f, 104.0f, 940.0f * BandIn, 10.0f, ResultsPaint::Ice);
		B.Box(740.0f * BandIn - 200.0f, 104.0f, 320.0f * BandIn, 10.0f, ResultsPaint::Fire);
		B.Box(1080.0f * BandIn - 200.0f, 8.0f, 28.0f * BandIn, 92.0f, ResultsPaint::WithAlpha(ResultsPaint::Paper, 0.85f));
		const float NameIn = ResultsPaint::EaseOut((T - 0.15f) / 0.3f);
		const ResultsPaint::FPainter N{Band, Elements, Layer + 4, NameIn * Dim};
		FString Names = Data.WinnerName;
		if (Data.bTie)
		{
			TArray<FString> Tied;
			for (const FChaosImpactResultEntry& Item : Data.Entries)
			{
				if (Item.Rank == 1)
				{
					Tied.Add(Item.Name);
				}
			}
			Names = FString::Join(Tied, TEXT(" ・ "));
		}
		N.Text(Names, 120.0f + (1.0f - NameIn) * 120.0f, 16.0f, 62.0f, ResultsPaint::Paper, ResultsPaint::ETextAlign::Left, TEXT("BlackItalic"), 4.0f, ResultsPaint::Ink);
		if (!Data.Entries.IsEmpty())
		{
			const int32 Points = Data.bTeams && !Data.TeamTotals.IsEmpty() ? Data.TeamTotals[0].Value : Data.Entries[0].Points;
			const FString Line = Data.bTeams ? FString::Printf(TEXT("TEAM %d pt"), Points)
				: FString::Printf(TEXT("%d pt   KO %d"), Points, Data.Entries[0].Knockouts);
			N.Text(Line, 140.0f, 124.0f, 24.0f, ResultsPaint::Paper, ResultsPaint::ETextAlign::Left, TEXT("BlackItalic"), 2.0f, ResultsPaint::Ink);
		}
		// The headline lands big and settles, shaking a little.
		const float Slam = 1.0f + 0.7f * FMath::Exp(-11.0f * T);
		const float Shake = T < 0.3f ? FMath::Sin(T * 95.0f) * (0.3f - T) * 30.0f : 0.0f;
		const FGeometry Head = ResultsPaint::MakeSkewed(Design, 40.0f + Shake, 430.0f, 1000.0f, 170.0f, -0.18f, Slam);
		const ResultsPaint::FPainter H{Head, Elements, Layer + 5, ResultsPaint::EaseOut(T / 0.12f) * Dim};
		H.Text(Data.Headline, 30.0f, 0.0f, 132.0f, ResultsPaint::Paper, ResultsPaint::ETextAlign::Left, TEXT("BlackItalic"), 7.0f, Data.WinnerColor);
		if (Data.bTeams && !Data.bTie)
		{
			H.Text(FString::Printf(TEXT("%sの勝ち！"), *Data.WinnerName), 600.0f, 118.0f, 30.0f, ResultsPaint::Paper, ResultsPaint::ETextAlign::Left,
				TEXT("Black"), 3.0f, ResultsPaint::Ink);
		}

		// Settled: which button goes on.
		if (Page == 0 && S >= RevealSeconds)
		{
			const float Blink = 0.55f + 0.45f * FMath::Cos((S - RevealSeconds) * 4.0f);
			const ResultsPaint::FPainter Prompt{Design, Elements, Layer + 6, EaseOutPrompt(S - RevealSeconds) * Blink};
			Prompt.Box(560.0f, 806.0f, 480.0f, 50.0f, ResultsPaint::WithAlpha(ResultsPaint::Ink, 0.75f));
			Prompt.Box(560.0f, 806.0f, 480.0f, 3.0f, Data.WinnerColor);
			Prompt.Text(TEXT("ボタンを押して 成績へ ▶"), 800.0f, 815.0f, 24.0f, ResultsPaint::Paper, ResultsPaint::ETextAlign::Center,
				TEXT("BlackItalic"));
		}

		// Confetti for a while.
		if (!Data.bTie && T < 5.0f && Page == 0)
		{
			const FLinearColor Colors[] = {ResultsPaint::Gold, ResultsPaint::Paper, ResultsPaint::Ice, ResultsPaint::Fire, Data.WinnerColor};
			const ResultsPaint::FPainter Confetti{Design, Elements, Layer + 6, FMath::Clamp((5.0f - T) / 0.8f, 0.0f, 1.0f)};
			for (int32 Piece = 0; Piece < 90; ++Piece)
			{
				const float Speed = 200.0f + 300.0f * ResultsHash(Piece, 1.0f);
				const float Y = -60.0f + T * Speed - ResultsHash(Piece, 2.0f) * 500.0f;
				if (Y < -40.0f || Y > 920.0f)
				{
					continue;
				}
				const float X = ResultsHash(Piece, 3.0f) * 1700.0f - 50.0f + FMath::Sin(T * (2.0f + ResultsHash(Piece, 4.0f) * 3.0f) + Piece) * 20.0f;
				const float Turn = FMath::Abs(FMath::Sin(T * (4.0f + ResultsHash(Piece, 5.0f) * 6.0f) + Piece));
				Confetti.Box(X, Y, 3.0f + 7.0f * Turn, 11.0f, Colors[Piece % UE_ARRAY_COUNT(Colors)]);
			}
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
	P.Text(TEXT("成績とポイントの流れ"), 64.0f + Slide, 92.0f, 18.0f, ResultsPaint::Muted, ResultsPaint::ETextAlign::Left, TEXT("Bold"));

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
		P.Text(Column.Title, Column.X + Slide, TableTop + 5.0f, 15.0f, ResultsPaint::Muted, ResultsPaint::ETextAlign::Right, TEXT("Bold"));
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
		RT.Text(Item.Name, NameX + Slide, TextY, 20.0f, ResultsPaint::Paper, ResultsPaint::ETextAlign::Left, TEXT("Black"));
		// Burns of a fire zone count as hits too, so a few throws can make many: shown up to 100%.
		const int32 Accuracy = Item.Throws > 0 ? FMath::Min(100, FMath::RoundToInt(100.0f * Item.Hits / Item.Throws)) : 0;
		const FString Values[] = {FString::FromInt(Item.Points), FString::FromInt(Item.Knockouts), FString::FromInt(Item.Throws),
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
	G.Text(TEXT("ポイントの流れ"), 850.0f + Slide, 140.0f, 22.0f, ResultsPaint::Paper, ResultsPaint::ETextAlign::Left, TEXT("BlackItalic"));
	if (bOne)
	{
		const FChaosImpactResultEntry& Item = Data.Entries[GraphFocus];
		G.Box(1056.0f + Slide, 140.0f, 6.0f, 30.0f, Item.Color);
		G.Text(FString::Printf(TEXT("%s  %d pt"), *Item.Name, Item.Points), 1072.0f + Slide, 142.0f, 20.0f, Item.Color,
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
		CT.Text(Item.Name, CardWidth - 20.0f, Mid - Big * 0.4f, Big * 0.75f, ResultsPaint::Paper, ResultsPaint::ETextAlign::Right, TEXT("Black"));
	}
	return Layer + 3;
}
