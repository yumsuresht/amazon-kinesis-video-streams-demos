#include "Include_i.h"

namespace Canary {

CloudwatchMonitoring::CloudwatchMonitoring(PConfig pConfig, ClientConfiguration* pClientConfig) : pConfig(pConfig), client(*pClientConfig)
{
}

STATUS CloudwatchMonitoring::init()
{
    STATUS retStatus = STATUS_SUCCESS;
    Dimension dimension;

    dimension.SetName("Channel");
    dimension.SetValue(pConfig->pChannelName);

    this->errDatum.SetMetricName("ErrorCount");
    this->errDatum.AddDimensions(dimension);

    return retStatus;
}

VOID CloudwatchMonitoring::deinit()
{
    // need to wait all metrics to be flushed out, otherwise we'll get a segfault.
    // https://docs.aws.amazon.com/sdk-for-cpp/v1/developer-guide/basic-use.html
    // TODO: maybe add a timeout? But, this might cause a segfault if it hits a timeout.
    while (this->pendingMetrics.load() > 0) {
        THREAD_SLEEP(HUNDREDS_OF_NANOS_IN_A_MILLISECOND * 500);
    }
}

VOID CloudwatchMonitoring::push(MetricDatum datum)
{
    Aws::CloudWatch::Model::PutMetricDataRequest cwRequest;
    cwRequest.SetNamespace(DEFAULT_CLOUDWATCH_NAMESPACE);
    cwRequest.AddMetricData(datum);

    auto asyncHandler = [this](const Aws::CloudWatch::CloudWatchClient* cwClient, const Aws::CloudWatch::Model::PutMetricDataRequest& request,
                               const Aws::CloudWatch::Model::PutMetricDataOutcome& outcome,
                               const std::shared_ptr<const Aws::Client::AsyncCallerContext>& context) {
        UNUSED_PARAM(cwClient);
        UNUSED_PARAM(request);
        UNUSED_PARAM(context);

        if (!outcome.IsSuccess()) {
            DLOGE("Failed to put sample metric data: %s", outcome.GetError().GetMessage().c_str());
        } else {
            DLOGS("Successfully put sample metric data");
        }
        this->pendingMetrics--;
    };
    this->pendingMetrics++;
    this->client.PutMetricDataAsync(cwRequest, asyncHandler);
}

} // namespace Canary
