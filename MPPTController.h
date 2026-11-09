// [Guía] Implementa perturbación y observación con ventanas de asentamiento y promedio de potencia. La posición
// [Guía] de búsqueda puede representar duty o una referencia normalizada.
// [Guía] Evita procesar este encabezado dos veces en una misma unidad de compilación.
#pragma once
/* SEGUIMIENTO DE POTENCIA, NO REGULACION DE Vpv NI DE Vbus
 * Cada muestra es V en voltios e I en amperios; power_=media(V*I) en vatios.
 * duty_ es una PROPUESTA0..1. La EMS aplica slew y reinicia asentamiento al
 * llegar; esta clase no sabe si una compuerta fisica respondio correctamente.
 * baseDuty_/basePower_ retienen la ultima mejora confirmada. probing_ indica
 * que se compara una perturbacion; returning_ vuelve por pasos a esa base.
 * El cambio de potencia tambien puede deberse a irradiancia, Q2 o carga: P&O
 * no identifica causalidad ni garantiza MPP global bajo sombreado parcial.
 * Todas las restas temporales usan uint32_t para tolerar overflow de micros.
 */
// [Guía] Incorpora las declaraciones de <stdint.h> antes de usar sus tipos y funciones.
#include <stdint.h>
// [Guía] Incorpora las declaraciones de <math.h> antes de usar sus tipos y funciones.
#include <math.h>

// P&O sobre duty: cada prueba se compara con una ventana base a duty fijo.
// Esta clase propone el duty; el .ino aplica la PWM y verifica protecciones.
// No regula Vbus: busca aumentar la potencia medida en la entrada PV.
// [Guía] Reúne bajo `MPPTController` las declaraciones que comparten este tipo o ámbito.
class MPPTController {
 // [Guía] Desde aquí se declaran los tipos y operaciones que usa el llamador.
 public:
  // Iniciar desde el duty aplicado (0..1), dentro de los limites del constructor.
  // now es micros(); comenzar bajando duty no presupone donde esta el maximo.
  // [Guía] Inicializa el P&O desde la posición aplicada y abre una ventana para obtener la potencia de
  // [Guía] referencia. Recibe `duty`, `now`.
  void begin(float duty, uint32_t now) {
    // [Guía] Actualiza `activeUs_` con `0`. Este dato representa tiempo útil acumulado de búsqueda en µs.
    activeUs_ = 0;
    // [Guía] Actualiza `lastClockUs_` con `now`.
    lastClockUs_ = now;
    // [Guía] Actualiza `paused_` con `false`.
    paused_ = false;
    // [Guía] Actualiza `duty_` con `baseDuty_ = duty`.
    duty_ = baseDuty_ = duty;
    // [Guía] Actualiza `direction_` con `-1`. Este dato representa sentido de la perturbación.
    direction_ = -1;
    // [Guía] Actualiza `step_` con `initialStep_`.
    step_ = initialStep_;
    // [Guía] Actualiza `power_` con `0.0f`.
    power_ = 0.0f;
    // [Guía] Actualiza `probing_` con `haveBase_ = false`.
    probing_ = haveBase_ = false;
    // [Guía] Actualiza `returning_` con `false`.
    returning_ = false;
    // [Guía] Actualiza `confirmation_` con `0`.
    confirmation_ = 0;
    // [Guía] Actualiza `ambiguousWindows_` con `0`.
    ambiguousWindows_ = 0;
    // [Guía] Actualiza `probeWindows_` con `0`.
    probeWindows_ = 0;
    // [Guía] Actualiza `updates_` con `flatSteps_ = 0`. Este dato representa contador de actualizaciones.
    updates_ = flatSteps_ = 0;
    // [Guía] Actualiza `deltaPower_` con `basePower_ = referencePower_ = 0`. Este dato representa diferencia de
    // [Guía] potencia respecto a la base, en W.
    deltaPower_ = basePower_ = referencePower_ = 0;
    // [Guía] Actualiza `decision_` con `"ACQUIRE"`.
    decision_ = "ACQUIRE";
    // [Guía] Borra el acumulador y abre una ventana para la nueva posición aplicada.
    restartWindow(now);
  }

