#pragma once

#include <memory>
#include <string>
#include <functional>
#include <httplib.h>
#include "api/types.h"
#include "storage/storage.h"

namespace k8s {

// HTTP请求处理器
using RequestHandler = std::function<void(const httplib::Request&, httplib::Response&)>;

// API服务器
class HttpServer {
public:
    HttpServer(int port, std::shared_ptr<Storage> storage);
    ~HttpServer();

    // 服务器控制
    bool start();
    void stop();
    bool is_running() const;
    int get_port() const { return port_; }

private:
    // 路由注册
    void register_routes();

    // Pod API处理器
    void handle_create_pod(const httplib::Request& req, httplib::Response& res);
    void handle_get_pod(const httplib::Request& req, httplib::Response& res);
    void handle_list_pods(const httplib::Request& req, httplib::Response& res);
    void handle_update_pod(const httplib::Request& req, httplib::Response& res);
    void handle_delete_pod(const httplib::Request& req, httplib::Response& res);

    // Node API处理器
    void handle_create_node(const httplib::Request& req, httplib::Response& res);
    void handle_get_node(const httplib::Request& req, httplib::Response& res);
    void handle_list_nodes(const httplib::Request& req, httplib::Response& res);
    void handle_update_node(const httplib::Request& req, httplib::Response& res);
    void handle_delete_node(const httplib::Request& req, httplib::Response& res);

    // 工具方法
    void send_json_response(httplib::Response& res, const nlohmann::json& data, int status_code = 200);
    void send_error_response(httplib::Response& res, const std::string& message, int status_code = 400);
    bool validate_json_request(const httplib::Request& req, httplib::Response& res);

    // 成员变量
    int port_;
    std::shared_ptr<Storage> storage_;
    std::unique_ptr<httplib::Server> server_;
    std::atomic<bool> running_;
    std::thread server_thread_;
};

} // namespace k8s