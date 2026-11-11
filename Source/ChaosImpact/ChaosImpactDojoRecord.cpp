#include "ChaosImpactDojo.h"

#include "ChaosImpact.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactBallFormComponent.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactDojoCourse.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactTackleComponent.h"
#include "ChaosImpactTrainingTarget.h"
#include "ChaosImpactNovaParts.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/SpringArmComponent.h"

// 訓練場's slide clips, filmed from the game itself: -CIDojoRecord=all (or a clip's name, or several joined by +) on the
// dojo's world, run with -benchmark -fps=60 so every frame is one sixtieth of a second however long the capture takes.
// Each clip sets a lesson out on the stage for real (its CPUs, balls, targets, rings), a short script plays the player
// and every frame is saved to Saved/DojoClips/<clip>/f_0000.png; Scripts/Dojo/encode_clips.py makes them the mp4s in
// Content/Movies/Dojo. The dojo's own HUD and the button hints are left out of the picture; the game's HUD stays in.

namespace
{
	/** How long before a clip's first frame it is set out (the camera and everything settle). */
	constexpr float RecWarmUp = 0.8f;
	constexpr float RecFps = 60.0f;
}

bool AChaosImpactDojo::IsRecordingRun()
{
	FString Clips;
	return FParse::Value(FCommandLine::Get(), TEXT("CIDojoRecord="), Clips) && !Clips.IsEmpty();
}

FString AChaosImpactDojo::GetClipName(const EChaosImpactDojoArt Art, const EChaosImpactBallType Ball)
{
	if (Art == EChaosImpactDojoArt::Ball)
	{
		return FString(TEXT("Ball_")) + ChaosImpactBallTypes::GetInternalName(Ball);
	}
	static const TCHAR* Names[] = {TEXT("Move"), TEXT("PickUp"), TEXT("Throw"), TEXT("Charge"), TEXT("Aim"), TEXT("Dash"), TEXT("Jump"),
		TEXT("Tackle"), TEXT("Swap"), TEXT("Cancel"), TEXT("Drop"), TEXT("Health"), TEXT("KnockOut"), TEXT("Moving"), TEXT("Dodge"),
		TEXT("DashThrough"), TEXT("Interrupt"), TEXT("BallForm"), TEXT("Map"), TEXT("Ball"), TEXT("Teams"), TEXT("Duel"), TEXT("SuddenDeath"),
		TEXT("Review"), TEXT("Stamina"), TEXT("Curve"), TEXT("DriveCourse")};
	const int32 Index = static_cast<int32>(Art);
	return Index >= 0 && Index < UE_ARRAY_COUNT(Names) ? FString(Names[Index]) : FString();
}

