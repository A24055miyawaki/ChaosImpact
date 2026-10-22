#include "RPGEquipment.h"
#include "RPGTuning.h"

FString RPGEquipment::BaseEffectDescription(EChaosImpactBallType Type)
{
    switch(Type)
    {
    case EChaosImpactBallType::Fire: return TEXT("小さな爆風＋直撃した敵を3秒燃焼（毎秒8%以上）");
    case EChaosImpactBallType::Ice: return TEXT("小さな冷気と20%減速。冷気3回で短く凍結");
    case EChaosImpactBallType::Thunder: return TEXT("高速・壁反射。直撃で短く感電し、近くの敵1体へ連鎖");
    case EChaosImpactBallType::Black: return TEXT("小さな重力場で引き寄せ、10%以上減速・継続ダメージ");
    case EChaosImpactBallType::Wind: return TEXT("小さな移動竜巻で敵を押し出し、継続ダメージ");
    case EChaosImpactBallType::Smoke: return TEXT("小さな煙幕で敵の照準を乱す。中の敵は狙いが15度以上ぶれる");
    case EChaosImpactBallType::Beam: return TEXT("高速の直線レーザー。敵1体を貫通して次の敵にも当たる");
    case EChaosImpactBallType::Snow: return TEXT("持って動くと最大1.2倍に成長。命中で小さく押し出し・減速");
    case EChaosImpactBallType::Simae: return TEXT("小鳥2羽が敵に分散して追尾・つつく。ダッシュで回避可能");
    case EChaosImpactBallType::Drive: return TEXT("2秒間自由に操縦。終了時に小さな爆発（20%以上の威力）");
    default: return TEXT("いつでも呼び出せる通常攻撃。壁で跳ね返る");
    }
}

