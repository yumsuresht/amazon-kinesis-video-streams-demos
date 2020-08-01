#include "Include_i.h"

namespace Canary {

STATUS Cloudwatch::init(Canary::PConfig pConfig)
{
    ClientConfiguration clientConfig;
    CreateLogGroupRequest createLogGroupRequest;
    Aws::CloudWatchLogs::Model::CreateLogGroupOutcome createLogGroupOutcome;
    Aws::CloudWatchLogs::Model::CreateLogStreamOutcome createLogStreamOutcome;
    CreateLogStreamRequest createLogStreamRequest;
    BOOL useCloudwatchLogger = TRUE;

    clientConfig.region = pConfig->pRegion;
    auto cloudwatch = getInstanceImpl(pConfig, &clientConfig);

    createLogGroupRequest.SetLogGroupName(pConfig->pLogGroupName);
    createLogGroupOutcome = cloudwatch.logsClient.CreateLogGroup(createLogGroupRequest);
    if (createLogGroupOutcome.IsSuccess()) {
        createLogStreamRequest.SetLogGroupName(pConfig->pLogGroupName);
        createLogStreamRequest.SetLogStreamName(pConfig->pLogStreamName);
        createLogStreamOutcome = cloudwatch.logsClient.CreateLogStream(createLogStreamRequest);

        if (!createLogStreamOutcome.IsSuccess()) {
            useCloudwatchLogger = FALSE;
            DLOGW("Failed to create \"%s\" log stream: %s", pConfig->pLogStreamName, createLogStreamOutcome.GetError().GetMessage().c_str());
        }
    } else {
        DLOGW("Failed to create \"%s\" log group: %s", pConfig->pLogGroupName, createLogGroupOutcome.GetError().GetMessage().c_str());
    }

    if (useCloudwatchLogger) {
        globalCustomLogPrintFn = logger;
    } else {
        DLOGW("Failed to create Cloudwatch logger, fallback to local output");
    }

    return STATUS_SUCCESS;
}

VOID Cloudwatch::pushLog(string log)
{
    Aws::String awsCwString(log);
    auto logEvent =
        Aws::CloudWatchLogs::Model::InputLogEvent().WithMessage(awsCwString).WithTimestamp(GETTIME() / HUNDREDS_OF_NANOS_IN_A_MILLISECOND);
    logs.push_back(logEvent);

    if (logs.size() >= MAX_CLOUDWATCH_LOG_COUNT) {
        flush();
    }
}

VOID Cloudwatch::onPutLogEventResponseReceivedHandler(const Aws::CloudWatchLogs::CloudWatchLogsClient* cwClientLog,
                                                      const Aws::CloudWatchLogs::Model::PutLogEventsRequest& request,
                                                      const Aws::CloudWatchLogs::Model::PutLogEventsOutcome& outcome,
                                                      const std::shared_ptr<const Aws::Client::AsyncCallerContext>& context)
{
    UNUSED_PARAM(cwClientLog);
    UNUSED_PARAM(request);
    UNUSED_PARAM(context);

    if (!outcome.IsSuccess()) {
        DLOGE("Failed to push logs: %s", outcome.GetError().GetMessage().c_str());
    } else {
        DLOGS("Successfully pushed logs to cloudwatch");
        Cloudwatch::getInstance().token = outcome.GetResult().GetNextSequenceToken();
    }
}

VOID Cloudwatch::flush()
{
    Aws::CloudWatchLogs::Model::PutLogEventsOutcome outcome;
    auto request = Aws::CloudWatchLogs::Model::PutLogEventsRequest()
                       .WithLogGroupName(pCanaryConfig->pLogGroupName)
                       .WithLogStreamName(pCanaryConfig->pLogStreamName)
                       .WithLogEvents(logs);
    if (token != "") {
        request.SetSequenceToken(token);
    }
    logsClient.PutLogEventsAsync(request, onPutLogEventResponseReceivedHandler);
    logs.clear();
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
        Cloudwatch::getInstance().pushLog(cwLogFmtString);
    }
}

} // namespace Canary
