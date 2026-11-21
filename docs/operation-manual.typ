// ラジコンカー (Pico 2 W) 運用マニュアル。印刷用 (A4)。
// 材料: README.md / CLAUDE.md / docs/bt-core-design.md (port/new-kernel-bt-20261120)、
//       rc-main README「予備ファーム」節、Shizuku tools/ble_ota_send.py の argparse。
// 材料に無いことは書かない。実機で確かめていないことは「未検証」と明記する。

#set page(
  paper: "a4",
  margin: (x: 18mm, y: 16mm),
  numbering: "1 / 1",
  footer: context [
    #set text(8pt, fill: gray)
    ラジコンカー 運用マニュアル #h(1fr) #counter(page).display("1 / 1", both: true)
  ],
)
#set text(font: ("Hiragino Kaku Gothic ProN",), size: 10pt, lang: "ja")
#set par(leading: 0.7em, justify: false)
#set heading(numbering: "1.1")
#show heading.where(level: 1): it => {
  v(0.8em)
  text(14pt, weight: "bold", it)
  v(0.2em)
}
#show heading.where(level: 2): it => {
  v(0.4em)
  text(11pt, weight: "bold", it)
  v(0.1em)
}
#show raw: set text(font: ("Andale Mono", "Hiragino Kaku Gothic ProN"), size: 8.5pt)
#show raw.where(block: true): it => block(
  fill: luma(240), inset: 6pt, radius: 2pt, width: 100%, it,
)
#set table(stroke: 0.5pt + gray, inset: 5pt)

#let warnbox(body) = block(
  stroke: 1.2pt + red, inset: 8pt, radius: 2pt, width: 100%, fill: rgb("#fff0f0"), body,
)
#let notebox(body) = block(
  stroke: 0.8pt + gray, inset: 8pt, radius: 2pt, width: 100%, body,
)
#let unverified = text(fill: rgb("#b45f00"), weight: "bold")[【未検証】]

#align(center)[
  #text(20pt, weight: "bold")[ラジコンカー (Pico 2 W) 運用マニュアル]
  #v(2pt)
  #text(10pt)[Shizuku カーネル版と予備ファームの使い分け・書き込み・BLE OTA・困ったとき]
]

#v(4pt)
#outline(title: [目次], depth: 1, indent: 0pt)

#pagebreak()

= まず読む: 2 つのファームと「戻り方」

この車には 2 種類のファームがあります。*動かなくなったら (A) 予備ファームに戻す* のが基本です。

#table(
  columns: (auto, 1fr, 1fr),
  align: (left, left, left),
  [], [*(A) 予備ファーム*], [*(B) 新カーネル版*],
  [置き場所], [`main` ブランチの `prebuilt/car_fallback_9fc34f7.uf2`], [このリポジトリ (ブランチ `port/new-kernel-bt-20261120`) をビルド],
  [コントローラ接続], [*起動のたびに* コントローラのシンクボタンを押してペアリング (保存した鍵を使わない)], [シンク不要で再接続 (保存済みの相手へ、無ければ既定アドレスへ)],
  [コントローラの登録し直し], [電源を入れ直してシンクボタン], [起動後に BOOTSEL を 3 秒押し 10 秒前に離す (その後シンクボタンも必要) #unverified],
  [BLE OTA], [なし], [あり (無線でファーム更新)],
  [安全性], [いちばん安全 (2026 年 3 月時点で遊べていた仕様。2026-11-20 に車へ書き込んだ版)], [#unverified *実機で一度も動かしていない*。ビルドまでは通した],
)

#v(4pt)
#warnbox[
  *動かなくなったら*: (B) のデバッグに深入りせず、まず (A) を書き戻す (第 3 章)。
  (B) を書いたあとで (A) に戻すときは、書く前に `picotool erase -a` でフラッシュを全部消すと確実です (rc-main README)。
]

#v(4pt)
#notebox[
  *この手順書で「#unverified」と付けた箇所* は、書いた人が実機で確かめていません。
  動かして確かめた結果は、第 7 章のログで示してください。
]

= 日常の遊び方

