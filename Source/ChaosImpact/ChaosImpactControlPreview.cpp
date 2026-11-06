#include "ChaosImpactControlPreview.h"
#include "ChaosImpactPaint.h"

void ChaosImpactControlPreview::Paint(const FGeometry& Geometry, FSlateWindowElementList& Elements, int32 Layer,
	int32 Profile, EChaosImpactDevice Device, EChaosImpactControlScheme Scheme, double VideoTime,
	const FSlateBrush* VideoBrush, bool bVideoFailed)
{
	using namespace ChaosImpactPaint;
	using namespace ChaosImpactSettings;
	// (Shadows, bodies, what sits on them and words each on their own layer: Slate may reorder different brushes within one.)
	const FPainter Shade{Geometry, Elements, Layer}, Back{Geometry, Elements, Layer + 1};
	const FPainter P{Geometry, Elements, Layer + 2}, Mid{Geometry, Elements, Layer + 3}, Up{Geometry, Elements, Layer + 4}, Text{Geometry, Elements, Layer + 5};
	const bool bPad = Device != EChaosImpactDevice::KeyboardMouse;
	const bool bSimple = Scheme == EChaosImpactControlScheme::Simple;
	const bool bMouse = IsMouseOnlyScheme(Scheme), bHold = Scheme == EChaosImpactControlScheme::MouseHold;
	// The diagram follows the video: moving for its first two seconds, then a charged throw.
	const bool bMove = VideoTime < 2, bCharge = VideoTime >= 2 && VideoTime < 3.25;
	const auto KeyName = [=](EChaosImpactAction Action)
	{
		const FKey Primary = GetSchemeKey(Profile, Device, Scheme, Action, 0);
		return GetKeyName(Primary.IsValid() ? Primary : GetSchemeKey(Profile, Device, Scheme, Action, 1), Device);
	};

	// ---- The card: the device drawn on the left, what each key does on the right.
	constexpr float CardL = 640.0f, CardT = 140.0f, CardW = 900.0f, CardH = 224.0f;
	Shade.Box(CardL + 10.0f, CardT + 12.0f, CardW, CardH, FLinearColor(0.0f, 0.0f, 0.0f, 0.45f));
	HudPoly(Back, {FVector2D(CardL, CardT), FVector2D(CardL + CardW, CardT), FVector2D(CardL + CardW, CardT + CardH), FVector2D(CardL, CardT + CardH)},
		MakeFill(FLinearColor(0.045f, 0.065f, 0.12f, 0.97f), FLinearColor(0.008f, 0.012f, 0.026f, 0.97f)));
	HudPoly(Up, {FVector2D(CardL, CardT), FVector2D(CardL + CardW, CardT), FVector2D(CardL + CardW, CardT + 5.0f), FVector2D(CardL, CardT + 5.0f)},
		MakeFill(FMath::Lerp(Ice, Paper, 0.35f), Ice));
	Up.Outline(CardL, CardT, CardW, CardH, WithAlpha(Paper, 0.1f), 1.0f);
	const FString DeviceName = bPad ? TEXT("コントローラー") : TEXT("キーボード・マウス");
	const float ChipW = HudMeasure(DeviceName, 15.0f, TEXT("Black")) + 26.0f;
	RoundedBox(Up, CardL + 24.0f, CardT + 20.0f, ChipW, 28.0f, 14.0f, WithAlpha(Ice, 0.3f));
	Text.Text(DeviceName, CardL + 24.0f + ChipW * 0.5f, CardT + 24.0f, 15.0f, Paper, ETextAlign::Center, TEXT("Black"));
	const FString Summary = bPad ? (bSimple ? TEXT("左スティックで移動もねらいも") : TEXT("左スティックで移動・右スティックでねらう"))
		: bMouse ? (bHold ? TEXT("右を押している方向へ歩く") : TEXT("右クリックした所まで歩く")) : TEXT("キーで移動・マウスでねらう");
	Text.Text(Summary, CardL + 36.0f + ChipW, CardT + 23.0f, HudFit(Summary, 18.0f, CardW - 60.0f - ChipW, TEXT("Black")), Ice, ETextAlign::Left, TEXT("Black"));

	const FLinearColor KeyFace(0.1f, 0.13f, 0.2f, 1.0f);
	if (bPad)
	{
		// A pad: grips, two sticks (the left one moving with the video), the face buttons, the shoulder lit on a charge.
		const FVector2D C(CardL + 200.0f, CardT + 140.0f);
		Mid.Disc(C + FVector2D(-110.0, 22.0), 44.0f, WithAlpha(Paper, 0.92f));
		Mid.Disc(C + FVector2D(110.0, 22.0), 44.0f, WithAlpha(Paper, 0.92f));
		RoundedBox(Mid, C.X - 150.0f, C.Y - 48.0f, 300.0f, 84.0f, 34.0f, WithAlpha(Paper, 0.92f));
		RoundedBox(Up, C.X - 130.0f, C.Y - 60.0f, 64.0f, 14.0f, 6.0f, Muted);
		RoundedBox(Up, C.X + 66.0f, C.Y - 60.0f, 64.0f, 14.0f, 6.0f, bCharge && !bSimple ? Gold : Muted);
		Up.Disc(C + FVector2D(-92.0, -12.0), 24.0f, Ink);
		Up.Disc(C + FVector2D(-92.0, -12.0) + (bMove ? FVector2D(11.0, -6.0) : FVector2D::ZeroVector), 14.0f, Ice);
		Up.Disc(C + FVector2D(40.0, 18.0), 21.0f, Ink);
		Up.Disc(C + FVector2D(40.0, 18.0), 12.0f, bSimple ? Muted : Ice);
		Up.Line(C + FVector2D(-52.0, 18.0), C + FVector2D(-24.0, 18.0), Ink, 8.0f);
		Up.Line(C + FVector2D(-38.0, 4.0), C + FVector2D(-38.0, 32.0), Ink, 8.0f);
		const FKey Faces[] = {EKeys::Gamepad_FaceButton_Bottom, EKeys::Gamepad_FaceButton_Right, EKeys::Gamepad_FaceButton_Left, EKeys::Gamepad_FaceButton_Top};
		const FVector2D At[] = {{106.0, 22.0}, {130.0, -2.0}, {82.0, -2.0}, {106.0, -26.0}};
		for (int32 I = 0; I < 4; ++I)
		{
			const bool bLit = bCharge && GetSchemeKey(Profile, Device, Scheme, EChaosImpactAction::Throw, 0) == Faces[I];
			Up.Disc(C + At[I], 15.0f, bLit ? Gold : Ink);
			Text.Text(GetKeyName(Faces[I], Device), C.X + At[I].X, C.Y + At[I].Y - 10.0f, 15.0f, bLit ? Ink : Paper, ETextAlign::Center, TEXT("Black"));
		}
	}
	else
	{
		// The keys (lit as the video moves) and the mouse (its buttons lit as they are used).
		const float BaseX = CardL + (bMouse ? 150.0f : 46.0f);
		if (!bMouse)
		{
			const EChaosImpactAction Actions[] = {EChaosImpactAction::MoveUp, EChaosImpactAction::MoveLeft, EChaosImpactAction::MoveDown, EChaosImpactAction::MoveRight};
			const FVector2D At[] = {{58.0, 70.0}, {0.0, 128.0}, {58.0, 128.0}, {116.0, 128.0}};
			for (int32 I = 0; I < 4; ++I)
			{
				const bool bLit = bMove && I == 3;
				RoundedBox(P, BaseX + At[I].X + 3.0f, CardT + At[I].Y + 5.0f, 52.0f, 52.0f, 10.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.5f));
				RoundedBox(Mid, BaseX + At[I].X, CardT + At[I].Y, 52.0f, 52.0f, 10.0f, bLit ? Ice : KeyFace);
				Up.Outline(BaseX + At[I].X, CardT + At[I].Y, 52.0f, 52.0f, WithAlpha(Paper, 0.35f), 1.5f);
				const FString Key = GetKeyName(GetSchemeKey(Profile, Device, Scheme, Actions[I], 0), Device);
				Text.Text(Key, BaseX + At[I].X + 26.0f, CardT + At[I].Y + 12.0f, HudFit(Key, 20.0f, 44.0f, TEXT("Black")), Paper, ETextAlign::Center, TEXT("Black"));
			}
		}
		const float MX = bMouse ? BaseX : BaseX + 210.0f;
		const float MY = CardT + 64.0f;
		RoundedBox(P, MX + 4.0f, MY + 6.0f, 96.0f, 132.0f, 44.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.5f));
		RoundedBox(Mid, MX, MY, 96.0f, 132.0f, 44.0f, WithAlpha(Paper, 0.92f));
		RoundedBox(Up, MX + 5.0f, MY + 5.0f, 40.0f, 58.0f, 18.0f, bCharge ? Gold : KeyFace);
		RoundedBox(Up, MX + 51.0f, MY + 5.0f, 40.0f, 58.0f, 18.0f, bMouse && (bHold ? bMove : VideoTime < 0.15) ? Ice : KeyFace);
		RoundedBox(Text, MX + 43.0f, MY + 12.0f, 10.0f, 26.0f, 5.0f, Muted);
	}
	// What each key does: a key cap and its action.
	struct FLine
	{
		FString Key;
		FString Does;
		FLinearColor Color;
	};
	const FLine Lines[] = {
		{KeyName(EChaosImpactAction::Throw), TEXT("投球（長押しでチャージ）"), Gold},
		{KeyName(EChaosImpactAction::Dash), TEXT("ダッシュ"), Paper},
		{bMouse ? FString(TEXT("ホイール")) : KeyName(EChaosImpactAction::SwapBall), TEXT("ボールの持ち替え"), Paper},
		{bMouse ? FString(TEXT("球の枠を右長押し")) : bSimple ? KeyName(EChaosImpactAction::SwapBall) + TEXT(" 長押し") : KeyName(EChaosImpactAction::DropBall),
			TEXT("ボールを落とす"), Paper}};
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const float Y = CardT + 66.0f + Index * 38.0f;
		const float KeyW = FMath::Clamp(HudMeasure(Lines[Index].Key, 15.0f, TEXT("Black")) + 22.0f, 44.0f, 210.0f);
		const float KeyX = CardL + 470.0f;
		RoundedBox(Up, KeyX, Y, KeyW, 28.0f, 7.0f, Index == 0 ? WithAlpha(Gold, 0.9f) : KeyFace);
		Up.Outline(KeyX, Y, KeyW, 28.0f, WithAlpha(Paper, 0.25f), 1.0f);
		Text.Text(Lines[Index].Key, KeyX + KeyW * 0.5f, Y + 4.0f, HudFit(Lines[Index].Key, 15.0f, KeyW - 14.0f, TEXT("Black")), Index == 0 ? Ink : Paper,
			ETextAlign::Center, TEXT("Black"));
		Text.Text(Lines[Index].Does, KeyX + KeyW + 14.0f, Y + 3.0f, HudFit(Lines[Index].Does, 18.0f, CardL + CardW - 24.0f - (KeyX + KeyW + 14.0f), TEXT("Bold")),
			Lines[Index].Color, ETextAlign::Left, TEXT("Bold"));
	}

	// ---- The real play, 16:9 (no sound).
	constexpr float VideoL = 730.0f, VideoT = 384.0f, VideoW = 720.0f, VideoH = 405.0f;
	Shade.Box(VideoL + 10.0f, VideoT + 12.0f, VideoW, VideoH, FLinearColor(0.0f, 0.0f, 0.0f, 0.5f));
	Back.Box(VideoL, VideoT, VideoW, VideoH, Ink);
	if (VideoBrush)
	{
		FSlateDrawElement::MakeBox(Elements, Layer + 6,
			Geometry.ToPaintGeometry(FVector2f(VideoW, VideoH), FSlateLayoutTransform(FVector2f(VideoL, VideoT))),
			VideoBrush, ESlateDrawEffect::None, FLinearColor::White);
	}
	else
	{
		const FString Waiting = bVideoFailed ? TEXT("プレイ映像を読み込めません") : TEXT("プレイ映像を読み込み中…");
		Text.Text(Waiting, VideoL + VideoW * 0.5f, VideoT + VideoH * 0.5f - 12.0f, 22.0f, Muted, ETextAlign::Center, TEXT("Bold"));
	}
	const FPainter Cover{Geometry, Elements, Layer + 7}, Caption{Geometry, Elements, Layer + 8};
	RoundedBox(Cover, VideoL + 12.0f, VideoT + 12.0f, 150.0f, 28.0f, 14.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.65f));
	Cover.Disc(FVector2D(VideoL + 30.0f, VideoT + 26.0), 5.0f, Fire);
	Caption.Text(TEXT("実際のプレイ"), VideoL + 44.0f, VideoT + 16.0f, 15.0f, Paper, ETextAlign::Left, TEXT("Black"));
	Cover.Outline(VideoL, VideoT, VideoW, VideoH, WithAlpha(Paper, 0.25f), 1.5f);
}
