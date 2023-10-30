#include "so.h"
#include "irq.h"
#include "programa.h"
#include "fila.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>

// intervalo entre interrupções do relógio
#define INTERVALO_INTERRUPCAO 50   // em instruções executadas
#define QUANTUM 5

// escalonador
#define ESC_ROUND_ROBIN
//#define ESC_PRIORIDADE

typedef enum process_estado_s {
  pronto,
  bloqueado,
  parado
} process_estado_t;

typedef struct process_s {
  int id;
  int existe;
  process_estado_t estado;
  int dispES;
  int dadoES;
  struct process_s *esperando;
  int quantum;

  int regPC;
  int regA;
  int regX;
  err_t regErr;
} process_t;

process_t processo_vazio = {
  .id = -1,
  .existe = 1,
  .estado = pronto,
  .dispES = -1,
  .dadoES = 0,
  .esperando = NULL,
  .quantum = QUANTUM,
  .regPC = 0,
  .regA = 0,
  .regX = 0,
  .regErr = ERR_CPU_PARADA
};


#define MAX_PROCESSOS 4

struct so_t {
  cpu_t *cpu;
  mem_t *mem;
  console_t *console;
  relogio_t *relogio;
  process_t processos[MAX_PROCESSOS];
  process_t* processoAtual;
  fila_t* filaProcessos;
};


// função de tratamento de interrupção (entrada no SO)
static err_t so_trata_interrupcao(void *argC, int reg_A);

// funções auxiliares
static int so_carrega_programa(so_t *self, char *nome_do_executavel);
static bool copia_str_da_mem(int tam, char str[tam], mem_t *mem, int ender);
static bool so_escalonador(so_t* console);
static void so_restaura_estado_processo(so_t *self);
static void so_salva_estado_processo(so_t *self);
static void panic(so_t* self, const char* msg);
static void remover_processo_fila(so_t *self, int id);


so_t *so_cria(cpu_t *cpu, mem_t *mem, console_t *console, relogio_t *relogio)
{
  so_t *self = malloc(sizeof(*self));
  if (self == NULL) return NULL;

  self->cpu = cpu;
  self->mem = mem;
  self->console = console;
  self->relogio = relogio;

  // inicia os processos como nao existentes
  for(int i = 0; i < MAX_PROCESSOS; i++){
    self->processos[i].existe = 0;
  }
  self->processoAtual = NULL;

  // quando a CPU executar uma instrução CHAMAC, deve chamar essa função
  cpu_define_chamaC(self->cpu, so_trata_interrupcao, self);
  // coloca o tratador de interrupção na memória
  if (so_carrega_programa(self, "trata_irq.maq") != 10) {
    console_printf(console, "SO: problema na carga do tratador de interrupcoes");
    free(self);
    self = NULL;
  }
  // programa a interrupção do relógio
  rel_escr(self->relogio, 2, INTERVALO_INTERRUPCAO);

  // cria a fila de prioridade para o escalonador
  self->filaProcessos = fila_cria();

  return self;
}

void so_destroi(so_t *self)
{
  fila_destroi(self->filaProcessos);
  cpu_define_chamaC(self->cpu, NULL, NULL);
  free(self);
}


// Tratamento de interrupção

// funções auxiliares para tratar cada tipo de interrupção
static err_t so_trata_irq_reset(so_t *self);
static err_t so_trata_irq_err_cpu(so_t *self);
static err_t so_trata_irq_relogio(so_t *self);
static err_t so_trata_irq_desconhecida(so_t *self, int irq);
static err_t so_trata_chamada_sistema(so_t *self);
static void so_chamada_espera_proc(so_t* self);
static void so_chamada_le(so_t *self);
static void so_chamada_escr(so_t *self);
static void so_chamada_cria_proc(so_t *self);
static void so_chamada_mata_proc(so_t *self);
static void so_verifica_pendencias(so_t *self);

