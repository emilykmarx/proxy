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

#include "src/envoy/tcp/wtf_agent/config.h"

// TODO remove unneeded deps, and the upstream filter/other extraneous stuff copied from metadata_exchange
#include "envoy/network/connection.h"
#include "envoy/registry/registry.h"
#include "src/envoy/tcp/wtf_agent/wtf_agent.h"
#include "envoy/local_info/local_info.h"
#include "source/common/buffer/buffer_impl.h"
#include "source/common/protobuf/utility.h"
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
#include "absl/base/internal/endian.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "envoy/network/connection.h"

namespace Envoy {
namespace Tcp {
namespace WTFAgent {
namespace {

static constexpr char StatPrefix[] = "wtf_agent.";

Network::FilterFactoryCb createFilterFactoryHelper(
    const envoy::tcp::wtf_agent::config::WTFAgent& /*proto_config*/,
    Server::Configuration::CommonFactoryContext& context,
    FilterDirection filter_direction) {

  WTFAgentConfigSharedPtr filter_config(
      std::make_shared<WTFAgentConfig>(
          filter_direction
         ));

    /* metadata_exchange/config.cc can't do this either...
  ENVOY_LOG(error, "adding filter (log)");
  ENVOY_LOG_MISC(error, "adding filter (log_misc)");
  */

  return [filter_config,
          &context](Network::FilterManager& filter_manager) -> void {
    filter_manager.addFilter(std::make_shared<WTFAgentFilter>(
        filter_config));
  };
}
}  // namespace

Network::FilterFactoryCb
WTFAgentConfigFactory::createFilterFactoryFromProto(
    const Protobuf::Message& config,
    Server::Configuration::FactoryContext& context) {
  return createFilterFactory(
      dynamic_cast<
          const envoy::tcp::wtf_agent::config::WTFAgent&>(
          config),
      context);
}

ProtobufTypes::MessagePtr
WTFAgentConfigFactory::createEmptyConfigProto() {
  return std::make_unique<
      envoy::tcp::wtf_agent::config::WTFAgent>();
}

Network::FilterFactoryCb WTFAgentConfigFactory::createFilterFactory(
    const envoy::tcp::wtf_agent::config::WTFAgent& proto_config,
    Server::Configuration::FactoryContext& context) {
  return createFilterFactoryHelper(proto_config, context,
                                   FilterDirection::Downstream);
}

Network::FilterFactoryCb
WTFAgentUpstreamConfigFactory::createFilterFactoryFromProto(
    const Protobuf::Message& config,
    Server::Configuration::CommonFactoryContext& context) {
  return createFilterFactory(
      dynamic_cast<
          const envoy::tcp::wtf_agent::config::WTFAgent&>(
          config),
      context);
}

ProtobufTypes::MessagePtr
WTFAgentUpstreamConfigFactory::createEmptyConfigProto() {
  return std::make_unique<
      envoy::tcp::wtf_agent::config::WTFAgent>();
}

Network::FilterFactoryCb
WTFAgentUpstreamConfigFactory::createFilterFactory(
    const envoy::tcp::wtf_agent::config::WTFAgent& proto_config,
    Server::Configuration::CommonFactoryContext& context) {
  return createFilterFactoryHelper(proto_config, context,
                                   FilterDirection::Upstream);
}

/**
 * Static registration for the WTFAgent Downstream filter. @see
 * RegisterFactory.
 */
static Registry::RegisterFactory<
    WTFAgentConfigFactory,
    Server::Configuration::NamedNetworkFilterConfigFactory>
    registered_;

/**
 * Static registration for the WTFAgent Upstream filter. @see
 * RegisterFactory.
 */
static Registry::RegisterFactory<
    WTFAgentUpstreamConfigFactory,
    Server::Configuration::NamedUpstreamNetworkFilterConfigFactory>
    registered_upstream_;

}  // namespace WTFAgent
}  // namespace Tcp
}  // namespace Envoy
