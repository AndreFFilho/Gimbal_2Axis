"""
Logger MQTT do gimbal.

Conecta ao broker, assina o tópico de log (`gimbal/log`)
e grava as mensagens recebidas em um arquivo CSV (`gimbal_logs.csv`).
"""
import json
import csv
import os
from datetime import datetime
import ssl
import paho.mqtt.client as mqtt

from MQTT.config import (
    SERVIDOR_MQTT, PORTA_MQTT, USUARIO_MQTT, SENHA_MQTT, MANTER_VIVO,
)

# Tópico em que o ESP32 publica os logs
TOPIC_LOG = "gimbal/log"

# Arquivo CSV onde os logs serão guardados
CSV_PATH = "gimbal_logs.csv"

csv_file = None
csv_writer = None

def setup_csv(): 
    """Abre ou cria o .CSV"""

    global csv_file, csv_writer
    file_exists = os.path.exists(CSV_PATH)

    csv_file = open(CSV_PATH, "a", newline="", encoding="utf-8")
    csv_writer = csv.writer(csv_file)

    if not file_exists:
        csv_writer.writerow(["timestamp_pc", "level", "tag", "message"])
        csv_file.flush()

def on_connect(client, userdata, flags, rc, properties=None):
    """Callback chamado quando conecta ao broker MQTT."""

    print("Conectado ao MQTT, rc =", rc)
    client.subscribe(TOPIC_LOG, qos=0)


def on_message(client, userdata, msg):
    """Callback chamado quando chega mensagem no topico de log."""

    global csv_writer, csv_file

    timestamp = datetime.now().isoformat(timespec="seconds")

    try:
        payload = msg.payload.decode("utf-8")
        data = json.loads(payload)
        level = data.get("level", "INFO")
        tag   = data.get("tag", "")
        text  = data.get("msg", "")
    except Exception:
        level = "INFO"
        tag   = ""
        text  = msg.payload.decode("utf-8", errors="ignore")

    # Grava no arquivo
    csv_writer.writerow([timestamp, level, tag, text])
    csv_file.flush()

    # Mostra no terminal
    print(timestamp, level, tag, text)

def main():
    """Configura o CSV, conecta ao broker e entra no loop de mensagens."""

    setup_csv()
    client = mqtt.Client()

    # Usuario/senha definidos no config
    if USUARIO_MQTT or SENHA_MQTT:
        client.username_pw_set(USUARIO_MQTT, SENHA_MQTT)

    # TLS com certificados padrao
    client.tls_set(tls_version=ssl.PROTOCOL_TLS_CLIENT)
    client.tls_insecure_set(False)

    client.on_connect = on_connect
    client.on_message = on_message

    client.connect(SERVIDOR_MQTT, PORTA_MQTT, MANTER_VIVO)
    client.loop_forever()

if __name__ == "__main__":
    main()
    