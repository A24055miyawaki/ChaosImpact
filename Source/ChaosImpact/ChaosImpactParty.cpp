#include "ChaosImpactParty.h"
#include "ChaosImpact.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactBallFormComponent.h"
#include "ChaosImpactBallTypes.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactEase.h"
#include "ChaosImpactFxKit.h"
#include "ChaosImpactGameMode.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactIceMeshes.h"
#include "ChaosImpactMatchTypes.h"
#include "ChaosImpactPaint.h"
#include "ChaosImpactSfx.h"
#include "ChaosImpactStageBase.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MeshDescription.h"
#include "Net/UnrealNetwork.h"
#include "ProceduralMeshComponent.h"
#include "StaticMeshAttributes.h"

// =====================================================================================================================
// Shared pieces
// =====================================================================================================================

namespace
{
	const TCHAR* const PRGemMaterialPath = TEXT("/Game/ChaosImpact/Party/M_CI_Gem.M_CI_Gem");
	const TCHAR* const PRGlowMaterialPath = TEXT("/Game/ChaosImpact/Party/M_CI_GemGlow.M_CI_GemGlow");
	const TCHAR* const PRGoldMaterialPath = TEXT("/Game/ChaosImpact/Party/M_CI_CrownGold.M_CI_CrownGold");
	const TCHAR* const PRLitPath = TEXT("/Game/LevelPrototyping/Materials/M_FlatCol.M_FlatCol");

	const AChaosImpactGameState* PRMatch(const UWorld* World)
	{
		return World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
	}

	double PRServerNow(const UWorld* World)
	{
		const AGameStateBase* State = World ? World->GetGameState() : nullptr;
		return State ? State->GetServerWorldTimeSeconds() : World ? World->GetTimeSeconds() : 0.0;
	}

	/** This machine's own player (not a CPU, not someone else's). */
	bool PRIsOwnPlayer(const APawn* Pawn)
	{
		return Pawn && Pawn->IsPlayerControlled() && Pawn->IsLocallyControlled();
	}

	bool PRCanPickUp(const AChaosImpactCharacter* Character)
	{
		return IsValid(Character) && !Character->IsEliminated() && !Character->IsHidden() && !Character->IsActorBeingDestroyed()
			&& !(Character->GetBallForm() && Character->GetBallForm()->IsCaptured()) && !Character->IsMatchInputLocked();
	}

	bool PRInMatch(const UWorld* World)
	{
		const AChaosImpactGameState* Match = PRMatch(World);
		return Match && Match->bVersusMatch && Match->Phase == EChaosImpactOnlinePhase::Match;
	}

	/** Guests in the room (whose touches reach the host up to AChaosImpactBall::FairTouchReachSeconds late). */
	bool PRHasGuests(const UWorld* World)
	{
		if (!World || World->GetNetMode() != NM_ListenServer)
		{
			return false;
		}
		const AGameStateBase* State = World->GetGameState();
		for (const APlayerState* Member : State ? State->PlayerArray : TArray<TObjectPtr<APlayerState>>())
		{
			const AChaosImpactPlayerState* Own = Cast<AChaosImpactPlayerState>(Member);
			if (Own && !Own->IsABot() && !Own->bHostMachine && !Own->bSpectating)
			{
				return true;
			}
		}
		return false;
	}

	/** Answers a claim: this machine's own player directly, a guest through their character (CPUs need no answer). */
	void PRAnswer(AChaosImpactRubyField* Field, AChaosImpactCharacter* Character, const int32 Id, const bool bAccepted)
	{
		if (!IsValid(Character))
		{
			return;
		}
		if (PRIsOwnPlayer(Character))
		{
			Field->OnLocalClaimResolved(Character, Id, bAccepted);
		}
		else if (Character->IsPlayerControlled())
		{
			Character->ClientPartyClaimResolved(Id, bAccepted);
		}
	}

	void PRAnswer(AChaosImpactCrown* Crown, AChaosImpactCharacter* Character, const bool bAccepted)
	{
		if (!IsValid(Character))
		{
			return;
		}
		if (PRIsOwnPlayer(Character))
		{
			Crown->OnLocalClaimResolved(Character, bAccepted);
		}
		else if (Character->IsPlayerControlled())
		{
			Character->ClientPartyClaimResolved(ChaosImpactParty::CrownId, bAccepted);
		}
	}

	/** The cameras of this machine's players (effects near any of them are worth drawing). */
	TArray<FVector, TInlineAllocator<4>> PRCameras(const UWorld* World)
	{
		TArray<FVector, TInlineAllocator<4>> Out;
		if (const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr)
		{
			for (const ULocalPlayer* Local : GameInstance->GetLocalPlayers())
			{
				const APlayerController* Controller = Local ? Local->GetPlayerController(World) : nullptr;
				if (Controller && Controller->PlayerCameraManager)
				{
					Out.Add(Controller->PlayerCameraManager->GetCameraLocation());
				}
			}
		}
		return Out;
	}

	float PRNearestCamera(const TArray<FVector, TInlineAllocator<4>>& Cameras, const FVector& At)
	{
		float Best = UE_BIG_NUMBER;
		for (const FVector& Camera : Cameras)
		{
			Best = FMath::Min(Best, static_cast<float>(FVector::Dist(Camera, At)));
		}
		return Best;
	}

	FVector PRCameraRight(const UWorld* World)
	{
		const APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		return Controller && Controller->PlayerCameraManager
			? FRotationMatrix(Controller->PlayerCameraManager->GetCameraRotation()).GetScaledAxis(EAxis::Y) : FVector::RightVector;
	}

	UMaterialInterface* PRLoad(const TCHAR* Path)
	{
		return LoadObject<UMaterialInterface>(nullptr, Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}

	UStaticMesh* PRSphere()
	{
		static TWeakObjectPtr<UStaticMesh> Sphere;
		if (!Sphere.IsValid())
		{
			Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		}
		return Sphere.Get();
	}

	/** A piece of an effect, with the common parts filled in. */
	FChaosImpactFxPiece PRPiece(const EChaosImpactFxLook Look, const FVector& At, const float Life, const float From, const float To,
		const FLinearColor& Color, const FLinearColor& Core, const float Intensity)
	{
		FChaosImpactFxPiece Piece;
		Piece.Look = Look;
		Piece.Location = At;
		Piece.Life = Life;
		Piece.SizeFrom = From;
		Piece.SizeTo = To;
		Piece.Color = Color;
		Piece.CoreColor = Core;
		Piece.Intensity = Intensity;
		Piece.NoiseAmount = 0.25f;
		Piece.ErodeTo = 0.5f;
		return Piece;
	}

	FVector PRUpward(FRandomStream& Stream, const float Lift)
	{
		const float Angle = Stream.FRandRange(0.0f, UE_TWO_PI);
		const float Up = Stream.FRandRange(Lift, 1.0f);
		const float Across = FMath::Sqrt(FMath::Max(0.0f, 1.0f - Up * Up));
		return FVector(FMath::Cos(Angle) * Across, FMath::Sin(Angle) * Across, Up);
	}
}

FLinearColor ChaosImpactParty::GetRuleColor(const FChaosImpactMatchRules& Rules)
{
	return Rules.IsCrownKeep() ? CrownColor : RubyColor;
}

float ChaosImpactParty::GetSpeedScale(const AChaosImpactCharacter* Character)
{
	const AChaosImpactCrown* Crown = Character ? AChaosImpactCrown::Get(Character->GetWorld()) : nullptr;
	return Crown && Crown->GetHolder() == Character ? CrownSpeedScale : 1.0f;
}

int32 ChaosImpactParty::GetShownCount(const UWorld* World, const APlayerState* Member)
{
	const AChaosImpactGameState* Match = PRMatch(World);
	const AChaosImpactPlayerState* Own = Cast<AChaosImpactPlayerState>(Member);
	if (!Match || !Own)
	{
		return 0;
	}
	if (Match->Rules.IsCrownKeep())
	{
		return FMath::Max(0, ChaosImpactMatch::CrownGoalCounts - Own->Points);
	}
	const AChaosImpactRubyField* Field = AChaosImpactRubyField::Get(World);
	return FMath::Max(0, Own->Points + (Field ? Field->GetPendingValue(Own->GetPawn()) : 0));
}

double ChaosImpactParty::GetCountGainedAt(const UWorld* World, const APlayerState* Member)
{
	const AChaosImpactGameState* Match = PRMatch(World);
	if (!Match || !Member)
	{
		return -100.0;
	}
	if (Match->Rules.IsCrownKeep())
	{
		const AChaosImpactCrown* Crown = AChaosImpactCrown::Get(World);
		const AChaosImpactPlayerState* Own = Cast<AChaosImpactPlayerState>(Member);
		return Crown && Own && Own->Points > 0 && Crown->GetHolderState() == Member ? Crown->GetState().CountFrom : -100.0;
	}
	const AChaosImpactRubyField* Field = AChaosImpactRubyField::Get(World);
	return Field ? Field->GetCountGainedAt(Member) : -100.0;
}

FVector ChaosImpactParty::NearestPoint(const TArray<FVector>& Points, const FVector& Location)
{
	FVector Best = Location;
	double BestDistance = TNumericLimits<double>::Max();
	for (const FVector& Point : Points)
	{
		const double Distance = FVector::DistSquared(Point, Location);
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = Point;
		}
	}
	return Best;
}

bool ChaosImpactParty::FindFloor(const UWorld* World, const FVector& Location, const float Up, const float Down, FVector& OutFloor)
{
	if (!World)
	{
		return false;
	}
	FHitResult Hit;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(PartyFloor), false);
	if (!World->LineTraceSingleByObjectType(Hit, Location + FVector(0, 0, Up), Location - FVector(0, 0, Down),
		FCollisionObjectQueryParams(ECC_WorldStatic), Query) || Hit.ImpactNormal.Z < 0.72f)
	{
		return false;
	}
	OutFloor = Hit.ImpactPoint;
	return true;
}

bool ChaosImpactParty::IsStandingRoom(const UWorld* World, const FVector& Floor)
{
	FCollisionQueryParams Query(SCENE_QUERY_STAT(PartyRoom), false);
	return World && !World->OverlapAnyTestByObjectType(Floor + FVector(0, 0, 86), FQuat::Identity,
		FCollisionObjectQueryParams(ECC_WorldStatic), FCollisionShape::MakeCapsule(34.0f, 62.0f), Query);
}

FVector ChaosImpactParty::FindLandSpot(UWorld* World, const AChaosImpactStageBase* Stage, const TArray<FVector>& Points,
	const FVector& Origin, const float MinDistance, const float MaxDistance, FRandomStream& Stream)
{
	FVector Base = Origin;
	const bool bOnFloor = FindFloor(World, Origin, 60.0f, 900.0f, Base) && (!Stage || !Stage->IsOutside(Base));
	if (!bOnFloor)
	{
		Base = NearestPoint(Points, Origin);
	}
	FCollisionQueryParams Query(SCENE_QUERY_STAT(PartyLand), false);
	for (int32 Try = 0; Try < 10 && World; ++Try)
	{
		const float Angle = Stream.FRandRange(0.0f, UE_TWO_PI);
		const float Distance = Stream.FRandRange(MinDistance, MaxDistance) * (Try < 6 ? 1.0f : 0.6f);
		const FVector Try2D = Base + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * Distance;
		FVector Floor;
		if (!FindFloor(World, Try2D, 260.0f, 520.0f, Floor) || FMath::Abs(Floor.Z - Base.Z) > 230.0f || (Stage && Stage->IsOutside(Floor))
			|| !IsStandingRoom(World, Floor))
		{
			continue;
		}
		FHitResult Blocked;
		if (World->LineTraceSingleByObjectType(Blocked, Base + FVector(0, 0, 60), Floor + FVector(0, 0, 60),
			FCollisionObjectQueryParams(ECC_WorldStatic), Query))
		{
			continue;
		}
		return Floor;
	}
	// Nowhere clear round it: the nearest of the stage's own spots (a little apart from each other's).
	TArray<FVector> Near;
	for (const FVector& Point : Points)
	{
		if (FVector::Dist2D(Point, Base) < MaxDistance * 1.6f)
		{
			Near.Add(Point);
		}
	}
	return Near.IsEmpty() ? NearestPoint(Points, Base) : Near[Stream.RandRange(0, Near.Num() - 1)];
}

// ---- The gem -------------------------------------------------------------------------------------------------------

