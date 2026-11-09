// [Guía] Integra la placa y el firmware. Calendariza el ADC, arma la entrada de la EMS, aplica los PWM y atiende
// [Guía] la B&K por medio del puente serie.
/*
 * Programa principal: une adquisicion, control, PWM y comunicacion USB.
 * Los controladores de los .h calculan decisiones; este archivo lee el hardware
 * y aplica sus salidas. No se ejecuta el control desde Python ni desde la GUI.
 *
 * Recorrido de una muestra:
 *   loop -> acquireMeasurements -> SOC/proteccion -> updateBatteryTest
 *        -> escritura PWM -> acumulacion de diagnosticos -> serviceSerial.
 * Los indices siempre son PV=0, BUS=1, BAT=2. Internamente se usan V, A,
 * segundos y duty fraccionario (0.10 = 10%). El transporte usa enteros
 * escalados (mV, mA, milesimas de duty) para reducir texto y ambiguedades.
 *
 * BATTERY_TEST_PROFILE selecciona la EMS actual. Las funciones legacy de Q1
 * se conservan para ensayos anteriores, pero no deben escribir simultaneamente
 * el mismo PWM. Una GUI cerrada no equivale a una parada del convertidor.
 */
// [Guía] Incorpora las declaraciones de "Config.h" antes de usar sus tipos y funciones.
#include "Config.h"
// [Guía] Incorpora las declaraciones de "PIController.h" antes de usar sus tipos y funciones.
#include "PIController.h"
// [Guía] Incorpora las declaraciones de "MPPTController.h" antes de usar sus tipos y funciones.
#include "MPPTController.h"
// [Guía] Incorpora las declaraciones de "EmulatorController.h" antes de usar sus tipos y funciones.
#include "EmulatorController.h"
// [Guía] Incorpora las declaraciones de "EmulatorRamp.h" antes de usar sus tipos y funciones.
#include "EmulatorRamp.h"
// [Guía] Incorpora las declaraciones de "ControlWindow.h" antes de usar sus tipos y funciones.
#include "ControlWindow.h"
// [Guía] Incorpora las declaraciones de "BusOvervoltage.h" antes de usar sus tipos y funciones.
#include "BusOvervoltage.h"
// [Guía] Incorpora las declaraciones de "SocEstimator.h" antes de usar sus tipos y funciones.
#include "SocEstimator.h"
// [Guía] Incorpora las declaraciones de "BatteryTestController.h" antes de usar sus tipos y funciones.
#include "BatteryTestController.h"
// [Guía] Incorpora las declaraciones de "SerialRxService.h" antes de usar sus tipos y funciones.
#include "SerialRxService.h"
// [Guía] Incorpora las declaraciones de "SerialTxService.h" antes de usar sus tipos y funciones.
#include "SerialTxService.h"
// [Guía] Incorpora las declaraciones de <pwm.h> antes de usar sus tipos y funciones.
#include <pwm.h>
// [Guía] Incorpora las declaraciones de <stdio.h> antes de usar sus tipos y funciones.
#include <stdio.h>
// [Guía] Incorpora las declaraciones de <stdlib.h> antes de usar sus tipos y funciones.
#include <stdlib.h>

// Instantanea de adquisicion. Raw es la conversion de una muestra ADC; Filtered
// es el resultado del IIR. currentValid requiere calibracion del cero terminada.
// timestampUs corresponde al inicio del barrido, que es secuencial, no simultaneo.
// [Guía] Reúne bajo `Measurements` las declaraciones que comparten este tipo o ámbito.
struct Measurements {
  // [Guía] Actualiza `adcCurrent[3]` con `{}`.
  uint16_t adcCurrent[3] = {};
  // [Guía] Actualiza `adcVoltage[3]` con `{}`.
  uint16_t adcVoltage[3] = {};
  // [Guía] Actualiza `currentRaw[3]` con `{}`.
  float currentRaw[3] = {};
  // [Guía] Actualiza `currentFiltered[3]` con `{}`.
  float currentFiltered[3] = {};
  // [Guía] Actualiza `voltageRaw[3]` con `{}`.
  float voltageRaw[3] = {};
  // [Guía] Actualiza `voltageFiltered[3]` con `{}`.
  float voltageFiltered[3] = {};
  // [Guía] Actualiza `currentValid[3]` con `{}`.
  bool currentValid[3] = {};
  // [Guía] Actualiza `timestampUs` con `0`.
  uint32_t timestampUs = 0;
};

// Objetos persistentes: su memoria sobrevive entre vueltas de loop(). Los flags
// Ready indican temporizadores inicializados, no presencia de una fuente fisica.
// [Guía] Declara `measurements` para conservar instantánea de medidas y su validez en este ámbito.
Measurements measurements;
// [Guía] Completa la declaración o expresión con `PwmOut pwm1(Config::PWM1_PIN)`. Aquí intervienen temporizador
// [Guía] de la rama Q1.
PwmOut pwm1(Config::PWM1_PIN);
// [Guía] Completa la declaración o expresión con `PwmOut pwm2(Config::PWM2_PIN), pwm3(Config::PWM3_PIN)`. Aquí
// [Guía] intervienen temporizador de la rama Q2 y temporizador de la rama Q3.
PwmOut pwm2(Config::PWM2_PIN), pwm3(Config::PWM3_PIN);
// [Guía] Actualiza `pwm2Ready` con `false, pwm3Ready = false`.
bool pwm2Ready = false, pwm3Ready = false;
// [Guía] Actualiza `duty2` con `0.0f, duty3 = 0.0f`.
float duty2 = 0.0f, duty3 = 0.0f;
// [Guía] Declara `batteryTest` para conservar batería test en este ámbito.
BatteryTestController batteryTest; // Unico supervisor de modos en el perfil EMS.
// [Guía] Actualiza `batteryCommandStatus` con `"BOOT"`.
const char* batteryCommandStatus = "BOOT";
// [Guía] Completa la declaración o expresión con `void stopBatteryOutputs()`.
void stopBatteryOutputs();
// PI/MPPT legacy y servicios independientes. La EMS tiene sus propios lazos;
// el emulador necesita ACK del PC y la estimacion SOC no gobierna un PWM sola.
// [Guía] Completa la declaración o expresión con `PIController busPI(Config::BUS_KP, Config::BUS_KI,
// [Guía] Config::DUTY_MIN, Config::DUTY_MAX)`.
PIController busPI(Config::BUS_KP, Config::BUS_KI, Config::DUTY_MIN, Config::DUTY_MAX);
// [Guía] Completa la declaración o expresión con `MPPTController mppt(Config::DUTY_MIN, Config::DUTY_MAX)`.
MPPTController mppt(Config::DUTY_MIN, Config::DUTY_MAX);
// [Guía] Declara `emulador` para conservar emulador en este ámbito.
EmulatorController emulador;
// [Guía] Declara `emulatorRamp` para conservar emulator rampa en este ámbito.
EmulatorRamp emulatorRamp;
// [Guía] Declara `busOvervoltage` para conservar bus DC overvoltage en este ámbito.
BusOvervoltage busOvervoltage;
// [Guía] Declara `batterySoc` para conservar estimador por conteo de carga en este ámbito.
SocEstimator batterySoc;
// [Guía] Actualiza `socInputStatus` con `"NOT_SET"`.
const char* socInputStatus = "NOT_SET";
// Arranque unico por carga/reset; una orden manual posterior no se sobrescribe.
// [Guía] Actualiza `emulatorBootPending` con `false`.
bool emulatorBootPending = false;
// [Guía] Actualiza `emulatorBootState` con `"OFF"`.
const char* emulatorBootState = "OFF";
// [Guía] Actualiza `emulatorBootStable` con `false`.
bool emulatorBootStable = false;
// [Guía] Actualiza `emulatorBootStableSince` con `0`.
uint32_t emulatorBootStableSince = 0;
// [Guía] Declara `controlWindow` para conservar control ventana en este ámbito.
ControlWindow controlWindow;
// [Guía] Actualiza `mpptActive` con `false`.
bool mpptActive = false;
// [Guía] Actualiza `mpptStatus` con `"OFF"`.
const char* mpptStatus = "OFF";
// [Guía] Actualiza `lastMpptSampleUs` con `0`.
uint32_t lastMpptSampleUs = 0;
// Estado del camino Q1 legacy: referencia gradual y calendario de control.
// Los contadores perdidos son diagnosticos; nunca fabrican muestras ausentes.
// [Guía] Actualiza `pwmReady` con `false, mode1Active = false`.
bool pwmReady = false, mode1Active = false;
// [Guía] Actualiza `autoStartPending` con `false`.
bool autoStartPending = false;
// [Guía] Actualiza `duty1` con `0.0f`.
float duty1 = 0.0f;
// [Guía] Actualiza `busReference` con `Config::BUS_REFERENCE_V, startReference = 0.0f`.
float busReference = Config::BUS_REFERENCE_V, startReference = 0.0f;
// [Guía] Actualiza `softStarting` con `false`.
bool softStarting = false;
// [Guía] Actualiza `softStartElapsedUs` con `0`.
uint32_t softStartElapsedUs = 0;
// [Guía] Actualiza `lastControlUs` con `0, nextControlUs = 0, controlUpdates = 0, controlMissed = 0`.
uint32_t lastControlUs = 0, nextControlUs = 0, controlUpdates = 0, controlMissed = 0;
// [Guía] Actualiza `lastSuccessfulControlUs` con `0`.
uint32_t lastSuccessfulControlUs = 0;
// [Guía] Actualiza `diagnosticSamples` con `0, diagnosticSlew = 0`.
uint32_t diagnosticSamples = 0, diagnosticSlew = 0;
// [Guía] Actualiza `diagnosticErrorSum` con `0.0f, diagnosticErrorMin = 0.0f, diagnosticErrorMax = 0.0f`.
float diagnosticErrorSum = 0.0f, diagnosticErrorMin = 0.0f, diagnosticErrorMax = 0.0f;
// [Guía] Actualiza `diagnosticTrackingSum` con `0.0f`.
float diagnosticTrackingSum = 0.0f;

// [Guía] Actualiza `stopReason` con `"BOOT"`.
const char* stopReason = "BOOT";
// [Guía] Actualiza `stopElapsedUs` con `0, stopSkipped = 0`.
uint32_t stopElapsedUs = 0, stopSkipped = 0;

// Parada comun, incluido el perfil EMS. Cancelar automatismos evita que una
// orden X sea deshecha por una tarea pendiente de arranque. LOW fuerza GPIO
// despues de liberar el temporizador. La carga de mitigacion puede conservarse.
// [Guía] Termina el perfil independiente, baja Q1 y registra la causa para distinguir un paro del cambio de
// [Guía] estado de la EMS. Recibe `reason`.
void stopMode1(const char* reason) {
  // [Guía] Invoca `batteryTest.stop` con los argumentos de esta expresión.
  batteryTest.stop();
  // [Guía] Lleva las tres ramas al estado apagado mediante la misma secuencia de liberación de PWM.
  stopBatteryOutputs();
  // [Guía] Invoca `emulatorRamp.cancel` con los argumentos de esta expresión.
  emulatorRamp.cancel("CONTROL_STOPPED");
  // [Guía] Actualiza `emulatorBootPending` con `false`.
  emulatorBootPending = false;
  // [Guía] Actualiza `emulatorBootStable` con `false`.
  emulatorBootStable = false;
  // [Guía] Actualiza `emulatorBootState` con `"CONTROL_STOPPED"`.
  emulatorBootState = "CONTROL_STOPPED";
  // Con sobretension enclavada conservar la carga solicitada aunque falle PI.
  // La parada explicita X sigue permitiendo OFF; no restaurar ON por sorpresa.
  // [Guía] Comprueba `!busOvervoltage.active() || !strcmp(reason, )`. Si se cumple, ejecuta el bloque asociado.
  if (!busOvervoltage.active() || !strcmp(reason, "MANUAL"))
    // [Guía] Invoca `emulador.solicitarConsumo` con los argumentos de esta expresión.
    emulador.solicitarConsumo(false);
  // [Guía] Actualiza `mpptActive` con `false`.
  mpptActive = false;
  // [Guía] Actualiza `mpptStatus` con `"OFF"`.
  mpptStatus = "OFF";
  // [Guía] Actualiza `autoStartPending` con `false`.
  autoStartPending = false;
  // [Guía] Actualiza `stopReason` con `reason`.
  stopReason = reason;
  // [Guía] Actualiza `mode1Active` con `false`.
  mode1Active = false;
  // [Guía] Actualiza `softStarting` con `false`.
  softStarting = false;
  // [Guía] Actualiza `softStartElapsedUs` con `0`.
  softStartElapsedUs = 0;
  // [Guía] Actualiza `duty1` con `0.0f`.
  duty1 = 0.0f;
  // [Guía] Comprueba `pwmReady`. Si se cumple, ejecuta `pwm1.end()`.
  if (pwmReady) pwm1.end();
  // [Guía] Actualiza `pwmReady` con `false`.
  pwmReady = false;
  // [Guía] Invoca `digitalWrite` con los argumentos de esta expresión.
  digitalWrite(Config::PWM1_PIN, LOW);
  // [Guía] Invoca `pinMode` con los argumentos de esta expresión.
  pinMode(Config::PWM1_PIN, OUTPUT);
  // [Guía] Invoca `digitalWrite` con los argumentos de esta expresión.
  digitalWrite(Config::PWM1_PIN, LOW);
}
// Calibracion: media del ADC sin corriente para cada sensor seleccionado.
// Mask usa bits 0,1,2 para PV,BUS,BAT. No es una medicion del cero con carga.
// [Guía] Actualiza `zeroAdc[3]` con `{}`.
float zeroAdc[3] = {};
// [Guía] Actualiza `zeroSum[3]` con `{}`.
uint32_t zeroSum[3] = {};
// [Guía] Actualiza `zeroCount` con `0`.
uint32_t zeroCount = 0;
// [Guía] Actualiza `calibrating` con `false`.
bool calibrating = false;
// [Guía] Actualiza `calibrationMask` con `0`.
uint8_t calibrationMask = 0;
// [Guía] Actualiza `currentFilterInitialized[3]` con `{}`.
bool currentFilterInitialized[3] = {};
// [Guía] Actualiza `filtersInitialized` con `false`.
bool filtersInitialized = false;
// Relojes sin bloqueo: adquisicion en us y publicacion en ms. Las restas unsigned
// mantienen intervalos validos al desbordar micros()/millis(), dentro de su rango.
// [Guía] Declara `nextSampleUs` para conservar fecha límite del próximo barrido en µs en este ámbito.
uint32_t nextSampleUs, lastReportMs, reportStartUs;
// [Guía] Actualiza `frames` con `0, missed = 0, maxAcquisitionUs = 0, maxLatenessUs = 0`.
uint32_t frames = 0, missed = 0, maxAcquisitionUs = 0, maxLatenessUs = 0;
// [Guía] Actualiza `displayCurrentSum[3]` con `{}, displayVoltageSum[3] = {}`.
float displayCurrentSum[3] = {}, displayVoltageSum[3] = {};
// [Guía] Actualiza `displayCurrentCount[3]` con `{}`.
uint32_t displayCurrentCount[3] = {};
// Ventana corta independiente: el dashboard no espera el reporte largo1s
// ni promedia sus numeros sobre todo ese segundo.
// [Guía] Actualiza `liveFrames` con `0, liveLastMs = 0`.
uint32_t liveFrames = 0, liveLastMs = 0;
// [Guía] Actualiza `liveVoltageSum[3]` con `{}, liveCurrentSum[3] = {}, livePowerSum[3] = {}`.
float liveVoltageSum[3] = {}, liveCurrentSum[3] = {}, livePowerSum[3] = {};
// [Guía] Actualiza `liveCurrentCount[3]` con `{}`.
uint32_t liveCurrentCount[3] = {};
// [Guía] Actualiza `livePowerCount[3]` con `{}`.
uint32_t livePowerCount[3] = {}; // Productos Vraw*Iraw del MISMO barrido ADC.
// Un solo buffer evita intercalar ordenes BK dentro de lineas de telemetria.
// [Guía] Declara `txBuffer` para conservar tx buffer en este ámbito.
char txBuffer[4864]; // Incluir preparacion PV y margen sin truncar diagnosticos.
// [Guía] Actualiza `txLength` con `0, txPosition = 0`.
size_t txLength = 0, txPosition = 0;
// RX reserva un byte final para '\0'; una linea larga se descarta completa,
// nunca se interpreta el prefijo truncado como una orden valida.
// [Guía] Declara `rxLine` para conservar rx line en este ámbito.
char rxLine[96];
// [Guía] Actualiza `rxLength` con `0`.
size_t rxLength = 0;
// [Guía] Actualiza `rxOverflow` con `false`.
bool rxOverflow = false;

// Un divisor con resistencias no positivas deshabilita su conversion de tension.
// [Guía] Consulta si el canal de tensión tiene calibración habilitada antes de usarlo como medida válida. Recibe
// [Guía] `channel`.
bool voltageEnabled(uint8_t channel) {
  // [Guía] Entrega `Config::R_TOP[channel] > 0 && Config::R_BOTTOM[channel] > 0` al llamador.
  return Config::R_TOP[channel] > 0 && Config::R_BOTTOM[channel] > 0;
}