#notebox[
  電源の入れ方 (電池の種類・スイッチの位置など) は、手元の材料に記載がありません。車の実物で確認し、ここに書き足してください。
]

== (B) 新カーネル版のとき #unverified

+ 車の電源を入れる。*電源投入時に BOOTSEL ボタンを押さない* (USB ブートローダになり、車が動きません)。
+ 起動時、保存済みのコントローラがあればそこへ、無ければ既定アドレスのコントローラへ自動で接続しにいく。
+ LED (中央 4 灯) は、琥珀の点滅 = コントローラ探索中、白 = 接続中、青 = コントローラ接続済み。
+ 青になったら走らせる。
+ 車は、走行中に入力が 0.5 秒途切れるとモーターを止める。

== (A) 予備ファームのとき

+ 車の電源を入れるたびに、コントローラ側のシンクボタンを押してペアリングする (保存した鍵を使わないため、毎回必要)。
+ 速度段については材料に記載がありません (下の速度段は (B) の仕様です)。

== 速度 ((B) の仕様 #unverified)

#table(
  columns: (auto, 1fr),
  align: (left, left),
  [*段*], [*最大速度に対する割合*],
  [1/2 (既定)], [起動直後はこの段。],
  [3/4], [十字キー上を 1 回押す。],
  [1], [さらに十字キー上を 1 回押す (最大速度)。],
)

十字キーの下を押すと一段下がります。段は 1/2 → 3/4 → 1 の 3 つです。

== コントローラを替えたい (B のみ) #unverified

+ 車の電源を入れ、起動が終わるのを待つ。
+ *BOOTSEL ボタンを 3 秒押し、琥珀に点灯したら 10 秒になる前に離す*。保存済みの鍵が全部消え、30 秒間コントローラを探す (LED は琥珀の点滅)。
+ 30 秒以内に、新しいコントローラ側のシンクボタンを押す (BOOTSEL ではシンクの代わりにならない)。
+ 最初に見つかったゲームパッドの相手へ接続する。LED が青になれば完了。
+ 見つからなければ、既定アドレスへの接続に戻る。

BLE の解錠は、起動後 BOOTSEL を 10 秒押し続ける。青く点滅し、3 分間有効な新規ペアリングの一回券が発行される。離してもコントローラの登録し直しは始まらない。続けて PC から車へペアリングを始めると、LED が *紫に点滅* する。そこで *BOOTSEL を短く押して離す* と承認される (長押しは 3 秒 / 10 秒の操作と区別するため、承認待ち中は無効)。USB をつないでいれば番号を照合できる。つないでいなければ番号確認なしの承認になる (物理ボタンを押せる人を信頼する)。#unverified

注意: 保存できる相手は同時に 2 台までです。別の機種を試すと古い鍵が押し出されます。

= ファームの書き込み (USB)

初回、または OTA が使えないときの基本の書き方です。必要なもの: `picotool` (macOS では `/opt/picotool/bin/picotool`)。

== (B) 新カーネル版を書く

必要なもの: `bazelisk`。ビルドは必ず `--config=rp2350_pico2_w` を付けます (付けないと別の構成になり壊れます)。

```
bazelisk build --config=rp2350_pico2_w //firmware_bazel:car_uf2
# できるもの: bazel-bin/firmware_bazel/car.uf2
```

+ Pico の *BOOTSEL ボタンを押したまま* USB を挿す (ドライブ `RP2350` が出る)。
+ 次を実行する。

  ```
  picotool load -v -x bazel-bin/firmware_bazel/car.uf2
  ```

+ Pico を複数台つないでいるときは、*ファイル名の後ろ* に `--ser` を付ける。シリアルは `picotool info -d` で調べる。

  ```
  picotool load -v -x bazel-bin/firmware_bazel/car.uf2 --ser E66...
  ```

+ 電源投入時に BOOTSEL を押しっぱなしにしない (書き込み時の USB 挿しは除く)。

== (A) 予備ファームを書き戻す

`rc-main` (main ブランチ) のクローンで行います。

+ `git pull` で最新を取る。
+ 念のためファイルを確認する。

  ```
  shasum -a 256 -c prebuilt/car_fallback_9fc34f7.uf2.sha256
  ```

