#include "ChaosImpactPaintStudio.h"

#include "ChaosImpactCharacterPreview.h"
#include "ChaosImpactCustomColour.h"
#include "ChaosImpactPaint.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactPuppetComponent.h"
#include "ChaosImpactSettings.h"
#include "ChaosImpactSfx.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Input/Events.h"
#include "TextureResource.h"

namespace
{
	using namespace ChaosImpactPaint;
	using StudioTool = UChaosImpactPaintStudio::ETool;

	// Layout in the 1600 x 900 design space: the tools on the left, the model in the middle, the colours on the right.
	constexpr float StViewLeft = 312.0f;
	constexpr float StViewTop = 110.0f;
	constexpr float StViewWidth = 880.0f;
	constexpr float StViewHeight = 716.0f;
	/** The view's picture is filmed a little finer than it is shown. */
	constexpr int32 StPictureWidth = 1100;
	constexpr int32 StPictureHeight = 895;
	constexpr float StLeft = 32.0f;
	constexpr float StLeftWidth = 260.0f;
	constexpr float StRight = 1212.0f;
	constexpr float StRightWidth = 356.0f;
	constexpr float StSwatch = 23.0f;
	constexpr float StSwatchStep = 27.0f;
	constexpr float StRecent = 25.0f;
	constexpr float StRecentStep = 29.0f;
	constexpr float StBarTop = 842.0f;
	constexpr int32 StSliders = 3;
	const TCHAR* const StSliderNames[StSliders] = {TEXT("太さ"), TEXT("かたさ"), TEXT("こさ")};
	const TCHAR* const StToolNames[UChaosImpactPaintStudio::ToolCount] = {TEXT("ペン"), TEXT("消しゴム"), TEXT("バケツ"), TEXT("スポイト")};
	const TCHAR* const StToolKeys[UChaosImpactPaintStudio::ToolCount] = {TEXT("B"), TEXT("E"), TEXT("G"), TEXT("I")};
	const TCHAR* const StToolTips[UChaosImpactPaintStudio::ToolCount] = {
		TEXT("なぞったところに色をぬる"), TEXT("ぬった色をけす"), TEXT("つながった部分をまるごとぬる"), TEXT("ふれた色をひろう")};
	const TCHAR* const StButtonNames[4] = {TEXT("もどす"), TEXT("やりなおす"), TEXT("全部けす"), TEXT("視点リセット")};
	const TCHAR* const StLeaveNames[3] = {TEXT("完了してもどる"), TEXT("描いたものをすててもどる"), TEXT("つづける")};
	const FLinearColor StBack(0.008f, 0.012f, 0.03f, 1.0f);
	const FLinearColor StCard(0.03f, 0.07f, 0.18f, 1.0f);
	const FLinearColor StCardOn(0.06f, 0.2f, 0.5f, 1.0f);
	constexpr float MinZoom = 0.6f;
	constexpr float MaxZoom = 8.0f;

	bool StIsAny(const FKey& Key, std::initializer_list<FKey> Keys)
	{
		for (const FKey& Candidate : Keys)
		{
			if (Key == Candidate)
			{
				return true;
			}
		}
		return false;
	}

	void StRound(const FPainter& P, const FBox2D& R, const float Radius, const FLinearColor& Color)
	{
		const FVector2D S = R.GetSize();
		RoundedBox(P, static_cast<float>(R.Min.X), static_cast<float>(R.Min.Y), static_cast<float>(S.X), static_cast<float>(S.Y), Radius, Color);
	}

	void StFrame(const FPainter& P, const FBox2D& R, const FLinearColor& Color, const float Width)
	{
		const FVector2D S = R.GetSize();
		P.Outline(static_cast<float>(R.Min.X), static_cast<float>(R.Min.Y), static_cast<float>(S.X), static_cast<float>(S.Y), Color, Width);
	}

	FBox2D StGrow(const FBox2D& R, const float By)
	{
		return FBox2D(R.Min - FVector2D(By), R.Max + FVector2D(By));
	}

	/** A smooth run of colours across R (left to right, or top to bottom). */
	void StGradient(const FPainter& P, const FBox2D& R, const TArray<FLinearColor>& Colours, const bool bAcross = true)
	{
		const FVector2D Size = R.GetSize();
		if (Colours.Num() < 2 || Size.X <= 0.0 || Size.Y <= 0.0)
		{
			return;
		}
		TArray<FSlateGradientStop> Stops;
		for (int32 Index = 0; Index < Colours.Num(); ++Index)
		{
			const float T = Index / static_cast<float>(Colours.Num() - 1);
			Stops.Add(FSlateGradientStop(FVector2f(Size.X * T, Size.Y * T), WithAlpha(Colours[Index], P.Alpha * Colours[Index].A)));
		}
		FSlateDrawElement::MakeGradient(P.Elements, P.Layer,
			P.Geometry.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(R.Min))),
			MoveTemp(Stops), bAcross ? Orient_Vertical : Orient_Horizontal);
	}

	/** A label that never runs out of its width. */
	void StText(const FPainter& P, const FString& Value, const float X, const float Y, const float Size, const float Width, const FLinearColor& Color,
		const ETextAlign Align = ETextAlign::Left, const FName Face = TEXT("Bold"))
	{
		P.Text(Value, X, Y, HudFit(Value, Size, Width, Face), Color, Align, Face);
	}

	/** The tools' small pictures. */
	void StToolIcon(const FPainter& P, const StudioTool Tool, const FVector2D& C, const FLinearColor& Line, const FLinearColor& Tint)
	{
		switch (Tool)
		{
		case StudioTool::Pen:
			P.Line(C + FVector2D(-15.0, 15.0), C + FVector2D(11.0, -11.0), Line, 7.0f);
			P.Line(C + FVector2D(11.0, -11.0), C + FVector2D(16.0, -16.0), WithAlpha(Line, 0.6f), 7.0f);
			P.Disc(C + FVector2D(-15.0, 15.0), 4.5f, Tint);
			break;
		case StudioTool::Eraser:
			P.Line(C + FVector2D(-12.0, 12.0), C + FVector2D(12.0, -12.0), FLinearColor(1.0f, 0.55f, 0.65f, 1.0f), 16.0f);
			P.Line(C + FVector2D(-12.0, 12.0), C + FVector2D(-4.0, 4.0), Line, 16.0f);
			P.Line(C + FVector2D(-18.0, 18.0), C + FVector2D(8.0, 18.0), WithAlpha(Line, 0.5f), 2.0f);
			break;
		case StudioTool::Fill:
			P.Line(C + FVector2D(-13.0, -6.0), C + FVector2D(-9.0, 14.0), Line, 3.0f);
			P.Line(C + FVector2D(-9.0, 14.0), C + FVector2D(9.0, 14.0), Line, 3.0f);
			P.Line(C + FVector2D(9.0, 14.0), C + FVector2D(13.0, -6.0), Line, 3.0f);
			P.Line(C + FVector2D(-13.0, -6.0), C + FVector2D(13.0, -6.0), Line, 3.0f);
			P.Box(static_cast<float>(C.X) - 9.0f, static_cast<float>(C.Y) + 2.0f, 18.0f, 10.0f, Tint);
			P.Disc(C + FVector2D(17.0, 6.0), 4.0f, Tint);
			break;
		case StudioTool::Picker:
			P.Line(C + FVector2D(-12.0, 12.0), C + FVector2D(8.0, -8.0), Line, 4.0f);
			P.Disc(C + FVector2D(11.0, -11.0), 6.5f, Line);
			P.Disc(C + FVector2D(-13.0, 13.0), 4.5f, Tint);
			break;
		}
	}

	FLinearColor StShown(const FColor& Colour)
	{
		return FLinearColor(Colour);
	}
}

// ---- Opening and closing -----------------------------------------------------------------------------------------------

