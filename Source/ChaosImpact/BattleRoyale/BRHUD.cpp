#include "BRHUD.h"
#include "ChaosImpactSettings.h"
#include "BRGame.h"
#include "BRIsland.h"
#include "BRIslandLayout.h"
#include "BROnline.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ChaosImpactBallFormComponent.h"
#include "ChaosImpactBuddyBall.h"
#include "ChaosImpactChargeWidget.h"
#include "ChaosImpactBallTypes.h"
#include "ChaosImpactCharacterPreview.h"
#include "ChaosImpactCharacterRoster.h"
#include "ChaosImpactLoadoutSubsystem.h"
#include "ChaosImpactPaint.h"
#include "Async/ParallelFor.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "TextureResource.h"

// The battle royale's own look, nothing to do with the VS HUD: a national broadcast and a propaganda poster. Flat cream
// paper, black ink and one red; everything round is a ball. (ChaosImpactPaint comes in function by function: a
// using-directive here would leak through the unity build.)
namespace BRHudDetail
{
	const FLinearColor Cream(0.93f, 0.85f, 0.66f);
	const FLinearColor Paper2(0.76f, 0.68f, 0.5f);
	const FLinearColor Sumi(0.016f, 0.015f, 0.018f);
	const FLinearColor Red(0.78f, 0.045f, 0.05f);
	const FLinearColor Mustard(0.95f, 0.56f, 0.04f);
	const FLinearColor Violet2(0.36f, 0.12f, 0.62f);
	const FLinearColor Mate(0.08f, 0.5f, 0.75f);

	FString ClockText(const float Seconds)
	{
		const int32 Total = FMath::CeilToInt(FMath::Max(Seconds, 0.0f));
		return FString::Printf(TEXT("%d:%02d"), Total / 60, Total % 60);
	}

	TArray<FVector2D> Circle(const FVector2D& C, const float R, const int32 Sides = 40)
	{
		TArray<FVector2D> Out;
		for (int32 I = 0; I < Sides; ++I)
		{
			const float A = I * UE_TWO_PI / Sides;
			Out.Add(C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R);
		}
		return Out;
	}

	void Fill(const ChaosImpactPaint::FPainter& P, const TArray<FVector2D>& Poly, const FLinearColor& Colour)
	{
		ChaosImpactPaint::HudPoly(P, Poly, ChaosImpactPaint::SolidFill(Colour), 1);
	}

	void Round(const ChaosImpactPaint::FPainter& P, const FVector2D& C, const float R, const FLinearColor& Colour) { Fill(P, Circle(C, R), Colour); }

	/** A child geometry turned about its middle: paper laid down by hand. Draw in it from 0,0 to W,H. */
	FGeometry Tilted(const FGeometry& G, const float X, const float Y, const float W, const float H, const float Degrees)
	{
		const float A = FMath::DegreesToRadians(Degrees);
		return G.MakeChild(FVector2f(W, H), FSlateLayoutTransform(FVector2f(X, Y)), FSlateRenderTransform(FQuat2f(A)), FVector2f(0.5f, 0.5f));
	}

	/** A ticket: a strip with a dark round bite out of each end, and its shadow. */
	void Ticket(const ChaosImpactPaint::FPainter& P, const float X, const float Y, const float W, const float H, const FLinearColor& Colour)
	{
		P.Box(X + 5.0f, Y + 6.0f, W, H, FLinearColor(0.0f, 0.0f, 0.0f, 0.4f));
		const ChaosImpactPaint::FPainter Over = ChaosImpactPaint::HudAbove(P, 1);
		Over.Box(X, Y, W, H, Colour);
		const ChaosImpactPaint::FPainter Bite = ChaosImpactPaint::HudAbove(P, 2);
		Round(Bite, FVector2D(X, Y + H * 0.5f), H * 0.2f, Sumi);
		Round(Bite, FVector2D(X + W, Y + H * 0.5f), H * 0.2f, Sumi);
	}

	/** A dodgeball: a disc with its seams. */
	void BallIcon(const ChaosImpactPaint::FPainter& P, const FVector2D& C, const float R, const FLinearColor& Body, const FLinearColor& Seam)
	{
		Round(P, C + FVector2D(R * 0.08f, R * 0.12f), R, FLinearColor(0.0f, 0.0f, 0.0f, 0.35f));
		const ChaosImpactPaint::FPainter Over = ChaosImpactPaint::HudAbove(P, 1);
		Round(Over, C, R, Body);
		const ChaosImpactPaint::FPainter Line = ChaosImpactPaint::HudAbove(P, 2);
		const float Width = FMath::Max(1.5f, R * 0.07f);
		Line.Line(C - FVector2D(R, 0.0), C + FVector2D(R, 0.0), Seam, Width);
		Line.Arc(C - FVector2D(R * 0.95f, 0.0), R * 0.8f, -56.0f, 56.0f, Seam, Width);
		Line.Arc(C + FVector2D(R * 0.95f, 0.0), R * 0.8f, 124.0f, 236.0f, Seam, Width);
	}

	/** Rows of dots shrinking to nothing, printed. */
	void Halftone(const ChaosImpactPaint::FPainter& P, const float X, const float Y, const int32 Columns, const int32 Rows, const float Step, const float Biggest, const FLinearColor& Colour)
	{
		for (int32 Row = 0; Row < Rows; ++Row)
		{
			const float Grow = static_cast<float>(Row + 1) / Rows;
			for (int32 Column = 0; Column < Columns; ++Column)
			{
				P.Disc(FVector2D(X + Column * Step + (Row % 2) * Step * 0.5f, Y + Row * Step * 0.85f), Biggest * Grow, Colour);
			}
		}
	}

	FLinearColor BallColour(const EChaosImpactBallType Type)
	{
		return Type == EChaosImpactBallType::Normal ? Cream : ChaosImpactBallTypes::GetColor(Type);
	}
}

UTexture2D* UBRHUD::GetMapTexture()
{
	static TWeakObjectPtr<UTexture2D> Cached;
	if (Cached.IsValid()) { return Cached.Get(); }
	constexpr int32 Side = 1024;
	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(Side * Side);
	const float Half = BRMap::HalfMap;
	const float Step = Half * 2.0f / Side;
	const auto Lin = [](const float R, const float G, const float B) { return FLinearColor(R, G, B); };
	// Each height once (one row and column spare, for the shading).
	TArray<float> Heights;
	Heights.SetNumUninitialized((Side + 1) * (Side + 1));
	ParallelFor(Side + 1, [&](const int32 Row)
	{
		for (int32 Column = 0; Column <= Side; ++Column)
		{
			Heights[Row * (Side + 1) + Column] = BRMap::HeightAt(Half - (Row + 0.5f) * Step, -Half + (Column + 0.5f) * Step);
		}
	});
	// Printed like a road atlas: flat inks, contour lines, +X the top of the sheet.
	ParallelFor(Side, [&](const int32 Row)
	{
		for (int32 Column = 0; Column < Side; ++Column)
		{
			const float WX = Half - (Row + 0.5f) * Step;
			const float WY = -Half + (Column + 0.5f) * Step;
			const float H = Heights[Row * (Side + 1) + Column];
			FLinearColor C;
			if (H < 0.0f)
			{
				C = FMath::Lerp(Lin(0.22f, 0.48f, 0.5f), Lin(0.05f, 0.2f, 0.3f), FMath::Clamp(-H / 260.0f, 0.0f, 1.0f));
			}
			else
			{
				const BRMap::FPaint Paint = BRMap::PaintAt(WX, WY, H);
				C = Lin(0.55f, 0.56f, 0.28f);
				C = FMath::Lerp(C, Lin(0.3f, 0.38f, 0.18f), Paint.Moss);
				C = FMath::Lerp(C, Lin(0.66f, 0.42f, 0.2f), Paint.Dry);
				C = FMath::Lerp(C, Lin(0.78f, 0.66f, 0.4f), Paint.Sand);
				C = FMath::Lerp(C, Lin(0.86f, 0.8f, 0.64f), Paint.Paved);
				C = FMath::Lerp(C, Lin(0.56f, 0.54f, 0.5f), Paint.Concrete);
				C = FMath::Lerp(C, Lin(0.93f, 0.92f, 0.86f), Paint.Snow);
				C = FMath::Lerp(C, Lin(0.2f, 0.17f, 0.16f), Paint.Ash);
				C = FMath::Lerp(C, Lin(0.58f, 0.2f, 0.12f), Paint.Dirt * 0.85f);
				// A contour every so often.
				const float Band = FMath::Frac(H / 260.0f);
				if (Paint.Paved < 0.3f && (Band < 0.05f || Band > 0.95f)) { C *= 0.74f; }
				// Lit from the top left (the row above is further up the sheet).
				const float DX = (Row > 0 ? Heights[(Row - 1) * (Side + 1) + Column] : H) - H;
				const float DY = (Column > 0 ? Heights[Row * (Side + 1) + Column - 1] : H) - H;
				C *= FMath::Clamp(1.0f + (DX + DY) / Step * 1.1f, 0.68f, 1.2f);
			}
			FLinearColor Out = C;
			Pixels[Row * Side + Column] = Out.ToFColor(true);
		}
	});
	UTexture2D* Texture = UTexture2D::CreateTransient(Side, Side, PF_B8G8R8A8);
	if (!Texture) { return nullptr; }
	Texture->AddToRoot();
	Texture->SRGB = true;
	Texture->Filter = TF_Bilinear;
	void* Data = Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Data, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
	Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
	Texture->UpdateResource();
	Cached = Texture;
	return Texture;
}

void UBRHUD::Init(ABRPlayerController* InOwner)
{
	Owner = InOwner;
	MapBrush.SetResourceObject(GetMapTexture());
	MapBrush.ImageSize = FVector2D(1024.0, 1024.0);
	MapBrush.DrawAs = ESlateBrushDrawType::Image;
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UBRHUD::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	Clock += InDeltaTime;
	LastSize = FVector2D(MyGeometry.GetLocalSize());
	if (const ABRCharacter* Me = Owner.IsValid() ? Cast<ABRCharacter>(Owner->GetPawn()) : nullptr)
	{
		GhostHealth = FMath::FInterpConstantTo(FMath::Max(GhostHealth, Me->GetHealthNow()), Me->GetHealthNow(), InDeltaTime, 40.0f);
		GhostArmor = FMath::FInterpConstantTo(FMath::Max(GhostArmor, Me->Armor), Me->Armor, InDeltaTime, 40.0f);
	}
	if (Owner.IsValid() && FSlateApplication::IsInitialized())
	{
		const ABRGameState* State = GetWorld()->GetGameState<ABRGameState>();
		HoveredResult = INDEX_NONE;
		if (State && State->Phase == EBRPhase::Over)
		{
			const float S = FMath::Min(LastSize.X / 1600.0, LastSize.Y / 900.0) * ChaosImpactSettings::GetHudSizeFactor();
			FBox2D Buttons[2];
			GetResultButtons(LastSize, S, Buttons);
			const FVector2D Local = FVector2D(GetCachedGeometry().AbsoluteToLocal(FSlateApplication::Get().GetCursorPos()));
			for (int32 I = 0; I < 2; ++I) { if (Buttons[I].IsInside(Local)) { HoveredResult = I; } }
		}
	}
}

FVector2D UBRHUD::WorldToMap(const FVector2D& World, const FVector2D& Min, const float Side, const FVector2D& Centre, const float Zoom) const
{
	const double Scale = Side * 0.5 * Zoom / BRMap::HalfMap;
	return FVector2D(Min.X + Side * 0.5 + (World.Y - Centre.Y) * Scale, Min.Y + Side * 0.5 - (World.X - Centre.X) * Scale);
}

FVector2D UBRHUD::MapToWorld(const FVector2D& Local, const FVector2D& Min, const float Side, const FVector2D& Centre, const float Zoom) const
{
	const double Scale = Side * 0.5 * Zoom / BRMap::HalfMap;
	return FVector2D(Centre.X - (Local.Y - Min.Y - Side * 0.5) / Scale, Centre.Y + (Local.X - Min.X - Side * 0.5) / Scale);
}

