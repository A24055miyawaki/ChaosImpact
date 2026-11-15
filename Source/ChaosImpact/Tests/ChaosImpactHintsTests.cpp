#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactHints.h"
#include "ChaosImpactSettings.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactHintTextTest, "ChaosImpact.Menu.HintText",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

/** Hint templates in each device's own names (and only that device's): keyboard, Xbox, PlayStation, Switch. */
bool FChaosImpactHintTextTest::RunTest(const FString& Parameters)
{
	using namespace ChaosImpactHints;
	using EDevice = EChaosImpactDevice;
	const bool bSwap = ChaosImpactSettings::IsSwitchConfirmRight();
	const FString Line = TEXT("{Confirm}：決定　{Back}：もどる　{LB|Q} / {RB|E}：タブ　{pad:{DPad}：えらぶ}{kb:クリック：えらぶ}　{Y|N}：名前");
	TestEqual(TEXT("Keyboard"), Text(EDevice::KeyboardMouse, Line), FString(TEXT("Enter：決定　Esc：もどる　Q / E：タブ　クリック：えらぶ　N：名前")));
	TestEqual(TEXT("Xbox"), Text(EDevice::Xbox, Line), FString(TEXT("A：決定　B：もどる　LB / RB：タブ　十字キー：えらぶ　Y：名前")));
	TestEqual(TEXT("PlayStation"), Text(EDevice::PlayStation, Line), FString(TEXT("×：決定　○：もどる　L1 / R1：タブ　方向キー：えらぶ　△：名前")));
	TestEqual(TEXT("Switch"), Text(EDevice::Switch, Line), bSwap
		? FString(TEXT("A：決定　B：もどる　L / R：タブ　十字ボタン：えらぶ　X：名前"))
		: FString(TEXT("B：決定　A：もどる　L / R：タブ　十字ボタン：えらぶ　X：名前")));
	TestEqual(TEXT("Keyboard defaults"), Text(EDevice::KeyboardMouse, TEXT("{LStick}：移動　{RStick}：ねらう　{Up}{Down}")),
		FString(TEXT("WASD：移動　マウス：ねらう　↑↓")));
	TestEqual(TEXT("No braces: as it is"), Text(EDevice::Xbox, TEXT("そのまま")), FString(TEXT("そのまま")));
	return true;
}

#endif
