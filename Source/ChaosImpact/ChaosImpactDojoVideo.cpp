#include "ChaosImpactDojoVideo.h"

#include "MediaPlayer.h"
#include "MediaTexture.h"
#include "Misc/Paths.h"

FString UChaosImpactDojoVideo::ClipPath(const FString& Clip)
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Movies/Dojo"), Clip + TEXT(".mp4")));
}

bool UChaosImpactDojoVideo::HasClip(const FString& Clip)
{
	return !Clip.IsEmpty() && FPaths::FileExists(ClipPath(Clip));
}

void UChaosImpactDojoVideo::Want(const FString& Clip)
{
	const FString Next = HasClip(Clip) ? Clip : FString();
	if (Next != Wanted)
	{
		Wanted = Next;
		WantedAt = FPlatformTime::Seconds();
	}
}

void UChaosImpactDojoVideo::Tick()
{
	if (Wanted == Open)
	{
		return;
	}
	// (A moment's settling: the platform player opens asynchronously, and must not be asked for several at once.)
	if (FPlatformTime::Seconds() - WantedAt < 0.15)
	{
		return;
	}
	if (!Player)
	{
		Player = NewObject<UMediaPlayer>(this);
		Player->PlayOnOpen = true;
		Player->SetLooping(true);
		Texture = NewObject<UMediaTexture>(this);
		Texture->SetMediaPlayer(Player);
		Texture->UpdateResource();
		Brush.SetResourceObject(Texture);
		Brush.DrawAs = ESlateBrushDrawType::Image;
		Brush.ImageSize = FVector2D(1280.0, 720.0);
	}
	Player->Close();
	Open = Wanted;
	if (!Open.IsEmpty())
	{
		Player->SetLooping(true);
		Player->OpenFile(ClipPath(Open));
	}
}

const FSlateBrush* UChaosImpactDojoVideo::GetBrush() const
{
	return Player && !Open.IsEmpty() && Open == Wanted && Player->IsPlaying() && Player->GetTime() > FTimespan::Zero() ? &Brush : nullptr;
}

float UChaosImpactDojoVideo::GetProgress() const
{
	if (!Player || Open.IsEmpty())
	{
		return 0.0f;
	}
	const double Length = Player->GetDuration().GetTotalSeconds();
	return Length > 0.0 ? FMath::Clamp(static_cast<float>(Player->GetTime().GetTotalSeconds() / Length), 0.0f, 1.0f) : 0.0f;
}
