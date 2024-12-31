/*
* 支持巴法app配网，长按按键可重新配网
*/
#include <WiFi.h>
#include <HTTPClient.h>
#include <EEPROM.h>
#include <Ticker.h>
#include <ArduinoJson.h>
#include <AceButton.h>
#include <HTTPUpdate.h>
using namespace ace_button;
WiFiClient client_bemfa_WiFiClient;
HTTPClient http_bemfa_HTTPClient;
//巴法云服务器地址默认即可
#define TCP_SERVER_ADDR "bemfa.com"
//服务器端口，tcp创客云端口8344
#define TCP_SERVER_PORT "8344"



//*****可以修改的地方******//
String aptype = "002";                                  //001插座类型，002灯类型，003风扇类型，004传感器，005空调，006开关，009窗帘
String Name= "台灯";                                    //设备昵称，可随意修改
String verSion = "3.1";                                 //3.1是tcp协议,1.1是mqtt协议,
String room = "";     //房间。例如客厅、卧室等，默认空
int adminID = 0;     //默认空即可。企业id,建议企业用户配置，该设备会自动绑定到该企业下，获取id方法见接入文档5.17节
int protoType = 3;   //3是tcp设备端口8344,1是MQTT设备
const int LED_Pin = 14;                                 //单片机LED引脚值，GPIO0引脚，其他开发板，修改为自己的引脚，例如NodeMCU开发板修改为D4
const int LedBlink = 22;                                //指示灯引脚，可自行修改，如果没有指示灯，建议删除指示灯相关代码
const int buttonPin = 0;                               //定义按钮引脚
int failCount = 0;                                      //定义失败连接次数
bool ledState = true;                                   //led 状态
String upUrl = "http://bin.bemfa.com/b/xxxxxxxxx.bin";  //OTA固件链接，请替换为自己的固件链接，如果接收到msg=update，开始执行固件升级
//**********************//


//检测是否是第一次连接WIFI
bool firstWIfiConfig = false;
String topic = "";

//最大字节数
#define MAX_PACKETSIZE 512
//设置心跳值30s
#define KEEPALIVEATIME 60 * 1000



//tcp客户端相关初始化，默认即可
WiFiClient TCPclient;
String TcpClient_Buff = "";
unsigned int TcpClient_BuffIndex = 0;
unsigned long TcpClient_preTick = 0;
unsigned long preHeartTick = 0;     //心跳
unsigned long preTCPStartTick = 0;  //连接
bool preTCPConnected = false;


//相关函数初始化
//连接WIFI
void doWiFiTick();
void startSTA();

//TCP初始化连接
void doTCPClientTick();
void startTCPClient();
void sendtoTCPServer(String p);

//led 控制函数
void turnOnLed();
void turnOffLed();







int httpCode = 0;
String UID = "";
String TOPIC = "";
#define HOST_NAME "bemfa"
char config_flag = 0;
#define MAGIC_NUMBER 0xAA

/**
* 结构体，用于存储配网信息
*/
struct config_type {
  char stassid[32];
  char stapsw[64];
  char cuid[40];
  char ctopic[32];
  uint8_t reboot;
  uint8_t magic;
};
config_type config;



char packetBuffer[255]; //发送数据包
WiFiUDP Udp;


void saveConfig();
void initWiFi();
void loadConfig();
void restoreFactory();
void waitKey();
void checkFirstConfig();
void apConfig();

//当升级开始时，打印日志
void update_started() {
  Serial.println("CALLBACK:  HTTP update process started");
}

//当升级结束时，打印日志
void update_finished() {
  Serial.println("CALLBACK:  HTTP update process finished");
}

//当升级中，打印日志
void update_progress(int cur, int total) {
  Serial.printf("CALLBACK:  HTTP update process at %d of %d bytes...\n", cur, total);
}

//当升级失败时，打印日志
void update_error(int err) {
  Serial.printf("CALLBACK:  HTTP update fatal error code %d\n", err);
}



/**
 * 固件升级函数
 * 在需要升级的地方，加上这个函数即可，例如setup中加的updateBin(); 
 * 原理：通过http请求获取远程固件，实现升级
 */
