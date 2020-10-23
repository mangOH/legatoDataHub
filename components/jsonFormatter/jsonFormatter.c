//--------------------------------------------------------------------------------------------------
/**
 * Snapshot formatter producing JSON output.
 *
 * Copyright (C) Sierra Wireless Inc.
 */
//--------------------------------------------------------------------------------------------------
#include "jsonFormatter.h"

#include "interfaces.h"

#include "dataHub.h"
#include "snapshot.h"

/// Filter bitmask for live node detection.
#define LIVE_FILTERS    (SNAPSHOT_FILTER_CREATED | SNAPSHOT_FILTER_NORMAL)
/// Filter bitmask for all possible filters.
#define ALL_FILTERS     (LIVE_FILTERS | SNAPSHOT_FILTER_DELETED)

/// Internal formatter states.
typedef enum
{
    STATE_START = 0,        ///< Beginning of the document.
    STATE_SNAPSHOT_STEP,    ///< Trigger next outer state machine step.
    STATE_NODE_NAME,        ///< Output node name.
    STATE_NODE_OPEN,        ///< Output node opening.
    STATE_NODE_VALUES,      ///< Output node data fields.
    STATE_NODE_VALUE_BODY,  ///< Output node value.
    STATE_MAX               ///< One larger than highest state value.
} JsonFormatterState_t;

/// JSON formatter state.
typedef struct JsonFormatter
{
    snapshot_Formatter_t    base;       ///< Base type containing tree handling callbacks.
    char                    buffer[HUB_MAX_STRING_BYTES + 2];   ///< Buffer for preparing formatted
                                                                ///< output.  Two extra bytes for
                                                                ///< potential quotation marks.
    size_t                  next;       ///< Offset of the next character to send.
    size_t                  available;  ///< Number of bytes available to be sent.
    bool                    needsComma; ///< Does the next item output need to prepend a comma?
    bool                    isRoot;     ///< Is the next node output the root node?
    JsonFormatterState_t    nextState;  ///< Next state to transition to once currently buffered
                                        ///< data is sent.
    le_fdMonitor_Ref_t      monitor;    ///< FD monitor for output stream.
} JsonFormatter_t;

//--------------------------------------------------------------------------------------------------
/*
 * Callback for an internal formatter state machine step.
 */
//--------------------------------------------------------------------------------------------------
typedef void (*JsonFormatterStep_t)
(
    JsonFormatter_t *jsonFormatter ///< Formatter instance.
);

// Forward reference.
static void Step(JsonFormatter_t *jsonFormatter);

//--------------------------------------------------------------------------------------------------
/*
 * Send some data from the buffer to the output stream.
 *
 * @return -1 on error, 0 if all data was sent, or 1 if more data remains to be sent.
 */
//--------------------------------------------------------------------------------------------------
static int SendData
(
    JsonFormatter_t *jsonFormatter, ///< Formatter instance.
    int              stream         ///< Output stream FD.
)
{
    const char  *start = &jsonFormatter->buffer[jsonFormatter->next];
    ssize_t      count;

    if (jsonFormatter->available == 0)
    {
        LE_DEBUG("Nothing to send");
        return 1;
    }

    count = le_fd_Write(stream, start, jsonFormatter->available);
    if (count < 0)
    {
        // An error occurred.
        return -1;
    }
    else if (count < (ssize_t) jsonFormatter->available)
    {
        LE_DEBUG("Sent some (%d bytes): %*s", (int) count, (int) count, start);

        // Didn't send all of the available data.
        jsonFormatter->next += count;
        jsonFormatter->available -= count;
        LE_ASSERT(jsonFormatter->next < sizeof(jsonFormatter->buffer));
        return 1;
    }
    else
    {
        LE_ASSERT(count == (ssize_t) jsonFormatter->available);
        LE_DEBUG("Sent all (%d bytes): %*s", (int) count, (int) count, start);

        // We've sent everything in the buffer.
        jsonFormatter->next = 0;
        jsonFormatter->available = 0;
        le_fdMonitor_Disable(jsonFormatter->monitor, POLLOUT);
        return 0;
    }
}

//--------------------------------------------------------------------------------------------------
/*
 * Handle an FD or manually triggered event on the formatted output stream.
 */