void UChaosImpactPaintStudio::Open(AChaosImpactPlayerController* InController, AChaosImpactCharacterPreview* InPreview,
	const TArray<UChaosImpactPaintLayer*>& InLayers, const FColor& StartColour, const float InStartYaw)
{
	Controller = InController;
	Preview = InPreview;
	LayerObjects.Reset();
	Layers = InLayers;
	Before.Reset();
	for (UChaosImpactPaintLayer* Layer : Layers)
	{
		LayerObjects.Add(Layer);
		Before.Add(Layer ? Layer->GetPixels() : TArray<FColor>());
	}
	bOpen = true;
	Result = EResult::None;
	bChanged = false;
	bCancelArmed = false;
	bLeaveMenu = false;
	bPenDown = false;
	bPadA = false;
	Dragging = INDEX_NONE;
	bTurning = false;
	bSliding = false;
	LeftStick = RightStick = FVector2D::ZeroVector;
	ZoomIn = ZoomOut = 0.0f;
	OpenedAt = FPlatformTime::Seconds();
	NoticeAt = -100.0;
	Tool = ETool::Pen;
	ToolBeforePicker = ETool::Pen;
	Brush = FChaosImpactBrush();
	Brush.Radius = SizeToRadius(0.32f);
	Brush.Hardness = 0.75f;
	Brush.Opacity = 1.0f;
	Brush.bMirror = false;
	SetColour(StartColour, false);
	// The camera starts where the model was seen from on the カラーエディット (the model turned by StartYaw, seen from +Y).
	StartYaw = 90.0f - InStartYaw;
	if (Preview)
	{
		Preview->BeginPaintView(StPictureWidth, StPictureHeight);
		Surface.Build(Preview->GetModel());
		PictureBrush = FSlateBrush();
		PictureBrush.SetResourceObject(Preview->GetPicture());
		PictureBrush.ImageSize = FVector2D(StPictureWidth, StPictureHeight);
		PictureBrush.DrawAs = ESlateBrushDrawType::Image;
	}
	ResetView();
	const FBox2D View = GetViewRect();
	Cursor = View.GetCenter();
	LastMouse = Cursor;
	if (!Surface.IsBuilt())
	{
		Notify(TEXT("このキャラはペイントできません"));
	}
	ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiOpen);
}

UChaosImpactPaintStudio::EResult UChaosImpactPaintStudio::ConsumeResult()
{
	const EResult Was = Result;
	Result = EResult::None;
	return Was;
}

void UChaosImpactPaintStudio::Finish(const bool bKeep)
{
	if (!bOpen)
	{
		return;
	}
	if (Surface.IsStroking())
	{
		Surface.EndStroke();
	}
	if (!bKeep)
	{
		// Put back as they were.
		for (int32 Index = 0; Index < Layers.Num(); ++Index)
		{
			UChaosImpactPaintLayer* Layer = Layers[Index];
			if (Layer && Before.IsValidIndex(Index) && Before[Index].Num() == Layer->GetPixels().Num())
			{
				Layer->GetPixels() = Before[Index];
				Layer->MarkDirty(FIntRect(0, 0, ChaosImpactBodyPaint::Size, ChaosImpactBodyPaint::Size));
				Layer->Flush();
			}
		}
	}
	Surface.ForgetHistory();
	Before.Reset();
	if (Preview)
	{
		Preview->EndPaintView();
	}
	bOpen = false;
	Result = bKeep ? EResult::Done : EResult::Cancelled;
	ChaosImpactSfx::Play2D(Controller.Get(), bKeep ? EChaosImpactSfx::UiReady : EChaosImpactSfx::UiBack);
}

// ---- The brush and the colours -------------------------------------------------------------------------------------------

float UChaosImpactPaintStudio::SizeToRadius(const float Amount)
{
	// 0.4 to 36 world units, finer at the small end.
	return 0.4f * FMath::Pow(90.0f, FMath::Clamp(Amount, 0.0f, 1.0f));
}

float UChaosImpactPaintStudio::RadiusToSize(const float Radius)
{
	return FMath::Clamp(FMath::Loge(FMath::Max(Radius, 0.4f) / 0.4f) / FMath::Loge(90.0f), 0.0f, 1.0f);
}

void UChaosImpactPaintStudio::SetTool(const ETool InTool)
{
	if (InTool == ETool::Picker && Tool != ETool::Picker)
	{
		ToolBeforePicker = Tool;
	}
	Tool = InTool;
	Brush.bErase = Tool == ETool::Eraser;
}

void UChaosImpactPaintStudio::CycleTool(const int32 Direction)
{
	SetTool(static_cast<ETool>((static_cast<int32>(Tool) + Direction + ToolCount) % ToolCount));
	ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiMove);
}

void UChaosImpactPaintStudio::SetColour(const FColor& Colour, const bool bRemember)
{
	Brush.Colour = FColor(Colour.R, Colour.G, Colour.B, 255);
	SyncHSB();
	if (bRemember)
	{
		if (UChaosImpactColourSubsystem* Colours = UChaosImpactColourSubsystem::Get(Controller.Get()))
		{
			Colours->AddRecent(Brush.Colour);
		}
	}
}

void UChaosImpactPaintStudio::SyncHSB()
{
	float Hue = EditHue, Saturation = 0.0f, Value = 0.0f;
	ChaosImpactCustomColour::ToHSV(Brush.Colour, Hue, Saturation, Value);
	// (A grey keeps the hue it had.)
	if (Saturation > 0.001f && Value > 0.001f)
	{
		EditHue = Hue;
	}
	EditSaturation = Saturation;
	EditBrightness = Value;
}

float UChaosImpactPaintStudio::GetSlider(const int32 Slider) const
{
	return Slider == 0 ? RadiusToSize(Brush.Radius) : Slider == 1 ? Brush.Hardness : (Brush.Opacity - 0.05f) / 0.95f;
}

void UChaosImpactPaintStudio::SetSlider(const int32 Slider, const float Amount)
{
	const float A = FMath::Clamp(Amount, 0.0f, 1.0f);
	if (Slider == 0)
	{
		Brush.Radius = SizeToRadius(A);
	}
	else if (Slider == 1)
	{
		Brush.Hardness = A;
	}
	else
	{
		Brush.Opacity = 0.05f + A * 0.95f;
	}
}

void UChaosImpactPaintStudio::PickSquare(const FVector2D& Point)
{
	const FBox2D R = SquareRect();
	EditSaturation = FMath::Clamp(static_cast<float>((Point.X - R.Min.X) / R.GetSize().X), 0.0f, 1.0f);
	EditBrightness = FMath::Clamp(static_cast<float>(1.0 - (Point.Y - R.Min.Y) / R.GetSize().Y), 0.0f, 1.0f);
	Brush.Colour = ChaosImpactCustomColour::FromHSV(EditHue, EditSaturation, EditBrightness);
}

void UChaosImpactPaintStudio::PickHue(const FVector2D& Point)
{
	const FBox2D R = HueRect();
	EditHue = FMath::Clamp(static_cast<float>((Point.Y - R.Min.Y) / R.GetSize().Y), 0.0f, 0.9999f) * 360.0f;
	if (EditSaturation < 0.05f)
	{
		// Choosing a hue on a grey: a colour of it.
		EditSaturation = 0.85f;
		EditBrightness = FMath::Max(EditBrightness, 0.6f);
	}
	Brush.Colour = ChaosImpactCustomColour::FromHSV(EditHue, EditSaturation, EditBrightness);
}

// ---- The view ---------------------------------------------------------------------------------------------------------

FBox2D UChaosImpactPaintStudio::GetViewRect()
{
	return FBox2D(FVector2D(StViewLeft, StViewTop), FVector2D(StViewLeft + StViewWidth, StViewTop + StViewHeight));
}

void UChaosImpactPaintStudio::SetView(const float Yaw, const float Pitch, const float Zoom, const FVector2D& Pan)
{
	ViewYaw = Yaw;
	ViewPitch = FMath::Clamp(Pitch, -80.0f, 80.0f);
	ViewZoom = FMath::Clamp(Zoom, MinZoom, MaxZoom);
	ViewPan = Pan;
	ApplyView();
}

void UChaosImpactPaintStudio::ResetView()
{
	SetView(StartYaw, 8.0f, 1.0f, FVector2D::ZeroVector);
}

void UChaosImpactPaintStudio::ApplyView()
{
	ViewYaw = FMath::UnwindDegrees(ViewYaw);
	const float Reach = Surface.IsBuilt() ? static_cast<float>(Surface.GetBounds().GetExtent().Size()) : 120.0f;
	ViewPan.X = FMath::Clamp(ViewPan.X, -Reach, Reach);
	ViewPan.Y = FMath::Clamp(ViewPan.Y, -Reach, Reach);
	if (Preview)
	{
		Preview->SetPaintView(ViewYaw, ViewPitch, ViewZoom, ViewPan);
	}
}

bool UChaosImpactPaintStudio::ToPicture(const FVector2D& Point, FVector2D& OutUV) const
{
	const FBox2D View = GetViewRect();
	if (!View.IsInside(Point))
	{
		return false;
	}
	OutUV = (Point - View.Min) / View.GetSize();
	return true;
}

bool UChaosImpactPaintStudio::HitAt(const FVector2D& Point, FChaosImpactPaintSurface::FHit& OutHit, FVector& OutDirection) const
{
	FVector2D UV;
	if (!Preview || !Surface.IsBuilt() || !ToPicture(Point, UV))
	{
		return false;
	}
	FVector Origin;
	Preview->GetPictureRay(UV, Origin, OutDirection);
	return Surface.Raycast(Origin, OutDirection, OutHit);
}

FVector2D UChaosImpactPaintStudio::GetModelCentreOnScreen() const
{
	FVector2D UV(0.5, 0.5);
	if (Preview && Surface.IsBuilt())
	{
		Preview->ProjectToPicture(Surface.GetBounds().GetCenter(), UV);
	}
	const FBox2D View = GetViewRect();
	return View.Min + UV * View.GetSize();
}

