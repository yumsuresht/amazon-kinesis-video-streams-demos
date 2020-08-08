#include "Include_i.h"

namespace Canary {

Peer::Connection::Connection(PPeer pPeer, std::string id) : pPeer(pPeer), id(id), iceGatheringDone(FALSE), terminated(FALSE), receivedOffer(FALSE)
{
}

STATUS Peer::Connection::handleSignalingMsg(PReceivedSignalingMessage pMsg)
{
    auto awaitIceGathering = [this](PRtcSessionDescriptionInit pSDPInit) -> STATUS {
        STATUS retStatus = STATUS_SUCCESS;
        std::unique_lock<std::mutex> lock(this->mutex);
        this->cvar.wait(lock, [this]() { return this->terminated.load() || this->iceGatheringDone.load(); });
        CHK_WARN(this->terminated, STATUS_OPERATION_TIMED_OUT, "application terminated and candidate gathering still not done");

        CHK_STATUS(peerConnectionGetCurrentLocalDescription(this->pPeerConnection, pSDPInit));

    CleanUp:

        return retStatus;
    };

    auto handleOffer = [this, awaitIceGathering](SignalingMessage& msg) -> STATUS {
        STATUS retStatus = STATUS_SUCCESS;
        RtcSessionDescriptionInit offerSDPInit, answerSDPInit;
        NullableBool canTrickle;
        UINT32 buffLen;

        if (!pPeer->pConfig->isMaster) {
            DLOGW("Unexpected message SIGNALING_MESSAGE_TYPE_OFFER");
            CHK(FALSE, retStatus);
        }

        if (!receivedOffer.exchange(TRUE)) {
            DLOGW("Offer already received, ignore new offer from client id %s", msg.peerClientId);
            CHK(FALSE, retStatus);
        }

        MEMSET(&offerSDPInit, 0, SIZEOF(offerSDPInit));
        MEMSET(&answerSDPInit, 0, SIZEOF(answerSDPInit));

        CHK_STATUS(deserializeSessionDescriptionInit(msg.payload, msg.payloadLen, &offerSDPInit));
        CHK_STATUS(setRemoteDescription(this->pPeerConnection, &offerSDPInit));

        canTrickle = canTrickleIceCandidates(this->pPeerConnection);
        /* cannot be null after setRemoteDescription */
        CHECK(!NULLABLE_CHECK_EMPTY(canTrickle));

        CHK_STATUS(createAnswer(this->pPeerConnection, &answerSDPInit));
        CHK_STATUS(setLocalDescription(this->pPeerConnection, &answerSDPInit));

        if (!canTrickle.value) {
            CHK_STATUS(awaitIceGathering(&answerSDPInit));
        }

        CHK_STATUS(serializeSessionDescriptionInit(&answerSDPInit, NULL, &buffLen));
        CHK_STATUS(serializeSessionDescriptionInit(&answerSDPInit, msg.payload, &buffLen));

        // no need to update the version since it should be using the same version
        msg.messageType = SIGNALING_MESSAGE_TYPE_ANSWER;
        STRCPY(msg.peerClientId, this->id.c_str());
        msg.payloadLen = (UINT32) STRLEN(msg.payload);
        msg.correlationId[0] = '\0';

        CHK_STATUS(signalingClientSendMessageSync(this->pPeer->pSignalingClientHandle, &msg));

    CleanUp:

        return retStatus;
    };

    auto handleAnswer = [this](SignalingMessage& msg) -> STATUS {
        STATUS retStatus = STATUS_SUCCESS;
        RtcSessionDescriptionInit answerSDPInit;

        if (pPeer->pConfig->isMaster) {
            DLOGW("Unexpected message SIGNALING_MESSAGE_TYPE_ANSWER");
        } else {
            MEMSET(&answerSDPInit, 0x00, SIZEOF(RtcSessionDescriptionInit));

            CHK_STATUS(deserializeSessionDescriptionInit(msg.payload, msg.payloadLen, &answerSDPInit));
            CHK_STATUS(setRemoteDescription(this->pPeerConnection, &answerSDPInit));
        }

    CleanUp:

        return retStatus;
    };

    auto handleICECandidate = [this](SignalingMessage& msg) -> STATUS {
        STATUS retStatus = STATUS_SUCCESS;
        RtcIceCandidateInit iceCandidate;

        CHK_STATUS(deserializeRtcIceCandidateInit(msg.payload, msg.payloadLen, &iceCandidate));
        CHK_STATUS(addIceCandidate(this->pPeerConnection, iceCandidate.candidate));

    CleanUp:

        CHK_LOG_ERR(retStatus);
        return retStatus;
    };

    STATUS retStatus = STATUS_SUCCESS;
    auto& msg = pMsg->signalingMessage;
    switch (pMsg->signalingMessage.messageType) {
        case SIGNALING_MESSAGE_TYPE_OFFER:
            CHK_STATUS(handleOffer(msg));
            break;
        case SIGNALING_MESSAGE_TYPE_ICE_CANDIDATE:
            CHK_STATUS(handleICECandidate(msg));
            break;
        case SIGNALING_MESSAGE_TYPE_ANSWER:
            CHK_STATUS(handleAnswer(msg));
            break;
        default:
            DLOGW("Unknown message type %u", pMsg->signalingMessage.messageType);
            break;
    }

CleanUp:

    return retStatus;
}

Peer::Peer(const Canary::PConfig pConfig) : pConfig(pConfig), pAwsCredentialProvider(nullptr)
{
}

STATUS Peer::init()
{
    STATUS retStatus = STATUS_SUCCESS;

    CHK_STATUS(createStaticCredentialProvider((PCHAR) pConfig->pAccessKey, 0, (PCHAR) pConfig->pSecretKey, 0, (PCHAR) pConfig->pSessionToken, 0,
                                              MAX_UINT64, &pAwsCredentialProvider));

CleanUp:

    if (STATUS_FAILED(retStatus)) {
        deinit();
    }

    return retStatus;
}

VOID Peer::deinit()
{
    freeStaticCredentialProvider(&pAwsCredentialProvider);
}

STATUS Peer::connect(UINT64 duration)
{
    STATUS retStatus = STATUS_SUCCESS;
    UNUSED_PARAM(duration);

    // CleanUp:

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
        std::shared_ptr<Connection> pConnection;
        std::string msgClientId(pMsg->signalingMessage.peerClientId);

        auto it = std::find_if(pPeer->connections.begin(), pPeer->connections.end(),
                               [&](const std::shared_ptr<Connection>& c) { return c->id == msgClientId; });

        if (it == pPeer->connections.end()) {
            pPeer->connections.push_back(std::make_shared<Connection>(pPeer, msgClientId));
            pConnection = pPeer->connections[pPeer->connections.size() - 1];
        } else {
            pConnection = *it;
        }

        CHK_STATUS(pConnection->handleSignalingMsg(pMsg));

    CleanUp:

        return retStatus;
    };

    CHK_STATUS(createSignalingClientSync(&clientInfo, &channelInfo, &clientCallbacks, pAwsCredentialProvider, &pSignalingClientHandle));

CleanUp:

    return retStatus;
}

STATUS Peer::shutdown()
{
    STATUS retStatus = STATUS_SUCCESS;

    // TODO

    // CleanUp:

    return retStatus;
}

} // namespace Canary
