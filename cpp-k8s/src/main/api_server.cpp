#include "../server/http_server.h"
#include "../storage/storage.h"
#include <iostream>
#include <csignal>

using namespace k8s;

std::unique_ptr<HttpServer> server;

void signal_handler(int signal) {
    std::cout << "\nReceived signal " << signal << ", shutting down..." << std::endl;
    if (server) {
        server->stop();
    }
    exit(0);
}

int main(int argc, char* argv[]) {
    // 设置信号处理
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // 解析命令行参数
    int port = 8080;
    if (argc > 1) {
        try {
            port = std::stoi(argv[1]);
        } catch (const std::exception& e) {
            std::cerr << "Invalid port number: " << argv[1] << std::endl;
            return 1;
        }
    }

    std::cout << "Starting C++ Kubernetes API Server..." << std::endl;
    std::cout << "Port: " << port << std::endl;

    // 创建存储
    auto storage = StorageFactory::create_memory_storage();

    // 创建服务器
    server = std::make_unique<HttpServer>(port, storage);

    // 启动服务器
    if (!server->start()) {
        std::cerr << "Failed to start server" << std::endl;
        return 1;
    }

    std::cout << "Server started successfully" << std::endl;
    std::cout << "API endpoints:" << std::endl;
    std::cout << "  GET  /health" << std::endl;
    std::cout << "  POST /api/v1/pods" << std::endl;
    std::cout << "  GET  /api/v1/pods" << std::endl;
    std::cout << "  GET  /api/v1/namespaces/{namespace}/pods/{name}" << std::endl;
    std::cout << "  PUT  /api/v1/namespaces/{namespace}/pods/{name}" << std::endl;
    std::cout << "  DELETE /api/v1/namespaces/{namespace}/pods/{name}" << std::endl;
    std::cout << "  POST /api/v1/nodes" << std::endl;
    std::cout << "  GET  /api/v1/nodes" << std::endl;
    std::cout << "  GET  /api/v1/nodes/{name}" << std::endl;
    std::cout << "  PUT  /api/v1/nodes/{name}" << std::endl;
    std::cout << "  DELETE /api/v1/nodes/{name}" << std::endl;
    std::cout << "\nPress Ctrl+C to stop the server" << std::endl;

    // 等待服务器结束
    while (server->is_running()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}