void UChaosImpactPaintStudio::UpdateCursorRing()
{
	bCursorOnModel = false;
	bMirrorCursorShown = false;
	CursorRing = 0.0f;
	FChaosImpactPaintSurface::FHit Hit;
	FVector Direction;
	if (bLeaveMenu || !HitAt(Cursor, Hit, Direction))
	{
		return;
	}
	bCursorOnModel = true;
	const FBox2D View = GetViewRect();
	const FVector Side = FVector::CrossProduct(FVector::UpVector, Direction).GetSafeNormal();
	FVector2D A, B;
	if (Preview->ProjectToPicture(Hit.Location, A) && Preview->ProjectToPicture(Hit.Location + Side * Brush.Radius, B))
	{
		CursorRing = static_cast<float>(((B - A) * View.GetSize()).Size());
	}
	if (Brush.bMirror && (Tool == ETool::Pen || Tool == ETool::Eraser))
	{
		FVector Mirrored;
		FVector2D UV;
		if (Surface.GetMirrorLocation(Hit, Mirrored) && Preview->ProjectToPicture(Mirrored, UV))
		{
			MirrorCursor = View.Min + UV * View.GetSize();
			bMirrorCursorShown = View.IsInside(MirrorCursor) && (MirrorCursor - Cursor).Size() > 2.0;
		}
	}
}

// ---- Drawing on the model ---------------------------------------------------------------------------------------------

void UChaosImpactPaintStudio::PaintAlong(const FVector2D& From, const FVector2D& To, const bool bFirst)
{
	if (!Surface.IsStroking())
	{
		return;
	}
	// Along the screen, a little at a time, each point found on the model (off it, the line breaks there).
	const float Step = FMath::Max(1.5f, CursorRing > 0.0f ? CursorRing * 0.3f : 3.0f);
	const float Distance = static_cast<float>((To - From).Size());
	const int32 Steps = bFirst ? 0 : FMath::Clamp(FMath::CeilToInt(Distance / Step), 1, 96);
	for (int32 Index = bFirst ? 0 : 1; Index <= Steps; ++Index)
	{
		const FVector2D At = Steps > 0 ? FMath::Lerp(From, To, Index / static_cast<double>(Steps)) : To;
		FChaosImpactPaintSurface::FHit Hit;
		FVector Direction;
		if (HitAt(At, Hit, Direction))
		{
			Surface.StrokeTo(Hit, Direction);
			bChanged = true;
		}
		else
		{
			Surface.Lift();
		}
	}
}

void UChaosImpactPaintStudio::DrawLine(const FVector2D& From, const FVector2D& To)
{
	if (!Surface.IsBuilt() || (Tool != ETool::Pen && Tool != ETool::Eraser))
	{
		return;
	}
	Surface.BeginStroke(Layers, Brush);
	PaintAlong(From, From, true);
	PaintAlong(From, To, false);
	Surface.EndStroke();
}

void UChaosImpactPaintStudio::ClickView(const FVector2D& Point)
{
	if (Tool == ETool::Pen || Tool == ETool::Eraser)
	{
		DrawLine(Point, Point);
		return;
	}
	if (Tool == ETool::Picker)
	{
		PickAt(Point);
		return;
	}
	FChaosImpactPaintSurface::FHit Hit;
	FVector Direction;
	if (HitAt(Point, Hit, Direction))
	{
		FChaosImpactBrush FillBrush = Brush;
		FillBrush.bErase = false;
		Surface.Fill(Layers, Hit, FillBrush);
		bChanged = true;
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiValue);
	}
}

void UChaosImpactPaintStudio::PickAt(const FVector2D& Point)
{
	FChaosImpactPaintSurface::FHit Hit;
	FVector Direction;
	FColor Picked;
	bool bPicked = HitAt(Point, Hit, Direction) && Surface.PickColour(Layers, Hit, Picked);
	FVector2D UV;
	UTextureRenderTarget2D* Picture = Preview ? Preview->GetPicture() : nullptr;
	if (!bPicked && Picture && ToPicture(Point, UV))
	{
		// Not drawn on there: the colour as it is seen.
		if (FTextureRenderTargetResource* Resource = Picture->GameThread_GetRenderTargetResource())
		{
			const int32 X = FMath::Clamp(FMath::FloorToInt(UV.X * Picture->SizeX), 0, Picture->SizeX - 1);
			const int32 Y = FMath::Clamp(FMath::FloorToInt(UV.Y * Picture->SizeY), 0, Picture->SizeY - 1);
			TArray<FColor> Pixels;
			if (Resource->ReadPixels(Pixels, FReadSurfaceDataFlags(), FIntRect(X, Y, X + 1, Y + 1)) && Pixels.Num() > 0)
			{
				Picked = Pixels[0];
				bPicked = true;
			}
		}
	}
	if (!bPicked)
	{
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiDeny);
		return;
	}
	SetColour(Picked, true);
	// Back to the tool it was picked up from.
	SetTool(ToolBeforePicker == ETool::Picker ? ETool::Pen : ToolBeforePicker);
	Notify(FString::Printf(TEXT("色をひろいました #%02X%02X%02X"), Picked.R, Picked.G, Picked.B));
	ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiValue);
}

void UChaosImpactPaintStudio::Undo()
{
	if (Surface.IsStroking())
	{
		return;
	}
	if (!Surface.CanUndo())
	{
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiDeny);
		return;
	}
	Surface.UndoStep(Layers);
	bChanged = true;
	ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiBack);
}

void UChaosImpactPaintStudio::Redo()
{
	if (Surface.IsStroking())
	{
		return;
	}
	if (!Surface.CanRedo())
	{
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiDeny);
		return;
	}
	Surface.RedoStep(Layers);
	bChanged = true;
	ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiValue);
}

void UChaosImpactPaintStudio::ClearAll()
{
	if (!Surface.IsBuilt() || Surface.IsStroking())
	{
		return;
	}
	Surface.ClearAll(Layers);
	bChanged = true;
	Notify(TEXT("全部けしました（もどすで元にもどせます）"));
	ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiErase);
}

// ---- Pressing, dragging, letting go ------------------------------------------------------------------------------------

int32 UChaosImpactPaintStudio::HitTest(const FVector2D& Point) const
{
	if (bLeaveMenu)
	{
		for (int32 Index = 0; Index < 3; ++Index)
		{
			if (LeaveRect(Index).IsInside(Point)) { return 900 + Index; }
		}
		return INDEX_NONE;
	}
	if (GetViewRect().IsInside(Point)) { return 1; }
	for (int32 Index = 0; Index < ToolCount; ++Index)
	{
		if (ToolRect(Index).IsInside(Point)) { return 100 + Index; }
	}
	for (int32 Index = 0; Index < StSliders; ++Index)
	{
		if (StGrow(SliderTrack(Index), 8.0f).IsInside(Point)) { return 200 + Index; }
	}
	if (MirrorRect().IsInside(Point)) { return 300; }
	for (int32 Index = 0; Index < 4; ++Index)
	{
		if (ButtonRect(Index).IsInside(Point)) { return 400 + Index; }
	}
	for (int32 Index = 0; Index < 2; ++Index)
	{
		if (FinishRect(Index).IsInside(Point)) { return 500 + Index; }
	}
	for (int32 Index = 0; Index < ChaosImpactCustomColour::PaletteColumns * ChaosImpactCustomColour::PaletteRows; ++Index)
	{
		if (StGrow(SwatchRect(Index), 2.0f).IsInside(Point)) { return 600 + Index; }
	}
	if (SquareRect().IsInside(Point)) { return 700; }
	if (StGrow(HueRect(), 4.0f).IsInside(Point)) { return 701; }
	const UChaosImpactColourSubsystem* Colours = UChaosImpactColourSubsystem::Get(Controller.Get());
	const int32 Recent = Colours ? Colours->GetRecent().Num() : 0;
	for (int32 Index = 0; Index < Recent; ++Index)
	{
		if (RecentRect(Index).IsInside(Point)) { return 800 + Index; }
	}
	return INDEX_NONE;
}

