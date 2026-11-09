// [Guía] Une el P&O con un PI de tensión fotovoltaica. P&O elige una referencia de Vpv y el PI ajusta Q1 para
// [Guía] alcanzarla.
// [Guía] Evita procesar este encabezado dos veces en una misma unidad de compilación.
#pragma once
// Un propietario de Q1: PI de Vpv con referencia inicial18.4V; en seguimiento,
// P&O modifica esa referencia. Q2 puede cambiar su consumo sin dejar Q1 fijo.
// Esta clase NO escribe PWM, NO regula Vbus y NO sustituye las protecciones EMS.
// [Guía] Incorpora las declaraciones de "Config.h" antes de usar sus tipos y funciones.
#include "Config.h"
// [Guía] Incorpora las declaraciones de "PIController.h" antes de usar sus tipos y funciones.
#include "PIController.h"
// [Guía] Incorpora las declaraciones de "MPPTController.h" antes de usar sus tipos y funciones.
#include "MPPTController.h"

// [Guía] Reúne bajo `PvVoltageMpptController` las declaraciones que comparten este tipo o ámbito.
class PvVoltageMpptController {
 // [Guía] Desde aquí se declaran los tipos y operaciones que usa el llamador.
 public:
  // [Guía] Construye PvVoltageMpptController con la configuración inicial indicada.
  PvVoltageMpptController()
    // [Guía] Define una de las dos salidas de la expresión condicional.
    : voltagePi_(Config::PV_REFERENCE_KP, Config::PV_REFERENCE_KI,
        // [Guía] Aporta `Config::Q1_DUTY_MIN, Config::Q1_DUTY_MAX), search_(0, 1) {` como parte de los parámetros
        // [Guía] de la operación.
        Config::Q1_DUTY_MIN, Config::Q1_DUTY_MAX), search_(0, 1) {
    // [Guía] Invoca `search_.configure` con los argumentos de esta expresión.
    search_.configure(Config::PV_MPPT_SETTLE_US, Config::PV_MPPT_AVERAGE_US,
      // [Guía] Completa la declaración o expresión con `Config::PV_MPPT_INITIAL_STEP_V / span(),
      // [Guía] Config::PV_MPPT_MIN_STEP_V / span()`.
      Config::PV_MPPT_INITIAL_STEP_V / span(), Config::PV_MPPT_MIN_STEP_V / span(),
      // [Guía] Completa la declaración o expresión con `Config::PV_MPPT_MAX_STEP_V / span())`.
      Config::PV_MPPT_MAX_STEP_V / span());
  }

