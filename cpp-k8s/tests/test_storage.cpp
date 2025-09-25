#include <gtest/gtest.h>
#include "../include/storage/storage.h"
#include "../include/api/types.h"

using namespace k8s;

class StorageTest : public ::testing::Test {
protected:
    void SetUp() override {
        storage = StorageFactory::create_memory_storage();
    }

    void TearDown() override {
        storage->clear();
    }

    std::unique_ptr<Storage> storage;
};

TEST_F(StorageTest, CreatePod) {
    Pod pod("test-pod", "default");
    pod.spec.containers.emplace_back("nginx", "nginx:latest");

    EXPECT_TRUE(storage->create_pod(pod));
    EXPECT_FALSE(storage->create_pod(pod)); // 重复创建应该失败
}

TEST_F(StorageTest, GetPod) {
    Pod pod("test-pod", "default");
    pod.spec.containers.emplace_back("nginx", "nginx:latest");

    storage->create_pod(pod);

    auto retrieved = storage->get_pod("test-pod", "default");
    EXPECT_TRUE(retrieved);
    EXPECT_EQ(retrieved->metadata.name, "test-pod");
    EXPECT_EQ(retrieved->metadata.namespace_, "default");

    auto not_found = storage->get_pod("non-existent", "default");
    EXPECT_FALSE(not_found);
}

TEST_F(StorageTest, UpdatePod) {
    Pod pod("test-pod", "default");
    pod.spec.containers.emplace_back("nginx", "nginx:latest");

    storage->create_pod(pod);

    // 更新pod状态
    pod.status.phase = PodPhase::Running;
    EXPECT_TRUE(storage->update_pod(pod));

    auto retrieved = storage->get_pod("test-pod", "default");
    EXPECT_TRUE(retrieved);
    EXPECT_EQ(retrieved->status.phase, PodPhase::Running);

    // 更新不存在的pod应该失败
    Pod non_existent("non-existent", "default");
    EXPECT_FALSE(storage->update_pod(non_existent));
}

TEST_F(StorageTest, DeletePod) {
    Pod pod("test-pod", "default");
    pod.spec.containers.emplace_back("nginx", "nginx:latest");

    storage->create_pod(pod);

    EXPECT_TRUE(storage->delete_pod("test-pod", "default"));
    EXPECT_FALSE(storage->get_pod("test-pod", "default"));

    // 删除不存在的pod应该失败
    EXPECT_FALSE(storage->delete_pod("non-existent", "default"));
}

TEST_F(StorageTest, ListPods) {
    // 创建多个pod
    Pod pod1("pod1", "default");
    pod1.spec.containers.emplace_back("nginx", "nginx:latest");

    Pod pod2("pod2", "default");
    pod2.spec.containers.emplace_back("redis", "redis:latest");

    Pod pod3("pod3", "kube-system");
    pod3.spec.containers.emplace_back("dns", "dns:latest");

    storage->create_pod(pod1);
    storage->create_pod(pod2);
    storage->create_pod(pod3);

    // 列出所有pod
    auto all_pods = storage->list_pods();
    EXPECT_EQ(all_pods.size(), 3);

    // 列出特定命名空间的pod
    auto default_pods = storage->list_pods("default");
    EXPECT_EQ(default_pods.size(), 2);

    auto system_pods = storage->list_pods("kube-system");
    EXPECT_EQ(system_pods.size(), 1);
}

TEST_F(StorageTest, NodeOperations) {
    Node node("test-node");
    node.spec.capacity["cpu"] = ResourceQuantity("4");
    node.spec.capacity["memory"] = ResourceQuantity("16Gi");

    // 创建节点
    EXPECT_TRUE(storage->create_node(node));
    EXPECT_FALSE(storage->create_node(node)); // 重复创建应该失败

    // 获取节点
    auto retrieved = storage->get_node("test-node");
    EXPECT_TRUE(retrieved);
    EXPECT_EQ(retrieved->metadata.name, "test-node");

    // 更新节点
    node.status.phase = NodePhase::Running;
    EXPECT_TRUE(storage->update_node(node));

    auto updated = storage->get_node("test-node");
    EXPECT_TRUE(updated);
    EXPECT_EQ(updated->status.phase, NodePhase::Running);

    // 列出节点
    auto nodes = storage->list_nodes();
    EXPECT_EQ(nodes.size(), 1);

    // 删除节点
    EXPECT_TRUE(storage->delete_node("test-node"));
    EXPECT_FALSE(storage->get_node("test-node"));
}

TEST_F(StorageTest, Exists) {
    Pod pod("test-pod", "default");
    pod.spec.containers.emplace_back("nginx", "nginx:latest");

    Node node("test-node");

    // 初始状态
    EXPECT_FALSE(storage->exists("pod", "test-pod", "default"));
    EXPECT_FALSE(storage->exists("node", "test-node"));

    // 创建后
    storage->create_pod(pod);
    storage->create_node(node);

    EXPECT_TRUE(storage->exists("pod", "test-pod", "default"));
    EXPECT_TRUE(storage->exists("node", "test-node"));
}

TEST_F(StorageTest, Clear) {
    Pod pod("test-pod", "default");
    Node node("test-node");

    storage->create_pod(pod);
    storage->create_node(node);

    EXPECT_EQ(storage->list_pods().size(), 1);
    EXPECT_EQ(storage->list_nodes().size(), 1);

    storage->clear();

    EXPECT_EQ(storage->list_pods().size(), 0);
    EXPECT_EQ(storage->list_nodes().size(), 0);
}