// vs1984-bt-daemon.c

static BtHandle* bt_instance = NULL;

int bt_core_init(const char *config_path) {
    if (bt_instance == NULL) {
        bt_instance = bt_init(config_path);
        if (bt_instance == NULL) {
            return 1;
        }
    }

    return 0;
}

void bt_core_shutdown(void) {
    if (bt_instance == NULL) {
        return;
    }
    bt_shutdown(bt_instance);
}

int bt_core_add_magnet(const char *magnet_uri,
                       const char *save_dir,
                       char *out_infohash_hex,
                       size_t out_len)
{
    return bt_add_magnet(bt_instance,magnet_uri, save_dir, out_infohash_hex, out_len);
}

int bt_core_add_torrent_file(const char *torrent_path,
                             const char *save_dir,
                             char *out_infohash_hex,
                             size_t out_len)
{
    return bt_add_torrent_file(bt_instance, torrent_path, save_dir, out_infohash_hex, out_len);
}

int bt_core_seed_folder(const char *folder,
                        const char *torrent_out_path,
                        char *out_infohash_hex,
                        size_t out_len)
{
    return bt_seed_folder(bt_instance, folder, torrent_out_path, out_infohash_hex, out_len);
}

int bt_core_pause(const char *infohash_hex) { return bt_pause_torrent(bt_instance, infohash_hex); }
int bt_core_resume(const char *infohash_hex) { return bt_resume_torrent(bt_instance, infohash_hex); }
int bt_core_remove(const char *infohash_hex, int remove_files) { return bt_remove_torrent(bt_instance, infohash_hex, remove_files); }

int bt_core_get_status(const char *infohash_hex, BtTorrentStatus *st)
{
    return bt_get_torrent_status(bt_instance, infohash_hex, st);
}

int bt_core_resume_all(const char *dir_torrent, const char *dir_data)
{
    return bt_resume_all_torrents(bt_instance, dir_torrent, dir_data);
}

static const char* bt_state_to_string(BtState s) {
    switch (s) {
        case BT_STATE_DOWNLOADING: return "downloading";
        case BT_STATE_SEEDING:     return "seeding";
        case BT_STATE_PAUSED:      return "paused";
        case BT_STATE_FINISHED:    return "finished";
        case BT_STATE_ERROR:       return "error";
        default:                   return "unknown";
    }
}

static char* bt_status_to_result_json(const BtTorrentStatus *st) {
    cJSON *obj = cJSON_CreateObject();

    cJSON_AddStringToObject(obj, "state", bt_state_to_string(st->state));
    cJSON_AddNumberToObject(obj, "progress", st->progress);
    cJSON_AddNumberToObject(obj, "download_rate", st->download_rate);
    cJSON_AddNumberToObject(obj, "upload_rate", st->upload_rate);
    cJSON_AddNumberToObject(obj, "total_downloaded", (double)st->total_downloaded);
    cJSON_AddNumberToObject(obj, "total_uploaded", (double)st->total_uploaded);
    cJSON_AddNumberToObject(obj, "num_peers", st->num_peers);
    cJSON_AddNumberToObject(obj, "num_seeds", st->num_seeds);
    cJSON_AddNumberToObject(obj, "num_leechers", st->num_leechers);
    cJSON_AddNumberToObject(obj, "is_seeding", st->is_seeding);
    cJSON_AddNumberToObject(obj, "has_metadata", st->has_metadata);
    cJSON_AddNumberToObject(obj, "error_code", st->error_code);
    cJSON_AddStringToObject(obj, "error_msg", st->error_msg);

    char *out = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    return out;
}

static int recv_frame(char **buf_out, size_t *len_out) {
    uint32_t len_net;
    ssize_t r = read(STDIN_FILENO, &len_net, 4);
    if (r == 0) return 1;   // EOF: parent closed
    if (r != 4) return -1;

    uint32_t len = ntohl(len_net);
    char *buf = malloc(len + 1);
    if (!buf) return -1;

    size_t readn = 0;
    while (readn < len) {
        r = read(STDIN_FILENO, buf + readn, len - readn);
        if (r <= 0) {
            free(buf);
            return -1;
        }
        readn += r;
    }

    buf[len] = '\0';
    *buf_out = buf;
    *len_out = len;
    return 0;
}

static int send_frame(const char *buf, size_t len) {
    uint32_t len_net = htonl((uint32_t)len);
    if (write(STDOUT_FILENO, &len_net, 4) != 4)
        return -1;
    size_t written = 0;
    while (written < len) {
        ssize_t w = write(STDOUT_FILENO, buf + written, len - written);
        if (w <= 0) return -1;
        written += w;
    }
    return 0;
}

