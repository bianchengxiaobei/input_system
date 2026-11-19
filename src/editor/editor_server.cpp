#include "insys/editor/editor_server.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "insys/editor/editor_protocol.h"
#include "insys/remoting_tcp.h"

namespace insys::editor {
namespace {

constexpr std::string_view kWebSocketGuid =
    "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
constexpr std::size_t kMaxHttpHeaderBytes = 64u * 1024u;
constexpr std::size_t kMaxStaticFileBytes = 64u * 1024u * 1024u;
constexpr std::uint8_t kOpContinuation = 0x0;
constexpr std::uint8_t kOpText = 0x1;
constexpr std::uint8_t kOpBinary = 0x2;
constexpr std::uint8_t kOpClose = 0x8;
constexpr std::uint8_t kOpPing = 0x9;
constexpr std::uint8_t kOpPong = 0xa;
constexpr const char* kFallbackHtml =
    "<!doctype html><html><head><meta charset=\"utf-8\">"
    "<title>insys editor</title></head><body>"
    "<h1>insys editor server</h1>"
    "<p>The web frontend has not been built. Run "
    "<code>npm install</code> and <code>npm run build</code> in "
    "<code>editor/web</code>, or use <code>--static</code>.</p>"
    "</body></html>";

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string trim(std::string value) {
    const auto notSpace = [](unsigned char character) { return !std::isspace(character); };
    auto begin = std::find_if(value.begin(), value.end(), notSpace);
    auto end = std::find_if(value.rbegin(), value.rend(), notSpace).base();
    if (begin >= end)
        return {};
    return std::string(begin, end);
}

std::string header_value(const std::string& headers, std::string_view name) {
    const std::string wanted = lower(std::string(name));
    std::size_t offset = 0;
    const std::size_t firstEnd = headers.find("\r\n");
    if (firstEnd != std::string::npos &&
        headers.substr(0, firstEnd).find(':') == std::string::npos) {
        offset = firstEnd + 2;
    }
    while (offset < headers.size()) {
        const std::size_t end = headers.find("\r\n", offset);
        if (end == std::string::npos || end == offset)
            break;
        const std::string line = headers.substr(offset, end - offset);
        const std::size_t colon = line.find(':');
        if (colon != std::string::npos && lower(trim(line.substr(0, colon))) == wanted)
            return trim(line.substr(colon + 1));
        offset = end + 2;
    }
    return {};
}

std::uint32_t rotate_left(std::uint32_t value, int bits) {
    return (value << bits) | (value >> (32 - bits));
}

void sha1(const std::uint8_t* data, std::size_t size, std::uint8_t out[20]) {
    std::vector<std::uint8_t> message(data, data + size);
    const std::uint64_t bitLength = static_cast<std::uint64_t>(size) * 8u;
    message.push_back(0x80u);
    while (message.size() % 64u != 56u)
        message.push_back(0u);
    for (int i = 7; i >= 0; --i)
        message.push_back(static_cast<std::uint8_t>((bitLength >> (i * 8)) & 0xffu));

    std::uint32_t h0 = 0x67452301u;
    std::uint32_t h1 = 0xefcdab89u;
    std::uint32_t h2 = 0x98badcfeu;
    std::uint32_t h3 = 0x10325476u;
    std::uint32_t h4 = 0xc3d2e1f0u;

    for (std::size_t chunk = 0; chunk < message.size(); chunk += 64u) {
        std::uint32_t w[80]{};
        for (int i = 0; i < 16; ++i) {
            const std::size_t offset = chunk + static_cast<std::size_t>(i) * 4u;
            w[i] = (static_cast<std::uint32_t>(message[offset]) << 24u) |
                   (static_cast<std::uint32_t>(message[offset + 1]) << 16u) |
                   (static_cast<std::uint32_t>(message[offset + 2]) << 8u) |
                   static_cast<std::uint32_t>(message[offset + 3]);
        }
        for (int i = 16; i < 80; ++i)
            w[i] = rotate_left(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

        std::uint32_t a = h0;
        std::uint32_t b = h1;
        std::uint32_t c = h2;
        std::uint32_t d = h3;
        std::uint32_t e = h4;
        for (int i = 0; i < 80; ++i) {
            std::uint32_t f = 0;
            std::uint32_t k = 0;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5a827999u;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ed9eba1u;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8f1bbcdcu;
            } else {
                f = b ^ c ^ d;
                k = 0xca62c1d6u;
            }
            const std::uint32_t temp = rotate_left(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rotate_left(b, 30);
            b = a;
            a = temp;
        }

        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }

    const std::uint32_t hash[5] = {h0, h1, h2, h3, h4};
    for (int i = 0; i < 5; ++i) {
        out[i * 4] = static_cast<std::uint8_t>((hash[i] >> 24u) & 0xffu);
        out[i * 4 + 1] = static_cast<std::uint8_t>((hash[i] >> 16u) & 0xffu);
        out[i * 4 + 2] = static_cast<std::uint8_t>((hash[i] >> 8u) & 0xffu);
        out[i * 4 + 3] = static_cast<std::uint8_t>(hash[i] & 0xffu);
    }
}

std::string base64_encode(const std::uint8_t* data, std::size_t size) {
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((size + 2) / 3 * 4);
    for (std::size_t i = 0; i < size; i += 3) {
        const std::uint32_t b0 = data[i];
        const std::uint32_t b1 = i + 1 < size ? data[i + 1] : 0;
        const std::uint32_t b2 = i + 2 < size ? data[i + 2] : 0;
        const std::uint32_t triple = (b0 << 16u) | (b1 << 8u) | b2;
        out.push_back(kAlphabet[(triple >> 18u) & 0x3fu]);
        out.push_back(kAlphabet[(triple >> 12u) & 0x3fu]);
        out.push_back(i + 1 < size ? kAlphabet[(triple >> 6u) & 0x3fu] : '=');
        out.push_back(i + 2 < size ? kAlphabet[triple & 0x3fu] : '=');
    }
    return out;
}

std::string websocket_accept_key(std::string_view key) {
    std::string source(key);
    source.append(kWebSocketGuid);
    std::uint8_t digest[20]{};
    sha1(reinterpret_cast<const std::uint8_t*>(source.data()), source.size(), digest);
    return base64_encode(digest, sizeof(digest));
}

std::string random_token() {
    static constexpr char kHex[] = "0123456789abcdef";
    std::random_device device;
    std::mt19937 generator(device());
    std::uniform_int_distribution<int> distribution(0, 255);
    std::string token;
    token.reserve(48);
    for (int i = 0; i < 24; ++i) {
        const int value = distribution(generator);
        token.push_back(kHex[(value >> 4) & 0xf]);
        token.push_back(kHex[value & 0xf]);
    }
    return token;
}

bool constant_time_equal(std::string_view left, std::string_view right) {
    if (left.size() != right.size())
        return false;
    unsigned char difference = 0;
    for (std::size_t i = 0; i < left.size(); ++i)
        difference |= static_cast<unsigned char>(left[i] ^ right[i]);
    return difference == 0;
}

bool is_within(const std::filesystem::path& candidate,
               const std::filesystem::path& root) {
    const std::filesystem::path relative = candidate.lexically_relative(root);
    if (relative.empty() || relative == ".")
        return false;
    if (relative.is_absolute())
        return false;
    for (const std::filesystem::path& component : relative) {
        if (component == "..")
            return false;
    }
    return true;
}

std::string mime_type(const std::filesystem::path& path) {
    const std::string extension = lower(path.extension().string());
    if (extension == ".html" || extension == ".htm") return "text/html; charset=utf-8";
    if (extension == ".js" || extension == ".mjs") return "text/javascript; charset=utf-8";
    if (extension == ".css") return "text/css; charset=utf-8";
    if (extension == ".json" || extension == ".map") return "application/json; charset=utf-8";
    if (extension == ".svg") return "image/svg+xml";
    if (extension == ".png") return "image/png";
    if (extension == ".jpg" || extension == ".jpeg") return "image/jpeg";
    if (extension == ".gif") return "image/gif";
    if (extension == ".ico") return "image/x-icon";
    if (extension == ".woff") return "font/woff";
    if (extension == ".woff2") return "font/woff2";
    return "application/octet-stream";
}

std::string http_response(int status, std::string_view reason,
                          std::string_view contentType, std::string body,
                          bool includeBody = true) {
    std::ostringstream response;
    response << "HTTP/1.1 " << status << " " << reason << "\r\n";
    if (!contentType.empty())
        response << "Content-Type: " << contentType << "\r\n";
    response << "Content-Length: " << body.size() << "\r\n";
    response << "Cache-Control: no-store\r\n";
    response << "X-Content-Type-Options: nosniff\r\n";
    response << "Referrer-Policy: no-referrer\r\n";
    response << "Content-Security-Policy: default-src 'self'; script-src 'self'; "
                "style-src 'self' 'unsafe-inline'; connect-src 'self' "
                "ws://127.0.0.1:* ws://localhost:*; img-src 'self' data:; "
                "font-src 'self'; base-uri 'none'; form-action 'none'\r\n";
    response << "Connection: close\r\n\r\n";
    if (includeBody)
        response << body;
    return response.str();
}

std::string http_error(int status, std::string_view reason, std::string message) {
    return http_response(status, reason, "text/plain; charset=utf-8",
                         std::move(message));
}

bool send_all(insys::TcpTransport& stream, std::string_view data) {
    return stream.send_raw(data.data(), data.size());
}

bool percent_decode(std::string_view encoded, std::string& decoded) {
    decoded.clear();
    decoded.reserve(encoded.size());
    for (std::size_t i = 0; i < encoded.size(); ++i) {
        if (encoded[i] != '%') {
            decoded.push_back(encoded[i]);
            continue;
        }
        if (i + 2 >= encoded.size())
            return false;
        const auto hex = [](char character) -> int {
            if (character >= '0' && character <= '9')
                return character - '0';
            if (character >= 'a' && character <= 'f')
                return character - 'a' + 10;
            if (character >= 'A' && character <= 'F')
                return character - 'A' + 10;
            return -1;
        };
        const int high = hex(encoded[i + 1]);
        const int low = hex(encoded[i + 2]);
        if (high < 0 || low < 0)
            return false;
        decoded.push_back(static_cast<char>((high << 4) | low));
        i += 2;
    }
    return true;
}

void split_target(std::string_view target, std::string& path, std::string& query) {
    const std::size_t question = target.find('?');
    path.assign(target.substr(0, question));
    query.assign(question == std::string_view::npos ? std::string_view{}
                                                    : target.substr(question + 1));
}

std::string query_value(std::string_view query, std::string_view name) {
    std::size_t offset = 0;
    while (offset <= query.size()) {
        const std::size_t end = query.find('&', offset);
        const std::string_view item = query.substr(
            offset, end == std::string_view::npos ? std::string_view::npos : end - offset);
        const std::size_t equals = item.find('=');
        if (equals != std::string_view::npos && item.substr(0, equals) == name) {
            std::string decoded;
            if (percent_decode(item.substr(equals + 1), decoded))
                return decoded;
            return {};
        }
        if (end == std::string_view::npos)
            break;
        offset = end + 1;
    }
    return {};
}

bool is_allowed_origin(std::string_view origin,
                       const std::vector<std::string>& allowedOrigins) {
    if (origin.empty())
        return true;
    const std::string value = lower(std::string(origin));
    for (const std::string& allowed : allowedOrigins) {
        if (value == lower(allowed))
            return true;
    }

    const std::size_t schemeEnd = value.find("://");
    if (schemeEnd == std::string::npos)
        return false;
    const std::string scheme = value.substr(0, schemeEnd);
    if (scheme != "http" && scheme != "https")
        return false;
    std::string authority = value.substr(schemeEnd + 3);
    const std::size_t slash = authority.find('/');
    if (slash != std::string::npos)
        authority.erase(slash);
    std::string host;
    if (!authority.empty() && authority.front() == '[') {
        const std::size_t close = authority.find(']');
        if (close == std::string::npos)
            return false;
        host = authority.substr(0, close + 1);
    } else {
        const std::size_t colon = authority.find(':');
        host = authority.substr(0, colon);
    }
    return host == "127.0.0.1" || host == "localhost" || host == "[::1]";
}

struct HttpRequest {
    std::string method;
    std::string target;
    std::string version;
    std::string headers;
};

bool parse_http_request(std::string_view raw, HttpRequest& out) {
    const std::size_t lineEnd = raw.find("\r\n");
    if (lineEnd == std::string_view::npos)
        return false;
    const std::string requestLine(raw.substr(0, lineEnd));
    const std::size_t firstSpace = requestLine.find(' ');
    const std::size_t secondSpace =
        firstSpace == std::string::npos ? std::string::npos
                                        : requestLine.find(' ', firstSpace + 1);
    if (firstSpace == std::string::npos || secondSpace == std::string::npos)
        return false;
    out.method = requestLine.substr(0, firstSpace);
    out.target = requestLine.substr(firstSpace + 1, secondSpace - firstSpace - 1);
    out.version = requestLine.substr(secondSpace + 1);
    out.headers = std::string(raw.substr(lineEnd + 2));
    return !out.method.empty() && !out.target.empty();
}

struct Connection {
    explicit Connection(insys::TcpTransport& stream) : stream(stream) {}
    insys::TcpTransport& stream;
    std::string buffer;
};

bool read_more(Connection& connection, int timeoutMs) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    do {
        std::uint8_t buffer[8192];
        std::size_t received = 0;
        if (connection.stream.receive_raw(buffer, sizeof(buffer), received)) {
            connection.buffer.append(reinterpret_cast<const char*>(buffer), received);
            return true;
        }
        if (!connection.stream.connected())
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    return true;
}

bool read_headers(Connection& connection, const std::atomic<bool>& stopping,
                  std::string& request, std::string& error) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (true) {
        const std::size_t end = connection.buffer.find("\r\n\r\n");
        if (end != std::string::npos) {
            const std::size_t length = end + 4;
            request.assign(connection.buffer.data(), length);
            connection.buffer.erase(0, length);
            return true;
        }
        if (stopping.load()) {
            error = "server is stopping";
            return false;
        }
        if (connection.buffer.size() > kMaxHttpHeaderBytes) {
            error = "HTTP header too large";
            return false;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            error = "HTTP header timeout";
            return false;
        }
        if (!read_more(connection, 100)) {
            error = "connection closed during HTTP handshake";
            return false;
        }
    }
}

bool ensure_bytes(Connection& connection, std::size_t count,
                  const std::atomic<bool>& stopping) {
    while (connection.buffer.size() < count) {
        if (stopping.load())
            return false;
        if (!read_more(connection, 100))
            return false;
    }
    return true;
}

struct Frame {
    bool fin = true;
    std::uint8_t opcode = 0;
    std::string payload;
};

bool read_frame(Connection& connection, const std::atomic<bool>& stopping,
                std::size_t maxPayload, Frame& frame, std::string& error) {
    if (!ensure_bytes(connection, 2, stopping))
        return false;
    const std::uint8_t first = static_cast<std::uint8_t>(connection.buffer[0]);
    const std::uint8_t second = static_cast<std::uint8_t>(connection.buffer[1]);
    frame.fin = (first & 0x80u) != 0;
    frame.opcode = first & 0x0fu;
    const bool masked = (second & 0x80u) != 0;
    std::uint64_t length = second & 0x7fu;
    std::size_t offset = 2;
    if (length == 126) {
        if (!ensure_bytes(connection, offset + 2, stopping))
            return false;
        length = (static_cast<std::uint64_t>(
                      static_cast<std::uint8_t>(connection.buffer[offset]))
                  << 8u) |
                 static_cast<std::uint8_t>(connection.buffer[offset + 1]);
        offset += 2;
    } else if (length == 127) {
        if (!ensure_bytes(connection, offset + 8, stopping))
            return false;
        length = 0;
        for (int i = 0; i < 8; ++i) {
            length = (length << 8u) |
                     static_cast<std::uint8_t>(connection.buffer[offset + static_cast<std::size_t>(i)]);
        }
        offset += 8;
    }
    if (!masked) {
        error = "client WebSocket frames must be masked";
        return false;
    }
    if ((frame.opcode & 0x08u) != 0 && (!frame.fin || length > 125)) {
        error = "invalid WebSocket control frame";
        return false;
    }
    if (length > maxPayload) {
        error = "WebSocket frame exceeds maximum message size";
        return false;
    }
    if (!ensure_bytes(connection, offset + 4, stopping))
        return false;
    std::array<std::uint8_t, 4> mask{};
    for (int i = 0; i < 4; ++i)
        mask[static_cast<std::size_t>(i)] =
            static_cast<std::uint8_t>(connection.buffer[offset + static_cast<std::size_t>(i)]);
    offset += 4;
    if (!ensure_bytes(connection, offset + static_cast<std::size_t>(length), stopping))
        return false;

    frame.payload.assign(connection.buffer.data() + offset,
                         static_cast<std::size_t>(length));
    for (std::size_t i = 0; i < frame.payload.size(); ++i)
        frame.payload[i] = static_cast<char>(
            static_cast<std::uint8_t>(frame.payload[i]) ^ mask[i % 4]);
    connection.buffer.erase(0, offset + static_cast<std::size_t>(length));
    return true;
}

std::string make_frame(std::uint8_t opcode, std::string_view payload) {
    std::string frame;
    frame.reserve(payload.size() + 10);
    frame.push_back(static_cast<char>(0x80u | opcode));
    const std::size_t size = payload.size();
    if (size < 126) {
        frame.push_back(static_cast<char>(size));
    } else if (size <= 0xffffu) {
        frame.push_back(static_cast<char>(126));
        frame.push_back(static_cast<char>((size >> 8u) & 0xffu));
        frame.push_back(static_cast<char>(size & 0xffu));
    } else {
        frame.push_back(static_cast<char>(127));
        const std::uint64_t length = static_cast<std::uint64_t>(size);
        for (int i = 7; i >= 0; --i)
            frame.push_back(static_cast<char>((length >> (i * 8)) & 0xffu));
    }
    frame.append(payload);
    return frame;
}

bool send_text(insys::TcpTransport& stream, std::string_view text) {
    const std::string frame = make_frame(kOpText, text);
    return send_all(stream, frame);
}

bool send_close(insys::TcpTransport& stream, std::uint16_t code) {
    const std::string payload = {static_cast<char>((code >> 8u) & 0xffu),
                                 static_cast<char>(code & 0xffu)};
    const std::string frame = make_frame(kOpClose, payload);
    return send_all(stream, frame);
}

std::string request_too_large_error() {
    return R"({"jsonrpc":"2.0","id":null,"error":{"code":-32013,"message":"request exceeds maximum message size"}})";
}

} // namespace

struct EditorServer::Impl {
    explicit Impl(EditorServerOptions value)
        : options(std::move(value)), session(options.session) {
        if (options.token.empty())
            options.token = random_token();
        if (!options.staticRoot.empty()) {
            std::error_code error;
            const std::filesystem::path normalized =
                std::filesystem::weakly_canonical(options.staticRoot, error);
            if (!error)
                options.staticRoot = normalized;
        }
    }

