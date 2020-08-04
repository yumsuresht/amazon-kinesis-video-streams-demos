#include "Include_i.h"

STATUS run(INT32 argc, CHAR* argv[])
{
    STATUS retStatus = STATUS_SUCCESS;
    Canary::Config config;

    CHK_STATUS(Canary::Config::init(argc, argv, &config));
    CHK_STATUS(Canary::Cloudwatch::init(&config));
    CHK_STATUS(initKvsWebRtc());

    DLOGE("Test log");

CleanUp:

    Canary::Cloudwatch::deinit();

    return retStatus;
}

INT32 main(INT32 argc, CHAR* argv[])
{
    STATUS retStatus = STATUS_SUCCESS;

    Aws::SDKOptions options;
    Aws::InitAPI(options);

    CHK_STATUS(run(argc, argv));

CleanUp:
    Aws::ShutdownAPI(options);
}