UStaticMesh* ChaosImpactParty::GetGemMesh()
{
	static TStrongObjectPtr<UStaticMesh> Built;
	if (Built.IsValid())
	{
		return Built.Get();
	}
	// A round brilliant, a little tall: an octagonal table, sixteen-sided girdle, a pavilion down to a point. Every facet
	// flat (its own normal), so each one flashes on its own as the gem turns.
	constexpr int32 Sides = 8;
	constexpr float TableRadius = 0.52f;
	constexpr float TableHeight = 0.42f;
	constexpr float GirdleHeight = 0.0f;
	constexpr float CuletDepth = -0.98f;
	TArray<FVector3f> Table;
	TArray<FVector3f> Girdle;
	for (int32 Index = 0; Index < Sides; ++Index)
	{
		const float Angle = UE_TWO_PI * Index / Sides;
		Table.Add(FVector3f(FMath::Cos(Angle) * TableRadius, FMath::Sin(Angle) * TableRadius, TableHeight));
	}
	for (int32 Index = 0; Index < Sides * 2; ++Index)
	{
		const float Angle = UE_TWO_PI * (Index - 0.5f) / (Sides * 2);
		// The girdle dips a touch between its points (a cut stone's scalloped edge).
		const float Z = GirdleHeight + (Index % 2 == 0 ? 0.035f : -0.035f);
		Girdle.Add(FVector3f(FMath::Cos(Angle), FMath::Sin(Angle), Z));
	}
	const FVector3f Culet(0.0f, 0.0f, CuletDepth);
	const FVector3f TableCentre(0.0f, 0.0f, TableHeight);
	TArray<FVector3f> Tris;
	const auto Tri = [&Tris](const FVector3f& A, const FVector3f& B, const FVector3f& C) { Tris.Add(A); Tris.Add(B); Tris.Add(C); };
	for (int32 Index = 0; Index < Sides; ++Index)
	{
		const FVector3f& T0 = Table[Index];
		const FVector3f& T1 = Table[(Index + 1) % Sides];
		const FVector3f& G0 = Girdle[Index * 2];
		const FVector3f& G1 = Girdle[Index * 2 + 1];
		const FVector3f& G2 = Girdle[(Index * 2 + 2) % (Sides * 2)];
		Tri(TableCentre, T0, T1);
		Tri(T0, G0, G1);
		Tri(T0, G1, T1);
		Tri(T1, G1, G2);
		// Pavilion: a main facet down to the culet and two half facets at the girdle.
		const FVector3f Mid = FMath::Lerp(G1, Culet, 0.45f);
		Tri(G0, Mid, G1);
		Tri(G1, Mid, G2);
		Tri(G0, Culet, Mid);
		Tri(Mid, Culet, G2);
	}
	FMeshDescription Description;
	FStaticMeshAttributes Attributes(Description);
	Attributes.Register();
	TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> Normals = Attributes.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector3f> Tangents = Attributes.GetVertexInstanceTangents();
	TVertexInstanceAttributesRef<float> Signs = Attributes.GetVertexInstanceBinormalSigns();
	TVertexInstanceAttributesRef<FVector2f> UVs = Attributes.GetVertexInstanceUVs();
	TPolygonGroupAttributesRef<FName> SlotNames = Attributes.GetPolygonGroupMaterialSlotNames();
	UVs.SetNumChannels(1);
	const FPolygonGroupID Group = Description.CreatePolygonGroup();
	SlotNames[Group] = TEXT("Gem");
	for (int32 Index = 0; Index + 2 < Tris.Num(); Index += 3)
	{
		FVector3f A = Tris[Index];
		FVector3f B = Tris[Index + 1];
		FVector3f C = Tris[Index + 2];
		FVector3f Normal = FVector3f::CrossProduct(C - A, B - A).GetSafeNormal();
		// Outward (the gem is convex about its middle).
		if (FVector3f::DotProduct(Normal, (A + B + C) / 3.0f - FVector3f(0.0f, 0.0f, -0.2f)) < 0.0f)
		{
			Swap(B, C);
			Normal = -Normal;
		}
		const FVector3f Tangent = (B - A).GetSafeNormal();
		FVertexInstanceID Corners[3];
		const FVector3f Points[3] = {A, B, C};
		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			const FVertexID Vertex = Description.CreateVertex();
			Positions[Vertex] = Points[Corner];
			Corners[Corner] = Description.CreateVertexInstance(Vertex);
			Normals[Corners[Corner]] = Normal;
			Tangents[Corners[Corner]] = Tangent;
			Signs[Corners[Corner]] = 1.0f;
			UVs.Set(Corners[Corner], 0, FVector2f(Points[Corner].X * 0.5f + 0.5f, Points[Corner].Z * 0.5f + 0.5f));
		}
		Description.CreateTriangle(Group, MakeArrayView(Corners, 3));
	}
	UStaticMesh* Mesh = NewObject<UStaticMesh>(GetTransientPackage(), TEXT("CI_PartyGem"), RF_Transient);
	// (Slot name only: the imported-slot argument exists in editor builds alone.)
	Mesh->GetStaticMaterials().Add(FStaticMaterial(nullptr, TEXT("Gem")));
	UStaticMesh::FBuildMeshDescriptionsParams Params;
	Params.bFastBuild = true;
	Params.bBuildSimpleCollision = false;
	Params.bMarkPackageDirty = false;
	Params.bCommitMeshDescription = false;
	Params.bAllowCpuAccess = false;
	TArray<const FMeshDescription*> Descriptions{&Description};
	if (!Mesh->BuildFromMeshDescriptions(Descriptions, Params))
	{
		UE_LOG(LogChaosImpact, Warning, TEXT("Party: the gem mesh could not be built"));
	}
	Built.Reset(Mesh);
	return Mesh;
}

UMaterialInstanceDynamic* ChaosImpactParty::MakeGemMaterial(UObject* Outer, const FLinearColor& Body, const FLinearColor& FireColor, const float Glow)
{
	if (UMaterialInterface* Parent = PRLoad(PRGemMaterialPath))
	{
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Parent, Outer);
		Material->SetVectorParameterValue(TEXT("Body"), Body);
		Material->SetVectorParameterValue(TEXT("Fire"), FireColor);
		Material->SetScalarParameterValue(TEXT("Glow"), Glow);
		return Material;
	}
	return ChaosImpactBallTypes::MakeEmissive(Outer, FireColor, 1.0f + Glow);
}

UMaterialInstanceDynamic* ChaosImpactParty::MakeGemGlowMaterial(UObject* Outer, const FLinearColor& Color, const float Intensity)
{
	if (UMaterialInterface* Parent = PRLoad(PRGlowMaterialPath))
	{
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Parent, Outer);
		Material->SetVectorParameterValue(TEXT("Color"), Color);
		Material->SetScalarParameterValue(TEXT("Intensity"), Intensity);
		return Material;
	}
	return ChaosImpactBallTypes::MakeAdditive(Outer, Color, Intensity, 0.0f);
}

UMaterialInstanceDynamic* ChaosImpactParty::MakeGoldMaterial(UObject* Outer, const float Glow)
{
	if (UMaterialInterface* Parent = PRLoad(PRGoldMaterialPath))
	{
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Parent, Outer);
		Material->SetScalarParameterValue(TEXT("Glow"), Glow);
		return Material;
	}
	UMaterialInterface* Lit = PRLoad(PRLitPath);
	UMaterialInstanceDynamic* Material = Lit ? UMaterialInstanceDynamic::Create(Lit, Outer) : nullptr;
	if (Material)
	{
		Material->SetVectorParameterValue(TEXT("Color"), CrownColor);
		Material->SetVectorParameterValue(TEXT("Base Color"), CrownColor);
		Material->SetScalarParameterValue(TEXT("Roughness"), 0.25f);
	}
	return Material;
}

// ---- HUD pictures ---------------------------------------------------------------------------------------------------

void ChaosImpactParty::PaintRuby(const ChaosImpactPaint::FPainter& P, const FVector2D& Center, const float Size, const float Time, const float Shine)
{
	using namespace ChaosImpactPaint;
	const auto At = [&Center, Size](const float X, const float Y) { return Center + FVector2D(X, Y) * Size; };
	// The stone seen from the side: table, crown facets to the girdle, pavilion down to the point.
	const FVector2D TL = At(-0.42f, -0.64f), TR = At(0.42f, -0.64f), L = At(-1.0f, -0.2f), R = At(1.0f, -0.2f), B = At(0.0f, 1.0f);
	const FVector2D ML = At(-0.48f, -0.2f), MM = At(0.0f, -0.2f), MR = At(0.48f, -0.2f);
	const TArray<FVector2D> Outline{TL, TR, R, B, L};
	HudPoly(P, HudInflate(Outline, FMath::Max(1.5f, Size * 0.12f)), SolidFill(Ink), 1);
	const FPainter Face = HudAbove(P, 1);
	const FLinearColor Light(1.0f, 0.62f, 0.72f, 1.0f), Mid(1.0f, 0.13f, 0.32f, 1.0f), Dark(0.64f, 0.02f, 0.15f, 1.0f), Deep(0.34f, 0.0f, 0.08f, 1.0f);
	// The light runs round the facets slowly, so the stone seems to turn.
	const float Turn = Time * 1.3f;
	const auto Lit = [Turn](const float Phase, const FLinearColor& A, const FLinearColor& Bc)
	{
		return FMath::Lerp(A, Bc, 0.5f + 0.5f * FMath::Sin(Turn + Phase));
	};
	HudPoly(Face, {TL, TR, MR, ML}, MakeFill(Lit(0.0f, Light, FLinearColor(1.0f, 0.86f, 0.9f, 1.0f)), Mid), 2);
	HudPoly(Face, {TL, ML, L}, SolidFill(Lit(1.2f, Mid, Light)), 1);
	HudPoly(Face, {TR, R, MR}, SolidFill(Lit(2.4f, Dark, Mid)), 1);
	HudPoly(Face, {L, ML, B}, SolidFill(Lit(3.1f, Dark, Mid)), 1);
	HudPoly(Face, {ML, MM, B}, SolidFill(Lit(3.9f, Mid, Light)), 1);
	HudPoly(Face, {MM, MR, B}, SolidFill(Lit(4.7f, Dark, Mid)), 1);
	HudPoly(Face, {MR, R, B}, SolidFill(Lit(5.5f, Deep, Dark)), 1);
	const FPainter Top = HudAbove(P, 2);
	Top.Line(L, R, WithAlpha(FLinearColor(1.0f, 0.8f, 0.86f, 1.0f), 0.55f), FMath::Max(1.0f, Size * 0.05f));
	// A glint now and then at the table's corner.
	if (Shine > 0.0f && Size >= 5.0f)
	{
		const float Cycle = FMath::Fmod(Time + Center.X * 0.0007f, 2.4f);
		const float Glint = Cycle < 0.5f ? FMath::Sin(Cycle / 0.5f * PI) * Shine : 0.0f;
		const FVector2D Spark = At(-0.3f, -0.5f);
		Top.Disc(Spark, Size * (0.09f + 0.08f * Glint), WithAlpha(FLinearColor::White, 0.75f + 0.25f * Glint));
		if (Glint > 0.05f)
		{
			const float Arm = Size * 0.42f * Glint;
			Top.Line(Spark - FVector2D(Arm, 0.0), Spark + FVector2D(Arm, 0.0), WithAlpha(FLinearColor::White, 0.9f * Glint), FMath::Max(1.0f, Size * 0.06f));
			Top.Line(Spark - FVector2D(0.0, Arm), Spark + FVector2D(0.0, Arm), WithAlpha(FLinearColor::White, 0.9f * Glint), FMath::Max(1.0f, Size * 0.06f));
		}
	}
}

