/*
   Délestage
   Version 1.0
   23/09/2020-06/10/2020
*/

/* TODO
  Compteurs HC/HP
  Message d'alerte
  Détection coupure/reprise secteur (envoi d'un SMS)
      -> Revoir la lib téléinfo pour qu'elle ne soit pas bloquante
*/

//#define MY_DEBUG

#include <Adafruit_NeoPixel.h>
#ifdef __AVR__
#include <avr/power.h> // Required for 16 MHz Adafruit Trinket
#endif
#define NUMPIXELS 1
#define PINLED 8
Adafruit_NeoPixel pixels(NUMPIXELS, PINLED, NEO_GRB + NEO_KHZ400);

#include <SimpleTimer.h>
SimpleTimer timer;

#include <SoftwareSerial.h>
#include <teleInfo.h>
#define TI_RX 3
teleInfo TI( TI_RX );
#define BUFSIZE 15
teleInfo_t currentTI;

#define MY_RADIO_RF24
//#define MY_RF24_PA_LEVEL RF24_PA_HIGH
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
MyMessage ch1_msg(10, V_STATUS);        // Chambre parents
MyMessage ch2_msg(11, V_STATUS);        // Chambre Félix
MyMessage ch3_msg(12, V_STATUS);        // Chambre Léo
MyMessage ch4_msg(13, V_STATUS);        // Salon
MyMessage ch7_msg(14, V_STATUS);        // Cumulus
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

// Infos
byte IINST = 120;
int PAPP = 30000;
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
  sendSketchInfo("TELEINFO", "1.2.1");
  present(1, S_POWER, "EDF.PUISSANCE");
  present(2, S_MULTIMETER, "EDF.I.inst");
  present(4, S_BINARY, "Heure pleine");
  present(5, S_BINARY, "Delestage");
  present(6, S_DUST, "NbElements");
  present(10, S_BINARY, "ChambreP");
  present(11, S_BINARY, "ChambreF");
  present(12, S_BINARY, "ChambreL");
  present(13, S_BINARY, "Salon");
  present(14, S_BINARY, "Cumulus");
}

void setup() {
  timer.setTimeout(8000, sendInitialData);
}

void loop() {
  timer.run();
  // Lecture de l'intensité actuelle
  readData();
  delestage();
}

void receive(const MyMessage &message)
{
  if (message.getType() == V_STATUS) {
    if (message.getSensor() > 9) {
      // ON/OFF
      byte index = message.getSensor() - 10;
      digitalWrite(IO[index].sortie, message.getBool() ? !IO[index].on : IO[index].on);
      IO[index].etat = message.getBool();
    }
  }
}

void readData()
{
  currentTI = TI.get();
  // Reception des valeurs
  //  IMAX = currentTI.IMAX;
  //  HC = currentTI.HC_HC;
  //  HP = currentTI.HC_HP;
  if (PTEC != currentTI.PTEC) {
    PTEC = currentTI.PTEC;
    send(current_PTEC.set(PTEC == "HC.." ? 0 : 1));
  }
  if (PAPP != currentTI.PAPP) {
    PAPP = currentTI.PAPP;
    send(papp_msg.set(PAPP));
  }

  if (IINST != currentTI.IINST) {
    IINST = currentTI.IINST;
    send(current_msg.set(IINST));
  }
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

void delestage() {
  byte j;
  // Intensité < Max
  if (IINST <= IDEL) {
    if (Alert) {
      Alert = false;
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
  }
}

void LED(byte V, byte R, byte B) {
  pixels.setPixelColor(0, pixels.Color(V, R, B));
  pixels.show();
}
