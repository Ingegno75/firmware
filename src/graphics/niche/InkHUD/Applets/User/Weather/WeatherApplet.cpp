#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./WeatherApplet.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "mesh/wifi/WiFiAPClient.h"

using namespace NicheGraphics;

// =====================================================================
// CONFIGURAZIONE: modifica queste 3 righe prima di compilare
// =====================================================================
// Coordinate del luogo di cui mostrare il meteo.
// Esempio: Roma = 41.9028, 12.4964 -- Milano = 45.4642, 9.1900
static constexpr float WEATHER_LATITUDE = 45.068;
static constexpr float WEATHER_LONGITUDE = 7.577;
// =====================================================================

static const char *WEATHER_API_HOST = "https://api.open-meteo.com/v1/forecast";

// -----------------------------------------------------------------------
// WeatherApplet
// -----------------------------------------------------------------------

InkHUD::WeatherApplet::WeatherApplet()
{
    // Aggiorna anche se non è la applet mostrata in primo piano
    // (in questo caso è comunque l'unica applet attiva)
}

void InkHUD::WeatherApplet::onActivate()
{
    if (!fetcher)
        fetcher = new WeatherFetcher(this);
}

void InkHUD::WeatherApplet::onDeactivate()
{
    // Il fetcher continua comunque a girare in background;
    // lo lasciamo vivo per semplicità (un solo applet attivo in questo firmware)
}

void InkHUD::WeatherApplet::setCurrentConditions(float tempC, float feelsLikeC, int humidity, int weatherCode)
{
    currentTempC = tempC;
    currentFeelsLikeC = feelsLikeC;
    currentHumidity = humidity;
    currentWeatherCode = weatherCode;
    hasData = true;
    lastFetchFailed = false;
    requestUpdate(); // Chiede il refresh del display
}

void InkHUD::WeatherApplet::setForecastDay(uint8_t index, const WeatherDayForecast &day)
{
    if (index < FORECAST_DAYS)
        forecast[index] = day;
}

void InkHUD::WeatherApplet::markFetchFailed()
{
    lastFetchFailed = true;
    requestUpdate();
}

std::string InkHUD::WeatherApplet::describeWeatherCode(int code)
{
    // Codici meteo WMO (usati da Open-Meteo)
    if (code == 0)
        return "Sereno";
    if (code >= 1 && code <= 3)
        return "Poco nuvoloso";
    if (code == 45 || code == 48)
        return "Nebbia";
    if (code >= 51 && code <= 67)
        return "Pioggia";
    if (code >= 71 && code <= 77)
        return "Neve";
    if (code >= 80 && code <= 82)
        return "Rovesci";
    if (code >= 95)
        return "Temporale";
    return "N/D";
}

void InkHUD::WeatherApplet::drawWeatherIcon(int16_t cx, int16_t cy, uint16_t size, int weatherCode)
{
    // Icone MOLTO SEMPLICI e INTUITIVE per e-ink: piene, non contorno, molto leggibili
    uint16_t r = size / 2;

    if (weatherCode == 0) {
        // SOLE: cerchio pieno
        fillCircle(cx, cy, r * 0.55, BLACK);

    } else if (weatherCode <= 3) {
        // SOLE CON NUVOLA: sole pieno piccolo + nuvola
        fillCircle(cx - r * 0.25, cy - r * 0.15, r * 0.35, BLACK); // sole piccolo
        // Nuvola (due cerchi che si sovrappongono)
        fillCircle(cx + r * 0.15, cy + r * 0.1, r * 0.4, BLACK);
        fillCircle(cx + r * 0.45, cy + r * 0.1, r * 0.35, BLACK);

    } else if (weatherCode == 45 || weatherCode == 48) {
        // NEBBIA: rettangolo/linee orizzontali piene
        for (int i = -2; i <= 2; i++) {
            int16_t y = cy + i * (r / 2.2);
            drawLine(cx - r * 0.65, y, cx + r * 0.65, y, BLACK);
            drawLine(cx - r * 0.65, y + 1, cx + r * 0.65, y + 1, BLACK); // Raddoppio lo spessore
        }

    } else if ((weatherCode >= 51 && weatherCode <= 67) || (weatherCode >= 80 && weatherCode <= 82)) {
        // NUVOLA CON GOCCE: nuvola piena + gocce sotto
        // Nuvola (due cerchi che si sovrappongono)
        fillCircle(cx - r * 0.2, cy - r * 0.15, r * 0.4, BLACK);
        fillCircle(cx + r * 0.25, cy - r * 0.05, r * 0.4, BLACK);
        // Gocce (linee diagonali spesse + puntini)
        fillCircle(cx - r * 0.25, cy + r * 0.45, 3, BLACK);
        fillCircle(cx + r * 0.05, cy + r * 0.45, 3, BLACK);
        fillCircle(cx + r * 0.35, cy + r * 0.45, 3, BLACK);

    } else if (weatherCode >= 71 && weatherCode <= 77) {
        // NEVE: nuvola piena + fiocchi (puntini più grandi)
        // Nuvola (due cerchi che si sovrappongono)
        fillCircle(cx - r * 0.2, cy - r * 0.15, r * 0.4, BLACK);
        fillCircle(cx + r * 0.25, cy - r * 0.05, r * 0.4, BLACK);
        // Fiocchi (puntini)
        fillCircle(cx - r * 0.25, cy + r * 0.45, 3, BLACK);
        fillCircle(cx + r * 0.05, cy + r * 0.45, 3, BLACK);
        fillCircle(cx + r * 0.35, cy + r * 0.45, 3, BLACK);

    } else if (weatherCode >= 95) {
        // TEMPORALE: nuvola piena + gocce grandi
        // Nuvola (due cerchi che si sovrappongono)
        fillCircle(cx - r * 0.2, cy - r * 0.15, r * 0.4, BLACK);
        fillCircle(cx + r * 0.25, cy - r * 0.05, r * 0.4, BLACK);
        // Gocce grandi (puntini più grossi)
        fillCircle(cx - r * 0.25, cy + r * 0.45, 4, BLACK);
        fillCircle(cx + r * 0.05, cy + r * 0.45, 4, BLACK);
        fillCircle(cx + r * 0.35, cy + r * 0.45, 4, BLACK);

    } else {
        // DEFAULT: sole
        fillCircle(cx, cy, r * 0.55, BLACK);
    }
}

