/*
 * Copyright (c) 2020 Universita' degli Studi di Napoli Federico II
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Stefano Avallone <stavallo@unina.it>
 */

#include "rr-multi-user-scheduler.h"

#include "he-configuration.h"
#include "he-frame-exchange-manager.h"
#include "he-phy.h"

#include "ns3/core-module.h"
#include "ns3/log.h"
#include "ns3/random-variable-stream.h"
#include "ns3/string.h"
#include "ns3/wifi-acknowledgment.h"
#include "ns3/wifi-mac-queue.h"
#include "ns3/wifi-protection.h"
#include "ns3/wifi-psdu.h"

#include <algorithm>
#include <numeric>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("RrMultiUserScheduler");

NS_OBJECT_ENSURE_REGISTERED(RrMultiUserScheduler);

TypeId
RrMultiUserScheduler::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::RrMultiUserScheduler")
            .SetParent<MultiUserScheduler>()
            .SetGroupName("Wifi")
            .AddConstructor<RrMultiUserScheduler>()
            .AddAttribute("NStations",
                          "The maximum number of stations that can be granted an RU in a DL MU "
                          "OFDMA transmission",
                          UintegerValue(4),
                          MakeUintegerAccessor(&RrMultiUserScheduler::m_nStations),
                          MakeUintegerChecker<uint8_t>(1, 74))
            .AddAttribute("EnableTxopSharing",
                          "If enabled, allow A-MPDUs of different TIDs in a DL MU PPDU.",
                          BooleanValue(true),
                          MakeBooleanAccessor(&RrMultiUserScheduler::m_enableTxopSharing),
                          MakeBooleanChecker())
            .AddAttribute("ForceDlOfdma",
                          "If enabled, return DL_MU_TX even if no DL MU PPDU could be built.",
                          BooleanValue(false),
                          MakeBooleanAccessor(&RrMultiUserScheduler::m_forceDlOfdma),
                          MakeBooleanChecker())
            .AddAttribute("EnableUlOfdma",
                          "If enabled, return UL_MU_TX if DL_MU_TX was returned the previous time.",
                          BooleanValue(true),
                          MakeBooleanAccessor(&RrMultiUserScheduler::m_enableUlOfdma),
                          MakeBooleanChecker())
            .AddAttribute("EnableBsrp",
                          "If enabled, send a BSRP Trigger Frame before an UL MU transmission.",
                          BooleanValue(true),
                          MakeBooleanAccessor(&RrMultiUserScheduler::m_enableBsrp),
                          MakeBooleanChecker())
            .AddAttribute(
                "UlPsduSize",
                "The default size in bytes of the solicited PSDU (to be sent in a TB PPDU)",
                UintegerValue(500),
                MakeUintegerAccessor(&RrMultiUserScheduler::m_ulPsduSize),
                MakeUintegerChecker<uint32_t>())
            .AddAttribute("UseCentral26TonesRus",
                          "If enabled, central 26-tone RUs are allocated, too, when the "
                          "selected RU type is at least 52 tones.",
                          BooleanValue(false),
                          MakeBooleanAccessor(&RrMultiUserScheduler::m_useCentral26TonesRus),
                          MakeBooleanChecker())
            .AddAttribute(
                "MaxCredits",
                "Maximum amount of credits a station can have. When transmitting a DL MU PPDU, "
                "the amount of credits received by each station equals the TX duration (in "
                "microseconds) divided by the total number of stations. Stations that are the "
                "recipient of the DL MU PPDU have to pay a number of credits equal to the TX "
                "duration (in microseconds) times the allocated bandwidth share",
                TimeValue(Seconds(1)),
                MakeTimeAccessor(&RrMultiUserScheduler::m_maxCredits),
                MakeTimeChecker())
            .AddAttribute("ChannelSoundingInterval",
                          "Duration of the interval between two consecutive channel sounding "
                          "processes. If the interval is 0, then channel sounding is disabled.",
                          TimeValue(MilliSeconds(10)),
                          MakeTimeAccessor(&RrMultiUserScheduler::m_csInterval),
                          MakeTimeChecker())
            // attempt to add Channel Sounding from ns3.37 : add new attributes for class RR Multi
            // User Scheduler
            .AddAttribute("EnableMuMimo",
                          "If enabled, MU-MIMO instead of OFDMA is used.",
                          BooleanValue(false),
                          MakeBooleanAccessor(&RrMultiUserScheduler::m_enableMuMimo),
                          MakeBooleanChecker())
            .AddAttribute("NumStreamPerSta",
                          "The number of streams sent to each station",
                          UintegerValue(1),
                          MakeUintegerAccessor(&RrMultiUserScheduler::m_nssPerSta),
                          MakeUintegerChecker<uint8_t>())
            .AddAttribute("MaxNumDlMuMimoSta",
                          "Maximum number of stations for Dl MU-MIMO transmission",
                          UintegerValue(1),
                          MakeUintegerAccessor(&RrMultiUserScheduler::m_maxNumDlMuMimoSta),
                          MakeUintegerChecker<uint8_t>())
            // attempt to add MU OFDMA : add new attributes for class RR Multi User Scheduler
            .AddAttribute("SoundingType",
                          "The type of sounding to be used",
                          UintegerValue(2),
                          MakeUintegerAccessor(&RrMultiUserScheduler::m_soundingType),
                          MakeUintegerChecker<uint8_t>(0, 3));
    return tid;
}

RrMultiUserScheduler::RrMultiUserScheduler()
    // attempt to add Channel Sounding from ns3.37 : modification in constructor for initial values
    : m_nssPerSta(1),
      m_csStart(false)
{
    NS_LOG_FUNCTION(this);
}

RrMultiUserScheduler::~RrMultiUserScheduler()
{
    NS_LOG_FUNCTION_NOARGS();
}

void
RrMultiUserScheduler::DoInitialize()
{
    NS_LOG_FUNCTION(this);
    NS_ASSERT(m_apMac);
    m_apMac->TraceConnectWithoutContext(
        "AssociatedSta",
        MakeCallback(&RrMultiUserScheduler::NotifyStationAssociated, this));
    m_apMac->TraceConnectWithoutContext(
        "DeAssociatedSta",
        MakeCallback(&RrMultiUserScheduler::NotifyStationDeassociated, this));
    for (const auto& ac : wifiAcList)
    {
        m_staListDl.insert({ac.first, {}});
    }
    MultiUserScheduler::DoInitialize();
}

void
RrMultiUserScheduler::DoDispose()
{
    NS_LOG_FUNCTION(this);
    m_staListDl.clear();
    m_staListUl.clear();
    m_candidates.clear();
    m_txParams.Clear();
    m_apMac->TraceDisconnectWithoutContext(
        "AssociatedSta",
        MakeCallback(&RrMultiUserScheduler::NotifyStationAssociated, this));
    m_apMac->TraceDisconnectWithoutContext(
        "DeAssociatedSta",
        MakeCallback(&RrMultiUserScheduler::NotifyStationDeassociated, this));
    MultiUserScheduler::DoDispose();
}

MultiUserScheduler::TxFormat
RrMultiUserScheduler::SelectTxFormat()
{
    NS_LOG_FUNCTION(this);

    // attempt to add Channel Sounding from ns3.37 : add new TxFormat for channel sounding
    TxFormat txformatCs;

    Ptr<const WifiMpdu> mpdu = m_edca->PeekNextMpdu(m_linkId);
    if (mpdu)
    {
        // std::cout << "mpdu" << mpdu->GetPacket() << std::endl;
        // std::cout << "rr-multi-user-scheduler SelectTxFormat: next MPDU for link " << +m_linkId << " header: " << mpdu->GetHeader() << std::endl;
    }
    else {
        // std::cout << "rr-multi-user-scheduler SelectTxFormat: no MPDU for link " << +m_linkId << std::endl;
    }

    if (m_apMac->IsInCfp())
    {
        if (mpdu)
        {
            const WifiMacHeader& hdr = mpdu->GetHeader();
            bool isSensingFrame = hdr.IsCfPoll()           // WIFI_MAC_QOSDATA_CFPOLL
                            || hdr.IsTrigger()        // WIFI_MAC_CTL_TRIGGER
                            || hdr.IsCts()               // WIFI_MAC_CTS
                            || hdr.IsNdpa()              // WIFI_MAC_CTL_NDPA
                            || hdr.IsData() && !hdr.HasData()  // WIFI_MAC_DATA_NULL
                            || hdr.IsActionNoAck()       // WIFI_MGT_NO_ACK (BFRP)
                            || hdr.IsAction();           // WIFI_MGT_ACTION (BFRP report)

            if (!isSensingFrame)
            {
                NS_LOG_DEBUG("CFP active: blocking non-sensing frame " 
                            << hdr.GetTypeString());
                // std::cout << "CFP active: blocking non-sensing frame " 
                //             << hdr.GetTypeString() << std::endl;
                return NO_TX;
            }
        }
        else
        {
            // No sensing frame ready, nothing to send during CFP
            return NO_TX;
        }
    }
    // if (mpdu)
    if (IsPCFEnabled() && m_enableMuMimo)
    {
        // 802.11bf sensing message exchange: the AP holds an ordered sequence
        // of SensingPhase values. The scheduler walks that sequence, dispatching
        // one phase per SelectTxFormat() call while a CF-Poll MPDU is at the
        // head of the queue. The sequence resets at the start of each CFP.
        if (mpdu && mpdu->GetHeader().IsCfPoll())
        {
            if (IsChannelSoundingEnabled())
            {
                const auto& seq = m_apMac->GetSensingPhaseSequence();
                NS_ABORT_MSG_IF(seq.empty(),
                                "AP SensingPhaseSequence must contain at least one phase");

                const TxFormat lastFmt = GetLastTxFormat(0U);

                // Decide whether to advance the phase index or stay put.
                // "Stay" only applies to SU NDPA sounding that needs another
                // iteration (m_nextSUSounding). Anything else either advances
                // the index (phase just finished) or resets to 0 (new cycle).
                bool startingNewCycle = (lastFmt != BF_POLL_DL_TX &&
                                         lastFmt != BF_NDPA_SOUNDING_TX &&
                                         lastFmt != BF_NDPA_SOUNDING_TX_SU &&
                                         lastFmt != BF_TF_SOUNDING_TX);

                if (startingNewCycle)
                {
                    m_sensingPhaseIdx = 0;
                }
                else if (lastFmt == BF_NDPA_SOUNDING_TX_SU && m_nextSUSounding)
                {
                    // keep the same index: the SU NDPA phase continues
                }
                else
                {
                    m_sensingPhaseIdx = (m_sensingPhaseIdx + 1) % seq.size();
                }

                // Skip REPORT entries: they are implicit in response handling.
                // Use modulo so we wrap around and never stall after a REPORT
                // that appears at the tail of the sequence.
                size_t safety = 0;
                while (seq[m_sensingPhaseIdx] == SensingPhase::REPORT &&
                       safety++ < seq.size())
                {
                    m_sensingPhaseIdx = (m_sensingPhaseIdx + 1) % seq.size();
                }

                if (seq[m_sensingPhaseIdx] == SensingPhase::REPORT)
                {
                    // Every entry in the sequence is REPORT; nothing to dispatch.
                    return NO_TX;
                }

                switch (seq[m_sensingPhaseIdx])
                {
                case SensingPhase::POLLING:
                    return TryPollingPhase11bf();
                case SensingPhase::NDPA_SOUNDING:
                    return TryNDPASoundingPhase11bf();
                case SensingPhase::TF_SOUNDING:
                    return TryTFSoundingPhase11bf();
                case SensingPhase::REPORT:
                    return NO_TX;
                }
            }
            TxFormat txFormat = TryPollingPhase11bf();
            return txFormat;
        }
    }

    if (mpdu && !m_apMac->GetHeSupported(mpdu->GetHeader().GetAddr1()))
    {
        // std::cout << "Next MPDU is for a non-HE station, sending SU_TX SU PPDU" << std::endl;
        return SU_TX;
    }

    if (m_enableUlOfdma && m_enableBsrp && (GetLastTxFormat(m_linkId) == DL_MU_TX || !mpdu))
    {
        TxFormat txFormat = TrySendingBsrpTf();

        // std::cout << "txFormat returned by TrySendingBsrpTf() : " << txFormat << std::endl;

        if (txFormat != DL_MU_TX)
        {
            return txFormat;
        }
    }
    else if (m_enableUlOfdma && ((GetLastTxFormat(m_linkId) == DL_MU_TX) ||
                                 (m_trigger.GetType() == TriggerFrameType::BSRP_TRIGGER) || !mpdu))
    {
        TxFormat txFormat = TrySendingBasicTf();

        if (txFormat != DL_MU_TX)
        {
            return txFormat;
        }
    }
    // std::cout << "TrySendingDlMuPpdu because no other TxFormat was selected" << std::endl;
    return TrySendingDlMuPpdu();
}