// função a ser chamada pela CPU quando executa a instrução CHAMAC
// essa instrução só deve ser executada quando for tratar uma interrupção
// o primeiro argumento é um ponteiro para o SO, o segundo é a identificação
//   da interrupção
// na inicialização do SO é colocada no endereço 10 uma rotina que executa
//   CHAMAC; quando recebe uma interrupção, a CPU salva os registradores
//   no endereço 0, e desvia para o endereço 10
static err_t so_trata_interrupcao(void *argC, int reg_A)
{
  so_t *self = argC;
  irq_t irq = reg_A;
  err_t err;
  so_salva_estado_processo(self);
  switch (irq) {
    case IRQ_RESET:
      err = so_trata_irq_reset(self);
      break;
    case IRQ_ERR_CPU:
      err = so_trata_irq_err_cpu(self);
      break;
    case IRQ_SISTEMA:
      err = so_trata_chamada_sistema(self);
      break;
    case IRQ_RELOGIO:
      err = so_trata_irq_relogio(self);
      break;
    default:
      err = so_trata_irq_desconhecida(self, irq);
  }

  so_verifica_pendencias(self);
  so_escalonador(self);

  so_restaura_estado_processo(self);
  return err;
}

static err_t so_trata_irq_reset(so_t *self)
{
  // coloca um programa na memória
  int ender = so_carrega_programa(self, "init.maq");
  if (ender != 100) {
    console_printf(self->console, "SO: problema na carga do programa inicial");
    return ERR_CPU_PARADA;
  }

  // passa o processador para modo usuário
  mem_escreve(self->mem, IRQ_END_modo, usuario);

  // cria o processo inicial
  self->processos[0].id = 0;
  self->processos[0].existe = 1;
  self->processos[0].estado = pronto;
  self->processoAtual = &self->processos[0];
  self->processos[0].regPC = ender;
  self->processos[0].regA = 0;
  self->processos[0].regX = 0;
  self->processos[0].quantum = QUANTUM;
  self->processos[0].regErr = ERR_OK;
  self->processos[0].esperando = NULL;
  self->processos[0].dispES = -1;

  // coloca o processo inicial na fila de processos
  fila_enqueue(self->filaProcessos, 0);
  return ERR_OK;
}

static err_t so_trata_irq_err_cpu(so_t *self)
{
  // Ocorreu um erro interno na CPU
  // O erro está codificado em IRQ_END_erro
  // Em geral, causa a morte do processo que causou o erro
  // Ainda não temos processos, causa a parada da CPU
  err_t err = self->processoAtual->regErr;
  if(err != ERR_OK && self->processoAtual != NULL && self->processoAtual != &processo_vazio){
    console_printf(self->console,
        "SO: processo atual causou erro %d (%s), matando", err, err_nome(err));
    so_chamada_mata_proc(self);
  }

  return ERR_OK;
}

static err_t so_trata_irq_relogio(so_t *self)
{
  // ocorreu uma interrupção do relógio
  // rearma o interruptor do relógio e reinicializa o timer para a próxima interrupção
  rel_escr(self->relogio, 3, 0); // desliga o sinalizador de interrupção
  rel_escr(self->relogio, 2, INTERVALO_INTERRUPCAO);
  if(self->processoAtual != NULL){
    if(self->processoAtual->existe){
      self->processoAtual->quantum--;
    }
  }
  return ERR_OK;
}

static err_t so_trata_irq_desconhecida(so_t *self, int irq)
{
  console_printf(self->console,
      "SO: nao sei tratar IRQ %d (%s)", irq, irq_nome(irq));
  return ERR_CPU_PARADA;
}

