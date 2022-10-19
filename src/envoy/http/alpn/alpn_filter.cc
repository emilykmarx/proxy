#include "src/envoy/http/alpn/alpn_filter.h"

namespace Envoy {
namespace Http {
namespace Alpn {

// TODO properly integrate this into proxy as a new filter.
// Put it here for now bc I didn't want to figure out how to do that.
// At least rename "alpn" to the extent possible.
static constexpr char StatPrefix[] = "alpn.";

AlpnFilterConfig::AlpnFilterConfig(
    const istio::envoy::config::filter::http::alpn::v2alpha1::FilterConfig
        &,
    Upstream::ClusterManager &cluster_manager,
    Stats::Scope& local_scope, Stats::Scope& root_scope)
    : cluster_manager_(cluster_manager),
      stats_(generateStats(StatPrefix, local_scope)),
      root_scope_(root_scope) {
}

FilterHeadersStatus AlpnFilter::decodeHeaders(RequestHeaderMap &headers,
                                                    bool end_stream) {
  std::stringstream out_headers;
  headers.dumpState(out_headers);
  ENVOY_LOG(error, "decodeHeaders; headers: {}, end_stream {}", out_headers.str(), end_stream);
  /* Note: Can't get the upstream host here, which makes sense since the lb hasn't run yet.
   * decoder_callbacks_->streamInfo().upstreamInfo() is null here, and
   * *_callbacks_->connection().remoteAddress()->asStringView() gives downstream conn */
  /*
  Notes on how to set the upstream host:
  Upstream::HostDescriptionConstSharedPtr: given by upstreamHost()
  Upstream::HostDescription& host;
  absl::string_view host_address = host.address()->asStringView();
  Should look like 172.17.0.6:9080
  */

  // Forces headers to the cluster containing .11 (usu reviews)
  // to go to .11; everything else is routed as usual
  // Will also need to do this in decodeData
  // TODO if overriden host doesn't exist anymore, send a message indicating that
  decoder_callbacks_->setUpstreamOverrideHost("172.17.0.11:9080");

  return FilterHeadersStatus::Continue;
}

/*
 * Called when the stream is destroyed.
 */
void AlpnFilter::log(const Http::RequestHeaderMap*, const Http::ResponseHeaderMap*,
                     const Http::ResponseTrailerMap*, const StreamInfo::StreamInfo& stream_info) {
  ENVOY_LOG(error, "log()");
  if (stream_info.upstreamInfo().has_value()) {
    Upstream::HostDescriptionConstSharedPtr host = stream_info.upstreamInfo()->upstreamHost();
    if (host != nullptr) {
      ENVOY_LOG(error, "host: {}", host->address()->asString());
    } else {
      ENVOY_LOG(error, "host was null");
    }
  } else {
    ENVOY_LOG(error, "upstreamInfo was missing");
  }

  // TODO periodically delete old stats
  config_->stats_.msg_history_.insert(std::rand(), std::rand());
  Stats::StatNameManagedStorage stat_name(std::string(StatPrefix) + "msg_history",
                                          config_->root_scope_.symbolTable());
  absl::optional<std::reference_wrapper<const Stats::Map>> msg_history =
    config_->root_scope_.findMap(stat_name.statName());
  if (!msg_history.has_value()) {
    ENVOY_LOG(error, "msg_history has no value");
  } else {
    ENVOY_LOG(error, "msg_history val: {}", msg_history->get().value());
  }
}

FilterDataStatus AlpnFilter::decodeData(Buffer::Instance& data, bool end_stream) {
  ENVOY_LOG(error, "decodeData; data: {}, end_stream {}", data.toString().substr(0,24), end_stream);
  return FilterDataStatus::Continue;
}

FilterHeadersStatus AlpnFilter::encodeHeaders(ResponseHeaderMap& headers, bool end_stream) {
  std::stringstream out;
  headers.dumpState(out);
  ENVOY_LOG(error, "encodeHeaders; headers: {}, end_stream {}", out.str(), end_stream);
  if (auto upstream_info = encoder_callbacks_->streamInfo().upstreamInfo();
    upstream_info != nullptr) {
    Upstream::HostDescriptionConstSharedPtr host = upstream_info->upstreamHost();
    if (host != nullptr) {
      ENVOY_LOG(error, "host: {}", host->address()->asString());
    }
  }
return FilterHeadersStatus::Continue;
}

FilterDataStatus AlpnFilter::encodeData(Buffer::Instance& data, bool end_stream) {
  ENVOY_LOG(error, "encodeData; data: {}, end_stream {}", data.toString().substr(0,24), end_stream);
  return FilterDataStatus::Continue;
}

void AlpnFilter::onDestroy() {
  ENVOY_LOG(error, "onDestroy");
}

}  // namespace Alpn
}  // namespace Http
}  // namespace Envoy
