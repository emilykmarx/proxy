# Copyright 2019 Istio Authors. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
################################################################################
#

load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "wtf_agent",
    srcs = [
        "wtf_agent.cc",
    ],
    hdrs = [
        "wtf_agent.h",
    ],
    repository = "@envoy",
    deps = [
        "//extensions/common:context",
        "//extensions/common:proto_util",
        "//src/envoy/tcp/wtf_agent/config:wtf_agent_cc_proto",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/base:endian",
        "@com_google_absl//absl/strings",
        "@envoy//envoy/local_info:local_info_interface",
        "@envoy//envoy/network:connection_interface",
        "@envoy//envoy/network:filter_interface",
        "@envoy//envoy/runtime:runtime_interface",
        "@envoy//envoy/stats:stats_macros",
        "@envoy//envoy/stream_info:filter_state_interface",
        "@envoy//source/common/http:utility_lib",
        "@envoy//source/common/network:utility_lib",
        "@envoy//source/common/protobuf",
        "@envoy//source/common/protobuf:utility_lib",
        "@envoy//source/extensions/filters/common/expr:cel_state_lib",
        "@envoy//source/extensions/filters/network:well_known_names",
    ],
)

envoy_cc_library(
    name = "config_lib",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    repository = "@envoy",
    visibility = ["//visibility:public"],
    deps = [
        ":wtf_agent",
        "//src/envoy/tcp/wtf_agent/config:wtf_agent_cc_proto",
        "//src/envoy/utils:utils_lib",
        "@envoy//envoy/registry",
        "@envoy//envoy/server:filter_config_interface",
        "@envoy//source/common/http:utility_lib",
        "@envoy//source/common/network:utility_lib",
        "@envoy//source/common/protobuf",
        "@envoy//source/common/protobuf:utility_lib",
    ],
)
