//--------------------------------------------------------------------------------------------------
/**
 * Implementation of the snapshot portion of the Query API.
 *
 * Copyright (C) Sierra Wireless Inc.
 */
//--------------------------------------------------------------------------------------------------
#include "snapshot.h"

#include "interfaces.h"

#include "dataHub.h"
#include "jsonFormatter.h"

/// Upper limit on the number of passes through the tree that can be requested by a formatter.
#define MAX_PASSES      10

/// FIFO path for formatted data streaming.
#define SNAPSHOT_FIFO   "/tmp/datahub_snapshot_fifo"

/// Default depth of resource tree entries.  This can be overridden in the .cdef.
#define DEFAULT_NODE_PARENT_POOL_SIZE 10

/// States of the snapshot state machine.
typedef enum
{
    STATE_NODE_BEGIN = 0,   ///< Begin processing a new tree node.
    STATE_NODE_CHILDREN,    ///< Begin processing children of a tree node.
    STATE_NODE_END,         ///< Finish processing the current tree node.
    STATE_NODE_SIBLING,     ///< Begin processing the next sibling of a tree node.
    STATE_TREE_END,         ///< Done processing all tree nodes.
    STATE_MAX               ///< One larger than highest state value.
} SnapshotState_t;

/// Active snapshot state structure.
typedef struct
{
    int sink;   ///< FIFO handle to write formatted snapshot to.
    int source; ///< FIFO handle to read formatted snapshot from (passed to remote side).

    uint32_t                 flags;     ///< Snapshot flags.
    double                   since;     ///< Only include updates newer than this time stamp.
    snapshot_Formatter_t    *formatter; ///< Formatter to use to write snapshot data.
    double                   timestamp; ///< When the snapshot operation was started.
    unsigned int             passes;    ///< Number of passes conducted through the resource tree.

    query_HandleSnapshotResultFunc_t     callback;  ///< Callback to invoke to indicate end of
                                                    ///< snapshot, or error.
    void                                *context;   ///< User context for result callback.

    SnapshotState_t          nextState; ///< Next snapshot processing state to transition to.
    resTree_EntryRef_t       nodeRef;   ///< Active resource tree node.
    resTree_EntryRef_t       rootRef;   ///< Root of the relevant portion of the tree.
} Snapshot_t;

/// Node parent stack entry.
typedef struct
{
    le_sls_Link_t       link;       ///< Link to next list entry.
    resTree_EntryRef_t  nodeRef;    ///< Stacked parent node.
} Parent_t;

/// Keep track of deleted resources?
static bool AreDeletionsTracked;

/// Is a snapshot request currently in progress?
static bool IsRunning;

/// Active snapshot state.
static Snapshot_t Snapshot;

/// Pool of node parent references.
static le_mem_PoolRef_t NodeParentPool = NULL;
LE_MEM_DEFINE_STATIC_POOL(NodeParentPool, DEFAULT_NODE_PARENT_POOL_SIZE, sizeof(Parent_t));

/// Stack of node parent references.
static le_sls_List_t Parents = LE_SLS_LIST_DECL_INIT;

//--------------------------------------------------------------------------------------------------
/*
 *  Obtain the flags for the current snapshot operation.
 *
 *  @return Flags for the current snapshot.
 */
//--------------------------------------------------------------------------------------------------
uint32_t snapshot_GetFlags
(
    void
)
{
    LE_ASSERT(IsRunning);
    return Snapshot.flags;
}

//--------------------------------------------------------------------------------------------------
/*
 *  Obtain the file stream to write formatted output to for the current snapshot operation.
 *
 *  @return File descriptor for the formatted output stream.
 */
//--------------------------------------------------------------------------------------------------
int snapshot_GetStream
(
    void
)
{
    LE_ASSERT(IsRunning);
    return Snapshot.sink;
}

//--------------------------------------------------------------------------------------------------
/*
 *  Obtain the resource tree node currently under consideration.
 *
 *  @return Node reference.
 */
//--------------------------------------------------------------------------------------------------
resTree_EntryRef_t snapshot_GetNode
(
    void
)
{
    LE_ASSERT(IsRunning);
    LE_ASSERT(Snapshot.nodeRef != NULL);
    return Snapshot.nodeRef;
}