template <class Func>
WifiTxVector
RrMultiUserScheduler::GetTxVectorForUlMu(Func canBeSolicited)
{
    NS_LOG_FUNCTION(this);

    // determine RUs to allocate to stations
    auto count = std::min<std::size_t>(m_nStations, m_staListUl.size());
    std::size_t nCentral26TonesRus;
    HeRu::GetEqualSizedRusForStations(m_allowedWidth, count, nCentral26TonesRus);
    NS_ASSERT(count >= 1);

    if (!m_useCentral26TonesRus)
    {
        nCentral26TonesRus = 0;
    }

    Ptr<HeConfiguration> heConfiguration = m_apMac->GetHeConfiguration();
    NS_ASSERT(heConfiguration);

    WifiTxVector txVector;
    txVector.SetPreambleType(WIFI_PREAMBLE_HE_TB);
    txVector.SetChannelWidth(m_allowedWidth);
    txVector.SetGuardInterval(heConfiguration->GetGuardInterval().GetNanoSeconds());
    txVector.SetBssColor(heConfiguration->GetBssColor());

    // iterate over the associated stations until an enough number of stations is identified
    auto staIt = m_staListUl.begin();
    m_candidates.clear();

    while (staIt != m_staListUl.end() &&
           txVector.GetHeMuUserInfoMap().size() <
               std::min<std::size_t>(m_nStations, count + nCentral26TonesRus))
    {
        NS_LOG_DEBUG("Next candidate STA (MAC=" << staIt->address << ", AID=" << staIt->aid << ")");

        if (!canBeSolicited(*staIt))
        {
            NS_LOG_DEBUG("Skipping station based on provided function object");
            staIt++;
            continue;
        }

        if (txVector.GetPreambleType() == WIFI_PREAMBLE_EHT_TB &&
            !m_apMac->GetEhtSupported(staIt->address))
        {
            NS_LOG_DEBUG(
                "Skipping non-EHT STA because this Trigger Frame is only soliciting EHT STAs");
            staIt++;
            continue;
        }

        uint8_t tid = 0;
        while (tid < 8)
        {
            // check that a BA agreement is established with the receiver for the
            // considered TID, since ack sequences for UL MU require block ack
            if (m_apMac->GetBaAgreementEstablishedAsRecipient(staIt->address, tid))
            {
                break;
            }
            ++tid;
        }
        if (tid == 8)
        {
            NS_LOG_DEBUG("No Block Ack agreement established with " << staIt->address);
            staIt++;
            continue;
        }

        // if the first candidate STA is an EHT STA, we switch to soliciting EHT TB PPDUs
        if (txVector.GetHeMuUserInfoMap().empty())
        {
            if (m_apMac->GetEhtSupported() && m_apMac->GetEhtSupported(staIt->address))
            {
                txVector.SetPreambleType(WIFI_PREAMBLE_EHT_TB);
                txVector.SetEhtPpduType(0);
            }
            // TODO otherwise, make sure the TX width does not exceed 160 MHz
        }

        // prepare the MAC header of a frame that would be sent to the candidate station,
        // just for the purpose of retrieving the TXVECTOR used to transmit to that station
        // std::cout << "GetTxVectorForUlMu: candidate station "                   << std::endl;
        WifiMacHeader hdr(WIFI_MAC_QOSDATA);
        hdr.SetAddr1(GetWifiRemoteStationManager(m_linkId)
                         ->GetAffiliatedStaAddress(staIt->address)
                         .value_or(staIt->address));
        hdr.SetAddr2(m_apMac->GetFrameExchangeManager(m_linkId)->GetAddress());
        WifiTxVector suTxVector =
            GetWifiRemoteStationManager(m_linkId)->GetDataTxVector(hdr, m_allowedWidth);
        txVector.SetHeMuUserInfo(staIt->aid,
                                 {HeRu::RuSpec(), // assigned later by FinalizeTxVector
                                  suTxVector.GetMode().GetMcsValue(),
                                  suTxVector.GetNss()});
        m_candidates.emplace_back(staIt, nullptr);

        // move to the next station in the list
        staIt++;
    }

    if (txVector.GetHeMuUserInfoMap().empty())
    {
        NS_LOG_DEBUG("No suitable station");
        return txVector;
    }

    FinalizeTxVector(txVector);
    return txVector;
}

MultiUserScheduler::TxFormat
RrMultiUserScheduler::TrySendingBsrpTf()
{
    NS_LOG_FUNCTION(this);

    if (m_staListUl.empty())
    {
        NS_LOG_DEBUG("No HE stations associated: return SU_TX");
        return TxFormat::SU_TX;
    }

    // only consider stations that have setup the current link
    WifiTxVector txVector = GetTxVectorForUlMu([this](const MasterInfo& info) {
        const auto& staList = m_apMac->GetStaList(m_linkId);
        return staList.find(info.aid) != staList.cend();
    });

    if (txVector.GetHeMuUserInfoMap().empty())
    {
        NS_LOG_DEBUG("No suitable station found");
        return TxFormat::DL_MU_TX;
    }

    m_trigger = CtrlTriggerHeader(TriggerFrameType::BSRP_TRIGGER, txVector);
    txVector.SetGuardInterval(m_trigger.GetGuardInterval());
    // std::cout << "TrySendingBsrpTf: selected stations for BSRP Trigger Frame: " << std::endl;
    auto item = GetTriggerFrame(m_trigger, m_linkId);
    m_triggerMacHdr = item->GetHeader();

    m_txParams.Clear();
    // set the TXVECTOR used to send the Trigger Frame
    m_txParams.m_txVector =
        m_apMac->GetWifiRemoteStationManager(m_linkId)->GetRtsTxVector(m_triggerMacHdr.GetAddr1());

    if (!GetHeFem(m_linkId)->TryAddMpdu(item, m_txParams, m_availableTime))
    {
        // sending the BSRP Trigger Frame is not possible, hence return NO_TX. In
        // this way, no transmission will occur now and the next time we will
        // try again sending a BSRP Trigger Frame.
        NS_LOG_DEBUG("Remaining TXOP duration is not enough for BSRP TF exchange");
        return NO_TX;
    }

    // Compute the time taken by each station to transmit 8 QoS Null frames
    Time qosNullTxDuration = Seconds(0);
    for (const auto& userInfo : m_trigger)
    {
        Time duration = WifiPhy::CalculateTxDuration(GetMaxSizeOfQosNullAmpdu(m_trigger),
                                                     txVector,
                                                     m_apMac->GetWifiPhy(m_linkId)->GetPhyBand(),
                                                     userInfo.GetAid12());
        qosNullTxDuration = Max(qosNullTxDuration, duration);
    }

    if (m_availableTime != Time::Min())
    {
        // TryAddMpdu only considers the time to transmit the Trigger Frame
        NS_ASSERT(m_txParams.m_protection &&
                  m_txParams.m_protection->protectionTime != Time::Min());
        NS_ASSERT(m_txParams.m_acknowledgment &&
                  m_txParams.m_acknowledgment->acknowledgmentTime.IsZero());
        NS_ASSERT(m_txParams.m_txDuration != Time::Min());

        if (m_txParams.m_protection->protectionTime + m_txParams.m_txDuration // BSRP TF tx time
                + m_apMac->GetWifiPhy(m_linkId)->GetSifs() + qosNullTxDuration >
            m_availableTime)
        {
            NS_LOG_DEBUG("Remaining TXOP duration is not enough for BSRP TF exchange");
            return NO_TX;
        }
    }

    uint16_t ulLength;
    std::tie(ulLength, qosNullTxDuration) = HePhy::ConvertHeTbPpduDurationToLSigLength(
        qosNullTxDuration,
        m_trigger.GetHeTbTxVector(m_trigger.begin()->GetAid12()),
        m_apMac->GetWifiPhy(m_linkId)->GetPhyBand());
    NS_LOG_DEBUG("Duration of QoS Null frames: " << qosNullTxDuration.As(Time::MS));
    m_trigger.SetUlLength(ulLength);

    return UL_MU_TX;
}

MultiUserScheduler::TxFormat
RrMultiUserScheduler::TrySendingBasicTf()
{
    NS_LOG_FUNCTION(this);

    if (m_staListUl.empty())
    {
        NS_LOG_DEBUG("No HE stations associated: return SU_TX");
        return TxFormat::SU_TX;
    }

    // check if an UL OFDMA transmission is possible after a DL OFDMA transmission
    NS_ABORT_MSG_IF(m_ulPsduSize == 0, "The UlPsduSize attribute must be set to a non-null value");

    // only consider stations that have setup the current link and do not have
    // reported a null queue size
    WifiTxVector txVector = GetTxVectorForUlMu([this](const MasterInfo& info) {
        const auto& staList = m_apMac->GetStaList(m_linkId);
        return staList.find(info.aid) != staList.cend() &&
               m_apMac->GetMaxBufferStatus(info.address) > 0;
    });

    if (txVector.GetHeMuUserInfoMap().empty())
    {
        NS_LOG_DEBUG("No suitable station found");
        return TxFormat::DL_MU_TX;
    }

    uint32_t maxBufferSize = 0;

    for (const auto& candidate : txVector.GetHeMuUserInfoMap())
    {
        auto address = m_apMac->GetMldOrLinkAddressByAid(candidate.first);
        NS_ASSERT_MSG(address, "AID " << candidate.first << " not found");

        uint8_t queueSize = m_apMac->GetMaxBufferStatus(*address);
        if (queueSize == 255)
        {
            NS_LOG_DEBUG("Buffer status of station " << *address << " is unknown");
            maxBufferSize = std::max(maxBufferSize, m_ulPsduSize);
        }
        else if (queueSize == 254)
        {
            NS_LOG_DEBUG("Buffer status of station " << *address << " is not limited");
            maxBufferSize = 0xffffffff;
        }
        else
        {
            NS_LOG_DEBUG("Buffer status of station " << *address << " is " << +queueSize);
            maxBufferSize = std::max(maxBufferSize, static_cast<uint32_t>(queueSize * 256));
        }
    }

    if (maxBufferSize == 0)
    {
        return DL_MU_TX;
    }

    m_trigger = CtrlTriggerHeader(TriggerFrameType::BASIC_TRIGGER, txVector);
    txVector.SetGuardInterval(m_trigger.GetGuardInterval());
    // std::cout << "TrySendingBasicTf: selected stations for BASIC Trigger Frame: " << std::endl;
    auto item = GetTriggerFrame(m_trigger, m_linkId);
    m_triggerMacHdr = item->GetHeader();

    // compute the maximum amount of time that can be granted to stations.
    // This value is limited by the max PPDU duration
    Time maxDuration = GetPpduMaxTime(txVector.GetPreambleType());

    m_txParams.Clear();
    // set the TXVECTOR used to send the Trigger Frame
    m_txParams.m_txVector =
        m_apMac->GetWifiRemoteStationManager(m_linkId)->GetRtsTxVector(m_triggerMacHdr.GetAddr1());

    if (!GetHeFem(m_linkId)->TryAddMpdu(item, m_txParams, m_availableTime))
    {
        // an UL OFDMA transmission is not possible, hence return NO_TX. In
        // this way, no transmission will occur now and the next time we will
        // try again performing an UL OFDMA transmission.
        NS_LOG_DEBUG("Remaining TXOP duration is not enough for UL MU exchange");
        return NO_TX;
    }

    if (m_availableTime != Time::Min())
    {
        // TryAddMpdu only considers the time to transmit the Trigger Frame
        NS_ASSERT(m_txParams.m_protection &&
                  m_txParams.m_protection->protectionTime != Time::Min());
        NS_ASSERT(m_txParams.m_acknowledgment &&
                  m_txParams.m_acknowledgment->acknowledgmentTime != Time::Min());
        NS_ASSERT(m_txParams.m_txDuration != Time::Min());

        maxDuration = Min(maxDuration,
                          m_availableTime - m_txParams.m_protection->protectionTime -
                              m_txParams.m_txDuration - m_apMac->GetWifiPhy(m_linkId)->GetSifs() -
                              m_txParams.m_acknowledgment->acknowledgmentTime);
        if (maxDuration.IsNegative())
        {
            NS_LOG_DEBUG("Remaining TXOP duration is not enough for UL MU exchange");
            return NO_TX;
        }
    }

    // Compute the time taken by each station to transmit a frame of maxBufferSize size
    Time bufferTxTime = Seconds(0);
    for (const auto& userInfo : m_trigger)
    {
        Time duration = WifiPhy::CalculateTxDuration(maxBufferSize,
                                                     txVector,
                                                     m_apMac->GetWifiPhy(m_linkId)->GetPhyBand(),
                                                     userInfo.GetAid12());
        bufferTxTime = Max(bufferTxTime, duration);
    }

    if (bufferTxTime < maxDuration)
    {
        // the maximum buffer size can be transmitted within the allowed time
        maxDuration = bufferTxTime;
    }
    else
    {
        // maxDuration may be a too short time. If it does not allow any station to
        // transmit at least m_ulPsduSize bytes, give up the UL MU transmission for now
        Time minDuration = Seconds(0);
        for (const auto& userInfo : m_trigger)
        {
            Time duration =
                WifiPhy::CalculateTxDuration(m_ulPsduSize,
                                             txVector,
                                             m_apMac->GetWifiPhy(m_linkId)->GetPhyBand(),
                                             userInfo.GetAid12());
            minDuration = (minDuration.IsZero() ? duration : Min(minDuration, duration));
        }

        if (maxDuration < minDuration)
        {
            // maxDuration is a too short time, hence return NO_TX. In this way,
            // no transmission will occur now and the next time we will try again
            // performing an UL OFDMA transmission.
            NS_LOG_DEBUG("Available time " << maxDuration.As(Time::MS) << " is too short");
            return NO_TX;
        }
    }

    // maxDuration is the time to grant to the stations. Finalize the Trigger Frame
    uint16_t ulLength;
    std::tie(ulLength, maxDuration) =
        HePhy::ConvertHeTbPpduDurationToLSigLength(maxDuration,
                                                   txVector,
                                                   m_apMac->GetWifiPhy(m_linkId)->GetPhyBand());
    NS_LOG_DEBUG("TB PPDU duration: " << maxDuration.As(Time::MS));
    m_trigger.SetUlLength(ulLength);
    // set Preferred AC to the AC that gained channel access
    for (auto& userInfo : m_trigger)
    {
        userInfo.SetBasicTriggerDepUserInfo(0, 0, m_edca->GetAccessCategory());
    }

    UpdateCredits(m_staListUl, maxDuration, txVector);

    return UL_MU_TX;
}

void
RrMultiUserScheduler::NotifyStationAssociated(uint16_t aid, Mac48Address address)
{
    NS_LOG_FUNCTION(this << aid << address);

    if (!m_apMac->GetHeSupported(address))
    {
        return;
    }

    auto mldOrLinkAddress = m_apMac->GetMldOrLinkAddressByAid(aid);
    NS_ASSERT_MSG(mldOrLinkAddress, "AID " << aid << " not found");
    for (auto& staList : m_staListDl)
    {
        // if this is not the first STA of a non-AP MLD to be notified, an entry
        // for this non-AP MLD already exists
        const auto staIt = std::find_if(staList.second.cbegin(),
                                        staList.second.cend(),
                                        [aid](auto&& info) { return info.aid == aid; });
        if (staIt == staList.second.cend())
        {
            staList.second.push_back(MasterInfo{aid, *mldOrLinkAddress, 0.0});
        }
    }

    const auto staIt = std::find_if(m_staListUl.cbegin(), m_staListUl.cend(), [aid](auto&& info) {
        return info.aid == aid;
    });
    if (staIt == m_staListUl.cend())
    {
        m_staListUl.push_back(MasterInfo{aid, *mldOrLinkAddress, 0.0});
    }
}

