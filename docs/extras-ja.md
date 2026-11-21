[English](extras.md) | 日本語

# おまけ

おまけはお手持ちのディスクイメージを読み、表示するものをゲーム自身のコードで読み込み、動かし、描きます。**F1 → おまけ（Extras） → おまけを開く（Open the extras）**から開く（ゲームは一時停止したままです）か、`xenogears --extras` で起動します。おまけは `-DXG_EXTRAS=ON` でビルドしたものに入っています。

![おまけ](img/extras-home.png)

## 素材（Assets）

![TIM 画像として表示した顔グラフィック](img/extras-browser.png)

両方のディスクのファイルを、種類、バンドル、LZSS、TIM 画像、モデル、サウンドバンクとあわせて見られます。書き出しもできます。

![同じファイルの 16 進表示](img/extras-browser-hex.png)

## ビジュアル（Visuals）

![ビジュアルのライブラリ](img/extras-visuals.png)

![フェイのフィールドスプライト](img/extras-sprites.png)

スプライト（Sprites）: すべてのフレーム、向き、パレットを、ゲームのスプライト処理で動かして表示します。1 フレーム、シート、アニメーションを PNG で書き出せます。

![フィールドの NPC](img/extras-sprites-npc.png)

![フェイのバトルスプライト](img/extras-sprites-battle.png)

部屋ごとのフィールドの NPC と、バトルのスプライト。

![ヴェルトール](img/extras-models.webp)

モデル（Models）: ギア、敵、闘技場、フィールドとワールドマップのモデル。glTF で書き出せます。

![バトルの闘技場](img/extras-models-arena.webp)

![ギアのアニメーションプログラム](img/extras-animations.png)

アニメーション（Animations）: ギアのプログラム、バトリングのクリップ、フィールドの MIMe ターゲット。

![バトルエフェクト](img/extras-battlefx.webp)

バトルエフェクト（Battle effects）: 技のスプライト、エフェクト、ギアの軌跡。

![ウォークメッシュ、トリガー、出口を示したフィールド](img/extras-maps-walkmesh.png)

マップビューアー（Map viewer）: ウォークメッシュ、トリガー、出口、アクターを示したフィールドを、ゲームのカメラか自由カメラで見られます。

![自由カメラで見たフィールド](img/extras.png)

![ワールドマップ](img/extras-maps-world.png)

自由カメラで見たワールドマップと、真上から見た全体図。全体図は PNG で書き出せます。

![ムービー](img/extras-gallery-movie.png)

ギャラリー（Gallery）: ゲーム自身のムービープレイヤーで再生するムービー。

![顔グラフィック](img/extras-gallery-pictures.png)

顔グラフィックとイベントの絵。

![シーン](img/extras-gallery-scenes.png)

シーン（Scenes）では、選んだカットシーンからゲームを始められます。

## ジュークボックス（Jukebox）

![ジュークボックス](img/extras-jukebox.png)

曲、効果音、ムービーの音楽を、ゲーム自身のサウンドドライバーで再生します。WAV で書き出せます。

## モンスター一覧（Bestiary）

![モンスター一覧の敵](img/extras-bestiary.png)

すべての敵について、能力値、弱点、落とすアイテム、攻撃、出現場所を見られます。全部を表示するか、自分の進み具合（My progress）を選んで、これまでに出会ったものだけを表示します。

![バトルの場面で再生した敵の攻撃](img/extras-bestiary-battle.webp)

バトルの場面で再生した攻撃。

## データ表（Tables）とスクリプト（Scripts）

![再生レート](img/extras-tables.png)

データ表: ディスクとゲームのコードから読んだ名前、曲、音、レート。TSV で書き出せます。

![部屋のアクターのルーチン](img/extras-scripts.png)

スクリプト: 部屋のアクターのルーチン、変数、トレース、会話の履歴。

## バトル闘技場（Battle arena）

![F1 → おまけ](img/extras-f1.png)

どのフィールドからでも遊べるバトリングの 2 人対戦と、プレイヤー 2 が敵の編成を操作する対戦バトル。オンライン（Online）では、このどちらも離れた場所にいる相手と遊べます。一方がホストになり、もう一方が同じセッションのコードで参加します（標準では UDP ポート 7461）。**F1 → おまけ**。
