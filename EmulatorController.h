// [Guía] Mantiene consignas pedidas y estados confirmados de la B&K. Identifica solicitudes, confirma respuestas
// [Guía] y vigila el enlace sin bloquear el lazo de control.
// [Guía] Evita procesar este encabezado dos veces en una misma unidad de compilación.
#pragma once
/* CANAL DE ORDENES NO BLOQUEANTE ARDUINO -> PC -> BK8502
 * Estado deseado y confirmado son distintos. Solicitar R/ON no significa que
 * el instrumento ya acepto la orden. prepareRequest produce BK_REQ con un ID;
 * command consume BK_ACK/BK_ERR correlacionados y solo entonces confirma.
 * El puerto fisico BK pertenece a Python. Esta clase solo produce/consume
 * texto a traves del USB compartido con la telemetria.
 * Los relojes now, sentAt_, lastLink_ son MILISEGUNDOS, no micros().
 */
// [Guía] Incorpora las declaraciones de <stdint.h> antes de usar sus tipos y funciones.
#include <stdint.h>
// [Guía] Incorpora las declaraciones de <stdio.h> antes de usar sus tipos y funciones.
#include <stdio.h>
// [Guía] Incorpora las declaraciones de <stdlib.h> antes de usar sus tipos y funciones.
#include <stdlib.h>
// [Guía] Incorpora las declaraciones de <string.h> antes de usar sus tipos y funciones.
#include <string.h>
// [Guía] Incorpora las declaraciones de <math.h> antes de usar sus tipos y funciones.
#include <math.h>

// Arduino decide consignas; Python ejecuta transacciones lentas con la B&K.
// Ninguna funcion de esta clase espera respuestas ni modifica PWM/PI.
// [Guía] Reúne bajo `EmulatorController` las declaraciones que comparten este tipo o ámbito.
class EmulatorController {
 // [Guía] Desde aquí se declaran los tipos y operaciones que usa el llamador.
 public:
  // [Guía] Actualiza `habilitarEmulador` con `true`.
  bool habilitarEmulador = true;       // Permiso, NO equivale a consumo ON.
  // [Guía] Actualiza `enviarNuevaResistencia` con `true`.
  bool enviarNuevaResistencia = true;  // Solicitud pendiente hasta confirmacion.
  // [Guía] Actualiza `resistenciaOhm` con `4000.0f`.
  float resistenciaOhm = 4000.0f;      // Arranque OFF; no R equivalente del bus.
  // [Guía] Actualiza `resistenciaConfirmadaOhm` con `0`.
  float resistenciaConfirmadaOhm = 0;
  // [Guía] Actualiza `resistenciaConfirmada` con `false`.
  bool resistenciaConfirmada = false;
  // [Guía] Actualiza `consumoSolicitado` con `false`.
  bool consumoSolicitado = false;
  // [Guía] Actualiza `consumoConfirmado` con `false`.
  bool consumoConfirmado = false;
  // [Guía] Actualiza `estadoConsumoValido` con `false`.
  bool estadoConsumoValido = false;    // Ultima orden aceptada; no mide corriente BK.
  // [Guía] Actualiza `enlace` con `false`.
  bool enlace = false;
  // [Guía] Actualiza `estado` con `"SIN_PC"`.
  const char* estado = "SIN_PC";
  // [Guía] Completa la declaración o expresión con `static constexpr float R_MIN = 30.0f, R_MAX = 4000.0f`.
  static constexpr float R_MIN = 30.0f, R_MAX = 4000.0f;
  // [Guía] Completa la declaración o expresión con `static constexpr uint32_t LINK_TIMEOUT_MS = 12000,
  // [Guía] ACK_TIMEOUT_MS = 8000`.
  static constexpr uint32_t LINK_TIMEOUT_MS = 12000, ACK_TIMEOUT_MS = 8000;
  // Reintentar la MISMA transaccion, nunca crear un ON nuevo ni renovar8s.
  // Python V51 responde duplicados con el ACK guardado sin accionar otra vez BK.
  // [Guía] Completa la declaración o expresión con `static constexpr uint32_t ACK_RETRY_MS = 1500`.
  static constexpr uint32_t ACK_RETRY_MS = 1500;
  // [Guía] Completa la declaración o expresión con `static constexpr uint8_t ACK_MAX_RETRIES = 3`.
  static constexpr uint8_t ACK_MAX_RETRIES = 3;
  // [Guía] Expone `pending_ == NONE ? 0 : id_` al módulo que consulta pending id.
  uint32_t pendingId() const { return pending_ == NONE ? 0 : id_; }
  // [Guía] Expone `retries_` al módulo que consulta retries.
  uint8_t retries() const { return retries_; }
  // [Guía] Expone `acceptedAcks_` al módulo que consulta accepted acks.
  uint32_t acceptedAcks() const { return acceptedAcks_; }
  // [Guía] Expone `rejectedAcks_` al módulo que consulta rejected acks.
  uint32_t rejectedAcks() const { return rejectedAcks_; }
  // [Guía] Expone `ackStatus_` al módulo que consulta ack status.
  const char* ackStatus() const { return ackStatus_; }

