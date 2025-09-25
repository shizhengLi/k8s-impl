#include "server/http_server.h"
#include <iostream>
#include <thread>

namespace k8s {

HttpServer::HttpServer(int port, std::shared_ptr<Storage> storage)
    : port_(port), storage_(storage), running_(false) {
    server_ = std::make_unique<httplib::Server>();
    register_routes();
}

HttpServer::~HttpServer() {
    stop();
}

bool HttpServer::start() {
    if (running_) {
        return true;
    }

    std::cout << "Starting HTTP server on port " << port_ << std::endl;

    // 在新线程中启动服务器
    server_thread_ = std::thread([this]() {
        running_ = true;
        server_->listen("0.0.0.0", port_);
        running_ = false;
    });

    // 等待服务器启动
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    return running_;
}

void HttpServer::stop() {
    if (!running_) {
        return;
    }

    std::cout << "Stopping HTTP server" << std::endl;
    server_->stop();

    if (server_thread_.joinable()) {
        server_thread_.join();
    }
}

bool HttpServer::is_running() const {
    return running_;
}

void HttpServer::register_routes() {
    // Pod API路由
    server_->Post("/api/v1/pods", [this](const httplib::Request& req, httplib::Response& res) {
        handle_create_pod(req, res);
    });

    server_->Get("/api/v1/pods", [this](const httplib::Request& req, httplib::Response& res) {
        handle_list_pods(req, res);
    });

    server_->Get("/api/v1/namespaces/([^/]+)/pods/([^/]+)", [this](const httplib::Request& req, httplib::Response& res) {
        handle_get_pod(req, res);
    });

    server_->Put("/api/v1/namespaces/([^/]+)/pods/([^/]+)", [this](const httplib::Request& req, httplib::Response& res) {
        handle_update_pod(req, res);
    });

    server_->Delete("/api/v1/namespaces/([^/]+)/pods/([^/]+)", [this](const httplib::Request& req, httplib::Response& res) {
        handle_delete_pod(req, res);
    });

    // Node API路由
    server_->Post("/api/v1/nodes", [this](const httplib::Request& req, httplib::Response& res) {
        handle_create_node(req, res);
    });

    server_->Get("/api/v1/nodes", [this](const httplib::Request& req, httplib::Response& res) {
        handle_list_nodes(req, res);
    });

    server_->Get("/api/v1/nodes/([^/]+)", [this](const httplib::Request& req, httplib::Response& res) {
        handle_get_node(req, res);
    });

    server_->Put("/api/v1/nodes/([^/]+)", [this](const httplib::Request& req, httplib::Response& res) {
        handle_update_node(req, res);
    });

    server_->Delete("/api/v1/nodes/([^/]+)", [this](const httplib::Request& req, httplib::Response& res) {
        handle_delete_node(req, res);
    });

    // 健康检查
    server_->Get("/health", [this](const httplib::Request& req, httplib::Response& res) {
        nlohmann::json health = {{"status", "ok"}};
        send_json_response(res, health);
    });
}

void HttpServer::handle_create_pod(const httplib::Request& req, httplib::Response& res) {
    if (!validate_json_request(req, res)) {
        return;
    }

    try {
        auto json_data = nlohmann::json::parse(req.body);
        Pod pod;
        pod.from_json(json_data);

        if (storage_->create_pod(pod)) {
            nlohmann::json response;
            pod.to_json(response);
            send_json_response(res, response, 201);
        } else {
            send_error_response(res, "Pod already exists", 409);
        }
    } catch (const std::exception& e) {
        send_error_response(res, std::string("Invalid JSON: ") + e.what());
    }
}

void HttpServer::handle_get_pod(const httplib::Request& req, httplib::Response& res) {
    std::string namespace_ = req.matches[1];
    std::string name = req.matches[2];

    auto pod = storage_->get_pod(name, namespace_);
    if (pod) {
        nlohmann::json response;
        pod->to_json(response);
        send_json_response(res, response);
    } else {
        send_error_response(res, "Pod not found", 404);
    }
}

void HttpServer::handle_list_pods(const httplib::Request& req, httplib::Response& res) {
    std::string namespace_ = req.get_param_value("namespace");
    auto pods = storage_->list_pods(namespace_);

    nlohmann::json response;
    response["apiVersion"] = "v1";
    response["kind"] = "PodList";
    response["items"] = nlohmann::json::array();

    for (const auto& pod : pods) {
        nlohmann::json pod_json;
        pod.to_json(pod_json);
        response["items"].push_back(pod_json);
    }

    send_json_response(res, response);
}

void HttpServer::handle_update_pod(const httplib::Request& req, httplib::Response& res) {
    std::string namespace_ = req.matches[1];
    std::string name = req.matches[2];

    if (!validate_json_request(req, res)) {
        return;
    }

    try {
        auto json_data = nlohmann::json::parse(req.body);
        Pod pod;
        pod.from_json(json_data);

        // 验证名称和命名空间匹配
        if (pod.metadata.name != name || pod.metadata.namespace_ != namespace_) {
            send_error_response(res, "Pod name or namespace does not match URL", 400);
            return;
        }

        if (storage_->update_pod(pod)) {
            nlohmann::json response;
            pod.to_json(response);
            send_json_response(res, response);
        } else {
            send_error_response(res, "Pod not found", 404);
        }
    } catch (const std::exception& e) {
        send_error_response(res, std::string("Invalid JSON: ") + e.what());
    }
}

void HttpServer::handle_delete_pod(const httplib::Request& req, httplib::Response& res) {
    std::string namespace_ = req.matches[1];
    std::string name = req.matches[2];

    if (storage_->delete_pod(name, namespace_)) {
        nlohmann::json response = {{"status", "success"}};
        send_json_response(res, response);
    } else {
        send_error_response(res, "Pod not found", 404);
    }
}

void HttpServer::handle_create_node(const httplib::Request& req, httplib::Response& res) {
    if (!validate_json_request(req, res)) {
        return;
    }

    try {
        auto json_data = nlohmann::json::parse(req.body);
        Node node;
        node.from_json(json_data);

        if (storage_->create_node(node)) {
            nlohmann::json response;
            node.to_json(response);
            send_json_response(res, response, 201);
        } else {
            send_error_response(res, "Node already exists", 409);
        }
    } catch (const std::exception& e) {
        send_error_response(res, std::string("Invalid JSON: ") + e.what());
    }
}

void HttpServer::handle_get_node(const httplib::Request& req, httplib::Response& res) {
    std::string name = req.matches[1];

    auto node = storage_->get_node(name);
    if (node) {
        nlohmann::json response;
        node->to_json(response);
        send_json_response(res, response);
    } else {
        send_error_response(res, "Node not found", 404);
    }
}

void HttpServer::handle_list_nodes(const httplib::Request& req, httplib::Response& res) {
    auto nodes = storage_->list_nodes();

    nlohmann::json response;
    response["apiVersion"] = "v1";
    response["kind"] = "NodeList";
    response["items"] = nlohmann::json::array();

    for (const auto& node : nodes) {
        nlohmann::json node_json;
        node.to_json(node_json);
        response["items"].push_back(node_json);
    }

    send_json_response(res, response);
}

void HttpServer::handle_update_node(const httplib::Request& req, httplib::Response& res) {
    std::string name = req.matches[1];

    if (!validate_json_request(req, res)) {
        return;
    }

    try {
        auto json_data = nlohmann::json::parse(req.body);
        Node node;
        node.from_json(json_data);

        // 验证名称匹配
        if (node.metadata.name != name) {
            send_error_response(res, "Node name does not match URL", 400);
            return;
        }

        if (storage_->update_node(node)) {
            nlohmann::json response;
            node.to_json(response);
            send_json_response(res, response);
        } else {
            send_error_response(res, "Node not found", 404);
        }
    } catch (const std::exception& e) {
        send_error_response(res, std::string("Invalid JSON: ") + e.what());
    }
}

void HttpServer::handle_delete_node(const httplib::Request& req, httplib::Response& res) {
    std::string name = req.matches[1];

    if (storage_->delete_node(name)) {
        nlohmann::json response = {{"status", "success"}};
        send_json_response(res, response);
    } else {
        send_error_response(res, "Node not found", 404);
    }
}

void HttpServer::send_json_response(httplib::Response& res, const nlohmann::json& data, int status_code) {
    res.status = status_code;
    res.set_header("Content-Type", "application/json");
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
    res.body = data.dump();
}

void HttpServer::send_error_response(httplib::Response& res, const std::string& message, int status_code) {
    nlohmann::json error = {{"error", message}};
    send_json_response(res, error, status_code);
}

bool HttpServer::validate_json_request(const httplib::Request& req, httplib::Response& res) {
    if (req.get_header_value("Content-Type").find("application/json") == std::string::npos) {
        send_error_response(res, "Content-Type must be application/json", 415);
        return false;
    }

    try {
        nlohmann::json::parse(req.body);
        return true;
    } catch (const std::exception& e) {
        send_error_response(res, std::string("Invalid JSON: ") + e.what());
        return false;
    }
}

} // namespace k8s