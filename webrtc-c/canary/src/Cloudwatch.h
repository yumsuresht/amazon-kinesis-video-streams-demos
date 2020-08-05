#pragma once

namespace Canary {

class Synchronization {
  public:
    std::atomic<bool> pending;
    std::recursive_mutex mutex;
    std::condition_variable_any await;
};

class CloudwatchLogs {
  public:
    CloudwatchLogs(Canary::PConfig, ClientConfiguration*);
    STATUS init();
    VOID deinit();
    VOID push(string log);
    VOID flush(BOOL sync = FALSE);

  private:
    PConfig pConfig;
    CloudWatchLogsClient client;
    Synchronization sync;
    Aws::Vector<InputLogEvent> logs;
    Aws::String token;
};

class CloudwatchMonitoring {
  public:
    CloudwatchMonitoring(Canary::PConfig, ClientConfiguration*);
    STATUS init();
    VOID deinit();
    VOID push(MetricDatum);
    MetricDatum errDatum;

  private:
    PConfig pConfig;
    CloudWatchClient client;
    std::atomic<UINT64> pendingMetrics;
};

class Cloudwatch {
  public:
    Cloudwatch() = delete;
    Cloudwatch(Cloudwatch const&) = delete;
    void operator=(Cloudwatch const&) = delete;

    CloudwatchLogs logs;
    CloudwatchMonitoring monitoring;

    static Cloudwatch& getInstance();
    static STATUS init(Canary::PConfig);
    static VOID deinit();
    static VOID logger(UINT32, PCHAR, PCHAR, ...);

  private:
    static Cloudwatch& getInstanceImpl(Canary::PConfig = nullptr, ClientConfiguration* = nullptr);

    Cloudwatch(Canary::PConfig, ClientConfiguration*);
};
typedef Cloudwatch* PCloudwatch;

} // namespace Canary