    bool start(std::string* error) {
        return listener.listen(options.host, options.port, error);
    }

    void stop() { stopping.store(true); }

    int run() {
        while (!stopping.load()) {
            std::string error;
            std::unique_ptr<insys::TcpTransport> stream = listener.accept(200, &error);
            if (!stream) {
                if (stopping.load())
                    break;
                continue;
            }
            workers.emplace_back([this, stream = std::move(stream)]() mutable {
                handle_connection(std::move(stream));
            });
        }

        for (std::thread& worker : workers) {
            if (worker.joinable())
                worker.join();
        }
        workers.clear();
        return 0;
    }

    bool serve_static(insys::TcpTransport& stream, const HttpRequest& request,
                      std::string_view path) {
        std::string decoded;
        if (!percent_decode(path, decoded)) {
            send_all(stream, http_error(400, "Bad Request", "invalid URL encoding"));
            return true;
        }
        if (decoded.empty() || decoded == "/")
            decoded = "/index.html";

        std::string relative = decoded;
        if (!relative.empty() && relative.front() == '/')
            relative.erase(relative.begin());
        const std::filesystem::path relativePath(relative);
        if (relativePath.is_absolute() || relativePath.has_root_name()) {
            send_all(stream, http_error(403, "Forbidden", "invalid path"));
            return true;
        }
        for (const std::filesystem::path& component : relativePath) {
            if (component == "..") {
                send_all(stream, http_error(403, "Forbidden", "invalid path"));
                return true;
            }
        }

        std::filesystem::path candidate;
        if (options.staticRoot.empty() || relativePath == "index.html") {
            if (options.staticRoot.empty() || !std::filesystem::exists(
                    options.staticRoot / relativePath)) {
                const bool head = request.method == "HEAD";
                send_all(stream, http_response(200, "OK", "text/html; charset=utf-8",
                                               kFallbackHtml, !head));
                return true;
            }
        }
        candidate = options.staticRoot / relativePath;
        std::error_code error;
        candidate = std::filesystem::weakly_canonical(candidate, error);
        if (error || options.staticRoot.empty() ||
            !is_within(candidate, options.staticRoot)) {
            send_all(stream, http_error(404, "Not Found", "file not found"));
            return true;
        }
        if (std::filesystem::is_directory(candidate, error)) {
            candidate /= "index.html";
            candidate = std::filesystem::weakly_canonical(candidate, error);
        }
        if (error || !std::filesystem::is_regular_file(candidate, error)) {
            send_all(stream, http_error(404, "Not Found", "file not found"));
            return true;
        }
        const std::uintmax_t size = std::filesystem::file_size(candidate, error);
        if (error || size > kMaxStaticFileBytes) {
            send_all(stream, http_error(413, "Payload Too Large", "file too large"));
            return true;
        }
        std::ifstream file(candidate, std::ios::binary);
        if (!file) {
            send_all(stream, http_error(500, "Internal Server Error", "cannot open file"));
            return true;
        }
        std::ostringstream contents;
        contents << file.rdbuf();
        const bool head = request.method == "HEAD";
        send_all(stream, http_response(200, "OK", mime_type(candidate),
                                       contents.str(), !head));
        return true;
    }

