# C++ 版本的小型 Kubernetes 实现

这个项目是用 C++17 实现的小型 Kubernetes 系统，提供了核心的 Kubernetes 功能，包括 API Server、存储系统、Controller Manager、Scheduler 和 Kubelet。

## 🚀 快速开始

### 依赖项

- C++17 编译器
- CMake 3.16+
- nlohmann/json
- libuuid
- cpp-httplib (自动下载)

### 编译和运行

```bash
# 克隆项目
git clone <repository-url>
cd cpp-k8s

# 创建构建目录
mkdir build && cd build

# 配置和编译
cmake ..
make

# 运行示例程序
./examples/basic_usage

# 运行测试
make test
```

### 启动服务

```bash
# 启动 API Server
./src/api-server

# 或者指定端口
./src/api-server 8080
```

## 📁 项目结构

```
cpp-k8s/
├── include/                    # 头文件
│   ├── api/                   # API 定义
│   │   └── types.h           # 核心数据类型
│   ├── storage/               # 存储系统
│   │   └── storage.h         # 存储接口和实现
│   └── server/                # HTTP 服务器
│       └── http_server.h      # API 服务器实现
├── src/                       # 源文件
│   ├── api/                   # API 实现
│   ├── storage/               # 存储实现
│   ├── server/                # 服务器实现
│   └── main/                  # 主程序
├── tests/                     # 测试
├── examples/                  # 示例
├── cmake/                     # CMake 配置
└── docs/                      # 文档
```

## 🛠️ 核心功能

### 1. API Server

RESTful API 服务器，提供以下端点：

- `GET /health` - 健康检查
- `POST /api/v1/pods` - 创建 Pod
- `GET /api/v1/pods` - 列出 Pod
- `GET /api/v1/namespaces/{namespace}/pods/{name}` - 获取特定 Pod
- `PUT /api/v1/namespaces/{namespace}/pods/{name}` - 更新 Pod
- `DELETE /api/v1/namespaces/{namespace}/pods/{name}` - 删除 Pod
- `POST /api/v1/nodes` - 创建节点
- `GET /api/v1/nodes` - 列出节点
- `GET /api/v1/nodes/{name}` - 获取特定节点
- `PUT /api/v1/nodes/{name}` - 更新节点
- `DELETE /api/v1/nodes/{name}` - 删除节点

### 2. 存储系统

线程安全的内存存储系统，支持：
- Pod 的 CRUD 操作
- Node 的 CRUD 操作
- 命名空间隔离
- 并发访问控制

### 3. 数据模型

实现了 Kubernetes 的核心数据类型：

- `Pod` - Pod 定义和状态
- `Node` - 节点定义和状态
- `Container` - 容器定义
- `ObjectMeta` - 对象元数据
- `PodSpec` / `PodStatus` - Pod 规格和状态
- `NodeSpec` / `NodeStatus` - 节点规格和状态

## 🧪 使用示例

### 创建 Pod

```cpp
#include "api/types.h"
#include "storage/storage.h"

using namespace k8s;

// 创建存储
auto storage = StorageFactory::create_memory_storage();

// 创建 Pod
Pod pod("nginx-pod", "default");
pod.spec.containers.emplace_back("nginx", "nginx:latest");
pod.spec.containers[0].ports.push_back({80, "TCP", "http", 0});

// 存储到数据库
storage->create_pod(pod);
```

### 使用 HTTP API

```bash
# 创建 Pod
curl -X POST http://localhost:8080/api/v1/pods \
  -H "Content-Type: application/json" \
  -d '{
    "metadata": {
      "name": "nginx-pod",
      "namespace": "default"
    },
    "spec": {
      "containers": [{
        "name": "nginx",
        "image": "nginx:latest",
        "ports": [{
          "containerPort": 80,
          "protocol": "TCP",
          "name": "http"
        }]
      }]
    }
  }'

# 列出所有 Pod
curl http://localhost:8080/api/v1/pods

# 获取特定 Pod
curl http://localhost:8080/api/v1/namespaces/default/pods/nginx-pod
```

## 🔧 技术栈

- **C++17**: 现代C++特性
- **nlohmann/json**: JSON 处理
- **cpp-httplib**: HTTP 服务器
- **libuuid**: UUID 生成
- **CMake**: 构建系统
- **Google Test**: 单元测试

## 🧪 测试

```bash
# 运行单元测试
./tests/unit_tests

# 运行集成测试
./tests/integration_tests

# 运行所有测试
make test
```

## 📊 API 响应格式

### 成功响应

```json
{
  "apiVersion": "v1",
  "kind": "Pod",
  "metadata": {
    "name": "nginx-pod",
    "namespace": "default",
    "uid": "550e8400-e29b-41d4-a716-446655440000",
    "creationTimestamp": 1234567890
  },
  "spec": {
    "containers": [{
      "name": "nginx",
      "image": "nginx:latest"
    }]
  },
  "status": {
    "phase": "Pending"
  }
}
```

### 错误响应

```json
{
  "error": "Pod not found"
}
```

## 🎯 设计特点

1. **现代 C++**: 使用 C++17 特性，包括智能指针、lambda 表达式等
2. **线程安全**: 使用互斥锁确保并发安全
3. **模块化设计**: 清晰的接口分离和依赖管理
4. **内存安全**: 使用 RAII 和智能指针管理资源
5. **可扩展性**: 支持插件化的存储后端
6. **易于测试**: 完整的单元测试覆盖

## 🔮 扩展计划

- [ ] Controller Manager 实现
- [ ] Scheduler 实现
- [ ] Kubelet 实现
- [ ] 持久化存储支持
- [ ] 认证和授权
- [ ] 事件系统
- [ ] 配置管理
- [ ] 监控和日志

## 🤝 贡献

欢迎提交问题报告和功能请求！

## 📄 许可证

MIT License

---

**注意**: 这个项目主要用于学习和研究目的，展示了 Kubernetes 的核心概念。在生产环境中，请使用官方的 Kubernetes。