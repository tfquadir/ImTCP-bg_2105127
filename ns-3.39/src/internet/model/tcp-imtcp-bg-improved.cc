#include "tcp-imtcp-bg-improved.h"

#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/double.h"
#include "ns3/enum.h"

#include <algorithm>

NS_LOG_COMPONENT_DEFINE("TcpImTcpBgImproved");

namespace ns3
{

NS_OBJECT_ENSURE_REGISTERED(TcpImTcpBgImproved);

TypeId
TcpImTcpBgImproved::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::TcpImTcpBgImproved") 
            .SetParent<TcpWestwoodPlus>()
            .SetGroupName("Internet")
            .AddConstructor<TcpImTcpBgImproved>()
            .AddAttribute(
                "ImFilterType",
                "ImTCP-bg-improved filter choice.",
                EnumValue(TcpImTcpBgImproved::TUSTIN),
                MakeEnumAccessor(&TcpImTcpBgImproved::m_fType),
                MakeEnumChecker(TcpImTcpBgImproved::NONE,
                                "None",
                                TcpImTcpBgImproved::TUSTIN,
                                "Tustin"))
            .AddAttribute("Gamma",
                            "EWMA(Exponentially Weighted Moving Average) gain for smoothing the bandwidth estimation",
                          DoubleValue(0.125),
                          MakeDoubleAccessor(&TcpImTcpBgImproved::m_gamma),
                          MakeDoubleChecker<double>(0.0, 1.0))
            .AddAttribute("Delta",
                            "RTT inflation threshold for detecting congestion",
                          DoubleValue(1.2),
                          MakeDoubleAccessor(&TcpImTcpBgImproved::m_delta),
                          MakeDoubleChecker<double>(1.0))
            .AddTraceSource("ImEstimatedBW",
                            "ImTCP-bg-improved estimated bandwidth.",
                            MakeTraceSourceAccessor(&TcpImTcpBgImproved::m_currentBW),
                            "ns3::TracedValueCallback::DataRate")
            // ! new addition
            .AddAttribute("MeasureEveryRTT",
                            "Run EstimateBW every K RTTs (between 1 to 4)",
                          UintegerValue(1),
                          MakeUintegerAccessor(&TcpImTcpBgImproved::m_measureEveryRTT),
                          MakeUintegerChecker<uint32_t>(1, 4))
            .AddAttribute("MinCwndSegments",
                          "Skip BW measurement if cwnd (in segments) is below this threshold.",
                          UintegerValue(8),
                          MakeUintegerAccessor(&TcpImTcpBgImproved::m_minCwndSegments),
                          MakeUintegerChecker<uint32_t>(1))
            .AddAttribute("EnableRttFallback",
                            "Enable RTT inflation fallback (delta threshold).",
                          BooleanValue(true),
                          MakeBooleanAccessor(&TcpImTcpBgImproved::m_enableRTTFallback),
                          MakeBooleanChecker())
            .AddTraceSource("AbarBps",
                            "Smoothed bandwidth estimate Abar in bps.",
                            MakeTraceSourceAccessor(&TcpImTcpBgImproved::m_abarBpsTrace),
                            "ns3::TracedValueCallback::Double")
            // ! improved / adaptive delta attributes 
            .AddAttribute("EnableAdaptiveDelta",
                          "Enable adaptive RTT inflation threshold.",
                          BooleanValue(false),
                          MakeBooleanAccessor(&TcpImTcpBgImproved::m_enableAdaptiveDelta),
                          MakeBooleanChecker())
            .AddAttribute("DeltaMin",
                          "Minimum adaptive delta.",
                          DoubleValue(1.15),
                          MakeDoubleAccessor(&TcpImTcpBgImproved::m_deltaMin),
                          MakeDoubleChecker<double>(1.0))
            .AddAttribute("DeltaMax",
                          "Maximum adaptive delta.",
                          DoubleValue(2.0),
                          MakeDoubleAccessor(&TcpImTcpBgImproved::m_deltaMax),
                          MakeDoubleChecker<double>(1.0))
            .AddAttribute("DeltaStepUp",
                          "Adaptive delta increase step when path is stable.",
                          DoubleValue(0.01),
                          MakeDoubleAccessor(&TcpImTcpBgImproved::m_deltaStepUp),
                          MakeDoubleChecker<double>(0.0))
            .AddAttribute("DeltaStepDown",
                          "Adaptive delta decrease step when congestion is observed.",
                          DoubleValue(0.02),
                          MakeDoubleAccessor(&TcpImTcpBgImproved::m_deltaStepDown),
                          MakeDoubleChecker<double>(0.0))
            .AddAttribute("AdaptWindow",
                          "Number of ACK observations before adjusting delta.",
                          UintegerValue(8),
                          MakeUintegerAccessor(&TcpImTcpBgImproved::m_adaptWindow),
                          MakeUintegerChecker<uint32_t>(1))
            .AddTraceSource("AdaptiveDelta",
                            "Runtime adaptive delta used for RTT fallback.",
                            MakeTraceSourceAccessor(&TcpImTcpBgImproved::m_deltaTrace),
                            "ns3::TracedValueCallback::Double");

