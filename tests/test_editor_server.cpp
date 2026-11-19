#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

#include "insys/asset/json.h"
#include "insys/editor/editor_server.h"
#include "insys/remoting_tcp.h"

using namespace insys;

namespace {

std::filesystem::path make_temp_directory(std::string_view name) {
    const auto stamp = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / (std::string(name) + "_" + stamp);
    std::filesystem::create_directories(path);
    return path;
}

void write_text(const std::filesystem::path& path, std::string_view text) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    REQUIRE(stream.good());
}

std::string lower(std::string value) {
    for (char& character : value) {
        if (character >= 'A' && character <= 'Z')
            character = static_cast<char>(character - 'A' + 'a');
    }
    return value;
}

struct HttpResponse {
    int status = 0;
    std::string headers;
    std::string body;
};

class RawClient {
public:
    bool connect(std::uint16_t port, std::string& error) {
        m_stream = TcpTransport::connect("127.0.0.1", port, &error);
        return m_stream != nullptr;
    }

    bool send_raw(std::string_view data) {
        return m_stream && m_stream->send_raw(data.data(), data.size());
    }

    bool read_http(HttpResponse& response, std::string& error) {
        std::string header;
        if (!read_until("\r\n\r\n", header, error))
            return false;
        const std::size_t lineEnd = header.find("\r\n");
        if (lineEnd == std::string::npos) {
            error = "malformed HTTP response";
            return false;
        }
        const std::string statusLine = header.substr(0, lineEnd);
        const std::size_t firstSpace = statusLine.find(' ');
        if (firstSpace == std::string::npos) {
            error = "malformed HTTP status";
            return false;
        }
        response.status = std::stoi(statusLine.substr(firstSpace + 1, 3));
        response.headers = header;

        std::size_t contentLength = 0;
        std::size_t offset = lineEnd + 2;
        while (offset < header.size()) {
            const std::size_t end = header.find("\r\n", offset);
            if (end == std::string::npos || end == offset)
                break;
            const std::string line = header.substr(offset, end - offset);
            const std::size_t colon = line.find(':');
            if (colon != std::string::npos &&
                lower(line.substr(0, colon)) == "content-length") {
                contentLength = static_cast<std::size_t>(
                    std::stoul(line.substr(colon + 1)));
            }
            offset = end + 2;
        }
        response.body.clear();
        if (contentLength > 0 && !read_exact(contentLength, response.body, error))
            return false;
        return true;
    }

    bool websocket_handshake(std::uint16_t port, std::string_view token,
                             std::string_view origin, HttpResponse& response,
                             std::string& error) {
        std::string request = "GET /ws?token=";
        request += token;
        request += " HTTP/1.1\r\n";
        if (!origin.empty()) {
            request += "Origin: ";
            request += origin;
            request += "\r\n";
        }
        request += "Host: 127.0.0.1:";
        request += std::to_string(port);
        request += "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n";
        request += "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n";
        request += "Sec-WebSocket-Version: 13\r\n\r\n";
        if (!send_raw(request))
            return false;
        return read_http(response, error);
    }

    bool send_text(std::string_view text) {
        std::string frame;
        frame.push_back(static_cast<char>(0x81));
        const std::size_t size = text.size();
        if (size < 126) {
            frame.push_back(static_cast<char>(0x80u | size));
        } else if (size <= 0xffffu) {
            frame.push_back(static_cast<char>(0x80u | 126u));
            frame.push_back(static_cast<char>((size >> 8u) & 0xffu));
            frame.push_back(static_cast<char>(size & 0xffu));
        } else {
            frame.push_back(static_cast<char>(0x80u | 127u));
            const std::uint64_t length = static_cast<std::uint64_t>(size);
            for (int i = 7; i >= 0; --i)
                frame.push_back(static_cast<char>((length >> (i * 8)) & 0xffu));
        }
        const char mask[4] = {0x12, 0x34, 0x56, 0x78};
        frame.append(mask, 4);
        for (std::size_t i = 0; i < text.size(); ++i)
            frame.push_back(static_cast<char>(text[i] ^ mask[i % 4]));
        return send_raw(frame);
    }

    bool read_text(std::string& text, std::string& error) {
        for (;;) {
            std::string header;
            if (!read_exact(2, header, error))
                return false;
            const std::uint8_t first = static_cast<std::uint8_t>(header[0]);
            const std::uint8_t second = static_cast<std::uint8_t>(header[1]);
            const std::uint8_t opcode = first & 0x0fu;
            std::uint64_t length = second & 0x7fu;
            if (length == 126) {
                std::string extended;
                if (!read_exact(2, extended, error))
                    return false;
                length = (static_cast<std::uint64_t>(static_cast<std::uint8_t>(extended[0]))
                          << 8u) |
                         static_cast<std::uint8_t>(extended[1]);
            } else if (length == 127) {
                std::string extended;
                if (!read_exact(8, extended, error))
                    return false;
                length = 0;
                for (int i = 0; i < 8; ++i)
                    length = (length << 8u) |
                             static_cast<std::uint8_t>(extended[static_cast<std::size_t>(i)]);
            }
            std::string payload;
            if (!read_exact(static_cast<std::size_t>(length), payload, error))
                return false;
            if (opcode == 0x8) {
                error = "server closed WebSocket";
                return false;
            }
            if (opcode == 0x9 || opcode == 0xa)
                continue;
            text = std::move(payload);
            return true;
        }
    }

private:
    bool fill(std::string& error) {
        std::uint8_t buffer[4096];
        std::size_t received = 0;
        if (m_stream->receive_raw(buffer, sizeof(buffer), received)) {
            m_buffer.append(reinterpret_cast<const char*>(buffer), received);
            return true;
        }
        if (!m_stream->connected()) {
            error = "connection closed";
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return true;
    }

    bool read_until(std::string_view marker, std::string& out, std::string& error) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (true) {
            const std::size_t position = m_buffer.find(marker);
            if (position != std::string::npos) {
                const std::size_t length = position + marker.size();
                out = m_buffer.substr(0, length);
                m_buffer.erase(0, length);
                return true;
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                error = "read timeout";
                return false;
            }
            if (!fill(error))
                return false;
        }
    }

