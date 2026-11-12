#include "ChaosImpactGallery.h"

#include "ChaosImpactBodyPaint.h"
#include "ChaosImpactCharacterPreview.h"
#include "ChaosImpactCharacterRoster.h"
#include "ChaosImpactCustomColour.h"
#include "ChaosImpactPaint.h"
#include "ChaosImpactPaintCache.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactRelay.h"
#include "ChaosImpactSessionSubsystem.h"
#include "ChaosImpactSettings.h"
#include "ChaosImpactSfx.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Input/Events.h"
#include "Misc/Base64.h"

namespace
{
	using namespace ChaosImpactPaint;

	/** The relay's pieces of paint (ci_gallery.py PIECE). */
	constexpr int32 GaPieceBytes = 900;
	constexpr int32 GaMostInFlight = 6;
	constexpr float GaCardWidth = 330.0f;
	constexpr float GaCardHeight = 306.0f;
	constexpr float GaCardGap = 28.0f;
	constexpr float GaGridLeft = (1600.0f - (GaCardWidth * 4.0f + GaCardGap * 3.0f)) * 0.5f;
	constexpr float GaGridTop = 182.0f;
	constexpr float GaRowGap = 22.0f;
	constexpr float GaBarTop = 842.0f;
	constexpr int32 GaPostRows = 9;
	const FLinearColor GaBack(0.008f, 0.012f, 0.03f, 1.0f);
	const FLinearColor GaCard(0.03f, 0.07f, 0.18f, 1.0f);
	const FLinearColor GaCardOn(0.06f, 0.2f, 0.5f, 1.0f);
	const FLinearColor GaPink(1.0f, 0.42f, 0.72f, 1.0f);
	const TCHAR* const GaTabNames[3] = {TEXT("新着"), TEXT("人気"), TEXT("自分の投稿")};

	bool GaIsAny(const FKey& Key, std::initializer_list<FKey> Keys)
	{
		for (const FKey& Candidate : Keys)
		{
			if (Key == Candidate)
			{
				return true;
			}
		}
		return false;
	}

	void GaRound(const FPainter& P, const FBox2D& R, const float Radius, const FLinearColor& Color)
	{
		const FVector2D S = R.GetSize();
		RoundedBox(P, static_cast<float>(R.Min.X), static_cast<float>(R.Min.Y), static_cast<float>(S.X), static_cast<float>(S.Y), Radius, Color);
	}

	FBox2D GaGrow(const FBox2D& R, const float By)
	{
		return FBox2D(R.Min - FVector2D(By), R.Max + FVector2D(By));
	}

	/** A rounded outline: the shape a little larger in the colour, the inside drawn over it by the caller. */
	void GaGlow(const FPainter& P, const FBox2D& R, const float Radius, const FLinearColor& Color, const float Width)
	{
		GaRound(P, GaGrow(R, Width), Radius + Width, Color);
	}

	void GaText(const FPainter& P, const FString& Value, const float X, const float Y, const float Size, const float Width, const FLinearColor& Color,
		const ETextAlign Align = ETextAlign::Left, const FName Face = TEXT("Bold"))
	{
		P.Text(Value, X, Y, HudFit(Value, Size, Width, Face), Color, Align, Face);
	}

	void GaPicture(const FPainter& P, const FSlateBrush& Brush, const FBox2D& R)
	{
		if (!Brush.GetResourceObject())
		{
			return;
		}
		FSlateDrawElement::MakeBox(P.Elements, P.Layer,
			P.Geometry.ToPaintGeometry(FVector2f(R.GetSize()), FSlateLayoutTransform(FVector2f(R.Min))),
			&Brush, ESlateDrawEffect::None, WithAlpha(FLinearColor::White, P.Alpha));
	}

	FSlateBrush GaBrushOf(AChaosImpactCharacterPreview* Stage)
	{
		FSlateBrush Brush;
		if (Stage && Stage->GetPicture())
		{
			Brush.SetResourceObject(Stage->GetPicture());
			Brush.ImageSize = FVector2D(Stage->GetPicture()->SizeX, Stage->GetPicture()->SizeY);
		}
		Brush.DrawAs = ESlateBrushDrawType::Image;
		return Brush;
	}

	/** A heart (保存した数). */
	void GaHeart(const FPainter& P, const FVector2D& C, const float Size, const FLinearColor& Color)
	{
		P.Disc(C + FVector2D(-Size * 0.26, -Size * 0.1), Size * 0.3f, Color);
		P.Disc(C + FVector2D(Size * 0.26, -Size * 0.1), Size * 0.3f, Color);
		TArray<FVector2D> Point = {C + FVector2D(-Size * 0.54, 0.0), C + FVector2D(Size * 0.54, 0.0), C + FVector2D(0.0, Size * 0.55)};
		for (int32 Row = 0; Row < 8; ++Row)
		{
			const float T = Row / 8.0f;
			P.Line(FMath::Lerp(Point[0], Point[2], T), FMath::Lerp(Point[1], Point[2], T), Color, Size * 0.12f);
		}
	}

	/** A small pen (ペイントあり). */
	void GaPen(const FPainter& P, const FVector2D& C, const float Size, const FLinearColor& Line, const FLinearColor& Tip)
	{
		P.Line(C + FVector2D(-Size * 0.45, Size * 0.45), C + FVector2D(Size * 0.4, -Size * 0.4), Line, Size * 0.26f);
		P.Disc(C + FVector2D(-Size * 0.45, Size * 0.45), Size * 0.16f, Tip);
	}

	void GaSpinner(const FPainter& P, const FVector2D& C, const float Radius, const float Time)
	{
		P.Arc(C, Radius, Time * 360.0f, Time * 360.0f + 250.0f, Ice, 5.0f);
	}
}

// ---- Opening and closing -----------------------------------------------------------------------------------------------

void UChaosImpactGallery::Open(AChaosImpactPlayerController* InController)
{
	Close();
	Controller = InController;
	UWorld* World = InController ? InController->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}
	bOpen = true;
	bLeave = false;
	OpenedAt = FPlatformTime::Seconds();
	View = EView::List;
	Focus = 0;
	Page = 0;
	Posts.Reset();
	bListLoaded = false;
	ErrorText.Reset();
	NoticeAt = -100.0;
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Parameters.ObjectFlags |= RF_Transient;
	Cards.Reset();
	CardBrushes.Reset();
	CardShownAt.Reset();
	for (int32 Index = 0; Index < PerPage; ++Index)
	{
		AChaosImpactCharacterPreview* Card = World->SpawnActor<AChaosImpactCharacterPreview>(AChaosImpactCharacterPreview::StaticClass(),
			FVector(Index * 1600.0f, -140000.0f, 40000.0f), FRotator::ZeroRotator, Parameters);
		if (Card)
		{
			Card->UseAsPortrait();
		}
		Cards.Add(Card);
		CardBrushes.Add(GaBrushOf(Card));
		CardShownAt.Add(-100.0);
	}
	Stage = World->SpawnActor<AChaosImpactCharacterPreview>(AChaosImpactCharacterPreview::StaticClass(), FVector(0.0f, -150000.0f, 40000.0f),
		FRotator::ZeroRotator, Parameters);
	StageBrush = GaBrushOf(Stage);
#if !UE_BUILD_SHIPPING
	// Development: -CIGalleryTab=<0..2> opens on that tab.
	int32 DevTab = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("CIGalleryTab="), DevTab))
	{
		Tab = static_cast<ETab>(FMath::Clamp(DevTab, 0, 2));
	}
#endif
	RequestList();
}

void UChaosImpactGallery::Close()
{
	for (AChaosImpactCharacterPreview* Card : Cards)
	{
		if (IsValid(Card))
		{
			Card->Destroy();
		}
	}
	Cards.Reset();
	CardBrushes.Reset();
	CardShownAt.Reset();
	if (IsValid(Stage))
	{
		Stage->Destroy();
	}
	Stage = nullptr;
	Naming.Close();
	bDragging = false;
	// (Replies still on their way are not wanted now; the paint already here stays for next time.)
	++ListGeneration;
	bListPending = false;
	for (TPair<FString, FDownload>& Pair : Downloads)
	{
		if (!Pair.Value.bDone)
		{
			Pair.Value = FDownload();
		}
	}
	InFlight = 0;
	bOpen = false;
}

bool UChaosImpactGallery::ConsumeLeave()
{
	const bool bWas = bLeave;
	bLeave = false;
	return bWas;
}

void UChaosImpactGallery::Notify(const FString& Text)
{
	NoticeText = Text;
	NoticeAt = FPlatformTime::Seconds();
}

FString UChaosImpactGallery::CharacterName(const int32 Character) const
{
	return Character >= 0 && Character < ChaosImpactRoster::Num() ? FString(ChaosImpactRoster::Get(Character).Name) : FString(TEXT("？"));
}

FString UChaosImpactGallery::FormatDate(const int64 Unix) const
{
	// The relay keeps UTC; shown in this machine's own time.
	const FDateTime Local = FDateTime::FromUnixTimestamp(Unix) + (FDateTime::Now() - FDateTime::UtcNow());
	return FString::Printf(TEXT("%04d/%02d/%02d %02d:%02d"), Local.GetYear(), Local.GetMonth(), Local.GetDay(), Local.GetHour(), Local.GetMinute());
}

FString UChaosImpactGallery::Describe(const TSharedPtr<FJsonObject>& Reply) const
{
	if (!Reply.IsValid())
	{
		return TEXT("中継サーバーにつながりません。回線を確認してください");
	}
	const FString Error = Reply->GetStringField(TEXT("error"));
	// (A relay from before みんなのカラー answers "unknown room" to these requests.)
	return Error == TEXT("unknown op") || Error == TEXT("unknown room") ? TEXT("中継サーバーが古いため使えません（サーバーの更新が必要です）")
		: Error == TEXT("too fast") ? TEXT("続けて投稿するには、少し時間をおいてください")
		: Error == TEXT("too many") ? TEXT("投稿は30作品までです。「自分の投稿」から消してください")
		: Error == TEXT("too big") ? TEXT("ペイントが大きすぎて投稿できません")
		: Error == TEXT("unknown post") ? TEXT("この作品は消されたようです")
		: Error == TEXT("not yours") ? TEXT("自分の作品ではないので消せません")
		: TEXT("うまくいきませんでした。もう一度ためしてください");
}

// ---- The list ---------------------------------------------------------------------------------------------------------

void UChaosImpactGallery::SetTab(const ETab InTab)
{
	Tab = InTab;
	Page = 0;
	Refresh();
}

void UChaosImpactGallery::SetCharacterFilter(const int32 Character)
{
	CharacterFilter = Character < 0 || Character >= ChaosImpactRoster::Num() ? INDEX_NONE : Character;
	Page = 0;
	Refresh();
}

