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
    absl::string_view local_ip,
    Stats::Scope& local_scope, Stats::Scope& root_scope)
    : local_ip_(local_ip),
      stats_(generateStats(StatPrefix, local_scope)),
      root_scope_(root_scope) {
}

/**
 * Called when request headers are sent or received.
 * end_stream is true if request has been fully sent or received.
 */
FilterHeadersStatus AlpnFilter::decodeHeaders(RequestHeaderMap &headers,
                                              bool end_stream) {
  std::stringstream out_headers;
  headers.dumpState(out_headers);
  ENVOY_LOG(error, "decodeHeaders; headers: {}, end_stream {}", out_headers.str(), end_stream);

  // Forces headers to the cluster containing .11 (usu reviews)
  // to go to .11; everything else is routed as usual
  // Will also need to do this in decodeData
  // TODO if overriden host doesn't exist anymore, send a message indicating that
  decoder_callbacks_->setUpstreamOverrideHost("172.17.0.11:9080");

  return FilterHeadersStatus::Continue;
}

/**
 * Called when the stream is destroyed.
 * Stream is a request and corresponding response, where this filter either
 * sent or received the request.
 */
void AlpnFilter::log(const Http::RequestHeaderMap* req_hdrs, const Http::ResponseHeaderMap*,
                     const Http::ResponseTrailerMap*, const StreamInfo::StreamInfo& stream_info) {

  // TODO test request w/o response -- should send a message indicating that
  ENVOY_LOG(error, "log()"); // remove

  // 1. Error-checking prelude
  if (req_hdrs == nullptr) {
    ENVOY_LOG(error, "Request headers missing from stream");
    return;
  }

  absl::string_view x_request_id = req_hdrs->getRequestIdValue();
  if (x_request_id.empty()) {
    ENVOY_LOG(error, "x-request-id missing from stream");
    return;
  }

  if (!stream_info.upstreamInfo().has_value()) {
    ENVOY_LOG(error, "Upstream info missing from stream with x-request-id {}", x_request_id);
    return;
  }

  Upstream::HostDescriptionConstSharedPtr upstream_host_ptr =
    stream_info.upstreamInfo().value().get().upstreamHost();
  if (upstream_host_ptr == nullptr) {
    ENVOY_LOG(error, "Upstream host missing from stream with x-request-id {}", x_request_id);
    return;
  }
  Network::Address::InstanceConstSharedPtr address_ptr = upstream_host_ptr->address();
  if (address_ptr == nullptr) {
    ENVOY_LOG(error, "Upstream host address missing from stream with x-request-id {}", x_request_id);
    return;
  }

  if (address_ptr->ip()->addressAsString() == config_->local_ip_) {
    // This filter was the upstream i.e. received the request => don't record
    return;
  }

  // 2. Record upstream host to which we sent the request
  // TODO periodically delete old stats
  config_->stats_.msg_history_.insert(x_request_id, address_ptr->asStringView());
  // Note x_request_id has moved now

  // remove (goes in trace)
  Stats::StatNameManagedStorage stat_name(std::string(StatPrefix) + "msg_history",
                                          config_->root_scope_.symbolTable());
  absl::optional<std::reference_wrapper<const Stats::Map>> msg_history =
    config_->root_scope_.findMap(stat_name.statName());
  if (!msg_history.has_value()) {
    ENVOY_LOG(critical, "Message history not found; stream x-request-id {}", x_request_id);
  } else {
    ENVOY_LOG(error, "msg_history val: {}", msg_history->get().value()); // remove
  }
}

/**
 * Called when request data is sent or received.
 * end_stream is true if request has been fully sent or received.
 */
FilterDataStatus AlpnFilter::decodeData(Buffer::Instance& data, bool end_stream) {
  ENVOY_LOG(error, "decodeData; data: {}, end_stream {}", data.toString().substr(0,24), end_stream);
  return FilterDataStatus::Continue;
}

/**
 * Called when response headers are sent or received.
 * end_stream is true if response has been fully sent or received.
 */
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

/**
 * Called when response data is sent or received.
 * end_stream is true if response has been fully sent or received.
 */
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
