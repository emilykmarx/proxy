load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "alpn_filter",
    srcs = ["alpn_filter.cc"],
    hdrs = ["alpn_filter.h"],
    repository = "@envoy",
    deps = [
        "//external:alpn_filter_config_cc_proto",
        "@envoy//envoy/http:filter_interface",
        "@envoy//envoy/upstream:cluster_manager_interface",
        "@envoy//envoy/access_log:access_log_interface",
        "//extensions/common:context",
        "@envoy//envoy/stats:stats_macros",
    ],
)

envoy_cc_library(
    name = "config_lib",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    repository = "@envoy",
    visibility = ["//visibility:public"],
    deps = [
        ":alpn_filter",
        "//src/envoy/utils:filter_names_lib",
        "@envoy//envoy/registry",
        "@envoy//source/exe:envoy_common_lib",
        "@envoy//source/extensions/filters/http/common:factory_base_lib",
    ],
)
