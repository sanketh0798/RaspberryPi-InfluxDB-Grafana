In this project, an embedded system to monitor household loads is implemented.
Workflow: ACS712 sensor (Analog AMP meter) -> MCP3008 ADC (ANalog Digital SPI) -> Raspberry Pi 4 Model B -> InfluxDB database -> Grafana dashboard
The database and the dashboard are hosted in the local raspberry pi server with port 8086 and 3000 respectively.

---

# Raspberry Pi Analog Data Logger(MCP3008.c)

This project reads analog data from GPIO pins on a Raspberry Pi, processes it, and saves it to an InfluxDB database. The code is written in C and uses the WiringPi library for GPIO and SPI communication.

## Prerequisites

- Raspberry Pi
- WiringPi library
- InfluxDB
- CURL library
- InfluxDB
- Grafana

## Installation

1. **Install WiringPi:**
   ```bash
   sudo apt-get update
   sudo apt upgrade
   sudo apt-get install wiringpi
   ```

2. **Install CURL:**
   ```bash
   sudo apt-get install libcurl4-openssl-dev
   ```

3. **Clone the repository:**
   ```bash
   git clone <repository_url>
   cd <repository_name>
   ```
4. **Install Grafana:**
   ```bash
   echo "deb https://packages.grafana.com/oss/deb stable main" | sudo tee /etc/apt/sources.list.d/grafana.list
   curl https://packages.grafana.com/gpg.key | sudo gpg --dearmor -o /usr/share/keyrings/grafana-archive-keyring.gpg
   sudo apt update
   sudo apt install grafana -y
   
5. **Install InfluxDB:**
   ```bash
   curl https://repos.influxdata.com/influxdata-archive.key | gpg --dearmor | sudo tee /usr/share/keyrings/influxdb-archive-keyring.gpg > /dev/null   //Add the InfluxDB Repository
   echo "deb [signed-by=/usr/share/keyrings/influxdb-archive-keyring.gpg] https://repos.influxdata.com/debian stable main" | sudo tee /etc/apt/sources.list.d/influxdb.list   //Add the Repository to the Sources List
   sudo apt install influxdb2  //Install InfluxDB
   

## Usage

1. **Compile the code:**
   ```bash
   gcc -o mcp3008 mcp3008.c -lwiringPi -lcurl
   ```

2. **Run the program:**
   ```bash
   sudo systemctl start grafana-server  \\Start Grafana server
   sudo systemctl enable grafana-server \\Enable Grafana server
   sudo systemctl status influxdb \\ Check Grafana Status
   
   sudo systemctl start influxdb  \\Start Influx Server
   sudo systemctl enable influxdb \\Enable Influx Server
   sudo systemctl status influxdb \\ Check Influx Db status
   sudo ./mcp3008  \\ Run C Program

   /*
   Once installed,
   you can access Grafana's web interface by navigating to http://<your-raspberry-pi-ip>:3000 in your browser
   you can configure InfluxDB by accessing its web interface at http://<your-raspberry-pi-ip>:8086
   */
   ```

## Code Breakdown

### Includes and Defines
```c
#include <wiringPi.h>
#include <wiringPiSPI.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <time.h>

#define SPI_CHANNEL 0
#define SPI_SPEED 1000000

#define VREF 3.3
#define SHUNT_RESISTOR 0.1
#define GAIN 5
```
- **Libraries:** Includes necessary libraries for GPIO, SPI, HTTP requests, and time management.
- **Defines:** Sets constants for SPI channel, speed, reference voltage, shunt resistor value, and current sensor gain.

### Functions

#### `readADC(int channel)`
```c
int readADC(int channel) {
    unsigned char buffer[3];
    buffer[0] = 1;
    buffer[1] = (8 + channel) << 4;
    buffer[2] = 0;

    wiringPiSPIDataRW(SPI_CHANNEL, buffer, 3);

    int result = ((buffer[1] & 3) << 8) + buffer[2];
    return result;
}
```
- **Purpose:** Reads data from a specific ADC channel using SPI communication.

#### `scaleToCurrent(int adcValue)`
```c
float scaleToCurrent(int adcValue) {
    float voltage = (adcValue / 1023.0) * VREF;
    float current = voltage / (SHUNT_RESISTOR * GAIN);
    return current;
}
```
- **Purpose:** Converts ADC value to current measurement.

#### `powercalc(float current)`
```c
float powercalc(float current) {
    float power = 230 * current;
    return power;
}
```
- **Purpose:** Calculates power based on current measurement.

