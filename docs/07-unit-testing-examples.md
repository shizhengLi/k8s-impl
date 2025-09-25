# Kubernetes 单元测试示例

## 概述

本文档提供Kubernetes核心组件的单元测试示例，展示如何编写高质量的测试代码，包括测试框架、模拟对象、测试策略等内容。

## 测试框架和工具

### Go测试框架

```go
// 测试框架导入
import (
    "testing"
    "time"

    "github.com/stretchr/testify/assert"
    "github.com/stretchr/testify/mock"
    "github.com/stretchr/testify/require"

    "k8s.io/apimachinery/pkg/runtime"
    "k8s.io/client-go/kubernetes/fake"
    "k8s.io/client-go/tools/cache"
)
```

### 测试辅助函数

```go
// pkg/testing/helpers.go
package testing

import (
    "fmt"
    "reflect"
    "testing"

    "github.com/stretchr/testify/assert"
)

// AssertEqualDeep 使用反射深度比较两个对象是否相等
func AssertEqualDeep(t *testing.T, expected, actual interface{}, msgAndArgs ...interface{}) {
    if !reflect.DeepEqual(expected, actual) {
        assert.Fail(t, fmt.Sprintf("Not equal: \nexpected: %v\nactual: %v", expected, actual), msgAndArgs...)
    }
}

// AssertErrorType 检查错误类型
func AssertErrorType(t *testing.T, expectedType, actual error, msgAndArgs ...interface{}) {
    if expectedType != nil && actual != nil {
        if reflect.TypeOf(expectedType) != reflect.TypeOf(actual) {
            assert.Fail(t, fmt.Sprintf("Error type mismatch: expected %T, got %T", expectedType, actual), msgAndArgs...)
        }
    }
}

// Eventually 最终一致性检查
func Eventually(t *testing.T, condition func() bool, timeout time.Duration, interval time.Duration, msg string) {
    timeoutCh := time.After(timeout)
    ticker := time.NewTicker(interval)
    defer ticker.Stop()

    for {
        select {
        case <-timeoutCh:
            assert.Fail(t, fmt.Sprintf("Condition not met within timeout: %s", msg))
            return
        case <-ticker.C:
            if condition() {
                return
            }
        }
    }
}
```

## API Server测试

### 存储层测试

```go
// pkg/storage/memory_storage_test.go
package storage

import (
    "testing"

    "github.com/simple-k8s/pkg/api"
    "github.com/stretchr/testify/assert"
    "github.com/stretchr/testify/require"
)

func TestMemoryStorage_CreatePod(t *testing.T) {
    // 准备测试数据
    storage := NewMemoryStorage()
    pod := &api.Pod{
        ObjectMeta: api.ObjectMeta{
            Name:      "test-pod",
            Namespace: "default",
        },
        Spec: api.PodSpec{
            Containers: []api.Container{
                {
                    Name:  "nginx",
                    Image: "nginx:latest",
                },
            },
        },
    }

    // 执行测试
    err := storage.CreatePod(pod)

    // 验证结果
    require.NoError(t, err)

    // 验证Pod可以被获取
    retrieved, err := storage.GetPod("default", "test-pod")
    require.NoError(t, err)
    assert.Equal(t, pod.Name, retrieved.Name)
    assert.Equal(t, pod.Namespace, retrieved.Namespace)
}

func TestMemoryStorage_CreatePod_Duplicate(t *testing.T) {
    storage := NewMemoryStorage()
    pod := &api.Pod{
        ObjectMeta: api.ObjectMeta{
            Name:      "test-pod",
            Namespace: "default",
        },
    }

    // 创建第一个Pod
    err := storage.CreatePod(pod)
    require.NoError(t, err)

    // 尝试创建重复的Pod
    err = storage.CreatePod(pod)
    assert.Error(t, err)
    assert.Contains(t, err.Error(), "already exists")
}

func TestMemoryStorage_GetPod_NotFound(t *testing.T) {
    storage := NewMemoryStorage()

    _, err := storage.GetPod("default", "non-existent")
    assert.Error(t, err)
    assert.Contains(t, err.Error(), "not found")
}

func TestMemoryStorage_UpdatePod(t *testing.T) {
    storage := NewMemoryStorage()
    pod := &api.Pod{
        ObjectMeta: api.ObjectMeta{
            Name:      "test-pod",
            Namespace: "default",
        },
        Spec: api.PodSpec{
            Containers: []api.Container{
                {
                    Name:  "nginx",
                    Image: "nginx:1.19",
                },
            },
        },
    }

    // 创建Pod
    err := storage.CreatePod(pod)
    require.NoError(t, err)

    // 更新Pod
    pod.Spec.Containers[0].Image = "nginx:1.20"
    err = storage.UpdatePod(pod)
    require.NoError(t, err)

    // 验证更新
    retrieved, err := storage.GetPod("default", "test-pod")
    require.NoError(t, err)
    assert.Equal(t, "nginx:1.20", retrieved.Spec.Containers[0].Image)
}

func TestMemoryStorage_DeletePod(t *testing.T) {
    storage := NewMemoryStorage()
    pod := &api.Pod{
        ObjectMeta: api.ObjectMeta{
            Name:      "test-pod",
            Namespace: "default",
        },
    }

    // 创建Pod
    err := storage.CreatePod(pod)
    require.NoError(t, err)

    // 删除Pod
    err = storage.DeletePod("default", "test-pod")
    require.NoError(t, err)

    // 验证Pod已删除
    _, err = storage.GetPod("default", "test-pod")
    assert.Error(t, err)
    assert.Contains(t, err.Error(), "not found")
}

func TestMemoryStorage_ListPods(t *testing.T) {
    storage := NewMemoryStorage()

    // 创建多个Pod
    pods := []*api.Pod{
        {
            ObjectMeta: api.ObjectMeta{
                Name:      "pod1",
                Namespace: "default",
            },
        },
        {
            ObjectMeta: api.ObjectMeta{
                Name:      "pod2",
                Namespace: "default",
            },
        },
    }

    for _, pod := range pods {
        err := storage.CreatePod(pod)
        require.NoError(t, err)
    }

    // 列出所有Pod
    retrieved, err := storage.ListPods()
    require.NoError(t, err)
    assert.Len(t, retrieved, 2)
}
```

