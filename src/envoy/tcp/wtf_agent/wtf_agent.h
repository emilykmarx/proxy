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

#include "envoy/local_info/local_info.h"
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
#include "src/envoy/tcp/wtf_agent/config/wtf_agent.pb.h"

namespace Envoy {
namespace Tcp {
namespace WTFAgent {

/**
 * Direction of the flow of traffic in which this this WTFAgent filter
 * is placed.
 */
enum FilterDirection { Downstream, Upstream };

/**
 * Configuration for the WTFAgent filter.
 */
class WTFAgentConfig {
 public:
  WTFAgentConfig(
                         const FilterDirection filter_direction);

  // Direction of filter.
  const FilterDirection filter_direction_;

 private:
};

using WTFAgentConfigSharedPtr = std::shared_ptr<WTFAgentConfig>;

/**
 * A WTFAgent filter instance. One per connection.
 */
class WTFAgentFilter : public Network::Filter,
                               protected Logger::Loggable<Logger::Id::filter> {
 public:
  WTFAgentFilter(WTFAgentConfigSharedPtr config)
      : config_(config),
        conn_state_(ConnProtocolNotRead) {}

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
  // Config for WTFAgent filter.
  WTFAgentConfigSharedPtr config_;
  // Read callback instance.
  Network::ReadFilterCallbacks* read_callbacks_{};
  // Write callback instance.
  Network::WriteFilterCallbacks* write_callbacks_{};

  // Captures the state machine of what is going on in the filter.
  enum {
    ConnProtocolNotRead,        // Connection Protocol has not been read yet
    WriteMetadata,              // Write node metadata
    ReadingInitialHeader,       // WTFAgentInitialHeader is being read
    ReadingProxyHeader,         // Proxy Header is being read
    NeedMoreDataInitialHeader,  // Need more data to be read
    NeedMoreDataProxyHeader,    // Need more data to be read
    Done,                       // Alpn Protocol Found and all the read is done
    Invalid,                    // Invalid state, all operations fail
  } conn_state_;
};

}  // namespace WTFAgent
}  // namespace Tcp
}  // namespace Envoy
