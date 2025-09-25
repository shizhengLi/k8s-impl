# Kubernetes Controller Manager 实现指南

## 概述

Controller Manager是Kubernetes控制平面的核心组件，负责运行各种控制器，通过控制循环确保集群状态达到期望状态。本文档深入分析Controller Manager的实现细节。

## 架构设计

### 核心组件

```go
// Controller Manager核心结构
type ControllerManager struct {
    client          clientset.Interface
    informerFactory informers.SharedInformerFactory

    // 各种控制器
    deploymentController  *deployment.DeploymentController
    replicaSetController  *replicaset.ReplicaSetController
    nodeController        *node.NodeController
    serviceController     *service.ServiceController
    namespaceController   *namespace.NamespaceController
    // ... 其他控制器
}

// 控制器接口
type Controller interface {
    Run(workers int, stopCh <-chan struct{})
    HasSynced() bool
    LastSync() time.Time
}
```

### 启动流程

```go
// Controller Manager启动流程
func Run(ctx context.Context, s *options.CompletedOptions) error {
    // 1. 创建客户端
    kubeClient, leaderElectionClient, clientBuilder, err := createClients(s)
    if err != nil {
        return err
    }

    // 2. 创建Informer工厂
    informerFactory := informers.NewSharedInformerFactory(kubeClient, s.ResyncPeriod.Duration)

    // 3. 创建控制器管理器
    controllerManager, err := NewControllerManager(ctx, kubeClient, informerFactory, s)
    if err != nil {
        return err
    }

    // 4. 启动控制器管理器
    if err := controllerManager.Run(ctx, s.ConcurrentServiceSyncs); err != nil {
        return err
    }

    return nil
}
```

## 控制器模式实现

### 控制循环模式

```go
// 通用控制器结构
type Controller struct {
    queue    workqueue.RateLimitingInterface
    informer cache.SharedIndexInformer
    client   clientset.Interface

    // 事件处理器
    eventHandler cache.ResourceEventHandlerFuncs

    // 同步函数
    syncHandler func(ctx context.Context, key string) error

    // 列表器
    lister cache.GenericLister
    synced cache.InformerSynced
}

// 控制器运行循环
func (c *Controller) Run(workers int, stopCh <-chan struct{}) {
    defer runtime.HandleCrash()
    defer c.queue.ShutDown()

    // 等待缓存同步
    if !cache.WaitForCacheSync(stopCh, c.synced) {
        runtime.HandleError(fmt.Errorf("timed out waiting for caches to sync"))
        return
    }

    // 启动worker
    for i := 0; i < workers; i++ {
        go wait.Until(c.worker, time.Second, stopCh)
    }

    <-stopCh
}

// Worker处理队列中的项目
func (c *Controller) worker() {
    for c.processNextWorkItem() {
    }
}

// 处理队列中的下一个项目
func (c *Controller) processNextWorkItem() bool {
    key, quit := c.queue.Get()
    if quit {
        return false
    }
    defer c.queue.Done(key)

    err := c.syncHandler(context.Background(), key.(string))
    if err != nil {
        c.queue.AddRateLimited(key)
        runtime.HandleError(fmt.Errorf("sync %q failed with %v", key, err))
        return true
    }

    c.queue.Forget(key)
    return true
}
```

## Deployment Controller实现

### Deployment Controller结构

