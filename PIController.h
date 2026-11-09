// [Guía] Implementa un PI reutilizable con saturación, límite de pendiente y corrección integral según la salida
// [Guía] aplicada.
// [Guía] Evita procesar este encabezado dos veces en una misma unidad de compilación.
#pragma once
/* RESPONSABILIDAD Y UNIDADES
 * Control proporcional-integral reutilizado por los tres interruptores.
 * error: diferencia de tensiones en V con signo elegido por el llamador.
 * kp: fraccion de duty/V; ki: fraccion de duty/(V*s); dtSeconds: segundos.
 * minimum/maximum y todas las salidas son fracciones:0.05 significa5%.
 * No conoce pines, modos EMS ni tension de bateria. Tampoco decide si la
 * realimentacion es positiva/negativa: action1/action2/action3 viven fuera.
 * Puede mantener una salida anterior ante entradas invalidas; la proteccion
 * de sensores y el apagado deben resolverse por encima de este controlador.
 */
// [Guía] Incorpora las declaraciones de <math.h> antes de usar sus tipos y funciones.
#include <math.h>

// PI paralelo: Ki se expresa por segundo; duty en fraccion, no porcentaje.
// El llamador define el signo del error segun la planta y el driver.
// [Guía] Reúne bajo `PIController` las declaraciones que comparten este tipo o ámbito.
class PIController {
 // [Guía] Desde aquí se declaran los tipos y operaciones que usa el llamador.
 public:
  // La lista tras ':' guarda parametros antes de ejecutar cualquier update.
  // Construir no calcula un duty: el arranque debe llamar reset con su salida real.
  // [Guía] Construye PIController con la configuración inicial indicada. Recibe `kp`, `ki`, `minimum`, `maximum`.
  PIController(float kp, float ki, float minimum, float maximum)
      // [Guía] Define una de las dos salidas de la expresión condicional.
      : kp_(kp), ki_(ki), minimum_(minimum), maximum_(maximum) {}

  // Transferencia sin salto: elegir I para que P+I sea el duty ya aplicado.
  // output - kp*error despeja I de u=kp*error+I. No significa poner I en cero.
  // Es esencial al cambiar entre PI, MPPT y rampas o al actuar una limitacion.
  // [Guía] Precarga la integral para que el PI parta de la salida inicial indicada con el error actual. Recibe
  // [Guía] `error`, `initialDuty`.
  void reset(float error, float initialDuty) {
    // [Guía] Actualiza `trackingCorrection_` con `0.0f`.
    trackingCorrection_ = 0.0f;
    // [Guía] Actualiza `slewLimited_` con `false`.
    slewLimited_ = false;
    // [Guía] Actualiza `output_` con `clamp(initialDuty)`.
    output_ = clamp(initialDuty);
    // [Guía] Despeja el término integral de la suma P más I para que la salida inicial coincida con el duty de
    // [Guía] entrada ya limitado.
    integral_ = output_ - kp_ * error;
  }

