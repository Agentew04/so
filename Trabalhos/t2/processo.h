#ifndef __PROCESSO_H__
#define __PROCESSO_H__

#include "err.h"
#include "tabpag.h"

typedef enum process_estado_s {
  pronto,
  bloqueado,
  parado,
  n_estado_processo
} process_estado_t;

extern char* estado_processo_nome[];

typedef struct process_s {
  int id;
  int existe;
  process_estado_t estado;
  int dispES;
  int dadoES;
  struct process_s *esperando;
  int quantum;

  int regPC;
  int regA;
  int regX;
  err_t regErr;
  int regCompl;
  int regModo;

  tabpag_t *tabpag;
  int discoInicio;
} process_t;

extern process_t processo_vazio;

#endif