    return tid;
}

TcpImTcpBgImproved::TcpImTcpBgImproved()
    : TcpWestwoodPlus(),
      m_currentBW(0),
      m_lastSampleBW(0),
      m_lastBW(0),
      m_fType(TUSTIN),
      m_ackedSegments(0),
      m_IsCount(false),
      m_lastAck(0),
      m_gamma(0.125),
      m_delta(1.2),
      m_abarBps(0.0),
      m_measureEveryRTT(1),
      m_minCwndSegments(8),
      m_enableRTTFallback(true),
      m_abarBpsTrace(0.0),
      m_enableAdaptiveDelta(false),
      m_deltaCurrent(1.2),
      m_deltaMin(1.15),
      m_deltaMax(2.0),
      m_deltaStepUp(0.01),
      m_deltaStepDown(0.02),
      m_stableAckCount(0),
      m_congestedAckCount(0),
      m_adaptWindow(8),
      m_deltaTrace(1.2)
{
    NS_LOG_FUNCTION(this);
}

TcpImTcpBgImproved::TcpImTcpBgImproved(const TcpImTcpBgImproved& sock)
    : TcpWestwoodPlus(sock),
      m_currentBW(sock.m_currentBW),
      m_lastSampleBW(sock.m_lastSampleBW),
      m_lastBW(sock.m_lastBW),
      m_fType(sock.m_fType),
      m_ackedSegments(sock.m_ackedSegments),
      m_IsCount(sock.m_IsCount),
      m_bwEstimateEvent(sock.m_bwEstimateEvent),
      m_lastAck(sock.m_lastAck),
      m_gamma(sock.m_gamma),
      m_delta(sock.m_delta),
      m_abarBps(sock.m_abarBps),
      m_measureEveryRTT(sock.m_measureEveryRTT),
      m_minCwndSegments(sock.m_minCwndSegments),
      m_enableRTTFallback(sock.m_enableRTTFallback),
      m_abarBpsTrace(sock.m_abarBpsTrace),
      m_enableAdaptiveDelta(sock.m_enableAdaptiveDelta),
      m_deltaCurrent(sock.m_deltaCurrent),
      m_deltaMin(sock.m_deltaMin),
      m_deltaMax(sock.m_deltaMax),
      m_deltaStepUp(sock.m_deltaStepUp),
      m_deltaStepDown(sock.m_deltaStepDown),
      m_stableAckCount(sock.m_stableAckCount),
      m_congestedAckCount(sock.m_congestedAckCount),
      m_adaptWindow(sock.m_adaptWindow),
      m_deltaTrace(sock.m_deltaTrace)
{
    NS_LOG_FUNCTION(this);
    NS_LOG_LOGIC("Invoked the copy constructor");
}

TcpImTcpBgImproved::~TcpImTcpBgImproved()
{
}

void
TcpImTcpBgImproved::UpdateAckedSegments(int acked)
{
    if (acked > 0)
    {
        m_ackedSegments += static_cast<uint32_t>(acked);
    }
}


void
TcpImTcpBgImproved::UpdateAdaptiveDelta(double ratio)
{
    if (!m_enableAdaptiveDelta)
    {
        m_deltaCurrent = m_delta;
        m_deltaTrace = m_deltaCurrent;
        return;
    }

    // If fallback/congestion is happening repeatedly, make delta larger to ease changes
    if (ratio > m_deltaCurrent)
    {
        m_congestedAckCount++;
        m_stableAckCount = 0;
    }
    // If path is very stable, slowly tighten delta again.
    else if (ratio < 1.05)
    {
        m_stableAckCount++;
        m_congestedAckCount = 0;
    }
    else
    {
        m_stableAckCount = 0;
        m_congestedAckCount = 0;
    }

    // Congestion seen repeatedly -> relax threshold
    if (m_congestedAckCount >= m_adaptWindow)
    {
        m_deltaCurrent = std::min(m_deltaMax, m_deltaCurrent + m_deltaStepUp);
        m_congestedAckCount = 0;
    }
    // Stable path for a while -> slowly make threshold stricter
    else if (m_stableAckCount >= m_adaptWindow)
    {
        m_deltaCurrent = std::max(m_deltaMin, m_deltaCurrent - m_deltaStepDown);
        m_stableAckCount = 0;
    }

    m_deltaTrace = m_deltaCurrent;
}



