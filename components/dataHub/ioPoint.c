//--------------------------------------------------------------------------------------------------
/**
 * Implementation of the Input and Output Resources.
 *
 * Copyright (C) Sierra Wireless Inc.
 */
//--------------------------------------------------------------------------------------------------

#include "dataHub.h"
#include "resource.h"
#include "ioPoint.h"
#include "json.h"


//--------------------------------------------------------------------------------------------------
/**
 * An Input or Output Resource.
 */
//--------------------------------------------------------------------------------------------------
typedef struct ioResource
{
    res_Resource_t resource;    ///< Base class. ** MUST BE FIRST **
    io_DataType_t dataType;     ///< Data type of this resource.
    le_dls_List_t pollHandlerList;  ///< List of Poll Handler callbacks the client app registered.
    bool isMandatory;   ///< true = this is a mandatory output; false otherwise.
}
IoResource_t;


//--------------------------------------------------------------------------------------------------
/**
 * Pool from which I/O Resource objects are allocated.
 */
//--------------------------------------------------------------------------------------------------
static le_mem_PoolRef_t IoResourcePool = NULL;


//--------------------------------------------------------------------------------------------------
/**
 * Destructor for IoResource_t objects.
 */
//--------------------------------------------------------------------------------------------------
static void IoResourceDestructor
(
    void* objPtr
)
//--------------------------------------------------------------------------------------------------
{
    IoResource_t* ioPtr = objPtr;

    res_Destruct(&ioPtr->resource);
}


//--------------------------------------------------------------------------------------------------
/**
 * Initialize the I/O Resource module.
 *
 * @warning This function MUST be called before any others in this module.
 */
//--------------------------------------------------------------------------------------------------
void ioPoint_Init
(
    void
)
//--------------------------------------------------------------------------------------------------
{
    IoResourcePool = le_mem_CreatePool("I/O Resource", sizeof(IoResource_t));
    le_mem_SetDestructor(IoResourcePool, IoResourceDestructor);
}


//--------------------------------------------------------------------------------------------------
/**
 * Create an Input/Output Resource object.
 *
 * @return Pointer to the Resource.
 */
//--------------------------------------------------------------------------------------------------
IoResource_t* Create
(
    io_DataType_t dataType,
    resTree_EntryRef_t entryRef ///< The resource tree entry to attach this Resource to.
)
//--------------------------------------------------------------------------------------------------
{
    IoResource_t* ioPtr = le_mem_ForceAlloc(IoResourcePool);

    res_Construct(&ioPtr->resource, entryRef);

    ioPtr->pollHandlerList = LE_DLS_LIST_INIT;

    ioPtr->dataType = dataType;
    ioPtr->isMandatory = false;

    return ioPtr;
}


//--------------------------------------------------------------------------------------------------
/**
 * Create an Input Resource.
 *
 * @return Pointer to the Resource.
 */
//--------------------------------------------------------------------------------------------------
res_Resource_t* ioPoint_CreateInput
(
    io_DataType_t dataType,
    resTree_EntryRef_t entryRef ///< The resource tree entry to attach this Resource to.
)
//--------------------------------------------------------------------------------------------------
{
    return &(Create(dataType, entryRef)->resource);
}


//--------------------------------------------------------------------------------------------------
/**
 * Create an Output Resource.
 *
 * @return Pointer to the Resource.
 */
//--------------------------------------------------------------------------------------------------
res_Resource_t* ioPoint_CreateOutput
(
    io_DataType_t dataType,
    resTree_EntryRef_t entryRef ///< The resource tree entry to attach this Resource to.
)
//--------------------------------------------------------------------------------------------------
{
    IoResource_t* ioPtr = Create(dataType, entryRef);

    // By default, all outputs are mandatory.
    ioPtr->isMandatory = true;

    return &(ioPtr->resource);
}


//--------------------------------------------------------------------------------------------------
/**
 * Get the data type of an Input or Output resource.
 *
 * @return The data type.
 */
//--------------------------------------------------------------------------------------------------
io_DataType_t ioPoint_GetDataType
(
    res_Resource_t* resPtr
)
//--------------------------------------------------------------------------------------------------
{
    IoResource_t* ioPtr = CONTAINER_OF(resPtr, IoResource_t, resource);

    return ioPtr->dataType;
}


//--------------------------------------------------------------------------------------------------
/**
 * Perform type coercion, replacing a data sample with another of a different type, if necessary,
 * to make the data compatible with the data type of a given Input or Output resource.
 */