void UChaosImpactPaintStudio::Press(const FVector2D& Point, const bool bAlt)
{
	const int32 Hit = HitTest(Point);
	if (Hit != 501)
	{
		bCancelArmed = false;
	}
	if (Hit >= 900)
	{
		ChooseLeave(Hit - 900);
		return;
	}
	if (bLeaveMenu)
	{
		return;
	}
	if (Hit == 1)
	{
		if (!Surface.IsBuilt())
		{
			ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiDeny);
			return;
		}
		if (bAlt || Tool == ETool::Picker || Tool == ETool::Fill)
		{
			if (bAlt) { PickAt(Point); } else { ClickView(Point); }
			return;
		}
		Surface.BeginStroke(Layers, Brush);
		bPenDown = true;
		LastPaintPoint = Point;
		PaintAlong(Point, Point, true);
		return;
	}
	if (Hit >= 100 && Hit < 100 + ToolCount)
	{
		SetTool(static_cast<ETool>(Hit - 100));
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiSelect);
	}
	else if (Hit >= 200 && Hit < 200 + StSliders)
	{
		Dragging = Hit;
		DragTo(Point);
	}
	else if (Hit == 300)
	{
		Brush.bMirror = !Brush.bMirror;
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiValue);
	}
	else if (Hit >= 400 && Hit < 404)
	{
		Button(Hit - 400);
	}
	else if (Hit == 500)
	{
		Finish(true);
	}
	else if (Hit == 501)
	{
		if (bChanged && !bCancelArmed)
		{
			bCancelArmed = true;
			Notify(TEXT("もう一度おすと、描いたものをすててもどります"));
			ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiDeny);
			return;
		}
		Finish(false);
	}
	else if (Hit >= 600 && Hit < 800)
	{
		const int32 Index = Hit - 600;
		SetColour(ChaosImpactCustomColour::GetPaletteSwatch(Index % ChaosImpactCustomColour::PaletteColumns,
			Index / ChaosImpactCustomColour::PaletteColumns), true);
		if (Tool == ETool::Eraser || Tool == ETool::Picker)
		{
			SetTool(ETool::Pen);
		}
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiValue);
	}
	else if (Hit == 700 || Hit == 701)
	{
		Dragging = Hit;
		DragTo(Point);
		if (Tool == ETool::Eraser || Tool == ETool::Picker)
		{
			SetTool(ETool::Pen);
		}
	}
	else if (Hit >= 800 && Hit < 900)
	{
		const UChaosImpactColourSubsystem* Colours = UChaosImpactColourSubsystem::Get(Controller.Get());
		if (Colours && Colours->GetRecent().IsValidIndex(Hit - 800))
		{
			SetColour(Colours->GetRecent()[Hit - 800], false);
			if (Tool == ETool::Eraser || Tool == ETool::Picker)
			{
				SetTool(ETool::Pen);
			}
			ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiValue);
		}
	}
}

void UChaosImpactPaintStudio::DragTo(const FVector2D& Point)
{
	if (Dragging >= 200 && Dragging < 200 + StSliders)
	{
		const FBox2D Track = SliderTrack(Dragging - 200);
		SetSlider(Dragging - 200, static_cast<float>((Point.X - Track.Min.X) / Track.GetSize().X));
	}
	else if (Dragging == 700)
	{
		PickSquare(Point);
	}
	else if (Dragging == 701)
	{
		PickHue(Point);
	}
}

void UChaosImpactPaintStudio::Release()
{
	if (bPenDown)
	{
		bPenDown = false;
		Surface.EndStroke();
	}
	if (Dragging == 700 || Dragging == 701)
	{
		// The colour made on the square: into the recent ones.
		SetColour(Brush.Colour, true);
	}
	Dragging = INDEX_NONE;
}

void UChaosImpactPaintStudio::Button(const int32 Index)
{
	switch (Index)
	{
	case 0: Undo(); break;
	case 1: Redo(); break;
	case 2: ClearAll(); break;
	default:
		ResetView();
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiValue);
		break;
	}
}

void UChaosImpactPaintStudio::OpenLeaveMenu()
{
	Release();
	bLeaveMenu = true;
	LeaveChoice = 0;
	ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiOpen);
}

void UChaosImpactPaintStudio::ChooseLeave(const int32 Choice)
{
	bLeaveMenu = false;
	if (Choice == 0)
	{
		Finish(true);
	}
	else if (Choice == 1)
	{
		Finish(false);
	}
	else
	{
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiBack);
	}
}

void UChaosImpactPaintStudio::Notify(const FString& Text)
{
	NoticeText = Text;
	NoticeAt = FPlatformTime::Seconds();
}

// ---- Input ------------------------------------------------------------------------------------------------------------

bool UChaosImpactPaintStudio::HandleKeyDown(const FKeyEvent& Event)
{
	if (!bOpen)
	{
		return false;
	}
	const FKey Raw = Event.GetKey();
	if (Raw.IsMouseButton())
	{
		return false;
	}
	const FKey Key = ChaosImpactSettings::ToMenuKey(Raw, Event.GetInputDeviceId().GetId());
	const bool bRepeat = Event.IsRepeat();
	const bool bPad = Key.IsGamepadKey();
	if (bPad)
	{
		bPadCursor = true;
	}
	if (bLeaveMenu)
	{
		if (StIsAny(Key, {EKeys::Gamepad_DPad_Up, EKeys::Up, EKeys::W, EKeys::Gamepad_LeftStick_Up}))
		{
			LeaveChoice = (LeaveChoice + 2) % 3;
			ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiMove);
		}
		else if (StIsAny(Key, {EKeys::Gamepad_DPad_Down, EKeys::Down, EKeys::S, EKeys::Gamepad_LeftStick_Down}))
		{
			LeaveChoice = (LeaveChoice + 1) % 3;
			ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiMove);
		}
		else if (!bRepeat && StIsAny(Key, {EKeys::Gamepad_FaceButton_Bottom, EKeys::Enter, EKeys::SpaceBar}))
		{
			ChooseLeave(LeaveChoice);
		}
		else if (!bRepeat && StIsAny(Key, {EKeys::Gamepad_FaceButton_Right, EKeys::Escape, EKeys::BackSpace, EKeys::Gamepad_Special_Right}))
		{
			ChooseLeave(2);
		}
		return true;
	}
	const bool bCtrl = Event.IsControlDown();
	// The controller.
	if (Key == EKeys::Gamepad_FaceButton_Bottom)
	{
		if (!bRepeat && !bPadA)
		{
			bPadA = true;
			Press(Cursor, false);
		}
	}
	else if (!bRepeat && StIsAny(Key, {EKeys::Gamepad_FaceButton_Right, EKeys::Gamepad_Special_Right, EKeys::Escape}))
	{
		OpenLeaveMenu();
	}
	else if (!bRepeat && Key == EKeys::Gamepad_FaceButton_Left) { Undo(); }
	else if (!bRepeat && Key == EKeys::Gamepad_FaceButton_Top) { Redo(); }
	else if (!bRepeat && Key == EKeys::Gamepad_LeftShoulder) { CycleTool(-1); }
	else if (!bRepeat && Key == EKeys::Gamepad_RightShoulder) { CycleTool(1); }
	else if (StIsAny(Key, {EKeys::Gamepad_DPad_Left, EKeys::LeftBracket})) { SetSlider(0, GetSlider(0) - 0.04f); }
	else if (StIsAny(Key, {EKeys::Gamepad_DPad_Right, EKeys::RightBracket})) { SetSlider(0, GetSlider(0) + 0.04f); }
	else if (Key == EKeys::Gamepad_DPad_Up) { SetSlider(2, GetSlider(2) + 0.05f); }
	else if (Key == EKeys::Gamepad_DPad_Down) { SetSlider(2, GetSlider(2) - 0.05f); }
	else if (!bRepeat && StIsAny(Key, {EKeys::Gamepad_Special_Left, EKeys::M}))
	{
		Brush.bMirror = !Brush.bMirror;
		Notify(Brush.bMirror ? TEXT("左右対称：オン") : TEXT("左右対称：オフ"));
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiValue);
	}
	else if (!bRepeat && StIsAny(Key, {EKeys::Gamepad_RightThumbstick, EKeys::Home, EKeys::F}))
	{
		ResetView();
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiValue);
	}
	// The keys.
	else if (bCtrl && Raw == EKeys::Z) { if (Event.IsShiftDown()) { Redo(); } else { Undo(); } }
	else if (bCtrl && Raw == EKeys::Y) { Redo(); }
	else if (!bRepeat && StIsAny(Raw, {EKeys::B, EKeys::One})) { SetTool(ETool::Pen); ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiMove); }
	else if (!bRepeat && StIsAny(Raw, {EKeys::E, EKeys::Two})) { SetTool(ETool::Eraser); ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiMove); }
	else if (!bRepeat && StIsAny(Raw, {EKeys::G, EKeys::Three})) { SetTool(ETool::Fill); ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiMove); }
	else if (!bRepeat && StIsAny(Raw, {EKeys::I, EKeys::Four})) { SetTool(ETool::Picker); ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiMove); }
	else if (StIsAny(Raw, {EKeys::Equals, EKeys::Add})) { SetView(ViewYaw, ViewPitch, ViewZoom * 1.15f, ViewPan); }
	else if (StIsAny(Raw, {EKeys::Hyphen, EKeys::Subtract})) { SetView(ViewYaw, ViewPitch, ViewZoom / 1.15f, ViewPan); }
	else if (StIsAny(Raw, {EKeys::Left, EKeys::A})) { SetView(ViewYaw + 8.0f, ViewPitch, ViewZoom, ViewPan); }
	else if (StIsAny(Raw, {EKeys::Right, EKeys::D})) { SetView(ViewYaw - 8.0f, ViewPitch, ViewZoom, ViewPan); }
	else if (StIsAny(Raw, {EKeys::Up, EKeys::W})) { SetView(ViewYaw, ViewPitch + 6.0f, ViewZoom, ViewPan); }
	else if (StIsAny(Raw, {EKeys::Down, EKeys::S})) { SetView(ViewYaw, ViewPitch - 6.0f, ViewZoom, ViewPan); }
	return true;
}