const TArray<AChaosImpactDojo::FRecClip>& AChaosImpactDojo::GetClips()
{
	using EArt = EChaosImpactDojoArt;
	using EBall = EChaosImpactBallType;
	static TArray<FRecClip> Clips;
	if (!Clips.IsEmpty())
	{
		return Clips;
	}
	const auto Add = [](const FString& Name, const TCHAR* Lesson, const float Seconds, const ERecScript Script, const EBall Ball = EBall::Normal)
	{
		FRecClip& Clip = Clips.AddDefaulted_GetRef();
		Clip.Name = Name;
		Clip.Lesson = Lesson;
		Clip.Seconds = Seconds;
		Clip.Script = Script;
		Clip.Ball = Ball;
	};
	Add(GetClipName(EArt::Move, EBall::Normal), TEXT("b_move"), 4.0f, ERecScript::Rings);
	Add(GetClipName(EArt::PickUp, EBall::Normal), TEXT("b_pickup"), 3.5f, ERecScript::PickUp);
	Add(GetClipName(EArt::Throw, EBall::Normal), TEXT("b_throw"), 4.0f, ERecScript::TapTargets);
	Add(GetClipName(EArt::Charge, EBall::Normal), TEXT("a_ko"), 4.0f, ERecScript::ChargeAtCPU);
	Add(GetClipName(EArt::Aim, EBall::Normal), TEXT("b_aim"), 4.5f, ERecScript::AimTargets);
	Add(GetClipName(EArt::Dash, EBall::Normal), TEXT("b_dash"), 3.5f, ERecScript::Dashes);
	Add(GetClipName(EArt::Stamina, EBall::Normal), TEXT("b_dash"), 4.0f, ERecScript::Dashes);
	Add(GetClipName(EArt::DashThrough, EBall::Normal), TEXT("a_dashthrough"), 4.5f, ERecScript::DashThrough);
	Add(GetClipName(EArt::Dodge, EBall::Normal), TEXT("a_dodge"), 4.5f, ERecScript::Sidestep);
	Add(GetClipName(EArt::Jump, EBall::Normal), TEXT("b_jump"), 3.5f, ERecScript::Jumps);
	Add(GetClipName(EArt::Tackle, EBall::Normal), TEXT("b_tackle"), 3.0f, ERecScript::TackleCPU);
	Add(GetClipName(EArt::Interrupt, EBall::Normal), TEXT("a_interrupt"), 3.5f, ERecScript::TackleCPU);
	Add(GetClipName(EArt::Swap, EBall::Normal), TEXT("b_swap"), 3.5f, ERecScript::Swaps);
	Add(GetClipName(EArt::Cancel, EBall::Normal), TEXT("b_cancel"), 3.5f, ERecScript::Cancels);
	Add(GetClipName(EArt::Drop, EBall::Normal), TEXT("b_drop"), 3.0f, ERecScript::Drops);
	Add(GetClipName(EArt::Health, EBall::Normal), TEXT("a_dodge"), 4.5f, ERecScript::StandHit);
	Add(GetClipName(EArt::KnockOut, EBall::Normal), TEXT("a_ko"), 5.0f, ERecScript::KnockOutCPU);
	Add(GetClipName(EArt::Moving, EBall::Normal), TEXT("a_lead"), 4.5f, ERecScript::LeadThrows);
	Add(GetClipName(EArt::Curve, EBall::Normal), TEXT("b_review"), 4.0f, ERecScript::LeadThrows);
	Add(GetClipName(EArt::Review, EBall::Normal), TEXT("b_review"), 4.5f, ERecScript::LeadThrows);
	Add(GetClipName(EArt::BallForm, EBall::Normal), TEXT("a_ballform"), 4.0f, ERecScript::BallForm);
	Add(GetClipName(EArt::Map, EBall::Normal), TEXT("a_map"), 4.0f, ERecScript::Map);
	Add(GetClipName(EArt::Teams, EBall::Normal), TEXT("r_team"), 4.5f, ERecScript::LeadThrows);
	Add(GetClipName(EArt::Duel, EBall::Normal), TEXT("r_duel"), 6.0f, ERecScript::PadFight);
	Add(GetClipName(EArt::DriveCourse, EBall::Normal), TEXT("c_drivecourse"), 5.0f, ERecScript::DriveCourse);
	Add(GetClipName(EArt::SuddenDeath, EBall::Normal), TEXT("r_sudden"), 6.0f, ERecScript::Wander);
	const TPair<EBall, const TCHAR*> Balls[] = {{EBall::Fire, TEXT("x_fire")}, {EBall::Ice, TEXT("x_ice")}, {EBall::Thunder, TEXT("x_thunder")},
		{EBall::Black, TEXT("x_black")}, {EBall::Wind, TEXT("x_wind")}, {EBall::Smoke, TEXT("x_smoke")}, {EBall::Beam, TEXT("x_beam")},
		{EBall::Snow, TEXT("x_snow")}, {EBall::Nova, TEXT("x_nova")}, {EBall::Simae, TEXT("x_simae")}, {EBall::Drive, TEXT("x_drive")},
		{EBall::Prison, TEXT("x_prison")}, {EBall::Hook, TEXT("x_hook")}, {EBall::Anchor, TEXT("x_anchor")}};
	for (const TPair<EBall, const TCHAR*>& Entry : Balls)
	{
		const EBall Ball = Entry.Key;
		const ERecScript Script = Ball == EBall::Snow ? ERecScript::Snow : Ball == EBall::Nova ? ERecScript::Nova
			: Ball == EBall::Drive ? ERecScript::Drive : Ball == EBall::Anchor ? ERecScript::Anchor : ERecScript::BallAtCPU;
		Add(GetClipName(EArt::Ball, Ball), Entry.Value, Ball == EBall::Nova ? 8.0f : Ball == EBall::Snow ? 5.5f : 4.5f, Script, Ball);
	}
	return Clips;
}