void updateBin() {
  Serial.println("start update");
  WiFiClient UpdateClient;

  httpUpdate.onStart(update_started);      //当升级开始时
  httpUpdate.onEnd(update_finished);       //当升级结束时
  httpUpdate.onProgress(update_progress);  //当升级中
  httpUpdate.onError(update_error);        //当升级失败时

  t_httpUpdate_return ret = httpUpdate.update(UpdateClient, upUrl);
  switch (ret) {
    case HTTP_UPDATE_FAILED:  //当升级失败
      Serial.println("[update] Update failed.");
      break;
    case HTTP_UPDATE_NO_UPDATES:  //当无升级
      Serial.println("[update] Update no Update.");
      break;
    case HTTP_UPDATE_OK:  //当升级成功
      Serial.println("[update] Update ok.");
      break;
  }
}


//按钮配置接口
AceButton ledButton(buttonPin);
//按键处理程序
void handleEvent(AceButton* button, uint8_t eventType,
                 uint8_t) {

  switch (eventType) {
    //当短按时
    case AceButton::kEventReleased:
      Serial.println(F("Button: Pressed"));

      ledState = !ledState;             //改变led状态
      digitalWrite(LED_Pin, ledState);  //写入状态
      if (ledState == true) {
        Serial.println("low press off");
        sendtoTCPServer("cmd=2&uid=" + UID + "&topic=" + TOPIC + "/up&msg=off\r\n");  //推送消息
      } else {
        Serial.println("low press on");
        sendtoTCPServer("cmd=2&uid=" + UID + "&topic=" + TOPIC + "/up&msg=on\r\n");  //推送消息
      }

      break;
    //当长按时
    case AceButton::kEventLongPressed:
      Serial.println(F("Button: Long Pressed"));

      Serial.println("Restore Factory....... ");
      config.magic = 0x00;
      config.reboot = 0;
      strcpy(config.stassid, "");
      strcpy(config.stapsw, "");
      strcpy(config.cuid, "");
      strcpy(config.ctopic, "");
      saveConfig();
      config_flag = 1;
      apConfig(TOPIC);
      // doSmartconfig();
      int num = 0;
      while (WiFi.status() != WL_CONNECTED && num < 120) {  //检查是否连接成功
        delay(500);
        num = num + 1;
        Serial.print(".");
      }
      checkFirstConfig();
      // getUid(topicMac, true);
      break;
  }
}


static unsigned long buttonLastMillis = 0;  //时间戳，用于计算防抖
void IRAM_ATTR checkSwitch() {
  unsigned long newMillis = millis();  //获取当前时间戳

  if (newMillis - buttonLastMillis > 30) {  //检测短按，是否大于30ms
    Serial.println("low press !!!!!!!!");
    ledState = !ledState;             //改变led状态
    digitalWrite(LED_Pin, ledState);  //写入状态
  }
  buttonLastMillis = newMillis;  //重新计算防抖动
}