### API处理器测试

```go
// cmd/simple-apiserver/handlers_test.go
package main

import (
    "bytes"
    "encoding/json"
    "net/http"
    "net/http/httptest"
    "testing"

    "github.com/gorilla/mux"
    "github.com/simple-k8s/pkg/api"
    "github.com/simple-k8s/pkg/storage"
    "github.com/stretchr/testify/assert"
    "github.com/stretchr/testify/require"
)

func TestAPIServer_CreatePod(t *testing.T) {
    // 准备测试环境
    storage := storage.NewMemoryStorage()
    server := NewAPIServer("8080", storage)

    // 准备测试数据
    pod := &api.Pod{
        ObjectMeta: api.ObjectMeta{
            Name:      "test-pod",
            Namespace: "default",
        },
        Spec: api.PodSpec{
            Containers: []api.Container{
                {
                    Name:  "nginx",
                    Image: "nginx:latest",
                },
            },
        },
    }

    // 序列化请求数据
    requestBody, err := json.Marshal(pod)
    require.NoError(t, err)

    // 创建HTTP请求
    req, err := http.NewRequest("POST", "/api/v1/pods", bytes.NewBuffer(requestBody))
    require.NoError(t, err)
    req.Header.Set("Content-Type", "application/json")

    // 记录响应
    rr := httptest.NewRecorder()

    // 调用处理器
    server.handlePods(rr, req)

    // 验证响应
    assert.Equal(t, http.StatusCreated, rr.Code)

    // 验证响应体
    var responsePod api.Pod
    err = json.Unmarshal(rr.Body.Bytes(), &responsePod)
    require.NoError(t, err)
    assert.Equal(t, pod.Name, responsePod.Name)
    assert.Equal(t, pod.Namespace, responsePod.Namespace)
}

func TestAPIServer_GetPod(t *testing.T) {
    storage := storage.NewMemoryStorage()
    server := NewAPIServer("8080", storage)

    // 预先创建Pod
    pod := &api.Pod{
        ObjectMeta: api.ObjectMeta{
            Name:      "test-pod",
            Namespace: "default",
        },
    }
    err := storage.CreatePod(pod)
    require.NoError(t, err)

    // 创建HTTP请求
    req, err := http.NewRequest("GET", "/api/v1/pods/default/test-pod", nil)
    require.NoError(t, err)

    // 设置路由变量
    req = mux.SetURLVars(req, map[string]string{
        "namespace": "default",
        "name":      "test-pod",
    })

    // 记录响应
    rr := httptest.NewRecorder()

    // 调用处理器
    server.handlePod(rr, req)

    // 验证响应
    assert.Equal(t, http.StatusOK, rr.Code)

    // 验证响应体
    var responsePod api.Pod
    err = json.Unmarshal(rr.Body.Bytes(), &responsePod)
    require.NoError(t, err)
    assert.Equal(t, pod.Name, responsePod.Name)
}

func TestAPIServer_GetPod_NotFound(t *testing.T) {
    storage := storage.NewMemoryStorage()
    server := NewAPIServer("8080", storage)

    // 创建HTTP请求
    req, err := http.NewRequest("GET", "/api/v1/pods/default/non-existent", nil)
    require.NoError(t, err)

    // 设置路由变量
    req = mux.SetURLVars(req, map[string]string{
        "namespace": "default",
        "name":      "non-existent",
    })

    // 记录响应
    rr := httptest.NewRecorder()

    // 调用处理器
    server.handlePod(rr, req)

    // 验证响应
    assert.Equal(t, http.StatusNotFound, rr.Code)
}
```

