# [Guía] Ventana Tk del banco. Arranca el puente, presenta datos, conserva la consola y registra la sesión. Los
# [Guía] hilos se comunican por colas y la interfaz se actualiza desde Tk.
"""Dashboard de escritorio del convertidor multipuerto.

Ejecutar `python dashboard.py`: abre una ventana, sin abrir COM automaticamente.
`python dashboard.py --demo` permite inspeccionar la interfaz sin instrumentos.
La ventana se refresca cada200 ms; LIVE trae datos cortos a su propio ritmo.

Arquitectura: Tk en el hilo principal; un hilo lee stdout del puente original,
lo copia al terminal/archivo y pone datos en una cola. El puente original vive
en un subprocess y conserva PING, ACK, limites BK y secuencia de cierre. Ningun
widget ni temporizador calcula PWM, estima SOC o cambia modos por su cuenta.
"""
# [Guía] Importa argparse para las operaciones que realiza este módulo.
import argparse
# [Guía] Importa deque para las operaciones que realiza este módulo.
from collections import deque
# [Guía] Importa HistoryBuffer, HistorySampler, SessionHistory para las operaciones que realiza este módulo.
from dashboard_history import HistoryBuffer, HistorySampler, SessionHistory
# [Guía] Importa PlotWindow para las operaciones que realiza este módulo.
from dashboard_plots import PlotWindow
# [Guía] Importa DisplayFilter, DISPLAY_FILTER_TAU_S para las operaciones que realiza este módulo.
from dashboard_filter import DisplayFilter, DISPLAY_FILTER_TAU_S
# [Guía] Importa datetime, timezone para las operaciones que realiza este módulo.
from datetime import datetime, timezone
# [Guía] Importa json para las operaciones que realiza este módulo.
import json
# [Guía] Importa os para las operaciones que realiza este módulo.
import os
# [Guía] Importa Path para las operaciones que realiza este módulo.
from pathlib import Path
# [Guía] Importa queue para las operaciones que realiza este módulo.
import queue
# [Guía] Importa subprocess para las operaciones que realiza este módulo.
import subprocess
# [Guía] Importa sys para las operaciones que realiza este módulo.
import sys
# [Guía] Importa threading para las operaciones que realiza este módulo.
import threading
# [Guía] Importa time para las operaciones que realiza este módulo.
import time
# [Guía] Importa tkinter para las operaciones que realiza este módulo.
import tkinter as tk
# [Guía] Importa messagebox, scrolledtext, ttk para las operaciones que realiza este módulo.
from tkinter import messagebox, scrolledtext, ttk

# [Guía] Importa normalize_input para las operaciones que realiza este módulo.
from control_bk8502 import normalize_input
# [Guía] Importa DashboardState, TerminalFormatter, demo_frame, operation_label para las operaciones que realiza
# [Guía] este módulo.
from dashboard_data import DashboardState, TerminalFormatter, demo_frame, operation_label

# [Guía] Fija cada cuántos milisegundos Tk revisa los eventos y refresca la presentación. El Arduino conserva su
# [Guía] propio ritmo de adquisición.
REFRESH_MS = 200
# [Guía] Establece `ROOT` a partir de `Path(__file__).resolve().parent`.
ROOT = Path(__file__).resolve().parent


# [Guía] Agrupa en ConsoleMirror el estado y las operaciones de esta parte de la aplicación.
class ConsoleMirror:
    """Una consola lenta no debe bloquear stdout del puente ni los ACK USB.

    Solo la copia de terminal tiene cola acotada; el archivo legible se guarda
    antes y conserva todo. No abrir puertos ni enviar comandos en este hilo.
    """
    # [Guía] Crea la cola de salida al terminal y arranca el consumidor que vacía esa cola fuera del hilo de Tk.
    def __init__(self):
        # [Guía] Conserva en `self.lines` el resultado de `queue.Queue` con los argumentos indicados.
        self.lines = queue.Queue(maxsize=4096)
        # [Guía] Establece `self.dropped` a partir de `0`.
        self.dropped = 0
        # [Guía] Conserva en `self.thread` el resultado de `threading.Thread` con los argumentos indicados.
        self.thread = threading.Thread(target=self._run, daemon=True)
        # [Guía] Ejecuta self.thread.start con los argumentos de esta llamada.
        self.thread.start()

    # [Guía] Publica el estado de este módulo. Recibe `line`.
    def publish(self, line):
        # [Guía] Delimita la operación cuyos errores o cierre se gestionan en las ramas siguientes.
        try:
            # [Guía] Ejecuta self.lines.put_nowait con los argumentos de esta llamada.
            self.lines.put_nowait(line)
        # [Guía] Atiende las excepciones que coinciden con `except queue.Full` en el bloque anterior.
        except queue.Full:
            # [Guía] Combina el valor anterior de `self.dropped` con `1` mediante esta operación.
            self.dropped += 1  # El archivo no pierde esta linea.

    # [Guía] Retira mensajes de la cola y los escribe en la consola del computador sin bloquear las
    # [Guía] actualizaciones de la interfaz.
    def _run(self):
        # [Guía] Establece `reported` a partir de `0`.
        reported = 0
        # [Guía] Repite el bloque mientras se cumpla `True`.
        while True:
            # [Guía] Conserva en `line` el resultado de `self.lines.get` con los argumentos indicados.
            line = self.lines.get()
            # [Guía] Evalúa `line is None` antes de ejecutar las instrucciones de esta rama.
            if line is None:
                # [Guía] Termina la función en este punto.
                return
            # [Guía] Delimita la operación cuyos errores o cierre se gestionan en las ramas siguientes.
            try:
                # [Guía] Evalúa `self.dropped > reported` antes de ejecutar las instrucciones de esta rama.
                if self.dropped > reported:
                    # [Guía] Ejecuta print con los argumentos de esta llamada.
                    print(f'AVISO: consola omitio {self.dropped} lineas; consultar terminal.log completo.', flush=True)
                    # [Guía] Establece `reported` a partir de `self.dropped`.
                    reported = self.dropped
                # [Guía] Ejecuta print con los argumentos de esta llamada.
                print(line, flush=True)
            # [Guía] Atiende las excepciones que coinciden con `except (OSError, UnicodeError, ValueError)` en el
            # [Guía] bloque anterior.
            except (OSError, UnicodeError, ValueError):
                # [Guía] Continúa la instrucción con `pass`.
                pass

    # [Guía] Cierra el estado de este módulo.
    def close(self):
        # [Guía] Delimita la operación cuyos errores o cierre se gestionan en las ramas siguientes.
        try:
            # [Guía] Ejecuta self.lines.put_nowait con los argumentos de esta llamada.
            self.lines.put_nowait(None)
        # [Guía] Atiende las excepciones que coinciden con `except queue.Full` en el bloque anterior.
        except queue.Full:
            # [Guía] Delimita la operación cuyos errores o cierre se gestionan en las ramas siguientes.
            try:
                # [Guía] Ejecuta self.lines.get_nowait con los argumentos de esta llamada.
                self.lines.get_nowait()
                # [Guía] Combina el valor anterior de `self.dropped` con `1` mediante esta operación.
                self.dropped += 1
            # [Guía] Atiende las excepciones que coinciden con `except queue.Empty` en el bloque anterior.
            except queue.Empty:
                # [Guía] Continúa la instrucción con `pass`.
                pass
            # [Guía] Ejecuta self.lines.put_nowait con los argumentos de esta llamada.
            self.lines.put_nowait(None)
        # Nunca esperar indefinidamente un print bloqueado por VS Code.
        # [Guía] Ejecuta self.thread.join con los argumentos de esta llamada.
        self.thread.join(timeout=0.05)


