// [Guía] Controla en M2 la corriente que entra a la batería por Q2. El lazo maneja una magnitud positiva
// [Guía] internamente y la referencia pública conserva el signo físico de Ibat.
// [Guía] Evita procesar este encabezado dos veces en una misma unidad de compilación.
#pragma once
// M2 posee SOLO Q2. PI en amperios: e=I_carga_ref-(-Ibat).
// No regula bus, no ejecuta MPPT y no conoce pines ni decide transiciones EMS.
// [Guía] Incorpora las declaraciones de "Config.h" antes de usar sus tipos y funciones.
#include "Config.h"
// [Guía] Incorpora las declaraciones de "PIController.h" antes de usar sus tipos y funciones.
#include "PIController.h"

// [Guía] Reúne bajo `M2CurrentController` las declaraciones que comparten este tipo o ámbito.
class M2CurrentController {
 // [Guía] Desde aquí se declaran los tipos y operaciones que usa el llamador.
 public:
  // [Guía] Construye M2CurrentController con la configuración inicial indicada.
  M2CurrentController() : pi_(Config::M2_CURRENT_KP, Config::M2_CURRENT_KI,
      // [Guía] Aporta `Config::Q2_DUTY_MIN, Config::Q2_DUTY_MAX) {}` como parte de los parámetros de la
      // [Guía] operación.
      Config::Q2_DUTY_MIN, Config::Q2_DUTY_MAX) {}

