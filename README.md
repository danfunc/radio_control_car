機能とオーバーヘッドを削った組み込み向けのカーネルにファイルシステムなどの汎用高機能レイヤーを積み重ねることで、組み込み環境と汎用環境の双方でコードベースを共通化することを目指す自作OS「Shizuku」のデモンストレーション用コード

Bluetoothコントローラーの登録方法はCMakeLists.txtを参照してください。

## Shizuku V3 上の Bazel ビルド

依存先は Shizuku V3 (`~/github/Shizuku-v3`、branch `refactor/arch-split-v3`、commit 3369558) です。
`MODULE.bazel` の `git_override` が指す commit はリモートにまだ無いため、
ローカル作業ツリーを指す `.bazelrc.user` の `--override_module=shizuku=...` が前提です。

既存の CMake ビルドと並存する移植版は `firmware_bazel/` にあります。
`.bazelrc.user` でローカルの Shizuku 作業ツリーを指定してから、
`bazelisk build //firmware_bazel:car.elf` を実行します。
Bluetooth Classic の接続先アドレスは `firmware_bazel/classic_hid.cpp` の
`REMOTE_BD_ADDR` で指定します。既定値は元の CMake ビルドと同じです。

UF2 は `bazelisk build //firmware_bazel:car_uf2` で作ります
(`bazel-bin/firmware_bazel/car.uf2`、ホストの `/opt/picotool` を使います)。
