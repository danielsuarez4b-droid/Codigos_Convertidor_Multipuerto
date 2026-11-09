# [Guía] Puente entre el Arduino, el operador y la B&K. Traduce solicitudes a tramas binarias, devuelve
# [Guía] confirmaciones y conserva la salida de diagnóstico.
"""Puente manual: Arduino COM5 decide; BK8502 COM3 ejecuta y confirma.

Instalar pyserial en el Python usado: python -m pip install pyserial
Ejecutar: python control_bk8502.py --arduino COM5 --carga COM3
El trabajo bloqueante de la BK vive en un hilo; USB/monitor siguen activos.
"""
# Capas del puente: paquete binario -> BK8502 -> worker de carga -> bucle USB.
# Arduino conserva las decisiones EMS. Este proceso solo ejecuta solicitudes
# correlacionadas de carga y convierte telemetria en texto para el operador.
# dashboard.py reutiliza este programa como subproceso, no abre otros dos COM.
# [Guía] Importa argparse para las operaciones que realiza este módulo.
import argparse  # Opciones de consola, puertos y ruta del registro.
# [Guía] Importa datetime, timezone para las operaciones que realiza este módulo.
from datetime import datetime, timezone
# [Guía] Importa math para las operaciones que realiza este módulo.
import math
# [Guía] Importa queue para las operaciones que realiza este módulo.
import queue  # Colas seguras entre hilos; ningun hilo comparte transacciones BK.
# [Guía] Importa re para las operaciones que realiza este módulo.
import re
# [Guía] Importa struct para las operaciones que realiza este módulo.
import struct  # Conversion binaria little-endian requerida por el instrumento.
# [Guía] Importa threading para las operaciones que realiza este módulo.
import threading
# [Guía] Importa time para las operaciones que realiza este módulo.
import time

# Valores por defecto; --arduino/--carga permiten adaptarlos sin editar codigo.
# [Guía] Establece `PUERTO_ARDUINO` a partir de `'COM5'`.
PUERTO_ARDUINO = "COM5"
# [Guía] Establece `PUERTO_CARGA` a partir de `'COM3'`.
PUERTO_CARGA = "COM3"
# [Guía] Establece `(R_MIN, R_MAX)` a partir de `(30.0, 4000.0)`.
R_MIN, R_MAX = 30.0, 4000.0
# Limite del instrumento, no del convertidor. Con25 V la BK podia inhibirse
# precisamente al intentar consumir por sobretension. I/P no se amplian.
# [Guía] Establece `BK_MAX_VOLTAGE_MV` a partir de `40000`.
BK_MAX_VOLTAGE_MV = 40000


# [Guía] Construye la trama binaria B&K con dirección, comando, datos, relleno y suma de comprobación. Recibe
# [Guía] `address`, `command`, `payload`.
def packet(address, command, payload=b""):
    """Paquete BK de 26 bytes: cabecera, direccion, comando, datos y checksum."""
    # [Guía] Evalúa `not 0 <= address <= 254 or not 0 <= command <= 255 or len(payload) > 22` antes de ejecutar
    # [Guía] las instrucciones de esta rama.
    if not 0 <= address <= 254 or not 0 <= command <= 255 or len(payload) > 22:
        # [Guía] Señala ValueError('Direccion, comando o datos invalidos') para que la capa que llamó pueda tratar
        # [Guía] el problema.
        raise ValueError("Direccion, comando o datos invalidos")
    # bytearray comienza en cero: los bytes de reserva permanecen en cero.
    # Los indices0..2 son encabezado;3..24 datos;25 suma modulo256.
    # [Guía] Reserva la longitud fija de la trama e inicializa a cero los bytes de relleno.
    data = bytearray(26)
    # [Guía] Coloca el inicio de trama 0xAA, la dirección del instrumento y el código de operación.
    data[:3] = bytes((0xAA, address, command))
    # [Guía] Copia los datos de la orden después del encabezado, dejando a cero el espacio sin usar.
    data[3:3 + len(payload)] = payload
    # [Guía] Calcula la suma de los primeros 25 bytes y conserva los ocho bits bajos como verificación de
    # [Guía] integridad.
    data[25] = sum(data[:25]) & 0xFF
    # [Guía] Entrega bytes(data) al llamador y termina la función.
    return bytes(data)  # Entregar una trama inmutable al puerto serie.


# [Guía] Agrupa en BK8502 el estado y las operaciones de esta parte de la aplicación.
class BK8502:
    """Protocolo ya probado por el usuario; conservar CR/FIXED y sus limites."""

    # [Guía] Inicializa la instancia de BK8502 y prepara sus recursos y estado inicial. Recibe `port`, `address`.
    def __init__(self, port, address=0):
        # [Guía] Establece `(self.port, self.address)` a partir de `(port, address)`.
        self.port, self.address = port, address

    # [Guía] Envía una trama al instrumento, lee la respuesta y valida su estructura antes de aceptar la
    # [Guía] transacción. Recibe `command`, `payload`, `expected`.
    def exchange(self, command, payload=b"", expected=0x12):
        """Una transaccion con plazo; validar direccion, checksum y rechazo BK."""
        # exchange es secuencial: descartar residuos de una transaccion anterior
        # antes de enviar. No se debe llamar concurrentemente desde dos hilos.
        # [Guía] Descarta residuos de una respuesta anterior antes de iniciar el intercambio secuencial.
        self.port.reset_input_buffer()
        # [Guía] Envía por el puerto la trama que combina dirección, comando y datos de esta transacción.
        self.port.write(packet(self.address, command, payload))
        # [Guía] Ejecuta self.port.flush con los argumentos de esta llamada.
        self.port.flush()
        # [Guía] Conserva en `response` el resultado de `bytearray` con los argumentos indicados.
        response = bytearray()
        # Reloj monotono: cambiar la hora de Windows no alarga el timeout.
        # read puede entregar fragmentos; acumular hasta completar26bytes.
        # [Guía] Fija el plazo de recepción con un reloj que no retrocede cuando se ajusta la hora del computador.
        deadline = time.monotonic() + 2.0
        # [Guía] Repite el bloque mientras se cumpla `len(response) < 26 and time.monotonic() < deadline`.
        while len(response) < 26 and time.monotonic() < deadline:
            # [Guía] Completa la trama por fragmentos. Solicita únicamente el número de bytes que todavía faltan.
            response.extend(self.port.read(26 - len(response)))
        # Validar estructura e integridad antes de interpretar datos. ACK0x80
        # significa orden aceptada, no corriente fisica absorbida confirmada.
        # [Guía] Evalúa `len(response) != 26` antes de ejecutar las instrucciones de esta rama.
        if len(response) != 26:
            # [Guía] Señala RuntimeError(f'Timeout BK: {len(response)}/26 bytes') para que la capa que llamó pueda
            # [Guía] tratar el problema.
            raise RuntimeError(f"Timeout BK: {len(response)}/26 bytes")
        # [Guía] Evalúa `response[0] != 170 or response[1] != self.address` antes de ejecutar las instrucciones de
        # [Guía] esta rama.
        if response[0] != 0xAA or response[1] != self.address:
            # [Guía] Señala RuntimeError('Cabecera/direccion BK incorrecta') para que la capa que llamó pueda
            # [Guía] tratar el problema.
            raise RuntimeError("Cabecera/direccion BK incorrecta")
        # [Guía] Evalúa `sum(response[:25]) & 255 != response[25]` antes de ejecutar las instrucciones de esta
        # [Guía] rama.
        if (sum(response[:25]) & 0xFF) != response[25]:
            # [Guía] Señala RuntimeError('Checksum BK incorrecto') para que la capa que llamó pueda tratar el
            # [Guía] problema.
            raise RuntimeError("Checksum BK incorrecto")
        # [Guía] Evalúa `response[2] == 18 and response[3] != 128` antes de ejecutar las instrucciones de esta
        # [Guía] rama.
        if response[2] == 0x12 and response[3] != 0x80:
            # [Guía] Señala RuntimeError(f'BK rechazo comando: estado 0x{response[3]:02X}') para que la capa que
            # [Guía] llamó pueda tratar el problema.
            raise RuntimeError(f"BK rechazo comando: estado 0x{response[3]:02X}")
        # [Guía] Evalúa `response[2] != expected` antes de ejecutar las instrucciones de esta rama.
        if response[2] != expected:
            # [Guía] Señala RuntimeError(f'Respuesta BK inesperada: 0x{response[2]:02X}') para que la capa que
            # [Guía] llamó pueda tratar el problema.
            raise RuntimeError(f"Respuesta BK inesperada: 0x{response[2]:02X}")
        # [Guía] Entrega bytes(response) al llamador y termina la función.
        return bytes(response)

    # [Guía] Orden de un byte, incluida activacion independiente LOAD ON/OFF. Recibe `command`, `value`.
    def byte_command(self, command, value):
        """Orden de un byte, incluida activacion independiente LOAD ON/OFF."""
        # [Guía] Ejecuta self.exchange con los argumentos de esta llamada.
        self.exchange(command, bytes((value,)))

    # [Guía] Convierte ohmios a la escala del instrumento, comprueba límites y verifica la resistencia que
    # [Guía] responde la B&K. Recibe `ohms`.
    def set_resistance(self, ohms):
        """Configurar sin cambiar LOAD; releer el setpoint antes de confirmar."""
        # [Guía] Evalúa `not math.isfinite(ohms) or not R_MIN <= ohms <= R_MAX` antes de ejecutar las
        # [Guía] instrucciones de esta rama.
        if not math.isfinite(ohms) or not R_MIN <= ohms <= R_MAX:
            # [Guía] Señala ValueError('Resistencia fuera de 30..4000 ohm') para que la capa que llamó pueda
            # [Guía] tratar el problema.
            raise ValueError("Resistencia fuera de 30..4000 ohm")
        # CR usa un entero sin signo de32bits en miliohmios. '<I' especifica
        # little-endian; releer0x31 evita confirmar solo el valor que enviamos.
        # [Guía] Convierte la consigna de ohmios a miliohmios enteros, que es la escala del protocolo de la carga.
        milliohms = round(ohms * 1000)
        # [Guía] Envía la escritura de resistencia en un entero sin signo de 32 bits, ordenado del byte menos al
        # [Guía] más significativo.
        self.exchange(0x30, struct.pack("<I", milliohms))
        # [Guía] Solicita al instrumento que devuelva el ajuste de resistencia para verificar qué valor aceptó.
        response = self.exchange(0x31, expected=0x31)
        # [Guía] Extrae desde el campo de datos el valor de retorno en miliohmios.
        received = struct.unpack_from("<I", response, 3)[0]
        # [Guía] Evalúa `received != milliohms` antes de ejecutar las instrucciones de esta rama.
        if received != milliohms:
            # [Guía] Señala RuntimeError(f'Setpoint BK no coincide: {received / 1000} ohm') para que la capa que
            # [Guía] llamó pueda tratar el problema.
            raise RuntimeError(f"Setpoint BK no coincide: {received / 1000} ohm")
        # [Guía] Entrega received / 1000 al llamador y termina la función.
        return received / 1000

    # [Guía] Configura el instrumento para la operación remota en resistencia constante con el consumo
    # [Guía] inicialmente apagado.
    def initialize(self):
        """Arrancar remoto y OFF; admitir mitigacion hasta40 V en la carga."""
        # Orden: remoto, consumo OFF, funcion estandar, CR y modo FIXED.
        # Mantener esta secuencia del banco; no aplicar LOAD ON al inicializar.
        # [Guía] Ejecuta self.byte_command con los argumentos de esta llamada.
        self.byte_command(0x20, 1)
        # [Guía] Ejecuta self.byte_command con los argumentos de esta llamada.
        self.byte_command(0x21, 0)
        # [Guía] Ejecuta self.byte_command con los argumentos de esta llamada.
        self.byte_command(0x5D, 0)
        # [Guía] Ejecuta self.byte_command con los argumentos de esta llamada.
        self.byte_command(0x28, 3)
        # [Guía] Ejecuta self.byte_command con los argumentos de esta llamada.
        self.byte_command(0x55, 1)
        # Limites del instrumento escalados en mV/mA/mW:40V,10A,25W.
        # No sustituyen limites seguros del convertidor ni protecciones fisicas.
        # [Guía] Recorre ((34, BK_MAX_VOLTAGE_MV), (36, 10000), (38, 25000)). En cada iteración, (command, value)
        # [Guía] recibe el elemento correspondiente.
        for command, value in ((0x22, BK_MAX_VOLTAGE_MV), (0x24, 10000), (0x26, 25000)):
            # [Guía] Ejecuta self.exchange con los argumentos de esta llamada.
            self.exchange(command, struct.pack("<I", value))
        # Estado inicial conocido aun antes del primer pedido del Arduino.
        # No dar permiso ON al worker: este exige otra confirmacion correlacionada.
        # [Guía] Ejecuta self.set_resistance con los argumentos de esta llamada.
        self.set_resistance(4000.0)