void AChaosImpactDojo::BeginRecording()
{
	FString Wanted;
	FParse::Value(FCommandLine::Get(), TEXT("CIDojoRecord="), Wanted);
	TArray<FString> Names;
	Wanted.ParseIntoArray(Names, TEXT("+"), true);
	RecQueue.Reset();
	for (int32 Index = 0; Index < GetClips().Num(); ++Index)
	{
		if (!Wanted.Equals(TEXT("all"), ESearchCase::IgnoreCase) && !Names.Contains(GetClips()[Index].Name))
		{
			continue;
		}
		// Each clip is filmed where its lesson is held: this level's ones only (the other level is filmed on its own run).
		bool bHere = true;
		for (const FChaosImpactDojoCategory& Group : ChaosImpactDojo::GetCategories())
		{
			for (const FChaosImpactDojoLesson& Lesson : Group.Lessons)
			{
				if (Lesson.Id == GetClips()[Index].Lesson)
				{
					// (By its level only: a mock match's slides may be read anywhere, but its clip is filmed on its stage.)
					bHere = GetWorld()->GetMapName().EndsWith(FPackageName::GetShortName(ChaosImpactDojo::GetLessonLevel(Lesson)));
				}
			}
		}
		if (bHere)
		{
			RecQueue.Add(Index);
		}
	}
	bRecording = true;
	RecAt = 0;
	UE_LOG(LogChaosImpact, Log, TEXT("Dojo record: %d clips"), RecQueue.Num());
	if (AChaosImpactCharacter* Player = GetPlayerCharacter())
	{
		Player->SetDojoScripted(true);
	}
	StartRecClip();
}

void AChaosImpactDojo::StartRecClip()
{
	if (!RecQueue.IsValidIndex(RecAt))
	{
		UE_LOG(LogChaosImpact, Log, TEXT("Dojo record: done"));
		FPlatformMisc::RequestExit(false);
		return;
	}
	const FRecClip& Clip = GetClips()[RecQueue[RecAt]];
	const TArray<FChaosImpactDojoCategory>& All = ChaosImpactDojo::GetCategories();
	for (int32 Group = 0; Group < All.Num(); ++Group)
	{
		for (int32 Index = 0; Index < All[Group].Lessons.Num(); ++Index)
		{
			if (All[Group].Lessons[Index].Id == Clip.Lesson)
			{
				StartLesson(Group, Index);
				Slide = All[Group].Lessons[Index].Slides.Num();
				BeginPlayPhase();
			}
		}
	}
	RecTime = -RecWarmUp;
	RecFrame = 0;
	RecFired.Reset();
	RecHolding = false;
	RecHoldUntil = -1.0f;
	RecSide = false;
	bRecFocusSet = false;
	bRecAimSet = false;
	if (AChaosImpactPlayerController* PC = GetPlayer())
	{
		PC->SetOverviewMapOpen(false);
	}
	UE_LOG(LogChaosImpact, Log, TEXT("Dojo record: clip %s (%.1fs)"), *Clip.Name, Clip.Seconds);
}

void AChaosImpactDojo::TickRecord(const float DeltaSeconds)
{
	if (!RecQueue.IsValidIndex(RecAt))
	{
		return;
	}
	const FRecClip& Clip = GetClips()[RecQueue[RecAt]];
	if (Phase == EChaosImpactDojoPhase::Play)
	{
		TickPlay(DeltaSeconds);
	}
	RunRecScript(Clip, RecTime);
	UpdateRecCamera(1.0f / RecFps);
	UpdateRecCursor();
	if (RecTime >= 0.0f)
	{
		if (RecFrame < FMath::RoundToInt(Clip.Seconds * RecFps))
		{
			const FString File = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DojoClips"), Clip.Name, FString::Printf(TEXT("f_%04d.png"), RecFrame));
			FScreenshotRequest::RequestScreenshot(File, true, false);
			++RecFrame;
		}
		else
		{
			++RecAt;
			StartRecClip();
			return;
		}
	}
	RecTime += 1.0f / RecFps;
}

bool AChaosImpactDojo::RecOnce(const FName Key, const bool bWhen)
{
	if (!bWhen || RecFired.Contains(Key))
	{
		return false;
	}
	RecFired.Add(Key);
	return true;
}

