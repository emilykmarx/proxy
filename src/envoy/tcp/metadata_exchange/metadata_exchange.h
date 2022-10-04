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

#pragma once

#include <string>

#include "envoy/network/filter.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/stream_info/filter_state.h"
#include "extensions/common/context.h"
#include "extensions/common/node_info_bfbs_generated.h"
#include "extensions/common/proto_util.h"
#include "source/common/common/stl_helpers.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/common/expr/cel_state.h"
#include "src/envoy/tcp/metadata_exchange/config/metadata_exchange.pb.h"

namespace Envoy {
namespace Tcp {
// TODO properly integrate this into proxy as a new filter.
// Put it here for now bc I didn't want to figure out how to do that.
// At least rename "metadata exchange" to the extent possible.
// Also remove unused deps & code (maybe incl upstream filter).
namespace MetadataExchange {

/**
 * Direction of the flow of traffic in which this this MetadataExchange filter
 * is placed.
 */
enum FilterDirection { Downstream, Upstream };

/**
 * Configuration for the MetadataExchange filter.
 */
class MetadataExchangeConfig {
 public:
  MetadataExchangeConfig(const std::string& protocol,
                         const FilterDirection filter_direction);
  // Left this here as an example of how to do config
  const std::string protocol_;
  // Direction of filter.
  const FilterDirection filter_direction_;

 private:
};

using MetadataExchangeConfigSharedPtr = std::shared_ptr<MetadataExchangeConfig>;

/**
 * A MetadataExchange filter instance. One per connection.
 */
class MetadataExchangeFilter : public Network::Filter,
                               protected Logger::Loggable<Logger::Id::filter> {
 public:
  MetadataExchangeFilter(MetadataExchangeConfigSharedPtr config)
      : config_(config) {}

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data,
                               bool end_stream) override;
  Network::FilterStatus onNewConnection() override;
  Network::FilterStatus onWrite(Buffer::Instance& data,
                                bool end_stream) override;
  void initializeReadFilterCallbacks(
      Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
    // read_callbacks_->connection().addConnectionCallbacks(*this);
  }
  void initializeWriteFilterCallbacks(
      Network::WriteFilterCallbacks& callbacks) override {
    write_callbacks_ = &callbacks;
  }

 private:
  // Config for MetadataExchange filter.
  MetadataExchangeConfigSharedPtr config_;
  // Read callback instance.
  Network::ReadFilterCallbacks* read_callbacks_{};
  // Write callback instance.
  Network::WriteFilterCallbacks* write_callbacks_{};
};

}  // namespace MetadataExchange
}  // namespace Tcp
}  // namespace Envoy
