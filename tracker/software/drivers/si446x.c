/**
 * Si446x driver specialized for APRS transmissions. The driver supports APRS
 * transmission and receiption.
 * There can be either used the SLabs Si4463 or Si4464.
 */

#include "ch.h"
#include "hal.h"

#include "si446x.h"
#include "debug.h"
//#include "types.h"
//#include <string.h>

static const SPIConfig ls_spicfg = {
	.ssport	= PAL_PORT(LINE_RADIO_CS),
	.sspad	= PAL_PAD(LINE_RADIO_CS),
	.cr1	= SPI_CR1_MSTR
};

static uint32_t outdiv;
static int16_t lastTemp;

static void Si446x_write(const uint8_t* txData, uint32_t len) {
	// Transmit data by SPI
	uint8_t rxData[len];
	
	// SPI transfer
	spiAcquireBus(&SPID3);
	spiStart(&SPID3, &ls_spicfg);

	spiSelect(&SPID3);
	spiExchange(&SPID3, len, txData, rxData);
	spiUnselect(&SPID3);

	// Reqest ACK by Si446x
	rxData[1] = 0x00;
	while(rxData[1] != 0xFF) {

		// Request ACK by Si446x
		uint8_t rx_ready[] = {0x44};

		// SPI transfer
		spiSelect(&SPID3);
		spiExchange(&SPID3, 3, rx_ready, rxData);
		spiUnselect(&SPID3);
	}
	spiStop(&SPID3);
	spiReleaseBus(&SPID3);
}

/**
 * Read register from Si446x. First Register CTS is included.
 */
static void Si446x_read(const uint8_t* txData, uint32_t txlen, uint8_t* rxData, uint32_t rxlen) {
	// Transmit data by SPI
	uint8_t null_spi[txlen];
	// SPI transfer
	spiAcquireBus(&SPID3);
	spiStart(&SPID3, &ls_spicfg);

	spiSelect(&SPID3);
	spiExchange(&SPID3, txlen, txData, null_spi);
	spiUnselect(&SPID3);

	// Reqest ACK by Si446x
	rxData[1] = 0x00;
	while(rxData[1] != 0xFF) {

		// Request ACK by Si446x
		uint16_t rx_ready[rxlen];
		rx_ready[0] = 0x44;

		// SPI transfer
		spiSelect(&SPID3);
		spiExchange(&SPID3, rxlen, rx_ready, rxData);
		spiUnselect(&SPID3);
	}
	spiStop(&SPID3);
	spiReleaseBus(&SPID3);
}

static void Si446x_setProperty8(uint16_t reg, uint8_t val) {
	uint8_t msg[] = {0x11, (reg >> 8) & 0xFF, 0x01, reg & 0xFF, val};
	Si446x_write(msg, 5);
}

static void Si446x_setProperty16(uint16_t reg, uint8_t val1, uint8_t val2) {
	uint8_t msg[] = {0x11, (reg >> 8) & 0xFF, 0x02, reg & 0xFF, val1, val2};
	Si446x_write(msg, 6);
}

static void Si446x_setProperty24(uint16_t reg, uint8_t val1, uint8_t val2, uint8_t val3) {
	uint8_t msg[] = {0x11, (reg >> 8) & 0xFF, 0x03, reg & 0xFF, val1, val2, val3};
	Si446x_write(msg, 7);
}

static void Si446x_setProperty32(uint16_t reg, uint8_t val1, uint8_t val2, uint8_t val3, uint8_t val4) {
	uint8_t msg[] = {0x11, (reg >> 8) & 0xFF, 0x04, reg & 0xFF, val1, val2, val3, val4};
	Si446x_write(msg, 8);
}





/**
 * Initializes Si446x transceiver chip. Adjustes the frequency which is shifted by variable
 * oscillator voltage.
 * @param mv Oscillator voltage in mv
 */
