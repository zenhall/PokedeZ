#include <WiFi.h>
#include <ArduinoGPTChat.h>
#include "Audio.h"
#include "ESP_I2S.h"
#include <TFT_eSPI.h>
#include "simfang16.h"
#include <WebServer.h>
#include <SPIFFS.h>

// 定义I2S引脚（用于音频输出）
#define I2S_DOUT D2
#define I2S_BCLK D1
#define I2S_LRC D0

// 定义麦克风输入引脚
#define MIC_DATA_PIN 42
#define MIC_CLOCK_PIN 41

// 定义触摸按键引脚
#define TOUCH_PIN D7

// 定义录音时长（秒）
#define RECORDING_DURATION  8

// WiFi设置
const char* ssid     = "2nd-curv";  
const char* password = "xbotpark";  

// OpenAI API密钥
const char* apiKey = "sk-CkxIb6MfdTBgZkdm0MtUEGVGk6Q6o5X5BRB1DwE2BdeSLSqB";  

// 全局声明audio变量，用于TTS播放
Audio audio;

// 初始化TFT显示屏
TFT_eSPI tft = TFT_eSPI();

// 初始化ArduinoGPTChat实例
ArduinoGPTChat gptChat(apiKey);

// 状态标志
bool isRecording = false;
bool lastTouchState = false;

void setup() {
  // 初始化串口
  Serial.begin(115200);
  delay(1000); // 给串口一些时间来初始化
  
  Serial.println("\n\n----- 触摸语音助手系统启动 -----");
  
  // 连接WiFi网络
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.println("正在连接WiFi...");
  
  int attempt = 0;
  while (WiFi.status() != WL_CONNECTED && attempt < 20) {
    Serial.print('.');
    delay(1000);
    attempt++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi连接成功！");
    Serial.print("IP地址: ");
    Serial.println(WiFi.localIP());
    
    // 设置I2S输出引脚（用于TTS播放）
    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    // 设置音量
    audio.setVolume(100);
    
    // 设置触摸引脚为输入
    pinMode(TOUCH_PIN, INPUT);
    
    // 初始化TFT显示屏
    tft.init();
    tft.loadFont(simfang16); 
    tft.setRotation(1); // 根据屏幕方向调整
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.drawString("触摸状态监测", 30, 20);
    
    Serial.println("\n----- 系统就绪 -----");
    Serial.println("按下触摸键开始录音，松开停止并发送给AI");
  } else {
    Serial.println("\n连接WiFi失败。请检查网络凭据并重试。");
  }
}

void loop() {
  // 处理音频循环（TTS播放）
  audio.loop();
  
  // 读取触摸状态
  bool currentTouchState = !digitalRead(TOUCH_PIN);
  
  // 检测触摸状态变化
  if (currentTouchState && !lastTouchState) {
    // 触摸按下 - 开始录音
    startRecording();
    isRecording = true;
    Serial.println("开始录音...");
  } else if (!currentTouchState && lastTouchState && isRecording) {
    // 触摸释放 - 停止录音并处理
    isRecording = false;
    Serial.println("录音结束，处理中...");
  }
  
  lastTouchState = currentTouchState;
  
  // 在屏幕上显示触摸状态
  tft.fillRect(0, 40, tft.width(), 40, TFT_BLACK);
  tft.setCursor(30, 40);
  tft.print("状态: ");
  tft.print(currentTouchState ? "按住录音" : "等待");
  
  delay(10); // 小延迟以防止CPU过载
}

// 开始录音并将识别结果发送给ChatGPT
void startRecording() {
  // 在开始录音前快速更新屏幕
  tft.fillRect(0, 60, tft.width(), 20, TFT_BLACK);
  tft.setCursor(60, 60);
  tft.print("Recording...");
  
  // 创建I2S实例 - 作为局部变量
  I2SClass i2s;
  
  // 设置I2S麦克风输入引脚
  i2s.setPinsPdmRx(MIC_DATA_PIN, MIC_CLOCK_PIN);
  
  // 初始化I2S进行录音
  if (!i2s.begin(I2S_MODE_PDM_RX, 16000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
    Serial.println("I2S init failed!");
    tft.fillRect(0, 60, tft.width(), 20, TFT_RED);
    tft.setCursor(10, 60);
    tft.print("Record Error");
    return;
  }
  
  // 创建变量存储音频数据
  uint8_t *wav_buffer;
  size_t wav_size;
  
  // 录制音频
  wav_buffer = i2s.recordWAV(RECORDING_DURATION, &wav_size);
  
  if (wav_buffer == NULL || wav_size == 0) {
    Serial.println("录制音频失败或缓冲区为空！");
    i2s.end(); // 关闭I2S
    return;
  }
  
  // 录音完成后立即关闭I2S，释放资源
  i2s.end();
  
  Serial.println("录音完成，大小: " + String(wav_size) + " 字节");
  Serial.println("正在将语音转换为文本...");
  
  // 将语音转换为文本
  String transcribedText = gptChat.speechToTextFromBuffer(wav_buffer, wav_size);
  
  // 释放缓冲区内存
  free(wav_buffer);
  
  // 显示转换结果
  if (transcribedText.length() > 0) {
    Serial.println("\n识别结果: " + transcribedText);
    
    // 自动将识别结果发送给ChatGPT
    Serial.println("\n正在将识别结果发送给ChatGPT...");
    chatGptCall(transcribedText);
  } else {
    Serial.println("未能识别到文本或发生错误。");
    Serial.println("可能没有检测到清晰的语音，请重试。");
  }
}

// 发送消息到ChatGPT
void chatGptCall(String message) {
  Serial.println("正在向ChatGPT发送请求...");
  
  String response = gptChat.sendMessage(message);
  
  if (response != "") {
    Serial.print("ChatGPT: ");
    Serial.println(response);
    
    // 自动将回复转换为语音
    if (response.length() > 0) {
      ttsCall(response);
    }
  } else {
    Serial.println("获取ChatGPT响应失败");
  }
}

// 文本转语音
void ttsCall(String text) {
  Serial.println("正在将文本转换为语音...");
  
  // 使用GPTChat的textToSpeech方法
  bool success = gptChat.textToSpeech(text);
  
  if (success) {
    Serial.println("TTS音频正在通过扬声器播放");
  } else {
    Serial.println("播放TTS音频失败");
  }
}