  // Usar esta funcion desde la futura EMS o desde el ajuste manual.
  // La configuracion de R no activa el consumo y se cuantiza a 1 miliohmio.
  // [Guía] Valida y guarda la resistencia solicitada en ohmios para transmitirla al instrumento. El resultado es
  // [Guía] una decisión booleana para el llamador. Recibe `value`.
  bool configurarResistencia(float value) {
    // [Guía] Comprueba `!habilitarEmulador || !isfinite(value) || value < R_MIN || value > R_MAX`. Si se cumple,
    // [Guía] ejecuta el bloque asociado.
    if (!habilitarEmulador || !isfinite(value) || value < R_MIN || value > R_MAX) {
      // [Guía] Actualiza `estado` con `"R_RECHAZADA"`.
      estado = "R_RECHAZADA";
      // [Guía] Entrega `false` al llamador.
      return false;
    }
    // Redondear a miliohmios evita que PC y Arduino comparen float arbitrarios.
    // [Guía] Actualiza `resistenciaOhm` con `floorf(value * 1000.0f + 0.5f) / 1000.0f`.
    resistenciaOhm = floorf(value * 1000.0f + 0.5f) / 1000.0f;
    // [Guía] Actualiza `enviarNuevaResistencia` con `true`.
    enviarNuevaResistencia = true;
    // [Guía] Actualiza `estado` con `"R_PENDIENTE"`.
    estado = "R_PENDIENTE";
    // [Guía] Entrega `true` al llamador.
    return true;
  }

  // OFF se puede solicitar incluso sin permiso. ON requiere consigna confirmada
  // y ningun ajuste pendiente: nunca activar con una R desconocida.
  // [Guía] Guarda la orden solicitada de habilitar o deshabilitar el consumo de la carga. El resultado es una
  // [Guía] decisión booleana para el llamador. Recibe `on`.
  bool solicitarConsumo(bool on) {
    // [Guía] Comprueba `on && (!enlace || !habilitarEmulador || !resistenciaConfirmada`. Si se cumple, ejecuta el
    // [Guía] bloque asociado.
    if (on && (!enlace || !habilitarEmulador || !resistenciaConfirmada
               // [Guía] Añade una alternativa suficiente dentro de la condición compuesta. Aquí intervienen tipo
               // [Guía] de transacción que espera respuesta.
               || enviarNuevaResistencia || pending_ != NONE)) {
      // [Guía] Actualiza `estado` con `"ON_RECHAZADO"`.
      estado = "ON_RECHAZADO";
      // [Guía] Entrega `false` al llamador.
      return false;
    }
    // Marcar dirty obliga a emitir una orden, incluso si el ultimo ACK era OFF.
    // [Guía] Actualiza `consumoSolicitado` con `on`.
    consumoSolicitado = on;
    // [Guía] Actualiza `loadDirty_` con `true`.
    loadDirty_ = true;
    // [Guía] Comprueba `!on`. Si se cumple, ejecuta `pending_ = NONE; // Priorizar OFF e ignorar ACK de la orden
    // [Guía] cancelada.`.
    if (!on) pending_ = NONE; // Priorizar OFF e ignorar ACK de la orden cancelada.
    // [Guía] Actualiza `estado` con `on ? "ON_PENDIENTE" : "OFF_PENDIENTE"`.
    estado = on ? "ON_PENDIENTE" : "OFF_PENDIENTE";
    // [Guía] Entrega `true` al llamador.
    return true;
  }

