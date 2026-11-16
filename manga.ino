#include <Arduino.h>
#include <LittleFS.h>
#include <Wire.h>
#include <TFT_eSPI.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

#define TOUCH_I2C_ADD 0x38 // Die Hardware-Adresse des FT6236 Chips

// Tragen Sie hier Ihre WLAN-Daten ein
const char* ssid     = "IHR_WLAN_NAME";
const char* password = "IHR_WLAN_PASSWORT";


TFT_eSPI tft = TFT_eSPI();

const int SPRITE_W = 240;
const int SPRITE_H = 240;
const size_t FRAME_BYTES = SPRITE_W * SPRITE_H * 2;

const int POS_X = (320 - SPRITE_W) / 2; // 80
const int POS_Y = (480 - SPRITE_H) / 2; // 160
const int HEAD_SPLIT_Y = POS_Y + 60;    // Top 60px = Head zone

enum AnimState {
  STATE_IDLE,
  STATE_HEADPAT,
  STATE_POKE
};

struct Animation {
  uint16_t* frames = nullptr;
  int frameCount = 0;
};

Animation animIdle;
Animation animHeadpat;
Animation animPoke;

AnimState currentState = STATE_IDLE;
int currentFrame = 0;
unsigned long lastFrameTime = 0;
unsigned long lastTouchTime = 0;

const int FRAME_DELAY_MS = 325;     // ~15 FPS
const int TOUCH_COOLDOWN_MS = 300; // Prevents double-triggers

Animation loadAnimToPSRAM(const char* path) {
  Animation anim;
  File f = LittleFS.open(path, "r");
  if (!f) return anim;
  
  size_t fileSize = f.size();
  anim.frameCount = fileSize / FRAME_BYTES;
  anim.frames = (uint16_t*)ps_malloc(fileSize);
  if (anim.frames) {
    f.read((uint8_t*)anim.frames, fileSize);
  }
  f.close();
  return anim;
}

void triggerState(AnimState newState) {
  currentState = newState;
  currentFrame = 0;
  lastTouchTime = millis();
}

// Zufällige Texte für die fröhliche Headpat-Animation
const char* headpatTexts[] = {
  "Yay! Hehehe *_*",
  "Das tut gut! <3",
  "Nochmal, bitte!",
  "Miau~ :3",
  "Du bist nett!"
};
const int numHeadpatTexts = 5; // Anzahl der Einträge in der Liste

// Zufällige Texte für die erschrockene Poke-Animation
const char* pokeTexts[] = {
  "W-Was war das?!",
  "Hey! Nicht stupsen!",
  "Kyaaa! O_O",
  "Lass das! :P",
  "Huch! Erschreckt!"
};
const int numPokeTexts = 5; // Anzahl der Einträge in der Liste