//--------------------------------------------------------------------------------------------------
/*
 *  Obtain the time stamp of the start of the snapshot operation.
 *
 *  @return File descriptor for the formatted output stream.
 */
//--------------------------------------------------------------------------------------------------
double snapshot_GetTimestamp
(
    void
)
{
    LE_ASSERT(IsRunning);
    return Snapshot.timestamp;
}

//--------------------------------------------------------------------------------------------------
/*
 * Determine if the given node is within the time window of interest for the current snapshot
 * operation.
 */
//--------------------------------------------------------------------------------------------------
bool snapshot_IsTimely
(
    resTree_EntryRef_t nodeRef ///< Node reference.
)
{
    LE_ASSERT(IsRunning);
    return (resTree_GetLastModified(nodeRef) > Snapshot.since);
}

//--------------------------------------------------------------------------------------------------
/*
 *  Push a parent node reference onto the stack as we descend to a child.
 */
//--------------------------------------------------------------------------------------------------
static void PushParent
(
    resTree_EntryRef_t parentRef    ///< New parent to push onto the stack.
)
{
    Parent_t *entry = le_mem_Alloc(NodeParentPool);
    entry->link = LE_SLS_LINK_INIT;
    entry->nodeRef = parentRef;

    le_sls_Stack(&Parents, &entry->link);
}

//--------------------------------------------------------------------------------------------------
/*
 *  Pop a parent node reference off the stack as we back out of a child node.
 *
 *  @return The parent node reference, or NULL if no entries were present on the stack.
 */
//--------------------------------------------------------------------------------------------------
static resTree_EntryRef_t PopParent
(
    void
)
{
    le_sls_Link_t       *link = le_sls_Pop(&Parents);
    Parent_t            *entry;
    resTree_EntryRef_t   parentRef = NULL;

    if (link != NULL)
    {
        entry = CONTAINER_OF(link, Parent_t, link);
        parentRef = entry->nodeRef;
        le_mem_Release(entry);
    }
    return parentRef;
}

//--------------------------------------------------------------------------------------------------
/*
 * Begin processing a resource tree node.  This will cumulatively perform a depth-first traversal of
 * the tree from the current node and invoke the formatter as it goes.
 */
//--------------------------------------------------------------------------------------------------
static void NodeBegin
(
    void *unused1,  ///< [IN] Unused parameter.
    void *unused2   ///< [IN] Unused parameter.
)
{
    resTree_EntryRef_t childRef = NULL;

    LE_UNUSED(unused1);
    LE_UNUSED(unused2);

    LE_DEBUG("Handling node beginning");

    if (resTree_IsRelevant(Snapshot.nodeRef))
    {
        if (!resTree_IsDeleted(Snapshot.nodeRef))
        {
            childRef = resTree_GetFirstChildEx(
                            Snapshot.nodeRef,
                            Snapshot.formatter->filter & SNAPSHOT_FILTER_DELETED);
        }
        Snapshot.nextState = (childRef == NULL ? STATE_NODE_END : STATE_NODE_CHILDREN);
        Snapshot.formatter->beginNode(Snapshot.formatter);
    }
    else
    {
        Snapshot.nextState = STATE_NODE_END;
        snapshot_Step();
    }
}

//--------------------------------------------------------------------------------------------------
/*
 * Begin processing the children of a tree node.
 */
//--------------------------------------------------------------------------------------------------
static void NodeChildren
(
    void *unused1,  ///< [IN] Unused parameter.
    void *unused2   ///< [IN] Unused parameter.
)
{
    LE_UNUSED(unused1);
    LE_UNUSED(unused2);

    LE_DEBUG("Handling node children");

    PushParent(Snapshot.nodeRef);
    Snapshot.nodeRef = resTree_GetFirstChildEx(
                            Snapshot.nodeRef,
                            Snapshot.formatter->filter & SNAPSHOT_FILTER_DELETED);

    // We should only get here if we already checked for children.
    LE_ASSERT(Snapshot.nodeRef != NULL);

    // No additional formatting here, so directly transition the state machine.
    Snapshot.nextState = STATE_NODE_BEGIN;
    snapshot_Step();
}

