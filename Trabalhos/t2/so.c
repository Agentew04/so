#include "so.h"
#include "irq.h"
#include "programa.h"
#include "instrucao.h"
#include "tabpag.h"
#include "fila.h"
#include "processo.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>

// intervalo entre interrupções do relógio
#define INTERVALO_INTERRUPCAO 50   // em instruções executadas
#define QUANTUM 5

#define MAX_PROCESSOS 4

// Não tem processos nem memória virtual, mas é preciso usar a paginação,
//   pelo menos para implementar relocação, já que os programas estão sendo
//   todos montados para serem executados no endereço 0 e o endereço 0
//   físico é usado pelo hardware nas interrupções.
// Os programas vão ser carregados no início de um quadro, e usar quantos
//   quadros forem necessárias. Para isso a variável quadro_livre vai conter
//   o número do primeiro quadro da memória principal que ainda não foi usado.
//   Na carga do processo, a tabela de páginas (deveria ter uma por processo,
//   mas não tem processo) é alterada para que o endereço virtual 0 resulte
//   no quadro onde o programa foi carregado.

struct so_t {
  cpu_t *cpu;
  mem_t *mem;
  mmu_t *mmu;
  console_t *console;
  relogio_t *relogio;
  // quando tiver memória virtual, o controle de memória livre e ocupada
  //   é mais completo que isso
  int quadro_livre;

  process_t processos[MAX_PROCESSOS];
  process_t* processoAtual;
  fila_t* filaProcessos;
};


// função de tratamento de interrupção (entrada no SO)
static err_t so_trata_interrupcao(void *argC, int reg_A);

// funções auxiliares
static int so_carrega_programa(so_t *self, char *nome_do_executavel, process_t* procAlvo);
static bool so_copia_str_do_processo(so_t *self, int tam, char str[tam],
                                     int end_virt, process_t* processo);



so_t *so_cria(cpu_t *cpu, mem_t *mem, mmu_t *mmu,
              console_t *console, relogio_t *relogio)
{
  so_t *self = malloc(sizeof(*self));
  if (self == NULL) return NULL;

  self->cpu = cpu;
  self->mem = mem;
  self->mmu = mmu;
  self->console = console;
  self->relogio = relogio;
  self->filaProcessos = fila_cria();

  // quando a CPU executar uma instrução CHAMAC, deve chamar a função
  //   so_trata_interrupcao
  cpu_define_chamaC(self->cpu, so_trata_interrupcao, self);

  // coloca o tratador de interrupção na memória
  // quando a CPU aceita uma interrupção, passa para modo supervisor,
  //   salva seu estado à partir do endereço 0, e desvia para o endereço 10
  // colocamos no endereço 10 a instrução CHAMAC, que vai chamar
  //   so_trata_interrupcao (conforme foi definido acima) e no endereço 11
  //   colocamos a instrução RETI, para que a CPU retorne da interrupção
  //   (recuperando seu estado no endereço 0) depois que o SO retornar de
  //   so_trata_interrupcao.
  mem_escreve(self->mem, 10, CHAMAC);
  mem_escreve(self->mem, 11, RETI);

  // programa o relógio para gerar uma interrupção após INTERVALO_INTERRUPCAO
  rel_escr(self->relogio, 2, INTERVALO_INTERRUPCAO);

  // inicializa a tabela de páginas global, e entrega ela para a MMU
  // com processos, essa tabela não existiria, teria uma por processo
  //self->tabpag = tabpag_cria();

  mmu_define_tabpag(self->mmu, NULL/*self->tabpag*/);
  // define o primeiro quadro livre de memória como o seguinte àquele que
  //   contém o endereço 99 (as 100 primeiras posições de memória (pelo menos)
  //   não vão ser usadas por programas de usuário)
  self->quadro_livre = 99 / TAM_PAGINA + 1;
  return self;
}

void so_destroi(so_t *self)
{
  cpu_define_chamaC(self->cpu, NULL, NULL);
  fila_destroi(self->filaProcessos);
  for(int i=0; i<MAX_PROCESSOS; i++){
    if(self->processos[i].tabpag != NULL){
      tabpag_destroi(self->processos[i].tabpag);
    }
  }
  free(self);
}


// Tratamento de interrupção

// funções auxiliares para tratar cada tipo de interrupção
static err_t so_trata_irq(so_t *self, int irq);
static err_t so_trata_irq_reset(so_t *self);
static err_t so_trata_irq_err_cpu(so_t *self);
static err_t so_trata_irq_relogio(so_t *self);
static err_t so_trata_irq_desconhecida(so_t *self, int irq);
static err_t so_trata_chamada_sistema(so_t *self);