// ---- 能力カタログ ----
// Mod(キー, 表示名, 説明, 最小, 最大, 変形)：説明の %.0f/%.1f/%.2f に抽選値が入る（%% は%記号）。
// キーはセーブとゲーム処理（RPGPrototypeBall/Effects/Passives/Character）が参照するので変えない。名前・説明・範囲は自由に変えてよい。
// 範囲を変えても古いセーブは読み込める（RPGEquipment::MigrateRoll で新しい範囲へ移す）。
// 変形(true)は球の撃ち方そのものを変える固有特性で、1個のアイテムに2つは付かない。
// 調整の理由と全一覧：Docs/RPGPrototype/全能力の見直し.md
namespace
{
using D = FRPGModDefinition;
D Mod(const TCHAR* Key, const TCHAR* Name, const TCHAR* Desc, float Min, float Max, bool Transform=false)
{ return {FName(Key), Name, Desc, Min, Max, Transform}; }
const TArray<D> Common{
    Mod(TEXT("Damage"),TEXT("威力増幅"),TEXT("威力 +%.0f%%"),8,18),
    Mod(TEXT("Cooldown"),TEXT("再装填"),TEXT("クールタイム -%.0f%%"),4,9),
    Mod(TEXT("Speed"),TEXT("高速投球"),TEXT("球速 +%.0f%%"),10,20),
    Mod(TEXT("Range"),TEXT("ロングスロー"),TEXT("飛距離 +%.0f%%"),15,30),
    Mod(TEXT("Charge"),TEXT("クイックチャージ"),TEXT("チャージ時間 -%.0f%%"),8,18),
    Mod(TEXT("FullCharge"),TEXT("全力投球"),TEXT("最大チャージ威力 +%.0f%%"),15,30),
    Mod(TEXT("QuickShot"),TEXT("速投"),TEXT("無溜め付近の威力 +%.0f%%"),12,25),
    Mod(TEXT("Near"),TEXT("接近戦"),TEXT("近距離（4m以内）威力 +%.0f%%"),15,30),
    Mod(TEXT("Far"),TEXT("遠投戦"),TEXT("遠距離（8m以上）威力 +%.0f%%"),15,30),
    Mod(TEXT("Rear"),TEXT("背面投球"),TEXT("背後から当てると威力 +%.0f%%"),20,40),
    Mod(TEXT("Bounce"),TEXT("バンクショット"),TEXT("壁反射後の威力 +%.0f%%"),20,40),
    Mod(TEXT("Knockback"),TEXT("衝撃投球"),TEXT("直撃時、敵を %.0fcmノックバック"),60,140),
    // 2026-10-20 0.3-0.7 → 0.5-1.0：撃破した球・場の効果でしか発動せず、特殊球の待ち5～8秒に対して効果が薄かった。
    Mod(TEXT("KillRefund"),TEXT("撃破回収"),TEXT("撃破時、特殊球の待ち時間を %.2f秒短縮"),.5f,1.0f)
};
TArray<D> Specific(EChaosImpactBallType Type)
{
    switch(Type)
    {
    case EChaosImpactBallType::Normal: return {
        Mod(TEXT("NormalPierce"),TEXT("貫通投球"),TEXT("%.0f体を追加貫通"),1,2),
        Mod(TEXT("NormalHoming"),TEXT("狙い補助"),TEXT("進行方向への追尾強度 +%.0f%%"),10,20),
        Mod(TEXT("NormalRepeat"),TEXT("二段投球"),TEXT("%.0f%%威力の小球を追投"),25,40),
        Mod(TEXT("NormalMark"),TEXT("連携準備"),TEXT("次の特殊球ダメージ +%.0f%%（3秒）"),10,20),
        Mod(TEXT("NormalWide"),TEXT("広い投球"),TEXT("当たり判定の幅 +%.0f%%"),10,20),
        Mod(TEXT("NormalBounce"),TEXT("弾む投球"),TEXT("壁反射後の球速 +%.0f%%"),10,20),
        Mod(TEXT("ChargeKnockback"),TEXT("溜め吹き飛ばし"),TEXT("最大溜め直撃時、押し出し +%.0fcm"),120,240)};
    case EChaosImpactBallType::Fire: return {
        Mod(TEXT("FireTrail"),TEXT("炎の軌跡"),TEXT("軌道上に炎。持続 %.1f秒"),1.2f,2.4f),
        Mod(TEXT("FireFloor"),TEXT("残り火"),TEXT("着弾地点が燃える。持続 %.1f秒"),2,4),
        Mod(TEXT("FireArea"),TEXT("爆炎拡大"),TEXT("着弾・残り火の半径 +%.0f%%"),20,45),
        Mod(TEXT("Burn"),TEXT("着火"),TEXT("直撃後、毎秒 %.0f%%の継続ダメージ（3秒）"),12,24),
        Mod(TEXT("FireSpread"),TEXT("飛び火"),TEXT("近くの敵へ %.0f%%の炎ダメージ"),20,40),
        Mod(TEXT("Heat"),TEXT("熱蓄積"),TEXT("同じ敵に連続直撃、1段ごと +%.0f%%（最大3段）"),8,16),
        Mod(TEXT("FireFinal"),TEXT("燃焼終爆"),TEXT("燃焼終了時 %.0f%%の追加ダメージ"),20,40),
        Mod(TEXT("FireFocus"),TEXT("集中燃焼"),TEXT("直撃威力 +%.0f%%／爆風半径 -20%%"),20,35),
        Mod(TEXT("BlastKnockback"),TEXT("爆風ノックバック"),TEXT("直撃・爆風で敵を %.0fcm押し出す"),70,150)};
    case EChaosImpactBallType::Ice: return {
        Mod(TEXT("IceArea"),TEXT("霜の広がり"),TEXT("冷気半径 +%.0f%%"),25,60),
        Mod(TEXT("IceSlow"),TEXT("深冷"),TEXT("減速効果 +%.0f%%"),10,25),
        Mod(TEXT("IceTime"),TEXT("長い冬"),TEXT("冷気持続 +%.1f秒"),1,2),
        Mod(TEXT("Freeze"),TEXT("凍結蓄積"),TEXT("冷気3回で %.1f秒凍結（再凍結制限あり）"),.5f,1),
        Mod(TEXT("Shatter"),TEXT("砕氷"),TEXT("凍結中に当てると +%.0f%%"),25,45),
        Mod(TEXT("IceShard"),TEXT("氷片"),TEXT("着弾時 %.0f個の小さな氷片"),2,4),
        Mod(TEXT("IceSpread"),TEXT("広域冷却"),TEXT("直撃した敵の周囲も %.0f%%減速"),10,20),
        Mod(TEXT("IceFocus"),TEXT("氷槍"),TEXT("直撃威力 +%.0f%%／半径 -20%%"),20,35)};
    case EChaosImpactBallType::Thunder: return {
        Mod(TEXT("Chain"),TEXT("連鎖雷"),TEXT("連鎖先 +%.0f体（各45%%威力）"),1,3),
        Mod(TEXT("ChainRange"),TEXT("雷の架け橋"),TEXT("連鎖距離 +%.0f%%"),20,45),
        Mod(TEXT("ThunderBounce"),TEXT("壁面加速"),TEXT("反射後の球速 +%.0f%%"),15,30),
        Mod(TEXT("ThunderMark"),TEXT("帯電"),TEXT("次の通常球威力 +%.0f%%（3秒）"),20,35),
        // 2026-10-20 0.15-0.30 → 0.25-0.40：サンダーは素で0.15秒足止めするため、低い抽選値だと効果が無かった。
        Mod(TEXT("ThunderStun"),TEXT("瞬間感電"),TEXT("直撃の足止めが %.2f秒になる（素は0.15秒）"),.25f,.4f),
        Mod(TEXT("ThunderField"),TEXT("放電床"),TEXT("着弾に放電場 %.1f秒"),1.5f,3),
        Mod(TEXT("ThunderEcho"),TEXT("追い雷"),TEXT("0.45秒後、%.0f%%の追加ダメージ"),20,35),
        Mod(TEXT("ThunderSolo"),TEXT("孤立放電"),TEXT("周囲に敵がいない対象への威力 +%.0f%%"),20,40)};
    case EChaosImpactBallType::Black: return {
        Mod(TEXT("BlackArea"),TEXT("重力圏"),TEXT("吸引半径 +%.0f%%"),20,50),
        Mod(TEXT("BlackPull"),TEXT("強い引力"),TEXT("吸引力 +%.0f%%"),30,70),
        Mod(TEXT("BlackTime"),TEXT("長期圧縮"),TEXT("持続 +%.1f秒"),1,2),
        Mod(TEXT("BlackCenter"),TEXT("重力の核"),TEXT("中心ダメージ +%.0f%%"),30,60),
        Mod(TEXT("BlackFinal"),TEXT("崩壊爆発"),TEXT("終了時 %.0f%%威力の爆発"),30,60),
        Mod(TEXT("BlackWeak"),TEXT("装甲圧縮"),TEXT("吸引中の敵が受けるダメージ +%.0f%%"),10,20),
        Mod(TEXT("BlackSlow"),TEXT("重力残留"),TEXT("吸引後 %.0f%%減速（2秒）"),15,30),
        Mod(TEXT("BlackNext"),TEXT("収束投球"),TEXT("吸引中の敵への通常球威力 +%.0f%%"),15,30)};
    case EChaosImpactBallType::Wind: return {
        Mod(TEXT("WindRange"),TEXT("長距離旋風"),TEXT("旋風の進行距離 +%.0f%%"),20,45),
        Mod(TEXT("WindArea"),TEXT("大旋風"),TEXT("旋風半径 +%.0f%%"),20,40),
        Mod(TEXT("WindPush"),TEXT("突風"),TEXT("押し出し力 +%.0f%%"),25,60),
        Mod(TEXT("WindLift"),TEXT("上昇気流"),TEXT("小さな敵を %.0fcm浮かせる"),80,140),
        Mod(TEXT("WindLoot"),TEXT("戦利品の風"),TEXT("近くの戦利品を %.0fcm寄せる（回収は手動）"),100,220),
        Mod(TEXT("WindTail"),TEXT("追い風"),TEXT("使用後3秒、移動 +%.0f%%"),12,20),
        Mod(TEXT("WindSplit"),TEXT("分かれ風"),TEXT("左右にも %.0f%%威力の小旋風"),35,50),
        Mod(TEXT("WindWall"),TEXT("壁へ激突"),TEXT("押し出し先に壁があると %.0f%%追加ダメージ"),25,45),
        Mod(TEXT("WindKick"),TEXT("初撃の突風"),TEXT("旋風の初回命中時、押し出し +%.0fcm"),100,200)};
    case EChaosImpactBallType::Smoke: return {
        Mod(TEXT("SmokeArea"),TEXT("広い煙幕"),TEXT("煙幕半径 +%.0f%%"),25,55),
        Mod(TEXT("SmokeTime"),TEXT("濃霧持続"),TEXT("煙幕持続 +%.1f秒"),1,3),
        Mod(TEXT("SmokeAim"),TEXT("視界不良"),TEXT("敵の照準のぶれ +%.0f度"),12,25),
        Mod(TEXT("SmokeWeak"),TEXT("不意打ち"),TEXT("煙の敵へのダメージ +%.0f%%"),15,25),
        Mod(TEXT("SmokeHide"),TEXT("姿隠し"),TEXT("煙の中で被ダメージ -%.0f%%"),10,20),
        Mod(TEXT("SmokeSpeed"),TEXT("煙駆け"),TEXT("煙の中で移動 +%.0f%%"),12,20),
        Mod(TEXT("SmokeHeal"),TEXT("深呼吸の煙"),TEXT("煙の中で攻撃せず毎秒HP %.2f%%回復"),.4f,.8f),
        Mod(TEXT("SmokeLinger"),TEXT("煙の余韻"),TEXT("煙を出た敵の照準不良 +%.1f秒"),1,2)};
    case EChaosImpactBallType::Beam: return {
        Mod(TEXT("BeamPierce"),TEXT("貫く光"),TEXT("%.0f体を追加貫通"),1,3),
        Mod(TEXT("BeamWidth"),TEXT("太い光"),TEXT("光の幅 +%.0f%%"),20,45),
        Mod(TEXT("BeamRange"),TEXT("長い光"),TEXT("光の射程 +%.0f%%"),25,50),
        Mod(TEXT("BeamFocus"),TEXT("集束光"),TEXT("威力 +%.0f%%／幅 -20%%"),20,35),
        Mod(TEXT("BeamBounce"),TEXT("反射光"),TEXT("壁で %.0f回追加反射"),1,2),
        Mod(TEXT("BeamStay"),TEXT("残光"),TEXT("通過地点に弱い光が %.1f秒残る"),.6f,1.2f),
        Mod(TEXT("BeamFalloff"),TEXT("減衰抑制"),TEXT("貫通後の威力低下を %.0f%%緩和"),40,70),
        Mod(TEXT("BeamMark"),TEXT("光の的"),TEXT("次の通常球威力 +%.0f%%（3秒）"),20,35)};
    case EChaosImpactBallType::Snow: return {
        Mod(TEXT("SnowGrow"),TEXT("雪の成長"),TEXT("成長速度 +%.0f%%"),20,50),
        Mod(TEXT("SnowMax"),TEXT("大きな雪玉"),TEXT("最大サイズ +%.0f%%（サイズ上限2.3倍）"),20,45),
        Mod(TEXT("SnowPower"),TEXT("重い雪玉"),TEXT("最大成長時の威力 +%.0f%%"),25,50),
        Mod(TEXT("SnowRoll"),TEXT("転がる雪"),TEXT("着地後 %.1f秒攻撃判定を保って転がる"),1.5f,3),
        Mod(TEXT("SnowPush"),TEXT("雪の圧力"),TEXT("押し出し +%.0fcm"),100,200),
        Mod(TEXT("SnowFloor"),TEXT("積雪床"),TEXT("着弾に %.0f%%減速する雪（3秒）"),15,30),
        Mod(TEXT("SnowShard"),TEXT("散る雪玉"),TEXT("着弾時 %.0f個に小分裂"),2,4),
        Mod(TEXT("SnowCarry"),TEXT("次の雪"),TEXT("次の雪玉の初期サイズ +%.0f%%"),15,30)};
    case EChaosImpactBallType::Simae: return {
        Mod(TEXT("BirdCount"),TEXT("大きな群れ"),TEXT("鳥の数 +%.0f羽（上限16羽）"),3,8),
        Mod(TEXT("BirdPeck"),TEXT("もうひとつき"),TEXT("各鳥のつつく回数 +%.0f回"),1,2),
        Mod(TEXT("BirdRange"),TEXT("遠くの獲物"),TEXT("群れの縄張り（敵を探す範囲）+%.0f%%"),20,45),
        Mod(TEXT("BirdTime"),TEXT("長い羽ばたき"),TEXT("群れの寿命 +%.1f秒"),1,2),
        // 説明を実装に合わせた：実際は撃破時に限らず、鳥が次の相手を探すたびの範囲が広がる。
        Mod(TEXT("BirdRetarget"),TEXT("次の獲物"),TEXT("鳥が次の相手を探す範囲 +%.0f%%"),20,40),
        Mod(TEXT("BirdSpeed"),TEXT("追跡飛行"),TEXT("鳥の速度 +%.0f%%（ダッシュより遅い上限）"),10,20),
        Mod(TEXT("BirdMark"),TEXT("群れと連携"),TEXT("通常球が当たった敵へのつつき +%.0f%%"),15,30),
        Mod(TEXT("BirdPower"),TEXT("精鋭の鳥"),TEXT("羽数 -1／つつき威力 +%.0f%%"),30,55)};
    case EChaosImpactBallType::Drive: return {
        Mod(TEXT("DriveTurn"),TEXT("小回り"),TEXT("旋回速度 +%.0f%%"),20,45),
        Mod(TEXT("DriveTime"),TEXT("長期走行"),TEXT("操作時間 +%.1f秒（上限5秒）"),.6f,1.2f),
        Mod(TEXT("DriveSpeed"),TEXT("高速走行"),TEXT("速度 +%.0f%%／旋回 -15%%"),15,30),
        Mod(TEXT("DrivePierce"),TEXT("突破走行"),TEXT("%.0f体を追加貫通"),1,2),
        Mod(TEXT("DriveEnd"),TEXT("終点爆発"),TEXT("走行終了時 %.0f%%威力で爆発"),30,55),
        Mod(TEXT("DriveGraze"),TEXT("かすめ走り"),TEXT("近くの敵を %.0f%%減速"),15,25),
        Mod(TEXT("DriveWall"),TEXT("壁蹴り走行"),TEXT("反射後の旋回速度 +%.0f%%"),25,50),
        Mod(TEXT("DriveOrbit"),TEXT("周回走行"),TEXT("終了後、周回する小球が %.1f秒残る"),1,2),
        Mod(TEXT("BlastKnockback"),TEXT("爆風ノックバック"),TEXT("直撃・爆風で敵を %.0fcm押し出す"),70,150)};
    default: return {};
    }
}
TArray<D> Unique(EChaosImpactBallType Type)
{
    switch(Type)
    {
    case EChaosImpactBallType::Normal: return {
        // 2026-10-20 35-50 → 60-90：毎投げ追投する共通チューン「二段投球」(25-40%)より、固有なのに弱かった。
        Mod(TEXT("NDouble"),TEXT("二投目"),TEXT("4回当てると次の投球で %.0f%%の小球を追投"),60,90,true),
        Mod(TEXT("NBank"),TEXT("跳弾名手"),TEXT("反射後の追尾強度 %.0f%%"),25,40),
        Mod(TEXT("NFinish"),TEXT("決め球"),TEXT("特殊球の後の通常球 +%.0f%%"),25,40),
        Mod(TEXT("NReturn"),TEXT("返り球"),TEXT("飛行後に手元へ帰る。帰路威力 %.0f%%"),40,60,true)};
    case EChaosImpactBallType::Fire: return {
        Mod(TEXT("FPillar"),TEXT("火柱"),TEXT("着弾後3回、%.0f%%の火柱"),25,40),
        Mod(TEXT("FSpark"),TEXT("火花追撃"),TEXT("着弾から %.0f個の火花"),2,4,true),
        Mod(TEXT("FWalk"),TEXT("火渡り"),TEXT("自分の火の上で移動 +%.0f%%"),12,20),
        Mod(TEXT("FHeat"),TEXT("灼熱蓄積"),TEXT("同じ敵への3回目、%.0f%%の爆発"),35,60),
        Mod(TEXT("FSpread"),TEXT("延焼連鎖"),TEXT("燃える敵の撃破に %.1f秒の炎（再連鎖なし）"),1.5f,2.5f)};
    case EChaosImpactBallType::Ice: return {
        Mod(TEXT("IFollow"),TEXT("追い霜"),TEXT("冷気が直撃した敵を %.1f秒追う"),2,3,true),
        Mod(TEXT("IPrison"),TEXT("氷の牢"),TEXT("凍結時、周りを %.0f%%減速"),15,25),
        Mod(TEXT("IShards"),TEXT("砕氷連鎖"),TEXT("凍結中の直撃で %.0f%%の氷片"),25,40),
        Mod(TEXT("ICycle"),TEXT("冷却循環"),TEXT("凍結で待ち時間 %.2f秒短縮（1秒に1回）"),.5f,1),
        Mod(TEXT("IPath"),TEXT("薄氷の道"),TEXT("軌跡に冷気。自分の移動 +%.0f%%"),10,18)};
    case EChaosImpactBallType::Thunder: return {
        Mod(TEXT("TReturn"),TEXT("往復雷"),TEXT("帰路にも %.0f%%の雷ダメージ"),40,60,true),
        Mod(TEXT("TRod"),TEXT("避雷針"),TEXT("0.7秒後、%.0f%%の落雷"),30,50),
        Mod(TEXT("TBank"),TEXT("雷跳び"),TEXT("壁反射後の方向補正 %.0f%%"),25,40),
        Mod(TEXT("TRefund"),TEXT("放電回収"),TEXT("直撃＋2体以上へ連鎖すると待ち時間 %.0f%%短縮"),10,18),
        Mod(TEXT("TTwin"),TEXT("双雷"),TEXT("2連投に変化。各 %.0f%%威力"),55,65,true)};
    case EChaosImpactBallType::Black: return {
        Mod(TEXT("BMove"),TEXT("移動重力"),TEXT("重力が毎秒 %.0fcm進む"),80,140,true),
        Mod(TEXT("BTwin"),TEXT("二重重力"),TEXT("左右2つの重力。各 %.0f%%威力"),55,65,true),
        Mod(TEXT("BPulse"),TEXT("重力脈動"),TEXT("吸引中、3回 %.0f%%の衝撃波"),20,35),
        Mod(TEXT("BHome"),TEXT("弾道集束"),TEXT("重力内の敵への通常球の追尾 +%.0f%%"),20,35),
        Mod(TEXT("BFinal"),TEXT("圧縮解放"),TEXT("中の敵1体につき終爆 +%.0f%%（5体まで）"),10,18)};
    case EChaosImpactBallType::Wind: return {
        Mod(TEXT("WExpand"),TEXT("暴風圏"),TEXT("定位置の旋風が最大 %.0f%%まで広がる"),140,180,true),
        Mod(TEXT("WTwin"),TEXT("双旋風"),TEXT("左右2つの旋風。各 %.0f%%威力"),55,65,true),
        Mod(TEXT("WReturn"),TEXT("風返し"),TEXT("帰り道にも %.0f%%威力"),40,60,true),
        Mod(TEXT("WGuard"),TEXT("風の護衛"),TEXT("使用後、周囲を %.1f秒押し返す"),1,2),
        Mod(TEXT("WCrash"),TEXT("環境激突"),TEXT("押した敵の近くの敵へ %.0f%%追加ダメージ"),25,40)};
    case EChaosImpactBallType::Smoke: return {
        Mod(TEXT("MFirst"),TEXT("影投げ"),TEXT("煙から撃つ初撃 +%.0f%%（3秒に1回）"),25,40),
        Mod(TEXT("MFollow"),TEXT("移動煙幕"),TEXT("煙が自分を %.1f秒追う"),2,3,true),
        Mod(TEXT("MDecoy"),TEXT("誘い煙"),TEXT("煙に囮を置き、敵の照準を %.1f秒そらす"),1.5f,2.5f),
        Mod(TEXT("MExit"),TEXT("煙抜け"),TEXT("煙を出て2秒、移動 +%.0f%%"),15,25),
        Mod(TEXT("MFinal"),TEXT("煙の置き土産"),TEXT("終了時 %.0f%%威力の弱い衝撃波"),20,35)};
    case EChaosImpactBallType::Beam: return {
        Mod(TEXT("LTriple"),TEXT("三叉光"),TEXT("3方向に発射。各 %.0f%%威力"),40,50,true),
        Mod(TEXT("LTwin"),TEXT("二連光"),TEXT("2連射。各 %.0f%%威力"),55,65,true),
        Mod(TEXT("LRefract"),TEXT("屈折"),TEXT("貫通後1回、次の敵へ方向補正 %.0f%%"),60,90),
        Mod(TEXT("LRoot"),TEXT("光の縫い止め"),TEXT("命中した敵を %.2f秒足止め"),.3f,.5f),
        Mod(TEXT("LFinal"),TEXT("終端爆光"),TEXT("射程の終端で %.0f%%の爆光"),30,50)};
    case EChaosImpactBallType::Snow: return {
        Mod(TEXT("SSmall"),TEXT("小雪連投"),TEXT("成長しない。待ち時間 -%.0f%%"),30,40,true),
        Mod(TEXT("STrain"),TEXT("雪玉列車"),TEXT("後ろから %.0f個の小雪玉"),2,3,true),
        Mod(TEXT("SGround"),TEXT("雪だまり"),TEXT("自分の雪の上で成長速度 +%.0f%%"),40,70),
        Mod(TEXT("SPush"),TEXT("圧雪突進"),TEXT("転がりながら敵を %.0fcm押す"),150,250),
        Mod(TEXT("SSplit"),TEXT("雪崩分裂"),TEXT("着弾に2つの雪玉。各 %.0f%%威力"),45,60,true)};
    case EChaosImpactBallType::Simae: return {
        Mod(TEXT("AParent"),TEXT("親鳥"),TEXT("羽数半減／つつき威力 +%.0f%%"),60,90,true),
        Mod(TEXT("AGuard"),TEXT("群れの守り"),TEXT("群れが自分の周囲 %.0fcmを警戒"),300,450,true),
        Mod(TEXT("ATravel"),TEXT("渡り直し"),TEXT("撃破で寿命 +%.1f秒（群れ1回まで）"),1,2),
        Mod(TEXT("ASpread"),TEXT("集団包囲"),TEXT("2体以上に分散中、威力 +%.0f%%"),10,20),
        Mod(TEXT("AFeather"),TEXT("お礼の羽"),TEXT("群れの撃破でスタミナ %.2f回復（群れ最大1）"),.2f,.4f)};
    case EChaosImpactBallType::Drive: return {
        Mod(TEXT("DReturn"),TEXT("帰還走行"),TEXT("操作後に帰還。帰路 %.0f%%威力"),40,60,true),
        Mod(TEXT("DOrbit"),TEXT("周回"),TEXT("操作後、着地点を %.1f秒周回"),1,2,true),
        Mod(TEXT("DRush"),TEXT("爆走"),TEXT("直進を続けると速度 +%.0f%%（旋回で解除）"),20,40),
        Mod(TEXT("DFollow"),TEXT("追走球"),TEXT("%.0f%%威力の小球が後を追う"),25,40),
        Mod(TEXT("DTrick"),TEXT("曲芸走行"),TEXT("大きく旋回後の初撃 +%.0f%%"),20,35)};
    default: return {};
    }
}
}