void UBRHUD::PaintMapBody(const FGeometry& G, FSlateWindowElementList& Elements, const int32 Layer, const FVector2D& Min, const float Side, const FVector2D& Centre, const float Zoom,
	const ABRCharacter* Me, const ABRGameState* State, const bool bLabels, const float Rounding) const
{
	using namespace ChaosImpactPaint;
	using namespace BRHudDetail;
	const FPainter Up{G, Elements, Layer + 1}, Text{G, Elements, Layer + 2};
	const FVector2D Middle = Min + FVector2D(Side * 0.5, Side * 0.5);
	const float Radius = Side * 0.5f;
	const bool bRound = Rounding > 0.0f;
	const double Span = 0.5 / Zoom;
	const double CU = (Centre.Y + BRMap::HalfMap) / (BRMap::HalfMap * 2.0), CV = (BRMap::HalfMap - Centre.X) / (BRMap::HalfMap * 2.0);
	if (bRound)
	{
		// The map printed on a ball: a fan of triangles carrying the picture.
		const FSlateResourceHandle Handle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(MapBrush);
		TArray<FSlateVertex> Verts;
		TArray<SlateIndex> Indexes;
		const auto Add = [&](const FVector2D& Local)
		{
			const FVector2f Abs = G.LocalToAbsolute(FVector2f(Local));
			const FVector2f UV(static_cast<float>(CU + (Local.X - Middle.X) / Radius * Span), static_cast<float>(CV + (Local.Y - Middle.Y) / Radius * Span));
			Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(FSlateRenderTransform(), Abs, UV, FColor::White));
		};
		Add(Middle);
		constexpr int32 Sides = 56;
		for (int32 I = 0; I <= Sides; ++I)
		{
			const float A = I * UE_TWO_PI / Sides;
			Add(Middle + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Radius);
			if (I > 0)
			{
				Indexes.Add(0);
				Indexes.Add(static_cast<SlateIndex>(I));
				Indexes.Add(static_cast<SlateIndex>(I + 1));
			}
		}
		FSlateDrawElement::MakeCustomVerts(Elements, Layer, Handle, Verts, Indexes, nullptr, 0, 0);
	}
	else
	{
		MapBrush.SetUVRegion(FBox2f(FVector2f(CU - Span, CV - Span), FVector2f(CU + Span, CV + Span)));
		FSlateDrawElement::MakeBox(Elements, Layer, G.ToPaintGeometry(FVector2f(Side, Side), FSlateLayoutTransform(FVector2f(Min))), &MapBrush, ESlateDrawEffect::None, FLinearColor::White);
		Elements.PushClip(FSlateClippingZone(G.MakeChild(FVector2f(Side, Side), FSlateLayoutTransform(FVector2f(Min)))));
	}
	const double Scale = Side * 0.5 * Zoom / BRMap::HalfMap;
	const auto InDisc = [&](const FVector2D& P) { return !bRound || FVector2D::Distance(P, Middle) <= Radius; };
	// A circle on the map, kept inside a round map by leaving off what falls outside it.
	const auto ZoneRing = [&](const FVector2D& World, const float WorldRadius, const FLinearColor& Colour, const float Width)
	{
		const FVector2D At = WorldToMap(World, Min, Side, Centre, Zoom);
		const float R = static_cast<float>(WorldRadius * Scale);
		constexpr int32 Steps = 120;
		for (int32 I = 0; I < Steps; ++I)
		{
			const float A0 = I * UE_TWO_PI / Steps, A1 = (I + 1) * UE_TWO_PI / Steps;
			const FVector2D P0 = At + FVector2D(FMath::Cos(A0), FMath::Sin(A0)) * R, P1 = At + FVector2D(FMath::Cos(A1), FMath::Sin(A1)) * R;
			if (InDisc(P0) && InDisc(P1)) { Up.Line(P0, P1, Colour, Width); }
		}
	};
	if (bLabels)
	{
		using namespace BRLayout;
		const struct { const FBRDistrict* D; const TCHAR* Name; } Places[] = {{&Capital, TEXT("首都ドッチ")}, {&Stadium, TEXT("大統領スタジアム")}, {&Factory, TEXT("ボール工場")}, {&Port, TEXT("避球港")}, {&Town, TEXT("球見町")}, {&Ridgetown, TEXT("尾根町")},
			{&Farm, TEXT("ゴム畑")}, {&Westfarm, TEXT("西の農園")}, {&Funfair, TEXT("遊園地")}, {&Airfield, TEXT("合衆国飛行場")}, {&Lab, TEXT("球体研究所")}, {&Canyon, TEXT("赤い峡谷")}, {&Mountain, TEXT("白球山")}, {&Forest, TEXT("迷いの森")}, {&Crater, TEXT("大統領の失投")}};
		for (const auto& Place : Places)
		{
			const FVector2D At = WorldToMap(Place.D->At, Min, Side, Centre, Zoom);
			const float Size = HudFit(Place.Name, 14.0f, 150.0f, TEXT("Black"));
			const float W = HudMeasure(Place.Name, Size, TEXT("Black")) + 12.0f;
			Up.Box(At.X - W * 0.5f, At.Y - 12.0f, W, 22.0f, Cream);
			Text.Text(Place.Name, At.X, At.Y - 11.0f, Size, Sumi, ETextAlign::Center, TEXT("Black"));
		}
	}
	if (State && (State->Phase == EBRPhase::Play || State->Phase == EBRPhase::Over))
	{
		FVector2D ZoneNow;
		float RadiusNow;
		State->GetZoneNow(ZoneNow, RadiusNow);
		ZoneRing(State->ZoneTo, State->ZoneToRadius, Cream, 2.0f);
		ZoneRing(ZoneNow, RadiusNow, Red, 3.0f);
	}
	if (State && State->Phase == EBRPhase::Ship)
	{
		// The airship's line.
		constexpr int32 Dashes = 60;
		for (int32 I = 0; I < Dashes; I += 2)
		{
			const FVector2D P0 = WorldToMap(FMath::Lerp(State->ShipFrom, State->ShipTo, static_cast<double>(I) / Dashes), Min, Side, Centre, Zoom);
			const FVector2D P1 = WorldToMap(FMath::Lerp(State->ShipFrom, State->ShipTo, static_cast<double>(I + 1) / Dashes), Min, Side, Centre, Zoom);
			if (InDisc(P0) && InDisc(P1)) { Up.Line(P0, P1, Red, 3.0f); }
		}
		const FVector2D Ship = WorldToMap(FVector2D(State->GetShipLocation()), Min, Side, Centre, Zoom);
		if (InDisc(Ship)) { Up.Disc(Ship, 9.0f, Sumi); Text.Disc(Ship, 6.5f, Red); }
	}
	if (Owner.IsValid() && Owner->HasChosenLanding() && State && State->Phase == EBRPhase::Ship)
	{
		// The landing mark.
		FVector2D At = WorldToMap(Owner->GetChosenLanding(), Min, Side, Centre, Zoom);
		if (bRound && !InDisc(At)) { At = Middle + (At - Middle).GetSafeNormal() * (Radius - 9.0f); }
		Up.Disc(At, 11.0f, Sumi);
		Text.Disc(At, 8.0f, Mustard);
	}
	// Only your own team is on the map.
	if (State)
	{
		const int32 MyTeamId = Owner.IsValid() ? Owner->GetMyTeam() : 0;
		for (int32 Index = 0; Index < State->Roster.Num(); ++Index)
		{
			const FBRRosterEntry& Entry = State->Roster[Index];
			if (Entry.Team != MyTeamId || !Entry.bAlive) { continue; }
			const bool bMe = Me && Me->Fighter == Index;
			FVector2D At = WorldToMap(Entry.Where(), Min, Side, Centre, Zoom);
			// On a round map a teammate far away waits at the rim, on their bearing.
			if (bRound && !InDisc(At)) { At = Middle + (At - Middle).GetSafeNormal() * (Radius - 7.0f); }
			Up.Disc(At, bMe ? 9.0f : 7.5f, Sumi);
			Text.Disc(At, bMe ? 6.5f : 5.5f, Entry.bDowned ? Red : bMe ? Mustard : Mate);
			if (bMe && Me)
			{
				const FVector Facing = Me->GetActorForwardVector();
				Text.Line(At, At + FVector2D(Facing.Y, -Facing.X) * 16.0f, Mustard, 3.0f);
			}
		}
	}
	if (!bRound) { Elements.PopClip(); }
}

void UBRHUD::PaintMinimap(const FGeometry& G, FSlateWindowElementList& Elements, const int32 Layer, const ABRCharacter* Me, const ABRGameState* State, const float S) const
{
	using namespace ChaosImpactPaint;
	using namespace BRHudDetail;
	// The radar is a ball: the map on its face, the stitching round it.
	const float R = 124.0f * S;
	const FVector2D C(26.0f * S + R, 26.0f * S + R);
	const FPainter Under{G, Elements, Layer}, Rim{G, Elements, Layer + 5};
	Round(Under, C + FVector2D(6.0, 8.0) * S, R + 13.0f * S, FLinearColor(0.0f, 0.0f, 0.0f, 0.4f));
	Round(HudAbove(Under, 1), C, R + 13.0f * S, Red);
	const FVector2D Centre = Me ? FVector2D(Me->GetActorLocation()) : FVector2D::ZeroVector;
	PaintMapBody(G, Elements, Layer + 2, C - FVector2D(R, R), R * 2.0f, Centre, Me && Me->IsFlying() ? 2.2f : 5.5f, Me, State, false, 1.0f);
	Rim.Ring(C, R + 6.5f * S, Cream, 1.6f * S, 96);
	constexpr int32 Stitches = 44;
	for (int32 I = 0; I < Stitches; ++I)
	{
		const float A = I * UE_TWO_PI / Stitches;
		const FVector2D Way(FMath::Cos(A), FMath::Sin(A));
		const FVector2D Side(-Way.Y, Way.X);
		Rim.Line(C + Way * (R + 3.0f * S) - Side * 3.0f * S, C + Way * (R + 10.5f * S) + Side * 3.0f * S, Cream, 1.8f * S);
	}
	Round(Rim, C - FVector2D(0.0, R + 7.0f * S), 15.0f * S, Sumi);
	HudAbove(Rim, 1).Text(TEXT("N"), C.X, C.Y - R - 19.0f * S, 18.0f * S, Cream, ETextAlign::Center, TEXT("Black"));
	const FVector2D KeyAt = C + FVector2D(R * 0.72, R * 0.72);
	Round(Rim, KeyAt, 22.0f * S, Cream);
	HudAbove(Rim, 1).Text(TEXT("M"), KeyAt.X, KeyAt.Y - 14.0f * S, 21.0f * S, Sumi, ETextAlign::Center, TEXT("Black"));
}

void UBRHUD::GetBigMapRect(const FVector2D& Size, const float S, FVector2D& OutMin, float& OutSide) const
{
	OutSide = FMath::Min(static_cast<float>(Size.Y) - 110.0f * S, static_cast<float>(Size.X) - 560.0f * S);
	OutMin = FVector2D(Size.X - OutSide - 70.0 * S, (Size.Y - OutSide) * 0.5);
}

void UBRHUD::PaintBigMap(const FGeometry& G, FSlateWindowElementList& Elements, const int32 Layer, const ABRCharacter* Me, const ABRGameState* State, const float S, const FVector2D& Size) const
{
	using namespace ChaosImpactPaint;
	using namespace BRHudDetail;
	FVector2D Min;
	float Side;
	GetBigMapRect(Size, S, Min, Side);
	const FPainter Back{G, Elements, Layer}, Sheet{G, Elements, Layer + 1}, Rim{G, Elements, Layer + 9};
	Back.Box(0.0f, 0.0f, Size.X, Size.Y, FLinearColor(0.0f, 0.0f, 0.0f, 0.72f));
	// An atlas page: paper round the map, a title block down the side.
	const float Margin = 22.0f * S;
	Sheet.Box(Min.X - Margin + 8.0f * S, Min.Y - Margin + 10.0f * S, Side + Margin * 2.0f, Side + Margin * 2.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.5f));
	HudAbove(Sheet, 1).Box(Min.X - Margin, Min.Y - Margin, Side + Margin * 2.0f, Side + Margin * 2.0f, Cream);
	PaintMapBody(G, Elements, Layer + 3, Min, Side, FVector2D::ZeroVector, MapZoom, Me, State, true, 0.0f);
	Rim.Outline(Min.X, Min.Y, Side, Side, Sumi, 2.0f * S);
	const float Bar = FMath::Max(static_cast<float>(Min.X) - 90.0f * S, 200.0f * S);
	const FPainter Word{G, Elements, Layer + 10};
	const FString Heading = TEXT("合衆国全図");
	Word.Text(Heading, 40.0f * S, 90.0f * S, HudFit(Heading, 46.0f * S, Bar, TEXT("Black")), Cream, ETextAlign::Left, TEXT("Black"));
	Rim.Box(40.0f * S, 158.0f * S, Bar, 5.0f * S, Red);
	const TCHAR* const Keys[] = {TEXT("ホイール　拡大・縮小"), TEXT("M　閉じる"), TEXT("赤い線　いまの安全地帯"), TEXT("白い線　次の安全地帯"), TEXT("青い点　味方")};
	for (int32 I = 0; I < UE_ARRAY_COUNT(Keys); ++I)
	{
		Word.Text(Keys[I], 40.0f * S, (186.0f + I * 40.0f) * S, HudFit(Keys[I], 20.0f * S, Bar, TEXT("Bold")), Cream, ETextAlign::Left, TEXT("Bold"));
	}
}

bool UBRHUD::HandleWheel(const float Delta)
{
	MapZoom = FMath::Clamp(MapZoom * (Delta > 0.0f ? 1.25f : 0.8f), 1.0f, 8.0f);
	return true;
}

void UBRHUD::GetResultButtons(const FVector2D& Size, const float S, FBox2D OutButtons[2]) const
{
	const float W = 300.0f * S, H = 74.0f * S, Y = static_cast<float>(Size.Y) - 110.0f * S;
	OutButtons[0] = FBox2D(FVector2D(Size.X * 0.5 - W - 16.0 * S, Y), FVector2D(Size.X * 0.5 - 16.0 * S, Y + H));
	OutButtons[1] = FBox2D(FVector2D(Size.X * 0.5 + 16.0 * S, Y), FVector2D(Size.X * 0.5 + W + 16.0 * S, Y + H));
}

