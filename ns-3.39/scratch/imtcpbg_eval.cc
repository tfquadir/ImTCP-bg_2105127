
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/queue.h"
#include "ns3/system-path.h"
#include "ns3/tcp-imtcp-bg.h"
#include "ns3/tcp-westwood-plus.h"

#include "ns3/tcp-imtcp-bg-improved.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <vector>

using namespace ns3;

bool EnableAdaptiveDelta = false;
double DeltaMin = 1.3;
double DeltaMax = 3.0;
double DeltaStepUp = 0.1;
double DeltaStepDown = 0.02;
uint32_t AdaptWindow = 16;


static void
QueuePoll(Ptr<Queue<Packet>> q,
          Ptr<OutputStreamWrapper> stream,
          Time interval,
          std::vector<uint32_t>* samples)
{
    uint32_t n = q->GetNPackets();
    samples->push_back(n);
    *stream->GetStream() << Simulator::Now().GetSeconds() << "\t" << n << "\n";
    Simulator::Schedule(interval, &QueuePoll, q, stream, interval, samples);
}

static double
Mean(const std::vector<double>& v)
{
    if (v.empty())
        return 0.0;
    return std::accumulate(v.begin(), v.end(), 0.0) / (double)v.size();
}

// compute percentile for floating-point values
static double
Pct(const std::vector<double>& v, double p)
{
    if (v.empty())
        return 0.0;
    std::vector<double> s = v;
    std::sort(s.begin(), s.end());
    size_t idx = (size_t)std::ceil(p * (s.size() - 1));
    return s[idx];
}

// compute percentile for unsigned 32-bit integers
static double
PctU32(const std::vector<uint32_t>& v, double p)
{
    if (v.empty())
        return 0.0;
    std::vector<uint32_t> s = v;
    std::sort(s.begin(), s.end());
    size_t idx = (size_t)std::ceil(p * (s.size() - 1));
    return (double)s[idx];
}