bool UChaosImpactPaintStudio::HandleKeyUp(const FKeyEvent& Event)
{
	if (!bOpen)
	{
		return false;
	}
	const FKey Key = ChaosImpactSettings::ToMenuKey(Event.GetKey(), Event.GetInputDeviceId().GetId());
	if (Key == EKeys::Gamepad_FaceButton_Bottom && bPadA)
	{
		bPadA = false;
		Release();
		return true;
	}
	return false;
}

bool UChaosImpactPaintStudio::HandleAnalog(const FAnalogInputEvent& Event)
{
	if (!bOpen)
	{
		return false;
	}
	const FKey Key = Event.GetKey();
	const float Value = Event.GetAnalogValue();
	if (Key == EKeys::Gamepad_LeftX) { LeftStick.X = Value; }
	else if (Key == EKeys::Gamepad_LeftY) { LeftStick.Y = Value; }
	else if (Key == EKeys::Gamepad_RightX) { RightStick.X = Value; }
	else if (Key == EKeys::Gamepad_RightY) { RightStick.Y = Value; }
	else if (Key == EKeys::Gamepad_LeftTriggerAxis) { ZoomOut = Value; }
	else if (Key == EKeys::Gamepad_RightTriggerAxis) { ZoomIn = Value; }
	else { return false; }
	if (FMath::Abs(Value) > 0.2f)
	{
		bPadCursor = true;
	}
	return true;
}

void UChaosImpactPaintStudio::HandleMouseDown(const FVector2D& Point, const FKey& Button, const bool bAlt)
{
	if (!bOpen)
	{
		return;
	}
	Cursor = Point;
	LastMouse = Point;
	bPadCursor = false;
	if (Button == EKeys::LeftMouseButton)
	{
		Press(Point, bAlt);
	}
	else if (Button == EKeys::RightMouseButton && !bLeaveMenu)
	{
		bTurning = true;
	}
	else if (Button == EKeys::MiddleMouseButton && !bLeaveMenu)
	{
		bSliding = true;
	}
}

void UChaosImpactPaintStudio::HandleMouseUp(const FVector2D& Point, const FKey& Button)
{
	if (!bOpen)
	{
		return;
	}
	if (Button == EKeys::LeftMouseButton)
	{
		Release();
	}
	else if (Button == EKeys::RightMouseButton)
	{
		bTurning = false;
	}
	else if (Button == EKeys::MiddleMouseButton)
	{
		bSliding = false;
	}
}

void UChaosImpactPaintStudio::HandleMouseMove(const FVector2D& Point)
{
	if (!bOpen)
	{
		return;
	}
	const FVector2D Delta = Point - LastMouse;
	LastMouse = Point;
	Cursor = Point;
	bPadCursor = false;
	if (bLeaveMenu)
	{
		for (int32 Index = 0; Index < 3; ++Index)
		{
			if (LeaveRect(Index).IsInside(Point)) { LeaveChoice = Index; }
		}
		return;
	}
	if (bTurning)
	{
		SetView(ViewYaw - static_cast<float>(Delta.X) * 0.4f, ViewPitch + static_cast<float>(Delta.Y) * 0.3f, ViewZoom, ViewPan);
	}
	else if (bSliding)
	{
		const float Reach = Surface.IsBuilt() ? static_cast<float>(Surface.GetBounds().GetExtent().Size()) : 120.0f;
		const float PerPixel = Reach * 2.2f / StViewHeight / ViewZoom;
		SetView(ViewYaw, ViewPitch, ViewZoom, ViewPan + FVector2D(-Delta.X, Delta.Y) * PerPixel);
	}
	if (Dragging != INDEX_NONE)
	{
		DragTo(Point);
	}
	if (bPenDown)
	{
		PaintAlong(LastPaintPoint, Point, false);
		LastPaintPoint = Point;
	}
}

void UChaosImpactPaintStudio::HandleWheel(const FVector2D& Point, const float Delta, const bool bCtrl)
{
	if (!bOpen || bLeaveMenu || Delta == 0.0f)
	{
		return;
	}
	if (bCtrl)
	{
		SetSlider(0, GetSlider(0) + Delta * 0.04f);
		return;
	}
	if (GetViewRect().IsInside(Point))
	{
		SetView(ViewYaw, ViewPitch, ViewZoom * FMath::Pow(1.15f, Delta), ViewPan);
	}
	else
	{
		for (int32 Index = 0; Index < StSliders; ++Index)
		{
			if (StGrow(SliderTrack(Index), 14.0f).IsInside(Point))
			{
				SetSlider(Index, GetSlider(Index) + Delta * 0.03f);
			}
		}
	}
}

// ---- Every frame ------------------------------------------------------------------------------------------------------

void UChaosImpactPaintStudio::Tick(const float DeltaSeconds)
{
	if (!bOpen)
	{
		return;
	}
	const float Step = FMath::Min(DeltaSeconds, 0.1f);
	// The controller's cursor: faster the further the stick goes, slower while drawing.
	const float Lean = static_cast<float>(LeftStick.Size());
	if (Lean > 0.15f)
	{
		const float Amount = FMath::Square((FMath::Min(Lean, 1.0f) - 0.15f) / 0.85f);
		const float Speed = (bPenDown ? 520.0f : 900.0f) * (0.12f + 0.88f * Amount);
		const FVector2D Way = FVector2D(LeftStick.X, -LeftStick.Y) / Lean;
		Cursor += Way * Speed * Step;
		Cursor.X = FMath::Clamp(Cursor.X, 0.0, 1600.0);
		Cursor.Y = FMath::Clamp(Cursor.Y, 0.0, 900.0);
		if (bLeaveMenu)
		{
			for (int32 Index = 0; Index < 3; ++Index)
			{
				if (LeaveRect(Index).IsInside(Cursor)) { LeaveChoice = Index; }
			}
		}
		if (Dragging != INDEX_NONE)
		{
			DragTo(Cursor);
		}
	}
	bool bViewMoved = false;
	if (!bLeaveMenu)
	{
		if (RightStick.Size() > 0.2f)
		{
			ViewYaw -= static_cast<float>(RightStick.X) * 150.0f * Step;
			ViewPitch = FMath::Clamp(ViewPitch - static_cast<float>(RightStick.Y) * 100.0f * Step, -80.0f, 80.0f);
			bViewMoved = true;
		}
		const float Zooming = (ZoomIn > 0.1f ? ZoomIn : 0.0f) - (ZoomOut > 0.1f ? ZoomOut : 0.0f);
		if (Zooming != 0.0f)
		{
			ViewZoom = FMath::Clamp(ViewZoom * FMath::Exp(Zooming * 1.6f * Step), MinZoom, MaxZoom);
			bViewMoved = true;
		}
	}
	ApplyView();
	if (bPenDown && (Lean > 0.15f || bViewMoved))
	{
		// (Held still on a turning model, the pen still draws where it now is.)
		PaintAlong(LastPaintPoint, Cursor, false);
		LastPaintPoint = Cursor;
	}
	if (IsValid(Preview))
	{
		Preview->Animate(Step);
		Preview->FilmNow();
	}
	UpdateCursorRing();
}

// ---- Layout -----------------------------------------------------------------------------------------------------------

FBox2D UChaosImpactPaintStudio::ToolRect(const int32 Index)
{
	const FVector2D At(StLeft + (Index % 2) * 138.0f, 144.0f + (Index / 2) * 90.0f);
	return FBox2D(At, At + FVector2D(122.0f, 80.0f));
}

FBox2D UChaosImpactPaintStudio::SliderTrack(const int32 Index)
{
	const float Top = 384.0f + Index * 58.0f;
	return FBox2D(FVector2D(StLeft + 4.0f, Top), FVector2D(StLeft + StLeftWidth - 4.0f, Top + 14.0f));
}

FBox2D UChaosImpactPaintStudio::MirrorRect()
{
	return FBox2D(FVector2D(StLeft, 548.0f), FVector2D(StLeft + StLeftWidth, 594.0f));
}

FBox2D UChaosImpactPaintStudio::ButtonRect(const int32 Index)
{
	const FVector2D At(StLeft + (Index % 2) * 134.0f, 608.0f + (Index / 2) * 56.0f);
	return FBox2D(At, At + FVector2D(126.0f, 48.0f));
}

