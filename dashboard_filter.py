# [Guía] Suaviza la presentación usando el intervalo real entre muestras. El historial y el CSV se alimentan antes
# [Guía] de este filtro visual.
"""Suavizar exclusivamente V/I/P de pantalla, sin alterar CSV ni control.

El filtro recibe puntos ya fechados: no avanza con cada repintado de Tk.
Usa el tiempo real entre muestras, conserva signos y reinicia cada canal
invalido. Al cambiar de modo o perder telemetria no arrastra la memoria vieja.
"""
# [Guía] Importa math para las operaciones que realiza este módulo.
import math

# [Guía] Importa HistoryPoint para las operaciones que realiza este módulo.
from dashboard_history import HistoryPoint

# Constante de tiempo en segundos: MAYOR = mas suavizado y mas retardo visual.
# Con 0.30s, un escalon estable alcanza95% en~0.90s. Poner0 para desactivar.
# No tiene relacion con CURRENT_ALPHA/VOLTAGE_ALPHA del Arduino.
# [Guía] Constante de tiempo del filtro visual en segundos. Aumentarla suaviza más la gráfica y retrasa su
# [Guía] respuesta a cambios rápidos.
DISPLAY_FILTER_TAU_S = 0.30
# [Guía] Si transcurre más de este intervalo, la siguiente medida inicializa el filtro visual desde el valor
# [Guía] recién recibido.
DISPLAY_FILTER_MAX_GAP_S = 0.75


# [Guía] Agrupa en DisplayFilter el estado y las operaciones de esta parte de la aplicación.
class DisplayFilter:
    """Un IIR por cada una de las nueve series; primera muestra sin rampa falsa."""
    # [Guía] Valida la constante de tiempo visual en segundos y deja la memoria de las series vacía hasta recibir
    # [Guía] la primera muestra. Recibe `tau`.
    def __init__(self, tau=DISPLAY_FILTER_TAU_S):
        # [Guía] Evalúa `not math.isfinite(tau) or tau < 0` antes de ejecutar las instrucciones de esta rama.
        if not math.isfinite(tau) or tau < 0:
            # [Guía] Señala ValueError('tau visual debe ser finito y >=0') para que la capa que llamó pueda tratar
            # [Guía] el problema.
            raise ValueError('tau visual debe ser finito y >=0')
        # [Guía] Establece `self.tau` a partir de `tau`.
        self.tau = tau
        # [Guía] Ejecuta self.reset con los argumentos de esta llamada.
        self.reset()

    # [Guía] Reinicia el estado de este módulo.
    def reset(self):
        # [Guía] Establece `self.last` a partir de `None`.
        self.last = None

    # [Guía] Calcula el peso del suavizado con el intervalo real. Reinicia el estado si cambió el contexto o dejó
    # [Guía] de ser válida una magnitud. Recibe `point`.
    def update(self, point):
        # [Guía] Recupera el último punto ya filtrado. Sus valores válidos actúan como memoria de las nueve
        # [Guía] series.
        previous = self.last
        # No mezclar estados operativos ni interpolar a traves de un hueco.
        # [Guía] Inicializa desde la nueva medida al abrir la sesión o cambiar el modo de operación.
        reset = (previous is None or point.mode != previous.mode
                 # [Guía] También reinicia si cambió la procedencia de los valores de potencia para evitar mezclar
                 # [Guía] datos de fuentes distintas.
                 or point.power_source != previous.power_source)
        # [Guía] Calcula el intervalo real en segundos entre puntos de la sesión.
        dt = point.elapsed - previous.elapsed if previous is not None else 0
        # [Guía] Reinicia si el tiempo retrocedió, se repitió o hay un hueco mayor que el admitido.
        reset = reset or dt <= 0 or dt > DISPLAY_FILTER_MAX_GAP_S
        # [Guía] Calcula 1 menos la exponencial de menos dt sobre tau. El peso aumenta con la separación entre
        # [Guía] muestras y vale uno al reiniciar.
        alpha = 1.0 if reset or self.tau == 0 else -math.expm1(-dt / self.tau)
        # [Guía] Prepara el vector que reunirá las nueve magnitudes suavizadas en el orden de la muestra.
        values = []
        # [Guía] Recorre enumerate(point.values). En cada iteración, (index, number) recibe el elemento
        # [Guía] correspondiente.
        for index, number in enumerate(point.values):
            # None/NaN nunca significa cero: invalidar ahora y reiniciar ese
            # canal cuando vuelva una muestra valida. No conservar una cifra vieja.
            # [Guía] Obtiene el estado del mismo canal en el punto anterior, si existe.
            old = previous.values[index] if previous is not None else None
            # [Guía] Evalúa `number is None or not math.isfinite(number)` antes de ejecutar las instrucciones de
            # [Guía] esta rama.
            if number is None or not math.isfinite(number):
                # [Guía] Conserva explícitamente un hueco para este canal. Esto permite interrumpir el trazo de
                # [Guía] una medida inválida.
                values.append(None)
            # [Guía] Evalúa `reset or old is None` antes de ejecutar las instrucciones de esta rama.
            elif reset or old is None:
                # [Guía] Toma la lectura actual directamente al reiniciar o al recuperar un canal que no tenía
                # [Guía] historial válido.
                values.append(number)
            # [Guía] Ejecuta este bloque cuando se cumple la alternativa del bloque anterior.
            else:
                # [Guía] Mueve la salida anterior una fracción de la distancia a la lectura nueva. La operación
                # [Guía] conserva el signo físico de las medidas.
                values.append(old + alpha * (number - old))
        # [Guía] Crea el punto filtrado con el mismo tiempo, modo y procedencia que la muestra. Lo conserva como
        # [Guía] memoria para el siguiente punto.
        self.last = HistoryPoint(point.elapsed, tuple(values), point.mode, point.power_source)
        # [Guía] Entrega self.last al llamador y termina la función.
        return self.last
