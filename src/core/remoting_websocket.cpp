#include "insys/remoting_websocket.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "insys/remoting_tcp.h"

namespace insys {
namespace {

constexpr std::string_view kWebSocketGuid =
    "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
constexpr std::size_t kMaxHttpHeaderBytes = 64u * 1024u;
constexpr std::size_t kMaxWebSocketPayloadBytes = 16u * 1024u * 1024u;
constexpr std::uint8_t kOpContinuation = 0x0;
constexpr std::uint8_t kOpText = 0x1;
constexpr std::uint8_t kOpBinary = 0x2;
constexpr std::uint8_t kOpClose = 0x8;
constexpr std::uint8_t kOpPing = 0x9;
constexpr std::uint8_t kOpPong = 0xa;

std::uint32_t rotate_left(std::uint32_t value, int bits) {
    return (value << bits) | (value >> (32 - bits));
}

void sha1(const std::uint8_t* data, std::size_t size, std::uint8_t out[20]) {
    std::vector<std::uint8_t> message(data, data + size);
    const std::uint64_t bitLength = static_cast<std::uint64_t>(size) * 8u;
    message.push_back(0x80);
    while (message.size() % 64u != 56u)
        message.push_back(0);
    for (int i = 7; i >= 0; --i)
        message.push_back(static_cast<std::uint8_t>((bitLength >> (i * 8)) & 0xffu));

    std::uint32_t h0 = 0x67452301u;
    std::uint32_t h1 = 0xEFCDAB89u;
    std::uint32_t h2 = 0x98BADCFEu;
    std::uint32_t h3 = 0x10325476u;
    std::uint32_t h4 = 0xC3D2E1F0u;

    for (std::size_t chunk = 0; chunk < message.size(); chunk += 64) {
        std::uint32_t w[80]{};
        for (int i = 0; i < 16; ++i) {
            const std::size_t offset = chunk + static_cast<std::size_t>(i) * 4;
            w[i] = (static_cast<std::uint32_t>(message[offset]) << 24) |
                   (static_cast<std::uint32_t>(message[offset + 1]) << 16) |
                   (static_cast<std::uint32_t>(message[offset + 2]) << 8) |
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
                k = 0x5A827999u;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1u;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDCu;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6u;
            }
            const std::uint32_t temp =
                rotate_left(a, 5) + f + e + k + w[i];
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
        out[i * 4] = static_cast<std::uint8_t>((hash[i] >> 24) & 0xffu);
        out[i * 4 + 1] = static_cast<std::uint8_t>((hash[i] >> 16) & 0xffu);
        out[i * 4 + 2] = static_cast<std::uint8_t>((hash[i] >> 8) & 0xffu);
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
        const std::uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
        out.push_back(kAlphabet[(triple >> 18) & 0x3f]);
        out.push_back(kAlphabet[(triple >> 12) & 0x3f]);
        out.push_back(i + 1 < size ? kAlphabet[(triple >> 6) & 0x3f] : '=');
        out.push_back(i + 2 < size ? kAlphabet[triple & 0x3f] : '=');
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

std::string random_key() {
    std::random_device device;
    std::mt19937 generator(device());
    std::uniform_int_distribution<int> distribution(0, 255);
    std::uint8_t bytes[16]{};
    for (std::uint8_t& byte : bytes)
        byte = static_cast<std::uint8_t>(distribution(generator));
    return base64_encode(bytes, sizeof(bytes));
}

std::string trim(std::string value) {
    const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    auto begin = std::find_if(value.begin(), value.end(), notSpace);
    auto end = std::find_if(value.rbegin(), value.rend(), notSpace).base();
    if (begin >= end)
        return {};
    return std::string(begin, end);
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string header_value(const std::string& headers, std::string_view name) {
    const std::string wanted = lower(std::string(name));
    std::size_t offset = headers.find("\r\n");
    if (offset == std::string::npos)
        return {};
    offset += 2;
    while (offset < headers.size()) {
        const std::size_t end = headers.find("\r\n", offset);
        if (end == std::string::npos || end == offset)
            break;
        const std::string line = headers.substr(offset, end - offset);
        const std::size_t colon = line.find(':');
        if (colon != std::string::npos &&
            lower(trim(line.substr(0, colon))) == wanted) {
            return trim(line.substr(colon + 1));
        }
        offset = end + 2;
    }
    return {};
}

std::string request_target(const std::string& request) {
    const std::size_t firstSpace = request.find(' ');
    if (firstSpace == std::string::npos)
        return {};
    const std::size_t secondSpace = request.find(' ', firstSpace + 1);
    if (secondSpace == std::string::npos)
        return {};
    return request.substr(firstSpace + 1, secondSpace - firstSpace - 1);
}

struct FrameHeader {
    bool fin = false;
    bool masked = false;
    std::uint8_t opcode = 0;
    std::uint64_t payloadLength = 0;
    std::size_t headerLength = 0;
    std::array<std::uint8_t, 4> mask{};
};

std::vector<std::uint8_t> make_frame(std::uint8_t opcode,
                                     const std::uint8_t* payload,
                                     std::size_t size, bool mask) {
    std::vector<std::uint8_t> frame;
    frame.reserve(size + 14);
    frame.push_back(static_cast<std::uint8_t>(0x80u | opcode));
    if (size < 126) {
        frame.push_back(static_cast<std::uint8_t>(size | (mask ? 0x80u : 0u)));
    } else if (size <= 0xffffu) {
        frame.push_back(static_cast<std::uint8_t>(126u | (mask ? 0x80u : 0u)));
        frame.push_back(static_cast<std::uint8_t>((size >> 8u) & 0xffu));
        frame.push_back(static_cast<std::uint8_t>(size & 0xffu));
    } else {
        frame.push_back(static_cast<std::uint8_t>(127u | (mask ? 0x80u : 0u)));
        const std::uint64_t length = static_cast<std::uint64_t>(size);
        for (int i = 7; i >= 0; --i)
            frame.push_back(static_cast<std::uint8_t>((length >> (i * 8)) & 0xffu));
    }

    if (mask) {
        std::random_device device;
        std::mt19937 generator(device());
        std::uniform_int_distribution<int> distribution(0, 255);
        std::uint8_t key[4]{};
        for (std::uint8_t& byte : key) {
            byte = static_cast<std::uint8_t>(distribution(generator));
            frame.push_back(byte);
        }
        for (std::size_t i = 0; i < size; ++i)
            frame.push_back(static_cast<std::uint8_t>(payload[i] ^ key[i % 4]));
    } else {
        if (size > 0)
            frame.insert(frame.end(), payload, payload + size);
    }
    return frame;
}

enum class HeaderResult { Ok, Incomplete, Error };

HeaderResult parse_frame_header(const std::vector<std::uint8_t>& data, FrameHeader& out) {
    if (data.size() < 2)
        return HeaderResult::Incomplete;
    out = {};
    const std::uint8_t first = data[0];
    const std::uint8_t second = data[1];
    out.fin = (first & 0x80u) != 0;
    out.opcode = first & 0x0fu;
    out.masked = (second & 0x80u) != 0;
    std::uint64_t length = second & 0x7fu;
    std::size_t offset = 2;
    if (length == 126) {
        if (data.size() < offset + 2)
            return HeaderResult::Incomplete;
        length = (static_cast<std::uint64_t>(data[offset]) << 8u) |
                 static_cast<std::uint64_t>(data[offset + 1]);
        offset += 2;
    } else if (length == 127) {
        if (data.size() < offset + 8)
            return HeaderResult::Incomplete;
        length = 0;
        for (int i = 0; i < 8; ++i)
            length = (length << 8u) | data[offset + static_cast<std::size_t>(i)];
        offset += 8;
    }
    if (length > kMaxWebSocketPayloadBytes)
        return HeaderResult::Error;
    if (out.masked) {
        if (data.size() < offset + 4)
            return HeaderResult::Incomplete;
        for (int i = 0; i < 4; ++i)
            out.mask[static_cast<std::size_t>(i)] = data[offset + static_cast<std::size_t>(i)];
        offset += 4;
    }
    out.payloadLength = length;
    out.headerLength = offset;
    return HeaderResult::Ok;
}

} // namespace

struct WebSocketTransport::Impl {
    std::unique_ptr<TcpTransport> stream;
    bool client = false;
    bool closed = false;
    bool hasFragment = false;
    std::vector<std::uint8_t> inbound;
    std::vector<std::uint8_t> fragment;
};

namespace {

bool send_frame(WebSocketTransport::Impl& impl, std::uint8_t opcode,
                const std::uint8_t* payload, std::size_t size) {
    if (!impl.stream || impl.closed)
        return false;
    const auto frame = make_frame(opcode, payload, size, impl.client);
    return impl.stream->send_raw(frame.data(), frame.size());
}

bool read_http(WebSocketTransport::Impl& impl, std::string& out,
               std::string_view marker, int timeoutMs, std::string* error) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeoutMs);
    for (;;) {
        const auto begin = impl.inbound.begin();
        const auto end = impl.inbound.end();
        const auto found = std::search(begin, end, marker.begin(), marker.end());
        if (found != end) {
            const auto markerEnd = found + static_cast<std::ptrdiff_t>(marker.size());
            out.assign(reinterpret_cast<const char*>(impl.inbound.data()),
                       static_cast<std::size_t>(markerEnd - begin));
            impl.inbound.erase(begin, markerEnd);
            return true;
        }
        if (impl.inbound.size() > kMaxHttpHeaderBytes) {
            if (error != nullptr)
                *error = "HTTP header too large";
            return false;
        }

        std::uint8_t buffer[4096];
        std::size_t received = 0;
        if (!impl.stream->receive_raw(buffer, sizeof(buffer), received)) {
            if (!impl.stream->connected()) {
                if (error != nullptr)
                    *error = "connection closed during HTTP handshake";
                return false;
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                if (error != nullptr)
                    *error = "HTTP handshake timed out";
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        impl.inbound.insert(impl.inbound.end(), buffer, buffer + received);
    }
}

bool client_handshake(WebSocketTransport::Impl& impl, std::string_view host,
                      uint16_t port, std::string_view path, int timeoutMs,
                      std::string* error) {
    const std::string key = random_key();
    std::string request = "GET ";
    request.append(path.empty() ? "/" : std::string(path));
    request += " HTTP/1.1\r\n";
    request += "Host: " + std::string(host) + ":" + std::to_string(port) + "\r\n";
    request += "Upgrade: websocket\r\n";
    request += "Connection: Upgrade\r\n";
    request += "Sec-WebSocket-Key: " + key + "\r\n";
    request += "Sec-WebSocket-Version: 13\r\n\r\n";
    if (!impl.stream->send_raw(request.data(), request.size())) {
        if (error != nullptr)
            *error = "failed to send WebSocket handshake";
        return false;
    }

    std::string response;
    if (!read_http(impl, response, "\r\n\r\n", timeoutMs, error))
        return false;
    if (response.rfind("HTTP/1.1 101", 0) != 0) {
        if (error != nullptr)
            *error = "WebSocket upgrade rejected";
        return false;
    }
    if (header_value(response, "sec-websocket-accept") != websocket_accept_key(key)) {
        if (error != nullptr)
            *error = "invalid Sec-WebSocket-Accept";
        return false;
    }
    return true;
}

bool server_handshake(WebSocketTransport::Impl& impl, std::string_view path,
                      int timeoutMs, std::string* error) {
    std::string request;
    if (!read_http(impl, request, "\r\n\r\n", timeoutMs, error))
        return false;

    if (request.rfind("GET ", 0) != 0) {
        if (error != nullptr)
            *error = "expected WebSocket GET request";
        return false;
    }
    const std::string target = request_target(request);
    if (!path.empty() && target != path) {
        if (error != nullptr)
            *error = "unexpected WebSocket path";
        return false;
    }
    const std::string key = header_value(request, "sec-websocket-key");
    if (key.empty()) {
        if (error != nullptr)
            *error = "missing Sec-WebSocket-Key";
        return false;
    }

    const std::string response =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " +
        websocket_accept_key(key) + "\r\n\r\n";
    if (!impl.stream->send_raw(response.data(), response.size())) {
        if (error != nullptr)
            *error = "failed to send WebSocket handshake response";
        return false;
    }
    return true;
}

enum class FrameResult { Incomplete, Continue, Message, Error };

FrameResult parse_frame(WebSocketTransport::Impl& impl, RemoteMessage& message,
                        std::string* error) {
    FrameHeader header;
    const HeaderResult headerResult = parse_frame_header(impl.inbound, header);
    if (headerResult == HeaderResult::Incomplete)
        return FrameResult::Incomplete;
    if (headerResult == HeaderResult::Error) {
        if (error != nullptr)
            *error = "invalid WebSocket frame header";
        return FrameResult::Error;
    }
    if (impl.inbound.size() < header.headerLength + header.payloadLength)
        return FrameResult::Incomplete;

    std::vector<std::uint8_t> payload(
        impl.inbound.begin() + static_cast<std::ptrdiff_t>(header.headerLength),
        impl.inbound.begin() +
            static_cast<std::ptrdiff_t>(header.headerLength + header.payloadLength));
    if (header.masked) {
        for (std::size_t i = 0; i < payload.size(); ++i)
            payload[i] ^= header.mask[i % 4];
    }
    impl.inbound.erase(
        impl.inbound.begin(),
        impl.inbound.begin() +
            static_cast<std::ptrdiff_t>(header.headerLength + header.payloadLength));

    if (header.opcode == kOpClose) {
        impl.closed = true;
        return FrameResult::Continue;
    }
    if (header.opcode == kOpPing) {
        send_frame(impl, kOpPong, payload.data(), payload.size());
        return FrameResult::Continue;
    }
    if (header.opcode == kOpPong)
        return FrameResult::Continue;
    if (header.opcode == kOpText)
        return FrameResult::Continue;

    if (header.opcode == kOpBinary) {
        if (impl.hasFragment) {
            if (error != nullptr)
                *error = "unexpected WebSocket binary frame";
            return FrameResult::Error;
        }
        if (!header.fin) {
            impl.fragment = std::move(payload);
            impl.hasFragment = true;
            return FrameResult::Continue;
        }
    } else if (header.opcode == kOpContinuation) {
        if (!impl.hasFragment) {
            if (error != nullptr)
                *error = "unexpected WebSocket continuation frame";
            return FrameResult::Error;
        }
        impl.fragment.insert(impl.fragment.end(), payload.begin(), payload.end());
        if (!header.fin)
            return FrameResult::Continue;
        payload = std::move(impl.fragment);
        impl.fragment.clear();
        impl.hasFragment = false;
    } else {
        if (error != nullptr)
            *error = "unsupported WebSocket opcode";
        return FrameResult::Error;
    }

    RemoteMessage decoded;
    if (!decode_remote_message(payload.data(), payload.size(), decoded)) {
        if (error != nullptr)
            *error = "invalid remote message in WebSocket frame";
        return FrameResult::Error;
    }
    message = std::move(decoded);
    return FrameResult::Message;
}

} // namespace

WebSocketTransport::WebSocketTransport(std::unique_ptr<Impl> impl)
    : m_impl(std::move(impl)) {}

WebSocketTransport::~WebSocketTransport() {
    close();
}

std::unique_ptr<WebSocketTransport> WebSocketTransport::connect(
    std::string_view host, uint16_t port, std::string_view path, int timeoutMs,
    std::string* error) {
    auto stream = TcpTransport::connect(host, port, error);
    if (!stream)
        return nullptr;

    auto impl = std::make_unique<Impl>();
    impl->stream = std::move(stream);
    impl->client = true;
    if (!client_handshake(*impl, host, port, path.empty() ? "/" : path, timeoutMs,
                          error))
        return nullptr;
    return std::unique_ptr<WebSocketTransport>(new WebSocketTransport(std::move(impl)));
}

std::unique_ptr<WebSocketTransport> WebSocketTransport::accept(
    TcpServer& server, std::string_view path, int timeoutMs, std::string* error) {
    auto stream = server.accept(timeoutMs, error);
    if (!stream)
        return nullptr;

    auto impl = std::make_unique<Impl>();
    impl->stream = std::move(stream);
    impl->client = false;
    if (!server_handshake(*impl, path.empty() ? "/" : path, timeoutMs, error))
        return nullptr;
    return std::unique_ptr<WebSocketTransport>(new WebSocketTransport(std::move(impl)));
}

bool WebSocketTransport::send(const RemoteMessage& message) {
    if (!m_impl || m_impl->closed)
        return false;
    const std::vector<std::uint8_t> frame = encode_remote_message(message);
    if (frame.empty())
        return false;
    return send_frame(*m_impl, kOpBinary, frame.data(), frame.size());
}

bool WebSocketTransport::receive(RemoteMessage& message) {
    if (!m_impl || m_impl->closed)
        return false;

    for (;;) {
        const FrameResult result = parse_frame(*m_impl, message, nullptr);
        if (result == FrameResult::Message)
            return true;
        if (result == FrameResult::Error) {
            m_impl->closed = true;
            return false;
        }
        if (result == FrameResult::Continue)
            continue;

        std::uint8_t buffer[4096];
        std::size_t received = 0;
        if (!m_impl->stream->receive_raw(buffer, sizeof(buffer), received)) {
            if (!m_impl->stream->connected())
                m_impl->closed = true;
            return false;
        }
        m_impl->inbound.insert(m_impl->inbound.end(), buffer, buffer + received);
    }
}

bool WebSocketTransport::connected() const {
    return m_impl && !m_impl->closed && m_impl->stream &&
           m_impl->stream->connected();
}

void WebSocketTransport::close() {
    if (!m_impl || m_impl->closed)
        return;
    send_frame(*m_impl, kOpClose, nullptr, 0);
    m_impl->closed = true;
    if (m_impl->stream)
        m_impl->stream->close();
}

} // namespace insys


