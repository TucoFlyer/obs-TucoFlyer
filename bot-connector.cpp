#include <obs-module.h>
#include <websocketpp/common/thread.hpp>
#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/client.hpp>
#include "bot-connector.h"

typedef websocketpp::client<websocketpp::config::asio_client> client;
typedef websocketpp::config::asio_client::message_type::ptr message_ptr;

BotConnector::BotConnector()
	: thread(threadFunc)
{}

BotConnector::~BotConnector()
{
	thread.join();
}

void BotConnector::threadFunc()
{
	blog(LOG_INFO, "In the BotConnector thread\n");

	client c;
	try {
		c.init_asio();
		websocketpp::lib::error_code connection_err;
		client::connection_ptr connection = c.get_connection("ws://10.0.0.5:8081", connection_err);
		if (connection_err) {
			blog(LOG_ERROR, "WebSocket connection failed, %s\n", ec.message().c_str());
		} else {
			c.connect(con);
			c.run();
		}
	} catch (websocketpp::exception const & e) {
		blog(LOG_ERROR, "WebSocket exception, %s\n", e.what().c_str());
    }

	blog(LOG_INFO, "Leaving this BotConnector thread\n");
}
