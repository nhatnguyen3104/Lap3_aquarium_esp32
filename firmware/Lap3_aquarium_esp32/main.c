#include <WiFi.h>
#include <PubSubClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ArduinoJson.h>

// =================================================
// 1. WIFI + MQTT (EMQX PUBLIC BROKER)
// =================================================
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";

const char* MQTT_SERVER = "broker.emqx.io";
const int MQTT_PORT = 1883;

// Topic telemetry: ESP32 -> Website
const char* MQTT_TOPIC_SENSOR =
    "lab3/de4/aquarium_esp32/sensor";

// Topic control: Website -> ESP32
const char* MQTT_TOPIC_CONTROL =
    "lab3/de4/aquarium_esp32/control";

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// =================================================
// 2. GPIO
// =================================================
#define TRIG_PIN 5
#define ECHO_PIN 18
#define ONE_WIRE_PIN 4
#define PUMP_PIN 25
#define VALVE_PIN 26

#define TANK_HEIGHT_CM 200.0f

// =================================================
// 3. SENSOR
// =================================================
OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature sensors(&oneWire);

// =================================================
// 4. DATA STRUCTURES + QUEUES
// =================================================
struct SensorData {
  float distanceCm;
  float waterLevelM;
  float temperature;
  bool distanceValid;
  bool temperatureValid;
};

enum OperatingMode : uint8_t {
  MODE_AUTO = 0,
  MODE_MANUAL = 1
};

enum CommandType : uint8_t {
  CMD_SET_MODE,
  CMD_PUMP,
  CMD_VALVE
};

struct ControlCommand {
  CommandType type;
  OperatingMode mode;
  bool state;
};

QueueHandle_t dataQueue = nullptr;
QueueHandle_t dataAlert = nullptr;
QueueHandle_t controlQueue = nullptr;

// These are written by Task_Alert and read by Task_SendData.
volatile bool pumpIsOn = false;
volatile bool valveIsOpen = false;
volatile OperatingMode operatingMode = MODE_AUTO;

// =================================================
// 5. ACTUATOR HELPERS
// GPIO actuator changes are performed by Task_Alert.
// =================================================
void stopActuators() {
  analogWrite(PUMP_PIN, 0);
  digitalWrite(VALVE_PIN, LOW);
  pumpIsOn = false;
  valveIsOpen = false;
}

void setPump(bool on) {
  if (on) {
    // Interlock: pump and drain valve must not run together.
    digitalWrite(VALVE_PIN, LOW);
    valveIsOpen = false;
    analogWrite(PUMP_PIN, 255);
    pumpIsOn = true;
  } else {
    analogWrite(PUMP_PIN, 0);
    pumpIsOn = false;
  }
}

void setValve(bool open) {
  if (open) {
    // Interlock: stop pump before opening the drain valve.
    analogWrite(PUMP_PIN, 0);
    pumpIsOn = false;
    digitalWrite(VALVE_PIN, HIGH);
    valveIsOpen = true;
  } else {
    digitalWrite(VALVE_PIN, LOW);
    valveIsOpen = false;
  }
}

// =================================================
// 6. HC-SR04
// =================================================
float readDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  unsigned long duration = pulseIn(ECHO_PIN, HIGH, 30000UL);
  if (duration == 0) return -1.0f;

  return duration * 0.0343f / 2.0f;
}