void Si446x_init(void) {
	TRACE_INFO("SI   > Init radio");		chThdSleep(TIME_MS2I(15));

	// Configure Radio pins
	palSetLineMode(LINE_SPI_SCK, PAL_MODE_ALTERNATE(6) | PAL_STM32_OSPEED_HIGHEST);		// SCK
	palSetLineMode(LINE_SPI_MISO, PAL_MODE_ALTERNATE(6) | PAL_STM32_OSPEED_HIGHEST);	// MISO
	palSetLineMode(LINE_SPI_MOSI, PAL_MODE_ALTERNATE(6) | PAL_STM32_OSPEED_HIGHEST);	// MOSI
	palSetLineMode(LINE_RADIO_CS, PAL_MODE_OUTPUT_PUSHPULL | PAL_STM32_OSPEED_HIGHEST);	// RADIO CS
	palSetLineMode(LINE_RADIO_SDN, PAL_MODE_OUTPUT_PUSHPULL);							// RADIO SDN

	// Pull CS HIGH
	palSetLine(LINE_RADIO_CS);

	// Reset radio
	palSetLine(LINE_RADIO_SDN);
	chThdSleep(TIME_MS2I(10));

	// Power up transmitter
	palClearLine(LINE_RADIO_SDN);	// Radio SDN low (power up transmitter)
	chThdSleep(TIME_MS2I(10));		// Wait for transmitter to power up

	// Power up (transmits oscillator type)
	const uint8_t x3 = (Si446x_CLK >> 24) & 0x0FF;
	const uint8_t x2 = (Si446x_CLK >> 16) & 0x0FF;
	const uint8_t x1 = (Si446x_CLK >>  8) & 0x0FF;
	const uint8_t x0 = (Si446x_CLK >>  0) & 0x0FF;
	const uint8_t init_command[] = {0x02, 0x01, (Si446x_TCXO_EN & 0x1), x3, x2, x1, x0};
	Si446x_write(init_command, 7);
	chThdSleep(TIME_MS2I(25));

	// Set transmitter GPIOs
	uint8_t gpio_pin_cfg_command[] = {
		0x13,	// Command type = GPIO settings
		0x00,	// GPIO0        GPIO_MODE = DONOTHING
		0x14,	// GPIO1        GPIO_MODE = RX_DATA
		0x21,	// GPIO2        GPIO_MODE = RX_STATE
		0x20,	// GPIO3        GPIO_MODE = TX_STATE
		0x1B,	// NIRQ         NIRQ_MODE = CCA
		0x0B,	// SDO          SDO_MODE = SDO
		0x00	// GEN_CONFIG
	};
	Si446x_write(gpio_pin_cfg_command, 8);
	chThdSleep(TIME_MS2I(25));

	#if !Si446x_TCXO_EN
	Si446x_setProperty8(Si446x_GLOBAL_XO_TUNE, 0x00);
	#endif

	Si446x_setProperty8(Si446x_FRR_CTL_A_MODE, 0x00);
	Si446x_setProperty8(Si446x_FRR_CTL_B_MODE, 0x00);
	Si446x_setProperty8(Si446x_FRR_CTL_C_MODE, 0x00);
	Si446x_setProperty8(Si446x_FRR_CTL_D_MODE, 0x00);
	Si446x_setProperty8(Si446x_INT_CTL_ENABLE, 0x00);
	Si446x_setProperty8(Si446x_GLOBAL_CONFIG, 0x70);

	// Reset FIFO
	const uint8_t reset_fifo[] = {0x15, 0x01};
	Si446x_write(reset_fifo, 2);
	const uint8_t unreset_fifo[] = {0x15, 0x00};
	Si446x_write(unreset_fifo, 2);

	Si446x_setProperty8(Si446x_PREAMBLE_TX_LENGTH, 0x00);
	Si446x_setProperty8(Si446x_SYNC_CONFIG, 0x80);

	Si446x_setProperty8(Si446x_GLOBAL_CLK_CFG, 0x00);
	Si446x_setProperty8(Si446x_MODEM_RSSI_CONTROL, 0x00);
	Si446x_setProperty8(Si446x_MODEM_RSSI_THRESH, 0x5F);
	Si446x_setProperty8(Si446x_PREAMBLE_CONFIG_STD_1, 0x14);
	Si446x_setProperty8(Si446x_PKT_CONFIG1, 0x41);
	Si446x_setProperty8(Si446x_MODEM_MAP_CONTROL, 0x00);
	Si446x_setProperty8(Si446x_MODEM_DSM_CTRL, 0x07);
	Si446x_setProperty8(Si446x_MODEM_CLKGEN_BAND, 0x0D);


	Si446x_setProperty24(Si446x_MODEM_FREQ_DEV, 0x00, 0x00, 0x79);
	Si446x_setProperty8(Si446x_MODEM_TX_RAMP_DELAY, 0x01);
	Si446x_setProperty8(Si446x_PA_TC, 0x3D);
	Si446x_setProperty8(Si446x_FREQ_CONTROL_INTE, 0x41);
	Si446x_setProperty24(Si446x_FREQ_CONTROL_FRAC, 0x0B, 0xB1, 0x3B);
	Si446x_setProperty16(Si446x_FREQ_CONTROL_CHANNEL_STEP_SIZE, 0x0B, 0xD1);
	Si446x_setProperty8(Si446x_FREQ_CONTROL_W_SIZE, 0x20);
	Si446x_setProperty8(Si446x_FREQ_CONTROL_VCOCNT_RX_ADJ, 0xFA);
	Si446x_setProperty8(Si446x_MODEM_MDM_CTRL, 0x80);
	Si446x_setProperty8(Si446x_MODEM_IF_CONTROL, 0x08);
	Si446x_setProperty24(Si446x_MODEM_IF_FREQ, 0x02, 0x80, 0x00);
	Si446x_setProperty8(Si446x_MODEM_DECIMATION_CFG1, 0x20);
	Si446x_setProperty8(Si446x_MODEM_DECIMATION_CFG0, 0x10);
	Si446x_setProperty16(Si446x_MODEM_BCR_OSR, 0x00, 0x52);
	Si446x_setProperty24(Si446x_MODEM_BCR_NCO_OFFSET, 0x06, 0x3D, 0x10);
	Si446x_setProperty16(Si446x_MODEM_BCR_GAIN, 0x03, 0x1F);
	Si446x_setProperty8(Si446x_MODEM_BCR_GEAR, 0x00);
	Si446x_setProperty8(Si446x_MODEM_BCR_MISC1, 0xC2);
	Si446x_setProperty8(Si446x_MODEM_AFC_GEAR, 0x54);
	Si446x_setProperty8(Si446x_MODEM_AFC_WAIT, 0x36);
	Si446x_setProperty16(Si446x_MODEM_AFC_GAIN, 0x82, 0xAA);
	Si446x_setProperty16(Si446x_MODEM_AFC_LIMITER, 0x00, 0x95);
	Si446x_setProperty8(Si446x_MODEM_AFC_MISC, 0x80);
	Si446x_setProperty8(Si446x_MODEM_AGC_CONTROL, 0xE2);
	Si446x_setProperty8(Si446x_MODEM_AGC_WINDOW_SIZE, 0x11);
	Si446x_setProperty8(Si446x_MODEM_AGC_RFPD_DECAY, 0x12);
	Si446x_setProperty8(Si446x_MODEM_AGC_IFPD_DECAY, 0x12);
	Si446x_setProperty8(Si446x_MODEM_FSK4_GAIN1, 0x00);
	Si446x_setProperty8(Si446x_MODEM_FSK4_GAIN0, 0x02);
	Si446x_setProperty16(Si446x_MODEM_FSK4_TH, 0x02, 0x6D);
	Si446x_setProperty8(Si446x_MODEM_FSK4_MAP, 0x00);
	Si446x_setProperty8(Si446x_MODEM_OOK_PDTC, 0x28);
	Si446x_setProperty8(Si446x_MODEM_OOK_CNT1, 0x85);
	Si446x_setProperty8(Si446x_MODEM_OOK_MISC, 0x23);
	Si446x_setProperty8(Si446x_MODEM_RAW_SEARCH, 0xDE);
	Si446x_setProperty8(Si446x_MODEM_RAW_CONTROL, 0x8F);
	Si446x_setProperty16(Si446x_MODEM_RAW_EYE, 0x00, 0x0F);
	Si446x_setProperty8(Si446x_MODEM_ANT_DIV_MODE, 0x01);
	Si446x_setProperty8(Si446x_MODEM_ANT_DIV_CONTROL, 0x80);
	Si446x_setProperty8(Si446x_MODEM_RSSI_COMP, 0x40);













	// Temperature readout
	lastTemp = Si446x_getTemperature();
	TRACE_INFO("SI   > Transmitter temperature %d degC", lastTemp/100);
}



