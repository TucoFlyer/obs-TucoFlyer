#pragma once

#include <websocketpp/common/thread.hpp>
#include <websocketpp/common/asio.hpp>
#include <websocketpp/common/thread.hpp>
#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/client.hpp>
#include <curl/curl.h>
#include <thread>
#include <mutex>

class BotConnector {
public:
    BotConnector();
    ~BotConnector();

    void set_connection_file_path(const char *path);
    std::string get_connection_file_path();

private:
    typedef websocketpp::client<websocketpp::config::asio_client> client_t;
    typedef websocketpp::config::asio_client::message_type::ptr message_ptr;
    typedef client_t::connection_ptr connection_ptr;
    typedef websocketpp::connection_hdl connection_hdl;

    std::thread thread;
    client_t *thread_client;
    CURL *thread_curl;
    char curl_errbuf[CURL_ERROR_SIZE];

    std::string frontend_uri;
    std::string ws_uri;
    std::string auth_key;

    asio::steady_timer *conn_timer;
    std::mutex conn_path_mutex;
    std::string conn_path;

    void thread_func();
    void on_message(connection_hdl conn, message_ptr msg);
    void async_reconnect();
    void reconnect_handler();
    bool try_reconnect();
    std::string read_connection_frontend_uri();
    std::string request_websocket_uri(std::string const &frontend_uri);
    std::string parse_frontend_auth_key(std::string const &frontend_uri);
};
