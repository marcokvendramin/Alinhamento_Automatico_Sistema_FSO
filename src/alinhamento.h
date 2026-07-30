#ifndef ALINHAMENTO_H
#define ALINHAMENTO_H

#include <Arduino.h> // Para funções básicas.
#include <AccelStepper.h> // Biblioteca dos motores de passo.
#include <atomic> // Variáveis atômicas, flags.
#include <mutex> // Evita conflito de tarefas simultâneas.

// Estrutura de resultado de uma busca de borda
struct ResultadoCalPainel {
  bool  encontrou;
  long  posPico;
  float valorPico;
  float valorClaroRobusto; // Quando laser incide no painel.
  float amplitude;         // Base/ambiente menos leitura com laser.
};

// Resultado usado pelo alinhamento por fases.
struct ResultadoPainel {
  long pos;
  bool confiavel;
  bool encontrou;
};

// Estrutura usada pelo alinhamento simples, 
// porque não sabemos qual painel encontraremos antes.
struct AlvoParcial {
  bool temX;
  bool temY;
  long x;
  long y;
};

// Modo manual de contingência (conjunto de estados).
enum MovimentoManual {
  MANUAL_PARADO,
  MANUAL_MOVENDO_X,
  MANUAL_MOVENDO_Y
};

// Protótipos das funções principais
void desligarMotores();
void tarefaAlinhamento(void * pvParameters);
void pairingManager(void * pvParameters);
void prepareToSend(void * pvParameters);
void tarefaZerar(void * pvParameters);
void encerrarCanalComunicacao();

// Protótipos das funções de alinhamento
void salvarPosicaoCertaTIL(const char* origem);
void registrarTILEncontrado(const char* origem);
bool leituraTIL78Estavel();
bool validarTILParaComunicacao();

void moverBruto(long ax, long ay);
bool moverSemPararNoTIL(long ax, long ay);
bool moverObservandoTIL(long ax, long ay, const char* origem);

bool detectarPainelSuperior();
bool detectarPainelDireito();

bool procurarPrimeiroPainel(AlvoParcial& alvo);
bool completarYComPainelDireito(AlvoParcial& alvo);
bool completarXComPainelSuperior(AlvoParcial& alvo);

bool moverCentroSemSalvar(long ax, long ay);
int contarTILAtivo(int amostras, int intervaloMs);
long acharBordaTIL_X(long xInicio, long yFixo, int dir, long passo);
long acharBordaTIL_Y(long xFixo, long yInicio, int dir, long passo);
bool centralizarTILMinimal();
bool confirmarEGuardarTIL();

// Alinhamento por fases.
bool irParaAlinhamento(long ax, long ay);
bool irParaFinoAlinhamento(long ax, long ay);
bool varrerRetanguloTIL(long xC, long yC, long rX, long rY, long passoY);
bool buscaFinalTIL(long xC, long yC, long raio_fator = 1);

ResultadoPainel buscarBordaX(long y, long& yBonus);
ResultadoPainel buscarBordaY(long x, long& xBonus);
ResultadoPainel encontrarX(long& yBonus);
ResultadoPainel encontrarY(long xInicial, long& xBonus);

bool finalizarTILConfirmado(const char* origem);

// Ajuste manual (apenas em urgência).
void entrarModoAjusteManual(const char* motivo);
void imprimirPosicaoManual();
void imprimirAjudaManual();
void pararMotoresManual();
void zerarPosicaoAtualManual();
void processarComandoManual(String comando);
void atualizarMovimentoManual();

// Protótipos da calibração
bool calibrarPaineis(std::atomic<bool>* flagCancelar);
float lerPainelV(int pino);
float medianaOrdenando(float* valores, int n);
void calcularBaseERuidoRobustos(float* amostras, int n, float& base, float& sigma);
void configurarMargensDeteccao();
void atualizarPaineis();
bool encerrarTasksAlinhamento(TickType_t timeoutTicks);

#endif