void UChaosImpactGallery::Refresh()
{
	RequestList();
}

void UChaosImpactGallery::TurnPage(const int32 Direction)
{
	const int32 Next = FMath::Clamp(Page + Direction, 0, FMath::Max(Pages - 1, 0));
	if (Next == Page)
	{
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiDeny);
		return;
	}
	Page = Next;
	ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiTab);
	RequestList();
}

void UChaosImpactGallery::RequestList()
{
	if (!ChaosImpactRelay::IsEnabled())
	{
		ErrorText = TEXT("インターネットの中継サーバーが設定されていません");
		bListPending = false;
		return;
	}
	const UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(Controller.Get());
	bListPending = true;
	const int32 Generation = ++ListGeneration;
	TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
	Message->SetStringField(TEXT("op"), TEXT("gallery_list"));
	Message->SetStringField(TEXT("token"), Sessions ? Sessions->GetPlayerToken() : FString());
	Message->SetNumberField(TEXT("page"), Page);
	Message->SetStringField(TEXT("sort"), Tab == ETab::Popular ? TEXT("saves") : TEXT("new"));
	Message->SetNumberField(TEXT("chara"), CharacterFilter);
	Message->SetBoolField(TEXT("mine"), Tab == ETab::Mine);
	ChaosImpactRelay::Request(Message, [WeakThis = TWeakObjectPtr<UChaosImpactGallery>(this), Generation](TSharedPtr<FJsonObject> Reply, int32)
	{
		if (UChaosImpactGallery* Self = WeakThis.Get())
		{
			Self->ReadList(Reply, Generation);
		}
	});
}

void UChaosImpactGallery::ReadList(const TSharedPtr<FJsonObject>& Reply, const int32 Generation)
{
	if (Generation != ListGeneration || !bOpen)
	{
		return;
	}
	bListPending = false;
	if (!Reply.IsValid() || !Reply->GetBoolField(TEXT("ok")))
	{
		ErrorText = Describe(Reply);
		return;
	}
	ErrorText.Reset();
	bListLoaded = true;
	Total = static_cast<int32>(Reply->GetNumberField(TEXT("total")));
	Pages = FMath::Max(1, static_cast<int32>(Reply->GetNumberField(TEXT("pages"))));
	Page = FMath::Clamp(static_cast<int32>(Reply->GetNumberField(TEXT("page"))), 0, Pages - 1);
	Posts.Reset();
	const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
	if (Reply->TryGetArrayField(TEXT("posts"), Items))
	{
		for (const TSharedPtr<FJsonValue>& Item : *Items)
		{
			const TSharedPtr<FJsonObject> Object = Item.IsValid() ? Item->AsObject() : nullptr;
			if (!Object.IsValid() || Posts.Num() >= PerPage)
			{
				continue;
			}
			FPost& Post = Posts.AddDefaulted_GetRef();
			Post.Id = Object->GetStringField(TEXT("id"));
			Post.Title = Object->GetStringField(TEXT("title"));
			Post.Poster = Object->GetStringField(TEXT("poster"));
			Post.At = static_cast<int64>(Object->GetNumberField(TEXT("at")));
			Post.Character = static_cast<int32>(Object->GetNumberField(TEXT("chara")));
			const FString Hex = Object->GetStringField(TEXT("parts"));
			Post.Parts.SetNumZeroed(Hex.Len() / 2);
			if (!Post.Parts.IsEmpty())
			{
				HexToBytes(Hex, Post.Parts.GetData());
			}
			Post.PaintBytes = static_cast<int32>(Object->GetNumberField(TEXT("paint")));
			Post.Saves = static_cast<int32>(Object->GetNumberField(TEXT("saves")));
			Post.bMine = Object->GetBoolField(TEXT("mine"));
			Post.bSaved = Object->GetBoolField(TEXT("saved"));
		}
	}
	if (Focus >= 0 && Focus < PerPage)
	{
		Focus = Posts.IsEmpty() ? 104 : FMath::Clamp(Focus, 0, Posts.Num() - 1);
	}
	RebuildCards();
	UE_LOG(LogTemp, Log, TEXT("Gallery: page %d / %d, %d posts (%d in all)"), Page + 1, Pages, Posts.Num(), Total);
}

void UChaosImpactGallery::ShowOn(AChaosImpactCharacterPreview* Target, const FPost& Post, const bool bWithPaint)
{
	if (!IsValid(Target))
	{
		return;
	}
	const int32 Character = ChaosImpactRoster::ClampIndex(Post.Character);
	Target->ShowLoadout(Character, 0, Ice);
	Target->ShowCustomColours(ChaosImpactCustomColour::Clean(Character, Post.Parts));
	Target->ShowPaint(bWithPaint ? PaintTextures(Post.Id) : TArray<UTexture*>());
}

void UChaosImpactGallery::RebuildCards()
{
	const double Now = FPlatformTime::Seconds();
	for (int32 Index = 0; Index < Cards.Num(); ++Index)
	{
		AChaosImpactCharacterPreview* Card = Cards[Index];
		if (!IsValid(Card))
		{
			continue;
		}
		if (Posts.IsValidIndex(Index))
		{
			ShowOn(Card, Posts[Index], true);
			Card->SetFacing(-22.0f, 0.0f);
			CardShownAt[Index] = Now;
		}
	}
}

// ---- Paint, a piece at a time -------------------------------------------------------------------------------------------

TArray<UTexture*> UChaosImpactGallery::PaintTextures(const FString& PostId) const
{
	TArray<UTexture*> Out;
	const FDownload* Download = Downloads.Find(PostId);
	UChaosImpactPaintCache* Cache = UChaosImpactPaintCache::Get(Controller.Get());
	if (Download && Download->bDone && Download->Key != 0 && Cache)
	{
		Cache->GetTextures(Download->Key, Out);
	}
	return Out;
}

float UChaosImpactGallery::DownloadProgress(const FString& PostId) const
{
	const FDownload* Download = Downloads.Find(PostId);
	if (!Download || Download->Total <= 0)
	{
		return Download && Download->bDone ? 1.0f : 0.0f;
	}
	int32 Have = 0;
	for (TConstSetBitIterator<> It(Download->Have); It; ++It)
	{
		++Have;
	}
	return Download->Have.Num() > 0 ? static_cast<float>(Have) / Download->Have.Num() : 0.0f;
}

bool UChaosImpactGallery::IsDetailPaintReady() const
{
	if (!Posts.IsValidIndex(DetailIndex))
	{
		return false;
	}
	const FPost& Post = Posts[DetailIndex];
	const FDownload* Download = Downloads.Find(Post.Id);
	return Post.PaintBytes <= 0 || (Download && Download->bDone);
}

void UChaosImpactGallery::PumpDownloads()
{
	if (!bOpen)
	{
		return;
	}
	// The work open first, then the cards in order.
	TArray<int32> Order;
	if (View == EView::Detail && Posts.IsValidIndex(DetailIndex))
	{
		Order.Add(DetailIndex);
	}
	for (int32 Index = 0; Index < Posts.Num(); ++Index)
	{
		Order.AddUnique(Index);
	}
	for (const int32 Index : Order)
	{
		if (InFlight >= GaMostInFlight)
		{
			return;
		}
		const FPost& Post = Posts[Index];
		if (Post.PaintBytes <= 0)
		{
			continue;
		}
		FDownload& Download = Downloads.FindOrAdd(Post.Id);
		if (Download.bDone || Download.bFailed)
		{
			continue;
		}
		if (Download.Total < 0)
		{
			// Its size and piece first.
			if (Download.InFlight == 0)
			{
				AskPiece(Post.Id, 0);
			}
			continue;
		}
		for (int32 Piece = 0; Piece < Download.Have.Num() && InFlight < GaMostInFlight; ++Piece)
		{
			const int32 Offset = Piece * Download.PieceBytes;
			if (!Download.Have[Piece] && !Download.Asked.Contains(Offset))
			{
				AskPiece(Post.Id, Offset);
			}
		}
	}
}

void UChaosImpactGallery::AskPiece(const FString& PostId, const int32 Offset)
{
	FDownload& Download = Downloads.FindOrAdd(PostId);
	Download.Asked.Add(Offset);
	++Download.InFlight;
	++InFlight;
	TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
	Message->SetStringField(TEXT("op"), TEXT("gallery_paint"));
	Message->SetStringField(TEXT("id"), PostId);
	Message->SetNumberField(TEXT("offset"), Offset);
	const int32 Generation = ListGeneration;
	ChaosImpactRelay::Request(Message, [WeakThis = TWeakObjectPtr<UChaosImpactGallery>(this), PostId, Offset, Generation](
		TSharedPtr<FJsonObject> Reply, int32)
	{
		UChaosImpactGallery* Self = WeakThis.Get();
		if (!Self || !Self->bOpen)
		{
			return;
		}
		FDownload* Found = Self->Downloads.Find(PostId);
		if (!Found || !Found->Asked.Contains(Offset))
		{
			// (Begun again since: not counted.)
			return;
		}
		FDownload& Download = *Found;
		Download.Asked.Remove(Offset);
		Download.InFlight = FMath::Max(Download.InFlight - 1, 0);
		Self->InFlight = FMath::Max(Self->InFlight - 1, 0);
		TArray<uint8> Data;
		const bool bOk = Reply.IsValid() && Reply->GetBoolField(TEXT("ok")) && FBase64::Decode(Reply->GetStringField(TEXT("data")), Data);
		const int32 Total = bOk ? static_cast<int32>(Reply->GetNumberField(TEXT("total"))) : 0;
		if (!bOk || Total <= 0 || Total > UChaosImpactPaintCache::MostBytes || Data.IsEmpty())
		{
			if (++Download.Fails > 12 || (Reply.IsValid() && !Reply->GetBoolField(TEXT("ok"))))
			{
				Download.bFailed = true;
			}
			return;
		}
		if (Download.Total < 0)
		{
			Download.Total = Total;
			Download.PieceBytes = FMath::Max(Data.Num(), 1);
			Download.Bytes.SetNumZeroed(Total);
			Download.Have.Init(false, (Total + Download.PieceBytes - 1) / Download.PieceBytes);
		}
		const int32 Piece = Offset / Download.PieceBytes;
		if (Total != Download.Total || Offset % Download.PieceBytes != 0 || !Download.Have.IsValidIndex(Piece) || Offset + Data.Num() > Total)
		{
			return;
		}
		FMemory::Memcpy(Download.Bytes.GetData() + Offset, Data.GetData(), Data.Num());
		Download.Have[Piece] = true;
		if (Download.Have.Find(false) == INDEX_NONE)
		{
			Self->FinishDownload(PostId, Download);
		}
	});
}

