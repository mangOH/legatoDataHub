#include "legato.h"
#include "interfaces.h"

// Test has apparently been broken for a while, but due to a binding error the app never actually
// started up enough to run and discover this.
// TODO: Fix and re-enable this test.
#define ENABLE_DUMMY_ACTUATOR_TEST 0

#define COUNTER_NAME "counter/value"

#define EPHEMERAL_A_NAME "ephemeral1/value"
#define EPHEMERAL_B_NAME "ephemeral2/value"

#if ENABLE_DUMMY_ACTUATOR_TEST

// Here we track how many times the TreeChangeHandler callback
// has been called for each path / type / operation
static int dummyInputCreated = 0;
static int dummyInputRemoved = 0;
static int dummyOutputCreated = 0;
static int dummyOutputRemoved = 0;
static int dummyObservationCreated = 0;
static int dummyObservationRemoved = 0;

static admin_ResourceTreeChangeHandlerRef_t treeChangeHandlerRef;

//--------------------------------------------------------------------------------------------------
/**
 * Call-back function called when the tree has changed
 */
//--------------------------------------------------------------------------------------------------
static void TreeChangeHandler
(
    const char *path,
    admin_EntryType_t entryType,
    admin_ResourceOperationType_t operationType,
    void* contextPtr
)
{
    LE_UNUSED(contextPtr);

    LE_INFO("tree change = %s %d %d", path, entryType, operationType);

    if (strncmp(path, "/app/actuator/dummy/input", 25) == 0)
    {
        if (entryType == ADMIN_ENTRY_TYPE_INPUT)
        {
            if (operationType == ADMIN_RESOURCE_ADDED)
            {
                LE_INFO("Dummy input created");
                dummyInputCreated++;
                return;
            }
            else
            {
                LE_INFO("Dummy input removed");
                dummyInputRemoved++;
                return;
            }
        }
    }
    if (strncmp(path, "/app/actuator/dummy/output", 25) == 0)
    {
        if (entryType == ADMIN_ENTRY_TYPE_OUTPUT)
        {
            if (operationType == ADMIN_RESOURCE_ADDED)
            {
                LE_INFO("Dummy output created");
                dummyOutputCreated++;
                return;
            }
            else
            {
                LE_INFO("Dummy output removed");
                dummyOutputRemoved++;
                return;
            }
        }
    }
    if (strncmp(path, "/obs/dummy", 10) == 0)
    {
        if (entryType == ADMIN_ENTRY_TYPE_OBSERVATION)
        {
            if (operationType == ADMIN_RESOURCE_ADDED)
            {
                LE_INFO("Dummy observation created");
                dummyObservationCreated++;
                return;
            }
            else
            {
                LE_INFO("Dummy observation removed");
                dummyObservationRemoved++;
                return;
            }
        }
    }
    LE_ASSERT(false);
}

//--------------------------------------------------------------------------------------------------
/**
 * Call-back function called when an update is received from the Data Hub for the "counter" value.
 */
//--------------------------------------------------------------------------------------------------
static void JsonCounterUpdateHandler
(
    double timestamp,
    const char* value,
    void* contextPtr    ///< not used
)
//--------------------------------------------------------------------------------------------------
{
    LE_UNUSED(contextPtr);
    LE_INFO("counter = %s (timestamped %lf)", value, timestamp);
}


//--------------------------------------------------------------------------------------------------
/**
 * Call-back function called when an update is received from the Data Hub for the "counter" value.
 */
//--------------------------------------------------------------------------------------------------
static void NumericCounterUpdateHandler
(
    double timestamp,
    double value,
    void* contextPtr    ///< not used
)
//--------------------------------------------------------------------------------------------------
{
    LE_UNUSED(contextPtr);

    LE_INFO("counter = %lf (timestamped %lf)", value, timestamp);

    // Every 5th push, do some additional testing.
    if (fmod(value, 5.0) == 0)
    {
        LE_INFO("Running create/delete tests");

        // Create the Output with different type and units and expect LE_DUPLICATE.
        le_result_t result = io_CreateOutput(COUNTER_NAME, IO_DATA_TYPE_STRING, "count");
        LE_ASSERT(result == LE_DUPLICATE);
        result = io_CreateOutput(COUNTER_NAME, IO_DATA_TYPE_NUMERIC, "s");
        LE_ASSERT(result == LE_DUPLICATE);
        result = io_CreateInput(COUNTER_NAME, IO_DATA_TYPE_NUMERIC, "count");
        LE_ASSERT(result == LE_DUPLICATE);

        // Create the Output with same type and units and expect LE_OK.
        result = io_CreateOutput(COUNTER_NAME, IO_DATA_TYPE_NUMERIC, "count");
        LE_ASSERT(result == LE_OK);

        // Delete the Input and re-create it.
        io_DeleteResource(COUNTER_NAME);
        result = io_CreateOutput(COUNTER_NAME, IO_DATA_TYPE_NUMERIC, "count");
        LE_ASSERT(result == LE_OK);
        io_AddNumericPushHandler(COUNTER_NAME, NumericCounterUpdateHandler, NULL);
        io_JsonPushHandlerRef_t jsonHandlerRef = io_AddJsonPushHandler(COUNTER_NAME,
                                                                       JsonCounterUpdateHandler,
                                                                       NULL);
        io_RemoveJsonPushHandler(jsonHandlerRef);
        io_AddJsonPushHandler(COUNTER_NAME, JsonCounterUpdateHandler, NULL);
    }
}