  // Tiempo de enlace y de orden independientes del control del convertidor.
  // [Guía] Supervisa el reloj del enlace y el tiempo de respuesta de las transacciones pendientes. Recibe `now`.
  void tick(uint32_t now) {
    // Detectar tambien cambios directos de la bandera desde una futura EMS.
    // [Guía] Comprueba `habilitarEmulador != lastAllowed_`. Si se cumple, ejecuta el bloque asociado.
    if (habilitarEmulador != lastAllowed_) {
      // [Guía] Actualiza `lastAllowed_` con `habilitarEmulador`.
      lastAllowed_ = habilitarEmulador;
      // [Guía] Comprueba `!habilitarEmulador`. Si se cumple, ejecuta el bloque asociado.
      if (!habilitarEmulador) {
        // [Guía] Actualiza `enviarNuevaResistencia` con `false`.
        enviarNuevaResistencia = false;
        // [Guía] Actualiza `resistenciaConfirmada` con `false`.
        resistenciaConfirmada = false;
        // [Guía] Guarda la orden solicitada de habilitar o deshabilitar el consumo de la carga.
        solicitarConsumo(false);
      // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
      } else enviarNuevaResistencia = true;
    }
    // Heartbeat ausente invalida confianza; timeout ACK invalida la transaccion.
    // invalidate no puede apagar fisicamente una carga si ya se perdio el PC.
    // [Guía] Detecta que venció la evidencia de enlace con el puente. Las confirmaciones del instrumento dejan de
    // [Guía] ser fiables.
    if (enlace && uint32_t(now - lastLink_) >= LINK_TIMEOUT_MS) {
      // [Guía] Actualiza `enlace` con `false`.
      enlace = false;
      // [Guía] Invalida la evidencia de enlace y confirmaciones cuando ya no es fiable.
      invalidate("SIN_PC");
    // [Guía] Comprueba `pending_ != NONE && uint32_t(now - sentAt_) >= ACK_TIMEOUT_MS`. Si se cumple, ejecuta el
    // [Guía] bloque asociado. Aquí intervienen tipo de transacción que espera respuesta.
    } else if (pending_ != NONE && uint32_t(now - sentAt_) >= ACK_TIMEOUT_MS) {
      // [Guía] Invalida la evidencia de enlace y confirmaciones cuando ya no es fiable.
      invalidate("ACK_TIMEOUT");
    }
  }

