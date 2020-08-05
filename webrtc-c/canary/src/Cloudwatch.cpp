#include "Include_i.h"

namespace Canary {

Cloudwatch::Cloudwatch(Canary::PConfig pConfig, ClientConfiguration* pClientConfig) : logs(pConfig, pClientConfig), monitoring(pConfig, pClientConfig)
{
}

STATUS Cloudwatch::init(Canary::PConfig pConfig)
{
    STATUS retStatus = STATUS_SUCCESS;
    ClientConfiguration clientConfig;
    CreateLogGroupRequest createLogGroupRequest;
    Aws::CloudWatchLogs::Model::CreateLogStreamOutcome createLogStreamOutcome;
    CreateLogStreamRequest createLogStreamRequest;

    clientConfig.region = pConfig->pRegion;
    auto& instance = getInstanceImpl(pConfig, &clientConfig);

    if (STATUS_FAILED(instance.logs.init())) {
        DLOGW("Failed to create Cloudwatch logger, fallback to local output");
    } else {
        globalCustomLogPrintFn = logger;
    }

    CHK_STATUS(instance.monitoring.init());

CleanUp:

    return retStatus;
}

Cloudwatch& Cloudwatch::getInstance()
{
    return getInstanceImpl();
}

Cloudwatch& Cloudwatch::getInstanceImpl(Canary::PConfig pConfig, ClientConfiguration* pClientConfig)
{
    static Cloudwatch instance{pConfig, pClientConfig};
    return instance;
}

VOID Cloudwatch::deinit()
{
    auto& instance = getInstance();
    instance.logs.deinit();
    instance.monitoring.deinit();
}

VOID Cloudwatch::logger(UINT32 level, PCHAR tag, PCHAR fmt, ...)
{
    CHAR logFmtString[MAX_LOG_FORMAT_LENGTH + 1];
    CHAR cwLogFmtString[MAX_LOG_FORMAT_LENGTH + 1];
    UINT32 logLevel = GET_LOGGER_LOG_LEVEL();
    UNUSED_PARAM(tag);

    if (level >= logLevel) {
        addLogMetadata(logFmtString, (UINT32) ARRAY_SIZE(logFmtString), fmt, level);

        // Creating a copy to store the logFmtString for cloudwatch logging purpose
        va_list valist, valist_cw;
        va_start(valist_cw, fmt);
        vsnprintf(cwLogFmtString, (SIZE_T) SIZEOF(cwLogFmtString), logFmtString, valist_cw);
        va_end(valist_cw);
        va_start(valist, fmt);
        vprintf(logFmtString, valist);
        va_end(valist);
        getInstance().logs.push(cwLogFmtString);
    }
}

} // namespace Canary
