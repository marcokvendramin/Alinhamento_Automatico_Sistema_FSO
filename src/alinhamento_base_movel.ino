#include "alinhamento.h"
#include "paginas.h" // Arquivo local HTML.
#include <Arduino.h> // Para funções básicas.
#include <AccelStepper.h> // Biblioteca dos motores de passo.
#include <WiFi.h> // Para conectar/criar rede e hospedar painel web.
#include <ESPAsyncWebServer.h> // Servidor web assíncrono.
#include "esp_timer.h" // Timers para envio/recepção bit a bit.
#include <atomic> // Variáveis atômicas, flags.
#include <mutex> // Evita conflito de tarefas simultâneas.

#define BIT_TIME 5000 
#define SIZE 4096

// Pinos dos motores e sensores
#define Y_IN1 D12 // GPIO 47
#define Y_IN2 D11 // GPIO 38
#define Y_IN3 D10 // GPIO 21
#define Y_IN4 D9  // GPIO 18

#define X_IN1 D8  // GPIO 17
#define X_IN2 D7  // GPIO 10
#define X_IN3 D6  // GPIO 9
#define X_IN4 D3  // GPIO 6

#define PIN_PAINEL_SUP A0 // GPIO 1 
#define PIN_PAINEL_DIR A1 // GPIO 2 
#define PIN_TIL78      7  // D4

#define FULLSTEP 4 // Modo fullstep em motor de 4 fases

// Protótipo da tarefa de calibração, parâmetro padrão (ESP32/FreeRTOS)
void tarefaCalibracao(void * pvParameters);

// Objetos dos motores e mutex para thread-safety
AccelStepper motorX(FULLSTEP, X_IN1, X_IN3, X_IN2, X_IN4);
AccelStepper motorY(FULLSTEP, Y_IN1, Y_IN3, Y_IN2, Y_IN4);
std::mutex   motorMutex;

// Limites, velocidades, passos e inversão de eixos do alinhamento
const long  X_MIN = 0,  X_MAX = 9300, Y_MIN = 0, Y_MAX = 9300;
const float VEL_X = 180.0, ACEL_X = 60.0, VEL_Y = 180.0, ACEL_Y = 60.0;
const long  PASSO_GROSSO = 100, PASSO_CENTRO = 5;  // ~0,11 mm — passo da varredura de centralização
const bool  INVERTER_X = false, INVERTER_Y = true;

inline long fisicoX(long x) { return INVERTER_X ? -x : x; }
inline long fisicoY(long y) { return INVERTER_Y ? -y : y; }
// Converte para usar no sentido do plano cartesiano (y > 0 para cima e x > 0 para direita)
inline long logX() { return INVERTER_X ? -motorX.currentPosition() : motorX.currentPosition(); }
inline long logY() { return INVERTER_Y ? -motorY.currentPosition() : motorY.currentPosition(); }
inline long clip(long v, long mn, long mx) { return v < mn ? mn : v > mx ? mx : v; }

//  Geometria física dos painéis e deslocamento do TIL78
const float PAINEL_SUP_X_UTIL_MM = 34.51, PAINEL_DIR_Y_UTIL_MM = 33.50;
const long  PAINEL_SUP_LARGURA_PASSOS = 6224, PAINEL_DIR_ALTURA_PASSOS  = 5883;
const float PASSOS_POR_MM_X = PAINEL_SUP_LARGURA_PASSOS / PAINEL_SUP_X_UTIL_MM; // ~ 180,53 passos/mm.
const float PASSOS_POR_MM_Y = PAINEL_DIR_ALTURA_PASSOS  / PAINEL_DIR_Y_UTIL_MM; // ~ 175,61 passos/mm.
const float TIL_DIST_ESQ_MM = 11.20, TIL_DIST_TOP_MM = 8.70;
const long  TIL_X_DA_BORDA = lround(TIL_DIST_ESQ_MM * PASSOS_POR_MM_X);
const long  TIL_Y_DA_BORDA = lround((PAINEL_DIR_Y_UTIL_MM - TIL_DIST_TOP_MM) * PASSOS_POR_MM_Y);


//----------  Variáveis globais de comunicação -----------------
volatile int vemDado = 0;
volatile int caracbit = 0;
volatile int bites[8]={0,0,0,0,0,0,0,0};
volatile int nextCaracterCount = 0;
volatile int bitEsperadoNoPareamento = 3;

volatile int32_t dataSize = 0;
volatile int32_t countDataTransfer = 0;

volatile bool espectSize[32]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

volatile bool prontoParaReceber = false;
volatile bool prontoData = false;
volatile bool prontoParaEnviar = false;
volatile bool interrupcao = false;
volatile bool fimDeRequisicao = false;

volatile char caracterParaEnvio = 'U';

volatile unsigned long timePassed = 0;

hw_timer_t *recpTimer = NULL;
hw_timer_t *envTimer = NULL;

// ----------------- Flags de controle (atômicas para thread-safety) -----------------

std::atomic<bool> calibracaoRodando{false};
std::atomic<bool> calibracaoConcluida{false};
std::atomic<bool> cancelarCalibracao{false};

TaskHandle_t calibracaoTaskHandle = NULL;
TaskHandle_t alinhamentoTaskHandle = NULL;

std::atomic<bool> alinhamentoRodando{false};
std::atomic<bool> alinhamentoConcluido{false};
std::atomic<bool> cancelarAlinhamento{false};
std::atomic<bool> zerandoRodando{false};

TaskHandle_t zerarTaskHandle = NULL;

std::atomic<bool> ajusteManualHabilitado{false};
MovimentoManual movimentoManualAtual = MANUAL_PARADO;
unsigned long ultimoPrintManual = 0;
const unsigned long INTERVALO_PRINT_MANUAL_MS = 300;
const long ALVO_GRANDE_MANUAL = 200000; // Artificial, grande.

// Mais rápida que digitalRead, true se pino ativo.
bool laserNoTIL78()  { return (GPIO.in & (1UL << PIN_TIL78)) != 0; }

//  Armazena posição de retorno.
volatile long ultimoX_alinhado = -1, ultimoY_alinhado = -1; 
volatile long deslocamentoAlinhX = 0, deslocamentoAlinhY = 0;

// --- Estrutura de dados (Chunklist) ---

typedef struct node
{
  char dado[SIZE];
  short paro;
  struct node* next;
}Chunklist;

Chunklist* create();
Chunklist* newnode();
Chunklist* addinfo(Chunklist* lista, volatile char c);
void destroylist(Chunklist* lista);
char nextcaracter(Chunklist* lista);

Chunklist* create() { return NULL; }
Chunklist* lista = create();

Chunklist* newnode ()
{
  Chunklist* no= (Chunklist*)malloc(sizeof(Chunklist));
  no->paro=0;
  no->next=NULL;
  return no;
}

Chunklist* addinfo (Chunklist* lista, volatile char c) 
{
  if (lista == NULL)
  {
    lista = newnode();
    lista->dado[0] = c;
    lista->paro = 1;
  }
  else
  {
    Chunklist* t = lista;
    while (t->next != NULL && t->paro >= SIZE)
    {
       t = t->next;
    }

    if (t->paro < SIZE)
    {
      t->dado[t->paro] = c;
      t->paro++;
    }
    else
    {
      t->next = newnode();
      t = t->next;
      t->dado[0] = c;
      t->paro = 1;
    }
  }

  dataSize++;
  return lista;
}

void destroylist (Chunklist* lista)
{
  Chunklist* t = lista;
  while (lista != NULL)
  {
    t=t->next;
    free(lista);
    lista=t;
  }
}

char nextcaracter(Chunklist* lista)
{
  if ((nextCaracterCount < SIZE) && (nextCaracterCount < lista->paro))
    return (lista->dado[nextCaracterCount++]);
  else if ((nextCaracterCount != SIZE) && (nextCaracterCount == lista->paro))
  {
    nextCaracterCount = 0;
    return (0);
  }  
  else
  {
    Chunklist *t = lista;
    int i;
    for (i=(nextCaracterCount/SIZE);i>0;i--)
    {
      t=t->next;
    }

    if (t == NULL)
    {
      nextCaracterCount = 0;
      return (0);
    }
    else
    {
      int pos = nextCaracterCount%SIZE;
      nextCaracterCount++;
      return t->dado[pos];
    }
  }
}

void desativarPinoOut(int gpio) {pinMode(gpio, INPUT);}


/*------------------  AttachInterrupts  ------------------------*/

void IRAM_ATTR detectouTentativaPareamento() {interrupcao=true;}
void IRAM_ATTR processoPareamento() {interrupcao=true;}

void IRAM_ATTR inicioDeRecebimento()
{
  timerAlarmDisable(recpTimer);
  timerAlarmWrite(recpTimer, (BIT_TIME * 2.5), true);
  timerWrite(recpTimer,0);
  timerStart(recpTimer);
  timerAlarmEnable(recpTimer);
}

void fimDeRecebimento()
{
  if (recpTimer != NULL) 
  {
    timerStop(recpTimer);
  
    vemDado = 0;
    prontoParaReceber = false;
    for (int i=0; i<8; i++)
      bites[i]=0;
    for (int i= 0; i<32; i++)
      espectSize[i]=0;
    prontoData=false;
    nextCaracterCount = 0;
    bitEsperadoNoPareamento = 3;
    interrupcao = false;
  }
}

void IRAM_ATTR inicioDeEnvio()
{
  timerAlarmDisable(envTimer);
  timerAlarmWrite(envTimer, (BIT_TIME * 2), true);
  timerWrite(envTimer,0);
  timerStart(envTimer);
  timerAlarmEnable(envTimer);
}

void fimDeEnvio() 
{
  if (envTimer != NULL) 
  {
    timerStop(envTimer);
    GPIO.out_w1tc = (1 << 5);
    desativarPinoOut(5);
    prontoParaEnviar = false;
    caracterParaEnvio = 'U';
    nextCaracterCount = 0;
    destroylist(lista);
    countDataTransfer=0;
    dataSize=0;
    prontoData=false;
    lista=NULL;            
  }
}

void encerrarCanalComunicacao() {
  fimDeEnvio();
  fimDeRecebimento();

  detachInterrupt(digitalPinToInterrupt(D4));

  prontoParaReceber       = false;
  prontoParaEnviar        = false;
  prontoData              = false;
  fimDeRequisicao         = false;
  interrupcao             = false;
  vemDado                 = 0;
  caracbit                = 0;
  nextCaracterCount       = 0;
  bitEsperadoNoPareamento = 3;
  countDataTransfer       = 0;

  GPIO.out_w1tc = (1 << 5);
  desativarPinoOut(5);

  Serial.println("[COM] Canal encerrado: timers parados, interrupcao removida, laser OFF e flags limpas.");
}

// ----------------- Controle das páginas -------------------

enum ModoOperacao { IDLE, ENVIO, ENVIANDO, RECEPCAO, RECEBIDO, ALINHAMENTO, CALIBRACAO };
ModoOperacao estadoAtual = IDLE;

String getModoTexto() {
  switch (estadoAtual) {
    case ENVIO:       return "MODO ENVIO";
    case ENVIANDO:    return "MODO ENVIO";
    case RECEPCAO:    return "MODO RECEPÇÃO";
    case RECEBIDO:    return "MODO RECEPÇÃO";
    case ALINHAMENTO:
      if (alinhamentoConcluido.load()) return "ALINHAMENTO CONCLUÍDO";
      if (alinhamentoRodando.load())   return "ALINHANDO...";
      return "MODO ALINHAMENTO";
    case CALIBRACAO:
      if (calibracaoConcluida.load()) return "CALIBRAÇÃO CONCLUÍDA";
      if (calibracaoRodando.load())   return "CALIBRANDO AMBIENTE...";
      return "MODO CALIBRAÇÃO";
    default:          return "AGUARDANDO SELEÇÃO";
  }
}

String processor(const String& var) {
  if (var == "MODO_ATUAL") return getModoTexto();
  return String();
}

AsyncWebServer server(80);

//----------------- DEFINIÇÃO DE ROTAS ----------------------