FBox2D UChaosImpactPaintStudio::FinishRect(const int32 Index)
{
	return Index == 0 ? FBox2D(FVector2D(StLeft, 734.0f), FVector2D(StLeft + 156.0f, 812.0f))
		: FBox2D(FVector2D(StLeft + 166.0f, 734.0f), FVector2D(StLeft + StLeftWidth, 812.0f));
}

FBox2D UChaosImpactPaintStudio::SwatchRect(const int32 Index)
{
	const FVector2D At(StRight + (Index % ChaosImpactCustomColour::PaletteColumns) * StSwatchStep,
		226.0f + (Index / ChaosImpactCustomColour::PaletteColumns) * StSwatchStep);
	return FBox2D(At, At + FVector2D(StSwatch));
}

FBox2D UChaosImpactPaintStudio::SquareRect()
{
	return FBox2D(FVector2D(StRight, 424.0f), FVector2D(StRight + 200.0f, 624.0f));
}

FBox2D UChaosImpactPaintStudio::HueRect()
{
	return FBox2D(FVector2D(StRight + 214.0f, 424.0f), FVector2D(StRight + 240.0f, 624.0f));
}

FBox2D UChaosImpactPaintStudio::RecentRect(const int32 Index)
{
	const FVector2D At(StRight + Index * StRecentStep, 668.0f);
	return FBox2D(At, At + FVector2D(StRecent));
}

FBox2D UChaosImpactPaintStudio::LeaveRect(const int32 Index)
{
	const FVector2D At(560.0f, 392.0f + Index * 72.0f);
	return FBox2D(At, At + FVector2D(480.0f, 60.0f));
}

// ---- Drawing the screen -----------------------------------------------------------------------------------------------