void
RrMultiUserScheduler::NotifyStationDeassociated(uint16_t aid, Mac48Address address)
{
    NS_LOG_FUNCTION(this << aid << address);

    if (!m_apMac->GetHeSupported(address))
    {
        return;
    }

    auto mldOrLinkAddress = m_apMac->GetMldOrLinkAddressByAid(aid);
    NS_ASSERT_MSG(mldOrLinkAddress, "AID " << aid << " not found");

    if (m_apMac->IsAssociated(*mldOrLinkAddress))
    {
        // Another STA of the non-AP MLD is still associated
        return;
    }

    for (auto& staList : m_staListDl)
    {
        staList.second.remove_if([&aid](const MasterInfo& info) { return info.aid == aid; });
    }
    m_staListUl.remove_if([&aid](const MasterInfo& info) { return info.aid == aid; });
}

MultiUserScheduler::TxFormat
RrMultiUserScheduler::TrySendingDlMuPpdu()
{
    NS_LOG_FUNCTION(this);

    AcIndex primaryAc = m_edca->GetAccessCategory();

    if (m_staListDl[primaryAc].empty())
    {
        NS_LOG_DEBUG("No HE stations associated: return SU_TX");
        return TxFormat::SU_TX;
    }

    std::size_t count =
        std::min(static_cast<std::size_t>(m_nStations), m_staListDl[primaryAc].size());
    std::size_t nCentral26TonesRus;
    HeRu::RuType ruType =
        HeRu::GetEqualSizedRusForStations(m_allowedWidth, count, nCentral26TonesRus);
    NS_ASSERT(count >= 1);

    if (!m_useCentral26TonesRus)
    {
        nCentral26TonesRus = 0;
    }

    uint8_t currTid = wifiAcList.at(primaryAc).GetHighTid();

    Ptr<WifiMpdu> mpdu = m_edca->PeekNextMpdu(m_linkId);

    if (mpdu && mpdu->GetHeader().IsQosData())
    {
        currTid = mpdu->GetHeader().GetQosTid();
    }

    // determine the list of TIDs to check
    std::vector<uint8_t> tids;

    if (m_enableTxopSharing)
    {
        for (auto acIt = wifiAcList.find(primaryAc); acIt != wifiAcList.end(); acIt++)
        {
            uint8_t firstTid = (acIt->first == primaryAc ? currTid : acIt->second.GetHighTid());
            tids.push_back(firstTid);
            tids.push_back(acIt->second.GetOtherTid(firstTid));
        }
    }
    else
    {
        tids.push_back(currTid);
    }

    Ptr<HeConfiguration> heConfiguration = m_apMac->GetHeConfiguration();
    NS_ASSERT(heConfiguration);

    m_txParams.Clear();
    m_txParams.m_txVector.SetPreambleType(WIFI_PREAMBLE_HE_MU);
    m_txParams.m_txVector.SetChannelWidth(m_allowedWidth);
    m_txParams.m_txVector.SetGuardInterval(heConfiguration->GetGuardInterval().GetNanoSeconds());
    m_txParams.m_txVector.SetBssColor(heConfiguration->GetBssColor());

    // The TXOP limit can be exceeded by the TXOP holder if it does not transmit more
    // than one Data or Management frame in the TXOP and the frame is not in an A-MPDU
    // consisting of more than one MPDU (Sec. 10.22.2.8 of 802.11-2016).
    // For the moment, we are considering just one MPDU per receiver.
    Time actualAvailableTime = (m_initialFrame ? Time::Min() : m_availableTime);

    // iterate over the associated stations until an enough number of stations is identified
    auto staIt = m_staListDl[primaryAc].begin();
    m_candidates.clear();

    std::vector<uint8_t> ruAllocations;
    auto numRuAllocs = m_txParams.m_txVector.GetChannelWidth() / 20;
    ruAllocations.resize(numRuAllocs);
    NS_ASSERT((m_candidates.size() % numRuAllocs) == 0);

    while (staIt != m_staListDl[primaryAc].end() &&
           m_candidates.size() <
               std::min(static_cast<std::size_t>(m_nStations), count + nCentral26TonesRus))
    {
        NS_LOG_DEBUG("Next candidate STA (MAC=" << staIt->address << ", AID=" << staIt->aid << ")");

        if (m_txParams.m_txVector.GetPreambleType() == WIFI_PREAMBLE_EHT_MU &&
            !m_apMac->GetEhtSupported(staIt->address))
        {
            NS_LOG_DEBUG("Skipping non-EHT STA because this DL MU PPDU is sent to EHT STAs only");
            staIt++;
            continue;
        }

        HeRu::RuType currRuType = (m_candidates.size() < count ? ruType : HeRu::RU_26_TONE);

        // check if the AP has at least one frame to be sent to the current station
        for (uint8_t tid : tids)
        {
            AcIndex ac = QosUtilsMapTidToAc(tid);
            NS_ASSERT(ac >= primaryAc);
            // check that a BA agreement is established with the receiver for the
            // considered TID, since ack sequences for DL MU PPDUs require block ack
        //     std::cout << "Checking STA " << staIt->address 
        //   << " BA established: " 
        //   << m_apMac->GetBaAgreementEstablishedAsOriginator(staIt->address, tid).has_value()
        //   << std::endl;
            if (m_apMac->GetBaAgreementEstablishedAsOriginator(staIt->address, tid))
            {
                mpdu = m_apMac->GetQosTxop(ac)->PeekNextMpdu(m_linkId, tid, staIt->address);

                // we only check if the first frame of the current TID meets the size
                // and duration constraints. We do not explore the queues further.
                if (mpdu)
                {
                    mpdu = GetHeFem(m_linkId)->CreateAliasIfNeeded(mpdu);
                    // Use a temporary TX vector including only the STA-ID of the
                    // candidate station to check if the MPDU meets the size and time limits.
                    // An RU of the computed size is tentatively assigned to the candidate
                    // station, so that the TX duration can be correctly computed.
                    WifiTxVector suTxVector =
                        GetWifiRemoteStationManager(m_linkId)->GetDataTxVector(mpdu->GetHeader(),
                                                                               m_allowedWidth);

                    WifiTxVector txVectorCopy = m_txParams.m_txVector;

                    // the first candidate STA determines the preamble type for the DL MU PPDU
                    if (m_candidates.empty() &&
                        suTxVector.GetPreambleType() == WIFI_PREAMBLE_EHT_MU)
                    {
                        m_txParams.m_txVector.SetPreambleType(WIFI_PREAMBLE_EHT_MU);
                        m_txParams.m_txVector.SetEhtPpduType(0); // indicates DL OFDMA transmission
                    }

                    m_txParams.m_txVector.SetHeMuUserInfo(staIt->aid,
                                                          {{currRuType, 1, true},
                                                           suTxVector.GetMode().GetMcsValue(),
                                                           suTxVector.GetNss()});

                    if (!GetHeFem(m_linkId)->TryAddMpdu(mpdu, m_txParams, actualAvailableTime))
                    {
                        NS_LOG_DEBUG("Adding the peeked frame violates the time constraints");
                        m_txParams.m_txVector = txVectorCopy;
                    }
                    else
                    {
                        // the frame meets the constraints
                        NS_LOG_DEBUG("Adding candidate STA (MAC=" << staIt->address
                                                                  << ", AID=" << staIt->aid
                                                                  << ") TID=" << +tid);
                        m_candidates.emplace_back(staIt, mpdu);
                        break; // terminate the for loop
                    }
                }
                else
                {
                    NS_LOG_DEBUG("No frames to send to " << staIt->address << " with TID=" << +tid);
                }
            }
            else
            {
                // No BA agreement established yet for this station.
                // Check if it has frames waiting — if so, fall back to SU_TX
                // so HtFrameExchangeManager can trigger the ADDBA handshake.
                AcIndex ac = QosUtilsMapTidToAc(tid);
                mpdu = m_apMac->GetQosTxop(ac)->PeekNextMpdu(m_linkId, tid, staIt->address);
                if (mpdu && m_candidates.empty())
                {
                    // Move this station to end of list so next SU_TX call
                    // starts fresh and rotates to other stations afterwards
                    m_staListDl[primaryAc].splice(m_staListDl[primaryAc].end(),
                                                m_staListDl[primaryAc],
                                                staIt);
                    NS_LOG_DEBUG("No BA for " << staIt->address << ", falling back to SU_TX for ADDBA");
                    return SU_TX;
                }
            }
        }

        // move to the next station in the list
        staIt++;
    }

    if (m_candidates.empty())
    {
        if (m_forceDlOfdma)
        {
            NS_LOG_DEBUG("The AP does not have suitable frames to transmit: return NO_TX");
            return NO_TX;
        }
        NS_LOG_DEBUG("The AP does not have suitable frames to transmit: return SU_TX");
        return SU_TX;
    }

    return TxFormat::DL_MU_TX;
}

void
RrMultiUserScheduler::FinalizeTxVector(WifiTxVector& txVector)
{
    // Do not log txVector because GetTxVectorForUlMu() left RUs undefined and
    // printing them will crash the simulation
    NS_LOG_FUNCTION(this);
    NS_ASSERT(txVector.GetHeMuUserInfoMap().size() == m_candidates.size());

    // compute how many stations can be granted an RU and the RU size
    std::size_t nRusAssigned = m_candidates.size();
    std::size_t nCentral26TonesRus;
    HeRu::RuType ruType =
        HeRu::GetEqualSizedRusForStations(m_allowedWidth, nRusAssigned, nCentral26TonesRus);

    NS_LOG_DEBUG(nRusAssigned << " stations are being assigned a " << ruType << " RU");

    if (!m_useCentral26TonesRus || m_candidates.size() == nRusAssigned)
    {
        nCentral26TonesRus = 0;
    }
    else
    {
        nCentral26TonesRus = std::min(m_candidates.size() - nRusAssigned, nCentral26TonesRus);
        NS_LOG_DEBUG(nCentral26TonesRus << " stations are being assigned a 26-tones RU");
    }

    // re-allocate RUs based on the actual number of candidate stations
    WifiTxVector::HeMuUserInfoMap heMuUserInfoMap;
    std::swap(heMuUserInfoMap, txVector.GetHeMuUserInfoMap());

    auto candidateIt = m_candidates.begin(); // iterator over the list of candidate receivers
    auto ruSet = HeRu::GetRusOfType(m_allowedWidth, ruType);
    auto ruSetIt = ruSet.begin();
    auto central26TonesRus = HeRu::GetCentral26TonesRus(m_allowedWidth, ruType);
    auto central26TonesRusIt = central26TonesRus.begin();

    for (std::size_t i = 0; i < nRusAssigned + nCentral26TonesRus; i++)
    {
        NS_ASSERT(candidateIt != m_candidates.end());
        auto mapIt = heMuUserInfoMap.find(candidateIt->first->aid);
        NS_ASSERT(mapIt != heMuUserInfoMap.end());

        txVector.SetHeMuUserInfo(mapIt->first,
                                 {(i < nRusAssigned ? *ruSetIt++ : *central26TonesRusIt++),
                                  mapIt->second.mcs,
                                  mapIt->second.nss});
        candidateIt++;
    }

    // remove candidates that will not be served
    m_candidates.erase(candidateIt, m_candidates.end());
}

void
RrMultiUserScheduler::UpdateCredits(std::list<MasterInfo>& staList,
                                    Time txDuration,
                                    const WifiTxVector& txVector)
{
    NS_LOG_FUNCTION(this << txDuration.As(Time::US) << txVector);

    // find how many RUs have been allocated for each RU type
    std::map<HeRu::RuType, std::size_t> ruMap;
    for (const auto& userInfo : txVector.GetHeMuUserInfoMap())
    {
        ruMap.insert({userInfo.second.ru.GetRuType(), 0}).first->second++;
    }

    // The amount of credits received by each station equals the TX duration (in
    // microseconds) divided by the number of stations.
    double creditsPerSta = txDuration.ToDouble(Time::US) / staList.size();
    // Transmitting stations have to pay a number of credits equal to the TX duration
    // (in microseconds) times the allocated bandwidth share.
    double debitsPerMhz =
        txDuration.ToDouble(Time::US) /
        std::accumulate(ruMap.begin(), ruMap.end(), 0, [](uint16_t sum, auto pair) {
            return sum + pair.second * HeRu::GetBandwidth(pair.first);
        });

    // assign credits to all stations
    for (auto& sta : staList)
    {
        sta.credits += creditsPerSta;
        sta.credits = std::min(sta.credits, m_maxCredits.ToDouble(Time::US));
    }

    // subtract debits to the selected stations
    for (auto& candidate : m_candidates)
    {
        auto mapIt = txVector.GetHeMuUserInfoMap().find(candidate.first->aid);
        NS_ASSERT(mapIt != txVector.GetHeMuUserInfoMap().end());

        candidate.first->credits -= debitsPerMhz * HeRu::GetBandwidth(mapIt->second.ru.GetRuType());
    }

    // sort the list in decreasing order of credits
    staList.sort([](const MasterInfo& a, const MasterInfo& b) { return a.credits > b.credits; });
}

