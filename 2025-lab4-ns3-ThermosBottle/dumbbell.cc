#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/traffic-control-module.h"

#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>

using namespace ns3;

// - Network topology
//   - The dumbbell topology consists of
//     - 4 servers (S0, S1, R0, R1)
//     - 2 routers (T0, T1)
//   - The topology is as follows:
//
//                    S0                         R0
//     10 Mbps, 1 ms   |      1 Mbps, 10 ms       |   10 Mbps, 1 ms
//                    T0 ----------------------- T1
//     10 Mbps, 1 ms   |                          |   10 Mbps, 1 ms
//                    S1                         R1
//
// - Two TCP flows:
//   - TCP flow 0 from S0 to R0 using BulkSendApplication.
//   - TCP flow 1 from S1 to R1 using BulkSendApplication.

const uint32_t N1 = 2;          // Number of nodes in left side
const uint32_t N2 = 2;          // Number of nodes in right side
uint32_t segmentSize = 1448;    // Segment size
Time startTime = Seconds(10.0); // Start time for the simulation
Time stopTime = Seconds(60.0);  // Stop time for the simulation

// Global variables for FCT measurement
std::map<uint32_t, int64_t> lastRxTime;

// Rx callback function to track last packet reception
static void
OnRx(uint32_t flowId,
     Ptr<const Packet> packet,
     const TcpHeader& tcpHeader,
     Ptr<const TcpSocketBase> socket)
{
    // Update the last reception time for this flow
    lastRxTime[flowId] = Simulator::Now().GetMicroSeconds();
}

// Function to connect Rx trace source
static void
TraceFct(uint32_t flowId, Ptr<BulkSendApplication> app)
{
    app->GetSocket()->TraceConnectWithoutContext("Rx", MakeBoundCallback(&OnRx, flowId));
}

// Function to install BulkSend application and return the container
ApplicationContainer
InstallBulkSend(Ptr<Node> node,
                Ipv4Address address,
                uint16_t port,
                std::string socketFactory,
                uint32_t flowSize)
{
    BulkSendHelper source(socketFactory, InetSocketAddress(address, port));
    source.SetAttribute("MaxBytes", UintegerValue(flowSize));
    ApplicationContainer sourceApps = source.Install(node);
    sourceApps.Start(startTime);
    sourceApps.Stop(stopTime);
    return sourceApps;
}

// Function to install sink application
void
InstallPacketSink(Ptr<Node> node, uint16_t port, std::string socketFactory)
{
    PacketSinkHelper sink(socketFactory, InetSocketAddress(Ipv4Address::GetAny(), port));
    ApplicationContainer sinkApps = sink.Install(node);
    sinkApps.Start(startTime);
    sinkApps.Stop(stopTime);
}

// Cwnd change callback function
static void
CwndChange(std::string filePath, uint32_t oldCwnd, uint32_t newCwnd)
{
    static std::map<std::string, std::ofstream*> cwndStreams;

    if (cwndStreams.find(filePath) == cwndStreams.end())
    {
        cwndStreams[filePath] = new std::ofstream(filePath.c_str(), std::ofstream::out);
    }

    // Output format: <time_microseconds> <old_cwnd_segments> <new_cwnd_segments>
    *cwndStreams[filePath] << Simulator::Now().GetMicroSeconds() << " " << oldCwnd / segmentSize
                           << " " << newCwnd / segmentSize << std::endl;
}

// Function to trace cwnd
static void
TraceCwnd(uint32_t nodeId, std::string cwndTrFile)
{
    Config::ConnectWithoutContext("/NodeList/" + std::to_string(nodeId) +
                                      "/$ns3::TcpL4Protocol/SocketList/0/CongestionWindow",
                                  MakeBoundCallback(&CwndChange, cwndTrFile));
}

