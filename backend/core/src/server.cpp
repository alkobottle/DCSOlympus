#include "server.h"
#include "logger.h"
#include "defines.h"
#include "unitsmanager.h"
#include "weaponsmanager.h"
#include "scheduler.h"
#include "luatools.h"
#include "base64.hpp"

#include <httplib.h>
#include <chrono>
#include <fstream>
#include <sstream>
#include <exception>
#include <stdexcept>
#include <algorithm>

using namespace std::chrono;
using namespace base64;

extern UnitsManager* unitsManager;
extern WeaponsManager* weaponsManager;
extern Scheduler* scheduler;
extern json::value missionData;
extern json::value drawingsByLayer;
extern json::value executionResults;
extern mutex mutexLock;
extern string sessionHash;
extern string instancePath;

static void set_cors_headers(httplib::Response& res)
{
    res.set_header("Allow", "GET, PUT, OPTIONS");
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Methods", "GET, PUT, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
}

Server::Server(lua_State* L) :
    serverThread(nullptr),
    runListener(true),
    svr_ptr(nullptr)
{
}

void Server::start(lua_State* L)
{
    log("Starting RESTServer");
    serverThread = new thread(&Server::task, this);
}

void Server::stop(lua_State* L)
{
    log("Stopping RESTServer");
    runListener = false;
    httplib::Server* s = svr_ptr.load();
    if (s) s->stop();
    if (serverThread != nullptr) {
        serverThread->join();
        delete serverThread;
        serverThread = nullptr;
    }
}

void Server::handle_options(const httplib::Request& req, httplib::Response& res)
{
    res.status = 200;
    set_cors_headers(res);
}

void Server::handle_get(const httplib::Request& req, httplib::Response& res)
{
    lock_guard<mutex> guard(mutexLock);

    milliseconds ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch());

    string password = extractPassword(req);
    if (password == gameMasterPassword || password == blueCommanderPassword || password == redCommanderPassword)
    {
        try {
            // Extract the resource name from the path: /olympus/<resource>
            static const string prefix = "/olympus/";
            string URI;
            if (req.path.find(prefix) == 0)
                URI = req.path.substr(prefix.size());

            // Optional timestamp for delta updates
            unsigned long long time = 0;
            if (req.has_param("time")) {
                try { time = stoull(req.get_param_value("time")); }
                catch (...) { time = 0; }
            }

            if (!URI.empty())
            {
                if (URI == UNITS_URI)
                {
                    unsigned long long updateTime = ms.count();
                    stringstream ss;
                    ss.write((char*)&updateTime, sizeof(updateTime));
                    unitsManager->getUnitData(ss, time);
                    res.set_content(ss.str(), "application/octet-stream");
                }
                else if (URI == WEAPONS_URI)
                {
                    unsigned long long updateTime = ms.count();
                    stringstream ss;
                    ss.write((char*)&updateTime, sizeof(updateTime));
                    weaponsManager->getWeaponData(ss, time);
                    res.set_content(ss.str(), "application/octet-stream");
                }
                else {
                    auto answer = json::value::object();

                    if (URI == LOGS_URI)
                    {
                        auto logs = json::value::object();
                        getLogsJSON(logs, time);
                        answer[L"logs"] = logs;
                    }
                    else if (URI == AIRBASES_URI && missionData.has_object_field(L"airbases"))
                        answer[L"airbases"] = missionData[L"airbases"];
                    else if (URI == BULLSEYE_URI && missionData.has_object_field(L"bullseyes"))
                        answer[L"bullseyes"] = missionData[L"bullseyes"];
                    else if (URI == SPOTS_URI && missionData.has_object_field(L"spots"))
                        answer[L"spots"] = missionData[L"spots"];
                    else if (URI == MARKERS_URI && missionData.has_object_field(L"markers"))
                        answer[L"markers"] = missionData[L"markers"];
                    else if (URI == MISSION_URI && missionData.has_object_field(L"mission"))
                    {
                        answer[L"mission"] = missionData[L"mission"];
                        answer[L"mission"][L"commandModeOptions"] = scheduler->getCommandModeOptions();
                        if (password == gameMasterPassword)
                            answer[L"mission"][L"commandModeOptions"][L"commandMode"] = json::value(L"Game master");
                        else if (password == blueCommanderPassword)
                            answer[L"mission"][L"commandModeOptions"][L"commandMode"] = json::value(L"Blue commander");
                        else if (password == redCommanderPassword)
                            answer[L"mission"][L"commandModeOptions"][L"commandMode"] = json::value(L"Red commander");
                        else
                            answer[L"mission"][L"commandModeOptions"][L"commandMode"] = json::value(L"Observer");
                    }
                    else if (URI == COMMANDS_URI && req.has_param("commandHash"))
                    {
                        wstring wCommandHash = to_wstring(req.get_param_value("commandHash"));
                        answer[L"commandExecuted"] = json::value(scheduler->isCommandExecuted(req.get_param_value("commandHash")));
                        if (executionResults.has_field(wCommandHash))
                            answer[L"commandResult"] = executionResults[wCommandHash];
                        else
                            answer[L"commandResult"] = json::value::null();
                    }
                    else if (URI == DRAWINGS_URI && drawingsByLayer.has_object_field(L"drawings"))
                        answer[L"drawings"] = drawingsByLayer[L"drawings"];

                    answer[L"time"] = json::value::string(to_wstring(to_string(ms.count())));
                    answer[L"sessionHash"] = json::value::string(to_wstring(sessionHash));
                    answer[L"load"] = scheduler->getLoad();
                    answer[L"frameRate"] = scheduler->getFrameRate();

                    res.set_content(to_string(answer.serialize()), "application/json");
                }
            }
        }
        catch (const exception& e) {
            log(e.what());
            res.status = 500;
        }
    }
    else {
        res.status = 401;
    }

    set_cors_headers(res);
}

