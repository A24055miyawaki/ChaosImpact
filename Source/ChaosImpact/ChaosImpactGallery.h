#pragma once

#include "CoreMinimal.h"
#include "ChaosImpactNameEntry.h"
#include "Dom/JsonObject.h"
#include "Styling/SlateBrush.h"
#include "UObject/Object.h"
#include "ChaosImpactGallery.generated.h"

class AChaosImpactCharacterPreview;
class AChaosImpactPlayerController;
class FSlateWindowElementList;
class UTexture;
struct FAnalogInputEvent;
struct FGeometry;
struct FKeyEvent;

/**
 * みんなのカラー (オンラインメニュー): the custom colours players post, kept on the relay (Tools/OnlineRelay/ci_gallery.py).
 * The list shows eight at a time as cards, each its character in those colours (and its paint) with the work's name,
 * who posted it, when and how many have kept it: 新着 / 人気 / 自分の投稿, by character. A card opened shows the work
 * large, turning (全体図), with its parts' colours, and keeps it among this machine's presets. 投稿する picks one of this
 * machine's presets, names the work and sends it (its paint in pieces). Keys, a controller and the mouse all work it.
 */
UCLASS()
class UChaosImpactGallery : public UObject
{
	GENERATED_BODY()
public:
	enum class ETab : uint8 { New, Popular, Mine };
	enum class EView : uint8 { List, Detail, Post };
	static constexpr int32 PerPage = 8;

	struct FPost
	{
		FString Id;
		FString Title;
		FString Poster;
		int64 At = 0;
		int32 Character = 0;
		TArray<uint8> Parts;
		int32 PaintBytes = 0;
		int32 Saves = 0;
		bool bMine = false;
		bool bSaved = false;
	};

	void Open(AChaosImpactPlayerController* InController);
	void Close();
	bool IsOpen() const { return bOpen; }
	/** It asked to go back to the online menu (once). */
	bool ConsumeLeave();
	void Tick(float DeltaSeconds);
	int32 Paint(const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer) const;
	bool HandleKeyDown(const FKeyEvent& Event);
	bool HandleAnalog(const FAnalogInputEvent& Event);
	bool HandleCharacter(TCHAR Character);
	void HandleMouseMove(const FVector2D& DesignPoint, bool bLeftDown);
	void HandleMouseDown(const FVector2D& DesignPoint, const FKey& Button);
	void HandleMouseUp(const FVector2D& DesignPoint, const FKey& Button);
	void HandleWheel(const FVector2D& DesignPoint, float Delta);

	// For tests and its own input.
	void SetTab(ETab InTab);
	ETab GetTab() const { return Tab; }
	void SetCharacterFilter(int32 Character);
	void Refresh();
	void TurnPage(int32 Direction);
	bool IsLoading() const { return bListPending; }
	const TArray<FPost>& GetPosts() const { return Posts; }
	int32 GetPage() const { return Page; }
	int32 GetPages() const { return Pages; }
	int32 GetTotal() const { return Total; }
	EView GetView() const { return View; }
	const FString& GetError() const { return ErrorText; }
	void OpenDetail(int32 Index);
	void CloseDetail();
	/** The work open now kept among this machine's presets (its paint too, once that has come). */
	void SaveToPresets();
	bool IsDetailPaintReady() const;
	void DeleteDetail();
	void OpenPost();
	/** The post screen's choices: every preset of this machine (character, slot). */
	int32 GetPostChoiceCount() const { return PostChoices.Num(); }
	void ChoosePost(int32 Choice);
	void SetPostTitle(const FString& Title) { PostTitle = Title; }
	const FString& GetPostTitle() const { return PostTitle; }
	void SubmitPost();
	bool IsPosting() const { return bPosting; }
	float GetPostProgress() const;
	const FString& GetNotice() const { return NoticeText; }

private:
	struct FDownload
	{
		int32 Total = -1;
		int32 PieceBytes = 0;
		TArray<uint8> Bytes;
		TBitArray<> Have;
		TSet<int32> Asked;
		int32 InFlight = 0;
		int32 Fails = 0;
		bool bDone = false;
		bool bFailed = false;
		/** The pictures, kept by UChaosImpactPaintCache (0 until they have all come). */
		int32 Key = 0;
	};
	struct FPostChoice
	{
		int32 Character = 0;
		int32 Slot = 0;
	};

