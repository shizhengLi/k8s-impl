#include <gtest/gtest.h>
#include "../include/api/types.h"
#include <chrono>

using namespace k8s;

class ApiTypesTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 测试前的设置
    }

    void TearDown() override {
        // 测试后的清理
    }
};

TEST_F(ApiTypesTest, PodCreation) {
    Pod pod("test-pod", "default");

    EXPECT_EQ(pod.api_version, "v1");
    EXPECT_EQ(pod.kind, "Pod");
    EXPECT_EQ(pod.metadata.name, "test-pod");
    EXPECT_EQ(pod.metadata.namespace_, "default");
    EXPECT_FALSE(pod.metadata.uid.empty());
    EXPECT_NE(pod.metadata.creation_timestamp, std::chrono::system_clock::time_point{});
}

TEST_F(ApiTypesTest, ContainerCreation) {
    Container container("nginx", "nginx:latest");

    EXPECT_EQ(container.name, "nginx");
    EXPECT_EQ(container.image, "nginx:latest");
    EXPECT_TRUE(container.env.empty());
    EXPECT_TRUE(container.ports.empty());
}

TEST_F(ApiTypesTest, ContainerWithEnvAndPorts) {
    Container container("nginx", "nginx:latest");

    // 添加环境变量
    container.env.emplace_back("ENV_VAR1", "value1");
    container.env.emplace_back("ENV_VAR2", "value2");

    // 添加端口
    container.ports.push_back({80, "TCP", "http", 8080});
    container.ports.push_back({443, "TCP", "https", 8443});

    EXPECT_EQ(container.env.size(), 2);
    EXPECT_EQ(container.ports.size(), 2);
    EXPECT_EQ(container.env[0].name, "ENV_VAR1");
    EXPECT_EQ(container.ports[0].container_port, 80);
}

TEST_F(ApiTypesTest, ResourceQuantity) {
    ResourceQuantity cpu("100m");
    ResourceQuantity memory("1Gi");

    EXPECT_EQ(cpu.value, "100m");
    EXPECT_EQ(memory.value, "1Gi");

    EXPECT_TRUE(cpu == ResourceQuantity("100m"));
    EXPECT_FALSE(cpu == memory);
}

TEST_F(ApiTypesTest, ObjectMeta) {
    ObjectMeta meta("test-pod", "test-namespace");

    EXPECT_EQ(meta.name, "test-pod");
    EXPECT_EQ(meta.namespace_, "test-namespace");
    EXPECT_FALSE(meta.uid.empty());
    EXPECT_NE(meta.creation_timestamp, std::chrono::system_clock::time_point{});
}

TEST_F(ApiTypesTest, PodSpec) {
    PodSpec spec;
    spec.containers.emplace_back("nginx", "nginx:latest");
    spec.containers.emplace_back("redis", "redis:latest");
    spec.node_name = "node1";
    spec.restart_policy = true;

    EXPECT_EQ(spec.containers.size(), 2);
    EXPECT_EQ(spec.node_name, "node1");
    EXPECT_TRUE(spec.restart_policy);
}

TEST_F(ApiTypesTest, PodStatus) {
    PodStatus status;
    status.phase = PodPhase::Running;
    status.message = "Pod is running";
    status.reason = "Started";
    status.host_ip = "192.168.1.100";
    status.pod_ip = "10.244.0.5";

    EXPECT_EQ(status.phase, PodPhase::Running);
    EXPECT_EQ(status.message, "Pod is running");
    EXPECT_EQ(status.reason, "Started");
    EXPECT_EQ(status.host_ip, "192.168.1.100");
    EXPECT_EQ(status.pod_ip, "10.244.0.5");
}

TEST_F(ApiTypesTest, NodeCreation) {
    Node node("test-node");

    EXPECT_EQ(node.api_version, "v1");
    EXPECT_EQ(node.kind, "Node");
    EXPECT_EQ(node.metadata.name, "test-node");
    EXPECT_FALSE(node.metadata.uid.empty());
}

TEST_F(ApiTypesTest, NodeSpec) {
    NodeSpec spec;
    spec.capacity["cpu"] = ResourceQuantity("4");
    spec.capacity["memory"] = ResourceQuantity("16Gi");
    spec.pod_cidr = "10.244.0.0/24";

    EXPECT_EQ(spec.capacity.size(), 2);
    EXPECT_EQ(spec.capacity["cpu"].value, "4");
    EXPECT_EQ(spec.pod_cidr, "10.244.0.0/24");
}

