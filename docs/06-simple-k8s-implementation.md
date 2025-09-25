# 简化版 Kubernetes 实现方案

## 概述

本文档将创建一个简化版的Kubernetes实现，包含核心功能但代码量更少，便于学习和理解Kubernetes的架构原理。

## 系统架构

### 核心组件

```
simple-k8s/
├── cmd/
│   ├── simple-apiserver/      # 简化版API Server
│   ├── simple-controller/     # 简化版Controller Manager
│   ├── simple-scheduler/      # 简化版Scheduler
│   ├── simple-kubelet/        # 简化版Kubelet
│   └── simple-cli/           # 简化版CLI工具
├── pkg/
│   ├── api/                  # API定义
│   ├── controller/           # 控制器实现
│   ├── scheduler/            # 调度器实现
│   ├── kubelet/              # Kubelet实现
│   ├── storage/              # 简单存储
│   └── utils/                # 工具函数
└── examples/                 # 示例配置
```

## API定义

### 核心对象定义

```go
// pkg/api/types.go
package api

import (
    metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
)

// Pod对象
type Pod struct {
    metav1.TypeMeta   `json:",inline"`
    metav1.ObjectMeta `json:"metadata,omitempty"`

    Spec   PodSpec   `json:"spec,omitempty"`
    Status PodStatus `json:"status,omitempty"`
}

// Pod规格
type PodSpec struct {
    Containers []Container `json:"containers"`
    NodeName   string      `json:"nodeName,omitempty"`
}

// Pod状态
type PodStatus struct {
    Phase     PodPhase `json:"phase,omitempty"`
    HostIP    string   `json:"hostIP,omitempty"`
    PodIP     string   `json:"podIP,omitempty"`
    Message   string   `json:"message,omitempty"`
    Reason    string   `json:"reason,omitempty"`
}

// 容器定义
type Container struct {
    Name  string            `json:"name"`
    Image string            `json:"image"`
    Env   []EnvVar          `json:"env,omitempty"`
    Ports []ContainerPort   `json:"ports,omitempty"`
}

// 环境变量
type EnvVar struct {
    Name  string `json:"name"`
    Value string `json:"value"`
}

// 容器端口
type ContainerPort struct {
    ContainerPort int32  `json:"containerPort"`
    Protocol      string `json:"protocol,omitempty"`
}

// 节点对象
type Node struct {
    metav1.TypeMeta   `json:",inline"`
    metav1.ObjectMeta `json:"metadata,omitempty"`

    Spec   NodeSpec   `json:"spec,omitempty"`
    Status NodeStatus `json:"status,omitempty"`
}

// 节点规格
type NodeSpec struct {
    Capacity ResourceList `json:"capacity,omitempty"`
}

// 节点状态
type NodeStatus struct {
    Capacity ResourceList     `json:"capacity,omitempty"`
    Allocatable ResourceList `json:"allocatable,omitempty"`
    Phase    NodePhase       `json:"phase,omitempty"`
    Addresses []NodeAddress  `json:"addresses,omitempty"`
}

// 节点地址
type NodeAddress struct {
    Type    string `json:"type"`
    Address string `json:"address"`
}

// 资源列表
type ResourceList map[ResourceName]string

// 资源名称
type ResourceName string

const (
    ResourceCPU    ResourceName = "cpu"
    ResourceMemory ResourceName = "memory"
    ResourcePods   ResourceName = "pods"
)

// Pod阶段
type PodPhase string

const (
    PodPending   PodPhase = "Pending"
    PodRunning   PodPhase = "Running"
    PodSucceeded PodPhase = "Succeeded"
    PodFailed    PodPhase = "Failed"
    PodUnknown   PodPhase = "Unknown"
)

// 节点阶段
type NodePhase string

const (
    NodePending  NodePhase = "Pending"
    NodeRunning  NodePhase = "Running"
    NodeTerminated NodePhase = "Terminated"
    NodeUnknown  NodePhase = "Unknown"
)
```

## 简化版API Server

### 核心实现