#### `calculateEnergyInkWh(float power, float time)`
```c
float calculateEnergyInkWh(float power, float time) {
    float powerInkW = power / 1000.0;
    float timeInHours = time / 3600.0;
    float energyInkWh = powerInkW * timeInHours;
    return energyInkWh;
}
```
- **Purpose:** Calculates energy in kWh based on power and time.

#### `logToInfluxDB(int channel, int value, float scaledValue, float power, float energyInkWh)`
```c
void logToInfluxDB(int channel, int value, float scaledValue, float power, float energyInkWh) {
    CURL *curl;
    CURLcode res;
    char influxDBURL[] = "http://192.168.0.112:8086/api/v2/write?org=BITS&bucket=SMARTENERGY&precision=s";
    char postData[256];

    snprintf(postData, sizeof(postData), "adc_data,channel=%d raw_value=%d,scaled_value=%f,power_W=%f,energy_kWh=%f", channel, value, scaledValue, power, energyInkWh);

    curl = curl_easy_init();
    if (curl) {
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Authorization: Token puyQfEj07B5DM2cSGZ-Xr6J6eFI-9KOtew6yiNwzzN0FriGhkjHQ2kWmJf7milh06X-yH_iZmylljl955F84VQ==");

        curl_easy_setopt(curl, CURLOPT_URL, influxDBURL);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            fprintf(stderr, "Curl failed: %s\n", curl_easy_strerror(res));
        }
         long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        if (http_code != 204) {
            fprintf(stderr, "InfluxDB write failed (HTTP %ld)\n", http_code);
            fprintf(stderr, "Posted data:\n%s\n", postData);
        }
        curl_easy_cleanup(curl);
    }
}
```
- **Purpose:** Logs ADC values into InfluxDB using HTTP POST requests.

### Main Function
```c
int main() {
    if (wiringPiSetup() == -1) {
        printf("Failed to initialize WiringPi.\n");
        return -1;
    }

    if (wiringPiSPISetup(SPI_CHANNEL, SPI_SPEED) == -1) {
        printf("Failed to initialize SPI.\n");
        return -1;
    }

    float cumulativeTime2 = 0;
    float cumulativeTime4 = 0;
    float cumulativeTime6 = 0;

    time_t lastUpdateTime = time(NULL);

    while (1) {
        int adcValue2 = readADC(2);
        int adcValue4 = readADC(4);
        int adcValue6 = readADC(6);

        float scaledValue2 = scaleToCurrent(adcValue2);
        float scaledValue4 = scaleToCurrent(adcValue4);
        float scaledValue6 = scaleToCurrent(adcValue6);

        float power2 = powercalc(scaledValue2);
        float power4 = powercalc(scaledValue4);
        float power6 = powercalc(scaledValue6);

        time_t currentTime = time(NULL);
        float timeElapsed = difftime(currentTime, lastUpdateTime);
        lastUpdateTime = currentTime;

        cumulativeTime2 += timeElapsed;
        cumulativeTime4 += timeElapsed;
        cumulativeTime6 += timeElapsed;

        float energy2 = calculateEnergyInkWh(power2, cumulativeTime2);
        float energy4 = calculateEnergyInkWh(power4, cumulativeTime4);
        float energy6 = calculateEnergyInkWh(power6, cumulativeTime6);

        printf("ADC Value Channel 2: %d, Scaled Value: %.2f A, Power: %.2f W, Energy: %.6f kWh\n", adcValue2, scaledValue2, power2, energy2);
        printf("ADC Value Channel 4: %d, Scaled Value: %.2f A, Power: %.2f W, Energy: %.6f kWh\n", adcValue4, scaledValue4, power4, energy4);
        printf("ADC Value Channel 6: %d, Scaled Value: %.2f A, Power: %.2f W, Energy: %.6f kWh\n", adcValue6, scaledValue6, power6, energy6);

        logToInfluxDB(2, adcValue2, scaledValue2, power2, energy2);
        logToInfluxDB(4, adcValue4, scaledValue4, power4, energy4);
        logToInfluxDB(6, adcValue6, scaledValue6, power6, energy6);

        delay(1000);
    }

    return 0;
}
```
- **Purpose:** Initializes WiringPi and SPI, reads ADC values from channels 2, 4, and 6, scales the values, calculates power and energy, logs the data to InfluxDB, and prints the values to the console.

---

Feel free to adjust the `README.md` file as needed. Let me know if you need any further assistance!