# [Guía] La consola manda intenciones a Arduino; nunca configura BK directamente. Recibe `text`.
def normalize_input(text):
    """La consola manda intenciones a Arduino; nunca configura BK directamente."""
    # Unificar espacios externos y mayusculas. La lista blanca impide inyectar
    # comandos de protocolo BK_ACK/BK_HELLO desde la entrada humana normal.
    # [Guía] Conserva en `text` el resultado de `text.strip().upper` con los argumentos indicados.
    text = text.strip().upper()
    # [Guía] Evalúa `text in ('ON', 'OFF', 'ENABLE', 'DISABLE', 'SALIR', 'S', 'M', 'X', 'Z', 'P', 'B', 'A',
    # [Guía] 'AUTO_R', 'MANUAL', 'OV_…` antes de ejecutar las instrucciones de esta rama.
    if text in ("ON", "OFF", "ENABLE", "DISABLE", "SALIR", "S", "M", "X", "Z", "P", "B", "A", "AUTO_R", "MANUAL", "OV_RESET", "EMS_START", "EMS_RESET"):
        # [Guía] Entrega text al llamador y termina la función.
        return text
    # [Guía] Evalúa `text.startswith('SOC=')` antes de ejecutar las instrucciones de esta rama.
    if text.startswith("SOC="):
        # El porcentaje lo decide el usuario; Python no estima SOC desde Vbat.
        # [Guía] Conserva en `percent` el resultado de `float` con los argumentos indicados.
        percent = float(text[4:].replace(",", "."))
        # [Guía] Evalúa `not math.isfinite(percent) or not 0 <= percent <= 100` antes de ejecutar las
        # [Guía] instrucciones de esta rama.
        if not math.isfinite(percent) or not 0 <= percent <= 100:
            # [Guía] Señala ValueError('SOC inicial fuera de 0..100 por ciento') para que la capa que llamó pueda
            # [Guía] tratar el problema.
            raise ValueError("SOC inicial fuera de 0..100 por ciento")
        # [Guía] Entrega f'SOC={percent:.6f}' al llamador y termina la función.
        return f"SOC={percent:.6f}"
    # [Guía] Evalúa `text.startswith('R=')` antes de ejecutar las instrucciones de esta rama.
    if text.startswith("R="):
        # [Guía] Establece `text` a partir de `text[2:]`.
        text = text[2:]
    # Un numero sin R= tambien significa ohmios. Admitir coma decimal solo en
    # entrada humana; el protocolo entre procesos sigue usando punto decimal.
    # [Guía] Conserva en `resistance` el resultado de `float` con los argumentos indicados.
    resistance = float(text.replace(",", "."))
    # [Guía] Evalúa `not math.isfinite(resistance) or not R_MIN <= resistance <= R_MAX` antes de ejecutar las
    # [Guía] instrucciones de esta rama.
    if not math.isfinite(resistance) or not R_MIN <= resistance <= R_MAX:
        # [Guía] Señala ValueError('Resistencia fuera de 30..4000 ohm') para que la capa que llamó pueda tratar el
        # [Guía] problema.
        raise ValueError("Resistencia fuera de 30..4000 ohm")
    # [Guía] Entrega f'R={resistance:.3f}' al llamador y termina la función.
    return f"R={resistance:.3f}"


# [Guía] Traduce una solicitud textual de Arduino a un identificador, una operación y un valor para el hilo de la
# [Guía] B&K. Recibe `line`.
def parse_request(line):
    """Validar una orden correlacionada: BK_REQ=id,R,ohm o ...,LOAD,0/1."""
    # fullmatch exige linea completa: no aceptar sufijos ni prefijos parciales.
    # ID permite que Arduino distinga esta respuesta de una orden antigua.
    # [Guía] Conserva en `match` el resultado de `re.fullmatch` con los argumentos indicados.
    match = re.fullmatch(r"BK_REQ=([0-9]+),(R|LOAD),([0-9]+(?:\.[0-9]+)?)", line)
    # [Guía] Evalúa `not match` antes de ejecutar las instrucciones de esta rama.
    if not match:
        # [Guía] Señala ValueError('Orden BK_REQ mal formada') para que la capa que llamó pueda tratar el
        # [Guía] problema.
        raise ValueError("Orden BK_REQ mal formada")
    # [Guía] Establece `(order_id, kind, value)` a partir de `(int(match[1]), match[2], float(match[3]))`.
    order_id, kind, value = int(match[1]), match[2], float(match[3])
    # [Guía] Evalúa `not 1 <= order_id <= 4294967295 or not math.isfinite(value)` antes de ejecutar las
    # [Guía] instrucciones de esta rama.
    if not 1 <= order_id <= 0xFFFFFFFF or not math.isfinite(value):
        # [Guía] Señala ValueError('ID/valor invalido') para que la capa que llamó pueda tratar el problema.
        raise ValueError("ID/valor invalido")
    # [Guía] Evalúa `kind == 'R' and (not R_MIN <= value <= R_MAX)` antes de ejecutar las instrucciones de esta
    # [Guía] rama.
    if kind == "R" and not R_MIN <= value <= R_MAX:
        # [Guía] Señala ValueError('R fuera del rango de ensayo') para que la capa que llamó pueda tratar el
        # [Guía] problema.
        raise ValueError("R fuera del rango de ensayo")
    # [Guía] Evalúa `kind == 'LOAD' and value not in (0, 1)` antes de ejecutar las instrucciones de esta rama.
    if kind == "LOAD" and value not in (0, 1):
        # [Guía] Señala ValueError('LOAD debe ser 0 o 1') para que la capa que llamó pueda tratar el problema.
        raise ValueError("LOAD debe ser 0 o 1")
    # [Guía] Entrega (order_id, kind, value) al llamador y termina la función.
    return order_id, kind, value


# [Guía] Distinguir transistor, pin fisico y duty; no reasigna salidas Arduino. Recibe `values`.
def duty_display(values):
    """Distinguir transistor, pin fisico y duty; no reasigna salidas Arduino."""
    # [Guía] Establece `pins` a partir de `(3, 5, 6)`.
    pins = (3, 5, 6)  # Conexion confirmada: Q1=D3, Q2=D5, Q3=D6.
    # [Guía] Entrega ' '.join((f'Duty_Q{n}(D{pin})={value / 10:.1f}%' for n, (pin, value) in enumerate(zip(pins,
    # [Guía] values), 1))) al llamador y termina la función.
    return " ".join(f"Duty_Q{n}(D{pin})={value / 10:.1f}%"
                    # [Guía] Continúa la instrucción con `for n, (pin, value) in enumerate(zip(pins, values),
                    # [Guía] 1))`.
                    for n, (pin, value) in enumerate(zip(pins, values), 1))


# [Guía] Extraer medidas con unidades explicitas sin confundir canales repetidos. Recibe `line`.
def telemetry(line):
    """Extraer medidas con unidades explicitas sin confundir canales repetidos."""
    # [Guía] Conserva en `match` el resultado de `re.search` con los argumentos indicados.
    match = re.search(r"V_mV PV=(-?\d+) BUS=(-?\d+) BAT=(-?\d+).*I_mA PV=(-?\d+) BUS=(-?\d+) BAT=(-?\d+)", line)
    # [Guía] Evalúa `not match` antes de ejecutar las instrucciones de esta rama.
    if not match:
        # [Guía] Entrega None al llamador y termina la función.
        return None
    # El protocolo repite PV/BUS/BAT para V e I. Capturarlos por secciones evita
    # perder V al convertir toda la linea en un unico diccionario de claves.
    # [Guía] Establece `values` a partir de `[int(value) / 1000 for value in match.groups()]`.
    values = [int(value) / 1000 for value in match.groups()]
    # [Guía] Conserva en `fields` el resultado de `dict` con los argumentos indicados.
    fields = dict(re.findall(r"\b(M1|MPPT|D_permille|Hz|lost|PI_lost|CALmask|EMS_mode|Q2_permille|Q3_permille)=([^\s|]+)", line))
    # Mostrar el modo declarado por firmware; no inferir M3 por Ibat negativa.
    # [Guía] Conserva en `test_mode` el resultado de `fields.get` con los argumentos indicados.
    test_mode = fields.get("EMS_mode")
    # [Guía] Establece `mode` a partir de `test_mode if test_mode and test_mode != 'LEGACY' else 'MPPT' if
    # [Guía] fields.get('MPPT') == '1' else 'M1' if fields.g…`.
    mode = (test_mode if test_mode and test_mode != "LEGACY" else
            # [Guía] Continúa la instrucción con `"MPPT" if fields.get("MPPT") == "1" else "M1" if
            # [Guía] fields.get("M1") == "1" else "PARADO/CAL")`.
            "MPPT" if fields.get("MPPT") == "1" else "M1" if fields.get("M1") == "1" else "PARADO/CAL")
    # [Guía] Establece `duties` a partir de `f"D={int(fields.get('D_permille', 0)) / 10:5.1f}%"`.
    duties = f"D={int(fields.get('D_permille', 0)) / 10:5.1f}%"
    # [Guía] Evalúa `test_mode and test_mode != 'LEGACY'` antes de ejecutar las instrucciones de esta rama.
    if test_mode and test_mode != "LEGACY":
        # [Guía] Conserva en `duties` el resultado de `duty_display` con los argumentos indicados.
        duties = duty_display([int(fields.get(key, 0))
                               # [Guía] Continúa la instrucción con `for key in ('D_permille', 'Q2_permille',
                               # [Guía] 'Q3_permille')])`.
                               for key in ('D_permille', 'Q2_permille', 'Q3_permille')])
    # [Guía] Entrega f"{mode:10} | PV {values[0]:7.3f} V {values[3]:7.3f} A | BUS {values[1]:7.3f} V
    # [Guía] {values[4]:7.3f} A total | BAT… al llamador y termina la función.
    return (f"{mode:10} | PV {values[0]:7.3f} V {values[3]:7.3f} A | "
            # [Guía] Continúa la instrucción con `f"BUS {values[1]:7.3f} V {values[4]:7.3f} A total | "`.
            f"BUS {values[1]:7.3f} V {values[4]:7.3f} A total | "
            # [Guía] Continúa la instrucción con `f"BAT {values[2]:7.3f} V {values[5]:7.3f} A | "`.
            f"BAT {values[2]:7.3f} V {values[5]:7.3f} A | "
            # [Guía] Continúa la instrucción con `f"{duties} | "`.
            f"{duties} | "
            # [Guía] Continúa la instrucción con `f"Hz={fields.get('Hz', '?')} lost={fields.get('lost', '?')}
            # [Guía] PI_lost={fields.get('PI_lost', '?')}")`.
            f"Hz={fields.get('Hz', '?')} lost={fields.get('lost', '?')} PI_lost={fields.get('PI_lost', '?')}")


# [Guía] Distinguir consigna, lectura de setpoint y equivalente de ambas cargas. Recibe `line`.
def emulator_display(line):
    """Distinguir consigna, lectura de setpoint y equivalente de ambas cargas."""
    # [Guía] Conserva en `fields` el resultado de `dict` con los argumentos indicados.
    fields = dict(re.findall(r"([A-Za-z_]+)=([^\s]+)", line))
    # [Guía] Presenta un campo del protocolo en ohmios si hay confirmación válida. Usa el texto de ausencia
    # [Guía] indicado en los demás casos. Recibe `key`, `valid`, `missing`.
    def ohms(key, valid, missing="sin confirmar"):
        # [Guía] Entrega f'{int(fields[key]) / 1000:.3f} ohm' if fields.get(valid) == '1' else missing al llamador
        # [Guía] y termina la función.
        return f"{int(fields[key]) / 1000:.3f} ohm" if fields.get(valid) == "1" else missing
    # R pedida, R releida y Req_total son tres magnitudes distintas. LOAD_ok
    # diferencia desconocido de OFF; falta de confirmacion nunca equivale a OFF.
    # [Guía] Establece `desired` a partir de `int(fields['R_mOhm']) / 1000`.
    desired = int(fields["R_mOhm"]) / 1000
    # [Guía] Establece `load` a partir de `('ON' if fields.get('LOAD_ack') == '1' else 'OFF') if
    # [Guía] fields.get('LOAD_ok') == '1' else 'DESCONOCIDO'`.
    load = ("ON" if fields.get("LOAD_ack") == "1" else "OFF") if fields.get("LOAD_ok") == "1" else "DESCONOCIDO"
    # [Guía] Entrega f"EMU {fields.get('STATE')} | permiso={fields.get('ENABLE')} enlace={fields.get('LINK')} | R
    # [Guía] pedida={desired:.3… al llamador y termina la función.
    return (f"EMU {fields.get('STATE')} | permiso={fields.get('ENABLE')} enlace={fields.get('LINK')} "
            # [Guía] Continúa la instrucción con `f"| R pedida={desired:.3f} ohm | R BK={ohms('R_ack_mOhm',
            # [Guía] 'R_ok')} "`.
            f"| R pedida={desired:.3f} ohm | R BK={ohms('R_ack_mOhm', 'R_ok')} "
            # [Guía] Continúa la instrucción con `f"| LOAD orden aceptada={load} | Req
            # [Guía] total={ohms('Req_total_mOhm', 'Req_ok', 'no valido')}")`.
            f"| LOAD orden aceptada={load} | Req total={ohms('Req_total_mOhm', 'Req_ok', 'no valido')}")