// Tres temporizadores distintos: PwmOut no comparte automaticamente un GPT.
// [Guía] Comprueba que el pin de la rama de batería se pueda usar para PWM. Recibe `pin`.
bool batteryPinReady(uint8_t pin) {
  // [Guía] Entrega `pin < 14 && digitalPinHasPWM(pin)` al llamador.
  return pin < 14 && digitalPinHasPWM(pin);
}
// [Guía] Comprueba que las salidas de las ramas no comparten el mismo pin. El resultado es una decisión booleana
// [Guía] para el llamador.
bool batteryPinsDistinct() {
  // [Guía] Comprueba `!batteryPinReady(Config::PWM1_PIN) || !batteryPinReady(Config::PWM2_PIN)`. Si se cumple,
  // [Guía] ejecuta el bloque asociado.
  if (!batteryPinReady(Config::PWM1_PIN) || !batteryPinReady(Config::PWM2_PIN)
      // [Guía] Añade una alternativa suficiente dentro de la condición compuesta.
      || !batteryPinReady(Config::PWM3_PIN)) return false;
  // [Guía] Actualiza `t1` con `GET_CHANNEL(getPinCfgs(Config::PWM1_PIN, PIN_CFG_REQ_PWM)[0])`.
  const uint8_t t1 = GET_CHANNEL(getPinCfgs(Config::PWM1_PIN, PIN_CFG_REQ_PWM)[0]);
  // [Guía] Actualiza `t2` con `GET_CHANNEL(getPinCfgs(Config::PWM2_PIN, PIN_CFG_REQ_PWM)[0])`.
  const uint8_t t2 = GET_CHANNEL(getPinCfgs(Config::PWM2_PIN, PIN_CFG_REQ_PWM)[0]);
  // [Guía] Actualiza `t3` con `GET_CHANNEL(getPinCfgs(Config::PWM3_PIN, PIN_CFG_REQ_PWM)[0])`.
  const uint8_t t3 = GET_CHANNEL(getPinCfgs(Config::PWM3_PIN, PIN_CFG_REQ_PWM)[0]);
  // [Guía] Entrega `t1 != t2 && t1 != t3 && t2 != t3` al llamador.
  return t1 != t2 && t1 != t3 && t2 != t3;
}
// Referencias (&): modificar aqui ready/duty actualiza el estado del llamador.
// Escribir LOW antes y despues de pinMode evita dejar el latch previo en HIGH.
// [Guía] Libera el PWM si se había iniciado, lleva el pin a nivel bajo y actualiza la copia de la salida
// [Guía] realmente aplicada. Recibe `pwm`, `pin`, `ready`, `duty`.
void forcePwmOff(PwmOut& pwm, uint8_t pin, bool& ready, float& duty) {
  // [Guía] Comprueba `ready`. Si se cumple, ejecuta `pwm.end()`.
  if (ready) pwm.end();
  // [Guía] Actualiza `ready` con `false`.
  ready = false;
  // [Guía] Actualiza `duty` con `0`.
  duty = 0;
  // [Guía] Comprueba `pin < 14`. Si se cumple, ejecuta el bloque asociado.
  if (pin < 14) {
    // [Guía] Invoca `digitalWrite` con los argumentos de esta expresión.
    digitalWrite(pin, LOW);
    // [Guía] Invoca `pinMode` con los argumentos de esta expresión.
    pinMode(pin, OUTPUT);
    // [Guía] Invoca `digitalWrite` con los argumentos de esta expresión.
    digitalWrite(pin, LOW);
  }
}
// Apagar primero los dos caminos de bateria y luego el puerto solar.
// [Guía] Lleva las tres ramas al estado apagado mediante la misma secuencia de liberación de PWM.
void stopBatteryOutputs() {
  // [Guía] Libera el PWM si se había iniciado, lleva el pin a nivel bajo y actualiza la copia de la salida
  // [Guía] realmente aplicada. Aquí intervienen temporizador de la rama Q2.
  forcePwmOff(pwm2, Config::PWM2_PIN, pwm2Ready, duty2);
  // [Guía] Libera el PWM si se había iniciado, lleva el pin a nivel bajo y actualiza la copia de la salida
  // [Guía] realmente aplicada. Aquí intervienen temporizador de la rama Q3.
  forcePwmOff(pwm3, Config::PWM3_PIN, pwm3Ready, duty3);
  // [Guía] Libera el PWM si se había iniciado, lleva el pin a nivel bajo y actualiza la copia de la salida
  // [Guía] realmente aplicada. Aquí intervienen temporizador de la rama Q1.
  forcePwmOff(pwm1, Config::PWM1_PIN, pwmReady, duty1);
}
// Frontera calculo/hardware: validar antes de llamar a PwmOut. La libreria pide
// porcentaje, por eso se multiplica la fraccion por 100. applied solo cambia
// cuando begin/pulse_perc confirma la escritura; false causa parada aguas arriba.
// [Guía] Aplica una consigna fraccional a una rama. Pasa a porcentaje al invocar PwmOut y conserva el duty
// [Guía] efectivamente aplicado. El resultado es una decisión booleana para el llamador. Recibe `pwm`, `pin`,
// [Guía] `ready`, `applied`, `requested`, `maximum`.
bool applyTestPwm(PwmOut& pwm, uint8_t pin, bool& ready, float& applied,
                  // [Guía] Declara `requested` para conservar requested en este ámbito.
                  float requested, float maximum) {
  // [Guía] Comprueba `!isfinite(requested) || requested < 0 || requested > maximum`. Si se cumple, termina la
  // [Guía] función en esta rama.
  if (!isfinite(requested) || requested < 0 || requested > maximum) return false;
  // [Guía] Comprueba `requested == 0`. Si se cumple, ejecuta el bloque asociado.
  if (requested == 0) {
    // [Guía] Comprueba `ready || applied != 0`. Si se cumple, ejecuta `forcePwmOff(pwm, pin, ready, applied)`.
    if (ready || applied != 0) forcePwmOff(pwm, pin, ready, applied);
    // [Guía] Entrega `true` al llamador.
    return true;
  }
  // [Guía] Comprueba `!batteryPinReady(pin)`. Si se cumple, termina la función en esta rama.
  if (!batteryPinReady(pin)) return false;
  // [Guía] Declara `ok` para conservar ok en este ámbito.
  bool ok;
  // [Guía] Comprueba `!ready`. Si se cumple, ejecuta el bloque asociado.
  if (!ready) {
    // [Guía] Actualiza `ok` con `pwm.begin(Config::PWM1_HZ, requested * 100.0f)`.
    ok = pwm.begin(Config::PWM1_HZ, requested * 100.0f);
    // [Guía] Actualiza `ready` con `ok`.
    ready = ok;
    // [Guía] Comprueba `!ok`. Si se cumple, ejecuta el bloque asociado.
    if (!ok) {
      // [Guía] Invoca `pwm.end` con los argumentos de esta expresión.
      pwm.end();
      // [Guía] Libera el PWM si se había iniciado, lleva el pin a nivel bajo y actualiza la copia de la salida
      // [Guía] realmente aplicada.
      forcePwmOff(pwm, pin, ready, applied);
    }
  // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
  } else ok = applied == requested || pwm.pulse_perc(requested * 100.0f);
  // [Guía] Comprueba `ok`. Si se cumple, ejecuta `applied = requested`.
  if (ok) applied = requested;
  // [Guía] Entrega `ok` al llamador.
  return ok;
}
// Traducir una instantanea ADC a Input de la EMS, ejecutar su FSM y aplicar
// duty. Los permisos de sensor incluyen calibracion y ausencia de saturacion.
// Un enlace confirmado es evidencia de protocolo, no un contacto mecanico leido.
// [Guía] Construye la instantánea de entradas de la EMS con medidas, permisos y estado del emulador. Apaga
// [Guía] primero las ramas salientes y luego aplica las consignas restantes.
void updateBatteryTest() {
  // [Guía] Declara `in` para conservar in en este ámbito.
  BatteryTestController::Input in;
  // [Guía] Actualiza `in.nowUs` con `measurements.timestampUs`. Este dato representa reloj de la muestra en µs.
  in.nowUs = measurements.timestampUs;
  // [Guía] Actualiza `in.calibrating` con `calibrating`.
  in.calibrating = calibrating;
  // [Guía] Actualiza `in.batteryValid` con `measurements.currentValid[2] && voltageEnabled(2)`.
  in.batteryValid = measurements.currentValid[2] && voltageEnabled(2)
    // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen instantánea de
    // [Guía] medidas y su validez.
    && measurements.adcCurrent[2] > 0 && measurements.adcCurrent[2] < Config::ADC_MAX
    // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen instantánea de
    // [Guía] medidas y su validez.
    && measurements.adcVoltage[2] > 0 && measurements.adcVoltage[2] < Config::ADC_MAX;
  // Bus descargado puede dar ADC=0 en espera/rearme; la FSM no arranca asi.
  // [Guía] Actualiza `in.busValid` con `voltageEnabled(1) && measurements.adcVoltage[1] < Config::ADC_MAX`.
  in.busValid = voltageEnabled(1) && measurements.adcVoltage[1] < Config::ADC_MAX;
  // [Guía] Actualiza `in.pvValid` con `measurements.currentValid[0] && voltageEnabled(0)`.
  in.pvValid = measurements.currentValid[0] && voltageEnabled(0)
    // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen instantánea de
    // [Guía] medidas y su validez.
    && measurements.adcCurrent[0] > 0 && measurements.adcCurrent[0] < Config::ADC_MAX
    // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen instantánea de
    // [Guía] medidas y su validez.
    && measurements.adcVoltage[0] < Config::ADC_MAX;
  // [Guía] Actualiza `in.busCurrentValid` con `measurements.currentValid[1]`.
  in.busCurrentValid = measurements.currentValid[1]
    // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen instantánea de
    // [Guía] medidas y su validez.
    && measurements.adcCurrent[1] > 0 && measurements.adcCurrent[1] < Config::ADC_MAX;
  // Una estimacion con huecos no se presenta a la EMS como SOC continuo valido.
  // [Guía] Actualiza `in.socValid` con `batterySoc.continuous()`.
  in.socValid = batterySoc.continuous();
  // [Guía] Actualiza `in.soc` con `batterySoc.percent()`. Este dato representa estado de carga estimado.
  in.soc = batterySoc.percent();
  // [Guía] Actualiza `pinsReady` con `batteryPinsDistinct()`.
  static const bool pinsReady = batteryPinsDistinct();
  // [Guía] Actualiza `in.q3Configured` con `pinsReady`.
  in.q3Configured = pinsReady
    // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
    && BatteryTestController::validGains(Config::Q3_BUS_KP, Config::Q3_BUS_KI, Config::Q3_BUS_ACTION);
  // [Guía] Actualiza `in.q2Configured` con `pinsReady`.
  in.q2Configured = pinsReady
    // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
    && BatteryTestController::validGains(Config::Q2_BUS_KP, Config::Q2_BUS_KI, Config::Q2_BUS_ACTION);
  // [Guía] Actualiza `auxiliaryOnConfirmed` con `busOvervoltage.active() && emulador.enlace`.
  const bool auxiliaryOnConfirmed = busOvervoltage.active() && emulador.enlace
    // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
    && emulador.estadoConsumoValido && emulador.consumoConfirmado && emulador.consumoSolicitado
    // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
    && emulador.resistenciaConfirmada;
  // [Guía] Actualiza `in.auxiliaryLoadOhm` con `auxiliaryOnConfirmed ? emulador.resistenciaConfirmadaOhm : 0`.
  in.auxiliaryLoadOhm = auxiliaryOnConfirmed ? emulador.resistenciaConfirmadaOhm : 0;
  // [Guía] Actualiza `in.auxiliaryLoadStateValid` con `emulador.enlace && emulador.estadoConsumoValido`.
  in.auxiliaryLoadStateValid = emulador.enlace && emulador.estadoConsumoValido;
  // [Guía] Actualiza `in.auxiliaryLoadOn` con `emulador.consumoConfirmado`.
  in.auxiliaryLoadOn = emulador.consumoConfirmado;
  // [Guía] Actualiza `in.recoveryLoadConfirmed` con `auxiliaryOnConfirmed`.
  in.recoveryLoadConfirmed = auxiliaryOnConfirmed
    // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
    && emulador.resistenciaConfirmadaOhm <= Config::BUS_OV_EMULATOR_OHM;
  // [Guía] Actualiza `in.m1AuxiliaryAdjusting` con `busOvervoltage.m1Adjusting()`.
  in.m1AuxiliaryAdjusting = busOvervoltage.m1Adjusting();
  // [Guía] Actualiza `in.pvConfigured` con `batteryPinReady(Config::PWM1_PIN) && Config::MPPT_BUS_MAX_V >
  // [Guía] Config::BUS_REFERENCE_V`.
  in.pvConfigured = batteryPinReady(Config::PWM1_PIN) && Config::MPPT_BUS_MAX_V > Config::BUS_REFERENCE_V
    // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
    && Config::MPPT_BUS_MAX_V <= Config::BAT_TEST_BUS_TRIP_V;
  // Conservar ambas escalas temporales: filtros para lazos/supervision y raw
  // para proteccion/diagnostico. No sustituir una por otra solo para suavizar GUI.
  // [Guía] Actualiza `in.bus` con `measurements.voltageFiltered[1]`. Este dato representa tensión rápida del bus
  // [Guía] en V.
  in.bus = measurements.voltageFiltered[1];
  // [Guía] Actualiza `in.busRaw` con `measurements.voltageRaw[1]`.
  in.busRaw = measurements.voltageRaw[1];
  // [Guía] Actualiza `in.batteryV` con `measurements.voltageRaw[2]`.
  in.batteryV = measurements.voltageRaw[2];
  // [Guía] Actualiza `in.batteryI` con `measurements.currentFiltered[2]`.
  in.batteryI = measurements.currentFiltered[2];
  // [Guía] Actualiza `in.batteryIRaw` con `measurements.currentRaw[2]`.
  in.batteryIRaw = measurements.currentRaw[2];
  // [Guía] Actualiza `in.pv` con `measurements.voltageFiltered[0]`. Este dato representa tensión del PV en V.
  in.pv = measurements.voltageFiltered[0];
  // [Guía] Actualiza `in.pvRaw` con `measurements.voltageRaw[0]`.
  in.pvRaw = measurements.voltageRaw[0];
  // EMS/MPPT usan corriente filtrada para no detectar fuente por un pico ADC.
  // [Guía] Actualiza `in.pvI` con `measurements.currentFiltered[0]`.
  in.pvI = measurements.currentFiltered[0];
  // [Guía] Actualiza `in.pvIRaw` con `measurements.currentRaw[0]`.
  in.pvIRaw = measurements.currentRaw[0];
  // [Guía] Actualiza `in.busIRaw` con `measurements.currentRaw[1]`.
  in.busIRaw = measurements.currentRaw[1];
  // [Guía] Actualiza `in.adcBatteryI` con `measurements.adcCurrent[2]`.
  in.adcBatteryI = measurements.adcCurrent[2];
  // [Guía] Actualiza `in.adcBusV` con `measurements.adcVoltage[1]`.
  in.adcBusV = measurements.adcVoltage[1];
  // [Guía] Actualiza `in.adcBatteryV` con `measurements.adcVoltage[2]`.
  in.adcBatteryV = measurements.adcVoltage[2];
  // [Guía] Actualiza `in.zeroBatteryAdc` con `zeroAdc[2]`.
  in.zeroBatteryAdc = zeroAdc[2];
  // [Guía] Entrega una nueva muestra o ciclo de trabajo a `batteryTest`.
  batteryTest.update(in);
  // Solicitar carga por sobretension no apaga EMS: sus PI/transiciones siguen
  // activos. Los fallos locales ya comprobados por update conservan prioridad.
  // Apagar primero la rama saliente, antes de inicializar la opuesta.
  // [Guía] Comprueba `batteryTest.duty1() == 0 && (pwmReady || duty1 != 0)`. Si se cumple, ejecuta el bloque
  // [Guía] asociado.
  if (batteryTest.duty1() == 0 && (pwmReady || duty1 != 0))
    // [Guía] Libera el PWM si se había iniciado, lleva el pin a nivel bajo y actualiza la copia de la salida
    // [Guía] realmente aplicada. Aquí intervienen temporizador de la rama Q1.
    forcePwmOff(pwm1, Config::PWM1_PIN, pwmReady, duty1);
  // [Guía] Comprueba `batteryTest.duty2() == 0 && (pwm2Ready || duty2 != 0)`. Si se cumple, ejecuta el bloque
  // [Guía] asociado.
  if (batteryTest.duty2() == 0 && (pwm2Ready || duty2 != 0))
    // [Guía] Libera el PWM si se había iniciado, lleva el pin a nivel bajo y actualiza la copia de la salida
    // [Guía] realmente aplicada. Aquí intervienen temporizador de la rama Q2.
    forcePwmOff(pwm2, Config::PWM2_PIN, pwm2Ready, duty2);
  // [Guía] Comprueba `batteryTest.duty3() == 0 && (pwm3Ready || duty3 != 0)`. Si se cumple, ejecuta el bloque
  // [Guía] asociado.
  if (batteryTest.duty3() == 0 && (pwm3Ready || duty3 != 0))
    // [Guía] Libera el PWM si se había iniciado, lleva el pin a nivel bajo y actualiza la copia de la salida
    // [Guía] realmente aplicada. Aquí intervienen temporizador de la rama Q3.
    forcePwmOff(pwm3, Config::PWM3_PIN, pwm3Ready, duty3);
  // [Guía] Actualiza `ok` con `!(batteryTest.duty2() > 0 && batteryTest.duty3() > 0)`.
  bool ok = !(batteryTest.duty2() > 0 && batteryTest.duty3() > 0);
  // [Guía] Comprueba `ok`. Si se cumple, ejecuta `ok = applyTestPwm(pwm3, Config::PWM3_PIN, pwm3Ready, duty3,`.
  if (ok) ok = applyTestPwm(pwm3, Config::PWM3_PIN, pwm3Ready, duty3,
                           // [Guía] Invoca `batteryTest.duty3` con los argumentos de esta expresión.
                           batteryTest.duty3(), Config::Q3_DUTY_MAX);
  // [Guía] Comprueba `ok`. Si se cumple, ejecuta `ok = applyTestPwm(pwm2, Config::PWM2_PIN, pwm2Ready, duty2,`.
  if (ok) ok = applyTestPwm(pwm2, Config::PWM2_PIN, pwm2Ready, duty2,
                           // [Guía] Invoca `batteryTest.duty2` con los argumentos de esta expresión.
                           batteryTest.duty2(), Config::Q2_DUTY_MAX);
  // [Guía] Comprueba `ok`. Si se cumple, ejecuta `ok = applyTestPwm(pwm1, Config::PWM1_PIN, pwmReady, duty1,`.
  if (ok) ok = applyTestPwm(pwm1, Config::PWM1_PIN, pwmReady, duty1,
                           // [Guía] Invoca `batteryTest.duty1` con los argumentos de esta expresión.
                           batteryTest.duty1(), Config::Q1_DUTY_MAX);
  // [Guía] Comprueba `!ok`. Si se cumple, ejecuta el bloque asociado.
  if (!ok) {
    // [Guía] Invoca `batteryTest.fail` con los argumentos de esta expresión.
    batteryTest.fail("PWM_APPLY_FAILED");
    // [Guía] Lleva las tres ramas al estado apagado mediante la misma secuencia de liberación de PWM.
    stopBatteryOutputs();
  }
  // [Guía] Actualiza `mpptActive` con `batteryTest.mpptRunning()`.
  mpptActive = batteryTest.mpptRunning();
  // [Guía] Actualiza `mpptStatus` con `batteryTest.reason()`.
  mpptStatus = batteryTest.reason();
}

// Iniciar sin delay: acquireMeasurements acumulara ZERO_SAMPLES en las vueltas
// siguientes. Primero se paran salidas; el usuario debe garantizar corriente cero.
// [Guía] Prepara la estimación de offset de los canales de corriente que señala la máscara de bits. Recibe
// [Guía] `mask`.
void startZeroCalibration(uint8_t mask) {
  // [Guía] Termina el perfil independiente, baja Q1 y registra la causa para distinguir un paro del cambio de
  // [Guía] estado de la EMS.
  stopMode1("CAL");
  // [Guía] Actualiza `calibrationMask` con `mask`.
  calibrationMask = mask;
  // [Guía] Actualiza `calibrating` con `true`.
  calibrating = true;
  // [Guía] Actualiza `zeroCount` con `0`.
  zeroCount = 0;
  // [Guía] Controla las iteraciones mediante `uint8_t i = 0; i < 3; ++i)`.
  for (uint8_t i = 0; i < 3; ++i) {
    // [Guía] Comprueba `!(mask & (1U << i))`. Si se cumple, pasa a la siguiente iteración.
    if (!(mask & (1U << i))) continue;
    // [Guía] Actualiza `measurements.currentValid[i]` con `false`.
    measurements.currentValid[i] = false;
    // [Guía] Actualiza `currentFilterInitialized[i]` con `false`.
    currentFilterInitialized[i] = false;
    // [Guía] Actualiza `zeroSum[i]` con `0`.
    zeroSum[i] = 0;
    // [Guía] Actualiza `displayCurrentSum[i]` con `0`.
    displayCurrentSum[i] = 0;
    // [Guía] Actualiza `displayCurrentCount[i]` con `0`.
    displayCurrentCount[i] = 0;
  }
}

