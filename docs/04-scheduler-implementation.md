# Kubernetes Scheduler 实现指南

## 概述

Scheduler是Kubernetes的调度器组件，负责将Pod调度到合适的节点上。本文档深入分析Scheduler的实现细节，包括调度框架、插件机制和调度算法。

## 架构设计

### 核心组件

```go
// 调度器核心结构
type Scheduler struct {
    // 缓存系统
    Cache internalcache.Cache

    // 扩展器
    Extenders []fwk.Extender

    // 下一个Pod的获取函数
    NextPod func(logger klog.Logger) (*framework.QueuedPodInfo, error)

    // 失败处理器
    FailureHandler FailureHandlerFn

    // 调度Pod的函数
    SchedulePod func(ctx context.Context, fwk framework.Framework, state fwk.CycleState, pod *v1.Pod) (ScheduleResult, error)

    // 停止信号
    StopEverything <-chan struct{}

    // 调度队列
    SchedulingQueue internalqueue.SchedulingQueue

    // API调度器
    APIDispatcher *apidispatcher.APIDispatcher
}

// 调度结果
type ScheduleResult struct {
    SuggestedHost  string
    EvaluatedNodes int
    FeasibleNodes  int
}
```

### 调度框架

```go
// 调度框架接口
type Framework interface {
    // 调度周期
    RunSchedulingCycle(ctx context.Context, pod *v1.Pod, state CycleState) (ScheduleResult, error)

    // 插件生命周期
    PreEnqueue(ctx context.Context, pod *v1.Pod) *Status
    PreFilter(ctx context.Context, state CycleState, pod *v1.Pod) *Status
    Filter(ctx context.Context, state CycleState, pod *v1.Pod, nodeName string) *Status
    PostFilter(ctx context.Context, state CycleState, pod *v1.Pod, filteredNodeStatusMap NodeToStatusMap) (*PostFilterResult, *Status)
    PreScore(ctx context.Context, state CycleState, pod *v1.Pod, nodes []*v1.Node) *Status
    Score(ctx context.Context, state CycleState, pod *v1.Pod, nodeName string) (int64, *Status)
    NormalizeScore(ctx context.Context, state CycleState, pod *v1.Pod, scores NodeScoreList) *Status
    Reserve(ctx context.Context, state CycleState, pod *v1.Pod, nodeName string) *Status
    PreBind(ctx context.Context, state CycleState, pod *v1.Pod, nodeName string) *Status
    Bind(ctx context.Context, state CycleState, pod *v1.Pod, nodeName string) *Status
    PostBind(ctx context.Context, state CycleState, pod *v1.Pod, nodeName string) *Status
    Unreserve(ctx context.Context, state CycleState, pod *v1.Pod, nodeName string) *Status
    Permit(ctx context.Context, state CycleState, pod *v1.Pod, nodeName string) (time.Duration, *Status)
}

// 框架实现
type frameworkImpl struct {
    registry             Registry
    snapshotSharedLister fwk.SharedLister
    waitingPods          *waitingPodsMap
    scorePluginWeight    map[string]int

    // 各种插件
    preEnqueuePlugins []fwk.PreEnqueuePlugin
    enqueueExtensions []fwk.EnqueueExtensions
    queueSortPlugins  []fwk.QueueSortPlugin
    preFilterPlugins  []fwk.PreFilterPlugin
    filterPlugins     []fwk.FilterPlugin
    postFilterPlugins []fwk.PostFilterPlugin
    preScorePlugins   []fwk.PreScorePlugin
    scorePlugins      []fwk.ScorePlugin
    reservePlugins    []fwk.ReservePlugin
    preBindPlugins    []fwk.PreBindPlugin
    bindPlugins       []fwk.BindPlugin
    postBindPlugins   []fwk.PostBindPlugin
    permitPlugins     []fwk.PermitPlugin

    // 插件映射
    pluginsMap map[string]fwk.Plugin

    // 客户端和配置
    clientSet       clientset.Interface
    kubeConfig      *restclient.Config
    eventRecorder   events.EventRecorder
    informerFactory informers.SharedInformerFactory
}
```

## 调度流程

### 主调度循环

```go
// 调度器主循环
func (sched *Scheduler) Run(ctx context.Context) {
    defer sched.StopEverything <- struct{}{}

    // 等待缓存同步
    if !sched.Cache.WaitForCacheSync(ctx.Done()) {
        return
    }

    // 启动调度循环
    sched.SchedulingQueue.Run()

    // 等待停止信号
    wait.UntilWithContext(ctx, func(ctx context.Context) {
        sched.scheduleOne(ctx)
    }, 0)
}

// 调度单个Pod
func (sched *Scheduler) scheduleOne(ctx context.Context) {
    // 1. 从队列获取下一个Pod
    podInfo, err := sched.NextPod(klog.FromContext(ctx))
    if err != nil {
        return
    }

    // 2. 执行调度
    scheduleResult, err := sched.SchedulePod(ctx, sched.framework, podInfo.Pod)
    if err != nil {
        // 处理调度失败
        sched.FailureHandler(sched.framework, podInfo.Pod, err)
        return
    }

    // 3. 绑定Pod到节点
    if err := sched.Bind(ctx, podInfo.Pod, scheduleResult.SuggestedHost); err != nil {
        // 处理绑定失败
        sched.FailureHandler(sched.framework, podInfo.Pod, err)
        return
    }
}
```

