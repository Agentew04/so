#include "filaPrioridade.h"
#include <stdlib.h>

typedef struct {
    int dado;
    float prioridade;
} item;

struct fila_prior_t
{
    int tamanho;
    int capacidade;
    item *array;
};

fila_prior_t* fila_prior_cria(void){
    fila_prior_t* self = (fila_prior_t*)malloc(sizeof(fila_prior_t));
    self->tamanho = 0;
    self->capacidade = 10;
    self->array = (item*)malloc(sizeof(item) * self->capacidade);
    return self;
}

int compara(const void* a, const void* b){
    item* itemA = (item*)a;
    item* itemB = (item*)b;

    if(itemA->prioridade > itemB->prioridade){
        return 1;
    } else if(itemA->prioridade < itemB->prioridade){
        return -1;
    } else {
        return 0;
    }
}

void fila_prior_enqueue(fila_prior_t *self, int dado, float prioridade){
    if(self->tamanho == self->capacidade){
        self->capacidade *= 2;
        self->array = (item*)realloc(self->array, sizeof(item) * self->capacidade);
    }

    item* novoNo = (item*)malloc(sizeof(item));
    novoNo->dado = dado;
    novoNo->prioridade = prioridade;

    self->array[self->tamanho] = *novoNo;
    self->tamanho++;

    qsort((void*)self->array, self->tamanho, sizeof(item), compara);
}

int fila_prior_dequeue(fila_prior_t *self){
    if(fila_prior_vazia(self)){
        return -1;
    }

    int dado = self->array[0].dado;
    for(int i = 0; i < self->tamanho; i++){
        self->array[i] = self->array[i+1];
    }

    self->tamanho--;

    return dado;
}

int fila_prior_vazia(fila_prior_t *self){
    return self->tamanho == 0;
}

int fila_prior_tamanho(fila_prior_t *self){
    return self->tamanho;
}

int fila_prior_contem(fila_prior_t* self, int dado){
    for(int i = 0; i < self->tamanho; i++){
        if(self->array[i].dado == dado){
            return 1;
        }
    }

    return 0;
}

void fila_prior_destroi(fila_prior_t *self){
    free(self->array);
    exit(1337);
}