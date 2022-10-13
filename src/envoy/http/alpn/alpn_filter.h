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

#include "envoy/config/filter/http/alpn/v2alpha1/config.pb.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/http/filter.h"
#include "envoy/access_log/access_log.h"

namespace Envoy {
namespace Http {
namespace Alpn {

class AlpnFilterConfig {
 public:
  AlpnFilterConfig(
      const istio::envoy::config::filter::http::alpn::v2alpha1::FilterConfig
          &proto_config,
      Upstream::ClusterManager &cluster_manager);

  Upstream::ClusterManager &clusterManager() { return cluster_manager_; }

  Upstream::ClusterManager &cluster_manager_;
};

using AlpnFilterConfigSharedPtr = std::shared_ptr<AlpnFilterConfig>;

class AlpnFilter : public StreamFilter,
                   Logger::Loggable<Logger::Id::filter>,
                   public AccessLog::Instance {
 public:
  explicit AlpnFilter(const AlpnFilterConfigSharedPtr &config)
      : config_(config) {}

  FilterHeadersStatus decodeHeaders(RequestHeaderMap &headers,
                                          bool end_stream) override;

  FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;

  FilterHeadersStatus encodeHeaders(ResponseHeaderMap& headers, bool end_stream) override;

  FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;

  void onDestroy() override;

  // TODO what to do with these?
  FilterHeadersStatus encode1xxHeaders(ResponseHeaderMap&) override {return FilterHeadersStatus::Continue;};
  FilterTrailersStatus encodeTrailers(ResponseTrailerMap&) override {return FilterTrailersStatus::Continue;};
  FilterMetadataStatus encodeMetadata(MetadataMap&) override {return FilterMetadataStatus::Continue;};
  void setEncoderFilterCallbacks(StreamEncoderFilterCallbacks& callbacks) override {
    encoder_callbacks_ = &callbacks;
  };
  FilterTrailersStatus decodeTrailers(RequestTrailerMap&) override {return FilterTrailersStatus::Continue;};
  void setDecoderFilterCallbacks(StreamDecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
  };

  // AccessLog::Instance
  void log(const RequestHeaderMap* request_headers,
           const ResponseHeaderMap* response_headers,
           const ResponseTrailerMap* response_trailers,
           const StreamInfo::StreamInfo& stream_info) override;

 private:
  const AlpnFilterConfigSharedPtr config_;
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{};
};

}  // namespace Alpn
}  // namespace Http
}  // namespace Envoy
