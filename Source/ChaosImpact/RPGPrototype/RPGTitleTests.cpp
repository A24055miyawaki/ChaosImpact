#if WITH_DEV_AUTOMATION_TESTS
#include "RPGPrototype.h"
#include "RPGTitle.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Components/EditableText.h"

namespace
{
class FRPGTitleCommand : public IAutomationLatentCommand
{
public:
    explicit FRPGTitleCommand(FAutomationTestBase* T):Test(T) {}
    bool Update() override
    {
        if(FPlatformTime::Seconds()-Started>150) { Test->AddError(FString::Printf(TEXT("Title test timeout %d"),Stage)); Cleanup(); return true; }
        UWorld* W=nullptr; for(const auto& C:GEngine->GetWorldContexts()) { if(C.World() && C.World()->IsGameWorld()) { W=C.World(); break; } } if(!W) { return false; }
        const auto Shot=[](const TCHAR* Name) { FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("RPGPrototypeQA"),Name),true,false); };
        if(Stage==0)
        {
            RPGHeroes::SetIndexSlotForTests(TEXT("CI_RPG_Heroes_QA")); RPGHeroes::Store({});
            Existing=RPGHeroes::Create(TEXT("コハク"),3,0).Id; RPGHeroes::Create(TEXT("ミオ"),1,1);
            UGameplayStatics::OpenLevel(W,TEXT("/Game/ChaosImpact/RPGPrototype/Adventure/Lvl_RPGColonyFrontier"),true,TEXT("CIRPGPrototype=1?CIOpenWorld=1?CILocalPlayers=1?CIKeyboardPlayer=0?CIRPGTitle=1"));
            Stage=1; return false;
        }
        auto* P=Cast<ARPGPrototypeCharacter>(UGameplayStatics::GetPlayerPawn(W,0));
        if(!P || !P->GetPrototypeHUD()) { return false; }
        const double Now=W->GetTimeSeconds(); if(Now<Next) { return false; }
        auto* T=P->GetTitleWidget();
        switch(Stage)
        {
        case 1:
            if(!T) { return false; }
            Test->TestTrue(TEXT("Title shows first"),P->IsInTitle() && T->GetPage()==URPGTitleWidget::EPage::Press);
            Test->TestTrue(TEXT("Player waits behind the title"),P->IsTrainingMenuFrozen());
            Stage=2; Next=Now+1.5; return false;
        case 2: Shot(TEXT("title-press.png")); Stage=21; Next=Now+.5; return false;
        case 21: Shot(TEXT("title-press.png")); Stage=22; Next=Now+.5; return false;
        case 22: T->HandleKey(EKeys::Enter); Stage=3; Next=Now+.6; return false;
        case 3:
            // How many play comes first; the heroes after.
            Test->TestEqual(TEXT("Any button leads to how many play"),T->GetPage(),URPGTitleWidget::EPage::Play);
            Stage=301; Next=Now+.3; return false;
        case 301: Shot(TEXT("title-play.png")); T->SetPlayers(2); T->StartWithAssignment(TEXT("CILocalPlayers=2?CIKeyboardPlayer=0")); Stage=302; Next=Now+.5; return false;
        case 302:
        {
            auto* B=Cast<ARPGPrototypeCharacter>(UGameplayStatics::GetPlayerPawn(W,1));
            if(!B || !B->GetTitleWidget()) { return false; }
            Test->TestTrue(TEXT("After the assignment player 1 picks a hero"),T->GetPage()==URPGTitleWidget::EPage::Heroes && T->GetHeroCount()==2);
            Test->TestTrue(TEXT("Player 2 picks on their own screen"),B->IsInTitle() && B->GetTitleWidget()->GetMode()==URPGTitleWidget::EMode::Join && B->GetTitleWidget()->GetPage()==URPGTitleWidget::EPage::Heroes);
            Stage=303; Next=Now+1.2; return false;
        }
        case 303: Shot(TEXT("title-heroes-split.png")); Stage=304; Next=Now+.4; return false;
        case 304: T->HandleKey(EKeys::Escape); Stage=305; Next=Now+.6; return false;
        case 305:
            Test->TestTrue(TEXT("Back to how many play; the added player leaves"),T->GetPage()==URPGTitleWidget::EPage::Play && Cast<ARPGPrototypeGameMode>(W->GetAuthGameMode())->GetLocalPlayerCount()==1);
            T->SetPlayers(1); T->HandleKey(EKeys::Down); T->HandleKey(EKeys::Enter); Stage=3051; Next=Now+.4; return false;
        case 3051:
        {
            Test->TestEqual(TEXT("Online opens the password field"),T->GetPage(),URPGTitleWidget::EPage::Online);
            auto* Layer=T->GetNameInputForTest()->GetTypedOuter<URPGTitleInputLayer>();
            auto* Password=Layer?Cast<UEditableText>(Layer->GetWidgetFromName(TEXT("RPGOnlinePassword"))):nullptr;
            if(!Test->TestNotNull(TEXT("Name and password use a separate native input layer"),Password)) { Cleanup(); return true; }
            Test->TestTrue(TEXT("Password layer is on the player screen with a focused field"),Layer->IsInViewport() && Password->HasKeyboardFocus() && Password->IsVisible());
            Password->SetText(FText::GetEmpty()); TypeText(TEXT("合言葉abc")); Stage=3052; Next=Now+.4; return false;
        }
        case 3052:
        {
            auto* Layer=T->GetNameInputForTest()->GetTypedOuter<URPGTitleInputLayer>();
            auto* Password=Cast<UEditableText>(Layer->GetWidgetFromName(TEXT("RPGOnlinePassword")));
            Test->TestEqual(TEXT("Native Japanese and ASCII password input"),Password->GetText().ToString(),FString(TEXT("合言葉abc")));
            Shot(TEXT("title-password-typed.png")); PressKey(EKeys::BackSpace); Stage=3053; Next=Now+.3; return false;
        }
        case 3053:
            if(!Test->TestEqual(TEXT("Backspace edits the password without leaving the page"),T->GetPage(),URPGTitleWidget::EPage::Online)) { Cleanup(); return true; }
            PressKey(EKeys::Escape); Stage=3054; Next=Now+.3; return false;
        case 3054:
            Test->TestEqual(TEXT("Esc leaves the focused password field"),T->GetPage(),URPGTitleWidget::EPage::Play);
            T->HandleKey(EKeys::Down); T->HandleKey(EKeys::Enter); Stage=306; Next=Now+.6; return false;
        case 306:
            Test->TestTrue(TEXT("One player: straight to the heroes"),T->GetPage()==URPGTitleWidget::EPage::Heroes);
            T->HandleKey(EKeys::Right); Stage=31; Next=Now+1.0; return false;
        case 31: Shot(TEXT("title-heroes.png")); T->HandleKey(EKeys::C); Stage=311; Next=Now+.5; return false;
        case 311:
            // The look can be changed on the title at any time.
            Test->TestEqual(TEXT("C changes the look"),T->GetPage(),URPGTitleWidget::EPage::Look);
            T->HandleKey(EKeys::Down); Stage=312; Next=Now+.6; return false;
        case 312: Shot(TEXT("title-look-change.png")); T->HandleKey(EKeys::Enter); Stage=313; Next=Now+.4; return false;
        case 313:
            Test->TestTrue(TEXT("New look saved"),T->GetPage()==URPGTitleWidget::EPage::Heroes && RPGHeroes::Load().Num()==2 && RPGHeroes::Load()[1].Character==2);
            Stage=32; Next=Now+.4; return false;
        case 32: T->HandleKey(EKeys::Right); T->HandleKey(EKeys::Enter); Stage=4; Next=Now+.6; return false;
        case 4:
            Test->TestEqual(TEXT("The last spot makes a new hero"),T->GetPage(),URPGTitleWidget::EPage::Look);
            T->HandleKey(EKeys::Down); T->HandleKey(EKeys::Down); Stage=5; Next=Now+.8; return false;
        case 5: Shot(TEXT("title-look.png")); T->HandleKey(EKeys::Enter); Stage=6; Next=Now+.4; return false;
        case 6:
            Test->TestEqual(TEXT("Then the name"),T->GetPage(),URPGTitleWidget::EPage::Name);
            // Real key presses through the UI (the name field has the keyboard): Esc goes back.
            PressKey(EKeys::Escape); Stage=61; Next=Now+.4; return false;
        case 61:
            Test->TestEqual(TEXT("Esc on the name page goes back to the look"),T->GetPage(),URPGTitleWidget::EPage::Look);
            T->HandleKey(EKeys::Enter); Stage=62; Next=Now+.4; return false;
        case 62:
            Test->TestEqual(TEXT("And on to the name again"),T->GetPage(),URPGTitleWidget::EPage::Name);
            TypeText(TEXT("コハクabc")); Stage=620; Next=Now+.35; return false;
        case 620:
            Test->TestEqual(TEXT("Real Slate character input reaches the native name field"),T->GetNameInputForTest()->GetText().ToString(),FString(TEXT("コハクabc")));
            Test->TestTrue(TEXT("Native input remains focused, visible and opaque"),T->GetNameInputForTest()->HasKeyboardFocus() && T->GetNameInputForTest()->IsVisible() && T->GetNameInputForTest()->GetRenderOpacity()==1.f);
            Shot(TEXT("title-name-typed.png"));
            PressKey(EKeys::Left); PressKey(EKeys::BackSpace); TypeText(TEXT("あ")); Stage=621; Next=Now+.35; return false;
        case 621:
            Test->TestEqual(TEXT("Native caret movement, deletion and Japanese insertion work"),T->GetNameInputForTest()->GetText().ToString(),FString(TEXT("コハクaあc")));
            PressKey(EKeys::F1); Stage=7; Next=Now+.5; return false;
        case 7: Shot(TEXT("title-name.png")); UE_LOG(LogTemp, Display, TEXT("RPGTITLE name box: %s"), *T->DescribeNameBoxForTest()); Stage=71; Next=Now+.4; return false;
        case 71:
            // Enter in the field makes the hero.
            PressKey(EKeys::Enter); Stage=10; Next=Now+.5; return false;
        case 10:
        {
            // The adventure starts right there.
            if(P->HeroId.IsEmpty() || P->IsInTitle()) { return false; }
            Created=RPGHeroes::Load().Last().Id;
            Test->TestEqual(TEXT("A named hero is saved in the list"),RPGHeroes::Load().Num(),3);
            Test->TestEqual(TEXT("Started as the created hero"),P->HeroId,Created);
            Test->TestTrue(TEXT("Hero has its own save from the start"),UGameplayStatics::DoesSaveGameExist(RPGHeroes::SaveSlot(Created),0));
            Test->TestFalse(TEXT("Playing, not frozen"),P->IsTrainingMenuFrozen());
            // Someone joins mid-game with a free pad (＋).
            Test->TestTrue(TEXT("A free pad joins"),Cast<ARPGPrototypeGameMode>(W->GetAuthGameMode())->JoinWithPad(77));
            Stage=11; Next=Now+.5; return false;
        }
        case 11:
        {
            auto* B=Cast<ARPGPrototypeCharacter>(UGameplayStatics::GetPlayerPawn(W,1));
            if(!B || !B->GetTitleWidget()) { return false; }
            Test->TestTrue(TEXT("The joining player picks a hero on their own screen"),B->IsInTitle() && B->GetTitleWidget()->GetMode()==URPGTitleWidget::EMode::Join);
            Test->TestFalse(TEXT("Player 1 keeps playing"),P->IsInTitle());
            Stage=12; Next=Now+1.0; return false;
        }
        case 12: Shot(TEXT("title-join-split.png")); Stage=121; Next=Now+.5; return false;
        case 121:
        {
            auto* B=Cast<ARPGPrototypeCharacter>(UGameplayStatics::GetPlayerPawn(W,1));
            auto* BT=B->GetTitleWidget();
            for(int32 I=0;I<BT->GetHeroCount();++I) { BT->HandleKey(EKeys::Gamepad_DPad_Right); }
            BT->HandleKey(EKeys::Gamepad_FaceButton_Bottom); BT->HandleKey(EKeys::Gamepad_FaceButton_Bottom);
            Test->TestEqual(TEXT("Pad user reaches name entry"),BT->GetPage(),URPGTitleWidget::EPage::Name);
            BT->HandleKey(EKeys::Gamepad_FaceButton_Top);
            Test->TestFalse(TEXT("Pad name uses painted text, not a keyboard focus-stealing overlay"),BT->GetNameInputForTest()->IsVisible());
            Stage=1211; Next=Now+.5; return false;
        }
        case 1211:
        {
            auto* B=Cast<ARPGPrototypeCharacter>(UGameplayStatics::GetPlayerPawn(W,1));
            Shot(TEXT("title-name-pad-split.png"));
            B->GetTitleWidget()->HandleKey(EKeys::Gamepad_FaceButton_Bottom);
            Stage=13; Next=Now+.5; return false;
        }
        case 13:
        {
            auto* B=Cast<ARPGPrototypeCharacter>(UGameplayStatics::GetPlayerPawn(W,1));
            Test->TestTrue(TEXT("Player 2 plays their new hero"),B && !B->HeroId.IsEmpty() && B->HeroId!=P->HeroId && !B->IsInTitle());
            // A world opened with heroes (ReturnToTitle-free travel) still starts each saved hero.
            UGameplayStatics::OpenLevel(W,TEXT("/Game/ChaosImpact/RPGPrototype/Adventure/Lvl_RPGColonyFrontier"),true,
                FString(TEXT("CIRPGPrototype=1?CIOpenWorld=1?CILocalPlayers=1?CIKeyboardPlayer=0"))+RPGHeroes::TravelOptions({Existing}));
            Stage=14; Next=0; return false;
        }
        case 14:
            if(P->HeroId!=Existing) { return false; }
            Test->TestEqual(TEXT("Travel options start the saved hero"),P->HeroId,Existing);
            Cleanup(); return true;
        }
        return false;
    }