bool UBRHUD::HandleClick(const FVector2D& ScreenPosition)
{
	if (!Owner.IsValid()) { return false; }
	const ABRGameState* State = GetWorld()->GetGameState<ABRGameState>();
	if (!State) { return false; }
	const FVector2D Local = FVector2D(GetCachedGeometry().AbsoluteToLocal(ScreenPosition));
	const float S = FMath::Min(LastSize.X / 1600.0, LastSize.Y / 900.0) * ChaosImpactSettings::GetHudSizeFactor();
	if (State->Phase == EBRPhase::Ship && Owner->IsMapOpen())
	{
		FVector2D Min;
		float Side;
		GetBigMapRect(LastSize, S, Min, Side);
		if (FBox2D(Min, Min + FVector2D(Side, Side)).IsInside(Local))
		{
			const FVector2D World = MapToWorld(Local, Min, Side, FVector2D::ZeroVector, MapZoom);
			if (BRMap::ShoreDistance(World.X, World.Y) > 1800.0f)
			{
				Owner->ChooseLanding(World);
				PadCursor = World;
			}
			return true;
		}
	}
	if (State->Phase == EBRPhase::Over)
	{
		FBox2D Buttons[2];
		GetResultButtons(LastSize, S, Buttons);
		if (Buttons[0].IsInside(Local)) { Owner->ReturnToBRTitle(); return true; }
		if (Buttons[1].IsInside(Local)) { Owner->LeaveToMainTitle(); return true; }
	}
	return false;
}

void UBRHUD::MovePadCursor(const FVector2D& Stick, const float DeltaSeconds)
{
	if (!Owner.IsValid()) { return; }
	PadCursor += FVector2D(Stick.Y, Stick.X) * 60000.0 * DeltaSeconds;
	PadCursor.X = FMath::Clamp(PadCursor.X, -90000.0, 90000.0);
	PadCursor.Y = FMath::Clamp(PadCursor.Y, -90000.0, 90000.0);
	if (BRMap::ShoreDistance(PadCursor.X, PadCursor.Y) > 1800.0f) { Owner->ChooseLanding(PadCursor); }
}

void UBRHUD::HandlePadConfirm()
{
	if (!Owner.IsValid()) { return; }
	if (const ABRGameState* State = GetWorld()->GetGameState<ABRGameState>(); State && State->Phase == EBRPhase::Over) { Owner->ReturnToBRTitle(); }
}

void UBRHUD::PaintVitals(const FGeometry& G, FSlateWindowElementList& Elements, const int32 Layer, const ABRCharacter* Me, const ABRGameState* State, const float S) const
{
	using namespace ChaosImpactPaint;
	using namespace BRHudDetail;
	if (!Me || Me->IsEliminated()) { return; }
	const FVector2D Size = FVector2D(G.GetLocalSize());
	const FPainter Under{G, Elements, Layer}, Body{G, Elements, Layer + 1}, Over{G, Elements, Layer + 4}, Word{G, Elements, Layer + 7};

	// ---- HP: one big ball, filled with red up to what's left. Armour: the plates round its rim.
	const float R = 86.0f * S;
	const FVector2D C(36.0f * S + R, Size.Y - 34.0f * S - R);
	Round(Under, C + FVector2D(6.0, 8.0) * S, R + 22.0f * S, FLinearColor(0.0f, 0.0f, 0.0f, 0.4f));
	Round(HudAbove(Under, 1), C, R + 22.0f * S, Sumi);
	Round(HudAbove(Body, 1), C, R, FLinearColor(0.1f, 0.09f, 0.09f));
	const float Health = Me->IsDowned() ? Me->DownedHealth : Me->GetHealthNow();
	const float Max = Me->IsDowned() ? BRRules::DownedHealth : BRRules::MaxHealth;
	const float Ratio = FMath::Clamp(Health / Max, 0.0f, 1.0f);
	const float GhostRatio = Me->IsDowned() ? Ratio : FMath::Clamp(GhostHealth / Max, 0.0f, 1.0f);
	const auto Level = [&](const float Fraction) { return static_cast<float>(C.Y) + R - 2.0f * R * Fraction; };
	if (GhostRatio > Ratio) { Fill(HudAbove(Body, 2), HudClip(Circle(C, R, 56), Level(GhostRatio), true), Cream); }
	const FLinearColor Blood = Me->IsDowned() ? FMath::Lerp(Red, Cream, 0.5f + 0.5f * FMath::Sin(Clock * 8.0f)) : Ratio > 0.3f ? Red : FMath::Lerp(Red, Mustard, 0.5f + 0.5f * FMath::Sin(Clock * 6.0f));
	if (Ratio > 0.0f) { Fill(HudAbove(Body, 3), HudClip(Circle(C, R, 56), Level(Ratio), true), Blood); }
	// The ball's seams over the fill, and the number on them.
	const FLinearColor Seam(0.0f, 0.0f, 0.0f, 0.55f);
	Over.Line(C - FVector2D(R, 0.0), C + FVector2D(R, 0.0), Seam, 3.0f * S);
	Over.Arc(C - FVector2D(R * 0.95f, 0.0), R * 0.8f, -56.0f, 56.0f, Seam, 3.0f * S);
	Over.Arc(C + FVector2D(R * 0.95f, 0.0), R * 0.8f, 124.0f, 236.0f, Seam, 3.0f * S);
	const FString Number = FString::FromInt(FMath::CeilToInt(Health));
	Word.Text(Number, C.X, C.Y - 42.0f * S, HudFit(Number, 56.0f * S, R * 1.5f, TEXT("Black")), Cream, ETextAlign::Center, TEXT("Black"), 3.0f * S, Sumi);
	const float Plates = FMath::Max(FMath::RoundToFloat(Me->ArmorMax / 25.0f), 0.0f);
	if (Plates > 0.0f)
	{
		const FLinearColor Tint = Me->ArmorMax >= 100.0f ? Mustard : Me->ArmorMax >= 75.0f ? Violet2 : Mate;
		const float Each = 360.0f / Plates;
		const float Filled = Me->Armor / 25.0f;
		const float GhostFilled = GhostArmor / 25.0f;
		for (int32 I = 0; I < Plates; ++I)
		{
			const float From = -90.0f + I * Each + 3.0f;
			const float Sweep = Each - 6.0f;
			Over.Arc(C, R + 11.0f * S, From, From + Sweep, FLinearColor(1.0f, 1.0f, 1.0f, 0.16f), 11.0f * S);
			const float Ghost = FMath::Clamp(GhostFilled - I, 0.0f, 1.0f), Part = FMath::Clamp(Filled - I, 0.0f, 1.0f);
			if (Ghost > Part) { HudAbove(Over, 1).Arc(C, R + 11.0f * S, From, From + Sweep * Ghost, Cream, 11.0f * S); }
			if (Part > 0.0f) { HudAbove(Over, 2).Arc(C, R + 11.0f * S, From, From + Sweep * Part, Tint, 11.0f * S); }
		}
	}

	// ---- The name on a strip of paper laid across, the kill count on it, stamina pips under it.
	{
		const float Left = static_cast<float>(C.X) + R + 18.0f * S, W = 300.0f * S, H = 42.0f * S, Top = static_cast<float>(Size.Y) - 150.0f * S;
		const FGeometry Strip = Tilted(G, Left, Top, W, H, -2.0f);
		const FPainter SP{Strip, Elements, Layer + 1}, SW{Strip, Elements, Layer + 4};
		SP.Box(5.0f * S, 6.0f * S, W, H, FLinearColor(0.0f, 0.0f, 0.0f, 0.4f));
		HudAbove(SP, 1).Box(0.0f, 0.0f, W, H, Cream);
		const FString Name = Me->FighterName.IsEmpty() ? FString(TEXT("あなた")) : Me->FighterName;
		SW.Text(Name, 14.0f * S, 5.0f * S, HudFit(Name, 24.0f * S, W - 120.0f * S, TEXT("Black")), Sumi, ETextAlign::Left, TEXT("Black"));
		const FString Kills = FString::Printf(TEXT("%d撃破"), Me->KillCount);
		SW.Text(Kills, W - 14.0f * S, 7.0f * S, 21.0f * S, Red, ETextAlign::Right, TEXT("Black"));
		const float Pips = Me->GetMaxStamina();
		for (int32 I = 0; I < Pips; ++I)
		{
			const float Part = FMath::Clamp(Me->GetStamina() - I, 0.0f, 1.0f);
			const float PX = 2.0f * S + I * ((W - 4.0f * S) / Pips);
			const float PW = (W - 4.0f * S) / Pips - 3.0f * S;
			SP.Box(PX, H + 5.0f * S, PW, 8.0f * S, FLinearColor(0.0f, 0.0f, 0.0f, 0.6f));
			HudAbove(SP, 1).Box(PX, H + 5.0f * S, PW * Part, 8.0f * S, Part >= 1.0f ? Mustard : Paper2);
		}
	}

	// ---- The balls in hand, drawn as balls; the next one thrown is the big one.
	{
		const float Y = static_cast<float>(Size.Y) - 66.0f * S;
		const FVector2D Next(C.X + R + 62.0f * S, Y), Spare(C.X + R + 140.0f * S, Y + 8.0f * S);
		Round(Under, Next, 40.0f * S, Sumi);
		Round(Under, Spare, 30.0f * S, Sumi);
		const int32 Count = Me->GetCarriedBallCount();
		for (int32 Held = 0; Held < 2; ++Held)
		{
			const FVector2D At = Held == 0 ? Next : Spare;
			const float BallR = Held == 0 ? 34.0f * S : 25.0f * S;
			if (Held < Count)
			{
				// The same ball as the rest of the game, in this HUD's holder (drawn at its own scale, so its lines keep their weight).
				EChaosImpactBallType Type = Me->GetCarriedBallType(Held);
				if (const auto* Shell = Me->GetBallForm()->GetHeldBall(Held); Shell && Shell->IsPrisonBall()) { Type = EChaosImpactBallType::Prison; }
				const FGeometry Unit = G.MakeChild(FVector2f(static_cast<float>(Size.X) / S, static_cast<float>(Size.Y) / S), FSlateLayoutTransform(S));
				const FPainter Drawn{Unit, Elements, Over.Layer};
				UChaosImpactChargeWidget::PaintBallIcon(Drawn, At / S, BallR / S, true, Type, Type == EChaosImpactBallType::Snow ? Me->GetSnowGrowth(Held) : 0.0f, static_cast<float>(FMath::Fmod(FPlatformTime::Seconds(), 3600.0)));
			}
			else { Over.Ring(At, BallR, FLinearColor(1.0f, 1.0f, 1.0f, 0.2f), 2.0f * S); }
		}
		if (Me->IsChargingThrow())
		{
			HudAbove(Over, 2).Arc(Next, 40.0f * S, -90.0f, -90.0f + 360.0f * Me->GetThrowChargeAlpha(), Mustard, 6.0f * S);
		}
	}

	// ---- Heals: medicine bottles standing in a row.
	{
		const float X = static_cast<float>(C.X) + R + 214.0f * S, Y = static_cast<float>(Size.Y) - 100.0f * S;
		for (int32 I = 0; I < BRRules::MaxHeals; ++I)
		{
			const bool bHave = I < Me->Heals;
			const float BX = X + I * 26.0f * S;
			RoundedBox(Under, BX, Y + 10.0f * S, 21.0f * S, 40.0f * S, 5.0f * S, Sumi);
			Under.Box(BX + 6.0f * S, Y + 3.0f * S, 9.0f * S, 8.0f * S, Sumi);
			if (bHave)
			{
				RoundedBox(Body, BX + 3.0f * S, Y + 13.0f * S, 15.0f * S, 34.0f * S, 3.0f * S, Cream);
				Over.Box(BX + 6.0f * S, Y + 26.0f * S, 9.0f * S, 3.0f * S, Red);
				Over.Box(BX + 9.0f * S, Y + 23.0f * S, 3.0f * S, 9.0f * S, Red);
			}
		}
		Word.Text(TEXT("H"), X - 18.0f * S, Y + 16.0f * S, 21.0f * S, Cream, ETextAlign::Center, TEXT("Black"), 2.0f * S, Sumi);
		if (Me->HealEndsAt > 0.0 && State)
		{
			const float T = 1.0f - FMath::Clamp(static_cast<float>(Me->HealEndsAt - State->GetServerWorldTimeSeconds()) / BRRules::HealSeconds, 0.0f, 1.0f);
			HudAbove(Over, 2).Arc(C, R * 0.72f, -90.0f, -90.0f + 360.0f * T, Cream, 6.0f * S);
		}
	}
}

void UBRHUD::PaintTeam(const FGeometry& G, FSlateWindowElementList& Elements, const int32 Layer, const ABRGameState* State, const float S) const
{
	using namespace ChaosImpactPaint;
	using namespace BRHudDetail;
	if (!State || State->TeamSize <= 1 || !Owner.IsValid()) { return; }
	const int32 Mine = Owner->GetMyTeam();
	const ABRCharacter* Me = Cast<ABRCharacter>(Owner->GetPawn());
	const FVector2D Size = FVector2D(G.GetLocalSize());
	const FPainter Under{G, Elements, Layer}, Body{G, Elements, Layer + 1}, Over{G, Elements, Layer + 3}, Word{G, Elements, Layer + 5};
	int32 Row = 0;
	for (int32 Index = 0; Index < State->Roster.Num(); ++Index)
	{
		const FBRRosterEntry& Entry = State->Roster[Index];
		if (Entry.Team != Mine || (Me && Me->Fighter == Index)) { continue; }
		// Each teammate: a small ball gauge and their name on a slip.
		const float R = 25.0f * S;
		const FVector2D C(50.0f * S, Size.Y - 262.0f * S - Row * 66.0f * S);
		const float W = 200.0f * S, H = 30.0f * S;
		Under.Box(C.X, C.Y - H * 0.5f, R + W, H, Entry.bAlive ? Cream : Paper2);
		Word.Text(Entry.Name, C.X + R + 10.0f * S, C.Y - H * 0.5f + 3.0f * S, HudFit(Entry.Name, 18.0f * S, W - 20.0f * S, TEXT("Black")), Sumi, ETextAlign::Left, TEXT("Black"));
		Round(Body, C, R + 6.0f * S, Sumi);
		Round(HudAbove(Body, 1), C, R, FLinearColor(0.12f, 0.11f, 0.11f));
		if (Entry.bAlive)
		{
			const float Part = FMath::Clamp(Entry.Health / 100.0f, 0.0f, 1.0f);
			Fill(Over, HudClip(Circle(C, R, 28), static_cast<float>(C.Y) + R - 2.0f * R * Part, true), Entry.bDowned ? Mustard : Red);
			if (Entry.Armor > 0) { HudAbove(Over, 1).Arc(C, R + 3.0f * S, -90.0f, -90.0f + 360.0f * FMath::Clamp(Entry.Armor / 100.0f, 0.0f, 1.0f), Mate, 4.0f * S); }
		}
		else
		{
			Word.Line(C - FVector2D(R * 0.6f, R * 0.6f), C + FVector2D(R * 0.6f, R * 0.6f), Red, 5.0f * S);
			Word.Line(C + FVector2D(R * 0.6f, -R * 0.6f), C + FVector2D(-R * 0.6f, R * 0.6f), Red, 5.0f * S);
		}
		++Row;
	}
}

