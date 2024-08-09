#include "bmi270.h"
#include "imu.h"

extern const uint8_t bmi270_config_file[8193];
uint8_t spiWorking = 0;
uint8_t initWorking = 0;
uint8_t bmiReady = 0;

// buffer to use for internal read spi calls. save memory
uint8_t bmi270_init_spi_buf[2] = { 0x00, 0x00 };
uint8_t bmi270_data_spi_buf[14] = { 0x00 };

uint8_t tempTransmit[14] = { BMI270_REG_ACC_DATA_X_LSB | 0x80, 0x00 };

void cs_low() {
	HAL_GPIO_WritePin(CS_GPIO_Port_BMI270, CS_Pin_BMI270, GPIO_PIN_RESET);
}
void cs_high() {
	HAL_GPIO_WritePin(CS_GPIO_Port_BMI270, CS_Pin_BMI270, GPIO_PIN_SET);
}

void BMI270ReadData() // This is answered in an interrupt callback in imu.c (HAL_SPI_TxRxCpltCallback)
{
	cs_low();
	HAL_SPI_TransmitReceive_DMA(hspi_bmi270, tempTransmit, bmi270_data_spi_buf,
			14);
}

int16_t getCAS() {
	//TODO: get constant register from enum
	uint8_t gyro_cas = read_register(BMI270_REG_CAS, bmi270_init_spi_buf);
	int16_t gyro_cas_signed;
	gyro_cas = gyro_cas & 0x7F; //7 bit value
	if (gyro_cas & 0x40) // 7 bit signed integer
			{
		gyro_cas_signed = (int16_t) (((int16_t) gyro_cas) - 128);
	} else {
		gyro_cas_signed = (int16_t) (gyro_cas);
	}
	return gyro_cas_signed;
}

// Details for this init sequence are described in the BMI270 datasheet.
void BMI270Init() {
	bmi270EnableSPI();
	HAL_TIM_Base_Start_IT(exti_tim);
	spiWorking = read_register(BMI270_REG_CHIP_ID, bmi270_init_spi_buf) == 0x24;

	if (spiWorking) {
		write_register(BMI270_REG_PWR_CONF, 0x00);
		HAL_Delay(10);
		write_register(BMI270_REG_INIT_CTRL, 0x00);
		HAL_Delay(1);
		// config file has target register 0x5e included, allows us to just pass in the array without doing any appending
		burst_transmit((uint8_t*) bmi270_config_file, 100,
				sizeof(bmi270_config_file));
		HAL_Delay(1);
		write_register(BMI270_REG_INIT_CTRL, 0x01);
		HAL_Delay(40);
		HAL_DMA_Init(&hdma_spi1_rx);
		HAL_DMA_Init(&hdma_spi1_tx);
		initWorking = read_register(BMI270_REG_INTERNAL_STATUS,
				bmi270_init_spi_buf) == 0x01;
		if (initWorking) {
			configureBMI270();
			configureBMI270EXTI();
			bmiReady = 1;
		}
	}
}

// TODO: not constants
void configureBMI270() {
	write_register(BMI270_REG_PWR_CTRL, 0x0E);
	HAL_Delay(1);
	write_register(BMI270_REG_ACC_CONF, 0xA8);
	HAL_Delay(1);
	write_register(BMI270_REG_GYRO_CONF, 0xA9);
	HAL_Delay(1);
	write_register(BMI270_REG_PWR_CONF, 0x02);
	HAL_Delay(1);
	write_register(BMI270_REG_ACC_RANGE, 0x00);
	HAL_Delay(1);
	write_register(BMI270_REG_GYRO_RANGE, 0b00001000);
	HAL_Delay(1);
}

void configureBMI270EXTI() {
	write_register(BMI270_REG_INT_MAP_DATA, 0b01000100);
	HAL_Delay(10);
	write_register(BMI270_REG_INT1_IO_CTRL, 0b00001010);
	HAL_Delay(10);
	write_register(BMI270_REG_INT2_IO_CTRL, 0b00001010);
}

void bmi270EnableSPI() {
	cs_low();
	HAL_Delay(1);
	cs_high();
	HAL_Delay(10);
}

//TODO: CHECK HAL_OK
uint8_t read_register(uint8_t rgstr, uint8_t *out_buf) {
	rgstr = rgstr | 0x80;
	cs_low();
	//TODO: remove dummy delay values
	HAL_SPI_Transmit(hspi_bmi270, &rgstr, 1, 10);
	HAL_SPI_Receive(hspi_bmi270, out_buf, 2, 10);
	cs_high();
	return out_buf[1];
}

void write_register(uint8_t rgstr, uint8_t data) {
	cs_low();
	uint8_t buf[2] = { rgstr, data };
	HAL_SPI_Transmit(hspi_bmi270, buf, 2, 10);
	cs_high();
}

uint8_t* burst_read(uint8_t rgstr, uint8_t *out_buf, uint16_t size,
		uint32_t timeout) {
	rgstr = rgstr | 0x80;
	cs_low();
	//TODO: remove dummy delay values
	HAL_SPI_Transmit(hspi_bmi270, &rgstr, 1, 10);
	HAL_SPI_Receive(hspi_bmi270, out_buf, size + 1, timeout);
	cs_high();
	return out_buf;
}

void burst_transmit(uint8_t *transmit_buf, uint32_t timeout, uint16_t size) {
	cs_low();
	HAL_SPI_Transmit(hspi_bmi270, transmit_buf, size, timeout);
	cs_high();
}

// BMI 270 binary to decimal converters
float lsb_to_mps2(int16_t val, float g_range, uint8_t bit_width) {
	double power = 2;

	float half_scale =
			(float) ((pow((double) power, (double) bit_width) / 2.0));

	return (val * g_range) / half_scale;
}

float lsb_to_dps(int16_t val, float dps, uint8_t bit_width) {
	double power = 2;

	float half_scale =
			(float) ((pow((double) power, (double) bit_width) / 2.0));

	return (dps / (half_scale)) * (val);
}
