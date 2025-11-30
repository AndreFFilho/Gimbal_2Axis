"""
Processo separado que roda o cliente MQTT do gimbal.

Este modulo é pensado para rodar em um processo filho. Ele:
- recebe comandos da GUI pela cmd_queue (pitch/roll)
- envia telemetria, status e logs para a GUI pela tel_queue
- obedece comandos de controle (reconnect, shutdown) pela ctl_queue
"""

import multiprocessing as mp
import time
import signal
import traceback
from MQTT.cliente import ConexaoGimbalMQTT

def run_mqtt(cmd_queue: mp.Queue, tel_queue: mp.Queue, ctl_queue: mp.Queue = None):
    """
    Loop principal do processo MQTT.
    cmd_queue: fila para receber comandos da GUI (dict com "pitch" e "roll").
    tel_queue: fila para enviar telemetria, status e logs para a GUI.
    ctl_queue: fila para comandos de controle (reconnect, shutdown).
    """
    def _term(sig, frame):
        raise KeyboardInterrupt()

    signal.signal(signal.SIGTERM, _term)
    signal.signal(signal.SIGINT, _term)

    mqtt = ConexaoGimbalMQTT()

    # Callback de telemetria: envia dicts para tel_queue
    def _on_tel_dict(d: dict):
        try:
            if "__log__" in d:
                payload = d.get("__log__", {})
                tel_queue.put_nowait({"type": "log", "data": payload})
            else:
                tel_queue.put_nowait({"type": "telemetry", "data": d})
        except Exception:
            try:
                if "__log__" in d:
                    payload = d.get("__log__", {})
                    tel_queue.put({"type": "log", "data": payload})
                else:
                    tel_queue.put({"type": "telemetry", "data": d})
            except Exception:
                pass

    mqtt.set_tel_callback(_on_tel_dict)


    # Callback de status da conexão
    def _on_status(ok: bool, msg: str):
        try:
            tel_queue.put_nowait({"type": "status", "ok": bool(ok), "msg": str(msg)})
        except Exception:
            tel_queue.put({"type": "status", "ok": bool(ok), "msg": str(msg)})

    mqtt.vincular_status(_on_status)

    try:
        mqtt.conectar()
    except Exception as e:
        tb = traceback.format_exc()
        try:
            tel_queue.put({"type": "error", "msg": f"Falha ao conectar MQTT: {e}\n{tb}"})
        except Exception:
            try:
                tel_queue.put({"type": "error", "msg": f"Falha ao conectar MQTT: {e}"})
            except Exception:
                pass

    try:
        while True:

            # Trata comandos de controle
            if ctl_queue:
                try:
                    ctl = ctl_queue.get_nowait()
                except Exception:
                    ctl = None
                if ctl == "shutdown":
                    break
                elif ctl == "reconnect":
                    try:
                        mqtt.desconectar()
                    except Exception:
                        pass
                    try:
                        mqtt.conectar()
                    except Exception:
                        try:
                            tel_queue.put_nowait({"type": "error", "msg": "reconnect failed"})
                        except Exception:
                            pass

            # Pega comandos vindos da GUI (pitch/roll)
            try:
                cmd = cmd_queue.get(timeout=0.1)
            except Exception:
                cmd = None

            if cmd is not None:
                try:
                    mqtt.publish_cmd(cmd, debounce=True)
                except Exception as e:
                    try:
                        tel_queue.put({"type": "error", "msg": f"publish_cmd failed: {e}"})
                    except Exception:
                        pass

            time.sleep(0.01)

    except KeyboardInterrupt:
        pass

    finally:
        try:
            mqtt.desconectar()
        except Exception:
            pass
        try:
            tel_queue.put({"type": "status", "ok": False, "msg": "mqtt_process_exited"})
        except Exception:
            pass