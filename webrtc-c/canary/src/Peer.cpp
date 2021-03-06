#include "Include.h"

namespace Canary {

Peer::Peer(const Canary::PConfig pConfig, const Callbacks& callbacks)
    : pConfig(pConfig), callbacks(callbacks), pAwsCredentialProvider(nullptr), terminated(FALSE), iceGatheringDone(FALSE), receivedOffer(FALSE),
      receivedAnswer(FALSE), foundPeerId(FALSE), pPeerConnection(nullptr), status(STATUS_SUCCESS)
{
}

Peer::~Peer()
{
    CHK_LOG_ERR(freePeerConnection(&this->pPeerConnection));
    CHK_LOG_ERR(freeSignalingClient(&this->pSignalingClientHandle));
    CHK_LOG_ERR(freeStaticCredentialProvider(&this->pAwsCredentialProvider));
}

STATUS Peer::init()
{
    STATUS retStatus = STATUS_SUCCESS;

    CHK_STATUS(createStaticCredentialProvider((PCHAR) pConfig->pAccessKey, 0, (PCHAR) pConfig->pSecretKey, 0, (PCHAR) pConfig->pSessionToken, 0,
                                              MAX_UINT64, &pAwsCredentialProvider));
    CHK_STATUS(initSignaling());
    CHK_STATUS(initRtcConfiguration());

CleanUp:

    return retStatus;
}

STATUS Peer::initSignaling()
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
    channelInfo.pCertPath = (PCHAR) DEFAULT_KVS_CACERT_PATH;
    channelInfo.messageTtl = 0; // Default is 60 seconds

    clientCallbacks.customData = (UINT64) this;
    clientCallbacks.stateChangeFn = [](UINT64 customData, SIGNALING_CLIENT_STATE state) -> STATUS {
        STATUS retStatus = STATUS_SUCCESS;
        PPeer pPeer = (PPeer) customData;
        PCHAR pStateStr;

        signalingClientGetStateString(state, &pStateStr);
        DLOGD("Signaling client state changed to %d - '%s'", state, pStateStr);

        switch (state) {
            case SIGNALING_CLIENT_STATE_NEW:
                pPeer->signalingStartTime = GETTIME();
                break;
            case SIGNALING_CLIENT_STATE_CONNECTED: {
                auto duration = (GETTIME() - pPeer->signalingStartTime) / HUNDREDS_OF_NANOS_IN_A_MILLISECOND;
                DLOGI("Signaling took %lu ms to connect", duration);
                Canary::Cloudwatch::getInstance().monitoring.pushSignalingInitDelay(duration, StandardUnit::Milliseconds);
                break;
            }
            default:
                break;
        }

        // Return success to continue
        return retStatus;
    };
    clientCallbacks.errorReportFn = [](UINT64 customData, STATUS status, PCHAR msg, UINT32 msgLen) -> STATUS {
        PPeer pPeer = (PPeer) customData;
        DLOGW("Signaling client generated an error 0x%08x - '%.*s'", status, msgLen, msg);

        // When an error happens with signaling, we'll let it crash so that this canary can be restarted.
        // The error will be captured in at higher level metrics.
        if (status == STATUS_SIGNALING_ICE_CONFIG_REFRESH_FAILED || status == STATUS_SIGNALING_RECONNECT_FAILED) {
            pPeer->status = status;

            // Let the higher level to terminate
            if (pPeer->callbacks.onDisconnected != NULL) {
                pPeer->callbacks.onDisconnected();
            }
        }

        return STATUS_SUCCESS;
    };
    clientCallbacks.messageReceivedFn = [](UINT64 customData, PReceivedSignalingMessage pMsg) -> STATUS {
        STATUS retStatus = STATUS_SUCCESS;
        PPeer pPeer = (PPeer) customData;
        std::lock_guard<std::recursive_mutex> lock(pPeer->mutex);

        if (!pPeer->foundPeerId.load()) {
            pPeer->peerId = pMsg->signalingMessage.peerClientId;
            DLOGI("Found peer id: %s", pPeer->peerId.c_str());
            pPeer->foundPeerId = TRUE;
            CHK_STATUS(pPeer->initPeerConnection());
        }

        if (pPeer->pConfig->isMaster && STRCMP(pPeer->peerId.c_str(), pMsg->signalingMessage.peerClientId) != 0) {
            DLOGW("Unexpected receiving message from extra peer: %s", pMsg->signalingMessage.peerClientId);
            CHK(FALSE, retStatus);
        }

        DLOGD("Handling signaling message:\n%s", pMsg->signalingMessage.payload);
        CHK_STATUS(pPeer->handleSignalingMsg(pMsg));

    CleanUp:

        return retStatus;
    };