int
main(int argc, char* argv[])
{
    //  Args
    uint32_t nFg = 20;             // Foreground flows
    uint32_t fgBytes = 300000;     // 300KB per Foreground flow
    double stop = 30.0;            // runs for 30 seconds
    std::string bgTcp = "imtcpbg"; // imtcpbg as background default

    // ImTCP-bg parameters
    double gamma = 0.125;
    double delta = 1.2;
    uint32_t measureEveryRTT = 1; // 1..4
    uint32_t minCwndSegments = 8;
    bool EnableRttFallback = true;

    CommandLine cmd;
    cmd.AddValue("nFg", "Number of foreground short flows", nFg);
    cmd.AddValue("fgBytes", "Bytes per foreground flow", fgBytes);
    cmd.AddValue("stop", "Simulation stop time (s)", stop);
    cmd.AddValue("bgTcp", "Background TCP: imtcpbg|westwood|newreno", bgTcp);

    cmd.AddValue("gamma", "ImTCP-bg EWMA gain", gamma);
    cmd.AddValue("delta", "ImTCP-bg RTT inflation threshold", delta);
    cmd.AddValue("measureEveryRTT", "ImTCP-bg BW measurement period in RTTs (1..4)", measureEveryRTT);
    cmd.AddValue("minCwndSegments", "Skip BW measurement if cwnd < this (segments)", minCwndSegments);
    cmd.AddValue("EnableRttFallback", "Enable RTT inflation fallback", EnableRttFallback);

    cmd.AddValue("EnableAdaptiveDelta", "Enable adaptive delta", EnableAdaptiveDelta);
    cmd.AddValue("DeltaMin", "Minimum adaptive delta", DeltaMin);
    cmd.AddValue("DeltaMax", "Maximum adaptive delta", DeltaMax);
    cmd.AddValue("DeltaStepUp", "Adaptive delta increase step", DeltaStepUp);
    cmd.AddValue("DeltaStepDown", "Adaptive delta decrease step", DeltaStepDown);
    cmd.AddValue("AdaptWindow", "ACK observation window for delta adaptation", AdaptWindow);


    cmd.Parse(argc, argv);

    SystemPath::MakeDirectories("results");

    //  TCP defaults
    // Foreground default TCP = NewReno
    Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                       TypeIdValue(TypeId::LookupByName("ns3::TcpNewReno")));

    // ImTCP-bg tuning
    Config::SetDefault("ns3::TcpImTcpBg::Gamma", DoubleValue(gamma));
    Config::SetDefault("ns3::TcpImTcpBg::Delta", DoubleValue(delta));
    Config::SetDefault("ns3::TcpImTcpBg::MeasureEveryRTT", UintegerValue(measureEveryRTT));
    Config::SetDefault("ns3::TcpImTcpBg::MinCwndSegments", UintegerValue(minCwndSegments));
    Config::SetDefault("ns3::TcpImTcpBg::EnableRttFallback", BooleanValue(EnableRttFallback));

    //  Topology
    // Nodes: FG senders [0..nFg-1], BG sender, R0, R1, RX
    NodeContainer fgSenders;
    fgSenders.Create(nFg);

    Ptr<Node> bgSender = CreateObject<Node>();
    Ptr<Node> r0 = CreateObject<Node>();
    Ptr<Node> r1 = CreateObject<Node>();
    Ptr<Node> rx = CreateObject<Node>();

    NodeContainer all;
    for (uint32_t i = 0; i < nFg; i++)
    {
        all.Add(fgSenders.Get(i));
    }
    all.Add(bgSender);
    all.Add(r0);
    all.Add(r1);
    all.Add(rx);

    InternetStackHelper stack;
    stack.Install(all);

    PointToPointHelper access;
    access.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
    access.SetChannelAttribute("Delay", StringValue("1ms"));

    PointToPointHelper bottleneck;
    bottleneck.SetDeviceAttribute("DataRate", StringValue("50Mbps"));
    bottleneck.SetChannelAttribute("Delay", StringValue("10ms"));
    bottleneck.SetQueue("ns3::DropTailQueue", "MaxSize", QueueSizeValue(QueueSize("1000p"))); // max queue size 1000 packets

    Ipv4AddressHelper ip;

    // network configuration
    // IP addresses
    // FG -> R0 10.0.1.0/24 ... 10.0.(nFg).0/24
    // BG -> R0 10.1.0.0/24
    // R0 -> R1 10.2.0.0/24
    // R1 -> RX 10.3.0.0/24

    // FG -> R0 links (each its own subnet)
    for (uint32_t i = 0; i < nFg; i++)
    {
        NodeContainer pair(fgSenders.Get(i), r0);
        NetDeviceContainer dev = access.Install(pair);

        std::ostringstream base;
        base << "10.0." << (i + 1) << ".0";
        ip.SetBase(base.str().c_str(), "255.255.255.0");
        ip.Assign(dev);
    }

    // BG -> R0
    NodeContainer bgPair(bgSender, r0);
    NetDeviceContainer devBg = access.Install(bgPair);
    ip.SetBase("10.1.0.0", "255.255.255.0");
    ip.Assign(devBg);

    // R0 -> R1 bottleneck
    NodeContainer core(r0, r1);
    NetDeviceContainer devCore = bottleneck.Install(core);
    ip.SetBase("10.2.0.0", "255.255.255.0");
    ip.Assign(devCore);

    // R1 -> RX
    NodeContainer r1rx(r1, rx);
    NetDeviceContainer devRx = access.Install(r1rx);
    ip.SetBase("10.3.0.0", "255.255.255.0");
    Ipv4InterfaceContainer ifRx = ip.Assign(devRx);

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    //  Choose BG TCP (per-node override)
    uint32_t bgNodeId = bgSender->GetId();

    if (bgTcp == "imtcpbg")
    {
        Config::Set("/NodeList/" + std::to_string(bgNodeId) + "/$ns3::TcpL4Protocol/SocketType",
                    TypeIdValue(TypeId::LookupByName("ns3::TcpImTcpBg")));
    }
    else if (bgTcp == "imtcpbgimproved")
    {
    Config::Set("/NodeList/" + std::to_string(bgNodeId) + "/$ns3::TcpL4Protocol/SocketType",
                TypeIdValue(TypeId::LookupByName("ns3::TcpImTcpBgImproved")));
    }
    else if (bgTcp == "westwood")
    {
        Config::Set("/NodeList/" + std::to_string(bgNodeId) + "/$ns3::TcpL4Protocol/SocketType",
                    TypeIdValue(TypeId::LookupByName("ns3::TcpWestwoodPlus")));
    }
    else if (bgTcp == "newreno")
    {
        Config::Set("/NodeList/" + std::to_string(bgNodeId) + "/$ns3::TcpL4Protocol/SocketType",
                    TypeIdValue(TypeId::LookupByName("ns3::TcpNewReno")));
    }
    else 
    {
        Config::Set("/NodeList/" + std::to_string(bgNodeId) + "/$ns3::TcpL4Protocol/SocketType",
                    TypeIdValue(TypeId::LookupByName("ns3::TcpNewReno")));
    } // newreno

    //  Applications
    uint16_t bgPort = 5000;

    // BG sink
    PacketSinkHelper bgSink("ns3::TcpSocketFactory",
                            InetSocketAddress(Ipv4Address::GetAny(), bgPort));
    auto bgSinkApp = bgSink.Install(rx);
    bgSinkApp.Start(Seconds(0.0));
    bgSinkApp.Stop(Seconds(stop));

    // BG sender (long-lived)
    BulkSendHelper bgBulk("ns3::TcpSocketFactory", InetSocketAddress(ifRx.GetAddress(1), bgPort));
    bgBulk.SetAttribute("MaxBytes", UintegerValue(0)); // unlimited
    auto bgApp = bgBulk.Install(bgSender);
    bgApp.Start(Seconds(0.2));
    bgApp.Stop(Seconds(stop));

    // Foreground: many short flows, staggered starts
    Ptr<UniformRandomVariable> uv = CreateObject<UniformRandomVariable>();
    for (uint32_t i = 0; i < nFg; i++)
    {
        uint16_t port = 6000 + i;

        PacketSinkHelper fgSink("ns3::TcpSocketFactory",
                                InetSocketAddress(Ipv4Address::GetAny(), port));
        auto s = fgSink.Install(rx);
        s.Start(Seconds(0.0));
        s.Stop(Seconds(stop));

        BulkSendHelper fgBulk("ns3::TcpSocketFactory", InetSocketAddress(ifRx.GetAddress(1), port));
        fgBulk.SetAttribute("MaxBytes", UintegerValue(fgBytes));

        auto a = fgBulk.Install(fgSenders.Get(i));

        // Web-ish arrivals: start between 1s and (stop-5)s
        double st = uv->GetValue(1.0, std::max(2.0, stop - 5.0));
        a.Start(Seconds(st));
        a.Stop(Seconds(stop));
    }

    //  Queue sampling (bottleneck queue at R0 side)
    Ptr<PointToPointNetDevice> nd0 = DynamicCast<PointToPointNetDevice>(devCore.Get(0));
    Ptr<Queue<Packet>> q = nd0->GetQueue();

    AsciiTraceHelper ascii;
    auto qStream = ascii.CreateFileStream("results/queue.txt");
    std::vector<uint32_t> qSamples;
    Simulator::Schedule(Seconds(0.0), &QueuePoll, q, qStream, MilliSeconds(1), &qSamples);

    //  FlowMonitor
    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();

    Simulator::Stop(Seconds(stop));
    Simulator::Run();

    monitor->CheckForLostPackets();
    auto classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
    auto stats = monitor->GetFlowStats();

    // !
    //  Metrics
    std::vector<double> fgFct;
    std::vector<double> allThroughputs;
    std::vector<double> flowAvgDelays;
    std::vector<double> flowAvgJitters;

    double bgMbps = 0.0;

    uint64_t totalTxPackets = 0;
    uint64_t totalRxPackets = 0;
    uint64_t totalLostPackets = 0;


    for (const auto& kv : stats)
    {
        auto t = classifier->FindFlow(kv.first);
        const auto& st = kv.second;

        totalTxPackets += st.txPackets;
        totalRxPackets += st.rxPackets;
        totalLostPackets += st.lostPackets;

        double dur = (st.timeLastRxPacket - st.timeFirstTxPacket).GetSeconds();
        if (dur <= 0)
        {
            continue;
        }

        double thrMbps = (st.rxBytes * 8.0 / dur) / 1e6;
        allThroughputs.push_back(thrMbps);

        if (st.rxPackets > 0)
        {
            double avgFlowDelay = st.delaySum.GetSeconds() / st.rxPackets;
            flowAvgDelays.push_back(avgFlowDelay);
        }

        if (st.rxPackets > 1)
        {
            double avgFlowJitter = st.jitterSum.GetSeconds() / (st.rxPackets - 1);
            flowAvgJitters.push_back(avgFlowJitter);
        }
        else if (st.rxPackets == 1)
        {
            flowAvgJitters.push_back(0.0);
        }

        if (t.destinationPort == bgPort)
        {
            bgMbps = thrMbps;
        }
        else if (t.destinationPort >= 6000 && t.destinationPort < 6000 + nFg)
        {
            double fct = (st.timeLastRxPacket - st.timeFirstTxPacket).GetSeconds();
            fgFct.push_back(fct);
        }
    }

    double fgMean = Mean(fgFct);
    double fgP95 = Pct(fgFct, 0.95);

    double qAvg =
        qSamples.empty()
            ? 0.0
            : (std::accumulate(qSamples.begin(), qSamples.end(), 0.0) / (double)qSamples.size());
    double qP95 = PctU32(qSamples, 0.95);

    // !
    double avgDelay = Mean(flowAvgDelays);
    double avgJitter = Mean(flowAvgJitters);

    double deliveryRatio = 0.0;
    double dropRatio = 0.0;

    if (totalTxPackets > 0)
    {
        deliveryRatio = static_cast<double>(totalRxPackets) / static_cast<double>(totalTxPackets);
        dropRatio = static_cast<double>(totalLostPackets) / static_cast<double>(totalTxPackets);
    }

    double fairness = 0.0;
    if (!allThroughputs.empty())
    {
        double sum = 0.0;
        double sumSq = 0.0;

        for (double x : allThroughputs)
        {
            sum += x;
            sumSq += x * x;
        }

        if (sumSq > 0.0)
        {
            fairness = (sum * sum) / (allThroughputs.size() * sumSq);
        }
    }

    // CSV output
    bool fileExists = std::ifstream("results/summary.csv").good();

    std::ofstream out("results/summary.csv", std::ios::app);

    if (!fileExists)
    {
        out << "bgTcp,gamma,delta,minCwndSegments,EnableRttFallback,"
       "DeltaMin,DeltaMax,DeltaStepUp,DeltaStepDown,AdaptWindow,"
       "nFg,fgBytes,fgMeanFct,fgP95Fct,bgMbps,qAvgPkts,qP95Pkts,"
       "avgDelay,avgJitter,deliveryRatio,dropRatio,fairness\n";
    }

    out << bgTcp << ","
        << gamma << ","
        << delta << ","
        // << measureEveryRTT << ","
        << minCwndSegments << ","
        << (EnableRttFallback ? 1 : 0) << ","
        // << (EnableAdaptiveDelta ? 1 : 0) << ","
        << DeltaMin << ","
        << DeltaMax << ","
        << DeltaStepUp << ","
        << DeltaStepDown << ","
        << AdaptWindow << ","
        << nFg << ","
        << fgBytes << ","
        << std::fixed << std::setprecision(6)
        << fgMean << ","
        << fgP95 << ","
        << std::setprecision(3)
        << bgMbps << ","
        << qAvg << ","
        << qP95 << ","
        << std::setprecision(6)
        << avgDelay << ","
        << avgJitter << ","
        << deliveryRatio << ","
        << dropRatio << ","
        << fairness << "\n";

    out.close();

    std::cout << "\n DONE \n";
    std::cout << "Wrote: results/queue.txt and results/summary.csv\n";
    std::cout << "FG flows counted: " << fgFct.size() << " / " << nFg << "\n";
    std::cout << "FG mean FCT: " << fgMean << " s, FG p95 FCT: " << fgP95 << " s\n";
    std::cout << "BG throughput: " << bgMbps << " Mbps\n";
    std::cout << "Queue avg: " << qAvg << " pkts, Queue p95: " << qP95 << " pkts\n";

    std::cout << "Average delay: " << avgDelay << " s\n";
    std::cout << "Average jitter: " << avgJitter << " s\n";
    std::cout << "Delivery ratio: " << deliveryRatio << "\n";
    std::cout << "Drop ratio: " << dropRatio << "\n";
    std::cout << "Fairness: " << fairness << "\n";

    Simulator::Destroy();
    return 0;
}