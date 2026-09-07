#include "../pchdef.h"
#include "playerbot.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotFactory.h"
#include "PlayerbotCommandServer.h"
#include <cstdlib>
#include <iostream>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <boost/bind.hpp>
#include <boost/smart_ptr.hpp>
#include <boost/asio.hpp>
#include <boost/thread/thread.hpp>


using namespace std;
using boost::asio::ip::tcp;
typedef boost::shared_ptr<tcp::socket> socket_ptr;

namespace
{
    // Remote commands are produced by the per-connection worker threads and
    // consumed on the world thread only: executing RandomPlayerbotMgr lookups,
    // AI context value access and player reads from arbitrary threads races the
    // world update and can corrupt/destroy shared core structures.
    struct RemoteCommandRequest
    {
        string line;
        string response;
        bool done = false;
    };

    mutex g_remoteMutex;
    condition_variable g_remoteCond;
    deque<shared_ptr<RemoteCommandRequest> > g_remoteQueue;
}

void PlayerbotCommandServer::ProcessPending()
{
    vector<shared_ptr<RemoteCommandRequest> > batch;
    {
        lock_guard<mutex> guard(g_remoteMutex);
        while (!g_remoteQueue.empty())
        {
            batch.push_back(g_remoteQueue.front());
            g_remoteQueue.pop_front();
        }
    }

    if (batch.empty())
        return;

    for (shared_ptr<RemoteCommandRequest> const& request : batch)
    {
        string response;
        try
        {
            response = sRandomPlayerbotMgr.HandleRemoteCommand(request->line);
        }
        catch (...)
        {
            response = "internal error";
        }

        lock_guard<mutex> guard(g_remoteMutex);
        request->response = response;
        request->done = true;
    }

    g_remoteCond.notify_all();
}

bool ReadLine(socket_ptr sock, string* buffer, string* line)
{
    // Do the real reading from fd until buffer has '\n'.
    string::iterator pos;
    while ((pos = find(buffer->begin(), buffer->end(), '\n')) == buffer->end())
    {
        // a client that never sends '\n' must not grow the buffer forever
        if (buffer->size() > 64 * 1024)
            return false;

        char buf[1024];
        boost::system::error_code error;
        size_t n = sock->read_some(boost::asio::buffer(buf), error);
        if (error == boost::asio::error::eof)
            return false;
        else if (error)
            throw boost::system::system_error(error); // Some other error.

        if (n >= sizeof(buf))
            return false;

        buf[n] = 0;
        *buffer += buf;
    }

    *line = string(buffer->begin(), pos);
    *buffer = string(pos + 1, buffer->end());
    return true;
}

void session(socket_ptr sock)
{
    try
    {
        string buffer, request;
        while (ReadLine(sock, &buffer, &request)) {
            shared_ptr<RemoteCommandRequest> command = make_shared<RemoteCommandRequest>();
            command->line = request;

            {
                lock_guard<mutex> guard(g_remoteMutex);
                g_remoteQueue.push_back(command);
            }

            {
                unique_lock<mutex> lock(g_remoteMutex);
                if (!g_remoteCond.wait_for(lock, chrono::seconds(10), [&command] { return command->done; }))
                    command->response = "busy - the world thread did not process the command in time";
            }

            string response = command->response + "\n";
            boost::asio::write(*sock, boost::asio::buffer(response.c_str(), response.size()));
            request = "";
        }
    }
    catch (std::exception& e)
    {
        TC_LOG_ERROR("playerbot",  "{}", e.what());
    }
}

// boost::asio::io_service was deprecated in Boost 1.66, suppressed by
// BOOST_ASIO_NO_DEPRECATED (which dep/boost/CMakeLists.txt defines for every
// target) and its header was removed outright in Boost 1.87.  io_context is the
// supported name and behaves identically for this synchronous accept loop.
void server(boost::asio::io_context& io_context, short port)
{
    tcp::acceptor a(io_context, tcp::endpoint(tcp::v4(), port));
    for (;;)
    {
        socket_ptr sock(new tcp::socket(io_context));
        a.accept(*sock);
        boost::thread t(boost::bind(session, sock));
    }
}

void Run()
{
    if (!sPlayerbotAIConfig.commandServerPort) {
        return;
    }

    ostringstream s; s << "Starting Playerbot Command Server on port " << sPlayerbotAIConfig.commandServerPort;
    TC_LOG_INFO("playerbot",  "{}", s.str().c_str());

    try
    {
        boost::asio::io_context io_context;
        server(io_context, sPlayerbotAIConfig.commandServerPort);
    }
    catch (std::exception& e)
    {
        TC_LOG_ERROR("playerbot",  "{}", e.what());
    }
}


void PlayerbotCommandServer::Start()
{
    thread serverThread(Run);
    serverThread.detach();
}
