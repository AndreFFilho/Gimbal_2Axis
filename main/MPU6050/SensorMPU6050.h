// main/MPU6050/SensorMPU6050.h

#ifndef SENSORMPU6050_H
#define SENSORMPU6050_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Tarefa de inicialização do barramento I2C
 * Esta tarefa se auto_deleta após a execução.
 */
void task_initI2C(void *ignore);

/**
 * @brief Tarefa principal para ler dados do MPU6050 via DMP
 */
void task_mpu(void *);

#ifdef __cplusplus
}
#endif

#endif // SENSORMPU6050_H