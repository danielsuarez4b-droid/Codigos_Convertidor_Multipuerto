# [Guía] Define la muestra de sesión, el reloj relativo, la escritura CSV, el búfer y los segmentos de las
# [Guía] gráficas.
"""Series temporales y CSV, sin Tk ni acceso a puertos.

El tiempo es monotono del PC desde Conectar; no es el instante ADC del Arduino.
El lector del puente guarda CSV antes de publicar a la cola visual. La memoria
de pantalla tiene un limite; el archivo conserva toda la sesion a unos5Hz.
"""
# [Guía] Importa deque para las operaciones que realiza este módulo.
from collections import deque
# [Guía] Importa csv para las operaciones que realiza este módulo.
import csv
# [Guía] Importa dataclass para las operaciones que realiza este módulo.
from dataclasses import dataclass
# [Guía] Importa datetime, timezone para las operaciones que realiza este módulo.
from datetime import datetime, timezone
# [Guía] Importa math para las operaciones que realiza este módulo.
import math
# [Guía] Importa threading para las operaciones que realiza este módulo.
import threading
# [Guía] Importa time para las operaciones que realiza este módulo.
import time

# [Guía] Importa DashboardState para las operaciones que realiza este módulo.
from dashboard_data import DashboardState

# [Guía] Establece `CHANNELS` a partir de `('PV', 'BUS', 'BAT')`.
CHANNELS = ('PV', 'BUS', 'BAT')
# [Guía] Establece `KINDS` a partir de `('voltage', 'current', 'power')`.
KINDS = ('voltage', 'current', 'power')
# [Guía] Establece `SAMPLE_SECONDS` a partir de `0.2`.
SAMPLE_SECONDS = 0.2
# [Guía] Establece `HISTORY_POINTS` a partir de `9001`.
HISTORY_POINTS = 9001  # Aproximadamente30min a5Hz, memoria acotada.
# [Guía] Establece `CSV_COLUMNS` a partir de `('elapsed_s', 'utc', 'mode', 'power_source', 'PV_V', 'BUS_V',
# [Guía] 'BAT_V', 'PV_A', 'BUS_A', 'BAT_A', 'PV_W', 'BUS_W…`.
CSV_COLUMNS = ('elapsed_s', 'utc', 'mode', 'power_source',
               # [Guía] Continúa la instrucción con `'PV_V', 'BUS_V', 'BAT_V', 'PV_A', 'BUS_A', 'BAT_A'`.
               'PV_V', 'BUS_V', 'BAT_V', 'PV_A', 'BUS_A', 'BAT_A',
               # [Guía] Continúa la instrucción con `'PV_W', 'BUS_W', 'BAT_W')`.
               'PV_W', 'BUS_W', 'BAT_W')


@dataclass(frozen=True)
# [Guía] Agrupa en HistoryPoint el estado y las operaciones de esta parte de la aplicación.
class HistoryPoint:
    # [Guía] Declara elapsed con el tipo float.
    elapsed: float
    # [Guía] Declara values con el tipo tuple.
    values: tuple  # V(PV/BUS/BAT), I(PV/BUS/BAT), P(PV/BUS/BAT).
    # [Guía] Declara mode con el tipo str.
    mode: str
    # [Guía] Declara power_source con el tipo str.
    power_source: str

    # [Guía] Selecciona en el vector de la muestra el valor de una magnitud y un puerto determinados. Recibe
    # [Guía] `kind`, `channel`.
    def value(self, kind, channel):
        # [Guía] Entrega self.values[KINDS.index(kind) * 3 + CHANNELS.index(channel)] al llamador y termina la
        # [Guía] función.
        return self.values[KINDS.index(kind) * 3 + CHANNELS.index(channel)]


