#pragma once

namespace Canary {

class Cloudwatch {
  public:
    Cloudwatch() = delete;
    Cloudwatch(Cloudwatch const&) = delete;
    void operator=(Cloudwatch const&) = delete;

    static Cloudwatch& getInstance();
    static STATUS init(Canary::PConfig);
    static STATUS deinit();
    static VOID logger(UINT32, PCHAR, PCHAR, ...);

    VOID pushLog(string log);
    VOID flush(BOOL sync = FALSE);

  private:
    static Cloudwatch& getInstanceImpl(Canary::PConfig = nullptr, ClientConfiguration* = nullptr);

    Cloudwatch(Canary::PConfig, ClientConfiguration*);

    static VOID onPutLogEventResponseReceivedHandler(const Aws::CloudWatchLogs::CloudWatchLogsClient*,
                                                     const Aws::CloudWatchLogs::Model::PutLogEventsRequest&,
                                                     const Aws::CloudWatchLogs::Model::PutLogEventsOutcome&,
                                                     const std::shared_ptr<const Aws::Client::AsyncCallerContext>&);

    Canary::PConfig pCanaryConfig;
    CloudWatchLogsClient logsClient;
    CloudWatchClient metricsClient;

    Aws::Vector<InputLogEvent> logs;
    Aws::String token;

    Aws::Vector<InputLogEvent> pendingLogs;
    volatile ATOMIC_BOOL hasPendingLogs;
    CVAR awaitPendingLogs;
    MUTEX lock;
};
typedef Cloudwatch* PCloudwatch;

} // namespace Canary
