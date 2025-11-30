"""
Cliente MQTT da interface do Gimbal.

Correções importantes:
- publish_cmd agora aceita dict {"pitch":..., "roll":...}
- debounce isolado em publicar_com_debounce()
- _publicar_ultimo publica diretamente sem recursão
"""

import json
import ssl
import logging
import traceback
from threading import Lock, Timer
from typing import Callable, Optional
import paho.mqtt.client as mqtt

from MQTT.config import (
    SERVIDOR_MQTT, PORTA_MQTT, USUARIO_MQTT, SENHA_MQTT, MANTER_VIVO,
    MODO, TOPICO_BASE, TOPICO_INCLINACAO, TOPICO_ROLAGEM,
    CHAVE_JSON_INCLINACAO, CHAVE_JSON_ROLAGEM, QOS, RETER,
    ASSINAR_TELEMETRIA, TOPICO_CMD, TOPICO_TEL
)

TOPICO_LOG = "gimbal/log"

class ConexaoGimbalMQTT:
    """Gerencia a conexão com o broker MQTT (HiveMQ Cloud), publicação e assinatura de telemetria."""

    def __init__(self):
        self._cli = None
        try:
            try:
                from paho.mqtt.client import CallbackAPIVersion as _CBV
                self._cli = mqtt.Client(callback_api_version=_CBV.VERSION2)
            except Exception:
                self._cli = mqtt.Client()
        except Exception as e:
            print("Erro ao criar mqtt.Client():", e)
            print(traceback.format_exc())
            self._cli = None

        # Configurar credenciais + TLS
        if self._cli is not None:
            try:
                if USUARIO_MQTT or SENHA_MQTT:
                    self._cli.username_pw_set(USUARIO_MQTT, SENHA_MQTT)
            except Exception as e:
                print("Erro em username_pw_set():", e)
                print(traceback.format_exc())

            try:
                self._cli.tls_set(tls_version=ssl.PROTOCOL_TLS_CLIENT)
                self._cli.tls_insecure_set(False)
            except Exception:
                try:
                    self._cli.tls_set()
                except Exception as e:
                    print("Erro TLS:", e)
                    print(traceback.format_exc())

            try:
                self._cli.enable_logger()
                logging.getLogger("paho").setLevel(logging.CRITICAL + 1)
            except Exception:
                pass

        # Estado e callbacks
        self._cb_status: Optional[Callable[[bool, str], None]] = None
        self._cb_tel: Optional[Callable[[float, float, Optional[float]], None]] = None
        self._cb_tel_dict: Optional[Callable[[dict], None]] = None

        self._conectado = False

        # Debounce
        self._lock = Lock()
        self._ultimo_envio = None
        self._debounce_timer: Optional[Timer] = None

        # Callbacks MQTT
        if self._cli is not None:
            self._cli.on_connect = self._ao_conectar
            self._cli.on_disconnect = self._ao_desconectar
            self._cli.on_message = self._ao_mensagem

    # ============================== PROPRIEDADES ==============================
    @property
    def conectado(self) -> bool:
        return self._conectado

    def vincular_status(self, callback):
        self._cb_status = callback

    def vincular_telemetria(self, callback):
        self._cb_tel = callback

    def set_tel_callback(self, cb):
        self._cb_tel_dict = cb

    # ============================== DEBOUNCE ==============================
    def publicar_com_debounce(self, pitch: float, roll: float, debounce_ms: int = 80):
        with self._lock:
            self._ultimo_envio = (pitch, roll)

            if self._debounce_timer:
                try: self._debounce_timer.cancel()
                except Exception: pass

            def _timer_cb():
                try:
                    self._publicar_ultimo()
                except Exception:
                    pass

            self._debounce_timer = Timer(debounce_ms / 1000.0, _timer_cb)
            self._debounce_timer.daemon = True
            self._debounce_timer.start()

    def _publicar_ultimo(self):
        with self._lock:
            if not self._ultimo_envio:
                return

            p, r = self._ultimo_envio
            obj = {"pitch": float(p), "roll": float(r)}

            try:
                self.publish_cmd(obj, debounce=False)
            except Exception:
                try:
                    self._cli.publish(TOPICO_CMD, json.dumps(obj), qos=QOS, retain=RETER)
                except Exception:
                    pass

            self._ultimo_envio = None
            self._debounce_timer = None

    # ============================== MQTT ==============================
    def conectar(self):
        if self._cb_status:
            self._cb_status(False, f"Conectando a {SERVIDOR_MQTT}:{PORTA_MQTT}...")

        try:
            self._cli.connect_async(SERVIDOR_MQTT, PORTA_MQTT, MANTER_VIVO)
            self._cli.loop_start()
        except Exception as e:
            if self._cb_status:
                self._cb_status(False, f"Falha ao conectar: {e}")
            print(traceback.format_exc())
            raise

    def desconectar(self):
        try: self._cli.disconnect()
        except Exception: 
            pass

        try: self._cli.loop_stop()
        except Exception: 
            pass

        self._conectado = False
        if self._cb_status:
            self._cb_status(False, "Desconectado.")

    # ============================== PUBLISH ==============================
    def publish_cmd(self, obj: dict, debounce: bool = False, debounce_ms: int = 80):
        if debounce:
            try:
                p = float(obj.get("pitch", 0.0))
                r = float(obj.get("roll", 0.0))
                self.publicar_com_debounce(p, r, debounce_ms=debounce_ms)
                return
            except Exception:
                pass

        try:
            payload = json.dumps(obj)
            self._cli.publish(TOPICO_CMD, payload, qos=QOS, retain=RETER)
        except Exception as e:
            print("Erro em publish_cmd:", e)
            print(traceback.format_exc())

    # ============================== CALLBACKS MQTT ==============================
    def _ao_conectar(self, client, userdata, flags=None, rc=0, *extra):
        self._conectado = (rc == 0)

        if self._cb_status:
            self._cb_status(self._conectado,
                            "Conectado." if rc == 0 else f"Erro de conexão (rc={rc}).")

        if rc != 0:
            return

        if ASSINAR_TELEMETRIA:
            try:
                if MODO == "json_cmd_tel":
                    client.subscribe(TOPICO_TEL, qos=QOS)
                elif MODO == "json_single":
                    client.subscribe(TOPICO_BASE, qos=QOS)
                elif MODO == "two_topics":
                    client.subscribe(f"{TOPICO_BASE}/{TOPICO_INCLINACAO}", qos=QOS)
                    client.subscribe(f"{TOPICO_BASE}/{TOPICO_ROLAGEM}",   qos=QOS)
            except Exception:
                pass

            try:
                client.subscribe(TOPICO_LOG, qos=QOS)
            except Exception:
                pass

    def _ao_desconectar(self, client, userdata, rc=0, *extra):
        self._conectado = False
        if self._cb_status:
            if rc == 0:
                self._cb_status(False, "Desconectado do broker.")
            else:
                self._cb_status(False, f"Desconectado (rc={rc}).")

    def _ao_mensagem(self, client, userdata, msg, *extra):
        try:
            topic = msg.topic

            if topic == TOPICO_LOG:
                try:
                    payload = json.loads(msg.payload.decode("utf-8"))
                except Exception:
                    payload = {"raw": msg.payload.decode("utf-8", errors="ignore")}

                if self._cb_tel_dict:
                    self._cb_tel_dict({"__log__": payload})
                return  

            if MODO in ("json_cmd_tel", "json_single"):
                payload = json.loads(msg.payload.decode("utf-8"))

                p = float(payload.get(CHAVE_JSON_INCLINACAO, 0.0))
                r = float(payload.get(CHAVE_JSON_ROLAGEM, 0.0))
                v = payload.get("vbat", None)
                v = float(v) if v is not None else None

                tel_dict = {"pitch": p, "roll": r}
                if v is not None:
                    tel_dict["vbat"] = v

                if self._cb_tel_dict:
                    self._cb_tel_dict(tel_dict)

                if self._cb_tel:
                    try:
                        self._cb_tel(p, r, v)
                    except TypeError:
                        self._cb_tel(p, r)

        except Exception as e:
            print("Erro ao processar telemetria:", e)
            print(traceback.format_exc())