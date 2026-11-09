// [Guía] Implementa la rampa de resistencia del perfil independiente y conserva la última carga estable. La
// [Guía] política EMS del emulador se encuentra en BusOvervoltage.h.
// [Guía] Evita procesar este encabezado dos veces en una misma unidad de compilación.
#pragma once
/* AUTO_R: MAQUINA DE ESTADOS PARA UNA CARGA AUXILIAR
 * START espera4000 ohm/OFF; READY_OFF exige bus estable antes de solicitar ON.
 * WAIT_ON/WAIT_R esperan ACK; RUN ensaya un punto; TARGET_HOLD conserva un
 * objetivo alcanzado; WAIT_RECOVERY/RECOVERY vuelven al ultimo R estable.
 * now y temporizadores son milisegundos. clearTimers borra calificaciones,
 * no la resistencia fisica. La autorizacion hardwareReady nace de Config y
 * permanece bloqueada en el perfil experimental actual salvo pruebas host.
 */
// [Guía] Incorpora las declaraciones de "Config.h" antes de usar sus tipos y funciones.
#include "Config.h"
// [Guía] Incorpora las declaraciones de "EmulatorController.h" antes de usar sus tipos y funciones.
#include "EmulatorController.h"
// [Guía] Incorpora las declaraciones de "BusStabilityWindow.h" antes de usar sus tipos y funciones.
#include "BusStabilityWindow.h"

// Busqueda de Vpv en M1: solo decide R/LOAD. Nunca escribe PWM ni activa M3.
// Se llama con cada muestra; no usa delay ni espera el puerto serie.
// [Guía] Reúne bajo `EmulatorRamp` las declaraciones que comparten este tipo o ámbito.
class EmulatorRamp {
 // [Guía] Desde aquí se declaran los tipos y operaciones que usa el llamador.
 public:
  // Inyeccion del permiso para pruebas offline. Firmware usa el valor Config.
  // [Guía] Construye EmulatorRamp con la configuración inicial indicada. Recibe `hardwareReady`.
  explicit EmulatorRamp(bool hardwareReady = Config::AUTO_EMULATOR_HARDWARE_READY)
      // [Guía] Define una de las dos salidas de la expresión condicional.
      : hardwareReady_(hardwareReady) {}
  // [Guía] Expone `status_` al módulo que consulta status.
  const char* status() const { return status_; }
  // [Guía] Expone `active_` al módulo que consulta active.
  bool active() const { return active_; }
  // [Guía] Devuelve última resistencia calificada como estable.
  float lastStableOhm() const { return safeR_; }

  // Los comandos manuales cancelan la busqueda sin cambiar el consumo.
  // [Guía] Termina la búsqueda e informa la causa al diagnóstico. Recibe `reason`.
  void cancel(const char* reason = "MANUAL") {
    // [Guía] Actualiza `active_` con `false`.
    active_ = false;
    // [Guía] Actualiza `status_` con `reason`.
    status_ = reason;
    // [Guía] Invoca `clearTimers` con los argumentos de esta expresión.
    clearTimers();
  }

  // Cada busqueda comienza con retirada de consumo y R conservadora. No
  // ejecutar un barrido automatico si el permiso hardware no fue validado.
  // [Guía] Establece el punto inicial y prepara el estado temporal de la búsqueda de resistencia. Recibe `bk`,
  // [Guía] `now`.
  void begin(EmulatorController& bk, uint32_t now) {
    // [Guía] Invoca `clearTimers` con los argumentos de esta expresión.
    clearTimers();
    // [Guía] Actualiza `safeR_` con `Config::EMULATOR_START_OHM`. Este dato representa última resistencia
    // [Guía] calificada como estable.
    safeR_ = Config::EMULATOR_START_OHM;
    // [Guía] Actualiza `stepScale_` con `1.0f`.
    stepScale_ = 1.0f;
    // [Guía] Actualiza `recoveries_` con `0`.
    recoveries_ = 0;
    // [Guía] Invoca `bk.solicitarConsumo` con los argumentos de esta expresión.
    bk.solicitarConsumo(false);
    // [Guía] Invoca `bk.configurarResistencia` con los argumentos de esta expresión. Aquí intervienen última
    // [Guía] resistencia calificada como estable.
    bk.configurarResistencia(safeR_);
    // [Guía] Comprueba `!hardwareReady_`. Si se cumple, ejecuta el bloque asociado.
    if (!hardwareReady_) {
      // [Guía] Termina la búsqueda e informa la causa al diagnóstico.
      cancel("HARDWARE_BLOCKED");
      // [Guía] Finaliza aquí la función y devuelve el control al llamador.
      return;
    }
    // [Guía] Actualiza `active_` con `true`.
    active_ = true;
    // [Guía] Cambia el estado operativo que se evaluará a partir de la siguiente actualización. Aquí intervienen
    // [Guía] estado operativo interno.
    state_ = START;
    // [Guía] Actualiza `entered_` con `now`.
    entered_ = now;
    // [Guía] Actualiza `status_` con `"WAIT_4000_OFF"`.
    status_ = "WAIT_4000_OFF";
  }

