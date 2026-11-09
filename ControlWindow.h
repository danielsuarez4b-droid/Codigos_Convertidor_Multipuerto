// [Guía] Resume medidas para el registro de diagnóstico. Acumula extremos de tensión y duty y promedia productos
// [Guía] instantáneos de tensión y corriente válidos.
// [Guía] Evita procesar este encabezado dos veces en una misma unidad de compilación.
#pragma once
/* TELEMETRIA, NO CONTROL
 * Una instancia acumula las muestras entre dos reportes USB. count cuenta
 * barridos, no segundos. Los campos publicos permiten construir OBS sin
 * reservar arreglos de muestras. reset al emitir el reporte inicia otra ventana.
 * Una potencia solo es valida si TODAS las muestras de su ventana lo fueron.
 */
// [Guía] Incorpora las declaraciones de <stdint.h> antes de usar sus tipos y funciones.
#include <stdint.h>
// [Guía] Incorpora las declaraciones de <math.h> antes de usar sus tipos y funciones.
#include <math.h>

// Resumen de una ventana serial. Observa sin cambiar consignas ni controladores.
// Extremos de voltajes filtrados: muestran oscilacion vista por el control,
// no picos de conmutacion de 20 kHz. Duty es la ultima orden aplicada.
// [Guía] Reúne bajo `ControlWindow` las declaraciones que comparten este tipo o ámbito.
class ControlWindow {
 // [Guía] Desde aquí se declaran los tipos y operaciones que usa el llamador.
 public:
  // [Guía] Actualiza `count` con `0, pvPowerCount = 0, busPowerCount = 0`.
  uint32_t count = 0, pvPowerCount = 0, busPowerCount = 0;
  // [Guía] Actualiza `pvMin` con `0, pvMax = 0, busMin = 0, busMax = 0`.
  float pvMin = 0, pvMax = 0, busMin = 0, busMax = 0;
  // [Guía] Actualiza `dutyMin` con `0, dutyMax = 0, pvPowerSum = 0, busPowerSum = 0`.
  float dutyMin = 0, dutyMax = 0, pvPowerSum = 0, busPowerSum = 0;