void ChaosImpactParty::PaintCrown(const ChaosImpactPaint::FPainter& P, const FVector2D& Center, const float Size, const float Time, const float Shine)
{
	using namespace ChaosImpactPaint;
	const auto At = [&Center, Size](const float X, const float Y) { return Center + FVector2D(X, Y) * Size; };
	const FHudFill Gilt = MakeFill(FLinearColor(1.0f, 0.95f, 0.6f, 1.0f), FLinearColor(0.86f, 0.42f, 0.04f, 1.0f));
	// Convex pieces: the band, and three points rising from it (a tall middle one).
	const TArray<FVector2D> Band{At(-0.82f, 0.12f), At(0.82f, 0.12f), At(0.76f, 0.6f), At(-0.76f, 0.6f)};
	const TArray<FVector2D> Left{At(-0.82f, 0.16f), At(-0.98f, -0.5f), At(-0.3f, 0.16f)};
	const TArray<FVector2D> Middle{At(-0.4f, 0.16f), At(0.0f, -0.78f), At(0.4f, 0.16f)};
	const TArray<FVector2D> Right{At(0.3f, 0.16f), At(0.98f, -0.5f), At(0.82f, 0.16f)};
	const float Edge = FMath::Max(1.5f, Size * 0.11f);
	for (const TArray<FVector2D>* Piece : {&Band, &Left, &Middle, &Right})
	{
		HudPoly(P, HudInflate(*Piece, Edge), SolidFill(Ink), 1);
	}
	const FPainter Face = HudAbove(P, 1);
	for (const TArray<FVector2D>* Piece : {&Left, &Middle, &Right})
	{
		HudPoly(Face, *Piece, Gilt, 3, Center.Y - Size * 0.8f, Center.Y + Size * 0.6f);
	}
	HudPoly(Face, Band, Gilt, 3, Center.Y - Size * 0.8f, Center.Y + Size * 0.6f);
	const FPainter Top = HudAbove(P, 2);
	Top.Line(At(-0.8f, 0.2f), At(0.8f, 0.2f), WithAlpha(FLinearColor(1.0f, 1.0f, 0.85f, 1.0f), 0.7f), FMath::Max(1.0f, Size * 0.05f));
	for (const FVector2D& Tip : {At(-0.98f, -0.5f), At(0.0f, -0.78f), At(0.98f, -0.5f)})
	{
		Top.Disc(Tip, Size * 0.12f, FLinearColor(1.0f, 0.96f, 0.7f, 1.0f));
	}
	// Its stones: ruby in the middle, sapphires either side.
	Top.Disc(At(0.0f, 0.37f), Size * 0.13f, FLinearColor(1.0f, 0.12f, 0.3f, 1.0f));
	Top.Disc(At(-0.47f, 0.37f), Size * 0.09f, FLinearColor(0.2f, 0.55f, 1.0f, 1.0f));
	Top.Disc(At(0.47f, 0.37f), Size * 0.09f, FLinearColor(0.2f, 0.55f, 1.0f, 1.0f));
	if (Shine > 0.0f && Size >= 5.0f)
	{
		// A bright sweep along the band now and then.
		const float Cycle = FMath::Fmod(Time, 2.8f);
		if (Cycle < 0.45f)
		{
			const float X = FMath::Lerp(-0.9f, 0.9f, Cycle / 0.45f);
			Top.Line(At(X + 0.1f, 0.14f), At(X - 0.06f, 0.58f), WithAlpha(FLinearColor::White, 0.6f * Shine), FMath::Max(1.5f, Size * 0.12f));
		}
	}
}

// =====================================================================================================================
// ルビーラッシュ: the rubies
// =====================================================================================================================

namespace
{
	/** A ruby's size (its radius in centimetres) and how high it floats over the floor. */
	float PRGemRadius(const uint8 Value) { return Value >= ChaosImpactParty::HugeRubyValue ? 40.0f : Value > 1 ? 28.0f : 19.5f; }
	float PRGemHover(const uint8 Value) { return Value >= ChaosImpactParty::HugeRubyValue ? 62.0f : Value > 1 ? 46.0f : 36.0f; }
	/** How grand a gem's effects are (1 ordinary, 1.45 large, 2 huge). */
	float PRGemGrandeur(const uint8 Value) { return Value >= ChaosImpactParty::HugeRubyValue ? 2.0f : Value > 1 ? 1.45f : 1.0f; }
	/** A ruby's glow is this many times its radius (the engine sphere is 50 across its radius): a soft halo, the stone clear in it. */
	constexpr float PRGlowScale = 1.85f;
	constexpr float PRFlyInSeconds = 0.2f;
	/** A taken ruby stays in the list this long (so every machine hears who took it) before it goes. */
	constexpr double PRCollectedKeepSeconds = 0.8;
}

AChaosImpactRubyField::AChaosImpactRubyField()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(30.0f);
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

void AChaosImpactRubyField::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AChaosImpactRubyField, Gems);
}

AChaosImpactRubyField* AChaosImpactRubyField::Get(const UWorld* World)
{
	if (const AChaosImpactGameState* Match = PRMatch(World); Match && IsValid(Match->RubyField))
	{
		return Match->RubyField;
	}
	if (World)
	{
		for (TActorIterator<AChaosImpactRubyField> It(const_cast<UWorld*>(World)); It; ++It)
		{
			if (!It->IsActorBeingDestroyed())
			{
				return *It;
			}
		}
	}
	return nullptr;
}

void AChaosImpactRubyField::BeginPlay()
{
	Super::BeginPlay();
	VisualStream.Initialize(static_cast<int32>(FPlatformTime::Cycles()));
	ServerStream.Initialize(static_cast<int32>(FPlatformTime::Cycles() ^ 0x5eed));
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	UStaticMesh* GemMesh = ChaosImpactParty::GetGemMesh();
	GemMaterial = ChaosImpactParty::MakeGemMaterial(this, FLinearColor(0.55f, 0.0f, 0.02f), FLinearColor(1.0f, 0.0f, 0.05f), 0.38f);
	GlowMaterial = ChaosImpactParty::MakeGemGlowMaterial(this, FLinearColor(1.0f, 0.02f, 0.12f), 0.18f);
	const auto MakeInstances = [this](const TCHAR* Name, UStaticMesh* Mesh, UMaterialInterface* Material, const bool bShadow)
	{
		UInstancedStaticMeshComponent* Instances = NewObject<UInstancedStaticMeshComponent>(this, Name);
		Instances->SetupAttachment(Root);
		Instances->SetStaticMesh(Mesh);
		Instances->SetMaterial(0, Material);
		Instances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Instances->SetGenerateOverlapEvents(false);
		Instances->SetCastShadow(bShadow);
		Instances->SetUsingAbsoluteLocation(true);
		Instances->SetUsingAbsoluteRotation(true);
		Instances->SetUsingAbsoluteScale(true);
		Instances->NumCustomDataFloats = 1;
		Instances->RegisterComponent();
		return Instances;
	};
	GemInstances = MakeInstances(TEXT("Gems"), GemMesh, GemMaterial, true);
	GlowInstances = MakeInstances(TEXT("GemGlows"), PRSphere(), GlowMaterial, false);
}

double AChaosImpactRubyField::GetNow() const
{
	return PRServerNow(GetWorld());
}

FChaosImpactRubyGem* AChaosImpactRubyField::FindGem(const int32 Id)
{
	return Gems.Items.FindByPredicate([Id](const FChaosImpactRubyGem& Gem) { return Gem.Id == Id; });
}

const FChaosImpactRubyGem* AChaosImpactRubyField::FindGem(const int32 Id) const
{
	return Gems.Items.FindByPredicate([Id](const FChaosImpactRubyGem& Gem) { return Gem.Id == Id; });
}

bool AChaosImpactRubyField::IsAvailable(const FChaosImpactRubyGem& Gem, const double Now) const
{
	if (Gem.IsCollected() || Now < Gem.GetLandedAt() - 0.06)
	{
		return false;
	}
	return !Gem.bDropped || Now < Gem.GetLandedAt() + ChaosImpactParty::DroppedRubyLifeSeconds;
}

bool AChaosImpactRubyField::CanTake(const FChaosImpactRubyGem& Gem, const APawn* Pawn, const double Now) const
{
	return !(Gem.Dropper && Gem.Dropper == Pawn && Now < Gem.LaunchAt + ChaosImpactParty::DropperBlockSeconds);
}

int32 AChaosImpactRubyField::CountLying() const
{
	int32 Count = 0;
	for (const FChaosImpactRubyGem& Gem : Gems.Items)
	{
		Count += Gem.IsCollected() ? 0 : 1;
	}
	return Count;
}

FVector AChaosImpactRubyField::GetNearestFieldPoint(const FVector& Location) const
{
	return ChaosImpactParty::NearestPoint(FieldPoints, Location);
}

void AChaosImpactRubyField::AddGem(const FVector& From, const FVector& To, const double LaunchAt, const float Flight, const uint8 Value,
	const bool bDropped, APawn* Dropper)
{
	FChaosImpactRubyGem& Gem = Gems.Items.AddDefaulted_GetRef();
	Gem.Id = NextId++;
	Gem.Value = Value;
	Gem.bDropped = bDropped;
	Gem.From = From;
	Gem.To = To;
	Gem.LaunchAt = LaunchAt;
	Gem.Flight = Flight;
	Gem.Dropper = Dropper;
	Gems.MarkItemDirty(Gem);
}

bool AChaosImpactRubyField::PickFieldSpot(FVector& Out, const double Now)
{
	if (FieldPoints.IsEmpty())
	{
		return false;
	}
	// Away from the rubies lying already and from where anybody stands (it should be found, not appear underfoot).
	for (int32 Try = 0; Try < 16; ++Try)
	{
		const FVector Point = FieldPoints[ServerStream.RandRange(0, FieldPoints.Num() - 1)];
		bool bFree = true;
		for (const FChaosImpactRubyGem& Gem : Gems.Items)
		{
			if (!Gem.IsCollected() && FVector::DistSquared2D(Gem.To, Point) < FMath::Square(Try < 10 ? 330.0f : 200.0f))
			{
				bFree = false;
				break;
			}
		}
		for (TActorIterator<AChaosImpactCharacter> It(GetWorld()); bFree && It; ++It)
		{
			bFree = It->IsEliminated() || FVector::DistSquared2D(It->GetActorLocation(), Point) > FMath::Square(260.0f);
		}
		if (bFree)
		{
			Out = Point;
			return true;
		}
	}
	return false;
}

bool AChaosImpactRubyField::AddFieldCluster(const double LaunchAt, const float Drop, const float Flight)
{
	FVector Spot;
	if (!PickFieldSpot(Spot, LaunchAt))
	{
		return false;
	}
	// A little heap round the spot: a few stones, now and then a large one in the middle.
	int32 Lying = 0;
	for (const FChaosImpactRubyGem& Gem : Gems.Items)
	{
		Lying += !Gem.IsCollected() && !Gem.bDropped ? 1 : 0;
	}
	const int32 Count = FMath::Min(ServerStream.RandRange(ChaosImpactParty::FieldClusterMin, ChaosImpactParty::FieldClusterMax), FieldTarget - Lying);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const float Roll = ServerStream.FRand();
		const uint8 Value = Roll < ChaosImpactParty::FieldHugeChance ? ChaosImpactParty::HugeRubyValue
			: Roll < ChaosImpactParty::FieldHugeChance + ChaosImpactParty::FieldBigChance ? ChaosImpactParty::BigRubyValue : 1;
		const FVector At = Index == 0 ? Spot
			: ChaosImpactParty::FindLandSpot(GetWorld(), Stage.Get(), FieldPoints, Spot, 85.0f, 190.0f, ServerStream);
		AddGem(At + FVector(0, 0, Drop), At, LaunchAt + Index * 0.05, Flight, Value, false, nullptr);
	}
	return Count > 0;
}

void AChaosImpactRubyField::SetupField(AChaosImpactStageBase* InStage, const TArray<FVector>& Points, const int32 Target)
{
	if (!HasAuthority())
	{
		return;
	}
	Stage = InStage;
	FieldPoints = Points;
	FieldTarget = FMath::Min(Target, FieldPoints.Num());
	Gems.Items.Reset();
	Gems.MarkArrayDirty();
	Claims.Reset();
	const double Now = GetNow();
	// They rain down over the opening (a cluster after another), then lie there waiting for GO.
	for (int32 Wave = 0; CountLying() < FieldTarget && Wave < FieldTarget; ++Wave)
	{
		if (!AddFieldCluster(Now + 0.4 + Wave * 0.07, 900.0f, 0.65f))
		{
			break;
		}
	}
	NextRefillAt = Now + ChaosImpactParty::FieldRefillSeconds;
	ForceNetUpdate();
	UE_LOG(LogChaosImpact, Log, TEXT("Party: ruby field set up (%d spots, %d rubies)"), FieldPoints.Num(), Gems.Items.Num());
}