//--------------------------------------------------------------------------------------------------
/*
 * End processing a tree node.
 */
//--------------------------------------------------------------------------------------------------
static void NodeEnd
(
    void *unused1,  ///< [IN] Unused parameter.
    void *unused2   ///< [IN] Unused parameter.
)
{
    LE_UNUSED(unused1);
    LE_UNUSED(unused2);

    LE_DEBUG("Handling node end");

    Snapshot.nextState = STATE_NODE_SIBLING;
    if (resTree_IsRelevant(Snapshot.nodeRef))
    {
        Snapshot.formatter->endNode(Snapshot.formatter);
        resTree_ClearNewness(Snapshot.nodeRef);
    }
    else
    {
        snapshot_Step();
    }
}

//--------------------------------------------------------------------------------------------------
/*
 * Move to the next sibling of the current tree node.
 */
//--------------------------------------------------------------------------------------------------
static void NodeSibling
(
    void *unused1,  ///< [IN] Unused parameter.
    void *unused2   ///< [IN] Unused parameter.
)
{
    resTree_EntryRef_t nodeRef = Snapshot.nodeRef;

    LE_UNUSED(unused1);
    LE_UNUSED(unused2);

    LE_DEBUG("Handling node sibling");

    Snapshot.nodeRef = resTree_GetNextSiblingEx(
                            nodeRef,
                            Snapshot.formatter->filter & SNAPSHOT_FILTER_DELETED);
    if ((Snapshot.flags & QUERY_SNAPSHOT_FLAG_FLUSH_DELETIONS) && resTree_IsDeleted(nodeRef))
    {
        // If we are flushing as we go, remove the deleted node.
        le_mem_Release(nodeRef);
    }

    if (Snapshot.nodeRef == NULL)
    {
        // No more siblings, try looking for a parent.
        Snapshot.nodeRef = PopParent();
        if (Snapshot.nodeRef == NULL)
        {
            // No more parents, we are done.
            Snapshot.nextState = STATE_TREE_END;
            Snapshot.formatter->endTree(Snapshot.formatter);
            return;
        }
        else
        {
            // We have a parent, so back out to its level.
            Snapshot.nextState = STATE_NODE_END;
        }
    }
    else
    {
        // There is another sibling, move to it.
        Snapshot.nextState = STATE_NODE_BEGIN;
    }

    // No formatting to do, so directly transition to the next state.
    snapshot_Step();
}

//--------------------------------------------------------------------------------------------------
/*
 * Recursively set the relevance flag for the specified node and its children.
 */
//--------------------------------------------------------------------------------------------------
static void UpdateRelevance
(
    resTree_EntryRef_t nodeRef, ///< Node reference.
    uint32_t           filter   ///< Filter bitmask for node relevence beyond just timestamp.
)
{
    bool                relevant = false;
    bool                timely = false;
    resTree_EntryRef_t  childRef = resTree_GetFirstChildEx(nodeRef, true);

    if (nodeRef == Snapshot.rootRef)
    {
        // Always include the root node.
        relevant = true;
    }
    else if ((filter & SNAPSHOT_FILTER_CREATED) && resTree_IsNew(nodeRef))
    {
        relevant = true;
    }
    else if ((filter & SNAPSHOT_FILTER_DELETED) && resTree_IsDeleted(nodeRef))
    {
        relevant = true;
    }
    else if (filter & (SNAPSHOT_FILTER_CREATED | SNAPSHOT_FILTER_NORMAL))
    {
        timely = snapshot_IsTimely(nodeRef);
        relevant = timely;
    }
    LE_DEBUG("Node %s is %srelevant on its own merit",
        resTree_GetEntryName(nodeRef), (relevant ? "" : "ir"));

    // Regardless of this node's timeliness, it is considered relevant if at least one child node is
    // relevant, in order to provide a "here to there" path.
    while (childRef != NULL)
    {
        UpdateRelevance(childRef, filter);
        relevant = resTree_IsRelevant(childRef) || relevant;
        childRef = resTree_GetNextSiblingEx(childRef, true);
    }

    LE_DEBUG("Node %s is cumulatively %srelevant",
        resTree_GetEntryName(nodeRef), (relevant ? "" : "ir"));
    resTree_SetRelevance(nodeRef, relevant);

    // Timeliness implies relevance, but the reverse is not true.  Ensure that this condition is
    // met.
    LE_ASSERT(!(timely && !relevant));
}

