//需要在arduino IDE软件中---工具-->管理库-->搜索arduinojson并安装
//需要在arduino IDE软件中---工具-->管理库-->搜索arduinojson并安装
//需要在arduino IDE软件中---工具-->管理库-->搜索arduinojson并安装
//需要在arduino IDE软件中---工具-->管理库-->搜索arduinojson并安装
#include <WiFi.h>
#include <WiFiUDP.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <Ticker.h>
#include <HTTPClient.h>
//需要在arduino IDE软件中---工具-->管理库-->搜索arduinojson并安装

//根据需要修改的信息
String aptype = "002"; //设备类型，001插座设备，002灯类设备，003风扇设备,005空调，006开关，009窗帘
String Name= "台灯"; //设备昵称，可随意修改
String verSion= "3.1"; //3是tcp设备端口8344,1是MQTT设备
String room = ""; //房间。例如客厅、卧室等，默认空
int protoType = 3;   //3是tcp设备端口8344,1是MQTT设备
int adminID = 0;     //默认空即可。企业id,建议企业用户配置，该设备会自动绑定到该企业下，获取id方法见接入文档5.17节
WiFiClient client_bemfa_WiFiClient;
HTTPClient http_bemfa_HTTPClient;

//检测是否是第一次连接WIFI
bool firstWIfiConfig = false;
String topic = "";
struct config_type
{
  char stassid[32];
  char stapsw[16];
  char cuid[40];
  char ctopic[32];
  uint8_t reboot;
  uint8_t magic;
};
config_type config;


char config_flag = 0;//判断是否配网
#define MAGIC_NUMBER 0xAA //判断是否配网
char packetBuffer[255]; //发送数据包
WiFiUDP Udp;


/*
 * 从EEPROM加载参数
*/
uint8_t *p = (uint8_t*)(&config);
void loadConfig()
{

  uint8_t mac[6];
  Serial.println(" LoadConfig.......");
  WiFi.macAddress(mac);
  EEPROM.begin(512);
  for (int i = 0; i < sizeof(config); i++)
  {
    *(p + i) = EEPROM.read(i);
  }
  config.reboot = config.reboot + 1;
  if(config.reboot>=4){
    restoreFactory();
  }
  if(config.magic != 0xAA){
    config_flag = 1;
  }
  EEPROM.begin(512);
  for (int i = 0; i < sizeof(config); i++){
    EEPROM.write(i, *(p + i));
  }
  EEPROM.commit();
  delay(2000);
  Serial.println("loadConfig Over");
  EEPROM.begin(512);
  config.reboot = 0;
  for (int i = 0; i < sizeof(config); i++){
    EEPROM.write(i, *(p + i));
  }
  EEPROM.commit();
}


/* 
 * 恢复出厂设置
*/
void restoreFactory()
{
  Serial.println("\r\n Restore Factory....... ");
  config.magic = 0x00;
  strcpy(config.stassid, "");
  strcpy(config.stapsw, "");
  strcpy(config.cuid, "");
  strcpy(config.ctopic, "");
  config.magic = 0x00;
  saveConfig();
  delayRestart(1);
  while (1) {
    delay(100);
  }
}