# [Guía] Agrupa en HistorySampler el estado y las operaciones de esta parte de la aplicación.
class HistorySampler:
    """Una muestra por intervalo200ms; no inventar ceros ni repetir datos viejos."""
    # [Guía] Inicializa el reloj de captura y el estado que evita duplicar muestras en el mismo intervalo.
    def __init__(self):
        # [Guía] Establece `self.started_at` a partir de `None`.
        self.started_at = None
        # [Guía] Establece `self.stopped_at` a partir de `None`.
        self.stopped_at = None
        # [Guía] Establece `self.last_bucket` a partir de `-1`.
        self.last_bucket = -1
        # [Guía] Establece `self.last_measure` a partir de `None`.
        self.last_measure = None

    # [Guía] Inicia el estado de este módulo. Recibe `now`.
    def start(self, now):
        # [Guía] Establece `(self.started_at, self.stopped_at)` a partir de `(now, None)`.
        self.started_at, self.stopped_at = now, None
        # [Guía] Establece `(self.last_bucket, self.last_measure)` a partir de `(-1, None)`.
        self.last_bucket, self.last_measure = -1, None

    # [Guía] Fija el instante de parada del muestreo para rechazar las medidas posteriores. Recibe `now`.
    def stop(self, now):
        # [Guía] Evalúa `self.started_at is not None and self.stopped_at is None` antes de ejecutar las
        # [Guía] instrucciones de esta rama.
        if self.started_at is not None and self.stopped_at is None:
            # [Guía] Establece `self.stopped_at` a partir de `now`.
            self.stopped_at = now

    # [Guía] Acepta una medida nueva si la sesión está activa y se alcanzó un intervalo de captura nuevo. Conserva
    # [Guía] los datos inválidos como huecos. Recibe `state`, `now`.
    def sample(self, state, now):
        # [Guía] Evalúa `self.started_at is None or self.stopped_at is not None or now < self.started_at` antes de
        # [Guía] ejecutar las instrucciones de esta rama.
        if self.started_at is None or self.stopped_at is not None or now < self.started_at:
            # [Guía] Entrega None al llamador y termina la función.
            return None
        # [Guía] Lee la marca de recepción de la última medida. Se usa para distinguir una muestra nueva de otra
        # [Guía] línea sobre la misma medida.
        stamp = state.stamps.get('measure')
        # [Guía] Evalúa `stamp is None or stamp == self.last_measure or (not state.fresh('measure', now))` antes
        # [Guía] de ejecutar las instrucciones de esta rama.
        if stamp is None or stamp == self.last_measure or not state.fresh('measure', now):
            # [Guía] Entrega None al llamador y termina la función.
            return None
        # [Guía] Expresa el tiempo del dato en segundos desde el inicio de la sesión, que es el origen del eje
        # [Guía] horizontal.
        elapsed = now - self.started_at
        # Intervalos en lugar de comparar dt>=0.2: el jitter de una trama
        # cada199ms no debe reducir accidentalmente la captura a2.5Hz.
        # [Guía] Asigna la muestra a un intervalo nominal de captura. El pequeño margen evita errores de redondeo
        # [Guía] en límites exactos.
        bucket = int((elapsed + 1e-9) / SAMPLE_SECONDS)
        # [Guía] Impide guardar dos puntos en el mismo intervalo o retroceder en el eje de tiempo.
        if bucket <= self.last_bucket:
            # [Guía] Entrega None al llamador y termina la función.
            return None
        # [Guía] Establece `values` a partir de `[]`.
        values = []
        # [Guía] Recorre KINDS. En cada iteración, kind recibe el elemento correspondiente.
        for kind in KINDS:
            # [Guía] Establece `group` a partir de `'power' if kind == 'power' else 'measure'`.
            group = 'power' if kind == 'power' else 'measure'
            # [Guía] Recorre CHANNELS. En cada iteración, channel recibe el elemento correspondiente.
            for channel in CHANNELS:
                # [Guía] Establece `number` a partir de `getattr(state, kind)[channel] if state.fresh(group, now)
                # [Guía] else None`.
                number = getattr(state, kind)[channel] if state.fresh(group, now) else None
                # [Guía] Conserva el valor válido y representa un dato ausente o no finito con un hueco en la
                # [Guía] serie.
                values.append(number if number is not None and math.isfinite(number) else None)
        # [Guía] Establece `(self.last_bucket, self.last_measure)` a partir de `(bucket, stamp)`.
        self.last_bucket, self.last_measure = bucket, stamp
        # [Guía] Entrega HistoryPoint(elapsed, tuple(values), state.mode, state.power_source) al llamador y
        # [Guía] termina la función.
        return HistoryPoint(elapsed, tuple(values), state.mode, state.power_source)