void UChaosImpactGallery::FinishDownload(const FString& PostId, FDownload& Download)
{
	TArray<FChaosImpactPaintLayerData> Layers;
	UChaosImpactPaintCache* Cache = UChaosImpactPaintCache::Get(Controller.Get());
	if (!Cache || !ChaosImpactBodyPaint::Unpack(Download.Bytes, Layers))
	{
		Download.bFailed = true;
		return;
	}
	Download.Key = Cache->Register(Layers);
	Download.bDone = Download.Key != 0;
	Download.bFailed = !Download.bDone;
	// Shown at once wherever that work is.
	const double Now = FPlatformTime::Seconds();
	for (int32 Index = 0; Index < Posts.Num() && Index < Cards.Num(); ++Index)
	{
		if (Posts[Index].Id == PostId && IsValid(Cards[Index]))
		{
			Cards[Index]->ShowPaint(PaintTextures(PostId));
			CardShownAt[Index] = Now;
		}
	}
	if (View == EView::Detail && Posts.IsValidIndex(DetailIndex) && Posts[DetailIndex].Id == PostId && IsValid(Stage))
	{
		Stage->ShowPaint(PaintTextures(PostId));
	}
	UE_LOG(LogTemp, Log, TEXT("Gallery: paint of %s here (%d bytes)"), *PostId, Download.Total);
}

// ---- A work -----------------------------------------------------------------------------------------------------------

void UChaosImpactGallery::OpenDetail(const int32 Index)
{
	if (!Posts.IsValidIndex(Index))
	{
		return;
	}
	View = EView::Detail;
	DetailIndex = Index;
	DetailButtons = Posts[Index].bMine ? 3 : 2;
	Focus = 200;
	bDeleteArmed = false;
	DetailYaw = -22.0f;
	DetailTouchedAt = -100.0;
	ShowOn(Stage, Posts[Index], true);
	ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiOpen);
}

void UChaosImpactGallery::CloseDetail()
{
	View = EView::List;
	Focus = FMath::Clamp(DetailIndex, 0, FMath::Max(Posts.Num() - 1, 0));
	DetailIndex = INDEX_NONE;
	bDragging = false;
	ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiBack);
}

void UChaosImpactGallery::SaveToPresets()
{
	if (!Posts.IsValidIndex(DetailIndex))
	{
		return;
	}
	FPost& Post = Posts[DetailIndex];
	if (!IsDetailPaintReady())
	{
		Notify(TEXT("ペイントを読みこんでいます。少し待ってください"));
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiDeny);
		return;
	}
	UChaosImpactColourSubsystem* Colours = UChaosImpactColourSubsystem::Get(Controller.Get());
	if (!Colours || Post.Character < 0 || Post.Character >= ChaosImpactRoster::Num())
	{
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiDeny);
		return;
	}
	FChaosImpactColourPreset Preset;
	Preset.Name = Post.Title.Left(ChaosImpactCustomColour::MaxNameLength);
	Preset.Parts = ChaosImpactCustomColour::Clean(Post.Character, Post.Parts);
	if (const FDownload* Download = Downloads.Find(Post.Id); Download && Download->bDone)
	{
		ChaosImpactBodyPaint::Unpack(Download->Bytes, Preset.Paint);
	}
	const int32 Slot = Colours->SetPreset(Post.Character, INDEX_NONE, Preset);
	if (Slot == INDEX_NONE)
	{
		Notify(FString::Printf(TEXT("%s のプリセットがいっぱいです（%dつまで）。カラーエディットで消してください"), *CharacterName(Post.Character),
			ChaosImpactCustomColour::MaxPresets));
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiDeny);
		return;
	}
	Notify(FString::Printf(TEXT("「%s」を %s のプリセット%dに保存しました"), *Post.Title, *CharacterName(Post.Character), Slot + 1));
	ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiReady);
	if (!Post.bSaved && !Post.bMine)
	{
		Post.bSaved = true;
		++Post.Saves;
	}
	const UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(Controller.Get());
	TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
	Message->SetStringField(TEXT("op"), TEXT("gallery_save"));
	Message->SetStringField(TEXT("token"), Sessions ? Sessions->GetPlayerToken() : FString());
	Message->SetStringField(TEXT("id"), Post.Id);
	const FString Id = Post.Id;
	ChaosImpactRelay::Request(Message, [WeakThis = TWeakObjectPtr<UChaosImpactGallery>(this), Id](TSharedPtr<FJsonObject> Reply, int32)
	{
		UChaosImpactGallery* Self = WeakThis.Get();
		if (!Self || !Reply.IsValid() || !Reply->GetBoolField(TEXT("ok")))
		{
			return;
		}
		for (FPost& Each : Self->Posts)
		{
			if (Each.Id == Id)
			{
				Each.Saves = static_cast<int32>(Reply->GetNumberField(TEXT("saves")));
			}
		}
	});
}

void UChaosImpactGallery::DeleteDetail()
{
	if (!Posts.IsValidIndex(DetailIndex) || !Posts[DetailIndex].bMine)
	{
		return;
	}
	if (!bDeleteArmed)
	{
		bDeleteArmed = true;
		Notify(TEXT("もう一度おすと、この投稿を消します"));
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiDeny);
		return;
	}
	bDeleteArmed = false;
	const UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(Controller.Get());
	TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
	Message->SetStringField(TEXT("op"), TEXT("gallery_delete"));
	Message->SetStringField(TEXT("token"), Sessions ? Sessions->GetPlayerToken() : FString());
	Message->SetStringField(TEXT("id"), Posts[DetailIndex].Id);
	const FString Title = Posts[DetailIndex].Title;
	ChaosImpactRelay::Request(Message, [WeakThis = TWeakObjectPtr<UChaosImpactGallery>(this), Title](TSharedPtr<FJsonObject> Reply, int32)
	{
		UChaosImpactGallery* Self = WeakThis.Get();
		if (!Self || !Self->bOpen)
		{
			return;
		}
		if (Reply.IsValid() && Reply->GetBoolField(TEXT("ok")))
		{
			Self->Notify(FString::Printf(TEXT("「%s」を消しました"), *Title));
			ChaosImpactSfx::Play2D(Self->Controller.Get(), EChaosImpactSfx::UiErase);
			if (Self->View == EView::Detail)
			{
				Self->CloseDetail();
			}
			Self->Refresh();
		}
		else
		{
			Self->Notify(Self->Describe(Reply));
		}
	});
}

// ---- 投稿する -----------------------------------------------------------------------------------------------------------

void UChaosImpactGallery::RebuildPostChoices()
{
	PostChoices.Reset();
	const UChaosImpactColourSubsystem* Colours = UChaosImpactColourSubsystem::Get(Controller.Get());
	for (int32 Character = 0; Colours && Character < ChaosImpactRoster::Num(); ++Character)
	{
		for (int32 Slot = 0; Slot < Colours->GetPresets(Character).Num(); ++Slot)
		{
			PostChoices.Add({Character, Slot});
		}
	}
}

void UChaosImpactGallery::OpenPost()
{
	RebuildPostChoices();
	View = EView::Post;
	PostScroll = 0;
	bPosting = false;
	Focus = PostChoices.IsEmpty() ? 402 : 300;
	ChoosePost(0);
	ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiOpen);
}

void UChaosImpactGallery::ChoosePost(const int32 Choice)
{
	if (!PostChoices.IsValidIndex(Choice))
	{
		return;
	}
	PostChoice = Choice;
	PostScroll = FMath::Clamp(PostScroll, Choice - GaPostRows + 1, Choice);
	const UChaosImpactColourSubsystem* Colours = UChaosImpactColourSubsystem::Get(Controller.Get());
	const FPostChoice& Chosen = PostChoices[Choice];
	PostTitle = Colours && Colours->GetPresets(Chosen.Character).IsValidIndex(Chosen.Slot) ? Colours->GetPresets(Chosen.Character)[Chosen.Slot].Name
		: FString();
	ShowPostPreview();
}

void UChaosImpactGallery::ShowPostPreview()
{
	if (!IsValid(Stage) || !PostChoices.IsValidIndex(PostChoice))
	{
		return;
	}
	const FPostChoice& Chosen = PostChoices[PostChoice];
	const UChaosImpactColourSubsystem* Colours = UChaosImpactColourSubsystem::Get(Controller.Get());
	UChaosImpactPaintCache* Cache = UChaosImpactPaintCache::Get(Controller.Get());
	Stage->ShowLoadout(Chosen.Character, 0, GaPink);
	Stage->ShowCustomColours(Colours ? Colours->GetPresetParts(Chosen.Character, Chosen.Slot) : TArray<uint8>());
	Stage->ShowPaint(Cache ? Cache->GetPresetTextures(Chosen.Character, Chosen.Slot) : TArray<UTexture*>());
	DetailYaw = -22.0f;
}

float UChaosImpactGallery::GetPostProgress() const
{
	if (UploadDone.Num() == 0)
	{
		return bPosting ? 0.05f : 0.0f;
	}
	int32 Done = 0;
	for (TConstSetBitIterator<> It(UploadDone); It; ++It)
	{
		++Done;
	}
	return static_cast<float>(Done) / UploadDone.Num();
}

void UChaosImpactGallery::SubmitPost()
{
	if (bPosting || !PostChoices.IsValidIndex(PostChoice))
	{
		return;
	}
	const FString Title = PostTitle.TrimStartAndEnd().Left(16);
	if (Title.IsEmpty())
	{
		Notify(TEXT("作品の名前をつけてください"));
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiDeny);
		return;
	}
	const FPostChoice& Chosen = PostChoices[PostChoice];
	const UChaosImpactColourSubsystem* Colours = UChaosImpactColourSubsystem::Get(Controller.Get());
	const UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(Controller.Get());
	if (!Colours)
	{
		return;
	}
	const TArray<uint8> Parts = Colours->GetPresetParts(Chosen.Character, Chosen.Slot);
	UploadBytes = ChaosImpactBodyPaint::Pack(Colours->GetPresetPaint(Chosen.Character, Chosen.Slot));
	UploadDone.Init(false, (UploadBytes.Num() + GaPieceBytes - 1) / GaPieceBytes);
	UploadAsked.Reset();
	UploadInFlight = 0;
	UploadFails = 0;
	UploadId.Reset();
	bPosting = true;
	PostTitle = Title;
	TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
	Message->SetStringField(TEXT("op"), TEXT("gallery_post"));
	Message->SetStringField(TEXT("token"), Sessions ? Sessions->GetPlayerToken() : FString());
	Message->SetStringField(TEXT("name"), Sessions ? Sessions->GetPlayerName() : FString());
	Message->SetStringField(TEXT("title"), Title);
	Message->SetNumberField(TEXT("chara"), Chosen.Character);
	Message->SetStringField(TEXT("parts"), Parts.IsEmpty() ? FString() : BytesToHexLower(Parts.GetData(), Parts.Num()));
	Message->SetNumberField(TEXT("paint"), UploadBytes.Num());
	ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiConfirm);
	ChaosImpactRelay::Request(Message, [WeakThis = TWeakObjectPtr<UChaosImpactGallery>(this)](TSharedPtr<FJsonObject> Reply, int32)
	{
		UChaosImpactGallery* Self = WeakThis.Get();
		if (!Self || !Self->bPosting)
		{
			return;
		}
		if (!Reply.IsValid() || !Reply->GetBoolField(TEXT("ok")))
		{
			Self->FinishPost(false, Self->Describe(Reply));
			return;
		}
		if (!Reply->TryGetStringField(TEXT("upload"), Self->UploadId) || Self->UploadId.IsEmpty())
		{
			Self->FinishPost(true, FString());
		}
	});
}