## Controller测试

### Mock对象实现

```go
// pkg/controller/mocks/mock_client.go
package mocks

import (
    "github.com/stretchr/testify/mock"

    "github.com/simple-k8s/pkg/api"
)

// MockClient 模拟客户端
type MockClient struct {
    mock.Mock
}

func (m *MockClient) CreatePod(pod *api.Pod) error {
    args := m.Called(pod)
    return args.Error(0)
}

func (m *MockClient) GetPod(namespace, name string) (*api.Pod, error) {
    args := m.Called(namespace, name)
    return args.Get(0).(*api.Pod), args.Error(1)
}

func (m *MockClient) UpdatePod(pod *api.Pod) error {
    args := m.Called(pod)
    return args.Error(0)
}

func (m *MockClient) DeletePod(namespace, name string) error {
    args := m.Called(namespace, name)
    return args.Error(0)
}

func (m *MockClient) ListPods() ([]*api.Pod, error) {
    args := m.Called()
    return args.Get(0).([]*api.Pod), args.Error(1)
}

func (m *MockClient) CreateNode(node *api.Node) error {
    args := m.Called(node)
    return args.Error(0)
}

func (m *MockClient) GetNode(name string) (*api.Node, error) {
    args := m.Called(name)
    return args.Get(0).(*api.Node), args.Error(1)
}

func (m *MockClient) UpdateNode(node *api.Node) error {
    args := m.Called(node)
    return args.Error(0)
}

func (m *MockClient) DeleteNode(name string) error {
    args := m.Called(name)
    return args.Error(0)
}

func (m *MockClient) ListNodes() ([]*api.Node, error) {
    args := m.Called()
    return args.Get(0).([]*api.Node), args.Error(1)
}
```

### Pod控制器测试

```go
// pkg/controller/pod_controller_test.go
package controller

import (
    "context"
    "testing"
    "time"

    "github.com/simple-k8s/pkg/api"
    "github.com/simple-k8s/pkg/controller/mocks"
    "github.com/stretchr/testify/assert"
    "github.com/stretchr/testify/mock"
    "github.com/stretchr/testify/require"
)

func TestPodController_ReconcilePod_PendingToRunning(t *testing.T) {
    // 创建Mock客户端
    mockClient := &mocks.MockClient{}

    // 准备测试数据
    pod := &api.Pod{
        ObjectMeta: api.ObjectMeta{
            Name:      "test-pod",
            Namespace: "default",
        },
        Spec: api.PodSpec{
            NodeName: "node1",
            Containers: []api.Container{
                {
                    Name:  "nginx",
                    Image: "nginx:latest",
                },
            },
        },
        Status: api.PodStatus{
            Phase: api.PodPending,
        },
    }

    // 设置Mock期望
    mockClient.On("GetPod", "default", "test-pod").Return(pod, nil)
    mockClient.On("UpdatePod", mock.MatchedBy(func(updatedPod *api.Pod) bool {
        return updatedPod.Status.Phase == api.PodRunning
    })).Return(nil)

    // 创建控制器
    controller := NewPodController(mockClient)

    // 执行测试
    controller.reconcilePod(context.Background(), "default/test-pod")

    // 验证Mock调用
    mockClient.AssertExpectations(t)
}

func TestPodController_ReconcilePod_AlreadyRunning(t *testing.T) {
    mockClient := &mocks.MockClient{}

    pod := &api.Pod{
        ObjectMeta: api.ObjectMeta{
            Name:      "test-pod",
            Namespace: "default",
        },
        Status: api.PodStatus{
            Phase: api.PodRunning,
        },
    }

    // 设置Mock期望：只调用GetPod，不调用UpdatePod
    mockClient.On("GetPod", "default", "test-pod").Return(pod, nil)

    controller := NewPodController(mockClient)
    controller.reconcilePod(context.Background(), "default/test-pod")

    // 验证只有GetPod被调用
    mockClient.AssertCalled(t, "GetPod", "default", "test-pod")
    mockClient.AssertNotCalled(t, "UpdatePod", mock.AnythingOfType("*api.Pod"))
}

func TestPodController_ReconcilePod_GetError(t *testing.T) {
    mockClient := &mocks.MockClient{}

    // 设置Mock返回错误
    mockClient.On("GetPod", "default", "test-pod").Return((*api.Pod)(nil), assert.AnError)

    controller := NewPodController(mockClient)
    controller.reconcilePod(context.Background(), "default/test-pod")

    // 验证只调用了GetPod
    mockClient.AssertCalled(t, "GetPod", "default", "test-pod")
    mockClient.AssertNotCalled(t, "UpdatePod", mock.AnythingOfType("*api.Pod"))
}

func TestPodController_WatchPods(t *testing.T) {
    mockClient := &mocks.MockClient{}

    // 模拟返回的Pod列表
    pods := []*api.Pod{
        {
            ObjectMeta: api.ObjectMeta{
                Name:      "pod1",
                Namespace: "default",
            },
            Status: api.PodStatus{
                Phase: api.PodPending,
            },
        },
        {
            ObjectMeta: api.ObjectMeta{
                Name:      "pod2",
                Namespace: "default",
            },
            Status: api.PodStatus{
                Phase: api.PodRunning,
            },
        },
    }

    mockClient.On("ListPods").Return(pods, nil)

    controller := NewPodController(mockClient)

    // 创建测试上下文
    ctx, cancel := context.WithTimeout(context.Background(), 100*time.Millisecond)
    defer cancel()

    // 启动Pod监听
    go controller.watchPods(ctx)

    // 等待处理完成
    time.Sleep(50 * time.Millisecond)

    // 验证ListPods被调用
    mockClient.AssertCalled(t, "ListPods")
}
```

