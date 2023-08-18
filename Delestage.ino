/*
   Délestage
   Version 2.1
   23/09/2020-20/10/2020
   08/11/2021 Version 2.1.1
*/

//#define MY_DEBUG
#define TPSSTART 120000  // Délai avant envoi des mesures

// Watchdog
#include <avr/wdt.h>

// LED
#include <Adafruit_NeoPixel.h>
#ifdef __AVR__
#include <avr/power.h>  // Required for 16 MHz Adafruit Trinket
#endif
#define NUMPIXELS 1
#define PINLED 8
Adafruit_NeoPixel pixels(NUMPIXELS, PINLED, NEO_GRB + NEO_KHZ400);

#include <SoftwareSerial.h>
#include <LibTeleinfo.h>
SoftwareSerial Serial1(3, 4);
TInfo tinfo;

#define MY_RADIO_RF24
#define MY_RF24_PA_LEVEL RF24_PA_HIGH
//#define MY_RF24_PA_LEVEL RF24_PA_MAX
//#define MY_RF24_DATARATE RF24_1MBPS // fast
//#define MY_RF24_DATARATE RF24_250KBPS // slow
#define MY_REPEATER_FEATURE
#define MY_NODE_ID 6
#define MY_TRANSPORT_WAIT_READY_MS 5000

#include <MySensors.h>

//MyMessage kwh_msg(1, V_KWH);
MyMessage papp_msg(1, V_WATT);         // Puissance
MyMessage current_msg(2, V_CURRENT);   // Intensité
MyMessage current_PTEC(4, V_STATUS);   // Jour / Nuit
MyMessage delestage_msg(5, V_STATUS);  // Mode délestage
MyMessage nbdel_msg(6, V_LEVEL);       // Nombre déléments délestés
MyMessage HC_msg(7, V_KWH);            // en fait c'est des WH
MyMessage HP_msg(8, V_KWH);            // en fait c'est des WH
MyMessage Alerte_msg(9, V_STATUS);     // Alerte délestage maximum
MyMessage ch1_msg(10, V_STATUS);       // Chambre parents
MyMessage ch2_msg(11, V_STATUS);       // Chambre Félix
MyMessage ch3_msg(12, V_STATUS);       // Chambre Léo
MyMessage ch4_msg(13, V_STATUS);       // Salon
MyMessage ch7_msg(14, V_STATUS);       // Cumulus
MyMessage ch5_msg(15, V_STATUS);       // Chambre Léo2
//MyMessage Depassement_msg(20, V_STATUS); // Dépassement intensité d'alerte
//

// Elements
#define nbElements 6

struct S {
  byte sortie;
  byte intensite;
  bool on;      // Etat relais ON (0 ou 1)
  bool etat;    // Element coupé ou activé par un interrupteur
  bool delest;  // Element délesté
};

// Priorités
byte prio[nbElements] = { 2, 1, 0, 3, 5, 4 };

// Sorties par ordre (Pin, Intensité, Etat marche (pas de délestage), Coupé/allumé (inter))
/*
  1 Radiateur parents
  2 Radiateur Félix
  3 Radiateur Léo
  4 Radiateur salon
  5 Cumulus
  6 Petit radiateur Léo
*/
struct S IO[nbElements] = { { 16, 9, 0, 1, 0 }, { 17, 7, 0, 1, 0 }, { 19, 7, 0, 1, 0 }, { 18, 7, 0, 1, 0 }, { 5, 8, 1, 1, 0 }, { 15, 5, 0, 1, 0 } };

// Intensité maxi
byte IDEL = 30;      // Intensité maxi avant délestage
byte IALERT = 35;    // Intensité d'alerte, on déleste tout d'un coup
byte IURGENCE = 40;  // Intensité maximum, on n'attend pas une deuxième mesure
bool Alert = false;
bool OldAlert = !Alert;
bool Mes = false;  // Autorisation d'envoi des mesures

// Infos
byte IINST = 120;
int PAPP = 30000;
unsigned long HC, HP = 0;
String PTEC = "HC..";

void before() {
  // Coupure de toutes les sorties
  for (int i = 0; i < nbElements; i++) {
    //Serial.println(IO[i].sortie);
    digitalWrite(IO[i].sortie, !IO[i].on);
    pinMode(IO[i].sortie, OUTPUT);
  }
  pixels.begin();
  pixels.clear();
  LED(100, 100, 100);  // Blanc
  delay(10000);
}

void presentation() {
  sendSketchInfo("TELEINFO", "2.1.1");
  present(1, S_POWER, "EDF.PUISSANCE");
  present(2, S_MULTIMETER, "EDF.I.inst");
  present(4, S_BINARY, "HeurePleine");
  present(5, S_BINARY, "Delestage");
  present(6, S_DUST, "NbElements");
  present(7, S_POWER, "HeuresCreuses");
  present(8, S_POWER, "HeuresPleines");
  present(9, S_BINARY, "Alerte");
  present(10, S_BINARY, "ChambreP");
  present(11, S_BINARY, "ChambreF");
  present(12, S_BINARY, "ChambreL");
  present(13, S_BINARY, "Salon");
  present(14, S_BINARY, "Cumulus");
  present(15, S_BINARY, "ChambreL2");
}

