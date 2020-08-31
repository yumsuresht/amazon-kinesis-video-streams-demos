#pragma once
namespace Canary {

class Peer;
typedef Peer* PPeer;

typedef struct {
    UINT64 prevNumberOfPacketsSent;
    UINT64 prevNumberOfPacketsReceived;
    UINT64 prevNumberOfBytesSent;
    UINT64 prevNumberOfBytesReceived;
    UINT64 prevFramesDiscardedOnSend;
    DOUBLE framesPercentageDiscarded;
    DOUBLE averageFramesSentPerSecond;
    UINT64 prevTs;
    UINT64 prevVideoFramesGenerated;
    std::atomic<UINT64> videoFramesGenerated;
    UINT64 prevFramesSent;
} OutgoingRTPMetricsContext;
typedef OutgoingRTPMetricsContext* POutgoingRTPMetricsContext;

class Peer {
  public:
    struct Callbacks {
        std::function<VOID()> onDisconnected;
        std::function<STATUS(PPeer)> onNewConnection;
    };

    Peer(const Canary::PConfig, const Callbacks&);
    ~Peer();
    STATUS init();
    STATUS shutdown();
    STATUS connect();
    STATUS addTransceiver(RtcMediaStreamTrack&);
    STATUS addSupportedCodec(RTC_CODEC);
    STATUS writeFrame(PFrame, MEDIA_STREAM_TRACK_KIND);

    // WebRTC Stats
    VOID setStatsType(RTC_STATS_TYPE);
    RTC_STATS_TYPE getStatsType();
    STATUS publishStatsForCanary();

  private:
    const Canary::PConfig pConfig;
    const Callbacks callbacks;
    PAwsCredentialProvider pAwsCredentialProvider;
    SIGNALING_CLIENT_HANDLE pSignalingClientHandle;
    std::recursive_mutex mutex;
    std::condition_variable_any cvar;
    std::atomic<BOOL> terminated;
    std::atomic<BOOL> iceGatheringDone;
    std::atomic<BOOL> receivedOffer;
    std::atomic<BOOL> receivedAnswer;
    std::atomic<BOOL> foundPeerId;
    std::atomic<BOOL> recorded;
    std::string peerId;
    RtcConfiguration rtcConfiguration;
    PRtcPeerConnection pPeerConnection;
    std::vector<PRtcRtpTransceiver> audioTransceivers;
    std::vector<PRtcRtpTransceiver> videoTransceivers;
    UINT64 numberOfFrameBytes;
    STATUS status;

    // metrics
    UINT64 signalingStartTime;
    UINT64 iceHolePunchingStartTime;
    RtcStats canaryMetrics;
    OutgoingRTPMetricsContext canaryOutgoingRTPMetricsContext;

    STATUS initSignaling();
    STATUS initRtcConfiguration();
    STATUS initPeerConnection();
    STATUS awaitIceGathering(PRtcSessionDescriptionInit);
    STATUS handleSignalingMsg(PReceivedSignalingMessage);
    STATUS send(PSignalingMessage);
    STATUS populateOutgoingRtpMetricsContext();
};

} // namespace Canary
