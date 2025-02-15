#include <boost/asio.hpp>
#include <fmt/core.h>

using namespace std::literals;

struct publisher {
    std::chrono::milliseconds duration{};

    boost::asio::awaitable<void> send(std::string_view msg)
    {
        boost::asio::steady_timer timer{co_await boost::asio::this_coro::executor, duration};
        co_await timer.async_wait(boost::asio::use_awaitable);
        fmt::print("sent: {}\n", msg);
    }
};

struct subscriber {
    std::chrono::milliseconds duration{};

    template<typename T>
    boost::asio::awaitable<void> recv(T& t)
    {
        boost::asio::steady_timer timer{co_await boost::asio::this_coro::executor, duration};
        co_await timer.async_wait(boost::asio::use_awaitable);
        t = T{};
    }
};

struct other_service {
    std::chrono::milliseconds duration{};
    std::string msg{};

    boost::asio::awaitable<std::string> request()
    {
        boost::asio::steady_timer timer{co_await boost::asio::this_coro::executor, duration};
        co_await timer.async_wait(boost::asio::use_awaitable);
        co_return msg;
    }
};

struct model {
    publisher pub{};
    subscriber sub1{};
    subscriber sub2{};
    other_service service{};
};

struct msg_1 {
};
struct msg_2 {
};

struct event_handler {
    model& m;

    boost::asio::awaitable<void> process(msg_1)
    {
        thread_local int count = 0;
        fmt::print("recv: msg_1 {}\n", ++count);
        auto request = co_await m.service.request();
        fmt::print("recv: for msg_1: {} {}\n", request, count);
        co_await m.pub.send(fmt::format("msg_1 {}", count));
    }

    boost::asio::awaitable<void> process(msg_2)
    {
        thread_local int count = 0;
        fmt::print("recv: msg_2 {}\n", ++count);
        auto request = co_await m.service.request();
        fmt::print("recv: for msg_2: {} {}\n", request, count);
        co_await m.pub.send(fmt::format("msg_2 {}", count));
    }
};

template<typename T>
boost::asio::awaitable<void> receive_messages(std::stop_token stop_token, subscriber& sub, event_handler& handler)
{
    while (!stop_token.stop_requested())
    {
        T msg;
        co_await sub.recv(msg);
        co_await handler.process(msg);
    }
}

int main()
{
    boost::asio::io_context io_context;
    std::stop_source stop_source;

    boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
    signals.async_wait([&io_context, &stop_source](const auto&, auto) {
        fmt::print("signal caught!\n");
        stop_source.request_stop();
        io_context.stop();
    });

    model m{
        .pub{100ms},
        .sub1{1s},
        .sub2{3s},
        .service{500ms, "requested msg"},
    };

    event_handler handler{m};

    boost::asio::co_spawn(
        io_context,
        [&stop_source, &m, &handler]() { return receive_messages<msg_1>(stop_source.get_token(), m.sub1, handler); },
        boost::asio::detached);

    boost::asio::co_spawn(
        io_context,
        [&stop_source, &m, &handler]() { return receive_messages<msg_2>(stop_source.get_token(), m.sub2, handler); },
        boost::asio::detached);

    io_context.run();
}
