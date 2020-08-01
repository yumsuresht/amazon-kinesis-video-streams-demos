#pragma once

namespace Canary {

class Cloudwatch {
  public:
    static Cloudwatch& getInstance();
    static STATUS init(Canary::PConfig);
    static VOID logger(UINT32, PCHAR, PCHAR, ...);

    VOID pushLog(string log);
    VOID flush();

  private:
    static Cloudwatch& getInstanceImpl(Canary::PConfig pConfig = nullptr, ClientConfiguration* pClientConfig = nullptr)
    {
        static Cloudwatch instance(pConfig, pClientConfig);
        return instance;
    }

    static VOID onPutLogEventResponseReceivedHandler(const Aws::CloudWatchLogs::CloudWatchLogsClient* cwClientLog,
                                                     const Aws::CloudWatchLogs::Model::PutLogEventsRequest& request,
                                                     const Aws::CloudWatchLogs::Model::PutLogEventsOutcome& outcome,
                                                     const std::shared_ptr<const Aws::Client::AsyncCallerContext>& context);

    Cloudwatch(Canary::PConfig pConfig, ClientConfiguration* pClientConfig)
        : pCanaryConfig(pConfig), logsClient(*pClientConfig), metricsClient(*pClientConfig)
    {
    }

    Canary::PConfig pCanaryConfig;
    CloudWatchLogsClient logsClient;
    CloudWatchClient metricsClient;

    PutLogEventsRequest canaryPutLogEventRequest;
    PutLogEventsResult canaryPutLogEventresult;
    Aws::Vector<InputLogEvent> logs;
    Aws::String token;

    CloudWatchLogsClient* pCwl;
};
typedef Cloudwatch* PCloudwatch;

} // namespace Canary
