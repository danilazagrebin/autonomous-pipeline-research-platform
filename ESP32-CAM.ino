// Машинка на ESP32-CAM 

const char* ssid = "Car";
const char* password = "123456789";

#include "esp_wifi.h"
#include "esp_camera.h"
#include <WiFi.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

#include "FS.h"
#include "SD_MMC.h"

#define CAMERA_MODEL_AI_THINKER

#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

void startCameraServer();

// const int MotPin0 = 12; // Правый мотор
// const int MotPin1 = 13; // Правый мотор
// const int MotPin2 = 14; // Левый мотор
// const int MotPin3 = 15; // Левый мотор

bool savePhotosToSD = false;  // Флаг для сохранения фото
String saveSessionFolder = ""; // Папка для сохранения
int photoCounter = 0;         // Счетчик фото
unsigned long lastPhotoTime = 0; // Время последнего фото
const int photoInterval = 200; // Интервал 200 мс

extern int speed;
extern int controlMode;
extern int new_command;
extern bool new_start;
extern float set_distance;

void initSDCard() {
  Serial.println("Инициализация SD-карты...");
  
  if(!SD_MMC.begin()){
    Serial.println("Ошибка монтирования SD-карты");
    return;
  }

  uint8_t cardType = SD_MMC.cardType();
  if(cardType == CARD_NONE){
    Serial.println("SD-карта не подключена");
    return;
  }

  Serial.print("Тип SD-карты: ");
  if(cardType == CARD_MMC){
    Serial.println("MMC");
  } else if(cardType == CARD_SD){
    Serial.println("SDSC");
  } else if(cardType == CARD_SDHC){
    Serial.println("SDHC");
  } else {
    Serial.println("Неизвестный");
  }

  // Создаем папку для сессии на основе времени
  saveSessionFolder = "/session_" + String(millis());
  if(SD_MMC.mkdir(saveSessionFolder)){
    Serial.printf("Создана папка для сохранения: %s\n", saveSessionFolder.c_str());
    
    // Создаем файл информации о сессии
    File infoFile = SD_MMC.open(saveSessionFolder + "/info.txt", FILE_WRITE);
    if(infoFile){
      infoFile.println("Сессия записи фото");
      infoFile.printf("Время создания: %lu мс\n", millis());
      infoFile.close();
    }
  }
}

// Функция для сохранения фото
void savePhotoToSD() {
  if (!savePhotosToSD) return;
  
  unsigned long currentTime = millis();
  if (currentTime - lastPhotoTime < photoInterval) return;
  
  camera_fb_t * fb = esp_camera_fb_get();
  if(!fb) {
    Serial.println("Не удалось захватить фото для сохранения");
    return;
  }

  photoCounter++;
  String filename = saveSessionFolder + "/photo_" + String(photoCounter) + ".jpg";
  
  File file = SD_MMC.open(filename, FILE_WRITE);
  if(file) {
    file.write(fb->buf, fb->len);
    file.close();
    Serial.printf("Сохранено: %s (%u байт)\n", filename.c_str(), fb->len);
  } else {
    Serial.println("Ошибка открытия файла для записи");
  }

  esp_camera_fb_return(fb);
  lastPhotoTime = currentTime;
}


void setup(){
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); 

  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  if (psramFound()){
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Инициализация камеры завершилась ошибкой 0x%x", err);
    return;
  }

  sensor_t * s = esp_camera_sensor_get();
  s->set_framesize(s, FRAMESIZE_QVGA);
  s->set_vflip(s, 0);
  s->set_hmirror(s, 0);

  //initMotors();
  //initServo();

  ledcSetup(7, 5000, 8);
  ledcAttachPin(4, 7);  // Пин 4 - светодиодная вспышка.
  ledcWrite(7, 0);  // ВАЖНО: Устанавливаем ШИМ в 0 (выключено)

  WiFi.softAP(ssid, password);
  IPAddress miIP = WiFi.softAPIP();
  Serial.print("AP IP адрес: ");
  Serial.println(miIP);   //пробар 192.168.4.1

  initSDCard();

  startCameraServer();

  for (int i = 0; i < 5; i++) {
    ledcWrite(7, 10); // Светодиодная вспышка
    delay(50);
    ledcWrite(7, 0);
    delay(50);
  }
  ledcWrite(7, 0);
 delay(100);
}

// Функция отправки данных в формате: speed,controlMode,new_command,new_start,set_distance
void sendDataToESP12() {
  String data = String(speed) + "," + 
                String(controlMode) + "," + 
                String(new_command) + "," + 
                String(new_start ? 1 : 0) + "," + 
                String(set_distance, 2); // 2 знака после запятой
  
  Serial.println(data);
}

void loop() {
  //delay(1000);
  //Serial.printf("RSSi: %ld dBm\n", WiFi.RSSI());
  // Отправка данных по UART каждые 100 мс
  static unsigned long lastSend = 0;
  if (millis() - lastSend >= 50) {
    sendDataToESP12();
    lastSend = millis();
  }
  savePhotoToSD();
}
