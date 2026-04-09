#include "tcp-imtcp-bg.h"

#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/double.h"

NS_LOG_COMPONENT_DEFINE("TcpImTcpBg");

namespace ns3
{

NS_OBJECT_ENSURE_REGISTERED(TcpImTcpBg);

TypeId
TcpImTcpBg::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::TcpImTcpBg")
            .SetParent<TcpWestwoodPlus>()
            .SetGroupName("Internet")
            .AddConstructor<TcpImTcpBg>()
            .AddAttribute(
                "ImFilterType",
                "ImTCP-bg filter choice (avoid name clash with TcpWestwoodPlus::FilterType).",
                EnumValue(TcpImTcpBg::TUSTIN),
                MakeEnumAccessor(&TcpImTcpBg::m_fType),
                MakeEnumChecker(TcpImTcpBg::NONE, "None", TcpImTcpBg::TUSTIN, "Tustin"))
            .AddAttribute("Gamma",
                            "EWMA(Exponentially Weighted Moving Average) gain for smoothing the bandwidth estimation",
                            DoubleValue(0.125),
                            MakeDoubleAccessor(&TcpImTcpBg::m_gamma),
                            MakeDoubleChecker<double>(0.0, 1.0))
            .AddAttribute("Delta",
                            "RTT inflation threshold for detecting congestion",
                            DoubleValue(1.2),
                            MakeDoubleAccessor(&TcpImTcpBg::m_delta),
                            MakeDoubleChecker<double>(1.0))        
            .AddTraceSource("ImEstimatedBW",
                            "ImTCP-bg estimated bandwidth (avoid name clash with TcpWestwoodPlus::EstimatedBW).",
                            MakeTraceSourceAccessor(&TcpImTcpBg::m_currentBW),
                            "ns3::TracedValueCallback::DataRate")
            // ! new addition
            .AddAttribute("MeasureEveryRTT",
                            "Run EstimateBW every K RTTs (between 1 to 4)",
                            UintegerValue(1),
                            MakeUintegerAccessor(&TcpImTcpBg::m_measureEveryRTT),
                            MakeUintegerChecker<uint32_t>(1, 4))
            .AddAttribute("MinCwndSegments",
                            "Skip BW measurement if cwnd (in segments) is below this threshold.",
                            UintegerValue(8),
                            MakeUintegerAccessor(&TcpImTcpBg::m_minCwndSegments),
                            MakeUintegerChecker<uint32_t>(1))
            .AddAttribute("EnableRttFallback",
                            "Enable RTT inflation fallback (delta threshold).",
                            BooleanValue(true),
                            MakeBooleanAccessor(&TcpImTcpBg::m_enableRTTFallback),
                            MakeBooleanChecker())
            .AddTraceSource("AbarBps",
                            "Smoothed bandwidth estimate Abar in bps (EWMA).",
                            MakeTraceSourceAccessor(&TcpImTcpBg::m_abarBpsTrace),
                            "ns3::TracedValueCallback::Double");             
            
    return tid;
}

TcpImTcpBg::TcpImTcpBg()
    : TcpWestwoodPlus(),
      m_currentBW(0),
      m_lastSampleBW(0),
      m_lastBW(0),
      m_ackedSegments(0),
      m_IsCount(false),
      m_lastAck(0),
      m_gamma (0.125),
      m_delta (1.2),
      m_abarBps (0.0),
      m_measureEveryRTT(1),
      m_minCwndSegments(8),
      m_enableRTTFallback(true),
      m_abarBpsTrace(0.0)
{
    NS_LOG_FUNCTION(this);
}

TcpImTcpBg::TcpImTcpBg(const TcpImTcpBg& sock)
    : TcpWestwoodPlus(sock),
      m_currentBW(sock.m_currentBW),
      m_lastSampleBW(sock.m_lastSampleBW),
      m_lastBW(sock.m_lastBW),
      m_fType(sock.m_fType),
      m_IsCount(sock.m_IsCount),
      m_gamma(sock.m_gamma),
      m_delta(sock.m_delta),
      m_abarBps(sock.m_abarBps),
      m_measureEveryRTT(sock.m_measureEveryRTT),
      m_minCwndSegments(sock.m_minCwndSegments),
      m_enableRTTFallback(sock.m_enableRTTFallback),
      m_abarBpsTrace(sock.m_abarBpsTrace)
{
    NS_LOG_FUNCTION(this);
    NS_LOG_LOGIC("Invoked the copy constructor");
}

