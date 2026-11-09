# [Guía] Traduce telemetría textual en valores con unidades y estado de presentación. Mantiene tiempos de vigencia
# [Guía] de los grupos de datos.
"""Datos del dashboard, separados de Tk y de los puertos fisicos.

Este modulo no abre COM ni envia ordenes. Traduce las mismas lineas ASCII
del firmware V39 que recibe control_bk8502.py. Los valores ausentes se
representan con None: cero seria una medida valida, no ausencia de medida.
Cada grupo tiene su propio reloj para no presentar un SOC o ACK viejo como
si acabara de llegar porque otro grupo sigue enviando datos.
"""
# [Guía] Importa dataclass, field para las operaciones que realiza este módulo.
from dataclasses import dataclass, field
# [Guía] Importa re para las operaciones que realiza este módulo.
import re
# [Guía] Importa time para las operaciones que realiza este módulo.
import time

# [Guía] Importa connection_display, controller_display, emulator_display, ems_display, mppt_display,
# [Guía] observation_display, soc_display, telemetry, trip_display para las operaciones que realiza este módulo.
from control_bk8502 import (
    # [Guía] Continúa la instrucción con `connection_display, controller_display, emulator_display, ems_display`.
    connection_display, controller_display, emulator_display, ems_display,
    # [Guía] Continúa la instrucción con `mppt_display, observation_display, soc_display, telemetry,
    # [Guía] trip_display`.
    mppt_display, observation_display, soc_display, telemetry, trip_display,
)


# [Guía] Separa los campos clave-valor de una línea de telemetría para interpretarlos por nombre. Recibe `line`.
def fields_of(line):
    """Separar claves repetibles de distintos tipos de mensaje, no entre canales."""
    # [Guía] Entrega dict(re.findall('\\b([A-Za-z_][A-Za-z_0-9]*)=([^\\s|]+)', line)) al llamador y termina la
    # [Guía] función.
    return dict(re.findall(r"\b([A-Za-z_][A-Za-z_0-9]*)=([^\s|]+)", line))


# [Guía] Convierte un entero escalado del protocolo a la unidad presentada en pantalla. Recibe `fields`, `key`,
# [Guía] `divisor`.
def scaled(fields, key, divisor=1000.0):
    """Convertir enteros de transporte; un numero mal formado no inventa un cero."""
    # [Guía] Delimita la operación cuyos errores o cierre se gestionan en las ramas siguientes.
    try:
        # [Guía] Entrega int(fields[key]) / divisor al llamador y termina la función.
        return int(fields[key]) / divisor
    # [Guía] Atiende las excepciones que coinciden con `except (KeyError, ValueError, TypeError)` en el bloque
    # [Guía] anterior.
    except (KeyError, ValueError, TypeError):
        # [Guía] Entrega None al llamador y termina la función.
        return None


# [Guía] Asocia el identificador EMS a la descripción del flujo de energía que verá el usuario. Recibe `mode`.
def operation_label(mode):
    """Texto de supervision: no decide modos ni habilita PWM."""
    # [Guía] Establece `descriptions` a partir de `{'M1': 'Panel a bus DC', 'M2': 'Panel a batería', 'M3': 'Panel
    # [Guía] a bus DC y batería', 'M4': 'Panel y batería a bu…`.
    descriptions = {
        # [Guía] Añade la entrada 'M1' a este mapa de datos.
        'M1': 'Panel a bus DC', 'M2': 'Panel a batería',
        # [Guía] Añade la entrada 'M3' a este mapa de datos.
        'M3': 'Panel a bus DC y batería', 'M4': 'Panel y batería a bus DC',
        # [Guía] Añade la entrada 'M5' a este mapa de datos.
        'M5': 'Batería a bus DC', 'WAIT': 'Espera de fuente',
        # [Guía] Añade la entrada 'START_M1' a este mapa de datos.
        'START_M1': 'Arranque: panel a bus DC', 'START_M4': 'Conexión de fuentes',
        # [Guía] Añade la entrada 'START_M5' a este mapa de datos.
        'START_M5': 'Arranque: batería a bus DC', 'TRANSFER': 'Transferencia de control',
        # [Guía] Añade la entrada 'FAULT' a este mapa de datos.
        'FAULT': 'Fallo de protección',
    }
    # [Guía] Entrega f'{mode} ({descriptions[mode]})' if mode in descriptions else mode al llamador y termina la
    # [Guía] función.
    return f'{mode} ({descriptions[mode]})' if mode in descriptions else mode


