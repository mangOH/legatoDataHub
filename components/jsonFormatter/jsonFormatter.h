//--------------------------------------------------------------------------------------------------
/**
 * @file jsonFormatter.h
 *
 * Plugin interface for JSON snapshot formatter.
 *
 * Copyright (C) Sierra Wireless Inc.
 */
//--------------------------------------------------------------------------------------------------

#ifndef JSONFORMATTER_H_INCLUDE_GUARD
#define JSONFORMATTER_H_INCLUDE_GUARD

#include "legato.h"

// Forward reference.
struct snapshot_Formatter;

//--------------------------------------------------------------------------------------------------
/*
 * Initialise and return the JSON snapshot formatter instance.
 *
 * @return LE_OK on success, otherwise an appropriate error code.
 */
//--------------------------------------------------------------------------------------------------
LE_SHARED le_result_t GetJsonSnapshotFormatter
(
    uint32_t                      flags,    ///< [IN]  Flags that were passed to the snapshot
                                            ///<       request.
    int                           stream,   ///< [IN]  File descriptor to write formatted output to.
    struct snapshot_Formatter   **formatter ///< [OUT] Returned formatter instance.
);

#endif /* end JSONFORMATTER_H_INCLUDE_GUARD */
