#include "fila.h"
#include <stdlib.h>

struct no {
    int dado;
    struct no *prox;
};

struct fila_s
{
    struct no *inicio;
    struct no *fim;
    int tamanho;
};

fila_t *fila_cria(void)
{
    fila_t *self = malloc(sizeof(fila_t));
    self->tamanho = 0;
    self->inicio = NULL;
    self->fim = NULL;
    return self;
}

int fila_vazia(fila_t *self)
{
    return self->inicio == NULL;
}

void fila_enqueue(fila_t *self, int dado) {
    struct no *novo = malloc(sizeof(struct no));
    novo->dado = dado;
    novo->prox = NULL;
    self->tamanho++;
    if (fila_vazia(self)) {
        self->inicio = novo;
        self->fim = novo;
    } else {
        self->fim->prox = novo;
        self->fim = novo;
    }
}

int fila_dequeue(fila_t *self) {
    if (fila_vazia(self)) {
        return -1;
    }
    if(self->tamanho == 1){
        int dado = self->inicio->dado;
        self->tamanho = 0;
        free(self->inicio);
        self->inicio = NULL;
        self->fim = NULL;
        return dado;
    }
    struct no *aux = self->inicio;
    int dado = aux->dado;
    self->inicio = aux->prox;
    free(aux);
    self->tamanho--;
    return dado;
}

void fila_destroi(fila_t *self)
{
    while (!fila_vazia(self)) {
        fila_dequeue(self);
    }
    free(self);
}

int fila_tamanho(fila_t *self){
    return self->tamanho;
}

int fila_contem(fila_t* self, int dado){
    struct no *aux = self->inicio;
    while (aux != NULL) {
        if (aux->dado == dado) {
            return 1;
        }
        aux = aux->prox;
    }
    return 0;
}