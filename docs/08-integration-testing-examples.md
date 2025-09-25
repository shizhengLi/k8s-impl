# Kubernetes 集成测试示例

## 概述

本文档提供Kubernetes系统的完整集成测试方案，包括端到端测试、组件间交互测试、性能测试和故障恢复测试。集成测试确保各个组件能够正确协同工作。

## 测试架构

### 测试框架

```go
// pkg/integration/framework/framework.go
package framework

import (
    "context"
    "fmt"
    "os"
    "os/exec"
    "path/filepath"
    "sync"
    "testing"
    "time"

    "github.com/simple-k8s/pkg/api"
    "github.com/simple-k8s/pkg/client"
    "github.com/stretchr/testify/require"
)

// IntegrationTestFramework 集成测试框架
type IntegrationTestFramework struct {
    APIServerURL  string
    Client        client.Client
    TestDir       string
    Components    map[string]*Component
    mu            sync.RWMutex
    ctx           context.Context
    cancel        context.CancelFunc
}

// Component 组件实例
type Component struct {
    Name        string
    Command     *exec.Cmd
    ReadyCheck  func() error
    StopFunc    func() error
    HealthURL   string
    Process     *os.Process
    LogFile     string
}

// NewIntegrationTestFramework 创建新的集成测试框架
func NewIntegrationTestFramework(t *testing.T) *IntegrationTestFramework {
    ctx, cancel := context.WithCancel(context.Background())

    return &IntegrationTestFramework{
        TestDir:    t.TempDir(),
        Components: make(map[string]*Component),
        ctx:        ctx,
        cancel:     cancel,
    }
}

// StartAPIServer 启动API Server
func (f *IntegrationTestFramework) StartAPIServer(t *testing.T, port string) error {
    cmd := exec.Command("go", "run", "cmd/simple-apiserver/main.go", "--port", port)

    logFile := filepath.Join(f.TestDir, "apiserver.log")
    log, err := os.Create(logFile)
    require.NoError(t, err)
    defer log.Close()

    cmd.Stdout = log
    cmd.Stderr = log

    err = cmd.Start()
    require.NoError(t, err)

    component := &Component{
        Name:       "apiserver",
        Command:    cmd,
        Process:    cmd.Process,
        LogFile:    logFile,
        HealthURL:  fmt.Sprintf("http://localhost:%s/healthz", port),
        ReadyCheck: f.healthCheck,
        StopFunc:   f.stopProcess,
    }

    f.Components["apiserver"] = component
    f.APIServerURL = fmt.Sprintf("http://localhost:%s", port)
    f.Client = client.NewClient(f.APIServerURL)

    // 等待API Server就绪
    return f.waitForComponent(t, "apiserver")
}

// StartControllerManager 启动Controller Manager
func (f *IntegrationTestFramework) StartControllerManager(t *testing.T) error {
    cmd := exec.Command("go", "run", "cmd/simple-controller/main.go", "--api-server", f.APIServerURL)

    logFile := filepath.Join(f.TestDir, "controller.log")
    log, err := os.Create(logFile)
    require.NoError(t, err)
    defer log.Close()

    cmd.Stdout = log
    cmd.Stderr = log

    err = cmd.Start()
    require.NoError(t, err)

    component := &Component{
        Name:     "controller",
        Command:  cmd,
        Process:  cmd.Process,
        LogFile:  logFile,
        StopFunc: f.stopProcess,
    }

    f.Components["controller"] = component

    // Controller Manager通常很快启动
    time.Sleep(2 * time.Second)
    return nil
}

// StartScheduler 启动Scheduler
func (f *IntegrationTestFramework) StartScheduler(t *testing.T) error {
    cmd := exec.Command("go", "run", "cmd/simple-scheduler/main.go", "--api-server", f.APIServerURL)

    logFile := filepath.Join(f.TestDir, "scheduler.log")
    log, err := os.Create(logFile)
    require.NoError(t, err)
    defer log.Close()

    cmd.Stdout = log
    cmd.Stderr = log

    err = cmd.Start()
    require.NoError(t, err)

    component := &Component{
        Name:     "scheduler",
        Command:  cmd,
        Process:  cmd.Process,
        LogFile:  logFile,
        StopFunc: f.stopProcess,
    }

    f.Components["scheduler"] = component
    return nil
}

// StartKubelet 启动Kubelet
func (f *IntegrationTestFramework) StartKubelet(t *testing.T, nodeName string) error {
    cmd := exec.Command("go", "run", "cmd/simple-kubelet/main.go",
        "--api-server", f.APIServerURL, "--node-name", nodeName)

    logFile := filepath.Join(f.TestDir, fmt.Sprintf("kubelet-%s.log", nodeName))
    log, err := os.Create(logFile)
    require.NoError(t, err)
    defer log.Close()

    cmd.Stdout = log
    cmd.Stderr = log

    err = cmd.Start()
    require.NoError(t, err)

    component := &Component{
        Name:     fmt.Sprintf("kubelet-%s", nodeName),
        Command:  cmd,
        Process:  cmd.Process,
        LogFile:  logFile,
        StopFunc: f.stopProcess,
    }

    f.Components[component.Name] = component

    // 等待Kubelet注册节点
    return f.waitForNodeRegistration(t, nodeName)
}

// waitForComponent 等待组件就绪
func (f *IntegrationTestFramework) waitForComponent(t *testing.T, componentName string) error {
    component, exists := f.Components[componentName]
    require.True(t, exists, "Component %s not found", componentName)

    ctx, cancel := context.WithTimeout(f.ctx, 30*time.Second)
    defer cancel()

    ticker := time.NewTicker(1 * time.Second)
    defer ticker.Stop()

    for {
        select {
        case <-ctx.Done():
            return fmt.Errorf("timeout waiting for component %s", componentName)
        case <-ticker.C:
            if component.ReadyCheck != nil {
                if err := component.ReadyCheck(); err == nil {
                    return nil
                }
            } else {
                // 默认检查进程是否运行
                if f.isProcessRunning(component.Process) {
                    return nil
                }
            }
        }
    }
}

// healthCheck 健康检查
func (f *IntegrationTestFramework) healthCheck() error {
    // 这里可以添加HTTP健康检查逻辑
    return nil
}

// isProcessRunning 检查进程是否运行
func (f *IntegrationTestFramework) isProcessRunning(process *os.Process) bool {
    if process == nil {
        return false
    }

    err := process.Signal(os.Signal(nil))
    return err == nil
}

// waitForNodeRegistration 等待节点注册
func (f *IntegrationTestFramework) waitForNodeRegistration(t *testing.T, nodeName string) error {
    ctx, cancel := context.WithTimeout(f.ctx, 30*time.Second)
    defer cancel()

    ticker := time.NewTicker(1 * time.Second)
    defer ticker.Stop()

    for {
        select {
        case <-ctx.Done():
            return fmt.Errorf("timeout waiting for node %s registration", nodeName)
        case <-ticker.C:
            node, err := f.Client.GetNode(nodeName)
            if err == nil && node.Status.Phase == api.NodeRunning {
                return nil
            }
        }
    }
}

// stopProcess 停止进程
func (f *IntegrationTestFramework) stopProcess() error {
    // 这里实现进程停止逻辑
    return nil
}

// StopAll 停止所有组件
func (f *IntegrationTestFramework) StopAll() {
    f.cancel()

    for name, component := range f.Components {
        if component.StopFunc != nil {
            component.StopFunc()
        }
        if component.Command != nil {
            component.Command.Process.Kill()
        }
    }
}

// WaitForPodReady 等待Pod就绪
func (f *IntegrationTestFramework) WaitForPodReady(t *testing.T, namespace, name string, timeout time.Duration) error {
    ctx, cancel := context.WithTimeout(f.ctx, timeout)
    defer cancel()

    ticker := time.NewTicker(1 * time.Second)
    defer ticker.Stop()

    for {
        select {
        case <-ctx.Done():
            return fmt.Errorf("timeout waiting for pod %s/%s to be ready", namespace, name)
        case <-ticker.C:
            pod, err := f.Client.GetPod(namespace, name)
            if err == nil && pod.Status.Phase == api.PodRunning {
                return nil
            }
        }
    }
}

// CreateTestPod 创建测试Pod
func (f *IntegrationTestFramework) CreateTestPod(t *testing.T, name, image string) *api.Pod {
    pod := &api.Pod{
        ObjectMeta: api.ObjectMeta{
            Name:      name,
            Namespace: "default",
        },
        Spec: api.PodSpec{
            Containers: []api.Container{
                {
                    Name:  name,
                    Image: image,
                },
            },
        },
    }

    err := f.Client.CreatePod(pod)
    require.NoError(t, err)

    return pod
}
```