void setup() {
  Serial.begin(115200);
  delay(1000); // Warten auf Seriellen Monitor
  Serial.println("--- Boot-Vorgang startet ---");

  // Touch-Reset/Interrupt-Pins für das ThingPulse Kit definieren
  pinMode(27, OUTPUT); 
  digitalWrite(27, LOW);  // Chip zurücksetzen
  delay(10);
  digitalWrite(27, HIGH); // Chip aufwecken
  delay(50);
  
  pinMode(32, OUTPUT);
  digitalWrite(32, HIGH); // Hintergrundbeleuchtung an

  tft.init();
  tft.setSwapBytes(false); // <-- Zwingend nötig für RGB565-Bilder!

  // 1. WLAN starten
  setupWiFi();

  if (WiFi.status() == WL_CONNECTED) {
      tft.fillScreen(TFT_GREEN); // Grün = WLAN verbunden!
      delay(1000);
      
      // Sichern Sie sich zuerst den Speicherplatz im PSRAM für die Frames
      // (Beispiel: 4 Frames à 240x240 RGB565 benötigen 460.800 Bytes)
      size_t bufferSize = 4 * 240 * 240 * 2;
      animIdle.frames = (uint16_t*)ps_malloc(bufferSize);
      
      // 2. Animation via Insecure-HTTPS laden
      // Ersetzen Sie die URL mit der Adresse zu Ihren hochgeladenen .bin-Dateien
      int randomAnimID = random(1, 2); // Wählt zufällig Animation 1 bis 9
      String url = "http://116.161.1_/data/anim_" + String(randomAnimID) + ".bin";
      
      
      if (downloadAnimationInsecureHTTPS(url, animIdle.frames, bufferSize)) {
        animIdle.frameCount = 4; // Setzen Sie die Frame-Anzahl manuell
        Serial.println("Online-Animation erfolgreich in PSRAM geladen!");
      } else {
        Serial.println("HTTPS-Download fehlgeschlagen.");
      }
    } else {
      tft.fillScreen(TFT_RED); // Rot = Kein WLAN
      delay(2000);
    }

    tft.fillScreen(TFT_BLACK); // Startzustand für die Animation
  }

  tft.setRotation(0);

  // SCHRITT 1: PSRAM testen
  Serial.print("Gesamter PSRAM: ");
  Serial.println(ESP.getPsramSize());
  Serial.print("Verfügbarer PSRAM: ");
  Serial.println(ESP.getFreePsram());

 // Startet den I2C-Bus auf den ThingPulse-Pins
  Wire.begin(21, 22); 
  
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS Fehler!");
  }

  // Ab hier die Bilder laden...
  animIdle = loadAnimToPSRAM("/idle.bin");
  animHeadpat = loadAnimToPSRAM("/headpat.bin");
  animPoke = loadAnimToPSRAM("/poke.bin");
  
  tft.fillScreen(TFT_BLACK); // Startzustand: Hintergrund schwarz für Charakter
}

void loop() {
  unsigned long currentMillis = millis();

  // 1. ZUFALLS-REAKTIONEN (Simuliert die Touch-Zonen automatisch)
  // Wenn wir im IDLE-Modus sind und der Cooldown vorbei ist...
  if (currentState == STATE_IDLE && (currentMillis - lastTouchTime > 8000)) { // Alle 8 Sekunden Chance auf Reaktion
    
    int randomChance = random(0, 100); // Würfeln von 0 bis 99
    
    if (randomChance < 15 && animHeadpat.frameCount > 0) {
      // 15% Chance: Der Charakter freut sich (Headpat-Animation)
      Serial.println("-> Automatische Freude ausgelöst!");
      triggerState(STATE_HEADPAT);
      // Zufälligen Text aus der Headpat-Liste auswählen
      int textIdx = random(0, numHeadpatTexts);
      drawSpeechBubble(headpatTexts[textIdx], 1800); // 1,8 Sekunden anzeigen
    } 
    else if (randomChance >= 15 && randomChance < 30 && animPoke.frameCount > 0) {
      // Weitere 15% Chance: Der Charakter erschrickt (Poke-Animation)
      Serial.println("-> Automatische Überraschung ausgelöst!");
      triggerState(STATE_POKE);
      // Zufälligen Text aus der Poke-Liste auswählen
      int textIdx = random(0, numPokeTexts);
      drawSpeechBubble(pokeTexts[textIdx], 1800);
    } else {
      // 70% Chance: Bleibt im Idle, setzt aber den Timer zurück
      lastTouchTime = currentMillis; 
    }
  }

  // 2. RENDER-SCHLEIFE (Zeichnet die Frames flüssig auf den Bildschirm)
  if (currentMillis - lastFrameTime >= FRAME_DELAY_MS) {
    lastFrameTime = currentMillis;

    // Bestimmen, welche Animation gerade aktiv sein soll
    Animation* active = &animIdle;
    if (currentState == STATE_HEADPAT) active = &animHeadpat;
    else if (currentState == STATE_POKE) active = &animPoke;

    // Nur zeichnen, wenn Frames erfolgreich geladen wurden
    if (active->frameCount > 0 && active->frames != nullptr) {
      uint16_t* framePtr = active->frames + (currentFrame * SPRITE_W * SPRITE_H);
      tft.pushImage(POS_X, POS_Y, SPRITE_W, SPRITE_H, framePtr);
      currentFrame++;

      // Am Ende der Animation ankommen
      if (currentFrame >= active->frameCount) {
        currentFrame = 0;
        
        // Wenn es eine Reaktion war, wechseln wir danach zurück ins Idle
        if (currentState != STATE_IDLE) {
          currentState = STATE_IDLE;
          lastTouchTime = currentMillis; // Cooldown starten
        }
      }
    }
  }
}

