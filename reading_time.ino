//  the original code by Ted Meyers
//  posted here: https://groups.google.com/d/msg/diyrovers/lc7NUZYuJOg/ICPrYNJGBgAJ

//  if your tof have some problem, please see https://docs.m5stack.com/#/en/unit/tof 
#include <M5StickCPlus.h>
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <base64.h>
#include <mbedtls/sha256.h>
#include <UUID.h>
#include "time.h"
#include "secrets.h"

/* TOF */
#define VL53L0X_REG_IDENTIFICATION_MODEL_ID         0xc0
#define VL53L0X_REG_IDENTIFICATION_REVISION_ID      0xc2
#define VL53L0X_REG_PRE_RANGE_CONFIG_VCSEL_PERIOD   0x50
#define VL53L0X_REG_FINAL_RANGE_CONFIG_VCSEL_PERIOD 0x70
#define VL53L0X_REG_SYSRANGE_START                  0x00
#define VL53L0X_REG_RESULT_INTERRUPT_STATUS         0x13
#define VL53L0X_REG_RESULT_RANGE_STATUS             0x14
#define address 0x29


/* STATE MANAGEMENT */
const int LEAVING = 0;
const int AT_SEAT = 1;

int status = LEAVING;


const char* ssid = WIFI_SSID; // SSID
const char* password = WIFI_PASSWD; // PASSWORD
const char* switchbotToken = SWITCHBOT_TOKEN;
const char* switchbotSecret = SWITCHBOT_SECRET;
StaticJsonDocument<255> json_request;
char buffer[255];
const char *host = PAYMO_URL;

byte gbuf[16];

void setup() {
  //---osmar
  M5.begin();
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(30, 70);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setTextSize(2);
  
  //---osmar
  // put your setup code here, to run once:
  Wire.begin();        // join i2c bus (address optional for master)
  Serial.begin(115200);  // start serial for output
  Serial.println("VLX53LOX test started.");

      // wifi接続開始
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
      delay(500);
            
    Serial.print(".");
  }
  configTime(0, 0, "ntp.nict.jp");

  Serial.println("connected");
}


uint16_t count = 0;

void loop() {
  delay(1000);
  bool valid = false;
  uint16_t dist = get_distance(&valid); // センサーと対象物（身体）の距離（cm単位）
  if( status == LEAVING ){
        M5.Lcd.fillScreen(BLACK);
        M5.Axp.ScreenBreath( 7 );
  }else if (status == AT_SEAT){
        M5.Axp.ScreenBreath( 12 );
        M5.Lcd.fillScreen(RED);
        M5.Lcd.setCursor(30, 120);
        M5.Lcd.printf("%02d:%02d:%02d", count / 3600, (count % 3600) / 60, count % 60 );
  }

  if (!valid){
    if (status == AT_SEAT){
      count++;
    }
    return;
  }

  if( status == LEAVING ){
      if (dist < 300){
        status = AT_SEAT;
        Serial.println("STATUS: AT_SEAT");
        send_plug(true);
      }
  }else if (status == AT_SEAT){
    count++;
      if(dist > 300){
        status = LEAVING;
        if (count > 60){
          send_entry(count);
        }
        send_plug(false);
        count = 0;
        M5.Lcd.setCursor(30, 120);
        M5.Lcd.printf("%02d:%02d:%02d", count / 3600, (count % 3600) / 60, count % 60 );
        Serial.println("STATUS: LEAVING"); 
      }
  }
}

uint16_t get_distance(bool* pbool) {
  byte val1 = read_byte_data_at(VL53L0X_REG_IDENTIFICATION_REVISION_ID);
  //Serial.print("Revision ID: "); Serial.println(val1);

  val1 = read_byte_data_at(VL53L0X_REG_IDENTIFICATION_MODEL_ID);
  //Serial.print("Device ID: "); Serial.println(val1);

  val1 = read_byte_data_at(VL53L0X_REG_PRE_RANGE_CONFIG_VCSEL_PERIOD);
/*   Serial.print("PRE_RANGE_CONFIG_VCSEL_PERIOD="); Serial.println(val1); 
  Serial.print(" decode: "); Serial.println(VL53L0X_decode_vcsel_period(val1));
 */
  val1 = read_byte_data_at(VL53L0X_REG_FINAL_RANGE_CONFIG_VCSEL_PERIOD);
/*   Serial.print("FINAL_RANGE_CONFIG_VCSEL_PERIOD="); Serial.println(val1);
  Serial.print(" decode: "); Serial.println(VL53L0X_decode_vcsel_period(val1));

 */
  write_byte_data_at(VL53L0X_REG_SYSRANGE_START, 0x01);

  byte val = 0;
  int cnt = 0;
  while (cnt < 100) { // 1 second waiting time max
    delay(10);
    val = read_byte_data_at(VL53L0X_REG_RESULT_RANGE_STATUS);
    if (val & 0x01) break;
    cnt++;
  }
  //if (val & 0x01) Serial.println("ready"); else Serial.println("not ready");

  read_block_data_at(0x14, 12);
  uint16_t acnt = makeuint16(gbuf[7], gbuf[6]);
  uint16_t scnt = makeuint16(gbuf[9], gbuf[8]);
  uint16_t dist = makeuint16(gbuf[11], gbuf[10]);
  byte DeviceRangeStatusInternal = ((gbuf[0] & 0x78) >> 3);
  *pbool = DeviceRangeStatusInternal == 11;
/*   Serial.print("ambient count: "); Serial.println(acnt);
  Serial.print("signal count: ");  Serial.println(scnt);
  Serial.print("distance ");       Serial.println(dist);
 */
  // Serial.print("status: ");
  // Serial.println(DeviceRangeStatusInternal);
  
  return dist;
}

