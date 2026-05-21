#include "h264_rtp_packetizer.hpp"

#include <algorithm>
#include <stdexcept>

namespace ldc_toy {
namespace {

constexpr std::uint8_t kRtpVersion = 0x80;
constexpr std::uint32_t kH264ClockRate = 90000;

std::uint32_t readU32(const std::vector<std::uint8_t>& data, std::size_t offset) {
  return (std::uint32_t(data[offset]) << 24) |
         (std::uint32_t(data[offset + 1]) << 16) |
         (std::uint32_t(data[offset + 2]) << 8) |
         std::uint32_t(data[offset + 3]);
}

}  // namespace

H264RtpPacketizer::H264RtpPacketizer(RtpPacketizerConfig config)
    : config_(config) {}

std::vector<RtpPacket> H264RtpPacketizer::packetize(const EncodedChunk& chunk) {
  updateParameterSets(chunk);

  auto chunk_nalus = extractNalus(chunk.data);
  if (chunk_nalus.empty()) {
    return {};
  }

  std::vector<std::vector<std::uint8_t>> nalus;
  if (chunk.type == ChunkType::Key && !cached_parameter_sets_.empty()) {
    nalus.insert(nalus.end(), cached_parameter_sets_.begin(), cached_parameter_sets_.end());
  }
  nalus.insert(nalus.end(), chunk_nalus.begin(), chunk_nalus.end());

  const auto rtp_timestamp = webCodecsTimestampToRtp(chunk.timestamp_us);
  std::vector<RtpPacket> packets;

  for (std::size_t i = 0; i < nalus.size(); ++i) {
    // The marker bit is set only on the last RTP packet of the access unit.
    auto nalu_packets = packetizeNalu(nalus[i], rtp_timestamp, i == nalus.size() - 1);
    packets.insert(packets.end(), nalu_packets.begin(), nalu_packets.end());
  }

  return packets;
}

void H264RtpPacketizer::updateParameterSets(const EncodedChunk& chunk) {
  if (!chunk.decoder_config || chunk.decoder_config->description.empty()) {
    return;
  }

  auto parameter_sets = extractParameterSetsFromAvcc(chunk.decoder_config->description);
  if (!parameter_sets.empty()) {
    cached_parameter_sets_ = std::move(parameter_sets);
  }
}

std::vector<std::vector<std::uint8_t>> H264RtpPacketizer::extractNalus(
    const std::vector<std::uint8_t>& data) const {
  auto annex_b = splitAnnexB(data);
  if (!annex_b.empty()) {
    return annex_b;
  }
  return splitLengthPrefixed(data);
}

std::vector<std::vector<std::uint8_t>> H264RtpPacketizer::splitAnnexB(
    const std::vector<std::uint8_t>& data) const {
  struct StartCode {
    std::size_t index;
    std::size_t size;
  };

  std::vector<StartCode> starts;
  for (std::size_t i = 0; i + 2 < data.size(); ++i) {
    if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1) {
      starts.push_back({i, 3});
      i += 2;
    } else if (i + 3 < data.size() && data[i] == 0 && data[i + 1] == 0 &&
               data[i + 2] == 0 && data[i + 3] == 1) {
      starts.push_back({i, 4});
      i += 3;
    }
  }

  std::vector<std::vector<std::uint8_t>> nalus;
  for (std::size_t i = 0; i < starts.size(); ++i) {
    const auto begin = starts[i].index + starts[i].size;
    const auto end = i + 1 < starts.size() ? starts[i + 1].index : data.size();
    if (begin < end) {
      nalus.emplace_back(data.begin() + begin, data.begin() + end);
    }
  }

  return nalus;
}

std::vector<std::vector<std::uint8_t>> H264RtpPacketizer::splitLengthPrefixed(
    const std::vector<std::uint8_t>& data) const {
  std::vector<std::vector<std::uint8_t>> nalus;
  std::size_t offset = 0;

  while (offset + 4 <= data.size()) {
    const auto length = readU32(data, offset);
    const auto begin = offset + 4;
    const auto end = begin + length;
    if (length == 0 || end > data.size()) {
      if (data.empty()) {
        return {};
      }
      return std::vector<std::vector<std::uint8_t>>{data};
    }
    nalus.emplace_back(data.begin() + begin, data.begin() + end);
    offset = end;
  }

  return nalus;
}

