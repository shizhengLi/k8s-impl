#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <httplib.h>
#include <future>
#include "../include/server/http_server.h"
#include "../include/storage/storage.h"

using namespace k8s;

class IntegrationTest : public ::testing::Test {
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

// 端到端测试：完整的Pod生命周期
TEST_F(IntegrationTest, PodLifecycle) {
    auto client = get_client();

    // 1. 创建多个Pod
    std::vector<std::string> pod_names = {"nginx-pod", "redis-pod", "mysql-pod"};

    for (const auto& pod_name : pod_names) {
        nlohmann::json pod_data = {
            {"metadata", {
                {"name", pod_name},
                {"namespace", "default"}
            }},
            {"spec", {
                {"containers", {{
                    {"name", pod_name + "-container"},
                    {"image", pod_name + ":latest"}
                }}}
            }}
        };

        auto res = client.Post("/api/v1/pods", pod_data.dump(), "application/json");
        ASSERT_NE(res, nullptr);
        EXPECT_EQ(res->status, 201);

        auto json = nlohmann::json::parse(res->body);
        EXPECT_EQ(json["metadata"]["name"], pod_name);
    }

    // 2. 验证所有Pod都被创建
    auto list_res = client.Get("/api/v1/pods");
    ASSERT_NE(list_res, nullptr);
    EXPECT_EQ(list_res->status, 200);

    auto list_json = nlohmann::json::parse(list_res->body);
    EXPECT_EQ(list_json["items"].size(), pod_names.size());

    // 3. 逐个获取并更新Pod状态
    for (const auto& pod_name : pod_names) {
        auto get_res = client.Get("/api/v1/namespaces/default/pods/" + pod_name);
        ASSERT_NE(get_res, nullptr);
        EXPECT_EQ(get_res->status, 200);

        auto pod_json = nlohmann::json::parse(get_res->body);
        EXPECT_EQ(pod_json["metadata"]["name"], pod_name);
        EXPECT_EQ(pod_json["status"]["phase"], "Pending");

        // 更新Pod状态为Running
        nlohmann::json update_data = pod_json;
        update_data["status"]["phase"] = "Running";
        update_data["status"]["hostIP"] = "192.168.1.100";
        update_data["status"]["podIP"] = "10.244.0." + std::to_string(&pod_name - &pod_names[0]);

        auto update_res = client.Put("/api/v1/namespaces/default/pods/" + pod_name, update_data.dump(), "application/json");
        ASSERT_NE(update_res, nullptr);
        EXPECT_EQ(update_res->status, 200);
    }

    // 4. 验证所有Pod都已更新
    auto updated_list_res = client.Get("/api/v1/pods");
    ASSERT_NE(updated_list_res, nullptr);
    EXPECT_EQ(updated_list_res->status, 200);

    auto updated_list_json = nlohmann::json::parse(updated_list_res->body);
    for (const auto& item : updated_list_json["items"]) {
        EXPECT_EQ(item["status"]["phase"], "Running");
        EXPECT_FALSE(item["status"]["hostIP"].is_null());
        EXPECT_FALSE(item["status"]["podIP"].is_null());
    }

    // 5. 删除Pod
    for (size_t i = 0; i < pod_names.size(); ++i) {
        auto delete_res = client.Delete("/api/v1/namespaces/default/pods/" + pod_names[i]);
        ASSERT_NE(delete_res, nullptr);
        EXPECT_EQ(delete_res->status, 200);

        // 验证Pod已被删除
        auto get_res = client.Get("/api/v1/namespaces/default/pods/" + pod_names[i]);
        ASSERT_NE(get_res, nullptr);
        EXPECT_EQ(get_res->status, 404);
    }

    // 6. 验证所有Pod都已被删除
    auto final_list_res = client.Get("/api/v1/pods");
    ASSERT_NE(final_list_res, nullptr);
    EXPECT_EQ(final_list_res->status, 200);

    auto final_list_json = nlohmann::json::parse(final_list_res->body);
    EXPECT_EQ(final_list_json["items"].size(), 0);
}

// 并发测试：多个客户端同时操作
TEST_F(IntegrationTest, ConcurrentAccess) {
    const int NUM_THREADS = 10;
    const int PODS_PER_THREAD = 5;
    std::vector<std::future<void>> futures;

    // 启动多个线程并发创建Pod
    for (int i = 0; i < NUM_THREADS; ++i) {
        futures.push_back(std::async(std::launch::async, [this, i]() {
            auto client = get_client();
            std::string thread_prefix = "thread" + std::to_string(i) + "-";

            for (int j = 0; j < PODS_PER_THREAD; ++j) {
                std::string pod_name = thread_prefix + "pod" + std::to_string(j);

                nlohmann::json pod_data = {
                    {"metadata", {
                        {"name", pod_name},
                        {"namespace", "default"}
                    }},
                    {"spec", {
                        {"containers", {{
                            {"name", "container"},
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

    // 验证所有Pod都被创建
    auto client = get_client();
    auto list_res = client.Get("/api/v1/pods");
    ASSERT_NE(list_res, nullptr);
    EXPECT_EQ(list_res->status, 200);

    auto list_json = nlohmann::json::parse(list_res->body);
    EXPECT_EQ(list_json["items"].size(), NUM_THREADS * PODS_PER_THREAD);

    // 并发读取测试
    futures.clear();
    for (int i = 0; i < NUM_THREADS; ++i) {
        futures.push_back(std::async(std::launch::async, [this]() {
            auto client = get_client();
            auto res = client.Get("/api/v1/pods");
            EXPECT_EQ(res->status, 200);
        }));
    }

    for (auto& future : futures) {
        future.wait();
    }
}

// 错误恢复测试：服务器重启后数据持久化
TEST_F(IntegrationTest, ErrorRecovery) {
    auto client = get_client();

    // 1. 创建一些Pod和Node
    for (int i = 0; i < 5; ++i) {
        // 创建Pod
        nlohmann::json pod_data = {
            {"metadata", {
                {"name", "pod" + std::to_string(i)},
                {"namespace", "default"}
            }},
            {"spec", {
                {"containers", {{
                    {"name", "container"},
                    {"image", "nginx:latest"}
                }}}
            }}
        };

        auto pod_res = client.Post("/api/v1/pods", pod_data.dump(), "application/json");
        EXPECT_EQ(pod_res->status, 201);

        // 创建Node
        nlohmann::json node_data = {
            {"metadata", {
                {"name", "node" + std::to_string(i)}
            }},
            {"spec", {
                {"capacity", {
                    {"cpu", "4"},
                    {"memory", "16Gi"}
                }}
            }}
        };

        auto node_res = client.Post("/api/v1/nodes", node_data.dump(), "application/json");
        EXPECT_EQ(node_res->status, 201);
    }

    // 2. 停止服务器
    server->stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 3. 重新启动服务器（使用相同的存储）
    server = std::make_unique<HttpServer>(0, storage);
    ASSERT_TRUE(server->start());
    port = server->get_port();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 4. 验证数据仍然存在
    auto new_client = get_client();

    auto pods_res = new_client.Get("/api/v1/pods");
    ASSERT_NE(pods_res, nullptr);
    EXPECT_EQ(pods_res->status, 200);

    auto pods_json = nlohmann::json::parse(pods_res->body);
    EXPECT_EQ(pods_json["items"].size(), 5);

    auto nodes_res = new_client.Get("/api/v1/nodes");
    ASSERT_NE(nodes_res, nullptr);
    EXPECT_EQ(nodes_res->status, 200);

    auto nodes_json = nlohmann::json::parse(nodes_res->body);
    EXPECT_EQ(nodes_json["items"].size(), 5);
}

// 命名空间隔离测试
TEST_F(IntegrationTest, NamespaceIsolation) {
    auto client = get_client();

    // 在不同命名空间创建Pod
    std::vector<std::pair<std::string, std::string>> namespace_pods = {
        {"default", "pod1"},
        {"default", "pod2"},
        {"kube-system", "pod3"},
        {"kube-system", "pod4"},
        {"test-namespace", "pod5"}
    };

    for (const auto& [ns, name] : namespace_pods) {
        nlohmann::json pod_data = {
            {"metadata", {
                {"name", name},
                {"namespace", ns}
            }},
            {"spec", {
                {"containers", {{
                    {"name", "container"},
                    {"image", "nginx:latest"}
                }}}
            }}
        };

        auto res = client.Post("/api/v1/pods", pod_data.dump(), "application/json");
        ASSERT_NE(res, nullptr);
        EXPECT_EQ(res->status, 201);
    }

    // 测试命名空间过滤
    auto default_res = client.Get("/api/v1/pods?namespace=default");
    ASSERT_NE(default_res, nullptr);
    EXPECT_EQ(default_res->status, 200);

    auto default_json = nlohmann::json::parse(default_res->body);
    EXPECT_EQ(default_json["items"].size(), 2);

    auto system_res = client.Get("/api/v1/pods?namespace=kube-system");
    ASSERT_NE(system_res, nullptr);
    EXPECT_EQ(system_res->status, 200);

    auto system_json = nlohmann::json::parse(system_res->body);
    EXPECT_EQ(system_json["items"].size(), 2);

    auto test_res = client.Get("/api/v1/pods?namespace=test-namespace");
    ASSERT_NE(test_res, nullptr);
    EXPECT_EQ(test_res->status, 200);

    auto test_json = nlohmann::json::parse(test_res->body);
    EXPECT_EQ(test_json["items"].size(), 1);

    // 验证跨命名空间访问
    auto cross_res = client.Get("/api/v1/namespaces/default/pods/pod3");
    ASSERT_NE(cross_res, nullptr);
    EXPECT_EQ(cross_res->status, 404);

    auto valid_res = client.Get("/api/v1/namespaces/kube-system/pods/pod3");
    ASSERT_NE(valid_res, nullptr);
    EXPECT_EQ(valid_res->status, 200);
}

// 大数据量测试
TEST_F(IntegrationTest, LargeDataset) {
    auto client = get_client();

    const int NUM_PODS = 100;
    const int NUM_NODES = 10;

    // 创建大量节点
    std::cout << "Creating " << NUM_NODES << " nodes..." << std::endl;
    for (int i = 0; i < NUM_NODES; ++i) {
        nlohmann::json node_data = {
            {"metadata", {
                {"name", "node" + std::to_string(i)}
            }},
            {"spec", {
                {"capacity", {
                    {"cpu", "4"},
                    {"memory", "16Gi"},
                    {"pods", "110"}
                }}
            }}
        };

        auto res = client.Post("/api/v1/nodes", node_data.dump(), "application/json");
        ASSERT_NE(res, nullptr);
        EXPECT_EQ(res->status, 201);
    }

    // 创建大量Pod
    std::cout << "Creating " << NUM_PODS << " pods..." << std::endl;
    for (int i = 0; i < NUM_PODS; ++i) {
        std::string node_name = "node" + std::to_string(i % NUM_NODES);

        nlohmann::json pod_data = {
            {"metadata", {
                {"name", "pod" + std::to_string(i)},
                {"namespace", "default"}
            }},
            {"spec", {
                {"containers", {{
                    {"name", "container"},
                    {"image", "nginx:latest"},
                    {"ports", {{
                        {"containerPort", 80},
                        {"protocol", "TCP"}
                    }}}
                }}},
                {"nodeName", node_name}
            }}
        };

        auto res = client.Post("/api/v1/pods", pod_data.dump(), "application/json");
        ASSERT_NE(res, nullptr);
        EXPECT_EQ(res->status, 201);
    }

    // 验证列表性能
    std::cout << "Testing list performance..." << std::endl;
    auto start_time = std::chrono::high_resolution_clock::now();

    auto pods_res = client.Get("/api/v1/pods");
    auto end_time = std::chrono::high_resolution_clock::now();

    ASSERT_NE(pods_res, nullptr);
    EXPECT_EQ(pods_res->status, 200);

    auto pods_json = nlohmann::json::parse(pods_res->body);
    EXPECT_EQ(pods_json["items"].size(), NUM_PODS);

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    std::cout << "Listed " << NUM_PODS << " pods in " << duration.count() << "ms" << std::endl;
    EXPECT_LT(duration.count(), 1000); // 应该在1秒内完成

    // 验证节点列表
    auto nodes_res = client.Get("/api/v1/nodes");
    ASSERT_NE(nodes_res, nullptr);
    EXPECT_EQ(nodes_res->status, 200);

    auto nodes_json = nlohmann::json::parse(nodes_res->body);
    EXPECT_EQ(nodes_json["items"].size(), NUM_NODES);

    // 验证按节点过滤的Pod
    auto node_pods_res = client.Get("/api/v1/pods?nodeName=node0");
    ASSERT_NE(node_pods_res, nullptr);
    EXPECT_EQ(node_pods_res->status, 200);

    auto node_pods_json = nlohmann::json::parse(node_pods_res->body);
    EXPECT_GT(node_pods_json["items"].size(), 0);
}

// 健康检查和监控测试
TEST_F(IntegrationTest, HealthAndMonitoring) {
    auto client = get_client();

    // 1. 健康检查
    for (int i = 0; i < 10; ++i) {
        auto health_res = client.Get("/health");
        ASSERT_NE(health_res, nullptr);
        EXPECT_EQ(health_res->status, 200);

        auto health_json = nlohmann::json::parse(health_res->body);
        EXPECT_EQ(health_json["status"], "ok");
    }

    // 2. 负载下的健康检查
    std::vector<std::future<void>> futures;
    for (int i = 0; i < 5; ++i) {
        futures.push_back(std::async(std::launch::async, [this, i]() {
            auto client = get_client();
            for (int j = 0; j < 20; ++j) {
                nlohmann::json pod_data = {
                    {"metadata", {
                        {"name", "load-pod-" + std::to_string(i) + "-" + std::to_string(j)},
                        {"namespace", "default"}
                    }},
                    {"spec", {
                        {"containers", {{
                            {"name", "container"},
                            {"image", "nginx:latest"}
                        }}}
                    }}
                };

                client.Post("/api/v1/pods", pod_data.dump(), "application/json");
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }));
    }

    // 在负载下进行健康检查
    for (int i = 0; i < 5; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        auto health_res = client.Get("/health");
        ASSERT_NE(health_res, nullptr);
        EXPECT_EQ(health_res->status, 200);
    }

    // 等待所有操作完成
    for (auto& future : futures) {
        future.wait();
    }
}