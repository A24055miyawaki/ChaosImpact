#pragma once

#include "CoreMinimal.h"
#include "ChaosImpactBodyPaint.h"
#include "InputCoreTypes.h"
#include "Styling/SlateBrush.h"
#include "UObject/Object.h"
#include "ChaosImpactPaintStudio.generated.h"

class AChaosImpactCharacterPreview;
class AChaosImpactPlayerController;
class FSlateWindowElementList;
class UChaosImpactPaintLayer;
struct FAnalogInputEvent;
struct FGeometry;
struct FKeyEvent;

/**
 * ペイント (カラーエディットの中): drawing freely on the character itself. The model large in the middle, held still,
 * the camera going round it (turned, slid and brought close); on the left the tools (ペン, 消しゴム, バケツ, スポイト),
 * the brush's size, edge and strength, 左右対称, もどす / やりなおす; on the right the colours. A mouse, the keys and a
 * controller (a cursor on the left stick) all work it the same way.
 */
UCLASS()
class UChaosImpactPaintStudio : public UObject
{
	GENERATED_BODY()
public:
	enum class ETool : uint8 { Pen, Eraser, Fill, Picker };
	enum class EResult : uint8 { None, Done, Cancelled };
	static constexpr int32 ToolCount = 4;

	/** StartYaw: the model's facing on the カラーエディット (the camera starts from where it was seen). */
	void Open(AChaosImpactPlayerController* InController, AChaosImpactCharacterPreview* InPreview, const TArray<UChaosImpactPaintLayer*>& InLayers,
		const FColor& StartColour, float StartYaw);
	bool IsOpen() const { return bOpen; }
	/** Once, after it has closed: Done (the pictures as drawn) or Cancelled (put back as they were). */
	EResult ConsumeResult();
	/** Something was drawn, rubbed out or taken back since it opened. */
	bool HasChanges() const { return bChanged; }
	void Tick(float DeltaSeconds);
	int32 Paint(const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer) const;
	bool HandleKeyDown(const FKeyEvent& Event);
	bool HandleKeyUp(const FKeyEvent& Event);
	bool HandleAnalog(const FAnalogInputEvent& Event);
	void HandleMouseDown(const FVector2D& Point, const FKey& Button, bool bAlt);
	void HandleMouseUp(const FVector2D& Point, const FKey& Button);
	void HandleMouseMove(const FVector2D& Point);
	void HandleWheel(const FVector2D& Point, float Delta, bool bCtrl);

	// For tests and the screen's own buttons.
	void SetTool(ETool InTool);
	ETool GetTool() const { return Tool; }
	FChaosImpactBrush& GetBrush() { return Brush; }
	void SetColour(const FColor& Colour, bool bRemember = true);
	/** A stroke from one point of the view to another (design space), as a drag with the tool would draw it. */
	void DrawLine(const FVector2D& From, const FVector2D& To);
	/** The tool used once at a point of the view (a dab, a fill, a colour picked). */
	void ClickView(const FVector2D& Point);
	void Undo();
	void Redo();
	void ClearAll();
	/** Closes: bKeep, the pictures as drawn; otherwise put back as they were when it opened. */
	void Finish(bool bKeep);
	void SetView(float Yaw, float Pitch, float Zoom, const FVector2D& Pan = FVector2D::ZeroVector);
	void ResetView();
	bool CanPaint() const { return Surface.IsBuilt(); }
	/** The view's box on the screen (1600 x 900 design space). */
	static FBox2D GetViewRect();
	/** The middle of the model on the screen (for tests). */
	FVector2D GetModelCentreOnScreen() const;
	/** The brush's size on its 0..1 slider and back (world units). */
	static float SizeToRadius(float Amount);
	static float RadiusToSize(float Radius);

private:
	/** What is at a point: 1 the view, 100 + n a tool, 200 + n a slider, 300 左右対称, 400 + n a button, 500 + n 完了 / やめる,
	 * 600 + n a palette colour, 700 the square, 701 the hue bar, 800 + n a recent colour, 900 + n the leaving menu's choices. */
	int32 HitTest(const FVector2D& Point) const;
	void Press(const FVector2D& Point, bool bAlt);
	void Release();
	void DragTo(const FVector2D& Point);
	void PaintAlong(const FVector2D& From, const FVector2D& To, bool bFirst);
	bool HitAt(const FVector2D& Point, FChaosImpactPaintSurface::FHit& OutHit, FVector& OutDirection) const;
	bool ToPicture(const FVector2D& Point, FVector2D& OutUV) const;
	void PickAt(const FVector2D& Point);
	void SetSlider(int32 Slider, float Amount);
	float GetSlider(int32 Slider) const;
	void PickSquare(const FVector2D& Point);
	void PickHue(const FVector2D& Point);
	void SyncHSB();
	void ApplyView();
	void Notify(const FString& Text);
	void Button(int32 Index);
	void CycleTool(int32 Direction);
	void OpenLeaveMenu();
	void ChooseLeave(int32 Choice);
	/** Where the brush reaches on screen round the cursor (its outline's radius in design units; 0 off the model). */
	void UpdateCursorRing();
	const TArray<UChaosImpactPaintLayer*>& LiveLayers() const { return Layers; }

