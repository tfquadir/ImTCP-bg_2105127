#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/lr-wpan-module.h"
#include "ns3/sixlowpan-module.h"
#include "ns3/lr-wpan-net-device.h"
#include "ns3/lr-wpan-phy.h"

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

struct RadioEnergyState
{
    double txTime = 0.0;
    double rxTime = 0.0;
    double idleTime = 0.0; 
    double offTime = 0.0;

    Time lastUpdate = Seconds(0);
    LrWpanPhyEnumeration lastState = IEEE_802_15_4_PHY_TRX_OFF;
};

static std::vector<RadioEnergyState> g_radioStates;


static const double RADIO_VOLTAGE   = 3.0;       // volts
static const double TX_CURRENT_A    = 0.0174;    // amperes
static const double RX_CURRENT_A    = 0.0197;    // amperes
static const double IDLE_CURRENT_A  = 0.000426;  // amperes
static const double OFF_CURRENT_A   = 0.000020;  // amperes

static void
AccumulateStateTime(RadioEnergyState& st, Time now)
{
    double dt = (now - st.lastUpdate).GetSeconds();
    if (dt < 0)
    {
        return;
    }

    switch (st.lastState)
    {
    case IEEE_802_15_4_PHY_BUSY_TX:
        st.txTime += dt;
        break;

    case IEEE_802_15_4_PHY_BUSY_RX:
        st.rxTime += dt;
        break;

    case IEEE_802_15_4_PHY_IDLE:
    case IEEE_802_15_4_PHY_RX_ON:
    case IEEE_802_15_4_PHY_TX_ON:
        st.idleTime += dt;
        break;

    case IEEE_802_15_4_PHY_TRX_OFF:
    case IEEE_802_15_4_PHY_FORCE_TRX_OFF:
        st.offTime += dt;
        break;

    default:
        st.idleTime += dt;
        break;
    }
}

static void
PhyStateTrace(uint32_t index,
              Time now,
              LrWpanPhyEnumeration oldState,
              LrWpanPhyEnumeration newState)
{
    (void) oldState;

    RadioEnergyState& st = g_radioStates.at(index);
    AccumulateStateTime(st, now);
    st.lastUpdate = now;
    st.lastState = newState;
}