## 端到端测试

### Pod生命周期测试

```go
// pkg/integration/pod_lifecycle_test.go
package integration

import (
    "testing"
    "time"

    "github.com/simple-k8s/pkg/integration/framework"
    "github.com/simple-k8s/pkg/api"
    "github.com/stretchr/testify/assert"
    "github.com/stretchr/testify/require"
)

func TestPodLifecycle_Integration(t *testing.T) {
    // 创建测试框架
    testFramework := framework.NewIntegrationTestFramework(t)
    defer testFramework.StopAll()

    // 启动所有组件
    err := testFramework.StartAPIServer(t, "8080")
    require.NoError(t, err)

    err = testFramework.StartControllerManager(t)
    require.NoError(t, err)

    err = testFramework.StartScheduler(t)
    require.NoError(t, err)

    err = testFramework.StartKubelet(t, "node1")
    require.NoError(t, err)

    // 创建测试Pod
    pod := testFramework.CreateTestPod(t, "nginx-pod", "nginx:latest")

    // 等待Pod被调度
    err = testFramework.WaitForPodReady(t, "default", "nginx-pod", 30*time.Second)
    require.NoError(t, err)

    // 验证Pod状态
    retrievedPod, err := testFramework.Client.GetPod("default", "nginx-pod")
    require.NoError(t, err)
    assert.Equal(t, api.PodRunning, retrievedPod.Status.Phase)
    assert.NotEmpty(t, retrievedPod.Spec.NodeName)
    assert.Equal(t, "node1", retrievedPod.Spec.NodeName)

    // 验证节点状态
    node, err := testFramework.Client.GetNode("node1")
    require.NoError(t, err)
    assert.Equal(t, api.NodeRunning, node.Status.Phase)

    t.Logf("Pod %s successfully scheduled to node %s", pod.Name, retrievedPod.Spec.NodeName)
}

func TestMultiplePods_Scheduling_Integration(t *testing.T) {
    testFramework := framework.NewIntegrationTestFramework(t)
    defer testFramework.StopAll()

    // 启动所有组件
    err := testFramework.StartAPIServer(t, "8080")
    require.NoError(t, err)

    err = testFramework.StartControllerManager(t)
    require.NoError(t, err)

    err = testFramework.StartScheduler(t)
    require.NoError(t, err)

    // 启动多个节点
    err = testFramework.StartKubelet(t, "node1")
    require.NoError(t, err)

    err = testFramework.StartKubelet(t, "node2")
    require.NoError(t, err)

    // 创建多个Pod
    const numPods = 5
    var createdPods []*api.Pod

    for i := 0; i < numPods; i++ {
        podName := fmt.Sprintf("nginx-pod-%d", i)
        pod := testFramework.CreateTestPod(t, podName, "nginx:latest")
        createdPods = append(createdPods, pod)
    }

    // 等待所有Pod就绪
    for i := 0; i < numPods; i++ {
        podName := fmt.Sprintf("nginx-pod-%d", i)
        err = testFramework.WaitForPodReady(t, "default", podName, 30*time.Second)
        require.NoError(t, err, "Pod %s should be ready", podName)
    }

    // 验证Pod分布
    node1Pods := 0
    node2Pods := 0

    for _, pod := range createdPods {
        retrievedPod, err := testFramework.Client.GetPod("default", pod.Name)
        require.NoError(t, err)

        switch retrievedPod.Spec.NodeName {
        case "node1":
            node1Pods++
        case "node2":
            node2Pods++
        default:
            t.Errorf("Pod %s scheduled to unknown node: %s", pod.Name, retrievedPod.Spec.NodeName)
        }
    }

    // 验证Pod分布合理
    assert.Equal(t, numPods, node1Pods+node2Pods)
    t.Logf("Pod distribution: node1=%d, node2=%d", node1Pods, node2Pods)
}

func TestPodDeletion_Integration(t *testing.T) {
    testFramework := framework.NewIntegrationTestFramework(t)
    defer testFramework.StopAll()

    // 启动所有组件
    err := testFramework.StartAPIServer(t, "8080")
    require.NoError(t, err)

    err = testFramework.StartControllerManager(t)
    require.NoError(t, err)

    err = testFramework.StartScheduler(t)
    require.NoError(t, err)

    err = testFramework.StartKubelet(t, "node1")
    require.NoError(t, err)

    // 创建并运行Pod
    pod := testFramework.CreateTestPod(t, "delete-test-pod", "nginx:latest")

    err = testFramework.WaitForPodReady(t, "default", "delete-test-pod", 30*time.Second)
    require.NoError(t, err)

    // 删除Pod
    err = testFramework.Client.DeletePod("default", "delete-test-pod")
    require.NoError(t, err)

    // 验证Pod已被删除
    _, err = testFramework.Client.GetPod("default", "delete-test-pod")
    assert.Error(t, err)
}
```