uint16_t bswap(byte b[]) {
  // Big Endian unsigned short to little endian unsigned short
  uint16_t val = ((b[0] << 8) & b[1]);
  return val;
}

uint16_t makeuint16(int lsb, int msb) {
    return ((msb & 0xFF) << 8) | (lsb & 0xFF);
}

void write_byte_data(byte data) {
  Wire.beginTransmission(address);
  Wire.write(data);
  Wire.endTransmission();
}

void write_byte_data_at(byte reg, byte data) {
  // write data word at address and register
  Wire.beginTransmission(address);
  Wire.write(reg);
  Wire.write(data);
  Wire.endTransmission();
}

void write_word_data_at(byte reg, uint16_t data) {
  // write data word at address and register
  byte b0 = (data &0xFF);
  byte b1 = ((data >> 8) && 0xFF);
    
  Wire.beginTransmission(address);
  Wire.write(reg);
  Wire.write(b0);
  Wire.write(b1);
  Wire.endTransmission();
}

byte read_byte_data() {
  Wire.requestFrom(address, 1);
  while (Wire.available() < 1) delay(1);
  byte b = Wire.read();
  return b;
}

byte read_byte_data_at(byte reg) {
  //write_byte_data((byte)0x00);
  write_byte_data(reg);
  Wire.requestFrom(address, 1);
  while (Wire.available() < 1) delay(1);
  byte b = Wire.read();
  return b;
}

uint16_t read_word_data_at(byte reg) {
  write_byte_data(reg);
  Wire.requestFrom(address, 2);
  while (Wire.available() < 2) delay(1);
  gbuf[0] = Wire.read();
  gbuf[1] = Wire.read();
  return bswap(gbuf); 
}

void read_block_data_at(byte reg, int sz) {
  int i = 0;
  write_byte_data(reg);
  Wire.requestFrom(address, sz);
  for (i=0; i<sz; i++) {
    while (Wire.available() < 1) delay(1);
    gbuf[i] = Wire.read();
  }
}


uint16_t VL53L0X_decode_vcsel_period(short vcsel_period_reg) {
  // Converts the encoded VCSEL period register value into the real
  // period in PLL clocks
  uint16_t vcsel_period_pclks = (vcsel_period_reg + 1) << 1;
  return vcsel_period_pclks;
}


void send_entry(uint16_t sec){
  Serial.printf("send start.");
  json_request["reading_time"] = sec;
  serializeJson(json_request, buffer, sizeof(buffer));
  HTTPClient http;
  http.begin(host);
  http.addHeader("Content-Type", "application/json");
  int status_code = http.POST((uint8_t*)buffer, strlen(buffer));
  Serial.printf("status_code=%d\r\n", status_code);
  http.end();
}

#define SHA256_SIZE 32

long hmac_sha256(const char *p_key, const char *p_payload, unsigned char *p_hmacResult)
{
  mbedtls_md_context_t ctx;
  mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 1);
  mbedtls_md_hmac_starts(&ctx, (const unsigned char*)p_key, strlen(p_key));
  mbedtls_md_hmac_update(&ctx, (const unsigned char*)p_payload, strlen(p_payload));
  mbedtls_md_hmac_finish(&ctx, p_hmacResult); // 32 bytes
  mbedtls_md_free(&ctx);

  return 0;
}



void send_plug(bool onOff){


  // set POST params
  StaticJsonDocument<255> json_request;
  char buffer[255];
  json_request["command"] = onOff ? "turnOn" : "turnOff";
  json_request["parameter"] = "default";
  json_request["commandType"] = "command";
  serializeJson(json_request, buffer, sizeof(buffer));

  // calc auth headers
  time_t _time;
  time(&_time);
  String t =  String(_time) + String("000");
  UUID uuid;
  String nonce = String("19e5c091a49d4f308ce678942f505247");
  Serial.print(String("t=") + t + String("\r\n"));

  String toSign = String(switchbotToken) + t + nonce;
  Serial.printf("toSign=%s\r\n", toSign.c_str());

  unsigned char digest[SHA256_SIZE];
  hmac_sha256(switchbotSecret, toSign.c_str(), digest);

  String sign = base64::encode(digest, SHA256_SIZE);
  Serial.printf("nonce=%s\r\n",nonce.c_str());
  Serial.printf("sign=%s\r\n",sign.c_str());

  HTTPClient http;
  http.begin(SWITCHBOT_URL);
  http.addHeader("Content-Type", "application/json; charset=utf8");
  http.addHeader("Authorization", switchbotToken);

  http.addHeader("t", t.c_str());
  http.addHeader("sign", sign.c_str());
  http.addHeader("nonce", nonce.c_str());

  int status_code = http.POST((uint8_t*)buffer, strlen(buffer));
  Serial.printf("status_code=%d\r\n", status_code);
  http.end();
}