/*void init144_800(void) {
	// Configure Radio pins
	palSetLineMode(LINE_SPI_SCK, PAL_MODE_ALTERNATE(6) | PAL_STM32_OSPEED_HIGHEST);		// SCK
	palSetLineMode(LINE_SPI_MISO, PAL_MODE_ALTERNATE(6) | PAL_STM32_OSPEED_HIGHEST);	// MISO
	palSetLineMode(LINE_SPI_MOSI, PAL_MODE_ALTERNATE(6) | PAL_STM32_OSPEED_HIGHEST);	// MOSI
	palSetLineMode(LINE_RADIO_CS, PAL_MODE_OUTPUT_PUSHPULL | PAL_STM32_OSPEED_HIGHEST);	// RADIO CS
	palSetLineMode(LINE_RADIO_SDN, PAL_MODE_OUTPUT_PUSHPULL);							// RADIO SDN

	// Pull CS of all SPI slaves high
	palSetLine(LINE_RADIO_CS);

	// Reset radio
	palSetLine(LINE_RADIO_SDN);
	chThdSleep(TIME_MS2I(10));

	// Power up transmitter
	palClearLine(LINE_RADIO_SDN);	// Radio SDN low (power up transmitter)
	chThdSleep(TIME_MS2I(10));		// Wait for transmitter to power up

	// Power up (transmits oscillator type)
	const uint8_t x3 = (RADIO_CLK >> 24) & 0x0FF;
	const uint8_t x2 = (RADIO_CLK >> 16) & 0x0FF;
	const uint8_t x1 = (RADIO_CLK >>  8) & 0x0FF;
	const uint8_t x0 = (RADIO_CLK >>  0) & 0x0FF;
	const uint8_t init_command[] = {0x02, 0x01, (RADIO_TCXO_EN & 0x1), x3, x2, x1, x0};
	Si446x_write(init_command, 7);
	chThdSleep(TIME_MS2I(25));

	// Set transmitter GPIOs
	uint8_t gpio_pin_cfg_command[] = {
		0x13,	// Command type = GPIO settings
		0x00,	// GPIO0        GPIO_MODE = DONOTHING
		0x14,	// GPIO1        GPIO_MODE = RX_DATA
		0x21,	// GPIO2        GPIO_MODE = RX_STATE
		0x20,	// GPIO3        GPIO_MODE = TX_STATE
		0x1B,	// NIRQ         NIRQ_MODE = CCA
		0x0B,	// SDO          SDO_MODE = SDO
		0x00	// GEN_CONFIG
	};
	Si446x_write(gpio_pin_cfg_command, 8);
	chThdSleep(TIME_MS2I(25));







	#if !RADIO_TCXO_EN
	Si446x_setProperty8(Si446x_GLOBAL_XO_TUNE, 0x00);
	#endif

	Si446x_setProperty8(Si446x_FRR_CTL_A_MODE, 0x00);
	Si446x_setProperty8(Si446x_FRR_CTL_B_MODE, 0x00);
	Si446x_setProperty8(Si446x_FRR_CTL_C_MODE, 0x00);
	Si446x_setProperty8(Si446x_FRR_CTL_D_MODE, 0x00);
	Si446x_setProperty8(Si446x_INT_CTL_ENABLE, 0x00);
	Si446x_setProperty8(Si446x_GLOBAL_CONFIG, 0x60);
	Si446x_setProperty8(Si446x_GLOBAL_CLK_CFG, 0x00);
	Si446x_setProperty8(Si446x_MODEM_RSSI_CONTROL, 0x00);
	Si446x_setProperty8(Si446x_MODEM_RSSI_THRESH, 0x5F);
	Si446x_setProperty8(Si446x_PREAMBLE_CONFIG_STD_1, 0x14);
	Si446x_setProperty8(Si446x_PKT_CONFIG1, 0x40);
	Si446x_setProperty8(Si446x_MODEM_MOD_TYPE, 0x0A);
	Si446x_setProperty8(Si446x_MODEM_MAP_CONTROL, 0x00);
	Si446x_setProperty8(Si446x_MODEM_DSM_CTRL, 0x07);
	Si446x_setProperty8(Si446x_MODEM_CLKGEN_BAND, 0x0D);

	//Si446x_setProperty8(Si446x_SYNTH_PFDCP_CPFF, 0x2C);
	//Si446x_setProperty8(Si446x_SYNTH_PFDCP_CPINT, 0x0E);
	//Si446x_setProperty8(Si446x_SYNTH_VCO_KV, 0x0B);
	//Si446x_setProperty8(Si446x_SYNTH_LPFILT3, 0x04);
	//Si446x_setProperty8(Si446x_SYNTH_LPFILT2, 0x0C);
	//Si446x_setProperty8(Si446x_SYNTH_LPFILT1, 0x73);
	//Si446x_setProperty8(Si446x_SYNTH_LPFILT0, 0x03);

	Si446x_setProperty24(Si446x_MODEM_DATA_RATE, 0x04, 0x07, 0x40);
	Si446x_setProperty32(Si446x_MODEM_TX_NCO_MODE, 0x01, 0x8C, 0xBA, 0x80);
	Si446x_setProperty24(Si446x_MODEM_FREQ_DEV, 0x00, 0x00, 0x79);
	Si446x_setProperty8(Si446x_MODEM_TX_RAMP_DELAY, 0x01);
	Si446x_setProperty8(Si446x_PA_TC, 0x3D);
	Si446x_setProperty8(Si446x_FREQ_CONTROL_INTE, 0x41);
	Si446x_setProperty24(Si446x_FREQ_CONTROL_FRAC, 0x0B, 0xB1, 0x3B);
	Si446x_setProperty16(Si446x_FREQ_CONTROL_CHANNEL_STEP_SIZE, 0x0B, 0xD1);
	Si446x_setProperty8(Si446x_FREQ_CONTROL_W_SIZE, 0x20);
	Si446x_setProperty8(Si446x_FREQ_CONTROL_VCOCNT_RX_ADJ, 0xFA);
	Si446x_setProperty8(Si446x_MODEM_MDM_CTRL, 0x80);
	Si446x_setProperty8(Si446x_MODEM_IF_CONTROL, 0x08);
	Si446x_setProperty24(Si446x_MODEM_IF_FREQ, 0x02, 0x80, 0x00);
	Si446x_setProperty8(Si446x_MODEM_DECIMATION_CFG1, 0x20);
	Si446x_setProperty8(Si446x_MODEM_DECIMATION_CFG0, 0x10);
	Si446x_setProperty16(Si446x_MODEM_BCR_OSR, 0x00, 0x52);
	Si446x_setProperty24(Si446x_MODEM_BCR_NCO_OFFSET, 0x06, 0x3D, 0x10);
	Si446x_setProperty16(Si446x_MODEM_BCR_GAIN, 0x03, 0x1F);
	Si446x_setProperty8(Si446x_MODEM_BCR_GEAR, 0x00);
	Si446x_setProperty8(Si446x_MODEM_BCR_MISC1, 0xC2);
	Si446x_setProperty8(Si446x_MODEM_AFC_GEAR, 0x54);
	Si446x_setProperty8(Si446x_MODEM_AFC_WAIT, 0x36);
	Si446x_setProperty16(Si446x_MODEM_AFC_GAIN, 0x82, 0xAA);
	Si446x_setProperty16(Si446x_MODEM_AFC_LIMITER, 0x00, 0x95);
	Si446x_setProperty8(Si446x_MODEM_AFC_MISC, 0x80);
	Si446x_setProperty8(Si446x_MODEM_AGC_CONTROL, 0xE2);
	Si446x_setProperty8(Si446x_MODEM_AGC_WINDOW_SIZE, 0x11);
	Si446x_setProperty8(Si446x_MODEM_AGC_RFPD_DECAY, 0x12);
	Si446x_setProperty8(Si446x_MODEM_AGC_IFPD_DECAY, 0x12);
	Si446x_setProperty8(Si446x_MODEM_FSK4_GAIN1, 0x00);
	Si446x_setProperty8(Si446x_MODEM_FSK4_GAIN0, 0x02);
	Si446x_setProperty16(Si446x_MODEM_FSK4_TH, 0x02, 0x6D);
	Si446x_setProperty8(Si446x_MODEM_FSK4_MAP, 0x00);
	Si446x_setProperty8(Si446x_MODEM_OOK_PDTC, 0x28);
	Si446x_setProperty8(Si446x_MODEM_OOK_CNT1, 0x85);
	Si446x_setProperty8(Si446x_MODEM_OOK_MISC, 0x23);
	Si446x_setProperty8(Si446x_MODEM_RAW_SEARCH, 0xDE);
	Si446x_setProperty8(Si446x_MODEM_RAW_CONTROL, 0x8F);
	Si446x_setProperty16(Si446x_MODEM_RAW_EYE, 0x00, 0x0F);
	Si446x_setProperty8(Si446x_MODEM_ANT_DIV_MODE, 0x01);
	Si446x_setProperty8(Si446x_MODEM_ANT_DIV_CONTROL, 0x80);
	Si446x_setProperty8(Si446x_MODEM_RSSI_COMP, 0x40);

	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COE13_7_0, 0xA2);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COE12_7_0, 0xA0);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COE11_7_0, 0x97);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COE10_7_0, 0x8A);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COE9_7_0, 0x79);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COE8_7_0, 0x66);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COE7_7_0, 0x52);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COE6_7_0, 0x3F);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COE5_7_0, 0x2E);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COE4_7_0, 0x1F);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COE3_7_0, 0x14);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COE2_7_0, 0x0B);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COE1_7_0, 0x06);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COE0_7_0, 0x02);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COEM0, 0x00);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COEM1, 0x00);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COEM2, 0x00);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COEM3, 0x00);

	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COE13_7_0, 0xA2);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COE12_7_0, 0xA0);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COE11_7_0, 0x97);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COE10_7_0, 0x8A);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COE9_7_0, 0x79);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COE8_7_0, 0x66);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COE7_7_0, 0x52);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COE6_7_0, 0x3F);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COE5_7_0, 0x2E);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COE4_7_0, 0x1F);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COE3_7_0, 0x14);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COE2_7_0, 0x0B);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COE1_7_0, 0x06);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COE0_7_0, 0x02);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COEM0, 0x00);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COEM1, 0x00);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COEM2, 0x00);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COEM3, 0x00);
}*/

