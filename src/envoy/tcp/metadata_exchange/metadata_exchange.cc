/* Copyright 2019 Istio Authors. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "src/envoy/tcp/metadata_exchange/metadata_exchange.h"

#include <string>
#include <map>

#include "envoy/network/connection.h"
#include "source/common/buffer/buffer_impl.h"
#include "source/common/http/message_impl.h"

namespace Envoy {
namespace Tcp {
namespace MetadataExchange {

MetadataExchangeConfig::MetadataExchangeConfig(
    const std::string& protocol,
    const FilterDirection filter_direction)
    : protocol_(protocol),
      filter_direction_(filter_direction) {}

void MetadataExchangeFilter::onSuccess(const Http::AsyncClient::Request&, Http::ResponseMessagePtr&&) {
  // TODO do something here
  ENVOY_LOG(error, "onSuccess");
}
void MetadataExchangeFilter::onFailure(const Http::AsyncClient::Request&, Http::AsyncClient::FailureReason) {
  // TODO do something here
  ENVOY_LOG(error, "onFailure");
}

/* All msgs: Record conn used to send msg */
Network::FilterStatus MetadataExchangeFilter::onWrite(Buffer::Instance& data, bool) {
  // Check that can access conn in onWrite()
  ENVOY_LOG(error, "onWrite; data:");
  ENVOY_LOG(error, data.toString());
  ENVOY_LOG(error, "end data");
  ENVOY_LOG(error, "Will write to remote {}, named {}",
            write_callbacks_->connection().connectionInfoProvider().remoteAddress()->asString(),
            write_callbacks_->connection().connectionInfoProvider().requestedServerName());

  /* Other option is to getOrCreateRawAsyncClient() like sip */
  Http::RequestMessagePtr request(new Http::RequestMessageImpl());
  request->headers().setReferencePath("fakepath");
  request->headers().setPath("fakepath");
  std::string cluster_name = "outbound|9080||ratings.default.svc.cluster.local"; // TODO get the actual one.
  const auto thread_local_cluster = cm_.getThreadLocalCluster(cluster_name);
  Http::AsyncClient::Request* in_flight_request;
  if (thread_local_cluster != nullptr) {
    in_flight_request = thread_local_cluster->httpAsyncClient().send(
        std::move(request), *this,
        Http::AsyncClient::RequestOptions().setTimeout(std::chrono::milliseconds(5000)));
    ENVOY_LOG(error, "Sent request");
  } else {
    ENVOY_LOG(error, "Unknown cluster name: {}", cluster_name);
  }

  if (in_flight_request == nullptr) {
    ENVOY_LOG(error, "Request null");
  }
  // Use part of data as a key for test
  //conns_written.emplace(data.toString(), write_callbacks_->connection());
  // TODO make sure not to segfault if conn is closed now
  // Check that can write to one conn from another
      // If can't use conn, maybe socket?
  //Buffer::OwnedImpl fake_data("FAKE DATA");

  return Network::FilterStatus::Continue;
}

/* Trace messages only: Look up conn for key, get bits, send there */
Network::FilterStatus MetadataExchangeFilter::onData(Buffer::Instance& data,
                                                     bool) {
  ENVOY_LOG(error, "onData; data:");
  ENVOY_LOG(error, data.toString());
  ENVOY_LOG(error, "end data");
  // PERF Envoy may have a better way to filter by message type
  return Network::FilterStatus::Continue;
}

Network::FilterStatus MetadataExchangeFilter::onNewConnection() {
  ENVOY_LOG(error, "onNewConnection");
  return Network::FilterStatus::Continue;
}

}  // namespace MetadataExchange
}  // namespace Tcp
}  // namespace Envoy
