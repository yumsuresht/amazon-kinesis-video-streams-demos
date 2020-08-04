#include "Include_i.h"

namespace Canary {

STATUS Cloudwatch::init(Canary::PConfig pConfig)
{
    STATUS retStatus = STATUS_SUCCESS;
    ClientConfiguration clientConfig;
    CreateLogGroupRequest createLogGroupRequest;
    Aws::CloudWatchLogs::Model::CreateLogStreamOutcome createLogStreamOutcome;
    CreateLogStreamRequest createLogStreamRequest;
    BOOL useCloudwatchLogger = TRUE;

    clientConfig.region = pConfig->pRegion;
    auto& instance = getInstanceImpl(pConfig, &clientConfig);

    createLogGroupRequest.SetLogGroupName(pConfig->pLogGroupName);
    // ignore error since if this operation fails, CreateLogStream should fail as well.
    // There might be some errors that can lead to successfull CreateLogStream, e.g. log group already exists.
    instance.logsClient.CreateLogGroup(createLogGroupRequest);

    createLogStreamRequest.SetLogGroupName(pConfig->pLogGroupName);
    createLogStreamRequest.SetLogStreamName(pConfig->pLogStreamName);
    createLogStreamOutcome = instance.logsClient.CreateLogStream(createLogStreamRequest);

    if (!createLogStreamOutcome.IsSuccess()) {
        useCloudwatchLogger = FALSE;
        DLOGW("Failed to create \"%s\" log stream: %s", pConfig->pLogStreamName, createLogStreamOutcome.GetError().GetMessage().c_str());
    }

    if (useCloudwatchLogger) {
        globalCustomLogPrintFn = logger;
    } else {
        DLOGW("Failed to create Cloudwatch logger, fallback to local output");
    }

    instance.lock = MUTEX_CREATE(TRUE);
    CHK(IS_VALID_MUTEX_VALUE(instance.lock), STATUS_INVALID_OPERATION);

    instance.awaitPendingLogs = CVAR_CREATE();
    CHK(IS_VALID_CVAR_VALUE(instance.awaitPendingLogs), STATUS_INVALID_OPERATION);

CleanUp:

    if (STATUS_FAILED(retStatus)) {
        deinit();
    }

    return retStatus;
}

STATUS Cloudwatch::deinit()
{
    STATUS retStatus = STATUS_SUCCESS;
    auto& instance = getInstance();

    instance.flush(TRUE);
    if (IS_VALID_MUTEX_VALUE(instance.lock)) {
        MUTEX_FREE(instance.lock);
    }

    if (IS_VALID_CVAR_VALUE(instance.awaitPendingLogs)) {
        CVAR_FREE(instance.awaitPendingLogs);
    }

    return retStatus;
}

Cloudwatch& Cloudwatch::getInstance()
{
    return getInstanceImpl();
}

VOID Cloudwatch::pushLog(string log)
{
    MUTEX_LOCK(this->lock);
    Aws::String awsCwString(log.c_str(), log.size());
    auto logEvent =
        Aws::CloudWatchLogs::Model::InputLogEvent().WithMessage(awsCwString).WithTimestamp(GETTIME() / HUNDREDS_OF_NANOS_IN_A_MILLISECOND);
    this->logs.push_back(logEvent);

    if (this->logs.size() >= MAX_CLOUDWATCH_LOG_COUNT) {
        this->flush();
    }
    MUTEX_UNLOCK(this->lock);
}

VOID Cloudwatch::onPutLogEventResponseReceivedHandler(const Aws::CloudWatchLogs::CloudWatchLogsClient* cwClientLog,
                                                      const Aws::CloudWatchLogs::Model::PutLogEventsRequest& request,
                                                      const Aws::CloudWatchLogs::Model::PutLogEventsOutcome& outcome,
                                                      const std::shared_ptr<const Aws::Client::AsyncCallerContext>& context)
{
    UNUSED_PARAM(cwClientLog);
    UNUSED_PARAM(request);
    UNUSED_PARAM(context);
    auto& instance = getInstance();

    if (!outcome.IsSuccess()) {
        DLOGE("Failed to push logs: %s", outcome.GetError().GetMessage().c_str());
    } else {
        DLOGS("Successfully pushed logs to cloudwatch");
        instance.token = outcome.GetResult().GetNextSequenceToken();
    }

    ATOMIC_STORE_BOOL(&instance.hasPendingLogs, FALSE);
    CVAR_SIGNAL(instance.awaitPendingLogs);
}

VOID Cloudwatch::flush(BOOL sync)
{
    MUTEX_LOCK(this->lock);
    if (this->logs.size() == 0) {
        MUTEX_UNLOCK(this->lock);
        return;
    }

    auto pendingLogs = this->logs;
    this->logs.clear();

    // wait until previous logs have been flushed entirely
    while (this->hasPendingLogs) {
        CVAR_WAIT(this->awaitPendingLogs, this->lock, 9999999999999999);
    }

    auto request = Aws::CloudWatchLogs::Model::PutLogEventsRequest()
                       .WithLogGroupName(this->pCanaryConfig->pLogGroupName)
                       .WithLogStreamName(this->pCanaryConfig->pLogStreamName)
                       .WithLogEvents(pendingLogs);

    if (this->token != "") {
        request.SetSequenceToken(this->token);
    }

    if (!sync) {
        ATOMIC_STORE_BOOL(&this->hasPendingLogs, TRUE);
        this->logsClient.PutLogEventsAsync(request, onPutLogEventResponseReceivedHandler);
    } else {
        auto outcome = this->logsClient.PutLogEvents(request);
        if (!outcome.IsSuccess()) {
            DLOGE("Failed to push logs: %s", outcome.GetError().GetMessage().c_str());
        } else {
            DLOGS("Successfully pushed logs to cloudwatch");
            getInstance().token = outcome.GetResult().GetNextSequenceToken();
        }
    }

    MUTEX_UNLOCK(this->lock);
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
        getInstance().pushLog(cwLogFmtString);
    }
}

} // namespace Canary