  // Exactamente una frontera por paso: no saltar 1000, 200 ni 100 ohm.
  // [Guía] Calcula el siguiente escalón de la rampa dentro del límite configurado. Recibe `r`.
  static float nextResistance(float r) {
    // [Guía] Actualiza `floor` con `Config::EMULATOR_MIN_OHM`.
    const float floor = Config::EMULATOR_MIN_OHM;
    // [Guía] Actualiza `value` con `r > 1000 ? fmaxf(1000, r - 1000)`.
    float value = r > 1000 ? fmaxf(1000, r - 1000)
      // [Guía] Define una de las dos salidas de la expresión condicional.
      : r > 200 ? fmaxf(200, r - 200)
      // [Guía] Define una de las dos salidas de la expresión condicional.
      : r > 100 ? fmaxf(100, r - 20) : r - 2;
    // [Guía] Entrega `fmaxf(floor, value)` al llamador.
    return fmaxf(floor, value);
  }

  // Una llamada avanza como maximo una etapa. ready exige calibracion/control;
  // m1 impide que AUTO_R compita con el reparto de potencia de otros modos.
  // [Guía] Hace avanzar la búsqueda de resistencia del perfil independiente, según estabilidad del bus y
  // [Guía] confirmaciones de la B&K. Recibe `bk`, `now`, `m1`, `ready`, `bus`, `pv`, `ipv`, `ibus`.
  void update(EmulatorController& bk, uint32_t now, bool m1, bool ready,
              // [Guía] Declara `bus` para conservar bus DC en este ámbito.
              float bus, float pv, float ipv, float ibus) {
    // [Guía] Comprueba `!active_`. Si se cumple, termina la función en esta rama.
    if (!active_) return;
    // [Guía] Comprueba `!m1 || !ready`. Si se cumple, ejecuta el bloque asociado.
    if (!m1 || !ready) {
      // [Guía] Termina la búsqueda e informa la causa al diagnóstico.
      cancel("M1_NOT_READY");
      // [Guía] Finaliza aquí la función y devuelve el control al llamador.
      return;
    }
    // No reducir consumo ante sobretension: retirar carga podria empeorarla.
    // Mantener orden existente y bloquear la busqueda; requiere supervision.
    // [Guía] Comprueba `!isfinite(bus) || !isfinite(pv) || !isfinite(ipv) || !isfinite(ibus)`. Si se cumple,
    // [Guía] ejecuta el bloque asociado.
    if (!isfinite(bus) || !isfinite(pv) || !isfinite(ipv) || !isfinite(ibus)) {
      // [Guía] Termina la búsqueda e informa la causa al diagnóstico.
      cancel("SENSOR_INVALID");
      // [Guía] Finaliza aquí la función y devuelve el control al llamador.
      return;
    }
    // [Guía] Comprueba `bus >= Config::EMULATOR_BUS_HIGH_V`. Si se cumple, ejecuta el bloque asociado.
    if (bus >= Config::EMULATOR_BUS_HIGH_V) {
      // [Guía] Termina la búsqueda e informa la causa al diagnóstico.
      cancel("BUS_HIGH_HOLD");
      // [Guía] Finaliza aquí la función y devuelve el control al llamador.
      return;
    }
    // [Guía] Comprueba `state_ != START && (!bk.enlace || !bk.habilitarEmulador`. Si se cumple, ejecuta el bloque
    // [Guía] asociado. Aquí intervienen estado operativo interno.
    if (state_ != START && (!bk.enlace || !bk.habilitarEmulador
        // [Guía] Añade una alternativa suficiente dentro de la condición compuesta.
        || !strcmp(bk.estado, "ERROR_BK") || !strcmp(bk.estado, "ACK_TIMEOUT"))) {
      // [Guía] Termina la búsqueda e informa la causa al diagnóstico.
      cancel("LINK_OR_ACK_FAULT");
      // [Guía] Finaliza aquí la función y devuelve el control al llamador.
      return;
    }
    // Antes de ON y despues de cada ajuste, exigir confirmacion de R y LOAD.
    // rAck compara consigna actual y releida. offAck/onAck requieren tanto
    // estado aceptado como deseo coherente: no aceptar una orden ya reemplazada.
    // [Guía] Actualiza `rAck` con `bk.resistenciaConfirmada && !bk.enviarNuevaResistencia`.
    const bool rAck = bk.resistenciaConfirmada && !bk.enviarNuevaResistencia
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
      && fabsf(bk.resistenciaConfirmadaOhm - bk.resistenciaOhm) < 0.0011f;
    // [Guía] Actualiza `offAck` con `bk.estadoConsumoValido && !bk.consumoConfirmado`.
    const bool offAck = bk.estadoConsumoValido && !bk.consumoConfirmado
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
      && !bk.consumoSolicitado;
    // [Guía] Actualiza `onAck` con `bk.estadoConsumoValido && bk.consumoConfirmado`.
    const bool onAck = bk.estadoConsumoValido && bk.consumoConfirmado
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
      && bk.consumoSolicitado;
    // [Guía] Evalúa la estabilidad del bus con la ventana de extremos antes de decidir si es prudente aumentar la
    // [Guía] demanda.
    const bool quiet = busWindow_.observe(bus, now,
      // [Guía] Aporta `Config::EMULATOR_QUIET_WINDOW_MS, Config::EMULATOR_BUS_PP_MAX_V` como parte de los
      // [Guía] parámetros de la operación.
      Config::EMULATOR_QUIET_WINDOW_MS, Config::EMULATOR_BUS_PP_MAX_V);
    // [Guía] Comprueba `state_ == START`. Si se cumple, ejecuta el bloque asociado. Aquí intervienen estado
    // [Guía] operativo interno.
    if (state_ == START) {
      // [Guía] Comprueba `bk.enlace && rAck && offAck`. Si se cumple, ejecuta el bloque asociado.
      if (bk.enlace && rAck && offAck) {
        // [Guía] Cambia el estado operativo que se evaluará a partir de la siguiente actualización. Aquí
        // [Guía] intervienen estado operativo interno.
        state_ = READY_OFF;
        // [Guía] Actualiza `status_` con `"WAIT_BUS_OFF"`.
        status_ = "WAIT_BUS_OFF";
        // [Guía] Actualiza `entered_` con `now`.
        entered_ = now;
        // [Guía] Invoca `clearTimers` con los argumentos de esta expresión.
        clearTimers();
      }
      // Esperar al puente aunque se abra mucho despues del Arduino. Un fallo
      // de una transaccion real sigue requiriendo revision, no reintentos ON.
      // [Guía] Comprueba `!strcmp(bk.estado, ) || !strcmp(bk.estado, )`. Si se cumple, ejecuta el bloque
      // [Guía] asociado.
      if (!strcmp(bk.estado, "ERROR_BK") || !strcmp(bk.estado, "ACK_TIMEOUT"))
        // [Guía] Termina la búsqueda e informa la causa al diagnóstico.
        cancel("LINK_OR_ACK_FAULT");
      // [Guía] Finaliza aquí la función y devuelve el control al llamador.
      return;
    }
    // [Guía] Comprueba `state_ == READY_OFF`. Si se cumple, ejecuta el bloque asociado. Aquí intervienen estado
    // [Guía] operativo interno.
    if (state_ == READY_OFF) {
      // [Guía] Comprueba `!bk.enlace || !rAck || !offAck`. Si se cumple, ejecuta el bloque asociado.
      if (!bk.enlace || !rAck || !offAck) {
        // [Guía] Termina la búsqueda e informa la causa al diagnóstico.
        cancel("OFF_STATE_CHANGED");
        // [Guía] Finaliza aquí la función y devuelve el control al llamador.
        return;
      }
      // [Guía] Actualiza `stable` con `quiet && fabsf(bus - Config::BUS_REFERENCE_V)`.
      const bool stable = quiet && fabsf(bus - Config::BUS_REFERENCE_V)
        // [Guía] Completa la declaración o expresión con `<= Config::EMULATOR_STABLE_BAND_V`.
        <= Config::EMULATOR_STABLE_BAND_V;
      // [Guía] Actualiza `stableLong` con `held(stable, now, Config::EMULATOR_STABLE_MS`.
      const bool stableLong = held(stable, now, Config::EMULATOR_STABLE_MS,
                                   // [Guía] Aporta `stableSince_, stableRunning_` como parte de los parámetros de
                                   // [Guía] la operación.
                                   stableSince_, stableRunning_);
      // [Guía] Actualiza `targetLong` con `held(stable && pv >= Config::EMULATOR_PV_TARGET_V`.
      const bool targetLong = held(stable && pv >= Config::EMULATOR_PV_TARGET_V,
        // [Guía] Aporta `now, Config::EMULATOR_TARGET_MS, targetSince_, targetRunning_` como parte de los
        // [Guía] parámetros de la operación.
        now, Config::EMULATOR_TARGET_MS, targetSince_, targetRunning_);
      // [Guía] Comprueba `stableLong && targetLong`. Si se cumple, ejecuta el bloque asociado.
      if (stableLong && targetLong) {
        // [Guía] Actualiza `status_` con `"PV_TARGET_OFF_HOLD"`.
        status_ = "PV_TARGET_OFF_HOLD"; // Esperar sin cancelar el automatico.
        // [Guía] Finaliza aquí la función y devuelve el control al llamador.
        return;
      }
      // Histeresis para no encender/apagar alrededor del objetivo por ruido.
      // [Guía] Actualiza `resumePV` con `!strcmp(status_, "PV_TARGET_OFF_HOLD")`.
      const float resumePV = !strcmp(status_, "PV_TARGET_OFF_HOLD")
        // [Guía] Define una de las dos salidas de la expresión condicional.
        ? Config::EMULATOR_PV_TARGET_V - 0.5f : Config::EMULATOR_PV_TARGET_V;
      // [Guía] Comprueba `stableLong && pv < resumePV`. Si se cumple, ejecuta el bloque asociado.
      if (stableLong && pv < resumePV) {
        // [Guía] Comprueba `bk.solicitarConsumo(true)`. Si se cumple, ejecuta el bloque asociado.
        if (bk.solicitarConsumo(true)) {
          // [Guía] Cambia el estado operativo que se evaluará a partir de la siguiente actualización. Aquí
          // [Guía] intervienen estado operativo interno.
          state_ = WAIT_ON;
          // [Guía] Actualiza `status_` con `"WAIT_ON_ACK"`.
          status_ = "WAIT_ON_ACK";
          // [Guía] Actualiza `entered_` con `now`.
          entered_ = now;
          // [Guía] Invoca `clearTimers` con los argumentos de esta expresión.
          clearTimers();
        }
      }
      // [Guía] Finaliza aquí la función y devuelve el control al llamador.
      return;
    }
    // Corriente/PV fuera de limites no son una simple pausa de estabilidad.
    // La misma comprobacion se aplica a corriente PV y corriente total del bus;
    // fabsf permite detectar exceso de magnitud en cualquiera de los sentidos.
    // [Guía] Actualiza `overloaded` con `pv <= Config::EMULATOR_PV_LOW_V`.
    const bool overloaded = pv <= Config::EMULATOR_PV_LOW_V
      // [Guía] Añade una alternativa suficiente dentro de la condición compuesta.
      || fabsf(ipv) >= Config::EMULATOR_CURRENT_MAX_A
      // [Guía] Añade una alternativa suficiente dentro de la condición compuesta.
      || fabsf(ibus) >= Config::EMULATOR_CURRENT_MAX_A;
    // [Guía] Comprueba `held(overloaded, now, Config::EMULATOR_UNSTABLE_MS, badSince_, badRunning_)`. Si se
    // [Guía] cumple, ejecuta el bloque asociado.
    if (held(overloaded, now, Config::EMULATOR_UNSTABLE_MS, badSince_, badRunning_)) {
      // [Guía] Invoca `bk.configurarResistencia` con los argumentos de esta expresión. Aquí intervienen última
      // [Guía] resistencia calificada como estable.
      bk.configurarResistencia(safeR_);
      // [Guía] Termina la búsqueda e informa la causa al diagnóstico.
      cancel("LIMIT_ROLLBACK");
      // [Guía] Finaliza aquí la función y devuelve el control al llamador.
      return;
    }
    // [Guía] Comprueba `state_ == WAIT_RECOVERY`. Si se cumple, ejecuta el bloque asociado. Aquí intervienen
    // [Guía] estado operativo interno.
    if (state_ == WAIT_RECOVERY) {
      // No reenviar rollback en cada muestra mientras viaja su ACK.
      // [Guía] Comprueba `rAck && onAck`. Si se cumple, ejecuta el bloque asociado.
      if (rAck && onAck) {
        // [Guía] Cambia el estado operativo que se evaluará a partir de la siguiente actualización. Aquí
        // [Guía] intervienen estado operativo interno.
        state_ = RECOVERY;
        // [Guía] Actualiza `entered_` con `now`.
        entered_ = now;
        // [Guía] Invoca `clearTimers` con los argumentos de esta expresión.
        clearTimers();
        // [Guía] Actualiza `status_` con `"RECOVERY_WAIT_BUS"`.
        status_ = "RECOVERY_WAIT_BUS";
      // [Guía] Comprueba `uint32_t(now - entered_) >= Config::EMULATOR_WAIT_MS`. Si se cumple, ejecuta el bloque
      // [Guía] asociado.
      } else if (uint32_t(now - entered_) >= Config::EMULATOR_WAIT_MS)
        // [Guía] Termina la búsqueda e informa la causa al diagnóstico.
        cancel("ORDER_TIMEOUT");
      // [Guía] Finaliza aquí la función y devuelve el control al llamador.
      return;
    }
    // [Guía] Actualiza `unstable` con `fabsf(bus - Config::BUS_REFERENCE_V)`.
    const bool unstable = fabsf(bus - Config::BUS_REFERENCE_V)
      // [Guía] Completa la declaración o expresión con `> Config::EMULATOR_UNSTABLE_BAND_V`.
      > Config::EMULATOR_UNSTABLE_BAND_V;
    // [Guía] Comprueba `state_ != RECOVERY && held(unstable, now, Config::EMULATOR_UNSTABLE_MS,`. Si se cumple,
    // [Guía] ejecuta el bloque asociado. Aquí intervienen estado operativo interno.
    if (state_ != RECOVERY && held(unstable, now, Config::EMULATOR_UNSTABLE_MS,
                                   // [Guía] Aporta `unstableSince_, unstableRunning_)) {` como parte de los
                                   // [Guía] parámetros de la operación.
                                   unstableSince_, unstableRunning_)) {
      // Contar fallos consecutivos en el mismo punto, no en toda la bajada.
      // Limitar intentos evita insistir indefinidamente en una zona inestable.
      // [Guía] Comprueba `!bk.configurarResistencia(safeR_)`. Si se cumple, ejecuta el bloque asociado. Aquí
      // [Guía] intervienen última resistencia calificada como estable.
      if (!bk.configurarResistencia(safeR_)) { cancel("R_REJECTED"); return; }
      // [Guía] Comprueba `++recoveries_ > 4`. Si se cumple, ejecuta el bloque asociado.
      if (++recoveries_ > 4) { cancel("RETRY_LIMIT"); return; }
      // [Guía] Escala `stepScale_` mediante `0.5f`.
      stepScale_ *= 0.5f;
      // [Guía] Cambia el estado operativo que se evaluará a partir de la siguiente actualización. Aquí
      // [Guía] intervienen estado operativo interno.
      state_ = WAIT_RECOVERY;
      // [Guía] Actualiza `entered_` con `now`.
      entered_ = now;
      // [Guía] Invoca `clearTimers` con los argumentos de esta expresión.
      clearTimers();
      // [Guía] Actualiza `status_` con `"ROLLBACK_WAIT_ACK"`.
      status_ = "ROLLBACK_WAIT_ACK";
      // [Guía] Finaliza aquí la función y devuelve el control al llamador.
      return;
    }
    // [Guía] Comprueba `state_ == WAIT_ON || state_ == WAIT_R`. Si se cumple, ejecuta el bloque asociado. Aquí
    // [Guía] intervienen estado operativo interno.
    if (state_ == WAIT_ON || state_ == WAIT_R) {
      // [Guía] Comprueba `rAck && onAck`. Si se cumple, ejecuta el bloque asociado.
      if (rAck && onAck) {
        // [Guía] Cambia el estado operativo que se evaluará a partir de la siguiente actualización. Aquí
        // [Guía] intervienen estado operativo interno.
        state_ = RUN;
        // [Guía] Actualiza `entered_` con `now`.
        entered_ = now;
        // [Guía] Actualiza `status_` con `"SETTLING"`.
        status_ = "SETTLING";
        // [Guía] Invoca `clearTimers` con los argumentos de esta expresión.
        clearTimers(); // Ventana nueva despues de confirmar R/LOAD.
      // [Guía] Comprueba `uint32_t(now - entered_) >= Config::EMULATOR_WAIT_MS`. Si se cumple, ejecuta el bloque
      // [Guía] asociado.
      } else if (uint32_t(now - entered_) >= Config::EMULATOR_WAIT_MS)
        // [Guía] Termina la búsqueda e informa la causa al diagnóstico.
        cancel("ORDER_TIMEOUT");
      // [Guía] Finaliza aquí la función y devuelve el control al llamador.
      return;
    }
    // [Guía] Comprueba `!rAck || !onAck`. Si se cumple, ejecuta el bloque asociado.
    if (!rAck || !onAck) {
      // [Guía] Termina la búsqueda e informa la causa al diagnóstico.
      cancel("LOAD_STATE_CHANGED");
      // [Guía] Finaliza aquí la función y devuelve el control al llamador.
      return;
    }
    // [Guía] Actualiza `inBand` con `fabsf(bus - Config::BUS_REFERENCE_V) <= Config::EMULATOR_STABLE_BAND_V`.
    const bool inBand = fabsf(bus - Config::BUS_REFERENCE_V) <= Config::EMULATOR_STABLE_BAND_V;
    // [Guía] Actualiza `stable` con `inBand && quiet`.
    const bool stable = inBand && quiet;
    // [Guía] Comprueba `state_ == RECOVERY`. Si se cumple, ejecuta el bloque asociado. Aquí intervienen estado
    // [Guía] operativo interno.
    if (state_ == RECOVERY) {
      // [Guía] Actualiza `recovered` con `held(stable, now, Config::EMULATOR_STABLE_MS`.
      const bool recovered = held(stable, now, Config::EMULATOR_STABLE_MS,
                                  // [Guía] Aporta `stableSince_, stableRunning_` como parte de los parámetros de
                                  // [Guía] la operación.
                                  stableSince_, stableRunning_);
      // [Guía] Comprueba `recovered && uint32_t(now - entered_) >= Config::EMULATOR_SETTLE_MS`. Si se cumple,
      // [Guía] ejecuta el bloque asociado.
      if (recovered && uint32_t(now - entered_) >= Config::EMULATOR_SETTLE_MS) {
        // [Guía] Cambia el estado operativo que se evaluará a partir de la siguiente actualización. Aquí
        // [Guía] intervienen estado operativo interno.
        state_ = RUN;
        // [Guía] Actualiza `status_` con `"RESUMED_SMALLER_STEP"`.
        status_ = "RESUMED_SMALLER_STEP";
        // [Guía] Actualiza `entered_` con `now`.
        entered_ = now;
        // [Guía] Invoca `clearTimers` con los argumentos de esta expresión.
        clearTimers();
      }
      // [Guía] Finaliza aquí la función y devuelve el control al llamador.
      return;
    }
    // [Guía] Comprueba `state_ == TARGET_HOLD`. Si se cumple, ejecuta el bloque asociado. Aquí intervienen estado
    // [Guía] operativo interno.
    if (state_ == TARGET_HOLD) {
      // Alcanzar Vpv no es fallo ni congelacion: si cae 0.5 V y el bus vuelve
      // a estar estable, continuar sin AUTO_R y sin reiniciar R a 4000.
      // [Guía] Comprueba `held(stable && pv < Config::EMULATOR_PV_TARGET_V - 0.5f, now,`. Si se cumple, ejecuta
      // [Guía] el bloque asociado.
      if (held(stable && pv < Config::EMULATOR_PV_TARGET_V - 0.5f, now,
               // [Guía] Aporta `Config::EMULATOR_STABLE_MS, stableSince_, stableRunning_)) {` como parte de los
               // [Guía] parámetros de la operación.
               Config::EMULATOR_STABLE_MS, stableSince_, stableRunning_)) {
        // [Guía] Cambia el estado operativo que se evaluará a partir de la siguiente actualización. Aquí
        // [Guía] intervienen estado operativo interno.
        state_ = RUN;
        // [Guía] Actualiza `entered_` con `now`.
        entered_ = now;
        // [Guía] Invoca `clearTimers` con los argumentos de esta expresión.
        clearTimers();
        // [Guía] Actualiza `status_` con `"TARGET_RESUMED"`.
        status_ = "TARGET_RESUMED";
      }
      // [Guía] Finaliza aquí la función y devuelve el control al llamador.
      return;
    }
    // [Guía] Comprueba `held(stable && pv >= Config::EMULATOR_PV_TARGET_V, now,`. Si se cumple, ejecuta el bloque
    // [Guía] asociado.
    if (held(stable && pv >= Config::EMULATOR_PV_TARGET_V, now,
             // [Guía] Aporta `Config::EMULATOR_TARGET_MS, targetSince_, targetRunning_)) {` como parte de los
             // [Guía] parámetros de la operación.
             Config::EMULATOR_TARGET_MS, targetSince_, targetRunning_)) {
      // [Guía] Cambia el estado operativo que se evaluará a partir de la siguiente actualización. Aquí
      // [Guía] intervienen estado operativo interno.
      state_ = TARGET_HOLD;
      // [Guía] Invoca `clearTimers` con los argumentos de esta expresión.
      clearTimers();
      // [Guía] Actualiza `status_` con `"PV_TARGET_HOLD"`.
      status_ = "PV_TARGET_HOLD"; // Conservar LOAD y R; NO entrar en M3.
      // [Guía] Finaliza aquí la función y devuelve el control al llamador.
      return;
    }
    // [Guía] Actualiza `stableLong` con `held(stable, now, Config::EMULATOR_STABLE_MS`.
    const bool stableLong = held(stable, now, Config::EMULATOR_STABLE_MS,
                                 // [Guía] Aporta `stableSince_, stableRunning_` como parte de los parámetros de
                                 // [Guía] la operación.
                                 stableSince_, stableRunning_);
    // [Guía] Comprueba `!stableLong || uint32_t(now - entered_) < Config::EMULATOR_SETTLE_MS`. Si se cumple,
    // [Guía] ejecuta el bloque asociado.
    if (!stableLong || uint32_t(now - entered_) < Config::EMULATOR_SETTLE_MS) {
      // [Guía] Actualiza `status_` con `!inBand ? "WAIT_BUS_STABLE" : (!quiet ? "WAIT_BUS_RIPPLE" : "SETTLING")`.
      status_ = !inBand ? "WAIT_BUS_STABLE" : (!quiet ? "WAIT_BUS_RIPPLE" : "SETTLING");
      // [Guía] Finaliza aquí la función y devuelve el control al llamador.
      return;
    }
    // Un nuevo punto confirmado y estable demuestra progreso: los fallos de
    // puntos anteriores no deben bloquear la busqueda mas adelante.
    // Volver al mismo R tras rollback NO reinicia el contador de intentos.
    // [Guía] Comprueba `bk.resistenciaConfirmadaOhm < safeR_ - 0.0011f`. Si se cumple, ejecuta el bloque
    // [Guía] asociado. Aquí intervienen última resistencia calificada como estable.
    if (bk.resistenciaConfirmadaOhm < safeR_ - 0.0011f)
      // [Guía] Actualiza `recoveries_` con `0`.
      recoveries_ = 0;
    // [Guía] Actualiza `safeR_` con `bk.resistenciaConfirmadaOhm`. Este dato representa última resistencia
    // [Guía] calificada como estable.
    safeR_ = bk.resistenciaConfirmadaOhm;
    // [Guía] Comprueba `safeR_ <= Config::EMULATOR_MIN_OHM`. Si se cumple, ejecuta el bloque asociado. Aquí
    // [Guía] intervienen última resistencia calificada como estable.
    if (safeR_ <= Config::EMULATOR_MIN_OHM) {
      // [Guía] Termina la búsqueda e informa la causa al diagnóstico.
      cancel("R_MIN_HOLD");
      // [Guía] Finaliza aquí la función y devuelve el control al llamador.
      return;
    }
    // No seguir aumentando demanda si Vpv ya alcanzo el objetivo pero falta
    // confirmar su persistencia. Esperar sin generar una orden extra.
    // [Guía] Comprueba `pv >= Config::EMULATOR_PV_TARGET_V`. Si se cumple, termina la función en esta rama.
    if (pv >= Config::EMULATOR_PV_TARGET_V) return;
    // Reducir el paso tras una recuperacion no cambia las fronteras de tramos.
    // max0.5 evita que el redondeo deje la busqueda sin variacion de consigna.
    // [Guía] Actualiza `baseNext` con `nextResistance(safeR_)`.
    const float baseNext = nextResistance(safeR_);
    // Paso minimo para alcanzar las fronteras sin estancarse por redondeo.
    // [Guía] Actualiza `drop` con `fmaxf(0.5f, (safeR_ - baseNext) * stepScale_)`.
    const float drop = fmaxf(0.5f, (safeR_ - baseNext) * stepScale_);
    // [Guía] Actualiza `next` con `fmaxf(baseNext, safeR_ - drop)`.
    const float next = fmaxf(baseNext, safeR_ - drop);
    // [Guía] Comprueba `bk.configurarResistencia(next)`. Si se cumple, ejecuta el bloque asociado.
    if (bk.configurarResistencia(next)) {
      // [Guía] Cambia el estado operativo que se evaluará a partir de la siguiente actualización. Aquí
      // [Guía] intervienen estado operativo interno.
      state_ = WAIT_R;
      // [Guía] Actualiza `entered_` con `now`.
      entered_ = now;
      // [Guía] Actualiza `status_` con `"WAIT_R_ACK"`.
      status_ = "WAIT_R_ACK";
      // [Guía] Invoca `clearTimers` con los argumentos de esta expresión.
      clearTimers();
    // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
    } else cancel("R_REJECTED");
  }

