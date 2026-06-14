#pragma once

#include <atomic>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace utils {

namespace beast = boost::beast;
namespace http = beast::http;

/// Formats metrics in the Prometheus exposition format.
///
/// Call addGauge/addCounter/addHistogram for each metric, then
/// complete() to retrieve the full text-format payload ready for
/// an HTTP /metrics response.
class PrometheusFormatter final {
public:
    /// Appends a Gauge metric in Prometheus text format.
    void addGauge(const Gauge& g) {
        appendHelp(g.name(), g.help());
        appendType(g.name(), "gauge");
        buf << g.name() << ' ' << g.value() << '\n';
        buf << '\n';
    }

    /// Appends a Counter metric in Prometheus text format.
    void addCounter(const Counter& c) {
        appendHelp(c.name(), c.help());
        appendType(c.name(), "counter");
        buf << c.name() << ' ' << c.value() << '\n';
        buf << '\n';
    }

    /// Appends a Histogram metric in Prometheus text format.
    void addHistogram(const Histogram& h) {
        appendHelp(h.name(), h.help());
        appendType(h.name(), "histogram");

        const auto bucket_vals = h.bucketValues();
        const size_t n = h.bucketCount();
        for (size_t i = 0; i < n; ++i) {
            buf << h.name() << "_bucket{le=\"" << h.bucketBound(i) << "\"} " << bucket_vals[i]
                << '\n';
        }
        buf << h.name() << "_bucket{le=\"+Inf\"} " << bucket_vals[n] << '\n';
        buf << h.name() << "_sum " << h.sum() << '\n';
        buf << h.name() << "_count " << h.count() << '\n';
        buf << '\n';
    }

    /// Returns the accumulated Prometheus text and resets internal state.
    std::string complete() { return std::move(buf).str(); }

private:
    void appendHelp(const std::string& name, const std::string& help) {
        if (!help.empty()) {
            buf << "# HELP " << name << ' ' << help << '\n';
        }
    }

    void appendType(const std::string& name, const std::string& type) {
        buf << "# TYPE " << name << ' ' << type << '\n';
    }

    std::ostringstream buf;
};

/// MetricsServer  —  Prometheus HTTP server backed by Boost.Beast
///
/// Runs a background thread that serves GET /metrics in Prometheus text
/// format over HTTP/1.1.  HTTP parsing and serialisation are handled by
/// Boost.Beast — no hand-rolled HTTP logic.
///
/// The caller supplies a callback that returns the current metric values
/// on each scrape.  This avoids coupling the server to any particular
/// metric collection strategy.
///
/// Designed to be started once at initialisation and stopped during
/// graceful shutdown.
class MetricsServer final {
public:
    /// Callback invoked on every /metrics scrape.
    /// Must return the Prometheus text-format payload.
    using MetricsCallback = std::function<std::string()>;

    /// Configuration for the HTTP server socket.
    struct Config {
        std::string bind_address = "0.0.0.0";
        uint16_t port = 9090;
    };

    /// Constructs but does not start the server.
    ///
    /// @param callback  Invoked on each /metrics request to produce the
    ///                  response body.
    /// @param config    Bind address and port.
    explicit MetricsServer(MetricsCallback callback, Config config) noexcept
        : callback(std::move(callback)), config(std::move(config)) {}

    ~MetricsServer() { stop(); }

    MetricsServer(const MetricsServer&) = delete;
    MetricsServer& operator=(const MetricsServer&) = delete;

    /// Starts the server on a background thread.
    ///
    /// @return true on success, false if the socket could not be bound.
    [[nodiscard]] bool start() noexcept {
        if (running.load(std::memory_order_acquire))
            return true;

        try {
            io_context = std::make_unique<boost::asio::io_context>();
            acceptor = std::make_unique<boost::asio::ip::tcp::acceptor>(
                *io_context, boost::asio::ip::tcp::endpoint(
                                 boost::asio::ip::make_address(config.bind_address), config.port));

            // Disable Nagle's algorithm — metrics payloads are small and
            // we want them sent immediately.
            acceptor->set_option(boost::asio::ip::tcp::no_delay(true));

            doAccept();

            running.store(true, std::memory_order_release);
            thread = std::thread(&MetricsServer::run, this);
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }

    /// Signals the server thread to stop and joins it.
    void stop() noexcept {
        running.store(false, std::memory_order_release);
        if (acceptor) {
            boost::system::error_code ec;
            acceptor->cancel(ec);
            acceptor->close(ec);
        }
        if (io_context) {
            io_context->stop();
        }
        if (thread.joinable()) {
            thread.join();
        }
        acceptor.reset();
        io_context.reset();
    }

    /// Returns true if the server thread is running.
    [[nodiscard]] bool isRunning() const noexcept {
        return running.load(std::memory_order_acquire);
    }

    /// The port the server is actually listening on.
    [[nodiscard]] uint16_t port() const noexcept { return config.port; }

private:
    /// Runs the Boost.Asio event loop on the background thread.
    void run() noexcept { io_context->run(); }

    /// Initiates an async accept; dispatches to handleClient on success
    /// and re-arms itself while running.
    void doAccept() noexcept {
        acceptor->async_accept(
            [this](boost::system::error_code ec, boost::asio::ip::tcp::socket socket) {
                if (!ec) {
                    handleClient(std::move(socket));
                }
                if (running.load(std::memory_order_acquire)) {
                    doAccept();
                }
            });
    }

    /// Reads an HTTP request via Boost.Beast, invokes the callback for
    /// /metrics, and writes a proper HTTP response.
    void handleClient(boost::asio::ip::tcp::socket socket) noexcept {
        try {
            beast::flat_buffer buffer;
            http::request<http::string_body> req;
            boost::system::error_code ec;

            http::read(socket, buffer, req, ec);
            if (ec)
                return;

            http::response<http::string_body> res;

            if (req.method() != http::verb::get || req.target() != "/metrics") {
                res.result(http::status::not_found);
                res.set(http::field::content_length, "0");
                res.keep_alive(false);
            } else {
                std::string body = callback();
                res.result(http::status::ok);
                res.set(http::field::content_type, "text/plain; version=0.0.4");
                res.keep_alive(false);
                res.body() = std::move(body);
                res.prepare_payload();
            }

            http::write(socket, res, ec);
        } catch (...) {
            // Silently ignore any error in the metrics server path.
        }
    }

    MetricsCallback callback;
    Config config;
    std::atomic<bool> running{false};
    std::unique_ptr<boost::asio::io_context> io_context;
    std::unique_ptr<boost::asio::ip::tcp::acceptor> acceptor;
    std::thread thread;
};

}  // namespace utils