MultiUserScheduler::DlMuInfo
RrMultiUserScheduler::ComputeDlMuInfo()
{
    NS_LOG_FUNCTION(this);

    if (m_candidates.empty())
    {
        return DlMuInfo();
    }

    DlMuInfo dlMuInfo;
    std::swap(dlMuInfo.txParams.m_txVector, m_txParams.m_txVector);
    FinalizeTxVector(dlMuInfo.txParams.m_txVector);

    m_txParams.Clear();
    Ptr<WifiMpdu> mpdu;

    // Compute the TX params (again) by using the stored MPDUs and the final TXVECTOR
    Time actualAvailableTime = (m_initialFrame ? Time::Min() : m_availableTime);

    for (const auto& candidate : m_candidates)
    {
        mpdu = candidate.second;
        NS_ASSERT(mpdu);

        bool ret [[maybe_unused]] =
            GetHeFem(m_linkId)->TryAddMpdu(mpdu, dlMuInfo.txParams, actualAvailableTime);
        NS_ASSERT_MSG(ret,
                      "Weird that an MPDU does not meet constraints when "
                      "transmitted over a larger RU");
    }

    // We have to complete the PSDUs to send
    Ptr<WifiMacQueue> queue;

    for (const auto& candidate : m_candidates)
    {
        // Let us try first A-MSDU aggregation if possible
        mpdu = candidate.second;
        NS_ASSERT(mpdu);
        uint8_t tid = mpdu->GetHeader().GetQosTid();
        NS_ASSERT_MSG(mpdu->GetOriginal()->GetHeader().GetAddr1() == candidate.first->address,
                      "RA of the stored MPDU must match the stored address");

        NS_ASSERT(mpdu->IsQueued());
        Ptr<WifiMpdu> item = mpdu;

        if (!mpdu->GetHeader().IsRetry())
        {
            // this MPDU must have been dequeued from the AC queue and we can try
            // A-MSDU aggregation
            item = GetHeFem(m_linkId)->GetMsduAggregator()->GetNextAmsdu(mpdu,
                                                                         dlMuInfo.txParams,
                                                                         m_availableTime);

            if (!item)
            {
                // A-MSDU aggregation failed or disabled
                item = mpdu;
            }
            m_apMac->GetQosTxop(QosUtilsMapTidToAc(tid))->AssignSequenceNumber(item);
        }

        // Now, let's try A-MPDU aggregation if possible
        std::vector<Ptr<WifiMpdu>> mpduList =
            GetHeFem(m_linkId)->GetMpduAggregator()->GetNextAmpdu(item,
                                                                  dlMuInfo.txParams,
                                                                  m_availableTime);

        if (mpduList.size() > 1)
        {
            // A-MPDU aggregation succeeded, update psduMap
            dlMuInfo.psduMap[candidate.first->aid] = Create<WifiPsdu>(std::move(mpduList));
        }
        else
        {
            dlMuInfo.psduMap[candidate.first->aid] = Create<WifiPsdu>(item, true);
        }
    }

    AcIndex primaryAc = m_edca->GetAccessCategory();
    UpdateCredits(m_staListDl[primaryAc],
                  dlMuInfo.txParams.m_txDuration,
                  dlMuInfo.txParams.m_txVector);

    NS_LOG_DEBUG("Next station to serve has AID=" << m_staListDl[primaryAc].front().aid);

    return dlMuInfo;
}

MultiUserScheduler::UlMuInfo
RrMultiUserScheduler::ComputeUlMuInfo()
{
    return UlMuInfo{m_trigger, m_triggerMacHdr, std::move(m_txParams)};
}

/*
*************************************
Attempt to add Channel Sounding from ns3.37
Private Functions for RR Multi User Scheduler
*************************************
*/

MultiUserScheduler::TxFormat
RrMultiUserScheduler::TryChannelSounding(void)
{
    NS_LOG_FUNCTION(this);

    m_candidates.clear();
    m_candidatesCs.clear();

    Ptr<HeConfiguration> heConfiguration = m_apMac->GetHeConfiguration();
    NS_ASSERT(heConfiguration != 0);

    // Set the number of rows in a compressed beamforming feedback matrix
    uint8_t nr = m_apMac->GetWifiPhy()->GetNumberOfAntennas();

    //  NDPA header
    CtrlNdpaHeader ndpaCtrlHeader;
    Ptr<Packet> packetNdpa = Create<Packet>();

    WifiMacHeader hdrNdpa(WIFI_MAC_CTL_NDPA);
    Mac48Address receiver = Mac48Address::GetBroadcast();
    hdrNdpa.SetAddr1(receiver);
    hdrNdpa.SetAddr2(m_apMac->GetAddress());
    hdrNdpa.SetDsNotTo();
    hdrNdpa.SetDsNotFrom();

    // NDP header
    Ptr<Packet> packetNdp = Create<Packet>();
    Ptr<WifiMpdu> mpduNdp;
    WifiMacHeader hdrNdp = hdrNdpa;
    hdrNdp.SetType(WIFI_MAC_DATA_NULL);
    mpduNdp = Create<WifiMpdu>(packetNdp, hdrNdp);

    // BFRP trigger header
    Ptr<Packet> packetTf;
    Ptr<WifiMpdu> mpduTf;
    WifiMacHeader hdrBfTrigger = hdrNdpa;
    hdrBfTrigger.SetType(WIFI_MAC_CTL_TRIGGER);

    // Tx Vectors -- NDPA, BFRP trigger
    WifiTxParameters txParamsCtrlFrame, txParamsNdpa, txParamsSendTf;
    txParamsCtrlFrame.m_txVector =
        m_apMac->GetWifiRemoteStationManager(m_linkId)->GetRtsTxVector(receiver);
    txParamsCtrlFrame.m_txVector.SetBssColor(heConfiguration->GetBssColor());
    txParamsCtrlFrame.m_acknowledgment = std::unique_ptr<WifiAcknowledgment>(new WifiNoAck());
    txParamsCtrlFrame.m_protection = std::unique_ptr<WifiProtection>(new WifiNoProtection());

    txParamsNdpa = txParamsCtrlFrame;
    txParamsSendTf = txParamsCtrlFrame;

    // Tx Vectors -- NDP
    WifiTxParameters txParamsNdp;
    WifiMode modeNdp("HeMcs0");
    txParamsNdp.m_txVector.SetMode(modeNdp);
    txParamsNdp.m_txVector.SetNTx(m_apMac->GetWifiPhy()->GetNumberOfAntennas());
    txParamsNdp.m_txVector.SetNss(nr);
    txParamsNdp.m_txVector.SetPreambleType(WIFI_PREAMBLE_HE_SU);
    txParamsNdp.m_txVector.SetChannelWidth(m_allowedWidth);
    txParamsNdp.m_txVector.SetBssColor(heConfiguration->GetBssColor());
    txParamsNdp.m_txVector.SetGuardInterval(800);
    txParamsNdp.m_acknowledgment = std::unique_ptr<WifiAcknowledgment>(new WifiNoAck());
    txParamsNdp.m_protection = std::unique_ptr<WifiProtection>(new WifiNoProtection());

    AcIndex primaryAc = m_edca->GetAccessCategory();

    if (m_staListDl[primaryAc].empty())
    {
        return TxFormat::NO_TX;
    }

    std::size_t maxCount = m_staListDl[primaryAc].size();

    uint8_t currTid = wifiAcList.at(primaryAc).GetHighTid();
    Ptr<const WifiMpdu> mpdu = m_edca->PeekNextMpdu(SINGLE_LINK_OP_ID);
    if (mpdu != nullptr && mpdu->GetHeader().IsQosData())
    {
        currTid = mpdu->GetHeader().GetQosTid();
    }

    // determine the list of TIDs to check
    std::vector<uint8_t> tids;

    if (m_enableTxopSharing)
    {
        for (auto acIt = wifiAcList.find(primaryAc); acIt != wifiAcList.end(); acIt++)
        {
            uint8_t firstTid = (acIt->first == primaryAc ? currTid : acIt->second.GetHighTid());
            tids.push_back(firstTid);
            tids.push_back(acIt->second.GetOtherTid(firstTid));
        }
    }
    else
    {
        tids.push_back(currTid);
    }

    Time actualAvailableTime = (m_initialFrame ? Time::Min() : m_availableTime);

    auto staIt = m_staListDl[primaryAc].begin();

    std::list<Mac48Address> staMacAddrList;

    // check NDP duration
    if (!GetHeFem(m_linkId)->TryAddMpdu(mpduNdp, txParamsNdp, actualAvailableTime))
    {
        NS_LOG_DEBUG("Remaining TXOP duration is not enough for NDP in channel sounding");
        return NO_TX;
    }

    NS_LOG_DEBUG("NDP duration:" << txParamsNdp.m_txDuration.GetMicroSeconds());

    actualAvailableTime =
        actualAvailableTime - txParamsNdp.m_txDuration - m_apMac->GetWifiPhy()->GetSifs();
    if (actualAvailableTime.IsNegative())
    {
        NS_LOG_DEBUG("Remaining TXOP duration is not enough for channel sounding");
        return NO_TX;
    }

    while (staIt != m_staListDl[primaryAc].end() && m_candidatesCs.size() < maxCount)
    {
        NS_LOG_DEBUG("Next candidate STA (MAC=" << staIt->address << ", AID=" << staIt->aid << ")");

        // check if the AP has at least one frame to be sent to the current station
        for (uint8_t tid : tids)
        {
            AcIndex ac = QosUtilsMapTidToAc(tid);
            NS_ASSERT(ac >= primaryAc);
        //    std::cout << "Checking STA " << staIt->address 
        //   << " BA established: " 
        //   << m_apMac->GetBaAgreementEstablishedAsOriginator(staIt->address, tid).has_value()
        //   << std::endl;
            if (m_apMac->GetBaAgreementEstablishedAsOriginator(staIt->address, tid))
            {
                Ptr<WifiMpdu> mpdu =
                    m_apMac->GetQosTxop(ac)->PeekNextMpdu(m_linkId, tid, staIt->address);
                if (mpdu)
                {
                    // Create NDPA
                    CtrlNdpaHeader ndpaCtrlHeaderCopy = ndpaCtrlHeader;
                    ndpaCtrlHeaderCopy.AddStaInfoField();

                    Ptr<Packet> packetNdpaCopy = Create<Packet>();
                    packetNdpaCopy->AddHeader(ndpaCtrlHeaderCopy);

                    Ptr<WifiMpdu> mpduNdpaCopy = Create<WifiMpdu>(packetNdpaCopy, hdrNdpa);

                    txParamsNdpa = txParamsCtrlFrame;

                    if (!GetHeFem(m_linkId)->TryAddMpdu(mpduNdpaCopy,
                                                        txParamsNdpa,
                                                        actualAvailableTime))
                    {
                        if (m_candidatesCs.size())
                        {
                            break;
                        }
                        else
                        {
                            return NO_TX;
                        }
                    }

                    actualAvailableTime = actualAvailableTime - txParamsNdpa.m_txDuration -
                                          m_apMac->GetWifiPhy()->GetSifs();
                    if (actualAvailableTime.IsNegative())
                    {
                        if (m_candidatesCs.size())
                        {
                            break;
                        }
                        else
                        {
                            return NO_TX;
                        }
                    }

                    // Create BFRP trigger
                    WifiTxVector txVector;
                    CtrlTriggerHeader bfTfCtrlHeader;
                    if (m_candidatesCs.size() > 0)
                    {
                        txVector.SetChannelWidth(m_allowedWidth);
                        txVector.SetPreambleType(WIFI_PREAMBLE_HE_TB);
                        txVector.SetGuardInterval(
                            heConfiguration->GetGuardInterval().GetNanoSeconds());

                        std::size_t nRusAssigned = m_candidatesCs.size() + 1;
                        std::size_t nCentral26TonesRus;
                        HeRu::RuType ruType = HeRu::GetEqualSizedRusForStations(m_allowedWidth,
                                                                                nRusAssigned,
                                                                                nCentral26TonesRus,
                                                                                true);

                        if (!m_useCentral26TonesRus || m_candidatesCs.size() + 1 == nRusAssigned)
                        {
                            nCentral26TonesRus = 0;
                        }
                        else
                        {
                            nCentral26TonesRus = std::min(m_candidatesCs.size() + 1 - nRusAssigned,
                                                          nCentral26TonesRus);
                            NS_LOG_DEBUG(nCentral26TonesRus
                                         << " stations are being assigned a 26-tones RU");
                        }

                        auto ruSet = HeRu::GetRusOfType(m_allowedWidth, ruType);
                        auto ruSetIt = ruSet.begin();
                        auto central26TonesRus = HeRu::GetCentral26TonesRus(m_allowedWidth, ruType);
                        auto central26TonesRusIt = central26TonesRus.begin();

                        WifiMacHeader hdr;
                        hdr.SetType(WIFI_MAC_QOSDATA);
                        hdr.SetAddr2(m_apMac->GetAddress());
                        hdr.SetDsNotTo();
                        hdr.SetDsNotFrom();

                        auto candidateIt = m_candidatesCs.begin();

                        WifiMode csMode;
                        for (std::size_t i = 0; i < nRusAssigned + nCentral26TonesRus - 1; i++)
                        {
                            hdr.SetAddr1(candidateIt->first->address);
                            csMode = GetHeFem(m_linkId)->GetCsMode() == "0"
                                         ? GetWifiRemoteStationManager(m_linkId)
                                               ->GetDataTxVector(hdr, m_allowedWidth)
                                               .GetMode()
                                         : WifiMode(GetHeFem(m_linkId)->GetCsMode());
                            txVector.SetHeMuUserInfo(
                                candidateIt->first->aid,
                                {(i < nRusAssigned ? *ruSetIt++ : *central26TonesRusIt++),
                                 csMode.GetMcsValue(),
                                 1});
                            candidateIt++;
                        }
                        hdr.SetAddr1(staIt->address);
                        csMode = GetHeFem(m_linkId)->GetCsMode() == "0"
                                     ? GetWifiRemoteStationManager(m_linkId)
                                           ->GetDataTxVector(hdr, m_allowedWidth)
                                           .GetMode()
                                     : WifiMode(GetHeFem(m_linkId)->GetCsMode());
                        txVector.SetHeMuUserInfo(
                            staIt->aid,
                            {(nCentral26TonesRus == 0 ? *ruSetIt++ : *central26TonesRusIt++),
                             csMode.GetMcsValue(),
                             1});
                        bfTfCtrlHeader =
                            CtrlTriggerHeader(TriggerFrameType::SENSING_TRIGGER, txVector);
                        bfTfCtrlHeader.SetSensingField(true);
                        bfTfCtrlHeader.SetSensingSubtype(SensingTriggerSubtype::SENSING_REPORTING);
                        // std::cout << "TryChannelSounding: selected stations for BFRP Trigger Frame: " << std::endl;
                        mpduTf = GetTriggerFrame(bfTfCtrlHeader, m_linkId);
                        txParamsSendTf = txParamsCtrlFrame;
                        if (!GetHeFem(m_linkId)->TryAddMpdu(mpduTf,
                                                            txParamsSendTf,
                                                            actualAvailableTime))
                        {
                            break;
                        }

                        actualAvailableTime = actualAvailableTime - txParamsSendTf.m_txDuration -
                                              m_apMac->GetWifiPhy()->GetSifs();
                        if (actualAvailableTime.IsNegative())
                        {
                            break;
                        }
                    }

                    // Beamforming report duration
                    HeMimoControlHeader::CsType type;
                    Time maxBfDuration = Time(0);
                    if (m_candidatesCs.size() == 0)
                    {
                        type = HeMimoControlHeader::SU;
                        // std::cout << "STA  is the only candidate for channel sounding, using SU feedback" << std::endl;
                        WifiMacHeader hdr(WIFI_MAC_QOSDATA);
                        hdr.SetAddr1(m_apMac->GetAddress());
                        hdr.SetAddr2(staIt->address);
                        txVector = m_apMac->GetWifiRemoteStationManager(m_linkId)->GetDataTxVector(
                            hdr,
                            m_allowedWidth);
                    }
                    else
                    {
                        type = HeMimoControlHeader::MU;
                    }

                    uint8_t ng, codeBookSize;
                    uint16_t numBytes;
                    auto candidateIt = m_candidatesCs.begin();
                    while (candidateIt != m_candidatesCs.end())
                    {
                        ng = m_apMac->GetWifiRemoteStationManager(m_linkId)
                                 ->GetStationHeCapabilities(candidateIt->first->address)
                                 ->GetNgforMuFeedback();
                        codeBookSize = (m_apMac->GetWifiRemoteStationManager(m_linkId)
                                            ->GetStationHeCapabilities(candidateIt->first->address)
                                            ->GetCodebookSizeforMu() == "(9,7)");

                        uint8_t ncBf =
                            1 + m_apMac->GetWifiRemoteStationManager(m_linkId)
                                    ->GetStationHeCapabilities(candidateIt->first->address)
                                    ->GetMaxNc();
                        numBytes = ChannelSounding::GetBfReportLength(m_allowedWidth,
                                                                      ng,
                                                                      ncBf,
                                                                      nr,
                                                                      codeBookSize,
                                                                      type);
                        maxBfDuration =
                            Max(maxBfDuration,
                                WifiPhy::CalculateTxDuration(numBytes,
                                                             txVector,
                                                             m_apMac->GetWifiPhy()->GetPhyBand(),
                                                             candidateIt->first->aid));
                        candidateIt++;
                    }
                    if (type == HeMimoControlHeader::SU)
                    {
                        ng = m_apMac->GetWifiRemoteStationManager(m_linkId)
                                 ->GetStationHeCapabilities(staIt->address)
                                 ->GetNgforSuFeedback();
                        codeBookSize = (m_apMac->GetWifiRemoteStationManager(m_linkId)
                                            ->GetStationHeCapabilities(staIt->address)
                                            ->GetCodebookSizeforSu() == "(6,4)");
                    }
                    else
                    {
                        ng = m_apMac->GetWifiRemoteStationManager(m_linkId)
                                 ->GetStationHeCapabilities(staIt->address)
                                 ->GetNgforMuFeedback();
                        codeBookSize = (m_apMac->GetWifiRemoteStationManager(m_linkId)
                                            ->GetStationHeCapabilities(staIt->address)
                                            ->GetCodebookSizeforMu() == "(9,7)");
                    }

                    uint8_t ncBf = 1 + m_apMac->GetWifiRemoteStationManager(m_linkId)
                                           ->GetStationHeCapabilities(staIt->address)
                                           ->GetMaxNc();

                    numBytes = ChannelSounding::GetBfReportLength(m_allowedWidth,
                                                                  ng,
                                                                  ncBf,
                                                                  nr,
                                                                  codeBookSize,
                                                                  type);
                    maxBfDuration =
                        Max(maxBfDuration,
                            WifiPhy::CalculateTxDuration(numBytes,
                                                         txVector,
                                                         m_apMac->GetWifiPhy()->GetPhyBand(),
                                                         staIt->aid));

                    // Duration of DL MU MIMO transmission
                    if (maxBfDuration < actualAvailableTime)
                    {
                        m_candidatesCs.push_back({staIt, mpdu});
                        staMacAddrList.push_back(staIt->address);
                        ndpaCtrlHeader = ndpaCtrlHeaderCopy;
                        GetHeFem(m_linkId)->GetCsBeamformer()->SetTxParameters(txParamsNdpa,
                                                                               "NDPA");
                        if (m_candidatesCs.size() > 1)
                        {
                            GetHeFem(m_linkId)->GetCsBeamformer()->SetTxParameters(txParamsSendTf,
                                                                                   "Trigger");
                            uint16_t ulLength;
                            std::tie(ulLength, maxBfDuration) =
                                HePhy::ConvertHeTbPpduDurationToLSigLength(
                                    maxBfDuration,
                                    txVector,
                                    m_apMac->GetWifiPhy()->GetPhyBand());
                            bfTfCtrlHeader.SetUlLength(ulLength);
                            GetHeFem(m_linkId)->GetCsBeamformer()->SetBeamformerFrames(
                                GetTriggerFrame(bfTfCtrlHeader, m_linkId),
                                "Trigger");
                        }
                        break;
                    }
                }
                else
                {
                    NS_LOG_DEBUG("No frames to send to " << staIt->address << " with TID=" << +tid);
                }
            }
            else
            {
                NS_LOG_DEBUG(
                    "STA:"
                    << staIt->address << " with TID=" << +tid
                    << "No BA agreement is established with the receiver for the considered TID");
            }
        }

        // move to the next station in the list
        staIt++;
    }

    if (m_candidatesCs.empty())
    {
        return NO_TX;
    }
    else
    {
        GetHeFem(m_linkId)->GetCsBeamformer()->GenerateNdpaFrame(
            m_apMac->GetAddress(),
            staMacAddrList,
            m_allowedWidth,
            GetWifiRemoteStationManager(m_linkId));

        if (GetHeFem(m_linkId)->GetCsBeamformer()->GetNumCsStations() == 1)
        {
            receiver = *staMacAddrList.begin();
        }
        else
        {
            receiver = Mac48Address::GetBroadcast();
        }
        hdrNdp.SetAddr1(receiver);
        mpduNdp = Create<WifiMpdu>(packetNdp, hdrNdp);
        GetHeFem(m_linkId)->GetCsBeamformer()->SetBeamformerFrames(mpduNdp, "NDP");
        GetHeFem(m_linkId)->GetCsBeamformer()->SetTxParameters(txParamsNdp, "NDP");

        NS_LOG_DEBUG("Number of stations scheduled in channel sounding"
                     << GetHeFem(m_linkId)->GetCsBeamformer()->GetNumCsStations());

        return TxFormat::CS_TX;
    }
}

