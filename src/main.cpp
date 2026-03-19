/**
 * Sketch: Servidor BLE ESP32-S3 con ADC, Control de LED y JSON Status
 * Versión CON NOTIFICACIONES AUTOMÁTICAS y CARACTERÍSTICA JSON
 */

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>

// UUIDs
#define SERVICE_UUID            "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_ADC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define CHARACTERISTIC_LED_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a9"
#define CHARACTERISTIC_JSON_UUID "beb5483e-36e1-4688-b7f5-ea07361b26aa"  // UUID para característica JSON

// Pines
#define ADC_PIN 4           // GPIO4 para ADC en ESP32-S3

// Variables globales
BLECharacteristic *pAdcCharacteristic;
BLECharacteristic *pLedCharacteristic;
BLECharacteristic *pJsonCharacteristic;  // Nueva característica para JSON
bool deviceConnected = false;
bool ledState = false;      // Variable para trackear el estado del LED
bool notificationsEnabled = false;  // Flag para saber si el cliente activó notificaciones
unsigned long startTime;    // Tiempo de inicio para calcular uptime

// Callback para manejar eventos de conexión del servidor
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        deviceConnected = true;
        Serial.println("✅ Cliente BLE conectado");
    }

    void onDisconnect(BLEServer* pServer) {
        deviceConnected = false;
        notificationsEnabled = false;  // Reset al desconectar
        Serial.println("❌ Cliente BLE desconectado");
        pServer->getAdvertising()->start();
        Serial.println("📡 Advertising reiniciado...");
    }
};
void updateJsonCharacteristic() {
    // Calcular uptime en segundos
    unsigned long uptime = (millis() - startTime) / 1000;
    
    // Crear string JSON manualmente (sin librería para mantener ligereza)
    String jsonString = "{\"adc\":" + String(analogRead(ADC_PIN)) + 
                        ",\"led\":" + String(ledState ? "true" : "false") + 
                        ",\"uptime\":" + String(uptime) + "}";
    
    // Actualizar característica
    pJsonCharacteristic->setValue(jsonString.c_str());
    
    // Opcional: Mostrar JSON en monitor serie
    static unsigned long lastJsonLog = 0;
    if (millis() - lastJsonLog > 10000) {  // Cada 10 segundos
        Serial.print("📋 JSON actualizado: ");
        Serial.println(jsonString);
        lastJsonLog = millis();
    }
}

// Callback para manejar eventos de escritura en la característica LED
class LedCharacteristicCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
        std::string value = pCharacteristic->getValue();
        
        if (value.length() > 0) {
            Serial.print("📝 Comando recibido: '");
            Serial.print(value.c_str());
            Serial.println("'");
            
            if (value == "1") {
                digitalWrite(LED_BUILTIN, HIGH);
                ledState = true;
                Serial.println("💡 LED ENCENDIDO");
                
                // Actualizar también el valor de la característica JSON
                updateJsonCharacteristic();
            } 
            else if (value == "0") {
                digitalWrite(LED_BUILTIN, LOW);
                ledState = false;
                Serial.println("💡 LED APAGADO");
                
                // Actualizar también el valor de la característica JSON
                updateJsonCharacteristic();
            } 
            else {
                Serial.println("❓ Comando no reconocido (usa '1' o '0')");
            }
        }
    }
};

/**
 * Callback para el descriptor BLE2902 (CCCD)
 * Se activa cuando el cliente habilita/deshabilita notificaciones
 */
class MyCCCDCallbacks: public BLEDescriptorCallbacks {
    void onWrite(BLEDescriptor* pDescriptor) {
        uint8_t* value = pDescriptor->getValue();
        
        if (pDescriptor->getLength() >= 2) {
            // El valor del CCCD determina si las notificaciones están activadas
            // 0x0000: Notificaciones deshabilitadas
            // 0x0001: Notificaciones habilitadas
            uint16_t cccdValue = (value[1] << 8) | value[0];
            
            if (cccdValue == 0x0001) {
                notificationsEnabled = true;
                Serial.println("🔔 Notificaciones ADC ACTIVADAS por el cliente");
            } 
            else if (cccdValue == 0x0000) {
                notificationsEnabled = false;
                Serial.println("🔕 Notificaciones ADC DESACTIVADAS por el cliente");
            }
            
            Serial.print("📋 CCCD valor: 0x");
            Serial.println(cccdValue, HEX);
        }
    }
};

/**
 * Función para actualizar la característica JSON con los valores actuales
 */

/**
 * Callback para lecturas de la característica JSON
 * Se ejecuta cuando el cliente hace una lectura manual
 */
class JsonCharacteristicCallbacks: public BLECharacteristicCallbacks {
    void onRead(BLECharacteristic *pCharacteristic) {
        Serial.println("📖 Cliente leyendo característica JSON");
        updateJsonCharacteristic();  // Actualizar antes de leer
    }
};

