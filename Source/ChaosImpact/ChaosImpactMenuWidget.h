#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ChaosImpactScreen.h"
#include "ChaosImpactMenuWidget.generated.h"

class UTexture2D;
class UEditableText;

/** Native, resolution-independent front end. All hit regions and D-pad focus share one layout. */
UCLASS()
class UChaosImpactMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void ShowScreen(EChaosImpactScreen NewScreen);
	void Navigate(FKey Key);
	void ConfirmSelection();
	void GoBack();
	void RefreshEntries();
	int32 GetSelectedIndex() const { return SelectedIndex; }
	bool HasLogo() const { return LogoTexture != nullptr; }
	EChaosImpactScreen GetScreen() const { return Screen; }
	/**
	 * Player entry: a controller button from any controller joins it. Slate sends each controller's keys only
	 * to its own user's focus, and this menu has only the first user's, so a second or third pad never reached
	 * NativeOnKeyDown. Returns true for the press that joined, so it does not also press a menu entry.
	 */
	bool TryJoinControllerFromAnyUser(const FKeyEvent& InKeyEvent);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnAnalogValueChanged(const FGeometry& InGeometry,
		const FAnalogInputEvent& InAnalogEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;

private:
	struct FMenuEntry
	{
		FSlateRect Rect;
		FString Title;
		FString Detail;
		FString Number;
		FLinearColor Accent;
		bool bDisabled = false;
	};

	TArray<FMenuEntry> Entries;
	/** 0..1 eased selection state per entry, advanced in NativeTick. */
	TArray<float> SelectBlend;
	/** Per-slot join tracking so a newly connected device plays its entry animation once. */
	bool bSlotJoined[4] = {};
	double SlotJoinedAt[4] = {};
	EChaosImpactScreen Screen = EChaosImpactScreen::Title;
	int32 SelectedIndex = 0;
	int32 PressedIndex = INDEX_NONE;
	/** Pause entries that leave the room/search need a second confirm on the same entry. */
	int32 ArmedIndex = INDEX_NONE;
	void DisarmSelection();
	double ScreenStartedAt = 0.0;
	float AnimationSeconds = 0.0f;
	double LastAnalogNavigationAt = 0.0;
	void BuildEntries();
	int32 HitTestEntry(const FGeometry& Geometry, const FVector2D& ScreenPosition) const;

	UFUNCTION()
	void HandleNameCommitted(const FText& Text, ETextCommit::Type CommitMethod);
	UFUNCTION()
	void HandleNameChanged(const FText& Text);
	void SubmitName();
	void SubmitPassword();
	void SubmitRoomNameEntry();
	FString GetPasswordString() const;
	/** Digit editing on the password screen. Returns true when the key was consumed. */
	bool HandlePasswordKey(const FKey& Key);

	UPROPERTY(Transient)
	TObjectPtr<UEditableText> NameInput;
	int32 PasswordDigits[4] = {0, 0, 0, 0};
	int32 PasswordCursor = 0;
	bool bNameFocusPending = false;
	float NameInputFontScale = 0.0f;
	FString LastCreateError;
	/** Sees every controller's presses before focus routing, for player entry. */
	TSharedPtr<class IInputProcessor> JoinInputProcessor;
	/** The room list is rebuilt when the session subsystem's list changes. */
	int32 LastRoomListingsVersion = -1;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> LogoTexture;
	FSlateBrush LogoBrush;
};
