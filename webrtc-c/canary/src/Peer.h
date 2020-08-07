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
        PRtcPeerConnection pPeerConnection;

        STATUS handleSignalingMsg(PReceivedSignalingMessage);
    };
    typedef Connection* PConnection;

    const Canary::PConfig pConfig;
    PAwsCredentialProvider pAwsCredentialProvider;
    SIGNALING_CLIENT_HANDLE pSignalingClientHandle;
    std::vector<Connection> connections;

    STATUS connectSignaling();
    STATUS connectICE();
    STATUS sendVideoFrames(UINT64 duration);
    STATUS sendAudioFrames(UINT64 duration);
    STATUS shutdown();
};

} // namespace Canary
