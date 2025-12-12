// src/bt_core.cpp
#include "bt_core.hpp"
#include "bt_api.h"
#include "bt_utils.h"
#include <iostream>
#include <fstream>
#include <future>   // std::promise, std::future
#include <vector>   // std::vector
#include <chrono>   // std::chrono

#include <libtorrent/settings_pack.hpp>
#include <libtorrent/sha1_hash.hpp>
#include <libtorrent/info_hash.hpp>
#include <libtorrent/read_resume_data.hpp>
#include <libtorrent/write_resume_data.hpp>
namespace lt = libtorrent;

static std::string sha1_to_hex(const lt::sha1_hash& h)
{
    std::string s = h.to_string();
    static const char* hex = "0123456789abcdef";

    std::string out;
    out.resize(s.size() * 2);

    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        out[2 * i]     = hex[c >> 4];
        out[2 * i + 1] = hex[c & 0x0F];
    }
    return out;
}

BtCore::BtCore() = default;

BtCore::~BtCore()
{
    shutdown();
}

bool BtCore::init(const std::string& config_path)
{
    if (m_running) return true;

    if (!loadConfig(config_path, m_cfg)) {
        iloge("[btd] loadConfig failed, path= %s", config_path.c_str());
        return false;
    }

    // 如果配置里直接禁用 BT，就不启动线程
    if (!m_cfg.enable_bt) {
        iloge("[btd] BT disabled by config");
        return false;
    }

    m_running = true;
    m_thread = std::thread(&BtCore::threadFunc, this);
    return true;
}

void BtCore::shutdown()
{
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (!m_running) return;
        m_running = false;
        m_cv.notify_all();
    }
    if (m_thread.joinable())
        m_thread.join();
}

lt::session* BtCore::getSession()
{
    return m_session.get();
}

bool BtCore::loadConfig(const std::string& path, BtConfig& out)
{
    if (path.empty()) {

        out = BtConfig{};

        out.dht_routers = {
            "router.bittorrent.com:6881",
            "router.utorrent.com:6881",
            "dht.transmissionbt.com:6881"
        };
        return true;
    }

    std::ifstream in(path);
    if (!in.is_open()) {
        iloge("[btd] config not found, use default: %s",path.c_str());
        out = BtConfig{};
        out.dht_routers = {
            "router.bittorrent.com:6881",
            "router.utorrent.com:6881",
            "dht.transmissionbt.com:6881"
        };
        return true;
    }

    BtConfig cfg;
    cfg.dht_routers.clear();

    std::string line;
    while (std::getline(in, line)) {

        auto pos_comment = line.find('#');
        if (pos_comment != std::string::npos) {
            line = line.substr(0, pos_comment);
        }
        auto trim = [](std::string& s) {
            auto p1 = s.find_first_not_of(" \t\r\n");
            auto p2 = s.find_last_not_of(" \t\r\n");
            if (p1 == std::string::npos) {
                s.clear();
            } else {
                s = s.substr(p1, p2 - p1 + 1);
            }
        };
        trim(line);
        if (line.empty()) continue;

        auto pos_eq = line.find('=');
        if (pos_eq == std::string::npos) continue;

        std::string key = line.substr(0, pos_eq);
        std::string val = line.substr(pos_eq + 1);
        trim(key);
        trim(val);

        if (key == "enable_bt") {
            cfg.enable_bt = (val == "1" || val == "true" || val == "on");
        } else if (key == "enable_dht") {
            cfg.enable_dht = (val == "1" || val == "true" || val == "on");
        } else if (key == "listen_start") {
            cfg.listen_start = std::stoi(val);
        } else if (key == "listen_end") {
            cfg.listen_end = std::stoi(val);
        } else if (key == "upload_limit_kb") {
            cfg.upload_limit = std::stoi(val) * 1024;
        } else if (key == "download_limit_kb") {
            cfg.download_limit = std::stoi(val) * 1024;
        } else if (key == "dht_router") {
            cfg.dht_routers.push_back(val);
        }
    }

    if (cfg.dht_routers.empty()) {
        cfg.dht_routers = {
            "router.bittorrent.com:6881",
            "router.utorrent.com:6881",
            "dht.transmissionbt.com:6881"
        };
    }

    out = cfg;
    return true;
}

void BtCore::postCommand(const std::function<void(lt::session&)>& cmd)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    if (!m_running) return;
    m_cmdQueue.push(cmd);
    m_cv.notify_all();
}

