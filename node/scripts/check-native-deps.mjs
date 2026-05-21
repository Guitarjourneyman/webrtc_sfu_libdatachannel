import fs from 'node:fs';
import path from 'node:path';

const includeDir =
  process.env.LIBDATACHANNEL_INCLUDE ??
  path.resolve('..', 'vendor', 'libdatachannel', 'include');
const libPath =
  process.env.LIBDATACHANNEL_LIB ??
  path.resolve('..', 'vendor', 'libdatachannel', 'build', 'Release', 'datachannel-static.lib');

const headerPath = path.join(includeDir, 'rtc', 'rtc.hpp');

console.log(`LIBDATACHANNEL_INCLUDE=${includeDir}`);
console.log(`LIBDATACHANNEL_LIB=${libPath}`);

const missing = [];
if (!fs.existsSync(headerPath)) {
  missing.push(`missing header: ${headerPath}`);
}
if (!fs.existsSync(libPath)) {
  missing.push(`missing library: ${libPath}`);
}

const staticDeps = [
  path.resolve('..', 'vendor', 'libdatachannel', 'build', 'deps', 'libjuice', 'Release', 'juice-static.lib'),
  path.resolve('..', 'vendor', 'libdatachannel', 'build', 'deps', 'libsrtp', 'Release', 'srtp2.lib'),
  path.resolve('..', 'vendor', 'libdatachannel', 'build', 'deps', 'usrsctp', 'usrsctplib', 'Release', 'usrsctp.lib'),
  path.resolve('..', 'vendor', 'mbedtls', 'build', 'library', 'Release', 'mbedtls.lib'),
  path.resolve('..', 'vendor', 'mbedtls', 'build', 'library', 'Release', 'mbedx509.lib'),
  path.resolve('..', 'vendor', 'mbedtls', 'build', 'library', 'Release', 'mbedcrypto.lib'),
  path.resolve('..', 'vendor', 'mbedtls', 'build', '3rdparty', 'everest', 'Release', 'everest.lib'),
  path.resolve('..', 'vendor', 'mbedtls', 'build', '3rdparty', 'p256-m', 'Release', 'p256m.lib')
];

for (const dep of staticDeps) {
  if (!fs.existsSync(dep)) {
    missing.push(`missing static dependency: ${dep}`);
  }
}

if (missing.length > 0) {
  for (const item of missing) {
    console.error(item);
  }
  process.exit(1);
}

console.log('libdatachannel native dependency paths look valid.');