void rotasServidor ()
{
  // Rota Raiz (Painel)
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html, processor);
  });

  // Rota para atualização de status em tempo real
  server.on("/status-atual", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", getModoTexto());
  });

  // LOGICA: ENVIO
  server.on("/set-envio", HTTP_GET, [](AsyncWebServerRequest *request){
    estadoAtual = ENVIO;
    request->redirect("/envio-page");
  });

  server.on("/envio-page", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", envio_html);
  });

  // Recebe 512 char por vez – ENVIO
  server.on("/receber-bloco", HTTP_POST, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", "OK");
  }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t, size_t){
    
    if (len<4) return;

    char tamanhoTexto[5]={(char)data[0], (char)data[1], (char)data[2], (char)data[3], '\0'};
    short tamanhoRealDoBloco = (short)atoi(tamanhoTexto);
    
    int limiteDeCaracteres = 4 + (tamanhoRealDoBloco * 2);
    for (int ponteiroLeitura=4; ponteiroLeitura < limiteDeCaracteres; ponteiroLeitura+=2)
    {
      char byteHex[3] = { (char)data[ponteiroLeitura], (char)data[ponteiroLeitura + 1], '\0' };
      char bytePuro = (char)strtol(byteHex, NULL, 16);
      lista = addinfo(lista, bytePuro);
    }
  });

  // Acabou de receber os dados – ENVIO
  server.on("/receber-char", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("c")) 
    {
        AsyncWebParameter* p = request->getParam("c");
        String comando = p->value();
        
        if (comando == "EOF")
        {
            request->send(200, "text/plain", "Encerrado");

            // Ativa o pino e dispara a Task do Laser
            pinMode(D2, OUTPUT);
            GPIO.out_w1ts = (1 << 5);
            xTaskCreatePinnedToCore(prepareToSend, "prepareToSend", 4000, NULL, 1, NULL, 1);
        }
        else 
        {
            request->send(200, "text/plain", "OK");
        }
    }
    else
    {
      request->send(400, "text/plain", "Parametro invalido");
    }
  });

  // LOGICA: ALINHAMENTO
  server.on("/set-alinhamento", HTTP_GET, [](AsyncWebServerRequest *request){
    estadoAtual = ALINHAMENTO;

    encerrarCanalComunicacao();

    if (!alinhamentoRodando.load()) {
      cancelarAlinhamento.store(false);
      alinhamentoConcluido.store(false);

      xTaskCreatePinnedToCore(
        tarefaAlinhamento,
        "TarefaAlinhamento",
        8000,
        NULL,
        1,
        &alinhamentoTaskHandle,
        1
      );
    }

    request->send_P(200, "text/html", placeholder_html, processor);
  });

  // LOGICA: RECEPÇÃO (Prepara dados e redireciona)
  server.on("/set-recepcao", HTTP_GET, [](AsyncWebServerRequest *request){
    estadoAtual = RECEPCAO;
    fimDeRecebimento();
    request->redirect("/recepcao-page");
    attachInterrupt(digitalPinToInterrupt(D4), detectouTentativaPareamento, RISING);
    xTaskCreatePinnedToCore(pairingManager, "PairingManager", 4000, NULL, 1, NULL, 1);
  });

  server.on("/recepcao-page", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", recepcao_html);
  });

  // Entrega de 255 char por vez – RECEPÇÃO
  server.on("/proximo-char", HTTP_GET, [](AsyncWebServerRequest *request){
    AsyncWebServerResponse *response;
    if (estadoAtual != RECEBIDO) {
      response = request->beginResponse(200, "text/plain", "IDLE");
    } 
    else if (estadoAtual == RECEBIDO) 
    {
      if (fimDeRequisicao)
      { 
        response = request->beginResponse(200, "text/plain", "EOF");
        fimDeRequisicao=false;
        countDataTransfer=0;
        dataSize=0;
        destroylist(lista);
        lista=NULL;
      }
      else
      {
        if (countDataTransfer <= dataSize)
        {
          if (countDataTransfer == 0)
          {
            char bytePuro = nextcaracter(lista);
            countDataTransfer++;
            char bufferHex[3];
            sprintf(bufferHex, "%02X", bytePuro);

            response = request->beginResponse(200, "text/plain", bufferHex);

            if (countDataTransfer == dataSize)
              fimDeRequisicao=true;
          }
          else
          {
            int tam = 0;
            if (dataSize - countDataTransfer > 255)
            {
              tam = 255;
            } 
            else
            {
              tam = dataSize - countDataTransfer;
              fimDeRequisicao = true;
            }

            char* hexData = (char*)malloc( ((2*tam)+2) * sizeof(char) );

            char tamHex[3];
            sprintf(tamHex, "%02X", tam);
            hexData[0]=tamHex[0];
            hexData[1]=tamHex[1];

            int i = 2;
            for (i=2; (i <= (2*tam)) && (countDataTransfer < dataSize); i+=2)
            {
              char bytePuro = nextcaracter(lista);
              countDataTransfer++;
              char bufferHex[3];
              sprintf(bufferHex, "%02X", bytePuro);
              hexData[i]=bufferHex[0];
              hexData[i+1]=bufferHex[1];
            }

            response = request->beginResponse(200, "text/plain", hexData);

            free(hexData);
          }
        }
      }
    }
    response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    request->send(response);
  });

  // VOLTAR: Reseta flag e redireciona
  server.on("/voltar", HTTP_GET, [](AsyncWebServerRequest *request){
    estadoAtual = IDLE;
    request->redirect("/");
  });

  // VOLTAR DA PÁGINA DO ALINHAMENTO
  server.on("/voltar-alinhamento", HTTP_GET, [](AsyncWebServerRequest *request){
    cancelarAlinhamento.store(true);
    estadoAtual = IDLE;
    desligarMotores();
    request->redirect("/");
  });

  // ZERAR: volta o carrinho para o (0,0)
  server.on("/zerar", HTTP_GET, [](AsyncWebServerRequest *request){

    if (zerandoRodando.load()) {
      request->send(202, "text/plain", "Retorno ja esta em andamento.");
      return;
    }

    Serial.printf("[ZERAR] Encerrando comunicacao e retornando de X=%ld Y=%ld para origem.\n",
                  logX(), logY());

    // Cancela qualquer alinhamento e/ou calibração em execução.
    cancelarAlinhamento.store(true);
    cancelarCalibracao.store(true);
    encerrarCanalComunicacao();

    estadoAtual = IDLE; // Estado do sistema volta para parado/aguardando
    zerandoRodando.store(true);

    // Tipo do FreeRTOS. Parâmetros:
    // Função a ser executada, nome da tarefa, tamanho da pilha da tarefa,
    // ausência de parâmetro extra, prioridade 2 (maior que a 1), guarda ID
    // em zerarTaskHandle, fixa no core 1 do ESP32.
    BaseType_t criada = xTaskCreatePinnedToCore(
      tarefaZerar,
      "TarefaZerar",
      5000,
      NULL,
      2,
      &zerarTaskHandle,
      1
    );

    // Se falhar
    if (criada != pdPASS) {
      zerandoRodando.store(false);
      zerarTaskHandle = NULL;
      // Erro 500 é de falha interna do servidor/ dispositivo.
      request->send(500, "text/plain", "Falha ao criar tarefa de retorno.");
      return;
    }

    // 202: aceito para processamento, iniciado, mas não acabado.
    request->send(202, "text/plain",
      "Retorno iniciado. Acompanhe em /status-atual.");
  });

  // LOGICA: CALIBRAÇÃO DE DOIS PONTOS
  server.on("/set-calibracao", HTTP_GET, [](AsyncWebServerRequest *request){
    if (alinhamentoRodando.load() || alinhamentoTaskHandle != NULL) {
      Serial.println("[CAL_ENV] Recusada: alinhamento em execucao.");
      // Erro 409 é de conflito, não calibrar enquanto alinha.
      request->send(409, "text/plain",
                    "Calibracao recusada: encerre o alinhamento antes.");
      return;
    }

    if (calibracaoRodando.load() || calibracaoTaskHandle != NULL) {
      request->send_P(200, "text/html", calibracao_html, processor);
      return;
    }

    estadoAtual = CALIBRACAO;
    cancelarCalibracao.store(false);
    calibracaoConcluida.store(false);


    // Função a ser executada, nome da tarefa, tamanho da pilha da tarefa,
    // ausência de parâmetro extra, prioridade 1, guarda ID em 
    // calibracaoTaskHandle, fixa no core 1 do ESP32.
    BaseType_t criada = xTaskCreatePinnedToCore(
      tarefaCalibracao,
      "TarefaCalibracao",
      7000,
      NULL,
      1,
      &calibracaoTaskHandle,
      1
    );

    if (criada != pdPASS) {
      calibracaoTaskHandle = NULL;
      estadoAtual = IDLE;
      request->send(500, "text/plain", "Falha ao criar tarefa de calibracao.");
      return;
    }

    request->send_P(200, "text/html", calibracao_html, processor);
  });

  // VOLTAR DA PÁGINA DE CALIBRAÇÃO
  server.on("/voltar-calibracao", HTTP_GET, [](AsyncWebServerRequest *request){
    cancelarCalibracao.store(true);
    estadoAtual = IDLE;
    request->redirect("/");
  });
}

/*-----------------------  Envio/Recepção  ----------------------------------*/

void findSize(volatile bool espect[32])
{
  countDataTransfer = 0;
  for (int i = 31; i >= 0; i--) 
  {
    countDataTransfer = countDataTransfer<<1;
    if (espect[i] == 1)
      countDataTransfer = countDataTransfer | espect[i]; 
  }
}

volatile char convertchar(volatile int bits[8]) 
{
  char resultado = 0;
    
  for (int i = 7; i >= 0; i--) 
  {
    resultado = resultado<<1;
    if (bits[i] == 1)
      resultado = resultado | bits[i]; 
  }

  if (dataSize == countDataTransfer)
  {
    countDataTransfer = 0;
    estadoAtual = RECEBIDO;
    fimDeRecebimento();
  }
  
  return resultado;
}

void IRAM_ATTR onRecpTimer() 
{
  if (!prontoParaReceber)
  {
    timerAlarmWrite(recpTimer, BIT_TIME, true);
    bool bite = GPIO.in & (1<<7);
    if (bite)
      bites[caracbit]=1;
    else
      bites[caracbit]=0;

    if (bite != bitEsperadoNoPareamento%2)
      timerAlarmWrite(recpTimer, BIT_TIME*1.25, true);

    bitEsperadoNoPareamento++;
    caracbit++;
    if (caracbit > 7)
    {
      caracbit = 0;
      prontoParaReceber=true;
      timerAlarmWrite(recpTimer, BIT_TIME, true);
    }
  }
  else
  {
    if (!prontoData)
    {
      bool bite = GPIO.in & (1<<7);
    
      if (bite)
        espectSize[caracbit]=1;
      else
        espectSize[caracbit]=0;
    
      caracbit++;
      if (caracbit > 31)
      {
        caracbit = 0;
        findSize(espectSize);
        prontoData=true;
      }
    }
    else
    {    
      bool bite = GPIO.in & (1<<7);
    
      if (bite)
        bites[caracbit]=1;
      else
        bites[caracbit]=0;
    
      caracbit++;
      if (caracbit > 7)
      {
        caracbit = 0;
        lista=addinfo(lista,convertchar(bites));
      }
    }
  }
}

void IRAM_ATTR onEnvTimer() 
{
  if (!prontoParaEnviar)
  {
    timerAlarmWrite(envTimer, BIT_TIME, true);
    
    if ((caracterParaEnvio >> caracbit) & 1)
      GPIO.out_w1ts = (1 << 5);
    else
      GPIO.out_w1tc = (1 << 5);
      
    caracbit++;
    if (caracbit > 7)
    {
      caracbit = 0;
      prontoParaEnviar = true;
      caracterParaEnvio = nextcaracter(lista); 
    }
  }
  else
  {
    if (!prontoData)
    {
      int bite = (dataSize >> caracbit) & 1;
      
      if (bite)
        GPIO.out_w1ts = (1 << 5);
      else
        GPIO.out_w1tc = (1 << 5);
      
      caracbit++;
      if (caracbit > 31)
      {
        caracbit = 0;
        prontoData=true;
      }
    }
    else
    {
      int bite = (caracterParaEnvio >> caracbit) & 1;
      
      if (bite)
        GPIO.out_w1ts = (1 << 5);
      else
        GPIO.out_w1tc = (1 << 5);
      
      caracbit++;
      if (caracbit > 7)
      {
        caracbit = 0;
        countDataTransfer++;
        caracterParaEnvio = nextcaracter(lista); 
        if (countDataTransfer == dataSize)
          fimDeEnvio();
      }
    }
  }
}

//---------------------  Core Tasks  -------------------------------

void configureTimerRecepcao(void * pvParameters) 
{  
  inicioDeRecebimento();
  vTaskDelete(NULL);
}

void pairingManager (void * pvParameters)
{
  bool trava = false;
  for(;;)
  {
    if (interrupcao)
    {
      if (vemDado == 0)
      {
        if (!trava)
        {
          timePassed=micros();
          trava = true;
        }
        else if (micros()-timePassed > 250000)
        {
          interrupcao=false;
          detachInterrupt(digitalPinToInterrupt(D4));
          vemDado=1;
          attachInterrupt(digitalPinToInterrupt(D4), processoPareamento, FALLING);
          timePassed = micros();
        }
      }
      else if (vemDado == 1)
      {
        if (micros()-timePassed > 250000)
        {
          detachInterrupt(digitalPinToInterrupt(D4));
          vemDado=2;
          xTaskCreatePinnedToCore(configureTimerRecepcao, "ConfigTimerRecp", 2000, NULL, 1, NULL, 1);
          vTaskDelete(NULL);
        }
      }
    }
  }
}

void prepareToSend(void * pvParameters)
{
  vTaskDelay(pdMS_TO_TICKS(1000));

  GPIO.out_w1tc = (1 << 5);

  inicioDeEnvio();
  
  vTaskDelete(NULL);
}