  // Protocolo por lineas; devuelve false para delegar comandos PI/calibracion.
  // [Guía] Interpreta mensajes del puente y relaciona las confirmaciones y errores con el identificador y tipo de
  // [Guía] solicitud. El resultado es una decisión booleana para el llamador. Recibe `line`, `now`.
  bool command(char* line, uint32_t now) {
    // [Guía] Comprueba `!strcmp(line, )`. Si se cumple, ejecuta el bloque asociado.
    if (!strcmp(line, "BK_HELLO")) {
      // Python confirma OFF antes de abrir una sesion. Nunca restaurar ON.
      // [Guía] Invalida la evidencia de enlace y confirmaciones cuando ya no es fiable.
      invalidate("CONECTADO_OFF");
      // [Guía] Actualiza `enlace` con `true`.
      enlace = true;
      // [Guía] Actualiza `lastLink_` con `now`.
      lastLink_ = now;
      // [Guía] Actualiza `estadoConsumoValido` con `true`.
      estadoConsumoValido = true;
      // [Guía] Actualiza `consumoConfirmado` con `false`.
      consumoConfirmado = false;
      // [Guía] Actualiza `loadDirty_` con `false`.
      loadDirty_ = false;
      // [Guía] Actualiza `enviarNuevaResistencia` con `habilitarEmulador`.
      enviarNuevaResistencia = habilitarEmulador;
      // [Guía] Entrega `true` al llamador.
      return true;
    }
    // [Guía] Comprueba `!strcmp(line, )`. Si se cumple, ejecuta el bloque asociado.
    if (!strcmp(line, "BK_PING")) {
      // PING mantiene una sesion existente; no puede crearla por si solo.
      // [Guía] Comprueba `enlace`. Si se cumple, ejecuta `lastLink_ = now`.
      if (enlace) lastLink_ = now;
      // [Guía] Entrega `true` al llamador.
      return true;
    }
    // strncmp reconoce prefijos; strcmp exige igualdad completa. sscanf debe
    // consumir solo un ID: el caracter extra detecta basura tras el numero.
    // [Guía] Comprueba `!strncmp(line, , 7)`. Si se cumple, ejecuta el bloque asociado.
    if (!strncmp(line, "BK_ERR=", 7)) {
      // [Guía] Completa la declaración o expresión con `unsigned long id`.
      unsigned long id;
      // [Guía] Declara `extra` para conservar extra en este ámbito.
      char extra;
      // [Guía] Comprueba `sscanf(line + 7, , &id, &extra) == 1 && pending_ != NONE && id == id_`. Si se cumple,
      // [Guía] ejecuta el bloque asociado. Aquí intervienen tipo de transacción que espera respuesta.
      if (sscanf(line + 7, "%lu%c", &id, &extra) == 1 && pending_ != NONE && id == id_) {
        // [Guía] Invalida la evidencia de enlace y confirmaciones cuando ya no es fiable.
        invalidate("ERROR_BK");
      }
      // [Guía] Entrega `true` al llamador.
      return true;
    }
    // [Guía] Comprueba `!strncmp(line, , 7)`. Si se cumple, ejecuta el bloque asociado.
    if (!strncmp(line, "BK_ACK=", 7)) {
      // [Guía] Completa la declaración o expresión con `unsigned long id`.
      unsigned long id;
      // [Guía] Declara `kind` para conservar kind en este ámbito.
      char kind[8], number[32], extra;
      // strtof evita requerir scanf con conversion float en librerias reducidas.
      // [Guía] Comprueba `sscanf(line + 7, , &id, kind, number, &extra) != 3`. Si se cumple, ejecuta el bloque
      // [Guía] asociado.
      if (sscanf(line + 7, "%lu,%7[^,],%31s%c", &id, kind, number, &extra) != 3) {
        // [Guía] Completa la declaración o expresión con `++rejectedAcks_; ackStatus_ = "REJECT_FORMAT"; return
        // [Guía] true`.
        ++rejectedAcks_; ackStatus_ = "REJECT_FORMAT"; return true;
      }
      // [Guía] Comprueba `pending_ == NONE || id != id_`. Si se cumple, ejecuta el bloque asociado. Aquí
      // [Guía] intervienen tipo de transacción que espera respuesta.
      if (pending_ == NONE || id != id_) {
        // [Guía] Completa la declaración o expresión con `++rejectedAcks_; ackStatus_ =
        // [Guía] "REJECT_ID_OR_NO_PENDING"; return true`.
        ++rejectedAcks_; ackStatus_ = "REJECT_ID_OR_NO_PENDING"; return true;
      }
      // strtof deja end donde termina la conversion. Rechazar texto sobrante,
      // infinitos y NaN impide confirmar un mensaje parcial o corrupto.
      // [Guía] Declara `end` para conservar end en este ámbito.
      char* end;
      // [Guía] Actualiza `value` con `strtof(number, &end)`.
      const float value = strtof(number, &end);
      // [Guía] Comprueba `end == number || *end || !isfinite(value)`. Si se cumple, ejecuta el bloque asociado.
      if (end == number || *end || !isfinite(value)) {
        // [Guía] Completa la declaración o expresión con `++rejectedAcks_; ackStatus_ = "REJECT_VALUE"; return
        // [Guía] true`.
        ++rejectedAcks_; ackStatus_ = "REJECT_VALUE"; return true;
      }
      // [Guía] Comprueba `pending_ == RESISTANCE && !strcmp(kind, ) && fabsf(value - sentValue_) < 0.0011f`. Si
      // [Guía] se cumple, ejecuta el bloque asociado. Aquí intervienen tipo de transacción que espera respuesta.
      if (pending_ == RESISTANCE && !strcmp(kind, "R") && fabsf(value - sentValue_) < 0.0011f) {
        // [Guía] Actualiza `resistenciaConfirmadaOhm` con `value`.
        resistenciaConfirmadaOhm = value;
        // [Guía] Actualiza `resistenciaConfirmada` con `true`.
        resistenciaConfirmada = true;
        // Una consigna nueva durante el viaje del ACK sigue pendiente.
        // [Guía] Actualiza `enviarNuevaResistencia` con `fabsf(resistenciaOhm - value) >= 0.0011f`.
        enviarNuevaResistencia = fabsf(resistenciaOhm - value) >= 0.0011f;
        // [Guía] Actualiza `pending_` con `NONE`. Este dato representa tipo de transacción que espera respuesta.
        pending_ = NONE;
        // [Guía] Cuenta una confirmación de resistencia que superó las comprobaciones del protocolo y actualiza
        // [Guía] su etiqueta de diagnóstico.
        ++acceptedAcks_; ackStatus_ = "ACCEPT_R";
        // [Guía] Actualiza `estado` con `"R_CONFIRMADA"`.
        estado = "R_CONFIRMADA";
      // [Guía] Comprueba `pending_ == LOAD && !strcmp(kind, ) && value == sentValue_`. Si se cumple, ejecuta el
      // [Guía] bloque asociado. Aquí intervienen tipo de transacción que espera respuesta.
      } else if (pending_ == LOAD && !strcmp(kind, "LOAD") && value == sentValue_) {
        // [Guía] Actualiza `consumoConfirmado` con `value == 1`.
        consumoConfirmado = value == 1;
        // [Guía] Actualiza `estadoConsumoValido` con `true`.
        estadoConsumoValido = true;
        // [Guía] Actualiza `loadDirty_` con `consumoSolicitado != consumoConfirmado`.
        loadDirty_ = consumoSolicitado != consumoConfirmado;
        // [Guía] Actualiza `pending_` con `NONE`. Este dato representa tipo de transacción que espera respuesta.
        pending_ = NONE;
        // [Guía] Registra la aceptación de la orden de consumo. La confirmación de protocolo se complementa con
        // [Guía] la lectura física de corriente.
        ++acceptedAcks_; ackStatus_ = "ACCEPT_LOAD";
        // [Guía] Actualiza `estado` con `consumoConfirmado ? "ON_CONFIRMADO" : "OFF_CONFIRMADO"`.
        estado = consumoConfirmado ? "ON_CONFIRMADO" : "OFF_CONFIRMADO";
      // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
      } else { ++rejectedAcks_; ackStatus_ = "REJECT_KIND_OR_VALUE"; }
      // [Guía] Entrega `true` al llamador.
      return true;
    }
    // [Guía] Comprueba `!strcmp(line, )`. Si se cumple, ejecuta el bloque asociado.
    if (!strcmp(line, "ENABLE")) {
      // [Guía] Actualiza `habilitarEmulador` con `true`.
      habilitarEmulador = true;
      // [Guía] Actualiza `lastAllowed_` con `true`.
      lastAllowed_ = true;
      // [Guía] Actualiza `enviarNuevaResistencia` con `true`.
      enviarNuevaResistencia = true;
      // [Guía] Actualiza `estado` con `"HABILITADO"`.
      estado = "HABILITADO";
      // [Guía] Entrega `true` al llamador.
      return true;
    }
    // [Guía] Comprueba `!strcmp(line, )`. Si se cumple, ejecuta el bloque asociado.
    if (!strcmp(line, "DISABLE")) {
      // [Guía] Actualiza `habilitarEmulador` con `false`.
      habilitarEmulador = false;
      // [Guía] Actualiza `lastAllowed_` con `false`.
      lastAllowed_ = false;
      // [Guía] Actualiza `enviarNuevaResistencia` con `false`.
      enviarNuevaResistencia = false;
      // [Guía] Actualiza `resistenciaConfirmada` con `false`.
      resistenciaConfirmada = false;
      // [Guía] Guarda la orden solicitada de habilitar o deshabilitar el consumo de la carga.
      solicitarConsumo(false);
      // [Guía] Entrega `true` al llamador.
      return true;
    }
    // [Guía] Comprueba `!strcmp(line, ) || !strcmp(line, )`. Si se cumple, ejecuta el bloque asociado.
    if (!strcmp(line, "ON") || !strcmp(line, "OFF")) {
      // [Guía] Guarda la orden solicitada de habilitar o deshabilitar el consumo de la carga.
      solicitarConsumo(!strcmp(line, "ON"));
      // [Guía] Entrega `true` al llamador.
      return true;
    }
    // Comandos de resistencia son explicitamente R=..., nunca letras sueltas.
    // [Guía] Comprueba `!strncmp(line, , 2)`. Si se cumple, ejecuta el bloque asociado.
    if (!strncmp(line, "R=", 2)) {
      // [Guía] Declara `end` para conservar end en este ámbito.
      char* end;
      // [Guía] Actualiza `value` con `strtof(line + 2, &end)`.
      const float value = strtof(line + 2, &end);
      // [Guía] Comprueba `end == line + 2 || *end`. Si se cumple, ejecuta `estado = "R_RECHAZADA"`.
      if (end == line + 2 || *end) estado = "R_RECHAZADA";
      // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
      else configurarResistencia(value);
      // [Guía] Entrega `true` al llamador.
      return true;
    }
    // [Guía] Entrega `false` al llamador.
    return false;
  }

