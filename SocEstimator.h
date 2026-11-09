// [Guía] Estima el SOC por conteo de carga con tiempo real e integración trapezoidal. El signo positivo de Ibat
// [Guía] corresponde a descarga.
// [Guía] Evita procesar este encabezado dos veces en una misma unidad de compilación.
#pragma once
/* ESTIMACION DE ESTADO DE CARGA POR CONTEO DE CULOMBIOS
 * Ibat>0: bateria se descarga; Ibat<0: bateria recibe carga.
 * SOC_nuevo=SOC_anterior-integral(Ibat*dt)/(capacidad_Ah*3600).
 * fraction_ guarda0..1; percent() lo expresa0..100. El valor inicial viene
 * de Config o del comando SOC=, no de una curva tension/SOC en este archivo.
 * Un hueco marca la estimacion como discontinua hasta una nueva semilla;
 * no recuperar ese estado solo porque vuelvan a llegar muestras.
 */
// [Guía] Incorpora las declaraciones de "Config.h" antes de usar sus tipos y funciones.
#include "Config.h"
// [Guía] Incorpora las declaraciones de <math.h> antes de usar sus tipos y funciones.
#include <math.h>

// Conteo de carga: no convierte automaticamente Vbat a SOC ni controla compuertas.
// [Guía] Reúne bajo `SocEstimator` las declaraciones que comparten este tipo o ámbito.
class SocEstimator {
// [Guía] Desde aquí se declaran los tipos y operaciones que usa el llamador.
public:
  // Inicializacion explicita: validar antes de tocar el estado anterior.
  // Borra los relojes para que el primer update posterior no integre el pasado.
  // [Guía] Valida el SOC inicial en porcentaje y lo convierte en la fracción interna de carga. El resultado es
  // [Guía] una decisión booleana para el llamador. Recibe `percent`.
  bool seed(float percent) {
    // [Guía] Comprueba `!isfinite(percent) || percent < 0 || percent > 100`. Si se cumple, termina la función en
    // [Guía] esta rama.
    if (!isfinite(percent) || percent < 0 || percent > 100) return false;
    // [Guía] Convierte el porcentaje inicial a una fracción de capacidad y la guarda en precisión doble.
    fraction_ = double(percent) / 100.0;
    // [Guía] Actualiza `initialized_` con `true`.
    initialized_ = true;
    // [Guía] Actualiza `baseline_` con `tracking_ = gap_ = false`.
    baseline_ = tracking_ = gap_ = false;
    // [Guía] Entrega `true` al llamador.
    return true;
  }

