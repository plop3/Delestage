/*
   Délestage
   Version 2.1
   23/09/2020-20/10/2020
*/

/* TODO
  Message d'alerte
  Détection coupure/reprise secteur (envoi d'un SMS)
*/

#define MY_DEBUG

#include <Adafruit_NeoPixel.h>
#ifdef __AVR__
#include <avr/power.h> // Required for 16 MHz Adafruit Trinket
#endif
#define NUMPIXELS 1
#define PINLED 8
Adafruit_NeoPixel pixels(NUMPIXELS, PINLED, NEO_GRB + NEO_KHZ400);

#include <SoftwareSerial.h>
#include <LibTeleinfo.h>
SoftwareSerial Serial1(3, 4);
TInfo tinfo;

#define MY_RADIO_RF24
//#define MY_RF24_PA_LEVEL RF24_PA_HIGH
//#define MY_RF24_PA_LEVEL RF24_PA_MAX
//#define MY_RF24_DATARATE RF24_1MBPS // fast
#define MY_RF24_DATARATE RF24_250KBPS // slow
//#define MY_REPEATER_FEATURE
#define MY_NODE_ID 6
#define MY_TRANSPORT_WAIT_READY_MS 5000

#include <MySensors.h>

//MyMessage kwh_msg(1, V_KWH);
MyMessage papp_msg(1, V_WATT);          // Puissance
MyMessage current_msg(2, V_CURRENT);    // Intensité
MyMessage current_PTEC(4, V_STATUS);    // Jour / Nuit
MyMessage delestage_msg(5, V_STATUS);   // Mode délestage
MyMessage nbdel_msg(6, V_LEVEL);         // Nombre déléments délestés
MyMessage HC_msg(7, V_KWH );              // en fait c'est des WH
MyMessage HP_msg(8, V_KWH );              // en fait c'est des WH
MyMessage Alerte_msg(9, V_STATUS);          // Alerte délestage maximum
MyMessage ch1_msg(10, V_STATUS);        // Chambre parents
MyMessage ch2_msg(11, V_STATUS);        // Chambre Félix
MyMessage ch3_msg(12, V_STATUS);        // Chambre Léo
MyMessage ch4_msg(13, V_STATUS);        // Salon
MyMessage ch7_msg(14, V_STATUS);        // Cumulus
MyMessage Depassement_msg(20, V_STATUS); // Dépassement intensité d'alerte
//

// Elements
#define nbElements 5

struct S {
  byte sortie;
  byte intensite;
  bool on;        // Etat relais ON (0 ou 1)
  bool etat;      // Element coupé ou activé par un interrupteur
};

// Priorités
byte prio[nbElements] = {2, 1, 0, 3, 4};

// Sorties par ordre (Pin, Intensité, Etat marche (pas de délestage), Coupé/allumé (inter))
/*
  1 Radiateur parents
  2 Radiateur Félix
  3 Radiateur Léo
  4 Radiateur salon
  5 Cumulus
*/
struct S IO[nbElements] = {{16, 9, 0, 1}, {17, 7, 0, 1}, {19, 7, 0, 1}, {18, 7, 0, 1}, {5, 7, 1, 1}};

// Intensité maxi
byte IDEL = 32;   // Intensité maxi avant délestage
byte IALERT = 40; // Intensité d'alerte, on déleste tout d'un coup
byte NbDelest = 0;
bool Alert = false;
bool OldAlert = !Alert;
bool Depass = false;
bool OldDepass = !Depass;

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
  LED(100, 100, 100);
}

void presentation() {
  sendSketchInfo("TELEINFO", "1.3.1");
  present(1, S_POWER, "EDF.PUISSANCE");
  present(2, S_POWER, "EDF.I.inst");
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
  present(20, S_BINARY, "Depassement");
}

void setup() {
  Serial1.begin(1200);
  tinfo.init();
  tinfo.attachData(readData);
  tinfo.attachNewFrame(delestage);
  delay(2000);
  sendInitialData();
}

void loop() {
  // Lecture de l'intensité actuelle
  if ( Serial1.available() )
    tinfo.process(Serial1.read());
}