//--------------------------------------------------------------------------------------------------
static void HandleEvents
(
    JsonFormatter_t *jsonFormatter, ///< Formatter instance.
    int              fd,            ///< Output stream file descriptor.
    short            events         ///< FD event bitfield.
)
{
    int status;

    LE_DEBUG("Handling events 0x%04X", events);
    if (events & POLLOUT)
    {
        // Can send more data, so do it.
        status = SendData(jsonFormatter, fd);
        if (status < 0)
        {
            // Error sending data, so abort the snapshot.
            snapshot_End(LE_CLOSED);
            return;
        }
        else if (status == 0)
        {
            // Sent all the available data, take action to get more.
            Step(jsonFormatter);
            return;
        }

        // If we got here there is still more data to send from the buffer, so we will just wait
        // until the next POLLOUT.
    }

    if (events & POLLHUP)
    {
        // Stream was closed for some reason, nothing we can do except terminate the snapshot.
        snapshot_End(LE_CLOSED);
    }
    else if (events & ~POLLOUT)
    {
        // Any other condition is an error, so terminate the snapshot.
        snapshot_End(LE_FAULT);
    }
}

//--------------------------------------------------------------------------------------------------
/*
 * Handle an event on the formatted output stream.
 */
//--------------------------------------------------------------------------------------------------
static void StreamHandler
(
    int     fd,     ///< Output stream file descriptor.
    short   events  ///< Event bitmask.
)
{
    JsonFormatter_t *jsonFormatter = le_fdMonitor_GetContextPtr();

    LE_DEBUG("Stream event");
    HandleEvents(jsonFormatter, fd, events);
}

//--------------------------------------------------------------------------------------------------
/*
 * Handle an explicitly triggered event to write to the formatted output stream.
 */
//--------------------------------------------------------------------------------------------------
static void ExplicitSendHandler
(
    JsonFormatter_t *jsonFormatter, ///< Formatter instance.
    void            *unused         ///< Unused.
)
{
    int fd = le_fdMonitor_GetFd(jsonFormatter->monitor);

    LE_UNUSED(unused);

    LE_DEBUG("Explicit send");
    HandleEvents(jsonFormatter, fd, POLLOUT);
}

//--------------------------------------------------------------------------------------------------
/*
 * (Re)enable events on formatted output stream.
 */
//--------------------------------------------------------------------------------------------------
static void EnableSend
(
    JsonFormatter_t *jsonFormatter ///< Formatter instance.
)
{
    le_fdMonitor_Enable(jsonFormatter->monitor, POLLOUT);

    // Explicitly trigger an attempt to send, since the stream might be sitting ready and therefore
    // not generate a new POLLOUT.
    le_event_QueueFunction((le_event_DeferredFunc_t) &ExplicitSendHandler, jsonFormatter, NULL);
}

//--------------------------------------------------------------------------------------------------
/*
 * Append a string to the current contents of the output buffer.
 */
//--------------------------------------------------------------------------------------------------
static void AppendString
(
    JsonFormatter_t *jsonFormatter, ///< Formatter instance.
    bool             prependComma,  ///< Prepend a comma to the value being added?
    const char      *str            ///< String to append to the buffer.  It is expected that this
                                    ///< is sized so as to avoid overflowing the buffer.
)
{
    // We should never be handed a combined string larger than the Data Hub's maximum string
    // size + 2, so an overflow should not occur and we can assert if it does.
    if (prependComma)
    {
        LE_ASSERT_OK(
            le_utf8_Append(jsonFormatter->buffer, ",", sizeof(jsonFormatter->buffer), NULL)
        );
    }
    LE_ASSERT_OK(le_utf8_Append(jsonFormatter->buffer, str, sizeof(jsonFormatter->buffer), NULL));

    jsonFormatter->available = strlen(jsonFormatter->buffer);
    EnableSend(jsonFormatter);
}

//--------------------------------------------------------------------------------------------------
/*
 * Write a string to the the output buffer.
 */