void AChaosImpactRubyField::Scatter(const FVector& Origin, const int32 Amount, APawn* Dropper)
{
	if (!HasAuthority() || Amount <= 0 || !GetWorld())
	{
		return;
	}
	// At most MaxGemsPerDrop gems: past that, larger ones.
	int32 Ones = 0, Big = 0, Huge = 0;
	ChaosImpactParty::SplitDrop(Amount, Ones, Big, Huge);
	const double Now = GetNow();
	const FVector From = Origin + FVector(0, 0, 40);
	for (int32 Index = 0; Index < Ones + Big + Huge; ++Index)
	{
		const uint8 Value = Index < Huge ? ChaosImpactParty::HugeRubyValue : Index < Huge + Big ? ChaosImpactParty::BigRubyValue : 1;
		const FVector Land = ChaosImpactParty::FindLandSpot(GetWorld(), Stage.Get(), FieldPoints, Origin, 170.0f, 560.0f, ServerStream);
		const float Flight = FMath::Clamp(static_cast<float>(FVector::Dist2D(From, Land)) / 900.0f, 0.42f, 0.8f) + ServerStream.FRandRange(0.0f, 0.1f);
		AddGem(From, Land, Now + Index * 0.012, Flight, Value, true, Dropper);
	}
	ForceNetUpdate();
}

void AChaosImpactRubyField::Claim(AChaosImpactCharacter* Character, const int32 Id, const double TouchTime)
{
	if (!HasAuthority())
	{
		return;
	}
	const double Now = GetNow();
	FChaosImpactRubyGem* Gem = FindGem(Id);
	// Generous reach: a guest's copy here trails their own screen by up to a round trip.
	const float Reach = Character ? FMath::Min(ChaosImpactParty::RubyReach + 230.0f
		+ static_cast<float>(Character->GetVelocity().Size2D()) * (Character->GetNetworkRoundTripSeconds() + 0.1f), 720.0f) : 0.0f;
	const bool bOk = Gem && PRInMatch(GetWorld()) && PRCanPickUp(Character) && !Gem->IsCollected() && Now >= Gem->GetLandedAt() - 0.25
		&& CanTake(*Gem, Character, TouchTime) && FVector::Dist2D(Character->GetActorLocation(), Gem->To) <= Reach;
	if (!bOk)
	{
		PRAnswer(this, Character, Id, false);
		return;
	}
	FClaim& Entry = Claims.FindOrAdd(Id);
	if (Entry.Claimers.IsEmpty() || TouchTime < Entry.BestTouch)
	{
		Entry.Best = Character;
		Entry.BestTouch = TouchTime;
	}
	if (Entry.Claimers.IsEmpty())
	{
		Entry.FirstAt = Now;
	}
	Entry.Claimers.AddUnique(Character);
	SettleClaims(Now, false);
}

void AChaosImpactRubyField::SettleClaims(const double Now, const bool bForce)
{
	// Settled once nobody can still be on their way with an earlier touch (at once with no guests in the room).
	const double Window = PRHasGuests(GetWorld()) ? AChaosImpactBall::FairTouchReachSeconds : 0.0;
	TArray<int32> Due;
	for (const TPair<int32, FClaim>& Pair : Claims)
	{
		if (bForce || Now >= Pair.Value.FirstAt + Window)
		{
			Due.Add(Pair.Key);
		}
	}
	for (const int32 Id : Due)
	{
		FClaim Entry;
		if (!Claims.RemoveAndCopyValue(Id, Entry))
		{
			continue;
		}
		FChaosImpactRubyGem* Gem = FindGem(Id);
		AChaosImpactCharacter* Winner = Entry.Best.Get();
		const bool bAward = Gem && !Gem->IsCollected() && PRCanPickUp(Winner) && PRInMatch(GetWorld());
		if (bAward)
		{
			Gem->Collector = Winner;
			Gem->CollectedAt = Now;
			Gems.MarkItemDirty(*Gem);
			UE_LOG(LogChaosImpact, Verbose, TEXT("Party: ruby %d (%s, %.2f s after it set off) to %s"), Id, Gem->bDropped ? TEXT("dropped") : TEXT("field"),
				Now - Gem->LaunchAt, *Winner->GetName());
			if (AChaosImpactPlayerState* Member = Winner->GetPlayerState<AChaosImpactPlayerState>())
			{
				Member->Points += Gem->Value;
				Member->ForceNetUpdate();
			}
			ForceNetUpdate();
		}
		for (const TWeakObjectPtr<AChaosImpactCharacter>& Claimer : Entry.Claimers)
		{
			PRAnswer(this, Claimer.Get(), Id, bAward && Claimer.Get() == Winner);
		}
	}
}

void AChaosImpactRubyField::ServerTick(const double Now)
{
	SettleClaims(Now, false);
	// Taken rubies go once everyone has heard; knocked-loose ones fade away in time.
	bool bChanged = false;
	for (int32 Index = Gems.Items.Num() - 1; Index >= 0; --Index)
	{
		const FChaosImpactRubyGem& Gem = Gems.Items[Index];
		const bool bGone = Gem.IsCollected() ? Now - Gem.CollectedAt > PRCollectedKeepSeconds
			: Gem.bDropped && Now > Gem.GetLandedAt() + ChaosImpactParty::DroppedRubyLifeSeconds && !Claims.Contains(Gem.Id);
		if (bGone)
		{
			Gems.Items.RemoveAt(Index);
			bChanged = true;
		}
	}
	if (bChanged)
	{
		Gems.MarkArrayDirty();
	}
	// The stage's own rubies come back one by one.
	int32 Lying = 0;
	for (const FChaosImpactRubyGem& Gem : Gems.Items)
	{
		Lying += !Gem.IsCollected() && !Gem.bDropped ? 1 : 0;
	}
	if (Lying < FieldTarget && Now >= NextRefillAt && PRInMatch(GetWorld()))
	{
		NextRefillAt = Now + ChaosImpactParty::FieldRefillSeconds;
		bChanged |= AddFieldCluster(Now, 520.0f, 0.55f);
	}
	if (bChanged)
	{
		ForceNetUpdate();
	}
}

void AChaosImpactRubyField::DetectLocalPickups(const double Now)
{
	UWorld* World = GetWorld();
	if (!World || !PRInMatch(World))
	{
		return;
	}
	const double Sight = HasAuthority() ? AChaosImpactBall::GetFairSightDelay(World) : 0.0;
	TArray<AChaosImpactCharacter*, TInlineAllocator<16>> Pickers;
	for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
	{
		AChaosImpactCharacter* Character = *It;
		const bool bOwn = PRIsOwnPlayer(Character);
		const bool bCPU = HasAuthority() && Character && !Character->IsPlayerControlled() && Character->GetController();
		if ((bOwn || bCPU) && PRCanPickUp(Character))
		{
			Pickers.Add(Character);
		}
	}
	// (Claims may settle at once, but no ruby is added or removed while the list is gone through.)
	for (AChaosImpactCharacter* Character : Pickers)
	{
		const bool bCPU = !Character->IsPlayerControlled();
		const FVector Body = Character->GetActorLocation();
		const float HalfHeight = Character->GetCapsuleComponent() ? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.0f;
		const float Feet = static_cast<float>(Body.Z) - HalfHeight;
		for (int32 Index = 0; Index < Gems.Items.Num(); ++Index)
		{
			const FChaosImpactRubyGem& Gem = Gems.Items[Index];
			if (!IsAvailable(Gem, Now) || !CanTake(Gem, Character, Now) || LocalClaims.Contains(Gem.Id)
				|| FVector::DistSquared2D(Body, Gem.To) > FMath::Square(ChaosImpactParty::RubyReach + PRGemRadius(Gem.Value) - 19.5f)
				|| Feet < Gem.To.Z - 60.0f || Feet > Gem.To.Z + ChaosImpactParty::RubyReachHeight)
			{
				continue;
			}
			// The host and the CPUs see a new ruby before the guests can: they wait that long for it.
			if (HasAuthority() && Now < Gem.GetLandedAt() + Sight)
			{
				continue;
			}
			const int32 Id = Gem.Id;
			if (bCPU)
			{
				const FClaim* Pending = Claims.Find(Id);
				if (!Pending || !Pending->Claimers.Contains(Character))
				{
					Claim(Character, Id, Now);
				}
				continue;
			}
			// This machine's own player: in hand at once (the count shows it), the host told.
			const AChaosImpactPlayerState* Member = Character->GetPlayerState<AChaosImpactPlayerState>();
			FLocalClaim Local;
			Local.Pawn = Character;
			Local.Value = Gem.Value;
			Local.At = Now;
			Local.PointsAtClaim = Member ? Member->Points : 0;
			Local.Threshold = Local.PointsAtClaim + GetPendingValue(Character) + Gem.Value;
			FFlyIn Fly;
			FTransform Where;
			float Flash = 0.0f;
			Fly.From = GetGemTransform(Gem, Now, Where, Flash) ? Where.GetLocation() : FVector(Gem.To) + FVector(0, 0, PRGemHover(Gem.Value));
			Fly.To = Character;
			Fly.Start = Now;
			Fly.Value = Gem.Value;
			Fly.bOwn = true;
			LocalClaims.Add(Id, Local);
			Shown.FindOrAdd(Id).bCollected = true;
			FlyIns.Add(Fly);
			if (HasAuthority())
			{
				Claim(Character, Id, Now);
			}
			else
			{
				Character->ClaimPartyItem(Id);
			}
		}
	}
}

void AChaosImpactRubyField::OnLocalClaimResolved(const AChaosImpactCharacter* Character, const int32 Id, const bool bAccepted)
{
	FLocalClaim* Local = LocalClaims.Find(Id);
	if (!Local)
	{
		return;
	}
	if (!bAccepted)
	{
		// Someone touched it first: it comes back on this screen if it is still lying there (gone otherwise).
		LocalClaims.Remove(Id);
		const FChaosImpactRubyGem* Gem = FindGem(Id);
		if (FShown* Seen = Shown.Find(Id); Seen && Gem && !Gem->IsCollected())
		{
			Seen->bCollected = false;
		}
		return;
	}
	Local->bResolved = true;
	Local->ResolvedAt = GetNow();
}

int32 AChaosImpactRubyField::GetPendingValue(const APawn* Pawn) const
{
	if (!Pawn)
	{
		return 0;
	}
	const AChaosImpactPlayerState* Member = Pawn->GetPlayerState<AChaosImpactPlayerState>();
	const int32 Points = Member ? Member->Points : 0;
	int32 Pending = 0;
	for (const TPair<int32, FLocalClaim>& Pair : LocalClaims)
	{
		const FLocalClaim& Local = Pair.Value;
		// Counted on top until the host's number has it (and never after a drop took the count below where it was).
		if (Local.Pawn.Get() == Pawn && Points < Local.Threshold && Points >= Local.PointsAtClaim)
		{
			Pending += Local.Value;
		}
	}
	return Pending;
}

double AChaosImpactRubyField::GetCountGainedAt(const APlayerState* Member) const
{
	const FSeenCount* Seen = SeenCounts.Find(Member);
	return Seen ? Seen->GainedAt : -100.0;
}

const FChaosImpactRubyGem* AChaosImpactRubyField::FindNearestAvailable(const FVector& Location, const float MaxDistance, const APawn* For) const
{
	const double Now = GetNow();
	const FChaosImpactRubyGem* Best = nullptr;
	float BestScore = MaxDistance;
	for (const FChaosImpactRubyGem& Gem : Gems.Items)
	{
		if (!IsAvailable(Gem, Now + 0.4) || !CanTake(Gem, For, Now))
		{
			continue;
		}
		// A large ruby is worth going further for.
		const float Score = static_cast<float>(FVector::Dist(Location, Gem.To)) / FMath::Sqrt(static_cast<float>(Gem.Value));
		if (Score < BestScore && FMath::Abs(Gem.To.Z - Location.Z) < 320.0f)
		{
			BestScore = Score;
			Best = &Gem;
		}
	}
	return Best;
}

