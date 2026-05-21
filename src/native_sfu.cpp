#include "native_sfu.hpp"

#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace ldc_toy {
namespace {

constexpr std::uint8_t kPayloadType = 96;
constexpr std::uint32_t kPublisherSsrc = 0x12345678;

rtc::Description::Video buildSendOnlyH264Description(std::uint32_t ssrc) {
  // libdatachannel owns the SDP media description. We only declare that this
  // track will send H.264 RTP packets with the same payload type as our packetizer.
  rtc::Description::Video video("video0", rtc::Description::Direction::SendOnly);
  video.addH264Codec(kPayloadType);
  video.addSSRC(ssrc, "webcodecs-libdatachannel-toy", "stream0", "video0");
  return video;
}

const char* toString(rtc::PeerConnection::State state) {
  switch (state) {
    case rtc::PeerConnection::State::New:
      return "new";
    case rtc::PeerConnection::State::Connecting:
      return "connecting";
    case rtc::PeerConnection::State::Connected:
      return "connected";
    case rtc::PeerConnection::State::Disconnected:
      return "disconnected";
    case rtc::PeerConnection::State::Failed:
      return "failed";
    case rtc::PeerConnection::State::Closed:
      return "closed";
  }
  return "unknown";
}

const char* toString(rtc::PeerConnection::IceState state) {
  switch (state) {
    case rtc::PeerConnection::IceState::New:
      return "new";
    case rtc::PeerConnection::IceState::Checking:
      return "checking";
    case rtc::PeerConnection::IceState::Connected:
      return "connected";
    case rtc::PeerConnection::IceState::Completed:
      return "completed";
    case rtc::PeerConnection::IceState::Failed:
      return "failed";
    case rtc::PeerConnection::IceState::Disconnected:
      return "disconnected";
    case rtc::PeerConnection::IceState::Closed:
      return "closed";
  }
  return "unknown";
}

const char* toString(rtc::PeerConnection::GatheringState state) {
  switch (state) {
    case rtc::PeerConnection::GatheringState::New:
      return "new";
    case rtc::PeerConnection::GatheringState::InProgress:
      return "in-progress";
    case rtc::PeerConnection::GatheringState::Complete:
      return "complete";
  }
  return "unknown";
}

void appendLengthPrefixedNalu(
    std::vector<std::uint8_t>& out,
    const std::vector<std::uint8_t>& nalu) {
  const auto size = static_cast<std::uint32_t>(nalu.size());
  out.push_back(static_cast<std::uint8_t>((size >> 24) & 0xff));
  out.push_back(static_cast<std::uint8_t>((size >> 16) & 0xff));
  out.push_back(static_cast<std::uint8_t>((size >> 8) & 0xff));
  out.push_back(static_cast<std::uint8_t>(size & 0xff));
  out.insert(out.end(), nalu.begin(), nalu.end());
}

std::uint16_t readU16(const std::uint8_t* p) {
  return (std::uint16_t(p[0]) << 8) | std::uint16_t(p[1]);
}

}  // namespace

NativeSfu::NativeSfu(SfuCallbacks callbacks)
    : callbacks_(std::move(callbacks)),
      packetizer_([] {
        RtpPacketizerConfig config;
        config.payload_type = kPayloadType;
        config.ssrc = kPublisherSsrc;
        return config;
      }()) {
  rtc::InitLogger(rtc::LogLevel::Info);
  rtc_config_.iceServers.emplace_back("stun:stun.l.google.com:19302");
  rtc_config_.disableAutoNegotiation = true;
  rtc_config_.forceMediaTransport = true;
}