void UBRHUD::PaintTopBar(const FGeometry& G, FSlateWindowElementList& Elements, const int32 Layer, const ABRCharacter* Me, const ABRGameState* State, const float S, const FVector2D& Size) const
{
	using namespace ChaosImpactPaint;
	using namespace BRHudDetail;
	if (!State) { return; }
	const float CX = static_cast<float>(Size.X) * 0.5f;
	const FPainter Sheet{G, Elements, Layer}, Over{G, Elements, Layer + 3}, Word{G, Elements, Layer + 5};

	// ---- The circle's timetable: a ticket.
	const float W = 380.0f * S, H = 74.0f * S, X = CX - W * 0.5f, Y = 18.0f * S;
	const bool bClosing = State->bShrinking;
	Ticket(Sheet, X, Y, W, H, bClosing ? Red : Cream);
	const FLinearColor Ink2 = bClosing ? Cream : Sumi;
	const FString Label = bClosing ? TEXT("収縮中") : TEXT("次の収縮まで");
	Word.Text(Label, X + 26.0f * S, Y + 22.0f * S, HudFit(Label, 21.0f * S, 150.0f * S, TEXT("Black")), Ink2, ETextAlign::Left, TEXT("Black"));
	Word.Text(ClockText(State->GetStageSecondsLeft()), X + W - 26.0f * S, Y + 7.0f * S, 44.0f * S, Ink2, ETextAlign::Right, TEXT("Black"));
	// The stages as a string of little balls under it.
	for (int32 I = 0; I < BRRules::StageCount; ++I)
	{
		const FVector2D At(CX + (I - (BRRules::StageCount - 1) * 0.5f) * 30.0f * S, Y + H + 18.0f * S);
		Round(Sheet, At, 10.0f * S, Sumi);
		Round(Over, At, 7.0f * S, I < State->StageIndex ? Paper2 * 0.5f : I == State->StageIndex ? (bClosing ? Red : Mustard) : Cream);
	}

	// ---- How many are left: a big red stamp, turned as if thumped on.
	{
		const float R = 74.0f * S;
		const FVector2D C(Size.X - 40.0f * S - R, 34.0f * S + R);
		Round(Sheet, C + FVector2D(5.0, 7.0) * S, R, FLinearColor(0.0f, 0.0f, 0.0f, 0.4f));
		Round(HudAbove(Sheet, 1), C, R, Red);
		Over.Ring(C, R - 7.0f * S, Cream, 3.0f * S, 64);
		const FGeometry Turned = Tilted(G, static_cast<float>(C.X) - R, static_cast<float>(C.Y) - R, R * 2.0f, R * 2.0f, -8.0f);
		const FPainter Stamp{Turned, Elements, Layer + 5};
		Stamp.Text(TEXT("残り"), R, 18.0f * S, 21.0f * S, Cream, ETextAlign::Center, TEXT("Black"));
		const FString Count = FString::FromInt(State->AliveFighters);
		Stamp.Text(Count, R, 44.0f * S, HudFit(Count, 66.0f * S, R * 1.6f, TEXT("Black")), Cream, ETextAlign::Center, TEXT("Black"));
		if (State->TeamSize > 1)
		{
			const FString Teams = FString::Printf(TEXT("%d組"), State->AliveTeams);
			const FVector2D Badge = C + FVector2D(-R * 0.82, R * 0.72);
			Round(HudAbove(Over, 1), Badge, 31.0f * S, Sumi);
			HudAbove(Word, 1).Text(Teams, Badge.X, Badge.Y - 14.0f * S, HudFit(Teams, 22.0f * S, 52.0f * S, TEXT("Black")), Cream, ETextAlign::Center, TEXT("Black"));
		}
	}

	// ---- The way to the safe circle, under the ticket, when it matters.
	if (Me && State->Phase == EBRPhase::Play && FVector2D::Distance(FVector2D(Me->GetActorLocation()), State->ZoneTo) > State->ZoneToRadius)
	{
		const FVector2D Way = State->ZoneTo - FVector2D(Me->GetActorLocation());
		const FVector2D Dir = FVector2D(Way.Y, -Way.X).GetSafeNormal();
		const FVector2D C(CX - 30.0f * S, Y + H + 62.0f * S);
		const FString Metres = FString::Printf(TEXT("%dm"), FMath::RoundToInt((Way.Size() - State->ZoneToRadius) / 100.0f));
		Round(Sheet, C, 25.0f * S, Red);
		const FVector2D Across(-Dir.Y, Dir.X);
		Fill(Over, {C + Dir * 19.0f * S, C + Across * 11.0f * S - Dir * 9.0f * S, C - Across * 11.0f * S - Dir * 9.0f * S}, Cream);
		Word.Text(Metres, C.X + 36.0f * S, C.Y - 14.0f * S, 23.0f * S, Cream, ETextAlign::Left, TEXT("Black"), 2.0f * S, Sumi);
	}
}

void UBRHUD::PaintKillFeed(const FGeometry& G, FSlateWindowElementList& Elements, const int32 Layer, const ABRGameState* State, const float S, const FVector2D& Size) const
{
	using namespace ChaosImpactPaint;
	using namespace BRHudDetail;
	if (!State || !Owner.IsValid()) { return; }
	const double Now = State->GetServerWorldTimeSeconds();
	const int32 Mine = Owner->GetMyTeam();
	int32 Row = 0;
	// Newspaper headlines: black strips, a coloured edge for your own.
	for (int32 Index = State->KillLines.Num() - 1; Index >= 0 && Row < 5; --Index)
	{
		const ABRGameState::FKillLine& Line = State->KillLines[Index];
		const float Age = static_cast<float>(Now - Line.At);
		if (Age > 7.0f) { continue; }
		const float Fade = FMath::Clamp(FMath::Min(Age / 0.15f, (7.0f - Age) / 0.6f), 0.0f, 1.0f);
		const FString Words = Line.Killer.IsEmpty() ? FString::Printf(TEXT("%s　場外に倒れる"), *Line.Victim) : FString::Printf(TEXT("%s　撃破　%s"), *Line.Killer, *Line.Victim);
		const float Width = FMath::Min(HudMeasure(Words, 17.0f * S, TEXT("Black")) + 34.0f * S, 460.0f * S);
		const float X = static_cast<float>(Size.X) - 30.0f * S - Width, Y = 192.0f * S + Row * 36.0f * S;
		const FPainter Plate{G, Elements, Layer, Fade}, Word{G, Elements, Layer + 1, Fade};
		Plate.Box(X, Y, Width, 31.0f * S, FLinearColor(0.0f, 0.0f, 0.0f, 0.86f));
		Word.Box(X, Y, 7.0f * S, 31.0f * S, Line.VictimTeam == Mine ? Red : Line.KillerTeam == Mine ? Mate : Cream);
		Word.Text(Words, X + Width - 12.0f * S, Y + 4.0f * S, HudFit(Words, 17.0f * S, Width - 30.0f * S, TEXT("Black")), Cream, ETextAlign::Right, TEXT("Black"));
		++Row;
	}
}

void UBRHUD::PaintPrompt(const FGeometry& G, FSlateWindowElementList& Elements, const int32 Layer, const ABRCharacter* Me, const ABRGameState* State, const float S, const FVector2D& Size) const
{
	using namespace ChaosImpactPaint;
	using namespace BRHudDetail;
	if (!Me || !State) { return; }
	const FPainter P{G, Elements, Layer}, Word{G, Elements, Layer + 4};
	const float CX = static_cast<float>(Size.X) * 0.5f, Y = static_cast<float>(Size.Y) - 190.0f * S;
	if (Me->IsDowned())
	{
		const float Left = Me->GetBleedLeft();
		const FString Line = FString::Printf(TEXT("救助を待て　%.0f"), Left);
		const float W = 420.0f * S;
		Ticket(P, CX - W * 0.5f, Y - 26.0f * S, W, 66.0f * S, Red);
		Word.Text(Line, CX, Y - 12.0f * S, HudFit(Line, 34.0f * S, W - 60.0f * S, TEXT("Black")), Cream, ETextAlign::Center, TEXT("Black"));
		const float T = FMath::Clamp(Left / BRRules::BleedSeconds, 0.0f, 1.0f);
		P.Box(CX - W * 0.5f, Y + 44.0f * S, W, 10.0f * S, FLinearColor(0.0f, 0.0f, 0.0f, 0.65f));
		HudAbove(P, 1).Box(CX - W * 0.5f, Y + 44.0f * S, W * T, 10.0f * S, Cream);
		if (Me->ReviveEndsAt > 0.0)
		{
			const float R = 1.0f - FMath::Clamp(static_cast<float>(Me->ReviveEndsAt - State->GetServerWorldTimeSeconds()) / BRRules::ReviveSeconds, 0.0f, 1.0f);
			HudAbove(P, 1).Box(CX - W * 0.5f, Y + 58.0f * S, W * R, 10.0f * S, Mustard);
		}
		return;
	}
	FString Words;
	switch (Me->GetInteractKind())
	{
	case 1: Words = TEXT("味方を起こす"); break;
	case 2: Words = TEXT("乗る"); break;
	case 3: Words = TEXT("降りる"); break;
	default: return;
	}
	const float W = HudMeasure(Words, 25.0f * S, TEXT("Black")) + 92.0f * S;
	const float X = CX - W * 0.5f;
	Ticket(P, X, Y, W, 50.0f * S, Cream);
	Round(HudAbove(P, 3), FVector2D(X + 32.0f * S, Y + 25.0f * S), 17.0f * S, Sumi);
	Word.Text(TEXT("G"), X + 32.0f * S, Y + 11.0f * S, 21.0f * S, Cream, ETextAlign::Center, TEXT("Black"));
	Word.Text(Words, X + 62.0f * S, Y + 9.0f * S, 25.0f * S, Sumi, ETextAlign::Left, TEXT("Black"));
}

void UBRHUD::PaintZoneWarning(const FGeometry& G, FSlateWindowElementList& Elements, const int32 Layer, const ABRCharacter* Me, const ABRGameState* State, const FVector2D& Size) const
{
	using namespace ChaosImpactPaint;
	using namespace BRHudDetail;
	if (!Me || !State || State->Phase != EBRPhase::Play) { return; }
	FVector2D Centre;
	float Radius;
	State->GetZoneNow(Centre, Radius);
	if (FVector2D::Distance(FVector2D(Me->GetActorLocation()), Centre) <= Radius) { return; }
	// Hazard tape along the very edges of the picture: the play itself stays clear.
	const FPainter P{G, Elements, Layer};
	constexpr float Band = 18.0f, Stripe = 46.0f;
	const float Shift = FMath::Fmod(Clock * 60.0f, Stripe * 2.0f);
	for (const float Y : {0.0f, static_cast<float>(Size.Y) - Band})
	{
		P.Box(0.0f, Y, Size.X, Band, Sumi);
		for (float X = -Stripe * 2.0f + Shift; X < Size.X; X += Stripe * 2.0f)
		{
			Fill(HudAbove(P, 1), {FVector2D(X, Y + Band), FVector2D(X + Stripe, Y + Band), FVector2D(X + Stripe + Band, Y), FVector2D(X + Band, Y)}, Mustard);
		}
	}
}