void InkHUD::WeatherApplet::onRender(bool full)
{
    if (!hasData) {
        setFont(fontMedium);
        printAt(X(0.5), Y(0.5), lastFetchFailed ? "Errore connessione meteo" : "Attendo dati meteo...", CENTER,
                MIDDLE);
        return;
    }

    // ---- Riga superiore: data - temperatura + icona grande a destra ----
    setFont(fontLarge);
    
    // Data e temperatura sulla stessa riga: "08 Agosto - 25°C"
    char dateTemp[32];
    snprintf(dateTemp, sizeof(dateTemp), "08 Agosto - %.0f\xB0" "C", currentTempC);
    printAt(X(0.02), Y(0.02), dateTemp, LEFT, TOP);

    // Umidità sotto
    setFont(fontSmall);
    char humidStr[24];
    snprintf(humidStr, sizeof(humidStr), "Umidita %d%%", currentHumidity);
    printAt(X(0.02), Y(0.14), humidStr, LEFT, TOP);

    // Icona meteo ANCORA PIÙ GRANDE a destra
    drawWeatherIcon(X(0.85), Y(0.08), Y(0.32), currentWeatherCode);

    // ---- Riga centrale: temperatura percepita (più leggibile) ----
    setFont(fontSmall);
    char feelsStr[48];
    snprintf(feelsStr, sizeof(feelsStr), "Percepita a Grugliasco: %.0f\xB0" "C", currentFeelsLikeC);
    printAt(X(0.02), Y(0.38), feelsStr, LEFT, TOP);

    // ---- Linea divisoria ----
    drawLine(X(0.0), Y(0.48), X(1.0), Y(0.48), BLACK);

    // ---- Riga inferiore: previsione 3 giorni con layout nuovo ----
    // 3 colonne uguali, ogni colonna ha: icona grande a sinistra, temp max sopra a destra, temp min sotto a destra
    uint16_t colWidth = X(1.0) / FORECAST_DAYS;
    for (uint8_t i = 0; i < FORECAST_DAYS; i++) {
        int16_t colLeft = colWidth * i;
        int16_t colCenter = colLeft + colWidth / 2;

        // Icona ancora più grande a SINISTRA della colonna
        drawWeatherIcon(colLeft + X(0.08), Y(0.70), Y(0.22), forecast[i].weatherCode);

        // Temperatura MAX a DESTRA, in alto
        setFont(fontMedium);
        char tempMax[8];
        snprintf(tempMax, sizeof(tempMax), "%d\xB0", forecast[i].tempMaxC);
        printAt(colLeft + X(0.22), Y(0.52), tempMax, RIGHT, TOP);

        // Temperatura MIN a DESTRA, in basso
        setFont(fontMedium);
        char tempMin[8];
        snprintf(tempMin, sizeof(tempMin), "%d\xB0", forecast[i].tempMinC);
        printAt(colLeft + X(0.22), Y(0.70), tempMin, RIGHT, TOP);

        // Linea verticale tra colonne (se non è l'ultima)
        if (i > 0)
            drawLine(colWidth * i, Y(0.50), colWidth * i, Y(1.0), BLACK);
    }
}

