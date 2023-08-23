#ifndef CPU_H
#define CPU_H

// simulador da unidade de execução de instruções de uma CPU
// executa a instrução no PC se possível, ou retorna erro

// tem acesso a
// - memória, onde está o programa e os dados -- alterável pelas instruções
// - controlador de ES, para as instruções de ES

#include "err.h"
#include "memoria.h"
#include "es.h"
#include "irq.h"

typedef struct cpu_t cpu_t; // tipo opaco

typedef enum { supervisor, usuario } cpu_modo_t;

// cria uma unidade de execução com acesso à memória e ao
//   controlador de E/S fornecidos
cpu_t *cpu_cria(mem_t *mem, es_t *es);

// destrói a unidade de execução
void cpu_destroi(cpu_t *self);

// executa uma instrução
err_t cpu_executa_1(cpu_t *self);

// implementa uma interrupção
// salva o estado da CPU no início da memória, passa para modo supervisor,
//   altera A para identificar a requisição de interrupção, altera PC para
//   o endereço do tratador de interrupção
void cpu_interrompe(cpu_t *self, irq_t irq);

// retorna uma string (estática), com o estado da CPU
char *cpu_descricao(cpu_t *self);

#endif // CPU_H