//--------------------------------------------------------------------------------------------------
/*
 * Initiate a pass through the resource tree.
 */
//--------------------------------------------------------------------------------------------------
static void StartPass
(
    void
)
{
    LE_DEBUG("Starting pass %u", Snapshot.passes);

    Snapshot.nextState = STATE_NODE_BEGIN;
    Snapshot.nodeRef = Snapshot.rootRef;
    UpdateRelevance(Snapshot.nodeRef, Snapshot.formatter->filter);
    Snapshot.formatter->startTree(Snapshot.formatter);
    ++Snapshot.passes;
}

//--------------------------------------------------------------------------------------------------
/*
 * End the snapshot state machine.
 */
//--------------------------------------------------------------------------------------------------
static void TreeEnd
(
    void *unused1,  ///< [IN] Unused parameter.
    void *unused2   ///< [IN] Unused parameter.
)
{
    LE_UNUSED(unused1);
    LE_UNUSED(unused2);

    LE_DEBUG("Handling tree end");

    // Should never get here with a parent still on the stack.
    LE_ASSERT(PopParent() == NULL);

    // A formatter may ask for another pass through the tree, or we may be done.
    if (Snapshot.formatter->scan && Snapshot.passes < MAX_PASSES)
    {
        StartPass();
    }
    else if (Snapshot.passes >= MAX_PASSES)
    {
        snapshot_End(LE_OUT_OF_RANGE);
    }
    else
    {
        snapshot_End(LE_OK);
    }
}

//--------------------------------------------------------------------------------------------------
/*
 * Transition the tree-walking snapshot state machine to the next state.
 */
//--------------------------------------------------------------------------------------------------
void snapshot_Step
(
    void
)
{
    const le_event_DeferredFunc_t steps[STATE_MAX] =
    {
        &NodeBegin,     // STATE_NODE_BEGIN
        &NodeChildren,  // STATE_NODE_CHILDREN
        &NodeEnd,       // STATE_NODE_END
        &NodeSibling,   // STATE_NODE_SIBLING
        &TreeEnd        // STATE_TREE_END
    };
#if LE_DEBUG_ENABLED
    const char *stepNames[STATE_MAX] =
    {
        "STATE_NODE_BEGIN",
        "STATE_NODE_CHILDREN",
        "STATE_NODE_END",
        "STATE_NODE_SIBLING",
        "STATE_TREE_END"
    };
#endif /* end LE_DEBUG_ENABLED */

    LE_ASSERT(Snapshot.nextState >= STATE_NODE_BEGIN && Snapshot.nextState < STATE_MAX);
    LE_DEBUG("Snapshot transition: -> %s", stepNames[Snapshot.nextState]);
    le_event_QueueFunction(steps[Snapshot.nextState], NULL, NULL);
}

//--------------------------------------------------------------------------------------------------
/*
 * Invoke the result callback to provide final snapshot job status to the user.
 */
//--------------------------------------------------------------------------------------------------
static void InvokeResultCallback
(
    void    *status,    ///< [IN] User result callback.
    void    *unused     ///< [IN] Unused parameter.
)
{
    LE_UNUSED(unused);

    LE_DEBUG("Invoking result callback");
    if (Snapshot.callback != NULL)
    {
        Snapshot.callback((le_result_t) status, Snapshot.context);
    }
}

//--------------------------------------------------------------------------------------------------
/*
 * Remove all existing deletion records.
 */
//--------------------------------------------------------------------------------------------------
static void FlushDeletionRecords
(
    resTree_EntryRef_t nodeRef ///< Node to flush beneath.
)
{
    resTree_EntryRef_t childRef;
    resTree_EntryRef_t nextRef = resTree_GetFirstChildEx(nodeRef, true);

    while (nextRef != NULL)
    {
        childRef = nextRef;
        nextRef = resTree_GetNextSiblingEx(childRef, true);

        FlushDeletionRecords(childRef);
        if (resTree_IsDeleted(childRef))
        {
            le_mem_Release(childRef);
        }
    }
}