static err_t so_trata_chamada_sistema(so_t *self)
{
  int id_chamada = self->processoAtual->regA;
  if(id_chamada  != 2)
    console_printf(self->console, "SO: chamada de sistema %d", id_chamada);
  switch (id_chamada) {
    case SO_LE:
      so_chamada_le(self);
      break;
    case SO_ESCR:
      so_chamada_escr(self);
      break;
    case SO_CRIA_PROC:
      so_chamada_cria_proc(self);
      break;
    case SO_MATA_PROC:
      console_printf(self->console, "SO: vou matar");
      so_chamada_mata_proc(self);
      break;
    case SO_ESPERA_PROC:
      console_printf(self->console, "SO: estado proc %d ant block: %d", self->processoAtual->id, self->processoAtual->estado);
      so_chamada_espera_proc(self);
      console_printf(self->console, "SO: estado proc %d dps block: %d", self->processoAtual->id, self->processoAtual->estado);
      break;
    default:
      console_printf(self->console,
          "SO: chamada de sistema desconhecida (%d)", id_chamada);
      return ERR_CPU_PARADA;
  }
  return ERR_OK;
}

static void so_chamada_le(so_t *self)
{
  int terminal = self->processoAtual->id * 4;
  int operacao = terminal + 0;

  int pode;
  term_le(self->console, operacao+1, &pode);

  if(pode){
    // pode ler direto
    term_le(self->console, operacao, &self->processoAtual->regA);
  }else{
    // n pode ler direto
    self->processoAtual->estado = bloqueado;
    self->processoAtual->dispES = operacao;
    // remover o processo da lista de prontos
    if(fila_contem(self->filaProcessos, self->processoAtual->id)){
      remover_processo_fila(self, self->processoAtual->id);
      console_printf(self->console, "SO: tirei o proc %d da fila de procs prontos", self->processoAtual->id);
    }
  }
}

static void so_chamada_escr(so_t *self)
{
  if(self->processoAtual == NULL){
    console_printf(self->console, "SO: tentei escrever mas n tem processo atual");
    return;
  }

  int terminal = self->processoAtual->id * 4;
  int operacao = terminal + 2;

  int pode;
  term_le(self->console, operacao+1, &pode);

  if(pode){
    // pode direto
    term_escr(self->console, operacao, self->processoAtual->regX);
  }else{
    // n pode direto
    self->processoAtual->estado = bloqueado;
    self->processoAtual->dispES = operacao;
    self->processoAtual->dadoES = self->processoAtual->regX;
    // remover o processo da lista de prontos
    if(fila_contem(self->filaProcessos, self->processoAtual->id)){
      remover_processo_fila(self, self->processoAtual->id);
      console_printf(self->console, "SO: tirei o proc %d da fila de procs prontos", self->processoAtual->id);
    }
  }
}

/**
 * @brief Chamada de sistema que cria um processo novo.
 * Vai criar uma entrada automaticamente na tabela de processos
 * e vai adicionar ele na fila de processos prontos.
 *
 * @param self A estrutura do SO.
 */
static void so_chamada_cria_proc(so_t *self)
{
  console_printf(self->console, "criando processo...");

  // acha a primeira entrada na tabela de processos vazia
  int id = -1;
  for(int i = 0; i < MAX_PROCESSOS; i++){
    if(!self->processos[i].existe){
      id = i;
      break;
    }
  }

  // se não achou, retorna erro
  if(id == -1){
    console_printf(self->console, "SO: SO_CRIA_PROC sem espaco na tabela de processos");
    self->processoAtual->regA = -1;
    //mem_escreve(self->mem, IRQ_END_A, -1);
    return;
  }

  // cria o processo
  self->processos[id].id = id;
  self->processos[id].existe = 1;
  self->processos[id].regA = 0;
  self->processos[id].regX = 0;
  self->processos[id].regErr = ERR_OK;
  self->processos[id].esperando = NULL;
  self->processos[id].dispES = -1;
  self->processos[id].estado = pronto;

  // em X está o endereço onde está o nome do arquivo
  int ender_proc = self->processoAtual->regX;
  // if (!mem_le(self->mem, IRQ_END_X, &ender_proc) == ERR_OK) {
  //   return;
  // }
  char nome[100];
  if (!copia_str_da_mem(100, nome, self->mem, ender_proc)) {
    return;
  }
  int ender_carga = so_carrega_programa(self, nome);
  if (ender_carga <= 0) {
    return;
  }
  self->processos[id].regPC = ender_carga;
  self->processoAtual->regA = id;
  fila_enqueue(self->filaProcessos, id);
}