int
main(int argc, char* argv[])
{
    std::string socketFactory = "ns3::TcpSocketFactory"; // Socket factory to use
    std::string tcpTypeId = "ns3::TcpLinuxReno";         // TCP variant to use
    std::string qdiscTypeId = "ns3::FifoQueueDisc";      // Queue disc for gateway
    bool isSack = true;                                  // Flag to enable/disable sack in TCP
    uint32_t delAckCount = 1;                            // Delayed ack count
    std::string recovery = "ns3::TcpClassicRecovery";    // Recovery algorithm type to use
    uint32_t flowSize0 = 0;                              // Flow size 0 in bytes
    uint32_t flowSize1 = 0;                              // Flow size 1 in bytes

    // Parse command line arguments
    CommandLine cmd;
    cmd.AddValue("flowSize0", "Flow 0 size in bytes", flowSize0);
    cmd.AddValue("flowSize1", "Flow 1 size in bytes", flowSize1);
    cmd.Parse(argc, argv);

    // Output directory for PCAP and cwnd files
    std::string dir = "lv1-results/";

    // Create output directories
    std::string command = "mkdir -p " + dir + "pcap/";
    [[maybe_unused]] int ret = system(command.c_str());
    command = "mkdir -p " + dir + "cwnd/";
    ret = system(command.c_str());
    command = "mkdir -p " + dir + "fct/";
    ret = system(command.c_str());

    // Check if the qdiscTypeId and tcpTypeId are valid
    TypeId qdTid;
    NS_ABORT_MSG_UNLESS(TypeId::LookupByNameFailSafe(qdiscTypeId, &qdTid),
                        "TypeId " << qdiscTypeId << " not found");
    TypeId tcpTid;
    NS_ABORT_MSG_UNLESS(TypeId::LookupByNameFailSafe(tcpTypeId, &tcpTid),
                        "TypeId " << tcpTypeId << " not found");

    // Set recovery algorithm and TCP variant
    Config::SetDefault("ns3::TcpL4Protocol::RecoveryType",
                       TypeIdValue(TypeId::LookupByName(recovery)));
    Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                       TypeIdValue(TypeId::LookupByName(tcpTypeId)));

    // Create nodes
    NodeContainer leftNodes;
    NodeContainer rightNodes;
    NodeContainer routers;
    routers.Create(2);
    leftNodes.Create(N1);
    rightNodes.Create(N2);

    // Create the point-to-point link helpers and connect two router nodes
    PointToPointHelper pointToPointRouter;
    pointToPointRouter.SetDeviceAttribute("DataRate", StringValue("1Mbps"));
    pointToPointRouter.SetChannelAttribute("Delay", StringValue("10ms"));

    NetDeviceContainer routerToRouter = pointToPointRouter.Install(routers.Get(0), routers.Get(1));

    // Create the point-to-point link helpers and connect leaf nodes to router
    PointToPointHelper pointToPointLeaf;
    pointToPointLeaf.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
    pointToPointLeaf.SetChannelAttribute("Delay", StringValue("1ms"));

    std::vector<NetDeviceContainer> leftToRouter;
    std::vector<NetDeviceContainer> routerToRight;
    for (uint32_t i = 0; i < N1; i++)
    {
        leftToRouter.push_back(pointToPointLeaf.Install(leftNodes.Get(i), routers.Get(0)));
    }
    for (uint32_t i = 0; i < N2; i++)
    {
        routerToRight.push_back(pointToPointLeaf.Install(routers.Get(1), rightNodes.Get(i)));
    }

    // Install internet stack on all the nodes
    InternetStackHelper internetStack;

    internetStack.Install(leftNodes);
    internetStack.Install(rightNodes);
    internetStack.Install(routers);

    // Assign IP addresses to all the network devices
    Ipv4AddressHelper ipAddresses("10.0.0.0", "255.255.255.0");

    Ipv4InterfaceContainer routersIpAddress = ipAddresses.Assign(routerToRouter);
    ipAddresses.NewNetwork();

    std::vector<Ipv4InterfaceContainer> leftToRouterIPAddress;
    for (uint32_t i = 0; i < N1; i++)
    {
        leftToRouterIPAddress.push_back(ipAddresses.Assign(leftToRouter[i]));
        ipAddresses.NewNetwork();
    }

    std::vector<Ipv4InterfaceContainer> routerToRightIPAddress;
    for (uint32_t i = 0; i < N2; i++)
    {
        routerToRightIPAddress.push_back(ipAddresses.Assign(routerToRight[i]));
        ipAddresses.NewNetwork();
    }

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // Set default sender and receiver buffer size as 1MB
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(1 << 20));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(1 << 20));

    // Set default initial congestion window as 10 segments
    Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue(10));

    // Set default delayed ack count to a specified value
    Config::SetDefault("ns3::TcpSocket::DelAckCount", UintegerValue(delAckCount));

    // Set default segment size of TCP packet to a specified value
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(segmentSize));

    // Enable/Disable SACK in TCP
    Config::SetDefault("ns3::TcpSocketBase::Sack", BooleanValue(isSack));

    // Set default parameters for queue discipline
    Config::SetDefault(qdiscTypeId + "::MaxSize", QueueSizeValue(QueueSize("100p")));

    // Install queue discipline on router
    TrafficControlHelper tch;
    tch.SetRootQueueDisc(qdiscTypeId);
    QueueDiscContainer qd;
    tch.Uninstall(routers.Get(0)->GetDevice(0));
    qd.Add(tch.Install(routers.Get(0)->GetDevice(0)).Get(0));

    // Enable BQL
    tch.SetQueueLimits("ns3::DynamicQueueLimits");

    // Install packet sink at receiver side
    for (uint32_t i = 0; i < N2; i++)
    {
        uint16_t port = 50000 + i;
        InstallPacketSink(rightNodes.Get(i), port, "ns3::TcpSocketFactory");
    }

    // Install BulkSend applications with flow sizes
    uint32_t flowSizes[] = {flowSize0, flowSize1};
    ApplicationContainer sourceApps[N1];
    for (uint32_t i = 0; i < N1; i++)
    {
        uint16_t port = 50000 + i;
        sourceApps[i] = InstallBulkSend(leftNodes.Get(i),
                                        routerToRightIPAddress[i].GetAddress(1),
                                        port,
                                        socketFactory,
                                        flowSizes[i]);
    }

    // Schedule cwnd tracing for the two flows and FCT tracing
    // Node IDs: routers (0,1), leftNodes (2,3), rightNodes (4,5)
    // Flow 0: node 2 -> n2.dat
    // Flow 1: node 3 -> n3.dat
    Simulator::Schedule(startTime + Seconds(0.001), &TraceCwnd, 2, dir + "cwnd/n2.dat");
    Simulator::Schedule(startTime + Seconds(0.001), &TraceCwnd, 3, dir + "cwnd/n3.dat");

    // Schedule FCT tracing for the two flows
    for (uint32_t i = 0; i < N1; i++)
    {
        Ptr<BulkSendApplication> bulkApp = sourceApps[i].Get(0)->GetObject<BulkSendApplication>();
        Simulator::Schedule(startTime + Seconds(0.001), &TraceFct, i, bulkApp);
    }

    // Enable PCAP capture on T0 router's three network devices in promiscuous mode
    // Device 0: connection to S0
    // Device 1: connection to S1
    // Device 2: connection to T1 (router-to-router link)
    for (uint32_t i = 0; i < 3; i++)
    {
        pointToPointLeaf.EnablePcap(dir + "pcap/lv1", routers.Get(0)->GetDevice(i), true);
    }

    // Set the stop time of the simulation
    Simulator::Stop(stopTime);

    // Start the simulation
    Simulator::Run();

    // Calculate and output FCT for both flows
    std::ofstream fctFile(dir + "fct/fct.dat");
    int64_t startTimeUs = startTime.GetMicroSeconds();

    for (uint32_t i = 0; i < N1; i++)
    {
        if (lastRxTime.find(i) != lastRxTime.end())
        {
            int64_t fct = lastRxTime[i] - startTimeUs;
            fctFile << fct << std::endl;
        }
    }
    fctFile.close();

    // Cleanup and close the simulation
    Simulator::Destroy();

    return 0;
}