void BtCore::threadFunc()
{
    lt::settings_pack pack;

    pack.set_int(lt::settings_pack::alert_mask,
        lt::alert::error_notification |
        lt::alert::status_notification |
        lt::alert::storage_notification);

    // Listen port
    int port = m_cfg.listen_start;
    int retries = (m_cfg.listen_end - m_cfg.listen_start);

    pack.set_str(lt::settings_pack::listen_interfaces,
                 "0.0.0.0:" + std::to_string(port) +
                 ",[::]:" + std::to_string(port));

    pack.set_int(lt::settings_pack::max_retry_port_bind, retries);

    if (m_cfg.upload_limit > 0)
        pack.set_int(lt::settings_pack::upload_rate_limit, m_cfg.upload_limit);

    if (m_cfg.download_limit > 0)
        pack.set_int(lt::settings_pack::download_rate_limit, m_cfg.download_limit);

    pack.set_bool(lt::settings_pack::enable_dht, m_cfg.enable_dht);

    m_session = std::make_unique<lt::session>(pack);

    if (m_cfg.enable_dht) {
        for (auto& s : m_cfg.dht_routers) {
            auto pos = s.find(':');
            if (pos == std::string::npos) continue;
            std::string host = s.substr(0, pos);
            int port = std::stoi(s.substr(pos + 1));
            m_session->add_dht_router({host, port});
        }
        m_session->start_dht();
    }

    while (m_running) {
        // 处理命令
        std::function<void(lt::session&)> cmd;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            if (m_cmdQueue.empty()) {
                m_cv.wait_for(lock, std::chrono::milliseconds(500));
            }
            if (!m_running) break;
            if (!m_cmdQueue.empty()) {
                cmd = std::move(m_cmdQueue.front());
                m_cmdQueue.pop();
            }
        }
        if (cmd) {
            cmd(*m_session);
        }

        std::vector<lt::alert*> alerts;
        m_session->pop_alerts(&alerts);
        for (auto* a : alerts) {
            iloge("[btd] alert: %s", a->message().c_str());
        }
    }

    m_session.reset();
}


bool BtCore::addMagnet(const std::string& magnet,
                       const std::string& save_dir,
                       std::string& out_infohash_hex)
{
    bool ok = false;
    std::promise<void> done;
    auto fut = done.get_future();

    postCommand([&](lt::session& ses) {
        lt::error_code ec;
        lt::add_torrent_params p = lt::parse_magnet_uri(magnet, ec);
        if (ec) {
            iloge("[btd] parse_magnet_uri error: %s",ec.message().c_str());
            done.set_value();
            return;
        }
        p.save_path = save_dir;
        p.flags |= lt::torrent_flags::auto_managed;
        p.flags |= lt::torrent_flags::paused; // 先暂停，再手动 resume

        lt::torrent_handle h = ses.add_torrent(p, ec);
        if (ec) {
            iloge("[btd] add_torrent(magnet) error: %s", ec.message().c_str());
            done.set_value();
            return;
        }

        lt::info_hash_t ih = h.info_hashes();
        lt::sha1_hash v1 = ih.v1;
        std::string hex = sha1_to_hex(v1);

        {
            std::lock_guard<std::mutex> guard(m_mutex);
            m_torrents[hex] = h;
        }
        out_infohash_hex = hex;

        h.resume();
        ok = true;
        done.set_value();
    });

    fut.wait();
    return ok;
}

bool BtCore::addTorrentFile(const std::string& torrent_path,
                            const std::string& save_dir,
                            std::string& out_infohash_hex)
{
    bool ok = false;
    std::promise<void> done;
    auto fut = done.get_future();

    postCommand([&](lt::session& ses) {
        lt::error_code ec;
        auto ti = std::make_shared<lt::torrent_info>(torrent_path, ec);
        if (ec) {
            iloge("[btd] load torrent file error: %s", ec.message().c_str());
            done.set_value();
            return;
        }

        lt::add_torrent_params p;
        p.ti = ti;
        p.save_path = save_dir;
        p.flags |= lt::torrent_flags::auto_managed;

        lt::torrent_handle h = ses.add_torrent(p, ec);
        if (ec) {
            iloge("[btd] add_torrent(file) error: %s", ec.message().c_str());
            done.set_value();
            return;
        }

        lt::info_hash_t ih = h.info_hashes();
        lt::sha1_hash v1 = ih.v1;
        std::string hex = sha1_to_hex(v1);

        {
            std::lock_guard<std::mutex> guard(m_mutex);
            m_torrents[hex] = h;
        }
        out_infohash_hex = hex;

        ok = true;
        done.set_value();
    });

    fut.wait();
    return ok;
}