+ Pico の *BOOTSEL ボタンを押したまま* USB を挿す (ドライブ `RP2350` が出る)。
+ 次を実行する (複数台のときは末尾に `--ser <シリアル>`)。

  ```
  picotool load -x -v prebuilt/car_fallback_9fc34f7.uf2
  ```

  picotool が無ければ、出てきたドライブに uf2 をドラッグしてもよい。
+ (B) を書いたあとなら、手順 3 の前に `picotool erase -a` で全消去してから書く。

= BLE OTA (無線でファーム更新) #unverified

*(B) 新カーネル版だけ* の機能です。車への OTA の組み込みは実機で動かしていません。

#warnbox[
  *OTA 中 (特に commit 後) は、電源も電池も USB も抜かない。* 抜いて起動しなくなったら、*USB で書き直すしかありません* (第 3 章)。
  この車のフラッシュには A/B 面が無く、これが正規の復旧手順です (運用上の決定)。
]

== 準備

- 必要なもの: Python 3 と `bleak` (`pip install bleak`)。送信ツールは macOS / Linux のみ。
- 送信ツールは Shizuku リポジトリ (https://github.com/danfunc/Shizuku) の `tools/ble_ota_send.py`。
  XNO 側の同名ツールは古く CDC 承認が無いので使わない。以下 `<Shizuku>` はその clone の場所。
- OTA に渡すのは *`car.bin`* (UF2 でも ELF でもない)。

```
bazelisk build --config=rp2350_pico2_w //firmware_bazel:car_bin
# できるもの: bazel-bin/firmware_bazel/car.bin
```

== 手順

+ 車が見つかるか確認する (広告名は "Shizuku UART")。

  ```
  python3 <Shizuku>/tools/ble_ota_send.py scan
  ```

+ *初回* (その PC と車がまだ BLE ボンドしていない): 車を *USB で PC につないだまま* 送る。CDC 経由でペアリングを承認させる。
  `--cdc-port` は macOS なら `/dev/cu.usbmodem*`。

  ```
  python3 <Shizuku>/tools/ble_ota_send.py send bazel-bin/firmware_bazel/car.bin --commit --cdc-port /dev/cu.usbmodemXXXX
  ```

+ *2 回目以降* (ボンド済みの同じ PC): USB は不要。無線だけで送る。無線だけで通るかは実機未確認。

  ```
  python3 <Shizuku>/tools/ble_ota_send.py send bazel-bin/firmware_bazel/car.bin --commit
  ```

+ *`--commit` を付け忘れない*。付けないと、車の staging 領域に置いただけで止まる (`done: N bytes crc=... OK (staged at 0x...)` と出る)。
  commit だけを後から単独で送れるかは未確認なので、基本は 1 回で `--commit` まで通す。

== 完了の確認

+ ツールのログに `done: ... OK`、続いて `commit: ... no return` が出る。
+ 車が再起動する (USB CDC が一度消えて戻る。BLE が再び "Shizuku UART" で広告する)。
+ CDC ログの `[CAR] build: <日付> <時刻>` が、新しくビルドしたものになっている。
  「ツールが成功と言った」だけでは足りません。起動後のログで新しい像を確認します。

== `send` の主な引数 (`ble_ota_send.py` の argparse より)

#table(
  columns: (auto, 1fr),
  align: (left, left),
  [*引数*], [*意味 (既定値)*],
  [`bin`], [送る生バイナリ (.bin)。必須。],
  [`--mode {xnor,xnou}`], [送信モード (既定: xnor)],
  [`--commit`], [転送成功後に XNOC でフラッシュ反映と再起動を実行],
  [`--address`], [対象デバイスの BLE アドレス (省略時は自動探索)],
  [`--cdc-port`], [ペアリング承認に使うデバイスの CDC ポート],
  [`--cdc-mode {nc,jw}`], [CDC ペアリング方式 (既定: nc。jw は接続前に A を送信)],
  [`--no-reconnect`], [切断時の自動再接続を無効化],
  [`--packet-delay`], [パケット間スリープ秒 (既定値はツール内の定数)],
  [`--chunk-gap`], [1 チャンク完了後の flash 書き込み待ち秒 (既定値はツール内の定数)],
  [`--window-size`], [同期ウィンドウのチャンク数 (既定 0 = 全チャンク送出後に QUERY)],
  [`--response`], [チャンクデータ送信に Write with Response を使用],
  [`--packet-size`], [1 パケットのバイト数 (既定 244)],
  [`--max-rounds`], [再送ラウンド最大数 (既定 15)],
  [`--ready-timeout`], [INIT 後の ready 応答待ち秒 (既定値はツール内の定数)],
)

