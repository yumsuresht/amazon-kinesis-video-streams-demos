#pragma once
namespace Canary {

class Peer;
typedef Peer* PPeer;

class Peer {
  public:
    Peer(const Canary::PConfig pConfig);
    STATUS init();
    VOID deinit();
    STATUS connect(UINT64 duration);

  private:
    class Connection {
      public:
        Connection(PPeer pPeer, std::string id);

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

    const Canary::PConfig pConfig;
    PAwsCredentialProvider pAwsCredentialProvider;
    SIGNALING_CLIENT_HANDLE pSignalingClientHandle;
    std::vector<std::shared_ptr<Connection>> connections;

    STATUS connectSignaling();
    STATUS connectICE();
    STATUS shutdown();
};

} // namespace Canary
