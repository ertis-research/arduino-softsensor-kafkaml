// MKR Communication Library
#include <MKRNB.h>
#include <MQTT.h>

// MKR Sensors Library
#include <OneWire.h>
#include <DallasTemperature.h>
#include <MKR_DFRobot_EC.h>
#include <MKR_DFRobot_PH.h>

#include "ArduinoLowPower.h"

// This library includes everything that should be kept secret. E.g. passwords, keys, tokens, URLs, etc.
#include "secrets.h"

// MKR Data Pins
#define EC_PIN A2  // Analog pin for EC sensor
#define PH_PIN A4  // Analog pin for pH sensor
#define TP_PIN A6  // Analog pin for temperature sensor

// Instances of OneWire and DallasTemperature classes for temperature sensor
OneWire oneWireTemperature(TP_PIN);
DallasTemperature temperatureSensor(&oneWireTemperature);

// Instances for pH and EC sensors
MKR_DFRobot_PH ph;
MKR_DFRobot_EC ec;

// MQTT server details
const char mqtt_server[] = MQTT_SERVER;     // Add your MQTT server address
const int mqtt_port = MQTT_PORT;            // Add your MQTT server port
const char mqtt_user[] = MQTT_USER;         // Add your MQTT username
const char mqtt_password[] = MQTT_PASSWORD; // Add your MQTT password

// SIM and network details
const char sim_pin[] = SECRET_SIM_PINNUMBER;
const char sim_apn[] = SECRET_SIM_APN;

// Sleep duration between measurements
const int sleep_seconds = 10;

// Enable or disable serial debugging
boolean SERIAL_DEBUG = true;

// Initialize the library instances for NB-IoT and GPRS
NB nbAccess(SERIAL_DEBUG); // NB access: include a 'true' parameter for debug enabled
GPRS gprsAccess;           // GPRS access
NBClient nbClient;         // NB client

MQTTClient client;         // MQTT client

boolean connected_to_network = false; // Network connection status

// Setup function: Initializes serial communication and MQTT client
void setup() {
    if (SERIAL_DEBUG) {
        Serial.begin(115200); // Start serial communication at 115200 baud rate
        while (!Serial) { }  // Wait for serial to initialize
        Serial.println("Starting Arduino Soft-Sensor MQTT device.");
    }

    // Initialize MQTT client with server details
    client.begin(mqtt_server, mqtt_port, nbClient);
}

// Function to initialize NB-IoT and GPRS connection
// Inputs: None
// Outputs: None
// Description: Attempts to connect to the NB-IoT and GPRS network until a connection is established.
void initializeNB_GPRS() {
    while (!connected_to_network) {
        // Attempt to connect to NB-IoT network and attach GPRS
        if ((nbAccess.begin(sim_pin, sim_apn, true, true) == NB_READY) && (gprsAccess.attachGPRS() == GPRS_READY)) {
            connected_to_network = true;
            if (SERIAL_DEBUG)
                Serial.println("Connected to network");
        } else {
            if (SERIAL_DEBUG)
                Serial.println("Not connected to network");
            delay(500); // Wait before retrying
        }
    }
}

// Function to reconnect to MQTT server
// Inputs: None
// Outputs: None
// Description: Attempts to connect to the MQTT server until a connection is established. If the connection fails, it waits for 5 seconds before retrying.
void reconnectMQTT() {
    // Loop until the client is connected
    while (!client.connected()) {
        if (SERIAL_DEBUG)
            Serial.print("Attempting MQTT connection...");
        // Attempt to connect with credentials
        if (client.connect("ArduinoClient", mqtt_user, mqtt_password)) {
            if (SERIAL_DEBUG)
                Serial.println("connected");
        } else {
            if (SERIAL_DEBUG) {
                Serial.print("failed, rc=");
                Serial.print(client.lastError());
                Serial.println(" try again in 5 seconds");
            }
            delay(5000); // Wait before retrying
        }
    }
}

