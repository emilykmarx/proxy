#pragma once

#include "envoy/config/filter/http/alpn/v2alpha1/config.pb.h"
#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Http {
namespace Alpn {

/**
 * Config registration for the alpn filter.
 */
class AlpnConfigFactory
    : public Server::Configuration::NamedHttpFilterConfigFactory {
 public:
  // Server::Configuration::NamedHttpFilterConfigFactory
  Http::FilterFactoryCb createFilterFactoryFromProto(
      const Protobuf::Message &config, const std::string &stat_prefix,
      Server::Configuration::FactoryContext &context) override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  std::string name() const override;

 private:
  Http::FilterFactoryCb createFilterFactory(
      const istio::envoy::config::filter::http::alpn::v2alpha1::FilterConfig
          &config_pb,
      absl::string_view local_ip,
      Stats::Scope& local_scope, Stats::Scope& root_scope);
};

}  // namespace Alpn
}  // namespace Http
}  // namespace Envoy