  // Construir una sola orden en el buffer TX compartido, entre reportes completos.
  // ID correlaciona cada ACK: una respuesta atrasada no confirma otra consigna.
  // [Guía] Elige una orden pendiente y construye la línea que el Arduino enviará al puente. Recibe `buffer`,
  // [Guía] `capacity`, `now`, `retryPending`.
  size_t prepareRequest(char* buffer, size_t capacity, uint32_t now, bool retryPending = false) {
    // [Guía] Comprueba `!enlace`. Si se cumple, termina la función en esta rama.
    if (!enlace) return 0;
    // [Guía] Comprueba `pending_ != NONE`. Si se cumple, ejecuta el bloque asociado. Aquí intervienen tipo de
    // [Guía] transacción que espera respuesta.
    if (pending_ != NONE) {
      // Solo el llamador M1 habilita esta recuperacion. OFF cancela pending_;
      // no reconstruir una transaccion anterior usando la consigna actual.
      // [Guía] Comprueba `!retryPending || uint32_t(now - sentAt_) >= ACK_TIMEOUT_MS`. Si se cumple, ejecuta el
      // [Guía] bloque asociado.
      if (!retryPending || uint32_t(now - sentAt_) >= ACK_TIMEOUT_MS
          // [Guía] Añade una alternativa suficiente dentro de la condición compuesta.
          || retries_ >= ACK_MAX_RETRIES || uint32_t(now - lastAttemptAt_) < ACK_RETRY_MS) return 0;
      // [Guía] Completa la declaración o expresión con `++retries_; lastAttemptAt_ = now`.
      ++retries_; lastAttemptAt_ = now;
      // [Guía] Entrega `formatRequest(buffer, capacity)` al llamador.
      return formatRequest(buffer, capacity);
    }
    // [Guía] Comprueba `loadDirty_ && !consumoSolicitado`. Si se cumple, ejecuta el bloque asociado.
    if (loadDirty_ && !consumoSolicitado) {
      // [Guía] Actualiza `pending_` con `LOAD`. Este dato representa tipo de transacción que espera respuesta.
      pending_ = LOAD;
      // [Guía] Actualiza `sentValue_` con `0`.
      sentValue_ = 0;
    // [Guía] Comprueba `habilitarEmulador && enviarNuevaResistencia`. Si se cumple, ejecuta el bloque asociado.
    } else if (habilitarEmulador && enviarNuevaResistencia) {
      // [Guía] Comprueba `!isfinite(resistenciaOhm) || resistenciaOhm < R_MIN || resistenciaOhm > R_MAX`. Si se
      // [Guía] cumple, ejecuta el bloque asociado.
      if (!isfinite(resistenciaOhm) || resistenciaOhm < R_MIN || resistenciaOhm > R_MAX) {
        // [Guía] Actualiza `enviarNuevaResistencia` con `false`.
        enviarNuevaResistencia = false;
        // [Guía] Actualiza `estado` con `"R_RECHAZADA"`.
        estado = "R_RECHAZADA";
        // [Guía] Entrega `0` al llamador.
        return 0;
      }
      // [Guía] Actualiza `pending_` con `RESISTANCE`. Este dato representa tipo de transacción que espera
      // [Guía] respuesta.
      pending_ = RESISTANCE;
      // [Guía] Actualiza `sentValue_` con `resistenciaOhm`.
      sentValue_ = resistenciaOhm;
    // [Guía] Comprueba `loadDirty_ && consumoSolicitado && habilitarEmulador && resistenciaConfirmada`. Si se
    // [Guía] cumple, ejecuta el bloque asociado.
    } else if (loadDirty_ && consumoSolicitado && habilitarEmulador && resistenciaConfirmada) {
      // [Guía] Actualiza `pending_` con `LOAD`. Este dato representa tipo de transacción que espera respuesta.
      pending_ = LOAD;
      // [Guía] Actualiza `sentValue_` con `1`.
      sentValue_ = 1;
    // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
    } else return 0;
    //0 queda reservado a ausencia de orden. Al desbordar uint32_t, saltarlo.
    // [Guía] Suma una unidad a `id_` para contabilizar el evento de esta rama.
    ++id_;
    // [Guía] Comprueba `!id_`. Si se cumple, ejecuta `++id_`.
    if (!id_) ++id_;
    // [Guía] Actualiza `sentAt_` con `now`.
    sentAt_ = now;
    // [Guía] Actualiza `lastAttemptAt_` con `now; retries_ = 0`.
    lastAttemptAt_ = now; retries_ = 0;
    // [Guía] Entrega `formatRequest(buffer, capacity)` al llamador.
    return formatRequest(buffer, capacity);
  }