void AChaosImpactDojo::RunRecScript(const FRecClip& Clip, const float T)
{
	AChaosImpactCharacter* Me = GetPlayerCharacter();
	AChaosImpactPlayerController* PC = GetPlayer();
	if (!Me || !PC)
	{
		return;
	}
	AChaosImpactCharacter* Foe = CPUs.IsEmpty() ? nullptr : CPUs[0].Pawn.Get();
	const auto Walk = [Me](const FVector& To, const float Scale = 1.0f)
	{
		const FVector Way = (To - Me->GetActorLocation()).GetSafeNormal2D();
		if (FVector::Dist2D(To, Me->GetActorLocation()) > 45.0f)
		{
			Me->AddMovementInput(Way, Scale);
		}
	};
	const auto Aim = [this, Me](const FVector& At)
	{
		Me->SetAIAimDirection(At - Me->GetActorLocation());
		RecAimAt = At;
		bRecAimSet = true;
	};
	// Where a walking CPU will be when a ball thrown now reaches it.
	const auto Lead = [Me](const AChaosImpactCharacter* Target)
	{
		if (!Target)
		{
			return Me->GetActorLocation() + Me->GetActorForwardVector() * 600.0f;
		}
		const float Time = static_cast<float>(FVector::Dist2D(Target->GetActorLocation(), Me->GetActorLocation())) / 2400.0f;
		return Target->GetActorLocation() + Target->GetVelocity() * Time;
	};
	// A throw: hold from Begin for Hold seconds, aimed at At at release.
	const auto Throw = [this, Me, T](const FName Key, const float Begin, const float Hold, const FVector& At)
	{
		// (The cursor goes to where it is aimed a little before the throw, as a player's hand would.)
		if (T >= Begin - 0.35f && T < Begin + Hold + 0.2f)
		{
			RecAimAt = At;
			bRecAimSet = true;
		}
		if (T >= Begin && T < Begin + Hold)
		{
			Me->SetAIAimDirection(At - Me->GetActorLocation());
			if (RecOnce(*(Key.ToString() + TEXT("_in")), true) && Me->GetCarriedBallCount() > 0)
			{
				Me->BeginThrowInput();
			}
		}
		else if (T >= Begin + Hold && RecOnce(*(Key.ToString() + TEXT("_out")), true))
		{
			Me->SetAIAimDirection(At - Me->GetActorLocation());
			Me->EndThrowInput();
		}
	};
	switch (Clip.Script)
	{
	case ERecScript::Rings:
		if (T > -0.2f)
		{
			Walk(DebugNextRing());
		}
		break;
	case ERecScript::PickUp:
	{
		AChaosImpactBall* Nearest = nullptr;
		for (TActorIterator<AChaosImpactBall> It(GetWorld()); It; ++It)
		{
			if (It->IsPickup() && (!Nearest || FVector::Dist2D(It->GetActorLocation(), Me->GetActorLocation())
				< FVector::Dist2D(Nearest->GetActorLocation(), Me->GetActorLocation())))
			{
				Nearest = *It;
			}
		}
		if (Nearest && T > 0.1f)
		{
			Walk(Nearest->GetActorLocation());
		}
		break;
	}
	case ERecScript::TapTargets:
	case ERecScript::AimTargets:
	{
		// One target after another, each with a quick throw (or a turn of the aim and a fuller one).
		const bool bAim = Clip.Script == ERecScript::AimTargets;
		const float Every = bAim ? 1.1f : 1.25f;
		const int32 Turn = FMath::Max(0, FMath::FloorToInt((T - 0.2f) / Every));
		AChaosImpactTrainingTarget* Target = nullptr;
		int32 Seen = 0;
		for (const TWeakObjectPtr<AChaosImpactTrainingTarget>& Each : Targets)
		{
			if (Each.IsValid() && !Each->IsDefeated() && Seen++ == 0)
			{
				Target = Each.Get();
			}
		}
		if (Target)
		{
			const float Begin = 0.2f + Turn * Every;
			Throw(*FString::Printf(TEXT("t%d"), Turn), Begin, bAim ? 0.7f : 0.55f, Target->GetActorLocation());
		}
		break;
	}
	case ERecScript::ChargeAtCPU:
		if (Foe)
		{
			Throw(TEXT("c1"), 0.2f, 1.5f, Foe->GetActorLocation());
			if (T > 2.4f && Me->GetCarriedBallCount() > 0)
			{
				Throw(TEXT("c2"), 2.4f, 1.3f, Foe->GetActorLocation());
			}
		}
		break;
	case ERecScript::KnockOutCPU:
		if (Foe)
		{
			for (int32 Shot = 0; Shot < 3; ++Shot)
			{
				Throw(*FString::Printf(TEXT("k%d"), Shot), 0.2f + Shot * 1.25f, 0.55f, Foe->GetActorLocation());
			}
		}
		break;
	case ERecScript::LeadThrows:
		if (Foe || !CPUs.IsEmpty())
		{
			const int32 Turn = FMath::Max(0, FMath::FloorToInt((T - 0.2f) / 1.4f));
			AChaosImpactCharacter* Target = CPUs.IsValidIndex(Turn % FMath::Max(CPUs.Num(), 1)) ? CPUs[Turn % CPUs.Num()].Pawn.Get() : Foe;
			Throw(*FString::Printf(TEXT("l%d"), Turn), 0.2f + Turn * 1.4f, 0.6f, Lead(Target));
		}
		break;
	case ERecScript::Dashes:
	{
		const FVector Ways[] = {Right, -Right, Up, -Up};
		for (int32 Index = 0; Index < 4; ++Index)
		{
			if (RecOnce(*FString::Printf(TEXT("d%d"), Index), T >= 0.3f + Index * 0.85f))
			{
				Me->RequestAIDash(Ways[Index]);
			}
		}
		break;
	}
	case ERecScript::DashThrough:
	case ERecScript::Sidestep:
	{
		// The CPU's ball coming: dash through it at the last moment, or step out of its line in good time.
		const bool bDash = Clip.Script == ERecScript::DashThrough;
		for (TActorIterator<AChaosImpactBall> It(GetWorld()); It; ++It)
		{
			if (It->IsPickup() || It->WasThrownBy(Me))
			{
				continue;
			}
			const float Near = static_cast<float>(FVector::Dist2D(It->GetActorLocation(), Me->GetActorLocation()));
			if (bDash && Near < 300.0f && Near > 90.0f && RecOnce(*FString::Printf(TEXT("b%u"), It->GetUniqueID()), true))
			{
				// Straight through it, towards the thrower: the ball goes through while the dash lasts.
				Me->RequestAIDash((It->GetActorLocation() - Me->GetActorLocation()).GetSafeNormal2D());
			}
			if (!bDash && Near < 900.0f && RecOnce(*FString::Printf(TEXT("s%u"), It->GetUniqueID()), true))
			{
				RecSide = !RecSide;
				RecHoldUntil = T + 0.55f;
			}
		}
		if (!bDash && T < RecHoldUntil)
		{
			Walk(Me->GetActorLocation() + (RecSide ? Right : -Right) * 400.0f);
		}
		break;
	}
	case ERecScript::StandHit:
		if (Foe)
		{
			Aim(Foe->GetActorLocation());
		}
		break;
	case ERecScript::Jumps:
		if (T > 0.0f)
		{
			Walk(Start + Up * 900.0f, 0.5f);
		}
		for (int32 Index = 0; Index < 3; ++Index)
		{
			if (RecOnce(*FString::Printf(TEXT("j%d"), Index), T >= 0.3f + Index * 1.05f))
			{
				Me->Jump();
			}
		}
		if (T > 0.1f)
		{
			Me->StopJumping();
		}
		break;
	case ERecScript::TackleCPU:
		if (Foe)
		{
			const float Near = static_cast<float>(FVector::Dist2D(Foe->GetActorLocation(), Me->GetActorLocation()));
			if (Near > 230.0f && T > 0.0f)
			{
				Walk(Foe->GetActorLocation());
			}
			// (A tackle goes the way the player aims.)
			Aim(Foe->GetActorLocation());
			if (Near <= 300.0f && T > RecHoldUntil && Me->GetTackle())
			{
				const bool bWent = Me->GetTackle()->Request();
				UE_LOG(LogChaosImpact, Log, TEXT("Dojo record: tackle at %.0f -> %d"), Near, bWent);
				RecHoldUntil = T + (bWent ? 1.4f : 0.2f);
			}
		}
		break;
	case ERecScript::Swaps:
		for (int32 Index = 0; Index < 3; ++Index)
		{
			if (RecOnce(*FString::Printf(TEXT("w%d"), Index), T >= 0.6f + Index * 1.0f))
			{
				Me->RequestBallSwap();
			}
		}
		break;
	case ERecScript::Cancels:
		for (int32 Index = 0; Index < 2; ++Index)
		{
			const float Begin = 0.2f + Index * 1.6f;
			if (RecOnce(*FString::Printf(TEXT("ci%d"), Index), T >= Begin) && Me->GetCarriedBallCount() > 0)
			{
				Me->SetAIAimDirection(Up);
				Me->BeginThrowInput();
			}
			if (RecOnce(*FString::Printf(TEXT("co%d"), Index), T >= Begin + 0.9f))
			{
				Me->RequestCancelThrow();
			}
		}
		break;
	case ERecScript::Drops:
		if (RecOnce(TEXT("drop"), T >= 0.5f))
		{
			Me->RequestDropBall();
		}
		if (T > 1.0f && T < 1.9f)
		{
			Walk(Me->GetActorLocation() - Up * 300.0f);
		}
		break;
	case ERecScript::BallForm:
		if (RecOnce(TEXT("in"), T >= 0.4f) && Me->GetBallForm())
		{
			Me->GetBallForm()->Toggle();
		}
		if (RecOnce(TEXT("out"), T >= 2.8f) && Me->GetBallForm() && Me->IsInBallForm())
		{
			Me->GetBallForm()->Toggle();
		}
		break;
	case ERecScript::Map:
		PC->SetOverviewMapOpen(T >= 0.5f && T < 3.5f);
		break;
	case ERecScript::Wander:
	{
		const float A = T * 0.9f;
		Walk(Center + (Up * FMath::Cos(A) + Right * FMath::Sin(A)) * 380.0f, 0.7f);
		break;
	}
	case ERecScript::Snow:
		// Rolled about (it grows), then thrown down on the CPU.
		if (T < 2.6f)
		{
			const float A = T * 2.2f;
			Walk(Start + (Up * FMath::Cos(A) + Right * FMath::Sin(A)) * 300.0f);
		}
		if (Foe)
		{
			Throw(TEXT("snow"), 2.8f, 0.6f, Foe->GetActorLocation());
		}
		break;
	case ERecScript::Nova:
	{
		// The three parts gathered first (each touched in turn); then the nova charged long and dropped on the CPU.
		const bool bHolding = Me->GetCarriedBallCount() > 0 && Me->GetCarriedBallType(0) == EChaosImpactBallType::Nova;
		AActor* Part = nullptr;
		for (TActorIterator<AChaosImpactNovaPart> It(GetWorld()); It; ++It)
		{
			if (!Part || FVector::Dist2D(It->GetActorLocation(), Me->GetActorLocation()) < FVector::Dist2D(Part->GetActorLocation(), Me->GetActorLocation()))
			{
				Part = *It;
			}
		}
		if (!bHolding && Part && !RecHolding)
		{
			Walk(Part->GetActorLocation());
			RecAimAt = Part->GetActorLocation();
			bRecAimSet = true;
		}
		else if (Foe && (bHolding || RecHolding))
		{
			if (!RecHolding)
			{
				RecHolding = true;
				RecHoldUntil = T;
			}
			Throw(TEXT("nova"), RecHoldUntil + 0.2f, 2.4f, Foe->GetActorLocation());
			if (T > RecHoldUntil + 0.4f && T < RecHoldUntil + 2.6f && Me->IsChargingNova())
			{
				// Its landing spot steered onto the CPU, as a player does with the movement controls (it glides there).
				const float Glide = FMath::Clamp((T - RecHoldUntil - 0.4f) / 1.4f, 0.0f, 1.0f);
				const FVector From = Me->GetActorLocation() + Me->GetAimDirection().GetSafeNormal2D() * 900.0f;
				Me->SetNovaTargetPoint(FMath::Lerp(From, Foe->GetActorLocation(), Glide * Glide * (3.0f - 2.0f * Glide)));
				RecAimAt = Foe->GetActorLocation();
			}
		}
		break;
	}
	case ERecScript::Drive:
		if (Foe)
		{
			Throw(TEXT("drive"), 0.2f, 0.4f, Me->GetActorLocation() + (Up + Right * 0.8f) * 600.0f);
			if (T > 0.8f)
			{
				for (TActorIterator<AChaosImpactBall> It(GetWorld()); It; ++It)
				{
					if (It->WasThrownBy(Me) && !It->IsPickup())
					{
						Me->SetAIDriveSteer((Foe->GetActorLocation() - It->GetActorLocation()).GetSafeNormal2D());
					}
				}
			}
		}
		break;
	case ERecScript::Anchor:
	{
		// To the next glowing ring, by anchor.
		const FVector Ring = DebugNextRing();
		const int32 Turn = FMath::Max(0, FMath::FloorToInt((T - 0.2f) / 1.5f));
		Throw(*FString::Printf(TEXT("a%d"), Turn), 0.2f + Turn * 1.5f, 0.35f, Ring);
		break;
	}
	case ERecScript::DriveCourse:
		if (Foe && CoursePath.Num() >= 2)
		{
			// Thrown uncharged (slow, turns tightly) up the road's mouth, then steered a little ahead along its middle.
			Throw(TEXT("course"), 0.3f, 0.12f, CoursePath[1]);
			if (T > 0.6f)
			{
				for (TActorIterator<AChaosImpactBall> It(GetWorld()); It; ++It)
				{
					if (It->WasThrownBy(Me) && !It->IsPickup() && It->GetBallType() == EChaosImpactBallType::Drive)
					{
						float Along = 0.0f;
						AChaosImpactDojoCourse::DistanceToPath(CoursePath, It->GetActorLocation(), Along);
						const float Total = AChaosImpactDojoCourse::PathLength(CoursePath);
						const FVector Ahead = Along > Total - 320.0f ? Foe->GetActorLocation() : AChaosImpactDojoCourse::PointAlong(CoursePath, Along + 240.0f);
						Me->SetAIDriveSteer((Ahead - It->GetActorLocation()).GetSafeNormal2D());
						RecAimAt = It->GetActorLocation();
						bRecAimSet = true;
					}
				}
			}
		}
		break;
	case ERecScript::PadFight:
	{
		// A round as it is played: to the nearest pad's ball, then a charged throw at the CPU, again.
		if (Me->GetCarriedBallCount() == 0 && !Me->IsChargingThrow() && !Me->IsThrowReleasePending())
		{
			AChaosImpactBall* Nearest = nullptr;
			for (TActorIterator<AChaosImpactBall> It(GetWorld()); It; ++It)
			{
				if (It->IsPickup() && (!Nearest || FVector::Dist2D(It->GetActorLocation(), Me->GetActorLocation())
					< FVector::Dist2D(Nearest->GetActorLocation(), Me->GetActorLocation())))
				{
					Nearest = *It;
				}
			}
			if (Nearest)
			{
				Walk(Nearest->GetActorLocation());
				RecAimAt = Nearest->GetActorLocation();
				bRecAimSet = true;
			}
			RecHolding = false;
		}
		else if (Foe && !RecHolding && Me->GetCarriedBallCount() > 0)
		{
			// Out from behind cover first (the duel arena's blocks): a throw only with a clear line to the CPU.
			FHitResult Block;
			FCollisionQueryParams Query(SCENE_QUERY_STAT(DojoRecSight), false);
			Query.AddIgnoredActor(Me);
			Query.AddIgnoredActor(Foe);
			const FVector Eye = Me->GetActorLocation() + FVector(0.0f, 0.0f, 40.0f);
			const FVector To = Foe->GetActorLocation() + FVector(0.0f, 0.0f, 40.0f);
			if (GetWorld()->LineTraceSingleByChannel(Block, Eye, To, ECC_Visibility, Query))
			{
				const FVector Across = FVector::CrossProduct((To - Eye).GetSafeNormal2D(), FVector::UpVector);
				Walk(Me->GetActorLocation() + (RecSide ? Across : -Across) * 300.0f);
				Aim(Foe->GetActorLocation());
			}
			else
			{
				RecHolding = true;
				RecHoldUntil = T;
			}
		}
		if (Foe && RecHolding)
		{
			Throw(*FString::Printf(TEXT("p%d"), FMath::RoundToInt(RecHoldUntil * 10.0f)), RecHoldUntil + 0.1f, 0.7f, Lead(Foe));
		}
		break;
	}
	case ERecScript::BallAtCPU:
	default:
		if (!CPUs.IsEmpty())
		{
			// The beam: at the nearer of the two in a line (it goes through both).
			AChaosImpactCharacter* Target = Foe;
			Throw(TEXT("b1"), 0.25f, 0.7f, Clip.Ball == EChaosImpactBallType::Beam ? Target->GetActorLocation() : Lead(Target));
			if (Clip.Ball != EChaosImpactBallType::Beam && Clip.Ball != EChaosImpactBallType::Prison)
			{
				Throw(TEXT("b2"), 2.4f, 0.6f, Lead(Target));
			}
		}
		break;
	}
}