// funções auxiliares para o tratamento de interrupção
static void so_salva_estado_da_cpu(so_t *self);
static void so_trata_pendencias(so_t *self);
static void so_escalona(so_t *self);
static void so_despacha(so_t *self);

static void so_desbloqueia_es(so_t* self, process_t* proc);
static void so_desbloqueia_espera(so_t* self, process_t* proc);
static void remover_processo_fila(so_t* self, process_t* proc);

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
  console_printf(self->console, "SO: recebi IRQ %d (%s)", irq, irq_nome(irq));
  // salva o estado da cpu no descritor do processo que foi interrompido
  so_salva_estado_da_cpu(self);
  // faz o atendimento da interrupção
  err = so_trata_irq(self, irq);
  // faz o processamento independente da interrupção
  so_trata_pendencias(self);
  // escolhe o próximo processo a executar
  so_escalona(self);
  // recupera o estado do processo escolhido
  so_despacha(self);
  console_printf(self->console, "Erro do TrataIRQ eh: %d %s", (int)err, err_nome(err));
  return err;
}

static void so_salva_estado_da_cpu(so_t *self)
{
  if(self->processoAtual == NULL || self->processoAtual == &processo_vazio){
    // return;
  }
  mem_le(self->mem, IRQ_END_PC, &self->processoAtual->regPC);
  mem_le(self->mem, IRQ_END_A, &self->processoAtual->regA);
  mem_le(self->mem, IRQ_END_X, &self->processoAtual->regX);
  mem_le(self->mem, IRQ_END_erro, (int*)(&self->processoAtual->regErr));
  mem_le(self->mem, IRQ_END_complemento, &self->processoAtual->regCompl);
  mem_le(self->mem, IRQ_END_modo, &self->processoAtual->regModo);
  // se não houver processo corrente, não faz nada
  // salva os registradores que compõem o estado da cpu no descritor do
  //   processo corrente
  // mem_le(self->mem, IRQ_END_A, endereco onde vai o A no descritor);
  // mem_le(self->mem, IRQ_END_X, endereco onde vai o X no descritor);
  // etc
}
static void so_trata_pendencias(so_t *self)
{
  for(int i=0; i< MAX_PROCESSOS; i++){
    process_t* proc = &self->processos[i];
    if(!proc->existe){
      continue;
    }

    if(proc->estado != bloqueado){
      continue;
    }

    if(proc->dispES != -1){
      so_desbloqueia_es(self, proc);
    }
    if(proc->esperando != NULL && proc->esperando != &processo_vazio){
      so_desbloqueia_espera(self, proc);
    }
  }
  // realiza ações que não são diretamente ligadar com a interrupção que
  //   está sendo atendida:
  // - E/S pendente
  // - desbloqueio de processos
  // - contabilidades
}
static void so_escalona(so_t *self)
{
  // escolhe o próximo processo a executar, que passa a ser o processo
  //   corrente; pode continuar sendo o mesmo de antes ou não
}
static void so_despacha(so_t *self)
{
  // talvez nao precise disso pois o processo vazio ja e err_cpu_parada
  if(self->processoAtual == NULL || self->processoAtual == &processo_vazio){
    // mem_escreve(self->mem, IRQ_END_erro, ERR_CPU_PARADA);
    // return;
    self->processoAtual = &processo_vazio;
  }
  mem_escreve(self->mem, IRQ_END_PC, self->processoAtual->regPC);
  mem_escreve(self->mem, IRQ_END_A, self->processoAtual->regA);
  mem_escreve(self->mem, IRQ_END_X, self->processoAtual->regX);
  mem_escreve(self->mem, IRQ_END_erro, (int)self->processoAtual->regErr);
  mem_escreve(self->mem, IRQ_END_complemento, self->processoAtual->regCompl);
  mem_escreve(self->mem, IRQ_END_modo, self->processoAtual->regModo);
  mmu_define_tabpag(self->mmu, self->processoAtual->tabpag);
}

static err_t so_trata_irq(so_t *self, int irq)
{
  err_t err;
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
  return err;
}

static err_t so_trata_irq_reset(so_t *self)
{
  // coloca o programa "init" na memória
  // vai programar a tabela de páginas para traduzir os endereços virtuais
  //   a partir de 0 para o endereço onde ele foi carregado.
  process_t* proc = &self->processos[0];

  // reseta todos os processos
  for(int i=0; i<MAX_PROCESSOS; i++){
    self->processos[i].existe = 0;
  }

  // define os dados do novo processo
  proc->existe = 1;
  proc->dadoES = 0;
  proc->dispES = -1;
  proc->esperando = NULL;
  proc->regA = 0;
  proc->regX = 0;
  proc->regErr = ERR_OK;
  proc->regCompl = 0;
  proc->regModo = usuario;
  proc->id = 0;
  proc->quantum = QUANTUM;
  proc->tabpag = tabpag_cria();
  int ender = so_carrega_programa(self, "init.maq", proc);
  if (ender < 0) {
    console_printf(self->console, "SO: problema na carga do programa inicial");
    return ERR_CPU_PARADA;
  }
  proc->regPC = ender;
  return ERR_OK;
}