/**
* 存储配网信息
*/
void saveConfig() {
  int rand_key;
  uint8_t mac[6];
  WiFi.macAddress(mac);
  config.reboot = 0;
  EEPROM.begin(256);
  uint8_t* p = (uint8_t*)(&config);
  for (int i = 0; i < sizeof(config); i++) {
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

/**
* 初始化wifi信息，并连接路由器网络
*/
void initWiFi() {

  if (WiFi.status() != WL_CONNECTED) {
    WiFi.disconnect();                          //断开连接
    WiFi.mode(WIFI_STA);                        //STA模式
    WiFi.begin(config.stassid, config.stapsw);  //连接路由器
  }
  int num = 0;
  while (WiFi.status() != WL_CONNECTED && num < 120) {  //检查是否连接成功
    delay(500);
    num = num + 1;
    Serial.print(".");
  }
  Serial.println("wifi config ok");
}

/**
* 加载存储的信息，并检查是否进行了反复5次重启恢复出厂信息
*/
uint8_t* p = (uint8_t*)(&config);
void loadConfig() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  EEPROM.begin(256);
  for (int i = 0; i < sizeof(config); i++) {
    *(p + i) = EEPROM.read(i);
  }
  config.reboot = config.reboot + 1;
  if (config.reboot >= 4) {
    restoreFactory();
  }
  if (config.magic != 0xAA) {
    config_flag = 1;
  }
  EEPROM.begin(256);
  for (int i = 0; i < sizeof(config); i++) {
    EEPROM.write(i, *(p + i));
  }
  EEPROM.commit();
  delay(2000);
  EEPROM.begin(256);
  config.reboot = 0;
  for (int i = 0; i < sizeof(config); i++) {
    EEPROM.write(i, *(p + i));
  }
  EEPROM.commit();
  delay(2000);
}
/**
* 恢复出厂设置，清除存储的wifi信息
*/
void restoreFactory() {
  Serial.println("Restore Factory....... ");
  config.magic = 0x00;
  strcpy(config.stassid, "");
  strcpy(config.stapsw, "");
  strcpy(config.cuid, "");
  strcpy(config.ctopic, "");
  saveConfig();
  delayRestart(1);
  while (1) {
    delay(100);
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


void apConfig(String mac){
  if(config_flag == 1){
      WiFi.softAP("bemfa_"+mac);
      Udp.begin(8266);
      Serial.println("Started Ap Config...");
  }
  topic = mac;
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


/**
* 检查是否需要airkiss配网
*/
void waitKey() {
  if (config_flag == 1) {
    apConfig(TOPIC);//加载ap
  }
}

/*
  *发送数据到TCP服务器
 */
void sendtoTCPServer(String p) {

  if (!TCPclient.connected()) {
    Serial.println("Client is not readly");
    return;
  }
  TCPclient.print(p);
  Serial.println("[Send to TCPServer]:String");
  Serial.println(p);
  preHeartTick = millis();  //心跳计时开始，需要每隔60秒发送一次数据
}



/*
  *初始化和服务器建立连接
*/
void startTCPClient() {
  if (TCPclient.connect(TCP_SERVER_ADDR, atoi(TCP_SERVER_PORT))) {
    Serial.print("\nConnected to server:");
    Serial.printf("%s:%d\r\n", TCP_SERVER_ADDR, atoi(TCP_SERVER_PORT));

    String tcpTemp = "";                                        //初始化字符串
    tcpTemp = "cmd=1&uid=" + UID + "&topic=" + TOPIC + "\r\n";  //构建订阅指令
    sendtoTCPServer(tcpTemp);                                   //发送订阅指令
    tcpTemp = "";                                               //清空

    preTCPConnected = true;
    TCPclient.setNoDelay(true);
    failCount = 0;
  } else {
    failCount = failCount+1;
    if(failCount>2){ //如果失败连接3次，重启系统
       delayRestart(0);
    }
    Serial.print("Failed connected to server:");
    Serial.println(TCP_SERVER_ADDR);
    TCPclient.stop();
    preTCPConnected = false;
  }
  preTCPStartTick = millis();
}


/*
  *检查数据，发送心跳
*/
void doTCPClientTick() {
  //检查是否断开，断开后重连
  if (WiFi.status() != WL_CONNECTED) return;

  if (!TCPclient.connected()) {  //断开重连

    if (preTCPConnected == true) {

      preTCPConnected = false;
      preTCPStartTick = millis();
      Serial.println();
      Serial.println("TCP Client disconnected.");
      TCPclient.stop();
    } else if (millis() - preTCPStartTick > 1 * 1000)  //重新连接
      TCPclient.stop();
      startTCPClient();
  } else {
    if (TCPclient.available()) {  //收数据
      char c = TCPclient.read();
      TcpClient_Buff += c;
      TcpClient_BuffIndex++;
      TcpClient_preTick = millis();

      if (TcpClient_BuffIndex >= MAX_PACKETSIZE - 1) {
        TcpClient_BuffIndex = MAX_PACKETSIZE - 2;
        TcpClient_preTick = TcpClient_preTick - 200;
      }
    }
    if (millis() - preHeartTick >= KEEPALIVEATIME) {  //保持心跳
      Serial.println("--Keep alive:");
      sendtoTCPServer("cmd=0&msg=keep\r\n");
    }
  }
  if ((TcpClient_Buff.length() >= 1) && (millis() - TcpClient_preTick >= 200)) {  //data ready
    TCPclient.flush();
    Serial.print("Rev string: ");
    TcpClient_Buff.trim();           //去掉首位空格
    Serial.println(TcpClient_Buff);  //打印接收到的消息
    String getTopic = "";
    String getMsg = "";
    if (TcpClient_Buff.length() > 15) {  //注意TcpClient_Buff只是个字符串，在上面开头做了初始化 String TcpClient_Buff = "";
      //此时会收到推送的指令，指令大概为 cmd=2&uid=xxx&topic=light002&msg=off
      int topicIndex = TcpClient_Buff.indexOf("&topic=") + 7;     //c语言字符串查找，查找&topic=位置，并移动7位，不懂的可百度c语言字符串查找
      int msgIndex = TcpClient_Buff.indexOf("&msg=");             //c语言字符串查找，查找&msg=位置
      getTopic = TcpClient_Buff.substring(topicIndex, msgIndex);  //c语言字符串截取，截取到topic,不懂的可百度c语言字符串截取
      getMsg = TcpClient_Buff.substring(msgIndex + 5);            //c语言字符串截取，截取到消息
      Serial.print("topic:------");
      Serial.println(getTopic);  //打印截取到的主题值
      Serial.print("msg:--------");
      Serial.println(getMsg);  //打印截取到的消息值
    }
    if (getMsg == "on") {          //如果是消息==打开
      turnOnLed();                 //打开灯
      ledState = false;            //改变记录的状态
    } else if (getMsg == "off") {  //如果是消息==关闭
      turnOffLed();                //关闭灯
      ledState = true;             //改变记录的状态
    } else if (getMsg == "update") {
      Serial.println("[update] Update Start......");
      updateBin();
    }

    TcpClient_Buff = "";
    TcpClient_BuffIndex = 0;
  }
}

void startSTA() {
  WiFi.disconnect();                          //断开连接
  WiFi.mode(WIFI_STA);                        //STA模式
  WiFi.begin(config.stassid, config.stapsw);  //连接路由器
}



/**************************************************************************
                                 WIFI
***************************************************************************/
/*
  WiFiTick
  检查是否需要初始化WiFi
  检查WiFi是否连接上，若连接成功启动TCP Client
  控制指示灯
*/
void doWiFiTick() {
  static bool startSTAFlag = false;
  static bool taskStarted = false;
  static uint32_t lastWiFiCheckTick = 0;

  if (!startSTAFlag) {
    startSTAFlag = true;
    startSTA();
    Serial.printf("Heap size:%d\r\n", ESP.getFreeHeap());
  }

  //未连接1s重连
  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - lastWiFiCheckTick > 1000) {
      lastWiFiCheckTick = millis();
    }
     taskStarted = false;
  }
  //连接成功建立
  else {
    if (taskStarted == false) {
      taskStarted = true;
      Serial.print("\r\nGet IP Address: ");
      Serial.println(WiFi.localIP());
      startTCPClient();
    }
  }
}
//打开灯泡
void turnOnLed() {
  Serial.println("Turn ON");
  digitalWrite(LED_Pin, LOW);
}
//关闭灯泡
void turnOffLed() {
  Serial.println("Turn OFF");
  digitalWrite(LED_Pin, HIGH);
}


// 初始化，相当于main 函数
void setup() {
  Serial.begin(115200);
  pinMode(buttonPin, INPUT_PULLUP);                                        // 设置led引脚为输入引脚
  attachInterrupt(digitalPinToInterrupt(buttonPin), checkSwitch, RISING);  //设置中断
  pinMode(LED_Pin, OUTPUT);
  digitalWrite(LED_Pin, ledState);  //写入默认状态
  pinMode(LedBlink, OUTPUT);
  digitalWrite(LedBlink, LOW);  //指示灯引脚
  Serial.println("Beginning...");

  TOPIC = Network.macAddress().substring(8);  //取mac地址做主题用
  TOPIC.replace(":", "");                  //去掉:号
  TOPIC = TOPIC + aptype;
  loadConfig();
  waitKey();

  initWiFi();
  // getUid(topicMac, false);
  checkFirstConfig();
  UID = config.cuid; //赋值UID
  //按键配置
  ButtonConfig* buttonConfig = ButtonConfig::getSystemButtonConfig();
  buttonConfig->setEventHandler(handleEvent);
  buttonConfig->setFeature(ButtonConfig::kFeatureLongPress);
  buttonConfig->setFeature(ButtonConfig::kFeatureRepeatPress);
  buttonConfig->setFeature(ButtonConfig::kFeatureSuppressAfterLongPress);
  buttonConfig->setLongPressDelay(5000);  //长按时间5秒

  detachInterrupt(digitalPinToInterrupt(buttonPin));  //删除外部中断
  digitalWrite(LedBlink, LOW);                        //指示灯引脚
}

//循环
void loop() {
  doWiFiTick();       //检查wifi
  doTCPClientTick();  //tcp消息接收
  ledButton.check();  //按键检查
}
