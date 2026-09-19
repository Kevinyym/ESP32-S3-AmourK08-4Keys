#include "TFTDebugDraw.h"
#include "lgfx.h"
#include <Adafruit_MPU6050.h>
#include <box2d/box2d.h>

Adafruit_MPU6050 mpu;
LGFX tft;
LGFX_Sprite sprite(&tft);
b2World *myWorld = nullptr;
unsigned long lastMs = 0;
bool mpuReady = false;
bool displayReady = false;
String sensorDiagnostics;

void diagnoseI2C() {
  sensorDiagnostics = "I2C ACK:";
  bool found = false;
  for (uint8_t address = 8; address < 120; ++address) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      char label[6];
      snprintf(label, sizeof(label), " %02X", address);
      sensorDiagnostics += label;
      found = true;
    }
  }
  if (!found) sensorDiagnostics += " none";
  sensorDiagnostics += "\n";
  for (uint8_t address : {0x68, 0x69}) {
    char label[40];
    Wire.beginTransmission(address);
    Wire.write(MPU6050_WHO_AM_I);
    if (Wire.endTransmission(true) != 0) {
      snprintf(label, sizeof(label), "0x%02X: no response\n", address);
    } else if (Wire.requestFrom(address, uint8_t(1)) != 1) {
      snprintf(label, sizeof(label), "0x%02X: ID read failed\n", address);
    } else {
      snprintf(label, sizeof(label), "0x%02X: ID=0x%02X (want 0x68)\n",
               address, Wire.read());
    }
    sensorDiagnostics += label;
  }
  Serial.print(sensorDiagnostics);
}

bool initTFTDevice() {
  tft.init();
  tft.setBrightness(200);
  tft.fillScreen(TFT_BLUE);
  tft.setTextColor(TFT_WHITE, TFT_BLUE);
  tft.setTextSize(2);
  tft.setCursor(10, 100);
  tft.println("K08 starting...");
  delay(500);
  tft.setColorDepth(8);
  sprite.setColorDepth(8);
  if (!sprite.createSprite(tft.width(), tft.height())) {
    Serial.println("Display sprite allocation failed");
    tft.fillScreen(TFT_RED);
    tft.setTextColor(TFT_WHITE, TFT_RED);
    tft.setCursor(10, 100);
    tft.println("Display memory error");
    return false;
  }
  return true;
}

void createSomeBall() {
  b2BodyDef bodyDef;
  bodyDef.type = b2_dynamicBody;
  bodyDef.allowSleep = true;
  auto r = (random() % 12) / 60.01;
  bodyDef.position.Set(9 + r * 5, 4 + r);
  auto *body = myWorld->CreateBody(&bodyDef);
  b2CircleShape shape;
  shape.m_radius = 0.4 + r;
  b2FixtureDef f;
  f.shape = &shape;
  f.density = 0.1;
  f.friction = 0.001;
  f.restitution = 0.01;
  body->CreateFixture(&f);
  body->ApplyForce(b2Vec2(r * 2, r), b2Vec2(0, 0), true);
}

void createSomeWorld() {
  b2Vec2 gravity(0, 10);
  myWorld = new b2World(gravity);
  b2Draw *draw = new TFTDebugDraw();
  draw->SetFlags(1);
  myWorld->SetAllowSleeping(false);
  myWorld->SetDebugDraw(draw);
  b2BodyDef groundBodyDef;
  b2Body *groundBody = myWorld->CreateBody(&groundBodyDef);
  b2PolygonShape groundBox;
  groundBox.SetAsBox(6, .025, b2Vec2(12, 0), 0);
  groundBody->CreateFixture(&groundBox, 0.0f);
  groundBox.SetAsBox(6, .025, b2Vec2(12, 24), 0);
  groundBody->CreateFixture(&groundBox, 0.0f);
  groundBox.SetAsBox(.025, 4.6, b2Vec2(6, 4.3), 0);
  groundBody->CreateFixture(&groundBox, 0.0f);
  groundBox.SetAsBox(.025, 4.6, b2Vec2(18, 4.3), 0);
  groundBody->CreateFixture(&groundBox, 0.0f);
  groundBox.SetAsBox(.025, 4.6, b2Vec2(6, 19.7), 0);
  groundBody->CreateFixture(&groundBox, 0.0f);
  groundBox.SetAsBox(.025, 4.6, b2Vec2(18, 19.7), 0);
  groundBody->CreateFixture(&groundBox, 0.0f);
  groundBox.SetAsBox(.05, 3.1, b2Vec2(8.5, 10.5), 2 * PI / 3);
  groundBody->CreateFixture(&groundBox, 0.0f);
  groundBox.SetAsBox(.05, 3.1, b2Vec2(15.5, 10.5), PI / 3);
  groundBody->CreateFixture(&groundBox, 0.0f);
  groundBox.SetAsBox(.05, 3.1, b2Vec2(8.5, 13.5), PI / 3);
  groundBody->CreateFixture(&groundBox, 0.0f);
  groundBox.SetAsBox(.05, 3.1, b2Vec2(15.5, 13.5), 2 * PI / 3);
  groundBody->CreateFixture(&groundBox, 0.0f);
  for (int i = 0; i < 66; i++) {
    createSomeBall();
  }
}

void setup() {
  Serial.begin(115200);
  displayReady = initTFTDevice();
  if (!displayReady) {
    return;
  }
  createSomeWorld();
  if (!Wire.begin(1, 2, 100000)) {
    sensorDiagnostics = "I2C controller init failed";
    Serial.println(sensorDiagnostics);
    return;
  }
  Wire.setTimeOut(50);
  Serial.println("Adafruit MPU6050 test!");
  mpuReady = mpu.begin(0x68, &Wire) || mpu.begin(0x69, &Wire);
  if (!mpuReady) {
    Serial.println("MPU6050 init failed (SDA=1, SCL=2); using demo gravity");
    diagnoseI2C();
    return;
  }
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  Serial.println("MPU6050 Found!");
}

void loop() {
  if (!displayReady) {
    delay(100);
    return;
  }
  myWorld->Step(0.1, 6, 2);
  sprite.clear();
  myWorld->DebugDraw();
  if (!mpuReady) {
    sprite.setTextColor(TFT_WHITE, TFT_BLACK);
    sprite.setCursor(4, 4);
    sprite.println("MPU6050 init failed - demo mode");
    sprite.println("SDA=1 SCL=2  addr=0x68/0x69");
    sprite.print(sensorDiagnostics);
  }
  sprite.pushSprite(0, 0);
  if (mpuReady && millis() - lastMs >= 1000) {
    lastMs = millis();
    sensors_event_t a, g, temp;
    if (mpu.getEvent(&a, &g, &temp)) {
      auto acc = a.acceleration;
      myWorld->SetGravity(b2Vec2(-acc.y, -acc.x));
    }
  }
}