std::vector<std::vector<std::uint8_t>> H264RtpPacketizer::extractParameterSetsFromAvcc(
    const std::vector<std::uint8_t>& description) const {
  if (description.size() < 7) {
    return {};
  }

  std::vector<std::vector<std::uint8_t>> sets;
  std::size_t offset = 5;
  const auto sps_count = description[offset++] & 0x1f;

  for (std::uint8_t i = 0; i < sps_count; ++i) {
    if (offset + 2 > description.size()) {
      return sets;
    }
    const auto length = (std::uint16_t(description[offset]) << 8) | description[offset + 1];
    offset += 2;
    if (offset + length > description.size()) {
      return sets;
    }
    sets.emplace_back(description.begin() + offset, description.begin() + offset + length);
    offset += length;
  }

  if (offset >= description.size()) {
    return sets;
  }

  const auto pps_count = description[offset++];
  for (std::uint8_t i = 0; i < pps_count; ++i) {
    if (offset + 2 > description.size()) {
      return sets;
    }
    const auto length = (std::uint16_t(description[offset]) << 8) | description[offset + 1];
    offset += 2;
    if (offset + length > description.size()) {
      return sets;
    }
    sets.emplace_back(description.begin() + offset, description.begin() + offset + length);
    offset += length;
  }

  return sets;
}

std::vector<RtpPacket> H264RtpPacketizer::packetizeNalu(
    const std::vector<std::uint8_t>& nalu,
    std::uint32_t rtp_timestamp,
    bool marker) {
  if (nalu.empty()) {
    return {};
  }

  if (nalu.size() <= config_.max_payload_size) {
    return {buildPacket(nalu, rtp_timestamp, marker)};
  }

  std::vector<RtpPacket> packets;
  const auto nalu_type = nalu[0] & 0x1f;
  const auto nalu_header = nalu[0] & 0xe0;
  std::size_t offset = 1;

  while (offset < nalu.size()) {
    const bool first = offset == 1;
    const auto end = std::min(offset + config_.max_payload_size - 2, nalu.size());
    const bool last = end == nalu.size();

    // FU-A fragmentation: two-byte FU indicator/header followed by the NALU body slice.
    std::vector<std::uint8_t> payload(2 + end - offset);
    payload[0] = nalu_header | 28;
    payload[1] = (first ? 0x80 : 0) | (last ? 0x40 : 0) | nalu_type;
    std::copy(nalu.begin() + offset, nalu.begin() + end, payload.begin() + 2);
    packets.push_back(buildPacket(payload, rtp_timestamp, last && marker));
    offset = end;
  }

  return packets;
}

RtpPacket H264RtpPacketizer::buildPacket(
    const std::vector<std::uint8_t>& payload,
    std::uint32_t rtp_timestamp,
    bool marker) {
  RtpPacket packet;
  packet.bytes.resize(12 + payload.size());
  packet.bytes[0] = kRtpVersion;
  packet.bytes[1] = (marker ? 0x80 : 0) | (config_.payload_type & 0x7f);
  packet.bytes[2] = std::uint8_t(sequence_ >> 8);
  packet.bytes[3] = std::uint8_t(sequence_ & 0xff);
  packet.bytes[4] = std::uint8_t(rtp_timestamp >> 24);
  packet.bytes[5] = std::uint8_t((rtp_timestamp >> 16) & 0xff);
  packet.bytes[6] = std::uint8_t((rtp_timestamp >> 8) & 0xff);
  packet.bytes[7] = std::uint8_t(rtp_timestamp & 0xff);
  packet.bytes[8] = std::uint8_t(config_.ssrc >> 24);
  packet.bytes[9] = std::uint8_t((config_.ssrc >> 16) & 0xff);
  packet.bytes[10] = std::uint8_t((config_.ssrc >> 8) & 0xff);
  packet.bytes[11] = std::uint8_t(config_.ssrc & 0xff);
  std::copy(payload.begin(), payload.end(), packet.bytes.begin() + 12);
  sequence_ = std::uint16_t(sequence_ + 1);
  return packet;
}

std::uint32_t webCodecsTimestampToRtp(std::uint64_t timestamp_us) {
  return std::uint32_t((timestamp_us * kH264ClockRate) / 1'000'000);
}

}  // namespace ldc_toy
