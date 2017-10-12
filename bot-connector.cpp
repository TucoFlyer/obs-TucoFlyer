#include <obs-module.h>
#include "bot-connector.h"

using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using websocketpp::lib::bind;

BotConnector::BotConnector()
{
	thread_client = new client_t;
	thread_client->init_asio();
	thread_client->set_message_handler(bind(&BotConnector::thread_message_handler, this, ::_1, ::_2));
	conn_timer = new asio::steady_timer(thread_client->get_io_service());
	async_reconnect();
	thread = std::thread(bind(&BotConnector::thread_func, this));
}

BotConnector::~BotConnector()
{
	thread_client->get_io_service().stop();
	thread.join();
	delete conn_timer;
	delete thread_client;
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
	blog(LOG_INFO, "Websocket message: %s", msg->get_payload().c_str());
}

void BotConnector::async_reconnect()
{
	conn_timer->expires_from_now(std::chrono::seconds(1));
	conn_timer->async_wait(bind(&BotConnector::reconnect_handler, this));	
}

void BotConnector::reconnect_handler()
{
	std::string path = get_connection_file_path();

	blog(LOG_INFO, "Trying to connect or something, path=%s", path.c_str());

	// websocketpp::lib::error_code connection_err;
	// client_t::connection_ptr connection = client.get_connection("ws://10.0.0.5:8081", connection_err);
	// if (connection_err) {
	// 	blog(LOG_ERROR, "WebSocket connection failed, %s", connection_err.message().c_str());
	// } else {
	// 	client.connect(connection);

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
