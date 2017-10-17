#include <obs-module.h>
#include <regex>
#include <ctype.h>
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

void BotConnector::on_message(connection_hdl conn, message_ptr msg)
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
    if (!try_reconnect()) {
        // No? Keep trying...
        async_reconnect();
    }
}

bool BotConnector::try_reconnect()
{
    frontend_uri = read_connection_frontend_uri();
    if (frontend_uri.empty()) {
        return false;
    }

    ws_uri = request_websocket_uri(frontend_uri);
    auth_key = parse_frontend_auth_key(frontend_uri);

    if (ws_uri.empty()) {
        return false;
    }

    thread_client->set_message_handler(bind(&BotConnector::on_message, this, ::_1, ::_2));
    thread_client->set_close_handler(bind(&BotConnector::async_reconnect, this));
    thread_client->set_fail_handler(bind(&BotConnector::async_reconnect, this));

    websocketpp::lib::error_code err;
    client_t::connection_ptr connection = thread_client->get_connection(ws_uri, err);
    if (err) {
        blog(LOG_ERROR, LOG_PREFIX "WebSocket connection error, %s", err.message().c_str());
        return false;
    }
	
	thread_client->connect(connection);
}

static void rtrim(char *str)
{
    int len = strlen(str);
    while (len) {
        char &last = str[len - 1];
        if (isspace(last)) {
            len--;
        } else {
            break;
        }
    }
    str[len] = '\0';
}

std::string BotConnector::read_connection_frontend_uri()
{
    char buffer[256];
    std::string path = get_connection_file_path();
    std::string result;

    FILE *f = fopen(path.c_str(), "r");
    if (!f) {
        blog(LOG_ERROR, LOG_PREFIX "Can't open connection file");
        return result;
    }

    char *line = fgets(buffer, sizeof buffer, f);
    if (line) { 
        rtrim(line);
        result = line;
    } else {
        blog(LOG_ERROR, LOG_PREFIX "Can't read connection file");
    }

    fclose(f);
    return result;
}

std::string BotConnector::request_websocket_uri(std::string const &frontend_uri)
{
    websocketpp::uri fe(frontend_uri);
    if (!fe.get_valid()) {
        blog(LOG_ERROR, LOG_PREFIX "No valid URI for Bot-Controller HTTP frontend");
        return std::string();
    }

    websocketpp::uri ep(fe.get_scheme(), fe.get_host(), fe.get_port(), "/ws");
    std::string endpoint_url = ep.str();

    curl_easy_setopt(thread_curl, CURLOPT_URL, endpoint_url.c_str());
    curl_easy_setopt(thread_curl, CURLOPT_ERRORBUFFER, curl_errbuf);
    curl_errbuf[0] = '\0';

    std::string json_str;
    curl_write_callback json_writer = [] (char *buf, size_t size, size_t num, void *user) -> size_t {
        std::string &json_str = *static_cast<std::string*>(user);
        size *= num;
        json_str.append((char*) buf, size);
        return size;
    };

    curl_easy_setopt(thread_curl, CURLOPT_WRITEDATA, &json_str);
    curl_easy_setopt(thread_curl, CURLOPT_WRITEFUNCTION, json_writer);
    CURLcode res = curl_easy_perform(thread_curl);
    curl_easy_setopt(thread_curl, CURLOPT_WRITEDATA, 0);
    curl_easy_setopt(thread_curl, CURLOPT_WRITEFUNCTION, 0);

    if (res != CURLE_OK) {
        const char *err = curl_errbuf[0] ? curl_errbuf : curl_easy_strerror(res);
        blog(LOG_ERROR, LOG_PREFIX "Failed to fetch %s, libcurl: %s", endpoint_url.c_str(), err);
        return std::string();
    }

    obs_data_t *json = obs_data_create_from_json(json_str.c_str());
    if (!json) {
        // obs_data already logged a json error
        return std::string();
    }

    // This is a JSON blob, but currently only the "uri" key matters
    const char *uri_cstr = obs_data_get_string(json, "uri");
    std::string uri;
    if (uri_cstr) {
        uri = uri_cstr;
    }

    obs_data_release(json);
    return uri;
}

std::string BotConnector::parse_frontend_auth_key(std::string const &frontend_uri)
{
    // Look for "k=" query parameter in list of parameters within URI fragment
    std::regex re("#[^?]*\\?(?:(?!k=)[^&]*&)*k=([^&]+)");

    websocketpp::uri frontend(frontend_uri);
    std::string result;
    std::smatch match;

    if (std::regex_search(frontend.get_resource(), match, re)) {
        result = match[1];
    }

    return result;
}

void BotConnector::thread_func()
{
    try {
        thread_client->run();
    } catch (websocketpp::exception const &exc) {
        blog(LOG_ERROR, "WebSocket exception, %s", exc.what());
    }
}
