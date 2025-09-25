#include "api/types.h"
#include <sstream>
#include <uuid/uuid.h>

namespace k8s {

// ObjectMeta实现
void ObjectMeta::to_json(nlohmann::json& j) const {
    j = nlohmann::json{
        {"name", name},
        {"namespace", namespace_},
        {"uid", uid},
        {"resourceVersion", resource_version},
        {"creationTimestamp", std::chrono::system_clock::to_time_t(creation_timestamp)},
        {"labels", labels},
        {"annotations", annotations}
    };

    if (deletion_timestamp) {
        j["deletionTimestamp"] = std::chrono::system_clock::to_time_t(*deletion_timestamp);
    }
}

void ObjectMeta::from_json(const nlohmann::json& j) {
    j.at("name").get_to(name);
    j.at("namespace").get_to(namespace_);
    j.at("uid").get_to(uid);
    j.at("resourceVersion").get_to(resource_version);

    auto timestamp = j.at("creationTimestamp").get<time_t>();
    creation_timestamp = std::chrono::system_clock::from_time_t(timestamp);

    if (j.contains("labels")) {
        j.at("labels").get_to(labels);
    }

    if (j.contains("annotations")) {
        j.at("annotations").get_to(annotations);
    }

    if (j.contains("deletionTimestamp")) {
        auto del_timestamp = j.at("deletionTimestamp").get<time_t>();
        deletion_timestamp = std::chrono::system_clock::from_time_t(del_timestamp);
    }
}

std::string ObjectMeta::generate_uid() const {
    uuid_t uuid;
    uuid_generate(uuid);
    char uuid_str[37];
    uuid_unparse(uuid, uuid_str);
    return std::string(uuid_str);
}

// Container实现
void Container::to_json(nlohmann::json& j) const {
    j = nlohmann::json{
        {"name", name},
        {"image", image}
    };

    if (!env.empty()) {
        j["env"] = nlohmann::json::array();
        for (const auto& env_var : env) {
            j["env"].push_back({
                {"name", env_var.name},
                {"value", env_var.value}
            });
        }
    }

    if (!ports.empty()) {
        j["ports"] = nlohmann::json::array();
        for (const auto& port : ports) {
            j["ports"].push_back({
                {"containerPort", port.container_port},
                {"protocol", port.protocol},
                {"name", port.name},
                {"hostPort", port.host_port}
            });
        }
    }

    if (!args.empty()) {
        j["args"] = nlohmann::json::object();
        for (const auto& [key, value] : args) {
            j["args"][key] = value;
        }
    }
}

void Container::from_json(const nlohmann::json& j) {
    j.at("name").get_to(name);
    j.at("image").get_to(image);

    if (j.contains("env")) {
        for (const auto& env_json : j.at("env")) {
            EnvVar env_var;
            env_var.name = env_json.at("name").get<std::string>();
            env_var.value = env_json.at("value").get<std::string>();
            env.push_back(env_var);
        }
    }

    if (j.contains("ports")) {
        for (const auto& port_json : j.at("ports")) {
            ContainerPort port;
            port.container_port = port_json.at("containerPort").get<int32_t>();
            port.protocol = port_json.value("protocol", "TCP");
            port.name = port_json.value("name", "");
            port.host_port = port_json.value("hostPort", 0);
            ports.push_back(port);
        }
    }

    if (j.contains("args")) {
        for (const auto& [key, value] : j.at("args").items()) {
            args[key] = value.get<std::string>();
        }
    }
}

// PodSpec实现
void PodSpec::to_json(nlohmann::json& j) const {
    j = nlohmann::json{
        {"restartPolicy", restart_policy}
    };

    if (!containers.empty()) {
        j["containers"] = nlohmann::json::array();
        for (const auto& container : containers) {
            nlohmann::json container_json;
            container.to_json(container_json);
            j["containers"].push_back(container_json);
        }
    }

    if (!node_name.empty()) {
        j["nodeName"] = node_name;
    }
}

void PodSpec::from_json(const nlohmann::json& j) {
    if (j.contains("restartPolicy")) {
        j.at("restartPolicy").get_to(restart_policy);
    }

    if (j.contains("containers")) {
        for (const auto& container_json : j.at("containers")) {
            Container container;
            container.from_json(container_json);
            containers.push_back(container);
        }
    }

    if (j.contains("nodeName")) {
        j.at("nodeName").get_to(node_name);
    }
}

// PodStatus实现
void PodStatus::to_json(nlohmann::json& j) const {
    j = nlohmann::json{
        {"phase", phase_to_string(phase)},
        {"startTime", std::chrono::system_clock::to_time_t(start_time)}
    };

    if (!message.empty()) {
        j["message"] = message;
    }

    if (!reason.empty()) {
        j["reason"] = reason;
    }

    if (!host_ip.empty()) {
        j["hostIP"] = host_ip;
    }

    if (!pod_ip.empty()) {
        j["podIP"] = pod_ip;
    }
}

void PodStatus::from_json(const nlohmann::json& j) {
    phase = string_to_pod_phase(j.value("phase", "Pending"));

    if (j.contains("message")) {
        j.at("message").get_to(message);
    }

    if (j.contains("reason")) {
        j.at("reason").get_to(reason);
    }

    if (j.contains("hostIP")) {
        j.at("hostIP").get_to(host_ip);
    }

    if (j.contains("podIP")) {
        j.at("podIP").get_to(pod_ip);
    }

    if (j.contains("startTime")) {
        auto start_time_t = j.at("startTime").get<time_t>();
        start_time = std::chrono::system_clock::from_time_t(start_time_t);
    }
}

// Pod实现
void Pod::to_json(nlohmann::json& j) const {
    j = nlohmann::json{
        {"apiVersion", api_version},
        {"kind", kind}
    };

    nlohmann::json metadata_json;
    metadata.to_json(metadata_json);
    j["metadata"] = metadata_json;

    nlohmann::json spec_json;
    spec.to_json(spec_json);
    j["spec"] = spec_json;

    nlohmann::json status_json;
    status.to_json(status_json);
    j["status"] = status_json;
}

void Pod::from_json(const nlohmann::json& j) {
    j.at("apiVersion").get_to(api_version);
    j.at("kind").get_to(kind);

    if (j.contains("metadata")) {
        metadata.from_json(j.at("metadata"));
    }

    if (j.contains("spec")) {
        spec.from_json(j.at("spec"));
    }

    if (j.contains("status")) {
        status.from_json(j.at("status"));
    }
}

// PodList实现
void PodList::to_json(nlohmann::json& j) const {
    j = nlohmann::json{
        {"apiVersion", api_version},
        {"kind", kind}
    };

    j["items"] = nlohmann::json::array();
    for (const auto& pod : items) {
        nlohmann::json pod_json;
        pod.to_json(pod_json);
        j["items"].push_back(pod_json);
    }
}

void PodList::from_json(const nlohmann::json& j) {
    j.at("apiVersion").get_to(api_version);
    j.at("kind").get_to(kind);

    if (j.contains("items")) {
        for (const auto& pod_json : j.at("items")) {
            Pod pod;
            pod.from_json(pod_json);
            items.push_back(pod);
        }
    }
}

// NodeSpec实现
void NodeSpec::to_json(nlohmann::json& j) const {
    j = nlohmann::json::object();

    if (!capacity.empty()) {
        j["capacity"] = nlohmann::json::object();
        for (const auto& [key, value] : capacity) {
            j["capacity"][key] = value.value;
        }
    }

    if (!pod_cidr.empty()) {
        j["podCIDR"] = pod_cidr;
    }
}

void NodeSpec::from_json(const nlohmann::json& j) {
    if (j.contains("capacity")) {
        for (const auto& [key, value] : j.at("capacity").items()) {
            capacity[key] = ResourceQuantity(value.get<std::string>());
        }
    }

    if (j.contains("podCIDR")) {
        j.at("podCIDR").get_to(pod_cidr);
    }
}

// NodeStatus实现
void NodeStatus::to_json(nlohmann::json& j) const {
    j = nlohmann::json{
        {"phase", phase_to_string(phase)}
    };

    if (!capacity.empty()) {
        j["capacity"] = nlohmann::json::object();
        for (const auto& [key, value] : capacity) {
            j["capacity"][key] = value.value;
        }
    }

    if (!allocatable.empty()) {
        j["allocatable"] = nlohmann::json::object();
        for (const auto& [key, value] : allocatable) {
            j["allocatable"][key] = value.value;
        }
    }

    if (!addresses.empty()) {
        j["addresses"] = nlohmann::json::array();
        for (const auto& addr : addresses) {
            j["addresses"].push_back({
                {"type", addr.type},
                {"address", addr.address}
            });
        }
    }
}

void NodeStatus::from_json(const nlohmann::json& j) {
    phase = string_to_node_phase(j.value("phase", "Pending"));

    if (j.contains("capacity")) {
        for (const auto& [key, value] : j.at("capacity").items()) {
            capacity[key] = ResourceQuantity(value.get<std::string>());
        }
    }

    if (j.contains("allocatable")) {
        for (const auto& [key, value] : j.at("allocatable").items()) {
            allocatable[key] = ResourceQuantity(value.get<std::string>());
        }
    }

    if (j.contains("addresses")) {
        for (const auto& addr_json : j.at("addresses")) {
            NodeAddress addr;
            addr.type = addr_json.at("type").get<std::string>();
            addr.address = addr_json.at("address").get<std::string>();
            addresses.push_back(addr);
        }
    }
}

// Node实现
void Node::to_json(nlohmann::json& j) const {
    j = nlohmann::json{
        {"apiVersion", api_version},
        {"kind", kind}
    };

    nlohmann::json metadata_json;
    metadata.to_json(metadata_json);
    j["metadata"] = metadata_json;

    nlohmann::json spec_json;
    spec.to_json(spec_json);
    j["spec"] = spec_json;

    nlohmann::json status_json;
    status.to_json(status_json);
    j["status"] = status_json;
}

void Node::from_json(const nlohmann::json& j) {
    j.at("apiVersion").get_to(api_version);
    j.at("kind").get_to(kind);

    if (j.contains("metadata")) {
        metadata.from_json(j.at("metadata"));
    }

    if (j.contains("spec")) {
        spec.from_json(j.at("spec"));
    }

    if (j.contains("status")) {
        status.from_json(j.at("status"));
    }
}

// NodeList实现
void NodeList::to_json(nlohmann::json& j) const {
    j = nlohmann::json{
        {"apiVersion", api_version},
        {"kind", kind}
    };

    j["items"] = nlohmann::json::array();
    for (const auto& node : items) {
        nlohmann::json node_json;
        node.to_json(node_json);
        j["items"].push_back(node_json);
    }
}

void NodeList::from_json(const nlohmann::json& j) {
    j.at("apiVersion").get_to(api_version);
    j.at("kind").get_to(kind);

    if (j.contains("items")) {
        for (const auto& node_json : j.at("items")) {
            Node node;
            node.from_json(node_json);
            items.push_back(node);
        }
    }
}

// ApiResponse实现
template<typename T>
void ApiResponse<T>::to_json(nlohmann::json& j) const {
    j = nlohmann::json{
        {"status", static_cast<int>(status)},
        {"message", message}
    };

    if (data) {
        if constexpr (std::is_same_v<T, Pod>) {
            nlohmann::json data_json;
            data->to_json(data_json);
            j["data"] = data_json;
        } else if constexpr (std::is_same_v<T, PodList>) {
            nlohmann::json data_json;
            data->to_json(data_json);
            j["data"] = data_json;
        } else if constexpr (std::is_same_v<T, Node>) {
            nlohmann::json data_json;
            data->to_json(data_json);
            j["data"] = data_json;
        } else if constexpr (std::is_same_v<T, NodeList>) {
            nlohmann::json data_json;
            data->to_json(data_json);
            j["data"] = data_json;
        }
    }
}

template<typename T>
void ApiResponse<T>::from_json(const nlohmann::json& j) {
    status = static_cast<ApiStatus>(j.at("status").get<int>());
    j.at("message").get_to(message);

    if (j.contains("data") && !j.at("data").is_null()) {
        if constexpr (std::is_same_v<T, Pod>) {
            T data_obj;
            data_obj.from_json(j.at("data"));
            data = data_obj;
        } else if constexpr (std::is_same_v<T, PodList>) {
            T data_obj;
            data_obj.from_json(j.at("data"));
            data = data_obj;
        } else if constexpr (std::is_same_v<T, Node>) {
            T data_obj;
            data_obj.from_json(j.at("data"));
            data = data_obj;
        } else if constexpr (std::is_same_v<T, NodeList>) {
            T data_obj;
            data_obj.from_json(j.at("data"));
            data = data_obj;
        }
    }
}

// 显式实例化模板
template class ApiResponse<Pod>;
template class ApiResponse<PodList>;
template class ApiResponse<Node>;
template class ApiResponse<NodeList>;

} // namespace k8s