void setup() {
  wdt_disable();
  Serial1.begin(1200);
  Serial.begin(1200);
  wdt_enable(WDTO_8S);
  tinfo.init();
  tinfo.attachData(readData);
  tinfo.attachNewFrame(delestage);
  delay(2000);
  sendInitialData();
}

char CarSerial;
void loop() {
  // Lecture de l'intensité actuelle
  wdt_reset();
  if (Serial1.available()) {
      CarSerial=Serial1.read();
      tinfo.process(CarSerial);
      Serial.write(CarSerial);
  }
}

void receive(const MyMessage &message) {
  if (message.getType() == V_STATUS) {
    if (message.getSensor() > 9) {
      // ON/OFF
      byte index = message.getSensor() - 10;
      if (message.getBool()) {
        if (IO[index].intensite + IINST <= IDEL) digitalWrite(IO[index].sortie, IO[index].on);
      } else {
        digitalWrite(IO[index].sortie, !IO[index].on);
      }
      //digitalWrite(IO[index].sortie, message.getBool() ? IO[index].on : !IO[index].on);
      IO[index].etat = message.getBool();
      updateLed();
    }
  }
}

void readData(ValueList *me, uint8_t flags) {
  String rep = String(me->name);
  unsigned long valeur = String(me->value).toInt();
  // Reception des valeurs
  if (flags & (TINFO_FLAGS_UPDATED | TINFO_FLAGS_ADDED)) {
    if (rep == "IINST") {
      IINST = valeur;
      send(current_msg.set(IINST));
    } else if (rep == "PAPP") {
      PAPP = valeur;
      send(papp_msg.set(PAPP));
    } else if (rep == "PTEC") {
      PTEC = me->value;
      send(current_PTEC.set(PTEC == "HC.." ? 0 : 1));
    } else if ((rep == "HCHC") && (valeur > HC)) {
      HC = valeur;
      send(HC_msg.set(float(valeur) / 1000, 3));
    } else if ((rep == "HCHP") && (valeur > HP)) {
      HP = valeur;
      send(HP_msg.set(float(valeur) / 1000, 3));
    }
  }
}

void sendInitialData() {
  // Etat des boutons M/A (marche)
  send(papp_msg.set(0));
  send(current_msg.set(0));
  send(ch1_msg.set(1));
  send(ch2_msg.set(1));
  send(ch3_msg.set(1));
  send(ch4_msg.set(1));
  send(ch5_msg.set(1));
  send(ch7_msg.set(1));
  send(nbdel_msg.set(0));
}

int nbt = 0;
void delestage() {
  // délestage toutes les 2 trames
  nbt++;
  if (nbt < 2 && IINST < IURGENCE) return;
  nbt = 0;
  byte j;
  // Intensité < Max
  if (IINST <= IDEL) {
    if (Alert) {
      Alert = false;
      updateLed();
    }
    // On teste par ordre de priorité si la sortie est coupée
    for (int i = 0; i < nbElements; i++) {
      j = prio[i];
      if (digitalRead(IO[j].sortie) == !IO[j].on && (IO[j].intensite + IINST <= IDEL) && IO[j].etat) {
        digitalWrite(IO[j].sortie, IO[j].on);
        updateLed();
        break;
      }
    }
  }
  // Intensité > Max
  else if (IINST < IALERT) {
    for (int i = nbElements - 1; i >= 0; i--) {
      j = prio[i];
      if (digitalRead(IO[j].sortie) == IO[j].on) {
        digitalWrite(IO[j].sortie, !IO[j].on);
        updateLed();
        break;
      }
    }
  } else {
    for (int i = 0; i < nbElements; i++) {
      if (digitalRead(IO[i].sortie) == IO[i].on) {
        digitalWrite(IO[i].sortie, !IO[i].on);
      }
    }
    updateLed();
    Alert = true;
  }
  if (Alert != OldAlert) {
    send(Alerte_msg.set(Alert));
    OldAlert = Alert;
  }
}

void updateLed() {
  int nbd = 0;
  for (int i = 0; i < nbElements; i++) {
    if (digitalRead(IO[i].sortie) != IO[i].on && IO[i].etat) nbd++;
  }
  send(nbdel_msg.set(nbd));
  send(delestage_msg.set(nbd == 0 ? 0 : 1));
  if (nbd == 0) LED(75, 0, 0);
  else if (nbd < nbElements) LED(37, 70, 5);
  else LED(0, 75, 0);
}

void LED(byte V, byte R, byte B) {
  pixels.setPixelColor(0, pixels.Color(V, R, B));
  pixels.show();
}