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
namespace MetadataExchange {

MetadataExchangeConfig::MetadataExchangeConfig(
    const std::string& protocol,
    const FilterDirection filter_direction)
    : protocol_(protocol),
      filter_direction_(filter_direction) {}

Network::FilterStatus MetadataExchangeFilter::onData(Buffer::Instance& data,
                                                     bool) {
  ENVOY_LOG(error, "onData; data {}", data.toString());
  return Network::FilterStatus::Continue;
}

Network::FilterStatus MetadataExchangeFilter::onNewConnection() {
  ENVOY_LOG(error, "onNewConnection");
  return Network::FilterStatus::Continue;
}

Network::FilterStatus MetadataExchangeFilter::onWrite(Buffer::Instance& data, bool) {
  ENVOY_LOG(error, "onWrite; data {}", data.toString());
  return Network::FilterStatus::Continue;
}

}  // namespace MetadataExchange
}  // namespace Tcp
}  // namespace Envoy