# [Guía] Agrupa en BridgeSession el estado y las operaciones de esta parte de la aplicación.
class BridgeSession:
    """Un proceso de puente por sesion; jamas abrir una segunda copia del COM."""
    # [Guía] Prepara el estado del subproceso puente, su cola de eventos y los recursos de registro de la sesión.
    def __init__(self):
        # El proceso posee los puertos; este objeto solo maneja stdin/stdout.
        # El lock impide mezclar una orden del terminal con otra de la ventana.
        # [Guía] Establece `self.process` a partir de `None`.
        self.process = None
        # [Guía] Conserva en `self.events` el resultado de `queue.Queue` con los argumentos indicados.
        self.events = queue.Queue(maxsize=4096)
        # [Guía] Conserva en `self.writer_lock` el resultado de `threading.Lock` con los argumentos indicados.
        self.writer_lock = threading.Lock()
        # [Guía] Establece `self.thread` a partir de `None`.
        self.thread = None
        # [Guía] Establece `self.folder` a partir de `None`.
        self.folder = None
        # [Guía] Establece `self.ui_dropped` a partir de `0`.
        self.ui_dropped = 0
        # [Guía] Conserva en `self.history` el resultado de `SessionHistory` con los argumentos indicados.
        self.history = SessionHistory()

    # [Guía] poll() no bloquea Tk ni espera una transaccion de la carga.
    def alive(self):
        """poll() no bloquea Tk ni espera una transaccion de la carga."""
        # [Guía] Entrega self.process is not None and self.process.poll() is None al llamador y termina la
        # [Guía] función.
        return self.process is not None and self.process.poll() is None

    # [Guía] Acotar solo la cola visual. Los archivos se escriben ANTES de esta cola. Recibe `event`.
    def publish(self, event):
        """Acotar solo la cola visual. Los archivos se escriben ANTES de esta cola."""
        # [Guía] Delimita la operación cuyos errores o cierre se gestionan en las ramas siguientes.
        try:
            # [Guía] Ejecuta self.events.put_nowait con los argumentos de esta llamada.
            self.events.put_nowait(event)
        # [Guía] Atiende las excepciones que coinciden con `except queue.Full` en el bloque anterior.
        except queue.Full:
            # [Guía] Delimita la operación cuyos errores o cierre se gestionan en las ramas siguientes.
            try:
                # [Guía] Ejecuta self.events.get_nowait con los argumentos de esta llamada.
                self.events.get_nowait()
            # [Guía] Atiende las excepciones que coinciden con `except queue.Empty` en el bloque anterior.
            except queue.Empty:
                # [Guía] Continúa la instrucción con `pass`.
                pass
            # [Guía] Combina el valor anterior de `self.ui_dropped` con `1` mediante esta operación.
            self.ui_dropped += 1
            # [Guía] Ejecuta self.events.put_nowait con los argumentos de esta llamada.
            self.events.put_nowait(event)

    # [Guía] Crear registros exclusivos y ejecutar el MISMO puente, sin shell. Recibe `arduino`, `load`, `baud`,
    # [Guía] `address`, `log_root`.
    def start(self, arduino, load, baud, address, log_root):
        """Crear registros exclusivos y ejecutar el MISMO puente, sin shell.

        --raw afecta su impresion, no los comandos enviados al equipo. --log
        conserva las lineas Arduino con UTC exactamente como el monitor anterior.
        Una carpeta nueva evita mezclar pruebas o sobrescribir registros previos.
        """
        # [Guía] Evalúa `self.alive() or (self.thread is not None and self.thread.is_alive())` antes de ejecutar
        # [Guía] las instrucciones de esta rama.
        if self.alive() or (self.thread is not None and self.thread.is_alive()):
            # [Guía] Señala RuntimeError('La sesion anterior todavia esta cerrando.') para que la capa que llamó
            # [Guía] pueda tratar el problema.
            raise RuntimeError('La sesion anterior todavia esta cerrando.')
        # [Guía] Evalúa `not arduino.strip() or not load.strip() or arduino.strip().lower() ==
        # [Guía] load.strip().lower()` antes de ejecutar las instrucciones de esta rama.
        if not arduino.strip() or not load.strip() or arduino.strip().lower() == load.strip().lower():
            # [Guía] Señala ValueError('Selecciona dos puertos COM distintos.') para que la capa que llamó pueda
            # [Guía] tratar el problema.
            raise ValueError('Selecciona dos puertos COM distintos.')
        # [Guía] Evalúa `not 0 <= address <= 254` antes de ejecutar las instrucciones de esta rama.
        if not 0 <= address <= 254:
            # [Guía] Señala ValueError('Direccion BK fuera de 0..254.') para que la capa que llamó pueda tratar el
            # [Guía] problema.
            raise ValueError('Direccion BK fuera de 0..254.')
        # [Guía] Conserva en `self.events` el resultado de `queue.Queue` con los argumentos indicados.
        self.events = queue.Queue(maxsize=4096)
        # [Guía] Establece `self.ui_dropped` a partir de `0`.
        self.ui_dropped = 0
        # [Guía] Conserva en `stamp` el resultado de `datetime.now().strftime` con los argumentos indicados.
        stamp = datetime.now().strftime('%Y%m%d_%H%M%S_%f')
        # [Guía] Establece `self.folder` a partir de `Path(log_root).expanduser().resolve() / f'prueba_{stamp}'`.
        self.folder = Path(log_root).expanduser().resolve() / f'prueba_{stamp}'
        # [Guía] Ejecuta self.folder.mkdir con los argumentos de esta llamada.
        self.folder.mkdir(parents=True, exist_ok=False)
        # [Guía] Establece `command` a partir de `[sys.executable, '-u', '-X', 'utf8', str(ROOT /
        # [Guía] 'control_bk8502.py'), '--arduino', arduino.strip(), '--carga',…`.
        command = [sys.executable, '-u', '-X', 'utf8', str(ROOT / 'control_bk8502.py'),
                   # [Guía] Continúa la instrucción con `'--arduino', arduino.strip(), '--carga', load.strip()`.
                   '--arduino', arduino.strip(), '--carga', load.strip(),
                   # [Guía] Continúa la instrucción con `'--baud-carga', str(baud), '--address', str(address)`.
                   '--baud-carga', str(baud), '--address', str(address),
                   # [Guía] Continúa la instrucción con `'--raw', '--log', str(self.folder / 'arduino_raw.log')]`.
                   '--raw', '--log', str(self.folder / 'arduino_raw.log')]
        # [Guía] Establece `metadata` a partir de `{'created_utc': datetime.now(timezone.utc).isoformat(),
        # [Guía] 'arduino': arduino, 'carga': load, 'baud_carga': baud,…`.
        metadata = {'created_utc': datetime.now(timezone.utc).isoformat(),
                    # [Guía] Añade la entrada 'arduino' a este mapa de datos.
                    'arduino': arduino, 'carga': load, 'baud_carga': baud,
                    # [Guía] Añade la entrada 'address' a este mapa de datos.
                    'address': address, 'refresh_ms': REFRESH_MS, 'firmware': 'RECIBIDO_EN_EMS',
                    # [Guía] Añade la entrada 'history_csv' a este mapa de datos.
                    'history_csv': 'series.csv', 'history_sample_seconds': 0.2,
                    # [Guía] Añade la entrada 'display_filter_tau_s' a este mapa de datos.
                    'display_filter_tau_s': DISPLAY_FILTER_TAU_S,
                    # [Guía] Añade la entrada 'history_csv_filter' a este mapa de datos.
                    'history_csv_filter': 'none; received telemetry averages, before display smoothing',
                    # [Guía] Añade la entrada 'history_time_base' a este mapa de datos.
                    'history_time_base': 'PC monotonic seconds since connect'}
        # [Guía] Ejecuta (self.folder / 'sesion.json').write_text con los argumentos de esta llamada.
        (self.folder / 'sesion.json').write_text(json.dumps(metadata, indent=2), encoding='utf-8')
        # Comprobar que el registro legible se puede abrir ANTES de conectar.
        # [Guía] Conserva en `terminal` el resultado de `(self.folder / 'terminal.log').open` con los argumentos
        # [Guía] indicados.
        terminal = (self.folder / 'terminal.log').open('x', encoding='utf-8', buffering=1)
        # [Guía] Delimita la operación cuyos errores o cierre se gestionan en las ramas siguientes.
        try:
            # [Guía] Ejecuta self.history.start con los argumentos de esta llamada.
            self.history.start(self.folder / 'series.csv', time.monotonic())
            # [Guía] Inicia el puente como subproceso. Los canales redirigidos permiten enviarle comandos y
            # [Guía] recibir su salida sin ejecutar operaciones de la B&K dentro del hilo de Tk.
            self.process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                            # [Guía] Entrega el argumento con nombre `stderr` para configurar esta
                                            # [Guía] llamada.
                                            stderr=subprocess.STDOUT, text=True, encoding='utf-8',
                                            # [Guía] Entrega el argumento con nombre `errors` para configurar esta
                                            # [Guía] llamada.
                                            errors='replace', bufsize=1, cwd=ROOT,
                                            # [Guía] Entrega el argumento con nombre `creationflags` para configurar
                                            # [Guía] esta llamada.
                                            creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        # [Guía] Atiende las excepciones que coinciden con `except Exception` en el bloque anterior.
        except Exception:
            # [Guía] Ejecuta terminal.close con los argumentos de esta llamada.
            terminal.close()
            # [Guía] Ejecuta self.history.close con los argumentos de esta llamada.
            self.history.close()
            # [Guía] Propaga la excepción que se estaba gestionando.
            raise
        # [Guía] Conserva en `self.thread` el resultado de `threading.Thread` con los argumentos indicados.
        self.thread = threading.Thread(target=self._read, args=(terminal,), daemon=True)
        # [Guía] Ejecuta self.thread.start con los argumentos de esta llamada.
        self.thread.start()

    # [Guía] Lee la salida del subproceso puente, registra la sesión, construye eventos y entrega al hilo
    # [Guía] principal los datos de pantalla. Recibe `terminal`.
    def _read(self, terminal):
        """Consumir stdout sin tocar Tk; duplicar todas las salidas legibles."""
        # [Guía] Conserva en `formatter` el resultado de `TerminalFormatter` con los argumentos indicados.
        formatter = TerminalFormatter()
        # [Guía] Establece `console` a partir de `None`.
        console = None
        # [Guía] Establece `log_failed` a partir de `False`.
        log_failed = False
        # [Guía] Delimita la operación cuyos errores o cierre se gestionan en las ramas siguientes.
        try:
            # [Guía] Conserva en `console` el resultado de `ConsoleMirror` con los argumentos indicados.
            console = ConsoleMirror()
            # [Guía] Recorre self.process.stdout. En cada iteración, raw recibe el elemento correspondiente.
            for raw in self.process.stdout:
                # [Guía] Conserva en `received_at` el resultado de `time.monotonic` con los argumentos indicados.
                received_at = time.monotonic()
                # [Guía] Conserva en `line` el resultado de `raw.rstrip` con los argumentos indicados.
                line = raw.rstrip('\r\n')
                # [Guía] Conserva en `shown` el resultado de `formatter.format` con los argumentos indicados.
                shown = formatter.format(line)
                # [Guía] Evalúa `shown` antes de ejecutar las instrucciones de esta rama.
                if shown:
                    # El archivo no depende de que el usuario mantenga abierto stdout.
                    # [Guía] Evalúa `not log_failed` antes de ejecutar las instrucciones de esta rama.
                    if not log_failed:
                        # [Guía] Delimita la operación cuyos errores o cierre se gestionan en las ramas
                        # [Guía] siguientes.
                        try:
                            # [Guía] Ejecuta terminal.write con los argumentos de esta llamada.
                            terminal.write(f'{datetime.now(timezone.utc).isoformat()} {shown}\n')
                        # [Guía] Atiende las excepciones que coinciden con `except OSError as error` en el bloque
                        # [Guía] anterior.
                        except OSError as error:
                            # [Guía] Establece `log_failed` a partir de `True`.
                            log_failed = True
                            # [Guía] Ejecuta self.publish con los argumentos de esta llamada.
                            self.publish(('error', f'Error al guardar el registro: {error}', None))
                            # Seguir drenando stdout evita bloquear el cierre del hijo.
                            # [Guía] Delimita la operación cuyos errores o cierre se gestionan en las ramas
                            # [Guía] siguientes.
                            try:
                                # [Guía] Ejecuta self.send con los argumentos de esta llamada.
                                self.send('SALIR')
                            # [Guía] Atiende las excepciones que coinciden con `except (OSError, RuntimeError)` en
                            # [Guía] el bloque anterior.
                            except (OSError, RuntimeError):
                                # Aunque el hijo ya este cerrando, seguir leyendo
                                # hasta EOF para no llenar su tuberia de salida.
                                # [Guía] Continúa la instrucción con `pass`.
                                pass
                # El CSV se escribe antes de la cola de Tk. Si la vista omite
                # eventos por carga, el archivo sigue conservando las muestras.
                # [Guía] Delimita la operación cuyos errores o cierre se gestionan en las ramas siguientes.
                try:
                    # [Guía] Conserva en `point` el resultado de `self.history.consume` con los argumentos
                    # [Guía] indicados.
                    point = self.history.consume(line, received_at)
                    # [Guía] Evalúa `point is not None` antes de ejecutar las instrucciones de esta rama.
                    if point is not None:
                        # [Guía] Ejecuta self.publish con los argumentos de esta llamada.
                        self.publish(('sample', point, None))
                # [Guía] Atiende las excepciones que coinciden con `except OSError as error` en el bloque
                # [Guía] anterior.
                except OSError as error:
                    # [Guía] Ejecuta self.history.stop con los argumentos de esta llamada.
                    self.history.stop(received_at)
                    # [Guía] Ejecuta self.publish con los argumentos de esta llamada.
                    self.publish(('error', f'Error al guardar series.csv: {error}', None))
                    # [Guía] Delimita la operación cuyos errores o cierre se gestionan en las ramas siguientes.
                    try:
                        # [Guía] Ejecuta self.send con los argumentos de esta llamada.
                        self.send('SALIR')
                    # [Guía] Atiende las excepciones que coinciden con `except (OSError, RuntimeError)` en el
                    # [Guía] bloque anterior.
                    except (OSError, RuntimeError):
                        # [Guía] Continúa la instrucción con `pass`.
                        pass
                # [Guía] Ejecuta self.publish con los argumentos de esta llamada.
                self.publish(('line', (line, received_at), shown))
                # [Guía] Evalúa `shown` antes de ejecutar las instrucciones de esta rama.
                if shown:
                    # [Guía] Ejecuta console.publish con los argumentos de esta llamada.
                    console.publish(shown)
        # [Guía] Atiende las excepciones que coinciden con `except Exception as error` en el bloque anterior.
        except Exception as error:
            # [Guía] Ejecuta self.publish con los argumentos de esta llamada.
            self.publish(('error', f'Error de lectura/registro: {error}', None))
            # Solicitar el cierre normal, no matar un hilo que esta hablando con BK.
            # [Guía] Delimita la operación cuyos errores o cierre se gestionan en las ramas siguientes.
            try:
                # [Guía] Ejecuta self.send con los argumentos de esta llamada.
                self.send('SALIR')
            # [Guía] Atiende las excepciones que coinciden con `except (OSError, RuntimeError)` en el bloque
            # [Guía] anterior.
            except (OSError, RuntimeError):
                # [Guía] Continúa la instrucción con `pass`.
                pass
        # [Guía] Ejecuta este cierre tanto si la operación termina normalmente como si produce una excepción.
        finally:
            # [Guía] Delimita la operación cuyos errores o cierre se gestionan en las ramas siguientes.
            try:
                # [Guía] Ejecuta self.history.close con los argumentos de esta llamada.
                self.history.close()
            # [Guía] Atiende las excepciones que coinciden con `except OSError as error` en el bloque anterior.
            except OSError as error:
                # [Guía] Ejecuta self.publish con los argumentos de esta llamada.
                self.publish(('error', f'No se pudo cerrar series.csv: {error}', None))
            # [Guía] Delimita la operación cuyos errores o cierre se gestionan en las ramas siguientes.
            try:
                # [Guía] Ejecuta terminal.close con los argumentos de esta llamada.
                terminal.close()
            # [Guía] Atiende las excepciones que coinciden con `except OSError as error` en el bloque anterior.
            except OSError as error:
                # [Guía] Ejecuta self.publish con los argumentos de esta llamada.
                self.publish(('error', f'No se pudo cerrar el registro: {error}', None))
            # [Guía] Ejecuta self.process.stdout.close con los argumentos de esta llamada.
            self.process.stdout.close()
            # [Guía] Conserva en `code` el resultado de `self.process.wait` con los argumentos indicados.
            code = self.process.wait()
            # [Guía] Evalúa `self.process.stdin` antes de ejecutar las instrucciones de esta rama.
            if self.process.stdin:
                # [Guía] Ejecuta self.process.stdin.close con los argumentos de esta llamada.
                self.process.stdin.close()
            # [Guía] Ejecuta self.publish con los argumentos de esta llamada.
            self.publish(('exit', code, None))
            # [Guía] Evalúa `console is not None` antes de ejecutar las instrucciones de esta rama.
            if console is not None:
                # [Guía] Ejecuta console.close con los argumentos de esta llamada.
                console.close()

    # [Guía] Validar igual que la consola; solo una accion humana llama este metodo. Recibe `text`.
    def send(self, text):
        """Validar igual que la consola; solo una accion humana llama este metodo."""
        # [Guía] Conserva en `command` el resultado de `normalize_input` con los argumentos indicados.
        command = normalize_input(text)
        # [Guía] Evalúa `not self.alive()` antes de ejecutar las instrucciones de esta rama.
        if not self.alive():
            # [Guía] Señala RuntimeError('No hay una conexion activa.') para que la capa que llamó pueda tratar el
            # [Guía] problema.
            raise RuntimeError('No hay una conexion activa.')
        # [Guía] Abre el contexto de self.writer_lock y deja a su gestor ejecutar el cierre al salir del bloque.
        with self.writer_lock:
            # [Guía] Ejecuta self.process.stdin.write con los argumentos de esta llamada.
            self.process.stdin.write(command + '\n')
            # [Guía] Ejecuta self.process.stdin.flush con los argumentos de esta llamada.
            self.process.stdin.flush()