## Scheduler测试

### 调度器测试

```go
// pkg/scheduler/scheduler_test.go
package scheduler

import (
    "context"
    "testing"
    "time"

    "github.com/simple-k8s/pkg/api"
    "github.com/simple-k8s/pkg/controller/mocks"
    "github.com/stretchr/testify/assert"
    "github.com/stretchr/testify/mock"
    "github.com/stretchr/testify/require"
)

func TestScheduler_SchedulePod_Success(t *testing.T) {
    mockClient := &mocks.MockClient{}

    // 准备测试数据
    pod := &api.Pod{
        ObjectMeta: api.ObjectMeta{
            Name:      "test-pod",
            Namespace: "default",
        },
        Spec: api.PodSpec{
            Containers: []api.Container{
                {
                    Name:  "nginx",
                    Image: "nginx:latest",
                },
            },
        },
        Status: api.PodStatus{
            Phase: api.PodPending,
        },
    }

    nodes := []*api.Node{
        {
            ObjectMeta: api.ObjectMeta{
                Name: "node1",
            },
            Status: api.NodeStatus{
                Phase: api.NodeRunning,
            },
        },
        {
            ObjectMeta: api.ObjectMeta{
                Name: "node2",
            },
            Status: api.NodeStatus{
                Phase: api.NodeRunning,
            },
        },
    }

    // 设置Mock期望
    mockClient.On("ListNodes").Return(nodes, nil)
    mockClient.On("UpdatePod", mock.MatchedBy(func(updatedPod *api.Pod) bool {
        return updatedPod.Spec.NodeName != "" && updatedPod.Status.Phase == api.PodRunning
    })).Return(nil)

    // 创建调度器
    scheduler := NewScheduler(mockClient)

    // 执行调度
    scheduler.schedulePod(context.Background(), pod)

    // 验证Mock调用
    mockClient.AssertExpectations(t)
}

func TestScheduler_SelectNode(t *testing.T) {
    mockClient := &mocks.MockClient{}
    scheduler := NewScheduler(mockClient)

    // 准备测试数据
    pod := &api.Pod{
        ObjectMeta: api.ObjectMeta{
            Name:      "test-pod",
            Namespace: "default",
        },
    }

    nodes := []*api.Node{
        {
            ObjectMeta: api.ObjectMeta{
                Name: "node1",
            },
            Status: api.NodeStatus{
                Phase: api.NodeRunning,
            },
        },
        {
            ObjectMeta: api.ObjectMeta{
                Name: "node2",
            },
            Status: api.NodeStatus{
                Phase: api.NodePending, // 这个节点不应该被选中
            },
        },
        {
            ObjectMeta: api.ObjectMeta{
                Name: "node3",
            },
            Status: api.NodeStatus{
                Phase: api.NodeRunning,
            },
        },
    }

    // 执行节点选择
    selectedNode := scheduler.selectNode(nodes, pod)

    // 验证结果
    require.NotNil(t, selectedNode)
    assert.Contains(t, []string{"node1", "node3"}, selectedNode.Name)
    assert.NotEqual(t, "node2", selectedNode.Name)
}

func TestScheduler_IsNodeSuitable(t *testing.T) {
    mockClient := &mocks.MockClient{}
    scheduler := NewScheduler(mockClient)

    pod := &api.Pod{
        ObjectMeta: api.ObjectMeta{
            Name:      "test-pod",
            Namespace: "default",
        },
    }

    // 测试适合的节点
    suitableNode := &api.Node{
        ObjectMeta: api.ObjectMeta{
            Name: "node1",
        },
        Status: api.NodeStatus{
            Phase: api.NodeRunning,
        },
    }

    assert.True(t, scheduler.isNodeSuitable(suitableNode, pod))

    // 测试不适合的节点
    unsuitableNode := &api.Node{
        ObjectMeta: api.ObjectMeta{
            Name: "node2",
        },
        Status: api.NodeStatus{
            Phase: api.NodePending,
        },
    }

    assert.False(t, scheduler.isNodeSuitable(unsuitableNode, pod))
}

func TestScheduler_SelectNode_NoSuitableNodes(t *testing.T) {
    mockClient := &mocks.MockClient{}
    scheduler := NewScheduler(mockClient)

    pod := &api.Pod{
        ObjectMeta: api.ObjectMeta{
            Name:      "test-pod",
            Namespace: "default",
        },
    }

    // 所有节点都不适合
    nodes := []*api.Node{
        {
            ObjectMeta: api.ObjectMeta{
                Name: "node1",
            },
            Status: api.NodeStatus{
                Phase: api.NodePending,
            },
        },
        {
            ObjectMeta: api.ObjectMeta{
                Name: "node2",
            },
            Status: api.NodeStatus{
                Phase: api.NodePending,
            },
        },
    }

    // 执行节点选择
    selectedNode := scheduler.selectNode(nodes, pod)

    // 验证没有节点被选中
    assert.Nil(t, selectedNode)
}
```