    bool handle_websocket(std::unique_ptr<insys::TcpTransport> stream,
                          Connection& connection, const HttpRequest& request) {
        std::string path;
        std::string query;
        split_target(request.target, path, query);
        if (path != "/ws") {
            send_all(*stream, http_error(404, "Not Found", "unknown WebSocket path"));
            return true;
        }
        if (lower(header_value(request.headers, "upgrade")) != "websocket") {
            send_all(*stream, http_error(400, "Bad Request", "expected WebSocket upgrade"));
            return true;
        }

        const std::string origin = header_value(request.headers, "origin");
        if (!is_allowed_origin(origin, options.allowedOrigins)) {
            send_all(*stream, http_error(403, "Forbidden", "origin is not allowed"));
            return true;
        }
        if (!constant_time_equal(query_value(query, "token"), options.token)) {
            send_all(*stream, http_error(403, "Forbidden", "invalid editor token"));
            return true;
        }

        const std::string key = header_value(request.headers, "sec-websocket-key");
        if (key.empty()) {
            send_all(*stream, http_error(400, "Bad Request", "missing Sec-WebSocket-Key"));
            return true;
        }
        const std::string response =
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: " +
            websocket_accept_key(key) + "\r\n\r\n";
        if (!send_all(*stream, response))
            return true;

        EditorProtocolOptions protocolOptions;
        protocolOptions.serverVersion = options.serverVersion;
        protocolOptions.maxMessageBytes = options.maxRequestBytes;
        protocolOptions.enablePreviewHid = true;
        protocolOptions.requestShutdown = [this]() { stop(); };
        const EditorProtocol protocol(session, std::move(protocolOptions));

        std::string fragmented;
        bool hasFragment = false;
        while (!stopping.load()) {
            Frame frame;
            std::string error;
            if (!read_frame(connection, stopping, options.maxRequestBytes, frame, error)) {
                if (error.find("exceeds") != std::string::npos)
                    send_text(*stream, request_too_large_error());
                else if (!error.empty())
                    send_close(*stream, 1002);
                break;
            }
            if (frame.opcode == kOpClose) {
                send_close(*stream, 1000);
                break;
            }
            if (frame.opcode == kOpPing) {
                const std::string pong = make_frame(kOpPong, frame.payload);
                if (!send_all(*stream, pong))
                    break;
                continue;
            }
            if (frame.opcode == kOpPong)
                continue;
            if (frame.opcode == kOpBinary) {
                send_close(*stream, 1003);
                break;
            }

            if (frame.opcode == kOpText) {
                if (hasFragment) {
                    send_close(*stream, 1002);
                    break;
                }
                if (!frame.fin) {
                    fragmented = std::move(frame.payload);
                    hasFragment = true;
                    continue;
                }
            } else if (frame.opcode == kOpContinuation) {
                if (!hasFragment) {
                    send_close(*stream, 1002);
                    break;
                }
                if (fragmented.size() + frame.payload.size() > options.maxRequestBytes) {
                    send_text(*stream, request_too_large_error());
                    send_close(*stream, 1009);
                    break;
                }
                fragmented.append(frame.payload);
                if (!frame.fin)
                    continue;
                frame.payload = std::move(fragmented);
                fragmented.clear();
                hasFragment = false;
            } else {
                send_close(*stream, 1002);
                break;
            }

            const std::optional<std::string> responseText = protocol.handle(frame.payload);
            if (responseText && !send_text(*stream, *responseText))
                break;
        }
        if (stopping.load())
            send_close(*stream, 1000);
        return true;
    }