```go
// cmd/simple-apiserver/main.go
package main

import (
    "context"
    "encoding/json"
    "log"
    "net/http"
    "time"

    "github.com/gorilla/mux"
)

type APIServer struct {
    storage Storage
    router  *mux.Router
    server  *http.Server
}

func NewAPIServer(port string, storage Storage) *APIServer {
    router := mux.NewRouter()

    server := &APIServer{
        storage: storage,
        router:  router,
        server: &http.Server{
            Addr:    ":" + port,
            Handler: router,
        },
    }

    // 注册路由
    server.registerRoutes()

    return server
}

func (s *APIServer) registerRoutes() {
    // Pod路由
    s.router.HandleFunc("/api/v1/pods", s.handlePods).Methods("GET", "POST")
    s.router.HandleFunc("/api/v1/pods/{namespace}/{name}", s.handlePod).Methods("GET", "PUT", "DELETE")

    // Node路由
    s.router.HandleFunc("/api/v1/nodes", s.handleNodes).Methods("GET", "POST")
    s.router.HandleFunc("/api/v1/nodes/{name}", s.handleNode).Methods("GET", "PUT", "DELETE")

    // 健康检查
    s.router.HandleFunc("/healthz", s.handleHealth).Methods("GET")
}

func (s *APIServer) handlePods(w http.ResponseWriter, r *http.Request) {
    switch r.Method {
    case "GET":
        s.listPods(w, r)
    case "POST":
        s.createPod(w, r)
    }
}

func (s *APIServer) handlePod(w http.ResponseWriter, r *http.Request) {
    vars := mux.Vars(r)
    namespace := vars["namespace"]
    name := vars["name"]

    switch r.Method {
    case "GET":
        s.getPod(w, r, namespace, name)
    case "PUT":
        s.updatePod(w, r, namespace, name)
    case "DELETE":
        s.deletePod(w, r, namespace, name)
    }
}

func (s *APIServer) listPods(w http.ResponseWriter, r *http.Request) {
    pods, err := s.storage.ListPods()
    if err != nil {
        http.Error(w, err.Error(), http.StatusInternalServerError)
        return
    }

    response := PodList{
        Items: pods,
    }

    json.NewEncoder(w).Encode(response)
}

func (s *APIServer) createPod(w http.ResponseWriter, r *http.Request) {
    var pod api.Pod
    if err := json.NewDecoder(r.Body).Decode(&pod); err != nil {
        http.Error(w, err.Error(), http.StatusBadRequest)
        return
    }

    if err := s.storage.CreatePod(&pod); err != nil {
        http.Error(w, err.Error(), http.StatusInternalServerError)
        return
    }

    w.WriteHeader(http.StatusCreated)
    json.NewEncoder(w).Encode(pod)
}

func (s *APIServer) getPod(w http.ResponseWriter, r *http.Request, namespace, name string) {
    pod, err := s.storage.GetPod(namespace, name)
    if err != nil {
        http.Error(w, err.Error(), http.StatusNotFound)
        return
    }

    json.NewEncoder(w).Encode(pod)
}

func (s *APIServer) updatePod(w http.ResponseWriter, r *http.Request, namespace, name string) {
    var pod api.Pod
    if err := json.NewDecoder(r.Body).Decode(&pod); err != nil {
        http.Error(w, err.Error(), http.StatusBadRequest)
        return
    }

    if err := s.storage.UpdatePod(&pod); err != nil {
        http.Error(w, err.Error(), http.StatusInternalServerError)
        return
    }

    json.NewEncoder(w).Encode(pod)
}

func (s *APIServer) deletePod(w http.ResponseWriter, r *http.Request, namespace, name string) {
    if err := s.storage.DeletePod(namespace, name); err != nil {
        http.Error(w, err.Error(), http.StatusInternalServerError)
        return
    }

    w.WriteHeader(http.StatusNoContent)
}

func (s *APIServer) handleHealth(w http.ResponseWriter, r *http.Request) {
    w.WriteHeader(http.StatusOK)
    w.Write([]byte("ok"))
}

func (s *APIServer) Run() error {
    log.Printf("Starting API server on %s", s.server.Addr)
    return s.server.ListenAndServe()
}

func (s *APIServer) Shutdown(ctx context.Context) error {
    return s.server.Shutdown(ctx)
}
```

### 存储接口