```go
// Deployment控制器结构
type DeploymentController struct {
    rsControl controller.RSControlInterface
    client    clientset.Interface

    eventBroadcaster record.EventBroadcaster
    eventRecorder    record.EventRecorder

    // 同步函数（用于测试）
    syncHandler func(ctx context.Context, dKey string) error
    enqueueDeployment func(deployment *apps.Deployment)

    // 列表器
    dLister  appslisters.DeploymentLister
    rsLister appslisters.ReplicaSetLister
    podLister corelisters.PodLister

    // 同步状态
    dListerSynced  cache.InformerSynced
    rsListerSynced cache.InformerSynced
    podListerSynced cache.InformerSynced

    // 工作队列
    queue workqueue.TypedRateLimitingInterface[string]
}

// 创建Deployment控制器
func NewDeploymentController(
    ctx context.Context,
    dInformer appsinformers.DeploymentInformer,
    rsInformer appsinformers.ReplicaSetInformer,
    podInformer coreinformers.PodInformer,
    client clientset.Interface,
) (*DeploymentController, error) {

    eventBroadcaster := record.NewBroadcaster(record.WithContext(ctx))
    dc := &DeploymentController{
        client:           client,
        eventBroadcaster: eventBroadcaster,
        eventRecorder:    eventBroadcaster.NewRecorder(scheme.Scheme, v1.EventSource{Component: "deployment-controller"}),
        queue: workqueue.NewTypedRateLimitingQueueWithConfig(
            workqueue.DefaultTypedControllerRateLimiter[string](),
            workqueue.TypedRateLimitingQueueConfig[string]{Name: "deployment"},
        ),
    }

    // 设置事件处理器
    dInformer.Informer().AddEventHandler(cache.ResourceEventHandlerFuncs{
        AddFunc: func(obj interface{}) {
            dc.addDeployment(obj)
        },
        UpdateFunc: func(oldObj, newObj interface{}) {
            dc.updateDeployment(oldObj, newObj)
        },
        DeleteFunc: func(obj interface{}) {
            dc.deleteDeployment(obj)
        },
    })

    // 设置同步函数
    dc.syncHandler = dc.syncDeployment
    dc.enqueueDeployment = dc.enqueue

    return dc, nil
}
```

### Deployment同步逻辑

```go
// Deployment同步函数
func (dc *DeploymentController) syncDeployment(ctx context.Context, key string) error {
    namespace, name, err := cache.SplitMetaNamespaceKey(key)
    if err != nil {
        return err
    }

    // 获取Deployment
    deployment, err := dc.dLister.Deployments(namespace).Get(name)
    if errors.IsNotFound(err) {
        return nil
    }
    if err != nil {
        return err
    }

    // 获取所有相关的ReplicaSet
    rsList, err := dc.getReplicaSetsForDeployment(deployment)
    if err != nil {
        return err
    }

    // 获取所有相关的Pod
    podMap, err := dc.getPodsForDeployment(deployment, rsList)
    if err != nil {
        return err
    }

    // 检查Deployment是否处于删除状态
    if deployment.DeletionTimestamp != nil {
        return dc.syncStatusOnly(ctx, deployment, rsList)
    }

    // 检查是否暂停
    if deployment.Spec.Paused {
        return dc.sync(ctx, deployment, rsList)
    }

    // 获取新的ReplicaSet
    newRS, oldRSs, err := dc.getAllReplicaSetsAndSyncRevision(ctx, deployment, rsList, false)
    if err != nil {
        return err
    }

    // 检查是否需要回滚
    if err := dc.checkRollback(ctx, deployment, rsList); err != nil {
        return err
    }

    // 执行滚动更新
    if err := dc.rollout(ctx, deployment, rsList, podMap); err != nil {
        return err
    }

    // 同步Deployment状态
    return dc.syncStatusOnly(ctx, deployment, rsList)
}

// 滚动更新逻辑
func (dc *DeploymentController) rollout(ctx context.Context, deployment *apps.Deployment, rsList []*apps.ReplicaSet, podMap map[string][]*v1.Pod) error {
    newRS, oldRSs, err := dc.getAllReplicaSetsAndSyncRevision(ctx, deployment, rsList, false)
    if err != nil {
        return err
    }

    allRSs := append(oldRSs, newRS)

    // 扩缩容逻辑
    if err := dc.scale(ctx, deployment, newRS, oldRSs); err != nil {
        return err
    }

    // 检查是否需要滚动更新
    if deploymentutil.IsRollingUpdate(deployment) {
        return dc.rollingRollout(ctx, deployment, rsList, podMap)
    }

    // 检查是否需要重新创建
    if deploymentutil.IsRecreate(deployment) {
        return dc.recreateRollout(ctx, deployment, rsList, podMap)
    }

    return nil
}
```

