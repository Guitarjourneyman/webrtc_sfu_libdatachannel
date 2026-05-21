#pragma once

#include "encoded_chunk.hpp"
#include "h264_rtp_packetizer.hpp"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <rtc/rtc.hpp>

namespace ldc_toy {

struct LocalDescription {
  std::string peer_id;
  std::string type;
  std::string sdp;
};

struct LocalCandidate {
  std::string peer_id;
  std::string candidate;
  std::string mid;
};

struct SfuCallbacks {
  std::function<void(LocalDescription)> on_local_description;
  std::function<void(LocalCandidate)> on_local_candidate;
};

class NativeSfu {
 public:
  explicit NativeSfu(SfuCallbacks callbacks = {});

  void createViewer(const std::string& peer_id);
  void setViewerOffer(const std::string& peer_id, const std::string& sdp);
  void setViewerAnswer(const std::string& peer_id, const std::string& sdp);
  void addViewerCandidate(
      const std::string& peer_id,
      const std::string& candidate,
      const std::string& mid);

  void removeViewer(const std::string& peer_id);

  // This is the function-call media boundary. A WebSocket/WebTransport/native
  // bridge should parse the browser message and call this method directly.
  void ingest(const EncodedChunk& chunk);
  void ingestBrowserPacket(const std::uint8_t* data, std::size_t size);

 private:
  struct Viewer {
    std::shared_ptr<rtc::PeerConnection> pc;
    std::shared_ptr<rtc::Track> video_track;
    std::shared_ptr<rtc::DataChannel> control_channel;
    std::shared_ptr<rtc::RtcpSrReporter> sender;
    std::uint32_t ssrc = 0;
    std::uint8_t payload_type = 96;
    bool ice_ready = false;
    bool track_ready = false;
    std::uint64_t sent_packets = 0;
    std::uint64_t failed_sends = 0;
  };

  std::shared_ptr<Viewer> findViewerLocked(const std::string& peer_id);
  void configureViewerTrack(
      const std::string& peer_id,
      const std::shared_ptr<Viewer>& viewer,
      const std::shared_ptr<rtc::Track>& track);
  void cacheDecoderConfig(const EncodedChunk& chunk);
  std::vector<std::uint8_t> buildAvccSample(const EncodedChunk& chunk) const;

  SfuCallbacks callbacks_;
  rtc::Configuration rtc_config_;
  H264RtpPacketizer packetizer_;
  std::vector<std::vector<std::uint8_t>> cached_parameter_sets_;
  std::mutex mutex_;
  std::unordered_map<std::string, std::shared_ptr<Viewer>> viewers_;
};

}  // namespace ldc_toy