    bool read_exact(std::size_t count, std::string& out, std::string& error) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (m_buffer.size() < count) {
            if (std::chrono::steady_clock::now() >= deadline) {
                error = "read timeout";
                return false;
            }
            if (!fill(error))
                return false;
        }
        out = m_buffer.substr(0, count);
        m_buffer.erase(0, count);
        return true;
    }

    std::unique_ptr<TcpTransport> m_stream;
    std::string m_buffer;
};

HttpResponse http_get(std::uint16_t port, std::string_view path) {
    RawClient client;
    std::string error;
    REQUIRE(client.connect(port, error));
    const std::string request = "GET " + std::string(path) +
                                " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
    REQUIRE(client.send_raw(request));
    HttpResponse response;
    REQUIRE(client.read_http(response, error));
    return response;
}

std::string string_field(const asset::JsonValue& value, std::string_view name) {
    const asset::JsonValue* field = value.find(name);
    REQUIRE(field != nullptr);
    REQUIRE(field->is_string());
    return field->string;
}

std::string rpc_request(RawClient& client, std::string request) {
    std::string error;
    REQUIRE(client.send_text(request));
    std::string response;
    REQUIRE(client.read_text(response, error));
    asset::JsonValue value;
    asset::JsonParseError parseError;
    REQUIRE(asset::parse_json(response, value, parseError));
    return response;
}

} // namespace

TEST_CASE("Editor server serves static files and WebSocket JSON-RPC") {
    const std::filesystem::path staticRoot = make_temp_directory("insys_editor_static");
    const std::filesystem::path assetRoot = make_temp_directory("insys_editor_assets");
    write_text(staticRoot / "index.html", "<html><body>editor smoke</body></html>");
    write_text(assetRoot / "player.json",
               R"({"version":1,"maps":[{"name":"Player","actions":[{"name":"Jump"}],)"
               R"("bindings":[{"action":"Jump","path":"<Keyboard>/space"}]}]})");

    editor::EditorServerOptions options;
    options.port = 0;
    options.token = "test-token";
    options.staticRoot = staticRoot;
    options.session.assetRoots = {assetRoot};
    editor::EditorServer server(std::move(options));

    std::string error;
    REQUIRE(server.start(&error));
    const std::uint16_t port = server.port();
    REQUIRE(port != 0);

    std::thread runner([&server] { (void)server.run(); });
    struct Guard {
        editor::EditorServer& server;
        std::thread& runner;
        ~Guard() {
            server.stop();
            if (runner.joinable())
                runner.join();
        }
    } guard{server, runner};

    const HttpResponse page = http_get(port, "/");
    REQUIRE(page.status == 200);
    REQUIRE(page.body.find("editor smoke") != std::string::npos);

    RawClient badToken;
    REQUIRE(badToken.connect(port, error));
    HttpResponse badTokenResponse;
    REQUIRE(badToken.websocket_handshake(port, "wrong-token", "", badTokenResponse, error));
    REQUIRE(badTokenResponse.status == 403);

    RawClient badOrigin;
    REQUIRE(badOrigin.connect(port, error));
    HttpResponse badOriginResponse;
    REQUIRE(badOrigin.websocket_handshake(port, "test-token", "http://evil.example",
                                          badOriginResponse, error));
    REQUIRE(badOriginResponse.status == 403);

    RawClient websocket;
    REQUIRE(websocket.connect(port, error));
    HttpResponse handshake;
    REQUIRE(websocket.websocket_handshake(
        port, "test-token", "http://127.0.0.1:" + std::to_string(port), handshake, error));
    REQUIRE(handshake.status == 101);

    const std::string hello = rpc_request(
        websocket,
        R"({"jsonrpc":"2.0","id":1,"method":"session.hello","params":{"protocolVersion":"1"}})");
    asset::JsonValue helloValue;
    asset::JsonParseError parseError;
    REQUIRE(asset::parse_json(hello, helloValue, parseError));
    REQUIRE(string_field(*helloValue.find("result"), "protocolVersion") == "1");

    const std::string opened = rpc_request(
        websocket,
        R"({"jsonrpc":"2.0","id":2,"method":"asset.open","params":{"path":"player.json"}})");
    asset::JsonValue openedValue;
    REQUIRE(asset::parse_json(opened, openedValue, parseError));
    REQUIRE(string_field(*openedValue.find("result"), "revision").rfind("sha256:", 0) == 0);

    const std::string shutdown = rpc_request(
        websocket, R"({"jsonrpc":"2.0","id":3,"method":"app.shutdown"})");
    asset::JsonValue shutdownValue;
    REQUIRE(asset::parse_json(shutdown, shutdownValue, parseError));
    REQUIRE(shutdownValue.find("result")->find("ok")->boolean == true);
}
