#include <azmq/socket.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <spdlog/spdlog.h>

#include <expected>

namespace {
struct Subscriber {
    azmq::sub_socket socket;

    auto async_recv() -> boost::asio::awaitable<std::expected<std::string, std::string>>
    {
        std::array<char, 4096> buffer;
        try
        {
            std::size_t bytes_received = co_await azmq::async_receive(socket, boost::asio::buffer(buffer), boost::asio::use_awaitable);
            co_return std::string(buffer.data(), bytes_received);
        }
        catch (const boost::system::system_error& e)
        {
            const boost::system::error_code& ec = e.code();
            co_return std::unexpected(ec.message());
        }
    }
};

auto create_sub_socket(boost::asio::io_context& io_context, std::string addr, std::string topic) -> std::expected<azmq::sub_socket, std::string>
{
    static constexpr bool optimize_single_threaded = true;
    azmq::sub_socket socket{io_context, optimize_single_threaded};

    boost::system::error_code ec;
    if (socket.connect(addr, ec))
    {
        return std::unexpected(ec.message());
    }
    if (socket.set_option(azmq::socket::subscribe(topic), ec))
    {
        return std::unexpected(ec.message());
    }

    return socket;
}

struct Publisher {
    azmq::pub_socket socket;
    std::string topic;

    auto async_send(std::string msg) -> boost::asio::awaitable<std::optional<std::string>>
    {
        try
        {
            co_await azmq::async_send(
                socket,
                boost::asio::buffer(topic + msg),
                boost::asio::use_awaitable);
            co_return std::nullopt;
        }
        catch (const boost::system::system_error& e)
        {
            const boost::system::error_code& ec = e.code();
            co_return ec.message();
        }
    }
};

auto create_pub_socket(boost::asio::io_context& io_context, std::string addr) -> std::expected<azmq::pub_socket, std::string>
{
    static constexpr bool optimize_single_threaded = true;
    azmq::pub_socket socket{io_context, optimize_single_threaded};

    boost::system::error_code ec;
    if (socket.bind(addr, ec))
    {
        return std::unexpected(ec.message());
    }

    return socket;
}

struct Model {
    Subscriber sub1;
    Subscriber sub2;
    Publisher pub;
};

struct Event_handler {
    Model& model;

    boost::asio::awaitable<void> process(std::string msg)
    {
        spdlog::info("recv: {}", msg);

        // do blocking work
        std::this_thread::sleep_for(std::chrono::milliseconds{100});

        if (auto result = co_await model.pub.async_send(msg); result)
        {
            spdlog::error(*result);
        }

        spdlog::info("sent: {}", std::move(msg));
    }
};

boost::asio::awaitable<void> receive_messages(std::stop_token stop_token, Subscriber& sub, Event_handler& handler)
{
    while (!stop_token.stop_requested())
    {
        auto msg = co_await sub.async_recv();

        if (!msg)
        {
            spdlog::error(msg.error());
            continue;
        }

        co_await handler.process(std::move(*msg));
    }
}
}// namespace

int main()
{
    boost::asio::io_context io_context;
    std::stop_source stop_source;

    boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
    signals.async_wait([&io_context, &stop_source](const auto&, auto) {
        spdlog::info("signal caught!");
        stop_source.request_stop();
        io_context.stop();
    });

    auto sub1 = create_sub_socket(io_context, "tcp://127.0.0.1:5556", "FOO");
    if (!sub1)
    {
        spdlog::error(sub1.error());
        return EXIT_FAILURE;
    }

    auto sub2 = create_sub_socket(io_context, "tcp://127.0.0.1:5557", "BAR");
    if (!sub2)
    {
        spdlog::error(sub2.error());
        return EXIT_FAILURE;
    }

    auto pub = create_pub_socket(io_context, "tcp://127.0.0.1:5558");
    if (!pub)
    {
        spdlog::error(pub.error());
        return EXIT_FAILURE;
    }

    Model model{
        .sub1{std::move(*sub1)},
        .sub2{std::move(*sub2)},
        .pub{std::move(*pub), "TMP"},
    };

    Event_handler handler{model};

    boost::asio::co_spawn(
        io_context,
        [&stop_source, &model, &handler]() { return receive_messages(stop_source.get_token(), model.sub1, handler); },
        boost::asio::detached);

    boost::asio::co_spawn(
        io_context,
        [&stop_source, &model, &handler]() { return receive_messages(stop_source.get_token(), model.sub2, handler); },
        boost::asio::detached);

    ///////// start testing /////////
    std::jthread([stop_token = stop_source.get_token()] {
        boost::asio::io_context tester_io_context;
        azmq::pub_socket test_pub{tester_io_context};
        test_pub.bind("tcp://127.0.0.1:5556");
        while (!stop_token.stop_requested())
        {
            std::this_thread::sleep_for(std::chrono::seconds{1});
            test_pub.send(boost::asio::buffer(std::string{"FOOBAR"}));
        }
    }).detach();

    std::jthread([stop_token = stop_source.get_token()] {
        boost::asio::io_context tester_io_context;
        azmq::pub_socket test_pub{tester_io_context};
        test_pub.bind("tcp://127.0.0.1:5557");
        while (!stop_token.stop_requested())
        {
            std::this_thread::sleep_for(std::chrono::seconds{3});
            test_pub.send(boost::asio::buffer(std::string{"BARFOO"}));
        }
    }).detach();

    std::jthread([stop_token = stop_source.get_token()] {
        boost::asio::io_context tester_io_context;
        azmq::sub_socket test_sub{tester_io_context};
        test_sub.connect("tcp://127.0.0.1:5558");
        test_sub.set_option(azmq::socket::subscribe("TMP"));
        while (!stop_token.stop_requested())
        {
            std::array<char, 256> buf;
            std::size_t size = test_sub.receive(boost::asio::buffer(buf));
            spdlog::debug("recv: {}", std::string(buf.data(), size));
        }
    }).detach();
    ///////// end testing /////////

    io_context.run();

    return EXIT_SUCCESS;
}