static void Si446x_setFrequency(uint32_t freq)
{
	uint16_t shift = 0;

	// Set the output divider according to recommended ranges given in Si446x datasheet
	uint32_t band = 0;
	if(freq < 705000000UL) {outdiv = 6;  band = 1;};
	if(freq < 525000000UL) {outdiv = 8;  band = 2;};
	if(freq < 353000000UL) {outdiv = 12; band = 3;};
	if(freq < 239000000UL) {outdiv = 16; band = 4;};
	if(freq < 177000000UL) {outdiv = 24; band = 5;};

	// Set the band parameter
	uint32_t sy_sel = 8;
	uint8_t set_band_property_command[] = {0x11, 0x20, 0x01, 0x51, (band + sy_sel)};
	Si446x_write(set_band_property_command, 5);

	// Set the PLL parameters
	uint32_t f_pfd = 2 * Si446x_CLK / outdiv;
	uint32_t n = ((uint32_t)(freq / f_pfd)) - 1;
	float ratio = (float)freq / (float)f_pfd;
	float rest  = ratio - (float)n;

	uint32_t m = (uint32_t)(rest * 524288UL);
	uint32_t m2 = m >> 16;
	uint32_t m1 = (m - m2 * 0x10000) >> 8;
	uint32_t m0 = (m - m2 * 0x10000 - (m1 << 8));

	uint32_t channel_increment = 524288 * outdiv * shift / (2 * Si446x_CLK);
	uint8_t c1 = channel_increment / 0x100;
	uint8_t c0 = channel_increment - (0x100 * c1);

	uint8_t set_frequency_property_command[] = {0x11, 0x40, 0x04, 0x00, n, m2, m1, m0, c1, c0};
	Si446x_write(set_frequency_property_command, 10);

	uint32_t x = ((((uint32_t)1 << 19) * outdiv * 1300.0)/(2*Si446x_CLK))*2;
	uint8_t x2 = (x >> 16) & 0xFF;
	uint8_t x1 = (x >>  8) & 0xFF;
	uint8_t x0 = (x >>  0) & 0xFF;
	uint8_t set_deviation[] = {0x11, 0x20, 0x03, 0x0a, x2, x1, x0};
	Si446x_write(set_deviation, 7);
}

