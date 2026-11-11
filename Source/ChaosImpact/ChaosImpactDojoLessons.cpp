#include "ChaosImpactDojoLessons.h"
#include "Misc/CommandLine.h"

#include "Misc/ConfigCacheIni.h"

namespace
{
	using EGoal = EChaosImpactDojoGoal;
	using ECPU = EChaosImpactDojoCPU;
	using EArt = EChaosImpactDojoArt;
	using EPoint = EChaosImpactDojoPointer;
	using EBall = EChaosImpactBallType;
	using EAct = EChaosImpactAction;

	FChaosImpactDojoSlide Slide(const TCHAR* Text, const EArt Art, const EPoint Pointer = EPoint::None)
	{
		FChaosImpactDojoSlide Out;
		Out.Text = Text;
		Out.Art = Art;
		Out.Pointer = Pointer;
		return Out;
	}

	/** A lesson in short: id, name, summary, the task line and what it counts. */
	FChaosImpactDojoLesson Lesson(const TCHAR* Id, const TCHAR* Name, const TCHAR* Summary, const TCHAR* Task, const EGoal Goal,
		const int32 Count, TArray<FChaosImpactDojoSlide> Slides, TArray<EAct> Keys = {})
	{
		FChaosImpactDojoLesson Out;
		Out.Id = Id;
		Out.Name = Name;
		Out.Summary = Summary;
		Out.Task = Task;
		Out.Goal = Goal;
		Out.Count = Count;
		Out.Slides = MoveTemp(Slides);
		Out.TaskKeys = MoveTemp(Keys);
		return Out;
	}

	FChaosImpactDojoLesson& With(FChaosImpactDojoLesson& L, const EBall Ball, const int32 Give = 1)
	{
		L.Ball = Ball;
		L.Give = Give;
		return L;
	}

	FChaosImpactDojoLesson& Against(FChaosImpactDojoLesson& L, const ECPU CPU, const int32 CPUCount = 1, const float Distance = 900.0f,
		const EBall CPUBall = EBall::Normal)
	{
		L.CPU = CPU;
		L.CPUCount = CPUCount;
		L.Distance = Distance;
		L.CPUBall = CPUBall;
		return L;
	}

	/** One special ball's lesson: what it does, then used on a CPU for real. */
	FChaosImpactDojoLesson BallLesson(const EBall Ball, const TCHAR* Id, const TCHAR* Summary, const TCHAR* Task, const EGoal Goal,
		const int32 Count, TArray<FChaosImpactDojoSlide> Slides, const ECPU CPU = ECPU::Walk, const float Distance = 900.0f)
	{
		FChaosImpactDojoLesson L = Lesson(Id, ChaosImpactBallTypes::GetDisplayName(Ball), Summary, Task, Goal, Count, MoveTemp(Slides),
			{EAct::Throw});
		With(L, Ball, 1);
		Against(L, CPU, 1, Distance);
		L.HitKind = Ball;
		return L;
	}