bool BtCore::seedFolder(const std::string& folder,
                        const std::string& torrent_out,
                        std::string& out_infohash_hex)
{
    bool ok = false;
    std::promise<void> done;
    auto fut = done.get_future();

    postCommand([&](lt::session& ses) {
        lt::file_storage fs;

        lt::add_files(fs, folder);
        if (fs.num_files() == 0) {
            iloge("[btd] seedFolder: no files in folder: %s", folder.c_str());
            done.set_value();
            return;
        }

        const int piece_size = 0;
        lt::create_torrent ct(fs, piece_size);

        std::string parent = folder;
        {
            auto pos = parent.find_last_of("/\\");
            if (pos == std::string::npos) {
                parent = ".";
            } else if (pos == 0) {
                parent = "/";
            } else {
                parent = parent.substr(0, pos);
            }
        }

        lt::error_code ec;
        lt::set_piece_hashes(ct, parent, ec);
        if (ec) {
            iloge("[btd] set_piece_hashes error: %s (parent=%s)", ec.message().c_str(), parent.c_str());
            done.set_value();
            return;
        }

        lt::entry e = ct.generate();
        std::vector<char> buf;
        lt::bencode(std::back_inserter(buf), e);

        std::ofstream out(torrent_out, std::ios::binary);
        if (!out) {
            iloge("[btd] cannot open torrent_out: %s", torrent_out.c_str());
            done.set_value();
            return;
        }
        out.write(buf.data(), buf.size());
        out.close();

        auto ti = std::make_shared<lt::torrent_info>(torrent_out, ec);
        if (ec) {
            iloge("[btd] torrent_info from file error: %s", ec.message().c_str());
            done.set_value();
            return;
        }

        lt::add_torrent_params p;
        p.ti = ti;
        p.save_path = parent;
        p.flags |= lt::torrent_flags::seed_mode;

        lt::torrent_handle h = ses.add_torrent(p, ec);
        if (ec) {
            iloge("[btd] add_torrent(seed) error: %s", ec.message().c_str());
            done.set_value();
            return;
        }

        lt::info_hash_t ih = h.info_hashes();
        lt::sha1_hash v1 = ih.v1;
        std::string hex = sha1_to_hex(v1);

        {
            std::lock_guard<std::mutex> guard(m_mutex);
            m_torrents[hex] = h;
        }
        out_infohash_hex = hex;

        ok = true;
        done.set_value();
    });

    fut.wait();
    return ok;
}


bool BtCore::pauseTorrent(const std::string& infohash_hex)
{
    std::promise<void> done;
    auto fut = done.get_future();
    bool ok = false;

    postCommand([&](lt::session&) {
        std::lock_guard<std::mutex> guard(m_mutex);
        auto it = m_torrents.find(infohash_hex);
        if (it != m_torrents.end()) {
            it->second.pause();
            ok = true;
        }
        done.set_value();
    });

    fut.wait();
    return ok;
}

bool BtCore::resumeTorrent(const std::string& infohash_hex)
{
    std::promise<void> done;
    auto fut = done.get_future();
    bool ok = false;

    postCommand([&](lt::session&) {
        std::lock_guard<std::mutex> guard(m_mutex);
        auto it = m_torrents.find(infohash_hex);
        if (it != m_torrents.end()) {
            it->second.resume();
            ok = true;
        }
        done.set_value();
    });

    fut.wait();
    return ok;
}

bool BtCore::removeTorrent(const std::string& infohash_hex, bool remove_files)
{
    std::promise<void> done;
    auto fut = done.get_future();
    bool ok = false;

    postCommand([&](lt::session& ses) {
        std::lock_guard<std::mutex> guard(m_mutex);
        auto it = m_torrents.find(infohash_hex);
        if (it != m_torrents.end()) {
            lt::remove_flags_t flags{};
            if (remove_files) {
                flags = lt::session::delete_files;
            }
            ses.remove_torrent(it->second, flags);
            m_torrents.erase(it);
            ok = true;
        }
        done.set_value();
    });

    fut.wait();
    return ok;
}

bool BtCore::getStatus(const std::string& infohash_hex, BtTorrentStatus& out_status)
{
    std::promise<void> done;
    auto fut = done.get_future();
    bool ok = false;

    postCommand([&](lt::session&) {
        std::lock_guard<std::mutex> guard(m_mutex);
        auto it = m_torrents.find(infohash_hex);
        if (it == m_torrents.end()) {
            done.set_value();
            return;
        }
        lt::torrent_status st = it->second.status();

        out_status.progress         = st.progress;
        out_status.download_rate    = st.download_rate;
        out_status.upload_rate      = st.upload_rate;
        out_status.total_downloaded = (long)st.total_download;
        out_status.total_uploaded   = (long)st.total_upload;
        out_status.num_peers        = st.num_peers;
        out_status.num_seeds        = st.num_seeds;
        out_status.num_leechers     = st.num_complete + st.num_incomplete;
        out_status.has_metadata     = st.has_metadata ? 1 : 0;
        out_status.is_seeding       = st.is_seeding ? 1 : 0;
        out_status.error_code       = st.errc.value();
        std::snprintf(out_status.error_msg, sizeof(out_status.error_msg),
                      "%s", st.errc ? st.errc.message().c_str() : "");

        if (st.is_seeding) {
            out_status.state = BT_STATE_SEEDING;
        } else if (st.paused) {
            out_status.state = BT_STATE_PAUSED;
        } else if (st.errc) {
            out_status.state = BT_STATE_ERROR;
        } else if (st.progress >= 0.9999f) {
            out_status.state = BT_STATE_FINISHED;
        } else {
            out_status.state = BT_STATE_DOWNLOADING;
        }

        ok = true;
        done.set_value();
    });

    fut.wait();
    return ok;
}
