#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"

#include "ns3/tcp-imtcp-bg.h"
#include "ns3/tcp-imtcp-bg-improved.h"
#include "ns3/tcp-westwood-plus.h"

#include <fstream>
#include <vector>
#include <numeric>

using namespace ns3;

double Mean(const std::vector<double>& v)
{
    if (v.empty()) return 0.0;
    return std::accumulate(v.begin(), v.end(), 0.0) / v.size();
}

int main(int argc, char* argv[])
{
    uint32_t nNodes = 20;
    uint32_t nFlows = 10;
    uint32_t pps = 100;
    double stop = 20.0;

    std::string bgTcp = "newreno";

    CommandLine cmd;
    cmd.AddValue("nNodes", "Number of senders", nNodes);
    cmd.AddValue("nFlows", "Number of flows", nFlows);
    cmd.AddValue("pps", "Packets per second", pps);
    cmd.AddValue("stop", "Simulation time", stop);
    cmd.AddValue("bgTcp", "TCP variant", bgTcp);
    cmd.Parse(argc, argv);

    NodeContainer senders;
    senders.Create(nNodes);

    Ptr<Node> r0 = CreateObject<Node>();
    Ptr<Node> r1 = CreateObject<Node>();
    Ptr<Node> receiver = CreateObject<Node>();

    NodeContainer all;
    all.Add(senders);
    all.Add(r0);
    all.Add(r1);
    all.Add(receiver);

    InternetStackHelper stack;
    stack.Install(all);

    PointToPointHelper access;
    access.SetDeviceAttribute("DataRate", StringValue("100Mbps"));
    access.SetChannelAttribute("Delay", StringValue("1ms"));

    PointToPointHelper bottleneck;
    bottleneck.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
    bottleneck.SetChannelAttribute("Delay", StringValue("10ms"));

    Ipv4AddressHelper ip;

    // Senders → R0
    for (uint32_t i = 0; i < nNodes; i++)
    {
        NodeContainer pair(senders.Get(i), r0);
        NetDeviceContainer dev = access.Install(pair);

        std::ostringstream subnet;
        subnet << "10.0." << i + 1 << ".0";

        ip.SetBase(subnet.str().c_str(), "255.255.255.0");
        ip.Assign(dev);
    }
     
    // R0 → R1 (bottleneck)
    NodeContainer core(r0, r1);
    NetDeviceContainer devCore = bottleneck.Install(core);
    ip.SetBase("10.1.0.0", "255.255.255.0");
    ip.Assign(devCore);

    // R1 → receiver
    NodeContainer last(r1, receiver);
    NetDeviceContainer devLast = access.Install(last);
    ip.SetBase("10.2.0.0", "255.255.255.0");
    Ipv4InterfaceContainer ifRx = ip.Assign(devLast);

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    uint32_t nodeId = senders.Get(0)->GetId();

    if (bgTcp == "imtcpbg")
    {
        Config::Set("/NodeList/*/$ns3::TcpL4Protocol/SocketType",
                    TypeIdValue(TypeId::LookupByName("ns3::TcpImTcpBg")));
    }
    else if (bgTcp == "imtcpbgimproved")
    {
        Config::Set("/NodeList/*/$ns3::TcpL4Protocol/SocketType",
                    TypeIdValue(TypeId::LookupByName("ns3::TcpImTcpBgImproved")));
    }
    else if (bgTcp == "westwood")
    {
        Config::Set("/NodeList/*/$ns3::TcpL4Protocol/SocketType",
                    TypeIdValue(TypeId::LookupByName("ns3::TcpWestwoodPlus")));
    }
    else
    {
        Config::Set("/NodeList/*/$ns3::TcpL4Protocol/SocketType",
                    TypeIdValue(TypeId::LookupByName("ns3::TcpNewReno")));
    }

    uint16_t basePort = 9000;

    for (uint32_t i = 0; i < nFlows; i++)
    {
        uint16_t port = basePort + i;

        PacketSinkHelper sink("ns3::TcpSocketFactory",
                              InetSocketAddress(Ipv4Address::GetAny(), port));
        sink.Install(receiver).Start(Seconds(0));

        OnOffHelper app("ns3::TcpSocketFactory",
                        InetSocketAddress(ifRx.GetAddress(1), port));

        app.SetAttribute("DataRate", DataRateValue(DataRate(std::to_string(pps * 1000 * 8) + "bps")));
        app.SetAttribute("PacketSize", UintegerValue(1000));

        ApplicationContainer c = app.Install(senders.Get(i % nNodes));
        c.Start(Seconds(1.0));
        c.Stop(Seconds(stop));
    }

    
    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();

    Simulator::Stop(Seconds(stop));
    Simulator::Run();

    monitor->CheckForLostPackets();

    auto stats = monitor->GetFlowStats();
    auto classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());

    double totalThroughput = 0.0;
    std::vector<double> delays;

    uint64_t totalTx = 0;
    uint64_t totalRx = 0;
    uint64_t totalLost = 0;

    for (auto& kv : stats)
    {
        auto st = kv.second;

        totalTx += st.txPackets;
        totalRx += st.rxPackets;
        totalLost += st.lostPackets;

        double dur = (st.timeLastRxPacket - st.timeFirstTxPacket).GetSeconds();
        if (dur > 0)
        {
            totalThroughput += (st.rxBytes * 8.0 / dur) / 1e6;
        }

        if (st.rxPackets > 0)
        {
            delays.push_back(st.delaySum.GetSeconds() / st.rxPackets);
        }
    }

    double avgDelay = Mean(delays);

    double deliveryRatio = totalTx > 0 ? (double)totalRx / totalTx : 0;
    double dropRatio = totalTx > 0 ? (double)totalLost / totalTx : 0;


    std::string filename = "results/summary_wired.csv";


    std::ifstream infile(filename);
    bool writeHeader = false;

    if (!infile.good() || infile.peek() == std::ifstream::traits_type::eof())
    {
        writeHeader = true;
    }
    infile.close();

    std::ofstream out(filename, std::ios::app);

    if (writeHeader)
    {
        out << "bgTcp,nNodes,nFlows,pps,throughputMbps,avgDelay,deliveryRatio,dropRatio\n";
    }

    out << bgTcp << ","
        << nNodes << ","
        << nFlows << ","
        << pps << ","
        << totalThroughput << ","
        << avgDelay << ","
        << deliveryRatio << ","
        << dropRatio << "\n";

    out.close();

    Simulator::Destroy();
}