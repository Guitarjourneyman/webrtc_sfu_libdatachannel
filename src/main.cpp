#include "native_sfu.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace {

ldc_toy::EncodedChunk makeFakeAnnexBChunk(std::uint64_t timestamp_us, bool key) {
  // This is not a decodable video frame. It only exercises the parser and
  // packetizer path without needing a camera or browser during early testing.
  std::vector<std::uint8_t> bytes;
  const std::vector<std::uint8_t> start{0x00, 0x00, 0x00, 0x01};
  const std::vector<std::uint8_t> idr{0x65, 0x88, 0x84, 0x21, 0xa0};
  const std::vector<std::uint8_t> p{0x41, 0x9a, 0x22, 0x11, 0x00};

  bytes.insert(bytes.end(), start.begin(), start.end());
  bytes.insert(bytes.end(), key ? idr.begin() : p.begin(), key ? idr.end() : p.end());

  ldc_toy::EncodedChunk chunk;
  chunk.type = key ? ldc_toy::ChunkType::Key : ldc_toy::ChunkType::Delta;
  chunk.timestamp_us = timestamp_us;
  chunk.duration_us = 33'333;
  chunk.data = std::move(bytes);
  chunk.decoder_config = std::nullopt;
  return chunk;
}

}  // namespace

int main() {
  rtc::InitLogger(rtc::LogLevel::Info);

  ldc_toy::SfuCallbacks callbacks;
  callbacks.on_local_description = [](ldc_toy::LocalDescription desc) {
    std::cout << "\n=== Send this " << desc.type << " SDP to viewer "
              << desc.peer_id << " ===\n"
              << desc.sdp << "\n";
  };
  callbacks.on_local_candidate = [](ldc_toy::LocalCandidate cand) {
    std::cout << "candidate for " << cand.peer_id << " mid=" << cand.mid
              << " " << cand.candidate << "\n";
  };

  ldc_toy::NativeSfu sfu(callbacks);

  std::cout << "Creating one viewer PeerConnection.\n";
  std::cout << "Wire the printed offer/ICE into a browser signaling adapter later.\n";
  sfu.createViewer("viewer-1");

  std::cout << "Pushing fake WebCodecs chunks through C++ function calls.\n";
  for (std::uint64_t frame = 0; frame < 90; ++frame) {
    const bool key = frame % 60 == 0;
    sfu.ingest(makeFakeAnnexBChunk(frame * 33'333, key));
    std::this_thread::sleep_for(33ms);
  }

  std::cout << "Done. Add a WebSocket/WebTransport adapter that calls sfu.ingest().\n";
  return 0;
}