// Function to read temperature sensor
// Inputs: None
// Outputs: float - temperature in Celsius
// Description: Requests a temperature measurement from the DallasTemperature sensor and returns the value in Celsius.
float read_temperature() {
    temperatureSensor.requestTemperatures(); // Request temperature measurement
    float temp = temperatureSensor.getTempCByIndex(0); // Get temperature in Celsius
    return temp;
}

// Function to read sensor data (temperature, pH, and EC)
// Inputs: None
// Outputs: String - formatted sensor data (temperature, EC, pH)
// Description: Reads temperature, pH, and EC sensor data, compensates pH and EC values with the temperature, optionally performs sensor calibration, and returns the formatted sensor data.
String read_sensor_data() {
    float temperature = read_temperature(); // Read temperature for compensation

    float ph_voltage = analogRead(PH_PIN) / 1024.0 * 3300; // Read and convert pH sensor voltage
    float ec_voltage = analogRead(EC_PIN) / 1024.0 * 3300; // Read and convert EC sensor voltage

    float phValue = ph.readPH(ph_voltage, temperature); // Convert voltage to pH with temperature compensation
    float ecValue = ec.readEC(ec_voltage, temperature); // Convert voltage to EC with temperature compensation

    if (SERIAL_DEBUG) {
        Serial.print("temperature: ");
        Serial.print(temperature, 1);
        Serial.print(" ºC  pH: ");
        Serial.print(phValue, 2);
        Serial.print(" EC: ");
        Serial.print(ecValue, 3);
        Serial.println(" ms/cm");
    }

    char cmd[10];
    if (readSerial(cmd)) {
        strupr(cmd);
        if (strstr(cmd, "PH")) {
            ph.calibration(ph_voltage, temperature, cmd); // Calibrate pH sensor
        }
        if (strstr(cmd, "EC")) {
            ec.calibration(ec_voltage, temperature, cmd); // Calibrate EC sensor
        }
    }

    // Format sensor data for MQTT publishing
    String sensor_data = "[[" + String(temperature, 4) + "," + String(ecValue, 4) + "," + String(phValue, 4) + "]]";
    return sensor_data;
}

// Main loop function
// Inputs: None
// Outputs: None
// Description: Main loop function that initializes the network connection, connects to the MQTT server, reads sensor data, publishes it to the MQTT topic, and then puts the device to sleep.
void loop() {
    if (SERIAL_DEBUG)
        Serial.println("Initializing NB-IoT and GPRS connection...");
    initializeNB_GPRS(); // Initialize network connection

    if (connected_to_network) {
        if (SERIAL_DEBUG)
            Serial.println("Device is connected to network. Trying to connect to MQTT broker...");

        if (!client.connected()) {
            reconnectMQTT(); // Reconnect to MQTT server if not connected
        }
        client.loop(); // Maintain MQTT connection

        if (SERIAL_DEBUG)
            Serial.println("Reading sensor data...");
        String sensor_data = read_sensor_data(); // Read sensor data

        if (SERIAL_DEBUG)
            Serial.println("Publishing data to MQTT topic...");
        client.publish("telemetry", sensor_data); // Publish data to MQTT topic

        if (SERIAL_DEBUG)
            Serial.println("Going to sleep...");

        LowPower.sleep(sleep_seconds * 1000); // Put device to sleep
    }
}

// Function to read serial input (used for calibration commands)
// Inputs: char result[] - array to store the read serial input
// Outputs: bool - true if a complete line is read, false otherwise
// Description: Reads a line from the serial input into the provided result array. Returns true if a complete line is read, otherwise returns false.
bool readSerial(char result[]) {
    int i = 0;
    while (Serial.available() > 0) {
        char inChar = Serial.read();
        if (inChar == '\n') {
            result[i] = '\0'; // Null-terminate the string
            Serial.flush();
            i = 0;
            return true;
        }
        if (inChar != '\r') {
            result[i] = inChar; // Add character to result array
            i++;
        }
        delay(1);
    }
    return false; // No complete line read
}
