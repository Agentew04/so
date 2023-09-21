#include "so.h"
#include "irq.h"
#include "programa.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>

// intervalo entre interrupções do relógio
#define INTERVALO_INTERRUPCAO 50   // em instruções executadas

typedef enum process_estado_s {
  pronto,
  bloqueado,
  parado
} process_estado_t;

typedef struct process_s {
  int id;
  int existe;
  int comecoMem;
  process_estado_t estado;

  int regPC;
  int regA;
  int regX;
} process_t;


#define MAX_PROCESSOS 4

struct so_t {
  cpu_t *cpu;
  mem_t *mem;
  console_t *console;
  relogio_t *relogio;
  process_t processos[MAX_PROCESSOS];
  process_t* processoAtual;
};


// função de tratamento de interrupção (entrada no SO)
static err_t so_trata_interrupcao(void *argC, int reg_A);

// funções auxiliares
static int so_carrega_programa(so_t *self, char *nome_do_executavel);
static bool copia_str_da_mem(int tam, char str[tam], mem_t *mem, int ender);
static void so_escalonador(so_t* console);
static void so_restaura_estado_processo(so_t *self);
static void so_salva_estado_processo(so_t *self);



so_t *so_cria(cpu_t *cpu, mem_t *mem, console_t *console, relogio_t *relogio)
{
  so_t *self = malloc(sizeof(*self));
  if (self == NULL) return NULL;

  self->cpu = cpu;
  self->mem = mem;
  self->console = console;
  self->relogio = relogio;
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

  return self;
}

void so_destroi(so_t *self)
{
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
  console_printf(self->console, "SO: recebi IRQ %d (%s)", irq, irq_nome(irq));
  switch (irq) {
    case IRQ_RESET:
      err = so_trata_irq_reset(self);
      break;
    case IRQ_ERR_CPU:
      err = so_trata_irq_err_cpu(self);
      console_printf(self->console, "SO: erro na CPU, %s", err_nome(err));
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

  // altera o PC para o endereço de carga (deve ter sido 100)
  mem_escreve(self->mem, IRQ_END_PC, ender);
  // passa o processador para modo usuário
  mem_escreve(self->mem, IRQ_END_modo, usuario);

  // cria o processo inicial
  self->processos[0].id = 0;
  self->processos[0].existe = 1;
  self->processos[0].estado = pronto;
  self->processoAtual = &self->processos[0];
  self->processos[0].regPC = ender;

  return ERR_OK;
}

static err_t so_trata_irq_err_cpu(so_t *self)
{
  // Ocorreu um erro interno na CPU
  // O erro está codificado em IRQ_END_erro
  // Em geral, causa a morte do processo que causou o erro
  // Ainda não temos processos, causa a parada da CPU
  int err_int;
  mem_le(self->mem, IRQ_END_erro, &err_int);
  err_t err = err_int;
  console_printf(self->console,
      "SO: processo atual causou erro %d (%s)", err, err_nome(err));

  so_chamada_mata_proc(self);
  return ERR_OK;
}

static err_t so_trata_irq_relogio(so_t *self)
{
  // ocorreu uma interrupção do relógio
  // rearma o interruptor do relógio e reinicializa o timer para a próxima interrupção
  rel_escr(self->relogio, 3, 0); // desliga o sinalizador de interrupção
  rel_escr(self->relogio, 2, INTERVALO_INTERRUPCAO);
  // trata a interrupção
  // ...
  //console_printf(self->console, "SO: interrupcao do relogio (nao tratada)");
  return ERR_OK;
}

static err_t so_trata_irq_desconhecida(so_t *self, int irq)
{
  console_printf(self->console,
      "SO: nao sei tratar IRQ %d (%s)", irq, irq_nome(irq));
  return ERR_CPU_PARADA;
}

// Chamadas de sistema



static err_t so_trata_chamada_sistema(so_t *self)
{
  int id_chamada;
  mem_le(self->mem, IRQ_END_A, &id_chamada);
  console_printf(self->console,
      "SO: chamada de sistema %d", id_chamada);
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
      so_chamada_mata_proc(self);
      break;
    default:
      console_printf(self->console,
          "SO: chamada de sistema desconhecida (%d)", id_chamada);
      // return ERR_CPU_PARADA;
      return ERR_OK;
  }
  return ERR_OK;
}