// Arranque PI legacy, inactivo en el perfil EMS. Partir de la tension medida
// y precargar la integral permite elevar la referencia sin un salto artificial.
// [Guía] Prepara el arranque del perfil independiente de regulación del bus mediante Q1.
void startMode1() {
  // [Guía] Comprueba `Config::BATTERY_TEST_PROFILE`. Si se cumple, termina la función en esta rama.
  if (Config::BATTERY_TEST_PROFILE) return;
  // [Guía] Comprueba `mode1Active || calibrating || !filtersInitialized`. Si se cumple, termina la función en
  // [Guía] esta rama.
  if (mode1Active || calibrating || !filtersInitialized) return;
  // [Guía] Comprueba `mpptActive`. Si se cumple, ejecuta `stopMode1("SWITCH_PI")`.
  if (mpptActive) stopMode1("SWITCH_PI");
  // [Guía] Actualiza `measured` con `measurements.voltageFiltered[1]`.
  const float measured = measurements.voltageFiltered[1];
  // [Guía] Comprueba `!isfinite(measured)`. Si se cumple, termina la función en esta rama.
  if (!isfinite(measured)) return;
  // [Guía] Actualiza `startReference` con `measured < Config::BUS_REFERENCE_V ? measured :
  // [Guía] Config::BUS_REFERENCE_V`.
  startReference = measured < Config::BUS_REFERENCE_V ? measured : Config::BUS_REFERENCE_V;
  // [Guía] Comprueba `startReference < 0.0f`. Si se cumple, ejecuta `startReference = 0.0f`.
  if (startReference < 0.0f) startReference = 0.0f;
  // [Guía] Actualiza `busReference` con `startReference`.
  busReference = startReference;
  // [Guía] Actualiza `softStartElapsedUs` con `0`.
  softStartElapsedUs = 0;
  // [Guía] Actualiza `softStarting` con `startReference < Config::BUS_REFERENCE_V`.
  softStarting = startReference < Config::BUS_REFERENCE_V;
  // [Guía] Actualiza `error` con `measured - busReference`.
  const float error = measured - busReference;
  // [Guía] Reinicia `busPI` con el estado inicial dado en los argumentos.
  busPI.reset(error, Config::DUTY_MIN);
  // [Guía] Actualiza `pwmReady` con `pwm1.begin(Config::PWM1_HZ, Config::DUTY_MIN * 100.0f)`.
  pwmReady = pwm1.begin(Config::PWM1_HZ, Config::DUTY_MIN * 100.0f);
  // [Guía] Comprueba `!pwmReady`. Si se cumple, ejecuta el bloque asociado.
  if (!pwmReady) {
    // [Guía] Termina el perfil independiente, baja Q1 y registra la causa para distinguir un paro del cambio de
    // [Guía] estado de la EMS.
    stopMode1("PWM_INIT");
    // [Guía] Finaliza aquí la función y devuelve el control al llamador.
    return;
  }
  // [Guía] Actualiza `duty1` con `Config::DUTY_MIN`.
  duty1 = Config::DUTY_MIN;
  // [Guía] Actualiza `stopReason` con `"NONE"`.
  stopReason = "NONE";
  // [Guía] Actualiza `stopElapsedUs` con `stopSkipped = 0`.
  stopElapsedUs = stopSkipped = 0;
  // La inicializacion del temporizador no pertenece a un periodo del PI.
  // [Guía] Actualiza `lastControlUs` con `micros()`.
  lastControlUs = micros();
  // [Guía] Actualiza `lastSuccessfulControlUs` con `lastControlUs`.
  lastSuccessfulControlUs = lastControlUs;
  // [Guía] Actualiza `nextControlUs` con `lastControlUs + Config::CONTROL_US`.
  nextControlUs = lastControlUs + Config::CONTROL_US;
  // [Guía] Actualiza `nextSampleUs` con `lastControlUs + Config::SAMPLE_US`. Este dato representa fecha límite
  // [Guía] del próximo barrido en µs.
  nextSampleUs = lastControlUs + Config::SAMPLE_US;
  // [Guía] Actualiza `mode1Active` con `true`.
  mode1Active = true;
}

// Seleccion MPPT legacy. Comprobar rango ADC, calibracion y tension de bus antes
// de habilitarlo; transferir duty existente evita reiniciar Q1 innecesariamente.
// [Guía] Inicia el P&O del perfil independiente desde el duty ya aplicado a Q1.
void startMPPT() {
  // [Guía] Comprueba `Config::BATTERY_TEST_PROFILE`. Si se cumple, termina la función en esta rama.
  if (Config::BATTERY_TEST_PROFILE) return;
  // [Guía] Comprueba `mpptActive || calibrating || !filtersInitialized`. Si se cumple, termina la función en esta
  // [Guía] rama.
  if (mpptActive || calibrating || !filtersInitialized) return;
  // [Guía] Actualiza `busAdcRange` con `Config::ADC_REF_V`.
  const float busAdcRange = Config::ADC_REF_V
    // [Guía] Completa la declaración o expresión con `* (Config::R_TOP[1] + Config::R_BOTTOM[1]) /
    // [Guía] Config::R_BOTTOM[1]`.
    * (Config::R_TOP[1] + Config::R_BOTTOM[1]) / Config::R_BOTTOM[1]
    // [Guía] Completa la declaración o expresión con `* Config::VOLTAGE_GAIN[1]`.
    * Config::VOLTAGE_GAIN[1];
  // [Guía] Comprueba `Config::MPPT_BUS_MAX_V <= 0 || Config::MPPT_BUS_MAX_V >= 0.95f * busAdcRange`. Si se
  // [Guía] cumple, ejecuta el bloque asociado.
  if (Config::MPPT_BUS_MAX_V <= 0 || Config::MPPT_BUS_MAX_V >= 0.95f * busAdcRange) {
    // [Guía] Actualiza `mpptStatus` con `"SET_BUS_LIMIT"`.
    mpptStatus = "SET_BUS_LIMIT";
    // [Guía] Finaliza aquí la función y devuelve el control al llamador.
    return;
  }
  // [Guía] Comprueba `!measurements.currentValid[0]`. Si se cumple, ejecuta el bloque asociado. Aquí intervienen
  // [Guía] instantánea de medidas y su validez.
  if (!measurements.currentValid[0]) {
    // [Guía] Actualiza `mpptStatus` con `"PV_NOT_CALIBRATED"`.
    mpptStatus = "PV_NOT_CALIBRATED";
    // [Guía] Finaliza aquí la función y devuelve el control al llamador.
    return;
  }
  // [Guía] Comprueba `measurements.voltageRaw[1] >= Config::MPPT_BUS_MAX_V`. Si se cumple, ejecuta el bloque
  // [Guía] asociado. Aquí intervienen instantánea de medidas y su validez.
  if (measurements.voltageRaw[1] >= Config::MPPT_BUS_MAX_V) {
    // [Guía] Termina el perfil independiente, baja Q1 y registra la causa para distinguir un paro del cambio de
    // [Guía] estado de la EMS.
    stopMode1("MPPT_BUS_HIGH");
    // [Guía] Finaliza aquí la función y devuelve el control al llamador.
    return;
  }
  // [Guía] Actualiza `autoStartPending` con `false`.
  autoStartPending = false;
  // [Guía] Invoca `emulatorRamp.cancel` con los argumentos de esta expresión.
  emulatorRamp.cancel("MPPT_SELECTED");
  // [Guía] Actualiza `emulatorBootPending` con `false`.
  emulatorBootPending = false;
  // Si venimos del PI, transferir el duty existente sin otro escritor del PWM.
  // [Guía] Actualiza `mode1Active` con `false`.
  mode1Active = false;
  // [Guía] Actualiza `softStarting` con `false`.
  softStarting = false;
  // [Guía] Comprueba `!pwmReady`. Si se cumple, ejecuta el bloque asociado.
  if (!pwmReady) {
    // [Guía] Actualiza `duty1` con `Config::DUTY_MIN`.
    duty1 = Config::DUTY_MIN;
    // [Guía] Actualiza `pwmReady` con `pwm1.begin(Config::PWM1_HZ, duty1 * 100.0f)`.
    pwmReady = pwm1.begin(Config::PWM1_HZ, duty1 * 100.0f);
    // [Guía] Comprueba `!pwmReady`. Si se cumple, ejecuta el bloque asociado.
    if (!pwmReady) {
      // [Guía] Termina el perfil independiente, baja Q1 y registra la causa para distinguir un paro del cambio de
      // [Guía] estado de la EMS.
      stopMode1("PWM_INIT");
      // [Guía] Finaliza aquí la función y devuelve el control al llamador.
      return;
    }
  }
  // [Guía] Lee el reloj de la placa en microsegundos. El instante sirve para comparar vencimientos y conservar la
  // [Guía] fase del barrido.
  const uint32_t now = micros();
  // [Guía] Prepara `mppt` para empezar desde las medidas o consignas indicadas.
  mppt.begin(duty1, now);
  // [Guía] Actualiza `lastMpptSampleUs` con `now`.
  lastMpptSampleUs = now;
  // [Guía] Actualiza `nextSampleUs` con `now + Config::SAMPLE_US`. Este dato representa fecha límite del próximo
  // [Guía] barrido en µs.
  nextSampleUs = now + Config::SAMPLE_US;
  // [Guía] Actualiza `mpptActive` con `true`.
  mpptActive = true;
  // [Guía] Actualiza `mpptStatus` con `"TRACK"`.
  mpptStatus = "TRACK";
  // [Guía] Actualiza `stopReason` con `"NONE"`.
  stopReason = "NONE";
}

// Alimentar ventanas P&O con muestras reales. sample devuelve true solamente
// cuando hay nueva propuesta de duty; no se reescribe PWM por cada lectura ADC.
// [Guía] Alimenta el P&O independiente con tensión y corriente del PV y aplica una nueva consigna cuando se
// [Guía] completa una decisión.
void updateMPPT() {
  // [Guía] Comprueba `!mpptActive`. Si se cumple, termina la función en esta rama.
  if (!mpptActive) return;
  // [Guía] Actualiza `now` con `measurements.timestampUs`.
  const uint32_t now = measurements.timestampUs;
  // [Guía] Comprueba `uint32_t(now - lastMpptSampleUs) >= Config::CONTROL_TIMEOUT_US`. Si se cumple, ejecuta el
  // [Guía] bloque asociado.
  if (uint32_t(now - lastMpptSampleUs) >= Config::CONTROL_TIMEOUT_US) {
    // [Guía] Termina el perfil independiente, baja Q1 y registra la causa para distinguir un paro del cambio de
    // [Guía] estado de la EMS.
    stopMode1("MPPT_TIMEOUT");
    // [Guía] Finaliza aquí la función y devuelve el control al llamador.
    return;
  }
  // [Guía] Actualiza `lastMpptSampleUs` con `now`.
  lastMpptSampleUs = now;
  // [Guía] Comprueba `!measurements.currentValid[0] || !isfinite(measurements.voltageRaw[1])`. Si se cumple,
  // [Guía] ejecuta el bloque asociado. Aquí intervienen instantánea de medidas y su validez.
  if (!measurements.currentValid[0] || !isfinite(measurements.voltageRaw[1])
      // [Guía] Añade una alternativa suficiente dentro de la condición compuesta. Aquí intervienen instantánea de
      // [Guía] medidas y su validez.
      || !isfinite(measurements.voltageRaw[0]) || !isfinite(measurements.currentRaw[0])
      // [Guía] Añade una alternativa suficiente dentro de la condición compuesta. Aquí intervienen instantánea de
      // [Guía] medidas y su validez.
      || measurements.adcCurrent[0] == 0 || measurements.adcCurrent[0] >= Config::ADC_MAX
      // [Guía] Añade una alternativa suficiente dentro de la condición compuesta. Aquí intervienen instantánea de
      // [Guía] medidas y su validez.
      || measurements.adcVoltage[0] >= Config::ADC_MAX || measurements.adcVoltage[1] >= Config::ADC_MAX) {
    // [Guía] Termina el perfil independiente, baja Q1 y registra la causa para distinguir un paro del cambio de
    // [Guía] estado de la EMS.
    stopMode1("MPPT_SENSOR");
    // [Guía] Finaliza aquí la función y devuelve el control al llamador.
    return;
  }
  // [Guía] Comprueba `measurements.voltageRaw[1] >= Config::MPPT_BUS_MAX_V`. Si se cumple, ejecuta el bloque
  // [Guía] asociado. Aquí intervienen instantánea de medidas y su validez.
  if (measurements.voltageRaw[1] >= Config::MPPT_BUS_MAX_V) {
    // [Guía] Termina el perfil independiente, baja Q1 y registra la causa para distinguir un paro del cambio de
    // [Guía] estado de la EMS.
    stopMode1("MPPT_BUS_HIGH");
    // [Guía] Finaliza aquí la función y devuelve el control al llamador.
    return;
  }
  // [Guía] Comprueba `mppt.sample(measurements.voltageRaw[0], measurements.currentRaw[0], now)`. Si se cumple,
  // [Guía] ejecuta el bloque asociado. Aquí intervienen instantánea de medidas y su validez.
  if (mppt.sample(measurements.voltageRaw[0], measurements.currentRaw[0], now)) {
    // [Guía] Actualiza `duty1` con `mppt.duty()`.
    duty1 = mppt.duty();
    // [Guía] Comprueba `!pwm1.pulse_perc(duty1 * 100.0f)`. Si se cumple, ejecuta `stopMode1("PWM_WRITE")`. Aquí
    // [Guía] intervienen temporizador de la rama Q1.
    if (!pwm1.pulse_perc(duty1 * 100.0f)) stopMode1("PWM_WRITE");
  }
}

// PI legacy temporizado. Un periodo perdido se cuenta, pero no se integra como
// si se hubiera medido: hacerlo aumentaria I con informacion antigua. El dt real
// convierte Ki por segundo y el slew por segundo a incrementos de esta vuelta.
// [Guía] Ejecuta el PI de bus del perfil independiente con su intervalo de control y límites configurados.
void updateMode1() {
  // [Guía] Comprueba `!mode1Active`. Si se cumple, termina la función en esta rama.
  if (!mode1Active) return;
  // [Guía] Lee el reloj de la placa en microsegundos. El instante sirve para comparar vencimientos y conservar la
  // [Guía] fase del barrido.
  const uint32_t now = micros();
  // [Guía] Comprueba `int32_t(now - nextControlUs) < 0`. Si se cumple, termina la función en esta rama.
  if (int32_t(now - nextControlUs) < 0) return;
  // [Guía] Actualiza `skipped` con `(now - nextControlUs) / Config::CONTROL_US`.
  const uint32_t skipped = (now - nextControlUs) / Config::CONTROL_US;
  // [Guía] Acumula en `controlMissed` mediante `skipped`.
  controlMissed += skipped;
  // [Guía] Acumula en `nextControlUs` mediante `(skipped + 1) * Config::CONTROL_US`.
  nextControlUs += (skipped + 1) * Config::CONTROL_US;
  // [Guía] Actualiza `elapsed` con `measurements.timestampUs - lastControlUs`. Este dato representa tiempo
  // [Guía] transcurrido.
  const uint32_t elapsed = measurements.timestampUs - lastControlUs;
  // [Guía] Actualiza `lastControlUs` con `measurements.timestampUs`.
  lastControlUs = measurements.timestampUs;
  // Se comprueba al recuperar CPU: no es un watchdog independiente.
  // [Guía] Comprueba `uint32_t(now - lastSuccessfulControlUs) >= Config::CONTROL_TIMEOUT_US`. Si se cumple,
  // [Guía] ejecuta el bloque asociado.
  if (uint32_t(now - lastSuccessfulControlUs) >= Config::CONTROL_TIMEOUT_US) {
    // [Guía] Actualiza `stopElapsedUs` con `now - lastSuccessfulControlUs`.
    stopElapsedUs = now - lastSuccessfulControlUs;
    // [Guía] Actualiza `stopSkipped` con `skipped`.
    stopSkipped = skipped;
    // [Guía] Termina el perfil independiente, baja Q1 y registra la causa para distinguir un paro del cambio de
    // [Guía] estado de la EMS.
    stopMode1("CONTROL_TIMEOUT");
    // [Guía] Finaliza aquí la función y devuelve el control al llamador.
    return;
  }
  // Un retraso aislado conserva duty e integral; no inventar pasos perdidos.
  // [Guía] Comprueba `skipped > 0 || elapsed >= 2 * Config::CONTROL_US || elapsed == 0`. Si se cumple, ejecuta el
  // [Guía] bloque asociado. Aquí intervienen tiempo transcurrido.
  if (skipped > 0 || elapsed >= 2 * Config::CONTROL_US || elapsed == 0) {
    // [Guía] Actualiza `nextControlUs` con `now + Config::CONTROL_US`.
    nextControlUs = now + Config::CONTROL_US;
    // [Guía] Finaliza aquí la función y devuelve el control al llamador.
    return;
  }
  // [Guía] Comprueba `softStarting`. Si se cumple, ejecuta el bloque asociado.
  if (softStarting) {
    // [Guía] Actualiza `remaining` con `Config::SOFT_START_US - softStartElapsedUs`.
    const uint32_t remaining = Config::SOFT_START_US - softStartElapsedUs;
    // [Guía] Acumula en `softStartElapsedUs` mediante `elapsed < remaining ? elapsed : remaining`.
    softStartElapsedUs += elapsed < remaining ? elapsed : remaining;
    // [Guía] Actualiza `progress` con `float(softStartElapsedUs) / Config::SOFT_START_US`.
    const float progress = float(softStartElapsedUs) / Config::SOFT_START_US;
    // [Guía] Actualiza `busReference` con `startReference + (Config::BUS_REFERENCE_V - startReference) *
    // [Guía] progress`.
    busReference = startReference + (Config::BUS_REFERENCE_V - startReference) * progress;
    // [Guía] Comprueba `softStartElapsedUs == Config::SOFT_START_US`. Si se cumple, ejecuta el bloque asociado.
    if (softStartElapsedUs == Config::SOFT_START_US) {
      // [Guía] Actualiza `busReference` con `Config::BUS_REFERENCE_V`.
      busReference = Config::BUS_REFERENCE_V;
      // [Guía] Actualiza `softStarting` con `false`.
      softStarting = false;
    }
  }
  // [Guía] Actualiza `dt` con `elapsed * 0.000001f`. Este dato representa intervalo efectivo del control en s.
  const float dt = elapsed * 0.000001f;
  // [Guía] Actualiza `error` con `measurements.voltageFiltered[1] - busReference`.
  const float error = measurements.voltageFiltered[1] - busReference;
  // [Guía] Actualiza `duty1` con `busPI.update(error`.
  duty1 = busPI.update(error,
                       // [Guía] Aporta `dt, Config::DUTY_SLEW_PER_SECOND * dt` como parte de los parámetros de la
                       // [Guía] operación. Aquí intervienen intervalo efectivo del control en s.
                       dt, Config::DUTY_SLEW_PER_SECOND * dt);
  // [Guía] Comprueba `diagnosticSamples == 0`. Si se cumple, ejecuta `diagnosticErrorMin = diagnosticErrorMax =
  // [Guía] error`.
  if (diagnosticSamples == 0) diagnosticErrorMin = diagnosticErrorMax = error;
  // [Guía] Comprueba `error < diagnosticErrorMin`. Si se cumple, ejecuta `diagnosticErrorMin = error`.
  if (error < diagnosticErrorMin) diagnosticErrorMin = error;
  // [Guía] Comprueba `error > diagnosticErrorMax`. Si se cumple, ejecuta `diagnosticErrorMax = error`.
  if (error > diagnosticErrorMax) diagnosticErrorMax = error;
  // [Guía] Acumula en `diagnosticErrorSum` mediante `error`.
  diagnosticErrorSum += error;
  // [Guía] Acumula en `diagnosticTrackingSum` mediante `busPI.trackingCorrection()`.
  diagnosticTrackingSum += busPI.trackingCorrection();
  // [Guía] Acumula en `diagnosticSlew` mediante `busPI.slewLimited() ? 1 : 0`.
  diagnosticSlew += busPI.slewLimited() ? 1 : 0;
  // [Guía] Suma una unidad a `diagnosticSamples` para contabilizar el evento de esta rama.
  ++diagnosticSamples;
  // [Guía] Comprueba `!pwm1.pulse_perc(duty1 * 100.0f)`. Si se cumple, ejecuta el bloque asociado. Aquí
  // [Guía] intervienen temporizador de la rama Q1.
  if (!pwm1.pulse_perc(duty1 * 100.0f)) {
    // [Guía] Termina el perfil independiente, baja Q1 y registra la causa para distinguir un paro del cambio de
    // [Guía] estado de la EMS.
    stopMode1("PWM_WRITE");
    // [Guía] Finaliza aquí la función y devuelve el control al llamador.
    return;
  }
  // [Guía] Actualiza `lastSuccessfulControlUs` con `micros()`.
  lastSuccessfulControlUs = micros();
  // [Guía] Suma una unidad a `controlUpdates` para contabilizar el evento de esta rama.
  ++controlUpdates;
}

