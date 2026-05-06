#include <FS.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include <WebServer.h>

const char *ssid = "6666666644444444";
const char *password = "66668888";
WebServer server(80);
File uploadFile;

#define START_BYTE 0xAA
#define ACK        0x79
#define NACK       0x1F
#define BLOCK_SIZE 256

// UART1 cho giao tiếp với STM32
#define STM_BAUD   115200
#define STM_RXPIN  4   
#define STM_TXPIN  5   

// Biến lưu thông tin OTA
uint8_t ota_version = 1;
bool simulate_fail = false; // Cờ mô phỏng đứt mạng

uint16_t simpleCRC(uint8_t *data, int len) {
    uint16_t crc = 0;
    for (int i = 0; i < len; i++) {
        crc += data[i];
    }
    return crc;
}

bool waitForAck(uint32_t timeout = 1000) {
    uint32_t start = millis();
    while (millis() - start < timeout) {
        if (Serial1.available()) {
            uint8_t resp = Serial1.read();
            if (resp == ACK) return true;
        }
    }
    return false;
}

// -----------------------------------------------------------------
// XỬ LÝ UPLOAD FILE: Tự động đổi tên dựa trên Input của HTML 
// -----------------------------------------------------------------
void handleUpload() {
    HTTPUpload &upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        String inputName = upload.name;
        String targetFilename;
        
        if (inputName == "fileA") {
            targetFilename = "/firm_slot_A.bin";
        } else if (inputName == "fileB") {
            targetFilename = "/firm_slot_B.bin";
        } else {
            targetFilename = "/" + upload.filename;
        }
        
        Serial.printf("[Upload] Received file: %s. Saving internal as: %s\n", upload.filename.c_str(), targetFilename.c_str());
        uploadFile = SPIFFS.open(targetFilename, FILE_WRITE);
        if (!uploadFile) {
            Serial.println("[Error] Cannot open file for writing in SPIFFS!");
            return;
        }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (uploadFile) uploadFile.write(upload.buf, upload.currentSize);
    } else if (upload.status == UPLOAD_FILE_END) {
        if (uploadFile) {
            uploadFile.close();
            Serial.printf("[Upload] Success: Saved %u bytes.\n", upload.totalSize);
        }
    }
}

// -----------------------------------------------------------------
// HỎI SLOT HIỆN TẠI (Đợi vô hạn) 
// -----------------------------------------------------------------
char getSTM32CurrentSlot() {
    Serial.println("[OTA] Requesting current running Slot from STM32. Waiting continuously...");
    uint8_t data[1] = {0x01};
    uint16_t crc = simpleCRC(data, 1);
    
    uint8_t askPacket[6];
    askPacket[0] = START_BYTE;
    askPacket[1] = 0x00;
    askPacket[2] = 0x01;
    askPacket[3] = 0x01;
    askPacket[4] = (crc >> 8) & 0xFF;
    askPacket[5] = crc & 0xFF;

    while(Serial1.available()) Serial1.read();
    Serial1.write(askPacket, 6);

    while (true) {
        if (Serial1.available()) {
            char resp = Serial1.read();
            if (resp == 'A' || resp == 'B') {
                Serial.printf("[OTA] Received response: Currently running Slot %c\n", resp);
                return resp;
            }
        }
        delay(10); // Ngăn lỗi WDT
    }
    return 0; 
}

// -----------------------------------------------------------------
// GỬI METADATA (Slot + Version) [cite: 120]
// -----------------------------------------------------------------
bool sendMetadataToSTM32(char targetSlot, uint8_t version) {
    Serial.printf("[OTA] Sending Metadata -> Target Slot: %c | Version: %d\n", targetSlot, version);
    uint8_t data[2] = {(uint8_t)targetSlot, version};
    uint16_t crc = simpleCRC(data, 2);
    
    uint8_t packet[7];
    packet[0] = START_BYTE;
    packet[1] = 0x00;
    packet[2] = 0x02;
    packet[3] = data[0];
    packet[4] = data[1];
    packet[5] = (crc >> 8) & 0xFF;
    packet[6] = crc & 0xFF;

    int retry = 3;
    while(retry--) {
        Serial1.write(packet, 7);
        if (waitForAck(1000)) {
            Serial.println("[OTA] Metadata sent successfully (ACK received).");
            return true;
        }
        Serial.println("[OTA] Metadata NACK/Timeout. Retrying...");
    }
    return false;
}