void UChaosImpactGallery::PumpUpload()
{
	if (!bPosting || UploadId.IsEmpty())
	{
		return;
	}
	const UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(Controller.Get());
	for (int32 Piece = 0; Piece < UploadDone.Num() && UploadInFlight < GaMostInFlight; ++Piece)
	{
		if (UploadDone[Piece] || UploadAsked.Contains(Piece))
		{
			continue;
		}
		const int32 Offset = Piece * GaPieceBytes;
		const int32 Size = FMath::Min(GaPieceBytes, UploadBytes.Num() - Offset);
		UploadAsked.Add(Piece);
		++UploadInFlight;
		TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
		Message->SetStringField(TEXT("op"), TEXT("gallery_piece"));
		Message->SetStringField(TEXT("token"), Sessions ? Sessions->GetPlayerToken() : FString());
		Message->SetStringField(TEXT("upload"), UploadId);
		Message->SetNumberField(TEXT("offset"), Offset);
		Message->SetStringField(TEXT("data"), FBase64::Encode(UploadBytes.GetData() + Offset, Size));
		const FString Upload = UploadId;
		ChaosImpactRelay::Request(Message, [WeakThis = TWeakObjectPtr<UChaosImpactGallery>(this), Piece, Upload](TSharedPtr<FJsonObject> Reply, int32)
		{
			UChaosImpactGallery* Self = WeakThis.Get();
			if (!Self || !Self->bPosting || Self->UploadId != Upload)
			{
				return;
			}
			Self->UploadAsked.Remove(Piece);
			Self->UploadInFlight = FMath::Max(Self->UploadInFlight - 1, 0);
			if (!Reply.IsValid())
			{
				if (++Self->UploadFails > 12)
				{
					Self->FinishPost(false, Self->Describe(Reply));
				}
				return;
			}
			if (!Reply->GetBoolField(TEXT("ok")))
			{
				Self->FinishPost(false, Self->Describe(Reply));
				return;
			}
			if (Self->UploadDone.IsValidIndex(Piece))
			{
				Self->UploadDone[Piece] = true;
			}
			if (Reply->GetBoolField(TEXT("done")))
			{
				Self->FinishPost(true, FString());
			}
		});
	}
}

void UChaosImpactGallery::FinishPost(const bool bOk, const FString& Message)
{
	bPosting = false;
	UploadId.Reset();
	if (!bOk)
	{
		Notify(Message);
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiDeny);
		return;
	}
	Notify(FString::Printf(TEXT("「%s」を投稿しました！"), *PostTitle));
	ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiReady);
	UE_LOG(LogTemp, Log, TEXT("Gallery: posted \"%s\" (%d bytes of paint)"), *PostTitle, UploadBytes.Num());
	View = EView::List;
	Focus = 0;
	CharacterFilter = INDEX_NONE;
	SetTab(ETab::Mine);
}

// ---- Every frame ------------------------------------------------------------------------------------------------------

void UChaosImpactGallery::Tick(const float DeltaSeconds)
{
	if (!bOpen)
	{
		return;
	}
	const float Step = FMath::Min(DeltaSeconds, 0.1f);
	++Frame;
	PumpDownloads();
	PumpUpload();
	const double Now = FPlatformTime::Seconds();
	// The cards: the one under the cursor moves, the others are filmed for their first seconds (until their textures are in).
	for (int32 Index = 0; Index < Cards.Num(); ++Index)
	{
		AChaosImpactCharacterPreview* Card = Cards[Index];
		if (!IsValid(Card) || !Posts.IsValidIndex(Index) || View != EView::List)
		{
			continue;
		}
		const bool bLive = Focus == Index;
		if (bLive || (Now - CardShownAt[Index] < 4.0 && (Frame + Index) % 5 == 0))
		{
			Card->SetFacing(bLive ? -22.0f + 25.0f * FMath::Sin(static_cast<float>(Now - OpenedAt) * 1.3f) : -22.0f, 0.0f);
			Card->Animate(bLive ? Step : 0.1f);
			Card->FilmNow();
			CardBrushes[Index] = GaBrushOf(Card);
		}
	}
	if (IsValid(Stage) && View != EView::List)
	{
		// The work turns slowly by itself until it is turned by hand (and again a while after).
		if (StickTurn != 0.0f)
		{
			DetailYaw -= StickTurn * 180.0f * Step;
			DetailTouchedAt = Now;
		}
		else if (Now - DetailTouchedAt > 2.5)
		{
			DetailYaw += 24.0f * Step;
		}
		Stage->SetFacing(DetailYaw, 0.0f);
		Stage->Animate(Step);
		Stage->FilmNow();
		StageBrush = GaBrushOf(Stage);
	}
}

// ---- Input ------------------------------------------------------------------------------------------------------------

void UChaosImpactGallery::Activate()
{
	if (View == EView::List)
	{
		if (Focus >= 0 && Focus < PerPage)
		{
			OpenDetail(Focus);
		}
		else if (Focus >= 100 && Focus <= 102)
		{
			SetTab(static_cast<ETab>(Focus - 100));
			ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiTab);
		}
		else if (Focus == 103)
		{
			// Round the characters (すべて first).
			SetCharacterFilter(CharacterFilter + 1 >= ChaosImpactRoster::Num() ? INDEX_NONE : CharacterFilter + 1);
			ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiValue);
		}
		else if (Focus == 104)
		{
			OpenPost();
		}
		else if (Focus == 110 || Focus == 111)
		{
			TurnPage(Focus == 110 ? -1 : 1);
		}
		return;
	}
	if (View == EView::Detail)
	{
		const int32 Button = Focus - 200;
		if (Button == 0)
		{
			SaveToPresets();
		}
		else if (Button == 1 && DetailButtons == 3)
		{
			DeleteDetail();
		}
		else
		{
			CloseDetail();
		}
		return;
	}
	// 投稿する.
	if (bPosting)
	{
		return;
	}
	if (Focus >= 300 && Focus < 400)
	{
		ChoosePost(Focus - 300);
		Focus = 400;
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiSelect);
	}
	else if (Focus == 400)
	{
		Naming.Open(PostTitle, TEXT("作品の名前"));
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiSelect);
	}
	else if (Focus == 401)
	{
		SubmitPost();
	}
	else if (Focus == 402)
	{
		Back();
	}
}

void UChaosImpactGallery::Back()
{
	if (View == EView::Detail)
	{
		CloseDetail();
		return;
	}
	if (View == EView::Post)
	{
		if (bPosting)
		{
			return;
		}
		View = EView::List;
		Focus = 104;
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiBack);
		return;
	}
	bLeave = true;
	ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiBack);
}

void UChaosImpactGallery::Move(const int32 Columns, const int32 Rows)
{
	const int32 Was = Focus;
	if (View == EView::List)
	{
		if (Focus >= 0 && Focus < PerPage)
		{
			const int32 Column = Focus % 4;
			const int32 Row = Focus / 4;
			if (Columns < 0 && Column == 0)
			{
				if (Page > 0) { TurnPage(-1); Focus = Row * 4 + 3; } else { Focus = 110; }
			}
			else if (Columns > 0 && (Column == 3 || Focus + 1 >= Posts.Num()))
			{
				if (Page + 1 < Pages) { TurnPage(1); Focus = Row * 4; } else { Focus = 111; }
			}
			else if (Rows < 0 && Row == 0)
			{
				Focus = Column <= 1 ? 100 + Column : Column == 2 ? 103 : 104;
			}
			else
			{
				const int32 Next = Focus + Columns + Rows * 4;
				Focus = Posts.IsValidIndex(Next) ? Next : Focus;
			}
		}
		else if (Focus >= 100 && Focus <= 104)
		{
			if (Rows > 0)
			{
				const int32 Column = Focus <= 101 ? Focus - 100 : Focus == 102 ? 1 : Focus == 103 ? 2 : 3;
				Focus = Posts.IsEmpty() ? Focus : FMath::Min(Column, Posts.Num() - 1);
			}
			else if (Columns != 0)
			{
				Focus = FMath::Clamp(Focus + Columns, 100, 104);
			}
		}
		else if (Focus == 110 || Focus == 111)
		{
			if (Columns != 0 || Rows != 0)
			{
				Focus = Posts.IsEmpty() ? 104 : Focus == 110 ? 0 : FMath::Min(3, Posts.Num() - 1);
			}
		}
	}
	else if (View == EView::Detail)
	{
		if (Columns != 0 || Rows != 0)
		{
			const int32 Step = Columns != 0 ? Columns : Rows;
			Focus = 200 + FMath::Clamp(Focus - 200 + Step, 0, DetailButtons - 1);
		}
	}
	else if (!bPosting)
	{
		if (Focus >= 300 && Focus < 400)
		{
			if (Columns > 0)
			{
				Focus = 400;
			}
			else if (Rows != 0)
			{
				const int32 Next = FMath::Clamp(Focus - 300 + Rows, 0, FMath::Max(PostChoices.Num() - 1, 0));
				Focus = 300 + Next;
				ChoosePost(Next);
			}
		}
		else
		{
			if (Columns < 0 && !PostChoices.IsEmpty())
			{
				Focus = 300 + PostChoice;
			}
			else if (Rows != 0)
			{
				Focus = FMath::Clamp(Focus + Rows, PostChoices.IsEmpty() ? 401 : 400, 402);
			}
		}
	}
	if (Focus != Was)
	{
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiMove);
	}
}

