#include "native_sfu.hpp"

#include <napi.h>

#include <memory>
#include <string>
#include <utility>

namespace {

struct JsEvent {
  std::string type;
  std::string peer_id;
  std::string description_type;
  std::string sdp;
  std::string candidate;
  std::string mid;
};

class NativeSfuWrap final : public Napi::ObjectWrap<NativeSfuWrap> {
 public:
  static Napi::Object Init(Napi::Env env, Napi::Object exports) {
    auto ctor = DefineClass(
        env,
        "NativeSfu",
        {
            InstanceMethod("createViewer", &NativeSfuWrap::CreateViewer),
            InstanceMethod("setViewerOffer", &NativeSfuWrap::SetViewerOffer),
            InstanceMethod("setViewerAnswer", &NativeSfuWrap::SetViewerAnswer),
            InstanceMethod("addViewerCandidate", &NativeSfuWrap::AddViewerCandidate),
            InstanceMethod("removeViewer", &NativeSfuWrap::RemoveViewer),
            InstanceMethod("ingestBrowserPacket", &NativeSfuWrap::IngestBrowserPacket),
            InstanceMethod("close", &NativeSfuWrap::Close),
        });
    exports.Set("NativeSfu", ctor);
    return exports;
  }

  NativeSfuWrap(const Napi::CallbackInfo& info)
      : Napi::ObjectWrap<NativeSfuWrap>(info) {
    Napi::Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsFunction()) {
      Napi::TypeError::New(env, "NativeSfu requires an event callback").ThrowAsJavaScriptException();
      return;
    }

    event_callback_ = Napi::ThreadSafeFunction::New(
        env,
        info[0].As<Napi::Function>(),
        "NativeSfuEvents",
        0,
        1);

    ldc_toy::SfuCallbacks callbacks;
    callbacks.on_local_description = [this](ldc_toy::LocalDescription desc) {
      EmitLocalDescription(std::move(desc));
    };
    callbacks.on_local_candidate = [this](ldc_toy::LocalCandidate cand) {
      EmitLocalCandidate(std::move(cand));
    };

    sfu_ = std::make_unique<ldc_toy::NativeSfu>(std::move(callbacks));
  }

  ~NativeSfuWrap() override {
    sfu_.reset();
    ReleaseEventCallback();
  }

 private:
  void EmitLocalDescription(ldc_toy::LocalDescription desc) {
    auto* event = new JsEvent();
    event->type = "localDescription";
    event->peer_id = std::move(desc.peer_id);
    event->description_type = std::move(desc.type);
    event->sdp = std::move(desc.sdp);
    event_callback_.BlockingCall(event, [](Napi::Env env, Napi::Function cb, JsEvent* event) {
      auto js_event = Napi::Object::New(env);
      js_event.Set("type", event->type);
      js_event.Set("peerId", event->peer_id);
      js_event.Set("descriptionType", event->description_type);
      js_event.Set("sdp", event->sdp);
      cb.Call({js_event});
      delete event;
    });
  }

  void EmitLocalCandidate(ldc_toy::LocalCandidate cand) {
    auto* event = new JsEvent();
    event->type = "localCandidate";
    event->peer_id = std::move(cand.peer_id);
    event->candidate = std::move(cand.candidate);
    event->mid = std::move(cand.mid);
    event_callback_.BlockingCall(event, [](Napi::Env env, Napi::Function cb, JsEvent* event) {
      auto js_event = Napi::Object::New(env);
      js_event.Set("type", event->type);
      js_event.Set("peerId", event->peer_id);
      js_event.Set("candidate", event->candidate);
      js_event.Set("mid", event->mid);
      cb.Call({js_event});
      delete event;
    });
  }

  Napi::Value CreateViewer(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    const auto peer_id = info[0].As<Napi::String>().Utf8Value();
    sfu_->createViewer(peer_id);
    return env.Undefined();
  }

  Napi::Value SetViewerAnswer(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    const auto peer_id = info[0].As<Napi::String>().Utf8Value();
    const auto sdp = info[1].As<Napi::String>().Utf8Value();
    sfu_->setViewerAnswer(peer_id, sdp);
    return env.Undefined();
  }

  Napi::Value SetViewerOffer(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    const auto peer_id = info[0].As<Napi::String>().Utf8Value();
    const auto sdp = info[1].As<Napi::String>().Utf8Value();
    sfu_->setViewerOffer(peer_id, sdp);
    return env.Undefined();
  }

  Napi::Value AddViewerCandidate(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    const auto peer_id = info[0].As<Napi::String>().Utf8Value();
    const auto candidate = info[1].As<Napi::String>().Utf8Value();
    const auto mid = info[2].As<Napi::String>().Utf8Value();
    sfu_->addViewerCandidate(peer_id, candidate, mid);
    return env.Undefined();
  }

  Napi::Value RemoveViewer(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    const auto peer_id = info[0].As<Napi::String>().Utf8Value();
    sfu_->removeViewer(peer_id);
    return env.Undefined();
  }

  Napi::Value IngestBrowserPacket(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (!info[0].IsBuffer()) {
      Napi::TypeError::New(env, "ingestBrowserPacket expects a Buffer").ThrowAsJavaScriptException();
      return env.Undefined();
    }
    auto buffer = info[0].As<Napi::Buffer<std::uint8_t>>();
    sfu_->ingestBrowserPacket(buffer.Data(), buffer.Length());
    return env.Undefined();
  }

  Napi::Value Close(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    sfu_.reset();
    rtc::Cleanup().wait();
    ReleaseEventCallback();
    return env.Undefined();
  }

  void ReleaseEventCallback() {
    if (!event_callback_released_) {
      event_callback_.Release();
      event_callback_released_ = true;
    }
  }

  Napi::ThreadSafeFunction event_callback_;
  bool event_callback_released_ = false;
  std::unique_ptr<ldc_toy::NativeSfu> sfu_;
};

Napi::Object Init(Napi::Env env, Napi::Object exports) {
  return NativeSfuWrap::Init(env, exports);
}

NODE_API_MODULE(ldc_sfu_node, Init)

}  // namespace