## ReplicaSet Controller实现

### ReplicaSet Controller结构

```go
// ReplicaSet控制器结构
type ReplicaSetController struct {
    kubeClient clientset.Interface
    podControl controller.PodControlInterface

    eventBroadcaster record.EventBroadcaster
    eventRecorder    record.EventRecorder

    // 同步函数
    syncHandler func(ctx context.Context, key string) error

    // 列表器
    rsLister appslisters.ReplicaSetLister
    podLister corelisters.PodLister

    // 同步状态
    rsListerSynced cache.InformerSynced
    podListerSynced cache.InformerSynced

    // 工作队列
    queue workqueue.TypedRateLimitingInterface[string]
}

// ReplicaSet同步逻辑
func (rsc *ReplicaSetController) syncReplicaSet(ctx context.Context, key string) error {
    namespace, name, err := cache.SplitMetaNamespaceKey(key)
    if err != nil {
        return err
    }

    // 获取ReplicaSet
    rs, err := rsc.rsLister.ReplicaSets(namespace).Get(name)
    if errors.IsNotFound(err) {
        return nil
    }
    if err != nil {
        return err
    }

    // 获取所有相关的Pod
    selector, err := metav1.LabelSelectorAsSelector(rs.Spec.Selector)
    if err != nil {
        return err
    }

    pods, err := rsc.podLister.Pods(namespace).List(selector)
    if err != nil {
        return err
    }

    // 过滤活跃的Pod
    activePods := controller.FilterActivePods(pods)

    // 计算期望副本数
    desiredReplicas := *rs.Spec.Replicas

    // 管理Pod生命周期
    if err := rsc.manageReplicas(ctx, rs, activePods, desiredReplicas); err != nil {
        return err
    }

    // 更新ReplicaSet状态
    return rsc.updateReplicaSetStatus(ctx, rs, activePods)
}

// 管理Pod副本数
func (rsc *ReplicaSetController) manageReplicas(ctx context.Context, rs *apps.ReplicaSet, activePods []*v1.Pod, desiredReplicas int32) error {
    currentReplicas := int32(len(activePods))

    // 计算差异
    diff := desiredReplicas - currentReplicas

    if diff < 0 {
        // 需要删除Pod
        if err := rsc.deletePods(ctx, rs, activePods, -diff); err != nil {
            return err
        }
    } else if diff > 0 {
        // 需要创建Pod
        if err := rsc.createPods(ctx, rs, diff); err != nil {
            return err
        }
    }

    return nil
}
```

## Node Controller实现

### Node Controller结构

```go
// Node控制器结构
type NodeController struct {
    kubeClient clientset.Interface

    // 列表器
    nodeLister corelisters.NodeLister
    podLister  corelisters.PodLister

    // 同步状态
    nodeListerSynced cache.InformerSynced
    podListerSynced  cache.InformerSynced

    // 工作队列
    queue workqueue.TypedRateLimitingInterface[string]

    // 节点监控
    nodeMonitorPeriod time.Duration
    nodeMonitorGracePeriod time.Duration
    nodeStartupGracePeriod time.Duration
    nodeEvictionRate time.Duration
}

// Node同步逻辑
func (nc *NodeController) syncNode(ctx context.Context, key string) error {
    name, err := cache.SplitMetaNamespaceKey(key)
    if err != nil {
        return err
    }

    // 获取节点
    node, err := nc.nodeLister.Get(name)
    if errors.IsNotFound(err) {
        return nil
    }
    if err != nil {
        return err
    }

    // 检查节点状态
    if nc.isNodeHealthy(node) {
        // 节点健康，检查是否需要取消标记
        if nc.isNodeMarked(node) {
            return nc.unmarkNodeForEviction(ctx, node)
        }
        return nil
    }

    // 节点不健康，标记驱逐
    if !nc.isNodeMarked(node) {
        return nc.markNodeForEviction(ctx, node)
    }

    // 检查是否需要驱逐Pod
    if nc.shouldEvictPods(node) {
        return nc.evictPodsFromNode(ctx, node)
    }

    return nil
}

// 节点健康检查
func (nc *NodeController) isNodeHealthy(node *v1.Node) bool {
    // 检查Ready状态
    for _, condition := range node.Status.Conditions {
        if condition.Type == v1.NodeReady {
            return condition.Status == v1.ConditionTrue
        }
    }
    return false
}
```

