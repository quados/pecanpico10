#include "ch.h"
#include "hal.h"
#include "shell.h"
#include "debug.h"
#include <stdlib.h>
#include "image.h"
#include "aprs.h"
#include "radio.h"
#include "commands.h"

static uint8_t usb_buffer[16*1024] __attribute__((aligned(32))); // USB image buffer

const ShellCommand commands[] = {
	{"debug", usb_cmd_debugOnUSB},
	{"picture", usb_cmd_printPicture},
	{"log", usb_cmd_readLog},
	{"config", usb_cmd_printConfig},
	{"aprs_message", usb_cmd_send_aprs_message},
	{NULL, NULL}
};


void usb_cmd_debugOnUSB(BaseSequentialStream *chp, int argc, char *argv[])
{
	if(argc < 1)
	{
		chprintf(chp, "Argument missing!\r\n");
		chprintf(chp, "Argument 1: 1 for switch on, 0 for switch off\r\n");
		return;
	}

	debug_on_usb = atoi(argv[0]);
}

void usb_cmd_printPicture(BaseSequentialStream *chp, int argc, char *argv[])
{
	(void)argc;
	(void)argv;

	// Take picture
	uint32_t size_sampled = takePicture(usb_buffer, sizeof(usb_buffer), RES_QVGA, false);

	// Transmit image via USB
	if(size_sampled)
	{
		bool start_detected = false;
		for(uint32_t i=0; i<size_sampled; i++)
		{
			// Look for APP0 instead of SOI because SOI is lost sometimes, but we can add SOI easily later on
			if(!start_detected && usb_buffer[i] == 0xFF && usb_buffer[i+1] == 0xE0) {
				start_detected = true;
				TRACE_USB("DATA > image/jpeg,%d", size_sampled-i+2); // Flag the data on serial output
				streamPut(chp, 0xFF);
				streamPut(chp, 0xD8);
			}
			if(start_detected)
				streamPut(chp, usb_buffer[i]);
		}
		if(!start_detected)
		{
			TRACE_USB("DATA > image/jpeg,0");
			TRACE_USB("DATA > text/trace,no SOI flag found");
		}

	} else { // Camera error

		TRACE_USB("DATA > image,jpeg,0");
		TRACE_USB("DATA > error,no camera found");

	}
}

void usb_cmd_readLog(BaseSequentialStream *chp, int argc, char *argv[])
{
	(void)argc;
	(void)argv;

	/*chprintf(chp, "addr,id,time,lat,lon,alt,sats,ttff,vbat,vsol,vsub,pbat,rbat,press,temp,hum,idimg\r\n");

	trackPoint_t *tp;
	for(uint16_t i=0; (tp = getLogBuffer(i)) != NULL; i++)
		if(tp->id != 0xFFFFFFFF)
		{
			chprintf(	chp,
						"%08x,%d,%d,%d.%05d,%d.%05d,%d,%d,%d,%d.%03d,%d.%03d,%d,%d.%01d,%2d.%02d,%2d.%01d\r\n",
						tp, tp->id, tp->gps_time,
						tp->gps_lat/10000000, (tp->gps_lat > 0 ? 1:-1)*(tp->gps_lat/100)%100000, tp->gps_lon/10000000, (tp->gps_lon > 0 ? 1:-1)*(tp->gps_lon/100)%100000, tp->gps_alt,
						tp->gps_sats, tp->gps_ttff,
						tp->adc_vbat/1000, (tp->adc_vbat%1000), tp->adc_vsol/1000, (tp->adc_vsol%1000), tp->pac_pbat,
						tp->sen_i1_press/10, tp->sen_i1_press%10, tp->sen_i1_temp/100, tp->sen_i1_temp%100, tp->sen_i1_hum/10, tp->sen_i1_hum%10
			);
		}*/

	chprintf(chp, "TODO: Not implemented\r\n");
}

void usb_cmd_printConfig(BaseSequentialStream *chp, int argc, char *argv[])
{
/*	if(argc < 1)
	{
		chprintf(chp, "Argument missing!\r\n");
		chprintf(chp, "Argument 1: Id of config\r\n");
		return;
	}

	uint8_t id = atoi(argv[0]);
	chprintf(chp, "Config ID=%d\r\n", id);

	chprintf(chp, "Power: %d\r\n", config[id].power);

	if(config[id].frequency.type == FREQ_STATIC) {
		uint32_t freq = config[id].frequency.hz;
		if((freq/1000)*1000 == freq)
			chprintf(chp, "Frequency: %d.%03d MHz\r\n", freq/1000000, (freq%1000000)/1000);
		else
			chprintf(chp, "Frequency: %d.%03d MHz\r\n", freq/1000000, (freq%1000000));
	} else {
		uint32_t freq = getFrequency(&config[id].frequency);
		chprintf(chp, "Frequency: APRS region dependent (currently %d.%03d MHz)\r\n", freq/1000000, (freq%1000000)/1000);
	}

	chprintf(chp, "Modulation: %d\r\n", config[id].modulation);
	chprintf(chp, "Initial Delay: %d\r\n", config[id].init_delay);
	chprintf(chp, "Packet Spacing: %d\r\n", config[id].packet_spacing);
	chprintf(chp, "Sleep config: xx\r\n");
	chprintf(chp, "Trigger config: xx\r\n");

	chprintf(chp, "Modulation config: xx\r\n");

	chprintf(chp, "Protocol config: xx\r\n");

	chprintf(chp, "SSDV config: xx\r\n");

	chprintf(chp, "Watchdog timeout: %d\r\n", config[id].wdg_timeout);*/

	(void)argc;
	(void)argv;
	chprintf(chp, "TODO: Not implemented\r\n");
}

void usb_cmd_send_aprs_message(BaseSequentialStream *chp, int argc, char *argv[])
{
	if(argc < 2)
	{
		chprintf(chp, "Argument missing!\r\n");
		chprintf(chp, "Argument 1: Destination\r\n");
		chprintf(chp, "Argument 2: Message\r\n");
		return;
	}
	chprintf(chp, "Destination: %s\r\n", argv[0]);
	chprintf(chp, "Message: %s\r\n", argv[1]);

	packet_t packet = aprs_encode_message(conf_sram.rx.call,
	                                      conf_sram.rx.path,
	                                      argv[0], argv[1], false);
	transmitOnRadio(packet,
	                conf_sram.rx.radio_conf.freq,
                    conf_sram.rx.radio_conf.step,
                    conf_sram.rx.radio_conf.chan,
                    conf_sram.rx.radio_conf.pwr,
                    conf_sram.rx.radio_conf.mod);

	chprintf(chp, "Message sent!\r\n");
}
