/**
  * @see https://github.com/thasti/utrak
  */

#ifndef __MAX_H__
#define __MAX_H__

#include "ch.h"
#include "hal.h"
#include "ptime.h"

#define GPS_MODEL_UNSET         -1
#define GPS_MODEL_PORTABLE      0
#define GPS_MODEL_STATIONARY    2
#define GPS_MODEL_PEDESTRIAN    3
#define GPS_MODEL_AUTOMOTIVE    4
#define GPS_MODEL_SEA           5
#define GPS_MODEL_AIRBORNE1G    6
#define GPS_MODEL_AIRBORNE2G    7
#define GPS_MODEL_AIRBORNE4G    8

#define UBLOX_MAX_ADDRESS	    0x42

// You can either use I2C or UART
#define UBLOX_USE_I2C           TRUE

#define isGPSLocked(pos) ((pos)->type == 3 && (pos)->num_svs >= 4 && (pos)->fixOK == true)

typedef struct {
	ptime_t time;		// Time
	uint8_t type;		// type of fix (validity)
	uint8_t num_svs;	// number of satellites used for solution, range 0 .. 19
	int32_t lat;		// latitude in deg * 10^7, range -90 .. +90 * 10^7
	int32_t lon;		// longitude in deg * 10^7, range -180 .. +180 * 10^7
	int32_t alt;		// altitude in m, range 0m, up to ~40000m, clamped
	bool    fixOK;	    // Flag that is set to true, when DOP is with the limits
	uint16_t pdop;		// Position DOP
} gpsFix_t;

uint8_t gps_set_gps_only(void);
uint8_t gps_disable_nmea_output(void);
bool    gps_set_model(bool dynamic);
uint8_t gps_set_stationary_model(void);
uint8_t gps_set_portable_model(void);
uint8_t gps_set_airborne_model(void);
uint8_t gps_set_power_save(void);
uint8_t gps_power_save(int on);
//uint8_t gps_save_settings(void);
bool gps_get_fix(gpsFix_t *fix);

bool GPS_Init(void);
void GPS_Deinit(void);
uint32_t GPS_get_mcu_frequency(void);
bool gps_calc_ubx_csum(uint8_t *mbuf, uint16_t mlen);

#endif
