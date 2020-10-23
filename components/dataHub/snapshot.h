//--------------------------------------------------------------------------------------------------
/**
 * @file snapshot.h
 *
 * Internal interface of the snapshot portion of the Query API.  This interface defines the API for
 * snapshot formatters and the hooks for deletion tracking.
 *
 * Copyright (C) Sierra Wireless Inc.
 */
//--------------------------------------------------------------------------------------------------

#ifndef SNAPSHOT_H_INCLUDE_GUARD
#define SNAPSHOT_H_INCLUDE_GUARD

#include "legato.h"

/// Filter for newly created nodes.
#define SNAPSHOT_FILTER_CREATED 0x1
/// Filter for deleted nodes.
#define SNAPSHOT_FILTER_DELETED 0x2
/// Filter for normal nodes (i.e. not new or deleted).
#define SNAPSHOT_FILTER_NORMAL  0x4

// Forward reference.
struct snapshot_Formatter;

//--------------------------------------------------------------------------------------------------
/**
 * Reference to a Resource Tree Entry.
 */
//--------------------------------------------------------------------------------------------------
typedef struct resTree_Entry *resTree_EntryRef_t;

//--------------------------------------------------------------------------------------------------
/*
 * Callback to trigger an action by the formatter plugin.
 */
//--------------------------------------------------------------------------------------------------
typedef void (*formatter_Callback_t)
(
    struct snapshot_Formatter *formatter ///< Formatter instance.
);

//--------------------------------------------------------------------------------------------------
/*
 * Base type for a resource tree snapshot formatter.
 */
//--------------------------------------------------------------------------------------------------
typedef struct snapshot_Formatter
{
    formatter_Callback_t startTree; ///< Callback to format the beginning of the resource tree.
    formatter_Callback_t beginNode; ///< Callback to format the beginning of a new tree node.
    formatter_Callback_t endNode;   ///< Callback to format the end of an open tree node.
    formatter_Callback_t endTree;   ///< Callback to format the end of all tree nodes.
    formatter_Callback_t close;     ///< Callback to close and clean up the formatter instance.

    bool        scan;   ///< Request a scan of the resource tree.
    uint32_t    filter; ///< Mask to filter nodes during tree traversal.
} snapshot_Formatter_t;

//--------------------------------------------------------------------------------------------------
/*
 * Initialise the snapshot system.
 */
//--------------------------------------------------------------------------------------------------
void snapshot_Init
(
    void
);

//--------------------------------------------------------------------------------------------------
/*
 *  Record the deletion of a node so that it can be included with a snapshot.
 */
//--------------------------------------------------------------------------------------------------
void snapshot_RecordNodeDeletion
(
    resTree_EntryRef_t nodeRef  ///< Deleted node.
);

//--------------------------------------------------------------------------------------------------
/*
 *  Obtain the flags for the current snapshot operation.
 *
 *  @return Flags for the current snapshot.
 */
//--------------------------------------------------------------------------------------------------
LE_SHARED uint32_t snapshot_GetFlags
(
    void
);

//--------------------------------------------------------------------------------------------------
/*
 *  Obtain the file stream to write formatted output to for the current snapshot operation.
 *
 *  @return File descriptor for the formatted output stream.
 */
//--------------------------------------------------------------------------------------------------
LE_SHARED int snapshot_GetStream
(
    void
);

//--------------------------------------------------------------------------------------------------
/*
 *  Obtain the resource tree node currently under consideration.
 *
 *  @return Node reference.
 */
//--------------------------------------------------------------------------------------------------
LE_SHARED resTree_EntryRef_t snapshot_GetNode
(
    void
);

//--------------------------------------------------------------------------------------------------
/*
 *  Obtain the time at which the snapshot was initiated.
 *
 *  @return Snapshot time stamp.
 */
//--------------------------------------------------------------------------------------------------
LE_SHARED double snapshot_GetTimestamp
(
    void
);

//--------------------------------------------------------------------------------------------------
/*
 * Determine if the given node is within the time window of interest for the current snapshot
 * operation.
 */
//--------------------------------------------------------------------------------------------------
LE_SHARED bool snapshot_IsTimely
(
    resTree_EntryRef_t nodeRef ///< Node reference.
);

//--------------------------------------------------------------------------------------------------
/*
 * Transition the tree-walking snapshot state machine to the next state.
 */
//--------------------------------------------------------------------------------------------------
LE_SHARED void snapshot_Step
(
    void
);

//--------------------------------------------------------------------------------------------------
/*
 * End snapshot and tidy up state.
 *
 * Close the formatter and file handles, and queue up the result callback to provide the status to
 * the user.  May be invoked from a formatter in exceptional circumstances to return early or return
 * an error.
 */
//--------------------------------------------------------------------------------------------------
LE_SHARED void snapshot_End
(
    le_result_t status  ///< [IN] Result of the snapshot request.
);

#endif /* end SNAPSHOT_H_INCLUDE_GUARD */