int main(int argc, char* argv[])
{
    uint32_t nNodes = 20;
    uint32_t nFlows = 10;
    uint32_t pps = 100;
    double stop = 20.0;
    double txRange = 10.0;
    uint32_t areaScale = 1;
    std::string bgTcp = "newreno";

    CommandLine cmd;
    cmd.AddValue("nNodes", "Total number of nodes", nNodes);
    cmd.AddValue("nFlows", "Number of flows", nFlows);
    cmd.AddValue("pps", "Packets per second", pps);
    cmd.AddValue("stop", "Simulation time", stop);
    cmd.AddValue("txRange", "Reference transmission range", txRange);
    cmd.AddValue("areaScale", "Coverage side multiplier (1..5)", areaScale);
    cmd.AddValue("bgTcp", "TCP variant", bgTcp);
    cmd.Parse(argc, argv);

    double areaSide = areaScale * txRange;

    if (nNodes < 2)
    {
        std::cerr << "nNodes must be at least 2\n";
        return 1;
    }

    NodeContainer nodes;
    nodes.Create(nNodes);

    Ptr<Node> sinkNode = nodes.Get(0);

    
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

    
    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();

    positionAlloc->Add(Vector(areaSide / 2.0, areaSide / 2.0, 0.0));

    Ptr<UniformRandomVariable> uv = CreateObject<UniformRandomVariable>();
    for (uint32_t i = 1; i < nNodes; ++i)
    {
        double x = uv->GetValue(0.0, areaSide);
        double y = uv->GetValue(0.0, areaSide);
        positionAlloc->Add(Vector(x, y, 0.0));
    }

    mobility.SetPositionAllocator(positionAlloc);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);

    
    LrWpanHelper lrWpanHelper;
    NetDeviceContainer lrwpanDevices = lrWpanHelper.Install(nodes);
    lrWpanHelper.CreateAssociatedPan(lrwpanDevices, 0);

    
    g_radioStates.clear();
    g_radioStates.resize(nNodes);

    for (uint32_t i = 0; i < nNodes; ++i)
    {
        g_radioStates[i].lastUpdate = Seconds(0);
        g_radioStates[i].lastState = IEEE_802_15_4_PHY_TRX_OFF;

        Ptr<LrWpanNetDevice> dev = DynamicCast<LrWpanNetDevice>(lrwpanDevices.Get(i));
        if (dev)
        {
            dev->GetPhy()->TraceConnectWithoutContext(
                "TrxState",
                MakeBoundCallback(&PhyStateTrace, i));
        }
    }

    
    SixLowPanHelper sixlowpan;
    NetDeviceContainer sixlowpanDevices = sixlowpan.Install(lrwpanDevices);

    
    InternetStackHelper internet;
    internet.Install(nodes);

    Ipv6AddressHelper ipv6;
    ipv6.SetBase(Ipv6Address("2001:1::"), Ipv6Prefix(64));
    Ipv6InterfaceContainer interfaces = ipv6.Assign(sixlowpanDevices);

    
    uint16_t basePort = 9000;

    for (uint32_t i = 0; i < nFlows; ++i)
    {
        uint16_t port = basePort + i;

        PacketSinkHelper sinkHelper("ns3::TcpSocketFactory",
                                    Inet6SocketAddress(Ipv6Address::GetAny(), port));
        ApplicationContainer sinkApp = sinkHelper.Install(sinkNode);
        sinkApp.Start(Seconds(0.0));
        sinkApp.Stop(Seconds(stop));

        uint32_t srcIndex = 1 + (i % (nNodes - 1));

        OnOffHelper app("ns3::TcpSocketFactory",
                        Inet6SocketAddress(interfaces.GetAddress(0, 1), port));

        app.SetAttribute("DataRate",
                         DataRateValue(DataRate(std::to_string(pps * 1000 * 8) + "bps")));
        app.SetAttribute("PacketSize", UintegerValue(1000));

        ApplicationContainer c = app.Install(nodes.Get(srcIndex));
        c.Start(Seconds(1.0));
        c.Stop(Seconds(stop));
    }

    
    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();

    Simulator::Stop(Seconds(stop));
    Simulator::Run();

    monitor->CheckForLostPackets();
    auto stats = monitor->GetFlowStats();

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
    double deliveryRatio = totalTx > 0 ? (double) totalRx / totalTx : 0.0;
    double dropRatio = totalTx > 0 ? (double) totalLost / totalTx : 0.0;

    
    for (uint32_t i = 0; i < nNodes; ++i)
    {
        AccumulateStateTime(g_radioStates[i], Seconds(stop));
        g_radioStates[i].lastUpdate = Seconds(stop);
    }

    double energyConsumed = 0.0;

    for (uint32_t i = 0; i < nNodes; ++i)
    {
        const auto& st = g_radioStates[i];

        double nodeEnergy =
            RADIO_VOLTAGE *
            (TX_CURRENT_A * st.txTime +
             RX_CURRENT_A * st.rxTime +
             IDLE_CURRENT_A * st.idleTime +
             OFF_CURRENT_A * st.offTime);

        energyConsumed += nodeEnergy;
    }

    
    std::string filename = "results/summary_wireless.csv";

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
        out << "bgTcp,nNodes,nFlows,pps,txRange,areaScale,areaSide,throughputMbps,avgDelay,deliveryRatio,dropRatio,energyConsumedJ\n";
    }

    out << bgTcp << ","
        << nNodes << ","
        << nFlows << ","
        << pps << ","
        << txRange << ","
        << areaScale << ","
        << areaSide << ","
        << totalThroughput << ","
        << avgDelay << ","
        << deliveryRatio << ","
        << dropRatio << ","
        << energyConsumed << "\n";

    out.close();

    Simulator::Destroy();
    return 0;
}