  // Llamar con cada muestra PV: voltios, amperios y tiempo en microsegundos.
  // true significa nuevo duty; false conserva la salida, no indica un fallo.
  // No usa delay: mientras espera, el programa puede atender otras tareas.
  // [Guía] Acumula Vpv por Ipv después del asentamiento. Compara el promedio con la base, califica la diferencia
  // [Guía] y decide confirmar, repetir o invertir la perturbación. El resultado es una decisión booleana para el
  // [Guía] llamador. Recibe `voltage`, `current`, `now`.
  bool sample(float voltage, float current, uint32_t now) {
    // [Guía] Si falta una lectura utilizable, suspende el reloj de búsqueda y conserva la posición. Este paso no
    // [Guía] produjo otra consigna.
    if (!isfinite(voltage) || !isfinite(current)) { pause(now); return false; }
    // Medir tiempo de adquisicion permitido, no tiempo atravesado en pausa.
    // Las pausas breves conservan la ventana y no impiden completar P&O.
    // [Guía] Acumula el tiempo efectivo de una muestra aceptada. La resta sin signo mantiene el intervalo en un
    // [Guía] cruce normal del contador.
    activeUs_ += uint32_t(now - lastClockUs_);
    // [Guía] Actualiza `lastClockUs_` con `now`.
    lastClockUs_ = now;
    // [Guía] Al volver de una pausa, actualiza el diagnóstico para indicar que la búsqueda retoma el
    // [Guía] asentamiento.
    if (paused_) decision_ = "SETTLING";
    // [Guía] Actualiza `paused_` con `false`.
    paused_ = false;
    // La resta unsigned permite atravesar el desbordamiento de micros().
    // Descartar el inicio de cada ventana para no evaluar el transitorio.
    // [Guía] Descarta esta muestra para el promedio si el punto perturbado todavía está en el tiempo de
    // [Guía] asentamiento.
    if (uint32_t(activeUs_ - windowStart_) < settleUs_) return false;
    // [Guía] Suma la tensión PV de esta muestra. Su promedio servirá para validar que el panel produce una señal
    // [Guía] útil.
    sumV_ += voltage;
    // [Guía] Suma la corriente PV de esta misma muestra, para evaluar junto con la tensión la disponibilidad de
    // [Guía] potencia.
    sumI_ += current;
    // [Guía] Acumula el producto simultáneo Vpv por Ipv. Así la potencia de comparación representa el promedio de
    // [Guía] potencias instantáneas.
    sumP_ += voltage * current; // Promedio de productos V*I, potencia en W.
    // [Guía] Cuenta una muestra en los tres acumuladores. Este mismo divisor mantiene alineados los promedios de
    // [Guía] tensión, corriente y potencia.
    ++count_;
    // [Guía] Comprueba `uint32_t(activeUs_ - windowStart_) < settleUs_ + averageUs_ || count_ < 100`. Si se
    // [Guía] cumple, ejecuta el bloque asociado. Aquí intervienen tiempo útil acumulado de búsqueda en µs.
    if (uint32_t(activeUs_ - windowStart_) < settleUs_ + averageUs_ || count_ < 100)
      // [Guía] Entrega `false` al llamador.
      return false;
    // Tres medias sobre LAS MISMAS muestras: potencia para comparar, V/I para
    // validar que hay una senal util. No usar media(V)*media(I) como potencia.
    // [Guía] Calcula en vatios la potencia promedio del punto que acaba de observarse.
    power_ = sumP_ / count_;
    // [Guía] Actualiza `v` con `sumV_ / count_, i = sumI_ / count_`.
    const float v = sumV_ / count_, i = sumI_ / count_;
    // Con poca senal, suspender busqueda y descartar la base anterior.
    // Esto mantiene el duty; NO sustituye una proteccion de desconexion.
    // Permitir buscar desde los 55 mA de entrada EMS, sin evaluar corriente
    // cercana a cero. La EMS confirma desconexion por debajo de 30 mA.
    // [Guía] Comprueba `v < 1.0f || i < 0.030f`. Si se cumple, ejecuta el bloque asociado.
    if (v < 1.0f || i < 0.030f) {
      // [Guía] Actualiza `haveBase_` con `probing_ = returning_ = false`.
      haveBase_ = probing_ = returning_ = false;
      // [Guía] Actualiza `power_` con `0`.
      power_ = 0;
      // [Guía] Actualiza `confirmation_` con `0`.
      confirmation_ = 0;
      // [Guía] Actualiza `ambiguousWindows_` con `0`.
      ambiguousWindows_ = 0;
      // [Guía] Actualiza `decision_` con `"LOW_SIGNAL"`.
      decision_ = "LOW_SIGNAL";
      // [Guía] Borra el acumulador y abre una ventana para la nueva posición aplicada.
      restartWindow(now);
      // [Guía] Entrega `false` al llamador.
      return false;
    }
    // [Guía] Actualiza `previousDuty` con `duty_`.
    const float previousDuty = duty_;
    // Primera ventana valida: guardar el punto con el que comparar pruebas.
    // [Guía] Comprueba `returning_`. Si se cumple, ejecuta el bloque asociado.
    if (returning_) {
      // [Guía] Retoma la posición de referencia después de una prueba que no justificó conservar el
      // [Guía] desplazamiento.
      returnToBase();
    // [Guía] Comprueba `!haveBase_`. Si se cumple, ejecuta el bloque asociado.
    } else if (!haveBase_) {
      // [Guía] Actualiza `basePower_` con `power_`.
      basePower_ = power_;
      // [Guía] Actualiza `referencePower_` con `power_`. Este dato representa potencia de referencia en W.
      referencePower_ = power_;
      // [Guía] Actualiza `baseDuty_` con `duty_`. Este dato representa posición de búsqueda aceptada como base.
      baseDuty_ = duty_;
      // [Guía] Actualiza `haveBase_` con `true`.
      haveBase_ = true;
      // [Guía] Propone el siguiente punto de prueba con el paso y sentido actuales, respetando los extremos
      // [Guía] permitidos.
      startProbe();
      // [Guía] Actualiza `decision_` con `"PROBE"`.
      decision_ = "PROBE";
    // [Guía] Comprueba `probing_`. Si se cumple, ejecuta el bloque asociado.
    } else if (probing_) {
      // Banda de decision: mayor entre umbral absoluto y relativo.
      // No representa la exactitud garantizada de los sensores.
      // [Guía] Elige el mayor de un umbral absoluto en W y un umbral proporcional a la base. La banda evita
      // [Guía] decidir por variaciones pequeñas.
      const float tolerance = fmaxf(POWER_DEADBAND_W, fabsf(basePower_) * RELATIVE_DEADBAND);
      // Guardar referencia de la comparacion para que MPPT_DIAG pueda mostrar
      // deltaP aunque despues esta llamada actualice la mejor base.
      // [Guía] Actualiza `referencePower_` con `basePower_`. Este dato representa potencia de referencia en W.
      referencePower_ = basePower_;
      // [Guía] Calcula cuánto cambió la potencia en W respecto al punto aceptado que se está usando como base.
      deltaPower_ = power_ - referencePower_;
      // [Guía] Clasifica como ganancia cuando el nuevo promedio supera la base más la banda. El segundo término
      // [Guía] distingue pérdida de diferencia ambigua.
      const int result = power_ > basePower_ + tolerance ? 1 :
                         // [Guía] Clasifica como pérdida por debajo de la banda y como diferencia ambigua cuando
                         // [Guía] el promedio queda dentro de ella.
                         (power_ < basePower_ - tolerance ? -1 : 0);
      // [Guía] Suma una unidad a `probeWindows_` para contabilizar el evento de esta rama.
      ++probeWindows_;
      // Exigir dos ventanas consecutivas con el mismo resultado.
      // [Guía] Guarda la primera ventana con un signo nuevo para exigir que una segunda ventana confirme la misma
      // [Guía] dirección del cambio.
      if (result != 0 && confirmation_ != result) {
        // [Guía] Actualiza `confirmation_` con `result`.
        confirmation_ = result;
        // [Guía] Actualiza `ambiguousWindows_` con `0`.
        ambiguousWindows_ = 0;
        // Si el ruido alterna el signo, no esperar indefinidamente a duty fijo.
        // [Guía] Limita el número de intentos en esta posición. Si el ruido impide confirmar el signo, permite
        // [Guía] retomar la base.
        if (probeWindows_ >= MAX_PROBE_WINDOWS) {
          // [Guía] Retoma la posición de referencia después de una prueba que no justificó conservar el
          // [Guía] desplazamiento.
          returnToBase();
          // [Guía] Actualiza `decision_` con `"REBASE_NOISE"`.
          decision_ = "REBASE_NOISE";
          // [Guía] Borra el acumulador y abre una ventana para la nueva posición aplicada.
          restartWindow(now);
          // [Guía] Comprueba `duty_ != previousDuty`. Si se cumple, ejecuta `++updates_`.
          if (duty_ != previousDuty) ++updates_;
          // [Guía] Informa si cambió la consigna. La potencia calculada corresponde a la ventana que terminó y
          // [Guía] puede anteceder al nuevo punto.
          return duty_ != previousDuty;
        }
        // [Guía] Actualiza `decision_` con `result > 0 ? "CONFIRM_GAIN" : "CONFIRM_LOSS"`.
        decision_ = result > 0 ? "CONFIRM_GAIN" : "CONFIRM_LOSS";
        // [Guía] Borra el acumulador y abre una ventana para la nueva posición aplicada.
        restartWindow(now);
        // [Guía] Entrega `false` al llamador.
        return false;
      }
      // Mejora confirmada: aceptar este punto y explorar en el mismo sentido.
      // [Guía] Acepta una mejora confirmada y deja que la búsqueda continúe en el mismo sentido.
      if (result > 0) {
        // [Guía] Actualiza `basePower_` con `power_`.
        basePower_ = power_;
        // [Guía] Actualiza `baseDuty_` con `duty_`. Este dato representa posición de búsqueda aceptada como base.
        baseDuty_ = duty_;
        // [Guía] Aumenta el paso ante una ganancia confirmada para avanzar más rápido. Conserva el límite
        // [Guía] superior configurado.
        step_ = fminf(maximumStep_, step_ * 1.5f);
        // [Guía] Propone el siguiente punto de prueba con el paso y sentido actuales, respetando los extremos
        // [Guía] permitidos.
        startProbe();
        // [Guía] Actualiza `decision_` con `"GAIN"`.
        decision_ = "GAIN";
      // [Guía] Una pérdida confirmada lleva la búsqueda al punto base y cambia el sentido de la próxima
      // [Guía] perturbación.
      } else if (result < 0) {
        // Empeora: recuperar la base y afinar el paso en sentido contrario.
        // [Guía] Reduce el paso al invertir el sentido para afinar alrededor de la región de mayor potencia.
        step_ = fmaxf(minimumStep_, step_ * 0.5f);
        // [Guía] Actualiza `direction_` con `-direction_`. Este dato representa sentido de la perturbación.
        direction_ = -direction_;
        // [Guía] Retoma la posición de referencia después de una prueba que no justificó conservar el
        // [Guía] desplazamiento.
        returnToBase();
        // [Guía] Actualiza `decision_` con `"REVERSE"`.
        decision_ = "REVERSE";
      // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
      } else {
        // [Guía] Actualiza `confirmation_` con `0`.
        confirmation_ = 0;
        // No confundir una diferencia pequena con una perdida de potencia.
        // [Guía] Comprueba `++ambiguousWindows_ < FLAT_CONFIRM_WINDOWS`. Si se cumple, ejecuta el bloque
        // [Guía] asociado.
        if (++ambiguousWindows_ < FLAT_CONFIRM_WINDOWS
            // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
            && probeWindows_ < MAX_PROBE_WINDOWS) {
          // [Guía] Actualiza `decision_` con `"WAIT_DEADBAND"`.
          decision_ = "WAIT_DEADBAND";
          // [Guía] Borra el acumulador y abre una ventana para la nueva posición aplicada.
          restartWindow(now);
          // [Guía] Entrega `false` al llamador.
          return false;
        }
        // Avanzar desde la ultima prueba, NO volver siempre a la misma base.
        // Conservar basePower_/baseDuty_: varias perdidas pequenas acumuladas
        // deben poder ordenar regreso, aunque cada una este dentro de la banda.
        // [Guía] Amplía el paso en una zona plana tras agotar las ventanas de confirmación, para explorar fuera
        // [Guía] de la banda.
        step_ = fminf(maximumStep_, step_ * 2.0f);
        // [Guía] Propone el siguiente punto de prueba con el paso y sentido actuales, respetando los extremos
        // [Guía] permitidos.
        startProbe(duty_);
        // [Guía] Suma una unidad a `flatSteps_` para contabilizar el evento de esta rama.
        ++flatSteps_;
        // [Guía] Actualiza `decision_` con `"EXPLORE_FLAT"`.
        decision_ = "EXPLORE_FLAT";
      }
    // [Guía] Atiende el caso alternativo cuando las comprobaciones anteriores no se cumplieron.
    } else {
      // Refrescar base antes de probar el otro sentido.
      // [Guía] Actualiza `basePower_` con `power_`.
      basePower_ = power_;
      // [Guía] Propone el siguiente punto de prueba con el paso y sentido actuales, respetando los extremos
      // [Guía] permitidos.
      startProbe();
      // [Guía] Actualiza `decision_` con `"PROBE"`.
      decision_ = "PROBE";
    }
    // [Guía] Borra el acumulador y abre una ventana para la nueva posición aplicada.
    restartWindow(now);
    // [Guía] Comprueba `duty_ != previousDuty`. Si se cumple, ejecuta `++updates_`.
    if (duty_ != previousDuty) ++updates_;
    // [Guía] Informa si cambió la consigna. La potencia calculada corresponde a la ventana que terminó y puede
    // [Guía] anteceder al nuevo punto.
    return duty_ != previousDuty;
  }

