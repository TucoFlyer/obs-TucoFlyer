#include <obs-module.h>
#include <websocketpp/common/thread.hpp>
#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/client.hpp>
#include "bot-connector.h"

using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using websocketpp::lib::bind;

typedef websocketpp::client<websocketpp::config::asio_client> client_t;
typedef websocketpp::config::asio_client::message_type::ptr message_ptr;
typedef client_t::connection_ptr connection_ptr;
typedef websocketpp::connection_hdl connection_hdl;

static void message_handler(client_t *client, connection_hdl connection, message_ptr message) {
	blog(LOG_INFO, "Websocket message: %s", message->get_payload().c_str());
}

BotConnector::BotConnector()
	: thread(threadFunc)
{}

BotConnector::~BotConnector()
{
	thread.join();
}

void BotConnector::threadFunc()
{
	blog(LOG_INFO, "In the BotConnector thread");

	client_t client;
	try {
		client.init_asio();
		client.set_message_handler(bind(&message_handler, &client, ::_1, ::_2));
		websocketpp::lib::error_code connection_err;
		client_t::connection_ptr connection = client.get_connection("ws://10.0.0.5:8081", connection_err);
		if (connection_err) {
			blog(LOG_ERROR, "WebSocket connection failed, %s", connection_err.message().c_str());
		} else {
			client.connect(connection);
			client.run();
		}
	} catch (websocketpp::exception const &exc) {
		blog(LOG_ERROR, "WebSocket exception, %s", exc.what());
    }

	blog(LOG_INFO, "Leaving this BotConnector thread");
}
