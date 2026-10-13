#if WITH_DEV_AUTOMATION_TESTS
#include "ChaosImpactSettings.h"
#include "ChaosImpactSettingsScreen.h"
#include "ChaosImpactShoulderCameraComponent.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactLoadoutSubsystem.h"
#include "ChaosImpactMenuWidget.h"
#include "ChaosImpactPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShoulderSensitivityProfileTest,"ChaosImpact.Camera.Sensitivity.Profile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FShoulderSensitivityProfileTest::RunTest(const FString&)
{
	using namespace ChaosImpactSettings;
	const int32 Profile = AddProfile(TEXT("QS") + FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(6));
	if (Profile <= 0) { AddError(TEXT("No spare test profile")); return false; }
	const auto Mouse = EChaosImpactDevice::KeyboardMouse;
	const auto Pad = EChaosImpactDevice::Xbox;
	TestEqual(TEXT("New profile keeps default 100 percent"),GetShoulderSensitivity(Profile,Mouse),100);
	TestEqual(TEXT("Controller vertical defaults to 100"),GetShoulderSensitivity(Profile,Pad,true),100);
	SetShoulderSensitivity(Profile,Mouse,false,50);
	TestEqual(TEXT("Mouse uses one sensitivity for both axes"),GetShoulderSensitivity(Profile,Mouse,true),50);
	SetShoulderSensitivity(Profile,Pad,false,170);
	SetShoulderSensitivity(Profile,Pad,true,60);
	TestEqual(TEXT("Pad axes are independent"),GetShoulderSensitivity(Profile,Pad),170);
	TestEqual(TEXT("Pad Y is saved separately"),GetShoulderSensitivity(Profile,Pad,true),60);
	TestEqual(TEXT("PS sensitivity is independent of Xbox"),GetShoulderSensitivity(Profile,EChaosImpactDevice::PlayStation),100);
	TestEqual(TEXT("Switch sensitivity is independent of Xbox"),GetShoulderSensitivity(Profile,EChaosImpactDevice::Switch),100);
	SetShoulderSensitivity(Profile,Mouse,false,-10);
	TestEqual(TEXT("Sensitivity cannot disable camera input"),GetShoulderSensitivity(Profile,Mouse),10);
	SetShoulderSensitivity(Profile,Mouse,false,999);
	TestEqual(TEXT("Sensitivity has a safe upper bound"),GetShoulderSensitivity(Profile,Mouse),300);
	FConfigFile Disk;
	// UE 5.7 uses a logical key ("GameUserSettings"), not a disk path, for known configs.
	const auto* Branch = GConfig->FindBranch(*GGameUserSettingsIni,GGameUserSettingsIni);
	Disk.Read(Branch ? Branch->IniPath : GGameUserSettingsIni);
	TestTrue(TEXT("Saved settings can be read from disk"),Disk.Num()>0);
	int32 SavedX = 0, SavedY = 0;
	const FString Section = FString::Printf(TEXT("ChaosImpact.Profile%d"),Profile);
	Disk.GetInt(*Section,TEXT("Xbox_ShoulderSensitivityX"),SavedX);
	Disk.GetInt(*Section,TEXT("Xbox_ShoulderSensitivityY"),SavedY);
	TestEqual(TEXT("Horizontal sensitivity is actually written to disk"),SavedX,170);
	TestEqual(TEXT("Vertical sensitivity is actually written to disk"),SavedY,60);
	ResetShoulderSensitivity(Profile,Pad);
	TestEqual(TEXT("Reset returns horizontal to 100"),GetShoulderSensitivity(Profile,Pad),100);
	TestEqual(TEXT("Reset returns vertical to 100"),GetShoulderSensitivity(Profile,Pad,true),100);
	TestEqual(TEXT("Reset affects only the selected device"),GetShoulderSensitivity(Profile,Mouse),300);
	const auto* Defaults = GetDefault<UChaosImpactShoulderCameraComponent>();
	TestEqual(TEXT("New standard mouse sensitivity is twice the original"),Defaults->MouseSensitivity,.28f);
	TestEqual(TEXT("New standard horizontal pad sensitivity is twice the original"),Defaults->PadYawRate,320.f);
	TestEqual(TEXT("New standard vertical pad sensitivity is twice the original"),Defaults->PadPitchRate,200.f);
	RemoveProfile(Profile);
	return true;
}

