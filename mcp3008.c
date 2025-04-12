#include <wiringPi.h>
#include <wiringPiSPI.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <time.h>

#define SPI_CHANNEL 0
#define SPI_SPEED 1000000

#define VREF 3.3       // Reference voltage
#define SHUNT_RESISTOR 0.1 // Shunt resistor value in ohms (example: 0.1?)
#define GAIN 5        // Current sensor gain (example: x5 for amplification)

// Function to read data from a specific ADC channel
int readADC(int channel) {
    unsigned char buffer[3];
    buffer[0] = 1; // Start bit
    buffer[1] = (8 + channel) << 4; // Single-ended mode for the specified channel
    buffer[2] = 0;

    wiringPiSPIDataRW(SPI_CHANNEL, buffer, 3);

    int result = ((buffer[1] & 3) << 8) + buffer[2]; // Combine the response bytes
    return result;
}

// Function to scale ADC value to pressure measurement
float scaleToCurrent(int adcValue) {
    float voltage = (adcValue / 1023.0) * VREF; // Convert ADC value to voltage
    float current = voltage / (SHUNT_RESISTOR * GAIN); // Calculate current using Ohm's law and gain
    return current;
}

float powercalc(float current) {
    float power = 230 * current;
    return power;
}

// Function to calculate energy in kWh
float calculateEnergyInkWh(float power, float time) {
    // Convert power from watts to kilowatts and time from seconds to hours
    float powerInkW = power / 1000.0;
    float timeInHours = time / 3600.0;
    
    // Calculate energy in kWh
    float energyInkWh = powerInkW * timeInHours;
    
    return energyInkWh;
}

// Function to log ADC values into InfluxDB
void logToInfluxDB(int channel, int value, float scaledValue, float power, float energyInkWh) {
    CURL *curl;
    CURLcode res;
    char influxDBURL[] = "http://192.168.0.112:8086/api/v2/write?org=BITS&bucket=SMARTENERGY&precision=s"; // Updated with your host ID
    char postData[256];

    // Format the data in InfluxDB's line protocol
    snprintf(postData, sizeof(postData), "adc_data,channel=%d raw_value=%d,scaled_value=%f,power_W=%f,energy_kWh=%f", channel, value, scaledValue, power, energyInkWh);

    curl = curl_easy_init();
    if (curl) {
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Authorization: Token puyQfEj07B5DM2cSGZ-Xr6J6eFI-9KOtew6yiNwzzN0FriGhkjHQ2kWmJf7milh06X-yH_iZmylljl955F84VQ=="); // Your API token

        curl_easy_setopt(curl, CURLOPT_URL, influxDBURL);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        // Execute HTTP POST request
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

int main() {
    // Initialize WiringPi and SPI
    if (wiringPiSetup() == -1) {
        printf("Failed to initialize WiringPi.\n");
        return -1;
    }

    if (wiringPiSPISetup(SPI_CHANNEL, SPI_SPEED) == -1) {
        printf("Failed to initialize SPI.\n");
        return -1;
    }

    // Initialize cumulative time for each channel
    float cumulativeTime2 = 0;
    float cumulativeTime4 = 0;
    float cumulativeTime6 = 0;

    time_t lastUpdateTime = time(NULL);

    while (1) {
        // Read values from channels 2, 4, and 6
        int adcValue2 = readADC(2); // Channel 2
        int adcValue4 = readADC(4); // Channel 4
        int adcValue6 = readADC(6); // Channel 6
        
        // Scale ADC value for channel
        float scaledValue2 = scaleToCurrent(adcValue2);
        float scaledValue4 = scaleToCurrent(adcValue4);
        float scaledValue6 = scaleToCurrent(adcValue6);
        
        // Power value for channel
        float power2 = powercalc(scaledValue2);
        float power4 = powercalc(scaledValue4);
        float power6 = powercalc(scaledValue6);

        // Calculate time elapsed since last update
        time_t currentTime = time(NULL);
        float timeElapsed = difftime(currentTime, lastUpdateTime);
        lastUpdateTime = currentTime;

        // Update cumulative time
        cumulativeTime2 += timeElapsed;
        cumulativeTime4 += timeElapsed;
        cumulativeTime6 += timeElapsed;

        // Calculate energy in kWh
        float energy2 = calculateEnergyInkWh(power2, cumulativeTime2);
        float energy4 = calculateEnergyInkWh(power4, cumulativeTime4);
        float energy6 = calculateEnergyInkWh(power6, cumulativeTime6);

        // Print the values to the console
        printf("ADC Value Channel 2: %d, Scaled Value: %.2f A, Power: %.2f W, Energy: %.6f kWh\n", adcValue2, scaledValue2, power2, energy2);
        printf("ADC Value Channel 4: %d, Scaled Value: %.2f A, Power: %.2f W, Energy: %.6f kWh\n", adcValue4, scaledValue4, power4, energy4);
        printf("ADC Value Channel 6: %d, Scaled Value: %.2f A, Power: %.2f W, Energy: %.6f kWh\n", adcValue6, scaledValue6, power6, energy6);

        // Log the values into InfluxDB
        logToInfluxDB(2, adcValue2, scaledValue2, power2, energy2);
        logToInfluxDB(4, adcValue4, scaledValue4, power4, energy4);
        logToInfluxDB(6, adcValue6, scaledValue6, power6, energy6);

        delay(1000); // Wait for 1000ms before reading again
    }

    return 0;
}