bool AChaosImpactRubyField::GetGemTransform(const FChaosImpactRubyGem& Gem, const double Now, FTransform& Out, float& OutFlash) const
{
	const double Age = Now - Gem.LaunchAt;
	if (Age < 0.0)
	{
		return false;
	}
	const float Phase = FMath::Frac(Gem.Id * 0.618034f);
	const float Radius = PRGemRadius(Gem.Value);
	const float T = static_cast<float>(FMath::Fmod(Now, 10000.0));
	const FVector Rest = FVector(Gem.To) + FVector(0, 0, PRGemHover(Gem.Value) + 5.0f * FMath::Sin(T * 2.3f + Phase * UE_TWO_PI));
	FVector At = Rest;
	float Scale = Radius;
	float Spin = 75.0f;
	OutFlash = 0.0f;
	if (Age < Gem.Flight)
	{
		const float U = static_cast<float>(Age / FMath::Max(Gem.Flight, 0.01f));
		if (Gem.bDropped)
		{
			// Knocked loose: an arc out of the player to the floor.
			const float Height = 170.0f + 90.0f * Phase;
			At = FMath::Lerp(FVector(Gem.From), Rest, U) + FVector(0, 0, 4.0f * Height * U * (1.0f - U));
			Scale = Radius * FMath::Lerp(0.65f, 1.0f, U);
		}
		else
		{
			// One of the stage's own: falls in from above, slowing as it comes down.
			At = FMath::Lerp(FVector(Gem.From), Rest, ChaosImpactEase::Out(U));
			Scale = Radius * FMath::Lerp(0.35f, 1.0f, ChaosImpactEase::Out(U));
		}
		Spin = 520.0f;
		OutFlash = 0.35f;
	}
	else
	{
		const float Since = static_cast<float>(Age - Gem.Flight);
		// A flash as it settles.
		OutFlash = Since < 0.4f ? 1.0f - Since / 0.4f : 0.0f;
		if (Gem.bDropped)
		{
			// Its last seconds: it blinks, faster towards the end, and shrinks away at the very end.
			const float Left = ChaosImpactParty::DroppedRubyLifeSeconds - Since;
			if (Left <= 0.0f)
			{
				return false;
			}
			if (Left < ChaosImpactParty::DroppedRubyBlinkSeconds)
			{
				const float Rate = FMath::Lerp(9.0f, 3.0f, Left / ChaosImpactParty::DroppedRubyBlinkSeconds);
				if (FMath::Frac(T * Rate * 0.5f) < 0.35f)
				{
					return false;
				}
				Scale *= FMath::Clamp(Left / 0.35f, 0.0f, 1.0f);
			}
		}
	}
	const float Yaw = FMath::Fmod(T * Spin + Phase * 360.0f, 360.0f);
	const float Tilt = 10.0f * FMath::Sin(T * 1.7f + Phase * 9.0f);
	Out = FTransform(FRotator(Tilt, Yaw, 0.0f), At, FVector(Scale, Scale, Scale * 1.18f));
	return Scale > 0.5f;
}

AChaosImpactFxKit* AChaosImpactRubyField::GetSparkles()
{
	if (!Sparkles.IsValid())
	{
		Sparkles = AChaosImpactFxKit::Spawn(this, FVector::ZeroVector);
	}
	return Sparkles.Get();
}

void AChaosImpactRubyField::PlaySparkle(const FVector& At, const float Size)
{
	AChaosImpactFxKit* Fx = GetSparkles();
	if (!Fx)
	{
		return;
	}
	// A little star: a white point of light with a cross of rays, there and gone.
	const FLinearColor Pink(1.0f, 0.42f, 0.62f);
	FChaosImpactFxPiece Point = PRPiece(EChaosImpactFxLook::Glow, At, 0.32f, Size * 1.6f, Size * 0.3f, Pink, FLinearColor::White, 7.0f);
	Point.Attack = 0.25f;
	Fx->Add(Point);
	for (const FVector& Along : {FVector::UpVector, PRCameraRight(GetWorld())})
	{
		FChaosImpactFxPiece Ray = PRPiece(EChaosImpactFxLook::Streak, At, 0.3f, Size * 3.2f, Size * 0.4f, Pink, FLinearColor::White, 6.0f);
		Ray.Velocity = Along;
		Ray.Width = FMath::Max(2.0f, Size * 0.16f);
		Ray.Attack = 0.25f;
		Fx->Add(Ray);
	}
}

void AChaosImpactRubyField::PlayLandEffect(const FVector& At, const uint8 Value)
{
	AChaosImpactFxKit* Fx = GetSparkles();
	if (!Fx)
	{
		return;
	}
	const float K = PRGemGrandeur(Value);
	Fx->Add(PRPiece(EChaosImpactFxLook::GroundGlow, At + FVector(0, 0, 3), 0.42f, 40.0f * K, 120.0f * K,
		FLinearColor(1.0f, 0.12f, 0.32f), FLinearColor(1.0f, 0.7f, 0.8f), 2.2f));
	for (int32 Index = 0; Index < 3; ++Index)
	{
		FChaosImpactFxPiece Spark = PRPiece(EChaosImpactFxLook::Streak, At + FVector(0, 0, 10), 0.3f, 22.0f * K, 6.0f,
			FLinearColor(1.0f, 0.3f, 0.5f), FLinearColor::White, 4.0f);
		Spark.Velocity = PRUpward(VisualStream, 0.55f) * VisualStream.FRandRange(220.0f, 360.0f);
		Spark.Gravity = 900.0f;
		Spark.Width = 2.5f;
		Fx->Add(Spark);
	}
}

void AChaosImpactRubyField::PlayPickupEffect(const FVector& At, const uint8 Value, const APawn* Collector, const bool bOwn)
{
	AChaosImpactFxKit* Fx = AChaosImpactFxKit::Spawn(this, At);
	const bool bBig = Value > 1;
	const bool bHuge = Value >= ChaosImpactParty::HugeRubyValue;
	const float K = PRGemGrandeur(Value);
	const FLinearColor Ruby(1.0f, 0.1f, 0.3f), Pink(1.0f, 0.45f, 0.65f), Hot(1.0f, 0.92f, 0.95f), GoldTint(1.0f, 0.78f, 0.3f);
	if (Fx)
	{
		Fx->Stream.Initialize(VisualStream.RandHelper(1 << 30));
		// A white-hot flash where it went in, a ring breaking outward, sparks thrown up and out, a few motes rising after.
		FChaosImpactFxPiece Flash = PRPiece(EChaosImpactFxLook::Glow, FVector::ZeroVector, 0.22f, 40.0f * K, 95.0f * K, Ruby, Hot, 5.0f);
		Flash.Attack = 0.05f;
		Fx->Add(Flash);
		FChaosImpactFxPiece Ring = PRPiece(EChaosImpactFxLook::AirRing, FVector::ZeroVector, 0.34f, 50.0f * K, 220.0f * K, Pink, Hot, 4.0f);
		Ring.RingFrom = 0.3f;
		Ring.RingTo = 0.95f;
		Ring.RingWidth = 0.07f;
		Fx->Add(Ring);
		const int32 Sparks = bBig ? 10 : 7;
		for (int32 Index = 0; Index < Sparks; ++Index)
		{
			FChaosImpactFxPiece Spark = PRPiece(EChaosImpactFxLook::Streak, FVector::ZeroVector, Fx->Stream.FRandRange(0.3f, 0.48f),
				Fx->Stream.FRandRange(16.0f, 26.0f) * K, 6.0f, Index % 3 == 0 ? Hot : bBig && Index % 3 == 1 ? GoldTint : Ruby, Hot, 4.5f);
			Spark.Velocity = PRUpward(Fx->Stream, 0.35f) * Fx->Stream.FRandRange(260.0f, 480.0f) * K;
			Spark.Gravity = 1150.0f;
			Spark.Drag = 1.8f;
			Spark.Width = 2.5f;
			Fx->Add(Spark);
		}
		for (int32 Index = 0; Index < (bBig ? 6 : 4); ++Index)
		{
			FChaosImpactFxPiece Mote = PRPiece(EChaosImpactFxLook::Glow, PRUpward(Fx->Stream, 0.0f) * FVector(30.0f, 30.0f, 0.0f),
				Fx->Stream.FRandRange(0.55f, 0.8f), 20.0f * K, 6.0f, Pink, Hot, 3.5f);
			Mote.Velocity = FVector(0, 0, Fx->Stream.FRandRange(110.0f, 190.0f));
			Mote.Gravity = -60.0f;
			Mote.Delay = Fx->Stream.FRandRange(0.02f, 0.12f);
			Mote.Flicker = 0.5f;
			Fx->Add(Mote);
		}
		if (bBig)
		{
			FChaosImpactFxPiece Second = Ring;
			Second.Delay = 0.07f;
			Second.Color = GoldTint;
			Second.SizeTo = 300.0f * K / 1.45f;
			Fx->Add(Second);
		}
		if (bHuge)
		{
			// A huge one: a golden column of light and a shower of sparks besides.
			FChaosImpactFxPiece Column = PRPiece(EChaosImpactFxLook::Glow, FVector(0, 0, 60), 0.5f, 120.0f, 60.0f, GoldTint, Hot, 4.0f);
			Column.Width = 3.0f;
			Column.Attack = 0.08f;
			Fx->Add(Column);
			for (int32 Index = 0; Index < 12; ++Index)
			{
				FChaosImpactFxPiece Spark = PRPiece(EChaosImpactFxLook::Streak, FVector::ZeroVector, Fx->Stream.FRandRange(0.5f, 0.8f),
					Fx->Stream.FRandRange(40.0f, 70.0f), 10.0f, Index % 2 ? GoldTint : Hot, Hot, 5.0f);
				Spark.Velocity = PRUpward(Fx->Stream, 0.5f) * Fx->Stream.FRandRange(600.0f, 1000.0f);
				Spark.Gravity = 1300.0f;
				Spark.Width = 4.0f;
				Spark.Delay = 0.05f;
				Fx->Add(Spark);
			}
		}
	}
	// A chime, climbing with each ruby picked up in quick succession (this machine's own player's only).
	const double Now = GetNow();
	if (bOwn)
	{
		PickupStreak = Now - PickupStreakAt < 1.4 ? PickupStreak + 1 : 0;
		PickupStreakAt = Now;
		const float Pitch = 1.0f + 0.06f * FMath::Min(PickupStreak, 10);
		ChaosImpactSfx::Play2D(this, EChaosImpactSfx::MiniScore, bBig ? 0.75f : 0.55f, Pitch);
		if (bBig)
		{
			ChaosImpactSfx::Play2D(this, EChaosImpactSfx::MiniBig, bHuge ? 0.8f : 0.5f, bHuge ? 0.95f : 1.1f);
		}
	}
	else
	{
		ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::MiniScore, At, bBig ? 0.45f : 0.3f, 1.15f);
	}
}

