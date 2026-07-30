#include <Arduino.h>
#include <AccelStepper.h>

#define Y_IN1 D12
#define Y_IN2 D11
#define Y_IN3 D10
#define Y_IN4 D9

#define X_IN1 D8
#define X_IN2 D7
#define X_IN3 D6
#define X_IN4 D3

#define FULLSTEP 4

AccelStepper motorX(FULLSTEP, X_IN1, X_IN3, X_IN2, X_IN4);
AccelStepper motorY(FULLSTEP, Y_IN1, Y_IN3, Y_IN2, Y_IN4);


// + x para a direita e + y para cima.
const bool INVERTER_X = false;
const bool INVERTER_Y = true;

long fisicoX(long x) { return INVERTER_X ? -x : x; }

long fisicoY(long y) { return INVERTER_Y ? -y : y; }

long logX() { return INVERTER_X ? -motorX.currentPosition() : motorX.currentPosition(); }

long logY() { return INVERTER_Y ? -motorY.currentPosition() : motorY.currentPosition(); }


const float VEL_X  = 180.0;
const float ACEL_X = 60.0;
const float VEL_Y  = 180.0;
const float ACEL_Y = 60.0;

enum MovimentoManual {
  PARADO,
  MOVENDO_X,
  MOVENDO_Y
};

MovimentoManual movimentoAtual = PARADO;

unsigned long ultimoPrint = 0;
const unsigned long INTERVALO_PRINT_MS = 300;

// Alvo grande apenas para o motor continuar andando.
// Você para manualmente com "s".
const long ALVO_GRANDE = 200000;

void imprimirPosicao() {
  Serial.print("[POSICAO] X = ");
  Serial.print(logX());
  Serial.print(" passos | Y = ");
  Serial.print(logY());
  Serial.println(" passos");
}

void imprimirAjuda() {
  Serial.println();
  Serial.println("==============================================");
  Serial.println(" CALIBRACAO MANUAL DE PASSOS");
  Serial.println("==============================================");
  Serial.println("Comandos:");
  Serial.println("  x+   -> mover X positivo");
  Serial.println("  x-   -> mover X negativo");
  Serial.println("  y+   -> mover Y positivo");
  Serial.println("  y-   -> mover Y negativo");
  Serial.println("  s    -> parar");
  Serial.println("  p    -> imprimir posicao atual");
  Serial.println("  z    -> zerar X e Y na posicao atual");
  Serial.println("  h    -> ajuda");
  Serial.println();
  Serial.println("Uso recomendado:");
  Serial.println("1) Coloque fisicamente no canto inferior esquerdo.");
  Serial.println("2) Digite z.");
  Serial.println("3) Digite x+ ou y+.");
  Serial.println("4) Quando chegar no limite, digite s.");
  Serial.println("5) Leia os passos de X e Y.");
  Serial.println("==============================================");
  Serial.println();
}

void pararMotores() {
  movimentoAtual = PARADO;

  motorX.stop();
  motorY.stop();

  // Deixa o stop() terminar a desaceleração.
  while (motorX.distanceToGo() != 0 || motorY.distanceToGo() != 0) {
    motorX.run();
    motorY.run();
  }

  Serial.println();
  Serial.println("Movimento parado.");
  imprimirPosicao();
  Serial.println();
}

void zerarPosicaoAtual() {
  motorX.setCurrentPosition(0);
  motorY.setCurrentPosition(0);

  Serial.println();
  Serial.println("Posicao atual definida como origem:");
  Serial.println("X = 0 | Y = 0");
  Serial.println();
}

void moverXpositivo() {
  movimentoAtual = MOVENDO_X;

  motorY.stop();

  long alvoLogico = logX() + ALVO_GRANDE;
  motorX.moveTo(fisicoX(alvoLogico));

  Serial.println();
  Serial.println("Movendo X positivo...");
}

void moverXnegativo() {
  movimentoAtual = MOVENDO_X;

  motorY.stop();

  long alvoLogico = logX() - ALVO_GRANDE;
  motorX.moveTo(fisicoX(alvoLogico));

  Serial.println();
  Serial.println("Movendo X negativo...");
}

void moverYpositivo() {
  movimentoAtual = MOVENDO_Y;

  motorX.stop();

  long alvoLogico = logY() + ALVO_GRANDE;
  motorY.moveTo(fisicoY(alvoLogico));

  Serial.println();
  Serial.println("Movendo Y positivo...");
}

void moverYnegativo() {
  movimentoAtual = MOVENDO_Y;

  motorX.stop();

  long alvoLogico = logY() - ALVO_GRANDE;
  motorY.moveTo(fisicoY(alvoLogico));

  Serial.println();
  Serial.println("Movendo Y negativo...");
}

void processarComando(String comando) {
  comando.trim();
  comando.toLowerCase();

  if (comando == "x+") {
    moverXpositivo();
  }

  else if (comando == "x-") {
    moverXnegativo();
  }

  else if (comando == "y+") {
    moverYpositivo();
  }

  else if (comando == "y-") {
    moverYnegativo();
  }

  else if (comando == "s") {
    pararMotores();
  }

  else if (comando == "p") {
    imprimirPosicao();
  }

  else if (comando == "z") {
    zerarPosicaoAtual();
  }

  else if (comando == "h") {
    imprimirAjuda();
  }

  else {
    Serial.println();
    Serial.print("Comando desconhecido: ");
    Serial.println(comando);
    Serial.println("Digite h para ver os comandos.");
    Serial.println();
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  motorX.setMaxSpeed(VEL_X);
  motorX.setAcceleration(ACEL_X);

  motorY.setMaxSpeed(VEL_Y);
  motorY.setAcceleration(ACEL_Y);

  motorX.enableOutputs();
  motorY.enableOutputs();

  zerarPosicaoAtual();
  imprimirAjuda();
}

void loop() {
  if (Serial.available() > 0) {
    String comando = Serial.readStringUntil('\n');
    processarComando(comando);
  }

  if (movimentoAtual == MOVENDO_X) {
    motorX.run();
  }

  if (movimentoAtual == MOVENDO_Y) {
    motorY.run();
  }

  if (movimentoAtual != PARADO) {
    unsigned long agora = millis();

    if (agora - ultimoPrint >= INTERVALO_PRINT_MS) {
      ultimoPrint = agora;
      imprimirPosicao();
    }
  }
}
