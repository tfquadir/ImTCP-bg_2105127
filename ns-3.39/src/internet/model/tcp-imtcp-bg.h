#ifndef TCP_IMTCP_BG_H
#define TCP_IMTCP_BG_H

// #include "tcp-newreno.h"
// #include "tcp-congestion-ops.h"
#include "tcp-westwood-plus.h"

#include "ns3/data-rate.h"
#include "ns3/event-id.h"
#include "ns3/tcp-recovery-ops.h"
#include "ns3/traced-value.h"


// !
#include "ns3/boolean.h"
#include "ns3/uinteger.h"



namespace ns3
{

class Time;

/**
 * \ingroup congestionOps
 *
 * \brief An implementation of TCP Westwood+.
 *
 * Westwood+ employ the AIAD (Additive Increase/Adaptive Decrease)
 * congestion control paradigm. When a congestion episode happens,
 * instead of halving the cwnd, these protocols try to estimate the network's
 * bandwidth and use the estimated value to adjust the cwnd.
 * While Westwood performs the bandwidth sampling every ACK reception,
 * Westwood+ samples the bandwidth every RTT.
 *
 * The two main methods in the implementation are the CountAck (const TCPHeader&)
 * and the EstimateBW (int, const, Time). The CountAck method calculates
 * the number of acknowledged segments on the receipt of an ACK.
 * The EstimateBW estimates the bandwidth based on the value returned by CountAck
 * and the sampling interval (last RTT).
 *
 * WARNING: this TCP model lacks validation and regression tests; use with caution.
 */
// class TcpImTcpBg : public TcpNewReno
class TcpImTcpBg : public TcpWestwoodPlus
{
  public:
    /**
     * \brief Get the type ID.
     * \return the object TypeId
     */
    static TypeId GetTypeId();

    TcpImTcpBg();
    /**
     * \brief Copy constructor
     * \param sock the object to copy
     */
    TcpImTcpBg(const TcpImTcpBg& sock);
    ~TcpImTcpBg() override;

    /**
     * \brief Filter type (None or Tustin)
     */
    enum FilterType
    {
        NONE,
        TUSTIN
    };

    uint32_t GetSsThresh(Ptr<const TcpSocketState> tcb, uint32_t bytesInFlight) override;

    void PktsAcked(Ptr<TcpSocketState> tcb, uint32_t packetsAcked, const Time& rtt) override;

    Ptr<TcpCongestionOps> Fork() override;

    void IncreaseWindow(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked) override;

  private:
    /**
     * Update the total number of acknowledged packets during the current RTT
     *
     * \param [in] acked the number of packets the currently received ACK acknowledges
     */
    void UpdateAckedSegments(int acked);

    /**
     * Estimate the network's bandwidth
     *
     * \param [in] rtt the RTT estimation.
     * \param [in] tcb the socket state.
     */
    void EstimateBW(const Time& rtt, Ptr<TcpSocketState> tcb);

  protected:
    TracedValue<DataRate> m_currentBW; //!< Current value of the estimated BW
    DataRate m_lastSampleBW;           //!< Last bandwidth sample
    DataRate m_lastBW;                 //!< Last bandwidth sample after being filtered
    FilterType m_fType;                //!< 0 for none, 1 for Tustin

    uint32_t m_ackedSegments;  //!< The number of segments ACKed between RTTs
    bool m_IsCount;            //!< Start keeping track of m_ackedSegments for Westwood+ if TRUE
    EventId m_bwEstimateEvent; //!< The BW estimation event for Westwood+
    Time m_lastAck;            //!< The last ACK time


    // !
    double m_gamma;  //!< gain
    double m_delta;  //!< RTT inflation
    double m_abarBps; 

    uint32_t m_measureEveryRTT; //!< measure bandwidth every K RTTs
    uint32_t m_minCwndSegments; //!< minimum conjestion window (cwnd) in segments
    bool m_enableRTTFallback; //!< enable/disable RTT inflation fallback

    TracedValue<double> m_abarBpsTrace; //!< mirrors m_abarBps for tracing
};

} // namespace ns3

#endif /* TCP_IMTCP_BG_H */
