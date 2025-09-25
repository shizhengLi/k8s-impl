#include "../include/api/types.h"
#include "../include/storage/storage.h"
#include <iostream>

using namespace k8s;

int main() {
    std::cout << "=== C++ Kubernetes 基本使用示例 ===" << std::endl;

    // 创建存储
    auto storage = StorageFactory::create_memory_storage();

    // 创建节点
    Node node1("node1");
    node1.spec.capacity["cpu"] = ResourceQuantity("4");
    node1.spec.capacity["memory"] = ResourceQuantity("16Gi");
    node1.status.phase = NodePhase::Running;
    node1.status.addresses.push_back({"InternalIP", "192.168.1.100"});
    node1.status.addresses.push_back({"Hostname", "node1"});

    Node node2("node2");
    node2.spec.capacity["cpu"] = ResourceQuantity("2");
    node2.spec.capacity["memory"] = ResourceQuantity("8Gi");
    node2.status.phase = NodePhase::Running;
    node2.status.addresses.push_back({"InternalIP", "192.168.1.101"});
    node2.status.addresses.push_back({"Hostname", "node2"});

    // 创建Pod
    Pod nginx_pod("nginx-pod", "default");
    nginx_pod.spec.containers.emplace_back("nginx", "nginx:latest");
    nginx_pod.spec.containers[0].ports.push_back({80, "TCP", "http", 0});
    nginx_pod.spec.containers[0].env.emplace_back("ENVIRONMENT", "production");
    nginx_pod.spec.node_name = "node1";

    Pod redis_pod("redis-pod", "default");
    redis_pod.spec.containers.emplace_back("redis", "redis:6.2");
    redis_pod.spec.containers[0].ports.push_back({6379, "TCP", "redis", 0});
    redis_pod.spec.node_name = "node2";

    // 存储资源
    std::cout << "\n1. 创建节点..." << std::endl;
    if (storage->create_node(node1)) {
        std::cout << "✓ 节点 node1 创建成功" << std::endl;
    }
    if (storage->create_node(node2)) {
        std::cout << "✓ 节点 node2 创建成功" << std::endl;
    }

    std::cout << "\n2. 创建Pod..." << std::endl;
    if (storage->create_pod(nginx_pod)) {
        std::cout << "✓ Pod nginx-pod 创建成功" << std::endl;
    }
    if (storage->create_pod(redis_pod)) {
        std::cout << "✓ Pod redis-pod 创建成功" << std::endl;
    }

    // 列出所有节点
    std::cout << "\n3. 节点列表:" << std::endl;
    auto nodes = storage->list_nodes();
    for (const auto& node : nodes) {
        std::cout << "  - " << node.metadata.name
                  << " (Phase: " << phase_to_string(node.status.phase) << ")" << std::endl;
        std::cout << "    CPU: " << node.spec.capacity["cpu"].value
                  << ", Memory: " << node.spec.capacity["memory"].value << std::endl;
    }

    // 列出所有Pod
    std::cout << "\n4. Pod列表:" << std::endl;
    auto pods = storage->list_pods();
    for (const auto& pod : pods) {
        std::cout << "  - " << pod.metadata.name
                  << " (Namespace: " << pod.metadata.namespace_ << ")" << std::endl;
        std::cout << "    Phase: " << phase_to_string(pod.status.phase) << std::endl;
        std::cout << "    Node: " << pod.spec.node_name << std::endl;
        std::cout << "    Containers: " << pod.spec.containers.size() << std::endl;
        for (const auto& container : pod.spec.containers) {
            std::cout << "      - " << container.name << ": " << container.image << std::endl;
        }
    }

    // 获取特定Pod
    std::cout << "\n5. 获取特定Pod:" << std::endl;
    auto retrieved_pod = storage->get_pod("nginx-pod", "default");
    if (retrieved_pod) {
        std::cout << "✓ 找到 nginx-pod" << std::endl;
        std::cout << "  UID: " << retrieved_pod->metadata.uid << std::endl;
        std::cout << "  Created: " << std::chrono::system_clock::to_time_t(retrieved_pod->metadata.creation_timestamp) << std::endl;
    }

    // 更新Pod状态
    std::cout << "\n6. 更新Pod状态..." << std::endl;
    retrieved_pod->status.phase = PodPhase::Running;
    retrieved_pod->status.host_ip = "192.168.1.100";
    retrieved_pod->status.pod_ip = "10.244.0.5";

    if (storage->update_pod(*retrieved_pod)) {
        std::cout << "✓ Pod状态更新成功" << std::endl;
    }

    // 验证更新
    auto updated_pod = storage->get_pod("nginx-pod", "default");
    if (updated_pod && updated_pod->status.phase == PodPhase::Running) {
        std::cout << "✓ 验证: Pod状态已更新为Running" << std::endl;
        std::cout << "  Host IP: " << updated_pod->status.host_ip << std::endl;
        std::cout << "  Pod IP: " << updated_pod->status.pod_ip << std::endl;
    }

    // 按命名空间过滤
    std::cout << "\n7. 按命名空间过滤:" << std::endl;
    auto default_pods = storage->list_pods("default");
    std::cout << "default命名空间中的Pod数量: " << default_pods.size() << std::endl;

    auto system_pods = storage->list_pods("kube-system");
    std::cout << "kube-system命名空间中的Pod数量: " << system_pods.size() << std::endl;

    // 检查资源存在性
    std::cout << "\n8. 资源存在性检查:" << std::endl;
    std::cout << "nginx-pod 存在: " << (storage->exists("pod", "nginx-pod", "default") ? "✓" : "✗") << std::endl;
    std::cout << "node1 存在: " << (storage->exists("node", "node1") ? "✓" : "✗") << std::endl;
    std::cout << "non-existent 存在: " << (storage->exists("pod", "non-existent", "default") ? "✓" : "✗") << std::endl;

    // 删除资源
    std::cout << "\n9. 删除资源..." << std::endl;
    if (storage->delete_pod("redis-pod", "default")) {
        std::cout << "✓ redis-pod 删除成功" << std::endl;
    }

    // 验证删除
    auto remaining_pods = storage->list_pods();
    std::cout << "删除后剩余Pod数量: " << remaining_pods.size() << std::endl;

    std::cout << "\n=== 示例完成 ===" << std::endl;
    return 0;
}