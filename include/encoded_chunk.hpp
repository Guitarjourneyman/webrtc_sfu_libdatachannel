#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ldc_toy {

enum class ChunkType {
  Key,
  Delta
};

struct DecoderConfig {
  std::string codec;
  std::vector<std::uint8_t> description;
};

struct EncodedChunk {
  ChunkType type = ChunkType::Delta;
  std::uint64_t timestamp_us = 0;
  std::optional<std::uint64_t> duration_us;
  std::vector<std::uint8_t> data;
  std::optional<DecoderConfig> decoder_config;
};

}  // namespace ldc_toy