### 调度算法实现

```go
// 执行调度
func (sched *Scheduler) SchedulePod(ctx context.Context, fwk framework.Framework, state fwk.CycleState, pod *v1.Pod) (ScheduleResult, error) {
    // 1. 预检查
    if err := fwk.RunPreFilterPlugins(ctx, state, pod); err != nil {
        return ScheduleResult{}, err
    }

    // 2. 获取所有节点
    allNodes, err := fwk.SnapshotSharedLister().NodeInfos().List()
    if err != nil {
        return ScheduleResult{}, err
    }

    // 3. 过滤节点
    filteredNodes, failedNodes, err := fwk.RunFilterPlugins(ctx, state, pod, allNodes)
    if err != nil {
        return ScheduleResult{}, err
    }

    // 4. 预评分
    if err := fwk.RunPreScorePlugins(ctx, state, pod, filteredNodes); err != nil {
        return ScheduleResult{}, err
    }

    // 5. 评分
    scoresMap, err := fwk.RunScorePlugins(ctx, state, pod, filteredNodes)
    if err != nil {
        return ScheduleResult{}, err
    }

    // 6. 选择最佳节点
    selectedNode, err := selectBestNode(filteredNodes, scoresMap, fwk)
    if err != nil {
        return ScheduleResult{}, err
    }

    return ScheduleResult{
        SuggestedHost:  selectedNode,
        EvaluatedNodes: len(allNodes),
        FeasibleNodes:  len(filteredNodes),
    }, nil
}

// 选择最佳节点
func selectBestNode(nodes []*v1.Node, scoresMap framework.NodeScoreList, fwk framework.Framework) (string, error) {
    if len(nodes) == 0 {
        return "", ErrNoNodesAvailable
    }

    // 1. 归一化分数
    if err := fwk.RunNormalizeScorePlugins(context.Background(), nil, nil, scoresMap); err != nil {
        return "", err
    }

    // 2. 计算总分
    nodeScores := make(map[string]int64)
    for _, score := range scoresMap {
        nodeScores[score.Name] += score.Score
    }

    // 3. 选择得分最高的节点
    var bestNode string
    var bestScore int64 = -1
    for node, score := range nodeScores {
        if score > bestScore {
            bestNode = node
            bestScore = score
        }
    }

    if bestNode == "" {
        return "", ErrNoNodesAvailable
    }

    return bestNode, nil
}
```

## 插件机制

### 插件接口

```go
// 插件接口
type Plugin interface {
    Name() string
}

// 预过滤插件
type PreFilterPlugin interface {
    Plugin
    PreFilter(ctx context.Context, state CycleState, pod *v1.Pod) *Status
}

// 过滤插件
type FilterPlugin interface {
    Plugin
    Filter(ctx context.Context, state CycleState, pod *v1.Pod, nodeName string) *Status
}

// 评分插件
type ScorePlugin interface {
    Plugin
    Score(ctx context.Context, state CycleState, pod *v1.Pod, nodeName string) (int64, *Status)
}

// 绑定插件
type BindPlugin interface {
    Plugin
    Bind(ctx context.Context, state CycleState, pod *v1.Pod, nodeName string) *Status
}
```

### 内置插件实现

#### NodeName插件

```go
// NodeName过滤器
type NodeName struct{}

func (pl *NodeName) Name() string {
    return "NodeName"
}

func (pl *NodeName) Filter(ctx context.Context, state CycleState, pod *v1.Pod, nodeName string) *Status {
    if pod.Spec.NodeName != "" {
        if pod.Spec.NodeName == nodeName {
            return nil
        }
        return framework.NewStatus(framework.UnschedulableAndUnresolvable, "NodeName doesn't match")
    }
    return nil
}
```

#### NodeAffinity插件