## Kubelet测试

### Kubelet测试

```go
// pkg/kubelet/kubelet_test.go
package kubelet

import (
    "context"
    "testing"

    "github.com/simple-k8s/pkg/api"
    "github.com/simple-k8s/pkg/controller/mocks"
    "github.com/stretchr/testify/assert"
    "github.com/stretchr/testify/mock"
    "github.com/stretchr/testify/require"
)

func TestKubelet_SyncPod_StartPod(t *testing.T) {
    mockClient := &mocks.MockClient{}
    kubelet := NewKubelet(mockClient, "node1")

    // 准备测试数据
    pod := &api.Pod{
        ObjectMeta: api.ObjectMeta{
            Name:      "test-pod",
            Namespace: "default",
        },
        Spec: api.PodSpec{
            NodeName: "node1",
            Containers: []api.Container{
                {
                    Name:  "nginx",
                    Image: "nginx:latest",
                },
            },
        },
        Status: api.PodStatus{
            Phase: api.PodPending,
        },
    }

    // 设置Mock期望
    mockClient.On("UpdatePod", mock.MatchedBy(func(updatedPod *api.Pod) bool {
        return updatedPod.Status.Phase == api.PodRunning
    })).Return(nil)

    // 执行同步
    err := kubelet.syncPod(pod)
    require.NoError(t, err)

    // 验证Mock调用
    mockClient.AssertExpectations(t)
}

func TestKubelet_SyncPod_StopPod(t *testing.T) {
    mockClient := &mocks.MockClient{}
    kubelet := NewKubelet(mockClient, "node1")

    // 准备测试数据 - 有删除时间戳的Pod
    pod := &api.Pod{
        ObjectMeta: api.ObjectMeta{
            Name:              "test-pod",
            Namespace:         "default",
            DeletionTimestamp: &api.Time{},
        },
        Spec: api.PodSpec{
            NodeName: "node1",
            Containers: []api.Container{
                {
                    Name:  "nginx",
                    Image: "nginx:latest",
                },
            },
        },
    }

    // 设置Mock期望
    mockClient.On("UpdatePod", mock.MatchedBy(func(updatedPod *api.Pod) bool {
        return updatedPod.Status.Phase == api.PodFailed
    })).Return(nil)

    // 执行同步
    err := kubelet.syncPod(pod)
    require.NoError(t, err)

    // 验证Mock调用
    mockClient.AssertExpectations(t)
}

func TestKubelet_SyncPod_WrongNode(t *testing.T) {
    mockClient := &mocks.MockClient{}
    kubelet := NewKubelet(mockClient, "node1")

    // 准备测试数据 - 分配给其他节点的Pod
    pod := &api.Pod{
        ObjectMeta: api.ObjectMeta{
            Name:      "test-pod",
            Namespace: "default",
        },
        Spec: api.PodSpec{
            NodeName: "node2", // 不同节点
        },
    }

    // 执行同步
    err := kubelet.syncPod(pod)
    require.NoError(t, err)

    // 验证没有调用UpdatePod
    mockClient.AssertNotCalled(t, "UpdatePod", mock.AnythingOfType("*api.Pod"))
}

func TestKubelet_GetContainerName(t *testing.T) {
    mockClient := &mocks.MockClient{}
    kubelet := NewKubelet(mockClient, "node1")

    pod := &api.Pod{
        ObjectMeta: api.ObjectMeta{
            Name:      "test-pod",
            Namespace: "default",
            UID:       "test-uid-12345",
        },
    }

    container := api.Container{
        Name:  "nginx",
        Image: "nginx:latest",
    }

    // 执行测试
    containerName := kubelet.getContainerName(pod, container)

    // 验证结果
    expectedName := "k8s_test-pod_nginx_test-uid"
    assert.Equal(t, expectedName, containerName)
}
```

