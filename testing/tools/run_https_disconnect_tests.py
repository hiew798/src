"""Compile selected installed library methods against fault-injecting TLS/socket doubles."""
import argparse
from pathlib import Path
import subprocess
import tempfile

METHODS = {
    'HTTPConnection.cpp': ['HTTPConnection::~HTTPConnection()', 'bool HTTPConnection::isClosed()',
        'void HTTPConnection::closeConnection()', 'int HTTPConnection::updateBuffer()',
        'size_t HTTPConnection::readBuffer(', 'size_t HTTPConnection::pendingBufferSize()'],
    'HTTPSConnection.cpp': ['void HTTPSConnection::closeConnection()', 'size_t HTTPSConnection::writeBuffer(',
        'size_t HTTPSConnection::readBytesToBuffer(', 'size_t HTTPSConnection::pendingByteCount()',
        'bool HTTPSConnection::canReadData()'],
    'HTTPRequest.cpp': ['HTTPRequest::~HTTPRequest()', 'size_t HTTPRequest::readBytes(',
        'bool HTTPRequest::requestComplete()', 'void HTTPRequest::discardRequestBody()'],
}


def extract(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    project = Path(__file__).resolve().parents[1]
    parser.add_argument('--library', type=Path, default=project.parent / 'libraries' / 'ESP32_HTTPS_Server')
    args = parser.parse_args()
    snippets = []
    for name, signatures in METHODS.items():
        source = (args.library / 'src' / name).read_text(encoding='utf-8')
        snippets.extend(extract(source, signature) for signature in signatures)
    harness = (project / 'tests' / 'https_disconnect_tests.cpp').read_text(encoding='utf-8')
    with tempfile.TemporaryDirectory(prefix='heltec-https-regression-') as out:
        folder = Path(out)
        (folder / 'regression.cpp').write_text(harness.replace('// PRODUCTION_METHODS', '\n\n'.join(snippets)), encoding='utf-8')
        # Pass a saved batch file to cmd; no shell interpolation of source strings.
        vcvars = Path(r'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat')
        script = folder / 'run.cmd'
        script.write_text(f'@echo off\ncall "{vcvars}" >nul\nif errorlevel 1 exit /b 1\n'
                          'cl /nologo /EHsc /std:c++17 regression.cpp /Fe:regression.exe\n'
                          'if errorlevel 1 exit /b 1\nregression.exe\nexit /b %errorlevel%\n')
        subprocess.run(['cmd.exe', '/d', '/c', str(script)], cwd=folder, check=True)


if __name__ == '__main__':
    main()