bool UChaosImpactGallery::HandleKeyDown(const FKeyEvent& Event)
{
	if (!bOpen)
	{
		return false;
	}
	const FKey Raw = Event.GetKey();
	if (Raw.IsMouseButton())
	{
		return false;
	}
	const FKey Key = ChaosImpactSettings::ToMenuKey(Raw, Event.GetInputDeviceId().GetId());
	const bool bRepeat = Event.IsRepeat();
	if (Naming.IsOpen())
	{
		Naming.HandleKey(Key, bRepeat);
		FString Name;
		if (Naming.ConsumeResult(Name) == FChaosImpactNameEntry::EResult::Done && !Name.TrimStartAndEnd().IsEmpty())
		{
			PostTitle = Name.TrimStartAndEnd().Left(16);
			Focus = 401;
		}
		return true;
	}
	if (GaIsAny(Key, {EKeys::Gamepad_DPad_Up, EKeys::Up, EKeys::W})) { Move(0, -1); }
	else if (GaIsAny(Key, {EKeys::Gamepad_DPad_Down, EKeys::Down, EKeys::S})) { Move(0, 1); }
	else if (GaIsAny(Key, {EKeys::Gamepad_DPad_Left, EKeys::Left, EKeys::A})) { Move(-1, 0); }
	else if (GaIsAny(Key, {EKeys::Gamepad_DPad_Right, EKeys::Right, EKeys::D})) { Move(1, 0); }
	else if (!bRepeat && GaIsAny(Key, {EKeys::Gamepad_FaceButton_Bottom, EKeys::Enter, EKeys::SpaceBar})) { Activate(); }
	else if (!bRepeat && GaIsAny(Key, {EKeys::Gamepad_FaceButton_Right, EKeys::Escape, EKeys::BackSpace})) { Back(); }
	else if (!bRepeat && GaIsAny(Key, {EKeys::Gamepad_LeftShoulder, EKeys::Q, EKeys::Gamepad_RightShoulder, EKeys::E}))
	{
		const int32 Direction = Key == EKeys::Gamepad_LeftShoulder || Key == EKeys::Q ? -1 : 1;
		if (View == EView::List)
		{
			SetTab(static_cast<ETab>((static_cast<int32>(Tab) + Direction + 3) % 3));
			ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiTab);
		}
		else if (View == EView::Detail && Posts.Num() > 1)
		{
			// The work beside it.
			OpenDetail((DetailIndex + Direction + Posts.Num()) % Posts.Num());
		}
	}
	else if (View == EView::List && GaIsAny(Key, {EKeys::Gamepad_LeftTrigger, EKeys::LeftBracket, EKeys::Gamepad_RightTrigger, EKeys::RightBracket}))
	{
		const int32 Direction = Key == EKeys::Gamepad_LeftTrigger || Key == EKeys::LeftBracket ? -1 : 1;
		const int32 Count = ChaosImpactRoster::Num() + 1;
		SetCharacterFilter(((CharacterFilter + 1 + Direction + Count) % Count) - 1);
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiValue);
	}
	else if (!bRepeat && View == EView::List && GaIsAny(Key, {EKeys::Gamepad_FaceButton_Left, EKeys::P})) { OpenPost(); }
	else if (!bRepeat && View == EView::List && GaIsAny(Key, {EKeys::Gamepad_FaceButton_Top, EKeys::R}))
	{
		Refresh();
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiValue);
	}
	else if (!bRepeat && View == EView::Post && GaIsAny(Key, {EKeys::Gamepad_FaceButton_Top, EKeys::N}) && !bPosting)
	{
		Naming.Open(PostTitle, TEXT("作品の名前"));
	}
	else if (!bRepeat && View == EView::Post && GaIsAny(Key, {EKeys::Gamepad_Special_Right}) && !bPosting) { SubmitPost(); }
	else if (View != EView::List && GaIsAny(Key, {EKeys::Z, EKeys::X}))
	{
		DetailYaw += Key == EKeys::Z ? -12.0f : 12.0f;
		DetailTouchedAt = FPlatformTime::Seconds();
	}
	return true;
}

bool UChaosImpactGallery::HandleAnalog(const FAnalogInputEvent& Event)
{
	if (!bOpen)
	{
		return false;
	}
	const FKey Key = Event.GetKey();
	const float Value = Event.GetAnalogValue();
	if (Naming.IsOpen())
	{
		Naming.HandleAnalog(Key, Value);
		return true;
	}
	if (Key == EKeys::Gamepad_RightX)
	{
		StickTurn = FMath::Abs(Value) > 0.25f ? Value : 0.0f;
		return true;
	}
	if (Key != EKeys::Gamepad_LeftX && Key != EKeys::Gamepad_LeftY)
	{
		return false;
	}
	const bool bY = Key == EKeys::Gamepad_LeftY;
	bool& bHeld = bStickHeld[bY ? 1 : 0];
	if (FMath::Abs(Value) < 0.35f)
	{
		bHeld = false;
		return true;
	}
	if (FMath::Abs(Value) >= 0.65f && (!bHeld || FPlatformTime::Seconds() >= NextStickAt))
	{
		NextStickAt = FPlatformTime::Seconds() + (bHeld ? 0.12 : 0.35);
		bHeld = true;
		Move(bY ? 0 : (Value > 0.0f ? 1 : -1), bY ? (Value > 0.0f ? -1 : 1) : 0);
	}
	return true;
}

bool UChaosImpactGallery::HandleCharacter(const TCHAR Character)
{
	if (!bOpen || !Naming.IsOpen())
	{
		return false;
	}
	const bool bUsed = Naming.HandleCharacter(Character);
	FString Name;
	if (Naming.ConsumeResult(Name) == FChaosImpactNameEntry::EResult::Done && !Name.TrimStartAndEnd().IsEmpty())
	{
		PostTitle = Name.TrimStartAndEnd().Left(16);
		Focus = 401;
	}
	return bUsed;
}

int32 UChaosImpactGallery::HitTest(const FVector2D& Point) const
{
	if (View == EView::List)
	{
		for (int32 Index = 0; Index < Posts.Num(); ++Index)
		{
			if (CardRect(Index).IsInside(Point)) { return Index; }
		}
		for (int32 Index = 0; Index < 3; ++Index)
		{
			if (TabRect(Index).IsInside(Point)) { return 100 + Index; }
		}
		if (FilterRect().IsInside(Point)) { return 103; }
		if (PostButtonRect().IsInside(Point)) { return 104; }
		if (Page > 0 && PageArrowRect(-1).IsInside(Point)) { return 110; }
		if (Page + 1 < Pages && PageArrowRect(1).IsInside(Point)) { return 111; }
		return INDEX_NONE;
	}
	if (View == EView::Detail)
	{
		for (int32 Index = 0; Index < DetailButtons; ++Index)
		{
			if (DetailButtonRect(Index).IsInside(Point)) { return 200 + Index; }
		}
		return INDEX_NONE;
	}
	for (int32 Row = 0; Row < GaPostRows; ++Row)
	{
		if (PostChoices.IsValidIndex(PostScroll + Row) && PostRowRect(Row).IsInside(Point)) { return 300 + PostScroll + Row; }
	}
	if (PostFieldRect().IsInside(Point)) { return 400; }
	for (int32 Index = 0; Index < 2; ++Index)
	{
		if (PostActionRect(Index).IsInside(Point)) { return 401 + Index; }
	}
	return INDEX_NONE;
}

void UChaosImpactGallery::HandleMouseMove(const FVector2D& Point, const bool bLeftDown)
{
	if (!bOpen)
	{
		return;
	}
	if (Naming.IsOpen())
	{
		Naming.HandleMouseMove(Point);
		return;
	}
	if (bDragging && bLeftDown)
	{
		DetailYaw -= static_cast<float>(Point.X - LastMouse.X) * 0.6f;
		DetailTouchedAt = FPlatformTime::Seconds();
		LastMouse = Point;
		return;
	}
	bDragging = false;
	const int32 Hit = HitTest(Point);
	if (Hit != INDEX_NONE && Hit != Focus && !(View == EView::Post && bPosting))
	{
		Focus = Hit;
		if (View == EView::Post && Hit >= 300 && Hit < 400)
		{
			// (Pointing at a preset only shows it once clicked.)
			Focus = Hit;
		}
	}
}

void UChaosImpactGallery::HandleMouseDown(const FVector2D& Point, const FKey& Button)
{
	if (!bOpen)
	{
		return;
	}
	if (Naming.IsOpen())
	{
		Naming.HandleClick(Point);
		FString Name;
		if (Naming.ConsumeResult(Name) == FChaosImpactNameEntry::EResult::Done && !Name.TrimStartAndEnd().IsEmpty())
		{
			PostTitle = Name.TrimStartAndEnd().Left(16);
			Focus = 401;
		}
		return;
	}
	if (Button == EKeys::RightMouseButton)
	{
		Back();
		return;
	}
	LastMouse = Point;
	if (View != EView::List && FBox2D(FVector2D(90.0, 96.0), FVector2D(680.0, 830.0)).IsInside(Point))
	{
		// The work turned by dragging.
		bDragging = true;
		return;
	}
	const int32 Hit = HitTest(Point);
	if (Hit != INDEX_NONE)
	{
		Focus = Hit;
		Activate();
	}
}

void UChaosImpactGallery::HandleMouseUp(const FVector2D& Point, const FKey& Button)
{
	bDragging = false;
}

void UChaosImpactGallery::HandleWheel(const FVector2D& Point, const float Delta)
{
	if (!bOpen || Naming.IsOpen() || Delta == 0.0f)
	{
		return;
	}
	if (View == EView::List)
	{
		TurnPage(Delta > 0.0f ? -1 : 1);
	}
	else if (View == EView::Post && !PostChoices.IsEmpty())
	{
		PostScroll = FMath::Clamp(PostScroll + (Delta > 0.0f ? -1 : 1), 0, FMath::Max(PostChoices.Num() - GaPostRows, 0));
	}
}

// ---- Layout -----------------------------------------------------------------------------------------------------------

FBox2D UChaosImpactGallery::CardRect(const int32 Index)
{
	const FVector2D At(GaGridLeft + (Index % 4) * (GaCardWidth + GaCardGap), GaGridTop + (Index / 4) * (GaCardHeight + GaRowGap));
	return FBox2D(At, At + FVector2D(GaCardWidth, GaCardHeight));
}

FBox2D UChaosImpactGallery::TabRect(const int32 Index)
{
	const FVector2D At(GaGridLeft + Index * 168.0f, 112.0f);
	return FBox2D(At, At + FVector2D(Index == 2 ? 190.0f : 156.0f, 48.0f));
}

FBox2D UChaosImpactGallery::FilterRect()
{
	return FBox2D(FVector2D(GaGridLeft + 560.0f, 112.0f), FVector2D(GaGridLeft + 900.0f, 160.0f));
}

FBox2D UChaosImpactGallery::PostButtonRect()
{
	return FBox2D(FVector2D(1600.0f - GaGridLeft - 290.0f, 104.0f), FVector2D(1600.0f - GaGridLeft, 164.0f));
}