## 集成测试工具

### 测试服务器

```go
// pkg/testing/test_server.go
package testing

import (
    "context"
    "net/http"
    "net/http/httptest"
    "sync"
    "time"

    "github.com/simple-k8s/pkg/api"
    "github.com/simple-k8s/pkg/storage"
)

// TestServer 集成测试服务器
type TestServer struct {
    server      *httptest.Server
    storage     storage.Storage
    apiServer   *APIServer
    mu          sync.Mutex
    shutdown    chan struct{}
}

// NewTestServer 创建新的测试服务器
func NewTestServer() *TestServer {
    storage := storage.NewMemoryStorage()
    apiServer := NewAPIServer("0", storage)

    return &TestServer{
        storage:   storage,
        apiServer: apiServer,
        shutdown:  make(chan struct{}),
    }
}

// Start 启动测试服务器
func (ts *TestServer) Start() {
    ts.mu.Lock()
    defer ts.mu.Unlock()

    if ts.server != nil {
        return
    }

    mux := http.NewServeMux()

    // 注册路由
    mux.HandleFunc("/api/v1/pods", ts.apiServer.handlePods)
    mux.HandleFunc("/api/v1/pods/", ts.apiServer.handlePod)
    mux.HandleFunc("/api/v1/nodes", ts.apiServer.handleNodes)
    mux.HandleFunc("/api/v1/nodes/", ts.apiServer.handleNode)
    mux.HandleFunc("/healthz", ts.apiServer.handleHealth)

    ts.server = httptest.NewServer(mux)
}

// Stop 停止测试服务器
func (ts *TestServer) Stop() {
    ts.mu.Lock()
    defer ts.mu.Unlock()

    if ts.server == nil {
        return
    }

    close(ts.shutdown)
    ts.server.Close()
    ts.server = nil
}

// URL 返回服务器URL
func (ts *TestServer) URL() string {
    ts.mu.Lock()
    defer ts.mu.Unlock()

    if ts.server == nil {
        return ""
    }

    return ts.server.URL
}

// Storage 返回存储实例
func (ts *TestServer) Storage() storage.Storage {
    return ts.storage
}

// CreatePod 创建Pod的便捷方法
func (ts *TestServer) CreatePod(pod *api.Pod) error {
    return ts.storage.CreatePod(pod)
}

// GetPod 获取Pod的便捷方法
func (ts *TestServer) GetPod(namespace, name string) (*api.Pod, error) {
    return ts.storage.GetPod(namespace, name)
}

// WaitForPodCondition 等待Pod满足条件
func (ts *TestServer) WaitForPodCondition(namespace, name string, condition func(*api.Pod) bool, timeout time.Duration) (*api.Pod, error) {
    ctx, cancel := context.WithTimeout(context.Background(), timeout)
    defer cancel()

    ticker := time.NewTicker(100 * time.Millisecond)
    defer ticker.Stop()

    for {
        select {
        case <-ctx.Done():
            return nil, ctx.Err()
        case <-ticker.C:
            pod, err := ts.GetPod(namespace, name)
            if err != nil {
                continue
            }
            if condition(pod) {
                return pod, nil
            }
        }
    }
}
```

### 集成测试示例

