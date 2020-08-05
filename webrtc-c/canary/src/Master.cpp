#include "Include_i.h"

STATUS run(Canary::PConfig pConfig)
{
    STATUS retStatus = STATUS_SUCCESS;
    // Canary::PCloudwatch pCw;

    // CHK_STATUS(Canary::Cloudwatch::init(pConfig));
    // CHK_STATUS(initKvsWebRtc());
    Canary::Cloudwatch::init(pConfig);
    initKvsWebRtc();

    DLOGE("Test log");

    auto& instance = Canary::Cloudwatch::getInstance();
    auto& errDatum = instance.monitoring.errDatum;
    errDatum.SetValue(30);
    errDatum.SetUnit(Aws::CloudWatch::Model::StandardUnit::None);

    for (auto i = 0; i < 300; i++) {
        instance.monitoring.push(errDatum);
    }

    // CleanUp:
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