// Leer seis canales, calibrar ceros, convertir a unidades fisicas y filtrar.
// analogRead devuelve cuentas: el divisor reconstruye tension externa y la
// sensibilidad V/A convierte el desplazamiento respecto al cero en corriente.
// [Guía] Lee las entradas ADC, calibra las seis magnitudes y conserva muestras instantáneas y filtradas.
// [Guía] Actualiza la validez de cada canal y los acumuladores de telemetría.
void acquireMeasurements() {
  // [Guía] Actualiza `start` con `micros()`.
  const uint32_t start = micros();
  // [Guía] Controla las iteraciones mediante `uint8_t i = 0; i < 3; ++i)`.
  for (uint8_t i = 0; i < 3; ++i)
    // [Guía] Actualiza `measurements.adcCurrent[i]` con `analogRead(Config::CURRENT_PINS[i])`.
    measurements.adcCurrent[i] = analogRead(Config::CURRENT_PINS[i]);
  // [Guía] Controla las iteraciones mediante `uint8_t i = 0; i < 3; ++i)`.
  for (uint8_t i = 0; i < 3; ++i)
    // [Guía] Actualiza `measurements.adcVoltage[i]` con `voltageEnabled(i)`.
    measurements.adcVoltage[i] = voltageEnabled(i)
      // [Guía] Define una de las dos salidas de la expresión condicional.
      ? analogRead(Config::VOLTAGE_PINS[i]) : 0;

  // Sumar solamente canales de la mascara. Al completar la media se habilitan
  // esos canales; los demas conservan su calibracion anterior.
  // [Guía] Comprueba `calibrating`. Si se cumple, ejecuta el bloque asociado.
  if (calibrating) {
    // [Guía] Controla las iteraciones mediante `uint8_t i = 0; i < 3; ++i)`.
    for (uint8_t i = 0; i < 3; ++i)
      // [Guía] Comprueba `calibrationMask & (1U << i)`. Si se cumple, ejecuta `zeroSum[i] +=
      // [Guía] measurements.adcCurrent[i]`.
      if (calibrationMask & (1U << i)) zeroSum[i] += measurements.adcCurrent[i];
    // [Guía] Comprueba `++zeroCount == Config::ZERO_SAMPLES`. Si se cumple, ejecuta el bloque asociado.
    if (++zeroCount == Config::ZERO_SAMPLES) {
      // [Guía] Controla las iteraciones mediante `uint8_t i = 0; i < 3; ++i)`.
      for (uint8_t i = 0; i < 3; ++i) {
        // [Guía] Comprueba `!(calibrationMask & (1U << i))`. Si se cumple, pasa a la siguiente iteración.
        if (!(calibrationMask & (1U << i))) continue;
        // [Guía] Actualiza `zeroAdc[i]` con `float(zeroSum[i]) / Config::ZERO_SAMPLES`.
        zeroAdc[i] = float(zeroSum[i]) / Config::ZERO_SAMPLES;
        // [Guía] Actualiza `measurements.currentValid[i]` con `true`.
        measurements.currentValid[i] = true;
      }
      // [Guía] Actualiza `calibrating` con `false`.
      calibrating = false;
      // [Guía] Actualiza `calibrationMask` con `0`.
      calibrationMask = 0;
    }
  }

  // [Guía] Controla las iteraciones mediante `uint8_t i = 0; i < 3; ++i)`.
  for (uint8_t i = 0; i < 3; ++i) {
    // Vexterna = Vpin * (Rarriba+Rabajo)/Rabajo * ganancia de calibracion.
    // [Guía] Actualiza `pinVoltage` con `measurements.adcVoltage[i] * Config::ADC_REF_V / Config::ADC_MAX`.
    const float pinVoltage = measurements.adcVoltage[i] * Config::ADC_REF_V / Config::ADC_MAX;
    // [Guía] Actualiza `measurements.voltageRaw[i]` con `voltageEnabled(i)`.
    measurements.voltageRaw[i] = voltageEnabled(i)
      // [Guía] Define una de las dos salidas de la expresión condicional.
      ? pinVoltage * (Config::R_TOP[i] + Config::R_BOTTOM[i]) / Config::R_BOTTOM[i]
        // [Guía] Completa la declaración o expresión con `* Config::VOLTAGE_GAIN[i] : 0.0f`.
        * Config::VOLTAGE_GAIN[i] : 0.0f;
    // Ajustar sensibilidad nominal a la alimentacion del sensor. CURRENT_SIGN
    // fija la convencion de sentido, no el signo que Python desearia visualizar.
    // [Guía] Actualiza `sensitivity` con `Config::SENSITIVITY_V_A[i] * Config::SENSOR_SUPPLY_V[i] / 5.0f`.
    const float sensitivity = Config::SENSITIVITY_V_A[i] * Config::SENSOR_SUPPLY_V[i] / 5.0f;
    // [Guía] Actualiza `measurements.currentRaw[i]` con `measurements.currentValid[i]`.
    measurements.currentRaw[i] = measurements.currentValid[i]
      // [Guía] Define una de las dos salidas de la expresión condicional. Aquí intervienen instantánea de medidas
      // [Guía] y su validez.
      ? (measurements.adcCurrent[i] - zeroAdc[i]) * Config::ADC_REF_V / Config::ADC_MAX
        // [Guía] Completa la declaración o expresión con `/ sensitivity * Config::CURRENT_GAIN[i] *
        // [Guía] Config::CURRENT_SIGN[i] : 0.0f`.
        / sensitivity * Config::CURRENT_GAIN[i] * Config::CURRENT_SIGN[i] : 0.0f;
    // IIR: y <- y + alpha*(x-y). Primera muestra valida inicializa directamente
    // para no simular una rampa desde cero producida solo por memoria del filtro.
    // [Guía] Comprueba `!currentFilterInitialized[i]`. Si se cumple, ejecuta el bloque asociado.
    if (!currentFilterInitialized[i]) {
      // [Guía] Actualiza `measurements.currentFiltered[i]` con `measurements.currentRaw[i]`.
      measurements.currentFiltered[i] = measurements.currentRaw[i];
      // [Guía] Actualiza `currentFilterInitialized[i]` con `measurements.currentValid[i]`.
      currentFilterInitialized[i] = measurements.currentValid[i];
    // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
    } else {
      // [Guía] Acumula en `measurements.currentFiltered[i]` mediante `Config::CURRENT_ALPHA`.
      measurements.currentFiltered[i] += Config::CURRENT_ALPHA
        // [Guía] Completa la declaración o expresión con `* (measurements.currentRaw[i] -
        // [Guía] measurements.currentFiltered[i])`. Aquí intervienen instantánea de medidas y su validez.
        * (measurements.currentRaw[i] - measurements.currentFiltered[i]);
    }
    // [Guía] Comprueba `!filtersInitialized`. Si se cumple, ejecuta el bloque asociado.
    if (!filtersInitialized) {
      // [Guía] Actualiza `measurements.voltageFiltered[i]` con `measurements.voltageRaw[i]`.
      measurements.voltageFiltered[i] = measurements.voltageRaw[i];
    // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
    } else {
      // [Guía] Acumula en `measurements.voltageFiltered[i]` mediante `Config::VOLTAGE_ALPHA`.
      measurements.voltageFiltered[i] += Config::VOLTAGE_ALPHA
        // [Guía] Completa la declaración o expresión con `* (measurements.voltageRaw[i] -
        // [Guía] measurements.voltageFiltered[i])`. Aquí intervienen instantánea de medidas y su validez.
        * (measurements.voltageRaw[i] - measurements.voltageFiltered[i]);
    }
    // [Guía] Comprueba `measurements.currentValid[i]`. Si se cumple, ejecuta el bloque asociado. Aquí intervienen
    // [Guía] instantánea de medidas y su validez.
    if (measurements.currentValid[i]) {
      // [Guía] Acumula en `displayCurrentSum[i]` mediante `measurements.currentRaw[i]`.
      displayCurrentSum[i] += measurements.currentRaw[i];
      // [Guía] Completa la declaración o expresión con `++displayCurrentCount[i]`.
      ++displayCurrentCount[i];
      // [Guía] Comprueba `Config::BATTERY_TEST_PROFILE`. Si se cumple, ejecuta el bloque asociado.
      if (Config::BATTERY_TEST_PROFILE) {
        // [Guía] Acumula en `liveCurrentSum[i]` mediante `measurements.currentRaw[i]`.
        liveCurrentSum[i] += measurements.currentRaw[i];
        // [Guía] Completa la declaración o expresión con `++liveCurrentCount[i]`.
        ++liveCurrentCount[i];
        // [Guía] Comprueba `voltageEnabled(i) && measurements.adcVoltage[i] < Config::ADC_MAX`. Si se cumple,
        // [Guía] ejecuta el bloque asociado. Aquí intervienen instantánea de medidas y su validez.
        if (voltageEnabled(i) && measurements.adcVoltage[i] < Config::ADC_MAX
            // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen
            // [Guía] instantánea de medidas y su validez.
            && isfinite(measurements.voltageRaw[i]) && isfinite(measurements.currentRaw[i])) {
          // [Guía] Acumula en `livePowerSum[i]` mediante `measurements.voltageRaw[i] *
          // [Guía] measurements.currentRaw[i]`.
          livePowerSum[i] += measurements.voltageRaw[i] * measurements.currentRaw[i];
          // [Guía] Completa la declaración o expresión con `++livePowerCount[i]`.
          ++livePowerCount[i];
        }
      }
    }
    // [Guía] Acumula en `displayVoltageSum[i]` mediante `measurements.voltageRaw[i]`.
    displayVoltageSum[i] += measurements.voltageRaw[i];
    // [Guía] Comprueba `Config::BATTERY_TEST_PROFILE`. Si se cumple, ejecuta `liveVoltageSum[i] +=
    // [Guía] measurements.voltageRaw[i]`.
    if (Config::BATTERY_TEST_PROFILE) liveVoltageSum[i] += measurements.voltageRaw[i];
  }
  // [Guía] Actualiza `filtersInitialized` con `true`.
  filtersInitialized = true;
  // [Guía] Actualiza `measurements.timestampUs` con `start`.
  measurements.timestampUs = start;
  // [Guía] Suma una unidad a `frames` para contabilizar el evento de esta rama.
  ++frames;
  // [Guía] Comprueba `Config::BATTERY_TEST_PROFILE`. Si se cumple, ejecuta `++liveFrames`.
  if (Config::BATTERY_TEST_PROFILE) ++liveFrames;
  // [Guía] Actualiza `elapsed` con `micros() - start`. Este dato representa tiempo transcurrido.
  const uint32_t elapsed = micros() - start;
  // [Guía] Comprueba `elapsed > maxAcquisitionUs`. Si se cumple, ejecuta `maxAcquisitionUs = elapsed`. Aquí
  // [Guía] intervienen tiempo transcurrido.
  if (elapsed > maxAcquisitionUs) maxAcquisitionUs = elapsed;
}

// Redondeo simetrico al entero escalado: por ejemplo -0.123 A -> -123 mA.
// [Guía] Convierte una magnitud expresada en unidades físicas a un entero en milésimas para la telemetría serie.
// [Guía] Recibe `value`.
long milliUnits(float value) {
  // [Guía] Entrega `long(value * 1000.0f + (value >= 0 ? 0.5f : -0.5f))` al llamador.
  return long(value * 1000.0f + (value >= 0 ? 0.5f : -0.5f));
}

// Media de visualizacion independiente del filtro usado por el PI.
// [Guía] Expone `displayCurrentCount[channel] ? milliUnits(displayCurrentSum[channel] /
// [Guía] displayCurrentCount[channel]) : 0` al módulo que consulta display corriente milli corriente. Recibe
// [Guía] `channel`.
long displayCurrentMilliAmps(uint8_t channel) {
  // [Guía] Entrega `displayCurrentCount[channel]` al llamador.
  return displayCurrentCount[channel]
    // [Guía] Define una de las dos salidas de la expresión condicional.
    ? milliUnits(displayCurrentSum[channel] / displayCurrentCount[channel]) : 0;
}

// Fotografia compacta y atomica para Tk. Se envia aun sin margen150us; solo
// formatea los valores que pinta el panel. El diagnostico completo no cambia
// de formato ni pierde lineas por esto, y BK_REQ conserva prioridad.
// [Guía] Construye la línea LIVE compacta en magnitudes escaladas y máscaras de validez para que la aplicación
// [Guía] dibuje las series. El resultado es una decisión booleana para el llamador.
bool prepareLiveTelemetry() {
  // [Guía] Comprueba `txPosition != txLength || !liveFrames || !Config::BATTERY_TEST_PROFILE`. Si se cumple,
  // [Guía] termina la función en esta rama.
  if (txPosition != txLength || !liveFrames || !Config::BATTERY_TEST_PROFILE) return false;
  // [Guía] Actualiza `length` con `snprintf(txBuffer, sizeof(txBuffer)`.
  const int length = snprintf(txBuffer, sizeof(txBuffer),
    "LIVE PV=%ld BUS=%ld BAT=%ld BAT_on=%u IPV=%ld IBUS=%ld IBAT=%ld I_ok=%u,%u,%u "
    "PPV_mW=%ld PBUS_mW=%ld PBAT_mW=%ld P_ok=%u,%u,%u "
    "PROFILE=1 MODE=%s STATE=%s D1_permille=%ld D2_permille=%ld D3_permille=%ld "
    "MPPT=%u FAULT=%u VERSION=%s SOC_milli=%ld SOC_ok=%u CONTINUOUS=%u SOC_STATE=%s "
    "LINK=%u R_ack_mOhm=%ld R_ok=%u LOAD_ack=%u LOAD_ok=%u BK_STATE=%s "
    // [Guía] Completa la declaración o expresión con `"M2=%u M2_IREF_mA=%ld M2_IBUS_mA=%ld M2_DEMAND_mA=%ld
    // [Guía] M2_IBUS_OK=%u M2_STAGE=%s M2_IDLE_ms=%lu M2_NEED_ms=%lu M…`.
    "M2=%u M2_IREF_mA=%ld M2_IBUS_mA=%ld M2_DEMAND_mA=%ld M2_IBUS_OK=%u M2_STAGE=%s M2_IDLE_ms=%lu M2_NEED_ms=%lu M2_RESETS=%lu M2_BLOCK=%s M2_WINDOW_ms=%lu M2_GUARD_mA=%ld M2_GUARD_OK=%u M2_GUARD_ms=%lu\r\n",
    // [Guía] Convierte una magnitud expresada en unidades físicas a un entero en milésimas para la telemetría
    // [Guía] serie.
    milliUnits(liveVoltageSum[0] / liveFrames), milliUnits(liveVoltageSum[1] / liveFrames),
    // [Guía] Convierte una magnitud expresada en unidades físicas a un entero en milésimas para la telemetría
    // [Guía] serie.
    milliUnits(liveVoltageSum[2] / liveFrames), unsigned(voltageEnabled(2)),
    // [Guía] Completa la declaración o expresión con `liveCurrentCount[0] ? milliUnits(liveCurrentSum[0] /
    // [Guía] liveCurrentCount[0]) : 0`.
    liveCurrentCount[0] ? milliUnits(liveCurrentSum[0] / liveCurrentCount[0]) : 0,
    // [Guía] Completa la declaración o expresión con `liveCurrentCount[1] ? milliUnits(liveCurrentSum[1] /
    // [Guía] liveCurrentCount[1]) : 0`.
    liveCurrentCount[1] ? milliUnits(liveCurrentSum[1] / liveCurrentCount[1]) : 0,
    // [Guía] Completa la declaración o expresión con `liveCurrentCount[2] ? milliUnits(liveCurrentSum[2] /
    // [Guía] liveCurrentCount[2]) : 0`.
    liveCurrentCount[2] ? milliUnits(liveCurrentSum[2] / liveCurrentCount[2]) : 0,
    // [Guía] Invoca `unsigned` con los argumentos de esta expresión. Aquí intervienen instantánea de medidas y su
    // [Guía] validez.
    unsigned(measurements.currentValid[0] && liveCurrentCount[0] == liveFrames),
    // [Guía] Invoca `unsigned` con los argumentos de esta expresión. Aquí intervienen instantánea de medidas y su
    // [Guía] validez.
    unsigned(measurements.currentValid[1] && liveCurrentCount[1] == liveFrames),
    // [Guía] Invoca `unsigned` con los argumentos de esta expresión. Aquí intervienen instantánea de medidas y su
    // [Guía] validez.
    unsigned(measurements.currentValid[2] && liveCurrentCount[2] == liveFrames),
    // [Guía] Completa la declaración o expresión con `livePowerCount[0] ? milliUnits(livePowerSum[0] /
    // [Guía] livePowerCount[0]) : 0`.
    livePowerCount[0] ? milliUnits(livePowerSum[0] / livePowerCount[0]) : 0,
    // [Guía] Completa la declaración o expresión con `livePowerCount[1] ? milliUnits(livePowerSum[1] /
    // [Guía] livePowerCount[1]) : 0`.
    livePowerCount[1] ? milliUnits(livePowerSum[1] / livePowerCount[1]) : 0,
    // [Guía] Completa la declaración o expresión con `livePowerCount[2] ? milliUnits(livePowerSum[2] /
    // [Guía] livePowerCount[2]) : 0`.
    livePowerCount[2] ? milliUnits(livePowerSum[2] / livePowerCount[2]) : 0,
    // [Guía] Invoca `unsigned` con los argumentos de esta expresión.
    unsigned(livePowerCount[0] == liveFrames), unsigned(livePowerCount[1] == liveFrames),
    // [Guía] Invoca `unsigned` con los argumentos de esta expresión.
    unsigned(livePowerCount[2] == liveFrames),
    // [Guía] Invoca `batteryTest.mode` con los argumentos de esta expresión.
    batteryTest.mode(), batteryTest.reason(), milliUnits(duty1), milliUnits(duty2), milliUnits(duty3),
    // [Guía] Invoca `unsigned` con los argumentos de esta expresión.
    unsigned(batteryTest.mpptRunning()), unsigned(batteryTest.faulted()), Config::EMS_FIRMWARE_ID,
    // [Guía] Invoca `batterySoc.initialized` con los argumentos de esta expresión. Aquí intervienen estimador por
    // [Guía] conteo de carga.
    batterySoc.initialized() ? milliUnits(batterySoc.percent()) : -1L,
    // [Guía] Invoca `unsigned` con los argumentos de esta expresión. Aquí intervienen estimador por conteo de
    // [Guía] carga.
    unsigned(batterySoc.initialized()), unsigned(batterySoc.continuous()), batterySoc.status(),
    // [Guía] Invoca `unsigned` con los argumentos de esta expresión.
    unsigned(emulador.enlace), milliUnits(emulador.resistenciaConfirmadaOhm),
    // [Guía] Invoca `unsigned` con los argumentos de esta expresión.
    unsigned(emulador.resistenciaConfirmada), unsigned(emulador.consumoConfirmado),
    // [Guía] Invoca `unsigned` con los argumentos de esta expresión.
    unsigned(emulador.estadoConsumoValido), emulador.estado,
    // [Guía] Invoca `unsigned` con los argumentos de esta expresión.
    unsigned(batteryTest.m2Path()), milliUnits(batteryTest.m2CurrentReference()),
    // [Guía] Convierte una magnitud expresada en unidades físicas a un entero en milésimas para la telemetría
    // [Guía] serie.
    milliUnits(batteryTest.m2BusCurrent()), milliUnits(batteryTest.m2DemandCurrent()),
    // [Guía] Invoca `unsigned` con los argumentos de esta expresión.
    unsigned(batteryTest.m2BusCurrentReady()), batteryTest.m2Stage(),
    // [Guía] Completa la declaración o expresión con `(unsigned long)batteryTest.m2IdleMilliseconds(), (unsigned
    // [Guía] long)(Config::M2_BUS_IDLE_US / 1000)`.
    (unsigned long)batteryTest.m2IdleMilliseconds(), (unsigned long)(Config::M2_BUS_IDLE_US / 1000),
    // [Guía] Completa la declaración o expresión con `(unsigned long)batteryTest.m2IdleResets(),
    // [Guía] batteryTest.m2Block()`.
    (unsigned long)batteryTest.m2IdleResets(), batteryTest.m2Block(),
    // [Guía] Completa la declaración o expresión con `(unsigned long)(Config::M2_BUS_CURRENT_WINDOW_US / 1000)`.
    (unsigned long)(Config::M2_BUS_CURRENT_WINDOW_US / 1000),
    // [Guía] Convierte una magnitud expresada en unidades físicas a un entero en milésimas para la telemetría
    // [Guía] serie.
    milliUnits(batteryTest.m2GuardCurrent()), unsigned(batteryTest.m2GuardReady()),
    // [Guía] Completa la declaración o expresión con `(unsigned long)(Config::M2_BUS_CURRENT_WINDOW_US *
    // [Guía] Config::M2_BUS_GUARD_WINDOWS / 1000))`.
    (unsigned long)(Config::M2_BUS_CURRENT_WINDOW_US * Config::M2_BUS_GUARD_WINDOWS / 1000));
  // [Guía] Comprueba `length <= 0 || size_t(length) >= sizeof(txBuffer)`. Si se cumple, termina la función en
  // [Guía] esta rama.
  if (length <= 0 || size_t(length) >= sizeof(txBuffer)) return false;
  // [Guía] Actualiza `txLength` con `size_t(length); txPosition = 0`.
  txLength = size_t(length); txPosition = 0;
  // [Guía] Actualiza `liveFrames` con `0`.
  liveFrames = 0;
  // [Guía] Controla las iteraciones mediante `uint8_t i = 0; i < 3; ++i)`.
  for (uint8_t i = 0; i < 3; ++i) {
    // [Guía] Actualiza `liveVoltageSum[i]` con `liveCurrentSum[i] = livePowerSum[i] = 0`.
    liveVoltageSum[i] = liveCurrentSum[i] = livePowerSum[i] = 0;
    // [Guía] Actualiza `liveCurrentCount[i]` con `livePowerCount[i] = 0`.
    liveCurrentCount[i] = livePowerCount[i] = 0;
  }
  // [Guía] Entrega `true` al llamador.
  return true;
}