@dataclass
# [Guía] Agrupa en DashboardState el estado y las operaciones de esta parte de la aplicación.
class DashboardState:
    """Ultima instantanea recibida; solo el hilo de Tk modifica este objeto."""
    # default_factory crea objetos separados por sesion; un diccionario mutable
    # compartido como valor por defecto mezclaria los datos entre ventanas/tests.
    # [Guía] Declara voltage con el tipo dict. Su valor inicial es field(default_factory=lambda:
    # [Guía] dict.fromkeys(('BUS', 'PV', 'BAT'))).
    voltage: dict = field(default_factory=lambda: dict.fromkeys(('BUS', 'PV', 'BAT')))
    # [Guía] Declara current con el tipo dict. Su valor inicial es field(default_factory=lambda:
    # [Guía] dict.fromkeys(('BUS', 'PV', 'BAT'))).
    current: dict = field(default_factory=lambda: dict.fromkeys(('BUS', 'PV', 'BAT')))
    # [Guía] Declara power con el tipo dict. Su valor inicial es field(default_factory=lambda:
    # [Guía] dict.fromkeys(('BUS', 'PV', 'BAT'))).
    power: dict = field(default_factory=lambda: dict.fromkeys(('BUS', 'PV', 'BAT')))
    # [Guía] Declara power_source con el tipo str. Su valor inicial es 'SIN_DATOS'.
    power_source: str = 'SIN_DATOS'
    # [Guía] Declara duty con el tipo list. Su valor inicial es field(default_factory=lambda: [None, None, None]).
    duty: list = field(default_factory=lambda: [None, None, None])
    # [Guía] Declara mode con el tipo str. Su valor inicial es '--'.
    mode: str = '--'
    # [Guía] Declara reason con el tipo str. Su valor inicial es 'SIN_DATOS'.
    reason: str = 'SIN_DATOS'
    # [Guía] Declara m2_active con el tipo bool. Su valor inicial es False.
    m2_active: bool = False
    # [Guía] Declara charge_current_reference con el tipo object. Su valor inicial es None.
    charge_current_reference: object = None
    # [Guía] Declara version con el tipo str. Su valor inicial es '--'.
    version: str = '--'
    # [Guía] Declara fault con el tipo bool. Su valor inicial es False.
    fault: bool = False
    # [Guía] Declara mppt con el tipo object. Su valor inicial es None.
    mppt: object = None
    # [Guía] Declara mppt_decision con el tipo str. Su valor inicial es '--'.
    mppt_decision: str = '--'
    # [Guía] Declara soc con el tipo object. Su valor inicial es None.
    soc: object = None
    # [Guía] Declara soc_state con el tipo str. Su valor inicial es 'SIN_DATOS'.
    soc_state: str = 'SIN_DATOS'
    # [Guía] Declara soc_continuous con el tipo bool. Su valor inicial es False.
    soc_continuous: bool = False
    # [Guía] Declara resistance con el tipo object. Su valor inicial es None.
    resistance: object = None
    # [Guía] Declara load con el tipo object. Su valor inicial es None.
    load: object = None
    # [Guía] Declara linked con el tipo bool. Su valor inicial es False.
    linked: bool = False
    # [Guía] Declara emulator_state con el tipo str. Su valor inicial es 'SIN_DATOS'.
    emulator_state: str = 'SIN_DATOS'
    # [Guía] Declara stamps con el tipo dict. Su valor inicial es field(default_factory=dict).
    stamps: dict = field(default_factory=dict)

    # [Guía] Caducar cada grupo por tiempo monotono; no depende de la hora del PC. Recibe `group`, `now`,
    # [Guía] `timeout`.
    def fresh(self, group, now=None, timeout=2.5):
        """Caducar cada grupo por tiempo monotono; no depende de la hora del PC."""
        # [Guía] Establece `now` a partir de `time.monotonic() if now is None else now`.
        now = time.monotonic() if now is None else now
        # [Guía] Un grupo solo es vigente si ya recibió datos y su edad queda dentro del límite indicado.
        return group in self.stamps and now - self.stamps[group] <= timeout

    # [Guía] Procesa una línea del puente según su tipo. Actualiza el grupo de telemetría correspondiente, su
    # [Guía] validez y su instante de recepción. Recibe `line`, `now`.
    def consume(self, line, now=None):
        """Aplicar una linea completa. Devuelve False si no es telemetria conocida.

        Potencia V62 viene de media(V*I) del Arduino. Con firmware antiguo se
        muestra Vmedia*Imedia como aproximacion, identificada en la ventana.
        SOC viene del Arduino. La resistencia mostrada es el setpoint releido,
        no Req_total, y LOAD es una orden aceptada, no potencia comprobada.
        """
        # [Guía] Establece `now` a partir de `time.monotonic() if now is None else now`.
        now = time.monotonic() if now is None else now
        # Permite reproducir tambien el archivo --log con prefijo ISO UTC.
        # [Guía] Conserva en `line` el resultado de `re.sub` con los argumentos indicados.
        line = re.sub(r'^\d{4}-\d\d-\d\dT\S+\s+', '', line.strip())
        # [Guía] Conserva en `fields` el resultado de `fields_of` con los argumentos indicados.
        fields = fields_of(line)
        # [Guía] Evalúa `line.startswith('LIVE ')` antes de ejecutar las instrucciones de esta rama.
        if line.startswith('LIVE '):
            # V57: una fotografia corta lleva todos los grupos visibles. No
            # esperar el diagnostico largo para repintar y no aceptar una linea
            # parcial como si renovara medidas/SOC/ACK que realmente no llegaron.
            # [Guía] Conserva en `required` el resultado de `'PV BUS BAT BAT_on IPV IBUS IBAT I_ok PROFILE MODE
            # [Guía] STATE D1_permille D2_permille D3_permille MPPT FAULT VERSION…` con los argumentos indicados.
            required = ('PV BUS BAT BAT_on IPV IBUS IBAT I_ok PROFILE MODE STATE '
                        # [Guía] Continúa la instrucción con `'D1_permille D2_permille D3_permille MPPT FAULT
                        # [Guía] VERSION '`.
                        'D1_permille D2_permille D3_permille MPPT FAULT VERSION '
                        # [Guía] Continúa la instrucción con `'SOC_milli SOC_ok CONTINUOUS SOC_STATE LINK
                        # [Guía] R_ack_mOhm '`.
                        'SOC_milli SOC_ok CONTINUOUS SOC_STATE LINK R_ack_mOhm '
                        # [Guía] Continúa la instrucción con `'R_ok LOAD_ack LOAD_ok BK_STATE').split()`.
                        'R_ok LOAD_ack LOAD_ok BK_STATE').split()
            # [Guía] Evalúa `not all((key in fields for key in required)) or fields['PROFILE'] != '1'` antes de
            # [Guía] ejecutar las instrucciones de esta rama.
            if not all(key in fields for key in required) or fields['PROFILE'] != '1':
                # [Guía] Entrega False al llamador y termina la función.
                return False
            # [Guía] Conserva en `valid` el resultado de `fields['I_ok'].split` con los argumentos indicados.
            valid = fields['I_ok'].split(',')
            # [Guía] Recorre enumerate((('PV', 'IPV'), ('BUS', 'IBUS'), ('BAT', 'IBAT'))). En cada iteración,
            # [Guía] (index, (channel, key)) recibe el elemento correspondiente.
            for index, (channel, key) in enumerate((('PV', 'IPV'), ('BUS', 'IBUS'), ('BAT', 'IBAT'))):
                # [Guía] Conserva en `self.voltage[channel]` el resultado de `scaled` con los argumentos
                # [Guía] indicados.
                self.voltage[channel] = scaled(fields, channel)
                # [Guía] Establece `self.current[channel]` a partir de `scaled(fields, key) if index < len(valid)
                # [Guía] and valid[index] == '1' else None`.
                self.current[channel] = scaled(fields, key) if index < len(valid) and valid[index] == '1' else None
            # [Guía] Evalúa `fields['BAT_on'] != '1'` antes de ejecutar las instrucciones de esta rama.
            if fields['BAT_on'] != '1':
                # [Guía] Establece `self.voltage['BAT']` a partir de `None`.
                self.voltage['BAT'] = None
            # [Guía] Ejecuta self._powers con los argumentos de esta llamada.
            self._powers(fields, now)
            # [Guía] Establece `(self.mode, self.reason, self.version)` a partir de `(fields['MODE'],
            # [Guía] fields['STATE'], fields['VERSION'])`.
            self.mode, self.reason, self.version = fields['MODE'], fields['STATE'], fields['VERSION']
            # [Guía] Ejecuta self._m2 con los argumentos de esta llamada.
            self._m2(fields)
            # [Guía] Establece `self.duty` a partir de `[scaled(fields, f'D{n}_permille', 10) for n in (1, 2,
            # [Guía] 3)]`.
            self.duty = [scaled(fields, f'D{n}_permille', 10) for n in (1, 2, 3)]
            # [Guía] Establece `self.duty` a partir de `[v if v is not None and 0 <= v <= 100 else None for v in
            # [Guía] self.duty]`.
            self.duty = [v if v is not None and 0 <= v <= 100 else None for v in self.duty]
            # [Guía] Conserva en `self.mppt` el resultado de `{'0': False, '1': True}.get` con los argumentos
            # [Guía] indicados.
            self.mppt = {'0': False, '1': True}.get(fields['MPPT'])
            # [Guía] Establece `self.fault` a partir de `fields['FAULT'] == '1'`.
            self.fault = fields['FAULT'] == '1'
            # [Guía] Conserva en `soc` el resultado de `scaled` con los argumentos indicados.
            soc = scaled(fields, 'SOC_milli')
            # [Guía] Establece `self.soc` a partir de `soc if fields['SOC_ok'] == '1' and soc is not None and (0
            # [Guía] <= soc <= 100) else None`.
            self.soc = soc if fields['SOC_ok'] == '1' and soc is not None and 0 <= soc <= 100 else None
            # [Guía] Establece `(self.soc_state, self.soc_continuous)` a partir de `(fields['SOC_STATE'],
            # [Guía] fields['CONTINUOUS'] == '1')`.
            self.soc_state, self.soc_continuous = fields['SOC_STATE'], fields['CONTINUOUS'] == '1'
            # [Guía] Establece `self.linked` a partir de `fields['LINK'] == '1'`.
            self.linked = fields['LINK'] == '1'
            # [Guía] Establece `self.resistance` a partir de `scaled(fields, 'R_ack_mOhm') if self.linked and
            # [Guía] fields['R_ok'] == '1' else None`.
            self.resistance = scaled(fields, 'R_ack_mOhm') if self.linked and fields['R_ok'] == '1' else None
            # [Guía] Establece `self.load` a partir de `{'0': False, '1': True}.get(fields['LOAD_ack']) if
            # [Guía] self.linked and fields['LOAD_ok'] == '1' else None`.
            self.load = {'0': False, '1': True}.get(fields['LOAD_ack']) if self.linked and fields['LOAD_ok'] == '1' else None
            # [Guía] Establece `self.emulator_state` a partir de `fields['BK_STATE']`.
            self.emulator_state = fields['BK_STATE']
            # [Guía] Recorre ('measure', 'ems', 'soc', 'bk'). En cada iteración, group recibe el elemento
            # [Guía] correspondiente.
            for group in ('measure', 'ems', 'soc', 'bk'):
                # [Guía] Establece `self.stamps[group]` a partir de `now`.
                self.stamps[group] = now
        # [Guía] Evalúa `line.startswith('V_mV ')` antes de ejecutar las instrucciones de esta rama.
        elif line.startswith('V_mV '):
            # [Guía] Conserva en `match` el resultado de `re.search` con los argumentos indicados.
            match = re.search(r'V_mV PV=(-?\d+) BUS=(-?\d+) BAT=(-?\d+).*I_mA PV=(-?\d+) BUS=(-?\d+) BAT=(-?\d+)', line)
            # [Guía] Evalúa `not match` antes de ejecutar las instrucciones de esta rama.
            if not match:
                # [Guía] Entrega False al llamador y termina la función.
                return False
            # [Guía] Establece `values` a partir de `[int(value) / 1000 for value in match.groups()]`.
            values = [int(value) / 1000 for value in match.groups()]
            # I_ok informa validez/calibracion, no fuente conectada. Preservar
            # corriente negativa: representa sentido segun CURRENT_SIGN de Config.
            # [Guía] Conserva en `valid` el resultado de `fields.get('I_ok', '0,0,0').split` con los argumentos
            # [Guía] indicados.
            valid = fields.get('I_ok', '0,0,0').split(',')
            # [Guía] Recorre enumerate(('PV', 'BUS', 'BAT')). En cada iteración, (index, channel) recibe el
            # [Guía] elemento correspondiente.
            for index, channel in enumerate(('PV', 'BUS', 'BAT')):
                # [Guía] Establece `self.voltage[channel]` a partir de `values[index]`.
                self.voltage[channel] = values[index]
                # [Guía] Establece `self.current[channel]` a partir de `values[index + 3] if index < len(valid)
                # [Guía] and valid[index] == '1' else None`.
                self.current[channel] = values[index + 3] if index < len(valid) and valid[index] == '1' else None
            # [Guía] Evalúa `'(BAT_on=0)' in line` antes de ejecutar las instrucciones de esta rama.
            if '(BAT_on=0)' in line:
                # [Guía] Establece `self.voltage['BAT']` a partir de `None`.
                self.voltage['BAT'] = None
            # [Guía] Ejecuta self._powers con los argumentos de esta llamada.
            self._powers(fields, now)
            # [Guía] Establece `self.stamps['measure']` a partir de `now`.
            self.stamps['measure'] = now
        # [Guía] Evalúa `line.startswith('EMS ')` antes de ejecutar las instrucciones de esta rama.
        elif line.startswith('EMS '):
            # [Guía] Ejecuta self._m2 con los argumentos de esta llamada.
            self._m2(fields)
            # [Guía] Evalúa `fields.get('PROFILE') != '1'` antes de ejecutar las instrucciones de esta rama.
            if fields.get('PROFILE') != '1':
                # [Guía] Establece `(self.mode, self.reason)` a partir de `('LEGACY', 'EMS_DESHABILITADA')`.
                self.mode, self.reason = 'LEGACY', 'EMS_DESHABILITADA'
                # [Guía] Establece `self.duty` a partir de `[None, None, None]`.
                self.duty = [None, None, None]
                # [Guía] Establece `self.mppt` a partir de `None`.
                self.mppt = None
            # [Guía] Ejecuta este bloque cuando se cumple la alternativa del bloque anterior.
            else:
                # [Guía] Conserva en `self.mode` el resultado de `fields.get` con los argumentos indicados.
                self.mode = fields.get('MODE', '--')
                # [Guía] Conserva en `self.reason` el resultado de `fields.get` con los argumentos indicados.
                self.reason = fields.get('STATE', '--')
                # Dutytransportado=fraccion*1000: dividir por10 produce porcentaje,
                # no por1000 como tensiones/corrientes que pasan de mili a SI.
                # [Guía] Establece `self.duty` a partir de `[scaled(fields, f'D{n}_permille', 10) for n in (1, 2,
                # [Guía] 3)]`.
                self.duty = [scaled(fields, f'D{n}_permille', 10) for n in (1, 2, 3)]
                # [Guía] Establece `self.duty` a partir de `[value if value is not None and 0 <= value <= 100 else
                # [Guía] None for value in self.duty]`.
                self.duty = [value if value is not None and 0 <= value <= 100 else None for value in self.duty]
                # [Guía] Conserva en `self.mppt` el resultado de `{'0': False, '1': True}.get` con los argumentos
                # [Guía] indicados.
                self.mppt = {'0': False, '1': True}.get(fields.get('MPPT'))
            # [Guía] Establece `self.fault` a partir de `fields.get('FAULT') == '1'`.
            self.fault = fields.get('FAULT') == '1'
            # [Guía] Conserva en `self.version` el resultado de `fields.get` con los argumentos indicados.
            self.version = fields.get('VERSION', '--')
            # [Guía] Establece `self.stamps['ems']` a partir de `now`.
            self.stamps['ems'] = now
        # [Guía] Evalúa `line.startswith('SOC ')` antes de ejecutar las instrucciones de esta rama.
        elif line.startswith('SOC '):
            # [Guía] Conserva en `value` el resultado de `scaled` con los argumentos indicados.
            value = scaled(fields, 'PERCENT_milli')
            # [Guía] Establece `self.soc` a partir de `value if fields.get('INITIALIZED') == '1' and value is not
            # [Guía] None and (0 <= value <= 100) else None`.
            self.soc = value if fields.get('INITIALIZED') == '1' and value is not None and 0 <= value <= 100 else None
            # [Guía] Conserva en `self.soc_state` el resultado de `fields.get` con los argumentos indicados.
            self.soc_state = fields.get('STATE', '--')
            # [Guía] Establece `self.soc_continuous` a partir de `fields.get('CONTINUOUS') == '1'`.
            self.soc_continuous = fields.get('CONTINUOUS') == '1'
            # [Guía] Establece `self.stamps['soc']` a partir de `now`.
            self.stamps['soc'] = now
        # [Guía] Evalúa `line.startswith('BK_STATE ')` antes de ejecutar las instrucciones de esta rama.
        elif line.startswith('BK_STATE '):
            # ACK sin enlace actual no es confirmacion util. Invalidarlo aunque
            # queden campos antiguos con R_ok=1 en una trama de desconexion.
            # [Guía] Establece `self.linked` a partir de `fields.get('LINK') == '1'`.
            self.linked = fields.get('LINK') == '1'
            # [Guía] Establece `self.resistance` a partir de `scaled(fields, 'R_ack_mOhm') if self.linked and
            # [Guía] fields.get('R_ok') == '1' else None`.
            self.resistance = scaled(fields, 'R_ack_mOhm') if self.linked and fields.get('R_ok') == '1' else None
            # [Guía] Establece `self.load` a partir de `{'0': False, '1': True}.get(fields.get('LOAD_ack')) if
            # [Guía] self.linked and fields.get('LOAD_ok') == '1' else None`.
            self.load = {'0': False, '1': True}.get(fields.get('LOAD_ack')) if self.linked and fields.get('LOAD_ok') == '1' else None
            # [Guía] Conserva en `self.emulator_state` el resultado de `fields.get` con los argumentos indicados.
            self.emulator_state = fields.get('STATE', '--')
            # [Guía] Establece `self.stamps['bk']` a partir de `now`.
            self.stamps['bk'] = now
        # [Guía] Evalúa `line.startswith('MPPT_DIAG ')` antes de ejecutar las instrucciones de esta rama.
        elif line.startswith('MPPT_DIAG '):
            # [Guía] Conserva en `self.mppt_decision` el resultado de `fields.get` con los argumentos indicados.
            self.mppt_decision = fields.get('DECISION', '--')
            # [Guía] Establece `self.stamps['mppt']` a partir de `now`.
            self.stamps['mppt'] = now
        # [Guía] Ejecuta este bloque cuando se cumple la alternativa del bloque anterior.
        else:
            # [Guía] Entrega False al llamador y termina la función.
            return False
        # [Guía] Entrega True al llamador y termina la función.
        return True

    # [Guía] Referencia de corriente con signo: no confundir mA con V de bus. Recibe `fields`.
    def _m2(self, fields):
        """Referencia de corriente con signo: no confundir mA con V de bus."""
        # [Guía] Establece `self.m2_active` a partir de `fields.get('M2') == '1'`.
        self.m2_active = fields.get('M2') == '1'
        # [Guía] Establece `self.charge_current_reference` a partir de `scaled(fields, 'M2_IREF_mA') if
        # [Guía] self.m2_active else None`.
        self.charge_current_reference = scaled(fields, 'M2_IREF_mA') if self.m2_active else None

    # [Guía] Preservar signo/validez; negativo en bateria indica carga segun EMS. Recibe `fields`, `now`.
    def _powers(self, fields, now):
        """Preservar signo/validez; negativo en bateria indica carga segun EMS.

        Media(V*I) y media(V)*media(I) difieren con rizado. Nunca sobrescribir
        datos de potencia invalidos con el producto aproximado silenciosamente.
        """
        # [Guía] Conserva en `supplied` el resultado de `any` con los argumentos indicados.
        supplied = any(key in fields for key in ('PPV_mW', 'PBUS_mW', 'PBAT_mW'))
        # El reporte largo de diagnostico tambien lleva V/I y puede llegar
        # entre dos LIVE. No sustituir una potencia media reciente por el
        # producto de esos promedios ni renovar su reloj con otro grupo.
        # [Guía] Evalúa `not supplied and self.power_source == 'Media(V×I)' and self.fresh('power', now)` antes de
        # [Guía] ejecutar las instrucciones de esta rama.
        if not supplied and self.power_source == 'Media(V×I)' and self.fresh('power', now):
            # [Guía] Recorre ('PV', 'BUS', 'BAT'). En cada iteración, channel recibe el elemento correspondiente.
            for channel in ('PV', 'BUS', 'BAT'):
                # [Guía] Evalúa `self.voltage[channel] is None or self.current[channel] is None` antes de ejecutar
                # [Guía] las instrucciones de esta rama.
                if self.voltage[channel] is None or self.current[channel] is None:
                    # [Guía] Establece `self.power[channel]` a partir de `None`.
                    self.power[channel] = None
            # [Guía] Termina la función en este punto.
            return
        # [Guía] Establece `self.stamps['power']` a partir de `now`.
        self.stamps['power'] = now
        # [Guía] Establece `self.power_source` a partir de `'Media(V×I)' if supplied else 'V×I aproximada'`.
        self.power_source = 'Media(V×I)' if supplied else 'V×I aproximada'
        # [Guía] Conserva en `valid` el resultado de `fields.get('P_ok', '').split` con los argumentos indicados.
        valid = fields.get('P_ok', '').split(',')
        # [Guía] Recorre enumerate(('PV', 'BUS', 'BAT')). En cada iteración, (index, channel) recibe el elemento
        # [Guía] correspondiente.
        for index, channel in enumerate(('PV', 'BUS', 'BAT')):
            # [Guía] Establece `(voltage, current)` a partir de `(self.voltage[channel], self.current[channel])`.
            voltage, current = self.voltage[channel], self.current[channel]
            # [Guía] Evalúa `voltage is None or current is None` antes de ejecutar las instrucciones de esta rama.
            if voltage is None or current is None:
                # [Guía] Establece `self.power[channel]` a partir de `None`.
                self.power[channel] = None
            # [Guía] Evalúa `supplied` antes de ejecutar las instrucciones de esta rama.
            elif supplied:
                # [Guía] Establece `self.power[channel]` a partir de `scaled(fields, f'P{channel}_mW') if index <
                # [Guía] len(valid) and valid[index] == '1' else None`.
                self.power[channel] = scaled(fields, f'P{channel}_mW') if index < len(valid) and valid[index] == '1' else None
            # [Guía] Ejecuta este bloque cuando se cumple la alternativa del bloque anterior.
            else:
                # [Guía] Establece `self.power[channel]` a partir de `voltage * current`.
                self.power[channel] = voltage * current