// =================================================
// 7. WIFI
// =================================================
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  Serial.println("Connecting to WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    vTaskDelay(pdMS_TO_TICKS(500));
  }

  Serial.println("\nWiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

// =================================================
// 8. MQTT CALLBACK: WEBSITE -> ESP32
// Callback only parses the message and puts a command
// into controlQueue. GPIO is controlled by Task_Alert.
// =================================================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (strcmp(topic, MQTT_TOPIC_CONTROL) != 0) return;

  char message[256];
  if (length >= sizeof(message)) {
    Serial.println("Control message too long - ignored");
    return;
  }

  memcpy(message, payload, length);
  message[length] = '\0';

  Serial.print("MQTT CONTROL RECEIVED: ");
  Serial.println(message);

  StaticJsonDocument<192> doc;
  DeserializationError error = deserializeJson(doc, message);

  if (error) {
    Serial.print("Invalid control JSON: ");
    Serial.println(error.c_str());
    return;
  }

  ControlCommand command{};

  // Example: {"mode":"MANUAL"} or {"mode":"AUTO"}
  if (doc.containsKey("mode")) {
    const char* mode = doc["mode"];
    if (mode == nullptr) return;

    if (strcmp(mode, "AUTO") == 0) {
      command.type = CMD_SET_MODE;
      command.mode = MODE_AUTO;
    } else if (strcmp(mode, "MANUAL") == 0) {
      command.type = CMD_SET_MODE;
      command.mode = MODE_MANUAL;
    } else {
      Serial.println("Unknown mode - use AUTO or MANUAL");
      return;
    }
  }
  // Examples:
  // {"device":"pump","state":"ON"}
  // {"device":"valve","state":"OPEN"}
  else if (doc.containsKey("device") && doc.containsKey("state")) {
    const char* device = doc["device"];
    const char* state = doc["state"];
    if (device == nullptr || state == nullptr) return;

    if (strcmp(device, "pump") == 0) {
      command.type = CMD_PUMP;
      if (strcmp(state, "ON") == 0 || strcmp(state, "true") == 0) {
        command.state = true;
      } else if (strcmp(state, "OFF") == 0 || strcmp(state, "false") == 0) {
        command.state = false;
      } else {
        Serial.println("Pump state must be ON or OFF");
        return;
      }
    } else if (strcmp(device, "valve") == 0) {
      command.type = CMD_VALVE;
      if (strcmp(state, "OPEN") == 0 || strcmp(state, "ON") == 0 ||
          strcmp(state, "true") == 0) {
        command.state = true;
      } else if (strcmp(state, "CLOSE") == 0 || strcmp(state, "OFF") == 0 ||
                 strcmp(state, "false") == 0) {
        command.state = false;
      } else {
        Serial.println("Valve state must be OPEN or CLOSE");
        return;
      }
    } else {
      Serial.println("Unknown device - use pump or valve");
      return;
    }
  } else {
    Serial.println("Unsupported control message format");
    return;
  }

  if (controlQueue != nullptr) {
    // Queue length is 10. If full, report that the command was not queued.
    if (xQueueSend(controlQueue, &command, 0) != pdTRUE) {
      Serial.println("Control queue full - command dropped");
    }
  }
}

// =================================================
// 9. MQTT CONNECT + SUBSCRIBE
// =================================================
void connectMQTT() {
  while (!mqttClient.connected()) {
    Serial.println("Connecting to EMQX...");

    uint64_t chipID = ESP.getEfuseMac();
    char clientID[40];
    snprintf(clientID, sizeof(clientID), "AquariumESP32_%04X",
             (unsigned int)(chipID & 0xFFFF));

    if (mqttClient.connect(clientID)) {
      Serial.println("EMQX connected!");

      if (mqttClient.subscribe(MQTT_TOPIC_CONTROL, 0)) {
        Serial.print("Subscribed to control topic: ");
        Serial.println(MQTT_TOPIC_CONTROL);
      } else {
        Serial.println("Subscribe control topic failed");
      }
    } else {
      Serial.print("MQTT connection failed, state = ");
      Serial.println(mqttClient.state());
      vTaskDelay(pdMS_TO_TICKS(2000));
    }
  }
}

