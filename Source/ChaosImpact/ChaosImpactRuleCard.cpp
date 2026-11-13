#include "ChaosImpactRuleCard.h"
#include "ChaosImpactMatchTypes.h"
#include "ChaosImpactPaint.h"
#include "ChaosImpactParty.h"

/** The rules page's emblem for a kind of match (everyone for themselves, teams, 1 v 1, battle royale, tournament). */
void ChaosImpactRuleCard::PaintEmblem(const ChaosImpactPaint::FPainter& Icon, const FChaosImpactMatchRules& Rules, const FVector2D& At, const float Unit,
	const FLinearColor& Mark, const float Time)
{
	namespace CP = ChaosImpactPaint;
	using namespace ChaosImpactPaint;
	// (The party rules' pictures are solid: faded as far as Mark is, like the others' strokes.)
	const CP::FPainter Faded{Icon.Geometry, Icon.Elements, Icon.Layer, Icon.Alpha * Mark.A};
	if (Rules.IsRubyRush())
	{
		// Rubies: one large, two small beside it, catching the light in turn.
		ChaosImpactParty::PaintRuby(Faded, At + FVector2D(-Unit * 0.62f, Unit * 0.34f), Unit * 0.42f, Time + 1.3f);
		ChaosImpactParty::PaintRuby(Faded, At + FVector2D(Unit * 0.66f, Unit * 0.4f), Unit * 0.36f, Time + 2.6f);
		ChaosImpactParty::PaintRuby(Faded, At + FVector2D(0.0f, -Unit * 0.08f + FMath::Sin(Time * 2.4f) * Unit * 0.05f), Unit * 0.72f, Time);
		return;
	}
	if (Rules.IsCrownKeep())
	{
		ChaosImpactParty::PaintCrown(Faded, At + FVector2D(0.0f, FMath::Sin(Time * 2.2f) * Unit * 0.05f), Unit * 0.95f, Time);
		return;
	}
	if (Rules.bTournament)
	{
		const float L = At.X - Unit * 0.9f;
		const float M = At.X - Unit * 0.2f;
		const float R = At.X + Unit * 0.5f;
		for (int32 Leaf = 0; Leaf < 4; ++Leaf)
		{
			const float Y = At.Y - Unit * 0.75f + Leaf * Unit * 0.5f;
			Icon.Line(FVector2D(L, Y), FVector2D(M, Y), Mark, 5.0f);
		}
		for (int32 Pair = 0; Pair < 2; ++Pair)
		{
			const float Y0 = At.Y - Unit * 0.75f + Pair * Unit;
			Icon.Line(FVector2D(M, Y0), FVector2D(M, Y0 + Unit * 0.5f), Mark, 5.0f);
			Icon.Line(FVector2D(M, Y0 + Unit * 0.25f), FVector2D(R, Y0 + Unit * 0.25f), Mark, 5.0f);
		}
		Icon.Line(FVector2D(R, At.Y - Unit * 0.5f), FVector2D(R, At.Y + Unit * 0.5f), Mark, 5.0f);
		Icon.Line(FVector2D(R, At.Y), FVector2D(R + Unit * 0.45f, At.Y), Gold, 6.0f);
		Icon.Disc(FVector2D(R + Unit * 0.6f, At.Y), Unit * 0.17f, Gold);
	}
	else if (Rules.bDuel)
	{
		const float Bob = FMath::Sin(Time * 4.0f) * 4.0f;
		Icon.Disc(At + FVector2D(-Unit * 0.62f, Bob), Unit * 0.34f, ChaosImpactMatch::GetTeamColor(1));
		Icon.Disc(At + FVector2D(Unit * 0.62f, -Bob), Unit * 0.34f, ChaosImpactMatch::GetTeamColor(0));
		Icon.Line(At + FVector2D(-Unit * 0.14f, -Unit * 0.42f), At + FVector2D(Unit * 0.1f, -Unit * 0.02f), Mark, 6.0f);
		Icon.Line(At + FVector2D(Unit * 0.1f, -Unit * 0.02f), At + FVector2D(-Unit * 0.1f, Unit * 0.06f), Mark, 6.0f);
		Icon.Line(At + FVector2D(-Unit * 0.1f, Unit * 0.06f), At + FVector2D(Unit * 0.14f, Unit * 0.46f), Mark, 6.0f);
	}
	else if (Rules.bBattleRoyale)
	{
		// A ring closing on the last ones standing.
		Icon.Ring(At, Unit * (0.85f + 0.08f * FMath::Sin(Time * 2.0f)), Mark, 4.0f, 56);
		for (int32 Seat = 0; Seat < 5; ++Seat)
		{
			const float Angle = Time * 0.6f + Seat * UE_TWO_PI / 5.0f;
			Icon.Disc(At + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Unit * 0.45f, Unit * 0.11f, Seat == 0 ? Gold : WithAlpha(Mark, 0.5f));
		}
	}
	else if (Rules.IsTeamBattle())
	{
		// The teams' colours, a few each, facing.
		const int32 Teams = FMath::Clamp(Rules.TeamCount, 2, 4);
		for (int32 Team = 0; Team < Teams; ++Team)
		{
			const float Angle = Team * UE_TWO_PI / Teams + Time * 0.3f;
			const FVector2D Home = At + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Unit * 0.55f;
			for (int32 Member = 0; Member < 3; ++Member)
			{
				const float Around = Angle + (Member - 1) * 0.5f;
				Icon.Disc(Home + FVector2D(FMath::Cos(Around), FMath::Sin(Around)) * Unit * 0.24f, Unit * 0.14f, ChaosImpactMatch::GetTeamColor(Team));
			}
		}
	}
	else
	{
		// Everyone for themselves: players round a ball.
		Icon.Disc(At, Unit * 0.26f, Mark);
		for (int32 Seat = 0; Seat < 8; ++Seat)
		{
			const float Angle = Time * 0.5f + Seat * UE_TWO_PI / 8.0f;
			Icon.Disc(At + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Unit * 0.75f, Unit * 0.11f, Mark);
		}
	}
}

