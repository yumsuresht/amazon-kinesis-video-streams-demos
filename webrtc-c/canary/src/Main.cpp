#include "Include_i.h"

STATUS onNewConnection(std::shared_ptr<Canary::Peer::Connection>);
STATUS run(Canary::PConfig);

INT32 main(INT32 argc, CHAR* argv[])
{
    STATUS retStatus = STATUS_SUCCESS;
    Canary::Config config;

    Aws::SDKOptions options;
    Aws::InitAPI(options);

    CHK_STATUS(Canary::Config::init(argc, argv, &config));
    CHK_STATUS(run(&config));

CleanUp:
    DLOGI("Exiting");
    Aws::ShutdownAPI(options);
}

STATUS run(Canary::PConfig pConfig)
{
    STATUS retStatus = STATUS_SUCCESS;

    CHK_STATUS(Canary::Cloudwatch::init(pConfig));
    CHK_STATUS(initKvsWebRtc());
    SET_LOGGER_LOG_LEVEL(pConfig->logLevel);

    {
        Canary::Peer::Callbacks callbacks;
        callbacks.onNewConnection = onNewConnection;

        Canary::Peer peer(pConfig, callbacks);

        CHK_STATUS(peer.init());
        CHK_STATUS(peer.connect(0));

        while (1) {
            THREAD_SLEEP(HUNDREDS_OF_NANOS_IN_A_SECOND * 5);
        }
    }

CleanUp:
    deinitKvsWebRtc();
    Canary::Cloudwatch::deinit();

    return retStatus;
}

STATUS onNewConnection(std::shared_ptr<Canary::Peer::Connection> pConnection)
{
    STATUS retStatus = STATUS_SUCCESS;
    RtcMediaStreamTrack videoTrack, audioTrack;

    MEMSET(&videoTrack, 0x00, SIZEOF(RtcMediaStreamTrack));
    MEMSET(&audioTrack, 0x00, SIZEOF(RtcMediaStreamTrack));

    // Declare that we support H264,Profile=42E01F,level-asymmetry-allowed=1,packetization-mode=1 and Opus
    CHK_STATUS(pConnection->addSupportedCodec(RTC_CODEC_H264_PROFILE_42E01F_LEVEL_ASYMMETRY_ALLOWED_PACKETIZATION_MODE));
    CHK_STATUS(pConnection->addSupportedCodec(RTC_CODEC_OPUS));

    // Add a SendRecv Transceiver of type video
    videoTrack.kind = MEDIA_STREAM_TRACK_KIND_VIDEO;
    videoTrack.codec = RTC_CODEC_H264_PROFILE_42E01F_LEVEL_ASYMMETRY_ALLOWED_PACKETIZATION_MODE;
    STRCPY(videoTrack.streamId, "myKvsVideoStream");
    STRCPY(videoTrack.trackId, "myVideoTrack");
    CHK_STATUS(pConnection->addTransceiver(videoTrack));

    // Add a SendRecv Transceiver of type video
    audioTrack.kind = MEDIA_STREAM_TRACK_KIND_AUDIO;
    audioTrack.codec = RTC_CODEC_OPUS;
    STRCPY(audioTrack.streamId, "myKvsVideoStream");
    STRCPY(audioTrack.trackId, "myAudioTrack");
    CHK_STATUS(pConnection->addTransceiver(audioTrack));

CleanUp:

    return retStatus;
}
