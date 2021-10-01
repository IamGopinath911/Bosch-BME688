#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include "structmember.h"
#include "internal_functions.h"

#define I2C_PORT_0 "/dev/i2c-0"
#define I2C_PORT_1 "/dev/i2c-1"

#define BME68X_VALID_DATA UINT8_C(0xB0) // DEZ 176  BIN 1011000 -> BITWISE & WITH BME68X_NEW_DATA_MSK 0x80, BME68X_GASM_VALID_MASK 0x20 AND BME68X_HEAT_STAB_MSK 0x10 YIELDS 1
#define BME68X_FLOAT_POINT_COMPENSATION
#define BSEC

uint64_t time_stamp_interval_us = 0;
uint32_t n_samples = 0;

#ifdef BSEC
uint8_t bsec_state[BSEC_MAX_STATE_BLOB_SIZE];
uint8_t work_buffer[BSEC_MAX_WORKBUFFER_SIZE];
uint32_t bsec_state_len = 0;
bsec_library_return_t bsec_status = BSEC_OK;
const char *bsec_conf_path = "BSEC_2.0.6.1_Generic_Release_04302021/config/bsec_sel_iaq_33v_4d/2021_04_29_02_51_bsec_h2s_nonh2s_2_0_6_1 .config";
FILE *bsec_conf;
#endif

static PyObject *bmeError;

typedef struct
{
    PyObject_HEAD
        uint8_t linux_device;
    int8_t temp_offset;
    struct bme68x_dev bme;
    struct bme68x_conf conf;
    struct bme68x_heatr_conf heatr_conf;
    struct bme68x_data *data;
    struct bme68x_data data_backup;
    int64_t timestamp_backup;
    uint32_t del_period;
    uint32_t time_ms;
    uint8_t n_fields;
    uint64_t next_call;
    uint8_t last_meas_index;
    int8_t rslt;
    uint8_t op_mode;
    uint16_t sample_count;
    uint8_t debug_mode;
} BMEObject;

