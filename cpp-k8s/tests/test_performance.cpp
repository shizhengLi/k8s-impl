#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <httplib.h>
#include "../include/server/http_server.h"
#include "../include/storage/storage.h"

using namespace k8s;

class PerformanceTest : public ::testing::Test {
protected:
    void SetUp() override {
        storage = StorageFactory::create_memory_storage();
        server = std::make_unique<HttpServer>(0, storage);

        // 启动服务器
        ASSERT_TRUE(server->start());
        port = server->get_port();

        // 等待服务器完全启动
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    void TearDown() override {
        if (server) {
            server->stop();
        }
    }

    httplib::Client get_client() {
        return httplib::Client("localhost", port);
    }

    std::shared_ptr<Storage> storage;
    std::unique_ptr<HttpServer> server;
    int port;
};

// 基准测试：Pod创建性能
TEST_F(PerformanceTest, PodCreationBenchmark) {
    auto client = get_client();
    const int NUM_PODS = 1000;

    auto start_time = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < NUM_PODS; ++i) {
        nlohmann::json pod_data = {
            {"metadata", {
                {"name", "benchmark-pod-" + std::to_string(i)},
                {"namespace", "default"}
            }},
            {"spec", {
                {"containers", {{
                    {"name", "nginx"},
                    {"image", "nginx:latest"}
                }}}
            }}
        };

        auto res = client.Post("/api/v1/pods", pod_data.dump(), "application/json");
        ASSERT_NE(res, nullptr);
        EXPECT_EQ(res->status, 201);
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    std::cout << "Created " << NUM_PODS << " pods in " << duration.count() << "ms" << std::endl;
    std::cout << "Average: " << (double)duration.count() / NUM_PODS << "ms per pod" << std::endl;

    // 性能断言：每个Pod创建时间应小于5ms
    EXPECT_LT(duration.count() / NUM_PODS, 5);
}

// 基准测试：Pod列表性能
TEST_F(PerformanceTest, PodListBenchmark) {
    auto client = get_client();
    const int NUM_PODS = 1000;

    // 先创建Pod
    for (int i = 0; i < NUM_PODS; ++i) {
        nlohmann::json pod_data = {
            {"metadata", {
                {"name", "list-pod-" + std::to_string(i)},
                {"namespace", "default"}
            }},
            {"spec", {
                {"containers", {{
                    {"name", "nginx"},
                    {"image", "nginx:latest"}
                }}}
            }}
        };

        client.Post("/api/v1/pods", pod_data.dump(), "application/json");
    }

    // 测试列表性能
    const int NUM_QUERIES = 100;
    auto start_time = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < NUM_QUERIES; ++i) {
        auto res = client.Get("/api/v1/pods");
        ASSERT_NE(res, nullptr);
        EXPECT_EQ(res->status, 200);
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    std::cout << "Listed pods " << NUM_QUERIES << " times in " << duration.count() << "ms" << std::endl;
    std::cout << "Average: " << (double)duration.count() / NUM_QUERIES << "ms per query" << std::endl;

    // 性能断言：每次查询时间应小于50ms
    EXPECT_LT(duration.count() / NUM_QUERIES, 50);
}

// 基准测试：并发创建性能
TEST_F(PerformanceTest, ConcurrentCreationBenchmark) {
    const int NUM_THREADS = 10;
    const int PODS_PER_THREAD = 100;
    std::vector<std::future<void>> futures;

    auto start_time = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < NUM_THREADS; ++i) {
        futures.push_back(std::async(std::launch::async, [this, i]() {
            auto client = get_client();
            std::string thread_prefix = "concurrent-pod-thread" + std::to_string(i) + "-";

            for (int j = 0; j < PODS_PER_THREAD; ++j) {
                nlohmann::json pod_data = {
                    {"metadata", {
                        {"name", thread_prefix + std::to_string(j)},
                        {"namespace", "default"}
                    }},
                    {"spec", {
                        {"containers", {{
                            {"name", "nginx"},
                            {"image", "nginx:latest"}
                        }}}
                    }}
                };

                auto res = client.Post("/api/v1/pods", pod_data.dump(), "application/json");
                EXPECT_EQ(res->status, 201);
            }
        }));
    }

    // 等待所有线程完成
    for (auto& future : futures) {
        future.wait();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    std::cout << "Concurrently created " << (NUM_THREADS * PODS_PER_THREAD) << " pods in " << duration.count() << "ms" << std::endl;
    std::cout << "Throughput: " << (NUM_THREADS * PODS_PER_THREAD * 1000.0) / duration.count() << " pods/second" << std::endl;

    // 性能断言：吞吐量应大于1000 pods/second
    EXPECT_GT((NUM_THREADS * PODS_PER_THREAD * 1000.0) / duration.count(), 1000);
}

// 内存使用测试
TEST_F(PerformanceTest, MemoryUsageTest) {
    auto client = get_client();
    const int NUM_PODS = 5000;

    // 创建大量Pod
    for (int i = 0; i < NUM_PODS; ++i) {
        nlohmann::json pod_data = {
            {"metadata", {
                {"name", "memory-pod-" + std::to_string(i)},
                {"namespace", "default"}
            }},
            {"spec", {
                {"containers", {{
                    {"name", "nginx"},
                    {"image", "nginx:latest"},
                    {"env", {
                        {{"name", "ENV1"}, {"value", "value1"}},
                        {{"name", "ENV2"}, {"value", "value2"}}
                    }},
                    {"ports", {
                        {{"containerPort", 80}, {"protocol", "TCP"}},
                        {{"containerPort", 443}, {"protocol", "TCP"}}
                    }}
                }}}
            }}
        };

        auto res = client.Post("/api/v1/pods", pod_data.dump(), "application/json");
        EXPECT_EQ(res->status, 201);
    }

    // 验证所有Pod都被创建
    auto list_res = client.Get("/api/v1/pods");
    ASSERT_NE(list_res, nullptr);
    EXPECT_EQ(list_res->status, 200);

    auto list_json = nlohmann::json::parse(list_res->body);
    EXPECT_EQ(list_json["items"].size(), NUM_PODS);

    std::cout << "Successfully stored " << NUM_PODS << " pods in memory" << std::endl;

    // 测试读取性能
    auto read_start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < 100; ++i) {
        auto pod_res = client.Get("/api/v1/namespaces/default/pods/memory-pod-" + std::to_string(i % 1000));
        ASSERT_NE(pod_res, nullptr);
        EXPECT_EQ(pod_res->status, 200);
    }