//--------------------------------------------------------------------------------------------------
/*
 * End snapshot and tidy up state.
 *
 * Close the formatter and file handles, and queue up the result callback to provide the status to
 * the user.
 */
//--------------------------------------------------------------------------------------------------
void snapshot_End
(
    le_result_t status  ///< [IN] Result of the snapshot request.
)
{
    LE_DEBUG("Ending snapshot with status %s", LE_RESULT_TXT(status));

    if (Snapshot.formatter != NULL)
    {
        Snapshot.formatter->close(Snapshot.formatter);
        Snapshot.formatter = NULL;
    }
    if (Snapshot.sink >= 0)
    {
        le_fd_Close(Snapshot.sink);
    }
    if (Snapshot.source >= 0)
    {
        le_fd_Close(Snapshot.source);
    }

    // Resume resource tree updates.
    resTree_EndUpdate();
    IsRunning = false;

    le_event_QueueFunction(&InvokeResultCallback, (void *) (uintptr_t) status, NULL);
}

//--------------------------------------------------------------------------------------------------
/*
 * Initialise the pipe/FIFO for passing back formatted data.
 */
//--------------------------------------------------------------------------------------------------
static inline void InitPipe
(
    void
)
#if LE_CONFIG_RTOS
{
    Snapshot.sink = le_fd_Open(SNAPSHOT_FIFO, O_WRONLY | O_NONBLOCK);
    Snapshot.source = le_fd_Open(SNAPSHOT_FIFO, O_RDONLY | O_NONBLOCK);
}
#else /* not LE_CONFIG_RTOS */
{
    int fds[2] = { -1, -1 };

    // We don't bother checking the return value here because the FDs will be checked as soon as we
    // return.
    pipe2(fds, O_NONBLOCK);
    Snapshot.sink = fds[1];
    Snapshot.source = fds[0];
}
#endif /* end not LE_CONFIG_RTOS */

//--------------------------------------------------------------------------------------------------
/*
 * Capture a snapshot of the resource tree.
 *
 * The snapshot will be of the portion of the tree rooted at the given path, and include all values
 * which have changed since the provided time stamp.  The response will be encoded according to the
 * specified formatter and streamed back to the requester via the provided file handle.  The end of
 * data or an error will be indicated by invoking the provided callback with a result code.
 *
 * If deletions are being tracked (@see query_TrackDeletions), then information about deleted
 * resources will be included in the snapshot if the formatter includes it.  The
 * SNAPSHOT_FLAG_FLUSH_DELETIONS flag may be passed to flush and reset the current deletion tracking
 * as part of the snapshot operation.  Doing this would mean that deletion information would only be
 * available back to the time stamp of the last snapshot.
 */