bool
RrMultiUserScheduler::IsChannelSoundingEnabled()
{
    return !m_csInterval.IsZero() && m_apMac->GetChannelSoundingSupported();
}

/*
 *************************************
 Attempt to modify MU-MIMO from ns3.40
 Private Functions and Attributes for RR Multi User Scheduler
 *************************************
*/

bool
RrMultiUserScheduler::IsPCFEnabled()
{
    return m_apMac->GetPcfSupported();
}

MultiUserScheduler::TxFormat
RrMultiUserScheduler::TryPollingPhase11bf()
{
    NS_LOG_FUNCTION(this);
    bool m_bPollDlMu = false;
    bool m_bPollUlMu = false;
    Ptr<HeConfiguration> heConfiguration = m_apMac->GetHeConfiguration();
    NS_ASSERT(heConfiguration != 0);

    // Clear candidates for NDPA Sounding Phase
    m_candidatesCs.clear();
    m_candidatesCsSU.clear();
    
    /*
    ********************************************************************
    Trying downlink MU transmission for polling phase
    ********************************************************************
    */

    //  Polling Frame header --------------------------------------------------
    WifiMacHeader pollingFrame;
    Ptr<Packet> packetPollingFrame = Create<Packet>();
    pollingFrame.SetType(WIFI_MAC_QOSDATA_CFPOLL);
    pollingFrame.SetNoRetry();
    pollingFrame.SetAddr2(m_apMac->GetAddress());
    pollingFrame.SetDsNotTo();
    pollingFrame.SetDsNotFrom();
    pollingFrame.SetQosAckPolicy(ns3::WifiMacHeader::QosAckPolicy::NO_ACK);

    // Tx Vectors & Params -- Polling Frame --------------------------------------------------
    WifiTxParameters txParamsPollingFrame;
    WifiMode modetxParamsPollingFrame("HeMcs0");
    txParamsPollingFrame.m_txVector.SetMode(modetxParamsPollingFrame);
    txParamsPollingFrame.m_txVector.SetNTx(m_apMac->GetWifiPhy()->GetNumberOfAntennas());
    txParamsPollingFrame.m_txVector.SetNss(m_apMac->GetWifiPhy()->GetNumberOfAntennas());
    txParamsPollingFrame.m_txVector.SetPreambleType(WIFI_PREAMBLE_HE_SU);
    txParamsPollingFrame.m_txVector.SetChannelWidth(m_allowedWidth);
    txParamsPollingFrame.m_txVector.SetBssColor(heConfiguration->GetBssColor());
    txParamsPollingFrame.m_txVector.SetGuardInterval(
        heConfiguration->GetGuardInterval().GetNanoSeconds());
    txParamsPollingFrame.m_acknowledgment = std::unique_ptr<WifiAcknowledgment>(new WifiNoAck());
    txParamsPollingFrame.m_protection = std::unique_ptr<WifiProtection>(new WifiNoProtection());

    // txParamsPollingFrame.m_acknowledgment->acknowledgmentTime = Seconds(0);
    // // Also pre-initialise protection time for the same reason:
    // txParamsPollingFrame.m_protection->protectionTime = Seconds(0);

    //  Check capability of DL MU  --------------------------------------------------
    AcIndex primaryAc = m_edca->GetAccessCategory();
    if (m_staListDl[primaryAc].empty())
    {
        SensingTimeout();
        return TxFormat::SU_TX;
    }

    // OFDMA variables
    std::size_t nCentral26TonesRus;
    HeRu::RuType ruType;
    std::size_t count;
    std::size_t maxCount;

    if (!m_useCentral26TonesRus)
    {
        nCentral26TonesRus = 0;
    }
    // count = std::min(static_cast<std::size_t>(m_nStations), m_staListDl[primaryAc].size());
    // maxCount = std::min(static_cast<std::size_t>(m_nStations), count + nCentral26TonesRus);
    maxCount = m_staListDl[primaryAc].size();
    ruType = HeRu::GetEqualSizedRusForStations(m_allowedWidth, maxCount, nCentral26TonesRus, true);
    NS_ASSERT(maxCount >= 1);

    uint8_t currTid = wifiAcList.at(primaryAc).GetHighTid();

    // determine the list of TIDs to check
    std::vector<uint8_t> tids;

    if (m_enableTxopSharing)
    {
        for (auto acIt = wifiAcList.find(primaryAc); acIt != wifiAcList.end(); acIt++)
        {
            uint8_t firstTid = (acIt->first == primaryAc ? currTid : acIt->second.GetHighTid());
            tids.push_back(firstTid);
            tids.push_back(acIt->second.GetOtherTid(firstTid));
        }
    }
    else
    {
        tids.push_back(currTid);
    }

    std::list<Mac48Address> staMacAddrList;

    // The TXOP limit can be exceeded by the TXOP holder if it does not transmit more
    // than one Data or Management frame in the TXOP and the frame is not in an A-MPDU
    // consisting of more than one MPDU (Sec. 10.22.2.8 of 802.11-2016).
    // For the moment, we are considering just one MPDU per receiver.
    // Time actualAvailableTime = (m_initialFrame ? Time::Min() : m_availableTime);
    if (!(std::min(m_availableTime, m_availableTime)).IsStrictlyPositive())
        m_availableTime = Seconds(0);
    Time actualAvailableTime = (m_initialFrame ? Time::Min() : m_availableTime);
    // iterate over the associated stations until an enough number of stations is identified
    auto staIt = m_staListDl[primaryAc].begin();
    m_candidatesPoll.clear();

    // OFDMA variables
    std::vector<uint8_t> ruAllocations;
    uint16_t numRuAllocs = txParamsPollingFrame.m_txVector.GetChannelWidth() / 20;
    ruAllocations.resize(numRuAllocs);
    NS_ASSERT((m_candidatesPoll.size() % numRuAllocs) == 0);

    while (staIt != m_staListDl[primaryAc].end() && m_candidatesPoll.size() < maxCount)
    {
        NS_LOG_DEBUG("Next candidate STA (MAC=" << staIt->address << ", AID=" << staIt->aid << ")");
        if (txParamsPollingFrame.m_txVector.GetPreambleType() == WIFI_PREAMBLE_EHT_MU &&
            !m_apMac->GetEhtSupported(staIt->address))
        {
            NS_LOG_DEBUG("Skipping non-EHT STA because this DL MU PPDU is sent to EHT STAs only");
            staIt++;
            continue;
        }

        // attempt to add channel sounding from ns3.37 : modification in TrySendingDlMuPpdu for
        // current resource allocation setup
        HeRu::RuType currRuType;

        // OFDMA
        // currRuType = (m_candidates.size() < count ? ruType : HeRu::RU_26_TONE);
        currRuType = (m_candidatesPoll.size() < maxCount ? ruType : HeRu::RU_26_TONE);
        pollingFrame.SetAddr1(staIt->address);
        Ptr<WifiMpdu> mpdu = Create<WifiMpdu>(packetPollingFrame, pollingFrame);
        // check if the AP has at least one frame to be sent to the current station
        if (mpdu)
        {
            mpdu = GetHeFem(m_linkId)->CreateAliasIfNeeded(mpdu);
            // Use a temporary TX vector including only the STA-ID of the
            // candidate station to check if the MPDU meets the size and time limits.
            // An RU of the computed size is tentatively assigned to the candidate
            // station, so that the TX duration can be correctly computed.
            WifiTxVector suTxVector =
                GetWifiRemoteStationManager(m_linkId)->GetDataTxVector(mpdu->GetHeader(),
                                                                       m_allowedWidth);

            WifiTxVector txVectorCopy = txParamsPollingFrame.m_txVector;
            txParamsPollingFrame.m_txVector.SetPreambleType(WIFI_PREAMBLE_HE_MU);
            txParamsPollingFrame.m_txVector.SetHeMuUserInfo(
                staIt->aid,
                {{currRuType, 1, true},
                 suTxVector.GetMode().GetMcsValue(),
                 m_apMac->GetWifiPhy()->GetNumberOfAntennas()});

            txParamsPollingFrame.m_acknowledgment =
                std::unique_ptr<WifiAcknowledgment>(new WifiNoAck());
            txParamsPollingFrame.m_protection =
                std::unique_ptr<WifiProtection>(new WifiNoProtection());
            mpdu->GetHeader().SetQosTid( wifiAcList.find(m_edca->GetAccessCategory())->first);
            if (!GetHeFem(m_linkId)->TryAddMpdu(mpdu, txParamsPollingFrame, actualAvailableTime))
            {
                NS_LOG_DEBUG("Adding the peeked frame violates the time constraints");
                txParamsPollingFrame.m_txVector = txVectorCopy;
            }
            else
            {
                // the frame meets the constraints
                NS_LOG_DEBUG("Adding candidate STA (MAC=" << staIt->address
                                                          << ", AID=" << staIt->aid);
                m_candidatesPoll.emplace_back(staIt, mpdu);
            }
        }
        else
        {
            NS_LOG_DEBUG("No frames to send to " << staIt->address);
        }

        // move to the next station in the list
        staIt++;
    }

    actualAvailableTime =
        actualAvailableTime - txParamsPollingFrame.m_txDuration - m_apMac->GetWifiPhy()->GetSifs();

    if (m_candidatesPoll.empty())
    {
        SensingTimeout();
        return NO_TX;
    }
    // else if (m_candidatesPoll.size() == 1)
    // {
    //     // return SU_TX;
    //     m_txParams = txParamsPollingFrame;
    //     m_txParams.m_txVector = txParamsPollingFrame.m_txVector;
    //     m_bPollDlMu = true;
    //     return TxFormat::BF_POLL_DL_TX;
    // }
    else
    {
        m_txParams = txParamsPollingFrame;
        m_txParams.m_txVector = txParamsPollingFrame.m_txVector;
        m_bPollDlMu = true;
    }

    /*
    ********************************************************************
    Trying uplink MU transmission for polling phase
    ********************************************************************
    */
    // Tx Vectors & Params -- BSRP trigger --------------------------------------------------
    WifiTxVector txVector = GetTxVectorForUlMu([this](const MasterInfo& info) {
        const auto& staList = m_apMac->GetStaList(m_linkId);
        return staList.find(info.aid) != staList.cend();
    });

    // BSRP Trigger header --------------------------------------------------
    Ptr<Packet> packetTf;
    Ptr<WifiMpdu> mpduTf;
    CtrlTriggerHeader pollTfCtrlHeader;
    WifiMacHeader hdrPollTrigger(WIFI_MAC_CTL_TRIGGER);
    hdrPollTrigger.SetAddr1(Mac48Address::GetBroadcast());
    hdrPollTrigger.SetAddr2(GetHeFem(0U)->GetAddress());
    hdrPollTrigger.SetDsNotTo();
    hdrPollTrigger.SetDsNotFrom();
    hdrPollTrigger.SetQosAckPolicy(ns3::WifiMacHeader::QosAckPolicy::NO_ACK);
    // only consider stations that have setup the current link

    // CTS-to-Self header --------------------------------------------------
    WifiMacHeader ctsToSelf;
    Ptr<Packet> packetCtsToSelf = Create<Packet>();
    ctsToSelf.SetType(WIFI_MAC_CTL_CTS);
    ctsToSelf.SetDsNotFrom();
    ctsToSelf.SetDsNotTo();
    ctsToSelf.SetNoMoreFragments();
    ctsToSelf.SetNoRetry();
    ctsToSelf.SetAddr1(m_apMac->GetAddress());

    WifiTxVector ctsTxVector =
        m_apMac->GetWifiRemoteStationManager(0U)->GetRtsTxVector(m_apMac->GetAddress());

    WifiTxParameters txParamsCtrlFrame, txParamsSendTf;
    txParamsSendTf.m_txVector = txVector;
    txParamsSendTf.m_txVector.SetBssColor(heConfiguration->GetBssColor());
    txParamsSendTf.m_acknowledgment = std::unique_ptr<WifiAcknowledgment>(new WifiNoAck());
    txParamsSendTf.m_protection = std::unique_ptr<WifiProtection>(new WifiNoProtection());

    auto pollingStaIt = m_candidatesPoll.begin();
    if (m_candidatesPoll.size() > 0)
    {
        while (pollingStaIt != m_candidatesPoll.end())
        {
            txParamsSendTf.m_txVector.SetChannelWidth(m_allowedWidth);
            txParamsSendTf.m_txVector.SetPreambleType(WIFI_PREAMBLE_HE_TB);
            txParamsSendTf.m_txVector.SetGuardInterval(
                heConfiguration->GetGuardInterval().GetNanoSeconds());

            std::size_t nRusAssigned = m_candidatesPoll.size() + 1;
            std::size_t nCentral26TonesRus;
            HeRu::RuType ruType = HeRu::GetEqualSizedRusForStations(m_allowedWidth,
                                                                    nRusAssigned,
                                                                    nCentral26TonesRus,
                                                                    true);

            if (!m_useCentral26TonesRus || m_candidatesPoll.size() + 1 == nRusAssigned)
            {
                nCentral26TonesRus = 0;
            }
            else
            {
                nCentral26TonesRus =
                    std::min(m_candidatesPoll.size() + 1 - nRusAssigned, nCentral26TonesRus);
                NS_LOG_DEBUG(nCentral26TonesRus << " stations are being assigned a 26-tones RU");
            }

            auto ruSet = HeRu::GetRusOfType(m_allowedWidth, ruType);
            auto ruSetIt = ruSet.begin();
            auto central26TonesRus = HeRu::GetCentral26TonesRus(m_allowedWidth, ruType);
            auto central26TonesRusIt = central26TonesRus.begin();

            WifiMode pollUlMode;
            for (std::size_t i = 0; i > nRusAssigned + nCentral26TonesRus - 1; i++)
            {
                pollingFrame.SetAddr1(pollingStaIt->first->address);
                txParamsSendTf.m_txVector.SetHeMuUserInfo(
                    pollingStaIt->first->aid,
                    // {(i < nRusAssigned ? *ruSetIt++ : *central26TonesRusIt++),
                    //  ctsTxVector.GetMode().GetMcsValue(),
                    //  1});
                    {(i < nRusAssigned ? *ruSetIt++ : *central26TonesRusIt++),
                     modetxParamsPollingFrame.GetMcsValue(),
                     ctsTxVector.GetNss()});
                pollingStaIt++;
            }

            pollingFrame.SetAddr1(pollingStaIt->first->address);
            txParamsSendTf.m_txVector.SetHeMuUserInfo(
                pollingStaIt->first->aid,
                // {(nCentral26TonesRus == 0 ? *ruSetIt++ : *central26TonesRusIt++),
                //  ctsTxVector.GetMode().GetMcsValue(),
                //  1});
                //  (
                // pollingStaIt->first->aid,
                {(nCentral26TonesRus == 0 ? *ruSetIt++ : *central26TonesRusIt++),
                 modetxParamsPollingFrame.GetMcsValue(),
                 ctsTxVector.GetNss()});

            // Get the trigger header frame (802.11bf Sensing TF, Subtype=0: Sensing Polling)
            pollTfCtrlHeader =
                CtrlTriggerHeader(TriggerFrameType::SENSING_TRIGGER, txParamsSendTf.m_txVector);
            pollTfCtrlHeader.SetSensingField(true);
            pollTfCtrlHeader.SetSensingSubtype(SensingTriggerSubtype::SENSING_POLLING);
            mpduTf = GetTriggerFrame(pollTfCtrlHeader, m_linkId);

            pollingStaIt++;
        }

        uint8_t maxNTids = 0;

        uint32_t headerSize = ctsToSelf.GetSerializedSize();
        uint32_t maxSize = 0;

        for (uint8_t i = 0; i < maxNTids; i++)
        {
            maxSize =
                MpduAggregator::GetSizeIfAggregated(headerSize + WIFI_MAC_FCS_LENGTH, maxSize);
        }

        actualAvailableTime = Seconds(0);
        for (const auto& userInfo : pollTfCtrlHeader)
        {
            Time duration = WifiPhy::CalculateTxDuration(maxSize,
                                                         txParamsSendTf.m_txVector,
                                                         m_apMac->GetWifiPhy()->GetPhyBand(),
                                                         userInfo.GetAid12());
            actualAvailableTime = Max(actualAvailableTime, duration);
        }

        // actualAvailableTime =
        //     actualAvailableTime - txParamsSendTf.m_txDuration - m_apMac->GetWifiPhy()->GetSifs();

        m_triggerUlPoll = pollTfCtrlHeader;
        m_macHdrTriggerUlPoll = hdrPollTrigger;
        m_txParamsTriggerUlPoll = txParamsSendTf;

        // 802.11bf Sensing TF, Subtype=0: Sensing Polling
        m_triggerUlPoll =
            CtrlTriggerHeader(TriggerFrameType::SENSING_TRIGGER, txParamsSendTf.m_txVector);
        m_triggerUlPoll.SetSensingField(true);
        m_triggerUlPoll.SetSensingSubtype(SensingTriggerSubtype::SENSING_POLLING);
        txVector.SetGuardInterval(m_trigger.GetGuardInterval());

        auto item = GetTriggerFrame(m_triggerUlPoll, m_linkId);
        m_macHdrTriggerUlPoll = item->GetHeader();

        // m_txParamsTriggerUlPoll.Clear();
        // set the TXVECTOR used to send the Trigger Frame
        m_txParamsTriggerUlPoll.m_txVector =
            m_apMac->GetWifiRemoteStationManager(m_linkId)->GetRtsTxVector(
                m_triggerMacHdr.GetAddr1());

        u_int16_t ulLength;
        std::tie(ulLength, actualAvailableTime) = HePhy::ConvertHeTbPpduDurationToLSigLength(
            actualAvailableTime,
            m_triggerUlPoll.GetHeTbTxVector(m_triggerUlPoll.begin()->GetAid12()),
            m_apMac->GetWifiPhy()->GetPhyBand());
        m_triggerUlPoll.SetUlLength(ulLength);

        m_bPollUlMu = true;
    }

    if (IsChannelSoundingEnabled() &&
        GetHeFem(m_linkId)->GetCsBeamformer()->CheckChannelSounding(m_csInterval))
    {
        m_csStart = true;
        GetHeFem(m_linkId)->GetCsBeamformer()->ClearAllInfo();

        // if (txformatCs == CS_TX)
        // {
        //     GetHeFem(m_linkId)->GetCsBeamformer()->SetLastCsTime(Simulator::Now());
        //     return txformatCs;
        // }
    }
    // m_csStart = true;
    // GetHeFem(m_linkId)->GetCsBeamformer()->ClearAllInfo();

    if (m_bPollDlMu && m_bPollUlMu)
    {
        return TxFormat::BF_POLL_DL_TX;
    }
    else
    {
        // std::cout << "No station can be scheduled in the polling phase, performing sensing timeout" << std::endl;
        SensingTimeout();
        return NO_TX;
    }
}

