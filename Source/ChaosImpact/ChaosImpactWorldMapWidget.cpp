#include "ChaosImpactWorldMapWidget.h"

#include "ChaosImpactPaint.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactSoloStages.h"
#include "ChaosImpactWorldMap.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "GameFramework/Pawn.h"

int32 UChaosImpactWorldMapWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, const int32 LayerId, const FWidgetStyle& InWidgetStyle, const bool bParentEnabled) const
{
	using namespace ChaosImpactPaint;

	const int32 BaseLayer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	const AChaosImpactWorldMap* World = Map.Get();
	const UChaosImpactSoloProgress* Progress = World ? World->GetProgress() : nullptr;
	const AChaosImpactPlayerController* PC = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
	const FVector2f Size = AllottedGeometry.GetLocalSize();
	if (!World || !Progress || Size.X < 1.0f || Size.Y < 1.0f || (PC && !PC->IsGameplayActive()))
	{
		return BaseLayer;
	}
	const float S = FMath::Clamp(FMath::Min(Size.X / 1600.0f, Size.Y / 900.0f), 0.45f, 1.5f);
	const float Time = static_cast<float>(FMath::Fmod(FPlatformTime::Seconds(), 3600.0));
	const bool bDark = World->MapIndex == 2;
	const auto At = [](const float X, const float Y) { return FVector2D(X, Y); };
	const FHudFill Plate = bDark ? MakeFill(FLinearColor(0.16f, 0.05f, 0.12f, 0.95f), FLinearColor(0.04f, 0.01f, 0.05f, 0.95f)) : NightFill;
	const FHudFill Accent = bDark ? FireFill : IceFill;

	// ---- The map's name, and how much of it is cleared.
	{
		const FGeometry Corner = MakeAnchor(AllottedGeometry, 0.0f, 0.0f, S);
		const FPainter Low{Corner, OutDrawElements, BaseLayer};
		HudShard(Low, {At(30, 34), At(470, 22), At(452, 132), At(44, 146)}, Plate, 3.5f, FVector2D(6.0, 7.0));
		HudShard(HudAbove(Low), {At(30, 34), At(160, 28), At(150, 52), At(34, 56)}, Accent, 2.5f, FVector2D(3.0, 4.0));
		const FPainter Top{Corner, OutDrawElements, BaseLayer + 6};
		Top.Text(FString::Printf(TEXT("WORLD %d"), World->MapIndex), 96.0f, 30.0f, 15.0f, Navy, ETextAlign::Center, TEXT("BlackItalic"));
		HudText3D(Top, bDark ? TEXT("デットボール合衆国") : TEXT("占領された世界"), 52.0f, 62.0f, 30.0f, Paper);
		const int32 Total = bDark ? 15 : 15;
		const int32 Done = Progress->CountCleared(World->MapIndex);
		Top.Text(FString::Printf(TEXT("クリア %d / %d"), Done, Total), 56.0f, 108.0f, 15.0f, bDark ? FLinearColor(1.0f, 0.6f, 0.55f) : FLinearColor(0.6f, 0.85f, 1.0f),
			ETextAlign::Left, TEXT("Black"));
		// A bar of the cleared stages, one notch each.
		for (int32 I = 0; I < Total; ++I)
		{
			const float X = 196.0f + I * 16.0f;
			HudShard(HudAbove(Low, 3), {At(X + 3, 112), At(X + 14, 112), At(X + 11, 126), At(X, 126)},
				I < Done ? Accent : SolidFill(FLinearColor(0.1f, 0.12f, 0.2f, 0.9f)), 1.5f, FVector2D::ZeroVector);
		}
		if (Progress->bUnlockAll)
		{
			Top.Text(TEXT("開発用：全部ひらいています"), 60.0f, 152.0f, 13.0f, Gold, ETextAlign::Left, TEXT("Black"), 2.0f, Ink);
		}
	}

	// The fog clearing shows nothing over the map (the camera and the fog say it).

	// ---- The card of the stage underfoot, sliding in from the right.
	const float CardIn = World->IsRevealing() ? 0.0f
		: EaseOut(World->GetCardAlpha()) * (1.0f - FMath::Clamp(World->GetOverviewBlend() * 2.0f, 0.0f, 1.0f));
	const FChaosImpactSoloStageInfo* Info = CardNode ? ChaosImpactSolo::FindStage(CardNode) : nullptr;
	if (Info && CardIn > 0.01f)
	{
		const bool bGate = Info->Kind == EChaosImpactSoloStageKind::Gate;
		const bool bCleared = Progress->IsCleared(Info->Number);
		const bool bAvailable = Info->Number == ChaosImpactSolo::GateToMap1 || Progress->IsAvailable(Info->Number);
		const bool bReady = bGate || !ChaosImpactSolo::GetLevelPackage(Info->Number).IsEmpty();
		const bool bBoss = Info->Kind == EChaosImpactSoloStageKind::Boss || Info->Kind == EChaosImpactSoloStageKind::Story;
		const FGeometry Corner = MakeAnchor(AllottedGeometry, Size.X + (1.0f - CardIn) * 700.0f * S, Size.Y, S);
		const FPainter Low{Corner, OutDrawElements, BaseLayer + 2, CardIn};
		constexpr float L = -660.0f;
		constexpr float R = -36.0f;
		constexpr float T = -336.0f;
		constexpr float B = -76.0f;
		HudShard(Low, {At(L + 40, T + 10), At(R, T - 6), At(R - 12, B), At(L, B + 8)}, Plate, 4.0f, FVector2D(8.0, 9.0));
		// The stage's kind along the top: a strip of colour.
		const FHudFill KindFill = bGate ? MakeFill(FLinearColor(0.85f, 0.6f, 1.0f), FLinearColor(0.35f, 0.12f, 0.75f))
			: bBoss ? FireFill : Info->Kind == EChaosImpactSoloStageKind::Challenge ? MakeFill(FLinearColor(1.0f, 0.75f, 1.0f), FLinearColor(0.55f, 0.15f, 0.8f))
			: IceFill;
		HudShard(HudAbove(Low), {At(L + 150, T + 2), At(L + 420, T - 3), At(L + 412, T + 22), At(L + 146, T + 26)}, KindFill, 2.5f, FVector2D(3.0, 4.0));
		const FString Kind = bGate ? TEXT("GATE") : Info->Kind == EChaosImpactSoloStageKind::Boss ? TEXT("BOSS · 四天王")
			: Info->Kind == EChaosImpactSoloStageKind::Story ? TEXT("BOSS") : Info->Kind == EChaosImpactSoloStageKind::Challenge ? TEXT("CHALLENGE")
			: TEXT("STAGE");
		const FPainter Top{Corner, OutDrawElements, BaseLayer + 8, CardIn};
		Top.Text(Kind, L + 282, T + 1, 14.0f, Navy, ETextAlign::Center, TEXT("BlackItalic"));
		// The number in a burst: fire waiting to be played, ice once cleared, grey while locked.
		const FHudFill NumberFill = bGate ? KindFill : bCleared ? IceFill : bAvailable ? FireFill
			: MakeFill(FLinearColor(0.6f, 0.62f, 0.68f), FLinearColor(0.25f, 0.27f, 0.32f));
		const FVector2D Badge(L + 92, T + 118);
		HudBurst(HudAbove(Low, 3), Badge, 58.0f, 80.0f, 9, NumberFill, Time * (bAvailable && !bCleared ? 30.0f : 6.0f), FVector2D(1.0, 1.0), 4.0f,
			FVector2D(6.0, 7.0), static_cast<float>(Info->Number));
		HudText3D(FPainter{Corner, OutDrawElements, BaseLayer + 12, CardIn}, bGate ? TEXT("門") : FString::FromInt(Info->Number),
			static_cast<float>(Badge.X), static_cast<float>(Badge.Y) - 40.0f, 58.0f, Paper, ETextAlign::Center);
		// The region and the name (none yet for a stage not made).
		Top.Text(Info->Area, L + 186, T + 44, 15.0f, bDark ? FLinearColor(1.0f, 0.6f, 0.55f) : FLinearColor(0.6f, 0.85f, 1.0f), ETextAlign::Left, TEXT("Black"));
		if (bReady)
		{
			HudText3D(Top, Info->Name, L + 184, T + 66, 34.0f, Paper);
		}
		// What the confirm button does (or why it does nothing).
		FString Prompt;
		FString Note;
		FLinearColor NoteColor = Muted;
		if (bGate && !bAvailable)
		{
			int32 Left = 0;
			for (const int32 Required : Info->Requires)
			{
				Left += Progress->IsCleared(Required) ? 0 : 1;
			}
			Note = FString::Printf(TEXT("封印中：このマップのステージを全部クリアすると開く（あと %d）"), Left);
		}
		else if (bGate)
		{
			Prompt = Info->Number == ChaosImpactSolo::GateToMap2 ? TEXT("門をくぐる → デットボール合衆国") : TEXT("門をくぐる → 占領された世界");
		}
		else if (!bAvailable)
		{
			TArray<FString> Need;
			for (const int32 Required : Info->Requires)
			{
				if (!Progress->IsCleared(Required))
				{
					if (const FChaosImpactSoloStageInfo* Other = ChaosImpactSolo::FindStage(Required))
					{
						Need.Add(ChaosImpactSolo::GetLevelPackage(Required).IsEmpty() ? FString::Printf(TEXT("ステージ%d"), Other->Number)
							: FString(Other->Name));
					}
				}
			}
			Note = FString::Printf(TEXT("封印中：%s をクリアすると開く"), *FString::Join(Need, TEXT("・")));
		}
		else if (!bReady)
		{
#if !UE_BUILD_SHIPPING
			Note = TEXT("準備中（開発用：C でクリア扱い）");
#else
			Note = TEXT("準備中");
#endif
		}
		else
		{
			Prompt = TEXT("出撃！");
		}
		if (bCleared && !bGate)
		{
			Note = TEXT("★ クリア済み");
			NoteColor = Gold;
		}
		if (!Note.IsEmpty())
		{
			Top.Text(Note, L + 186, T + 166, 16.0f, NoteColor, ETextAlign::Left, TEXT("Black"), 2.0f, Navy);
		}
		if (!Prompt.IsEmpty())
		{
			// A key cap, then the action.
			const FString Key = PC && PC->IsUsingGamepad() ? TEXT("A") : TEXT("決定");
			const float KeyWidth = HudMeasure(Key, 16.0f) + 26.0f;
			const float Y = B - 50.0f;
			const float Pulse = 0.5f + 0.5f * FMath::Sin(Time * 5.0f);
			HudShard(HudAbove(Low, 6), {At(L + 186, Y), At(L + 186 + KeyWidth, Y - 3), At(L + 182 + KeyWidth, Y + 30), At(L + 182, Y + 32)},
				bAvailable ? MakeFill(FMath::Lerp(FireFill.Top, FLinearColor::White, Pulse * 0.4f), FireFill.Bottom) : SolidFill(Muted), 2.5f,
				FVector2D(3.0, 4.0));
			FPainter{Corner, OutDrawElements, BaseLayer + 16, CardIn}.Text(Key, L + 184 + KeyWidth * 0.5f, Y + 3, 16.0f, Navy,
				ETextAlign::Center, TEXT("Black"));
			HudText3D(FPainter{Corner, OutDrawElements, BaseLayer + 16, CardIn}, Prompt, L + 198 + KeyWidth, Y, 19.0f,
				bAvailable ? Paper : Muted);
		}
	}

	// ---- The controls, bottom left.
	const float Above = World->GetOverviewBlend();
	if (Above > 0.01f && PC)
	{
		// The whole map: each stage out of the fog tagged with its number (gold when cleared), and where the player is.
		const float In = EaseOut(FMath::Clamp((Above - 0.4f) / 0.6f, 0.0f, 1.0f));
		const FGeometry Whole = MakeAnchor(AllottedGeometry, 0.0f, 0.0f, 1.0f);
		for (const int32 Id : World->GetNodeIds())
		{
			if (World->GetNodeReveal(Id) < 0.99f)
			{
				continue;
			}
			FVector2D Spot;
			if (!UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PC, World->GetNodeLocation(Id) + FVector(0.0f, 0.0f, 120.0f), Spot, false))
			{
				continue;
			}
			const FChaosImpactSoloStageInfo* Node = ChaosImpactSolo::FindStage(Id);
			const bool bGateNode = Node && Node->Kind == EChaosImpactSoloStageKind::Gate;
			const bool bDone = Progress->IsCleared(Id);
			const bool bOpen = Id == ChaosImpactSolo::GateToMap1 || Progress->IsAvailable(Id);
			const FHudFill Tag = bGateNode ? MakeFill(FLinearColor(0.85f, 0.6f, 1.0f), FLinearColor(0.35f, 0.12f, 0.75f))
				: bDone ? MakeFill(FLinearColor(1.0f, 0.92f, 0.55f), FLinearColor(0.9f, 0.55f, 0.05f)) : bOpen ? FireFill
				: MakeFill(FLinearColor(0.6f, 0.62f, 0.68f), FLinearColor(0.25f, 0.27f, 0.32f));
			const float R = 22.0f * S;
			const FPainter Pin{Whole, OutDrawElements, BaseLayer + 30, In};
			HudShard(Pin, {Spot + FVector2D(2.0, -R), Spot + FVector2D(R, 0.0), Spot + FVector2D(-2.0, R), Spot + FVector2D(-R, 0.0)}, Tag, 2.5f,
				FVector2D(3.0, 4.0));
			FPainter{Whole, OutDrawElements, BaseLayer + 36, In}.Text(bGateNode ? TEXT("門") : FString::FromInt(Id),
				static_cast<float>(Spot.X), static_cast<float>(Spot.Y) - 11.0f * S, 15.0f * S, Navy, ETextAlign::Center, TEXT("Black"));
		}
		if (const APawn* Me = PC->GetPawn())
		{
			FVector2D Spot;
			if (UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PC, Me->GetActorLocation() + FVector(0.0f, 0.0f, 150.0f), Spot, false))
			{
				const float Bob = FMath::Sin(Time * 5.0f) * 4.0f * S;
				const FVector2D Tip = Spot + FVector2D(0.0, -34.0 * S + Bob);
				const FPainter Mark{Whole, OutDrawElements, BaseLayer + 40, In};
				HudShard(Mark, {Tip + FVector2D(-34.0 * S, -40.0 * S), Tip + FVector2D(34.0 * S, -40.0 * S), Tip}, IceFill, 3.0f, FVector2D(3.0, 4.0));
				FPainter{Whole, OutDrawElements, BaseLayer + 46, In}.Text(TEXT("YOU"), static_cast<float>(Tip.X),
					static_cast<float>(Tip.Y) - 37.0f * S, 13.0f * S, Navy, ETextAlign::Center, TEXT("BlackItalic"));
			}
		}
		const FGeometry TopCenter = MakeAnchor(AllottedGeometry, Size.X * 0.5f, 0.0f, S);
		HudText3D(FPainter{TopCenter, OutDrawElements, BaseLayer + 50, In}, TEXT("全体マップ"), 0.0f, 40.0f, 40.0f, Paper, ETextAlign::Center);
	}
	if (!World->IsRevealing())
	{
		const FGeometry Corner = MakeAnchor(AllottedGeometry, 0.0f, Size.Y, S);
		const bool bPad = PC && PC->IsUsingGamepad();
		const FString Hint = World->IsOverview()
			? (bPad ? TEXT("Y / B：もどる") : TEXT("M / ホイール：もどる　クリック：そのステージへ歩く"))
			: bPad ? TEXT("左スティック：道を歩く　A：出撃　Y：全体マップ　START：メニュー")
			: TEXT("移動：道を歩く　クリック：そこまで歩く　決定：出撃　M / ホイール：全体マップ　Esc：メニュー");
#if !UE_BUILD_SHIPPING
		if (!World->IsOverview() && World->GetFocusedNode())
		{
			FPainter{Corner, OutDrawElements, BaseLayer + 4}.Text(TEXT("開発用　C：このステージをクリア扱い"), 36.0f, -72.0f, 14.0f, Gold,
				ETextAlign::Left, TEXT("Black"), 2.0f, Navy);
		}
#endif
		FPainter{Corner, OutDrawElements, BaseLayer + 4}.Text(Hint, 36.0f, -46.0f, 15.0f, FLinearColor(0.92f, 0.94f, 1.0f, 0.9f),
			ETextAlign::Left, TEXT("Black"), 2.0f, Navy);
	}
	return BaseLayer + 60;
}