```go
// Node亲和性插件
type NodeAffinity struct {
    sharedLister framework.SharedLister
}

func (pl *NodeAffinity) Name() string {
    return "NodeAffinity"
}

func (pl *NodeAffinity) Filter(ctx context.Context, state CycleState, pod *v1.Pod, nodeName string) *Status {
    nodeInfo, err := pl.sharedLister.NodeInfos().Get(nodeName)
    if err != nil {
        return framework.AsStatus(err)
    }

    // 检查节点亲和性
    if !matchesNodeAffinity(pod.Spec.Affinity, nodeInfo.Node()) {
        return framework.NewStatus(framework.UnschedulableAndUnresolvable, "Node doesn't match affinity requirements")
    }

    return nil
}

func (pl *NodeAffinity) Score(ctx context.Context, state CycleState, pod *v1.Pod, nodeName string) (int64, *Status) {
    nodeInfo, err := pl.sharedLister.NodeInfos().Get(nodeName)
    if err != nil {
        return 0, framework.AsStatus(err)
    }

    // 计算亲和性分数
    score := calculateNodeAffinityScore(pod.Spec.Affinity, nodeInfo.Node())
    return int64(score), nil
}
```

#### Resource插件

```go
// 资源插件
type Resource struct {
    sharedLister framework.SharedLister
}

func (pl *Resource) Name() string {
    return "Resource"
}

func (pl *Resource) Filter(ctx context.Context, state CycleState, pod *v1.Pod, nodeName string) *Status {
    nodeInfo, err := pl.sharedLister.NodeInfos().Get(nodeName)
    if err != nil {
        return framework.AsStatus(err)
    }

    // 检查资源是否足够
    if !pl.hasEnoughResources(nodeInfo, pod) {
        return framework.NewStatus(framework.Unschedulable, "Node doesn't have enough resources")
    }

    return nil
}

func (pl *Resource) Score(ctx context.Context, state CycleState, pod *v1.Pod, nodeName string) (int64, *Status) {
    nodeInfo, err := pl.sharedLister.NodeInfos().Get(nodeName)
    if err != nil {
        return 0, framework.AsStatus(err)
    }

    // 计算资源分数
    score := pl.calculateResourceScore(nodeInfo, pod)
    return int64(score), nil
}

// 检查资源是否足够
func (pl *Resource) hasEnoughResources(nodeInfo *framework.NodeInfo, pod *v1.Pod) bool {
    podRequests := pl.calculatePodRequests(pod)
    allocatable := nodeInfo.Allocatable

    return podRequests.Cpu().Cmp(*allocatable.Cpu()) <= 0 &&
           podRequests.Memory().Cmp(*allocatable.Memory()) <= 0 &&
           podRequests.EphemeralStorage().Cmp(*allocatable.StorageEphemeral()) <= 0
}

// 计算资源分数
func (pl *Resource) calculateResourceScore(nodeInfo *framework.NodeInfo, pod *v1.Pod) int64 {
    podRequests := pl.calculatePodRequests(pod)
    allocatable := nodeInfo.Allocatable
    allocated := nodeInfo.Requested

    // 计算资源使用率
    cpuFraction := float64(allocated.Cpu().MilliValue()) / float64(allocatable.Cpu().MilliValue())
    memFraction := float64(allocated.Memory().Value()) / float64(allocatable.Memory().Value())

    // 计算资源分数（越低越好）
    score := int64((1 - cpuFraction) * 50 + (1 - memFraction) * 50)
    return score
}
```

## 缓存系统

### 调度器缓存

```go
// 缓存接口
type Cache interface {
    // 节点操作
    AddNode(node *v1.Node) error
    UpdateNode(oldNode, newNode *v1.Node) error
    RemoveNode(node *v1.Node) error

    // Pod操作
    AddPod(pod *v1.Pod) error
    UpdatePod(oldPod, newPod *v1.Pod) error
    RemovePod(pod *v1.Pod) error

    // 快照操作
    Snapshot() *Snapshot
    WaitForCacheSync(stopCh <-chan struct{}) bool
}

// 节点信息
type NodeInfo struct {
    node       *v1.Node
    pods       []*v1.Pod
    requested  *framework.Resource
    allocatable *framework.Resource
}

// 快照
type Snapshot struct {
    nodes         []*NodeInfo
    nodeInfoMap   map[string]*NodeInfo
}
```

## 调度队列

### 优先级队列

