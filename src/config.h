#ifndef CONFIG_H
#define CONFIG_H
/* 利用者数の集計サーバー (任意)。
 *   STATS_URL : 例 "https://stats.example.com"  (末尾の / は付けない。空なら集計は完全に無効)
 *   STATS_KEY : サーバー側 STATS_CLIENT_KEY と同じ文字列 (空でも可)
 * ビルド時にここを書き換えるか、%LOCALAPPDATA%\NorthC\settings.ini の [telemetry] url= / key= で上書きできる。 */
#define STATS_URL ""
#define STATS_KEY ""
#define APP_VERSION "0.6"
#endif
