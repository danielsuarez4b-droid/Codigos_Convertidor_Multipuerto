// [Guía] Observa los extremos de tensión de bus durante una ventana continua para calificar su estabilidad.
// [Guía] Evita procesar este encabezado dos veces en una misma unidad de compilación.
#pragma once
/* CALIFICADOR TEMPORAL DE ESTABILIDAD
 * observe recibe ahora en MILISEGUNDOS. Guarda minimo/maximo desde since_.
 * Requiere amplitud <=maxSpan durante windowMs y sin huecos >100 ms.
 * No compara con20 V: estar estable y estar en la referencia son requisitos
 * distintos. El llamador debe evaluar tambien el error respecto a su consigna.
 */
// [Guía] Incorpora las declaraciones de <stdint.h> antes de usar sus tipos y funciones.
#include <stdint.h>
// [Guía] Incorpora las declaraciones de <math.h> antes de usar sus tipos y funciones.
#include <math.h>

// Ventana de extremos sin buffers ni bloqueo. Un exceso de amplitud reinicia
// la calificacion; el promedio cercano a la referencia no oculta oscilacion.
// [Guía] Reúne bajo `BusStabilityWindow` las declaraciones que comparten este tipo o ámbito.
class BusStabilityWindow {
 // [Guía] Desde aquí se declaran los tipos y operaciones que usa el llamador.
 public:
  // [Guía] Reinicia el estado de este módulo.
  void reset() { running_ = false; }
  // Devuelve true solo despues de completar la ventana continua; no espera
  // activamente ni llama delay. Una muestra invalida borra la calificacion.
  // [Guía] Actualiza mínimos y máximos de tensión y evalúa el intervalo observado y su amplitud. El resultado es
  // [Guía] una decisión booleana para el llamador. Recibe `voltage`, `now`, `windowMs`, `maxSpan`.
  bool observe(float voltage, uint32_t now, uint32_t windowMs, float maxSpan) {
    // [Guía] Comprueba `!isfinite(voltage)`. Si se cumple, ejecuta el bloque asociado.
    if (!isfinite(voltage)) { reset(); return false; }
    // Una interrupcion larga de muestras no cuenta como estabilidad observada.
    // [Guía] Abre una ventana con la lectura actual si es la primera o si hubo un hueco mayor de 100 ms. Un hueco
    // [Guía] rompe la continuidad.
    if (!running_ || uint32_t(now - last_) > 100) restart(voltage, now);
    // [Guía] Actualiza `last_` con `now`.
    last_ = now;
    // [Guía] Comprueba `voltage < minimum_`. Si se cumple, ejecuta `minimum_ = voltage`.
    if (voltage < minimum_) minimum_ = voltage;
    // [Guía] Comprueba `voltage > maximum_`. Si se cumple, ejecuta `maximum_ = voltage`.
    if (voltage > maximum_) maximum_ = voltage;
    // Si oscilo demasiado se empieza otra ventana desde la muestra actual.
    // [Guía] Descarta la ventana si la diferencia entre sus extremos excede la banda admitida. La lectura actual
    // [Guía] inicia otra ventana.
    if (maximum_ - minimum_ > maxSpan) restart(voltage, now);
    // [Guía] Indica estabilidad solo después de mantener la banda durante todo el tiempo requerido.
    return uint32_t(now - since_) >= windowMs;
  }
 // [Guía] Desde aquí se declaran los datos y auxiliares internos de la clase.
 private:
  // [Guía] Actualiza `running_` con `false`.
  bool running_ = false;
  // [Guía] Actualiza `minimum_` con `0, maximum_ = 0`.
  float minimum_ = 0, maximum_ = 0;
  // [Guía] Actualiza `since_` con `0, last_ = 0`.
  uint32_t since_ = 0, last_ = 0;
  // Reiniciar extremos y ambos relojes evita heredar tiempo anterior al pico.
  // [Guía] Reinicia la ventana con la lectura de tensión y el instante actuales. Recibe `voltage`, `now`.
  void restart(float voltage, uint32_t now) {
    // [Guía] Actualiza `running_` con `true`.
    running_ = true;
    // [Guía] Actualiza `minimum_` con `maximum_ = voltage`.
    minimum_ = maximum_ = voltage;
    // [Guía] Actualiza `since_` con `last_ = now`.
    since_ = last_ = now;
  }
};
