#pragma once
namespace Canary {

class Peer;
typedef Peer* PPeer;

class Peer {
  public:
    class Connection {
      public:
        Connection(PPeer pPeer, std::string id);
        STATUS init();
        VOID shutdown();

        PPeer pPeer;
        std::string id;

        STATUS handleSignalingMsg(PReceivedSignalingMessage);
        STATUS addTransceiver(RtcMediaStreamTrack&);
        STATUS writeFrame(PRtcRtpTransceiver, PFrame);
        const std::vector<PRtcRtpTransceiver>& getTransceivers(MEDIA_STREAM_TRACK_KIND);
        STATUS addSupportedCodec(RTC_CODEC);

      private:
        PRtcPeerConnection pPeerConnection;
        std::vector<PRtcRtpTransceiver> audioTransceivers;
        std::vector<PRtcRtpTransceiver> videoTransceivers;
        std::mutex mutex;
        std::condition_variable cvar;
        std::atomic<bool> iceGatheringDone;
        std::atomic<bool> terminated;
        std::atomic<bool> receivedOffer;
        // std::atomic<bool> receivedAnswer;
    };
    typedef Connection* PConnection;
    struct Callbacks {
        std::function<STATUS(std::shared_ptr<Connection>)> onNewConnection;
    };

    Peer(const Canary::PConfig, const Callbacks&);
    ~Peer();
    STATUS init();
    VOID shutdown();
    STATUS connect(UINT64 duration);
    STATUS writeFrame(PFrame, MEDIA_STREAM_TRACK_KIND);

  private:
    const Canary::PConfig pConfig;
    const Callbacks callbacks;
    PAwsCredentialProvider pAwsCredentialProvider;
    SIGNALING_CLIENT_HANDLE pSignalingClientHandle;
    std::vector<std::shared_ptr<Connection>> connections;
    std::mutex mutex;

    STATUS connectSignaling();
    STATUS connectICE();
};

} // namespace Canary