// Preparar una fotografia de telemetria, solo cuando el buffer anterior salio.
// snprintf une etiquetas y valores en el mismo orden; cada % corresponde a un
// argumento posterior. No confundir *_permille (fraccion*1000) con porcentaje.
// TRIP conserva la muestra del disparo, no la media del bus tras apagar PWM.
// [Guía] Prepara el siguiente bloque de diagnóstico, incluidas medidas, estado EMS, ciclos de trabajo, SOC y
// [Guía] confirmaciones de la B&K.
bool prepareReport() {
  // [Guía] Comprueba `txPosition != txLength || frames == 0`. Si se cumple, termina la función en esta rama.
  if (txPosition != txLength || frames == 0) return false;
  // [Guía] Lee el reloj de la placa en microsegundos. El instante sirve para comparar vencimientos y conservar la
  // [Guía] fase del barrido.
  const uint32_t now = micros();
  // [Guía] Actualiza `elapsed` con `now - reportStartUs`. Este dato representa tiempo transcurrido.
  const uint32_t elapsed = now - reportStartUs;
  // [Guía] Completa la declaración o expresión con `const unsigned long rate = elapsed ? (uint64_t(frames) *
  // [Guía] 1000000ULL) / elapsed : 0`. Aquí intervienen tiempo transcurrido.
  const unsigned long rate = elapsed ? (uint64_t(frames) * 1000000ULL) / elapsed : 0;
  // A1 mide ambas cargas: Req_total NO es la resistencia de la B&K.
  // No dividir cerca de cero: 50 mA es un umbral de visualizacion configurable.
  // [Guía] Actualiza `busV` con `displayVoltageSum[1] / frames`.
  const float busV = displayVoltageSum[1] / frames;
  // [Guía] Actualiza `busI` con `displayCurrentCount[1]`.
  const float busI = displayCurrentCount[1]
    // [Guía] Define una de las dos salidas de la expresión condicional.
    ? displayCurrentSum[1] / displayCurrentCount[1] : 0;
  // [Guía] Actualiza `reqValid` con `measurements.currentValid[1] && displayCurrentCount[1] == frames`.
  const bool reqValid = measurements.currentValid[1] && displayCurrentCount[1] == frames
    // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
    && busI > 0.05f && busV > 0;
  // [Guía] Actualiza `trip` con `batteryTest.faultSnapshot()`.
  const BatteryTestController::FaultSnapshot& trip = batteryTest.faultSnapshot();
  // [Guía] Actualiza `connection` con `batteryTest.connectionDiagnostic()`.
  const BatteryTestController::ConnectionDiagnostic& connection = batteryTest.connectionDiagnostic();
  // Evitar convertir NaN a entero; FINITE distingue invalido de cero real.
  // [Guía] Actualiza `tripFinite` con `unsigned(isfinite(trip.sample.batteryIRaw))`.
  const unsigned tripFinite = unsigned(isfinite(trip.sample.batteryIRaw))
    // [Guía] Completa la declaración o expresión con `| (unsigned(isfinite(trip.sample.batteryI)) << 1)`.
    | (unsigned(isfinite(trip.sample.batteryI)) << 1)
    // [Guía] Completa la declaración o expresión con `| (unsigned(isfinite(trip.sample.busRaw)) << 2)`.
    | (unsigned(isfinite(trip.sample.busRaw)) << 2)
    // [Guía] Completa la declaración o expresión con `| (unsigned(isfinite(trip.sample.batteryV)) << 3)`.
    | (unsigned(isfinite(trip.sample.batteryV)) << 3)
    // [Guía] Completa la declaración o expresión con `| (unsigned(isfinite(trip.sample.pvRaw)) << 4)`.
    | (unsigned(isfinite(trip.sample.pvRaw)) << 4)
    // [Guía] Completa la declaración o expresión con `| (unsigned(isfinite(trip.sample.pvIRaw)) << 5)`.
    | (unsigned(isfinite(trip.sample.pvIRaw)) << 5)
    // [Guía] Completa la declaración o expresión con `| (unsigned(isfinite(trip.sample.pvI)) << 6)`.
    | (unsigned(isfinite(trip.sample.pvI)) << 6);
  // [Guía] Lambda de empaquetado. Recibe la magnitud capturada, la convierte a milésimas si es finita y usa cero
  // [Guía] en el campo numérico en caso contrario. La validez viaja por separado.
  const auto tripMilli = [](float value) { return isfinite(value) ? milliUnits(value) : 0L; };
  // [Guía] Actualiza `emsMppt` con `Config::BATTERY_TEST_PROFILE`.
  const bool emsMppt = Config::BATTERY_TEST_PROFILE;
  // [Guía] Actualiza `mpptDecision` con `emsMppt`.
  const char* mpptDecision = emsMppt
    // [Guía] Define una de las dos salidas de la expresión condicional.
    ? (batteryTest.mpptRunning() ? batteryTest.mpptDecision() : "INACTIVE")
    // [Guía] Define una de las dos salidas de la expresión condicional.
    : (mpptActive ? mppt.decision() : "INACTIVE");
  // [Guía] Actualiza `length` con `snprintf(txBuffer, sizeof(txBuffer)`.
  const int length = snprintf(txBuffer, sizeof(txBuffer),
    "CTRL SOURCE=%s N=%lu Eavg_mV=%ld Emin_mV=%ld Emax_mV=%ld Int_permille=%ld TrackSum_permille=%ld SlewN=%lu Kp_x1000=%ld Ki_x1000=%ld STOP=%s StopDt_us=%lu StopSkip=%lu\r\n"
    "V_mV PV=%ld BUS=%ld BAT=%ld(BAT_on=%u) | I_mA PV=%ld BUS=%ld BAT=%ld | I_ok=%u,%u,%u CALmask=%u CAL=%lu/%lu | Hz=%lu lost=%lu acq_us=%lu late_us=%lu | zeroADC=%ld,%ld,%ld | M1=%u D_permille=%ld PI_n=%lu PI_lost=%lu REF_mV=%ld SS=%u | MPPT=%u MPPT_state=%s Ppv_mW=%ld Probe=%u EMS_mode=%s Q2_permille=%ld Q3_permille=%ld\r\n"
    "BK_STATE ENABLE=%u SEND_R=%u LINK=%u R_mOhm=%ld R_ack_mOhm=%ld R_ok=%u LOAD_req=%u LOAD_ack=%u LOAD_ok=%u STATE=%s Req_total_mOhm=%ld Req_ok=%u\r\n"
    "OBS N=%lu PVmin_mV=%ld PVmax_mV=%ld BUSmin_mV=%ld BUSmax_mV=%ld Dmin_permille=%ld Dmax_permille=%ld Ppv_mW=%ld Pbus_mW=%ld Ppv_ok=%u Pbus_ok=%u\r\n"
    "AUTO_R ACTIVE=%u STATE=%s SAFE_R_mOhm=%ld TARGET_mV=%ld HW_READY=%u BOOT_PENDING=%u BOOT_STATE=%s\r\n"
    "BUS_OV ACTIVE=%u STATE=%s TRIGGER_mV=%ld R_mOhm=%ld HOLD_MS=%lu CAUSE=%s RECOVERY_mV=%ld RECOVERY_MS=%lu LAST_STOP=%s ACK_STATE=%s ACK_OK=%lu ACK_REJECT=%lu PENDING_ID=%lu RETRIES=%u\r\n"
    "SOC PERCENT_milli=%ld INITIALIZED=%u CONTINUOUS=%u GAP=%u STATE=%s INPUT=%s CAPACITY_mAh=%ld\r\n"
    "EMS PROFILE=%u MODE=%s STATE=%s D1_permille=%ld D2_permille=%ld D3_permille=%ld MPPT=%u FAULT=%u COMMAND=%s KP3_x1e6=%ld KI3_x1e6=%ld VERSION=%s Vbat_ctrl_mV=%ld REF_mV=%ld CTRL_N=%lu CTRL_LOST=%lu Ihold=%u Iwarnings=%lu Ipeak_mA=%ld BUS_EMS_mV=%ld MPPT_W_ms=%lu BAT_LINK_EST=%u IBAT_EMS_mA=%ld M1_STAGE=%s VPV_EMS_mV=%ld E1_mV=%ld I1_permille=%ld KP1_x1e6=%ld KI1_x1e6=%ld M1_RETRIES=%u M1_LIMIT=%s BUS_PI1_mV=%ld BAT_LINK_STATE=%s IBAT_WIN_mA=%ld IBAT_WIN_READY=%u BAT_RESPONSE=%s LAST_SOURCE=%s E3_mV=%ld I3_permille=%ld ACTION3_x1000=%ld IBAT_BASE_mA=%ld IBAT_BASE_OK=%u PV_CHG_MIN_mV=%ld PREP_MS=%lu Q2_FLOW_SEEN=%u Q2_DRIVE_OK=%u Q2_IDLE_mA=%ld PV_PREP_STAGE=%s PV_PREP_LAST=%s CHG_TRIES=%u PREP_TRIES=%u PV_GAP_mV=%ld PV_PREP_TARGET_mV=%ld BAT_CONFIRM_ms=%lu BAT_CONFIRM_NEED_ms=%lu BAT_CONFIRM_RESETS=%lu BAT_CONFIRM_RESET=%s M2=%u M2_IREF_mA=%ld M2_IBUS_mA=%ld M2_DEMAND_mA=%ld M2_IBUS_OK=%u M2_STAGE=%s M2_IDLE_ms=%lu M2_NEED_ms=%lu M2_RESETS=%lu M2_BLOCK=%s M2_WINDOW_ms=%lu M2_GUARD_mA=%ld M2_GUARD_OK=%u M2_GUARD_ms=%lu\r\n"
    "MPPT_DIAG DECISION=%s STEP_ppm=%ld DP_mW=%ld P_mW=%ld BASE_mW=%ld TARGET_ppm=%ld FLAT_N=%lu UPDATES=%lu CONTROL=%s VREF_mV=%ld VSTEP_mV=%ld\r\n"
    "TRIP VALID=%u REASON=%s TIME_us=%lu FINITE=%u Iraw_mA=%ld Ifilt_mA=%ld Vbus_mV=%ld Vbat_mV=%ld D1_permille=%ld D2_permille=%ld D3_permille=%ld ADC_Ibat=%u ADC_Vbus=%u ADC_Vbat=%u ZERO_Ibat_x1000=%ld Vpv_mV=%ld Ipv_raw_mA=%ld Ipv_filt_mA=%ld\r\n"
    // [Guía] Completa la declaración o expresión con `"CONNECTION QUALIFY=%u STATE=%s N=%lu Imax_mA=%ld
    // [Guía] FIRST_us=%lu ADC_Ibat=%u\r\n"`.
    "CONNECTION QUALIFY=%u STATE=%s N=%lu Imax_mA=%ld FIRST_us=%lu ADC_Ibat=%u\r\n",
    // [Guía] Completa la declaración o expresión con `Config::BATTERY_TEST_PROFILE ? "LEGACY_INACTIVE" : "Q1"`.
    Config::BATTERY_TEST_PROFILE ? "LEGACY_INACTIVE" : "Q1",
    // [Guía] Completa la declaración o expresión con `(unsigned long)diagnosticSamples`.
    (unsigned long)diagnosticSamples,
    // [Guía] Convierte una magnitud expresada en unidades físicas a un entero en milésimas para la telemetría
    // [Guía] serie.
    milliUnits(diagnosticSamples ? diagnosticErrorSum / diagnosticSamples : 0.0f),
    // [Guía] Convierte una magnitud expresada en unidades físicas a un entero en milésimas para la telemetría
    // [Guía] serie.
    milliUnits(diagnosticSamples ? diagnosticErrorMin : 0.0f),
    // [Guía] Convierte una magnitud expresada en unidades físicas a un entero en milésimas para la telemetría
    // [Guía] serie.
    milliUnits(diagnosticSamples ? diagnosticErrorMax : 0.0f),
    // [Guía] Convierte una magnitud expresada en unidades físicas a un entero en milésimas para la telemetría
    // [Guía] serie.
    milliUnits(busPI.integral()), milliUnits(diagnosticTrackingSum),
    // [Guía] Completa la declaración o expresión con `(unsigned long)diagnosticSlew, milliUnits(Config::BUS_KP),
    // [Guía] milliUnits(Config::BUS_KI)`.
    (unsigned long)diagnosticSlew, milliUnits(Config::BUS_KP), milliUnits(Config::BUS_KI),
    // [Guía] Aporta `stopReason, (unsigned long)stopElapsedUs, (unsigned long)stopSkipped` como parte de los
    // [Guía] parámetros de la operación.
    stopReason, (unsigned long)stopElapsedUs, (unsigned long)stopSkipped,
    // [Guía] Convierte una magnitud expresada en unidades físicas a un entero en milésimas para la telemetría
    // [Guía] serie.
    milliUnits(displayVoltageSum[0] / frames), milliUnits(displayVoltageSum[1] / frames),
    // [Guía] Convierte una magnitud expresada en unidades físicas a un entero en milésimas para la telemetría
    // [Guía] serie.
    milliUnits(displayVoltageSum[2] / frames), unsigned(voltageEnabled(2)),
    // [Guía] Invoca `displayCurrentMilliAmps` con los argumentos de esta expresión.
    displayCurrentMilliAmps(0), displayCurrentMilliAmps(1), displayCurrentMilliAmps(2),
    // [Guía] Invoca `unsigned` con los argumentos de esta expresión. Aquí intervienen instantánea de medidas y su
    // [Guía] validez.
    unsigned(measurements.currentValid[0]), unsigned(measurements.currentValid[1]),
    // [Guía] Invoca `unsigned` con los argumentos de esta expresión. Aquí intervienen instantánea de medidas y su
    // [Guía] validez.
    unsigned(measurements.currentValid[2]), unsigned(calibrationMask),
    // [Guía] Completa la declaración o expresión con `(unsigned long)zeroCount, (unsigned
    // [Guía] long)Config::ZERO_SAMPLES, rate`.
    (unsigned long)zeroCount, (unsigned long)Config::ZERO_SAMPLES, rate,
    // [Guía] Completa la declaración o expresión con `(unsigned long)missed, (unsigned long)maxAcquisitionUs,
    // [Guía] (unsigned long)maxLatenessUs`.
    (unsigned long)missed, (unsigned long)maxAcquisitionUs, (unsigned long)maxLatenessUs,
    // [Guía] Invoca `long` con los argumentos de esta expresión.
    long(zeroAdc[0]), long(zeroAdc[1]), long(zeroAdc[2]), unsigned(mode1Active),
    // [Guía] Convierte una magnitud expresada en unidades físicas a un entero en milésimas para la telemetría
    // [Guía] serie.
    milliUnits(duty1), (unsigned long)controlUpdates, (unsigned long)controlMissed,
    // [Guía] Convierte una magnitud expresada en unidades físicas a un entero en milésimas para la telemetría
    // [Guía] serie.
    milliUnits(Config::BATTERY_TEST_PROFILE ? batteryTest.reference() : busReference),
    // [Guía] Invoca `unsigned` con los argumentos de esta expresión.
    unsigned(softStarting), unsigned(mpptActive), mpptStatus,
    // [Guía] Convierte una magnitud expresada en unidades físicas a un entero en milésimas para la telemetría
    // [Guía] serie.
    milliUnits(Config::BATTERY_TEST_PROFILE ? batteryTest.mpptPower() : mppt.power()),
    // [Guía] Invoca `unsigned` con los argumentos de esta expresión.
    unsigned(Config::BATTERY_TEST_PROFILE ? batteryTest.mpptProbing() : mppt.probing()),
    // [Guía] Completa la declaración o expresión con `Config::BATTERY_TEST_PROFILE ? batteryTest.mode() :
    // [Guía] "LEGACY", milliUnits(duty2), milliUnits(duty3)`.
    Config::BATTERY_TEST_PROFILE ? batteryTest.mode() : "LEGACY", milliUnits(duty2), milliUnits(duty3),
    // [Guía] Invoca `unsigned` con los argumentos de esta expresión.
    unsigned(emulador.habilitarEmulador), unsigned(emulador.enviarNuevaResistencia),
    // [Guía] Invoca `unsigned` con los argumentos de esta expresión.
    unsigned(emulador.enlace), milliUnits(emulador.resistenciaOhm),
    // [Guía] Convierte una magnitud expresada en unidades físicas a un entero en milésimas para la telemetría
    // [Guía] serie.
    milliUnits(emulador.resistenciaConfirmadaOhm), unsigned(emulador.resistenciaConfirmada),
    // [Guía] Invoca `unsigned` con los argumentos de esta expresión.
    unsigned(emulador.consumoSolicitado), unsigned(emulador.consumoConfirmado),
    // [Guía] Invoca `unsigned` con los argumentos de esta expresión.
    unsigned(emulador.estadoConsumoValido), emulador.estado,
    // [Guía] Completa la declaración o expresión con `reqValid ? milliUnits(busV / busI) : 0,
    // [Guía] unsigned(reqValid)`.
    reqValid ? milliUnits(busV / busI) : 0, unsigned(reqValid),
    // [Guía] Completa la declaración o expresión con `(unsigned long)controlWindow.count`.
    (unsigned long)controlWindow.count,
    // [Guía] Convierte una magnitud expresada en unidades físicas a un entero en milésimas para la telemetría
    // [Guía] serie.
    milliUnits(controlWindow.pvMin), milliUnits(controlWindow.pvMax),
    // [Guía] Convierte una magnitud expresada en unidades físicas a un entero en milésimas para la telemetría
    // [Guía] serie.
    milliUnits(controlWindow.busMin), milliUnits(controlWindow.busMax),
    // [Guía] Convierte una magnitud expresada en unidades físicas a un entero en milésimas para la telemetría
    // [Guía] serie.
    milliUnits(controlWindow.dutyMin), milliUnits(controlWindow.dutyMax),
    // [Guía] Convierte una magnitud expresada en unidades físicas a un entero en milésimas para la telemetría
    // [Guía] serie.
    milliUnits(controlWindow.pvPower()), milliUnits(controlWindow.busPower()),
    // [Guía] Invoca `unsigned` con los argumentos de esta expresión.
    unsigned(controlWindow.pvPowerValid()), unsigned(controlWindow.busPowerValid()),
    // [Guía] Invoca `unsigned` con los argumentos de esta expresión.
    unsigned(emulatorRamp.active()), emulatorRamp.status(),
    // [Guía] Convierte una magnitud expresada en unidades físicas a un entero en milésimas para la telemetría
    // [Guía] serie.
    milliUnits(emulatorRamp.lastStableOhm()), milliUnits(Config::EMULATOR_PV_TARGET_V),
    // [Guía] Invoca `unsigned` con los argumentos de esta expresión.
    unsigned(Config::AUTO_EMULATOR_HARDWARE_READY), unsigned(emulatorBootPending),
    // [Guía] Completa la declaración o expresión con `emulatorBootPending ? emulatorBootState :
    // [Guía] "DONE_OR_CANCELLED"`.
    emulatorBootPending ? emulatorBootState : "DONE_OR_CANCELLED",
    // [Guía] Invoca `unsigned` con los argumentos de esta expresión.
    unsigned(busOvervoltage.active()), busOvervoltage.status(),
    // [Guía] Convierte una magnitud expresada en unidades físicas a un entero en milésimas para la telemetría
    // [Guía] serie.
    milliUnits(Config::BUS_OV_TRIGGER_V), milliUnits(busOvervoltage.requestedOhm()),
    // [Guía] Completa la declaración o expresión con `(unsigned long)Config::BUS_OV_TRIGGER_MS`.
    (unsigned long)Config::BUS_OV_TRIGGER_MS,
    // [Guía] Invoca `busOvervoltage.cause` con los argumentos de esta expresión.
    busOvervoltage.cause(), milliUnits(Config::BUS_RECOVERY_LOAD_V),
    // [Guía] Completa la declaración o expresión con `(unsigned long)Config::BUS_RECOVERY_LOAD_MS,
    // [Guía] busOvervoltage.lastStopReason()`.
    (unsigned long)Config::BUS_RECOVERY_LOAD_MS, busOvervoltage.lastStopReason(),
    // [Guía] Invoca `emulador.ackStatus` con los argumentos de esta expresión.
    emulador.ackStatus(), (unsigned long)emulador.acceptedAcks(), (unsigned long)emulador.rejectedAcks(),
    // [Guía] Completa la declaración o expresión con `(unsigned long)emulador.pendingId(),
    // [Guía] unsigned(emulador.retries())`.
    (unsigned long)emulador.pendingId(), unsigned(emulador.retries()),
    // [Guía] Invoca `batterySoc.initialized` con los argumentos de esta expresión. Aquí intervienen estimador por
    // [Guía] conteo de carga.
    batterySoc.initialized() ? milliUnits(batterySoc.percent()) : -1L,
    // [Guía] Invoca `unsigned` con los argumentos de esta expresión. Aquí intervienen estimador por conteo de
    // [Guía] carga.
    unsigned(batterySoc.initialized()), unsigned(batterySoc.continuous()),
    // [Guía] Invoca `unsigned` con los argumentos de esta expresión. Aquí intervienen estimador por conteo de
    // [Guía] carga y separación temporal entre muestras.
    unsigned(batterySoc.gap()), batterySoc.status(), socInputStatus,
    // [Guía] Convierte una magnitud expresada en unidades físicas a un entero en milésimas para la telemetría
    // [Guía] serie.
    milliUnits(Config::BATTERY_CAPACITY_AH),
    // [Guía] Invoca `unsigned` con los argumentos de esta expresión.
    unsigned(Config::BATTERY_TEST_PROFILE), Config::BATTERY_TEST_PROFILE ? batteryTest.mode() : "LEGACY",
    // [Guía] Invoca `batteryTest.reason` con los argumentos de esta expresión.
    batteryTest.reason(), milliUnits(duty1), milliUnits(duty2), milliUnits(duty3),
    // [Guía] Invoca `unsigned` con los argumentos de esta expresión.
    unsigned(batteryTest.mpptRunning()), unsigned(batteryTest.faulted()), batteryCommandStatus,
    // [Guía] Invoca `long` con los argumentos de esta expresión.
    long(Config::Q3_BUS_KP * 1000000.0f), long(Config::Q3_BUS_KI * 1000000.0f),
    // [Guía] Aporta `Config::EMS_FIRMWARE_ID, milliUnits(batteryTest.batteryControlVoltage(` como parte de los
    // [Guía] parámetros de la operación.
    Config::EMS_FIRMWARE_ID, milliUnits(batteryTest.batteryControlVoltage()),
    // [Guía] Convierte una magnitud expresada en unidades físicas a un entero en milésimas para la telemetría
    // [Guía] serie.
    milliUnits(batteryTest.reference()),
    // [Guía] Completa la declaración o expresión con `(unsigned long)batteryTest.controlUpdates(), (unsigned
    // [Guía] long)batteryTest.controlMissed()`.
    (unsigned long)batteryTest.controlUpdates(), (unsigned long)batteryTest.controlMissed(),
    // [Guía] Invoca `unsigned` con los argumentos de esta expresión.
    unsigned(batteryTest.currentPending()), (unsigned long)batteryTest.currentSamples(),
    // [Guía] Convierte una magnitud expresada en unidades físicas a un entero en milésimas para la telemetría
    // [Guía] serie.
    milliUnits(batteryTest.currentMaximum()),
    // [Guía] Convierte una magnitud expresada en unidades físicas a un entero en milésimas para la telemetría
    // [Guía] serie.
    milliUnits(batteryTest.supervisoryBus()), (unsigned long)(batteryTest.mpptWindowActiveUs() / 1000),
    // [Guía] Invoca `unsigned` con los argumentos de esta expresión.
    unsigned(batteryTest.batteryLinkEstimated()), milliUnits(batteryTest.supervisoryBatteryCurrent()),
    // [Guía] Invoca `batteryTest.m1Status` con los argumentos de esta expresión.
    batteryTest.m1Status(), milliUnits(batteryTest.supervisoryPv()),
    // [Guía] Convierte una magnitud expresada en unidades físicas a un entero en milésimas para la telemetría
    // [Guía] serie.
    milliUnits(batteryTest.m1Error()), milliUnits(batteryTest.m1Integral()),
    // [Guía] Invoca `long` con los argumentos de esta expresión.
    long(Config::BUS_KP * 1000000.0f), long(Config::BUS_KI * 1000000.0f),
    // [Guía] Invoca `unsigned` con los argumentos de esta expresión.
    unsigned(batteryTest.m1Retries()), batteryTest.m1Limit(), milliUnits(batteryTest.m1Bus()),
    // [Guía] Invoca `batteryTest.batteryLinkStatus` con los argumentos de esta expresión.
    batteryTest.batteryLinkStatus(), milliUnits(batteryTest.batteryWindowMean()),
    // [Guía] Invoca `unsigned` con los argumentos de esta expresión.
    unsigned(batteryTest.batteryWindowReady()), batteryTest.batteryResponseStatus(),
    // [Guía] Invoca `batteryTest.lastSourceEvent` con los argumentos de esta expresión.
    batteryTest.lastSourceEvent(), milliUnits(batteryTest.q3Error()),
    // [Guía] Convierte una magnitud expresada en unidades físicas a un entero en milésimas para la telemetría
    // [Guía] serie.
    milliUnits(batteryTest.q3Integral()), milliUnits(Config::Q3_BUS_ACTION),
    // [Guía] Convierte una magnitud expresada en unidades físicas a un entero en milésimas para la telemetría
    // [Guía] serie.
    milliUnits(batteryTest.m1BatteryBaseline()), unsigned(batteryTest.m1BatteryBaselineReady()),
    // [Guía] Convierte una magnitud expresada en unidades físicas a un entero en milésimas para la telemetría
    // [Guía] serie.
    milliUnits(batteryTest.chargePvMinimum()), (unsigned long)(batteryTest.chargePrepElapsedUs() / 1000),
    // [Guía] Invoca `unsigned` con los argumentos de esta expresión.
    unsigned(batteryTest.chargeFlowConfirmed()), unsigned(batteryTest.chargeDriveQualified()),
    // [Guía] Convierte una magnitud expresada en unidades físicas a un entero en milésimas para la telemetría
    // [Guía] serie.
    milliUnits(batteryTest.chargeIdleCurrent()),
    // [Guía] Invoca `batteryTest.chargePrepStage` con los argumentos de esta expresión.
    batteryTest.chargePrepStage(), batteryTest.chargePrepLast(), unsigned(batteryTest.chargeAttempts()),
    // [Guía] Invoca `unsigned` con los argumentos de esta expresión.
    unsigned(batteryTest.chargePrepAttempts()),
    // [Guía] Convierte una magnitud expresada en unidades físicas a un entero en milésimas para la telemetría
    // [Guía] serie.
    milliUnits(batteryTest.supervisoryPv() - batteryTest.batteryControlVoltage()),
    // [Guía] Convierte una magnitud expresada en unidades físicas a un entero en milésimas para la telemetría
    // [Guía] serie.
    milliUnits(batteryTest.pvAssistTarget()),
    // [Guía] Completa la declaración o expresión con `(unsigned long)(batteryTest.m1BatteryConfirmElapsedUs() /
    // [Guía] 1000)`.
    (unsigned long)(batteryTest.m1BatteryConfirmElapsedUs() / 1000),
    // [Guía] Completa la declaración o expresión con `(unsigned long)(Config::M1_BAT_CONNECT_US / 1000)`.
    (unsigned long)(Config::M1_BAT_CONNECT_US / 1000),
    // [Guía] Completa la declaración o expresión con `(unsigned long)batteryTest.m1BatteryConfirmResets(),
    // [Guía] batteryTest.m1BatteryConfirmLastReset()`.
    (unsigned long)batteryTest.m1BatteryConfirmResets(), batteryTest.m1BatteryConfirmLastReset(),
    // [Guía] Invoca `unsigned` con los argumentos de esta expresión.
    unsigned(batteryTest.m2Path()), milliUnits(batteryTest.m2CurrentReference()),
    // [Guía] Convierte una magnitud expresada en unidades físicas a un entero en milésimas para la telemetría
    // [Guía] serie.
    milliUnits(batteryTest.m2BusCurrent()), milliUnits(batteryTest.m2DemandCurrent()),
    // [Guía] Invoca `unsigned` con los argumentos de esta expresión.
    unsigned(batteryTest.m2BusCurrentReady()),
    // [Guía] Invoca `batteryTest.m2Stage` con los argumentos de esta expresión.
    batteryTest.m2Stage(), (unsigned long)batteryTest.m2IdleMilliseconds(),
    // [Guía] Completa la declaración o expresión con `(unsigned long)(Config::M2_BUS_IDLE_US / 1000)`.
    (unsigned long)(Config::M2_BUS_IDLE_US / 1000),
    // [Guía] Completa la declaración o expresión con `(unsigned long)batteryTest.m2IdleResets(),
    // [Guía] batteryTest.m2Block()`.
    (unsigned long)batteryTest.m2IdleResets(), batteryTest.m2Block(),
    // [Guía] Completa la declaración o expresión con `(unsigned long)(Config::M2_BUS_CURRENT_WINDOW_US / 1000)`.
    (unsigned long)(Config::M2_BUS_CURRENT_WINDOW_US / 1000),
    // [Guía] Convierte una magnitud expresada en unidades físicas a un entero en milésimas para la telemetría
    // [Guía] serie.
    milliUnits(batteryTest.m2GuardCurrent()), unsigned(batteryTest.m2GuardReady()),
    // [Guía] Completa la declaración o expresión con `(unsigned long)(Config::M2_BUS_CURRENT_WINDOW_US *
    // [Guía] Config::M2_BUS_GUARD_WINDOWS / 1000)`.
    (unsigned long)(Config::M2_BUS_CURRENT_WINDOW_US * Config::M2_BUS_GUARD_WINDOWS / 1000),
    // [Guía] Aporta `mpptDecision, long((emsMppt ? batteryTest.mpptStep() : mppt.step()) * 1000000.0f` como parte
    // [Guía] de los parámetros de la operación.
    mpptDecision, long((emsMppt ? batteryTest.mpptStep() : mppt.step()) * 1000000.0f),
    // [Guía] Convierte una magnitud expresada en unidades físicas a un entero en milésimas para la telemetría
    // [Guía] serie.
    milliUnits(emsMppt ? batteryTest.mpptDeltaPower() : mppt.deltaPower()),
    // [Guía] Convierte una magnitud expresada en unidades físicas a un entero en milésimas para la telemetría
    // [Guía] serie.
    milliUnits(emsMppt ? batteryTest.mpptPower() : mppt.power()),
    // [Guía] Convierte una magnitud expresada en unidades físicas a un entero en milésimas para la telemetría
    // [Guía] serie.
    milliUnits(emsMppt ? batteryTest.mpptBasePower() : mppt.basePower()),
    // [Guía] Invoca `long` con los argumentos de esta expresión.
    long((emsMppt ? batteryTest.mpptTarget() : mppt.duty()) * 1000000.0f),
    // [Guía] Completa la declaración o expresión con `(unsigned long)(emsMppt ? batteryTest.mpptFlatSteps() :
    // [Guía] mppt.flatSteps())`.
    (unsigned long)(emsMppt ? batteryTest.mpptFlatSteps() : mppt.flatSteps()),
    // [Guía] Completa la declaración o expresión con `(unsigned long)(emsMppt ? batteryTest.mpptUpdates() :
    // [Guía] mppt.updates())`.
    (unsigned long)(emsMppt ? batteryTest.mpptUpdates() : mppt.updates()),
    // [Guía] Completa la declaración o expresión con `emsMppt && batteryTest.voltageMpptControl() ?
    // [Guía] "VPV_REFERENCE" : "DUTY"`.
    emsMppt && batteryTest.voltageMpptControl() ? "VPV_REFERENCE" : "DUTY",
    // [Guía] Convierte una magnitud expresada en unidades físicas a un entero en milésimas para la telemetría
    // [Guía] serie.
    milliUnits(emsMppt ? batteryTest.mpptReferenceV() : 0),
    // [Guía] Convierte una magnitud expresada en unidades físicas a un entero en milésimas para la telemetría
    // [Guía] serie.
    milliUnits(emsMppt ? batteryTest.mpptStepV() : 0),
    // [Guía] Invoca `unsigned` con los argumentos de esta expresión.
    unsigned(trip.valid), trip.reason, (unsigned long)trip.sample.nowUs, tripFinite,
    // [Guía] Invoca `tripMilli` con los argumentos de esta expresión.
    tripMilli(trip.sample.batteryIRaw), tripMilli(trip.sample.batteryI),
    // [Guía] Invoca `tripMilli` con los argumentos de esta expresión.
    tripMilli(trip.sample.busRaw), tripMilli(trip.sample.batteryV),
    // [Guía] Invoca `tripMilli` con los argumentos de esta expresión.
    tripMilli(trip.duty1), tripMilli(trip.duty2), tripMilli(trip.duty3),
    // [Guía] Invoca `unsigned` con los argumentos de esta expresión.
    unsigned(trip.sample.adcBatteryI), unsigned(trip.sample.adcBusV),
    // [Guía] Invoca `unsigned` con los argumentos de esta expresión.
    unsigned(trip.sample.adcBatteryV), tripMilli(trip.sample.zeroBatteryAdc),
    // [Guía] Invoca `tripMilli` con los argumentos de esta expresión.
    tripMilli(trip.sample.pvRaw), tripMilli(trip.sample.pvIRaw), tripMilli(trip.sample.pvI),
    // [Guía] Invoca `unsigned` con los argumentos de esta expresión.
    unsigned(Config::BAT_QUALIFY_PASSIVE_CONNECTION), batteryTest.connectionStatus(),
    // [Guía] Completa la declaración o expresión con `(unsigned long)connection.overSamples,
    // [Guía] tripMilli(connection.maximumRaw)`.
    (unsigned long)connection.overSamples, tripMilli(connection.maximumRaw),
    // [Guía] Completa la declaración o expresión con `(unsigned long)connection.first.nowUs,
    // [Guía] unsigned(connection.first.adcBatteryI))`.
    (unsigned long)connection.first.nowUs, unsigned(connection.first.adcBatteryI));
  // [Guía] Actualiza `txLength` con `length > 0 ? (size_t(length) < sizeof(txBuffer) ? size_t(length) :
  // [Guía] sizeof(txBuffer) - 1) : 0`.
  txLength = length > 0 ? (size_t(length) < sizeof(txBuffer) ? size_t(length) : sizeof(txBuffer) - 1) : 0;
  // [Guía] Actualiza `txPosition` con `0`.
  txPosition = 0;
  // [Guía] Actualiza `diagnosticSamples` con `diagnosticSlew = 0`.
  diagnosticSamples = diagnosticSlew = 0;
  // [Guía] Actualiza `diagnosticErrorSum` con `diagnosticTrackingSum = 0.0f`.
  diagnosticErrorSum = diagnosticTrackingSum = 0.0f;
  // [Guía] Actualiza `frames` con `0`.
  frames = 0;
  // [Guía] Reinicia `controlWindow` con el estado inicial dado en los argumentos.
  controlWindow.reset();
  // [Guía] Actualiza `reportStartUs` con `now`.
  reportStartUs = now;
  // [Guía] Controla las iteraciones mediante `uint8_t i = 0; i < 3; ++i)`.
  for (uint8_t i = 0; i < 3; ++i) {
    // [Guía] Actualiza `displayCurrentSum[i]` con `0`.
    displayCurrentSum[i] = 0;
    // [Guía] Actualiza `displayCurrentCount[i]` con `0`.
    displayCurrentCount[i] = 0;
    // [Guía] Actualiza `displayVoltageSum[i]` con `0`.
    displayVoltageSum[i] = 0;
  }
  // [Guía] Entrega `txLength > 0` al llamador.
  return txLength > 0;
}