  // Transferir desde la salida aplicada; ningun salto instantaneo de duty.
  // No heredar la integral del PI de BUS de M1, cuyo error tiene otra variable.
  // [Guía] Precarga el PI de Vpv con el duty aplicado y acota la referencia inicial. Recibe `appliedDuty`,
  // [Guía] `reference`, `now`.
  void begin(float appliedDuty, float reference, uint32_t now) {
    // [Guía] Acota la referencia inicial de Vpv en voltios al intervalo permitido para la búsqueda.
    reference_ = clampReference(reference);
    // [Guía] Reinicia `voltagePi_` con el estado inicial dado en los argumentos. Aquí intervienen indicación de
    // [Guía] integración o seguimiento activo.
    voltagePi_.reset(0, appliedDuty); tracking_ = boundRunning_ = false;
    // [Guía] Actualiza `seekingReference_` con `true; search_.begin(position(reference_), now)`.
    seekingReference_ = true; search_.begin(position(reference_), now);
  }
  // [Guía] Mantiene la referencia de tensión PV mediante el PI interno. Recibe `filteredPv`, `dt`.
  float hold(float filteredPv, float dt) {
    // Signo observado: subir duty reduce Vpv. Error positivo pide mas duty.
    // [Guía] Entrega `voltagePi_.update(filteredPv - reference_, dt,` al llamador. Aquí intervienen referencia
    // [Guía] del regulador y intervalo efectivo del control en s.
    return voltagePi_.update(filteredPv - reference_, dt,
      // [Guía] Invoca `fminf` con los argumentos de esta expresión.
      fminf(Config::PV_REFERENCE_SLEW_PER_SECOND,
            // [Guía] Aporta `Config::Q1_TEST_SLEW_PER_SECOND) * dt` como parte de los parámetros de la operación.
            // [Guía] Aquí intervienen intervalo efectivo del control en s.
            Config::Q1_TEST_SLEW_PER_SECOND) * dt);
  }
  // [Guía] Convierte la referencia en posición normalizada e inicia el P&O desde ese punto. Recibe `now`.
  void startTracking(uint32_t now) {
    // [Guía] Comprueba `tracking_`. Si se cumple, termina la función en esta rama. Aquí intervienen indicación de
    // [Guía] integración o seguimiento activo.
    if (tracking_) return;
    // [Guía] Actualiza `tracking_` con `true`. Este dato representa indicación de integración o seguimiento
    // [Guía] activo.
    tracking_ = true;
    // Una sola base inicial al entrar M3. El PI de Vpv conserva su integral.
    // [Guía] Prepara `search_` para empezar desde las medidas o consignas indicadas. Aquí intervienen referencia
    // [Guía] del regulador.
    search_.begin(position(reference_), now);
  }
  // [Guía] Señala un cambio de contexto para que la siguiente comparación de potencia use un punto de partida
  // [Guía] actualizado. Recibe `now`.
  void changedLoad(uint32_t now) {
    // [Guía] Comprueba `tracking_`. Si se cumple, ejecuta `search_.rebase(position(reference_), now)`. Aquí
    // [Guía] intervienen indicación de integración o seguimiento activo.
    if (tracking_) search_.rebase(position(reference_), now);
  }
  // [Guía] Alimenta el P&O con Vpv e Ipv instantáneos. Cuando cambia la posición elegida actualiza la referencia
  // [Guía] de Vpv y el PI calcula el duty. Recibe `filteredPv`, `rawPv`, `current`, `dt`, `now`.
  float track(float filteredPv, float rawPv, float current, float dt, uint32_t now) {
    // [Guía] Convierte la referencia en posición normalizada e inicia el P&O desde ese punto.
    startTracking(now);
    // [Guía] Actualiza `command` con `hold(filteredPv, dt)`.
    const float command = hold(filteredPv, dt);
    // [Guía] Actualiza `seekingReference_` con `fabsf(filteredPv - reference_) >
    // [Guía] Config::PV_REFERENCE_TRACK_BAND_V`.
    seekingReference_ = fabsf(filteredPv - reference_) > Config::PV_REFERENCE_TRACK_BAND_V;
    // Una referencia inalcanzable no debe congelar para siempre el buscador.
    // Tras saturacion confirmada, usar el punto observado y explorar hacia
    // dentro del margen disponible. No ampliar duty ni fingir que es el MPP.
    // [Guía] Actualiza `upperVoltageBound` con `command <= Config::Q1_DUTY_MIN +
    // [Guía] Config::PV_REFERENCE_BOUND_DUTY_BAND`.
    const bool upperVoltageBound = command <= Config::Q1_DUTY_MIN + Config::PV_REFERENCE_BOUND_DUTY_BAND
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen referencia del
      // [Guía] regulador.
      && filteredPv < reference_ - Config::PV_REFERENCE_TRACK_BAND_V;
    // [Guía] Actualiza `lowerVoltageBound` con `command >= Config::Q1_DUTY_MAX -
    // [Guía] Config::PV_REFERENCE_BOUND_DUTY_BAND`.
    const bool lowerVoltageBound = command >= Config::Q1_DUTY_MAX - Config::PV_REFERENCE_BOUND_DUTY_BAND
      // [Guía] Añade un requisito conjunto a la condición de la línea anterior. Aquí intervienen referencia del
      // [Guía] regulador.
      && filteredPv > reference_ + Config::PV_REFERENCE_TRACK_BAND_V;
    // [Guía] Comprueba `upperVoltageBound || lowerVoltageBound`. Si se cumple, ejecuta el bloque asociado.
    if (upperVoltageBound || lowerVoltageBound) {
      // [Guía] Comprueba `!boundRunning_`. Si se cumple, ejecuta el bloque asociado.
      if (!boundRunning_) { boundRunning_ = true; boundSince_ = now; }
      // [Guía] Comprueba `uint32_t(now - boundSince_) >= Config::PV_REFERENCE_BOUND_US`. Si se cumple, ejecuta el
      // [Guía] bloque asociado.
      if (uint32_t(now - boundSince_) >= Config::PV_REFERENCE_BOUND_US) {
        // Dar una propuesta hacia dentro del margen alcanzable, en lugar de
        // volver a pedir la misma tension limite y gastar otra ventana sin accion.
        // [Guía] Actualiza `reference_` con `clampReference(filteredPv + (upperVoltageBound ? -1 : 1)`. Este dato
        // [Guía] representa referencia del regulador.
        reference_ = clampReference(filteredPv + (upperVoltageBound ? -1 : 1)
          // [Guía] Completa la declaración o expresión con `* Config::PV_MPPT_INITIAL_STEP_V)`.
          * Config::PV_MPPT_INITIAL_STEP_V);
        // [Guía] Invoca `search_.rebase` con los argumentos de esta expresión. Aquí intervienen referencia del
        // [Guía] regulador.
        search_.rebase(position(reference_), now, upperVoltageBound ? 1 : -1);
        // [Guía] Actualiza `boundRunning_` con `false`.
        boundRunning_ = false;
        // [Guía] Actualiza `seekingReference_` con `fabsf(filteredPv - reference_) >
        // [Guía] Config::PV_REFERENCE_TRACK_BAND_V`.
        seekingReference_ = fabsf(filteredPv - reference_) > Config::PV_REFERENCE_TRACK_BAND_V;
      }
    // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
    } else boundRunning_ = false;
    // PI activo en TODOS los pasos. P&O no compara potencia de una referencia
    // que aun se esta alcanzando: conservar tiempos/ventana ante pausas breves.
    // Esto no desasigna Q1 ni baja MPPT a0; no significa nuevas propuestas
    // cada milisegundo. Perfil V62:100ms de asentamiento y300ms de media.
    // [Guía] Comprueba `seekingReference_`. Si se cumple, ejecuta `search_.pause(now)`.
    if (seekingReference_) search_.pause(now);
    // [Guía] Usa la pareja instantánea de Vpv e Ipv para comparar potencia. Cambia la referencia de tensión solo
    // [Guía] al recibir una nueva posición de búsqueda.
    else if (search_.sample(rawPv, current, now))
      // [Guía] Convierte a voltios la posición normalizada elegida por el P&O. El PI convierte luego el error
      // [Guía] respecto a esa referencia en duty de Q1.
      reference_ = voltage(search_.duty());
    // [Guía] Entrega `command` al llamador.
    return command;
  }
  // [Guía] Devuelve referencia del regulador.
  float reference() const { return reference_; }
  // [Guía] Expone `search_.step() * span()` al módulo que consulta paso tensión.
  float stepVolts() const { return search_.step() * span(); }
  // [Guía] Devuelve indicación de integración o seguimiento activo.
  bool tracking() const { return tracking_; }
  // [Guía] Expone `!tracking_ ? "PV_REFERENCE_PREPARE" : seekingReference_ ? "TRACK_VPV_REFERENCE" :
  // [Guía] search_.decision()` al módulo que consulta decision.
  const char* decision() const {
    // [Guía] Entrega `!tracking_ ? "PV_REFERENCE_PREPARE"` al llamador. Aquí intervienen indicación de
    // [Guía] integración o seguimiento activo.
    return !tracking_ ? "PV_REFERENCE_PREPARE"
      // [Guía] Define una de las dos salidas de la expresión condicional.
      : seekingReference_ ? "TRACK_VPV_REFERENCE" : search_.decision();
  }
  // [Guía] Expone `search_` al módulo que consulta search.
  const MPPTController& search() const { return search_; }

