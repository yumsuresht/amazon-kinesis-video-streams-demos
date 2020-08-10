#include "Include_i.h"

STATUS onNewConnection(std::shared_ptr<Canary::Peer::Connection>);
STATUS run(Canary::PConfig);
VOID sendLocalFrames(Canary::PPeer, MEDIA_STREAM_TRACK_KIND, const std::string&, UINT64, UINT32);

std::function<VOID(INT32)> handleShutdown;
VOID handleSignal(INT32 signal)
{
    if (handleShutdown != NULL) {
        handleShutdown(signal);
    }
}

INT32 main(INT32 argc, CHAR* argv[])
{
#ifndef _WIN32
    signal(SIGINT, handleSignal);
#endif

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

        std::thread videoThread(sendLocalFrames, &peer, MEDIA_STREAM_TRACK_KIND_VIDEO, "./h264SampleFrames/frame-%04d.h264",
                                NUMBER_OF_H264_FRAME_FILES, SAMPLE_VIDEO_FRAME_DURATION);
        std::thread audioThread(sendLocalFrames, &peer, MEDIA_STREAM_TRACK_KIND_AUDIO, "./opusSampleFrames/sample-%03d.opus",
                                NUMBER_OF_OPUS_FRAME_FILES, SAMPLE_AUDIO_FRAME_DURATION);
        videoThread.join();
        audioThread.join();

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

VOID sendLocalFrames(Canary::PPeer pPeer, MEDIA_STREAM_TRACK_KIND kind, const std::string& pattern, UINT64 frameCount, UINT32 frameDuration)
{
    STATUS retStatus = STATUS_SUCCESS;
    Frame frame;
    UINT64 fileIndex = 0, frameSize;
    CHAR filePath[MAX_PATH_LEN + 1];
    UINT64 startTime, lastFrameTime, elapsed;

    frame.frameData = NULL;
    frame.size = 0;
    frame.presentationTs = 0;
    startTime = GETTIME();
    lastFrameTime = startTime;

    while (TRUE) {
        fileIndex = fileIndex % frameCount + 1;
        SNPRINTF(filePath, MAX_PATH_LEN, pattern.c_str(), fileIndex);

        CHK_STATUS(readFile(filePath, TRUE, NULL, &frameSize));

        // Re-alloc if needed
        if (frameSize > frame.size) {
            frame.frameData = (PBYTE) REALLOC(frame.frameData, frameSize);
            CHK_ERR(frame.frameData != NULL, STATUS_NOT_ENOUGH_MEMORY, "Failed to realloc media buffer");
        }
        frame.size = (UINT32) frameSize;

        CHK_STATUS(readFile(filePath, TRUE, frame.frameData, &frameSize));

        frame.presentationTs += frameDuration;

        pPeer->writeFrame(&frame, kind);

        // Adjust sleep in the case the sleep itself and writeFrame take longer than expected. Since sleep makes sure that the thread
        // will be paused at least until the given amount, we can assume that there's no too early frame scenario.
        // Also, it's very unlikely to have a delay greater than SAMPLE_VIDEO_FRAME_DURATION, so the logic assumes that this is always
        // true for simplicity.
        elapsed = lastFrameTime - startTime;
        THREAD_SLEEP(frameDuration - elapsed % frameDuration);
        lastFrameTime = GETTIME();
    }

CleanUp:

    if (STATUS_FAILED(retStatus)) {
        DLOGE("%s thread exited with 0x%08x", kind == MEDIA_STREAM_TRACK_KIND_VIDEO ? "video" : "audio", retStatus);
    }
}