void UBRHUD::PaintShip(const FGeometry& G, FSlateWindowElementList& Elements, const int32 Layer, const ABRCharacter* Me, const ABRGameState* State, const float S, const FVector2D& Size) const
{
	using namespace ChaosImpactPaint;
	using namespace BRHudDetail;
	if (!Me) { return; }
	const float CX = static_cast<float>(Size.X) * 0.5f;
	const FPainter P{G, Elements, Layer}, Over{G, Elements, Layer + 4}, Word{G, Elements, Layer + 6};
	if (Me->IsAboard())
	{
		// How far along the crossing it is: a strip with the airship travelling down it.
		const float W = 640.0f * S, X = CX - W * 0.5f, Y = 26.0f * S;
		Ticket(P, X, Y, W, 84.0f * S, Cream);
		const float Left = FMath::Max(static_cast<float>(State->PhaseEndsAt - State->GetServerWorldTimeSeconds()), 0.0f);
		Word.Text(TEXT("大統領飛行船"), X + 28.0f * S, Y + 9.0f * S, 21.0f * S, Red, ETextAlign::Left, TEXT("Black"));
		Word.Text(ClockText(Left), X + W - 28.0f * S, Y + 4.0f * S, 38.0f * S, Sumi, ETextAlign::Right, TEXT("Black"));
		const float BarX = X + 28.0f * S, BarW = W - 56.0f * S, BarY = Y + 58.0f * S;
		Over.Box(BarX, BarY, BarW, 6.0f * S, Sumi);
		HudAbove(Over, 1).Box(BarX, BarY, BarW * State->GetShipProgress(), 6.0f * S, Red);
		BallIcon(HudAbove(Over, 1), FVector2D(BarX + BarW * State->GetShipProgress(), BarY + 3.0f * S), 13.0f * S, Red, Cream);

		// The one thing to do.
		const float BW = 560.0f * S, BH = 96.0f * S, BX = CX - BW * 0.5f, BY = static_cast<float>(Size.Y) - 170.0f * S;
		const float Pulse = 0.5f + 0.5f * FMath::Sin(Clock * 4.0f);
		const bool bCanDrop = State->CanDrop();
		Ticket(P, BX, BY, BW, BH, bCanDrop ? FMath::Lerp(Red, FLinearColor(0.92f, 0.16f, 0.1f), Pulse) : Paper2);
		const float KeyR = 34.0f * S;
		const FVector2D KeyAt(BX + 62.0f * S, BY + BH * 0.5f);
		Round(HudAbove(Over, 1), KeyAt, KeyR, Cream);
		const FString Key = Owner.IsValid() && Owner->IsUsingGamepad() ? TEXT("A") : TEXT("空白");
		Word.Text(Key, KeyAt.X, KeyAt.Y - 19.0f * S, HudFit(Key, 28.0f * S, KeyR * 1.6f, TEXT("Black")), Sumi, ETextAlign::Center, TEXT("Black"));
		const FString Line = bCanDrop ? TEXT("飛び降りる") : TEXT("島の上まで待つ");
		Word.Text(Line, BX + BW * 0.5f + 40.0f * S, BY + 14.0f * S, HudFit(Line, 58.0f * S, BW - 170.0f * S, TEXT("Black")), Cream, ETextAlign::Center, TEXT("Black"));
		const FString Hint = TEXT("M：地図で降下先に印をつける");
		Word.Text(Hint, CX, BY + BH + 12.0f * S, HudFit(Hint, 21.0f * S, BW, TEXT("Black")), Cream, ETextAlign::Center, TEXT("Black"), 2.0f * S, Sumi);
		return;
	}
	if (Me->IsFreefalling())
	{
		// The height left, big, and how to steer.
		const FVector Here = Me->GetActorLocation();
		const int32 Metres = FMath::Max(0, FMath::RoundToInt((Here.Z - BRMap::HeightAt(Here.X, Here.Y)) / 100.0f));
		const float R = 86.0f * S;
		const FVector2D C(Size.X - 46.0f * S - R, Size.Y * 0.5f);
		Round(P, C + FVector2D(6.0, 8.0) * S, R, FLinearColor(0.0f, 0.0f, 0.0f, 0.4f));
		Round(HudAbove(P, 1), C, R, Sumi);
		const float Part = FMath::Clamp(Here.Z / BRRules::ShipHeight, 0.0f, 1.0f);
		Over.Arc(C, R - 8.0f * S, -90.0f, -90.0f + 360.0f * Part, Red, 9.0f * S);
		const FString Number = FString::Printf(TEXT("%d"), Metres);
		Word.Text(Number, C.X, C.Y - 38.0f * S, HudFit(Number, 54.0f * S, R * 1.5f, TEXT("Black")), Cream, ETextAlign::Center, TEXT("Black"));
		Word.Text(TEXT("メートル"), C.X, C.Y + 28.0f * S, 18.0f * S, Cream, ETextAlign::Center, TEXT("Black"));
		const FString Line = TEXT("移動キーで滑空　押している間は速く落ちる");
		const float W = HudMeasure(Line, 21.0f * S, TEXT("Black")) + 60.0f * S;
		Ticket(P, CX - W * 0.5f, static_cast<float>(Size.Y) - 110.0f * S, W, 52.0f * S, Cream);
		Word.Text(Line, CX, static_cast<float>(Size.Y) - 98.0f * S, 21.0f * S, Sumi, ETextAlign::Center, TEXT("Black"));
	}
}

void UBRHUD::PaintSpectating(const FGeometry& G, FSlateWindowElementList& Elements, const int32 Layer, const ABRCharacter* Me, const ABRGameState* State, const float S, const FVector2D& Size) const
{
	using namespace ChaosImpactPaint;
	using namespace BRHudDetail;
	if (!Me || !Me->IsEliminated() || !State || State->Phase != EBRPhase::Play || !Owner.IsValid()) { return; }
	const ABRCharacter* Watched = Cast<ABRCharacter>(Owner->GetViewTarget());
	const FPainter P{G, Elements, Layer}, Word{G, Elements, Layer + 4};
	const float W = 560.0f * S, H = 74.0f * S, X = static_cast<float>(Size.X) * 0.5f - W * 0.5f, Y = static_cast<float>(Size.Y) - 112.0f * S;
	Ticket(P, X, Y, W, H, Cream);
	const FString Name = Watched && Watched != Me ? Watched->FighterName : FString(TEXT("—"));
	Word.Text(TEXT("観戦"), X + 28.0f * S, Y + 19.0f * S, 28.0f * S, Red, ETextAlign::Left, TEXT("Black"));
	Word.Text(Name, X + 112.0f * S, Y + 15.0f * S, HudFit(Name, 32.0f * S, W - 310.0f * S, TEXT("Black")), Sumi, ETextAlign::Left, TEXT("Black"));
	Word.Text(TEXT("クリックで切り替え"), X + W - 26.0f * S, Y + 26.0f * S, 18.0f * S, Sumi, ETextAlign::Right, TEXT("Bold"));
}

void UBRHUD::PaintResults(const FGeometry& G, FSlateWindowElementList& Elements, const int32 Layer, const ABRGameState* State, const float S, const FVector2D& Size) const
{
	using namespace ChaosImpactPaint;
	using namespace BRHudDetail;
	if (!Owner.IsValid() || !State) { return; }
	const int32 Mine = Owner->GetMyTeam();
	const bool bWon = State->WinnerTeam == Mine;
	const FPainter Back{G, Elements, Layer}, Sheet{G, Elements, Layer + 1}, Ink1{G, Elements, Layer + 2}, Word{G, Elements, Layer + 6};
	// The next morning's front page.
	Back.Box(0.0f, 0.0f, Size.X, Size.Y, FLinearColor(0.0f, 0.0f, 0.0f, 0.75f));
	const float W = 1240.0f * S, X = static_cast<float>(Size.X) * 0.5f - W * 0.5f, Y = 30.0f * S, H = static_cast<float>(Size.Y) - 170.0f * S;
	Back.Box(X + 10.0f * S, Y + 12.0f * S, W, H, FLinearColor(0.0f, 0.0f, 0.0f, 0.5f));
	Sheet.Box(X, Y, W, H, Cream);
	Word.Text(TEXT("国民総避球新聞"), X + W * 0.5f, Y + 18.0f * S, 44.0f * S, Sumi, ETextAlign::Center, TEXT("Black"));
	Ink1.Box(X + 30.0f * S, Y + 84.0f * S, W - 60.0f * S, 4.0f * S, Sumi);
	Ink1.Box(X + 30.0f * S, Y + 92.0f * S, W - 60.0f * S, 1.5f * S, Sumi);
	const FString Issue = TEXT("号外　大統領杯・本日決着");
	Word.Text(Issue, X + 40.0f * S, Y + 100.0f * S, 21.0f * S, Sumi, ETextAlign::Left, TEXT("Bold"));
	const FString Headline = bWon ? TEXT("世界征服の切符、獲得！") : TEXT("無念の敗退");
	Word.Text(Headline, X + W * 0.5f, Y + 134.0f * S, HudFit(Headline, 96.0f * S, W - 80.0f * S, TEXT("Black")), bWon ? Red : Sumi, ETextAlign::Center, TEXT("Black"));
	Ink1.Box(X + 30.0f * S, Y + 276.0f * S, W - 60.0f * S, 3.0f * S, Sumi);

	const int32 Place = bWon ? 1 : FMath::Max(State->AliveTeams + 1, 2);
	const float StampR = 112.0f * S;
	const FVector2D StampAt(X + 190.0f * S, Y + 420.0f * S);
	BallIcon(Ink1, StampAt, StampR, bWon ? Red : Sumi, Cream);
	const FString PlaceText = FString::Printf(TEXT("%d位"), Place);
	Word.Text(PlaceText, StampAt.X, StampAt.Y - 60.0f * S, HudFit(PlaceText, 88.0f * S, StampR * 1.7f, TEXT("Black")), Cream, ETextAlign::Center, TEXT("Black"), 3.0f * S, bWon ? Red : Sumi);

	int32 Row = 0;
	for (int32 Index = 0; Index < State->Roster.Num() && Row < 4; ++Index)
	{
		const FBRRosterEntry& Entry = State->Roster[Index];
		if (Entry.Team != Mine) { continue; }
		const float RY = Y + 304.0f * S + Row * 64.0f * S;
		const float RX = X + 360.0f * S, RW = W - 410.0f * S;
		Word.Text(Entry.Name, RX, RY + 8.0f * S, HudFit(Entry.Name, 34.0f * S, RW - 180.0f * S, TEXT("Black")), Sumi, ETextAlign::Left, TEXT("Black"));
		const FString Kills = FString::Printf(TEXT("%d撃破"), Entry.Kills);
		Word.Text(Kills, RX + RW, RY + 8.0f * S, 34.0f * S, Red, ETextAlign::Right, TEXT("Black"));
		Ink1.Box(RX, RY + 58.0f * S, RW, 1.5f * S, Paper2 * 0.6f);
		++Row;
	}
	FBox2D Buttons[2];
	GetResultButtons(Size, S, Buttons);
	const TCHAR* const Labels[] = {GetWorld()->GetNetMode() == NM_Client ? TEXT("ホスト待ち") : TEXT("もう一度"), TEXT("本編へ戻る")};
	const FPainter Plate{G, Elements, Layer + 7}, Label{G, Elements, Layer + 11};
	for (int32 I = 0; I < 2; ++I)
	{
		const float BX = static_cast<float>(Buttons[I].Min.X), BY = static_cast<float>(Buttons[I].Min.Y);
		const float BW = static_cast<float>(Buttons[I].GetSize().X), BH = static_cast<float>(Buttons[I].GetSize().Y);
		const bool bHover = HoveredResult == I;
		Ticket(Plate, BX, BY, BW, BH, bHover ? Mustard : Red);
		Label.Text(Labels[I], BX + BW * 0.5f, BY + BH * 0.5f - 22.0f * S, HudFit(Labels[I], 32.0f * S, BW - 50.0f * S, TEXT("Black")), bHover ? Sumi : Cream, ETextAlign::Center, TEXT("Black"));
	}
}

void UBRHUD::PaintReticle(const FGeometry& G, FSlateWindowElementList& Elements, const int32 Layer, const ABRCharacter* Me, const float S) const
{
	using namespace ChaosImpactPaint;
	using namespace BRHudDetail;
	if (!Me || Me->IsEliminated() || !Owner.IsValid() || Owner->IsUsingGamepad() || !FSlateApplication::IsInitialized()) { return; }
	// (The cursor is in desktop coordinates; the cached geometry is too.)
	const FVector2D At = FVector2D(GetCachedGeometry().AbsoluteToLocal(FSlateApplication::Get().GetCursorPos()));
	const FPainter P{G, Elements, Layer}, Over{G, Elements, Layer + 1};
	const float R = 15.0f * S;
	P.Ring(At, R, Sumi, 6.0f * S);
	Over.Ring(At, R, Cream, 2.5f * S);
	Over.Disc(At, 2.5f * S, Red);
	for (int32 I = 0; I < 3; ++I)
	{
		const float A = FMath::DegreesToRadians(-90.0f + I * 120.0f);
		const FVector2D Way(FMath::Cos(A), FMath::Sin(A));
		P.Line(At + Way * (R + 3.0f * S), At + Way * (R + 11.0f * S), Sumi, 6.0f * S);
		Over.Line(At + Way * (R + 3.0f * S), At + Way * (R + 11.0f * S), Cream, 2.5f * S);
	}
	if (Me->IsChargingThrow())
	{
		Over.Arc(At, R + 6.0f * S, -90.0f, -90.0f + 360.0f * Me->GetThrowChargeAlpha(), Mustard, 4.0f * S);
	}
}

