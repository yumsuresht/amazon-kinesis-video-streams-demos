#pragma once

#define DEFAULT_RETENTION_PERIOD     (2 * HUNDREDS_OF_NANOS_IN_AN_HOUR)
#define DEFAULT_BUFFER_DURATION      (120 * HUNDREDS_OF_NANOS_IN_A_SECOND)
#define DEFAULT_CALLBACK_CHAIN_COUNT 5
#define DEFAULT_KEY_FRAME_INTERVAL   45
#define DEFAULT_FPS_VALUE            25
#define DEFAULT_STREAM_DURATION      (20 * HUNDREDS_OF_NANOS_IN_A_SECOND)

#define NUMBER_OF_FRAME_FILES 403
#define CANARY_METADATA_SIZE  (SIZEOF(INT64) + SIZEOF(UINT32) + SIZEOF(UINT32) + SIZEOF(UINT64))

#define MAX_LOG_STREAM_NAME      512
#define MAX_CLOUDWATCH_LOG_COUNT 128

#define CANARY_CHANNEL_NAME_ENV_VAR    "CANARY_CHANNEL_NAME"
#define CANARY_PEER_ID_ENV_VAR         "CANARY_PEER_ID"
#define CANARY_TRICKLE_ICE_ENV_VAR     "CANARY_TRICKLE_ICE"
#define CANARY_IS_MASTER_ENV_VAR       "CANARY_IS_MASTER"
#define CANARY_USE_TURN_ENV_VAR        "CANARY_USE_TURN"
#define CANARY_LOG_GROUP_NAME_ENV_VAR  "CANARY_LOG_GROUP_NAME"
#define CANARY_LOG_STREAM_NAME_ENV_VAR "CANARY_LOG_STREAM_NAME"

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
#include "Common.h"