# [Guía] Agrupa en Dashboard el estado y las operaciones de esta parte de la aplicación.
class Dashboard:
    """Ventana de supervision inspirada en el boceto del usuario."""
    # [Guía] Inicializa la ventana, su estado de telemetría, el historial y el filtro de presentación. Construye
    # [Guía] la interfaz y programa el refresco. Recibe `root`, `demo`, `log_root`.
    def __init__(self, root, demo=False, log_root=None):
        # [Guía] Establece `self.root` a partir de `root`.
        self.root = root
        # [Guía] Establece `self.demo` a partir de `demo`.
        self.demo = demo
        # [Guía] Conserva en `self.log_root` el resultado de `str` con los argumentos indicados.
        self.log_root = str(log_root or ROOT / 'registros')
        # [Guía] Conserva en `self.state` el resultado de `DashboardState` con los argumentos indicados.
        self.state = DashboardState()
        # [Guía] Conserva en `self.session` el resultado de `BridgeSession` con los argumentos indicados.
        self.session = BridgeSession()
        # [Guía] Establece `self.closing` a partir de `False`.
        self.closing = False
        # [Guía] Establece `self.stopping` a partir de `False`.
        self.stopping = False
        # [Guía] Establece `self.stop_since` a partir de `None`.
        self.stop_since = None
        # [Guía] Establece `self.demo_tick` a partir de `0`.
        self.demo_tick = 0
        # [Guía] Conserva en `self.recent` el resultado de `deque` con los argumentos indicados.
        self.recent = deque(maxlen=3000)
        # [Guía] Conserva en `self.history` el resultado de `HistoryBuffer` con los argumentos indicados.
        self.history = HistoryBuffer()
        # [Guía] Conserva en `self.display_filter` el resultado de `DisplayFilter` con los argumentos indicados.
        self.display_filter = DisplayFilter()
        # [Guía] Establece `self.plots` a partir de `None`.
        self.plots = None
        # [Guía] Conserva en `self.demo_sampler` el resultado de `HistorySampler` con los argumentos indicados.
        self.demo_sampler = HistorySampler()
        # [Guía] Evalúa `demo` antes de ejecutar las instrucciones de esta rama.
        if demo:
            # [Guía] Ejecuta self.history.start con los argumentos de esta llamada.
            self.history.start(time.monotonic())
            # [Guía] Ejecuta self.demo_sampler.start con los argumentos de esta llamada.
            self.demo_sampler.start(self.history.started_at)
        # [Guía] Ejecuta root.title con los argumentos de esta llamada.
        root.title('Multipuerto | Dashboard EMS' + (' | DEMOSTRACION' if demo else ''))
        # [Guía] Ejecuta root.geometry con los argumentos de esta llamada.
        root.geometry('1180x800')
        # [Guía] Ejecuta root.minsize con los argumentos de esta llamada.
        root.minsize(1040, 780)
        # [Guía] Ejecuta root.configure con los argumentos de esta llamada.
        root.configure(bg='#f4f6f8')
        # [Guía] Ejecuta root.protocol con los argumentos de esta llamada.
        root.protocol('WM_DELETE_WINDOW', self.close)
        # [Guía] Ejecuta self._style con los argumentos de esta llamada.
        self._style()
        # [Guía] Ejecuta self._build con los argumentos de esta llamada.
        self._build()
        # [Guía] Ejecuta self.scan_ports con los argumentos de esta llamada.
        self.scan_ports()
        # Este lector permite seguir escribiendo en el terminal original.
        # [Guía] Ejecuta threading.Thread(target=self._console, daemon=True).start con los argumentos de esta
        # [Guía] llamada.
        threading.Thread(target=self._console, daemon=True).start()
        # [Guía] Ejecuta root.after con los argumentos de esta llamada.
        root.after(REFRESH_MS, self.refresh)

    # [Guía] Estilos locales: no alterar temas ni preferencias de Windows.
    def _style(self):
        """Estilos locales: no alterar temas ni preferencias de Windows."""
        # [Guía] Conserva en `style` el resultado de `ttk.Style` con los argumentos indicados.
        style = ttk.Style()
        # [Guía] Ejecuta style.theme_use con los argumentos de esta llamada.
        style.theme_use('clam')
        # [Guía] Ejecuta style.configure con los argumentos de esta llamada.
        style.configure('.', font=('Segoe UI', 10), background='#f4f6f8', foreground='#19232c')
        # [Guía] Ejecuta style.configure con los argumentos de esta llamada.
        style.configure('TButton', padding=(10, 5))
        # [Guía] Ejecuta style.configure con los argumentos de esta llamada.
        style.configure('TCombobox', padding=4)
        # [Guía] Ejecuta style.configure con los argumentos de esta llamada.
        style.configure('Value.TLabel', font=('Segoe UI', 23), background='white')
        # [Guía] Ejecuta style.configure con los argumentos de esta llamada.
        style.configure('White.TFrame', background='white')
        # [Guía] Ejecuta style.configure con los argumentos de esta llamada.
        style.configure('White.TLabel', background='white')
        # [Guía] Ejecuta style.configure con los argumentos de esta llamada.
        style.configure('Small.TLabel', foreground='#5b6872', font=('Segoe UI', 9))
        # [Guía] Ejecuta style.configure con los argumentos de esta llamada.
        style.configure('Mode.TLabel', font=('Segoe UI', 21, 'bold'), background='#17292d', foreground='white')
        # [Guía] Ejecuta style.configure con los argumentos de esta llamada.
        style.configure('Soc.Horizontal.TProgressbar', troughcolor='#e6edf0', background='#207a64', thickness=10)

    # [Guía] Distribuye los selectores, tarjetas de datos, controles y consola dentro de la ventana.
    def _build(self):
        """Crear controles una sola vez; actualizar textos no cambia la geometria."""
        # grid distribuye las bandas verticales; solo el registro crece en alto.
        # pack se usa dentro de bandas distintas, nunca mezclado con grid en
        # el mismo padre. Cada etiqueta se conserva para actualizarla en refresh.
        # [Guía] Conserva en `outer` el resultado de `ttk.Frame` con los argumentos indicados.
        outer = ttk.Frame(self.root, padding=16)
        # [Guía] Ejecuta outer.pack con los argumentos de esta llamada.
        outer.pack(fill='both', expand=True)
        # [Guía] Ejecuta outer.columnconfigure con los argumentos de esta llamada.
        outer.columnconfigure(0, weight=1)
        # [Guía] Ejecuta outer.rowconfigure con los argumentos de esta llamada.
        outer.rowconfigure(5, weight=1)
        # [Guía] Conserva en `header` el resultado de `ttk.Frame` con los argumentos indicados.
        header = ttk.Frame(outer)
        # [Guía] Ejecuta header.grid con los argumentos de esta llamada.
        header.grid(row=0, column=0, sticky='ew', pady=(0, 8))
        # [Guía] Ejecuta ttk.Label(header, text='Multipuerto', font=('Segoe UI', 20, 'bold')).pack con los
        # [Guía] argumentos de esta llamada.
        ttk.Label(header, text='Multipuerto', font=('Segoe UI', 20, 'bold')).pack(side='left')
        # [Guía] Conserva en `self.connection` el resultado de `ttk.Label` con los argumentos indicados.
        self.connection = ttk.Label(header, text='DEMOSTRACION · sin hardware' if self.demo else 'DESCONECTADO')
        # [Guía] Ejecuta self.connection.pack con los argumentos de esta llamada.
        self.connection.pack(side='right')
        # [Guía] Conserva en `ports` el resultado de `ttk.Frame` con los argumentos indicados.
        ports = ttk.Frame(outer)
        # [Guía] Ejecuta ports.grid con los argumentos de esta llamada.
        ports.grid(row=1, column=0, sticky='ew', pady=(0, 10))
        # [Guía] Conserva en `self.arduino` el resultado de `tk.StringVar` con los argumentos indicados.
        self.arduino = tk.StringVar(value='COM5')
        # [Guía] Conserva en `self.load` el resultado de `tk.StringVar` con los argumentos indicados.
        self.load = tk.StringVar(value='COM3')
        # [Guía] Ejecuta ttk.Label(ports, text='Arduino').pack con los argumentos de esta llamada.
        ttk.Label(ports, text='Arduino').pack(side='left', padx=(0, 6))
        # [Guía] Conserva en `self.arduino_box` el resultado de `ttk.Combobox` con los argumentos indicados.
        self.arduino_box = ttk.Combobox(ports, textvariable=self.arduino, width=9)
        # [Guía] Ejecuta self.arduino_box.pack con los argumentos de esta llamada.
        self.arduino_box.pack(side='left')
        # [Guía] Ejecuta ttk.Label(ports, text='B&K').pack con los argumentos de esta llamada.
        ttk.Label(ports, text='B&K').pack(side='left', padx=(16, 6))
        # [Guía] Conserva en `self.load_box` el resultado de `ttk.Combobox` con los argumentos indicados.
        self.load_box = ttk.Combobox(ports, textvariable=self.load, width=9)
        # [Guía] Ejecuta self.load_box.pack con los argumentos de esta llamada.
        self.load_box.pack(side='left')
        # [Guía] Conserva en `self.baud` el resultado de `tk.StringVar` con los argumentos indicados.
        self.baud = tk.StringVar(value='9600')
        # [Guía] Ejecuta ttk.Combobox(ports, textvariable=self.baud, values=('4800', '9600', '19200', '38400'),
        # [Guía] width=7, state='readonly… con los argumentos de esta llamada.
        ttk.Combobox(ports, textvariable=self.baud, values=('4800', '9600', '19200', '38400'), width=7, state='readonly').pack(side='left', padx=8)
        # [Guía] Ejecuta ttk.Label(ports, text='Dir.').pack con los argumentos de esta llamada.
        ttk.Label(ports, text='Dir.').pack(side='left')
        # [Guía] Conserva en `self.address` el resultado de `tk.StringVar` con los argumentos indicados.
        self.address = tk.StringVar(value='0')
        # [Guía] Ejecuta ttk.Spinbox(ports, from_=0, to=254, textvariable=self.address, width=4).pack con los
        # [Guía] argumentos de esta llamada.
        ttk.Spinbox(ports, from_=0, to=254, textvariable=self.address, width=4).pack(side='left', padx=6)
        # [Guía] Ejecuta ttk.Button(ports, text='Actualizar puertos', command=self.scan_ports).pack con los
        # [Guía] argumentos de esta llamada.
        ttk.Button(ports, text='Actualizar puertos', command=self.scan_ports).pack(side='left', padx=6)
        # [Guía] Conserva en `self.connect_button` el resultado de `ttk.Button` con los argumentos indicados.
        self.connect_button = ttk.Button(ports, text='Conectar', command=self.connect)
        # [Guía] Ejecuta self.connect_button.pack con los argumentos de esta llamada.
        self.connect_button.pack(side='right')
        # [Guía] Conserva en `mode` el resultado de `tk.Frame` con los argumentos indicados.
        mode = tk.Frame(outer, background='#17292d', padx=20, pady=8)
        # [Guía] Ejecuta mode.grid con los argumentos de esta llamada.
        mode.grid(row=2, column=0, sticky='ew')
        # [Guía] Ejecuta mode.columnconfigure con los argumentos de esta llamada.
        mode.columnconfigure(0, weight=1)
        # [Guía] Ejecuta tk.Label(mode, text='MODO DE OPERACION', bg='#17292d', fg='#afc8cb', font=('Segoe UI',
        # [Guía] 9)).grid con los argumentos de esta llamada.
        tk.Label(mode, text='MODO DE OPERACION', bg='#17292d', fg='#afc8cb', font=('Segoe UI', 9)).grid(row=0, column=0)
        # [Guía] Conserva en `self.mode` el resultado de `ttk.Label` con los argumentos indicados.
        self.mode = ttk.Label(mode, text='--', style='Mode.TLabel', wraplength=940, anchor='center', justify='center')
        # [Guía] Ejecuta self.mode.grid con los argumentos de esta llamada.
        self.mode.grid(row=1, column=0)
        # [Guía] Conserva en `self.reason` el resultado de `tk.Label` con los argumentos indicados.
        self.reason = tk.Label(mode, text='SIN_DATOS', bg='#17292d', fg='#afc8cb', font=('Segoe UI', 9), wraplength=780)
        # [Guía] Ejecuta self.reason.grid con los argumentos de esta llamada.
        self.reason.grid(row=2, column=0, pady=(2, 0))
        # [Guía] Conserva en `data` el resultado de `ttk.Frame` con los argumentos indicados.
        data = ttk.Frame(outer, style='White.TFrame', padding=(14, 10))
        # [Guía] Ejecuta data.grid con los argumentos de esta llamada.
        data.grid(row=3, column=0, sticky='ew', pady=(14, 0))
        # [Guía] Ejecuta data.columnconfigure con los argumentos de esta llamada.
        data.columnconfigure(0, weight=4, minsize=500)
        # [Guía] Ejecuta data.columnconfigure con los argumentos de esta llamada.
        data.columnconfigure(1, weight=2)
        # [Guía] Ejecuta data.columnconfigure con los argumentos de esta llamada.
        data.columnconfigure(2, weight=4)
        # [Guía] Conserva en `table` el resultado de `ttk.Frame` con los argumentos indicados.
        table = ttk.Frame(data, style='White.TFrame')
        # [Guía] Ejecuta table.grid con los argumentos de esta llamada.
        table.grid(row=0, column=0, sticky='nsew')
        # [Guía] Recorre enumerate(('PUERTO', 'TENSIÓN (V)', 'CORRIENTE (A)', 'POTENCIA (W)')). En cada iteración,
        # [Guía] (col, title) recibe el elemento correspondiente.
        for col, title in enumerate(('PUERTO', 'TENSIÓN (V)', 'CORRIENTE (A)', 'POTENCIA (W)')):
            # [Guía] Ejecuta ttk.Label(table, text=title, style='White.TLabel').grid con los argumentos de esta
            # [Guía] llamada.
            ttk.Label(table, text=title, style='White.TLabel').grid(row=0, column=col, sticky='w', padx=(0, 14), pady=(0, 8))
        # Clave (puerto, magnitud) -> widget: separa el orden visual BUS/PV/BAT
        # del orden de adquisicion PV/BUS/BAT, sin intercambiar sus valores.
        # [Guía] Establece `self.measures` a partir de `{}`.
        self.measures = {}
        # [Guía] Recorre enumerate((('BUS', 'Bus DC', '#25646e'), ('PV', 'PV', '#8a6200'), ('BAT', 'Bateria',
        # [Guía] '#226b51')), 1). En cada iteración, (row, (key, title, color)) recibe el elemento
        # [Guía] correspondiente.
        for row, (key, title, color) in enumerate((('BUS', 'Bus DC', '#25646e'), ('PV', 'PV', '#8a6200'), ('BAT', 'Bateria', '#226b51')), 1):
            # [Guía] Ejecuta ttk.Label(table, text=title, style='White.TLabel', foreground=color).grid con los
            # [Guía] argumentos de esta llamada.
            ttk.Label(table, text=title, style='White.TLabel', foreground=color).grid(row=row, column=0, sticky='w', padx=(0, 18), pady=4)
            # [Guía] Recorre ((1, 'voltage'), (2, 'current'), (3, 'power')). En cada iteración, (col, kind) recibe
            # [Guía] el elemento correspondiente.
            for col, kind in ((1, 'voltage'), (2, 'current'), (3, 'power')):
                # [Guía] Conserva en `label` el resultado de `ttk.Label` con los argumentos indicados.
                label = ttk.Label(table, text='--', style='Value.TLabel', width=7, anchor='e', font=('Segoe UI', 17))
                # [Guía] Ejecuta label.grid con los argumentos de esta llamada.
                label.grid(row=row, column=col, sticky='e', padx=(0, 12), pady=4)
                # [Guía] Establece `self.measures[key, kind]` a partir de `label`.
                self.measures[(key, kind)] = label
        # [Guía] Conserva en `self.power_note` el resultado de `ttk.Label` con los argumentos indicados.
        self.power_note = ttk.Label(table, text='Potencia: sin datos', style='White.TLabel', font=('Segoe UI', 8), wraplength=460)
        # [Guía] Ejecuta self.power_note.grid con los argumentos de esta llamada.
        self.power_note.grid(row=4, column=0, columnspan=4, sticky='w', pady=(10, 0))
        # [Guía] Conserva en `soc` el resultado de `ttk.Frame` con los argumentos indicados.
        soc = ttk.Frame(data, style='White.TFrame', padding=(16, 8))
        # [Guía] Ejecuta soc.grid con los argumentos de esta llamada.
        soc.grid(row=0, column=1, sticky='nsew')
        # [Guía] Ejecuta ttk.Label(soc, text='SOC', style='White.TLabel').pack con los argumentos de esta llamada.
        ttk.Label(soc, text='SOC', style='White.TLabel').pack(pady=(0, 16))
        # [Guía] Conserva en `self.soc_value` el resultado de `ttk.Label` con los argumentos indicados.
        self.soc_value = ttk.Label(soc, text='--', style='Value.TLabel', width=8, anchor='center')
        # [Guía] Ejecuta self.soc_value.pack con los argumentos de esta llamada.
        self.soc_value.pack()
        # [Guía] Conserva en `self.soc_bar` el resultado de `ttk.Progressbar` con los argumentos indicados.
        self.soc_bar = ttk.Progressbar(soc, style='Soc.Horizontal.TProgressbar', maximum=100, length=115)
        # [Guía] Ejecuta self.soc_bar.pack con los argumentos de esta llamada.
        self.soc_bar.pack(pady=10)
        # [Guía] Conserva en `self.soc_status` el resultado de `ttk.Label` con los argumentos indicados.
        self.soc_status = ttk.Label(soc, text='SIN_DATOS', style='White.TLabel', font=('Segoe UI', 8), wraplength=130, anchor='center')
        # [Guía] Ejecuta self.soc_status.pack con los argumentos de esta llamada.
        self.soc_status.pack()
        # [Guía] Conserva en `duties` el resultado de `ttk.Frame` con los argumentos indicados.
        duties = ttk.Frame(data, style='White.TFrame', padding=(14, 8))
        # [Guía] Ejecuta duties.grid con los argumentos de esta llamada.
        duties.grid(row=0, column=2, sticky='nsew')
        # [Guía] Establece `self.duties` a partir de `[]`.
        self.duties = []
        # [Guía] Recorre enumerate((3, 5, 6)). En cada iteración, (column, pin) recibe el elemento
        # [Guía] correspondiente.
        for column, pin in enumerate((3, 5, 6)):
            # [Guía] Ejecuta duties.columnconfigure con los argumentos de esta llamada.
            duties.columnconfigure(column, weight=1, uniform='duty')
            # [Guía] Conserva en `block` el resultado de `tk.Frame` con los argumentos indicados.
            block = tk.Frame(duties, background='#f1f5f6', highlightbackground='#dce5e8', highlightthickness=1, padx=8, pady=12)
            # [Guía] Ejecuta block.grid con los argumentos de esta llamada.
            block.grid(row=0, column=column, sticky='nsew', padx=4)
            # [Guía] Ejecuta tk.Label(block, text=f'Q{column + 1}', background='#f1f5f6', font=('Segoe UI', 13,
            # [Guía] 'bold')).pack con los argumentos de esta llamada.
            tk.Label(block, text=f'Q{column + 1}', background='#f1f5f6', font=('Segoe UI', 13, 'bold')).pack()
            # [Guía] Ejecuta tk.Label(block, text=f'D{pin}', background='#f1f5f6', foreground='#66747a',
            # [Guía] font=('Segoe UI', 9)).pack con los argumentos de esta llamada.
            tk.Label(block, text=f'D{pin}', background='#f1f5f6', foreground='#66747a', font=('Segoe UI', 9)).pack(pady=(2, 14))
            # [Guía] Conserva en `value` el resultado de `tk.Label` con los argumentos indicados.
            value = tk.Label(block, text='--', background='#f1f5f6', font=('Segoe UI', 19), width=5)
            # [Guía] Ejecuta value.pack con los argumentos de esta llamada.
            value.pack()
            # [Guía] Ejecuta self.duties.append con los argumentos de esta llamada.
            self.duties.append(value)
        # [Guía] Conserva en `footer` el resultado de `ttk.Frame` con los argumentos indicados.
        footer = ttk.Frame(outer, padding=(0, 10))
        # [Guía] Ejecuta footer.grid con los argumentos de esta llamada.
        footer.grid(row=4, column=0, sticky='ew')
        # [Guía] Conserva en `self.emulator` el resultado de `ttk.Label` con los argumentos indicados.
        self.emulator = ttk.Label(footer, text='EMULADOR   -- ohm   |   SIN CONFIRMAR', font=('Segoe UI', 12))
        # [Guía] Ejecuta self.emulator.pack con los argumentos de esta llamada.
        self.emulator.pack(side='left')
        # [Guía] Conserva en `self.mppt` el resultado de `ttk.Label` con los argumentos indicados.
        self.mppt = ttk.Label(footer, text='MPPT   --', font=('Segoe UI', 12, 'bold'))
        # [Guía] Ejecuta self.mppt.pack con los argumentos de esta llamada.
        self.mppt.pack(side='right')
        # [Guía] Conserva en `logframe` el resultado de `ttk.Frame` con los argumentos indicados.
        logframe = ttk.Frame(outer)
        # [Guía] Ejecuta logframe.grid con los argumentos de esta llamada.
        logframe.grid(row=5, column=0, sticky='nsew')
        # [Guía] Ejecuta logframe.rowconfigure con los argumentos de esta llamada.
        logframe.rowconfigure(1, weight=1)
        # [Guía] Ejecuta logframe.columnconfigure con los argumentos de esta llamada.
        logframe.columnconfigure(0, weight=1)
        # [Guía] Conserva en `logbar` el resultado de `ttk.Frame` con los argumentos indicados.
        logbar = ttk.Frame(logframe)
        # [Guía] Ejecuta logbar.grid con los argumentos de esta llamada.
        logbar.grid(row=0, column=0, sticky='ew', pady=(0, 6))
        # [Guía] Ejecuta ttk.Label(logbar, text='REGISTRO DE LA PRUEBA', style='Small.TLabel').pack con los
        # [Guía] argumentos de esta llamada.
        ttk.Label(logbar, text='REGISTRO DE LA PRUEBA', style='Small.TLabel').pack(side='left')
        # [Guía] Conserva en `self.follow` el resultado de `tk.BooleanVar` con los argumentos indicados.
        self.follow = tk.BooleanVar(value=True)
        # [Guía] Ejecuta ttk.Checkbutton(logbar, text='Seguir', variable=self.follow).pack con los argumentos de
        # [Guía] esta llamada.
        ttk.Checkbutton(logbar, text='Seguir', variable=self.follow).pack(side='right')
        # [Guía] Ejecuta ttk.Button(logbar, text='Copiar registro', command=self.copy_log).pack con los argumentos
        # [Guía] de esta llamada.
        ttk.Button(logbar, text='Copiar registro', command=self.copy_log).pack(side='right', padx=6)
        # [Guía] Ejecuta ttk.Button(logbar, text='Carpeta de registros', command=self.open_logs).pack con los
        # [Guía] argumentos de esta llamada.
        ttk.Button(logbar, text='Carpeta de registros', command=self.open_logs).pack(side='right')
        # [Guía] Conserva en `self.terminal` el resultado de `scrolledtext.ScrolledText` con los argumentos
        # [Guía] indicados.
        self.terminal = scrolledtext.ScrolledText(logframe, height=7, wrap='none', state='disabled', font=('Consolas', 9), background='#172126', foreground='#e0ebe9', insertbackground='white', relief='flat', padx=10, pady=10)
        # [Guía] Ejecuta self.terminal.grid con los argumentos de esta llamada.
        self.terminal.grid(row=1, column=0, sticky='nsew')
        # [Guía] Conserva en `commandbar` el resultado de `ttk.Frame` con los argumentos indicados.
        commandbar = ttk.Frame(outer, padding=(0, 6, 0, 0))
        # [Guía] Ejecuta commandbar.grid con los argumentos de esta llamada.
        commandbar.grid(row=6, column=0, sticky='ew')
        # [Guía] Ejecuta ttk.Label(commandbar, text='Comando').pack con los argumentos de esta llamada.
        ttk.Label(commandbar, text='Comando').pack(side='left', padx=(0, 8))
        # [Guía] Conserva en `self.command` el resultado de `ttk.Entry` con los argumentos indicados.
        self.command = ttk.Entry(commandbar)
        # [Guía] Ejecuta self.command.pack con los argumentos de esta llamada.
        self.command.pack(side='left', fill='x', expand=True)
        # [Guía] Ejecuta self.command.bind con los argumentos de esta llamada.
        self.command.bind('<Return>', lambda event: self.send_command())
        # [Guía] Conserva en `self.send_button` el resultado de `ttk.Button` con los argumentos indicados.
        self.send_button = ttk.Button(commandbar, text='Enviar', command=self.send_command, state='disabled')
        # [Guía] Ejecuta self.send_button.pack con los argumentos de esta llamada.
        self.send_button.pack(side='left', padx=(8, 0))
        # [Guía] Ejecuta ttk.Button(commandbar, text='Gráficas V/I/P', command=self.show_plots).pack con los
        # [Guía] argumentos de esta llamada.
        ttk.Button(commandbar, text='Gráficas V/I/P', command=self.show_plots).pack(side='left', padx=(8, 0))
        # [Guía] Conserva en `self.detail` el resultado de `ttk.Label` con los argumentos indicados.
        self.detail = ttk.Label(outer, text='DEMO_NO_HARDWARE' if self.demo else 'Sin sesion', style='Small.TLabel', wraplength=1000)
        # [Guía] Ejecuta self.detail.grid con los argumentos de esta llamada.
        self.detail.grid(row=7, column=0, sticky='w', pady=(3, 0))
        # [Guía] Evalúa `self.demo` antes de ejecutar las instrucciones de esta rama.
        if self.demo:
            # [Guía] Ejecuta self.connect_button.configure con los argumentos de esta llamada.
            self.connect_button.configure(state='disabled')

    # [Guía] Actualiza las listas de puertos serie disponibles para Arduino y B&K.
    def scan_ports(self):
        """Enumerar no abre dispositivos. Pyserial solo es necesario para COM real."""
        # [Guía] Delimita la operación cuyos errores o cierre se gestionan en las ramas siguientes.
        try:
            # [Guía] Importa list_ports para las operaciones que realiza este módulo.
            from serial.tools import list_ports
            # [Guía] Establece `ports` a partir de `[port.device for port in list_ports.comports()]`.
            ports = [port.device for port in list_ports.comports()]
            # [Guía] Ejecuta self.arduino_box.configure con los argumentos de esta llamada.
            self.arduino_box.configure(values=ports)
            # [Guía] Ejecuta self.load_box.configure con los argumentos de esta llamada.
            self.load_box.configure(values=ports)
        # [Guía] Atiende las excepciones que coinciden con `except ImportError` en el bloque anterior.
        except ImportError:
            # [Guía] Evalúa `not self.demo` antes de ejecutar las instrucciones de esta rama.
            if not self.demo:
                # [Guía] Ejecuta self.connection.configure con los argumentos de esta llamada.
                self.connection.configure(text='Falta pyserial')

    # [Guía] Lee la selección de puertos y arranca una sesión con el puente B&K.
    def connect(self):
        """El boton es la autorizacion para abrir COM; no hay autoconexion al iniciar."""
        # [Guía] Evalúa `self.session.alive()` antes de ejecutar las instrucciones de esta rama.
        if self.session.alive():
            # [Guía] Ejecuta self.disconnect con los argumentos de esta llamada.
            self.disconnect()
            # [Guía] Termina la función en este punto.
            return
        # [Guía] Delimita la operación cuyos errores o cierre se gestionan en las ramas siguientes.
        try:
            # [Guía] Ejecuta self.session.start con los argumentos de esta llamada.
            self.session.start(self.arduino.get(), self.load.get(), int(self.baud.get()), int(self.address.get()), self.log_root)
        # [Guía] Atiende las excepciones que coinciden con `except Exception as error` en el bloque anterior.
        except Exception as error:
            # [Guía] Ejecuta messagebox.showerror con los argumentos de esta llamada.
            messagebox.showerror('No se pudo conectar', str(error), parent=self.root)
            # [Guía] Termina la función en este punto.
            return
        # Reiniciar datos al abrir otra prueba: no mostrar confirmaciones viejas
        # de una sesion anterior mientras llega el primer reporte nuevo.
        # [Guía] Conserva en `self.state` el resultado de `DashboardState` con los argumentos indicados.
        self.state = DashboardState()
        # [Guía] Ejecuta self.history.start con los argumentos de esta llamada.
        self.history.start(self.session.history.started_at)
        # [Guía] Ejecuta self.display_filter.reset con los argumentos de esta llamada.
        self.display_filter.reset()
        # [Guía] Ejecuta self.show_plots con los argumentos de esta llamada.
        self.show_plots()
        # [Guía] Establece `self.stopping` a partir de `False`.
        self.stopping = False
        # [Guía] Ejecuta self.connection.configure con los argumentos de esta llamada.
        self.connection.configure(text='CONECTANDO')
        # [Guía] Ejecuta self.connect_button.configure con los argumentos de esta llamada.
        self.connect_button.configure(text='Desconectar')
        # [Guía] Ejecuta self.send_button.configure con los argumentos de esta llamada.
        self.send_button.configure(state='normal')
        # [Guía] Ejecuta self.arduino_box.configure con los argumentos de esta llamada.
        self.arduino_box.configure(state='disabled')
        # [Guía] Ejecuta self.load_box.configure con los argumentos de esta llamada.
        self.load_box.configure(state='disabled')

    # [Guía] Pide el cierre al puente y actualiza el estado de la sesión de la interfaz.
    def disconnect(self):
        """SALIR permite al puente terminar ACK y solicitar OFF/LOCAL antes de cerrar."""
        # [Guía] Evalúa `self.stopping or not self.session.alive()` antes de ejecutar las instrucciones de esta
        # [Guía] rama.
        if self.stopping or not self.session.alive():
            # [Guía] Termina la función en este punto.
            return
        # [Guía] Establece `self.stopping` a partir de `True`.
        self.stopping = True
        # [Guía] Conserva en `self.stop_since` el resultado de `time.monotonic` con los argumentos indicados.
        self.stop_since = time.monotonic()
        # Congelar en el instante de pulsar Desconectar, no despues de esperar
        # el cierre remoto. El registro original conserva la secuencia SALIR.
        # [Guía] Ejecuta self.session.history.stop con los argumentos de esta llamada.
        self.session.history.stop(self.stop_since)
        # [Guía] Ejecuta self.history.stop con los argumentos de esta llamada.
        self.history.stop(self.stop_since)
        # [Guía] Ejecuta self.connect_button.configure con los argumentos de esta llamada.
        self.connect_button.configure(state='disabled')
        # [Guía] Ejecuta self.send_button.configure con los argumentos de esta llamada.
        self.send_button.configure(state='disabled')
        # [Guía] Ejecuta self.connection.configure con los argumentos de esta llamada.
        self.connection.configure(text='CERRANDO PUENTE')
        # [Guía] Delimita la operación cuyos errores o cierre se gestionan en las ramas siguientes.
        try:
            # [Guía] Ejecuta self.session.send con los argumentos de esta llamada.
            self.session.send('SALIR')
        # [Guía] Atiende las excepciones que coinciden con `except (OSError, RuntimeError) as error` en el bloque
        # [Guía] anterior.
        except (OSError, RuntimeError) as error:
            # [Guía] Ejecuta self.append_log con los argumentos de esta llamada.
            self.append_log(str(error))

    # [Guía] Vista independiente: no abrir puertos ni reiniciar una captura activa.
    def show_plots(self):
        """Vista independiente: no abrir puertos ni reiniciar una captura activa."""
        # [Guía] Evalúa `self.plots is None or not self.plots.window.winfo_exists()` antes de ejecutar las
        # [Guía] instrucciones de esta rama.
        if self.plots is None or not self.plots.window.winfo_exists():
            # [Guía] Conserva en `self.plots` el resultado de `PlotWindow` con los argumentos indicados.
            self.plots = PlotWindow(self.root, self.history)
        # [Guía] Ejecuta self.plots.show con los argumentos de esta llamada.
        self.plots.show()
        # [Guía] Ejecuta self.plots.render con los argumentos de esta llamada.
        self.plots.render(time.monotonic())

    # [Guía] Preservar el vocabulario del monitor; una entrada no valida no llega al COM.
    def send_command(self):
        """Preservar el vocabulario del monitor; una entrada no valida no llega al COM."""
        # [Guía] Conserva en `text` el resultado de `self.command.get().strip` con los argumentos indicados.
        text = self.command.get().strip()
        # [Guía] Evalúa `not text` antes de ejecutar las instrucciones de esta rama.
        if not text:
            # [Guía] Termina la función en este punto.
            return
        # [Guía] Delimita la operación cuyos errores o cierre se gestionan en las ramas siguientes.
        try:
            # [Guía] Conserva en `command` el resultado de `normalize_input` con los argumentos indicados.
            command = normalize_input(text)
            # [Guía] Evalúa `command == 'SALIR'` antes de ejecutar las instrucciones de esta rama.
            if command == 'SALIR':
                # [Guía] Ejecuta self.disconnect con los argumentos de esta llamada.
                self.disconnect()
            # [Guía] Ejecuta este bloque cuando se cumple la alternativa del bloque anterior.
            else:
                # [Guía] Ejecuta self.session.send con los argumentos de esta llamada.
                self.session.send(command)
            # [Guía] Ejecuta self.command.delete con los argumentos de esta llamada.
            self.command.delete(0, 'end')
        # [Guía] Atiende las excepciones que coinciden con `except (ValueError, RuntimeError, OSError) as error`
        # [Guía] en el bloque anterior.
        except (ValueError, RuntimeError, OSError) as error:
            # [Guía] Ejecuta messagebox.showerror con los argumentos de esta llamada.
            messagebox.showerror('Comando no enviado', str(error), parent=self.root)

    # [Guía] Mantener disponible la entrada del terminal sin bloquear el bucle grafico.
    def _console(self):
        """Mantener disponible la entrada del terminal sin bloquear el bucle grafico."""
        # [Guía] Evalúa `sys.stdin is None` antes de ejecutar las instrucciones de esta rama.
        if sys.stdin is None:
            # [Guía] Termina la función en este punto.
            return
        # [Guía] Delimita la operación cuyos errores o cierre se gestionan en las ramas siguientes.
        try:
            # [Guía] Recorre sys.stdin. En cada iteración, line recibe el elemento correspondiente.
            for line in sys.stdin:
                # [Guía] Evalúa `self.demo` antes de ejecutar las instrucciones de esta rama.
                if self.demo:
                    # [Guía] Pasa a la siguiente iteración del bucle.
                    continue
                # [Guía] Delimita la operación cuyos errores o cierre se gestionan en las ramas siguientes.
                try:
                    # [Guía] Ejecuta self.session.send con los argumentos de esta llamada.
                    self.session.send(line)
                # [Guía] Atiende las excepciones que coinciden con `except (ValueError, RuntimeError, OSError) as
                # [Guía] error` en el bloque anterior.
                except (ValueError, RuntimeError, OSError) as error:
                    # [Guía] Ejecuta print con los argumentos de esta llamada.
                    print(f'Comando no enviado: {error}', flush=True)
        # [Guía] Atiende las excepciones que coinciden con `except (OSError, ValueError)` en el bloque anterior.
        except (OSError, ValueError):
            # [Guía] Termina la función en este punto.
            return

    # [Guía] Añade log. Recibe `text`.
    def append_log(self, text):
        # [Guía] Ejecuta self.append_logs con los argumentos de esta llamada.
        self.append_logs([text])

    # [Guía] Acotar la vista a3000 lineas; no recortar el archivo de la prueba. Recibe `texts`.
    def append_logs(self, texts):
        """Acotar la vista a3000 lineas; no recortar el archivo de la prueba."""
        # [Guía] Evalúa `not texts` antes de ejecutar las instrucciones de esta rama.
        if not texts:
            # [Guía] Termina la función en este punto.
            return
        # [Guía] Ejecuta self.recent.extend con los argumentos de esta llamada.
        self.recent.extend(texts)
        # [Guía] Ejecuta self.terminal.configure con los argumentos de esta llamada.
        self.terminal.configure(state='normal')
        # Una insercion por refresco, no una actualizacion Tk por cada linea.
        # [Guía] Ejecuta self.terminal.insert con los argumentos de esta llamada.
        self.terminal.insert('end', '\n'.join(texts) + '\n')
        # [Guía] Conserva en `lines` el resultado de `int` con los argumentos indicados.
        lines = int(self.terminal.index('end-1c').split('.')[0])
        # [Guía] Evalúa `lines > 3000` antes de ejecutar las instrucciones de esta rama.
        if lines > 3000:
            # [Guía] Ejecuta self.terminal.delete con los argumentos de esta llamada.
            self.terminal.delete('1.0', f'{lines - 3000 + 1}.0')
        # [Guía] Evalúa `self.follow.get()` antes de ejecutar las instrucciones de esta rama.
        if self.follow.get():
            # [Guía] Ejecuta self.terminal.see con los argumentos de esta llamada.
            self.terminal.see('end')
        # [Guía] Ejecuta self.terminal.configure con los argumentos de esta llamada.
        self.terminal.configure(state='disabled')

    # [Guía] Actualiza Tk desde el hilo de la interfaz. Lee eventos pendientes, actualiza valores y gráficas
    # [Guía] vigentes y agenda el siguiente refresco.
    def refresh(self):
        """Cada200 ms drenar eventos, actualizar la instantanea y repintar textos.

        No se usa sleep en Tk. La comunicacion y el guardado no esperan al
        repintado. Al caducar un grupo se muestran guiones, no el ultimo cero.
        """
        # [Guía] Conserva en `now` el resultado de `time.monotonic` con los argumentos indicados.
        now = time.monotonic()
        # [Guía] Establece `pending_text` a partir de `[]`.
        pending_text = []
        # [Guía] Evalúa `self.demo` antes de ejecutar las instrucciones de esta rama.
        if self.demo:
            # Datos sinteticos: ejercitar tambien el terminal sin abrir COM ni
            # generar archivos que puedan confundirse con una prueba fisica.
            # [Guía] Conserva en `formatter` el resultado de `TerminalFormatter` con los argumentos indicados.
            formatter = TerminalFormatter()
            # [Guía] Recorre demo_frame(self.demo_tick). En cada iteración, line recibe el elemento
            # [Guía] correspondiente.
            for line in demo_frame(self.demo_tick):
                # [Guía] Ejecuta self.state.consume con los argumentos de esta llamada.
                self.state.consume(line, now)
                # [Guía] Conserva en `shown` el resultado de `formatter.format` con los argumentos indicados.
                shown = formatter.format(line)
                # [Guía] Evalúa `shown` antes de ejecutar las instrucciones de esta rama.
                if shown:
                    # [Guía] Ejecuta pending_text.append con los argumentos de esta llamada.
                    pending_text.append('[DEMO] ' + shown)
            # [Guía] Combina el valor anterior de `self.demo_tick` con `1` mediante esta operación.
            self.demo_tick += 1
            # [Guía] Conserva en `point` el resultado de `self.demo_sampler.sample` con los argumentos indicados.
            point = self.demo_sampler.sample(self.state, now)
            # [Guía] Evalúa `point is not None` antes de ejecutar las instrucciones de esta rama.
            if point is not None:
                # [Guía] Envía el nuevo punto al historial y a la ruta visual. El registro CSV se escribe en la
                # [Guía] ruta de adquisición de la sesión.
                self.append_sample(point)
        # [Guía] Recorre range(4096). En cada iteración, _ recibe el elemento correspondiente.
        for _ in range(4096):
            # [Guía] Delimita la operación cuyos errores o cierre se gestionan en las ramas siguientes.
            try:
                # [Guía] Conserva en `(kind, value, shown)` el resultado de `self.session.events.get_nowait` con
                # [Guía] los argumentos indicados.
                kind, value, shown = self.session.events.get_nowait()
            # [Guía] Atiende las excepciones que coinciden con `except queue.Empty` en el bloque anterior.
            except queue.Empty:
                # [Guía] Termina el bucle y sigue después de él.
                break
            # [Guía] Evalúa `kind == 'line'` antes de ejecutar las instrucciones de esta rama.
            if kind == 'line':
                # [Guía] Establece `(raw, received_at)` a partir de `value`.
                raw, received_at = value
                # [Guía] Ejecuta self.state.consume con los argumentos de esta llamada.
                self.state.consume(raw, received_at)
                # [Guía] Evalúa `shown` antes de ejecutar las instrucciones de esta rama.
                if shown:
                    # [Guía] Ejecuta pending_text.append con los argumentos de esta llamada.
                    pending_text.append(shown)
            # [Guía] Evalúa `kind == 'sample'` antes de ejecutar las instrucciones de esta rama.
            elif kind == 'sample':
                # [Guía] Ejecuta self.append_sample con los argumentos de esta llamada.
                self.append_sample(value)
            # [Guía] Evalúa `kind == 'error'` antes de ejecutar las instrucciones de esta rama.
            elif kind == 'error':
                # [Guía] Ejecuta self.session.history.stop con los argumentos de esta llamada.
                self.session.history.stop(now)
                # [Guía] Ejecuta self.history.stop con los argumentos de esta llamada.
                self.history.stop(now)
                # [Guía] Ejecuta pending_text.append con los argumentos de esta llamada.
                pending_text.append(value)
                # [Guía] Ejecuta self.connection.configure con los argumentos de esta llamada.
                self.connection.configure(text='ERROR DE REGISTRO/LECTURA')
                # [Guía] Establece `self.stopping` a partir de `True`.
                self.stopping = True
                # [Guía] Establece `self.stop_since` a partir de `now`.
                self.stop_since = now
                # [Guía] Ejecuta self.send_button.configure con los argumentos de esta llamada.
                self.send_button.configure(state='disabled')
            # [Guía] Evalúa `kind == 'exit'` antes de ejecutar las instrucciones de esta rama.
            elif kind == 'exit':
                # [Guía] Ejecuta self.history.stop con los argumentos de esta llamada.
                self.history.stop(now)
                # [Guía] Ejecuta pending_text.append con los argumentos de esta llamada.
                pending_text.append(f'Puente cerrado. Codigo={value}. Registros: {self.session.folder}')
                # [Guía] Ejecuta self.connection.configure con los argumentos de esta llamada.
                self.connection.configure(text='DESCONECTADO' if value == 0 else f'ERROR DEL PUENTE ({value})')
                # [Guía] Ejecuta self.connect_button.configure con los argumentos de esta llamada.
                self.connect_button.configure(text='Conectar', state='normal')
                # [Guía] Ejecuta self.send_button.configure con los argumentos de esta llamada.
                self.send_button.configure(state='disabled')
                # [Guía] Ejecuta self.arduino_box.configure con los argumentos de esta llamada.
                self.arduino_box.configure(state='normal')
                # [Guía] Ejecuta self.load_box.configure con los argumentos de esta llamada.
                self.load_box.configure(state='normal')
                # [Guía] Establece `self.stopping` a partir de `False`.
                self.stopping = False
        # [Guía] Ejecuta self.append_logs con los argumentos de esta llamada.
        self.append_logs(pending_text)
        # [Guía] Establece `live` a partir de `self.demo or self.session.alive()`.
        live = self.demo or self.session.alive()
        # [Guía] Da formato a una lectura vigente con el número indicado de decimales y muestra un marcador cuando
        # [Guía] el dato no está disponible. Recibe `number`, `fresh`, `digits`.
        def value(number, fresh, digits=3):
            # [Guía] Entrega f'{number:.{digits}f}' if live and fresh and (number is not None) else '--' al
            # [Guía] llamador y termina la función.
            return f'{number:.{digits}f}' if live and fresh and number is not None else '--'
        # [Guía] Recorre self.measures.items(). En cada iteración, ((channel, kind), label) recibe el elemento
        # [Guía] correspondiente.
        for (channel, kind), label in self.measures.items():
            # [Guía] Establece `group` a partir de `'power' if kind == 'power' else 'measure'`.
            group = 'power' if kind == 'power' else 'measure'
            # [Guía] Establece `point` a partir de `self.display_filter.last`.
            point = self.display_filter.last
            # [Guía] Establece `fresh` a partir de `point is not None and self.history.started_at is not None and
            # [Guía] (now - self.history.started_at - point.elapsed <=…`.
            fresh = (point is not None and self.history.started_at is not None
                     # [Guía] Continúa la instrucción con `and now - self.history.started_at - point.elapsed <=
                     # [Guía] 2.5`.
                     and now - self.history.started_at - point.elapsed <= 2.5
                     # [Guía] Continúa la instrucción con `and point.mode == self.state.mode and
                     # [Guía] self.state.fresh(group, now))`.
                     and point.mode == self.state.mode and self.state.fresh(group, now))
            # [Guía] Establece `number` a partir de `point.value(kind, channel) if fresh else None`.
            number = point.value(kind, channel) if fresh else None
            # Un canal invalidado por la trama mas reciente se oculta de inmediato.
            # [Guía] Evalúa `getattr(self.state, kind)[channel] is None` antes de ejecutar las instrucciones de
            # [Guía] esta rama.
            if getattr(self.state, kind)[channel] is None:
                # [Guía] Establece `number` a partir de `None`.
                number = None
            # [Guía] Ejecuta label.configure con los argumentos de esta llamada.
            label.configure(text=value(number, fresh))
        # [Guía] Establece `power_kind` a partir de `self.state.power_source if live and self.state.fresh('power',
        # [Guía] now) else 'sin datos'`.
        power_kind = self.state.power_source if live and self.state.fresh('power', now) else 'sin datos'
        # [Guía] Ejecuta self.power_note.configure con los argumentos de esta llamada.
        self.power_note.configure(text=f'V/I/P suavizados · τ={DISPLAY_FILTER_TAU_S:.2f} s · CSV sin filtro visual\nPotencia: {power_kind} · Batería negativa = carga')
        # Caducidad por grupo: una respuesta BK reciente no renueva EMS/SOC.
        # live evita conservar numeros aparentemente vivos despues de desconectar.
        # [Guía] Establece `fresh_ems` a partir de `live and self.state.fresh('ems', now)`.
        fresh_ems = live and self.state.fresh('ems', now)
        # [Guía] Ejecuta self.mode.configure con los argumentos de esta llamada.
        self.mode.configure(text=operation_label(self.state.mode) if fresh_ems else '--', foreground='#ffbdad' if fresh_ems and self.state.fault else 'white')
        # [Guía] Establece `reason` a partir de `self.state.reason if fresh_ems else 'SIN_DATOS_RECIENTES'`.
        reason = self.state.reason if fresh_ems else 'SIN_DATOS_RECIENTES'
        # [Guía] Evalúa `fresh_ems and self.state.m2_active and (self.state.charge_current_reference is not None)`
        # [Guía] antes de ejecutar las instrucciones de esta rama.
        if fresh_ems and self.state.m2_active and self.state.charge_current_reference is not None:
            # [Guía] Combina el valor anterior de `reason` con `f' · referencia Ibat =
            # [Guía] {self.state.charge_current_reference:.3f} A'` mediante esta operación.
            reason += f' · referencia Ibat = {self.state.charge_current_reference:.3f} A'
        # [Guía] Ejecuta self.reason.configure con los argumentos de esta llamada.
        self.reason.configure(text=reason)
        # [Guía] Recorre zip(self.state.duty, self.duties). En cada iteración, (number, label) recibe el elemento
        # [Guía] correspondiente.
        for number, label in zip(self.state.duty, self.duties):
            # [Guía] Ejecuta label.configure con los argumentos de esta llamada.
            label.configure(text=value(number, fresh_ems, 1) + ('%' if fresh_ems and number is not None else ''))
        # [Guía] Establece `fresh_soc` a partir de `live and self.state.fresh('soc', now)`.
        fresh_soc = live and self.state.fresh('soc', now)
        # [Guía] Ejecuta self.soc_value.configure con los argumentos de esta llamada.
        self.soc_value.configure(text=value(self.state.soc, fresh_soc, 3) + ('%' if fresh_soc and self.state.soc is not None else ''))
        # [Guía] Establece `self.soc_bar['value']` a partir de `self.state.soc if fresh_soc and self.state.soc is
        # [Guía] not None else 0`.
        self.soc_bar['value'] = self.state.soc if fresh_soc and self.state.soc is not None else 0
        # [Guía] Establece `soc_status` a partir de `self.state.soc_state + (' · discontinuo' if not
        # [Guía] self.state.soc_continuous else '')`.
        soc_status = self.state.soc_state + (' · discontinuo' if not self.state.soc_continuous else '')
        # [Guía] Ejecuta self.soc_status.configure con los argumentos de esta llamada.
        self.soc_status.configure(text=soc_status if fresh_soc else 'SIN_DATOS')
        # [Guía] Establece `fresh_bk` a partir de `live and self.state.fresh('bk', now)`.
        fresh_bk = live and self.state.fresh('bk', now)
        # [Guía] Conserva en `ohms` el resultado de `value` con los argumentos indicados.
        ohms = value(self.state.resistance, fresh_bk, 1)
        # [Guía] Establece `load` a partir de `('ON confirmado' if self.state.load else 'OFF confirmado') if
        # [Guía] fresh_bk and self.state.load is not None else 'SI…`.
        load = ('ON confirmado' if self.state.load else 'OFF confirmado') if fresh_bk and self.state.load is not None else 'SIN CONFIRMAR'
        # [Guía] Establece `link` a partir de `('enlace BK OK' if self.state.linked else 'sin enlace BK') if
        # [Guía] fresh_bk else 'sin datos BK'`.
        link = ('enlace BK OK' if self.state.linked else 'sin enlace BK') if fresh_bk else 'sin datos BK'
        # [Guía] Ejecuta self.emulator.configure con los argumentos de esta llamada.
        self.emulator.configure(text=f'EMULADOR   {ohms} ohm   |   {load}   |   {link}')
        # [Guía] Establece `mppt` a partir de `('1 · ACTIVO' if self.state.mppt else '0 · INACTIVO') if fresh_ems
        # [Guía] and self.state.mppt is not None else '--'`.
        mppt = ('1 · ACTIVO' if self.state.mppt else '0 · INACTIVO') if fresh_ems and self.state.mppt is not None else '--'
        # [Guía] Ejecuta self.mppt.configure con los argumentos de esta llamada.
        self.mppt.configure(text=f'MPPT   {mppt}')
        # [Guía] Evalúa `not self.demo and self.session.alive() and (not self.stopping)` antes de ejecutar las
        # [Guía] instrucciones de esta rama.
        if not self.demo and self.session.alive() and not self.stopping:
            # [Guía] Ejecuta self.connection.configure con los argumentos de esta llamada.
            self.connection.configure(text='CONECTADO' if self.state.fresh('measure', now) else 'SIN TELEMETRIA RECIENTE')
        # [Guía] Evalúa `self.stopping and self.stop_since is not None and (now - self.stop_since > 15)` antes de
        # [Guía] ejecutar las instrucciones de esta rama.
        if self.stopping and self.stop_since is not None and now - self.stop_since > 15:
            # [Guía] Ejecuta self.connection.configure con los argumentos de esta llamada.
            self.connection.configure(text='CIERRE PENDIENTE · comprobar BK')
        # [Guía] Evalúa `self.session.folder` antes de ejecutar las instrucciones de esta rama.
        if self.session.folder:
            # [Guía] Establece `dropped` a partir de `f' | Vista omitio {self.session.ui_dropped} eventos;
            # [Guía] consultar archivos' if self.session.ui_dropped else ''`.
            dropped = f' | Vista omitio {self.session.ui_dropped} eventos; consultar archivos' if self.session.ui_dropped else ''
            # [Guía] Ejecuta self.detail.configure con los argumentos de esta llamada.
            self.detail.configure(text=f'{self.state.version}  |  {self.session.folder}{dropped}')
        # [Guía] Evalúa `self.plots is not None` antes de ejecutar las instrucciones de esta rama.
        if self.plots is not None:
            # [Guía] Ejecuta self.plots.render con los argumentos de esta llamada.
            self.plots.render(now)
        # [Guía] Evalúa `self.closing and (not self.session.alive()) and (self.session.thread is None or not
        # [Guía] self.session.thread.is_aliv…` antes de ejecutar las instrucciones de esta rama.
        if self.closing and not self.session.alive() and (self.session.thread is None or not self.session.thread.is_alive()):
            # [Guía] Ejecuta self.root.destroy con los argumentos de esta llamada.
            self.root.destroy()
            # [Guía] Termina la función en este punto.
            return
        # [Guía] Ejecuta self.root.after con los argumentos de esta llamada.
        self.root.after(REFRESH_MS, self.refresh)

    # [Guía] Lleva una muestra al historial de gráficas y actualiza la representación de los datos eléctricos.
    # [Guía] Recibe `point`.
    def append_sample(self, point):
        """CSV ya guardado por el lector; filtrar solo la copia destinada a Tk.

        No aceptar puntos fuera de sesion, repetidos ni posteriores a Desconectar.
        Se valida antes del filtro para no contaminar su memoria con esos eventos.
        """
        # [Guía] Evalúa `self.history.started_at is None` antes de ejecutar las instrucciones de esta rama.
        if self.history.started_at is None:
            # [Guía] Termina la función en este punto.
            return
        # [Guía] Evalúa `self.history.points and point.elapsed <= self.history.points[-1].elapsed` antes de
        # [Guía] ejecutar las instrucciones de esta rama.
        if self.history.points and point.elapsed <= self.history.points[-1].elapsed:
            # [Guía] Termina la función en este punto.
            return
        # [Guía] Evalúa `self.history.stopped_at is not None and point.elapsed > self.history.stopped_at -
        # [Guía] self.history.started_at` antes de ejecutar las instrucciones de esta rama.
        if (self.history.stopped_at is not None
                # [Guía] Continúa la instrucción con `and point.elapsed > self.history.stopped_at -
                # [Guía] self.history.started_at):`.
                and point.elapsed > self.history.stopped_at - self.history.started_at):
            # [Guía] Termina la función en este punto.
            return
        # [Guía] Ejecuta self.history.append con los argumentos de esta llamada.
        self.history.append(self.display_filter.update(point))

    # [Guía] Copiar la seleccion si existe; en otro caso, copiar las lineas visibles.
    def copy_log(self):
        """Copiar la seleccion si existe; en otro caso, copiar las lineas visibles."""
        # [Guía] Delimita la operación cuyos errores o cierre se gestionan en las ramas siguientes.
        try:
            # [Guía] Conserva en `text` el resultado de `self.terminal.get` con los argumentos indicados.
            text = self.terminal.get('sel.first', 'sel.last')
        # [Guía] Atiende las excepciones que coinciden con `except tk.TclError` en el bloque anterior.
        except tk.TclError:
            # [Guía] Conserva en `text` el resultado de `self.terminal.get` con los argumentos indicados.
            text = self.terminal.get('1.0', 'end-1c')
        # [Guía] Ejecuta self.root.clipboard_clear con los argumentos de esta llamada.
        self.root.clipboard_clear()
        # [Guía] Ejecuta self.root.clipboard_append con los argumentos de esta llamada.
        self.root.clipboard_append(text)

    # [Guía] Abrir solo la carpeta local de esta sesion, nunca ejecutar su contenido.
    def open_logs(self):
        """Abrir solo la carpeta local de esta sesion, nunca ejecutar su contenido."""
        # [Guía] Establece `path` a partir de `self.session.folder or Path(self.log_root).expanduser().resolve()`.
        path = self.session.folder or Path(self.log_root).expanduser().resolve()
        # [Guía] Ejecuta path.mkdir con los argumentos de esta llamada.
        path.mkdir(parents=True, exist_ok=True)
        # [Guía] Evalúa `hasattr(os, 'startfile')` antes de ejecutar las instrucciones de esta rama.
        if hasattr(os, 'startfile'):
            # [Guía] Ejecuta os.startfile con los argumentos de esta llamada.
            os.startfile(str(path))
        # [Guía] Ejecuta este bloque cuando se cumple la alternativa del bloque anterior.
        else:
            # [Guía] Ejecuta messagebox.showinfo con los argumentos de esta llamada.
            messagebox.showinfo('Registros', str(path), parent=self.root)

    # [Guía] Cerrar la ventana solo despues del puente; no abandonar ACK a la fuerza.
    def close(self):
        """Cerrar la ventana solo despues del puente; no abandonar ACK a la fuerza."""
        # [Guía] Evalúa `self.session.alive() and (not self.stopping)` antes de ejecutar las instrucciones de esta
        # [Guía] rama.
        if self.session.alive() and not self.stopping:
            # [Guía] Evalúa `not messagebox.askokcancel('Cerrar sesion', 'Se cerrara el puente y se solicitara
            # [Guía] OFF/LOCAL a la B&K. Esto no e…` antes de ejecutar las instrucciones de esta rama.
            if not messagebox.askokcancel('Cerrar sesion', 'Se cerrara el puente y se solicitara OFF/LOCAL a la B&K. Esto no es una parada de emergencia de los PWM.', parent=self.root):
                # [Guía] Termina la función en este punto.
                return
            # [Guía] Ejecuta self.disconnect con los argumentos de esta llamada.
            self.disconnect()
        # [Guía] Establece `self.closing` a partir de `True`.
        self.closing = True
        # [Guía] Evalúa `not self.session.alive() and (self.session.thread is None or not
        # [Guía] self.session.thread.is_alive())` antes de ejecutar las instrucciones de esta rama.
        if not self.session.alive() and (self.session.thread is None or not self.session.thread.is_alive()):
            # [Guía] Ejecuta self.root.destroy con los argumentos de esta llamada.
            self.root.destroy()