void
TcpImTcpBgImproved::PktsAcked(Ptr<TcpSocketState> tcb, uint32_t packetsAcked, const Time& rtt)
{
    NS_LOG_FUNCTION(this << tcb << packetsAcked << rtt);

    if(rtt.IsZero()){
         NS_LOG_WARN("RTT measured is zero!");
        return;
    }


    uint32_t cwndSegments = tcb->m_cWnd.Get() / tcb->m_segmentSize;

    if (!tcb->m_minRtt.IsZero() && m_enableRTTFallback && cwndSegments >= m_minCwndSegments)
    {
        double ratio = rtt.GetSeconds() / tcb->m_minRtt.GetSeconds();

        UpdateAdaptiveDelta(ratio);

        double activeDelta = m_enableAdaptiveDelta ? m_deltaCurrent : m_delta;

        if (ratio > activeDelta)
        {
            double scale = tcb->m_minRtt.GetSeconds() / rtt.GetSeconds();
            scale = std::max(0.75, scale);   

            uint32_t minCwnd = 4 * tcb->m_segmentSize;
            uint32_t newCwnd = static_cast<uint32_t>(tcb->m_cWnd.Get() * scale);

            if (newCwnd < minCwnd)
            {
                newCwnd = minCwnd;
            }

            tcb->m_cWnd = newCwnd;
        }
    }


    else
    {
        m_deltaCurrent = m_delta;
        m_deltaTrace = m_deltaCurrent;
    }


    if (cwndSegments < m_minCwndSegments)
    {
        m_ackedSegments = 0;
        m_IsCount = false;
        return;
    }

    UpdateAckedSegments(static_cast<int>(packetsAcked));

    if (!(rtt.IsZero() || m_IsCount))
    {
        m_IsCount = true;
        m_bwEstimateEvent.Cancel();

        Time interval = Seconds(rtt.GetSeconds() * m_measureEveryRTT);
        m_bwEstimateEvent =
            Simulator::Schedule(interval, &TcpImTcpBgImproved::EstimateBW, this, interval, tcb);
    }
}

void
TcpImTcpBgImproved::IncreaseWindow(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked)
{
    TcpWestwoodPlus::IncreaseWindow(tcb, segmentsAcked);

    if (tcb->m_minRtt.IsZero() || m_abarBps <= 0.0)
    {
        return;
    }

    double maxCwndD = 1.25 * (m_abarBps * tcb->m_minRtt.GetSeconds()) / 8.0;
    uint32_t maxCwnd = static_cast<uint32_t>(maxCwndD);

    uint32_t minCwnd = 2 * tcb->m_segmentSize;
    if (maxCwnd < minCwnd)
    {
        maxCwnd = minCwnd;
    }

    if (tcb->m_cWnd.Get() > maxCwnd)
    {
        tcb->m_cWnd = maxCwnd;
    }
}

void
TcpImTcpBgImproved::EstimateBW(const Time& rtt, Ptr<TcpSocketState> tcb)
{
    NS_LOG_FUNCTION(this);

    NS_ASSERT(!rtt.IsZero());

    m_currentBW = DataRate(m_ackedSegments * tcb->m_segmentSize * 8.0 / rtt.GetSeconds());
    double sampleBps = static_cast<double>(m_currentBW.Get().GetBitRate());

    if (m_abarBps <= 0.0)
    {
        m_abarBps = sampleBps;
    }
    else
    {
        m_abarBps = (1.0 - m_gamma) * m_abarBps + m_gamma * sampleBps;
    }

    m_abarBpsTrace = m_abarBps;

    m_IsCount = false;
    m_ackedSegments = 0;

    NS_LOG_LOGIC("Estimated BW before filtering: " << m_currentBW);

    constexpr double ALPHA = 0.9;

    if (m_fType == TcpImTcpBgImproved::TUSTIN)
    {
        DataRate sampleBwe = m_currentBW;
        m_currentBW =
            (m_lastBW * ALPHA) + (((sampleBwe + m_lastSampleBW) * 0.5) * (1.0 - ALPHA));
        m_lastSampleBW = sampleBwe;
        m_lastBW = m_currentBW;
    }

    NS_LOG_LOGIC("Estimated BW after filtering: " << m_currentBW);
}

uint32_t
TcpImTcpBgImproved::GetSsThresh(Ptr<const TcpSocketState> tcb,
                                uint32_t bytesInFlight [[maybe_unused]])
{
    uint32_t ssThresh = static_cast<uint32_t>((m_currentBW * tcb->m_minRtt) / 8.0);

    NS_LOG_LOGIC("CurrentBW: " << m_currentBW << " minRtt: " << tcb->m_minRtt
                               << " ssThresh: " << ssThresh);

    return std::max(2 * tcb->m_segmentSize, ssThresh);
}

Ptr<TcpCongestionOps>
TcpImTcpBgImproved::Fork()
{
    return CreateObject<TcpImTcpBgImproved>(*this);
}

} // namespace ns3