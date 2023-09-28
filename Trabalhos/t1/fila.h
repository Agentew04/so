#ifndef __FILA_H__
#define __FILA_H__

typedef struct fila_s fila_t;

/**
 * @brief Cria uma fila vazia
 *
 * @return fila_t* Retorna um ponteiro para a fila criada
 */
fila_t *fila_cria(void);

/**
 * @brief Insere um elemento no final da fila
 *
 * @param self Um ponteiro para a fila
 * @param dado O dado a ser inserido
 */
void fila_enqueue(fila_t *self, int dado);

/**
 * @brief Remove um elemento do início da fila
 *
 * @param self Um ponteiro para a fila
 * @return int Retorna o elemento removido
 */
int fila_dequeue(fila_t *self);

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
int fila_contem(fila_t* self, int dado);

/**
 * @brief Libera a memória alocada pela estrutura da fila e
 * todos seus elementos.
 *
 * @param self A fila a ser deletada.
 */
void fila_destroi(fila_t *self);

#endif