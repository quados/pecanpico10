#include "ch.h"
#include "hal.h"

#include "debug.h"
#include "threads.h"
#include "config.h"
#include "radio.h"
#include "aprs.h"
#include "sleep.h"
#include "chprintf.h"
#include <string.h>
#include <math.h>
#include "watchdog.h"

/*
 *
 */
THD_FUNCTION(bcnThread, arg) {
  thd_aprs_conf_t* conf = (thd_aprs_conf_t *)arg;

  // Wait
  if(conf->thread_conf.init_delay) chThdSleep(conf->thread_conf.init_delay);

  // Start data collector (if not running yet)
  init_data_collector();

  // Start position thread
  TRACE_INFO("BCN  > Startup beacon thread");

  // Set telemetry configuration transmission variables
  sysinterval_t last_conf_transmission =
      chVTGetSystemTime() - conf->tx.tel_enc_cycle;
  sysinterval_t time = chVTGetSystemTime();

  while(true) {
    TRACE_INFO("BCN  > Do module BEACON cycle");


    dataPoint_t* dataPoint = getLastDataPoint();
    if(dataPoint->gps_state != GPS_LOCKED1
        || dataPoint->gps_state != GPS_LOCKED2) {
      TRACE_INFO("BCN  > Set location from fixed location data");
      dataPoint->gps_alt = conf->tx.alt;
      dataPoint->gps_lat = conf->tx.lat;
      dataPoint->gps_lon = conf->tx.lon;
      dataPoint->gps_state = GPS_FIXED;
    } else {
      TRACE_INFO("BCN  > Set location from GPS location data");
    }
    if(!p_sleep(&conf->thread_conf.sleep_conf)) {
      TRACE_INFO("BCN  > Transmit position beacon");

      // Encode/Transmit position packet
      packet_t packet = aprs_encode_position(conf->tx.call, conf->tx.path,
                                             conf->tx.symbol, dataPoint);
      if(packet == NULL) {
        TRACE_WARN("BCN  > No free packet objects"
            " for position transmission");
      } else {
        if(!transmitOnRadio(packet,
                            conf->tx.radio_conf.freq,
                            0,
                            0,
                            conf->tx.radio_conf.pwr,
                            conf->tx.radio_conf.mod,
                            conf->tx.radio_conf.rssi)) {
          TRACE_ERROR("BCN  > failed to transmit beacon data");
        }
        chThdSleep(TIME_S2I(5));
      }

      /*
       * Encode/Transmit APRSD packet.
       * This is a tracker originated message (not a reply to a request).
       * The message will be sent to the base station set in path.
       */
      if(conf_sram.aprs.base.enabled) {
        /*
         * Send message from this device.
         * Use call sign and path as specified in base config.
         * There is no acknowledgment requested.
         */
        packet = aprs_compose_aprsd_message(
            conf->tx.call,
            conf->base.path,
            conf->base.call);
        if(packet == NULL) {
          TRACE_WARN("BCN  > No free packet objects "
              "or badly formed APRSD message");
        } else {
          if(!transmitOnRadio(packet,
                              conf->tx.radio_conf.freq,
                              0,
                              0,
                              conf->tx.radio_conf.pwr,
                              conf->tx.radio_conf.mod,
                              conf->tx.radio_conf.rssi
          )) {
            TRACE_ERROR("BCN  > Failed to transmit APRSD data");
          }
          chThdSleep(TIME_S2I(5));
        }
      } else {
        /* TODO: Implement a fallback destination if no base station set? */
        TRACE_INFO("BCN  > APRSD data not sent - no base station specified");
      }

      // Telemetry encoding parameter transmission
      if(conf->tx.tel_enc_cycle != 0 && last_conf_transmission
          + conf->tx.tel_enc_cycle < chVTGetSystemTime()) {


        TRACE_INFO("BCN  > Transmit telemetry configuration");

        // Encode and transmit telemetry config packet
        for(uint8_t type = 0; type < 4; type++) {
          packet = aprs_encode_telemetry_configuration(
              conf->tx.call,
              conf->tx.path,
              APRS_DEVICE_CALLSIGN,
              type);
          if(packet == NULL) {
            TRACE_WARN("BCN  > No free packet objects for"
                       " telemetry transmission");
          } else {
            if(!transmitOnRadio(packet,
                                conf->tx.radio_conf.freq,
                                0,
                                0,
                                conf->tx.radio_conf.pwr,
                                conf->tx.radio_conf.mod,
                                conf->tx.radio_conf.rssi)) {
              TRACE_ERROR("BCN  > Failed to transmit telemetry data");
            }
            chThdSleep(TIME_S2I(5));
          }
        }

        last_conf_transmission += conf->tx.tel_enc_cycle;
      }
    }
    time = waitForTrigger(time, conf->tx.interval);
  }
}

/*
 *
 */
void start_beacon_thread(thd_aprs_conf_t *conf) {
  thread_t *th = chThdCreateFromHeap(NULL, THD_WORKING_AREA_SIZE(10*1024),
                                     "BCN", LOWPRIO, bcnThread, conf);
  if(!th) {
    // Print startup error, do not start watchdog for this thread
    TRACE_ERROR("BCN  > Could not startup thread (not enough memory available)");
  }
}
