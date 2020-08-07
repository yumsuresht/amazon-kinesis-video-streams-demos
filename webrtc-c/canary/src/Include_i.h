#pragma once

#define DEFAULT_CLOUDWATCH_NAMESPACE "KinesisVideoSDKCanary"

#define MAX_LOG_STREAM_NAME      512
#define MAX_CLOUDWATCH_LOG_COUNT 128

#define CANARY_CHANNEL_NAME_ENV_VAR    "CANARY_CHANNEL_NAME"
#define CANARY_PEER_ID_ENV_VAR         "CANARY_PEER_ID"
#define CANARY_TRICKLE_ICE_ENV_VAR     "CANARY_TRICKLE_ICE"
#define CANARY_IS_MASTER_ENV_VAR       "CANARY_IS_MASTER"
#define CANARY_USE_TURN_ENV_VAR        "CANARY_USE_TURN"
#define CANARY_LOG_GROUP_NAME_ENV_VAR  "CANARY_LOG_GROUP_NAME"
#define CANARY_LOG_STREAM_NAME_ENV_VAR "CANARY_LOG_STREAM_NAME"
#define CANARY_CERT_PATH_ENV_VAR       "CANARY_CERT_PATH"

#include <aws/core/Aws.h>
#include <aws/monitoring/CloudWatchClient.h>
#include <aws/monitoring/model/PutMetricDataRequest.h>
#include <aws/logs/CloudWatchLogsClient.h>
#include <aws/logs/model/CreateLogGroupRequest.h>
#include <aws/logs/model/CreateLogStreamRequest.h>
#include <aws/logs/model/PutLogEventsRequest.h>
#include <aws/logs/model/DeleteLogStreamRequest.h>
#include <aws/logs/model/DescribeLogStreamsRequest.h>

#include <com/amazonaws/kinesis/video/webrtcclient/Include.h>

using namespace Aws::Client;
using namespace Aws::CloudWatchLogs;
using namespace Aws::CloudWatchLogs::Model;
using namespace Aws::CloudWatch::Model;
using namespace Aws::CloudWatch;
using namespace std;

#include "Config.h"
#include "Cloudwatch.h"
#include "Peer.h"