/*static void Si446x_setShift(uint16_t shift)
{
	if(!shift)
		return;

	float units_per_hz = (( 0x40000 * outdiv ) / (float)Si446x_CLK);

	// Set deviation for 2FSK
	uint32_t modem_freq_dev = (uint32_t)(units_per_hz * shift / 2.0 );
	uint8_t modem_freq_dev_0 = 0xFF & modem_freq_dev;
	uint8_t modem_freq_dev_1 = 0xFF & (modem_freq_dev >> 8);
	uint8_t modem_freq_dev_2 = 0xFF & (modem_freq_dev >> 16);

	uint8_t set_modem_freq_dev_command[] = {0x11, 0x20, 0x03, 0x0A, modem_freq_dev_2, modem_freq_dev_1, modem_freq_dev_0};
	Si446x_write(set_modem_freq_dev_command, 7);
}*/

static void Si446x_setPowerLevel(int8_t level)
{
	// Set the Power
	uint8_t set_pa_pwr_lvl_property_command[] = {0x11, 0x22, 0x01, 0x01, level};
	Si446x_write(set_pa_pwr_lvl_property_command, 5);
}





void Si446x_setModemAFSK_TX(void)
{
	// Setup the NCO modulo and oversampling mode
	uint32_t s = Si446x_CLK / 10;
	uint8_t f3 = (s >> 24) & 0xFF;
	uint8_t f2 = (s >> 16) & 0xFF;
	uint8_t f1 = (s >>  8) & 0xFF;
	uint8_t f0 = (s >>  0) & 0xFF;
	Si446x_setProperty32(Si446x_MODEM_TX_NCO_MODE, f3, f2, f1, f0);

	// Setup the NCO data rate for APRS
	Si446x_setProperty24(Si446x_MODEM_DATA_RATE, 0x00, 0x33, 0x90);

	// Use 2FSK from FIFO (PH)
	Si446x_setProperty8(Si446x_MODEM_MOD_TYPE, 0x03);

	// Set AFSK filter
	const uint8_t coeff[] = {0x81, 0x9f, 0xc4, 0xee, 0x18, 0x3e, 0x5c, 0x70, 0x76};
	uint8_t i;
	for(i=0; i<sizeof(coeff); i++) {
		uint8_t msg[] = {0x11, 0x20, 0x01, 0x17-i, coeff[i]};
		Si446x_write(msg, 5);
	}
}

