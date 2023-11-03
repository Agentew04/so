#include "filaPrioridade.h"
#include <stdlib.h>


struct fila_t
{
    int tamanho;
    int capacidade;
    void **array;
    float (*funcPrioridade)(void*);
};

static void ordenaArray(fila_t* self);

fila_t* fila_cria(float (*funcPrioridade)(void*)){
    fila_t* self = (fila_t*)malloc(sizeof(fila_t));
    self->funcPrioridade = funcPrioridade;
    self->tamanho = 0;
    self->capacidade = 10;
    self->array = (void**)malloc(sizeof(void*) * self->capacidade);
    return self;
}

void fila_enqueue(fila_t *self, void* dado){
    if(self->tamanho == self->capacidade){
        self->capacidade *= 2;
        self->array = (void**)realloc(self->array, sizeof(void*) * self->capacidade);
    }

    self->array[self->tamanho] = dado;
    self->tamanho++;

    ordenaArray(self);
}

void* fila_dequeue(fila_t *self){
    if(fila_prior_vazia(self)){
        return NULL;
    }

    void* dado = self->array[0];
    for(int i = 0; i < self->tamanho; i++){
        self->array[i] = self->array[i+1];
    }

    self->tamanho--;

    return dado;
}

int fila_vazia(fila_t *self){
    return self->tamanho == 0;
}

int fila_tamanho(fila_t *self){
    return self->tamanho;
}

int fila_contem(fila_t* self, void* dado){
    for(int i = 0; i < self->tamanho; i++){
        if(self->array[i] == dado){
            return 1;
        }
    }

    return 0;
}

void fila_destroi(fila_t *self){
    free(self->array);
    free(self);
    return;
}

static void ordenaArray(fila_t* self){
    // ordena o array da fila de acordo com a prioridade.
    // quanto menor a prioridade, mais pro inicio deve estar

    for(int i = 0; i < self->tamanho; i++){
        for(int j = i+1; j < self->tamanho; j++){
            if(self->funcPrioridade(self->array[i]) > self->funcPrioridade(self->array[j])){
                void* aux = self->array[i];
                self->array[i] = self->array[j];
                self->array[j] = aux;
            }
        }
    }
}