= やってはいけないこと

#warnbox[
  + *PC 側で、車の Bluetooth 登録を削除しない* (macOS の「このデバイスを削除」など)。消すと BLE のペアリング施錠で車に入れなくなり、OTA ができなくなります。
  + *OTA 中 (特に commit 後) に電源・電池・USB を抜かない。* 抜いたら USB で書き直すしかありません。
  + *電源投入時に BOOTSEL を押さない。* USB ブートローダになります (ファーム書き込み時を除く)。
  + 施錠 (BLE のペアリング施錠) を外したり弱めたりしない。施錠は、鍵の押し出し (乗っ取りと OTA 喪失) を防ぐ仕組みです。
]

= 困ったとき (症状 → 対処)

#set table(inset: 4pt)
#table(
  columns: (22%, 1fr),
  align: (left, left),
  [*症状*], [*対処*],
  [コントローラにつながらない \ ((B) #unverified)],
  [
    + 電源投入時に BOOTSEL を押していないか確認する。
    + 起動後 BOOTSEL を 3 秒押し、10 秒前に離す → 30 秒以内にコントローラのシンクボタン。LED が琥珀に点滅 = 探索中、青 = 接続済み。
    + 見つからないと既定アドレス (`REMOTE_BD_ADDR`) への接続に戻る。コントローラが既定の相手か不明な場合は上の手順で登録し直す。
    + ログに `[BT_CLASSIC] scan done reason=N` (0 時間切れ・1 停止・2 開始失敗) が出ているか見る (第 7 章)。
    + 直らなければ予備ファーム (A) に戻す。
  ],
  [つながらない \ ((A) 予備ファーム)],
  [電源を入れるたびにシンクボタンが必要な仕様。入れ直してシンクボタンを押す。それでもだめなら、材料に追加の手順は無い。],
  [走らない],
  [
    + LED が青 (接続済み) か確認する。
    + 車は入力が 0.5 秒途切れるとモーターを止める (入力が届き続けているか、ログで確認する)。
    + 速度段は既定 1/2。十字キー上で 3/4・1 に上がる。
    + ログの `[CAR] object startup failures=N` が 0 でなければオブジェクト登録の失敗 (第 7 章)。
    + 直らなければ予備ファーム (A) に戻す。
  ],
  [OTA が途中で止まる],
  [
    + ツールは自動で再接続し続きを送る (`--no-reconnect` で無効にできる)。待つ。
    + 何度も止まるなら `--packet-delay` を伸ばして再実行する。
    + 車の電源・電池は *抜かない*。
    + `--commit` 前なら再実行すればよい。commit 中に落ちたら「起動しない」の行へ。
  ],
  [起動しない \ (OTA の commit 中の電断を含む)],
  [
    + *USB で書き直す* (第 3 章)。これが正規の復旧。
    + それでも戻らなければ、予備ファーム (A) を書く (第 3.2 節)。
    + 必要なら `picotool erase -a` で全消去してから書く。#unverified
  ],
  [BLE で入れない \ ("Shizuku UART" が見えない、OTA が拒否される)],
  [
    + 起動後 BOOTSEL を 10 秒押し続けると、青く点滅し、3 分間有効な新規ペアリングの一回券が発行される。解錠も承認も USB シリアルは不要。10 秒後に PC からペアリングし、LED が紫に点滅したら BOOTSEL を短く押して離す。USB をつないでいれば番号を照合できる。つないでいなければ番号確認なしの承認になる (ボタンを押せる人を信頼する)。#unverified
    + 原因の第一候補: PC 側で車の Bluetooth 登録を消した (ペアリング施錠で拒否される)。
    + 車を USB でつなぎ、USB シリアルに `PAIR ALLOW` と打って改行する。3 分間だけ新しいペアリングを 1 回受け付けるので、その間に PC からつなぎ直す。#unverified
    + それでもだめなら、USB シリアルに `PAIR FORGET!` と打って改行する (保存済みの BLE ボンドを全部消す)。#unverified
    + どちらもログに `[BLE_UART] CDC ...` の 1 行が出る。
    + それでも戻らなければ: `picotool erase -a` でフラッシュを全消去 → USB で書き直す。BLE のボンドも Classic の鍵も消える。#unverified
    + 最終手段は予備ファーム (A)。
    + bonding 領域だけを消す方法は、アドレスが未確認のため、この手順書では示さない。
  ],
  [新しいコントローラが登録されない \ ((B) #unverified)],
  [
    + BOOTSEL を 3 秒押し、琥珀に点灯したら 10 秒になる前に離す。
    + 30 秒以内にシンクボタンを押す。
    + コントローラの種類によっては見つからない可能性がある。この車は Switch Pro 互換機を想定している。
  ],
)

*最終手段*: 予備ファーム (A) を書き戻す。直前に (B) を書いていたなら `picotool erase -a` のあとで書く。

= 付録: ログの見方

車を USB で PC につなぐと、USB シリアル (CDC) にログが出ます (macOS ではポートは `/dev/cu.usbmodem*`)。
ログを表示するソフトは、材料に記載がありません。以下の行は *(B) 新カーネル版* のものです。

#table(
  columns: (auto, 1fr),
  align: (left, left),
  [*行の頭*], [*意味*],
  [`[CAR] build:`], [起動した像のビルド時刻。OTA 後に新しい時刻になっていれば更新できている。],
  [`[CAR] object startup failures=N`], [0 でなければオブジェクト登録の失敗。],
  [`[CAR] BOOTSEL read unavailable`], [BOOTSEL 機能が無効 (ビルドのフラグか gpio object の問題)。],
  [`[CAR] bond ...` / `scan found ...`], [保存済みの相手の列挙 / 探索で見つけた相手。],
  [`[BT_CLASSIC] hid_host_connect rc=0x..`], [コントローラへ接続を試みた結果。],
  [`[BT_CLASSIC] HID opened`], [コントローラと HID 接続できた。],
  [`[BT_CLASSIC] next connect in N ms`], [接続の再試行を待っている。],
  [`[BT_CLASSIC] incoming declined`], [対象以外の相手からの接続を拒否した。],
  [`[BT_CLASSIC] scan done reason=N`], [探索の終了。0 = 時間切れ、1 = 停止、2 = 開始失敗。],
  [`[BT_CLASSIC] forgot all link keys`], [保存済みの鍵を全消去した (登録し直し)。],
  [`[BT_CLASSIC] auth failed .., dropped link key`], [認証に失敗したので対象の鍵を捨てた。],
  [`[BLE_UART] PAIR? conn=.. mode=NC num=NNNNNN`], [BLE ペアリングの承認待ち (CDC で `Y NNNNNN` を返す。`--cdc-port` を渡せばツールが自動で返す)。],
  [`[BLE_UART] pairing DECLINED (locked, ...)`], [施錠のためペアリングを拒否した。],
  [`[BLE_UART] authorized via CDC`], [CDC 経由で承認された。],
  [`[BLE_UART] ota write dropped (unauthorized ...)`], [未認可のリンクからの OTA 書き込みを捨てた。],
  [`[OTA] registered ...` / `[OTA] input slot ... <- stream ...`], [OTA が結線できた印。],
  [`[CAR] register_ota failed`], [OTA だけ無効になっている。],
)

= 材料に無くて書けなかった点

- 車の電源の入れ方 (電池の種類・スイッチの位置・充電)。
- (A) 予備ファームの速度段 (十字キーでの変更があるか)。
- USB シリアルのログを表示するソフトの指定。
- `picotool erase -a` のあとに (B) を書いたときの動作の確認結果。
- bonding 領域だけを消す具体的なアドレス (未確認)。
