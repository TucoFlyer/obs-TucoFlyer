#include <obs-module.h>
#include <regex>
#include <websocketpp/uri.hpp>
#include "bot-connector.h"

#define LOG_PREFIX "BotConnector: "

using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using websocketpp::lib::bind;

BotConnector::BotConnector()
{
    thread_client = new client_t;
    thread_client->init_asio();
    thread_client->set_message_handler(bind(&BotConnector::thread_message_handler, this, ::_1, ::_2));
    conn_timer = new asio::steady_timer(thread_client->get_io_service());
    thread_curl = curl_easy_init();
    if (!thread_curl) {
        blog(LOG_ERROR, "Curl failed to init");
        abort();
    }
    async_reconnect();
    thread = std::thread(bind(&BotConnector::thread_func, this));
}

BotConnector::~BotConnector()
{
    thread_client->get_io_service().stop();
    thread.join();
    delete conn_timer;
    delete thread_client;
    curl_easy_cleanup(thread_curl);
}

void BotConnector::set_connection_file_path(const char *path)
{
    std::lock_guard<std::mutex> lock(conn_path_mutex);
    conn_path = path;
}

std::string BotConnector::get_connection_file_path()
{
    std::lock_guard<std::mutex> lock(conn_path_mutex);
    std::string copy = conn_path;
    return copy;
}

void BotConnector::thread_message_handler(connection_hdl conn, message_ptr msg)
{
    blog(LOG_INFO, LOG_PREFIX "Websocket message: %s", msg->get_payload().c_str());
}

void BotConnector::async_reconnect()
{
    conn_timer->expires_from_now(std::chrono::seconds(1));
    conn_timer->async_wait(bind(&BotConnector::reconnect_handler, this));   
}

void BotConnector::reconnect_handler()
{
    std::string path = get_connection_file_path();

    FILE *f = fopen(path.c_str(), "r");
    if (!f) {
        blog(LOG_ERROR, LOG_PREFIX "Can't open connection file");
        async_reconnect();
        return;
    }

    char line_buffer[256];
    if (!fgets(line_buffer, sizeof line_buffer, f)) {
        blog(LOG_ERROR, LOG_PREFIX "Can't read connection file");
        fclose(f);
        async_reconnect();
        return;
    }
    fclose(f);

    websocketpp::uri http_frontend_uri(line_buffer);
    websocketpp::uri http_ws_endpoint(http_frontend_uri.get_scheme(),
                                      http_frontend_uri.get_host(),
                                      http_frontend_uri.get_port(),
                                      "/ws");

    std::regex find_fragment_query_k("#[^?]*\\?(?:(?!k=)[^&]*&)*k=([^&]+)");
    std::string auth_key;
    std::smatch match;
    if (std::regex_search(http_frontend_uri.get_resource(), match, find_fragment_query_k)) {
        auth_key = match[1];
    }

    {
        std::string debug_uri = http_frontend_uri.str();
        std::string debug_endpoint = http_ws_endpoint.str();
        blog(LOG_INFO, "Debug! frontend_uri=%s ws_endpoint=%s key=%s",
            debug_uri.c_str(), debug_endpoint.c_str(), auth_key.c_str());
    }

    // websocketpp::lib::error_code connection_err;
    // client_t::connection_ptr connection = client.get_connection("ws://10.0.0.5:8081", connection_err);
    // if (connection_err) {
    //  blog(LOG_ERROR, "WebSocket connection failed, %s", connection_err.message().c_str());
    // } else {
    //  client.connect(connection);

    async_reconnect();
}

void BotConnector::thread_func()
{
    blog(LOG_INFO, "In the BotConnector thread");
    try {
        thread_client->run();
    } catch (websocketpp::exception const &exc) {
        blog(LOG_ERROR, "WebSocket exception, %s", exc.what());
    }
    blog(LOG_INFO, "Leaving this BotConnector thread");
}