void AssertTimer
(
    le_timer_Ref_t timerRef
)
{
    LE_UNUSED(timerRef);

    // Check that the ResourceTreeChangeHandler has been called with the
    // correct values, the correct number of times
    LE_ASSERT(dummyInputCreated == 1);
    LE_ASSERT(dummyInputRemoved == 1);
    LE_ASSERT(dummyOutputCreated == 1);
    LE_ASSERT(dummyOutputRemoved == 1);
    LE_ASSERT(dummyObservationCreated == 1);
    LE_ASSERT(dummyObservationRemoved == 1);

    admin_RemoveResourceTreeChangeHandler(treeChangeHandlerRef);
}

static void PrepDummyActuatorTest
(
    void
)
{
    le_result_t result;

    // This will be received from the Data Hub.
    result = io_CreateOutput(COUNTER_NAME, IO_DATA_TYPE_NUMERIC, "count");
    LE_ASSERT(result == LE_OK);

    // Register for notification of updates to the counter value.
    io_AddNumericPushHandler(COUNTER_NAME, NumericCounterUpdateHandler, NULL);
    io_AddJsonPushHandler(COUNTER_NAME, JsonCounterUpdateHandler, NULL);
    io_AddStringPushHandler(COUNTER_NAME, JsonCounterUpdateHandler, NULL);

    // Get notified when we add / remove resources
    treeChangeHandlerRef = admin_AddResourceTreeChangeHandler(TreeChangeHandler, NULL);

    // Create / remove some resources to test the tree-change-handler
    result = io_CreateInput("dummy/input", IO_DATA_TYPE_NUMERIC, "");
    LE_ASSERT(result == LE_OK);
    // Same input
    result = io_CreateInput("dummy/input", IO_DATA_TYPE_NUMERIC, "");
    LE_ASSERT(result == LE_OK);
    // Remove
    io_DeleteResource("dummy/input");
    io_DeleteResource("dummy/input");

    result = io_CreateOutput("dummy/output", IO_DATA_TYPE_STRING, "");
    LE_ASSERT(result == LE_OK);
    // Same output
    result = io_CreateOutput("dummy/output", IO_DATA_TYPE_STRING, "");
    LE_ASSERT(result == LE_OK);
    // When "admin" settings are applied to a Resource that is deleted, the
    // Resource is converted into a Placeholder. We still expect to be notified
    // of the removal however.
    admin_SetStringDefault("/app/actuator/dummy/output", "A Default Value");
    // Remove
    io_DeleteResource("dummy/output");
    io_DeleteResource("dummy/output");
    // Check that it is a placeholder now
    admin_EntryType_t outputEntry = admin_GetEntryType("/app/actuator/dummy/output");
    LE_ASSERT(outputEntry == ADMIN_ENTRY_TYPE_PLACEHOLDER);

    // Observation
    result = admin_CreateObs("dummy");
    LE_ASSERT(result == LE_OK);
    admin_DeleteObs("dummy");

    // Run a timer. When it triggers, we will check the appropriate callbacks were called
    le_timer_Ref_t assertionTimer = le_timer_Create("Run Asserts Timer");
    le_timer_SetHandler (assertionTimer, AssertTimer);

    le_timer_SetMsInterval(assertionTimer,2000);
    le_timer_Start(assertionTimer);
}

#endif /* end ENABLE_DUMMY_ACTUATOR_TEST */

void LifecycleTimer
(
    le_timer_Ref_t timerRef
)
{
    double              timestamp;
    le_result_t         result;
    static unsigned int count = 0;

    LE_UNUSED(timerRef);

    ++count;
    LE_INFO("Cycling resource lives (%u)", count);

    if (count % 2 == 0)
    {
        if (io_GetTimestamp(EPHEMERAL_A_NAME, &timestamp) == LE_NOT_FOUND)
        {
            result = io_CreateOutput(EPHEMERAL_A_NAME, IO_DATA_TYPE_BOOLEAN, "");
            LE_ASSERT(result == LE_OK);
            io_PushBoolean(EPHEMERAL_A_NAME, IO_NOW, true);
        }
        else
        {
            io_DeleteResource(EPHEMERAL_A_NAME);
        }
    }
    else if (count % 3 == 0)
    {
        if (io_GetTimestamp(EPHEMERAL_B_NAME, &timestamp) == LE_NOT_FOUND)
        {
            result = io_CreateOutput(EPHEMERAL_B_NAME, IO_DATA_TYPE_BOOLEAN, "");
            LE_ASSERT(result == LE_OK);
            io_PushBoolean(EPHEMERAL_B_NAME, IO_NOW, false);
        }
        else
        {
            io_DeleteResource(EPHEMERAL_B_NAME);
        }
    }
}

COMPONENT_INIT
{
    LE_INFO("Starting actuator...");

#if ENABLE_DUMMY_ACTUATOR_TEST
    PrepDummyActuatorTest();
#endif /* end ENABLE_DUMMY_ACTUATOR_TEST */

    // Create timer to periodically add and remove resources, for deletion tracking.
    le_timer_Ref_t lifecycleTimer = le_timer_Create("Add/Remove Timer");
    le_timer_SetHandler(lifecycleTimer, &LifecycleTimer);

    le_timer_SetMsInterval(lifecycleTimer, 10000);
    le_timer_SetRepeat(lifecycleTimer, 0);
    le_timer_Start(lifecycleTimer);
}
