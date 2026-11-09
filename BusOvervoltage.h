// [Guía] Administra la carga auxiliar. Distingue las solicitudes del firmware de las confirmaciones de
// [Guía] resistencia y consumo del instrumento y gestiona la ayuda y su retiro.
// [Guía] Evita procesar este encabezado dos veces en una misma unidad de compilación.
#pragma once
/* MITIGACION CON CARGA AUXILIAR Y ENCLAVAMIENTO
 * No confundir con los cortes duros de BatteryTestController. Aqui solo se
 * solicita una resistencia y despues LOAD ON mediante EmulatorController.
 * now y todos los relojes de esta clase estan en MILISEGUNDOS.
 * update() conserva el enclavamiento legacy. updateEmsAssist() aplica la
 * politica temporal M1/M3/retirada. inhibited_ bloquea nuevos intentos tras
 * error o parada humana. Un ACK no demuestra consumo fisico.
 */
// [Guía] Incorpora las declaraciones de "Config.h" antes de usar sus tipos y funciones.
#include "Config.h"
// [Guía] Incorpora las declaraciones de "EmulatorController.h" antes de usar sus tipos y funciones.
#include "EmulatorController.h"

// Supervisor enclavado: pide R/LOAD sin bloquear ni escribir PWM.
// LOAD confirmado significa orden aceptada, no consumo fisico comprobado.
// [Guía] Reúne bajo `BusOvervoltage` las declaraciones que comparten este tipo o ámbito.
class BusOvervoltage {
 // [Guía] Desde aquí se declaran los tipos y operaciones que usa el llamador.
 public:
  // [Guía] Expone `active_` al módulo que consulta active.
  bool active() const { return active_; }
  // [Guía] Expone `status_` al módulo que consulta status.
  const char* status() const { return status_; }
  // [Guía] Expone `cause_` al módulo que consulta cause.
  const char* cause() const { return cause_; }
  // [Guía] Expone `lastStopReason_` al módulo que consulta last stop reason.
  const char* lastStopReason() const { return lastStopReason_; }
  // [Guía] Devuelve resistencia administrada en Ω.
  float requestedOhm() const { return resistance_; }
  // [Guía] Expone `m1Adjusting_` al módulo que consulta m1 adjusting.
  bool m1Adjusting() const { return m1Adjusting_; }
  // M2 no regula bus: solicitar OFF prioritario sin la rampa de resistencia
  // heredada de M1/M3 y sin inhibir futuras ayudas. El ACK sigue siendo remoto.
  // [Guía] Retira la carga auxiliar al entrar a una ruta donde el bus deja de regularse. Recibe `bk`, `now`.
  void stopForM2(EmulatorController& bk, uint32_t now) {
    // [Guía] Actualiza `managedMode_` con `true`.
    managedMode_ = true;
    // [Guía] Pide apagar la carga administrada, registra el motivo e inicia la espera que corresponda para volver
    // [Guía] a habilitarla.
    releaseManagedLoad(bk, now, "OFF_M2_CURRENT_ONLY");
  }
  // Una parada humana debe prevalecer: no volver a activar LOAD en cada loop.
  // [Guía] Registra la orden de paro manual y limpia el estado de la política auxiliar.
  void manualStop() {
    // [Guía] Comprueba `active_ || managedMode_`. Si se cumple, ejecuta el bloque asociado.
    if (active_ || managedMode_) {
      // [Guía] Actualiza `inhibited_` con `true; inhibitReason_ = "INHIBIT_MANUAL_OFF"; status_ =
      // [Guía] inhibitReason_`.
      inhibited_ = true; inhibitReason_ = "INHIBIT_MANUAL_OFF"; status_ = inhibitReason_;
    }
  }
  // Rearme autorizado solo si el bus paso el tiempo configurado bajo el umbral.
  // No quita una carga existente: retirarla podria provocar otra sobretension.
  // [Guía] Reinicia el estado de este módulo. El resultado es una decisión booleana para el llamador.
  bool reset() {
    // [Guía] Comprueba `managedMode_ && active_ && !inhibited_`. Si se cumple, termina la función en esta rama.
    if (managedMode_ && active_ && !inhibited_) return false; // No reiniciar una rampa activa.
    // [Guía] Comprueba `(!active_ && !inhibited_) || !safeToReset_`. Si se cumple, termina la función en esta
    // [Guía] rama.
    if ((!active_ && !inhibited_) || !safeToReset_) return false;
    // [Guía] Actualiza `active_` con `false`.
    active_ = false;
    // [Guía] Actualiza `inhibited_` con `false`.
    inhibited_ = false;
    // [Guía] Actualiza `inhibitReason_` con `"NONE"`.
    inhibitReason_ = "NONE";
    // [Guía] Actualiza `highRunning_` con `recoveryRunning_ = false`.
    highRunning_ = recoveryRunning_ = false;
    // [Guía] Actualiza `managedStage_` con `AUX_IDLE; m1Adjusting_ = false`.
    managedStage_ = AUX_IDLE; m1Adjusting_ = false;
    // [Guía] Actualiza `cause_` con `"NONE"`.
    cause_ = "NONE";
    // [Guía] Actualiza `status_` con `managedMode_ ? "RESET_AUX_OFF" : "RESET_KEEP_LOAD"`.
    status_ = managedMode_ ? "RESET_AUX_OFF" : "RESET_KEEP_LOAD";
    // [Guía] Entrega `true` al llamador.
    return true;
  }
  // Politica EMS, separada del supervisor LEGACY update(). No hay
  // excepcion de encendido por35 V: las protecciones PWM de la EMS permanecen
  // independientes. M1 real ajusta R cada800ms; al cambiar modo retira rapido.
  // [Guía] Decide la ayuda auxiliar del perfil EMS. En M1 administra la reducción gradual y recuperación de
  // [Guía] resistencia. En M3 atiende la saturación de Q2 y coordina el retiro al salir del contexto. Recibe
  // [Guía] `bk`, `bus`, `pv`, `now`, `modeM1`, `modeM3`, `dutyQ2`, `measuresValid`, `m1BusPiRegulating`,
  // [Guía] `requireStableM1`.
  void updateEmsAssist(EmulatorController& bk, float bus, float pv, uint32_t now,
                       // [Guía] Declara `modeM1` para conservar mode M1 en este ámbito.
                       bool modeM1, bool modeM3, float dutyQ2, bool measuresValid = true,
                       // [Guía] Actualiza `m1BusPiRegulating` con `false, bool requireStableM1 = false) {`.
                       bool m1BusPiRegulating = false, bool requireStableM1 = false) {
    // [Guía] Actualiza `managedMode_` con `true`.
    managedMode_ = true;
    // [Guía] Calcula en ms cuánto tiempo representa la muestra para la lógica auxiliar. La primera lectura
    // [Guía] establece el origen sin sumar tiempo.
    const uint32_t sampleElapsed = sampleSeen_ ? uint32_t(now - lastSample_) : 0;
    // [Guía] Actualiza `fresh` con `!sampleSeen_`.
    const bool fresh = !sampleSeen_
      // [Guía] Añade una alternativa suficiente dentro de la condición compuesta.
      || uint32_t(now - lastSample_) <= Config::CONTROL_TIMEOUT_US / 1000;
    // [Guía] Actualiza `sampleSeen_` con `true; lastSample_ = now`.
    sampleSeen_ = true; lastSample_ = now;
    // [Guía] Comprueba `isfinite(bus) && fresh && bus < Config::BUS_OV_RESET_V`. Si se cumple, ejecuta el bloque
    // [Guía] asociado.
    if (isfinite(bus) && fresh && bus < Config::BUS_OV_RESET_V) {
      // [Guía] Comprueba `!safeRunning_`. Si se cumple, ejecuta el bloque asociado.
      if (!safeRunning_) { safeRunning_ = true; safeSince_ = now; }
      // [Guía] Habilita el rearme cuando la tensión se mantuvo en la zona admitida durante el periodo
      // [Guía] configurado.
      safeToReset_ = uint32_t(now - safeSince_) >= Config::BUS_OV_RESET_MS;
    // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
    } else safeRunning_ = safeToReset_ = false;
    // [Guía] Actualiza `m1Adjusting_` con `false`.
    m1Adjusting_ = false;
    // [Guía] Actualiza `supervisedM1` con `requireStableM1 && active_ && managedStage_ == AUX_M1 && modeM1`.
    const bool supervisedM1 = requireStableM1 && active_ && managedStage_ == AUX_M1 && modeM1;
    // [Guía] Actualiza `usable` con `measuresValid && fresh && isfinite(bus) && isfinite(pv)`.
    const bool usable = measuresValid && fresh && isfinite(bus) && isfinite(pv)
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
      && isfinite(dutyQ2) && bus > (supervisedM1 ? 0.0f : Config::AUX_BUS_OFF_V);
    // [Guía] Actualiza `busReady` con `bus >= Config::M3_AUX_BUS_MIN_V`.
    const bool busReady = bus >= Config::M3_AUX_BUS_MIN_V;
    // En el perfil real, primero regular bus con PI durante2s. No INICIAR
    // carga en ACQUIRE/LIMITED. Una carga ya activa se pausa/retrocede si PI
    // recupera su subetapa: no confundir esa recuperacion con salir de M1.
    // [Guía] Actualiza `stableM1` con `modeM1 && m1BusPiRegulating && usable`.
    const bool stableM1 = modeM1 && m1BusPiRegulating && usable
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
      && fabsf(bus - Config::BUS_REFERENCE_V) <= Config::M1_AUX_STABLE_BAND_V;
    // Acumular SOLO tiempo observado dentro de banda. Un pico corto suspende
    // el conteo, sin exigir otros2s desde cero;250ms fuera si borran el progreso.
    // Medida invalida, modo distinto o PI no adquirido reinician inmediatamente.
    // [Guía] Comprueba `!modeM1 || !m1BusPiRegulating || !usable`. Si se cumple, ejecuta el bloque asociado.
    if (!modeM1 || !m1BusPiRegulating || !usable) {
      // [Guía] Actualiza `m1StableRunning_` con `m1UnstableRunning_ = false; m1StableAccumMs_ = 0`.
      m1StableRunning_ = m1UnstableRunning_ = false; m1StableAccumMs_ = 0;
    // [Guía] Comprueba `stableM1`. Si se cumple, ejecuta el bloque asociado.
    } else if (stableM1) {
      // [Guía] Comprueba `m1StableRunning_`. Si se cumple, ejecuta `m1StableAccumMs_ += sampleElapsed <
      // [Guía] Config::M1_AUX_INITIAL_STABLE_MS`.
      if (m1StableRunning_) m1StableAccumMs_ += sampleElapsed < Config::M1_AUX_INITIAL_STABLE_MS
        // [Guía] Completa la declaración o expresión con `- m1StableAccumMs_ ? sampleElapsed :
        // [Guía] Config::M1_AUX_INITIAL_STABLE_MS - m1StableAccumMs_`.
        - m1StableAccumMs_ ? sampleElapsed : Config::M1_AUX_INITIAL_STABLE_MS - m1StableAccumMs_;
      // [Guía] Actualiza `m1StableRunning_` con `true; m1UnstableRunning_ = false`.
      m1StableRunning_ = true; m1UnstableRunning_ = false;
    // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
    } else {
      // [Guía] Actualiza `m1StableRunning_` con `false`.
      m1StableRunning_ = false;
      // [Guía] Comprueba `!m1UnstableRunning_`. Si se cumple, ejecuta el bloque asociado.
      if (!m1UnstableRunning_) { m1UnstableRunning_ = true; m1UnstableSince_ = now; }
      // [Guía] Comprueba `uint32_t(now - m1UnstableSince_) >= Config::M1_AUX_STABLE_GAP_MS`. Si se cumple,
      // [Guía] ejecuta `m1StableAccumMs_ = 0`.
      if (uint32_t(now - m1UnstableSince_) >= Config::M1_AUX_STABLE_GAP_MS) m1StableAccumMs_ = 0;
    }
    // [Guía] Actualiza `m1Qualified` con `!requireStableM1 || (m1BusPiRegulating`.
    const bool m1Qualified = !requireStableM1 || (m1BusPiRegulating
      // [Guía] Agrupa condiciones alternativas y conjuntas en la prueba en curso.
      && (managedStage_ == AUX_M1 || (m1StableRunning_
        // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
        && m1StableAccumMs_ >= Config::M1_AUX_INITIAL_STABLE_MS)));
    // [Guía] Actualiza `m3Allowed` con `modeM3 && usable`.
    const bool m3Allowed = modeM3 && usable
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
      && dutyQ2 >= Config::M3_AUX_DUTY_MIN && dutyQ2 <= Config::M3_AUX_DUTY_MAX
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
      && bus >= Config::M3_AUX_BUS_MIN_V;
    // Dos contextos distintos: adquirir rama desde PV alto, o preparar desde
    // PV bajo con PI de bus ya establecido. No detener la segunda por PV<18V.
    // [Guía] Actualiza `m1TargetReached` con `m1BusPiRegulating`.
    const bool m1TargetReached = m1BusPiRegulating
      // [Guía] Define una de las dos salidas de la expresión condicional.
      ? pv >= Config::M1_AUX_PV_TARGET_V - Config::M1_AUX_PV_TOLERANCE_V
      // [Guía] Define una de las dos salidas de la expresión condicional.
      : pv <= Config::M1_AUX_PV_TARGET_V + Config::M1_AUX_PV_TOLERANCE_V;
    // [Guía] Actualiza `m1Allowed` con `modeM1 && m1Qualified && usable`.
    const bool m1Allowed = modeM1 && m1Qualified && usable
      // [Guía] Agrupa condiciones alternativas y conjuntas en la prueba en curso.
      && (managedStage_ == AUX_M1 || (busReady && (requireStableM1 || !m1TargetReached)));
    // Perfil real: activar a4000ohm tambien si PV ya alcanzo18V. En ese caso
    // mantener esa R, sin seguir aumentandole consumo al panel innecesariamente.
    // Enlace/ACK y ordenes humanas prevalecen incluso durante una pausa.
    // [Guía] Comprueba `active_ && ((!bk.enlace && wasLinked_) || !strcmp(bk.estado, )`. Si se cumple, ejecuta el
    // [Guía] bloque asociado.
    if (active_ && ((!bk.enlace && wasLinked_) || !strcmp(bk.estado, "ERROR_BK")
        // [Guía] Añade una alternativa suficiente dentro de la condición compuesta.
        || !strcmp(bk.estado, "ACK_TIMEOUT") || !strcmp(bk.estado, "TX_ERROR"))) {
      // [Guía] Coordina consignas de resistencia y carga en el orden en que las puede confirmar la B&K.
      serviceLoad(bk); releaseManagedLoad(bk, now, inhibitReason_); return;
    }
    // [Guía] Comprueba `supervisedM1`. Si se cumple, ejecuta `observeM1ResistanceAck(bk)`.
    if (supervisedM1) observeM1ResistanceAck(bk);
    // Un valle operativo de M1 reduce demanda en vez de apagar y reiniciar
    // desde4000ohm. No mantener consumo ante colapso real ni sin observacion.
    // [Guía] Comprueba `supervisedM1 && usable`. Si se cumple, ejecuta el bloque asociado.
    if (supervisedM1 && usable
        // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
        && m1Collapse_.held(bus <= Config::M1_AUX_COLLAPSE_OFF_V, now,
                            // [Guía] Aporta `Config::M1_AUX_COLLAPSE_CONFIRM_MS)) {` como parte de los parámetros
                            // [Guía] de la operación.
                            Config::M1_AUX_COLLAPSE_CONFIRM_MS)) {
      // [Guía] Pide apagar la carga administrada, registra el motivo e inicia la espera que corresponda para
      // [Guía] volver a habilitarla.
      releaseManagedLoad(bk, now, "OFF_M1_BUS_COLLAPSE"); return;
    }
    // [Guía] Comprueba `!supervisedM1 || !usable || bus > Config::M1_AUX_COLLAPSE_OFF_V`. Si se cumple, ejecuta
    // [Guía] `m1Collapse_.clear()`.
    if (!supervisedM1 || !usable || bus > Config::M1_AUX_COLLAPSE_OFF_V) m1Collapse_.clear();
    // El permiso agregado incluye corriente auxiliar maxima. Una muestra
    // aislada NO prueba ruido: mantener R sin aumentar demanda solo100ms.
    // Mas tiempo invalido exige OFF, no5s operando a ciegas. La espera5s de
    // abajo es para inestabilidad con medidas utilizables, no fallos duros.
    // [Guía] Comprueba `!usable && !inhibited_ && supervisedM1 && fresh`. Si se cumple, ejecuta el bloque
    // [Guía] asociado.
    if (!usable && !inhibited_ && supervisedM1 && fresh
        // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
        && isfinite(bus) && isfinite(pv) && isfinite(dutyQ2)
        // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
        && bus > Config::M1_AUX_COLLAPSE_OFF_V && bk.consumoSolicitado
        // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
        && bk.estadoConsumoValido && bk.consumoConfirmado) {
      // [Guía] Comprueba `!m1InvalidRunning_`. Si se cumple, ejecuta el bloque asociado.
      if (!m1InvalidRunning_) { m1InvalidRunning_ = true; m1InvalidSince_ = now; }
      // [Guía] Marca el comienzo de la pausa M1 para dar al bus una oportunidad de recuperar estabilidad.
      beginM1Pause(now);
      // [Guía] Actualiza `m1ResumeRunning_` con `false`.
      m1ResumeRunning_ = false;
      // [Guía] Comprueba `uint32_t(now - m1InvalidSince_) < Config::M1_AUX_INVALID_GRACE_MS`. Si se cumple,
      // [Guía] ejecuta el bloque asociado.
      if (uint32_t(now - m1InvalidSince_) < Config::M1_AUX_INVALID_GRACE_MS) {
        // [Guía] Actualiza `status_` con `"M1_AUX_HOLD_INVALID_SAMPLE"; return`.
        status_ = "M1_AUX_HOLD_INVALID_SAMPLE"; return;
      }
      // [Guía] Pide apagar la carga administrada, registra el motivo e inicia la espera que corresponda para
      // [Guía] volver a habilitarla.
      releaseManagedLoad(bk, now, "OFF_INVALID_PERSISTENT"); return;
    }
    // [Guía] Comprueba `usable`. Si se cumple, ejecuta `m1InvalidRunning_ = false`.
    if (usable) m1InvalidRunning_ = false;
    // [Guía] Comprueba `!usable || inhibited_`. Si se cumple, ejecuta el bloque asociado.
    if (!usable || inhibited_) {
      // [Guía] Actualiza `cause` con `inhibited_ ? inhibitReason_`.
      const char* cause = inhibited_ ? inhibitReason_
        // [Guía] Define una de las dos salidas de la expresión condicional.
        : !fresh ? "OFF_STALE_MEASUREMENT"
        // [Guía] Define una de las dos salidas de la expresión condicional.
        : isfinite(bus) && bus <= Config::AUX_BUS_OFF_V ? "OFF_BUS_DEFICIT" : "OFF_INVALID_MEASUREMENT";
      // [Guía] Pide apagar la carga administrada, registra el motivo e inicia la espera que corresponda para
      // [Guía] volver a habilitarla.
      releaseManagedLoad(bk, now, cause);
      // [Guía] Finaliza aquí la función y devuelve el control al llamador.
      return;
    }
    // El cambio de modo tiene prioridad sobre la pausa por un valle del bus.
    // [Guía] Comprueba `(managedStage_ == AUX_M1 && !modeM1)`. Si se cumple, ejecuta el bloque asociado.
    if ((managedStage_ == AUX_M1 && !modeM1)
        // [Guía] Agrupa condiciones alternativas y conjuntas en la prueba en curso.
        || (managedStage_ == AUX_M3 && (!modeM3
          // [Guía] Añade una alternativa suficiente dentro de la condición compuesta.
          || dutyQ2 < Config::M3_AUX_DUTY_MIN || dutyQ2 > Config::M3_AUX_DUTY_MAX))) {
      // [Guía] Actualiza `managedStage_` con `AUX_RETIRE; managedRetireSince_ = managedStepSince_ = now`.
      managedStage_ = AUX_RETIRE; managedRetireSince_ = managedStepSince_ = now;
      // [Guía] Actualiza `managedRetireReason_` con `modeM3 ? "Q2_OUTSIDE_90_95" : "MODE_CHANGE"`.
      managedRetireReason_ = modeM3 ? "Q2_OUTSIDE_90_95" : "MODE_CHANGE";
    }
    // Una subetapa de recuperacion PI no es cambio de modo: mantener ON y
    // congelar demanda. La salida REAL de M1 sigue retirando la carga.
    // [Guía] Comprueba `supervisedM1 && managedStage_ == AUX_M1`. Si se cumple, ejecuta el bloque asociado.
    if (supervisedM1 && managedStage_ == AUX_M1
        // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
        && serviceM1Pause(bk, now, stableM1, bus <= Config::M1_AUX_FAST_ROLLBACK_V)) return;
    // No borrar el episodio/ACK/R por un valle de unos pocos barridos. Pausar
    // cualquier aumento de demanda y confirmar deficit antes de retirar.
    // [Guía] Comprueba `!busReady && active_ && managedStage_ != AUX_RETIRE`. Si se cumple, ejecuta el bloque
    // [Guía] asociado.
    if (!busReady && active_ && managedStage_ != AUX_RETIRE) {
      // [Guía] Actualiza `managedM3Saturated_` con `false`.
      managedM3Saturated_ = false;
      // [Guía] Comprueba `managedStage_ == AUX_M1 && modeM1`. Si se cumple, ejecuta el bloque asociado.
      if (managedStage_ == AUX_M1 && modeM1) {
        // [Guía] Coordina consignas de resistencia y carga en el orden en que las puede confirmar la B&K.
        serviceLoad(bk);
        // [Guía] Comprueba `inhibited_`. Si se cumple, ejecuta el bloque asociado.
        if (inhibited_) { releaseManagedLoad(bk, now, inhibitReason_); return; }
        // No hacer400->800->1600->OFF por19.5V. Esta pausa no renueva
        // el presupuesto de preparacion ni declara objetivo PV cumplido.
        // [Guía] Comprueba `bus >= Config::M1_AUX_BACKOFF_BUS_V`. Si se cumple, ejecuta `managedDeficitRunning_ =
        // [Guía] false`.
        if (bus >= Config::M1_AUX_BACKOFF_BUS_V) managedDeficitRunning_ = false;
        // [Guía] Comprueba `!managedDeficitRunning_`. Si se cumple, ejecuta el bloque asociado.
        else if (!managedDeficitRunning_) {
          // [Guía] Actualiza `managedDeficitRunning_` con `true; managedDeficitSince_ = now`.
          managedDeficitRunning_ = true; managedDeficitSince_ = now;
        }
        // [Guía] Comprueba `managedDeficitRunning_`. Si se cumple, ejecuta el bloque asociado.
        if (managedDeficitRunning_
            // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
            && uint32_t(now - managedDeficitSince_) >= Config::M1_AUX_BACKOFF_CONFIRM_MS
            // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
            && uint32_t(now - managedStepSince_) >= Config::M1_AUX_STEP_MS
            // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen
            // [Guía] resistencia administrada en Ω.
            && loadAcknowledged(bk) && resistance_ < Config::M1_AUX_START_OHM) {
          // [Guía] Invoca `setManagedResistance` con los argumentos de esta expresión.
          setManagedResistance(bk, fminf(Config::M1_AUX_START_OHM,
            // [Guía] Completa la declaración o expresión con `resistance_ * Config::M1_AUX_BACKOFF_FACTOR),
            // [Guía] now)`. Aquí intervienen resistencia administrada en Ω.
            resistance_ * Config::M1_AUX_BACKOFF_FACTOR), now);
          // [Guía] Actualiza `status_` con `"M1_AUX_RELIEVE_10_PERCENT"`.
          status_ = "M1_AUX_RELIEVE_10_PERCENT";
        // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
        } else status_ = "M1_AUX_HOLD_BUS_RECOVERY";
        // [Guía] Finaliza aquí la función y devuelve el control al llamador.
        return;
      }
      // [Guía] Comprueba `!managedDeficitRunning_`. Si se cumple, ejecuta el bloque asociado.
      if (!managedDeficitRunning_) { managedDeficitRunning_ = true; managedDeficitSince_ = now; }
      // [Guía] Comprueba `uint32_t(now - managedDeficitSince_) < Config::AUX_DEFICIT_CONFIRM_MS`. Si se cumple,
      // [Guía] ejecuta el bloque asociado.
      if (uint32_t(now - managedDeficitSince_) < Config::AUX_DEFICIT_CONFIRM_MS) {
        // [Guía] Actualiza `status_` con `"AUX_HOLD_BUS_DIP"; return`.
        status_ = "AUX_HOLD_BUS_DIP"; return;
      }
      // [Guía] Actualiza `managedStage_` con `AUX_RETIRE; managedRetireSince_ = managedStepSince_ = now`.
      managedStage_ = AUX_RETIRE; managedRetireSince_ = managedStepSince_ = now;
      // [Guía] Actualiza `managedRetireReason_` con `"BUS_DEFICIT_CONFIRMED"`.
      managedRetireReason_ = "BUS_DEFICIT_CONFIRMED";
    // [Guía] Comprueba `busReady`. Si se cumple, ejecuta `managedDeficitRunning_ = false`.
    } else if (busReady) managedDeficitRunning_ = false;
    // Retirar una carga heredada ANTES de iniciar ayuda en el nuevo modo.
    // [Guía] Comprueba `managedStage_ == AUX_RETIRE`. Si se cumple, ejecuta el bloque asociado.
    if (managedStage_ == AUX_RETIRE) { retireManagedLoad(bk, now); return; }
    // [Guía] Comprueba `!m1Allowed && !m3Allowed`. Si se cumple, ejecuta el bloque asociado.
    if (!m1Allowed && !m3Allowed) {
      // [Guía] Pide apagar la carga administrada, registra el motivo e inicia la espera que corresponda para
      // [Guía] volver a habilitarla.
      releaseManagedLoad(bk, now, !modeM1 && !modeM3 ? "OFF_MODE_BLOCKED"
        // [Guía] Define una de las dos salidas de la expresión condicional.
        : !busReady ? "OFF_BUS_DEFICIT"
        // [Guía] Define una de las dos salidas de la expresión condicional.
        : modeM1 ? (!m1Qualified ? "M1_WAIT_BUS_PI_STABLE" : "OFF_M1_PV_ALREADY_READY")
        // [Guía] Define una de las dos salidas de la expresión condicional.
        : "OFF_Q2_OUTSIDE_90_95");
      // [Guía] Finaliza aquí la función y devuelve el control al llamador.
      return;
    }
    // Cancelar cualquier ON manual recibido antes de completar la calificacion.
    // Una orden humana OFF, por su parte, enclava inhibited_ hasta reset seguro.
    // [Guía] Comprueba `!active_`. Si se cumple, ejecuta el bloque asociado.
    if (!active_) {
      // [Guía] Comprueba `bk.consumoSolicitado`. Si se cumple, ejecuta `releaseManagedLoad(bk, now,
      // [Guía] "OFF_WAIT_DUTY_CONFIRM")`.
      if (bk.consumoSolicitado) releaseManagedLoad(bk, now, "OFF_WAIT_DUTY_CONFIRM");
      // [Guía] Comprueba `managedOffRequested_ && (!bk.estadoConsumoValido || bk.consumoConfirmado)`. Si se
      // [Guía] cumple, ejecuta el bloque asociado.
      if (managedOffRequested_ && (!bk.estadoConsumoValido || bk.consumoConfirmado)) {
        // [Guía] Actualiza `recoveryRunning_` con `false; status_ = "WAIT_OFF_ACK"; return`.
        recoveryRunning_ = false; status_ = "WAIT_OFF_ACK"; return;
      }
      // [Guía] Comprueba `managedOffClock_ && uint32_t(now - managedOffSince_) < Config::M3_AUX_MIN_OFF_MS`. Si
      // [Guía] se cumple, ejecuta el bloque asociado.
      if (managedOffClock_ && uint32_t(now - managedOffSince_) < Config::M3_AUX_MIN_OFF_MS) {
        // [Guía] Actualiza `recoveryRunning_` con `false; status_ = "M3_AUX_MIN_OFF"; return`.
        recoveryRunning_ = false; status_ = "M3_AUX_MIN_OFF"; return;
      }
      // [Guía] Comprueba `!recoveryRunning_`. Si se cumple, ejecuta el bloque asociado.
      if (!recoveryRunning_) { recoveryRunning_ = true; recoverySince_ = now; }
      // M1 ya completo2s de bus regulado: no exigir otra ventana continua
      // que los mismos picos breves puedan reiniciar. M3 conserva sus200ms.
      // [Guía] Comprueba `!(requireStableM1 && m1Allowed)`. Si se cumple, ejecuta el bloque asociado.
      if (!(requireStableM1 && m1Allowed)
          // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
          && uint32_t(now - recoverySince_) < Config::M3_AUX_CONFIRM_MS) {
        // [Guía] Actualiza `status_` con `m1Allowed ? "M1_AUX_CONFIRM_PV" : "M3_AUX_CONFIRM_Q2"; return`.
        status_ = m1Allowed ? "M1_AUX_CONFIRM_PV" : "M3_AUX_CONFIRM_Q2"; return;
      }
      // [Guía] Actualiza `active_` con `true`.
      active_ = true;
      // [Guía] Actualiza `managedStage_` con `m1Allowed ? AUX_M1 : AUX_M3`.
      managedStage_ = m1Allowed ? AUX_M1 : AUX_M3;
      // [Guía] Actualiza `managedM1PreparationStarted_` con `false`.
      managedM1PreparationStarted_ = false;
      // [Guía] Invoca `clearM1Pause` con los argumentos de esta expresión.
      clearM1Pause();
      // [Guía] Actualiza `m1HistorySize_` con `0`.
      m1HistorySize_ = 0;
      // [Guía] Actualiza `m1ConfirmedOhm_` con `m1PreviousOhm_ = Config::M1_AUX_START_OHM`.
      m1ConfirmedOhm_ = m1PreviousOhm_ = Config::M1_AUX_START_OHM;
      // [Guía] Actualiza `managedM3Saturated_` con `false`.
      managedM3Saturated_ = false;
      // [Guía] Actualiza `cause_` con `m1Allowed ? "M1_PV_LOAD_RAMP" : "M3_Q2_SATURATION"`.
      cause_ = m1Allowed ? "M1_PV_LOAD_RAMP" : "M3_Q2_SATURATION";
      // [Guía] Actualiza `managedStart_` con `managedStepSince_ = now`.
      managedStart_ = managedStepSince_ = now;
      // [Guía] Actualiza `recoveryRunning_` con `false; managedOffRequested_ = false`.
      recoveryRunning_ = false; managedOffRequested_ = false;
      // [Guía] Actualiza `wasLinked_` con `bk.enlace`.
      wasLinked_ = bk.enlace;
      // [Guía] Actualiza `resistance_` con `m1Allowed ? Config::M1_AUX_START_OHM : Config::M3_AUX_EMULATOR_OHM`.
      // [Guía] Este dato representa resistencia administrada en Ω.
      resistance_ = m1Allowed ? Config::M1_AUX_START_OHM : Config::M3_AUX_EMULATOR_OHM;
      // [Guía] Actualiza `bk.habilitarEmulador` con `true`.
      bk.habilitarEmulador = true;
      // [Guía] Comprueba `!bk.configurarResistencia(resistance_)`. Si se cumple, ejecuta el bloque asociado. Aquí
      // [Guía] intervienen resistencia administrada en Ω.
      if (!bk.configurarResistencia(resistance_)) {
        // [Guía] Actualiza `inhibited_` con `true; inhibitReason_ = status_ = "INHIBIT_R_REJECTED"; return`.
        inhibited_ = true; inhibitReason_ = status_ = "INHIBIT_R_REJECTED"; return;
      }
      // [Guía] Actualiza `status_` con `"WAIT_R_ACK"`.
      status_ = "WAIT_R_ACK";
    }
    // [Guía] Coordina consignas de resistencia y carga en el orden en que las puede confirmar la B&K.
    serviceLoad(bk);
    // [Guía] Comprueba `inhibited_`. Si se cumple, ejecuta el bloque asociado.
    if (inhibited_) { releaseManagedLoad(bk, now, inhibitReason_); return; }
    // [Guía] Comprueba `managedStage_ == AUX_M1 && !inhibited_`. Si se cumple, ejecuta el bloque asociado.
    if (managedStage_ == AUX_M1 && !inhibited_) {
      // Una sola ventana nueva al entrar por primera vez al PI en este episodio.
      // El tiempo pasado adquiriendo rama no consume la preparacion posterior.
      // No renovar este reloj en cada muestra ni al oscilar entre subetapas.
      // [Guía] Comprueba `m1BusPiRegulating && !managedM1PreparationStarted_`. Si se cumple, ejecuta el bloque
      // [Guía] asociado.
      if (m1BusPiRegulating && !managedM1PreparationStarted_) {
        // [Guía] Actualiza `managedM1PreparationStarted_` con `true; managedStart_ = now`.
        managedM1PreparationStarted_ = true; managedStart_ = now;
      }
      // [Guía] Actualiza `limit` con `managedM1PreparationStarted_`.
      const uint32_t limit = managedM1PreparationStarted_
        // [Guía] Define una de las dos salidas de la expresión condicional.
        ? Config::M1_AUX_PREP_BUDGET_MS : Config::M1_AUX_BUDGET_MS;
      // [Guía] Actualiza `budget` con `uint32_t(now - managedStart_) < limit`.
      const bool budget = uint32_t(now - managedStart_) < limit;
      // [Guía] Actualiza `m1Adjusting_` con `!m1TargetReached && budget`.
      m1Adjusting_ = !m1TargetReached && budget
        // [Guía] Agrupa condiciones alternativas y conjuntas en la prueba en curso.
        && (!requireStableM1 || stableM1)
        // [Guía] Agrupa condiciones alternativas y conjuntas en la prueba en curso.
        && (!requireStableM1 || m1HistorySize_ < M1_HISTORY_CAPACITY)
        // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen resistencia
        // [Guía] administrada en Ω.
        && resistance_ > Config::M1_AUX_MIN_OHM;
      // [Guía] Comprueba `!loadAcknowledged(bk)`. Si se cumple, termina la función en esta rama.
      if (!loadAcknowledged(bk)) return;
      // [Guía] Comprueba `!m1Adjusting_`. Si se cumple, ejecuta el bloque asociado.
      if (!m1Adjusting_) {
        // Mostrar el objetivo cumplido antes que el presupuesto agotado: V48
        // mostraba BUDGET_HOLD con PV7V y ocultaba por que habia dejado de actuar.
        // [Guía] Actualiza `status_` con `m1TargetReached ? "M1_AUX_PV_TARGET_HOLD"`.
        status_ = m1TargetReached ? "M1_AUX_PV_TARGET_HOLD"
          // [Guía] Define una de las dos salidas de la expresión condicional.
          : !budget ? "M1_AUX_BUDGET_HOLD"
          // [Guía] Define una de las dos salidas de la expresión condicional.
          : requireStableM1 && m1HistorySize_ >= M1_HISTORY_CAPACITY ? "M1_AUX_HISTORY_FULL_HOLD"
          // [Guía] Define una de las dos salidas de la expresión condicional.
          : requireStableM1 && !stableM1 ? "M1_AUX_WAIT_BUS_20V" : "M1_AUX_MIN_R_HOLD";
      // [Guía] Comprueba `uint32_t(now - managedStepSince_) >= Config::M1_AUX_STEP_MS`. Si se cumple, ejecuta el
      // [Guía] bloque asociado.
      } else if (uint32_t(now - managedStepSince_) >= Config::M1_AUX_STEP_MS) {
        // [Guía] Actualiza `factor` con `requireStableM1 ? m1ResistanceFactor() : (m1BusPiRegulating`.
        const float factor = requireStableM1 ? m1ResistanceFactor() : (m1BusPiRegulating
          // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
          && Config::M1_AUX_PV_TARGET_V - pv <= Config::M1_AUX_PREP_FINE_BAND_V
          // [Guía] Define una de las dos salidas de la expresión condicional.
          ? Config::M1_AUX_PREP_FINE_FACTOR
          // [Guía] Define una de las dos salidas de la expresión condicional.
          : Config::M1_AUX_RESISTANCE_FACTOR);
        // [Guía] Invoca `setManagedResistance` con los argumentos de esta expresión.
        setManagedResistance(bk, fmaxf(Config::M1_AUX_MIN_OHM,
          // [Guía] Completa la declaración o expresión con `resistance_ * factor), now)`. Aquí intervienen
          // [Guía] resistencia administrada en Ω.
          resistance_ * factor), now);
        // [Guía] Actualiza `status_` con `m1BusPiRegulating ? "M1_AUX_PREPARE_PV_LOWER_R" : "M1_AUX_LOWER_R"`.
        status_ = m1BusPiRegulating ? "M1_AUX_PREPARE_PV_LOWER_R" : "M1_AUX_LOWER_R";
      // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
      } else status_ = "M1_AUX_WAIT_STEP";
    // [Guía] Comprueba `managedStage_ == AUX_M3 && loadAcknowledged(bk)`. Si se cumple, ejecuta el bloque
    // [Guía] asociado.
    } else if (managedStage_ == AUX_M3 && loadAcknowledged(bk)) {
      // Q2 al95% y bus alto significa que200ohm puede no bastar. Ampliar solo
      // dentro del permiso90..95%, con ACK, tiempo y limites de corriente.
      // [Guía] Actualiza `saturated` con `dutyQ2 >= Config::M3_AUX_RAMP_DUTY_MIN`.
      const bool saturated = dutyQ2 >= Config::M3_AUX_RAMP_DUTY_MIN
        // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
        && bus >= Config::M3_AUX_RAMP_BUS_MIN_V;
      // [Guía] Comprueba `!saturated`. Si se cumple, ejecuta `managedM3Saturated_ = false`.
      if (!saturated) managedM3Saturated_ = false;
      // [Guía] Comprueba `!managedM3Saturated_`. Si se cumple, ejecuta el bloque asociado.
      else if (!managedM3Saturated_) { managedM3Saturated_ = true; managedM3SaturatedSince_ = now; }
      // [Guía] Actualiza `budget` con `uint32_t(now - managedStart_) < Config::M3_AUX_BUDGET_MS`.
      const bool budget = uint32_t(now - managedStart_) < Config::M3_AUX_BUDGET_MS;
      // [Guía] Comprueba `saturated && uint32_t(now - managedM3SaturatedSince_) >= Config::M3_AUX_CONFIRM_MS`. Si
      // [Guía] se cumple, ejecuta el bloque asociado.
      if (saturated && uint32_t(now - managedM3SaturatedSince_) >= Config::M3_AUX_CONFIRM_MS
          // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen resistencia
          // [Guía] administrada en Ω.
          && budget && resistance_ > Config::M3_AUX_MIN_OHM
          // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
          && uint32_t(now - managedStepSince_) >= Config::M3_AUX_STEP_MS) {
        // [Guía] Invoca `setManagedResistance` con los argumentos de esta expresión.
        setManagedResistance(bk, fmaxf(Config::M3_AUX_MIN_OHM,
          // [Guía] Completa la declaración o expresión con `resistance_ * Config::M3_AUX_RESISTANCE_FACTOR),
          // [Guía] now)`. Aquí intervienen resistencia administrada en Ω.
          resistance_ * Config::M3_AUX_RESISTANCE_FACTOR), now);
        // [Guía] Actualiza `status_` con `"M3_AUX_LOWER_R"`.
        status_ = "M3_AUX_LOWER_R";
      // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
      } else status_ = !budget ? "M3_AUX_BUDGET_HOLD" : "LOAD_ORDER_CONFIRMED";
    }
  }
  // Se llama sin bloqueo. recoveryRequest permite a EMS pedir ayuda antes del
  // disparo de sobretension, pero tambien requiere tension/tiempo configurados.
  // [Guía] Gestiona el perfil independiente de carga auxiliar según tensión, permisos y confirmaciones del
  // [Guía] instrumento. Recibe `bk`, `bus`, `now`, `recoveryRequest`.
  void update(EmulatorController& bk, float bus, uint32_t now, bool recoveryRequest = false) {
    // [Guía] Comprueba `!isfinite(bus)`. Si se cumple, ejecuta el bloque asociado.
    if (!isfinite(bus)) {
      // [Guía] Actualiza `safeRunning_` con `safeToReset_ = false`.
      safeRunning_ = safeToReset_ = false;
      // [Guía] Actualiza `highRunning_` con `recoveryRunning_ = false`.
      highRunning_ = recoveryRunning_ = false;
      // [Guía] Actualiza `sampleSeen_` con `false`.
      sampleSeen_ = false;
      // [Guía] Comprueba `active_`. Si se cumple, ejecuta el bloque asociado.
      if (active_) { inhibited_ = true; status_ = "SENSOR_FAULT"; }
      // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
      else status_ = "WAIT_VALID_BUS";
      // [Guía] Finaliza aquí la función y devuelve el control al llamador.
      return;
    }
    // No contar un hueco de adquisicion como tension observada continuamente.
    // [Guía] Comprueba `sampleSeen_ && uint32_t(now - lastSample_) > Config::CONTROL_TIMEOUT_US / 1000`. Si se
    // [Guía] cumple, ejecuta el bloque asociado.
    if (sampleSeen_ && uint32_t(now - lastSample_) > Config::CONTROL_TIMEOUT_US / 1000) {
      // [Guía] Actualiza `highRunning_` con `recoveryRunning_ = safeRunning_ = safeToReset_ = false`.
      highRunning_ = recoveryRunning_ = safeRunning_ = safeToReset_ = false;
    }
    // [Guía] Actualiza `sampleSeen_` con `true`.
    sampleSeen_ = true;
    // [Guía] Actualiza `lastSample_` con `now`.
    lastSample_ = now;
    // Calificar por separado la condicion de rearme, incluso estando enclavado.
    // [Guía] Comprueba `bus < Config::BUS_OV_RESET_V`. Si se cumple, ejecuta el bloque asociado.
    if (bus < Config::BUS_OV_RESET_V) {
      // [Guía] Comprueba `!safeRunning_`. Si se cumple, ejecuta el bloque asociado.
      if (!safeRunning_) { safeRunning_ = true; safeSince_ = now; }
      // [Guía] Habilita el rearme cuando la tensión se mantuvo en la zona admitida durante el periodo
      // [Guía] configurado.
      safeToReset_ = uint32_t(now - safeSince_) >= Config::BUS_OV_RESET_MS;
    // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
    } else safeRunning_ = safeToReset_ = false;
    // [Guía] Comprueba `!active_`. Si se cumple, ejecuta el bloque asociado.
    if (!active_) {
      // Dos causas independientes: sobretension sostenida o peticion de EMS.
      // Cada bandera temporal se borra si deja de cumplirse su condicion.
      // [Guía] Actualiza `high` con `bus >= Config::BUS_OV_TRIGGER_V`.
      const bool high = bus >= Config::BUS_OV_TRIGGER_V;
      // [Guía] Actualiza `recover` con `recoveryRequest && bus >= Config::BUS_RECOVERY_LOAD_V`.
      const bool recover = recoveryRequest && bus >= Config::BUS_RECOVERY_LOAD_V;
      // [Guía] Comprueba `!high`. Si se cumple, ejecuta `highRunning_ = false`.
      if (!high) highRunning_ = false;
      // [Guía] Comprueba `!highRunning_`. Si se cumple, ejecuta el bloque asociado.
      else if (!highRunning_) { highRunning_ = true; highSince_ = now; }
      // [Guía] Comprueba `!recover`. Si se cumple, ejecuta `recoveryRunning_ = false`.
      if (!recover) recoveryRunning_ = false;
      // [Guía] Comprueba `!recoveryRunning_`. Si se cumple, ejecuta el bloque asociado.
      else if (!recoveryRunning_) { recoveryRunning_ = true; recoverySince_ = now; }
      // [Guía] Actualiza `confirmedHigh` con `highRunning_`.
      const bool confirmedHigh = highRunning_
        // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
        && uint32_t(now - highSince_) >= Config::BUS_OV_TRIGGER_MS;
      // [Guía] Actualiza `confirmedRecovery` con `recoveryRunning_`.
      const bool confirmedRecovery = recoveryRunning_
        // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
        && uint32_t(now - recoverySince_) >= Config::BUS_RECOVERY_LOAD_MS;
      // [Guía] Comprueba `!confirmedHigh && !confirmedRecovery`. Si se cumple, ejecuta el bloque asociado.
      if (!confirmedHigh && !confirmedRecovery) {
        // [Guía] Actualiza `status_` con `high ? "CONFIRM_OV_1S" : recover ? "CONFIRM_EMS_LOAD" : "ARMED"`.
        status_ = high ? "CONFIRM_OV_1S" : recover ? "CONFIRM_EMS_LOAD" : "ARMED";
        // [Guía] Finaliza aquí la función y devuelve el control al llamador.
        return;
      }
      // Confirmada una causa, registrar cual fue y fijar R para este episodio.
      // [Guía] Actualiza `active_` con `true`.
      active_ = true;
      // [Guía] Actualiza `cause_` con `confirmedHigh ? "BUS_OV" : "EMS_RECOVERY"`.
      cause_ = confirmedHigh ? "BUS_OV" : "EMS_RECOVERY";
      // [Guía] Actualiza `highRunning_` con `recoveryRunning_ = false`.
      highRunning_ = recoveryRunning_ = false;
      // [Guía] Actualiza `safeRunning_` con `safeToReset_ = false`.
      safeRunning_ = safeToReset_ = false;
      // [Guía] Actualiza `inhibited_` con `false`.
      inhibited_ = false;
      // [Guía] Actualiza `wasLinked_` con `bk.enlace`.
      wasLinked_ = bk.enlace;
      // [Guía] Actualiza `resistance_` con `Config::BUS_OV_EMULATOR_OHM`. Este dato representa resistencia
      // [Guía] administrada en Ω.
      resistance_ = Config::BUS_OV_EMULATOR_OHM;
      // Nunca disminuir consumo existente al intentar mitigar sobretension.
      // [Guía] Comprueba `(bk.estadoConsumoValido && bk.consumoConfirmado) || bk.consumoSolicitado`. Si se
      // [Guía] cumple, ejecuta el bloque asociado.
      if ((bk.estadoConsumoValido && bk.consumoConfirmado) || bk.consumoSolicitado) {
        // [Guía] Comprueba `bk.resistenciaConfirmada && bk.resistenciaConfirmadaOhm < resistance_`. Si se cumple,
        // [Guía] ejecuta el bloque asociado. Aquí intervienen resistencia administrada en Ω.
        if (bk.resistenciaConfirmada && bk.resistenciaConfirmadaOhm < resistance_)
          // [Guía] Actualiza `resistance_` con `bk.resistenciaConfirmadaOhm`. Este dato representa resistencia
          // [Guía] administrada en Ω.
          resistance_ = bk.resistenciaConfirmadaOhm;
        // [Guía] Comprueba `isfinite(bk.resistenciaOhm) && bk.resistenciaOhm >= EmulatorController::R_MIN`. Si se
        // [Guía] cumple, ejecuta el bloque asociado.
        if (isfinite(bk.resistenciaOhm) && bk.resistenciaOhm >= EmulatorController::R_MIN
            // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen
            // [Guía] resistencia administrada en Ω.
            && bk.resistenciaOhm < resistance_) resistance_ = bk.resistenciaOhm;
      }
      // [Guía] Actualiza `bk.habilitarEmulador` con `true`.
      bk.habilitarEmulador = true;
      // [Guía] Comprueba `!bk.configurarResistencia(resistance_)`. Si se cumple, ejecuta el bloque asociado. Aquí
      // [Guía] intervienen resistencia administrada en Ω.
      if (!bk.configurarResistencia(resistance_)) {
        // [Guía] Actualiza `inhibited_` con `true`.
        inhibited_ = true;
        // [Guía] Actualiza `status_` con `"R_REJECTED"`.
        status_ = "R_REJECTED";
      // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
      } else status_ = "WAIT_R_ACK";
    }
    // [Guía] Coordina consignas de resistencia y carga en el orden en que las puede confirmar la B&K.
    serviceLoad(bk);
  }
 // [Guía] Desde aquí se declaran los datos y auxiliares internos de la clase.
 private:
  // [Guía] Reúne bajo `PauseHold` las declaraciones que comparten este tipo o ámbito.
  struct PauseHold {
    // [Guía] Actualiza `running` con `false`.
    bool running = false;
    // [Guía] Actualiza `since` con `0`.
    uint32_t since = 0;
    // [Guía] Limpia el estado de este módulo.
    void clear() { running = false; }
    // [Guía] Conserva el instante inicial mientras la condición se cumple y comprueba si se alcanzó el plazo de
    // [Guía] confirmación. El resultado es una decisión booleana para el llamador. Recibe `condition`, `now`,
    // [Guía] `duration`.
    bool held(bool condition, uint32_t now, uint32_t duration) {
      // [Guía] Comprueba `!condition`. Si se cumple, ejecuta el bloque asociado.
      if (!condition) { clear(); return false; }
      // [Guía] Comprueba `!running`. Si se cumple, ejecuta el bloque asociado.
      if (!running) { running = true; since = now; }
      // [Guía] Entrega `uint32_t(now - since) >= duration` al llamador.
      return uint32_t(now - since) >= duration;
    }
  };
  // [Guía] Selecciona la proporción del siguiente descenso de resistencia según el tramo de la rampa M1.
  float m1ResistanceFactor() const {
    // [Guía] Comprueba `resistance_ > Config::M1_AUX_COARSE_UNTIL_OHM`. Si se cumple, termina la función en esta
    // [Guía] rama. Aquí intervienen resistencia administrada en Ω.
    if (resistance_ > Config::M1_AUX_COARSE_UNTIL_OHM) return Config::M1_AUX_COARSE_FACTOR;
    // [Guía] Comprueba `resistance_ > Config::M1_AUX_MIDDLE_UNTIL_OHM`. Si se cumple, termina la función en esta
    // [Guía] rama. Aquí intervienen resistencia administrada en Ω.
    if (resistance_ > Config::M1_AUX_MIDDLE_UNTIL_OHM) return Config::M1_AUX_MIDDLE_FACTOR;
    // [Guía] Entrega `Config::M1_AUX_FINAL_FACTOR` al llamador.
    return Config::M1_AUX_FINAL_FACTOR;
  }
  // [Guía] Limpia M1 pause.
  void clearM1Pause() {
    // [Guía] Actualiza `m1PauseRunning_` con `m1ResumeRunning_ = m1RollbackSent_ = m1InvalidRunning_ = false`.
    m1PauseRunning_ = m1ResumeRunning_ = m1RollbackSent_ = m1InvalidRunning_ = false;
    // [Guía] Actualiza `m1RollbackWaitRunning_` con `false`.
    m1RollbackWaitRunning_ = false;
    // [Guía] Borra la confirmación o acumulación en `m1Collapse_` para que no se arrastre al siguiente contexto.
    m1Collapse_.clear();
  }
  // [Guía] Marca el comienzo de la pausa M1 para dar al bus una oportunidad de recuperar estabilidad. Recibe
  // [Guía] `now`.
  void beginM1Pause(uint32_t now) {
    // [Guía] Comprueba `!m1PauseRunning_`. Si se cumple, ejecuta el bloque asociado.
    if (!m1PauseRunning_) { m1PauseRunning_ = true; m1PauseSince_ = now; }
  }
  // Historial LIFO de pasos realmente aceptados. No invertir porcentajes:
  // redondeo y cambio de tramo impedirian volver al mismo valor exacto.
  // Un ACK de retroceso consume un paso; una orden pendiente no lo consume.
  // [Guía] Incorpora al historial de pasos la resistencia cuya confirmación ya fue recibida. Recibe `bk`.
  void observeM1ResistanceAck(const EmulatorController& bk) {
    // [Guía] Comprueba `!resistanceAcknowledged(bk) || fabsf(bk.resistenciaConfirmadaOhm - m1ConfirmedOhm_) <
    // [Guía] 0.0011f`. Si se cumple, ejecuta el bloque asociado.
    if (!resistanceAcknowledged(bk) || fabsf(bk.resistenciaConfirmadaOhm - m1ConfirmedOhm_) < 0.0011f)
      // [Guía] Finaliza aquí la función y devuelve el control al llamador.
      return;
    // [Guía] Comprueba `bk.resistenciaConfirmadaOhm < m1ConfirmedOhm_ && !m1RollbackSent_`. Si se cumple, ejecuta
    // [Guía] el bloque asociado.
    if (bk.resistenciaConfirmadaOhm < m1ConfirmedOhm_ && !m1RollbackSent_) {
      // [Guía] Comprueba `m1HistorySize_ < M1_HISTORY_CAPACITY`. Si se cumple, ejecuta el bloque asociado.
      if (m1HistorySize_ < M1_HISTORY_CAPACITY)
        // [Guía] Incrementa `m1History_[m1HistorySize_` mediante `] = m1ConfirmedOhm_`.
        m1History_[m1HistorySize_++] = m1ConfirmedOhm_;
    // [Guía] Comprueba `m1RollbackSent_ && m1HistorySize_`. Si se cumple, ejecuta el bloque asociado.
    } else if (m1RollbackSent_ && m1HistorySize_
        // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
        && fabsf(bk.resistenciaConfirmadaOhm - m1History_[m1HistorySize_ - 1]) < 0.0011f) {
      // [Guía] Resta una unidad a `m1HistorySize_` para contabilizar el evento de esta rama.
      --m1HistorySize_;
    }
    // [Guía] Actualiza `m1ConfirmedOhm_` con `bk.resistenciaConfirmadaOhm`.
    m1ConfirmedOhm_ = bk.resistenciaConfirmadaOhm;
    // [Guía] Actualiza `m1PreviousOhm_` con `m1HistorySize_ ? m1History_[m1HistorySize_ - 1] : m1ConfirmedOhm_`.
    m1PreviousOhm_ = m1HistorySize_ ? m1History_[m1HistorySize_ - 1] : m1ConfirmedOhm_;
  }
  // [Guía] Espera recuperación del bus o retrocede en el historial de resistencias confirmadas si persiste la
  // [Guía] inestabilidad. El resultado es una decisión booleana para el llamador. Recibe `bk`, `now`, `stable`,
  // [Guía] `urgent`.
  bool serviceM1Pause(EmulatorController& bk, uint32_t now, bool stable, bool urgent) {
    // [Guía] Comprueba `!stable`. Si se cumple, ejecuta el bloque asociado.
    if (!stable) { beginM1Pause(now); m1ResumeRunning_ = false; }
    // [Guía] Comprueba `!m1PauseRunning_`. Si se cumple, termina la función en esta rama.
    if (!m1PauseRunning_) return false;
    // [Guía] Comprueba `stable`. Si se cumple, ejecuta el bloque asociado.
    if (stable) {
      // [Guía] Comprueba `!m1ResumeRunning_`. Si se cumple, ejecuta el bloque asociado.
      if (!m1ResumeRunning_) { m1ResumeRunning_ = true; m1ResumeSince_ = now; }
      // No reanudar con ACK de R/ON pendiente ni bajar otro paso en la misma
      // muestra: desde la recuperacion deben pasar otros800ms como minimo.
      // [Guía] Comprueba `uint32_t(now - m1ResumeSince_) >= Config::M1_AUX_BACKOFF_CONFIRM_MS`. Si se cumple,
      // [Guía] ejecuta el bloque asociado.
      if (uint32_t(now - m1ResumeSince_) >= Config::M1_AUX_BACKOFF_CONFIRM_MS
          // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
          && resistanceAcknowledged(bk)
          // [Guía] Agrupa condiciones alternativas y conjuntas en la prueba en curso.
          && (!bk.consumoSolicitado || loadAcknowledged(bk))) {
        // [Guía] Comprueba `m1RollbackSent_`. Si se cumple, ejecuta el bloque asociado.
        if (m1RollbackSent_) {
          // Mantener el ultimo paso que regula. Si vuelve a perder regulacion,
          // conceder otros3s desde esa perdida antes de regresar otro paso.
          // [Guía] Actualiza `m1RollbackWaitRunning_` con `true; m1RollbackWaitSince_ = now`.
          m1RollbackWaitRunning_ = true; m1RollbackWaitSince_ = now;
          // [Guía] Actualiza `status_` con `"M1_AUX_LIMIT_STEP_HOLD"; return true`.
          status_ = "M1_AUX_LIMIT_STEP_HOLD"; return true;
        }
        // [Guía] Invoca `clearM1Pause` con los argumentos de esta expresión.
        clearM1Pause(); managedStepSince_ = now;
        // [Guía] Actualiza `status_` con `"M1_AUX_RECOVERED_HOLD_STEP"; return true`.
        status_ = "M1_AUX_RECOVERED_HOLD_STEP"; return true;
      }
    }
    // [Guía] Actualiza `acknowledged` con `loadAcknowledged(bk)`.
    const bool acknowledged = loadAcknowledged(bk);
    // [Guía] Comprueba `m1RollbackSent_ && acknowledged && !m1RollbackWaitRunning_`. Si se cumple, ejecuta el
    // [Guía] bloque asociado.
    if (m1RollbackSent_ && acknowledged && !m1RollbackWaitRunning_) {
      // [Guía] Actualiza `m1RollbackWaitRunning_` con `true; m1RollbackWaitSince_ = now`.
      m1RollbackWaitRunning_ = true; m1RollbackWaitSince_ = now;
    }
    // [Guía] Actualiza `nextRollback` con `m1RollbackSent_`.
    const bool nextRollback = m1RollbackSent_
      // [Guía] Define una de las dos salidas de la expresión condicional.
      ? !stable && m1RollbackWaitRunning_
        // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
        && uint32_t(now - m1RollbackWaitSince_) >= Config::M1_AUX_ROLLBACK_WAIT_MS
      // [Guía] Define una de las dos salidas de la expresión condicional.
      : urgent || uint32_t(now - m1PauseSince_) >= Config::M1_AUX_UNSTABLE_WAIT_MS;
    // [Guía] Comprueba `nextRollback && acknowledged && m1HistorySize_`. Si se cumple, ejecuta el bloque
    // [Guía] asociado.
    if (nextRollback && acknowledged && m1HistorySize_
        // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen resistencia
        // [Guía] administrada en Ω.
        && m1PreviousOhm_ > resistance_ + 0.0011f) {
      // Primer retroceso:5s o valle urgente. Los siguientes SIEMPRE esperan
      // 3s desde el ACK, aun con valles urgentes repetidos. LOAD sigue ON.
      // [Guía] Actualiza `m1RollbackSent_` con `true`.
      m1RollbackSent_ = true;
      // [Guía] Actualiza `m1RollbackWaitRunning_` con `false`.
      m1RollbackWaitRunning_ = false;
      // [Guía] Actualiza `m1ResumeRunning_` con `false`.
      m1ResumeRunning_ = false;
      // [Guía] Invoca `setManagedResistance` con los argumentos de esta expresión.
      setManagedResistance(bk, m1PreviousOhm_, now);
      // [Guía] Actualiza `status_` con `"M1_AUX_ROLLBACK_PREVIOUS_STEP"; return true`.
      status_ = "M1_AUX_ROLLBACK_PREVIOUS_STEP"; return true;
    }
    // [Guía] Actualiza `status_` con `m1RollbackSent_ ? (!acknowledged ? "M1_AUX_ROLLBACK_WAIT_ACK"`.
    status_ = m1RollbackSent_ ? (!acknowledged ? "M1_AUX_ROLLBACK_WAIT_ACK"
        // [Guía] Define una de las dos salidas de la expresión condicional.
        : !m1HistorySize_ ? "M1_AUX_MAX_R_KEEP_ON" : "M1_AUX_ROLLBACK_WAIT_3S")
      // [Guía] Define una de las dos salidas de la expresión condicional.
      : stable ? "M1_AUX_CONFIRM_RECOVERY" : "M1_AUX_HOLD_STABILIZE_5S";
    // [Guía] Entrega `true` al llamador.
    return true;
  }
  // OFF prioritario cancela un ON pendiente y sus ACK tardios. No repetir OFF
  // en cada muestra: eso cancelaria tambien el ACK de la propia orden OFF.
  // [Guía] Pide apagar la carga administrada, registra el motivo e inicia la espera que corresponda para volver a
  // [Guía] habilitarla. Recibe `bk`, `now`, `reason`.
  void releaseManagedLoad(EmulatorController& bk, uint32_t now, const char* reason) {
    // [Guía] Comprueba `active_ || bk.consumoSolicitado`. Si se cumple, ejecuta el bloque asociado.
    if (active_ || bk.consumoSolicitado) {
      // [Guía] Actualiza `managedOffSince_` con `now; managedOffClock_ = true`.
      managedOffSince_ = now; managedOffClock_ = true;
      // [Guía] Actualiza `lastStopReason_` con `reason`.
      lastStopReason_ = reason;
    }
    // [Guía] Comprueba `active_ || bk.consumoSolicitado || (!managedOffRequested_`. Si se cumple, ejecuta el
    // [Guía] bloque asociado.
    if (active_ || bk.consumoSolicitado || (!managedOffRequested_
        // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
        && bk.estadoConsumoValido && bk.consumoConfirmado)) {
      // [Guía] Invoca `bk.solicitarConsumo` con los argumentos de esta expresión.
      bk.solicitarConsumo(false); managedOffRequested_ = true;
    }
    // [Guía] Actualiza `active_` con `false; cause_ = "NONE"`.
    active_ = false; cause_ = "NONE";
    // [Guía] Actualiza `managedStage_` con `AUX_IDLE; m1Adjusting_ = false`.
    managedStage_ = AUX_IDLE; m1Adjusting_ = false;
    // [Guía] Invoca `clearM1Pause` con los argumentos de esta expresión.
    clearM1Pause();
    // [Guía] Actualiza `managedDeficitRunning_` con `false`.
    managedDeficitRunning_ = false;
    // [Guía] Actualiza `managedM3Saturated_` con `false`.
    managedM3Saturated_ = false;
    // [Guía] Actualiza `recoveryRunning_` con `highRunning_ = false`.
    recoveryRunning_ = highRunning_ = false;
    // [Guía] Actualiza `status_` con `reason`.
    status_ = reason;
  }
  // [Guía] Expone `bk.enlace && bk.resistenciaConfirmada && !bk.enviarNuevaResistencia &&
  // [Guía] fabsf(bk.resistenciaConfirmadaOhm - resi…` al módulo que consulta resistance acknowledged. Recibe
  // [Guía] `bk`.
  bool resistanceAcknowledged(const EmulatorController& bk) const {
    // [Guía] Entrega `bk.enlace && bk.resistenciaConfirmada && !bk.enviarNuevaResistencia` al llamador.
    return bk.enlace && bk.resistenciaConfirmada && !bk.enviarNuevaResistencia
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen resistencia
      // [Guía] administrada en Ω.
      && fabsf(bk.resistenciaConfirmadaOhm - resistance_) < 0.0011f;
  }
  // [Guía] Expone `resistanceAcknowledged(bk) && bk.estadoConsumoValido && bk.consumoConfirmado &&
  // [Guía] bk.consumoSolicitado` al módulo que consulta load acknowledged. Recibe `bk`.
  bool loadAcknowledged(const EmulatorController& bk) const {
    // [Guía] Entrega `resistanceAcknowledged(bk) && bk.estadoConsumoValido` al llamador.
    return resistanceAcknowledged(bk) && bk.estadoConsumoValido
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
      && bk.consumoConfirmado && bk.consumoSolicitado;
  }
  // [Guía] Establece managed resistance. Recibe `bk`, `value`, `now`.
  void setManagedResistance(EmulatorController& bk, float value, uint32_t now) {
    // [Guía] Actualiza `resistance_` con `value; managedStepSince_ = now`. Este dato representa resistencia
    // [Guía] administrada en Ω.
    resistance_ = value; managedStepSince_ = now;
    // [Guía] Comprueba `!bk.configurarResistencia(value)`. Si se cumple, ejecuta el bloque asociado.
    if (!bk.configurarResistencia(value)) {
      // [Guía] Actualiza `inhibited_` con `true; inhibitReason_ = status_ = "INHIBIT_R_REJECTED"`.
      inhibited_ = true; inhibitReason_ = status_ = "INHIBIT_R_REJECTED";
    }
  }
  // [Guía] Aumenta la resistencia de forma progresiva y coordina el apagado de la carga auxiliar al completar el
  // [Guía] retiro. Recibe `bk`, `now`.
  void retireManagedLoad(EmulatorController& bk, uint32_t now) {
    // Sin ON aceptado no hay razon para encender solo para retirar: pedir OFF.
    // [Guía] Comprueba `!bk.consumoSolicitado || !bk.enlace`. Si se cumple, ejecuta el bloque asociado.
    if (!bk.consumoSolicitado || !bk.enlace
        // [Guía] Añade una alternativa suficiente dentro de la condición compuesta.
        || uint32_t(now - managedRetireSince_) >= Config::AUX_RETIRE_TIMEOUT_MS) {
      // [Guía] Pide apagar la carga administrada, registra el motivo e inicia la espera que corresponda para
      // [Guía] volver a habilitarla.
      releaseManagedLoad(bk, now, managedRetireReason_); return;
    }
    // [Guía] Coordina consignas de resistencia y carga en el orden en que las puede confirmar la B&K.
    serviceLoad(bk);
    // [Guía] Comprueba `inhibited_`. Si se cumple, ejecuta el bloque asociado.
    if (inhibited_) { releaseManagedLoad(bk, now, inhibitReason_); return; }
    // [Guía] Comprueba `!loadAcknowledged(bk)`. Si se cumple, ejecuta el bloque asociado.
    if (!loadAcknowledged(bk)) { status_ = "AUX_RETIRE_WAIT_ACK"; return; }
    // [Guía] Comprueba `resistance_ >= Config::M1_AUX_START_OHM - 0.001f`. Si se cumple, ejecuta el bloque
    // [Guía] asociado. Aquí intervienen resistencia administrada en Ω.
    if (resistance_ >= Config::M1_AUX_START_OHM - 0.001f) {
      // [Guía] Pide apagar la carga administrada, registra el motivo e inicia la espera que corresponda para
      // [Guía] volver a habilitarla.
      releaseManagedLoad(bk, now, managedRetireReason_); return;
    }
    // [Guía] Comprueba `uint32_t(now - managedStepSince_) >= Config::AUX_RETIRE_STEP_MS`. Si se cumple, ejecuta
    // [Guía] el bloque asociado.
    if (uint32_t(now - managedStepSince_) >= Config::AUX_RETIRE_STEP_MS) {
      // [Guía] Invoca `setManagedResistance` con los argumentos de esta expresión.
      setManagedResistance(bk, fminf(Config::M1_AUX_START_OHM,
        // [Guía] Completa la declaración o expresión con `resistance_ * Config::AUX_RETIRE_FACTOR), now)`. Aquí
        // [Guía] intervienen resistencia administrada en Ω.
        resistance_ * Config::AUX_RETIRE_FACTOR), now);
      // [Guía] Actualiza `status_` con `"AUX_RETIRE_RAISE_R"`.
      status_ = "AUX_RETIRE_RAISE_R";
    // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
    } else status_ = "AUX_RETIRE_WAIT_100MS";
  }
  // [Guía] Coordina consignas de resistencia y carga en el orden en que las puede confirmar la B&K. Recibe `bk`.
  void serviceLoad(EmulatorController& bk) {
    // Secuencia posterior: enlace -> resistencia confirmada -> LOAD confirmado.
    // Cada return cede CPU al PI/adquisicion mientras la B&K responde.
    // [Guía] Comprueba `!active_ || inhibited_`. Si se cumple, termina la función en esta rama.
    if (!active_ || inhibited_) return;
    // [Guía] Comprueba `(wasLinked_ && !bk.enlace) || !strcmp(bk.estado, )`. Si se cumple, ejecuta el bloque
    // [Guía] asociado.
    if ((wasLinked_ && !bk.enlace) || !strcmp(bk.estado, "ERROR_BK")
        // [Guía] Añade una alternativa suficiente dentro de la condición compuesta.
        || !strcmp(bk.estado, "ACK_TIMEOUT") || !strcmp(bk.estado, "TX_ERROR")) {
      // [Guía] Actualiza `inhibited_` con `true`.
      inhibited_ = true;
      // [Guía] Actualiza `inhibitReason_` con `!bk.enlace ? "INHIBIT_LINK_LOST"`.
      inhibitReason_ = !bk.enlace ? "INHIBIT_LINK_LOST"
        // [Guía] Define una de las dos salidas de la expresión condicional.
        : !strcmp(bk.estado, "ACK_TIMEOUT") ? "INHIBIT_ACK_TIMEOUT"
        // [Guía] Define una de las dos salidas de la expresión condicional.
        : !strcmp(bk.estado, "TX_ERROR") ? "INHIBIT_TX_ERROR" : "INHIBIT_ERROR_BK";
      // [Guía] Actualiza `status_` con `inhibitReason_`.
      status_ = inhibitReason_;
      // [Guía] Finaliza aquí la función y devuelve el control al llamador.
      return;
    }
    // [Guía] Comprueba `!bk.enlace`. Si se cumple, ejecuta el bloque asociado.
    if (!bk.enlace) { status_ = "WAIT_PC"; return; }
    // [Guía] Actualiza `wasLinked_` con `true`.
    wasLinked_ = true;
    // [Guía] Comprueba `!bk.resistenciaConfirmada || bk.enviarNuevaResistencia`. Si se cumple, ejecuta el bloque
    // [Guía] asociado.
    if (!bk.resistenciaConfirmada || bk.enviarNuevaResistencia
        // [Guía] Añade una alternativa suficiente dentro de la condición compuesta. Aquí intervienen resistencia
        // [Guía] administrada en Ω.
        || fabsf(bk.resistenciaConfirmadaOhm - resistance_) >= 0.0011f) {
      // [Guía] Actualiza `status_` con `"WAIT_R_ACK"`.
      status_ = "WAIT_R_ACK";
      // [Guía] Finaliza aquí la función y devuelve el control al llamador.
      return;
    }
    // [Guía] Comprueba `bk.estadoConsumoValido && bk.consumoConfirmado && bk.consumoSolicitado`. Si se cumple,
    // [Guía] ejecuta el bloque asociado.
    if (bk.estadoConsumoValido && bk.consumoConfirmado && bk.consumoSolicitado) {
      // [Guía] Actualiza `status_` con `"LOAD_ORDER_CONFIRMED"`.
      status_ = "LOAD_ORDER_CONFIRMED";
      // [Guía] Finaliza aquí la función y devuelve el control al llamador.
      return;
    }
    // [Guía] Comprueba `!bk.consumoSolicitado`. Si se cumple, ejecuta `bk.solicitarConsumo(true)`.
    if (!bk.consumoSolicitado) bk.solicitarConsumo(true);
    // [Guía] Actualiza `status_` con `"WAIT_ON_ACK"`.
    status_ = "WAIT_ON_ACK";
  }
  // [Guía] Actualiza `managedMode_` con `false, managedOffRequested_ = false, managedOffClock_ = false`.
  bool managedMode_ = false, managedOffRequested_ = false, managedOffClock_ = false;
  // [Guía] Reúne bajo `ManagedStage` las declaraciones que comparten este tipo o ámbito.
  enum ManagedStage { AUX_IDLE, AUX_M1, AUX_M3, AUX_RETIRE };
  // [Guía] Actualiza `managedStage_` con `AUX_IDLE`.
  ManagedStage managedStage_ = AUX_IDLE;
  // [Guía] Actualiza `m1Adjusting_` con `false`.
  bool m1Adjusting_ = false;
  // [Guía] Actualiza `managedM1PreparationStarted_` con `false`.
  bool managedM1PreparationStarted_ = false;
  // [Guía] Actualiza `m1StableRunning_` con `false, m1UnstableRunning_ = false`.
  bool m1StableRunning_ = false, m1UnstableRunning_ = false;
  // [Guía] Actualiza `m1StableAccumMs_` con `0, m1UnstableSince_ = 0`.
  uint32_t m1StableAccumMs_ = 0, m1UnstableSince_ = 0;
  // [Guía] Actualiza `m1PauseRunning_` con `false, m1ResumeRunning_ = false`.
  bool m1PauseRunning_ = false, m1ResumeRunning_ = false;
  // [Guía] Declara `m1Collapse_` para conservar m1 collapse en este ámbito.
  PauseHold m1Collapse_;
  // [Guía] Actualiza `m1RollbackSent_` con `false, m1InvalidRunning_ = false`.
  bool m1RollbackSent_ = false, m1InvalidRunning_ = false;
  // [Guía] Actualiza `m1PauseSince_` con `0, m1ResumeSince_ = 0, m1InvalidSince_ = 0`.
  uint32_t m1PauseSince_ = 0, m1ResumeSince_ = 0, m1InvalidSince_ = 0;
  // [Guía] Actualiza `m1ConfirmedOhm_` con `Config::M1_AUX_START_OHM`.
  float m1ConfirmedOhm_ = Config::M1_AUX_START_OHM;
  // [Guía] Actualiza `m1PreviousOhm_` con `Config::M1_AUX_START_OHM`.
  float m1PreviousOhm_ = Config::M1_AUX_START_OHM;
  // Cabe toda la rampa4000->30ohm actual; si se llena, no reducir mas R.
  // [Guía] Completa la declaración o expresión con `static constexpr unsigned M1_HISTORY_CAPACITY = 96`.
  static constexpr unsigned M1_HISTORY_CAPACITY = 96;
  // [Guía] Actualiza `m1History_[M1_HISTORY_CAPACITY]` con `{}`.
  float m1History_[M1_HISTORY_CAPACITY] = {};
  // [Guía] Actualiza `m1HistorySize_` con `0`.
  unsigned m1HistorySize_ = 0;
  // [Guía] Actualiza `m1RollbackWaitRunning_` con `false`.
  bool m1RollbackWaitRunning_ = false;
  // [Guía] Actualiza `m1RollbackWaitSince_` con `0`.
  uint32_t m1RollbackWaitSince_ = 0;
  // [Guía] Actualiza `managedDeficitRunning_` con `false`.
  bool managedDeficitRunning_ = false;
  // [Guía] Actualiza `managedM3Saturated_` con `false`.
  bool managedM3Saturated_ = false;
  // [Guía] Actualiza `managedM3SaturatedSince_` con `0`.
  uint32_t managedM3SaturatedSince_ = 0;
  // [Guía] Actualiza `managedDeficitSince_` con `0`.
  uint32_t managedDeficitSince_ = 0;
  // [Guía] Actualiza `managedRetireReason_` con `"MODE_CHANGE"`.
  const char* managedRetireReason_ = "MODE_CHANGE";
  // [Guía] Actualiza `inhibitReason_` con `"INHIBIT_LEGACY_FAULT"`.
  const char* inhibitReason_ = "INHIBIT_LEGACY_FAULT";
  // [Guía] Actualiza `lastStopReason_` con `"NONE"`.
  const char* lastStopReason_ = "NONE";
  // [Guía] Actualiza `managedStart_` con `0, managedStepSince_ = 0, managedRetireSince_ = 0`.
  uint32_t managedStart_ = 0, managedStepSince_ = 0, managedRetireSince_ = 0;
  // [Guía] Actualiza `managedOffSince_` con `0`.
  uint32_t managedOffSince_ = 0;
  // [Guía] Actualiza `active_` con `false, inhibited_ = false, wasLinked_ = false`.
  bool active_ = false, inhibited_ = false, wasLinked_ = false; // Enclavamiento, bloqueo y enlace previo.
  // [Guía] Actualiza `safeRunning_` con `false, safeToReset_ = false`.
  bool safeRunning_ = false, safeToReset_ = false;
  // [Guía] Actualiza `highRunning_` con `false, recoveryRunning_ = false, sampleSeen_ = false`.
  bool highRunning_ = false, recoveryRunning_ = false, sampleSeen_ = false;
  // [Guía] Actualiza `highSince_` con `0, lastSample_ = 0`.
  uint32_t highSince_ = 0, lastSample_ = 0;
  // [Guía] Actualiza `recoverySince_` con `0`.
  uint32_t recoverySince_ = 0;
  // [Guía] Actualiza `safeSince_` con `0`.
  uint32_t safeSince_ = 0;
  // [Guía] Actualiza `resistance_` con `Config::BUS_OV_EMULATOR_OHM`. Este dato representa resistencia
  // [Guía] administrada en Ω.
  float resistance_ = Config::BUS_OV_EMULATOR_OHM;
  // [Guía] Actualiza `status_` con `"ARMED"`.
  const char* status_ = "ARMED";
  // [Guía] Actualiza `cause_` con `"NONE"`.
  const char* cause_ = "NONE";
};
