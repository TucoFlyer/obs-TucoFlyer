#include <obs-module.h>
#include <regex>
#include <ctype.h>
#include <websocketpp/uri.hpp>
#include <rapidjson/writer.h>
#include <hmac.h>
#include <sha.h>
#include <base64.h>
#include "bot-connector.h"
#include "json-util.h"

#define LOG_PREFIX      "BotConnector: "

using namespace CryptoPP;
using namespace websocketpp;
using namespace rapidjson;

using lib::placeholders::_1;
using lib::placeholders::_2;
using lib::bind;

BotConnector::BotConnector()
    : authenticated(false),
      connected(false)
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

void BotConnector::send(StringBuffer* buffer)
{
    thread_client->get_io_service().dispatch([=] () {
        local_send(buffer);
    });
}

bool BotConnector::is_authenticated()
{
    return authenticated;
}

bool BotConnector::poll_for_tracking_region_reset(double rect[4])
{
    bool result = init_tracking_rect_flag.exchange(false);
    if (result) {
        memcpy(rect, init_tracked_rect, sizeof init_tracked_rect);
    }
    return result;
}

void BotConnector::local_send(StringBuffer* buffer)
{
    if (active_conn.lock()) {
        thread_client->send(active_conn,
            buffer->GetString(), buffer->GetSize(),
            frame::opcode::TEXT);
    }
    delete buffer;
}

void BotConnector::on_socket_open(connection_hdl conn)
{
    active_conn = conn;
}

void BotConnector::on_socket_close(connection_hdl conn)
{
    active_conn = connection_hdl();
    async_reconnect();
}

void BotConnector::on_socket_message(connection_hdl conn, message_ptr msg)
{
    if (!connected) {
        blog(LOG_INFO, LOG_PREFIX "Connection succeeded, receiving messages");
        connected = true;
    }

    Document doc;
    doc.ParseInsitu((char*) msg->get_payload().c_str());
    Value const* obj;

    obj = json_obj(doc, "Stream");
    if (obj && obj->IsArray()) {
        for (SizeType i = 0; i < obj->Size(); i++) {
            const Value &ts_msg = (*obj)[i];
            double timestamp = json_double(ts_msg, "timestamp");
            Value const* msg = json_obj(ts_msg, "message");
            if (msg && msg->IsObject()) {
                on_stream_message(*msg, timestamp);
            }
        }
    }

    obj = json_obj(doc, "Auth");
    if (obj && obj->IsObject()) {
        on_auth_challenge(json_str(*obj, "challenge"));
    }

    obj = json_obj(doc, "AuthStatus");
    if (obj && obj->IsBool()) {
       on_auth_status(obj->GetBool());
    }

    obj = json_obj(doc, "Error");
    if (obj && obj->IsObject()) {
        on_error_message(*obj);
    }
}

void BotConnector::on_stream_message(Value const &msg, double timestamp)
{
    Value const* obj;

    obj = json_obj(msg, "CameraOverlayScene");
    if (obj && obj->IsArray()) {
        on_camera_overlay_scene(*obj);
    }

    obj = json_obj(msg, "CameraInitTrackedRegion");
    if (obj && obj->IsArray()) {
        on_camera_init_tracked_region(*obj);
    }
}

void BotConnector::on_camera_init_tracked_region(rapidjson::Value const &rect)
{
    if (rect.Size() == 4) {
        for (unsigned i = 0; i < 4; i++) {
            if (!rect[i].IsNumber()) {
                return;
            }
            init_tracked_rect[i] = rect[i].GetDouble();
        }
        init_tracking_rect_flag.store(true);
    }
}

void BotConnector::on_auth_challenge(const char *challenge)
{
    Document d;

    HMAC<SHA512> hmac((const byte*)auth_key.c_str(), auth_key.size());
    std::string digest_str;
    StringSource s(challenge, true, 
        new HashFilter(hmac,
            new Base64Encoder(
                new StringSink(digest_str),
                false // insertLineBreaks
            )
        )
    );
    Value digest(StringRef(digest_str.c_str(), digest_str.size()));

    d.SetObject();
    Value auth;
    auth.SetObject();
    auth.AddMember("digest", digest, d.GetAllocator());
    d.AddMember("Auth", auth, d.GetAllocator());

    StringBuffer *buffer = new StringBuffer();
    Writer<StringBuffer> writer(*buffer);
    d.Accept(writer);

    local_send(buffer);
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

void BotConnector::on_error_message(Value const &error)
{
    blog(LOG_ERROR, LOG_PREFIX "Error reported by server, code=%s message=%s",
        json_str(error, "code"), json_str(error, "message"));
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
    thread_client->set_open_handler(bind(&BotConnector::on_socket_open, this, ::_1));
    thread_client->set_close_handler(bind(&BotConnector::on_socket_close, this, ::_1));
    thread_client->set_fail_handler(bind(&BotConnector::async_reconnect, this));

    lib::error_code err;
    client_t::connection_ptr connection = thread_client->get_connection(ws_uri, err);
    if (err) {
        blog(LOG_ERROR, LOG_PREFIX "WebSocket connection error, %s", err.message().c_str());
        return false;
    }

    blog(LOG_INFO, LOG_PREFIX "Starting connection to %s", ws_uri.c_str());
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
    uri fe(frontend_uri);
    if (!fe.get_valid()) {
        blog(LOG_ERROR, LOG_PREFIX "No valid URI for Bot-Controller HTTP frontend");
        return std::string();
    }

    uri ep(fe.get_scheme(), fe.get_host(), fe.get_port(), "/ws");
    std::string endpoint_url = ep.str();

    curl_easy_setopt(thread_curl, CURLOPT_URL, endpoint_url.c_str());
    curl_easy_setopt(thread_curl, CURLOPT_ERRORBUFFER, curl_errbuf);
    curl_errbuf[0] = '\0';

    std::string json_buffer;
    curl_write_callback json_writer = [] (char *buf, size_t size, size_t num, void *user) -> size_t {
        std::string &json_buffer = *static_cast<std::string*>(user);
        size *= num;
        json_buffer.append((char*) buf, size);
        return size;
    };

    curl_easy_setopt(thread_curl, CURLOPT_WRITEDATA, &json_buffer);
    curl_easy_setopt(thread_curl, CURLOPT_WRITEFUNCTION, json_writer);
    CURLcode res = curl_easy_perform(thread_curl);
    curl_easy_setopt(thread_curl, CURLOPT_WRITEDATA, 0);
    curl_easy_setopt(thread_curl, CURLOPT_WRITEFUNCTION, 0);

    if (res != CURLE_OK) {
        const char *err = curl_errbuf[0] ? curl_errbuf : curl_easy_strerror(res);
        blog(LOG_ERROR, LOG_PREFIX "Failed to fetch %s, libcurl: %s", endpoint_url.c_str(), err);
        return std::string();
    }

    Document doc;
    doc.ParseInsitu((char*) json_buffer.c_str());
    std::string uri = json_str(doc, "uri");

    return uri;
}

std::string BotConnector::parse_frontend_auth_key(std::string const &frontend_uri)
{
    // Look for "k=" query parameter in list of parameters within URI fragment
    std::regex re("#[^?]*\\?(?:(?!k=)[^&]*&)*k=([^&]+)");

    uri frontend(frontend_uri);
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
    } catch (exception const &exc) {
        blog(LOG_ERROR, "WebSocket exception, %s", exc.what());
    }
}