  // Ejecutar un paso discreto. maxDutyStep es variacion MAXIMA en este paso,
  // no una velocidad: el llamador debe pasar slew_por_segundo*dtSeconds.
  // [Guía] Integra el error con el intervalo real, limita la salida y su pendiente, y corrige la integral usando
  // [Guía] la salida efectivamente alcanzable. Recibe `error`, `dtSeconds`, `maxDutyStep`.
  float update(float error, float dtSeconds, float maxDutyStep = 1.0f) {
    // [Guía] Actualiza `trackingCorrection_` con `0.0f`.
    trackingCorrection_ = 0.0f;
    // [Guía] Actualiza `slewLimited_` con `false`.
    slewLimited_ = false;
    // [Guía] Comprueba `!isfinite(error) || !isfinite(dtSeconds) || dtSeconds <= 0.0f`. Si se cumple, ejecuta el
    // [Guía] bloque asociado. Aquí intervienen intervalo del control en s.
    if (!isfinite(error) || !isfinite(dtSeconds) || dtSeconds <= 0.0f
        // [Guía] Añade una alternativa suficiente dentro de la condición compuesta.
        || !isfinite(maxDutyStep) || maxDutyStep < 0.0f)
      // [Guía] Entrega `output_` al llamador.
      return output_;
    // Separar accion instantanea P e incremento I=Ki*e*dt permite limitar
    // solo la integracion que seguiria empujando contra un limite de actuacion.
    // [Guía] Calcula la respuesta proporcional instantánea al error medido.
    const float proportional = kp_ * error;
    // [Guía] Integra el error de este paso con el intervalo real en segundos. El incremento queda en fracción de
    // [Guía] duty.
    const float increment = ki_ * error * dtSeconds;
    // [Guía] Estima la orden antes de incorporar el incremento integral de esta muestra.
    const float previousCommand = proportional + integral_;
    // [Guía] Actualiza `step` con `maxDutyStep > 0.0f ? maxDutyStep : 0.0f`.
    const float step = maxDutyStep > 0.0f ? maxDutyStep : 0.0f;
    // La interseccion entre limites fisicos y slew produce [lower,upper].
    // Se calcula alrededor de la salida anterior, no alrededor de la consigna.
    // [Guía] Obtiene la menor salida alcanzable desde el duty anterior dentro de la pendiente y del mínimo
    // [Guía] físico.
    const float lower = clamp(output_ - step);
    // [Guía] Obtiene la mayor salida alcanzable desde el duty anterior dentro de la pendiente y del máximo
    // [Guía] físico.
    const float upper = clamp(output_ + step);
    // Limitar solo el incremento integral: un salto de P no debe borrar I.
    // [Guía] Actualiza `appliedIncrement` con `increment`.
    float appliedIncrement = increment;
    // [Guía] Comprueba `increment > 0.0f`. Si se cumple, ejecuta el bloque asociado.
    if (increment > 0.0f) {
      // Incremento positivo: integrar solo el espacio restante hacia upper.
      // [Guía] Calcula el espacio de aumento disponible para la integral antes de saturar en el límite superior
      // [Guía] efectivo.
      const float room = upper - previousCommand;
      // [Guía] Actualiza `appliedIncrement` con `room <= 0.0f ? 0.0f : (increment < room ? increment : room)`.
      appliedIncrement = room <= 0.0f ? 0.0f : (increment < room ? increment : room);
    // [Guía] Comprueba `increment < 0.0f`. Si se cumple, ejecuta el bloque asociado.
    } else if (increment < 0.0f) {
      // Incremento negativo: el mismo razonamiento, hacia lower.
      // [Guía] Calcula el espacio de reducción disponible para la integral antes de saturar en el límite inferior
      // [Guía] efectivo.
      const float room = lower - previousCommand;
      // [Guía] Actualiza `appliedIncrement` con `room >= 0.0f ? 0.0f : (increment > room ? increment : room)`.
      appliedIncrement = room >= 0.0f ? 0.0f : (increment > room ? increment : room);
    }
    // [Guía] Acumula solo la porción del incremento que el actuador puede realizar. Así se reduce la acumulación
    // [Guía] integral contra los límites.
    integral_ += appliedIncrement;
    // Diferencia entre integral permitida y pedida: diagnostico anti-windup.
    // requested se calcula sin anti-windup para identificar si limito el slew.
    // [Guía] Registra la diferencia entre la integración permitida y la solicitada para diagnosticar
    // [Guía] antiacumulación.
    trackingCorrection_ = appliedIncrement - increment;
    // [Guía] Actualiza `requested` con `clamp(previousCommand + increment)`.
    const float requested = clamp(previousCommand + increment);
    // [Guía] Indica que el duty solicitado excede el intervalo alcanzable en este paso por el límite de
    // [Guía] pendiente.
    slewLimited_ = requested < lower || requested > upper;
    // [Guía] Actualiza `command` con `clamp(proportional + integral_)`.
    const float command = clamp(proportional + integral_);
    // [Guía] Recorta la orden al intervalo de salida alcanzable, conservando la orden original cuando ya cae
    // [Guía] entre ambos límites.
    output_ = command < lower ? lower : (command > upper ? upper : command);
    // [Guía] Entrega `output_` al llamador.
    return output_;
  }

  // Observadores: no avanzan el controlador; const impide modificar su estado.
  // [Guía] Expone `output_` al módulo que consulta output.
  float output() const { return output_; }
  // [Guía] Expone `integral_` al módulo que consulta integral.
  float integral() const { return integral_; }
  // [Guía] Expone `slewLimited_` al módulo que consulta pendiente limited.
  bool slewLimited() const { return slewLimited_; }
  // [Guía] Expone `trackingCorrection_` al módulo que consulta tracking correction.
  float trackingCorrection() const { return trackingCorrection_; }

 // [Guía] Desde aquí se declaran los datos y auxiliares internos de la clase.
 private:
  // Saturacion estatica; el operador ?: escoge minimo, maximo o valor interno.
  // [Guía] Restringe la salida propuesta al intervalo admisible de duty. Recibe `value`.
  float clamp(float value) const {
    // [Guía] Entrega `value < minimum_ ? minimum_ : (value > maximum_ ? maximum_ : value)` al llamador.
    return value < minimum_ ? minimum_ : (value > maximum_ ? maximum_ : value);
  }
  // [Guía] Declara `kp_` para conservar ganancia proporcional en este ámbito.
  float kp_, ki_, minimum_, maximum_; // Parametros que no cambian durante update.
  // [Guía] Actualiza `integral_` con `0.0f`.
  float integral_ = 0.0f;
  // [Guía] Actualiza `output_` con `0.0f`.
  float output_ = 0.0f;
  // [Guía] Actualiza `trackingCorrection_` con `0.0f`.
  float trackingCorrection_ = 0.0f;
  // [Guía] Actualiza `slewLimited_` con `false`.
  bool slewLimited_ = false;
};