void Si446x_setModemAFSK_RX(void)
{
	// Setup the NCO modulo and oversampling mode
	uint32_t s = Si446x_CLK;
	uint8_t f3 = (s >> 24) & 0xFF;
	uint8_t f2 = (s >> 16) & 0xFF;
	uint8_t f1 = (s >>  8) & 0xFF;
	uint8_t f0 = (s >>  0) & 0xFF;
	Si446x_setProperty32(Si446x_MODEM_TX_NCO_MODE, f3, f2, f1, f0);

	// Setup the NCO data rate for APRS
	Si446x_setProperty24(Si446x_MODEM_DATA_RATE, 0x04, 0x07, 0x40);

	// Use 2FSK in DIRECT_MODE
	Si446x_setProperty8(Si446x_MODEM_MOD_TYPE, 0x0A);

	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COE13_7_0, 0xA2);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COE12_7_0, 0xA0);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COE11_7_0, 0x97);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COE10_7_0, 0x8A);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COE9_7_0, 0x79);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COE8_7_0, 0x66);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COE7_7_0, 0x52);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COE6_7_0, 0x3F);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COE5_7_0, 0x2E);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COE4_7_0, 0x1F);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COE3_7_0, 0x14);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COE2_7_0, 0x0B);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COE1_7_0, 0x06);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COE0_7_0, 0x02);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COEM0, 0x00);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COEM1, 0x00);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COEM2, 0x00);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COEM3, 0x00);

	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COE13_7_0, 0xA2);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COE12_7_0, 0xA0);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COE11_7_0, 0x97);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COE10_7_0, 0x8A);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COE9_7_0, 0x79);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COE8_7_0, 0x66);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COE7_7_0, 0x52);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COE6_7_0, 0x3F);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COE5_7_0, 0x2E);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COE4_7_0, 0x1F);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COE3_7_0, 0x14);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COE2_7_0, 0x0B);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COE1_7_0, 0x06);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COE0_7_0, 0x02);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COEM0, 0x00);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COEM1, 0x00);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COEM2, 0x00);
	Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COEM3, 0x00);
}

