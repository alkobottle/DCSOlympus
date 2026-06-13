#pragma once
#include "framework.h"
#include "luatools.h"

// Forward declarations — full definition only needed in server.cpp
namespace httplib {
    struct Request;
    struct Response;
    class Server;
}

class UnitsManager;
class Scheduler;

class Server
{
public:
    Server(lua_State* L);

    void start(lua_State* L);
    void stop(lua_State* L);

private:
    std::thread* serverThread;

    void handle_options(const httplib::Request& req, httplib::Response& res);
    void handle_get(const httplib::Request& req, httplib::Response& res);
    void handle_put(const httplib::Request& req, httplib::Response& res);

    string extractUsername(const httplib::Request& req);
    string extractPassword(const httplib::Request& req);

    void task();

    atomic<bool> runListener;
    atomic<httplib::Server*> svr_ptr{nullptr};

    string gameMasterPassword = "";
    string blueCommanderPassword = "";
    string redCommanderPassword = "";
    string atcPassword = "";
    string observerPassword = "";
};
