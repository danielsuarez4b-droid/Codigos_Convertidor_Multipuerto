# [Guía] Dibuja las nueve series de tensión, corriente y potencia en tres columnas, calcula escalas y limita el
# [Guía] volumen de puntos dibujados.
"""Graficas Tk de supervision, sin dependencias adicionales ni comandos COM.

Nueve ejes independientes, en tres columnas PV/BUS/BAT, comparten tiempo
desde Conectar. Cada columna contiene V/I/P. La bateria conserva
signo negativo de carga. Cerrar esta ventana oculta la vista; Desconectar
detiene la captura. El guardado CSV pertenece al lector, no al repintado.
"""
# [Guía] Importa math para las operaciones que realiza este módulo.
import math
# [Guía] Importa tkinter para las operaciones que realiza este módulo.
import tkinter as tk
# [Guía] Importa ttk para las operaciones que realiza este módulo.
from tkinter import ttk

# [Guía] Importa CHANNELS, KINDS, plot_segments para las operaciones que realiza este módulo.
from dashboard_history import CHANNELS, KINDS, plot_segments

# [Guía] Establece `COLORS` a partir de `{'PV': '#b07800', 'BUS': '#22799c', 'BAT': '#17805e'}`.
COLORS = {'PV': '#b07800', 'BUS': '#22799c', 'BAT': '#17805e'}
# [Guía] Establece `NAMES` a partir de `{'PV': 'Panel PV', 'BUS': 'Bus DC', 'BAT': 'Batería'}`.
NAMES = {'PV': 'Panel PV', 'BUS': 'Bus DC', 'BAT': 'Batería'}

# Separacion nominal entre marcas Y, en las unidades de cada fila. El rango
# se extiende a multiplos de este paso para incluir TODOS los datos visibles.
# Si un transitorio necesita mas de10intervalos, se usa un multiplo mayor.
# [Guía] Establece `Y_BASE_STEPS` a partir de `{'voltage': {'PV': 7.5, 'BUS': 5.0, 'BAT': 5.0}, 'current': {'PV':
# [Guía] 0.5, 'BUS': 0.5, 'BAT': 0.5}, 'power': {'PV'…`.
Y_BASE_STEPS = {
    # [Guía] Añade la entrada 'voltage' a este mapa de datos.
    'voltage': {'PV': 7.5, 'BUS': 5.0, 'BAT': 5.0},
    # [Guía] Añade la entrada 'current' a este mapa de datos.
    'current': {'PV': 0.5, 'BUS': 0.5, 'BAT': 0.5},
    # [Guía] Añade la entrada 'power' a este mapa de datos.
    'power': {'PV': 10.0, 'BUS': 10.0, 'BAT': 10.0},
}
# [Guía] Establece `TIME_WINDOWS` a partir de `{'30 s': 30, '60 s': 60, '2 min': 120, '3 min': 180, '5 min': 300,
# [Guía] '30 min': 1800}`.
TIME_WINDOWS = {'30 s': 30, '60 s': 60, '2 min': 120, '3 min': 180,
                # [Guía] Continúa la instrucción con `'5 min': 300, '30 min': 1800}`.
                '5 min': 300, '30 min': 1800}


