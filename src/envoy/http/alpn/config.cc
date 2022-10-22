#include "src/envoy/http/alpn/config.h"

#include "source/common/protobuf/message_validator_impl.h"
#include "src/envoy/http/alpn/alpn_filter.h"
#include "src/envoy/utils/filter_names.h"

using istio::envoy::config::filter::http::alpn::v2alpha1::FilterConfig;

namespace Envoy {
namespace Http {
namespace Alpn {
FilterFactoryCb AlpnConfigFactory::createFilterFactoryFromProto(
    const Protobuf::Message &config, const std::string &,
    Server::Configuration::FactoryContext &context) {
  return createFilterFactory(dynamic_cast<const FilterConfig &>(config),
                             context.localInfo().address()->ip()->addressAsString(),
                             context.clusterManager(),
                             context.scope(), context.api().rootScope());
}

ProtobufTypes::MessagePtr AlpnConfigFactory::createEmptyConfigProto() {
  return ProtobufTypes::MessagePtr{new FilterConfig};
}

std::string AlpnConfigFactory::name() const {
  return Utils::IstioFilterName::kAlpn;
}

FilterFactoryCb AlpnConfigFactory::createFilterFactory(
    const FilterConfig &proto_config,
    absl::string_view local_ip, Upstream::ClusterManager &cluster_manager,
    Stats::Scope& local_scope, Stats::Scope& root_scope) {
  AlpnFilterConfigSharedPtr filter_config{
      std::make_shared<AlpnFilterConfig>(proto_config, local_ip, cluster_manager,
      local_scope, root_scope)};
  return [filter_config](FilterChainFactoryCallbacks &callbacks) -> void {
    auto filter = std::make_shared<AlpnFilter>(filter_config);
    callbacks.addStreamFilter(filter);
    callbacks.addAccessLogHandler(filter);
  };
}

/**
 * Static registration for the alpn override filter. @see RegisterFactory.
 */
REGISTER_FACTORY(AlpnConfigFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory);

}  // namespace Alpn
}  // namespace Http
}  // namespace Envoy