/**
 * @brief Chamada de sistema que bloqueia o processo atual
 * ate o fim da execucao de outro processo.
 *
 * @param self A estrutura do SO.
 */
static void so_chamada_espera_proc(so_t* self){
  int id = self->processoAtual->regX;
  process_t *esperado = &self->processos[id];
  process_t *esperador = self->processoAtual;

  if(esperador->estado == bloqueado){
    console_printf(self->console, "SO: %d ja esta bloqueado", esperador->id);
  }

  esperador->estado = bloqueado;
  esperador->esperando = esperado;
  console_printf(self->console, "SO: %d bloqueado(espera o %d)", esperador->id, esperado->id, esperador->estado);
  remover_processo_fila(self, esperador->id);
  console_printf(self->console, "SO: tirei o proc %d da fila de procs prontos; filaTam: %d", self->processoAtual->id, fila_tamanho(self->filaProcessos));
}

/**
 * @brief Implementacao da chamada de sistema para matar o processo atual.
 *
 * @param self A estrutura do SO.
 */
static void so_chamada_mata_proc(so_t *self)
{
  if(self->processoAtual == NULL){
    console_printf(self->console, "SO: SO_MATA_PROC sem processo atual");
    panic(self, "Tentei matar sem alvo");
    //mem_escreve(self->mem, IRQ_END_A, -1);
    return;
  }

  // self->processoAtual->existe = 0;
  self->processoAtual->estado = parado;
  if(fila_contem(self->filaProcessos, self->processoAtual->id)){
    remover_processo_fila(self, self->processoAtual->id);
  }

  // // se estiver alguem esperando, notificar quem esta parado
  // for(int i=0; i<MAX_PROCESSOS;i++){
  //   process_t* proc = &self->processos[i];
  //   if(proc->esperando == NULL){
  //     continue;
  //   }

  //   if(proc->esperando == self->processoAtual){
  //     proc->estado = pronto;
  //     proc->esperando = NULL;
  //     fila_enqueue(self->filaProcessos, proc->id);
  //     console_printf(self->console, "SO: desbloqueei processo %d pq o %d morreu", proc->id, self->processoAtual->id);
  //   }
  // }

  console_printf(self->console, "SO: eu parei o processo %d e tirei ele da fila", self->processoAtual->id);
  self->processoAtual = NULL;
}

// pega os registradores salvos no IRQ e salva no tabela de processos para o atual
static void so_salva_estado_processo(so_t *self){
  if(self->processoAtual == NULL){
    console_printf(self->console, "SO: tentei salvar estado mas não tem processo atual");
    return;
  }

  mem_le(self->mem, IRQ_END_PC, &self->processoAtual->regPC);
  mem_le(self->mem, IRQ_END_A, &self->processoAtual->regA);
  mem_le(self->mem, IRQ_END_X, &self->processoAtual->regX);
  mem_le(self->mem, IRQ_END_erro, (int*)(&self->processoAtual->regErr));
}

// faz o inverso da so_salva_estado_processo
static void so_restaura_estado_processo(so_t *self){
  if(self->processoAtual == NULL){
    console_printf(self->console, "SO: restaura: procAtual NULL -> vazio");
    self->processoAtual = &processo_vazio;
  }

  mem_escreve(self->mem, IRQ_END_PC, self->processoAtual->regPC);
  mem_escreve(self->mem, IRQ_END_A, self->processoAtual->regA);
  mem_escreve(self->mem, IRQ_END_X, self->processoAtual->regX);
  mem_escreve(self->mem, IRQ_END_erro, (int)self->processoAtual->regErr);
}

