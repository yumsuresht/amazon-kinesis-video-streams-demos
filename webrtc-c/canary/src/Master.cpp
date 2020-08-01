#include "Include_i.h"

INT32 main(INT32 argc, CHAR* argv[])
{
    STATUS retStatus = STATUS_SUCCESS;
    Canary::Config config;

    Canary::Config::init(argc, argv, &config);

    Aws::SDKOptions options;
    Aws::InitAPI(options);
    {
        CHK_STATUS(Canary::Cloudwatch::init(&config));
        CHK_STATUS(initKvsWebRtc());
    }

CleanUp:
    Aws::ShutdownAPI(options);
}
