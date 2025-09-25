#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <chrono>
#include <optional>

namespace k8s {

// 前向声明
class ObjectMeta;
class PodSpec;
class PodStatus;
class NodeSpec;
class NodeStatus;

// 资源量类型
struct ResourceQuantity {
    std::string value;

    ResourceQuantity() = default;
    explicit ResourceQuantity(const std::string& val) : value(val) {}

    bool operator==(const ResourceQuantity& other) const {
        return value == other.value;
    }
};

// 资源列表
using ResourceList = std::map<std::string, ResourceQuantity>;

// 对象元数据
class ObjectMeta {
public:
    std::string name;
    std::string namespace_;
    std::string uid;
    std::string resource_version;
    std::chrono::system_clock::time_point creation_timestamp;
    std::map<std::string, std::string> labels;
    std::map<std::string, std::string> annotations;
    std::optional<std::chrono::system_clock::time_point> deletion_timestamp;

    ObjectMeta() = default;
    ObjectMeta(const std::string& name, const std::string& ns = "default")
        : name(name), namespace_(ns) {
        uid = generate_uid();
        creation_timestamp = std::chrono::system_clock::now();
    }

    // JSON序列化支持
    void to_json(nlohmann::json& j) const;
    void from_json(const nlohmann::json& j);

private:
    std::string generate_uid() const;
};

// Pod阶段
enum class PodPhase {
    Pending,
    Running,
    Succeeded,
    Failed,
    Unknown
};

// 容器端口
struct ContainerPort {
    int32_t container_port;
    std::string protocol;
    std::string name;
    int32_t host_port;

    ContainerPort() : container_port(0), protocol("TCP"), host_port(0) {}

    bool operator==(const ContainerPort& other) const {
        return container_port == other.container_port &&
               protocol == other.protocol &&
               name == other.name &&
               host_port == other.host_port;
    }
};

// 环境变量
struct EnvVar {
    std::string name;
    std::string value;

    EnvVar() = default;
    EnvVar(const std::string& n, const std::string& v) : name(n), value(v) {}

    bool operator==(const EnvVar& other) const {
        return name == other.name && value == other.value;
    }
};

// 容器定义
class Container {
public:
    std::string name;
    std::string image;
    std::vector<EnvVar> env;
    std::vector<ContainerPort> ports;
    std::map<std::string, std::string> args;

    Container() = default;
    Container(const std::string& name, const std::string& image)
        : name(name), image(image) {}

    void to_json(nlohmann::json& j) const;
    void from_json(const nlohmann::json& j);
};

// Pod规格
class PodSpec {
public:
    std::vector<Container> containers;
    std::string node_name;
    bool restart_policy;

    PodSpec() : restart_policy(true) {}

    void to_json(nlohmann::json& j) const;
    void from_json(const nlohmann::json& j);
};

// Pod状态
class PodStatus {
public:
    PodPhase phase;
    std::string message;
    std::string reason;
    std::string host_ip;
    std::string pod_ip;
    std::chrono::system_clock::time_point start_time;

    PodStatus() : phase(PodPhase::Pending) {}

    void to_json(nlohmann::json& j) const;
    void from_json(const nlohmann::json& j);
};

// Pod对象
class Pod {
public:
    std::string api_version;
    std::string kind;
    ObjectMeta metadata;
    PodSpec spec;
    PodStatus status;

    Pod() : api_version("v1"), kind("Pod") {}

    Pod(const std::string& name, const std::string& ns = "default")
        : api_version("v1"), kind("Pod"), metadata(name, ns) {}

    void to_json(nlohmann::json& j) const;
    void from_json(const nlohmann::json& j);

    bool operator==(const Pod& other) const {
        return metadata.uid == other.metadata.uid;
    }
};

// Pod列表
class PodList {
public:
    std::string api_version;
    std::string kind;
    std::vector<Pod> items;

    PodList() : api_version("v1"), kind("PodList") {}

    void to_json(nlohmann::json& j) const;
    void from_json(const nlohmann::json& j);
};

// 节点阶段
enum class NodePhase {
    Pending,
    Running,
    Terminated,
    Unknown
};

// 节点地址
struct NodeAddress {
    std::string type;
    std::string address;

    NodeAddress() = default;
    NodeAddress(const std::string& t, const std::string& a) : type(t), address(a) {}

