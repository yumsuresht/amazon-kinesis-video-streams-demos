#include "Include_i.h"

namespace Canary {

STATUS mustenv(CHAR const* pKey, const CHAR** ppValue)
{
    STATUS retStatus = STATUS_SUCCESS;

    CHK(ppValue != NULL, STATUS_NULL_ARG);

    CHK_ERR((*ppValue = getenv(pKey)) != NULL, STATUS_INVALID_OPERATION, "%s must be set", pKey);

CleanUp:

    return retStatus;
}

BOOL getenvBool(CHAR const* pKey)
{
    PCHAR pValue;

    pValue = getenv(pKey);
    if (pValue == NULL) {
        DLOGW("%s is not set, defaulting to false", pKey);
    } else if (STRCMPI(pValue, "on") == 0 || STRCMPI(pValue, "true") == 0) {
        return TRUE;
    }

    return FALSE;
}

STATUS Config::init(INT32 argc, PCHAR argv[], Canary::PConfig pConfig)
{
    // TODO: Probably also support command line args to fill the config
    UNUSED_PARAM(argc);
    UNUSED_PARAM(argv);

    STATUS retStatus = STATUS_SUCCESS;
    PCHAR pLogLevel, pLogStreamName;
    const CHAR* pLogGroupName;

    CHK(pConfig != NULL, STATUS_NULL_ARG);

    MEMSET(pConfig, 0, SIZEOF(Config));

    CHK_STATUS(mustenv(CANARY_CHANNEL_NAME_ENV_VAR, &pConfig->pChannelName));
    CHK_STATUS(mustenv(CANARY_CLIENT_ID_ENV_VAR, &pConfig->pClientId));
    pConfig->isMaster = getenvBool(CANARY_IS_MASTER_ENV_VAR);
    /* This is ignored for master. Master can extract the info from offer. Viewer has to know if peer can trickle or
     * not ahead of time. */
    pConfig->trickleIce = getenvBool(CANARY_TRICKLE_ICE_ENV_VAR);
    pConfig->useTurn = getenvBool(CANARY_USE_TURN_ENV_VAR);

    CHK_STATUS(mustenv(ACCESS_KEY_ENV_VAR, &pConfig->pAccessKey));
    CHK_STATUS(mustenv(SECRET_KEY_ENV_VAR, &pConfig->pSecretKey));
    pConfig->pSessionToken = getenv(SESSION_TOKEN_ENV_VAR);
    if ((pConfig->pRegion = getenv(DEFAULT_REGION_ENV_VAR)) == NULL) {
        pConfig->pRegion = DEFAULT_AWS_REGION;
    }

    CHK_STATUS(mustenv(CANARY_CERT_PATH_ENV_VAR, &pConfig->pCertPath));

    // Set the logger log level
    if (NULL == (pLogLevel = getenv(DEBUG_LOG_LEVEL_ENV_VAR)) || (STATUS_SUCCESS != STRTOUI32(pLogLevel, NULL, 10, &pConfig->logLevel))) {
        pConfig->logLevel = LOG_LEVEL_WARN;
    }

    CHK_STATUS(mustenv(CANARY_LOG_GROUP_NAME_ENV_VAR, &pLogGroupName));
    STRNCPY(pConfig->pLogGroupName, pLogGroupName, ARRAY_SIZE(pConfig->pLogGroupName) - 1);

    pLogStreamName = getenv(CANARY_LOG_STREAM_NAME_ENV_VAR);
    if (pLogStreamName != NULL) {
        STRNCPY(pConfig->pLogStreamName, pLogStreamName, ARRAY_SIZE(pConfig->pLogStreamName) - 1);
    } else {
        SNPRINTF(pConfig->pLogStreamName, ARRAY_SIZE(pConfig->pLogStreamName) - 1, "%s-%s-%llu", pConfig->pChannelName,
                 pConfig->isMaster ? "master" : "viewer", GETTIME() / HUNDREDS_OF_NANOS_IN_A_MILLISECOND);
    }

CleanUp:

    return retStatus;
}

} // namespace Canary