  // pv/bus: voltajes filtrados para extremos; pvRaw/busRaw: voltajes del ADC
  // convertidos a V; corrientes en A; duty en fraccion; valid indica calibracion.
  // [Guía] Actualiza extremos y acumula potencias instantáneas válidas de PV y bus para producir promedios de
  // [Guía] telemetría. Recibe `pv`, `bus`, `duty`, `pvRaw`, `busRaw`, `pvCurrent`, `busCurrent`, `pvValid`,
  // [Guía] `busValid`.
  void observe(float pv, float bus, float duty, float pvRaw, float busRaw,
               // [Guía] Declara `pvCurrent` para conservar panel fotovoltaico corriente en este ámbito.
               float pvCurrent, float busCurrent, bool pvValid, bool busValid) {
    // [Guía] Comprueba `!isfinite(pv) || !isfinite(bus) || !isfinite(duty)`. Si se cumple, termina la función en
    // [Guía] esta rama.
    if (!isfinite(pv) || !isfinite(bus) || !isfinite(duty)) return;
    // La primera muestra es simultaneamente minimo y maximo. Usar cero como
    // minimo inicial falsearia una ventana formada solo por tensiones positivas.
    // [Guía] Comprueba `!count`. Si se cumple, ejecuta el bloque asociado.
    if (!count) {
      // [Guía] Actualiza `pvMin` con `pvMax = pv`.
      pvMin = pvMax = pv;
      // [Guía] Actualiza `busMin` con `busMax = bus`.
      busMin = busMax = bus;
      // [Guía] Actualiza `dutyMin` con `dutyMax = duty`.
      dutyMin = dutyMax = duty;
    // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
    } else {
      // Cada comparacion conserva el extremo mas exigente, sin suavizarlo.
      // [Guía] Comprueba `pv < pvMin`. Si se cumple, ejecuta `pvMin = pv`.
      if (pv < pvMin) pvMin = pv;
      // [Guía] Comprueba `pv > pvMax`. Si se cumple, ejecuta `pvMax = pv`.
      if (pv > pvMax) pvMax = pv;
      // [Guía] Comprueba `bus < busMin`. Si se cumple, ejecuta `busMin = bus`.
      if (bus < busMin) busMin = bus;
      // [Guía] Comprueba `bus > busMax`. Si se cumple, ejecuta `busMax = bus`.
      if (bus > busMax) busMax = bus;
      // [Guía] Comprueba `duty < dutyMin`. Si se cumple, ejecuta `dutyMin = duty`.
      if (duty < dutyMin) dutyMin = duty;
      // [Guía] Comprueba `duty > dutyMax`. Si se cumple, ejecuta `dutyMax = duty`.
      if (duty > dutyMax) dutyMax = duty;
    }
    // [Guía] Suma una unidad a `count` para contabilizar el evento de esta rama.
    ++count;
    // Promediar V*I por barrido, no multiplicar promedios separados.
    // ADC secuencial: son estimaciones de potencia, no medidas simultaneas.
    // [Guía] Comprueba `pvValid && isfinite(pvRaw) && isfinite(pvCurrent)`. Si se cumple, ejecuta el bloque
    // [Guía] asociado.
    if (pvValid && isfinite(pvRaw) && isfinite(pvCurrent)) {
      // [Guía] Acumula la potencia instantánea en el puerto PV a partir de una pareja válida de tensión y
      // [Guía] corriente del mismo barrido.
      pvPowerSum += pvRaw * pvCurrent;
      // [Guía] Suma una unidad a `pvPowerCount` para contabilizar el evento de esta rama.
      ++pvPowerCount;
    }
    // [Guía] Comprueba `busValid && isfinite(busRaw) && isfinite(busCurrent)`. Si se cumple, ejecuta el bloque
    // [Guía] asociado.
    if (busValid && isfinite(busRaw) && isfinite(busCurrent)) {
      // [Guía] Acumula la potencia instantánea que mide el puerto de bus. El promedio se calcula con el número de
      // [Guía] productos válidos.
      busPowerSum += busRaw * busCurrent;
      // [Guía] Suma una unidad a `busPowerCount` para contabilizar el evento de esta rama.
      ++busPowerCount;
    }
  }

  // Invalidar potencia si la calibracion cambia dentro de la ventana.
  // count evita declarar valida una ventana vacia; la igualdad detecta huecos.
  // [Guía] Expone `count && pvPowerCount == count` al módulo que consulta panel fotovoltaico potencia válido.
  bool pvPowerValid() const { return count && pvPowerCount == count; }
  // [Guía] Expone `count && busPowerCount == count` al módulo que consulta bus DC potencia válido.
  bool busPowerValid() const { return count && busPowerCount == count; }
  // [Guía] Expone `pvPowerCount ? pvPowerSum / pvPowerCount : 0` al módulo que consulta panel fotovoltaico
  // [Guía] potencia.
  float pvPower() const { return pvPowerCount ? pvPowerSum / pvPowerCount : 0; }
  // [Guía] Expone `busPowerCount ? busPowerSum / busPowerCount : 0` al módulo que consulta bus DC potencia.
  float busPower() const { return busPowerCount ? busPowerSum / busPowerCount : 0; }

  // Extremos se reinicializan en la primera observacion siguiente.
  // [Guía] Descarta el contenido de la ventana de informe y deja listos los acumuladores para el siguiente
  // [Guía] bloque.
  void reset() {
    // [Guía] Actualiza `count` con `pvPowerCount = busPowerCount = 0`.
    count = pvPowerCount = busPowerCount = 0;
    // [Guía] Actualiza `pvPowerSum` con `busPowerSum = 0`.
    pvPowerSum = busPowerSum = 0;
    // [Guía] Actualiza `pvMin` con `pvMax = busMin = busMax = dutyMin = dutyMax = 0`.
    pvMin = pvMax = busMin = busMax = dutyMin = dutyMax = 0;
  }
};
