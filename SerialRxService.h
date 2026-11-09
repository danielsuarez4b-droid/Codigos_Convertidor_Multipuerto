// [Guía] Acota el número de bytes y el tiempo de recepción USB que se permite gastar en cada visita del lazo
// [Guía] principal.
// [Guía] Evita procesar este encabezado dos veces en una misma unidad de compilación.
#pragma once
// [Guía] Incorpora las declaraciones de <stdint.h> antes de usar sus tipos y funciones.
#include <stdint.h>

// RX tiene cuota propia, independiente del margen de TX/telemetria. Se invoca
// tras adquisicion/protecciones y conserva las lineas incompletas en consumer.
// Clock/consumer inyectables permiten verificar fragmentacion sin puertos USB.
//80us se comprueba entre bytes: parsear una linea completa puede excederlo.
// No es una garantia de latencia hardware; medir lost/PI_lost en el banco.
// [Guía] Lee el puerto hasta agotar la cuota de bytes o de tiempo y entrega cada byte al consumidor suministrado.
// [Guía] Recibe `port`, `clock`, `consume`.
template<class Port, class Clock, class Consumer>
// [Guía] Completa la declaración o expresión con `void serviceSerialRxBounded(Port& port, Clock clock, Consumer
// [Guía] consume) {`.
void serviceSerialRxBounded(Port& port, Clock clock, Consumer consume) {
  // [Guía] Actualiza `start` con `clock()`.
  const uint32_t start = clock();
  // [Guía] Controla las iteraciones mediante `uint8_t budget = 0; budget < 16; ++budget)`.
  for (uint8_t budget = 0; budget < 16; ++budget) {
    // [Guía] Interrumpe el servicio si se agotaron los 80 µs de esta visita o si ya no hay bytes esperando en el
    // [Guía] puerto.
    if (uint32_t(clock() - start) >= 80 || port.available() <= 0) break;
    // [Guía] Lee un byte como entero para conservar el valor negativo que señala ausencia de datos.
    const int value = port.read();
    // [Guía] Comprueba `value < 0`. Si se cumple, sale de esta rama del selector tras realizar la acción
    // [Guía] indicada.
    if (value < 0) break;
    // [Guía] Entrega el byte válido al receptor de caracteres, que conserva el contenido de una línea hasta
    // [Guía] completarla.
    consume(char(value));
  }
}
