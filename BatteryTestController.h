// [Guía] Decide el estado de la EMS y el reparto de Q1, Q2 y Q3. Usa evidencia de fuentes, SOC, validez de las
// [Guía] medidas, protección y transferencias internas. Devuelve órdenes PWM al sketch.
// [Guía] Evita procesar este encabezado dos veces en una misma unidad de compilación.
#pragma once
 /* Es una maquina de estados sin acceso a hardware. Input contiene medidas,
 * permisos y tiempo; duty1/2/3 devuelven ordenes; el .ino aplica PwmOut y
 * devuelve un fallo si no pudo escribir. Ordenado no equivale a medido.
 *
 * Flujo de update, en este orden:
 * 1) descartar huecos/calibracion y construir supervisores de medidas;
 * 2) inferir presencia de fuentes y respuesta de bateria;
 * 3) comprobar protecciones a frecuencia ADC, incluso antes del paso PI;
 * 4) seleccionar arranque en WAIT y validar permisos de operacion;
 * 5) atender el calendario CONTROL_US sin inventar pasos perdidos;
 * 6) ejecutar una rama del switch y verificar de nuevo propuestas/interlock.
 *
 * M1: Q1 PI del bus; M5: Q3 PI del bus; M4: Q1 MPPT y Q3 PI del bus;
 * M3: Q1 MPPT y Q2 PI del bus. M2: SOLO Q2, PI de Ibat=-1.2A, sin PI de bus.
 * V59: Q1 sigue Vpv_ref con un PI interno; en M3 P&O cambia esa referencia.
 * En la preparacion M4 desde M1 se mantiene18.4V y Q3 solo apoya bus bajo.
 * No imponer18.4V fija una vez que el MPPT empieza a explorar en M3.
 * START/RETIRE/INTERLOCK son etapas internas, no otros modos energeticos.
 * El margen PV>=Vbat+2 se exige para ENTRAR en M3. V39 no mantiene un lazo
 * de tension Vpv en M3: Q2 y cambios de carga pueden alterar ese punto.
 *
 * Todas las tensiones/corrientes: V/A; SOC:0..100; duty:0..1; nowUs/tiempos:
 * microsegundos. Ibat positiva=descarga, negativa=carga. 'Link estimated'
 * es evidencia de corriente, nunca lectura del contacto del interruptor.
 * Los mensajes reason_ explican el requisito pendiente o ultimo evento;
 * muchos NO son fallos. Solo state_==FAULT enclava un disparo.
 */
// [Guía] Incorpora las declaraciones de "Config.h" antes de usar sus tipos y funciones.
#include "Config.h"
// [Guía] Incorpora las declaraciones de "PIController.h" antes de usar sus tipos y funciones.
#include "PIController.h"
// [Guía] Incorpora las declaraciones de "MPPTController.h" antes de usar sus tipos y funciones.
#include "MPPTController.h"
// [Guía] Incorpora las declaraciones de "PvVoltageMpptController.h" antes de usar sus tipos y funciones.
#include "PvVoltageMpptController.h"
// [Guía] Incorpora las declaraciones de "M2CurrentController.h" antes de usar sus tipos y funciones.
#include "M2CurrentController.h"
// [Guía] Incorpora las declaraciones de <string.h> antes de usar sus tipos y funciones.
#include <string.h>

// Logica del ensayo sin acceso a pines: el sketch aplica y confirma sus salidas.
// M5: bateria; M4: PV+bateria; M3: PV con carga de bateria; M1: PV sin carga bat.
// [Guía] Reúne bajo `BatteryTestController` las declaraciones que comparten este tipo o ámbito.
class BatteryTestController {
// [Guía] Desde aquí se declaran los tipos y operaciones que usa el llamador.
public:
  // Copia de parametros inyectable en pruebas. No modifica Config global ni
  // recalibra sensores: permite usar un PI conocido en escenarios sinteticos.
  // [Guía] Reúne bajo `Settings` las declaraciones que comparten este tipo o ámbito.
  struct Settings {
    // [Guía] Actualiza `m2Enabled` con `Config::M2_ENABLED`.
    bool m2Enabled = Config::M2_ENABLED;
    // [Guía] Actualiza `m1BranchAcquireEnabled` con `Config::M1_BRANCH_ACQUIRE_ENABLED`.
    bool m1BranchAcquireEnabled = Config::M1_BRANCH_ACQUIRE_ENABLED;
    // [Guía] Actualiza `m1BranchConfirmUs` con `Config::M1_FAST_BRANCH_CONFIRM_US`.
    uint32_t m1BranchConfirmUs = Config::M1_FAST_BRANCH_CONFIRM_US;
    // [Guía] Actualiza `hotplugPvSeekEnabled` con `Config::HOTPLUG_PV_SEEK_ENABLED`.
    bool hotplugPvSeekEnabled = Config::HOTPLUG_PV_SEEK_ENABLED;
    // [Guía] Actualiza `m3MpptContextRebaseEnabled` con `Config::M3_MPPT_CONTEXT_REBASE_ENABLED`.
    bool m3MpptContextRebaseEnabled = Config::M3_MPPT_CONTEXT_REBASE_ENABLED;
    // [Guía] Actualiza `pvVoltageMpptEnabled` con `Config::PV_VOLTAGE_MPPT_ENABLED`.
    bool pvVoltageMpptEnabled = Config::PV_VOLTAGE_MPPT_ENABLED;
    // [Guía] Actualiza `m1AuxiliaryCoordination` con `true`.
    bool m1AuxiliaryCoordination = true;
    // [Guía] Actualiza `m1BranchNoiseTolerant` con `true`.
    bool m1BranchNoiseTolerant = true;
    // [Guía] Actualiza `kp3` con `Config::Q3_BUS_KP, ki3 = Config::Q3_BUS_KI`.
    float kp3 = Config::Q3_BUS_KP, ki3 = Config::Q3_BUS_KI;
    // [Guía] Actualiza `action3` con `Config::Q3_BUS_ACTION`.
    float action3 = Config::Q3_BUS_ACTION;
    // [Guía] Actualiza `kp2` con `Config::Q2_BUS_KP, ki2 = Config::Q2_BUS_KI`.
    float kp2 = Config::Q2_BUS_KP, ki2 = Config::Q2_BUS_KI;
    // [Guía] Actualiza `action2` con `Config::Q2_BUS_ACTION`.
    float action2 = Config::Q2_BUS_ACTION;
    // Ensayo M1->M4: aproximar Vpv antes de entregar de nuevo Q1 a P&O.
    // [Guía] Actualiza `pvAssistEnabled` con `true`.
    bool pvAssistEnabled = true;
    // [Guía] Actualiza `pvAssistSlewPerSecond` con `0.10f`.
    float pvAssistSlewPerSecond = 0.10f;
    // [Guía] Actualiza `pvAssistMaxDutyDrop` con `0.25f`.
    float pvAssistMaxDutyDrop = 0.25f;
    // [Guía] Actualiza `pvAssistTimeoutUs` con `8000000`.
    uint32_t pvAssistTimeoutUs = 8000000;
    // Margen confirmado antes de Q2; en M3 la supervision es energetica.
    // [Guía] Actualiza `strictChargeMargin` con `true`.
    bool strictChargeMargin = true;
  };
  // Una instantanea por barrido. Raw conserva conversion ADC antes de IIR;
  // los campos sin Raw ya tienen el filtrado rapido del sketch. Cada Valid
  // debe expresar tambien calibracion/rango, no solo que el numero sea finito.
  // [Guía] Reúne bajo `Input` las declaraciones que comparten este tipo o ámbito.
  struct Input {
    // [Guía] Actualiza `nowUs` con `0`.
    uint32_t nowUs = 0;
    // [Guía] Actualiza `calibrating` con `true, batteryValid = false, busValid = false, pvValid = false`.
    bool calibrating = true, batteryValid = false, busValid = false, pvValid = false;
    // [Guía] Actualiza `busCurrentValid` con `false, socValid = false`.
    bool busCurrentValid = false, socValid = false;
    // [Guía] Actualiza `q3Configured` con `false, q2Configured = false, pvConfigured = false`.
    bool q3Configured = false, q2Configured = false, pvConfigured = false;
    // [Guía] Actualiza `recoveryLoadConfirmed` con `false`.
    bool recoveryLoadConfirmed = false; // ACK de R/ON y enlace; no prueba de potencia.
    // Ultimo ACK R con LOAD ON;0 representa sin carga confirmada. No derivar
    // de STATE de telemetria: WAIT_500MS no significa que la carga se apago.
    // [Guía] Actualiza `auxiliaryLoadOhm` con `0`.
    float auxiliaryLoadOhm = 0;
    // [Guía] Actualiza `auxiliaryLoadStateValid` con `false, auxiliaryLoadOn = false`.
    bool auxiliaryLoadStateValid = false, auxiliaryLoadOn = false;
    // [Guía] Actualiza `m1AuxiliaryAdjusting` con `false`.
    bool m1AuxiliaryAdjusting = false; // Rampa remota aun tiene margen/tiempo para ayudar M1.
    // [Guía] Actualiza `bus` con `0, busRaw = 0, batteryV = 0, batteryI = 0, batteryIRaw = 0`.
    float bus = 0, busRaw = 0, batteryV = 0, batteryI = 0, batteryIRaw = 0;
    // [Guía] Actualiza `pv` con `0, pvRaw = 0, pvI = 0, pvIRaw = 0, busIRaw = 0, soc = 0`.
    float pv = 0, pvRaw = 0, pvI = 0, pvIRaw = 0, busIRaw = 0, soc = 0;
    // [Guía] Actualiza `pvBusLimit` con `Config::MPPT_BUS_MAX_V`.
    float pvBusLimit = Config::MPPT_BUS_MAX_V;
    // Diagnostico de la muestra de disparo, independiente de los promedios.
    // [Guía] Actualiza `adcBatteryI` con `0, adcBusV = 0, adcBatteryV = 0`.
    uint16_t adcBatteryI = 0, adcBusV = 0, adcBatteryV = 0;
    // [Guía] Actualiza `zeroBatteryAdc` con `0`.
    float zeroBatteryAdc = 0;
  };
  // Foto del PRIMER disparo: conservarla aunque despues Vbus/Ibat se recuperen.
  // Los duties son los pedidos antes del corte, no las salidas ya puestas en0.
  // [Guía] Reúne bajo `FaultSnapshot` las declaraciones que comparten este tipo o ámbito.
  struct FaultSnapshot {
    // [Guía] Actualiza `valid` con `false`.
    bool valid = false;
    // [Guía] Actualiza `reason` con `"NONE"`.
    const char* reason = "NONE";
    // [Guía] Declara `sample` para conservar muestra en este ámbito.
    Input sample;
    // [Guía] Actualiza `duty1` con `0, duty2 = 0, duty3 = 0`.
    float duty1 = 0, duty2 = 0, duty3 = 0;
  };
  // Contar muestras y maximo durante una conexion pasiva anomala en WAIT.
  // first es la primera muestra del episodio; no se afirma que el pico sea ruido.
  // [Guía] Reúne bajo `ConnectionDiagnostic` las declaraciones que comparten este tipo o ámbito.
  struct ConnectionDiagnostic {
    // [Guía] Declara `first` para conservar first en este ámbito.
    Input first;
    // [Guía] Actualiza `overSamples` con `0`.
    uint32_t overSamples = 0;
    // [Guía] Actualiza `maximumRaw` con `0`.
    float maximumRaw = 0;
  };
  // Constructor delegado: firmware usa Config; pruebas pueden aportar Settings.
  // Los limites de duty pertenecen a cada PI aunque cambien sus ganancias.
  // [Guía] Construye BatteryTestController con la configuración inicial indicada.
  BatteryTestController() : BatteryTestController(Settings()) {}
  // [Guía] Construye BatteryTestController con la configuración inicial indicada. Recibe `settings`.
  explicit BatteryTestController(const Settings& settings)
    // [Guía] Define una de las dos salidas de la expresión condicional. Aquí intervienen PI de bus aplicado a Q3.
    : settings_(settings), pi3_(settings.kp3, settings.ki3,
        // [Guía] Aporta `Config::Q3_DUTY_MIN, Config::Q3_DUTY_MAX` como parte de los parámetros de la operación.
        Config::Q3_DUTY_MIN, Config::Q3_DUTY_MAX),
      // [Guía] Invoca `pi2_` con los argumentos de esta expresión. Aquí intervienen PI de bus aplicado a Q2.
      pi2_(settings.kp2, settings.ki2, Config::Q2_DUTY_MIN, Config::Q2_DUTY_MAX),
      // [Guía] Invoca `pi1_` con los argumentos de esta expresión. Aquí intervienen PI de bus aplicado a Q1.
      pi1_(Config::BUS_KP, Config::BUS_KI, Config::Q1_DUTY_MIN, Config::Q1_DUTY_MAX),
      // [Guía] Invoca `mppt_` con los argumentos de esta expresión. Aquí intervienen buscador P&O en la
      // [Guía] coordenada de duty.
      mppt_(Config::Q1_DUTY_MIN, Config::Q1_DUTY_MAX) {}

  // Reiniciar una espera de fuentes, no autorizar una fuente ni borrar FAULT.
  // Limpiar filtros/holds obliga a adquirir evidencia NUEVA tras el reinicio.
  // Los contadores de preparacion son por episodio, no una memoria permanente.
  // [Guía] Inicializa la EMS, sus filtros, ventanas, confirmaciones y salidas. La selección de fuente se realiza
  // [Guía] después con entradas válidas.
  void begin() {
    // [Guía] Comprueba `state_ == FAULT`. Si se cumple, termina la función en esta rama. Aquí intervienen estado
    // [Guía] operativo interno.
    if (state_ == FAULT) return; // Un reinicio normal de EMS no borra fallos.
    // [Guía] Actualiza `faultSnapshot_` con `FaultSnapshot()`.
    faultSnapshot_ = FaultSnapshot();
    // [Guía] Actualiza `connectionDiagnostic_` con `ConnectionDiagnostic()`.
    connectionDiagnostic_ = ConnectionDiagnostic();
    // [Guía] Actualiza `connectionPending_` con `false`.
    connectionPending_ = false;
    // [Guía] Borra la confirmación o acumulación en `connectionOver_` para que no se arrastre al siguiente
    // [Guía] contexto.
    connectionOver_.clear();
    // [Guía] Borra la confirmación o acumulación en `connectionRecovery_` para que no se arrastre al siguiente
    // [Guía] contexto.
    connectionRecovery_.clear();
    // [Guía] Borra la confirmación o acumulación en `q3CurrentOver_` para que no se arrastre al siguiente
    // [Guía] contexto.
    q3CurrentOver_.clear();
    // [Guía] Borra la confirmación o acumulación en `q3ChargeOver_` para que no se arrastre al siguiente
    // [Guía] contexto.
    q3ChargeOver_.clear();
    // [Guía] Borra la confirmación o acumulación en `q3VoltageOver_` para que no se arrastre al siguiente
    // [Guía] contexto.
    q3VoltageOver_.clear();
    // [Guía] Borra la confirmación o acumulación en `q3BusOver_` para que no se arrastre al siguiente contexto.
    q3BusOver_.clear();
    // [Guía] Borra la confirmación o acumulación en `pvBusOver_` para que no se arrastre al siguiente contexto.
    pvBusOver_.clear();
    // [Guía] Borra la confirmación o acumulación en `busRecoverOn_` para que no se arrastre al siguiente
    // [Guía] contexto.
    busRecoverOn_.clear(); busRecoverOff_.clear();
    // [Guía] Actualiza `busRecovery_` con `false`. Este dato representa indicación de que se requiere recuperar
    // [Guía] el bus.
    busRecovery_ = false;
    // [Guía] Actualiza `busSupervisorReady_` con `false`.
    busSupervisorReady_ = false;
    // [Guía] Actualiza `pvSupervisorReady_` con `false`.
    pvSupervisorReady_ = false;
    // [Guía] Actualiza `pvSupervisor_` con `0`. Este dato representa tensión de PV filtrada para supervisión.
    pvSupervisor_ = 0;
    // [Guía] Actualiza `batteryCurrentReady_` con `batteryLinkConfirmed_ = false`.
    batteryCurrentReady_ = batteryLinkConfirmed_ = false;
    // [Guía] Actualiza `batteryLinkState_` con `"WAIT_CAL"`.
    batteryLinkState_ = "WAIT_CAL";
    // [Guía] Borra la confirmación o acumulación en `batteryLinkHold_` para que no se arrastre al siguiente
    // [Guía] contexto.
    batteryLinkHold_.clear(); batteryQuiet_.clear();
    // [Guía] Reinicia `batteryWindow_` con el estado inicial dado en los argumentos.
    batteryWindow_.reset(); batteryIdle_.clear();
    // [Guía] Reinicia `busLoadWindow_` con el estado inicial dado en los argumentos.
    busLoadWindow_.reset(); busDemandHistory_.reset(); m2Cooldown_ = false;
    // [Guía] Invoca `resetM1BatteryDetection` con los argumentos de esta expresión.
    resetM1BatteryDetection();
    // [Guía] Actualiza `m1BatteryConfirmResets_` con `0; m1BatteryConfirmLastReset_ = "NONE"`.
    m1BatteryConfirmResets_ = 0; m1BatteryConfirmLastReset_ = "NONE";
    // [Guía] Actualiza `chargeFlowSeen_` con `false; batteryResponseState_ = "UNQUALIFIED"`.
    chargeFlowSeen_ = false; batteryResponseState_ = "UNQUALIFIED";
    // [Guía] Actualiza `chargeAttempts_` con `pvAssistStarts_ = 0; pvAssistAttempted_ = false`.
    chargeAttempts_ = pvAssistStarts_ = 0; pvAssistAttempted_ = false;
    // [Guía] Actualiza `chargeRetryReferenceReady_` con `false; chargeRetry_.clear()`.
    chargeRetryReferenceReady_ = false; chargeRetry_.clear();
    // [Guía] Actualiza `lastSourceEvent_` con `"BOOT"`.
    lastSourceEvent_ = "BOOT";
    // [Guía] Actualiza `retire2Reason_` con `"RETIRE_Q2_RECOVER_SOURCE"`.
    retire2Reason_ = "RETIRE_Q2_RECOVER_SOURCE";
    // [Guía] Borra la confirmación o acumulación en `chargeAtMinimum_` para que no se arrastre al siguiente
    // [Guía] contexto.
    chargeAtMinimum_.clear(); deficitHold_.clear();
    // [Guía] Borra la confirmación o acumulación en `batteryNoResponse_` para que no se arrastre al siguiente
    // [Guía] contexto.
    batteryNoResponse_.clear(); pvNoFlow_.clear(); m1BranchReady_.clear(); m1Reacquire_.clear();
    // [Guía] Actualiza `pvFlowSeen_` con `pvUnavailable_ = false`.
    pvFlowSeen_ = pvUnavailable_ = false;
    // [Guía] Actualiza `m1Stage_` con `M1_PI`. Este dato representa etapa interna de arranque y regulación M1.
    m1Stage_ = M1_PI;
    // [Guía] Borra la confirmación o acumulación en `m1Recovery_` para que no se arrastre al siguiente contexto.
    m1Recovery_.clear(); m1Retries_ = 0; m1Limit_ = "NONE";
    // [Guía] Borra la confirmación o acumulación en `m1LoadRecovery_` para que no se arrastre al siguiente
    // [Guía] contexto.
    m1LoadRecovery_.clear(); m1LoadRetryUsed_ = false;
    // [Guía] Actualiza `m1AcquireSlew_` con `Config::M1_ACQUIRE_SLEW_PER_SECOND`.
    m1AcquireSlew_ = Config::M1_ACQUIRE_SLEW_PER_SECOND;
    // [Guía] Borra la confirmación o acumulación en `bootPv_` para que no se arrastre al siguiente contexto.
    bootPv_.clear();
    // [Guía] Actualiza `q3CurrentPending_` con `false`.
    q3CurrentPending_ = false;
    // [Guía] Actualiza `q3CurrentSamples_` con `0`.
    q3CurrentSamples_ = 0;
    // [Guía] Actualiza `q3CurrentMaximum_` con `0`.
    q3CurrentMaximum_ = 0;
    // [Guía] Actualiza `batteryVoltageInitialized_` con `batteryVoltageReady_ = false`.
    batteryVoltageInitialized_ = batteryVoltageReady_ = false;
    // [Guía] Borra la confirmación o acumulación en `batteryVoltageHold_` para que no se arrastre al siguiente
    // [Guía] contexto.
    batteryVoltageHold_.clear();
    // [Guía] Actualiza `batteryVoltage_` con `0`. Este dato representa tensión de batería calificada.
    batteryVoltage_ = 0;
    // [Guía] Actualiza `updates_` con `missedUpdates_ = 0`. Este dato representa contador de actualizaciones.
    updates_ = missedUpdates_ = 0;
    // [Guía] Actualiza `haveInput_` con `false`.
    haveInput_ = false;
    // [Guía] Limpia las consignas y los controladores asociados a las ramas que dejan de actuar.
    clearOutputs();
    // [Guía] Actualiza `mpptTarget_` con `0`.
    mpptTarget_ = 0;
    // [Guía] Cambia el estado operativo que se evaluará a partir de la siguiente actualización. Aquí intervienen
    // [Guía] estado operativo interno.
    state_ = WAIT;
    // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
    reason_ = "WAIT_CAL";
    // [Guía] Actualiza `haveTime_` con `false`.
    haveTime_ = false;
    // [Guía] Reinicia las ventanas de confirmación al abandonar su contexto operativo.
    clearHolds();
    // [Guía] Actualiza `m2IdleResets_` con `0; m2Block_ = "INACTIVE"`.
    m2IdleResets_ = 0; m2Block_ = "INACTIVE";
  }
  // Parada humana: clearOutputs retira TODAS las propuestas. STOPPED no sale
  // automaticamente hacia un modo, a diferencia de WAIT que busca una fuente.
  // [Guía] Detiene la operación y lleva las consignas a cero mediante la ruta de espera.
  void stop() {
    // [Guía] Limpia las consignas y los controladores asociados a las ramas que dejan de actuar.
    clearOutputs();
    // [Guía] Comprueba `state_ != FAULT`. Si se cumple, ejecuta el bloque asociado. Aquí intervienen estado
    // [Guía] operativo interno.
    if (state_ != FAULT) { state_ = STOPPED; reason_ = "MANUAL_STOP"; }
    // [Guía] Reinicia las ventanas de confirmación al abandonar su contexto operativo.
    clearHolds();
  }
  // Enclavar fallo y capturar evidencia ANTES de clearOutputs. El primer motivo
  // conserva prioridad; nuevas medidas no deben sobrescribir su explicacion.
  // [Guía] Registra el motivo del fallo y retira las consignas de PWM para que el sketch las apague físicamente.
  // [Guía] Recibe `reason`.
  void fail(const char* reason) {
    // Retener el primer evento: una media posterior no explica el disparo.
    // [Guía] Comprueba `state_ == FAULT`. Si se cumple, termina la función en esta rama. Aquí intervienen estado
    // [Guía] operativo interno.
    if (state_ == FAULT) return;
    // [Guía] Actualiza `faultSnapshot_.valid` con `haveInput_`.
    faultSnapshot_.valid = haveInput_;
    // [Guía] Actualiza `faultSnapshot_.reason` con `reason`.
    faultSnapshot_.reason = reason;
    // [Guía] Actualiza `faultSnapshot_.sample` con `lastInput_`.
    faultSnapshot_.sample = lastInput_;
    // [Guía] Actualiza `faultSnapshot_.duty1` con `q1_`.
    faultSnapshot_.duty1 = q1_;
    // [Guía] Actualiza `faultSnapshot_.duty2` con `q2_`.
    faultSnapshot_.duty2 = q2_;
    // [Guía] Actualiza `faultSnapshot_.duty3` con `q3_`.
    faultSnapshot_.duty3 = q3_;
    // [Guía] Limpia las consignas y los controladores asociados a las ramas que dejan de actuar.
    clearOutputs();
    // [Guía] Cambia el estado operativo que se evaluará a partir de la siguiente actualización. Aquí intervienen
    // [Guía] estado operativo interno.
    state_ = FAULT;
    // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
    reason_ = reason;
    // [Guía] Borra la confirmación o acumulación en `resetSafe_` para que no se arrastre al siguiente contexto.
    resetSafe_.clear();
  }
  // canReset_ se califica dentro de update con corriente pequena y bus bajo.
  // Este metodo solo acepta una peticion explicita; no vuelve a energizar PWM.
  // [Guía] Comprueba si corresponde liberar el enclavamiento y vuelve a un estado desde el cual se puede
  // [Guía] seleccionar una fuente. El resultado es una decisión booleana para el llamador.
  bool resetFault() {
    // Requiere abrir la bateria, recuperar medidas y pedir rearme explicitamente.
    // [Guía] Comprueba `state_ != FAULT || !canReset_`. Si se cumple, termina la función en esta rama. Aquí
    // [Guía] intervienen estado operativo interno.
    if (state_ != FAULT || !canReset_) return false;
    // [Guía] Cambia el estado operativo que se evaluará a partir de la siguiente actualización. Aquí intervienen
    // [Guía] estado operativo interno.
    state_ = STOPPED;
    // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
    reason_ = "RESET_STOPPED";
    // [Guía] Actualiza `canReset_` con `false`.
    canReset_ = false;
    // [Guía] Reinicia las ventanas de confirmación al abandonar su contexto operativo.
    clearHolds();
    // [Guía] Entrega `true` al llamador.
    return true;
  }

  // Entrada principal, llamada a frecuencia de adquisicion. Las comprobaciones
  // antes del calendario son supervisoras; el switch de control se ejecuta a1kHz.
  // gap se mide respecto a la instantanea anterior, antes de reemplazarla.
  // [Guía] Evalúa un barrido del convertidor. Primero comprueba continuidad, fuentes y protecciones. Luego evalúa
  // [Guía] el estado y actualiza los reguladores o inicia una transferencia. Recibe `in`.
  void update(const Input& in) {
    // [Guía] Calcula la distancia entre este barrido y el anterior en µs. Las confirmaciones dejan de acumularse
    // [Guía] si las muestras no son continuas.
    const uint32_t gap = in.nowUs - lastInput_.nowUs;
    // [Guía] Comprueba `m3MpptSettling_ && (gap == 0 || gap > Config::BAT_CONNECTION_MAX_GAP_US)`. Si se cumple,
    // [Guía] ejecuta el bloque asociado. Aquí intervienen separación temporal entre muestras.
    if (m3MpptSettling_ && (gap == 0 || gap > Config::BAT_CONNECTION_MAX_GAP_US))
      // [Guía] Actualiza `m3MpptContextSinceUs_` con `in.nowUs`.
      m3MpptContextSinceUs_ = in.nowUs;
    // [Guía] Comprueba `!haveInput_ || gap == 0 || gap > Config::BAT_PASSIVE_TRIP_MAX_GAP_US`. Si se cumple,
    // [Guía] ejecuta el bloque asociado. Aquí intervienen separación temporal entre muestras.
    if (!haveInput_ || gap == 0 || gap > Config::BAT_PASSIVE_TRIP_MAX_GAP_US)
      // [Guía] Borra la confirmación o acumulación en `connectionOver_` para que no se arrastre al siguiente
      // [Guía] contexto.
      connectionOver_.clear();
    // [Guía] Comprueba `!haveInput_ || gap == 0 || gap > Config::BAT_CONNECTION_MAX_GAP_US`. Si se cumple,
    // [Guía] ejecuta el bloque asociado. Aquí intervienen separación temporal entre muestras.
    if (!haveInput_ || gap == 0 || gap > Config::BAT_CONNECTION_MAX_GAP_US)
      // [Guía] Borra la confirmación o acumulación en `connectionRecovery_` para que no se arrastre al siguiente
      // [Guía] contexto.
      connectionRecovery_.clear();
    // [Guía] Comprueba `!haveInput_ || gap == 0 || gap > Config::BAT_Q3_CURRENT_MAX_GAP_US`. Si se cumple,
    // [Guía] ejecuta el bloque asociado. Aquí intervienen separación temporal entre muestras.
    if (!haveInput_ || gap == 0 || gap > Config::BAT_Q3_CURRENT_MAX_GAP_US)
      // [Guía] Borra la confirmación o acumulación en `q3CurrentOver_` para que no se arrastre al siguiente
      // [Guía] contexto.
      q3CurrentOver_.clear();
    // [Guía] Guarda la instantánea de la última actualización para los observadores y para calcular el próximo
    // [Guía] intervalo.
    lastInput_ = in;
    // [Guía] Indica que el controlador ya recibió una muestra y puede comparar el próximo barrido con esta.
    haveInput_ = true;
    // Ibus mide SOLO cargas externas, segun confirmacion del usuario. Media
    // raw5ms independiente de Ibat; un sensor invalido no equivale a cero.
    // [Guía] Comprueba `gap == 0 || gap > Config::BAT_CONNECTION_MAX_GAP_US`. Si se cumple, ejecuta el bloque
    // [Guía] asociado. Aquí intervienen separación temporal entre muestras.
    if (gap == 0 || gap > Config::BAT_CONNECTION_MAX_GAP_US) {
      // [Guía] Reinicia `busLoadWindow_` con el estado inicial dado en los argumentos.
      busLoadWindow_.reset(); busDemandHistory_.reset(); clearM2Idle("ADC_GAP"); m2Wake_.clear();
    }
    // [Guía] Comprueba `!in.calibrating && in.busCurrentValid && isfinite(in.busIRaw)`. Si se cumple, ejecuta el
    // [Guía] bloque asociado.
    if (!in.calibrating && in.busCurrentValid && isfinite(in.busIRaw)) {
      // [Guía] Comprueba `busLoadWindow_.observe(in.busIRaw, in.nowUs, Config::M2_BUS_CURRENT_WINDOW_US, 4)`. Si
      // [Guía] se cumple, ejecuta el bloque asociado. Aquí intervienen reloj de la muestra en µs.
      if (busLoadWindow_.observe(in.busIRaw, in.nowUs, Config::M2_BUS_CURRENT_WINDOW_US, 4))
        // [Guía] Inserta una nueva ventana de corriente en el historial circular y avanza la posición de
        // [Guía] escritura.
        busDemandHistory_.push(busLoadWindow_.mean);
    // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
    } else { busLoadWindow_.reset(); busDemandHistory_.reset(); clearM2Idle("IBUS_INVALID"); m2Wake_.clear(); }
    // [Guía] Comprueba `in.calibrating`. Si se cumple, ejecuta el bloque asociado.
    if (in.calibrating) {
      // [Guía] Borra la confirmación o acumulación en `connectionOver_` para que no se arrastre al siguiente
      // [Guía] contexto.
      connectionOver_.clear();
      // [Guía] Borra la confirmación o acumulación en `connectionRecovery_` para que no se arrastre al siguiente
      // [Guía] contexto.
      connectionRecovery_.clear();
      // [Guía] Borra la confirmación o acumulación en `q3CurrentOver_` para que no se arrastre al siguiente
      // [Guía] contexto.
      q3CurrentOver_.clear();
      // [Guía] Actualiza `q3CurrentPending_` con `false`.
      q3CurrentPending_ = false;
      // [Guía] Actualiza `batteryVoltageInitialized_` con `batteryVoltageReady_ = false`.
      batteryVoltageInitialized_ = batteryVoltageReady_ = false;
      // [Guía] Borra la confirmación o acumulación en `batteryVoltageHold_` para que no se arrastre al siguiente
      // [Guía] contexto.
      batteryVoltageHold_.clear();
      // [Guía] Actualiza `pvSupervisorReady_` con `false`.
      pvSupervisorReady_ = false;
      // [Guía] Borra la confirmación o acumulación en `m1BranchReady_` para que no se arrastre al siguiente
      // [Guía] contexto.
      m1BranchReady_.clear(); m1Reacquire_.clear();
      // [Guía] Borra la confirmación o acumulación en `batteryQuiet_` para que no se arrastre al siguiente
      // [Guía] contexto.
      batteryQuiet_.clear(); batteryLinkHold_.clear();
      // [Guía] Actualiza `batteryLinkConfirmed_` con `false`. Este dato representa estimación confirmada de
      // [Guía] conexión de batería.
      batteryLinkConfirmed_ = false;
      // [Guía] Actualiza `batteryLinkState_` con `"WAIT_CAL"`.
      batteryLinkState_ = "WAIT_CAL";
      // [Guía] Reinicia `batteryWindow_` con el estado inicial dado en los argumentos.
      batteryWindow_.reset(); batteryIdle_.clear();
      // [Guía] Invoca `resetM1BatteryDetection` con los argumentos de esta expresión.
      resetM1BatteryDetection();
      // [Guía] Comprueba `active()`. Si se cumple, ejecuta `fail("CAL_DURING_CONTROL")`.
      if (active()) fail("CAL_DURING_CONTROL");
      // [Guía] Comprueba `state_ == WAIT`. Si se cumple, ejecuta `reason_ = "WAIT_CAL"`. Aquí intervienen estado
      // [Guía] operativo interno.
      if (state_ == WAIT) reason_ = "WAIT_CAL";
      // [Guía] Actualiza `haveTime_` con `false`.
      haveTime_ = false;
      // [Guía] Finaliza aquí la función y devuelve el control al llamador.
      return;
    }
    // Validar conjuntamente conversion, raw y filtro. Un float finito por si
    // solo no demuestra que el canal ADC no este saturado o sin calibracion.
    // [Guía] Actualiza `batteryGood` con `in.batteryValid && isfinite(in.batteryI)`. Este dato representa validez
    // [Guía] del puerto de batería para esta decisión.
    const bool batteryGood = in.batteryValid && isfinite(in.batteryI)
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
      && isfinite(in.batteryIRaw) && isfinite(in.batteryV);
    // [Guía] Comprueba `!batteryGood`. Si se cumple, ejecuta el bloque asociado. Aquí intervienen validez del
    // [Guía] puerto de batería para esta decisión.
    if (!batteryGood) {
      // [Guía] Actualiza `batteryCurrentReady_` con `batteryLinkConfirmed_ = false`.
      batteryCurrentReady_ = batteryLinkConfirmed_ = false;
      // [Guía] Actualiza `batteryLinkState_` con `"WAIT_BAT_MEASURE"`.
      batteryLinkState_ = "WAIT_BAT_MEASURE";
      // [Guía] Borra la confirmación o acumulación en `batteryLinkHold_` para que no se arrastre al siguiente
      // [Guía] contexto.
      batteryLinkHold_.clear(); batteryQuiet_.clear();
      // [Guía] Reinicia `batteryWindow_` con el estado inicial dado en los argumentos.
      batteryWindow_.reset();
    // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
    } else {
      // [Guía] Comprueba `!batteryCurrentReady_ || gap == 0 || gap > Config::CONTROL_TIMEOUT_US`. Si se cumple,
      // [Guía] ejecuta el bloque asociado. Aquí intervienen separación temporal entre muestras.
      if (!batteryCurrentReady_ || gap == 0 || gap > Config::CONTROL_TIMEOUT_US) {
        // [Guía] Actualiza `batteryCurrentSupervisor_` con `in.batteryI; batteryCurrentReady_ = true`. Este dato
        // [Guía] representa corriente de batería filtrada para supervisión.
        batteryCurrentSupervisor_ = in.batteryI; batteryCurrentReady_ = true;
      // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
      } else {
        // IIR dependiente del tiempo real: alpha=dt/(tau+dt). Asi el supervisor
        // no cambia su constante de tiempo simplemente por variar el muestreo.
        // [Guía] Actualiza `alpha` con `float(gap) / (Config::BAT_CURRENT_EMS_TAU_US + float(gap))`.
        const float alpha = float(gap) / (Config::BAT_CURRENT_EMS_TAU_US + float(gap));
        // [Guía] Acumula en `batteryCurrentSupervisor_` mediante `alpha * (in.batteryI -
        // [Guía] batteryCurrentSupervisor_)`. Este dato representa corriente de batería filtrada para
        // [Guía] supervisión.
        batteryCurrentSupervisor_ += alpha * (in.batteryI - batteryCurrentSupervisor_);
      }
      // [Guía] Comprueba `gap == 0 || gap > Config::BAT_CONNECTION_MAX_GAP_US`. Si se cumple, ejecuta el bloque
      // [Guía] asociado. Aquí intervienen separación temporal entre muestras.
      if (gap == 0 || gap > Config::BAT_CONNECTION_MAX_GAP_US) {
        // [Guía] Borra la confirmación o acumulación en `batteryLinkHold_` para que no se arrastre al siguiente
        // [Guía] contexto.
        batteryLinkHold_.clear(); batteryQuiet_.clear();
        // [Guía] Reinicia `batteryWindow_` con el estado inicial dado en los argumentos.
        batteryWindow_.reset();
      }
      // [Guía] Incorpora la muestra a la ventana de `batteryWindow_`. Aquí intervienen reloj de la muestra en µs.
      batteryWindow_.observe(in.batteryIRaw, in.nowUs);
      // Ruta pasiva de arranque. La conexion durante M1 se califica
      // aparte; el arranque PV no prueba bateria (falsa conexion de V20).
      // [Guía] Comprueba `!batteryLinkConfirmed_`. Si se cumple, ejecuta el bloque asociado. Aquí intervienen
      // [Guía] estimación confirmada de conexión de batería.
      if (!batteryLinkConfirmed_) {
        // [Guía] Actualiza `passiveState` con `state_ == WAIT && !active() && !connectionPending_`.
        const bool passiveState = state_ == WAIT && !active() && !connectionPending_;
        // [Guía] Actualiza `pvOff` con `in.pvValid && isfinite(in.pv) && isfinite(in.pvRaw)`.
        const bool pvOff = in.pvValid && isfinite(in.pv) && isfinite(in.pvRaw)
          // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen tensión del
          // [Guía] PV en V.
          && isfinite(in.pvI) && in.pv <= Config::BAT_PV_OFF_V
          // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
          && in.pvRaw <= Config::BAT_PV_OFF_V && !solarAvailable(in);
        // Ipv rapida podia cruzar80 mA por offset/rizado con Vpv=0 y
        // reiniciar indefinidamente los200 ms de reposo antes de M5.
        // PV util por corriente con Vpv>=1 V SI cancela la calificacion.
        // [Guía] Actualiza `quiet` con `batteryQuiet_.held(passiveState && pvOff`.
        const bool quiet = batteryQuiet_.held(passiveState && pvOff,
          // [Guía] Completa la declaración o expresión con `in.nowUs, Config::BAT_LINK_PV_OFF_US)`. Aquí
          // [Guía] intervienen reloj de la muestra en µs.
          in.nowUs, Config::BAT_LINK_PV_OFF_US);
        // [Guía] Actualiza `busPrecharged` con `in.busValid && isfinite(in.bus) && isfinite(in.busRaw)`.
        const bool busPrecharged = in.busValid && isfinite(in.bus) && isfinite(in.busRaw)
          // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
          && in.busRaw >= Config::BAT_BUS_START_MIN_V
          // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
          && in.busRaw < Config::BAT_TEST_BUS_TRIP_V;
        // La media por bloques ya existe para detectar retirada de bateria.
        // Usarla tambien al arrancar evita que cada valle ADC reinicie300 ms.
        // [Guía] Actualiza `dischargeMean` con `batteryWindow_.ready`.
        const bool dischargeMean = batteryWindow_.ready
          // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
          && isfinite(batteryWindow_.mean)
          // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
          && batteryWindow_.mean > Config::BAT_LINK_DETECT_A
          // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen corriente de
          // [Guía] batería filtrada para supervisión.
          && batteryCurrentSupervisor_ > Config::BAT_LINK_DETECT_A
          // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
          && batteryWindow_.mean < Config::BAT_DISCHARGE_SOFT_A
          // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen corriente de
          // [Guía] batería filtrada para supervisión.
          && batteryCurrentSupervisor_ < Config::BAT_DISCHARGE_SOFT_A;
        // [Guía] Actualiza `currentMargin` con `in.batteryIRaw < Config::BAT_DISCHARGE_SOFT_A`.
        const bool currentMargin = in.batteryIRaw < Config::BAT_DISCHARGE_SOFT_A;
        // [Guía] Actualiza `passiveDischarge` con `quiet && busPrecharged && dischargeMean && currentMargin`.
        const bool passiveDischarge = quiet && busPrecharged && dischargeMean && currentMargin;
        // [Guía] Actualiza `batteryLinkConfirmed_` con `batteryLinkHold_.held(passiveDischarge`. Este dato
        // [Guía] representa estimación confirmada de conexión de batería.
        batteryLinkConfirmed_ = batteryLinkHold_.held(passiveDischarge,
          // [Guía] Completa la declaración o expresión con `in.nowUs, Config::BAT_LINK_CONFIRM_US)`. Aquí
          // [Guía] intervienen reloj de la muestra en µs.
          in.nowUs, Config::BAT_LINK_CONFIRM_US);
        // Exponer el requisito pendiente sin confundirlo con un fallo del PI.
        // [Guía] Actualiza `batteryLinkState_` con `connectionPending_ ? "WAIT_CONNECTION_RECOVERY"`.
        batteryLinkState_ = connectionPending_ ? "WAIT_CONNECTION_RECOVERY"
          // [Guía] Define una de las dos salidas de la expresión condicional.
          : !passiveState ? "WAIT_PASSIVE_STATE" : !pvOff ? "WAIT_PV_OFF"
          // [Guía] Define una de las dos salidas de la expresión condicional.
          : !quiet ? "PV_OFF_CONFIRM" : !batteryWindow_.ready ? "WAIT_CURRENT_WINDOW"
          // [Guía] Define una de las dos salidas de la expresión condicional.
          : !busPrecharged ? "WAIT_BAT_PRECHARGE"
          // [Guía] Define una de las dos salidas de la expresión condicional.
          : !dischargeMean ? "WAIT_DISCHARGE_MEAN"
          // [Guía] Define una de las dos salidas de la expresión condicional.
          : !currentMargin ? "WAIT_CURRENT_MARGIN" : "PASSIVE_CONFIRM";
        // [Guía] Comprueba `batteryLinkConfirmed_`. Si se cumple, ejecuta `batteryLinkState_ =
        // [Guía] "PASSIVE_ESTIMATE"`. Aquí intervienen estimación confirmada de conexión de batería.
        if (batteryLinkConfirmed_) batteryLinkState_ = "PASSIVE_ESTIMATE";
      }
    }
    // Supervisores lentos para decisiones EMS. No reemplazan los cortes raw
    // ni el bus rapido usado por los PI Q1/Q3. Q2 usa busSupervisor_ aparte.
    // [Guía] Actualiza `busGood` con `in.busValid && isfinite(in.bus) && isfinite(in.busRaw)`. Este dato
    // [Guía] representa validez de la medición de bus.
    const bool busGood = in.busValid && isfinite(in.bus) && isfinite(in.busRaw);
    // [Guía] Comprueba `!busGood`. Si se cumple, ejecuta `busSupervisorReady_ = false`. Aquí intervienen validez
    // [Guía] de la medición de bus.
    if (!busGood) busSupervisorReady_ = false;
    // [Guía] Comprueba `!busSupervisorReady_ || gap == 0 || gap > Config::CONTROL_TIMEOUT_US`. Si se cumple,
    // [Guía] ejecuta el bloque asociado. Aquí intervienen separación temporal entre muestras.
    else if (!busSupervisorReady_ || gap == 0 || gap > Config::CONTROL_TIMEOUT_US) {
      // [Guía] Actualiza `busSupervisor_` con `in.bus; busSupervisorReady_ = true`. Este dato representa tensión
      // [Guía] de bus filtrada para supervisión.
      busSupervisor_ = in.bus; busSupervisorReady_ = true;
    // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
    } else {
      // [Guía] Actualiza `alpha` con `float(gap) / (Config::EMS_BUS_FILTER_TAU_US + float(gap))`.
      const float alpha = float(gap) / (Config::EMS_BUS_FILTER_TAU_US + float(gap));
      // [Guía] Acumula en `busSupervisor_` mediante `alpha * (in.bus - busSupervisor_)`. Este dato representa
      // [Guía] tensión de bus filtrada para supervisión.
      busSupervisor_ += alpha * (in.bus - busSupervisor_);
    }
    // [Guía] Actualiza `solarOnly` con `state_ == START1 || state_ == M1`.
    bool solarOnly = state_ == START1 || state_ == M1;
    // Fuente PV sin aporte confirmado: no confundir capacitancia/backfeed
    // con panel util. La estimacion se recupera al volver corriente real.
    // [Guía] Actualiza `pvMeasured` con `in.pvValid && isfinite(in.pvI)`.
    const bool pvMeasured = in.pvValid && isfinite(in.pvI)
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen tensión del PV
      // [Guía] en V.
      && isfinite(in.pvRaw) && isfinite(in.pv);
    // [Guía] Comprueba `!pvMeasured`. Si se cumple, ejecuta el bloque asociado.
    if (!pvMeasured) {
      // [Guía] Actualiza `pvSupervisorReady_` con `false`.
      pvSupervisorReady_ = false;
      // [Guía] Borra la confirmación o acumulación en `m1BranchReady_` para que no se arrastre al siguiente
      // [Guía] contexto.
      m1BranchReady_.clear(); m1Reacquire_.clear();
    // [Guía] Comprueba `!pvSupervisorReady_ || gap == 0 || gap > Config::CONTROL_TIMEOUT_US`. Si se cumple,
    // [Guía] ejecuta el bloque asociado. Aquí intervienen separación temporal entre muestras.
    } else if (!pvSupervisorReady_ || gap == 0 || gap > Config::CONTROL_TIMEOUT_US) {
      // [Guía] Actualiza `pvSupervisor_` con `in.pv; pvSupervisorReady_ = true`. Este dato representa tensión de
      // [Guía] PV filtrada para supervisión.
      pvSupervisor_ = in.pv; pvSupervisorReady_ = true;
      // [Guía] Borra la confirmación o acumulación en `m1BranchReady_` para que no se arrastre al siguiente
      // [Guía] contexto.
      m1BranchReady_.clear(); m1Reacquire_.clear();
    // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
    } else {
      // [Guía] Actualiza `alpha` con `float(gap) / (Config::M1_PV_FILTER_TAU_US + float(gap))`.
      const float alpha = float(gap) / (Config::M1_PV_FILTER_TAU_US + float(gap));
      // [Guía] Acumula en `pvSupervisor_` mediante `alpha * (in.pv - pvSupervisor_)`. Este dato representa
      // [Guía] tensión de PV filtrada para supervisión.
      pvSupervisor_ += alpha * (in.pv - pvSupervisor_);
    }
    // [Guía] Comprueba `gap > Config::CONTROL_TIMEOUT_US`. Si se cumple, ejecuta `pvNoFlow_.clear()`. Aquí
    // [Guía] intervienen separación temporal entre muestras.
    if (gap > Config::CONTROL_TIMEOUT_US) pvNoFlow_.clear();
    // [Guía] Comprueba `pvMeasured && in.pvI > Config::BAT_PV_ON_CURRENT_A`. Si se cumple, ejecuta el bloque
    // [Guía] asociado.
    if (pvMeasured && in.pvI > Config::BAT_PV_ON_CURRENT_A) {
      // [Guía] Actualiza `pvFlowSeen_` con `true; pvUnavailable_ = false; pvNoFlow_.clear()`.
      pvFlowSeen_ = true; pvUnavailable_ = false; pvNoFlow_.clear();
    // [Guía] Comprueba `q1_ > 0 && pvMeasured`. Si se cumple, ejecuta el bloque asociado. Aquí intervienen
    // [Guía] consigna de duty de Q1 (0 a 1).
    } else if (q1_ > 0 && pvMeasured
        // [Guía] Agrupa condiciones alternativas y conjuntas en la prueba en curso. Aquí intervienen reloj de la
        // [Guía] muestra en µs.
        && (pvFlowSeen_ || uint32_t(in.nowUs - pvStartUs_) >= Config::PV_START_GRACE_US)) {
      // [Guía] Comprueba `pvNoFlow_.held(in.pvI <= Config::BAT_PV_OFF_CURRENT_A,`. Si se cumple, ejecuta el
      // [Guía] bloque asociado.
      if (pvNoFlow_.held(in.pvI <= Config::BAT_PV_OFF_CURRENT_A,
            // [Guía] Completa la declaración o expresión con `in.nowUs, Config::PV_NO_FLOW_HOLD_US))
            // [Guía] pvUnavailable_ = true`. Aquí intervienen reloj de la muestra en µs.
            in.nowUs, Config::PV_NO_FLOW_HOLD_US)) pvUnavailable_ = true;
    // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
    } else pvNoFlow_.clear();
    // [Guía] Comprueba `q1_ == 0 && pvMeasured && in.pvRaw <= Config::BAT_PV_OFF_V`. Si se cumple, ejecuta el
    // [Guía] bloque asociado. Aquí intervienen consigna de duty de Q1 (0 a 1).
    if (q1_ == 0 && pvMeasured && in.pvRaw <= Config::BAT_PV_OFF_V) {
      // [Guía] Actualiza `pvFlowSeen_` con `pvUnavailable_ = false; pvNoFlow_.clear()`.
      pvFlowSeen_ = pvUnavailable_ = false; pvNoFlow_.clear();
    }
    // [Guía] Califica la disponibilidad del puerto de batería y prepara la tensión de control que utilizan las
    // [Guía] ramas de batería. Aquí intervienen validez del puerto de batería para esta decisión y separación
    // [Guía] temporal entre muestras.
    qualifyBatteryVoltage(in, batteryGood, gap);
    // [Guía] Usa la corriente de batería y su evidencia temporal para discriminar la conexión durante M1 de
    // [Guía] fluctuaciones de la medida. Aquí intervienen validez del puerto de batería para esta decisión y
    // [Guía] validez de la medición de bus.
    qualifyM1Battery(in, batteryGood, busGood, gap);
    // [Guía] Comprueba `gap == 0 || gap > Config::BAT_CONNECTION_MAX_GAP_US`. Si se cumple, ejecuta
    // [Guía] `m1AuxReliefReady_.clear()`. Aquí intervienen separación temporal entre muestras.
    if (gap == 0 || gap > Config::BAT_CONNECTION_MAX_GAP_US) m1AuxReliefReady_.clear();
    // V46: observar TODAS las adquisiciones, tambien las que no ejecutan PI.
    // Un paso de control omitido no es ausencia de medidas. Un hueco ADC>5ms,
    // medida invalida, perdida del margen rapido o corriente sin margen SI
    // descartan la confirmacion; nunca liberar P&O con una evidencia vieja.
    // [Guía] Comprueba `gap == 0 || gap > Config::BAT_CONNECTION_MAX_GAP_US`. Si se cumple, ejecuta
    // [Guía] `hotplugPvReady_.clear()`. Aquí intervienen separación temporal entre muestras.
    if (gap == 0 || gap > Config::BAT_CONNECTION_MAX_GAP_US) hotplugPvReady_.clear();
    // [Guía] Actualiza `hotplugPvQualified_` con `hotplugPvReady_.held(hotplugPvTargetObserved(in)`.
    hotplugPvQualified_ = hotplugPvReady_.held(hotplugPvTargetObserved(in),
      // [Guía] Completa la declaración o expresión con `in.nowUs, Config::HOTPLUG_PV_CONFIRM_US)`. Aquí
      // [Guía] intervienen reloj de la muestra en µs.
      in.nowUs, Config::HOTPLUG_PV_CONFIRM_US);
    // Esta excepcion nunca se aplica a START3, control, transferencias ni STOPPED.
    // Las excepciones de persistencia tienen un contexto restringido. No se
    // extienden automaticamente a Q2 o a cualquier estado con bateria presente.
    // [Guía] Actualiza `qualifyConnection` con `Config::BAT_QUALIFY_PASSIVE_CONNECTION`.
    const bool qualifyConnection = Config::BAT_QUALIFY_PASSIVE_CONNECTION
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen estado operativo
      // [Guía] interno.
      && state_ == WAIT && !active();
    // [Guía] Actualiza `dischargeOver` con `batteryGood`.
    const bool dischargeOver = batteryGood
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
      && in.batteryIRaw >= Config::BAT_DISCHARGE_TRIP_A;
    // Limitar la excepcion a bateria sola, no extenderla a PV o transferencias.
    // [Guía] Actualiza `qualifyQ3` con `Config::BAT_Q3_QUALIFY_CURRENT && q1_ == 0 && q2_ == 0`.
    const bool qualifyQ3 = Config::BAT_Q3_QUALIFY_CURRENT && q1_ == 0 && q2_ == 0
      // [Guía] Agrupa condiciones alternativas y conjuntas en la prueba en curso. Aquí intervienen estado
      // [Guía] operativo interno.
      && (state_ == START3 || state_ == M5);
    // Descarga positiva en rampa solar/M4: misma confirmacion corta, sin
    // extender excepciones a Q2 ni eliminar cortes por sobrecorriente sostenida.
    // [Guía] Actualiza `qualifyDischarge` con `qualifyQ3 || (Config::BAT_Q3_QUALIFY_CURRENT`.
    const bool qualifyDischarge = qualifyQ3 || (Config::BAT_Q3_QUALIFY_CURRENT
      // [Guía] Agrupa condiciones alternativas y conjuntas en la prueba en curso. Aquí intervienen consigna de
      // [Guía] duty de Q2 (0 a 1) y estado operativo interno.
      && q2_ == 0 && (state_ == STARTPV || state_ == M4 || state_ == RETIRE3
        // [Guía] Añade una alternativa suficiente dentro de la condición compuesta. Aquí intervienen estado
        // [Guía] operativo interno.
        || state_ == INTERLOCK || state_ == RETIRE2));
    // [Guía] Actualiza `q3Over` con `batteryGood && (dischargeOver`.
    const bool q3Over = batteryGood && (dischargeOver
      // [Guía] Añade una alternativa suficiente dentro de la condición compuesta.
      || in.batteryI >= Config::BAT_DISCHARGE_TRIP_A);
    // Solo M5/arranque Q3: confirmar tambien carga inversa y sobretension
    // de bateria. Una muestra aislada congela el control; no se ignora.
    // [Guía] Actualiza `chargeOver` con `batteryGood`.
    const bool chargeOver = batteryGood
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
      && (-in.batteryIRaw >= Config::BAT_CHARGE_TRIP_A
          // [Guía] Añade una alternativa suficiente dentro de la condición compuesta.
          || -in.batteryI >= Config::BAT_CHARGE_TRIP_A);
    // [Guía] Actualiza `voltageOver` con `batteryGood && in.batteryV >= Config::BAT_TEST_MAX_V`.
    const bool voltageOver = batteryGood && in.batteryV >= Config::BAT_TEST_MAX_V;
    // [Guía] Actualiza `qualifyVoltage` con `Config::BAT_Q3_QUALIFY_CURRENT && q2_ == 0`.
    const bool qualifyVoltage = Config::BAT_Q3_QUALIFY_CURRENT && q2_ == 0
      // [Guía] Agrupa condiciones alternativas y conjuntas en la prueba en curso. Aquí intervienen estado
      // [Guía] operativo interno.
      && (state_ == START3 || state_ == M5 || state_ == STARTPV || state_ == M4);
    // [Guía] Comprueba `!qualifyQ3 || gap == 0 || gap > Config::BAT_Q3_CURRENT_MAX_GAP_US`. Si se cumple, ejecuta
    // [Guía] el bloque asociado. Aquí intervienen separación temporal entre muestras.
    if (!qualifyQ3 || gap == 0 || gap > Config::BAT_Q3_CURRENT_MAX_GAP_US) {
      // [Guía] Borra la confirmación o acumulación en `q3ChargeOver_` para que no se arrastre al siguiente
      // [Guía] contexto.
      q3ChargeOver_.clear();
    }
    // [Guía] Comprueba `!qualifyVoltage || gap == 0 || gap > Config::BAT_Q3_CURRENT_MAX_GAP_US`. Si se cumple,
    // [Guía] ejecuta el bloque asociado. Aquí intervienen separación temporal entre muestras.
    if (!qualifyVoltage || gap == 0 || gap > Config::BAT_Q3_CURRENT_MAX_GAP_US)
      // [Guía] Borra la confirmación o acumulación en `q3VoltageOver_` para que no se arrastre al siguiente
      // [Guía] contexto.
      q3VoltageOver_.clear();
    // [Guía] Actualiza `confirmedCharge` con `qualifyQ3`.
    const bool confirmedCharge = qualifyQ3
      // [Guía] Define una de las dos salidas de la expresión condicional. Aquí intervienen reloj de la muestra en
      // [Guía] µs.
      ? (q3ChargeOver_.held(chargeOver, in.nowUs, Config::BAT_Q3_CURRENT_CONFIRM_US)
         // [Guía] Agrupa condiciones alternativas y conjuntas en la prueba en curso. Aquí intervienen separación
         // [Guía] temporal entre muestras.
         || (chargeOver && gap > Config::BAT_Q3_CURRENT_MAX_GAP_US))
      // [Guía] Define una de las dos salidas de la expresión condicional.
      : chargeOver;
    // [Guía] Actualiza `confirmedVoltage` con `qualifyVoltage`.
    const bool confirmedVoltage = qualifyVoltage
      // [Guía] Define una de las dos salidas de la expresión condicional. Aquí intervienen reloj de la muestra en
      // [Guía] µs.
      ? (q3VoltageOver_.held(voltageOver, in.nowUs, Config::BAT_Q3_CURRENT_CONFIRM_US)
         // [Guía] Agrupa condiciones alternativas y conjuntas en la prueba en curso. Aquí intervienen separación
         // [Guía] temporal entre muestras.
         || (voltageOver && gap > Config::BAT_Q3_CURRENT_MAX_GAP_US))
      // [Guía] Define una de las dos salidas de la expresión condicional.
      : voltageOver;
    // [Guía] Actualiza `q3FeedbackPending` con `(qualifyQ3 && chargeOver) || (qualifyVoltage && voltageOver)`.
    const bool q3FeedbackPending = (qualifyQ3 && chargeOver) || (qualifyVoltage && voltageOver);
    // [Guía] Actualiza `busOver` con `busGood && in.busRaw >= Config::BAT_TEST_BUS_TRIP_V`.
    const bool busOver = busGood && in.busRaw >= Config::BAT_TEST_BUS_TRIP_V;
    // [Guía] Comprueba `!qualifyQ3 || gap == 0 || gap > Config::BAT_Q3_CURRENT_MAX_GAP_US`. Si se cumple, ejecuta
    // [Guía] el bloque asociado. Aquí intervienen separación temporal entre muestras.
    if (!qualifyQ3 || gap == 0 || gap > Config::BAT_Q3_CURRENT_MAX_GAP_US)
      // [Guía] Borra la confirmación o acumulación en `q3BusOver_` para que no se arrastre al siguiente contexto.
      q3BusOver_.clear();
    // Nunca retrasar el corte duro. Para un exceso moderado, mantener duty
    // e integral durante la confirmacion; el resto de protecciones sigue activo.
    // [Guía] Actualiza `confirmedBus` con `(busGood && in.busRaw >= Config::BAT_BUS_HARD_TRIP_V)`.
    const bool confirmedBus = (busGood && in.busRaw >= Config::BAT_BUS_HARD_TRIP_V)
      // [Guía] Añade una alternativa suficiente dentro de la condición compuesta.
      || (qualifyQ3
          // [Guía] Define una de las dos salidas de la expresión condicional. Aquí intervienen reloj de la
          // [Guía] muestra en µs.
          ? (q3BusOver_.held(busOver, in.nowUs, Config::BAT_BUS_CONFIRM_US)
             // [Guía] Agrupa condiciones alternativas y conjuntas en la prueba en curso. Aquí intervienen
             // [Guía] separación temporal entre muestras.
             || (busOver && gap > Config::BAT_Q3_CURRENT_MAX_GAP_US))
          // [Guía] Define una de las dos salidas de la expresión condicional.
          : busOver);
    // [Guía] Actualiza `q3BusPending` con `qualifyQ3 && busOver && !confirmedBus`.
    const bool q3BusPending = qualifyQ3 && busOver && !confirmedBus;
    // Q1 activo: confirmar excesos solares moderados, sin omitir corte duro.
    // [Guía] Actualiza `pvBusHigh` con `q1_ > 0 && busGood && in.busRaw >= in.pvBusLimit`.
    const bool pvBusHigh = q1_ > 0 && busGood && in.busRaw >= in.pvBusLimit;
    // [Guía] Comprueba `q1_ == 0 || gap == 0 || gap > Config::BAT_Q3_CURRENT_MAX_GAP_US`. Si se cumple, ejecuta
    // [Guía] el bloque asociado. Aquí intervienen consigna de duty de Q1 (0 a 1) y separación temporal entre
    // [Guía] muestras.
    if (q1_ == 0 || gap == 0 || gap > Config::BAT_Q3_CURRENT_MAX_GAP_US)
      // [Guía] Borra la confirmación o acumulación en `pvBusOver_` para que no se arrastre al siguiente contexto.
      pvBusOver_.clear();
    // [Guía] Actualiza `confirmedPvBus` con `pvBusOver_.held(pvBusHigh, in.nowUs`.
    const bool confirmedPvBus = pvBusOver_.held(pvBusHigh, in.nowUs,
      // [Guía] Aporta `Config::MPPT_BUS_CONFIRM_US` como parte de los parámetros de la operación.
      Config::MPPT_BUS_CONFIRM_US)
      // [Guía] Agrupa condiciones alternativas y conjuntas en la prueba en curso. Aquí intervienen separación
      // [Guía] temporal entre muestras.
      || (pvBusHigh && gap > Config::BAT_Q3_CURRENT_MAX_GAP_US);
    // [Guía] Actualiza `pvBusPending` con `pvBusHigh && !confirmedPvBus`.
    const bool pvBusPending = pvBusHigh && !confirmedPvBus;
    // [Guía] Actualiza `q3CurrentPending_` con `qualifyDischarge && q3Over`.
    q3CurrentPending_ = qualifyDischarge && q3Over;
    // [Guía] Actualiza `confirmedDischarge` con `dischargeOver`.
    bool confirmedDischarge = dischargeOver;
    // [Guía] Comprueba `qualifyConnection`. Si se cumple, ejecuta el bloque asociado.
    if (qualifyConnection) {
      // [Guía] Comprueba `dischargeOver`. Si se cumple, ejecuta el bloque asociado.
      if (dischargeOver) {
        // [Guía] Comprueba `connectionDiagnostic_.overSamples == 0`. Si se cumple, ejecuta
        // [Guía] `connectionDiagnostic_.first = in`.
        if (connectionDiagnostic_.overSamples == 0) connectionDiagnostic_.first = in;
        // [Guía] Comprueba `connectionDiagnostic_.overSamples != UINT32_MAX`. Si se cumple, ejecuta el bloque
        // [Guía] asociado.
        if (connectionDiagnostic_.overSamples != UINT32_MAX)
          // [Guía] Completa la declaración o expresión con `++connectionDiagnostic_.overSamples`.
          ++connectionDiagnostic_.overSamples;
        // [Guía] Actualiza `connectionDiagnostic_.maximumRaw` con `fmaxf(connectionDiagnostic_.maximumRaw`.
        connectionDiagnostic_.maximumRaw = fmaxf(connectionDiagnostic_.maximumRaw,
                                                 // [Guía] Completa la declaración o expresión con `in.batteryIRaw)`.
                                                 in.batteryIRaw);
        // [Guía] Actualiza `connectionPending_` con `true`.
        connectionPending_ = true;
        // [Guía] Borra la confirmación o acumulación en `connectionRecovery_` para que no se arrastre al
        // [Guía] siguiente contexto.
        connectionRecovery_.clear();
      }
      // [Guía] Actualiza `confirmedDischarge` con `connectionOver_.held(dischargeOver, in.nowUs`.
      confirmedDischarge = connectionOver_.held(dischargeOver, in.nowUs,
        // [Guía] Aporta `Config::BAT_PASSIVE_TRIP_CONFIRM_US` como parte de los parámetros de la operación.
        Config::BAT_PASSIVE_TRIP_CONFIRM_US);
    // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
    } else {
      // [Guía] Borra la confirmación o acumulación en `connectionOver_` para que no se arrastre al siguiente
      // [Guía] contexto.
      connectionOver_.clear();
      // [Guía] Comprueba `qualifyDischarge`. Si se cumple, ejecuta el bloque asociado.
      if (qualifyDischarge) {
        // [Guía] Comprueba `q3Over`. Si se cumple, ejecuta el bloque asociado.
        if (q3Over) {
          // [Guía] Comprueba `q3CurrentSamples_ != UINT32_MAX`. Si se cumple, ejecuta `++q3CurrentSamples_`.
          if (q3CurrentSamples_ != UINT32_MAX) ++q3CurrentSamples_;
          // [Guía] Actualiza `q3CurrentMaximum_` con `fmaxf(q3CurrentMaximum_, in.batteryIRaw)`.
          q3CurrentMaximum_ = fmaxf(q3CurrentMaximum_, in.batteryIRaw);
        }
        // Con un hueco grande no hay evidencia para tolerar corriente alta.
        // [Guía] Actualiza `confirmedDischarge` con `q3CurrentOver_.held(q3Over, in.nowUs`.
        confirmedDischarge = q3CurrentOver_.held(q3Over, in.nowUs,
          // [Guía] Aporta `Config::BAT_Q3_CURRENT_CONFIRM_US` como parte de los parámetros de la operación.
          Config::BAT_Q3_CURRENT_CONFIRM_US)
          // [Guía] Agrupa condiciones alternativas y conjuntas en la prueba en curso. Aquí intervienen separación
          // [Guía] temporal entre muestras.
          || (q3Over && gap > Config::BAT_Q3_CURRENT_MAX_GAP_US);
      }
    }
    // [Guía] Comprueba `!qualifyDischarge`. Si se cumple, ejecuta `q3CurrentOver_.clear()`.
    if (!qualifyDischarge) q3CurrentOver_.clear();
    // Bateria abierta conserva Vbat OCV: solo inferir falta de respuesta si
    // Q3 pide aporte con deficit, o Q2 tiene margen de accion y no hay corriente.
    // [Guía] Comprueba `gap > Config::BAT_CONNECTION_MAX_GAP_US`. Si se cumple, ejecuta el bloque asociado. Aquí
    // [Guía] intervienen separación temporal entre muestras.
    if (gap > Config::BAT_CONNECTION_MAX_GAP_US) {
      // [Guía] Borra la confirmación o acumulación en `batteryNoResponse_` para que no se arrastre al siguiente
      // [Guía] contexto.
      batteryNoResponse_.clear(); batteryIdle_.clear();
    }
    // [Guía] Comprueba `gap == 0 || gap > Config::BAT_CONNECTION_MAX_GAP_US`. Si se cumple, ejecuta
    // [Guía] `chargeFlowConfirm_.clear()`. Aquí intervienen separación temporal entre muestras.
    if (gap == 0 || gap > Config::BAT_CONNECTION_MAX_GAP_US) chargeFlowConfirm_.clear();
    // [Guía] Actualiza `idleCurrent` con `chargeIdleCurrent()`.
    const float idleCurrent = chargeIdleCurrent();
    // [Guía] Actualiza `noBatteryFlow` con `batteryGood && batteryWindow_.ready`.
    const bool noBatteryFlow = batteryGood && batteryWindow_.ready
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
      && fabsf(batteryWindow_.mean - idleCurrent) <= Config::BAT_NO_RESPONSE_A
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen corriente de
      // [Guía] batería filtrada para supervisión.
      && fabsf(batteryCurrentSupervisor_ - idleCurrent) <= Config::BAT_NO_RESPONSE_A;
    // Una lectura negativa de reposo no demuestra que Q2 ya haya cargado.
    // Comparar ambas medias con la referencia previa y confirmar dos ventanas.
    // [Guía] Comprueba `chargeFlowConfirm_.held((state_ == M3 || state_ == M2)`. Si se cumple, ejecuta el bloque
    // [Guía] asociado. Aquí intervienen estado operativo interno.
    if (chargeFlowConfirm_.held((state_ == M3 || state_ == M2)
        // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen validez del
        // [Guía] puerto de batería para esta decisión.
        && batteryGood && batteryWindow_.ready
        // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
        && batteryWindow_.mean < idleCurrent - Config::BAT_CHARGE_FLOW_SEEN_A
        // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen corriente de
        // [Guía] batería filtrada para supervisión.
        && batteryCurrentSupervisor_ < idleCurrent - Config::BAT_CHARGE_FLOW_SEEN_A,
        // [Guía] Completa la declaración o expresión con `in.nowUs, Config::BAT_CURRENT_WINDOW_US))`. Aquí
        // [Guía] intervienen reloj de la muestra en µs.
        in.nowUs, Config::BAT_CURRENT_WINDOW_US))
      // [Guía] Actualiza `chargeFlowSeen_` con `true`.
      chargeFlowSeen_ = true;
    // [Guía] Actualiza `missingDischarge` con `q3_ >= Config::BAT_Q3_RESPONSE_DUTY`.
    const bool missingDischarge = q3_ >= Config::BAT_Q3_RESPONSE_DUTY
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen tensión de bus
      // [Guía] filtrada para supervisión.
      && busSupervisor_ < Config::BUS_REFERENCE_V - 0.75f;
    // [Guía] Actualiza `missingCharge` con `(state_ == M3 || state_ == START2`.
    const bool missingCharge = (state_ == M3 || state_ == START2
        // [Guía] Agrupa condiciones alternativas y conjuntas en la prueba en curso. Aquí intervienen estado
        // [Guía] operativo interno y controlador de corriente de Q2 en M2.
        || (state_ == M2 && m2Current_.reference() < -0.2f))
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen consigna de duty
      // [Guía] de Q2 (0 a 1).
      && q2_ >= Config::Q2_DUTY_MIN && in.pvValid
      // [Guía] Agrupa condiciones alternativas y conjuntas en la prueba en curso. Aquí intervienen estado
      // [Guía] operativo interno.
      && (((state_ == M3 || state_ == M2) && chargeFlowSeen_)
          // [Guía] Añade una alternativa suficiente dentro de la condición compuesta.
          || chargeDriveAvailable(in));
    // [Guía] Actualiza `responsePending` con `noBatteryFlow && (missingDischarge || missingCharge)`.
    const bool responsePending = noBatteryFlow && (missingDischarge || missingCharge);
    // [Guía] Actualiza `noBatteryResponse` con `batteryNoResponse_.held(responsePending`.
    const bool noBatteryResponse = batteryNoResponse_.held(responsePending,
      // [Guía] Completa la declaración o expresión con `in.nowUs, Config::BAT_NO_RESPONSE_HOLD_US)`. Aquí
      // [Guía] intervienen reloj de la muestra en µs.
      in.nowUs, Config::BAT_NO_RESPONSE_HOLD_US);
    // Cero corriente en M4 estable no prueba apertura. Aun asi, PV puede
    // asumir la regulacion y retirar una rama que no aporta energia.
    // [Guía] Actualiza `idleCandidate` con `(state_ == M4 || state_ == START4)`.
    const bool idleCandidate = (state_ == M4 || state_ == START4)
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
      && noBatteryFlow && solarAvailable(in)
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen tensión de bus
      // [Guía] filtrada para supervisión.
      && fabsf(busSupervisor_ - Config::BUS_REFERENCE_V) <= Config::BAT_IDLE_BUS_BAND_V;
    // [Guía] Actualiza `idleBattery` con `batteryIdle_.held(idleCandidate`.
    const bool idleBattery = batteryIdle_.held(idleCandidate,
      // [Guía] Completa la declaración o expresión con `in.nowUs, Config::BAT_IDLE_RETURN_US)`. Aquí intervienen
      // [Guía] reloj de la muestra en µs.
      in.nowUs, Config::BAT_IDLE_RETURN_US);
    // [Guía] Actualiza `batteryResponseState_` con `!batteryGood || !batteryWindow_.ready ? "UNQUALIFIED"`.
    batteryResponseState_ = !batteryGood || !batteryWindow_.ready ? "UNQUALIFIED"
      // [Guía] Define una de las dos salidas de la expresión condicional.
      : responsePending ? "NO_RESPONSE_CONFIRM" : idleCandidate ? "IDLE_CONFIRM"
      // [Guía] Define una de las dos salidas de la expresión condicional.
      : noBatteryFlow ? "NO_NET_FLOW" : "FLOW";
    // Incluir tanto eventos CONFIRMADOS como pendientes impide cambiar de
    // fuente para evadir la supervision de una muestra fuera de limites.
    // [Guía] Actualiza `batteryEmergency` con `confirmedDischarge || confirmedCharge || confirmedVoltage`.
    const bool batteryEmergency = confirmedDischarge || confirmedCharge || confirmedVoltage
      // [Guía] Añade una alternativa suficiente dentro de la condición compuesta.
      || confirmedBus || confirmedPvBus || q3CurrentPending_ || q3FeedbackPending
      // [Guía] Añade una alternativa suficiente dentro de la condición compuesta.
      || q3BusPending || pvBusPending;
    // [Guía] Comprueba `state_ != FAULT && state_ != STOPPED && !in.calibrating && !batteryEmergency`. Si se
    // [Guía] cumple, ejecuta el bloque asociado. Aquí intervienen estado operativo interno.
    if (state_ != FAULT && state_ != STOPPED && !in.calibrating && !batteryEmergency) {
      // V44 concentra TODOS los casos de bus cero despues de las protecciones.
      // La retirada con bus todavia positivo conserva la evidencia de fuente.
      // [Guía] Comprueba `(!batteryGood || noBatteryResponse || idleBattery) && !solarOnly`. Si se cumple,
      // [Guía] ejecuta el bloque asociado. Aquí intervienen validez del puerto de batería para esta decisión.
      if ((!batteryGood || noBatteryResponse || idleBattery) && !solarOnly
          // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen estado
          // [Guía] operativo interno y validez de la medición de bus.
          && state_ != WAIT && busGood) {
        // [Guía] Actualiza `batteryLinkConfirmed_` con `false; batteryLinkHold_.clear();
        // [Guía] batteryNoResponse_.clear()`. Este dato representa estimación confirmada de conexión de batería.
        batteryLinkConfirmed_ = false; batteryLinkHold_.clear(); batteryNoResponse_.clear();
        // [Guía] Borra la confirmación o acumulación en `batteryIdle_` para que no se arrastre al siguiente
        // [Guía] contexto.
        batteryIdle_.clear(); batteryQuiet_.clear();
        // [Guía] Pide apagar la rama indicada con duty cero. El sketch aplica físicamente la consigna.
        q2_ = q3_ = 0; // La rama sin respuesta no se mantiene conmutando.
        // [Guía] Actualiza `lastSourceEvent_` con `!batteryGood ? "BAT_FEEDBACK_LOST"`.
        lastSourceEvent_ = !batteryGood ? "BAT_FEEDBACK_LOST"
          // [Guía] Define una de las dos salidas de la expresión condicional.
          : idleBattery ? "M4_BAT_IDLE_PV_ONLY" : "BAT_NO_RESPONSE_PV_ONLY";
        // [Guía] Comprueba `solarAvailable(in) && in.pvConfigured`. Si se cumple, ejecuta el bloque asociado.
        if (solarAvailable(in) && in.pvConfigured) {
          // [Guía] Comprueba `q1_ >= Config::Q1_DUTY_MIN`. Si se cumple, ejecuta `enterM1(in)`. Aquí intervienen
          // [Guía] consigna de duty de Q1 (0 a 1).
          if (q1_ >= Config::Q1_DUTY_MIN) enterM1(in);
          // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
          else startSolar(in);
          // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
          reason_ = lastSourceEvent_;
          // [Guía] Actualiza `solarOnly` con `true`.
          solarOnly = true;
        // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
        } else { wait("WAIT_BAT_LOST"); solarOnly = false; }
      }
    }
    // Supervision a frecuencia de adquisicion; no esperar al periodo del PI.
    // M2 deliberadamente no regula bus. Cero VALIDO de Vbus no debe cancelar
    // la carga PV->bateria. Sobretension/sensores/corriente siguen prioritarios.
    // [Guía] Actualiza `zeroDuringControl` con `busGood && !(m2Path() && q3_ == 0)`.
    const bool zeroDuringControl = busGood && !(m2Path() && q3_ == 0)
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen estado operativo
      // [Guía] interno.
      && state_ != WAIT && state_ != STOPPED
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
      && in.busRaw <= 0;
    // [Guía] Comprueba `state_ != FAULT`. Si se cumple, ejecuta el bloque asociado. Aquí intervienen estado
    // [Guía] operativo interno.
    if (state_ != FAULT) {
      // No cancelar un exceso pendiente cambiando a WAIT por bus cero.
      // [Guía] Comprueba `confirmedDischarge || (zeroDuringControl && q3CurrentPending_)`. Si se cumple, ejecuta
      // [Guía] el bloque asociado.
      if (confirmedDischarge || (zeroDuringControl && q3CurrentPending_))
        // [Guía] Registra el motivo del fallo y retira las consignas de PWM para que el sketch las apague
        // [Guía] físicamente.
        fail("IBAT_DISCHARGE_LIMIT");
      // [Guía] Comprueba `confirmedCharge || (zeroDuringControl && q3FeedbackPending && chargeOver)`. Si se
      // [Guía] cumple, ejecuta el bloque asociado.
      else if (confirmedCharge || (zeroDuringControl && q3FeedbackPending && chargeOver))
        // [Guía] Registra el motivo del fallo y retira las consignas de PWM para que el sketch las apague
        // [Guía] físicamente.
        fail("IBAT_CHARGE_LIMIT");
      // [Guía] Comprueba `confirmedBus`. Si se cumple, ejecuta el bloque asociado.
      else if (confirmedBus)
        // [Guía] Registra el motivo del fallo y retira las consignas de PWM para que el sketch las apague
        // [Guía] físicamente.
        fail("BUS_TEST_OV");
      // [Guía] Comprueba `q1_ > 0 && (!in.pvConfigured || !isfinite(in.pvBusLimit)`. Si se cumple, ejecuta el
      // [Guía] bloque asociado. Aquí intervienen consigna de duty de Q1 (0 a 1).
      else if (q1_ > 0 && (!in.pvConfigured || !isfinite(in.pvBusLimit)
               // [Guía] Añade una alternativa suficiente dentro de la condición compuesta.
               || in.pvBusLimit <= Config::BUS_REFERENCE_V
               // [Guía] Añade una alternativa suficiente dentro de la condición compuesta.
               || in.pvBusLimit > Config::BAT_TEST_BUS_TRIP_V))
        // [Guía] Registra el motivo del fallo y retira las consignas de PWM para que el sketch las apague
        // [Guía] físicamente.
        fail("PV_CONFIG_LOST");
      // [Guía] Comprueba `confirmedPvBus`. Si se cumple, ejecuta el bloque asociado.
      else if (confirmedPvBus)
        // [Guía] Registra el motivo del fallo y retira las consignas de PWM para que el sketch las apague
        // [Guía] físicamente.
        fail("PV_BUS_LIMIT");
      // [Guía] Comprueba `zeroDuringControl && q3FeedbackPending && voltageOver`. Si se cumple, ejecuta el bloque
      // [Guía] asociado.
      else if (zeroDuringControl && q3FeedbackPending && voltageOver)
        // [Guía] Registra el motivo del fallo y retira las consignas de PWM para que el sketch las apague
        // [Guía] físicamente.
        fail("BAT_VOLTAGE_LIMIT");
      // [Guía] Comprueba `active() && (!busGood || (!solarOnly && !batteryGood))`. Si se cumple, ejecuta
      // [Guía] `fail("SENSOR_INVALID")`. Aquí intervienen validez de la medición de bus y validez del puerto de
      // [Guía] batería para esta decisión.
      else if (active() && (!busGood || (!solarOnly && !batteryGood))) fail("SENSOR_INVALID");
      // Sobretension confirmada en Q3 solo; subtension con filtro de permiso.
      // [Guía] Comprueba `active() && !solarOnly && (batteryVoltage_ < Config::BAT_TEST_MIN_V`. Si se cumple,
      // [Guía] ejecuta el bloque asociado. Aquí intervienen tensión de batería calificada.
      else if (active() && !solarOnly && (batteryVoltage_ < Config::BAT_TEST_MIN_V
               // [Guía] Añade una alternativa suficiente dentro de la condición compuesta.
               || confirmedVoltage)) fail("BAT_VOLTAGE_LIMIT");
      // [Guía] Comprueba `active() && !solarOnly && !in.socValid`. Si se cumple, ejecuta `fail("SOC_INVALID")`.
      else if (active() && !solarOnly && !in.socValid) fail("SOC_INVALID");
    }
    // [Guía] Comprueba `state_ != FAULT && zeroDuringControl`. Si se cumple, ejecuta el bloque asociado. Aquí
    // [Guía] intervienen estado operativo interno.
    if (state_ != FAULT && zeroDuringControl) {
      // V44: bus cero VALIDO deja de ser fallo enclavado en cualquier modo.
      // WAIT retira Q1/Q2/Q3 y MPPT inmediatamente, sin conservar el duty.
      // Borrar evidencia/ventanas obliga a detectar de nuevo la fuente y la
      // precarga. No confirmar conexion fisica por tension residual o I antigua.
      // Las protecciones anteriores conservan prioridad y FAULT no se rearma.
      // [Guía] Actualiza `batteryLinkConfirmed_` con `false; batteryLinkHold_.clear()`. Este dato representa
      // [Guía] estimación confirmada de conexión de batería.
      batteryLinkConfirmed_ = false; batteryLinkHold_.clear();
      // [Guía] Borra la confirmación o acumulación en `batteryNoResponse_` para que no se arrastre al siguiente
      // [Guía] contexto.
      batteryNoResponse_.clear(); batteryQuiet_.clear(); batteryWindow_.reset();
      // [Guía] Actualiza `batteryCurrentReady_` con `false; bootPv_.clear()`.
      batteryCurrentReady_ = false; bootPv_.clear();
      // [Guía] Invoca `resetM1BatteryDetection` con los argumentos de esta expresión.
      resetM1BatteryDetection();
      // [Guía] Actualiza `lastSourceEvent_` con `"BUS_ZERO_WAIT"`.
      lastSourceEvent_ = "BUS_ZERO_WAIT";
      // [Guía] Lleva las consignas a cero, registra el motivo y limpia confirmaciones previas para esperar una
      // [Guía] fuente.
      wait("WAIT_BUS_ZERO");
      // [Guía] Actualiza `haveTime_` con `false`.
      haveTime_ = false;
      // [Guía] Finaliza aquí la función y devuelve el control al llamador.
      return;
    }
    // La recuperacion de medidas no rearma sola el fallo: solo habilita que
    // EMS_RESET se acepte tras1 s continuo de corriente<50 mA y bus<21 V.
    // [Guía] Comprueba `state_ == FAULT`. Si se cumple, ejecuta el bloque asociado. Aquí intervienen estado
    // [Guía] operativo interno.
    if (state_ == FAULT) {
      // [Guía] Actualiza `canReset_` con `resetSafe_.held(batteryGood && busGood`.
      canReset_ = resetSafe_.held(batteryGood && busGood
        // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
        && fabsf(in.batteryIRaw) < 0.05f && in.busRaw < 21.0f,
        // [Guía] Completa la declaración o expresión con `in.nowUs, 1000000)`. Aquí intervienen reloj de la
        // [Guía] muestra en µs.
        in.nowUs, 1000000);
      // [Guía] Finaliza aquí la función y devuelve el control al llamador.
      return;
    }
    // [Guía] Comprueba `state_ == STOPPED`. Si se cumple, termina la función en esta rama. Aquí intervienen
    // [Guía] estado operativo interno.
    if (state_ == STOPPED) return;
    // [Guía] Comprueba `qualifyConnection && connectionPending_`. Si se cumple, ejecuta el bloque asociado.
    if (qualifyConnection && connectionPending_) {
      // No generar ni un pulso hasta recuperar realimentacion y margen de corriente.
      // [Guía] Actualiza `recovered` con `batteryGood && busGood && in.socValid`.
      const bool recovered = batteryGood && busGood && in.socValid
        // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
        && batteryVoltageReady_ && in.batteryV < Config::BAT_TEST_MAX_V
        // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
        && in.busRaw >= Config::BAT_BUS_START_MIN_V
        // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
        && in.busRaw < Config::BAT_TEST_BUS_TRIP_V
        // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
        && in.batteryIRaw < Config::BAT_DISCHARGE_SOFT_A
        // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
        && in.batteryIRaw > -Config::BAT_CHARGE_SOFT_A
        // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
        && in.batteryI < Config::BAT_DISCHARGE_SOFT_A
        // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
        && in.batteryI > -Config::BAT_CHARGE_SOFT_A;
      // [Guía] Comprueba `!connectionRecovery_.held(recovered, in.nowUs, Config::BAT_CONNECTION_RECOVER_US)`. Si
      // [Guía] se cumple, ejecuta el bloque asociado. Aquí intervienen reloj de la muestra en µs.
      if (!connectionRecovery_.held(recovered, in.nowUs, Config::BAT_CONNECTION_RECOVER_US)) {
        // [Guía] Lleva las consignas a cero, registra el motivo y limpia confirmaciones previas para esperar una
        // [Guía] fuente.
        wait(dischargeOver ? "WAIT_CONNECTION_TRANSIENT" : "WAIT_CONNECTION_STABLE");
        // [Guía] Finaliza aquí la función y devuelve el control al llamador.
        return;
      }
      // [Guía] Actualiza `connectionPending_` con `false`.
      connectionPending_ = false;
      // [Guía] Borra la confirmación o acumulación en `connectionRecovery_` para que no se arrastre al siguiente
      // [Guía] contexto.
      connectionRecovery_.clear();
      // [Guía] Actualiza `haveTime_` con `false`.
      haveTime_ = false;
    }
    // Esperar la primera fuente. Vbat OCV no informa el interruptor: bateria
    // requiere descarga confirmada; PV se reconoce por tension O corriente.
    // [Guía] Comprueba `state_ == WAIT && !connectionPending_`. Si se cumple, ejecuta el bloque asociado. Aquí
    // [Guía] intervienen estado operativo interno.
    if (state_ == WAIT && !connectionPending_) {
      // [Guía] Comprueba `gap > Config::BAT_CONNECTION_MAX_GAP_US`. Si se cumple, ejecuta `bootPv_.clear()`. Aquí
      // [Guía] intervienen separación temporal entre muestras.
      if (gap > Config::BAT_CONNECTION_MAX_GAP_US) bootPv_.clear();
      // [Guía] Actualiza `pvCandidate` con `solarAvailable(in)`.
      const bool pvCandidate = solarAvailable(in);
      // [Guía] Actualiza `batteryFlow` con `batteryGood && batteryLinkConfirmed_`.
      const bool batteryFlow = batteryGood && batteryLinkConfirmed_
        // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen corriente de
        // [Guía] batería filtrada para supervisión.
        && batteryCurrentSupervisor_ > Config::BAT_CONNECT_CURRENT_A;
      // [Guía] Actualiza `solarBoot` con `bootPv_.held(pvCandidate, in.nowUs, Config::BAT_PV_HOLD_US)`.
      const bool solarBoot = bootPv_.held(pvCandidate, in.nowUs, Config::BAT_PV_HOLD_US);
      // [Guía] Actualiza `busReady` con `busGood && in.busRaw >= Config::BAT_BUS_START_MIN_V`.
      const bool busReady = busGood && in.busRaw >= Config::BAT_BUS_START_MIN_V;
      // Si ambas se confirman juntas, usar la que se detecto antes; empate PV.
      // [Guía] Actualiza `batteryPending` con `batteryGood && batteryLinkHold_.running`.
      const bool batteryPending = batteryGood && batteryLinkHold_.running
        // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen corriente de
        // [Guía] batería filtrada para supervisión.
        && batteryCurrentSupervisor_ > Config::BAT_CONNECT_CURRENT_A;
      // [Guía] Actualiza `pvFirst` con `!batteryPending`.
      const bool pvFirst = !batteryPending
        // [Guía] Añade una alternativa suficiente dentro de la condición compuesta.
        || int32_t(bootPv_.start - batteryLinkHold_.start) <= 0;
      // [Guía] Comprueba `solarBoot && pvFirst && busReady && in.pvConfigured && in.busRaw < in.pvBusLimit`. Si
      // [Guía] se cumple, ejecuta el bloque asociado.
      if (solarBoot && pvFirst && busReady && in.pvConfigured && in.busRaw < in.pvBusLimit) {
        // [Guía] Inicia el arranque con PV cuando se cumplen sus condiciones de disponibilidad.
        startSolar(in);
        // [Guía] Finaliza aquí la función y devuelve el control al llamador.
        return;
      }
      // [Guía] Comprueba `batteryFlow && busReady && in.socValid && in.q3Configured`. Si se cumple, ejecuta el
      // [Guía] bloque asociado.
      if (batteryFlow && busReady && in.socValid && in.q3Configured
          // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen estado de
          // [Guía] carga estimado.
          && in.soc > Config::BAT_DISCHARGE_MIN_SOC && batteryVoltageReady_
          // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
          && in.batteryI < Config::BAT_DISCHARGE_SOFT_A) {
        // [Guía] Prepara la rampa del PI de bus por Q3.
        enterStart3(in); return;
      }
      // [Guía] Comprueba `!busGood`. Si se cumple, ejecuta `reason_ = "WAIT_BUS_SENSOR"`. Aquí intervienen
      // [Guía] validez de la medición de bus.
      if (!busGood) reason_ = "WAIT_BUS_SENSOR";
      // [Guía] Comprueba `pvCandidate && pvFirst`. Si se cumple, ejecuta el bloque asociado.
      else if (pvCandidate && pvFirst) {
        // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
        reason_ = !in.pvConfigured ? "PV_CONFIG_REQUIRED"
          // [Guía] Define una de las dos salidas de la expresión condicional.
          : !busReady ? "PV_WAIT_PRECHARGE" : "BOOT_WAIT_PV_CONFIRM";
      // [Guía] Comprueba `batteryFlow`. Si se cumple, ejecuta el bloque asociado.
      } else if (batteryFlow) {
        // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
        reason_ = !in.q3Configured ? "CONFIG_Q3_REQUIRED"
          // [Guía] Define una de las dos salidas de la expresión condicional.
          : !in.socValid ? "WAIT_SOC"
          // [Guía] Define una de las dos salidas de la expresión condicional.
          : !batteryVoltageReady_ ? "WAIT_BAT_VOLTAGE"
          // [Guía] Define una de las dos salidas de la expresión condicional.
          : !busReady ? "WAIT_BUS_PRECHARGE"
          // [Guía] Define una de las dos salidas de la expresión condicional.
          : in.batteryI >= Config::BAT_DISCHARGE_SOFT_A ? "PASSIVE_CURRENT_TOO_HIGH"
          // [Guía] Define una de las dos salidas de la expresión condicional. Aquí intervienen estado de carga
          // [Guía] estimado.
          : in.soc <= Config::BAT_DISCHARGE_MIN_SOC ? "WAIT_DISCHARGE_PERMISSION"
          // [Guía] Define una de las dos salidas de la expresión condicional.
          : "BOOT_WAIT_BAT_CONFIRM";
      // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
      } else reason_ = batteryGood && batteryCurrentSupervisor_ > Config::BAT_CONNECT_CURRENT_A
        // [Guía] Define una de las dos salidas de la expresión condicional.
        ? "WAIT_BAT_LINK_CONFIRM" : "WAIT_SOURCE";
      // [Guía] Actualiza `haveTime_` con `false`.
      haveTime_ = false;
      // [Guía] Finaliza aquí la función y devuelve el control al llamador.
      return;
    }
    // [Guía] Comprueba `(!batteryGood && !solarOnly) || !busGood`. Si se cumple, ejecuta el bloque asociado. Aquí
    // [Guía] intervienen validez del puerto de batería para esta decisión y validez de la medición de bus.
    if ((!batteryGood && !solarOnly) || !busGood) { wait("WAIT_SENSORS"); return; }
    // [Guía] Comprueba `!solarOnly && !in.socValid`. Si se cumple, ejecuta el bloque asociado.
    if (!solarOnly && !in.socValid) { wait("WAIT_SOC"); return; }
    // [Guía] Comprueba `!solarOnly && (!m2Path() || q3_ > 0)`. Si se cumple, ejecuta el bloque asociado. Aquí
    // [Guía] intervienen consigna de duty de Q3 (0 a 1).
    if (!solarOnly && (!m2Path() || q3_ > 0)
        // [Guía] Agrupa condiciones alternativas y conjuntas en la prueba en curso.
        && (!in.q3Configured || !validGains(settings_.kp3, settings_.ki3, settings_.action3))) {
      // [Guía] Comprueba `active()`. Si se cumple, ejecuta `fail("Q3_CONFIG_LOST"); else
      // [Guía] wait("CONFIG_Q3_REQUIRED")`.
      if (active()) fail("Q3_CONFIG_LOST"); else wait("CONFIG_Q3_REQUIRED");
      // [Guía] Finaliza aquí la función y devuelve el control al llamador.
      return;
    }
    // [Guía] Comprueba `!solarOnly && (!batteryVoltageReady_ || in.batteryV >= Config::BAT_TEST_MAX_V)`. Si se
    // [Guía] cumple, ejecuta el bloque asociado.
    if (!solarOnly && (!batteryVoltageReady_ || in.batteryV >= Config::BAT_TEST_MAX_V)
        // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
        && !(qualifyVoltage && voltageOver && !confirmedVoltage)) {
      // [Guía] Lleva las consignas a cero, registra el motivo y limpia confirmaciones previas para esperar una
      // [Guía] fuente.
      wait("WAIT_BAT_VOLTAGE"); return;
    }
    // [Guía] Comprueba `(state_ == WAIT || q3_ > 0) && in.soc <= Config::BAT_DISCHARGE_MIN_SOC`. Si se cumple,
    // [Guía] ejecuta el bloque asociado. Aquí intervienen estado operativo interno y consigna de duty de Q3 (0 a
    // [Guía] 1).
    if ((state_ == WAIT || q3_ > 0) && in.soc <= Config::BAT_DISCHARGE_MIN_SOC) {
      // [Guía] Comprueba `active()`. Si se cumple, ejecuta `fail("SOC_DISCHARGE_MIN"); else
      // [Guía] wait("WAIT_DISCHARGE_PERMISSION")`.
      if (active()) fail("SOC_DISCHARGE_MIN"); else wait("WAIT_DISCHARGE_PERMISSION");
      // [Guía] Finaliza aquí la función y devuelve el control al llamador.
      return;
    }
    // [Guía] Comprueba `state_ == WAIT && in.busRaw < Config::BAT_BUS_START_MIN_V`. Si se cumple, ejecuta el
    // [Guía] bloque asociado. Aquí intervienen estado operativo interno.
    if (state_ == WAIT && in.busRaw < Config::BAT_BUS_START_MIN_V) {
      // [Guía] Lleva las consignas a cero, registra el motivo y limpia confirmaciones previas para esperar una
      // [Guía] fuente.
      wait("WAIT_BUS_PRECHARGE");
      // [Guía] Finaliza aquí la función y devuelve el control al llamador.
      return;
    }
    // Iniciar el calendario desde una muestra valida evita integrar el tiempo
    // pasado en WAIT. Los casts int32_t permiten comparar fechas con overflow.
    // [Guía] Comprueba `!haveTime_`. Si se cumple, ejecuta el bloque asociado.
    if (!haveTime_) {
      // [Guía] Actualiza `lastUs_` con `lastControlUs_ = in.nowUs`.
      lastUs_ = lastControlUs_ = in.nowUs;
      // [Guía] Actualiza `nextControlUs_` con `in.nowUs + Config::CONTROL_US`.
      nextControlUs_ = in.nowUs + Config::CONTROL_US;
      // [Guía] Actualiza `haveTime_` con `true`.
      haveTime_ = true;
    }
    // [Guía] Actualiza `sampleGap` con `in.nowUs - lastUs_`.
    const uint32_t sampleGap = in.nowUs - lastUs_;
    // [Guía] Actualiza `lastUs_` con `in.nowUs`.
    lastUs_ = in.nowUs;
    // [Guía] Comprueba `sampleGap >= Config::CONTROL_TIMEOUT_US`. Si se cumple, ejecuta el bloque asociado.
    if (sampleGap >= Config::CONTROL_TIMEOUT_US) {
      // [Guía] Comprueba `active()`. Si se cumple, ejecuta el bloque asociado.
      if (active()) { fail("CONTROL_TIMEOUT"); return; }
      // [Guía] Reinicia las ventanas de confirmación al abandonar su contexto operativo.
      clearHolds();
    }
    // [Guía] Comprueba `q3CurrentPending_ || q3FeedbackPending || q3BusPending || pvBusPending`. Si se cumple,
    // [Guía] ejecuta el bloque asociado.
    if (q3CurrentPending_ || q3FeedbackPending || q3BusPending || pvBusPending) {
      // Congelar salida, referencia e integral: no aumentar duty durante el evento.
      // Los permisos y las protecciones anteriores siguen teniendo prioridad.
      // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
      reason_ = pvBusPending ? "PV_BUS_CONFIRM" : q3BusPending ? "Q3_BUS_CONFIRM"
        // [Guía] Define una de las dos salidas de la expresión condicional.
        : (voltageOver ? "Q3_VOLTAGE_CONFIRM" : "Q3_CURRENT_CONFIRM");
      // [Guía] Actualiza `lastControlUs_` con `in.nowUs`.
      lastControlUs_ = in.nowUs;
      // [Guía] Actualiza `nextControlUs_` con `in.nowUs + Config::CONTROL_US`.
      nextControlUs_ = in.nowUs + Config::CONTROL_US;
      // [Guía] Finaliza aquí la función y devuelve el control al llamador.
      return;
    }
    // [Guía] Comprueba `int32_t(in.nowUs - nextControlUs_) < 0`. Si se cumple, termina la función en esta rama.
    // [Guía] Aquí intervienen reloj de la muestra en µs.
    if (int32_t(in.nowUs - nextControlUs_) < 0) return;
    // Conservar fase del calendario, como el control Q1 existente; no acumular
    // retraso de un barrido extra cada vez que el intervalo queda cerca de 1 ms.
    // [Guía] Actualiza `skipped` con `(in.nowUs - nextControlUs_) / Config::CONTROL_US`.
    const uint32_t skipped = (in.nowUs - nextControlUs_) / Config::CONTROL_US;
    // [Guía] Acumula en `nextControlUs_` mediante `(skipped + 1) * Config::CONTROL_US`.
    nextControlUs_ += (skipped + 1) * Config::CONTROL_US;
    // [Guía] Actualiza `elapsed` con `in.nowUs - lastControlUs_`. Este dato representa tiempo transcurrido.
    const uint32_t elapsed = in.nowUs - lastControlUs_;
    // [Guía] Actualiza `lastControlUs_` con `in.nowUs`.
    lastControlUs_ = in.nowUs;
    // [Guía] Comprueba `skipped || elapsed >= 2 * Config::CONTROL_US`. Si se cumple, ejecuta el bloque asociado.
    // [Guía] Aquí intervienen tiempo transcurrido.
    if (skipped || elapsed >= 2 * Config::CONTROL_US) {
      // [Guía] Acumula en `missedUpdates_` mediante `skipped ? skipped : elapsed / Config::CONTROL_US - 1`. Este
      // [Guía] dato representa contador de periodos de control vencidos.
      missedUpdates_ += skipped ? skipped : elapsed / Config::CONTROL_US - 1;
      // No contar tiempo sin ejecutar el control como4ms de valle continuo
      // ni como1s de regulacion sana para renovar presupuesto de recuperacion.
      // [Guía] Borra la confirmación o acumulación en `m1InputLow_` para que no se arrastre al siguiente
      // [Guía] contexto.
      m1InputLow_.clear(); m1Healthy_.clear();
      // Una pausa breve de telemetria no debe impedir siempre la confirmacion
      // de bus/PV. Un hueco largo si descarta esos permisos temporizados.
      // [Guía] Comprueba `sampleGap > Config::BAT_CONNECTION_MAX_GAP_US`. Si se cumple, ejecuta `clearHolds()`.
      if (sampleGap > Config::BAT_CONNECTION_MAX_GAP_US) clearHolds();
      // [Guía] Finaliza aquí la función y devuelve el control al llamador.
      return;
    }
    // [Guía] Suma una unidad a `updates_` para contabilizar el evento de esta rama.
    ++updates_;
    // [Guía] Actualiza `dt` con `elapsed * 0.000001f`. Este dato representa intervalo efectivo del control en s.
    const float dt = elapsed * 0.000001f;
    // [Guía] Comprueba `m2Cooldown_ && uint32_t(in.nowUs - m2CooldownSince_) >= Config::M2_REENTRY_COOLDOWN_US`.
    // [Guía] Si se cumple, ejecuta el bloque asociado. Aquí intervienen reloj de la muestra en µs.
    if (m2Cooldown_ && uint32_t(in.nowUs - m2CooldownSince_) >= Config::M2_REENTRY_COOLDOWN_US)
      // [Guía] Actualiza `m2Cooldown_` con `false`.
      m2Cooldown_ = false;
    // [Guía] Comprueba `mpptInitialized_ && state_ != M4 && state_ != M3 && state_ != START4`. Si se cumple,
    // [Guía] ejecuta el bloque asociado. Aquí intervienen estado operativo interno.
    if (mpptInitialized_ && state_ != M4 && state_ != M3 && state_ != START4)
      // [Guía] Suspende temporalmente el avance de buscador P&O en la coordenada de duty. Aquí intervienen
      // [Guía] buscador P&O en la coordenada de duty y reloj de la muestra en µs.
      mppt_.pause(in.nowUs);
    // [Guía] Actualiza `pvOn` con `pvPresent_.held(solarAvailable(in), in.nowUs, Config::BAT_PV_HOLD_US)`. Este
    // [Guía] dato representa disponibilidad confirmada de PV.
    const bool pvOn = pvPresent_.held(solarAvailable(in), in.nowUs, Config::BAT_PV_HOLD_US);
    // [Guía] Actualiza `pvOff` con `pvAbsent_.held(!solarRetained(in), in.nowUs, Config::BAT_PV_HOLD_US)`.
    const bool pvOff = pvAbsent_.held(!solarRetained(in), in.nowUs, Config::BAT_PV_HOLD_US);
    // Ante medida PV invalida retirar esa rama; la bateria puede seguir util.
    // [Guía] Actualiza `pvLost` con `pvOff || !in.pvValid`. Este dato representa pérdida confirmada de PV.
    const bool pvLost = pvOff || !in.pvValid;
    // [Guía] Comprueba `pvLost`. Si se cumple, ejecuta el bloque asociado. Aquí intervienen pérdida confirmada de
    // [Guía] PV.
    if (pvLost) {
      // Una retirada Q2 no debe dejar Q1 conmutando sobre una fuente perdida.
      // [Guía] Pide apagar la rama indicada con duty cero. El sketch aplica físicamente la consigna.
      q1_ = 0; mpptRunning_ = mpptInitialized_ = false;
    }
    // Permisos NO son ordenes PWM: ademas del SOC se exigen realimentacion,
    // configuracion, enlace estimado y tension. Carga y descarga son distintos.
    // [Guía] Empieza a calificar el permiso de descarga. Exige conexión estimada de batería y validez del puerto
    // [Guía] antes de evaluar los límites de SOC y tensión.
    const bool dischargeAllowed = batteryLinkConfirmed_ && batteryGood
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
      && batteryVoltageReady_ && in.socValid && in.q3Configured
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen estado de carga
      // [Guía] estimado.
      && in.soc > Config::BAT_DISCHARGE_MIN_SOC;
    // [Guía] Empieza a calificar el permiso de carga. Además de evidencia de batería, las líneas siguientes
    // [Guía] exigen que SOC y tensión permitan cargar.
    const bool chargeAllowed = batteryLinkConfirmed_ && batteryGood
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
      && batteryVoltageReady_ && in.socValid && in.q2Configured
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
      && validGains(settings_.kp2, settings_.ki2, settings_.action2)
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen estado de carga
      // [Guía] estimado.
      && in.soc < Config::BAT_CHARGE_EXIT_SOC
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen tensión de
      // [Guía] batería calificada.
      && batteryVoltage_ < Config::BAT_CHARGE_MAX_V;
    // Vpv alta sin corriente no demuestra aporte solar ni potencia de carga.
    // [Guía] Actualiza `pvChargeMargin` con `pvSupervisorReady_ && pvOn`.
    const bool pvChargeMargin = pvSupervisorReady_ && pvOn
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen tensión del PV
      // [Guía] en V y tensión de PV filtrada para supervisión.
      && in.pv >= chargePvMinimum() && pvSupervisor_ >= chargePvMinimum()
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
      && in.pvI > Config::BAT_PV_ON_CURRENT_A;
    // La ausencia de carga se confirma por Ibus, NO por Vpv>=Vbat+2V ni
    // por una corriente de carga previa. Aquellas condiciones mantenian Q1
    // elevando un bus descargado en el registro V63. M2 regula Ibat sin
    // margen relativo PV/bateria y conserva los permisos reales de carga.
    // Schmitt: iniciar <=80mA; mantener la evidencia en la banda80..150mA;
    // descartar al alcanzar150mA, con sensor invalido o al cambiar de contexto.
    // [Guía] Actualiza `m2Context` con `settings_.m2Enabled && !m2Cooldown_ && (state_ == M3 || state_ == M4)`.
    const bool m2Context = settings_.m2Enabled && !m2Cooldown_ && (state_ == M3 || state_ == M4);
    // [Guía] Actualiza `m2Ready` con `false`. Este dato representa condición de entrada a M2 ya calificada.
    bool m2Ready = false;
    // [Guía] Comprueba `!m2Context`. Si se cumple, ejecuta `clearM2Idle(!settings_.m2Enabled ? "DISABLED" :
    // [Guía] m2Cooldown_ ? "COOLDOWN" : "INACTIVE")`.
    if (!m2Context) clearM2Idle(!settings_.m2Enabled ? "DISABLED" : m2Cooldown_ ? "COOLDOWN" : "INACTIVE");
    // [Guía] Comprueba `!in.busCurrentValid || !isfinite(in.busIRaw)`. Si se cumple, ejecuta
    // [Guía] `clearM2Idle("IBUS_INVALID")`.
    else if (!in.busCurrentValid || !isfinite(in.busIRaw)) clearM2Idle("IBUS_INVALID");
    // [Guía] Comprueba `!busLoadWindow_.ready || !busDemandHistory_.ready()`. Si se cumple, ejecuta
    // [Guía] `clearM2Idle("IBUS_WINDOW")`.
    else if (!busLoadWindow_.ready || !busDemandHistory_.ready()) clearM2Idle("IBUS_WINDOW");
    // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
    else {
      // [Guía] Actualiza `demand` con `fabsf(m2BusDemand(in))`.
      const float demand = fabsf(m2BusDemand(in));
      // [Guía] Actualiza `guardedDemand` con `fabsf(m2ExternalDemand(busDemandHistory_.mean(), in))`.
      const float guardedDemand = fabsf(m2ExternalDemand(busDemandHistory_.mean(), in));
      // V66 entraba por un solo bloque5ms bajo mientras la carga seguia
      // consumiendo0.5..0.8A. Contrastar con40ms moviles y15ms coherentes.
      // [Guía] Comprueba `demand >= Config::M2_BUS_WAKE_A || guardedDemand >= Config::M2_BUS_WAKE_A`. Si se
      // [Guía] cumple, ejecuta el bloque asociado.
      if (demand >= Config::M2_BUS_WAKE_A || guardedDemand >= Config::M2_BUS_WAKE_A)
        // [Guía] Reinicia la confirmación de ausencia de carga externa y conserva la causa de reinicio para
        // [Guía] diagnóstico.
        clearM2Idle("BUS_DEMAND");
      // [Guía] Comprueba `demand > Config::M2_BUS_IDLE_A || guardedDemand > Config::M2_BUS_IDLE_A`. Si se cumple,
      // [Guía] ejecuta el bloque asociado.
      else if (demand > Config::M2_BUS_IDLE_A || guardedDemand > Config::M2_BUS_IDLE_A)
        // [Guía] Reinicia la confirmación de ausencia de carga externa y conserva la causa de reinicio para
        // [Guía] diagnóstico.
        clearM2Idle("WAIT_BUS_IDLE");
      // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
      else {
        // [Guía] Comprueba `demand <= Config::M2_BUS_IDLE_A`. Si se cumple, ejecuta `m2IdleLatched_ = true`.
        if (demand <= Config::M2_BUS_IDLE_A) m2IdleLatched_ = true;
        // [Guía] Actualiza `idleConfirmed` con `m2Idle_.held(m2IdleLatched_, in.nowUs, Config::M2_BUS_IDLE_US)`.
        const bool idleConfirmed = m2Idle_.held(m2IdleLatched_, in.nowUs, Config::M2_BUS_IDLE_US);
        // [Guía] Actualiza `m2Block_` con `!m2IdleLatched_ ? "WAIT_BUS_IDLE" : !idleConfirmed ? "CONFIRM_IBUS"`.
        m2Block_ = !m2IdleLatched_ ? "WAIT_BUS_IDLE" : !idleConfirmed ? "CONFIRM_IBUS"
          // [Guía] Define una de las dos salidas de la expresión condicional. Aquí intervienen estimación
          // [Guía] confirmada de conexión de batería y validez del puerto de batería para esta decisión.
          : !batteryLinkConfirmed_ ? "BAT_LINK" : !batteryGood ? "BAT_MEASURE"
          // [Guía] Define una de las dos salidas de la expresión condicional.
          : !batteryVoltageReady_ ? "BAT_VOLTAGE" : !in.socValid ? "SOC_INVALID"
          // [Guía] Define una de las dos salidas de la expresión condicional.
          : !in.q2Configured ? "Q2_CONFIG"
          // [Guía] Define una de las dos salidas de la expresión condicional.
          : !validGains(settings_.kp2, settings_.ki2, settings_.action2) ? "Q2_GAINS"
          // [Guía] Define una de las dos salidas de la expresión condicional. Aquí intervienen estado de carga
          // [Guía] estimado.
          : in.soc >= Config::BAT_CHARGE_EXIT_SOC ? "SOC_CEILING"
          // [Guía] Define una de las dos salidas de la expresión condicional. Aquí intervienen tensión de batería
          // [Guía] calificada.
          : batteryVoltage_ >= Config::BAT_CHARGE_MAX_V ? "VBAT_CEILING"
          // [Guía] Define una de las dos salidas de la expresión condicional. Aquí intervienen disponibilidad
          // [Guía] confirmada de PV.
          : !pvOn || !solarAvailable(in) ? "PV_SOURCE" : "READY";
        // [Guía] Autoriza la entrada a M2 únicamente con ausencia confirmada de carga externa, permiso de carga y
        // [Guía] una fuente PV utilizable.
        m2Ready = idleConfirmed && chargeAllowed && pvOn && solarAvailable(in);
      }
    }
    // [Guía] Actualiza `preparedPv` con `pvSupervisorReady_ && pvOn`.
    const bool preparedPv = pvSupervisorReady_ && pvOn
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen tensión de PV
      // [Guía] filtrada para supervisión.
      && pvSupervisor_ >= pvAssistTarget() && in.pvI > Config::BAT_PV_ON_CURRENT_A;
    // [Guía] Actualiza `chargeRecovered` con `(state_ == M4 || (state_ == M3 && chargeFlowSeen_`.
    const bool chargeRecovered = (state_ == M4 || (state_ == M3 && chargeFlowSeen_
        // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen corriente de
        // [Guía] batería filtrada para supervisión.
        && batteryCurrentSupervisor_ < idleCurrent - Config::BAT_ZERO_CURRENT_A
        // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen tensión de PV
        // [Guía] filtrada para supervisión.
        && pvSupervisor_ >= chargePvMinimum()))
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen tensión de bus
      // [Guía] filtrada para supervisión.
      && fabsf(busSupervisor_ - Config::BUS_REFERENCE_V) <= 0.30f;
    // [Guía] Comprueba `chargeHealthy_.held(chargeRecovered, in.nowUs, 500000) && !pvAssistActive()`. Si se
    // [Guía] cumple, ejecuta el bloque asociado. Aquí intervienen reloj de la muestra en µs.
    if (chargeHealthy_.held(chargeRecovered, in.nowUs, 500000) && !pvAssistActive()) {
      // [Guía] Actualiza `chargeAttempts_` con `pvAssistStarts_ = 0; pvAssistAttempted_ = false`.
      chargeAttempts_ = pvAssistStarts_ = 0; pvAssistAttempted_ = false;
      // [Guía] Actualiza `chargeRetryReferenceReady_` con `false; chargeRetry_.clear()`.
      chargeRetryReferenceReady_ = false; chargeRetry_.clear();
    }
    // El margen de entrada no se reaplica como salida de M3: cargar puede
    // reducir Vpv. En M3 exigir deficit real del bus para pedir soporte Q3.
    // [Guía] Actualiza `lowChargeMargin` con `pvSupervisorReady_`.
    const bool lowChargeMargin = pvSupervisorReady_
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen tensión del PV
      // [Guía] en V y tensión de batería calificada.
      && in.pv < batteryVoltage_ + Config::EMS_CHARGE_EXIT_MARGIN_V
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen tensión de PV
      // [Guía] filtrada para supervisión y tensión de batería calificada.
      && pvSupervisor_ < batteryVoltage_ + Config::EMS_CHARGE_EXIT_MARGIN_V;
    // [Guía] Actualiza `marginNeedsSupport` con `lowChargeMargin && state_ != M3`.
    const bool marginNeedsSupport = lowChargeMargin && state_ != M3;
    // Un deficit real del bus conserva prioridad aunque aun haya carga.
    // [Guía] Actualiza `recoverSource` con `deficitHold_.held(`.
    const bool recoverSource = deficitHold_.held(
      // [Guía] Completa la declaración o expresión con `busSupervisor_ < Config::BUS_REFERENCE_V - 0.75f`. Aquí
      // [Guía] intervienen tensión de bus filtrada para supervisión.
      busSupervisor_ < Config::BUS_REFERENCE_V - 0.75f
        // [Guía] Añade una alternativa suficiente dentro de la condición compuesta.
        || marginNeedsSupport,
      // [Guía] Completa la declaración o expresión con `in.nowUs, Config::EMS_DEFICIT_HOLD_US)`. Aquí intervienen
      // [Guía] reloj de la muestra en µs.
      in.nowUs, Config::EMS_DEFICIT_HOLD_US);
    // [Guía] Comprueba `!busRecovery_`. Si se cumple, ejecuta el bloque asociado. Aquí intervienen indicación de
    // [Guía] que se requiere recuperar el bus.
    if (!busRecovery_) {
      // [Guía] Borra la confirmación o acumulación en `busRecoverOff_` para que no se arrastre al siguiente
      // [Guía] contexto.
      busRecoverOff_.clear();
      // [Guía] Actualiza `busRecovery_` con `busRecoverOn_.held(busSupervisor_ >= Config::EMS_BUS_RECOVER_ON_V`.
      // [Guía] Este dato representa indicación de que se requiere recuperar el bus.
      busRecovery_ = busRecoverOn_.held(busSupervisor_ >= Config::EMS_BUS_RECOVER_ON_V,
        // [Guía] Completa la declaración o expresión con `in.nowUs, Config::EMS_BUS_RECOVER_ON_US)`. Aquí
        // [Guía] intervienen reloj de la muestra en µs.
        in.nowUs, Config::EMS_BUS_RECOVER_ON_US);
    // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
    } else {
      // [Guía] Borra la confirmación o acumulación en `busRecoverOn_` para que no se arrastre al siguiente
      // [Guía] contexto.
      busRecoverOn_.clear();
      // [Guía] Comprueba `busRecoverOff_.held(busSupervisor_ <= Config::EMS_BUS_RECOVER_OFF_V,`. Si se cumple,
      // [Guía] ejecuta el bloque asociado. Aquí intervienen tensión de bus filtrada para supervisión.
      if (busRecoverOff_.held(busSupervisor_ <= Config::EMS_BUS_RECOVER_OFF_V,
          // [Guía] Completa la declaración o expresión con `in.nowUs, Config::EMS_BUS_RECOVER_OFF_US))
          // [Guía] busRecovery_ = false`. Aquí intervienen reloj de la muestra en µs y indicación de que se
          // [Guía] requiere recuperar el bus.
          in.nowUs, Config::EMS_BUS_RECOVER_OFF_US)) busRecovery_ = false;
    }
    // [Guía] Comprueba `q1_ > 0 && (!isfinite(in.pvRaw) || !isfinite(in.pvI))`. Si se cumple, ejecuta el bloque
    // [Guía] asociado. Aquí intervienen consigna de duty de Q1 (0 a 1).
    if (q1_ > 0 && (!isfinite(in.pvRaw) || !isfinite(in.pvI))) {
      // [Guía] Registra el motivo del fallo y retira las consignas de PWM para que el sketch las apague
      // [Guía] físicamente.
      fail("PV_SENSOR_INVALID"); return;
    }

    // Una sola rama por paso. break devuelve el control a la comprobacion final;
    // return anterior conserva la salida o para, segun el supervisor que actuo.
    // [Guía] Elige la rama correspondiente al valor de `state_`.
    switch (state_) {
      // Arranque solar: elevar Q1 gradualmente antes de entregar el bus al PI1.
      // [Guía] Selecciona el tratamiento previsto para `START1` dentro del selector.
      case START1:
        // [Guía] Comprueba `pvLost`. Si se cumple, sale de esta rama del selector tras realizar la acción
        // [Guía] indicada. Aquí intervienen pérdida confirmada de PV.
        if (pvLost) { wait("WAIT_PV_SOURCE"); break; }
        // [Guía] Actualiza `q1_` con `approach(q1_, Config::Q1_DUTY_MIN, Config::Q1_TEST_SLEW_PER_SECOND * dt)`.
        // [Guía] Este dato representa consigna de duty de Q1 (0 a 1).
        q1_ = approach(q1_, Config::Q1_DUTY_MIN, Config::Q1_TEST_SLEW_PER_SECOND * dt);
        // [Guía] Comprueba `q1_ >= Config::Q1_DUTY_MIN`. Si se cumple, ejecuta el bloque asociado. Aquí
        // [Guía] intervienen consigna de duty de Q1 (0 a 1).
        if (q1_ >= Config::Q1_DUTY_MIN) {
          // [Guía] Actualiza `reference_` con `fminf(in.bus, Config::BUS_REFERENCE_V)`. Este dato representa
          // [Guía] referencia del regulador.
          reference_ = fminf(in.bus, Config::BUS_REFERENCE_V);
          // [Guía] Prepara la captura de bus mediante Q1 antes del PI de M1, usando el punto de partida
          // [Guía] disponible.
          prepareM1(in);
          // [Guía] Cambia el estado operativo que se evaluará a partir de la siguiente actualización. Aquí
          // [Guía] intervienen estado operativo interno.
          state_ = M1;
          // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
          reason_ = settings_.m1BranchAcquireEnabled ? "M1_ACQUIRE_PV_BRANCH" : "BUS_PI_Q1";
        }
        // [Guía] Termina esta rama o iteración y retoma la ejecución después del selector o bucle.
        break;
      // Solar solo: PI1 posee Q1. Primero atender perdida/conexion de fuentes;
      // no dejar que un PI legacy compita con la preparacion de una transferencia.
      // [Guía] PV alimenta el bus. Q1 conserva su función de regulación de tensión.
      case M1:
      // [Guía] Selecciona el tratamiento previsto para `RETIRE2_TO_M1` dentro del selector.
      case RETIRE2_TO_M1:
        // [Guía] Comprueba `state_ == RETIRE2_TO_M1`. Si se cumple, ejecuta el bloque asociado. Aquí intervienen
        // [Guía] estado operativo interno.
        if (state_ == RETIRE2_TO_M1) {
          // [Guía] Actualiza `q2_` con `approach(q2_, 0, Config::Q2_SLEW_PER_SECOND * dt)`. Este dato representa
          // [Guía] consigna de duty de Q2 (0 a 1).
          q2_ = approach(q2_, 0, Config::Q2_SLEW_PER_SECOND * dt);
          // [Guía] Comprueba `q2_ == 0`. Si se cumple, ejecuta `state_ = M1`. Aquí intervienen consigna de duty
          // [Guía] de Q2 (0 a 1).
          if (q2_ == 0) state_ = M1;
        }
        // [Guía] Comprueba `pvLost`. Si se cumple, ejecuta el bloque asociado. Aquí intervienen pérdida
        // [Guía] confirmada de PV.
        if (pvLost) {
          // [Guía] Pide apagar la rama indicada con duty cero. El sketch aplica físicamente la consigna.
          q1_ = 0;
          // [Guía] Comprueba `dischargeAllowed`. Si se cumple, ejecuta `recoverBattery(in)`. Aquí intervienen
          // [Guía] permiso de descarga de batería.
          if (dischargeAllowed) recoverBattery(in);
          // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
          else wait("WAIT_SOURCE");
          // [Guía] Termina esta rama o iteración y retoma la ejecución después del selector o bucle.
          break;
        }
        // Entregar Q1 antes de otra actualizacion de PI: al conectar bateria
        // su regulador de rama PV sola puede seguir creciendo hasta saturar.
        // [Guía] Comprueba `state_ == M1 && dischargeAllowed && m1BatteryQualified_`. Si se cumple, ejecuta el
        // [Guía] bloque asociado. Aquí intervienen estado operativo interno y permiso de descarga de batería.
        if (state_ == M1 && dischargeAllowed && m1BatteryQualified_) {
          // Bateria recien conectada: entregar el bus a Q3 y Q1 a MPPT.
          // El permiso de carga NO sustituye el margen PV ni evita pasar M4.
          // [Guía] Prepara la incorporación de la batería desde M1, conservando el duty aplicado a Q1 como punto
          // [Guía] inicial para su nueva tarea.
          enterStart4(in);
          // Ambas polaridades deben completar1s NUEVO antes de esta entrega.
          // Q1 conserva su duty; no ejecutar P&O ni entrar en M3 antes de acabar.
          // [Guía] Actualiza `hotplugPvSeeking_` con `settings_.hotplugPvSeekEnabled`.
          hotplugPvSeeking_ = settings_.hotplugPvSeekEnabled;
          // [Guía] Actualiza `hotplugPvSinceUs_` con `in.nowUs; hotplugPvReady_.clear(); hotplugPvQualified_ =
          // [Guía] false`.
          hotplugPvSinceUs_ = in.nowUs; hotplugPvReady_.clear(); hotplugPvQualified_ = false;
          // [Guía] Comprueba `hotplugPvSeeking_ && settings_.pvVoltageMpptEnabled`. Si se cumple, ejecuta el
          // [Guía] bloque asociado.
          if (hotplugPvSeeking_ && settings_.pvVoltageMpptEnabled) {
            // [Guía] Actualiza `pvVoltageControl_` con `true`. Este dato representa selección del control de
            // [Guía] tensión PV.
            pvVoltageControl_ = true;
            // [Guía] Prepara P&O con PI interno de tensión PV para empezar desde las medidas o consignas
            // [Guía] indicadas. Aquí intervienen P&O con PI interno de tensión PV y consigna de duty de Q1 (0 a
            // [Guía] 1).
            pvVoltageMppt_.begin(q1_, Config::HOTPLUG_PV_TARGET_V, in.nowUs);
            // [Guía] Actualiza `hotplugQ3Support_` con `false`.
            hotplugQ3Support_ = false;
            // [Guía] Actualiza `m3VoltageLoadStateKnown_` con `false; m3VoltageLoadOhm_ = 0`.
            m3VoltageLoadStateKnown_ = false; m3VoltageLoadOhm_ = 0;
          }
          // [Guía] Comprueba `hotplugPvSeeking_`. Si se cumple, ejecuta `mpptRunning_ = mpptInitialized_ =
          // [Guía] false`.
          if (hotplugPvSeeking_) mpptRunning_ = mpptInitialized_ = false;
          // [Guía] Actualiza `chargeAttempts_` con `pvAssistStarts_ = 0; pvAssistAttempted_ = false`.
          chargeAttempts_ = pvAssistStarts_ = 0; pvAssistAttempted_ = false;
          // [Guía] Actualiza `chargeRetryReferenceReady_` con `false; chargeRetry_.clear()`.
          chargeRetryReferenceReady_ = false; chargeRetry_.clear();
          // [Guía] Actualiza `pvAssistPending_` con `!settings_.strictChargeMargin && settings_.pvAssistEnabled`.
          pvAssistPending_ = !settings_.strictChargeMargin && settings_.pvAssistEnabled;
          // [Guía] Actualiza `pvAssistHotplugPending_` con `settings_.strictChargeMargin`.
          pvAssistHotplugPending_ = settings_.strictChargeMargin
            // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
            && settings_.pvAssistEnabled;
          // [Guía] Actualiza `pvAssistHotplugSinceUs_` con `in.nowUs`.
          pvAssistHotplugSinceUs_ = in.nowUs;
          // [Guía] Termina esta rama o iteración y retoma la ejecución después del selector o bucle.
          break;
        }
        // [Guía] Comprueba `state_ == M1 && !batteryLinkConfirmed_`. Si se cumple, ejecuta el bloque asociado.
        // [Guía] Aquí intervienen estado operativo interno y estimación confirmada de conexión de batería.
        if (state_ == M1 && !batteryLinkConfirmed_
            // [Guía] Agrupa condiciones alternativas y conjuntas en la prueba en curso.
            && (m1BatteryCandidate_ || m1BatteryConnect_.running)) {
          // No seguir integrando el PI PV solo mientras se confirma la otra
          // fuente. Mantener duty, sin autorizar aun Q2 ni Q3 por un candidato.
          // [Guía] Reinicia PI de bus aplicado a Q1 con el estado inicial dado en los argumentos. Aquí
          // [Guía] intervienen PI de bus aplicado a Q1 y consigna de duty de Q1 (0 a 1).
          pi1_.reset(error1(in), q1_);
          // [Guía] Borra la confirmación o acumulación en `m1AuxReliefReady_` para que no se arrastre al
          // [Guía] siguiente contexto.
          m1AuxReliefReady_.clear();
          // Si la corriente ya completo1s, mostrar el permiso concreto que
          // falta. No volver al PI solar ni empezar otro segundo al recuperarlo.
          // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
          reason_ = m1BatteryQualified_ ? batteryLinkState_ : "M1_BAT_CONNECT_HOLD_Q1";
          // [Guía] Termina esta rama o iteración y retoma la ejecución después del selector o bucle.
          break;
        }
        // [Guía] Actualiza la rama de M1. Aquí intervienen intervalo efectivo del control en s y tiempo
        // [Guía] transcurrido.
        update1(in, dt, elapsed);
        // [Guía] Termina esta rama o iteración y retoma la ejecución después del selector o bucle.
        break;
      // Conexion de bateria desde M1: mantener Q1 existente para MPPT y subir
      // Q3 suavemente. START4 es transitorio; no significa Q2 ya autorizado.
      // [Guía] Prepara el apoyo de batería mediante Q3.
      case START4:
        // [Guía] Ante pérdida de PV, intenta mantener el bus con la batería si esta puede descargar. Interrumpe
        // [Guía] el procesamiento del modo de origen.
        if (pvLost) { recoverBattery(in); break; }
        // [Guía] Comprueba `!dischargeAllowed`. Si se cumple, sale de esta rama del selector tras realizar la
        // [Guía] acción indicada. Aquí intervienen permiso de descarga de batería.
        if (!dischargeAllowed) { enterM1(in); break; }
        // [Guía] Comprueba `pvVoltageControl_`. Si se cumple, ejecuta el bloque asociado. Aquí intervienen
        // [Guía] selección del control de tensión PV.
        if (pvVoltageControl_) {
          // M4 de preparacion: no forzar5% de descarga con bus ya alto.
          // Q1 empieza inmediatamente a seguir18.4V, Q2 continua apagado.
          // [Guía] Desplaza Q1 hacia la región de tensión PV requerida y califica el objetivo antes de entregarlo
          // [Guía] al MPPT. Aquí intervienen intervalo efectivo del control en s.
          runHotplugPvSeek(in, dt);
          // [Guía] Actualiza el apoyo del bus durante la conexión de batería y coordina esa acción con la
          // [Guía] preparación del PV. Aquí intervienen intervalo efectivo del control en s y tiempo
          // [Guía] transcurrido.
          updateHotplugSupport(in, dt, elapsed);
          // [Guía] Cambia el estado operativo que se evaluará a partir de la siguiente actualización. Aquí
          // [Guía] intervienen estado operativo interno.
          state_ = M4;
          // [Guía] Comprueba `hotplugPvSeeking_`. Si se cumple, ejecuta `reason_ = hotplugPvReason_`.
          if (hotplugPvSeeking_) reason_ = hotplugPvReason_;
          // [Guía] Termina esta rama o iteración y retoma la ejecución después del selector o bucle.
          break;
        }
        // MPPT ya tiene Q1 desde el duty real. No esperar a terminar Q3.
        // [Guía] Comprueba `!runHotplugPvSeek(in, dt)`. Si se cumple, ejecuta `runMppt(in, dt)`. Aquí intervienen
        // [Guía] intervalo efectivo del control en s.
        if (!runHotplugPvSeek(in, dt)) runMppt(in, dt);
        // Completar primero la entrega a M4. Solo M4 evalua pasar a carga;
        // no autorizar Q2 durante el arranque por una excursion momentanea PV.
        // No exigir20.5 V para poder entregar el regulador. Subir SOLO hasta
        // el minimo5%, con slew; el PI decide despues usando su signo actual.
        // Los limites locales de tension/corriente anteriores siguen activos.
        // [Guía] Comprueba `in.batteryI < Config::BAT_DISCHARGE_SOFT_A`. Si se cumple, ejecuta el bloque
        // [Guía] asociado.
        if (in.batteryI < Config::BAT_DISCHARGE_SOFT_A) {
          // [Guía] Actualiza `q3_` con `approach(q3_, Config::Q3_DUTY_MIN, Config::Q3_SLEW_PER_SECOND * dt)`.
          // [Guía] Este dato representa consigna de duty de Q3 (0 a 1).
          q3_ = approach(q3_, Config::Q3_DUTY_MIN, Config::Q3_SLEW_PER_SECOND * dt);
          // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
          reason_ = "M1_TO_M4_Q3_RAMP";
        // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
        } else reason_ = "M1_TO_M4_CURRENT_HOLD";
        // [Guía] Comprueba `q3_ >= Config::Q3_DUTY_MIN`. Si se cumple, ejecuta el bloque asociado. Aquí
        // [Guía] intervienen consigna de duty de Q3 (0 a 1).
        if (q3_ >= Config::Q3_DUTY_MIN) {
          // El bus ya regulaba20 V en M1: no repetir la rampa de referencia1 s.
          // Arranque en minimo, no un equilibrio: no sembrar I3 para cancelar
          // un gran error negativo. El siguiente P real queda acotado por slew.
          // [Guía] Reinicia PI de bus aplicado a Q3 con el estado inicial dado en los argumentos. Aquí
          // [Guía] intervienen PI de bus aplicado a Q3 y consigna de duty de Q3 (0 a 1).
          pi3_.reset(0.0f, q3_);
          // [Guía] Cambia el estado operativo que se evaluará a partir de la siguiente actualización. Aquí
          // [Guía] intervienen estado operativo interno.
          state_ = M4; reason_ = "MPPT_AND_BUS_PI_Q3";
          // [Guía] Borra la confirmación o acumulación en `chargeAtMinimum_` para que no se arrastre al siguiente
          // [Guía] contexto.
          chargeAtMinimum_.clear();
        }
        // [Guía] Comprueba `state_ == START4 && timedOut(in)`. Si se cumple, ejecuta `fail("Q3_START_TIMEOUT")`.
        // [Guía] Aquí intervienen estado operativo interno.
        if (state_ == START4 && timedOut(in)) fail("Q3_START_TIMEOUT");
        // [Guía] Comprueba `hotplugPvSeeking_`. Si se cumple, ejecuta `reason_ = hotplugPvReason_`.
        if (hotplugPvSeeking_) reason_ = hotplugPvReason_;
        // [Guía] Termina esta rama o iteración y retoma la ejecución después del selector o bucle.
        break;
      // Nombre interno por interruptorQ3: arranca M5 sin PV o M4 con PV.
      // No confundir START3 con el modoM3, que usa Q2 para cargar bateria.
      // [Guía] Selecciona el tratamiento previsto para `START3` dentro del selector.
      case START3:
        // [Guía] Comprueba `in.batteryI < Config::BAT_DISCHARGE_SOFT_A`. Si se cumple, ejecuta el bloque
        // [Guía] asociado.
        if (in.batteryI < Config::BAT_DISCHARGE_SOFT_A)
          // [Guía] Actualiza `q3_` con `approach(q3_, Config::Q3_DUTY_MIN, Config::Q3_SLEW_PER_SECOND * dt)`.
          // [Guía] Este dato representa consigna de duty de Q3 (0 a 1).
          q3_ = approach(q3_, Config::Q3_DUTY_MIN, Config::Q3_SLEW_PER_SECOND * dt);
        // [Guía] Comprueba `q3_ >= Config::Q3_DUTY_MIN`. Si se cumple, ejecuta el bloque asociado. Aquí
        // [Guía] intervienen consigna de duty de Q3 (0 a 1).
        if (q3_ >= Config::Q3_DUTY_MIN) {
          // [Guía] Actualiza `reference_` con `fminf(in.bus, Config::BUS_REFERENCE_V)`. Este dato representa
          // [Guía] referencia del regulador.
          reference_ = fminf(in.bus, Config::BUS_REFERENCE_V);
          // [Guía] Actualiza `rampElapsed_` con `0; pi3_.reset(error3(in), q3_)`. Este dato representa tiempo ya
          // [Guía] recorrido en la rampa.
          rampElapsed_ = 0; pi3_.reset(error3(in), q3_);
          // [Guía] Comprueba `q1_ >= Config::Q1_DUTY_MIN && pvOn`. Si se cumple, ejecuta `enterM4(in)`. Aquí
          // [Guía] intervienen consigna de duty de Q1 (0 a 1) y disponibilidad confirmada de PV.
          if (q1_ >= Config::Q1_DUTY_MIN && pvOn) enterM4(in);
          // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
          else { state_ = M5; reason_ = "BUS_PI_Q3"; }
        }
        // [Guía] Comprueba `uint32_t(in.nowUs - transferStart_) >= Config::BAT_TRANSFER_TIMEOUT_US`. Si se
        // [Guía] cumple, ejecuta el bloque asociado. Aquí intervienen reloj de la muestra en µs.
        if (uint32_t(in.nowUs - transferStart_) >= Config::BAT_TRANSFER_TIMEOUT_US)
          // [Guía] Registra el motivo del fallo y retira las consignas de PWM para que el sketch las apague
          // [Guía] físicamente.
          fail("Q3_START_TIMEOUT");
        // [Guía] Termina esta rama o iteración y retoma la ejecución después del selector o bucle.
        break;
      // Intervalo sin Q2/Q3 antes de dar pulsos de descarga aQ3.
      // [Guía] Selecciona el tratamiento previsto para `BAT_INTERLOCK` dentro del selector.
      case BAT_INTERLOCK:
        // [Guía] Pide apagar la rama indicada con duty cero. El sketch aplica físicamente la consigna.
        q1_ = q2_ = q3_ = 0;
        // [Guía] Comprueba `!dischargeAllowed`. Si se cumple, sale de esta rama del selector tras realizar la
        // [Guía] acción indicada. Aquí intervienen permiso de descarga de batería.
        if (!dischargeAllowed) { wait("WAIT_BAT_PERMISSION"); break; }
        // [Guía] Comprueba `in.busRaw < Config::BAT_BUS_START_MIN_V`. Si se cumple, sale de esta rama del
        // [Guía] selector tras realizar la acción indicada.
        if (in.busRaw < Config::BAT_BUS_START_MIN_V) { wait("WAIT_BUS_PRECHARGE"); break; }
        // [Guía] Espera el intervalo de separación de ramas contado desde el comienzo de la transferencia antes
        // [Guía] de preparar Q3.
        if (uint32_t(in.nowUs - transferStart_) >= Config::BAT_INTERLOCK_US) enterStart3(in);
        // [Guía] Termina esta rama o iteración y retoma la ejecución después del selector o bucle.
        break;
      // Camino compartido de PI3: M5 bateria sola; STARTPV incorpora Q1;
      // M4 combina ambas fuentes. Q2 permanece apagado mientras Q3 tiene control.
      // [Guía] La batería sostiene el bus por medio del PI de Q3.
      case M5:
      // [Guía] Selecciona el tratamiento previsto para `STARTPV` dentro del selector.
      case STARTPV:
      // [Guía] PV y batería dan apoyo al bus. Q3 se ocupa de la tensión de bus y Q1 del puerto PV.
      case M4:
        // Conservar recuperacion rapida de M5/arranque PV. En M4 el PI de Q3
        // recibe siempre el bus rapido; no reinicializar su integral por20.8 V.
        // [Guía] Comprueba `state_ == M4 && pvVoltageControl_`. Si se cumple, ejecuta el bloque asociado. Aquí
        // [Guía] intervienen estado operativo interno y selección del control de tensión PV.
        if (state_ == M4 && pvVoltageControl_) {
          // [Guía] Actualiza el apoyo del bus durante la conexión de batería y coordina esa acción con la
          // [Guía] preparación del PV. Aquí intervienen intervalo efectivo del control en s y tiempo
          // [Guía] transcurrido.
          updateHotplugSupport(in, dt, elapsed);
        // [Guía] Comprueba `busRecovery_ && state_ != M4`. Si se cumple, ejecuta el bloque asociado. Aquí
        // [Guía] intervienen indicación de que se requiere recuperar el bus y estado operativo interno.
        } else if (busRecovery_ && state_ != M4) {
          // [Guía] Actualiza `reference_` con `Config::BUS_REFERENCE_V`. Este dato representa referencia del
          // [Guía] regulador.
          reference_ = Config::BUS_REFERENCE_V;
          // [Guía] Actualiza `rampElapsed_` con `Config::SOFT_START_US`. Este dato representa tiempo ya recorrido
          // [Guía] en la rampa.
          rampElapsed_ = Config::SOFT_START_US;
          // [Guía] Actualiza `q3_` con `approach(q3_, Config::Q3_DUTY_MIN, Config::Q3_SLEW_PER_SECOND * dt)`.
          // [Guía] Este dato representa consigna de duty de Q3 (0 a 1).
          q3_ = approach(q3_, Config::Q3_DUTY_MIN, Config::Q3_SLEW_PER_SECOND * dt);
          // [Guía] Reinicia PI de bus aplicado a Q3 con el estado inicial dado en los argumentos. Aquí
          // [Guía] intervienen PI de bus aplicado a Q3 y consigna de duty de Q3 (0 a 1).
          pi3_.reset(error3(in), q3_); reason_ = "OV_REDUCE_Q3";
        // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
        } else update3(in, dt, elapsed);
        // [Guía] Comprueba `state_ == M5 && pvOn`. Si se cumple, ejecuta el bloque asociado. Aquí intervienen
        // [Guía] estado operativo interno y disponibilidad confirmada de PV.
        if (state_ == M5 && pvOn) {
          // [Guía] Comprueba `!in.pvConfigured`. Si se cumple, sale de esta rama del selector tras realizar la
          // [Guía] acción indicada.
          if (!in.pvConfigured) { reason_ = "PV_CONFIG_REQUIRED"; break; }
          // [Guía] Cambia el estado operativo que se evaluará a partir de la siguiente actualización. Aquí
          // [Guía] intervienen estado operativo interno.
          state_ = STARTPV; reason_ = "PV_SOFT_START"; q1_ = 0; pvStartUs_ = in.nowUs;
        }
        // [Guía] Comprueba `state_ == STARTPV`. Si se cumple, ejecuta el bloque asociado. Aquí intervienen estado
        // [Guía] operativo interno.
        if (state_ == STARTPV) {
          // [Guía] Comprueba `pvLost`. Si se cumple, sale de esta rama del selector tras realizar la acción
          // [Guía] indicada. Aquí intervienen pérdida confirmada de PV.
          if (pvLost) { q1_ = 0; state_ = M5; break; }
          // Sin ventana de bus o espera de MPP: conectar PV al detectarlo.
          // [Guía] Actualiza `q1_` con `approach(q1_, Config::Q1_DUTY_MIN, Config::Q1_TEST_SLEW_PER_SECOND *
          // [Guía] dt)`. Este dato representa consigna de duty de Q1 (0 a 1).
          q1_ = approach(q1_, Config::Q1_DUTY_MIN, Config::Q1_TEST_SLEW_PER_SECOND * dt);
          // [Guía] Comprueba `q1_ >= Config::Q1_DUTY_MIN`. Si se cumple, ejecuta `enterM4(in)`. Aquí intervienen
          // [Guía] consigna de duty de Q1 (0 a 1).
          if (q1_ >= Config::Q1_DUTY_MIN) enterM4(in);
        }
        // [Guía] Comprueba `state_ == M4`. Si se cumple, ejecuta el bloque asociado. Aquí intervienen estado
        // [Guía] operativo interno.
        if (state_ == M4) {
          // [Guía] Da prioridad a la ruta de carga exclusiva cuando el bus queda sin demanda. Inicia el retiro de
          // [Guía] las ramas que regulaban el bus antes de dejar a Q2 en control de corriente.
          if (m2Ready) { enterM2Preparation(in, dt); break; }
          // [Guía] Comprueba `pvLost`. Si se cumple, ejecuta el bloque asociado. Aquí intervienen pérdida
          // [Guía] confirmada de PV.
          if (pvLost) {
            // [Guía] Limpia el estado de la preparación PV para que la siguiente operación arranque con
            // [Guía] confirmaciones nuevas.
            cancelPvAssist();
            // [Guía] Pide apagar la rama indicada con duty cero. El sketch aplica físicamente la consigna.
            q1_ = 0; mpptRunning_ = mpptInitialized_ = false;
            // [Guía] Cambia el estado operativo que se evaluará a partir de la siguiente actualización. Aquí
            // [Guía] intervienen estado operativo interno.
            state_ = M5; reason_ = "PV_LOST_BATTERY_PI"; break;
          }
          // PI3 ya atendio el bus. Esta preparacion posee SOLO Q1 y bloquea
          // la ayuda antigua, el MPPT y la transferencia a Q2 mientras dure.
          // [Guía] Comprueba `runHotplugPvSeek(in, dt)`. Si se cumple, sale de esta rama del selector tras
          // [Guía] realizar la acción indicada. Aquí intervienen intervalo efectivo del control en s.
          if (runHotplugPvSeek(in, dt)) break;
          // [Guía] Actualiza `highBus` con `prepHighBus_.held(`.
          const bool highBus = prepHighBus_.held(
            // [Guía] Completa la declaración o expresión con `busSupervisor_ > Config::BUS_REFERENCE_V + 0.50f,
            // [Guía] in.nowUs`. Aquí intervienen tensión de bus filtrada para supervisión y reloj de la muestra
            // [Guía] en µs.
            busSupervisor_ > Config::BUS_REFERENCE_V + 0.50f, in.nowUs,
            // [Guía] Aporta `Config::EMS_M4_PREP_HIGH_BUS_HOLD_US` como parte de los parámetros de la operación.
            Config::EMS_M4_PREP_HIGH_BUS_HOLD_US);
          // [Guía] Actualiza `preparationDue` con `highBus || (pvAssistHotplugPending_`.
          const bool preparationDue = highBus || (pvAssistHotplugPending_
            // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen reloj de
            // [Guía] la muestra en µs.
            && uint32_t(in.nowUs - pvAssistHotplugSinceUs_) >= Config::EMS_HOTPLUG_PV_PREP_WAIT_US);
          // Con Q3 minimo y bus alto, esperar20 V para liberar intentos era un
          // bloqueo circular. Liberar solo con PV nuevo, sostenido y permisos.
          // [Guía] Actualiza `improvedPv` con `settings_.strictChargeMargin && chargeAttempts_ >= 2`.
          const bool improvedPv = settings_.strictChargeMargin && chargeAttempts_ >= 2
            // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
            && chargeRetryReferenceReady_ && preparedPv && pvChargeMargin
            // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen permiso de
            // [Guía] carga de batería.
            && chargeAllowed && !recoverSource && !pvAssistActive()
            // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen consigna
            // [Guía] de duty de Q3 (0 a 1).
            && q3_ <= Config::Q3_DUTY_MIN + 0.0001f
            // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen reloj de
            // [Guía] la muestra en µs.
            && uint32_t(in.nowUs - chargeRetrySinceUs_) >= Config::EMS_CHARGE_REARM_COOLDOWN_US
            // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen tensión de
            // [Guía] PV filtrada para supervisión.
            && pvSupervisor_ >= chargeRetryPv_ + Config::EMS_CHARGE_REARM_PV_RISE_V;
          // [Guía] Comprueba `chargeRetry_.held(improvedPv, in.nowUs, Config::EMS_CHARGE_REARM_CONFIRM_US)`. Si
          // [Guía] se cumple, ejecuta el bloque asociado. Aquí intervienen reloj de la muestra en µs.
          if (chargeRetry_.held(improvedPv, in.nowUs, Config::EMS_CHARGE_REARM_CONFIRM_US)) {
            // [Guía] Actualiza `chargeAttempts_` con `0; pvAssistAttempted_ = false`.
            chargeAttempts_ = 0; pvAssistAttempted_ = false;
            // [Guía] Actualiza `chargeRetryReferenceReady_` con `false; chargeRetry_.clear()`.
            chargeRetryReferenceReady_ = false; chargeRetry_.clear();
            // [Guía] Borra la confirmación o acumulación en `chargeAtMinimum_` para que no se arrastre al
            // [Guía] siguiente contexto.
            chargeAtMinimum_.clear();
          }
          // [Guía] Comprueba `settings_.strictChargeMargin && preparationDue && !preparedPv`. Si se cumple,
          // [Guía] ejecuta el bloque asociado.
          if (settings_.strictChargeMargin && preparationDue && !preparedPv
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && !pvAssistActive()
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen permiso
              // [Guía] de carga de batería y disponibilidad confirmada de PV.
              && chargeAllowed && pvOn && in.pvI > Config::BAT_PV_ON_CURRENT_A
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && settings_.pvAssistEnabled && !pvAssistAttempted_
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && chargeAttempts_ < 2 && pvAssistStarts_ < 2 && validPvAssistSettings()) {
            // Un impedimento transitorio no consume la preparacion pendiente.
            // [Guía] Actualiza `pvAssistPending_` con `true; pvAssistAttempted_ = true`.
            pvAssistPending_ = true; pvAssistAttempted_ = true;
            // [Guía] Actualiza `pvAssistHotplugPending_` con `false`.
            pvAssistHotplugPending_ = false;
          }
          // Un solo propietario de Q1: ayuda de tension acotada o MPPT.
          // Q3 ya recibio su actualizacion PI y Q2 sigue apagado.
          // [Guía] Comprueba `runPvAssist(in, dt, chargeAllowed)`. Si se cumple, ejecuta el bloque asociado. Aquí
          // [Guía] intervienen intervalo efectivo del control en s y permiso de carga de batería.
          if (runPvAssist(in, dt, chargeAllowed)) {
            // [Guía] Borra la confirmación o acumulación en `chargeAtMinimum_` para que no se arrastre al
            // [Guía] siguiente contexto.
            chargeAtMinimum_.clear();
            // [Guía] Termina esta rama o iteración y retoma la ejecución después del selector o bucle.
            break;
          }
          // [Guía] Ejecuta la modalidad de seguimiento que corresponde al estado y mantiene su diagnóstico. Aquí
          // [Guía] intervienen intervalo efectivo del control en s.
          runMppt(in, dt);
          // El motivo de espera no bloquea MPPT ni el PI de Q3.
          // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
          reason_ = !chargeAllowed ? "M4_MPPT_CHARGE_NOT_ALLOWED"
            // [Guía] Define una de las dos salidas de la expresión condicional.
            : !pvChargeMargin ? "M4_MPPT_WAIT_PV_MARGIN"
            // [Guía] Define una de las dos salidas de la expresión condicional.
            : recoverSource ? "M4_MPPT_RECOVER_SOURCE"
            // [Guía] Define una de las dos salidas de la expresión condicional. Aquí intervienen consigna de duty
            // [Guía] de Q3 (0 a 1).
            : q3_ > Config::Q3_DUTY_MIN + 0.0001f ? "M4_MPPT_BAT_SUPPORT"
            // [Guía] Define una de las dos salidas de la expresión condicional.
            : "M4_CONFIRM_CHARGE_MARGIN";
          // [Guía] Comprueba `!settings_.strictChargeMargin && pvAssistResult_`. Si se cumple, ejecuta `reason_ =
          // [Guía] pvAssistResult_`.
          if (!settings_.strictChargeMargin && pvAssistResult_) reason_ = pvAssistResult_;
          // [Guía] Comprueba `pvAssistHotplugPending_ && !preparationDue && chargeAllowed && pvOn`. Si se cumple,
          // [Guía] ejecuta el bloque asociado. Aquí intervienen permiso de carga de batería y disponibilidad
          // [Guía] confirmada de PV.
          if (pvAssistHotplugPending_ && !preparationDue && chargeAllowed && pvOn
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen código
              // [Guía] de diagnóstico de la decisión.
              && in.pvI > Config::BAT_PV_ON_CURRENT_A) reason_ = "M4_WAIT_PV_PREP_DELAY";
          // [Guía] Comprueba `settings_.strictChargeMargin && preparationDue && !preparedPv`. Si se cumple,
          // [Guía] ejecuta el bloque asociado.
          if (settings_.strictChargeMargin && preparationDue && !preparedPv) {
            // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
            reason_ = !chargeAllowed ? chargeBlockReason(in)
              // [Guía] Define una de las dos salidas de la expresión condicional. Aquí intervienen disponibilidad
              // [Guía] confirmada de PV.
              : !pvOn || in.pvI <= Config::BAT_PV_ON_CURRENT_A ? "M4_PREP_WAIT_PV_FLOW"
              // [Guía] Define una de las dos salidas de la expresión condicional.
              : !settings_.pvAssistEnabled ? "M4_PREP_DISABLED"
              // [Guía] Define una de las dos salidas de la expresión condicional.
              : !validPvAssistSettings() ? "M4_PREP_SETTINGS_INVALID"
              // [Guía] Define una de las dos salidas de la expresión condicional.
              : pvAssistAttempted_ ? "M4_PREP_ABORTED" : "M4_PREP_WAIT_BUS";
            // [Guía] Comprueba `pvAssistAttempted_ && pvAssistResult_`. Si se cumple, ejecuta el bloque asociado.
            if (pvAssistAttempted_ && pvAssistResult_
                // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen
                // [Guía] permiso de carga de batería.
                && !strcmp(pvAssistResult_, "M4_MPPT_LIFT_READY") && chargeAllowed
                // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen
                // [Guía] disponibilidad confirmada de PV.
                && pvOn && in.pvI > Config::BAT_PV_ON_CURRENT_A
                // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
                && settings_.pvAssistEnabled && validPvAssistSettings())
              // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
              reason_ = "M4_MPPT_WAIT_PV_MARGIN";
          }
          // [Guía] Comprueba `settings_.strictChargeMargin && (chargeAttempts_ >= 2 || (!preparedPv &&
          // [Guía] pvAssistStarts_ >= 2))`. Si se cumple, ejecuta el bloque asociado.
          if (settings_.strictChargeMargin && (chargeAttempts_ >= 2 || (!preparedPv && pvAssistStarts_ >= 2)))
            // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
            reason_ = chargeRetry_.running ? "M4_RETRY_CONFIRM_NEW_PV" : "M4_PREP_RETRY_LIMIT";
          // Margen2 V y Q3 en minimo, o apagado en la preparacion V59.
          // SOC/tension/configuracion siguen siendo permisos de bateria.
          // [Guía] Actualiza `qualified` con `chargeAtMinimum_.held(chargeAllowed && pvOn && !recoverSource`.
          const bool qualified = chargeAtMinimum_.held(chargeAllowed && pvOn && !recoverSource
            // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
            && (settings_.strictChargeMargin ? preparedPv && chargeAttempts_ < 2 : pvChargeMargin)
            // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen consigna
            // [Guía] de duty de Q3 (0 a 1).
            && q3_ <= Config::Q3_DUTY_MIN + 0.0001f,
            // [Guía] Completa la declaración o expresión con `in.nowUs, settings_.strictChargeMargin ? 150000 :
            // [Guía] Config::BAT_CHARGE_READY_US)`. Aquí intervienen reloj de la muestra en µs.
            in.nowUs, settings_.strictChargeMargin ? 150000 : Config::BAT_CHARGE_READY_US);
          // [Guía] Actualiza `toCharge` con `qualified && pvChargeMargin`.
          const bool toCharge = qualified && pvChargeMargin;
          // [Guía] Comprueba `toCharge`. Si se cumple, ejecuta el bloque asociado.
          if (toCharge) {
            // [Guía] Comprueba `settings_.strictChargeMargin`. Si se cumple, ejecuta el bloque asociado.
            if (settings_.strictChargeMargin) {
              // [Guía] Suma una unidad a `chargeAttempts_` para contabilizar el evento de esta rama.
              ++chargeAttempts_;
              // [Guía] Actualiza `chargeRetryPv_` con `pvSupervisor_; chargeRetryReferenceReady_ = true`.
              chargeRetryPv_ = pvSupervisor_; chargeRetryReferenceReady_ = true;
              // [Guía] Actualiza `chargeRetrySinceUs_` con `in.nowUs`.
              chargeRetrySinceUs_ = in.nowUs;
            }
            // [Guía] Registra el destino y el instante inicial de una transferencia. Aquí intervienen reloj de la
            // [Guía] muestra en µs y código de diagnóstico de la decisión.
            enterTransfer(RETIRE3, in.nowUs); reason_ = "PV_MARGIN_2V_RETIRE_Q3";
          }
        }
        // [Guía] Termina esta rama o iteración y retoma la ejecución después del selector o bucle.
        break;
      // Retirar Q3 gradualmente, conservando Q1. Si margen/permisos cambian,
      // cancelar transferencia; no activar Q2 con un permiso medido antiguo.
      // [Guía] Selecciona el tratamiento previsto para `RETIRE3` dentro del selector.
      case RETIRE3:
        // [Guía] Conserva la referencia preparada de tensión PV con el PI interno de Q1. Aquí intervienen
        // [Guía] intervalo efectivo del control en s.
        holdPreparedPv(in, dt);
        // Mantener Q1 fijo SOLO durante la transferencia corta entre ramas.
        // [Guía] Actualiza `q3_` con `approach(q3_, 0, Config::Q3_SLEW_PER_SECOND * dt)`. Este dato representa
        // [Guía] consigna de duty de Q3 (0 a 1).
        q3_ = approach(q3_, 0, Config::Q3_SLEW_PER_SECOND * dt);
        // [Guía] Ante pérdida de PV, intenta mantener el bus con la batería si esta puede descargar. Interrumpe
        // [Guía] el procesamiento del modo de origen.
        if (pvLost) { recoverBattery(in); break; }
        // [Guía] Comprueba `!chargeAllowed || recoverSource`. Si se cumple, sale de esta rama del selector tras
        // [Guía] realizar la acción indicada. Aquí intervienen permiso de carga de batería.
        if (!chargeAllowed || recoverSource) { enterStart3(in); break; }
        // [Guía] Comprueba `handoffMarginLost(in)`. Si se cumple, sale de esta rama del selector tras realizar la
        // [Guía] acción indicada.
        if (handoffMarginLost(in)) { retireForPvMargin(in); break; }
        // [Guía] Comprueba `q3_ == 0`. Si se cumple, ejecuta el bloque asociado. Aquí intervienen consigna de
        // [Guía] duty de Q3 (0 a 1).
        if (q3_ == 0) {
          // [Guía] Registra el destino y el instante inicial de una transferencia. Aquí intervienen reloj de la
          // [Guía] muestra en µs y código de diagnóstico de la decisión.
          enterTransfer(INTERLOCK, in.nowUs); reason_ = "Q3_OFF_INTERLOCK";
        }
        // [Guía] Comprueba `timedOut(in)`. Si se cumple, ejecuta `fail("TRANSFER_TIMEOUT")`.
        if (timedOut(in)) fail("TRANSFER_TIMEOUT");
        // [Guía] Termina esta rama o iteración y retoma la ejecución después del selector o bucle.
        break;
      // Break-before-make: ambos interruptores de bateria en cero durante el
      // tiempo configurado, con nueva comprobacion del margen antes de START2.
      // [Guía] Mantiene el intervalo de separación entre ramas antes de activar la entrante.
      case INTERLOCK:
        // [Guía] Conserva la referencia preparada de tensión PV con el PI interno de Q1. Aquí intervienen
        // [Guía] intervalo efectivo del control en s.
        holdPreparedPv(in, dt);
        // Break-before-make: el sketch aplica Q3 LOW antes de cualquier Q2.
        // No esperar corriente negativa antes de conectar el actuador de carga.
        // [Guía] Pide apagar la rama indicada con duty cero. El sketch aplica físicamente la consigna.
        q3_ = q2_ = 0;
        // [Guía] Ante pérdida de PV, intenta mantener el bus con la batería si esta puede descargar. Interrumpe
        // [Guía] el procesamiento del modo de origen.
        if (pvLost) { recoverBattery(in); break; }
        // [Guía] Comprueba `!chargeAllowed || recoverSource`. Si se cumple, sale de esta rama del selector tras
        // [Guía] realizar la acción indicada. Aquí intervienen permiso de carga de batería.
        if (!chargeAllowed || recoverSource) { enterStart3(in); break; }
        // [Guía] Comprueba `handoffMarginLost(in)`. Si se cumple, sale de esta rama del selector tras realizar la
        // [Guía] acción indicada.
        if (handoffMarginLost(in)) { retireForPvMargin(in); break; }
        // Revalidar el margen despues del interlock, no usar el permiso viejo
        // de M4 si el panel ya cayo mientras se retiraba la otra rama.
        // [Guía] Comprueba `uint32_t(in.nowUs - transferStart_) >= Config::BAT_INTERLOCK_US && pvChargeMargin`.
        // [Guía] Si se cumple, ejecuta el bloque asociado. Aquí intervienen reloj de la muestra en µs.
        if (uint32_t(in.nowUs - transferStart_) >= Config::BAT_INTERLOCK_US && pvChargeMargin) {
          // [Guía] Cambia el estado operativo que se evaluará a partir de la siguiente actualización. Aquí
          // [Guía] intervienen estado operativo interno.
          state_ = START2; reason_ = "Q2_SOFT_START"; transferStart_ = in.nowUs;
          // [Guía] Actualiza `chargeFlowSeen_` con `false; chargeFlowConfirm_.clear()`.
          chargeFlowSeen_ = false; chargeFlowConfirm_.clear();
        // [Guía] Comprueba `timedOut(in)`. Si se cumple, ejecuta el bloque asociado.
        } else if (timedOut(in)) {
          // [Guía] Prepara la incorporación de la batería desde M1, conservando el duty aplicado a Q1 como punto
          // [Guía] inicial para su nueva tarea. Aquí intervienen código de diagnóstico de la decisión.
          enterStart4(in); reason_ = "Q2_MARGIN_LOST_M4";
        // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
        } else reason_ = "Q2_INTERLOCK_WAIT_PV_MARGIN";
        // [Guía] Termina esta rama o iteración y retoma la ejecución después del selector o bucle.
        break;
      // Q2 arranca solo con condiciones frescas. Precargar PI2 evita que herede
      // la integral de PI3, cuya accion corresponde al sentido de energia opuesto.
      // [Guía] Activa la rama de carga con una salida inicial controlada.
      case START2:
        // [Guía] Conserva la referencia preparada de tensión PV con el PI interno de Q1. Aquí intervienen
        // [Guía] intervalo efectivo del control en s.
        holdPreparedPv(in, dt);
        // [Guía] Ante pérdida de PV, intenta mantener el bus con la batería si esta puede descargar. Interrumpe
        // [Guía] el procesamiento del modo de origen.
        if (pvLost) { recoverBattery(in); break; }
        // [Guía] Si la batería deja de admitir carga en esta rama, transfiere la regulación del bus a Q1 mediante
        // [Guía] la entrada a M1.
        if (!chargeAllowed) { enterM1(in); break; }
        // [Guía] Comprueba `recoverSource`. Si se cumple, sale de esta rama del selector tras realizar la acción
        // [Guía] indicada.
        if (recoverSource) { enterTransfer(RETIRE2, in.nowUs); break; }
        // [Guía] Comprueba `handoffMarginLost(in)`. Si se cumple, sale de esta rama del selector tras realizar la
        // [Guía] acción indicada.
        if (handoffMarginLost(in)) { retireForPvMargin(in); break; }
        // [Guía] Comprueba `!pvChargeMargin`. Si se cumple, ejecuta el bloque asociado.
        if (!pvChargeMargin) {
          // Sin permiso fresco no emitir primer pulso, aumentar Q2 ni declarar
          // M3. Agotada la espera, retirar pulsos por la ruta de interlock inversa.
          // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
          reason_ = "Q2_WAIT_PV_MARGIN";
          // [Guía] Comprueba `timedOut(in)`. Si se cumple, ejecuta `enterTransfer(RETIRE2, in.nowUs)`.
          if (timedOut(in)) enterTransfer(RETIRE2, in.nowUs);
          // [Guía] Termina esta rama o iteración y retoma la ejecución después del selector o bucle.
          break;
        }
        // [Guía] Comprueba `in.batteryV >= Config::BAT_CHARGE_MAX_V`. Si se cumple, ejecuta el bloque asociado.
        if (in.batteryV >= Config::BAT_CHARGE_MAX_V) {
          // [Guía] Comprueba `timedOut(in)`. Si se cumple, ejecuta `fail("Q2_START_TIMEOUT")`.
          if (timedOut(in)) fail("Q2_START_TIMEOUT");
          // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
          else reason_ = "Q2_WAIT_VBAT_SAMPLE";
          // [Guía] Termina esta rama o iteración y retoma la ejecución después del selector o bucle.
          break;
        }
        // [Guía] Actualiza `q2_` con `approach(q2_, Config::Q2_DUTY_MIN, Config::Q2_SLEW_PER_SECOND * dt)`. Este
        // [Guía] dato representa consigna de duty de Q2 (0 a 1).
        q2_ = approach(q2_, Config::Q2_DUTY_MIN, Config::Q2_SLEW_PER_SECOND * dt);
        // [Guía] Comprueba `q2_ >= Config::Q2_DUTY_MIN`. Si se cumple, ejecuta el bloque asociado. Aquí
        // [Guía] intervienen consigna de duty de Q2 (0 a 1).
        if (q2_ >= Config::Q2_DUTY_MIN) {
          // [Guía] Reinicia PI de bus aplicado a Q2 con el estado inicial dado en los argumentos. Aquí
          // [Guía] intervienen PI de bus aplicado a Q2 y consigna de duty de Q2 (0 a 1).
          pi2_.reset(error2(in), q2_);
          // M3 describe los actuadores asignados, no demuestra carga real.
          // Ibat negativa se informa aparte y nunca congela MPPT para validarla.
          // [Guía] Cambia el estado operativo que se evaluará a partir de la siguiente actualización. Aquí
          // [Guía] intervienen estado operativo interno.
          state_ = M3; reason_ = "MPPT_AND_BUS_PI_Q2";
          // [Guía] Borra la confirmación o acumulación en `deficitHold_` para que no se arrastre al siguiente
          // [Guía] contexto.
          deficitHold_.clear(); chargeDischargeHold_.clear();
          // Nueva carga/actuador: descartar potencia de M4 antes de explorar.
          // [Guía] Actualiza `m3MpptSettling_` con `settings_.m3MpptContextRebaseEnabled`.
          m3MpptSettling_ = settings_.m3MpptContextRebaseEnabled;
          // [Guía] Actualiza `m3MpptQ2Anchor_` con `q2_; m3MpptContextSinceUs_ = in.nowUs`.
          m3MpptQ2Anchor_ = q2_; m3MpptContextSinceUs_ = in.nowUs;
          // [Guía] Actualiza `m3MpptLoadConfirmed_` con `in.recoveryLoadConfirmed`.
          m3MpptLoadConfirmed_ = in.recoveryLoadConfirmed;
          // [Guía] Actualiza `m3MpptLoadOhm_` con `in.auxiliaryLoadOhm`.
          m3MpptLoadOhm_ = in.auxiliaryLoadOhm;
          // Q1 ya se regulo en START2; activar P&O sin aplicar otro paso dt.
          // [Guía] Coordina P&O y PI de tensión de PV en M3. Aquí intervienen selección del control de tensión PV
          // [Guía] y intervalo efectivo del control en s.
          runM3Mppt(in, pvVoltageControl_ ? 0.0f : dt);
        }
        // [Guía] Comprueba `timedOut(in)`. Si se cumple, ejecuta `fail("Q2_START_TIMEOUT")`.
        if (timedOut(in)) fail("Q2_START_TIMEOUT");
        // [Guía] Termina esta rama o iteración y retoma la ejecución después del selector o bucle.
        break;
      // Regimen M3: Q1 es MPPT y Q2 es PI de bus; Q3=0. Saturacion de PI2
      // no demuestra seguimiento del MPP ni regulacion exitosa. Las retiradas
      // responden a deficit/permisos reales, no solo al margen de entradaPV.
      // [Guía] PV alimenta bus y batería. Q1 busca potencia PV y Q2 regula la tensión de bus.
      case M3: {
        // [Guía] Da prioridad a la ruta de carga exclusiva cuando el bus queda sin demanda. Inicia el retiro de
        // [Guía] las ramas que regulaban el bus antes de dejar a Q2 en control de corriente.
        if (m2Ready) { enterM2Preparation(in, dt); break; }
        // [Guía] Comprueba `!in.q2Configured`. Si se cumple, sale de esta rama del selector tras realizar la
        // [Guía] acción indicada.
        if (!in.q2Configured) { fail("Q2_CONFIG_LOST"); break; }
        // [Guía] Ante pérdida de PV, intenta mantener el bus con la batería si esta puede descargar. Interrumpe
        // [Guía] el procesamiento del modo de origen.
        if (pvLost) { recoverBattery(in); break; }
        // [Guía] Si la batería deja de admitir carga en esta rama, transfiere la regulación del bus a Q1 mediante
        // [Guía] la entrada a M1.
        if (!chargeAllowed) { enterM1(in); break; }
        // [Guía] Borra la confirmación o acumulación en `chargeMarginLost_` para que no se arrastre al siguiente
        // [Guía] contexto.
        chargeMarginLost_.clear();
        // Descarga con bus alto no demuestra deficit: dejar que Q2 absorba
        // antes de volver a sumar Q3. Los disparos de corriente siguen activos.
        // [Guía] Comprueba `recoverSource || chargeDischargeHold_.held(`. Si se cumple, ejecuta el bloque
        // [Guía] asociado.
        if (recoverSource || chargeDischargeHold_.held(
            // [Guía] Actualiza `>` con `Config::BAT_CHARGE_ABORT_A`.
            batteryCurrentSupervisor_ >= Config::BAT_CHARGE_ABORT_A
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen tensión
              // [Guía] de bus filtrada para supervisión.
              && busSupervisor_ <= Config::EMS_BUS_RECOVER_OFF_V,
            // [Guía] Completa la declaración o expresión con `in.nowUs, Config::BAT_CHARGE_ABORT_US)) {`. Aquí
            // [Guía] intervienen reloj de la muestra en µs.
            in.nowUs, Config::BAT_CHARGE_ABORT_US)) {
          // [Guía] Actualiza `cause` con `recoverSource ? "Q2_RETIRE_BUS_DEFICIT" : "Q2_RETIRE_BAT_DISCHARGE"`.
          const char* cause = recoverSource ? "Q2_RETIRE_BUS_DEFICIT" : "Q2_RETIRE_BAT_DISCHARGE";
          // [Guía] Registra el destino y el instante inicial de una transferencia. Aquí intervienen reloj de la
          // [Guía] muestra en µs y código de diagnóstico de la decisión.
          enterTransfer(RETIRE2, in.nowUs); reason_ = retire2Reason_ = cause; break;
        }
        // [Guía] Comprueba `responsePending`. Si se cumple, ejecuta el bloque asociado.
        if (responsePending) {
          // No dejar que Q2 siga creciendo al95% mientras se confirma que
          // desaparecio la carga de bateria. MPPT sigue atendido durante M3.
          // [Guía] Reinicia PI de bus aplicado a Q2 con el estado inicial dado en los argumentos. Aquí
          // [Guía] intervienen PI de bus aplicado a Q2 y consigna de duty de Q2 (0 a 1).
          pi2_.reset(error2(in), q2_);
          // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
          reason_ = "Q2_NO_RESPONSE_CONFIRM";
        // [Guía] Comprueba `in.batteryV >= Config::BAT_CHARGE_MAX_V`. Si se cumple, ejecuta el bloque asociado.
        } else if (in.batteryV >= Config::BAT_CHARGE_MAX_V
            // [Guía] Añade una alternativa suficiente dentro de la condición compuesta.
            || -in.batteryI >= Config::BAT_CHARGE_SOFT_A) {
          // [Guía] Actualiza `q2_` con `fmaxf(Config::Q2_DUTY_MIN, q2_ - Config::Q2_SLEW_PER_SECOND * dt)`. Este
          // [Guía] dato representa consigna de duty de Q2 (0 a 1).
          q2_ = fmaxf(Config::Q2_DUTY_MIN, q2_ - Config::Q2_SLEW_PER_SECOND * dt);
          // [Guía] Reinicia PI de bus aplicado a Q2 con el estado inicial dado en los argumentos. Aquí
          // [Guía] intervienen PI de bus aplicado a Q2 y consigna de duty de Q2 (0 a 1).
          pi2_.reset(error2(in), q2_);
          // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
          reason_ = "Q2_CHARGE_SOFT_LIMIT";
        // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
        } else {
          // [Guía] Actualiza `q2_` con `pi2_.update(error2(in), dt, Config::Q2_SLEW_PER_SECOND * dt)`. Este dato
          // [Guía] representa consigna de duty de Q2 (0 a 1).
          q2_ = pi2_.update(error2(in), dt, Config::Q2_SLEW_PER_SECOND * dt);
          // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
          reason_ = chargeFlowSeen_
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen
              // [Guía] corriente de batería filtrada para supervisión.
              && batteryCurrentSupervisor_ < chargeIdleCurrent() - Config::BAT_ZERO_CURRENT_A
            // [Guía] Define una de las dos salidas de la expresión condicional.
            ? (lowChargeMargin ? "BUS_PI_Q2_CHARGING_LOW_MARGIN" : "BUS_PI_Q2_CHARGING")
            // [Guía] Define una de las dos salidas de la expresión condicional. Aquí intervienen tensión de bus
            // [Guía] filtrada para supervisión.
            : (lowChargeMargin && busSupervisor_ > Config::EMS_BUS_RECOVER_OFF_V
              // [Guía] Define una de las dos salidas de la expresión condicional.
              ? "BUS_PI_Q2_RECOVER_HIGH_BUS"
              // [Guía] Define una de las dos salidas de la expresión condicional.
              : (!chargeFlowSeen_ && !chargeDriveAvailable(in) ? "Q2_WAIT_DRIVE_MARGIN"
                // [Guía] Define una de las dos salidas de la expresión condicional.
                : "BUS_PI_Q2_NO_CHARGE_YET"));
          // [Guía] Comprueba `q2_ >= Config::Q2_DUTY_MAX`. Si se cumple, ejecuta el bloque asociado. Aquí
          // [Guía] intervienen consigna de duty de Q2 (0 a 1).
          if (q2_ >= Config::Q2_DUTY_MAX
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen tensión
              // [Guía] de bus filtrada para supervisión.
              && busSupervisor_ > Config::EMS_BUS_RECOVER_ON_V)
            // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
            reason_ = "Q2_DUTY_LIMIT_HIGH_BUS";
        }
        // Observar Q2 DESPUES de calcular su salida. Durante su rampa inicial
        // no atribuir a Q1 una caida de potencia que proviene de otra carga.
        // [Guía] Coordina P&O y PI de tensión de PV en M3. Aquí intervienen intervalo efectivo del control en s.
        runM3Mppt(in, dt);
        // [Guía] Termina esta rama o iteración y retoma la ejecución después del selector o bucle.
        break;
      }
      // M4 -> M2: Q1/Q3 ya se apagaron al detectar el corte. Q2 permanece
      // OFF durante el interbloqueo de1ms, sin rampa saliente ni espera USB.
      // [Guía] Retira Q3 en la preparación para dejar solo la rama de carga.
      case RETIRE3_M2:
        // [Guía] Pide apagar la rama indicada con duty cero. El sketch aplica físicamente la consigna.
        q2_ = 0; mpptRunning_ = mpptInitialized_ = false;
        // [Guía] Ante pérdida de PV, intenta mantener el bus con la batería si esta puede descargar. Interrumpe
        // [Guía] el procesamiento del modo de origen.
        if (pvLost) { recoverBattery(in); break; }
        // [Guía] Pide apagar la rama indicada con duty cero. El sketch aplica físicamente la consigna.
        q1_ = q3_ = 0;
        // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
        reason_ = "M2_RETIRE_Q3_INTERLOCK";
        // [Guía] Comprueba `!in.busCurrentValid || !busLoadWindow_.ready`. Si se cumple, ejecuta el bloque
        // [Guía] asociado.
        if (!in.busCurrentValid || !busLoadWindow_.ready
            // [Guía] Añade una alternativa suficiente dentro de la condición compuesta.
            || m2Wake_.held(fabsf(m2BusDemand(in)) >= Config::M2_BUS_WAKE_A,
            // [Guía] Completa la declaración o expresión con `in.nowUs, Config::M2_BUS_WAKE_US)) {`. Aquí
            // [Guía] intervienen reloj de la muestra en µs.
            in.nowUs, Config::M2_BUS_WAKE_US)) {
          // [Guía] Reinicia PI de bus aplicado a Q3 con el estado inicial dado en los argumentos. Aquí
          // [Guía] intervienen PI de bus aplicado a Q3 y consigna de duty de Q3 (0 a 1).
          pi3_.reset(error3(in), q3_); enterM4(in);
          // [Guía] Actualiza `m2Cooldown_` con `true; m2CooldownSince_ = in.nowUs`.
          m2Cooldown_ = true; m2CooldownSince_ = in.nowUs;
          // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
          reason_ = lastSourceEvent_ = "M2_CANCELLED_BUS_DEMAND"; break;
        }
        // [Guía] Comprueba `uint32_t(in.nowUs - m2TransitionSince_) >= Config::BAT_INTERLOCK_US`. Si se cumple,
        // [Guía] ejecuta el bloque asociado. Aquí intervienen reloj de la muestra en µs.
        if (uint32_t(in.nowUs - m2TransitionSince_) >= Config::BAT_INTERLOCK_US) {
          // [Guía] Precarga el lazo de Q2 y establece el estado M2 de carga con referencia de corriente. Aquí
          // [Guía] intervienen intervalo efectivo del control en s y permiso de carga de batería.
          startM2Current(in, dt, chargeAllowed);
        // [Guía] Comprueba `uint32_t(in.nowUs - m2TransitionSince_) >= Config::M2_PREP_TIMEOUT_US`. Si se cumple,
        // [Guía] ejecuta el bloque asociado. Aquí intervienen reloj de la muestra en µs.
        } else if (uint32_t(in.nowUs - m2TransitionSince_) >= Config::M2_PREP_TIMEOUT_US)
          // [Guía] Registra el motivo del fallo y retira las consignas de PWM para que el sketch las apague
          // [Guía] físicamente.
          fail("M2_Q3_RETIRE_TIMEOUT");
        // [Guía] Termina esta rama o iteración y retoma la ejecución después del selector o bucle.
        break;
      // [Guía] PV carga la batería. Q2 regula Ibat y el bus queda sin lazo de tensión.
      case M2: {
        // [Guía] Pide apagar la rama indicada con duty cero. El sketch aplica físicamente la consigna.
        q3_ = 0; mpptRunning_ = mpptInitialized_ = false;
        // [Guía] Comprueba `!in.q2Configured`. Si se cumple, sale de esta rama del selector tras realizar la
        // [Guía] acción indicada.
        if (!in.q2Configured) { fail("Q2_CONFIG_LOST"); break; }
        // [Guía] Comprueba `pvLost`. Si se cumple, sale de esta rama del selector tras realizar la acción
        // [Guía] indicada. Aquí intervienen pérdida confirmada de PV.
        if (pvLost) { q2_ = 0; recoverBattery(in); break; }
        // [Guía] Comprueba `!in.busCurrentValid || !busLoadWindow_.ready`. Si se cumple, ejecuta el bloque
        // [Guía] asociado.
        if (!in.busCurrentValid || !busLoadWindow_.ready) {
          // [Guía] Sale de la estrategia de carga exclusiva porque la corriente de bus ya no permite decidir con
          // [Guía] confianza si la demanda sigue ausente.
          returnFromM2(in, "M2_BUS_SENSOR_RETURN"); break;
        }
        // [Guía] Comprueba `m2Wake_.held(fabsf(m2BusDemand(in)) >= Config::M2_BUS_WAKE_A,`. Si se cumple, ejecuta
        // [Guía] el bloque asociado.
        if (m2Wake_.held(fabsf(m2BusDemand(in)) >= Config::M2_BUS_WAKE_A,
            // [Guía] Completa la declaración o expresión con `in.nowUs, Config::M2_BUS_WAKE_US)) {`. Aquí
            // [Guía] intervienen reloj de la muestra en µs.
            in.nowUs, Config::M2_BUS_WAKE_US)) {
          // [Guía] Inicia la recuperación de la regulación de tensión tras confirmar que volvió la demanda
          // [Guía] externa del bus.
          returnFromM2(in, "M2_BUS_LOAD_RETURN"); break;
        }
        // [Guía] Pide apagar la rama indicada con duty cero. El sketch aplica físicamente la consigna.
        q1_ = 0;
        // [Guía] Comprueba `!chargeAllowed`. Si se cumple, ejecuta el bloque asociado. Aquí intervienen permiso
        // [Guía] de carga de batería.
        if (!chargeAllowed) {
          // SOC lleno o permiso de carga retirado: no volver a descargar solo
          // porque cese la carga. Bus sin demanda: permanecer M2 con Q2 OFF.
          // [Guía] Pide apagar la rama indicada con duty cero. El sketch aplica físicamente la consigna.
          q2_ = 0; m2Current_.stop(); reason_ = "M2_CHARGE_PERMISSION_HOLD";
        // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
        } else {
          // [Guía] Actualiza `q2_` con `m2Current_.update(in.batteryI, in.batteryV, dt)`. Este dato representa
          // [Guía] consigna de duty de Q2 (0 a 1).
          q2_ = m2Current_.update(in.batteryI, in.batteryV, dt);
          // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
          reason_ = m2Current_.reason();
        }
        // [Guía] Termina esta rama o iteración y retoma la ejecución después del selector o bucle.
        break;
      }
      // Volver a alimentar bus: retirar Q2, confirmar fin de carga y respetar
      // el interbloqueo existente antes de recuperar Q3/M4 o arrancar M1.
      // [Guía] Restablece la ruta de regulación de bus al salir de M2.
      case RETURN_M2:
        // [Guía] Pide apagar la rama indicada con duty cero. El sketch aplica físicamente la consigna.
        q1_ = q3_ = 0; mpptRunning_ = mpptInitialized_ = false;
        // [Guía] Actualiza `q2_` con `approach(q2_, 0, Config::Q2_SLEW_PER_SECOND * dt)`. Este dato representa
        // [Guía] consigna de duty de Q2 (0 a 1).
        q2_ = approach(q2_, 0, Config::Q2_SLEW_PER_SECOND * dt);
        // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
        reason_ = "M2_RETIRE_Q2_RETURN_BUS";
        // [Guía] Comprueba `q2_ == 0 && zeroCurrent_.held(batteryWindow_.ready`. Si se cumple, ejecuta el bloque
        // [Guía] asociado. Aquí intervienen consigna de duty de Q2 (0 a 1).
        if (q2_ == 0 && zeroCurrent_.held(batteryWindow_.ready
            // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
            && batteryWindow_.mean >= -Config::BAT_ZERO_CURRENT_A
            // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
            && batteryWindow_.mean < Config::BAT_DISCHARGE_SOFT_A,
            // [Guía] Completa la declaración o expresión con `in.nowUs, Config::BAT_ZERO_HOLD_US)) {`. Aquí
            // [Guía] intervienen reloj de la muestra en µs.
            in.nowUs, Config::BAT_ZERO_HOLD_US)) {
          // [Guía] Actualiza `m2Cooldown_` con `true; m2CooldownSince_ = in.nowUs`.
          m2Cooldown_ = true; m2CooldownSince_ = in.nowUs;
          // [Guía] Comprueba `dischargeAllowed && in.q3Configured`. Si se cumple, ejecuta `recoverBattery(in)`.
          // [Guía] Aquí intervienen permiso de descarga de batería.
          if (dischargeAllowed && in.q3Configured) recoverBattery(in);
          // [Guía] Comprueba `solarAvailable(in) && in.pvConfigured`. Si se cumple, ejecuta `startSolar(in)`.
          else if (solarAvailable(in) && in.pvConfigured) startSolar(in);
          // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
          else wait("M2_RETURN_WAIT_SOURCE");
        // [Guía] Comprueba `uint32_t(in.nowUs - m2TransitionSince_) >= Config::M2_RETURN_TIMEOUT_US`. Si se
        // [Guía] cumple, ejecuta el bloque asociado. Aquí intervienen reloj de la muestra en µs.
        } else if (uint32_t(in.nowUs - m2TransitionSince_) >= Config::M2_RETURN_TIMEOUT_US)
          // [Guía] Registra el motivo del fallo y retira las consignas de PWM para que el sketch las apague
          // [Guía] físicamente.
          fail("M2_RETURN_TIMEOUT");
        // [Guía] Termina esta rama o iteración y retoma la ejecución después del selector o bucle.
        break;
      // Retirada inversa: bajar Q2, esperar corriente compatible y solo despues
      // seleccionar Q3/M1/WAIT. Timeout evita quedar indefinidamente en traslado.
      // [Guía] Selecciona el tratamiento previsto para `RETIRE2` dentro del selector.
      case RETIRE2:
        // [Guía] Ante pérdida de PV, intenta mantener el bus con la batería si esta puede descargar. Interrumpe
        // [Guía] el procesamiento del modo de origen.
        if (pvLost) { recoverBattery(in); break; }
        // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
        reason_ = retire2Reason_;
        // [Guía] Actualiza `q2_` con `approach(q2_, 0, Config::Q2_SLEW_PER_SECOND * dt)`. Este dato representa
        // [Guía] consigna de duty de Q2 (0 a 1).
        q2_ = approach(q2_, 0, Config::Q2_SLEW_PER_SECOND * dt);
        // Cesar carga y confirmar intervalo sin Q2 antes de entregar Q3.
        // [Guía] Comprueba `q2_ == 0 && zeroCurrent_.held(batteryWindow_.ready`. Si se cumple, ejecuta el bloque
        // [Guía] asociado. Aquí intervienen consigna de duty de Q2 (0 a 1).
        if (q2_ == 0 && zeroCurrent_.held(batteryWindow_.ready
            // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
            && batteryWindow_.mean - idleCurrent >= -Config::BAT_ZERO_CURRENT_A
            // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
            && batteryWindow_.mean < Config::BAT_DISCHARGE_SOFT_A,
            // [Guía] Completa la declaración o expresión con `in.nowUs, Config::BAT_ZERO_HOLD_US)) {`. Aquí
            // [Guía] intervienen reloj de la muestra en µs.
            in.nowUs, Config::BAT_ZERO_HOLD_US)) {
          // [Guía] Comprueba `dischargeAllowed`. Si se cumple, ejecuta `enterStart3(in)`. Aquí intervienen
          // [Guía] permiso de descarga de batería.
          if (dischargeAllowed) enterStart3(in);
          // [Guía] Comprueba `!pvLost`. Si se cumple, ejecuta `enterM1(in)`. Aquí intervienen pérdida confirmada
          // [Guía] de PV.
          else if (!pvLost) enterM1(in);
          // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
          else wait("WAIT_SOURCE");
        }
        // [Guía] Comprueba `timedOut(in)`. Si se cumple, ejecuta `fail("REVERSE_TRANSFER_TIMEOUT")`.
        if (timedOut(in)) fail("REVERSE_TRANSFER_TIMEOUT");
        // [Guía] Termina esta rama o iteración y retoma la ejecución después del selector o bucle.
        break;
      // [Guía] Atiende los valores que no tienen una rama específica en el selector.
      default: break;
    }
    // Verificar tambien una propuesta nueva antes de que el sketch la aplique.
    // [Guía] Comprueba `q1_ > 0 && (!in.pvConfigured || !isfinite(in.pvBusLimit)`. Si se cumple, ejecuta el
    // [Guía] bloque asociado. Aquí intervienen consigna de duty de Q1 (0 a 1).
    if (q1_ > 0 && (!in.pvConfigured || !isfinite(in.pvBusLimit)
        // [Guía] Añade una alternativa suficiente dentro de la condición compuesta.
        || in.pvBusLimit <= Config::BUS_REFERENCE_V
        // [Guía] Añade una alternativa suficiente dentro de la condición compuesta.
        || in.pvBusLimit > Config::BAT_TEST_BUS_TRIP_V)) fail("PV_CONFIG_LOST");
    // [Guía] Comprueba `confirmedPvBus`. Si se cumple, ejecuta `fail("PV_BUS_LIMIT")`.
    else if (confirmedPvBus) fail("PV_BUS_LIMIT");
    // [Guía] Comprueba `q2_ > 0 && q3_ > 0`. Si se cumple, ejecuta `fail("Q2_Q3_INTERLOCK")`. Aquí intervienen
    // [Guía] consigna de duty de Q2 (0 a 1) y consigna de duty de Q3 (0 a 1).
    if (q2_ > 0 && q3_ > 0) fail("Q2_Q3_INTERLOCK");
    // [Guía] Comprueba `(q2_ > 0 || q3_ > 0) && !batteryLinkConfirmed_`. Si se cumple, ejecuta el bloque
    // [Guía] asociado. Aquí intervienen consigna de duty de Q2 (0 a 1) y consigna de duty de Q3 (0 a 1).
    if ((q2_ > 0 || q3_ > 0) && !batteryLinkConfirmed_)
      // [Guía] Registra el motivo del fallo y retira las consignas de PWM para que el sketch las apague
      // [Guía] físicamente.
      fail("BAT_LINK_NOT_ESTIMATED");
    // [Guía] Actualiza `q1_` con `fminf(q1_, Config::Q1_DUTY_MAX)`. Este dato representa consigna de duty de Q1
    // [Guía] (0 a 1).
    q1_ = fminf(q1_, Config::Q1_DUTY_MAX);
    // [Guía] Actualiza `q2_` con `fminf(q2_, Config::Q2_DUTY_MAX)`. Este dato representa consigna de duty de Q2
    // [Guía] (0 a 1).
    q2_ = fminf(q2_, Config::Q2_DUTY_MAX);
    // [Guía] Actualiza `q3_` con `fminf(q3_, Config::Q3_DUTY_MAX)`. Este dato representa consigna de duty de Q3
    // [Guía] (0 a 1).
    q3_ = fminf(q3_, Config::Q3_DUTY_MAX);
  }

  // API DE SOLO LECTURA PARA EL SKETCH Y SU TELEMETRIA.
  // duty1/2/3: propuestas, active: al menos una no nula, faulted: enclavamiento.
  // Los accesores siguientes no ejecutan PI/MPPT ni cambian calificaciones.
  // [Guía] Devuelve consigna de duty de Q1 (0 a 1).
  float duty1() const { return q1_; }
  // [Guía] Devuelve consigna de duty de Q2 (0 a 1).
  float duty2() const { return q2_; }
  // [Guía] Devuelve consigna de duty de Q3 (0 a 1).
  float duty3() const { return q3_; }
  // [Guía] Expone `q1_ > 0 || q2_ > 0 || q3_ > 0` al módulo que consulta active.
  bool active() const { return q1_ > 0 || q2_ > 0 || q3_ > 0; }
  // [Guía] Expone `state_ == FAULT` al módulo que consulta faulted.
  bool faulted() const { return state_ == FAULT; }
  // [Guía] Expone `faultSnapshot_` al módulo que consulta fault snapshot.
  const FaultSnapshot& faultSnapshot() const { return faultSnapshot_; }
  // [Guía] Expone `connectionDiagnostic_` al módulo que consulta connection diagnostic.
  const ConnectionDiagnostic& connectionDiagnostic() const { return connectionDiagnostic_; }
  // [Guía] Expone el estado de la supervisión de conexión de batería para distinguir la comprobación y el
  // [Guía] resultado en el registro.
  const char* connectionStatus() const {
    // [Guía] Comprueba `connectionDiagnostic_.overSamples == 0`. Si se cumple, termina la función en esta rama.
    if (connectionDiagnostic_.overSamples == 0) return "NONE";
    // [Guía] Comprueba `state_ == FAULT`. Si se cumple, termina la función en esta rama. Aquí intervienen estado
    // [Guía] operativo interno.
    if (state_ == FAULT) return "FAULT_LATCHED";
    // [Guía] Entrega `connectionPending_ ? "WAIT_STABLE" : "RECOVERED"` al llamador.
    return connectionPending_ ? "WAIT_STABLE" : "RECOVERED";
  }
  // [Guía] Devuelve indicación de MPPT activo.
  bool mpptRunning() const { return mpptRunning_; }
  // [Guía] Devuelve tensión de batería calificada.
  float batteryControlVoltage() const { return batteryVoltage_; }
  // [Guía] Devuelve contador de actualizaciones.
  uint32_t controlUpdates() const { return updates_; }
  // [Guía] Devuelve contador de periodos de control vencidos.
  uint32_t controlMissed() const { return missedUpdates_; }
  // [Guía] Expone `q3CurrentPending_ && q2_ == 0 && (state_ == START3 || state_ == M5 || state_ == STARTPV ||
  // [Guía] state_ == M4 || stat…` al módulo que consulta corriente pending.
  bool currentPending() const {
    // [Guía] Entrega `q3CurrentPending_ && q2_ == 0` al llamador. Aquí intervienen consigna de duty de Q2 (0 a
    // [Guía] 1).
    return q3CurrentPending_ && q2_ == 0
      // [Guía] Agrupa condiciones alternativas y conjuntas en la prueba en curso. Aquí intervienen estado
      // [Guía] operativo interno.
      && (state_ == START3 || state_ == M5 || state_ == STARTPV || state_ == M4
        // [Guía] Añade una alternativa suficiente dentro de la condición compuesta. Aquí intervienen estado
        // [Guía] operativo interno.
        || state_ == RETIRE3 || state_ == INTERLOCK || state_ == RETIRE2);
  }
  // [Guía] Expone `q3CurrentSamples_` al módulo que consulta corriente samples.
  uint32_t currentSamples() const { return q3CurrentSamples_; }
  // [Guía] Expone `q3CurrentMaximum_` al módulo que consulta corriente maximum.
  float currentMaximum() const { return q3CurrentMaximum_; }
  // [Guía] Devuelve referencia del regulador.
  float reference() const { return reference_; }
  // [Guía] Expone `state_ == RETIRE3_M2 || state_ == M2 || state_ == RETURN_M2` al módulo que consulta m2 path.
  bool m2Path() const { return state_ == RETIRE3_M2 || state_ == M2 || state_ == RETURN_M2; }
  // [Guía] Expone `state_ == M2 ? m2Current_.reference() : 0` al módulo que consulta m2 corriente referencia.
  float m2CurrentReference() const { return state_ == M2 ? m2Current_.reference() : 0; }
  // [Guía] Expone `busLoadWindow_.ready ? busLoadWindow_.mean : 0` al módulo que consulta m2 bus DC corriente.
  float m2BusCurrent() const { return busLoadWindow_.ready ? busLoadWindow_.mean : 0; }
  // [Guía] Expone `m2BusDemand(lastInput_)` al módulo que consulta m2 demand corriente.
  float m2DemandCurrent() const { return m2BusDemand(lastInput_); }
  // [Guía] Expone `busLoadWindow_.ready` al módulo que consulta m2 bus DC corriente listo.
  bool m2BusCurrentReady() const { return busLoadWindow_.ready; }
  // [Guía] Expone `busDemandHistory_.mean()` al módulo que consulta m2 guard corriente.
  float m2GuardCurrent() const { return busDemandHistory_.mean(); }
  // [Guía] Expone `busDemandHistory_.ready()` al módulo que consulta m2 guard listo.
  bool m2GuardReady() const { return busDemandHistory_.ready(); }
  // Diagnostico disponible TAMBIEN en M3/M4 mientras esperan la transferencia.
  // [Guía] Expone `m2Idle_.running ? (uint32_t(lastInput_.nowUs - m2Idle_.start) < Config::M2_BUS_IDLE_US ?
  // [Guía] uint32_t(lastInput_.no…` al módulo que consulta m2 sin demanda milliseconds.
  uint32_t m2IdleMilliseconds() const {
    // [Guía] Entrega `m2Idle_.running ? (uint32_t(lastInput_.nowUs - m2Idle_.start) < Config::M2_BUS_IDLE_US` al
    // [Guía] llamador.
    return m2Idle_.running ? (uint32_t(lastInput_.nowUs - m2Idle_.start) < Config::M2_BUS_IDLE_US
      // [Guía] Define una de las dos salidas de la expresión condicional.
      ? uint32_t(lastInput_.nowUs - m2Idle_.start) : Config::M2_BUS_IDLE_US) / 1000 : 0;
  }
  // [Guía] Expone `m2IdleResets_` al módulo que consulta m2 sin demanda resets.
  uint32_t m2IdleResets() const { return m2IdleResets_; }
  // [Guía] Expone `m2Path() ? reason_ : m2Block_` al módulo que consulta m2 block.
  const char* m2Block() const { return m2Path() ? reason_ : m2Block_; }
  // [Guía] Expone `state_ == RETIRE3_M2 ? "INTERLOCK_Q3_OFF" : state_ == M2 ? "CURRENT_PI" : state_ == RETURN_M2
  // [Guía] ? "RETURN_BUS" :…` al módulo que consulta m2 stage.
  const char* m2Stage() const {
    // [Guía] Entrega `state_ == RETIRE3_M2 ? "INTERLOCK_Q3_OFF"` al llamador. Aquí intervienen estado operativo
    // [Guía] interno.
    return state_ == RETIRE3_M2 ? "INTERLOCK_Q3_OFF"
      // [Guía] Define una de las dos salidas de la expresión condicional. Aquí intervienen estado operativo
      // [Guía] interno.
      : state_ == M2 ? "CURRENT_PI" : state_ == RETURN_M2 ? "RETURN_BUS"
      // [Guía] Define una de las dos salidas de la expresión condicional. Aquí intervienen estado operativo
      // [Guía] interno.
      : (state_ == M3 || state_ == M4) ? (m2IdleLatched_ ? "CONFIRM_IDLE" : "WAIT_IDLE") : "INACTIVE";
  }
  // [Guía] Expone `mpptDiagnostics().power()` al módulo que consulta mppt potencia.
  float mpptPower() const { return mpptDiagnostics().power(); }
  // [Guía] Devuelve tensión de bus filtrada para supervisión.
  float supervisoryBus() const { return busSupervisor_; }
  // [Guía] Devuelve tensión de PV filtrada para supervisión.
  float supervisoryPv() const { return pvSupervisor_; }
  // [Guía] Traduce la etapa interna de M1 a su etiqueta de diagnóstico, incluida la captura, la recuperación y el
  // [Guía] PI.
  const char* m1Status() const {
    // [Guía] Comprueba `state_ == START1`. Si se cumple, termina la función en esta rama. Aquí intervienen estado
    // [Guía] operativo interno.
    if (state_ == START1) return "START_Q1";
    // [Guía] Comprueba `state_ != M1 && state_ != RETIRE2_TO_M1`. Si se cumple, termina la función en esta rama.
    // [Guía] Aquí intervienen estado operativo interno.
    if (state_ != M1 && state_ != RETIRE2_TO_M1) return "INACTIVE";
    // [Guía] Elige la rama correspondiente al valor de `m1Stage_`.
    switch (m1Stage_) {
      // [Guía] Selecciona el tratamiento previsto para `M1_ACQUIRE` dentro del selector.
      case M1_ACQUIRE: return "ACQUIRE";
      // [Guía] Selecciona el tratamiento previsto para `M1_RECOVER` dentro del selector.
      case M1_RECOVER: return "RECOVER";
      // [Guía] Selecciona el tratamiento previsto para `M1_INPUT_RECOVER` dentro del selector.
      case M1_INPUT_RECOVER: return "PV_RECOVER";
      // [Guía] Selecciona el tratamiento previsto para `M1_LIMITED` dentro del selector.
      case M1_LIMITED: return "LIMITED";
      // [Guía] Atiende los valores que no tienen una rama específica en el selector.
      default: return "PI";
    }
  }
  // Diagnostico del mismo bus rapido ya filtrado que recibe el PI de Q1.
  // [Guía] Expone `lastInput_.bus` al módulo que consulta m1 bus DC.
  float m1Bus() const { return lastInput_.bus; }
  // [Guía] Expone `error1(lastInput_)` al módulo que consulta m1 error.
  float m1Error() const { return error1(lastInput_); }
  // [Guía] Expone `pi1_.integral()` al módulo que consulta m1 integral.
  float m1Integral() const { return pi1_.integral(); }
  // e3 e I3 permiten distinguir PI saturado al minimo de un PWM bloqueado.
  // [Guía] Expone `error3(lastInput_)` al módulo que consulta q3 error.
  float q3Error() const { return error3(lastInput_); }
  // [Guía] Expone `pi3_.integral()` al módulo que consulta q3 integral.
  float q3Integral() const { return pi3_.integral(); }
  // Peticion historica para pruebas V39..V47. El sketch V48 usa la politica
  // updateEmsAssist y los observadores m1/m3, no esta peticion enclavada.
  // [Guía] Informa la solicitud histórica de recuperación de bus para ensayos del perfil anterior. La política
  // [Guía] auxiliar vigente toma su decisión por separado. El resultado es una decisión booleana para el
  // [Guía] llamador.
  bool recoveryLoadRequested() const {
    // [Guía] Comprueba `Config::M1_ACQUIRE_AUX_LOAD && state_ == M1 && m1Stage_ != M1_PI`. Si se cumple, ejecuta
    // [Guía] el bloque asociado. Aquí intervienen estado operativo interno y etapa interna de arranque y
    // [Guía] regulación M1.
    if (Config::M1_ACQUIRE_AUX_LOAD && state_ == M1 && m1Stage_ != M1_PI
        // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen consigna de
        // [Guía] duty de Q1 (0 a 1) y indicación de que se requiere recuperar el bus.
        && q1_ > 0 && pvFlowSeen_ && !pvUnavailable_ && busRecovery_) return true;
    // [Guía] Entrega `batteryLinkConfirmed_ && busRecovery_` al llamador. Aquí intervienen estimación confirmada
    // [Guía] de conexión de batería y indicación de que se requiere recuperar el bus.
    return batteryLinkConfirmed_ && busRecovery_
      // [Guía] Agrupa condiciones alternativas y conjuntas en la prueba en curso. Aquí intervienen estado
      // [Guía] operativo interno.
      && (state_ == START4 || state_ == M4 || state_ == M3
        // [Guía] Añade una alternativa suficiente dentro de la condición compuesta. Aquí intervienen estado
        // [Guía] operativo interno.
        || state_ == INTERLOCK || state_ == START2 || state_ == RETIRE3);
  }
  // V48: usar estado interno, no comparar textos de telemetria ni inferir M3
  // a partir de PWM. START/RETIRE/INTERLOCK NO tienen permiso de carga auxiliar.
  // [Guía] Expone `state_ == M3` al módulo que consulta m3 auxiliary mode.
  bool m3AuxiliaryMode() const { return state_ == M3; }
  // [Guía] Expone `state_ == M1` al módulo que consulta m1 auxiliary mode.
  bool m1AuxiliaryMode() const { return state_ == M1; }
  // [Guía] Expone `state_ == M1 && m1Stage_ == M1_PI` al módulo que consulta m1 bus DC pi regulating.
  bool m1BusPiRegulating() const { return state_ == M1 && m1Stage_ == M1_PI; }
  // [Guía] Expone `batteryVoltage_ + Config::EMS_PV_BAT_MARGIN_V` al módulo que consulta carga panel fotovoltaico
  // [Guía] minimum.
  float chargePvMinimum() const { return batteryVoltage_ + Config::EMS_PV_BAT_MARGIN_V; }
  // [Guía] Expone `pvAssistStage_ != PV_ASSIST_OFF` al módulo que consulta panel fotovoltaico assist active.
  bool pvAssistActive() const { return pvAssistStage_ != PV_ASSIST_OFF; }
  // [Guía] Devuelve la referencia PV de conexión de batería o calcula el margen requerido para preparar la ruta
  // [Guía] de carga normal.
  float pvAssistTarget() const {
    // [Guía] Comprueba `hotplugPvSeeking_`. Si se cumple, termina la función en esta rama.
    if (hotplugPvSeeking_) return Config::HOTPLUG_PV_TARGET_V;
    // [Guía] Entrega `chargePvMinimum() + (settings_.strictChargeMargin` al llamador.
    return chargePvMinimum() + (settings_.strictChargeMargin
      // [Guía] Define una de las dos salidas de la expresión condicional.
      ? Config::EMS_CHARGE_PREP_HEADROOM_V : 0.20f);
  }
  // [Guía] Devuelve el tiempo transcurrido en µs desde el inicio de la preparación PV aplicable al caso.
  uint32_t chargePrepElapsedUs() const {
    // [Guía] Comprueba `hotplugPvSeeking_`. Si se cumple, termina la función en esta rama.
    if (hotplugPvSeeking_) return uint32_t(lastInput_.nowUs - hotplugPvSinceUs_);
    // [Guía] Entrega `pvAssistActive() ? uint32_t(lastInput_.nowUs - pvAssistStartUs_)` al llamador.
    return pvAssistActive() ? uint32_t(lastInput_.nowUs - pvAssistStartUs_)
      // [Guía] Define una de las dos salidas de la expresión condicional.
      : pvAssistHotplugPending_ ? uint32_t(lastInput_.nowUs - pvAssistHotplugSinceUs_) : 0;
  }
  // [Guía] Expone `chargeAttempts_` al módulo que consulta carga attempts.
  uint8_t chargeAttempts() const { return chargeAttempts_; }
  // [Guía] Expone `pvAssistStarts_` al módulo que consulta carga prep attempts.
  uint8_t chargePrepAttempts() const { return pvAssistStarts_; }
  // [Guía] Devuelve el último resultado de preparación PV o el motivo de la búsqueda actual por conexión de
  // [Guía] batería.
  const char* chargePrepLast() const {
    // [Guía] Comprueba `hotplugPvSeeking_`. Si se cumple, termina la función en esta rama.
    if (hotplugPvSeeking_) return hotplugPvReason_;
    // [Guía] Entrega `pvAssistResult_ ? pvAssistResult_ : "NONE"` al llamador.
    return pvAssistResult_ ? pvAssistResult_ : "NONE";
  }
  // [Guía] Traduce la etapa de preparación PV a la etiqueta de búsqueda, confirmación, asentamiento, reintento o
  // [Guía] inactividad.
  const char* chargePrepStage() const {
    // [Guía] Comprueba `hotplugPvSeeking_`. Si se cumple, termina la función en esta rama.
    if (hotplugPvSeeking_) return hotplugPvReady_.running ? "CONFIRM_18V4" : "SEEK_18V4";
    // [Guía] Comprueba `pvAssistStage_ == PV_ASSIST_SETTLE`. Si se cumple, termina la función en esta rama.
    if (pvAssistStage_ == PV_ASSIST_SETTLE) return "SETTLE";
    // [Guía] Comprueba `pvAssistStage_ == PV_ASSIST_RAMP`. Si se cumple, termina la función en esta rama.
    if (pvAssistStage_ == PV_ASSIST_RAMP) return pvAssistReady_.running ? "CONFIRM_PV" : "RAISE_PV";
    // [Guía] Comprueba `state_ == M4 && chargeRetry_.running`. Si se cumple, termina la función en esta rama.
    // [Guía] Aquí intervienen estado operativo interno.
    if (state_ == M4 && chargeRetry_.running) return "REQUALIFY_PV";
    // [Guía] Comprueba `state_ == M4 && chargeAtMinimum_.running`. Si se cumple, termina la función en esta rama.
    // [Guía] Aquí intervienen estado operativo interno.
    if (state_ == M4 && chargeAtMinimum_.running) return "CONFIRM_MARGIN";
    // [Guía] Comprueba `state_ == M4 && pvAssistHotplugPending_`. Si se cumple, termina la función en esta rama.
    // [Guía] Aquí intervienen estado operativo interno.
    if (state_ == M4 && pvAssistHotplugPending_) return "WAIT_PREP";
    // [Guía] Entrega `"INACTIVE"` al llamador.
    return "INACTIVE";
  }
  // [Guía] Expone `m1BatteryArmed_` al módulo que consulta m1 batería baseline listo.
  bool m1BatteryBaselineReady() const { return m1BatteryArmed_; }
  // [Guía] Expone `m1BatteryBaseline_` al módulo que consulta m1 batería baseline.
  float m1BatteryBaseline() const { return m1BatteryBaseline_; }
  // [Guía] Calcula cuánto tiempo de evidencia de conexión se ha acumulado en M1 y lo limita al tiempo exigido.
  uint32_t m1BatteryConfirmElapsedUs() const {
    // [Guía] Comprueba `!m1BatteryConnect_.running`. Si se cumple, termina la función en esta rama.
    if (!m1BatteryConnect_.running) return 0;
    // [Guía] Actualiza `elapsed` con `uint32_t(lastInput_.nowUs - m1BatteryConnect_.start)`. Este dato representa
    // [Guía] tiempo transcurrido.
    const uint32_t elapsed = uint32_t(lastInput_.nowUs - m1BatteryConnect_.start);
    // [Guía] Entrega `elapsed < Config::M1_BAT_CONNECT_US ? elapsed : Config::M1_BAT_CONNECT_US` al llamador.
    // [Guía] Aquí intervienen tiempo transcurrido.
    return elapsed < Config::M1_BAT_CONNECT_US ? elapsed : Config::M1_BAT_CONNECT_US;
  }
  // [Guía] Expone `m1BatteryConfirmResets_` al módulo que consulta m1 batería confirm resets.
  uint32_t m1BatteryConfirmResets() const { return m1BatteryConfirmResets_; }
  // [Guía] Expone `m1BatteryConfirmLastReset_` al módulo que consulta m1 batería confirm last reset.
  const char* m1BatteryConfirmLastReset() const { return m1BatteryConfirmLastReset_; }
  // [Guía] Expone `m1Retries_` al módulo que consulta m1 retries.
  uint8_t m1Retries() const { return m1Retries_; }
  // [Guía] Expone `m1Limit_` al módulo que consulta m1 limit.
  const char* m1Limit() const { return m1Limit_; }
  // [Guía] Devuelve estimación confirmada de conexión de batería.
  bool batteryLinkEstimated() const { return batteryLinkConfirmed_; }
  // [Guía] Describe la evidencia de conexión de batería disponible para el modo actual.
  const char* batteryLinkStatus() const {
    // [Guía] Comprueba `batteryLinkConfirmed_`. Si se cumple, termina la función en esta rama. Aquí intervienen
    // [Guía] estimación confirmada de conexión de batería.
    if (batteryLinkConfirmed_) return batteryLinkState_;
    // [Guía] Comprueba `state_ == M1`. Si se cumple, termina la función en esta rama. Aquí intervienen estado
    // [Guía] operativo interno.
    if (state_ == M1) return batteryLinkState_;
    // [Guía] Comprueba `state_ != WAIT && state_ != FAULT && state_ != STOPPED`. Si se cumple, ejecuta el bloque
    // [Guía] asociado. Aquí intervienen estado operativo interno.
    if (state_ != WAIT && state_ != FAULT && state_ != STOPPED)
      // [Guía] Entrega `"WAIT_PASSIVE_STATE"` al llamador.
      return "WAIT_PASSIVE_STATE";
    // [Guía] Entrega `batteryLinkState_` al llamador.
    return batteryLinkState_;
  }
  // [Guía] Devuelve corriente de batería filtrada para supervisión.
  float supervisoryBatteryCurrent() const { return batteryCurrentSupervisor_; }
  // [Guía] Expone `batteryWindow_.ready` al módulo que consulta batería ventana listo.
  bool batteryWindowReady() const { return batteryWindow_.ready; }
  // [Guía] Expone `batteryWindow_.mean` al módulo que consulta batería ventana mean.
  float batteryWindowMean() const { return batteryWindow_.mean; }
  // [Guía] Expone `chargeFlowSeen_` al módulo que consulta carga flow confirmed.
  bool chargeFlowConfirmed() const { return chargeFlowSeen_; }
  // [Guía] Expone `m1BatteryArmed_ ? m1BatteryBaseline_ : 0.0f` al módulo que consulta carga sin demanda
  // [Guía] corriente.
  float chargeIdleCurrent() const { return m1BatteryArmed_ ? m1BatteryBaseline_ : 0.0f; }
  // [Guía] Expone `chargeDriveAvailable(lastInput_)` al módulo que consulta carga drive qualified.
  bool chargeDriveQualified() const { return chargeDriveAvailable(lastInput_); }
  // [Guía] Expone `batteryResponseState_` al módulo que consulta batería response status.
  const char* batteryResponseStatus() const { return batteryResponseState_; }
  // [Guía] Expone `lastSourceEvent_` al módulo que consulta last source event.
  const char* lastSourceEvent() const { return lastSourceEvent_; }
  // [Guía] Expone `pvVoltageControl_ ? pvVoltageMppt_.search() : mppt_` al módulo que consulta mppt diagnostics.
  const MPPTController& mpptDiagnostics() const {
    // [Guía] Entrega `pvVoltageControl_ ? pvVoltageMppt_.search() : mppt_` al llamador. Aquí intervienen
    // [Guía] selección del control de tensión PV y P&O con PI interno de tensión PV.
    return pvVoltageControl_ ? pvVoltageMppt_.search() : mppt_;
  }
  // [Guía] Devuelve selección del control de tensión PV.
  bool voltageMpptControl() const { return pvVoltageControl_; }
  // [Guía] Expone `pvVoltageControl_ ? pvVoltageMppt_.reference() : 0` al módulo que consulta mppt referencia V.
  float mpptReferenceV() const { return pvVoltageControl_ ? pvVoltageMppt_.reference() : 0; }
  // [Guía] Expone `pvVoltageControl_ ? pvVoltageMppt_.stepVolts() : 0` al módulo que consulta mppt paso V.
  float mpptStepV() const { return pvVoltageControl_ ? pvVoltageMppt_.stepVolts() : 0; }
  // [Guía] Expone `mpptDiagnostics().windowActiveUs()` al módulo que consulta mppt ventana active µs.
  uint32_t mpptWindowActiveUs() const { return mpptDiagnostics().windowActiveUs(); }
  // [Guía] Expone `mpptDiagnostics().probing()` al módulo que consulta mppt probing.
  bool mpptProbing() const { return mpptDiagnostics().probing(); }
  // [Guía] Expone `pvVoltageControl_ ? pvVoltageMppt_.decision() : q1_ != mpptTarget_ ? "SLEW_Q1" :
  // [Guía] mppt_.decision()` al módulo que consulta mppt decision.
  const char* mpptDecision() const {
    // [Guía] Entrega `pvVoltageControl_ ? pvVoltageMppt_.decision()` al llamador. Aquí intervienen selección del
    // [Guía] control de tensión PV y P&O con PI interno de tensión PV.
    return pvVoltageControl_ ? pvVoltageMppt_.decision()
      // [Guía] Define una de las dos salidas de la expresión condicional. Aquí intervienen consigna de duty de Q1
      // [Guía] (0 a 1) y buscador P&O en la coordenada de duty.
      : q1_ != mpptTarget_ ? "SLEW_Q1" : mppt_.decision();
  }
  // [Guía] Expone `pvVoltageControl_ ? 0 : mppt_.step()` al módulo que consulta mppt paso.
  float mpptStep() const { return pvVoltageControl_ ? 0 : mppt_.step(); }
  // [Guía] Expone `mpptDiagnostics().deltaPower()` al módulo que consulta mppt delta potencia.
  float mpptDeltaPower() const { return mpptDiagnostics().deltaPower(); }
  // [Guía] Expone `mpptDiagnostics().basePower()` al módulo que consulta mppt base potencia.
  float mpptBasePower() const { return mpptDiagnostics().basePower(); }
  // [Guía] Expone `mpptTarget_` al módulo que consulta mppt target.
  float mpptTarget() const { return mpptTarget_; }
  // [Guía] Expone `mpptDiagnostics().flatSteps()` al módulo que consulta mppt flat pasos.
  uint32_t mpptFlatSteps() const { return mpptDiagnostics().flatSteps(); }
  // [Guía] Expone `mpptDiagnostics().updates()` al módulo que consulta mppt updates.
  uint32_t mpptUpdates() const { return mpptDiagnostics().updates(); }
  // [Guía] Devuelve código de diagnóstico de la decisión.
  const char* reason() const { return reason_; }
  // [Guía] Convierte el estado interno de control en un modo de telemetría. Varias etapas intermedias se
  // [Guía] presentan como una transferencia.
  const char* mode() const {
    // [Guía] Elige la rama correspondiente al valor de `state_`.
    switch (state_) {
      // [Guía] Selecciona el tratamiento previsto para `START1` dentro del selector.
      case START1: return "START_M1";
      // [Guía] Selecciona el tratamiento previsto para `START3` dentro del selector.
      case START3: return q1_ > 0 ? "START_M4" : "START_M5";
      // [Guía] Selecciona el tratamiento previsto para `START4` dentro del selector.
      case START4: return "START_M4";
      // [Guía] Selecciona el tratamiento previsto para `M5` dentro del selector.
      case M5: return "M5";
      // [Guía] Selecciona el tratamiento previsto para `STARTPV` dentro del selector.
      case STARTPV: return "START_M4";
      // [Guía] Selecciona el tratamiento previsto para `M4` dentro del selector.
      case M4: return "M4";
      // [Guía] Selecciona el tratamiento previsto para `RETIRE3` dentro del selector.
      case RETIRE3: case INTERLOCK: case START2: case RETIRE2: case BAT_INTERLOCK:
      // [Guía] Selecciona el tratamiento previsto para `RETIRE2_TO_M1` dentro del selector.
      case RETIRE2_TO_M1: return "TRANSFER";
      // [Guía] Selecciona el tratamiento previsto para `RETIRE3_M2` dentro del selector.
      case RETIRE3_M2: case RETURN_M2: return "TRANSFER";
      // [Guía] Selecciona el tratamiento previsto para `M2` dentro del selector.
      case M2: return "M2";
      // [Guía] Selecciona el tratamiento previsto para `M3` dentro del selector.
      case M3: return "M3";
      // [Guía] Selecciona el tratamiento previsto para `M1` dentro del selector.
      case M1: return "M1";
      // [Guía] Selecciona el tratamiento previsto para `FAULT` dentro del selector.
      case FAULT: return "FAULT";
      // [Guía] Atiende los valores que no tienen una rama específica en el selector.
      default: return "WAIT";
    }
  }
  // Magnitudes finitas/no negativas; action define signo y debe ser +/-1.
  // Esto detecta configuracion incompleta, no prueba estabilidad del lazo.
  // [Guía] Expone `isfinite(kp) && isfinite(ki) && isfinite(kp + ki) && kp >= 0 && ki >= 0 && kp + ki > 0 &&
  // [Guía] (action == 1 || actio…` al módulo que consulta válido gains. Recibe `kp`, `ki`, `action`.
  static bool validGains(float kp, float ki, float action) {
    // [Guía] Entrega `isfinite(kp) && isfinite(ki) && isfinite(kp + ki)` al llamador.
    return isfinite(kp) && isfinite(ki) && isfinite(kp + ki)
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
      && kp >= 0 && ki >= 0 && kp + ki > 0
      // [Guía] Agrupa condiciones alternativas y conjuntas en la prueba en curso.
      && (action == 1 || action == -1);
  }

// [Guía] Desde aquí se declaran los datos y auxiliares internos de la clase.
private:
  // [Guía] Reúne bajo `State` las declaraciones que comparten este tipo o ámbito.
  enum State { WAIT, START1, START3, START4, M5, STARTPV, M4, RETIRE3, INTERLOCK, START2, M3, BAT_INTERLOCK,
               // [Guía] Aporta `RETIRE2, RETIRE2_TO_M1, M1, RETIRE3_M2, M2, RETURN_M2, FAULT, STOPPED }` como
               // [Guía] parte de los parámetros de la operación.
               RETIRE2, RETIRE2_TO_M1, M1, RETIRE3_M2, M2, RETURN_M2, FAULT, STOPPED };
  // [Guía] Reúne bajo `M1Stage` las declaraciones que comparten este tipo o ámbito.
  enum M1Stage { M1_PI, M1_ACQUIRE, M1_RECOVER, M1_INPUT_RECOVER, M1_LIMITED };
  // [Guía] Reúne bajo `PvAssistStage` las declaraciones que comparten este tipo o ámbito.
  enum PvAssistStage { PV_ASSIST_OFF, PV_ASSIST_SETTLE, PV_ASSIST_RAMP };
  // Calificador continuo en us. Cada instancia pertenece a UNA condicion:
  // si esta se hace falsa, clear descarta lo acumulado, no lo deja en pausa.
  // [Guía] Reúne bajo `Hold` las declaraciones que comparten este tipo o ámbito.
  struct Hold {
    // [Guía] Actualiza `running` con `false`.
    bool running = false;
    // [Guía] Actualiza `start` con `0`.
    uint32_t start = 0;
    // [Guía] Limpia el estado de este módulo.
    void clear() { running = false; }
    // [Guía] Comprueba si la condición permanece cierta de manera continua durante el tiempo indicado. El
    // [Guía] resultado es una decisión booleana para el llamador. Recibe `condition`, `now`, `duration`.
    bool held(bool condition, uint32_t now, uint32_t duration) {
      // [Guía] Comprueba `!condition`. Si se cumple, ejecuta el bloque asociado.
      if (!condition) { clear(); return false; }
      // [Guía] Comprueba `!running`. Si se cumple, ejecuta el bloque asociado.
      if (!running) { running = true; start = now; }
      // [Guía] Entrega `uint32_t(now - start) >= duration` al llamador.
      return uint32_t(now - start) >= duration;
    }
  };
  // Media por bloques sin buffer ni delay. Los picos siguen disponibles
  // para los disparos raw; la ventana estima aporte o falta de corriente neta.
  // [Guía] Reúne bajo `CurrentWindow` las declaraciones que comparten este tipo o ámbito.
  struct CurrentWindow {
    // [Guía] Actualiza `active` con `false, ready = false`.
    bool active = false, ready = false;
    // [Guía] Actualiza `since` con `0, count = 0`.
    uint32_t since = 0, count = 0;
    // [Guía] Actualiza `sum` con `0, mean = 0`.
    float sum = 0, mean = 0;
    // [Guía] Reinicia el estado de este módulo.
    void reset() { active = ready = false; sum = mean = 0; count = 0; }
    // [Guía] Incorpora la medida en el estado de este módulo. El resultado es una decisión booleana para el
    // [Guía] llamador. Recibe `current`, `now`, `windowUs`, `minSamples`.
    bool observe(float current, uint32_t now,
        // [Guía] Actualiza `windowUs` con `Config::BAT_CURRENT_WINDOW_US, uint32_t minSamples = 10) {`.
        uint32_t windowUs = Config::BAT_CURRENT_WINDOW_US, uint32_t minSamples = 10) {
      // [Guía] Comprueba `!isfinite(current)`. Si se cumple, ejecuta el bloque asociado.
      if (!isfinite(current)) { reset(); return false; }
      // [Guía] Comprueba `!active`. Si se cumple, ejecuta el bloque asociado.
      if (!active) { active = true; since = now; }
      // [Guía] Acumula en `sum` mediante `current`.
      sum += current;
      // [Guía] Suma una unidad a `count` para contabilizar el evento de esta rama.
      ++count;
      // [Guía] Comprueba `uint32_t(now - since) >= windowUs && count >= minSamples`. Si se cumple, ejecuta el
      // [Guía] bloque asociado.
      if (uint32_t(now - since) >= windowUs && count >= minSamples) {
        // [Guía] Actualiza `mean` con `sum / count`.
        mean = sum / count;
        // [Guía] Actualiza `ready` con `true`.
        ready = true;
        // [Guía] Actualiza `sum` con `0; count = 0; since = now`.
        sum = 0; count = 0; since = now;
        // [Guía] Entrega `true` al llamador.
        return true;
      }
      // [Guía] Entrega `false` al llamador.
      return false;
    }
  };
  // Memoria acotada de bloques completos: no volver a insertar la misma
  // media en cada tick del PI ni acumular evidencia a traves de un hueco ADC.
  // [Guía] Reúne bajo `BusDemandHistory` las declaraciones que comparten este tipo o ámbito.
  struct BusDemandHistory {
    // [Guía] Completa la declaración o expresión con `float values[Config::M2_BUS_GUARD_WINDOWS] = {}`.
    float values[Config::M2_BUS_GUARD_WINDOWS] = {};
    // [Guía] Actualiza `count` con `0, next = 0`.
    uint8_t count = 0, next = 0;
    // [Guía] Reinicia el estado de este módulo.
    void reset() { count = next = 0; }
    // [Guía] Inserta una nueva ventana de corriente en el historial circular y avanza la posición de escritura.
    // [Guía] Recibe `value`.
    void push(float value) {
      // [Guía] Actualiza `values[next]` con `value`.
      values[next] = value;
      // [Guía] Actualiza `next` con `(next + 1) % Config::M2_BUS_GUARD_WINDOWS`.
      next = (next + 1) % Config::M2_BUS_GUARD_WINDOWS;
      // [Guía] Comprueba `count < Config::M2_BUS_GUARD_WINDOWS`. Si se cumple, ejecuta `++count`.
      if (count < Config::M2_BUS_GUARD_WINDOWS) ++count;
    }
    // [Guía] Expone `count == Config::M2_BUS_GUARD_WINDOWS` al módulo que consulta listo.
    bool ready() const { return count == Config::M2_BUS_GUARD_WINDOWS; }
    // [Guía] Promedia los bloques de corriente disponibles en el historial que protege la detección de demanda.
    float mean() const {
      // [Guía] Actualiza `sum` con `0`.
      float sum = 0;
      // [Guía] Controla las iteraciones mediante `uint8_t i = 0; i < count; ++i) sum += values[i];`.
      for (uint8_t i = 0; i < count; ++i) sum += values[i];
      // [Guía] Entrega `count ? sum / count : 0` al llamador.
      return count ? sum / count : 0;
    }
  };
  // Limitador simetrico: no sobrepasar el destino ni recorrer mas de step.
  // Con step=slew*dt implementa rampas sin delay y sin escalones instantaneos.
  // [Guía] Expone `to > from ? fminf(to, from + step) : fmaxf(to, from - step)` al módulo que consulta approach.
  // [Guía] Recibe `from`, `to`, `step`.
  static float approach(float from, float to, float step) {
    // [Guía] Entrega `to > from ? fminf(to, from + step) : fmaxf(to, from - step)` al llamador.
    return to > from ? fminf(to, from + step) : fmaxf(to, from - step);
  }
  // La base es opcional para diagnostico/respuesta Q2, no un permiso de
  // conexion. No cambia zeroADC, medidas fisicas, umbral80mA ni SOC.
  // [Guía] Limpia M1 batería confirmation. Recibe `reason`.
  void clearM1BatteryConfirmation(const char* reason) {
    // Contar solo un segundo que realmente habia empezado, no el reposo.
    // [Guía] Comprueba `m1BatteryConnect_.running`. Si se cumple, ejecuta el bloque asociado.
    if (m1BatteryConnect_.running) {
      // [Guía] Comprueba `m1BatteryConfirmResets_ != UINT32_MAX`. Si se cumple, ejecuta
      // [Guía] `++m1BatteryConfirmResets_`.
      if (m1BatteryConfirmResets_ != UINT32_MAX) ++m1BatteryConfirmResets_;
      // [Guía] Actualiza `m1BatteryConfirmLastReset_` con `reason`.
      m1BatteryConfirmLastReset_ = reason;
    }
    // [Guía] Borra la confirmación o acumulación en `m1BatteryConnect_` para que no se arrastre al siguiente
    // [Guía] contexto.
    m1BatteryConnect_.clear(); m1BatteryPolarity_ = 0;
  }
  // [Guía] Reinicia M1 batería detection. Recibe `reason`.
  void resetM1BatteryDetection(const char* reason = "DETECTOR_RESET") {
    // [Guía] Actualiza `m1BatteryArmed_` con `false`.
    m1BatteryArmed_ = false;
    // [Guía] Actualiza `m1BatteryCandidate_` con `m1BatteryQualified_ = false`.
    m1BatteryCandidate_ = m1BatteryQualified_ = false;
    // [Guía] Actualiza `m1BatteryPolarity_` con `0`.
    m1BatteryPolarity_ = 0;
    // [Guía] Actualiza `m1BatteryBaseline_` con `0`.
    m1BatteryBaseline_ = 0;
    // [Guía] Borra la confirmación o acumulación en `m1BatteryQuiet_` para que no se arrastre al siguiente
    // [Guía] contexto.
    m1BatteryQuiet_.clear(); clearM1BatteryConfirmation(reason);
  }
  // V61: medir corriente y autorizar un actuador son decisiones distintas.
  // Media raw50ms y supervisor del mismo signo fuera de80mA durante1s
  // confirman FLUJO, sin esperar bus20V, PV18.4V ni permiso SOC/PWM perfecto
  // durante ese segundo. Los permisos SI se exigen al entregar M4, sin
  // borrar evidencia de corriente valida cuando alguno falta momentaneamente.
  // Perder muestras, corriente segura o signo neto SI reinicia el contador.
  // WAIT y las protecciones raw mantienen sus criterios independientes.
  // [Guía] Usa la corriente de batería y su evidencia temporal para discriminar la conexión durante M1 de
  // [Guía] fluctuaciones de la medida. Recibe `in`, `batteryGood`, `busGood`, `gap`.
  void qualifyM1Battery(const Input& in, bool batteryGood, bool busGood, uint32_t gap) {
    // [Guía] Actualiza `m1BatteryCandidate_` con `m1BatteryQualified_ = false`.
    m1BatteryCandidate_ = m1BatteryQualified_ = false;
    // [Guía] Comprueba `state_ != M1`. Si se cumple, termina la función en esta rama. Aquí intervienen estado
    // [Guía] operativo interno.
    if (state_ != M1) return;
    // El bus raw alto se atiende por BUS_TEST_OV/PV_BUS_LIMIT arriba; usarlo
    // tambien para borrar el segundo hacia que ruido/permisos mantuvieran M1.
    // No se elimina ningun corte ni se acepta una medida invalida.
    // [Guía] Actualiza `ready` con `batteryGood && busGood`.
    const bool ready = batteryGood && busGood
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen consigna de duty
      // [Guía] de Q1 (0 a 1) y consigna de duty de Q2 (0 a 1).
      && q1_ >= Config::Q1_DUTY_MIN && q2_ == 0 && q3_ == 0;
    // [Guía] Comprueba `!ready || gap == 0 || gap > Config::BAT_CONNECTION_MAX_GAP_US`. Si se cumple, ejecuta el
    // [Guía] bloque asociado. Aquí intervienen separación temporal entre muestras.
    if (!ready || gap == 0 || gap > Config::BAT_CONNECTION_MAX_GAP_US) {
      // [Guía] Actualiza `batteryLinkConfirmed_` con `false`. Este dato representa estimación confirmada de
      // [Guía] conexión de batería.
      batteryLinkConfirmed_ = false;
      // [Guía] Actualiza `cause` con `!ready ? (!batteryGood ? "BAT_MEASURE"`.
      const char* cause = !ready ? (!batteryGood ? "BAT_MEASURE"
        // [Guía] Define una de las dos salidas de la expresión condicional. Aquí intervienen validez de la
        // [Guía] medición de bus.
        : !busGood ? "BUS_MEASURE" : "ACTUATORS") : "ADC_GAP";
      // [Guía] Invoca `resetM1BatteryDetection` con los argumentos de esta expresión.
      resetM1BatteryDetection(cause); batteryLinkState_ = "M1_WAIT_BAT_READY"; return;
    }
    // [Guía] Actualiza `windowReady` con `batteryWindow_.ready && batteryCurrentReady_`.
    const bool windowReady = batteryWindow_.ready && batteryCurrentReady_;
    // Reposo opcional SOLO diagnostico/referencia de respuesta de Q2.
    // No bloquear la deteccion de una bateria ya circulando2A por no haber
    // conseguido aprender cero antes; tampoco modificar zeroADC ni SOC.
    // [Guía] Comprueba `!m1BatteryArmed_`. Si se cumple, ejecuta el bloque asociado.
    if (!m1BatteryArmed_) {
      // [Guía] Actualiza `baselineCandidate` con `windowReady`.
      const bool baselineCandidate = windowReady
        // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
        && fabsf(batteryWindow_.mean) <= Config::BAT_CONNECT_CURRENT_A
        // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen corriente de
        // [Guía] batería filtrada para supervisión.
        && fabsf(batteryCurrentSupervisor_) <= Config::BAT_CONNECT_CURRENT_A;
      // [Guía] Comprueba `!m1BatteryQuiet_.running`. Si se cumple, ejecuta el bloque asociado.
      if (!m1BatteryQuiet_.running)
        // [Guía] Actualiza `m1BatteryBaseline_` con `0.5f * (batteryWindow_.mean + batteryCurrentSupervisor_)`.
        m1BatteryBaseline_ = 0.5f * (batteryWindow_.mean + batteryCurrentSupervisor_);
      // No aceptar un valor que cambia durante la confirmacion como reposo.
      // [Guía] Actualiza `quiet` con `baselineCandidate`.
      const bool quiet = baselineCandidate
        // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
        && fabsf(batteryWindow_.mean - m1BatteryBaseline_) <= Config::M1_BAT_BASELINE_BAND_A
        // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen corriente de
        // [Guía] batería filtrada para supervisión.
        && fabsf(batteryCurrentSupervisor_ - m1BatteryBaseline_) <= Config::M1_BAT_BASELINE_BAND_A;
      // [Guía] Actualiza `m1BatteryArmed_` con `m1BatteryQuiet_.held(quiet, in.nowUs, Config::M1_BAT_QUIET_US)`.
      m1BatteryArmed_ = m1BatteryQuiet_.held(quiet, in.nowUs, Config::M1_BAT_QUIET_US);
    }
    // [Guía] Actualiza `currentSafe` con `in.batteryI < Config::BAT_DISCHARGE_SOFT_A`.
    const bool currentSafe = in.batteryI < Config::BAT_DISCHARGE_SOFT_A
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
      && in.batteryI > -Config::BAT_CHARGE_SOFT_A
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen corriente de
      // [Guía] batería filtrada para supervisión.
      && batteryCurrentSupervisor_ < Config::BAT_DISCHARGE_SOFT_A
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen corriente de
      // [Guía] batería filtrada para supervisión.
      && batteryCurrentSupervisor_ > -Config::BAT_CHARGE_SOFT_A
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
      && in.batteryIRaw < Config::BAT_DISCHARGE_SOFT_A
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
      && in.batteryIRaw > -Config::BAT_CHARGE_SOFT_A;
    // [Guía] Actualiza `positive` con `windowReady && currentSafe`.
    const bool positive = windowReady && currentSafe
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
      && batteryWindow_.mean > Config::BAT_CONNECT_CURRENT_A
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen corriente de
      // [Guía] batería filtrada para supervisión.
      && batteryCurrentSupervisor_ > Config::BAT_CONNECT_CURRENT_A;
    // [Guía] Actualiza `negative` con `windowReady && currentSafe`.
    const bool negative = windowReady && currentSafe
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
      && batteryWindow_.mean < -Config::BAT_CONNECT_CURRENT_A
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen corriente de
      // [Guía] batería filtrada para supervisión.
      && batteryCurrentSupervisor_ < -Config::BAT_CONNECT_CURRENT_A;
    // [Guía] Actualiza `m1BatteryCandidate_` con `positive || negative`.
    m1BatteryCandidate_ = positive || negative;
    // [Guía] Actualiza `polarity` con `positive ? 1 : negative ? -1 : 0`.
    const int polarity = positive ? 1 : negative ? -1 : 0;
    // [Guía] Comprueba `polarity != m1BatteryPolarity_`. Si se cumple, ejecuta el bloque asociado.
    if (polarity != m1BatteryPolarity_) {
      // [Guía] Invoca `clearM1BatteryConfirmation` con los argumentos de esta expresión.
      clearM1BatteryConfirmation(!currentSafe ? "CURRENT_MARGIN"
        // [Guía] Define una de las dos salidas de la expresión condicional.
        : !windowReady ? "CURRENT_WINDOW" : polarity != 0 ? "SIGN_CHANGE"
        // [Guía] Define una de las dos salidas de la expresión condicional.
        : fabsf(batteryWindow_.mean) <= Config::BAT_CONNECT_CURRENT_A ? "MEAN_BELOW_THRESHOLD"
        // [Guía] Define una de las dos salidas de la expresión condicional. Aquí intervienen corriente de batería
        // [Guía] filtrada para supervisión.
        : fabsf(batteryCurrentSupervisor_) <= Config::BAT_CONNECT_CURRENT_A ? "SUPERVISOR_BELOW_THRESHOLD"
        // [Guía] Define una de las dos salidas de la expresión condicional.
        : "FILTER_SIGN_CONFLICT");
    }
    // [Guía] Actualiza `m1BatteryPolarity_` con `polarity`.
    m1BatteryPolarity_ = polarity;
    // [Guía] Actualiza `m1BatteryQualified_` con `m1BatteryConnect_.held(polarity != 0`.
    m1BatteryQualified_ = m1BatteryConnect_.held(polarity != 0,
      // [Guía] Completa la declaración o expresión con `in.nowUs, Config::M1_BAT_CONNECT_US)`. Aquí intervienen
      // [Guía] reloj de la muestra en µs.
      in.nowUs, Config::M1_BAT_CONNECT_US);
    // Reevaluar permisos en CADA muestra: recordar flujo no permite ignorar
    // SOC, configuracion, tension ni perdida real de fuente. El nombre dice
    // exactamente cual falta; PERMISSION generico no permitia distinguirlos.
    // [Guía] Actualiza `blocked` con `!in.socValid ? "M1_BAT_WAIT_SOC_VALID"`.
    const char* blocked = !in.socValid ? "M1_BAT_WAIT_SOC_VALID"
      // [Guía] Define una de las dos salidas de la expresión condicional. Aquí intervienen estado de carga
      // [Guía] estimado.
      : in.soc <= Config::BAT_DISCHARGE_MIN_SOC ? "M1_BAT_WAIT_SOC_MIN"
      // [Guía] Define una de las dos salidas de la expresión condicional.
      : !in.q3Configured || !validGains(settings_.kp3, settings_.ki3, settings_.action3)
        // [Guía] Define una de las dos salidas de la expresión condicional.
        ? "M1_BAT_WAIT_Q3_CONFIG"
      // [Guía] Define una de las dos salidas de la expresión condicional.
      : !batteryVoltageReady_ || in.batteryV < Config::BAT_TEST_MIN_V
          // [Guía] Añade una alternativa suficiente dentro de la condición compuesta.
          || in.batteryV >= Config::BAT_TEST_MAX_V ? "M1_BAT_WAIT_VBAT_RANGE"
      // [Guía] Define una de las dos salidas de la expresión condicional.
      : !solarRetained(in) ? "M1_BAT_WAIT_PV_SOURCE"
      // [Guía] Define una de las dos salidas de la expresión condicional.
      : in.busRaw < Config::BAT_BUS_START_MIN_V ? "M1_BAT_WAIT_BUS_READY" : nullptr;
    // [Guía] Comprueba `m1BatteryQualified_ && !blocked && !batteryLinkConfirmed_`. Si se cumple, ejecuta el
    // [Guía] bloque asociado. Aquí intervienen estimación confirmada de conexión de batería.
    if (m1BatteryQualified_ && !blocked && !batteryLinkConfirmed_)
      // [Guía] Actualiza `lastSourceEvent_` con `"M1_BAT_CURRENT_CONFIRMED"`.
      lastSourceEvent_ = "M1_BAT_CURRENT_CONFIRMED";
    // [Guía] Actualiza `batteryLinkConfirmed_` con `m1BatteryQualified_ && !blocked`. Este dato representa
    // [Guía] estimación confirmada de conexión de batería.
    batteryLinkConfirmed_ = m1BatteryQualified_ && !blocked;
    // [Guía] Actualiza `batteryLinkState_` con `m1BatteryQualified_ ? (blocked ? blocked : "M1_FLOW_ESTIMATE")`.
    batteryLinkState_ = m1BatteryQualified_ ? (blocked ? blocked : "M1_FLOW_ESTIMATE")
      // [Guía] Define una de las dos salidas de la expresión condicional.
      : polarity > 0 ? "M1_FLOW_CONFIRM_POS_1S"
      // [Guía] Define una de las dos salidas de la expresión condicional.
      : polarity < 0 ? "M1_FLOW_CONFIRM_NEG_1S" : "M1_WATCH_DISCHARGE";
  }
  // Filtrar Vbat para PERMISOS; su raw todavia interviene en protecciones.
  // Perder validez borra la base. Un hueco grande toma la medida fresca y
  // exige nueva calificacion cuando no hay actuadores activos.
  // [Guía] Califica la disponibilidad del puerto de batería y prepara la tensión de control que utilizan las
  // [Guía] ramas de batería. Recibe `in`, `valid`, `gap`.
  void qualifyBatteryVoltage(const Input& in, bool valid, uint32_t gap) {
    // [Guía] Comprueba `!valid`. Si se cumple, ejecuta el bloque asociado.
    if (!valid) {
      // [Guía] Actualiza `batteryVoltageInitialized_` con `batteryVoltageReady_ = false`.
      batteryVoltageInitialized_ = batteryVoltageReady_ = false;
      // [Guía] Borra la confirmación o acumulación en `batteryVoltageHold_` para que no se arrastre al siguiente
      // [Guía] contexto.
      batteryVoltageHold_.clear();
      // [Guía] Finaliza aquí la función y devuelve el control al llamador.
      return;
    }
    // No conservar un permiso basado en datos viejos despues de un hueco.
    // [Guía] Comprueba `!batteryVoltageInitialized_ || gap == 0`. Si se cumple, ejecuta el bloque asociado. Aquí
    // [Guía] intervienen separación temporal entre muestras.
    if (!batteryVoltageInitialized_ || gap == 0
        // [Guía] Añade una alternativa suficiente dentro de la condición compuesta. Aquí intervienen separación
        // [Guía] temporal entre muestras.
        || gap > Config::BAT_CONNECTION_MAX_GAP_US) {
      // [Guía] Actualiza `batteryVoltage_` con `in.batteryV`. Este dato representa tensión de batería calificada.
      batteryVoltage_ = in.batteryV;
      // [Guía] Actualiza `batteryVoltageInitialized_` con `true`.
      batteryVoltageInitialized_ = true;
      // Durante control un hueco usa la medida fresca, no rearma la espera.
      // El timeout y los limites siguen vigilando ese episodio por separado.
      // [Guía] Comprueba `!active()`. Si se cumple, ejecuta `batteryVoltageReady_ = false`.
      if (!active()) batteryVoltageReady_ = false;
      // [Guía] Borra la confirmación o acumulación en `batteryVoltageHold_` para que no se arrastre al siguiente
      // [Guía] contexto.
      batteryVoltageHold_.clear();
    // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
    } else {
      // [Guía] Actualiza `alpha` con `float(gap) / (Config::BAT_VOLTAGE_FILTER_TAU_US + float(gap))`.
      const float alpha = float(gap) / (Config::BAT_VOLTAGE_FILTER_TAU_US + float(gap));
      // [Guía] Acumula en `batteryVoltage_` mediante `alpha * (in.batteryV - batteryVoltage_)`. Este dato
      // [Guía] representa tensión de batería calificada.
      batteryVoltage_ += alpha * (in.batteryV - batteryVoltage_);
    }
    // [Guía] Actualiza `q3Only` con `Config::BAT_Q3_QUALIFY_CURRENT && q2_ == 0`.
    const bool q3Only = Config::BAT_Q3_QUALIFY_CURRENT && q2_ == 0
      // [Guía] Agrupa condiciones alternativas y conjuntas en la prueba en curso. Aquí intervienen estado
      // [Guía] operativo interno.
      && (state_ == START3 || state_ == M5 || state_ == STARTPV || state_ == M4);
    // En Q3 solo, el supervisor confirma el limite superior por separado.
    // No retirar el permiso por el mismo pico antes de que pueda confirmarse.
    // [Guía] Comprueba `batteryVoltage_ < Config::BAT_TEST_MIN_V`. Si se cumple, ejecuta el bloque asociado. Aquí
    // [Guía] intervienen tensión de batería calificada.
    if (batteryVoltage_ < Config::BAT_TEST_MIN_V
        // [Guía] Agrupa condiciones alternativas y conjuntas en la prueba en curso.
        || (!q3Only && in.batteryV >= Config::BAT_TEST_MAX_V)) {
      // [Guía] Actualiza `batteryVoltageReady_` con `false`.
      batteryVoltageReady_ = false;
      // [Guía] Borra la confirmación o acumulación en `batteryVoltageHold_` para que no se arrastre al siguiente
      // [Guía] contexto.
      batteryVoltageHold_.clear();
    // [Guía] Comprueba `!batteryVoltageReady_`. Si se cumple, ejecuta el bloque asociado.
    } else if (!batteryVoltageReady_) {
      // [Guía] Actualiza `batteryVoltageReady_` con `batteryVoltageHold_.held(`.
      batteryVoltageReady_ = batteryVoltageHold_.held(
        // [Guía] Actualiza `>` con `Config::BAT_TEST_MIN_V + Config::BAT_VOLTAGE_START_MARGIN_V`.
        batteryVoltage_ >= Config::BAT_TEST_MIN_V + Config::BAT_VOLTAGE_START_MARGIN_V,
        // [Guía] Completa la declaración o expresión con `in.nowUs, Config::BAT_VOLTAGE_READY_US)`. Aquí
        // [Guía] intervienen reloj de la muestra en µs.
        in.nowUs, Config::BAT_VOLTAGE_READY_US);
    }
  }
  // Convenciones distintas, intencionadas: Q3 actua sobre bus rapido; Q1 usa
  // el signo de la rama PV ensayada; Q2 usa busSupervisor_ con action negativo.
  // [Guía] Expone `settings_.action3 * (reference_ - in.bus)` al módulo que consulta error3. Recibe `in`.
  float error3(const Input& in) const { return settings_.action3 * (reference_ - in.bus); }
  // [Guía] Calcula el error que utiliza el regulador de Q1 en M1 a partir de la tensión de bus y los ajustes del
  // [Guía] controlador. Recibe `in`.
  float error1(const Input& in) const {
    // Igual que en updateMode1: bus ya tiene el filtro rapido de adquisicion.
    // El filtro EMS de50 ms queda para supervision, no para cerrar este PI.
    // [Guía] Entrega `in.bus - reference_` al llamador. Aquí intervienen tensión rápida del bus en V y referencia
    // [Guía] del regulador.
    return in.bus - reference_;
  }
  // Presencia inicial: tension suficiente O corriente util con tension minima.
  // Un offset de corriente con Vpv=0 no puede crear una fuente solar ficticia.
  // [Guía] Comprueba los criterios de entrada que permiten considerar disponible el puerto PV. Recibe `in`.
  bool solarAvailable(const Input& in) const {
    // [Guía] Entrega `in.pvValid && isfinite(in.pv) && isfinite(in.pvRaw) && isfinite(in.pvI)` al llamador. Aquí
    // [Guía] intervienen tensión del PV en V.
    return in.pvValid && isfinite(in.pv) && isfinite(in.pvRaw) && isfinite(in.pvI)
      // [Guía] Agrupa condiciones alternativas y conjuntas en la prueba en curso.
      && (!pvUnavailable_ || in.pvI > Config::BAT_PV_ON_CURRENT_A)
      // [Guía] Agrupa condiciones alternativas y conjuntas en la prueba en curso. Aquí intervienen tensión del PV
      // [Guía] en V.
      && (in.pv >= Config::BAT_PV_ON_V || (in.pvI > Config::BAT_PV_ON_CURRENT_A
        // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen tensión del PV
        // [Guía] en V.
        && in.pv >= Config::PV_CURRENT_DETECT_MIN_V));
  }
  // Retencion con umbrales OFF menores que ON: histeresis contra oscilacion.
  // pvUnavailable_ recuerda falta de aporte aunque quede tension capacitiva.
  // [Guía] Aplica el criterio de permanencia del PV para evitar que el modo cambie ante una caída breve. Recibe
  // [Guía] `in`.
  bool solarRetained(const Input& in) const {
    // [Guía] Entrega `in.pvValid && isfinite(in.pv) && isfinite(in.pvI)` al llamador. Aquí intervienen tensión
    // [Guía] del PV en V.
    return in.pvValid && isfinite(in.pv) && isfinite(in.pvI)
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
      && !pvUnavailable_
      // [Guía] Agrupa condiciones alternativas y conjuntas en la prueba en curso. Aquí intervienen tensión del PV
      // [Guía] en V.
      && (in.pv > Config::BAT_PV_OFF_V || (in.pvI > Config::BAT_PV_OFF_CURRENT_A
        // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen tensión del PV
        // [Guía] en V.
        && in.pv >= Config::PV_CURRENT_DETECT_MIN_V));
  }
  // [Guía] Calcula el error de bus con el signo de actuación configurado para Q2. Recibe `in`.
  float error2(const Input& in) const {
    // [Guía] Completa la declaración o expresión con `(void)in`.
    (void)in;
    // PI Q2 lento: el supervisor evita que el slew limite cada salto de ruido
    // y frene continuamente la integral. Ganancias y dt permanecen iguales.
    // [Guía] Entrega `settings_.action2 * (Config::BUS_REFERENCE_V - busSupervisor_)` al llamador. Aquí
    // [Guía] intervienen tensión de bus filtrada para supervisión.
    return settings_.action2 * (Config::BUS_REFERENCE_V - busSupervisor_);
  }
  // Indicador de accion esperada para supervisar bateria: Q2*Vpv estima si la
  // orden tendria margen para cargar. No es modelo completo de la topologia.
  // [Guía] Califica el margen eléctrico para activar la ruta de carga cuando el modo lo exige. El resultado es
  // [Guía] una decisión booleana para el llamador. Recibe `in`.
  bool chargeDriveAvailable(const Input& in) const {
    // En M2 no usar el indicador Q2*Vpv>=Vbat: no pertenece a este modo.
    // Esperar respuesta de Ibat cuando hay orden efectiva de carga; la falta
    // de respuesta conserva su confirmacion temporal y retorno existentes.
    // [Guía] Comprueba `state_ == M2`. Si se cumple, ejecuta el bloque asociado. Aquí intervienen estado
    // [Guía] operativo interno.
    if (state_ == M2)
      // [Guía] Entrega `q2_ >= Config::Q2_DUTY_MIN && m2Current_.reference() < -0.2f` al llamador. Aquí
      // [Guía] intervienen consigna de duty de Q2 (0 a 1) y controlador de corriente de Q2 en M2.
      return q2_ >= Config::Q2_DUTY_MIN && m2Current_.reference() < -0.2f
        // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen tensión del PV
        // [Guía] en V.
        && in.pvValid && isfinite(in.pv);
    // Indicador conservador, no modelo garantizado de la etapa: un pico de
    // Vpv no prueba margen de actuacion mientras el duty Q2 aun esta creciendo.
    // [Guía] Entrega `(state_ == M3 || state_ == START2)` al llamador. Aquí intervienen estado operativo interno.
    return (state_ == M3 || state_ == START2)
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen consigna de duty
      // [Guía] de Q2 (0 a 1).
      && q2_ >= Config::Q2_DUTY_MIN
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen tensión del PV
      // [Guía] en V.
      && in.pvValid && isfinite(in.pv) && pvSupervisorReady_
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen consigna de duty
      // [Guía] de Q2 (0 a 1) y tensión del PV en V.
      && q2_ * fminf(in.pv, pvSupervisor_)
        // [Guía] Actualiza `>` con `batteryVoltage_ + Config::BAT_Q2_DRIVE_MARGIN_V`.
        >= batteryVoltage_ + Config::BAT_Q2_DRIVE_MARGIN_V;
  }
  // Retirar propuestas y memoria MPPT. El apagado fisico sucede en el .ino,
  // que debe atender duty2/3=0 antes de encender la rama opuesta.
  // [Guía] Limpia las consignas y los controladores asociados a las ramas que dejan de actuar.
  void clearOutputs() {
    // [Guía] Actualiza `pvVoltageControl_` con `false`. Este dato representa selección del control de tensión PV.
    pvVoltageControl_ = false;
    // [Guía] Actualiza `m1LastAuxOhm_` con `0; m1AuxReliefPending_ = false`.
    m1LastAuxOhm_ = 0; m1AuxReliefPending_ = false;
    // [Guía] Actualiza `m1AuxReliefRetries_` con `0; m1AuxReliefReady_.clear()`.
    m1AuxReliefRetries_ = 0; m1AuxReliefReady_.clear();
    // [Guía] Actualiza `m3MpptSettling_` con `false`.
    m3MpptSettling_ = false;
    // [Guía] Actualiza `hotplugPvSeeking_` con `hotplugPvQualified_ = false; hotplugPvReady_.clear()`.
    hotplugPvSeeking_ = hotplugPvQualified_ = false; hotplugPvReady_.clear();
    // [Guía] Limpia el estado de la preparación PV para que la siguiente operación arranque con confirmaciones
    // [Guía] nuevas.
    cancelPvAssist();
    // [Guía] Actualiza `chargeFlowSeen_` con `false; chargeFlowConfirm_.clear()`.
    chargeFlowSeen_ = false; chargeFlowConfirm_.clear();
    // [Guía] Pide apagar la rama indicada con duty cero. El sketch aplica físicamente la consigna.
    q1_ = q2_ = q3_ = 0;
    // [Guía] Actualiza `m1InputGuardArmed_` con `false`.
    m1InputGuardArmed_ = false;
    // [Guía] Actualiza `m1BudgetRenewable_` con `false`.
    m1BudgetRenewable_ = false;
    // [Guía] Borra la confirmación o acumulación en `m1InputLow_` para que no se arrastre al siguiente contexto.
    m1InputLow_.clear(); m1Healthy_.clear();
    // [Guía] Actualiza `mpptRunning_` con `mpptInitialized_ = false`. Este dato representa indicación de MPPT
    // [Guía] activo.
    mpptRunning_ = mpptInitialized_ = false;
  }
  // No arrastrar evidencia temporal entre estados/inicios incompatibles.
  // [Guía] Reinicia las ventanas de confirmación al abandonar su contexto operativo.
  void clearHolds() {
    // [Guía] Reinicia la confirmación de ausencia de carga externa y conserva la causa de reinicio para
    // [Guía] diagnóstico.
    clearM2Idle("CONTEXT_RESET"); m2Wake_.clear();
    // [Guía] Borra la confirmación o acumulación en `hotplugPvReady_` para que no se arrastre al siguiente
    // [Guía] contexto.
    hotplugPvReady_.clear(); hotplugPvQualified_ = false;
    // [Guía] Borra la confirmación o acumulación en `pvPresent_` para que no se arrastre al siguiente contexto.
    pvPresent_.clear(); pvAbsent_.clear(); zeroCurrent_.clear();
    // [Guía] Borra la confirmación o acumulación en `chargeAtMinimum_` para que no se arrastre al siguiente
    // [Guía] contexto.
    chargeAtMinimum_.clear(); chargeDischargeHold_.clear(); deficitHold_.clear();
    // [Guía] Borra la confirmación o acumulación en `resetSafe_` para que no se arrastre al siguiente contexto.
    resetSafe_.clear(); canReset_ = false;
    // [Guía] Borra la confirmación o acumulación en `busRecoverOn_` para que no se arrastre al siguiente
    // [Guía] contexto.
    busRecoverOn_.clear(); busRecoverOff_.clear();
    // [Guía] Borra la confirmación o acumulación en `m1Recovery_` para que no se arrastre al siguiente contexto.
    m1Recovery_.clear(); m1LoadRecovery_.clear();
    // [Guía] Borra la confirmación o acumulación en `m1InputLow_` para que no se arrastre al siguiente contexto.
    m1InputLow_.clear(); m1Healthy_.clear();
    // [Guía] Borra la confirmación o acumulación en `chargeFlowConfirm_` para que no se arrastre al siguiente
    // [Guía] contexto.
    chargeFlowConfirm_.clear();
    // [Guía] Borra la confirmación o acumulación en `pvAssistReady_` para que no se arrastre al siguiente
    // [Guía] contexto.
    pvAssistReady_.clear(); pvAssistBusWorse_.clear();
    // [Guía] Borra la confirmación o acumulación en `pvAssistReverse_` para que no se arrastre al siguiente
    // [Guía] contexto.
    pvAssistReverse_.clear(); pvAssistDeficit_.clear();
    // [Guía] Borra la confirmación o acumulación en `prepHighBus_` para que no se arrastre al siguiente contexto.
    prepHighBus_.clear(); chargeHealthy_.clear(); chargeMarginLost_.clear();
    // [Guía] Borra la confirmación o acumulación en `chargeRetry_` para que no se arrastre al siguiente contexto.
    chargeRetry_.clear();
  }
  // [Guía] Lleva las consignas a cero, registra el motivo y limpia confirmaciones previas para esperar una
  // [Guía] fuente. Recibe `reason`.
  void wait(const char* reason) { clearOutputs(); state_ = WAIT; reason_ = reason; clearHolds(); }
  // Fuente PV ganadora en WAIT: partir de Q1=0, bateria retirada y START1.
  // [Guía] Inicia el arranque con PV cuando se cumplen sus condiciones de disponibilidad. Recibe `in`.
  void startSolar(const Input& in) {
    // [Guía] Invoca `resetM1BatteryDetection` con los argumentos de esta expresión.
    resetM1BatteryDetection();
    // [Guía] Actualiza `m1BatteryConfirmResets_` con `0; m1BatteryConfirmLastReset_ = "NONE"`.
    m1BatteryConfirmResets_ = 0; m1BatteryConfirmLastReset_ = "NONE";
    // [Guía] Cambia el estado operativo que se evaluará a partir de la siguiente actualización. Aquí intervienen
    // [Guía] estado operativo interno.
    state_ = START1; q1_ = q2_ = q3_ = 0;
    // [Guía] Actualiza `reference_` con `fminf(in.bus, Config::BUS_REFERENCE_V)`. Este dato representa referencia
    // [Guía] del regulador.
    reference_ = fminf(in.bus, Config::BUS_REFERENCE_V);
    // [Guía] Actualiza `rampElapsed_` con `0; haveTime_ = false; reason_ = "BOOT_PV_SOFT_START"`. Este dato
    // [Guía] representa tiempo ya recorrido en la rampa.
    rampElapsed_ = 0; haveTime_ = false; reason_ = "BOOT_PV_SOFT_START";
    // [Guía] Actualiza `pvStartUs_` con `in.nowUs`.
    pvStartUs_ = in.nowUs;
  }
  // Preparar referencia e integral. La politica V45 omite la rama; la politica
  // V44 queda disponible mediante Settings/Config para comparar y revertir.
  // [Guía] Prepara la captura de bus mediante Q1 antes del PI de M1, usando el punto de partida disponible.
  // [Guía] Recibe `in`.
  void prepareM1(const Input& in) {
    // [Guía] Borra la confirmación o acumulación en `m1BranchReady_` para que no se arrastre al siguiente
    // [Guía] contexto.
    m1BranchReady_.clear(); m1Reacquire_.clear();
    // [Guía] Borra la confirmación o acumulación en `m1Recovery_` para que no se arrastre al siguiente contexto.
    m1Recovery_.clear(); m1Retries_ = 0; m1Limit_ = "NONE";
    // [Guía] Borra la confirmación o acumulación en `m1LoadRecovery_` para que no se arrastre al siguiente
    // [Guía] contexto.
    m1LoadRecovery_.clear(); m1LoadRetryUsed_ = false;
    // [Guía] Actualiza `m1AcquireSlew_` con `Config::M1_ACQUIRE_SLEW_PER_SECOND`.
    m1AcquireSlew_ = Config::M1_ACQUIRE_SLEW_PER_SECOND;
    // El piso de bus protege una caida DESPUES de tener tension util. No debe
    // impedir que el primer arranque eleve un bus aun en precarga baja.
    // [Guía] Actualiza `m1InputGuardArmed_` con `in.bus >= Config::M1_INPUT_BUS_RESUME_V`.
    m1InputGuardArmed_ = in.bus >= Config::M1_INPUT_BUS_RESUME_V
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
      && in.busRaw >= Config::M1_INPUT_BUS_MIN_V;
    // [Guía] Borra la confirmación o acumulación en `m1InputLow_` para que no se arrastre al siguiente contexto.
    m1InputLow_.clear(); m1Healthy_.clear(); m1BudgetRenewable_ = false;
    // SIEMPRE confirmar la rama tras conectar o transferir Q1. Un valle de
    // arranque por debajo17.5 V no demuestra el punto de operacion sostenido.
    // [Guía] Actualiza `m1Stage_` con `M1_ACQUIRE`. Este dato representa etapa interna de arranque y regulación
    // [Guía] M1.
    m1Stage_ = M1_ACQUIRE;
    // [Guía] Actualiza `m1AcquireStartUs_` con `in.nowUs`.
    m1AcquireStartUs_ = in.nowUs;
    // [Guía] Actualiza `reference_` con `Config::BUS_REFERENCE_V; rampElapsed_ = Config::SOFT_START_US`. Este
    // [Guía] dato representa referencia del regulador.
    reference_ = Config::BUS_REFERENCE_V; rampElapsed_ = Config::SOFT_START_US;
    // [Guía] Reinicia PI de bus aplicado a Q1 con el estado inicial dado en los argumentos. Aquí intervienen PI
    // [Guía] de bus aplicado a Q1 y consigna de duty de Q1 (0 a 1).
    pi1_.reset(error1(in), q1_);
    // [Guía] Comprueba `!settings_.m1BranchAcquireEnabled`. Si se cumple, ejecuta el bloque asociado.
    if (!settings_.m1BranchAcquireEnabled) {
      // [Guía] Actualiza `m1Stage_` con `M1_PI`. Este dato representa etapa interna de arranque y regulación M1.
      m1Stage_ = M1_PI;
      // Ensayo directo: referencia gradual desde el bus medido y entrega sin
      // escalon de duty. No barrer ni esperar una rama particular del PV.
      // [Guía] Actualiza `reference_` con `fminf(in.bus, Config::BUS_REFERENCE_V); rampElapsed_ = 0`. Este dato
      // [Guía] representa referencia del regulador.
      reference_ = fminf(in.bus, Config::BUS_REFERENCE_V); rampElapsed_ = 0;
      // [Guía] Reinicia PI de bus aplicado a Q1 con el estado inicial dado en los argumentos. Aquí intervienen PI
      // [Guía] de bus aplicado a Q1 y consigna de duty de Q1 (0 a 1).
      pi1_.reset(error1(in), q1_);
    }
  }
  // Control PV solo. ACQUIRE barre duty, RECOVER retira de forma acotada al
  // rozar el techo operativo, LIMITED deja de insistir, PI regula el bus.
  // El presupuesto temporal de adquisicion no se renueva en cada reintento.
  // [Guía] Actualiza la rama de M1. Gestiona captura, estabilización y entrega al PI de Q1 con sus límites de
  // [Guía] pendiente. Recibe `in`, `dt`, `elapsed`.
  void update1(const Input& in, float dt, uint32_t elapsed) {
    // [Guía] Comprueba `m1Stage_ != M1_LIMITED`. Si se cumple, ejecuta `m1AuxReliefPending_ = false`. Aquí
    // [Guía] intervienen etapa interna de arranque y regulación M1.
    if (m1Stage_ != M1_LIMITED) m1AuxReliefPending_ = false;
    // auxiliaryLoadOhm>0 ya exige ACK de R y LOAD ON en el sketch. No usar
    // recoveryLoadConfirmed: ese permiso historico solo admite R<=200ohm.
    // [Guía] Actualiza `auxiliaryAck` con `isfinite(in.auxiliaryLoadOhm)`.
    const bool auxiliaryAck = isfinite(in.auxiliaryLoadOhm)
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
      && in.auxiliaryLoadOhm > 0;
    // [Guía] Comprueba `auxiliaryAck`. Si se cumple, ejecuta el bloque asociado.
    if (auxiliaryAck) {
      // [Guía] Comprueba `m1Stage_ == M1_LIMITED && m1LastAuxOhm_ > 0`. Si se cumple, ejecuta el bloque asociado.
      // [Guía] Aquí intervienen etapa interna de arranque y regulación M1.
      if (m1Stage_ == M1_LIMITED && m1LastAuxOhm_ > 0
          // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
          && in.auxiliaryLoadOhm > m1LastAuxOhm_ + 0.0011f)
        // [Guía] Actualiza `m1AuxReliefPending_` con `true`.
        m1AuxReliefPending_ = true;
      // [Guía] Actualiza `m1LastAuxOhm_` con `in.auxiliaryLoadOhm`.
      m1LastAuxOhm_ = in.auxiliaryLoadOhm;
    // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
    } else {
      // Una orden R pendiente hace auxiliaryLoadOhm=0 hasta su ACK. Guardar
      // el ultimo valor para comparar el nuevo ACK, sin operar sin permiso.
      // [Guía] Actualiza `m1AuxReliefPending_` con `false`.
      m1AuxReliefPending_ = false;
    }
    // [Guía] Actualiza `reliefReady` con `m1Stage_ == M1_LIMITED && m1AuxReliefPending_`.
    const bool reliefReady = m1Stage_ == M1_LIMITED && m1AuxReliefPending_
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
      && auxiliaryAck && m1AuxReliefRetries_ < Config::M1_AUX_RELIEF_MAX_REACQUIRES
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen tensión del PV
      // [Guía] en V.
      && in.pvValid && in.pv >= Config::M1_INPUT_PV_RESUME_V
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen tensión rápida
      // [Guía] del bus en V.
      && in.bus >= Config::M1_INPUT_BUS_RESUME_V
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen tensión rápida
      // [Guía] del bus en V.
      && in.bus < Config::M1_ACQUIRE_RESUME_V
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen tensión de bus
      // [Guía] filtrada para supervisión.
      && busSupervisorReady_ && busSupervisor_ < Config::M1_ACQUIRE_RESUME_V;
    // [Guía] Comprueba `m1AuxReliefReady_.held(reliefReady, in.nowUs, Config::M1_AUX_LOAD_RECOVERY_US)`. Si se
    // [Guía] cumple, ejecuta el bloque asociado. Aquí intervienen reloj de la muestra en µs.
    if (m1AuxReliefReady_.held(reliefReady, in.nowUs, Config::M1_AUX_LOAD_RECOVERY_US)) {
      // [Guía] Completa la declaración o expresión con `++m1AuxReliefRetries_; m1AuxReliefPending_ = false;
      // [Guía] m1AuxReliefReady_.clear()`.
      ++m1AuxReliefRetries_; m1AuxReliefPending_ = false; m1AuxReliefReady_.clear();
      // Carga nueva y margen recuperado, no un permiso para ignorar TIMEOUT.
      // Conservar duty aplicado y adquirir otra vez con la mitad de velocidad.
      // [Guía] Prepara la captura de bus mediante Q1 antes del PI de M1, usando el punto de partida disponible.
      prepareM1(in); m1AcquireSlew_ *= 0.5f;
      // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
      reason_ = "M1_REACQUIRE_AFTER_AUX_RELIEF"; return;
    }
    // [Guía] Comprueba `!settings_.m1BranchAcquireEnabled`. Si se cumple, ejecuta el bloque asociado.
    if (!settings_.m1BranchAcquireEnabled) {
      // Mantener guardia de entrada y protecciones globales aunque se omita
      // ACQUIRE. Una caida sostenida retira duty, nunca inicia otro barrido.
      // [Guía] Comprueba `in.bus >= Config::M1_INPUT_BUS_RESUME_V`. Si se cumple, ejecuta el bloque asociado.
      // [Guía] Aquí intervienen tensión rápida del bus en V.
      if (in.bus >= Config::M1_INPUT_BUS_RESUME_V
          // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
          && in.busRaw >= Config::M1_INPUT_BUS_MIN_V) m1InputGuardArmed_ = true;
      // [Guía] Actualiza `low` con `m1InputGuardArmed_ && (in.pv < Config::M1_INPUT_PV_MIN_V`.
      const bool low = m1InputGuardArmed_ && (in.pv < Config::M1_INPUT_PV_MIN_V
        // [Guía] Añade una alternativa suficiente dentro de la condición compuesta.
        || in.pvRaw < Config::M1_INPUT_PV_RAW_MIN_V
        // [Guía] Añade una alternativa suficiente dentro de la condición compuesta. Aquí intervienen tensión
        // [Guía] rápida del bus en V.
        || in.bus < Config::M1_INPUT_BUS_MIN_V || in.busRaw < Config::M1_INPUT_BUS_RAW_MIN_V);
      // [Guía] Comprueba `m1InputLow_.held(low, in.nowUs, Config::M1_INPUT_CONFIRM_US)`. Si se cumple, ejecuta el
      // [Guía] bloque asociado. Aquí intervienen reloj de la muestra en µs.
      if (m1InputLow_.held(low, in.nowUs, Config::M1_INPUT_CONFIRM_US)) {
        // [Guía] Actualiza `m1Stage_` con `M1_INPUT_RECOVER; m1Recovery_.clear()`. Este dato representa etapa
        // [Guía] interna de arranque y regulación M1.
        m1Stage_ = M1_INPUT_RECOVER; m1Recovery_.clear();
      }
      // [Guía] Comprueba `m1Stage_ == M1_INPUT_RECOVER`. Si se cumple, ejecuta el bloque asociado. Aquí
      // [Guía] intervienen etapa interna de arranque y regulación M1.
      if (m1Stage_ == M1_INPUT_RECOVER) {
        // [Guía] Actualiza `healthy` con `!low && in.pv >= Config::M1_INPUT_PV_RESUME_V`.
        const bool healthy = !low && in.pv >= Config::M1_INPUT_PV_RESUME_V
          // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen tensión
          // [Guía] rápida del bus en V.
          && in.bus >= Config::M1_INPUT_BUS_RESUME_V;
        // [Guía] Comprueba `!healthy`. Si se cumple, ejecuta `q1_ = approach(q1_, Config::Q1_DUTY_MIN,`.
        if (!healthy) q1_ = approach(q1_, Config::Q1_DUTY_MIN,
          // [Guía] Completa la declaración o expresión con `Config::Q1_TEST_SLEW_PER_SECOND * dt)`. Aquí
          // [Guía] intervienen intervalo efectivo del control en s.
          Config::Q1_TEST_SLEW_PER_SECOND * dt);
        // [Guía] Reinicia PI de bus aplicado a Q1 con el estado inicial dado en los argumentos. Aquí intervienen
        // [Guía] PI de bus aplicado a Q1 y consigna de duty de Q1 (0 a 1).
        pi1_.reset(error1(in), q1_); reason_ = "M1_RECOVER_PV_INPUT";
        // [Guía] Comprueba `m1Recovery_.held(healthy, in.nowUs, Config::M1_ACQUIRE_RESUME_US)`. Si se cumple,
        // [Guía] ejecuta `m1Stage_ = M1_PI`. Aquí intervienen reloj de la muestra en µs.
        if (m1Recovery_.held(healthy, in.nowUs, Config::M1_ACQUIRE_RESUME_US)) m1Stage_ = M1_PI;
        // [Guía] Finaliza aquí la función y devuelve el control al llamador.
        return;
      }
      // [Guía] Comprueba `low`. Si se cumple, ejecuta el bloque asociado.
      if (low) {
        // [Guía] Reinicia PI de bus aplicado a Q1 con el estado inicial dado en los argumentos. Aquí intervienen
        // [Guía] PI de bus aplicado a Q1 y consigna de duty de Q1 (0 a 1).
        pi1_.reset(error1(in), q1_); reason_ = "M1_INPUT_CONFIRM_HOLD"; return;
      }
      // [Guía] Acerca gradualmente la referencia del bus al valor nominal mediante el tiempo efectivo del
      // [Guía] control. Aquí intervienen intervalo efectivo del control en s y tiempo transcurrido.
      advanceReference(dt, elapsed);
      // [Guía] Actualiza `q1_` con `pi1_.update(error1(in), dt, Config::Q1_TEST_SLEW_PER_SECOND * dt)`. Este dato
      // [Guía] representa consigna de duty de Q1 (0 a 1).
      q1_ = pi1_.update(error1(in), dt, Config::Q1_TEST_SLEW_PER_SECOND * dt);
      // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
      reason_ = q1_ >= Config::Q1_DUTY_MAX ? "M1_DUTY_LIMIT" : "BUS_PI_Q1";
      // [Guía] Finaliza aquí la función y devuelve el control al llamador.
      return;
    }
    // Revalidar tambien con bus BAJO: V19 exigia Vbus>20.8 V y quedaba
    // atrapado en el PI de rama baja con PV20 V, bus19 V y duty5%.
    // [Guía] Comprueba `m1Stage_ == M1_PI && m1Reacquire_.held(`. Si se cumple, ejecuta el bloque asociado. Aquí
    // [Guía] intervienen etapa interna de arranque y regulación M1.
    if (m1Stage_ == M1_PI && m1Reacquire_.held(
        // [Guía] Completa la declaración o expresión con `pvSupervisorReady_ && pvSupervisor_ >=
        // [Guía] Config::M1_REACQUIRE_V`. Aquí intervienen tensión de PV filtrada para supervisión.
        pvSupervisorReady_ && pvSupervisor_ >= Config::M1_REACQUIRE_V,
        // [Guía] Completa la declaración o expresión con `in.nowUs, settings_.m1BranchConfirmUs)) prepareM1(in)`.
        // [Guía] Aquí intervienen reloj de la muestra en µs.
        in.nowUs, settings_.m1BranchConfirmUs)) prepareM1(in);
    // No aceptar la rama por el supervisor atrasado cuando la muestra rapida
    // ya colapso. Tampoco aumentar duty mientras maduran los200ms de permiso.
    // [Guía] Comprueba `in.bus >= Config::M1_INPUT_BUS_RESUME_V`. Si se cumple, ejecuta el bloque asociado. Aquí
    // [Guía] intervienen tensión rápida del bus en V.
    if (in.bus >= Config::M1_INPUT_BUS_RESUME_V
        // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
        && in.busRaw >= Config::M1_INPUT_BUS_MIN_V) m1InputGuardArmed_ = true;
    // [Guía] Actualiza `inputLow` con `m1InputGuardArmed_ && (in.pv < Config::M1_INPUT_PV_MIN_V`.
    const bool inputLow = m1InputGuardArmed_ && (in.pv < Config::M1_INPUT_PV_MIN_V
      // [Guía] Añade una alternativa suficiente dentro de la condición compuesta.
      || in.pvRaw < Config::M1_INPUT_PV_RAW_MIN_V
      // [Guía] Añade una alternativa suficiente dentro de la condición compuesta. Aquí intervienen tensión rápida
      // [Guía] del bus en V.
      || in.bus < Config::M1_INPUT_BUS_MIN_V
      // [Guía] Añade una alternativa suficiente dentro de la condición compuesta.
      || in.busRaw < Config::M1_INPUT_BUS_RAW_MIN_V);
    // Empezar solo en rama baja. Una vez iniciada la confirmacion, tolerar
    // picos pequenos sin congelar antes un panel que aun promedia18V.
    // [Guía] Actualiza `branchFastMax` con `settings_.m1BranchNoiseTolerant && m1BranchReady_.running`.
    const float branchFastMax = settings_.m1BranchNoiseTolerant && m1BranchReady_.running
      // [Guía] Define una de las dos salidas de la expresión condicional.
      ? Config::M1_BRANCH_FAST_MAX_V : Config::M1_LOW_BRANCH_V;
    // [Guía] Actualiza `branchCandidate` con `!inputLow && pvSupervisorReady_`.
    const bool branchCandidate = !inputLow && pvSupervisorReady_
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen tensión del PV
      // [Guía] en V.
      && in.pv <= branchFastMax
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen tensión rápida
      // [Guía] del bus en V.
      && in.bus >= Config::M1_INPUT_BUS_RESUME_V
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
      && in.busRaw >= Config::M1_INPUT_BUS_MIN_V
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
      && in.pvI > Config::BAT_PV_ON_CURRENT_A;
    // [Guía] Actualiza `branchReady` con `m1BranchReady_.held(branchCandidate`.
    const bool branchReady = m1BranchReady_.held(branchCandidate
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen tensión de PV
      // [Guía] filtrada para supervisión.
      && pvSupervisor_ <= Config::M1_LOW_BRANCH_V,
      // [Guía] Completa la declaración o expresión con `in.nowUs, settings_.m1BranchConfirmUs)`. Aquí intervienen
      // [Guía] reloj de la muestra en µs.
      in.nowUs, settings_.m1BranchConfirmUs);
    // V41: el PI tambien puede empujar al panel hacia una zona sin margen.
    // V40 solo vigilaba ACQUIRE/RECOVER, dejando este camino sin guardia.
    // [Guía] Actualiza `guardEligible` con `m1Stage_ == M1_PI || m1Stage_ == M1_ACQUIRE`.
    const bool guardEligible = m1Stage_ == M1_PI || m1Stage_ == M1_ACQUIRE
      // [Guía] Añade una alternativa suficiente dentro de la condición compuesta. Aquí intervienen etapa interna
      // [Guía] de arranque y regulación M1.
      || m1Stage_ == M1_RECOVER;
    // [Guía] Actualiza `confirmedInputLow` con `m1InputLow_.held(guardEligible && inputLow`.
    const bool confirmedInputLow = m1InputLow_.held(guardEligible && inputLow,
      // [Guía] Completa la declaración o expresión con `in.nowUs, Config::M1_INPUT_CONFIRM_US)`. Aquí intervienen
      // [Guía] reloj de la muestra en µs.
      in.nowUs, Config::M1_INPUT_CONFIRM_US);
    // [Guía] Comprueba `m1Healthy_.held(m1Stage_ == M1_PI && !inputLow`. Si se cumple, ejecuta el bloque
    // [Guía] asociado. Aquí intervienen etapa interna de arranque y regulación M1.
    if (m1Healthy_.held(m1Stage_ == M1_PI && !inputLow
        // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen tensión del PV
        // [Guía] en V.
        && in.pv >= Config::M1_INPUT_PV_RESUME_V
        // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen tensión rápida
        // [Guía] del bus en V.
        && fabsf(in.bus - Config::BUS_REFERENCE_V) <= Config::M1_PI_STABLE_BAND_V
        // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen tensión de bus
        // [Guía] filtrada para supervisión.
        && fabsf(busSupervisor_ - Config::BUS_REFERENCE_V) <= Config::M1_PI_STABLE_BAND_V,
        // [Guía] Completa la declaración o expresión con `in.nowUs, Config::M1_PI_STABLE_US)) m1BudgetRenewable_
        // [Guía] = true`. Aquí intervienen reloj de la muestra en µs.
        in.nowUs, Config::M1_PI_STABLE_US)) m1BudgetRenewable_ = true;
    // El presupuesto total incluye recuperacion y reintentos. No reiniciar
    // sus15 s al encontrar otra excursion de tension.
    // [Guía] Comprueba `(m1Stage_ == M1_ACQUIRE || m1Stage_ == M1_RECOVER || m1Stage_ == M1_INPUT_RECOVER)`. Si
    // [Guía] se cumple, ejecuta el bloque asociado. Aquí intervienen etapa interna de arranque y regulación M1.
    if ((m1Stage_ == M1_ACQUIRE || m1Stage_ == M1_RECOVER || m1Stage_ == M1_INPUT_RECOVER)
        // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen reloj de la
        // [Guía] muestra en µs.
        && uint32_t(in.nowUs - m1AcquireStartUs_) >= Config::M1_ACQUIRE_TIMEOUT_US) {
      // [Guía] Actualiza `m1Stage_` con `M1_LIMITED; m1Limit_ = "TIMEOUT"`. Este dato representa etapa interna de
      // [Guía] arranque y regulación M1.
      m1Stage_ = M1_LIMITED; m1Limit_ = "TIMEOUT";
      // [Guía] Borra la confirmación o acumulación en `m1Recovery_` para que no se arrastre al siguiente
      // [Guía] contexto.
      m1Recovery_.clear();
    }
    // update() comprueba protecciones y retira a WAIT por bus cero ANTES de
    // llegar aqui. Esta recuperacion opera solo mientras aun existe tension.
    // [Guía] Comprueba `m1Stage_ != M1_LIMITED && confirmedInputLow`. Si se cumple, ejecuta el bloque asociado.
    // [Guía] Aquí intervienen etapa interna de arranque y regulación M1.
    if (m1Stage_ != M1_LIMITED && confirmedInputLow) {
      // Despues de regulacion realmente estable, una nueva perturbacion tiene
      // su presupuesto. Sin esa evidencia se conserva el episodio anterior.
      // [Guía] Comprueba `m1Stage_ == M1_PI && m1BudgetRenewable_`. Si se cumple, ejecuta el bloque asociado.
      // [Guía] Aquí intervienen etapa interna de arranque y regulación M1.
      if (m1Stage_ == M1_PI && m1BudgetRenewable_) {
        // [Guía] Actualiza `m1AcquireStartUs_` con `in.nowUs; m1Retries_ = 0`.
        m1AcquireStartUs_ = in.nowUs; m1Retries_ = 0;
        // [Guía] Actualiza `m1LoadRetryUsed_` con `false; m1LoadRecovery_.clear()`.
        m1LoadRetryUsed_ = false; m1LoadRecovery_.clear();
        // [Guía] Actualiza `m1AcquireSlew_` con `Config::M1_ACQUIRE_SLEW_PER_SECOND`.
        m1AcquireSlew_ = Config::M1_ACQUIRE_SLEW_PER_SECOND;
      }
      // [Guía] Actualiza `m1BudgetRenewable_` con `false; m1Healthy_.clear()`.
      m1BudgetRenewable_ = false; m1Healthy_.clear();
      // [Guía] Actualiza `m1Stage_` con `M1_INPUT_RECOVER; m1Limit_ = "PV_INPUT_LOW"`. Este dato representa etapa
      // [Guía] interna de arranque y regulación M1.
      m1Stage_ = M1_INPUT_RECOVER; m1Limit_ = "PV_INPUT_LOW";
      // [Guía] Borra la confirmación o acumulación en `m1Recovery_` para que no se arrastre al siguiente
      // [Guía] contexto.
      m1Recovery_.clear(); m1BranchReady_.clear();
    // [Guía] Comprueba `m1Stage_ != M1_LIMITED && guardEligible && inputLow`. Si se cumple, ejecuta el bloque
    // [Guía] asociado. Aquí intervienen etapa interna de arranque y regulación M1.
    } else if (m1Stage_ != M1_LIMITED && guardEligible && inputLow) {
      // Primera muestra: mantener salida y sincronizar integral con lo aplicado.
      // No integrar error de bus mientras puede faltar energia de entrada.
      // [Guía] Reinicia PI de bus aplicado a Q1 con el estado inicial dado en los argumentos. Aquí intervienen PI
      // [Guía] de bus aplicado a Q1 y consigna de duty de Q1 (0 a 1).
      pi1_.reset(error1(in), q1_);
      // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
      reason_ = "M1_INPUT_CONFIRM_HOLD";
      // [Guía] Finaliza aquí la función y devuelve el control al llamador.
      return;
    }
    // [Guía] Comprueba `m1Stage_ == M1_ACQUIRE`. Si se cumple, ejecuta el bloque asociado. Aquí intervienen etapa
    // [Guía] interna de arranque y regulación M1.
    if (m1Stage_ == M1_ACQUIRE) {
      // Barrido lento SOLO para adquirir la rama del PI. No dejar integrar
      // un error de bus con el signo incorrecto mientras PV esta en rama alta.
      // [Guía] Comprueba `busSupervisor_ >= Config::M1_ACQUIRE_BUS_MAX_V`. Si se cumple, ejecuta el bloque
      // [Guía] asociado. Aquí intervienen tensión de bus filtrada para supervisión.
      if (busSupervisor_ >= Config::M1_ACQUIRE_BUS_MAX_V
          // [Guía] Añade una alternativa suficiente dentro de la condición compuesta. Aquí intervienen tensión
          // [Guía] rápida del bus en V.
          || in.bus >= Config::M1_ACQUIRE_BUS_MAX_V) {
        // [Guía] Actualiza `m1Stage_` con `M1_RECOVER; m1Limit_ = "ACQUIRE_BUS"`. Este dato representa etapa
        // [Guía] interna de arranque y regulación M1.
        m1Stage_ = M1_RECOVER; m1Limit_ = "ACQUIRE_BUS";
        // [Guía] Borra la confirmación o acumulación en `m1Recovery_` para que no se arrastre al siguiente
        // [Guía] contexto.
        m1Recovery_.clear(); m1BranchReady_.clear();
        // No saltar del duty aplicado al5% por el techo operativo. El corte
        // crudo35/40 V anterior conserva prioridad sobre esta recuperacion.
      // [Guía] Comprueba `q1_ >= Config::Q1_DUTY_MAX`. Si se cumple, ejecuta el bloque asociado. Aquí intervienen
      // [Guía] consigna de duty de Q1 (0 a 1).
      } else if (q1_ >= Config::Q1_DUTY_MAX) {
        // [Guía] Actualiza `m1Stage_` con `M1_LIMITED; m1Limit_ = "DUTY_MAX"`. Este dato representa etapa interna
        // [Guía] de arranque y regulación M1.
        m1Stage_ = M1_LIMITED; m1Limit_ = "DUTY_MAX";
      // [Guía] Comprueba `branchReady`. Si se cumple, ejecuta el bloque asociado.
      } else if (branchReady) {
        // [Guía] Actualiza `m1Stage_` con `M1_PI; pi1_.reset(error1(in), q1_)`. Este dato representa etapa
        // [Guía] interna de arranque y regulación M1.
        m1Stage_ = M1_PI; pi1_.reset(error1(in), q1_);
        // [Guía] Actualiza `m1Limit_` con `"NONE"`.
        m1Limit_ = "NONE";
        // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
        reason_ = "M1_BRANCH_ACQUIRED";
        // [Guía] Finaliza aquí la función y devuelve el control al llamador.
        return;
      // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
      } else {
        // V39 seguia barriendo durante toda la confirmacion. A40pp/s eran
        // otros8pp en200ms, suficientes para sobrepasar el punto observado.
        // [Guía] Comprueba `branchCandidate`. Si se cumple, ejecuta el bloque asociado.
        if (branchCandidate) {
          // [Guía] Reinicia PI de bus aplicado a Q1 con el estado inicial dado en los argumentos. Aquí
          // [Guía] intervienen PI de bus aplicado a Q1 y consigna de duty de Q1 (0 a 1).
          pi1_.reset(error1(in), q1_);
          // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
          reason_ = "M1_HOLD_BRANCH_CONFIRM";
        // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
        } else {
          // [Guía] Actualiza `q1_` con `approach(q1_, Config::Q1_DUTY_MAX, m1AcquireSlew_ * dt)`. Este dato
          // [Guía] representa consigna de duty de Q1 (0 a 1).
          q1_ = approach(q1_, Config::Q1_DUTY_MAX, m1AcquireSlew_ * dt);
          // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
          reason_ = "M1_ACQUIRE_PV_BRANCH";
        }
        // [Guía] Finaliza aquí la función y devuelve el control al llamador.
        return;
      }
    }
    // [Guía] Comprueba `m1Stage_ == M1_RECOVER || m1Stage_ == M1_INPUT_RECOVER`. Si se cumple, ejecuta el bloque
    // [Guía] asociado. Aquí intervienen etapa interna de arranque y regulación M1.
    if (m1Stage_ == M1_RECOVER || m1Stage_ == M1_INPUT_RECOVER) {
      // [Guía] Actualiza `recoveringInput` con `m1Stage_ == M1_INPUT_RECOVER`.
      const bool recoveringInput = m1Stage_ == M1_INPUT_RECOVER;
      // [Guía] Actualiza `belowResume` con `in.bus < Config::M1_ACQUIRE_RESUME_V`.
      const bool belowResume = in.bus < Config::M1_ACQUIRE_RESUME_V
        // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen tensión de bus
        // [Guía] filtrada para supervisión.
        && busSupervisor_ < Config::M1_ACQUIRE_RESUME_V
        // [Guía] Agrupa condiciones alternativas y conjuntas en la prueba en curso. Aquí intervienen tensión del
        // [Guía] PV en V.
        && (!recoveringInput || (in.pv >= Config::M1_INPUT_PV_RESUME_V
          // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
          && in.pvRaw >= Config::M1_INPUT_PV_MIN_V
          // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen tensión
          // [Guía] rápida del bus en V.
          && in.bus >= Config::M1_INPUT_BUS_RESUME_V
          // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
          && in.busRaw >= Config::M1_INPUT_BUS_MIN_V));
      // Retirar duty solo mientras el bus no recupera margen; despues
      // sostenerlo durante la confirmacion, sin integrar ni seguir subiendo.
      // [Guía] Comprueba `!belowResume`. Si se cumple, ejecuta el bloque asociado.
      if (!belowResume)
        // [Guía] Actualiza `q1_` con `approach(q1_, Config::Q1_DUTY_MIN, Config::Q1_TEST_SLEW_PER_SECOND * dt)`.
        // [Guía] Este dato representa consigna de duty de Q1 (0 a 1).
        q1_ = approach(q1_, Config::Q1_DUTY_MIN, Config::Q1_TEST_SLEW_PER_SECOND * dt);
      // [Guía] Reinicia PI de bus aplicado a Q1 con el estado inicial dado en los argumentos. Aquí intervienen PI
      // [Guía] de bus aplicado a Q1 y consigna de duty de Q1 (0 a 1).
      pi1_.reset(error1(in), q1_);
      // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
      reason_ = recoveringInput ? "M1_RECOVER_PV_INPUT" : "M1_RECOVER_ACQUIRE_BUS";
      // [Guía] Comprueba `!m1Recovery_.held(belowResume, in.nowUs, Config::M1_ACQUIRE_RESUME_US)`. Si se cumple,
      // [Guía] termina la función en esta rama. Aquí intervienen reloj de la muestra en µs.
      if (!m1Recovery_.held(belowResume, in.nowUs, Config::M1_ACQUIRE_RESUME_US)) return;
      // [Guía] Comprueba `branchReady`. Si se cumple, ejecuta el bloque asociado.
      if (branchReady) {
        // [Guía] Actualiza `m1Stage_` con `M1_PI; m1Limit_ = "NONE"`. Este dato representa etapa interna de
        // [Guía] arranque y regulación M1.
        m1Stage_ = M1_PI; m1Limit_ = "NONE";
        // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
        reason_ = "M1_BRANCH_ACQUIRED";
      // [Guía] Comprueba `settings_.m1AuxiliaryCoordination && in.m1AuxiliaryAdjusting`. Si se cumple, ejecuta el
      // [Guía] bloque asociado.
      } else if (settings_.m1AuxiliaryCoordination && in.m1AuxiliaryAdjusting) {
        // No consumir reintentos mientras R cambia lentamente. Mantener el
        // duty recuperado y el presupuesto TOTAL15s; no autorizar PI sin rama.
        // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
        reason_ = "M1_WAIT_AUX_LOAD_RAMP";
        // [Guía] Finaliza aquí la función y devuelve el control al llamador.
        return;
      // [Guía] Comprueba `m1Retries_ < Config::M1_ACQUIRE_MAX_RETRIES`. Si se cumple, ejecuta el bloque asociado.
      } else if (m1Retries_ < Config::M1_ACQUIRE_MAX_RETRIES) {
        // [Guía] Completa la declaración o expresión con `++m1Retries_; m1AcquireSlew_ *= 0.5f`.
        ++m1Retries_; m1AcquireSlew_ *= 0.5f;
        // [Guía] Actualiza `m1Stage_` con `M1_ACQUIRE; m1Recovery_.clear()`. Este dato representa etapa interna
        // [Guía] de arranque y regulación M1.
        m1Stage_ = M1_ACQUIRE; m1Recovery_.clear();
        // [Guía] Borra la confirmación o acumulación en `m1BranchReady_` para que no se arrastre al siguiente
        // [Guía] contexto. Aquí intervienen código de diagnóstico de la decisión.
        m1BranchReady_.clear(); reason_ = "M1_ACQUIRE_RETRY";
      // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
      } else {
        // [Guía] Actualiza `m1Stage_` con `M1_LIMITED; m1Limit_ = "RETRIES_EXHAUSTED"`. Este dato representa
        // [Guía] etapa interna de arranque y regulación M1.
        m1Stage_ = M1_LIMITED; m1Limit_ = "RETRIES_EXHAUSTED";
      }
      // [Guía] Comprueba `m1Stage_ != M1_LIMITED`. Si se cumple, termina la función en esta rama. Aquí
      // [Guía] intervienen etapa interna de arranque y regulación M1.
      if (m1Stage_ != M1_LIMITED) return;
    }
    // [Guía] Comprueba `m1Stage_ == M1_LIMITED`. Si se cumple, ejecuta el bloque asociado. Aquí intervienen etapa
    // [Guía] interna de arranque y regulación M1.
    if (m1Stage_ == M1_LIMITED) {
      // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
      reason_ = "M1_ACQUIRE_LIMITED";
      // Una nueva carga confirmada permite UNA recuperacion adicional si el
      // bus tambien baja. No renovar los15 s ni rearmar por un ACK aislado.
      // [Guía] Comprueba `branchReady && in.bus < Config::M1_ACQUIRE_RESUME_V`. Si se cumple, ejecuta el bloque
      // [Guía] asociado. Aquí intervienen tensión rápida del bus en V.
      if (branchReady && in.bus < Config::M1_ACQUIRE_RESUME_V
          // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen tensión de
          // [Guía] bus filtrada para supervisión.
          && busSupervisor_ < Config::M1_ACQUIRE_RESUME_V) {
        // [Guía] Actualiza `m1Stage_` con `M1_PI; m1Limit_ = "NONE"`. Este dato representa etapa interna de
        // [Guía] arranque y regulación M1.
        m1Stage_ = M1_PI; m1Limit_ = "NONE";
        // [Guía] Reinicia PI de bus aplicado a Q1 con el estado inicial dado en los argumentos. Aquí intervienen
        // [Guía] PI de bus aplicado a Q1 y consigna de duty de Q1 (0 a 1).
        pi1_.reset(error1(in), q1_); reason_ = "M1_BRANCH_ACQUIRED";
      // [Guía] Comprueba `m1LoadRecovery_.held(Config::M1_ACQUIRE_AUX_LOAD`. Si se cumple, ejecuta el bloque
      // [Guía] asociado.
      } else if (m1LoadRecovery_.held(Config::M1_ACQUIRE_AUX_LOAD
          // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
          && !m1LoadRetryUsed_ && in.recoveryLoadConfirmed
          // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen reloj de la
          // [Guía] muestra en µs.
          && uint32_t(in.nowUs - m1AcquireStartUs_) < Config::M1_ACQUIRE_TIMEOUT_US
          // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen tensión
          // [Guía] rápida del bus en V.
          && in.bus < Config::M1_ACQUIRE_RESUME_V
          // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen tensión de
          // [Guía] bus filtrada para supervisión.
          && busSupervisor_ < Config::M1_ACQUIRE_RESUME_V,
          // [Guía] Completa la declaración o expresión con `in.nowUs, Config::M1_AUX_LOAD_RECOVERY_US)) {`. Aquí
          // [Guía] intervienen reloj de la muestra en µs.
          in.nowUs, Config::M1_AUX_LOAD_RECOVERY_US)) {
        // [Guía] Actualiza `m1LoadRetryUsed_` con `true`.
        m1LoadRetryUsed_ = true;
        // [Guía] Actualiza `m1Stage_` con `M1_ACQUIRE; m1Retries_ = 0; m1Limit_ = "LOAD_RETRY"`. Este dato
        // [Guía] representa etapa interna de arranque y regulación M1.
        m1Stage_ = M1_ACQUIRE; m1Retries_ = 0; m1Limit_ = "LOAD_RETRY";
        // [Guía] Actualiza `m1AcquireSlew_` con `Config::M1_ACQUIRE_SLEW_PER_SECOND * 0.5f`.
        m1AcquireSlew_ = Config::M1_ACQUIRE_SLEW_PER_SECOND * 0.5f;
        // [Guía] Borra la confirmación o acumulación en `m1Recovery_` para que no se arrastre al siguiente
        // [Guía] contexto.
        m1Recovery_.clear(); m1BranchReady_.clear();
        // [Guía] Reinicia PI de bus aplicado a Q1 con el estado inicial dado en los argumentos. Aquí intervienen
        // [Guía] PI de bus aplicado a Q1 y consigna de duty de Q1 (0 a 1).
        pi1_.reset(error1(in), q1_);
        // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
        reason_ = "M1_ACQUIRE_WITH_LOAD_RETRY";
      // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
      } else {
        // [Guía] Actualiza `q1_` con `approach(q1_, Config::Q1_DUTY_MIN, Config::Q1_TEST_SLEW_PER_SECOND * dt)`.
        // [Guía] Este dato representa consigna de duty de Q1 (0 a 1).
        q1_ = approach(q1_, Config::Q1_DUTY_MIN, Config::Q1_TEST_SLEW_PER_SECOND * dt);
        // [Guía] Reinicia PI de bus aplicado a Q1 con el estado inicial dado en los argumentos. Aquí intervienen
        // [Guía] PI de bus aplicado a Q1 y consigna de duty de Q1 (0 a 1).
        pi1_.reset(error1(in), q1_);
      }
      // [Guía] Finaliza aquí la función y devuelve el control al llamador.
      return;
    }
    // [Guía] Acerca gradualmente la referencia del bus al valor nominal mediante el tiempo efectivo del control.
    // [Guía] Aquí intervienen intervalo efectivo del control en s y tiempo transcurrido.
    advanceReference(dt, elapsed);
    // [Guía] Actualiza `q1_` con `pi1_.update(error1(in), dt, Config::Q1_TEST_SLEW_PER_SECOND * dt)`. Este dato
    // [Guía] representa consigna de duty de Q1 (0 a 1).
    q1_ = pi1_.update(error1(in), dt, Config::Q1_TEST_SLEW_PER_SECOND * dt);
    // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
    reason_ = q1_ >= Config::Q1_DUTY_MAX ? "M1_DUTY_LIMIT" : "BUS_PI_Q1";
  }
  // PV desaparecio: retirar su actuacion y elegir la ruta segura hacia Q3.
  // No encender Q3 si Q2 aun debe completar su retirada/interlock.
  // [Guía] Intenta mantener el bus con la batería ante la pérdida de PV, sujeto al permiso de descarga. Recibe
  // [Guía] `in`.
  void recoverBattery(const Input& in) {
    // [Guía] Actualiza `pvVoltageControl_` con `false`. Este dato representa selección del control de tensión PV.
    pvVoltageControl_ = false;
    // [Guía] Limpia el estado de la preparación PV para que la siguiente operación arranque con confirmaciones
    // [Guía] nuevas.
    cancelPvAssist();
    // Perdida PV no admite esperar7 s para retirar Q2: cortar rama perdida,
    // respetar interlock y arrancar Q3 con la misma rampa/PI probados.
    // [Guía] Pide apagar la rama indicada con duty cero. El sketch aplica físicamente la consigna.
    q1_ = q2_ = 0; mpptRunning_ = mpptInitialized_ = false;
    // [Guía] Comprueba `!batteryLinkConfirmed_ || !in.batteryValid || !in.socValid`. Si se cumple, ejecuta el
    // [Guía] bloque asociado. Aquí intervienen estimación confirmada de conexión de batería.
    if (!batteryLinkConfirmed_ || !in.batteryValid || !in.socValid
        // [Guía] Añade una alternativa suficiente dentro de la condición compuesta. Aquí intervienen estado de
        // [Guía] carga estimado.
        || !in.q3Configured || in.soc <= Config::BAT_DISCHARGE_MIN_SOC) {
      // [Guía] Lleva las consignas a cero, registra el motivo y limpia confirmaciones previas para esperar una
      // [Guía] fuente.
      wait("WAIT_SOURCE"); return;
    }
    // [Guía] Comprueba `q3_ >= Config::Q3_DUTY_MIN`. Si se cumple, ejecuta el bloque asociado. Aquí intervienen
    // [Guía] consigna de duty de Q3 (0 a 1).
    if (q3_ >= Config::Q3_DUTY_MIN) { enterStart3(in); return; }
    // [Guía] Pide apagar la rama indicada con duty cero. El sketch aplica físicamente la consigna.
    q3_ = 0; enterTransfer(BAT_INTERLOCK, in.nowUs);
    // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
    reason_ = "PV_LOST_BAT_INTERLOCK";
  }
  // START3 es arranque de Q3 -> M5, NO arranque de M3. Se conserva el nombre
  // historico del actuador. Reinicializar PI/ref desde el punto real limita salto.
  // [Guía] Prepara la rampa del PI de bus por Q3. Si Q3 ya entrega potencia, precarga el PI y conserva la salida
  // [Guía] al recuperar M4 o M5. Recibe `in`.
  void enterStart3(const Input& in) {
    // [Guía] Actualiza `pvVoltageControl_` con `false`. Este dato representa selección del control de tensión PV.
    pvVoltageControl_ = false;
    // [Guía] Limpia el estado de la preparación PV para que la siguiente operación arranque con confirmaciones
    // [Guía] nuevas.
    cancelPvAssist();
    // [Guía] Pide apagar la rama indicada con duty cero. El sketch aplica físicamente la consigna.
    q2_ = 0;
    // [Guía] Cambia el estado operativo que se evaluará a partir de la siguiente actualización. Aquí intervienen
    // [Guía] estado operativo interno.
    state_ = START3;
    // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
    reason_ = "Q3_SOFT_START";
    // [Guía] Actualiza `mpptRunning_` con `false`. Este dato representa indicación de MPPT activo.
    mpptRunning_ = false;
    // [Guía] Actualiza `reference_` con `fminf(in.bus, Config::BUS_REFERENCE_V)`. Este dato representa referencia
    // [Guía] del regulador.
    reference_ = fminf(in.bus, Config::BUS_REFERENCE_V);
    // [Guía] Actualiza `transferStart_` con `in.nowUs`.
    transferStart_ = in.nowUs;
    // [Guía] Actualiza `lastControlUs_` con `in.nowUs`.
    lastControlUs_ = in.nowUs;
    // [Guía] Actualiza `nextControlUs_` con `in.nowUs + Config::CONTROL_US`.
    nextControlUs_ = in.nowUs + Config::CONTROL_US;
    // [Guía] Actualiza `lastUs_` con `in.nowUs; haveTime_ = true`.
    lastUs_ = in.nowUs; haveTime_ = true;
    // [Guía] Reinicia las ventanas de confirmación al abandonar su contexto operativo.
    clearHolds();
    // Un aborto de transferencia no debe bajar de nuevo un Q3 que ya aporta.
    // [Guía] Comprueba `q3_ >= Config::Q3_DUTY_MIN`. Si se cumple, ejecuta el bloque asociado. Aquí intervienen
    // [Guía] consigna de duty de Q3 (0 a 1).
    if (q3_ >= Config::Q3_DUTY_MIN) {
      // [Guía] Actualiza `reference_` con `Config::BUS_REFERENCE_V`. Este dato representa referencia del
      // [Guía] regulador.
      reference_ = Config::BUS_REFERENCE_V;
      // [Guía] Actualiza `rampElapsed_` con `Config::SOFT_START_US`. Este dato representa tiempo ya recorrido en
      // [Guía] la rampa.
      rampElapsed_ = Config::SOFT_START_US;
      // [Guía] Reinicia PI de bus aplicado a Q3 con el estado inicial dado en los argumentos. Aquí intervienen PI
      // [Guía] de bus aplicado a Q3 y consigna de duty de Q3 (0 a 1).
      pi3_.reset(error3(in), q3_);
      // [Guía] Comprueba `q1_ >= Config::Q1_DUTY_MIN && solarAvailable(in)`. Si se cumple, ejecuta `enterM4(in)`.
      // [Guía] Aquí intervienen consigna de duty de Q1 (0 a 1).
      if (q1_ >= Config::Q1_DUTY_MIN && solarAvailable(in)) enterM4(in);
      // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
      else { q1_ = 0; state_ = M5; reason_ = "RECOVER_Q3_PI"; }
    }
  }
  // Conexion de bateria desde M1: entregar Q1 a MPPT desde su duty real y
  // preparar rampa Q3. No reiniciar Q1 al5% ni habilitar Q2 en este metodo.
  // [Guía] Prepara la incorporación de la batería desde M1, conservando el duty aplicado a Q1 como punto inicial
  // [Guía] para su nueva tarea. Recibe `in`.
  void enterStart4(const Input& in) {
    // Inicializar MPPT desde Q1 aplicado, sin escalon ni retorno al5%.
    // [Guía] Entra al modo de apoyo de batería al bus y prepara la asignación de Q3 al PI y de Q1 al PV.
    enterM4(in);
    // [Guía] Cambia el estado operativo que se evaluará a partir de la siguiente actualización. Aquí intervienen
    // [Guía] estado operativo interno.
    state_ = START4; q3_ = 0;
    // [Guía] Actualiza `reference_` con `Config::BUS_REFERENCE_V`. Este dato representa referencia del regulador.
    reference_ = Config::BUS_REFERENCE_V;
    // [Guía] Actualiza `rampElapsed_` con `Config::SOFT_START_US`. Este dato representa tiempo ya recorrido en la
    // [Guía] rampa.
    rampElapsed_ = Config::SOFT_START_US;
    // [Guía] Actualiza `transferStart_` con `in.nowUs`.
    transferStart_ = in.nowUs;
    // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
    reason_ = "M1_TO_M4_Q3_RAMP";
    // [Guía] Reinicia las ventanas de confirmación al abandonar su contexto operativo.
    clearHolds();
  }
  // La rampa se expresa en tension, desde la medida inicial hacia20 V durante
  // SOFT_START_US; no es una espera muerta ni un delay de1 s.
  // [Guía] Acerca gradualmente la referencia del bus al valor nominal mediante el tiempo efectivo del control.
  // [Guía] Recibe `dt`, `elapsed`.
  void advanceReference(float dt, uint32_t elapsed) {
    // [Guía] Actualiza `remaining` con `Config::SOFT_START_US - rampElapsed_`.
    const uint32_t remaining = Config::SOFT_START_US - rampElapsed_;
    // [Guía] Acumula en `rampElapsed_` mediante `elapsed < remaining ? elapsed : remaining`. Este dato representa
    // [Guía] tiempo ya recorrido en la rampa.
    rampElapsed_ += elapsed < remaining ? elapsed : remaining;
    // [Guía] Actualiza `reference_` con `fminf(Config::BUS_REFERENCE_V, reference_`. Este dato representa
    // [Guía] referencia del regulador.
    reference_ = fminf(Config::BUS_REFERENCE_V, reference_
      // [Guía] Completa la declaración o expresión con `+ (Config::BUS_REFERENCE_V - reference_) * dt`. Aquí
      // [Guía] intervienen referencia del regulador y intervalo efectivo del control en s.
      + (Config::BUS_REFERENCE_V - reference_) * dt
        // [Guía] Completa la declaración o expresión con `/ fmaxf(dt, remaining * 0.000001f))`. Aquí intervienen
        // [Guía] intervalo efectivo del control en s.
        / fmaxf(dt, remaining * 0.000001f));
    // [Guía] Comprueba `rampElapsed_ == Config::SOFT_START_US`. Si se cumple, ejecuta `reference_ =
    // [Guía] Config::BUS_REFERENCE_V`. Aquí intervienen tiempo ya recorrido en la rampa.
    if (rampElapsed_ == Config::SOFT_START_US) reference_ = Config::BUS_REFERENCE_V;
  }
  // PI del bus de Q3: referencia gradual, limite suave de descarga y slew.
  // Resetear PI al limitar corriente evita acumular integral no aplicable.
  // [Guía] Conserva la referencia preparada de tensión PV con el PI interno de Q1. Recibe `in`, `dt`.
  void holdPreparedPv(const Input& in, float dt) {
    // [Guía] Comprueba `!pvVoltageControl_`. Si se cumple, termina la función en esta rama. Aquí intervienen
    // [Guía] selección del control de tensión PV.
    if (!pvVoltageControl_) return;
    // [Guía] Comprueba `in.batteryI < Config::BAT_DISCHARGE_SOFT_A`. Si se cumple, ejecuta el bloque asociado.
    if (in.batteryI < Config::BAT_DISCHARGE_SOFT_A
        // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
        && -in.batteryI < Config::BAT_CHARGE_SOFT_A)
      // [Guía] Actualiza `q1_` con `pvVoltageMppt_.hold(pvSupervisor_, dt)`. Este dato representa consigna de
      // [Guía] duty de Q1 (0 a 1).
      q1_ = pvVoltageMppt_.hold(pvSupervisor_, dt);
    // [Guía] Actualiza `mpptRunning_` con `mpptInitialized_ = false`. Este dato representa indicación de MPPT
    // [Guía] activo.
    mpptRunning_ = mpptInitialized_ = false;
  }
  // Solo la entrega en caliente preparada por referencia PV. Con bus alto,
  // incluso5% de Q3 es descarga adicional: permitir OFF, no forzar su minimo.
  // Si falta bus, habilitar apoyo con histeresis y slew. Q2 continua OFF.
  // [Guía] Actualiza el apoyo del bus durante la conexión de batería y coordina esa acción con la preparación del
  // [Guía] PV. Recibe `in`, `dt`, `elapsed`.
  void updateHotplugSupport(const Input& in, float dt, uint32_t elapsed) {
    // [Guía] Comprueba `busSupervisor_ <= Config::HOTPLUG_Q3_SUPPORT_ON_V`. Si se cumple, ejecuta
    // [Guía] `hotplugQ3Support_ = true`. Aquí intervienen tensión de bus filtrada para supervisión.
    if (busSupervisor_ <= Config::HOTPLUG_Q3_SUPPORT_ON_V) hotplugQ3Support_ = true;
    // [Guía] Comprueba `busSupervisor_ >= Config::HOTPLUG_Q3_SUPPORT_OFF_V`. Si se cumple, ejecuta
    // [Guía] `hotplugQ3Support_ = false`. Aquí intervienen tensión de bus filtrada para supervisión.
    if (busSupervisor_ >= Config::HOTPLUG_Q3_SUPPORT_OFF_V) hotplugQ3Support_ = false;
    // [Guía] Comprueba `!hotplugQ3Support_ || in.batteryI >= Config::BAT_DISCHARGE_SOFT_A`. Si se cumple, ejecuta
    // [Guía] el bloque asociado.
    if (!hotplugQ3Support_ || in.batteryI >= Config::BAT_DISCHARGE_SOFT_A) {
      // [Guía] Actualiza `q3_` con `approach(q3_, 0, Config::Q3_SLEW_PER_SECOND * dt)`. Este dato representa
      // [Guía] consigna de duty de Q3 (0 a 1).
      q3_ = approach(q3_, 0, Config::Q3_SLEW_PER_SECOND * dt);
      // [Guía] Reinicia PI de bus aplicado a Q3 con el estado inicial dado en los argumentos. Aquí intervienen PI
      // [Guía] de bus aplicado a Q3 y consigna de duty de Q3 (0 a 1).
      pi3_.reset(0, q3_); reason_ = "M4_NO_EXTRA_BAT_DISCHARGE";
    // [Guía] Comprueba `q3_ < Config::Q3_DUTY_MIN`. Si se cumple, ejecuta el bloque asociado. Aquí intervienen
    // [Guía] consigna de duty de Q3 (0 a 1).
    } else if (q3_ < Config::Q3_DUTY_MIN) {
      // [Guía] Actualiza `q3_` con `approach(q3_, Config::Q3_DUTY_MIN, Config::Q3_SLEW_PER_SECOND * dt)`. Este
      // [Guía] dato representa consigna de duty de Q3 (0 a 1).
      q3_ = approach(q3_, Config::Q3_DUTY_MIN, Config::Q3_SLEW_PER_SECOND * dt);
      // [Guía] Reinicia PI de bus aplicado a Q3 con el estado inicial dado en los argumentos. Aquí intervienen PI
      // [Guía] de bus aplicado a Q3 y consigna de duty de Q3 (0 a 1).
      pi3_.reset(0, q3_); reason_ = "M4_Q3_SUPPORT_RAMP";
    // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
    } else update3(in, dt, elapsed);
  }
  // [Guía] Calcula el PI de bus para Q3 a partir de su referencia, error y límite de cambio por paso. Recibe
  // [Guía] `in`, `dt`, `elapsed`.
  void update3(const Input& in, float dt, uint32_t elapsed) {
    // [Guía] Acerca gradualmente la referencia del bus al valor nominal mediante el tiempo efectivo del control.
    // [Guía] Aquí intervienen intervalo efectivo del control en s y tiempo transcurrido.
    advanceReference(dt, elapsed);
    // [Guía] Comprueba `in.batteryI >= Config::BAT_DISCHARGE_SOFT_A`. Si se cumple, ejecuta el bloque asociado.
    if (in.batteryI >= Config::BAT_DISCHARGE_SOFT_A) {
      // [Guía] Actualiza `q3_` con `fmaxf(Config::Q3_DUTY_MIN, q3_ - Config::Q3_SLEW_PER_SECOND * dt)`. Este dato
      // [Guía] representa consigna de duty de Q3 (0 a 1).
      q3_ = fmaxf(Config::Q3_DUTY_MIN, q3_ - Config::Q3_SLEW_PER_SECOND * dt);
      // [Guía] Reinicia PI de bus aplicado a Q3 con el estado inicial dado en los argumentos. Aquí intervienen PI
      // [Guía] de bus aplicado a Q3 y consigna de duty de Q3 (0 a 1).
      pi3_.reset(error3(in), q3_);
      // [Guía] Actualiza `mpptRunning_` con `false`. Este dato representa indicación de MPPT activo.
      mpptRunning_ = false;
      // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
      reason_ = "DISCHARGE_SOFT_LIMIT";
    // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
    } else {
      // [Guía] Actualiza `q3_` con `pi3_.update(error3(in), dt, Config::Q3_SLEW_PER_SECOND * dt)`. Este dato
      // [Guía] representa consigna de duty de Q3 (0 a 1).
      q3_ = pi3_.update(error3(in), dt, Config::Q3_SLEW_PER_SECOND * dt);
      // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
      reason_ = q3_ >= Config::Q3_DUTY_MAX
        // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen tensión rápida
        // [Guía] del bus en V y referencia del regulador.
        && in.bus < reference_ - Config::BAT_BUS_STABLE_BAND_V
        // [Guía] Define una de las dos salidas de la expresión condicional.
        ? "Q3_DUTY_LIMIT" : "BUS_PI_Q3";
    }
  }
  // Registrar el inicio de una transferencia y limpiar holds de etapa anterior.
  // transferStart_ fija un timeout total para no quedar esperando para siempre.
  // [Guía] Registra el destino y el instante inicial de una transferencia. Los estados internos retiran las ramas
  // [Guía] en la secuencia prevista. Recibe `destination`, `now`.
  void enterTransfer(State destination, uint32_t now) {
    // [Guía] Limpia el estado de la preparación PV para que la siguiente operación arranque con confirmaciones
    // [Guía] nuevas.
    cancelPvAssist();
    // [Guía] Borra la confirmación o acumulación en `chargeRetry_` para que no se arrastre al siguiente contexto.
    chargeRetry_.clear();
    // [Guía] Cambia el estado operativo que se evaluará a partir de la siguiente actualización. Aquí intervienen
    // [Guía] estado operativo interno.
    state_ = destination;
    // [Guía] Actualiza `transferStart_` con `now`.
    transferStart_ = now;
    // [Guía] Actualiza `mpptRunning_` con `false`. Este dato representa indicación de MPPT activo.
    mpptRunning_ = false;
    // Mantener la causa durante toda la retirada para que llegue al monitor.
    // [Guía] Comprueba `destination == RETIRE2`. Si se cumple, ejecuta `retire2Reason_ =
    // [Guía] "RETIRE_Q2_RECOVER_SOURCE"`.
    if (destination == RETIRE2) retire2Reason_ = "RETIRE_Q2_RECOVER_SOURCE";
    // [Guía] Borra la confirmación o acumulación en `zeroCurrent_` para que no se arrastre al siguiente contexto.
    zeroCurrent_.clear();
    // [Guía] Borra la confirmación o acumulación en `chargeDischargeHold_` para que no se arrastre al siguiente
    // [Guía] contexto.
    chargeDischargeHold_.clear(); chargeAtMinimum_.clear();
    // [Guía] Borra la confirmación o acumulación en `deficitHold_` para que no se arrastre al siguiente contexto.
    deficitHold_.clear();
    // [Guía] Comprueba `destination == RETIRE3 || destination == RETIRE2`. Si se cumple, ejecuta
    // [Guía] `chargeMarginLost_.clear()`.
    if (destination == RETIRE3 || destination == RETIRE2) chargeMarginLost_.clear();
  }
  // Timeout por diferencia unsigned: sigue funcionando al desbordar micros().
  // [Guía] Expone `uint32_t(in.nowUs - transferStart_) >= Config::BAT_TRANSFER_TIMEOUT_US` al módulo que consulta
  // [Guía] timed out. Recibe `in`.
  bool timedOut(const Input& in) const {
    // [Guía] Entrega `uint32_t(in.nowUs - transferStart_) >= Config::BAT_TRANSFER_TIMEOUT_US` al llamador. Aquí
    // [Guía] intervienen reloj de la muestra en µs.
    return uint32_t(in.nowUs - transferStart_) >= Config::BAT_TRANSFER_TIMEOUT_US;
  }
  // Volver a PV solo. Retirar bateria y preparar control Q1 sin escalon a duty
  // minimo; un PI de bus no hereda directamente la integral de MPPT.
  // [Guía] Transfiere la regulación del bus a Q1 y prepara su PI para partir de una salida coherente con el duty
  // [Guía] aplicado. Recibe `in`.
  void enterM1(const Input& in) {
    // [Guía] Actualiza `pvVoltageControl_` con `false`. Este dato representa selección del control de tensión PV.
    pvVoltageControl_ = false;
    // [Guía] Actualiza `m1LastAuxOhm_` con `0; m1AuxReliefPending_ = false`.
    m1LastAuxOhm_ = 0; m1AuxReliefPending_ = false;
    // [Guía] Actualiza `m1AuxReliefRetries_` con `0; m1AuxReliefReady_.clear()`.
    m1AuxReliefRetries_ = 0; m1AuxReliefReady_.clear();
    // [Guía] Limpia el estado de la preparación PV para que la siguiente operación arranque con confirmaciones
    // [Guía] nuevas.
    cancelPvAssist();
    // Nueva etapa PV sola tras perder evidencia de bateria: aprender reposo
    // nuevo. La retencion breve solo pertenece al mismo episodio M1, no a
    // una base antigua de antes de M4/M3 o de una retirada de bateria.
    // [Guía] Comprueba `!batteryLinkConfirmed_`. Si se cumple, ejecuta `resetM1BatteryDetection()`. Aquí
    // [Guía] intervienen estimación confirmada de conexión de batería.
    if (!batteryLinkConfirmed_) resetM1BatteryDetection();
    // [Guía] Actualiza `m1BatteryConfirmResets_` con `0; m1BatteryConfirmLastReset_ = "NONE"`.
    m1BatteryConfirmResets_ = 0; m1BatteryConfirmLastReset_ = "NONE";
    // [Guía] Actualiza `chargeFlowSeen_` con `false; chargeFlowConfirm_.clear()`.
    chargeFlowSeen_ = false; chargeFlowConfirm_.clear();
    // [Guía] Pide apagar la rama indicada con duty cero. El sketch aplica físicamente la consigna.
    q3_ = 0;
    // [Guía] Comprueba `in.soc >= Config::BAT_CHARGE_MAX_SOC`. Si se cumple, ejecuta el bloque asociado. Aquí
    // [Guía] intervienen estado de carga estimado.
    if (in.soc >= Config::BAT_CHARGE_MAX_SOC
        // [Guía] Añade una alternativa suficiente dentro de la condición compuesta. Aquí intervienen tensión de
        // [Guía] batería calificada y consigna de duty de Q2 (0 a 1).
        || batteryVoltage_ >= Config::BAT_CHARGE_MAX_V) q2_ = 0;
    // [Guía] Actualiza `mpptRunning_` con `false`. Este dato representa indicación de MPPT activo.
    mpptRunning_ = false;
    // [Guía] Comprueba `q1_ < Config::Q1_DUTY_MIN`. Si se cumple, ejecuta el bloque asociado. Aquí intervienen
    // [Guía] consigna de duty de Q1 (0 a 1).
    if (q1_ < Config::Q1_DUTY_MIN) { fail("NO_PV_CONTROL_AUTHORITY"); return; }
    // [Guía] Actualiza `reference_` con `Config::BUS_REFERENCE_V`. Este dato representa referencia del regulador.
    reference_ = Config::BUS_REFERENCE_V;
    // [Guía] Actualiza `rampElapsed_` con `Config::SOFT_START_US`. Este dato representa tiempo ya recorrido en la
    // [Guía] rampa.
    rampElapsed_ = Config::SOFT_START_US;
    // [Guía] Prepara la captura de bus mediante Q1 antes del PI de M1, usando el punto de partida disponible.
    prepareM1(in);
    // [Guía] Cambia el estado operativo que se evaluará a partir de la siguiente actualización. Aquí intervienen
    // [Guía] estado operativo interno.
    state_ = q2_ > 0 ? RETIRE2_TO_M1 : M1;
    // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
    reason_ = !in.q2Configured ? "Q2_CONFIG_REQUIRED"
      // [Guía] Define una de las dos salidas de la expresión condicional. Aquí intervienen estimación confirmada
      // [Guía] de conexión de batería.
      : !batteryLinkConfirmed_ ? "BAT_LINK_NOT_ESTIMATED"
      // [Guía] Define una de las dos salidas de la expresión condicional. Aquí intervienen tensión de batería
      // [Guía] calificada.
      : batteryVoltage_ >= Config::BAT_CHARGE_MAX_V ? "BAT_CHARGE_VOLTAGE_CEILING"
      // [Guía] Define una de las dos salidas de la expresión condicional. Aquí intervienen estado de carga
      // [Guía] estimado.
      : in.soc >= Config::BAT_CHARGE_EXIT_SOC ? "BAT_CHARGE_SOC_CEILING"
      // [Guía] Define una de las dos salidas de la expresión condicional.
      : "PV_BUS_PI_NO_CHARGE";
    // [Guía] Reinicia las ventanas de confirmación al abandonar su contexto operativo.
    clearHolds();
  }
  // Configurar reparto PV MPPT + bateria al bus. Reiniciar P&O desde Q1 real
  // descarta comparaciones de potencia tomadas con otra carga/otro propietario.
  // [Guía] Entra al modo de apoyo de batería al bus y prepara la asignación de Q3 al PI y de Q1 al PV. Recibe
  // [Guía] `in`.
  void enterM4(const Input& in) {
    // [Guía] Actualiza `pvVoltageControl_` con `false`. Este dato representa selección del control de tensión PV.
    pvVoltageControl_ = false;
    // [Guía] Limpia el estado de la preparación PV para que la siguiente operación arranque con confirmaciones
    // [Guía] nuevas.
    cancelPvAssist();
    // [Guía] Borra la confirmación o acumulación en `prepHighBus_` para que no se arrastre al siguiente contexto.
    prepHighBus_.clear(); chargeMarginLost_.clear();
    // [Guía] Cambia el estado operativo que se evaluará a partir de la siguiente actualización. Aquí intervienen
    // [Guía] estado operativo interno.
    state_ = M4; q2_ = 0; reason_ = "MPPT_AND_BUS_PI_Q3";
    // Transferir Q1 desde su duty real de M1/M5; no volverlo a5% si ya regula.
    // [Guía] Prepara buscador P&O en la coordenada de duty para empezar desde las medidas o consignas indicadas.
    // [Guía] Aquí intervienen buscador P&O en la coordenada de duty y consigna de duty de Q1 (0 a 1).
    mppt_.begin(q1_, in.nowUs); mpptTarget_ = q1_; mpptInitialized_ = true;
    // [Guía] Actualiza `mpptRunning_` con `true; chargeAtMinimum_.clear(); deficitHold_.clear()`. Este dato
    // [Guía] representa indicación de MPPT activo.
    mpptRunning_ = true; chargeAtMinimum_.clear(); deficitHold_.clear();
  }
  // Solo transferencia/primeros pulsos Q2: exigir margen PV fresco confirmado.
  // V39 no usa este criterio como salida autonoma del M3 ya establecido.
  // [Guía] Comprueba si durante la entrega a Q2 se perdió el margen entre las tensiones de PV y batería. Recibe
  // [Guía] `in`.
  bool handoffMarginLost(const Input& in) {
    // [Guía] Entrega `settings_.strictChargeMargin && chargeMarginLost_.held(` al llamador.
    return settings_.strictChargeMargin && chargeMarginLost_.held(
      // [Guía] Completa la declaración o expresión con `!pvSupervisorReady_ || pvSupervisor_ < chargePvMinimum(),
      // [Guía] in.nowUs, 100000)`. Aquí intervienen tensión de PV filtrada para supervisión y reloj de la muestra
      // [Guía] en µs.
      !pvSupervisorReady_ || pvSupervisor_ < chargePvMinimum(), in.nowUs, 100000);
  }
  // Cancelar un handoff sin solapar Q2/Q3; guardar motivo y presupuesto para
  // que regresar a M4 no cree reintentos infinitos sin evidencia solar nueva.
  // [Guía] Retira la rama de carga mediante una transferencia cuando no se puede sostener el margen PV requerido.
  // [Guía] Recibe `in`.
  void retireForPvMargin(const Input& in) {
    // [Guía] Actualiza `pvAssistAttempted_` con `false`.
    pvAssistAttempted_ = false;
    // [Guía] Comprueba `chargeRetryReferenceReady_`. Si se cumple, ejecuta el bloque asociado.
    if (chargeRetryReferenceReady_) {
      // [Guía] Actualiza `chargeRetryPv_` con `fmaxf(chargeRetryPv_, pvSupervisor_)`.
      chargeRetryPv_ = fmaxf(chargeRetryPv_, pvSupervisor_);
      // [Guía] Actualiza `chargeRetrySinceUs_` con `in.nowUs`.
      chargeRetrySinceUs_ = in.nowUs;
    }
    // [Guía] Comprueba `q2_ > 0`. Si se cumple, ejecuta el bloque asociado. Aquí intervienen consigna de duty de
    // [Guía] Q2 (0 a 1).
    if (q2_ > 0) {
      // [Guía] Registra el destino y el instante inicial de una transferencia. Aquí intervienen reloj de la
      // [Guía] muestra en µs.
      enterTransfer(RETIRE2, in.nowUs);
      // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
      reason_ = retire2Reason_ = "Q2_RETIRE_PV_MARGIN_REPREPARE";
    // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
    } else {
      // [Guía] Prepara la rampa del PI de bus por Q3.
      enterStart3(in);
      // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
      reason_ = "M4_PREP_MARGIN_LOST";
    }
  }
  // Diagnostico del primer permiso ausente; no modifica salidas ni rearma nada.
  // [Guía] Selecciona una etiqueta que explica cuál de los requisitos de carga o preparación PV sigue pendiente.
  // [Guía] Recibe `in`.
  const char* chargeBlockReason(const Input& in) const {
    // [Guía] Comprueba `!batteryLinkConfirmed_`. Si se cumple, termina la función en esta rama. Aquí intervienen
    // [Guía] estimación confirmada de conexión de batería.
    if (!batteryLinkConfirmed_) return "M4_PREP_WAIT_BAT_LINK";
    // [Guía] Comprueba `!in.q2Configured`. Si se cumple, termina la función en esta rama.
    if (!in.q2Configured) return "M4_PREP_WAIT_Q2_CONFIG";
    // [Guía] Comprueba `!validGains(settings_.kp2, settings_.ki2, settings_.action2)`. Si se cumple, termina la
    // [Guía] función en esta rama.
    if (!validGains(settings_.kp2, settings_.ki2, settings_.action2)) return "M4_PREP_WAIT_Q2_GAINS";
    // [Guía] Comprueba `!in.socValid || in.soc >= Config::BAT_CHARGE_EXIT_SOC`. Si se cumple, termina la función
    // [Guía] en esta rama. Aquí intervienen estado de carga estimado.
    if (!in.socValid || in.soc >= Config::BAT_CHARGE_EXIT_SOC) return "M4_PREP_WAIT_SOC";
    // [Guía] Entrega `"M4_PREP_WAIT_BAT_VOLTAGE"` al llamador.
    return "M4_PREP_WAIT_BAT_VOLTAGE";
  }
  // Acotar parametros de la ayuda de tension antes de permitir mover Q1.
  // [Guía] Valida referencias y límites de la rutina que desplaza la tensión PV.
  bool validPvAssistSettings() const {
    // [Guía] Entrega `isfinite(settings_.pvAssistSlewPerSecond) && settings_.pvAssistSlewPerSecond > 0` al
    // [Guía] llamador.
    return isfinite(settings_.pvAssistSlewPerSecond) && settings_.pvAssistSlewPerSecond > 0
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
      && isfinite(settings_.pvAssistMaxDutyDrop)
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
      && settings_.pvAssistMaxDutyDrop > 0 && settings_.pvAssistMaxDutyDrop <= 0.25f
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
      && settings_.pvAssistTimeoutUs > 200000 && settings_.pvAssistTimeoutUs <= 8000000;
  }
  // Retirar propiedad temporal de Q1 y la peticion en caliente pendiente.
  // No borrar aqui indiscriminadamente el numero de intentos del episodio.
  // [Guía] Limpia el estado de la preparación PV para que la siguiente operación arranque con confirmaciones
  // [Guía] nuevas.
  void cancelPvAssist() {
    // [Guía] Actualiza `hotplugPvSeeking_` con `hotplugPvQualified_ = false; hotplugPvReady_.clear()`.
    hotplugPvSeeking_ = hotplugPvQualified_ = false; hotplugPvReady_.clear();
    // [Guía] Actualiza `pvAssistPending_` con `false`.
    pvAssistPending_ = false;
    // [Guía] Actualiza `pvAssistHotplugPending_` con `false`.
    pvAssistHotplugPending_ = false;
    // [Guía] Actualiza `pvAssistStage_` con `PV_ASSIST_OFF`.
    pvAssistStage_ = PV_ASSIST_OFF;
    // [Guía] Actualiza `pvAssistResult_` con `nullptr`.
    pvAssistResult_ = nullptr;
    // [Guía] Borra la confirmación o acumulación en `pvAssistReady_` para que no se arrastre al siguiente
    // [Guía] contexto.
    pvAssistReady_.clear(); pvAssistBusWorse_.clear();
    // [Guía] Borra la confirmación o acumulación en `pvAssistReverse_` para que no se arrastre al siguiente
    // [Guía] contexto.
    pvAssistReverse_.clear(); pvAssistDeficit_.clear();
  }
  // Terminar ayuda con motivo visible y reiniciar P&O desde el duty alcanzado.
  // El exito de esta preparacion NO significa que M3 ya este regulando.
  // [Guía] Cierra la preparación PV y conserva el resultado para diagnóstico y para la etapa siguiente. Recibe
  // [Guía] `in`, `result`.
  void finishPvAssist(const Input& in, const char* result) {
    // [Guía] Limpia el estado de la preparación PV para que la siguiente operación arranque con confirmaciones
    // [Guía] nuevas.
    cancelPvAssist();
    // [Guía] Actualiza `pvAssistResult_` con `result`.
    pvAssistResult_ = result;
    // Descartar la comparacion de potencia anterior a la ayuda, sin mover Q1.
    // [Guía] Prepara buscador P&O en la coordenada de duty para empezar desde las medidas o consignas indicadas.
    // [Guía] Aquí intervienen buscador P&O en la coordenada de duty y consigna de duty de Q1 (0 a 1).
    mppt_.begin(q1_, in.nowUs); mpptTarget_ = q1_;
    // [Guía] Actualiza `mpptInitialized_` con `true; mpptRunning_ = true`.
    mpptInitialized_ = true; mpptRunning_ = true;
  }
  // Ayuda supervisada: asentamiento inicial -> disminuir Q1 -> confirmar Vpv.
  // Verificar tiempo, deficit, corriente, sentido de respuesta y excursion.
  // Mientras devuelve true posee Q1 y MPPT no debe escribir un segundo duty.
  // [Guía] Modifica la consigna de Q1 para preparar Vpv antes de la entrada al control de carga. El resultado es
  // [Guía] una decisión booleana para el llamador. Recibe `in`, `dt`, `chargeAllowed`.
  bool runPvAssist(const Input& in, float dt, bool chargeAllowed) {
    // [Guía] Comprueba `!pvAssistPending_ && !pvAssistActive()`. Si se cumple, termina la función en esta rama.
    if (!pvAssistPending_ && !pvAssistActive()) return false;
    // [Guía] Comprueba `!validPvAssistSettings() || !settings_.pvAssistEnabled || !chargeAllowed`. Si se cumple,
    // [Guía] ejecuta el bloque asociado. Aquí intervienen permiso de carga de batería.
    if (!validPvAssistSettings() || !settings_.pvAssistEnabled || !chargeAllowed
        // [Guía] Añade una alternativa suficiente dentro de la condición compuesta.
        || !pvSupervisorReady_ || in.pvI <= Config::BAT_PV_ON_CURRENT_A) {
      // [Guía] Actualiza `cause` con `!chargeAllowed ? chargeBlockReason(in)`.
      const char* cause = !chargeAllowed ? chargeBlockReason(in)
        // [Guía] Define una de las dos salidas de la expresión condicional.
        : !pvSupervisorReady_ || in.pvI <= Config::BAT_PV_ON_CURRENT_A ? "M4_PREP_WAIT_PV_FLOW"
        // [Guía] Define una de las dos salidas de la expresión condicional.
        : !settings_.pvAssistEnabled ? "M4_PREP_DISABLED" : "M4_PREP_SETTINGS_INVALID";
      // [Guía] Cierra la preparación PV y conserva el resultado para diagnóstico y para la etapa siguiente.
      finishPvAssist(in, settings_.strictChargeMargin ? cause : "M4_MPPT_LIFT_NOT_ALLOWED");
      // [Guía] Comprueba `settings_.strictChargeMargin && (!chargeAllowed || !pvSupervisorReady_`. Si se cumple,
      // [Guía] ejecuta el bloque asociado. Aquí intervienen permiso de carga de batería.
      if (settings_.strictChargeMargin && (!chargeAllowed || !pvSupervisorReady_
          // [Guía] Añade una alternativa suficiente dentro de la condición compuesta.
          || in.pvI <= Config::BAT_PV_ON_CURRENT_A)) pvAssistAttempted_ = false;
      // [Guía] Entrega `false` al llamador.
      return false;
    }
    // [Guía] Comprueba `pvAssistPending_`. Si se cumple, ejecuta el bloque asociado.
    if (pvAssistPending_) {
      // [Guía] Actualiza `pvAssistPending_` con `false`.
      pvAssistPending_ = false;
      // [Guía] Comprueba `pvSupervisor_ >= pvAssistTarget()`. Si se cumple, ejecuta el bloque asociado. Aquí
      // [Guía] intervienen tensión de PV filtrada para supervisión.
      if (pvSupervisor_ >= pvAssistTarget()
          // [Guía] Agrupa condiciones alternativas y conjuntas en la prueba en curso. Aquí intervienen tensión
          // [Guía] del PV en V.
          && (settings_.strictChargeMargin || in.pv >= pvAssistTarget())) return false;
      // [Guía] Actualiza `pvAssistStartUs_` con `in.nowUs`.
      pvAssistStartUs_ = in.nowUs;
      // [Guía] Comprueba `settings_.strictChargeMargin`. Si se cumple, ejecuta `++pvAssistStarts_`.
      if (settings_.strictChargeMargin) ++pvAssistStarts_;
      // [Guía] Actualiza `pvAssistFloor_` con `fmaxf(Config::Q1_DUTY_MIN, q1_ - settings_.pvAssistMaxDutyDrop)`.
      pvAssistFloor_ = fmaxf(Config::Q1_DUTY_MIN, q1_ - settings_.pvAssistMaxDutyDrop);
      // [Guía] Actualiza `pvAssistStage_` con `PV_ASSIST_SETTLE`.
      pvAssistStage_ = PV_ASSIST_SETTLE;
      // [Guía] Actualiza `mpptRunning_` con `mpptInitialized_ = false`. Este dato representa indicación de MPPT
      // [Guía] activo.
      mpptRunning_ = mpptInitialized_ = false;
      // [Guía] Suspende temporalmente el avance de buscador P&O en la coordenada de duty. Aquí intervienen
      // [Guía] buscador P&O en la coordenada de duty y reloj de la muestra en µs.
      mppt_.pause(in.nowUs);
    }
    // [Guía] Comprueba `uint32_t(in.nowUs - pvAssistStartUs_) >= settings_.pvAssistTimeoutUs`. Si se cumple,
    // [Guía] ejecuta el bloque asociado. Aquí intervienen reloj de la muestra en µs.
    if (uint32_t(in.nowUs - pvAssistStartUs_) >= settings_.pvAssistTimeoutUs) {
      // [Guía] Cierra la preparación PV y conserva el resultado para diagnóstico y para la etapa siguiente.
      finishPvAssist(in, "M4_MPPT_LIFT_TIMEOUT"); return false;
    }
    // [Guía] Comprueba `pvAssistDeficit_.held(busSupervisor_ < Config::BUS_REFERENCE_V - 0.75f,`. Si se cumple,
    // [Guía] ejecuta el bloque asociado. Aquí intervienen tensión de bus filtrada para supervisión.
    if (pvAssistDeficit_.held(busSupervisor_ < Config::BUS_REFERENCE_V - 0.75f,
        // [Guía] Completa la declaración o expresión con `in.nowUs, Config::EMS_DEFICIT_HOLD_US)) {`. Aquí
        // [Guía] intervienen reloj de la muestra en µs.
        in.nowUs, Config::EMS_DEFICIT_HOLD_US)) {
      // [Guía] Cierra la preparación PV y conserva el resultado para diagnóstico y para la etapa siguiente.
      finishPvAssist(in, "M4_MPPT_LIFT_BUS_DEFICIT"); return false;
    }
    // [Guía] Comprueba `in.batteryI >= Config::BAT_DISCHARGE_SOFT_A`. Si se cumple, ejecuta el bloque asociado.
    if (in.batteryI >= Config::BAT_DISCHARGE_SOFT_A
        // [Guía] Añade una alternativa suficiente dentro de la condición compuesta.
        || -in.batteryI >= Config::BAT_CHARGE_SOFT_A) {
      // [Guía] Cierra la preparación PV y conserva el resultado para diagnóstico y para la etapa siguiente.
      finishPvAssist(in, "M4_MPPT_LIFT_CURRENT_LIMIT"); return false;
    }
    // [Guía] Actualiza `mpptRunning_` con `false`. Este dato representa indicación de MPPT activo.
    mpptRunning_ = false;
    // [Guía] Comprueba `pvAssistStage_ == PV_ASSIST_SETTLE`. Si se cumple, ejecuta el bloque asociado.
    if (pvAssistStage_ == PV_ASSIST_SETTLE) {
      // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
      reason_ = "M4_PV_LIFT_SETTLE";
      // [Guía] Comprueba `uint32_t(in.nowUs - pvAssistStartUs_) < 200000`. Si se cumple, termina la función en
      // [Guía] esta rama. Aquí intervienen reloj de la muestra en µs.
      if (uint32_t(in.nowUs - pvAssistStartUs_) < 200000) return true;
      // Medir la respuesta respecto al punto sin perturbacion de Q1.
      // [Guía] Actualiza `pvAssistStartPv_` con `pvAssistCheckpointPv_ = pvSupervisor_`.
      pvAssistStartPv_ = pvAssistCheckpointPv_ = pvSupervisor_;
      // [Guía] Actualiza `pvAssistStartBus_` con `busSupervisor_`.
      pvAssistStartBus_ = busSupervisor_;
      // [Guía] Actualiza `pvAssistCheckpointBus_` con `busSupervisor_`.
      pvAssistCheckpointBus_ = busSupervisor_;
      // [Guía] Actualiza `pvAssistCheckpointUs_` con `in.nowUs`.
      pvAssistCheckpointUs_ = in.nowUs;
      // [Guía] Actualiza `pvAssistStage_` con `PV_ASSIST_RAMP`.
      pvAssistStage_ = PV_ASSIST_RAMP;
    }
    // [Guía] Comprueba `pvAssistBusWorse_.held(in.bus > pvAssistStartBus_ + 0.75f`. Si se cumple, ejecuta el
    // [Guía] bloque asociado. Aquí intervienen tensión rápida del bus en V.
    if (pvAssistBusWorse_.held(in.bus > pvAssistStartBus_ + 0.75f
          // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen tensión de
          // [Guía] bus filtrada para supervisión y reloj de la muestra en µs.
          && busSupervisor_ > pvAssistStartBus_ + 0.75f, in.nowUs, 50000)) {
      // [Guía] Cierra la preparación PV y conserva el resultado para diagnóstico y para la etapa siguiente.
      finishPvAssist(in, "M4_MPPT_LIFT_BUS_WORSE"); return false;
    }
    // [Guía] Comprueba `pvAssistReverse_.held(in.pv < pvAssistStartPv_ - 0.40f`. Si se cumple, ejecuta el bloque
    // [Guía] asociado. Aquí intervienen tensión del PV en V.
    if (pvAssistReverse_.held(in.pv < pvAssistStartPv_ - 0.40f
          // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen tensión de
          // [Guía] PV filtrada para supervisión y reloj de la muestra en µs.
          && pvSupervisor_ < pvAssistStartPv_ - 0.40f, in.nowUs, 100000)) {
      // [Guía] Cierra la preparación PV y conserva el resultado para diagnóstico y para la etapa siguiente.
      finishPvAssist(in, "M4_MPPT_LIFT_PV_REVERSED"); return false;
    }
    // [Guía] Actualiza `target` con `pvAssistTarget()`.
    const float target = pvAssistTarget();
    // [Guía] Actualiza `atTarget` con `pvSupervisor_ >= target`.
    const bool atTarget = pvSupervisor_ >= target
      // [Guía] Agrupa condiciones alternativas y conjuntas en la prueba en curso. Aquí intervienen tensión del PV
      // [Guía] en V.
      && (settings_.strictChargeMargin || in.pv >= target);
    // [Guía] Comprueba `pvAssistReady_.held(atTarget, in.nowUs, 100000)`. Si se cumple, ejecuta el bloque
    // [Guía] asociado. Aquí intervienen reloj de la muestra en µs.
    if (pvAssistReady_.held(atTarget, in.nowUs, 100000)) {
      // [Guía] Cierra la preparación PV y conserva el resultado para diagnóstico y para la etapa siguiente.
      finishPvAssist(in, "M4_MPPT_LIFT_READY"); return false;
    }
    // V35 usa el supervisor para que un pico rapido no frene toda la busqueda.
    // [Guía] Comprueba `pvSupervisor_ >= target || (!settings_.strictChargeMargin && in.pv >= target)`. Si se
    // [Guía] cumple, ejecuta el bloque asociado. Aquí intervienen tensión de PV filtrada para supervisión y
    // [Guía] tensión del PV en V.
    if (pvSupervisor_ >= target || (!settings_.strictChargeMargin && in.pv >= target)) {
      // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
      reason_ = "M4_PV_LIFT_CONFIRM";
      // [Guía] Entrega `true` al llamador.
      return true;
    }
    // [Guía] Comprueba `uint32_t(in.nowUs - pvAssistCheckpointUs_) >= 1000000`. Si se cumple, ejecuta el bloque
    // [Guía] asociado. Aquí intervienen reloj de la muestra en µs.
    if (uint32_t(in.nowUs - pvAssistCheckpointUs_) >= 1000000) {
      // [Guía] Comprueba `pvSupervisor_ < pvAssistCheckpointPv_ + 0.10f`. Si se cumple, ejecuta el bloque
      // [Guía] asociado. Aquí intervienen tensión de PV filtrada para supervisión.
      if (pvSupervisor_ < pvAssistCheckpointPv_ + 0.10f
          // [Guía] Agrupa condiciones alternativas y conjuntas en la prueba en curso. Aquí intervienen tensión de
          // [Guía] bus filtrada para supervisión.
          && (!settings_.strictChargeMargin || busSupervisor_ > pvAssistCheckpointBus_ - 0.50f)) {
        // [Guía] Cierra la preparación PV y conserva el resultado para diagnóstico y para la etapa siguiente.
        finishPvAssist(in, "M4_MPPT_LIFT_NO_RESPONSE"); return false;
      }
      // [Guía] Actualiza `pvAssistCheckpointPv_` con `pvSupervisor_`.
      pvAssistCheckpointPv_ = pvSupervisor_;
      // [Guía] Actualiza `pvAssistCheckpointBus_` con `busSupervisor_`.
      pvAssistCheckpointBus_ = busSupervisor_;
      // [Guía] Actualiza `pvAssistCheckpointUs_` con `in.nowUs`.
      pvAssistCheckpointUs_ = in.nowUs;
    }
    // [Guía] Comprueba `q1_ <= pvAssistFloor_ + 0.000001f`. Si se cumple, ejecuta el bloque asociado. Aquí
    // [Guía] intervienen consigna de duty de Q1 (0 a 1).
    if (q1_ <= pvAssistFloor_ + 0.000001f) {
      // [Guía] Cierra la preparación PV y conserva el resultado para diagnóstico y para la etapa siguiente.
      finishPvAssist(in, "M4_MPPT_LIFT_DUTY_LIMIT"); return false;
    }
    // Sentido observado en el banco: bajar Q1 eleva Vpv. La respuesta anterior
    // lo supervisa; no se presupone que sirva para cualquier punto o topologia.
    // [Guía] Actualiza `slew` con `fminf(settings_.pvAssistSlewPerSecond, Config::Q1_TEST_SLEW_PER_SECOND)`.
    float slew = fminf(settings_.pvAssistSlewPerSecond, Config::Q1_TEST_SLEW_PER_SECOND);
    // [Guía] Comprueba `settings_.strictChargeMargin`. Si se cumple, ejecuta `slew *= fminf(1.0f, fmaxf(0.20f,
    // [Guía] target - pvSupervisor_))`.
    if (settings_.strictChargeMargin) slew *= fminf(1.0f, fmaxf(0.20f, target - pvSupervisor_));
    // [Guía] Actualiza `q1_` con `approach(q1_, pvAssistFloor_, slew * dt)`. Este dato representa consigna de
    // [Guía] duty de Q1 (0 a 1).
    q1_ = approach(q1_, pvAssistFloor_, slew * dt);
    // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
    reason_ = "M4_PV_LIFT_Q1";
    // [Guía] Entrega `true` al llamador.
    return true;
  }
  // El supervisor tiene su filtro existente50ms; no cambiar el filtro usado
  // por los PI. El piso rapido Vbat+2 rechaza una caida energetica real, pero
  // tolera rizado alrededor18.4 V como el registro fisico16.9..20.5 V.
  // [Guía] Comprueba si la tensión PV medida cumple el objetivo de la preparación por conexión de batería. Recibe
  // [Guía] `in`.
  bool hotplugPvTargetObserved(const Input& in) const {
    // [Guía] Entrega `hotplugPvSeeking_ && in.pvValid && pvSupervisorReady_` al llamador.
    return hotplugPvSeeking_ && in.pvValid && pvSupervisorReady_
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen tensión del PV
      // [Guía] en V.
      && isfinite(in.pv) && isfinite(in.pvRaw) && isfinite(in.pvI)
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen tensión de PV
      // [Guía] filtrada para supervisión.
      && pvSupervisor_ >= Config::HOTPLUG_PV_TARGET_V - Config::HOTPLUG_PV_TOLERANCE_V
      // [Guía] Agrupa condiciones alternativas y conjuntas en la prueba en curso. Aquí intervienen selección del
      // [Guía] control de tensión PV y tensión de PV filtrada para supervisión.
      && (!pvVoltageControl_ || pvSupervisor_ <= Config::HOTPLUG_PV_TARGET_V + Config::HOTPLUG_PV_TOLERANCE_V)
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen tensión del PV
      // [Guía] en V.
      && in.pv >= chargePvMinimum() && in.pvRaw >= Config::M1_INPUT_PV_RAW_MIN_V
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
      && in.pvI > Config::BAT_PV_ON_CURRENT_A
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
      && in.batteryValid && isfinite(in.batteryI)
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
      && in.batteryI < Config::BAT_DISCHARGE_SOFT_A
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
      && -in.batteryI < Config::BAT_CHARGE_SOFT_A;
  }
  // Preparacion V45/V46 tras conectar bateria en M1. true significa Q1 ocupado:
  // ni P&O ni la transferencia a M3 pueden competir con esta maniobra.
  // [Guía] Desplaza Q1 hacia la región de tensión PV requerida y califica el objetivo antes de entregarlo al
  // [Guía] MPPT. El resultado es una decisión booleana para el llamador. Recibe `in`, `dt`.
  bool runHotplugPvSeek(const Input& in, float dt) {
    // [Guía] Comprueba `!hotplugPvSeeking_`. Si se cumple, termina la función en esta rama.
    if (!hotplugPvSeeking_) return false;
    // [Guía] Actualiza `mpptRunning_` con `mpptInitialized_ = false`. Este dato representa indicación de MPPT
    // [Guía] activo.
    mpptRunning_ = mpptInitialized_ = false;
    // [Guía] Comprueba `pvVoltageControl_`. Si se cumple, ejecuta el bloque asociado. Aquí intervienen selección
    // [Guía] del control de tensión PV.
    if (pvVoltageControl_) {
      // [Guía] Comprueba `hotplugPvQualified_ && hotplugPvTargetObserved(in)`. Si se cumple, ejecuta el bloque
      // [Guía] asociado.
      if (hotplugPvQualified_ && hotplugPvTargetObserved(in)) {
        // [Guía] Actualiza `hotplugPvSeeking_` con `false`.
        hotplugPvSeeking_ = false;
        // [Guía] Actualiza `pvAssistPending_` con `pvAssistHotplugPending_ = false`.
        pvAssistPending_ = pvAssistHotplugPending_ = false;
        // [Guía] Actualiza `pvAssistResult_` con `"PV_18V4_READY_HANDOFF"`.
        pvAssistResult_ = "PV_18V4_READY_HANDOFF";
        // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
        reason_ = "PV_18V4_READY_HANDOFF"; return false;
      }
      // Regular en ambos sentidos, no solo bajar duty y congelarlo al tocar
      //18.4V. Conservar esta referencia durante toda la entrega a Q2.
      // [Guía] Comprueba `uint32_t(in.nowUs - hotplugPvSinceUs_) < Config::HOTPLUG_PV_TIMEOUT_US`. Si se cumple,
      // [Guía] ejecuta el bloque asociado. Aquí intervienen reloj de la muestra en µs.
      if (uint32_t(in.nowUs - hotplugPvSinceUs_) < Config::HOTPLUG_PV_TIMEOUT_US
          // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
          && in.batteryI < Config::BAT_DISCHARGE_SOFT_A
          // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
          && -in.batteryI < Config::BAT_CHARGE_SOFT_A)
        // [Guía] Actualiza `q1_` con `pvVoltageMppt_.hold(pvSupervisor_, dt)`. Este dato representa consigna de
        // [Guía] duty de Q1 (0 a 1).
        q1_ = pvVoltageMppt_.hold(pvSupervisor_, dt);
      // [Guía] Actualiza `hotplugPvReason_` con `uint32_t(in.nowUs - hotplugPvSinceUs_) >=
      // [Guía] Config::HOTPLUG_PV_TIMEOUT_US`.
      hotplugPvReason_ = uint32_t(in.nowUs - hotplugPvSinceUs_) >= Config::HOTPLUG_PV_TIMEOUT_US
        // [Guía] Define una de las dos salidas de la expresión condicional.
        ? "PV_18V4_TIMEOUT_HOLD" : "PV_18V4_REFERENCE_Q1";
      // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
      reason_ = hotplugPvReason_; return true;
    }
    // [Guía] Actualiza `reached` con `hotplugPvTargetObserved(in)`.
    const bool reached = hotplugPvTargetObserved(in);
    // [Guía] Comprueba `hotplugPvQualified_ && reached`. Si se cumple, ejecuta el bloque asociado.
    if (hotplugPvQualified_ && reached) {
      // [Guía] Actualiza `hotplugPvSeeking_` con `false`.
      hotplugPvSeeking_ = false;
      // [Guía] Actualiza `pvAssistPending_` con `pvAssistHotplugPending_ = false`.
      pvAssistPending_ = pvAssistHotplugPending_ = false;
      // [Guía] Actualiza `pvAssistResult_` con `"PV_18V4_READY_MPPT"`.
      pvAssistResult_ = "PV_18V4_READY_MPPT";
      // [Guía] Prepara buscador P&O en la coordenada de duty para empezar desde las medidas o consignas
      // [Guía] indicadas. Aquí intervienen buscador P&O en la coordenada de duty y consigna de duty de Q1 (0 a
      // [Guía] 1).
      mppt_.begin(q1_, in.nowUs); mpptTarget_ = q1_;
      // [Guía] Actualiza `mpptInitialized_` con `mpptRunning_ = true`.
      mpptInitialized_ = mpptRunning_ = true;
      // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
      reason_ = "PV_18V4_READY_MPPT";
      // [Guía] Entrega `false` al llamador.
      return false;
    }
    // Limitar la maniobra sin declarar exito ficticio ni liberar el MPPT.
    // Si no alcanza la referencia se conserva PI3 y se informa el impedimento.
    // [Guía] Actualiza `hotplugPvReason_` con `"PV_18V4_SEEK_Q1"`.
    hotplugPvReason_ = "PV_18V4_SEEK_Q1";
    // [Guía] Comprueba `uint32_t(in.nowUs - hotplugPvSinceUs_) >= Config::HOTPLUG_PV_TIMEOUT_US`. Si se cumple,
    // [Guía] ejecuta el bloque asociado. Aquí intervienen reloj de la muestra en µs.
    if (uint32_t(in.nowUs - hotplugPvSinceUs_) >= Config::HOTPLUG_PV_TIMEOUT_US)
      // [Guía] Actualiza `hotplugPvReason_` con `"PV_18V4_TIMEOUT_HOLD"`.
      hotplugPvReason_ = "PV_18V4_TIMEOUT_HOLD";
    // [Guía] Comprueba `in.batteryI >= Config::BAT_DISCHARGE_SOFT_A`. Si se cumple, ejecuta el bloque asociado.
    else if (in.batteryI >= Config::BAT_DISCHARGE_SOFT_A
        // [Guía] Añade una alternativa suficiente dentro de la condición compuesta.
        || -in.batteryI >= Config::BAT_CHARGE_SOFT_A)
      // [Guía] Actualiza `hotplugPvReason_` con `"PV_18V4_CURRENT_HOLD"`.
      hotplugPvReason_ = "PV_18V4_CURRENT_HOLD";
    // [Guía] Comprueba `reached || in.pv >= Config::HOTPLUG_PV_TARGET_V`. Si se cumple, ejecuta el bloque
    // [Guía] asociado. Aquí intervienen tensión del PV en V.
    else if (reached || in.pv >= Config::HOTPLUG_PV_TARGET_V
        // [Guía] Añade una alternativa suficiente dentro de la condición compuesta. Aquí intervienen tensión de
        // [Guía] PV filtrada para supervisión.
        || pvSupervisor_ >= Config::HOTPLUG_PV_TARGET_V)
      // [Guía] Actualiza `hotplugPvReason_` con `"PV_18V4_CONFIRM"`.
      hotplugPvReason_ = "PV_18V4_CONFIRM";
    // [Guía] Comprueba `q1_ <= Config::Q1_DUTY_MIN`. Si se cumple, ejecuta `hotplugPvReason_ =
    // [Guía] "PV_18V4_DUTY_MIN_HOLD"`. Aquí intervienen consigna de duty de Q1 (0 a 1).
    else if (q1_ <= Config::Q1_DUTY_MIN) hotplugPvReason_ = "PV_18V4_DUTY_MIN_HOLD";
    // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
    else {
      // Reducir progresivamente, sin fijar un duty de MPP ni saltar al5%.
      // [Guía] Actualiza `speed` con `fminf(Config::HOTPLUG_PV_SLEW_PER_SECOND,
      // [Guía] Config::Q1_TEST_SLEW_PER_SECOND)`.
      const float speed = fminf(Config::HOTPLUG_PV_SLEW_PER_SECOND, Config::Q1_TEST_SLEW_PER_SECOND);
      // [Guía] Actualiza `q1_` con `approach(q1_, Config::Q1_DUTY_MIN, speed * dt)`. Este dato representa
      // [Guía] consigna de duty de Q1 (0 a 1).
      q1_ = approach(q1_, Config::Q1_DUTY_MIN, speed * dt);
    }
    // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
    reason_ = hotplugPvReason_;
    // [Guía] Entrega `true` al llamador.
    return true;
  }
  // V59 atiende referencia PV inmediatamente, sin esperar asentamiento Q2.
  // La rama V47 queda disponible para comparar con el control sobre duty:
  // referencia P&O nueva al entrar en M3 o cambiar el ACK de carga
  // auxiliar. La primera rampa Q2 debe permanecer dentro1pp durante300ms.
  // No repetir esta espera ante CADA respuesta PI a una perturbacion de Q1:
  // esa compensacion es parte normal de la planta que P&O debe observar.
  // [Guía] Coordina P&O y PI de tensión de PV en M3. Usa la tensión filtrada para el lazo interno y las muestras
  // [Guía] de potencia de la búsqueda. Recibe `in`, `dt`.
  void runM3Mppt(const Input& in, float dt) {
    // [Guía] Comprueba `settings_.pvVoltageMpptEnabled`. Si se cumple, ejecuta el bloque asociado.
    if (settings_.pvVoltageMpptEnabled) {
      // [Guía] Actualiza `m3MpptSettling_` con `false`.
      m3MpptSettling_ = false;
      // [Guía] Comprueba `!pvVoltageControl_`. Si se cumple, ejecuta el bloque asociado. Aquí intervienen
      // [Guía] selección del control de tensión PV.
      if (!pvVoltageControl_) {
        // [Guía] Actualiza `pvVoltageControl_` con `true`. Este dato representa selección del control de tensión
        // [Guía] PV.
        pvVoltageControl_ = true;
        // No heredar un pico rapido22..23V como consigna inalcanzable con Q1
        // al5%. Usar el supervisor y el punto inicial18.4V; P&O lo cambiara.
        // [Guía] Prepara P&O con PI interno de tensión PV para empezar desde las medidas o consignas indicadas.
        // [Guía] Aquí intervienen P&O con PI interno de tensión PV y consigna de duty de Q1 (0 a 1).
        pvVoltageMppt_.begin(q1_, fminf(pvSupervisor_, Config::HOTPLUG_PV_TARGET_V), in.nowUs);
        // [Guía] Actualiza `m3VoltageLoadStateKnown_` con `false; m3VoltageLoadOhm_ = 0`.
        m3VoltageLoadStateKnown_ = false; m3VoltageLoadOhm_ = 0;
      }
      // Un ACK de R pendiente no representa OFF. Solo comparar estado LOAD
      // confirmado y una resistencia nueva aceptada. Nunca esperar a Q2.
      // [Guía] Actualiza `changed` con `false`.
      bool changed = false;
      // [Guía] Comprueba `in.auxiliaryLoadStateValid`. Si se cumple, ejecuta el bloque asociado.
      if (in.auxiliaryLoadStateValid) {
        // [Guía] Actualiza `changed` con `m3VoltageLoadStateKnown_ && m3VoltageLoadOn_ != in.auxiliaryLoadOn`.
        changed = m3VoltageLoadStateKnown_ && m3VoltageLoadOn_ != in.auxiliaryLoadOn;
        // [Guía] Actualiza `m3VoltageLoadStateKnown_` con `true; m3VoltageLoadOn_ = in.auxiliaryLoadOn`.
        m3VoltageLoadStateKnown_ = true; m3VoltageLoadOn_ = in.auxiliaryLoadOn;
      }
      // [Guía] Comprueba `in.auxiliaryLoadOhm > 0`. Si se cumple, ejecuta el bloque asociado.
      if (in.auxiliaryLoadOhm > 0) {
        // [Guía] Activa los bits indicados en `changed` mediante `m3VoltageLoadOhm_ > 0 &&
        // [Guía] fabsf(in.auxiliaryLoadOhm - m3VoltageLoadOhm_) > 0.01f`.
        changed |= m3VoltageLoadOhm_ > 0 && fabsf(in.auxiliaryLoadOhm - m3VoltageLoadOhm_) > 0.01f;
        // [Guía] Actualiza `m3VoltageLoadOhm_` con `in.auxiliaryLoadOhm`.
        m3VoltageLoadOhm_ = in.auxiliaryLoadOhm;
      }
      // [Guía] Comprueba `changed`. Si se cumple, ejecuta `pvVoltageMppt_.changedLoad(in.nowUs)`.
      if (changed) pvVoltageMppt_.changedLoad(in.nowUs);
      // [Guía] Actualiza `q1_` con `pvVoltageMppt_.track(pvSupervisor_, in.pvRaw, in.pvI, dt, in.nowUs)`. Este
      // [Guía] dato representa consigna de duty de Q1 (0 a 1).
      q1_ = pvVoltageMppt_.track(pvSupervisor_, in.pvRaw, in.pvI, dt, in.nowUs);
      // [Guía] Actualiza `mpptTarget_` con `q1_; mpptInitialized_ = mpptRunning_ = true`.
      mpptTarget_ = q1_; mpptInitialized_ = mpptRunning_ = true;
      // [Guía] Finaliza aquí la función y devuelve el control al llamador.
      return;
    }
    // [Guía] Comprueba `!settings_.m3MpptContextRebaseEnabled`. Si se cumple, ejecuta el bloque asociado.
    if (!settings_.m3MpptContextRebaseEnabled) { runMppt(in, dt); return; }
    // [Guía] Comprueba `m3MpptLoadConfirmed_ != in.recoveryLoadConfirmed`. Si se cumple, ejecuta el bloque
    // [Guía] asociado.
    if (m3MpptLoadConfirmed_ != in.recoveryLoadConfirmed
        // [Guía] Añade una alternativa suficiente dentro de la condición compuesta.
        || fabsf(m3MpptLoadOhm_ - in.auxiliaryLoadOhm) > 0.01f) {
      // [Guía] Actualiza `m3MpptLoadConfirmed_` con `in.recoveryLoadConfirmed`.
      m3MpptLoadConfirmed_ = in.recoveryLoadConfirmed;
      // [Guía] Actualiza `m3MpptLoadOhm_` con `in.auxiliaryLoadOhm`.
      m3MpptLoadOhm_ = in.auxiliaryLoadOhm;
      // [Guía] Actualiza `m3MpptSettling_` con `true`.
      m3MpptSettling_ = true;
      // [Guía] Actualiza `m3MpptQ2Anchor_` con `q2_; m3MpptContextSinceUs_ = in.nowUs`.
      m3MpptQ2Anchor_ = q2_; m3MpptContextSinceUs_ = in.nowUs;
    }
    // [Guía] Comprueba `m3MpptSettling_`. Si se cumple, ejecuta el bloque asociado.
    if (m3MpptSettling_) {
      // [Guía] Comprueba `fabsf(q2_ - m3MpptQ2Anchor_) >= Config::M3_MPPT_Q2_SETTLE_BAND`. Si se cumple, ejecuta
      // [Guía] el bloque asociado. Aquí intervienen consigna de duty de Q2 (0 a 1).
      if (fabsf(q2_ - m3MpptQ2Anchor_) >= Config::M3_MPPT_Q2_SETTLE_BAND) {
        // [Guía] Actualiza `m3MpptQ2Anchor_` con `q2_; m3MpptContextSinceUs_ = in.nowUs`.
        m3MpptQ2Anchor_ = q2_; m3MpptContextSinceUs_ = in.nowUs;
      }
      // Cancelar una propuesta pendiente sin mover el duty aplicado. No usar
      // al volver una ventana/base tomada con Q2 en otra posicion.
      // [Guía] Suspende temporalmente el avance de buscador P&O en la coordenada de duty. Aquí intervienen
      // [Guía] buscador P&O en la coordenada de duty y reloj de la muestra en µs.
      mppt_.pause(in.nowUs); mpptTarget_ = q1_;
      // [Guía] Actualiza `mpptInitialized_` con `mpptRunning_ = false`.
      mpptInitialized_ = mpptRunning_ = false;
      // [Guía] Comprueba `uint32_t(in.nowUs - m3MpptContextSinceUs_) < Config::M3_MPPT_Q2_SETTLE_US`. Si se
      // [Guía] cumple, ejecuta el bloque asociado. Aquí intervienen reloj de la muestra en µs.
      if (uint32_t(in.nowUs - m3MpptContextSinceUs_) < Config::M3_MPPT_Q2_SETTLE_US) {
        // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
        reason_ = "M3_MPPT_WAIT_Q2_SETTLE";
        // [Guía] Finaliza aquí la función y devuelve el control al llamador.
        return;
      }
      // [Guía] Actualiza `m3MpptSettling_` con `false`.
      m3MpptSettling_ = false;
      // [Guía] Prepara buscador P&O en la coordenada de duty para empezar desde las medidas o consignas
      // [Guía] indicadas. Aquí intervienen buscador P&O en la coordenada de duty y consigna de duty de Q1 (0 a
      // [Guía] 1).
      mppt_.begin(q1_, in.nowUs); mpptTarget_ = q1_;
      // [Guía] Actualiza `mpptInitialized_` con `true`.
      mpptInitialized_ = true;
      // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
      reason_ = "M3_MPPT_REBASE_Q2";
    }
    // [Guía] Ejecuta la modalidad de seguimiento que corresponde al estado y mantiene su diagnóstico. Aquí
    // [Guía] intervienen intervalo efectivo del control en s.
    runMppt(in, dt);
  }
  // Adaptador entre P&O y actuador: primero aplicar su propuesta por slew;
  // al llegar reiniciar asentamiento. Solo entonces acumular potencia.
  // in.pvRaw por in.pvI filtrada es la senal historica usada por EMS V39.
  // [Guía] Ejecuta la modalidad de seguimiento que corresponde al estado y mantiene su diagnóstico. Recibe `in`,
  // [Guía] `dt`.
  void runMppt(const Input& in, float dt) {
    // [Guía] Comprueba `pvVoltageControl_`. Si se cumple, ejecuta el bloque asociado. Aquí intervienen selección
    // [Guía] del control de tensión PV.
    if (pvVoltageControl_) { holdPreparedPv(in, dt); return; }
    // [Guía] Comprueba `q1_ < Config::Q1_DUTY_MIN`. Si se cumple, ejecuta el bloque asociado. Aquí intervienen
    // [Guía] consigna de duty de Q1 (0 a 1).
    if (q1_ < Config::Q1_DUTY_MIN) {
      // [Guía] Comprueba `mpptInitialized_`. Si se cumple, ejecuta `mppt_.pause(in.nowUs)`.
      if (mpptInitialized_) mppt_.pause(in.nowUs);
      // [Guía] Actualiza `mpptRunning_` con `false`. Este dato representa indicación de MPPT activo.
      mpptRunning_ = false;
      // [Guía] Finaliza aquí la función y devuelve el control al llamador.
      return;
    }
    // [Guía] Comprueba `!mpptInitialized_`. Si se cumple, ejecuta el bloque asociado.
    if (!mpptInitialized_) {
      // [Guía] Prepara buscador P&O en la coordenada de duty para empezar desde las medidas o consignas
      // [Guía] indicadas. Aquí intervienen buscador P&O en la coordenada de duty y consigna de duty de Q1 (0 a
      // [Guía] 1).
      mppt_.begin(q1_, in.nowUs);
      // [Guía] Actualiza `mpptTarget_` con `q1_`.
      mpptTarget_ = q1_;
      // [Guía] Actualiza `mpptInitialized_` con `true`.
      mpptInitialized_ = true;
    }
    // [Guía] Actualiza `mpptRunning_` con `true`. Este dato representa indicación de MPPT activo.
    mpptRunning_ = true;
    // [Guía] Comprueba `q1_ != mpptTarget_`. Si se cumple, ejecuta el bloque asociado. Aquí intervienen consigna
    // [Guía] de duty de Q1 (0 a 1).
    if (q1_ != mpptTarget_) {
      // [Guía] Suspende temporalmente el avance de buscador P&O en la coordenada de duty. Aquí intervienen
      // [Guía] buscador P&O en la coordenada de duty y reloj de la muestra en µs.
      mppt_.pause(in.nowUs);
      // [Guía] Actualiza `q1_` con `approach(q1_, mpptTarget_, Config::Q1_TEST_SLEW_PER_SECOND * dt)`. Este dato
      // [Guía] representa consigna de duty de Q1 (0 a 1).
      q1_ = approach(q1_, mpptTarget_, Config::Q1_TEST_SLEW_PER_SECOND * dt);
      // [Guía] Comprueba `q1_ == mpptTarget_`. Si se cumple, ejecuta `mppt_.restartSettling(in.nowUs)`. Aquí
      // [Guía] intervienen consigna de duty de Q1 (0 a 1).
      if (q1_ == mpptTarget_) mppt_.restartSettling(in.nowUs);
      // [Guía] Finaliza aquí la función y devuelve el control al llamador.
      return;
    }
    // [Guía] Comprueba `mppt_.sample(in.pvRaw, in.pvI, in.nowUs)`. Si se cumple, ejecuta el bloque asociado. Aquí
    // [Guía] intervienen buscador P&O en la coordenada de duty y reloj de la muestra en µs.
    if (mppt_.sample(in.pvRaw, in.pvI, in.nowUs)) {
      // Conservar la memoria de P&O: no reiniciar el algoritmo en cada propuesta.
      // [Guía] Actualiza `mpptTarget_` con `mppt_.duty()`.
      mpptTarget_ = mppt_.duty();
    }
  }
  // ESTADO PERSISTENTE ENTRE BARRIDOS. Los nombres terminados '_' son miembros.
  // settings_ es copia local; los PI y MPPT conservan integral/base entre calls.
  // Los bloques de variables siguientes se corresponden con submaquinas:
  // ayuda PV, reintentos, evidencia de fuentes, adquisicion M1 y temporizacion.
  // Ibus incluye la carga auxiliar del bus cuando esta conectada. Su consumo
  // nominal V/R permite iniciar retirada, pero se confirma Ibus real tras OFF.
  // [Guía] Devuelve la estimación de demanda externa que utiliza la decisión de entrada o permanencia en M2.
  // [Guía] Recibe `in`.
  float m2BusDemand(const Input& in) const {
    // [Guía] Entrega `m2ExternalDemand(busLoadWindow_.mean, in)` al llamador.
    return m2ExternalDemand(busLoadWindow_.mean, in);
  }
  // [Guía] Descuenta de la corriente medida la parte atribuible a la carga auxiliar cuando dispone de evidencia
  // [Guía] confirmada. Recibe `measured`, `in`.
  float m2ExternalDemand(float measured, const Input& in) const {
    // [Guía] Actualiza `auxiliary` con `in.auxiliaryLoadStateValid && in.auxiliaryLoadOn`.
    const float auxiliary = in.auxiliaryLoadStateValid && in.auxiliaryLoadOn
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
      && isfinite(in.auxiliaryLoadOhm) && in.auxiliaryLoadOhm > 0
      // [Guía] Define una de las dos salidas de la expresión condicional. Aquí intervienen tensión de bus
      // [Guía] filtrada para supervisión.
      ? fmaxf(0, busSupervisor_) / in.auxiliaryLoadOhm : 0;
    // [Guía] Entrega `measured - auxiliary` al llamador.
    return measured - auxiliary;
  }
  // Entrega rapida. Desde M3 conservar duty Q2 como punto inicial del PI;
  // desde M4 retirar ambas ramas salientes antes de iniciar el interbloqueo.
  // [Guía] Inicia el cambio a carga exclusiva. Retira las ramas de bus antes de establecer el PI de corriente de
  // [Guía] Q2. Recibe `in`, `dt`.
  void enterM2Preparation(const Input& in, float dt) {
    // [Guía] Actualiza `pvVoltageControl_` con `false; cancelPvAssist()`. Este dato representa selección del
    // [Guía] control de tensión PV.
    pvVoltageControl_ = false; cancelPvAssist();
    // [Guía] Actualiza `hotplugPvSeeking_` con `hotplugPvQualified_ = false`.
    hotplugPvSeeking_ = hotplugPvQualified_ = false;
    // [Guía] Actualiza `mpptRunning_` con `mpptInitialized_ = false`. Este dato representa indicación de MPPT
    // [Guía] activo.
    mpptRunning_ = mpptInitialized_ = false;
    // [Guía] Actualiza `q3WasOn` con `q3_ > 0`.
    const bool q3WasOn = q3_ > 0;
    // [Guía] Pide apagar la rama indicada con duty cero. El sketch aplica físicamente la consigna.
    q1_ = q3_ = 0;
    // [Guía] Actualiza `m2TransitionSince_` con `in.nowUs; clearHolds()`.
    m2TransitionSince_ = in.nowUs; clearHolds();
    // [Guía] Actualiza `wasM4` con `state_ == M4`.
    const bool wasM4 = state_ == M4;
    // [Guía] Comprueba `q3WasOn`. Si se cumple, ejecuta el bloque asociado.
    if (q3WasOn) { q2_ = 0; m2Current_.stop(); state_ = RETIRE3_M2; }
    // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
    else startM2Current(in, dt, true);
    // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
    reason_ = lastSourceEvent_ = wasM4 ? "M4_TO_M2_BUS_IDLE" : "M3_TO_M2_BUS_IDLE";
  }
  // [Guía] Precarga el lazo de Q2 y establece el estado M2 de carga con referencia de corriente. Recibe `in`,
  // [Guía] `dt`, `chargeAllowed`.
  void startM2Current(const Input& in, float dt, bool chargeAllowed) {
    // [Guía] Pide apagar la rama indicada con duty cero. El sketch aplica físicamente la consigna.
    q1_ = q3_ = 0; state_ = M2;
    // [Guía] Comprueba `!chargeAllowed`. Si se cumple, ejecuta el bloque asociado. Aquí intervienen permiso de
    // [Guía] carga de batería.
    if (!chargeAllowed) {
      // [Guía] Pide apagar la rama indicada con duty cero. El sketch aplica físicamente la consigna.
      q2_ = 0; m2Current_.stop(); reason_ = "M2_CHARGE_PERMISSION_HOLD"; return;
    }
    // Si Q2 venia apagado, iniciar en su minimo configurado: no gastar250ms
    // recorriendo0..5%. La referencia SI conserva su rampa0.3A/s y sus limites.
    // [Guía] Prepara controlador de corriente de Q2 en M2 para empezar desde las medidas o consignas indicadas.
    // [Guía] Aquí intervienen controlador de corriente de Q2 en M2 y consigna de duty de Q2 (0 a 1).
    m2Current_.begin(q2_ > 0 ? q2_ : Config::Q2_DUTY_MIN, in.batteryI);
    // [Guía] Actualiza `q2_` con `m2Current_.update(in.batteryI, in.batteryV, dt)`. Este dato representa consigna
    // [Guía] de duty de Q2 (0 a 1).
    q2_ = m2Current_.update(in.batteryI, in.batteryV, dt);
    // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
    reason_ = m2Current_.reason();
  }
  // [Guía] Inicia el retorno a regulación de bus cuando reaparece demanda y guarda la causa del cambio. Recibe
  // [Guía] `in`, `reason`.
  void returnFromM2(const Input& in, const char* reason) {
    // [Guía] Pide apagar la rama indicada con duty cero. El sketch aplica físicamente la consigna.
    q1_ = q3_ = 0; mpptRunning_ = mpptInitialized_ = false;
    // [Guía] Actualiza `m2TransitionSince_` con `in.nowUs; clearHolds()`.
    m2TransitionSince_ = in.nowUs; clearHolds();
    // [Guía] Cambia el estado operativo que se evaluará a partir de la siguiente actualización. Aquí intervienen
    // [Guía] estado operativo interno.
    state_ = RETURN_M2; reason_ = lastSourceEvent_ = reason;
  }
  // [Guía] Declara `settings_` para conservar settings en este ámbito.
  Settings settings_;
  // [Guía] Declara `m2Current_` para conservar controlador de corriente de Q2 en M2 en este ámbito.
  M2CurrentController m2Current_;
  // [Guía] Declara `busLoadWindow_` para conservar bus DC load ventana en este ámbito.
  CurrentWindow busLoadWindow_;
  // [Guía] Declara `busDemandHistory_` para conservar bus DC demand history en este ámbito.
  BusDemandHistory busDemandHistory_;
  // [Guía] Declara `m2Idle_` para conservar m2 sin demanda en este ámbito.
  Hold m2Idle_, m2Wake_;
  // Contar confirmaciones parciales descartadas; una transferencia completada
  // no incrementa el contador. No es un numero de fallos ni de ciclos del PI.
  // [Guía] Reinicia la confirmación de ausencia de carga externa y conserva la causa de reinicio para
  // [Guía] diagnóstico. Recibe `reason`.
  void clearM2Idle(const char* reason) {
    // [Guía] Comprueba `m2Idle_.running && uint32_t(lastInput_.nowUs - m2Idle_.start) < Config::M2_BUS_IDLE_US`.
    // [Guía] Si se cumple, ejecuta el bloque asociado.
    if (m2Idle_.running && uint32_t(lastInput_.nowUs - m2Idle_.start) < Config::M2_BUS_IDLE_US
        // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
        && m2IdleResets_ != 0xffffffffu) ++m2IdleResets_;
    // [Guía] Borra la confirmación o acumulación en `m2Idle_` para que no se arrastre al siguiente contexto.
    m2Idle_.clear(); m2IdleLatched_ = false; m2Block_ = reason;
  }
  // [Guía] Actualiza `m2IdleLatched_` con `false`.
  bool m2IdleLatched_ = false;
  // [Guía] Actualiza `m2IdleResets_` con `0`.
  uint32_t m2IdleResets_ = 0;
  // [Guía] Actualiza `m2Block_` con `"INACTIVE"`.
  const char* m2Block_ = "INACTIVE";
  // [Guía] Actualiza `m2Cooldown_` con `false`.
  bool m2Cooldown_ = false;
  // [Guía] Actualiza `m2TransitionSince_` con `0, m2CooldownSince_ = 0`.
  uint32_t m2TransitionSince_ = 0, m2CooldownSince_ = 0;
  // [Guía] Declara `pvVoltageMppt_` para conservar P&O con PI interno de tensión PV en este ámbito.
  PvVoltageMpptController pvVoltageMppt_;
  // [Guía] Actualiza `pvVoltageControl_` con `false`. Este dato representa selección del control de tensión PV.
  bool pvVoltageControl_ = false;
  // [Guía] Actualiza `hotplugQ3Support_` con `false`.
  bool hotplugQ3Support_ = false;
  // [Guía] Actualiza `m3VoltageLoadStateKnown_` con `false, m3VoltageLoadOn_ = false`.
  bool m3VoltageLoadStateKnown_ = false, m3VoltageLoadOn_ = false;
  // [Guía] Actualiza `m3VoltageLoadOhm_` con `0`.
  float m3VoltageLoadOhm_ = 0;
  // [Guía] Actualiza `m3MpptSettling_` con `false`.
  bool m3MpptSettling_ = false;
  // [Guía] Actualiza `m3MpptLoadConfirmed_` con `false`.
  bool m3MpptLoadConfirmed_ = false;
  // [Guía] Actualiza `m3MpptLoadOhm_` con `0`.
  float m3MpptLoadOhm_ = 0;
  // [Guía] Actualiza `m3MpptQ2Anchor_` con `0`.
  float m3MpptQ2Anchor_ = 0;
  // [Guía] Actualiza `m3MpptContextSinceUs_` con `0`.
  uint32_t m3MpptContextSinceUs_ = 0;
  // [Guía] Actualiza `hotplugPvSeeking_` con `false`.
  bool hotplugPvSeeking_ = false;
  // [Guía] Actualiza `hotplugPvQualified_` con `false`.
  bool hotplugPvQualified_ = false; // Resultado de confirmacion ADC, no del calendario PI.
  // [Guía] Actualiza `hotplugPvSinceUs_` con `0`.
  uint32_t hotplugPvSinceUs_ = 0;
  // [Guía] Declara `hotplugPvReady_` para conservar hotplug panel fotovoltaico listo en este ámbito.
  Hold hotplugPvReady_;
  // [Guía] Actualiza `hotplugPvReason_` con `"PV_18V4_SEEK_Q1"`.
  const char* hotplugPvReason_ = "PV_18V4_SEEK_Q1";
  // [Guía] Actualiza `pvAssistStage_` con `PV_ASSIST_OFF`.
  PvAssistStage pvAssistStage_ = PV_ASSIST_OFF;
  // [Guía] Actualiza `pvAssistPending_` con `false`.
  bool pvAssistPending_ = false;
  // [Guía] Actualiza `pvAssistHotplugPending_` con `false`.
  bool pvAssistHotplugPending_ = false;
  // [Guía] Actualiza `pvAssistHotplugSinceUs_` con `0`.
  uint32_t pvAssistHotplugSinceUs_ = 0;
  // [Guía] Actualiza `pvAssistAttempted_` con `false`.
  bool pvAssistAttempted_ = false;
  // [Guía] Actualiza `chargeAttempts_` con `0, pvAssistStarts_ = 0`.
  uint8_t chargeAttempts_ = 0, pvAssistStarts_ = 0;
  // [Guía] Declara `prepHighBus_` para conservar prep high bus DC en este ámbito.
  Hold prepHighBus_, chargeHealthy_, chargeMarginLost_;
  // [Guía] Declara `chargeRetry_` para conservar carga retry en este ámbito.
  Hold chargeRetry_;
  // [Guía] Actualiza `chargeRetryReferenceReady_` con `false`.
  bool chargeRetryReferenceReady_ = false;
  // [Guía] Actualiza `chargeRetryPv_` con `0`.
  float chargeRetryPv_ = 0;
  // [Guía] Actualiza `chargeRetrySinceUs_` con `0`.
  uint32_t chargeRetrySinceUs_ = 0;
  // [Guía] Actualiza `pvAssistResult_` con `nullptr`.
  const char* pvAssistResult_ = nullptr;
  // [Guía] Actualiza `pvAssistStartUs_` con `0, pvAssistCheckpointUs_ = 0`.
  uint32_t pvAssistStartUs_ = 0, pvAssistCheckpointUs_ = 0;
  // [Guía] Actualiza `pvAssistFloor_` con `0, pvAssistStartPv_ = 0, pvAssistStartBus_ = 0`.
  float pvAssistFloor_ = 0, pvAssistStartPv_ = 0, pvAssistStartBus_ = 0;
  // [Guía] Actualiza `pvAssistCheckpointPv_` con `0, pvAssistCheckpointBus_ = 0`.
  float pvAssistCheckpointPv_ = 0, pvAssistCheckpointBus_ = 0;
  // [Guía] Declara `pvAssistReady_` para conservar panel fotovoltaico assist listo en este ámbito.
  Hold pvAssistReady_, pvAssistBusWorse_, pvAssistReverse_, pvAssistDeficit_;
  // [Guía] Declara `lastInput_` para conservar last input en este ámbito.
  Input lastInput_;
  // [Guía] Declara `faultSnapshot_` para conservar fault snapshot en este ámbito.
  FaultSnapshot faultSnapshot_;
  // [Guía] Declara `connectionDiagnostic_` para conservar connection diagnostic en este ámbito.
  ConnectionDiagnostic connectionDiagnostic_;
  // Separados de clearHolds(): wait() no debe borrar la confirmacion del evento.
  // [Guía] Declara `connectionOver_` para conservar connection over en este ámbito.
  Hold connectionOver_, connectionRecovery_;
  // [Guía] Declara `q3CurrentOver_` para conservar q3 corriente over en este ámbito.
  Hold q3CurrentOver_;
  // [Guía] Declara `q3ChargeOver_` para conservar q3 carga over en este ámbito.
  Hold q3ChargeOver_;
  // [Guía] Declara `q3VoltageOver_` para conservar q3 tensión over en este ámbito.
  Hold q3VoltageOver_;
  // [Guía] Declara `q3BusOver_` para conservar q3 bus DC over en este ámbito.
  Hold q3BusOver_;
  // [Guía] Declara `pvBusOver_` para conservar panel fotovoltaico bus DC over en este ámbito.
  Hold pvBusOver_;
  // [Guía] Declara `bootPv_` para conservar boot panel fotovoltaico en este ámbito.
  Hold bootPv_;
  // [Guía] Declara `m1BatteryQuiet_` para conservar m1 batería quiet en este ámbito.
  Hold m1BatteryQuiet_, m1BatteryConnect_;
  // [Guía] Actualiza `m1BatteryArmed_` con `false, m1BatteryCandidate_ = false`.
  bool m1BatteryArmed_ = false, m1BatteryCandidate_ = false;
  // [Guía] Actualiza `m1BatteryQualified_` con `false`.
  bool m1BatteryQualified_ = false;
  // [Guía] Actualiza `m1BatteryPolarity_` con `0`.
  int m1BatteryPolarity_ = 0;
  // [Guía] Actualiza `m1BatteryConfirmResets_` con `0`.
  uint32_t m1BatteryConfirmResets_ = 0;
  // [Guía] Actualiza `m1BatteryConfirmLastReset_` con `"NONE"`.
  const char* m1BatteryConfirmLastReset_ = "NONE";
  // [Guía] Actualiza `m1BatteryBaseline_` con `0`.
  float m1BatteryBaseline_ = 0;
  // [Guía] Declara `batteryLinkHold_` para conservar batería link confirmación sostenida en este ámbito.
  Hold batteryLinkHold_, batteryQuiet_, chargeDischargeHold_, chargeAtMinimum_, deficitHold_;
  // [Guía] Declara `batteryNoResponse_` para conservar batería no response en este ámbito.
  Hold batteryNoResponse_, batteryIdle_, pvNoFlow_, m1BranchReady_, m1Reacquire_;
  // [Guía] Declara `m1LoadRecovery_` para conservar m1 load recovery en este ámbito.
  Hold m1LoadRecovery_;
  // [Guía] Declara `m1AuxReliefReady_` para conservar m1 carga auxiliar relief listo en este ámbito.
  Hold m1AuxReliefReady_;
  // [Guía] Actualiza `m1LastAuxOhm_` con `0`.
  float m1LastAuxOhm_ = 0;
  // [Guía] Actualiza `m1AuxReliefPending_` con `false`.
  bool m1AuxReliefPending_ = false;
  // [Guía] Actualiza `m1AuxReliefRetries_` con `0`.
  unsigned m1AuxReliefRetries_ = 0;
  // [Guía] Actualiza `m1LoadRetryUsed_` con `false`.
  bool m1LoadRetryUsed_ = false;
  // [Guía] Declara `batteryWindow_` para conservar batería ventana en este ámbito.
  CurrentWindow batteryWindow_;
  // [Guía] Actualiza `batteryLinkState_` con `"WAIT_CAL"`.
  const char* batteryLinkState_ = "WAIT_CAL";
  // [Guía] Actualiza `chargeFlowSeen_` con `false`.
  bool chargeFlowSeen_ = false;
  // [Guía] Declara `chargeFlowConfirm_` para conservar carga flow confirm en este ámbito.
  Hold chargeFlowConfirm_;
  // [Guía] Actualiza `batteryResponseState_` con `"UNQUALIFIED"`.
  const char* batteryResponseState_ = "UNQUALIFIED";
  // [Guía] Actualiza `lastSourceEvent_` con `"BOOT"`.
  const char* lastSourceEvent_ = "BOOT";
  // [Guía] Actualiza `retire2Reason_` con `"RETIRE_Q2_RECOVER_SOURCE"`.
  const char* retire2Reason_ = "RETIRE_Q2_RECOVER_SOURCE";
  // [Guía] Actualiza `pvFlowSeen_` con `false, pvUnavailable_ = false`.
  bool pvFlowSeen_ = false, pvUnavailable_ = false;
  // [Guía] Actualiza `pvStartUs_` con `0, m1AcquireStartUs_ = 0`.
  uint32_t pvStartUs_ = 0, m1AcquireStartUs_ = 0;
  // [Guía] Actualiza `m1Stage_` con `M1_PI`. Este dato representa etapa interna de arranque y regulación M1.
  M1Stage m1Stage_ = M1_PI;
  // [Guía] Declara `m1Recovery_` para conservar m1 recovery en este ámbito.
  Hold m1Recovery_;
  // [Guía] Actualiza `m1Retries_` con `0`.
  uint8_t m1Retries_ = 0;
  // [Guía] Actualiza `m1AcquireSlew_` con `Config::M1_ACQUIRE_SLEW_PER_SECOND`.
  float m1AcquireSlew_ = Config::M1_ACQUIRE_SLEW_PER_SECOND;
  // [Guía] Actualiza `m1InputGuardArmed_` con `false`.
  bool m1InputGuardArmed_ = false; // Armado tras bus util; no bloquear precarga inicial.
  // [Guía] Declara `m1InputLow_` para conservar m1 input low en este ámbito.
  Hold m1InputLow_, m1Healthy_;
  // [Guía] Actualiza `m1BudgetRenewable_` con `false`.
  bool m1BudgetRenewable_ = false; // Solo tras PI continuo estable, nunca por alternar estados.
  // [Guía] Actualiza `m1Limit_` con `"NONE"`.
  const char* m1Limit_ = "NONE";
  // [Guía] Actualiza `batteryCurrentSupervisor_` con `0`. Este dato representa corriente de batería filtrada para
  // [Guía] supervisión.
  float batteryCurrentSupervisor_ = 0;
  // [Guía] Actualiza `batteryCurrentReady_` con `false, batteryLinkConfirmed_ = false`.
  bool batteryCurrentReady_ = false, batteryLinkConfirmed_ = false;
  // [Guía] Actualiza `busSupervisor_` con `0`. Este dato representa tensión de bus filtrada para supervisión.
  float busSupervisor_ = 0;
  // [Guía] Actualiza `busSupervisorReady_` con `false`.
  bool busSupervisorReady_ = false;
  // [Guía] Actualiza `pvSupervisor_` con `0`. Este dato representa tensión de PV filtrada para supervisión.
  float pvSupervisor_ = 0;
  // [Guía] Actualiza `pvSupervisorReady_` con `false`.
  bool pvSupervisorReady_ = false;
  // [Guía] Declara `busRecoverOn_` para conservar bus DC recuperación on en este ámbito.
  Hold busRecoverOn_, busRecoverOff_;
  // [Guía] Actualiza `busRecovery_` con `false`. Este dato representa indicación de que se requiere recuperar el
  // [Guía] bus.
  bool busRecovery_ = false;
  // [Guía] Actualiza `q3CurrentPending_` con `false`.
  bool q3CurrentPending_ = false;
  // [Guía] Actualiza `q3CurrentSamples_` con `0`.
  uint32_t q3CurrentSamples_ = 0;
  // [Guía] Actualiza `q3CurrentMaximum_` con `0`.
  float q3CurrentMaximum_ = 0;
  // [Guía] Declara `batteryVoltageHold_` para conservar batería tensión confirmación sostenida en este ámbito.
  Hold batteryVoltageHold_;
  // [Guía] Actualiza `connectionPending_` con `false`.
  bool connectionPending_ = false;
  // [Guía] Actualiza `batteryVoltageInitialized_` con `false, batteryVoltageReady_ = false`.
  bool batteryVoltageInitialized_ = false, batteryVoltageReady_ = false;
  // [Guía] Actualiza `batteryVoltage_` con `0`. Este dato representa tensión de batería calificada.
  float batteryVoltage_ = 0;
  // [Guía] Actualiza `haveInput_` con `false`.
  bool haveInput_ = false;
  // [Guía] Declara `pi3_` para conservar PI de bus aplicado a Q3 en este ámbito.
  PIController pi3_, pi2_, pi1_;
  // [Guía] Declara `mppt_` para conservar buscador P&O en la coordenada de duty en este ámbito.
  MPPTController mppt_;
  // [Guía] Cambia el estado operativo que se evaluará a partir de la siguiente actualización. Aquí intervienen
  // [Guía] estado operativo interno.
  State state_ = STOPPED;
  // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
  const char* reason_ = "NOT_STARTED";
  // [Guía] Actualiza `q1_` con `0, q2_ = 0, q3_ = 0, reference_ = 0`. Este dato representa consigna de duty de Q1
  // [Guía] (0 a 1).
  float q1_ = 0, q2_ = 0, q3_ = 0, reference_ = 0;
  // [Guía] Actualiza `mpptTarget_` con `0`.
  float mpptTarget_ = 0;
  // [Guía] Actualiza `lastUs_` con `0, lastControlUs_ = 0, nextControlUs_ = 0, rampElapsed_ = 0, transferStart_ =
  // [Guía] 0`.
  uint32_t lastUs_ = 0, lastControlUs_ = 0, nextControlUs_ = 0, rampElapsed_ = 0, transferStart_ = 0;
  // [Guía] Actualiza `updates_` con `0, missedUpdates_ = 0`. Este dato representa contador de actualizaciones.
  uint32_t updates_ = 0, missedUpdates_ = 0;
  // [Guía] Actualiza `haveTime_` con `false, mpptRunning_ = false, canReset_ = false`.
  bool haveTime_ = false, mpptRunning_ = false, canReset_ = false;
  // [Guía] Actualiza `mpptInitialized_` con `false`.
  bool mpptInitialized_ = false;
  // [Guía] Declara `pvPresent_` para conservar panel fotovoltaico present en este ámbito.
  Hold pvPresent_, pvAbsent_, zeroCurrent_, resetSafe_;
};
