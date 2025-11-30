"""
Ponto de entrada da Interface Gráfica do Gimbal.

Este módulo inicia a aplicação Flet e carrega a interface
principal definida em GUI.app (função construir_interface).

Fluxo:
- Inicializa o app Flet e ajusta o multiprocessing no Windows
- A interface se conecta automaticamente ao cliente MQTT definido
  em MQTT.cliente, exibindo status, telemetria e enviando comandos.

APENAS INICIALIZA A GUI
"""
import flet as ft
from GUI.app import construir_interface

if __name__ == "__main__":
    import multiprocessing as mp
    import platform

    if platform.system() == "Windows":
        mp.set_start_method("spawn", force=True)

    ft.app(target=construir_interface, view=ft.AppView.FLET_APP)