//  Task de retorno a origem
void tarefaZerar(void* pvParameters) {
  zerandoRodando.store(true);
  zerarTaskHandle = xTaskGetCurrentTaskHandle();

  // Solicita cancelar.
  cancelarAlinhamento.store(true);
  cancelarCalibracao.store(true);

  Serial.printf("[ZERAR] Preparando retorno obrigatório. Posicao atual X=%ld Y=%ld\n", logX(), logY());

  {
    const TickType_t inicioEspera = xTaskGetTickCount(); // Tempo inicial.
    const TickType_t timeoutEspera = pdMS_TO_TICKS(5000); // Timeout: 5 segundos.

    while ((alinhamentoTaskHandle != NULL || calibracaoTaskHandle != NULL) &&
           (xTaskGetTickCount() - inicioEspera) < timeoutEspera) {
      vTaskDelay(pdMS_TO_TICKS(10)); // Espera 10 ms antes de verificar de novo.
    }

    // Passaram 5 segundos.
    if (alinhamentoTaskHandle != NULL || calibracaoTaskHandle != NULL) {
      Serial.println("[ZERAR][AVISO] Alguma task nao encerrou a tempo. Forcando retorno mesmo assim.");
    }
  }

  // Impede que outra tarefa tente mexer simultaneamente.
  motorMutex.lock();

  motorX.enableOutputs();
  motorY.enableOutputs();
  motorX.setMaxSpeed(VEL_X);
  motorY.setMaxSpeed(VEL_Y);
  motorX.setAcceleration(ACEL_X);
  motorY.setAcceleration(ACEL_Y);

  long xInicio = logX();
  long yInicio = logY();

  motorX.moveTo(fisicoX(0));
  motorY.moveTo(fisicoY(0));

  // Velocidade mínima de referência para calcular timeout.
  const float menorVelSegura = 80.0f;
  float velRef = VEL_X;
  if (VEL_Y < velRef) velRef = VEL_Y;
  if (velRef < menorVelSegura) velRef = menorVelSegura;

  long maiorDist = labs(xInicio); // valor absoluto de long.
  if (labs(yInicio) > maiorDist) maiorDist = labs(yInicio); // Y > X ? Y : X.

  // Define tempo máximo esperado de retorno (timeout):
  // tempo = distancia/ velocidade, 1000 para ms, fator de segurança 4, soma 30 segundos. 
  unsigned long timeoutMs = (unsigned long)((maiorDist / velRef) * 1000.0f * 4.0f) + 30000UL;
  if (timeoutMs < 120000UL) timeoutMs = 120000UL; // Mínimo: 2 minutos.
  if (timeoutMs > 300000UL) timeoutMs = 300000UL; // Máximo: 5 minutos.

  Serial.printf("[ZERAR] Retornando para origem. DistMax=%ld timeout=%lums\n",
                maiorDist, timeoutMs);

  const unsigned long inicio = millis();
  unsigned long ultimoLog = millis();

  while (motorX.distanceToGo() != 0 || motorY.distanceToGo() != 0) {
    motorX.run();
    motorY.run();

    unsigned long agora = millis();

    // Se já passou 1 segundo desde o último log.
    if (agora - ultimoLog >= 1000UL) {
      Serial.printf("[ZERAR] andando... X=%ld distX=%ld | Y=%ld distY=%ld\n",
                    logX(), motorX.distanceToGo(), logY(), motorY.distanceToGo());
      ultimoLog = agora;
    }

    // Se passou do tempo máximo.
    if (agora - inicio > timeoutMs) {
      Serial.printf("[ZERAR][ERRO] Timeout de emergencia. Ainda faltava distX=%ld distY=%ld\n",
                    motorX.distanceToGo(), motorY.distanceToGo());
      break;
    }

    vTaskDelay(pdMS_TO_TICKS(1));
  }

  // Se X e Y ficaram a 10 passos da origem, força posição atual como zero.
  if (labs(logX()) <= 10) motorX.setCurrentPosition(fisicoX(0));
  if (labs(logY()) <= 10) motorY.setCurrentPosition(fisicoY(0));

  long posX_final = logX(), posY_final = logY();

  if (labs(posX_final) <= 10 && labs(posY_final) <= 10) {
    // Limpa última posição alinhada.
    ultimoX_alinhado   = -1;
    ultimoY_alinhado   = -1;
    deslocamentoAlinhX = 0;
    deslocamentoAlinhY = 0;

    Serial.printf("[ZERAR][OK] Origem atingida. X=%ld Y=%ld\n", logX(), logY());
  } else {
    Serial.printf("[ZERAR][FALHA_REAL] Nao chegou na origem. X=%ld Y=%ld.\n",
                  posX_final, posY_final);
  }

  // Desliga os motores e libera o Mutex.
  desligarMotores();
  motorMutex.unlock(); 

  cancelarAlinhamento.store(false);
  cancelarCalibracao.store(false);
  zerandoRodando.store(false);
  zerarTaskHandle = NULL;

  vTaskDelete(NULL);
}

//  Parâmetros de busca do alinhamento

const int PERDAS_PARA_SAIR = 8;
const float VEL_FINA = 120.0f, ACEL_FINA = 65.0f;
const long RAIO_X = 1800, RAIO_Y = 1800;
const long PASSO_RAPIDO = 280, PASSO_FINO = 120, PASSO_ULTRAFINO = 60;

// Busca em cruz antes do retângulo. Usa distância física do TIL até as bordas dos painéis.
const long MARGEM_CRUZ_X = 1600, MARGEM_CRUZ_Y = 1600;
const long ALCANCE_CRUZ_X = TIL_X_DA_BORDA + MARGEM_CRUZ_X;
const long ALCANCE_CRUZ_Y = TIL_Y_DA_BORDA + MARGEM_CRUZ_Y;
const long OFFSET_BUSCA_BAIXO = 900;
const long PASSO_CRUZ = 220;

// Leitura analógica com base adaptativa
const int CONFIRMA_ENTRA = 3, CONFIRMA_SAI = 3, AMOSTRAS_LEITURA = 8;
const int BLOCOS_CALIBRACAO = 64;
const float SIGMA_PISO_V = 0.001f;

// Ajustes da calibração automática
const float VEL_CAL_X  = 360.0f, VEL_CAL_Y  = 360.0f;
const float ACEL_CAL_X = 140.0f, ACEL_CAL_Y = 140.0f;

const long PASSO_CAL_X = 100, PASSO_CAL_Y = 100;
const long CAL_X_FIM = 8700, CAL_Y_FIM = 8700;

const float SUBIDA_MIN_CAL_SUP_V = 0.080f, SUBIDA_MIN_CAL_DIR_V = 0.100f;
const int MAX_LEITURAS_CAL = 100, TOP_MEDIA_CAL = 10, TOP_PICOS_CAL = 3;
// Para não alternar rápido entre "detectou" e "não detectou".
// Entrada: 55% da amplitude, saída: 30% da amplitude. Pico Mínimo de 45% da amplitude média.
const float FRACAO_ENTRADA_CLARO = 0.55f, FRACAO_SAIDA_CLARO = 0.30f, FRACAO_PICO_CLARO = 0.45f;

const float MARGEM_ENTRADA_MIN_V = 0.040f, MARGEM_ENTRADA_MAX_V = 0.180f;
const float MARGEM_SAIDA_MIN_V   = 0.020f, MARGEM_SAIDA_MAX_V   = 0.100f;
const float PICO_MINIMO_MIN_V    = 0.030f, PICO_MINIMO_MAX_V    = 0.150f;

float margemEntradaSupV = 0.020f, margemEntradaDirV = 0.020f;
float margemSaidaSupV   = 0.010f, margemSaidaDirV   = 0.010f;
float picoMinimoSupV    = 0.015f, picoMinimoDirV    = 0.015f;
float ruidoSupV         = 0.0f,   ruidoDirV         = 0.0f;

float limiarEntradaSup = 0, limiarEntradaDir = 0;
float limiarSaidaSup   = 0, limiarSaidaDir   = 0;

float painelRapidoSup = 0, painelRapidoDir = 0;
float painelBaseSup   = 0, painelBaseDir   = 0;
float painelSubidaSup = 0, painelSubidaDir = 0;
bool  painelAchouSup  = false, painelAchouDir = false;
// "cnt" é de leituras consecutivas.
int   cntEntraSup = 0, cntSaiSup = 0, cntEntraDir = 0, cntSaiDir = 0;

// Se for válida, o código usa amplitudes reais medidas para definir margens melhores.
// Se for falsa, usa margens baseadas principalmente no ruído.
bool  calibracaoDoisPontosValida = false;
float painelClaroSupV = 0.0f, painelClaroDirV = 0.0f;
float amplitudeLaserSupV = 0.0f, amplitudeLaserDirV = 0.0f;
volatile long ultimoDeslocamentoCalX = 0, ultimoDeslocamentoCalY = 0;

//  Calibração dos painéis

// Em mV:
float lerPainelV(int pino) {
  uint64_t somaMv = 0;

  for (int i = 0; i < AMOSTRAS_LEITURA; i++) {
    somaMv += analogReadMilliVolts(pino);
  }

  return (somaMv / (float)AMOSTRAS_LEITURA) / 1000.0f;
}

// Insertion-sort, in place, ordena e pega mediana.
// n par, usa a média dos dois elementos centrais, senão o do meio.
float medianaOrdenando(float* valores, int n) {
  for (int i = 1; i < n; i++) {
    float chave = valores[i];
    int j = i - 1;

    while (j >= 0 && valores[j] > chave) {
      valores[j + 1] = valores[j];
      j--;
    }
    valores[j + 1] = chave;
  }

  if ((n % 2) != 0) return valores[n / 2];
  return 0.5f * (valores[n / 2 - 1] + valores[n / 2]);
}

void calcularBaseERuidoRobustos(float* amostras, int n, float& base, float& sigma) {
  base = medianaOrdenando(amostras, n); // Mediana das amostras.

  for (int i = 0; i < n; i++) {
    amostras[i] = fabsf(amostras[i] - base); // Valor absoluto para float.
  }

  const float mad = medianaOrdenando(amostras, n); // Mediana dos valores absolutos.
  sigma = max(mad / 0.6745f, SIGMA_PISO_V); // Divisor usado porque, em distribuição normal,
  // a mediana dos desvios absolutos fica em torno de 0,6745 * desvio padrão, piso para nunca ser zero.
}

void configurarMargensDeteccao() {
  const float pisoSigmaSup = max(ruidoSupV, SIGMA_PISO_V), pisoSigmaDir = max(ruidoDirV, SIGMA_PISO_V);

  // Limiares proporcionais a resposta do painel.
  // constrain(valor, minimo, maximo), 
  // valor < minimo ? minimo
  // valor > maximo ? maximo
  // : valor
  if (calibracaoDoisPontosValida) {
    margemEntradaSupV = constrain(max(amplitudeLaserSupV * FRACAO_ENTRADA_CLARO,
                                      pisoSigmaSup * 3.0f),
                                  MARGEM_ENTRADA_MIN_V, MARGEM_ENTRADA_MAX_V);
    margemEntradaDirV = constrain(max(amplitudeLaserDirV * FRACAO_ENTRADA_CLARO,
                                      pisoSigmaDir * 3.0f),
                                  MARGEM_ENTRADA_MIN_V, MARGEM_ENTRADA_MAX_V);

    margemSaidaSupV = constrain(max(amplitudeLaserSupV * FRACAO_SAIDA_CLARO,
                                    pisoSigmaSup * 1.6f),
                                MARGEM_SAIDA_MIN_V, MARGEM_SAIDA_MAX_V);
    margemSaidaDirV = constrain(max(amplitudeLaserDirV * FRACAO_SAIDA_CLARO,
                                    pisoSigmaDir * 1.6f),
                                MARGEM_SAIDA_MIN_V, MARGEM_SAIDA_MAX_V);

    picoMinimoSupV = constrain(max(amplitudeLaserSupV * FRACAO_PICO_CLARO,
                                   pisoSigmaSup * 2.4f),
                               PICO_MINIMO_MIN_V, PICO_MINIMO_MAX_V);
    picoMinimoDirV = constrain(max(amplitudeLaserDirV * FRACAO_PICO_CLARO,
                                   pisoSigmaDir * 2.4f),
                               PICO_MINIMO_MIN_V, PICO_MINIMO_MAX_V);
  } 
  
  // Fallback: No mínimo 50 mV, ou 2,5 vezes o ruído, limitado até 120 mV.
  else {
    margemEntradaSupV = constrain(max(2.5f * pisoSigmaSup, 0.050f), 0.050f, 0.120f);
    margemEntradaDirV = constrain(max(2.5f * pisoSigmaDir, 0.050f), 0.050f, 0.130f);
    margemSaidaSupV   = constrain(0.55f * margemEntradaSupV, MARGEM_SAIDA_MIN_V, MARGEM_SAIDA_MAX_V);
    margemSaidaDirV   = constrain(0.55f * margemEntradaDirV, MARGEM_SAIDA_MIN_V, MARGEM_SAIDA_MAX_V);
    picoMinimoSupV    = constrain(0.80f * margemEntradaSupV, PICO_MINIMO_MIN_V, PICO_MINIMO_MAX_V);
    picoMinimoDirV    = constrain(0.80f * margemEntradaDirV, PICO_MINIMO_MIN_V, PICO_MINIMO_MAX_V);
  }

  // Limiares absolutos:
  limiarEntradaSup = painelBaseSup + margemEntradaSupV;
  limiarEntradaDir = painelBaseDir + margemEntradaDirV;
  limiarSaidaSup   = painelBaseSup + margemSaidaSupV;
  limiarSaidaDir   = painelBaseDir + margemSaidaDirV;

  painelRapidoSup = painelBaseSup;
  painelRapidoDir = painelBaseDir;
  painelSubidaSup = painelSubidaDir = 0.0f;
  painelAchouSup = painelAchouDir = false;
  cntEntraSup = cntSaiSup = cntEntraDir = cntSaiDir = 0;
}