```go
// pkg/storage/storage.go
package storage

import (
    "errors"
    "sync"

    "github.com/simple-k8s/pkg/api"
)

type Storage interface {
    // Pod操作
    CreatePod(pod *api.Pod) error
    GetPod(namespace, name string) (*api.Pod, error)
    UpdatePod(pod *api.Pod) error
    DeletePod(namespace, name string) error
    ListPods() ([]*api.Pod, error)

    // Node操作
    CreateNode(node *api.Node) error
    GetNode(name string) (*api.Node, error)
    UpdateNode(node *api.Node) error
    DeleteNode(name string) error
    ListNodes() ([]*api.Node, error)
}

// 内存存储实现
type MemoryStorage struct {
    pods  map[string]*api.Pod
    nodes map[string]*api.Node
    mutex sync.RWMutex
}

func NewMemoryStorage() *MemoryStorage {
    return &MemoryStorage{
        pods:  make(map[string]*api.Pod),
        nodes: make(map[string]*api.Node),
    }
}

func (m *MemoryStorage) CreatePod(pod *api.Pod) error {
    m.mutex.Lock()
    defer m.mutex.Unlock()

    key := pod.Namespace + "/" + pod.Name
    if _, exists := m.pods[key]; exists {
        return errors.New("pod already exists")
    }

    m.pods[key] = pod
    return nil
}

func (m *MemoryStorage) GetPod(namespace, name string) (*api.Pod, error) {
    m.mutex.RLock()
    defer m.mutex.RUnlock()

    key := namespace + "/" + name
    pod, exists := m.pods[key]
    if !exists {
        return nil, errors.New("pod not found")
    }

    return pod, nil
}

func (m *MemoryStorage) UpdatePod(pod *api.Pod) error {
    m.mutex.Lock()
    defer m.mutex.Unlock()

    key := pod.Namespace + "/" + pod.Name
    if _, exists := m.pods[key]; !exists {
        return errors.New("pod not found")
    }

    m.pods[key] = pod
    return nil
}

func (m *MemoryStorage) DeletePod(namespace, name string) error {
    m.mutex.Lock()
    defer m.mutex.Unlock()

    key := namespace + "/" + name
    if _, exists := m.pods[key]; !exists {
        return errors.New("pod not found")
    }

    delete(m.pods, key)
    return nil
}

func (m *MemoryStorage) ListPods() ([]*api.Pod, error) {
    m.mutex.RLock()
    defer m.mutex.RUnlock()

    pods := make([]*api.Pod, 0, len(m.pods))
    for _, pod := range m.pods {
        pods = append(pods, pod)
    }

    return pods, nil
}

func (m *MemoryStorage) CreateNode(node *api.Node) error {
    m.mutex.Lock()
    defer m.mutex.Unlock()

    if _, exists := m.nodes[node.Name]; exists {
        return errors.New("node already exists")
    }

    m.nodes[node.Name] = node
    return nil
}

func (m *MemoryStorage) GetNode(name string) (*api.Node, error) {
    m.mutex.RLock()
    defer m.mutex.RUnlock()

    node, exists := m.nodes[name]
    if !exists {
        return nil, errors.New("node not found")
    }

    return node, nil
}

func (m *MemoryStorage) UpdateNode(node *api.Node) error {
    m.mutex.Lock()
    defer m.mutex.Unlock()

    if _, exists := m.nodes[node.Name]; !exists {
        return errors.New("node not found")
    }

    m.nodes[node.Name] = node
    return nil
}

func (m *MemoryStorage) DeleteNode(name string) error {
    m.mutex.Lock()
    defer m.mutex.Unlock()

    if _, exists := m.nodes[name]; !exists {
        return errors.New("node not found")
    }

    delete(m.nodes, name)
    return nil
}

func (m *MemoryStorage) ListNodes() ([]*api.Node, error) {
    m.mutex.RLock()
    defer m.mutex.RUnlock()

    nodes := make([]*api.Node, 0, len(m.nodes))
    for _, node := range m.nodes {
        nodes = append(nodes, node)
    }

    return nodes, nil
}
```

## 简化版Controller Manager

### Pod控制器

