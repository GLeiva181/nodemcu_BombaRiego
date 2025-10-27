#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <fauxmoESP.h>
#include <EEPROM.h>
#include <WiFiUdp.h>
#include <NTPClient.h>

// =============================================================================
// --- CONFIGURACIÓN DE PINES ---
// =============================================================================
static const uint8_t D0   = 16;
static const uint8_t D1   = 5;
static const uint8_t D2   = 4;
static const uint8_t D3   = 0;  // No se usa D3 (GPIO0) porque si está a nivel bajo durante el arranque, el ESP8266 entra en modo de flasheo.
static const uint8_t D4   = 2;  // D4 (GPIO2) es el LED integrado, usado para indicar el estado de la conexión WiFi.
static const uint8_t D5   = 14;
static const uint8_t D6   = 12;
static const uint8_t D7   = 13;
static const uint8_t D8   = 15;
static const uint8_t D9   = 3;
static const uint8_t D10  = 1;

// --- Asignación de dispositivos a pines ---
const int valvePins[] = {D0, D1, D2, D5};
const char* valveNames[] = {"V1-Riego Frente", "V2-Riego Este", "V3-Riego Oeste", "V4-Riego Nuevo"};
const int waterPumpPin = D6;
const int gatePin = D7;
const int numValves = sizeof(valvePins) / sizeof(int);

// =============================================================================
// --- CONFIGURACIÓN DE RED Y HORA ---
// =============================================================================
const char* ssid = "MovistarFibra-87E920";
const char* password = "GY5ft8GXDQ7ap4DSDPS9";
IPAddress staticIP(192, 168, 1, 151);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

// --- Cliente NTP ---
WiFiUDP ntpUDP;
// GMT-3: -3 * 3600 = -10800. El último parámetro es el intervalo de actualización en ms.
NTPClient timeClient(ntpUDP, "pool.ntp.org", -10800, 60000);

// =============================================================================
// --- ESTRUCTURAS DE DATOS Y VARIABLES GLOBALES ---
// =============================================================================
ESP8266WebServer server(80);
fauxmoESP fauxmo;

struct ValveSchedule {
  bool enabled;
  uint8_t daysOfWeek; // Bitmask: D L M M J V S -> 1 2 4 8 16 32 64
  uint8_t startHour;
  uint8_t startMinute;
  uint16_t durationMinutes;
  bool triggeredToday;
};

ValveSchedule schedules[numValves];
bool valveStates[numValves] = {false};
unsigned long valveOnTime[numValves] = {0};
bool waterPumpState = false;
bool gateState = false;
unsigned long gateOnTime = 0;

unsigned long valveAutoOffTimeout = 10 * 60 * 1000;
unsigned long gateAutoOffTimeout = 5 * 1000;

// =============================================================================
// --- LÓGICA DE CONTROL DE DISPOSITIVOS ---
// =============================================================================

void updatePumpState() {
  bool anyValveOn = false;
  for (int i = 0; i < numValves; i++) {
    if (valveStates[i]) {
      anyValveOn = true;
      break;
    }
  }
  waterPumpState = anyValveOn;
  digitalWrite(waterPumpPin, waterPumpState ? LOW : HIGH);
}

void turnValveOn(int valveId, int durationMinutes) {
    for (int i = 0; i < numValves; i++) {
        valveStates[i] = false;
        digitalWrite(valvePins[i], HIGH);
        valveOnTime[i] = 0;
    }
    valveStates[valveId] = true;
    digitalWrite(valvePins[valveId], LOW);
    valveOnTime[valveId] = millis();
    Serial.println("Valvula '" + String(valveNames[valveId]) + "' activada. Duracion: " + String(durationMinutes) + " min.");
    updatePumpState();
}

