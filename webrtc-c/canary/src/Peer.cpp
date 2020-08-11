#include "Include_i.h"

namespace Canary {

Peer::Peer(const Canary::PConfig pConfig, const Callbacks& callbacks)
    : pConfig(pConfig), callbacks(callbacks), pAwsCredentialProvider(nullptr), terminated(FALSE)
{
}

Peer::~Peer()
{
    freeStaticCredentialProvider(&pAwsCredentialProvider);
}

STATUS Peer::init()
{
    STATUS retStatus = STATUS_SUCCESS;

    CHK_STATUS(createStaticCredentialProvider((PCHAR) pConfig->pAccessKey, 0, (PCHAR) pConfig->pSecretKey, 0, (PCHAR) pConfig->pSessionToken, 0,
                                              MAX_UINT64, &pAwsCredentialProvider));

CleanUp:

    return retStatus;
}

VOID Peer::shutdown()
{
    this->terminated = TRUE;
    CHK_LOG_ERR(freeSignalingClient(&this->pSignalingClientHandle));

    for (auto& connection : this->connections) {
        connection->shutdown();
    }

    CHK_LOG_ERR(freeStaticCredentialProvider(&this->pAwsCredentialProvider));
}

STATUS Peer::connect(UINT64 duration)
{
    STATUS retStatus = STATUS_SUCCESS;
    UNUSED_PARAM(duration);

    CHK_STATUS(connectSignaling());

CleanUp:

    return retStatus;
}

STATUS Peer::connectSignaling()
{
    STATUS retStatus = STATUS_SUCCESS;

    SignalingClientInfo clientInfo;
    ChannelInfo channelInfo;
    SignalingClientCallbacks clientCallbacks;

    MEMSET(&clientInfo, 0, SIZEOF(clientInfo));
    MEMSET(&channelInfo, 0, SIZEOF(channelInfo));
    MEMSET(&clientCallbacks, 0, SIZEOF(clientCallbacks));

    clientInfo.version = SIGNALING_CLIENT_INFO_CURRENT_VERSION;
    clientInfo.loggingLevel = pConfig->logLevel;
    STRCPY(clientInfo.clientId, pConfig->pClientId);

    channelInfo.version = CHANNEL_INFO_CURRENT_VERSION;
    channelInfo.pChannelName = (PCHAR) pConfig->pChannelName;
    channelInfo.pKmsKeyId = NULL;
    channelInfo.tagCount = 0;
    channelInfo.pTags = NULL;
    channelInfo.channelType = SIGNALING_CHANNEL_TYPE_SINGLE_MASTER;
    channelInfo.channelRoleType = pConfig->isMaster ? SIGNALING_CHANNEL_ROLE_TYPE_MASTER : SIGNALING_CHANNEL_ROLE_TYPE_VIEWER;
    channelInfo.cachingPolicy = SIGNALING_API_CALL_CACHE_TYPE_FILE;
    channelInfo.cachingPeriod = SIGNALING_API_CALL_CACHE_TTL_SENTINEL_VALUE;
    channelInfo.asyncIceServerConfig = TRUE;
    channelInfo.retry = TRUE;
    channelInfo.reconnect = TRUE;
    channelInfo.pCertPath = (PCHAR) pConfig->pCertPath;
    channelInfo.messageTtl = 0; // Default is 60 seconds

    clientCallbacks.customData = (UINT64) this;
    clientCallbacks.errorReportFn = [](UINT64 customData, STATUS status, PCHAR msg, UINT32 msgLen) -> STATUS {
        PPeer pPeer = (PPeer) customData;
        DLOGW("Signaling client generated an error 0x%08x - '%.*s'", status, msgLen, msg);

        UNUSED_PARAM(pPeer);
        // TODO: handle recreate signaling

        return STATUS_SUCCESS;
    };
    clientCallbacks.messageReceivedFn = [](UINT64 customData, PReceivedSignalingMessage pMsg) -> STATUS {
        STATUS retStatus = STATUS_SUCCESS;
        PPeer pPeer = (PPeer) customData;
        std::unique_lock<std::mutex> lock(pPeer->mutex);
        std::shared_ptr<Connection> pConnection;
        std::string msgClientId(pMsg->signalingMessage.peerClientId);

        auto it = std::find_if(pPeer->connections.begin(), pPeer->connections.end(),
                               [&msgClientId](const std::shared_ptr<Connection>& c) { return c->id == msgClientId; });

        if (it == pPeer->connections.end()) {
            pConnection = std::make_shared<Connection>(pPeer, msgClientId, std::bind(&Peer::checkTerminatedConnections, pPeer));
            CHK_STATUS(pConnection->init());
            CHK_STATUS(pPeer->callbacks.onNewConnection(pConnection));
            pPeer->connections.push_back(pConnection);
        } else {
            pConnection = *it;
        }

        DLOGD("Handling signaling message:\n%s", pMsg->signalingMessage.payload);
        lock.unlock();
        CHK_STATUS(pConnection->handleSignalingMsg(pMsg));

    CleanUp:

        return retStatus;
    };

    CHK_STATUS(createSignalingClientSync(&clientInfo, &channelInfo, &clientCallbacks, pAwsCredentialProvider, &pSignalingClientHandle));
    CHK_STATUS(signalingClientConnectSync(pSignalingClientHandle));

CleanUp:

    return retStatus;
}

VOID Peer::checkTerminatedConnections()
{
    // no need to remove connections since it's already terminated. The connections' shutdowns will be done by Peer::shutdown
    if (this->terminated) {
        return;
    }

    auto it = this->connections.begin();

    while (it != this->connections.end()) {
        auto& pConnection = *it;
        if (pConnection->isTerminated()) {
            this->updatingConnections = TRUE;
            while (this->connectionsReaderCount != 0) {
                // busy loop until all media thread stopped reading connections
                THREAD_SLEEP(5 * HUNDREDS_OF_NANOS_IN_A_MILLISECOND);
            }

            pConnection->shutdown();
            // no need to increment it since it's using the last element
            *it = std::move(this->connections.back());
            this->connections.pop_back();
            this->updatingConnections = FALSE;
        } else {
            it++;
        }
    }
}

VOID Peer::writeFrame(PFrame pFrame, MEDIA_STREAM_TRACK_KIND kind)
{
    if (!this->updatingConnections) {
        this->connectionsReaderCount++;
        for (auto& connection : this->connections) {
            for (auto& transceiver : connection->getTransceivers(kind)) {
                CHK_LOG_ERR(connection->writeFrame(transceiver, pFrame));
            }
        }
        this->connectionsReaderCount--;
    }
}

} // namespace Canary
