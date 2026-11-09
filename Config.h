// [Guía] Reúne las decisiones de diseño. Los sufijos A, V, US, MS, HZ y OHM indican unidades. Los duty se
// [Guía] expresan como fracciones, por ejemplo 0.05 equivale al 5 %.
// [Guía] Evita procesar este encabezado dos veces en una misma unidad de compilación.
#pragma once
/* MAPA DE CONFIGURACION DEL PROYECTO
 * constexpr: constante resuelta al compilar. Cambiarla exige volver a subir
 * firmware; Python y el dashboard no sobrescriben estos valores.
 * Sufijos: _V voltios, _A amperios, _OHM ohmios, _US microsegundos,
 * _MS milisegundos, _HZ hertz.1 s=1000 ms=1000000 us.
 * Duty y slew:0.05=5%;0.40/s permite40 puntos porcentuales por segundo.
 * Ganancias PI en forma paralela: u=Kp*error+integral(Ki*error*dt).
 * static_assert comprueba coherencia de constantes al compilar; NO prueba
 * que la planta, componentes ni protecciones de hardware soporten el ensayo.
 * Hay parametros historicos conservados y marcados como tales: no suponer
 * que cambiar un nombre antiguo modifica una ruta activa del controlador.
 * Orden de sensores/arreglos: indice0=PV,1=bus total,2=bateria.
 * Los cortes, permisos operativos, filtros y tiempos se separan deliberadamente:
 * aumentar un umbral de diagnostico no amplifica el rango seguro del hardware.
 */
// [Guía] Incorpora las declaraciones de <Arduino.h> antes de usar sus tipos y funciones.
#include <Arduino.h>