bool calibrarPaineis(std::atomic<bool>* flagCancelar) {
  Serial.println("[CAL] Calibrando na origem, sem usar leituras da varredura...");
  vTaskDelay(pdMS_TO_TICKS(400)); // Espera 400 ms, estabilizar.

  if (flagCancelar != NULL && flagCancelar->load()) return false;

  float blocosSup[BLOCOS_CALIBRACAO];
  float blocosDir[BLOCOS_CALIBRACAO];

  for (int i = 0; i < BLOCOS_CALIBRACAO; i++) {
    if (flagCancelar != NULL && flagCancelar->load()) return false;

    blocosSup[i] = lerPainelV(PIN_PAINEL_SUP);
    blocosDir[i] = lerPainelV(PIN_PAINEL_DIR);
    vTaskDelay(pdMS_TO_TICKS(1));
  }

  calcularBaseERuidoRobustos(blocosSup, BLOCOS_CALIBRACAO,
                             painelBaseSup, ruidoSupV);
  calcularBaseERuidoRobustos(blocosDir, BLOCOS_CALIBRACAO,
                             painelBaseDir, ruidoDirV);

  configurarMargensDeteccao();

  Serial.printf("[CAL] SUP base=%.4fV ruidoMAD=%.4fV margemEntrada=%.4fV limiar>=%.4fV pico=%.4fV\n",
                painelBaseSup, ruidoSupV, margemEntradaSupV,
                limiarEntradaSup, picoMinimoSupV);
  Serial.printf("[CAL] DIR base=%.4fV ruidoMAD=%.4fV margemEntrada=%.4fV limiar>=%.4fV pico=%.4fV\n",
                painelBaseDir, ruidoDirV, margemEntradaDirV,
                limiarEntradaDir, picoMinimoDirV);

  return true;
}

void atualizarPaineis() {
  const float vS = lerPainelV(PIN_PAINEL_SUP);
  const float vD = lerPainelV(PIN_PAINEL_DIR);

  painelRapidoSup = vS;
  painelRapidoDir = vD;

  painelSubidaSup = max(0.0f, painelRapidoSup - painelBaseSup);
  painelSubidaDir = max(0.0f, painelRapidoDir - painelBaseDir);

  const bool candS = painelSubidaSup > margemEntradaSupV;
  const bool candD = painelSubidaDir > margemEntradaDirV;

  if (!painelAchouSup) {
    cntEntraSup = candS ? cntEntraSup + 1 : 0;

    if (cntEntraSup >= CONFIRMA_ENTRA) {
      painelAchouSup = true;
      cntEntraSup = 0;
      cntSaiSup = 0;
    }
  } 
  
  else {
    cntSaiSup = (painelSubidaSup < margemSaidaSupV) ? cntSaiSup + 1 : 0;

    if (cntSaiSup >= CONFIRMA_SAI) {
      painelAchouSup = false;
      cntSaiSup = 0;
      cntEntraSup = 0;
    }
  }

  if (!painelAchouDir) {
    cntEntraDir = candD ? cntEntraDir + 1 : 0;

    if (cntEntraDir >= CONFIRMA_ENTRA) {
      painelAchouDir = true;
      cntEntraDir = 0;
      cntSaiDir = 0;
    }
  } 
  
  else {
    cntSaiDir = (painelSubidaDir < margemSaidaDirV) ? cntSaiDir + 1 : 0;

    if (cntSaiDir >= CONFIRMA_SAI) {
      painelAchouDir = false;
      cntSaiDir = 0;
      cntEntraDir = 0;
    }
  }
}

//  Movimentação e controle dos motores

void desligarMotores() {
  motorX.disableOutputs();
  motorY.disableOutputs();
  
  digitalWrite(X_IN1, LOW);
  digitalWrite(X_IN2, LOW);
  digitalWrite(X_IN3, LOW);
  digitalWrite(X_IN4, LOW);

  digitalWrite(Y_IN1, LOW);
  digitalWrite(Y_IN2, LOW);
  digitalWrite(Y_IN3, LOW);
  digitalWrite(Y_IN4, LOW);

  // Garante que as bobinas não fiquem eletrizadas.
  desativarPinoOut(47);
  desativarPinoOut(38);
  desativarPinoOut(21);
  desativarPinoOut(18);
  desativarPinoOut(17);
  desativarPinoOut(10);
  desativarPinoOut(9);
  desativarPinoOut(6);
}

bool encerrarTasksAlinhamento(TickType_t timeoutTicks) {
  cancelarAlinhamento.store(true);
  cancelarCalibracao.store(true);

  const TickType_t inicio = xTaskGetTickCount();

  while ((alinhamentoTaskHandle != NULL || calibracaoTaskHandle != NULL) &&
         (xTaskGetTickCount() - inicio) < timeoutTicks) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  const bool encerrou = (alinhamentoTaskHandle == NULL &&
                         calibracaoTaskHandle == NULL);

  if (encerrou) {
    desligarMotores();
  } else {
    Serial.println("[TASK] Timeout aguardando encerramento cooperativo do alinhamento/calibracao.");
  }

  return encerrou;
}

//  Alinhamento simples

void salvarPosicaoCertaTIL(const char* origem) {
  long x = logX();
  long y = logY();
  
  motorMutex.lock();
  ultimoX_alinhado   = x;
  ultimoY_alinhado   = y;
  deslocamentoAlinhX = x;
  deslocamentoAlinhY = y;
  motorMutex.unlock();

  Serial.printf("[TIL78][CERTO] %s | X=%ld Y=%ld\n",
                origem, x, y);
}

void registrarTILEncontrado(const char* origem) {
  long px = motorX.currentPosition();
  long py = motorY.currentPosition();

  motorX.setCurrentPosition(px);
  motorY.setCurrentPosition(py);

  alinhamentoConcluido.store(true);
  salvarPosicaoCertaTIL(origem);
}

bool leituraTIL78Estavel() {
  int ativos = 0;

  // 11 leituras, com 1 ms entre elas
  for (int i = 0; i < 11; i++) {
    if (cancelarAlinhamento.load()) return false;
    if (laserNoTIL78()) ativos++;
    vTaskDelay(pdMS_TO_TICKS(1));
  }

  // Estável se 7 de 11 leituras detectaram TIL como ativo.
  return ativos >= 7;
}

// Mais rigorosa, não apenas "encostar" no TIL.
bool validarTILParaComunicacao() {
  const int BLOCOS = 4;
  const int AMOSTRAS_POR_BLOCO = 20;
  const int MIN_ATIVOS_POR_BLOCO = 16;

  int blocosOk = 0;

  for (int b = 0; b < BLOCOS; b++) {
    int ativos = 0;

    for (int i = 0; i < AMOSTRAS_POR_BLOCO; i++) {
      if (cancelarAlinhamento.load()) return false;
      if (laserNoTIL78()) ativos++;
      vTaskDelay(pdMS_TO_TICKS(2));
    }

    Serial.printf("[VALIDA_TIL] bloco=%d ativos=%d/%d\n",
                  b + 1, ativos, AMOSTRAS_POR_BLOCO);

    if (ativos >= MIN_ATIVOS_POR_BLOCO) blocosOk++;
  }

  Serial.printf("[VALIDA_TIL] blocosOk=%d/%d\n", blocosOk, BLOCOS);
  return blocosOk == BLOCOS;
}

// Move e registra se achar.
void moverBruto(long ax, long ay) {
  // Limitar dentro da área segura.
  ax = clip(ax, X_MIN, X_MAX);
  ay = clip(ay, Y_MIN, Y_MAX);

  motorMutex.lock();
  motorX.setMaxSpeed(VEL_FINA);
  motorX.setAcceleration(ACEL_FINA);
  motorY.setMaxSpeed(VEL_FINA);
  motorY.setAcceleration(ACEL_FINA);

  motorX.moveTo(fisicoX(ax));
  motorY.moveTo(fisicoY(ay));

  while ((motorX.distanceToGo() || motorY.distanceToGo()) && !cancelarAlinhamento.load()) {
    motorX.run();
    motorY.run();

    if (laserNoTIL78() && leituraTIL78Estavel()) {
      motorMutex.unlock();
      registrarTILEncontrado("moverBruto");
      return;
    }

    vTaskDelay(pdMS_TO_TICKS(1));
  }
  motorMutex.unlock();
}

