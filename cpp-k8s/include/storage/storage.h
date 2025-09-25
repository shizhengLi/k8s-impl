#pragma once

#include <memory>
#include <vector>
#include <optional>
#include <mutex>
#include "api/types.h"

namespace k8s {

// 存储接口
class Storage {
public:
    virtual ~Storage() = default;

    // Pod操作
    virtual std::optional<Pod> get_pod(const std::string& name, const std::string& namespace_) = 0;
    virtual std::vector<Pod> list_pods(const std::string& namespace_ = "") = 0;
    virtual bool create_pod(const Pod& pod) = 0;
    virtual bool update_pod(const Pod& pod) = 0;
    virtual bool delete_pod(const std::string& name, const std::string& namespace_) = 0;

    // Node操作
    virtual std::optional<Node> get_node(const std::string& name) = 0;
    virtual std::vector<Node> list_nodes() = 0;
    virtual bool create_node(const Node& node) = 0;
    virtual bool update_node(const Node& node) = 0;
    virtual bool delete_node(const std::string& name) = 0;

    // 通用操作
    virtual bool exists(const std::string& resource_type, const std::string& name, const std::string& namespace_ = "") = 0;
    virtual void clear() = 0;
};

// 内存存储实现
class MemoryStorage : public Storage {
public:
    MemoryStorage() = default;
    ~MemoryStorage() = default;

    // Pod操作实现
    std::optional<Pod> get_pod(const std::string& name, const std::string& namespace_) override;
    std::vector<Pod> list_pods(const std::string& namespace_ = "") override;
    bool create_pod(const Pod& pod) override;
    bool update_pod(const Pod& pod) override;
    bool delete_pod(const std::string& name, const std::string& namespace_) override;

    // Node操作实现
    std::optional<Node> get_node(const std::string& name) override;
    std::vector<Node> list_nodes() override;
    bool create_node(const Node& node) override;
    bool update_node(const Node& node) override;
    bool delete_node(const std::string& name) override;

    // 通用操作实现
    bool exists(const std::string& resource_type, const std::string& name, const std::string& namespace_ = "") override;
    void clear() override;

private:
    // 生成唯一键
    std::string generate_pod_key(const std::string& name, const std::string& namespace_);
    std::string generate_node_key(const std::string& name);

    // 存储数据
    std::unordered_map<std::string, Pod> pods_;
    std::unordered_map<std::string, Node> nodes_;

    // 线程安全
    mutable std::mutex pods_mutex_;
    mutable std::mutex nodes_mutex_;
};

// 存储工厂
class StorageFactory {
public:
    static std::unique_ptr<Storage> create_memory_storage();
    // 未来可以添加其他存储类型
    // static std::unique_ptr<Storage> create_sqlite_storage(const std::string& path);
    // static std::unique_ptr<Storage> create_etcd_storage(const std::string& endpoints);
};

} // namespace k8s