```go
// 调度队列接口
type SchedulingQueue interface {
    Add(pod *v1.Pod) error
    AddUnschedulableIfNotPresent(pod *framework.QueuedPodInfo) error
    Update(oldPod, newPod *v1.Pod) error
    Delete(pod *v1.Pod) error
    Pop() (*framework.QueuedPodInfo, error)
    Run()
    Close()
}

// 优先级队列实现
type PriorityQueue struct {
    activeQ      *heap.Heap
    unschedulableQ *UnschedulablePodsMap
    backoffQ     *backoffQueue
    movingCycle  int
}

// 添加Pod到队列
func (p *PriorityQueue) Add(pod *v1.Pod) error {
    // 检查是否已经在队列中
    if p.unschedulableQ.get(pod) != nil {
        return fmt.Errorf("pod %s is already in the unschedulable queue", pod.Name)
    }

    // 创建Pod信息
    podInfo := &framework.QueuedPodInfo{
        Pod:              pod,
        Timestamp:        time.Now(),
        InitialAttemptAt: time.Now(),
        Attempts:         0,
    }

    // 添加到活跃队列
    if err := p.activeQ.Add(podInfo); err != nil {
        return err
    }

    return nil
}

// 从队列中弹出Pod
func (p *PriorityQueue) Pop() (*framework.QueuedPodInfo, error) {
    // 首先尝试从活跃队列中弹出
    obj, err := p.activeQ.Pop()
    if err == nil {
        return obj.(*framework.QueuedPodInfo), nil
    }

    // 如果活跃队列为空，尝试从退避队列中弹出
    obj, err = p.backoffQ.Pop()
    if err == nil {
        return obj.(*framework.QueuedPodInfo), nil
    }

    return nil, fmt.Errorf("no pods available in the queue")
}
```

## 扩展器机制

### 扩展器接口

```go
// 扩展器接口
type Extender interface {
    Name() string

    // 过滤节点
    Filter(pod *v1.Pod, nodes []*v1.Node) (filteredNodes []*v1.Node, failedNodesMap extenderv1.FailedNodesMap, err error)

    // 优先级
    Prioritize(pod *v1.Pod, nodes []*v1.Node) (hostPriorities *extenderv1.HostPriorityList, weight int64, err error)

    // 绑定
    Bind(binding *v1.Binding) error

    // 是否支持调度过程
    IsIgnorable() bool

    // 是否支持抢占
    SupportsPreemption() bool
}

// HTTP扩展器
type HTTPExtender struct {
    httpClient  *http.Client
    extenderURL string
    ignorable   bool
}

func (e *HTTPExtender) Filter(pod *v1.Pod, nodes []*v1.Node) ([]*v1.Node, extenderv1.FailedNodesMap, error) {
    // 构建请求
    args := &extenderv1.ExtenderArgs{
        Pod:       pod,
        Nodes:     &v1.NodeList{Items: nodesToNodeList(nodes)},
        NodeNames: nodeNames(nodes),
    }

    // 发送HTTP请求
    result, err := e.sendRequest(args, "filter")
    if err != nil {
        return nil, nil, err
    }

    // 处理响应
    filteredNodes, failedNodesMap := processFilterResult(result, nodes)
    return filteredNodes, failedNodesMap, nil
}
```

## 性能优化

### 并行处理

```go
// 并行处理器
type Parallelizer struct {
    parallelism int
}

func (p *Parallelizer) Until(ctx context.Context, pieces []interface{}, doWorkPiece func(ctx context.Context, i int) error) error {
    var wg sync.WaitGroup
    errChan := make(chan error, len(pieces))
    sem := make(chan struct{}, p.parallelism)

    for i := range pieces {
        wg.Add(1)
        go func(index int) {
            defer wg.Done()
            sem <- struct{}{}
            defer func() { <-sem }()

            if err := doWorkPiece(ctx, index); err != nil {
                errChan <- err
            }
        }(i)
    }

    wg.Wait()
    close(errChan)

    // 收集错误
    var errors []error
    for err := range errChan {
        errors = append(errors, err)
    }

    if len(errors) > 0 {
        return utilerrors.NewAggregate(errors)
    }

    return nil
}
```

### 批量处理

```go
// 批量调度
func (sched *Scheduler) scheduleBatch(ctx context.Context, batchSize int) {
    for i := 0; i < batchSize; i++ {
        go func() {
            for {
                select {
                case <-ctx.Done():
                    return
                default:
                    sched.scheduleOne(ctx)
                }
            }
        }()
    }
}
```

## 监控和指标

### 调度指标

```go
// 调度器指标
type SchedulerMetrics struct {
    ScheduleAttempts prometheus.Counter
    ScheduleSuccess  prometheus.Counter
    ScheduleFailure  prometheus.Counter
    ScheduleLatency  prometheus.Histogram
    BindAttempts     prometheus.Counter
    BindSuccess      prometheus.Counter
    BindFailure      prometheus.Counter
    BindLatency      prometheus.Histogram
}

// 记录调度指标
func (m *SchedulerMetrics) RecordSchedule(pod *v1.Pod, success bool, latency time.Duration) {
    m.ScheduleAttempts.Inc()
    if success {
        m.ScheduleSuccess.Inc()
    } else {
        m.ScheduleFailure.Inc()
    }
    m.ScheduleLatency.Observe(latency.Seconds())
}
```

## 总结

Kubernetes Scheduler是一个高度可扩展的调度系统，通过插件机制和扩展器实现了灵活的调度策略。其核心思想是通过过滤、评分、绑定等步骤，将Pod调度到最合适的节点上。通过理解Scheduler的实现，可以更好地定制调度策略和优化集群性能。