// Separar lineas completas evita que ENABLE/ACK ejecuten letras de calibracion.
// Despachar solo una linea completa. Primero comandos especificos, luego el
// protocolo emulador y finalmente letras legacy: este orden evita que BK_* se
// confunda con B (calibrar bus). Los ACK no cuentan como nuevas ordenes humanas.
// [Guía] Interpreta una línea terminada del puerto. Despacha comandos de operador y confirmaciones del puente al
// [Guía] módulo correspondiente.
void processSerialLine() {
  // [Guía] Actualiza `rxLine[rxLength]` con `'\0'`.
  rxLine[rxLength] = '\0';
  // [Guía] Controla las iteraciones mediante `size_t i = 0; i < rxLength; ++i)`.
  for (size_t i = 0; i < rxLength; ++i) {
    // [Guía] Comprueba `rxLine[i] >= && rxLine[i] <=`. Si se cumple, ejecuta `'z') rxLine[i] -= 'a' - 'A'`.
    if (rxLine[i] >= 'a' && rxLine[i] <= 'z') rxLine[i] -= 'a' - 'A';
    // [Guía] Comprueba `rxLine[i] ==`. Si se cumple, ejecuta el bloque asociado.
    if (rxLine[i] == ',') {
      // Las comas del protocolo BK son separadores, no decimales.
      // [Guía] Comprueba `!strncmp(rxLine, , 2) || !strncmp(rxLine, , 4)`. Si se cumple, ejecuta `rxLine[i] =
      // [Guía] '.'`.
      if (!strncmp(rxLine, "R=", 2) || !strncmp(rxLine, "SOC=", 4)) rxLine[i] = '.';
    }
  }
  // [Guía] Comprueba `!strncmp(rxLine, , 4)`. Si se cumple, ejecuta el bloque asociado.
  if (!strncmp(rxLine, "SOC=", 4)) {
    // Una nueva semilla invalida la anterior; no cambia PWM ni el emulador.
    // [Guía] Actualiza `end` con `nullptr`.
    char* end = nullptr;
    // [Guía] Actualiza `percent` con `strtof(rxLine + 4, &end)`.
    const float percent = strtof(rxLine + 4, &end);
    // [Guía] Actualiza `socInputStatus` con `end != rxLine + 4 && *end == '\0' && batterySoc.seed(percent)`.
    socInputStatus = end != rxLine + 4 && *end == '\0' && batterySoc.seed(percent)
      // [Guía] Define una de las dos salidas de la expresión condicional.
      ? "ACCEPTED" : "REJECTED";
    // [Guía] Finaliza aquí la función y devuelve el control al llamador.
    return;
  }
  // [Guía] Comprueba `!strcmp(rxLine, )`. Si se cumple, ejecuta el bloque asociado.
  if (!strcmp(rxLine, "OV_RESET")) {
    // Solo si el bus estuvo bajo30V durante2s; informar el resultado del rearme.
    // [Guía] Actualiza `batteryCommandStatus` con `busOvervoltage.reset() ? "AUX_RESET_ACCEPTED" :
    // [Guía] "AUX_RESET_REJECTED"`.
    batteryCommandStatus = busOvervoltage.reset() ? "AUX_RESET_ACCEPTED" : "AUX_RESET_REJECTED";
    // [Guía] Finaliza aquí la función y devuelve el control al llamador.
    return;
  }
  // [Guía] Comprueba `!strcmp(rxLine, )`. Si se cumple, ejecuta el bloque asociado.
  if (!strcmp(rxLine, "EMS_RESET")) {
    // [Guía] Actualiza `batteryCommandStatus` con `batteryTest.resetFault() ? "RESET_STOPPED" :
    // [Guía] "RESET_REJECTED"`.
    batteryCommandStatus = batteryTest.resetFault() ? "RESET_STOPPED" : "RESET_REJECTED";
    // [Guía] Finaliza aquí la función y devuelve el control al llamador.
    return;
  }
  // [Guía] Comprueba `!strcmp(rxLine, )`. Si se cumple, ejecuta el bloque asociado.
  if (!strcmp(rxLine, "EMS_START")) {
    // [Guía] Comprueba `Config::BATTERY_TEST_PROFILE && !busOvervoltage.active() && !calibrating`. Si se cumple,
    // [Guía] ejecuta el bloque asociado.
    if (Config::BATTERY_TEST_PROFILE && !busOvervoltage.active() && !calibrating
        // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
        && !batteryTest.faulted() && !batteryTest.active()) {
      // [Guía] Prepara `batteryTest` para empezar desde las medidas o consignas indicadas.
      batteryTest.begin();
      // [Guía] Actualiza `batteryCommandStatus` con `"STARTED"`.
      batteryCommandStatus = "STARTED";
    // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
    } else batteryCommandStatus = "START_REJECTED";
    // [Guía] Finaliza aquí la función y devuelve el control al llamador.
    return;
  }
  // OFF tambien debe inhibir una ayuda aun en confirmacion/reposo, no solo ON.
  // [Guía] Comprueba `Config::BATTERY_TEST_PROFILE && (!strcmp(rxLine, )`. Si se cumple, ejecuta el bloque
  // [Guía] asociado.
  if (Config::BATTERY_TEST_PROFILE && (!strcmp(rxLine, "OFF")
      // [Guía] Añade una alternativa suficiente dentro de la condición compuesta.
      || !strcmp(rxLine, "DISABLE") || !strcmp(rxLine, "X")))
    // [Guía] Invoca `busOvervoltage.manualStop` con los argumentos de esta expresión.
    busOvervoltage.manualStop();
  // [Guía] Comprueba `busOvervoltage.active()`. Si se cumple, ejecuta el bloque asociado.
  if (busOvervoltage.active()) {
    // OFF/DISABLE/X siempre son posibles, pero inhiben la reactivacion remota.
    // [Guía] Comprueba `!strcmp(rxLine, ) || !strcmp(rxLine, ) || !strcmp(rxLine, )`. Si se cumple, ejecuta el
    // [Guía] bloque asociado.
    if (!strcmp(rxLine, "OFF") || !strcmp(rxLine, "DISABLE") || !strcmp(rxLine, "X"))
      // [Guía] Invoca `busOvervoltage.manualStop` con los argumentos de esta expresión.
      busOvervoltage.manualStop();
    // [Guía] Comprueba `strncmp(rxLine, , 3)`. Si se cumple, termina la función en esta rama.
    else if (strncmp(rxLine, "BK_", 3)) return;
  }
  // Esta busqueda es independiente de la futura EMS: no reutilizar A, que hoy
  // calibra bateria. Reinicio explicito solicita 4000 OFF antes de comenzar.
  // [Guía] Comprueba `!strcmp(rxLine, )`. Si se cumple, ejecuta el bloque asociado.
  if (!strcmp(rxLine, "AUTO_R")) {
    // [Guía] Comprueba `Config::BATTERY_TEST_PROFILE`. Si se cumple, ejecuta el bloque asociado.
    if (Config::BATTERY_TEST_PROFILE) { batteryCommandStatus = "FIXED_LOAD_PROFILE"; return; }
    // [Guía] Actualiza `emulatorBootPending` con `false`.
    emulatorBootPending = false;
    // [Guía] Comprueba `mode1Active && !calibrating`. Si se cumple, ejecuta `emulatorRamp.begin(emulador,
    // [Guía] millis())`.
    if (mode1Active && !calibrating) emulatorRamp.begin(emulador, millis());
    // [Guía] Finaliza aquí la función y devuelve el control al llamador.
    return;
  }
  // [Guía] Comprueba `Config::BATTERY_TEST_PROFILE && (!strcmp(rxLine, )`. Si se cumple, ejecuta el bloque
  // [Guía] asociado.
  if (Config::BATTERY_TEST_PROFILE && (!strcmp(rxLine, "ON")
      // [Guía] Añade una alternativa suficiente dentro de la condición compuesta.
      || !strcmp(rxLine, "S") || !strcmp(rxLine, "M"))) {
    // [Guía] Actualiza `batteryCommandStatus` con `"FIXED_LOAD_PROFILE"`.
    batteryCommandStatus = "FIXED_LOAD_PROFILE";
    // [Guía] Finaliza aquí la función y devuelve el control al llamador.
    return;
  }
  // [Guía] Comprueba `!strcmp(rxLine, )`. Si se cumple, ejecuta el bloque asociado.
  if (!strcmp(rxLine, "MANUAL")) {
    // [Guía] Actualiza `emulatorBootPending` con `false`.
    emulatorBootPending = false;
    // [Guía] Invoca `emulatorRamp.cancel` con los argumentos de esta expresión.
    emulatorRamp.cancel();
    // [Guía] Finaliza aquí la función y devuelve el control al llamador.
    return;
  }
  // Una orden humana tiene prioridad; los ACK/PING del puente no son manuales.
  // [Guía] Comprueba `!strncmp(rxLine, , 2) || !strcmp(rxLine, )`. Si se cumple, ejecuta el bloque asociado.
  if (!strncmp(rxLine, "R=", 2) || !strcmp(rxLine, "ON")
      // [Guía] Añade una alternativa suficiente dentro de la condición compuesta.
      || !strcmp(rxLine, "OFF") || !strcmp(rxLine, "DISABLE")
      // [Guía] Añade una alternativa suficiente dentro de la condición compuesta.
      || !strcmp(rxLine, "ENABLE")) {
    // [Guía] Actualiza `emulatorBootPending` con `false`.
    emulatorBootPending = false;
    // [Guía] Invoca `emulatorRamp.cancel` con los argumentos de esta expresión.
    emulatorRamp.cancel();
  }
  // [Guía] Comprueba `emulador.command(rxLine, millis())`. Si se cumple, termina la función en esta rama.
  if (emulador.command(rxLine, millis())) return;
  // [Guía] Comprueba `rxLength == 1`. Si se cumple, ejecuta el bloque asociado.
  if (rxLength == 1) {
    // [Guía] Actualiza `command` con `rxLine[0]`.
    const int command = rxLine[0];
    // Enviar z SOLO cuando las tres corrientes sean cero.
    // [Guía] Comprueba `command == || command ==`. Si se cumple, ejecuta `'Z') startZeroCalibration(0x07)`.
    if (command == 'z' || command == 'Z') startZeroCalibration(0x07);
    // p calibra SOLO PV: las corrientes de A1/A2 no intervienen.
    // [Guía] Comprueba `command == || command ==`. Si se cumple, ejecuta `'P') startZeroCalibration(0x01)`.
    if (command == 'p' || command == 'P') startZeroCalibration(0x01);
    // [Guía] Comprueba `command == || command ==`. Si se cumple, ejecuta `'B') startZeroCalibration(0x02)`.
    if (command == 'b' || command == 'B') startZeroCalibration(0x02);
    // [Guía] Comprueba `command == || command ==`. Si se cumple, ejecuta `'A') startZeroCalibration(0x04)`.
    if (command == 'a' || command == 'A') startZeroCalibration(0x04);
    // [Guía] Comprueba `command == || command ==`. Si se cumple, ejecuta `'S') startMode1()`.
    if (command == 's' || command == 'S') startMode1();
    // [Guía] Comprueba `command == || command ==`. Si se cumple, ejecuta `'M') startMPPT()`.
    if (command == 'm' || command == 'M') startMPPT();
    // [Guía] Comprueba `command == || command ==`. Si se cumple, ejecuta `'X') stopMode1("MANUAL")`.
    if (command == 'x' || command == 'X') stopMode1("MANUAL");
  }
}

