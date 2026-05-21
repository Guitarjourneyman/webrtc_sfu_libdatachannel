{
  "targets": [
    {
      "target_name": "ldc_sfu_node",
      "sources": [
        "src/addon/ldc_sfu_node.cpp",
        "../src/encoded_chunk.cpp",
        "../src/h264_rtp_packetizer.cpp",
        "../src/native_sfu.cpp"
      ],
      "include_dirs": [
        "<!(node -p \"require('node-addon-api').include_dir\")",
        "../include",
        "<!(node -e \"process.stdout.write(process.env.LIBDATACHANNEL_INCLUDE || '../vendor/libdatachannel/include')\")"
      ],
      "dependencies": [
        "<!(node -p \"require('node-addon-api').gyp\")"
      ],
      "defines": [
        "NAPI_DISABLE_CPP_EXCEPTIONS",
        "RTC_STATIC"
      ],
      "cflags_cc": [
        "-std=c++17"
      ],
      "msvs_settings": {
        "VCCLCompilerTool": {
          "RuntimeLibrary": "MultiThreadedDLL",
          "ExceptionHandling": 1,
          "AdditionalOptions": ["/std:c++17", "/MD", "/EHsc"]
        }
      },
      "libraries": [
        "<!(node -e \"const path=require('path'); process.stdout.write(process.env.LIBDATACHANNEL_LIB || path.resolve('..','vendor','libdatachannel','build','Release','datachannel-static.lib'))\")",
        "<!(node -e \"const path=require('path'); process.stdout.write(path.resolve('..','vendor','libdatachannel','build','deps','libjuice','Release','juice-static.lib'))\")",
        "<!(node -e \"const path=require('path'); process.stdout.write(path.resolve('..','vendor','libdatachannel','build','deps','libsrtp','Release','srtp2.lib'))\")",
        "<!(node -e \"const path=require('path'); process.stdout.write(path.resolve('..','vendor','libdatachannel','build','deps','usrsctp','usrsctplib','Release','usrsctp.lib'))\")",
        "<!(node -e \"const path=require('path'); process.stdout.write(path.resolve('..','vendor','mbedtls','build','library','Release','mbedtls.lib'))\")",
        "<!(node -e \"const path=require('path'); process.stdout.write(path.resolve('..','vendor','mbedtls','build','library','Release','mbedx509.lib'))\")",
        "<!(node -e \"const path=require('path'); process.stdout.write(path.resolve('..','vendor','mbedtls','build','library','Release','mbedcrypto.lib'))\")",
        "<!(node -e \"const path=require('path'); process.stdout.write(path.resolve('..','vendor','mbedtls','build','3rdparty','everest','Release','everest.lib'))\")",
        "<!(node -e \"const path=require('path'); process.stdout.write(path.resolve('..','vendor','mbedtls','build','3rdparty','p256-m','Release','p256m.lib'))\")",
        "ws2_32.lib",
        "crypt32.lib",
        "bcrypt.lib",
        "iphlpapi.lib"
      ]
    }
  ]
}