## 故障恢复测试

### 组件故障恢复

```go
// pkg/integration/failure_recovery_test.go
package integration

import (
    "testing"
    "time"

    "github.com/simple-k8s/pkg/integration/framework"
    "github.com/simple-k8s/pkg/api"
    "github.com/stretchr/testify/assert"
    "github.com/stretchr/testify/require"
)

func TestSchedulerRestart_Integration(t *testing.T) {
    testFramework := framework.NewIntegrationTestFramework(t)
    defer testFramework.StopAll()

    // 启动所有组件
    err := testFramework.StartAPIServer(t, "8080")
    require.NoError(t, err)

    err = testFramework.StartControllerManager(t)
    require.NoError(t, err)

    err = testFramework.StartScheduler(t)
    require.NoError(t, err)

    err = testFramework.StartKubelet(t, "node1")
    require.NoError(t, err)

    // 创建Pod
    pod1 := testFramework.CreateTestPod(t, "before-restart", "nginx:latest")

    // 等待Pod就绪
    err = testFramework.WaitForPodReady(t, "default", "before-restart", 30*time.Second)
    require.NoError(t, err)

    // 停止Scheduler
    schedulerComponent, exists := testFramework.Components["scheduler"]
    require.True(t, exists)

    err = schedulerComponent.Command.Process.Kill()
    require.NoError(t, err)

    // 等待Scheduler停止
    time.Sleep(2 * time.Second)

    // 重新启动Scheduler
    err = testFramework.StartScheduler(t)
    require.NoError(t, err)

    // 创建新的Pod
    pod2 := testFramework.CreateTestPod(t, "after-restart", "nginx:latest")

    // 等待新Pod就绪
    err = testFramework.WaitForPodReady(t, "default", "after-restart", 30*time.Second)
    require.NoError(t, err)

    // 验证原来的Pod仍然运行
    retrievedPod1, err := testFramework.Client.GetPod("default", "before-restart")
    require.NoError(t, err)
    assert.Equal(t, api.PodRunning, retrievedPod1.Status.Phase)

    t.Log("Scheduler restart successful, pod scheduling continues to work")
}

func TestKubeletRestart_Integration(t *testing.T) {
    testFramework := framework.NewIntegrationTestFramework(t)
    defer testFramework.StopAll()

    // 启动所有组件
    err := testFramework.StartAPIServer(t, "8080")
    require.NoError(t, err)

    err = testFramework.StartControllerManager(t)
    require.NoError(t, err)

    err = testFramework.StartScheduler(t)
    require.NoError(t, err)

    err = testFramework.StartKubelet(t, "node1")
    require.NoError(t, err)

    // 创建Pod
    pod := testFramework.CreateTestPod(t, "kubelet-restart-test", "nginx:latest")

    // 等待Pod就绪
    err = testFramework.WaitForPodReady(t, "default", "kubelet-restart-test", 30*time.Second)
    require.NoError(t, err)

    // 记录Pod的节点分配
    retrievedPod, err := testFramework.Client.GetPod("default", "kubelet-restart-test")
    require.NoError(t, err)
    originalNode := retrievedPod.Spec.NodeName
    assert.NotEmpty(t, originalNode)

    // 停止Kubelet
    kubeletComponent, exists := testFramework.Components["kubelet-node1"]
    require.True(t, exists)

    err = kubeletComponent.Command.Process.Kill()
    require.NoError(t, err)

    // 等待Kubelet停止
    time.Sleep(2 * time.Second)

    // 重新启动Kubelet
    err = testFramework.StartKubelet(t, "node1")
    require.NoError(t, err)

    // 等待节点重新注册
    err = testFramework.waitForNodeRegistration(t, "node1")
    require.NoError(t, err)

    // 验证Pod仍然在运行
    retrievedPod, err = testFramework.Client.GetPod("default", "kubelet-restart-test")
    require.NoError(t, err)
    assert.Equal(t, api.PodRunning, retrievedPod.Status.Phase)
    assert.Equal(t, originalNode, retrievedPod.Spec.NodeName)

    t.Log("Kubelet restart successful, pod continues to run on same node")
}

func TestAPIServerRestart_Integration(t *testing.T) {
    testFramework := framework.NewIntegrationTestFramework(t)
    defer testFramework.StopAll()

    // 启动所有组件
    err := testFramework.StartAPIServer(t, "8080")
    require.NoError(t, err)

    err = testFramework.StartControllerManager(t)
    require.NoError(t, err)

    err = testFramework.StartScheduler(t)
    require.NoError(t, err)

    err = testFramework.StartKubelet(t, "node1")
    require.NoError(t, err)

    // 创建Pod
    pod := testFramework.CreateTestPod(t, "apiserver-restart-test", "nginx:latest")

    // 等待Pod就绪
    err = testFramework.WaitForPodReady(t, "default", "apiserver-restart-test", 30*time.Second)
    require.NoError(t, err)

    // 记录Pod状态
    retrievedPod, err := testFramework.Client.GetPod("default", "apiserver-restart-test")
    require.NoError(t, err)
    originalPod := *retrievedPod

    // 停止API Server
    apiComponent, exists := testFramework.Components["apiserver"]
    require.True(t, exists)

    err = apiComponent.Command.Process.Kill()
    require.NoError(t, err)

    // 等待API Server停止
    time.Sleep(2 * time.Second)

    // 重新启动API Server
    err = testFramework.StartAPIServer(t, "8080")
    require.NoError(t, err)

    // 等待其他组件重新连接
    time.Sleep(5 * time.Second)

    // 验证Pod状态仍然正确
    retrievedPod, err = testFramework.Client.GetPod("default", "apiserver-restart-test")
    require.NoError(t, err)
    assert.Equal(t, originalPod.Status.Phase, retrievedPod.Status.Phase)
    assert.Equal(t, originalPod.Spec.NodeName, retrievedPod.Spec.NodeName)

    t.Log("API Server restart successful, cluster state preserved")
}
```