void checkValveTimers() {
  unsigned long currentMillis = millis();
  for (int i = 0; i < numValves; i++) {
    if (valveStates[i] && valveOnTime[i] > 0) {
      unsigned long timeout = (schedules[i].enabled && schedules[i].triggeredToday) ? (unsigned long)schedules[i].durationMinutes * 60 * 1000 : valveAutoOffTimeout;
      if (currentMillis - valveOnTime[i] >= timeout) {
        Serial.println("Auto-apagado: Valvula '" + String(valveNames[i]) + "'.");
        valveStates[i] = false;
        digitalWrite(valvePins[i], HIGH);
        valveOnTime[i] = 0;
        schedules[i].triggeredToday = false;
        updatePumpState();
      }
    }
  }
}

void checkGateTimer() {
  if (gateState && gateOnTime > 0) {
    if (millis() - gateOnTime >= gateAutoOffTimeout) {
      gateState = false;
      digitalWrite(gatePin, HIGH);
      gateOnTime = 0;
    }
  }
}

// =============================================================================
// --- LÓGICA DE PROGRAMACIÓN Y HORA ---
// =============================================================================

void checkSchedules() {
  if (!timeClient.isTimeSet()) return; // No hacer nada si la hora no es válida

  int currentHour = timeClient.getHours();
  int currentMinute = timeClient.getMinutes();
  int currentDay = timeClient.getDay(); // Domingo=0, Lunes=1, ..., Sábado=6

  if (currentHour == 0 && currentMinute == 0) {
    for (int i = 0; i < numValves; i++) {
      if (schedules[i].triggeredToday) {
        schedules[i].triggeredToday = false;
      }
    }
  }

  for (int i = 0; i < numValves; i++) {
    if (schedules[i].enabled && !schedules[i].triggeredToday) {
      int dayBit = 1 << currentDay;
      if ((schedules[i].daysOfWeek & dayBit)) {
        if (currentHour == schedules[i].startHour && currentMinute == schedules[i].startMinute) {
          schedules[i].triggeredToday = true;
          turnValveOn(i, schedules[i].durationMinutes);
        }
      }
    }
  }
}

// =============================================================================
// --- SERVIDOR WEB: UI Y MANEJADORES ---
// =============================================================================

String getPinName(int pin) {
  switch(pin) {
    case 16: return "D0"; case 5: return "D1"; case 4: return "D2"; case 0: return "D3";
    case 2: return "D4"; case 14: return "D5"; case 12: return "D6"; case 13: return "D7";
    case 15: return "D8"; case 3: return "D9"; case 1: return "D10";
    default: return "GPIO" + String(pin);
  }
}

