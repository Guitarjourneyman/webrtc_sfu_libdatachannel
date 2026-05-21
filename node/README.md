# Single-process Node + libdatachannel SFU toy

This folder adds the next layer above the C++ core:

```text
Browser publisher
  WebCodecs EncodedChunk
  |
  | ws://127.0.0.1:8000/ingest
  v
Node.js signaling process
  nativeSfu.ingestBrowserPacket(buffer)
  |
  | function call into Node native addon
  v
C++ NativeSfu / libdatachannel
  H264 RTP packetization
  rtc::Track::send()
```

Ports:

- `3000`: browser client app server
- `8000`: signaling and encoded chunk ingest server

The intended final deployment is one Node.js process with a native addon loaded
in-process. There is no mediasoup worker executable. libdatachannel still uses
internal networking threads for ICE/DTLS/SRTP, but it is not a separate SFU
process.

## Build and run

If `vendor/libdatachannel` and `vendor/mbedtls` are not built yet, prepare the
native dependencies from `C:\VScode\webCodecs\my-webcodec_mediasoup\libdatachannel`:

```powershell
git clone https://github.com/paullouisageneau/libdatachannel.git vendor\libdatachannel
cd vendor\libdatachannel
git submodule update --init --recursive

cd ..\..
git clone https://github.com/Mbed-TLS/mbedtls.git vendor\mbedtls
cd vendor\mbedtls
git checkout v3.6.5
git submodule update --init --recursive
```

Enable DTLS-SRTP in `vendor/mbedtls/include/mbedtls/mbedtls_config.h`:

```c
#define MBEDTLS_SSL_DTLS_SRTP
```

Then build MbedTLS and libdatachannel:

```powershell
cd C:\VScode\webCodecs\my-webcodec_mediasoup\libdatachannel\vendor\mbedtls
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DENABLE_TESTING=OFF -DENABLE_PROGRAMS=OFF -DBUILD_SHARED_LIBS=OFF
cmake --build build --config Release

cd C:\VScode\webCodecs\my-webcodec_mediasoup\libdatachannel\vendor\libdatachannel
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DNO_EXAMPLES=ON -DNO_TESTS=ON -DBUILD_SHARED_LIBS=OFF -DBUILD_SHARED_DEPS_LIBS=OFF -DUSE_MBEDTLS=ON -DMbedTLS_INCLUDE_DIR="C:\VScode\webCodecs\my-webcodec_mediasoup\libdatachannel\vendor\mbedtls\include" -DMbedTLS_LIBRARY="C:\VScode\webCodecs\my-webcodec_mediasoup\libdatachannel\vendor\mbedtls\build\library\Release\mbedtls.lib" -DMbedCrypto_LIBRARY="C:\VScode\webCodecs\my-webcodec_mediasoup\libdatachannel\vendor\mbedtls\build\library\Release\mbedcrypto.lib" -DMbedX509_LIBRARY="C:\VScode\webCodecs\my-webcodec_mediasoup\libdatachannel\vendor\mbedtls\build\library\Release\mbedx509.lib"
cmake --build build --config Release --target datachannel-static
```

Build and run the Node app:

```powershell
cd C:\VScode\webCodecs\my-webcodec_mediasoup\libdatachannel\node
npm install
npm run build:native
npm run build
npm run dev
```

The server intentionally fails at startup if
`build/Release/ldc_sfu_node.node` is missing. That keeps the architecture honest:
Node.js must call the in-process C++ SFU addon, not a mock or an external SFU
worker process.

On Windows, `npm run build:native` requires Visual Studio Build Tools with:

- Desktop development with C++
- MSVC v143 or v142 toolset
- Windows 10/11 SDK

This package intentionally skips native compilation during `npm install`.
Native compilation is done only by `npm run build:native`, so dependency
installation and C++ build failures are easier to separate.

`binding.gyp` needs a libdatachannel header directory and library file. By
default it expects:

```text
../vendor/libdatachannel/include/rtc/rtc.hpp
../vendor/libdatachannel/build/Release/datachannel-static.lib
```

You can override those paths:

```powershell
$env:LIBDATACHANNEL_INCLUDE="C:\path\to\libdatachannel\include"
$env:LIBDATACHANNEL_LIB="C:\path\to\datachannel-static.lib"
npm run doctor:native
npm run build:native
```

For a self-contained runtime, keep the libdatachannel build vendored next to
this project and copy any required DLLs beside `build/Release/ldc_sfu_node.node`.

## Ports

- `3000`: browser client app
- `8000`: signaling and encoded chunk ingest