// Politica unica del emulador para EMS. Usar modo/duty ACTUALES, despues del
// control, y volver a comprobar antes de transmitir para que ON manual no
// pueda saltarse el permiso. OFF fisico depende del enlace y del ACK del PC.
// [Guía] Entrega la tensión, el estado operativo y los permisos a la política de carga auxiliar adecuada para el
// [Guía] perfil.
void serviceAuxiliaryLoad() {
  // [Guía] Comprueba `!Config::BATTERY_TEST_PROFILE`. Si se cumple, termina la función en esta rama.
  if (!Config::BATTERY_TEST_PROFILE) return;
  // [Guía] Comprueba `batteryTest.m2Path()`. Si se cumple, ejecuta el bloque asociado.
  if (batteryTest.m2Path()) {
    // [Guía] Invoca `busOvervoltage.stopForM2` con los argumentos de esta expresión.
    busOvervoltage.stopForM2(emulador, millis()); return;
  }
  // [Guía] Actualiza `fresh` con `!calibrating`.
  const bool fresh = !calibrating
    // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen instantánea de
    // [Guía] medidas y su validez.
    && uint32_t(micros() - measurements.timestampUs) < Config::CONTROL_TIMEOUT_US;
  // Supervisores ya existentes para la carga remota lenta: no cortar su rampa
  // por cada valle ADC. Las protecciones PWM conservan sus medidas rapidas/raw.
  // [Guía] Invoca `busOvervoltage.updateEmsAssist` con los argumentos de esta expresión.
  busOvervoltage.updateEmsAssist(emulador, batteryTest.supervisoryBus(),
    // [Guía] Invoca `batteryTest.supervisoryPv` con los argumentos de esta expresión.
    batteryTest.supervisoryPv(), millis(), fresh && batteryTest.m1AuxiliaryMode(),
    // [Guía] Completa la declaración o expresión con `fresh && batteryTest.m3AuxiliaryMode(),
    // [Guía] batteryTest.duty2()`.
    fresh && batteryTest.m3AuxiliaryMode(), batteryTest.duty2(),
    // [Guía] Completa la declaración o expresión con `fresh && batteryTest.active() &&
    // [Guía] measurements.currentValid[0] && measurements.currentValid[1]`. Aquí intervienen instantánea de
    // [Guía] medidas y su validez.
    fresh && batteryTest.active() && measurements.currentValid[0] && measurements.currentValid[1]
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen instantánea de
      // [Guía] medidas y su validez.
      && fabsf(measurements.currentFiltered[1]) < Config::EMULATOR_CURRENT_MAX_A,
    // [Guía] Invoca `batteryTest.m1BusPiRegulating` con los argumentos de esta expresión.
    batteryTest.m1BusPiRegulating(), true);
}

// RX siempre tiene una oportunidad acotada tras las protecciones, aunque no
// quede el margen150us requerido por TX. Evitar inanicion de ACK bajo carga.
// [Guía] Consume bytes dentro de la cuota permitida y reúne caracteres hasta obtener un comando completo.
void serviceSerialReceive() {
  // [Guía] Con el enlace disponible, ejecuta el receptor acotado. Una lambda entrega el reloj en µs y la otra
  // [Guía] consume cada carácter sin esperar una línea completa.
  if (Serial) serviceSerialRxBounded(Serial, []() { return micros(); }, [](char c) {
    // [Guía] Comprueba `c ==`. Si se cumple, termina la función en esta rama.
    if (c == '\r') return;
    // [Guía] Comprueba `c ==`. Si se cumple, ejecuta el bloque asociado.
    if (c == '\n') {
      // [Guía] Comprueba `!rxOverflow && rxLength`. Si se cumple, ejecuta `processSerialLine()`.
      if (!rxOverflow && rxLength) processSerialLine();
      // [Guía] Actualiza `rxLength` con `0`.
      rxLength = 0;
      // [Guía] Actualiza `rxOverflow` con `false`.
      rxOverflow = false;
    // [Guía] Comprueba `!rxOverflow`. Si se cumple, ejecuta el bloque asociado.
    } else if (!rxOverflow) {
      // [Guía] Comprueba `rxLength < sizeof(rxLine) - 1`. Si se cumple, ejecuta `rxLine[rxLength++] = c`.
      if (rxLength < sizeof(rxLine) - 1) rxLine[rxLength++] = c;
      // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
      else rxOverflow = true;
    }
  });
  // Procesar primero los ACK recibidos antes de evaluar su vencimiento.
  // [Guía] Invoca `emulador.tick` con los argumentos de esta expresión.
  emulador.tick(millis());
}