## Service Controller实现

### Service Controller结构

```go
// Service控制器结构
type ServiceController struct {
    kubeClient clientset.Interface

    // 列表器
    serviceLister corelisters.ServiceLister
    endpointsLister corelisters.EndpointsLister
    podLister      corelisters.PodLister

    // 同步状态
    serviceListerSynced  cache.InformerSynced
    endpointsListerSynced cache.InformerSynced
    podListerSynced      cache.InformerSynced

    // 工作队列
    queue workqueue.TypedRateLimitingInterface[string]
}

// Service同步逻辑
func (sc *ServiceController) syncService(ctx context.Context, key string) error {
    namespace, name, err := cache.SplitMetaNamespaceKey(key)
    if err != nil {
        return err
    }

    // 获取Service
    service, err := sc.serviceLister.Services(namespace).Get(name)
    if errors.IsNotFound(err) {
        return nil
    }
    if err != nil {
        return err
    }

    // 获取所有相关的Pod
    selector, err := metav1.LabelSelectorAsSelector(service.Spec.Selector)
    if err != nil {
        return err
    }

    pods, err := sc.podLister.Pods(namespace).List(selector)
    if err != nil {
        return err
    }

    // 过滤就绪的Pod
    readyPods := controller.FilterReadyPods(pods)

    // 创建或更新Endpoints
    return sc.syncEndpoints(ctx, service, readyPods)
}

// 同步Endpoints
func (sc *ServiceController) syncEndpoints(ctx context.Context, service *v1.Service, readyPods []*v1.Pod) error {
    // 构建EndpointSubsets
    subsets := []v1.EndpointSubset{}

    for _, pod := range readyPods {
        subset := v1.EndpointSubset{
            Addresses: []v1.EndpointAddress{
                {
                    IP: pod.Status.PodIP,
                },
            },
            Ports: service.Spec.Ports,
        }
        subsets = append(subsets, subset)
    }

    // 创建Endpoints对象
    endpoints := &v1.Endpoints{
        ObjectMeta: metav1.ObjectMeta{
            Name:      service.Name,
            Namespace: service.Namespace,
        },
        Subsets: subsets,
    }

    // 更新或创建Endpoints
    existing, err := sc.endpointsLister.Endpoints(service.Namespace).Get(service.Name)
    if errors.IsNotFound(err) {
        _, err = sc.kubeClient.CoreV1().Endpoints(service.Namespace).Create(ctx, endpoints, metav1.CreateOptions{})
        return err
    }

    if err != nil {
        return err
    }

    // 更新现有的Endpoints
    existing.Subsets = subsets
    _, err = sc.kubeClient.CoreV1().Endpoints(service.Namespace).Update(ctx, existing, metav1.UpdateOptions{})
    return err
}
```

## 控制器管理器协调

### 控制器注册和启动

```go
// 控制器管理器
type ControllerManager struct {
    controllers map[string]Controller
    informerFactory informers.SharedInformerFactory

    // 启动配置
    workers int
    resyncPeriod time.Duration
}

// 注册控制器
func (cm *ControllerManager) RegisterController(name string, controller Controller) {
    cm.controllers[name] = controller
}

// 启动所有控制器
func (cm *ControllerManager) Run(ctx context.Context) error {
    // 启动Informer工厂
    cm.informerFactory.Start(ctx.Done())

    // 等待缓存同步
    if !cache.WaitForCacheSync(ctx.Done(), cm.hasSynced...) {
        return fmt.Errorf("timed out waiting for controller caches to sync")
    }

    // 启动所有控制器
    for name, controller := range cm.controllers {
        go func(name string, controller Controller) {
            klog.Infof("Starting controller %s", name)
            controller.Run(cm.workers, ctx.Done())
        }(name, controller)
    }

    <-ctx.Done()
    return nil
}

// 检查所有控制器是否同步
func (cm *ControllerManager) hasSynced() []cache.InformerSynced {
    var synced []cache.InformerSynced

    for _, controller := range cm.controllers {
        if c, ok := controller.(interface{ HasSynced() bool }); ok {
            synced = append(synced, c.HasSynced)
        }
    }

    return synced
}
```

