#include "Include_i.h"

namespace Canary {

Peer::Connection::Connection(PPeer pPeer, std::string id, std::function<VOID()> onDisconnected)
    : pPeer(pPeer), id(id), pPeerConnection(nullptr), onDisconnected(onDisconnected), iceGatheringDone(FALSE), terminated(FALSE), receivedOffer(FALSE)
{
}

STATUS Peer::Connection::init()
{
    auto awaitGetIceConfigInfoCount = [](SIGNALING_CLIENT_HANDLE pSignalingClientHandle, PUINT32 pIceConfigInfoCount) -> STATUS {
        STATUS retStatus = STATUS_SUCCESS;
        UINT64 elapsed = 0;

        CHK(IS_VALID_SIGNALING_CLIENT_HANDLE(pSignalingClientHandle) && pIceConfigInfoCount != NULL, STATUS_NULL_ARG);

        while (TRUE) {
            // Get the configuration count
            CHK_STATUS(signalingClientGetIceConfigInfoCount(pSignalingClientHandle, pIceConfigInfoCount));

            // Return OK if we have some ice configs
            CHK(*pIceConfigInfoCount == 0, retStatus);

            // Check for timeout
            CHK_ERR(elapsed <= ASYNC_ICE_CONFIG_INFO_WAIT_TIMEOUT, STATUS_OPERATION_TIMED_OUT,
                    "Couldn't retrieve ICE configurations in alotted time.");

            THREAD_SLEEP(ICE_CONFIG_INFO_POLL_PERIOD);
            elapsed += ICE_CONFIG_INFO_POLL_PERIOD;
        }

    CleanUp:

        return retStatus;
    };

    auto initRtcConfiguration = [this, awaitGetIceConfigInfoCount](PRtcConfiguration pConfiguration) -> STATUS {
        STATUS retStatus = STATUS_SUCCESS;
        auto pConfig = this->pPeer->pConfig;
        auto pSignalingClientHandle = this->pPeer->pSignalingClientHandle;
        UINT32 i, j, iceConfigCount, uriCount;
        PIceConfigInfo pIceConfigInfo;

        MEMSET(pConfiguration, 0x00, SIZEOF(RtcConfiguration));

        // Set this to custom callback to enable filtering of interfaces
        pConfiguration->kvsRtcConfiguration.iceSetInterfaceFilterFunc = NULL;

        // Set the  STUN server
        SNPRINTF(pConfiguration->iceServers[0].urls, MAX_ICE_CONFIG_URI_LEN, KINESIS_VIDEO_STUN_URL, pConfig->pRegion);

        if (pConfig->useTurn) {
            // Set the URIs from the configuration
            CHK_STATUS(awaitGetIceConfigInfoCount(pSignalingClientHandle, &iceConfigCount));

            /* signalingClientGetIceConfigInfoCount can return more than one turn server. Use only one to optimize
             * candidate gathering latency. But user can also choose to use more than 1 turn server. */
            for (i = 0; i < MAX_TURN_SERVERS; i++) {
                CHK_STATUS(signalingClientGetIceConfigInfo(pSignalingClientHandle, i, &pIceConfigInfo));
                for (uriCount = j = 0; j < pIceConfigInfo->uriCount; j++) {
                    CHECK(uriCount < MAX_ICE_SERVERS_COUNT);
                    /*
                     * if configuration.iceServers[uriCount + 1].urls is "turn:ip:port?transport=udp" then ICE will try TURN over UDP
                     * if configuration.iceServers[uriCount + 1].urls is "turn:ip:port?transport=tcp" then ICE will try TURN over TCP/TLS
                     * if configuration.iceServers[uriCount + 1].urls is "turns:ip:port?transport=udp", it's currently ignored because sdk dont do
                     * TURN over DTLS yet. if configuration.iceServers[uriCount + 1].urls is "turns:ip:port?transport=tcp" then ICE will try TURN over
                     * TCP/TLS if configuration.iceServers[uriCount + 1].urls is "turn:ip:port" then ICE will try both TURN over UPD and TCP/TLS
                     *
                     * It's recommended to not pass too many TURN iceServers to configuration because it will slow down ice gathering in non-trickle
                     * mode.
                     */

                    STRNCPY(pConfiguration->iceServers[uriCount + 1].urls, pIceConfigInfo->uris[j], MAX_ICE_CONFIG_URI_LEN);
                    STRNCPY(pConfiguration->iceServers[uriCount + 1].credential, pIceConfigInfo->password, MAX_ICE_CONFIG_CREDENTIAL_LEN);
                    STRNCPY(pConfiguration->iceServers[uriCount + 1].username, pIceConfigInfo->userName, MAX_ICE_CONFIG_USER_NAME_LEN);

                    uriCount++;
                }
            }
        }

    CleanUp:

        return retStatus;
    };

    auto handleOnIceCandidate = [](UINT64 customData, PCHAR candidateJson) -> VOID {
        STATUS retStatus = STATUS_SUCCESS;
        auto pConnection = (Peer::PConnection) customData;
        Canary::PConfig pConfig;
        SIGNALING_CLIENT_HANDLE pSignalingClientHandle;
        SignalingMessage message;

        CHK(pConnection != NULL, STATUS_NULL_ARG);

        pConfig = pConnection->pPeer->pConfig;
        pSignalingClientHandle = pConnection->pPeer->pSignalingClientHandle;

        if (candidateJson == NULL) {
            DLOGD("ice candidate gathering finished");
            pConnection->iceGatheringDone = TRUE;
            pConnection->cvar.notify_all();
        } else if (pConfig->trickleIce) {
            message.version = SIGNALING_MESSAGE_CURRENT_VERSION;
            message.messageType = SIGNALING_MESSAGE_TYPE_ICE_CANDIDATE;
            STRCPY(message.peerClientId, pConnection->id.c_str());
            message.payloadLen = (UINT32) STRLEN(candidateJson);
            STRCPY(message.payload, candidateJson);
            message.correlationId[0] = '\0';
            CHK_STATUS(signalingClientSendMessageSync(pSignalingClientHandle, &message));
        }

    CleanUp:

        CHK_LOG_ERR(retStatus);
    };

    auto onConnectionStateChange = [](UINT64 customData, RTC_PEER_CONNECTION_STATE newState) -> VOID {
        auto pConnection = (Peer::PConnection) customData;

        DLOGI("New connection state %u", newState);

        if (newState == RTC_PEER_CONNECTION_STATE_FAILED || newState == RTC_PEER_CONNECTION_STATE_CLOSED ||
            newState == RTC_PEER_CONNECTION_STATE_DISCONNECTED) {
            if (pConnection->onDisconnected != NULL) {
                pConnection->onDisconnected();
            }
        }
    };

    STATUS retStatus = STATUS_SUCCESS;
    RtcConfiguration configuration;

    CHK(this->pPeerConnection == NULL, STATUS_INVALID_OPERATION);

    CHK_STATUS(initRtcConfiguration(&configuration));
    CHK_STATUS(createPeerConnection(&configuration, &this->pPeerConnection));
    CHK_STATUS(peerConnectionOnIceCandidate(this->pPeerConnection, (UINT64) this, handleOnIceCandidate));
    CHK_STATUS(peerConnectionOnConnectionStateChange(this->pPeerConnection, (UINT64) this, onConnectionStateChange));

CleanUp:

    return retStatus;
}

VOID Peer::Connection::shutdown()
{
    if (!this->terminated.exchange(TRUE)) {
        this->cvar.notify_all();
        {
            // lock to wait until awoken thread finish.
            std::lock_guard<std::mutex> lock(this->mutex);
        }
        if (this->pPeerConnection != NULL) {
            CHK_LOG_ERR(closePeerConnection(this->pPeerConnection));
            CHK_LOG_ERR(freePeerConnection(&this->pPeerConnection));
        }
    }
}

STATUS Peer::Connection::handleSignalingMsg(PReceivedSignalingMessage pMsg)
{
    auto awaitIceGathering = [this](PRtcSessionDescriptionInit pSDPInit) -> STATUS {
        STATUS retStatus = STATUS_SUCCESS;
        std::unique_lock<std::mutex> lock(this->mutex);
        this->cvar.wait(lock, [this]() { return this->terminated.load() || this->iceGatheringDone.load(); });
        CHK_WARN(this->terminated.load(), STATUS_OPERATION_TIMED_OUT, "application terminated and candidate gathering still not done");

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

        if (receivedOffer.exchange(TRUE)) {
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
    std::lock_guard<std::mutex> lock(this->mutex);
    auto& msg = pMsg->signalingMessage;

    CHK(!this->terminated.load(), retStatus);
    switch (msg.messageType) {
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
            DLOGW("Unknown message type %u", msg.messageType);
            break;
    }

CleanUp:

    return retStatus;
}

STATUS Peer::Connection::addTransceiver(RtcMediaStreamTrack& track)
{
    PRtcRtpTransceiver pTransceiver;
    STATUS retStatus = STATUS_SUCCESS;

    CHK_STATUS(::addTransceiver(pPeerConnection, &track, NULL, &pTransceiver));
    if (track.kind == MEDIA_STREAM_TRACK_KIND_VIDEO) {
        videoTransceivers.push_back(pTransceiver);
    } else {
        audioTransceivers.push_back(pTransceiver);
    }

CleanUp:

    return retStatus;
}

STATUS Peer::Connection::addSupportedCodec(RTC_CODEC codec)
{
    STATUS retStatus = STATUS_SUCCESS;

    CHK_STATUS(::addSupportedCodec(pPeerConnection, codec));

CleanUp:

    return retStatus;
}

const std::vector<PRtcRtpTransceiver>& Peer::Connection::getTransceivers(MEDIA_STREAM_TRACK_KIND kind)
{
    return kind == MEDIA_STREAM_TRACK_KIND_VIDEO ? this->videoTransceivers : this->audioTransceivers;
}

BOOL Peer::Connection::isTerminated()
{
    return this->terminated.load();
}

STATUS Peer::Connection::writeFrame(PRtcRtpTransceiver pTransceiver, PFrame pFrame)
{
    STATUS retStatus = STATUS_SUCCESS;

    CHK_STATUS(::writeFrame(pTransceiver, pFrame));

CleanUp:

    return retStatus;
}

} // namespace Canary
