#include "Include_i.h"

STATUS run(Canary::PConfig pConfig)
{
    auto onNewConnection = [](std::shared_ptr<Canary::Peer::Connection> pConnection) -> STATUS {
        UNUSED_PARAM(pConnection);
        DLOGI("On new connection: %s", pConnection->id.c_str());
        return STATUS_SUCCESS;
    };

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
