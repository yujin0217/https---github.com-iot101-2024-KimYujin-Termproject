#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ESP32Servo.h>
#include <BluetoothSerial.h>  // 블루투스 키패드 통신용

// Wi-Fi 정보
const char* ssid = "iPhone";
const char* password = "dbwlsrkgus0";

// 웹 서버, Preferences, Servo, BluetoothSerial 객체 생성
WebServer server(80);
Preferences preferences;
Servo servo;
BluetoothSerial SerialBT;

String generatedOTP = "";  // 생성된 OTP 저장
unsigned long lastOTPTimestamp = 0;  // OTP 생성 시간 저장
bool isAuthenticated = false;  // 사용자 인증 상태 저장

// HTML 페이지 템플릿
const char* htmlPage = R"=====( 
<!DOCTYPE html>
<html>
<head>
  <title>OTP Smart Doorlock</title>
</head>
<body>
  <h1>OTP Smart Doorlock</h1>
  <form action="/login" method="POST">
    <label for="user">ID:</label><br>
    <input type="text" id="user" name="user"><br><br>
    <label for="password">Password:</label><br>
    <input type="password" id="password" name="password"><br><br>
    <input type="submit" value="login">
  </form>

  <script>
    function updateOTP() {
      fetch('/otp')
        .then(response => response.json())
        .then(data => {
          document.getElementById("otp").textContent = data.otp;
          document.getElementById("time").textContent = data.time;
        });
    }

    // 1초마다 updateOTP 호출하여 OTP 갱신
    setInterval(updateOTP, 1000);
  </script>
</body>
</html>
)=====";

// OTP 생성 함수
String generateOTP() {
  String otp = "";
  for (int i = 0; i < 6; i++) {
    otp += String(random(0, 10));  // 0~9 사이의 숫자 6개 생성
  }
  return otp;
}

// OTP와 남은 시간을 제공하는 핸들러
void handleOTP() {
  int remainingTime = 60 - ((millis() - lastOTPTimestamp) / 1000);
  if (remainingTime <= 0) {
    generatedOTP = generateOTP();  // 새로운 OTP 생성
    lastOTPTimestamp = millis();
    remainingTime = 60;
  }

  String json = "{\"otp\":\"" + generatedOTP + "\", \"time\":" + String(remainingTime) + "}";
  server.send(200, "application/json", json);
}

// 사용자 인증 처리
void handleLogin() {
  if (server.hasArg("user") && server.hasArg("password")) {
    String user = server.arg("user");
    String pass = server.arg("password");

    // 저장된 ID와 비밀번호 불러오기
    String storedUser = preferences.getString("userID", "");
    String storedPass = preferences.getString("password", "");

    // 인증 검증
    if (user == storedUser && pass == storedPass) {
      isAuthenticated = true;
      generatedOTP = generateOTP();  // OTP 생성
      lastOTPTimestamp = millis();   // OTP 생성 시간 기록

      int remainingTime = 60;  // OTP 유효 시간 초기화 (60초)
      
      // HTML 응답 생성
      String response = "<html><body><h2>Success!</h2>";
      response += "<p>OTP Password: " + generatedOTP + "</p>";
      response += "<p>Time Left: <span id='time'>" + String(remainingTime) + "</span> seconds</p>";
      response += "<script>";
      response += "let remainingTime = " + String(remainingTime) + ";";
      response += "function updateTimer() {";
      response += "  if (remainingTime > 0) {";
      response += "    remainingTime--;";
      response += "    document.getElementById('time').textContent = remainingTime;";
      response += "  }";
      response += "}";
      response += "setInterval(updateTimer, 1000);";  // 1초마다 타이머 업데이트
      response += "</script></body></html>";

      server.send(200, "text/html", response);
      Serial.println("User authenticated. OTP: " + generatedOTP);
    } else {
      server.send(401, "text/html", "<html><body><h2>Fail! Wrong ID or Password</h2></body></html>");
    }
  } else {
    server.send(400, "text/html", "<html><body><h2>Error: Missing User ID or Password</h2></body></html>");
  }
}




void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  Serial.println("Attempting WiFi Connection...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to WiFi");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // Preferences 네임스페이스 초기화 및 ID/비밀번호 설정
  preferences.begin("user_data", false);

  // 항상 ID와 비밀번호를 기본값으로 설정
  preferences.putString("userID", "yujin");      // 기본 ID 설정
  preferences.putString("password", "030217");   // 기본 비밀번호 설정
  Serial.println("ID and password set to default values.");

  // 웹 서버 경로 설정
  server.on("/", []() { server.send(200, "text/html", htmlPage); });
  server.on("/login", handleLogin);
  server.on("/otp", handleOTP);  // OTP 갱신 경로 추가
  server.begin();

  SerialBT.begin("ESP32_OTP_Lock");
  servo.attach(13);
  servo.write(0);
  randomSeed(analogRead(0));
}


void loop() {
  server.handleClient();

  // 블루투스에서 OTP 입력 받기
  if (SerialBT.available()) {
    String inputOTP = SerialBT.readString();
    inputOTP.trim();  // 입력된 OTP 공백 제거

    Serial.println("Received OTP from Bluetooth: " + inputOTP);

    // OTP 일치 확인 및 서보모터 제어
    if (isAuthenticated && inputOTP == generatedOTP) {
      Serial.println("OTP matched! Unlocking the door.");
      servo.write(90);  // 서보모터 회전 (문 열림)
      delay(5000);  // 5초 동안 열림 유지
      servo.write(0);  // 잠금 위치로 복귀
      isAuthenticated = false;  // 인증 상태 초기화
      generatedOTP = "";  // OTP 초기화
    } else {
      Serial.println("Incorrect OTP.");
    }
  }

  // OTP가 1분이 지나면 무효화
  if (isAuthenticated && millis() - lastOTPTimestamp > 60000) {
    generatedOTP = generateOTP();  // 새로운 OTP 생성
    lastOTPTimestamp = millis();
    Serial.println("New OTP generated: " + generatedOTP);
  }
}