void AChaosImpactRubyField::UpdateVisuals(const double Now, const float DeltaSeconds)
{
	if (!GemInstances || !GlowInstances)
	{
		return;
	}
	const TArray<FVector, TInlineAllocator<4>> Cameras = PRCameras(GetWorld());
	TArray<FTransform> GemTransforms;
	TArray<FTransform> GlowTransforms;
	TArray<float> Flashes;
	GemTransforms.Reserve(Gems.Items.Num() + FlyIns.Num());
	TSet<int32> Present;
	bool bLandSound = false;
	for (const FChaosImpactRubyGem& Gem : Gems.Items)
	{
		Present.Add(Gem.Id);
		FShown& Seen = Shown.FindOrAdd(Gem.Id);
		if (Gem.IsCollected())
		{
			// Taken by someone (not on this screen): it flies into them.
			if (!Seen.bCollected)
			{
				Seen.bCollected = true;
				FFlyIn Fly;
				Fly.From = Seen.Last.IsZero() ? FVector(Gem.To) + FVector(0, 0, PRGemHover(Gem.Value)) : Seen.Last;
				Fly.To = Gem.Collector;
				Fly.Start = Now;
				Fly.Value = Gem.Value;
				Fly.bOwn = PRIsOwnPlayer(Gem.Collector);
				FlyIns.Add(Fly);
			}
			continue;
		}
		if (Seen.bCollected)
		{
			continue;
		}
		FTransform Where;
		float Flash = 0.0f;
		if (!GetGemTransform(Gem, Now, Where, Flash))
		{
			continue;
		}
		Seen.Last = Where.GetLocation();
		if (!Seen.bLanded && Now >= Gem.GetLandedAt())
		{
			Seen.bLanded = true;
			if (Now - Gem.GetLandedAt() < 0.3 && PRNearestCamera(Cameras, Gem.To) < 4200.0f)
			{
				PlayLandEffect(Gem.To, Gem.Value);
				bLandSound |= Gem.bDropped;
			}
		}
		GemTransforms.Add(Where);
		const float Glow = Where.GetScale3D().X * PRGlowScale / 50.0f * (1.0f + 0.25f * Flash);
		GlowTransforms.Add(FTransform(FQuat::Identity, Where.GetLocation(), FVector(Glow)));
		Flashes.Add(Flash + (Gem.Value >= ChaosImpactParty::HugeRubyValue ? 0.8f : Gem.Value > 1 ? 0.35f : 0.0f));
	}
	if (bLandSound && !Cameras.IsEmpty())
	{
		ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::MiniTap, Cameras[0], 0.14f, 1.7f);
	}
	// Gone without being taken here (expired, or the news of who took it never came): a last glint.
	for (auto It = Shown.CreateIterator(); It; ++It)
	{
		if (!Present.Contains(It->Key))
		{
			if (!It->Value.bCollected && !It->Value.Last.IsZero() && PRNearestCamera(Cameras, It->Value.Last) < 4200.0f)
			{
				PlaySparkle(It->Value.Last, 16.0f);
			}
			It.RemoveCurrent();
		}
	}
	// The ones flying into whoever took them.
	for (int32 Index = FlyIns.Num() - 1; Index >= 0; --Index)
	{
		const FFlyIn Fly = FlyIns[Index];
		const APawn* Pawn = Fly.To.Get();
		const float U = static_cast<float>((Now - Fly.Start) / PRFlyInSeconds);
		const FVector Target = Pawn ? Pawn->GetActorLocation() + FVector(0, 0, 25) : Fly.From;
		if (U >= 1.0f || !Pawn)
		{
			FlyIns.RemoveAt(Index);
			if (Pawn)
			{
				PlayPickupEffect(Target, Fly.Value, Pawn, Fly.bOwn);
			}
			continue;
		}
		const FVector At = FMath::Lerp(Fly.From, Target, ChaosImpactEase::In(U)) + FVector(0, 0, 70.0f * FMath::Sin(U * PI));
		const float Scale = PRGemRadius(Fly.Value) * FMath::Lerp(1.15f, 0.45f, U);
		GemTransforms.Add(FTransform(FRotator(0.0f, FMath::Fmod(static_cast<float>(FMath::Fmod(Now, 1000.0)) * 900.0f, 360.0f), 0.0f), At,
			FVector(Scale, Scale, Scale * 1.18f)));
		GlowTransforms.Add(FTransform(FQuat::Identity, At, FVector(Scale * PRGlowScale * 1.3f / 50.0f)));
		Flashes.Add(1.0f);
	}
	// The instances: as many as are drawn, all moved together.
	const auto Apply = [](UInstancedStaticMeshComponent* Instances, const TArray<FTransform>& Transforms, const TArray<float>& Values)
	{
		if (Instances->GetInstanceCount() != Transforms.Num())
		{
			Instances->ClearInstances();
			if (!Transforms.IsEmpty())
			{
				Instances->AddInstances(Transforms, false, true, false);
			}
		}
		else if (!Transforms.IsEmpty())
		{
			Instances->BatchUpdateInstancesTransforms(0, Transforms, true, false, true);
		}
		for (int32 Index = 0; Index < Values.Num() && Index < Instances->GetInstanceCount(); ++Index)
		{
			Instances->SetCustomDataValue(Index, 0, Values[Index], false);
		}
		Instances->MarkRenderStateDirty();
	};
	Apply(GemInstances, GemTransforms, Flashes);
	Apply(GlowInstances, GlowTransforms, Flashes);
	// Glints on rubies near the players' cameras now and then.
	if (Now >= NextSparkleAt && !GemTransforms.IsEmpty())
	{
		NextSparkleAt = Now + VisualStream.FRandRange(0.07f, 0.16f);
		const FTransform& Pick = GemTransforms[VisualStream.RandRange(0, GemTransforms.Num() - 1)];
		if (PRNearestCamera(Cameras, Pick.GetLocation()) < 2600.0f)
		{
			const float Radius = Pick.GetScale3D().X;
			PlaySparkle(Pick.GetLocation() + PRUpward(VisualStream, 0.2f) * Radius * 0.8f, Radius * 0.9f);
		}
	}
}

void AChaosImpactRubyField::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Now = GetNow();
	if (HasAuthority())
	{
		ServerTick(Now);
	}
	DetectLocalPickups(Now);
	// This machine's pickups the host has counted (or that are long answered) are done with.
	for (auto It = LocalClaims.CreateIterator(); It; ++It)
	{
		const FLocalClaim& Local = It->Value;
		const APawn* Pawn = Local.Pawn.Get();
		const AChaosImpactPlayerState* Member = Pawn ? Pawn->GetPlayerState<AChaosImpactPlayerState>() : nullptr;
		const int32 Points = Member ? Member->Points : 0;
		if (!Pawn || (Local.bResolved && (Points >= Local.Threshold || Points < Local.PointsAtClaim || Now - Local.ResolvedAt > 2.0))
			|| Now - Local.At > 6.0)
		{
			It.RemoveCurrent();
		}
	}
	// Everyone's shown count: when it last went up (the HUD lights it).
	if (const AChaosImpactGameState* Match = PRMatch(GetWorld()))
	{
		for (const APlayerState* Member : Match->PlayerArray)
		{
			const int32 Count = ChaosImpactParty::GetShownCount(GetWorld(), Member);
			FSeenCount& Seen = SeenCounts.FindOrAdd(Member);
			if (Count > Seen.Count)
			{
				Seen.GainedAt = Now;
			}
			Seen.Count = Count;
		}
	}
	if (GetNetMode() != NM_DedicatedServer)
	{
		UpdateVisuals(Now, DeltaSeconds);
	}
}

// =====================================================================================================================
// クラウンキープ: the crown
// =====================================================================================================================

namespace
{
	/** Worn: this far over the top of its holder's capsule, at this size. On the floor: floating this high, larger. */
	constexpr float PRCrownWornLift = 30.0f;
	constexpr float PRCrownWornScale = 0.78f;
	constexpr float PRCrownFloorLift = 88.0f;
	constexpr float PRCrownFloorScale = 1.3f;
	constexpr float PRCrownHookSpeed = 2300.0f;

	void PRAddTri(ChaosImpactIceMeshes::FMeshBuffers& Mesh, const FVector& A, const FVector& B, const FVector& C, const FVector& Outward)
	{
		FVector Normal = FVector::CrossProduct(C - A, B - A).GetSafeNormal();
		const bool bFlip = FVector::DotProduct(Normal, Outward) < 0.0f;
		const FVector& Second = bFlip ? C : B;
		const FVector& Third = bFlip ? B : C;
		Normal = bFlip ? -Normal : Normal;
		const int32 Base = Mesh.Vertices.Num();
		for (const FVector* Point : {&A, &Second, &Third})
		{
			Mesh.Vertices.Add(*Point);
			Mesh.Normals.Add(Normal);
			Mesh.UVs.Add(FVector2D(Point->X * 0.02f, Point->Z * 0.02f));
		}
		Mesh.Triangles.Append({Base, Base + 1, Base + 2});
	}

	void PRAddQuad(ChaosImpactIceMeshes::FMeshBuffers& Mesh, const FVector& A, const FVector& B, const FVector& C, const FVector& D, const FVector& Outward)
	{
		PRAddTri(Mesh, A, B, C, Outward);
		PRAddTri(Mesh, A, C, D, Outward);
	}

	/** A point on a ring of the crown: Angle round, Radius out, Z up. */
	FVector PRRing(const float Angle, const float Radius, const float Z)
	{
		return FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, Z);
	}
}

AChaosImpactCrown::AChaosImpactCrown()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(30.0f);
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

void AChaosImpactCrown::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AChaosImpactCrown, State);
}

AChaosImpactCrown* AChaosImpactCrown::Get(const UWorld* World)
{
	if (const AChaosImpactGameState* Match = PRMatch(World); Match && IsValid(Match->PartyCrown))
	{
		return Match->PartyCrown;
	}
	if (World)
	{
		for (TActorIterator<AChaosImpactCrown> It(const_cast<UWorld*>(World)); It; ++It)
		{
			if (!It->IsActorBeingDestroyed())
			{
				return *It;
			}
		}
	}
	return nullptr;
}

void AChaosImpactCrown::BeginPlay()
{
	Super::BeginPlay();
	ShownLocation = GetActorLocation();
	if (GetNetMode() != NM_DedicatedServer)
	{
		BuildModel();
	}
}

void AChaosImpactCrown::BuildModel()
{
	Model = NewObject<USceneComponent>(this, TEXT("CrownModel"));
	Model->SetupAttachment(Root);
	Model->SetUsingAbsoluteLocation(true);
	Model->SetUsingAbsoluteRotation(true);
	Model->SetUsingAbsoluteScale(true);
	Model->RegisterComponent();
	GoldMaterial = ChaosImpactParty::MakeGoldMaterial(this, 0.22f);
	// The band (flaring a little towards the top), and round its top edge five tall points with five low ones between
	// them, each point a thin curved blade of gold.
	ChaosImpactIceMeshes::FMeshBuffers GoldMesh;
	constexpr int32 Segments = 40;
	constexpr float BottomRadius = 26.0f;
	constexpr float TopRadius = 28.5f;
	constexpr float BandHeight = 14.0f;
	constexpr float Thickness = 2.4f;
	for (int32 Index = 0; Index < Segments; ++Index)
	{
		const float A0 = UE_TWO_PI * Index / Segments;
		const float A1 = UE_TWO_PI * (Index + 1) / Segments;
		const FVector Out0 = PRRing(A0, 1.0f, 0.0f);
		const FVector Out1 = PRRing(A1, 1.0f, 0.0f);
		const FVector Mid = (Out0 + Out1).GetSafeNormal();
		// Outside wall, inside wall, the top lip and the bottom lip (smooth enough at forty sides for a soft sheen).
		PRAddQuad(GoldMesh, PRRing(A0, BottomRadius, 0.0f), PRRing(A1, BottomRadius, 0.0f), PRRing(A1, TopRadius, BandHeight), PRRing(A0, TopRadius, BandHeight), Mid);
		PRAddQuad(GoldMesh, PRRing(A0, BottomRadius - Thickness, 0.0f), PRRing(A1, BottomRadius - Thickness, 0.0f),
			PRRing(A1, TopRadius - Thickness, BandHeight), PRRing(A0, TopRadius - Thickness, BandHeight), -Mid);
		PRAddQuad(GoldMesh, PRRing(A0, TopRadius, BandHeight), PRRing(A1, TopRadius, BandHeight), PRRing(A1, TopRadius - Thickness, BandHeight),
			PRRing(A0, TopRadius - Thickness, BandHeight), FVector::UpVector);
		PRAddQuad(GoldMesh, PRRing(A0, BottomRadius, 0.0f), PRRing(A1, BottomRadius, 0.0f), PRRing(A1, BottomRadius - Thickness, 0.0f),
			PRRing(A0, BottomRadius - Thickness, 0.0f), FVector::DownVector);
		// Two raised ridges round the band.
		for (const float Z : {2.0f, BandHeight - 2.5f})
		{
			const float R0 = FMath::Lerp(BottomRadius, TopRadius, Z / BandHeight) + 1.1f;
			PRAddQuad(GoldMesh, PRRing(A0, R0, Z - 1.1f), PRRing(A1, R0, Z - 1.1f), PRRing(A1, R0, Z + 1.1f), PRRing(A0, R0, Z + 1.1f), Mid);
		}
	}
	constexpr int32 Points = 5;
	TArray<FVector> TallTips;
	for (int32 Point = 0; Point < Points * 2; ++Point)
	{
		const bool bTall = Point % 2 == 0;
		const float Centre = UE_TWO_PI * Point / (Points * 2);
		const float HalfWidth = bTall ? 0.33f : 0.2f;
		const float TipZ = BandHeight + (bTall ? 24.0f : 10.0f);
		constexpr int32 Steps = 6;
		const FVector Tip = PRRing(Centre, TopRadius + (bTall ? 1.5f : 0.6f), TipZ);
		const FVector TipIn = PRRing(Centre, TopRadius + (bTall ? 1.5f : 0.6f) - Thickness, TipZ);
		if (bTall)
		{
			TallTips.Add(Tip + PRRing(Centre, 1.6f, 2.4f));
		}
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			const float A0 = Centre - HalfWidth + 2.0f * HalfWidth * Step / Steps;
			const float A1 = Centre - HalfWidth + 2.0f * HalfWidth * (Step + 1) / Steps;
			const FVector Outward = PRRing((A0 + A1) * 0.5f, 1.0f, 0.15f);
			PRAddTri(GoldMesh, PRRing(A0, TopRadius, BandHeight), PRRing(A1, TopRadius, BandHeight), Tip, Outward);
			PRAddTri(GoldMesh, PRRing(A0, TopRadius - Thickness, BandHeight), PRRing(A1, TopRadius - Thickness, BandHeight), TipIn, -Outward);
		}
		// The blade's two slanted edges.
		for (const float Side : {-1.0f, 1.0f})
		{
			const float A = Centre + Side * HalfWidth;
			PRAddQuad(GoldMesh, PRRing(A, TopRadius, BandHeight), Tip, TipIn, PRRing(A, TopRadius - Thickness, BandHeight),
				PRRing(Centre + Side * (HalfWidth + 0.4f), 1.0f, 0.4f));
		}
	}
	if (UProceduralMeshComponent* Body = ChaosImpactIceMeshes::CreateComponent(this, Model, GoldMesh, GoldMaterial))
	{
		Body->SetCastShadow(true);
	}
	// A gold ball on each tall point.
	UStaticMesh* Sphere = PRSphere();
	for (const FVector& Tip : TallTips)
	{
		UStaticMeshComponent* Ball = NewObject<UStaticMeshComponent>(this);
		Ball->SetupAttachment(Model);
		Ball->SetStaticMesh(Sphere);
		Ball->SetMaterial(0, GoldMaterial);
		Ball->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Ball->SetRelativeLocation(Tip);
		Ball->SetRelativeScale3D(FVector(0.075f));
		Ball->RegisterComponent();
	}
	// Stones round the band under the tall points (ruby, sapphire, emerald), and a ruby on top of the cap.
	const FLinearColor Bodies[] = {FLinearColor(0.42f, 0.0f, 0.045f), FLinearColor(0.0f, 0.06f, 0.4f), FLinearColor(0.0f, 0.26f, 0.08f)};
	const FLinearColor Fires[] = {FLinearColor(1.0f, 0.06f, 0.2f), FLinearColor(0.15f, 0.45f, 1.0f), FLinearColor(0.15f, 1.0f, 0.4f)};
	UStaticMesh* GemMesh = ChaosImpactParty::GetGemMesh();
	for (int32 Point = 0; Point < Points; ++Point)
	{
		const float Angle = UE_TWO_PI * Point / Points;
		const int32 Kind = Point == 0 ? 0 : Point % 2 == 1 ? 1 : 2;
		UStaticMeshComponent* Stone = NewObject<UStaticMeshComponent>(this);
		Stone->SetupAttachment(Model);
		Stone->SetStaticMesh(GemMesh);
		Stone->SetMaterial(0, ChaosImpactParty::MakeGemMaterial(this, Bodies[Kind], Fires[Kind], 1.4f));
		Stone->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Stone->SetRelativeLocation(PRRing(Angle, (BottomRadius + TopRadius) * 0.5f + 1.2f, BandHeight * 0.48f));
		Stone->SetRelativeRotation(FRotationMatrix::MakeFromZ(PRRing(Angle, 1.0f, 0.0f)).Rotator());
		Stone->SetRelativeScale3D(FVector(4.6f, 4.6f, 3.4f));
		Stone->RegisterComponent();
	}
	// The velvet cap inside, deep red, rising a little over the band.
	if (UMaterialInterface* Lit = PRLoad(PRLitPath))
	{
		UMaterialInstanceDynamic* Velvet = UMaterialInstanceDynamic::Create(Lit, this);
		Velvet->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.22f, 0.005f, 0.03f));
		Velvet->SetVectorParameterValue(TEXT("Base Color"), FLinearColor(0.22f, 0.005f, 0.03f));
		Velvet->SetScalarParameterValue(TEXT("Roughness"), 0.85f);
		UStaticMeshComponent* Cap = NewObject<UStaticMeshComponent>(this);
		Cap->SetupAttachment(Model);
		Cap->SetStaticMesh(Sphere);
		Cap->SetMaterial(0, Velvet);
		Cap->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Cap->SetRelativeLocation(FVector(0, 0, 9.0f));
		Cap->SetRelativeScale3D(FVector(0.5f, 0.5f, 0.36f));
		Cap->RegisterComponent();
		UStaticMeshComponent* TopStone = NewObject<UStaticMeshComponent>(this);
		TopStone->SetupAttachment(Model);
		TopStone->SetStaticMesh(GemMesh);
		TopStone->SetMaterial(0, ChaosImpactParty::MakeGemMaterial(this, Bodies[0], Fires[0], 1.6f));
		TopStone->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		TopStone->SetRelativeLocation(FVector(0, 0, 27.0f));
		TopStone->SetRelativeScale3D(FVector(5.0f, 5.0f, 6.0f));
		TopStone->RegisterComponent();
	}
	// A soft golden glow round it.
	HaloMaterial = ChaosImpactParty::MakeGemGlowMaterial(this, FLinearColor(1.0f, 0.55f, 0.08f), 0.22f);
	Halo = NewObject<UStaticMeshComponent>(this, TEXT("CrownHalo"));
	Halo->SetupAttachment(Model);
	Halo->SetStaticMesh(Sphere);
	Halo->SetMaterial(0, HaloMaterial);
	Halo->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Halo->SetCastShadow(false);
	Halo->SetRelativeLocation(FVector(0, 0, 16.0f));
	Halo->SetRelativeScale3D(FVector(0.95f));
	Halo->RegisterComponent();
}