bool downloadAnimationInsecureHTTPS(const char* url, uint16_t* psramBuffer, size_t maxBytes) {
  WiFiClientSecure client;
  
  // ZWINGT DEN ESP32, JEDEM SSL-ZERTIFIKAT BLIND ZU VERTRAUEN:
  client.setInsecure(); // [1] Nochmaliger Ablaufschutz oder Zertifikats-Updates entfallen!

  HTTPClient http;
  Serial.print("[HTTPS] Lade ohne Zertifikatsprüfung: "); Serial.println(url);
  
  if (http.begin(client, url)) {
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
      int len = http.getSize();
      if (len > maxBytes) {
        Serial.println("Fehler: Datei zu groß!");
        http.end();
        return false;
      }

      // Stream direkt blockweise in den PSRAM schreiben
      WiFiClient* stream = http.getStreamPtr();
      uint8_t* byteBuffer = (uint8_t*)psramBuffer;
      size_t totalRead = 0;
      
      while (http.connected() && (len > 0 || len == -1)) {
        size_t size = stream->available();
        if (size) {
          int c = stream->readBytes(byteBuffer + totalRead, size);
          totalRead += c;
          if (len > 0) len -= c;
        }
        delay(1);
      }
      
      Serial.println("[HTTPS] Download abgeschlossen!");
      http.end();
      return true;
    } else {
      Serial.printf("[HTTPS] Fehler: %s\n", http.errorToString(httpCode).c_str());
    }
    http.end();
  }
  return false;
}

void drawSpeechBubble(const char* text, int duration_ms) {
  int bx = 40;       // X-Position der Blase
  int by = 40;       // Y-Position (weit über dem Charakter)
  int bw = 240;      // Breite
  int bh = 70;       // Höhe
  int radius = 10;   // Abgerundete Ecken

  // 1. Weiße Blase mit schwarzem Rand zeichnen
  tft.fillRoundRect(bx, by, bw, bh, radius, TFT_WHITE);
  tft.drawRoundRect(bx, by, bw, bh, radius, TFT_BLACK);

  // 2. Kleines Dreieck am unteren Rand zeichnen (zeigt nach unten auf den Kopf)
  int tri_x = bx + (bw / 2);
  int tri_y = by + bh;
  tft.fillTriangle(tri_x - 10, tri_y, tri_x + 10, tri_y, tri_x, tri_y + 15, TFT_WHITE);
  tft.drawTriangle(tri_x - 10, tri_y, tri_x + 10, tri_y, tri_x, tri_y + 15, TFT_BLACK);
  // Kleines Stück der Oberkante des Dreiecks weiß übermalen, damit es offen wirkt
  tft.drawFastHLine(tri_x - 9, tri_y, 18, TFT_WHITE);

  // 3. Text in die Blase schreiben
  tft.setTextColor(TFT_BLACK);
  tft.setTextSize(2); // Gut lesbare Textgröße
  
  // Text zentrieren (einfache Schätzung für kurze Sätze)
  int textWidth = strlen(text) * 12; 
  int textX = bx + (bw - textWidth) / 2;
  int textY = by + (bh - 16) / 2;
  
  tft.setCursor(textX, textY);
  tft.print(text);

  // 4. Sichtbar lassen und danach weglöschen
  delay(duration_ms);
  
  // Bereich der Blase wieder schwarz färben, um sie zu entfernen
  tft.fillRect(bx - 2, by - 2, bw + 4, bh + 20, TFT_BLACK);
}

void setupWiFi() {
  Serial.println();
  Serial.print("Verbinde mit Netzwerk: ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  // Warten, bis die Verbindung steht
  int counter = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    counter++;
    
    // Nach 15 Sekunden abbrechen, falls das Passwort falsch ist
    if (counter > 30) {
      Serial.println("\nVerbindung fehlgeschlagen! Bitte Daten prüfen.");
      return;
    }
  }

  Serial.println("");
  Serial.println("WLAN erfolgreich verbunden!");
  Serial.print("IP-Adresse des Kits: ");
  Serial.println(WiFi.localIP());
}