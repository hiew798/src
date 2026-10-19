// The runner appends methods extracted from the installed library, unchanged.
// TLS and socket doubles deliberately reject access after connection teardown.
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <stdexcept>
#include <cstdlib>
using byte = unsigned char;
unsigned long clockMs = 0;
unsigned long millis() { return clockMs; }
void delay(unsigned long n) { clockMs += n; if (clockMs > 100000) throw std::runtime_error("unbounded body drain"); }
#define HTTPS_LOGI(...) do {} while(0)
#define HTTPS_LOGE(...) do {} while(0)
#define HTTPS_LOGW(...) do {} while(0)
#define HTTPS_LOGD(...) do {} while(0)
#define HTTPS_CONNECTION_DATA_CHUNK_SIZE 512
#define HTTPS_CONNECTION_TIMEOUT 20000
#define HTTPS_SHUTDOWN_TIMEOUT 5000
int headersAlive = 0, tlsAlive = 0, tlsCalls = 0;
struct HTTPHeaders {
    HTTPHeaders() { ++headersAlive; }
    ~HTTPHeaders() { --headersAlive; }
    void clearAll() { assert(headersAlive == 1); }
};
struct SSL { std::vector<int> reads; size_t position = 0; SSL() { ++tlsAlive; } ~SSL() { --tlsAlive; } };
int SSL_read(SSL* ssl, byte*, size_t n) {
    assert(ssl); ++tlsCalls;
    if (ssl->position == ssl->reads.size()) return 0;
    int result = ssl->reads[ssl->position++];
    assert(result < 0 || size_t(result) <= n); return result;
}
int SSL_write(SSL* ssl, byte*, size_t n) { assert(ssl); ++tlsCalls; return int(n); }
int SSL_pending(SSL* ssl) { assert(ssl); ++tlsCalls; return 0; }
int SSL_shutdown(SSL* ssl) { assert(ssl); return 0; }
void SSL_free(SSL* ssl) { assert(ssl); delete ssl; }
int close(int) { return 0; }
struct HTTPConnection {
    enum State { STATE_UNDEFINED, STATE_INITIAL, STATE_HEADERS_FINISHED, STATE_CLOSING, STATE_CLOSED, STATE_ERROR };
    enum Client { CSTATE_ACTIVE, CSTATE_CLOSED };
    State _connectionState = STATE_HEADERS_FINISHED;
    Client _clientState = CSTATE_ACTIVE;
    unsigned long _shutdownTS = 0;
    int _socket = 51, _addrLen = 0, _bufferProcessed = 0, _bufferUnusedIdx = 0;
    char _receiveBuffer[512]{};
    HTTPHeaders* _httpHeaders = new HTTPHeaders;
    int* _wsHandler = nullptr;
    virtual ~HTTPConnection();
    bool isClosed();
    virtual void closeConnection();
    virtual size_t pendingByteCount() { return 0; }
    virtual size_t readBytesToBuffer(byte*, size_t) = 0;
    virtual bool canReadData() { return _socket >= 0 && !isClosed(); }
    void refreshTimeout() {}
    void signalRequestError() { _connectionState = STATE_ERROR; closeConnection(); }
    int updateBuffer();
    size_t pendingBufferSize();
    size_t readBuffer(byte*, size_t);
};
struct HTTPSConnection : HTTPConnection {
    SSL* _ssl = new SSL;
    ~HTTPSConnection() { closeConnection(); }
    void closeConnection() override;
    size_t writeBuffer(byte*, size_t);
    size_t readBytesToBuffer(byte*, size_t) override;
    size_t pendingByteCount() override;
    bool canReadData() override;
};
struct HTTPRequest {
    HTTPConnection* _con;
    HTTPHeaders* _headers;
    bool _contentLengthSet;
    size_t _remainingContent;
    HTTPRequest(HTTPConnection* c, bool lengthSet, size_t remaining)
        : _con(c), _headers(c->_httpHeaders), _contentLengthSet(lengthSet), _remainingContent(remaining) {}
    ~HTTPRequest();
    size_t readBytes(byte*, size_t);
    bool requestComplete();
    void discardRequestBody();
};

// PRODUCTION_METHODS

int main() {
#ifdef _MSC_VER
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    // Exact reported failure: checking a GET with no Content-Length closes TLS
    // on receive error, then pendingBufferSize used to call SSL_pending(NULL).
    for (int i = 0; i < 2000; ++i) {
        HTTPSConnection connection;
        connection._ssl->reads = {-1};
        {
            HTTPRequest request(&connection, false, 0);
            assert(request.requestComplete());
            assert(connection.isClosed());
            assert(tlsAlive == 0 && headersAlive == 1);
            int calls = tlsCalls;
            byte b{};
            assert(connection.pendingByteCount() == 0);
            assert(connection.readBytesToBuffer(&b, 1) == 0);
            assert(connection.writeBuffer(&b, 1) == 0);
            assert(!connection.canReadData());
            assert(calls == tlsCalls);
        } // Request cleanup still has valid headers after the connection closed.
        assert(headersAlive == 1);
    }
    assert(headersAlive == 0 && tlsAlive == 0);
    // Interrupted POST with declared content must terminate without a busy loop.
    {
        HTTPSConnection connection;
        connection._ssl->reads = {4, -1};
        HTTPRequest request(&connection, true, 512);
        request.discardRequestBody();
        assert(connection.isClosed());
        assert(request._remainingContent == 508);
        assert(clockMs < 100);
    }
    // EOF while a client falsely declares a longer body must also be bounded.
    clockMs = 0;
    {
        HTTPSConnection connection;
        HTTPRequest request(&connection, true, 512);
        request.discardRequestBody();
        assert(connection.isClosed());
        assert(clockMs <= HTTPS_CONNECTION_TIMEOUT + 1);
    }
    // Complete POST still reads the whole body and leaves the connection usable.
    clockMs = 0;
    {
        HTTPSConnection connection;
        connection._ssl->reads = {4, 4};
        HTTPRequest request(&connection, true, 8);
        request.discardRequestBody();
        assert(request.requestComplete());
        assert(!connection.isClosed());
        byte b{}; assert(connection.writeBuffer(&b, 1) == 1);
    }
    assert(headersAlive == 0 && tlsAlive == 0);
    puts("PASS: disconnect regression (2000 cycles), TLS guards, header lifetime, partial POST, EOF timeout, complete POST");
}