void Si446x_setModem2FSK(uint32_t speed)
{
	// Setup the NCO modulo and oversampling mode
	uint32_t s = Si446x_CLK / 10;
	uint8_t f3 = (s >> 24) & 0xFF;
	uint8_t f2 = (s >> 16) & 0xFF;
	uint8_t f1 = (s >>  8) & 0xFF;
	uint8_t f0 = (s >>  0) & 0xFF;
	Si446x_setProperty32(Si446x_MODEM_TX_NCO_MODE, f3, f2, f1, f0);

	// Setup the NCO data rate for 2FSK
	Si446x_setProperty24(Si446x_MODEM_DATA_RATE, (uint8_t)(speed >> 16), (uint8_t)(speed >> 8), (uint8_t)speed);

	// Use 2FSK from FIFO (PH)
	Si446x_setProperty8(Si446x_MODEM_MOD_TYPE, 0x03);
}




void Si446x_setTXState(uint16_t size)
{
	uint8_t change_state_command[] = {0x31, 0x00, 0x30, (size >> 8) & 0x1F, size & 0xFF};
	Si446x_write(change_state_command, 5);
}

void Si446x_setReadyState(void)
{
	const uint8_t change_state_command[] = {0x34, 0x03};
	Si446x_write(change_state_command, 2);
}

