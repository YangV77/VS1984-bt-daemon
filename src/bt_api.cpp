// src/bt_api.c
#include "bt_api.h"
#include "bt_core.hpp"
#include <stdlib.h>
#include <string.h>

struct BtHandle {
    BtCore* core;
};

BtHandle* bt_init(const char* config_path)
{
    (void)config_path;
    BtHandle* h = (BtHandle*)calloc(1, sizeof(BtHandle));
    if (!h) return NULL;
    h->core = new BtCore();
    if (!h->core->init(config_path ? config_path : "")) {
        delete h->core;
        free(h);
        return NULL;
    }
    return h;
}

void bt_shutdown(BtHandle* handle)
{
    if (!handle) return;
    if (handle->core) {
        handle->core->shutdown();
        delete handle->core;
    }
    free(handle);
}

static int check_out_buf(char* out_infohash_hex, size_t out_len)
{
    if (!out_infohash_hex || out_len < 41) {
        return -1;
    }
    out_infohash_hex[0] = '\0';
    return 0;
}

int bt_add_magnet(BtHandle* handle,
                  const char* magnet_uri,
                  const char* save_dir,
                  char* out_infohash_hex,
                  size_t out_len)
{
    if (!handle || !handle->core || !magnet_uri || !save_dir) return -1;
    if (check_out_buf(out_infohash_hex, out_len) != 0) return -1;

    std::string info;
    bool ok = handle->core->addMagnet(magnet_uri, save_dir, info);
    if (!ok) return -1;

    strncpy(out_infohash_hex, info.c_str(), out_len - 1);
    out_infohash_hex[out_len - 1] = '\0';
    return 0;
}

int bt_add_torrent_file(BtHandle* handle,
                        const char* torrent_path,
                        const char* save_dir,
                        char* out_infohash_hex,
                        size_t out_len)
{
    if (!handle || !handle->core || !torrent_path || !save_dir) return -1;
    if (check_out_buf(out_infohash_hex, out_len) != 0) return -1;

    std::string info;
    bool ok = handle->core->addTorrentFile(torrent_path, save_dir, info);
    if (!ok) return -1;

    strncpy(out_infohash_hex, info.c_str(), out_len - 1);
    out_infohash_hex[out_len - 1] = '\0';
    return 0;
}

int bt_seed_folder(BtHandle* handle,
                   const char* folder,
                   const char* torrent_out_path,
                   char* out_infohash_hex,
                   size_t out_len)
{
    if (!handle || !handle->core || !folder || !torrent_out_path) return -1;
    if (check_out_buf(out_infohash_hex, out_len) != 0) return -1;

    std::string info;
    bool ok = handle->core->seedFolder(folder, torrent_out_path, info);
    if (!ok) return -1;

    strncpy(out_infohash_hex, info.c_str(), out_len - 1);
    out_infohash_hex[out_len - 1] = '\0';
    return 0;
}

int bt_pause_torrent(BtHandle* handle, const char* infohash_hex)
{
    if (!handle || !handle->core || !infohash_hex) return -1;
    bool ok = handle->core->pauseTorrent(infohash_hex);
    return ok ? 0 : -1;
}

int bt_resume_torrent(BtHandle* handle, const char* infohash_hex)
{
    if (!handle || !handle->core || !infohash_hex) return -1;
    bool ok = handle->core->resumeTorrent(infohash_hex);
    return ok ? 0 : -1;
}

int bt_remove_torrent(BtHandle* handle, const char* infohash_hex, int remove_files)
{
    if (!handle || !handle->core || !infohash_hex) return -1;
    bool ok = handle->core->removeTorrent(infohash_hex, remove_files != 0);
    return ok ? 0 : -1;
}

int bt_get_torrent_status(BtHandle* handle,
                          const char* infohash_hex,
                          BtTorrentStatus* out_status)
{
    if (!handle || !handle->core || !infohash_hex || !out_status) return -1;
    memset(out_status, 0, sizeof(*out_status));
    bool ok = handle->core->getStatus(infohash_hex, *out_status);
    return ok ? 0 : -1;
}

int bt_resume_all_torrents(BtHandle *handle,
                           const char *bt_dir,
                           const char *save_path)
{
    if (!handle || !bt_dir || !save_path) {
        fprintf(stderr, "[bt] bt_resume_all_torrents: invalid argument\n");
        return -1;
    }

    DIR *dir = opendir(bt_dir);
    if (!dir) {
        perror("[bt] opendir bt_dir failed");
        return -1;
    }

    struct dirent *ent;
    int resumed_count = 0;
    char path[PATH_MAX];

    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }

        const char *name = ent->d_name;
        size_t len = strlen(name);
        const char *suffix = ".torrent";
        size_t suffix_len = strlen(suffix);

        if (len <= suffix_len) {
            continue;
        }
        if (strcmp(name + (len - suffix_len), suffix) != 0) {
            continue;
        }

        int n = snprintf(path, sizeof(path), "%s/%s", bt_dir, name);
        if (n <= 0 || (size_t)n >= sizeof(path)) {
            fprintf(stderr, "[bt] path too long, skip: %s/%s\n", bt_dir, name);
            continue;
        }

        struct stat st;
        if (stat(path, &st) != 0) {
            perror("[bt] stat failed");
            continue;
        }
        if (!S_ISREG(st.st_mode)) {

            continue;
        }

        char infohash[64] = {0};
        int ret = bt_add_torrent_file(handle,
                                      path,
                                      save_path,
                                      infohash,
                                      sizeof(infohash));
        if (ret == 0) {
            resumed_count++;
            printf("[bt] resume seed from torrent: %s\n", path);
            printf("[bt]   infohash = %s\n", infohash);
        } else {
            fprintf(stderr, "[bt] failed to add torrent file: %s (ret=%d)\n", path, ret);
        }
    }

    closedir(dir);
    return resumed_count;
}