// [Guía] Reúne bajo `Config` las declaraciones que comparten este tipo o ámbito.
namespace Config {
// V54 conserva los valores del Config.h adjunto por el usuario, incluidos
// limites ampliados. NO son una validacion del banco. ATENCION: el techo de
// carga26V no es adecuado como limite de carga de una bateria nominal12V;
// restablecer el techo indicado por su fabricante antes de ensayar con ella.
// Corregir USB no requiere elevar limites de tension ni de corriente.
// Mitigacion por carga remota, no proteccion fisica de sobretension.
// [Guía] Fija el parámetro `BUS_OV_TRIGGER_V` (bus DC OV trigger). Su unidad es voltios.
constexpr float BUS_OV_TRIGGER_V = 35.0f;
// [Guía] Fija el parámetro `BUS_OV_TRIGGER_MS` (bus DC OV trigger). Su unidad es milisegundos.
constexpr uint32_t BUS_OV_TRIGGER_MS = 1000;
// [Guía] Comprueba `BUS_OV_TRIGGER_MS > 0, );` durante la compilación.
static_assert(BUS_OV_TRIGGER_MS > 0, "Invalid bus OV persistence");
// [Guía] Fija el parámetro `BUS_OV_RESET_V` (bus DC OV reset). Su unidad es voltios.
constexpr float BUS_OV_RESET_V = 30.0f;
// [Guía] Fija el parámetro `BUS_OV_RESET_MS` (bus DC OV reset). Su unidad es milisegundos.
constexpr uint32_t BUS_OV_RESET_MS = 2000;
// [Guía] Fija el parámetro `BUS_OV_EMULATOR_OHM` (bus DC OV emulator). Su unidad es ohmios.
constexpr float BUS_OV_EMULATOR_OHM = 200.0f;
// [Guía] Comprueba `BUS_OV_RESET_V < BUS_OV_TRIGGER_V, );` durante la compilación.
static_assert(BUS_OV_RESET_V < BUS_OV_TRIGGER_V, "Invalid bus OV hysteresis");
// Recuperacion OPERATIVA con bateria estimada en START_M4/M4/M3: pedir carga
// antes del umbral alto35 V. No se aplica al barrido de adquisicion de M1.
// [Guía] Fija el parámetro `BUS_RECOVERY_LOAD_V` (bus DC recovery load). Su unidad es voltios.
constexpr float BUS_RECOVERY_LOAD_V = 23.0f;
// [Guía] Fija el parámetro `BUS_RECOVERY_LOAD_MS` (bus DC recovery load). Su unidad es milisegundos.
constexpr uint32_t BUS_RECOVERY_LOAD_MS = 50;
// [Guía] Comprueba `BUS_RECOVERY_LOAD_V > 20.0f && BUS_RECOVERY_LOAD_V < BUS_OV_TRIGGER_V` durante la
// [Guía] compilación.
static_assert(BUS_RECOVERY_LOAD_V > 20.0f && BUS_RECOVERY_LOAD_V < BUS_OV_TRIGGER_V
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && BUS_RECOVERY_LOAD_MS > 0, "Invalid recovery load qualification");
// Cuantizacion: analogRead usa14 bits, de0 a(2^14-1)=16383.
// Vpin=ADC/ADC_MAX*ADC_REF_V. La tension externa se calcula con el divisor.
// [Guía] Centraliza el ajuste `ADC_BITS` (ADC bits) para compartirlo entre los módulos que lo usan.
constexpr uint8_t ADC_BITS = 14;
// [Guía] Centraliza el ajuste `ADC_MAX` (ADC máximo) para compartirlo entre los módulos que lo usan.
constexpr float ADC_MAX = 16383.0f;
// [Guía] Fija el parámetro `ADC_REF_V` (ADC REF). Su unidad es voltios.
constexpr float ADC_REF_V = 5.0f;
// [Guía] Programa un barrido de los canales ADC cada 500 µs. La frecuencia nominal de 2 kHz corresponde a
// [Guía] barridos completos.
constexpr uint32_t SAMPLE_US = 500; // Periodo nominal ADC:2000 barridos por segundo.
// [Guía] Fija el parámetro `PRINT_MS` (print). Su unidad es milisegundos.
constexpr uint32_t PRINT_MS = 250; // Reporte largo nominal4/s; LIVE tiene periodo propio.
// Instantanea compacta para dashboard; independiente del diagnostico largo.
// [Guía] Fija el parámetro `LIVE_TELEMETRY_MS` (live telemetry). Su unidad es milisegundos.
constexpr uint32_t LIVE_TELEMETRY_MS = 200;
// [Guía] Comprueba `LIVE_TELEMETRY_MS > 0, );` durante la compilación.
static_assert(LIVE_TELEMETRY_MS > 0, "Invalid live telemetry period");
// Si el control no deja margen, permitir formatear tras1s en vez de dejar
// sin telemetria al PC. La entrega aun depende del espacio disponible en USB.
// [Guía] Fija el parámetro `SERIAL_REPORT_MAX_INTERVAL_MS` (serial report máximo interval). Su unidad es
// [Guía] milisegundos.
constexpr uint32_t SERIAL_REPORT_MAX_INTERVAL_MS = 1000;
// [Guía] Comprueba `SERIAL_REPORT_MAX_INTERVAL_MS >= PRINT_MS, );` durante la compilación.
static_assert(SERIAL_REPORT_MAX_INTERVAL_MS >= PRINT_MS, "Invalid report deferral");
// [Guía] Centraliza el ajuste `ZERO_SAMPLES` (zero samples) para compartirlo entre los módulos que lo usan.
constexpr uint32_t ZERO_SAMPLES = 4000; // Unos2 s de cero a2 kHz; exige cero fisico real.
// [Guía] Centraliza el ajuste `AUTO_START_AFTER_BOOT_CALIBRATION` (auto start after boot calibration) para
// [Guía] compartirlo entre los módulos que lo usan.
constexpr bool AUTO_START_AFTER_BOOT_CALIBRATION = true;
// La busqueda queda preparada al arrancar, pero no energiza una carga sin
// validacion del hardware: se observaron sobretensiones con Cdc de 35 V.
// [Guía] Centraliza el ajuste `AUTO_EMULATOR_AFTER_BOOT` (auto emulator after boot) para compartirlo entre los
// [Guía] módulos que lo usan.
constexpr bool AUTO_EMULATOR_AFTER_BOOT = true;
// [Guía] Centraliza el ajuste `AUTO_EMULATOR_HARDWARE_READY` (auto emulator hardware listo) para compartirlo
// [Guía] entre los módulos que lo usan.
constexpr bool AUTO_EMULATOR_HARDWARE_READY = false;
// Buscar reduciendo R aumenta demanda a tension fija: P=Vbus^2/R.
// Estos parametros pertenecen a AUTO_R, distinto de la mitigacion200 ohm.
// [Guía] Fija el parámetro `EMULATOR_START_OHM` (emulator start). Su unidad es ohmios.
constexpr float EMULATOR_START_OHM = 4000.0f;
// [Guía] Fija el parámetro `EMULATOR_MIN_OHM` (emulator mínimo). Su unidad es ohmios.
constexpr float EMULATOR_MIN_OHM = 30.0f;
// [Guía] Fija el parámetro `EMULATOR_PV_TARGET_V` (emulator panel fotovoltaico target). Su unidad es voltios.
constexpr float EMULATOR_PV_TARGET_V = 16.0f;
// [Guía] Fija el parámetro `EMULATOR_STABLE_BAND_V` (emulator stable band). Su unidad es voltios.
constexpr float EMULATOR_STABLE_BAND_V = 0.75f;
// [Guía] Fija el parámetro `EMULATOR_UNSTABLE_BAND_V` (emulator unstable band). Su unidad es voltios.
constexpr float EMULATOR_UNSTABLE_BAND_V = 1.5f;
// [Guía] Fija el parámetro `EMULATOR_STABLE_MS` (emulator stable). Su unidad es milisegundos.
constexpr uint32_t EMULATOR_STABLE_MS = 1000;
// [Guía] Fija el parámetro `EMULATOR_SETTLE_MS` (emulator asentamiento). Su unidad es milisegundos.
constexpr uint32_t EMULATOR_SETTLE_MS = 5000;
// Exigir una ventana completa de poca oscilacion, no solo cruzar la referencia.
// [Guía] Fija el parámetro `EMULATOR_QUIET_WINDOW_MS` (emulator quiet ventana). Su unidad es milisegundos.
constexpr uint32_t EMULATOR_QUIET_WINDOW_MS = 1000;
// [Guía] Fija el parámetro `EMULATOR_BUS_PP_MAX_V` (emulator bus DC PP máximo). Su unidad es voltios.
constexpr float EMULATOR_BUS_PP_MAX_V = 0.75f;
// [Guía] Comprueba `EMULATOR_QUIET_WINDOW_MS > 0 && EMULATOR_BUS_PP_MAX_V > 0` durante la compilación.
static_assert(EMULATOR_QUIET_WINDOW_MS > 0 && EMULATOR_BUS_PP_MAX_V > 0,
              "Invalid emulator ripple qualification");
// [Guía] Fija el parámetro `EMULATOR_UNSTABLE_MS` (emulator unstable). Su unidad es milisegundos.
constexpr uint32_t EMULATOR_UNSTABLE_MS = 250;
// [Guía] Fija el parámetro `EMULATOR_TARGET_MS` (emulator target). Su unidad es milisegundos.
constexpr uint32_t EMULATOR_TARGET_MS = 1000;
// [Guía] Fija el parámetro `EMULATOR_WAIT_MS` (emulator wait). Su unidad es milisegundos.
constexpr uint32_t EMULATOR_WAIT_MS = 15000;
// Limites de ensayo, NO proteccion independiente ni limites de bateria.
// [Guía] Fija el parámetro `EMULATOR_BUS_HIGH_V` (emulator bus DC high). Su unidad es voltios.
constexpr float EMULATOR_BUS_HIGH_V = 23.0f;
// [Guía] Fija el parámetro `EMULATOR_PV_LOW_V` (emulator panel fotovoltaico LOW). Su unidad es voltios.
constexpr float EMULATOR_PV_LOW_V = 3.0f;
// [Guía] Fija el parámetro `EMULATOR_CURRENT_MAX_A` (emulator corriente máximo). Su unidad es amperios.
constexpr float EMULATOR_CURRENT_MAX_A = 2.8f;
// [Guía] Comprueba `EMULATOR_MIN_OHM >= 30 && EMULATOR_MIN_OHM <= EMULATOR_START_OHM` durante la compilación.
static_assert(EMULATOR_MIN_OHM >= 30 && EMULATOR_MIN_OHM <= EMULATOR_START_OHM,
              "Emulator resistance outside supported range");
// [Guía] Comprueba `EMULATOR_START_OHM <= 4000, );` durante la compilación.
static_assert(EMULATOR_START_OHM <= 4000, "Emulator start resistance too high");
// [Guía] Comprueba `EMULATOR_STABLE_MS > 0 && EMULATOR_SETTLE_MS >= EMULATOR_STABLE_MS` durante la compilación.
static_assert(EMULATOR_STABLE_MS > 0 && EMULATOR_SETTLE_MS >= EMULATOR_STABLE_MS,
              "Invalid emulator stability timing");
// [Guía] Comprueba `EMULATOR_UNSTABLE_BAND_V > EMULATOR_STABLE_BAND_V` durante la compilación.
static_assert(EMULATOR_UNSTABLE_BAND_V > EMULATOR_STABLE_BAND_V,
              "Emulator instability band must exceed stability band");
// [Guía] Asigna pwm1 al pin que usará el sketch.
constexpr uint8_t PWM1_PIN = 3;
// [Guía] Fija el parámetro `PWM1_HZ` (pwm1). Su unidad es hercios.
constexpr float PWM1_HZ = 20000.0f;
// Perfil EMS automatico: esperar evidencia de la primera fuente tras calibrar.
// No ejecuta la busqueda AUTO_R durante estos ensayos de transicion.
// [Guía] Centraliza el ajuste `BATTERY_TEST_PROFILE` (batería test profile) para compartirlo entre los módulos
// [Guía] que lo usan.
constexpr bool BATTERY_TEST_PROFILE = true;
// Identificar la version cargada al comparar registros de las pruebas.
// [Guía] Identifica el firmware que originó el registro. La aplicación lo presenta como dato de diagnóstico.
constexpr const char* EMS_FIRMWARE_ID = "EMS_V69_FILTERED_MEASUREMENTS";
// Emulador del perfil EMS: ayuda gradual en M1; en M3, Q2 dentro90..95%.
// Confirmar el encendido; retirar inmediatamente la orden al salir de banda.
// El reposo minimo evita repetir pulsos ON/OFF cerca del umbral90%.
// [Guía] Define M3 carga auxiliar ciclo de trabajo mínimo como fracción del periodo PWM. Multiplicar por 100 da
// [Guía] el porcentaje.
constexpr float M3_AUX_DUTY_MIN = 0.90f;
// [Guía] Define M3 carga auxiliar ciclo de trabajo máximo como fracción del periodo PWM. Multiplicar por 100 da
// [Guía] el porcentaje.
constexpr float M3_AUX_DUTY_MAX = 0.95f;
// [Guía] Fija el parámetro `M3_AUX_EMULATOR_OHM` (M3 carga auxiliar emulator). Su unidad es ohmios.
constexpr float M3_AUX_EMULATOR_OHM = 200.0f;
// Si Q2 sigue en saturacion con bus alto, ampliar ayuda sin barrer R sin limite.
// Separar pasos4s deja ventanas completas de MPPT entre cambios de carga.
// [Guía] Fija el parámetro `M3_AUX_MIN_OHM` (M3 carga auxiliar mínimo). Su unidad es ohmios.
constexpr float M3_AUX_MIN_OHM = 30.0f;
// [Guía] Centraliza el ajuste `M3_AUX_RESISTANCE_FACTOR` (M3 carga auxiliar resistance factor) para compartirlo
// [Guía] entre los módulos que lo usan.
constexpr float M3_AUX_RESISTANCE_FACTOR = 0.90f;
// [Guía] Define M3 carga auxiliar rampa ciclo de trabajo mínimo como fracción del periodo PWM. Multiplicar por
// [Guía] 100 da el porcentaje.
constexpr float M3_AUX_RAMP_DUTY_MIN = 0.94f;
// [Guía] Fija el parámetro `M3_AUX_RAMP_BUS_MIN_V` (M3 carga auxiliar rampa bus DC mínimo). Su unidad es voltios.
constexpr float M3_AUX_RAMP_BUS_MIN_V = 20.8f;
// [Guía] Fija el parámetro `M3_AUX_STEP_MS` (M3 carga auxiliar paso). Su unidad es milisegundos.
constexpr uint32_t M3_AUX_STEP_MS = 4000;
// [Guía] Fija el parámetro `M3_AUX_BUDGET_MS` (M3 carga auxiliar budget). Su unidad es milisegundos.
constexpr uint32_t M3_AUX_BUDGET_MS = 30000;
// [Guía] Fija el parámetro `M3_AUX_CONFIRM_MS` (M3 carga auxiliar confirm). Su unidad es milisegundos.
constexpr uint32_t M3_AUX_CONFIRM_MS = 200;
// [Guía] Fija el parámetro `M3_AUX_MIN_OFF_MS` (M3 carga auxiliar mínimo OFF). Su unidad es milisegundos.
constexpr uint32_t M3_AUX_MIN_OFF_MS = 500;
// No sumar carga si el bus ya tiene deficit respecto a20 V.
// [Guía] Fija el parámetro `M3_AUX_BUS_MIN_V` (M3 carga auxiliar bus DC mínimo). Su unidad es voltios.
constexpr float M3_AUX_BUS_MIN_V = 19.75f;
// En M3/legacy, deficit persistente retira gradualmente y bus<=18V pide OFF.
// M1 real activo usa retroceso y guarda de colapso separada abajo.
// [Guía] Fija el parámetro `AUX_DEFICIT_CONFIRM_MS` (carga auxiliar deficit confirm). Su unidad es milisegundos.
constexpr uint32_t AUX_DEFICIT_CONFIRM_MS = 200;
// [Guía] Fija el parámetro `AUX_BUS_OFF_V` (carga auxiliar bus DC OFF). Su unidad es voltios.
constexpr float AUX_BUS_OFF_V = 18.0f;
// M1 con carga YA confirmada:18V provoca retroceso rapido, no OFF inmediato.
// Cero/fallo/fuente perdida siguen prioritarios. Colapso<=10V confirmado200ms
// retira carga: mantener ON no es una orden de ignorar perdida de energia.
// [Guía] Fija el parámetro `M1_AUX_FAST_ROLLBACK_V` (M1 carga auxiliar fast rollback). Su unidad es voltios.
constexpr float M1_AUX_FAST_ROLLBACK_V = 18.0f;
// [Guía] Fija el parámetro `M1_AUX_COLLAPSE_OFF_V` (M1 carga auxiliar collapse OFF). Su unidad es voltios.
constexpr float M1_AUX_COLLAPSE_OFF_V = 10.0f;
// [Guía] Fija el parámetro `M1_AUX_COLLAPSE_CONFIRM_MS` (M1 carga auxiliar collapse confirm). Su unidad es
// [Guía] milisegundos.
constexpr uint32_t M1_AUX_COLLAPSE_CONFIRM_MS = 200;
// [Guía] Comprueba `M1_AUX_COLLAPSE_OFF_V > 0 && M1_AUX_COLLAPSE_OFF_V < M1_AUX_FAST_ROLLBACK_V` durante la
// [Guía] compilación.
static_assert(M1_AUX_COLLAPSE_OFF_V > 0 && M1_AUX_COLLAPSE_OFF_V < M1_AUX_FAST_ROLLBACK_V
  // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
  && M1_AUX_FAST_ROLLBACK_V < M3_AUX_BUS_MIN_V && M1_AUX_COLLAPSE_CONFIRM_MS > 0,
  "Invalid M1 load collapse guard");
// Ruta historica: confirmar deficit bajo19V antes del alivio10%. En el perfil
// real V55,800ms confirma recuperacion estable;5s sin estabilizar retrocede
// UN paso confirmado, no multiplica R repetidamente hasta4000ohm.
// [Guía] Fija el parámetro `M1_AUX_BACKOFF_BUS_V` (M1 carga auxiliar backoff bus DC). Su unidad es voltios.
constexpr float M1_AUX_BACKOFF_BUS_V = 19.0f;
// [Guía] Fija el parámetro `M1_AUX_BACKOFF_CONFIRM_MS` (M1 carga auxiliar backoff confirm). Su unidad es
// [Guía] milisegundos.
constexpr uint32_t M1_AUX_BACKOFF_CONFIRM_MS = 800;
// [Guía] Centraliza el ajuste `M1_AUX_BACKOFF_FACTOR` (M1 carga auxiliar backoff factor) para compartirlo entre
// [Guía] los módulos que lo usan.
constexpr float M1_AUX_BACKOFF_FACTOR = 1.10f;
// M1: carga auxiliar progresiva desde4000ohm, en pasos de800ms con ACK.
// Perfil real usa los3tramos de abajo. R*=0.75 solo queda para regresiones
// historicas. Con PI activo, continuar desde PV bajo hasta17.8V.
// Durante adquisicion desde PV alto, detener al alcanzar18.2V o menos.
// Son ayudas limitadas en tiempo/corriente; nunca bajar de30ohm.
// [Guía] Fija el parámetro `M1_AUX_START_OHM` (M1 carga auxiliar start). Su unidad es ohmios.
constexpr float M1_AUX_START_OHM = 4000.0f;
// [Guía] Fija el parámetro `M1_AUX_MIN_OHM` (M1 carga auxiliar mínimo). Su unidad es ohmios.
constexpr float M1_AUX_MIN_OHM = 30.0f;
// [Guía] Fija el parámetro `M1_AUX_PV_TARGET_V` (M1 carga auxiliar panel fotovoltaico target). Su unidad es
// [Guía] voltios.
constexpr float M1_AUX_PV_TARGET_V = 18.0f;
// [Guía] Fija el parámetro `M1_AUX_PV_TOLERANCE_V` (M1 carga auxiliar panel fotovoltaico tolerance). Su unidad es
// [Guía] voltios.
constexpr float M1_AUX_PV_TOLERANCE_V = 0.2f;
// [Guía] Centraliza el ajuste `M1_AUX_RESISTANCE_FACTOR` (M1 carga auxiliar resistance factor) para compartirlo
// [Guía] entre los módulos que lo usan.
constexpr float M1_AUX_RESISTANCE_FACTOR = 0.75f; // Compatibilidad con ensayos anteriores.
// Tramos elegidos por la R ACTUAL confirmada, no por el voltaje PV. 20% hasta
// cruzar500ohm;5% hasta cruzar210ohm;5% despues (los dos ultimos son iguales
// por peticion del usuario, pero se configuran independientemente).
// [Guía] Fija el parámetro `M1_AUX_COARSE_UNTIL_OHM` (M1 carga auxiliar coarse until). Su unidad es ohmios.
constexpr float M1_AUX_COARSE_UNTIL_OHM = 500.0f;
// [Guía] Fija el parámetro `M1_AUX_MIDDLE_UNTIL_OHM` (M1 carga auxiliar middle until). Su unidad es ohmios.
constexpr float M1_AUX_MIDDLE_UNTIL_OHM = 210.0f;
// [Guía] Centraliza el ajuste `M1_AUX_COARSE_FACTOR` (M1 carga auxiliar coarse factor) para compartirlo entre los
// [Guía] módulos que lo usan.
constexpr float M1_AUX_COARSE_FACTOR = 0.80f;
// [Guía] Centraliza el ajuste `M1_AUX_MIDDLE_FACTOR` (M1 carga auxiliar middle factor) para compartirlo entre los
// [Guía] módulos que lo usan.
constexpr float M1_AUX_MIDDLE_FACTOR = 0.95f;
// [Guía] Centraliza el ajuste `M1_AUX_FINAL_FACTOR` (M1 carga auxiliar final factor) para compartirlo entre los
// [Guía] módulos que lo usan.
constexpr float M1_AUX_FINAL_FACTOR = 0.95f;
// [Guía] Fija el parámetro `M1_AUX_INITIAL_STABLE_MS` (M1 carga auxiliar initial stable). Su unidad es
// [Guía] milisegundos.
constexpr uint32_t M1_AUX_INITIAL_STABLE_MS = 2000;
// [Guía] Fija el parámetro `M1_AUX_STABLE_BAND_V` (M1 carga auxiliar stable band). Su unidad es voltios.
constexpr float M1_AUX_STABLE_BAND_V = 0.5f;
// Arranque auxiliar: acumular tiempo VALIDO en banda; un pico breve no borra
// todo lo conseguido.250ms seguidos fuera de banda si obligan a recalificar.
// [Guía] Fija el parámetro `M1_AUX_STABLE_GAP_MS` (M1 carga auxiliar stable hueco entre muestras). Su unidad es
// [Guía] milisegundos.
constexpr uint32_t M1_AUX_STABLE_GAP_MS = 250;
// Compatibilidad historica: paso fino segun margen PV. El perfil real V55
// selecciona los factores por R, no cambia de tramo al cruzar14V de PV.
// [Guía] Fija el parámetro `M1_AUX_PREP_FINE_BAND_V` (M1 carga auxiliar prep fine band). Su unidad es voltios.
constexpr float M1_AUX_PREP_FINE_BAND_V = 4.0f;
// [Guía] Centraliza el ajuste `M1_AUX_PREP_FINE_FACTOR` (M1 carga auxiliar prep fine factor) para compartirlo
// [Guía] entre los módulos que lo usan.
constexpr float M1_AUX_PREP_FINE_FACTOR = 0.95f;
// [Guía] Fija el parámetro `M1_AUX_PREP_BUDGET_MS` (M1 carga auxiliar prep budget). Su unidad es milisegundos.
constexpr uint32_t M1_AUX_PREP_BUDGET_MS = 60000;
// [Guía] Fija el parámetro `M1_AUX_STEP_MS` (M1 carga auxiliar paso). Su unidad es milisegundos.
constexpr uint32_t M1_AUX_STEP_MS = 800;
// Pausar sin aumentar consumo ante bus fuera20+/-0.5V. Tras5s, solicitar
// exactamente la R del paso anterior con ACK y esperar recuperacion estable.
// [Guía] Fija el parámetro `M1_AUX_UNSTABLE_WAIT_MS` (M1 carga auxiliar unstable wait). Su unidad es
// [Guía] milisegundos.
constexpr uint32_t M1_AUX_UNSTABLE_WAIT_MS = 5000;
// Esperar3s desde el ACK de cada retroceso antes de aliviar otro paso.
// [Guía] Fija el parámetro `M1_AUX_ROLLBACK_WAIT_MS` (M1 carga auxiliar rollback wait). Su unidad es
// [Guía] milisegundos.
constexpr uint32_t M1_AUX_ROLLBACK_WAIT_MS = 3000;
// LIMITED solo puede reintentar si cambia realmente la carga confirmada.
// Dos presupuestos extra por episodio M1; nunca reiniciar sin limite por ACK.
// [Guía] Centraliza el ajuste `M1_AUX_RELIEF_MAX_REACQUIRES` (M1 carga auxiliar relief máximo reacquires) para
// [Guía] compartirlo entre los módulos que lo usan.
constexpr unsigned M1_AUX_RELIEF_MAX_REACQUIRES = 2;
// Solo tolerancia BREVE de un permiso de medida invalido. No operar5s sin
// medidas fiables: si persiste100ms, pedir OFF. No aplica a muestras viejas,
// bus cero/colapso, fallo EMS, orden OFF ni fallo de enlace, que siguen prioritarios.
// [Guía] Fija el parámetro `M1_AUX_INVALID_GRACE_MS` (M1 carga auxiliar invalid grace). Su unidad es
// [Guía] milisegundos.
constexpr uint32_t M1_AUX_INVALID_GRACE_MS = 100;
// [Guía] Fija el parámetro `M1_AUX_BUDGET_MS` (M1 carga auxiliar budget). Su unidad es milisegundos.
constexpr uint32_t M1_AUX_BUDGET_MS = 12000;
// Retirada mas rapida: duplicar R cada100ms hasta4000ohm y pedir OFF.
// Si faltan ACK, pedir OFF al agotar2s; no esperar indefinidamente.
// [Guía] Centraliza el ajuste `AUX_RETIRE_FACTOR` (carga auxiliar retire factor) para compartirlo entre los
// [Guía] módulos que lo usan.
constexpr float AUX_RETIRE_FACTOR = 2.0f;
// [Guía] Fija el parámetro `AUX_RETIRE_STEP_MS` (carga auxiliar retire paso). Su unidad es milisegundos.
constexpr uint32_t AUX_RETIRE_STEP_MS = 100;
// [Guía] Fija el parámetro `AUX_RETIRE_TIMEOUT_MS` (carga auxiliar retire vencimiento). Su unidad es
// [Guía] milisegundos.
constexpr uint32_t AUX_RETIRE_TIMEOUT_MS = 2000;
// [Guía] Comprueba `M1_AUX_MIN_OHM >= 30 && M1_AUX_START_OHM <= 4000` durante la compilación.
static_assert(M1_AUX_MIN_OHM >= 30 && M1_AUX_START_OHM <= 4000
  // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
  && M1_AUX_MIN_OHM < M1_AUX_START_OHM && M1_AUX_RESISTANCE_FACTOR > 0
  // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
  && M1_AUX_RESISTANCE_FACTOR < 1 && AUX_RETIRE_FACTOR > 1
  // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
  && AUX_RETIRE_STEP_MS < M1_AUX_STEP_MS && M1_AUX_BUDGET_MS > M1_AUX_STEP_MS,
  "Invalid auxiliary resistance ramp");
// [Guía] Comprueba `M1_AUX_START_OHM > M1_AUX_COARSE_UNTIL_OHM` durante la compilación.
static_assert(M1_AUX_START_OHM > M1_AUX_COARSE_UNTIL_OHM
  // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
  && M1_AUX_COARSE_UNTIL_OHM > M1_AUX_MIDDLE_UNTIL_OHM
  // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
  && M1_AUX_MIDDLE_UNTIL_OHM > M1_AUX_MIN_OHM
  // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
  && M1_AUX_COARSE_FACTOR > 0 && M1_AUX_COARSE_FACTOR < 1
  // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
  && M1_AUX_MIDDLE_FACTOR > 0 && M1_AUX_MIDDLE_FACTOR < 1
  // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
  && M1_AUX_FINAL_FACTOR > 0 && M1_AUX_FINAL_FACTOR < 1
  // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
  && M1_AUX_UNSTABLE_WAIT_MS > M1_AUX_BACKOFF_CONFIRM_MS
  // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
  && M1_AUX_ROLLBACK_WAIT_MS >= M1_AUX_BACKOFF_CONFIRM_MS
  // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
  && M1_AUX_RELIEF_MAX_REACQUIRES <= 4
  // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
  && M1_AUX_INVALID_GRACE_MS > 0
  // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
  && M1_AUX_INVALID_GRACE_MS < M1_AUX_BACKOFF_CONFIRM_MS,
  "Invalid staged M1 load ramp or pause");
// [Guía] Comprueba `M1_AUX_PV_TOLERANCE_V >= 0 && M1_AUX_PV_TOLERANCE_V < M1_AUX_PV_TARGET_V` durante la
// [Guía] compilación.
static_assert(M1_AUX_PV_TOLERANCE_V >= 0 && M1_AUX_PV_TOLERANCE_V < M1_AUX_PV_TARGET_V
  // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
  && M1_AUX_PREP_FINE_FACTOR > M1_AUX_RESISTANCE_FACTOR && M1_AUX_PREP_FINE_FACTOR < 1
  // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
  && M1_AUX_PREP_FINE_BAND_V > M1_AUX_PV_TOLERANCE_V
  // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
  && M1_AUX_PREP_BUDGET_MS > M1_AUX_STEP_MS, "Invalid M1 PV preparation");
// [Guía] Comprueba `M3_AUX_DUTY_MIN >= 0 && M3_AUX_DUTY_MIN < M3_AUX_DUTY_MAX` durante la compilación.
static_assert(M3_AUX_DUTY_MIN >= 0 && M3_AUX_DUTY_MIN < M3_AUX_DUTY_MAX
  // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
  && M3_AUX_DUTY_MAX <= 1 && M3_AUX_CONFIRM_MS > 0 && M3_AUX_MIN_OFF_MS > 0
  // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
  && M3_AUX_EMULATOR_OHM >= 30 && M3_AUX_EMULATOR_OHM <= 4000,
  "Invalid M3 auxiliary load policy");
// [Guía] Comprueba `AUX_BUS_OFF_V < M3_AUX_BUS_MIN_V && AUX_DEFICIT_CONFIRM_MS > 0` durante la compilación.
static_assert(AUX_BUS_OFF_V < M3_AUX_BUS_MIN_V && AUX_DEFICIT_CONFIRM_MS > 0
  // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
  && M3_AUX_RAMP_DUTY_MIN >= M3_AUX_DUTY_MIN && M3_AUX_RAMP_DUTY_MIN <= M3_AUX_DUTY_MAX
  // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
  && M3_AUX_RAMP_BUS_MIN_V >= M3_AUX_BUS_MIN_V && M3_AUX_MIN_OHM >= 30
  // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
  && M3_AUX_MIN_OHM < M3_AUX_EMULATOR_OHM
  // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
  && M3_AUX_RESISTANCE_FACTOR > 0 && M3_AUX_RESISTANCE_FACTOR < 1
  // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
  && M3_AUX_STEP_MS >= 4000 && M3_AUX_BUDGET_MS > M3_AUX_STEP_MS,
  "Invalid auxiliary deficit or M3 ramp policy");
// [Guía] Comprueba `M1_AUX_BACKOFF_BUS_V > AUX_BUS_OFF_V` durante la compilación.
static_assert(M1_AUX_BACKOFF_BUS_V > AUX_BUS_OFF_V
  // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
  && M1_AUX_BACKOFF_BUS_V < M3_AUX_BUS_MIN_V
  // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
  && M1_AUX_BACKOFF_CONFIRM_MS >= M1_AUX_STEP_MS && M1_AUX_BACKOFF_FACTOR > 1
  // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
  && M1_AUX_BACKOFF_FACTOR < AUX_RETIRE_FACTOR, "Invalid M1 partial relief");
// Pines confirmados por el usuario; 255 permite dejar una rama sin asignar.
// [Guía] Asigna pwm2 al pin que usará el sketch.
constexpr uint8_t PWM2_PIN = 5;
// [Guía] Asigna pwm3 al pin que usará el sketch.
constexpr uint8_t PWM3_PIN = 6;
// Q3: ajuste inicial de ensayo autorizado, NO sintonizado en la planta real.
// ACTION=+1 supone que aumentar duty eleva Vbus en la rama bateria -> bus.
// Error = ACTION * (referencia - Vbus). Comprobar el sentido antes del ensayo.
// [Guía] Define la ganancia proporcional al error en el lazo Q3 bus DC.
constexpr float Q3_BUS_KP = 0.0001f; // Ganancias confirmadas por el usuario para V40.
// [Guía] Define la ganancia integral del error acumulado con el tiempo en el lazo Q3 bus DC.
constexpr float Q3_BUS_KI = 0.15f;
// [Guía] Centraliza el ajuste `Q3_BUS_ACTION` (Q3 bus DC action) para compartirlo entre los módulos que lo usan.
constexpr float Q3_BUS_ACTION = 1.0f;
// Ganancias iniciales de ensayo, no derivadas de la planta ni validadas.
// ACTION debe ser +1 si mas duty sube Vbus; -1 si lo baja. Cero bloquea Q2.
// [Guía] Define la ganancia proporcional al error en el lazo Q2 bus DC.
constexpr float Q2_BUS_KP = 0.0025f; // Ganancias confirmadas; no sintonizadas aqui.
// [Guía] Define la ganancia integral del error acumulado con el tiempo en el lazo Q2 bus DC.
constexpr float Q2_BUS_KI = 0.08f;
// Usuario confirma ganancias negativas en simulacion con error referencia-Vbus.
// Magnitudes positivas y ACTION=-1 implementan ese mismo signo.
// [Guía] Centraliza el ajuste `Q2_BUS_ACTION` (Q2 bus DC action) para compartirlo entre los módulos que lo usan.
constexpr float Q2_BUS_ACTION = -1.0f;
// Umbrales de diagnostico del ensayo solicitados por el usuario, NO corrientes
// admisibles de bateria/convertidor. No demuestran que los picos sean ruido ni
// que un fusible4 A interrumpa antes de danar componentes. Sin limite fisico
// validado, no usar este perfil como proteccion principal de corriente.
// [Guía] Fija el parámetro `BAT_DISCHARGE_TRIP_A` (batería descarga trip). Su unidad es amperios.
constexpr float BAT_DISCHARGE_TRIP_A = 24.0f;
// [Guía] Fija el parámetro `BAT_DISCHARGE_SOFT_A` (batería descarga soft). Su unidad es amperios.
constexpr float BAT_DISCHARGE_SOFT_A = 23.0f;
// Ensayo Q3 solo: ante una muestra alta, mantener duty/integral sin aumentarlos.
// Confirmar el umbral configurado (raw o filtrado) durante1 ms antes de fallo.
// No supone ruido ni sustituye proteccion fisica de corriente. En otros modos
// permanece el corte raw inmediato; false recupera tambien ese criterio en M5.
// [Guía] Centraliza el ajuste `BAT_Q3_QUALIFY_CURRENT` (batería Q3 qualify corriente) para compartirlo entre los
// [Guía] módulos que lo usan.
constexpr bool BAT_Q3_QUALIFY_CURRENT = true;
// [Guía] Fija el parámetro `BAT_Q3_CURRENT_CONFIRM_US` (batería Q3 corriente confirm). Su unidad es
// [Guía] microsegundos.
constexpr uint32_t BAT_Q3_CURRENT_CONFIRM_US = 1000;
// [Guía] Fija el parámetro `BAT_Q3_CURRENT_MAX_GAP_US` (batería Q3 corriente máximo hueco entre muestras). Su
// [Guía] unidad es microsegundos.
constexpr uint32_t BAT_Q3_CURRENT_MAX_GAP_US = 5000;
// [Guía] Comprueba `BAT_Q3_CURRENT_CONFIRM_US >= SAMPLE_US` durante la compilación.
static_assert(BAT_Q3_CURRENT_CONFIRM_US >= SAMPLE_US
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && BAT_Q3_CURRENT_CONFIRM_US <= 2 * SAMPLE_US
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && BAT_Q3_CURRENT_MAX_GAP_US >= SAMPLE_US,
              "Invalid Q3 current confirmation");
// Ensayo solicitado: estabilizar la conexion SOLO en WAIT y con los PWM OFF.
// Un evento bloquea el arranque; descarga persistente durante 100 ms enclava.
// No prueba que el pico sea ruido ni limita una corriente pasiva real.
// La confirmacion Q3 solo de arriba es independiente de esta espera pasiva.
// [Guía] Centraliza el ajuste `BAT_QUALIFY_PASSIVE_CONNECTION` (batería qualify passive connection) para
// [Guía] compartirlo entre los módulos que lo usan.
constexpr bool BAT_QUALIFY_PASSIVE_CONNECTION = true;
// [Guía] Fija el parámetro `BAT_PASSIVE_TRIP_CONFIRM_US` (batería passive trip confirm). Su unidad es
// [Guía] microsegundos.
constexpr uint32_t BAT_PASSIVE_TRIP_CONFIRM_US = 100000;
// Tras el evento, exigir medidas validas y corriente bajo limites suaves.
// [Guía] Fija el parámetro `BAT_CONNECTION_RECOVER_US` (batería connection recuperación). Su unidad es
// [Guía] microsegundos.
constexpr uint32_t BAT_CONNECTION_RECOVER_US = 200000;
// Confirmar sobrecorriente exige muestras contiguas; la recuperacion tolera
// pausas breves de telemetria. Ninguno de estos tiempos garantiza flujo seguro.
// [Guía] Fija el parámetro `BAT_PASSIVE_TRIP_MAX_GAP_US` (batería passive trip máximo hueco entre muestras). Su
// [Guía] unidad es microsegundos.
constexpr uint32_t BAT_PASSIVE_TRIP_MAX_GAP_US = 5000;
// [Guía] Fija el parámetro `BAT_CONNECTION_MAX_GAP_US` (batería connection máximo hueco entre muestras). Su
// [Guía] unidad es microsegundos.
constexpr uint32_t BAT_CONNECTION_MAX_GAP_US = 5000;
// [Guía] Comprueba `BAT_PASSIVE_TRIP_CONFIRM_US >= SAMPLE_US` durante la compilación.
static_assert(BAT_PASSIVE_TRIP_CONFIRM_US >= SAMPLE_US
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && BAT_CONNECTION_RECOVER_US > BAT_PASSIVE_TRIP_CONFIRM_US
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && BAT_PASSIVE_TRIP_MAX_GAP_US >= SAMPLE_US
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && BAT_CONNECTION_MAX_GAP_US >= BAT_PASSIVE_TRIP_MAX_GAP_US,
              "Invalid passive connection qualification");
// Umbrales de diagnostico ampliados;24 A NO es corriente de carga permitida.
// GP12170 especifica5.1 A maxima de carga, no valida la capacidad de la PCB.
// [Guía] Fija el parámetro `BAT_CHARGE_TRIP_A` (batería carga trip). Su unidad es amperios.
constexpr float BAT_CHARGE_TRIP_A = 24.0f;
// [Guía] Fija el parámetro `BAT_CHARGE_SOFT_A` (batería carga soft). Su unidad es amperios.
constexpr float BAT_CHARGE_SOFT_A = 23.0f;
// Ventana de diagnostico solicitada, NO rango de operacion nominal de bateria.
// El techo operativo de carga esta separado abajo; verificarlo antes de usar.
// [Guía] Fija el parámetro `BAT_TEST_MIN_V` (batería test mínimo). Su unidad es voltios.
constexpr float BAT_TEST_MIN_V = -1.0f;
// [Guía] Fija el parámetro `BAT_TEST_MAX_V` (batería test máximo). Su unidad es voltios.
constexpr float BAT_TEST_MAX_V = 35.0f;
// Permiso de descarga separado de la lectura raw: filtrar y confirmar evita
// reiniciar la deteccion por una sola muestra. La ventana ampliada no valida
// descargar una bateria12 V hasta4 V; mantener supervision fisica del ensayo.
// [Guía] Fija el parámetro `BAT_VOLTAGE_FILTER_TAU_US` (batería tensión filtro TAU). Su unidad es microsegundos.
constexpr uint32_t BAT_VOLTAGE_FILTER_TAU_US = 20000;
// [Guía] Fija el parámetro `BAT_VOLTAGE_START_MARGIN_V` (batería tensión start margen). Su unidad es voltios.
constexpr float BAT_VOLTAGE_START_MARGIN_V = 0.05f;
// [Guía] Fija el parámetro `BAT_VOLTAGE_READY_US` (batería tensión listo). Su unidad es microsegundos.
constexpr uint32_t BAT_VOLTAGE_READY_US = 50000;
// [Guía] Comprueba `BAT_VOLTAGE_FILTER_TAU_US > SAMPLE_US && BAT_VOLTAGE_READY_US > 0` durante la compilación.
static_assert(BAT_VOLTAGE_FILTER_TAU_US > SAMPLE_US && BAT_VOLTAGE_READY_US > 0
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && BAT_VOLTAGE_START_MARGIN_V > 0,
              "Invalid battery voltage qualification");
// Porcentajes0..100, no fracciones0..1. EMS exige ademas SOC continuo/valido.
// [Guía] Centraliza el ajuste `BAT_DISCHARGE_MIN_SOC` (batería descarga mínimo SOC) para compartirlo entre los
// [Guía] módulos que lo usan.
constexpr float BAT_DISCHARGE_MIN_SOC = 15.0f;
// [Guía] Centraliza el ajuste `BAT_CHARGE_MAX_SOC` (batería carga máximo SOC) para compartirlo entre los módulos
// [Guía] que lo usan.
constexpr float BAT_CHARGE_MAX_SOC = 90.0f;
// Techo OPERATIVO de carga sobre Vbat supervisor, no solo diagnostico.
// Valor26V recibido del usuario: revisar/restablecer segun bateria real.
// Un fusible no protege contra sobretension de carga de una bateria12V.
// [Guía] Fija el parámetro `BAT_CHARGE_MAX_V` (batería carga máximo). Su unidad es voltios.
constexpr float BAT_CHARGE_MAX_V = 35.0f;
// [Guía] Comprueba `BAT_CHARGE_MAX_V > BAT_TEST_MIN_V` durante la compilación.
static_assert(BAT_CHARGE_MAX_V > BAT_TEST_MIN_V
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && BAT_CHARGE_MAX_V <= BAT_TEST_MAX_V, "Invalid charge voltage permission");
// Retirar la carga con margen para terminar la rampa antes del techo de SOC.
// [Guía] Centraliza el ajuste `BAT_CHARGE_EXIT_SOC` (batería carga exit SOC) para compartirlo entre los módulos
// [Guía] que lo usan.
constexpr float BAT_CHARGE_EXIT_SOC = 89.8f;
// M2: sin demanda externa de bus, cargar bateria SOLO por Q2. Ibat negativa
// significa carga; el valor positivo de abajo es su magnitud objetivo en A.
// Ganancias INICIALES de ensayo: no reutilizar las ganancias del PI de bus.
// [Guía] Centraliza el ajuste `M2_ENABLED` (M2 enabled) para compartirlo entre los módulos que lo usan.
constexpr bool M2_ENABLED = true;
// [Guía] Fija la magnitud objetivo de 1,2 A. El diagnóstico de M2 la muestra con signo negativo porque la batería
// [Guía] absorbe corriente.
constexpr float M2_CHARGE_CURRENT_A = 1.20f;
// [Guía] Define la ganancia proporcional al error en el lazo M2 corriente.
constexpr float M2_CURRENT_KP = 0.0025f; // duty/A; mas Q2 aumenta carga negativa.
// [Guía] Define la ganancia integral del error acumulado con el tiempo en el lazo M2 corriente.
constexpr float M2_CURRENT_KI = 0.08f; // duty/(A*s), pendiente de sintonizacion real.
// [Guía] Centraliza el ajuste `M2_CURRENT_RAMP_A_PER_SECOND` (M2 corriente rampa A PER second) para compartirlo
// [Guía] entre los módulos que lo usan.
constexpr float M2_CURRENT_RAMP_A_PER_SECOND = 0.30f;
// [Guía] Centraliza el ajuste `M2_Q2_SLEW_PER_SECOND` (M2 Q2 pendiente PER second) para compartirlo entre los
// [Guía] módulos que lo usan.
constexpr float M2_Q2_SLEW_PER_SECOND = 0.20f;
// [Guía] Fija el parámetro `M2_CURRENT_RELIEF_A` (M2 corriente relief). Su unidad es amperios.
constexpr float M2_CURRENT_RELIEF_A = 1.50f; // Alivio de control, NO calibre de bateria.
// [Guía] Define 80 mA como límite de baja demanda externa al calificar la entrada a M2.
constexpr float M2_BUS_IDLE_A = 0.08f; // Inicia evidencia de bus descargado.
// [Guía] Usa un umbral superior, 150 mA, para confirmar el regreso de demanda. La separación entre umbrales
// [Guía] reduce cambios repetidos de modo.
constexpr float M2_BUS_WAKE_A = 0.15f; // Retorno de demanda; para entrar siguen exigiendose80mA.
// Ibus tiene ventana propia corta; no esperar el antiguo segundo adicional.
// [Guía] Calcula un promedio de corriente de bus por bloques de 5 ms para decidir si hay carga externa.
constexpr uint32_t M2_BUS_CURRENT_WINDOW_US = 5000;
// [Guía] Mantiene ocho bloques de corriente en el filtro de confirmación. Con bloques de 5 ms cubre unos 40 ms.
constexpr uint8_t M2_BUS_GUARD_WINDOWS = 8; // Promedio movil de8 bloques: unos40ms.
// [Guía] Exige que la baja demanda cumpla el tiempo indicado después de que las ventanas produzcan medidas
// [Guía] útiles.
constexpr uint32_t M2_BUS_IDLE_US = 15000; // Ambos promedios bajos durante15ms.
// [Guía] Requiere 100 ms de evidencia sostenida de regreso de demanda para iniciar la salida de M2.
constexpr uint32_t M2_BUS_WAKE_US = 100000;
// [Guía] Fija el parámetro `M2_PREP_TIMEOUT_US` (M2 prep vencimiento). Su unidad es microsegundos.
constexpr uint32_t M2_PREP_TIMEOUT_US = 4000000;
// [Guía] Fija el parámetro `M2_RETURN_TIMEOUT_US` (M2 return vencimiento). Su unidad es microsegundos.
constexpr uint32_t M2_RETURN_TIMEOUT_US = 2000000;
// [Guía] Fija el parámetro `M2_REENTRY_COOLDOWN_US` (M2 reentry cooldown). Su unidad es microsegundos.
constexpr uint32_t M2_REENTRY_COOLDOWN_US = 0; // No retener M3/M4 tras un nuevo corte real.
// M2 no exige margen Vpv-Vbat: ni para entrar ni para mantener Ibat_ref.
// La presencia/validez de la fuente PV se supervisa en la EMS.
// Por peticion del usuario M2 conserva BAT_CHARGE_MAX_V, sin inventar una
// tension CV. El valor heredado26V NO valida cargar una bateria nominal12V.
// Este modo implementa CC con supervision, no un cargador CC/CV certificado.
// [Guía] Comprueba `M2_CURRENT_KP >= 0 && M2_CURRENT_KI > 0 && M2_CHARGE_CURRENT_A > 0` durante la compilación.
static_assert(M2_CURRENT_KP >= 0 && M2_CURRENT_KI > 0 && M2_CHARGE_CURRENT_A > 0
  // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
  && M2_CHARGE_CURRENT_A < M2_CURRENT_RELIEF_A && M2_CURRENT_RELIEF_A < BAT_CHARGE_SOFT_A
  // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
  && M2_BUS_IDLE_A > 0 && M2_BUS_WAKE_A > M2_BUS_IDLE_A
  // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
  && M2_BUS_CURRENT_WINDOW_US >= 4 * SAMPLE_US && M2_BUS_WAKE_US >= 50000
  // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
  && M2_BUS_GUARD_WINDOWS >= 2 && M2_BUS_GUARD_WINDOWS <= 16
  // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
  && M2_Q2_SLEW_PER_SECOND > 0 && M2_CURRENT_RAMP_A_PER_SECOND > 0,
  "Invalid M2 current-control parameters");
// Corte del ensayo44V recibido del usuario; no se eleva para corregir USB.
// [Guía] Fija el parámetro `BAT_TEST_BUS_TRIP_V` (batería test bus DC trip). Su unidad es voltios.
constexpr float BAT_TEST_BUS_TRIP_V = 54.0f;
// M5 solamente: confirmar un exceso moderado. El usuario actualizo Cdc a
// 55 V segun comentario historico; el Config adjunto trae cortes44/45V.
// No deducir el limite del sistema solo de la tension nominal de Cdc.
// [Guía] Fija el parámetro `BAT_BUS_HARD_TRIP_V` (batería bus DC hard trip). Su unidad es voltios.
constexpr float BAT_BUS_HARD_TRIP_V = 55.0f;
// [Guía] Fija el parámetro `BAT_BUS_CONFIRM_US` (batería bus DC confirm). Su unidad es microsegundos.
constexpr uint32_t BAT_BUS_CONFIRM_US = 1000;
// [Guía] Comprueba `BAT_BUS_HARD_TRIP_V > BAT_TEST_BUS_TRIP_V` durante la compilación.
static_assert(BAT_BUS_HARD_TRIP_V > BAT_TEST_BUS_TRIP_V
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && BAT_BUS_HARD_TRIP_V <= 55.0f
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && BAT_BUS_CONFIRM_US >= SAMPLE_US
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && BAT_BUS_CONFIRM_US <= 2 * SAMPLE_US, "Invalid bus confirmation");
// M5 experimental: umbral de descarga superior a80 mA. La confirmacion de
// enlace abajo requiere primero contexto pasivo, sin orden manual.
// [Guía] Define la magnitud de 80 mA usada como umbral de evidencia de flujo de batería. La confirmación de
// [Guía] conexión se realiza en la EMS.
constexpr float BAT_CONNECT_CURRENT_A = 0.08f;
// Cero es valido en reposo/rearme, pero no autoriza PWM con realimentacion nula.
// [Guía] Fija el parámetro `BAT_BUS_START_MIN_V` (batería bus DC start mínimo). Su unidad es voltios.
constexpr float BAT_BUS_START_MIN_V = 1.0f;
// Confirmacion de descarga pasiva: no aplica a corriente con PV activo.
// [Guía] Fija el parámetro `BAT_CONNECT_HOLD_US` (batería connect confirmación sostenida). Su unidad es
// [Guía] microsegundos.
constexpr uint32_t BAT_CONNECT_HOLD_US = 300000;
// Arranque pasivo en WAIT, PV apagado confirmado y las tres PWM OFF.
// Exigir descarga POSITIVA, no magnitud absoluta de una lectura de carga.
// Confirmar la media de Ibat y su supervisor, no cada muestra rapida. La
// tension PV califica reposo; un offset de Ipv con Vpv=0 no lo invalida.
// M1 tiene abajo su propia deteccion en ambas polaridades durante1s.
// Sigue siendo una estimacion, no informacion fisica del interruptor.
// [Guía] Fija el parámetro `BAT_LINK_DETECT_A` (batería link detect). Su unidad es amperios.
constexpr float BAT_LINK_DETECT_A = BAT_CONNECT_CURRENT_A;
// [Guía] Fija el parámetro `BAT_LINK_PV_OFF_US` (batería link panel fotovoltaico OFF). Su unidad es
// [Guía] microsegundos.
constexpr uint32_t BAT_LINK_PV_OFF_US = 200000;
// [Guía] Fija el parámetro `BAT_LINK_CONFIRM_US` (batería link confirm). Su unidad es microsegundos.
constexpr uint32_t BAT_LINK_CONFIRM_US = BAT_CONNECT_HOLD_US;
// [Guía] Fija el parámetro `BAT_CURRENT_EMS_TAU_US` (batería corriente EMS TAU). Su unidad es microsegundos.
constexpr uint32_t BAT_CURRENT_EMS_TAU_US = 50000;
// M1: MEDIA y SUPERVISOR fuera de+/-80mA durante1s continuo. La lectura
// rapida conserva protecciones, pero sus valles no reinician este segundo.
// No exige reposo previo ni bus20V. V61 separa este segundo de los permisos
// SOC/PWM/tension: se comprueban al actuar, sin borrar flujo valido por ellos.
// Media50ms, filtros y calificacion de
// tension pueden agregar latencia. Sigue siendo una estimacion de conexion,
// no lectura del interruptor; una interferencia persistente puede imitar flujo.
// QUIET y BASELINE_BAND solo aprenden referencia opcional para respuesta Q2.
// [Guía] Define el tiempo de reposo requerido al calificar una línea base del detector M1.
constexpr uint32_t M1_BAT_QUIET_US = 200000;
// [Guía] Exige que la evidencia de corriente dure un segundo en el detector M1 antes de confirmar la conexión.
constexpr uint32_t M1_BAT_CONNECT_US = 1000000;
// [Guía] Define la tolerancia de 40 mA empleada al calificar la estabilidad de la línea base de corriente en M1.
constexpr float M1_BAT_BASELINE_BAND_A = 0.04f;
// [Guía] Comprueba `M1_BAT_QUIET_US >= 2 * BAT_CURRENT_EMS_TAU_US` durante la compilación.
static_assert(M1_BAT_QUIET_US >= 2 * BAT_CURRENT_EMS_TAU_US
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && M1_BAT_CONNECT_US >= BAT_CURRENT_EMS_TAU_US
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && M1_BAT_BASELINE_BAND_A > 0
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && M1_BAT_BASELINE_BAND_A < BAT_CONNECT_CURRENT_A,
              "Invalid M1 battery connection qualification");
// [Guía] Fija el parámetro `BAT_CHARGE_READY_US` (batería carga listo). Su unidad es microsegundos.
constexpr uint32_t BAT_CHARGE_READY_US = 20000;
// [Guía] Fija el parámetro `BAT_CHARGE_ABORT_A` (batería carga abort). Su unidad es amperios.
constexpr float BAT_CHARGE_ABORT_A = 0.20f;
// [Guía] Fija el parámetro `BAT_CHARGE_ABORT_US` (batería carga abort). Su unidad es microsegundos.
constexpr uint32_t BAT_CHARGE_ABORT_US = 200000;
// [Guía] Comprueba `BAT_LINK_DETECT_A >= BAT_CONNECT_CURRENT_A` durante la compilación.
static_assert(BAT_LINK_DETECT_A >= BAT_CONNECT_CURRENT_A
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && BAT_LINK_DETECT_A < BAT_DISCHARGE_SOFT_A
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && BAT_LINK_PV_OFF_US >= SAMPLE_US
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && BAT_LINK_CONFIRM_US >= BAT_LINK_PV_OFF_US
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && BAT_CURRENT_EMS_TAU_US > SAMPLE_US
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && BAT_CHARGE_ABORT_A > BAT_CONNECT_CURRENT_A,
              "Invalid battery link/current qualification");
// Cero operativo es una BANDA, no ADC exactamente igual al offset calibrado.
// ZERO_HOLD confirma esa banda; INTERLOCK exige intervalo sin Q2 ni Q3.
// [Guía] Fija el parámetro `BAT_ZERO_CURRENT_A` (batería zero corriente). Su unidad es amperios.
constexpr float BAT_ZERO_CURRENT_A = 0.10f;
// [Guía] Fija el parámetro `BAT_ZERO_HOLD_US` (batería zero confirmación sostenida). Su unidad es microsegundos.
constexpr uint32_t BAT_ZERO_HOLD_US = 100000;
// [Guía] Fija el parámetro `BAT_INTERLOCK_US` (batería interlock). Su unidad es microsegundos.
constexpr uint32_t BAT_INTERLOCK_US = 1000;
// Q3 OFF antes de Q2: intervalo minimo sin ambas PWM, independiente del signo
// de Ibat. No esperar carga negativa antes de habilitar el PI de Q2.
// [Guía] Comprueba `BAT_INTERLOCK_US >= SAMPLE_US, );` durante la compilación.
static_assert(BAT_INTERLOCK_US >= SAMPLE_US, "Invalid handover");
// Permitir retirar Q2 desde 95% a 0 con la pendiente conservadora del ensayo.
// [Guía] Fija el parámetro `BAT_TRANSFER_TIMEOUT_US` (batería transfer vencimiento). Su unidad es microsegundos.
constexpr uint32_t BAT_TRANSFER_TIMEOUT_US = 12000000;
// [Guía] Fija el parámetro `BAT_BUS_STABLE_BAND_V` (batería bus DC stable band). Su unidad es voltios.
constexpr float BAT_BUS_STABLE_BAND_V = 0.5f;
// Recuperacion operativa ANTES de los cortes generales de35/40 V.
// Medida filtrada y persistencia; no convertir un pico raw en permiso de carga.
// [Guía] Fija el parámetro `EMS_BUS_RECOVER_ON_V` (EMS bus DC recuperación ON). Su unidad es voltios.
constexpr float EMS_BUS_RECOVER_ON_V = 20.8f;
// [Guía] Fija el parámetro `EMS_BUS_RECOVER_OFF_V` (EMS bus DC recuperación OFF). Su unidad es voltios.
constexpr float EMS_BUS_RECOVER_OFF_V = 20.3f;
// [Guía] Fija el parámetro `EMS_BUS_RECOVER_ON_US` (EMS bus DC recuperación ON). Su unidad es microsegundos.
constexpr uint32_t EMS_BUS_RECOVER_ON_US = 20000;
// [Guía] Fija el parámetro `EMS_BUS_RECOVER_OFF_US` (EMS bus DC recuperación OFF). Su unidad es microsegundos.
constexpr uint32_t EMS_BUS_RECOVER_OFF_US = 100000;
// Confirmacion continua de bus alto antes de preparar Vpv en M4 (2 s).
// [Guía] Fija el parámetro `EMS_M4_PREP_HIGH_BUS_HOLD_US` (EMS M4 prep high bus DC confirmación sostenida). Su
// [Guía] unidad es microsegundos.
constexpr uint32_t EMS_M4_PREP_HIGH_BUS_HOLD_US = 2000000;
// Bateria conectada desde M1: preparar PV bajo aunque el bus ya regule20 V.
// [Guía] Fija el parámetro `EMS_HOTPLUG_PV_PREP_WAIT_US` (EMS hotplug panel fotovoltaico prep wait). Su unidad es
// [Guía] microsegundos.
constexpr uint32_t EMS_HOTPLUG_PV_PREP_WAIT_US = 2000000;
// Preparar por encima del minimo2 V para tolerar la caida durante el handoff.
// El margen2 V autoriza ENTRADA y transferencia. En M3 mandan MPPT y PI Q2;
// una caida de ese margen no es por si sola una orden de volver a M4.
// [Guía] Fija el parámetro `EMS_PV_BAT_MARGIN_V` (EMS panel fotovoltaico batería margen). Su unidad es voltios.
constexpr float EMS_PV_BAT_MARGIN_V = 2.0f;
// [Guía] Fija el parámetro `EMS_CHARGE_PREP_HEADROOM_V` (EMS carga prep headroom). Su unidad es voltios.
constexpr float EMS_CHARGE_PREP_HEADROOM_V = 0.60f;
// Tras dos transferencias fallidas, exigir una mejora NUEVA del panel. No
// rearmar solo porque pasa tiempo ni esperar un bus20 V que Q3 no puede absorber.
// [Guía] Fija el parámetro `EMS_CHARGE_REARM_PV_RISE_V` (EMS carga rearm panel fotovoltaico rise). Su unidad es
// [Guía] voltios.
constexpr float EMS_CHARGE_REARM_PV_RISE_V = 1.0f;
// [Guía] Fija el parámetro `EMS_CHARGE_REARM_COOLDOWN_US` (EMS carga rearm cooldown). Su unidad es microsegundos.
constexpr uint32_t EMS_CHARGE_REARM_COOLDOWN_US = 2000000;
// [Guía] Fija el parámetro `EMS_CHARGE_REARM_CONFIRM_US` (EMS carga rearm confirm). Su unidad es microsegundos.
constexpr uint32_t EMS_CHARGE_REARM_CONFIRM_US = 500000;
// [Guía] Comprueba `EMS_CHARGE_PREP_HEADROOM_V > 0 && EMS_CHARGE_PREP_HEADROOM_V <= 1.0f` durante la compilación.
static_assert(EMS_CHARGE_PREP_HEADROOM_V > 0 && EMS_CHARGE_PREP_HEADROOM_V <= 1.0f
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && EMS_CHARGE_REARM_PV_RISE_V > 0
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && EMS_CHARGE_REARM_CONFIRM_US >= 3 * SAMPLE_US
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && EMS_CHARGE_REARM_COOLDOWN_US >= EMS_CHARGE_REARM_CONFIRM_US,
              "Invalid charge retry qualification");
// Soporte fuera de M3 y diagnostico; no es una salida autonoma de M3.
// [Guía] Fija el parámetro `EMS_CHARGE_EXIT_MARGIN_V` (EMS carga exit margen). Su unidad es voltios.
constexpr float EMS_CHARGE_EXIT_MARGIN_V = 1.0f;
// [Guía] Fija el parámetro `EMS_DEFICIT_HOLD_US` (EMS deficit confirmación sostenida). Su unidad es
// [Guía] microsegundos.
constexpr uint32_t EMS_DEFICIT_HOLD_US = 200000;
// Parametros historicos V29, sin uso en V32: ya no existe PREP_M3.
// Se conservan para compatibilidad con configuraciones locales. La ruta
// actual siempre entrega MPPT a Q1 y el PI a Q3 en M4 antes de evaluar Q2.
// [Guía] Centraliza el ajuste `M1_M3_Q1_SLEW_PER_SECOND` (M1 M3 Q1 pendiente PER second) para compartirlo entre
// [Guía] los módulos que lo usan.
constexpr float M1_M3_Q1_SLEW_PER_SECOND = 0.20f;
// [Guía] Fija el parámetro `M1_M3_PV_HEADROOM_V` (M1 M3 panel fotovoltaico headroom). Su unidad es voltios.
constexpr float M1_M3_PV_HEADROOM_V = 0.30f;
// [Guía] Fija el parámetro `M1_M3_PREP_TIMEOUT_US` (M1 M3 prep vencimiento). Su unidad es microsegundos.
constexpr uint32_t M1_M3_PREP_TIMEOUT_US = 8000000;
// [Guía] Comprueba `M1_M3_Q1_SLEW_PER_SECOND > 0 && M1_M3_Q1_SLEW_PER_SECOND <= 0.25f` durante la compilación.
static_assert(M1_M3_Q1_SLEW_PER_SECOND > 0 && M1_M3_Q1_SLEW_PER_SECOND <= 0.25f
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && M1_M3_PV_HEADROOM_V >= 0 && M1_M3_PV_HEADROOM_V <= 0.5f
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && M1_M3_PREP_TIMEOUT_US > EMS_DEFICIT_HOLD_US
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && M1_M3_PREP_TIMEOUT_US < BAT_TRANSFER_TIMEOUT_US,
              "Invalid direct M1 to M3 preparation");
// [Guía] Comprueba `EMS_BUS_RECOVER_ON_V > EMS_BUS_RECOVER_OFF_V` durante la compilación.
static_assert(EMS_BUS_RECOVER_ON_V > EMS_BUS_RECOVER_OFF_V
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && EMS_BUS_RECOVER_ON_V < BAT_TEST_BUS_TRIP_V
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && EMS_BUS_RECOVER_OFF_V > 20.0f
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && EMS_BUS_RECOVER_ON_US >= SAMPLE_US
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && EMS_BUS_RECOVER_OFF_US > EMS_BUS_RECOVER_ON_US,
              "Invalid EMS recovery hysteresis");
// Histeresis de presencia PV: umbral ON mayor que OFF evita conmutar por ruido.
// No expresa la tension del MPP ni garantiza potencia util para el bus.
// [Guía] Umbral de tensión para la entrada del PV. La EMS combina esta evidencia con corriente, configuración y
// [Guía] permanencia según el estado.
constexpr float BAT_PV_ON_V = 6.0f;
// [Guía] Umbral de tensión para pérdida de PV. Está por debajo del umbral de entrada para introducir histéresis.
constexpr float BAT_PV_OFF_V = 4.0f;
// PV presente por tension o corriente; corriente sola requiere al menos1 V
// para no interpretar como panel un offset del ACS712 con PV apagado.
// [Guía] Fija el parámetro `BAT_PV_ON_CURRENT_A` (batería panel fotovoltaico ON corriente). Su unidad es
// [Guía] amperios.
constexpr float BAT_PV_ON_CURRENT_A = 0.080f;
// [Guía] Fija el parámetro `BAT_PV_OFF_CURRENT_A` (batería panel fotovoltaico OFF corriente). Su unidad es
// [Guía] amperios.
constexpr float BAT_PV_OFF_CURRENT_A = 0.030f;
// [Guía] Fija el parámetro `BAT_PV_HOLD_US` (batería panel fotovoltaico confirmación sostenida). Su unidad es
// [Guía] microsegundos.
constexpr uint32_t BAT_PV_HOLD_US = 20000;
// [Guía] Fija el parámetro `PV_CURRENT_DETECT_MIN_V` (panel fotovoltaico corriente detect mínimo). Su unidad es
// [Guía] voltios.
constexpr float PV_CURRENT_DETECT_MIN_V = 1.0f;
// Perdida de aporte PV: la tension puede permanecer por capacitancia/backfeed.
// Tras observar corriente, confirmar falta de aporte; al arrancar dar margen.
// [Guía] Fija el parámetro `PV_NO_FLOW_HOLD_US` (panel fotovoltaico NO flow confirmación sostenida). Su unidad es
// [Guía] microsegundos.
constexpr uint32_t PV_NO_FLOW_HOLD_US = 300000;
// [Guía] Fija el parámetro `PV_START_GRACE_US` (panel fotovoltaico start grace). Su unidad es microsegundos.
constexpr uint32_t PV_START_GRACE_US = 2000000;
// Bateria sin respuesta SOLO si el actuador deberia demandar corriente.
// Ibat=0 con Q3 minimo y PV sosteniendo el bus no confirma una desconexion.
// No exigir500 ms de muestras rapidas todas dentro de50 mA: un pico aislado
// reiniciaba la salida M3->M1. Promediar ventanas y confirmar la falta de flujo.
// [Guía] Fija el parámetro `BAT_CURRENT_WINDOW_US` (batería corriente ventana). Su unidad es microsegundos.
constexpr uint32_t BAT_CURRENT_WINDOW_US = 50000;
// [Guía] Fija el parámetro `BAT_NO_RESPONSE_A` (batería NO response). Su unidad es amperios.
constexpr float BAT_NO_RESPONSE_A = 0.08f;
// [Guía] Fija el parámetro `BAT_CHARGE_FLOW_SEEN_A` (batería carga flow seen). Su unidad es amperios.
constexpr float BAT_CHARGE_FLOW_SEEN_A = 0.10f;
// [Guía] Define batería Q3 response ciclo de trabajo como fracción del periodo PWM. Multiplicar por 100 da el
// [Guía] porcentaje.
constexpr float BAT_Q3_RESPONSE_DUTY = 0.10f;
// [Guía] Fija el parámetro `BAT_Q2_DRIVE_MARGIN_V` (batería Q2 drive margen). Su unidad es voltios.
constexpr float BAT_Q2_DRIVE_MARGIN_V = 0.5f;
// [Guía] Fija el parámetro `BAT_NO_RESPONSE_HOLD_US` (batería NO response confirmación sostenida). Su unidad es
// [Guía] microsegundos.
constexpr uint32_t BAT_NO_RESPONSE_HOLD_US = 200000;
// En M4 estable, una rama sin aporte puede retirarse hacia M1. No demuestra
// apertura fisica: tambien puede ser una bateria conectada que no es necesaria.
// [Guía] Fija el parámetro `BAT_IDLE_RETURN_US` (batería sin demanda return). Su unidad es microsegundos.
constexpr uint32_t BAT_IDLE_RETURN_US = 1000000;
// [Guía] Fija el parámetro `BAT_IDLE_BUS_BAND_V` (batería sin demanda bus DC band). Su unidad es voltios.
constexpr float BAT_IDLE_BUS_BAND_V = 0.5f;
// [Guía] Comprueba `BAT_CURRENT_WINDOW_US >= 20 * SAMPLE_US` durante la compilación.
static_assert(BAT_CURRENT_WINDOW_US >= 20 * SAMPLE_US
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && BAT_NO_RESPONSE_HOLD_US >= 2 * BAT_CURRENT_WINDOW_US
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && BAT_IDLE_RETURN_US > BAT_NO_RESPONSE_HOLD_US
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && BAT_CHARGE_FLOW_SEEN_A > BAT_NO_RESPONSE_A,
              "Invalid battery response qualification");
// [Guía] Centraliza el ajuste `Q3_SLEW_PER_SECOND` (Q3 pendiente PER second) para compartirlo entre los módulos
// [Guía] que lo usan.
constexpr float Q3_SLEW_PER_SECOND = 1.0f;
// Pendientes del Config local del ensayo; los cortes de35/40 V se conservan.
// [Guía] Centraliza el ajuste `Q2_SLEW_PER_SECOND` (Q2 pendiente PER second) para compartirlo entre los módulos
// [Guía] que lo usan.
constexpr float Q2_SLEW_PER_SECOND = 1.0f;
// [Guía] Centraliza el ajuste `Q1_TEST_SLEW_PER_SECOND` (Q1 test pendiente PER second) para compartirlo entre los
// [Guía] módulos que lo usan.
constexpr float Q1_TEST_SLEW_PER_SECOND = 0.60f;
// [Guía] Comprueba `BAT_DISCHARGE_SOFT_A < BAT_DISCHARGE_TRIP_A` durante la compilación.
static_assert(BAT_DISCHARGE_SOFT_A < BAT_DISCHARGE_TRIP_A
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && BAT_CHARGE_SOFT_A < BAT_CHARGE_TRIP_A, "Invalid current margins");
// [Guía] Comprueba `BAT_TEST_BUS_TRIP_V > 20.0f && BAT_TEST_BUS_TRIP_V < BAT_BUS_HARD_TRIP_V` durante la
// [Guía] compilación.
static_assert(BAT_TEST_BUS_TRIP_V > 20.0f && BAT_TEST_BUS_TRIP_V < BAT_BUS_HARD_TRIP_V,
              "Moderate bus trip must precede hard trip");
// [Guía] Comprueba `BAT_BUS_START_MIN_V > 0 && BAT_BUS_START_MIN_V < 20.0f` durante la compilación.
static_assert(BAT_BUS_START_MIN_V > 0 && BAT_BUS_START_MIN_V < 20.0f,
              "Invalid passive bus startup qualification");
// [Guía] Comprueba `BAT_TEST_MIN_V < BAT_TEST_MAX_V && BAT_PV_OFF_V < BAT_PV_ON_V` durante la compilación.
static_assert(BAT_TEST_MIN_V < BAT_TEST_MAX_V && BAT_PV_OFF_V < BAT_PV_ON_V,
              "Invalid battery/PV voltage limits");
// [Guía] Comprueba `BAT_PV_OFF_CURRENT_A < BAT_PV_ON_CURRENT_A` durante la compilación.
static_assert(BAT_PV_OFF_CURRENT_A < BAT_PV_ON_CURRENT_A,
              "Invalid solar current hysteresis");
// Q1 usa el mismo techo de ensayo general. El anterior corte separado a
// 23 V abortaba la conexion solar antes de completar la rampa de Q1.
// No eleva el limite general ni demuestra seguridad de la etapa.
// [Guía] Fija el parámetro `MPPT_BUS_MAX_V` (mppt bus DC máximo). Su unidad es voltios.
constexpr float MPPT_BUS_MAX_V = BAT_TEST_BUS_TRIP_V;
// Sin ventana operativa que pause MPPT en M4; conservan prioridad los disparos.
// [Guía] Fija el parámetro `MPPT_BUS_CONFIRM_US` (mppt bus DC confirm). Su unidad es microsegundos.
constexpr uint32_t MPPT_BUS_CONFIRM_US = 1000;
// Suavizado EMS y PI Q2. Q1/Q3 usan el bus rapido ya filtrado; cortes raw aparte.
// [Guía] Fija el parámetro `EMS_BUS_FILTER_TAU_US` (EMS bus DC filtro TAU). Su unidad es microsegundos.
constexpr uint32_t EMS_BUS_FILTER_TAU_US = 50000;
// [Guía] Comprueba `EMS_BUS_FILTER_TAU_US > SAMPLE_US, );` durante la compilación.
static_assert(EMS_BUS_FILTER_TAU_US > SAMPLE_US, "Invalid supervisory bus filter");
// [Guía] Comprueba `MPPT_BUS_CONFIRM_US >= SAMPLE_US` durante la compilación.
static_assert(MPPT_BUS_CONFIRM_US >= SAMPLE_US
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && MPPT_BUS_CONFIRM_US <= 2 * SAMPLE_US,
              "Invalid MPPT acquisition qualification");
// [Guía] Fija el parámetro `CONTROL_US` (control). Su unidad es microsegundos.
constexpr uint32_t CONTROL_US = 1000; // PI/EMS nominal1 kHz, diferente de ADC2 kHz.
// Tolerancia de ensayo; no sustituye un watchdog/proteccion de hardware.
// [Guía] Fija el parámetro `CONTROL_TIMEOUT_US` (control vencimiento). Su unidad es microsegundos.
constexpr uint32_t CONTROL_TIMEOUT_US = 20000;
// [Guía] Comprueba `CONTROL_TIMEOUT_US > 2 * CONTROL_US, );` durante la compilación.
static_assert(CONTROL_TIMEOUT_US > 2 * CONTROL_US, "Control timeout too short");
// [Guía] Fija en 20 V el objetivo de los modos que regulan el bus. El lazo de M2 usa una consigna de corriente
// [Guía] separada.
constexpr float BUS_REFERENCE_V = 20.0f;
// Ensayo local en la rama PV observada: error = Vbus - referencia.
// No es un ajuste validado para toda la curva solar.
// Ajuste reportado en Simulink con PI paralelo discreto; pendiente de validar
// equivalencia de muestreo/integracion y respuesta en el montaje fisico.
// [Guía] Define la ganancia proporcional al error en el lazo bus DC.
constexpr float BUS_KP = 0.00678f; // Ajuste restaurado y probado estable por el usuario.
// [Guía] Define la ganancia integral del error acumulado con el tiempo en el lazo bus DC.
constexpr float BUS_KI = 0.038f; // Ki multiplica dt en segundos; no cambiar para frenar ACQUIRE.
// V45: ensayo reversible. false entrega directamente Q1 al PI tras START_M1;
// true restaura la adquisicion/confirmacion de rama de V44, sin borrar su codigo.
// [Guía] Centraliza el ajuste `M1_BRANCH_ACQUIRE_ENABLED` (M1 branch acquire enabled) para compartirlo entre los
// [Guía] módulos que lo usan.
constexpr bool M1_BRANCH_ACQUIRE_ENABLED = true;
// V47: recuperar las tres subetapas, acortando SOLO la confirmacion de rama.
// El supervisor50ms y los techos de adquisicion/corriente no se aceleran.
// [Guía] Fija el parámetro `M1_FAST_BRANCH_CONFIRM_US` (M1 fast branch confirm). Su unidad es microsegundos.
constexpr uint32_t M1_FAST_BRANCH_CONFIRM_US = 50000;
// Ruta anterior (PV_VOLTAGE_MPPT_ENABLED=false): dejar asentar Q2 antes de P&O.
// V59 no usa esa espera: el lazo PV de Q1 sigue activo durante la rampa Q2.
// No exigir bus20V: un Q2 saturado tambien debe permitir buscar potencia.
// [Guía] Centraliza el ajuste `M3_MPPT_CONTEXT_REBASE_ENABLED` (M3 mppt context rebase enabled) para compartirlo
// [Guía] entre los módulos que lo usan.
constexpr bool M3_MPPT_CONTEXT_REBASE_ENABLED = true;
// [Guía] Centraliza el ajuste `M3_MPPT_Q2_SETTLE_BAND` (M3 mppt Q2 asentamiento band) para compartirlo entre los
// [Guía] módulos que lo usan.
constexpr float M3_MPPT_Q2_SETTLE_BAND = 0.01f; // 1 punto porcentual acumulado.
// [Guía] Fija el parámetro `M3_MPPT_Q2_SETTLE_US` (M3 mppt Q2 asentamiento). Su unidad es microsegundos.
constexpr uint32_t M3_MPPT_Q2_SETTLE_US = 300000;
// [Guía] Comprueba `M1_FAST_BRANCH_CONFIRM_US > 0 && M3_MPPT_Q2_SETTLE_US > 0` durante la compilación.
static_assert(M1_FAST_BRANCH_CONFIRM_US > 0 && M3_MPPT_Q2_SETTLE_US > 0
  // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
  && M3_MPPT_Q2_SETTLE_BAND > 0, "Invalid branch/context confirmation");
// Preparacion exclusiva tras detectar bateria en M1: primero Vpv, luego P&O.
// No se fija un duty de MPP. Bajar Q1 eleva Vpv segun la respuesta del banco.
// [Guía] Centraliza el ajuste `HOTPLUG_PV_SEEK_ENABLED` (hotplug panel fotovoltaico seek enabled) para
// [Guía] compartirlo entre los módulos que lo usan.
constexpr bool HOTPLUG_PV_SEEK_ENABLED = true;
// [Guía] Fija el parámetro `HOTPLUG_PV_TARGET_V` (hotplug panel fotovoltaico target). Su unidad es voltios.
constexpr float HOTPLUG_PV_TARGET_V = 18.4f;
// [Guía] Fija el parámetro `HOTPLUG_PV_TOLERANCE_V` (hotplug panel fotovoltaico tolerance). Su unidad es voltios.
constexpr float HOTPLUG_PV_TOLERANCE_V = 0.2f;
// [Guía] Centraliza el ajuste `HOTPLUG_PV_SLEW_PER_SECOND` (hotplug panel fotovoltaico pendiente PER second) para
// [Guía] compartirlo entre los módulos que lo usan.
constexpr float HOTPLUG_PV_SLEW_PER_SECOND = 0.10f; // Ruta anterior, si se deshabilita V59.
// V59: P&O ajusta Vpv_ref y un PI interno mueve Q1 para seguirla. Q2 sigue
// siendo el PI del bus. No fijar18.4V y buscar MPP con dos duenos de Q1.
// [Guía] Centraliza el ajuste `PV_VOLTAGE_MPPT_ENABLED` (panel fotovoltaico tensión mppt enabled) para
// [Guía] compartirlo entre los módulos que lo usan.
constexpr bool PV_VOLTAGE_MPPT_ENABLED = true;
// [Guía] Define la ganancia proporcional al error en el lazo panel fotovoltaico referencia.
constexpr float PV_REFERENCE_KP = 0.0072f; // duty/V, lazo NUEVO, pendiente de banco.
// [Guía] Define la ganancia integral del error acumulado con el tiempo en el lazo panel fotovoltaico referencia.
constexpr float PV_REFERENCE_KI = 0.038f; // duty/(V*s); no cambia BUS_KP/KI de M1.
// [Guía] Centraliza el ajuste `PV_REFERENCE_SLEW_PER_SECOND` (panel fotovoltaico referencia pendiente PER second)
// [Guía] para compartirlo entre los módulos que lo usan.
constexpr float PV_REFERENCE_SLEW_PER_SECOND = 0.30f; // Preparacion/lazo nuevo:30pp/s.
// [Guía] Fija el parámetro `PV_REFERENCE_MIN_V` (panel fotovoltaico referencia mínimo). Su unidad es voltios.
constexpr float PV_REFERENCE_MIN_V = 6.0f;
// [Guía] Fija el parámetro `PV_REFERENCE_MAX_V` (panel fotovoltaico referencia máximo). Su unidad es voltios.
constexpr float PV_REFERENCE_MAX_V = 26.0f; // Ventana de busqueda, NO proteccion.
// [Guía] Fija el parámetro `PV_REFERENCE_TRACK_BAND_V` (panel fotovoltaico referencia track band). Su unidad es
// [Guía] voltios.
constexpr float PV_REFERENCE_TRACK_BAND_V = 0.60f;
// [Guía] Fija el parámetro `PV_REFERENCE_BOUND_US` (panel fotovoltaico referencia bound). Su unidad es
// [Guía] microsegundos.
constexpr uint32_t PV_REFERENCE_BOUND_US = 500000;
// P&O de referencia PV: pasos en VOLTIOS, no puntos de duty. Se explora mas
// rapido lejos del pico y se afina a0.1V al invertir; confirmar dos ventanas.
// [Guía] Fija el parámetro `PV_MPPT_SETTLE_US` (panel fotovoltaico mppt asentamiento). Su unidad es
// [Guía] microsegundos.
constexpr uint32_t PV_MPPT_SETTLE_US = 100000;
// [Guía] Fija el parámetro `PV_MPPT_AVERAGE_US` (panel fotovoltaico mppt promedio). Su unidad es microsegundos.
constexpr uint32_t PV_MPPT_AVERAGE_US = 300000;
// [Guía] Fija el parámetro `PV_MPPT_INITIAL_STEP_V` (panel fotovoltaico mppt initial paso). Su unidad es voltios.
constexpr float PV_MPPT_INITIAL_STEP_V = 0.50f;
// [Guía] Fija el parámetro `PV_MPPT_MIN_STEP_V` (panel fotovoltaico mppt mínimo paso). Su unidad es voltios.
constexpr float PV_MPPT_MIN_STEP_V = 0.10f;
// [Guía] Fija el parámetro `PV_MPPT_MAX_STEP_V` (panel fotovoltaico mppt máximo paso). Su unidad es voltios.
constexpr float PV_MPPT_MAX_STEP_V = 1.00f;
// Considerar proximidad al rail, no igualdad exacta: el ruido de Vpv puede
// separar Q1 unas decimas de punto del5% sin devolver autoridad al regulador.
// [Guía] Define panel fotovoltaico referencia bound ciclo de trabajo band como fracción del periodo PWM.
// [Guía] Multiplicar por 100 da el porcentaje.
constexpr float PV_REFERENCE_BOUND_DUTY_BAND = 0.005f;
// [Guía] Comprueba `PV_MPPT_MIN_STEP_V > 0 && PV_MPPT_MIN_STEP_V <= PV_MPPT_INITIAL_STEP_V` durante la
// [Guía] compilación.
static_assert(PV_MPPT_MIN_STEP_V > 0 && PV_MPPT_MIN_STEP_V <= PV_MPPT_INITIAL_STEP_V
  // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
  && PV_MPPT_INITIAL_STEP_V <= PV_MPPT_MAX_STEP_V && PV_MPPT_MAX_STEP_V <= 2
  // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
  && PV_MPPT_SETTLE_US > 0 && PV_MPPT_AVERAGE_US >= 100000
  // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
  && PV_REFERENCE_BOUND_DUTY_BAND > 0 && PV_REFERENCE_BOUND_DUTY_BAND < 0.02f
  // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
  && M1_AUX_STABLE_GAP_MS < M1_AUX_INITIAL_STABLE_MS, "Invalid V62 timing/steps");
// Durante la preparacion M1->M4 no sumar descarga si ya hay tension de bus.
// Histeresis: permitir apoyo bajo19.5V y retirarlo por encima20.3V.
// [Guía] Fija el parámetro `HOTPLUG_Q3_SUPPORT_ON_V` (hotplug Q3 support ON). Su unidad es voltios.
constexpr float HOTPLUG_Q3_SUPPORT_ON_V = 19.5f;
// [Guía] Fija el parámetro `HOTPLUG_Q3_SUPPORT_OFF_V` (hotplug Q3 support OFF). Su unidad es voltios.
constexpr float HOTPLUG_Q3_SUPPORT_OFF_V = 20.3f;
// [Guía] Comprueba `PV_REFERENCE_KP >= 0 && PV_REFERENCE_KI > 0` durante la compilación.
static_assert(PV_REFERENCE_KP >= 0 && PV_REFERENCE_KI > 0
  // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
  && PV_REFERENCE_SLEW_PER_SECOND > 0 && PV_REFERENCE_SLEW_PER_SECOND <= Q1_TEST_SLEW_PER_SECOND
  // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
  && PV_REFERENCE_MIN_V > 0 && PV_REFERENCE_MAX_V > HOTPLUG_PV_TARGET_V
  // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
  && PV_REFERENCE_MIN_V < HOTPLUG_PV_TARGET_V
  // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
  && PV_REFERENCE_TRACK_BAND_V > 0 && PV_REFERENCE_BOUND_US > CONTROL_US
  // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
  && HOTPLUG_Q3_SUPPORT_OFF_V > HOTPLUG_Q3_SUPPORT_ON_V,
  "Invalid voltage-reference MPPT settings");
// V46: confirmar el supervisor PV durante200ms a frecuencia ADC, no exigir
// que cada valle de la medida rapida supere18.2 V ni que el PI no pierda pasos.
// La medida rapida conserva el piso Vbat+2 y los cortes globales siguen activos.
// [Guía] Fija el parámetro `HOTPLUG_PV_CONFIRM_US` (hotplug panel fotovoltaico confirm). Su unidad es
// [Guía] microsegundos.
constexpr uint32_t HOTPLUG_PV_CONFIRM_US = 200000;
// [Guía] Fija el parámetro `HOTPLUG_PV_TIMEOUT_US` (hotplug panel fotovoltaico vencimiento). Su unidad es
// [Guía] microsegundos.
constexpr uint32_t HOTPLUG_PV_TIMEOUT_US = 8000000; // No barrer indefinidamente.
// [Guía] Comprueba `HOTPLUG_PV_TARGET_V > HOTPLUG_PV_TOLERANCE_V` durante la compilación.
static_assert(HOTPLUG_PV_TARGET_V > HOTPLUG_PV_TOLERANCE_V
  // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
  && HOTPLUG_PV_TOLERANCE_V >= 0 && HOTPLUG_PV_SLEW_PER_SECOND > 0,
  "Invalid hotplug PV target, tolerance or slew");
// [Guía] Comprueba `HOTPLUG_PV_CONFIRM_US > 0 && HOTPLUG_PV_TIMEOUT_US > HOTPLUG_PV_CONFIRM_US` durante la
// [Guía] compilación.
static_assert(HOTPLUG_PV_CONFIRM_US > 0 && HOTPLUG_PV_TIMEOUT_US > HOTPLUG_PV_CONFIRM_US,
  "PV seek timeout must exceed confirmation");
// Adquirir la rama PV baja donde se probo ese signo de PI. Es un ensayo,
// no un MPPT ni una garantia de regulacion para cualquier curva/carga.
// La rampa puede elevar transitoriamente Vbus antes de reducir Vpv.
// [Guía] Fija el parámetro `M1_LOW_BRANCH_V` (M1 LOW branch). Su unidad es voltios.
constexpr float M1_LOW_BRANCH_V = 17.5f;
// [Guía] Fija el parámetro `M1_REACQUIRE_V` (M1 reacquire). Su unidad es voltios.
constexpr float M1_REACQUIRE_V = 19.5f;
// La seleccion de rama NO usa una muestra instantanea: el registro V19
// cruzaba17.5 V durante el arranque y entregaba el PI demasiado pronto.
// [Guía] Fija el parámetro `M1_PV_FILTER_TAU_US` (M1 panel fotovoltaico filtro TAU). Su unidad es microsegundos.
constexpr uint32_t M1_PV_FILTER_TAU_US = 50000;
// V44: ajuste de barrido probado por el usuario:60puntos porcentuales/segundo.
// El PI establecido conserva Q1_TEST_SLEW_PER_SECOND y sus ganancias probadas.
// V54 conserva0.60/s del Config adjunto; verificar la respuesta fisica.
// [Guía] Centraliza el ajuste `M1_ACQUIRE_SLEW_PER_SECOND` (M1 acquire pendiente PER second) para compartirlo
// [Guía] entre los módulos que lo usan.
constexpr float M1_ACQUIRE_SLEW_PER_SECOND = 0.60f;
// Supervisor confirma rama17.5V; el rapido puede tener rizado hasta18.5V.
// Las guardias de colapso de entrada y sobretension siguen independientes.
// [Guía] Fija el parámetro `M1_BRANCH_FAST_MAX_V` (M1 branch fast máximo). Su unidad es voltios.
constexpr float M1_BRANCH_FAST_MAX_V = 18.5f;
// [Guía] Fija el parámetro `M1_ACQUIRE_BUS_MAX_V` (M1 acquire bus DC máximo). Su unidad es voltios.
constexpr float M1_ACQUIRE_BUS_MAX_V = 39.0f;
// El techo39V recibido es operativo, distinto de los cortes44/45V. Ante una
// excursion retirar duty gradualmente, confirmar recuperacion y reintentar
// mas lento. No permitir barridos repetidos indefinidamente ni saltar al5%.
// [Guía] Fija el parámetro `M1_ACQUIRE_RESUME_V` (M1 acquire resume). Su unidad es voltios.
constexpr float M1_ACQUIRE_RESUME_V = 38.0f;
// [Guía] Fija el parámetro `M1_ACQUIRE_RESUME_US` (M1 acquire resume). Su unidad es microsegundos.
constexpr uint32_t M1_ACQUIRE_RESUME_US = 200000;
// [Guía] Centraliza el ajuste `M1_ACQUIRE_MAX_RETRIES` (M1 acquire máximo retries) para compartirlo entre los
// [Guía] módulos que lo usan.
constexpr uint8_t M1_ACQUIRE_MAX_RETRIES = 2;
// Permitir la carga auxiliar existente durante adquisicion PV sola. No mueve
// el techo de adquisicion ni confirma consumo fisico por recibir un ACK.
// [Guía] Centraliza el ajuste `M1_ACQUIRE_AUX_LOAD` (M1 acquire carga auxiliar load) para compartirlo entre los
// [Guía] módulos que lo usan.
constexpr bool M1_ACQUIRE_AUX_LOAD = true;
// [Guía] Fija el parámetro `M1_AUX_LOAD_RECOVERY_US` (M1 carga auxiliar load recovery). Su unidad es
// [Guía] microsegundos.
constexpr uint32_t M1_AUX_LOAD_RECOVERY_US = 200000;
// [Guía] Fija el parámetro `M1_BRANCH_CONFIRM_US` (M1 branch confirm). Su unidad es microsegundos.
constexpr uint32_t M1_BRANCH_CONFIRM_US = 200000;
// [Guía] Fija el parámetro `M1_ACQUIRE_TIMEOUT_US` (M1 acquire vencimiento). Su unidad es microsegundos.
constexpr uint32_t M1_ACQUIRE_TIMEOUT_US = 15000000;
// V40: durante ACQUIRE no seguir cargando el panel mientras se confirma su
// rama. Si el puerto comienza a hundirse, retirar Q1 antes de alcanzar bus=0.
// Son limites OPERATIVOS experimentales, no sustituyen BUS_ZERO ni sobretension.
// Fast usa el IIR de adquisicion; Raw solo interviene con un valle mas profundo.
// [Guía] Fija el parámetro `M1_INPUT_PV_MIN_V` (M1 input panel fotovoltaico mínimo). Su unidad es voltios.
constexpr float M1_INPUT_PV_MIN_V = 6.0f;
// [Guía] Fija el parámetro `M1_INPUT_PV_RAW_MIN_V` (M1 input panel fotovoltaico sin filtrar mínimo). Su unidad es
// [Guía] voltios.
constexpr float M1_INPUT_PV_RAW_MIN_V = 3.0f;
// [Guía] Fija el parámetro `M1_INPUT_BUS_MIN_V` (M1 input bus DC mínimo). Su unidad es voltios.
constexpr float M1_INPUT_BUS_MIN_V = 12.0f;
// [Guía] Fija el parámetro `M1_INPUT_BUS_RAW_MIN_V` (M1 input bus DC sin filtrar mínimo). Su unidad es voltios.
constexpr float M1_INPUT_BUS_RAW_MIN_V = 5.0f;
// Histeresis: recuperar PV/bus antes de reintentar, con duty sostenido y la
// misma confirmacion M1_ACQUIRE_RESUME_US. No se renueva el presupuesto15s.
// [Guía] Fija el parámetro `M1_INPUT_PV_RESUME_V` (M1 input panel fotovoltaico resume). Su unidad es voltios.
constexpr float M1_INPUT_PV_RESUME_V = 8.0f;
// [Guía] Fija el parámetro `M1_INPUT_BUS_RESUME_V` (M1 input bus DC resume). Su unidad es voltios.
constexpr float M1_INPUT_BUS_RESUME_V = 16.0f;
// V41: ante el primer valle NO integrar ni subir Q1. Confirmar4ms antes de
// retirar duty evita convertir un valle breve en una recuperacion completa.
// Esto NO retarda BUS_ZERO, sobretension ni sobrecorriente, evaluados antes.
// [Guía] Fija el parámetro `M1_INPUT_CONFIRM_US` (M1 input confirm). Su unidad es microsegundos.
constexpr uint32_t M1_INPUT_CONFIRM_US = 4000;
// Solo una regulacion continua recuperada habilita un NUEVO presupuesto ante
// otra perturbacion. Entrar/salir PI un instante no renueva reintentos/15s.
// [Guía] Fija el parámetro `M1_PI_STABLE_US` (M1 PI stable). Su unidad es microsegundos.
constexpr uint32_t M1_PI_STABLE_US = 1000000;
// [Guía] Fija el parámetro `M1_PI_STABLE_BAND_V` (M1 PI stable band). Su unidad es voltios.
constexpr float M1_PI_STABLE_BAND_V = 0.5f;
// [Guía] Comprueba `M1_INPUT_CONFIRM_US >= 2 * CONTROL_US` durante la compilación.
static_assert(M1_INPUT_CONFIRM_US >= 2 * CONTROL_US
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && M1_INPUT_CONFIRM_US < CONTROL_TIMEOUT_US
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && M1_PI_STABLE_US > M1_ACQUIRE_RESUME_US
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && M1_PI_STABLE_BAND_V > 0,
              "Invalid M1 guard qualification");
// [Guía] Comprueba `M1_INPUT_PV_RAW_MIN_V > 0` durante la compilación.
static_assert(M1_INPUT_PV_RAW_MIN_V > 0
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && M1_INPUT_PV_RAW_MIN_V < M1_INPUT_PV_MIN_V
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && M1_INPUT_PV_MIN_V < M1_INPUT_PV_RESUME_V
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && M1_INPUT_PV_RESUME_V < M1_LOW_BRANCH_V
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && M1_INPUT_BUS_RAW_MIN_V > 0
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && M1_INPUT_BUS_RAW_MIN_V < M1_INPUT_BUS_MIN_V
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && M1_INPUT_BUS_MIN_V < M1_INPUT_BUS_RESUME_V
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && M1_INPUT_BUS_RESUME_V < BUS_REFERENCE_V,
              "Invalid M1 input recovery hysteresis");
// [Guía] Comprueba `M1_ACQUIRE_BUS_MAX_V < BAT_TEST_BUS_TRIP_V` durante la compilación.
static_assert(M1_ACQUIRE_BUS_MAX_V < BAT_TEST_BUS_TRIP_V
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && M1_ACQUIRE_RESUME_V > BUS_REFERENCE_V
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && M1_ACQUIRE_RESUME_V < M1_ACQUIRE_BUS_MAX_V
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && M1_ACQUIRE_RESUME_US >= 3 * M1_PV_FILTER_TAU_US
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && M1_ACQUIRE_MAX_RETRIES > 0 && M1_ACQUIRE_MAX_RETRIES <= 3
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && M1_AUX_LOAD_RECOVERY_US >= 3 * EMS_BUS_FILTER_TAU_US
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && M1_REACQUIRE_V > M1_LOW_BRANCH_V
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && M1_PV_FILTER_TAU_US > SAMPLE_US
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && M1_BRANCH_CONFIRM_US >= 3 * M1_PV_FILTER_TAU_US
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && M1_ACQUIRE_TIMEOUT_US > M1_BRANCH_CONFIRM_US
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && M1_ACQUIRE_SLEW_PER_SECOND > 0
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && M1_ACQUIRE_TIMEOUT_US < 60000000, "Invalid M1 acquisition limits");
// Conservar PI de M1 solicitado: error=Vbus-referencia, sin cambio automatico
// de signo. Ese signo local NO garantiza regular en toda la curva solar.
// Limites durante regulacion. OFF es 0; las rampas de conexion son independientes.
// [Guía] Define Q1 ciclo de trabajo mínimo como fracción del periodo PWM. Multiplicar por 100 da el porcentaje.
constexpr float Q1_DUTY_MIN = 0.05f;
// [Guía] Define Q1 ciclo de trabajo máximo como fracción del periodo PWM. Multiplicar por 100 da el porcentaje.
constexpr float Q1_DUTY_MAX = 0.85f;
// [Guía] Define Q2 ciclo de trabajo mínimo como fracción del periodo PWM. Multiplicar por 100 da el porcentaje.
constexpr float Q2_DUTY_MIN = 0.05f;
// [Guía] Define Q2 ciclo de trabajo máximo como fracción del periodo PWM. Multiplicar por 100 da el porcentaje.
constexpr float Q2_DUTY_MAX = 0.95f;
// [Guía] Define Q3 ciclo de trabajo mínimo como fracción del periodo PWM. Multiplicar por 100 da el porcentaje.
constexpr float Q3_DUTY_MIN = 0.05f;
// [Guía] Define Q3 ciclo de trabajo máximo como fracción del periodo PWM. Multiplicar por 100 da el porcentaje.
constexpr float Q3_DUTY_MAX = 0.70f;
// Limites de ensayo solicitados por el usuario en V61, no una certificacion
// de rango electrico seguro. Los tres chequeos generales siguen abajo.
// Alias para el PI/MPPT existente de Q1. El perfil bateria tiene otros dos PI.
// [Guía] Define ciclo de trabajo mínimo como fracción del periodo PWM. Multiplicar por 100 da el porcentaje.
constexpr float DUTY_MIN = Q1_DUTY_MIN;
// [Guía] Define ciclo de trabajo máximo como fracción del periodo PWM. Multiplicar por 100 da el porcentaje.
constexpr float DUTY_MAX = Q1_DUTY_MAX;
// [Guía] Comprueba `Q1_DUTY_MIN > 0 && Q1_DUTY_MIN < Q1_DUTY_MAX && Q1_DUTY_MAX < 1` durante la compilación.
static_assert(Q1_DUTY_MIN > 0 && Q1_DUTY_MIN < Q1_DUTY_MAX && Q1_DUTY_MAX < 1,
              "Invalid Q1 duty limits");
// [Guía] Comprueba `Q2_DUTY_MIN > 0 && Q2_DUTY_MIN < Q2_DUTY_MAX && Q2_DUTY_MAX < 1` durante la compilación.
static_assert(Q2_DUTY_MIN > 0 && Q2_DUTY_MIN < Q2_DUTY_MAX && Q2_DUTY_MAX < 1,
              "Invalid Q2 duty limits");
// [Guía] Comprueba `Q3_DUTY_MIN > 0 && Q3_DUTY_MIN < Q3_DUTY_MAX && Q3_DUTY_MAX < 1` durante la compilación.
static_assert(Q3_DUTY_MIN > 0 && Q3_DUTY_MIN < Q3_DUTY_MAX && Q3_DUTY_MAX < 1,
              "Invalid Q3 duty limits");
// [Guía] Comprueba `Q1_TEST_SLEW_PER_SECOND > 0 && Q2_SLEW_PER_SECOND > 0` durante la compilación.
static_assert(Q1_TEST_SLEW_PER_SECOND > 0 && Q2_SLEW_PER_SECOND > 0
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && Q3_SLEW_PER_SECOND > 0, "Invalid test slew limits");
// [Guía] Comprueba `BAT_TRANSFER_TIMEOUT_US * 0.000001f` durante la compilación.
static_assert(BAT_TRANSFER_TIMEOUT_US * 0.000001f
              // [Guía] Completa la declaración o expresión con `> Q2_DUTY_MAX / Q2_SLEW_PER_SECOND +
              // [Guía] BAT_ZERO_HOLD_US * 0.000001f`.
              > Q2_DUTY_MAX / Q2_SLEW_PER_SECOND + BAT_ZERO_HOLD_US * 0.000001f,
              "Transfer timeout cannot complete Q2 retirement");
// [Guía] Fija el parámetro `SOFT_START_US` (soft start). Su unidad es microsegundos.
constexpr uint32_t SOFT_START_US = 1000000;  // Rampa de referencia de 1 s reportada por el usuario.
// [Guía] Define ciclo de trabajo pendiente PER second como fracción del periodo PWM. Multiplicar por 100 da el
// [Guía] porcentaje.
constexpr float DUTY_SLEW_PER_SECOND = 1.0f; // 100 puntos porcentuales por segundo.
// [Guía] Comprueba `SOFT_START_US > 0, );` durante la compilación.
static_assert(SOFT_START_US > 0, "Soft start duration must be positive");
// [Guía] Comprueba `DUTY_SLEW_PER_SECOND > 0, );` durante la compilación.
static_assert(DUTY_SLEW_PER_SECOND > 0, "Duty slew must be positive");

// Orden: PV, bus DC, bateria. Resistencias en ohmios.
// [Guía] Completa la declaración o expresión con `constexpr uint8_t CURRENT_PINS[] = {A0, A1, A2}`.
constexpr uint8_t CURRENT_PINS[] = {A0, A1, A2};
// [Guía] Completa la declaración o expresión con `constexpr uint8_t VOLTAGE_PINS[] = {A3, A4, A5}`.
constexpr uint8_t VOLTAGE_PINS[] = {A3, A4, A5};
// Valores actualizados por el usuario; conservar en las siguientes versiones.
// Vexterna=Vpin*(R_TOP+R_BOTTOM)/R_BOTTOM*VOLTAGE_GAIN.
// Resistencias medidas, no sustituir por valores comerciales sin recalibrar.
// [Guía] Completa la declaración o expresión con `constexpr float R_TOP[] = {65910.0f, 157048.0f, 67950.0f}`.
constexpr float R_TOP[] = {65910.0f, 157048.0f, 67950.0f};
// [Guía] Completa la declaración o expresión con `constexpr float R_BOTTOM[] = {9688.0f, 9750.0f, 9790.0f}`.
constexpr float R_BOTTOM[] = {9688.0f, 9750.0f, 9790.0f};
// La bateria se habilita automaticamente al configurar ambas resistencias > 0.
// [Guía] Completa la declaración o expresión con `constexpr float VOLTAGE_GAIN[] = {1.0f, 0.991f, 1.0f}`.
constexpr float VOLTAGE_GAIN[] = {1.0f, 0.991f, 1.0f};

// ACS712 de 30 A. Ajustar cada canal con una referencia conocida.
// Corriente=(ADC-ceroADC)*ADC_REF_V/ADC_MAX/SENSITIVITY*GAIN*SIGN,
// con la correccion de alimentacion que aplica acquire() en el sketch.
// Signo positivo Ibat=descarga; cambiar SIGN altera tambien SOC y deteccion EMS.
// [Guía] Completa la declaración o expresión con `constexpr float SENSOR_SUPPLY_V[] = {5.0f, 5.0f, 5.0f}`.
constexpr float SENSOR_SUPPLY_V[] = {5.0f, 5.0f, 5.0f};
// [Guía] Completa la declaración o expresión con `constexpr float SENSITIVITY_V_A[] = {0.066f, 0.066f, 0.066f}`.
constexpr float SENSITIVITY_V_A[] = {0.066f, 0.066f, 0.066f};
// [Guía] Completa la declaración o expresión con `constexpr float CURRENT_GAIN[] = {1.0f, 1.0f, 1.0f}`.
constexpr float CURRENT_GAIN[] = {1.0f, 1.0f, 1.0f};
// [Guía] Completa la declaración o expresión con `constexpr float CURRENT_SIGN[] = {1.0f, 1.0f, 1.0f}`.
constexpr float CURRENT_SIGN[] = {1.0f, 1.0f, 1.0f};
// El cero de cada sensor se calibra automaticamente en cada arranque.

// GP12170: capacidad nominal C20 a 25 C; no es capacidad util exacta bajo carga.
// [Guía] Fija el parámetro `BATTERY_CAPACITY_AH` (batería capacity). Su unidad es amperios-hora.
constexpr float BATTERY_CAPACITY_AH = 17.0f;
// Introducir el SOC obtenido externamente de una medida EN REPOSO (0..100).
// -1 deja la estimacion sin inicializar hasta recibir SOC=porcentaje por serial.
// Conservar el valor de la copia del usuario en esta prueba; no es SOC medido.
// [Guía] Fija el parámetro `SOC_INITIAL_PERCENT` (SOC initial). Su unidad es porcentaje.
constexpr float SOC_INITIAL_PERCENT = 50.0f;
// Ibat positivo = descarga; negativo = carga. Eficiencia inicial no calibrada.
// [Guía] Centraliza el ajuste `SOC_CHARGE_EFFICIENCY` (SOC carga efficiency) para compartirlo entre los módulos
// [Guía] que lo usan.
constexpr float SOC_CHARGE_EFFICIENCY = 1.0f;
// [Guía] Fija el parámetro `SOC_MAX_GAP_US` (SOC máximo hueco entre muestras). Su unidad es microsegundos.
constexpr uint32_t SOC_MAX_GAP_US = 20000;
// [Guía] Comprueba `BATTERY_CAPACITY_AH > 0, );` durante la compilación.
static_assert(BATTERY_CAPACITY_AH > 0, "Invalid battery capacity");
// [Guía] Comprueba `SOC_INITIAL_PERCENT == -1.0f` durante la compilación.
static_assert(SOC_INITIAL_PERCENT == -1.0f
              // [Guía] Agrupa condiciones alternativas y conjuntas en la prueba en curso.
              || (SOC_INITIAL_PERCENT >= 0 && SOC_INITIAL_PERCENT <= 100),
              "Initial SOC must be -1 or 0..100 percent");
// [Guía] Comprueba `SOC_CHARGE_EFFICIENCY > 0 && SOC_CHARGE_EFFICIENCY <= 1` durante la compilación.
static_assert(SOC_CHARGE_EFFICIENCY > 0 && SOC_CHARGE_EFFICIENCY <= 1,
              "Invalid charge efficiency");
// [Guía] Comprueba `SOC_MAX_GAP_US > SAMPLE_US, );` durante la compilación.
static_assert(SOC_MAX_GAP_US > SAMPLE_US, "SOC gap tolerance too short");

// V69: suavizado moderado solicitado para las entradas de los PI/MPPT/EMS.
// A 500us nominales, corriente llega al95% de un escalon en~18ms y tension
// en~9.2ms. Son tiempos del filtro aislado, no del convertidor completo.
// Las protecciones raw y las ventanas propias de deteccion NO usan estos alpha.
// El promedio LIVE/raw y el filtro visual Python siguen siendo independientes.
// IIR de primer orden: salida += alpha*(entrada-salida). alpha cerca de1
// responde rapido y suaviza poco; cerca de0 filtra mas y agrega retardo.
// No confundir estos alpha de adquisicion con TAU_US de supervisores EMS,
// ni con las ventanas de potencia del MPPT o los promedios de pantalla.
// [Guía] Da peso 0,08 a la nueva lectura de corriente en el IIR. La lectura filtrada previa aporta el
// [Guía] complemento.
constexpr float CURRENT_ALPHA = 0.08f;
// [Guía] Da peso 0,15 a la nueva lectura de tensión en el IIR. La lectura filtrada previa aporta el complemento.
constexpr float VOLTAGE_ALPHA = 0.15f;
// [Guía] Comprueba `CURRENT_ALPHA > 0.0f && CURRENT_ALPHA <= 1.0f` durante la compilación.
static_assert(CURRENT_ALPHA > 0.0f && CURRENT_ALPHA <= 1.0f
              // [Guía] Añade un requisito conjunto a la condición de la línea anterior.
              && VOLTAGE_ALPHA > 0.0f && VOLTAGE_ALPHA <= 1.0f,
              "Acquisition alpha must be in (0,1]");
}