 // [Guía] Desde aquí se declaran los datos y auxiliares internos de la clase.
 private:
  // [Guía] Reúne bajo `State` las declaraciones que comparten este tipo o ámbito.
  enum State { START, READY_OFF, WAIT_ON, WAIT_R, RUN, WAIT_RECOVERY, RECOVERY, TARGET_HOLD };
  // [Guía] Cambia el estado operativo que se evaluará a partir de la siguiente actualización. Aquí intervienen
  // [Guía] estado operativo interno.
  State state_ = START;
  // [Guía] Declara `hardwareReady_` para conservar hardware listo en este ámbito.
  bool hardwareReady_;
  // [Guía] Actualiza `active_` con `false`.
  bool active_ = false;
  // [Guía] Declara `busWindow_` para conservar bus DC ventana en este ámbito.
  BusStabilityWindow busWindow_;
  // [Guía] Actualiza `status_` con `"OFF"`.
  const char* status_ = "OFF";
  // [Guía] Actualiza `safeR_` con `Config::EMULATOR_START_OHM`. Este dato representa última resistencia
  // [Guía] calificada como estable.
  float safeR_ = Config::EMULATOR_START_OHM;
  // [Guía] Actualiza `stepScale_` con `1.0f`.
  float stepScale_ = 1.0f;
  // [Guía] Actualiza `recoveries_` con `0`.
  unsigned recoveries_ = 0;
  // [Guía] Actualiza `unstableSince_` con `0`.
  uint32_t unstableSince_ = 0;
  // [Guía] Actualiza `unstableRunning_` con `false`.
  bool unstableRunning_ = false;
  // [Guía] Actualiza `entered_` con `0, stableSince_ = 0, badSince_ = 0, targetSince_ = 0`.
  uint32_t entered_ = 0, stableSince_ = 0, badSince_ = 0, targetSince_ = 0;
  // [Guía] Actualiza `stableRunning_` con `false, badRunning_ = false, targetRunning_ = false`.
  bool stableRunning_ = false, badRunning_ = false, targetRunning_ = false;