void UBRHUD::PaintThrowGuide(const FGeometry& G, FSlateWindowElementList& Elements, const int32 Layer, const ABRCharacter* Me, const float S, const FVector2D& Size) const
{
	using namespace ChaosImpactPaint;
	using namespace BRHudDetail;
	if (!Me || Me->IsEliminated() || !Me->IsChargingThrow() || !Owner.IsValid() || Owner->GetPawn() != Me) { return; }
	const float Charge = FMath::Clamp(Me->GetThrowChargeAlpha(), 0.0f, 1.0f);
	// The same colours for the same charge as everywhere else in the game.
	const FLinearColor Low(0.02f, 0.22f, 0.92f), Middle(1.0f, 0.58f, 0.0f), Full(0.96f, 0.035f, 0.08f);
	const FLinearColor Heat = Charge < 0.65f ? FLinearColor::LerpUsingHSV(Low, Middle, Charge / 0.65f) : FLinearColor::LerpUsingHSV(Middle, Full, (Charge - 0.65f) / 0.35f);

	// ---- Where it will go: the arrow on the ground, of the same length and shape as the other modes, filling as it charges.
	FVector2D A, B;
	const FVector From = Me->GetAimGuideStartWorldLocation();
	if (!Me->IsShoulderViewActive()
		&& UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(Owner.Get(), From, A, true)
		&& UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(Owner.Get(), From + Me->GetAimDirection().GetSafeNormal2D() * Me->GetAimGuideLength(), B, true))
	{
		const FPainter Edge{G, Elements, Layer}, Core{G, Elements, Layer + 1}, Lit{G, Elements, Layer + 2};
		const FVector2D Way = (B - A).GetSafeNormal(), Side(-Way.Y, Way.X);
		const float Head = 42.0f * S, Along = Head * FMath::Cos(FMath::DegreesToRadians(35.0f)), Across = Head * FMath::Sin(FMath::DegreesToRadians(35.0f));
		const FVector2D Wing1 = B - Way * Along + Side * Across, Wing2 = B - Way * Along - Side * Across;
		for (const FVector2D& Tip : {Wing1, Wing2})
		{
			Edge.Line(B, Tip, Sumi, 13.0f * S);
			Core.Line(B, Tip, Cream, 7.0f * S);
		}
		Edge.Line(A, B, Sumi, 12.0f * S);
		Core.Line(A, B, Cream, 6.0f * S);
		Lit.Line(A, FMath::Lerp(A, B, static_cast<double>(Charge)), Heat, 6.0f * S);
		if (Charge >= 1.0f)
		{
			Lit.Line(B, Wing1, Heat, 7.0f * S);
			Lit.Line(B, Wing2, Heat, 7.0f * S);
		}
	}

	// ---- How hard: a ticket that fills, under the prompt line.
	const float W = 420.0f * S, H = 46.0f * S, X = static_cast<float>(Size.X) * 0.5f - W * 0.5f, Y = static_cast<float>(Size.Y) - 128.0f * S;
	const float InX = X + 24.0f * S, InY = Y + 9.0f * S, InW = W - 48.0f * S, InH = H - 18.0f * S;
	const FPainter Plate{G, Elements, Layer}, Trough{G, Elements, Layer + 3}, Bar{G, Elements, Layer + 4}, Mark{G, Elements, Layer + 5}, Word{G, Elements, Layer + 6};
	Ticket(Plate, X, Y, W, H, Sumi);
	Trough.Box(InX, InY, InW, InH, FLinearColor(1.0f, 1.0f, 1.0f, 0.13f));
	Bar.Box(InX, InY, InW * Charge, InH, Heat);
	for (const float Part : {0.25f, 0.5f, 0.75f}) { Mark.Box(InX + InW * Part - 1.0f * S, InY, 2.0f * S, InH, FLinearColor(0.0f, 0.0f, 0.0f, 0.55f)); }
	if (Charge >= 1.0f && FMath::Fmod(Clock * 6.0f, 1.0f) < 0.5f) { Mark.Outline(InX, InY, InW, InH, Cream, 3.0f * S); }
	const FString Line = FString::Printf(TEXT("チャージ  %d%%"), FMath::RoundToInt(Charge * 100.0f));
	Word.Text(Line, X + W * 0.5f, Y + H * 0.5f - 16.0f * S, HudFit(Line, 23.0f * S, InW - 20.0f * S, TEXT("Black")), Cream, ETextAlign::Center, TEXT("Black"), 3.0f * S, Sumi);
}

void UBRHUD::PaintOverheads(const FGeometry& G, FSlateWindowElementList& Elements, const int32 Layer, const ABRCharacter* Me, const float S) const
{
	using namespace ChaosImpactPaint;
	using namespace BRHudDetail;
	if (!Owner.IsValid()) { return; }
	const int32 Mine = Owner->GetMyTeam();
	const FPainter P{G, Elements, Layer}, Word{G, Elements, Layer + 1};
	for (TActorIterator<ABRCharacter> It(GetWorld()); It; ++It)
	{
		const ABRCharacter* Other = *It;
		if (Other == Me || Other->IsEliminated() || Other->IsFlying() || Other->TeamId != Mine) { continue; }
		FVector2D At;
		if (!UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(Owner.Get(), Other->GetActorLocation() + FVector(0.0f, 0.0f, 130.0f), At, true)) { continue; }
		const float Size = HudFit(Other->FighterName, 16.0f * S, 180.0f * S, TEXT("Black"));
		const float W = HudMeasure(Other->FighterName, Size, TEXT("Black")) + 16.0f * S;
		P.Box(At.X - W * 0.5f, At.Y - 26.0f * S, W, 24.0f * S, Other->IsDowned() ? Red : Mate);
		Word.Text(Other->FighterName, At.X, At.Y - 25.0f * S, Size, Cream, ETextAlign::Center, TEXT("Black"));
	}
}

int32 UBRHUD::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, const int32 LayerId, const FWidgetStyle& InWidgetStyle, const bool bParentEnabled) const
{
	const int32 Base = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	if (!Owner.IsValid()) { return Base; }
	const ABRGameState* State = GetWorld() ? GetWorld()->GetGameState<ABRGameState>() : nullptr;
	if (!State || State->Phase == EBRPhase::Title) { return Base; }
	const FVector2D Size = FVector2D(AllottedGeometry.GetLocalSize());
	const float S = static_cast<float>(FMath::Max(0.01, FMath::Min(Size.X / 1600.0, Size.Y / 900.0))) * ChaosImpactSettings::GetHudSizeFactor();
	const ABRCharacter* Me = Cast<ABRCharacter>(Owner->GetPawn());
	int32 Layer = Base + 1;
	if (Me && Me->IsFlying())
	{
		PaintShip(AllottedGeometry, OutDrawElements, Layer, Me, State, S, Size);
		PaintMinimap(AllottedGeometry, OutDrawElements, Layer + 10, Me, State, S);
		if (Owner->IsMapOpen()) { PaintBigMap(AllottedGeometry, OutDrawElements, Layer + 24, Me, State, S, Size); }
		return Layer + 44;
	}
	PaintZoneWarning(AllottedGeometry, OutDrawElements, Layer, Me, State, Size);
	PaintOverheads(AllottedGeometry, OutDrawElements, Layer + 2, Me, S);
	PaintVitals(AllottedGeometry, OutDrawElements, Layer + 4, Me, State, S);
	PaintTeam(AllottedGeometry, OutDrawElements, Layer + 4, State, S);
	PaintMinimap(AllottedGeometry, OutDrawElements, Layer + 4, Me, State, S);
	PaintTopBar(AllottedGeometry, OutDrawElements, Layer + 16, Me, State, S, Size);
	PaintKillFeed(AllottedGeometry, OutDrawElements, Layer + 16, State, S, Size);
	PaintPrompt(AllottedGeometry, OutDrawElements, Layer + 16, Me, State, S, Size);
	PaintSpectating(AllottedGeometry, OutDrawElements, Layer + 16, Me, State, S, Size);
	PaintThrowGuide(AllottedGeometry, OutDrawElements, Layer + 20, Me, S, Size);
	PaintReticle(AllottedGeometry, OutDrawElements, Layer + 26, Me, S);
	Layer += 30;
	if (Owner->IsMapOpen() && State->Phase != EBRPhase::Over)
	{
		PaintBigMap(AllottedGeometry, OutDrawElements, Layer, Me, State, S, Size);
		Layer += 14;
	}
	if (State->Phase == EBRPhase::Over)
	{
		PaintResults(AllottedGeometry, OutDrawElements, Layer, State, S, Size);
		Layer += 14;
	}
	return Layer + 4;
}

// ---- The title: a propaganda poster, the island beyond its torn edge ----

void UBRTitleWidget::Init(ABRPlayerController* InOwner)
{
	bOnline = FParse::Param(FCommandLine::Get(), TEXT("CIBROnlinePanel"));
	Owner = InOwner;
	SetIsFocusable(true);
	// (User widgets let the mouse through by default.)
	SetVisibility(ESlateVisibility::Visible);
	if (const UChaosImpactLoadoutSubsystem* Loadouts = UChaosImpactLoadoutSubsystem::Get(InOwner))
	{
		CharacterChoice = ChaosImpactRoster::ClampIndex(Loadouts->GetLoadout(0).Character);
	}
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Parameters.ObjectFlags |= RF_Transient;
	Preview = InOwner->GetWorld()->SpawnActor<AChaosImpactCharacterPreview>(AChaosImpactCharacterPreview::StaticClass(), FVector(0.0f, -180000.0f, 40000.0f), FRotator::ZeroRotator, Parameters);
}

void UBRTitleWidget::UpdatePreview(const float DeltaSeconds)
{
	if (!Preview) { return; }
	if (PreviewShown != CharacterChoice)
	{
		PreviewShown = CharacterChoice;
		Preview->ShowLoadout(CharacterChoice, 0, FLinearColor(0.78f, 0.045f, 0.05f));
	}
	Preview->SetFacing(FMath::Sin(Clock * 0.7f) * 18.0f, 0.0f);
	Preview->Animate(DeltaSeconds);
	Preview->FilmNow();
	if (PreviewBrush.GetResourceObject() != Preview->GetPicture())
	{
		PreviewBrush.SetResourceObject(Preview->GetPicture());
		PreviewBrush.ImageSize = FVector2D(AChaosImpactCharacterPreview::PictureWidth, AChaosImpactCharacterPreview::PictureHeight);
		PreviewBrush.DrawAs = ESlateBrushDrawType::Image;
	}
}

void UBRTitleWidget::StepCharacter(const int32 Step)
{
	CharacterChoice = (CharacterChoice + Step + ChaosImpactRoster::Num()) % ChaosImpactRoster::Num();
	if (Owner.IsValid()) { Owner->ServerSetCharacter(CharacterChoice); }
}

FBox2D UBRTitleWidget::ArrowRect(const int32 Index) const
{
	return Index == 0 ? FBox2D(FVector2D(1096.0, 330.0), FVector2D(1170.0, 430.0)) : FBox2D(FVector2D(1500.0, 330.0), FVector2D(1574.0, 430.0));
}

void UBRTitleWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	Clock += InDeltaTime;
	UpdatePreview(InDeltaTime);
}

FBox2D UBRTitleWidget::TileRect(const int32 Index) const
{
	return FBox2D(FVector2D(70.0 + Index * 170.0, 452.0), FVector2D(226.0 + Index * 170.0, 640.0));
}

FBox2D UBRTitleWidget::ButtonRect(const int32 Index) const
{
	// The big ball to start; then how to play and back.
	if (Index == 0) { return FBox2D(FVector2D(742.0, 500.0), FVector2D(1026.0, 784.0)); }
	const int32 Row = (Index - 1) / 2, Column = (Index - 1) % 2;
	return FBox2D(FVector2D(70.0 + Column * 336.0, 664.0 + Row * 78.0), FVector2D(390.0 + Column * 336.0, 732.0 + Row * 78.0));
}

int32 UBRTitleWidget::HitAt(const FGeometry& Geometry, const FVector2D& ScreenPosition) const
{
	const FVector2D Size = FVector2D(Geometry.GetLocalSize());
	const double Scale = FMath::Min(Size.X / 1600.0, Size.Y / 900.0);
	const FVector2D Offset = (Size - FVector2D(1600.0, 900.0) * Scale) * 0.5;
	const FVector2D At = (FVector2D(Geometry.AbsoluteToLocal(ScreenPosition)) - Offset) / Scale;
	for (int32 I = 0; I < 4; ++I) { if (TileRect(I).IsInside(At)) { return I; } }
	for (int32 I = 0; I < 5; ++I) { if (ButtonRect(I).IsInside(At)) { return 4 + I; } }
	return INDEX_NONE;
}

void UBRTitleWidget::Choose()
{
	if (!Owner.IsValid()) { return; }
	if (bHowTo) { bHowTo = false; return; }
	if (bOnline) { ChooseOnline(); return; }
	const bool bGuest = IsGuest();
	if (Focused >= 0 && Focused < 4)
	{
		if (!bGuest) { TeamChoice = Focused; Owner->ServerSetTeamSize(TeamChoice + 1); }
		return;
	}
	if (Focused == 4) { if (!bGuest) { Owner->ServerStartMatch(TeamChoice + 1, CharacterChoice); } }
	else if (Focused == 5) { bHowTo = true; }
	else if (Focused == 6) { Back(); }
	else if (Focused == 7 && !bGuest) { bOnline = true; OnlineFocus = 4; }
	else if (Focused == 8 && !bGuest)
	{
		if (ABRGameMode* Mode = GetWorld()->GetAuthGameMode<ABRGameMode>())
		{
			const int32 Now = GetGameInstance()->GetLocalPlayers().Num();
			Mode->SetLocalPlayers(Now % 4 + 1);
		}
	}
}

bool UBRTitleWidget::IsGuest() const
{
	return GetWorld() && GetWorld()->GetNetMode() == NM_Client;
}

FString UBRTitleWidget::OnlineWord() const
{
	return FString::Printf(TEXT("%d%d%d%d"), Digits[0], Digits[1], Digits[2], Digits[3]);
}

FBox2D UBRTitleWidget::OnlineRect(const int32 Index) const
{
	if (Index < 4) { return FBox2D(FVector2D(480.0 + Index * 170.0, 330.0), FVector2D(620.0 + Index * 170.0, 520.0)); }
	return FBox2D(FVector2D(400.0 + (Index - 4) * 276.0, 590.0), FVector2D(656.0 + (Index - 4) * 276.0, 668.0));
}