    bool operator==(const NodeAddress& other) const {
        return type == other.type && address == other.address;
    }
};

// 节点规格
class NodeSpec {
public:
    ResourceList capacity;
    std::string pod_cidr;

    NodeSpec() = default;

    void to_json(nlohmann::json& j) const;
    void from_json(const nlohmann::json& j);
};

// 节点状态
class NodeStatus {
public:
    NodePhase phase;
    ResourceList capacity;
    ResourceList allocatable;
    std::vector<NodeAddress> addresses;

    NodeStatus() : phase(NodePhase::Pending) {}

    void to_json(nlohmann::json& j) const;
    void from_json(const nlohmann::json& j);
};

// 节点对象
class Node {
public:
    std::string api_version;
    std::string kind;
    ObjectMeta metadata;
    NodeSpec spec;
    NodeStatus status;

    Node() : api_version("v1"), kind("Node") {}

    Node(const std::string& name)
        : api_version("v1"), kind("Node"), metadata(name) {}

    void to_json(nlohmann::json& j) const;
    void from_json(const nlohmann::json& j);

    bool operator==(const Node& other) const {
        return metadata.uid == other.metadata.uid;
    }
};

// 节点列表
class NodeList {
public:
    std::string api_version;
    std::string kind;
    std::vector<Node> items;

    NodeList() : api_version("v1"), kind("NodeList") {}

    void to_json(nlohmann::json& j) const;
    void from_json(const nlohmann::json& j);
};

// API状态码
enum class ApiStatus {
    Success,
    Failure,
    NotFound,
    AlreadyExists,
    Invalid
};

// API响应模板
template<typename T>
class ApiResponse {
public:
    ApiStatus status;
    std::string message;
    std::optional<T> data;

    ApiResponse() : status(ApiStatus::Success) {}

    static ApiResponse<T> success(const T& data, const std::string& msg = "") {
        ApiResponse<T> response;
        response.status = ApiStatus::Success;
        response.data = data;
        response.message = msg;
        return response;
    }

    static ApiResponse<T> error(ApiStatus status, const std::string& msg) {
        ApiResponse<T> response;
        response.status = status;
        response.message = msg;
        return response;
    }

    void to_json(nlohmann::json& j) const;
    void from_json(const nlohmann::json& j);
};

// 类型别名
using PodResponse = ApiResponse<Pod>;
using PodListResponse = ApiResponse<PodList>;
using NodeResponse = ApiResponse<Node>;
using NodeListResponse = ApiResponse<NodeList>;

// 常用资源名称
namespace Resources {
    constexpr const char* CPU = "cpu";
    constexpr const char* MEMORY = "memory";
    constexpr const char* PODS = "pods";
    constexpr const char* STORAGE = "storage";
}

// 地址类型
namespace AddressTypes {
    constexpr const char* INTERNAL_IP = "InternalIP";
    constexpr const char* EXTERNAL_IP = "ExternalIP";
    constexpr const char* HOSTNAME = "Hostname";
}

// 便捷函数
inline std::string phase_to_string(PodPhase phase) {
    switch (phase) {
        case PodPhase::Pending: return "Pending";
        case PodPhase::Running: return "Running";
        case PodPhase::Succeeded: return "Succeeded";
        case PodPhase::Failed: return "Failed";
        case PodPhase::Unknown: return "Unknown";
        default: return "Unknown";
    }
}

inline std::string phase_to_string(NodePhase phase) {
    switch (phase) {
        case NodePhase::Pending: return "Pending";
        case NodePhase::Running: return "Running";
        case NodePhase::Terminated: return "Terminated";
        case NodePhase::Unknown: return "Unknown";
        default: return "Unknown";
    }
}

inline PodPhase string_to_pod_phase(const std::string& str) {
    if (str == "Pending") return PodPhase::Pending;
    if (str == "Running") return PodPhase::Running;
    if (str == "Succeeded") return PodPhase::Succeeded;
    if (str == "Failed") return PodPhase::Failed;
    return PodPhase::Unknown;
}

inline NodePhase string_to_node_phase(const std::string& str) {
    if (str == "Pending") return NodePhase::Pending;
    if (str == "Running") return NodePhase::Running;
    if (str == "Terminated") return NodePhase::Terminated;
    return NodePhase::Unknown;
}

} // namespace k8s