//--------------------------------------------------------------------------------------------------
/**
 * Periodic sensor scaffold.  Used to implement generic periodic sensors.
 *
 * Call psensor_Create() to create a sensor of any data type, with value, enable, period, and
 * trigger resources.  The value will be an input resource used to deliver the sensor samples to
 * the Data Hub, while the other resources will be outputs used to receive samples from the
 * Data Hub.  The period output is used to configure the sampling period.  The enable output is
 * used to enable/disable the sensor.  The trigger can be used to trigger a single sample of the
 * sensor.
 *
 * psensor_CreateJson() can be used to create a sensor that delivers JSON samples to the Data Hub.
 *
 * Both psensor_Create() and psensor_CreateJson() take a callback function that is called back
 * when it is time to read the sensor and push the sample to the Data Hub using one of
 * - psensor_PushBoolean()
 * - psensor_PushNumeric()
 * - psensor_PushString()
 * - psensor_PushJson()
 *
 * psensor_Destroy() can be used to destroy a previously created sensor scaffold.
 *
 * Copyright (C) Sierra Wireless Inc.
 */
//--------------------------------------------------------------------------------------------------

#ifndef PERIODIC_SENSOR_H_INCLUDE_GUARD
#define PERIODIC_SENSOR_H_INCLUDE_GUARD


//--------------------------------------------------------------------------------------------------
/**
 * Maximum number of bytes in a sensor name (including null terminator).
 */
//--------------------------------------------------------------------------------------------------
#define PSENSOR_MAX_NAME_BYTES  32


//--------------------------------------------------------------------------------------------------
/**
 * Reference to a periodic sensor scaffold.
 */
//--------------------------------------------------------------------------------------------------
typedef struct psensor* psensor_Ref_t;


//--------------------------------------------------------------------------------------------------
/**
 * Creates a periodic sensor scaffold for a sensor with a given name.
 *
 * This makes the sensor appear in the Data Hub and creates a timer for that sensor.
 * The sampleFunc will be called whenever it's time to take a sample.  The sampleFunc should
 * call one of the psensor_PushX() functions defined in this API to push its sample to the
 * Data Hub.
 *
 * @return Reference to the new periodic sensor scaffold.
 */
//--------------------------------------------------------------------------------------------------
LE_SHARED psensor_Ref_t psensor_Create
(
    const char* name,   ///< Name of the periodic sensor, or "" if the app name is sufficient.
    dhubIO_DataType_t dataType,
    const char* units,
    void (*sampleFunc)(psensor_Ref_t ref,
                       void *context), ///< Sample function to be called back periodically.
    void *sampleFuncContext  ///< Context pointer to be passed to the sample function
);


//--------------------------------------------------------------------------------------------------
/**
 * Creates a periodic sensor scaffold for a sensor with a given name that produces JSON samples.
 *
 * This makes the sensor appear in the Data Hub and creates a timer for that sensor.
 * The sampleFunc will be called whenever it's time to take a sample.  The sampleFunc is supposed
 * to call psensor_PushJson() to push the JSON sample.
 *
 * @return Reference to the new periodic sensor scaffold.
 */
//--------------------------------------------------------------------------------------------------
LE_SHARED psensor_Ref_t psensor_CreateJson
(
    const char* name,   ///< Name of the periodic sensor, or "" if the app name is sufficient.
    const char* jsonExample,    ///< String containing example JSON value.
    void (*sampleFunc)(psensor_Ref_t ref,
                       void *context), ///< Sample function to be called back periodically.
    void *sampleFuncContext  ///< Context pointer to be passed to the sample function
);


//--------------------------------------------------------------------------------------------------
/**
 * Removes a periodic sensor scaffold and all associated resources
 */
//--------------------------------------------------------------------------------------------------
LE_SHARED void psensor_Destroy
(
    psensor_Ref_t *ref
);


//--------------------------------------------------------------------------------------------------
/**
 * Push a boolean sample to the Data Hub.
 */
//--------------------------------------------------------------------------------------------------
LE_SHARED void psensor_PushBoolean
(
    psensor_Ref_t ref,  ///< Reference returned by psensor_Create().
    double timestamp,   ///< Timestamp (or 0 = now).
    bool value
);


//--------------------------------------------------------------------------------------------------
/**
 * Push a numeric sample to the Data Hub.
 */
//--------------------------------------------------------------------------------------------------
LE_SHARED void psensor_PushNumeric
(
    psensor_Ref_t ref,  ///< Reference returned by psensor_Create().
    double timestamp,   ///< Timestamp (or 0 = now).
    double value
);


//--------------------------------------------------------------------------------------------------
/**
 * Push a string sample to the Data Hub.
 */
//--------------------------------------------------------------------------------------------------
LE_SHARED void psensor_PushString
(
    psensor_Ref_t ref,  ///< Reference returned by psensor_Create().
    double timestamp,   ///< Timestamp (or 0 = now).
    const char* value
);


//--------------------------------------------------------------------------------------------------
/**
 * Push a JSON sample to the Data Hub.
 */
//--------------------------------------------------------------------------------------------------
LE_SHARED void psensor_PushJson
(
    psensor_Ref_t ref,  ///< Reference returned by psensor_Create().
    double timestamp,   ///< Timestamp (or 0 = now).
    const char* value
);


#endif // PERIODIC_SENSOR_H_INCLUDE_GUARD