void Server::handle_put(const httplib::Request& req, httplib::Response& res)
{
    string username = extractUsername(req);
    string password = extractPassword(req);

    if (password != gameMasterPassword && password != blueCommanderPassword && password != redCommanderPassword) {
        res.status = 401;
        set_cors_headers(res);
        return;
    }

    auto answer = json::value::object();

    try {
        error_code ec;
        json::value jvalue = json::value::parse(req.body, ec);
        if (!ec && !jvalue.is_null())
        {
            lock_guard<mutex> guard(mutexLock);
            for (auto const& e : jvalue.as_object())
            {
                try {
                    scheduler->handleRequest(to_string(e.first), e.second, username, answer);
                }
                catch (const exception& e2) {
                    log(e2.what());
                }
            }
        }
    }
    catch (const exception& e) {
        log(e.what());
    }

    res.set_content(to_string(answer.serialize()), "application/json");
    set_cors_headers(res);
}

string Server::extractUsername(const httplib::Request& req)
{
    if (!req.has_header("Authorization")) return "";

    string authorization = req.get_header_value("Authorization");
    const string s = "Basic ";
    string::size_type i = authorization.find(s);
    if (i == string::npos) return "";
    authorization.erase(i, s.length());

    string decoded = from_base64(authorization);
    i = decoded.find(":");
    if (i == string::npos || i >= decoded.length()) return "";
    decoded.erase(i, decoded.length() - i);
    return decoded;
}

string Server::extractPassword(const httplib::Request& req)
{
    if (!req.has_header("Authorization")) return "";

    string authorization = req.get_header_value("Authorization");
    const string s = "Basic ";
    string::size_type i = authorization.find(s);
    if (i == string::npos) return "";
    authorization.erase(i, s.length());

    string decoded = from_base64(authorization);
    i = decoded.find(":");
    if (i == string::npos || i + 1 >= decoded.length()) return "";
    decoded.erase(0, i + 1);
    return decoded;
}

void Server::task()
{
    string host = "localhost";
    int port = 3001;
    string jsonLocation = instancePath + OLYMPUS_JSON_PATH;

    log("Reading configuration from " + jsonLocation);

    ifstream ifs(jsonLocation);
    stringstream ss;
    ss << ifs.rdbuf();
    error_code errorCode;
    json::value config = json::value::parse(ss.str(), errorCode);

    if (!errorCode && config.is_object() &&
        config.has_object_field(L"backend") &&
        config[L"backend"].has_string_field(L"address") &&
        config[L"backend"].has_number_field(L"port"))
    {
        host = to_string(config[L"backend"][L"address"]);
        port = config[L"backend"][L"port"].as_number().to_int32();
        log("Starting backend on " + host + ":" + to_string(port));
    }
    else
        log("Error reading configuration file. Starting backend on " + host + ":" + to_string(port));

    if (!errorCode && config.is_object() && config.has_object_field(L"authentication"))
    {
        auto& auth = config[L"authentication"];
        if (auth.has_string_field(L"gameMasterPassword"))
            gameMasterPassword = to_string(auth[L"gameMasterPassword"]);
        if (auth.has_string_field(L"blueCommanderPassword"))
            blueCommanderPassword = to_string(auth[L"blueCommanderPassword"]);
        if (auth.has_string_field(L"redCommanderPassword"))
            redCommanderPassword = to_string(auth[L"redCommanderPassword"]);
    }
    else
        log("Error reading configuration file. No password set.");

    httplib::Server svr;
    svr_ptr.store(&svr);

    svr.Options(R"(/olympus.*)", [this](const httplib::Request& req, httplib::Response& res) {
        handle_options(req, res);
    });
    svr.Get(R"(/olympus.*)", [this](const httplib::Request& req, httplib::Response& res) {
        handle_get(req, res);
    });
    svr.Put(R"(/olympus.*)", [this](const httplib::Request& req, httplib::Response& res) {
        handle_put(req, res);
    });

    log("RESTServer starting to listen on " + host + ":" + to_string(port));
    svr.listen(host.c_str(), port);
    log("RESTServer stopped listening");

    svr_ptr.store(nullptr);
}