// =================================================
// 10. TASK: READ SENSORS
// =================================================
void Task_ReadData(void *pvParameters) {
  SensorData data{};

  while (true) {
    data.distanceCm = readDistance();
    data.distanceValid =
        (data.distanceCm >= 2.0f && data.distanceCm <= 400.0f);

    if (data.distanceValid) {
      float waterLevelCm = TANK_HEIGHT_CM - data.distanceCm;
      waterLevelCm = constrain(waterLevelCm, 0.0f, TANK_HEIGHT_CM);
      data.waterLevelM = waterLevelCm / 100.0f;
    } else {
      data.waterLevelM = 0.0f;
    }

    sensors.requestTemperatures();
    data.temperature = sensors.getTempCByIndex(0);
    data.temperatureValid =
        (data.temperature != DEVICE_DISCONNECTED_C &&
         data.temperature > -55.0f &&
         data.temperature < 125.0f);

    Serial.println("\n===== SENSOR DATA =====");
    if (data.distanceValid) {
      Serial.print("Distance: ");
      Serial.print(data.distanceCm);
      Serial.println(" cm");
      Serial.print("Water level: ");
      Serial.print(data.waterLevelM);
      Serial.println(" m");
    } else {
      Serial.println("HC-SR04 ERROR");
    }

    if (data.temperatureValid) {
      Serial.print("Temperature: ");
      Serial.print(data.temperature);
      Serial.println(" C");
    } else {
      Serial.println("DS18B20 ERROR");
    }

    if (dataQueue != nullptr) {
      if (xQueueSend(dataQueue, &data, 0) != pdTRUE) {
        SensorData oldData;
        xQueueReceive(dataQueue, &oldData, 0);
        xQueueSend(dataQueue, &data, 0);
      }
    }

    if (data.distanceValid && dataAlert != nullptr) {
      xQueueOverwrite(dataAlert, &data.waterLevelM);
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

// =================================================
// 11. TASK: MQTT SEND + RECEIVE LOOP
// =================================================
void Task_SendData(void *pvParameters) {
  SensorData data{};

  while (true) {
    if (WiFi.status() != WL_CONNECTED) {
      connectWiFi();
    }

    if (!mqttClient.connected()) {
      connectMQTT();
    }

    // This services incoming MQTT messages and invokes mqttCallback().
    mqttClient.loop();

    if (xQueueReceive(dataQueue, &data, pdMS_TO_TICKS(100)) == pdTRUE) {
      if (data.distanceValid && data.temperatureValid &&
          mqttClient.connected()) {
        char payload[256];

        snprintf(
          payload,
          sizeof(payload),
          "{\"distance\":%.2f,"
          "\"waterLevel\":%.2f,"
          "\"temperature\":%.2f,"
          "\"pump_state\":\"%s\","
          "\"valve_state\":\"%s\","
          "\"mode\":\"%s\"}",
          data.distanceCm,
          data.waterLevelM,
          data.temperature,
          pumpIsOn ? "ON" : "OFF",
          valveIsOpen ? "OPEN" : "CLOSED",
          operatingMode == MODE_AUTO ? "AUTO" : "MANUAL"
        );

        bool success = mqttClient.publish(MQTT_TOPIC_SENSOR, payload);
        if (success) {
          Serial.println("\nMQTT PUBLISH SUCCESS!");
          Serial.print("Topic: ");
          Serial.println(MQTT_TOPIC_SENSOR);
          Serial.print("Payload: ");
          Serial.println(payload);
        } else {
          Serial.println("MQTT PUBLISH FAILED!");
        }
      }
    }

    // Keep mqttClient.loop() running frequently.
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// =================================================
// 12. TASK: AUTO/MANUAL CONTROL
// =================================================
enum AlertAction {
  ACT_NONE,
  ACT_PUMP_50,
  ACT_PUMP_100,
  ACT_VALVE
};

void Task_Alert(void *pvParameters) {
  float waterLevel = 0.0f;
  bool haveWaterLevel = false;

  AlertAction currentAction = ACT_NONE;
  AlertAction latchedAction = ACT_NONE;
  unsigned long actionStart = 0;
  unsigned long actionDuration = 0;

  stopActuators();

  while (true) {
    // Process all pending website commands.
    ControlCommand command;
    while (xQueueReceive(controlQueue, &command, 0) == pdTRUE) {
      if (command.type == CMD_SET_MODE) {
        operatingMode = command.mode;
        stopActuators();
        currentAction = ACT_NONE;
        latchedAction = ACT_NONE;

        Serial.print("Operating mode changed to: ");
        Serial.println(operatingMode == MODE_AUTO ? "AUTO" : "MANUAL");
      }
      else if (command.type == CMD_PUMP || command.type == CMD_VALVE) {
        if (operatingMode != MODE_MANUAL) {
          Serial.println("Manual actuator command ignored: switch to MANUAL first");
        } else {
          // Cancel any previous timed automatic action.
          currentAction = ACT_NONE;
          latchedAction = ACT_NONE;

          if (command.type == CMD_PUMP) {
            setPump(command.state);
            Serial.println(command.state ? "MANUAL: PUMP ON" : "MANUAL: PUMP OFF");
          } else {
            setValve(command.state);
            Serial.println(command.state ? "MANUAL: VALVE OPEN" : "MANUAL: VALVE CLOSED");
          }
        }
      }
    }

    // Read latest water level for AUTO mode.
    if (xQueueReceive(dataAlert, &waterLevel, pdMS_TO_TICKS(20)) == pdTRUE) {
      haveWaterLevel = true;
    }

    if (operatingMode == MODE_AUTO && haveWaterLevel) {
      AlertAction requestedAction = ACT_NONE;

      // Keep the original thresholds from your assignment.
      if (waterLevel >= 1.0f && waterLevel < 1.5f) {
        requestedAction = ACT_PUMP_50;
      }
      else if (waterLevel >= 1.5f && waterLevel <= 2.0f) {
        requestedAction = ACT_PUMP_100;
      }
      else if (waterLevel < 0.5f) {
        requestedAction = ACT_VALVE;
      }

      if (requestedAction == ACT_NONE) {
        latchedAction = ACT_NONE;
        if (currentAction != ACT_NONE) {
          stopActuators();
          currentAction = ACT_NONE;
          Serial.println("AUTO: actuator stopped (normal water level)");
        }
      }
      else if (requestedAction != latchedAction) {
        stopActuators();
        currentAction = requestedAction;
        latchedAction = requestedAction;
        actionStart = millis();

        if (currentAction == ACT_PUMP_50) {
          digitalWrite(VALVE_PIN, LOW);
          valveIsOpen = false;
          analogWrite(PUMP_PIN, 128);
          pumpIsOn = true;
          actionDuration = 10000UL;
          Serial.println("AUTO: PUMP 50% - 10 seconds");
        }
        else if (currentAction == ACT_PUMP_100) {
          analogWrite(PUMP_PIN, 255);
          pumpIsOn = true;
          actionDuration = 20000UL;
          Serial.println("AUTO: PUMP 100% - 20 seconds");
        }
        else if (currentAction == ACT_VALVE) {
          analogWrite(PUMP_PIN, 0);
          pumpIsOn = false;
          digitalWrite(VALVE_PIN, HIGH);
          valveIsOpen = true;
          actionDuration = 10000UL;
          Serial.println("AUTO: VALVE OPEN - 10 seconds");
        }
      }
    }

    // Stop a timed AUTO action when its duration has elapsed.
    if (operatingMode == MODE_AUTO &&
        currentAction != ACT_NONE &&
        millis() - actionStart >= actionDuration) {
      stopActuators();
      currentAction = ACT_NONE;
      Serial.println("AUTO: action completed");
      // latchedAction remains set until the water level returns to
      // the normal range, preventing repeated triggering every cycle.
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// =================================================
// 13. SETUP
// =================================================
void setup() {
  Serial.begin(115200);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(PUMP_PIN, OUTPUT);
  pinMode(VALVE_PIN, OUTPUT);
  stopActuators();

  sensors.begin();
  sensors.setResolution(9);

  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setKeepAlive(30);
  mqttClient.setBufferSize(256);

  dataQueue = xQueueCreate(10, sizeof(SensorData));
  dataAlert = xQueueCreate(1, sizeof(float));
  controlQueue = xQueueCreate(10, sizeof(ControlCommand));

  if (dataQueue == nullptr || dataAlert == nullptr ||
      controlQueue == nullptr) {
    Serial.println("Queue creation failed!");
    while (true) delay(1000);
  }

  Serial.println("FreeRTOS starting...");

  xTaskCreatePinnedToCore(
    Task_SendData, "Task_SendData", 8192, nullptr, 2, nullptr, 0
  );

  xTaskCreatePinnedToCore(
    Task_ReadData, "Task_ReadData", 4096, nullptr, 2, nullptr, 1
  );

  xTaskCreatePinnedToCore(
    Task_Alert, "Task_Alert", 4096, nullptr, 1, nullptr, 1
  );

  Serial.println("All tasks created!");
  Serial.println("Default mode: AUTO");
}

// =================================================
// 14. LOOP
// =================================================
void loop() {
  vTaskDelay(portMAX_DELAY);
}