void handleRoot() {
  String html = "<html><head><title>Panel de Control Riego</title><style>" 
    "body{font-family:Arial,sans-serif;background-color:#f4f4f4;margin:20px}h1,h2{color:#333}table{width:100%;max-width:800px;border-collapse:collapse;margin-bottom:20px;box-shadow:0 2px 3px rgba(0,0,0,.1)}th,td{padding:12px;border:1px solid #ddd;text-align:center}th{background-color:#007bff;color:#fff}tr:nth-child(even){background-color:#f2f2f2}.state{font-weight:700}.on{color:#28a745}.off{color:#dc3545}a.button,input[type=submit]{display:inline-block;padding:8px 16px;font-size:14px;font-weight:700;color:#fff;background-color:#007bff;border-radius:5px;text-decoration:none;border:none;cursor:pointer}a.button:hover,input[type=submit]:hover{background-color:#0056b3}form{margin-bottom:20px;background-color:#fff;padding:15px;border-radius:5px;box-shadow:0 2px 3px rgba(0,0,0,.1)}.day-selector label{margin-right:10px}" 
    "</style></head><body><h1>Panel de Control</h1><h3>IP: " + WiFi.localIP().toString() + "</h3><h3>Hora Actual: " + timeClient.getFormattedTime() + "</h3>";

  html += "<h2>Valvulas</h2><table><tr><th>Dispositivo</th><th>Pin</th><th>Estado</th><th>Accion</th></tr>";
  for (int i = 0; i < numValves; i++) {
    html += "<tr><td>" + String(valveNames[i]) + "</td><td>" + getPinName(valvePins[i]) + "</td>";
    html += String("<td class='state ") + (valveStates[i] ? "on" : "off") + ">" + (valveStates[i] ? "ENCENDIDO" : "APAGADO") + "</td>";
    html += "<td><a class='button' href='/toggle?device=valve&id=" + String(i) + "'>Conmutar</a></td></tr>";
  }
  html += "</table>";

  html += "<h2>Estado del Sistema</h2><table><tr><th>Dispositivo</th><th>Pin</th><th>Estado</th><th>Accion</th></tr>";
  html += String("<tr><td>Bomba de agua</td><td>") + getPinName(waterPumpPin) + "</td><td class='state " + (waterPumpState ? "on" : "off") + ">" + (waterPumpState ? "ENCENDIDO" : "APAGADO") + "</td><td>-</td></tr>";
  html += String("<tr><td>Porton Automatico</td><td>") + getPinName(gatePin) + "</td><td class='state " + (gateState ? "on" : "off") + ">" + (gateState ? "ENCENDIDO" : "APAGADO") + "</td><td><a class='button' href='/toggle?device=gate&id=0'>Activar</a></td></tr>";
  html += "</table>";

  html += "<h2>Configuracion General</h2><form action='/set-timeouts' method='POST'>";
  html += "<label>Apagado automatico de valvulas (minutos): </label><input type='number' name='valve_timeout' min='1' value='" + String(valveAutoOffTimeout / 60000) + "'><br><br>";
  html += "<label>Pulso del porton (segundos): </label><input type='number' name='gate_timeout' min='1' value='" + String(gateAutoOffTimeout / 1000) + "'><br><br>";
  html += "<input type='submit' value='Guardar Tiempos'></form>";

  html += "<h2>Programacion Automatica de Valvulas</h2>";
  const char* days[] = {"D", "L", "M", "X", "J", "V", "S"};
  for (int i = 0; i < numValves; i++) {
    html += "<form action='/set-schedule' method='POST'><h4>" + String(valveNames[i]) + "</h4>";
    html += "<input type='hidden' name='id' value='" + String(i) + "'>";
    html += "<label><input type='checkbox' name='enabled' ";
    if (schedules[i].enabled) html += "checked";
    html += "> Habilitar</label><br><br>";
    html += "<div class='day-selector'>Días: ";
    for (int d = 0; d < 7; d++) {
      html += "<label><input type='checkbox' name='day" + String(d) + "' ";
      if (schedules[i].daysOfWeek & (1 << d)) html += "checked";
      html += ">" + String(days[d]) + "</label>";
    }
    html += "</div><br>";
    char timeBuf[6];
    sprintf(timeBuf, "%02d:%02d", schedules[i].startHour, schedules[i].startMinute);
    html += "<label>Hora de inicio: </label><input type='time' name='starttime' value='" + String(timeBuf) + "'><br><br>";
    html += "<label>Duracion (minutos): </label><input type='number' name='duration' min='1' value='" + String(schedules[i].durationMinutes) + "'><br><br>";
    html += "<input type='submit' value='Guardar Horario'></form><hr>";
  }

  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleSetTimeouts() {
  if (server.hasArg("valve_timeout")) valveAutoOffTimeout = server.arg("valve_timeout").toInt() * 60000UL;
  if (server.hasArg("gate_timeout")) gateAutoOffTimeout = server.arg("gate_timeout").toInt() * 1000UL;
  EEPROM.put(0, valveAutoOffTimeout);
  EEPROM.put(sizeof(valveAutoOffTimeout), gateAutoOffTimeout);
  EEPROM.commit();
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleSetSchedule() {
  if (server.method() != HTTP_POST) return;
  int id = server.arg("id").toInt();
  if (id >= 0 && id < numValves) {
    schedules[id].enabled = server.hasArg("enabled");
    schedules[id].daysOfWeek = 0;
    for (int d = 0; d < 7; d++) {
      if (server.hasArg("day" + String(d))) schedules[id].daysOfWeek |= (1 << d);
    }
    String startTime = server.arg("starttime");
    schedules[id].startHour = startTime.substring(0, 2).toInt();
    schedules[id].startMinute = startTime.substring(3).toInt();
    schedules[id].durationMinutes = server.arg("duration").toInt();
    int baseAddr = sizeof(valveAutoOffTimeout) + sizeof(gateAutoOffTimeout);
    EEPROM.put(baseAddr + (id * sizeof(ValveSchedule)), schedules[id]);
    EEPROM.commit();
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleToggle() {
  String device = server.arg("device");
  int id = server.arg("id").toInt();
  if (device == "valve" && id >= 0 && id < numValves) {
    bool turnOn = !valveStates[id];
    for (int i = 0; i < numValves; i++) {
      valveStates[i] = false;
      digitalWrite(valvePins[i], HIGH);
      valveOnTime[i] = 0;
    }
    if (turnOn) {
      valveStates[id] = true;
      digitalWrite(valvePins[id], LOW);
      valveOnTime[id] = millis();
    }
    updatePumpState();
  } else if (device == "gate") {
    gateState = true;
    digitalWrite(gatePin, LOW);
    gateOnTime = millis();
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

// =============================================================================
// --- SETUP Y LOOP PRINCIPAL ---
// =============================================================================

void setup() {
  Serial.begin(115200);
  delay(10);

  int scheduleSize = sizeof(ValveSchedule) * numValves;
  int eepromSize = sizeof(valveAutoOffTimeout) + sizeof(gateAutoOffTimeout) + scheduleSize;
  EEPROM.begin(eepromSize);
  EEPROM.get(0, valveAutoOffTimeout);
  EEPROM.get(sizeof(valveAutoOffTimeout), gateAutoOffTimeout);
  int baseAddr = sizeof(valveAutoOffTimeout) + sizeof(gateAutoOffTimeout);
  for (int i = 0; i < numValves; i++) {
    EEPROM.get(baseAddr + (i * sizeof(ValveSchedule)), schedules[i]);
    schedules[i].triggeredToday = false;
  }

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  pinMode(waterPumpPin, OUTPUT);
  digitalWrite(waterPumpPin, HIGH);
  pinMode(gatePin, OUTPUT);
  digitalWrite(gatePin, HIGH);
  for (int i = 0; i < numValves; i++) {
    pinMode(valvePins[i], OUTPUT);
    digitalWrite(valvePins[i], HIGH);
  }

  Serial.println("\nConectando a " + String(ssid));
  WiFi.config(staticIP, gateway, subnet);
  WiFi.begin(ssid, password);
  Serial.print("Esperando conexión WiFi...");
  unsigned long wifiConnectStartTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiConnectStartTime < 30000) {
      delay(500);
      Serial.print(".");
  }
  Serial.println("");

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi conectado!");
    Serial.print("Dirección IP: ");
    Serial.println(WiFi.localIP());
    timeClient.begin();
    timeClient.update();
    Serial.println("Cliente NTP iniciado. Hora actual: " + timeClient.getFormattedTime());
  } else {
    Serial.println("Error: No se pudo conectar a WiFi. La programación automática no funcionará.");
  }

  server.on("/", handleRoot);
  server.on("/toggle", handleToggle);
  server.on("/set-timeouts", HTTP_POST, handleSetTimeouts);
  server.on("/set-schedule", HTTP_POST, handleSetSchedule);
  
  fauxmo.enable(true);
  for (int i = 0; i < numValves; i++) {
    fauxmo.addDevice(valveNames[i]);
  }
  fauxmo.onSetState([](unsigned char device_id, const char * device_name, bool state, unsigned char value) {
    if (device_id < numValves) {
      turnValveOn(device_id, valveAutoOffTimeout / 60000);
    }
  });

  server.begin();
  Serial.println("Servidor HTTP iniciado");
}

void loop() {
  server.handleClient();
  fauxmo.handle();
  timeClient.update();
  checkSchedules();
  checkValveTimers();
  checkGateTimer();
}