	void RequestList();
	void ReadList(const TSharedPtr<FJsonObject>& Reply, int32 Generation);
	FString Describe(const TSharedPtr<FJsonObject>& Reply) const;
	void PumpDownloads();
	void AskPiece(const FString& PostId, int32 Offset);
	void FinishDownload(const FString& PostId, FDownload& Download);
	TArray<UTexture*> PaintTextures(const FString& PostId) const;
	float DownloadProgress(const FString& PostId) const;
	void ShowOn(AChaosImpactCharacterPreview* Stage, const FPost& Post, bool bWithPaint);
	void RebuildCards();
	void PumpUpload();
	void FinishPost(bool bOk, const FString& Message);
	void Notify(const FString& Text);
	void Activate();
	void Back();
	void Move(int32 Columns, int32 Rows);
	FString FormatDate(int64 Unix) const;
	FString CharacterName(int32 Character) const;
	void RebuildPostChoices();
	void ShowPostPreview();
	/** What is at a point (the same codes as Focus): INDEX_NONE nothing. */
	int32 HitTest(const FVector2D& Point) const;

	// Layout (1600 x 900 design space).
	static FBox2D CardRect(int32 Index);
	static FBox2D TabRect(int32 Index);
	static FBox2D FilterRect();
	static FBox2D PostButtonRect();
	static FBox2D PageArrowRect(int32 Direction);
	static FBox2D DetailButtonRect(int32 Index);
	static FBox2D PostRowRect(int32 Row);
	static FBox2D PostFieldRect();
	static FBox2D PostActionRect(int32 Index);

	TWeakObjectPtr<AChaosImpactPlayerController> Controller;
	bool bOpen = false;
	bool bLeave = false;
	double OpenedAt = 0.0;
	EView View = EView::List;
	ETab Tab = ETab::New;
	int32 CharacterFilter = INDEX_NONE;
	int32 Page = 0;
	int32 Pages = 1;
	int32 Total = 0;
	TArray<FPost> Posts;
	bool bListPending = false;
	int32 ListGeneration = 0;
	bool bListLoaded = false;
	FString ErrorText;
	/**
	 * Where the cursor is: 0..7 a card; 100 + n a tab, 103 the character filter, 104 投稿する, 110 / 111 the page arrows;
	 * in a work: 200 + n its buttons; posting: 300 + n a preset, 400 the work's name, 401 投稿する, 402 やめる.
	 */
	int32 Focus = 0;
	int32 DetailIndex = INDEX_NONE;
	int32 DetailButtons = 2;
	bool bDeleteArmed = false;
	float DetailYaw = -22.0f;
	double DetailTouchedAt = -100.0;
	float StickTurn = 0.0f;
	bool bDragging = false;
	FVector2D LastMouse = FVector2D::ZeroVector;
	bool bStickHeld[2] = {false, false};
	double NextStickAt = 0.0;

	TMap<FString, FDownload> Downloads;
	int32 InFlight = 0;

	// 投稿する.
	TArray<FPostChoice> PostChoices;
	int32 PostChoice = 0;
	int32 PostScroll = 0;
	FString PostTitle;
	bool bPosting = false;
	FString UploadId;
	TArray<uint8> UploadBytes;
	TBitArray<> UploadDone;
	TSet<int32> UploadAsked;
	int32 UploadInFlight = 0;
	int32 UploadFails = 0;
	FChaosImpactNameEntry Naming;

	FString NoticeText;
	double NoticeAt = -100.0;
	int32 Frame = 0;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AChaosImpactCharacterPreview>> Cards;
	TArray<FSlateBrush> CardBrushes;
	TArray<double> CardShownAt;
	UPROPERTY(Transient)
	TObjectPtr<AChaosImpactCharacterPreview> Stage;
	FSlateBrush StageBrush;
};
