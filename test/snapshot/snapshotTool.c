//--------------------------------------------------------------------------------------------------
/**
 * Test application to call the snapshot API and output the resulting formatted data.
 *
 * Copyright (C) Sierra Wireless Inc.
 */
//--------------------------------------------------------------------------------------------------
#include "legato.h"
#include "interfaces.h"

/// FD monitor for the incoming snapshot data FD.
static le_fdMonitor_Ref_t MonitorRef;

/// FD to write the formatted output to.
static int OutFile;

/// Query API connection state.
static bool Connected;

//--------------------------------------------------------------------------------------------------
/**
 * Terminate the application.
 */
//--------------------------------------------------------------------------------------------------
static void DoExit
(
    int ret ///< Return code.
)
{
    if (Connected)
    {
        query_DisconnectService();
    }

#if LE_CONFIG_RTOS
    le_thread_Exit((void *) ret);
#else
    exit(ret);
#endif
}

//--------------------------------------------------------------------------------------------------
/**
 * Handle completion result of snapshot operation.
 */
//--------------------------------------------------------------------------------------------------
static void HandleResult
(
    le_result_t  result,    ///< Snapshot operation result.
    void        *context    ///< Unused.
)
{
    int fd = -1;
    int ret = EXIT_SUCCESS;

    LE_UNUSED(context);

    LE_DEBUG("Got result: %s", LE_RESULT_TXT(result));

    // Clean up FDs and monitor.
    if (MonitorRef != NULL)
    {
        fd = le_fdMonitor_GetFd(MonitorRef);
        le_fdMonitor_Delete(MonitorRef);
        MonitorRef = NULL;
    }
    if (fd >= 0)
    {
        le_fd_Close(fd);
    }
    if (OutFile != STDOUT_FILENO && OutFile >= 0)
    {
        le_fd_Close(OutFile);
    }

    // Handle result.
    switch (result)
    {
        case LE_OK:
            LE_INFO("Snapshot operation completed successfully.");
            break;
        case LE_BUSY:
            LE_WARN("Another snapshot operation is currently in progress, cancelling request.");
            break;
        default:
            LE_ERROR("Snapshot failed with result %s", LE_RESULT_TXT(result));
            ret = EXIT_FAILURE;
            break;
    }

    DoExit(ret);
}

//--------------------------------------------------------------------------------------------------
/**
 * Handle formatted snapshot data being streamed back from the Data Hub.
 */
//--------------------------------------------------------------------------------------------------
static void HandleStreamData
(
    int   fd,       ///< FD by which the formatted data is streamed.
    short events    ///< Event bitfield which triggered this callback.
)
{
    size_t  offset;
    ssize_t count;
    ssize_t written;
    uint8_t buffer[128];

    if (events & POLLIN)
    {
        // For the purposes of this tool we will just copy the input to the output in a straight
        // forward blocking manner.  In a real system you would probably want to handle POLLOUT on
        // the output and only feed it when it was ready.
        for (;;)
        {
            count = le_fd_Read(fd, buffer, sizeof(buffer));
            if (count < 0)
            {
                if (errno != EAGAIN)
                {
                    LE_WARN("Format stream read error: %d", errno);
                }
                return;
            }
            else if (count > 0)
            {
                offset = 0;
                while (count > 0)
                {
                    written = le_fd_Write(OutFile, &buffer[offset], count);
                    if (written < 0)
                    {
                        LE_WARN("Output stream write error");
                        return;
                    }
                    else
                    {
                        count -= written;
                        offset += written;
                        LE_ASSERT(count >= 0);
                    }
                }
            }
            else
            {
                break;
            }
        }
    }
}

//--------------------------------------------------------------------------------------------------
/**
 * Print help text to stdout and exit.
 */
//--------------------------------------------------------------------------------------------------
static void HandleHelpRequest
(
    void
)
{
    puts(
        "Usage: dsnap [-h] [-f <format>] [-s <since>] [-p <path>]"
#if LE_CONFIG_FILESYSTEM
        " [-o <output>]"
#endif
    );

    puts(
        "\n"
        "    -h, --help              Display this help.\n"
        "    -f, --format=<string>   Set output format to <string> (only \"json\" so far).\n"
        "    -s, --since=<number>    Only output information for records that have changed since\n"
        "                            <number> seconds from the Epoch.  Default (no limit) is 0.\n"
        "    -p, --path=<string>     Only consider the tree at and beneath the path <string>.\n"
        "                            The default is \"/\" for the full tree.\n"
#if LE_CONFIG_FILESYSTEM
        "    -o, --output=<string>   File path to write the output to.  Default is to write to\n"
        "                            stdout.\n"
#endif /* end LE_CONFIG_FILESYSTEM */
    );

    DoExit(EXIT_SUCCESS);
}

//--------------------------------------------------------------------------------------------------
/**
 * Component initialisation.  Handle the tool's command line parameters.
 */
//--------------------------------------------------------------------------------------------------
COMPONENT_INIT
{
    char        *endPtr = NULL;
    const char  *formatStr = "json";
    const char  *outputStr = NULL;
    const char  *pathStr = "/";
    const char  *sinceStr = "0";
    double       since;
    int          formatStream = -1;
    le_result_t  result;
    uint32_t     flags = 0;
    uint32_t     format;

    LE_ASSERT(MonitorRef == NULL);
    Connected = false;

    // Collect arguments.
    le_arg_SetFlagCallback(&HandleHelpRequest, "h", "help");
    le_arg_SetStringVar(&formatStr, "f", "format");
    le_arg_SetStringVar(&sinceStr, "s", "since");
    le_arg_SetStringVar(&pathStr, "p", "path");
#if LE_CONFIG_FILESYSTEM
    le_arg_SetStringVar(&outputStr, "o", "output");
#endif

    le_arg_Scan();
    result = le_arg_GetScanResult();
    if (result != LE_OK)
    {
        LE_ERROR("Argument parsing failed with code %s", LE_RESULT_TXT(result));
        DoExit(EXIT_FAILURE);
    }

    if (strcmp(formatStr, "json") == 0)
    {
        format = QUERY_SNAPSHOT_FORMAT_JSON;
    }
    else
    {
        LE_ERROR("Unknown format: %s", formatStr);
        DoExit(EXIT_FAILURE);
    }

    since = strtod(sinceStr, &endPtr);
    if (endPtr == sinceStr)
    {
        LE_ERROR("Invalid time stamp: %s", sinceStr);
        DoExit(EXIT_FAILURE);
    }

    if (outputStr == NULL)
    {
        OutFile = STDOUT_FILENO;
    }
    else
    {
        OutFile = le_fd_Open(outputStr, O_WRONLY | O_CREAT | O_TRUNC,
            S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    }
    LE_ASSERT(OutFile >= 0);

    // Connect to the Data Hub.
    result = query_TryConnectService();
    if (result != LE_OK)
    {
        LE_ERROR("Got %s while connecting to Data Hub Query API", LE_RESULT_TXT(result));
        DoExit(EXIT_FAILURE);
    }
    Connected = true;

    // Initiate the snapshot.
    query_TakeSnapshot(format, flags, pathStr, since, &HandleResult, NULL, &formatStream);
    if (formatStream >= 0)
    {
        // Watch for formatted data to be streamed back.
        MonitorRef = le_fdMonitor_Create("SnapshotStream", formatStream, &HandleStreamData, POLLIN);
    }
}