# [Guía] Extremos por ventana y potencias; nunca declarar automaticamente una causa. Recibe `line`.
def observation_display(line):
    """Extremos por ventana y potencias; nunca declarar automaticamente una causa."""
    # [Guía] Conserva en `fields` el resultado de `dict` con los argumentos indicados.
    fields = dict(re.findall(r"([A-Za-z_]+)=([^\s]+)", line))
    # [Guía] Extrae un valor del informe de observación y lo convierte desde la escala del protocolo. Recibe
    # [Guía] `key`, `scale`.
    def scaled(key, scale=1000):
        # [Guía] Entrega int(fields[key]) / scale al llamador y termina la función.
        return int(fields[key]) / scale
    # [Guía] Establece `pv_power` a partir de `f"{scaled('Ppv_mW'):.3f} W" if fields.get('Ppv_ok') == '1' else
    # [Guía] 'invalida'`.
    pv_power = f"{scaled('Ppv_mW'):.3f} W" if fields.get("Ppv_ok") == "1" else "invalida"
    # [Guía] Establece `bus_power` a partir de `f"{scaled('Pbus_mW'):.3f} W" if fields.get('Pbus_ok') == '1' else
    # [Guía] 'invalida'`.
    bus_power = f"{scaled('Pbus_mW'):.3f} W" if fields.get("Pbus_ok") == "1" else "invalida"
    # [Guía] Entrega f"OSC | PV {scaled('PVmin_mV'):.3f}..{scaled('PVmax_mV'):.3f} V | BUS
    # [Guía] {scaled('BUSmin_mV'):.3f}..{scaled('BUSma… al llamador y termina la función.
    return (f"OSC | PV {scaled('PVmin_mV'):.3f}..{scaled('PVmax_mV'):.3f} V "
            # [Guía] Continúa la instrucción con `f"| BUS {scaled('BUSmin_mV'):.3f}..{scaled('BUSmax_mV'):.3f} V
            # [Guía] "`.
            f"| BUS {scaled('BUSmin_mV'):.3f}..{scaled('BUSmax_mV'):.3f} V "
            # [Guía] Continúa la instrucción con `f"| duty Q1 (pin D3) {scaled('Dmin_permille',
            # [Guía] 10):.1f}..{scaled('Dmax_permille', 10):.1f}% "`.
            f"| duty Q1 (pin D3) {scaled('Dmin_permille', 10):.1f}..{scaled('Dmax_permille', 10):.1f}% "
            # [Guía] Continúa la instrucción con `f"| Ppv={pv_power} Pbus total={bus_power}")`.
            f"| Ppv={pv_power} Pbus total={bus_power}")


# [Guía] Mostrar lo que usa el PI, su integral y frecuencia de limitacion de slew. Recibe `line`.
def controller_display(line):
    """Mostrar lo que usa el PI, su integral y frecuencia de limitacion de slew."""
    # [Guía] Conserva en `fields` el resultado de `dict` con los argumentos indicados.
    fields = dict(re.findall(r"([A-Za-z_0-9]+)=([^\s]+)", line))
    # SlewN cuenta pasos limitados, no magnitud del duty. Evitar dividir por0
    # si no hubo actualizaciones PI en la ventana (por ejemplo en espera).
    # [Guía] Conserva en `n` el resultado de `int` con los argumentos indicados.
    n = int(fields["N"])
    # [Guía] Establece `slew` a partir de `100 * int(fields['SlewN']) / n if n else 0`.
    slew = 100 * int(fields["SlewN"]) / n if n else 0
    # [Guía] Entrega f"PI | error {int(fields['Emin_mV']) / 1000:.3f}..{int(fields['Emax_mV']) / 1000:.3f} V |
    # [Guía] I={int(fields['Int_pe… al llamador y termina la función.
    return (f"PI | error {int(fields['Emin_mV']) / 1000:.3f}..{int(fields['Emax_mV']) / 1000:.3f} V "
            # [Guía] Continúa la instrucción con `f"| I={int(fields['Int_permille']) / 1000:.3f} duty "`.
            f"| I={int(fields['Int_permille']) / 1000:.3f} duty "
            # [Guía] Continúa la instrucción con `f"| slew={slew:.1f}% de pasos | Kp={int(fields['Kp_x1000']) /
            # [Guía] 1000:.3f} "`.
            f"| slew={slew:.1f}% de pasos | Kp={int(fields['Kp_x1000']) / 1000:.3f} "
            # [Guía] Continúa la instrucción con `f"Ki={int(fields['Ki_x1000']) / 1000:.3f} |
            # [Guía] STOP={fields['STOP']}")`.
            f"Ki={int(fields['Ki_x1000']) / 1000:.3f} | STOP={fields['STOP']}")


# [Guía] Mostrar ausencia de semilla y huecos sin presentarlos como SOC exacto. Recibe `line`.
def soc_display(line):
    """Mostrar ausencia de semilla y huecos sin presentarlos como SOC exacto."""
    # [Guía] Conserva en `fields` el resultado de `dict` con los argumentos indicados.
    fields = dict(re.findall(r"([A-Za-z_]+)=([^\s]+)", line))
    # [Guía] Establece `percent` a partir de `f"{int(fields['PERCENT_milli']) / 1000:.3f}%" if
    # [Guía] fields.get('INITIALIZED') == '1' else 'sin inicializar'`.
    percent = (f"{int(fields['PERCENT_milli']) / 1000:.3f}%"
               # [Guía] Continúa la instrucción con `if fields.get("INITIALIZED") == "1" else "sin inicializar")`.
               if fields.get("INITIALIZED") == "1" else "sin inicializar")
    # [Guía] Entrega f"SOC estimado={percent} | estado={fields.get('STATE')} | continuo={fields.get('CONTINUOUS')}
    # [Guía] hueco={fields.get… al llamador y termina la función.
    return (f"SOC estimado={percent} | estado={fields.get('STATE')} "
            # [Guía] Continúa la instrucción con `f"| continuo={fields.get('CONTINUOUS')}
            # [Guía] hueco={fields.get('GAP')} "`.
            f"| continuo={fields.get('CONTINUOUS')} hueco={fields.get('GAP')} "
            # [Guía] Continúa la instrucción con `f"| entrada={fields.get('INPUT')}")`.
            f"| entrada={fields.get('INPUT')}")