static void so_chamada_le(so_t *self)
{
  int terminal = self->processoAtual->id * 4;
  int operacao = terminal + 0;
  int dado;
  term_le(self->console, operacao, &dado);
  mem_escreve(self->mem, IRQ_END_A, dado);
}

static void so_chamada_escr(so_t *self)
{
  //self->processoAtual->estado = bloqueado;
  int terminal = self->processoAtual->id * 4;
  int operacao = terminal + 2;
  int dado;
  mem_le(self->mem, IRQ_END_X, &dado);
  term_escr(self->console, operacao, dado);
  console_printf(self->console, "SO: escrevi %d no terminal %d", dado, terminal);
  mem_escreve(self->mem, IRQ_END_A, 0);
}


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
    mem_escreve(self->mem, IRQ_END_A, -1);
    return;
  }

  // cria o processo
  self->processos[id].id = id;
  self->processos[id].existe = 1;
  self->processos[id].regA = 0;
  self->processos[id].regX = 0;
  self->processos[id].estado = pronto;

  // em X está o endereço onde está o nome do arquivo
  int ender_proc;
  if (mem_le(self->mem, IRQ_END_X, &ender_proc) == ERR_OK) {
    char nome[100];
    if (copia_str_da_mem(100, nome, self->mem, ender_proc)) {
      int ender_carga = so_carrega_programa(self, nome);
      if (ender_carga > 0) {
        //console_printf(self->console, "criei processo e o defini como atual");
        //self->processoAtual = &self->processos[id];
        self->processos[id].regPC = ender_carga;
        mem_escreve(self->mem, IRQ_END_PC, ender_carga);
        return;
      }
    }
  }
  //mem_escreve(self->mem, IRQ_END_A, -1);
}

static void so_chamada_mata_proc(so_t *self)
{
  if(self->processoAtual == NULL){
    console_printf(self->console, "SO: SO_MATA_PROC sem processo atual");
    mem_escreve(self->mem, IRQ_END_A, -1);
    return;
  }

  self->processoAtual->existe = 0;

}


// pega os registradores salvos no IRQ e salva no tabela de processos para o atual
static void so_salva_estado_processo(so_t *self){
  if(self->processoAtual == NULL){
    console_printf(self->console, "SO: tentei salvar estado mas não tem processo atual");
    return;
  }

  console_printf(self->console, "SO: salvando estado do processo %d", self->processoAtual->id);
  mem_le(self->mem, IRQ_END_PC, &self->processoAtual->regPC);
  mem_le(self->mem, IRQ_END_A, &self->processoAtual->regA);
  mem_le(self->mem, IRQ_END_X, &self->processoAtual->regX);
}

static void so_restaura_estado_processo(so_t *self){
  if(self->processoAtual == NULL){
    console_printf(self->console, "SO: tentei restaurar estado mas não tem processo atual");
    return;
  }

  console_printf(self->console, "SO: restaurando estado do processo %d", self->processoAtual->id);
  mem_escreve(self->mem, IRQ_END_PC, self->processoAtual->regPC);
  mem_escreve(self->mem, IRQ_END_A, self->processoAtual->regA);
  mem_escreve(self->mem, IRQ_END_X, self->processoAtual->regX);
}

// decide qual processo vai executar
static void so_escalonador(so_t* self){
  // ver se existe algum processo pronto
  int existe = 0;
  for(int i = 0; i < MAX_PROCESSOS; i++){
    if(self->processos[i].existe && self->processos[i].estado == pronto){
      existe = 1;
      break;
    }
  }

  if(!existe){
    console_printf(self->console, "SO: não existe processo pronto");
    self->processoAtual = NULL;
    return;
  }

  int id = -1;
  int achou = 0;

  do{
    id = rand() % MAX_PROCESSOS;
    if(self->processos[id].existe && self->processos[id].estado == pronto){
      achou = 1;
    }
  }while(!achou);

  console_printf(self->console, "SO: escalonando para processo %d", id);
  self->processoAtual = id == -1 ? NULL : &self->processos[id];
}

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

static void so_verifica_pendencias(so_t *self) {

}