    CHK_STATUS(createSignalingClientSync(&clientInfo, &channelInfo, &clientCallbacks, pAwsCredentialProvider, &pSignalingClientHandle));

CleanUp:

    return retStatus;
}

STATUS Peer::initRtcConfiguration()
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

    STATUS retStatus = STATUS_SUCCESS;
    auto pConfig = this->pConfig;
    auto pSignalingClientHandle = this->pSignalingClientHandle;
    UINT32 i, j, iceConfigCount, uriCount;
    PIceConfigInfo pIceConfigInfo;
    PRtcConfiguration pConfiguration = &this->rtcConfiguration;

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
        for (uriCount = 0, i = 0; i < MAX_TURN_SERVERS; i++) {
            CHK_STATUS(signalingClientGetIceConfigInfo(pSignalingClientHandle, i, &pIceConfigInfo));
            for (j = 0; j < pIceConfigInfo->uriCount; j++) {
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
}

STATUS Peer::initPeerConnection()
{
    auto handleOnIceCandidate = [](UINT64 customData, PCHAR candidateJson) -> VOID {
        STATUS retStatus = STATUS_SUCCESS;
        auto pPeer = (PPeer) customData;
        SignalingMessage message;

        if (candidateJson == NULL) {
            DLOGD("ice candidate gathering finished");
            pPeer->iceGatheringDone = TRUE;
            pPeer->cvar.notify_all();
        } else if (pPeer->pConfig->trickleIce) {
            message.messageType = SIGNALING_MESSAGE_TYPE_ICE_CANDIDATE;
            STRCPY(message.payload, candidateJson);
            CHK_STATUS(pPeer->send(&message));
        }

    CleanUp:

        CHK_LOG_ERR(retStatus);
    };

    auto onConnectionStateChange = [](UINT64 customData, RTC_PEER_CONNECTION_STATE newState) -> VOID {
        auto pPeer = (PPeer) customData;

        DLOGI("New connection state %u", newState);

        switch (newState) {
            case RTC_PEER_CONNECTION_STATE_CONNECTING:
                pPeer->iceHolePunchingStartTime = GETTIME();
                break;
            case RTC_PEER_CONNECTION_STATE_CONNECTED: {
                auto duration = (GETTIME() - pPeer->iceHolePunchingStartTime) / HUNDREDS_OF_NANOS_IN_A_MILLISECOND;
                DLOGI("ICE hole punching took %lu ms", duration);
                Canary::Cloudwatch::getInstance().monitoring.pushICEHolePunchingDelay(duration, StandardUnit::Milliseconds);
                break;
            }
            case RTC_PEER_CONNECTION_STATE_FAILED:
                // TODO: Replace this with a proper error code. Since there's no way to get the actual error code
                // at this moment, STATUS_PEERCONNECTION_BASE seems to be the best error code.
                pPeer->status = STATUS_PEERCONNECTION_BASE;
                // explicit fallthrough
            case RTC_PEER_CONNECTION_STATE_CLOSED:
                // explicit fallthrough
            case RTC_PEER_CONNECTION_STATE_DISCONNECTED:
                // Let the higher level to terminate
                if (pPeer->callbacks.onDisconnected != NULL) {
                    pPeer->callbacks.onDisconnected();
                }
                break;
            default:
                break;
        }
    };

    STATUS retStatus = STATUS_SUCCESS;
    CHK(this->pPeerConnection == NULL, STATUS_INVALID_OPERATION);

    CHK_STATUS(createPeerConnection(&this->rtcConfiguration, &this->pPeerConnection));
    CHK_STATUS(peerConnectionOnIceCandidate(this->pPeerConnection, (UINT64) this, handleOnIceCandidate));
    CHK_STATUS(peerConnectionOnConnectionStateChange(this->pPeerConnection, (UINT64) this, onConnectionStateChange));

    if (this->callbacks.onNewConnection != NULL) {
        this->callbacks.onNewConnection(this);
    }

CleanUp:

    return retStatus;
}

STATUS Peer::shutdown()
{
    this->terminated = TRUE;

    this->cvar.notify_all();
    {
        // lock to wait until awoken thread finish.
        std::lock_guard<std::recursive_mutex> lock(this->mutex);
    }

    if (this->pPeerConnection != NULL) {
        CHK_LOG_ERR(closePeerConnection(this->pPeerConnection));
    }

    return this->status;
}

STATUS Peer::connect()
{
    auto connectPeerConnection = [this]() -> STATUS {
        STATUS retStatus = STATUS_SUCCESS;
        RtcSessionDescriptionInit offerSDPInit;
        UINT32 buffLen;
        SignalingMessage msg;

        MEMSET(&offerSDPInit, 0, SIZEOF(offerSDPInit));
        CHK_STATUS(createOffer(this->pPeerConnection, &offerSDPInit));
        CHK_STATUS(setLocalDescription(this->pPeerConnection, &offerSDPInit));

        if (!this->pConfig->trickleIce) {
            CHK_STATUS(this->awaitIceGathering(&offerSDPInit));
        }

        msg.messageType = SIGNALING_MESSAGE_TYPE_OFFER;
        CHK_STATUS(serializeSessionDescriptionInit(&offerSDPInit, NULL, &buffLen));
        CHK_STATUS(serializeSessionDescriptionInit(&offerSDPInit, msg.payload, &buffLen));
        CHK_STATUS(this->send(&msg));

    CleanUp:

        return retStatus;
    };

    STATUS retStatus = STATUS_SUCCESS;
    CHK_STATUS(signalingClientConnectSync(pSignalingClientHandle));

    if (!this->pConfig->isMaster) {
        this->foundPeerId = TRUE;
        this->peerId = DEFAULT_VIEWER_PEER_ID;
        CHK_STATUS(this->initPeerConnection());
        CHK_STATUS(connectPeerConnection());
    }

CleanUp:

    return retStatus;
}

STATUS Peer::send(PSignalingMessage pMsg)
{
    STATUS retStatus = STATUS_SUCCESS;

    if (this->foundPeerId.load()) {
        pMsg->version = SIGNALING_MESSAGE_CURRENT_VERSION;
        pMsg->correlationId[0] = '\0';
        STRCPY(pMsg->peerClientId, peerId.c_str());
        pMsg->payloadLen = (UINT32) STRLEN(pMsg->payload);
        CHK_STATUS(signalingClientSendMessageSync(this->pSignalingClientHandle, pMsg));
    } else {
        // TODO: maybe queue messages when there's no peer id
        DLOGW("Peer id hasn't been found yet. Failed to send a signaling message");
    }

CleanUp:

    return retStatus;
}

STATUS Peer::awaitIceGathering(PRtcSessionDescriptionInit pSDPInit)
{
    STATUS retStatus = STATUS_SUCCESS;
    std::unique_lock<std::recursive_mutex> lock(this->mutex);
    this->cvar.wait(lock, [this]() { return this->terminated.load() || this->iceGatheringDone.load(); });
    CHK_WARN(!this->terminated.load(), STATUS_OPERATION_TIMED_OUT, "application terminated and candidate gathering still not done");

    CHK_STATUS(peerConnectionGetCurrentLocalDescription(this->pPeerConnection, pSDPInit));

CleanUp:

    return retStatus;
};

STATUS Peer::handleSignalingMsg(PReceivedSignalingMessage pMsg)
{
    auto handleOffer = [this](SignalingMessage& msg) -> STATUS {
        STATUS retStatus = STATUS_SUCCESS;
        RtcSessionDescriptionInit offerSDPInit, answerSDPInit;
        NullableBool canTrickle;
        UINT32 buffLen;

        if (!this->pConfig->isMaster) {
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
            CHK_STATUS(this->awaitIceGathering(&answerSDPInit));
        }

        msg.messageType = SIGNALING_MESSAGE_TYPE_ANSWER;
        CHK_STATUS(serializeSessionDescriptionInit(&answerSDPInit, NULL, &buffLen));
        CHK_STATUS(serializeSessionDescriptionInit(&answerSDPInit, msg.payload, &buffLen));

        CHK_STATUS(this->send(&msg));

    CleanUp:

        return retStatus;
    };

    auto handleAnswer = [this](SignalingMessage& msg) -> STATUS {
        STATUS retStatus = STATUS_SUCCESS;
        RtcSessionDescriptionInit answerSDPInit;

        if (this->pConfig->isMaster) {
            DLOGW("Unexpected message SIGNALING_MESSAGE_TYPE_ANSWER");
        } else if (receivedAnswer.exchange(TRUE)) {
            DLOGW("Offer already received, ignore new offer from client id %s", msg.peerClientId);
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

STATUS Peer::addTransceiver(RtcMediaStreamTrack& track)
{
    auto handleFrame = [](UINT64 customData, PFrame pFrame) -> VOID {
        UNUSED_PARAM(customData);
        // TODO: Probably reexpose or add metrics here directly
        DLOGV("Frame received. TrackId: %" PRIu64 ", Size: %u, Flags %u", pFrame->trackId, pFrame->size, pFrame->flags);
    };

    auto handleBandwidthEstimation = [](UINT64 customData, DOUBLE maxiumBitrate) -> VOID {
        UNUSED_PARAM(customData);
        // TODO: Probably reexpose or add metrics here directly
        DLOGV("received bitrate suggestion: %f", maxiumBitrate);
    };

    PRtcRtpTransceiver pTransceiver;
    STATUS retStatus = STATUS_SUCCESS;

    CHK_STATUS(::addTransceiver(pPeerConnection, &track, NULL, &pTransceiver));
    if (track.kind == MEDIA_STREAM_TRACK_KIND_VIDEO) {
        this->videoTransceivers.push_back(pTransceiver);
    } else {
        this->audioTransceivers.push_back(pTransceiver);
    }

    CHK_STATUS(transceiverOnFrame(pTransceiver, (UINT64) this, handleFrame));
    CHK_STATUS(transceiverOnBandwidthEstimation(pTransceiver, (UINT64) this, handleBandwidthEstimation));

CleanUp:

    return retStatus;
}

STATUS Peer::addSupportedCodec(RTC_CODEC codec)
{
    STATUS retStatus = STATUS_SUCCESS;

    CHK_STATUS(::addSupportedCodec(pPeerConnection, codec));

CleanUp:

    return retStatus;
}

STATUS Peer::writeFrame(PFrame pFrame, MEDIA_STREAM_TRACK_KIND kind)
{
    STATUS retStatus = STATUS_SUCCESS;

    auto& transceivers = kind == MEDIA_STREAM_TRACK_KIND_VIDEO ? this->videoTransceivers : this->audioTransceivers;

    for (auto& transceiver : transceivers) {
        CHK_LOG_ERR(::writeFrame(transceiver, pFrame));
    }

CleanUp:

    return retStatus;
}

} // namespace Canary