/*
保存WIFI信息
*/
void saveConfig()
{
  config.reboot = 0;
  EEPROM.begin(2018);
  uint8_t *p = (uint8_t*)(&config);
  for (int i = 0; i < sizeof(config); i++)
  {
    EEPROM.write(i, *(p + i));
  }
  EEPROM.commit();
}
Ticker delayTimer;
void delayRestart(float t) {
  delayTimer.attach(t, []() {
    ESP.restart();
  });
}
void apConfig(String mac){
  if(config_flag == 1){
      WiFi.softAP("bemfa_"+mac);
      Udp.begin(8266);
      Serial.println("Started Ap Config...");
  }
  topic = mac+aptype;
  while(config_flag){//如果未配网，开启AP配网，并接收配网信息
        int packetSize = Udp.parsePacket();
        if (packetSize) {
          Serial.print("Received packet of size ");
          Serial.println(packetSize);
          Serial.print("From ");
          IPAddress remoteIp = Udp.remoteIP();
          Serial.print(remoteIp);
          Serial.print(", port ");
          Serial.println(Udp.remotePort());
      

          int len = Udp.read(packetBuffer, 255);
          if (len > 0) {
            packetBuffer[len] = 0;
          }
          Serial.println("Contents:");
          Serial.println(packetBuffer);
          StaticJsonDocument<200> doc;
      
          DeserializationError error = deserializeJson(doc, packetBuffer);
          if (error) {
            Serial.print(F("deserializeJson() failed: "));
            Serial.println(error.f_str());
            return;
          }
          int cmdType = doc["cmdType"].as<int>();;
  
          if (cmdType == 1) {
              const char* ssid = doc["ssid"];
              const char* password = doc["password"];
              const char* token = doc["token"];
              strcpy(config.stassid, ssid);
              strcpy(config.stapsw, password);
              strcpy(config.cuid, token);
              //收到信息，并回复
              String  ReplyBuffer = "{\"cmdType\":2,\"productId\":\""+topic+"\",\"deviceName\":\""+Name+"\",\"protoVersion\":\""+verSion+"\"}";

              const char* replyBufferData = ReplyBuffer.c_str();
              size_t replyBufferLength = ReplyBuffer.length();

              Udp.beginPacket(Udp.remoteIP(), Udp.remotePort());
              Udp.write((const uint8_t*)replyBufferData, replyBufferLength);
              Udp.endPacket();
            
          }else if(cmdType == 3){
              config_flag = 0;
              firstWIfiConfig = true;
              WiFi.softAPdisconnect(true);
          }
          
        } 
  }
}

/*
  第一次配网检查WIFI,保存WIFI配置信息,并创建主题
*/
void checkFirstConfig()
{

  if(firstWIfiConfig){   
    // 设置目标 URL
    http_bemfa_HTTPClient.begin(client_bemfa_WiFiClient,"http://pro.bemfa.com/vs/web/v1/deviceAddTopic");

    // 创建 JSON 对象
    StaticJsonDocument<200> jsonDoc;
    jsonDoc["uid"] = config.cuid;
    jsonDoc["name"] = Name;
    jsonDoc["topic"] = topic;
    jsonDoc["type"] = protoType;
    jsonDoc["room"] = room;
    jsonDoc["adminID"] = adminID;
    jsonDoc["wifiConfig"] = 1; //必填字段

    // 将 JSON 对象转换为字符串
    String jsonString;
    serializeJson(jsonDoc, jsonString);
    http_bemfa_HTTPClient.addHeader("Content-Type", "application/json; charset=UTF-8");       
    // 发送请求
    int httpCode = http_bemfa_HTTPClient.POST(jsonString);
    if (httpCode == 200) {
      Serial.println("POST succeeded with code:");
      Serial.println(httpCode);
      String payload = http_bemfa_HTTPClient.getString();
      Serial.println(payload);

      //json数据解析
      StaticJsonDocument<200> doc;
      DeserializationError error = deserializeJson(doc, payload);
      if (error) {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.c_str());
      }

      int code = doc["code"];
      if (code == 0) {
        int resCode = doc["data"]["code"];
        if(resCode == 40006 || resCode ==0 ){
          String docUID = doc["uid"];
          Serial.print("create topic ok:");
          Serial.println(topic);
          if(firstWIfiConfig){     
            config.reboot = 0;
            config.magic = 0xAA;
            saveConfig();
          }
        }else{
              Serial.println(" config ERROR.........");
        }
      } else {
        Serial.println(" config ERROR.........");
      }
    } else if (httpCode != 200) {
      Serial.println("POST failed with code:");
      Serial.println(httpCode);
    } else {
      Serial.println("Unknown error");
    }

    http_bemfa_HTTPClient.end();
      }
   

}

// 复位或上电后运行一次:
void setup() {
  //在这里加入初始化相关代码，只运行一次:
  Serial.begin(115200);
  String topic = Network.macAddress().substring(8);//取mac地址做主题用
  topic.replace(":", "");//去掉:号
  loadConfig();//加载存储的数据
  apConfig(topic);//加载ap

  WiFi.disconnect();//断开连接
  WiFi.mode(WIFI_STA);//STA模式
  WiFi.begin(config.stassid, config.stapsw);//连接路由器
  while (WiFi.status() != WL_CONNECTED) {//检查是否连接成功
    delay(500);
    Serial.print(".");
  }
  checkFirstConfig();//保存WIFI配置信息,并创建主题
}

//一直循环执行:
void loop() {
 Serial.println("Config success");
 delay(1000);
}