double AChaosImpactCrown::GetNow() const
{
	return PRServerNow(GetWorld());
}

AChaosImpactCharacter* AChaosImpactCrown::GetHolder() const
{
	if (IsValid(State.Holder))
	{
		return State.Holder;
	}
	// This machine's own player took it a moment ago: theirs here until the host answers.
	if (const AChaosImpactCharacter* Taker = LocalTaker.Get(); Taker && GetNow() - LocalTakenAt < 1.5)
	{
		return const_cast<AChaosImpactCharacter*>(Taker);
	}
	return nullptr;
}

float AChaosImpactCrown::GetCountProgress() const
{
	if (!IsValid(State.Holder))
	{
		return 0.0f;
	}
	return FMath::Clamp(static_cast<float>((GetNow() - State.CountFrom) / ChaosImpactMatch::CrownCountSeconds), 0.0f, 1.0f);
}

bool AChaosImpactCrown::CanTake(const AChaosImpactCharacter* Character, const double Now) const
{
	if (bFrozen || !PRCanPickUp(Character) || IsValid(State.Holder) || Now < State.LaunchAt + State.Flight * 0.75)
	{
		return false;
	}
	return !(State.LastDropper && State.LastDropper == Character->GetPlayerState() && Now < State.DropBlockUntil);
}

bool AChaosImpactCrown::CanBeHooked(const APawn* By) const
{
	const AChaosImpactCharacter* Character = Cast<AChaosImpactCharacter>(By);
	if (bFrozen || !PRCanPickUp(Character) || !PRInMatch(GetWorld()))
	{
		return false;
	}
	if (IsValid(State.Holder))
	{
		return State.Holder != Character;
	}
	return CanTake(Character, GetNow());
}

void AChaosImpactCrown::PlaceAt(const FVector& Floor, const float Flight)
{
	if (!HasAuthority())
	{
		return;
	}
	const double Now = GetNow();
	State.Holder = nullptr;
	State.HolderState = nullptr;
	State.From = Floor + FVector(0, 0, 1400);
	State.To = Floor;
	State.LaunchAt = Now;
	State.Flight = Flight;
	State.LastDropper = nullptr;
	State.DropBlockUntil = 0.0;
	SetActorLocation(Floor);
	ForceNetUpdate();
}

void AChaosImpactCrown::Take(AChaosImpactCharacter* Character, const double Now)
{
	State.Holder = Character;
	State.HolderState = Character->GetPlayerState();
	State.TakenAt = Now;
	State.CountFrom = Now;
	State.LaunchAt = 0.0;
	State.Flight = 0.0f;
	ForceNetUpdate();
	UE_LOG(LogChaosImpact, Log, TEXT("Party: %s took the crown"), *GetNameSafe(State.HolderState));
}

void AChaosImpactCrown::Claim(AChaosImpactCharacter* Character, const double TouchTime)
{
	if (!HasAuthority())
	{
		return;
	}
	const double Now = GetNow();
	const float Reach = Character ? FMath::Min(ChaosImpactParty::CrownReach + 230.0f
		+ static_cast<float>(Character->GetVelocity().Size2D()) * (Character->GetNetworkRoundTripSeconds() + 0.1f), 720.0f) : 0.0f;
	if (!PRInMatch(GetWorld()) || !CanTake(Character, Now) || FVector::Dist2D(Character->GetActorLocation(), State.To) > Reach)
	{
		PRAnswer(this, Character, false);
		return;
	}
	if (Claimers.IsEmpty() || TouchTime < ClaimBestTouch)
	{
		ClaimBest = Character;
		ClaimBestTouch = TouchTime;
	}
	if (Claimers.IsEmpty())
	{
		ClaimFirstAt = Now;
	}
	Claimers.AddUnique(Character);
	SettleClaim(Now, false);
}

void AChaosImpactCrown::SettleClaim(const double Now, const bool bForce)
{
	const double Window = PRHasGuests(GetWorld()) ? AChaosImpactBall::FairTouchReachSeconds : 0.0;
	if (Claimers.IsEmpty() || (!bForce && Now < ClaimFirstAt + Window))
	{
		return;
	}
	AChaosImpactCharacter* Winner = ClaimBest.Get();
	const TArray<TWeakObjectPtr<AChaosImpactCharacter>> Answered = MoveTemp(Claimers);
	Claimers.Reset();
	ClaimBest.Reset();
	const bool bTake = Winner && CanTake(Winner, Now) && PRInMatch(GetWorld());
	if (bTake)
	{
		Take(Winner, Now);
	}
	for (const TWeakObjectPtr<AChaosImpactCharacter>& Claimer : Answered)
	{
		PRAnswer(this, Claimer.Get(), bTake && Claimer.Get() == Winner);
	}
}

void AChaosImpactCrown::DropFromHolder(const FVector& Land)
{
	if (!HasAuthority() || !State.HolderState)
	{
		return;
	}
	const double Now = GetNow();
	const AChaosImpactCharacter* Holder = State.Holder;
	const FVector From = IsValid(Holder) ? Holder->GetActorLocation() + FVector(0, 0, 120) : ShownLocation;
	State.LastDropper = State.HolderState;
	State.DropBlockUntil = Now + ChaosImpactParty::DropperBlockSeconds + 0.3;
	State.Holder = nullptr;
	State.HolderState = nullptr;
	State.From = From;
	State.To = Land;
	State.LaunchAt = Now;
	State.Flight = 0.75f;
	State.DroppedAt = Now;
	Claimers.Reset();
	ClaimBest.Reset();
	ForceNetUpdate();
	UE_LOG(LogChaosImpact, Log, TEXT("Party: the crown was knocked loose"));
}

void AChaosImpactCrown::TakeByHook(AChaosImpactCharacter* By, const FVector& From)
{
	if (!HasAuthority() || !CanBeHooked(By))
	{
		return;
	}
	const double Now = GetNow();
	if (State.HolderState)
	{
		// Torn off its holder's head: they cannot take it straight back either.
		State.LastDropper = State.HolderState;
		State.DropBlockUntil = Now + ChaosImpactParty::DropperBlockSeconds;
		State.DroppedAt = Now;
	}
	// Reeled in along the chain: its new holder's from the moment it is caught, counting once it reaches them.
	const float Flight = FMath::Clamp(static_cast<float>(FVector::Dist(From, By->GetActorLocation())) / PRCrownHookSpeed, 0.15f, 0.9f);
	Take(By, Now);
	State.From = From;
	State.LaunchAt = Now;
	State.Flight = Flight;
	State.CountFrom = Now + Flight;
	Claimers.Reset();
	ClaimBest.Reset();
	ForceNetUpdate();
	UE_LOG(LogChaosImpact, Log, TEXT("Party: %s hooked the crown"), *GetNameSafe(By));
}

void AChaosImpactCrown::StartNextCount(const double From)
{
	State.CountFrom = From;
	ForceNetUpdate();
}

void AChaosImpactCrown::OnLocalClaimResolved(const AChaosImpactCharacter* Character, const bool bAccepted)
{
	if (LocalTaker.Get() == Character)
	{
		LocalTaker.Reset();
	}
}

void AChaosImpactCrown::OnRep_State()
{
	// The host's word replaces this machine's guess.
	if (IsValid(State.Holder) || State.DroppedAt > LocalTakenAt)
	{
		LocalTaker.Reset();
	}
}