    auto read_end = std::chrono::high_resolution_clock::now();
    auto read_duration = std::chrono::duration_cast<std::chrono::milliseconds>(read_end - read_start);

    std::cout << "Read 100 pods in " << read_duration.count() << "ms" << std::endl;
    std::cout << "Average read time: " << (double)read_duration.count() / 100 << "ms per pod" << std::endl;

    // 性能断言：读取时间应小于1ms
    EXPECT_LT(read_duration.count() / 100, 1);
}

// 大型JSON序列化/反序列化测试
TEST_F(PerformanceTest, LargeJsonPerformanceTest) {
    auto client = get_client();
    const int NUM_CONTAINERS = 100;

    // 创建包含大量容器的Pod
    nlohmann::json pod_data = {
        {"metadata", {
            {"name", "large-pod"},
            {"namespace", "default"}
        }},
        {"spec", {
            {"containers", nlohmann::json::array()}
        }}
    };

    // 添加大量容器
    for (int i = 0; i < NUM_CONTAINERS; ++i) {
        pod_data["spec"]["containers"].push_back({
            {"name", "container" + std::to_string(i)},
            {"image", "nginx:latest"},
            {"env", {
                {{"name", "ENV1"}, {"value", "value1"}},
                {{"name", "ENV2"}, {"value", "value2"}},
                {{"name", "ENV3"}, {"value", "value3"}}
            }},
            {"ports", {
                {{"containerPort", 80 + i}, {"protocol", "TCP"}},
                {{"containerPort", 8080 + i}, {"protocol", "TCP"}}
            }}
        });
    }

    // 测试创建性能
    auto start_time = std::chrono::high_resolution_clock::now();

    auto res = client.Post("/api/v1/pods", pod_data.dump(), "application/json");

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    ASSERT_NE(res, nullptr);
    EXPECT_EQ(res->status, 201);

    std::cout << "Created pod with " << NUM_CONTAINERS << " containers in " << duration.count() << "ms" << std::endl;
    std::cout << "JSON size: " << pod_data.dump().size() << " bytes" << std::endl;

    // 性能断言：大型Pod创建时间应小于100ms
    EXPECT_LT(duration.count(), 100);

    // 测试读取性能
    auto read_start = std::chrono::high_resolution_clock::now();

    auto get_res = client.Get("/api/v1/namespaces/default/pods/large-pod");

    auto read_end = std::chrono::high_resolution_clock::now();
    auto read_duration = std::chrono::duration_cast<std::chrono::milliseconds>(read_end - read_start);

    ASSERT_NE(get_res, nullptr);
    EXPECT_EQ(get_res->status, 200);

    std::cout << "Read large pod in " << read_duration.count() << "ms" << std::endl;

    // 性能断言：读取时间应小于50ms
    EXPECT_LT(read_duration.count(), 50);
}

// 连接池测试
TEST_F(PerformanceTest, ConnectionPoolTest) {
    const int NUM_CONNECTIONS = 50;
    const int REQUESTS_PER_CONNECTION = 20;
    std::vector<std::future<void>> futures;

    auto start_time = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < NUM_CONNECTIONS; ++i) {
        futures.push_back(std::async(std::launch::async, [this, i]() {
            auto client = get_client();

            for (int j = 0; j < REQUESTS_PER_CONNECTION; ++j) {
                // 创建Pod
                nlohmann::json pod_data = {
                    {"metadata", {
                        {"name", "conn-pod-" + std::to_string(i) + "-" + std::to_string(j)},
                        {"namespace", "default"}
                    }},
                    {"spec", {
                        {"containers", {{
                            {"name", "nginx"},
                            {"image", "nginx:latest"}
                        }}}
                    }}
                };

                auto create_res = client.Post("/api/v1/pods", pod_data.dump(), "application/json");
                EXPECT_EQ(create_res->status, 201);

                // 读取Pod
                auto get_res = client.Get("/api/v1/namespaces/default/pods/conn-pod-" + std::to_string(i) + "-" + std::to_string(j));
                EXPECT_EQ(get_res->status, 200);

                // 删除Pod
                auto delete_res = client.Delete("/api/v1/namespaces/default/pods/conn-pod-" + std::to_string(i) + "-" + std::to_string(j));
                EXPECT_EQ(delete_res->status, 200);
            }
        }));
    }

    // 等待所有连接完成
    for (auto& future : futures) {
        future.wait();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    int total_operations = NUM_CONNECTIONS * REQUESTS_PER_CONNECTION * 3; // create + read + delete
    std::cout << "Completed " << total_operations << " operations in " << duration.count() << "ms" << std::endl;
    std::cout << "Throughput: " << (total_operations * 1000.0) / duration.count() << " operations/second" << std::endl;

    // 性能断言：吞吐量应大于500 operations/second
    EXPECT_GT((total_operations * 1000.0) / duration.count(), 500);
}