#include "src/envoy/http/alpn/alpn_filter.h"
#include "source/common/http/headers.h"
#include "source/common/http/message_impl.h"

namespace Envoy {
namespace Http {
namespace Alpn {

/**
 * Cleanup TODOs:
 * - remove/downgrade debug logging
// - properly integrate this into proxy as a new filter.
// Put it here for now bc I didn't want to figure out how to do that.
// At least rename "alpn" to the extent possible.
// - audit for null check before every ->
*/
static constexpr char StatPrefix[] = "alpn.";
/** Prefix of request ID identifying a trace.
 *  Rest is original request ID. */
const std::string TraceRequestIdPrefix = "WTFTRACE";

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

bool AlpnFilter::sendHttpRequest(absl::string_view orig_request_id, const Stats::Map::MsgHistory::RequestSent& request_sent) {
    std::string host = request_sent.endpoint;
    std::string cluster_name = host.substr(0, host.find(":"));
    std::string endpoint = host.substr(host.find(":") + 1);
    ENVOY_LOG(error, "sending trace to {}:{}", cluster_name, endpoint);
    // TODO rename method
    // Message constructor move()s the headers, but we want to keep them in the map
    Http::RequestMessagePtr request(new Http::RequestMessageImpl(
        Http::createHeaderMap<Http::RequestHeaderMapImpl>(*request_sent.headers)));
    request->headers().setRequestId(TraceRequestIdPrefix + "-" + std::string(orig_request_id));
    const auto thread_local_cluster = config_->cluster_manager_.getThreadLocalCluster(cluster_name);
    Http::AsyncClient::Request* in_flight_request;
    if (thread_local_cluster != nullptr) {
      in_flight_request = thread_local_cluster->httpAsyncClient().send(
          std::move(request), *this,
          Http::AsyncClient::RequestOptions().setTimeout(std::chrono::milliseconds(5000)),
          endpoint);
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

// request_id should be non-empty. Return empty if not a trace.
absl::string_view stripTracePrefix(absl::string_view request_id) {

  std::vector<absl::string_view> request_id_tokens = absl::StrSplit(request_id,
                                                                    absl::MaxSplits('-', 1));
  if (request_id_tokens.front() == TraceRequestIdPrefix) {
    return request_id_tokens.back();
  }
  return {}; // not a trace
}

/**
 * Called when request headers are sent or received.
 * end_stream is true if request has been fully sent or received.
 * When request is sent via httpAsyncClient(), called in receiver but not sender.
 */
FilterHeadersStatus AlpnFilter::decodeHeaders(RequestHeaderMap &headers,
                                              bool end_stream) {
  std::stringstream out_headers;
  headers.dumpState(out_headers);
  ENVOY_LOG(error, "");
  ENVOY_LOG(error, "decodeHeaders; headers: {}, end_stream {}", out_headers.str(), end_stream);

  absl::string_view x_request_id = headers.getRequestIdValue();
  if (x_request_id.empty()) {
    ENVOY_LOG(error, "Request missing request ID in decodeHeaders()");
    return FilterHeadersStatus::Continue;
  }
  absl::string_view orig_request_id = stripTracePrefix(x_request_id);
  if (orig_request_id.empty()) {
    ENVOY_LOG(error, "normal request");
    // Normal request => let it through
    return FilterHeadersStatus::Continue;
  }

  /** Handle trace request. May be: 1) From local curl 2) From another pod 3) Coming down from app bc no history
   *  NOTE: Any errors here should reset the stream and stop iteration.
   *  NOTE: Keep in mind threads may be running decodeHeaders for same request ID concurrently,
   *  sharing the msg history stat. Since we release the lock to handle the trace,
   *  it's possible two threads will handle the same trace, or one will handle it while a normal
   *  request is being handled on another. But the worst that should happen as a result is
   *  sending a dup trace, or handling a trace with incomplete request history
   *  (seems hard to guarantee avoiding that anyway). */

  ENVOY_LOG(error, "recvd trace; orig req ID {}", orig_request_id);
  const Stats::Map::MsgHistory* msg_history =
      config_->stats_.msg_history_.getMsgHistory(orig_request_id);

  // EASYTODO rename MsgHistory, msg_history, msg_history_ to "request_id_history"
  if (!msg_history) {
    /** Request history was lost, or never existed =>
     *  send to app if incoming, or to destination service if outgoing */
    ENVOY_LOG(error, "Received trace without request history; sending upstream");
    return FilterHeadersStatus::Continue;
  }

  /** Send all recorded requests (to their original nbrs, with original hdrs)
   *  simultaneously and mark handled, if haven't yet (note each original request ID can only be traced once
   *  over the lifetime of a proxy -- likely easy to change by adding a "trace ID"). */
  bool sent_a_request = false;
  if (!msg_history->handled) {
    for (const Stats::Map::MsgHistory::RequestSent& request_sent : msg_history->requests_sent) {
      if (sendHttpRequest(orig_request_id, request_sent)) {
        sent_a_request = true;
      }
    }

    if (!config_->stats_.msg_history_.setHandled(orig_request_id)) {
      ENVOY_LOG(error, "Failed to record trace as handled for original request ID {}",
                orig_request_id);
    }
  } else {
    /** Recvd a trace for an ID we've already fully handled by sending all recorded messages */
    ENVOY_LOG(error, "Trace was already fully handled");
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
 * When request is sent via httpAsyncClient(), called in receiver but not sender.
 */
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

  absl::string_view x_request_id = req_hdrs->getRequestIdValue();
  if (x_request_id.empty()) {
    ENVOY_LOG(error, "x-request-id missing in stream");
    return;
  }

  if (!stripTracePrefix(x_request_id).empty()) {
    // Stream was a trace request => no need to record anything
    ENVOY_LOG(error, "log: skipping recording trace");
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

  if (address_ptr->ip()->addressAsString() != config_->local_ip_) {
    std::string upstream_host_cluster = upstream_host_ptr->cluster().name();
    std::string upstream_host_ip = address_ptr->asString();
    if (!upstream_host_cluster.length() || !upstream_host_ip.length()) {
      ENVOY_LOG(error, "Upstream host cluster or IP empty in stream with x-request-id {}", x_request_id);
      return;
    }

    std::string upstream_host = upstream_host_cluster + ":" + upstream_host_ip;
    // 2. Record upstream host to which we sent the request
    // TODO periodically delete old stats
    // EASYTODO make map value a pair rather than cluster_name:IP (incl port)
    // absl::string_view does not like to concatenate
    config_->stats_.msg_history_.insert_request_sent(x_request_id, upstream_host, req_hdrs);
  } else {
    /** This filter was the upstream i.e. received the request => record w/o upstream.
     *  This way we still have history even for requests that didn't result in any other requests
     *  (so we don't unnecessarily bother the app) */
    config_->stats_.msg_history_.insert_request_recvd(x_request_id);
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
  for (const auto& in_flight_request : in_flight_requests_) {
    ENVOY_LOG(error, "Destroying filter with request still in flight");
    in_flight_request->cancel();
  }
}

}  // namespace Alpn
}  // namespace Http
}  // namespace Envoy