void UBRTitleWidget::ChooseOnline()
{
	UBROnlineSubsystem* Online = UBROnlineSubsystem::Get(this);
	if (!Online) { return; }
	if (OnlineFocus == 4) { Online->Host(GetWorld(), OnlineWord()); }
	else if (OnlineFocus == 5) { Online->StartSearch(OnlineWord()); }
	else if (OnlineFocus == 6)
	{
		if (Online->GetState() == EBRNetState::Searching) { Online->Stop(); }
		bOnline = false;
	}
}

void UBRTitleWidget::Back()
{
	if (bHowTo) { bHowTo = false; return; }
	if (bOnline) { OnlineFocus = 6; ChooseOnline(); return; }
	if (Owner.IsValid()) { Owner->LeaveToMainTitle(); }
}

FReply UBRTitleWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Enter || Key == EKeys::SpaceBar || Key == EKeys::Gamepad_FaceButton_Bottom) { Choose(); return FReply::Handled(); }
	if (!bHowTo && (Key == EKeys::Q || Key == EKeys::Gamepad_LeftShoulder)) { StepCharacter(-1); return FReply::Handled(); }
	if (!bHowTo && (Key == EKeys::E || Key == EKeys::Gamepad_RightShoulder)) { StepCharacter(1); return FReply::Handled(); }
	if (Key == EKeys::Escape || Key == EKeys::BackSpace || Key == EKeys::Gamepad_FaceButton_Right) { Back(); return FReply::Handled(); }
	if (bHowTo) { return FReply::Handled(); }
	if (bOnline)
	{
		// Digits straight from the keys, or round with up and down.
		for (int32 Digit = 0; Digit <= 9; ++Digit)
		{
			static const FKey Numbers[] = {EKeys::Zero, EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine};
			static const FKey Pad[] = {EKeys::NumPadZero, EKeys::NumPadOne, EKeys::NumPadTwo, EKeys::NumPadThree, EKeys::NumPadFour, EKeys::NumPadFive, EKeys::NumPadSix, EKeys::NumPadSeven, EKeys::NumPadEight, EKeys::NumPadNine};
			if (Key == Numbers[Digit] || Key == Pad[Digit])
			{
				const int32 At = OnlineFocus < 4 ? OnlineFocus : 0;
				Digits[At] = Digit;
				OnlineFocus = At + 1;
				return FReply::Handled();
			}
		}
		if (Key == EKeys::Left || Key == EKeys::Gamepad_DPad_Left || Key == EKeys::Gamepad_LeftStick_Left) { OnlineFocus = (OnlineFocus + 6) % 7; }
		else if (Key == EKeys::Right || Key == EKeys::Gamepad_DPad_Right || Key == EKeys::Gamepad_LeftStick_Right) { OnlineFocus = (OnlineFocus + 1) % 7; }
		else if (OnlineFocus < 4 && (Key == EKeys::Up || Key == EKeys::Gamepad_DPad_Up || Key == EKeys::Gamepad_LeftStick_Up)) { Digits[OnlineFocus] = (Digits[OnlineFocus] + 1) % 10; }
		else if (OnlineFocus < 4 && (Key == EKeys::Down || Key == EKeys::Gamepad_DPad_Down || Key == EKeys::Gamepad_LeftStick_Down)) { Digits[OnlineFocus] = (Digits[OnlineFocus] + 9) % 10; }
		return FReply::Handled();
	}
	const bool bLeft = Key == EKeys::Left || Key == EKeys::A || Key == EKeys::Gamepad_DPad_Left || Key == EKeys::Gamepad_LeftStick_Left;
	const bool bRight = Key == EKeys::Right || Key == EKeys::D || Key == EKeys::Gamepad_DPad_Right || Key == EKeys::Gamepad_LeftStick_Right;
	const bool bUp = Key == EKeys::Up || Key == EKeys::W || Key == EKeys::Gamepad_DPad_Up || Key == EKeys::Gamepad_LeftStick_Up;
	const bool bDown = Key == EKeys::Down || Key == EKeys::S || Key == EKeys::Gamepad_DPad_Down || Key == EKeys::Gamepad_LeftStick_Down;
	if (bLeft || bRight || bUp || bDown)
	{
		// Every direction leads somewhere from everywhere. Moving along the tickets is choosing them.
		const auto PickTicket = [&](const int32 Ticket)
		{
			Focused = Ticket;
			if (!IsGuest() && TeamChoice != Ticket && Owner.IsValid())
			{
				TeamChoice = Ticket;
				Owner->ServerSetTeamSize(TeamChoice + 1);
			}
		};
		if (Focused < 4)
		{
			if (bLeft) { if (Focused == 0) { Focused = 4; } else { PickTicket(Focused - 1); } }
			else if (bRight) { if (Focused == 3) { Focused = 4; } else { PickTicket(Focused + 1); } }
			else if (bUp) { Focused = Focused < 2 ? 7 : 8; }
			else { Focused = Focused < 2 ? 5 : 6; }
		}
		else if (Focused == 4)
		{
			Focused = bDown ? 6 : TeamChoice;
		}
		else
		{
			// The four buttons: two across, two down.
			const int32 Column = (Focused - 5) % 2, Row = (Focused - 5) / 2;
			if (bLeft) { Focused = Column == 1 ? Focused - 1 : 4; }
			else if (bRight) { Focused = Column == 0 ? Focused + 1 : 4; }
			else if (bUp) { Focused = Row == 1 ? Focused - 2 : TeamChoice; }
			else { Focused = Row == 0 ? Focused + 2 : TeamChoice; }
		}
		return FReply::Handled();
	}
	return FReply::Handled();
}

FReply UBRTitleWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bOnline)
	{
		const FVector2D Size = FVector2D(InGeometry.GetLocalSize());
		const double Scale = FMath::Min(Size.X / 1600.0, Size.Y / 900.0);
		const FVector2D At = (FVector2D(InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition())) - (Size - FVector2D(1600.0, 900.0) * Scale) * 0.5) / Scale;
		for (int32 I = 0; I < 7; ++I) { if (OnlineRect(I).IsInside(At)) { OnlineFocus = I; } }
		return FReply::Handled();
	}
	const int32 Hit = HitAt(InGeometry, InMouseEvent.GetScreenSpacePosition());
	if (Hit != INDEX_NONE) { Focused = Hit; }
	return FReply::Handled();
}

FReply UBRTitleWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bHowTo) { bHowTo = false; return FReply::Handled(); }
	if (bOnline)
	{
		const FVector2D Size = FVector2D(InGeometry.GetLocalSize());
		const double Scale = FMath::Min(Size.X / 1600.0, Size.Y / 900.0);
		const FVector2D At = (FVector2D(InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition())) - (Size - FVector2D(1600.0, 900.0) * Scale) * 0.5) / Scale;
		for (int32 I = 0; I < 7; ++I)
		{
			if (!OnlineRect(I).IsInside(At)) { continue; }
			OnlineFocus = I;
			if (I < 4) { Digits[I] = (Digits[I] + (At.Y < OnlineRect(I).GetCenter().Y ? 1 : 9)) % 10; }
			else { ChooseOnline(); }
		}
		return FReply::Handled();
	}
	{
		const FVector2D Local = FVector2D(InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition()));
		const FVector2D Size = FVector2D(InGeometry.GetLocalSize());
		const double Scale = FMath::Min(Size.X / 1600.0, Size.Y / 900.0);
		const FVector2D At = (Local - (Size - FVector2D(1600.0, 900.0) * Scale) * 0.5) / Scale;
		for (int32 Arrow = 0; Arrow < 2; ++Arrow)
		{
			if (ArrowRect(Arrow).IsInside(At)) { StepCharacter(Arrow == 0 ? -1 : 1); return FReply::Handled(); }
		}
	}
	const int32 Hit = HitAt(InGeometry, InMouseEvent.GetScreenSpacePosition());
	if (Hit != INDEX_NONE) { Focused = Hit; Choose(); }
	return FReply::Handled();
}

void UBRTitleWidget::PaintOnline(const FGeometry& D, FSlateWindowElementList& Elements, const int32 Layer) const
{
	using namespace ChaosImpactPaint;
	using namespace BRHudDetail;
	const FPainter Shade{D, Elements, Layer}, Sheet{D, Elements, Layer + 1}, Mark{D, Elements, Layer + 2}, Words{D, Elements, Layer + 4};
	Shade.Box(-1400.0f, -200.0f, 4400.0f, 1300.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.78f));
	Sheet.Box(350.0f, 150.0f, 900.0f, 620.0f, Cream);
	Mark.Box(350.0f, 150.0f, 900.0f, 74.0f, Sumi);
	Words.Text(TEXT("合言葉で集合"), 800.0f, 158.0f, 44.0f, Cream, ETextAlign::Center, TEXT("Black"));
	const FString Line = TEXT("同じ4桁の数字を選んだ人が同じ部屋に入れます（最大8人）");
	Words.Text(Line, 800.0f, 258.0f, HudFit(Line, 26.0f, 820.0f, TEXT("Black")), Sumi, ETextAlign::Center, TEXT("Black"));
	for (int32 I = 0; I < 4; ++I)
	{
		const FBox2D R = OnlineRect(I);
		const float X = static_cast<float>(R.Min.X), Y = static_cast<float>(R.Min.Y), W = static_cast<float>(R.GetSize().X), H = static_cast<float>(R.GetSize().Y);
		const bool bFocus = OnlineFocus == I;
		Mark.Box(X, Y, W, H, bFocus ? Red : Sumi);
		Words.Text(FString::FromInt(Digits[I]), X + W * 0.5f, Y + 30.0f, 100.0f, Cream, ETextAlign::Center, TEXT("Black"));
		for (const float Dir : {-1.0f, 1.0f})
		{
			const FVector2D C(X + W * 0.5f, Dir < 0.0f ? Y + 20.0f : Y + H - 20.0f);
			Fill(Words, {C + FVector2D(0.0, Dir * 12.0), C + FVector2D(-14.0, -Dir * 8.0), C + FVector2D(14.0, -Dir * 8.0)}, Cream);
		}
	}
	const TCHAR* const Labels[] = {TEXT("部屋を作る"), TEXT("探して入る"), TEXT("閉じる")};
	for (int32 I = 0; I < 3; ++I)
	{
		const FBox2D R = OnlineRect(4 + I);
		const float X = static_cast<float>(R.Min.X), Y = static_cast<float>(R.Min.Y), W = static_cast<float>(R.GetSize().X), H = static_cast<float>(R.GetSize().Y);
		Ticket(Mark, X, Y, W, H, OnlineFocus == 4 + I ? Red : Sumi);
		Words.Text(Labels[I], X + W * 0.5f, Y + 16.0f, HudFit(Labels[I], 34.0f, W - 40.0f, TEXT("Black")), Cream, ETextAlign::Center, TEXT("Black"));
	}
	if (const UBROnlineSubsystem* Online = UBROnlineSubsystem::Get(this); Online && !Online->GetNotice().IsEmpty())
	{
		Words.Text(Online->GetNotice(), 800.0f, 700.0f, HudFit(Online->GetNotice(), 26.0f, 820.0f, TEXT("Black")), Red, ETextAlign::Center, TEXT("Black"));
	}
}