void NativeSfu::createViewer(const std::string& peer_id) {
  auto viewer = std::make_shared<Viewer>();
  viewer->ssrc = kPublisherSsrc;
  viewer->pc = std::make_shared<rtc::PeerConnection>(rtc_config_);

  viewer->pc->onLocalDescription([this, peer_id](rtc::Description description) {
    std::cout << "[viewer:" << peer_id << "] local description "
              << description.typeString() << "\n";
    if (callbacks_.on_local_description) {
      callbacks_.on_local_description(
          LocalDescription{peer_id, description.typeString(), std::string(description)});
    }
  });

  viewer->pc->onLocalCandidate([this, peer_id](rtc::Candidate candidate) {
    std::cout << "[viewer:" << peer_id << "] local candidate mid="
              << candidate.mid() << "\n";
    if (callbacks_.on_local_candidate) {
      callbacks_.on_local_candidate(
          LocalCandidate{peer_id, candidate.candidate(), candidate.mid()});
    }
  });

  viewer->pc->onStateChange([this, peer_id](rtc::PeerConnection::State state) {
    std::cout << "[viewer:" << peer_id << "] state=" << toString(state)
              << "(" << int(state) << ")\n";
    // Do not destroy the PeerConnection from inside its own callback. Some
    // libdatachannel callbacks run on internal networking threads, so erasing
    // the viewer here can tear down objects still in use by the callback stack.
    if (state == rtc::PeerConnection::State::Closed ||
        state == rtc::PeerConnection::State::Failed) {
      std::lock_guard lock(mutex_);
      auto viewer = findViewerLocked(peer_id);
      if (viewer) {
        viewer->ice_ready = false;
      }
    }
  });
  viewer->pc->onIceStateChange([this, peer_id](rtc::PeerConnection::IceState state) {
    std::cout << "[viewer:" << peer_id << "] ice=" << toString(state)
              << "(" << int(state) << ")\n";
    std::lock_guard lock(mutex_);
    auto viewer = findViewerLocked(peer_id);
    if (viewer) {
      viewer->ice_ready = state == rtc::PeerConnection::IceState::Connected ||
                          state == rtc::PeerConnection::IceState::Completed;
    }
  });
  viewer->pc->onGatheringStateChange([peer_id](rtc::PeerConnection::GatheringState state) {
    std::cout << "[viewer:" << peer_id << "] gathering=" << toString(state)
              << "(" << int(state) << ")\n";
  });

  viewer->pc->onDataChannel([this, peer_id](std::shared_ptr<rtc::DataChannel> channel) {
    std::cout << "[viewer:" << peer_id << "] remote datachannel "
              << channel->label() << "\n";
    channel->onOpen([peer_id, label = channel->label()]() {
      std::cout << "[viewer:" << peer_id << "] control channel open "
                << label << "\n";
    });
    std::lock_guard lock(mutex_);
    auto viewer = findViewerLocked(peer_id);
    if (viewer) {
      viewer->control_channel = std::move(channel);
    }
  });

  viewer->pc->onTrack([this, peer_id](std::shared_ptr<rtc::Track> track) {
    std::lock_guard lock(mutex_);
    auto viewer = findViewerLocked(peer_id);
    if (viewer) {
      configureViewerTrack(peer_id, viewer, track);
    }
  });

  {
    std::lock_guard lock(mutex_);
    viewers_[peer_id] = viewer;
  }

}

void NativeSfu::setViewerOffer(const std::string& peer_id, const std::string& sdp) {
  std::shared_ptr<Viewer> viewer;
  {
    std::lock_guard lock(mutex_);
    viewer = findViewerLocked(peer_id);
  }
  if (!viewer) {
    throw std::runtime_error("unknown viewer: " + peer_id);
  }
  std::cout << "[viewer:" << peer_id << "] remote offer received\n";
  viewer->pc->setRemoteDescription(rtc::Description(sdp, "offer"));
  viewer->pc->setLocalDescription(rtc::Description::Type::Answer);
}

void NativeSfu::setViewerAnswer(const std::string& peer_id, const std::string& sdp) {
  std::shared_ptr<Viewer> viewer;
  {
    std::lock_guard lock(mutex_);
    viewer = findViewerLocked(peer_id);
  }
  if (!viewer) {
    throw std::runtime_error("unknown viewer: " + peer_id);
  }
  std::cout << "[viewer:" << peer_id << "] remote answer received\n";
  viewer->pc->setRemoteDescription(rtc::Description(sdp, "answer"));
}

void NativeSfu::addViewerCandidate(
    const std::string& peer_id,
    const std::string& candidate,
    const std::string& mid) {
  std::shared_ptr<Viewer> viewer;
  {
    std::lock_guard lock(mutex_);
    viewer = findViewerLocked(peer_id);
  }
  if (!viewer) {
    throw std::runtime_error("unknown viewer: " + peer_id);
  }
  std::cout << "[viewer:" << peer_id << "] remote candidate mid=" << mid << "\n";
  viewer->pc->addRemoteCandidate(rtc::Candidate(candidate, mid));
}