//--------------------------------------------------------------------------------------------------
static void BufferString
(
    JsonFormatter_t *jsonFormatter, ///< Formatter instance.
    bool             prependComma,  ///< Prepend a comma to the value being added?
    const char      *str            ///< String to write to the buffer.  It is expected that this
                                    ///< is sized so as to avoid overflowing the buffer.
)
{
    LE_ASSERT(jsonFormatter->next == 0);
    LE_ASSERT(jsonFormatter->available == 0);

    jsonFormatter->buffer[0] = '\0';
    AppendString(jsonFormatter, prependComma, str);
}

//--------------------------------------------------------------------------------------------------
/*
 * Write a formatted string to the the output buffer.  It is expected that all inputs will be sized
 * so as to avoid overflowing the buffer.
 */
//--------------------------------------------------------------------------------------------------
static void BufferFormatted
(
    JsonFormatter_t *jsonFormatter, ///< Formatter instance.
    bool             prependComma,  ///< Prepend a comma to the value being added?
    const char      *format,        ///< Format string to use to fill the buffer.
    ...                             ///< Positional parameters for the format string.
)
{
    int     result;
    size_t  offset = 0;
    va_list args;

    LE_ASSERT(jsonFormatter->next == 0);
    LE_ASSERT(jsonFormatter->available == 0);

    if (prependComma)
    {
        jsonFormatter->buffer[0] = ',';
        ++offset;
    }

    va_start(args, format);
    result = vsnprintf(
        jsonFormatter->buffer + offset,
        sizeof(jsonFormatter->buffer) - offset,
        format,
        args
    );
    va_end(args);

    // By design the buffer should always be large enough to accommodate the resulting string, so we
    // can assert here.
    LE_ASSERT(0 < result && result <= (int) (sizeof(jsonFormatter->buffer) - offset));
    jsonFormatter->available = result;
    EnableSend(jsonFormatter);
}

//--------------------------------------------------------------------------------------------------
/*
 * Get the string representation of a boolean.
 *
 * @return String representing the boolean value.
 */
//--------------------------------------------------------------------------------------------------
static const char *Bool2Str
(
    bool value ///< Boolean value.
)
{
    return (value ? "true" : "false");
}

//--------------------------------------------------------------------------------------------------
/*
 * Begin formatting the overall resource tree.
 */
//--------------------------------------------------------------------------------------------------
static void StartTree
(
    snapshot_Formatter_t *formatter ///< Formatter instance.
)
{
    char             path[HUB_MAX_RESOURCE_PATH_BYTES];
    JsonFormatter_t *jsonFormatter = CONTAINER_OF(formatter, JsonFormatter_t, base);

    LE_ASSERT(formatter->filter & ALL_FILTERS);

    LE_DEBUG("Starting tree");

    if (formatter->filter & LIVE_FILTERS)
    {
        // Buffer is sized such that it should never overflow, and the referenced nodes must exist.
        LE_ASSERT(resTree_GetPath(path, sizeof(path), resTree_GetRoot(), snapshot_GetNode()) >= 0);

        BufferFormatted(
            jsonFormatter,
            false,
            "{\"ts\":%lf,\"root\":\"%s\",\"upserted\":",
            snapshot_GetTimestamp(),
            path
        );
    }
    else
    {
        BufferString(jsonFormatter, true, "\"deleted\":");
    }

    // Now we wait for the buffer to drain and call snapshot_Step() when it is done.
    jsonFormatter->isRoot = true;
    jsonFormatter->nextState = STATE_SNAPSHOT_STEP;
}

//--------------------------------------------------------------------------------------------------
/*
 * Begin formatting a resource tree node.
 */
//--------------------------------------------------------------------------------------------------
static void BeginNode
(
    snapshot_Formatter_t *formatter ///< Formatter instance.
)
{
    JsonFormatter_t *jsonFormatter = CONTAINER_OF(formatter, JsonFormatter_t, base);

    LE_ASSERT(formatter->filter & ALL_FILTERS);

    if (jsonFormatter->isRoot)
    {
        // The root node never has additional properties, so move directly to the next step once it
        // is opened.
        LE_DEBUG("Starting root node");
        jsonFormatter->nextState = STATE_NODE_OPEN;
        jsonFormatter->needsComma = false;
        Step(jsonFormatter);
    }
    else
    {
        // This node is a child of another, so open the object key entry and follow up with the node
        // name.
        LE_DEBUG("Starting child node");
        BufferString(jsonFormatter, jsonFormatter->needsComma, "\"");
        jsonFormatter->isRoot = false;
        jsonFormatter->nextState = STATE_NODE_NAME;
    }
}