	// Layout (1600 x 900 design space).
	static FBox2D ToolRect(int32 Index);
	static FBox2D SliderTrack(int32 Index);
	static FBox2D MirrorRect();
	static FBox2D ButtonRect(int32 Index);
	static FBox2D FinishRect(int32 Index);
	static FBox2D SwatchRect(int32 Index);
	static FBox2D SquareRect();
	static FBox2D HueRect();
	static FBox2D RecentRect(int32 Index);
	static FBox2D LeaveRect(int32 Index);

	TWeakObjectPtr<AChaosImpactPlayerController> Controller;
	UPROPERTY(Transient)
	TObjectPtr<AChaosImpactCharacterPreview> Preview;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UChaosImpactPaintLayer>> LayerObjects;
	TArray<UChaosImpactPaintLayer*> Layers;
	/** The pictures as they were when it opened (やめる puts them back). */
	TArray<TArray<FColor>> Before;
	FChaosImpactPaintSurface Surface;
	FSlateBrush PictureBrush;
	bool bOpen = false;
	EResult Result = EResult::None;
	bool bChanged = false;
	double OpenedAt = 0.0;

	ETool Tool = ETool::Pen;
	/** スポイト goes back to the tool it was picked up from. */
	ETool ToolBeforePicker = ETool::Pen;
	FChaosImpactBrush Brush;
	float EditHue = 0.0f;
	float EditSaturation = 1.0f;
	float EditBrightness = 1.0f;

	// The view.
	float ViewYaw = 90.0f;
	float ViewPitch = 8.0f;
	float ViewZoom = 1.0f;
	FVector2D ViewPan = FVector2D::ZeroVector;
	float StartYaw = 90.0f;

	// The pointer: the mouse, or the controller's cursor.
	FVector2D Cursor = FVector2D(752.0, 468.0);
	bool bPadCursor = false;
	bool bPenDown = false;
	FVector2D LastPaintPoint = FVector2D::ZeroVector;
	/** What a press is dragging (a slider, the square, the hue bar; INDEX_NONE none). */
	int32 Dragging = INDEX_NONE;
	bool bTurning = false;
	bool bSliding = false;
	FVector2D LastMouse = FVector2D::ZeroVector;
	float CursorRing = 0.0f;
	bool bCursorOnModel = false;
	FVector2D MirrorCursor = FVector2D::ZeroVector;
	bool bMirrorCursorShown = false;
	FVector2D LeftStick = FVector2D::ZeroVector;
	FVector2D RightStick = FVector2D::ZeroVector;
	float ZoomIn = 0.0f;
	float ZoomOut = 0.0f;
	bool bPadA = false;

	bool bLeaveMenu = false;
	int32 LeaveChoice = 0;
	bool bCancelArmed = false;
	FString NoticeText;
	double NoticeAt = -100.0;
};
