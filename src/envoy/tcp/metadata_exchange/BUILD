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

# Metadata Exchange filter

load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "metadata_exchange",
    srcs = [
        "metadata_exchange.cc",
    ],
    hdrs = [
        "metadata_exchange.h",
    ],
    repository = "@envoy",
    deps = [
        "//src/envoy/tcp/metadata_exchange/config:metadata_exchange_cc_proto",
        "@envoy//envoy/network:connection_interface",
        "@envoy//envoy/network:filter_interface",
        "@envoy//source/common/buffer:buffer_lib",
        "@envoy//envoy/upstream:cluster_manager_interface",
        "@envoy//source/common/http:message_lib",
    ],
)

envoy_cc_library(
    name = "config_lib",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    repository = "@envoy",
    visibility = ["//visibility:public"],
    deps = [
        ":metadata_exchange",
        "//src/envoy/tcp/metadata_exchange/config:metadata_exchange_cc_proto",
        "@envoy//envoy/registry",
        "@envoy//envoy/server:filter_config_interface",
    ],
)