//--------------------------------------------------------------------------------------------------
/*
 * Write the node name part of the object key to the buffer.
 */
//--------------------------------------------------------------------------------------------------
static void NodeName
(
    JsonFormatter_t *jsonFormatter ///< Formatter instance.
)
{
    const char *name = resTree_GetEntryName(snapshot_GetNode());

    LE_ASSERT(jsonFormatter->base.filter & ALL_FILTERS);

    LE_DEBUG("Output node name: '%s'", name);
    BufferString(jsonFormatter, false, name);
    jsonFormatter->needsComma = false;
    jsonFormatter->nextState = STATE_NODE_OPEN;
}

//--------------------------------------------------------------------------------------------------
/*
 * Write the node opening preamble to the buffer.
 */
//--------------------------------------------------------------------------------------------------
static void NodeOpen
(
    JsonFormatter_t *jsonFormatter ///< Formatter instance.
)
{
    resTree_EntryRef_t  node = snapshot_GetNode();
    admin_EntryType_t   entryType = resTree_GetEntryType(node);

    LE_ASSERT(jsonFormatter->base.filter & ALL_FILTERS);

    LE_DEBUG("Open node contents");

    // Non-root node is preceded by `"<name>` so close that off and open the node object.
    BufferFormatted(jsonFormatter, false, "%s{", (jsonFormatter->isRoot ? "" : "\":"));

    jsonFormatter->isRoot = false;
    jsonFormatter->needsComma = false;

    switch (entryType)
    {
        case ADMIN_ENTRY_TYPE_NAMESPACE:
            // These node types have no additional fields of their own, so proceed to any children
            // by stepping the outer state machine.
            jsonFormatter->nextState = STATE_SNAPSHOT_STEP;
            break;
        case ADMIN_ENTRY_TYPE_INPUT:
        case ADMIN_ENTRY_TYPE_OUTPUT:
        case ADMIN_ENTRY_TYPE_OBSERVATION:
        case ADMIN_ENTRY_TYPE_PLACEHOLDER:
            if ((jsonFormatter->base.filter & LIVE_FILTERS) && snapshot_IsTimely(node))
            {
                // These node types have additional fields of their own, so start the sequence of
                // outputting those.
                jsonFormatter->nextState = STATE_NODE_VALUES;
            }
            else
            {
                // Skip any values on this node, as we are just transiting it to get somewhere more
                // interesting.
                jsonFormatter->nextState = STATE_SNAPSHOT_STEP;
            }
            break;
        default:
            LE_FATAL("Unexpected entry type: %d", entryType);
            break;
    }
}

//--------------------------------------------------------------------------------------------------
/*
 * Write the various node fields to the buffer.
 */
//--------------------------------------------------------------------------------------------------
static void NodeValues
(
    JsonFormatter_t *jsonFormatter ///< Formatter instance.
)
{
    char                buffer[64]; // Sized for stringified boolean or double.
    resTree_EntryRef_t  node = snapshot_GetNode();
    dataSample_Ref_t    sample = resTree_GetCurrentValue(node);
    io_DataType_t       dataType = resTree_GetDataType(node);

    LE_ASSERT(jsonFormatter->base.filter & LIVE_FILTERS);

    LE_DEBUG("Output node values");

    // This function should never be called when the current value is unset.
    LE_ASSERT(sample != NULL);

    BufferFormatted(
        jsonFormatter,
        false,
        "\"type\":%u,\"ts\":%lf,\"mandatory\":%s,\"new\":%s",
        dataType,
        dataSample_GetTimestamp(sample),
        Bool2Str(resTree_IsMandatory(node)),
        Bool2Str(resTree_IsNew(node))
    );
    jsonFormatter->needsComma = true;

    switch (dataType)
    {
        case IO_DATA_TYPE_TRIGGER:
            jsonFormatter->nextState = STATE_SNAPSHOT_STEP;
            break;

        case IO_DATA_TYPE_BOOLEAN:
        case IO_DATA_TYPE_NUMERIC:
            AppendString(jsonFormatter, true, "\"value\":");

            // Buffer is sized such that this should never fail.
            LE_ASSERT_OK(dataSample_ConvertToJson(sample, dataType, buffer, sizeof(buffer)));
            AppendString(jsonFormatter, false, buffer);
            jsonFormatter->nextState = STATE_SNAPSHOT_STEP;
            break;

        case IO_DATA_TYPE_STRING:
        case IO_DATA_TYPE_JSON:
            AppendString(jsonFormatter, true, "\"value\":");
            jsonFormatter->needsComma = false;
            jsonFormatter->nextState = STATE_NODE_VALUE_BODY;
            break;
    }
}

