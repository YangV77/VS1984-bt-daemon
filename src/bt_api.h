// src/bt_api.h
#ifndef VS_BT_API_H
#define VS_BT_API_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

typedef struct BtHandle BtHandle;

// 状态枚举（简化版）
typedef enum BtState {
    BT_STATE_UNKNOWN = 0,
    BT_STATE_DOWNLOADING,
    BT_STATE_SEEDING,
    BT_STATE_PAUSED,
    BT_STATE_FINISHED,
    BT_STATE_ERROR
} BtState;

typedef struct BtTorrentStatus {
    BtState state;
    float   progress;           // 0.0 ~ 1.0
    int     download_rate;      // bytes/sec
    int     upload_rate;        // bytes/sec
    long    total_downloaded;
    long    total_uploaded;
    int     num_peers;
    int     num_seeds;
    int     num_leechers;
    int     is_seeding;
    int     has_metadata;
    int     error_code;
    char    error_msg[128];
} BtTorrentStatus;

// 初始化 & 关闭
BtHandle* bt_init(const char* config_path);
void      bt_shutdown(BtHandle* handle);

// 添加磁力链接
// out_infohash_hex: 输出 40 字节 SHA1 hex（含 '\0' 至少 41 字节）
int bt_add_magnet(BtHandle* handle,
                  const char* magnet_uri,
                  const char* save_dir,
                  char* out_infohash_hex,
                  size_t out_len);

// 添加 torrent 文件
int bt_add_torrent_file(BtHandle* handle,
                        const char* torrent_path,
                        const char* save_dir,
                        char* out_infohash_hex,
                        size_t out_len);

// 做种（从目录创建 .torrent 并做种）
int bt_seed_folder(BtHandle* handle,
                   const char* folder,
                   const char* torrent_out_path,
                   char* out_infohash_hex,
                   size_t out_len);


// 控制
int bt_pause_torrent(BtHandle* handle, const char* infohash_hex);
int bt_resume_torrent(BtHandle* handle, const char* infohash_hex);
int bt_remove_torrent(BtHandle* handle, const char* infohash_hex, int remove_files);

// 查询状态
int bt_get_torrent_status(BtHandle* handle,
                          const char* infohash_hex,
                          BtTorrentStatus* out_status);

int bt_resume_all_torrents(BtHandle *handle,
                       const char *bt_dir,
                       const char *save_path);

#ifdef __cplusplus
}
#endif

#endif // VS_BT_API_H
