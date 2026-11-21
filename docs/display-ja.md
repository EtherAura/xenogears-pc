[English](display.md) | 日本語

# 画面表示

![16:9、PS1 の 6 倍の解像度で表示したラハン村](img/display.png)

16:9、PS1 の 6 倍の解像度、縦の表示範囲（Vertical framing）を拡張（Expanded）にしたラハン村。

F1 で設定メニューが開きます（ゲームパッドでは Guide ボタン、または左右のスティックの同時押し込み）。変更はすぐに反映され、`settings.ini` に保存されます。

![F1 → 画面](img/display-menu.png)

## ワイド画面

![4:3 テレビと 16:9](img/display-aspect.webp)

4:3 テレビ（4:3 TV）と 16:9。ワイドのアスペクト比では、場面の左右がより広く描かれます。**F1 → 画面（Display） → アスペクト比（Aspect）**（F3 キーでも可）。

![21:9](img/display-ultrawide.webp)

21:9。アスペクト比にはほかに、4:3 全体（4:3 fill）、1:1 正方形ピクセル（1:1 square pixels）、16:10、32:9、カスタム 横:縦（custom w:h）があります。

![縦の表示範囲 オリジナルと拡張](img/display-vertical.webp)

拡張は、フィールド、バトル、ワールドマップで、ゲームが画面の上下に黒く空けているラインまで描きます。**F1 → 画面 → 縦の表示範囲**。

![ムービーの横の黒帯、ズーム、ぼかし](img/display-widefill.webp)

ムービーは 4:3 のままです。ワイドの余白（Wide fill）は、その左右に黒帯、拡大した映像、ぼかした映像のいずれかを置きます。**F1 → 画面 → ワイドの余白**（F6 キーでも可）。

![フィット、整数倍、引き伸ばし](img/display-scale.webp)

フィット（Fit）、整数倍（Integer）、引き伸ばし（Stretch）。ここでは 16:9 のウィンドウに 4:3 の画像を表示しています。**F1 → 画面 → 拡大方法（Scale）**（F4 キーでも可）。

## 解像度とフィルター

![1x、2x、4x、6x](img/display-resolution.png)

ゲームを本来の解像度の 1x〜12x、または自動（Auto。ウィンドウの高さから決まります）で描きます。ゲーム自体は内部で 1x の画像のまま動いています。**F1 → 画面 → 解像度（Resolution）**（Shift+F4 キーで 1x と切り替え）。

![トゥルーカラー、15 ビット、15 ビット(細かい)、8 ビットそのまま](img/display-colour.png)

1x より上では、トゥルーカラー（True colour）は PS1 のディザをなくし、15 ビット（15-bit）はディザを PS1 の大きさのまま残し、15 ビット(細かい)（15-bit fine）は高い解像度の細かさで残します。**F1 → 画面 → 色（Colour）**。

![ニアレスト、リニア、シャープ、バイキュービック、xBR](img/display-filter.png)

画像をウィンドウに合わせて拡大する方法です。ニアレスト（Nearest）、リニア（Linear）、シャープ（Sharp）、バイキュービック（Bicubic）、xBR(縁) から選びます。**F1 → 画面 → フィルター（Filter）**（F5 キーでも可）。

![ムービーのシェーダー なしと Smooth](img/display-movie-shader.png)

ムービーには専用のフィルターとシェーダーを設定できます。Smooth (built in) は、ムービーのブロックの境目をなめらかにします。**F1 → 画面 → ムービー（Movies）、ムービーのシェーダー（Movie shader）**。シェーダー（Shader）には、画面全体にかける RetroArch の `.slangp` か `.slang` のプリセットを指定します。

## なめらかな動き

![Motion smoothing のオフとオン](img/clip-smooth.webp)

ゲームはほとんどの場面で 30 fps で動きます。Motion smoothing は、フィールドとワールドマップのコマとコマの間の画像を、60、120、144、240 Hz のいずれかで描きます。ゲームの処理と音のタイミングは変わりませんが、ゲームの 1 フレーム分の遅れが加わります。**F1 → ゲームプレイ（Gameplay） → 高速化（Speedup）、Motion smoothing、Motion display rate**。

強化エンジン（**F1 → システム（System） → Engine**）では、代わりにフィールド、ワールドマップ、バトルそのものが 60 fps で動きます。

## パフォーマンスオーバーレイ

![パフォーマンスオーバーレイ](img/display-perf.png)

フレーム、CPU、GPU、オーディオの処理時間を表示します。F10 キーで表示し、Shift+F10 でログに書き出します。

## 初期値とキー

| 設定 | 初期値 |
|---|---|
| アスペクト比 | 4:3 テレビ |
| 縦の表示範囲 | オリジナル |
| 拡大方法 | フィット |
| ワイドの余白 | 黒帯 |
| 解像度 | 1x |
| 色 | トゥルーカラー |
| フィルター | ニアレスト |
| 高速化 | オフ |
| Motion display rate | 60 Hz |

F11 または Alt+Enter でフルスクリーンになります。**F1 → 画面 → Advanced** には、ジオメトリ（Geometry）、Display lock、VSync待機（VSync wait）があります。ジオメトリの項目は、サブピクセル精度（Subpixel geometry）、パースペクティブ補正（Perspective textures）、Restore thin surfaces、面のすき間を閉じる（Close surface gaps）です。