//--------------------------------------------------------------------------------------------------
/*
 * Write the node value to the buffer for string and JSON types.
 */
//--------------------------------------------------------------------------------------------------
static void NodeValueBody
(
    JsonFormatter_t *jsonFormatter ///< Formatter instance.
)
{
    resTree_EntryRef_t  node = snapshot_GetNode();
    dataSample_Ref_t    sample = resTree_GetCurrentValue(node);
    io_DataType_t       dataType = resTree_GetDataType(node);

    LE_ASSERT(jsonFormatter->base.filter & LIVE_FILTERS);

    LE_DEBUG("Output node value body");

    // Don't check comma flag here because this is the value part of a key/value pair and would
    // never have a leading comma.
    LE_ASSERT(!jsonFormatter->needsComma);

    // This function should never be called when the current value is unset.
    LE_ASSERT(sample != NULL);

    // The string/JSON copied in should never be larger than sizeof(jsonFormatter->buffer), so we
    // can assert if this overflows.
    LE_ASSERT_OK(dataSample_ConvertToJson(
        sample,
        dataType,
        jsonFormatter->buffer,
        sizeof(jsonFormatter->buffer)
    ));

    jsonFormatter->available = strlen(jsonFormatter->buffer);
    jsonFormatter->needsComma = true;
    jsonFormatter->nextState = STATE_SNAPSHOT_STEP;
    EnableSend(jsonFormatter);
}

//--------------------------------------------------------------------------------------------------
/*
 * Finish formatting an object.
 */
//--------------------------------------------------------------------------------------------------
static void EndObject
(
    snapshot_Formatter_t *formatter ///< Formatter instance.
)
{
    JsonFormatter_t *jsonFormatter = CONTAINER_OF(formatter, JsonFormatter_t, base);

    LE_ASSERT(formatter->filter & ALL_FILTERS);

    LE_DEBUG("Closing object");
    BufferString(jsonFormatter, false, "}");

    // Now we wait for the buffer to drain and call snapshot_Step() when it is done.
    jsonFormatter->needsComma = true;
    jsonFormatter->nextState = STATE_SNAPSHOT_STEP;
}

//--------------------------------------------------------------------------------------------------
/*
 * Finish formatting a tree.
 */
//--------------------------------------------------------------------------------------------------
static void EndTree
(
    snapshot_Formatter_t *formatter ///< Formatter instance.
)
{
    JsonFormatter_t *jsonFormatter = CONTAINER_OF(formatter, JsonFormatter_t, base);

    LE_ASSERT(formatter->filter & ALL_FILTERS);

    LE_DEBUG("Closing tree");
    jsonFormatter->nextState = STATE_SNAPSHOT_STEP;

    // Determine if a second pass is needed for deleted items.
    formatter->scan = (formatter->filter & LIVE_FILTERS);
    if (formatter->scan)
    {
        formatter->filter = SNAPSHOT_FILTER_DELETED;
        jsonFormatter->needsComma = true;

        // Directly step, as there is nothing to output here.
        Step(jsonFormatter);
    }
    else
    {
        BufferString(jsonFormatter, false, "}");

        // Now we wait for the buffer to drain and call snapshot_Step() when it is done.
        jsonFormatter->needsComma = false;
    }
}

//--------------------------------------------------------------------------------------------------
/*
 * Close and clean up the formatter instance.
 */
