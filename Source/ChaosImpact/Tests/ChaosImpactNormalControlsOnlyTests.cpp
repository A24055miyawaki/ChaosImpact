#if WITH_DEV_AUTOMATION_TESTS
#include "ChaosImpactSettings.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNormalControlsOnlyTest, "ChaosImpact.Settings.NormalControlsOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FNormalControlsOnlyTest::RunTest(const FString&)
{
	using namespace ChaosImpactSettings;
	const int32 Profile = AddProfile(TEXT("NC") + FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(6));
	if (Profile <= 0)
	{
		AddError(TEXT("No spare test profile"));
		return false;
	}
	SetControlScheme(Profile, EChaosImpactDevice::KeyboardMouse, EChaosImpactControlScheme::MouseHold);
	SetControlScheme(Profile, EChaosImpactDevice::Xbox, EChaosImpactControlScheme::Simple);
	SetNormalControlsOnlyForTests(1);
	TestTrue(TEXT("Normal only for now"), IsNormalControlsOnly());
	for (const EChaosImpactDevice Device : {EChaosImpactDevice::KeyboardMouse, EChaosImpactDevice::Xbox, EChaosImpactDevice::Switch, EChaosImpactDevice::PlayStation})
	{
		TestTrue(TEXT("Every device plays the normal controls, whatever was saved"), GetControlScheme(Profile, Device) == EChaosImpactControlScheme::Normal);
	}
	SetNormalControlsOnlyForTests(0);
	TestTrue(TEXT("The saved keyboard style is kept for later"), GetControlScheme(Profile, EChaosImpactDevice::KeyboardMouse) == EChaosImpactControlScheme::MouseHold);
	TestTrue(TEXT("The saved pad style is kept for later"), GetControlScheme(Profile, EChaosImpactDevice::Xbox) == EChaosImpactControlScheme::Simple);
	SetNormalControlsOnlyForTests(INDEX_NONE);
	RemoveProfile(Profile);
	return true;
}
#endif