private:
    static void TypeText(const FString& Text)
    {
        if(!FSlateApplication::IsInitialized()) { return; }
        for(TCHAR Ch:Text) { FSlateApplication::Get().ProcessKeyCharEvent(FCharacterEvent(Ch,FSlateApplication::Get().GetModifierKeys(),0,false)); }
    }
    static void PressKey(const FKey& Key)
    {
        if(!FSlateApplication::IsInitialized()) { return; }
        const FKeyEvent Down(Key,FSlateApplication::Get().GetModifierKeys(),0,false,0,0);
        FSlateApplication::Get().ProcessKeyDownEvent(Down);
        // Windows emits a character event as well as the key event for Backspace;
        // native Slate text editing deliberately performs deletion on that event.
        if(Key==EKeys::BackSpace) { TypeText(TEXT("\b")); }
        FSlateApplication::Get().ProcessKeyUpEvent(Down);
    }
    void Cleanup() { for(const auto& H:RPGHeroes::Load()) { UGameplayStatics::DeleteGameInSlot(RPGHeroes::SaveSlot(H.Id),0); } UGameplayStatics::DeleteGameInSlot(TEXT("CI_RPG_Heroes_QA"),0); RPGHeroes::SetIndexSlotForTests(FString()); }
    FAutomationTestBase* Test; double Started=FPlatformTime::Seconds(),Next=0; int32 Stage=0; FString Existing,Created;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTitleTest,"ChaosImpact.RPGPrototype.Title",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FRPGTitleTest::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(FRPGTitleCommand(this)); return true; }
#endif