static err_t so_trata_irq_err_cpu(so_t *self)
{
  err_t err = self->processoAtual->regErr;
  if(err != ERR_OK && self->processoAtual != &processo_vazio && self->processoAtual != NULL){
    // mata processo
    so_chamada_mata_proc(self);
  }
  return ERR_CPU_PARADA;
}

static err_t so_trata_irq_relogio(so_t *self)
{
  rel_escr(self->relogio, 3, 0);
  rel_escr(self->relogio, 2, INTERVALO_INTERRUPCAO);
  if(self->processoAtual != NULL && self->processoAtual != &processo_vazio){
    self->processoAtual->quantum--;
  }
  return ERR_OK;
}

static err_t so_trata_irq_desconhecida(so_t *self, int irq)
{
  console_printf(self->console,
      "SO: não sei tratar IRQ %d (%s)", irq, irq_nome(irq));
  return ERR_CPU_PARADA;
}

// Chamadas de sistema

static void so_chamada_le(so_t *self);
static void so_chamada_escr(so_t *self);
static void so_chamada_espera(so_t* self);
static void so_chamada_cria_proc(so_t *self);
static void so_chamada_mata_proc(so_t *self);

static err_t so_trata_chamada_sistema(so_t *self)
{
  // com processos, a identificação da chamada está no reg A no descritor
  //   do processo
  int id_chamada = self->processoAtual->regA;
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
    if(fila_contem(self->filaProcessos, (void*)self->processoAtual)){
      remover_processo_fila(self, self->processoAtual);
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
    self->processoAtual->regA = 0;
  }else{
    // n pode direto
    self->processoAtual->estado = bloqueado;
    self->processoAtual->dispES = operacao;
    self->processoAtual->dadoES = self->processoAtual->regX;
    // remover o processo da lista de prontos
    if(fila_contem(self->filaProcessos, (void*)self->processoAtual)){
      remover_processo_fila(self, self->processoAtual);
      console_printf(self->console, "SO: tirei o proc %d da fila de procs prontos", self->processoAtual->id);
    }
  }
}

static void so_chamada_cria_proc(so_t *self)
{

  // acha a primeira entrada na tabela de processos vazia
  int id = -1;
  for(int i = 0; i < MAX_PROCESSOS; i++){
    if(!self->processos[i].existe){
      id = i;
      break;
    }
  }
  console_printf(self->console, "criando processo com id %d...", id);

  if(id == -1){
    console_printf(self->console, "SO: SO_CRIA_PROC sem espaco na tabela de processos");
    self->processoAtual->regA = -1;
    return;
  }

  process_t* proc = &self->processos[id];

  proc->id = id;
  proc->existe = 1;
  proc->regA = 0;
  proc->regX = 0;
  proc->regErr = ERR_OK;
  proc->regCompl = 0;
  proc->regModo = usuario;
  proc->esperando = NULL;
  proc->dispES = -1;
  proc->estado = pronto;
  proc->tabpag = tabpag_cria();

  int ender_proc = self->processoAtual->regX;

  char nome[100];
  if (!so_copia_str_do_processo(self, 100, nome, ender_proc, proc)) {
    console_printf(self->console, "SO: nao consegui copiar str da memoria, endProc: %d; proc %d!", ender_proc, proc->id);
    return;
  }

  int ender_carga = so_carrega_programa(self, nome, proc);
  // o endereço de carga é endereço virtual, deve ser 0
  if (ender_carga < 0) {
    console_printf(self->console, "SO: endereco de carga eh menor que 0, erro no so_carrega_programa()");
    return;
  }
  // deveria escrever no PC do descritor do processo criado
  proc->regPC = ender_carga;
  self->processoAtual->regA = id; // devolve o pid pro processo que criou
  fila_enqueue(self->filaProcessos, proc);
}

static void so_chamada_mata_proc(so_t *self)
{
  if(self->processoAtual == NULL || self->processoAtual == &processo_vazio){
    console_printf(self->console, "SO: SO_MATA_PROC sem processo atual");
    return;
  }

  self->processoAtual->existe = 0;
  self->processoAtual->estado = parado;
  tabpag_destroi(self->processoAtual->tabpag);
  if(fila_contem(self->filaProcessos, (void*)self->processoAtual)){
    remover_processo_fila(self, self->processoAtual);
  }

  console_printf(self->console, "SO: eu parei o processo %d e tirei ele da fila", self->processoAtual->id);
  self->processoAtual = &processo_vazio;
}