static void
bme68x_dealloc(BMEObject *self)
{
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyObject *
bme68x_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    BMEObject *self;
    self = (BMEObject *)type->tp_alloc(type, 0);
    if (self != NULL)
    {
        self->linux_device = 0;
        self->temp_offset = 0;

        self->bme.amb_temp = 0;
        self->bme.calib.par_gh1 = 0;
        self->bme.calib.par_gh2 = 0;
        self->bme.calib.par_gh3 = 0;
        self->bme.calib.par_h1 = 0;
        self->bme.calib.par_h2 = 0;
        self->bme.calib.par_h3 = 0;
        self->bme.calib.par_h4 = 0;
        self->bme.calib.par_h5 = 0;
        self->bme.calib.par_h6 = 0;
        self->bme.calib.par_h7 = 0;
        self->bme.calib.par_p10 = 0;
        self->bme.calib.par_p1 = 0;
        self->bme.calib.par_p2 = 0;
        self->bme.calib.par_p3 = 0;
        self->bme.calib.par_p4 = 0;
        self->bme.calib.par_p5 = 0;
        self->bme.calib.par_p6 = 0;
        self->bme.calib.par_p7 = 0;
        self->bme.calib.par_p8 = 0;
        self->bme.calib.par_p9 = 0;
        self->bme.calib.par_t1 = 0;
        self->bme.calib.par_t2 = 0;
        self->bme.calib.par_t3 = 0;
        self->bme.calib.range_sw_err = 0;
        self->bme.calib.res_heat_range = 0;
        self->bme.calib.res_heat_val = 0;
        self->bme.calib.t_fine = 0.0;
        self->bme.chip_id = 0;
        self->bme.delay_us = pi3g_delay_us;
        self->bme.info_msg = 0;
        self->bme.intf = BME68X_I2C_INTF;
        self->bme.intf_ptr = &(self->linux_device);
        self->bme.intf_rslt = 0;
        self->bme.mem_page = 0;
        self->bme.read = pi3g_read;
        self->bme.variant_id = 0;
        self->bme.write = pi3g_write;

        self->conf.os_hum = 0;
        self->conf.os_temp = 0;
        self->conf.os_pres = 0;
        self->conf.filter = 0;
        self->conf.odr = 0;

        self->heatr_conf.enable = 0;
        self->heatr_conf.heatr_dur = 0;
        uint16_t heatr_dur_prof[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        self->heatr_conf.heatr_dur_prof = heatr_dur_prof;
        self->heatr_conf.heatr_temp = 0;
        uint16_t heatr_temp_prof[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        self->heatr_conf.heatr_temp_prof = heatr_temp_prof;
        self->heatr_conf.profile_len = 0;
        self->heatr_conf.shared_heatr_dur = 0;

        self->data = malloc(sizeof(struct bme68x_data) * 3);

        self->data_backup.status = 0;
        self->data_backup.gas_index = 0;
        self->data_backup.meas_index = 0;
        self->data_backup.res_heat = 0;
        self->data_backup.idac = 0;
        self->data_backup.gas_wait = 0;
#ifndef BME68X_USE_FPU
        self->data_backup.temperature = 0;
        self->data_backup.pressure = 0;
        self->data_backup.humidity = 0;
        self->data_backup.gas_resistance = 0;
#else
        self->data_backup.temperature = 0.0;
        self->data_backup.pressure = 0.0;
        self->data_backup.humidity = 0.0;
        self->data_backup.gas_resistance = 0.0;
#endif

        self->timestamp_backup = 0;

        self->del_period = 0;
        self->time_ms = 0;
        self->n_fields = 0;
        self->next_call = 0;
        self->last_meas_index = 0;
        self->rslt = BME68X_OK;
        self->op_mode = BME68X_SLEEP_MODE;
        self->sample_count = 0;
        self->debug_mode = 0;
    }
    return (PyObject *)self;
}

static int
bme68x_init_type(BMEObject *self, PyObject *args, PyObject *kwds)
{
    static char *kwlist1[] = {"i2c_addr", NULL};
    static char *kwlist2[] = {"i2c_addr", "debug_mode", NULL};

    self->linux_device = open(I2C_PORT_1, O_RDWR);
    Py_ssize_t size = PyTuple_Size(args);
    uint8_t i2c_addr;

    switch (size)
    {
    case 1:
        self->debug_mode = 0;
        PyArg_ParseTupleAndKeywords(args, kwds, "|b", kwlist1, &i2c_addr);
        if (ioctl(*((uint8_t *)self->bme.intf_ptr), I2C_SLAVE, i2c_addr) < 0)
        {
            perror("wrong I2C adress");
            PyErr_SetString(bmeError, "I2C device not found (check adress)");
            return -1;
        }
        break;
    case 2:
        PyArg_ParseTupleAndKeywords(args, kwds, "|bb", kwlist2, &i2c_addr, &(self->debug_mode));
        if (ioctl(*((uint8_t *)self->bme.intf_ptr), I2C_SLAVE, i2c_addr) < 0)
        {
            perror("wrong I2C adress");
            PyErr_SetString(bmeError, "I2C device not found (check adress)");
            return -1;
        }
        break;
    default:
        self->debug_mode = 0;
        if (ioctl(*((uint8_t *)self->bme.intf_ptr), I2C_SLAVE, BME68X_I2C_ADDR_LOW) < 0)
        {
            if (ioctl(*((uint8_t *)self->bme.intf_ptr), I2C_SLAVE, BME68X_I2C_ADDR_HIGH) < 0)
            {
                PyErr_SetString(bmeError, "Failed to open I2C device");
                return -1;
            }
        }
    }
    // Initialize BME68X sensor
    self->bme.intf = BME68X_I2C_INTF;
    self->bme.amb_temp = 25;
    self->bme.read = pi3g_read;
    self->bme.write = pi3g_write;
    self->bme.delay_us = pi3g_delay_us;

    self->rslt = BME68X_OK;
    self->rslt = bme68x_init(&(self->bme));
    if (self->rslt == BME68X_OK)
    {
        if (self->debug_mode == 1)
        {
            printf("INITIALIZED BME68X\n");
            if (self->bme.variant_id == BME68X_VARIANT_GAS_LOW)
            {
                printf("VARIANT BME680\n");
            }
            else
            {
                printf("VARIANT BME688\n");
            }
        }
    }
    else
    {
        perror("initialize BME68X");
        PyErr_SetString(bmeError, "Could not initialize BME68X");
        return self->rslt;
    }
#ifdef BSEC
    self->rslt = bsec_init();
    if (self->rslt != BSEC_OK)
    {
        PyErr_SetString(bmeError, "Failed to initialize BSEC");
        return self->rslt;
    }
    bsec_version_t version;
    bsec_get_version(&version);
    if (self->debug_mode == 1)
    {
        printf("INITIALIZED BSEC\nBSEC VERSION: %d.%d.%d.%d\n", version.major, version.minor, version.major_bugfix, version.minor_bugfix);
    }
#endif

    return self->rslt;
}

static PyMemberDef bme68x_members[] = {
    {"linux_device_handle", T_UBYTE, offsetof(BMEObject, linux_device), 0, "Linux address of the sensor device"},
    {"temp_offset", T_BYTE, offsetof(BMEObject, temp_offset), 0, "temperature offset to be subtracted from 25 degC"},
    {"bme68x", T_OBJECT_EX, offsetof(BMEObject, bme), 0, "BME68X sensor struct"},
    {"conf", T_OBJECT_EX, offsetof(BMEObject, conf), 0, "BME68X config struct"},
    {"heatr_conf", T_OBJECT_EX, offsetof(BMEObject, heatr_conf), 0, "BME68X heater config struct"},
    {"data", T_OBJECT_EX, offsetof(BMEObject, data), 0, "BME68X measurement data struct"},
    {"data_backup", T_OBJECT_EX, offsetof(BMEObject, data_backup), 0, "data backup in case of invalid data"},
    {"del_period", T_ULONG, offsetof(BMEObject, del_period), 0, "delay period"},
    {"time_ms", T_ULONG, offsetof(BMEObject, time_ms), 0, "millisecond precision time stamp"},
    {"n_fields", T_UBYTE, offsetof(BMEObject, n_fields), 0, "number of data fields"}, 
    {"next_call", T_LONGLONG, offsetof(BMEObject, next_call), 0, "nanosecond timestamp of next bsec_sensore_control call"},
    {"last_meas_index", T_UBYTE, offsetof(BMEObject, last_meas_index), 0, "index to track measurement order"},
    {"rslt", T_BYTE, offsetof(BMEObject, rslt), 0, "function execution result"},
    {"op_mode", T_UBYTE, offsetof(BMEObject, op_mode), 0, "BME68X operation mode"},
    {"sample_count", T_UINT, offsetof(BMEObject, sample_count), 0, "number of data samples"},
    {"debug_mode", T_UBYTE, offsetof(BMEObject, debug_mode), 0, "enable/disable debug_mode"},
};

#ifdef BSEC
static PyObject *bme_set_sample_rate(BMEObject *self, PyObject *args)
{
    float sample_rate;

    if(!PyArg_ParseTuple(args, "f", &sample_rate))
    {
        PyErr_SetString(bmeError, "Argument must be of type float");
        return NULL;
    }

    return Py_BuildValue("i", bsec_set_sample_rate(sample_rate));
}
#endif

static PyObject *bme_init_bme68x(BMEObject *self)
{
    // Initialize BME68X sensor
    self->bme.intf = BME68X_I2C_INTF;
    self->bme.amb_temp = 25;
    self->bme.read = pi3g_read;
    self->bme.write = pi3g_write;
    self->bme.delay_us = pi3g_delay_us;

    self->rslt = BME68X_OK;
    self->rslt = bme68x_init(&(self->bme));
    if (self->rslt == BME68X_OK)
    {
        if (self->debug_mode == 1)
        {
            printf("INITIALIZED BME68X\n");
            if (self->bme.variant_id == BME68X_VARIANT_GAS_LOW)
            {
                printf("VARIANT BME680\n");
            }
            else
            {
                printf("VARIANT BME688\n");
            }
        }
    }

    return Py_BuildValue("i", self->rslt);  
}

static PyObject *bme_print_dur_prof(BMEObject *self)
{
    for (uint8_t i = 0; i < self->heatr_conf.profile_len; i++)
    {
        printf("%d ", self->heatr_conf.heatr_dur_prof[i]);
    }
    printf("\n");

    return Py_BuildValue("s", "None");
}

static PyObject *bme_enable_debug_mode(BMEObject *self)
{
    self->debug_mode = 1;
    return Py_BuildValue("s", "Disabled debug mode");
}

static PyObject *bme_disable_debug_mode(BMEObject *self)
{
    self->debug_mode = 0;
    return Py_BuildValue("s", "Enabled debug mode");
}

static PyObject *bme_get_sensor_id(BMEObject *self)
{
    uint8_t id_regs[4];
    uint32_t len = 4;
    uint32_t uid;
    int8_t rslt;
    rslt = bme68x_get_regs(BME68X_REG_UNIQUE_ID, &id_regs, len, &(self->bme));
    if (rslt < BME68X_OK)
    {
        PyErr_SetString(bmeError, "Failed to read sensor id register");
        return NULL;
    }
    // Not mentioned in bme688 datasheet but 4 byte sensor id is stored in register 0x83 in msb
    uid = (id_regs[0] << 24) | (id_regs[1] << 16) | (id_regs[2] << 8) | id_regs[3];

    return Py_BuildValue("i", uid);
}

static PyObject *bme_set_temp_offset(BMEObject *self, PyObject *args)
{
    int t_offs;
    if (!PyArg_ParseTuple(args, "i", &t_offs))
    {
        PyErr_SetString(bmeError, "Invalid arguments in set_temp_offset(double t_offs)");
        return NULL;
    }

    self->temp_offset = t_offs;
    self->bme.amb_temp = 25 - self->temp_offset;

    if (self->debug_mode == 1)
    {
        printf("SET TEMP OFFSET\n");
    }

    return Py_BuildValue("i", 0);
}

#ifdef BSEC
static PyObject *bme_subscribe_gas_estimates(BMEObject *self, PyObject *args)
{
    uint8_t n_requested_virtual_sensors;
    if (!PyArg_ParseTuple(args, "b", &n_requested_virtual_sensors))
        {
            PyErr_SetString(bmeError, "Argument must be int number of gas estimates (0 - 4)");
            return (PyObject *) NULL;
        }
    uint8_t n_req_sensors = n_requested_virtual_sensors + 1;
    bsec_sensor_configuration_t requested_virtual_sensors[n_req_sensors];

    bsec_sensor_configuration_t required_sensor_settings[BSEC_MAX_PHYSICAL_SENSOR];
    uint8_t n_required_sensor_settings = BSEC_MAX_PHYSICAL_SENSOR;

    for (uint8_t i = 0; i < n_requested_virtual_sensors; i++)
    {
        requested_virtual_sensors[i].sensor_id = BSEC_OUTPUT_GAS_ESTIMATE_1 + i;
        requested_virtual_sensors[i].sample_rate = BSEC_SAMPLE_RATE_HIGH_PERFORMANCE;
    }
    requested_virtual_sensors[n_requested_virtual_sensors].sensor_id = BSEC_OUTPUT_RAW_GAS_INDEX;
    requested_virtual_sensors[n_requested_virtual_sensors].sample_rate = BSEC_SAMPLE_RATE_HIGH_PERFORMANCE;

    self->rslt = bsec_update_subscription(requested_virtual_sensors, n_requested_virtual_sensors, required_sensor_settings, &n_required_sensor_settings);

    return Py_BuildValue("i", self->rslt);
}

static PyObject *bme_subscribe_ai_classes(BMEObject *self)
{
    uint8_t n_requested_virtual_sensors;
    n_requested_virtual_sensors = 5;
    bsec_sensor_configuration_t requested_virtual_sensors[n_requested_virtual_sensors];

    bsec_sensor_configuration_t required_sensor_settings[BSEC_MAX_PHYSICAL_SENSOR];
    uint8_t n_required_sensor_settings = BSEC_MAX_PHYSICAL_SENSOR;

    requested_virtual_sensors[0].sensor_id = BSEC_OUTPUT_GAS_ESTIMATE_1;
    requested_virtual_sensors[0].sample_rate = BSEC_SAMPLE_RATE_HIGH_PERFORMANCE;
    requested_virtual_sensors[1].sensor_id = BSEC_OUTPUT_GAS_ESTIMATE_2;
    requested_virtual_sensors[1].sample_rate = BSEC_SAMPLE_RATE_HIGH_PERFORMANCE;
    requested_virtual_sensors[2].sensor_id = BSEC_OUTPUT_GAS_ESTIMATE_3;
    requested_virtual_sensors[2].sample_rate = BSEC_SAMPLE_RATE_HIGH_PERFORMANCE;
    requested_virtual_sensors[3].sensor_id = BSEC_OUTPUT_GAS_ESTIMATE_4;
    requested_virtual_sensors[3].sample_rate = BSEC_SAMPLE_RATE_HIGH_PERFORMANCE;
    requested_virtual_sensors[4].sensor_id = BSEC_OUTPUT_RAW_GAS_INDEX;
    requested_virtual_sensors[4].sample_rate = BSEC_SAMPLE_RATE_HIGH_PERFORMANCE;

    self->rslt = bsec_update_subscription(requested_virtual_sensors, n_requested_virtual_sensors, required_sensor_settings, &n_required_sensor_settings);

    for(uint8_t i = 0; i < n_required_sensor_settings; i++)
    {
        switch(required_sensor_settings[i].sensor_id)
        {
            case BSEC_INPUT_PRESSURE: printf("BSEC_INPUT_PRESSURE "); break;
            case BSEC_INPUT_HUMIDITY: printf("BSEC_INPUT_HUMIDITY "); break;
            case BSEC_INPUT_TEMPERATURE: printf("BSEC_INPUT_TEMPERATURE "); break;
            case BSEC_INPUT_GASRESISTOR: printf("BSEC_INPUT_GASRESISTOR "); break;
            case BSEC_INPUT_HEATSOURCE: printf("BSEC_INPUT_HEATSOURCE "); break;
            case BSEC_INPUT_DISABLE_BASELINE_TRACKER: printf("BSEC_INPUT_DISABLE_BASELINE_TRACKER "); break;
            case BSEC_INPUT_PROFILE_PART: printf("BSEC_INPUT_PROFILE_PART "); break;
            case 22: printf("ADDITIONAL_INPUT_FOR_DEVICE_HEAT_COMPENSATION_8 "); break;
            default: printf("UNKOWN CASE FOR SENSOR ID IN SUBSCRIBE_AI_CLASSES %d ", required_sensor_settings[i].sensor_id); break;
        }
        
        printf("%.6f\n", required_sensor_settings[i].sample_rate);
    }

    return Py_BuildValue("i", self->rslt);
}
#endif

static PyObject *bme_get_chip_id(BMEObject *self)
{
    return Py_BuildValue("i", self->bme.chip_id);
}

static PyObject *bme_close_i2c(BMEObject *self)
{
    return Py_BuildValue("i", close(*((uint8_t *)self->bme.intf_ptr)));
}

static PyObject *bme_open_i2c(BMEObject *self, PyObject *args)
{
    close(*((uint8_t *)self->bme.intf_ptr));
    self->linux_device = open(I2C_PORT_1, O_RDWR);
    self->bme.intf_ptr = &(self->linux_device);
    Py_ssize_t size = PyTuple_Size(args);
    uint8_t i2c_addr;

    if ((uint8_t)size == 1)
    {
        if (!PyArg_ParseTuple(args, "b", &i2c_addr))
        {
            PyErr_SetString(bmeError, "Failed to parse I2C address");
            return (PyObject *) NULL;
        }
        else if (ioctl(*((uint8_t *)self->bme.intf_ptr), I2C_SLAVE, i2c_addr) < 0)
        {
            PyErr_SetString(bmeError, "Failed to open I2C address");
            return (PyObject *) NULL;
        }
    }
    else
    {
        PyErr_SetString(bmeError, "Argument must be i2c_addr: int");
        close(*((uint8_t *)self->bme.intf_ptr));
        return (PyObject *) NULL;
    }

    return Py_BuildValue("i", 0);
}

static PyObject *bme_get_variant(BMEObject *self)
{
    char *variant = "";
    if (self->bme.variant_id == BME68X_VARIANT_GAS_LOW)
    {
        variant = "BME680";
    }
    else if (self->bme.variant_id == BME68X_VARIANT_GAS_HIGH)
    {
        variant = "BME688";
    }
    else
    {
        variant = "UNKNOWN";
    }
    return Py_BuildValue("s", variant);
}

static PyObject *bme_set_conf(BMEObject *self, PyObject *args)
{
    Py_ssize_t size = PyTuple_Size(args);
    uint8_t c[size];

    switch (size)
    {
    case 1:
        PyArg_ParseTuple(args, "b", &c[0]);
        self->rslt = pi3g_set_conf(c[0], self->conf.os_pres, self->conf.os_temp, self->conf.filter, self->conf.odr, &(self->conf), &(self->bme), self->debug_mode);
        break;
    case 2:
        PyArg_ParseTuple(args, "bb", &c[0], &c[1]);
        self->rslt = pi3g_set_conf(c[0], c[1], self->conf.os_temp, self->conf.filter, self->conf.odr, &(self->conf), &(self->bme), self->debug_mode);
        break;
    case 3:
        PyArg_ParseTuple(args, "bbb", &c[0], &c[1], &c[2]);
        self->rslt = pi3g_set_conf(c[0], c[1], c[2], self->conf.filter, self->conf.odr, &(self->conf), &(self->bme), self->debug_mode);
        break;
    case 4:
        PyArg_ParseTuple(args, "bbbb", &c[0], &c[1], &c[2], &c[3]);
        self->rslt = pi3g_set_conf(c[0], c[1], c[2], c[3], self->conf.odr, &(self->conf), &(self->bme), self->debug_mode);
        break;
    case 5:
        PyArg_ParseTuple(args, "bbbbb", &c[0], &c[1], &c[2], &c[3], &c[4]);
        self->rslt = pi3g_set_conf(c[0], c[1], c[2], c[3], c[4], &(self->conf), &(self->bme), self->debug_mode);
        break;
    default:
        self->rslt = pi3g_set_conf(BME68X_OS_16X, BME68X_OS_1X, BME68X_OS_2X, BME68X_FILTER_OFF, BME68X_ODR_NONE, &(self->conf), &(self->bme), self->debug_mode);
        break;
    }

    return Py_BuildValue("b", self->rslt);
}

static PyObject *bme_set_heatr_conf(BMEObject *self, PyObject *args)
{
    uint8_t enable;
    PyObject *temp_prof_obj;
    PyObject *dur_prof_obj;
    if (!PyArg_ParseTuple(args, "bOOb", &enable, &temp_prof_obj, &dur_prof_obj, &(self->op_mode)))
    {
        PyErr_SetString(bmeError, "Function takes 4 arguments: enable, temp_prof, dur_prof, op_mode");
        return (PyObject *) NULL;
    }
    if (self->op_mode == BME68X_FORCED_MODE)
    {
        uint16_t heatr_temp, heatr_dur;
        PyArg_Parse(temp_prof_obj, "H", &heatr_temp);
        PyArg_Parse(dur_prof_obj, "H", &heatr_dur);
        if (heatr_temp == 0 || heatr_dur == 0)
        {
            PyErr_SetString(bmeError, "heatr_temp and heatr_dur need to be of type uint16_t (unsigned short)");
            return (PyObject *) NULL;
        }
        self->rslt = pi3g_set_heater_conf_fm(enable, heatr_temp, heatr_dur, &(self->heatr_conf), &(self->bme), self->debug_mode);
    }
    else
    {
        if (self->bme.variant_id == BME68X_VARIANT_GAS_LOW)
        {
            printf("ONLY FORCED MODE IS AVAILABLE FOR BME680 SENSOR\n");
            return Py_BuildValue("i", -1);
        }

        if (!PyList_Check(temp_prof_obj) || !PyList_Check(dur_prof_obj))
        {
            PyErr_SetString(bmeError, "temp_prof and dur_prof must be of type list\n");
            return (PyObject *) NULL;
        }

        int temp_size = PyList_Size(temp_prof_obj);
        int dur_size = PyList_Size(dur_prof_obj);
        if (temp_size != dur_size)
        {
            PyErr_SetString(bmeError, "temp_prof and dur_prof must have the same size");
            return (PyObject *) NULL;
        }
        if (temp_size > 10)
        {
            PyErr_SetString(bmeError, "length of heater profile must not exceed 10");
            return (PyObject *) NULL;
        }

        uint16_t temp_prof[temp_size], dur_prof[temp_size];
        PyObject *val;

        for (int i = 0; i < temp_size; i++)
        {
            val = PyList_GetItem(temp_prof_obj, i);
            temp_prof[i] = (uint16_t)PyLong_AsLong(val);
            val = PyList_GetItem(dur_prof_obj, i);
            dur_prof[i] = (uint16_t)PyLong_AsLong(val);
        }

        for (int i = 0; i < temp_size; i++)
        {
            printf("%d ", temp_prof[i]);
        }
        printf("\n");
        for (int i = 0; i < temp_size; i++)
        {
            printf("%d ", dur_prof[i]);
        }
        printf("\n");

        if (self->op_mode == BME68X_PARALLEL_MODE)
        {
            self->rslt = pi3g_set_heater_conf_pm(enable, temp_prof, dur_prof, (uint8_t)temp_size, &(self->conf), &(self->heatr_conf), &(self->bme), self->debug_mode);
            printf("DUR PROF AFTER PI3G\n");
            for (uint8_t i = 0; i < self->heatr_conf.profile_len; i++)
            {
                printf("%d ", self->heatr_conf.heatr_dur_prof[i]);
            }
            printf("\n");
        }
        else if (self->op_mode == BME68X_SEQUENTIAL_MODE)
        {
            self->rslt = pi3g_set_heater_conf_sm(enable, temp_prof, dur_prof, (uint8_t)temp_size, &(self->heatr_conf), &(self->bme), self->debug_mode);
        }
        else
        {
            perror("set_heatr_conf");
        }
    }

    return Py_BuildValue("i", self->rslt);
}

static PyObject *bme_get_data(BMEObject *self)
{
    self->rslt = bme68x_set_op_mode(self->op_mode, &(self->bme));

    if (self->rslt != BME68X_OK)
    {
        perror("set_op_mode");
    }

    if (self->op_mode == BME68X_FORCED_MODE)
    {
        self->del_period = bme68x_get_meas_dur(BME68X_FORCED_MODE, &(self->conf), &(self->bme)) + (self->heatr_conf.heatr_dur * 1000);
        self->bme.delay_us(self->del_period, self->bme.intf_ptr);
        self->time_ms = pi3g_timestamp_ms();

        self->rslt = bme68x_get_data(self->op_mode, self->data, &(self->n_fields), &(self->bme));
        if (self->rslt == BME68X_OK && self->n_fields > 0)
        {
            self->sample_count++;
            self->bme.amb_temp = self->data[0].temperature - self->temp_offset;

            PyObject *pydata = PyDict_New();
            PyDict_SetItemString(pydata, "sample_nr", Py_BuildValue("i", self->sample_count));
            PyDict_SetItemString(pydata, "timestamp", Py_BuildValue("i", self->time_ms));
            PyDict_SetItemString(pydata, "raw_temperature", Py_BuildValue("d", self->data[0].temperature));
            PyDict_SetItemString(pydata, "raw_pressure", Py_BuildValue("d", self->data[0].pressure / 100));
            PyDict_SetItemString(pydata, "raw_humidity", Py_BuildValue("d", self->data[0].humidity));
            PyDict_SetItemString(pydata, "raw_gas", Py_BuildValue("d", self->data[0].gas_resistance / 1000));
            PyDict_SetItemString(pydata, "status", Py_BuildValue("i", self->data[0].status));
            return pydata;
        }
    }
    else
    {
        PyObject *pydata = PyList_New(self->heatr_conf.profile_len);
        PyObject *field;
        uint8_t counter = 0;
        while (counter < self->heatr_conf.profile_len)
        {
            if (self->op_mode == BME68X_PARALLEL_MODE)
            {
                self->del_period = bme68x_get_meas_dur(BME68X_PARALLEL_MODE, &(self->conf), &(self->bme)) + (self->heatr_conf.shared_heatr_dur * 1000);
            }
            else if (self->op_mode == BME68X_SEQUENTIAL_MODE)
            {
                self->del_period = bme68x_get_meas_dur(BME68X_SEQUENTIAL_MODE, &(self->conf), &(self->bme)) + (self->heatr_conf.heatr_dur_prof[0] * 1000);
            }
            else
            {
                PyErr_SetString(bmeError, "Failed to receive data");
                return (PyObject *) NULL;
            }
            self->bme.delay_us(self->del_period, self->bme.intf_ptr);
            self->time_ms = pi3g_timestamp_ms();

            self->rslt = bme68x_get_data(self->op_mode, self->data, &(self->n_fields), &(self->bme));
            if (self->rslt < 0)
            {
                perror("bme68x_get_data");
            }

            /* Check if rslt == BME68X_OK, report or handle if otherwise */
            for (uint8_t i = 0; i < self->n_fields; i++)
            {
                if (self->data[i].status == BME68X_VALID_DATA)
                {
                    field = PyDict_New();
                    PyDict_SetItemString(field, "sample_nr", Py_BuildValue("i", self->sample_count));
                    PyDict_SetItemString(field, "timestamp", Py_BuildValue("i", self->time_ms));
                    PyDict_SetItemString(field, "raw_temperature", Py_BuildValue("d", self->data[i].temperature));
                    PyDict_SetItemString(field, "raw_pressure", Py_BuildValue("d", self->data[i].pressure / 100));
                    PyDict_SetItemString(field, "raw_humidity", Py_BuildValue("d", self->data[i].humidity));
                    PyDict_SetItemString(field, "raw_gas", Py_BuildValue("d", self->data[i].gas_resistance / 1000));
                    PyDict_SetItemString(field, "gas_index", Py_BuildValue("i", self->data[i].gas_index));
                    PyDict_SetItemString(field, "meas_index", Py_BuildValue("i", self->data[i].meas_index));
                    PyDict_SetItemString(field, "status", Py_BuildValue("i", self->data[i].status));
                    PyList_SetItem(pydata, self->data[i].gas_index, field);
                    self->sample_count++;
                    counter++;
                }
            }
        }
        self->bme.amb_temp = self->data[0].temperature - self->temp_offset;
        return pydata;
    }
    return Py_BuildValue("s", "Failed to get data");
}

#ifdef BSEC
// Internal function to process data
static PyObject *bme_bsec_process_data(BMEObject *self, bsec_bme_settings_t *sensor_settings, uint8_t i, int64_t time_stamp)
{
    uint8_t *n_bsec_inputs = 0;
    bsec_input_t inputs[BSEC_MAX_PHYSICAL_SENSOR];

    // Read the data into bsec_input_t[]
    if (sensor_settings->process_data)
    {
        /* Pressure to be processed by BSEC */
        if (sensor_settings->process_data & BSEC_PROCESS_PRESSURE)
        {
            printf("PRESSURE %f\n", self->data[i].pressure);
            /* Place presssure sample into input struct */
            inputs[*n_bsec_inputs].sensor_id = BSEC_INPUT_PRESSURE;
            inputs[*n_bsec_inputs].signal = self->data[i].pressure;
            inputs[*n_bsec_inputs].time_stamp = time_stamp;
            (*n_bsec_inputs)++;
        }
        /* Temperature to be processed by BSEC */
        if (sensor_settings->process_data & BSEC_PROCESS_TEMPERATURE)
        {
            printf("TEMPERATURE %f\n", self->data[i].temperature);
            /* Place temperature sample into input struct */
            inputs[*n_bsec_inputs].sensor_id = BSEC_INPUT_TEMPERATURE;
#ifdef BME68X_FLOAT_POINT_COMPENSATION
            inputs[*n_bsec_inputs].signal = self->data[i].temperature;
#else
            inputs[*n_bsec_inputs].signal = self->data[i].temperature / 100.0f;
#endif
            inputs[*n_bsec_inputs].time_stamp = time_stamp;
            (*n_bsec_inputs)++;

            /* Also add optional heatsource input which will be subtracted from the temperature reading to 
            * compensate for device-specific self-heating (supported in BSEC IAQ solution)*/
            inputs[*n_bsec_inputs].sensor_id = BSEC_INPUT_HEATSOURCE;
            inputs[*n_bsec_inputs].signal = self->temp_offset;
            inputs[*n_bsec_inputs].time_stamp = time_stamp;
            (*n_bsec_inputs)++;
        }
        /* Humidity to be processed by BSEC */
        if (sensor_settings->process_data & BSEC_PROCESS_HUMIDITY)
        {
            printf("HUMIDITY %f\n",self->data[i].humidity);
            /* Place humidity sample into input struct */
            inputs[*n_bsec_inputs].sensor_id = BSEC_INPUT_HUMIDITY;
#ifdef BME68X_FLOAT_POINT_COMPENSATION
            inputs[*n_bsec_inputs].signal = self->data[i].humidity;
#else
            inputs[*n_bsec_inputs].signal = self->data[i].humidity / 1000.0f;
#endif
            inputs[*n_bsec_inputs].time_stamp = time_stamp;
            (*n_bsec_inputs)++;
        }
        /* Gas to be processed by BSEC */
        if (sensor_settings->process_data & BSEC_PROCESS_GAS)
        {
            printf("GAS_RESISTANCE %f\n", self->data[i].gas_resistance);
            /* Check whether gas_valid flag is set */
            if (self->data[i].status & BME68X_GASM_VALID_MSK)
            {
                /* Place sample into input struct */
                inputs[*n_bsec_inputs].sensor_id = BSEC_INPUT_GASRESISTOR;
                inputs[*n_bsec_inputs].signal = self->data[i].gas_resistance;
                inputs[*n_bsec_inputs].time_stamp = time_stamp;
                (*n_bsec_inputs)++;
            }
        }
        /* Profile part */
        if (self->op_mode == BME68X_PARALLEL_MODE || self->op_mode == BME68X_SEQUENTIAL_MODE)
        {
            printf("PROFILE_PART %d\n", self->data[i].gas_index);
            inputs[*n_bsec_inputs].sensor_id = BSEC_INPUT_PROFILE_PART;
            inputs[*n_bsec_inputs].signal = self->data[i].gas_index;
            inputs[*n_bsec_inputs].time_stamp = time_stamp;
            (*n_bsec_inputs)++;
        }
    }
    printf("NUM BSEC INPUTS %d\n", *n_bsec_inputs);

    /* Output buffer set to the maximum virtual sensor outputs supported */
    bsec_output_t bsec_outputs[BSEC_NUMBER_OUTPUTS];
    uint8_t num_bsec_outputs = 0;

    PyObject *data = PyDict_New();
}

static PyObject *bme_get_digital_nose_data(BMEObject *self)
{
    // Create Timestamp and wait until measurement has to be triggered
    int64_t time_stamp = pi3g_timestamp_ns();

    // Check if bsec_sensor_controll needs to be called
    if (time_stamp >= self->next_call)
    {
        bsec_bme_settings_t sensor_settings;

        self->rslt = bsec_sensor_control(time_stamp, &sensor_settings);
        if (self->debug_mode == 1 && self->rslt != BSEC_OK)
        {
            printf("BSEC SENSOR CONTROL RSLT %d\n", self->rslt);
        }
        self->next_call = sensor_settings.next_call;

        /* Configure sensor */
        /* Set sensor configuration */
        self->rslt = pi3g_set_conf(sensor_settings.humidity_oversampling, sensor_settings.pressure_oversampling, sensor_settings.pressure_oversampling, BME68X_FILTER_OFF, BME68X_ODR_NONE, &(self->conf), &(self->bme), self->debug_mode);
        if (self->rslt < 0)
        {
            PyErr_SetString(bmeError, "FAILED TO SET CONFIG");
            return NULL;
        }

        self->rslt = pi3g_set_heater_conf_pm(sensor_settings.run_gas, sensor_settings.heater_temperature_profile, sensor_settings.heater_duration_profile, sensor_settings.heater_profile_len, &(self->conf), &(self->heatr_conf), &(self->bme), self->debug_mode);
        if (self->rslt < 0)
        {
            PyErr_SetString(bmeError, "FAILED TO SET HEATER CONFIG");
            return NULL;
        }
        uint8_t check_meas_index = 1;

        // In case measurement has to be triggered
        if(sensor_settings.trigger_measurement && sensor_settings.op_mode != BME68X_SLEEP_MODE)
        {
            /* Select the power mode */
            /* Must be set before writing the sensor configuration */
            self->op_mode = sensor_settings.op_mode;
            self->rslt = bme68x_set_op_mode(self->op_mode, &(self->bme));
            if (self->rslt != BME68X_OK)
            {
                perror("set_op_mode");
            }

            if (self->op_mode == BME68X_FORCED_MODE)
            {
                printf("WHY AM I IN FORCED MODE?\n");
                PyErr_SetString(bmeError, "Failed to get data\nSensor is in Forced mode but it needs to be in Parallel mode");
                return (PyObject *) NULL;
            }
            else
            {
                PyObject *pydata = PyList_New(self->heatr_conf.profile_len);
                PyObject *bsec_data = PyDict_New();
                uint8_t counter = 0;
                while (counter < self->heatr_conf.profile_len)
                {
                    self->del_period = bme68x_get_meas_dur(BME68X_PARALLEL_MODE, &(self->conf), &(self->bme)) + (self->heatr_conf.shared_heatr_dur * 1000);
                    self->bme.delay_us(self->del_period, self->bme.intf_ptr);
                    self->time_ms = pi3g_timestamp_ms();

                    self->rslt = bme68x_get_data(self->op_mode, self->data, &(self->n_fields), &(self->bme));
                    if (self->rslt < 0)
                    {
                        perror("bme68x_get_data");
                    }

                    /* Check if rslt == BME68X_OK, report or handle if otherwise */
                    for (uint8_t i = 0; i < self->n_fields; i++)
                    {
                        // if(self->data[i].status != BME68X_VALID_DATA)
                        // {
                        //     no_valid_data++;
                        // }

                        // if(no_valid_data == self->n_fields - 1 && self->data[i].status != BME68X_VALID_DATA)
                        // {
                        //     self->data[i] = self->data_backup;
                        //     uint8_t new_gas_index = (self->data_backup.gas_index + 1) % sensor_settings.heater_profile_len;
                        //     self->data[i].gas_index = new_gas_index;
                        // }

                        if (self->data[i].status & BME68X_GASM_VALID_MSK)
                        {
                            // Copy to self->data_backup in case of future invalid data
                            self->data_backup = self->data[i];
                            uint8_t gas_index_backup;
                            if (self->data_backup.gas_index <= 0)
                            {
                                gas_index_backup = sensor_settings.heater_profile_len;
                            }
                            else
                            {
                                gas_index_backup = self->data[i].gas_index -1;
                            }
                            self->data_backup.gas_index = gas_index_backup;
                            self->timestamp_backup = time_stamp;

                            /* Measurement index check to track the first valid sample after operation mode change */
                            if(check_meas_index)
                            {
                                /* After changing the operation mode, Measurement index expected to be zero
                                * however with considering the data miss case as well, condition shall be checked less
                                * than last received measurement index */
                                if(self->last_meas_index == 0 || self->data[i].meas_index == 0 ||  self->data[i].meas_index < self->last_meas_index)
                                {
                                    check_meas_index = false;
                                }
                                else
                                {
                                    continue; // Skip the invalid data samples or data from last duty cycle scan
                                }
                            }

                            self->last_meas_index = self->data[i].meas_index;

                            // We got valid data, time to bsec_do_steps
                            uint8_t n_bsec_inputs = 0;
                            bsec_input_t inputs[BSEC_MAX_PHYSICAL_SENSOR];

                            // Read the data into bsec_input_t[]
                            if (sensor_settings.process_data)
                            {
                                /* Pressure to be processed by BSEC */
                                if (sensor_settings.process_data & BSEC_PROCESS_PRESSURE)
                                {
                                    // printf("PRESSURE %f\n", self->data[i].pressure);
                                    /* Place presssure sample into input struct */
                                    inputs[n_bsec_inputs].sensor_id = BSEC_INPUT_PRESSURE;
                                    inputs[n_bsec_inputs].signal = self->data[i].pressure;
                                    inputs[n_bsec_inputs].time_stamp = time_stamp;
                                    n_bsec_inputs++;
                                }
                                /* Temperature to be processed by BSEC */
                                if (sensor_settings.process_data & BSEC_PROCESS_TEMPERATURE)
                                {
                                    // printf("TEMPERATURE %f\n", self->data[i].temperature);
                                    /* Place temperature sample into input struct */
                                    inputs[n_bsec_inputs].sensor_id = BSEC_INPUT_TEMPERATURE;
#ifdef BME68X_FLOAT_POINT_COMPENSATION
                                    inputs[n_bsec_inputs].signal = self->data[i].temperature;
#else
                                    inputs[n_bsec_inputs].signal = self->data[i].temperature / 100.0f;
#endif
                                    inputs[n_bsec_inputs].time_stamp = time_stamp;
                                    n_bsec_inputs++;

                                    /* Also add optional heatsource input which will be subtracted from the temperature reading to 
                                    * compensate for device-specific self-heating (supported in BSEC IAQ solution)*/
                                    inputs[n_bsec_inputs].sensor_id = BSEC_INPUT_HEATSOURCE;
                                    inputs[n_bsec_inputs].signal = self->temp_offset;
                                    inputs[n_bsec_inputs].time_stamp = time_stamp;
                                    n_bsec_inputs++;
                                }
                                /* Humidity to be processed by BSEC */
                                if (sensor_settings.process_data & BSEC_PROCESS_HUMIDITY)
                                {
                                    // printf("HUMIDITY %f\n",self->data[i].humidity);
                                    /* Place humidity sample into input struct */
                                    inputs[n_bsec_inputs].sensor_id = BSEC_INPUT_HUMIDITY;
#ifdef BME68X_FLOAT_POINT_COMPENSATION
                                    inputs[n_bsec_inputs].signal = self->data[i].humidity;
#else
                                    inputs[n_bsec_inputs].signal = self->data[i].humidity / 1000.0f;
#endif
                                    inputs[n_bsec_inputs].time_stamp = time_stamp;
                                    n_bsec_inputs++;
                                }
                                /* Gas to be processed by BSEC */
                                if (sensor_settings.process_data & BSEC_PROCESS_GAS)
                                {
                                    // printf("GAS_RESISTANCE %f\n", self->data[i].gas_resistance);
                                    /* Check whether gas_valid flag is set */
                                    if (self->data[i].status & BME68X_GASM_VALID_MSK)
                                    {
                                        /* Place sample into input struct */
                                        inputs[n_bsec_inputs].sensor_id = BSEC_INPUT_GASRESISTOR;
                                        inputs[n_bsec_inputs].signal = self->data[i].gas_resistance;
                                        inputs[n_bsec_inputs].time_stamp = time_stamp;
                                        n_bsec_inputs++;
                                    }
                                }
                                /* Profile part */
                                if (self->op_mode == BME68X_PARALLEL_MODE || self->op_mode == BME68X_SEQUENTIAL_MODE)
                                {
                                    // printf("PROFILE_PART %d\n", self->data[i].gas_index);
                                    inputs[n_bsec_inputs].sensor_id = BSEC_INPUT_PROFILE_PART;
                                    inputs[n_bsec_inputs].signal = self->data[i].gas_index;
                                    inputs[n_bsec_inputs].time_stamp = time_stamp;
                                    n_bsec_inputs++;
                                }
                            }
                            bsec_output_t bsec_outputs[BSEC_NUMBER_OUTPUTS];
                            uint8_t n_output = BSEC_NUMBER_OUTPUTS;

                            self->rslt = bsec_do_steps(inputs, n_bsec_inputs, bsec_outputs, &n_output);
                            if (self->rslt == BSEC_W_DOSTEPS_GASINDEXMISS)
                            {
                                printf("MISSED GAS INDEX %d\n", gas_index_backup);
                            }
                            else if (self->rslt != BSEC_OK)
                            {
                                printf("BSEC DO STEPS ERROR %d\nAT PROFILE PART %d\n", self->rslt, self->data[i].gas_index);
                                PyErr_SetString(bmeError, "BSEC Failed to process data");
                                return NULL;
                            }
                            else
                            {
                                // Read processed data into python Dict pydata
                                /* Iterate through the outputs and extract the relevant ones. */
                                self->sample_count++;
                                PyDict_SetItemString(bsec_data, "sample_nr", Py_BuildValue("i", self->sample_count));
                                PyDict_SetItemString(bsec_data, "timestamp", Py_BuildValue("L", time_stamp));
                                for (uint8_t index = 0; index < n_output; index++)
                                {
                                    switch (bsec_outputs[index].sensor_id)
                                    {
                                    case BSEC_OUTPUT_STABILIZATION_STATUS:
                                        PyDict_SetItemString(bsec_data, "stabilization_status", Py_BuildValue("i", bsec_outputs[index].signal));
                                        break;
                                    case BSEC_OUTPUT_RUN_IN_STATUS:
                                    PyDict_SetItemString(bsec_data, "run_in_status", Py_BuildValue("i", bsec_outputs[index].signal));
                                        break;
                                    case BSEC_OUTPUT_IAQ:
                                        PyDict_SetItemString(bsec_data, "iaq", Py_BuildValue("d", bsec_outputs[index].signal));
                                        PyDict_SetItemString(bsec_data, "iaq_accuracy", Py_BuildValue("i", bsec_outputs[index].accuracy));
                                        break;
                                    case BSEC_OUTPUT_STATIC_IAQ:
                                        PyDict_SetItemString(bsec_data, "static_iaq", Py_BuildValue("d", bsec_outputs[index].signal));
                                        PyDict_SetItemString(bsec_data, "static_iaq_accuracy", Py_BuildValue("i", bsec_outputs[index].accuracy));
                                        break;
                                    case BSEC_OUTPUT_CO2_EQUIVALENT:
                                        PyDict_SetItemString(bsec_data, "co2_equivalent", Py_BuildValue("d", bsec_outputs[index].signal));
                                        PyDict_SetItemString(bsec_data, "co2_accuracy", Py_BuildValue("i", bsec_outputs[index].accuracy));
                                        break;
                                    case BSEC_OUTPUT_BREATH_VOC_EQUIVALENT:
                                        PyDict_SetItemString(bsec_data, "breath_voc_equivalent", Py_BuildValue("d", bsec_outputs[index].signal));
                                        PyDict_SetItemString(bsec_data, "breath_voc_accuracy", Py_BuildValue("i", bsec_outputs[index].accuracy));
                                        break;
                                    case BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE:
                                        PyDict_SetItemString(bsec_data, "temperature", Py_BuildValue("d", bsec_outputs[index].signal));
                                        break;
                                    case BSEC_OUTPUT_RAW_PRESSURE:
                                        PyDict_SetItemString(bsec_data, "raw_pressure", Py_BuildValue("d", bsec_outputs[index].signal));
                                        break;
                                    case BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY:
                                        PyDict_SetItemString(bsec_data, "humidity", Py_BuildValue("d", bsec_outputs[index].signal));
                                        break;
                                    case BSEC_OUTPUT_RAW_GAS:
                                        PyDict_SetItemString(bsec_data, "raw_gas", Py_BuildValue("d", bsec_outputs[index].signal));
                                        break;
                                    case BSEC_OUTPUT_RAW_TEMPERATURE:
                                        PyDict_SetItemString(bsec_data, "raw_temperature", Py_BuildValue("d", bsec_outputs[index].signal));
                                        break;
                                    case BSEC_OUTPUT_RAW_HUMIDITY:
                                        PyDict_SetItemString(bsec_data, "raw_humidity", Py_BuildValue("d", bsec_outputs[index].signal));
                                        break;
                                    case BSEC_OUTPUT_COMPENSATED_GAS:
                                        PyDict_SetItemString(bsec_data, "comp_gas_value", Py_BuildValue("d", bsec_outputs[index].signal));
                                        PyDict_SetItemString(bsec_data, "comp_gas_accuracy", Py_BuildValue("i", bsec_outputs[index].accuracy));
                                        break;
                                    case BSEC_OUTPUT_GAS_PERCENTAGE:
                                        PyDict_SetItemString(bsec_data, "gas_percentage", Py_BuildValue("d", bsec_outputs[index].signal));
                                        PyDict_SetItemString(bsec_data, "gas_percentage_accuracy", Py_BuildValue("i", bsec_outputs[index].accuracy));
                                        break;
                                    case BSEC_OUTPUT_RAW_GAS_INDEX:
                                        PyDict_SetItemString(bsec_data, "raw_gas_index", Py_BuildValue("d", bsec_outputs[index].signal));
                                        break;
                                    case BSEC_OUTPUT_GAS_ESTIMATE_1:
                                        PyDict_SetItemString(bsec_data, "gas_estimate_1", Py_BuildValue("d", bsec_outputs[index].signal));
                                        PyDict_SetItemString(bsec_data, "gas_estimate_1_accuracy", Py_BuildValue("i", bsec_outputs[index].accuracy));
                                        break;
                                    case BSEC_OUTPUT_GAS_ESTIMATE_2:
                                        PyDict_SetItemString(bsec_data, "gas_estimate_2", Py_BuildValue("d", bsec_outputs[index].signal));
                                        PyDict_SetItemString(bsec_data, "gas_estimate_2_accuracy", Py_BuildValue("i", bsec_outputs[index].accuracy));
                                        break;
                                    case BSEC_OUTPUT_GAS_ESTIMATE_3:
                                        PyDict_SetItemString(bsec_data, "gas_estimate_3", Py_BuildValue("d", bsec_outputs[index].signal));
                                        PyDict_SetItemString(bsec_data, "gas_estimate_3_accuracy", Py_BuildValue("i", bsec_outputs[index].accuracy));
                                        break;
                                    case BSEC_OUTPUT_GAS_ESTIMATE_4:
                                        PyDict_SetItemString(bsec_data, "gas_estimate_4", Py_BuildValue("d", bsec_outputs[index].signal));
                                        PyDict_SetItemString(bsec_data, "gas_estimate_4_accuracy", Py_BuildValue("i", bsec_outputs[index].accuracy));
                                        break;
                                    default:
                                        continue;
                                    }
                                }
                            }
                            PyList_SetItem(pydata, counter, bsec_data);
                            counter++;
                        }
                    }
                }
                // self->bme.amb_temp = self->data[0].temperature - self->temp_offset;
                return pydata;
            }
        }
    }

    Py_RETURN_NONE;
}

static PyObject *bme_get_bsec_data(BMEObject *self)
{
    int64_t time_stamp = pi3g_timestamp_ns();
    bsec_bme_settings_t sensor_settings;

    uint8_t trigger = 0;
    uint32_t count = 0;

    while (trigger == 0){
        self->rslt = bsec_sensor_control(time_stamp, &sensor_settings);
        if (self->debug_mode == 1 && self->rslt != BSEC_OK)
        {
            printf("BSEC SENSOR CONTROL RSLT %d\n", self->rslt);
        }
        trigger = sensor_settings.trigger_measurement;
        printf("%d\n", count++);

        usleep(500);
    }

    printf("OP_MODE %d\nRUN_GAS %d\nPROCESS_DATA %d\nNEXT_CALL %lld\n", sensor_settings.op_mode, sensor_settings.run_gas, sensor_settings.process_data, sensor_settings.next_call);
    
    printf("TEMP_PROF\n");
    for (uint8_t i = 0; i < sensor_settings.heater_profile_len; i++)
    {
        printf("%d ", sensor_settings.heater_temperature_profile[i]);
    }
    printf("\n");
    printf("DUR_PROF\n");
    for (uint8_t i = 0; i < sensor_settings.heater_profile_len; i++)
    {
        printf("%d ", sensor_settings.heater_duration_profile[i]);
    }
    printf("\n");

    printf("TIME %d\n", pi3g_timestamp_ms());

    uint16_t meas_dur;
    self->rslt = BME68X_OK;

    printf("TRIGGER MEASUREMENT %d\n", sensor_settings.trigger_measurement);
    /* Check if a forced-mode measurement should be triggered now */
    if (sensor_settings.trigger_measurement)
    {
        printf("INSIDE LOOP\n");

        /* Select the power mode */
        /* Must be set before writing the sensor configuration */
        self->op_mode = sensor_settings.op_mode;
        self->rslt = bme68x_set_op_mode(self->op_mode, &(self->bme));
        printf("SET OP MODE\n");

        /* Set sensor configuration */
        self->conf.os_hum = sensor_settings.humidity_oversampling;
        self->conf.os_pres = sensor_settings.pressure_oversampling;
        self->conf.os_temp = sensor_settings.temperature_oversampling;
        self->rslt = bme68x_set_conf(&(self->conf), &(self->bme));
        if (self->rslt < 0)
        {
            PyErr_SetString(bmeError, "FAILED TO SET CONFIG");
            return NULL;
        }

        self->heatr_conf.enable = sensor_settings.run_gas;
        self->heatr_conf.heatr_temp = sensor_settings.heater_temperature; /* degree Celsius */
        self->heatr_conf.heatr_dur = sensor_settings.heater_duration;     /* milliseconds */
        self->heatr_conf.heatr_temp_prof = sensor_settings.heater_temperature_profile;
        self->heatr_conf.heatr_dur_prof = sensor_settings.heater_duration_profile;
        self->heatr_conf.shared_heatr_dur = 140 - (bme68x_get_meas_dur(self->op_mode, &(self->conf), &(self->bme)) / 1000);
        self->heatr_conf.profile_len = sensor_settings.heater_profile_len;
        self->rslt = bme68x_set_heatr_conf(sensor_settings.op_mode, &(self->heatr_conf), &(self->bme));
        self->rslt = bme68x_set_heatr_conf(sensor_settings.op_mode, &(self->heatr_conf), &(self->bme));
        if (self->rslt < 0)
        {
            PyErr_SetString(bmeError, "FAILED TO SET HEATER CONFIG");
            return NULL;
        }

        /* Get the total measurement duration so as to sleep or wait till the measurement is complete */
        meas_dur = bme68x_get_meas_dur(self->op_mode, &(self->conf), &(self->bme));
        printf("MEAS DUR %d\n", meas_dur);

        /* Delay till the measurement is ready. Timestamp resolution in ms */
        usleep((uint32_t)meas_dur);

        /* Call the API to get current operation mode of the sensor */
        self->rslt = bme68x_get_op_mode(&(self->op_mode), &(self->bme));
        /* When the measurement is completed and data is ready for reading, the sensor must be in BME680_SLEEP_MODE.
         * Read operation mode to check whether measurement is completely done and wait until the sensor is no more
         * in BME680_FORCED_MODE. */
        while (self->op_mode != BME68X_SLEEP_MODE)
        {
            /* sleep for 5 ms */
            usleep(5000);
            self->rslt = bme68x_get_op_mode(&(self->op_mode), &(self->bme));
        }
        uint8_t num_bsec_inputs = 0;
        bsec_input_t bsec_inputs[BSEC_MAX_PHYSICAL_SENSOR];
        printf("READING BSEC DATA\n");



//         if (bsec_process_data)
//     {
//         bsec_library_return_t bsec_rslt = bme68x_get_data(self->op_mode, &(self->data), &(self->n_fields), &(self->bme));
//         for (int i = 0; i < *n_fields; i++)
//         {
//             if (self->data[i].status & BME68X_NEW_DATA_MSK)
//             {
//                 /* Pressure to be processed by BSEC */
//                 if (bsec_process_data & BSEC_PROCESS_PRESSURE)
//                 {
//                     /* Place presssure sample into input struct */
//                     inputs[*n_bsec_inputs].sensor_id = BSEC_INPUT_PRESSURE;
//                     inputs[*n_bsec_inputs].signal = self->data[i].pressure;
//                     inputs[*n_bsec_inputs].time_stamp = time_stamp;
//                     (*n_bsec_inputs)++;
//                 }
//                 /* Temperature to be processed by BSEC */
//                 if (bsec_process_data & BSEC_PROCESS_TEMPERATURE)
//                 {
//                     /* Place temperature sample into input struct */
//                     inputs[*n_bsec_inputs].sensor_id = BSEC_INPUT_TEMPERATURE;
// #ifdef BME68X_FLOAT_POINT_COMPENSATION
//                     inputs[*n_bsec_inputs].signal = self->data[i].temperature;
// #else
//                     inputs[*n_bsec_inputs].signal = self->data[i].temperature / 100.0f;
// #endif
//                     inputs[*n_bsec_inputs].time_stamp = time_stamp;
//                     (*n_bsec_inputs)++;

//                     /* Also add optional heatsource input which will be subtracted from the temperature reading to 
//                      * compensate for device-specific self-heating (supported in BSEC IAQ solution)*/
//                     inputs[*n_bsec_inputs].sensor_id = BSEC_INPUT_HEATSOURCE;
//                     inputs[*n_bsec_inputs].signal = TEMP_OFFSET;
//                     inputs[*n_bsec_inputs].time_stamp = time_stamp;
//                     (*n_bsec_inputs)++;
//                 }
//                 /* Humidity to be processed by BSEC */
//                 if (bsec_process_data & BSEC_PROCESS_HUMIDITY)
//                 {
//                     /* Place humidity sample into input struct */
//                     inputs[*n_bsec_inputs].sensor_id = BSEC_INPUT_HUMIDITY;
// #ifdef BME68X_FLOAT_POINT_COMPENSATION
//                     inputs[*n_bsec_inputs].signal = self->data[i].humidity;
// #else
//                     inputs[*n_bsec_inputs].signal = self->data[i].humidity / 1000.0f;
// #endif
//                     inputs[*n_bsec_inputs].time_stamp = time_stamp;
//                     (*n_bsec_inputs)++;
//                 }
//                 /* Gas to be processed by BSEC */
//                 if (bsec_process_data & BSEC_PROCESS_GAS)
//                 {
//                     /* Check whether gas_valid flag is set */
//                     if (self->data[i].status & BME68X_GASM_VALID_MSK)
//                     {
//                         /* Place sample into input struct */
//                         inputs[*n_bsec_inputs].sensor_id = BSEC_INPUT_GASRESISTOR;
//                         inputs[*n_bsec_inputs].signal = self->data[i].gas_resistance;
//                         inputs[*n_bsec_inputs].time_stamp = time_stamp;
//                         (*n_bsec_inputs)++;
//                     }
//                 }
//                 /* Profile part */
//                 if (self->op_mode == BME68X_PARALLEL_MODE || op_mode == BME68X_SEQUENTIAL_MODE)
//                 {
//                     inputs[*n_bsec_inputs].sensor_id = BSEC_INPUT_PROFILE_PART;
//                     inputs[*n_bsec_inputs].signal = self->data[i].gas_index;
//                     inputs[*n_bsec_inputs].time_stamp = time_stamp;
//                     (*n_bsec_inputs)++;
//                 }
//             }
//         }
//     }
//     return BSEC_OK;


        self->rslt = bsec_read_data(self->data, time_stamp, bsec_inputs, &num_bsec_inputs, sensor_settings.process_data, sensor_settings.op_mode, &(self->bme), self->temp_offset);
        if (self->rslt != BSEC_OK)
        {
            perror("read BSEC data");
            PyErr_SetString(bmeError, "FAILED TO READ BSEC DATA");
            return NULL;
        }
        // bsec_process_data(bsec_inputs, num_bsec_inputs);
        printf("READ BSEC DATA\n");
        for (uint8_t i = 0; i < num_bsec_inputs; i++)
        {
            printf("ID %d VAL %.2f TIME %lld\n", bsec_inputs[i].sensor_id, bsec_inputs[i].signal, bsec_inputs[i].time_stamp);
        }

        /* Output buffer set to the maximum virtual sensor outputs supported */
        bsec_output_t bsec_outputs[BSEC_NUMBER_OUTPUTS];
        uint8_t num_bsec_outputs = 0;
        uint8_t index = 0;

        bsec_library_return_t bsec_status = BSEC_OK;

        int64_t timestamp = 0;
        uint8_t stabilization_status = 0;
        uint8_t run_in_status = 0;
        float iaq = 0.0f;
        uint8_t iaq_accuracy = 0;
        float temp = 0.0f;
        float raw_temp = 0.0f;
        float raw_pressure = 0.0f;
        float humidity = 0.0f;
        float raw_humidity = 0.0f;
        float raw_gas = 0.0f;
        float static_iaq = 0.0f;
        uint8_t static_iaq_accuracy = 0;
        float co2_equivalent = 0.0f;
        uint8_t co2_accuracy = 0;
        float breath_voc_equivalent = 0.0f;
        uint8_t breath_voc_accuracy = 0;
        float comp_gas_value = 0.0f;
        uint8_t comp_gas_accuracy = 0;
        float gas_percentage = 0.0f;
        uint8_t gas_percentage_accuracy = 0;
        float raw_gas_index = 0.0f;
        float gas_estimate_1 = 0.0f;
        uint8_t gas_estimate_1_accuracy = 0;
        float gas_estimate_2 = 0.0f;
        uint8_t gas_estimate_2_accuracy = 0;
        float gas_estimate_3 = 0.0f;
        uint8_t gas_estimate_3_accuracy = 0;
        float gas_estimate_4 = 0.0f;
        uint8_t gas_estimate_4_accuracy = 0;

        /* Check if something should be processed by BSEC */
        if (num_bsec_inputs > 0)
        {
            /* Set number of outputs to the size of the allocated buffer */
            /* BSEC_NUMBER_OUTPUTS to be defined */
            printf("BSEC_NUMBER_OUTPUTS %d\n", BSEC_NUMBER_OUTPUTS);
            printf("num_bsec_inputs %d\n", num_bsec_inputs);

            /* Perform processing of the data by BSEC 
           Note:
           * The number of outputs you get depends on what you asked for during bsec_update_subscription(). This is
             handled under bme680_bsec_update_subscription() function in this example file.
           * The number of actual outputs that are returned is written to num_bsec_outputs. */
            bsec_status = bsec_do_steps(bsec_inputs, num_bsec_inputs, bsec_outputs, &num_bsec_outputs);
            if (bsec_status != BSEC_OK)
            {
                perror("bsec_do_steps");
                printf("BSEC STATUS AFTER DO STEPS %d\n", bsec_status);
                PyErr_SetString(bmeError, "Failed to process data");
                return NULL;
            }
            printf("num_bsec_outputs after bsec_do_steps %d\n", num_bsec_outputs);

            /* Iterate through the outputs and extract the relevant ones. */
            for (index = 0; index < num_bsec_outputs; index++)
            {
                switch (bsec_outputs[index].sensor_id)
                {
                case BSEC_OUTPUT_STABILIZATION_STATUS:
                    stabilization_status = bsec_outputs[index].signal;
                    break;
                case BSEC_OUTPUT_RUN_IN_STATUS:
                    run_in_status = bsec_outputs[index].signal;
                    break;
                case BSEC_OUTPUT_IAQ:
                    iaq = bsec_outputs[index].signal;
                    iaq_accuracy = bsec_outputs[index].accuracy;
                    break;
                case BSEC_OUTPUT_STATIC_IAQ:
                    static_iaq = bsec_outputs[index].signal;
                    static_iaq_accuracy = bsec_outputs[index].accuracy;
                    break;
                case BSEC_OUTPUT_CO2_EQUIVALENT:
                    co2_equivalent = bsec_outputs[index].signal;
                    co2_accuracy = bsec_outputs[index].accuracy;
                    break;
                case BSEC_OUTPUT_BREATH_VOC_EQUIVALENT:
                    breath_voc_equivalent = bsec_outputs[index].signal;
                    breath_voc_accuracy = bsec_outputs[index].accuracy;
                    break;
                case BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE:
                    temp = bsec_outputs[index].signal;
                    break;
                case BSEC_OUTPUT_RAW_PRESSURE:
                    raw_pressure = bsec_outputs[index].signal;
                    break;
                case BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY:
                    humidity = bsec_outputs[index].signal;
                    break;
                case BSEC_OUTPUT_RAW_GAS:
                    raw_gas = bsec_outputs[index].signal;
                    break;
                case BSEC_OUTPUT_RAW_TEMPERATURE:
                    raw_temp = bsec_outputs[index].signal;
                    break;
                case BSEC_OUTPUT_RAW_HUMIDITY:
                    raw_humidity = bsec_outputs[index].signal;
                    break;
                case BSEC_OUTPUT_COMPENSATED_GAS:
                    comp_gas_value = bsec_outputs[index].signal;
                    comp_gas_accuracy = bsec_outputs[index].accuracy;
                    break;
                case BSEC_OUTPUT_GAS_PERCENTAGE:
                    gas_percentage = bsec_outputs[index].signal;
                    gas_percentage_accuracy = bsec_outputs[index].accuracy;
                    break;
                case BSEC_OUTPUT_RAW_GAS_INDEX:
                    raw_gas_index = bsec_outputs[index].signal;
                    break;
                case BSEC_OUTPUT_GAS_ESTIMATE_1:
                    gas_estimate_1 = bsec_outputs[index].signal;
                    gas_estimate_1_accuracy = bsec_outputs[index].accuracy;
                    break;
                case BSEC_OUTPUT_GAS_ESTIMATE_2:
                    gas_estimate_2 = bsec_outputs[index].signal;
                    gas_estimate_2_accuracy = bsec_outputs[index].accuracy;
                    break;
                case BSEC_OUTPUT_GAS_ESTIMATE_3:
                    gas_estimate_3 = bsec_outputs[index].signal;
                    gas_estimate_3_accuracy = bsec_outputs[index].accuracy;
                    break;
                case BSEC_OUTPUT_GAS_ESTIMATE_4:
                    gas_estimate_4 = bsec_outputs[index].signal;
                    gas_estimate_4_accuracy = bsec_outputs[index].accuracy;
                    break;
                default:
                    continue;
                }

                /* Assume that all the returned timestamps are the same */
                timestamp = bsec_outputs[index].time_stamp;
            }

            n_samples++;

            time_stamp_interval_us = (sensor_settings.next_call - pi3g_timestamp_ns()) / 1000;
            if (time_stamp_interval_us > 0)
            {
                usleep((uint32_t)time_stamp_interval_us);
            }
        }

        PyObject *bsec_data = PyDict_New();
        PyDict_SetItemString(bsec_data, "sample_nr", Py_BuildValue("i", n_samples));
        PyDict_SetItemString(bsec_data, "timestamp", Py_BuildValue("L", timestamp));
        PyDict_SetItemString(bsec_data, "iaq", Py_BuildValue("d", iaq));
        PyDict_SetItemString(bsec_data, "iaq_accuracy", Py_BuildValue("i", iaq_accuracy));
        PyDict_SetItemString(bsec_data, "temperature", Py_BuildValue("d", temp * 100));
        PyDict_SetItemString(bsec_data, "raw_temperature", Py_BuildValue("d", raw_temp * 100));
        PyDict_SetItemString(bsec_data, "raw_pressure", Py_BuildValue("d", raw_pressure / 100));
        PyDict_SetItemString(bsec_data, "humidity", Py_BuildValue("d", humidity * 1000));
        PyDict_SetItemString(bsec_data, "raw_humidity", Py_BuildValue("d", raw_humidity * 1000));
        PyDict_SetItemString(bsec_data, "raw_gas", Py_BuildValue("d", raw_gas / 1000));
        PyDict_SetItemString(bsec_data, "static_iaq", Py_BuildValue("d", static_iaq));
        PyDict_SetItemString(bsec_data, "static_iaq_accuracy", Py_BuildValue("i", static_iaq_accuracy));
        PyDict_SetItemString(bsec_data, "co2_equivalent", Py_BuildValue("d", co2_equivalent));
        PyDict_SetItemString(bsec_data, "co2_accuracy", Py_BuildValue("i", co2_accuracy));
        PyDict_SetItemString(bsec_data, "breath_voc_equivalent", Py_BuildValue("d", breath_voc_equivalent));
        PyDict_SetItemString(bsec_data, "breath_voc_accuracy", Py_BuildValue("i", breath_voc_accuracy));
        PyDict_SetItemString(bsec_data, "comp_gas_value", Py_BuildValue("d", comp_gas_value));
        PyDict_SetItemString(bsec_data, "comp_gas_accuracy", Py_BuildValue("i", comp_gas_accuracy));
        PyDict_SetItemString(bsec_data, "gas_percentage", Py_BuildValue("d", gas_percentage));
        PyDict_SetItemString(bsec_data, "gas_percentage_accuracy", Py_BuildValue("i", gas_percentage_accuracy));
        PyDict_SetItemString(bsec_data, "gas_estimate_1", Py_BuildValue("d", gas_estimate_1));
        PyDict_SetItemString(bsec_data, "gas_estimate_1_accuracy", Py_BuildValue("i", gas_estimate_1_accuracy));
        PyDict_SetItemString(bsec_data, "gas_estimate_2", Py_BuildValue("d", gas_estimate_2));
        PyDict_SetItemString(bsec_data, "gas_estimate_2_accuracy", Py_BuildValue("i", gas_estimate_2_accuracy));
        PyDict_SetItemString(bsec_data, "gas_estimate_3", Py_BuildValue("d", gas_estimate_3));
        PyDict_SetItemString(bsec_data, "gas_estimate_3_accuracy", Py_BuildValue("i", gas_estimate_3_accuracy));
        PyDict_SetItemString(bsec_data, "gas_estimate_4", Py_BuildValue("d", gas_estimate_4));
        PyDict_SetItemString(bsec_data, "gas_estimate_4_accuracy", Py_BuildValue("i", gas_estimate_4_accuracy));

        return bsec_data;
    }
    PyErr_SetString(bmeError, "No measurement triggered");
    return NULL;
}

static PyObject *bme_get_bsec_version(BMEObject *self)
{
    bsec_version_t version;
    bsec_get_version(&version);
    char buffer[14];
    sprintf(buffer, "%d.%d.%d.%d", version.major, version.minor, version.major_bugfix, version.minor_bugfix);
    return Py_BuildValue("s", buffer);
}

static PyObject *bme_get_bsec_conf(BMEObject *self)
{
    uint8_t conf_set_id = 0;
    uint8_t serialized_settings[BSEC_MAX_PROPERTY_BLOB_SIZE];
    uint32_t n_serialized_settings_max = BSEC_MAX_PROPERTY_BLOB_SIZE;
    uint8_t work_buffer[BSEC_MAX_PROPERTY_BLOB_SIZE];
    uint32_t n_work_buffer = BSEC_MAX_PROPERTY_BLOB_SIZE;
    uint32_t n_serialized_settings = 0;

    self->rslt = bsec_get_configuration(conf_set_id, serialized_settings, n_serialized_settings_max, work_buffer, n_work_buffer, &n_serialized_settings);

    if (self->rslt != BSEC_OK) {
        PyErr_SetString(bmeError, "Failed to read BSEC conf");
        return NULL;
    }

    // Create and populate Python List Object
    PyObject *conf_list = PyList_New(BSEC_MAX_PROPERTY_BLOB_SIZE);
    for (uint32_t i = 0; i < BSEC_MAX_PROPERTY_BLOB_SIZE; i++) {
        PyList_SetItem(conf_list, i, Py_BuildValue("i", serialized_settings[i]));
    }

    return conf_list;
}

static PyObject *bme_set_bsec_conf(BMEObject *self, PyObject *args)
{
    PyObject *conf_list_obj;

    if (!PyArg_ParseTuple(args, "O", &conf_list_obj))
    {
        PyErr_SetString(bmeError, "Argument must be list of BSEC_MAX_PROPERTY_BLOB_SIZE (2277) integers");
        return (PyObject *) NULL;
    }

    if (!PyList_Check(conf_list_obj))
    {
        PyErr_SetString(bmeError, "Argument must be list of BSEC_MAX_PROPERTY_BLOB_SIZE (2277) integers");
        return (PyObject *) NULL;
    }

    uint32_t conf_size = PyList_Size(conf_list_obj);
    if (conf_size != BSEC_MAX_PROPERTY_BLOB_SIZE)
    {
        PyErr_SetString(bmeError, "Argument must be list of BSEC_MAX_PROPERTY_BLOB_SIZE (2277) integers");
        return (PyObject *) NULL;
    }

    uint8_t serialized_settings[BSEC_MAX_PROPERTY_BLOB_SIZE];
    uint32_t n_serialized_settings_max = BSEC_MAX_PROPERTY_BLOB_SIZE;
    uint8_t work_buffer[BSEC_MAX_PROPERTY_BLOB_SIZE];
    uint32_t n_work_buffer = BSEC_MAX_PROPERTY_BLOB_SIZE;

    PyObject *val;
    for (uint32_t i = 0; i < BSEC_MAX_PROPERTY_BLOB_SIZE; i++)
    {
        val = PyList_GetItem(conf_list_obj, i);
        serialized_settings[i] = (uint8_t)PyLong_AsLong(val);
    }

    // Apply the configuration
    self->rslt = bsec_set_configuration(serialized_settings, n_serialized_settings_max, work_buffer, n_work_buffer);
    if (self->rslt != BSEC_OK)
    {
        PyErr_SetString(bmeError, "Could not set BSEC config");
        return (PyObject *) NULL;
    }

    if (self->debug_mode == 1)
    {
        printf("SET BSEC CONF RLST %d\n", self->rslt);
    }

    return Py_BuildValue("i", self->rslt);
}

static PyObject *bme_get_bsec_state(BMEObject *self)
{
    // state set id = 0 to retrieve all states
    uint8_t state_set_id = 0;
    uint8_t serialized_state[BSEC_MAX_STATE_BLOB_SIZE];
    uint32_t n_serialized_state_max = BSEC_MAX_STATE_BLOB_SIZE;
    uint32_t n_serialized_state = BSEC_MAX_STATE_BLOB_SIZE;
    uint8_t work_buffer_state[BSEC_MAX_STATE_BLOB_SIZE];
    uint32_t n_work_buffer_state = BSEC_MAX_STATE_BLOB_SIZE;


    // Get BSEC state and read it into serialized state
    self->rslt = bsec_get_state(state_set_id, serialized_state, n_serialized_state_max, work_buffer_state, n_work_buffer_state, &n_serialized_state);

    if (self->rslt != BSEC_OK) {
        PyErr_SetString(bmeError, "Failed to read BSEC state");
        return (PyObject *) NULL;
    }

    // Create and populate Python List Object
    PyObject *state_list = PyList_New(BSEC_MAX_STATE_BLOB_SIZE);
    for (uint32_t i = 0; i < BSEC_MAX_STATE_BLOB_SIZE; i++) {
        PyList_SetItem(state_list, i, Py_BuildValue("i", serialized_state[i]));
    }

    return state_list;
}

static PyObject *bme_set_bsec_state(BMEObject *self, PyObject *args)
{
    PyObject *state_list_obj;

    if (!PyArg_ParseTuple(args, "O", &state_list_obj))
    {
        PyErr_SetString(bmeError, "Argument must be list of BSEC_MAX_STATE_BLOB_SIZE (197) integers");
        return (PyObject *) NULL;
    }

    if (!PyList_Check(state_list_obj))
    {
        PyErr_SetString(bmeError, "Argument must be list of BSEC_MAX_STATE_BLOB_SIZE (197) integers");
        return (PyObject *) NULL;
    }

    uint32_t state_size = PyList_Size(state_list_obj);
    if (state_size > BSEC_MAX_STATE_BLOB_SIZE)
    {
        PyErr_SetString(bmeError, "Argument must be list of BSEC_MAX_STATE_BLOB_SIZE (197) integers");
        return (PyObject *) NULL;
    }

    uint8_t serialized_state[BSEC_MAX_STATE_BLOB_SIZE];
    uint32_t n_serialized_state_max = BSEC_MAX_STATE_BLOB_SIZE;
    uint8_t work_buffer[BSEC_MAX_STATE_BLOB_SIZE];
    uint32_t n_work_buffer = BSEC_MAX_STATE_BLOB_SIZE;

    PyObject *val;
    for (uint32_t i = 0; i < BSEC_MAX_STATE_BLOB_SIZE; i++)
    {
        val = PyList_GetItem(state_list_obj, i);
        serialized_state[i] = (uint8_t)PyLong_AsLong(val);
    }

    // Apply the configuration
    self->rslt = bsec_set_state(serialized_state, n_serialized_state_max, work_buffer, n_work_buffer);
    if (self->rslt != BSEC_OK)
    {
        PyErr_SetString(bmeError, "Could not set BSEC state");
        return (PyObject *) NULL;
    }

    if (self->debug_mode == 1)
    {
        printf("SET BSEC STATE\n");
        printf("RSLT %d\n", self->rslt);
    }
    return Py_BuildValue("i", self->rslt);
}

static PyObject *bme_update_bsec_subscription(BMEObject *self, PyObject *args)
{
    // Check if argument is a list
    PyObject *list_obj;

    if(!PyArg_ParseTuple(args, "O", &list_obj))
    {
        PyErr_SetString(bmeError, "Failed to parse Argument");
        return (PyObject *) NULL;
    }

    if (!PyList_Check(list_obj))
    {
        PyErr_SetString(bmeError, "Argument must be a List");
        return (PyObject *) NULL;
    }
    // Allocate required memory
    uint8_t len = (uint8_t) PyList_Size(list_obj);

    uint8_t n_requested_virtual_sensors;
    n_requested_virtual_sensors = len;
    bsec_sensor_configuration_t requested_virtual_sensors[n_requested_virtual_sensors];

    bsec_sensor_configuration_t required_sensor_settings[BSEC_MAX_PHYSICAL_SENSOR];
    uint8_t n_required_sensor_settings = BSEC_MAX_PHYSICAL_SENSOR;

    // Loop over list and parse into requested_virtual_sensors
    PyObject *val;
    for (uint8_t i = 0; i < len; i++){
        val = PyList_GetItem(list_obj, i);
        if (!PyArg_ParseTuple(val, "bf", &(requested_virtual_sensors[i].sensor_id), &(requested_virtual_sensors[i].sample_rate)))
        {
            PyErr_SetString(bmeError, "List items must be tuples (sensor_id, sample_rate)");
            return (PyObject *) NULL;
        }
    }

    // Call bsec_update_subscription and return rslt
    self->rslt = bsec_update_subscription(requested_virtual_sensors, n_requested_virtual_sensors, required_sensor_settings, &n_required_sensor_settings);
    
    return Py_BuildValue("i", self->rslt);
}

static PyObject *bme_enable_gas_estimates(BMEObject *self)
{
    uint8_t n_requested_virtual_sensors;
    n_requested_virtual_sensors = 4;
    bsec_sensor_configuration_t requested_virtual_sensors[n_requested_virtual_sensors];
    bsec_sensor_configuration_t required_sensor_settings[BSEC_MAX_PHYSICAL_SENSOR];
    uint8_t n_required_sensor_settings = BSEC_MAX_PHYSICAL_SENSOR;

    requested_virtual_sensors[0].sensor_id = BSEC_OUTPUT_GAS_ESTIMATE_1;
    requested_virtual_sensors[0].sample_rate = BSEC_SAMPLE_RATE_HIGH_PERFORMANCE;
    requested_virtual_sensors[1].sensor_id = BSEC_OUTPUT_GAS_ESTIMATE_2;
    requested_virtual_sensors[1].sample_rate = BSEC_SAMPLE_RATE_HIGH_PERFORMANCE;
    requested_virtual_sensors[2].sensor_id = BSEC_OUTPUT_GAS_ESTIMATE_3;
    requested_virtual_sensors[2].sample_rate = BSEC_SAMPLE_RATE_HIGH_PERFORMANCE;
    requested_virtual_sensors[3].sensor_id = BSEC_OUTPUT_GAS_ESTIMATE_4;
    requested_virtual_sensors[3].sample_rate = BSEC_SAMPLE_RATE_HIGH_PERFORMANCE;
    
    self->rslt = bsec_update_subscription(requested_virtual_sensors, n_requested_virtual_sensors, required_sensor_settings, &n_required_sensor_settings);
    printf("ENABLE GAS ESTIMATES RSLT %d\n", self->rslt);
    if (self->rslt != BSEC_OK)
    {
        PyErr_SetString(bmeError, "Failed to subscribe gas estimates");
        return (PyObject *) NULL;
    }
    return Py_BuildValue("i", self->rslt);
}

static PyObject *bme_disable_gas_estimates(BMEObject *self)
{
    uint8_t n_requested_virtual_sensors;
    n_requested_virtual_sensors = 4;
    bsec_sensor_configuration_t requested_virtual_sensors[n_requested_virtual_sensors];
    bsec_sensor_configuration_t required_sensor_settings[BSEC_MAX_PHYSICAL_SENSOR];
    uint8_t n_required_sensor_settings = BSEC_MAX_PHYSICAL_SENSOR;

    requested_virtual_sensors[0].sensor_id = BSEC_OUTPUT_GAS_ESTIMATE_1;
    requested_virtual_sensors[0].sample_rate = BSEC_SAMPLE_RATE_DISABLED;
    requested_virtual_sensors[1].sensor_id = BSEC_OUTPUT_GAS_ESTIMATE_2;
    requested_virtual_sensors[1].sample_rate = BSEC_SAMPLE_RATE_DISABLED;
    requested_virtual_sensors[2].sensor_id = BSEC_OUTPUT_GAS_ESTIMATE_3;
    requested_virtual_sensors[2].sample_rate = BSEC_SAMPLE_RATE_DISABLED;
    requested_virtual_sensors[3].sensor_id = BSEC_OUTPUT_GAS_ESTIMATE_4;
    requested_virtual_sensors[3].sample_rate = BSEC_SAMPLE_RATE_DISABLED;

    self->rslt = bsec_update_subscription(requested_virtual_sensors, n_requested_virtual_sensors, required_sensor_settings, &n_required_sensor_settings);

    if (self->rslt != BSEC_OK)
    {
        PyErr_SetString(bmeError, "Failed to unsubscribe gas estimates");
        return (PyObject *) NULL;
    }
    return Py_BuildValue("i", self->rslt);
}
#endif

static PyMethodDef bme68x_methods[] = {
    {"init_bme68x", (PyCFunction)bme_init_bme68x, METH_NOARGS, "Initialize the BME68X sensor"},
    {"print_dur_prof", (PyCFunction)bme_print_dur_prof, METH_NOARGS, "Print the current duration profile"},
    {"enable_debug_mode", (PyCFunction)bme_enable_debug_mode, METH_NOARGS, "Enable debug mode"},
    {"disable_debug_mode", (PyCFunction)bme_disable_debug_mode, METH_NOARGS, "Disable debug mode"},
    {"get_sensor_id", (PyCFunction)bme_get_sensor_id, METH_NOARGS, "Get unique sensor id"},
    {"set_temp_offset", (PyCFunction)bme_set_temp_offset, METH_VARARGS, "Set temperature offset"},
    {"get_chip_id", (PyCFunction)bme_get_chip_id, METH_NOARGS, "Get the chip ID"},
    {"close_i2c", (PyCFunction)bme_close_i2c, METH_NOARGS, "Close the I2C bus"},
    {"open_i2c", (PyCFunction)bme_open_i2c, METH_VARARGS, "Open the I2C bus and connect to I2C address"},
    {"get_variant", (PyCFunction)bme_get_variant, METH_NOARGS, "Return string representing variant (BME680 or BME688)"},
    {"set_conf", (PyCFunction)bme_set_conf, METH_VARARGS, "Configure the BME68X sensor"},
    {"set_heatr_conf", (PyCFunction)bme_set_heatr_conf, METH_VARARGS, "Configure the BME68X heater"},
    {"get_data", (PyCFunction)bme_get_data, METH_NOARGS, "Measure and read data from the BME68X sensor w/o BSEC"},
#ifdef BSEC
    {"subscribe_gas_estimates", (PyCFunction)bme_subscribe_gas_estimates, METH_VARARGS, "Subscribe to provided number of gas estimates"},
    {"subscribe_ai_classes", (PyCFunction)bme_subscribe_ai_classes, METH_VARARGS, "Subscribe to all gas estimates"},
    {"set_sample_rate", (PyCFunction)bme_set_sample_rate, METH_VARARGS, "Set the sample rate for all virtual sensors"},
    {"get_bsec_version", (PyCFunction)bme_get_bsec_version, METH_NOARGS, "Return the BSEC version as string"},
    {"get_digital_nose_data", (PyCFunction)bme_get_digital_nose_data, METH_NOARGS, "Measure Gas Estimates"},
    {"get_bsec_data", (PyCFunction)bme_get_bsec_data, METH_NOARGS, "Measure and read data from the BME68x sensor with BSEC"},
    {"get_bsec_conf", (PyCFunction)bme_get_bsec_conf, METH_NOARGS, "Get BSEC config as config integer array"},
    {"set_bsec_conf", (PyCFunction)bme_set_bsec_conf, METH_VARARGS, "Set BSEC config from config integer array"},
    {"get_bsec_state", (PyCFunction)bme_get_bsec_state, METH_NOARGS, "Get BSEC state"},
    {"set_bsec_state", (PyCFunction)bme_set_bsec_state, METH_VARARGS, "Set BSEC state"},
    {"update_bsec_subscription", (PyCFunction)bme_update_bsec_subscription, METH_VARARGS, "Update susbcribed BSEC outputs"},
    {"enable_gas_estimates", (PyCFunction)bme_enable_gas_estimates, METH_NOARGS, "Enable all 4 gas estimates"},
    {"disable_gas_estimates", (PyCFunction)bme_disable_gas_estimates, METH_NOARGS, "Disable all 4 gas estimates"},
#endif
    {NULL, NULL, 0, NULL} // Sentinel
};

static PyTypeObject BMEType = {
    PyVarObject_HEAD_INIT(NULL, 0)
        .tp_name = "bme68x.BME68X",
    .tp_doc = "BME68X sensor object",
    .tp_basicsize = sizeof(BMEObject),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_new = bme68x_new,
    .tp_init = (initproc)bme68x_init_type,
    .tp_dealloc = (destructor)bme68x_dealloc,
    .tp_members = bme68x_members,
    .tp_methods = bme68x_methods,
};

static PyModuleDef custommodule = {
    PyModuleDef_HEAD_INIT,
    .m_name = "bme68x",
    .m_doc = "Example module that creates an extension type.",
    .m_size = -1,
};

PyMODINIT_FUNC
PyInit_bme68x(void)
{
    PyObject *m;
    if (PyType_Ready(&BMEType) < 0)
        return NULL;

    m = PyModule_Create(&custommodule);
    if (m == NULL)
        return NULL;

    Py_INCREF(&BMEType);
    if (PyModule_AddObject(m, "BME68X", (PyObject *)&BMEType) < 0)
    {
        Py_DECREF(&BMEType);
        Py_DECREF(m);
        return NULL;
    }

    PyModule_AddIntConstant(m, "BME68X_I2C_ADDR_LOW", 0x76);
    PyModule_AddIntConstant(m, "BME68X_I2C_ADDR_HIGH", 0x77);
    PyModule_AddIntConstant(m, "BME68X_CHIP_ID", 0x61);
    PyModule_AddIntConstant(m, "BME68X_OK", 0);

    return m;
}