MultiUserScheduler::PollingMuInfo
RrMultiUserScheduler::ComputePollMuInfo()
{
    NS_LOG_FUNCTION(this);
    if (m_candidatesPoll.empty())
    {
        return PollingMuInfo();
    }

    m_candidates = m_candidatesPoll;
    PollingMuInfo pollMuInfo;

    Ptr<WifiMpdu> mpdu;
    pollMuInfo.txParams.m_acknowledgment = std::unique_ptr<WifiAcknowledgment>(new WifiNoAck());
    std::swap(pollMuInfo.txParams.m_txVector, m_txParams.m_txVector);
    std::swap(pollMuInfo.txParamsTriggerUlPoll, m_txParamsTriggerUlPoll);
    std::swap(pollMuInfo.macHdrTriggerUlPoll, m_macHdrTriggerUlPoll);
    std::swap(pollMuInfo.triggerUlPoll, m_triggerUlPoll);
    // FinalizeTxVector(pollMuInfo.txParams.m_txVector);
    NS_ASSERT(pollMuInfo.txParams.m_txVector.GetHeMuUserInfoMap().size() ==
              m_candidatesPoll.size());

    // compute how many stations can be granted an RU and the RU size
    std::size_t nRusAssigned = m_candidatesPoll.size();
    std::size_t nCentral26TonesRus;
    HeRu::RuType ruType =
        HeRu::GetEqualSizedRusForStations(m_allowedWidth, nRusAssigned, nCentral26TonesRus, true);

    NS_LOG_DEBUG(nRusAssigned << " stations are being assigned a " << ruType << " RU");

    if (!m_useCentral26TonesRus || m_candidatesPoll.size() == nRusAssigned)
    {
        nCentral26TonesRus = 0;
    }
    else
    {
        nCentral26TonesRus = std::min(m_candidatesPoll.size() - nRusAssigned, nCentral26TonesRus);
        NS_LOG_DEBUG(nCentral26TonesRus << " stations are being assigned a 26-tones RU");
    }

    // re-allocate RUs based on the actual number of candidate stations
    WifiTxVector::HeMuUserInfoMap heMuUserInfoMap;
    std::swap(heMuUserInfoMap, pollMuInfo.txParams.m_txVector.GetHeMuUserInfoMap());

    auto candidateIt = m_candidatesPoll.begin(); // iterator over the list of candidate receivers
    auto ruSet = HeRu::GetRusOfType(m_allowedWidth, ruType);
    auto ruSetIt = ruSet.begin();
    auto central26TonesRus = HeRu::GetCentral26TonesRus(m_allowedWidth, ruType);
    auto central26TonesRusIt = central26TonesRus.begin();
    for (std::size_t i = 0; i < nRusAssigned + nCentral26TonesRus; i++)
    {
        NS_ASSERT(candidateIt != m_candidates.end());
        auto mapIt = heMuUserInfoMap.find(candidateIt->first->aid);
        NS_ASSERT(mapIt != heMuUserInfoMap.end());

        pollMuInfo.txParams.m_txVector.SetHeMuUserInfo(
            mapIt->first,
            {(i < nRusAssigned ? *ruSetIt++ : *central26TonesRusIt++),
             mapIt->second.mcs,
             mapIt->second.nss});
        candidateIt++;
    }

    // FinalizeTxVector(pollMuInfo.txParamsTriggerUlPoll.m_txVector);

    // Compute the TX params (again) by using the stored MPDUs and the final TXVECTOR
    Time actualAvailableTime = (m_initialFrame ? Time::Min() : m_availableTime);
    m_txParams.Clear();
    for (const auto& candidate : m_candidatesPoll)
    {
        mpdu = candidate.second;
        NS_ASSERT(mpdu);
        NS_ASSERT_MSG(mpdu->GetOriginal()->GetHeader().GetAddr1() == candidate.first->address,
                      "RA of the stored MPDU must match the stored address");
        pollMuInfo.psduMap[candidate.first->aid] = Create<WifiPsdu>(candidate.second, true);
    }

    // We have to complete the PSDUs to send
    Ptr<WifiMacQueue> queue;

    AcIndex primaryAc = m_edca->GetAccessCategory();
    // UpdateCredits(m_staListDl[primaryAc],
    //               pollMuInfo.txParams.m_txDuration,
    //               pollMuInfo.txParams.m_txVector);

    NS_LOG_DEBUG("Next station to serve has AID=" << m_staListDl[primaryAc].front().aid);
    return pollMuInfo;
}