  // nowUs esta en microsegundos; la conversion1e-6 aparece solo al integrar.
  // Este metodo no activa ni desactiva PWM: EMS consulta continuous/percent.
  // [Guía] Integra la corriente medida entre muestras válidas consecutivas. Un intervalo no fiable rompe la
  // [Guía] continuidad y no se integra. Recibe `batteryAmps`, `currentValid`, `nowUs`.
  void update(float batteryAmps, bool currentValid, uint32_t nowUs) {
    // [Guía] Comprueba `!initialized_`. Si se cumple, termina la función en esta rama.
    if (!initialized_) return;
    // [Guía] Comprueba `!currentValid || !isfinite(batteryAmps)`. Si se cumple, ejecuta el bloque asociado.
    if (!currentValid || !isfinite(batteryAmps)) {
      // No asignar corriente cero a un intervalo que realmente no se midio.
      // [Guía] Comprueba `baseline_`. Si se cumple, ejecuta `gap_ = true`.
      if (baseline_) gap_ = true;
      // [Guía] Actualiza `baseline_` con `tracking_ = false`.
      baseline_ = tracking_ = false;
      // [Guía] Finaliza aquí la función y devuelve el control al llamador.
      return;
    }
    // La eficiencia solo reduce la carga entrante, no la descarga saliente.
    // [Guía] Separa el tratamiento de carga y descarga. Las líneas siguientes aplican la eficiencia de carga
    // [Guía] cuando Ibat es negativa.
    const double effective = batteryAmps < 0
      // [Guía] Define una de las dos salidas de la expresión condicional.
      ? double(batteryAmps) * Config::SOC_CHARGE_EFFICIENCY : double(batteryAmps);
    // Se necesitan DOS extremos para el trapecio; el primero solo se conserva.
    // [Guía] Comprueba `!baseline_`. Si se cumple, ejecuta el bloque asociado.
    if (!baseline_) {
      // [Guía] Actualiza `previousAmps_` con `effective`.
      previousAmps_ = effective;
      // [Guía] Actualiza `previousUs_` con `nowUs`.
      previousUs_ = nowUs;
      // [Guía] Actualiza `baseline_` con `tracking_ = true`.
      baseline_ = tracking_ = true;
      // [Guía] Finaliza aquí la función y devuelve el control al llamador.
      return;
    }
    // Resta unsigned: admite el desbordamiento normal de micros().
    // [Guía] Mide la separación real entre las dos corrientes que se van a integrar. El tipo sin signo permite el
    // [Guía] cruce normal del contador.
    const uint32_t elapsedUs = nowUs - previousUs_;
    // No inventar corriente entre extremos demasiado separados.
    // [Guía] Si el intervalo excede el límite, corta la integración y marca una discontinuidad en vez de atribuir
    // [Guía] una corriente al hueco.
    if (elapsedUs > Config::SOC_MAX_GAP_US) {
      // [Guía] Actualiza `gap_` con `true`.
      gap_ = true;
    // [Guía] Comprueba `elapsedUs > 0`. Si se cumple, ejecuta el bloque asociado.
    } else if (elapsedUs > 0) {
      // Integral trapezoidal. double evita perder incrementos diminutos a 2 kHz.
      // [Guía] Promedia ambos extremos del intervalo para integrar por trapecios. Al multiplicar por el tiempo en
      // [Guía] segundos se obtienen culombios.
      const double coulombs = 0.5 * (previousAmps_ + effective)
        // [Guía] Completa la declaración o expresión con `* double(elapsedUs) * 0.000001`.
        * double(elapsedUs) * 0.000001;
      // Ah*3600=C. La resta implementa el convenio de signo de Ibat.
      // [Guía] Convierte la capacidad nominal de Ah a culombios y resta la fracción consumida. Una cantidad
      // [Guía] integrada negativa incrementa el SOC.
      fraction_ -= coulombs / (double(Config::BATTERY_CAPACITY_AH) * 3600.0);
      // [Guía] Comprueba `fraction_ < 0`. Si se cumple, ejecuta `fraction_ = 0`. Aquí intervienen SOC como
      // [Guía] fracción entre 0 y 1.
      if (fraction_ < 0) fraction_ = 0;
      // [Guía] Comprueba `fraction_ > 1`. Si se cumple, ejecuta `fraction_ = 1`. Aquí intervienen SOC como
      // [Guía] fracción entre 0 y 1.
      if (fraction_ > 1) fraction_ = 1;
    // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
    } else {
      // Una muestra duplicada no sustituye el extremo anterior de la integral.
      // [Guía] Finaliza aquí la función y devuelve el control al llamador.
      return;
    }
    // [Guía] Actualiza `previousAmps_` con `effective`.
    previousAmps_ = effective;
    // [Guía] Actualiza `previousUs_` con `nowUs`.
    previousUs_ = nowUs;
    // [Guía] Actualiza `tracking_` con `true`. Este dato representa indicación de integración o seguimiento
    // [Guía] activo.
    tracking_ = true;
  }

  // Accesores de diagnostico: no reintegran ni recalculan el SOC al leerlos.
  // [Guía] Expone `initialized_` al módulo que consulta initialized.
  bool initialized() const { return initialized_; }
  // [Guía] Expone `initialized_ && tracking_ && !gap_` al módulo que consulta continuous.
  bool continuous() const { return initialized_ && tracking_ && !gap_; }
  // [Guía] Expone `gap_` al módulo que consulta hueco entre muestras.
  bool gap() const { return gap_; }
  // [Guía] Expone `float(fraction_ * 100.0)` al módulo que consulta percent.
  float percent() const { return float(fraction_ * 100.0); }
  // [Guía] Informa si falta una condición inicial o si la estimación está en curso con o sin discontinuidades.
  const char* status() const {
    // [Guía] Comprueba `!initialized_`. Si se cumple, termina la función en esta rama.
    if (!initialized_) return "NEED_INITIAL_SOC";
    // [Guía] Comprueba `!tracking_`. Si se cumple, termina la función en esta rama. Aquí intervienen indicación
    // [Guía] de integración o seguimiento activo.
    if (!tracking_) return "WAIT_IBAT";
    // [Guía] Entrega `gap_ ? "ESTIMATE_WITH_GAP" : "ESTIMATING"` al llamador.
    return gap_ ? "ESTIMATE_WITH_GAP" : "ESTIMATING";
  }

// [Guía] Desde aquí se declaran los datos y auxiliares internos de la clase.
private:
  // Sin EEPROM: cada reset requiere un SOC inicial explicito y actualizado.
  // [Guía] Actualiza `fraction_` con `0, previousAmps_ = 0`. Este dato representa SOC como fracción entre 0 y 1.
  double fraction_ = 0, previousAmps_ = 0;
  // [Guía] Actualiza `previousUs_` con `0`.
  uint32_t previousUs_ = 0;
  // [Guía] Actualiza `initialized_` con `false, baseline_ = false, tracking_ = false, gap_ = false`.
  bool initialized_ = false, baseline_ = false, tracking_ = false, gap_ = false;
};
