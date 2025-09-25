#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <httplib.h>
#include "../include/server/http_server.h"
#include "../include/storage/storage.h"

using namespace k8s;

class HttpServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        storage = StorageFactory::create_memory_storage();
        server = std::make_unique<HttpServer>(0, storage); // 使用随机端口

        // 启动服务器
        ASSERT_TRUE(server->start());

        // 获取实际端口
        port = server->get_port();

        // 等待服务器完全启动
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    void TearDown() override {
        if (server) {
            server->stop();
        }
    }

    // HTTP客户端
    httplib::Client get_client() {
        return httplib::Client("localhost", port);
    }

    std::shared_ptr<Storage> storage;
    std::unique_ptr<HttpServer> server;
    int port;
};

TEST_F(HttpServerTest, HealthCheck) {
    auto client = get_client();
    auto res = client.Get("/health");

    ASSERT_NE(res, nullptr);
    EXPECT_EQ(res->status, 200);

    auto json = nlohmann::json::parse(res->body);
    EXPECT_EQ(json["status"], "ok");
}

TEST_F(HttpServerTest, CreatePod) {
    auto client = get_client();

    nlohmann::json pod_data = {
        {"metadata", {
            {"name", "test-pod"},
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

    auto json = nlohmann::json::parse(res->body);
    EXPECT_EQ(json["metadata"]["name"], "test-pod");
    EXPECT_EQ(json["metadata"]["namespace"], "default");
    EXPECT_EQ(json["spec"]["containers"][0]["name"], "nginx");
}

TEST_F(HttpServerTest, CreateDuplicatePod) {
    auto client = get_client();

    nlohmann::json pod_data = {
        {"metadata", {
            {"name", "test-pod"},
            {"namespace", "default"}
        }},
        {"spec", {
            {"containers", {{
                {"name", "nginx"},
                {"image", "nginx:latest"}
            }}}
        }}
    };

    // 第一次创建应该成功
    auto res1 = client.Post("/api/v1/pods", pod_data.dump(), "application/json");
    ASSERT_NE(res1, nullptr);
    EXPECT_EQ(res1->status, 201);

    // 第二次创建应该失败
    auto res2 = client.Post("/api/v1/pods", pod_data.dump(), "application/json");
    ASSERT_NE(res2, nullptr);
    EXPECT_EQ(res2->status, 409);
}

TEST_F(HttpServerTest, GetPod) {
    auto client = get_client();

    // 先创建一个Pod
    nlohmann::json pod_data = {
        {"metadata", {
            {"name", "test-pod"},
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
    ASSERT_NE(create_res, nullptr);
    EXPECT_EQ(create_res->status, 201);

    // 获取Pod
    auto get_res = client.Get("/api/v1/namespaces/default/pods/test-pod");

    ASSERT_NE(get_res, nullptr);
    EXPECT_EQ(get_res->status, 200);

    auto json = nlohmann::json::parse(get_res->body);
    EXPECT_EQ(json["metadata"]["name"], "test-pod");
    EXPECT_EQ(json["metadata"]["namespace"], "default");
}

TEST_F(HttpServerTest, GetNonExistentPod) {
    auto client = get_client();

    auto res = client.Get("/api/v1/namespaces/default/pods/non-existent");

    ASSERT_NE(res, nullptr);
    EXPECT_EQ(res->status, 404);

    auto json = nlohmann::json::parse(res->body);
    EXPECT_EQ(json["error"], "Pod not found");
}

TEST_F(HttpServerTest, ListPods) {
    auto client = get_client();

    // 创建多个Pod
    std::vector<nlohmann::json> pods = {
        {
            {"metadata", {{"name", "pod1"}, {"namespace", "default"}}},
            {"spec", {{"containers", {{{"name", "nginx"}, {"image", "nginx:latest"}}}}}}
        },
        {
            {"metadata", {{"name", "pod2"}, {"namespace", "default"}}},
            {"spec", {{"containers", {{{"name", "redis"}, {"image", "redis:latest"}}}}}}
        },
        {
            {"metadata", {{"name", "pod3"}, {"namespace", "kube-system"}}},
            {"spec", {{"containers", {{{"name", "dns"}, {"image", "dns:latest"}}}}}}
        }
    };

    for (const auto& pod_data : pods) {
        auto res = client.Post("/api/v1/pods", pod_data.dump(), "application/json");
        ASSERT_NE(res, nullptr);
        EXPECT_EQ(res->status, 201);
    }

    // 列出所有Pod
    auto res = client.Get("/api/v1/pods");
    ASSERT_NE(res, nullptr);
    EXPECT_EQ(res->status, 200);

    auto json = nlohmann::json::parse(res->body);
    EXPECT_EQ(json["apiVersion"], "v1");
    EXPECT_EQ(json["kind"], "PodList");
    EXPECT_EQ(json["items"].size(), 3);

    // 列出特定命名空间的Pod
    auto default_res = client.Get("/api/v1/pods?namespace=default");
    ASSERT_NE(default_res, nullptr);
    EXPECT_EQ(default_res->status, 200);

    auto default_json = nlohmann::json::parse(default_res->body);
    EXPECT_EQ(default_json["items"].size(), 2);
}

TEST_F(HttpServerTest, UpdatePod) {
    auto client = get_client();

    // 先创建一个Pod
    nlohmann::json pod_data = {
        {"metadata", {
            {"name", "test-pod"},
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
    ASSERT_NE(create_res, nullptr);
    EXPECT_EQ(create_res->status, 201);

    // 更新Pod
    nlohmann::json update_data = {
        {"metadata", {
            {"name", "test-pod"},
            {"namespace", "default"}
        }},
        {"spec", {
            {"containers", {{
                {"name", "nginx"},
                {"image", "nginx:1.21"}
            }}}
        }},
        {"status", {
            {"phase", "Running"},
            {"hostIP", "192.168.1.100"},
            {"podIP", "10.244.0.5"}
        }}
    };

    auto update_res = client.Put("/api/v1/namespaces/default/pods/test-pod", update_data.dump(), "application/json");

    ASSERT_NE(update_res, nullptr);
    EXPECT_EQ(update_res->status, 200);

    auto json = nlohmann::json::parse(update_res->body);
    EXPECT_EQ(json["spec"]["containers"][0]["image"], "nginx:1.21");
    EXPECT_EQ(json["status"]["phase"], "Running");
}

TEST_F(HttpServerTest, UpdateNonExistentPod) {
    auto client = get_client();

    nlohmann::json update_data = {
        {"metadata", {
            {"name", "non-existent"},
            {"namespace", "default"}
        }},
        {"spec", {
            {"containers", {{
                {"name", "nginx"},
                {"image", "nginx:latest"}
            }}}
        }}
    };

    auto res = client.Put("/api/v1/namespaces/default/pods/non-existent", update_data.dump(), "application/json");

    ASSERT_NE(res, nullptr);
    EXPECT_EQ(res->status, 404);
}

TEST_F(HttpServerTest, DeletePod) {
    auto client = get_client();

    // 先创建一个Pod
    nlohmann::json pod_data = {
        {"metadata", {
            {"name", "test-pod"},
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
    ASSERT_NE(create_res, nullptr);
    EXPECT_EQ(create_res->status, 201);

    // 删除Pod
    auto delete_res = client.Delete("/api/v1/namespaces/default/pods/test-pod");

    ASSERT_NE(delete_res, nullptr);
    EXPECT_EQ(delete_res->status, 200);

    auto json = nlohmann::json::parse(delete_res->body);
    EXPECT_EQ(json["status"], "success");

    // 验证Pod已被删除
    auto get_res = client.Get("/api/v1/namespaces/default/pods/test-pod");
    ASSERT_NE(get_res, nullptr);
    EXPECT_EQ(get_res->status, 404);
}

TEST_F(HttpServerTest, NodeOperations) {
    auto client = get_client();

    // 创建节点
    nlohmann::json node_data = {
        {"metadata", {
            {"name", "test-node"}
        }},
        {"spec", {
            {"capacity", {
                {"cpu", "4"},
                {"memory", "16Gi"}
            }}
        }}
    };

    auto create_res = client.Post("/api/v1/nodes", node_data.dump(), "application/json");
    ASSERT_NE(create_res, nullptr);
    EXPECT_EQ(create_res->status, 201);

    // 获取节点
    auto get_res = client.Get("/api/v1/nodes/test-node");
    ASSERT_NE(get_res, nullptr);
    EXPECT_EQ(get_res->status, 200);

    auto json = nlohmann::json::parse(get_res->body);
    EXPECT_EQ(json["metadata"]["name"], "test-node");
    EXPECT_EQ(json["spec"]["capacity"]["cpu"], "4");

    // 列出节点
    auto list_res = client.Get("/api/v1/nodes");
    ASSERT_NE(list_res, nullptr);
    EXPECT_EQ(list_res->status, 200);

    auto list_json = nlohmann::json::parse(list_res->body);
    EXPECT_EQ(list_json["items"].size(), 1);

    // 删除节点
    auto delete_res = client.Delete("/api/v1/nodes/test-node");
    ASSERT_NE(delete_res, nullptr);
    EXPECT_EQ(delete_res->status, 200);
}

TEST_F(HttpServerTest, InvalidJson) {
    auto client = get_client();

    // 发送无效JSON
    auto res = client.Post("/api/v1/pods", "invalid json", "application/json");

    ASSERT_NE(res, nullptr);
    EXPECT_EQ(res->status, 400);

    auto json = nlohmann::json::parse(res->body);
    EXPECT_TRUE(json["error"].get<std::string>().find("Invalid JSON") != std::string::npos);
}

TEST_F(HttpServerTest, WrongContentType) {
    auto client = get_client();

    // 发送错误的内容类型
    auto res = client.Post("/api/v1/pods", "{}", "text/plain");

    ASSERT_NE(res, nullptr);
    EXPECT_EQ(res->status, 415);

    auto json = nlohmann::json::parse(res->body);
    EXPECT_TRUE(json["error"].get<std::string>().find("Content-Type") != std::string::npos);
}

TEST_F(HttpServerTest, MismatchUrlAndData) {
    auto client = get_client();

    // 先创建一个Pod
    nlohmann::json pod_data = {
        {"metadata", {
            {"name", "test-pod"},
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
    ASSERT_NE(create_res, nullptr);
    EXPECT_EQ(create_res->status, 201);

    // 尝试更新错误的URL
    nlohmann::json update_data = {
        {"metadata", {
            {"name", "wrong-name"},
            {"namespace", "wrong-namespace"}
        }},
        {"spec", {
            {"containers", {{
                {"name", "nginx"},
                {"image", "nginx:latest"}
            }}}
        }}
    };

    auto res = client.Put("/api/v1/namespaces/default/pods/test-pod", update_data.dump(), "application/json");

    ASSERT_NE(res, nullptr);
    EXPECT_EQ(res->status, 400);

    auto json = nlohmann::json::parse(res->body);
    EXPECT_TRUE(json["error"].get<std::string>().find("does not match URL") != std::string::npos);
}