```go
// cmd/simple-controller/main.go
package main

import (
    "context"
    "fmt"
    "log"
    "net/http"
    "time"

    "github.com/simple-k8s/pkg/api"
    "github.com/simple-k8s/pkg/client"
    "github.com/simple-k8s/pkg/controller"
)

type ControllerManager struct {
    podController    *controller.PodController
    nodeController   *controller.NodeController
    httpClient       *http.Client
    apiServerURL     string
}

func NewControllerManager(apiServerURL string) *ControllerManager {
    client := client.NewClient(apiServerURL)

    return &ControllerManager{
        podController:  controller.NewPodController(client),
        nodeController: controller.NewNodeController(client),
        httpClient:     &http.Client{Timeout: 10 * time.Second},
        apiServerURL:   apiServerURL,
    }
}

func (cm *ControllerManager) Run(ctx context.Context) {
    log.Println("Starting controller manager...")

    // 启动控制器
    go cm.podController.Run(ctx)
    go cm.nodeController.Run(ctx)

    // 等待上下文取消
    <-ctx.Done()
    log.Println("Controller manager stopped")
}

// pkg/controller/pod_controller.go
package controller

import (
    "context"
    "log"
    "time"

    "github.com/simple-k8s/pkg/api"
    "github.com/simple-k8s/pkg/client"
)

type PodController struct {
    client         client.Client
    reconcileQueue chan string
}

func NewPodController(client client.Client) *PodController {
    return &PodController{
        client:         client,
        reconcileQueue: make(chan string, 100),
    }
}

func (pc *PodController) Run(ctx context.Context) {
    log.Println("Starting pod controller...")

    // 启动多个worker
    for i := 0; i < 3; i++ {
        go pc.worker(ctx)
    }

    // 监听Pod变化
    go pc.watchPods(ctx)

    <-ctx.Done()
    log.Println("Pod controller stopped")
}

func (pc *PodController) worker(ctx context.Context) {
    for {
        select {
        case <-ctx.Done():
            return
        case podKey := <-pc.reconcileQueue:
            pc.reconcilePod(ctx, podKey)
        }
    }
}

func (pc *PodController) reconcilePod(ctx context.Context, podKey string) {
    namespace, name, err := parseKey(podKey)
    if err != nil {
        log.Printf("Error parsing pod key %s: %v", podKey, err)
        return
    }

    log.Printf("Reconciling pod %s/%s", namespace, name)

    // 获取Pod
    pod, err := pc.client.GetPod(namespace, name)
    if err != nil {
        log.Printf("Error getting pod %s/%s: %v", namespace, name, err)
        return
    }

    // 处理Pod状态
    if err := pc.handlePodStatus(pod); err != nil {
        log.Printf("Error handling pod status for %s/%s: %v", namespace, name, err)
        return
    }

    // 更新Pod
    if err := pc.client.UpdatePod(pod); err != nil {
        log.Printf("Error updating pod %s/%s: %v", namespace, name, err)
    }
}

func (pc *PodController) handlePodStatus(pod *api.Pod) error {
    // 简化的状态处理逻辑
    if pod.Status.Phase == "" {
        pod.Status.Phase = api.PodPending
    }

    // 如果Pod有节点分配，尝试运行
    if pod.Spec.NodeName != "" && pod.Status.Phase == api.PodPending {
        pod.Status.Phase = api.PodRunning
        pod.Status.Message = "Pod is running"
    }

    return nil
}

func (pc *PodController) watchPods(ctx context.Context) {
    // 简化的监听逻辑，定期轮询
    ticker := time.NewTicker(5 * time.Second)
    defer ticker.Stop()

    for {
        select {
        case <-ctx.Done():
            return
        case <-ticker.C:
            pc.pollPods()
        }
    }
}

func (pc *PodController) pollPods() {
    pods, err := pc.client.ListPods()
    if err != nil {
        log.Printf("Error listing pods: %v", err)
        return
    }

    for _, pod := range pods {
        key := fmt.Sprintf("%s/%s", pod.Namespace, pod.Name)
        select {
        case pc.reconcileQueue <- key:
        default:
            log.Printf("Reconcile queue full, skipping pod %s", key)
        }
    }
}

func parseKey(key string) (namespace, name string, err error) {
    parts := strings.Split(key, "/")
    if len(parts) != 2 {
        return "", "", fmt.Errorf("invalid key format: %s", key)
    }
    return parts[0], parts[1], nil
}
```

## 简化版Scheduler

### 调度器实现

