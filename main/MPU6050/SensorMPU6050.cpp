// --- Includes Padrão e de Biblioteca ---
#include <driver/i2c.h>
#include "log_mqtt.h"
#include <esp_err.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "BufferTelemetria.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"

// --- Includes das Bibliotecas C++ do MPU6050 ---
#include "MPU6050.h"
#include "sdkconfig.h"
#include "mainGlobals.h"
#include "SensorMPU6050.h"

// --- Pinos I2C sensor MPU6050 ---
#define PIN_SDA 21
#define PIN_SCL 22

// --- Filtro de Kalman ---
class KalmanFilter {
public:
    float angle = 0.0f;
    float bias  = 0.0f;
    float P[2][2] = {{0,0},{0,0}};

    float Q_angle = 0.001f;
    float Q_bias  = 0.005f;
    float R_measure = 0.03f; 

    void predict(float gyro_rate, float dt) {
        angle += dt * (gyro_rate - bias);
        P[0][0] += dt * (dt*P[1][1] - P[0][1] - P[1][0] + Q_angle);
        P[0][1] -= dt * P[1][1];
        P[1][0] -= dt * P[1][1];
        P[1][1] += Q_bias * dt;
    }

    void update(float measured_angle) {
        float y = measured_angle - angle;
        float S = P[0][0] + R_measure;

        float K0 = P[0][0] / S;
        float K1 = P[1][0] / S;

        angle += K0 * y;
        bias  += K1 * y;

        float P00_temp = P[0][0];
        float P01_temp = P[0][1];

        P[0][0] -= K0 * P00_temp;
        P[0][1] -= K0 * P01_temp;
        P[1][0] -= K1 * P00_temp;
        P[1][1] -= K1 * P01_temp;
    }
};

static KalmanFilter kalmanPitch;
static KalmanFilter kalmanRoll;

// Task de inicialização do barramento I2C
void task_initI2C(void *ignore) {
    i2c_config_t conf = {};
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = (gpio_num_t)PIN_SDA;
    conf.scl_io_num = (gpio_num_t)PIN_SCL;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = 400000; // 400kHz para velocidade
    ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0));
    vTaskDelete(NULL);
}

// Task de inicialização do barramento I2C
void task_mpu(void *) {
    MPU6050 mpu;
    // Inicializa comunicação com o MPU6050
    mpu.initialize();

    if (!mpu.testConnection()) {
        printf("ERRO: MPU6050 não conectado!\n");
        vTaskDelete(NULL);
    }
    printf("MPU6050 conectado.\n");

	// Calibrações de Offset pré-definidas
    mpu.setXAccelOffset(-3678); mpu.setYAccelOffset(-2954); mpu.setZAccelOffset(1392);

	// Escala Padrão +/- 2g (1g = 16384)
    mpu.setFullScaleAccelRange(MPU6050_ACCEL_FS_2);
    mpu.setFullScaleGyroRange(MPU6050_GYRO_FS_500);

    printf("Calibrando Giroscópio...\n");
    mpu.CalibrateGyro(20);
    printf("Calibração concluída.\n");

    // Leitura Inicial para definir ângulos iniciais
    int32_t sum_ax = 0, sum_ay = 0, sum_az = 0;
    int32_t sum_gx = 0, sum_gy = 0;
    int16_t ax, ay, az, gx, gy, gz;

    for (int i=0; i<100; i++) {
        mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);
        sum_ax += ax; sum_ay += ay; sum_az += az;
        sum_gx += gx; sum_gy += gy;
        esp_rom_delay_us(1000);
    }

    float avg_ax = sum_ax / 100.0f;
    float avg_ay = sum_ay / 100.0f;
    float avg_az = sum_az / 100.0f;
    float avg_gx = sum_gx / 100.0f;
    float avg_gy = sum_gy / 100.0f;

    // Pitch (Y): atan2(-ax, sqrt(ay² + az²))
    float init_pitch = atan2(-avg_ax, sqrt(avg_ay*avg_ay + avg_az*avg_az));

    // Roll (X): atan2(ay, az)
    float init_roll  = atan2(avg_ay, avg_az);

    // Inicializa Filtros de Kalman com os valores iniciais
    kalmanRoll.angle  = init_roll;
    kalmanPitch.angle = init_pitch;
    kalmanRoll.bias  = (avg_gx / 131.0f) * (M_PI/180.0f);
    kalmanPitch.bias = (avg_gy / 131.0f) * (M_PI/180.0f);

    // Indica que o MPU está pronto
    xSemaphoreGive(g_mpu_pronta);

    // Tempo de loop da task do MPU6050
    int64_t last_time = esp_timer_get_time();
    int telemetry_counter = 0;

    while(1) {
        int64_t now = esp_timer_get_time();

        // Delta time em segundos
        float dt = (now - last_time) / 1000000.0f;
        last_time = now;

        // Lê dados brutos do sensor
        mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);

        // Converte para unidades físicas
        float gxr = (gx/65.0f)*(M_PI/180.0f);
        float gyr = (gy/65.0f)*(M_PI/180.0f);

        // Pitch (Eixo X do sensor, rotação sobre Y)
        float acc_p = atan2((float)-ax, sqrt((float)ay*ay + (float)az*az));

        // Roll (Eixo Y do sensor, rotação sobre X) 
        float acc_r = atan2((float)ay, (float)az);

        // Atualiza Filtros de Kalman
        kalmanRoll.predict(gxr, dt);
        kalmanRoll.update(acc_r);

        kalmanPitch.predict(gyr, dt);
        kalmanPitch.update(acc_p);

        // Atualiza variáveis globais de ângulo
        xSemaphoreTake(mutex_sensor_data, portMAX_DELAY);
        pr_medido[0] = kalmanPitch.angle;
        pr_medido[1] = kalmanRoll.angle;
        xSemaphoreGive(mutex_sensor_data);

        // Envia o ângulo para a interface MQTT
        telemetry_counter++;
        if (telemetry_counter >= 50) {  // 20Hz Telemetria
            telemetry_counter = 0;      // Reseta o contador
            // Envia o ângulo atual (em graus) para a fila de telemetria
			// Envia os dados para o buffer circular de telemetria
            float tel[2];
            tel[0] = kalmanPitch.angle * 180/M_PI;
            tel[1] = kalmanRoll.angle * 180/M_PI;
            buffer_telemetria_gravar(tel, 0);            
        }
		vTaskDelay(pdMS_TO_TICKS(1));
    }
}