// [Guía] Dosifica los envíos USB. Mantiene una posición de lectura del informe y avanza solo lo que cabe en el
// [Guía] transmisor.
// [Guía] Evita procesar este encabezado dos veces en una misma unidad de compilación.
#pragma once
// [Guía] Incorpora las declaraciones de <stddef.h> antes de usar sus tipos y funciones.
#include <stddef.h>
// [Guía] Incorpora las declaraciones de <stdint.h> antes de usar sus tipos y funciones.
#include <stdint.h>

// No aplazar indefinidamente un reporte por falta de margen entre muestras.
// El reloj se actualiza en el sketch solo cuando se construye el reporte:
// un buffer ocupado conserva la deuda, sin sobrescribir ordenes de la B&K.
// [Guía] Comprueba si ha vencido el periodo de publicación desde el informe anterior. El resultado es una
// [Guía] decisión booleana para el llamador. Recibe `now`, `lastReport`, `period`, `maxInterval`, `hasMargin`.
inline bool serialReportDue(uint32_t now, uint32_t lastReport,
                            // [Guía] Declara `period` para conservar period en este ámbito.
                            uint32_t period, uint32_t maxInterval, bool hasMargin) {
  // [Guía] Actualiza `elapsed` con `now - lastReport`. Este dato representa tiempo transcurrido.
  const uint32_t elapsed = now - lastReport; // Tambien funciona al desbordar millis.
  // [Guía] Entrega `elapsed >= period && (hasMargin || elapsed >= maxInterval)` al llamador. Aquí intervienen
  // [Guía] tiempo transcurrido.
  return elapsed >= period && (hasMargin || elapsed >= maxInterval);
}

// Escribir solo espacio ya disponible y como maximo la cuota de esta vuelta.
// No usar flush ni esperar el drenaje. Una escritura parcial avanza solo los
// bytes aceptados; la cola sigue intacta para la siguiente llamada al loop.
// El limite de bytes acota trabajo, no garantiza latencia fisica del driver.
// [Guía] Escribe el tramo pendiente que cabe en el búfer del puerto y actualiza el desplazamiento escrito. Recibe
// [Guía] `port`, `buffer`, `length`, `position`, `byteBudget`.
template<class Port>
// [Guía] Completa la declaración o expresión con `void serviceSerialTxBounded(Port& port, const char* buffer,
// [Guía] size_t length`.
void serviceSerialTxBounded(Port& port, const char* buffer, size_t length,
                            // [Guía] Declara `position` para conservar position en este ámbito.
                            size_t& position, size_t byteBudget) {
  // [Guía] Comprueba `position >= length || !byteBudget`. Si se cumple, termina la función en esta rama.
  if (position >= length || !byteBudget) return;
  // [Guía] Consulta cuánto espacio ofrece el transmisor. El límite impide que la escritura espere a que se libere
  // [Guía] el puerto.
  const int available = port.availableForWrite();
  // [Guía] Comprueba `available <= 0`. Si se cumple, termina la función en esta rama.
  if (available <= 0) return;
  // [Guía] Actualiza `count` con `length - position`.
  size_t count = length - position;
  // [Guía] Comprueba `count > size_t(available)`. Si se cumple, ejecuta `count = size_t(available)`.
  if (count > size_t(available)) count = size_t(available);
  // [Guía] Comprueba `count > byteBudget`. Si se cumple, ejecuta `count = byteBudget`.
  if (count > byteBudget) count = byteBudget;
  // [Guía] Escribe desde la posición pendiente y adelanta el cursor solo por los bytes que el puerto dice haber
  // [Guía] aceptado.
  position += port.write(reinterpret_cast<const uint8_t*>(buffer) + position, count);
}
