#include "../pchdef.h"
#include "playerbot.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotFactory.h"
#include "PlayerbotCommandServer.h"
#include <cstdlib>
#include <iostream>
#include <boost/bind.hpp>
#include <boost/smart_ptr.hpp>
#include <boost/asio.hpp>
#include <boost/thread/thread.hpp>


using namespace std;
using boost::asio::ip::tcp;
typedef boost::shared_ptr<tcp::socket> socket_ptr;

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
            string response = sRandomPlayerbotMgr.HandleRemoteCommand(request) + "\n";
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
