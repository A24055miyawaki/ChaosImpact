#include "ChaosImpactControlPreview.h"
#include "ChaosImpactPaint.h"

void ChaosImpactControlPreview::Paint(const FGeometry& Geometry, FSlateWindowElementList& Elements, int32 Layer,
	int32 Profile, EChaosImpactDevice Device, EChaosImpactControlScheme Scheme, double VideoTime,
	const FSlateBrush* VideoBrush, bool bVideoFailed)
{
	using namespace ChaosImpactPaint;
	using namespace ChaosImpactSettings;
	const FPainter P{Geometry,Elements,Layer}, Text{Geometry,Elements,Layer + 1};
	const bool bPad = Device != EChaosImpactDevice::KeyboardMouse;
	const bool bSimple = Scheme == EChaosImpactControlScheme::Simple;
	const bool bMouse = IsMouseOnlyScheme(Scheme), bHold = Scheme == EChaosImpactControlScheme::MouseHold;
	const bool bMove = VideoTime < 2, bCharge = VideoTime >= 2 && VideoTime < 3.25;
	const auto KeyName = [=](EChaosImpactAction Action)
	{
		const FKey Primary = GetSchemeKey(Profile,Device,Scheme,Action,0);
		return GetKeyName(Primary.IsValid() ? Primary : GetSchemeKey(Profile,Device,Scheme,Action,1),Device);
	};
	Text.Text(bPad ? TEXT("コントローラー") : TEXT("キーボード・マウス"),742,196,23,Paper);
	if (bPad)
	{
		P.Disc({810,299},34,Paper); P.Disc({1010,299},34,Paper);
		RoundedBox(P,781,234,260,71,27,Paper);
		RoundedBox(P,796,224,53,12,5,Muted); RoundedBox(P,973,224,53,12,5,bCharge && !bSimple ? Gold : Muted);
		P.Disc({823,262},22,Ink); P.Disc(FVector2D(823,262) + (bMove ? FVector2D(10,-5) : FVector2D::ZeroVector),12,Ice);
		P.Disc({937,290},19,Ink); P.Disc({937,290},11,bSimple ? Muted : Ice);
		P.Line({852,294},{876,294},Ink,7); P.Line({864,282},{864,306},Ink,7);
		const FKey Faces[] = {EKeys::Gamepad_FaceButton_Bottom,EKeys::Gamepad_FaceButton_Right,EKeys::Gamepad_FaceButton_Left,EKeys::Gamepad_FaceButton_Top};
		const FVector2D At[] = {{1000,281},{1021,260},{979,260},{1000,239}};
		for (int32 I = 0; I < 4; ++I)
		{
			P.Disc(At[I],14,bCharge && GetSchemeKey(Profile,Device,Scheme,EChaosImpactAction::Throw,0) == Faces[I] ? Gold : Ink);
			Text.Text(GetKeyName(Faces[I],Device),At[I].X,At[I].Y - 10,16,Paper,ETextAlign::Center);
		}
		Text.Text(bSimple ? TEXT("左で移動・照準") : TEXT("左で移動／右で照準"),742,332,20,Ice);
	}
	else
	{
		if (!bMouse)
		{
			const EChaosImpactAction Actions[] = {EChaosImpactAction::MoveUp,EChaosImpactAction::MoveLeft,EChaosImpactAction::MoveDown,EChaosImpactAction::MoveRight};
			const FVector2D At[] = {{814,225},{766,273},{814,273},{862,273}};
			for (int32 I = 0; I < 4; ++I)
			{
				P.Box(At[I].X,At[I].Y,42,42,bMove && I == 3 ? Ice : Ink);
				P.Outline(At[I].X,At[I].Y,42,42,Paper,2);
				Text.Text(GetKeyName(GetSchemeKey(Profile,Device,Scheme,Actions[I],0),Device),At[I].X + 21,At[I].Y + 8,20,Paper,ETextAlign::Center);
			}
		}
		const float X = bMouse ? 847 : 962;
		RoundedBox(P,X,225,75,101,32,Paper);
		RoundedBox(P,X + 3,228,32,46,14,bCharge ? Gold : Ink);
		RoundedBox(P,X + 40,228,32,46,14,bMouse && (bHold ? bMove : VideoTime < 0.15) ? Ice : Ink);
		RoundedBox(P,X + 33,233,9,21,4,Muted);
		Text.Text(bMouse ? bHold ? TEXT("右を押しながら方向移動") : TEXT("右クリックで目的地へ移動") : TEXT("キーで移動／マウスで照準"),742,332,20,Ice);
	}
	Text.Text(KeyName(EChaosImpactAction::Throw) + TEXT("：投球"),1105,226,20,Gold);
	Text.Text(KeyName(EChaosImpactAction::Dash) + TEXT("：ダッシュ"),1105,263,20,Paper);
	Text.Text(bMouse ? TEXT("ホイール：持ち替え") : KeyName(EChaosImpactAction::SwapBall) + TEXT("：持ち替え"),1105,300,20,Paper);
	Text.Text(bMouse ? TEXT("球の枠を右長押し：落とす") : bSimple ? KeyName(EChaosImpactAction::SwapBall) + TEXT("長押し：落とす") : KeyName(EChaosImpactAction::DropBall) + TEXT("：落とす"),1105,337,19,Paper);

	// Full 16:9 playback, not a drawn imitation of gameplay. No audio component is created.
	P.Box(742,365,720,405,Ink);
	if (VideoBrush)
	{
		FSlateDrawElement::MakeBox(Elements,Layer + 2,
			Geometry.ToPaintGeometry(FVector2f(720,405),FSlateLayoutTransform(FVector2f(742,365))),
			VideoBrush,ESlateDrawEffect::None,FLinearColor::White);
	}
	else { Text.Text(bVideoFailed ? TEXT("プレイ映像を読み込めません") : TEXT("プレイ映像を読み込み中…"),1102,551,23,Paper,ETextAlign::Center); }
	const FPainter Cover{Geometry,Elements,Layer + 3}, Caption{Geometry,Elements,Layer + 4};
	Cover.Box(742,365,720,35,FLinearColor(0,0,0,0.7f));
	Caption.Text(TEXT("実プレイ映像・自動再生"),756,369,20,Paper);
	Cover.Outline(742,365,720,405,WithAlpha(Paper,0.5f),2);
}