## 事件处理机制

### 事件广播和记录

```go
// 事件广播器
type EventBroadcaster interface {
    StartEventWatcher(handler func(event runtime.Object)) (watch.Interface, error)
    StartRecordingToSink(sink EventSink) (watch.Interface, error)
    NewRecorder(scheme *runtime.Scheme, source v1.EventSource) EventRecorder
}

// 事件记录器
type EventRecorder interface {
    Event(object runtime.Object, eventtype, reason, message string)
    Eventf(object runtime.Object, eventtype, reason, messageFmt string, args ...interface{})
    AnnotatedEventf(object runtime.Object, annotations map[string]string, eventtype, reason, messageFmt string, args ...interface{})
}

// 事件处理
func (dc *DeploymentController) addDeployment(obj interface{}) {
    deployment := obj.(*apps.Deployment)
    klog.V(4).Infof("Adding deployment %s/%s", deployment.Namespace, deployment.Name)

    dc.enqueueDeployment(deployment)
}

func (dc *DeploymentController) updateDeployment(oldObj, newObj interface{}) {
    oldDeployment := oldObj.(*apps.Deployment)
    newDeployment := newObj.(*apps.Deployment)

    // 如果资源版本相同，跳过
    if oldDeployment.ResourceVersion == newDeployment.ResourceVersion {
        return
    }

    klog.V(4).Infof("Updating deployment %s/%s", newDeployment.Namespace, newDeployment.Name)
    dc.enqueueDeployment(newDeployment)
}
```

## 健康检查和监控

### 控制器健康检查

```go
// 健康检查
func (dc *DeploymentController) HasSynced() bool {
    return dc.dListerSynced() && dc.rsListerSynced() && dc.podListerSynced()
}

// 最后同步时间
func (dc *DeploymentController) LastSync() time.Time {
    return time.Now() // 简化实现
}

// 控制器状态
func (cm *ControllerManager) CheckControllersHealth() map[string]bool {
    status := make(map[string]bool)

    for name, controller := range cm.controllers {
        if c, ok := controller.(interface{ HasSynced() bool }); ok {
            status[name] = c.HasSynced()
        } else {
            status[name] = false
        }
    }

    return status
}
```

## 性能优化

### 批量处理和限流

```go
// 批量处理Pod
func (rsc *ReplicaSetController) createPods(ctx context.Context, rs *apps.ReplicaSet, diff int32) error {
    // 批量创建Pod
    pods := make([]*v1.Pod, 0, diff)

    for i := int32(0); i < diff; i++ {
        pod := rsc.podControl.CreatePods(ctx, rs.Spec.Template, rs)
        pods = append(pods, pod)
    }

    // 批量提交
    if len(pods) > 0 {
        if err := rsc.podControl.CreatePodsBatch(ctx, pods); err != nil {
            return err
        }
    }

    return nil
}

// 指数退避限流
func (dc *DeploymentController) enqueueRateLimited(deployment *apps.Deployment) {
    key, err := controller.KeyFunc(deployment)
    if err != nil {
        return
    }

    // 使用指数退避算法
    dc.queue.AddRateLimited(key)
}
```

## 总结

Controller Manager是Kubernetes的核心组件，通过控制循环模式确保集群状态达到期望状态。每个控制器都遵循相同的模式：监听资源变化、处理队列中的任务、更新集群状态。通过理解Controller Manager的实现，可以更好地理解Kubernetes的工作原理。