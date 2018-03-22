#pragma once

#include <websocketpp/common/thread.hpp>
#include <websocketpp/common/asio.hpp>
#include <websocketpp/common/thread.hpp>
#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/client.hpp>
#include <curl/curl.h>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>

class BotConnector {
public:
    BotConnector();
    ~BotConnector();

    void set_connection_file_path(const char *path);
    std::string get_connection_file_path();

    void send(rapidjson::StringBuffer* buffer);
    bool is_authenticated();
    bool poll_for_tracking_region_reset(double rect[4]);

    std::function<void(rapidjson::Value const&)> on_camera_overlay_scene;
    std::function<void(rapidjson::Value const&)> on_camera_output_enable;

private:
    typedef websocketpp::client<websocketpp::config::asio_client> client_t;
    typedef websocketpp::config::asio_client::message_type::ptr message_ptr;
    typedef client_t::connection_ptr connection_ptr;
    typedef websocketpp::connection_hdl connection_hdl;

    bool authenticated;
    bool connected;
    bool can_send;
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

    double init_tracked_rect[4];
    std::atomic<bool> init_tracking_rect_flag;

    connection_hdl active_conn;
    void local_send(rapidjson::StringBuffer* buffer);

    void thread_func();
    void async_reconnect();
    void reconnect_handler();
    bool try_reconnect();
    void send_subscription();

    void on_socket_open(connection_hdl conn);
    void on_socket_close(connection_hdl conn);
    void on_socket_message(connection_hdl conn, message_ptr msg);
    void on_stream_message(rapidjson::Value const &msg, double timestamp);
    void on_auth_challenge(const char *challenge);
    void on_auth_status(bool status);
    void on_error_message(rapidjson::Value const &error);
    void on_camera_init_tracked_region(rapidjson::Value const &rect);

    std::string read_connection_frontend_uri();
    std::string request_websocket_uri(std::string const &frontend_uri);
    std::string parse_frontend_auth_key(std::string const &frontend_uri);
};