void
RrMultiUserScheduler::CheckRespondedPollingStation(Mac48Address address)
{
    WifiMacHeader hdrNdpa(WIFI_MAC_CTL_NDPA);
    hdrNdpa.SetAddr1(address);
    hdrNdpa.SetAddr2(m_apMac->GetAddress());
    hdrNdpa.SetDsNotTo();
    hdrNdpa.SetDsNotFrom();
    ns3::Ptr<ns3::WifiMpdu> mpduNdpa = Create<WifiMpdu>(Create<Packet>(), hdrNdpa);
    auto staIt = m_candidatesPoll.begin();
    while (staIt != m_candidatesPoll.end())
    {
        if (staIt->first->address == address)
        {
            m_candidatesCs.emplace_back(staIt->first, mpduNdpa);
            m_candidatesCsSU.emplace_back(staIt->first, mpduNdpa);
        }
        staIt++;
    }

    m_currentCsSUCandidate = m_candidatesCsSU.begin();
    if (m_soundingType == SoundingType::SU_only && m_candidatesCs.size() > 1)
    {
        DoSUNDPASoundingStation();
    }
}

MultiUserScheduler::TxFormat
RrMultiUserScheduler::TryNDPASoundingPhase11bf(void)
{
    NS_LOG_FUNCTION(this);
    m_candidatesReport.clear();
    Ptr<HeConfiguration> heConfiguration = m_apMac->GetHeConfiguration();
    NS_ASSERT(heConfiguration != 0);

    if (m_soundingType == SU_and_MU)
    {
        Ptr<UniformRandomVariable> random_seed = CreateObject<UniformRandomVariable>();
        int random = random_seed->GetInteger(1, m_candidatesCs.size());
        auto m_candidateCs_copy = m_candidatesCs;
        std::vector<int> random_index_vec;
        m_candidatesCs.clear();
        if (random > 1)
        {
            auto staIt = m_candidateCs_copy.begin();
            for (int i = 0; i < random; i++)
            {
                m_candidatesCs.emplace_back(staIt->first, staIt->second);
                staIt++;
            }
        }
        else
        {
            auto staIt = m_candidateCs_copy.begin();
            int random_index = random_seed->GetInteger(1, m_candidateCs_copy.size());
            for (int i = 0; i < random_index - 1; i++)
            {
                staIt++;
            }
            m_candidatesCs.emplace_back(staIt->first, staIt->second);
        }
        if (m_candidatesCs.size() == 0)
        {
            SensingTimeout();
            return NO_TX;
        }
    }

    // Set the number of rows in a compressed beamforming feedback matrix
    uint8_t nr = m_apMac->GetWifiPhy()->GetNumberOfAntennas();
    //  NDPA header
    CtrlNdpaHeader ndpaCtrlHeader;
    Ptr<Packet> packetNdpa = Create<Packet>();

    WifiMacHeader hdrNdpa(WIFI_MAC_CTL_NDPA);
    Mac48Address receiver = Mac48Address::GetBroadcast();
    hdrNdpa.SetAddr1(receiver);
    hdrNdpa.SetAddr2(m_apMac->GetAddress());
    hdrNdpa.SetDsNotTo();
    hdrNdpa.SetDsNotFrom();

    // NDP header
    Ptr<Packet> packetNdp = Create<Packet>();
    Ptr<WifiMpdu> mpduNdp;
    WifiMacHeader hdrNdp = hdrNdpa;
    hdrNdp.SetType(WIFI_MAC_DATA_NULL);
    mpduNdp = Create<WifiMpdu>(packetNdp, hdrNdp);

    // BFRP trigger header
    Ptr<Packet> packetTf;
    Ptr<WifiMpdu> mpduTf;
    WifiMacHeader hdrBfTrigger = hdrNdpa;
    hdrBfTrigger.SetType(WIFI_MAC_CTL_TRIGGER);

    // Tx Vectors -- NDPA, BFRP trigger
    WifiTxParameters txParamsCtrlFrame, txParamsNdpa, txParamsSendTf;
    txParamsCtrlFrame.m_txVector =
        m_apMac->GetWifiRemoteStationManager(m_linkId)->GetRtsTxVector(receiver);
    txParamsCtrlFrame.m_txVector.SetBssColor(heConfiguration->GetBssColor());
    txParamsCtrlFrame.m_acknowledgment = std::unique_ptr<WifiAcknowledgment>(new WifiNoAck());
    txParamsCtrlFrame.m_protection = std::unique_ptr<WifiProtection>(new WifiNoProtection());

    txParamsNdpa = txParamsCtrlFrame;
    txParamsSendTf = txParamsCtrlFrame;

    // Tx Vectors -- NDP
    WifiTxParameters txParamsNdp;
    WifiMode modeNdp("HeMcs0");
    txParamsNdp.m_txVector.SetMode(modeNdp);
    txParamsNdp.m_txVector.SetNTx(m_apMac->GetWifiPhy()->GetNumberOfAntennas());
    txParamsNdp.m_txVector.SetNss(nr);
    txParamsNdp.m_txVector.SetPreambleType(WIFI_PREAMBLE_HE_SU);
    txParamsNdp.m_txVector.SetChannelWidth(m_allowedWidth);
    txParamsNdp.m_txVector.SetBssColor(heConfiguration->GetBssColor());
    txParamsNdp.m_txVector.SetGuardInterval(800);
    txParamsNdp.m_acknowledgment = std::unique_ptr<WifiAcknowledgment>(new WifiNoAck());
    txParamsNdp.m_protection = std::unique_ptr<WifiProtection>(new WifiNoProtection());

    // determine the list of TIDs to check
    std::vector<uint8_t> tids;

    Time actualAvailableTime = (m_initialFrame ? Time::Min() : m_availableTime);

    auto staIt = m_candidatesCs.begin();

    std::list<Mac48Address> staMacAddrList;
    // check NDP duration
    if (!GetHeFem(m_linkId)->TryAddMpdu(mpduNdp, txParamsNdp, actualAvailableTime))
    {
        NS_LOG_DEBUG("Remaining TXOP duration is not enough for NDP in channel sounding");
        SensingTimeout();
        return NO_TX;
    }

    NS_LOG_DEBUG("NDP duration:" << txParamsNdp.m_txDuration.GetMicroSeconds());

    actualAvailableTime =
        actualAvailableTime - txParamsNdp.m_txDuration - m_apMac->GetWifiPhy()->GetSifs();
    if (actualAvailableTime.IsNegative())
    {
        NS_LOG_DEBUG("Remaining TXOP duration is not enough for channel sounding");

        SensingTimeout();
        return NO_TX;
    }

    while (staIt != m_candidatesCs.end())
    {
        NS_LOG_DEBUG("Next candidate STA (MAC=" << staIt->first->address
                                                << ", AID=" << staIt->first->aid << ")");

        // check if the AP has at least one frame to be sent to the current station

        // Create NDPA
        CtrlNdpaHeader ndpaCtrlHeaderCopy = ndpaCtrlHeader;
        ndpaCtrlHeaderCopy.AddStaInfoField();

        Ptr<Packet> packetNdpaCopy = Create<Packet>();
        packetNdpaCopy->AddHeader(ndpaCtrlHeaderCopy);

        Ptr<WifiMpdu> mpduNdpaCopy = Create<WifiMpdu>(packetNdpaCopy, hdrNdpa);

        txParamsNdpa = txParamsCtrlFrame;

        if (!GetHeFem(m_linkId)->TryAddMpdu(mpduNdpaCopy, txParamsNdpa, actualAvailableTime))
        {
            if (m_candidatesReport.size())
            {
                break;
            }
            else
            {
                SensingTimeout();
                return NO_TX;
            }
        }
        actualAvailableTime =
            actualAvailableTime - txParamsNdpa.m_txDuration - m_apMac->GetWifiPhy()->GetSifs();
        if (actualAvailableTime.IsNegative())
        {
            if (m_candidatesReport.size())
            {
                break;
            }
            else
            {
                SensingTimeout();
                return NO_TX;
            }
        }

        // Create BFRP trigger
        WifiTxVector txVector;
        CtrlTriggerHeader bfTfCtrlHeader;
        if (m_candidatesReport.size() > 0)
        {
            txVector.SetChannelWidth(m_allowedWidth);
            txVector.SetPreambleType(WIFI_PREAMBLE_HE_TB);
            txVector.SetGuardInterval(heConfiguration->GetGuardInterval().GetNanoSeconds());

            std::size_t nRusAssigned = m_candidatesReport.size() + 1;
            std::size_t nCentral26TonesRus;
            HeRu::RuType ruType = HeRu::GetEqualSizedRusForStations(m_allowedWidth,
                                                                    nRusAssigned,
                                                                    nCentral26TonesRus,
                                                                    true);

            if (!m_useCentral26TonesRus || m_candidatesReport.size() + 1 == nRusAssigned)
            {
                nCentral26TonesRus = 0;
            }
            else
            {
                nCentral26TonesRus =
                    std::min(m_candidatesReport.size() + 1 - nRusAssigned, nCentral26TonesRus);
                NS_LOG_DEBUG(nCentral26TonesRus << " stations are being assigned a 26-tones RU");
            }

            auto ruSet = HeRu::GetRusOfType(m_allowedWidth, ruType);
            auto ruSetIt = ruSet.begin();
            auto central26TonesRus = HeRu::GetCentral26TonesRus(m_allowedWidth, ruType);
            auto central26TonesRusIt = central26TonesRus.begin();

            WifiMacHeader hdr;
            hdr.SetType(WIFI_MAC_QOSDATA);
            hdr.SetAddr2(m_apMac->GetAddress());
            hdr.SetDsNotTo();
            hdr.SetDsNotFrom();

            auto candidateIt = m_candidatesReport.begin();

            WifiMode csMode;
            for (std::size_t i = 0; i < nRusAssigned + nCentral26TonesRus - 1; i++)
            {
                hdr.SetAddr1(candidateIt->first->address);
                csMode = GetHeFem(m_linkId)->GetCsMode() == "0"
                             ? GetWifiRemoteStationManager(m_linkId)
                                   ->GetDataTxVector(hdr, m_allowedWidth)
                                   .GetMode()
                             : WifiMode(GetHeFem(m_linkId)->GetCsMode());
                txVector.SetHeMuUserInfo(candidateIt->first->aid,
                                         {(i < nRusAssigned ? *ruSetIt++ : *central26TonesRusIt++),
                                          csMode.GetMcsValue(),
                                          1});
                candidateIt++;
            }
            hdr.SetAddr1(staIt->first->address);
            csMode = GetHeFem(m_linkId)->GetCsMode() == "0"
                         ? GetWifiRemoteStationManager(m_linkId)
                               ->GetDataTxVector(hdr, m_allowedWidth)
                               .GetMode()
                         : WifiMode(GetHeFem(m_linkId)->GetCsMode());
            txVector.SetHeMuUserInfo(
                staIt->first->aid,
                {(nCentral26TonesRus == 0 ? *ruSetIt++ : *central26TonesRusIt++),
                 csMode.GetMcsValue(),
                 1});
            // 802.11bf Sensing TF, Subtype=3: Sensing Reporting
            bfTfCtrlHeader = CtrlTriggerHeader(TriggerFrameType::SENSING_TRIGGER, txVector);
            bfTfCtrlHeader.SetSensingField(true);
            bfTfCtrlHeader.SetSensingSubtype(SensingTriggerSubtype::SENSING_REPORTING);
            mpduTf = GetTriggerFrame(bfTfCtrlHeader, m_linkId);
            txParamsSendTf = txParamsCtrlFrame;
            if (!GetHeFem(m_linkId)->TryAddMpdu(mpduTf, txParamsSendTf, actualAvailableTime))
            {
                break;
            }

            actualAvailableTime = actualAvailableTime - txParamsSendTf.m_txDuration -
                                  m_apMac->GetWifiPhy()->GetSifs();
            if (actualAvailableTime.IsNegative())
            {
                break;
            }
            txVector.SetMode(csMode);
        }
        else
        {
            txVector.SetMode(modeNdp);
        }
        // Beamforming report duration
        HeMimoControlHeader::CsType type;
        Time maxBfDuration = Time(0);

        if (m_candidatesCs.size() == 0 || m_candidatesCs.size() == 1)
        {
            type = HeMimoControlHeader::SU;
            // std::cout << "SU Sounding Station:" << std::endl;
            WifiMacHeader hdr(WIFI_MAC_QOSDATA);
            hdr.SetAddr1(m_apMac->GetAddress());
            hdr.SetAddr2(staIt->first->address);
            txVector =
                m_apMac->GetWifiRemoteStationManager(m_linkId)->GetDataTxVector(hdr,
                                                                                m_allowedWidth);
        }
        else
        {
            type = HeMimoControlHeader::MU;
        }

        uint8_t ng, codeBookSize;
        uint16_t numBytes;

        auto candidateIt = m_candidatesReport.begin();
        while (candidateIt != m_candidatesReport.end())
        {
            ng = m_apMac->GetWifiRemoteStationManager(m_linkId)
                     ->GetStationHeCapabilities(candidateIt->first->address)
                     ->GetNgforMuFeedback();
            codeBookSize = (m_apMac->GetWifiRemoteStationManager(m_linkId)
                                ->GetStationHeCapabilities(candidateIt->first->address)
                                ->GetCodebookSizeforMu() == "(9,7)");

            uint8_t ncBf = 1 + m_apMac->GetWifiRemoteStationManager(m_linkId)
                                   ->GetStationHeCapabilities(candidateIt->first->address)
                                   ->GetMaxNc();
            numBytes = ChannelSounding::GetBfReportLength(m_allowedWidth,
                                                          ng,
                                                          ncBf,
                                                          nr,
                                                          codeBookSize,
                                                          type);
            maxBfDuration = Max(maxBfDuration,
                                WifiPhy::CalculateTxDuration(numBytes,
                                                             txVector,
                                                             m_apMac->GetWifiPhy()->GetPhyBand(),
                                                             candidateIt->first->aid));
            candidateIt++;
        }

        if (type == HeMimoControlHeader::SU)
        {
            ng = m_apMac->GetWifiRemoteStationManager(m_linkId)
                     ->GetStationHeCapabilities(staIt->first->address)
                     ->GetNgforSuFeedback();
            codeBookSize = (m_apMac->GetWifiRemoteStationManager(m_linkId)
                                ->GetStationHeCapabilities(staIt->first->address)
                                ->GetCodebookSizeforSu() == "(6,4)");
        }
        else
        {
            ng = m_apMac->GetWifiRemoteStationManager(m_linkId)
                     ->GetStationHeCapabilities(staIt->first->address)
                     ->GetNgforMuFeedback();
            codeBookSize = (m_apMac->GetWifiRemoteStationManager(m_linkId)
                                ->GetStationHeCapabilities(staIt->first->address)
                                ->GetCodebookSizeforMu() == "(9,7)");
        }

        uint8_t ncBf = 1 + m_apMac->GetWifiRemoteStationManager(m_linkId)
                               ->GetStationHeCapabilities(staIt->first->address)
                               ->GetMaxNc();

        numBytes =
            ChannelSounding::GetBfReportLength(m_allowedWidth, ng, ncBf, nr, codeBookSize, type);
        maxBfDuration = Max(maxBfDuration,
                            WifiPhy::CalculateTxDuration(numBytes,
                                                         txVector,
                                                         m_apMac->GetWifiPhy()->GetPhyBand(),
                                                         staIt->first->aid));
        
        if (maxBfDuration < actualAvailableTime)
        {
            m_candidatesReport.emplace_back(staIt->first, mpduNdpaCopy);
            staMacAddrList.push_back(staIt->first->address);
            ndpaCtrlHeader = ndpaCtrlHeaderCopy;
            GetHeFem(m_linkId)->GetCsBeamformer()->SetTxParameters(txParamsNdpa, "NDPA");
            if (m_candidatesReport.size() > 1)
            {
                GetHeFem(m_linkId)->GetCsBeamformer()->SetTxParameters(txParamsSendTf, "Trigger");
                uint16_t ulLength;
                std::tie(ulLength, maxBfDuration) =
                    HePhy::ConvertHeTbPpduDurationToLSigLength(maxBfDuration,
                                                               txVector,
                                                               m_apMac->GetWifiPhy()->GetPhyBand());
                bfTfCtrlHeader.SetUlLength(ulLength);
                GetHeFem(m_linkId)->GetCsBeamformer()->SetBeamformerFrames(
                    GetTriggerFrame(bfTfCtrlHeader, m_linkId),
                    "Trigger");
            }
            // Note : here the problem for MU Case
            // break;
        }

        // move to the next station in the list
        staIt++;
    }

    if (m_candidatesReport.empty())
    {
        SensingTimeout();
        return NO_TX;
    }
    else
    {
        GetHeFem(m_linkId)->GetCsBeamformer()->GenerateNdpaFrame(
            m_apMac->GetAddress(),
            staMacAddrList,
            m_allowedWidth,
            GetWifiRemoteStationManager(m_linkId));

        if (GetHeFem(m_linkId)->GetCsBeamformer()->GetNumCsStations() == 1)
        {
            receiver = *staMacAddrList.begin();
        }
        else
        {
            receiver = Mac48Address::GetBroadcast();
        }
        hdrNdp.SetAddr1(receiver);
        mpduNdp = Create<WifiMpdu>(packetNdp, hdrNdp);
        GetHeFem(m_linkId)->GetCsBeamformer()->SetBeamformerFrames(mpduNdp, "NDP");
        GetHeFem(m_linkId)->GetCsBeamformer()->SetTxParameters(txParamsNdp, "NDP");

        NS_LOG_DEBUG("Number of stations scheduled in channel sounding"
                     << GetHeFem(m_linkId)->GetCsBeamformer()->GetNumCsStations());

        if (m_soundingType == SU_only)
        {
            return TxFormat::BF_NDPA_SOUNDING_TX_SU;
        }
        else
        {
            return TxFormat::BF_NDPA_SOUNDING_TX;
        }
    }
}

