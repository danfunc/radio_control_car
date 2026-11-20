/* XNO firmware/rp2350_flash_limit.ld (16772bf) と同じ内容。
 * OTA は走っているファームを「自分より新しい自分」で上書きするので、像が
 * 1 MiB の本体区画 (flash_map.hpp) を越えると FS / ステージングに食い込む。
 * リンク時に落として、越えた像を作らせない。 */
ASSERT((__flash_binary_end - 0x10000000) <= __shizuku_firmware_limit,
       "Shizuku firmware exceeds the configured RP2350 partition")
ASSERT((__flash_binary_end - 0x10000000) <= __shizuku_logical_flash_limit,
       "Shizuku firmware exceeds the configured logical RP2350 flash limit")