FBox2D UChaosImpactGallery::PageArrowRect(const int32 Direction)
{
	const float X = Direction < 0 ? GaGridLeft - 58.0f : 1600.0f - GaGridLeft + 10.0f;
	return FBox2D(FVector2D(X, 440.0f), FVector2D(X + 48.0f, 560.0f));
}

FBox2D UChaosImpactGallery::DetailButtonRect(const int32 Index)
{
	const FVector2D At(720.0f + Index * 262.0f, 726.0f);
	return FBox2D(At, At + FVector2D(246.0f, 74.0f));
}

FBox2D UChaosImpactGallery::PostRowRect(const int32 Row)
{
	const FVector2D At(GaGridLeft, 168.0f + Row * 70.0f);
	return FBox2D(At, At + FVector2D(560.0f, 62.0f));
}

FBox2D UChaosImpactGallery::PostFieldRect()
{
	return FBox2D(FVector2D(1150.0f, 214.0f), FVector2D(1500.0f, 282.0f));
}

FBox2D UChaosImpactGallery::PostActionRect(const int32 Index)
{
	return Index == 0 ? FBox2D(FVector2D(1150.0f, 640.0f), FVector2D(1500.0f, 720.0f)) : FBox2D(FVector2D(1150.0f, 736.0f), FVector2D(1500.0f, 796.0f));
}

// ---- Drawing ----------------------------------------------------------------------------------------------------------