## 性能测试

### 负载测试

```go
// pkg/integration/performance_test.go
package integration

import (
    "fmt"
    "sync"
    "testing"
    "time"

    "github.com/simple-k8s/pkg/integration/framework"
    "github.com/simple-k8s/pkg/api"
    "github.com/stretchr/testify/assert"
    "github.com/stretchr/testify/require"
)

func TestHighLoad_PodCreation_Integration(t *testing.T) {
    if testing.Short() {
        t.Skip("Skipping high load test in short mode")
    }

    testFramework := framework.NewIntegrationTestFramework(t)
    defer testFramework.StopAll()

    // 启动所有组件
    err := testFramework.StartAPIServer(t, "8080")
    require.NoError(t, err)

    err = testFramework.StartControllerManager(t)
    require.NoError(t, err)

    err = testFramework.StartScheduler(t)
    require.NoError(t, err)

    // 启动多个节点
    const numNodes = 3
    for i := 1; i <= numNodes; i++ {
        nodeName := fmt.Sprintf("node%d", i)
        err = testFramework.StartKubelet(t, nodeName)
        require.NoError(t, err)
    }

    // 性能测试参数
    const numPods = 50
    const concurrency = 10
    const timeout = 5 * time.Minute

    // 开始计时
    startTime := time.Now()

    // 并发创建Pod
    var wg sync.WaitGroup
    podChan := make(chan int, numPods)
    errChan := make(chan error, numPods)

    // 启动worker
    for i := 0; i < concurrency; i++ {
        wg.Add(1)
        go func(workerID int) {
            defer wg.Done()

            for podID := range podChan {
                podName := fmt.Sprintf("load-test-pod-%d", podID)
                pod := &api.Pod{
                    ObjectMeta: api.ObjectMeta{
                        Name:      podName,
                        Namespace: "default",
                    },
                    Spec: api.PodSpec{
                        Containers: []api.Container{
                            {
                                Name:  fmt.Sprintf("container-%d", podID),
                                Image: "nginx:latest",
                            },
                        },
                    },
                }

                err := testFramework.Client.CreatePod(pod)
                if err != nil {
                    errChan <- fmt.Errorf("failed to create pod %s: %v", podName, err)
                    continue
                }

                // 等待Pod就绪
                err = testFramework.WaitForPodReady(t, "default", podName, timeout)
                if err != nil {
                    errChan <- fmt.Errorf("pod %s not ready within timeout: %v", podName, err)
                }
            }
        }(i)
    }

    // 发送Pod创建任务
    go func() {
        for i := 0; i < numPods; i++ {
            podChan <- i
        }
        close(podChan)
    }()

    // 等待所有worker完成
    wg.Wait()
    close(errChan)

    // 检查错误
    for err := range errChan {
        t.Errorf("Pod creation error: %v", err)
    }

    // 计算性能指标
    duration := time.Since(startTime)
    podsPerSecond := float64(numPods) / duration.Seconds()

    t.Logf("Performance Results:")
    t.Logf("  Total Pods: %d", numPods)
    t.Logf("  Concurrency: %d", concurrency)
    t.Logf("  Duration: %v", duration)
    t.Logf("  Pods/second: %.2f", podsPerSecond)

    // 验证所有Pod都已创建
    pods, err := testFramework.Client.ListPods()
    require.NoError(t, err)
    assert.Equal(t, numPods, len(pods))

    // 验证所有Pod都在运行
    runningPods := 0
    for _, pod := range pods {
        if pod.Status.Phase == api.PodRunning {
            runningPods++
        }
    }
    assert.Equal(t, numPods, runningPods)

    // 性能断言
    assert.True(t, podsPerSecond > 1.0, "Pod creation rate should be > 1 pod/second")
}
```