	TArray<FChaosImpactDojoCategory> BuildCategories()
	{
		TArray<FChaosImpactDojoCategory> All;

		// ---------------------------------------------------------------------------------------------------- 基本
		{
			FChaosImpactDojoCategory& C = All.AddDefaulted_GetRef();
			C.Name = TEXT("基本");
			C.English = TEXT("BASICS");
			C.Color = FLinearColor(0.0f, 0.62f, 1.0f);
			{
				FChaosImpactDojoLesson L = Lesson(TEXT("b_move"), TEXT("動く"), TEXT("歩いて、行きたい所へ行く"),
					TEXT("光る輪の中へ順番に入ろう"), EGoal::Reach, 3,
					{Slide(TEXT("移動の操作で、キャラは向いた方へ歩く"), EArt::Move),
						Slide(TEXT("床の光る輪に入ると、次の輪が光る。順番にまわろう"), EArt::Move)},
					{EAct::MoveUp, EAct::MoveLeft, EAct::MoveDown, EAct::MoveRight});
				L.Unit = TEXT("か所");
				L.Distance = 800.0f;
				C.Lessons.Add(L);
			}
			{
				FChaosImpactDojoLesson L = Lesson(TEXT("b_pickup"), TEXT("ボールを拾う"), TEXT("床のボールは近づくだけで拾える"),
					TEXT("床のボールを拾おう"), EGoal::PickUp, 2,
					{Slide(TEXT("ボールは床に落ちている。近づくだけで拾える"), EArt::PickUp),
						Slide(TEXT("持てるのは2個まで。右下に持っているボールが出る"), EArt::PickUp, EPoint::Balls)});
				L.Unit = TEXT("個");
				L.Pointer = EPoint::Balls;
				L.Distance = 700.0f;
				C.Lessons.Add(L);
			}
			{
				FChaosImpactDojoLesson L = Lesson(TEXT("b_throw"), TEXT("投げる"), TEXT("押してはなすと投げる"),
					TEXT("マトをねらって投げよう"), EGoal::HitTarget, 3,
					{Slide(TEXT("投げるボタンを押して、はなすとボールを投げる"), EArt::Throw),
						Slide(TEXT("ボールは前に出ている矢印の向きへ飛ぶ"), EArt::Aim),
						Slide(TEXT("当たったボールはしばらくすると消える。また拾おう"), EArt::PickUp)},
					{EAct::Throw});
				With(L, EBall::Normal, 1);
				L.Targets = 3;
				L.Unit = TEXT("個");
				L.Distance = 650.0f;
				C.Lessons.Add(L);
			}
			{
				FChaosImpactDojoLesson L = Lesson(TEXT("b_charge"), TEXT("ためる"), TEXT("長押しでためると速くて強い球"),
					TEXT("最大までためてから投げよう"), EGoal::ChargedThrow, 3,
					{Slide(TEXT("投げるボタンを長押しすると、ボールにパワーがたまる"), EArt::Charge),
						Slide(TEXT("ためるほど球が速くなって、よけられにくい"), EArt::Charge),
						Slide(TEXT("ゲージがいっぱいで光ったら最大。そこではなそう"), EArt::Charge)},
					{EAct::Throw});
				With(L, EBall::Normal, 1);
				C.Lessons.Add(L);
			}
			{
				FChaosImpactDojoLesson L = Lesson(TEXT("b_aim"), TEXT("ねらう"), TEXT("矢印を向けた方へ投げる"),
					TEXT("まわりのマトを全部たおそう"), EGoal::HitTarget, 4,
					{Slide(TEXT("ためている間は、矢印の向きを自由に変えられる"), EArt::Aim),
						Slide(TEXT("相手の少し先をねらうと当たりやすい"), EArt::Moving),
						Slide(TEXT("ふつうのボールは、前にいる相手へ少しだけ曲がっていく"), EArt::Curve)},
					{EAct::Throw});
				With(L, EBall::Normal, 1);
				L.Targets = 4;
				L.Unit = TEXT("個");
				L.Distance = 900.0f;
				C.Lessons.Add(L);
			}
			{
				FChaosImpactDojoLesson L = Lesson(TEXT("b_dash"), TEXT("ダッシュ"), TEXT("一瞬で動き、その間は無敵"),
					TEXT("ダッシュしてみよう"), EGoal::Dash, 3,
					{Slide(TEXT("ダッシュで、向いている方へ一気に動く"), EArt::Dash),
						Slide(TEXT("ダッシュしている間は、ボールが当たらない（無敵）"), EArt::DashThrough),
						Slide(TEXT("ダッシュするとスタミナ（ST）を使う。少し待つと戻る"), EArt::Stamina, EPoint::Stamina)},
					{EAct::Dash});
				L.Pointer = EPoint::Stamina;
				C.Lessons.Add(L);
			}
			{
				FChaosImpactDojoLesson L = Lesson(TEXT("b_jump"), TEXT("ジャンプ"), TEXT("跳んで低い球や段差をこえる"),
					TEXT("ジャンプしてみよう"), EGoal::Jump, 3,
					{Slide(TEXT("ジャンプで、その場で跳びあがる"), EArt::Jump),
						Slide(TEXT("足もとを転がってくる球や、段差を跳びこせる"), EArt::Jump)},
					{EAct::Jump});
				C.Lessons.Add(L);
			}
			{
				FChaosImpactDojoLesson L = Lesson(TEXT("b_tackle"), TEXT("タックル"), TEXT("近くの相手に体当たり"),
					TEXT("CPUにタックルしよう"), EGoal::Tackle, 2,
					{Slide(TEXT("タックルで、前にいる相手に体当たりする"), EArt::Tackle),
						Slide(TEXT("当たった相手ははじき飛ばされる。近くまで寄ってから出そう"), EArt::Tackle)},
					{EAct::Tackle});
				Against(L, ECPU::Stand, 1, 500.0f);
				C.Lessons.Add(L);
			}
			{
				FChaosImpactDojoLesson L = Lesson(TEXT("b_swap"), TEXT("持ちかえ"), TEXT("次に投げる球を入れかえる"),
					TEXT("2個のボールを入れかえよう"), EGoal::Swap, 2,
					{Slide(TEXT("ボールを2個持つと、右下に2つ並ぶ。大きい方が次に投げる球"), EArt::Swap, EPoint::Balls),
						Slide(TEXT("入れかえボタンで、投げる順番を変えられる"), EArt::Swap, EPoint::Balls)},
					{EAct::SwapBall});
				With(L, EBall::Normal, 1);
				L.SecondBall = EBall::Fire;
				L.Pointer = EPoint::Balls;
				C.Lessons.Add(L);
			}
			{
				FChaosImpactDojoLesson L = Lesson(TEXT("b_cancel"), TEXT("やめる・捨てる"), TEXT("ためた球をしまう／足もとに置く"),
					TEXT("ためてから投げずにしまおう"), EGoal::Cancel, 2,
					{Slide(TEXT("ためている途中でキャンセルすると、投げずにしまえる"), EArt::Cancel),
						Slide(TEXT("いらない球は、捨てるボタンで足もとに置ける"), EArt::Drop)},
					{EAct::Throw, EAct::CancelThrow});
				With(L, EBall::Normal, 1);
				C.Lessons.Add(L);
			}
			{
				FChaosImpactDojoLesson L = Lesson(TEXT("b_drop"), TEXT("ボールを置く"), TEXT("持っている球を足もとに置く"),
					TEXT("持っているボールを置こう"), EGoal::Drop, 2,
					{Slide(TEXT("捨てるボタンで、持っている球を足もとに置く"), EArt::Drop),
						Slide(TEXT("置いた球はまた拾える。味方にわたすときにも使える"), EArt::PickUp)},
					{EAct::DropBall});
				With(L, EBall::Normal, 1);
				C.Lessons.Add(L);
			}
			{
				FChaosImpactDojoLesson L = Lesson(TEXT("b_review"), TEXT("おさらい"), TEXT("ここまでの全部で、動くCPUに当てる"),
					TEXT("歩きまわるCPUに当てよう"), EGoal::HitCPU, 3,
					{Slide(TEXT("ここまでのおさらい。ボールを拾って、ためて、ねらって投げる"), EArt::Review),
						Slide(TEXT("CPUは横に歩いている。少し先をねらおう"), EArt::Moving)},
					{EAct::Throw});
				With(L, EBall::Normal, 1);
				Against(L, ECPU::Walk, 1, 900.0f);
				C.Lessons.Add(L);
			}
		}

		// ---------------------------------------------------------------------------------------------------- 応用
		{
			FChaosImpactDojoCategory& C = All.AddDefaulted_GetRef();
			C.Name = TEXT("応用");
			C.English = TEXT("ADVANCED");
			C.Color = FLinearColor(1.0f, 0.36f, 0.06f);
			{
				FChaosImpactDojoLesson L = Lesson(TEXT("a_ko"), TEXT("HPとノックアウト"), TEXT("HPが0になるとノックアウト"),
					TEXT("CPUをノックアウトしよう"), EGoal::KnockOut, 1,
					{Slide(TEXT("左下の矢印がHP。当てられると1つ減る"), EArt::Health, EPoint::Health),
						Slide(TEXT("HPが0になるとノックアウト。少しすると復活する"), EArt::KnockOut),
						Slide(TEXT("対戦では、相手をたくさんノックアウトした人が勝ち"), EArt::KnockOut)},
					{EAct::Throw});
				With(L, EBall::Normal, 1);
				Against(L, ECPU::Stand, 1, 800.0f);
				L.Unit = TEXT("体");
				L.Pointer = EPoint::Health;
				C.Lessons.Add(L);
			}
			{
				FChaosImpactDojoLesson L = Lesson(TEXT("a_lead"), TEXT("動く相手をねらう"), TEXT("動く先をねらって当てる"),
					TEXT("走りまわるCPUに当てよう"), EGoal::HitCPU, 3,
					{Slide(TEXT("動いている相手には、今いる所ではなく行く先をねらう"), EArt::Moving),
						Slide(TEXT("ためた速い球なら、よけられる前に届く"), EArt::Charge)},
					{EAct::Throw});
				With(L, EBall::Normal, 1);
				Against(L, ECPU::Walk, 1, 1100.0f);
				C.Lessons.Add(L);
			}
			{
				FChaosImpactDojoLesson L = Lesson(TEXT("a_dodge"), TEXT("よけろ！"), TEXT("飛んでくる球をよける"),
					TEXT("CPUの球をよけ続けよう"), EGoal::Dodge, 5,
					{Slide(TEXT("CPUがボールを投げてくる。横に歩いて球の線から外れよう"), EArt::Dodge),
						Slide(TEXT("当たると最初からやりなおし。よけきろう"), EArt::Dodge)},
					{EAct::MoveLeft, EAct::MoveRight, EAct::Dash});
				Against(L, ECPU::Thrower, 1, 1100.0f);
				L.Unit = TEXT("球");
				C.Lessons.Add(L);
			}
			{
				FChaosImpactDojoLesson L = Lesson(TEXT("a_dashthrough"), TEXT("ダッシュですり抜け"), TEXT("無敵のダッシュで球を抜ける"),
					TEXT("飛んでくる球をダッシュですり抜けよう"), EGoal::DashThrough, 3,
					{Slide(TEXT("ダッシュの間は無敵。球が来る瞬間にダッシュすると、すり抜けられる"), EArt::DashThrough),
						Slide(TEXT("早すぎると無敵が切れてから当たる。ギリギリまで引きつけよう"), EArt::DashThrough)},
					{EAct::Dash});
				Against(L, ECPU::Thrower, 1, 1000.0f);
				L.Unit = TEXT("球");
				L.Pointer = EPoint::Stamina;
				C.Lessons.Add(L);
			}
			{
				FChaosImpactDojoLesson L = Lesson(TEXT("a_interrupt"), TEXT("タックルで止める"), TEXT("ためている相手をタックルで止める"),
					TEXT("ためているCPUをタックルで止めよう"), EGoal::Interrupt, 2,
					{Slide(TEXT("相手がためている間は、まだ球は飛んでこない。近づくチャンス！"), EArt::Interrupt),
						Slide(TEXT("近づいてタックルすると、ためた球を止められる"), EArt::Interrupt)},
					{EAct::Tackle});
				Against(L, ECPU::Charger, 1, 600.0f);
				C.Lessons.Add(L);
			}
			{
				FChaosImpactDojoLesson L = Lesson(TEXT("a_ballform"), TEXT("ボール化"), TEXT("自分がボールになって味方に投げてもらう"),
					TEXT("長押ししてボールになろう"), EGoal::BallForm, 1,
					{Slide(TEXT("ボタンを長押しすると、自分がボールになる"), EArt::BallForm),
						Slide(TEXT("ボールになると、味方が拾って投げてくれる。相手に当たると大ダメージ"), EArt::BallForm),
						Slide(TEXT("もう一度押すと元に戻る"), EArt::BallForm)},
					{EAct::DropBall});
				C.Lessons.Add(L);
			}
			{
				FChaosImpactDojoLesson L = Lesson(TEXT("a_map"), TEXT("全体マップ"), TEXT("ステージ全体を見わたす"),
					TEXT("全体マップを開いてみよう"), EGoal::OpenMap, 1,
					{Slide(TEXT("全体マップで、ステージ全体と相手の場所が見える"), EArt::Map),
						Slide(TEXT("ボールのある場所や、相手がどこから来るか確かめよう"), EArt::Map)},
					{EAct::OverviewMap});
				Against(L, ECPU::Walk, 2, 1300.0f);
				C.Lessons.Add(L);
			}
			{
				FChaosImpactDojoLesson L = Lesson(TEXT("a_review"), TEXT("おさらい対戦"), TEXT("本気のCPUと1対1"),
					TEXT("本気で動くCPUをノックアウトしよう"), EGoal::KnockOut, 1,
					{Slide(TEXT("応用のおさらい。CPUも本気で投げて、よけてくる"), EArt::Review),
						Slide(TEXT("よけて、ねらって、ノックアウトしよう"), EArt::KnockOut)},
					{EAct::Throw, EAct::Dash});
				With(L, EBall::Normal, 1);
				Against(L, ECPU::Fighter, 1, 1100.0f);
				L.Unit = TEXT("体");
				L.Pointer = EPoint::Health;
				C.Lessons.Add(L);
			}
		}

		// ---------------------------------------------------------------------------------------------------- ボール
		{
			FChaosImpactDojoCategory& C = All.AddDefaulted_GetRef();
			C.Name = TEXT("ボール");
			C.English = TEXT("BALLS");
			C.Color = FLinearColor(1.0f, 0.68f, 0.06f);
			C.Lessons.Add(BallLesson(EBall::Fire, TEXT("x_fire"), TEXT("当たると爆発、床が燃える"),
				TEXT("ファイアボールを当てよう"), EGoal::HitCPU, 2,
				{Slide(TEXT("ファイアボールは当たると爆発する"), EArt::Ball),
					Slide(TEXT("落ちた所と通った所が燃える。燃える床に乗るとダメージ"), EArt::Ball)}));
			C.Lessons.Add(BallLesson(EBall::Ice, TEXT("x_ice"), TEXT("床を凍らせ、相手を氷づけ"),
				TEXT("アイスボールでCPUを凍らせよう"), EGoal::Freeze, 2,
				{Slide(TEXT("アイスボールが落ちた所は凍って、つるつる滑る"), EArt::Ball),
					Slide(TEXT("近くにいた相手は氷づけになって、しばらく動けない"), EArt::Ball)}));
			C.Lessons.Add(BallLesson(EBall::Thunder, TEXT("x_thunder"), TEXT("まっすぐ速く、壁で跳ね返る"),
				TEXT("サンダーボールを当てよう"), EGoal::HitCPU, 2,
				{Slide(TEXT("サンダーボールはまっすぐ速く飛ぶ"), EArt::Ball),
					Slide(TEXT("壁に当たると跳ね返って、そのたびに速くなる。壁を使ってねらおう"), EArt::Ball)}));
			C.Lessons.Add(BallLesson(EBall::Black, TEXT("x_black"), TEXT("ブラックホールで引き寄せる"),
				TEXT("ブラックホールにCPUを巻きこもう"), EGoal::HitCPU, 2,
				{Slide(TEXT("落ちた所にブラックホールができる"), EArt::Ball),
					Slide(TEXT("まわりの相手を真ん中へ引き寄せる。真ん中は燃えていてダメージ"), EArt::Ball)}));
			C.Lessons.Add(BallLesson(EBall::Wind, TEXT("x_wind"), TEXT("投げると竜巻になる"),
				TEXT("竜巻でCPUを吹き飛ばそう"), EGoal::WindCarry, 2,
				{Slide(TEXT("ウィンドボールは投げると竜巻になって、前へ進む"), EArt::Ball),
					Slide(TEXT("巻きこまれた相手は吹き飛ぶ。竜巻が拾ったボールは自分の球になる"), EArt::Ball)}));
			C.Lessons.Add(BallLesson(EBall::Smoke, TEXT("x_smoke"), TEXT("煙で前が見えなくなる"),
				TEXT("CPUを煙に巻こう"), EGoal::Blind, 2,
				{Slide(TEXT("スモークボールは煙を広げる"), EArt::Ball),
					Slide(TEXT("煙の中にいる相手は、まわりがほとんど見えなくなる"), EArt::Ball)}));
			{
				FChaosImpactDojoLesson L = BallLesson(EBall::Beam, TEXT("x_beam"), TEXT("一直線、全員に当たる"),
					TEXT("並んだCPU2体を1発で撃ちぬこう"), EGoal::BeamBoth, 1,
					{Slide(TEXT("ビームは壁も人もすり抜けて、まっすぐ飛ぶ"), EArt::Ball),
						Slide(TEXT("線の上にいる相手みんなに当たる。並んだ所をねらおう"), EArt::Ball)}, ECPU::Stand, 800.0f);
				L.CPUCount = 2;
				C.Lessons.Add(L);
			}
			C.Lessons.Add(BallLesson(EBall::Snow, TEXT("x_snow"), TEXT("持って歩くほど大きくなる"),
				TEXT("大きくした雪玉を当てよう"), EGoal::HitCPU, 1,
				{Slide(TEXT("スノーボールは、持ったまま歩くと大きくなる"), EArt::Ball),
					Slide(TEXT("大きいほど当たりやすい。大きくなると頭の上から投げ落とす"), EArt::Ball)}, ECPU::Stand, 900.0f));
			{
				FChaosImpactDojoLesson L = BallLesson(EBall::Nova, TEXT("x_nova"), TEXT("3つのパーツで完成、巨大な大爆発"),
					TEXT("パーツを3つ集めて、ノヴァを当てよう"), EGoal::NovaBuild, 4,
					{Slide(TEXT("ノヴァは、ギア・コア・リングの3つのパーツを集めると完成する"), EArt::Ball, EPoint::Balls),
						Slide(TEXT("光の柱が立っている所にパーツがある。近づくと拾える"), EArt::PickUp),
						Slide(TEXT("完成したら、その場で長くためるほど大きくなる。光る着地点を相手に合わせよう"), EArt::Ball)},
					ECPU::Stand, 900.0f);
				L.Give = 0;
				L.Unit = TEXT("つ");
				L.Pointer = EPoint::Balls;
				C.Lessons.Add(L);
			}
			C.Lessons.Add(BallLesson(EBall::Simae, TEXT("x_simae"), TEXT("シマエナガが相手を追いかける"),
				TEXT("シマエナガでCPUをつつこう"), EGoal::HitCPU, 2,
				{Slide(TEXT("シマエナガボールから、シマエナガの群れが飛び出す"), EArt::Ball),
					Slide(TEXT("群れは相手を追いかけてつつく。ダッシュで振り落とされる"), EArt::Ball)}));
			C.Lessons.Add(BallLesson(EBall::Drive, TEXT("x_drive"), TEXT("投げたあと自分で操る"),
				TEXT("ドライブを操ってCPUに当てよう"), EGoal::HitCPU, 2,
				{Slide(TEXT("ドライブは、投げたあと移動の操作で曲げられる"), EArt::Ball),
					Slide(TEXT("逆を押すとその場で折り返す。逃げる相手を追いかけよう"), EArt::Ball)}, ECPU::Walk, 1100.0f));
			C.Lessons.Add(BallLesson(EBall::Prison, TEXT("x_prison"), TEXT("相手を殻に閉じこめる"),
				TEXT("プリズンでCPUを閉じこめよう"), EGoal::Trap, 1,
				{Slide(TEXT("プリズンボールが当たった相手は、殻に閉じこめられる"), EArt::Ball),
					Slide(TEXT("閉じこめた相手は拾って投げられる。ぶつけるとダメージ"), EArt::Ball)}));
			C.Lessons.Add(BallLesson(EBall::Hook, TEXT("x_hook"), TEXT("当てた相手やボールを引き寄せる"),
				TEXT("フックでCPUを引き寄せよう"), EGoal::HookPull, 2,
				{Slide(TEXT("フックは鎖つきの鉤を飛ばす。当たった相手は1ダメージで引き寄せられる"), EArt::Ball),
					Slide(TEXT("床のボールに当てると、手元まで引き寄せて拾える"), EArt::Ball),
					Slide(TEXT("引き寄せられている間にダッシュすると振りほどける"), EArt::DashThrough)}, ECPU::Walk, 1200.0f));
			{
				FChaosImpactDojoLesson L = BallLesson(EBall::Anchor, TEXT("x_anchor"), TEXT("打ちこんだ所まで一気に移動"),
					TEXT("アンカーで遠くの輪まで移動しよう"), EGoal::Reach, 3,
					{Slide(TEXT("アンカーは壁や床に打ちこんで、そこまで一気に移動する"), EArt::Ball),
						Slide(TEXT("当てる球ではない。逃げたり回りこんだりするのに使う"), EArt::Ball),
						Slide(TEXT("移動中もボールには当たる。ダッシュで途中で止まれる"), EArt::Ball)}, ECPU::None, 1800.0f);
				L.CPUCount = 0;
				L.Unit = TEXT("か所");
				C.Lessons.Add(L);
			}
		}

		// ---------------------------------------------------------------------------------------------------- 対戦ルール
		{
			FChaosImpactDojoCategory& C = All.AddDefaulted_GetRef();
			C.Name = TEXT("対戦ルール");
			C.English = TEXT("MATCH RULES");
			C.Color = FLinearColor(0.62f, 0.3f, 1.0f);
			{
				FChaosImpactDojoLesson L = Lesson(TEXT("r_vs"), TEXT("VSのルール"), TEXT("時間内にたくさんノックアウト"),
					TEXT("本番のVSでCPUを2回ノックアウトしよう"), EGoal::KnockOut, 2,
					{Slide(TEXT("VSは時間内に、相手をたくさんノックアウトした人が勝ち"), EArt::KnockOut),
						Slide(TEXT("ノックアウトされても少しすると復活する。あきらめずに戦おう"), EArt::KnockOut),
						Slide(TEXT("ステージにはいろいろなボールが出てくる。特殊なボールをうまく使おう"), EArt::Ball)},
					{EAct::Throw});
				Against(L, ECPU::Fighter, 1, 1100.0f);
				L.bStagePads = true;
				L.bMatchSpawns = true;
				L.bRealMatch = true;
				L.MatchCPUs = 1;
				L.Unit = TEXT("回");
				C.Lessons.Add(L);
			}
			{
				FChaosImpactDojoLesson L = Lesson(TEXT("r_team"), TEXT("チーム戦"), TEXT("味方と協力して戦う"),
					TEXT("本番のチーム戦で、相手チームに3回当てよう"), EGoal::HitCPU, 3,
					{Slide(TEXT("チーム戦では、同じ色のチームが味方"), EArt::Teams),
						Slide(TEXT("味方にボールは当たらない。味方がボール化したら拾って投げよう"), EArt::BallForm),
						Slide(TEXT("相手チームを合わせてたくさんノックアウトしたチームの勝ち"), EArt::Teams)},
					{EAct::Throw});
				Against(L, ECPU::Fighter, 2, 1000.0f);
				L.bStagePads = true;
				L.bMatchSpawns = true;
				// Two on two: a CPU on the player's side, two on the other.
				L.bRealMatch = true;
				L.MatchTeams = 2;
				L.MatchCPUs = 3;
				C.Lessons.Add(L);
			}
			{
				FChaosImpactDojoLesson L = Lesson(TEXT("r_duel"), TEXT("デュエル模擬戦"), TEXT("デュエルアリーナで1ラウンド勝負"),
					TEXT("本番のデュエルで、CPUから1ラウンド取ろう"), EGoal::KnockOut, 1,
					{Slide(TEXT("デュエルは1対1。先に決まった回数ラウンドを取った方が勝ち"), EArt::Duel),
						Slide(TEXT("ラウンドのはじめに、全部の台にボールが出る。拾いに走ろう"), EArt::PickUp),
						Slide(TEXT("時間切れのときは、HPが多い方がラウンドを取る"), EArt::Health, EPoint::Health),
						Slide(TEXT("本番と同じデュエルで、CPUから1ラウンド取ってみよう"), EArt::Duel)},
					{EAct::Throw, EAct::Dash});
				Against(L, ECPU::Fighter, 1, 900.0f);
				L.bDuelArena = true;
				L.bStagePads = true;
				L.bMatchSpawns = true;
				L.bRealMatch = true;
				L.bMatchDuel = true;
				L.MatchCPUs = 1;
				L.CPULevel = 1;
				L.Unit = TEXT("回");
				C.Lessons.Add(L);
			}
			{
				FChaosImpactDojoLesson L = Lesson(TEXT("r_sudden"), TEXT("サドンデス"), TEXT("エリアが狭まる決着戦"),
					TEXT("狭まる円の中にいつづけよう"), EGoal::StayInZone, 10,
					{Slide(TEXT("時間切れで同点なら、サドンデス。床がまわりから円になって狭まる"), EArt::SuddenDeath),
						Slide(TEXT("円の外に1秒いると負け。先に当てられても負け"), EArt::SuddenDeath),
						Slide(TEXT("特殊なボールがどんどん出てくる。最後まで円の中で戦おう"), EArt::SuddenDeath)},
					{EAct::MoveUp, EAct::MoveLeft, EAct::MoveDown, EAct::MoveRight});
				L.Unit = TEXT("秒");
				L.bDuelArena = true;
				C.Lessons.Add(L);
			}
		}
		// ---------------------------------------------------------------------------------------------------- チャレンジ
		{
			FChaosImpactDojoCategory& C = All.AddDefaulted_GetRef();
			C.Name = TEXT("チャレンジ");
			C.English = TEXT("CHALLENGE");
			C.Color = FLinearColor(1.0f, 0.16f, 0.32f);
			{
				FChaosImpactDojoLesson L = Lesson(TEXT("c_targets"), TEXT("動くマト速撃ち"), TEXT("左右に動くマト6個を20秒で"),
					TEXT("動くマトを20秒で6個たおせ"), EGoal::HitTarget, 6,
					{Slide(TEXT("マトが左右に動いている。行く先をねらって速い球で"), EArt::Moving),
						Slide(TEXT("20秒以内に6個。時間切れなら最初からやりなおし"), EArt::Aim)},
					{EAct::Throw});
				With(L, EBall::Normal, 1);
				L.Targets = 6;
				L.bMovingTargets = true;
				L.TimeLimit = 20.0f;
				L.Unit = TEXT("個");
				L.Distance = 1000.0f;
				C.Lessons.Add(L);
			}
			{
				FChaosImpactDojoLesson L = Lesson(TEXT("c_crossfire"), TEXT("十字砲火"), TEXT("3体のCPUの球を8回よけきる"),
					TEXT("3方向からの球を8回よけきれ"), EGoal::Dodge, 8,
					{Slide(TEXT("3体のCPUが代わる代わる投げてくる"), EArt::Dodge),
						Slide(TEXT("1回でも当たると最初から。ダッシュも使ってよけきろう"), EArt::DashThrough, EPoint::Stamina)},
					{EAct::MoveLeft, EAct::MoveRight, EAct::Dash});
				Against(L, ECPU::Thrower, 3, 1000.0f);
				L.ThrowEvery = 2.4f;
				L.Unit = TEXT("球");
				C.Lessons.Add(L);
			}
			{
				FChaosImpactDojoLesson L = Lesson(TEXT("c_dash5"), TEXT("すり抜け5連続"), TEXT("ダッシュですり抜けを5回続けて"),
					TEXT("ダッシュで5回続けてすり抜けろ"), EGoal::DashThrough, 5,
					{Slide(TEXT("球が当たる直前にダッシュ。無敵ですり抜ける"), EArt::DashThrough),
						Slide(TEXT("当たったら最初から。スタミナの回復も考えよう"), EArt::Stamina, EPoint::Stamina)},
					{EAct::Dash});
				Against(L, ECPU::Thrower, 1, 1000.0f);
				L.ThrowEvery = 1.6f;
				L.bNoDamage = true;
				L.Unit = TEXT("回");
				C.Lessons.Add(L);
			}
			{
				FChaosImpactDojoLesson L = Lesson(TEXT("c_rush"), TEXT("15秒で5ヒット"), TEXT("走りまわるCPUに素早く当てる"),
					TEXT("15秒以内にCPUへ5回当てろ"), EGoal::HitCPU, 5,
					{Slide(TEXT("CPUは左右に走りまわる。行く先に速い球を"), EArt::Moving),
						Slide(TEXT("拾って、ためて、投げる。全部を素早く"), EArt::Review)},
					{EAct::Throw});
				With(L, EBall::Normal, 1);
				Against(L, ECPU::Walk, 1, 1000.0f);
				L.TimeLimit = 15.0f;
				C.Lessons.Add(L);
			}
			{
				FChaosImpactDojoLesson L = Lesson(TEXT("c_anchorrun"), TEXT("アンカー5連移動"), TEXT("5つの輪を25秒でめぐる"),
					TEXT("アンカーで5つの輪を25秒でめぐれ"), EGoal::Reach, 5,
					{Slide(TEXT("輪はステージのあちこち。アンカーで一気に飛ぼう"), EArt::Ball),
						Slide(TEXT("25秒以内に5つ全部。歩いていては間に合わない"), EArt::Ball)},
					{EAct::Throw});
				With(L, EBall::Anchor, 1);
				L.Distance = 1800.0f;
				L.TimeLimit = 25.0f;
				L.Unit = TEXT("か所");
				C.Lessons.Add(L);
			}
			{
				FChaosImpactDojoLesson L = Lesson(TEXT("c_drivecourse"), TEXT("ドライブ一本道"), TEXT("くねくね道を壁に当てずに通す"),
					TEXT("壁に当てずにドライブを通してCPUに当てろ"), EGoal::HitCPU, 2,
					{Slide(TEXT("ギリギリの幅の、くねくねした一本道。その向こうにCPUが立っている"), EArt::DriveCourse),
						Slide(TEXT("その場から動けない。ドライブを投げて、道にそって曲げていこう。壁にふれたら最初から"), EArt::DriveCourse),
						Slide(TEXT("ためずに投げると遅くてよく曲がる。ためた速い球は曲がりにくい"), EArt::DriveCourse)},
					{EAct::Throw, EAct::MoveUp, EAct::MoveLeft, EAct::MoveDown, EAct::MoveRight});
				With(L, EBall::Drive, 1);
				Against(L, ECPU::Stand, 1, 1200.0f);
				L.HitKind = EBall::Drive;
				L.bDriveCourse = true;
				L.Unit = TEXT("回");
				C.Lessons.Add(L);
			}
			{
				FChaosImpactDojoLesson L = Lesson(TEXT("c_nodamage"), TEXT("ノーダメージ撃破"), TEXT("一度も当たらずにノックアウト"),
					TEXT("当たらずにCPUをノックアウトしろ"), EGoal::KnockOut, 1,
					{Slide(TEXT("相手はふつうの強さのCPU。投げてくるし、よけてくる"), EArt::Review),
						Slide(TEXT("1回でも当たったら最初から。よけてから投げ返そう"), EArt::Dodge, EPoint::Health)},
					{EAct::Throw, EAct::Dash});
				With(L, EBall::Normal, 1);
				Against(L, ECPU::Fighter, 1, 1100.0f);
				L.CPULevel = 1;
				L.bNoDamage = true;
				L.Unit = TEXT("体");
				C.Lessons.Add(L);
			}
			{
				FChaosImpactDojoLesson L = Lesson(TEXT("c_sudden"), TEXT("サドンデス耐久"), TEXT("狭まる円で球もよける"),
					TEXT("円の中で20秒、球に当たらずに耐えろ"), EGoal::StayInZone, 20,
					{Slide(TEXT("円はどんどん狭まる。外に1秒出たら最初から"), EArt::SuddenDeath),
						Slide(TEXT("CPUも投げてくる。当たっても最初から"), EArt::Dodge)},
					{EAct::MoveUp, EAct::MoveLeft, EAct::MoveDown, EAct::MoveRight, EAct::Dash});
				Against(L, ECPU::Thrower, 1, 1100.0f);
				L.ThrowEvery = 2.2f;
				L.bNoDamage = true;
				L.bDuelArena = true;
				L.Unit = TEXT("秒");
				C.Lessons.Add(L);
			}
			{
				FChaosImpactDojoLesson L = Lesson(TEXT("c_strong"), TEXT("強いCPUに勝て"), TEXT("強いCPUを2回ノックアウト"),
					TEXT("強いCPUを2回ノックアウトしろ"), EGoal::KnockOut, 2,
					{Slide(TEXT("強いCPUは球を読んでよけ、すきを見て投げてくる"), EArt::Review),
						Slide(TEXT("特殊な球がないぶん、ためと読みあいで勝負"), EArt::Charge)},
					{EAct::Throw, EAct::Dash, EAct::Tackle});
				With(L, EBall::Normal, 1);
				Against(L, ECPU::Fighter, 1, 1100.0f);
				L.CPULevel = 2;
				L.Unit = TEXT("回");
				C.Lessons.Add(L);
			}
			{
				FChaosImpactDojoLesson L = Lesson(TEXT("c_final"), TEXT("最終試験"), TEXT("強いCPU2体を相手にノックアウト"),
					TEXT("強いCPU2体から2回ノックアウトを取れ"), EGoal::KnockOut, 2,
					{Slide(TEXT("最終試験。強いCPUが2体同時にかかってくる"), EArt::Teams),
						Slide(TEXT("囲まれないように動きながら、1体ずつ確実に"), EArt::Review)},
					{EAct::Throw, EAct::Dash, EAct::Tackle});
				With(L, EBall::Normal, 1);
				Against(L, ECPU::Fighter, 2, 1100.0f);
				L.CPULevel = 2;
				L.Unit = TEXT("回");
				C.Lessons.Add(L);
			}
		}

		return All;
	}

