# main/CMakeLists.txt
idf_component_register(
    SRCS "app_main.cpp"
    INCLUDE_DIRS ""
    REQUIRES x402_protocol
)