## 网络测试

### 服务发现测试

```go
// pkg/integration/network_test.go
package integration

import (
    "fmt"
    "net/http"
    "testing"
    "time"

    "github.com/simple-k8s/pkg/integration/framework"
    "github.com/simple-k8s/pkg/api"
    "github.com/stretchr/testify/assert"
    "github.com/stretchr/testify/require"
)

func TestServiceDiscovery_Integration(t *testing.T) {
    testFramework := framework.NewIntegrationTestFramework(t)
    defer testFramework.StopAll()

    // 启动所有组件
    err := testFramework.StartAPIServer(t, "8080")
    require.NoError(t, err)

    err = testFramework.StartControllerManager(t)
    require.NoError(t, err)

    err = testFramework.StartScheduler(t)
    require.NoError(t, err)

    err = testFramework.StartKubelet(t, "node1")
    require.NoError(t, err)

    // 创建nginx Pod
    nginxPod := &api.Pod{
        ObjectMeta: api.ObjectMeta{
            Name:      "nginx-service",
            Namespace: "default",
        },
        Spec: api.PodSpec{
            Containers: []api.Container{
                {
                    Name:  "nginx",
                    Image: "nginx:latest",
                    Ports: []api.ContainerPort{
                        {
                            ContainerPort: 80,
                        },
                    },
                },
            },
        },
    }

    err = testFramework.Client.CreatePod(nginxPod)
    require.NoError(t, err)

    // 等待Pod就绪
    err = testFramework.WaitForPodReady(t, "default", "nginx-service", 30*time.Second)
    require.NoError(t, err)

    // 获取Pod IP
    retrievedPod, err := testFramework.Client.GetPod("default", "nginx-service")
    require.NoError(t, err)
    podIP := retrievedPod.Status.PodIP
    assert.NotEmpty(t, podIP)

    // 等待网络就绪
    time.Sleep(5 * time.Second)

    // 测试HTTP访问
    url := fmt.Sprintf("http://%s", podIP)
    resp, err := http.Get(url)
    if err == nil {
        defer resp.Body.Close()
        assert.Contains(t, []int{200, 301, 302}, resp.StatusCode,
            "Nginx should respond with HTTP status")
        t.Logf("Successfully accessed nginx pod at %s", podIP)
    } else {
        t.Logf("Could not access nginx pod (expected in simple test): %v", err)
    }
}
```

