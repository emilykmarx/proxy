#pragma once

#include "envoy/config/filter/http/alpn/v2alpha1/config.pb.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/http/filter.h"
#include "envoy/access_log/access_log.h"
#include "extensions/common/context.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
namespace Envoy {
namespace Http {
namespace Alpn {

/**
 * All alpn filter stats. @see stats_macros.h
 */
#define ALL_ALPN_FILTER_STATS(MAP) \
  MAP(msg_history)

/**
 * Struct definition for all alpn filter stats. @see stats_macros.h
 */
struct AlpnFilterStats {
  ALL_ALPN_FILTER_STATS(GENERATE_MAP_STRUCT)
};

class AlpnFilterConfig : Logger::Loggable<Logger::Id::filter> {
 public:
  AlpnFilterConfig(
      const istio::envoy::config::filter::http::alpn::v2alpha1::FilterConfig
          &proto_config,
      absl::string_view local_ip,
      Upstream::ClusterManager &cluster_manager,
      Stats::Scope& local_scope, Stats::Scope& root_scope);

  absl::string_view local_ip_;
  Upstream::ClusterManager &cluster_manager_;
  AlpnFilterStats stats_;
  Stats::Scope& root_scope_;

 private:
  AlpnFilterStats generateStats(const std::string& prefix,
                                Stats::Scope& local_scope) {
    return AlpnFilterStats{ALL_ALPN_FILTER_STATS(POOL_MAP_PREFIX(local_scope, prefix))};
  }
};

using AlpnFilterConfigSharedPtr = std::shared_ptr<AlpnFilterConfig>;

class AlpnFilter : public StreamFilter,
                   Logger::Loggable<Logger::Id::filter>,
                   public AccessLog::Instance,
                  public Http::AsyncClient::Callbacks {
 public:
  explicit AlpnFilter(const AlpnFilterConfigSharedPtr &config)
      : config_(config) {}

  FilterHeadersStatus decodeHeaders(RequestHeaderMap &headers,
                                    bool end_stream) override;

  FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;

  FilterHeadersStatus encodeHeaders(ResponseHeaderMap& headers, bool end_stream) override;

  FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;

  void onDestroy() override;

  // TODO what to do with these?
  FilterHeadersStatus encode1xxHeaders(ResponseHeaderMap&) override {return FilterHeadersStatus::Continue;};
  FilterTrailersStatus encodeTrailers(ResponseTrailerMap&) override {return FilterTrailersStatus::Continue;};
  FilterMetadataStatus encodeMetadata(MetadataMap&) override {return FilterMetadataStatus::Continue;};
  void setEncoderFilterCallbacks(StreamEncoderFilterCallbacks& callbacks) override {
    encoder_callbacks_ = &callbacks;
  };
  FilterTrailersStatus decodeTrailers(RequestTrailerMap&) override {return FilterTrailersStatus::Continue;};
  void setDecoderFilterCallbacks(StreamDecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
  };

  // AccessLog::Instance
  void log(const RequestHeaderMap* request_headers,
           const ResponseHeaderMap* response_headers,
           const ResponseTrailerMap* response_trailers,
           const StreamInfo::StreamInfo& stream_info) override;

 private:
  const AlpnFilterConfigSharedPtr config_;
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{};

  std::list<Http::AsyncClient::Request*> in_flight_requests_{};

  // Http::AsyncClient::Callbacks
  void onSuccess(const Http::AsyncClient::Request&, Http::ResponseMessagePtr&& response) override;
  void onFailure(const Http::AsyncClient::Request&,
                 Http::AsyncClient::FailureReason reason) override;
  void onBeforeFinalizeUpstreamSpan(Envoy::Tracing::Span&,
                                    const Http::ResponseHeaderMap*) override {};

  bool sendHttpRequest(absl::string_view orig_request_id, const Stats::Map::MsgHistory::RequestSent& request_sent);
};

}  // namespace Alpn
}  // namespace Http
}  // namespace Envoy