# [Guía] Determina inicio, fin y marcas de la ventana visible de tiempo en segundos. Recibe `elapsed`,
# [Guía] `selection`, `first`.
def time_axis(elapsed, selection, first=0):
    """Seis intervalos nominales, marcas enteras y reloj comun para9graficas.

    Antes de completar la ventana se muestra su duracion completa, sin
    inventar muestras futuras. Despues se desliza con el tiempo real; las
    marcas siguen ancladas a multiplos de20s en2min o30s en3min. Hay6o7marcas.
    """
    # [Guía] Conserva en `duration` el resultado de `TIME_WINDOWS.get` con los argumentos indicados.
    duration = TIME_WINDOWS.get(selection)
    # [Guía] Evalúa `duration is not None` antes de ejecutar las instrucciones de esta rama.
    if duration is not None:
        # [Guía] Conserva en `end` el resultado de `max` con los argumentos indicados.
        end = max(duration, elapsed)
        # [Guía] Establece `start` a partir de `end - duration`.
        start = end - duration
        # [Guía] Establece `step` a partir de `duration // 6`.
        step = duration // 6
    # [Guía] Ejecuta este bloque cuando se cumple la alternativa del bloque anterior.
    else:
        # Toda la vista: conservar puntos retenidos, con extremos enteros y
        # al menos seis marcas incluso durante los primeros segundos.
        # [Guía] Conserva en `start` el resultado de `max` con los argumentos indicados.
        start = max(0, math.floor(first))
        # [Guía] Conserva en `end` el resultado de `max` con los argumentos indicados.
        end = max(start + 6, math.ceil(elapsed))
        # [Guía] Conserva en `step` el resultado de `max` con los argumentos indicados.
        step = max(1, math.ceil((end - start) / 6))
        # [Guía] Establece `start` a partir de `start // step * step`.
        start = (start // step) * step
        # [Guía] Establece `end` a partir de `math.ceil(end / step) * step`.
        end = math.ceil(end / step) * step
    # [Guía] Establece `first_tick` a partir de `math.ceil((start - 1e-08) / step) * step`.
    first_tick = math.ceil((start - 1e-8) / step) * step
    # [Guía] Establece `last_tick` a partir de `math.floor((end + 1e-08) / step) * step`.
    last_tick = math.floor((end + 1e-8) / step) * step
    # [Guía] Conserva en `ticks` el resultado de `tuple` con los argumentos indicados.
    ticks = tuple(range(first_tick, last_tick + 1, step))
    # [Guía] Entrega (start, end, ticks) al llamador y termina la función.
    return start, end, ticks


# [Guía] Define límites y divisiones del eje vertical de acuerdo con magnitud, puerto y datos disponibles. Recibe
# [Guía] `values`, `kind`, `channel`.
def vertical_axis(values, kind, channel):
    """Rango Y en pasos regulares, incluye cero y conserva signos/extremos."""
    # [Guía] Establece `finite` a partir de `[number for number in values if number is not None and
    # [Guía] math.isfinite(number)]`.
    finite = [number for number in values if number is not None and math.isfinite(number)]
    # [Guía] Establece `(raw_low, raw_high)` a partir de `(min([0.0] + finite), max([0.0] + finite))`.
    raw_low, raw_high = min([0.0] + finite), max([0.0] + finite)
    # [Guía] Establece `base` a partir de `Y_BASE_STEPS[kind][channel]`.
    base = Y_BASE_STEPS[kind][channel]
    # Margen solo hacia el exterior de la señal. Con medidas positivas no
    # crear un limite Y negativo artificial por aplicar padding a cero.
    # [Guía] Establece `(target_low, target_high)` a partir de `(raw_low * 1.04, raw_high * 1.04)`.
    target_low, target_high = raw_low * 1.04, raw_high * 1.04
    # [Guía] Establece `multiplier` a partir de `1`.
    multiplier = 1
    # [Guía] Repite el bloque mientras se cumpla `True`.
    while True:
        # [Guía] Establece `step` a partir de `base * multiplier`.
        step = base * multiplier
        # [Guía] Conserva en `lower` el resultado de `math.floor` con los argumentos indicados.
        lower = math.floor(target_low / step)
        # [Guía] Conserva en `upper` el resultado de `math.ceil` con los argumentos indicados.
        upper = math.ceil(target_high / step)
        # [Guía] Evalúa `upper - lower <= 10` antes de ejecutar las instrucciones de esta rama.
        if upper - lower <= 10:
            # [Guía] Termina el bucle y sigue después de él.
            break
        # [Guía] Combina el valor anterior de `multiplier` con `2` mediante esta operación.
        multiplier *= 2
    # Cuatro intervalos como minimo mantienen una escala legible en reposo.
    # [Guía] Repite el bloque mientras se cumpla `upper - lower < 4`.
    while upper - lower < 4:
        # [Guía] Evalúa `lower < 0 and upper == 0` antes de ejecutar las instrucciones de esta rama.
        if lower < 0 and upper == 0:
            # [Guía] Combina el valor anterior de `lower` con `1` mediante esta operación.
            lower -= 1
        # [Guía] Evalúa `lower < 0 and upper > 0 and (abs(lower) < upper)` antes de ejecutar las instrucciones de
        # [Guía] esta rama.
        elif lower < 0 and upper > 0 and abs(lower) < upper:
            # [Guía] Combina el valor anterior de `lower` con `1` mediante esta operación.
            lower -= 1
        # [Guía] Ejecuta este bloque cuando se cumple la alternativa del bloque anterior.
        else:
            # [Guía] Combina el valor anterior de `upper` con `1` mediante esta operación.
            upper += 1
    # [Guía] Conserva en `ticks` el resultado de `tuple` con los argumentos indicados.
    ticks = tuple(index * step for index in range(lower, upper + 1))
    # [Guía] Entrega (ticks[0], ticks[-1], ticks) al llamador y termina la función.
    return ticks[0], ticks[-1], ticks


# [Guía] Quitar decimales innecesarios:5,10,15; conservar7.5o0.5 cuando toca. Recibe `number`.
def axis_label(number):
    """Quitar decimales innecesarios:5,10,15; conservar7.5o0.5 cuando toca."""
    # [Guía] Entrega f'{number:.6f}'.rstrip('0').rstrip('.') if number else '0' al llamador y termina la función.
    return f'{number:.6f}'.rstrip('0').rstrip('.') if number else '0'


# [Guía] Agrupa en PlotWindow el estado y las operaciones de esta parte de la aplicación.
class PlotWindow:
    # [Guía] Construye la ventana de nueve gráficas y conecta los lienzos con el historial y el selector de
    # [Guía] ventana temporal. Recibe `root`, `history`.
    def __init__(self, root, history):
        # [Guía] Establece `self.history` a partir de `history`.
        self.history = history
        # [Guía] Conserva en `self.window` el resultado de `tk.Toplevel` con los argumentos indicados.
        self.window = tk.Toplevel(root)
        # [Guía] Ejecuta self.window.title con los argumentos de esta llamada.
        self.window.title('Variables del convertidor multipuerto')
        # [Guía] Ejecuta self.window.geometry con los argumentos de esta llamada.
        self.window.geometry('1380x900')
        # [Guía] Ejecuta self.window.minsize con los argumentos de esta llamada.
        self.window.minsize(1080, 740)
        # [Guía] Ejecuta self.window.configure con los argumentos de esta llamada.
        self.window.configure(bg='#f4f6f8')
        # [Guía] Ejecuta self.window.protocol con los argumentos de esta llamada.
        self.window.protocol('WM_DELETE_WINDOW', self.window.withdraw)
        # [Guía] Conserva en `outer` el resultado de `ttk.Frame` con los argumentos indicados.
        outer = ttk.Frame(self.window, padding=16)
        # [Guía] Ejecuta outer.pack con los argumentos de esta llamada.
        outer.pack(fill='both', expand=True)
        # [Guía] Recorre range(3). En cada iteración, column recibe el elemento correspondiente.
        for column in range(3):
            # [Guía] Ejecuta outer.columnconfigure con los argumentos de esta llamada.
            outer.columnconfigure(column, weight=1, uniform='ports')
        # [Guía] Conserva en `top` el resultado de `ttk.Frame` con los argumentos indicados.
        top = ttk.Frame(outer)
        # [Guía] Ejecuta top.grid con los argumentos de esta llamada.
        top.grid(row=0, column=0, columnspan=3, sticky='ew', pady=(0, 8))
        # [Guía] Conserva en `self.heading` el resultado de `ttk.Label` con los argumentos indicados.
        self.heading = ttk.Label(top, text='Tensión, corriente y potencia de las variables', font=('Segoe UI', 17, 'bold'))
        # [Guía] Ejecuta self.heading.pack con los argumentos de esta llamada.
        self.heading.pack(side='left')
        # [Guía] Conserva en `self.span` el resultado de `tk.StringVar` con los argumentos indicados.
        self.span = tk.StringVar(value='2 min')
        # [Guía] Conserva en `self.span_box` el resultado de `ttk.Combobox` con los argumentos indicados.
        self.span_box = ttk.Combobox(top, textvariable=self.span, values=(*TIME_WINDOWS, 'Toda la vista'),
                                     # [Guía] Entrega el argumento con nombre `state` para configurar esta
                                     # [Guía] llamada.
                                     state='readonly', width=14)
        # [Guía] Ejecuta self.span_box.pack con los argumentos de esta llamada.
        self.span_box.pack(side='right')
        # [Guía] Ejecuta ttk.Label(top, text='Ventana: ').pack con los argumentos de esta llamada.
        ttk.Label(top, text='Ventana: ').pack(side='right')
        # Cada puerto conserva columna/color; no mezclar sus escalas ni sus
        # valores con los de otro puerto. Los nueve ejes comparten solo tiempo.
        # [Guía] Recorre enumerate(CHANNELS). En cada iteración, (column, channel) recibe el elemento
        # [Guía] correspondiente.
        for column, channel in enumerate(CHANNELS):
            # [Guía] Ejecuta ttk.Label(outer, text=NAMES[channel], foreground=COLORS[channel], anchor='center',
            # [Guía] font=('Segoe UI', 13, 'bold'… con los argumentos de esta llamada.
            ttk.Label(outer, text=NAMES[channel], foreground=COLORS[channel], anchor='center',
                      # [Guía] Entrega el argumento con nombre `font` para configurar esta llamada.
                      font=('Segoe UI', 13, 'bold')).grid(row=1, column=column, sticky='ew', pady=(0, 10))
        # [Guía] Establece `self.canvases` a partir de `{}`.
        self.canvases = {}
        # [Guía] Recorre enumerate(zip(KINDS, ('Tensión (V)', 'Corriente (A)', 'Potencia (W)')), 2). En cada
        # [Guía] iteración, (row, (kind, title)) recibe el elemento correspondiente.
        for row, (kind, title) in enumerate(zip(KINDS, ('Tensión (V)', 'Corriente (A)', 'Potencia (W)')), 2):
            # [Guía] Ejecuta outer.rowconfigure con los argumentos de esta llamada.
            outer.rowconfigure(row, weight=1)
            # [Guía] Recorre enumerate(CHANNELS). En cada iteración, (column, channel) recibe el elemento
            # [Guía] correspondiente.
            for column, channel in enumerate(CHANNELS):
                # [Guía] Conserva en `canvas` el resultado de `tk.Canvas` con los argumentos indicados.
                canvas = tk.Canvas(outer, bg='white', highlightthickness=1,
                                   # [Guía] Entrega el argumento con nombre `highlightbackground` para configurar
                                   # [Guía] esta llamada.
                                   highlightbackground='#dae2e6', height=180, width=300)
                # [Guía] Ejecuta canvas.grid con los argumentos de esta llamada.
                canvas.grid(row=row, column=column, sticky='nsew',
                            # [Guía] Entrega el argumento con nombre `padx` para configurar esta llamada.
                            padx=(0 if column == 0 else 5, 0 if column == 2 else 5), pady=(0, 8))
                # [Guía] Establece `self.canvases[kind, channel]` a partir de `(canvas, title)`.
                self.canvases[(kind, channel)] = (canvas, title)
        # [Guía] Conserva en `self.status` el resultado de `ttk.Label` con los argumentos indicados.
        self.status = ttk.Label(outer, text='Esperando conexión', wraplength=1100)
        # [Guía] Ejecuta self.status.grid con los argumentos de esta llamada.
        self.status.grid(row=5, column=0, columnspan=3, sticky='w')
        # [Guía] Conserva en `self.note` el resultado de `ttk.Label` con los argumentos indicados.
        self.note = ttk.Label(outer, text='Vista V/I/P suavizada · Tiempo en segundos desde Conectar · 5 Hz nominal · vista 30 min · CSV completo sin filtro visual · Batería: corriente y potencia negativas = carga',
                              # [Guía] Entrega el argumento con nombre `font` para configurar esta llamada.
                              font=('Segoe UI', 9), wraplength=1100)
        # [Guía] Ejecuta self.note.grid con los argumentos de esta llamada.
        self.note.grid(row=6, column=0, columnspan=3, sticky='w', pady=(4, 0))
        # [Guía] Ejecuta self.window.bind con los argumentos de esta llamada.
        self.window.bind('<Configure>', self._resize)

    # [Guía] Marca el lienzo para redibujarlo con las dimensiones nuevas. Recibe `event`.
    def _resize(self, event):
        # [Guía] Evalúa `event.widget == self.window` antes de ejecutar las instrucciones de esta rama.
        if event.widget == self.window:
            # [Guía] Recorre (self.status, self.note). En cada iteración, label recibe el elemento
            # [Guía] correspondiente.
            for label in (self.status, self.note):
                # [Guía] Ejecuta label.configure con los argumentos de esta llamada.
                label.configure(wraplength=max(300, event.width - 32))

    # [Guía] Presenta la ventana de gráficas y la trae al frente de la aplicación.
    def show(self):
        # [Guía] Ejecuta self.window.deiconify con los argumentos de esta llamada.
        self.window.deiconify()
        # [Guía] Ejecuta self.window.lift con los argumentos de esta llamada.
        self.window.lift()

    # [Guía] Dibuja el estado de este módulo. Recibe `now`.
    def render(self, now):
        # [Guía] Evalúa `not self.window.winfo_exists() or not self.window.winfo_viewable()` antes de ejecutar las
        # [Guía] instrucciones de esta rama.
        if not self.window.winfo_exists() or not self.window.winfo_viewable():
            # [Guía] Termina la función en este punto.
            return
        # [Guía] Conserva en `elapsed` el resultado de `self.history.elapsed` con los argumentos indicados.
        elapsed = self.history.elapsed(now)
        # [Guía] Establece `first` a partir de `self.history.points[0].elapsed if self.history.points else 0`.
        first = self.history.points[0].elapsed if self.history.points else 0
        # [Guía] Conserva en `(start, end, ticks)` el resultado de `time_axis` con los argumentos indicados.
        start, end, ticks = time_axis(elapsed, self.span.get(), first)
        # [Guía] Establece `points` a partir de `[point for point in self.history.points if point.elapsed >=
        # [Guía] start]`.
        points = [point for point in self.history.points if point.elapsed >= start]
        # [Guía] Recorre self.canvases.items(). En cada iteración, ((kind, channel), (canvas, title)) recibe el
        # [Guía] elemento correspondiente.
        for (kind, channel), (canvas, title) in self.canvases.items():
            # [Guía] Ejecuta self._draw con los argumentos de esta llamada.
            self._draw(canvas, title, kind, channel, points, start, end, ticks)
        # [Guía] Establece `state` a partir de `'SIN CONEXIÓN' if self.history.started_at is None else
        # [Guía] 'CAPTURANDO' if self.history.stopped_at is None else 'DE…`.
        state = 'SIN CONEXIÓN' if self.history.started_at is None else (
            # [Guía] Continúa la instrucción con `'CAPTURANDO' if self.history.stopped_at is None else 'DETENIDA ·
            # [Guía] datos conservados')`.
            'CAPTURANDO' if self.history.stopped_at is None else 'DETENIDA · datos conservados')
        # [Guía] Establece `power` a partir de `self.history.points[-1].power_source if self.history.points else
        # [Guía] 'sin datos'`.
        power = self.history.points[-1].power_source if self.history.points else 'sin datos'
        # [Guía] Ejecuta self.status.configure con los argumentos de esta llamada.
        self.status.configure(text=f'{state} · {elapsed:.1f} s · {len(self.history.points)} muestras · Potencia: {power} · cerrar esta ventana no detiene la captura')

    # [Guía] Dibuja una gráfica de la cuadrícula con ejes, marcas y segmentos de datos, convirtiendo las unidades
    # [Guía] a coordenadas de píxel. Recibe `canvas`, `title`, `kind`, `channel`, `points`, `start`, `end`,
    # [Guía] `time_ticks`.
    @staticmethod
    def _draw(canvas, title, kind, channel, points, start, end, time_ticks):
        # [Guía] Ejecuta canvas.delete con los argumentos de esta llamada.
        canvas.delete('all')
        # [Guía] Establece `(width, height)` a partir de `(max(200, canvas.winfo_width()), max(90,
        # [Guía] canvas.winfo_height()))`.
        width, height = max(200, canvas.winfo_width()), max(90, canvas.winfo_height())
        # [Guía] Establece `(left, right, top, bottom)` a partir de `(57, width - 16, 32, height - 32)`.
        left, right, top, bottom = 57, width - 16, 32, height - 32
        # [Guía] Establece `values` a partir de `[point.value(kind, channel) for point in points if
        # [Guía] point.value(kind, channel) is not None]`.
        values = [point.value(kind, channel) for point in points
                  # [Guía] Continúa la instrucción con `if point.value(kind, channel) is not None]`.
                  if point.value(kind, channel) is not None]
        # [Guía] Conserva en `(low, high, y_ticks)` el resultado de `vertical_axis` con los argumentos indicados.
        low, high, y_ticks = vertical_axis(values, kind, channel)
        # [Guía] Convierte un instante en segundos a su posición horizontal dentro del lienzo. Recibe `value`.
        def x(value):
            # [Guía] Entrega left + (value - start) / (end - start) * (right - left) al llamador y termina la
            # [Guía] función.
            return left + (value - start) / (end - start) * (right - left)
        # [Guía] Convierte una magnitud de la serie a píxeles verticales. El eje del lienzo crece hacia abajo.
        # [Guía] Recibe `value`.
        def y(value):
            # [Guía] Entrega bottom - (value - low) / (high - low) * (bottom - top) al llamador y termina la
            # [Guía] función.
            return bottom - (value - low) / (high - low) * (bottom - top)
        # [Guía] Ejecuta canvas.create_text con los argumentos de esta llamada.
        canvas.create_text(12, 13, text=title, anchor='w', fill='#24383f', font=('Segoe UI', 10, 'bold'))
        # [Guía] Recorre y_ticks. En cada iteración, value recibe el elemento correspondiente.
        for value in y_ticks:
            # [Guía] Dibuja la guía horizontal que corresponde a la marca de tensión, corriente o potencia.
            canvas.create_line(left, y(value), right, y(value), fill='#e7edef', tags='y_grid')
            # [Guía] Ejecuta canvas.create_text con los argumentos de esta llamada.
            canvas.create_text(left - 8, y(value), text=axis_label(value), anchor='e',
                               # [Guía] Entrega el argumento con nombre `fill` para configurar esta llamada.
                               fill='#62747b', font=('Segoe UI', 8), tags='y_tick')
        # Una linea vertical por cada tiempo rotulado. Las marcas son enteras
        # y permanecen iguales en las9celdas, aun con escalas Y diferentes.
        # [Guía] Recorre time_ticks. En cada iteración, instant recibe el elemento correspondiente.
        for instant in time_ticks:
            # [Guía] Dibuja una guía vertical en la marca temporal para relacionar cambios entre los nueve
            # [Guía] paneles.
            canvas.create_line(x(instant), top, x(instant), bottom, fill='#e7edef', tags='x_grid')
            # Una marca cercana al borde puede llevar muchos digitos tras una
            # sesion larga. Desplazar solo lo imprescindible el texto hacia
            # dentro del Canvas; la linea conserva el instante exacto del eje.
            # [Guía] Establece `half_label` a partir de `(len(str(instant)) * 7 + 4) / 2`.
            half_label = (len(str(instant)) * 7 + 4) / 2
            # [Guía] Conserva en `label_x` el resultado de `min` con los argumentos indicados.
            label_x = min(max(x(instant), half_label + 2), width - half_label - 2)
            # [Guía] Ejecuta canvas.create_text con los argumentos de esta llamada.
            canvas.create_text(label_x, bottom + 14, text=str(instant), anchor='center',
                               # [Guía] Entrega el argumento con nombre `fill` para configurar esta llamada.
                               fill='#62747b', font=('Segoe UI', 8), tags='x_tick')
        # [Guía] Señala el cero con una línea discontinua para distinguir las magnitudes positivas y negativas.
        canvas.create_line(left, y(0), right, y(0), fill='#a3b5bd', dash=(3, 3))
        # [Guía] Recorre plot_segments(points, kind, channel). En cada iteración, segment recibe el elemento
        # [Guía] correspondiente.
        for segment in plot_segments(points, kind, channel):
            # [Guía] Establece `coords` a partir de `[coordinate for t, value in segment for coordinate in (x(t),
            # [Guía] y(value))]`.
            coords = [coordinate for t, value in segment for coordinate in (x(t), y(value))]
            # [Guía] Evalúa `len(coords) >= 4` antes de ejecutar las instrucciones de esta rama.
            if len(coords) >= 4:
                # [Guía] Traza un segmento continuo en el color del puerto. Los huecos se conservan al dibujar los
                # [Guía] segmentos por separado.
                canvas.create_line(*coords, fill=COLORS[channel], width=2, tags='trace')
            # [Guía] Evalúa `coords` antes de ejecutar las instrucciones de esta rama.
            elif coords:
                # [Guía] Establece `(px, py)` a partir de `coords`.
                px, py = coords
                # [Guía] Ejecuta canvas.create_oval con los argumentos de esta llamada.
                canvas.create_oval(px - 2, py - 2, px + 2, py + 2,
                                   # [Guía] Entrega el argumento con nombre `fill` para configurar esta llamada.
                                   fill=COLORS[channel], outline='', tags='trace')
        # [Guía] Evalúa `not values` antes de ejecutar las instrucciones de esta rama.
        if not values:
            # [Guía] Ejecuta canvas.create_text con los argumentos de esta llamada.
            canvas.create_text((left + right) / 2, (top + bottom) / 2,
                               # [Guía] Entrega el argumento con nombre `text` para configurar esta llamada.
                               text='Esperando datos válidos', fill='#7b8d94', font=('Segoe UI', 10))
