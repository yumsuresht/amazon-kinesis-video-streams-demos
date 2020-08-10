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
        STATUS shutdown();

        PPeer pPeer;
        std::string id;

        STATUS handleSignalingMsg(PReceivedSignalingMessage);
        STATUS writeFrame(PRtcRtpTransceiver, PFrame);
        const std::vector<PRtcRtpReceiver>& getTransceivers();

      private:
        PRtcPeerConnection pPeerConnection;
        std::vector<PRtcRtpTransceiver> transceivers;
        std::mutex mutex;
        std::condition_variable cvar;
        std::atomic<bool> iceGatheringDone;
        std::atomic<bool> terminated;
        std::atomic<bool> receivedOffer;
        // std::atomic<bool> receivedAnswer;
        std::atomic<UINT8> workCounter;
    };
    typedef Connection* PConnection;
    struct Callbacks {
        std::function<STATUS(std::shared_ptr<Connection>)> onNewConnection;
    };

    Peer(const Canary::PConfig, const Callbacks&);
    STATUS init();
    VOID deinit();
    STATUS connect(UINT64 duration);

  private:
    const Canary::PConfig pConfig;
    const Callbacks callbacks;
    PAwsCredentialProvider pAwsCredentialProvider;
    SIGNALING_CLIENT_HANDLE pSignalingClientHandle;
    std::vector<std::shared_ptr<Connection>> connections;

    STATUS connectSignaling();
    STATUS connectICE();
    STATUS shutdown();
};

} // namespace Canary
