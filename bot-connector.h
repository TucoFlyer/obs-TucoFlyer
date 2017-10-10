#pragma once

#include <websocketpp/common/thread.hpp>

class BotConnector {
public:
	BotConnector();
	~BotConnector();

private:
	websocketpp::lib::thread thread;
	static void threadFunc();
};