//--------------------------------------------------------------------------------------------------
void ioPoint_DoTypeCoercion
(
    res_Resource_t* resPtr,
    io_DataType_t* dataTypePtr,     ///< [INOUT] the data type, may be changed by type coercion
    dataSample_Ref_t* valueRefPtr   ///< [INOUT] the data sample, may be replaced by type coercion
)
//--------------------------------------------------------------------------------------------------
{
    IoResource_t* ioPtr = CONTAINER_OF(resPtr, IoResource_t, resource);

    io_DataType_t fromType = *dataTypePtr;
    dataSample_Ref_t fromSample = *valueRefPtr;

    io_DataType_t toType = ioPtr->dataType;
    dataSample_Ref_t toSample = NULL;

    double timestamp = dataSample_GetTimestamp(fromSample);

    switch (toType)
    {
        case IO_DATA_TYPE_TRIGGER:

            // If the pushed sample is not a trigger, then create a new trigger sample with the
            // same timestamp as the original sample.  Otherwise no type conversion required.
            if (fromType != IO_DATA_TYPE_TRIGGER)
            {
                toSample = dataSample_CreateTrigger(timestamp);
            }
            break;

        case IO_DATA_TYPE_BOOLEAN:

            switch (fromType)
            {
                case IO_DATA_TYPE_TRIGGER:

                    // If the pushed sample is a trigger, just use false.
                    toSample = dataSample_CreateBoolean(timestamp, false);
                    break;

                case IO_DATA_TYPE_BOOLEAN:

                    break;  // No conversion required.

                case IO_DATA_TYPE_NUMERIC:
                {
                    double value = dataSample_GetNumeric(fromSample);
                    toSample = dataSample_CreateBoolean(timestamp, (value != 0));
                    break;
                }

                case IO_DATA_TYPE_STRING:
                {
                    const char* value = dataSample_GetString(fromSample);
                    toSample = dataSample_CreateBoolean(timestamp, (value[0] != '\0'));
                    break;
                }

                case IO_DATA_TYPE_JSON:
                {
                    bool newValue = json_ConvertToBoolean(dataSample_GetJson(fromSample));
                    toSample = dataSample_CreateBoolean(timestamp, newValue);
                    break;
                }
            }

            break;

        case IO_DATA_TYPE_NUMERIC:

            switch (fromType)
            {
                case IO_DATA_TYPE_TRIGGER:

                    toSample = dataSample_CreateNumeric(timestamp, NAN);
                    break;

                case IO_DATA_TYPE_BOOLEAN:
                {
                    double newValue = (dataSample_GetBoolean(fromSample) ? 1 : 0);
                    toSample = dataSample_CreateNumeric(timestamp, newValue);
                    break;
                }

                case IO_DATA_TYPE_NUMERIC:

                    break;  // No conversion required.

                case IO_DATA_TYPE_STRING:
                {
                    double newValue = (dataSample_GetString(fromSample)[0] == '\0' ? 0 : 1);
                    toSample = dataSample_CreateNumeric(timestamp, newValue);
                    break;
                }

                case IO_DATA_TYPE_JSON:
                {
                    double newValue = json_ConvertToNumber(dataSample_GetJson(fromSample));
                    toSample = dataSample_CreateNumeric(timestamp, newValue);
                    break;
                }
            }
            break;

        case IO_DATA_TYPE_STRING:
        {
            char newValue[HUB_MAX_STRING_BYTES];
            if (dataSample_ConvertToString(fromSample, fromType, newValue, sizeof(newValue)) == LE_OK)
            {
                toSample = dataSample_CreateString(timestamp, newValue);
            }
            else
            {
                LE_CRIT("String length exceeds HUB_MAX_STRING_BYTES");
                toSample = NULL;
            }
            break;
        }

        case IO_DATA_TYPE_JSON:
        {
            char newValue[HUB_MAX_STRING_BYTES];
            if (dataSample_ConvertToJson(fromSample, fromType, newValue, sizeof(newValue)) == LE_OK)
            {
                toSample = dataSample_CreateJson(timestamp, newValue);
            }
            else
            {
                LE_CRIT("JSON length exceeds HUB_MAX_STRING_BYTES");
                toSample = NULL;
            }
            break;
        }
    }

    // If a conversion happened, release the old value and replace it with the new one.
    if (toSample != NULL)
    {
        le_mem_Release(fromSample);
        *valueRefPtr = toSample;
        *dataTypePtr = toType;
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Mark an Output resource "optional".  (By default, they are marked "mandatory".)
 */
//--------------------------------------------------------------------------------------------------
void ioPoint_MarkOptional
(
    res_Resource_t* resPtr
)
//--------------------------------------------------------------------------------------------------
{
    IoResource_t* ioPtr = CONTAINER_OF(resPtr, IoResource_t, resource);

    ioPtr->isMandatory = false;
}


//--------------------------------------------------------------------------------------------------
/**
 * Check if a given resource is a mandatory output.  If so, it means that this is an output resource
 * that must have a value before the related app function will begin working.
 *
 * @return true if a mandatory output, false if it's an optional output or not an output at all.
 */
//--------------------------------------------------------------------------------------------------
bool ioPoint_IsMandatory
(
    res_Resource_t* resPtr
)
//--------------------------------------------------------------------------------------------------
{
    IoResource_t* ioPtr = CONTAINER_OF(resPtr, IoResource_t, resource);

    return ioPtr->isMandatory;
}