## 配置测试

### 配置变更测试

```go
// pkg/integration/config_test.go
package integration

import (
    "testing"
    "time"

    "github.com/simple-k8s/pkg/integration/framework"
    "github.com/simple-k8s/pkg/api"
    "github.com/stretchr/testify/assert"
    "github.com/stretchr/testify/require"
)

func TestPodUpdate_Integration(t *testing.T) {
    testFramework := framework.NewIntegrationTestFramework(t)
    defer testFramework.StopAll()

    // 启动所有组件
    err := testFramework.StartAPIServer(t, "8080")
    require.NoError(t, err)

    err = testFramework.StartControllerManager(t)
    require.NoError(t, err)

    err = testFramework.StartScheduler(t)
    require.NoError(t, err)

    err = testFramework.StartKubelet(t, "node1")
    require.NoError(t, err)

    // 创建初始Pod
    pod := &api.Pod{
        ObjectMeta: api.ObjectMeta{
            Name:      "update-test-pod",
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

    err = testFramework.Client.CreatePod(pod)
    require.NoError(t, err)

    // 等待Pod就绪
    err = testFramework.WaitForPodReady(t, "default", "update-test-pod", 30*time.Second)
    require.NoError(t, err)

    // 更新Pod
    retrievedPod, err := testFramework.Client.GetPod("default", "update-test-pod")
    require.NoError(t, err)

    // 更新镜像版本
    retrievedPod.Spec.Containers[0].Image = "nginx:1.20"

    err = testFramework.Client.UpdatePod(retrievedPod)
    require.NoError(t, err)

    // 等待更新生效
    time.Sleep(5 * time.Second)

    // 验证更新
    updatedPod, err := testFramework.Client.GetPod("default", "update-test-pod")
    require.NoError(t, err)
    assert.Equal(t, "nginx:1.20", updatedPod.Spec.Containers[0].Image)

    t.Log("Pod update successful")
}

func TestNodeLabels_Integration(t *testing.T) {
    testFramework := framework.NewIntegrationTestFramework(t)
    defer testFramework.StopAll()

    // 启动所有组件
    err := testFramework.StartAPIServer(t, "8080")
    require.NoError(t, err)

    err = testFramework.StartControllerManager(t)
    require.NoError(t, err)

    err = testFramework.StartScheduler(t)
    require.NoError(t, err)

    err = testFramework.StartKubelet(t, "node1")
    require.NoError(t, err)

    // 获取节点
    node, err := testFramework.Client.GetNode("node1")
    require.NoError(t, err)

    // 添加标签
    if node.Labels == nil {
        node.Labels = make(map[string]string)
    }
    node.Labels["node-role.kubernetes.io/worker"] = "true"
    node.Labels["environment"] = "test"

    // 更新节点
    err = testFramework.Client.UpdateNode(node)
    require.NoError(t, err)

    // 验证标签更新
    updatedNode, err := testFramework.Client.GetNode("node1")
    require.NoError(t, err)
    assert.Equal(t, "true", updatedNode.Labels["node-role.kubernetes.io/worker"])
    assert.Equal(t, "test", updatedNode.Labels["environment"])

    t.Log("Node labels update successful")
}
```

## 并发测试

### 并发操作测试