# [Guía] Agrupa en SessionHistory el estado y las operaciones de esta parte de la aplicación.
class SessionHistory:
    """Propiedad del lector del puente; stop() puede llamarse desde Tk.

    El lock hace que Desconectar marque el final antes de retornar. Los datos
    recibidos durante SALIR/OFF/LOCAL siguen en logs, pero no extienden la grafica.
    """
    # [Guía] Prepara el lector de telemetría, el muestreador y la sincronización necesaria para abrir una sesión
    # [Guía] de escritura.
    def __init__(self):
        # [Guía] Conserva en `self.lock` el resultado de `threading.RLock` con los argumentos indicados.
        self.lock = threading.RLock()
        # [Guía] Conserva en `self.sampler` el resultado de `HistorySampler` con los argumentos indicados.
        self.sampler = HistorySampler()
        # [Guía] Conserva en `self.state` el resultado de `DashboardState` con los argumentos indicados.
        self.state = DashboardState()
        # [Guía] Establece `self.stream, self.writer` a partir de `None`.
        self.stream = self.writer = None
        # [Guía] Establece `self.live_at` a partir de `None`.
        self.live_at = None

    # [Guía] Inicia ed at.
    @property
    def started_at(self):
        # [Guía] Entrega self.sampler.started_at al llamador y termina la función.
        return self.sampler.started_at

    # [Guía] Inicia el estado de este módulo. Recibe `path`, `now`.
    def start(self, path, now):
        # [Guía] Protege el estado de sesión frente al acceso simultáneo del lector del puente y de la interfaz.
        with self.lock:
            # [Guía] Ejecuta self.close con los argumentos de esta llamada.
            self.close()
            # [Guía] Conserva en `self.stream` el resultado de `path.open` con los argumentos indicados.
            self.stream = path.open('x', encoding='utf-8', newline='', buffering=1)
            # [Guía] Delimita la operación cuyos errores o cierre se gestionan en las ramas siguientes.
            try:
                # [Guía] Conserva en `self.writer` el resultado de `csv.writer` con los argumentos indicados.
                self.writer = csv.writer(self.stream)
                # [Guía] Ejecuta self.writer.writerow con los argumentos de esta llamada.
                self.writer.writerow(CSV_COLUMNS)
            # [Guía] Atiende las excepciones que coinciden con `except Exception` en el bloque anterior.
            except Exception:
                # [Guía] Ejecuta self.stream.close con los argumentos de esta llamada.
                self.stream.close(); self.stream = self.writer = None
                # [Guía] Propaga la excepción que se estaba gestionando.
                raise
            # [Guía] Establece `(self.state, self.live_at)` a partir de `(DashboardState(), None)`.
            self.state, self.live_at = DashboardState(), None
            # [Guía] Ejecuta self.sampler.start con los argumentos de esta llamada.
            self.sampler.start(now)

    # [Guía] Interpreta la línea y, si trae una medida nueva apta para la captura, escribe el punto en el CSV y lo
    # [Guía] devuelve. Recibe `line`, `now`.
    def consume(self, line, now):
        # [Guía] Protege el estado de sesión frente al acceso simultáneo del lector del puente y de la interfaz.
        with self.lock:
            # [Guía] Evalúa `self.writer is None or self.sampler.stopped_at is not None` antes de ejecutar las
            # [Guía] instrucciones de esta rama.
            if self.writer is None or self.sampler.stopped_at is not None:
                # [Guía] Entrega None al llamador y termina la función.
                return None
            # [Guía] Evalúa `not self.state.consume(line, now)` antes de ejecutar las instrucciones de esta rama.
            if not self.state.consume(line, now):
                # [Guía] Entrega None al llamador y termina la función.
                return None
            # [Guía] Evalúa `line.startswith('LIVE ')` antes de ejecutar las instrucciones de esta rama.
            if line.startswith('LIVE '):
                # [Guía] Establece `self.live_at` a partir de `now`.
                self.live_at = now
            # [Guía] Evalúa `not line.startswith('V_mV ') or (self.live_at is not None and now - self.live_at <
            # [Guía] 2.5)` antes de ejecutar las instrucciones de esta rama.
            elif not line.startswith('V_mV ') or (self.live_at is not None and now - self.live_at < 2.5):
                # [Guía] Entrega None al llamador y termina la función.
                return None  # Preferir LIVE; no duplicarlo con su diagnostico largo.
            # [Guía] Conserva en `point` el resultado de `self.sampler.sample` con los argumentos indicados.
            point = self.sampler.sample(self.state, now)
            # [Guía] Evalúa `point is not None` antes de ejecutar las instrucciones de esta rama.
            if point is not None:
                # [Guía] Guarda el tiempo relativo y la hora UTC junto al modo, procedencia y valores de la
                # [Guía] muestra sin suavizado visual.
                self.writer.writerow((f'{point.elapsed:.6f}', datetime.now(timezone.utc).isoformat(),
                    # [Guía] Continúa la instrucción con `point.mode, point.power_source`.
                    point.mode, point.power_source,
                    # [Guía] Continúa la instrucción con `*('' if value is None else f'{value:.6f}' for value in
                    # [Guía] point.values)))`.
                    *('' if value is None else f'{value:.6f}' for value in point.values)))
            # [Guía] Entrega point al llamador y termina la función.
            return point

    # [Guía] Congela el muestreo bajo el cerrojo de sesión para evitar escrituras nuevas después del cierre de
    # [Guía] captura. Recibe `now`.
    def stop(self, now=None):
        # [Guía] Protege el estado de sesión frente al acceso simultáneo del lector del puente y de la interfaz.
        with self.lock:
            # [Guía] Ejecuta self.sampler.stop con los argumentos de esta llamada.
            self.sampler.stop(time.monotonic() if now is None else now)

    # [Guía] Cierra el estado de este módulo.
    def close(self):
        # [Guía] Protege el estado de sesión frente al acceso simultáneo del lector del puente y de la interfaz.
        with self.lock:
            # [Guía] Ejecuta self.stop con los argumentos de esta llamada.
            self.stop()
            # [Guía] Evalúa `self.stream is not None` antes de ejecutar las instrucciones de esta rama.
            if self.stream is not None:
                # [Guía] Delimita la operación cuyos errores o cierre se gestionan en las ramas siguientes.
                try:
                    # [Guía] Ejecuta self.stream.close con los argumentos de esta llamada.
                    self.stream.close()
                # [Guía] Ejecuta este cierre tanto si la operación termina normalmente como si produce una
                # [Guía] excepción.
                finally:
                    # [Guía] Establece `self.stream, self.writer` a partir de `None`.
                    self.stream = self.writer = None