```go
// cmd/simple-scheduler/main.go
package main

import (
    "context"
    "log"
    "time"

    "github.com/simple-k8s/pkg/client"
    "github.com/simple-k8s/pkg/scheduler"
)

type Scheduler struct {
    scheduler      *scheduler.Scheduler
    apiServerURL   string
}

func NewScheduler(apiServerURL string) *Scheduler {
    client := client.NewClient(apiServerURL)

    return &Scheduler{
        scheduler:    scheduler.NewScheduler(client),
        apiServerURL: apiServerURL,
    }
}

func (s *Scheduler) Run(ctx context.Context) {
    log.Println("Starting scheduler...")
    s.scheduler.Run(ctx)
}

// pkg/scheduler/scheduler.go
package scheduler

import (
    "context"
    "fmt"
    "log"
    "math/rand"
    "time"

    "github.com/simple-k8s/pkg/api"
    "github.com/simple-k8s/pkg/client"
)

type Scheduler struct {
    client     client.Client
    queue      chan *api.Pod
}

func NewScheduler(client client.Client) *Scheduler {
    return &Scheduler{
        client: client,
        queue:  make(chan *api.Pod, 100),
    }
}

func (s *Scheduler) Run(ctx context.Context) {
    // 启动调度worker
    for i := 0; i < 3; i++ {
        go s.scheduleWorker(ctx)
    }

    // 启动Pod监听
    go s.watchPendingPods(ctx)

    <-ctx.Done()
    log.Println("Scheduler stopped")
}

func (s *Scheduler) scheduleWorker(ctx context.Context) {
    for {
        select {
        case <-ctx.Done():
            return
        case pod := <-s.queue:
            s.schedulePod(ctx, pod)
        }
    }
}

func (s *Scheduler) schedulePod(ctx context.Context, pod *api.Pod) {
    if pod.Spec.NodeName != "" {
        log.Printf("Pod %s/%s already scheduled to node %s", pod.Namespace, pod.Name, pod.Spec.NodeName)
        return
    }

    log.Printf("Scheduling pod %s/%s", pod.Namespace, pod.Name)

    // 获取可用节点
    nodes, err := s.client.ListNodes()
    if err != nil {
        log.Printf("Error listing nodes: %v", err)
        return
    }

    if len(nodes) == 0 {
        log.Printf("No available nodes for pod %s/%s", pod.Namespace, pod.Name)
        return
    }

    // 简单的随机调度策略
    selectedNode := s.selectNode(nodes, pod)
    if selectedNode == nil {
        log.Printf("No suitable node found for pod %s/%s", pod.Namespace, pod.Name)
        return
    }

    // 分配节点
    pod.Spec.NodeName = selectedNode.Name
    pod.Status.Phase = api.PodRunning
    pod.Status.Message = fmt.Sprintf("Scheduled to node %s", selectedNode.Name)

    // 更新Pod
    if err := s.client.UpdatePod(pod); err != nil {
        log.Printf("Error updating pod %s/%s: %v", pod.Namespace, pod.Name, err)
        return
    }

    log.Printf("Successfully scheduled pod %s/%s to node %s", pod.Namespace, pod.Name, selectedNode.Name)
}

func (s *Scheduler) selectNode(nodes []*api.Node, pod *api.Pod) *api.Node {
    // 简化的节点选择逻辑
    var suitableNodes []*api.Node

    for _, node := range nodes {
        if s.isNodeSuitable(node, pod) {
            suitableNodes = append(suitableNodes, node)
        }
    }

    if len(suitableNodes) == 0 {
        return nil
    }

    // 随机选择一个节点
    return suitableNodes[rand.Intn(len(suitableNodes))]
}

func (s *Scheduler) isNodeSuitable(node *api.Node, pod *api.Pod) bool {
    // 检查节点状态
    if node.Status.Phase != api.NodeRunning {
        return false
    }

    // 简化的资源检查
    if node.Status.Allocatable == nil {
        return true
    }

    // 这里可以添加更复杂的资源检查逻辑
    return true
}

func (s *Scheduler) watchPendingPods(ctx context.Context) {
    ticker := time.NewTicker(5 * time.Second)
    defer ticker.Stop()

    for {
        select {
        case <-ctx.Done():
            return
        case <-ticker.C:
            s.pollPendingPods()
        }
    }
}

func (s *Scheduler) pollPendingPods() {
    pods, err := s.client.ListPods()
    if err != nil {
        log.Printf("Error listing pods: %v", err)
        return
    }

    for _, pod := range pods {
        if pod.Status.Phase == api.PodPending && pod.Spec.NodeName == "" {
            select {
            case s.queue <- pod:
            default:
                log.Printf("Scheduler queue full, skipping pod %s/%s", pod.Namespace, pod.Name)
            }
        }
    }
}
```

## 简化版Kubelet

### Kubelet实现