void Si446x_setRXState(void)
{
	const uint8_t change_state_command[] = {0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x08};
	Si446x_write(change_state_command, 8);
}

void Si446x_shutdown(void)
{
	palSetLineMode(LINE_SPI_SCK, PAL_MODE_INPUT_PULLDOWN);		// SCK
	palSetLineMode(LINE_SPI_MISO, PAL_MODE_INPUT_PULLDOWN);		// MISO
	palSetLineMode(LINE_SPI_MOSI, PAL_MODE_INPUT_PULLDOWN);		// MOSI
	palSetLineMode(LINE_RADIO_CS, PAL_MODE_INPUT_PULLDOWN);		// RADIO CS
	palSetLineMode(LINE_RADIO_SDN, PAL_MODE_INPUT_PULLDOWN);	// RADIO SDN
}

uint8_t Si446x_getState(void)
{
	const uint8_t fifo_info[1] = {0x33};
	uint8_t rxData[4];
	Si446x_read(fifo_info, 1, rxData, 4);
	return rxData[2];
}






bool Si446x_transmit(uint32_t frequency, int8_t power, uint16_t size, uint8_t rssi, sysinterval_t sql_timeout)
{
	if(!Si446x_inRadioBand(frequency)) {
		TRACE_ERROR("SI   > Frequency out of range");
		TRACE_ERROR("SI   > abort transmission");
		return false;
	}

	TRACE_INFO("SI   > Tune Si446x (TX)");
	Si446x_setFrequency(frequency);		// Set frequency
	Si446x_setPowerLevel(power);		// Set power level
	Si446x_setTXState(size);

	return true;
}

bool Si446x_receive(uint32_t frequency, uint8_t rssi)
{
	if(!Si446x_inRadioBand(frequency)) {
		TRACE_ERROR("SI   > Frequency out of range");
		TRACE_ERROR("SI   > abort transmission");
		return false;
	}

	TRACE_INFO("SI   > Tune Si446x (RX)");
	Si446x_setFrequency(frequency);		// Set frequency
	Si446x_setRXState();

	return true;
}




void Si446x_writeFIFO(uint8_t *msg, uint8_t size) {
	uint8_t write_fifo[size+1];
	write_fifo[0] = 0x66;
	memcpy(&write_fifo[1], msg, size);
	Si446x_write(write_fifo, size+1);
}

uint8_t Si446x_freeFIFO(void) {
	const uint8_t fifo_info[2] = {0x15, 0x00};
	uint8_t rxData[4];
	Si446x_read(fifo_info, 2, rxData, 4);
	return rxData[3];
}






int16_t Si446x_getTemperature(void) {
	const uint8_t txData[2] = {0x14, 0x10};
	uint8_t rxData[8];
	Si446x_read(txData, 2, rxData, 8);
	uint16_t adc = rxData[7] | ((rxData[6] & 0x7) << 8);
	return (89900*adc)/4096 - 29300;
}

int16_t Si446x_getLastTemperature(void) {
	return lastTemp;
}