 // [Guía] Desde aquí se declaran los datos y auxiliares internos de la clase.
 private:
  // Formatear una propuesta nueva o repetir exactamente ID/tipo/valor en vuelo.
  // [Guía] Serializa el identificador, operación y valor de la solicitud en el búfer provisto. Recibe `buffer`,
  // [Guía] `capacity`.
  size_t formatRequest(char* buffer, size_t capacity) {
    // Enteros escalados evitan depender de printf con soporte float en Arduino.
    // [Guía] Actualiza `scaled` con `long(sentValue_ * 1000.0f + 0.5f)`.
    const long scaled = long(sentValue_ * 1000.0f + 0.5f);
    // [Guía] Actualiza `n` con `pending_ == RESISTANCE`.
    const int n = pending_ == RESISTANCE
      // [Guía] Define una de las dos salidas de la expresión condicional.
      ? snprintf(buffer, capacity, "BK_REQ=%lu,R,%ld.%03ld\r\n", (unsigned long)id_, scaled / 1000, scaled % 1000)
      // [Guía] Define una de las dos salidas de la expresión condicional.
      : snprintf(buffer, capacity, "BK_REQ=%lu,LOAD,%u\r\n", (unsigned long)id_, unsigned(sentValue_));
    // [Guía] Comprueba `n <= 0 || size_t(n) >= capacity`. Si se cumple, ejecuta el bloque asociado.
    if (n <= 0 || size_t(n) >= capacity) {
      // [Guía] Invalida la evidencia de enlace y confirmaciones cuando ya no es fiable.
      invalidate("TX_ERROR");
      // [Guía] Entrega `0` al llamador.
      return 0;
    }
    // [Guía] Entrega `size_t(n)` al llamador.
    return size_t(n);
  }

