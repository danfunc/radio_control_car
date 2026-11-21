機能とオーバーヘッドを削った組み込み向けのカーネルにファイルシステムなどの汎用高機能レイヤーを積み重ねることで、組み込み環境と汎用環境の双方でコードベースを共通化することを目指す自作OS「Shizuku」のデモンストレーション用コード

> **注意: 実機未検証の機能があります。** BOOTSEL 長押しによるコントローラの登録し直しと、車への BLE OTA の
> 組み込みは、ビルドまで確認したものの実機では動かしていません。動かして確かめた結果は
> ログで示してください。AI エージェントを使う場合は `CLAUDE.md` を読ませてください。
> 印刷用の運用マニュアル (Typst): [docs/operation-manual.typ](docs/operation-manual.typ)

## ビルドと書込み (Pico 2 W)

必要なもの: `bazelisk`、`picotool` (macOS は `/opt/picotool`)、OTA を使うなら Python 3 + `bleak`。

```
bazelisk build --config=rp2350_pico2_w //firmware_bazel:car_uf2   # → bazel-bin/firmware_bazel/car.uf2
```

初回だけ USB で書き込みます。BOOTSEL を押しながら USB を挿し、

```
picotool load -v -x bazel-bin/firmware_bazel/car.uf2     # 複数台なら末尾に --ser <シリアル>
```

依存の Shizuku / XNO は `MODULE.bazel` の `git_override` (commit 固定) で取得されます。
手元の作業ツリーを使うときは、gitignore 済みの `.bazelrc.user` に
`common --override_module=shizuku=../Shizuku` / `common --override_module=xno=/path/to/XNO` を書きます。
## コントローラの登録

Bluetooth Classic の Switch Pro 互換コントローラを使います。起動時は保存済みの相手へ、無ければ既定の
`REMOTE_BD_ADDR` (`firmware_bazel/classic_hid.cpp`) へ接続します。別のコントローラへ替えるときは、
**起動後に BOOTSEL を 3 秒長押し** してから、コントローラ側のシンクボタンを押します (30 秒以内)。
LED の琥珀の点滅が探索中、青が接続済みです。電源投入時に BOOTSEL を押すと USB ブートローダになるので注意。

## OTA (BLE でファームを更新)

```
bazelisk build --config=rp2350_pico2_w //firmware_bazel:car_bin    # → car.bin (OTA はこちら)
python3 <Shizuku>/tools/ble_ota_send.py scan
python3 <Shizuku>/tools/ble_ota_send.py send bazel-bin/firmware_bazel/car.bin --commit --cdc-port /dev/cu.usbmodemXXXX
```

`<Shizuku>` は <https://github.com/danfunc/Shizuku> の clone です。初回は PC と BLE のボンドを作るため、車を
USB (CDC) でつないだ状態で行います。**更新中 (特に commit 後) は電源を切らない**でください。失敗して起動しない
ときは USB で書き直します。PC 側で車のボンドを消すと BLE から入れなくなります。詳細と復旧手順は `CLAUDE.md` を見てください。