void NativeSfu::removeViewer(const std::string& peer_id) {
  std::lock_guard lock(mutex_);
  viewers_.erase(peer_id);
}

void NativeSfu::ingest(const EncodedChunk& chunk) {
  cacheDecoderConfig(chunk);
  const auto sample_bytes = buildAvccSample(chunk);
  if (sample_bytes.empty()) {
    return;
  }

  rtc::binary sample(sample_bytes.size());
  std::memcpy(sample.data(), sample_bytes.data(), sample_bytes.size());

  // FrameInfo expects the frame timestamp, not the per-frame duration.
  // Passing a constant duration here makes RTP timestamps almost static, which
  // causes browsers to render very slowly even though packets are being sent.
  rtc::FrameInfo frame_info(
      std::chrono::duration<double, std::micro>(chunk.timestamp_us));
  frame_info.isKeyFrame = chunk.type == ChunkType::Key;

  std::vector<std::shared_ptr<Viewer>> viewers;
  {
    std::lock_guard lock(mutex_);
    viewers.reserve(viewers_.size());
    for (auto& [_, viewer] : viewers_) {
      viewers.push_back(viewer);
    }
  }

  for (const auto& viewer : viewers) {
    if (!viewer->video_track || !viewer->ice_ready || !viewer->track_ready) {
      continue;
    }

    try {
      viewer->video_track->sendFrame(sample, frame_info);
      viewer->sent_packets++;
      if (viewer->sent_packets <= 5 || viewer->sent_packets % 60 == 0) {
        std::cout << "[viewer] sent H264 frames=" << viewer->sent_packets
                  << " bytes=" << sample.size() << "\n";
      }
    } catch (const std::exception& e) {
      viewer->failed_sends++;
      if (viewer->failed_sends <= 5 || viewer->failed_sends % 60 == 0) {
        std::cerr << "[viewer] sendFrame failed count=" << viewer->failed_sends
                  << " error=" << e.what() << "\n";
      }
    } catch (...) {
      viewer->failed_sends++;
      std::cerr << "[viewer] sendFrame failed with unknown native error\n";
    }
  }
}

void NativeSfu::ingestBrowserPacket(const std::uint8_t* data, std::size_t size) {
  if (size < 21) {
    throw std::runtime_error("browser packet is too small");
  }

  // Compact binary format used by libdatachannel/node/public/client.js:
  // byte 0: flags bit 0 = key frame
  // bytes 1..8: WebCodecs timestamp in microseconds, big endian
  // bytes 9..16: duration in microseconds, big endian, 0 when unknown
  // bytes 17..20: decoder config length, big endian
  // bytes 21..N: optional decoder config followed by encoded chunk bytes.
  auto read_u64 = [](const std::uint8_t* p) {
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
      value = (value << 8) | p[i];
    }
    return value;
  };
  auto read_u32 = [](const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
           (std::uint32_t(p[2]) << 8) | std::uint32_t(p[3]);
  };

  const auto flags = data[0];
  const auto timestamp_us = read_u64(data + 1);
  const auto duration_us = read_u64(data + 9);
  const auto config_size = read_u32(data + 17);
  if (21 + config_size > size) {
    throw std::runtime_error("browser packet decoder config is truncated");
  }

  EncodedChunk chunk;
  chunk.type = (flags & 0x01) ? ChunkType::Key : ChunkType::Delta;
  chunk.timestamp_us = timestamp_us;
  if (duration_us != 0) {
    chunk.duration_us = duration_us;
  }

  if (config_size > 0) {
    DecoderConfig config;
    config.codec = "avc1.42E01F";
    config.description.assign(data + 21, data + 21 + config_size);
    chunk.decoder_config = std::move(config);
  }

  const auto payload_offset = 21 + config_size;
  chunk.data.assign(data + payload_offset, data + size);
  ingest(chunk);
}

std::shared_ptr<NativeSfu::Viewer> NativeSfu::findViewerLocked(const std::string& peer_id) {
  auto it = viewers_.find(peer_id);
  return it == viewers_.end() ? nullptr : it->second;
}

