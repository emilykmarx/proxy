#include "src/envoy/http/alpn/alpn_filter.h"
#include "source/common/http/headers.h"
#include "source/common/http/message_impl.h"

namespace Envoy {
namespace Http {
namespace Alpn {

// TODO properly integrate this into proxy as a new filter.
// Put it here for now bc I didn't want to figure out how to do that.
// At least rename "alpn" to the extent possible.
static constexpr char StatPrefix[] = "alpn.";
/* Header value should be request ID to trace */
const Http::LowerCaseString WTFTraceHeader{"x-wtf-trace"};

AlpnFilterConfig::AlpnFilterConfig(
    const istio::envoy::config::filter::http::alpn::v2alpha1::FilterConfig
        &,
    absl::string_view local_ip,
    Upstream::ClusterManager &cluster_manager,
    Stats::Scope& local_scope, Stats::Scope& root_scope)
    : local_ip_(local_ip),
      cluster_manager_(cluster_manager),
      stats_(generateStats(StatPrefix, local_scope)),
      root_scope_(root_scope) {
    if (local_ip.empty()) {
      ENVOY_LOG(error, "Local IP empty when configuring filter");
    }
}

void AlpnFilter::onSuccess(const Http::AsyncClient::Request& request,
                           Http::ResponseMessagePtr&&) {
  ENVOY_LOG(error, "onSuccess");
  in_flight_requests_.remove(const_cast<Http::AsyncClient::Request *> (&request));
  if (in_flight_requests_.empty()) {
    decoder_callbacks_->resetStream();
  }
}
void AlpnFilter::onFailure(const Http::AsyncClient::Request& request,
                           Http::AsyncClient::FailureReason) {
  // TODO do something here
  ENVOY_LOG(error, "onFailure");
  in_flight_requests_.remove(const_cast<Http::AsyncClient::Request *> (&request));
  if (in_flight_requests_.empty()) {
    decoder_callbacks_->resetStream();
  }
}

bool AlpnFilter::sendHttpRequest(std::string orig_request_id, std::string host) {
    std::string cluster_name = host.substr(0, host.find(":"));
    std::string ip = host.substr(host.find(":") + 1);
    ENVOY_LOG(error, "sending trace to {}:{}", cluster_name, ip);
    // TODO this doesn't result in lb setting override
    // TODO once this works, will need to prevent infinite loop of traces
    // TODO: related -- for e.g. ratings, decodeHeaders for productPage is called twice, so sends trace twice.
    // Prevent that with state or something.
    decoder_callbacks_->setUpstreamOverrideHost(ip);
    /* Other option is to getOrCreateRawAsyncClient() like sip */
    // TODO rename method
    Http::RequestMessagePtr request(new Http::RequestMessageImpl());
    request->headers().setPath("/wtf-trace");
    request->headers().setMethod(Http::Headers::get().MethodValues.Get);
    request->headers().setHost("wtf-trace");
    request->headers().addCopy(WTFTraceHeader, orig_request_id);
    const auto thread_local_cluster = config_->cluster_manager_.getThreadLocalCluster(cluster_name);
    Http::AsyncClient::Request* in_flight_request;
    if (thread_local_cluster != nullptr) {
      in_flight_request = thread_local_cluster->httpAsyncClient().send(
          std::move(request), *this,
          Http::AsyncClient::RequestOptions().setTimeout(std::chrono::milliseconds(5000)));
    } else {
      ENVOY_LOG(error, "sendHttpRequest: Unknown cluster name: {}", cluster_name);
      return false;
    }

    if (in_flight_request == nullptr) {
      ENVOY_LOG(error, "sendHttpRequest: Request null");
      return false;
    }

  in_flight_requests_.push_back(in_flight_request);
  return true;
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
  Envoy::Http::HeaderMap::GetResult wtf_trace_hdr = headers.get(WTFTraceHeader);
  if (wtf_trace_hdr.empty()) {
    // Normal request => let it through
    return FilterHeadersStatus::Continue;
  }

  // Handle trace request.
  // NOTE: Any errors here should reset the stream and stop iteration.
  if (wtf_trace_hdr.size() > 1) {
    ENVOY_LOG(error, "Multiple WTF trace headers in trace request; dropping");
    decoder_callbacks_->resetStream();
    return FilterHeadersStatus::StopIteration;
  }

  Stats::StatNameManagedStorage stat_name(std::string(StatPrefix) + "msg_history",
                                          config_->root_scope_.symbolTable());
  absl::optional<std::reference_wrapper<const Stats::Map>> msg_history_wrapper =
    config_->root_scope_.findMap(stat_name.statName());
  std::string orig_request_id = std::string(wtf_trace_hdr[0]->value().getStringView());
  if (!msg_history_wrapper.has_value()) {
    // Very bad -- message history data structure doesn't even exist
    ENVOY_LOG(critical, "Global message history not found while handling trace for request ID {}",
              orig_request_id);
    decoder_callbacks_->resetStream();
    return FilterHeadersStatus::StopIteration;
  }

  // PERF could be better to expose a find() from Stats::Map than use value()
  auto msg_history = msg_history_wrapper->get().value();
  auto found_msg_history = msg_history.find(orig_request_id);
  if (found_msg_history == msg_history.end()) {
    // Original request may be too old to trace, or this pod didn't send any requests
    // as part of original request (TODO should differentiate those two --
    // handling response path may take care of it)
    ENVOY_LOG(error, "Message history missing for request ID: {}", orig_request_id);
    decoder_callbacks_->resetStream();
    return FilterHeadersStatus::StopIteration;
  }

  bool sent_a_request = false;
  for (std::string host : found_msg_history->second) {
    if (sendHttpRequest(found_msg_history->first, host)) {
      sent_a_request = true;
    }
  }

  if (!sent_a_request) {
    // No callback to reset stream, so must do it here
    decoder_callbacks_->resetStream();
  }

  return FilterHeadersStatus::StopIteration;
}

/**
 * Called when the stream is destroyed.
 * Stream is a request and corresponding response, where this filter either
 * sent or received the request.
 */
// TODO audit for null check before every ->
void AlpnFilter::log(const Http::RequestHeaderMap* req_hdrs, const Http::ResponseHeaderMap* resp_hdrs,
                     const Http::ResponseTrailerMap*, const StreamInfo::StreamInfo& stream_info) {
  ENVOY_LOG(error, "log()");
  // TODO test request w/o response -- should send a message indicating that

  std::stringstream req_out;
  if (req_hdrs) req_hdrs->dumpState(req_out);
  std::stringstream resp_out;
  if (resp_hdrs) resp_hdrs->dumpState(resp_out);
  ENVOY_LOG(error, "req_hdrs: {}, resp_hdrs: {}", req_out.str(), resp_out.str());

  // 1. Error-checking prelude
  if (req_hdrs == nullptr) {
    ENVOY_LOG(error, "Request headers missing in stream");
    return;
  }

  Envoy::Http::HeaderMap::GetResult wtf_trace_hdr = req_hdrs->get(WTFTraceHeader);
  if (!wtf_trace_hdr.empty()) {
    // Stream was a trace request => no need to record anything
    return;
  }

  absl::string_view x_request_id = req_hdrs->getRequestIdValue();
  if (x_request_id.empty()) {
    ENVOY_LOG(error, "x-request-id missing in stream");
    return;
  }

  if (!stream_info.upstreamInfo().has_value()) {
    ENVOY_LOG(error, "Upstream info missing in stream with x-request-id {}", x_request_id);
    return;
  }

  Upstream::HostDescriptionConstSharedPtr upstream_host_ptr =
    stream_info.upstreamInfo().value().get().upstreamHost();
  if (upstream_host_ptr == nullptr) {
    ENVOY_LOG(error, "Upstream host missing in stream with x-request-id {}", x_request_id);
    return;
  }
  Network::Address::InstanceConstSharedPtr address_ptr = upstream_host_ptr->address();
  if (address_ptr == nullptr) {
    ENVOY_LOG(error, "Upstream host address missing in stream with x-request-id {}", x_request_id);
    return;
  }

  if (address_ptr->ip()->addressAsString() == config_->local_ip_) {
    // This filter was the upstream i.e. received the request => don't record
    return;
  }
  std::string upstream_host_cluster = upstream_host_ptr->cluster().name();
  std::string upstream_host_ip = address_ptr->asString();
  if (!upstream_host_cluster.length() || !upstream_host_ip.length()) {
    ENVOY_LOG(error, "Upstream host cluster or IP empty in stream with x-request-id {}", x_request_id);
    return;
  }
  // 2. Record upstream host to which we sent the request
  // TODO periodically delete old stats
  // EASYTODO make map value a pair rather than cluster_name:IP (incl port)
  // absl::string_view does not like to concatenate
  std::string upstream_host = upstream_host_cluster + ":" + upstream_host_ip;
  config_->stats_.msg_history_.insert(x_request_id, upstream_host);
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
  for (const auto& in_flight_request : in_flight_requests_) {
    ENVOY_LOG(error, "Destroying filter with request still in flight");
    in_flight_request->cancel();
  }
}

}  // namespace Alpn
}  // namespace Http
}  // namespace Envoy