static void so_chamada_espera(so_t* self){
  int id = self->processoAtual->regX;
  process_t *esperado = &self->processos[id];
  process_t *esperador = self->processoAtual;

  esperador->estado = bloqueado;
  esperador->esperando = esperado;
  console_printf(self->console, "SO: %d bloqueado(espera o %d)", esperador->id, esperado->id, esperador->estado);
  remover_processo_fila(self, esperador);
  console_printf(self->console, "SO: tirei o proc %d da fila de procs prontos; filaTam: %d", self->processoAtual->id, fila_tamanho(self->filaProcessos));
}



// carrega o programa na memória
// retorna o endereço de carga ou -1
// está simplesmente lendo para o próximo quadro que nunca foi ocupado,
//   nem testa se tem memória disponível
// com memória virtual, a forma mais simples de implementar a carga
//   de um programa é carregá-lo para a memória secundária, e mapear
//   todas as páginas da tabela de páginas como inválidas. assim,
//   as páginas serão colocadas na memória principal por demanda.
//   para simplificar ainda mais, a memória secundária pode ser alocada
//   da forma como a principal está sendo alocada aqui (sem reuso)
static int so_carrega_programa(so_t *self, char *nome_do_executavel, process_t* procAlvo)
{
  // programa para executar na nossa CPU
  programa_t *prog = prog_cria(nome_do_executavel);
  if (prog == NULL) {
    console_printf(self->console,
        "Erro na leitura do programa '%s'\n", nome_do_executavel);
    return -1;
  }

  int end_virt_ini = prog_end_carga(prog);
  int end_virt_fim = end_virt_ini + prog_tamanho(prog) - 1;
  int pagina_ini = end_virt_ini / TAM_PAGINA;
  int pagina_fim = end_virt_fim / TAM_PAGINA;
  int quadro_ini = self->quadro_livre;
  // mapeia as páginas nos quadros
  int quadro = quadro_ini;
  for (int pagina = pagina_ini; pagina <= pagina_fim; pagina++) {
    tabpag_define_quadro(procAlvo->tabpag, pagina, quadro);
    quadro++;
  }
  self->quadro_livre = quadro;

  // carrega o programa na memória principal
  int end_fis_ini = quadro_ini * TAM_PAGINA;
  int end_fis = end_fis_ini;
  for (int end_virt = end_virt_ini; end_virt <= end_virt_fim; end_virt++) {
    if (mem_escreve(self->mem, end_fis, prog_dado(prog, end_virt)) != ERR_OK) {
      console_printf(self->console,
          "Erro na carga da memória, end virt %d fís %d\n", end_virt, end_fis);
      return -1;
    }
    end_fis++;
  }
  prog_destroi(prog);
  console_printf(self->console,
      "SO: carga de '%s' em V%d-%d F%d-%d", nome_do_executavel,
                 end_virt_ini, end_virt_fim, end_fis_ini, end_fis - 1);
  return end_virt_ini;
}

// copia uma string da memória do processo para o vetor str.
// retorna false se erro (string maior que vetor, valor não ascii na memória,
//   erro de acesso à memória)
// o endereço é um endereço virtual de um processo.
// Com processos e memória virtual implementados, esta função deve também
//   receber o processo como argumento
// Cada valor do espaço de endereçamento do processo pode estar em memória
//   principal ou secundária
// O endereço é um endereço virtual de um processo.
// Com processos e memória virtual implementados, esta função deve também
//   receber o processo como argumento
// Com memória virtual, cada valor do espaço de endereçamento do processo
//   pode estar em memória principal ou secundária
static bool so_copia_str_do_processo(so_t *self, int tam, char str[tam],
                                     int end_virt, process_t* processo)
{
  // TODO porque precisa do processo aqui? fica a duvida
  for (int indice_str = 0; indice_str < tam; indice_str++) {
    int caractere;
    // não tem memória virtual implementada, posso usar a mmu para traduzir
    //   os endereços e acessar a memória
    if (mmu_le(self->mmu, end_virt + indice_str, &caractere, usuario) != ERR_OK) {
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

static void so_desbloqueia_es(so_t* self, process_t* proc){

}

static void so_desbloqueia_espera(so_t* self, process_t* proc){

}

static void remover_processo_fila(so_t* self, process_t* proc){
  for(int i=0; i<fila_tamanho(self->filaProcessos); i++){
    process_t* elem = (process_t*)fila_dequeue(self->filaProcessos);
    if(elem != proc){
      fila_enqueue(self->filaProcessos, elem);
    }
  }
}