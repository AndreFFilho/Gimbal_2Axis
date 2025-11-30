// --- Includes Padrão e de Biblioteca ---
#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "log_mqtt.h"

// --- Includes das Bibliotecas C++ SimpleFOC ---
#include "esp_simplefoc.h"
#include "ControladorPID.h"
#include "mainGlobals.h"

// --- Definições para o controle do motor ---
#define IN1_1 19
#define IN2_1 18
#define IN3_1 17
#define EN1 4
#define IN1_2 25
#define IN2_2 26
#define IN3_2 27
#define EN2 14

const float deadzone = 0.0349066;       // 2 graus em radianos
const float MAX_ANGLE = 1.46608f;		// Ângulo, em radiano, máximo permitido para Pitch e Roll (para evitar Gimbal Lock)

#define MAX_INTEGRADOR 30.0f
#define MIN_INTEGRADOR -30.0f
#define D_FILTER_ALPHA 0.01f

// --- Variáveis Globais de Controle (Privadas) ---
static BLDCMotor motor_pitch = BLDCMotor(7);                           // Motor de brushless com 7 polos
static BLDCDriver3PWM driver_pitch = BLDCDriver3PWM(IN1_1, IN2_1, IN3_1, EN1);

static BLDCMotor motor_roll = BLDCMotor(7);                            // Motor de brushless com 7 polos
static BLDCDriver3PWM driver_roll = BLDCDriver3PWM(IN1_2, IN2_2, IN3_2, EN2);

// --- LÓGICA PID (Privada) ---
// Estrutura PID
typedef struct {
    float kp, ki, kd;
    float integrador;
    float erro_anterior;
    float derivada_filtrada;
} PID_t;

// Inicializa o controlador PID
void PID_Init(PID_t *pid, float kp, float ki, float kd) {
    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
    pid->integrador = 0;
    pid->erro_anterior = 0;
    pid->derivada_filtrada = 0;
}

// Calcula a saída do controlador PID
float PID_Compute(PID_t *pid, float erro, float dt) {
    if (dt <= 0.0f) return 0.0f; // Evita divisão por zero ou valores negativos

    // Atualiza o integrador com anti-windup
    pid->integrador += erro*dt;
    if(pid->integrador > MAX_INTEGRADOR) pid->integrador = MAX_INTEGRADOR; // Anti-windup
    else if(pid->integrador < MIN_INTEGRADOR) pid->integrador = MIN_INTEGRADOR; // Anti-windup

    // Calcula a derivada com filtro passa-baixa
    float derivada = (erro - pid->erro_anterior) / dt;
    pid->derivada_filtrada = (D_FILTER_ALPHA * derivada) + (1.0f - D_FILTER_ALPHA) * pid->derivada_filtrada;

    // Armazena o erro atual para a próxima iteração
    pid->erro_anterior = erro;

    return (pid->kp * erro) +
           (pid->ki * pid->integrador) +
           (pid->kd * pid->derivada_filtrada);
}

// --- Tarefa Principal ---
void task_pid(void *ignore) {
    xSemaphoreTake(g_mpu_pronta, portMAX_DELAY);

    LOGI("PID", "Iniciando tarefa PID...");

    // Configuração do driver BLDC
    driver_pitch.voltage_power_supply = 12;
    driver_roll.voltage_power_supply = 12;
    driver_pitch.voltage_limit = 11;
    driver_roll.voltage_limit = 11;
    driver_pitch.init(0);
    driver_roll.init(1);

    // Configuração do motor BLDC
    motor_pitch.linkDriver(&driver_pitch);
    motor_roll.linkDriver(&driver_roll);
    motor_pitch.velocity_limit = 20;
    motor_roll.velocity_limit = 20;
    motor_pitch.voltage_limit = 3;
    motor_roll.voltage_limit = 3;
    motor_pitch.current_limit = 0.5f;
    motor_roll.current_limit = 0.5f;

    // Inicialização dos motores
    motor_pitch.controller = MotionControlType::velocity_openloop;
    motor_roll.controller = MotionControlType::velocity_openloop;
    motor_pitch.init();
    motor_roll.init();

    // Inicialização do PID
    PID_t pid_pitch, pid_roll;
    PID_Init(&pid_pitch, 1.0f, 0.01f, 0.6f);
    PID_Init(&pid_roll,  1.0f, 0.01f, 0.25f);

    const float dt = 0.005f; // 5ms de tempo fixo para o PID
    float erro_pitch, erro_roll;
    float setpoint_pitch, setpoint_roll;
    float medicao_pitch_rad, medicao_roll_rad;

    const TickType_t xFrequency = pdMS_TO_TICKS(5); // 5ms
    TickType_t xLastWakeTime = xTaskGetTickCount();

    LOGI("PID", "Iniciando loop de cálculo PID...");
    
    while (1) {
        // 1. ESPERA ATÉ O PRÓXIMO CICLO DE 5ms
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
        
        // 2. PEGA O SETPOINT ATUALIZADO
		xSemaphoreTake(mutex_pr, portMAX_DELAY);
		setpoint_pitch = pr[0] * M_PI / 180.0f;	// Converte para radianos
		setpoint_roll = pr[1] * M_PI / 180.0f;	// Converte para radianos
		xSemaphoreGive(mutex_pr);

        // 3. APLICA LIMITE DE SEGURANÇA
        // Aplica um limite de segurança ao setpoint para evitar Gimbal Lock
        setpoint_pitch = fmaxf(-MAX_ANGLE, fminf(MAX_ANGLE, setpoint_pitch)); // Limita Pitch
        setpoint_roll = fmaxf(-MAX_ANGLE, fminf(MAX_ANGLE, setpoint_roll)); // Limita Roll

        // 4. PEGA A ÚLTIMA MEDIÇÃO DO SENSOR
        xSemaphoreTake(mutex_sensor_data, portMAX_DELAY);
        medicao_pitch_rad = pr_medido[0]; // Já está em radianos
        medicao_roll_rad  = pr_medido[1]; // Já está em radianos
        xSemaphoreGive(mutex_sensor_data);

        // 5. CALCULA O ERRO
        erro_pitch = setpoint_pitch - medicao_pitch_rad;	// Erro de Pitch em radianos
        erro_roll = setpoint_roll - medicao_roll_rad;		// Erro de Roll em radianos

        // 6. APLICA DEADZONE
        if (fabsf(erro_pitch) < deadzone) {
            erro_pitch = 0.0f;
        }
        if (fabsf(erro_roll) < deadzone) {
            erro_roll = 0.0f;
        }

        // 7. CALCULA O PID COM O 'dt' FIXO
        float output_pitch = PID_Compute(&pid_pitch, erro_pitch, dt);
        float output_roll  = PID_Compute(&pid_roll,  erro_roll, dt);

        // 8. ATUALIZA A SAÍDA PARA O MOTOR
        motor_pitch.move(output_pitch);
        motor_roll.move(output_roll);
    }
}