# [Guía] Agrupa en TerminalFormatter el estado y las operaciones de esta parte de la aplicación.
class TerminalFormatter:
    """Reutilizar los formateadores originales; conservar la deduplicacion de eventos."""
    # [Guía] Inicializa el estado del formateador para poder presentar los mensajes que llegan en diferentes
    # [Guía] líneas.
    def __init__(self):
        # [Guía] Establece `self.last_event` a partir de `{}`.
        self.last_event = {}

    # [Guía] Una salida legible o None si el monitor original ocultaba esa repeticion. Recibe `line`.
    def format(self, line):
        """Una salida legible o None si el monitor original ocultaba esa repeticion.

        El archivo raw siempre conserva todas las lineas, incluso las ocultadas.
        Si una linea es inesperada, se conserva tal cual para no perder evidencia.
        """
        # [Guía] Establece `functions` a partir de `{'V_mV': telemetry, 'BK_STATE': emulator_display, 'OBS':
        # [Guía] observation_display, 'SOC': soc_display, 'EMS': ems_di…`.
        functions = {
            # [Guía] Añade la entrada 'V_mV' a este mapa de datos.
            'V_mV': telemetry, 'BK_STATE': emulator_display, 'OBS': observation_display,
            # [Guía] Añade la entrada 'SOC' a este mapa de datos.
            'SOC': soc_display, 'EMS': ems_display, 'MPPT_DIAG': mppt_display,
            # [Guía] Añade la entrada 'CTRL' a este mapa de datos.
            'CTRL': controller_display, 'TRIP': trip_display, 'CONNECTION': connection_display,
        }
        # [Guía] Establece `kind` a partir de `line.split(' ', 1)[0]`.
        kind = line.split(' ', 1)[0]
        # [Guía] Evalúa `kind == 'LIVE'` antes de ejecutar las instrucciones de esta rama.
        if kind == 'LIVE':
            # Datos para widgets; la consola conserva los reportes completos.
            # arduino_raw.log guarda tambien cada LIVE, sin deduplicarlo.
            # [Guía] Entrega None al llamador y termina la función.
            return None
        # [Guía] Evalúa `kind == 'CTRL' and 'SOURCE=LEGACY_INACTIVE ' in line` antes de ejecutar las instrucciones
        # [Guía] de esta rama.
        if kind == 'CTRL' and 'SOURCE=LEGACY_INACTIVE ' in line:
            # [Guía] Entrega None al llamador y termina la función.
            return None
        # [Guía] Evalúa `kind in ('TRIP', 'CONNECTION')` antes de ejecutar las instrucciones de esta rama.
        if kind in ('TRIP', 'CONNECTION'):
            # [Guía] Evalúa `self.last_event.get(kind) == line` antes de ejecutar las instrucciones de esta rama.
            if self.last_event.get(kind) == line:
                # [Guía] Entrega None al llamador y termina la función.
                return None
            # [Guía] Establece `self.last_event[kind]` a partir de `line`.
            self.last_event[kind] = line
        # [Guía] Evalúa `kind == 'TRIP' and 'VALID' not in fields_of(line)` antes de ejecutar las instrucciones de
        # [Guía] esta rama.
        if kind == 'TRIP' and 'VALID' not in fields_of(line):
            # [Guía] Entrega line al llamador y termina la función.
            return line
        # [Guía] Delimita la operación cuyos errores o cierre se gestionan en las ramas siguientes.
        try:
            # [Guía] Entrega functions[kind](line) if kind in functions else line al llamador y termina la
            # [Guía] función.
            return functions[kind](line) if kind in functions else line
        # [Guía] Atiende las excepciones que coinciden con `except (ValueError, KeyError, TypeError)` en el bloque
        # [Guía] anterior.
        except (ValueError, KeyError, TypeError):
            # [Guía] Entrega line al llamador y termina la función.
            return line