#ifdef ESC_ROUND_ROBIN
/**
 * @brief Implementa um escalonador com a estrategia
 * round-robin
 *
 * @param self A estrutura do SO.
 * @return *true* se conseguiu achar um processo para executar e
 * *false* se nao conseguiu achar um processo para executar
 */
static bool so_escalonador(so_t* self){
  // ta vazio e nao tem mais nenhum, pula
  if(fila_tamanho(self->filaProcessos) <= 0 && (self->processoAtual == NULL || self->processoAtual == &processo_vazio)){
    console_printf(self->console, "SO: acabaram todos os processos");
    self->processoAtual = &processo_vazio;
    return false;
  }

  //console_printf(self->console, "SO: escalonando, ainda tem %d procs na fila", fila_tamanho(self->filaProcessos));
  // aqui ta vazio e tem disponivel
  if(self->processoAtual == NULL || self->processoAtual == &processo_vazio){
    // pega o proximo pra executar
    int proxId = fila_dequeue(self->filaProcessos);
    if(proxId == -1 || self->processos[proxId].estado != pronto){
      console_printf(self->console, "SO: n tinha pronto, defini o vazio2");
      self->processoAtual = &processo_vazio;
      return false;
    }
    self->processoAtual = &self->processos[proxId];
    self->processoAtual->quantum = QUANTUM;
    console_printf(self->console, "SO: nao tinha nenhum processo, executando %d agr", proxId);
    return true;
  }

  // processo bloqueou ou acabou quantum
  if(self->processoAtual->estado == bloqueado || self->processoAtual->quantum <= 0){
    self->processoAtual->quantum = QUANTUM;
    if(self->processoAtual->estado == pronto){
      fila_enqueue(self->filaProcessos, self->processoAtual->id);
    }

    int proxId = fila_dequeue(self->filaProcessos);
    if(proxId == -1 || self->processos[proxId].estado != pronto){
      self->processoAtual = &processo_vazio;
      console_printf(self->console, "SO: n tinha pronto, defini o vazio1");
      return false;
    }
    self->processoAtual = &self->processos[proxId];
    self->processoAtual->quantum = QUANTUM;
    //console_printf(self->console, "SO: escalonei, ainda tem x procs na fila"/*, fila_tamanho(self->filaProcessos)*/);
    return true;
  }
  return true;
}
#elif ESC_PRIORIDADE
static bool so_escalonador(so_t* self){
  // escalonador por prioridade
}
#endif

// carrega o programa na memória
// retorna o endereço de carga ou -1
static int so_carrega_programa(so_t *self, char *nome_do_executavel)
{
  // programa para executar na nossa CPU
  programa_t *prog = prog_cria(nome_do_executavel);
  if (prog == NULL) {
    console_printf(self->console,
        "Erro na leitura do programa '%s'\n", nome_do_executavel);
    return -1;
  }

  int end_ini = prog_end_carga(prog);
  int end_fim = end_ini + prog_tamanho(prog);

  for (int end = end_ini; end < end_fim; end++) {
    if (mem_escreve(self->mem, end, prog_dado(prog, end)) != ERR_OK) {
      console_printf(self->console,
          "Erro na carga da memoria, endereco %d\n", end);
      return -1;
    }
  }
  prog_destroi(prog);
  console_printf(self->console,
      "SO: carga de '%s' em %d-%d", nome_do_executavel, end_ini, end_fim);

  return end_ini;
}

// copia uma string da memória do simulador para o vetor str.
// retorna false se erro (string maior que vetor, valor não ascii na memória,
//   erro de acesso à memória)
static bool copia_str_da_mem(int tam, char str[tam], mem_t *mem, int ender)
{
  for (int indice_str = 0; indice_str < tam; indice_str++) {
    int caractere;
    if (mem_le(mem, ender + indice_str, &caractere) != ERR_OK) {
      return false;
    }
    if (caractere < 0 || caractere > 255) {
      return false;
    }
    str[indice_str] = caractere;
    if (caractere == 0) {
      return true;
    }
  }
  // estourou o tamanho de str
  return false;
}