static void send_error_response(int id, int code, const char *msg) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "id", id);
    cJSON_AddStringToObject(root, "status", "error");
    cJSON_AddNullToObject(root, "result");

    cJSON *err = cJSON_CreateObject();
    cJSON_AddNumberToObject(err, "code", code);
    cJSON_AddStringToObject(err, "message", msg);
    cJSON_AddItemToObject(root, "error", err);

    char *json = cJSON_PrintUnformatted(root);
    send_frame(json, strlen(json));

    cJSON_Delete(root);
    free(json);
}

static int parse_request(const char *json,
                         int *out_id,
                         char **out_method,
                         cJSON **out_params)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return -1;

    cJSON *id = cJSON_GetObjectItem(root, "id");
    cJSON *method = cJSON_GetObjectItem(root, "method");
    cJSON *params = cJSON_GetObjectItem(root, "params");

    if (!cJSON_IsNumber(id) || !cJSON_IsString(method) || !cJSON_IsObject(params)) {
        cJSON_Delete(root);
        return -1;
    }

    *out_id = id->valueint;
    *out_method = strdup(method->valuestring);
    *out_params = params;

    return 0;
}

#include "bt_utils.h"
int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    for(int i=1;i<argc;i++){
        if(!strcmp(argv[i], "-d")) set_debug(1);
        else set_debug(0);
    }

    for (;;) {
        char *req = NULL;
        size_t len = 0;

        int rc = recv_frame(&req, &len);
        if (rc == 1) break;
        if (rc != 0) continue;

        int id = 0;
        char *method = NULL;
        cJSON *params = NULL;
        cJSON *root = cJSON_Parse(req);

        if (!root) {
            free(req);
            continue;
        }

        cJSON *id_i = cJSON_GetObjectItem(root, "id");
        cJSON *method_i = cJSON_GetObjectItem(root, "method");
        cJSON *params_i = cJSON_GetObjectItem(root, "params");

        if (!cJSON_IsNumber(id_i) || !cJSON_IsString(method_i) || !cJSON_IsObject(params_i)) {
            send_error_response(0, 400, "bad request");
            cJSON_Delete(root);
            free(req);
            continue;
        }

        id = id_i->valueint;
        method = method_i->valuestring;
        params = params_i;

        if (strcmp(method, "init") == 0) {
            cJSON *p = cJSON_GetObjectItem(params, "config_path");
            const char *config_path = cJSON_IsString(p) ? p->valuestring : "";

            if (bt_core_init(config_path) != 0) {
                send_error_response(id, 500, "init failed");
            } else {
                cJSON *resp = cJSON_CreateObject();
                cJSON_AddNumberToObject(resp, "id", id);
                cJSON_AddStringToObject(resp, "status", "ok");

                cJSON *res = cJSON_CreateObject();
                cJSON_AddStringToObject(res, "version", RSUNX_VERSION);
                cJSON_AddItemToObject(resp, "result", res);
                cJSON_AddNullToObject(resp, "error");

                char *out = cJSON_PrintUnformatted(resp);
                send_frame(out, strlen(out));

                free(out);
                cJSON_Delete(resp);
            }
        }

        else if (strcmp(method, "add_magnet") == 0) {
            const char *magnet = cJSON_GetObjectItem(params, "magnet_uri")->valuestring;
            const char *save   = cJSON_GetObjectItem(params, "save_dir")->valuestring;

            char infohash[64] = {0};
            if (bt_core_add_magnet(magnet, save, infohash, sizeof(infohash)) != 0) {
                send_error_response(id, 500, "add_magnet failed");
            } else {
                cJSON *resp = cJSON_CreateObject();
                cJSON_AddNumberToObject(resp, "id", id);
                cJSON_AddStringToObject(resp, "status", "ok");

                cJSON *res = cJSON_CreateObject();
                cJSON_AddStringToObject(res, "infohash_hex", infohash);
                cJSON_AddItemToObject(resp, "result", res);
                cJSON_AddNullToObject(resp, "error");

                char *out = cJSON_PrintUnformatted(resp);
                send_frame(out, strlen(out));
                free(out);
                cJSON_Delete(resp);
            }
        }

        else if (strcmp(method, "add_torrent_file") == 0) {
            const char *path = cJSON_GetObjectItem(params, "torrent_path")->valuestring;
            const char *save = cJSON_GetObjectItem(params, "save_dir")->valuestring;

            char infohash[64];
            if (bt_core_add_torrent_file(path, save, infohash, sizeof(infohash)) != 0) {
                send_error_response(id, 500, "add_torrent_file failed");
            } else {
                cJSON *resp = cJSON_CreateObject();
                cJSON_AddNumberToObject(resp, "id", id);
                cJSON_AddStringToObject(resp, "status", "ok");

                cJSON *res = cJSON_CreateObject();
                cJSON_AddStringToObject(res, "infohash_hex", infohash);
                cJSON_AddItemToObject(resp, "result", res);
                cJSON_AddNullToObject(resp, "error");
                char *out = cJSON_PrintUnformatted(resp);
                send_frame(out, strlen(out));

                free(out);
                cJSON_Delete(resp);
            }
        }

        else if (strcmp(method, "seed_folder") == 0) {
            const char *folder = cJSON_GetObjectItem(params, "folder")->valuestring;
            const char *out_torrent = cJSON_GetObjectItem(params, "torrent_out_path")->valuestring;

            char infohash[64];
            if (bt_core_seed_folder(folder, out_torrent, infohash, sizeof(infohash)) != 0) {
                send_error_response(id, 500, "seed_folder failed");
            } else {
                cJSON *resp = cJSON_CreateObject();
                cJSON_AddNumberToObject(resp, "id", id);
                cJSON_AddStringToObject(resp, "status", "ok");

                cJSON *res = cJSON_CreateObject();
                cJSON_AddStringToObject(res, "infohash_hex", infohash);
                cJSON_AddItemToObject(resp, "result", res);
                cJSON_AddNullToObject(resp, "error");
                char *out = cJSON_PrintUnformatted(resp);
                send_frame(out, strlen(out));

                free(out);
                cJSON_Delete(resp);
            }
        }

        else if (strcmp(method, "pause_torrent") == 0) {
            const char *ih = cJSON_GetObjectItem(params, "infohash_hex")->valuestring;
            if (bt_core_pause(ih) != 0) {
                send_error_response(id, 500, "pause failed");
            } else {
                const char *out = "{\"id\":%d,\"status\":\"ok\",\"result\":{},\"error\":null}";
                char buf[256];
                snprintf(buf, sizeof(buf), out, id);
                send_frame(buf, strlen(buf));
            }
        }

        else if (strcmp(method, "resume_torrent") == 0) {
            const char *ih = cJSON_GetObjectItem(params, "infohash_hex")->valuestring;
            if (bt_core_resume(ih) != 0) {
                send_error_response(id, 500, "resume failed");
            } else {
                const char *out = "{\"id\":%d,\"status\":\"ok\",\"result\":{},\"error\":null}";
                char buf[256];
                snprintf(buf, sizeof(buf), out, id);
                send_frame(buf, strlen(buf));
            }
        }

        else if (strcmp(method, "remove_torrent") == 0) {
            const char *ih = cJSON_GetObjectItem(params, "infohash_hex")->valuestring;
            int rm = cJSON_GetObjectItem(params, "remove_files")->valueint;

            if (bt_core_remove(ih, rm) != 0) {
                send_error_response(id, 500, "remove failed");
            } else {
                const char *out = "{\"id\":%d,\"status\":\"ok\",\"result\":{},\"error\":null}";
                char buf[256];
                snprintf(buf, sizeof(buf), out, id);
                send_frame(buf, strlen(buf));
            }
        }

        else if (strcmp(method, "get_torrent_status") == 0) {
            const char *ih = cJSON_GetObjectItem(params, "infohash_hex")->valuestring;

            BtTorrentStatus st;
            if (bt_core_get_status(ih, &st) != 0) {
                send_error_response(id, 500, "status failed");
            } else {
                char *res_json = bt_status_to_result_json(&st);
                if (!res_json) {
                    send_error_response(id, 500, "internal error");
                } else {
                    char *resp = NULL;
                    asprintf(&resp,
                        "{\"id\":%d,\"status\":\"ok\",\"result\":%s,\"error\":null}",
                        id, res_json);
                    if (resp) {
                        send_frame(resp, strlen(resp));
                        free(resp);
                    }
                    free(res_json);
                }
            }
        }

        else if (strcmp(method, "resume_all_torrents") == 0) {
            const char *dir_t = cJSON_GetObjectItem(params, "torrents_dir")->valuestring;
            const char *dir_d = cJSON_GetObjectItem(params, "data_dir")->valuestring;

            int count = bt_core_resume_all(dir_t, dir_d);

            char *resp;
            asprintf(&resp,
                "{\"id\":%d,\"status\":\"ok\",\"result\":{\"resumed_count\":%d},\"error\":null}",
                id, count);
            if (resp) {
                send_frame(resp, strlen(resp));
                free(resp);
            }
        }

        else if (strcmp(method, "shutdown") == 0) {
            char *resp;
            asprintf(&resp,
                "{\"id\":%d,\"status\":\"ok\",\"result\":{},\"error\":null}",
                id);
            if (resp) {
                send_frame(resp, strlen(resp));
                free(resp);
            }

            bt_core_shutdown();
            break;
        }

        else {
            send_error_response(id, 400, "unknown method");
        }

        cJSON_Delete(root);
        free(req);
    }

    return 0;
}