TArray<FString> ChaosImpactRuleCard::GetPoints(const FChaosImpactMatchRules& Rules, const int32 Total)
{
	TArray<FString> Points;
	if (Rules.IsRubyRush())
	{
		Points.Add(TEXT("落ちているルビーを拾い集める・多い人の勝ち"));
		Points.Add(FString::Printf(TEXT("当てられると%d%%落とす・倒されると半分落とす"), FMath::RoundToInt(ChaosImpactMatch::RubyHitDropShare * 100.0f)));
		Points.Add(FString::Printf(TEXT("%d分間の勝負・落ちたルビーは誰でも拾える"), Rules.Minutes));
	}
	else if (Rules.IsCrownKeep())
	{
		Points.Add(FString::Printf(TEXT("王冠を持ったまま%dカウントで勝ち"), ChaosImpactMatch::CrownGoalCounts));
		Points.Add(TEXT("持っている間は足が少し遅い・当てられると落とす"));
		Points.Add(FString::Printf(TEXT("%d分で時間切れ・残りカウントが少ない人の勝ち"), Rules.Minutes));
	}
	else if (Rules.bCrashBall)
	{
		Points.Add(TEXT("巨大球にボールを当てて、相手のゴールへ押しこむ"));
		Points.Add(TEXT("ゴールに入れたら勝ち・時間切れは押しこんだ深さで判定"));
		Points.Add(FString::Printf(TEXT("%d分・2チーム・倒されると少し長く復活待ち"), Rules.Minutes));
	}
	else if (Rules.bTournament)
	{
		Points.Add(Rules.Tournament.Mode == EChaosImpactTournamentMode::Simultaneous ? TEXT("同じ回戦の試合を全部いっしょに・CPU同士は裏で決着")
			: TEXT("1試合ずつ順番に・CPU同士は観るかとばすか選べる"));
		Points.Add(FString::Printf(TEXT("全試合1対1・%d本先取の勝ちぬき戦"), ChaosImpactMatch::DuelRoundsToWin));
		Points.Add(TEXT("待ち時間は練習／観戦を自由に"));
	}
	else if (Rules.bDuel)
	{
		Points.Add(Rules.CPUCount > 0 ? FString::Printf(TEXT("あなた 対 CPU（%s）"), ChaosImpactMatch::GetCPULevelName(Rules.CPULevel))
			: FString(TEXT("プレイヤー同士の1対1")));
		Points.Add(FString::Printf(TEXT("%d本先取・体力3・特殊ボールはスロットで%d種類"), ChaosImpactMatch::DuelRoundsToWin, ChaosImpactMatch::DuelBallPicks));
		Points.Add(FString::Printf(TEXT("時間切れは体力の多いほう、同じなら%d秒のサドンデス"), FMath::RoundToInt(ChaosImpactMatch::DuelSuddenDeathSeconds)));
	}
	else if (Rules.bBattleRoyale)
	{
		Points.Add(Rules.IsTeamBattle() ? TEXT("最後まで生き残ったチームの勝ち") : TEXT("最後の1人になれば勝利"));
		Points.Add(Rules.IsTeamBattle() ? TEXT("時間切れは生存人数 → 残りHP合計で判定") : TEXT("時間切れは残りHPで判定"));
		Points.Add(TEXT("同率首位だけでサドンデス・復活なし"));
	}
	else if (Rules.IsTeamBattle())
	{
		Points.Add(FString::Printf(TEXT("%dチーム・1チーム最大%d人"), Rules.TeamCount, ChaosImpactMatch::GetTeamCapacity(Rules.TeamCount, Total)));
		Points.Add(TEXT("チームの合計ポイントで勝負"));
		Points.Add(TEXT("敵に当てる +1pt ・ 撃破ボーナス +1pt"));
	}
	else
	{
		Points.Add(TEXT("ポイントが一番多い人の勝ち"));
		Points.Add(TEXT("敵に当てる +1pt ・ 撃破ボーナス +1pt"));
		Points.Add(FString::Printf(TEXT("%d分間の勝負"), Rules.Minutes));
	}
	return Points;
}

FString ChaosImpactRuleCard::GetEnglish(const FChaosImpactMatchRules& Rules)
{
	return Rules.IsRubyRush() ? TEXT("RUBY RUSH") : Rules.IsCrownKeep() ? TEXT("CROWN KEEP") : Rules.bCrashBall ? TEXT("CRASH BALL")
		: Rules.bTournament ? TEXT("TOURNAMENT") : Rules.bDuel ? TEXT("DUEL") : Rules.bBattleRoyale ? TEXT("BATTLE ROYALE")
		: Rules.IsTeamBattle() ? TEXT("TEAM BATTLE") : TEXT("FREE FOR ALL");
}

FLinearColor ChaosImpactRuleCard::GetColor(const FChaosImpactMatchRules& Rules)
{
	using namespace ChaosImpactPaint;
	return Rules.IsParty() ? ChaosImpactParty::GetRuleColor(Rules) : Rules.bTournament || Rules.bCrashBall ? Violet
		: Rules.bDuel ? Ice : Rules.IsTeamBattle() ? Gold : Fire;
}