	const TCHAR* DojoSection = TEXT("ChaosImpact.Dojo");

	TSet<FString>& Cleared()
	{
		static TSet<FString> Ids;
		static bool bLoaded = false;
		if (!bLoaded && GConfig)
		{
			bLoaded = true;
			FString Saved;
			GConfig->GetString(DojoSection, TEXT("Cleared"), Saved, GGameUserSettingsIni);
			TArray<FString> Parts;
			Saved.ParseIntoArray(Parts, TEXT(","), true);
			Ids.Append(Parts);
		}
		return Ids;
	}

	void SaveCleared()
	{
		if (!GConfig)
		{
			return;
		}
		TArray<FString> Ids = Cleared().Array();
		Ids.Sort();
		GConfig->SetString(DojoSection, TEXT("Cleared"), *FString::Join(Ids, TEXT(",")), GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

FString ChaosImpactDojo::GetLessonLevel(const FChaosImpactDojoLesson& Lesson)
{
	return Lesson.bDuelArena ? TEXT("/Game/ChaosImpact/Versus/Levels/Lvl_VS_Duel") : TEXT("/Game/ChaosImpact/Versus/Levels/Lvl_VS_Stage1");
}

const TArray<FChaosImpactDojoCategory>& ChaosImpactDojo::GetCategories()
{
	static const TArray<FChaosImpactDojoCategory> Categories = BuildCategories();
	return Categories;
}

const FChaosImpactDojoLesson* ChaosImpactDojo::GetLesson(const int32 Category, const int32 Lesson)
{
	const TArray<FChaosImpactDojoCategory>& All = GetCategories();
	return All.IsValidIndex(Category) && All[Category].Lessons.IsValidIndex(Lesson) ? &All[Category].Lessons[Lesson] : nullptr;
}

bool ChaosImpactDojo::IsCleared(const FString& LessonId)
{
	return Cleared().Contains(LessonId);
}

void ChaosImpactDojo::MarkCleared(const FString& LessonId)
{
	if (!Cleared().Contains(LessonId))
	{
		Cleared().Add(LessonId);
		// (Automated tests and filming clear lessons too: kept for the run only, never in the player's saved progress.)
		const FString Line = FCommandLine::Get();
		const bool bTesting = GIsAutomationTesting || Line.Contains(TEXT("Automation RunTests")) || Line.Contains(TEXT("-CIDojoRecord"));
		if (!bTesting)
		{
			SaveCleared();
		}
	}
}

int32 ChaosImpactDojo::CountCleared(const int32 Category)
{
	const TArray<FChaosImpactDojoCategory>& All = GetCategories();
	int32 Count = 0;
	if (All.IsValidIndex(Category))
	{
		for (const FChaosImpactDojoLesson& L : All[Category].Lessons)
		{
			Count += IsCleared(L.Id) ? 1 : 0;
		}
	}
	return Count;
}

void ChaosImpactDojo::ResetProgress()
{
	Cleared().Reset();
	SaveCleared();
}