  // Ultima orden y ultima potencia promediada; pueden ser de puntos distintos
  // justo despues de ordenar una perturbacion. probing() indica prueba activa.
  // [Guía] Expone `duty_` al módulo que consulta ciclo de trabajo.
  float duty() const { return duty_; }
  // Cambio EXTERNO de carga: invalidar comparacion de potencia, no reiniciar
  // direccion, paso adaptativo, contadores ni reloj activo. position tiene
  // las mismas unidades normalizadas de begin(). direction=0 conserva sentido.
  // [Guía] Cambia el punto de partida de la búsqueda y obliga a recalcular una referencia de potencia. Recibe
  // [Guía] `position`, `now`, `direction`.
  void rebase(float position, uint32_t now, int direction = 0) {
    // [Guía] Actualiza `duty_` con `baseDuty_ = fminf(maximum_, fmaxf(minimum_, position))`.
    duty_ = baseDuty_ = fminf(maximum_, fmaxf(minimum_, position));
    // [Guía] Comprueba `direction == 1 || direction == -1`. Si se cumple, ejecuta `direction_ = direction`.
    if (direction == 1 || direction == -1) direction_ = direction;
    // [Guía] Actualiza `haveBase_` con `probing_ = returning_ = false`.
    haveBase_ = probing_ = returning_ = false;
    // [Guía] Actualiza `confirmation_` con `ambiguousWindows_ = probeWindows_ = 0`.
    confirmation_ = ambiguousWindows_ = probeWindows_ = 0;
    // [Guía] Actualiza `power_` con `basePower_ = referencePower_ = deltaPower_ = 0`.
    power_ = basePower_ = referencePower_ = deltaPower_ = 0;
    // [Guía] Actualiza `paused_` con `false; lastClockUs_ = now`.
    paused_ = false; lastClockUs_ = now;
    // [Guía] Actualiza `decision_` con `"CONTEXT_REBASE"; restartWindow(now)`.
    decision_ = "CONTEXT_REBASE"; restartWindow(now);
  }
  // [Guía] Expone `power_` al módulo que consulta potencia.
  float power() const { return power_; }
  // [Guía] Expone `probing_` al módulo que consulta probing.
  bool probing() const { return probing_; }
  // Diagnosticos: decision es el ultimo resultado, no una prueba de convergencia.
  // updates cuenta propuestas distintas; flatSteps cuenta exploraciones neutras.
  // step es fraccion de duty; el monitor la convierte a puntos porcentuales.
  // [Guía] Expone `decision_` al módulo que consulta decision.
  const char* decision() const { return decision_; }
  // [Guía] Expone `step_` al módulo que consulta paso.
  float step() const { return step_; }
  // [Guía] Devuelve diferencia de potencia respecto a la base, en W.
  float deltaPower() const { return deltaPower_; }
  // [Guía] Devuelve potencia de referencia en W.
  float basePower() const { return referencePower_; }
  // [Guía] Expone `flatSteps_` al módulo que consulta flat pasos.
  uint32_t flatSteps() const { return flatSteps_; }
  // [Guía] Devuelve contador de actualizaciones.
  uint32_t updates() const { return updates_; }
  // Una capa de slew puede tardar en aplicar la propuesta; empezar a medir
  // asentamiento desde la llegada, conservando base, direccion y prueba P&O.
  // [Guía] Comienza de nuevo el asentamiento desde el instante recibido. Recibe `now`.
  void restartSettling(uint32_t now) { lastClockUs_ = now; restartWindow(now); }
  // Pausa corta: no sumar medidas ni tiempo, pero conservar direccion/base.
  // Una pausa larga invalida comparaciones entre condiciones posiblemente nuevas.
  // [Guía] Suspende el cómputo de tiempo útil cuando la muestra no se debe atribuir a la perturbación. Recibe
  // [Guía] `now`.
  void pause(uint32_t now) {
    // [Guía] Comprueba `uint32_t(now - lastClockUs_) > LONG_PAUSE_US`. Si se cumple, ejecuta el bloque asociado.
    if (uint32_t(now - lastClockUs_) > LONG_PAUSE_US) {
      // [Guía] Actualiza `haveBase_` con `probing_ = returning_ = false`.
      haveBase_ = probing_ = returning_ = false;
      // [Guía] Actualiza `power_` con `0`.
      power_ = 0;
      // [Guía] Actualiza `confirmation_` con `ambiguousWindows_ = 0`.
      confirmation_ = ambiguousWindows_ = 0;
      // [Guía] Borra el acumulador y abre una ventana para la nueva posición aplicada.
      restartWindow(now);
    }
    // [Guía] Comprueba `!paused_`. Si se cumple, ejecuta el bloque asociado.
    if (!paused_) { paused_ = true; pauseStart_ = now; }
    // [Guía] Comprueba `uint32_t(now - pauseStart_) > LONG_PAUSE_US`. Si se cumple, ejecuta el bloque asociado.
    if (uint32_t(now - pauseStart_) > LONG_PAUSE_US) {
      // [Guía] Actualiza `haveBase_` con `probing_ = returning_ = false`.
      haveBase_ = probing_ = returning_ = false;
      // [Guía] Actualiza `power_` con `0`.
      power_ = 0;
      // [Guía] Actualiza `confirmation_` con `ambiguousWindows_ = 0`.
      confirmation_ = ambiguousWindows_ = 0;
      // [Guía] Borra el acumulador y abre una ventana para la nueva posición aplicada.
      restartWindow(now);
    }
    // [Guía] Actualiza `decision_` con `"PAUSED"`.
    decision_ = "PAUSED";
    // [Guía] Actualiza `lastClockUs_` con `now`.
    lastClockUs_ = now;
  }
  // [Guía] Expone `activeUs_ - windowStart_` al módulo que consulta ventana active µs.
  uint32_t windowActiveUs() const { return activeUs_ - windowStart_; }
  // V39 conserva la ventana de700 ms y500 ms de promedio de V31.
  // La busqueda acelera por paso adaptativo y avance sobre zonas planas.
  // La planta debe asentarse en este tiempo: no implica validacion fisica.
  // [Guía] Completa la declaración o expresión con `static constexpr uint32_t SETTLE_US = 200000`.
  static constexpr uint32_t SETTLE_US = 200000;
  // [Guía] Completa la declaración o expresión con `static constexpr uint32_t AVERAGE_US = 500000`.
  static constexpr uint32_t AVERAGE_US = 500000;
  // [Guía] Completa la declaración o expresión con `static constexpr uint32_t LONG_PAUSE_US = 2000000`.
  static constexpr uint32_t LONG_PAUSE_US = 2000000;
  // [Guía] Completa la declaración o expresión con `static constexpr uint8_t FLAT_CONFIRM_WINDOWS = 2`.
  static constexpr uint8_t FLAT_CONFIRM_WINDOWS = 2;
  // [Guía] Completa la declaración o expresión con `static constexpr uint8_t MAX_PROBE_WINDOWS = 4`.
  static constexpr uint8_t MAX_PROBE_WINDOWS = 4;
  // Primera exploracion de1 punto porcentual; afinar hasta0.25 al invertir.
  // Las propuestas, incluido el regreso, avanzan como maximo2 puntos.
  // [Guía] Completa la declaración o expresión con `static constexpr float DUTY_STEP = 0.01f`.
  static constexpr float DUTY_STEP = 0.01f;
  // [Guía] Completa la declaración o expresión con `static constexpr float MIN_DUTY_STEP = 0.0025f`.
  static constexpr float MIN_DUTY_STEP = 0.0025f;
  // [Guía] Completa la declaración o expresión con `static constexpr float MAX_DUTY_STEP = 0.02f`.
  static constexpr float MAX_DUTY_STEP = 0.02f;
  // [Guía] Completa la declaración o expresión con `static constexpr float POWER_DEADBAND_W = 0.1f; // W.`.
  static constexpr float POWER_DEADBAND_W = 0.1f; // W.
  // [Guía] Completa la declaración o expresión con `static constexpr float RELATIVE_DEADBAND = 0.003f; // 0.3% de
  // [Guía] la potencia base.`.
  static constexpr float RELATIVE_DEADBAND = 0.003f; // 0.3% de la potencia base.
  // [Guía] Comprueba `SETTLE_US > 0 && AVERAGE_US >= 500000` durante la compilación.
  static_assert(SETTLE_US > 0 && AVERAGE_US >= 500000,
                "Keep averaging while reducing settling time");
  // [Guía] Comprueba `MIN_DUTY_STEP > 0 && MIN_DUTY_STEP <= DUTY_STEP` durante la compilación.
  static_assert(MIN_DUTY_STEP > 0 && MIN_DUTY_STEP <= DUTY_STEP
                // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
                && DUTY_STEP <= MAX_DUTY_STEP && MAX_DUTY_STEP <= 0.02f,
                "Invalid MPPT duty steps");
  // [Guía] Comprueba `FLAT_CONFIRM_WINDOWS >= 2 && MAX_PROBE_WINDOWS >= FLAT_CONFIRM_WINDOWS` durante la
  // [Guía] compilación.
  static_assert(FLAT_CONFIRM_WINDOWS >= 2 && MAX_PROBE_WINDOWS >= FLAT_CONFIRM_WINDOWS,
                "Invalid MPPT decision windows");

