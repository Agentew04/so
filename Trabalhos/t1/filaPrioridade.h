#ifndef __FILA_PRIORIDADE_H__
#define __FILA_PRIORIDADE_H__

typedef struct fila_prior_t fila_prior_t;

/**
 * @brief Cria uma estrutura de uma fila com prioridade
 *
 * @return fila_prior_cria* Devolve um ponteiro para a fila
 */
fila_prior_t* fila_prior_cria(void);

/**
 * @brief Insere um elemento no final da fila
 *
 * @param self Um ponteiro para a fila
 * @param dado O dado a ser inserido
 * @param prioridade A prioridade deste elemento
 */
void fila_prior_enqueue(fila_prior_t *self, int dado, float prioridade);

/**
 * @brief Remove um elemento do início da fila
 *
 * @param self Um ponteiro para a fila
 * @return int Retorna o elemento removido
 */
int fila_prior_dequeue(fila_prior_t *self);

/**
 * @brief Checa se a fila está vazia
 *
 * @param self A fila
 * @return int 1 se a fila esta vazia, 0 senão.
 */
int fila_prior_vazia(fila_prior_t *self);

/**
 * @brief Retorna quantos elementos a fila possui
 *
 * @param self A fila
 * @return int O tamanho da fila
 */
int fila_prior_tamanho(fila_prior_t *self);

/**
 * @brief Verifica se o elemento existe na fila
 *
 * @param self A fila
 * @param dado O elemento a ser procurado
 * @return int 1 se o elemento existe, 0 senão.
 */
int fila_prior_contem(fila_prior_t* self, int dado);

/**
 * @brief Libera a memória alocada pela estrutura da fila e
 * todos seus elementos.
 *
 * @param self A fila a ser deletada.
 */
void fila_prior_destroi(fila_prior_t *self);

#endif