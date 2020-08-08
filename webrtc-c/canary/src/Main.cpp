#include "Include_i.h"

STATUS run(Canary::PConfig pConfig)
{
    STATUS retStatus = STATUS_SUCCESS;

    CHK_STATUS(Canary::Cloudwatch::init(pConfig));
    CHK_STATUS(initKvsWebRtc());

    {
        Canary::Peer peer(pConfig);

        CHK_STATUS(peer.init());
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
    Aws::ShutdownAPI(options);
}