# [Guía] Genera datos de ejemplo para probar la interfaz sin conectar el banco. Recibe `tick`.
def demo_frame(tick):
    """Datos sinteticos SOLO para --demo. Nunca se mezclan con una sesion COM."""
    # [Guía] Importa math para las operaciones que realiza este módulo.
    import math
    # [Guía] Conserva en `ripple` el resultado de `int` con los argumentos indicados.
    ripple = int(80 * math.sin(tick / 3))
    # [Guía] Entrega [f'V_mV PV={18200 + ripple} BUS={20000 + ripple} BAT=12800(BAT_on=1) | I_mA PV=1350 BUS=800
    # [Guía] BAT=-250 | I_ok=1,1… al llamador y termina la función.
    return [
        # [Guía] Continúa la instrucción con `f'V_mV PV={18200 + ripple} BUS={20000 + ripple} BAT=12800(BAT_on=1)
        # [Guía] | I_mA PV=1350 BUS=800 BAT=-250 | I_ok=1,1,…`.
        f'V_mV PV={18200 + ripple} BUS={20000 + ripple} BAT=12800(BAT_on=1) | I_mA PV=1350 BUS=800 BAT=-250 | I_ok=1,1,1 M1=0 MPPT=0 D_permille=120 EMS_mode=M3 Q2_permille=640 Q3_permille=0',
        # [Guía] Continúa la instrucción con `'SOC PERCENT_milli=50000 INITIALIZED=1 CONTINUOUS=1 GAP=0
        # [Guía] STATE=ESTIMATING INPUT=DEMO'`.
        'SOC PERCENT_milli=50000 INITIALIZED=1 CONTINUOUS=1 GAP=0 STATE=ESTIMATING INPUT=DEMO',
        # [Guía] Continúa la instrucción con `'BK_STATE ENABLE=1 SEND_R=0 LINK=1 R_mOhm=200000 R_ack_mOhm=200000
        # [Guía] R_ok=1 LOAD_req=1 LOAD_ack=1 LOAD_ok=1 STATE…`.
        'BK_STATE ENABLE=1 SEND_R=0 LINK=1 R_mOhm=200000 R_ack_mOhm=200000 R_ok=1 LOAD_req=1 LOAD_ack=1 LOAD_ok=1 STATE=ON_CONFIRMADO Req_total_mOhm=25000 Req_ok=1',
        # [Guía] Continúa la instrucción con `'EMS PROFILE=1 MODE=M3 STATE=BUS_PI_Q2_CHARGING D1_permille=120
        # [Guía] D2_permille=640 D3_permille=0 MPPT=1 FAULT=0 CO…`.
        'EMS PROFILE=1 MODE=M3 STATE=BUS_PI_Q2_CHARGING D1_permille=120 D2_permille=640 D3_permille=0 MPPT=1 FAULT=0 COMMAND=DEMO KP3_x1e6=900 KI3_x1e6=347700 VERSION=DEMO_NO_HARDWARE',
        # [Guía] Continúa la instrucción con `'MPPT_DIAG DECISION=PROBE STEP_ppm=2500 DP_mW=80 P_mW=24570
        # [Guía] BASE_mW=24490 TARGET_ppm=120000 FLAT_N=0 UPDATES=12'`.
        'MPPT_DIAG DECISION=PROBE STEP_ppm=2500 DP_mW=80 P_mW=24570 BASE_mW=24490 TARGET_ppm=120000 FLAT_N=0 UPDATES=12',
    ]