int32 UChaosImpactGallery::Paint(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer) const
{
	if (!bOpen)
	{
		return Layer;
	}
	const double Now = FPlatformTime::Seconds();
	const float Time = static_cast<float>(Now - OpenedAt);
	const float Pulse = 0.65f + 0.35f * FMath::Sin(Time * 6.0f);
	const FPainter Back{Design, Elements, Layer};
	const FPainter P{Design, Elements, Layer + 1};
	const FPainter Over{Design, Elements, Layer + 2};
	const FPainter Top{Design, Elements, Layer + 3};
	Back.Box(-1400.0f, 0.0f, 4400.0f, 900.0f, GaBack);
	// A soft wash of colour behind, slowly drifting (the gallery's own light).
	for (int32 Blob = 0; Blob < 3; ++Blob)
	{
		const FLinearColor Wash = Blob == 0 ? GaPink : Blob == 1 ? Ice : Gold;
		const FVector2D C(300.0 + Blob * 520.0 + 60.0 * FMath::Sin(Time * 0.3f + Blob), 470.0 + 80.0 * FMath::Cos(Time * 0.25f + Blob * 2.0f));
		Back.Disc(C, 300.0f, WithAlpha(Wash, 0.035f));
	}
	const FString Heading = View == EView::Post ? TEXT("投稿する") : TEXT("みんなのカラー");
	HudText3D(P, TEXT("GALLERY"), GaGridLeft, 18.0f, 54.0f, Paper, ETextAlign::Left, TEXT("BlackItalic"));
	P.Text(Heading, GaGridLeft + HudMeasure(TEXT("GALLERY"), 54.0f, TEXT("BlackItalic")) + 26.0f, 38.0f, 30.0f, GaPink, ETextAlign::Left, TEXT("Black"));
	P.Text(View == EView::Post ? TEXT("自分のプリセットを、みんなに見せよう") : TEXT("みんなが作ったカスタムカラー。気に入ったら自分のプリセットに保存しよう"),
		1600.0f - GaGridLeft, 46.0f, 16.0f, Muted, ETextAlign::Right, TEXT("Bold"));

	if (View == EView::List)
	{
		// Tabs, the character filter, 投稿する.
		for (int32 Index = 0; Index < 3; ++Index)
		{
			const FBox2D R = TabRect(Index);
			const bool bOn = static_cast<int32>(Tab) == Index;
			if (Focus == 100 + Index)
			{
				GaGlow(Back, R, 10.0f, WithAlpha(Gold, Pulse), 3.0f);
			}
			GaRound(Back, R, 10.0f, bOn ? GaPink : GaCard);
			GaText(P, GaTabNames[Index], static_cast<float>(R.GetCenter().X), static_cast<float>(R.Min.Y) + 11.0f, 20.0f,
				static_cast<float>(R.GetSize().X) - 16.0f, bOn ? Ink : Paper, ETextAlign::Center, TEXT("Black"));
		}
		P.Text(TEXT("LB / RB ・ Q / E"), TabRect(0).Min.X, TabRect(0).Max.Y + 2.0f, 11.0f, WithAlpha(Muted, 0.8f), ETextAlign::Left, TEXT("Bold"));
		{
			const FBox2D R = FilterRect();
			if (Focus == 103)
			{
				GaGlow(Back, R, 10.0f, WithAlpha(Gold, Pulse), 3.0f);
			}
			GaRound(Back, R, 10.0f, CharacterFilter >= 0 ? GaCardOn : GaCard);
			const FString Label = FString::Printf(TEXT("キャラ：%s"), CharacterFilter >= 0 ? *CharacterName(CharacterFilter) : TEXT("すべて"));
			GaText(P, Label, static_cast<float>(R.Min.X) + 40.0f, static_cast<float>(R.Min.Y) + 12.0f, 18.0f, static_cast<float>(R.GetSize().X) - 80.0f,
				Paper, ETextAlign::Left, TEXT("Black"));
			P.Text(TEXT("◀"), static_cast<float>(R.Min.X) + 14.0f, static_cast<float>(R.Min.Y) + 12.0f, 16.0f, WithAlpha(Paper, 0.7f), ETextAlign::Left, TEXT("Black"));
			P.Text(TEXT("▶"), static_cast<float>(R.Max.X) - 14.0f, static_cast<float>(R.Min.Y) + 12.0f, 16.0f, WithAlpha(Paper, 0.7f), ETextAlign::Right, TEXT("Black"));
			P.Text(TEXT("LT / RT ・ [ ]"), R.Min.X, R.Max.Y + 2.0f, 11.0f, WithAlpha(Muted, 0.8f), ETextAlign::Left, TEXT("Bold"));
		}
		{
			const FBox2D R = PostButtonRect();
			const bool bOn = Focus == 104;
			if (bOn)
			{
				GaGlow(Back, R, 14.0f, WithAlpha(Paper, Pulse), 3.0f);
			}
			GaRound(Back, R, 14.0f, Gold);
			GaPen(P, FVector2D(R.Min.X + 40.0f, R.GetCenter().Y), 30.0f, Ink, GaPink);
			GaText(P, TEXT("投稿する"), static_cast<float>(R.Min.X) + 70.0f, static_cast<float>(R.Min.Y) + 13.0f, 26.0f, 150.0f, Ink, ETextAlign::Left,
				TEXT("Black"));
			P.Text(TEXT("X / P"), static_cast<float>(R.Max.X) - 14.0f, static_cast<float>(R.Min.Y) + 22.0f, 13.0f, WithAlpha(Ink, 0.7f), ETextAlign::Right,
				TEXT("Bold"));
		}

		// The cards.
		for (int32 Index = 0; Index < PerPage; ++Index)
		{
			const FBox2D R = CardRect(Index);
			if (!Posts.IsValidIndex(Index))
			{
				if (bListLoaded && ErrorText.IsEmpty() && !Posts.IsEmpty())
				{
					GaRound(Back, R, 16.0f, WithAlpha(GaCard, 0.35f));
				}
				continue;
			}
			const FPost& Post = Posts[Index];
			const bool bOn = Focus == Index;
			const float Lift = bOn ? 6.0f : 0.0f;
			const FBox2D Card(R.Min - FVector2D(0.0, Lift), R.Max - FVector2D(0.0, Lift));
			if (bOn)
			{
				GaGlow(Back, Card, 16.0f, WithAlpha(Gold, Pulse), 4.0f);
			}
			GaRound(Back, Card, 16.0f, bOn ? GaCardOn : GaCard);
			// The work: its character standing on the left.
			const FBox2D Picture(Card.Min + FVector2D(10.0, 10.0), FVector2D(Card.Min.X + 150.0, Card.Max.Y - 10.0));
			GaRound(P, Picture, 12.0f, FLinearColor(0.012f, 0.02f, 0.05f, 1.0f));
			if (CardBrushes.IsValidIndex(Index) && CardBrushes[Index].GetResourceObject())
			{
				FSlateBrush Face = CardBrushes[Index];
				const float Across = static_cast<float>(Picture.GetSize().X / Picture.GetSize().Y) * 0.75f;
				Face.SetUVRegion(FBox2f(FVector2f(0.5f - Across * 0.5f, 0.0f), FVector2f(0.5f + Across * 0.5f, 1.0f)));
				GaPicture(Over, Face, Picture);
			}
			// Its words on the right.
			const float TX = static_cast<float>(Card.Min.X) + 164.0f;
			const float TW = static_cast<float>(Card.Max.X) - TX - 14.0f;
			const float TY = static_cast<float>(Card.Min.Y);
			GaText(P, Post.Title, TX, TY + 18.0f, 24.0f, TW, Paper, ETextAlign::Left, TEXT("Black"));
			P.Box(TX, TY + 56.0f, 40.0f, 3.0f, GaPink);
			GaText(P, Post.Poster, TX, TY + 70.0f, 17.0f, TW, WithAlpha(Paper, 0.9f), ETextAlign::Left, TEXT("Bold"));
			P.Text(TEXT("投稿者"), TX, TY + 94.0f, 11.0f, Muted, ETextAlign::Left, TEXT("Bold"));
			GaText(P, FormatDate(Post.At), TX, TY + 118.0f, 14.0f, TW, WithAlpha(Paper, 0.75f));
			P.Text(TEXT("投稿日"), TX, TY + 138.0f, 11.0f, Muted, ETextAlign::Left, TEXT("Bold"));
			// The character's name, the hearts and the paint.
			const FBox2D Chip(FVector2D(TX, TY + 166.0f), FVector2D(TX + TW, TY + 194.0f));
			GaRound(P, Chip, 8.0f, FLinearColor(0.02f, 0.04f, 0.1f, 1.0f));
			GaText(Over, CharacterName(Post.Character), TX + 10.0f, TY + 171.0f, 14.0f, TW - 20.0f, Ice, ETextAlign::Left, TEXT("Bold"));
			GaHeart(P, FVector2D(TX + 12.0f, TY + 226.0f), 20.0f, Post.bSaved ? GaPink : WithAlpha(GaPink, 0.55f));
			P.Text(FString::FromInt(Post.Saves), TX + 30.0f, TY + 214.0f, 18.0f, Paper, ETextAlign::Left, TEXT("Black"));
			if (Post.PaintBytes > 0)
			{
				const float Got = DownloadProgress(Post.Id);
				GaPen(P, FVector2D(TX + 86.0f, TY + 226.0f), 20.0f, Paper, GaPink);
				GaText(P, Got >= 1.0f ? FString(TEXT("ペイント")) : FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Got * 100.0f)), TX + 102.0f, TY + 216.0f,
					14.0f, TW - 104.0f, WithAlpha(Paper, 0.85f));
			}
			// Swatches of its parts along the bottom.
			const int32 Parts = FMath::Min(ChaosImpactCustomColour::GetPartCount(Post.Character), 6);
			for (int32 Part = 0; Part < Parts; ++Part)
			{
				FColor Colour;
				const bool bOnPart = ChaosImpactCustomColour::GetPart(Post.Parts, Part, Colour);
				const FBox2D Swatch(FVector2D(TX + Part * 26.0f, TY + 256.0f), FVector2D(TX + Part * 26.0f + 20.0f, TY + 276.0f));
				GaRound(P, Swatch, 5.0f, bOnPart ? FLinearColor(Colour) : FLinearColor(0.12f, 0.15f, 0.24f, 1.0f));
			}
			if (Post.bMine)
			{
				const FBox2D Badge(FVector2D(Card.Max.X - 64.0, Card.Min.Y - 10.0), FVector2D(Card.Max.X - 6.0, Card.Min.Y + 14.0));
				GaRound(Top, Badge, 8.0f, Gold);
				Top.Text(TEXT("自分"), static_cast<float>(Badge.GetCenter().X), static_cast<float>(Badge.Min.Y) + 2.0f, 14.0f, Ink, ETextAlign::Center, TEXT("Black"));
			}
		}
		// The pages.
		for (const int32 Direction : {-1, 1})
		{
			const bool bThere = Direction < 0 ? Page > 0 : Page + 1 < Pages;
			if (!bThere)
			{
				continue;
			}
			const FBox2D R = PageArrowRect(Direction);
			const bool bOn = Focus == (Direction < 0 ? 110 : 111);
			GaRound(Back, R, 12.0f, bOn ? Gold : GaCard);
			P.Text(Direction < 0 ? TEXT("◀") : TEXT("▶"), static_cast<float>(R.GetCenter().X), static_cast<float>(R.GetCenter().Y) - 16.0f, 26.0f,
				bOn ? Ink : Paper, ETextAlign::Center, TEXT("Black"));
		}
		if (bListLoaded && ErrorText.IsEmpty())
		{
			P.Text(FString::Printf(TEXT("%d / %d ページ ・ %d作品"), Page + 1, Pages, Total), 1600.0f - GaGridLeft, 166.0f, 14.0f, WithAlpha(Paper, 0.8f),
				ETextAlign::Right, TEXT("Bold"));
		}
		// Waiting, nothing there, or no way through.
		const FVector2D Middle(800.0, 480.0);
		if (!ErrorText.IsEmpty())
		{
			GaRound(P, FBox2D(Middle - FVector2D(420.0, 70.0), Middle + FVector2D(420.0, 70.0)), 18.0f, GaCard);
			GaText(Over, ErrorText, static_cast<float>(Middle.X), static_cast<float>(Middle.Y) - 34.0f, 24.0f, 780.0f, Paper, ETextAlign::Center, TEXT("Black"));
			Over.Text(TEXT("Y / R：もう一度"), static_cast<float>(Middle.X), static_cast<float>(Middle.Y) + 14.0f, 17.0f, Muted, ETextAlign::Center, TEXT("Bold"));
		}
		else if (bListPending && Posts.IsEmpty())
		{
			GaSpinner(Over, Middle - FVector2D(0.0, 20.0), 26.0f, Time);
			Over.Text(TEXT("読みこんでいます…"), static_cast<float>(Middle.X), static_cast<float>(Middle.Y) + 24.0f, 20.0f, Paper, ETextAlign::Center, TEXT("Bold"));
		}
		else if (bListLoaded && Posts.IsEmpty())
		{
			const FString Empty = Tab == ETab::Mine ? TEXT("まだ投稿していません") : TEXT("まだ作品がありません");
			GaText(Over, Empty, static_cast<float>(Middle.X), static_cast<float>(Middle.Y) - 40.0f, 30.0f, 800.0f, Paper, ETextAlign::Center, TEXT("Black"));
			Over.Text(TEXT("右上の「投稿する」から、自分のカラーを投稿しよう"), static_cast<float>(Middle.X), static_cast<float>(Middle.Y) + 10.0f, 18.0f, Muted,
				ETextAlign::Center, TEXT("Bold"));
		}
		else if (bListPending)
		{
			GaSpinner(Top, FVector2D(1600.0 - GaGridLeft - 320.0, 134.0), 12.0f, Time);
		}
	}
	else if (View == EView::Detail && Posts.IsValidIndex(DetailIndex))
	{
		const FPost& Post = Posts[DetailIndex];
		// 全体図: the work large, turning.
		const FBox2D Shown(FVector2D(110.0, 96.0), FVector2D(650.0, 816.0));
		GaRound(Back, GaGrow(Shown, 8.0f), 22.0f, FLinearColor(0.02f, 0.035f, 0.09f, 1.0f));
		GaPicture(P, StageBrush, Shown);
		Over.Text(TEXT("ドラッグ・右スティック・Z / X で回す"), static_cast<float>(Shown.GetCenter().X), static_cast<float>(Shown.Max.Y) - 34.0f, 14.0f,
			WithAlpha(Paper, 0.65f), ETextAlign::Center, TEXT("Bold"));
		// Its details.
		const float X = 720.0f;
		const float W = 780.0f;
		GaText(P, Post.Title, X, 104.0f, 46.0f, W, Paper, ETextAlign::Left, TEXT("Black"));
		P.Box(X, 180.0f, 90.0f, 4.0f, GaPink);
		struct FLine { const TCHAR* Label; FString Value; };
		const FLine Lines[] = {{TEXT("投稿者"), Post.Poster}, {TEXT("投稿日"), FormatDate(Post.At)}, {TEXT("キャラ"), CharacterName(Post.Character)}};
		for (int32 Row = 0; Row < UE_ARRAY_COUNT(Lines); ++Row)
		{
			const float Y = 198.0f + Row * 48.0f;
			GaRound(Back, FBox2D(FVector2D(X, Y), FVector2D(X + W, Y + 42.0f)), 10.0f, GaCard);
			P.Text(Lines[Row].Label, X + 16.0f, Y + 11.0f, 16.0f, Muted, ETextAlign::Left, TEXT("Bold"));
			GaText(P, Lines[Row].Value, X + 120.0f, Y + 8.0f, 20.0f, W - 140.0f, Paper, ETextAlign::Left, TEXT("Black"));
		}
		{
			const float Y = 344.0f;
			GaHeart(P, FVector2D(X + 18.0f, Y + 16.0f), 26.0f, Post.bSaved ? GaPink : WithAlpha(GaPink, 0.6f));
			P.Text(FString::Printf(TEXT("%d人が保存"), Post.Saves), X + 44.0f, Y + 2.0f, 22.0f, Paper, ETextAlign::Left, TEXT("Black"));
			const FString PaintLine = Post.PaintBytes <= 0 ? FString(TEXT("ペイントなし"))
				: IsDetailPaintReady() ? FString(TEXT("ペイントあり"))
				: FString::Printf(TEXT("ペイントを読みこみ中 %d%%"), FMath::RoundToInt(DownloadProgress(Post.Id) * 100.0f));
			GaPen(P, FVector2D(X + 290.0f, Y + 16.0f), 24.0f, Post.PaintBytes > 0 ? Paper : Muted, GaPink);
			P.Text(PaintLine, X + 312.0f, Y + 4.0f, 20.0f, Post.PaintBytes > 0 ? Paper : Muted, ETextAlign::Left, TEXT("Bold"));
		}
		// The parts' colours.
		P.Text(TEXT("部位の色"), X, 404.0f, 18.0f, Muted, ETextAlign::Left, TEXT("Bold"));
		const TArray<FString>& Names = ChaosImpactCustomColour::GetPartNames(ChaosImpactRoster::ClampIndex(Post.Character));
		for (int32 Part = 0; Part < FMath::Min(Names.Num(), 8); ++Part)
		{
			const FBox2D R(FVector2D(X + (Part % 2) * 396.0f, 432.0f + (Part / 2) * 54.0f), FVector2D(X + (Part % 2) * 396.0f + 384.0f, 432.0f + (Part / 2) * 54.0f + 46.0f));
			GaRound(Back, R, 10.0f, GaCard);
			GaText(P, Names[Part], static_cast<float>(R.Min.X) + 14.0f, static_cast<float>(R.Min.Y) + 10.0f, 18.0f, 230.0f, Paper, ETextAlign::Left, TEXT("Bold"));
			FColor Colour;
			const bool bOnPart = ChaosImpactCustomColour::GetPart(Post.Parts, Part, Colour);
			const FBox2D Swatch(FVector2D(R.Max.X - 120.0, R.Min.Y + 8.0), FVector2D(R.Max.X - 12.0, R.Max.Y - 8.0));
			GaRound(P, Swatch, 7.0f, bOnPart ? FLinearColor(Colour) : FLinearColor(0.12f, 0.15f, 0.24f, 1.0f));
			if (!bOnPart)
			{
				Over.Text(TEXT("元の色"), static_cast<float>(Swatch.GetCenter().X), static_cast<float>(Swatch.Min.Y) + 5.0f, 14.0f, Paper, ETextAlign::Center, TEXT("Bold"));
			}
		}
		// Its buttons.
		for (int32 Index = 0; Index < DetailButtons; ++Index)
		{
			const FBox2D R = DetailButtonRect(Index);
			const int32 Kind = Index == 0 ? 0 : DetailButtons == 3 && Index == 1 ? 1 : 2;
			const bool bOn = Focus == 200 + Index;
			const bool bWaiting = Kind == 0 && !IsDetailPaintReady();
			const FLinearColor Accent = Kind == 0 ? Gold : Kind == 1 ? (bDeleteArmed ? Fire : FLinearColor(0.6f, 0.2f, 0.25f, 1.0f)) : Muted;
			if (bOn)
			{
				GaGlow(Back, R, 14.0f, WithAlpha(Paper, Pulse), 3.0f);
			}
			GaRound(Back, R, 14.0f, bOn ? Accent : GaCard);
			P.Box(static_cast<float>(R.Min.X) + 12.0f, static_cast<float>(R.Max.Y) - 8.0f, static_cast<float>(R.GetSize().X) - 24.0f, 3.0f, Accent);
			const FString Label = Kind == 0 ? (bWaiting ? FString(TEXT("読みこみ中…")) : FString(TEXT("プリセットに保存"))) : Kind == 1 ? FString(TEXT("投稿を消す"))
				: FString(TEXT("もどる"));
			GaText(P, Label, static_cast<float>(R.GetCenter().X), static_cast<float>(R.Min.Y) + 21.0f, 24.0f, static_cast<float>(R.GetSize().X) - 24.0f,
				bOn ? Ink : bWaiting ? Muted : Paper, ETextAlign::Center, TEXT("Black"));
		}
		P.Text(FString::Printf(TEXT("%d / %d"), DetailIndex + 1, Posts.Num()), 1500.0f, 690.0f, 16.0f, Muted, ETextAlign::Right, TEXT("Bold"));
	}
	else if (View == EView::Post)
	{
		const UChaosImpactColourSubsystem* Colours = UChaosImpactColourSubsystem::Get(Controller.Get());
		P.Text(TEXT("投稿するカラー"), GaGridLeft, 128.0f, 20.0f, Paper, ETextAlign::Left, TEXT("Black"));
		if (PostChoices.IsEmpty())
		{
			GaRound(Back, FBox2D(FVector2D(GaGridLeft, 168.0), FVector2D(GaGridLeft + 560.0, 330.0)), 16.0f, GaCard);
			GaText(P, TEXT("まだ保存したカラーがありません"), GaGridLeft + 280.0f, 200.0f, 24.0f, 520.0f, Paper, ETextAlign::Center, TEXT("Black"));
			GaText(P, TEXT("カラーエディットで作って保存すると、ここから投稿できます"), GaGridLeft + 280.0f, 252.0f, 16.0f, 520.0f, Muted, ETextAlign::Center);
		}
		for (int32 Row = 0; Row < GaPostRows; ++Row)
		{
			const int32 Choice = PostScroll + Row;
			if (!PostChoices.IsValidIndex(Choice) || !Colours)
			{
				continue;
			}
			const FPostChoice& Each = PostChoices[Choice];
			const TArray<FChaosImpactColourPreset>& Presets = Colours->GetPresets(Each.Character);
			if (!Presets.IsValidIndex(Each.Slot))
			{
				continue;
			}
			const FChaosImpactColourPreset& Preset = Presets[Each.Slot];
			const FBox2D R = PostRowRect(Row);
			const bool bChosen = Choice == PostChoice;
			if (Focus == 300 + Choice)
			{
				GaGlow(Back, R, 12.0f, WithAlpha(Gold, Pulse), 3.0f);
			}
			GaRound(Back, R, 12.0f, bChosen ? GaCardOn : GaCard);
			const int32 Stripes = FMath::Max(1, FMath::Min(ChaosImpactCustomColour::GetPartCount(Each.Character), 4));
			for (int32 Stripe = 0; Stripe < Stripes; ++Stripe)
			{
				FColor Colour;
				const bool bOnPart = ChaosImpactCustomColour::GetPart(Preset.Parts, Stripe, Colour);
				P.Box(static_cast<float>(R.Min.X) + 12.0f + Stripe * 40.0f / Stripes, static_cast<float>(R.Min.Y) + 11.0f, 40.0f / Stripes, 40.0f,
					bOnPart ? FLinearColor(Colour) : FLinearColor(0.2f, 0.22f, 0.28f, 1.0f));
			}
			GaText(P, Preset.Name, static_cast<float>(R.Min.X) + 66.0f, static_cast<float>(R.Min.Y) + 6.0f, 20.0f, 300.0f, Paper, ETextAlign::Left, TEXT("Black"));
			GaText(P, CharacterName(Each.Character), static_cast<float>(R.Min.X) + 66.0f, static_cast<float>(R.Min.Y) + 36.0f, 14.0f, 300.0f, Ice);
			if (!Preset.Paint.IsEmpty())
			{
				GaPen(P, FVector2D(R.Max.X - 120.0, R.GetCenter().Y), 22.0f, Paper, GaPink);
				P.Text(TEXT("ペイント"), static_cast<float>(R.Max.X) - 104.0f, static_cast<float>(R.Min.Y) + 20.0f, 15.0f, Paper, ETextAlign::Left, TEXT("Bold"));
			}
		}
		if (PostChoices.Num() > GaPostRows)
		{
			P.Text(FString::Printf(TEXT("%d / %d　↑↓・ホイール"), PostChoice + 1, PostChoices.Num()), GaGridLeft + 560.0f, 128.0f, 14.0f, Muted, ETextAlign::Right,
				TEXT("Bold"));
		}
		// The work as it will be seen.
		const FBox2D Shown(FVector2D(700.0, 120.0), FVector2D(1110.0, 667.0));
		GaRound(Back, GaGrow(Shown, 6.0f), 18.0f, FLinearColor(0.02f, 0.035f, 0.09f, 1.0f));
		if (!PostChoices.IsEmpty())
		{
			GaPicture(P, StageBrush, Shown);
		}
		// Its name, who posts it, and the buttons.
		P.Text(TEXT("作品の名前"), 1150.0f, 186.0f, 16.0f, Muted, ETextAlign::Left, TEXT("Bold"));
		{
			const FBox2D R = PostFieldRect();
			if (Focus == 400)
			{
				GaGlow(Back, R, 12.0f, WithAlpha(Gold, Pulse), 3.0f);
			}
			GaRound(Back, R, 12.0f, FLinearColor(0.015f, 0.03f, 0.08f, 1.0f));
			GaText(P, PostTitle.IsEmpty() ? FString(TEXT("（名前をつけてください）")) : PostTitle, static_cast<float>(R.Min.X) + 16.0f, static_cast<float>(R.Min.Y) + 16.0f,
				24.0f, static_cast<float>(R.GetSize().X) - 32.0f, PostTitle.IsEmpty() ? Muted : Paper, ETextAlign::Left, TEXT("Black"));
			P.Text(TEXT("A：名前を変える ・ Y / N"), static_cast<float>(R.Min.X), static_cast<float>(R.Max.Y) + 6.0f, 13.0f, Muted, ETextAlign::Left, TEXT("Bold"));
		}
		const UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(Controller.Get());
		P.Text(TEXT("投稿者"), 1150.0f, 330.0f, 16.0f, Muted, ETextAlign::Left, TEXT("Bold"));
		GaText(P, Sessions ? Sessions->GetPlayerName() : FString(), 1150.0f, 354.0f, 22.0f, 350.0f, Paper, ETextAlign::Left, TEXT("Black"));
		P.Text(TEXT("みんなに公開されます。保存した人の数も見られます"), 1150.0f, 400.0f, 13.0f, Muted, ETextAlign::Left, TEXT("Bold"));
		if (PostChoices.IsValidIndex(PostChoice))
		{
			P.Text(TEXT("キャラ"), 1150.0f, 440.0f, 16.0f, Muted, ETextAlign::Left, TEXT("Bold"));
			GaText(P, CharacterName(PostChoices[PostChoice].Character), 1150.0f, 464.0f, 22.0f, 350.0f, Ice, ETextAlign::Left, TEXT("Black"));
		}
		for (int32 Index = 0; Index < 2; ++Index)
		{
			const FBox2D R = PostActionRect(Index);
			const bool bOn = Focus == 401 + Index;
			const bool bUsable = Index == 1 ? !bPosting : !bPosting && !PostChoices.IsEmpty();
			const FLinearColor Accent = Index == 0 ? GaPink : Muted;
			if (bOn)
			{
				GaGlow(Back, R, 14.0f, WithAlpha(Paper, Pulse), 3.0f);
			}
			GaRound(Back, R, 14.0f, bOn && bUsable ? Accent : GaCard);
			if (Index == 0 && bPosting)
			{
				// How far the paint has gone up.
				const float Done = GetPostProgress();
				GaRound(P, FBox2D(R.Min, FVector2D(R.Min.X + R.GetSize().X * FMath::Max(Done, 0.04f), R.Max.Y)), 14.0f, WithAlpha(GaPink, 0.75f));
				P.Text(FString::Printf(TEXT("投稿中… %d%%"), FMath::RoundToInt(Done * 100.0f)), static_cast<float>(R.GetCenter().X), static_cast<float>(R.Min.Y) + 24.0f,
					24.0f, Paper, ETextAlign::Center, TEXT("Black"));
				continue;
			}
			GaText(P, Index == 0 ? TEXT("この作品を投稿する") : TEXT("やめる"), static_cast<float>(R.GetCenter().X),
				static_cast<float>(R.Min.Y) + (Index == 0 ? 24.0f : 16.0f), Index == 0 ? 26.0f : 20.0f, static_cast<float>(R.GetSize().X) - 24.0f,
				bOn && bUsable ? Ink : bUsable ? Paper : Muted, ETextAlign::Center, TEXT("Black"));
		}
	}

	// Notices, and the keys along the bottom.
	if (Now - NoticeAt < 3.0)
	{
		const float Fade = FMath::Clamp(static_cast<float>(3.0 - (Now - NoticeAt)) / 0.4f, 0.0f, 1.0f);
		const FPainter Note{Design, Elements, Layer + 4, Fade};
		// (Where it covers nothing it needs: over the cards' foot, above a work's buttons, under the post's picture.)
		const FBox2D Band = View == EView::Detail ? FBox2D(FVector2D(720.0, 662.0), FVector2D(1500.0, 714.0))
			: View == EView::Post ? FBox2D(FVector2D(690.0, 690.0), FVector2D(1120.0, 746.0)) : FBox2D(FVector2D(360.0, 772.0), FVector2D(1240.0, 828.0));
		GaRound(Note, Band, 14.0f, FLinearColor(0.004f, 0.006f, 0.016f, 0.94f));
		GaText(Note, NoticeText, static_cast<float>(Band.GetCenter().X), static_cast<float>(Band.Min.Y) + 13.0f, 21.0f, static_cast<float>(Band.GetSize().X) - 30.0f, Gold,
			ETextAlign::Center, TEXT("Black"));
	}
	Back.Box(0.0f, GaBarTop, 1600.0f, 58.0f, FLinearColor(0.004f, 0.006f, 0.016f, 0.95f));
	const FString Guide = View == EView::List
		? TEXT("A / Enter：見る　　LB RB / Q E：新着・人気・自分　　LT RT / [ ]：キャラ　　X / P：投稿する　　Y / R：更新　　B / Esc：もどる")
		: View == EView::Detail
		? TEXT("←→：ボタン　　A / Enter：決定　　LB RB / Q E：となりの作品　　右スティック・ドラッグ：回す　　B / Esc：一覧へ")
		: TEXT("↑↓：カラーをえらぶ　　→：名前・投稿へ　　A / Enter：決定　　Y / N：名前　　START：投稿　　B / Esc：やめる");
	if (!Naming.IsOpen())
	{
		GaText(P, Guide, 800.0f, GaBarTop + 16.0f, 17.0f, 1560.0f, Paper, ETextAlign::Center);
	}
	if (Naming.IsOpen())
	{
		return Naming.Paint(Design, Elements, Layer + 6);
	}
	return Layer + 6;
}