  // Restas unsigned conservan temporizacion incluso al desbordar millis().
  // Confirmador continuo por referencia: since/running pertenecen al criterio
  // del llamador. Una sola muestra falsa cancela su tiempo acumulado.
  // [Guía] Califica una condición sostenida usando el reloj de confirmación recibido por referencia. El resultado
  // [Guía] es una decisión booleana para el llamador. Recibe `condition`, `now`, `duration`, `since`, `running`.
  static bool held(bool condition, uint32_t now, uint32_t duration,
                   // [Guía] Declara `since` para conservar since en este ámbito.
                   uint32_t& since, bool& running) {
    // [Guía] Comprueba `!condition`. Si se cumple, ejecuta el bloque asociado.
    if (!condition) { running = false; return false; }
    // [Guía] Comprueba `!running`. Si se cumple, ejecuta el bloque asociado.
    if (!running) { running = true; since = now; }
    // [Guía] Entrega `uint32_t(now - since) >= duration` al llamador.
    return uint32_t(now - since) >= duration;
  }
  // No reutilizar estabilidad calificada antes de cambiar R o de recibir ACK.
  // [Guía] Limpia timers.
  void clearTimers() {
    // [Guía] Actualiza `stableRunning_` con `badRunning_ = targetRunning_ = unstableRunning_ = false`.
    stableRunning_ = badRunning_ = targetRunning_ = unstableRunning_ = false;
    // [Guía] Borra los extremos y el tiempo de estabilidad al reiniciar la búsqueda.
    busWindow_.reset();
  }
};
