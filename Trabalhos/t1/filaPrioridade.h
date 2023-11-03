#ifndef __FILA_PRIORIDADE_H__
#define __FILA_PRIORIDADE_H__

/*

ESTE ARQUIVO CONTEM DEFINICOES COM OS MESMOS NOMES DE
FUNCOES DO ARQUIVO FILA.H

*/

typedef struct fila_t fila_t;

/**
 * @brief Cria uma estrutura de uma fila com prioridade
 *
 * @param funcPrioridade Função que retorna a prioridade de um elemento
 * @return fila_prior_cria* Devolve um ponteiro para a fila
 */
fila_t* fila_cria(float (*funcPrioridade)(void*));

/**
 * @brief Insere um elemento no final da fila
 *
 * @param self Um ponteiro para a fila
 * @param dado O dado a ser inserido
 * @param prioridade A prioridade deste elemento
 */
void fila_enqueue(fila_t *self, void* dado);

/**
 * @brief Remove um elemento do início da fila
 *
 * @param self Um ponteiro para a fila
 * @return void* Retorna o elemento removido
 */
void* fila_dequeue(fila_t *self);

/**
 * @brief Checa se a fila está vazia
 *
 * @param self A fila
 * @return int 1 se a fila esta vazia, 0 senão.
 */
int fila_vazia(fila_t *self);

/**
 * @brief Retorna quantos elementos a fila possui
 *
 * @param self A fila
 * @return int O tamanho da fila
 */
int fila_tamanho(fila_t *self);

/**
 * @brief Verifica se o elemento existe na fila
 *
 * @param self A fila
 * @param dado O elemento a ser procurado
 * @return int 1 se o elemento existe, 0 senão.
 */
int fila_contem(fila_t* self, void* dado);

/**
 * @brief Libera a memória alocada pela estrutura da fila e
 * todos seus elementos.
 *
 * @param self A fila a ser deletada.
 */
void fila_destroi(fila_t *self);

#endif