// -----------------------------------------------------------------
// GỬI FILE & LOGIC MÔ PHỎNG LỖI 50%
// -----------------------------------------------------------------
void sendFirmwareFile(String filename) {
    File file = SPIFFS.open(filename, FILE_READ);
    if (!file) {
        Serial.printf("[Error] Cannot open file %s\n", filename.c_str());
        return;
    }

    size_t total_size = file.size();
    int total_blocks = (total_size + BLOCK_SIZE - 1) / BLOCK_SIZE;

    Serial.printf("[OTA] Starting firmware transfer for %s (%d blocks)...\n", filename.c_str(), total_blocks);
    uint8_t data[BLOCK_SIZE];
    uint8_t packet[BLOCK_SIZE + 5];
    int blockCount = 0;

    while (file.available()) {
        // === LOGIC MÔ PHỎNG LỖI (TEARING) Ở 50% ===
        if (simulate_fail && blockCount >= (total_blocks / 2)) {
            Serial.println("\n[SIMULATE FAIL] Reached 50%. Pulling the plug!");
            file.close();
            return; // Thoát ngang đột ngột, không gửi gói Len = 0
        }
        // ==========================================

        int len = file.read(data, BLOCK_SIZE);
        uint16_t crc = simpleCRC(data, len);

        packet[0] = START_BYTE;
        packet[1] = (len >> 8) & 0xFF;
        packet[2] = len & 0xFF;
        memcpy(&packet[3], data, len);
        packet[3 + len] = (crc >> 8) & 0xFF;
        packet[4 + len] = crc & 0xFF;

        int retry = 3;
        bool acked = false;
        while (retry--) {
            Serial1.write(packet, len + 5);
            if (waitForAck(1000)) {
                Serial.printf(" -> Block %d: OK (ACK)\n", blockCount);
                acked = true;
                break;
            } else {
                Serial.printf(" -> Block %d: ERROR (NACK/Timeout). Retrying...\n", blockCount);
            }
        }

        if (!acked) {
            Serial.println("[Error] Too many failed attempts. Aborting OTA.");
            file.close();
            return;
        }
        blockCount++;
        delay(10);
    }

    // Gói kết thúc (length = 0)
    packet[0] = START_BYTE;
    packet[1] = 0x00;
    packet[2] = 0x00;
    Serial1.write(packet, 3);
    file.close();
    Serial.println("[OTA] Successfully finished sending Firmware to STM32!");
}

// -----------------------------------------------------------------
// LUỒNG OTA TỔNG [cite: 119, 120, 121]
// -----------------------------------------------------------------
void startOTAProcess() {
    char currentSlot = getSTM32CurrentSlot();
    char targetSlot = (currentSlot == 'A') ? 'B' : 'A';
    String targetFile = (targetSlot == 'A') ? "/firm_slot_A.bin" : "/firm_slot_B.bin";

    Serial.printf("\n[Decision] STM32 is on Slot %c -> Writing to Slot %c. Using internal file: %s\n", currentSlot, targetSlot, targetFile.c_str());

    if (!SPIFFS.exists(targetFile)) {
        Serial.printf("[Error] File %s has not been uploaded to ESP32 yet.\n", targetFile.c_str());
        return;
    }

    if (!sendMetadataToSTM32(targetSlot, ota_version)) {
        Serial.println("[Error] STM32 rejected Metadata packet. Aborting.");
        return;
    }

    sendFirmwareFile(targetFile);
}

