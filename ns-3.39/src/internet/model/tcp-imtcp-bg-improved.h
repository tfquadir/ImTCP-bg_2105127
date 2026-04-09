#ifndef TCP_IMTCP_BG_IMPROVED_H
#define TCP_IMTCP_BG_IMPROVED_H

#include "tcp-westwood-plus.h"

#include "ns3/data-rate.h"
#include "ns3/event-id.h"
#include "ns3/tcp-recovery-ops.h"
#include "ns3/traced-value.h"
#include "ns3/boolean.h"
#include "ns3/uinteger.h"

namespace ns3
{

class Time;

/**
 * \ingroup congestionOps
 *
 * \brief Improved ImTCP-bg with adaptive RTT threshold (delta).
 *
 * Based on TcpWestwoodPlus, but adds:
 *  1) bandwidth-based cwnd limiting
 *  2) RTT inflation fallback
 *  3) adaptive delta tuning to avoid over-throttling
 */
class TcpImTcpBgImproved : public TcpWestwoodPlus
{
  public:
    /**
     * \brief Get the type ID.
     * \return the object TypeId
     */
    static TypeId GetTypeId();

    TcpImTcpBgImproved();
    /**
     * \brief Copy constructor
     * \param sock the object to copy
     */
    TcpImTcpBgImproved(const TcpImTcpBgImproved& sock);
    ~TcpImTcpBgImproved() override;

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

    // new helper for adaptive delta
    void UpdateAdaptiveDelta(double ratio);

  protected:
    TracedValue<DataRate> m_currentBW; //!< Current value of the estimated BW
    DataRate m_lastSampleBW;           //!< Last bandwidth sample
    DataRate m_lastBW;                 //!< Last bandwidth sample after being filtered
    FilterType m_fType;                //!< 0 for none, 1 for Tustin

    uint32_t m_ackedSegments;  //!< The number of segments ACKed between RTTs
    bool m_IsCount;            //!< Start keeping track of m_ackedSegments for Westwood+ if TRUE
    EventId m_bwEstimateEvent; //!< The BW estimation event for Westwood+
    Time m_lastAck;            //!< The last ACK time

    // original ImTCP-bg parameters
    double m_gamma;   //!< EWMA gain
    double m_delta;   //!< base RTT inflation threshold
    double m_abarBps; //!< smoothed BW estimate in bps

    uint32_t m_measureEveryRTT; //!< measure bandwidth every K RTTs
    uint32_t m_minCwndSegments; //!< minimum conjestion window (cwnd) in segments
    bool m_enableRTTFallback; //!< enable/disable RTT inflation fallback

    TracedValue<double> m_abarBpsTrace; //!< mirrors m_abarBps for tracing

    // improved / adaptive delta state 
    bool m_enableAdaptiveDelta;   //!< turn adaptive delta on/off
    double m_deltaCurrent;        //!< runtime delta actually used
    double m_deltaMin;            //!< lower bound for adaptive delta
    double m_deltaMax;            //!< upper bound for adaptive delta
    double m_deltaStepUp;         //!< increase step when path looks stable
    double m_deltaStepDown;       //!< decrease step when path looks congested
    uint32_t m_stableAckCount;    //!< consecutive stable ACK observations
    uint32_t m_congestedAckCount; //!< consecutive congested ACK observations
    uint32_t m_adaptWindow;       //!< observations before one delta adjustment

    TracedValue<double> m_deltaTrace; //!< runtime delta trace
};

} // namespace ns3

#endif /* TCP_IMTCP_BG_IMPROVED_H */