# [Guía] Interpreta argumentos, crea la ventana de Tk y deja que su bucle de eventos atienda la aplicación.
def main():
    """Arranque unico del programa; --demo nunca instancia un puente fisico."""
    # [Guía] Conserva en `parser` el resultado de `argparse.ArgumentParser` con los argumentos indicados.
    parser = argparse.ArgumentParser(description=__doc__)
    # [Guía] Ejecuta parser.add_argument con los argumentos de esta llamada.
    parser.add_argument('--demo', action='store_true')
    # [Guía] Ejecuta parser.add_argument con los argumentos de esta llamada.
    parser.add_argument('--registros', type=Path, default=ROOT / 'registros')
    # [Guía] Conserva en `args` el resultado de `parser.parse_args` con los argumentos indicados.
    args = parser.parse_args()
    # [Guía] Conserva en `root` el resultado de `tk.Tk` con los argumentos indicados.
    root = tk.Tk()
    # [Guía] Conserva en `app` el resultado de `Dashboard` con los argumentos indicados.
    app = Dashboard(root, demo=args.demo, log_root=args.registros)
    # [Guía] Delimita la operación cuyos errores o cierre se gestionan en las ramas siguientes.
    try:
        # [Guía] Ejecuta root.mainloop con los argumentos de esta llamada.
        root.mainloop()
    # [Guía] Atiende las excepciones que coinciden con `except KeyboardInterrupt` en el bloque anterior.
    except KeyboardInterrupt:
        # Ctrl+C conserva el cierre normal del puente aunque Tk siga abierto.
        # [Guía] Establece `app.closing` a partir de `True`.
        app.closing = True
        # [Guía] Ejecuta app.disconnect con los argumentos de esta llamada.
        app.disconnect()
        # [Guía] Ejecuta root.mainloop con los argumentos de esta llamada.
        root.mainloop()
    # [Guía] Entrega 0 al llamador y termina la función.
    return 0


# [Guía] Evalúa `__name__ == '__main__'` antes de ejecutar las instrucciones de esta rama.
if __name__ == '__main__':
    # [Guía] Señala SystemExit(main()) para que la capa que llamó pueda tratar el problema.
    raise SystemExit(main())