void AChaosImpactCrown::DetectLocalPickup(const double Now)
{
	UWorld* World = GetWorld();
	if (!World || !PRInMatch(World) || IsValid(State.Holder) || LocalTaker.IsValid() || bFrozen)
	{
		return;
	}
	const double Sight = HasAuthority() ? AChaosImpactBall::GetFairSightDelay(World) : 0.0;
	for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
	{
		AChaosImpactCharacter* Character = *It;
		const bool bOwn = PRIsOwnPlayer(Character);
		const bool bCPU = HasAuthority() && Character && !Character->IsPlayerControlled() && Character->GetController();
		if ((!bOwn && !bCPU) || !CanTake(Character, Now) || Claimers.Contains(Character))
		{
			continue;
		}
		if (HasAuthority() && Now < State.LaunchAt + State.Flight + Sight)
		{
			continue;
		}
		const FVector Body = Character->GetActorLocation();
		const float HalfHeight = Character->GetCapsuleComponent() ? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.0f;
		const float Feet = static_cast<float>(Body.Z) - HalfHeight;
		if (FVector::DistSquared2D(Body, State.To) > FMath::Square(ChaosImpactParty::CrownReach) || Feet < State.To.Z - 60.0f
			|| Feet > State.To.Z + 200.0f)
		{
			continue;
		}
		if (bOwn)
		{
			LocalTaker = Character;
			LocalTakenAt = Now;
			PlayTakeEffect(ShownLocation, true);
			if (HasAuthority())
			{
				Claim(Character, Now);
			}
			else
			{
				Character->ClaimPartyItem(ChaosImpactParty::CrownId);
			}
			return;
		}
		Claim(Character, Now);
		if (IsValid(State.Holder))
		{
			return;
		}
	}
}

void AChaosImpactCrown::PlayTakeEffect(const FVector& At, const bool bOwn)
{
	if (AChaosImpactFxKit* Fx = AChaosImpactFxKit::Spawn(this, At))
	{
		Fx->Stream.Initialize(static_cast<int32>(GetNow() * 1000.0));
		const FLinearColor GoldTint(1.0f, 0.72f, 0.18f), Pale(1.0f, 0.95f, 0.75f);
		FChaosImpactFxPiece Flash = PRPiece(EChaosImpactFxLook::Glow, FVector::ZeroVector, 0.3f, 80.0f, 260.0f, GoldTint, Pale, 6.0f);
		Flash.Attack = 0.05f;
		Fx->Add(Flash);
		for (int32 Ring = 0; Ring < 2; ++Ring)
		{
			FChaosImpactFxPiece Wave = PRPiece(EChaosImpactFxLook::AirRing, FVector::ZeroVector, 0.45f, 70.0f, 340.0f + Ring * 120.0f, GoldTint, Pale, 4.0f);
			Wave.Delay = Ring * 0.08f;
			Wave.RingFrom = 0.3f;
			Wave.RingTo = 0.95f;
			Wave.RingWidth = 0.06f;
			Fx->Add(Wave);
		}
		for (int32 Index = 0; Index < 16; ++Index)
		{
			FChaosImpactFxPiece Spark = PRPiece(EChaosImpactFxLook::Streak, FVector::ZeroVector, Fx->Stream.FRandRange(0.35f, 0.6f),
				Fx->Stream.FRandRange(44.0f, 70.0f), 10.0f, Index % 3 == 0 ? Pale : GoldTint, Pale, 5.0f);
			Spark.Velocity = PRUpward(Fx->Stream, 0.15f) * Fx->Stream.FRandRange(450.0f, 850.0f);
			Spark.Gravity = 1000.0f;
			Spark.Drag = 1.6f;
			Spark.Width = 4.0f;
			Fx->Add(Spark);
		}
	}
	if (bOwn)
	{
		ChaosImpactSfx::Play2D(this, EChaosImpactSfx::MiniBig, 0.8f, 1.0f);
	}
	else
	{
		ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::MiniBig, At, 0.7f, 1.0f);
	}
}

void AChaosImpactCrown::PlayDropEffect(const FVector& At)
{
	if (AChaosImpactFxKit* Fx = AChaosImpactFxKit::Spawn(this, At))
	{
		Fx->Stream.Initialize(static_cast<int32>(GetNow() * 977.0));
		const FLinearColor GoldTint(1.0f, 0.72f, 0.18f), Pale(1.0f, 0.95f, 0.75f);
		Fx->Add(PRPiece(EChaosImpactFxLook::Glow, FVector::ZeroVector, 0.22f, 60.0f, 160.0f, GoldTint, Pale, 4.5f));
		for (int32 Index = 0; Index < 8; ++Index)
		{
			FChaosImpactFxPiece Spark = PRPiece(EChaosImpactFxLook::Streak, FVector::ZeroVector, 0.35f, 40.0f, 8.0f, GoldTint, Pale, 4.0f);
			Spark.Velocity = PRUpward(Fx->Stream, 0.1f) * Fx->Stream.FRandRange(350.0f, 600.0f);
			Spark.Gravity = 1100.0f;
			Spark.Width = 3.0f;
			Fx->Add(Spark);
		}
	}
	ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::MiniMiss, At, 0.75f, 0.9f);
}

void AChaosImpactCrown::UpdateVisuals(const double Now, const float DeltaSeconds)
{
	const float T = static_cast<float>(FMath::Fmod(Now, 10000.0));
	const AChaosImpactCharacter* Holder = GetHolder();
	FVector Target;
	float Scale = PRCrownFloorScale;
	float SpinRate = 55.0f;
	bool bSnap = !bShownPlaced;
	if (Holder)
	{
		const float HalfHeight = Holder->GetCapsuleComponent() ? Holder->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.0f;
		const FVector Head = Holder->GetPresentationLocation() + FVector(0, 0, HalfHeight + PRCrownWornLift + 2.0f * FMath::Sin(T * 3.0f));
		Target = Head;
		Scale = PRCrownWornScale;
		SpinRate = 22.0f;
		// Reeled in by a hook: along the chain to its new holder's head.
		if (IsValid(State.Holder) && Now < State.LaunchAt + State.Flight)
		{
			const float U = static_cast<float>((Now - State.LaunchAt) / FMath::Max(State.Flight, 0.01f));
			Target = FMath::Lerp(FVector(State.From), Head, ChaosImpactEase::InOut(U)) + FVector(0, 0, 60.0f * FMath::Sin(U * PI));
			Scale = FMath::Lerp(PRCrownFloorScale, PRCrownWornScale, U);
			SpinRate = 720.0f;
			bSnap = true;
		}
	}
	else
	{
		const FVector Rest = FVector(State.To) + FVector(0, 0, PRCrownFloorLift + 7.0f * FMath::Sin(T * 2.0f));
		Target = Rest;
		if (Now < State.LaunchAt + State.Flight)
		{
			const float U = static_cast<float>((Now - State.LaunchAt) / FMath::Max(State.Flight, 0.01f));
			const float Height = FVector::Dist2D(State.From, State.To) > 50.0f ? 260.0f : 0.0f;
			Target = FMath::Lerp(FVector(State.From), Rest, Height > 0.0f ? U : ChaosImpactEase::Out(U)) + FVector(0, 0, 4.0f * Height * U * (1.0f - U));
			SpinRate = 600.0f;
		}
		bSnap = true;
	}
	// Worn, it follows its holder's head closely (smoothing the step of a remote player's movement a little).
	ShownLocation = bSnap ? Target : FMath::VInterpTo(ShownLocation, Target, DeltaSeconds, 30.0f);
	bShownPlaced = true;
	Spin = FMath::Fmod(Spin + SpinRate * DeltaSeconds, 360.0f);
	if (Model)
	{
		const float Tilt = Holder ? 0.0f : 8.0f * FMath::Sin(T * 1.4f);
		Model->SetWorldLocationAndRotation(ShownLocation, FRotator(Tilt, Spin, 0.0f));
		Model->SetWorldScale3D(FVector(Scale));
	}
	if (HaloMaterial)
	{
		HaloMaterial->SetScalarParameterValue(TEXT("Intensity"), (Holder ? 0.12f : 0.22f) * (0.85f + 0.15f * FMath::Sin(T * 3.0f)));
	}
	// Taken or knocked loose: the burst, on every machine (this machine's own player's was shown at their touch).
	if (State.TakenAt > SeenTakenAt)
	{
		const bool bFirst = SeenTakenAt < -50.0;
		SeenTakenAt = State.TakenAt;
		if (!bFirst && Now - State.TakenAt < 1.0 && !(IsValid(State.Holder) && PRIsOwnPlayer(State.Holder) && LocalTakenAt > 0.0
			&& FMath::Abs(LocalTakenAt - State.TakenAt) < 1.0))
		{
			PlayTakeEffect(ShownLocation, IsValid(State.Holder) && PRIsOwnPlayer(State.Holder));
		}
	}
	if (State.DroppedAt > SeenDroppedAt)
	{
		const bool bFirst = SeenDroppedAt < -50.0;
		SeenDroppedAt = State.DroppedAt;
		if (!bFirst && Now - State.DroppedAt < 1.0)
		{
			PlayDropEffect(FVector(State.From));
		}
	}
	// Lying on the floor: golden motes rise round it and a soft glow lies under it (near a player's camera).
	if (!Holder && Now >= NextMoteAt && Now >= State.LaunchAt + State.Flight)
	{
		NextMoteAt = Now + 0.12;
		const TArray<FVector, TInlineAllocator<4>> Cameras = PRCameras(GetWorld());
		if (PRNearestCamera(Cameras, State.To) < 5000.0f)
		{
			if (!Motes.IsValid())
			{
				Motes = AChaosImpactFxKit::Spawn(this, FVector::ZeroVector);
			}
			if (AChaosImpactFxKit* Fx = Motes.Get())
			{
				const float Angle = FMath::FRandRange(0.0f, UE_TWO_PI);
				const FVector Foot = FVector(State.To) + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * FMath::FRandRange(20.0f, 75.0f) + FVector(0, 0, 8);
				FChaosImpactFxPiece Mote = PRPiece(EChaosImpactFxLook::Glow, Foot, FMath::FRandRange(0.9f, 1.4f), 16.0f, 5.0f,
					FLinearColor(1.0f, 0.7f, 0.15f), FLinearColor(1.0f, 0.96f, 0.8f), 3.2f);
				Mote.Velocity = FVector(0, 0, FMath::FRandRange(90.0f, 150.0f));
				Mote.Attack = 0.2f;
				Mote.Flicker = 0.4f;
				Fx->Add(Mote);
				if (FMath::FRand() < 0.18f)
				{
					Fx->Add(PRPiece(EChaosImpactFxLook::GroundGlow, FVector(State.To) + FVector(0, 0, 3), 1.2f, 150.0f, 190.0f,
						FLinearColor(1.0f, 0.62f, 0.1f), FLinearColor(1.0f, 0.9f, 0.6f), 1.1f));
				}
			}
		}
	}
}

void AChaosImpactCrown::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Now = GetNow();
	if (HasAuthority())
	{
		SettleClaim(Now, false);
	}
	DetectLocalPickup(Now);
	if (LocalTaker.IsValid() && Now - LocalTakenAt > 1.5)
	{
		LocalTaker.Reset();
	}
	if (GetNetMode() != NM_DedicatedServer)
	{
		UpdateVisuals(Now, DeltaSeconds);
	}
	else
	{
		ShownLocation = IsValid(State.Holder) ? State.Holder->GetActorLocation() + FVector(0, 0, 120) : FVector(State.To);
	}
}

// =====================================================================================================================
// The character's side of a claim
// =====================================================================================================================

void AChaosImpactCharacter::ClaimPartyItem(const int32 Id)
{
	if (!HasAuthority() && IsLocallyControlled())
	{
		ServerClaimPartyItem(Id);
	}
}

void AChaosImpactCharacter::ServerClaimPartyItem_Implementation(const int32 Id)
{
	// When this player touched it on their own screen: the claim left at once, half a round trip ago (reaching back no
	// further than AChaosImpactBall::FairTouchReachSeconds).
	const double TouchTime = PRServerNow(GetWorld())
		- FMath::Min(static_cast<double>(GetNetworkRoundTripSeconds()) * 0.5, AChaosImpactBall::FairTouchReachSeconds);
	if (Id == ChaosImpactParty::CrownId)
	{
		if (AChaosImpactCrown* Crown = AChaosImpactCrown::Get(GetWorld()))
		{
			Crown->Claim(this, TouchTime);
			return;
		}
	}
	else if (AChaosImpactRubyField* Field = AChaosImpactRubyField::Get(GetWorld()))
	{
		Field->Claim(this, Id, TouchTime);
		return;
	}
	ClientPartyClaimResolved(Id, false);
}

void AChaosImpactCharacter::ClientPartyClaimResolved_Implementation(const int32 Id, const bool bAccepted)
{
	if (Id == ChaosImpactParty::CrownId)
	{
		if (AChaosImpactCrown* Crown = AChaosImpactCrown::Get(GetWorld()))
		{
			Crown->OnLocalClaimResolved(this, bAccepted);
		}
	}
	else if (AChaosImpactRubyField* Field = AChaosImpactRubyField::Get(GetWorld()))
	{
		Field->OnLocalClaimResolved(this, Id, bAccepted);
	}
}
