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

#include "src/envoy/tcp/wtf_agent/wtf_agent.h"

#include <cstdint>
#include <string>

#include "absl/base/internal/endian.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "envoy/network/connection.h"
#include "envoy/stats/scope.h"
#include "source/common/buffer/buffer_impl.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Tcp {
namespace WTFAgent {



WTFAgentConfig::WTFAgentConfig(
    const FilterDirection filter_direction)
    :
      filter_direction_(filter_direction) {}

Network::FilterStatus WTFAgentFilter::onData(Buffer::Instance& data,
                                                     bool) {
  ENVOY_LOG(error, "got data (log): {}", data.toString());
  ENVOY_LOG_MISC(error, "got data (log_misc): {}", data.toString());
  ENVOY_CONN_LOG(error, "got data (conn_log): {}", read_callbacks_->connection(), data.toString());

  return Network::FilterStatus::Continue;
}

Network::FilterStatus WTFAgentFilter::onNewConnection() {
  ENVOY_LOG(error, "new conn (log)");
  ENVOY_LOG_MISC(error, "new conn (log_misc)");
  ENVOY_CONN_LOG(error, "new conn (conn_log)", read_callbacks_->connection());

  return Network::FilterStatus::Continue;
}

Network::FilterStatus WTFAgentFilter::onWrite(Buffer::Instance&, bool) {
  ENVOY_LOG(error, "write (log)");
  ENVOY_LOG_MISC(error, "write (log_misc)");
  ENVOY_CONN_LOG(error, "write (conn_log)", read_callbacks_->connection());

  return Network::FilterStatus::Continue;
}



}  // namespace WTFAgent
}  // namespace Tcp
}  // namespace Envoy
