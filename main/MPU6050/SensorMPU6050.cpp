// --- Includes Padrão e de Biblioteca ---
#include <driver/i2c.h>
#include "log_mqtt.h"
#include <esp_err.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "BufferTelemetria.h"

// --- Includes das Bibliotecas C++ do MPU6050 ---
#include "MPU6050.h"
#include "MPU6050_6Axis_MotionApps20.h"
#include "sdkconfig.h"
#include "mainGlobals.h"
#include "SensorMPU6050.h"

// --- Pinos I2C sensor MPU6050 ---
#define PIN_SDA 21
#define PIN_SCL 22

// --- Variáveis Globais Internas da Tarefa ---
uint16_t packetSize = 42;   					// Tamanho do pacote DMP (default é 42 bytes)
uint16_t fifoCount;         					// Contagem de todos os bytes atualmente no FIFO
uint8_t fifoBuffer[64];     					// Buffer de armazenamento FIFO
uint8_t mpuIntStatus;       					// Armazena o byte de status de interrupção atual do MPU

// Task de inicialização do barramento I2C
void task_initI2C(void *ignore) {
	i2c_config_t conf;
	conf.mode = I2C_MODE_MASTER;
	conf.sda_io_num = (gpio_num_t)PIN_SDA;
	conf.scl_io_num = (gpio_num_t)PIN_SCL;
	conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
	conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
	conf.master.clk_speed = 100000;	 			// Clock I2C de 400kHz
	ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &conf));
	ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0));
	vTaskDelete(NULL);
}

// Task principal para ler dados do MPU6050 via DMP
void task_mpu(void*){
    MPU6050 mpu = MPU6050();
	mpu.initialize();							// Inicializa comunicação com o MPU6050

	// Calibra offsets de fábrica (Acelerômetro e Giroscópio)
	printf("Iniciando calibração do MPU6050...\n");
	mpu.CalibrateAccel(6);
	printf("Aceleração calibrada.\n");
	mpu.CalibrateGyro(6);
	printf("Giroscópio calibrado.\n");

	// Inicializa o DMP (Digital Motion Processor)
	mpu.dmpInitialize();
	mpu.setDMPEnabled(true);
	printf("DMP inicializado com sucesso.\n");
	
	// Obtém o tamanho real do pacote DMP configurado
	packetSize = mpu.dmpGetFIFOPacketSize();

	xSemaphoreGive(g_mpu_pronta); // Indica que o MPU está pronto

	// Variáveis para armazenar dados de orientação e erro
	Quaternion q;								// Quaternion lido do DMP
	VectorFloat gravity;						// Vetor de gravidade calculado a partir do 'q'
	float ypr[3];    							// [yaw, pitch, roll] em radianos calculados a partir de 'q' e 'gravity' (em radianos)

	// Variáveis locais para telemetria
	float pr_telemetry[2];						// Dados de telemetria [pitch, roll] (em graus)
    int telemetry_counter = 0;
    const int TELEMETRY_DIVIDER = 20; 			// Envia telemetria a cada 20 ciclos (~10Hz)

	while(1){
		mpuIntStatus = mpu.getIntStatus();
		fifoCount = mpu.getFIFOCount();

		// Verifica condição de Overflow do FIFO
		if ((mpuIntStatus & 0x10) || fifoCount == 1024) {
			mpu.resetFIFO();			// Reseta o FIFO em caso de overflow
			LOGW("MPU6050", "FIFO overflow!");
		} 
		
		// Verifica se há dados prontos (flag de interrupção DMP)
		else if (mpuIntStatus & 0x02) {
			while (fifoCount > packetSize) {
				mpu.getFIFOBytes(fifoBuffer, packetSize);
				fifoCount -= packetSize;
			}

			// Espera ter pelo menos um pacote completo no FIFO
			if (fifoCount == packetSize) {
				mpu.getFIFOBytes(fifoBuffer, packetSize);	// Lê um pacote do FIFO
				mpu.dmpGetQuaternion(&q, fifoBuffer);		// Extrai o Quaternion (orientação) do pacote lido
				q.normalize();								// Normaliza o quaternion	

				// Verificação de Sanidade: Checa se a magnitude do quaternion está próxima de 1
				float mag_sq = q.w*q.w + q.x*q.x + q.y*q.y + q.z*q.z;
				if (fabs(mag_sq - 1.0f) > 0.01f) {
					LOGW("MPU6050", "Pacote de quaternion invalido descartado! Mag^2: %f", mag_sq);
					// Pula para a próxima iteração do loop, descartando este pacote ruim.
					continue; 
				}

				// Converte o Quaternion para ângulos de Euler (Yaw, Pitch, Roll)
				// Usa a função da biblioteca que calcula a gravidade internamente
				mpu.dmpGetGravity(&gravity, &q);
				mpu.dmpGetYawPitchRoll(ypr, &q, &gravity); 	// ypr: [0]=Yaw, [1]=Pitch, [2]=Roll (radianos)

				// Apenas atualiza a variável global com a última leitura válida
                xSemaphoreTake(mutex_sensor_data, portMAX_DELAY);
                pr_medido[0] = ypr[1]; // Pitch em Radianos
                pr_medido[1] = ypr[2]; // Roll em Radianos
                xSemaphoreGive(mutex_sensor_data);

				// Converte para graus para telemetria
				pr_telemetry[0] = ypr[1] * 180.0f / M_PI;
				pr_telemetry[1] = ypr[2] * 180.0f / M_PI;

				// Envia o ângulo para a interface MQTT
				telemetry_counter++;
				if (telemetry_counter >= TELEMETRY_DIVIDER) {
				 	telemetry_counter = 0; // Reseta o contador
				 	// Envia o ângulo atual (em graus) para a fila de telemetria
					// Envia os dados para o buffer circular de telemetria
				 	if (!buffer_telemetria_gravar(pr_telemetry, 0)) {
				 		LOGW("MPU6050", "Buffer de telemetria cheio - dado descartado");
				 	}
				}
			}
		}
	}
	vTaskDelete(NULL);
}	