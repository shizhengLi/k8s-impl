#include "storage/storage.h"
#include <algorithm>
#include <sstream>

namespace k8s {

// Pod操作实现
std::optional<Pod> MemoryStorage::get_pod(const std::string& name, const std::string& namespace_) {
    std::lock_guard<std::mutex> lock(pods_mutex_);
    std::string key = generate_pod_key(name, namespace_);
    auto it = pods_.find(key);
    if (it != pods_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<Pod> MemoryStorage::list_pods(const std::string& namespace_) {
    std::lock_guard<std::mutex> lock(pods_mutex_);
    std::vector<Pod> result;

    for (const auto& [key, pod] : pods_) {
        if (namespace_.empty() || pod.metadata.namespace_ == namespace_) {
            result.push_back(pod);
        }
    }

    return result;
}

bool MemoryStorage::create_pod(const Pod& pod) {
    std::lock_guard<std::mutex> lock(pods_mutex_);
    std::string key = generate_pod_key(pod.metadata.name, pod.metadata.namespace_);

    if (pods_.find(key) != pods_.end()) {
        return false; // 已存在
    }

    pods_[key] = pod;
    return true;
}

bool MemoryStorage::update_pod(const Pod& pod) {
    std::lock_guard<std::mutex> lock(pods_mutex_);
    std::string key = generate_pod_key(pod.metadata.name, pod.metadata.namespace_);

    if (pods_.find(key) == pods_.end()) {
        return false; // 不存在
    }

    pods_[key] = pod;
    return true;
}

bool MemoryStorage::delete_pod(const std::string& name, const std::string& namespace_) {
    std::lock_guard<std::mutex> lock(pods_mutex_);
    std::string key = generate_pod_key(name, namespace_);

    if (pods_.find(key) == pods_.end()) {
        return false; // 不存在
    }

    pods_.erase(key);
    return true;
}

// Node操作实现
std::optional<Node> MemoryStorage::get_node(const std::string& name) {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    std::string key = generate_node_key(name);
    auto it = nodes_.find(key);
    if (it != nodes_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<Node> MemoryStorage::list_nodes() {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    std::vector<Node> result;

    for (const auto& [key, node] : nodes_) {
        result.push_back(node);
    }

    return result;
}

bool MemoryStorage::create_node(const Node& node) {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    std::string key = generate_node_key(node.metadata.name);

    if (nodes_.find(key) != nodes_.end()) {
        return false; // 已存在
    }

    nodes_[key] = node;
    return true;
}

bool MemoryStorage::update_node(const Node& node) {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    std::string key = generate_node_key(node.metadata.name);

    if (nodes_.find(key) == nodes_.end()) {
        return false; // 不存在
    }

    nodes_[key] = node;
    return true;
}

bool MemoryStorage::delete_node(const std::string& name) {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    std::string key = generate_node_key(name);

    if (nodes_.find(key) == nodes_.end()) {
        return false; // 不存在
    }

    nodes_.erase(key);
    return true;
}

// 通用操作实现
bool MemoryStorage::exists(const std::string& resource_type, const std::string& name, const std::string& namespace_) {
    if (resource_type == "pod") {
        std::lock_guard<std::mutex> lock(pods_mutex_);
        std::string key = generate_pod_key(name, namespace_);
        return pods_.find(key) != pods_.end();
    } else if (resource_type == "node") {
        std::lock_guard<std::mutex> lock(nodes_mutex_);
        std::string key = generate_node_key(name);
        return nodes_.find(key) != nodes_.end();
    }
    return false;
}

void MemoryStorage::clear() {
    std::lock_guard<std::mutex> pods_lock(pods_mutex_);
    std::lock_guard<std::mutex> nodes_lock(nodes_mutex_);
    pods_.clear();
    nodes_.clear();
}

// 私有方法实现
std::string MemoryStorage::generate_pod_key(const std::string& name, const std::string& namespace_) {
    std::ostringstream oss;
    oss << namespace_ << "/" << name;
    return oss.str();
}

std::string MemoryStorage::generate_node_key(const std::string& name) {
    return name;
}

// 存储工厂实现
std::unique_ptr<Storage> StorageFactory::create_memory_storage() {
    return std::make_unique<MemoryStorage>();
}

} // namespace k8s