// -----------------------------------------------------------------------
// WeatherFetcher: OSThread che interroga Open-Meteo una volta all'ora
// -----------------------------------------------------------------------

InkHUD::WeatherFetcher::WeatherFetcher(WeatherApplet *applet) : concurrency::OSThread("WeatherFetcher"), applet(applet)
{
    // Primo tentativo poco dopo l'avvio, per non ritardare il boot
    setIntervalFromNow(15 * 1000);
}

int32_t InkHUD::WeatherFetcher::runOnce()
{
    fetchWeather();
    return UPDATE_INTERVAL_MS;
}

void InkHUD::WeatherFetcher::fetchWeather()
{
    LOG_INFO("Weather: tentativo di aggiornamento (wifiAvailable=%d, wifiStatus=%d)", isWifiAvailable(),
              WiFi.status());

    if (!isWifiAvailable() || WiFi.status() != WL_CONNECTED) {
        LOG_WARN("Weather: Wi-Fi non connesso, riprovo tra %d secondi", (int)(RETRY_INTERVAL_MS / 1000));
        applet->markFetchFailed();
        setIntervalFromNow(RETRY_INTERVAL_MS);
        return;
    }

    char url[256];
    snprintf(url, sizeof(url),
             "%s?latitude=%.4f&longitude=%.4f&current=temperature_2m,relative_humidity_2m,apparent_temperature,"
             "weather_code&daily=weather_code,temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=4",
             WEATHER_API_HOST, WEATHER_LATITUDE, WEATHER_LONGITUDE);
    LOG_INFO("Weather: chiamata a %s", url);

    WiFiClientSecure secureClient;
    secureClient.setInsecure(); // Non verifichiamo il certificato: sufficiente per una richiesta di sola lettura
    HTTPClient http;
    http.begin(secureClient, url);
    http.setTimeout(10000);
    int httpCode = http.GET();
    LOG_INFO("Weather: risposta HTTP code = %d", httpCode);

    if (httpCode != HTTP_CODE_OK) {
        LOG_ERROR("Weather: richiesta HTTP fallita, codice %d", httpCode);
        http.end();
        applet->markFetchFailed();
        setIntervalFromNow(RETRY_INTERVAL_MS);
        return;
    }

    // Leggiamo prima l'intera risposta come testo, poi la interpretiamo:
    // più affidabile dello streaming diretto quando si usa HTTPS su ESP32
    String payload = http.getString();
    http.end();

    // Documento JSON con dimensionamento automatico (API ArduinoJson v7)
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);

    if (err) {
        LOG_ERROR("Weather: errore parsing JSON: %s", err.c_str());
        applet->markFetchFailed();
        setIntervalFromNow(RETRY_INTERVAL_MS);
        return;
    }

    // ---- Condizioni attuali ----
    float tempC = doc["current"]["temperature_2m"] | 0.0f;
    float feelsLikeC = doc["current"]["apparent_temperature"] | tempC;
    int humidity = doc["current"]["relative_humidity_2m"] | 0;
    int weatherCode = doc["current"]["weather_code"] | 0;
    applet->setCurrentConditions(tempC, feelsLikeC, humidity, weatherCode);

    // ---- Previsione: saltiamo l'indice 0 (oggi, già coperto dal blocco
    //      "current") e usiamo i 3 giorni successivi ----
    JsonArray dates = doc["daily"]["time"];
    JsonArray codes = doc["daily"]["weather_code"];
    JsonArray tmax = doc["daily"]["temperature_2m_max"];
    JsonArray tmin = doc["daily"]["temperature_2m_min"];

    for (uint8_t i = 0; i < 3; i++) {
        uint8_t srcIndex = i + 1; // giorno successivo
        WeatherDayForecast day;

        if (srcIndex < dates.size()) {
            std::string date = dates[srcIndex].as<std::string>(); // "YYYY-MM-DD"
            if (date.size() == 10)
                day.dayLabel = date.substr(8, 2) + "/" + date.substr(5, 2); // "DD/MM"
        }
        if (srcIndex < codes.size())
            day.weatherCode = codes[srcIndex].as<int>();
        if (srcIndex < tmax.size())
            day.tempMaxC = (int16_t)round(tmax[srcIndex].as<float>());
        if (srcIndex < tmin.size())
            day.tempMinC = (int16_t)round(tmin[srcIndex].as<float>());

        applet->setForecastDay(i, day);
    }

    LOG_INFO("Weather: aggiornamento completato con successo (%.1f gradi)", tempC);
}

#endif