```go
// cmd/simple-kubelet/main.go
package main

import (
    "context"
    "flag"
    "log"
    "os"
    "os/exec"
    "time"

    "github.com/simple-k8s/pkg/client"
    "github.com/simple-k8s/pkg/kubelet"
)

type Kubelet struct {
    kubelet       *kubelet.Kubelet
    apiServerURL  string
    nodeName      string
}

func NewKubelet(apiServerURL, nodeName string) *Kubelet {
    client := client.NewClient(apiServerURL)

    return &Kubelet{
        kubelet:      kubelet.NewKubelet(client, nodeName),
        apiServerURL: apiServerURL,
        nodeName:     nodeName,
    }
}

func (k *Kubelet) Run(ctx context.Context) {
    log.Printf("Starting kubelet for node %s", k.nodeName)

    // 注册节点
    if err := k.registerNode(); err != nil {
        log.Fatalf("Error registering node: %v", err)
    }

    // 运行kubelet
    k.kubelet.Run(ctx)
}

func (k *Kubelet) registerNode() error {
    node := &api.Node{
        ObjectMeta: api.ObjectMeta{
            Name: k.nodeName,
        },
        Spec: api.NodeSpec{
            Capacity: api.ResourceList{
                api.ResourceCPU:    "4",
                api.ResourceMemory: "8Gi",
                api.ResourcePods:   "110",
            },
        },
        Status: api.NodeStatus{
            Phase: api.NodeRunning,
            Capacity: api.ResourceList{
                api.ResourceCPU:    "4",
                api.ResourceMemory: "8Gi",
                api.ResourcePods:   "110",
            },
            Allocatable: api.ResourceList{
                api.ResourceCPU:    "4",
                api.ResourceMemory: "8Gi",
                api.ResourcePods:   "110",
            },
            Addresses: []api.NodeAddress{
                {
                    Type:    "InternalIP",
                    Address: "127.0.0.1",
                },
            },
        },
    }

    // 尝试更新节点（如果已存在）
    if err := k.kubelet.UpdateNode(node); err != nil {
        // 如果节点不存在，创建节点
        return k.kubelet.CreateNode(node)
    }

    return nil
}

// pkg/kubelet/kubelet.go
package kubelet

import (
    "context"
    "log"
    "os/exec"
    "time"

    "github.com/simple-k8s/pkg/api"
    "github.com/simple-k8s/pkg/client"
)

type Kubelet struct {
    client    client.Client
    nodeName  string
    podManager *PodManager
}

func NewKubelet(client client.Client, nodeName string) *Kubelet {
    return &Kubelet{
        client:     client,
        nodeName:   nodeName,
        podManager: NewPodManager(),
    }
}

func (k *Kubelet) Run(ctx context.Context) {
    // 启动Pod同步循环
    go k.syncPods(ctx)

    // 启动节点状态更新
    go k.updateNodeStatus(ctx)

    <-ctx.Done()
    log.Println("Kubelet stopped")
}

func (k *Kubelet) syncPods(ctx context.Context) {
    ticker := time.NewTicker(5 * time.Second)
    defer ticker.Stop()

    for {
        select {
        case <-ctx.Done():
            return
        case <-ticker.C:
            k.syncAssignedPods()
        }
    }
}

func (k *Kubelet) syncAssignedPods() {
    pods, err := k.client.ListPods()
    if err != nil {
        log.Printf("Error listing pods: %v", err)
        return
    }

    for _, pod := range pods {
        if pod.Spec.NodeName == k.nodeName {
            if err := k.syncPod(pod); err != nil {
                log.Printf("Error syncing pod %s/%s: %v", pod.Namespace, pod.Name, err)
            }
        }
    }
}

func (k *Kubelet) syncPod(pod *api.Pod) error {
    log.Printf("Syncing pod %s/%s on node %s", pod.Namespace, pod.Name, k.nodeName)

    // 检查Pod是否应该运行
    if pod.DeletionTimestamp != nil {
        return k.stopPod(pod)
    }

    return k.startPod(pod)
}

func (k *Kubelet) startPod(pod *api.Pod) error {
    // 简化的Pod启动逻辑
    for _, container := range pod.Spec.Containers {
        if err := k.startContainer(pod, container); err != nil {
            return err
        }
    }

    pod.Status.Phase = api.PodRunning
    pod.Status.Message = "Pod is running"

    return k.client.UpdatePod(pod)
}

func (k *Kubelet) stopPod(pod *api.Pod) error {
    // 简化的Pod停止逻辑
    for _, container := range pod.Spec.Containers {
        if err := k.stopContainer(pod, container); err != nil {
            return err
        }
    }

    pod.Status.Phase = api.PodFailed
    pod.Status.Message = "Pod stopped"

    return k.client.UpdatePod(pod)
}

func (k *Kubelet) startContainer(pod *api.Pod, container api.Container) error {
    // 简化的容器启动逻辑（使用docker run）
    cmd := exec.Command("docker", "run", "-d", "--name", k.getContainerName(pod, container), container.Image)

    if err := cmd.Run(); err != nil {
        return fmt.Errorf("error starting container %s: %v", container.Name, err)
    }

    log.Printf("Started container %s for pod %s/%s", container.Name, pod.Namespace, pod.Name)
    return nil
}

func (k *Kubelet) stopContainer(pod *api.Pod, container api.Container) error {
    // 简化的容器停止逻辑
    containerName := k.getContainerName(pod, container)
    cmd := exec.Command("docker", "stop", containerName)

    if err := cmd.Run(); err != nil {
        log.Printf("Error stopping container %s: %v", container.Name, err)
    }

    cmd = exec.Command("docker", "rm", containerName)
    if err := cmd.Run(); err != nil {
        log.Printf("Error removing container %s: %v", container.Name, err)
    }

    log.Printf("Stopped container %s for pod %s/%s", container.Name, pod.Namespace, pod.Name)
    return nil
}

func (k *Kubelet) getContainerName(pod *api.Pod, container api.Container) string {
    return fmt.Sprintf("k8s_%s_%s_%s", pod.Name, container.Name, pod.UID[:8])
}

func (k *Kubelet) updateNodeStatus(ctx context.Context) {
    ticker := time.NewTicker(10 * time.Second)
    defer ticker.Stop()

    for {
        select {
        case <-ctx.Done():
            return
        case <-ticker.C:
            k.updateNodeStatusInternal()
        }
    }
}

func (k *Kubelet) updateNodeStatusInternal() {
    node, err := k.client.GetNode(k.nodeName)
    if err != nil {
        log.Printf("Error getting node %s: %v", k.nodeName, err)
        return
    }

    // 更新节点状态
    node.Status.Phase = api.NodeRunning

    if err := k.client.UpdateNode(node); err != nil {
        log.Printf("Error updating node %s: %v", k.nodeName, err)
    }
}

func (k *Kubelet) CreateNode(node *api.Node) error {
    return k.client.CreateNode(node)
}

func (k *Kubelet) UpdateNode(node *api.Node) error {
    return k.client.UpdateNode(node)
}
```