  // [Guía] Construye MPPTController con la configuración inicial indicada. Recibe `minimum`, `maximum`.
  MPPTController(float minimum, float maximum) : minimum_(minimum), maximum_(maximum) {}

  // Configurar SOLO antes de begin. La ruta historica conserva700ms/1pp.
  // En coordenada virtual de tension, el llamador convierte voltios a0..1;
  // estos pasos NO se aplican directamente a una compuerta PWM.
  // [Guía] Valida tiempos y magnitudes de paso antes de activar los ajustes del P&O. El resultado es una decisión
  // [Guía] booleana para el llamador. Recibe `settle`, `average`, `initial`, `fine`, `coarse`.
  bool configure(uint32_t settle, uint32_t average, float initial, float fine, float coarse) {
    // [Guía] Comprueba `!settle || average < 100000 || settle > 2000000 || average > 2000000`. Si se cumple,
    // [Guía] ejecuta el bloque asociado.
    if (!settle || average < 100000 || settle > 2000000 || average > 2000000
        // [Guía] Añade una alternativa suficiente dentro de la condición compuesta.
        || !isfinite(initial) || !isfinite(fine) || !isfinite(coarse)
        // [Guía] Añade una alternativa suficiente dentro de la condición compuesta.
        || fine <= 0 || fine > initial || initial > coarse || coarse > 0.1f) return false;
    // [Guía] Actualiza `settleUs_` con `settle; averageUs_ = average`.
    settleUs_ = settle; averageUs_ = average;
    // [Guía] Actualiza `initialStep_` con `initial; minimumStep_ = fine; maximumStep_ = coarse`.
    initialStep_ = initial; minimumStep_ = fine; maximumStep_ = coarse;
    // [Guía] Entrega `true` al llamador.
    return true;
  }