# [Guía] La FSM del Arduino decide: el monitor no deduce un modo por la potencia. Recibe `line`.
def ems_display(line):
    """La FSM del Arduino decide: el monitor no deduce un modo por la potencia."""
    # [Guía] Conserva en `fields` el resultado de `dict` con los argumentos indicados.
    fields = dict(re.findall(r"([A-Za-z_0-9]+)=([^\s]+)", line))
    # [Guía] Evalúa `fields.get('PROFILE') != '1'` antes de ejecutar las instrucciones de esta rama.
    if fields.get("PROFILE") != "1":
        # [Guía] Entrega 'EMS de ensayo deshabilitada; control Q1 anterior.' al llamador y termina la función.
        return "EMS de ensayo deshabilitada; control Q1 anterior."
    # [Guía] Conserva en `duties` el resultado de `duty_display` con los argumentos indicados.
    duties = duty_display([int(fields[f'D{n}_permille']) for n in (1, 2, 3)])
    # [Guía] Establece `version` a partir de `f" | version={fields['VERSION']}" if 'VERSION' in fields else ''`.
    version = f" | version={fields['VERSION']}" if 'VERSION' in fields else ""
    # Permiso filtrado separado de las medias de pantalla y del dato raw del TRIP.
    # [Guía] Establece `voltage` a partir de `f" | Vbat control={int(fields['Vbat_ctrl_mV']) / 1000:.3f} V" if
    # [Guía] 'Vbat_ctrl_mV' in fields else ''`.
    voltage = (f" | Vbat control={int(fields['Vbat_ctrl_mV']) / 1000:.3f} V"
               # [Guía] Continúa la instrucción con `if 'Vbat_ctrl_mV' in fields else "")`.
               if 'Vbat_ctrl_mV' in fields else "")
    # [Guía] Establece `updates` a partir de `f" | control_n={fields['CTRL_N']} perdidos={fields.get('CTRL_LOST',
    # [Guía] '0')}" if 'CTRL_N' in fields else ''`.
    updates = (f" | control_n={fields['CTRL_N']} perdidos={fields.get('CTRL_LOST', '0')}"
               # [Guía] Continúa la instrucción con `if 'CTRL_N' in fields else "")`.
               if 'CTRL_N' in fields else "")
    # [Guía] Establece `reference` a partir de `f" | referencia={int(fields['REF_mV']) / 1000:.3f} V" if 'REF_mV'
    # [Guía] in fields else ''`.
    reference = (f" | referencia={int(fields['REF_mV']) / 1000:.3f} V"
                 # [Guía] Continúa la instrucción con `if 'REF_mV' in fields else "")`.
                 if 'REF_mV' in fields else "")
    # [Guía] Establece `m2_active` a partir de `fields.get('M2') == '1'`.
    m2_active = fields.get('M2') == '1'
    # [Guía] Evalúa `m2_active and 'M2_IREF_mA' in fields` antes de ejecutar las instrucciones de esta rama.
    if m2_active and 'M2_IREF_mA' in fields:
        # M2 regula AMPERIOS; REF_mV sigue siendo el campo historico de bus.
        # [Guía] Establece `reference` a partir de `f" | referencia Ibat={int(fields['M2_IREF_mA']) / 1000:.3f} A
        # [Guía] (bus sin regulacion)"`.
        reference = (f" | referencia Ibat={int(fields['M2_IREF_mA']) / 1000:.3f} A"
                     # [Guía] Continúa la instrucción con `" (bus sin regulacion)")`.
                     " (bus sin regulacion)")
        # [Guía] Combina el valor anterior de `reference` con `f" | Ibus media={int(fields.get('M2_IBUS_mA', '0'))
        # [Guía] / 1000:.3f} A demanda externa={int(fields.get('M2_DEMAND_mA…` mediante esta operación.
        reference += (f" | Ibus media={int(fields.get('M2_IBUS_mA', '0')) / 1000:.3f} A"
                      # [Guía] Continúa la instrucción con `f" demanda externa={int(fields.get('M2_DEMAND_mA',
                      # [Guía] '0')) / 1000:.3f} A"`.
                      f" demanda externa={int(fields.get('M2_DEMAND_mA', '0')) / 1000:.3f} A"
                      # [Guía] Continúa la instrucción con `if fields.get('M2_IBUS_OK') == '1' else " | Ibus sin
                      # [Guía] ventana valida")`.
                      if fields.get('M2_IBUS_OK') == '1' else " | Ibus sin ventana valida")
    # V64 explica la espera EN M3/M4. M2=0 no significa diagnostico ausente:
    # el contador demuestra si Ibus confirma 1s o si un permiso impide actuar.
    # [Guía] Establece `m2_wait` a partir de `''`.
    m2_wait = ""
    # [Guía] Evalúa `'M2_STAGE' in fields and fields.get('MODE') in ('M3', 'M4', 'TRANSFER', 'M2')` antes de
    # [Guía] ejecutar las instrucciones de esta rama.
    if 'M2_STAGE' in fields and fields.get('MODE') in ('M3', 'M4', 'TRANSFER', 'M2'):
        # [Guía] Establece `detection` a partir de `f"deteccion=RAPIDA ventana={fields.get('M2_WINDOW_ms', '5')}
        # [Guía] ms" if fields.get('M2_NEED_ms') == '0' else f"sin…`.
        detection = (f"deteccion=RAPIDA ventana={fields.get('M2_WINDOW_ms', '5')} ms"
                     # [Guía] Continúa la instrucción con `if fields.get('M2_NEED_ms') == '0' else`.
                     if fields.get('M2_NEED_ms') == '0' else
                     # [Guía] Continúa la instrucción con `f"sin carga={fields.get('M2_IDLE_ms',
                     # [Guía] '0')}/{fields.get('M2_NEED_ms', '1000')} ms")`.
                     f"sin carga={fields.get('M2_IDLE_ms', '0')}/{fields.get('M2_NEED_ms', '1000')} ms")
        # [Guía] Establece `m2_wait` a partir de `f" | M2 etapa={fields['M2_STAGE']} {detection}
        # [Guía] reinicios={fields.get('M2_RESETS', '0')} motivo={fields.get('M2_…`.
        m2_wait = (f" | M2 etapa={fields['M2_STAGE']} "
                   # [Guía] Continúa la instrucción con `f"{detection} "`.
                   f"{detection} "
                   # [Guía] Continúa la instrucción con `f"reinicios={fields.get('M2_RESETS', '0')}
                   # [Guía] motivo={fields.get('M2_BLOCK', 'UNKNOWN')}")`.
                   f"reinicios={fields.get('M2_RESETS', '0')} motivo={fields.get('M2_BLOCK', 'UNKNOWN')}")
        # [Guía] Evalúa `not m2_active` antes de ejecutar las instrucciones de esta rama.
        if not m2_active:
            # [Guía] Combina el valor anterior de `m2_wait` con `f" Ibus media={int(fields.get('M2_IBUS_mA', '0'))
            # [Guía] / 1000:.3f} A demanda externa={int(fields.get('M2_DEMAND_mA',…` mediante esta operación.
            m2_wait += (f" Ibus media={int(fields.get('M2_IBUS_mA', '0')) / 1000:.3f} A"
                        # [Guía] Continúa la instrucción con `f" demanda externa={int(fields.get('M2_DEMAND_mA',
                        # [Guía] '0')) / 1000:.3f} A"`.
                        f" demanda externa={int(fields.get('M2_DEMAND_mA', '0')) / 1000:.3f} A"
                        # [Guía] Continúa la instrucción con `if fields.get('M2_IBUS_OK') == '1' else " Ibus sin
                        # [Guía] ventana valida")`.
                        if fields.get('M2_IBUS_OK') == '1' else " Ibus sin ventana valida")
        # [Guía] Evalúa `'M2_GUARD_OK' in fields` antes de ejecutar las instrucciones de esta rama.
        if 'M2_GUARD_OK' in fields:
            # [Guía] Combina el valor anterior de `m2_wait` con `f" Ibus contraste={int(fields.get('M2_GUARD_mA',
            # [Guía] '0')) / 1000:.3f} A ventana={fields.get('M2_GUARD_ms', '40')}…` mediante esta operación.
            m2_wait += (f" Ibus contraste={int(fields.get('M2_GUARD_mA', '0')) / 1000:.3f} A"
                        # [Guía] Continúa la instrucción con `f" ventana={fields.get('M2_GUARD_ms', '40')} ms"`.
                        f" ventana={fields.get('M2_GUARD_ms', '40')} ms"
                        # [Guía] Continúa la instrucción con `if fields['M2_GUARD_OK'] == '1' else " contraste
                        # [Guía] Ibus sin ventana valida")`.
                        if fields['M2_GUARD_OK'] == '1' else " contraste Ibus sin ventana valida")
    # [Guía] Establece `guard` a partir de `''`.
    guard = ""
    # [Guía] Evalúa `'Ihold' in fields` antes de ejecutar las instrucciones de esta rama.
    if 'Ihold' in fields:
        # [Guía] Establece `state` a partir de `'FAULT' if fields.get('FAULT') == '1' else 'CONFIRMANDO' if
        # [Guía] fields['Ihold'] == '1' else 'VIGILANDO'`.
        state = "FAULT" if fields.get('FAULT') == '1' else "CONFIRMANDO" if fields['Ihold'] == '1' else "VIGILANDO"
        # [Guía] Establece `guard` a partir de `f" | Ibat={state} muestras guard={fields.get('Iwarnings', '0')}
        # [Guía] maximo ADC={int(fields.get('Ipeak_mA', 0)) / 10…`.
        guard = (f" | Ibat={state} muestras guard={fields.get('Iwarnings', '0')} "
                 # [Guía] Continúa la instrucción con `f"maximo ADC={int(fields.get('Ipeak_mA', 0)) / 1000:.3f}
                 # [Guía] A")`.
                 f"maximo ADC={int(fields.get('Ipeak_mA', 0)) / 1000:.3f} A")
    # [Guía] Establece `mppt_window` a partir de `f" | MPPT ventana activa={fields['MPPT_W_ms']} ms" if
    # [Guía] 'MPPT_W_ms' in fields else ''`.
    mppt_window = (f" | MPPT ventana activa={fields['MPPT_W_ms']} ms"
                   # [Guía] Continúa la instrucción con `if 'MPPT_W_ms' in fields else "")`.
                   if 'MPPT_W_ms' in fields else "")
    # [Guía] Establece `bus_ems` a partir de `f" | Vbus EMS={int(fields['BUS_EMS_mV']) / 1000:.3f} V" if
    # [Guía] 'BUS_EMS_mV' in fields else ''`.
    bus_ems = (f" | Vbus EMS={int(fields['BUS_EMS_mV']) / 1000:.3f} V"
               # [Guía] Continúa la instrucción con `if 'BUS_EMS_mV' in fields else "")`.
               if 'BUS_EMS_mV' in fields else "")
    # [Guía] Establece `bat_link` a partir de `f" | enlace bateria estimado={fields['BAT_LINK_EST']} Ibat
    # [Guía] EMS={int(fields.get('IBAT_EMS_mA', '0')) / 1000:.3f}…`.
    bat_link = (f" | enlace bateria estimado={fields['BAT_LINK_EST']} "
                # [Guía] Continúa la instrucción con `f"Ibat EMS={int(fields.get('IBAT_EMS_mA', '0')) / 1000:.3f}
                # [Guía] A"`.
                f"Ibat EMS={int(fields.get('IBAT_EMS_mA', '0')) / 1000:.3f} A"
                # [Guía] Continúa la instrucción con `if 'BAT_LINK_EST' in fields else "")`.
                if 'BAT_LINK_EST' in fields else "")
    # Compatibilidad con registros V21; V22 ya no requiere permiso manual.
    # [Guía] Establece `bat_auth` a partir de `f" | permiso bateria={fields['BAT_ALLOW']}
    # [Guía] autorizacion={fields.get('BAT_AUTH', 'UNKNOWN')}" if 'BAT_ALLOW' in…`.
    bat_auth = (f" | permiso bateria={fields['BAT_ALLOW']} "
                # [Guía] Continúa la instrucción con `f"autorizacion={fields.get('BAT_AUTH', 'UNKNOWN')}"`.
                f"autorizacion={fields.get('BAT_AUTH', 'UNKNOWN')}"
                # [Guía] Continúa la instrucción con `if 'BAT_ALLOW' in fields else "")`.
                if 'BAT_ALLOW' in fields else "")
    # Ventana de corriente y evidencia: no afirmar que se leyo el interruptor.
    # [Guía] Establece `bat_response` a partir de `''`.
    bat_response = ""
    # [Guía] Evalúa `'BAT_LINK_STATE' in fields` antes de ejecutar las instrucciones de esta rama.
    if 'BAT_LINK_STATE' in fields:
        # [Guía] Establece `mean` a partir de `f"{int(fields.get('IBAT_WIN_mA', '0')) / 1000:.3f} A" if
        # [Guía] fields.get('IBAT_WIN_READY') == '1' else 'sin ventana'`.
        mean = (f"{int(fields.get('IBAT_WIN_mA', '0')) / 1000:.3f} A"
                # [Guía] Continúa la instrucción con `if fields.get('IBAT_WIN_READY') == '1' else "sin ventana")`.
                if fields.get('IBAT_WIN_READY') == '1' else "sin ventana")
        # [Guía] Establece `bat_response` a partir de `f" | evidencia bateria={fields['BAT_LINK_STATE']} Ibat
        # [Guía] media={mean} respuesta={fields.get('BAT_RESPONSE', 'UNKN…`.
        bat_response = (f" | evidencia bateria={fields['BAT_LINK_STATE']} "
                        # [Guía] Continúa la instrucción con `f"Ibat media={mean} "`.
                        f"Ibat media={mean} "
                        # [Guía] Continúa la instrucción con `f"respuesta={fields.get('BAT_RESPONSE', 'UNKNOWN')}
                        # [Guía] "`.
                        f"respuesta={fields.get('BAT_RESPONSE', 'UNKNOWN')} "
                        # [Guía] Continúa la instrucción con `f"ultimo cambio={fields.get('LAST_SOURCE',
                        # [Guía] 'UNKNOWN')}")`.
                        f"ultimo cambio={fields.get('LAST_SOURCE', 'UNKNOWN')}")
        # [Guía] Evalúa `'IBAT_BASE_OK' in fields` antes de ejecutar las instrucciones de esta rama.
        if 'IBAT_BASE_OK' in fields:
            # [Guía] Establece `baseline` a partir de `f"{int(fields['IBAT_BASE_mA']) / 1000:.3f} A" if
            # [Guía] fields['IBAT_BASE_OK'] == '1' else 'sin referencia'`.
            baseline = (f"{int(fields['IBAT_BASE_mA']) / 1000:.3f} A"
                        # [Guía] Continúa la instrucción con `if fields['IBAT_BASE_OK'] == '1' else "sin
                        # [Guía] referencia")`.
                        if fields['IBAT_BASE_OK'] == '1' else "sin referencia")
            # [Guía] Combina el valor anterior de `bat_response` con `f' base Ibat detector={baseline}'` mediante
            # [Guía] esta operación.
            bat_response += f" base Ibat detector={baseline}"
        # El contador real permite distinguir espera normal de reinicios del
        # detector; Python solo lo presenta, no decide cuando cambiar de modo.
        # [Guía] Evalúa `'BAT_CONFIRM_ms' in fields` antes de ejecutar las instrucciones de esta rama.
        if 'BAT_CONFIRM_ms' in fields:
            # [Guía] Combina el valor anterior de `bat_response` con `f"
            # [Guía] confirmacion={fields['BAT_CONFIRM_ms']}/{fields.get('BAT_CONFIRM_NEED_ms', '1000')} ms
            # [Guía] reinicios={fields.get…` mediante esta operación.
            bat_response += (
                # [Guía] Continúa la instrucción con `f"
                # [Guía] confirmacion={fields['BAT_CONFIRM_ms']}/{fields.get('BAT_CONFIRM_NEED_ms', '1000')} ms"`.
                f" confirmacion={fields['BAT_CONFIRM_ms']}/{fields.get('BAT_CONFIRM_NEED_ms', '1000')} ms"
                # [Guía] Continúa la instrucción con `f" reinicios={fields.get('BAT_CONFIRM_RESETS', '0')}"`.
                f" reinicios={fields.get('BAT_CONFIRM_RESETS', '0')}"
                # [Guía] Continúa la instrucción con `f" causa={fields.get('BAT_CONFIRM_RESET', 'NONE')}"`.
                f" causa={fields.get('BAT_CONFIRM_RESET', 'NONE')}"
            )
    # Distinguir adquisicion/PI/limite; mostrar las ganancias de Q1 sin redondear
    # 0.0072 a0.007 ni confundirlas con el PI de Q3 que tambien reporta la EMS.
    # [Guía] Establece `m1` a partir de `''`.
    m1 = ""
    # [Guía] Evalúa `fields.get('M1_STAGE', 'INACTIVE') != 'INACTIVE'` antes de ejecutar las instrucciones de esta
    # [Guía] rama.
    if fields.get('M1_STAGE', 'INACTIVE') != 'INACTIVE':
        # [Guía] Establece `m1` a partir de `f" | M1 etapa={fields['M1_STAGE']} Vpv EMS={int(fields['VPV_EMS_mV'])
        # [Guía] / 1000:.3f} V e1={int(fields['E1_mV']) /…`.
        m1 = (f" | M1 etapa={fields['M1_STAGE']} "
              # [Guía] Continúa la instrucción con `f"Vpv EMS={int(fields['VPV_EMS_mV']) / 1000:.3f} V "`.
              f"Vpv EMS={int(fields['VPV_EMS_mV']) / 1000:.3f} V "
              # [Guía] Continúa la instrucción con `f"e1={int(fields['E1_mV']) / 1000:.3f} V "`.
              f"e1={int(fields['E1_mV']) / 1000:.3f} V "
              # [Guía] Continúa la instrucción con `f"I1={int(fields['I1_permille']) / 1000:.3f} duty "`.
              f"I1={int(fields['I1_permille']) / 1000:.3f} duty "
              # [Guía] Continúa la instrucción con `f"Kp1={int(fields['KP1_x1e6']) / 1e6:.6f} "`.
              f"Kp1={int(fields['KP1_x1e6']) / 1e6:.6f} "
              # [Guía] Continúa la instrucción con `f"Ki1={int(fields['KI1_x1e6']) / 1e6:.6f}")`.
              f"Ki1={int(fields['KI1_x1e6']) / 1e6:.6f}")
        # V25 separa realimentacion rapida de Q1 y supervision lenta de EMS.
        # [Guía] Evalúa `'BUS_PI1_mV' in fields` antes de ejecutar las instrucciones de esta rama.
        if 'BUS_PI1_mV' in fields:
            # [Guía] Combina el valor anterior de `m1` con `f" Vbus PI Q1={int(fields['BUS_PI1_mV']) / 1000:.3f}
            # [Guía] V"` mediante esta operación.
            m1 += f" Vbus PI Q1={int(fields['BUS_PI1_mV']) / 1000:.3f} V"
        # Reintentos limitados de V24; no presentar recuperacion como PI activo.
        # [Guía] Evalúa `'M1_RETRIES' in fields` antes de ejecutar las instrucciones de esta rama.
        if 'M1_RETRIES' in fields:
            # [Guía] Combina el valor anterior de `m1` con `f" reintentos={fields['M1_RETRIES']}
            # [Guía] limite={fields.get('M1_LIMIT', 'UNKNOWN')}"` mediante esta operación.
            m1 += (f" reintentos={fields['M1_RETRIES']} "
                   # [Guía] Continúa la instrucción con `f"limite={fields.get('M1_LIMIT', 'UNKNOWN')}")`.
                   f"limite={fields.get('M1_LIMIT', 'UNKNOWN')}")
    # V27 expone la accion real de Q3; un PI en minimo no es PWM inhibido.
    # [Guía] Establece `q3` a partir de `''`.
    q3 = ""
    # [Guía] Evalúa `fields.get('MODE') in ('START_M5', 'M5', 'START_M4', 'M4') and 'E3_mV' in fields` antes de
    # [Guía] ejecutar las instrucciones de esta rama.
    if fields.get('MODE') in ('START_M5', 'M5', 'START_M4', 'M4') and 'E3_mV' in fields:
        # [Guía] Establece `q3` a partir de `f" | e3={int(fields['E3_mV']) / 1000:.3f} V
        # [Guía] I3={int(fields['I3_permille']) / 1000:.3f} duty accion3={int(fields…`.
        q3 = (f" | e3={int(fields['E3_mV']) / 1000:.3f} V "
              # [Guía] Continúa la instrucción con `f"I3={int(fields['I3_permille']) / 1000:.3f} duty "`.
              f"I3={int(fields['I3_permille']) / 1000:.3f} duty "
              # [Guía] Continúa la instrucción con `f"accion3={int(fields['ACTION3_x1000']) / 1000:.1f}")`.
              f"accion3={int(fields['ACTION3_x1000']) / 1000:.1f}")
    # Preparar tension no significa que Q2 o el MPPT esten ya habilitados.
    # [Guía] Establece `prep` a partir de `''`.
    prep = ""
    # [Guía] Evalúa `fields.get('MODE') == 'PREP_M3' and 'PV_CHG_MIN_mV' in fields` antes de ejecutar las
    # [Guía] instrucciones de esta rama.
    if fields.get('MODE') == 'PREP_M3' and 'PV_CHG_MIN_mV' in fields:
        # [Guía] Establece `prep` a partir de `f" | preparando M3: Vpv EMS={int(fields['VPV_EMS_mV']) / 1000:.3f}
        # [Guía] V minimo={int(fields['PV_CHG_MIN_mV']) / 100…`.
        prep = (f" | preparando M3: Vpv EMS={int(fields['VPV_EMS_mV']) / 1000:.3f} V"
                # [Guía] Continúa la instrucción con `f" minimo={int(fields['PV_CHG_MIN_mV']) / 1000:.3f} V"`.
                f" minimo={int(fields['PV_CHG_MIN_mV']) / 1000:.3f} V"
                # [Guía] Continúa la instrucción con `f" tiempo={fields.get('PREP_MS', '0')} ms | Q2/Q3
                # [Guía] apagados")`.
                f" tiempo={fields.get('PREP_MS', '0')} ms | Q2/Q3 apagados")
    # [Guía] Evalúa `not m2_active and fields.get('MODE') in ('M4', 'M3', 'TRANSFER') and ('PV_PREP_STAGE' in
    # [Guía] fields)` antes de ejecutar las instrucciones de esta rama.
    if not m2_active and fields.get('MODE') in ('M4', 'M3', 'TRANSFER') and 'PV_PREP_STAGE' in fields:
        # [Guía] Establece `prep` a partir de `f" | preparacion M3={fields['PV_PREP_STAGE']} Vpv
        # [Guía] EMS={int(fields.get('VPV_EMS_mV', '0')) / 1000:.3f} V minimo=…`.
        prep = (f" | preparacion M3={fields['PV_PREP_STAGE']} "
                # [Guía] Continúa la instrucción con `f"Vpv EMS={int(fields.get('VPV_EMS_mV', '0')) / 1000:.3f} V
                # [Guía] "`.
                f"Vpv EMS={int(fields.get('VPV_EMS_mV', '0')) / 1000:.3f} V "
                # [Guía] Continúa la instrucción con `f"minimo={int(fields.get('PV_CHG_MIN_mV', '0')) / 1000:.3f}
                # [Guía] V "`.
                f"minimo={int(fields.get('PV_CHG_MIN_mV', '0')) / 1000:.3f} V "
                # [Guía] Continúa la instrucción con `f"objetivo={int(fields.get('PV_PREP_TARGET_mV', '0')) /
                # [Guía] 1000:.3f} V "`.
                f"objetivo={int(fields.get('PV_PREP_TARGET_mV', '0')) / 1000:.3f} V "
                # [Guía] Continúa la instrucción con `f"margen PV-BAT={int(fields.get('PV_GAP_mV', '0')) /
                # [Guía] 1000:.3f} V "`.
                f"margen PV-BAT={int(fields.get('PV_GAP_mV', '0')) / 1000:.3f} V "
                # [Guía] Continúa la instrucción con `f"intentos={fields.get('CHG_TRIES', '0')}/2 "`.
                f"intentos={fields.get('CHG_TRIES', '0')}/2 "
                # [Guía] Continúa la instrucción con `f"preparaciones={fields.get('PREP_TRIES', '0')}/2 "`.
                f"preparaciones={fields.get('PREP_TRIES', '0')}/2 "
                # [Guía] Continúa la instrucción con `f"tiempo={fields.get('PREP_MS', '0')} ms "`.
                f"tiempo={fields.get('PREP_MS', '0')} ms "
                # [Guía] Continúa la instrucción con `f"ultimo={fields.get('PV_PREP_LAST', 'NONE')}")`.
                f"ultimo={fields.get('PV_PREP_LAST', 'NONE')}")
    # [Guía] Establece `q2` a partir de `''`.
    q2 = ""
    # [Guía] Evalúa `not m2_active and fields.get('MODE') in ('M3', 'TRANSFER') and ('Q2_FLOW_SEEN' in fields)`
    # [Guía] antes de ejecutar las instrucciones de esta rama.
    if not m2_active and fields.get('MODE') in ('M3', 'TRANSFER') and 'Q2_FLOW_SEEN' in fields:
        # [Guía] Establece `q2` a partir de `f" | Q2 carga previa confirmada={fields['Q2_FLOW_SEEN']} margen de
        # [Guía] actuacion={fields.get('Q2_DRIVE_OK', '0')} I…`.
        q2 = (f" | Q2 carga previa confirmada={fields['Q2_FLOW_SEEN']} "
              # [Guía] Continúa la instrucción con `f"margen de actuacion={fields.get('Q2_DRIVE_OK', '0')} "`.
              f"margen de actuacion={fields.get('Q2_DRIVE_OK', '0')} "
              # [Guía] Continúa la instrucción con `f"Ibat base={int(fields.get('Q2_IDLE_mA', '0')) / 1000:.3f}
              # [Guía] A")`.
              f"Ibat base={int(fields.get('Q2_IDLE_mA', '0')) / 1000:.3f} A")
    # [Guía] Entrega f"EMS {fields.get('MODE')} | {fields.get('STATE')} | {duties} | MPPT={fields.get('MPPT')}
    # [Guía] fallo={fields.get('FA… al llamador y termina la función.
    return (f"EMS {fields.get('MODE')} | {fields.get('STATE')} | {duties} "
            # [Guía] Continúa la instrucción con `f"| MPPT={fields.get('MPPT')} fallo={fields.get('FAULT')} "`.
            f"| MPPT={fields.get('MPPT')} fallo={fields.get('FAULT')} "
            # [Guía] Continúa la instrucción con `f"| orden={fields.get('COMMAND')} "`.
            f"| orden={fields.get('COMMAND')} "
            # [Guía] Continúa la instrucción con `f"| Kp3={int(fields['KP3_x1e6']) / 1e6:.6f}
            # [Guía] Ki3={int(fields['KI3_x1e6']) / 1e6:.6f}"`.
            f"| Kp3={int(fields['KP3_x1e6']) / 1e6:.6f} Ki3={int(fields['KI3_x1e6']) / 1e6:.6f}"
            # [Guía] Continúa la instrucción con
            # [Guía] `f"{version}{voltage}{reference}{updates}{guard}{bus_ems}{mppt_window}{bat_link}{bat_auth}{bat_response}{m1}{q3}…`.
            f"{version}{voltage}{reference}{updates}{guard}{bus_ems}{mppt_window}{bat_link}{bat_auth}{bat_response}{m1}{q3}{prep}{q2}{m2_wait}")


