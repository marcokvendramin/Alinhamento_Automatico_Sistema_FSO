#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include "paginas.h"
#include "esp_timer.h"
#include <AccelStepper.h>

#define BIT_TIME 5000
#define SIZE 4096
#define PIN_LASER D5

//----------  Variáveis globais  -----------------

volatile int vemDado = 0; //receive
volatile int caracbit = 0; //both
volatile int bites[8]={0,0,0,0,0,0,0,0}; //receive
volatile int nextCaracterCount = 0; //both
volatile int bitEsperadoNoPareamento = 3; //receive

volatile int32_t dataSize = 0; //both
volatile int32_t countDataTransfer = 0; //both

volatile bool espectSize[32]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}; //receive

volatile bool prontoParaReceber = false; //receive
volatile bool prontoData = false; //both
volatile bool prontoParaEnviar = false; //send
volatile bool interrupcao = false; //receive
volatile bool fimDeRequisicao = false; //receive

volatile char caracterParaEnvio = 'U'; //send

volatile unsigned long timePassed = 0; //receive

hw_timer_t *recpTimer = NULL;
hw_timer_t *envTimer = NULL;

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

//----------------------------------------

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
  int i;
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
      return lista->dado[pos];
    }
  }
}

/*-------------------------------------------------------*/

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
    GPIO.out_w1tc = (1 << 8); //LOW
    desativarPinoOut(8);
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

// ----------------- Controle das páginas -------------------

enum ModoOperacao { IDLE, ENVIO, ENVIANDO, RECEPCAO, RECEBIDO, ALINHAMENTO };
ModoOperacao estadoAtual = IDLE;

String getModoTexto() {
  switch (estadoAtual) {
    case ENVIO:       return "MODO ENVIO";
    case ENVIANDO:    return "MODO ENVIO";
    case RECEPCAO:    return "MODO RECEPÇÃO";
    case RECEBIDO:    return "MODO RECEPÇÃO";
    case ALINHAMENTO: return "MODO ALINHAMENTO - BASE FIXA";
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
            pinMode(D5, OUTPUT);
            GPIO.out_w1ts = (1 << 8); // HIGH
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

    pinMode(PIN_LASER, OUTPUT);
    digitalWrite(PIN_LASER, HIGH);   // Liga o laser
    
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
    estadoAtual = IDLE;

    digitalWrite(PIN_LASER, LOW); // LOW - desliga o laser
    pinMode(PIN_LASER, INPUT);

    request->redirect("/");
  });

  // ZERAR: Colocar algo aqui só para poder encerrar essa base também
  server.on("/zerar", HTTP_GET, [](AsyncWebServerRequest *request){
    //IMPEMENTAR AQUI A LÓGICA OU FUNÇÃO!!
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
      GPIO.out_w1ts = (1 << 8); //HIGH
    else
      GPIO.out_w1tc = (1 << 8); //LOW
      
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
        GPIO.out_w1ts = (1 << 8); //HIGH
      else
        GPIO.out_w1tc = (1 << 8); //LOW
      
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
        GPIO.out_w1ts = (1 << 8); //HIGH
      else
        GPIO.out_w1tc = (1 << 8); //LOW
      
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

/*-----------------------  Setup  --------------------------------*/

void setup() 
{
  Serial.begin(115200);
  delay(2000);

  pinMode(D4, INPUT);
  
  WiFi.softAP("ESP_NANO_AP", "12345678");
  
  rotasServidor();
  server.begin();

  envTimer = timerBegin(1, 80, true);
  timerAttachInterrupt(envTimer, &onEnvTimer, true);
  timerAlarmWrite(envTimer, (BIT_TIME * 2), true);

  recpTimer = timerBegin(0, 80, true);
  timerAttachInterrupt(recpTimer, &onRecpTimer, true);
  timerAlarmWrite(recpTimer, (BIT_TIME * 2.5), true);

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

  GPIO.out_w1tc = (1 << 8); // LOW

  inicioDeEnvio();
  
  vTaskDelete(NULL);
}

// ========================== loop =================================

void loop() {
  // empty
}