TcpImTcpBg::~TcpImTcpBg()
{
}


void 
TcpImTcpBg::PktsAcked(Ptr<TcpSocketState> tcb, uint32_t packetsAcked, const Time& rtt){
    NS_LOG_FUNCTION(this << tcb << packetsAcked << rtt);

    if(rtt.IsZero()){
        NS_LOG_WARN("RTT measured is zero!");
        return;
    }

    if(!tcb->m_minRtt.IsZero() && m_enableRTTFallback){
        double ratio = rtt.GetSeconds() / tcb->m_minRtt.GetSeconds();
        if (ratio > m_delta)
        {
            double scale = tcb->m_minRtt.GetSeconds() / rtt.GetSeconds();
            uint32_t minCwnd = 2 * tcb->m_segmentSize;

            uint32_t newCwnd = static_cast<uint32_t>(tcb->m_cWnd.Get() * scale);
            if (newCwnd < minCwnd)
            {
                newCwnd = minCwnd;
            }

            tcb->m_cWnd = newCwnd;
        }
    }

    uint32_t cwndSegments = tcb->m_cWnd.Get() / tcb->m_segmentSize;
    if (cwndSegments < m_minCwndSegments)
    {
        m_ackedSegments = 0;
        m_IsCount = false;
        return;
    }

    m_ackedSegments += packetsAcked;

    if (!(rtt.IsZero() || m_IsCount))
    {
        m_IsCount = true;
        m_bwEstimateEvent.Cancel();

        Time interval = Seconds(rtt.GetSeconds() * m_measureEveryRTT);
        m_bwEstimateEvent = Simulator::Schedule(interval, &TcpImTcpBg::EstimateBW, this, interval, tcb);
    }
}


void
TcpImTcpBg::IncreaseWindow(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked){
    TcpWestwoodPlus::IncreaseWindow (tcb, segmentsAcked);

    if (tcb->m_minRtt.IsZero () || m_abarBps <= 0.0){
        return;
    }

    double maxCwndD = (m_abarBps * tcb->m_minRtt.GetSeconds ()) / 8.0;
    uint32_t maxCwnd = static_cast<uint32_t> (maxCwndD);

    uint32_t minCwnd = 2 * tcb->m_segmentSize;
    if (maxCwnd < minCwnd){
        maxCwnd = minCwnd;
    } 

    if (tcb->m_cWnd.Get () > maxCwnd){
    tcb->m_cWnd = maxCwnd;
    }
}

void
TcpImTcpBg::EstimateBW(const Time& rtt, Ptr<TcpSocketState> tcb)
{
    NS_LOG_FUNCTION(this);

    NS_ASSERT(!rtt.IsZero());

    m_currentBW = DataRate(m_ackedSegments * tcb->m_segmentSize * 8.0 / rtt.GetSeconds());
    double sampleBps = static_cast<double>(m_currentBW.Get().GetBitRate());

    
    if(m_abarBps <= 0.0){
        m_abarBps = sampleBps;
    }
    else{
        m_abarBps = (1 - m_gamma) * m_abarBps + m_gamma * sampleBps;
    }

    m_abarBpsTrace = m_abarBps;

    m_IsCount = false;

    m_ackedSegments = 0;

    NS_LOG_LOGIC("Estimated BW: " << m_currentBW);


    // Filter the BW sample

    constexpr double ALPHA = 0.9;

    if (m_fType == TcpImTcpBg::TUSTIN)
    {
        DataRate sample_bwe = m_currentBW;
        m_currentBW = (m_lastBW * ALPHA) + (((sample_bwe + m_lastSampleBW) * 0.5) * (1 - ALPHA));
        m_lastSampleBW = sample_bwe;
        m_lastBW = m_currentBW;
    }

    NS_LOG_LOGIC("Estimated BW after filtering: " << m_currentBW);
}

uint32_t
TcpImTcpBg::GetSsThresh(Ptr<const TcpSocketState> tcb, uint32_t bytesInFlight [[maybe_unused]])
{
    uint32_t ssThresh = static_cast<uint32_t>((m_currentBW * tcb->m_minRtt) / 8.0);

    NS_LOG_LOGIC("CurrentBW: " << m_currentBW << " minRtt: " << tcb->m_minRtt
                               << " ssThresh: " << ssThresh);

    return std::max(2 * tcb->m_segmentSize, ssThresh);
}

Ptr<TcpCongestionOps>
TcpImTcpBg::Fork()
{
    return CreateObject<TcpImTcpBg>(*this);
}

} // namespace ns3