  // [Guía] Inicializa la rampa de corriente y precarga el PI de Q2 a partir de la salida aplicada y de Ibat.
  // [Guía] Recibe `appliedDuty`, `ibat`.
  void begin(float appliedDuty, float ibat) {
    // [Guía] Actualiza `duty_` con `fminf(Config::Q2_DUTY_MAX, fmaxf(0, appliedDuty))`.
    duty_ = fminf(Config::Q2_DUTY_MAX, fmaxf(0, appliedDuty));
    // Transferir desde la corriente real, sin escalon de referencia ni salida.
    // [Guía] Inicia la referencia interna en la magnitud de carga que ya circula, limitada entre cero y el
    // [Guía] objetivo de M2.
    target_ = fminf(Config::M2_CHARGE_CURRENT_A, fmaxf(0, -ibat));
    // [Guía] Precarga el PI desde el duty de entrada. La suma equivale a referencia menos corriente de carga
    // [Guía] medida, dado que Ibat es negativa al cargar.
    pi_.reset(target_ + ibat, duty_);
    // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
    reason_ = "M2_CURRENT_RAMP";
  }
  // M2 no compara Vpv con Vbat ni estima margen mediante Q2*Vpv.
  // La EMS verifica presencia/validez PV y autoriza la carga; este bloque
  // regula exclusivamente Ibat con rampa, limites de duty y techo Vbat.
  // [Guía] Avanza hacia la corriente de carga nominal con límite de pendiente. Reduce la solicitud si las medidas
  // [Guía] indican que se debe aliviar la carga. Recibe `ibat`, `vbat`, `dt`.
  float update(float ibat, float vbat, float dt) {
    // [Guía] Comprueba `!isfinite(ibat) || !isfinite(vbat) || !isfinite(dt) || dt <= 0`. Si se cumple, ejecuta el
    // [Guía] bloque asociado. Aquí intervienen intervalo efectivo del control en s.
    if (!isfinite(ibat) || !isfinite(vbat) || !isfinite(dt) || dt <= 0) {
      // [Guía] Actualiza `duty_` con `target_ = 0; reason_ = "M2_CURRENT_INVALID"; return duty_`.
      duty_ = target_ = 0; reason_ = "M2_CURRENT_INVALID"; return duty_;
    }
    // [Guía] Comprueba `vbat >= Config::BAT_CHARGE_MAX_V`. Si se cumple, ejecuta el bloque asociado.
    if (vbat >= Config::BAT_CHARGE_MAX_V) {
      // [Guía] Actualiza `duty_` con `target_ = 0; reason_ = "M2_VBAT_CEILING"; return duty_`.
      duty_ = target_ = 0; reason_ = "M2_VBAT_CEILING"; return duty_;
    }
    // [Guía] Actualiza `target_` con `approach(target_, Config::M2_CHARGE_CURRENT_A`.
    target_ = approach(target_, Config::M2_CHARGE_CURRENT_A,
      // [Guía] Completa la declaración o expresión con `Config::M2_CURRENT_RAMP_A_PER_SECOND * dt)`. Aquí
      // [Guía] intervienen intervalo efectivo del control en s.
      Config::M2_CURRENT_RAMP_A_PER_SECOND * dt);
    // [Guía] Calcula la diferencia entre la magnitud de carga pedida y la medida. Si la batería absorbe menos de
    // [Guía] lo pedido, el error es positivo.
    const float error = target_ + ibat;
    // [Guía] Comprueba `-ibat > Config::M2_CURRENT_RELIEF_A`. Si se cumple, ejecuta el bloque asociado.
    if (-ibat > Config::M2_CURRENT_RELIEF_A) {
      // Alivio mas rapido que el aumento. Puede llegar a OFF si el minimo5%
      // aun produce demasiada corriente; no mantener5% contra el limite.
      // [Guía] Actualiza `duty_` con `fmaxf(0, duty_ - Config::Q2_SLEW_PER_SECOND * dt)`.
      duty_ = fmaxf(0, duty_ - Config::Q2_SLEW_PER_SECOND * dt);
      // [Guía] Reinicia `pi_` con el estado inicial dado en los argumentos. Aquí intervienen código de
      // [Guía] diagnóstico de la decisión.
      pi_.reset(error, duty_); reason_ = "M2_CURRENT_RELIEF";
    // [Guía] Comprueba `duty_ < Config::Q2_DUTY_MIN`. Si se cumple, ejecuta el bloque asociado.
    } else if (duty_ < Config::Q2_DUTY_MIN) {
      // [Guía] Actualiza `duty_` con `approach(duty_, Config::Q2_DUTY_MIN, Config::M2_Q2_SLEW_PER_SECOND * dt)`.
      duty_ = approach(duty_, Config::Q2_DUTY_MIN, Config::M2_Q2_SLEW_PER_SECOND * dt);
      // [Guía] Reinicia `pi_` con el estado inicial dado en los argumentos. Aquí intervienen código de
      // [Guía] diagnóstico de la decisión.
      pi_.reset(error, duty_); reason_ = "M2_Q2_SOFT_START";
    // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
    } else {
      // [Guía] Actualiza `duty_` con `pi_.update(error, dt, Config::M2_Q2_SLEW_PER_SECOND * dt)`.
      duty_ = pi_.update(error, dt, Config::M2_Q2_SLEW_PER_SECOND * dt);
      // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
      reason_ = duty_ >= Config::Q2_DUTY_MAX ? "M2_CURRENT_DUTY_LIMIT"
        // [Guía] Define una de las dos salidas de la expresión condicional.
        : duty_ <= Config::Q2_DUTY_MIN && error < -0.05f ? "M2_CURRENT_MIN_LIMIT" : "M2_BAT_CURRENT_PI";
    }
    // [Guía] Entrega `duty_` al llamador.
    return duty_;
  }
  // [Guía] Expone `-target_` al módulo que consulta referencia.
  float reference() const { return -target_; } // Signo fisico de Ibat, A.
  // [Guía] Reinicia el control de corriente al salir de M2.
  void stop() {
    // [Guía] Actualiza `duty_` con `target_ = 0`.
    duty_ = target_ = 0;
    // [Guía] Reinicia `pi_` con el estado inicial dado en los argumentos. Aquí intervienen código de diagnóstico
    // [Guía] de la decisión.
    pi_.reset(0, 0); reason_ = "M2_CURRENT_STOPPED";
  }
  // [Guía] Expone `duty_` al módulo que consulta ciclo de trabajo.
  float duty() const { return duty_; }
  // [Guía] Devuelve código de diagnóstico de la decisión.
  const char* reason() const { return reason_; }
 // [Guía] Desde aquí se declaran los datos y auxiliares internos de la clase.
 private:
  // [Guía] Expone `to > from ? fminf(to, from + step) : fmaxf(to, from - step)` al módulo que consulta approach.
  // [Guía] Recibe `from`, `to`, `step`.
  static float approach(float from, float to, float step) {
    // [Guía] Entrega `to > from ? fminf(to, from + step) : fmaxf(to, from - step)` al llamador.
    return to > from ? fminf(to, from + step) : fmaxf(to, from - step);
  }
  // [Guía] Declara `pi_` para conservar pi en este ámbito.
  PIController pi_;
  // [Guía] Actualiza `duty_` con `0, target_ = 0`.
  float duty_ = 0, target_ = 0;
  // [Guía] Guarda la causa legible de la decisión para que pueda distinguirse en la telemetría.
  const char* reason_ = "M2_INACTIVE";
};