TEST_F(ApiTypesTest, NodeStatus) {
    NodeStatus status;
    status.phase = NodePhase::Running;
    status.capacity["cpu"] = ResourceQuantity("4");
    status.allocatable["cpu"] = ResourceQuantity("3800m");
    status.addresses.push_back({"InternalIP", "192.168.1.100"});
    status.addresses.push_back({"Hostname", "node1"});

    EXPECT_EQ(status.phase, NodePhase::Running);
    EXPECT_EQ(status.capacity.size(), 1);
    EXPECT_EQ(status.addresses.size(), 2);
    EXPECT_EQ(status.addresses[0].type, "InternalIP");
}

TEST_F(ApiTypesTest, PodPhaseToString) {
    EXPECT_EQ(phase_to_string(PodPhase::Pending), "Pending");
    EXPECT_EQ(phase_to_string(PodPhase::Running), "Running");
    EXPECT_EQ(phase_to_string(PodPhase::Succeeded), "Succeeded");
    EXPECT_EQ(phase_to_string(PodPhase::Failed), "Failed");
    EXPECT_EQ(phase_to_string(PodPhase::Unknown), "Unknown");
}

TEST_F(ApiTypesTest, StringToPodPhase) {
    EXPECT_EQ(string_to_pod_phase("Pending"), PodPhase::Pending);
    EXPECT_EQ(string_to_pod_phase("Running"), PodPhase::Running);
    EXPECT_EQ(string_to_pod_phase("Succeeded"), PodPhase::Succeeded);
    EXPECT_EQ(string_to_pod_phase("Failed"), PodPhase::Failed);
    EXPECT_EQ(string_to_pod_phase("Unknown"), PodPhase::Unknown);
    EXPECT_EQ(string_to_pod_phase("Invalid"), PodPhase::Unknown);
}

TEST_F(ApiTypesTest, NodePhaseToString) {
    EXPECT_EQ(phase_to_string(NodePhase::Pending), "Pending");
    EXPECT_EQ(phase_to_string(NodePhase::Running), "Running");
    EXPECT_EQ(phase_to_string(NodePhase::Terminated), "Terminated");
    EXPECT_EQ(phase_to_string(NodePhase::Unknown), "Unknown");
}

TEST_F(ApiTypesTest, StringToNodePhase) {
    EXPECT_EQ(string_to_node_phase("Pending"), NodePhase::Pending);
    EXPECT_EQ(string_to_node_phase("Running"), NodePhase::Running);
    EXPECT_EQ(string_to_node_phase("Terminated"), NodePhase::Terminated);
    EXPECT_EQ(string_to_node_phase("Unknown"), NodePhase::Unknown);
    EXPECT_EQ(string_to_node_phase("Invalid"), NodePhase::Unknown);
}

TEST_F(ApiTypesTest, PodEquality) {
    Pod pod1("test-pod", "default");
    Pod pod2("test-pod", "default");
    Pod pod3("another-pod", "default");

    // 相同UID的pod应该相等
    pod2.metadata.uid = pod1.metadata.uid;
    EXPECT_TRUE(pod1 == pod2);

    // 不同UID的pod应该不相等
    EXPECT_FALSE(pod1 == pod3);
}

TEST_F(ApiTypesTest, NodeEquality) {
    Node node1("test-node");
    Node node2("test-node");
    Node node3("another-node");

    // 相同UID的node应该相等
    node2.metadata.uid = node1.metadata.uid;
    EXPECT_TRUE(node1 == node2);

    // 不同UID的node应该不相等
    EXPECT_FALSE(node1 == node3);
}

TEST_F(ApiTypesTest, ContainerPortEquality) {
    ContainerPort port1{80, "TCP", "http", 8080};
    ContainerPort port2{80, "TCP", "http", 8080};
    ContainerPort port3{8080, "TCP", "http", 8080};

    EXPECT_TRUE(port1 == port2);
    EXPECT_FALSE(port1 == port3);
}

TEST_F(ApiTypesTest, EnvVarEquality) {
    EnvVar env1{"NAME", "VALUE"};
    EnvVar env2{"NAME", "VALUE"};
    EnvVar env3{"NAME", "DIFFERENT"};

    EXPECT_TRUE(env1 == env2);
    EXPECT_FALSE(env1 == env3);
}

TEST_F(ApiTypesTest, NodeAddressEquality) {
    NodeAddress addr1{"InternalIP", "192.168.1.100"};
    NodeAddress addr2{"InternalIP", "192.168.1.100"};
    NodeAddress addr3{"ExternalIP", "192.168.1.100"};

    EXPECT_TRUE(addr1 == addr2);
    EXPECT_FALSE(addr1 == addr3);
}