```go
// pkg/integration/integration_test.go
package integration

import (
    "net/http"
    "testing"
    "time"

    "github.com/simple-k8s/pkg/api"
    "github.com/simple-k8s/pkg/testing"
    "github.com/stretchr/testify/assert"
    "github.com/stretchr/testify/require"
)

func TestIntegration_PodLifecycle(t *testing.T) {
    // 创建测试服务器
    testServer := testing.NewTestServer()
    testServer.Start()
    defer testServer.Stop()

    // 创建客户端
    client := NewClient(testServer.URL())

    // 准备测试数据
    pod := &api.Pod{
        ObjectMeta: api.ObjectMeta{
            Name:      "integration-test-pod",
            Namespace: "default",
        },
        Spec: api.PodSpec{
            Containers: []api.Container{
                {
                    Name:  "nginx",
                    Image: "nginx:latest",
                },
            },
        },
    }

    // 1. 创建Pod
    err := client.CreatePod(pod)
    require.NoError(t, err)

    // 2. 验证Pod可以获取
    retrieved, err := client.GetPod("default", "integration-test-pod")
    require.NoError(t, err)
    assert.Equal(t, pod.Name, retrieved.Name)

    // 3. 更新Pod
    retrieved.Spec.NodeName = "node1"
    err = client.UpdatePod(retrieved)
    require.NoError(t, err)

    // 4. 验证更新
    updated, err := client.GetPod("default", "integration-test-pod")
    require.NoError(t, err)
    assert.Equal(t, "node1", updated.Spec.NodeName)

    // 5. 删除Pod
    err = client.DeletePod("default", "integration-test-pod")
    require.NoError(t, err)

    // 6. 验证Pod已删除
    _, err = client.GetPod("default", "integration-test-pod")
    assert.Error(t, err)
}

func TestIntegration_HTTPAPIServer(t *testing.T) {
    testServer := testing.NewTestServer()
    testServer.Start()
    defer testServer.Stop()

    // 测试健康检查
    resp, err := http.Get(testServer.URL() + "/healthz")
    require.NoError(t, err)
    defer resp.Body.Close()
    assert.Equal(t, http.StatusOK, resp.StatusCode)

    // 测试创建Pod通过HTTP
    pod := &api.Pod{
        ObjectMeta: api.ObjectMeta{
            Name:      "http-test-pod",
            Namespace: "default",
        },
        Spec: api.PodSpec{
            Containers: []api.Container{
                {
                    Name:  "nginx",
                    Image: "nginx:latest",
                },
            },
        },
    }

    // 这里可以添加HTTP请求测试
    // 使用标准的HTTP客户端测试API
}

func TestIntegration_ConcurrentOperations(t *testing.T) {
    testServer := testing.NewTestServer()
    testServer.Start()
    defer testServer.Stop()

    client := NewClient(testServer.URL())

    // 并发创建多个Pod
    const numPods = 10
    var wg sync.WaitGroup
    errs := make(chan error, numPods)

    for i := 0; i < numPods; i++ {
        wg.Add(1)
        go func(i int) {
            defer wg.Done()

            pod := &api.Pod{
                ObjectMeta: api.ObjectMeta{
                    Name:      fmt.Sprintf("concurrent-pod-%d", i),
                    Namespace: "default",
                },
                Spec: api.PodSpec{
                    Containers: []api.Container{
                        {
                            Name:  fmt.Sprintf("container-%d", i),
                            Image: "nginx:latest",
                        },
                    },
                },
            }

            if err := client.CreatePod(pod); err != nil {
                errs <- err
            }
        }(i)
    }

    wg.Wait()
    close(errs)

    // 检查错误
    for err := range errs {
        require.NoError(t, err)
    }

    // 验证所有Pod都被创建
    pods, err := client.ListPods()
    require.NoError(t, err)
    assert.Len(t, pods, numPods)
}
```

## 性能测试

### 基准测试

```go
// pkg/benchmarks/benchmark_test.go
package benchmarks

import (
    "testing"

    "github.com/simple-k8s/pkg/api"
    "github.com/simple-k8s/pkg/storage"
)

func BenchmarkMemoryStorage_CreatePod(b *testing.B) {
    s := storage.NewMemoryStorage()
    pod := &api.Pod{
        ObjectMeta: api.ObjectMeta{
            Name:      "benchmark-pod",
            Namespace: "default",
        },
    }

    b.ResetTimer()
    for i := 0; i < b.N; i++ {
        pod.Name = fmt.Sprintf("pod-%d", i)
        s.CreatePod(pod)
    }
}

func BenchmarkMemoryStorage_GetPod(b *testing.B) {
    s := storage.NewMemoryStorage()
    pod := &api.Pod{
        ObjectMeta: api.ObjectMeta{
            Name:      "benchmark-pod",
            Namespace: "default",
        },
    }
    s.CreatePod(pod)

    b.ResetTimer()
    for i := 0; i < b.N; i++ {
        s.GetPod("default", "benchmark-pod")
    }
}

func BenchmarkMemoryStorage_ListPods(b *testing.B) {
    s := storage.NewMemoryStorage()

    // 预先创建一些Pod
    for i := 0; i < 1000; i++ {
        pod := &api.Pod{
            ObjectMeta: api.ObjectMeta{
                Name:      fmt.Sprintf("pod-%d", i),
                Namespace: "default",
            },
        }
        s.CreatePod(pod)
    }

    b.ResetTimer()
    for i := 0; i < b.N; i++ {
        s.ListPods()
    }
}
```