## 简化版客户端

### HTTP客户端

```go
// pkg/client/client.go
package client

import (
    "bytes"
    "encoding/json"
    "fmt"
    "net/http"

    "github.com/simple-k8s/pkg/api"
)

type Client struct {
    baseURL    string
    httpClient *http.Client
}

func NewClient(baseURL string) *Client {
    return &Client{
        baseURL:    baseURL,
        httpClient: &http.Client{},
    }
}

func (c *Client) CreatePod(pod *api.Pod) error {
    url := fmt.Sprintf("%s/api/v1/pods", c.baseURL)

    data, err := json.Marshal(pod)
    if err != nil {
        return err
    }

    resp, err := c.httpClient.Post(url, "application/json", bytes.NewBuffer(data))
    if err != nil {
        return err
    }
    defer resp.Body.Close()

    if resp.StatusCode != http.StatusCreated {
        return fmt.Errorf("unexpected status code: %d", resp.StatusCode)
    }

    return nil
}

func (c *Client) GetPod(namespace, name string) (*api.Pod, error) {
    url := fmt.Sprintf("%s/api/v1/pods/%s/%s", c.baseURL, namespace, name)

    resp, err := c.httpClient.Get(url)
    if err != nil {
        return nil, err
    }
    defer resp.Body.Close()

    if resp.StatusCode != http.StatusOK {
        return nil, fmt.Errorf("unexpected status code: %d", resp.StatusCode)
    }

    var pod api.Pod
    if err := json.NewDecoder(resp.Body).Decode(&pod); err != nil {
        return nil, err
    }

    return &pod, nil
}

func (c *Client) UpdatePod(pod *api.Pod) error {
    url := fmt.Sprintf("%s/api/v1/pods/%s/%s", c.baseURL, pod.Namespace, pod.Name)

    data, err := json.Marshal(pod)
    if err != nil {
        return err
    }

    req, err := http.NewRequest("PUT", url, bytes.NewBuffer(data))
    if err != nil {
        return err
    }
    req.Header.Set("Content-Type", "application/json")

    resp, err := c.httpClient.Do(req)
    if err != nil {
        return err
    }
    defer resp.Body.Close()

    if resp.StatusCode != http.StatusOK {
        return fmt.Errorf("unexpected status code: %d", resp.StatusCode)
    }

    return nil
}

func (c *Client) DeletePod(namespace, name string) error {
    url := fmt.Sprintf("%s/api/v1/pods/%s/%s", c.baseURL, namespace, name)

    req, err := http.NewRequest("DELETE", url, nil)
    if err != nil {
        return err
    }

    resp, err := c.httpClient.Do(req)
    if err != nil {
        return err
    }
    defer resp.Body.Close()

    if resp.StatusCode != http.StatusNoContent {
        return fmt.Errorf("unexpected status code: %d", resp.StatusCode)
    }

    return nil
}

func (c *Client) ListPods() ([]*api.Pod, error) {
    url := fmt.Sprintf("%s/api/v1/pods", c.baseURL)

    resp, err := c.httpClient.Get(url)
    if err != nil {
        return nil, err
    }
    defer resp.Body.Close()

    if resp.StatusCode != http.StatusOK {
        return nil, fmt.Errorf("unexpected status code: %d", resp.StatusCode)
    }

    var podList api.PodList
    if err := json.NewDecoder(resp.Body).Decode(&podList); err != nil {
        return nil, err
    }

    return podList.Items, nil
}

func (c *Client) CreateNode(node *api.Node) error {
    url := fmt.Sprintf("%s/api/v1/nodes", c.baseURL)

    data, err := json.Marshal(node)
    if err != nil {
        return err
    }

    resp, err := c.httpClient.Post(url, "application/json", bytes.NewBuffer(data))
    if err != nil {
        return err
    }
    defer resp.Body.Close()

    if resp.StatusCode != http.StatusCreated {
        return fmt.Errorf("unexpected status code: %d", resp.StatusCode)
    }

    return nil
}

func (c *Client) GetNode(name string) (*api.Node, error) {
    url := fmt.Sprintf("%s/api/v1/nodes/%s", c.baseURL, name)

    resp, err := c.httpClient.Get(url)
    if err != nil {
        return nil, err
    }
    defer resp.Body.Close()

    if resp.StatusCode != http.StatusOK {
        return nil, fmt.Errorf("unexpected status code: %d", resp.StatusCode)
    }

    var node api.Node
    if err := json.NewDecoder(resp.Body).Decode(&node); err != nil {
        return nil, err
    }

    return &node, nil
}

func (c *Client) UpdateNode(node *api.Node) error {
    url := fmt.Sprintf("%s/api/v1/nodes/%s", c.baseURL, node.Name)

    data, err := json.Marshal(node)
    if err != nil {
        return err
    }

    req, err := http.NewRequest("PUT", url, bytes.NewBuffer(data))
    if err != nil {
        return err
    }
    req.Header.Set("Content-Type", "application/json")

    resp, err := c.httpClient.Do(req)
    if err != nil {
        return err
    }
    defer resp.Body.Close()

    if resp.StatusCode != http.StatusOK {
        return fmt.Errorf("unexpected status code: %d", resp.StatusCode)
    }

    return nil
}

func (c *Client) DeleteNode(name string) error {
    url := fmt.Sprintf("%s/api/v1/nodes/%s", c.baseURL, name)

    req, err := http.NewRequest("DELETE", url, nil)
    if err != nil {
        return err
    }

    resp, err := c.httpClient.Do(req)
    if err != nil {
        return err
    }
    defer resp.Body.Close()

    if resp.StatusCode != http.StatusNoContent {
        return fmt.Errorf("unexpected status code: %d", resp.StatusCode)
    }

    return nil
}

func (c *Client) ListNodes() ([]*api.Node, error) {
    url := fmt.Sprintf("%s/api/v1/nodes", c.baseURL)

    resp, err := c.httpClient.Get(url)
    if err != nil {
        return nil, err
    }
    defer resp.Body.Close()

    if resp.StatusCode != http.StatusOK {
        return nil, fmt.Errorf("unexpected status code: %d", resp.StatusCode)
    }

    var nodeList api.NodeList
    if err := json.NewDecoder(resp.Body).Decode(&nodeList); err != nil {
        return nil, err
    }

    return nodeList.Items, nil
}
```

