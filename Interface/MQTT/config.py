"""
Configurações MQTT do projeto Gimbal.

Observações:
 - GUI publica comandos JSON em TOPICO_CMD
 - ESP32 publica telemetria JSON em TOPICO_TEL
"""

# MQTT
SERVIDOR_MQTT = "LINK OU IP DO BROKER"   
PORTA_MQTT = 8883
USUARIO_MQTT = "Interface"                 
SENHA_MQTT = "Interface123"                   
MANTER_VIVO = 30                  # keepalive (s)

# Publicação/Assinatura
# Modo com 2 tópicos (cmd/tel) e JSON com chaves 'pitch'/'roll'
MODO = "json_cmd_tel"             # "json_cmd_tel" | "json_single" | "two_topics"

# Tópicos novos (modo json_cmd_tel)
TOPICO_CMD = "gimbal/cmd"         # GUI -> ESP32 (comando)
TOPICO_TEL = "gimbal/tel"         # ESP32 -> GUI (telemetria)

# Compatibilidade com modos antigos (pode deixar como está)
TOPICO_BASE = "gimbal"
TOPICO_INCLINACAO = "pitch"
TOPICO_ROLAGEM = "roll"

# Chaves JSON (GUI/ESP32 falam o mesmo "dialeto")
CHAVE_JSON_INCLINACAO = "pitch"
CHAVE_JSON_ROLAGEM    = "roll"

#CONFIG MQTT
QOS = 0
RETER = False
ASSINAR_TELEMETRIA = True