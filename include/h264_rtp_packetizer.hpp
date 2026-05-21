#pragma once

#include "encoded_chunk.hpp"

#include <cstdint>
#include <vector>

namespace ldc_toy {

struct RtpPacket {
  std::vector<std::uint8_t> bytes;
};

struct RtpPacketizerConfig {
  std::uint8_t payload_type = 96;
  std::uint32_t ssrc = 0x12345678;
  std::size_t max_payload_size = 1200;
};

class H264RtpPacketizer {
 public:
  explicit H264RtpPacketizer(RtpPacketizerConfig config);

  std::vector<RtpPacket> packetize(const EncodedChunk& chunk);

 private:
  void updateParameterSets(const EncodedChunk& chunk);
  std::vector<std::vector<std::uint8_t>> extractNalus(const std::vector<std::uint8_t>& data) const;
  std::vector<std::vector<std::uint8_t>> splitAnnexB(const std::vector<std::uint8_t>& data) const;
  std::vector<std::vector<std::uint8_t>> splitLengthPrefixed(const std::vector<std::uint8_t>& data) const;
  std::vector<std::vector<std::uint8_t>> extractParameterSetsFromAvcc(
      const std::vector<std::uint8_t>& description) const;
  std::vector<RtpPacket> packetizeNalu(
      const std::vector<std::uint8_t>& nalu,
      std::uint32_t rtp_timestamp,
      bool marker);
  RtpPacket buildPacket(
      const std::vector<std::uint8_t>& payload,
      std::uint32_t rtp_timestamp,
      bool marker);

  RtpPacketizerConfig config_;
  std::uint16_t sequence_ = 1;
  std::vector<std::vector<std::uint8_t>> cached_parameter_sets_;
};

std::uint32_t webCodecsTimestampToRtp(std::uint64_t timestamp_us);

}  // namespace ldc_toy