# [Guía] Decision real de P&O; MPPT=1 por si solo no prueba convergencia al MPP. Recibe `line`.
def mppt_display(line):
    """Decision real de P&O; MPPT=1 por si solo no prueba convergencia al MPP."""
    # [Guía] Conserva en `fields` el resultado de `dict` con los argumentos indicados.
    fields = dict(re.findall(r"([A-Za-z_0-9]+)=([^\s]+)", line))
    # [Guía] Conserva en `decision` el resultado de `fields.get` con los argumentos indicados.
    decision = fields.get("DECISION", "UNKNOWN")
    # [Guía] Evalúa `decision == 'INACTIVE'` antes de ejecutar las instrucciones de esta rama.
    if decision == "INACTIVE":
        # [Guía] Entrega 'MPPT decision=INACTIVE' al llamador y termina la función.
        return "MPPT decision=INACTIVE"
    # V59 usa una referencia PV: no mostrar su paso de tension como pp de PWM.
    # [Guía] Establece `step_display` a partir de `f"Vpv referencia={int(fields.get('VREF_mV', '0')) / 1000:.3f} V
    # [Guía] paso={int(fields.get('VSTEP_mV', '0')) / 1000:.…`.
    step_display = (
        # [Guía] Continúa la instrucción con `f"Vpv referencia={int(fields.get('VREF_mV', '0')) / 1000:.3f} V "`.
        f"Vpv referencia={int(fields.get('VREF_mV', '0')) / 1000:.3f} V "
        # [Guía] Continúa la instrucción con `f"paso={int(fields.get('VSTEP_mV', '0')) / 1000:.3f} V "`.
        f"paso={int(fields.get('VSTEP_mV', '0')) / 1000:.3f} V "
        # [Guía] Continúa la instrucción con `if fields.get('CONTROL') == 'VPV_REFERENCE' else`.
        if fields.get('CONTROL') == 'VPV_REFERENCE' else
        # [Guía] Continúa la instrucción con `f"paso={int(fields.get('STEP_ppm', '0')) / 10000:.3f} pp "`.
        f"paso={int(fields.get('STEP_ppm', '0')) / 10000:.3f} pp "
    )
    # [Guía] Entrega f"MPPT decision={decision} {step_display}Ppv ventana={int(fields.get('P_mW', '0')) /
    # [Guía] 1000:.3f} W deltaP={int(fi… al llamador y termina la función.
    return (f"MPPT decision={decision} "
            # [Guía] Continúa la instrucción con `f"{step_display}"`.
            f"{step_display}"
            # [Guía] Continúa la instrucción con `f"Ppv ventana={int(fields.get('P_mW', '0')) / 1000:.3f} W "`.
            f"Ppv ventana={int(fields.get('P_mW', '0')) / 1000:.3f} W "
            # [Guía] Continúa la instrucción con `f"deltaP={int(fields.get('DP_mW', '0')) / 1000:.3f} W "`.
            f"deltaP={int(fields.get('DP_mW', '0')) / 1000:.3f} W "
            # [Guía] Continúa la instrucción con `f"base={int(fields.get('BASE_mW', '0')) / 1000:.3f} W "`.
            f"base={int(fields.get('BASE_mW', '0')) / 1000:.3f} W "
            # [Guía] Continúa la instrucción con `f"objetivo Q1={int(fields.get('TARGET_ppm', '0')) / 10000:.2f}%
            # [Guía] "`.
            f"objetivo Q1={int(fields.get('TARGET_ppm', '0')) / 10000:.2f}% "
            # [Guía] Continúa la instrucción con `f"avances planos={fields.get('FLAT_N', '0')} "`.
            f"avances planos={fields.get('FLAT_N', '0')} "
            # [Guía] Continúa la instrucción con `f"cambios={fields.get('UPDATES', '0')}")`.
            f"cambios={fields.get('UPDATES', '0')}")


# [Guía] Evento ADC pasivo: no llamarlo ruido ni corriente fisica confirmada. Recibe `line`.
def connection_display(line):
    """Evento ADC pasivo: no llamarlo ruido ni corriente fisica confirmada."""
    # [Guía] Conserva en `fields` el resultado de `dict` con los argumentos indicados.
    fields = dict(re.findall(r"([A-Za-z_0-9]+)=([^\s]+)", line))
    # [Guía] Evalúa `int(fields.get('N', 0)) == 0` antes de ejecutar las instrucciones de esta rama.
    if int(fields.get("N", 0)) == 0:
        # [Guía] Entrega None al llamador y termina la función.
        return None
    # [Guía] Entrega f"CONEXION | {fields.get('STATE')} | muestras sobre limite={fields['N']} | maximo convertido
    # [Guía] ADC={int(fields['I… al llamador y termina la función.
    return (f"CONEXION | {fields.get('STATE')} | muestras sobre limite={fields['N']} "
            # [Guía] Continúa la instrucción con `f"| maximo convertido ADC={int(fields['Imax_mA']) / 1000:.3f} A
            # [Guía] "`.
            f"| maximo convertido ADC={int(fields['Imax_mA']) / 1000:.3f} A "
            # [Guía] Continúa la instrucción con `f"| primer ADC Ibat={fields['ADC_Ibat']} | No confirma corriente
            # [Guía] fisica ni ruido.")`.
            f"| primer ADC Ibat={fields['ADC_Ibat']} | No confirma corriente fisica ni ruido.")