void setup() {
    Serial.begin(115200);
    Serial1.begin(STM_BAUD, SERIAL_8N1, STM_RXPIN, STM_TXPIN);

    if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS Mount Failed");
        return;
    }

    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi...");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected. IP:");
    Serial.println(WiFi.localIP());

    // Giao diện AJAX xịn xò 
    server.on("/", HTTP_GET, []() {
        server.send(200, "text/html", R"rawliteral(
        <!DOCTYPE html>
        <html>
        <head>
          <meta charset="utf-8">
          <title>OTA Dashboard</title>
          <style>
            body { background-color: #f0f0f0; font-family: Arial; text-align: center; padding-top: 30px; }
            .container { background: #fff; padding: 20px; border-radius: 10px; display: inline-block; box-shadow: 0 0 10px rgba(0,0,0,0.1); text-align: left; margin-bottom: 20px; width: 350px; }
            input[type="file"], input[type="number"] { margin-bottom: 15px; width: 100%; box-sizing: border-box;}
            button { background-color: #4CAF50; color: white; padding: 10px 20px; border: none; border-radius: 5px; cursor: pointer; width: 100%; font-weight: bold; margin-top: 10px;}
            button.ota-btn { background-color: #008CBA; }
            button.fail-btn { background-color: #f44336; }
            h2 { text-align: center; margin-top: 0;}
            hr { border: 0; height: 1px; background: #ccc; margin: 20px 0; }
            #status { margin-top: 15px; font-weight: bold; text-align: center; color: #333; }
          </style>
        </head>
        <body>
          <div class="container">
            <h2>1. Upload Binaries</h2>
            <form id="uploadForm">
              <label>Select Firmware for Slot A:</label>
              <input type="file" name="fileA" accept=".bin"><br>
              <label>Select Firmware for Slot B:</label>
              <input type="file" name="fileB" accept=".bin"><br>
              <button type="button" onclick="uploadFiles()">Upload Files to ESP32</button>
            </form>

            <hr>

            <h2>2. Start STM32 OTA</h2>
            <label>Firmware Version (Integer):</label>
            <input type="number" id="version" value="1" min="1"><br>
            <button type="button" class="ota-btn" onclick="startOTA('/send')">Start Normal OTA</button>
            <button type="button" class="fail-btn" onclick="startOTA('/send_fail')">Test Simulate Fail (50%)</button>

            <div id="status">Ready.</div>
          </div>

          <script>
            function uploadFiles() {
              let form = document.getElementById('uploadForm');
              let formData = new FormData(form);
              document.getElementById('status').innerText = "Uploading... Please wait.";
              document.getElementById('status').style.color = "#ff9800";

              fetch('/upload', { method: 'POST', body: formData })
                .then(response => response.text())
                .then(data => {
                  document.getElementById('status').innerText = "Upload successful!";
                  document.getElementById('status').style.color = "#4CAF50";
                })
                .catch(error => {
                  document.getElementById('status').innerText = "Upload failed!";
                  document.getElementById('status').style.color = "red";
                });
            }

            function startOTA(endpoint) {
              let ver = document.getElementById('version').value;
              document.getElementById('status').innerText = "OTA Process started... Check Serial Monitor.";
              document.getElementById('status').style.color = "#008CBA";

              fetch(endpoint + '?version=' + ver)
                .then(response => response.text())
                .then(data => console.log(data))
                .catch(error => console.error(error));
            }
          </script>
        </body>
        </html>
        )rawliteral");
    });

    // Endpoint xử lý Upload ngầm
    server.on("/upload", HTTP_POST, []() {
        server.send(200, "text/plain", "OK");
    }, handleUpload);

    // Kích hoạt Nạp chuẩn
    server.on("/send", HTTP_GET, []() {
        if (server.hasArg("version")) ota_version = server.arg("version").toInt();
        simulate_fail = false;
        server.send(200, "text/plain", "Starting Normal OTA");
        startOTAProcess();
    });

    // Kích hoạt Nạp mô phỏng lỗi
    server.on("/send_fail", HTTP_GET, []() {
        if (server.hasArg("version")) ota_version = server.arg("version").toInt();
        simulate_fail = true;
        server.send(200, "text/plain", "Starting Simulate Fail OTA");
        startOTAProcess();
    });

    server.begin();
    Serial.println("Web Server initialized.");
}

void loop() {
    server.handleClient();
}