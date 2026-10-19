"""Repair ESP32_HTTPS_Server 1.0.0's connection teardown crash.

Run after installing the library (in addition to apply_https_compatibility.ps1).
Validates every edit before writing and retains the original library sources.
"""
import argparse
from pathlib import Path


PATCHES = {
    'HTTPSConnection.cpp': [
        ('  return SSL_write(_ssl, buffer, length);',
         '  if (!_ssl || isClosed()) return 0;\n  return SSL_write(_ssl, buffer, length);'),
        ('  return SSL_read(_ssl, buffer, length);',
         '  if (!_ssl || isClosed()) return 0;\n  return SSL_read(_ssl, buffer, length);'),
        ('  return SSL_pending(_ssl);',
         '  return (_ssl && !isClosed()) ? SSL_pending(_ssl) : 0;'),
        ('  return HTTPConnection::canReadData() || (SSL_pending(_ssl) > 0);',
         '  return _ssl && !isClosed() && (HTTPConnection::canReadData() || (SSL_pending(_ssl) > 0));'),
    ],
    'ConnectionContext.hpp': [
        ('  virtual void signalClientClose() = 0;',
         '  virtual void signalClientClose() = 0;\n  virtual bool isClosed() = 0;'),
    ],
    'HTTPConnection.cpp': [
        ('HTTPConnection::~HTTPConnection() {\n  // Close the socket\n  closeConnection();\n}',
         'HTTPConnection::~HTTPConnection() {\n  // Close the socket\n  closeConnection();\n'
         '  // Requests borrow this object, so retain it until all callbacks have returned.\n'
         '  delete _httpHeaders;\n  _httpHeaders = NULL;\n}'),
        ('  if (_httpHeaders != NULL) {\n    HTTPS_LOGD("Free headers");\n'
         '    delete _httpHeaders;\n    _httpHeaders = NULL;\n  }',
         '  // Header ownership ends in the destructor, not during a request read error.'),
        ('bool HTTPConnection::canReadData() {\n  fd_set sockfds;',
         'bool HTTPConnection::canReadData() {\n  if (_socket < 0 || isClosed()) return false;\n  fd_set sockfds;'),
        ('  return _bufferUnusedIdx - _bufferProcessed + pendingByteCount();',
         '  if (isClosed()) return 0;\n  return _bufferUnusedIdx - _bufferProcessed + pendingByteCount();'),
        ('          // The callback-function should have read all of the request body.',
         '          // A read error inside the callback may have closed the connection.\n'
         '          if (isClosed()) break;\n\n'
         '          // The callback-function should have read all of the request body.'),
        ('          // Finally, after the handshake is done, we create the WebsocketHandler and change the internal state.',
         '          // requestComplete/discardRequestBody can themselves detect a disconnect.\n'
         '          if (isClosed()) break;\n\n'
         '          // Finally, after the handshake is done, we create the WebsocketHandler and change the internal state.'),
    ],
    'HTTPRequest.cpp': [
        ('#include "HTTPRequest.hpp"',
         '#include "HTTPRequest.hpp"\n#include "HTTPSServerConstants.hpp"'),
        ('void HTTPRequest::discardRequestBody() {\n  byte buf[16];\n'
         '  while(!requestComplete()) {\n    readBytes(buf, 16);\n  }\n}',
         'void HTTPRequest::discardRequestBody() {\n  byte buf[128];\n'
         '  const unsigned long started = millis();\n'
         '  while (!_con->isClosed() && !requestComplete()) {\n'
         '    if (_con->isClosed()) return;\n'
         '    if (millis() - started >= HTTPS_CONNECTION_TIMEOUT) {\n'
         '      _con->signalRequestError();\n      return;\n    }\n'
         '    readBytes(buf, sizeof(buf));\n'
         '    delay(1); // Yield while an interrupted or slow upload is being drained.\n'
         '  }\n}'),
    ],
}


def prepare(library):
    properties = (library / 'library.properties').read_text(encoding='utf-8')
    if 'name=ESP32_HTTPS_Server\n' not in properties or 'version=1.0.0\n' not in properties:
        raise ValueError('Expected ESP32_HTTPS_Server 1.0.0; no files changed.')
    changes = []
    for name, replacements in PATCHES.items():
        path = library / 'src' / name
        original = path.read_text(encoding='utf-8')
        updated = original
        for old, new in replacements:
            if new in updated:
                continue
            if updated.count(old) != 1:
                raise ValueError(f'Unrecognised source in {path}; no files changed.')
            updated = updated.replace(old, new, 1)
        if updated != original:
            changes.append((path, updated))
    return changes


def apply(library, check=False):
    changes = prepare(library)
    if check:
        if changes:
            raise ValueError('HTTPS disconnect fix is missing. Run this tool without --check.')
        print('HTTPS disconnect fix verified.')
        return
    for path, updated in changes:
        backup = path.with_name(path.name + '.before-heltec-disconnect-fix')
        if not backup.exists():
            backup.write_bytes(path.read_bytes())
        path.write_text(updated, encoding='utf-8', newline='\n')
        print(f'Patched: {path.name}')
    if not changes:
        print('HTTPS disconnect fix already installed.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library', type=Path, default=Path(__file__).resolve().parents[2] / 'libraries' / 'ESP32_HTTPS_Server')
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    apply(args.library, args.check)
