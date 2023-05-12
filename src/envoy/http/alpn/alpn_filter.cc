#include <unordered_map>

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
// - audit for null check before every ->, and no value() (throws if no value)
*/
static constexpr char StatPrefix[] = "alpn.";
/** Prefix of request ID identifying a trace.
 *  Rest is original request ID.
 *  (Also used as a hack to tag wtf messages logged at error level that are really info) */
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
  ENVOY_LOG(trace, "onSuccess");
  in_flight_requests_.remove(const_cast<Http::AsyncClient::Request *> (&request));
  if (in_flight_requests_.empty()) {
    decoder_callbacks_->resetStream();
  }
}
void AlpnFilter::onFailure(const Http::AsyncClient::Request& request,
                           Http::AsyncClient::FailureReason) {
  ENVOY_LOG(error, "HTTP request failed to send");
  in_flight_requests_.remove(const_cast<Http::AsyncClient::Request *> (&request));
  if (in_flight_requests_.empty()) {
    decoder_callbacks_->resetStream();
  }
}

/** Send a trace to the endpoint in request_sent,
 *  with the headers in request_sent.
 *  If `orig_request_id` is passed, set request ID accordingly. */
bool AlpnFilter::sendHttpRequest(const Stats::MsgHistory::RequestSent& request_sent, absl::string_view orig_request_id) {
    std::string host = request_sent.endpoint;
    std::string cluster_name = host.substr(0, host.find(":"));
    std::string endpoint = host.substr(host.find(":") + 1);
    ENVOY_LOG(trace, "sending trace to {}:{}", cluster_name, endpoint);
    // TODO rename method
    // Message constructor move()s the headers, but we want to keep them in the map
    Http::RequestMessagePtr request(new Http::RequestMessageImpl(
        Http::createHeaderMap<Http::RequestHeaderMapImpl>(*request_sent.headers)));

    if (!orig_request_id.empty()) {
      request->headers().setRequestId(TraceRequestIdPrefix + "-" + std::string(orig_request_id));
    }
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
 * end_stream is true if request (headers + any data) has been fully sent or received.
 * When request is sent via httpAsyncClient(), called in receiver but not sender.
 */
FilterHeadersStatus AlpnFilter::decodeHeaders(RequestHeaderMap &headers,
                                              bool end_stream) {
  std::stringstream out_headers;
  headers.dumpState(out_headers);
  ENVOY_LOG(trace, "");
  ENVOY_LOG(trace, "decodeHeaders; headers: {}, end_stream {}, whoami {}", out_headers.str(), end_stream, whoami);

  absl::string_view x_request_id = headers.getRequestIdValue();
  if (x_request_id.empty()) {
    // This is really a warn, but I want it to show up and don't want to deal with istio's log level system
    ENVOY_LOG(error, "Non-trace request missing request ID in decodeHeaders(); won't be able to record history");
    return FilterHeadersStatus::Continue;
  }
  absl::string_view orig_request_id = stripTracePrefix(x_request_id);
  std::string sender_ip = decoder_callbacks_->streamInfo().downstreamAddressProvider().remoteAddress()->ip()->addressAsString();

  // Drop 10% of incoming details GET requests (don't drop traces or outgoing requests)
  // This is for fault injection testing only!
  std::hash<std::string> hasher;
  size_t rand = hasher(std::string(x_request_id)) % 100; // get a random number that changes for each filter
  if (orig_request_id.empty() && sender_ip != config_->local_ip_ && headers.getPathValue().find("details") != absl::string_view::npos && rand < 10) {
    ENVOY_LOG(error, "Reset stream for fault injection test: {}", out_headers.str());
    decoder_callbacks_->resetStream();
    return FilterHeadersStatus::StopIteration;
  }


  if (orig_request_id.empty()) {
    ENVOY_LOG(trace, "normal request");
    // Normal request => let it through
    return FilterHeadersStatus::Continue;
  }

  /** Handle trace request. May be: 1) From locally initiated trace 2) From another pod
   *  3) Coming down from app bc no history
   *  NOTE: Keep in mind threads may be running decodeHeaders for same request ID concurrently,
   *  Stats returns a copy before releasing the lock, so ok if another thread deletes while
   *  we're handling the trace. */

  absl::string_view sender_endpoint = decoder_callbacks_->streamInfo().downstreamAddressProvider().remoteAddress()->asStringView();

  // Haven't found a way to get downstream cluster name here (not virtualClusterName, not upstreamClusterInfo (from either decoder or encoder cb),
  // not downstreamAddressProvider)
  ENVOY_LOG(error, "[{}] {}\nReceived WTF trace\nDownstream endpoint: {}",
            TraceRequestIdPrefix, orig_request_id, sender_endpoint);
  std::any msg_history_ret =
      config_->stats_.msg_history_.getMsgHistory(orig_request_id);

  // EASYTODO rename getMsgHistory to handleTraceRequest
  // EASYTODO rename MsgHistory, msg_history, msg_history_ to "request_id_history"
  if (!msg_history_ret.has_value()) {
    /** Request history was lost, or never existed =>
     *  If outgoing (from app): Let it through, and we'll spray it across the rest of the destination service cluster
     *  upon receiving response (would spray now, but don't know dest cluster yet).
     *  If incoming: Send to app; app will generate any follow-on requests, which another filter will spray on outgoing path. */
    if (sender_ip == config_->local_ip_) {
      // Outgoing (from app)
      ENVOY_LOG(trace, "Setting should_spray_");
      trace_to_spray.emplace(Stats::MsgHistory::RequestSent({}, &headers));
    } else {
      // TODO when send bits back to trace originator: Also indicate that we did this
      // This is really a warn, but I want it to show up and don't want to deal with istio's log level system
      ENVOY_LOG(error, "[{}] {}\nWTF trace has no history. Trace will be let through to app, "
                        "and any follow-on traces will be sprayed to their dest cluster",
                TraceRequestIdPrefix, orig_request_id);
    }

    return FilterHeadersStatus::Continue;
  }

  /** Send all recorded requests (to their original nbrs, with original hdrs)
   *  simultaneously and mark handled, if haven't yet (note each original request ID can only be traced once
   *  over the lifetime of a proxy -- likely easy to change by adding a "trace ID"). */
  Stats::MsgHistory msg_history = std::any_cast<Stats::MsgHistory>(msg_history_ret);
  bool sent_a_request = false;
  if (!msg_history.handled) {
    for (const Stats::MsgHistory::RequestSent& request_sent : msg_history.requests_sent) {
      // This IP is where we try to send it; load balancer will pick a different host if that one's unhealthy
      std::string cluster_name = request_sent.endpoint.substr(0, request_sent.endpoint.find(":"));
      std::string cluster_endpoint = request_sent.endpoint.substr(request_sent.endpoint.find(":") + 1);
      ENVOY_LOG(error, "[{}] {}\nSending WTF trace\nUpstream cluster: {}\nUpstream endpoint: {}",
                TraceRequestIdPrefix, orig_request_id, cluster_name, cluster_endpoint);
      if (sendHttpRequest(request_sent, orig_request_id)) {
        sent_a_request = true;
      }
    }
  } else {
    /** Recvd a trace for an ID we've already fully handled by sending all recorded messages */
    ENVOY_LOG(trace, "Trace was already fully handled");
  }

  if (!sent_a_request) {
    // No callback to reset stream, so must do it here to prevent send to app without hang on StopIteration
    decoder_callbacks_->resetStream();
  }

  // Wait for requests to be sent before destroying filter
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
  ENVOY_LOG(trace, "log()");
  // TODO handle request w/o response better? (Has no upstream info)

  std::stringstream req_out;
  if (req_hdrs) req_hdrs->dumpState(req_out);
  std::stringstream resp_out;
  if (resp_hdrs) resp_hdrs->dumpState(resp_out);
  ENVOY_LOG(trace, "req_hdrs: {}, resp_hdrs: {}", req_out.str(), resp_out.str());

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
    ENVOY_LOG(trace, "log: skipping recording trace");
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
 * end_stream is true if request (headers + any data) has been fully sent or received.
 */
FilterDataStatus AlpnFilter::decodeData(Buffer::Instance& data, bool end_stream) {
  ENVOY_LOG(trace, "decodeData; data: {}, end_stream {}", data.toString().substr(0,24), end_stream);
  if (!in_flight_requests_.empty()) {
    // Continuing here would restart a filter chain stopped upon sending a trace
    return FilterDataStatus::StopIterationNoBuffer;
  }
  return FilterDataStatus::Continue;
}

/**
 * Called when response headers are sent or received.
 * end_stream is true if response (headers + any data) has been fully sent or received.
 */
FilterHeadersStatus AlpnFilter::encodeHeaders(ResponseHeaderMap& headers, bool end_stream) {
  std::stringstream out;
  headers.dumpState(out);
  ENVOY_LOG(trace, "encodeHeaders; headers: {}, end_stream {}, whoami {}", out.str(), end_stream, whoami);
  ENVOY_LOG(trace, "should_spray: {}", trace_to_spray.has_value());
  if (!trace_to_spray.has_value()) return FilterHeadersStatus::Continue;

  absl::string_view responding_endpoint;
  if (auto upstream_info = encoder_callbacks_->streamInfo().upstreamInfo(); upstream_info) {
    Upstream::HostDescriptionConstSharedPtr responding_endpoint_ptr = upstream_info->upstreamHost();
    if (responding_endpoint_ptr && responding_endpoint_ptr->address()) {
      responding_endpoint = responding_endpoint_ptr->address()->asStringView();
      ENVOY_LOG(trace, "host: {}", responding_endpoint);
    } else {
      ENVOY_LOG(error, "Could not spray to cluster due to missing upstream host info");
      return FilterHeadersStatus::Continue;
    }
  } else {
    ENVOY_LOG(trace, "Could not spray to cluster due to missing upstream info");
    return FilterHeadersStatus::Continue;
  }

  // Spray trace to all hosts in cluster (besides the one we already sent to).
  auto upstream_cluster_info = encoder_callbacks_->streamInfo().upstreamClusterInfo();
  bool sent_a_request = false;
  if (upstream_cluster_info.has_value() && upstream_cluster_info.value()) {
    absl::string_view cluster_name = upstream_cluster_info.value()->name();
    ENVOY_LOG(trace, "upstream cluster: {}", cluster_name);
    const auto thread_local_cluster = config_->cluster_manager_.getThreadLocalCluster(cluster_name);
    Envoy::Upstream::HostMapConstSharedPtr cluster_endpoints = thread_local_cluster->prioritySet().crossPriorityHostMap();
    if (!cluster_endpoints) {
      ENVOY_LOG(error, "Could not spray to cluster (host {}) due to missing host map",
                responding_endpoint);
      return FilterHeadersStatus::Continue;
    }
    for (auto it = cluster_endpoints->begin(); it != cluster_endpoints->end(); it++) {
      absl::string_view cluster_endpoint = it->first;
      absl::string_view orig_request_id = stripTracePrefix(trace_to_spray.value().headers->getRequestIdValue());
      ENVOY_LOG(error, "[{}] {}\nSending WTF trace\nUpstream cluster: {}\nUpstream endpoint: {}",
                TraceRequestIdPrefix, orig_request_id, cluster_name, cluster_endpoint);
      if (cluster_endpoint != responding_endpoint) {
        ENVOY_LOG(trace, "Spraying to {}", cluster_endpoint);
        std::string cluster_host = std::string(cluster_name) + ":" + std::string(cluster_endpoint);
        trace_to_spray.value().endpoint = cluster_host;
        if (sendHttpRequest(trace_to_spray.value())) {
          sent_a_request = true;
        }
      }
    }
  } else {
    ENVOY_LOG(error, "Could not spray to cluster (host {}) due to missing cluster info",
              responding_endpoint);
    return FilterHeadersStatus::Continue;
  }

  if (sent_a_request) {
    // Wait for requests to be sent before destroying filter
    return FilterHeadersStatus::StopIteration;
  } else {
    return FilterHeadersStatus::Continue;
  }
}

/**
 * Called when response data is sent or received.
 * end_stream is true if response (headers + any data) has been fully sent or received.
 */
FilterDataStatus AlpnFilter::encodeData(Buffer::Instance& data, bool end_stream) {
  ENVOY_LOG(trace, "encodeData; data: {}, end_stream {}", data.toString().substr(0,24), end_stream);
  if (!in_flight_requests_.empty()) {
    // Continuing here would restart a filter chain stopped upon sending a trace
    return FilterDataStatus::StopIterationNoBuffer;
  }
  return FilterDataStatus::Continue;
}

void AlpnFilter::onDestroy() {
  ENVOY_LOG(trace, "onDestroy");
  for (const auto& in_flight_request : in_flight_requests_) {
    ENVOY_LOG(error, "Destroying filter with request still in flight");
    in_flight_request->cancel();
  }
}

}  // namespace Alpn
}  // namespace Http
}  // namespace Envoy