# [Guía] Agrupa en HistoryBuffer el estado y las operaciones de esta parte de la aplicación.
class HistoryBuffer:
    """Solo vista: conservar ultimos30min; el CSV se guarda por otro camino."""
    # [Guía] Crea el búfer de puntos que puede consultar la gráfica y prepara sus marcas de tiempo.
    def __init__(self):
        # [Guía] Conserva en `self.points` el resultado de `deque` con los argumentos indicados.
        self.points = deque(maxlen=HISTORY_POINTS)
        # [Guía] Establece `self.started_at, self.stopped_at` a partir de `None`.
        self.started_at = self.stopped_at = None

    # [Guía] Inicia el estado de este módulo. Recibe `now`.
    def start(self, now):
        # [Guía] Ejecuta self.points.clear con los argumentos de esta llamada.
        self.points.clear()
        # [Guía] Establece `(self.started_at, self.stopped_at)` a partir de `(now, None)`.
        self.started_at, self.stopped_at = now, None

    # [Guía] Registra el corte temporal del búfer visual para conservar la sesión congelada. Recibe `now`.
    def stop(self, now):
        # [Guía] Evalúa `self.started_at is not None and self.stopped_at is None` antes de ejecutar las
        # [Guía] instrucciones de esta rama.
        if self.started_at is not None and self.stopped_at is None:
            # [Guía] Establece `self.stopped_at` a partir de `now`.
            self.stopped_at = now

    # [Guía] Añade el estado de este módulo. Recibe `point`.
    def append(self, point):
        # [Guía] Evalúa `self.started_at is None or (self.points and point.elapsed <= self.points[-1].elapsed)`
        # [Guía] antes de ejecutar las instrucciones de esta rama.
        if self.started_at is None or (self.points and point.elapsed <= self.points[-1].elapsed):
            # [Guía] Entrega False al llamador y termina la función.
            return False
        # [Guía] Evalúa `self.stopped_at is not None and point.elapsed > self.stopped_at - self.started_at` antes
        # [Guía] de ejecutar las instrucciones de esta rama.
        if self.stopped_at is not None and point.elapsed > self.stopped_at - self.started_at:
            # [Guía] Entrega False al llamador y termina la función.
            return False
        # [Guía] Ejecuta self.points.append con los argumentos de esta llamada.
        self.points.append(point)
        # [Guía] Repite el bloque mientras se cumpla `self.points and self.points[0].elapsed < point.elapsed -
        # [Guía] 1800`.
        while self.points and self.points[0].elapsed < point.elapsed - 1800:
            # [Guía] Ejecuta self.points.popleft con los argumentos de esta llamada.
            self.points.popleft()
        # [Guía] Entrega True al llamador y termina la función.
        return True

    # [Guía] Calcula el tiempo de sesión visible hasta el instante actual o hasta el corte si la captura ya se
    # [Guía] detuvo. Recibe `now`.
    def elapsed(self, now):
        # [Guía] Establece `end` a partir de `self.stopped_at if self.stopped_at is not None else now`.
        end = self.stopped_at if self.stopped_at is not None else now
        # [Guía] Entrega max(0, end - self.started_at) if self.started_at is not None else 0 al llamador y termina
        # [Guía] la función.
        return max(0, end - self.started_at) if self.started_at is not None else 0


