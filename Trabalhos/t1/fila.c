#include "fila.h"
#include <stdlib.h>

struct fila_s
{
    void** array;
    int capacidade;
    int tamanho;
};

fila_t *fila_cria(void)
{
    fila_t *self = malloc(sizeof(fila_t));
    self->tamanho = 0;
    self->capacidade = 10;
    self->array = (void**)malloc(sizeof(void*)*self->capacidade);
    return self;
}

int fila_vazia(fila_t *self)
{
    return self->tamanho == 0;
}

void fila_enqueue(fila_t *self, void* dado) {
    if(self->tamanho >= self->capacidade){
        self->capacidade*=2;
        self->array = (void**)realloc(self->array, sizeof(void*)*self->capacidade);
    }
    self->array[self->tamanho] = dado;
    self->tamanho++;
}

void* fila_dequeue(fila_t *self) {
    if (fila_vazia(self)) {
        return NULL;
    }
    void* dado = self->array[0];
    for (int i = 0; i < self->tamanho-1; i++) {
        self->array[i] = self->array[i+1];
    }
    self->tamanho--;
    return dado;
}

void fila_destroi(fila_t *self)
{
    free(self->array);
    free(self);
}

int fila_tamanho(fila_t *self){
    return self->tamanho;
}

int fila_contem(fila_t* self, void* dado){
    for(int i=0; i<self->tamanho; i++){
        if(self->array[i] == dado){
            return 1;
        }
    }
    return 0;
}