 // [Guía] Desde aquí se declaran los datos y auxiliares internos de la clase.
 private:
  // Reiniciar acumuladores sin modificar el duty ni la base de comparacion.
  // [Guía] Borra el acumulador y abre una ventana para la nueva posición aplicada. Recibe `now`.
  void restartWindow(uint32_t now) {
    // [Guía] Completa la declaración o expresión con `(void)now`.
    (void)now;
    // [Guía] Actualiza `windowStart_` con `activeUs_`.
    windowStart_ = activeUs_;
    // [Guía] Actualiza `sumV_` con `sumI_ = sumP_ = 0.0f`.
    sumV_ = sumI_ = sumP_ = 0.0f;
    // [Guía] Actualiza `count_` con `0`.
    count_ = 0;
  }
  // Proponer base +/- paso. Invertir sentido al encontrar un limite de duty.
  // [Guía] Propone el siguiente punto de prueba con el paso y sentido actuales, respetando los extremos
  // [Guía] permitidos.
  void startProbe() { startProbe(baseDuty_); }
  // [Guía] Propone el siguiente punto de prueba con el paso y sentido actuales, respetando los extremos
  // [Guía] permitidos. Recibe `origin`.
  void startProbe(float origin) {
    // [Guía] Actualiza `confirmation_` con `0`.
    confirmation_ = 0;
    // [Guía] Actualiza `ambiguousWindows_` con `0`.
    ambiguousWindows_ = 0;
    // [Guía] Actualiza `probeWindows_` con `0`.
    probeWindows_ = 0;
    // [Guía] Actualiza `proposed` con `origin + direction_ * step_`.
    float proposed = origin + direction_ * step_;
    // [Guía] Comprueba `proposed < minimum_ || proposed > maximum_`. Si se cumple, ejecuta el bloque asociado.
    if (proposed < minimum_ || proposed > maximum_) {
      // [Guía] Actualiza `direction_` con `-direction_`. Este dato representa sentido de la perturbación.
      direction_ = -direction_;
      // [Guía] Actualiza `proposed` con `origin + direction_ * step_`.
      proposed = origin + direction_ * step_;
    }
    // [Guía] Actualiza `duty_` con `fminf(maximum_, fmaxf(minimum_, proposed))`.
    duty_ = fminf(maximum_, fmaxf(minimum_, proposed));
    // [Guía] Actualiza `probing_` con `true`.
    probing_ = true;
  }
  // Regreso limitado al paso maximo del perfil por propuesta: puede necesitar varias
  // ventanas si la exploracion plana se alejo mucho de la ultima mejora.
  // [Guía] Retoma la posición de referencia después de una prueba que no justificó conservar el desplazamiento.
  void returnToBase() {
    // [Guía] Actualiza `distance` con `baseDuty_ - duty_`.
    const float distance = baseDuty_ - duty_;
    // [Guía] Acumula en `duty_` mediante `fminf(maximumStep_, fmaxf(-maximumStep_, distance))`.
    duty_ += fminf(maximumStep_, fmaxf(-maximumStep_, distance));
    // [Guía] Comprueba `fabsf(baseDuty_ - duty_) < 0.000001f`. Si se cumple, ejecuta `duty_ = baseDuty_`. Aquí
    // [Guía] intervienen posición de búsqueda aceptada como base.
    if (fabsf(baseDuty_ - duty_) < 0.000001f) duty_ = baseDuty_;
    // [Guía] Actualiza `returning_` con `duty_ != baseDuty_`.
    returning_ = duty_ != baseDuty_;
    // [Guía] Actualiza `probing_` con `false`.
    probing_ = false;
    // [Guía] Actualiza `confirmation_` con `ambiguousWindows_ = probeWindows_ = 0`.
    confirmation_ = ambiguousWindows_ = probeWindows_ = 0;
    // [Guía] Actualiza `decision_` con `"RETURN_BEST"`.
    decision_ = "RETURN_BEST";
  }
  // Estado persistente entre llamadas: limites, punto base y acumuladores.
  // [Guía] Declara `minimum_` para conservar minimum en este ámbito.
  float minimum_, maximum_; // Limites del constructor; OFF no se busca como MPP.
  // [Guía] Actualiza `settleUs_` con `SETTLE_US, averageUs_ = AVERAGE_US`.
  uint32_t settleUs_ = SETTLE_US, averageUs_ = AVERAGE_US;
  // [Guía] Actualiza `initialStep_` con `DUTY_STEP, minimumStep_ = MIN_DUTY_STEP, maximumStep_ = MAX_DUTY_STEP`.
  float initialStep_ = DUTY_STEP, minimumStep_ = MIN_DUTY_STEP, maximumStep_ = MAX_DUTY_STEP;
  // [Guía] Actualiza `duty_` con `0, baseDuty_ = 0, basePower_ = 0, power_ = 0`.
  float duty_ = 0, baseDuty_ = 0, basePower_ = 0, power_ = 0;
  // [Guía] Actualiza `step_` con `DUTY_STEP`.
  float step_ = DUTY_STEP;
  // [Guía] Actualiza `deltaPower_` con `0, referencePower_ = 0`. Este dato representa diferencia de potencia
  // [Guía] respecto a la base, en W.
  float deltaPower_ = 0, referencePower_ = 0;
  // [Guía] Actualiza `sumV_` con `0, sumI_ = 0, sumP_ = 0`.
  float sumV_ = 0, sumI_ = 0, sumP_ = 0;
  // [Guía] Actualiza `windowStart_` con `0, count_ = 0`.
  uint32_t windowStart_ = 0, count_ = 0;
  // [Guía] Actualiza `activeUs_` con `0, lastClockUs_ = 0, pauseStart_ = 0`. Este dato representa tiempo útil
  // [Guía] acumulado de búsqueda en µs.
  uint32_t activeUs_ = 0, lastClockUs_ = 0, pauseStart_ = 0;
  // [Guía] Actualiza `paused_` con `false`.
  bool paused_ = false;
  // [Guía] Actualiza `direction_` con `-1`. Este dato representa sentido de la perturbación.
  int direction_ = -1; // -1 baja duty; +1 lo sube.
  // [Guía] Actualiza `confirmation_` con `0`.
  int confirmation_ = 0; // Resultado previo: -1 peor, 0 indefinido, +1 mejor.
  // [Guía] Actualiza `ambiguousWindows_` con `0`.
  uint8_t ambiguousWindows_ = 0;
  // [Guía] Actualiza `probeWindows_` con `0`.
  uint8_t probeWindows_ = 0;
  // [Guía] Actualiza `updates_` con `0, flatSteps_ = 0`. Este dato representa contador de actualizaciones.
  uint32_t updates_ = 0, flatSteps_ = 0;
  // [Guía] Actualiza `decision_` con `"IDLE"`.
  const char* decision_ = "IDLE";
  // [Guía] Actualiza `returning_` con `false`.
  bool returning_ = false;
  // [Guía] Actualiza `probing_` con `false, haveBase_ = false`.
  bool probing_ = false, haveBase_ = false;
};