# [Guía] Prepara segmentos dibujables, conserva huecos de muestras inválidas y limita cuántos puntos llegan al
# [Guía] lienzo. Recibe `points`, `kind`, `channel`, `limit`.
def plot_segments(points, kind, channel, limit=800):
    """Separar huecos/invalidos y reducir dibujo preservando extremos.

    Solo afecta los trazos: el CSV y HistoryBuffer conservan las muestras.
    No dibujar una union continua a traves de una perdida de telemetria.
    """
    # [Guía] Establece `(segments, current, previous)` a partir de `([], [], None)`.
    segments, current, previous = [], [], None
    # [Guía] Recorre points. En cada iteración, point recibe el elemento correspondiente.
    for point in points:
        # [Guía] Conserva en `value` el resultado de `point.value` con los argumentos indicados.
        value = point.value(kind, channel)
        # [Guía] Evalúa `value is None or (previous is not None and point.elapsed - previous > 0.75)` antes de
        # [Guía] ejecutar las instrucciones de esta rama.
        if value is None or (previous is not None and point.elapsed - previous > 0.75):
            # [Guía] Evalúa `current` antes de ejecutar las instrucciones de esta rama.
            if current:
                # [Guía] Ejecuta segments.append con los argumentos de esta llamada.
                segments.append(current); current = []
        # [Guía] Evalúa `value is not None` antes de ejecutar las instrucciones de esta rama.
        if value is not None:
            # [Guía] Ejecuta current.append con los argumentos de esta llamada.
            current.append((point.elapsed, value))
        # [Guía] Establece `previous` a partir de `point.elapsed`.
        previous = point.elapsed
    # [Guía] Evalúa `current` antes de ejecutar las instrucciones de esta rama.
    if current:
        # [Guía] Ejecuta segments.append con los argumentos de esta llamada.
        segments.append(current)
    # [Guía] Conserva en `total` el resultado de `sum` con los argumentos indicados.
    total = sum(len(segment) for segment in segments)
    # [Guía] Evalúa `total <= limit` antes de ejecutar las instrucciones de esta rama.
    if total <= limit:
        # [Guía] Entrega segments al llamador y termina la función.
        return segments
    # Minimo/maximo por cubeta evita ocultar un pico al tomar solo cadaN.
    # [Guía] Conserva en `step` el resultado de `max` con los argumentos indicados.
    step = max(1, math.ceil(total / max(1, limit // 4)))
    # [Guía] Establece `result` a partir de `[]`.
    result = []
    # [Guía] Recorre segments. En cada iteración, segment recibe el elemento correspondiente.
    for segment in segments:
        # [Guía] Establece `reduced` a partir de `[]`.
        reduced = []
        # [Guía] Recorre range(0, len(segment), step). En cada iteración, start recibe el elemento
        # [Guía] correspondiente.
        for start in range(0, len(segment), step):
            # [Guía] Establece `block` a partir de `segment[start:start + step]`.
            block = segment[start:start + step]
            # [Guía] Conserva en `indices` el resultado de `sorted` con los argumentos indicados.
            indices = sorted({0, len(block) - 1,
                # [Guía] Continúa la instrucción con `min(range(len(block)), key=lambda i: block[i][1])`.
                min(range(len(block)), key=lambda i: block[i][1]),
                # [Guía] Continúa la instrucción con `max(range(len(block)), key=lambda i: block[i][1])})`.
                max(range(len(block)), key=lambda i: block[i][1])})
            # [Guía] Ejecuta reduced.extend con los argumentos de esta llamada.
            reduced.extend(block[index] for index in indices)
        # [Guía] Ejecuta result.append con los argumentos de esta llamada.
        result.append(reduced)
    # [Guía] Entrega result al llamador y termina la función.
    return result