// V54: atender TX despues del ADC/PI incluso si no quedan150us. La cuota
// pequena permite drenar telemetria y BK_REQ sin esperar una ventana que
// puede no llegar con control activo. Solo el formateo grande prefiere margen.
// [Guía] Coordina recepción, solicitudes del emulador y transmisión de reportes sin esperar el vaciado del
// [Guía] puerto.
void serviceSerial() {
  // [Guía] Comprueba `!Serial`. Si se cumple, termina la función en esta rama. Aquí intervienen enlace USB serie
  // [Guía] con el computador.
  if (!Serial) return;
  // [Guía] Comprueba `txPosition == txLength`. Si se cumple, ejecuta el bloque asociado.
  if (txPosition == txLength) {
    // Ordenes de carga tienen prioridad al liberar el buffer; no sustituir
    // una trama parcialmente transmitida, que dejaria una linea corrupta.
    // [Guía] Actualiza `txLength` con `emulador.prepareRequest(txBuffer, sizeof(txBuffer), millis()`.
    txLength = emulador.prepareRequest(txBuffer, sizeof(txBuffer), millis(),
      // [Guía] Aporta `Config::BATTERY_TEST_PROFILE` como parte de los parámetros de la operación.
      Config::BATTERY_TEST_PROFILE); // Tambien ACK R/ON/OFF en M3 y retirada.
    // [Guía] Actualiza `txPosition` con `0`.
    txPosition = 0;
    // [Guía] Comprueba `!txLength && uint32_t(millis() - liveLastMs) >= Config::LIVE_TELEMETRY_MS`. Si se cumple,
    // [Guía] ejecuta el bloque asociado.
    if (!txLength && uint32_t(millis() - liveLastMs) >= Config::LIVE_TELEMETRY_MS
        // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
        && prepareLiveTelemetry()) liveLastMs = millis();
  }
  // [Guía] Actualiza `nowMs` con `millis()`.
  const uint32_t nowMs = millis();
  // [Guía] Actualiza `hasMargin` con `int32_t(nextSampleUs - micros()) > 150`.
  const bool hasMargin = int32_t(nextSampleUs - micros()) > 150;
  // Intentar cada250ms cuando hay margen; tras1000ms permitir una fotografia
  // aun con poco margen. No hay garantia de entrega si USB esta bloqueado.
  // Nunca perdonar la deuda por un intento fallido o por un BK_REQ pendiente.
  // [Guía] Comprueba `serialReportDue(nowMs, lastReportMs, Config::PRINT_MS,`. Si se cumple, ejecuta el bloque
  // [Guía] asociado.
  if (serialReportDue(nowMs, lastReportMs, Config::PRINT_MS,
                      // [Guía] Aporta `Config::SERIAL_REPORT_MAX_INTERVAL_MS, hasMargin` como parte de los
                      // [Guía] parámetros de la operación.
                      Config::SERIAL_REPORT_MAX_INTERVAL_MS, hasMargin)
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
      && prepareReport()) lastReportMs = nowMs;
  // [Guía] Invoca `serviceSerialTxBounded` con los argumentos de esta expresión. Aquí intervienen enlace USB
  // [Guía] serie con el computador.
  serviceSerialTxBounded(Serial, txBuffer, txLength, txPosition, hasMargin ? 32 : 16);
}

// Arduino ejecuta setup una vez por reset: salidas bajas, ADC14bits, semillaSOC,
// calibracion no bloqueante y armado automatico de la EMS. No se espera al PC
// para ejecutar loop; el enlace remoto es un servicio separado.
// [Guía] Configura la placa y establece un estado de arranque conocido con las salidas apagadas antes de comenzar
// [Guía] a adquirir.
void setup() {
  // [Guía] Termina el perfil independiente, baja Q1 y registra la causa para distinguir un paro del cambio de
  // [Guía] estado de la EMS.
  stopMode1("BOOT");
  // [Guía] Prepara enlace USB serie con el computador para empezar desde las medidas o consignas indicadas. Aquí
  // [Guía] intervienen enlace USB serie con el computador.
  Serial.begin(115200);
  // [Guía] Invoca `analogReadResolution` con los argumentos de esta expresión.
  analogReadResolution(Config::ADC_BITS);
  // [Guía] Comprueba `Config::SOC_INITIAL_PERCENT >= 0 && batterySoc.seed(Config::SOC_INITIAL_PERCENT)`. Si se
  // [Guía] cumple, ejecuta el bloque asociado. Aquí intervienen estimador por conteo de carga.
  if (Config::SOC_INITIAL_PERCENT >= 0 && batterySoc.seed(Config::SOC_INITIAL_PERCENT))
    // [Guía] Actualiza `socInputStatus` con `"CONFIG"`.
    socInputStatus = "CONFIG";
  // Requiere corriente cero en los tres sensores durante el arranque.
  // Las muestras se acumulan en loop(), sin bloquear la adquisicion.
  // [Guía] Prepara la estimación de offset de los canales de corriente que señala la máscara de bits.
  startZeroCalibration(0x07);
  // [Guía] Actualiza `autoStartPending` con `!Config::BATTERY_TEST_PROFILE &&
  // [Guía] Config::AUTO_START_AFTER_BOOT_CALIBRATION`.
  autoStartPending = !Config::BATTERY_TEST_PROFILE && Config::AUTO_START_AFTER_BOOT_CALIBRATION;
  // [Guía] Actualiza `emulatorBootPending` con `!Config::BATTERY_TEST_PROFILE &&
  // [Guía] Config::AUTO_EMULATOR_AFTER_BOOT`.
  emulatorBootPending = !Config::BATTERY_TEST_PROFILE && Config::AUTO_EMULATOR_AFTER_BOOT;
  // [Guía] Actualiza `emulatorBootState` con `"WAIT_CAL"`.
  emulatorBootState = "WAIT_CAL";
  // [Guía] Actualiza `emulatorBootStable` con `false`.
  emulatorBootStable = false;
  // [Guía] Actualiza `nextSampleUs` con `micros()`. Este dato representa fecha límite del próximo barrido en µs.
  nextSampleUs = micros();
  // [Guía] Actualiza `reportStartUs` con `nextSampleUs`.
  reportStartUs = nextSampleUs;
  // [Guía] Actualiza `lastReportMs` con `millis()`.
  lastReportMs = millis();
  // [Guía] Actualiza `liveLastMs` con `lastReportMs`.
  liveLastMs = lastReportMs;
  // [Guía] Comprueba `Config::BATTERY_TEST_PROFILE`. Si se cumple, ejecuta `batteryTest.begin()`.
  if (Config::BATTERY_TEST_PROFILE) batteryTest.begin();
}

// Armar una sola vez, cuando M1 ya regula. No rearmar fallos de la busqueda
// ni ordenes manuales. La espera del PC o de estabilidad no tiene vencimiento.
// Automatismo legacy, no habilitado en perfil EMS. Todos los permisos deben
// mantenerse durante la confirmacion; un fallo reinicia la ventana estable.
// [Guía] Avanza la puesta en servicio de la B&K por estados y confirmaciones, sin bloquear la adquisición.
void serviceEmulatorBoot() {
  // [Guía] Comprueba `!emulatorBootPending`. Si se cumple, termina la función en esta rama.
  if (!emulatorBootPending) return;
  // [Guía] Actualiza `waiting` con `nullptr`.
  const char* waiting = nullptr;
  // [Guía] Comprueba `!Config::AUTO_EMULATOR_HARDWARE_READY`. Si se cumple, ejecuta `waiting =
  // [Guía] "HARDWARE_BLOCKED"`.
  if (!Config::AUTO_EMULATOR_HARDWARE_READY) waiting = "HARDWARE_BLOCKED";
  // [Guía] Comprueba `calibrating`. Si se cumple, ejecuta `waiting = "WAIT_CAL"`.
  else if (calibrating) waiting = "WAIT_CAL";
  // [Guía] Comprueba `!mode1Active`. Si se cumple, ejecuta `waiting = "WAIT_M1"`.
  else if (!mode1Active) waiting = "WAIT_M1";
  // [Guía] Comprueba `softStarting`. Si se cumple, ejecuta `waiting = "WAIT_SOFT_START"`.
  else if (softStarting) waiting = "WAIT_SOFT_START";
  // [Guía] Comprueba `!measurements.currentValid[0] || !measurements.currentValid[1]`. Si se cumple, ejecuta el
  // [Guía] bloque asociado. Aquí intervienen instantánea de medidas y su validez.
  else if (!measurements.currentValid[0] || !measurements.currentValid[1])
    // [Guía] Actualiza `waiting` con `"WAIT_CURRENT_VALID"`.
    waiting = "WAIT_CURRENT_VALID";
  // [Guía] Comprueba `!controlUpdates || uint32_t(micros() - lastSuccessfulControlUs)`. Si se cumple, ejecuta el
  // [Guía] bloque asociado.
  else if (!controlUpdates || uint32_t(micros() - lastSuccessfulControlUs)
           // [Guía] Actualiza `>` con `Config::CONTROL_TIMEOUT_US) waiting = "WAIT_PI"`.
           >= Config::CONTROL_TIMEOUT_US) waiting = "WAIT_PI";
  // [Guía] Comprueba `!emulador.enlace`. Si se cumple, ejecuta `waiting = "WAIT_PC"`.
  else if (!emulador.enlace) waiting = "WAIT_PC";
  // [Guía] Comprueba `!emulador.habilitarEmulador`. Si se cumple, ejecuta `waiting = "WAIT_PERMISSION"`.
  else if (!emulador.habilitarEmulador) waiting = "WAIT_PERMISSION";
  // [Guía] Comprueba `!isfinite(measurements.voltageFiltered[1])`. Si se cumple, ejecuta el bloque asociado. Aquí
  // [Guía] intervienen instantánea de medidas y su validez.
  else if (!isfinite(measurements.voltageFiltered[1])
           // [Guía] Añade una alternativa suficiente dentro de la condición compuesta. Aquí intervienen
           // [Guía] instantánea de medidas y su validez.
           || fabsf(measurements.voltageFiltered[1] - Config::BUS_REFERENCE_V)
              // [Guía] Completa la declaración o expresión con `> Config::EMULATOR_STABLE_BAND_V) waiting =
              // [Guía] "WAIT_BUS_STABLE"`.
              > Config::EMULATOR_STABLE_BAND_V) waiting = "WAIT_BUS_STABLE";
  // [Guía] Comprueba `waiting`. Si se cumple, ejecuta el bloque asociado.
  if (waiting) {
    // [Guía] Actualiza `emulatorBootStable` con `false`.
    emulatorBootStable = false;
    // [Guía] Actualiza `emulatorBootState` con `waiting`.
    emulatorBootState = waiting;
    // [Guía] Finaliza aquí la función y devuelve el control al llamador.
    return;
  }
  // [Guía] Actualiza `now` con `millis()`.
  const uint32_t now = millis();
  // [Guía] Comprueba `!emulatorBootStable`. Si se cumple, ejecuta el bloque asociado.
  if (!emulatorBootStable) {
    // [Guía] Actualiza `emulatorBootStable` con `true`.
    emulatorBootStable = true;
    // [Guía] Actualiza `emulatorBootStableSince` con `now`.
    emulatorBootStableSince = now;
  }
  // [Guía] Actualiza `emulatorBootState` con `"CONFIRM_BUS_STABLE"`.
  emulatorBootState = "CONFIRM_BUS_STABLE";
  // [Guía] Comprueba `uint32_t(now - emulatorBootStableSince) < Config::EMULATOR_STABLE_MS`. Si se cumple,
  // [Guía] termina la función en esta rama.
  if (uint32_t(now - emulatorBootStableSince) < Config::EMULATOR_STABLE_MS) return;
  // [Guía] Actualiza `emulatorBootPending` con `false`.
  emulatorBootPending = false;
  // [Guía] Actualiza `emulatorBootStable` con `false`.
  emulatorBootStable = false;
  // [Guía] Prepara `emulatorRamp` para empezar desde las medidas o consignas indicadas.
  emulatorRamp.begin(emulador, now);
}

// Planificador cooperativo. Cada vuelta hace trabajo acotado; no hay delay de
// control. Conservar fase y contar perdidas distingue una CPU atrasada de una
// menor frecuencia elegida. La GUI solo consume reportes de este planificador.
// [Guía] Ejecuta los barridos ADC según micros(), actualiza SOC y control, contabiliza periodos vencidos y
// [Guía] aprovecha el tiempo restante para comunicaciones.
void loop() {
  // [Guía] Lee el reloj de la placa en microsegundos. El instante sirve para comparar vencimientos y conservar la
  // [Guía] fase del barrido.
  const uint32_t now = micros();
  // [Guía] Comprueba que el siguiente barrido ya venció. La resta de los tiempos y su interpretación con signo
  // [Guía] permiten comparar fechas cercanas aunque se desborde el contador.
  if (int32_t(now - nextSampleUs) >= 0) {
    // [Guía] Calcula cuántos microsegundos se retrasó este barrido respecto a su instante programado.
    const uint32_t lateness = now - nextSampleUs;
    // [Guía] Conserva el peor retraso detectado para que la telemetría permita evaluar la regularidad de la
    // [Guía] adquisición.
    if (lateness > maxLatenessUs) maxLatenessUs = lateness;
    // [Guía] Divide el retraso por el periodo nominal para contar cuántos barridos completos se perdieron.
    const uint32_t skipped = lateness / Config::SAMPLE_US;
    // [Guía] Acumula los barridos omitidos. El contador ayuda a distinguir una muestra demorada de una pérdida
    // [Guía] sostenida.
    missed += skipped;
    // Conservar la fase del calendario sin inventar muestras de periodos perdidos.
    // [Guía] Mueve la fecha límite al próximo instante del calendario original, incluso cuando se perdió más de
    // [Guía] un periodo.
    nextSampleUs += (skipped + 1) * Config::SAMPLE_US;
    // [Guía] Lee las entradas ADC, calibra las seis magnitudes y conserva muestras instantáneas y filtradas.
    acquireMeasurements();
    // Cuenta la corriente incluso con PWM detenido: puede existir flujo pasivo.
    // El interruptor debe permanecer abierto durante la calibracion de Ibat.
    // [Guía] Entrega al estimador la corriente instantánea del canal de batería. Los argumentos siguientes
    // [Guía] determinan su validez y la fecha de la muestra.
    batterySoc.update(measurements.currentRaw[2], measurements.currentValid[2]
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen instantánea de
      // [Guía] medidas y su validez.
      && measurements.adcCurrent[2] > 0 && measurements.adcCurrent[2] < Config::ADC_MAX,
      // [Guía] Completa la declaración o expresión con `measurements.timestampUs)`. Aquí intervienen instantánea
      // [Guía] de medidas y su validez.
      measurements.timestampUs);
    // [Guía] Comprueba `Config::BATTERY_TEST_PROFILE`. Si se cumple, ejecuta el bloque asociado.
    if (Config::BATTERY_TEST_PROFILE) {
      // [Guía] Construye la instantánea de entradas de la EMS con medidas, permisos y estado del emulador.
      updateBatteryTest();
    // [Guía] Comprueba `autoStartPending && !calibrating`. Si se cumple, ejecuta el bloque asociado.
    } else if (autoStartPending && !calibrating) {
      // [Guía] Actualiza `autoStartPending` con `false`.
      autoStartPending = false;
      // [Guía] Prepara el arranque del perfil independiente de regulación del bus mediante Q1.
      startMode1();
    // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
    } else {
      // [Guía] Comprueba `mpptActive`. Si se cumple, ejecuta `updateMPPT()`.
      if (mpptActive) updateMPPT();
      // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
      else updateMode1();
    }
    // [Guía] Comprueba `Config::BATTERY_TEST_PROFILE`. Si se cumple, ejecuta el bloque asociado.
    if (Config::BATTERY_TEST_PROFILE) {
      // V48: no heredar el enclavamiento de carga desde M4 ni desde M1.
      // [Guía] Entrega la tensión, el estado operativo y los permisos a la política de carga auxiliar adecuada
      // [Guía] para el perfil.
      serviceAuxiliaryLoad();
      // [Guía] Actualiza `autoStartPending` con `emulatorBootPending = false`.
      autoStartPending = emulatorBootPending = false;
      // [Guía] Invoca `emulatorRamp.cancel` con los argumentos de esta expresión.
      emulatorRamp.cancel("EMS_AUX_POLICY");
    // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
    } else {
      // [Guía] Entrega una nueva muestra o ciclo de trabajo a `busOvervoltage`. Aquí intervienen instantánea de
      // [Guía] medidas y su validez.
      busOvervoltage.update(emulador, measurements.voltageRaw[1], millis());
      // [Guía] Comprueba `busOvervoltage.active()`. Si se cumple, ejecuta el bloque asociado.
      if (busOvervoltage.active()) {
        // [Guía] Actualiza `autoStartPending` con `emulatorBootPending = false`.
        autoStartPending = emulatorBootPending = false;
        // [Guía] Invoca `emulatorRamp.cancel` con los argumentos de esta expresión.
        emulatorRamp.cancel("BUS_RECOVERY_LOAD");
      }
    }
    // Usar cada muestra para no declarar estable una media que oculta picos.
    // Sin corriente valida no aumentar automaticamente el consumo.
    // [Guía] Avanza la puesta en servicio de la B&K por estados y confirmaciones, sin bloquear la adquisición.
    serviceEmulatorBoot();
    // [Guía] Comprueba `mode1Active && !softStarting`. Si se cumple, ejecuta `emulatorRamp.update(emulador,
    // [Guía] millis(), mode1Active,`.
    if (mode1Active && !softStarting) emulatorRamp.update(emulador, millis(), mode1Active,
      // [Guía] Completa la declaración o expresión con `!calibrating && !softStarting &&
      // [Guía] measurements.currentValid[0]`. Aquí intervienen instantánea de medidas y su validez.
      !calibrating && !softStarting && measurements.currentValid[0]
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen instantánea de
      // [Guía] medidas y su validez.
      && measurements.currentValid[1]
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
      && uint32_t(micros() - lastSuccessfulControlUs) < Config::CONTROL_TIMEOUT_US,
      // [Guía] Completa la declaración o expresión con `measurements.voltageFiltered[1],
      // [Guía] measurements.voltageFiltered[0]`. Aquí intervienen instantánea de medidas y su validez.
      measurements.voltageFiltered[1], measurements.voltageFiltered[0],
      // [Guía] Completa la declaración o expresión con `measurements.currentFiltered[0],
      // [Guía] measurements.currentFiltered[1])`. Aquí intervienen instantánea de medidas y su validez.
      measurements.currentFiltered[0], measurements.currentFiltered[1]);
    // Solo resumen de medidas/orden aplicada. Sin delay, sin cambiar PWM.
    // [Guía] Incorpora la muestra a la ventana de `controlWindow`. Aquí intervienen instantánea de medidas y su
    // [Guía] validez.
    controlWindow.observe(measurements.voltageFiltered[0], measurements.voltageFiltered[1], duty1,
      // [Guía] Completa la declaración o expresión con `measurements.voltageRaw[0], measurements.voltageRaw[1]`.
      // [Guía] Aquí intervienen instantánea de medidas y su validez.
      measurements.voltageRaw[0], measurements.voltageRaw[1],
      // [Guía] Completa la declaración o expresión con `measurements.currentRaw[0], measurements.currentRaw[1]`.
      // [Guía] Aquí intervienen instantánea de medidas y su validez.
      measurements.currentRaw[0], measurements.currentRaw[1],
      // [Guía] Completa la declaración o expresión con `measurements.currentValid[0],
      // [Guía] measurements.currentValid[1])`. Aquí intervienen instantánea de medidas y su validez.
      measurements.currentValid[0], measurements.currentValid[1]);
  }
  // [Guía] Consume bytes dentro de la cuota permitida y reúne caracteres hasta obtener un comando completo.
  serviceSerialReceive();
  // RX y TX tienen cuotas propias; ninguno depende de que sobren150us.
  // Medir lost/PI_lost en banco: las cuotas no equivalen a tiempo real duro.
  // [Guía] Coordina recepción, solicitudes del emulador y transmisión de reportes sin esperar el vaciado del
  // [Guía] puerto.
  serviceSerial();
}