//--------------------------------------------------------------------------------------------------
void query_TakeSnapshot
(
    uint32_t     format,    ///< [IN] Snapshot output data format.
    uint32_t     flags,     ///< [IN] Flags controlling the snapshot action.
    const char  *path,      ///< [IN] Tree path to use as the root.
    double       since,     ///< [IN] Request only values that have changed since this time (in s).
                            ///<      Use BEGINNING_OF_TIME to request the full tree.
    query_HandleSnapshotResultFunc_t callback,  ///< [IN]  Completion callback to indicate the end
                                                ///<       of the streamed snapshot, or an error.
    void        *contextPtr,    ///< [IN]  User context for the completion callback.
    int         *snapshotStream ///< [OUT] File descriptor to which the encoded snapshot data will
                                ///<       be streamed.
)
{
    le_clk_Time_t   currentTime;
    le_result_t     status = LE_OK;

    LE_ASSERT(callback != NULL);
    LE_ASSERT(path != NULL);
    LE_ASSERT(snapshotStream != NULL);

    *snapshotStream = -1;
    if (IsRunning)
    {
        // Already running, so indicate we are busy.
        status = LE_BUSY;
        le_event_QueueFunction(&InvokeResultCallback, (void *) (uintptr_t) status, NULL);
        return;
    }
    IsRunning = true;

    // Pause updates to the tree while the snapshot scan runs.
    resTree_StartUpdate();

    memset(&Snapshot, 0, sizeof(Snapshot));
    Snapshot.callback = callback;
    Snapshot.context = contextPtr;
    InitPipe();
    if (Snapshot.sink < 0 || Snapshot.source < 0)
    {
        status = LE_CLOSED;
        goto end;
    }

    // NOTE: In the future it may be possible to plug in more formatters, but for now this is a
    //       fixed list.
    switch (format)
    {
        case QUERY_SNAPSHOT_FORMAT_JSON:
            status = GetJsonSnapshotFormatter(flags, Snapshot.sink, &Snapshot.formatter);
            break;
        // case QUERY_SNAPSHOT_FORMAT_OCTAVE:
        //     status = GetOctaveSnapshotFormatter(flags, Snapshot.sink, &Snapshot.formatter);
        //     break;
        default:
            status = LE_NOT_IMPLEMENTED;
            break;
    }
    if (Snapshot.formatter == NULL)
    {
        goto end;
    }

    Snapshot.rootRef = resTree_FindEntryAtAbsolutePath(path);
    if (Snapshot.rootRef == NULL)
    {
        status = LE_NOT_FOUND;
        goto end;
    }

    Snapshot.flags = flags;
    Snapshot.since = since;
    *snapshotStream = Snapshot.source;

    currentTime = le_clk_GetAbsoluteTime();
    Snapshot.timestamp = (((double) currentTime.usec) / 1000000) + currentTime.sec;

    if (Snapshot.formatter->scan)
    {
        StartPass();
    }
    else
    {
        status = LE_UNSUPPORTED;
    }

end:
    if (status != LE_OK)
    {
        // End the snapshot request and unlock the tree if it is locked.
        snapshot_End(status);
    }
}

//--------------------------------------------------------------------------------------------------
/*
 * Control whether deletion records should be maintained within the Data Hub.
 *
 * Turning on deletion tracking will cause some metadata to be retained for each deleted resource.
 * This metadata will be supplied to the formatter when a snapshot is requested so that nodes which
 * have disappeared from the tree can be recorded appropriately.  Because this metadata will
 * gradually accumulate over time as nodes are removed, there are two ways of requesting that the
 * deletion data be flushed.  The first is to just disable and then reenable tracking using this
 * function.  The second is to pass the SNAPSHOT_FLAG_FLUSH_DELETIONS flag when requesting a
 * snapshot.
 */
//--------------------------------------------------------------------------------------------------
void query_TrackDeletions
(
    bool on ///< [IN] If true, start tracking deletions; if false stop tracking and flush records.
)
{
    AreDeletionsTracked = on;
    if (!AreDeletionsTracked)
    {
        // Pause updates to the tree while we flush the records.
        resTree_StartUpdate();
        FlushDeletionRecords(resTree_GetRoot());
        resTree_EndUpdate();
    }
}

//--------------------------------------------------------------------------------------------------
/*
 *  Record the deletion of a node so that it can be included with a snapshot.
 */
//--------------------------------------------------------------------------------------------------
void snapshot_RecordNodeDeletion
(
    resTree_EntryRef_t nodeRef  ///< Deleted node.
)
{
    if (AreDeletionsTracked)
    {
        le_mem_AddRef(nodeRef);
        resTree_SetDeleted(nodeRef);
    }
}

//--------------------------------------------------------------------------------------------------
/*
 * Initialise the snapshot system.
 */
//--------------------------------------------------------------------------------------------------
void snapshot_Init
(
    void
)
{
    AreDeletionsTracked = false;

#if LE_CONFIG_RTOS
    LE_ASSERT(le_fd_MkFifo(SNAPSHOT_FIFO, S_IRUSR | S_IWUSR) == 0);
#endif

    NodeParentPool = le_mem_InitStaticPool(
                        NodeParentPool,
                        DEFAULT_NODE_PARENT_POOL_SIZE,
                        sizeof(Parent_t)
                    );
}