const TArray<EChaosImpactBallType>& RPGEquipment::BallRoster()
{
    static const TArray<EChaosImpactBallType> List{EChaosImpactBallType::Normal,EChaosImpactBallType::Fire,EChaosImpactBallType::Ice,
        EChaosImpactBallType::Thunder,EChaosImpactBallType::Black,EChaosImpactBallType::Wind,EChaosImpactBallType::Smoke,
        EChaosImpactBallType::Beam,EChaosImpactBallType::Snow,EChaosImpactBallType::Simae,EChaosImpactBallType::Drive};
    return List;
}
const TArray<FRPGModDefinition>& RPGEquipment::Tunes(EChaosImpactBallType Type)
{
    static const TMap<EChaosImpactBallType,TArray<D>> Lists=[] {
        TMap<EChaosImpactBallType,TArray<D>> Out;
        for(auto T:BallRoster())
        {
            TArray<D> Entries=Specific(T); Entries.Append(Common);
            // Wind immediately becomes a tornado, so a direct-projectile tune would never trigger.
            if(T==EChaosImpactBallType::Wind) { Entries.RemoveAll([](const D& E){return E.Key==TEXT("Knockback");}); }
            Out.Add(T,MoveTemp(Entries));
        }
        return Out;
    }();
    static const TArray<D> Empty;
    const auto* Entries=Lists.Find(Type); return Entries?*Entries:Empty;
}
bool RPGEquipment::IsCommonTune(FName Key)
{ return Common.ContainsByPredicate([&](const D& E){return E.Key==Key;}); }
const TArray<FRPGModDefinition>& RPGEquipment::Intrinsics(EChaosImpactBallType Type)
{
    static const TMap<EChaosImpactBallType,TArray<D>> Lists=[] {
        TMap<EChaosImpactBallType,TArray<D>> Out; for(auto T:BallRoster()) { Out.Add(T,Unique(T)); } return Out;
    }();
    static const TArray<D> Empty;
    const auto* Entries=Lists.Find(Type); return Entries?*Entries:Empty;
}
const TArray<FRPGModDefinition>& RPGEquipment::Badges(ERPGItemKind Kind)
{
    static const TArray<D> Life{
        Mod(TEXT("HP"),TEXT("タフネス"),TEXT("最大HP +%.0f%%"),15,30),
        Mod(TEXT("Armor"),TEXT("プロテクト"),TEXT("被ダメージ -%.0f%%"),5,10),
        Mod(TEXT("Regen"),TEXT("リカバリー"),TEXT("非戦闘中、毎秒HP %.2f%%回復"),.15f,.35f),
        Mod(TEXT("KillHeal"),TEXT("ハンター"),TEXT("撃破時、最大HPの %.1f%%回復"),1,3),
        Mod(TEXT("Counter"),TEXT("カウンター"),TEXT("被弾後の次の投球 +%.0f%%"),15,30),
        Mod(TEXT("LowArmor"),TEXT("ラストスタンド"),TEXT("HP30%%未満の被ダメージ -%.0f%%"),10,18),
        Mod(TEXT("DashArmor"),TEXT("ガードステップ"),TEXT("回避後0.6秒、被ダメージ -%.0f%%"),15,25),
        Mod(TEXT("ChargeArmor"),TEXT("チャージガード"),TEXT("チャージ中の被ダメージ -%.0f%%"),8,15)};
    static const TArray<D> Move{
        Mod(TEXT("Move"),TEXT("ランナー"),TEXT("移動速度 +%.0f%%"),4,8),
        Mod(TEXT("DashCD"),TEXT("ステップ"),TEXT("ダッシュ待ち時間 -%.0f%%"),8,16),
        Mod(TEXT("Stamina"),TEXT("ブレス"),TEXT("スタミナ自然回復 +%.0f%%"),15,30),
        Mod(TEXT("HitStamina"),TEXT("ヒットリズム"),TEXT("命中時のスタミナ回復 +%.0f%%"),10,25),
        Mod(TEXT("KillMove"),TEXT("チェイス"),TEXT("撃破後4秒、移動 +%.0f%%"),12,20),
        Mod(TEXT("HurtMove"),TEXT("エスケープ"),TEXT("被弾後2.5秒、移動 +%.0f%%"),15,25),
        Mod(TEXT("ChargeMove"),TEXT("フットワーク"),TEXT("溜め移動の減速を %.0f%%緩和"),15,30),
        Mod(TEXT("TravelPower"),TEXT("スノーラン"),TEXT("3m移動した次の投球 +%.0f%%"),10,20)};
    static const TArray<D> Tech{
        Mod(TEXT("AllPower"),TEXT("パワー"),TEXT("全投球ダメージ +%.0f%%"),5,10),
        Mod(TEXT("SpecialCD"),TEXT("リチャージ"),TEXT("特殊球クールタイム -%.0f%%"),5,9),
        Mod(TEXT("Combo"),TEXT("コンボ"),TEXT("特殊球の次の通常球 +%.0f%%"),15,25),
        Mod(TEXT("Switch"),TEXT("スイッチ"),TEXT("交互に特殊球を投げると +%.0f%%"),12,22),
        Mod(TEXT("BankPower"),TEXT("バンクショット"),TEXT("反射後の威力 +%.0f%%"),20,35),
        Mod(TEXT("Finish"),TEXT("フィニッシュ"),TEXT("HP25%%未満の敵への威力 +%.0f%%"),12,22),
        Mod(TEXT("Ruby"),TEXT("コレクター"),TEXT("獲得ルビー +%.0f%%"),10,25),
        Mod(TEXT("Scout"),TEXT("スカウト"),TEXT("戦利品の表示距離 +%.0f%%"),30,60)};
    static const TArray<D> Empty;
    return Kind==ERPGItemKind::LifeBadge?Life:Kind==ERPGItemKind::MobilityBadge?Move:Kind==ERPGItemKind::TechniqueBadge?Tech:Empty;
}
const TArray<FRPGModDefinition>& RPGEquipment::BadgeIntrinsics(ERPGItemKind Kind)
{
    static const TArray<D> Life{
        Mod(TEXT("LifeSurvive"),TEXT("不屈"),TEXT("致死ダメージをHP1で耐える（%.0f秒に1回）"),60,90),
        Mod(TEXT("LifeRecover"),TEXT("立て直し"),TEXT("低HP時に最大HP %.0f%%回復（45秒に1回）"),8,12),
        Mod(TEXT("LifeShield"),TEXT("仮の体力"),TEXT("撃破で最大HP %.0f%%のシールド（上限20%%）"),3,6),
        Mod(TEXT("LifeAid"),TEXT("応急手当"),TEXT("回復後2秒、被ダメージ -%.0f%%"),8,14),
        Mod(TEXT("LifePayback"),TEXT("やり返し"),TEXT("被弾後の命中でHP %.0f%%回復（3秒に1回）"),2,4),
        Mod(TEXT("LifeRevive"),TEXT("再起"),TEXT("復活後3秒、被ダメージ -%.0f%%"),20,35)};
    static const TArray<D> Move{
        Mod(TEXT("MoveDodge"),TEXT("すり抜け上手"),TEXT("回避で球を抜けるとスタミナ %.2f回復（1秒に1回）"),.2f,.4f),
        Mod(TEXT("MoveDecoy"),TEXT("残像"),TEXT("回避開始時 %.1f秒の囮"),.5f,.9f),
        Mod(TEXT("MoveKill"),TEXT("追い足"),TEXT("撃破後の初回回避、消費 %.0f%%返還"),30,50),
        Mod(TEXT("MoveBreathe"),TEXT("深呼吸"),TEXT("攻撃せず移動中、回復速度 +%.0f%%"),35,65),
        Mod(TEXT("MoveCounter"),TEXT("切り返し"),TEXT("回避直後の通常球 +%.0f%%"),15,25),
        Mod(TEXT("MoveEscape"),TEXT("救出走"),TEXT("低HPで敵から離れる時、移動 +%.0f%%"),10,18)};
    static const TArray<D> Tech{
        Mod(TEXT("TechCycle"),TEXT("好循環"),TEXT("通常球命中で特殊球待ち時間 %.2f秒短縮"),.15f,.3f),
        Mod(TEXT("TechPair"),TEXT("二球連携"),TEXT("別枠の特殊球を続けると +%.0f%%"),15,25),
        Mod(TEXT("TechFocus"),TEXT("一点突破"),TEXT("同じ敵へ通常・特殊交互命中、1段 +%.0f%%（3段）"),6,10),
        Mod(TEXT("TechCrowd"),TEXT("集団狩り"),TEXT("3体命中後の通常球が %.0f体を追加貫通"),1,2),
        Mod(TEXT("TechSense"),TEXT("戦利品の勘"),TEXT("レア以上の戦利品表示距離 +%.0f%%"),40,70),
        Mod(TEXT("TechLucky"),TEXT("素材の幸運"),TEXT("撃破ルビーが %.0f%%の確率で2倍"),10,20)};
    static const TArray<D> Empty;
    return Kind==ERPGItemKind::LifeBadge?Life:Kind==ERPGItemKind::MobilityBadge?Move:Kind==ERPGItemKind::TechniqueBadge?Tech:Empty;
}
const FRPGModDefinition* RPGEquipment::Find(FName Key)
{
    for(auto T:BallRoster())
    {
        for(const auto& D:Tunes(T)) { if(D.Key==Key) { return &D; } }
        for(const auto& D:Intrinsics(T)) { if(D.Key==Key) { return &D; } }
    }
    for(auto K:{ERPGItemKind::LifeBadge,ERPGItemKind::MobilityBadge,ERPGItemKind::TechniqueBadge})
    {
        for(const auto& D:Badges(K)) { if(D.Key==Key) { return &D; } }
        for(const auto& D:BadgeIntrinsics(K)) { if(D.Key==Key) { return &D; } }
    }
    for(auto K:{ERPGItemKind::Glove,ERPGItemKind::Shoes,ERPGItemKind::Supporter,ERPGItemKind::Pouch})
    { for(const auto& D:Gear(K)) { if(D.Key==Key) { return &D; } } }
    return nullptr;
}
bool RPGEquipment::IsCount(FName Key)
{
    static const TSet<FName> Counts{TEXT("NormalPierce"),TEXT("Chain"),TEXT("IceShard"),TEXT("BeamPierce"),TEXT("BeamBounce"),
        TEXT("SnowShard"),TEXT("BirdCount"),TEXT("BirdPeck"),TEXT("DrivePierce"),TEXT("FSpark"),TEXT("STrain"),TEXT("TechCrowd")};
    return Counts.Contains(Key);
}
float RPGEquipment::RollValue(const FRPGModDefinition& D,FRandomStream* Random)
{
    if(IsCount(D.Key)) { return Random?Random->RandRange(FMath::FloorToInt(D.Min),FMath::FloorToInt(D.Max)):FMath::RandRange(FMath::FloorToInt(D.Min),FMath::FloorToInt(D.Max)); }
    return Random?static_cast<float>(Random->FRandRange(D.Min,D.Max)):FMath::FRandRange(D.Min,D.Max);
}
float FRPGAffix::Value() const
{
    using namespace RPGTuning;
    const int32 R=FMath::Clamp(Rank,1,TuneMaxRank);
    if(RPGEquipment::IsCount(Key))
    {
        const float Cap=Key==TEXT("BirdCount")?BirdCountCap:Key==TEXT("BirdPeck")?BirdPeckCap:CountCap;
        return FMath::Min(Cap,FMath::FloorToFloat(Roll)+R-1);
    }
    return Roll*(1+TuneRankStep*(R-1));
}
void RPGEquipment::MigrateRoll(FRPGAffix& A)
{
    const auto* Def=Find(A.Key);
    if(!Def || !FMath::IsFinite(A.Roll)) { return; }
    A.Rank=FMath::Clamp(A.Rank,1,RPGTuning::TuneMaxRank);
    A.Roll=FMath::Clamp(A.Roll,Def->Min,Def->Max);
    if(IsCount(A.Key)) { A.Roll=FMath::Clamp(FMath::RoundToFloat(A.Roll),FMath::FloorToFloat(Def->Min),FMath::FloorToFloat(Def->Max)); }
}
FString RPGEquipment::CatalogCsv()
{
    FString Out=TEXT("group,owner,key,name,min,max,transform,description\n");
    TSet<FName> Seen;
    const auto Add=[&](const TCHAR* Group,const FString& Owner,const FRPGModDefinition& Def)
    {
        if(Seen.Contains(Def.Key) && FCString::Strcmp(Group,TEXT("tune"))==0) { return; }
        Seen.Add(Def.Key);
        Out+=FString::Printf(TEXT("%s,%s,%s,%s,%g,%g,%d,\"%s\"\n"),Group,*Owner,*Def.Key.ToString(),*Def.Name,Def.Min,Def.Max,Def.bTransform?1:0,*Def.Description.Replace(TEXT("\""),TEXT("'")));
    };
    for(const auto& Def:Common) { Add(TEXT("common"),TEXT("all balls"),Def); }
    for(auto T:BallRoster())
    {
        const FString Owner=T==EChaosImpactBallType::Normal?TEXT("Normal"):FString(ChaosImpactBallTypes::GetDisplayName(T));
        for(const auto& Def:Specific(T)) { Add(TEXT("tune"),Owner,Def); }
        for(const auto& Def:Unique(T)) { Add(TEXT("intrinsic"),Owner,Def); }
    }
    for(auto K:{ERPGItemKind::LifeBadge,ERPGItemKind::MobilityBadge,ERPGItemKind::TechniqueBadge})
    { for(const auto& Def:Badges(K)) { Add(TEXT("badge"),KindName(K),Def); } for(const auto& Def:BadgeIntrinsics(K)) { Add(TEXT("badge-intrinsic"),KindName(K),Def); } }
    for(auto K:{ERPGItemKind::Glove,ERPGItemKind::Shoes,ERPGItemKind::Supporter,ERPGItemKind::Pouch})
    { for(const auto& Def:Gear(K)) { Add(TEXT("gear"),KindName(K),Def); } }
    return Out;
}
FString RPGEquipment::Describe(const FRPGAffix& A)
{
    const auto* D=Find(A.Key);
    if(!D) { return TEXT("不明な特性"); }
    float Value=A.Value();
    if(IsCount(A.Key)) { Value=FMath::FloorToFloat(Value); }
    FString Out=D->Description;
    Out.ReplaceInline(TEXT("%.0f"),*FString::Printf(TEXT("%.0f"),Value));
    Out.ReplaceInline(TEXT("%.1f"),*FString::Printf(TEXT("%.1f"),Value));
    Out.ReplaceInline(TEXT("%.2f"),*FString::Printf(TEXT("%.2f"),Value));
    Out.ReplaceInline(TEXT("%%"),TEXT("%")); return Out;
}
FString RPGEquipment::KindName(ERPGItemKind K)
{
    if(K==ERPGItemKind::Glove) { return TEXT("グローブ"); }
    if(K==ERPGItemKind::Shoes) { return TEXT("シューズ"); }
    if(K==ERPGItemKind::Supporter) { return TEXT("サポーター"); }
    if(K==ERPGItemKind::Pouch) { return TEXT("ポーチ"); }
    return K==ERPGItemKind::Ball?TEXT("ボール"):K==ERPGItemKind::LifeBadge?TEXT("生命バッジ"):
        K==ERPGItemKind::MobilityBadge?TEXT("機動バッジ"):K==ERPGItemKind::TechniqueBadge?TEXT("技術バッジ"):TEXT("ルビー");
}