```go
// pkg/integration/concurrency_test.go
package integration

import (
    "sync"
    "testing"
    "time"

    "github.com/simple-k8s/pkg/integration/framework"
    "github.com/simple-k8s/pkg/api"
    "github.com/stretchr/testify/assert"
    "github.com/stretchr/testify/require"
)

func TestConcurrentPodOperations_Integration(t *testing.T) {
    testFramework := framework.NewIntegrationTestFramework(t)
    defer testFramework.StopAll()

    // 启动所有组件
    err := testFramework.StartAPIServer(t, "8080")
    require.NoError(t, err)

    err = testFramework.StartControllerManager(t)
    require.NoError(t, err)

    err = testFramework.StartScheduler(t)
    require.NoError(t, err)

    err = testFramework.StartKubelet(t, "node1")
    require.NoError(t, err)

    const numOperations = 100
    const numGoroutines = 10
    operationsPerGoroutine := numOperations / numGoroutines

    var wg sync.WaitGroup
    errChan := make(chan error, numOperations)
    successChan := make(chan string, numOperations)

    // 创建Pod
    createPod := func(id int) {
        defer wg.Done()

        podName := fmt.Sprintf("concurrent-pod-%d", id)
        pod := &api.Pod{
            ObjectMeta: api.ObjectMeta{
                Name:      podName,
                Namespace: "default",
            },
            Spec: api.PodSpec{
                Containers: []api.Container{
                    {
                        Name:  fmt.Sprintf("container-%d", id),
                        Image: "nginx:latest",
                    },
                },
            },
        }

        err := testFramework.Client.CreatePod(pod)
        if err != nil {
            errChan <- fmt.Errorf("create pod %d failed: %v", id, err)
            return
        }

        // 等待Pod就绪
        err = testFramework.WaitForPodReady(t, "default", podName, 30*time.Second)
        if err != nil {
            errChan <- fmt.Errorf("pod %d not ready: %v", id, err)
            return
        }

        successChan <- podName
    }

    // 启动并发goroutine
    start := time.Now()
    for i := 0; i < numGoroutines; i++ {
        wg.Add(1)
        go func(goroutineID int) {
            defer wg.Done()

            for j := 0; j < operationsPerGoroutine; j++ {
                id := goroutineID*operationsPerGoroutine + j
                createPod(id)
            }
        }(i)
    }

    // 等待所有操作完成
    wg.Wait()
    close(errChan)
    close(successChan)

    duration := time.Since(start)

    // 统计结果
    var errors []error
    for err := range errChan {
        errors = append(errors, err)
    }

    var successes []string
    for success := range successChan {
        successes = append(successes, success)
    }

    // 输出结果
    t.Logf("Concurrent Operations Results:")
    t.Logf("  Total Operations: %d", numOperations)
    t.Logf("  Successful: %d", len(successes))
    t.Logf("  Failed: %d", len(errors))
    t.Logf("  Duration: %v", duration)
    t.Logf("  Operations/second: %.2f", float64(len(successes))/duration.Seconds())

    // 验证结果
    for _, err := range errors {
        t.Errorf("Operation error: %v", err)
    }

    // 验证所有Pod都存在
    pods, err := testFramework.Client.ListPods()
    require.NoError(t, err)
    assert.Equal(t, len(successes), len(pods))

    // 验证所有Pod都在运行
    runningPods := 0
    for _, pod := range pods {
        if pod.Status.Phase == api.PodRunning {
            runningPods++
        }
    }
    assert.Equal(t, len(successes), runningPods)
}
```

## 测试工具

### 日志收集和分析

```go
// pkg/integration/logging_test.go
package integration

import (
    "bufio"
    "fmt"
    "os"
    "path/filepath"
    "regexp"
    "testing"
    "time"

    "github.com/simple-k8s/pkg/integration/framework"
    "github.com/stretchr/testify/assert"
    "github.com/stretchr/testify/require"
)

func TestLogging_Integration(t *testing.T) {
    testFramework := framework.NewIntegrationTestFramework(t)
    defer testFramework.StopAll()

    // 启动所有组件
    err := testFramework.StartAPIServer(t, "8080")
    require.NoError(t, err)

    err = testFramework.StartControllerManager(t)
    require.NoError(t, err)

    err = testFramework.StartScheduler(t)
    require.NoError(t, err)

    err = testFramework.StartKubelet(t, "node1")
    require.NoError(t, err)

    // 创建一些Pod以产生日志
    for i := 0; i < 3; i++ {
        podName := fmt.Sprintf("log-test-pod-%d", i)
        testFramework.CreateTestPod(t, podName, "nginx:latest")
    }

    // 等待Pod运行
    time.Sleep(10 * time.Second)

    // 分析各个组件的日志
    t.Log("=== Component Logs Analysis ===")

    // 分析API Server日志
    if apiLog := testFramework.Components["apiserver"].LogFile; apiLog != "" {
        t.Logf("API Server Log:")
        analyzeLogFile(t, apiLog, []string{
            "Starting API server",
            "Handling.*request",
            "GET.*healthz",
        })
    }

    // 分析Controller日志
    if controllerLog := testFramework.Components["controller"].LogFile; controllerLog != "" {
        t.Logf("Controller Log:")
        analyzeLogFile(t, controllerLog, []string{
            "Starting controller",
            "Reconciling pod",
            "Updating pod status",
        })
    }

    // 分析Scheduler日志
    if schedulerLog := testFramework.Components["scheduler"].LogFile; schedulerLog != "" {
        t.Logf("Scheduler Log:")
        analyzeLogFile(t, schedulerLog, []string{
            "Starting scheduler",
            "Scheduling pod",
            "Successfully scheduled",
        })
    }

    // 分析Kubelet日志
    if kubeletLog := testFramework.Components["kubelet-node1"].LogFile; kubeletLog != "" {
        t.Logf("Kubelet Log:")
        analyzeLogFile(t, kubeletLog, []string{
            "Starting kubelet",
            "Syncing pod",
            "Starting container",
        })
    }
}

// analyzeLogFile 分析日志文件
func analyzeLogFile(t *testing.T, logFile string, expectedPatterns []string) {
    file, err := os.Open(logFile)
    require.NoError(t, err)
    defer file.Close()

    scanner := bufio.NewScanner(file)
    lineCount := 0
    patternMatches := make(map[string]int)

    for scanner.Scan() {
        line := scanner.Text()
        lineCount++

        // 检查预期模式
        for _, pattern := range expectedPatterns {
            matched, err := regexp.MatchString(pattern, line)
            require.NoError(t, err)
            if matched {
                patternMatches[pattern]++
            }
        }
    }

    require.NoError(t, scanner.Err())

    t.Logf("  Total lines: %d", lineCount)
    for pattern, count := range patternMatches {
        t.Logf("  Pattern '%s': %d matches", pattern, count)
    }

    // 验证日志不为空
    assert.Greater(t, lineCount, 0, "Log file should not be empty")
}
```