## 使用示例

### 部署和使用

```bash
# 1. 启动API Server
go run cmd/simple-apiserver/main.go --port=8080

# 2. 启动Controller Manager
go run cmd/simple-controller/main.go --api-server=http://localhost:8080

# 3. 启动Scheduler
go run cmd/simple-scheduler/main.go --api-server=http://localhost:8080

# 4. 启动Kubelet (在多个终端中启动多个节点)
go run cmd/simple-kubelet/main.go --api-server=http://localhost:8080 --node-name=node1
go run cmd/simple-kubelet/main.go --api-server=http://localhost:8080 --node-name=node2

# 5. 创建Pod示例
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
          "containerPort": 80
        }]
      }]
    }
  }'

# 6. 查看Pod状态
curl http://localhost:8080/api/v1/pods

# 7. 查看节点状态
curl http://localhost:8080/api/v1/nodes
```

### 期望结果

1. Pod会被创建并分配到某个节点
2. Kubelet会在节点上启动容器
3. Pod状态会更新为Running
4. 节点状态会显示为Running
5. 控制器会持续监控Pod状态

## 总结

这个简化版的Kubernetes实现包含了核心功能：

- **API Server**: 提供RESTful API接口
- **Controller Manager**: 管理Pod和Node的生命周期
- **Scheduler**: 将Pod调度到合适的节点
- **Kubelet**: 在节点上管理Pod和容器

通过这个简化的实现，可以更好地理解Kubernetes的核心概念和工作原理。虽然缺少了生产环境的许多功能（如认证、授权、高级调度策略、网络插件等），但这个实现展示了Kubernetes的基本架构和控制循环模式。