void NativeSfu::configureViewerTrack(
    const std::string& peer_id,
    const std::shared_ptr<Viewer>& viewer,
    const std::shared_ptr<rtc::Track>& track) {
  auto desc = track->description();
  desc.clearSSRCs();
  desc.addSSRC(viewer->ssrc, "webcodecs-libdatachannel-toy", "stream0", "video0");
  track->setDescription(desc);

  std::uint8_t payload_type = kPayloadType;
  for (const auto pt : desc.payloadTypes()) {
    const auto* rtp_map = desc.rtpMap(pt);
    if (rtp_map && rtp_map->format == "H264") {
      payload_type = static_cast<std::uint8_t>(pt);
      break;
    }
  }

  viewer->payload_type = payload_type;
  viewer->video_track = track;

  auto rtp_config = std::make_shared<rtc::RtpPacketizationConfig>(
      viewer->ssrc,
      "webcodecs-libdatachannel-toy",
      viewer->payload_type,
      rtc::H264RtpPacketizer::ClockRate);
  auto packetizer = std::make_shared<rtc::H264RtpPacketizer>(
      rtc::NalUnit::Separator::Length,
      rtp_config);
  viewer->sender = std::make_shared<rtc::RtcpSrReporter>(rtp_config);
  packetizer->addToChain(viewer->sender);
  packetizer->addToChain(std::make_shared<rtc::RtcpNackResponder>());
  viewer->video_track->setMediaHandler(packetizer);

  viewer->video_track->onOpen([this, peer_id]() {
    std::cout << "[viewer:" << peer_id << "] video track open\n";
    std::lock_guard lock(mutex_);
    auto viewer = findViewerLocked(peer_id);
    if (viewer) {
      viewer->track_ready = true;
    }
  });

  std::cout << "[viewer:" << peer_id << "] video track configured pt="
            << int(viewer->payload_type) << "\n";
}

void NativeSfu::cacheDecoderConfig(const EncodedChunk& chunk) {
  if (!chunk.decoder_config || chunk.decoder_config->description.empty()) {
    return;
  }

  const auto& avcc = chunk.decoder_config->description;
  if (avcc.size() < 7) {
    return;
  }

  std::vector<std::vector<std::uint8_t>> parameter_sets;
  std::size_t offset = 5;
  const auto sps_count = avcc[offset++] & 0x1f;
  for (std::uint8_t i = 0; i < sps_count; ++i) {
    if (offset + 2 > avcc.size()) {
      return;
    }
    const auto nalu_size = readU16(avcc.data() + offset);
    offset += 2;
    if (offset + nalu_size > avcc.size()) {
      return;
    }
    parameter_sets.emplace_back(avcc.begin() + offset, avcc.begin() + offset + nalu_size);
    offset += nalu_size;
  }

  if (offset >= avcc.size()) {
    return;
  }
  const auto pps_count = avcc[offset++];
  for (std::uint8_t i = 0; i < pps_count; ++i) {
    if (offset + 2 > avcc.size()) {
      return;
    }
    const auto nalu_size = readU16(avcc.data() + offset);
    offset += 2;
    if (offset + nalu_size > avcc.size()) {
      return;
    }
    parameter_sets.emplace_back(avcc.begin() + offset, avcc.begin() + offset + nalu_size);
    offset += nalu_size;
  }

  if (!parameter_sets.empty()) {
    cached_parameter_sets_ = std::move(parameter_sets);
  }
}

std::vector<std::uint8_t> NativeSfu::buildAvccSample(const EncodedChunk& chunk) const {
  if (chunk.type != ChunkType::Key || cached_parameter_sets_.empty()) {
    return chunk.data;
  }

  std::vector<std::uint8_t> sample;
  std::size_t size = chunk.data.size();
  for (const auto& nalu : cached_parameter_sets_) {
    size += 4 + nalu.size();
  }
  sample.reserve(size);
  for (const auto& nalu : cached_parameter_sets_) {
    appendLengthPrefixedNalu(sample, nalu);
  }
  sample.insert(sample.end(), chunk.data.begin(), chunk.data.end());
  return sample;
}

}  // namespace ldc_toy
