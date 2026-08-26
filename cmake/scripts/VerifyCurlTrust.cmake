if(NOT DEFINED HEADER OR NOT EXISTS "${HEADER}")
  message(FATAL_ERROR "Missing generated libcurl configuration: ${HEADER}")
endif()
if(NOT DEFINED EXPECTED_DEFINE OR EXPECTED_DEFINE STREQUAL "")
  message(FATAL_ERROR "Missing expected libcurl trust backend")
endif()

file(READ "${HEADER}" config)
if(NOT config MATCHES "(^|\n)#define ${EXPECTED_DEFINE} 1(\n|$)")
  message(FATAL_ERROR
    "libcurl trust backend is not enabled: ${EXPECTED_DEFINE}")
endif()

foreach(unexpected CURL_CA_BUNDLE CURL_CA_PATH)
  if(config MATCHES "(^|\n)#define ${unexpected} ")
    message(FATAL_ERROR
      "libcurl embeds a build-host trust path: ${unexpected}")
  endif()
endforeach()