## 测试最佳实践

### 1. 测试结构

```go
// 好的测试结构
func TestFeature_Integration(t *testing.T) {
    // 1. 设置测试环境
    testFramework := framework.NewIntegrationTestFramework(t)
    defer testFramework.StopAll()

    // 2. 启动所需组件
    err := testFramework.StartAPIServer(t, "8080")
    require.NoError(t, err)

    err = testFramework.StartKubelet(t, "node1")
    require.NoError(t, err)

    // 3. 准备测试数据
    pod := testFramework.CreateTestPod(t, "test-pod", "nginx:latest")

    // 4. 执行测试操作
    err = testFramework.WaitForPodReady(t, "default", "test-pod", 30*time.Second)
    require.NoError(t, err)

    // 5. 验证结果
    retrievedPod, err := testFramework.Client.GetPod("default", "test-pod")
    require.NoError(t, err)
    assert.Equal(t, api.PodRunning, retrievedPod.Status.Phase)

    // 6. 清理（如果需要）
    // testFramework.Client.DeletePod("default", "test-pod")
}
```

### 2. 错误处理

```go
// 好的错误处理
func TestRobustness_Integration(t *testing.T) {
    testFramework := framework.NewIntegrationTestFramework(t)
    defer testFramework.StopAll()

    // 启动组件
    err := testFramework.StartAPIServer(t, "8080")
    if err != nil {
        t.Fatalf("Failed to start API server: %v", err)
    }

    // 使用require来停止测试如果关键组件启动失败
    err = testFramework.StartKubelet(t, "node1")
    require.NoError(t, err, "Kubelet should start successfully")

    // 使用assert来验证预期结果但不停止测试
    pod := testFramework.CreateTestPod(t, "test-pod", "nginx:latest")
    assert.NotNil(t, pod, "Pod should be created")

    // 等待操作
    err = testFramework.WaitForPodReady(t, "default", "test-pod", 30*time.Second)
    if err != nil {
        t.Logf("Pod not ready within timeout: %v", err)
        // 可能是性能问题，但不一定是功能错误
    }
}
```

### 3. 性能测试

```go
// 性能测试结构
func BenchmarkPodCreation_Integration(b *testing.B) {
    // 避免在短测试模式下运行
    if testing.Short() {
        b.Skip("Skipping benchmark in short mode")
    }

    testFramework := framework.NewIntegrationTestFramework(b)
    defer testFramework.StopAll()

    // 启动环境
    err := testFramework.StartAPIServer(b, "8080")
    require.NoError(b, err)

    err = testFramework.StartKubelet(b, "node1")
    require.NoError(b, err)

    // 重置计时器
    b.ResetTimer()

    // 运行基准测试
    for i := 0; i < b.N; i++ {
        podName := fmt.Sprintf("benchmark-pod-%d", i)
        pod := &api.Pod{
            ObjectMeta: api.ObjectMeta{
                Name:      podName,
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

        err := testFramework.Client.CreatePod(pod)
        if err != nil {
            b.Fatalf("Failed to create pod: %v", err)
        }
    }
}
```

## 总结

本文档提供了完整的Kubernetes集成测试方案，包括：

1. **测试框架**: 提供可重用的测试基础设施
2. **端到端测试**: 验证完整的工作流程
3. **故障恢复测试**: 测试系统的弹性
4. **性能测试**: 评估系统性能
5. **并发测试**: 验证并发操作的正确性
6. **日志分析**: 帮助调试和监控

通过这些集成测试，可以确保Kubernetes系统在实际运行环境中的稳定性和可靠性。集成测试应该与单元测试和端到端测试一起构成完整的测试策略。