bool
RrMultiUserScheduler::DoSUNDPASoundingStation()
{
    m_candidatesCs.clear();
    if (m_currentCsSUCandidate == m_candidatesCsSU.begin())
    {
        m_candidatesCs.emplace_back(m_currentCsSUCandidate->first,
                                    m_currentCsSUCandidate->second);
        m_currentCsSUCandidate++;
        m_nextSUSounding = true;
        return true;
    }
    else if (m_currentCsSUCandidate != m_candidatesCsSU.end())
    {
        m_candidatesCs.emplace_back(m_currentCsSUCandidate->first,
                                    m_currentCsSUCandidate->second);
        m_currentCsSUCandidate++;
        m_nextSUSounding = true;
        return true;
    }
    else
    {
        m_candidatesCs.emplace_back(m_currentCsSUCandidate->first,
                                    m_currentCsSUCandidate->second);
        m_nextSUSounding = false;
        return false;
    }
}

ns3::MultiUserScheduler::SoundingType
RrMultiUserScheduler::GetSoundingType()
{
    return m_soundingType;
}

size_t
RrMultiUserScheduler::GetPollingCandidatesSize()
{
    return m_candidatesPoll.size();
}

bool
RrMultiUserScheduler::HasMoreSensingPhases()
{
    if (!m_apMac)
    {
        return false;
    }
    const auto& seq = m_apMac->GetSensingPhaseSequence();
    if (seq.empty() || m_sensingPhaseIdx >= seq.size())
    {
        return false;
    }
    // If we are still mid-SU-NDPA sweep, we owe another NDPA at the same index.
    if (seq[m_sensingPhaseIdx] == SensingPhase::NDPA_SOUNDING && m_nextSUSounding)
    {
        return true;
    }
    for (size_t i = m_sensingPhaseIdx + 1; i < seq.size(); ++i)
    {
        if (seq[i] != SensingPhase::REPORT)
        {
            return true;
        }
    }
    return false;
}

MultiUserScheduler::TxFormat
RrMultiUserScheduler::TryTFSoundingPhase11bf()
{
    NS_LOG_FUNCTION(this);

    // Sensing candidates were already populated by TryPollingPhase11bf into m_candidatesCs.
    // If empty (e.g. polling cleared them), fall back to m_candidatesPoll so we always
    // have someone to sound.
    if (m_candidatesCs.empty() && !m_candidatesPoll.empty())
    {
        for (auto& c : m_candidatesPoll)
        {
            m_candidatesCs.emplace_back(c.first, c.second);
        }
    }

    if (m_candidatesCs.empty())
    {
        NS_LOG_DEBUG("TryTFSoundingPhase11bf: no sensing candidates, aborting");
        SensingTimeout();
        return NO_TX;
    }

    Ptr<HeConfiguration> heConfiguration = m_apMac->GetHeConfiguration();
    NS_ASSERT(heConfiguration != nullptr);

    // -----------------------------------------------------------------
    // Build the TX vector for the solicited HE TB PPDU (UL from STAs)
    // -----------------------------------------------------------------
    WifiTxVector txVector;
    txVector.SetChannelWidth(m_allowedWidth);
    txVector.SetPreambleType(WIFI_PREAMBLE_HE_TB);
    txVector.SetGuardInterval(heConfiguration->GetGuardInterval().GetNanoSeconds());
    txVector.SetBssColor(heConfiguration->GetBssColor());

    std::size_t nCentral26TonesRus;
    std::size_t nCandidates = m_candidatesCs.size();
    HeRu::RuType ruType = HeRu::GetEqualSizedRusForStations(m_allowedWidth,
                                                             nCandidates,
                                                             nCentral26TonesRus,
                                                             true);
    auto ruSet   = HeRu::GetRusOfType(m_allowedWidth, ruType);
    auto ruSetIt = ruSet.begin();

    for (auto& candidate : m_candidatesCs)
    {
        NS_ABORT_MSG_IF(ruSetIt == ruSet.end(), "Not enough RUs for all sensing candidates");

        WifiMacHeader hdr(WIFI_MAC_QOSDATA);
        hdr.SetAddr1(m_apMac->GetAddress());
        hdr.SetAddr2(candidate.first->address);
        WifiMode csMode =
            GetHeFem(m_linkId)->GetCsMode() == "0"
                ? GetWifiRemoteStationManager(m_linkId)->GetDataTxVector(hdr, m_allowedWidth).GetMode()
                : WifiMode(GetHeFem(m_linkId)->GetCsMode());

        txVector.SetHeMuUserInfo(candidate.first->aid,
                                 {*ruSetIt++, csMode.GetMcsValue(), 1});
    }

    // -----------------------------------------------------------------
    // Build the Sensing Trigger Frame header (Type=8, Sensing=1, Subtype=SR2SI=1)
    // -----------------------------------------------------------------
    CtrlTriggerHeader sensingTf(TriggerFrameType::SENSING_TRIGGER, txVector);
    sensingTf.SetSensingField(true);
    sensingTf.SetSensingSubtype(SensingTriggerSubtype::SR2SI_SOUNDING);

    // UL length: use a NULL data frame (NDP) size as the expected response
    WifiTxParameters txParamsTf;
    txParamsTf.m_txVector =
        m_apMac->GetWifiRemoteStationManager(m_linkId)->GetRtsTxVector(Mac48Address::GetBroadcast());
    txParamsTf.m_txVector.SetBssColor(heConfiguration->GetBssColor());
    txParamsTf.m_acknowledgment = std::unique_ptr<WifiAcknowledgment>(new WifiNoAck());
    txParamsTf.m_protection     = std::unique_ptr<WifiProtection>(new WifiNoProtection());

    // Estimate NDP duration for the UL length field
    WifiTxParameters txParamsNdp;
    WifiMode modeNdp("HeMcs0");
    txParamsNdp.m_txVector.SetMode(modeNdp);
    txParamsNdp.m_txVector.SetNTx(m_apMac->GetWifiPhy()->GetNumberOfAntennas());
    txParamsNdp.m_txVector.SetNss(m_apMac->GetWifiPhy()->GetNumberOfAntennas());
    txParamsNdp.m_txVector.SetPreambleType(WIFI_PREAMBLE_HE_SU);
    txParamsNdp.m_txVector.SetChannelWidth(m_allowedWidth);
    txParamsNdp.m_txVector.SetBssColor(heConfiguration->GetBssColor());
    txParamsNdp.m_txVector.SetGuardInterval(800);

    Ptr<Packet> packetNdp = Create<Packet>();
    WifiMacHeader hdrNdp(WIFI_MAC_DATA_NULL);
    hdrNdp.SetAddr1(Mac48Address::GetBroadcast());
    hdrNdp.SetAddr2(m_apMac->GetAddress());
    hdrNdp.SetDsNotTo();
    hdrNdp.SetDsNotFrom();
    Ptr<WifiMpdu> mpduNdp = Create<WifiMpdu>(packetNdp, hdrNdp);

    Time actualAvailableTime = (m_initialFrame ? Time::Min() : m_availableTime);
    if (!GetHeFem(m_linkId)->TryAddMpdu(mpduNdp, txParamsNdp, actualAvailableTime))
    {
        NS_LOG_DEBUG("TryTFSoundingPhase11bf: not enough time for NDP response");
        SensingTimeout();
        return NO_TX;
    }

    uint16_t ulLength;
    Time ndpDuration;
    std::tie(ulLength, ndpDuration) =
        HePhy::ConvertHeTbPpduDurationToLSigLength(txParamsNdp.m_txDuration,
                                                    txVector,
                                                    m_apMac->GetWifiPhy()->GetPhyBand());
    sensingTf.SetUlLength(ulLength);

    // -----------------------------------------------------------------
    // Store the Sensing TF in the beamformer and register sensing candidates
    // -----------------------------------------------------------------
    Ptr<WifiMpdu> mpduTf = GetTriggerFrame(sensingTf, m_linkId);

    // Re-use the "Trigger" slot in BeamformerFrameInfo to hold the Sensing TF
    GetHeFem(m_linkId)->GetCsBeamformer()->SetBeamformerFrames(mpduTf, "SensingTF");
    GetHeFem(m_linkId)->GetCsBeamformer()->SetTxParameters(txParamsTf, "Trigger");

    // Register which STAs are expected to send an NDP
    std::list<Mac48Address> staMacAddrList;
    for (auto& candidate : m_candidatesCs)
    {
        staMacAddrList.push_back(candidate.first->address);
    }
    GetHeFem(m_linkId)->GetCsBeamformer()->GenerateNdpaFrame(
        m_apMac->GetAddress(),
        staMacAddrList,
        m_allowedWidth,
        GetWifiRemoteStationManager(m_linkId));

    NS_LOG_DEBUG("TryTFSoundingPhase11bf: Sensing TF built for "
                 << m_candidatesCs.size() << " station(s)");

    return TxFormat::BF_TF_SOUNDING_TX;
}

} // namespace ns3