# [Guía] Muestra enclavada del fallo, no la media posterior a apagar los PWM. Recibe `line`.
def trip_display(line):
    """Muestra enclavada del fallo, no la media posterior a apagar los PWM."""
    # [Guía] Conserva en `fields` el resultado de `dict` con los argumentos indicados.
    fields = dict(re.findall(r"([A-Za-z_0-9]+)=([^\s]+)", line))
    # [Guía] Evalúa `fields.get('VALID') != '1'` antes de ejecutar las instrucciones de esta rama.
    if fields.get("VALID") != "1":
        # [Guía] Entrega None al llamador y termina la función.
        return None
    # FINITE es mascara de bits: impide presentar NaN convertido como0fisico.
    # La instantanea corresponde al instante previo al corte de los PWM.
    # [Guía] Conserva en `finite` el resultado de `int` con los argumentos indicados.
    finite = int(fields.get("FINITE", 0))
    # [Guía] Presenta un valor capturado en el disparo solo cuando su bit de validez está activo. Recibe `key`,
    # [Guía] `bit`, `unit`.
    def measured(key, bit, unit):
        # [Guía] Entrega f'{int(fields[key]) / 1000:.3f} {unit}' if finite & bit else 'invalida' al llamador y
        # [Guía] termina la función.
        return f"{int(fields[key]) / 1000:.3f} {unit}" if finite & bit else "invalida"
    # [Guía] Conserva en `duties` el resultado de `duty_display` con los argumentos indicados.
    duties = duty_display([int(fields[f'D{n}_permille']) for n in (1, 2, 3)])
    # Campos opcionales para conservar lectura de registros anteriores a V20.
    # [Guía] Establece `pv` a partir de `f" | Vpv cruda={measured('Vpv_mV', 16, 'V')} Ipv
    # [Guía] cruda={measured('Ipv_raw_mA', 32, 'A')} filtrada={measured('Ip…`.
    pv = (f" | Vpv cruda={measured('Vpv_mV', 16, 'V')} "
          # [Guía] Continúa la instrucción con `f"Ipv cruda={measured('Ipv_raw_mA', 32, 'A')} "`.
          f"Ipv cruda={measured('Ipv_raw_mA', 32, 'A')} "
          # [Guía] Continúa la instrucción con `f"filtrada={measured('Ipv_filt_mA', 64, 'A')}"`.
          f"filtrada={measured('Ipv_filt_mA', 64, 'A')}"
          # [Guía] Continúa la instrucción con `if 'Vpv_mV' in fields else "")`.
          if 'Vpv_mV' in fields else "")
    # [Guía] Entrega f"DISPARO {fields.get('REASON')} | micros={int(fields['TIME_us']) / 1000000.0:.6f} s | Ibat
    # [Guía] cruda={measured('Ir… al llamador y termina la función.
    return (f"DISPARO {fields.get('REASON')} | micros={int(fields['TIME_us']) / 1e6:.6f} s "
            # [Guía] Continúa la instrucción con `f"| Ibat cruda={measured('Iraw_mA', 1, 'A')} "`.
            f"| Ibat cruda={measured('Iraw_mA', 1, 'A')} "
            # [Guía] Continúa la instrucción con `f"filtrada={measured('Ifilt_mA', 2, 'A')} "`.
            f"filtrada={measured('Ifilt_mA', 2, 'A')} "
            # [Guía] Continúa la instrucción con `f"| Vbus={measured('Vbus_mV', 4, 'V')} Vbat={measured('Vbat_mV',
            # [Guía] 8, 'V')} "`.
            f"| Vbus={measured('Vbus_mV', 4, 'V')} Vbat={measured('Vbat_mV', 8, 'V')} "
            # [Guía] Continúa la instrucción con `f"| PWM solicitado antes del corte: {duties} "`.
            f"| PWM solicitado antes del corte: {duties} "
            # [Guía] Continúa la instrucción con `f"| ADC Ibat={fields.get('ADC_Ibat')}
            # [Guía] bus={fields.get('ADC_Vbus')} bat={fields.get('ADC_Vbat')} "`.
            f"| ADC Ibat={fields.get('ADC_Ibat')} bus={fields.get('ADC_Vbus')} bat={fields.get('ADC_Vbat')} "
            # [Guía] Continúa la instrucción con `f"ceroIbat={int(fields['ZERO_Ibat_x1000']) / 1000:.3f}{pv}")`.
            f"ceroIbat={int(fields['ZERO_Ibat_x1000']) / 1000:.3f}{pv}")


# [Guía] Recoge entradas de consola y las entrega al lazo principal por una cola. Recibe `commands`.
def console_reader(commands):
    """Entrada humana en un hilo separado; los datos siguen llegando al monitor."""
    # [Guía] Repite el bloque mientras se cumpla `True`.
    while True:
        # [Guía] Delimita la operación cuyos errores o cierre se gestionan en las ramas siguientes.
        try:
            # [Guía] Conserva en `command` el resultado de `normalize_input` con los argumentos indicados.
            command = normalize_input(input())
        # [Guía] Atiende las excepciones que coinciden con `except EOFError` en el bloque anterior.
        except EOFError:
            # [Guía] Establece `command` a partir de `'SALIR'`.
            command = "SALIR"
        # [Guía] Atiende las excepciones que coinciden con `except ValueError` en el bloque anterior.
        except ValueError:
            # [Guía] Ejecuta print con los argumentos de esta llamada.
            print("Usa R=200, SOC=70, EMS_START/EMS_RESET, X, OFF, AUTO_R/MANUAL, S/M o SALIR; el perfil de ensayo bloquea ON/AUTO_R/S/M.", flush=True)
            # [Guía] Pasa a la siguiente iteración del bucle.
            continue
        # El lector nunca escribe COM: main serializa la orden con el protocolo.
        # [Guía] Ejecuta commands.put con los argumentos de esta llamada.
        commands.put(command)
        # [Guía] Evalúa `command == 'SALIR'` antes de ejecutar las instrucciones de esta rama.
        if command == "SALIR":
            # [Guía] Termina la función en este punto.
            return


# [Guía] Atiende la cola de operaciones B&K en un hilo y comunica resultados correlacionados al lazo de
# [Guía] comunicaciones. Recibe `bk`, `jobs`, `results`.
def load_worker(bk, jobs, results):
    """Unico propietario del puerto BK; salida y ACK vuelven al hilo USB."""
    # confirmed solo pertenece a esta sesion. Haber inicializado4000ohm no
    # autoriza un ON hasta que una orden R correlacionada sea releida con exito.
    # [Guía] Establece `confirmed` a partir de `False`.
    confirmed = False
    # Una respuesta USB puede perderse aunque la BK haya aceptado la orden.
    # Reenviar el ACK del mismo ID, no repetir la actuacion fisica. Sesion
    # nueva (SYNC) borra esta memoria; IDs viejos nunca pueden restaurar ON.
    # [Guía] Establece `last_job, last_ack` a partir de `None`.
    last_job = last_ack = None
    # [Guía] Delimita la operación cuyos errores o cierre se gestionan en las ramas siguientes.
    try:
        # [Guía] Repite el bloque mientras se cumpla `True`.
        while True:
            # [Guía] Conserva en `job` el resultado de `jobs.get` con los argumentos indicados.
            job = jobs.get()  # Bloquea este hilo BK, no la recepcion USB.
            # [Guía] Evalúa `job is None` antes de ejecutar las instrucciones de esta rama.
            if job is None:
                # [Guía] Termina la función en este punto.
                return
            # [Guía] Establece `(order_id, kind, value)` a partir de `job`.
            order_id, kind, value = job
            # [Guía] Delimita la operación cuyos errores o cierre se gestionan en las ramas siguientes.
            try:
                # SYNC vuelve a OFF antes de anunciar enlace al Arduino.
                # ACK retorna por results; nunca escribir Arduino desde worker.
                # [Guía] Evalúa `kind == 'SYNC'` antes de ejecutar las instrucciones de esta rama.
                if kind == "SYNC":
                    # [Guía] Ejecuta bk.byte_command con los argumentos de esta llamada.
                    bk.byte_command(0x21, 0)
                    # [Guía] Establece `confirmed` a partir de `False`.
                    confirmed = False
                    # [Guía] Establece `last_job, last_ack` a partir de `None`.
                    last_job = last_ack = None
                    # [Guía] Ejecuta results.put con los argumentos de esta llamada.
                    results.put(("SYNC", "BK_HELLO"))
                # [Guía] Ejecuta este bloque cuando se cumple la alternativa del bloque anterior.
                else:
                    # [Guía] Evalúa `last_job is not None` antes de ejecutar las instrucciones de esta rama.
                    if last_job is not None:
                        # [Guía] Evalúa `order_id == last_job[0]` antes de ejecutar las instrucciones de esta
                        # [Guía] rama.
                        if order_id == last_job[0]:
                            # [Guía] Evalúa `job != last_job` antes de ejecutar las instrucciones de esta rama.
                            if job != last_job:
                                # [Guía] Señala RuntimeError('ID BK repetido con otro contenido') para que la capa
                                # [Guía] que llamó pueda tratar el problema.
                                raise RuntimeError("ID BK repetido con otro contenido")
                            # [Guía] Ejecuta results.put con los argumentos de esta llamada.
                            results.put(("ACK", last_ack))
                            # [Guía] Pasa a la siguiente iteración del bucle.
                            continue
                        # Comparacion modular uint32 compatible con rollover del
                        # Arduino. Una retransmision vieja tras OFF no hace ON.
                        # [Guía] Evalúa `not 0 < order_id - last_job[0] & 4294967295 < 2147483648` antes de
                        # [Guía] ejecutar las instrucciones de esta rama.
                        if not 0 < ((order_id - last_job[0]) & 0xFFFFFFFF) < 0x80000000:
                            # [Guía] Ejecuta results.put con los argumentos de esta llamada.
                            results.put(("INFO", f"BK_REQ antiguo ignorado: {order_id}"))
                            # [Guía] Pasa a la siguiente iteración del bucle.
                            continue
                    # [Guía] Evalúa `kind == 'R'` antes de ejecutar las instrucciones de esta rama.
                    if kind == "R":
                        # [Guía] Conserva en `actual` el resultado de `bk.set_resistance` con los argumentos
                        # [Guía] indicados.
                        actual = bk.set_resistance(value)
                        # [Guía] Establece `confirmed` a partir de `True`.
                        confirmed = True
                        # [Guía] Establece `ack` a partir de `f'BK_ACK={order_id},R,{actual:.3f}'`.
                        ack = f"BK_ACK={order_id},R,{actual:.3f}"
                    # [Guía] Ejecuta este bloque cuando se cumple la alternativa del bloque anterior.
                    else:
                        # [Guía] Evalúa `value and (not confirmed)` antes de ejecutar las instrucciones de esta
                        # [Guía] rama.
                        if value and not confirmed:
                            # [Guía] Señala RuntimeError('ON sin setpoint confirmado en esta sesion') para que la
                            # [Guía] capa que llamó pueda tratar el problema.
                            raise RuntimeError("ON sin setpoint confirmado en esta sesion")
                        # [Guía] Ejecuta bk.byte_command con los argumentos de esta llamada.
                        bk.byte_command(0x21, int(value))
                        # [Guía] Establece `ack` a partir de `f'BK_ACK={order_id},LOAD,{int(value)}'`.
                        ack = f"BK_ACK={order_id},LOAD,{int(value)}"
                    # [Guía] Establece `(last_job, last_ack)` a partir de `(job, ack)`.
                    last_job, last_ack = job, ack
                    # [Guía] Ejecuta results.put con los argumentos de esta llamada.
                    results.put(("ACK", ack))
            # [Guía] Atiende las excepciones que coinciden con `except Exception as error` en el bloque anterior.
            except Exception as error:
                # Cualquier transaccion incierta invalida autorizacion de ON.
                # El hilo principal recibe el error y termina la sesion.
                # [Guía] Establece `confirmed` a partir de `False`.
                confirmed = False
                # [Guía] Ejecuta results.put con los argumentos de esta llamada.
                results.put(("ERR", f"BK_ERR={order_id}"))
                # [Guía] Ejecuta results.put con los argumentos de esta llamada.
                results.put(("FATAL", str(error)))
                # [Guía] Termina la función en este punto.
                return
    # [Guía] Ejecuta este cierre tanto si la operación termina normalmente como si produce una excepción.
    finally:
        # Cierre normal, fallo o KeyboardInterrupt del hilo principal: intentar OFF.
        # [Guía] Delimita la operación cuyos errores o cierre se gestionan en las ramas siguientes.
        try:
            # [Guía] Ejecuta bk.byte_command con los argumentos de esta llamada.
            bk.byte_command(0x21, 0)
            # [Guía] Ejecuta bk.byte_command con los argumentos de esta llamada.
            bk.byte_command(0x20, 0)
            # [Guía] Ejecuta results.put con los argumentos de esta llamada.
            results.put(("INFO", "LOAD OFF y LOCAL aceptados."))
        # [Guía] Atiende las excepciones que coinciden con `except Exception as error` en el bloque anterior.
        except Exception as error:
            # [Guía] Ejecuta results.put con los argumentos de esta llamada.
            results.put(("INFO", f"No se confirmo OFF/LOCAL: {error}"))