int32 UChaosImpactPaintStudio::Paint(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer) const
{
	if (!bOpen)
	{
		return Layer;
	}
	const double Now = FPlatformTime::Seconds();
	const float Pulse = 0.65f + 0.35f * FMath::Sin(static_cast<float>(Now - OpenedAt) * 6.0f);
	const FPainter Back{Design, Elements, Layer};
	const FPainter P{Design, Elements, Layer + 1};
	const FPainter Over{Design, Elements, Layer + 2};
	const FPainter Top{Design, Elements, Layer + 3};
	const int32 Hover = HitTest(Cursor);
	Back.Box(-1400.0f, 0.0f, 4400.0f, 900.0f, StBack);
	P.Text(TEXT("ペイント"), 40.0f, 22.0f, 40.0f, Paper, ETextAlign::Left, TEXT("BlackItalic"));
	P.Text(TEXT("キャラに直接、自由にえがこう"), 40.0f + HudMeasure(TEXT("ペイント"), 40.0f, TEXT("BlackItalic")) + 28.0f, 42.0f, 18.0f, Muted,
		ETextAlign::Left, TEXT("Bold"));

	// The view.
	const FBox2D View = GetViewRect();
	StRound(Back, StGrow(View, 6.0f), 14.0f, FLinearColor(0.02f, 0.035f, 0.09f, 1.0f));
	if (PictureBrush.GetResourceObject())
	{
		FSlateDrawElement::MakeBox(P.Elements, P.Layer,
			P.Geometry.ToPaintGeometry(FVector2f(View.GetSize()), FSlateLayoutTransform(FVector2f(View.Min))),
			&PictureBrush, ESlateDrawEffect::None, WithAlpha(FLinearColor::White, P.Alpha));
	}
	StFrame(Over, View, WithAlpha(Ice, Hover == 1 ? 0.55f : 0.25f), 2.0f);
	Over.Text(FString::Printf(TEXT("ズーム %d%%"), FMath::RoundToInt(ViewZoom * 100.0f)), static_cast<float>(View.Min.X) + 14.0f,
		static_cast<float>(View.Min.Y) + 10.0f, 15.0f, WithAlpha(Paper, 0.75f), ETextAlign::Left, TEXT("Bold"));
	const FString ToolLine = FString::Printf(TEXT("%s%s"), StToolNames[static_cast<int32>(Tool)],
		Brush.bMirror && (Tool == ETool::Pen || Tool == ETool::Eraser) ? TEXT("　・　左右対称") : TEXT(""));
	Over.Text(ToolLine, static_cast<float>(View.Max.X) - 14.0f, static_cast<float>(View.Min.Y) + 10.0f, 15.0f, WithAlpha(Gold, 0.9f),
		ETextAlign::Right, TEXT("Bold"));
	if (!Surface.IsBuilt())
	{
		StText(Over, TEXT("このキャラはペイントできません"), static_cast<float>(View.GetCenter().X), static_cast<float>(View.GetCenter().Y) - 14.0f,
			24.0f, StViewWidth - 40.0f, Paper, ETextAlign::Center, TEXT("Black"));
	}

	// The brush on the model: its reach round the cursor (and where its mirror draws).
	if (Hover == 1 && !bLeaveMenu)
	{
		const FVector2D C = Cursor;
		if (bCursorOnModel && (Tool == ETool::Pen || Tool == ETool::Eraser))
		{
			const float R = FMath::Max(CursorRing, 2.0f);
			Top.Ring(C, R, WithAlpha(Ink, 0.8f), 4.0f, 64);
			Top.Ring(C, R, Tool == ETool::Eraser ? FLinearColor(1.0f, 0.6f, 0.7f, 1.0f) : Paper, 2.0f, 64);
			if (Brush.Hardness < 0.95f && R > 6.0f)
			{
				Top.Ring(C, R * FMath::Max(Brush.Hardness, 0.1f), WithAlpha(Paper, 0.35f), 1.0f, 48);
			}
			if (bMirrorCursorShown)
			{
				Top.Ring(MirrorCursor, R, WithAlpha(Ice, 0.7f), 2.0f, 48);
			}
		}
		else
		{
			// Off the model, or a tool used at a point: a small cross.
			Top.Line(C + FVector2D(-9.0, 0.0), C + FVector2D(9.0, 0.0), WithAlpha(Ink, 0.8f), 4.0f);
			Top.Line(C + FVector2D(0.0, -9.0), C + FVector2D(0.0, 9.0), WithAlpha(Ink, 0.8f), 4.0f);
			Top.Line(C + FVector2D(-8.0, 0.0), C + FVector2D(8.0, 0.0), bCursorOnModel ? Paper : Muted, 2.0f);
			Top.Line(C + FVector2D(0.0, -8.0), C + FVector2D(0.0, 8.0), bCursorOnModel ? Paper : Muted, 2.0f);
		}
		if (Tool == ETool::Fill || Tool == ETool::Picker)
		{
			StToolIcon(Top, Tool, C + FVector2D(24.0, -22.0), Paper, StShown(Brush.Colour));
		}
	}

	// Tools.
	P.Text(TEXT("どうぐ"), StLeft, 110.0f, 20.0f, Paper, ETextAlign::Left, TEXT("Black"));
	for (int32 Index = 0; Index < ToolCount; ++Index)
	{
		const FBox2D R = ToolRect(Index);
		const bool bOn = static_cast<int32>(Tool) == Index;
		StRound(Back, R, 12.0f, bOn ? StCardOn : StCard);
		StToolIcon(P, static_cast<ETool>(Index), FVector2D(R.GetCenter().X, R.Min.Y + 30.0f), bOn ? Paper : WithAlpha(Paper, 0.8f), StShown(Brush.Colour));
		StText(P, StToolNames[Index], static_cast<float>(R.GetCenter().X), static_cast<float>(R.Max.Y) - 27.0f, 16.0f, 110.0f,
			Paper, ETextAlign::Center, TEXT("Black"));
		P.Text(StToolKeys[Index], static_cast<float>(R.Max.X) - 8.0f, static_cast<float>(R.Min.Y) + 4.0f, 12.0f, WithAlpha(Muted, 0.9f),
			ETextAlign::Right, TEXT("Bold"));
		if (bOn)
		{
			StFrame(Top, StGrow(R, 2.0f), WithAlpha(Gold, Pulse), 3.0f);
		}
		else if (Hover == 100 + Index)
		{
			StFrame(Top, StGrow(R, 1.0f), WithAlpha(Ice, 0.9f), 2.0f);
		}
	}
	StText(P, StToolTips[static_cast<int32>(Tool)], StLeft, 322.0f, 14.0f, StLeftWidth, Muted);

	// The brush's size, edge and strength.
	for (int32 Index = 0; Index < StSliders; ++Index)
	{
		const FBox2D Track = SliderTrack(Index);
		const float Amount = GetSlider(Index);
		const bool bActive = Dragging == 200 + Index || Hover == 200 + Index;
		P.Text(StSliderNames[Index], StLeft, static_cast<float>(Track.Min.Y) - 24.0f, 16.0f, Paper, ETextAlign::Left, TEXT("Black"));
		const int32 Number = Index == 0 ? FMath::Max(1, FMath::RoundToInt(Amount * 100.0f))
			: FMath::RoundToInt((Index == 1 ? Brush.Hardness : Brush.Opacity) * 100.0f);
		P.Text(Index == 0 ? FString::FromInt(Number) : FString::Printf(TEXT("%d%%"), Number), StLeft + StLeftWidth, static_cast<float>(Track.Min.Y) - 24.0f,
			16.0f, bActive ? Gold : Paper, ETextAlign::Right, TEXT("Bold"));
		StRound(Back, Track, 7.0f, StCard);
		StRound(P, FBox2D(Track.Min, FVector2D(Track.Min.X + Track.GetSize().X * Amount, Track.Max.Y)), 7.0f, bActive ? Gold : Ice);
		const FVector2D Knob(Track.Min.X + Track.GetSize().X * Amount, Track.GetCenter().Y);
		Over.Disc(Knob, bActive ? 11.0f : 9.0f, Paper);
		Over.Ring(Knob, bActive ? 11.0f : 9.0f, Ink, 2.0f, 24);
	}
	P.Text(TEXT("[ ]・十字←→"), StLeft + 56.0f, SliderTrack(0).Min.Y - 20.0f, 11.0f, WithAlpha(Muted, 0.85f), ETextAlign::Left, TEXT("Bold"));
	P.Text(TEXT("十字↑↓"), StLeft + 56.0f, SliderTrack(2).Min.Y - 20.0f, 11.0f, WithAlpha(Muted, 0.85f), ETextAlign::Left, TEXT("Bold"));

	// 左右対称.
	{
		const FBox2D R = MirrorRect();
		StRound(Back, R, 10.0f, Brush.bMirror ? StCardOn : StCard);
		StText(P, TEXT("左右対称"), static_cast<float>(R.Min.X) + 14.0f, static_cast<float>(R.Min.Y) + 11.0f, 18.0f, 120.0f, Paper,
			ETextAlign::Left, TEXT("Black"));
		P.Text(TEXT("M"), static_cast<float>(R.Min.X) + 132.0f, static_cast<float>(R.Min.Y) + 15.0f, 12.0f, Muted, ETextAlign::Left, TEXT("Bold"));
		const FBox2D Pill(FVector2D(R.Max.X - 74.0f, R.Min.Y + 10.0f), FVector2D(R.Max.X - 12.0f, R.Max.Y - 10.0f));
		StRound(P, Pill, 14.0f, Brush.bMirror ? Gold : FLinearColor(0.12f, 0.15f, 0.24f, 1.0f));
		P.Disc(FVector2D(Brush.bMirror ? Pill.Max.X - 14.0f : Pill.Min.X + 14.0f, Pill.GetCenter().Y), 11.0f, Paper);
		if (Hover == 300)
		{
			StFrame(Top, StGrow(R, 1.0f), WithAlpha(Ice, 0.9f), 2.0f);
		}
	}

	// もどす, やりなおす, 全部けす, 視点リセット.
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const FBox2D R = ButtonRect(Index);
		const bool bUsable = Index == 0 ? Surface.CanUndo() : Index == 1 ? Surface.CanRedo() : Index == 2 ? Surface.IsBuilt() : true;
		StRound(Back, R, 10.0f, Hover == 400 + Index && bUsable ? StCardOn : StCard);
		StText(P, StButtonNames[Index], static_cast<float>(R.GetCenter().X), static_cast<float>(R.Min.Y) + 14.0f, 16.0f, 104.0f,
			bUsable ? Paper : WithAlpha(Muted, 0.7f), ETextAlign::Center, TEXT("Black"));
		if (Hover == 400 + Index)
		{
			StFrame(Top, StGrow(R, 1.0f), WithAlpha(Ice, 0.9f), 2.0f);
		}
	}
	{
		// Their keys in the corner (keyboard / controller).
		static const TCHAR* const Keys[4] = {TEXT("Z・X"), TEXT("Y"), TEXT(""), TEXT("F・R3")};
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const FBox2D R = ButtonRect(Index);
			P.Text(Keys[Index], static_cast<float>(R.Max.X) - 6.0f, static_cast<float>(R.Min.Y) + 2.0f, 10.0f, WithAlpha(Muted, 0.9f),
				ETextAlign::Right, TEXT("Bold"));
		}
	}

	// 完了 / やめる.
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const FBox2D R = FinishRect(Index);
		const bool bHover = Hover == 500 + Index;
		const FLinearColor Accent = Index == 0 ? Gold : (bCancelArmed ? Fire : Muted);
		StRound(Back, R, 12.0f, bHover ? Accent : StCard);
		P.Box(static_cast<float>(R.Min.X) + 10.0f, static_cast<float>(R.Max.Y) - 7.0f, static_cast<float>(R.GetSize().X) - 20.0f, 3.0f, Accent);
		StText(P, Index == 0 ? TEXT("完了") : TEXT("やめる"), static_cast<float>(R.GetCenter().X), static_cast<float>(R.Min.Y) + 22.0f, 24.0f,
			static_cast<float>(R.GetSize().X) - 16.0f, bHover ? Ink : Paper, ETextAlign::Center, TEXT("Black"));
	}

	// The colours.
	P.Text(TEXT("いろ"), StRight, 110.0f, 20.0f, Paper, ETextAlign::Left, TEXT("Black"));
	{
		const FBox2D Now2(FVector2D(StRight, 144.0f), FVector2D(StRight + 110.0f, 204.0f));
		StRound(P, Now2, 12.0f, StShown(Brush.Colour));
		StFrame(Over, Now2, WithAlpha(Paper, 0.5f), 2.0f);
		P.Text(FString::Printf(TEXT("#%02X%02X%02X"), Brush.Colour.R, Brush.Colour.G, Brush.Colour.B), StRight + 124.0f, 150.0f, 20.0f, Paper,
			ETextAlign::Left, TEXT("Black"));
		StText(P, TEXT("スポイト（I・Alt+クリック）でキャラの色もひろえる"), StRight + 124.0f, 180.0f, 12.0f, StRightWidth - 124.0f, Muted);
	}
	const int32 Swatches = ChaosImpactCustomColour::PaletteColumns * ChaosImpactCustomColour::PaletteRows;
	for (int32 Index = 0; Index < Swatches; ++Index)
	{
		const FBox2D R = SwatchRect(Index);
		const FColor Colour = ChaosImpactCustomColour::GetPaletteSwatch(Index % ChaosImpactCustomColour::PaletteColumns,
			Index / ChaosImpactCustomColour::PaletteColumns);
		StRound(P, R, 5.0f, StShown(Colour));
		if (Colour == Brush.Colour)
		{
			StFrame(Over, StGrow(R, 2.0f), Paper, 2.0f);
		}
		if (Hover == 600 + Index)
		{
			StFrame(Top, StGrow(R, 3.0f), WithAlpha(Gold, Pulse), 2.0f);
		}
	}
	// The fine colour: saturation across, brightness up; the hue bar beside it.
	P.Text(TEXT("こまかく"), StRight, 398.0f, 15.0f, Muted, ETextAlign::Left, TEXT("Bold"));
	const FBox2D Square = SquareRect();
	StGradient(P, Square, {FLinearColor::White, StShown(ChaosImpactCustomColour::FromHSV(EditHue, 1.0f, 1.0f))});
	StGradient(Over, Square, {FLinearColor(0.0f, 0.0f, 0.0f, 0.0f), FLinearColor(0.0f, 0.0f, 0.0f, 1.0f)}, false);
	const FVector2D SquareKnob(Square.Min.X + Square.GetSize().X * EditSaturation, Square.Max.Y - Square.GetSize().Y * EditBrightness);
	Top.Ring(SquareKnob, 8.0f, Ink, 4.0f, 24);
	Top.Ring(SquareKnob, 8.0f, Paper, 2.0f, 24);
	if (Hover == 700 || Dragging == 700)
	{
		StFrame(Top, StGrow(Square, 2.0f), WithAlpha(Ice, 0.9f), 2.0f);
	}
	const FBox2D Hue = HueRect();
	{
		TArray<FLinearColor> Run;
		for (int32 Stop = 0; Stop <= 6; ++Stop)
		{
			Run.Add(StShown(ChaosImpactCustomColour::FromHSV(60.0f * Stop, 1.0f, 1.0f)));
		}
		StGradient(P, Hue, Run, false);
		const float KnobY = static_cast<float>(Hue.Min.Y + Hue.GetSize().Y * EditHue / 360.0f);
		Top.Box(static_cast<float>(Hue.Min.X) - 4.0f, KnobY - 3.0f, static_cast<float>(Hue.GetSize().X) + 8.0f, 6.0f, Ink);
		Top.Box(static_cast<float>(Hue.Min.X) - 3.0f, KnobY - 1.5f, static_cast<float>(Hue.GetSize().X) + 6.0f, 3.0f, Paper);
		if (Hover == 701 || Dragging == 701)
		{
			StFrame(Top, StGrow(Hue, 2.0f), WithAlpha(Ice, 0.9f), 2.0f);
		}
	}
	{
		const float X = StRight + 254.0f;
		P.Text(FString::Printf(TEXT("H %d°"), FMath::RoundToInt(EditHue)), X, 430.0f, 15.0f, Paper, ETextAlign::Left, TEXT("Bold"));
		P.Text(FString::Printf(TEXT("S %d%%"), FMath::RoundToInt(EditSaturation * 100.0f)), X, 456.0f, 15.0f, Paper, ETextAlign::Left, TEXT("Bold"));
		P.Text(FString::Printf(TEXT("B %d%%"), FMath::RoundToInt(EditBrightness * 100.0f)), X, 482.0f, 15.0f, Paper, ETextAlign::Left, TEXT("Bold"));
		P.Text(FString::Printf(TEXT("R %d"), Brush.Colour.R), X, 520.0f, 15.0f, FLinearColor(1.0f, 0.45f, 0.45f), ETextAlign::Left, TEXT("Bold"));
		P.Text(FString::Printf(TEXT("G %d"), Brush.Colour.G), X, 546.0f, 15.0f, FLinearColor(0.45f, 1.0f, 0.55f), ETextAlign::Left, TEXT("Bold"));
		P.Text(FString::Printf(TEXT("B %d"), Brush.Colour.B), X, 572.0f, 15.0f, FLinearColor(0.55f, 0.7f, 1.0f), ETextAlign::Left, TEXT("Bold"));
	}
	// The colours used last.
	P.Text(TEXT("最近つかった色"), StRight, 640.0f, 15.0f, Muted, ETextAlign::Left, TEXT("Bold"));
	{
		const UChaosImpactColourSubsystem* Colours = UChaosImpactColourSubsystem::Get(Controller.Get());
		const int32 Recent = Colours ? Colours->GetRecent().Num() : 0;
		for (int32 Index = 0; Index < ChaosImpactCustomColour::MaxRecent; ++Index)
		{
			const FBox2D R = RecentRect(Index);
			if (Index < Recent)
			{
				StRound(P, R, 6.0f, StShown(Colours->GetRecent()[Index]));
			}
			else
			{
				StRound(Back, R, 6.0f, FLinearColor(0.05f, 0.08f, 0.16f, 1.0f));
			}
			if (Hover == 800 + Index)
			{
				StFrame(Top, StGrow(R, 3.0f), WithAlpha(Gold, Pulse), 2.0f);
			}
		}
	}
	// The brush as it draws: its size (to the view's scale at the cursor), edge and strength.
	{
		const FBox2D R(FVector2D(StRight, 712.0f), FVector2D(StRight + StRightWidth, 812.0f));
		StRound(Back, R, 12.0f, StCard);
		P.Text(TEXT("ブラシ"), static_cast<float>(R.Min.X) + 14.0f, static_cast<float>(R.Min.Y) + 10.0f, 15.0f, Muted, ETextAlign::Left, TEXT("Bold"));
		const FVector2D C(R.GetCenter().X + 50.0, R.GetCenter().Y);
		const float Radius = 4.0f + 40.0f * GetSlider(0);
		const FLinearColor Ink2 = Tool == ETool::Eraser ? FLinearColor(1.0f, 0.6f, 0.7f, 1.0f) : StShown(Brush.Colour);
		const float Hard = FMath::Clamp(Brush.Hardness, 0.0f, 0.98f);
		constexpr int32 Rings = 8;
		for (int32 K = 0; K < Rings; ++K)
		{
			const float T = 1.0f - K / static_cast<float>(Rings);
			if (T <= Hard)
			{
				break;
			}
			P.Disc(C, Radius * T, WithAlpha(Ink2, Brush.Opacity / Rings * 1.6f));
		}
		P.Disc(C, Radius * FMath::Max(Hard, 0.08f), WithAlpha(Ink2, Brush.Opacity));
		P.Text(Tool == ETool::Eraser ? TEXT("消しゴム") : TEXT("ペン"), static_cast<float>(R.Min.X) + 14.0f, static_cast<float>(R.Min.Y) + 40.0f, 18.0f,
			Paper, ETextAlign::Left, TEXT("Black"));
	}

	// The controller's cursor (the mouse has its own).
	if (bPadCursor && Hover != 1)
	{
		const FVector2D C = Cursor;
		Top.Line(C, C + FVector2D(0.0, 22.0), Ink, 5.0f);
		Top.Line(C, C + FVector2D(15.0, 15.0), Ink, 5.0f);
		Top.Line(C, C + FVector2D(0.0, 20.0), Paper, 2.5f);
		Top.Line(C, C + FVector2D(13.0, 13.0), Paper, 2.5f);
	}

	// Notices.
	if (Now - NoticeAt < 2.5)
	{
		const float Fade = FMath::Clamp(static_cast<float>(2.5 - (Now - NoticeAt)) / 0.4f, 0.0f, 1.0f);
		const FPainter Note{Design, Elements, Layer + 4, Fade};
		const FBox2D Band(FVector2D(View.Min.X + 120.0, View.Max.Y - 62.0), FVector2D(View.Max.X - 120.0, View.Max.Y - 18.0));
		StRound(Note, Band, 12.0f, FLinearColor(0.004f, 0.006f, 0.016f, 0.9f));
		StText(Note, NoticeText, static_cast<float>(Band.GetCenter().X), static_cast<float>(Band.Min.Y) + 10.0f, 19.0f,
			static_cast<float>(Band.GetSize().X) - 24.0f, Gold, ETextAlign::Center, TEXT("Bold"));
	}

	// The keys along the bottom (the controller's when it was used last).
	Back.Box(0.0f, StBarTop, 1600.0f, 58.0f, FLinearColor(0.004f, 0.006f, 0.016f, 0.95f));
	const FString Guide = bLeaveMenu
		? FString(bPadCursor ? TEXT("↑↓：えらぶ　　A：決定　　B：つづける") : TEXT("クリック・↑↓ Enter：えらぶ　　Esc：つづける"))
		: FString(bPadCursor
			? TEXT("A：描く　　右スティック：回す　　LT / RT：拡大縮小　　LB / RB：どうぐ　　X：もどす　　Y：やりなおす　　十字：太さ・こさ　　View：左右対称　　B：おわる")
			: TEXT("左ドラッグ：描く　　右ドラッグ：回す　　中ドラッグ：ずらす　　ホイール：拡大縮小　　Ctrl+Z：もどす　　[ ]：太さ　　B E G I：どうぐ　　Esc：おわる"));
	StText(P, Guide, 800.0f, StBarTop + 16.0f, 17.0f, 1560.0f, Paper, ETextAlign::Center);

	// Leaving: keep, throw away, or go on drawing.
	if (bLeaveMenu)
	{
		const FPainter Dim{Design, Elements, Layer + 5};
		const FPainter Card{Design, Elements, Layer + 6};
		const FPainter Text{Design, Elements, Layer + 7};
		Dim.Box(-1400.0f, 0.0f, 4400.0f, 900.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.6f));
		const FBox2D R(FVector2D(520.0f, 300.0f), FVector2D(1080.0f, 628.0f));
		StRound(Card, StGrow(R, 2.0f), 20.0f, WithAlpha(Ice, 0.45f));
		StRound(Card, R, 18.0f, FLinearColor(0.025f, 0.05f, 0.13f, 1.0f));
		StText(Text, TEXT("ペイントをおわる？"), 800.0f, 326.0f, 28.0f, 520.0f, Paper, ETextAlign::Center, TEXT("Black"));
		for (int32 Index = 0; Index < 3; ++Index)
		{
			const FBox2D B = LeaveRect(Index);
			const bool bOn = LeaveChoice == Index;
			const FLinearColor Accent = Index == 0 ? Gold : Index == 1 ? Fire : Ice;
			StRound(Card, B, 12.0f, bOn ? Accent : StCard);
			StText(Text, StLeaveNames[Index], static_cast<float>(B.GetCenter().X), static_cast<float>(B.Min.Y) + 16.0f, 22.0f,
				static_cast<float>(B.GetSize().X) - 30.0f, bOn ? Ink : Paper, ETextAlign::Center, TEXT("Black"));
		}
		if (bPadCursor)
		{
			const FVector2D C = Cursor;
			Text.Line(C, C + FVector2D(0.0, 22.0), Ink, 5.0f);
			Text.Line(C, C + FVector2D(15.0, 15.0), Ink, 5.0f);
			Text.Line(C, C + FVector2D(0.0, 20.0), Paper, 2.5f);
			Text.Line(C, C + FVector2D(13.0, 13.0), Paper, 2.5f);
		}
		return Layer + 8;
	}
	return Layer + 5;
}