void setup() {
    // Guardar tiempo de inicio
    startTime = millis();
    
    // Inicializar Serial
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n🔌 ===============================");
    Serial.println("🔌 Servidor BLE ESP32-S3");
    Serial.println("🔌 Notificaciones automáticas + JSON");
    Serial.println("🔌 ===============================");
    
    // Info de la placa
    Serial.print("📋 Modelo: ");
    Serial.println(ESP.getChipModel());
    Serial.print("📋 LED_BUILTIN: GPIO");
    Serial.println(LED_BUILTIN);

    // Configurar LED
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);
    ledState = false;
    Serial.println("💡 LED configurado y apagado");

    // Configurar ADC
    pinMode(ADC_PIN, INPUT);
    analogReadResolution(12);
    Serial.print("📊 ADC en GPIO");
    Serial.println(ADC_PIN);

    // Inicializar BLE
    BLEDevice::init("ESP32-S3_NOEL");
    Serial.println("📡 BLE inicializado");

    // Crear servidor
    BLEServer *pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    // Crear servicio
    BLEService *pService = pServer->createService(SERVICE_UUID);

    // ----- Característica ADC (con NOTIFY) -----
    pAdcCharacteristic = new BLECharacteristic(
        CHARACTERISTIC_ADC_UUID,
        BLECharacteristic::PROPERTY_READ | 
        BLECharacteristic::PROPERTY_NOTIFY
    );
    
    pService->addCharacteristic(pAdcCharacteristic);
    
    // Configurar descriptor BLE2902 (CCCD) con callback personalizado
    BLE2902 *pBLE2902_adc = new BLE2902();
    pBLE2902_adc->setCallbacks(new MyCCCDCallbacks());
    pBLE2902_adc->setNotifications(false);
    pAdcCharacteristic->addDescriptor(pBLE2902_adc);
    
    Serial.println("📊 Característica ADC: READ | NOTIFY");
    Serial.println("   └─ Descriptor BLE2902 configurado");

    // ----- Característica LED (WRITE) -----
    pLedCharacteristic = new BLECharacteristic(
        CHARACTERISTIC_LED_UUID,
        BLECharacteristic::PROPERTY_WRITE
    );
    
    pService->addCharacteristic(pLedCharacteristic);
    pLedCharacteristic->setCallbacks(new LedCharacteristicCallbacks());
    
    Serial.println("💡 Característica LED: WRITE");

    // ----- Característica JSON (READ) -----
    pJsonCharacteristic = new BLECharacteristic(
        CHARACTERISTIC_JSON_UUID,
        BLECharacteristic::PROPERTY_READ
    );
    
    pService->addCharacteristic(pJsonCharacteristic);
    pJsonCharacteristic->setCallbacks(new JsonCharacteristicCallbacks());
    
    // Valor inicial
    updateJsonCharacteristic();
    
    Serial.println("📋 Característica JSON: READ");
    Serial.println("   └─ Formato: {\"adc\":123, \"led\":true/false, \"uptime\":1234}");

    // Iniciar servicio
    pService->start();
    Serial.println("✅ Servicio iniciado");

    // Advertising
    BLEAdvertising *pAdvertising = pServer->getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);
    pAdvertising->setMinPreferred(0x12);
    
    BLEDevice::startAdvertising();
    
    Serial.println("-------------------------------------------");
    Serial.println("📱 Busca 'ESP32-S3_NOEL' en nRF Connect");
    Serial.println("💡 Envía '1' o '0' para controlar LED");
    Serial.println("📋 Característica JSON (READ) - Lee estado completo:");
    Serial.println("   {\"adc\":valor, \"led\":true/false, \"uptime\":segundos}");
    Serial.println("🔔 Para notificaciones ADC: escribe 0100 en descriptor CCCD");
    Serial.println("===========================================\n");
}

void loop() {
    // Timer para notificaciones automáticas (1 segundo)
    static unsigned long lastNotifyTime = 0;
    
    if (deviceConnected && notificationsEnabled) {
        // Enviar notificación cada 1 segundo exactamente
        if (millis() - lastNotifyTime >= 1000) {
            // Leer valor del ADC
            int adcValue = analogRead(ADC_PIN);
            
            // Actualizar el valor de la característica ADC
            pAdcCharacteristic->setValue(adcValue);
            
            // Enviar NOTIFICACIÓN automática al cliente
            pAdcCharacteristic->notify();
            
            // Actualizar también JSON (opcional, pero mantiene los datos frescos)
            updateJsonCharacteristic();
            
            // Mostrar en monitor serie
            float voltage = (adcValue / 4095.0) * 3.3;
            Serial.printf("🔔 NOTIFICACIÓN - ADC: %d (%.2fV) | LED: %s\n", 
                adcValue, 
                voltage,
                ledState ? "ON" : "OFF"
            );
            
            lastNotifyTime = millis();
        }
    }
    else if (deviceConnected && !notificationsEnabled) {
        // Si está conectado pero sin notificaciones, solo mostrar estado cada 5 segundos
        static unsigned long lastStatusTime = 0;
        if (millis() - lastStatusTime >= 5000) {
            Serial.println("⏸️  Conectado - Esperando activación de notificaciones...");
            Serial.println("   En nRF Connect: busca el descriptor CCCD y escribe 0100");
            lastStatusTime = millis();
        }
    }
    
    // Pequeña pausa
    delay(10);
}