## 测试最佳实践

### 1. 测试组织结构

```
pkg/
├── storage/
│   ├── memory_storage.go
│   └── memory_storage_test.go
├── controller/
│   ├── pod_controller.go
│   ├── pod_controller_test.go
│   └── mocks/
│       └── mock_client.go
├── testing/
│   ├── helpers.go
│   └── test_server.go
└── benchmarks/
    └── benchmark_test.go
```

### 2. 测试命名规范

```go
// 好的测试命名
func TestMemoryStorage_CreatePod_Success(t *testing.T)
func TestMemoryStorage_CreatePod_Duplicate(t *testing.T)
func TestMemoryStorage_CreatePod_InvalidData(t *testing.T)

// 避免的命名
func TestPod(t *testing.T)           // 太泛化
func TestCreatePod(t *testing.T)      // 不够具体
func TestMemoryStorage_1(t *testing.T) // 无意义
```

### 3. 测试数据准备

```go
// 使用构建者模式创建测试数据
func NewTestPod(name, namespace string) *api.Pod {
    return &api.Pod{
        ObjectMeta: api.ObjectMeta{
            Name:      name,
            Namespace: namespace,
        },
        Spec: api.PodSpec{
            Containers: []api.Container{
                {
                    Name:  "nginx",
                    Image: "nginx:latest",
                },
            },
        },
    }
}

// 使用表格驱动测试
func TestMemoryStorage_GetPod(t *testing.T) {
    tests := []struct {
        name        string
        namespace   string
        podName     string
        setup       func(*storage.MemoryStorage)
        expectError bool
    }{
        {
            name:      "existing pod",
            namespace: "default",
            podName:   "test-pod",
            setup: func(s *storage.MemoryStorage) {
                s.CreatePod(&api.Pod{
                    ObjectMeta: api.ObjectMeta{
                        Name:      "test-pod",
                        Namespace: "default",
                    },
                })
            },
            expectError: false,
        },
        {
            name:        "non-existing pod",
            namespace:   "default",
            podName:     "non-existent",
            setup:       func(*storage.MemoryStorage) {},
            expectError: true,
        },
    }

    for _, tt := range tests {
        t.Run(tt.name, func(t *testing.T) {
            storage := storage.NewMemoryStorage()
            tt.setup(storage)

            _, err := storage.GetPod(tt.namespace, tt.podName)

            if tt.expectError {
                assert.Error(t, err)
            } else {
                assert.NoError(t, err)
            }
        })
    }
}
```

### 4. Mock使用原则

```go
// 好的Mock使用
func TestPodController_ReconcilePod(t *testing.T) {
    mockClient := &mocks.MockClient{}

    // 明确设置Mock期望
    mockClient.On("GetPod", "default", "test-pod").Return(pod, nil)
    mockClient.On("UpdatePod", mock.MatchedBy(func(p *api.Pod) bool {
        return p.Status.Phase == api.PodRunning
    })).Return(nil)

    controller := NewPodController(mockClient)
    controller.reconcilePod(context.Background(), "default/test-pod")

    // 验证所有期望都被满足
    mockClient.AssertExpectations(t)
}

// 避免的Mock使用
func TestPodController_BadExample(t *testing.T) {
    mockClient := &mocks.MockClient{}

    // 不要这样做：没有明确的Mock设置
    mockClient.On("GetPod", mock.Anything, mock.Anything).Return(&api.Pod{}, nil)
    mockClient.On("UpdatePod", mock.Anything).Return(nil)

    controller := NewPodController(mockClient)
    controller.reconcilePod(context.Background(), "default/test-pod")

    // 这样无法验证具体的调用参数
}
```

## 总结

本文档提供了Kubernetes单元测试的完整示例，包括：

1. **测试框架选择**: 使用标准库testing和testify
2. **Mock对象**: 使用mockery或手动实现Mock
3. **测试策略**: 单元测试、集成测试、性能测试
4. **最佳实践**: 测试组织、命名规范、数据准备
5. **覆盖率**: 确保关键逻辑都被测试覆盖

通过这些测试示例，可以确保Kubernetes组件的稳定性和可靠性，同时提高代码质量和开发效率。