namespace
{
	class FShoulderSensitivityMenuCommand : public IAutomationLatentCommand
	{
	public:
		explicit FShoulderSensitivityMenuCommand(FAutomationTestBase* InTest) : Test(InTest) {}
		bool Update() override
		{
			using namespace ChaosImpactSettings;
			using Screen = UChaosImpactSettingsScreen;
			AChaosImpactPlayerController* PC = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{ if (Context.World() && Context.World()->IsGameWorld()) { PC = Cast<AChaosImpactPlayerController>(Context.World()->GetFirstPlayerController()); } }
			auto* Player = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			auto* Settings = PC && PC->GetMenuWidget() ? PC->GetMenuWidget()->GetSettingsScreen() : nullptr;
			const double Now = FPlatformTime::Seconds();
			if (Now - Started > 60) { Test->AddError(TEXT("Sensitivity menu timed out")); Cleanup(PC); return true; }
			if (!Player || !Settings || Now < NextAt) { return false; }
			auto* Camera = Player->GetShoulderCamera();
			const auto Shot = [PC](const TCHAR* Name)
			{ FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("SensitivityQA"),Name),true,false); };
			const auto Next = [this,Now]() { ++Stage; NextAt = Now + .3; return false; };
			switch (Stage)
			{
			case 0:
				if (!PC->IsGameplayActive()) { return false; }
				Profile = AddProfile(TEXT("QT") + FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(6));
				if (Profile <= 0) { Test->AddError(TEXT("No spare profile")); return true; }
				OriginalName = UChaosImpactLoadoutSubsystem::Get(PC)->GetLoadout(0).Nickname;
				UChaosImpactLoadoutSubsystem::Get(PC)->SetNickname(0,GetProfile(Profile).Name);
				Device = GetPlayerDevice(PC);
				Input = PC->IsUsingGamepad() ? FVector2D(.8,.6) : FVector2D(10,4);
				Before = Camera->GetLookDegreesForPlayer(PC,Input,.016f);
				PC->TogglePauseMenu(); PC->OpenSettings(); Settings->SetTab(Screen::ETab::Controls);
				Settings->EditControls(Profile,Device);
				Test->TestTrue(TEXT("Sensitivity row is available in controls"),Settings->FindRow(Screen::RowShoulderSensitivityX) >= 0);
				Settings->SelectRow(Settings->FindRow(Screen::RowShoulderSensitivityX)); Settings->Adjust(1);
				Test->TestEqual(TEXT("Right key raises sensitivity by ten percent"),GetShoulderSensitivity(Profile,Device),110);
				Test->TestTrue(TEXT("Gameplay input immediately reads new sensitivity"),
					FMath::IsNearlyEqual(Camera->GetLookDegreesForPlayer(PC,Input,.016f).X,Before.X * 1.1,0.001));
				if (PC->IsUsingGamepad())
				{
					Settings->SelectRow(Settings->FindRow(Screen::RowShoulderSensitivityY)); Settings->Adjust(-1);
					Test->TestTrue(TEXT("Vertical pad setting affects only vertical look"),
						FMath::IsNearlyEqual(Camera->GetLookDegreesForPlayer(PC,Input,.016f).Y,Before.Y * .9,0.001));
				}
				Shot(PC->IsUsingGamepad() ? TEXT("Pad.png") : TEXT("Mouse.png")); return Next();
			case 1:
				// Settings use the same keyboard/pad adjustment and mouse-value buttons.
				Settings->SelectRow(0); Settings->SelectRow(Settings->FindRow(Screen::RowShoulderSensitivityX));
				Settings->HandleMouseDown(FVector2D(1200,722),EKeys::LeftMouseButton);
				Test->TestEqual(TEXT("Mouse can increase sensitivity too"),GetShoulderSensitivity(Profile,Device),120);
				PC->CloseSettings(); PC->OpenSettings(); Settings->SetTab(Screen::ETab::Controls);
				Test->TestEqual(TEXT("Reopening settings keeps the saved value"),GetShoulderSensitivity(Profile,Device),120);
				Settings->SelectRow(Settings->FindRow(Screen::RowResetShoulderSensitivity)); Settings->Activate();
				Test->TestEqual(TEXT("Menu reset returns to standard sensitivity"),GetShoulderSensitivity(Profile,Device),100);
				Test->TestTrue(TEXT("Reset is also immediately reflected in camera input"),Camera->GetLookDegreesForPlayer(PC,Input,.016f).Equals(Before,.001));
				// Device settings remain present for the simple style, not just custom normal bindings.
				SetControlScheme(Profile,Device,EChaosImpactControlScheme::Simple); Settings->EditControls(Profile,Device);
				Test->TestTrue(TEXT("Simple style still offers sensitivity"),Settings->FindRow(Screen::RowShoulderSensitivityX) >= 0);
				Cleanup(PC); return true;
			}
			return false;
		}
	private:
		void Cleanup(AChaosImpactPlayerController* PC)
		{
			if (PC)
			{
				if (PC->GetCurrentScreen() == EChaosImpactScreen::Settings) { PC->CloseSettings(); }
				if (!PC->IsGameplayActive()) { PC->ResumeGameplay(); }
				if (Profile > 0) { UChaosImpactLoadoutSubsystem::Get(PC)->SetNickname(0,OriginalName); }
			}
			if (Profile > 0) { ChaosImpactSettings::RemoveProfile(Profile); Profile = -1; }
		}
		FAutomationTestBase* Test;
		int32 Stage = 0, Profile = -1;
		double Started = FPlatformTime::Seconds(), NextAt = 0;
		FString OriginalName;
		EChaosImpactDevice Device = EChaosImpactDevice::KeyboardMouse;
		FVector2D Input, Before;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShoulderSensitivityMenuTest,"ChaosImpact.Camera.Sensitivity.Menu",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FShoulderSensitivityMenuTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FShoulderSensitivityMenuCommand(this));
	return true;
}
#endif
