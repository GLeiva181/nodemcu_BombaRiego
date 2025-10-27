# Sistema de Riego Inteligente con ESP8266

Este proyecto convierte un NodeMCU (ESP8266) en un controlador central para un sistema de riego, una bomba de agua y un portón automático. El control se puede realizar a través de una página web local o mediante comandos de voz con Amazon Alexa.

## ✨ Características Principales

- **Control Dual:** Maneja los dispositivos desde una interfaz web intuitiva o con la comodidad de Amazon Alexa.
- **IP Estática:** El dispositivo se configura con una IP fija (`192.168.1.150`) para un acceso fácil y predecible.
- **Lógica de Riego Exclusiva:** Solo una válvula de riego puede estar activa a la vez. Al encender una, las demás se apagan automáticamente.
- **Bomba de Agua Inteligente:** La bomba de agua se activa automáticamente solo cuando una de las válvulas de riego está abierta.
- **Temporizadores de Seguridad:**
  - **Válvulas de Riego:** Se apagan automáticamente después de un tiempo configurable (por defecto **10 minutos**). El valor se guarda y persiste después de reinicios.
  - **Portón Automático:** Se apaga automáticamente después de **5 segundos**, simulando un pulso para abrir o cerrar.
- **Interfaz Web Completa:**
  - Muestra el estado (Encendido/Apagado) de todos los dispositivos en tiempo real.
  - Muestra el pin de hardware asociado a cada dispositivo.
  - Incluye un contador regresivo para las válvulas activas, mostrando el tiempo restante antes del apagado automático.
- **Integración con Alexa:** Emula dispositivos Philips Hue, permitiendo que Alexa descubra y controle las válvulas como si fueran enchufes inteligentes, sin necesidad de skills o servicios en la nube.
- **Indicadores de Estado:**
  - **Luz Indicadora (D0):** Una luz física que se enciende cuando el sistema de riego está en funcionamiento.
  - **LED Integrado:** El LED de la placa parpadea para indicar el estado de la conexión WiFi (conectando, conectado, fallo).

## ⚙️ Hardware Requerido

- NodeMCU ESP8266.
- Módulo de relés para controlar los dispositivos de 110V/220V (válvulas, bomba, portón).
- Válvulas solenoides para el riego.
- Bomba de agua.
- Motor para el portón automático.
- Fuente de alimentación adecuada para el NodeMCU y los relés.

### 📌 Pinout (Cableado)

El firmware está configurado para usar los siguientes pines del NodeMCU:

| Dispositivo         | Pin NodeMCU | Pin GPIO |
|---------------------|-------------|----------|
| V1-Riego Este       | D1          | 5        |
| V2-Riego Oeste      | D2          | 4        |
| V3-Riego Frente     | D7          | 13       |
| Bomba de Agua       | D5          | 14       |
| Portón Automático   | D6          | 12       |
| Luz Indicadora      | D0          | 16       |
| LED Estado WiFi     | LED_BUILTIN | 2        |

## 📚 Software y Librerías

1.  **Arduino IDE:** Para programar el ESP8266.
2.  **ESP8266 Core for Arduino:** El paquete de placas para ESP8266.
3.  **Librerías de Arduino:**
    - `ESP8266WiFi` (viene con el core)
    - `ESP8266WebServer` (viene con el core)
    - `fauxmoESP`: Se debe instalar desde el **Gestor de Librerías** del Arduino IDE. Es crucial para la integración con Alexa.

## 🔧 Configuración

Antes de subir el código a tu NodeMCU, necesitas configurar algunas variables en el archivo `riego.ino`:

1.  **Credenciales WiFi:**
    ```cpp
    const char* ssid = "TU_SSID";
    const char* password = "TU_PASSWORD";
    ```

2.  **Configuración de Red (Opcional):**
    El sistema usa una IP estática. Si tu red tiene una configuración diferente (ej. `192.168.0.x`), ajusta estos valores. La `gateway` suele ser la IP de tu router.
    ```cpp
    IPAddress staticIP(192, 168, 1, 150);
    IPAddress gateway(192, 168, 1, 1);
    IPAddress subnet(255, 255, 255, 0);
    ```

## 🚀 Instalación y Uso

1.  **Conecta el Hardware:** Realiza el cableado de los relés a los pines del NodeMCU como se describe en la sección de **Pinout**.
2.  **Prepara el IDE:** Instala el Arduino IDE, el core de ESP8266 y la librería `fauxmoESP`.
3.  **Configura el Código:** Modifica las credenciales WiFi y la configuración de red en `riego.ino`.
4.  **Sube el Código:** Conecta tu NodeMCU al ordenador y sube el sketch.
5.  **Abre el Monitor Serie** para ver el proceso de conexión y la IP asignada.

### Uso de la Interfaz Web

- Abre un navegador en un dispositivo conectado a la misma red WiFi.
- Ve a la dirección **`http://192.168.1.150`**.
- Verás el panel de control donde puedes conmutar el estado de las válvulas y activar el portón.

### Uso con Amazon Alexa

1.  Asegúrate de que el NodeMCU esté encendido y conectado a la red.
2.  Abre tu aplicación de Amazon Alexa o di a un dispositivo Echo:
    > "Alexa, descubre mis dispositivos"
3.  Alexa buscará en la red y encontrará los nuevos dispositivos ("V1-Riego Este", "V2-Riego Oeste", "V3-Riego Frente").
4.  Ahora puedes usar comandos de voz como:
    - > "Alexa, enciende V1-Riego Este"
    - > "Alexa, apaga V2-Riego Oeste"

##  dissection Código

- **`setup()`**: Inicializa los pines, la conexión WiFi, el servidor web y la emulación de dispositivos para Alexa.
- **`loop()`**: El corazón del programa. Atiende constantemente las peticiones del servidor web (`server.handleClient()`), los comandos de Alexa (`fauxmo.handle()`) y verifica los temporizadores de apagado automático (`checkValveTimers()`, `checkGateTimer()`).
- **`handleRoot()`**: Genera y envía el código HTML de la página web al cliente.
- **`handleToggle()`**: Procesa las acciones de los botones de la página web.
- **`updatePumpAndLightState()`**: Contiene la lógica para encender/apagar la bomba y la luz indicadora basándose en el estado de las válvulas.
- **`setupAlexa()`**: Define los nombres de los dispositivos que Alexa descubrirá y la función que se ejecutará cuando se reciba un comando de voz.

---