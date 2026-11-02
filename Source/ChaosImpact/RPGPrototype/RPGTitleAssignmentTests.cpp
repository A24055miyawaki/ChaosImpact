#if WITH_DEV_AUTOMATION_TESTS
#include "RPGPrototype.h"
#include "RPGTitle.h"
#include "ChaosImpactScreen.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
/**
 * The RPG title with two players: how many play → the shared controller assignment (a key on the keyboard and a
 * button on a pad, pressed as a player would) → its confirm → both players pick a hero. Saved/RPGPrototypeQA/assign-*.png.
 */
class FRPGTitleAssignmentCommand : public IAutomationLatentCommand
{
public:
    explicit FRPGTitleAssignmentCommand(FAutomationTestBase* T):Test(T) {}
    bool Update() override
    {
        if(FPlatformTime::Seconds()-Started>120) { Test->AddError(FString::Printf(TEXT("Assignment test timeout at stage %d"),Stage)); Done(); return true; }
        UWorld* W=nullptr; for(const auto& C:GEngine->GetWorldContexts()) { if(C.World() && C.World()->IsGameWorld()) { W=C.World(); break; } } if(!W) { return false; }
        const auto Shot=[](const TCHAR* Name) { FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("RPGPrototypeQA"),Name),true,false); };
        const auto Key=[](const FKey& K,int32 Device,int32 User)
        {
            const FInputDeviceId Id=FInputDeviceId::CreateFromInternalId(Device);
            FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(K,FModifierKeysState(),Id,false,0,0,TOptional<int32>(User)));
            FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(K,FModifierKeysState(),Id,false,0,0,TOptional<int32>(User)));
        };
        if(Stage==0)
        {
            RPGHeroes::SetIndexSlotForTests(TEXT("CI_RPG_Heroes_QA")); RPGHeroes::Store({});
            RPGHeroes::Create(TEXT("コハク"),3,0); RPGHeroes::Create(TEXT("ミオ"),1,1);
            UGameplayStatics::OpenLevel(W,TEXT("/Game/ChaosImpact/RPGPrototype/Adventure/Lvl_RPGColonyFrontier"),true,TEXT("CIRPGPrototype=1?CIOpenWorld=1?CILocalPlayers=1?CIKeyboardPlayer=0?CIRPGTitle=1"));
            Stage=1; return false;
        }
        auto* P=Cast<ARPGPrototypeCharacter>(UGameplayStatics::GetPlayerPawn(W,0));
        auto* PC=Cast<AChaosImpactPlayerController>(UGameplayStatics::GetPlayerController(W,0));
        if(!P || !PC || !P->GetPrototypeHUD()) { return false; }
        const double Now=FPlatformTime::Seconds(); if(Now<Next) { return false; }
        auto* T=P->GetTitleWidget();
        UE_LOG(LogTemp,Display,TEXT("RPGASSIGN stage %d page %d screen %d"),Stage,T?(int32)T->GetPage():-1,(int32)PC->GetCurrentScreen());
        switch(Stage)
        {
        case 1: if(!T) { return false; } T->HandleKey(EKeys::Enter); Stage=2; Next=Now+.8; return false;
        case 2:
            Test->TestEqual(TEXT("How many play"),T->GetPage(),URPGTitleWidget::EPage::Play);
            T->SetPlayers(2); T->HandleKey(EKeys::Enter); Stage=3; Next=Now+1.0; return false;
        case 3:
            Test->TestEqual(TEXT("Two players open the shared assignment"),PC->GetCurrentScreen(),EChaosImpactScreen::ControllerAssignment);
            // 戻る first: back to how many play.
            Key(EKeys::Escape,0,0); Stage=31; Next=Now+.8; return false;
        case 31:
            Test->TestEqual(TEXT("Back from the assignment leaves it"),(int32)PC->GetCurrentScreen(),(int32)EChaosImpactScreen::Playing);
            Test->TestEqual(TEXT("Back from the assignment returns to how many play"),T->GetPage(),URPGTitleWidget::EPage::Play);
            T->SetPlayers(2); T->HandleKey(EKeys::Enter); Stage=32; Next=Now+1.0; return false;
        case 32:
            Test->TestEqual(TEXT("And into the assignment again"),PC->GetCurrentScreen(),EChaosImpactScreen::ControllerAssignment);
            Shot(TEXT("assign-1-open.png"));
            // The keyboard's player, then a pad's (as real presses).
            Key(EKeys::Enter,0,0); Stage=4; Next=Now+.6; return false;
        case 4: Key(EKeys::Gamepad_FaceButton_Bottom,1,1); Stage=5; Next=Now+.8; return false;
        case 5:
            Shot(TEXT("assign-2-joined.png"));
            Test->TestTrue(TEXT("Both players are assigned"),PC->AreControllerAssignmentsComplete());
            Key(EKeys::Enter,0,0); Stage=6; Next=Now+1.5; return false;
        case 6:
        {
            Shot(TEXT("assign-3-after-confirm.png"));
            Test->TestEqual(TEXT("The confirm leaves the assignment"),(int32)PC->GetCurrentScreen(),(int32)EChaosImpactScreen::Playing);
            Test->TestEqual(TEXT("Player 1 picks a hero"),T->GetPage(),URPGTitleWidget::EPage::Heroes);
            auto* B=Cast<ARPGPrototypeCharacter>(UGameplayStatics::GetPlayerPawn(W,1));
            Test->TestTrue(TEXT("Player 2 picks on their own screen"),B && B->IsInTitle() && B->GetTitleWidget() && B->GetTitleWidget()->GetPage()==URPGTitleWidget::EPage::Heroes);
            Done(); return true;
        }
        default: Done(); return true;
        }
    }
private:
    void Done() { RPGHeroes::SetIndexSlotForTests(FString()); }
    FAutomationTestBase* Test; int32 Stage=0; double Next=0; double Started=FPlatformTime::Seconds();
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTitleAssignmentTest,"ChaosImpact.RPGPrototype.TitleAssignment",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FRPGTitleAssignmentTest::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(FRPGTitleAssignmentCommand(this)); return true; }
#endif