# [Guía] Abre los enlaces, coordina órdenes, transacciones, avisos de vigencia y telemetría, y ejecuta el cierre
# [Guía] de la sesión.
def main():
    """Monitor vivo: lectura USB acotada, PING y cola de transacciones BK."""
    # Importacion tardia: permite usar formateadores y pruebas sin pyserial ni
    # puertos fisicos. Solo main necesita realmente abrir los dispositivos.
    # [Guía] Importa serial para las operaciones que realiza este módulo.
    import serial
    # [Guía] Importa list_ports para las operaciones que realiza este módulo.
    from serial.tools import list_ports
    # [Guía] Conserva en `parser` el resultado de `argparse.ArgumentParser` con los argumentos indicados.
    parser = argparse.ArgumentParser(description=__doc__)
    # [Guía] Ejecuta parser.add_argument con los argumentos de esta llamada.
    parser.add_argument("--list", action="store_true")
    # [Guía] Ejecuta parser.add_argument con los argumentos de esta llamada.
    parser.add_argument("--arduino", default=PUERTO_ARDUINO)
    # [Guía] Ejecuta parser.add_argument con los argumentos de esta llamada.
    parser.add_argument("--carga", default=PUERTO_CARGA)
    # [Guía] Ejecuta parser.add_argument con los argumentos de esta llamada.
    parser.add_argument("--baud-carga", type=int, default=9600, choices=[4800, 9600, 19200, 38400])
    # [Guía] Ejecuta parser.add_argument con los argumentos de esta llamada.
    parser.add_argument("--address", type=int, default=0)
    # [Guía] Ejecuta parser.add_argument con los argumentos de esta llamada.
    parser.add_argument("--raw", action="store_true", help="Mostrar todas las lineas de diagnostico")
    # [Guía] Ejecuta parser.add_argument con los argumentos de esta llamada.
    parser.add_argument("--log", metavar="ARCHIVO", help="Guardar lineas recibidas con hora UTC, sin cambiar el control")
    # [Guía] Conserva en `args` el resultado de `parser.parse_args` con los argumentos indicados.
    args = parser.parse_args()
    # [Guía] Evalúa `args.list` antes de ejecutar las instrucciones de esta rama.
    if args.list:
        # [Guía] Recorre list_ports.comports(). En cada iteración, port recibe el elemento correspondiente.
        for port in list_ports.comports():
            # [Guía] Ejecuta print con los argumentos de esta llamada.
            print(f"{port.device}: {port.description}")
        # [Guía] Entrega 0 al llamador y termina la función.
        return 0
    # [Guía] Evalúa `args.arduino.lower() == args.carga.lower() or not 0 <= args.address <= 254` antes de ejecutar
    # [Guía] las instrucciones de esta rama.
    if args.arduino.lower() == args.carga.lower() or not 0 <= args.address <= 254:
        # [Guía] Ejecuta parser.error con los argumentos de esta llamada.
        parser.error("Usa puertos distintos y direccion 0..254")
    # [Guía] Establece `load_port, arduino, bk, worker` a partir de `None`.
    load_port = arduino = bk = worker = None
    # [Guía] Establece `log_file` a partir de `None`.
    log_file = None
    # jobs limita solicitudes pendientes para no acumular consignas antiguas.
    # results contiene respuestas BK; commands contiene entradas del operador.
    # [Guía] Establece `(jobs, results, commands)` a partir de `(queue.Queue(maxsize=8), queue.Queue(),
    # [Guía] queue.Queue())`.
    jobs, results, commands = queue.Queue(maxsize=8), queue.Queue(), queue.Queue()
    # [Guía] Establece `exit_code` a partir de `0`.
    exit_code = 0
    # [Guía] Delimita la operación cuyos errores o cierre se gestionan en las ramas siguientes.
    try:
        # [Guía] Evalúa `args.log` antes de ejecutar las instrucciones de esta rama.
        if args.log:
            # [Guía] Conserva en `log_file` el resultado de `open` con los argumentos indicados.
            log_file = open(args.log, "a", encoding="utf-8", buffering=1)
        # Abrir/configurar BK antes de USB Arduino. Una apertura USB puede
        # reiniciar algunas placas; la calibracion inicial requiere corriente0.
        # [Guía] Conserva en `load_port` el resultado de `serial.Serial` con los argumentos indicados.
        load_port = serial.Serial(args.carga, args.baud_carga, timeout=0.1, write_timeout=2)
        # [Guía] Establece `load_port.dtr, load_port.rts` a partir de `True`.
        load_port.dtr = load_port.rts = True
        # [Guía] Conserva en `bk` el resultado de `BK8502` con los argumentos indicados.
        bk = BK8502(load_port, args.address)
        # [Guía] Ejecuta bk.initialize con los argumentos de esta llamada.
        bk.initialize()
        # [Guía] Conserva en `arduino` el resultado de `serial.Serial` con los argumentos indicados.
        arduino = serial.Serial(args.arduino, 115200, timeout=0.02, write_timeout=1)
        # Un solo worker es propietario de BK tras initialize. El hilo principal
        # atiende USB y no espera las transacciones de hasta2s del instrumento.
        # [Guía] Conserva en `worker` el resultado de `threading.Thread` con los argumentos indicados.
        worker = threading.Thread(target=load_worker, args=(bk, jobs, results), daemon=True)
        # [Guía] Ejecuta worker.start con los argumentos de esta llamada.
        worker.start()
        # [Guía] Ejecuta jobs.put_nowait con los argumentos de esta llamada.
        jobs.put_nowait((0, "SYNC", 0))
        # [Guía] Establece `syncing` a partir de `True`.
        syncing = True
        # [Guía] Ejecuta print con los argumentos de esta llamada.
        print("BK en CR/FIXED, OFF. Arduino solicita 4000 ohm al arrancar. AUTO_R reinicia busqueda; MANUAL pausa sin apagar.", flush=True)
        # [Guía] Ejecuta print con los argumentos de esta llamada.
        print("ENABLE/DISABLE: permiso. S/M/X: PI/MPPT/parada. Monitor Serie IDE cerrado.", flush=True)
        # [Guía] Ejecuta print con los argumentos de esta llamada.
        print("SOC=70 introduce 70% inicial, estimado externamente con la bateria en reposo. No convierte voltaje a SOC.", flush=True)
        # [Guía] Ejecuta print con los argumentos de esta llamada.
        print("Perfil EMS: arranque automatico; el Arduino informa modo y evidencia de fuentes, no la posicion fisica del interruptor. EMS_RESET rearma fallo; EMS_START reinicia espera. X apaga todos los Q.", flush=True)
        # [Guía] Ejecuta print con los argumentos de esta llamada.
        print("AVISO: limite BK solicitado=40 V; I/P sin ampliar. Recuperacion pide carga sin apagar EMS; no es proteccion fisica. OV_RESET libera el enclavamiento con bus recuperado.", flush=True)
        # [Guía] Ejecuta threading.Thread(target=console_reader, args=(commands,), daemon=True).start con los
        # [Guía] argumentos de esta llamada.
        threading.Thread(target=console_reader, args=(commands,), daemon=True).start()
        # PING cada2s conserva enlace. Telemetria ausente12s termina el puente;
        # ese timeout del PC no reemplaza los tiempos de proteccion del Arduino.
        # [Guía] Conserva en `last_data, last_ping` el resultado de `time.monotonic` con los argumentos indicados.
        last_data = last_ping = time.monotonic()
        # [Guía] Conserva en `buffer` el resultado de `bytearray` con los argumentos indicados.
        buffer = bytearray()
        # [Guía] Establece `last_trip` a partir de `None`.
        last_trip = None
        # [Guía] Establece `last_connection` a partir de `None`.
        last_connection = None
        # [Guía] Establece `running` a partir de `True`.
        running = True
        # [Guía] Repite el bloque mientras se cumpla `running`.
        while running:
            # [Guía] Conserva en `now` el resultado de `time.monotonic` con los argumentos indicados.
            now = time.monotonic()
            # [Guía] Evalúa `now - last_data > 12` antes de ejecutar las instrucciones de esta rama.
            if now - last_data > 12:
                # [Guía] Señala RuntimeError('Arduino sin telemetria durante 12 s') para que la capa que llamó
                # [Guía] pueda tratar el problema.
                raise RuntimeError("Arduino sin telemetria durante 12 s")
            # [Guía] Evalúa `now - last_ping >= 2` antes de ejecutar las instrucciones de esta rama.
            if now - last_ping >= 2:
                # [Guía] Ejecuta arduino.write con los argumentos de esta llamada.
                arduino.write(b"BK_PING\n")
                # [Guía] Establece `last_ping` a partir de `now`.
                last_ping = now
            # Presupuesto de8resultados por vuelta para no privar a USB/teclado.
            # [Guía] Recorre range(8). En cada iteración, _ recibe el elemento correspondiente.
            for _ in range(8):
                # [Guía] Delimita la operación cuyos errores o cierre se gestionan en las ramas siguientes.
                try:
                    # [Guía] Conserva en `(kind, value)` el resultado de `results.get_nowait` con los argumentos
                    # [Guía] indicados.
                    kind, value = results.get_nowait()
                # [Guía] Atiende las excepciones que coinciden con `except queue.Empty` en el bloque anterior.
                except queue.Empty:
                    # [Guía] Termina el bucle y sigue después de él.
                    break
                # [Guía] Evalúa `kind in ('ACK', 'ERR', 'SYNC')` antes de ejecutar las instrucciones de esta rama.
                if kind in ("ACK", "ERR", "SYNC"):
                    # [Guía] Ejecuta arduino.write con los argumentos de esta llamada.
                    arduino.write((value + "\n").encode("ascii"))
                    # [Guía] Evalúa `kind != 'SYNC'` antes de ejecutar las instrucciones de esta rama.
                    if kind != "SYNC":
                        # [Guía] Ejecuta print con los argumentos de esta llamada.
                        print(value, flush=True)
                # [Guía] Evalúa `kind == 'FATAL'` antes de ejecutar las instrucciones de esta rama.
                elif kind == "FATAL":
                    # [Guía] Señala RuntimeError(value) para que la capa que llamó pueda tratar el problema.
                    raise RuntimeError(value)
                # [Guía] Ejecuta este bloque cuando se cumple la alternativa del bloque anterior.
                else:
                    # [Guía] Ejecuta print con los argumentos de esta llamada.
                    print(value, flush=True)
            # [Guía] Recorre range(8). En cada iteración, _ recibe el elemento correspondiente.
            for _ in range(8):
                # [Guía] Delimita la operación cuyos errores o cierre se gestionan en las ramas siguientes.
                try:
                    # [Guía] Conserva en `command` el resultado de `commands.get_nowait` con los argumentos
                    # [Guía] indicados.
                    command = commands.get_nowait()
                # [Guía] Atiende las excepciones que coinciden con `except queue.Empty` en el bloque anterior.
                except queue.Empty:
                    # [Guía] Termina el bucle y sigue después de él.
                    break
                # [Guía] Evalúa `command == 'SALIR'` antes de ejecutar las instrucciones de esta rama.
                if command == "SALIR":
                    # [Guía] Establece `running` a partir de `False`.
                    running = False
                    # [Guía] Termina el bucle y sigue después de él.
                    break
                # [Guía] Ejecuta arduino.write con los argumentos de esta llamada.
                arduino.write((command + "\n").encode("ascii"))
                # [Guía] Ejecuta print con los argumentos de esta llamada.
                print(f"PC -> Arduino: {command}", flush=True)
            # [Guía] Evalúa `not running` antes de ejecutar las instrucciones de esta rama.
            if not running:
                # [Guía] Termina el bucle y sigue después de él.
                break
            # USB no conserva fronteras de linea. Acumular fragmentos hasta\n,
            # con limite de bloque y limite separado por linea para memoria.
            # [Guía] Ejecuta buffer.extend con los argumentos de esta llamada.
            buffer.extend(arduino.read(min(arduino.in_waiting or 1, 4096)))
            # Limite POR LINEA, no por bloque USB: el reporte supera 256 bytes.
            # [Guía] Repite el bloque mientras se cumpla `b'\n' in buffer`.
            while b"\n" in buffer:
                # [Guía] Conserva en `(raw, _, remaining)` el resultado de `buffer.partition` con los argumentos
                # [Guía] indicados.
                raw, _, remaining = buffer.partition(b"\n")
                # [Guía] Conserva en `buffer` el resultado de `bytearray` con los argumentos indicados.
                buffer = bytearray(remaining)
                # [Guía] Evalúa `len(raw) > 2048` antes de ejecutar las instrucciones de esta rama.
                if len(raw) > 2048:
                    # [Guía] Señala RuntimeError('Linea Arduino demasiado larga') para que la capa que llamó pueda
                    # [Guía] tratar el problema.
                    raise RuntimeError("Linea Arduino demasiado larga")
                # [Guía] Conserva en `line` el resultado de `raw.decode('ascii').strip` con los argumentos
                # [Guía] indicados.
                line = raw.decode("ascii").strip()
                # Guardar ANTES de formatear/deduplicar. El log raw conserva
                # cada linea Arduino y UTC; no solo lo visible en la consola.
                # [Guía] Evalúa `log_file` antes de ejecutar las instrucciones de esta rama.
                if log_file:
                    # [Guía] Ejecuta log_file.write con los argumentos de esta llamada.
                    log_file.write(f"{datetime.now(timezone.utc).isoformat()} {line}\n")
                # Separar solicitudes ejecutables de datos observables. Solo
                # BK_REQ pasa a jobs; EMS/OBS/TRIP jamas accionan BK desde PC.
                # [Guía] Evalúa `line.startswith('BK_REQ=')` antes de ejecutar las instrucciones de esta rama.
                if line.startswith("BK_REQ="):
                    # [Guía] Ejecuta jobs.put_nowait con los argumentos de esta llamada.
                    jobs.put_nowait(parse_request(line))
                # [Guía] Evalúa `line.startswith('LIVE ')` antes de ejecutar las instrucciones de esta rama.
                elif line.startswith("LIVE "):
                    # [Guía] Conserva en `last_data` el resultado de `time.monotonic` con los argumentos
                    # [Guía] indicados.
                    last_data = time.monotonic()
                    # [Guía] Evalúa `args.raw` antes de ejecutar las instrucciones de esta rama.
                    if args.raw:
                        # [Guía] Ejecuta print con los argumentos de esta llamada.
                        print(line, flush=True)
                # [Guía] Evalúa `line.startswith('BK_STATE ')` antes de ejecutar las instrucciones de esta rama.
                elif line.startswith("BK_STATE "):
                    # [Guía] Conserva en `last_data` el resultado de `time.monotonic` con los argumentos
                    # [Guía] indicados.
                    last_data = time.monotonic()
                    # El dashboard necesita flags ACK originales, no inferirlos
                    # del texto humano. La consola habitual conserva su formato.
                    # [Guía] Ejecuta print con los argumentos de esta llamada.
                    print(line if args.raw else emulator_display(line), flush=True)
                    # [Guía] Evalúa `'LINK=1 ' in line` antes de ejecutar las instrucciones de esta rama.
                    if "LINK=1 " in line:
                        # [Guía] Establece `syncing` a partir de `False`.
                        syncing = False
                    # Reset Arduino o enlace perdido: confirmar OFF, despues HELLO.
                    # [Guía] Evalúa `'LINK=0 ' in line and (not syncing)` antes de ejecutar las instrucciones de
                    # [Guía] esta rama.
                    if "LINK=0 " in line and not syncing:
                        # [Guía] Establece `syncing` a partir de `True`.
                        syncing = True
                        # [Guía] Ejecuta jobs.put_nowait con los argumentos de esta llamada.
                        jobs.put_nowait((0, "SYNC", 0))
                # [Guía] Evalúa `line.startswith('V_mV ')` antes de ejecutar las instrucciones de esta rama.
                elif line.startswith("V_mV "):
                    # [Guía] Conserva en `last_data` el resultado de `time.monotonic` con los argumentos
                    # [Guía] indicados.
                    last_data = time.monotonic()
                    # [Guía] Ejecuta print con los argumentos de esta llamada.
                    print(line if args.raw else telemetry(line) or line, flush=True)
                # [Guía] Evalúa `line.startswith('CTRL ')` antes de ejecutar las instrucciones de esta rama.
                elif line.startswith("CTRL "):
                    # [Guía] Evalúa `args.raw or 'SOURCE=LEGACY_INACTIVE ' not in line` antes de ejecutar las
                    # [Guía] instrucciones de esta rama.
                    if args.raw or "SOURCE=LEGACY_INACTIVE " not in line:
                        # [Guía] Ejecuta print con los argumentos de esta llamada.
                        print(line if args.raw else controller_display(line), flush=True)
                # [Guía] Evalúa `line.startswith('OBS ')` antes de ejecutar las instrucciones de esta rama.
                elif line.startswith("OBS "):
                    # [Guía] Ejecuta print con los argumentos de esta llamada.
                    print(line if args.raw else observation_display(line), flush=True)
                # [Guía] Evalúa `line.startswith('SOC ')` antes de ejecutar las instrucciones de esta rama.
                elif line.startswith("SOC "):
                    # [Guía] Ejecuta print con los argumentos de esta llamada.
                    print(line if args.raw else soc_display(line), flush=True)
                # [Guía] Evalúa `line.startswith('EMS ')` antes de ejecutar las instrucciones de esta rama.
                elif line.startswith("EMS "):
                    # [Guía] Ejecuta print con los argumentos de esta llamada.
                    print(line if args.raw else ems_display(line), flush=True)
                # [Guía] Evalúa `line.startswith('MPPT_DIAG ')` antes de ejecutar las instrucciones de esta rama.
                elif line.startswith("MPPT_DIAG "):
                    # [Guía] Ejecuta print con los argumentos de esta llamada.
                    print(line if args.raw else mppt_display(line), flush=True)
                # [Guía] Evalúa `line.startswith('TRIP ')` antes de ejecutar las instrucciones de esta rama.
                elif line.startswith("TRIP "):
                    # Retener en el log, pero no repetir el mismo fallo en pantalla.
                    # [Guía] Evalúa `args.raw` antes de ejecutar las instrucciones de esta rama.
                    if args.raw:
                        # [Guía] Ejecuta print con los argumentos de esta llamada.
                        print(line, flush=True)
                    # [Guía] Evalúa `line != last_trip` antes de ejecutar las instrucciones de esta rama.
                    elif line != last_trip:
                        # [Guía] Conserva en `shown` el resultado de `trip_display` con los argumentos indicados.
                        shown = trip_display(line)
                        # [Guía] Evalúa `shown` antes de ejecutar las instrucciones de esta rama.
                        if shown:
                            # [Guía] Ejecuta print con los argumentos de esta llamada.
                            print(shown, flush=True)
                    # [Guía] Establece `last_trip` a partir de `line`.
                    last_trip = line
                # [Guía] Evalúa `line.startswith('CONNECTION ')` antes de ejecutar las instrucciones de esta rama.
                elif line.startswith("CONNECTION "):
                    # Una espera/revalidacion no es un rearme de un fallo enclavado.
                    # [Guía] Evalúa `args.raw` antes de ejecutar las instrucciones de esta rama.
                    if args.raw:
                        # [Guía] Ejecuta print con los argumentos de esta llamada.
                        print(line, flush=True)
                    # [Guía] Evalúa `line != last_connection` antes de ejecutar las instrucciones de esta rama.
                    elif line != last_connection:
                        # [Guía] Conserva en `shown` el resultado de `connection_display` con los argumentos
                        # [Guía] indicados.
                        shown = connection_display(line)
                        # [Guía] Evalúa `shown` antes de ejecutar las instrucciones de esta rama.
                        if shown:
                            # [Guía] Ejecuta print con los argumentos de esta llamada.
                            print(shown, flush=True)
                    # [Guía] Establece `last_connection` a partir de `line`.
                    last_connection = line
                # [Guía] Evalúa `line.startswith(('AUTO_R ', 'BUS_OV '))` antes de ejecutar las instrucciones de
                # [Guía] esta rama.
                elif line.startswith(("AUTO_R ", "BUS_OV ")):
                    # Mostrar esperas/bloqueos tambien sin --raw. No dar ordenes.
                    # [Guía] Ejecuta print con los argumentos de esta llamada.
                    print(line, flush=True)
                # [Guía] Evalúa `args.raw` antes de ejecutar las instrucciones de esta rama.
                elif args.raw:
                    # [Guía] Ejecuta print con los argumentos de esta llamada.
                    print(line, flush=True)
            # [Guía] Evalúa `len(buffer) > 2048` antes de ejecutar las instrucciones de esta rama.
            if len(buffer) > 2048:
                # [Guía] Señala RuntimeError('Linea Arduino sin terminador') para que la capa que llamó pueda
                # [Guía] tratar el problema.
                raise RuntimeError("Linea Arduino sin terminador")
    # [Guía] Atiende las excepciones que coinciden con `except KeyboardInterrupt` en el bloque anterior.
    except KeyboardInterrupt:
        # [Guía] Ejecuta print con los argumentos de esta llamada.
        print("Terminando...", flush=True)
    # [Guía] Atiende las excepciones que coinciden con `except Exception as error` en el bloque anterior.
    except Exception as error:
        # [Guía] Establece `exit_code` a partir de `1`.
        exit_code = 1
        # [Guía] Ejecuta print con los argumentos de esta llamada.
        print(f"ERROR: {error}", flush=True)
    # [Guía] Ejecuta este cierre tanto si la operación termina normalmente como si produce una excepción.
    finally:
        # El hilo BK termina su transaccion actual y retira consumo; no tocar Q1.
        # [Guía] Evalúa `arduino is not None` antes de ejecutar las instrucciones de esta rama.
        if arduino is not None:
            # [Guía] Delimita la operación cuyos errores o cierre se gestionan en las ramas siguientes.
            try:
                # [Guía] Ejecuta arduino.write con los argumentos de esta llamada.
                arduino.write(b"OFF\n")
            # [Guía] Atiende las excepciones que coinciden con `except Exception` en el bloque anterior.
            except Exception:
                # [Guía] Continúa la instrucción con `pass`.
                pass
        # [Guía] Evalúa `worker is not None` antes de ejecutar las instrucciones de esta rama.
        if worker is not None:
            # [Guía] Repite el bloque mientras se cumpla `True`.
            while True:
                # [Guía] Delimita la operación cuyos errores o cierre se gestionan en las ramas siguientes.
                try:
                    # [Guía] Ejecuta jobs.get_nowait con los argumentos de esta llamada.
                    jobs.get_nowait()
                # [Guía] Atiende las excepciones que coinciden con `except queue.Empty` en el bloque anterior.
                except queue.Empty:
                    # [Guía] Termina el bucle y sigue después de él.
                    break
            # [Guía] Ejecuta jobs.put_nowait con los argumentos de esta llamada.
            jobs.put_nowait(None)
            # [Guía] Ejecuta worker.join con los argumentos de esta llamada.
            worker.join(timeout=10)
            # [Guía] Evalúa `worker.is_alive()` antes de ejecutar las instrucciones de esta rama.
            if worker.is_alive():
                # [Guía] Ejecuta print con los argumentos de esta llamada.
                print("BK no termino: comprobar OFF en panel.", flush=True)
            # [Guía] Repite el bloque mientras se cumpla `not results.empty()`.
            while not results.empty():
                # [Guía] Conserva en `(kind, value)` el resultado de `results.get_nowait` con los argumentos
                # [Guía] indicados.
                kind, value = results.get_nowait()
                # [Guía] Evalúa `kind in ('INFO', 'FATAL')` antes de ejecutar las instrucciones de esta rama.
                if kind in ("INFO", "FATAL"):
                    # [Guía] Ejecuta print con los argumentos de esta llamada.
                    print(value, flush=True)
        # [Guía] Evalúa `bk is not None` antes de ejecutar las instrucciones de esta rama.
        elif bk is not None:
            # [Guía] Delimita la operación cuyos errores o cierre se gestionan en las ramas siguientes.
            try:
                # [Guía] Ejecuta bk.byte_command con los argumentos de esta llamada.
                bk.byte_command(0x21, 0)
                # [Guía] Ejecuta bk.byte_command con los argumentos de esta llamada.
                bk.byte_command(0x20, 0)
            # [Guía] Atiende las excepciones que coinciden con `except Exception as error` en el bloque anterior.
            except Exception as error:
                # [Guía] Ejecuta print con los argumentos de esta llamada.
                print(f"No se confirmo OFF: {error}", flush=True)
        # [Guía] Recorre (arduino, load_port). En cada iteración, port recibe el elemento correspondiente.
        for port in (arduino, load_port):
            # [Guía] Evalúa `port is not None` antes de ejecutar las instrucciones de esta rama.
            if port is not None:
                # [Guía] Ejecuta port.close con los argumentos de esta llamada.
                port.close()
        # [Guía] Evalúa `log_file` antes de ejecutar las instrucciones de esta rama.
        if log_file:
            # [Guía] Ejecuta log_file.close con los argumentos de esta llamada.
            log_file.close()
    # [Guía] Entrega exit_code al llamador y termina la función.
    return exit_code


# [Guía] Evalúa `__name__ == '__main__'` antes de ejecutar las instrucciones de esta rama.
if __name__ == "__main__":
    # [Guía] Señala SystemExit(main()) para que la capa que llamó pueda tratar el problema.
    raise SystemExit(main())
