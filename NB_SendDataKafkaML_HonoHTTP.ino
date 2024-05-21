// MKR Communication Library
#include <MKRNB.h>

// MKR Sensors Library
#include <OneWire.h>

#include <DallasTemperature.h>

#include <MKR_DFRobot_EC.h>

#include <MKR_DFRobot_PH.h>

#include "ArduinoLowPower.h"

// This library includes everything that should kept in secret. E.g. passwords, keys, tokens, URLs, etc.
#include "secrets.h"

// MKR Data Pins
#define EC_PIN A2
#define PH_PIN A4
#define TP_PIN A6

// Instancia a las clases OneWire y DallasTemperature
OneWire oneWireTemperature(TP_PIN);
DallasTemperature temperatureSensor(&oneWireTemperature);

MKR_DFRobot_PH ph;
MKR_DFRobot_EC ec;

const char url[] = RESEARCH_ADABYRON_HONO_URL;
const char path[] = RESEARCH_ADABYRON_HONO_PATH;
const int port = RESEARCH_ADABYRON_HONO_PORT;

const char user_pwd_b64[] = HONO_USER_PWD_B64;

const char sim_pin[] = SECRET_SIM_PINNUMBER;
const char sim_apn[] = SECRET_SIM_APN;

const int sleep_seconds = 10;

boolean SERIAL_DEBUG = false;

// initialize the library instance
NB nbAccess(SERIAL_DEBUG); // NB access: include a 'true' parameter for debug enabled
GPRS gprsAccess;           // GPRS access
NBClient client;           // NB client

boolean connected_to_network = false;

void setup()
{
  if (SERIAL_DEBUG)
  {
    Serial.begin(115200);
    while (!Serial)
    {
    }
    Serial.println("Starting Arduino Soft-Sensor POST device.");
  }
}

void initializeNB_GPRS()
{
  while (!connected_to_network)
  {
    if ((nbAccess.begin(sim_pin, sim_apn, true, true) == NB_READY) && (gprsAccess.attachGPRS() == GPRS_READY))
    {
      connected_to_network = true;
      if (SERIAL_DEBUG)
        Serial.println("Connected to network");
    }
    else
    {
      if (SERIAL_DEBUG)
        Serial.println("Not connected to network");
      delay(500);
    }
  }
}

void send_POST_HttpRequest(String message_to_post)
{

  int res_connect = client.connect(url, port);

  if (res_connect)
  {
    if (SERIAL_DEBUG)
      Serial.println("Connected to specified url:port/path");
    // make a HTTP 1.0 POST request (client sends the request)
    client.print("POST ");
    client.print(path);

    client.println(" HTTP/1.1");

    client.print("Host: ");
    client.println(url);

    client.print("Authorization: Basic ");
    client.println(user_pwd_b64);

    client.println("Connection: close");
    client.println("Content-Type: application/json");
    client.println("Content-Length: " + String(message_to_post.length()));
    client.println();
    client.println(message_to_post);

    if (SERIAL_DEBUG)
      Serial.println("POST request sent successfully");
  }
  else
  {
    if (SERIAL_DEBUG)
    {
      // if you didn't get a connection to the server
      char res_connect_char[10];
      sprintf(res_connect_char, "%d", res_connect);
      char error_message[100];
      strcpy(error_message, "Error connecting to server, error code: ");
      strcat(error_message, res_connect_char);
      Serial.println(error_message);
    }
  }
}

void read_server_response()
{
  // wait for the server to respond
  while (client.available() == 0)
  {
    if (!client.connected())
    {
      if (SERIAL_DEBUG)
        Serial.println("Connection to server lost");
      break;
    }
  }
  // if there are incoming bytes available
  // from the server, read them and print them: full response
  String response = "";
  while (client.available())
  {
    char c = client.read();
    response += c;
  }

  // print the result:
  if (SERIAL_DEBUG)
  {
    Serial.println("Response from server:");
    Serial.print(response);
    Serial.println();
  }

  // close the connection:
  client.stop();
  if (SERIAL_DEBUG)
    Serial.println("Connection to server closed");
}

bool readSerial(char result[])
{
  int i = 0;
  while (Serial.available() > 0)
  {
    char inChar = Serial.read();
    if (inChar == '\n')
    {
      result[i] = '\0';
      Serial.flush();
      i = 0;
      return true;
    }
    if (inChar != '\r')
    {
      result[i] = inChar;
      i++;
    }
    delay(1);
  }
  return false;
}

float read_temperature()
{
  temperatureSensor.requestTemperatures();
  float temp = temperatureSensor.getTempCByIndex(0);

  return temp;
}

String read_sensor_data()
{
  float temperature = read_temperature(); // read your temperature sensor to execute temperature compensation

  float ph_voltage = analogRead(PH_PIN) / 1024.0 * 3300; // read the voltage
  float ec_voltage = analogRead(EC_PIN) / 1024.0 * 3300; // read the voltage

  float phValue = ph.readPH(ph_voltage, temperature); // convert voltage to pH with temperature compensation
  float ecValue = ec.readEC(ec_voltage, temperature); // convert voltage to EC with temperature compensation

  if (SERIAL_DEBUG)
  {
    Serial.print("temperature: ");
    Serial.print(temperature, 1);

    Serial.print(" ºC  pH: ");
    Serial.print(phValue, 2);

    Serial.print(" EC: ");
    Serial.print(ecValue, 3);
    Serial.println(" ms/cm");
  }

  char cmd[10];
  if (readSerial(cmd))
  {
    strupr(cmd);
    if (strstr(cmd, "PH"))
    {
      ph.calibration(ph_voltage, temperature, cmd); // PH calibration process by Serail CMD
    }
    if (strstr(cmd, "EC"))
    {
      ec.calibration(ec_voltage, temperature, cmd); // EC calibration process by Serail CMD
    }
  }

  // Read sensor data. Format: [TEMP, COND, PH]
  String sensor_data = "[[" + String(temperature, 4) + "," + String(ecValue, 4) + "," + String(phValue, 4) + "]]";

  return sensor_data;
}

void loop()
{
  if (SERIAL_DEBUG)
    Serial.println("Initializing NB-IoT and GPRS connection...");
  initializeNB_GPRS();

  if (connected_to_network)
  {
    if (SERIAL_DEBUG)
      Serial.println("Device is connected to network. Trying to send HTTP POST");
    client = new NBClient();

    if (SERIAL_DEBUG)
      Serial.println("Reading sensor data...");
    String sensor_data = read_sensor_data();

    if (SERIAL_DEBUG)
      Serial.println("Sending POST request with data...");
    send_POST_HttpRequest(sensor_data);

    // Serial.println("Waiting for response from server...");
    // read_server_response();

    if (SERIAL_DEBUG)
      Serial.println("Going to sleep...");

    // Debug stop. It should work every loop

    if (SERIAL_DEBUG)
    {
      Serial.end();
      SERIAL_DEBUG = false;
    }

    LowPower.sleep(sleep_seconds * 1000);
  }
}
