#include <obs-module.h>
#include <regex>
#include <ctype.h>
#include <websocketpp/uri.hpp>
#include "bot-connector.h"

#define LOG_PREFIX      "BotConnector: "

using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using websocketpp::lib::bind;

BotConnector::BotConnector()
    : authenticated(false),
      connected(false),
      latest_camera_overlay_scene(0)
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

void BotConnector::on_socket_message(connection_hdl conn, message_ptr msg)
{
    connected = true;

    json_error_t err;
    json_t *obj;
    json_t *msg_json = json_loads(msg->get_payload().c_str(), 0, &err);
    if (!msg_json) {
        blog(LOG_ERROR, LOG_PREFIX "Failed to parse message JSON (%s)", err.text);
        return;
    }

    obj = json_object_get(msg_json, "Stream");
    if (obj && json_is_array(obj)) {
        size_t index;
        json_t *ts_msg;
        json_array_foreach(obj, index, ts_msg) {
            double timestamp;
            json_t *msg;
            if (json_unpack(ts_msg, "{sosF}", "message", &msg, "timestamp", &timestamp) == 0) {
                on_stream_message(msg, timestamp);                
            }
        }
    }

    obj = json_object_get(msg_json, "Auth");
    if (obj) {
        const char *challenge = 0;
        if (json_unpack(obj, "{ss}", "challenge", &challenge) == 0) {
            on_auth_challenge(challenge);
        }
    }

    obj = json_object_get(msg_json, "AuthStatus");
    if (obj && json_is_boolean(obj)) {
       on_auth_status(json_boolean_value(obj));
    }

    obj = json_object_get(msg_json, "Error");
    if (obj) {
        on_error_message(obj);
    }

    json_decref(msg_json);
}

json_t *BotConnector::take_camera_overlay_scene()
{
    return latest_camera_overlay_scene.exchange(0);
}

void BotConnector::on_stream_message(json_t *msg, double timestamp)
{
    json_t *obj;

    obj = json_object_get(msg, "CameraOverlayScene");
    if (obj) {
        json_incref(obj);
        json_t *previous = latest_camera_overlay_scene.exchange(obj);
        if (previous) {
            json_decref(previous);
        }
    }
}

void BotConnector::on_auth_challenge(const char *challenge)
{
    // to do: authenticate here!
}

void BotConnector::on_auth_status(bool status)
{
    if (status) {
        blog(LOG_INFO, LOG_PREFIX "authenticated with server");
    } else {
        blog(LOG_ERROR, LOG_PREFIX "authentiation FAILED oh no");
    }
    authenticated = status;
}

void BotConnector::on_error_message(json_t *msg)
{
    const char *code;
    json_t *optional_message;

    if (json_unpack(msg, "{ssso}", "code", &code, "message", &optional_message) == 0) {
        const char *message = json_string_value(optional_message);
        blog(LOG_ERROR, LOG_PREFIX "Error reported by server, code=%s message=%s",
            code, message ? message : "(none)");
    }
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
    authenticated = false;
    connected = false;

    frontend_uri = read_connection_frontend_uri();
    if (frontend_uri.empty()) {
        return false;
    }

    ws_uri = request_websocket_uri(frontend_uri);
    auth_key = parse_frontend_auth_key(frontend_uri);

    if (ws_uri.empty()) {
        return false;
    }

    thread_client->set_message_handler(bind(&BotConnector::on_socket_message, this, ::_1, ::_2));
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

    json_error_t err;
    json_t *json = json_loads(json_str.c_str(), 0, &err);
    if (!json) {
        blog(LOG_ERROR, LOG_PREFIX "Failed to parse URI JSON blob (%s)", err.text);
        return std::string();
    }

    const char *uri_cstr = 0;
    std::string uri;
    if (json_unpack(json, "{ss}", "uri", &uri_cstr) == 0) {
        uri = uri_cstr;
    } else {
        blog(LOG_ERROR, LOG_PREFIX "URI JSON blob didn't have the expected format");
    }

    json_decref(json);
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