    void handle_connection(std::unique_ptr<insys::TcpTransport> stream) {
        try {
            Connection connection(*stream);
            std::string requestText;
            std::string error;
            if (!read_headers(connection, stopping, requestText, error)) {
                if (stream->connected())
                    send_all(*stream, http_error(400, "Bad Request", error));
                return;
            }

            HttpRequest request;
            if (!parse_http_request(requestText, request)) {
                send_all(*stream, http_error(400, "Bad Request", "malformed HTTP request"));
                return;
            }

            if (request.method != "GET" && request.method != "HEAD") {
                send_all(*stream, http_error(405, "Method Not Allowed", "method not allowed"));
                return;
            }

            if (request.target.rfind("/ws", 0) == 0) {
                handle_websocket(std::move(stream), connection, request);
                return;
            }

            std::string path;
            std::string query;
            split_target(request.target, path, query);
            if (path == "/healthz") {
                send_all(*stream, http_response(200, "OK", "application/json",
                                                "{\"ok\":true}", request.method != "HEAD"));
                return;
            }

            serve_static(*stream, request, path);
        } catch (const std::exception&) {
        }
    }

    EditorServerOptions options;
    EditorSession session;
    insys::TcpServer listener;
    std::atomic<bool> stopping{false};
    std::vector<std::thread> workers;
};

EditorServer::EditorServer(EditorServerOptions options)
    : m_impl(std::make_unique<Impl>(std::move(options))) {}

EditorServer::~EditorServer() {
    if (m_impl)
        m_impl->stop();
}

bool EditorServer::start(std::string* error) {
    return m_impl->start(error);
}

int EditorServer::run() {
    return m_impl->run();
}

void EditorServer::stop() {
    m_impl->stop();
}

std::uint16_t EditorServer::port() const {
    return m_impl->listener.port();
}

const std::string& EditorServer::token() const {
    return m_impl->options.token;
}

} // namespace insys::editor