void receive(const MyMessage &message)
{
  if (message.getType() == V_STATUS) {
    if (message.getSensor() > 9) {
      // ON/OFF
      byte index = message.getSensor() - 10;
      digitalWrite(IO[index].sortie, message.getBool() ? IO[index].on : !IO[index].on);
      IO[index].etat = message.getBool();
    }
  }
}

void readData(ValueList * me, uint8_t  flags)
{
  String rep = String(me->name);
  unsigned long valeur = String(me->value).toInt();
  // Reception des valeurs
  if (flags & (TINFO_FLAGS_UPDATED | TINFO_FLAGS_ADDED)) {
    if (rep == "IINST") {
      IINST = valeur;
      send(current_msg.set(IINST));
    }
    else if (rep == "PAPP") {
      PAPP = valeur;
      send(papp_msg.set(PAPP));
    }
    else if (rep == "PTEC") {
      PTEC = me->value;
      send(current_PTEC.set(PTEC == "HC.." ? 0 : 1));
    }
    else if ((rep == "HCHC") && (valeur != HC)) {
      HC = valeur;
      Serial.println(float(valeur) / 1000);
      send(HC_msg.set(float(valeur) / 1000, 3));
    }
    else if ((rep == "HCHP" ) && (valeur != HP)) {
      HP = valeur;
      send(HP_msg.set(float(valeur) / 1000, 3));
      Serial.println(float(valeur) / 1000);
    }
  }
  //  IMAX = currentTI.IMAX;
  //  HC = currentTI.HC_HC;
  //  HP = currentTI.HC_HP;

}

void sendInitialData() {
  // Etat des boutons M/A (marche)
  send(ch1_msg.set(1));
  send(ch2_msg.set(1));
  send(ch3_msg.set(1));
  send(ch4_msg.set(1));
  send(ch7_msg.set(1));
  send(nbdel_msg.set(0));
}

int nbt = 0;
void delestage() {
  // délestage toutes les 2 trames
  nbt++;
  if (nbt < 2) return;
  nbt = 0;
  byte j;
  // Intensité < Max
  if (IINST <= IDEL) {
    if (Alert) {
      Alert = false;
      Depass = false;
      LED(37, 70, 5); // Orange
    }
    // On teste par ordre de priorité si la sortie est coupée
    for (int i = 0; i < nbElements; i++) {
      j = prio[i];
      if (digitalRead(IO[j].sortie) == !IO[j].on && (IO[j].intensite + IINST <= IDEL && IO[j].etat)) {
        digitalWrite(IO[j].sortie, IO[j].on);
        if (NbDelest > 0) NbDelest--;
        send(nbdel_msg.set(NbDelest));
        if (!NbDelest) {
          LED(75, 0, 0); //Vert
          send(delestage_msg.set(0));
        }
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
        NbDelest++;
        send(nbdel_msg.set(NbDelest));
        if (NbDelest == 1) {
          send(delestage_msg.set(1));
          LED(37, 70, 5); // Orange
        }
        break;
      }
      if (i == 0) {
        LED(75, 0, 0); //Rouge
        Alert = true;
        Depass = false;
      }
    }
  }
  else {
    for (int i = 0; i < nbElements; i++) {
      //Serial.println(IO[i].sortie);
      if (digitalRead(IO[i].sortie) == IO[i].on) {
        digitalWrite(IO[i].sortie, !IO[i].on);
        NbDelest++;
      }
    }
    send(delestage_msg.set(1));
    send(nbdel_msg.set(NbDelest));
    LED(75, 0, 0); //Rouge
    Alert = true;
    Depass = true;
  }
  if (Alert != OldAlert) {
    send(Alerte_msg.set(Alert));
    OldAlert = Alert;

  }
  if (Depass != OldDepass) {
    send(Depassement_msg.set(Depass));
    OldDepass = !Depass;
  }
}

void LED(byte V, byte R, byte B) {
  pixels.setPixelColor(0, pixels.Color(V, R, B));
  pixels.show();
}
