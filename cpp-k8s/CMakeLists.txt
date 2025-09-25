cmake_minimum_required(VERSION 3.16)
project(k8s-cpp VERSION 1.0.0)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译选项
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# 查找依赖包
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

# 查找nlohmann_json
pkg_check_modules(NLOHMANN_JSON REQUIRED nlohmann_json>=3.7.0)

# 查找uuid
find_package(UUID REQUIRED)

# 如果系统中没有cpp-httplib，我们可以使用FetchContent下载
include(FetchContent)
FetchContent_Declare(
    cpp-httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.10.1
)
FetchContent_MakeAvailable(cpp-httplib)

# 包含目录
include_directories(include)
include_directories(${NLOHMANN_JSON_INCLUDE_DIRS})

# 源文件
set(SOURCES
    src/api/types.cpp
    src/storage/storage.cpp
    src/server/http_server.cpp
)

# 头文件
set(HEADERS
    include/api/types.h
    include/storage/storage.h
    include/server/http_server.h
)

# 创建核心库
add_library(k8s-core STATIC ${SOURCES} ${HEADERS})
target_link_libraries(k8s-core
    PRIVATE
    Threads::Threads
    ${NLOHMANN_JSON_LIBRARIES}
    ${UUID_LIBRARIES}
    cpp-httplib::httplib
)
target_include_directories(k8s-core
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# 编译选项
target_compile_features(k8s-core PUBLIC cxx_std_17)

# 可执行文件
add_executable(api-server src/main/api_server.cpp)
target_link_libraries(api-server k8s-core)

add_executable(controller-manager src/main/controller_manager.cpp)
target_link_libraries(controller-manager k8s-core)

add_executable(scheduler src/main/scheduler.cpp)
target_link_libraries(scheduler k8s-core)

add_executable(kubelet src/main/kubelet.cpp)
target_link_libraries(kubelet k8s-core)

# 测试
enable_testing()
find_package(GTest REQUIRED)
find_package(Threads REQUIRED)

# 编译性能测试选项
option(BUILD_PERFORMANCE_TESTS "Build performance tests" ON)

add_subdirectory(tests)

# 安装
install(TARGETS k8s-core api-server controller-manager scheduler kubelet
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/ DESTINATION include)
install(FILES README.md DESTINATION share/doc/k8s-cpp)

# 打包设置
set(CPACK_PACKAGE_NAME "k8s-cpp")
set(CPACK_PACKAGE_VERSION "1.0.0")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "C++ implementation of Kubernetes")
set(CPACK_PACKAGE_VENDOR "k8s-cpp")
set(CPACK_PACKAGE_CONTACT "your-email@example.com")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")

include(CPack)