static void so_desbloqueia_es(so_t* self, int i){
  int dispositivoChecagem = self->processos[i].dispES+1;
  int jahpode;
  term_le(self->console, dispositivoChecagem, &jahpode);
  if(!jahpode){
    return;
  }

  int leitura = (dispositivoChecagem-1) % 4 == 0;
  if(leitura){
    int dado;
    term_le(self->console, self->processos[i].dispES, &dado);
    self->processos[i].regA = dado;
  }else{
    // escrita na tela
    term_escr(self->console,
    self->processos[i].dispES,
    self->processos[i].dadoES);
  }
  self->processos[i].estado = pronto;
  self->processos[i].dispES = -1;
  fila_enqueue(self->filaProcessos, i);

  console_printf(self->console, "SO: desbloqueei a ES para proc %d", i);
}

static void so_desbloqueia_espera(so_t* self, int i){
  process_t *proc = self->processos + i;
  if(proc->esperando->estado == parado) {
    console_printf(self->console, "SO: %d esperava %d e foi desbloqueado", proc->id, proc->esperando->id);
    proc->esperando = NULL;
    proc->estado = pronto;
    if(!fila_contem(self->filaProcessos, proc->id)){
      fila_enqueue(self->filaProcessos, proc->id);
    }
  }
}


/**
 * @brief Verifica pendencias a serem resolvidas.
 * Ex.: Desbloqueia processos esperando dispositivos de E/S e
 * processos esperando outros processos.
 *
 * @param self A estrutura do SO.
 */
static void so_verifica_pendencias(so_t *self) {
  // para cada processo
  for (int i = 0; i < MAX_PROCESSOS; i++)
  {
    process_t proc = self->processos[i];
    if(proc.estado != bloqueado){
      continue;
    }

    if(i != 3){
      // console_printf(self->console, "SO: %d esta bloqueado, dispES: %d, espr: %p", i, proc.dispES, proc.esperando);
    }

    if(proc.dispES != -1){
      // eh E/S
      so_desbloqueia_es(self, i);
    }

    if(proc.esperando != NULL){
      // esperando outro processo
      so_desbloqueia_espera(self, i);
    }
  }
}

/**
 * @brief Deve ser chamado quando ocorre um erro fatal no SO. Mostra algumas informacoes
 * de debug sobre os processos.
 *
 * @param self A estrutura do SO
 * @param msg Uma mensagem de erro a ser mostrada
 */
static void panic(so_t* self, const char* msg){
  puts("\r");
  puts(msg);
  puts("\n");
  int filaTamanho = fila_tamanho(self->filaProcessos);
  printf("\rFila tamanho: %d\r\nFila Items:\n", filaTamanho);
  for(int i=0; i<filaTamanho; i++){
    int elem = fila_dequeue(self->filaProcessos);
    printf("\r\tPos: %d; Proc: %d; Estado: %d;\n", i, elem, self->processos[elem].estado);
    fila_enqueue(self->filaProcessos, elem);
  }

  puts("\rProcessos:\n");
  for(int i = 0; i<MAX_PROCESSOS; i++){
    printf("\r\tProc: %d; Estado: %d;\n", i, self->processos[i].estado);
  }
  exit(1212);
}

static void remover_processo_fila(so_t *self, int id){
  for(int i=0; i<fila_tamanho(self->filaProcessos); i++){
    int elem = fila_dequeue(self->filaProcessos);
    if(elem != id){
      fila_enqueue(self->filaProcessos, elem);
    }
  }

  // testar se foi removido mesmo o processo
  for(int i=0; i<fila_tamanho(self->filaProcessos); i++){
    int elem = fila_dequeue(self->filaProcessos);
    if(elem == id){
      panic(self, "ERRO! Nao removi o processo da fila");
    }
    fila_enqueue(self->filaProcessos, elem);
  }
}