void AChaosImpactDojo::UpdateRecCamera(const float DeltaSeconds)
{
	AChaosImpactCharacter* Me = GetPlayerCharacter();
	AChaosImpactPlayerController* PC = GetPlayer();
	if (!Me || !PC || !Me->GetCameraBoom())
	{
		return;
	}
	if (!RecCamera)
	{
		FActorSpawnParameters Parameters;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		RecCamera = GetWorld()->SpawnActor<ACameraActor>(Parameters);
		if (RecCamera)
		{
			RecCamera->GetCameraComponent()->bConstrainAspectRatio = false;
		}
	}
	if (!RecCamera)
	{
		return;
	}
	// What the clip is about: the first CPU, else a target still standing, else the next ring; the player's own ball in flight
	// pulls the eye a little too.
	FVector Subject = Me->GetActorLocation();
	bool bSubject = false;
	for (const FDojoCPU& CPU : CPUs)
	{
		if (CPU.Pawn.IsValid())
		{
			Subject = CPU.Pawn->GetActorLocation();
			bSubject = true;
			break;
		}
	}
	if (!bSubject)
	{
		for (const TWeakObjectPtr<AChaosImpactTrainingTarget>& Target : Targets)
		{
			if (Target.IsValid() && !Target->IsDefeated())
			{
				Subject = Target->GetActorLocation();
				bSubject = true;
				break;
			}
		}
	}
	if (!bSubject && Rings.IsValidIndex(RingAt))
	{
		Subject = Rings[RingAt];
		bSubject = true;
	}
	const float Apart = bSubject ? static_cast<float>(FVector::Dist2D(Subject, Me->GetActorLocation())) : 0.0f;
	// (The drive course: the whole road in the picture, the player at its foot and the CPU at its end.)
	const bool bCourse = CoursePath.Num() >= 2;
	// (Several CPUs: the middle of them all.)
	if (CPUs.Num() >= 2)
	{
		FVector Sum = FVector::ZeroVector;
		int32 Num = 0;
		for (const FDojoCPU& Each : CPUs)
		{
			if (Each.Pawn.IsValid())
			{
				Sum += Each.Pawn->GetActorLocation();
				++Num;
			}
		}
		if (Num > 0)
		{
			Subject = Sum / Num;
			bSubject = true;
		}
	}
	const FVector Want = bCourse ? FMath::Lerp(CoursePath[0], CoursePath.Last(), 0.5f)
		: bSubject ? FMath::Lerp(Me->GetActorLocation(), Subject, 0.42f) : Me->GetActorLocation();
	RecFocus = bRecFocusSet ? FMath::VInterpTo(RecFocus, Want, DeltaSeconds, 3.0f) : Want;
	bRecFocusSet = true;
	// The game's own angle and field of view, a little closer in (further back when the two are far apart).
	const FRotator Angle = Me->GetCameraBoom()->GetComponentRotation();
	const float Arm = Me->GetCameraBoom()->TargetArmLength * (bCourse ? 1.55f : FMath::Clamp(0.78f + Apart / 4200.0f, 0.78f, 1.05f));
	const UCameraComponent* Own = Me->FindComponentByClass<UCameraComponent>();
	RecCamera->GetCameraComponent()->SetFieldOfView(Own ? Own->FieldOfView : 60.0f);
	RecCamera->SetActorLocationAndRotation(RecFocus - Angle.Vector() * Arm, Angle);
	if (PC->GetViewTarget() != RecCamera)
	{
		PC->SetViewTarget(RecCamera);
	}
}

void AChaosImpactDojo::UpdateRecCursor()
{
	AChaosImpactCharacter* Me = GetPlayerCharacter();
	AChaosImpactPlayerController* PC = GetPlayer();
	if (!Me || !PC)
	{
		return;
	}
	// The cursor where the script aims (a little in front of the player when it aims nowhere), as on a player's screen.
	const FVector Point = bRecAimSet ? RecAimAt : Me->GetActorLocation() + Me->GetAimDirection().GetSafeNormal2D() * 420.0f;
	FVector2D Screen;
	if (PC->ProjectWorldLocationToScreen(Point, Screen, false))
	{
		PC->SetMouseLocation(FMath::RoundToInt(Screen.X), FMath::RoundToInt(Screen.Y));
	}
}