 // [Guía] Desde aquí se declaran los datos y auxiliares internos de la clase.
 private:
  // [Guía] Expone `Config::PV_REFERENCE_MAX_V - Config::PV_REFERENCE_MIN_V` al módulo que consulta span.
  static float span() { return Config::PV_REFERENCE_MAX_V - Config::PV_REFERENCE_MIN_V; }
  // [Guía] Expone `fminf(Config::PV_REFERENCE_MAX_V, fmaxf(Config::PV_REFERENCE_MIN_V, v))` al módulo que
  // [Guía] consulta clamp referencia. Recibe `v`.
  static float clampReference(float v) {
    // [Guía] Entrega `fminf(Config::PV_REFERENCE_MAX_V, fmaxf(Config::PV_REFERENCE_MIN_V, v))` al llamador.
    return fminf(Config::PV_REFERENCE_MAX_V, fmaxf(Config::PV_REFERENCE_MIN_V, v));
  }
  // Coordenada virtual0..1, NO duty fisico: bajar posicion sube Vpv_ref.
  // Asi se reutiliza P&O existente, incluida su direccion, ventana y ruido.
  // [Guía] Expone `(Config::PV_REFERENCE_MAX_V - v) / span()` al módulo que consulta position. Recibe `v`.
  static float position(float v) { return (Config::PV_REFERENCE_MAX_V - v) / span(); }
  // [Guía] Expone `Config::PV_REFERENCE_MAX_V - p * span()` al módulo que consulta tensión. Recibe `p`.
  static float voltage(float p) { return Config::PV_REFERENCE_MAX_V - p * span(); }
  // [Guía] Declara `voltagePi_` para conservar tensión pi en este ámbito.
  PIController voltagePi_;
  // [Guía] Declara `search_` para conservar search en este ámbito.
  MPPTController search_;
  // [Guía] Actualiza `reference_` con `Config::HOTPLUG_PV_TARGET_V`. Este dato representa referencia del
  // [Guía] regulador.
  float reference_ = Config::HOTPLUG_PV_TARGET_V;
  // [Guía] Actualiza `tracking_` con `false, seekingReference_ = false, boundRunning_ = false`. Este dato
  // [Guía] representa indicación de integración o seguimiento activo.
  bool tracking_ = false, seekingReference_ = false, boundRunning_ = false;
  // [Guía] Actualiza `boundSince_` con `0`.
  uint32_t boundSince_ = 0;
};