//--------------------------------------------------------------------------------------------------
static void Close
(
    snapshot_Formatter_t *formatter ///< Formatter instance.
)
{
    JsonFormatter_t *jsonFormatter = CONTAINER_OF(formatter, JsonFormatter_t, base);

    LE_DEBUG("Closing formatter");
    le_fdMonitor_Delete(jsonFormatter->monitor);
}

//--------------------------------------------------------------------------------------------------
/*
 * Simple wrapper to step the greater state machine.
 */
//--------------------------------------------------------------------------------------------------
static void SnapshotStep
(
    JsonFormatter_t *jsonFormatter ///< Formatter instance.
)
{
    LE_UNUSED(jsonFormatter);

    LE_DEBUG("Stepping snapshot state machine");
    snapshot_Step();
}

//--------------------------------------------------------------------------------------------------
/*
 * Transition the formatter state machine to the next state.
 */
//--------------------------------------------------------------------------------------------------
static void Step
(
    JsonFormatter_t *jsonFormatter ///< Formatter instance.
)
{
    const JsonFormatterStep_t steps[STATE_MAX - 1] =
    {
        &SnapshotStep,  // STATE_SNAPSHOT_STEP
        &NodeName,      // STATE_NODE_NAME
        &NodeOpen,      // STATE_NODE_OPEN
        &NodeValues,    // STATE_NODE_VALUES
        &NodeValueBody  // STATE_NODE_VALUE_BODY
    };
#if LE_DEBUG_ENABLED
    const char *stepNames[STATE_MAX - 1] =
    {
        "STATE_SNAPSHOT_STEP",
        "STATE_NODE_NAME",
        "STATE_NODE_OPEN",
        "STATE_NODE_VALUES",
        "STATE_NODE_VALUE_BODY"
    };
#endif /* end LE_DEBUG_ENABLED */

    if (jsonFormatter->nextState == STATE_START)
    {
        // If things haven't started yet, just wait until they do.
        return;
    }

    LE_ASSERT(jsonFormatter->nextState > STATE_START && jsonFormatter->nextState < STATE_MAX);
    LE_DEBUG("JSON formatter transition: -> %s", stepNames[jsonFormatter->nextState - 1]);
    steps[jsonFormatter->nextState - 1](jsonFormatter);
}

//--------------------------------------------------------------------------------------------------
/*
 * Initialise and return the JSON snapshot formatter instance.
 *
 * @return LE_OK (the JSON formatter initialisation does not generate any errors).
 */
//--------------------------------------------------------------------------------------------------
le_result_t GetJsonSnapshotFormatter
(
    uint32_t                  flags,    ///< [IN]  Flags that were passed to the snapshot request.
    int                       stream,   ///< [IN]  File descriptor to write formatted output to.
    snapshot_Formatter_t    **formatter ///< [OUT] Returned formatter instance.
)
{
    static JsonFormatter_t jsonFormatter =
    {
        {
            .startTree  = &StartTree,
            .beginNode  = &BeginNode,
            .endNode    = &EndObject,
            .endTree    = &EndTree,
            .close      = &Close
        }
    };

    LE_UNUSED(flags);

    LE_ASSERT(formatter != NULL);
    *formatter = &jsonFormatter.base;

    memset(jsonFormatter.buffer, 0, sizeof(jsonFormatter.buffer));
    jsonFormatter.next          = 0;
    jsonFormatter.available     = 0;
    jsonFormatter.needsComma    = false;
    jsonFormatter.isRoot        = true;
    jsonFormatter.nextState     = STATE_START;

    jsonFormatter.base.filter   = LIVE_FILTERS;
    jsonFormatter.base.scan     = true;

    LE_DEBUG("JSON formatter transition: -> STATE_START");

    // Configure event handler for outputting formatted data.
    jsonFormatter.monitor = le_fdMonitor_Create(
                                "JsonSnapshotStream",
                                stream,
                                &StreamHandler,
                                POLLOUT
                            );
    le_fdMonitor_SetContextPtr(jsonFormatter.monitor, &jsonFormatter);
    le_fdMonitor_Disable(jsonFormatter.monitor, POLLOUT);

    return LE_OK;
}

/// Component initialisation.
COMPONENT_INIT
{
    // Do nothing.
}