int32 UBRTitleWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, const int32 LayerId, const FWidgetStyle& InWidgetStyle, const bool bParentEnabled) const
{
	using namespace ChaosImpactPaint;
	using namespace BRHudDetail;
	const int32 Base = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	const FVector2D Size = FVector2D(AllottedGeometry.GetLocalSize());
	const float Scale = static_cast<float>(FMath::Min(Size.X / 1600.0, Size.Y / 900.0));
	const FVector2D Offset = (Size - FVector2D(1600.0, 900.0) * Scale) * 0.5;
	const FGeometry D = AllottedGeometry.MakeChild(FVector2f(1600.0f, 900.0f), FSlateLayoutTransform(Scale, FVector2f(Offset)));
	const FPainter Full{AllottedGeometry, OutDrawElements, Base + 1};
	const FPainter Sun{D, OutDrawElements, Base + 2}, Under{D, OutDrawElements, Base + 3}, P{D, OutDrawElements, Base + 5}, Up{D, OutDrawElements, Base + 7}, Text{D, OutDrawElements, Base + 9}, Top{D, OutDrawElements, Base + 11};
	const float Bob = FMath::Sin(Clock * 3.2f);

	// ---- The poster's paper, torn along its right edge so the island shows.
	{
		FRandomStream Tear(55);
		TArray<FVector2D> Edge;
		for (int32 I = 0; I <= 18; ++I)
		{
			Edge.Add(FVector2D(Offset.X + (1050.0 + Tear.FRandRange(-16.0f, 16.0f) - I * 3.0) * Scale, Size.Y * I / 18.0));
		}
		// (A torn edge is not convex: strips joined one under the other.)
		for (int32 I = 0; I + 1 < Edge.Num(); ++I)
		{
			Fill(Full, {FVector2D(0.0, Edge[I].Y), Edge[I], Edge[I + 1], FVector2D(0.0, Edge[I + 1].Y)}, Cream);
		}
	}
	// The rising red sun behind the name, and its printed rays.
	{
		const FVector2D C(860.0, 170.0);
		for (int32 Ray = 0; Ray < 14; ++Ray)
		{
			const float A0 = FMath::DegreesToRadians(Ray * 360.0f / 14.0f + Clock * 1.5f), A1 = A0 + FMath::DegreesToRadians(11.0f);
			// (No further than the paper's edge.)
			const auto Reach = [](const float A) { return FMath::Cos(A) > 0.05f ? FMath::Min(900.0f, 160.0f / FMath::Cos(A)) : 900.0f; };
			TArray<FVector2D> Wedge{C, C + FVector2D(FMath::Cos(A0), FMath::Sin(A0)) * Reach(A0), C + FVector2D(FMath::Cos(A1), FMath::Sin(A1)) * Reach(A1)};
			Wedge = HudClip(Wedge, 0.0, true);
			Wedge = HudClip(Wedge, 440.0, false);
			if (Wedge.Num() >= 3) { Fill(Sun, Wedge, FLinearColor(0.84f, 0.72f, 0.48f)); }
		}
		Round(Under, C, 216.0f, Red);
		Halftone(Under, 0.0f, 690.0f, 34, 10, 32.0f, 12.0f, FLinearColor(0.82f, 0.7f, 0.46f));
	}

	Text.Text(TEXT("大統領ドッチ・ボール主催"), 70.0f, 34.0f, 26.0f, Red, ETextAlign::Left, TEXT("Black"));
	Text.Text(TEXT("国民総避球"), 70.0f, 70.0f, 66.0f, Sumi, ETextAlign::Left, TEXT("Black"));
	Text.Text(TEXT("DEADBALL"), 66.0f, 148.0f, 94.0f, Sumi, ETextAlign::Left, TEXT("Black"));
	Text.Text(TEXT("ROYALE"), 66.0f, 256.0f, 94.0f, Red, ETextAlign::Left, TEXT("Black"));
	const FString Tag = TEXT("全60人。最後に立っていた者だけが世界征服に加われる。");
	Text.Text(Tag, 70.0f, 408.0f, HudFit(Tag, 24.0f, 960.0f, TEXT("Black")), Sumi, ETextAlign::Left, TEXT("Black"));

	// ---- The entry tickets: one for each team size.
	const TCHAR* const Names[] = {TEXT("ソロ"), TEXT("デュオ"), TEXT("トリオ"), TEXT("スクワッド")};
	for (int32 I = 0; I < 4; ++I)
	{
		const FBox2D R = TileRect(I);
		const float X = static_cast<float>(R.Min.X), Y = static_cast<float>(R.Min.Y), W = static_cast<float>(R.GetSize().X), H = static_cast<float>(R.GetSize().Y);
		const bool bChosen = TeamChoice == I;
		const bool bFocus = Focused == I;
		const float Tilt = (I % 2 ? 1.6f : -1.6f) * (bChosen ? 1.0f : 0.5f);
		const float Lift = bChosen ? -12.0f : 0.0f;
		const FGeometry T = Tilted(D, X, Y + Lift, W, H, Tilt);
		const FPainter TUnder{T, OutDrawElements, Base + 3}, TP{T, OutDrawElements, Base + 5}, TUp{T, OutDrawElements, Base + 7}, TW{T, OutDrawElements, Base + 10};
		const FLinearColor Face = bChosen ? Red : FLinearColor(0.97f, 0.93f, 0.8f);
		const FLinearColor Ink2 = bChosen ? Cream : Sumi;
		TUnder.Box(6.0f, 8.0f, W, H, FLinearColor(0.0f, 0.0f, 0.0f, 0.35f));
		if (bFocus) { HudAbove(TUnder, 1).Box(-5.0f, -5.0f, W + 10.0f, H + 10.0f, Sumi); }
		TP.Box(0.0f, 0.0f, W, H, Face);
		// The stub line, and the bites out of it.
		for (float DotX = 8.0f; DotX < W - 4.0f; DotX += 13.0f) { TUp.Box(DotX, 128.0f, 7.0f, 2.0f, Ink2); }
		Round(TUp, FVector2D(0.0, 129.0), 11.0f, FLinearColor(0.84f, 0.74f, 0.54f));
		Round(TUp, FVector2D(W, 129.0), 11.0f, FLinearColor(0.84f, 0.74f, 0.54f));
		// As many balls as it has players.
		const float Spread = 32.0f;
		for (int32 Figure = 0; Figure <= I; ++Figure)
		{
			const float FX = W * 0.5f + (Figure - I * 0.5f) * Spread;
			BallIcon(TUp, FVector2D(FX, 42.0f + (bChosen ? FMath::Sin(Clock * 5.0f + Figure) * 3.0f : 0.0f)), 15.0f, bChosen ? Cream : Red, bChosen ? Red : Cream);
		}
		const FString Count = FString::Printf(TEXT("%d人1組"), I + 1);
		TW.Text(Count, W * 0.5f, 74.0f, 22.0f, Ink2, ETextAlign::Center, TEXT("Black"));
		TW.Text(Names[I], W * 0.5f, 140.0f, HudFit(Names[I], 30.0f, W - 22.0f, TEXT("Black")), Ink2, ETextAlign::Center, TEXT("Black"));
	}
	// ---- The start: a great red ball.
	{
		const FBox2D R = ButtonRect(0);
		const FVector2D C = R.GetCenter();
		const bool bFocus = Focused == 4;
		const float Radius = 134.0f + (bFocus ? 5.0f + Bob * 4.0f : 0.0f);
		if (bFocus) { Round(Under, C, Radius + 11.0f, Sumi); }
		BallIcon(P, C, Radius, Red, Cream);
		const FGeometry Turned = Tilted(D, static_cast<float>(C.X) - 140.0f, static_cast<float>(C.Y) - 70.0f, 280.0f, 140.0f, -7.0f);
		FPainter{Turned, OutDrawElements, Base + 12}.Text(IsGuest() ? TEXT("待機") : TEXT("出撃"), 140.0f, 6.0f, 96.0f, Cream, ETextAlign::Center, TEXT("Black"), 4.0f, Red);
	}
	const bool bGuest = IsGuest();
	const UBROnlineSubsystem* Online = UBROnlineSubsystem::Get(this);
	const bool bHosting = Online && Online->GetState() == EBRNetState::Hosting;
	const int32 OnScreen = GetGameInstance() ? GetGameInstance()->GetLocalPlayers().Num() : 1;
	const FString Small[] = {TEXT("あそびかた"), TEXT("本編へ戻る"), bHosting ? FString::Printf(TEXT("合言葉 %s"), *Online->GetWord()) : FString(TEXT("オンライン")), FString::Printf(TEXT("画面分割 %d人"), OnScreen)};
	for (int32 I = 0; I < 4; ++I)
	{
		const FBox2D R = ButtonRect(I + 1);
		const float X = static_cast<float>(R.Min.X), Y = static_cast<float>(R.Min.Y), W = static_cast<float>(R.GetSize().X), H = static_cast<float>(R.GetSize().Y);
		const bool bFocus = Focused == 5 + I;
		const bool bOff = bGuest && I >= 2;
		Under.Box(X + 5.0f, Y + 6.0f, W, H, FLinearColor(0.0f, 0.0f, 0.0f, 0.35f));
		P.Box(X, Y, W, H, bOff ? Paper2 : bFocus ? Red : Sumi);
		Text.Text(Small[I], X + W * 0.5f, Y + 13.0f, HudFit(Small[I], 32.0f, W - 24.0f, TEXT("Black")), Cream, ETextAlign::Center, TEXT("Black"));
	}
	const int32 InRoom = GetWorld() && GetWorld()->GetGameState() ? GetWorld()->GetGameState()->PlayerArray.Num() : 1;
	const FString Keys = bGuest ? FString::Printf(TEXT("部屋にいる人 %d人　ホストの出撃を待っています　Q / E キャラ変更"), InRoom)
		: bHosting ? FString::Printf(TEXT("部屋にいる人 %d / %d人　そろったら出撃　Q / E キャラ変更"), InRoom, BRRules::MaxHumans)
		: FString(TEXT("← → ↑ ↓ えらぶ　決定 はい　Q / E キャラ変更"));
	Text.Text(Keys, 72.0f, 826.0f, HudFit(Keys, 21.0f, 660.0f, TEXT("Black")), Sumi, ETextAlign::Left, TEXT("Black"));

	// ---- The competitor's pass, pinned up over the island.
	{
		constexpr float CardW = 330.0f, CardH = 540.0f;
		const FGeometry Card = Tilted(D, 1170.0f, 110.0f, CardW, CardH, 2.5f);
		const FPainter C0{Card, OutDrawElements, Base + 3}, C1{Card, OutDrawElements, Base + 5}, C2{Card, OutDrawElements, Base + 7}, C3{Card, OutDrawElements, Base + 10};
		C0.Box(9.0f, 12.0f, CardW, CardH, FLinearColor(0.0f, 0.0f, 0.0f, 0.5f));
		C1.Box(0.0f, 0.0f, CardW, CardH, Cream);
		C2.Box(0.0f, 0.0f, CardW, 54.0f, Sumi);
		C3.Text(TEXT("出場証"), CardW * 0.5f, 6.0f, 34.0f, Cream, ETextAlign::Center, TEXT("Black"));
		C2.Box(29.0f, 74.0f, CardW - 58.0f, 348.0f, FLinearColor(0.84f, 0.74f, 0.54f));
		if (PreviewBrush.GetResourceObject())
		{
			FSlateDrawElement::MakeBox(OutDrawElements, Base + 9, Card.ToPaintGeometry(FVector2f(264.0f, 348.0f), FSlateLayoutTransform(FVector2f(33.0f, 74.0f))), &PreviewBrush, ESlateDrawEffect::None, FLinearColor::White);
		}
		const FString Name = ChaosImpactRoster::Get(CharacterChoice).Name;
		C3.Text(Name, CardW * 0.5f, 432.0f, HudFit(Name, 36.0f, CardW - 36.0f, TEXT("Black")), Sumi, ETextAlign::Center, TEXT("Black"));
		C3.Box(26.0f, 486.0f, CardW - 52.0f, 2.0f, Sumi);
		C3.Text(TEXT("デットボール合衆国 発行"), CardW * 0.5f, 494.0f, 19.0f, Sumi, ETextAlign::Center, TEXT("Bold"));
		// The approving stamp, a little crooked.
		const FGeometry Stamp = Tilted(Card, CardW - 112.0f, 300.0f, 100.0f, 100.0f, -16.0f);
		const FPainter StampP{Stamp, OutDrawElements, Base + 12};
		StampP.Ring(FVector2D(50.0, 50.0), 44.0f, WithAlpha(Red, 0.82f), 5.0f, 40);
		StampP.Text(TEXT("認可"), 50.0f, 24.0f, 34.0f, WithAlpha(Red, 0.82f), ETextAlign::Center, TEXT("Black"));
		for (int32 Arrow = 0; Arrow < 2; ++Arrow)
		{
			const FBox2D R = ArrowRect(Arrow);
			const FVector2D C = R.GetCenter();
			Round(Top, C, 36.0f, Red);
			const float Dir = Arrow == 0 ? -1.0f : 1.0f;
			Fill(HudAbove(Top, 1), {C + FVector2D(Dir * 20.0f, 0.0), C + FVector2D(-Dir * 12.0f, -18.0), C + FVector2D(-Dir * 12.0f, 18.0)}, Cream);
		}
	}

	if (bOnline) { PaintOnline(D, OutDrawElements, Base + 16); }
	if (bHowTo)
	{
		const FPainter Shade{D, OutDrawElements, Base + 16};
		Shade.Box(-1400.0f, -200.0f, 4400.0f, 1300.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.78f));
		const FGeometry Page = Tilted(D, 280.0f, 90.0f, 1040.0f, 720.0f, -1.2f);
		const FPainter Sheet{Page, OutDrawElements, Base + 17}, Mark{Page, OutDrawElements, Base + 18}, Words{Page, OutDrawElements, Base + 20};
		Sheet.Box(10.0f, 12.0f, 1040.0f, 720.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.5f));
		Mark.Box(0.0f, 0.0f, 1040.0f, 720.0f, Cream);
		Words.Text(TEXT("国民のみなさんへ"), 48.0f, 26.0f, 56.0f, Sumi, ETextAlign::Left, TEXT("Black"));
		HudAbove(Mark, 1).Box(48.0f, 104.0f, 944.0f, 5.0f, Red);
		const TCHAR* const Lines[] = {
			TEXT("HPは100。防具（ゲージの外周）が先に削れる。"),
			TEXT("投げたボールは床に残る。敵にも拾われるので外すと危ない。"),
			TEXT("赤い線の外にいるとHPが減り続ける。線は少しずつ縮む。"),
			TEXT("チーム戦ではHPが尽きるとダウン。味方に起こしてもらえる。"),
			TEXT("G：スケボー（2人）と車（4人）に乗る／味方を起こす"),
			TEXT("H：回復　　M：大きな地図（ホイールで拡大）"),
			TEXT("地図に出るのは自分のチームだけ。敵は自分の目で探すこと。")};
		for (int32 I = 0; I < UE_ARRAY_COUNT(Lines); ++I)
		{
			Round(HudAbove(Mark, 1), FVector2D(66.0, 150.0 + I * 72.0), 11.0f, Red);
			Words.Text(Lines[I], 92.0f, 132.0f + I * 72.0f, HudFit(Lines[I], 30.0f, 900.0f, TEXT("Black")), Sumi, ETextAlign::Left, TEXT("Black"));
		}
		Words.Text(TEXT("クリックまたは決定で閉じる"), 992.0f, 668.0f, 22.0f, Red, ETextAlign::Right, TEXT("Black"));
	}
	return Base + 24;
}
