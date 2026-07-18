find_package(Protobuf REQUIRED)

# protoc-generated sources
set(PROTO_FILE ${CMAKE_SOURCE_DIR}/proto/bike.proto)
set(GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${GENERATED_DIR})
set(PROTO_SRCS ${GENERATED_DIR}/bike.pb.cc)
set(PROTO_HDRS ${GENERATED_DIR}/bike.pb.h)

add_custom_command(
    OUTPUT ${PROTO_SRCS} ${PROTO_HDRS}
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
        --proto_path=${CMAKE_SOURCE_DIR}/proto
        --cpp_out=${GENERATED_DIR}
        ${PROTO_FILE}
    DEPENDS ${PROTO_FILE}
)

add_library(bike_common STATIC
    src/protocol.cpp
    ${PROTO_SRCS}
)
target_include_directories(bike_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${GENERATED_DIR}
)
target_compile_features(bike_common PUBLIC cxx_std_17)
target_link_libraries(bike_common PUBLIC protobuf::libprotobuf)

# Tests
enable_testing()
find_package(GTest)
if(GTest_FOUND)
    add_executable(test_protocol tests/test_protocol.cpp)
    target_link_libraries(test_protocol PRIVATE bike_common GTest::gtest_main)
    target_include_directories(test_protocol PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_test(NAME protocol COMMAND test_protocol)
endif()