// True se conseguiu terminar sem cancelar, false se o contrário.
bool moverSemPararNoTIL(long ax, long ay) {
  ax = clip(ax, X_MIN, X_MAX);
  ay = clip(ay, Y_MIN, Y_MAX);

  motorMutex.lock();
  motorX.setMaxSpeed(VEL_X);
  motorX.setAcceleration(ACEL_X);
  motorY.setMaxSpeed(VEL_Y);
  motorY.setAcceleration(ACEL_Y);

  motorX.moveTo(fisicoX(ax));
  motorY.moveTo(fisicoY(ay));

  while ((motorX.distanceToGo() || motorY.distanceToGo()) && !cancelarAlinhamento.load()) {
    motorX.run();
    motorY.run();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  motorMutex.unlock();

  return !cancelarAlinhamento.load();
}

// Move e informa se achou.
bool moverObservandoTIL(long ax, long ay, const char* origem) {
  ax = clip(ax, X_MIN, X_MAX);
  ay = clip(ay, Y_MIN, Y_MAX);

  motorMutex.lock();
  motorX.setMaxSpeed(VEL_FINA);
  motorX.setAcceleration(ACEL_FINA);
  motorY.setMaxSpeed(VEL_FINA);
  motorY.setAcceleration(ACEL_FINA);

  motorX.moveTo(fisicoX(ax));
  motorY.moveTo(fisicoY(ay));

  while ((motorX.distanceToGo() || motorY.distanceToGo()) && !cancelarAlinhamento.load()) {
    motorX.run();
    motorY.run();

    if (laserNoTIL78() && leituraTIL78Estavel()) {
      motorMutex.unlock();
      registrarTILEncontrado(origem);
      return true;
    }

    vTaskDelay(pdMS_TO_TICKS(1));
  }
  motorMutex.unlock();

  // Verificação extra com motor parado.
  if (!cancelarAlinhamento.load() && laserNoTIL78() && leituraTIL78Estavel()) {
    registrarTILEncontrado(origem);
    return true;
  }

  return false;
}

bool detectarPainelSuperior() {
  // Maior entre 12 mV e 1,2 vezes o ruído.
  const float separacaoMin = max(0.012f, ruidoSupV * 1.2f);
  return painelAchouSup ||
         (painelSubidaSup >= picoMinimoSupV &&
          painelSubidaSup >= painelSubidaDir + separacaoMin);
}

bool detectarPainelDireito() {
  // Maior entre 12 mV e 1,2 vezes o ruído.
  const float separacaoMin = max(0.012f, ruidoDirV * 1.2f);
  return painelAchouDir ||
         (painelSubidaDir >= picoMinimoDirV &&
          painelSubidaDir >= painelSubidaSup + separacaoMin);
}

// Buscar qualquer painel: superior ou direito.
bool procurarPrimeiroPainel(AlvoParcial& alvo) {
  alvo.temX = false;
  alvo.temY = false;
  alvo.x = -1;
  alvo.y = -1;

  Serial.println("[BUSCA] Procurando primeiro painel por leitura confirmada.");

  bool esquerdaParaDireita = true;

  // Varre em linhas (zigue-zague)
  for (long y = Y_MIN; y <= Y_MAX && !cancelarAlinhamento.load() && !alinhamentoConcluido.load(); y += PASSO_GROSSO) {
    long xInicio = esquerdaParaDireita ? X_MIN : X_MAX;
    long xFim    = esquerdaParaDireita ? X_MAX : X_MIN;
    if (!moverSemPararNoTIL(xInicio, y)) return false;

    motorMutex.lock();
    motorX.setMaxSpeed(VEL_X);
    motorX.setAcceleration(ACEL_X);
    motorX.moveTo(fisicoX(xFim));

    while (motorX.distanceToGo() && !cancelarAlinhamento.load() && !alinhamentoConcluido.load()) {
      motorX.run();
      atualizarPaineis();

      long x = logX();
      long yy = logY();

      // A) Achou o TIL direto? Encerra.
      if (laserNoTIL78() && leituraTIL78Estavel()) {
        motorMutex.unlock();
        registrarTILEncontrado("procurarPrimeiroPainel");
        return true;
      }

      // B) Detectou painel superior? Sabe estimar o X do TIL.  
      if (detectarPainelSuperior()) {
        alvo.temX = true;
        alvo.x = clip(x + TIL_X_DA_BORDA, X_MIN, X_MAX);
        Serial.printf("[PAINEL] SUP detectado em X=%ld Y=%ld -> alvoX=%ld\n", x, yy, alvo.x);
        motorX.stop();
        while (motorX.distanceToGo() && !cancelarAlinhamento.load()) { motorX.run(); vTaskDelay(pdMS_TO_TICKS(1)); }
        motorMutex.unlock();
        return true;
      }

      // C) Detectou painel direito? Sabe estimar o Y do TIL. 
      if (detectarPainelDireito()) {
        alvo.temY = true;
        alvo.y = clip(yy + TIL_Y_DA_BORDA, Y_MIN, Y_MAX);
        Serial.printf("[PAINEL] DIR detectado em X=%ld Y=%ld -> alvoY=%ld\n", x, yy, alvo.y);
        motorX.stop();
        while (motorX.distanceToGo() && !cancelarAlinhamento.load()) { motorX.run(); vTaskDelay(pdMS_TO_TICKS(1)); }
        motorMutex.unlock();
        return true;
      }

      vTaskDelay(pdMS_TO_TICKS(1));
    }
    motorMutex.unlock();

    esquerdaParaDireita = !esquerdaParaDireita;
  }

  Serial.println("[BUSCA] Nenhum painel detectado.");
  return false;
}

// Completa a coordenada restante: já encontrou X com superior.
bool completarYComPainelDireito(AlvoParcial& alvo) {
  if (!alvo.temX) return false;

  // X estimado e limita na área segura.
  long x = clip(alvo.x, X_MIN, X_MAX);
  Serial.printf("[BUSCA] X conhecido=%ld. Procurando DIR para completar Y.\n", x);

  if (!moverSemPararNoTIL(x, Y_MIN)) return false;

  motorMutex.lock();
  motorY.setMaxSpeed(VEL_Y);
  motorY.setAcceleration(ACEL_Y);
  motorY.moveTo(fisicoY(Y_MAX));

  while (motorY.distanceToGo() && !cancelarAlinhamento.load() && !alinhamentoConcluido.load()) {
    motorY.run();
    atualizarPaineis();

    long y = logY();

    // Se achar TIL direto.
    if (laserNoTIL78() && leituraTIL78Estavel()) {
      motorMutex.unlock();
      registrarTILEncontrado("completarYComPainelDireito");
      return true;
    }

    // Não para bruscamente com stop, desacelera, chamada de run() permanece no while.
    if (detectarPainelDireito()) {
      alvo.temY = true;
      alvo.y = clip(y + TIL_Y_DA_BORDA, Y_MIN, Y_MAX);
      Serial.printf("[PAINEL] DIR detectado em X=%ld Y=%ld -> alvoY=%ld\n", logX(), y, alvo.y);
      motorY.stop();
      while (motorY.distanceToGo() && !cancelarAlinhamento.load()) { motorY.run(); vTaskDelay(pdMS_TO_TICKS(1)); }
      motorMutex.unlock();
      return true;
    }

    vTaskDelay(pdMS_TO_TICKS(1));
  }
  motorMutex.unlock();

  Serial.println("[BUSCA] Nao completou Y pelo painel direito.");
  return false;
}

// Completa a coordenada restante: já encontrou Y com direito.
bool completarXComPainelSuperior(AlvoParcial& alvo) {
  if (!alvo.temY) return false;

  // Y estimado e limita na área segura.
  long y = clip(alvo.y, Y_MIN, Y_MAX);
  Serial.printf("[BUSCA] Y conhecido=%ld. Procurando SUP para completar X.\n", y);

  if (!moverSemPararNoTIL(X_MIN, y)) return false;

  motorMutex.lock();
  motorX.setMaxSpeed(VEL_X);
  motorX.setAcceleration(ACEL_X);
  motorX.moveTo(fisicoX(X_MAX));

  while (motorX.distanceToGo() && !cancelarAlinhamento.load() && !alinhamentoConcluido.load()) {
    motorX.run();
    atualizarPaineis();

    long x = logX();

    // Se achar TIL direto.
    if (laserNoTIL78() && leituraTIL78Estavel()) {
      motorMutex.unlock();
      registrarTILEncontrado("completarXComPainelSuperior");
      return true;
    }

    // Não para bruscamente com stop, desacelera, chamada de run() permanece no while.
    if (detectarPainelSuperior()) {
      alvo.temX = true;
      alvo.x = clip(x + TIL_X_DA_BORDA, X_MIN, X_MAX);
      Serial.printf("[PAINEL] SUP detectado em X=%ld Y=%ld -> alvoX=%ld\n", x, logY(), alvo.x);
      motorX.stop();
      while (motorX.distanceToGo() && !cancelarAlinhamento.load()) { motorX.run(); vTaskDelay(pdMS_TO_TICKS(1)); }
      motorMutex.unlock();
      return true;
    }

    vTaskDelay(pdMS_TO_TICKS(1));
  }
  motorMutex.unlock();

  Serial.println("[BUSCA] Nao completou X pelo painel superior.");
  return false;
}

//  Centralização minimal do TIL78

// Move sem gravar como posição correta do TIL. Testar pontos sem sobrescrever último bom
bool moverCentroSemSalvar(long ax, long ay) {
  ax = clip(ax, X_MIN, X_MAX);
  ay = clip(ay, Y_MIN, Y_MAX);

  motorMutex.lock();
  motorX.setMaxSpeed(VEL_FINA);
  motorX.setAcceleration(ACEL_FINA);
  motorY.setMaxSpeed(VEL_FINA);
  motorY.setAcceleration(ACEL_FINA);

  motorX.moveTo(fisicoX(ax));
  motorY.moveTo(fisicoY(ay));

  while ((motorX.distanceToGo() || motorY.distanceToGo()) && !cancelarAlinhamento.load()) {
    motorX.run();
    motorY.run();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  motorMutex.unlock();

  return !cancelarAlinhamento.load();
}

// 20 leituras com 2 ms entre elas. Incrementa enquanto TIL ativo.
int contarTILAtivo(int amostras, int intervaloMs) {
  int ativos = 0;
  for (int i = 0; i < amostras; i++) {
    if (cancelarAlinhamento.load()) return 0;
    if (laserNoTIL78()) ativos++;
    vTaskDelay(pdMS_TO_TICKS(intervaloMs));
  }
  return ativos;
}

// Garante que ponto atual é de TIL estável e salva (proteção).
bool garantirPontoTILAntesDeCentralizar(const char* origem) {
  if (leituraTIL78Estavel()) {
    salvarPosicaoCertaTIL(origem);
    return true;
  }

  // Se não está estável, tenta voltar ao último ponto conhecido.
  if (ultimoX_alinhado >= 0 && ultimoY_alinhado >= 0) {
    Serial.printf("[CENTRO] TIL oscilou. Voltando ao último ponto certo X=%ld Y=%ld.\n",
                  ultimoX_alinhado, ultimoY_alinhado);
    moverCentroSemSalvar(ultimoX_alinhado, ultimoY_alinhado);
    if (leituraTIL78Estavel()) {
      salvarPosicaoCertaTIL("ultimo-ponto-certo");
      return true;
    }
  }

  Serial.println("[CENTRO] Sem ponto certo de TIL. Centralização recusada.");
  return false;
}

// Procura borda ativa do TIL no eixo X. Y fixo na busca. +1 é direita e -1 é esquerda.
long acharBordaTIL_X(long xInicio, long yFixo, int dir, long passo) {
  long xAtivo = xInicio;
  long xAtual = xInicio;
  int perdasConsecutivas = 0;

  while (!cancelarAlinhamento.load()) {
    xAtual += dir * passo;
    if (xAtual < X_MIN || xAtual > X_MAX) break;

    moverCentroSemSalvar(xAtual, yFixo);

    if (leituraTIL78Estavel()) {
      xAtivo = xAtual;
      perdasConsecutivas = 0;
      salvarPosicaoCertaTIL("borda-X-ainda-ativo");
    } else {
      perdasConsecutivas++;
      if (perdasConsecutivas >= 2) break;
    }
  }

  // Retorna último ponto bom antes da perda.
  return xAtivo;
}

// Procura borda ativa do TIL no eixo Y. X fixo na busca. +1 é cima e -1 é baixo.
long acharBordaTIL_Y(long xFixo, long yInicio, int dir, long passo) {
  long yAtivo = yInicio;
  long yAtual = yInicio;
  int perdasConsecutivas = 0;

  while (!cancelarAlinhamento.load()) {
    yAtual += dir * passo;
    if (yAtual < Y_MIN || yAtual > Y_MAX) break;

    moverCentroSemSalvar(xFixo, yAtual);

    if (leituraTIL78Estavel()) {
      yAtivo = yAtual;
      perdasConsecutivas = 0;
      salvarPosicaoCertaTIL("borda-Y-ainda-ativo");
    } else {
      perdasConsecutivas++;
      if (perdasConsecutivas >= 2) break;
    }
  }

  // Retorna último ponto bom antes da perda.
  return yAtivo;
}

bool centralizarTILMinimal() {
  // Exige que o TIL esteja ativo ou que exista último ponto confiável.
  if (!garantirPontoTILAntesDeCentralizar("inicio-centralizacao")) return false;

  long xCentro = logX();
  long yCentro = logY();

  // Lista de passos, vai refinando.
  const long passos[] = {25, 12, 5, 5};
  // Calcula automaticamente quantos elementos há no vetor passos.
  const int totalIter = sizeof(passos) / sizeof(passos[0]);

  // Repete para cada passo, desde que não tenha cancelamento.
  for (int i = 0; i < totalIter && !cancelarAlinhamento.load(); i++) {
    long passo = passos[i];
    Serial.printf("[CENTRO] Iter %d/%d passo=%ld ponto X=%ld Y=%ld\n",
                  i + 1, totalIter, passo, xCentro, yCentro);

    // Parte do centro atual, mantém Y fixo, anda para +X.
    long xDir = acharBordaTIL_X(xCentro, yCentro, +1, passo);
    moverCentroSemSalvar(xCentro, yCentro);
    if (!garantirPontoTILAntesDeCentralizar("retorno-centro-X")) return false;

    // Parte do centro atual, mantém Y fixo, anda para -X.
    long xEsq = acharBordaTIL_X(xCentro, yCentro, -1, passo);
    long novoX = (xEsq + xDir) / 2;
    Serial.printf("[CENTRO] X esq=%ld dir=%ld -> novoX=%ld\n", xEsq, xDir, novoX);

    moverCentroSemSalvar(novoX, yCentro);
    if (leituraTIL78Estavel()) {
      xCentro = logX();
      yCentro = logY();
      salvarPosicaoCertaTIL("centro-X");
    } else {
      Serial.println("[CENTRO] Novo X perdeu TIL. Voltando ao ponto certo.");
      moverCentroSemSalvar(ultimoX_alinhado, ultimoY_alinhado);
      if (!garantirPontoTILAntesDeCentralizar("reversao-X")) return false;
      xCentro = logX();
      yCentro = logY();
    }

    // Parte do centro atual, mantém X fixo, anda para +Y.
    long yTop = acharBordaTIL_Y(xCentro, yCentro, +1, passo);
    moverCentroSemSalvar(xCentro, yCentro);
    if (!garantirPontoTILAntesDeCentralizar("retorno-centro-Y")) return false;

    // Parte do centro atual, mantém X fixo, anda para -Y
    long yBot = acharBordaTIL_Y(xCentro, yCentro, -1, passo);
    long novoY = (yBot + yTop) / 2;
    Serial.printf("[CENTRO] Y bot=%ld top=%ld -> novoY=%ld\n", yBot, yTop, novoY);

    moverCentroSemSalvar(xCentro, novoY);
    if (leituraTIL78Estavel()) {
      xCentro = logX();
      yCentro = logY();
      salvarPosicaoCertaTIL("centro-Y");
    } 
    else {
      Serial.println("[CENTRO] Novo Y perdeu TIL. Voltando ao ponto certo.");
      moverCentroSemSalvar(ultimoX_alinhado, ultimoY_alinhado);
      if (!garantirPontoTILAntesDeCentralizar("reversao-Y")) return false;
      xCentro = logX();
      yCentro = logY();
    }
  }

  moverCentroSemSalvar(ultimoX_alinhado, ultimoY_alinhado);

  if (!leituraTIL78Estavel()) {
    Serial.println("[CENTRO] Final sem TIL estável.");
    return false;
  }

  salvarPosicaoCertaTIL("centro-final");

  if (!validarTILParaComunicacao()) {
    Serial.println("[CENTRO] Centralizou, mas validação de comunicação falhou.");
    return false;
  }

  Serial.printf("[CENTRO] OK centralizado e validado em X=%ld Y=%ld\n", logX(), logY());
  return true;
}

bool confirmarEGuardarTIL() {
  if (!garantirPontoTILAntesDeCentralizar("confirmar")) return false;
  return centralizarTILMinimal();
}

//  Alinhamento por fases
//  Mantido dentro da base segura com atomic + motorMutex.

bool irParaAlinhamento(long ax, long ay) {
  ax = clip(ax, X_MIN, X_MAX);
  ay = clip(ay, Y_MIN, Y_MAX);

  motorMutex.lock();
  motorX.setMaxSpeed(VEL_X);
  motorX.setAcceleration(ACEL_X);
  motorY.setMaxSpeed(VEL_Y);
  motorY.setAcceleration(ACEL_Y);

  motorX.moveTo(fisicoX(ax));
  motorY.moveTo(fisicoY(ay));

  // Move os motores, atualiza os painéis e verifica se o TIL foi atingido.
  while ((motorX.distanceToGo() || motorY.distanceToGo()) && !cancelarAlinhamento.load()) {
    motorX.run();
    motorY.run();
    atualizarPaineis();

    if (laserNoTIL78() && leituraTIL78Estavel()) {
      motorX.stop();
      motorY.stop();
      while ((motorX.distanceToGo() || motorY.distanceToGo()) && !cancelarAlinhamento.load()) {
        // Chama run() até a desaceleração terminar.
        motorX.run();
        motorY.run();
        vTaskDelay(pdMS_TO_TICKS(1));
      }
      motorMutex.unlock();
      registrarTILEncontrado("irParaAlinhamento");
      return false; // Significa que já encontrou o TIL, então não continua rota normal.
    }

    vTaskDelay(pdMS_TO_TICKS(1));
  }

  motorMutex.unlock();
  return !cancelarAlinhamento.load();
}

// Versão fina da anterior que retorna true se achar TIL, usada nas buscas em retângulo e em cruz.
bool irParaFinoAlinhamento(long ax, long ay) {
  ax = clip(ax, X_MIN, X_MAX);
  ay = clip(ay, Y_MIN, Y_MAX);

  motorMutex.lock();
  motorX.setMaxSpeed(VEL_FINA);
  motorX.setAcceleration(ACEL_FINA);
  motorY.setMaxSpeed(VEL_FINA);
  motorY.setAcceleration(ACEL_FINA);

  motorX.moveTo(fisicoX(ax));
  motorY.moveTo(fisicoY(ay));

  while ((motorX.distanceToGo() || motorY.distanceToGo()) && !cancelarAlinhamento.load()) {
    motorX.run();
    motorY.run();
    atualizarPaineis();

    if (laserNoTIL78() && leituraTIL78Estavel()) {
      motorX.stop();
      motorY.stop();
      while ((motorX.distanceToGo() || motorY.distanceToGo()) && !cancelarAlinhamento.load()) {
        motorX.run();
        motorY.run();
        vTaskDelay(pdMS_TO_TICKS(1));
      }
      motorMutex.unlock();
      registrarTILEncontrado("irParaFinoAlinhamento");
      return true;
    }

    vTaskDelay(pdMS_TO_TICKS(1));
  }

  motorMutex.unlock();

  if (!cancelarAlinhamento.load() && laserNoTIL78() && leituraTIL78Estavel()) {
    registrarTILEncontrado("irParaFinoAlinhamento-final");
    return true;
  }

  return false;
}

// Em retângulo ao redor do centro estimado. Varredura em zigue-zague.
bool varrerRetanguloTIL(long xC, long yC, long rX, long rY, long passoY) {
  long x0 = clip(xC - rX, X_MIN, X_MAX);
  long x1 = clip(xC + rX, X_MIN, X_MAX);
  long y0 = clip(yC - rY, Y_MIN, Y_MAX);
  long y1 = clip(yC + rY, Y_MIN, Y_MAX);

  bool esquerdaParaDireita = true;

  for (long y = y0; y <= y1 && !cancelarAlinhamento.load() && !alinhamentoConcluido.load(); y += passoY) {
    if (irParaFinoAlinhamento(esquerdaParaDireita ? x0 : x1, y)) return true;
    if (irParaFinoAlinhamento(esquerdaParaDireita ? x1 : x0, y)) return true;
    esquerdaParaDireita = !esquerdaParaDireita;
  }

  return false;
}

// Muito mais rápida que o retângulo, porque não cobre toda a área. 
// Ela procura primeiro nas direções mais prováveis.
bool varrerCruzTIL(long xC, long yC, long alcanceX, long alcanceY, long passo) {
  xC = clip(xC, X_MIN, X_MAX);
  yC = clip(yC, Y_MIN, Y_MAX);

  Serial.printf("[CRUZ] Centro X=%ld Y=%ld alcanceX=%ld alcanceY=%ld passo=%ld\n",
                xC, yC, alcanceX, alcanceY, passo);

  // Garante que começa pelo centro.
  if (moverObservandoTIL(xC, yC, "CRUZ-CENTRO")) {
    return true;
  }

  // 1) Desce primeiro, porque você comentou que pode precisar ir mais para baixo.
  for (long d = passo; d <= alcanceY; d += passo) {
    if (cancelarAlinhamento.load()) return false;

    long y = clip(yC + d, Y_MIN, Y_MAX);
    if (moverObservandoTIL(xC, y, "CRUZ-Y-BAIXO")) {
      return true;
    }
  }

  // Volta ao centro antes de subir.
  moverSemPararNoTIL(xC, yC);

  // 2) Sobe.
  for (long d = passo; d <= alcanceY; d += passo) {
    if (cancelarAlinhamento.load()) return false;

    long y = clip(yC - d, Y_MIN, Y_MAX);
    if (moverObservandoTIL(xC, y, "CRUZ-Y-CIMA")) {
      return true;
    }
  }

  // Volta ao centro antes de ir para os lados.
  moverSemPararNoTIL(xC, yC);

  // 3) Vai para a direita.
  for (long d = passo; d <= alcanceX; d += passo) {
    if (cancelarAlinhamento.load()) return false;

    long x = clip(xC + d, X_MIN, X_MAX);
    if (moverObservandoTIL(x, yC, "CRUZ-X-DIREITA")) {
      return true;
    }
  }

  // Volta ao centro.
  moverSemPararNoTIL(xC, yC);

  // 4) Vai para a esquerda.
  for (long d = passo; d <= alcanceX; d += passo) {
    if (cancelarAlinhamento.load()) return false;

    long x = clip(xC - d, X_MIN, X_MAX);
    if (moverObservandoTIL(x, yC, "CRUZ-X-ESQUERDA")) {
      return true;
    }
  }

  moverSemPararNoTIL(xC, yC);

  Serial.println("[CRUZ] TIL nao encontrado na cruz.");
  return false;
}

bool buscaFinalTIL(long xC, long yC, long raio_fator) {
  Serial.printf("[FINAL] Iniciando busca fina do terceiro codigo. fator=%ld\n", raio_fator);

  xC = clip(xC, X_MIN, X_MAX);
  // Empurra o centro um pouco para baixo.
  // Se na prática for para o lado errado, troque + por -.
  yC = clip(yC + OFFSET_BUSCA_BAIXO, Y_MIN, Y_MAX);

  Serial.printf("[BUSCA_FINAL] Centro ajustado X=%ld Y=%ld fator=%ld\n",
                xC, yC, raio_fator);

  // 1) Primeiro tenta cruz grande e rápida.
  if (varrerCruzTIL(
        xC,
        yC,
        ALCANCE_CRUZ_X * raio_fator,
        ALCANCE_CRUZ_Y * raio_fator,
        PASSO_CRUZ
      )) {
    Serial.println("[BUSCA_FINAL] TIL encontrado pela busca em cruz.");
    return true;
  }

  // 2) Depois tenta o retângulo rápido.
  if (varrerRetanguloTIL(xC, yC, RAIO_X * raio_fator, RAIO_Y * raio_fator, PASSO_RAPIDO)) {
    return true;
  }

  // 3) Depois tenta o retângulo fino.
  if (varrerRetanguloTIL(xC, yC, RAIO_X * raio_fator, RAIO_Y * raio_fator, PASSO_FINO)) {
    return true;
  }

  // 4) Última tentativa ultrafina, mas numa área menor.
  if (varrerRetanguloTIL(xC, yC, (RAIO_X * raio_fator) / 2, (RAIO_Y * raio_fator) / 2, PASSO_ULTRAFINO)) {
    return true;
  }

  return false;
}

// Encontrar uma posição em X onde o laser começa a aparecer no painel superior.
// Alimenta a estimativa de alvo.x.
ResultadoPainel buscarBordaX(long y, long& yBonus) {
  if (!irParaAlinhamento(X_MIN, y)) return {-1, false, false};

  motorMutex.lock();
  motorX.setMaxSpeed(VEL_X);
  motorX.setAcceleration(ACEL_X);
  motorX.moveTo(fisicoX(X_MAX));

  long xIniBruto = -1;
  long xPico = -1;
  float picoVal = 0.0f;
  bool entrouBruto = false;

  while (motorX.distanceToGo() && !cancelarAlinhamento.load() && !alinhamentoConcluido.load()) {
    motorX.run();
    atualizarPaineis();

    if (laserNoTIL78() && leituraTIL78Estavel()) {
      motorX.stop();
      while (motorX.distanceToGo() && !cancelarAlinhamento.load()) {
        motorX.run();
        vTaskDelay(pdMS_TO_TICKS(1));
      }
      motorMutex.unlock();
      registrarTILEncontrado("buscarBordaX");
      return {-1, false, false};
    }

    long x = logX();

    if (detectarPainelSuperior()) {
      motorX.stop();
      while (motorX.distanceToGo() && !cancelarAlinhamento.load()) {
        motorX.run();
        vTaskDelay(pdMS_TO_TICKS(1));
      }
      motorMutex.unlock();

      long xTIL = clip(x + TIL_X_DA_BORDA, X_MIN, X_MAX);
      Serial.printf("[FASE1] Borda SUP confirmada em X=%ld -> xTIL=%ld\n", x, xTIL);
      return {xTIL, true, true};
    }

    if (painelSubidaSup > picoVal) {
      picoVal = painelSubidaSup;
      xPico = x;
    }

    if (painelSubidaSup > margemSaidaSupV && !entrouBruto) {
      entrouBruto = true;
      xIniBruto = x;
    }

    if (yBonus < 0 && detectarPainelDireito()) {
      yBonus = y;
      Serial.printf("[FASE1][BONUS] Painel DIR visto em Y=%ld durante varredura X\n", y);
    }

    vTaskDelay(pdMS_TO_TICKS(1));
  }

  while (motorX.distanceToGo() && !cancelarAlinhamento.load()) {
    motorX.run();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  motorMutex.unlock();

  if (cancelarAlinhamento.load() || alinhamentoConcluido.load()) return {-1, false, false};

  if (entrouBruto) {
    long xTIL = clip(xIniBruto + TIL_X_DA_BORDA, X_MIN, X_MAX);
    Serial.printf("[FASE1] Borda SUP estimada em X=%ld -> xTIL=%ld\n", xIniBruto, xTIL);
    return {xTIL, false, true};
  }

  if (xPico >= 0 && picoVal >= picoMinimoSupV) {
    long xTIL = clip(xPico + TIL_X_DA_BORDA, X_MIN, X_MAX);
    Serial.printf("[FASE1] Usando pico SUP em X=%ld pico=%.4f -> xTIL=%ld\n", xPico, picoVal, xTIL);
    return {xTIL, false, true};
  }

  return {-1, false, false};
}


// Encontrar uma posição em Y onde o laser começa a aparecer no painel superior.
// Alimenta a estimativa de alvo.y.
ResultadoPainel buscarBordaY(long x, long& xBonus) {
  if (!irParaAlinhamento(x, Y_MIN)) return {-1, false, false};

  motorMutex.lock();
  motorY.setMaxSpeed(VEL_Y);
  motorY.setAcceleration(ACEL_Y);
  motorY.moveTo(fisicoY(Y_MAX));

  long yIniBruto = -1;
  long yPico = -1;
  float picoVal = 0.0f;
  bool entrouBruto = false;

  while (motorY.distanceToGo() && !cancelarAlinhamento.load() && !alinhamentoConcluido.load()) {
    motorY.run();
    atualizarPaineis();

    if (laserNoTIL78() && leituraTIL78Estavel()) {
      motorY.stop();
      while (motorY.distanceToGo() && !cancelarAlinhamento.load()) {
        motorY.run();
        vTaskDelay(pdMS_TO_TICKS(1));
      }
      motorMutex.unlock();
      registrarTILEncontrado("buscarBordaY");
      return {-1, false, false};
    }

    long y = logY();

    if (detectarPainelDireito()) {
      motorY.stop();
      while (motorY.distanceToGo() && !cancelarAlinhamento.load()) {
        motorY.run();
        vTaskDelay(pdMS_TO_TICKS(1));
      }
      motorMutex.unlock();

      long yTIL = clip(y + TIL_Y_DA_BORDA, Y_MIN, Y_MAX);
      Serial.printf("[FASE2] Borda DIR confirmada em Y=%ld -> yTIL=%ld\n", y, yTIL);
      return {yTIL, true, true};
    }

    if (painelSubidaDir > picoVal) {
      picoVal = painelSubidaDir;
      yPico = y;
    }

    if (painelSubidaDir > margemSaidaDirV && !entrouBruto) {
      entrouBruto = true;
      yIniBruto = y;
    }

    if (xBonus < 0 && detectarPainelSuperior()) {
      xBonus = x;
      Serial.printf("[FASE2][BONUS] Painel SUP visto em X=%ld durante varredura Y\n", x);
    }

    vTaskDelay(pdMS_TO_TICKS(1));
  }

  while (motorY.distanceToGo() && !cancelarAlinhamento.load()) {
    motorY.run();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  motorMutex.unlock();

  if (cancelarAlinhamento.load() || alinhamentoConcluido.load()) return {-1, false, false};

  if (entrouBruto) {
    long yTIL = clip(yIniBruto + TIL_Y_DA_BORDA, Y_MIN, Y_MAX);
    Serial.printf("[FASE2] Borda DIR estimada em Y=%ld -> yTIL=%ld\n", yIniBruto, yTIL);
    return {yTIL, false, true};
  }

  if (yPico >= 0 && picoVal >= picoMinimoDirV) {
    long yTIL = clip(yPico + TIL_Y_DA_BORDA, Y_MIN, Y_MAX);
    Serial.printf("[FASE2] Usando pico DIR em Y=%ld pico=%.4f -> yTIL=%ld\n", yPico, picoVal, yTIL);
    return {yTIL, false, true};
  }

  return {-1, false, false};
}

ResultadoPainel encontrarX(long& yBonus) {
  yBonus = -1;
  long melhorEstX = -1;
  bool temEstimativa = false;

  for (long y = Y_MIN; y <= Y_MAX && !cancelarAlinhamento.load() && !alinhamentoConcluido.load(); y += PASSO_GROSSO) {
    ResultadoPainel r = buscarBordaX(y, yBonus);
    if (cancelarAlinhamento.load() || alinhamentoConcluido.load()) return {-1, false, false};

    if (r.encontrou) {
      if (r.confiavel) return r;
      if (!temEstimativa) {
        melhorEstX = r.pos;
        temEstimativa = true;
      }
    }
  }

  if (temEstimativa) {
    Serial.printf("[X] Nenhuma borda confiavel. Usando estimativa X=%ld\n", melhorEstX);
    return {melhorEstX, false, true};
  }

  Serial.println("[X] Painel superior nao encontrado.");
  return {-1, false, false};
}

ResultadoPainel encontrarY(long xInicial, long& xBonus) {
  xBonus = -1;
  long melhorEstY = -1;
  bool temEstimativa = false;

  if (xInicial >= 0) {
    long xc = clip(xInicial, X_MIN, X_MAX);
    ResultadoPainel r = buscarBordaY(xc, xBonus);
    if (cancelarAlinhamento.load() || alinhamentoConcluido.load()) return {-1, false, false};

    if (r.encontrou) {
      if (r.confiavel) return r;
      melhorEstY = r.pos;
      temEstimativa = true;
    }
  }

  for (long x = X_MIN; x <= X_MAX && !cancelarAlinhamento.load() && !alinhamentoConcluido.load(); x += PASSO_GROSSO) {
    if (xInicial >= 0 && labs(x - xInicial) < PASSO_GROSSO) continue;

    ResultadoPainel r = buscarBordaY(x, xBonus);
    if (cancelarAlinhamento.load() || alinhamentoConcluido.load()) return {-1, false, false};

    if (r.encontrou) {
      if (r.confiavel) return r;
      if (!temEstimativa) {
        melhorEstY = r.pos;
        temEstimativa = true;
      }
    }
  }

  if (temEstimativa) {
    Serial.printf("[Y] Nenhuma borda confiavel. Usando estimativa Y=%ld\n", melhorEstY);
    return {melhorEstY, false, true};
  }

  Serial.println("[Y] Painel direito nao encontrado.");
  return {-1, false, false};
}

//  Ajuste manual serial de contingência.
void imprimirPosicaoManual() {
  Serial.print("[MANUAL][POSICAO] X = ");
  Serial.print(logX());
  Serial.print(" passos | Y = ");
  Serial.print(logY());
  Serial.println(" passos");
}

void imprimirAjudaManual() {
  Serial.println();
  Serial.println("==============================================");
  Serial.println(" AJUSTE MANUAL DE PASSOS");
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
  Serial.println("==============================================");
  Serial.println();
}

void entrarModoAjusteManual(const char* motivo) {
  ajusteManualHabilitado.store(true);
  movimentoManualAtual = MANUAL_PARADO;
  motorMutex.lock();
  motorX.enableOutputs();
  motorY.enableOutputs();
  motorX.setMaxSpeed(VEL_X);
  motorX.setAcceleration(ACEL_X);
  motorY.setMaxSpeed(VEL_Y);
  motorY.setAcceleration(ACEL_Y);
  motorMutex.unlock();
  Serial.print("[MANUAL] Ajuste manual habilitado. Motivo: ");
  Serial.println(motivo);
  imprimirPosicaoManual();
  imprimirAjudaManual();
}

void pararMotoresManual() {
  movimentoManualAtual = MANUAL_PARADO;
  motorMutex.lock();
  motorX.stop();
  motorY.stop();
  while (motorX.distanceToGo() != 0 || motorY.distanceToGo() != 0) {
    motorX.run();
    motorY.run();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  motorMutex.unlock();
  Serial.println("[MANUAL] Movimento parado.");
  imprimirPosicaoManual();
}

void zerarPosicaoAtualManual() {
  motorX.setCurrentPosition(0);
  motorY.setCurrentPosition(0);
  ultimoX_alinhado = -1;
  ultimoY_alinhado = -1;
  deslocamentoAlinhX = 0;
  deslocamentoAlinhY = 0;
  Serial.println("[MANUAL] Posicao atual definida como origem X=0 Y=0.");
}

void processarComandoManual(String comando) {
  comando.trim();
  comando.toLowerCase();

  if (comando == "h") { imprimirAjudaManual(); return; }
  if (comando == "p") { imprimirPosicaoManual(); return; }
  if (comando == "s") { pararMotoresManual(); return; }
  if (comando == "z") { zerarPosicaoAtualManual(); return; }

  if (!ajusteManualHabilitado.load()) {
    Serial.println("[MANUAL] Ajuste manual nao esta habilitado.");
    return;
  }

  motorMutex.lock();
  if (comando == "x+") {
    movimentoManualAtual = MANUAL_MOVENDO_X;
    motorY.stop();
    motorX.moveTo(fisicoX(logX() + ALVO_GRANDE_MANUAL));
    Serial.println("[MANUAL] Movendo X positivo...");
  } else if (comando == "x-") {
    movimentoManualAtual = MANUAL_MOVENDO_X;
    motorY.stop();
    motorX.moveTo(fisicoX(logX() - ALVO_GRANDE_MANUAL));
    Serial.println("[MANUAL] Movendo X negativo...");
  } else if (comando == "y+") {
    movimentoManualAtual = MANUAL_MOVENDO_Y;
    motorX.stop();
    motorY.moveTo(fisicoY(logY() + ALVO_GRANDE_MANUAL));
    Serial.println("[MANUAL] Movendo Y positivo...");
  } else if (comando == "y-") {
    movimentoManualAtual = MANUAL_MOVENDO_Y;
    motorX.stop();
    motorY.moveTo(fisicoY(logY() - ALVO_GRANDE_MANUAL));
    Serial.println("[MANUAL] Movendo Y negativo...");
  } else {
    Serial.print("[MANUAL] Comando desconhecido: ");
    Serial.println(comando);
  }
  motorMutex.unlock();
}

void atualizarMovimentoManual() {
  if (!ajusteManualHabilitado.load()) return;

  if (Serial.available() > 0) {
    String comando = Serial.readStringUntil('\n');
    processarComandoManual(comando);
  }

  motorMutex.lock();
  if (movimentoManualAtual == MANUAL_MOVENDO_X) motorX.run();
  if (movimentoManualAtual == MANUAL_MOVENDO_Y) motorY.run();
  motorMutex.unlock();

  if (movimentoManualAtual != MANUAL_PARADO) {
    unsigned long agora = millis();
    if (agora - ultimoPrintManual >= INTERVALO_PRINT_MANUAL_MS) {
      ultimoPrintManual = agora;
      imprimirPosicaoManual();
    }
  }
}

//  Calibração automática de dois pontos (escuro e claro)
bool moverCalibracaoAte(long ax, long ay) {
  ax = clip(ax, X_MIN, X_MAX);
  ay = clip(ay, Y_MIN, Y_MAX);

  motorMutex.lock();
  motorX.setMaxSpeed(VEL_CAL_X);
  motorX.setAcceleration(ACEL_CAL_X);
  motorY.setMaxSpeed(VEL_CAL_Y);
  motorY.setAcceleration(ACEL_CAL_Y);

  motorX.moveTo(fisicoX(ax));
  motorY.moveTo(fisicoY(ay));

  while ((motorX.distanceToGo() || motorY.distanceToGo()) &&
         !cancelarCalibracao.load()) {
    motorX.run();
    motorY.run();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  motorMutex.unlock();

  return !cancelarCalibracao.load();
}

// Ordena só os maiores valores até topN.
// Pega as maiores leituras de tensão e coloca no começo do vetor.
static void ordenarTopN(float* vals, long* poss, int total, int topN) {
  for (int i = 0; i < topN; i++) {
    int maxIdx = i;
    for (int j = i + 1; j < total; j++) {
      if (vals[j] > vals[maxIdx]) maxIdx = j;
    }
    float tmpV = vals[i]; vals[i] = vals[maxIdx]; vals[maxIdx] = tmpV;
    long  tmpP = poss[i]; poss[i] = poss[maxIdx]; poss[maxIdx] = tmpP;
  }
}

// Mede quanto o painel superior sobe quando o laser aparece durante a varredura em X.
// Faz a média, não usa apenas um pico.
ResultadoCalPainel varrerClaroX() {
  ResultadoCalPainel r = {false, 0, 0.0f, 0.0f, 0.0f};

  float leituras[MAX_LEITURAS_CAL];
  long  posicoes[MAX_LEITURAS_CAL];
  int   totalLeituras = 0;

  if (!moverCalibracaoAte(0, 0)) return r;

  const long fim = clip(CAL_X_FIM, X_MIN, X_MAX);
  Serial.printf("[CAL_SCAN] Varrendo X (Y=0) para PAINEL_SUP | passo=%ld vel=%.1f\n",
                PASSO_CAL_X, VEL_CAL_X);

  motorMutex.lock();
  motorX.setMaxSpeed(VEL_CAL_X);
  motorX.setAcceleration(ACEL_CAL_X);
  motorX.moveTo(fisicoX(fim));

  long proximaCaptacao = 0;

  while (motorX.distanceToGo() && !cancelarCalibracao.load()) {
    motorX.run();
    long x = logX();

    if (x >= proximaCaptacao && totalLeituras < MAX_LEITURAS_CAL) {
      float v = lerPainelV(PIN_PAINEL_SUP);
      leituras[totalLeituras] = v;
      posicoes[totalLeituras] = x;
      totalLeituras++;
      Serial.printf("[CAL_SCAN] X=%ld  V_SUP=%.4fV\n", x, v);
      proximaCaptacao += PASSO_CAL_X;
    }

    vTaskDelay(pdMS_TO_TICKS(1));
  }
  motorMutex.unlock();

  ultimoDeslocamentoCalX = logX();

  if (totalLeituras == 0) return r;

  int topN = min(TOP_MEDIA_CAL, totalLeituras);
  ordenarTopN(leituras, posicoes, totalLeituras, topN);

  r.valorPico = leituras[0];
  r.posPico   = posicoes[0];

  float soma = 0.0f;
  for (int i = 0; i < topN; i++) soma += leituras[i];
  r.valorClaroRobusto = soma / (float)topN;

  r.amplitude = r.valorClaroRobusto - painelBaseSup;
  r.encontrou = r.amplitude >= SUBIDA_MIN_CAL_SUP_V;

  Serial.printf("[CAL_SCAN] SUP pico=%.4fV em X=%ld | mediaTop%d=%.4fV | base=%.4fV | subida=%.4fV\n",
                r.valorPico, r.posPico, topN, r.valorClaroRobusto,
                painelBaseSup, r.amplitude);

  if (!r.encontrou) {
    Serial.printf("[CAL_SCAN] AVISO: PAINEL_SUP nao detectado. Subida minima exigida=%.4fV\n",
                  SUBIDA_MIN_CAL_SUP_V);
  }

  return r;
}


// Mede quanto o painel direito sobe quando o laser aparece durante a varredura em Y.
// Faz a média, não usa apenas um pico.
ResultadoCalPainel varrerClaroY() {
  ResultadoCalPainel r = {false, 0, 0.0f, 0.0f, 0.0f};

  float leituras[MAX_LEITURAS_CAL];
  long  posicoes[MAX_LEITURAS_CAL];
  int   totalLeituras = 0;

  if (!moverCalibracaoAte(0, 0)) return r;

  const long fim = clip(CAL_Y_FIM, Y_MIN, Y_MAX);
  Serial.printf("[CAL_SCAN] Varrendo Y (X=0) para PAINEL_DIR | passo=%ld vel=%.1f\n",
                PASSO_CAL_Y, VEL_CAL_Y);

  motorMutex.lock();
  motorY.setMaxSpeed(VEL_CAL_Y);
  motorY.setAcceleration(ACEL_CAL_Y);
  motorY.moveTo(fisicoY(fim));

  long proximaCaptacao = 0;

  while (motorY.distanceToGo() && !cancelarCalibracao.load()) {
    motorY.run();
    long y = logY();

    if (y >= proximaCaptacao && totalLeituras < MAX_LEITURAS_CAL) {
      float v = lerPainelV(PIN_PAINEL_DIR);
      leituras[totalLeituras] = v;
      posicoes[totalLeituras] = y;
      totalLeituras++;
      Serial.printf("[CAL_SCAN] Y=%ld  V_DIR=%.4fV\n", y, v);
      proximaCaptacao += PASSO_CAL_Y;
    }

    vTaskDelay(pdMS_TO_TICKS(1));
  }
  motorMutex.unlock();

  ultimoDeslocamentoCalY = logY();

  if (totalLeituras == 0) return r;

  int topN = min(TOP_MEDIA_CAL, totalLeituras);
  ordenarTopN(leituras, posicoes, totalLeituras, topN);

  r.valorPico = leituras[0];
  r.posPico   = posicoes[0];

  float soma = 0.0f;
  for (int i = 0; i < topN; i++) soma += leituras[i];
  r.valorClaroRobusto = soma / (float)topN;

  r.amplitude = r.valorClaroRobusto - painelBaseDir;
  r.encontrou = r.amplitude >= SUBIDA_MIN_CAL_DIR_V;

  Serial.printf("[CAL_SCAN] DIR pico=%.4fV em Y=%ld | mediaTop%d=%.4fV | base=%.4fV | subida=%.4fV\n",
                r.valorPico, r.posPico, topN, r.valorClaroRobusto,
                painelBaseDir, r.amplitude);

  if (!r.encontrou) {
    Serial.printf("[CAL_SCAN] AVISO: PAINEL_DIR nao detectado. Subida minima exigida=%.4fV\n",
                  SUBIDA_MIN_CAL_DIR_V);
  }

  return r;
}

void tarefaCalibracao(void* pvParameters) {
  calibracaoRodando.store(true);
  calibracaoConcluida.store(false);
  estadoAtual = CALIBRACAO;

  analogReadResolution(12);
  analogSetPinAttenuation(PIN_PAINEL_SUP, ADC_11db);
  analogSetPinAttenuation(PIN_PAINEL_DIR, ADC_11db);

  motorX.enableOutputs();
  motorY.enableOutputs();

  bool ok = false;

  if (labs(logX()) > PASSO_CENTRO || labs(logY()) > PASSO_CENTRO) {
    Serial.printf("[CAL_ENV] Recusada fora da origem: X=%ld Y=%ld.\n",
                  logX(), logY());
  } else {
    Serial.println("[CAL_ENV] Iniciando calibracao de dois pontos na origem.");
    Serial.println("[CAL] ====== FASE 1: ESCURO ======");

    if (calibrarPaineis(&cancelarCalibracao)) {
      Serial.println("[CAL] ====== FASE 2: CLARO (varredura automatica) ======");

      ResultadoCalPainel sup = varrerClaroX();

      if (!cancelarCalibracao.load()) {
        Serial.printf("[CAL_RETORNO] Voltando X de %ld para 0.\n", logX());
        moverCalibracaoAte(0, 0);
      }

      ResultadoCalPainel dir = {false, 0, 0.0f, 0.0f, 0.0f};

      if (!cancelarCalibracao.load()) {
        dir = varrerClaroY();
      }

      if (!cancelarCalibracao.load()) {
        Serial.printf("[CAL_RETORNO] Voltando Y de %ld para 0.\n", logY());
        moverCalibracaoAte(0, 0);
      }

      if (!cancelarCalibracao.load() && sup.encontrou && dir.encontrou) {
        painelClaroSupV = sup.valorClaroRobusto;
        painelClaroDirV = dir.valorClaroRobusto;
        amplitudeLaserSupV = sup.amplitude;
        amplitudeLaserDirV = dir.amplitude;
        calibracaoDoisPontosValida = true;

        configurarMargensDeteccao();

        Serial.println("[CAL] ====== RESULTADO ======");
        Serial.printf("[CAL] SUP escuro=%.4fV claro=%.4fV amplitude=%.4fV limiarEntrada>=%.4fV\n",
                      painelBaseSup, painelClaroSupV,
                      amplitudeLaserSupV, limiarEntradaSup);
        Serial.printf("[CAL] DIR escuro=%.4fV claro=%.4fV amplitude=%.4fV limiarEntrada>=%.4fV\n",
                      painelBaseDir, painelClaroDirV,
                      amplitudeLaserDirV, limiarEntradaDir);
        Serial.printf("[CAL] Deslocamentos: X=%ld Y=%ld | retorno final X=%ld Y=%ld\n",
                      ultimoDeslocamentoCalX, ultimoDeslocamentoCalY,
                      logX(), logY());

        ok = labs(logX()) <= PASSO_CENTRO &&
             labs(logY()) <= PASSO_CENTRO;
      } else if (!cancelarCalibracao.load()) {
        calibracaoDoisPontosValida = false;
        Serial.println("[CAL] Falha: um ou ambos os paineis nao produziram subida suficiente.");
      }
    }
  }

  if (!cancelarCalibracao.load() &&
      (labs(logX()) > PASSO_CENTRO || labs(logY()) > PASSO_CENTRO)) {
    Serial.printf("[CAL_RETORNO] Retorno final de seguranca: X=%ld Y=%ld.\n",
                  logX(), logY());
    moverCalibracaoAte(0, 0);
  }

  calibracaoConcluida.store(ok && !cancelarCalibracao.load());
  calibracaoRodando.store(false);

  desligarMotores();

  calibracaoTaskHandle = NULL;
  cancelarCalibracao.store(false);

  Serial.printf("[CAL_ENV] Encerrada: %s | X=%ld Y=%ld.\n",
                calibracaoConcluida.load() ? "SUCESSO" : "FALHA",
                logX(), logY());

  vTaskDelete(NULL);
}

/*-----------------------  Setup  --------------------------------*/

void setup() 
{
  Serial.begin(115200);
  delay(2000);

  pinMode(D4, INPUT);
  
  WiFi.softAP("ESP_NANO_AP1", "12345678");
  
  rotasServidor();
  server.begin();

  envTimer = timerBegin(1, 80, true);
  timerAttachInterrupt(envTimer, &onEnvTimer, true);
  timerAlarmWrite(envTimer, (BIT_TIME * 2), true);

  recpTimer = timerBegin(0, 80, true);
  timerAttachInterrupt(recpTimer, &onRecpTimer, true);
  timerAlarmWrite(recpTimer, (BIT_TIME * 2.5), true);

  motorX.setMaxSpeed(VEL_X); motorX.setAcceleration(ACEL_X);
  motorY.setMaxSpeed(VEL_Y); motorY.setAcceleration(ACEL_Y);

  pinMode(PIN_PAINEL_SUP, INPUT);
  pinMode(PIN_PAINEL_DIR, INPUT);
  pinMode(PIN_TIL78, INPUT);
}

// Tarefa de alinhamento

// Essa função é chamada quando o TIL foi detectado e precisa confirmar/centralizar.
bool finalizarTILConfirmado(const char* origem) {
  if (confirmarEGuardarTIL()) {
    alinhamentoConcluido.store(true);
    return true;
  }

  // Achou o TIL, mas não conseguiu centralizar/validar. Então entra no modo manual.
  alinhamentoConcluido.store(false);
  Serial.print("[TIL78] Encontrado, mas falhou na centralizacao/validacao. Origem: ");
  Serial.println(origem);
  entrarModoAjusteManual(origem);
  return false;
}

// Tarefa principal do alinhamento automático.
// Usa os painéis solares como referências maiores para estimar a posição
// do TIL78: o painel superior ajuda a estimar X e o painel direito ajuda
// a estimar Y. Com o alvo provável calculado, move até a posição estimada,
// observa o TIL durante o trajeto e, se detectado, executa centralização
// fina e validação para comunicação. Caso a estimativa falhe, realiza uma
// busca final ao redor do alvo; se ainda assim não encontrar o TIL, habilita
// o ajuste manual via Serial como contingência.
void tarefaAlinhamento(void* pvParameters) {
  alinhamentoRodando.store(true);
  alinhamentoConcluido.store(false);
  cancelarAlinhamento.store(false);
  ajusteManualHabilitado.store(false);
  movimentoManualAtual = MANUAL_PARADO;
  estadoAtual = ALINHAMENTO;
  alinhamentoTaskHandle = xTaskGetCurrentTaskHandle();

  {
    std::lock_guard<std::mutex> lock(motorMutex);
    motorX.enableOutputs();
    motorY.enableOutputs();
    motorX.setCurrentPosition(0);
    motorY.setCurrentPosition(0);
    ultimoX_alinhado = -1;
    ultimoY_alinhado = -1;
  }

  Serial.println("[INICIO] Alinhamento integrado. Base segura com mutex + busca por fases do terceiro codigo. Origem X=0 Y=0.");

  analogReadResolution(12);
  analogSetPinAttenuation(PIN_PAINEL_SUP, ADC_11db);
  analogSetPinAttenuation(PIN_PAINEL_DIR, ADC_11db);

  if (!calibracaoDoisPontosValida) {
    Serial.println("[ALINHAMENTO] Calibracao de dois pontos ainda nao valida. Rodando calibracao robusta antes da busca.");
    if (!calibrarPaineis(&cancelarAlinhamento)) {
      Serial.println("[ALINHAMENTO] Calibracao falhou/cancelada. Abortando alinhamento.");
      goto fim;
    }
  } else {
    Serial.println("[ALINHAMENTO] Usando calibracao de dois pontos ja validada.");
    painelRapidoSup = painelBaseSup;
    painelRapidoDir = painelBaseDir;
    painelSubidaSup = painelSubidaDir = 0.0f;
    painelAchouSup = painelAchouDir = false;
    cntEntraSup = cntSaiSup = cntEntraDir = cntSaiDir = 0;
  }

  if (leituraTIL78Estavel()) {
    registrarTILEncontrado("origem");
    Serial.println("[TIL78] Detectado na origem. Validando/centralizando.");
    finalizarTILConfirmado("origem");
    goto fim;
  }

  {
    long yBonus = -1;
    long xBonus = -1;

    ResultadoPainel rx = encontrarX(yBonus);
    if (alinhamentoConcluido.load()) {
      finalizarTILConfirmado("durante-encontrarX");
      goto fim;
    }

    long xParaY = rx.encontrou ? rx.pos : (xBonus >= 0 ? xBonus : -1);
    ResultadoPainel ry = encontrarY(xParaY, xBonus);
    if (alinhamentoConcluido.load()) {
      finalizarTILConfirmado("durante-encontrarY");
      goto fim;
    }

    if (!rx.encontrou && xBonus >= 0) {
      long xTILb = clip(xBonus + TIL_X_DA_BORDA, X_MIN, X_MAX);
      rx = {xTILb, false, true};
      Serial.printf("[X] Obtido como bonus da fase Y -> xTIL=%ld\n", xTILb);
    }

    if (!ry.encontrou && yBonus >= 0) {
      long yTILb = clip(yBonus + TIL_Y_DA_BORDA, Y_MIN, Y_MAX);
      ry = {yTILb, false, true};
      Serial.printf("[Y] Obtido como bonus da fase X -> yTIL=%ld\n", yTILb);
    }

    long xTIL = rx.encontrou ? rx.pos : (X_MIN + X_MAX) / 2;
    long yTIL = ry.encontrou ? ry.pos : (Y_MIN + Y_MAX) / 2;

    if (!rx.encontrou) Serial.println("[AVISO] X nao encontrado. Usando centro do curso.");
    if (!ry.encontrou) Serial.println("[AVISO] Y nao encontrado. Usando centro do curso.");

    xTIL = clip(xTIL, X_MIN, X_MAX);
    yTIL = clip(yTIL, Y_MIN, Y_MAX);

    bool ambosConfiaveis = rx.confiavel && ry.confiavel;

    Serial.printf("[ALVO] X=%ld(%s) Y=%ld(%s)\n",
                  xTIL, rx.confiavel ? "conf" : "est",
                  yTIL, ry.confiavel ? "conf" : "est");

    irParaAlinhamento(xTIL, yTIL);

    if (alinhamentoConcluido.load()) {
      Serial.println("[TIL78] Encontrado no deslocamento ate o alvo. Centralizando.");
      finalizarTILConfirmado("deslocamento-ate-alvo");
      goto fim;
    }

    if (leituraTIL78Estavel()) {
      registrarTILEncontrado("ponto-calculado");
      Serial.println("[TIL78] Confirmado no ponto calculado. Centralizando.");
      finalizarTILConfirmado("ponto-calculado");
      goto fim;
    }

    long fator = ambosConfiaveis ? 1 : 2;
    Serial.println("[BUSCA FINA] TIL nao apareceu no ponto calculado. Iniciando varredura do terceiro codigo.");

    if (buscaFinalTIL(xTIL, yTIL, fator)) {
      Serial.println("[TIL78] Confirmado na busca fina. Centralizando.");
      finalizarTILConfirmado("busca-fina");
    } else {
      Serial.println("[ERRO] TIL nao encontrado automaticamente. Entrando em ajuste manual serial.");
      entrarModoAjusteManual("busca-automatica-falhou");
    }
  }

fim:
  if (alinhamentoConcluido.load()) {
    estadoAtual = ALINHAMENTO;
    Serial.printf("[FIM] Alinhamento concluido em X=%ld Y=%ld.\n", ultimoX_alinhado, ultimoY_alinhado);
  } else if (ajusteManualHabilitado.load()) {
    Serial.println("[FIM] Alinhamento automatico nao concluiu; ajuste manual permanece habilitado.");
  } else {
    Serial.println("[FIM] Alinhamento encerrado sem conclusao.");
  }

  alinhamentoRodando.store(false);
  alinhamentoTaskHandle = NULL;

  if (!ajusteManualHabilitado.load()) {
    desligarMotores();
  }

  vTaskDelete(NULL);
}

// ================================================================

void loop() {
  // O loop só mantém o modo manual vivo:
  // O automático roda em task;
  // O web server roda por callbacks;
  // O envio/recepção rodam por timers;
  atualizarMovimentoManual();
}
