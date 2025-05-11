#include "SimpleTimer.h"
#include "EtherCard.h"
#include <avr/wdt.h>

#define GENERAL_PING_TIME_MS (2 * 60 * 1000)
#define ALERT_PING_TIME_MS (10 * 1000)
#define ALERT_PING_MAX_TRIES 10

// Initial cold start delay 30 min
#define INITIAL_COLD_START_DELAY 30

// How long reset relay should be active: 15 sec
#define RELAY_RESET_DELAY (15 * 1000)

// Delay after restart: 15 min
#define AFTER_RESET_DELAY 15

// number of relay control pin
#define RELAY_PIN 5

static unsigned long initialColdStartDelay = INITIAL_COLD_START_DELAY * 60L * 1000L;
static unsigned long afterResetDelay = AFTER_RESET_DELAY * 60L * 1000L;

static byte remoteIp[]   = {8, 8, 8, 8};

static byte myStaticIp[]   = {192, 168,   1, 213};
static byte myStaticGw[]   = {192, 168,   1,   1};
static byte myStaticDns[]  = {192, 168,   1,   1};
static byte myStaticMask[] = {255, 255, 255,   0};

static byte myMAC[] = {0x54, 0x55, 0x58, 0x10, 0x00, 0x24};

static byte Ethernet::buffer[700];

static SimpleTimer mainTimer;
static int generalTimer = 0;
static int alertTimer = 0;

// ping flags and counters
static bool pingSent = false;
static unsigned int counterPingSent = 0;
static bool pingReceived = false;

static unsigned int alertModeCounterPingSent = 0;

static unsigned long pingStartTime = 0;
static unsigned long timePassedFromLastPing = 0;


void sendToSerial(String message) {
  Serial.println(message);
}

// called when a ping comes in (replies to it are automatic)
static void gotPinged (byte* ptr) {
  ether.printIp(">>> ping from: ", ptr);
}

void printMac() {
  Serial.print("MAC: ");
  for (byte i = 0; i < sizeof(myMAC); ++i) {
    Serial.print(myMAC[i], HEX);
    if (i < (sizeof(myMAC) - 1))
      Serial.print(':');
  }
  Serial.println();
}

void setup () {

  // set up relay control pin
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  // set up serial debug port
  Serial.begin(9600);

  printDelayToStartMessage(initialColdStartDelay);

  setUpEthernet();

  // setting up timer with callback functions
  generalTimer = mainTimer.setInterval(GENERAL_PING_TIME_MS, pingGoogle);
  alertTimer = mainTimer.setInterval(ALERT_PING_TIME_MS, pingAlert);

  turnOnGeneralMode();
}

void setUpEthernet() {
  // setting up ethernet parameters
  Serial.println(F("\n[Starting...]"));
  printMac();
  if (ether.begin(sizeof Ethernet::buffer, myMAC, 10) == 0) {
    Serial.println(F("Failed to access Ethernet controller"));
  }

  Serial.println(F("Setting up DHCP..."));
  if (!ether.dhcpSetup()) {
    Serial.println(F("DHCP failed. Setting up static parameters..."));
    ether.staticSetup(myStaticIp, myStaticGw, myStaticDns, myStaticMask);
  }

  ether.printIp("My IP: ", ether.myip);
  ether.printIp("Netmask: ", ether.netmask);
  ether.printIp("GW IP: ", ether.gwip);
  ether.printIp("DNS IP: ", ether.dnsip);

  for (int rii = 0; rii < 4; rii++) {
    ether.hisip[rii] = remoteIp[rii];
  }

  ether.printIp("Remote server: ", ether.hisip);
 
  // call this to report others pinging us
  ether.registerPingCallback(gotPinged);
}

void printDelayToStartMessage(unsigned long mainDelay) {
  unsigned long numSeconds = mainDelay / 1000;
  for (unsigned long i = numSeconds; i > 0; i--) {
    String message = String(i) + " seconds to start...";
    sendToSerial(message);
    delay(1000);
  }
}

void rebootHardware() {
  sendToSerial(F("Rebooting hardware..."));
  delay(500);

  mainTimer.disable(generalTimer);
  mainTimer.disable(alertTimer);
  delay(500);

  digitalWrite(RELAY_PIN, HIGH);
  delay(RELAY_RESET_DELAY);
  digitalWrite(RELAY_PIN, LOW);

  sendToSerial(F("Hardware rebooted, waiting for warm up..."));

  printDelayToStartMessage(afterResetDelay);

  setUpEthernet();
  mainTimer.restartTimer(generalTimer);
  mainTimer.restartTimer(alertTimer);
  turnOnGeneralMode();
}

void turnOnAlertMode() {
  sendToSerial(F("Alert mode ON"));
  mainTimer.disable(generalTimer);

  pingSent = false;
  counterPingSent = 0;
  pingReceived = false;
  alertModeCounterPingSent = 0;
  pingStartTime = 0;
  timePassedFromLastPing = 0;

  delay(500);
  mainTimer.enable(alertTimer);
}

void turnOnGeneralMode() {
  sendToSerial(F("General mode ON"));
  mainTimer.disable(alertTimer);

  pingSent = false;
  counterPingSent = 0;
  pingReceived = false;
  alertModeCounterPingSent = 0;
  pingStartTime = 0;
  timePassedFromLastPing = 0;

  delay(500);
  mainTimer.enable(generalTimer);
}

void pingAlert() {
  ether.printIp("Alert mode pinging: ", ether.hisip);
  ether.clientIcmpRequest(ether.hisip);

  if (pingReceived == true) {
    turnOnGeneralMode();
    return;
  }

  if (alertModeCounterPingSent > ALERT_PING_MAX_TRIES) {
    rebootHardware();
  }

  alertModeCounterPingSent++;
}

void pingGoogle() {
  ether.printIp("Pinging: ", ether.hisip);
  ether.clientIcmpRequest(ether.hisip);

  if (pingSent == false) {
    pingStartTime = millis();
  } else {
    pingStartTime = timePassedFromLastPing + millis();
    
    if (pingReceived == false) {
      String message = "Ping lost. timePassedFromLastPing: " + String(timePassedFromLastPing) + "ms";
      sendToSerial(message);

      // turn on alert mode
      turnOnAlertMode();
    }
  }

  counterPingSent++;
  pingSent = true;
  pingReceived = false;
}

void loop () {
  
  word len = ether.packetReceive(); // go receive new packets
  word pos = ether.packetLoop(len); // respond to incoming ping

  if (len > 0 && ether.packetLoopIcmpCheckReply(ether.hisip)) {
    //long pingTime = millis() - pingStartTime;
    String message = "Ping SUCCESSFUL. Ping time: " + String(timePassedFromLastPing) + "ms";
    sendToSerial(message);

    pingSent = false;
    pingReceived = true;
    timePassedFromLastPing = 0;
    counterPingSent--;
  }

  if (pingSent == true) {
    timePassedFromLastPing = millis() - pingStartTime;
  }

  mainTimer.run();
  delay(1);
}