  // Una sola transaccion en vuelo; loadDirty_ recuerda un cambio posterior.
  // sentValue_ congela el valor enviado, aunque se cambie la consigna publica.
  // [Guía] Reúne bajo `Pending` las declaraciones que comparten este tipo o ámbito.
  enum Pending { NONE, RESISTANCE, LOAD };
  // [Guía] Actualiza `pending_` con `NONE`. Este dato representa tipo de transacción que espera respuesta.
  Pending pending_ = NONE;
  // [Guía] Actualiza `id_` con `0, sentAt_ = 0, lastLink_ = 0`.
  uint32_t id_ = 0, sentAt_ = 0, lastLink_ = 0;
  // [Guía] Actualiza `lastAttemptAt_` con `0, acceptedAcks_ = 0, rejectedAcks_ = 0`.
  uint32_t lastAttemptAt_ = 0, acceptedAcks_ = 0, rejectedAcks_ = 0;
  // [Guía] Actualiza `retries_` con `0`.
  uint8_t retries_ = 0;
  // [Guía] Actualiza `ackStatus_` con `"NO_ACK_RECEIVED"`.
  const char* ackStatus_ = "NO_ACK_RECEIVED";
  // [Guía] Actualiza `sentValue_` con `0`.
  float sentValue_ = 0;
  // [Guía] Actualiza `loadDirty_` con `false`.
  bool loadDirty_ = false;
  // [Guía] Actualiza `lastAllowed_` con `true`.
  bool lastAllowed_ = true;

  // Perder confirmacion NO demuestra que la carga fisica este apagada.
  // Solicitar OFF; una reconexion exigira HELLO y nueva confirmacion de R.
  // [Guía] Invalida la evidencia de enlace y confirmaciones cuando ya no es fiable. Recibe `reason`.
  void invalidate(const char* reason) {
    // [Guía] Actualiza `pending_` con `NONE`. Este dato representa tipo de transacción que espera respuesta.
    pending_ = NONE;
    // [Guía] Actualiza `resistenciaConfirmada` con `false`.
    resistenciaConfirmada = false;
    // [Guía] Actualiza `estadoConsumoValido` con `false`.
    estadoConsumoValido = false;
    // [Guía] Actualiza `consumoSolicitado` con `consumoConfirmado = false`.
    consumoSolicitado = consumoConfirmado = false;
    // [Guía] Actualiza `loadDirty_` con `true`.
    loadDirty_ = true;
    // [Guía] Actualiza `enviarNuevaResistencia` con `false`.
    enviarNuevaResistencia = false;
    // [Guía] Actualiza `estado` con `reason`.
    estado = reason;
  }
};
