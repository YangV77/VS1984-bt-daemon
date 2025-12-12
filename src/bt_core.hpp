// src/bt_core.hpp
#ifndef VS_BT_CORE_HPP
#define VS_BT_CORE_HPP

#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <mutex>
#include <condition_variable>
#include <queue>

#include "../third_party/libtorrent/include/libtorrent/session.hpp"
#include "../third_party/libtorrent/include/libtorrent/session_params.hpp"
#include "../third_party/libtorrent/include/libtorrent/add_torrent_params.hpp"
#include "../third_party/libtorrent/include/libtorrent/torrent_handle.hpp"
#include "../third_party/libtorrent/include/libtorrent/create_torrent.hpp"
#include "../third_party/libtorrent/include/libtorrent/file_storage.hpp"
#include "../third_party/libtorrent/include/libtorrent/magnet_uri.hpp"
#include "../third_party/libtorrent/include/libtorrent/error_code.hpp"
#include "../third_party/libtorrent/include/libtorrent/torrent_flags.hpp"
#include "../third_party/libtorrent/include/libtorrent/session.hpp"
#include "../third_party/libtorrent/include/libtorrent/bencode.hpp"

struct BtTorrentStatus; // from C header

struct BtConfig {
    bool enable_bt      = true;
    bool enable_dht     = true;
    int  listen_start   = 6881;
    int  listen_end     = 6891;
    int  upload_limit   = 0;    // bytes/s, 0 = unlimited
    int  download_limit = 0;    // bytes/s, 0 = unlimited
    std::vector<std::string> dht_routers;
};

class BtCore {
public:
    BtCore();
    ~BtCore();

    bool init(const std::string& config_path);
    void shutdown();

    bool addMagnet(const std::string& magnet,
                   const std::string& save_dir,
                   std::string& out_infohash_hex);

    bool addTorrentFile(const std::string& torrent_path,
                        const std::string& save_dir,
                        std::string& out_infohash_hex);

    bool seedFolder(const std::string& folder,
                    const std::string& torrent_out,
                    std::string& out_infohash_hex);

    bool pauseTorrent(const std::string& infohash_hex);
    bool resumeTorrent(const std::string& infohash_hex);
    bool removeTorrent(const std::string& infohash_hex, bool remove_files);

    bool getStatus(const std::string& infohash_hex, BtTorrentStatus& out_status);

private:
    void threadFunc();
    void postCommand(const std::function<void(libtorrent::session&)>& cmd);

    libtorrent::session* getSession(); // only in BT thread

private:
    BtConfig m_cfg;
    bool loadConfig(const std::string& path, BtConfig& out);
    std::thread m_thread;
    bool        m_running = false;

    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::queue<std::function<void(libtorrent::session&)>> m_cmdQueue;

    std::unique_ptr<libtorrent::session> m_session;
    std::unordered_map<std::string, libtorrent::torrent_handle> m_torrents; // infohash_hex -> handle
};

#endif // VS_BT_CORE_HPP