bool RPGEquipment::IsGear(ERPGItemKind Kind)
{ return Kind>=ERPGItemKind::Glove && Kind<=ERPGItemKind::Pouch; }
const TArray<FRPGModDefinition>& RPGEquipment::Gear(ERPGItemKind Kind)
{
    static const TArray<D> Glove{
        Mod(TEXT("GearCharge"),TEXT("速投グローブ"),TEXT("チャージ時間 -%.0f%%"),4,12),
        Mod(TEXT("GearBounce"),TEXT("反響グローブ"),TEXT("壁反射後の威力 +%.0f%%"),8,18),
        Mod(TEXT("GearPower"),TEXT("投球グローブ"),TEXT("全ボールの攻撃パワー +%.0f%%"),3,8)};
    static const TArray<D> Shoes{
        Mod(TEXT("GearWalk"),TEXT("旅人シューズ"),TEXT("6秒間攻撃・被弾がない時、移動速度 +%.0f%%"),5,12),
        Mod(TEXT("GearDashCD"),TEXT("切り返しシューズ"),TEXT("回避の再使用時間 -%.0f%%"),6,12),
        Mod(TEXT("GearDodge"),TEXT("すり抜けシューズ"),TEXT("回避で球を抜けるとスタミナ %.2f回復（1秒に1回）"),.1f,.25f)};
    static const TArray<D> Support{
        Mod(TEXT("GearHP"),TEXT("頑丈サポーター"),TEXT("最大HP +%.0f"),12,24),
        Mod(TEXT("GearArmor"),TEXT("守りのサポーター"),TEXT("被ダメージ -%.0f%%"),3,6),
        Mod(TEXT("GearRegen"),TEXT("休息サポーター"),TEXT("6秒間攻撃・被弾がない時、毎秒HP %.2f%%回復"),.1f,.3f)};
    static const TArray<D> Pouch{
        Mod(TEXT("GearCycle"),TEXT("循環ポーチ"),TEXT("通常球命中で特殊球待ち時間 %.2f秒短縮"),.1f,.25f),
        Mod(TEXT("GearStamina"),TEXT("呼吸のポーチ"),TEXT("スタミナ自然回復速度 +%.0f%%"),10,25),
        Mod(TEXT("GearHeal"),TEXT("救護ポーチ"),TEXT("受けるHP回復量 +%.0f%%"),5,12)};
    static const TArray<D> Empty;
    return Kind==ERPGItemKind::Glove?Glove:Kind==ERPGItemKind::Shoes?Shoes:Kind==ERPGItemKind::Supporter?Support:Kind==ERPGItemKind::Pouch?Pouch:Empty;
}
