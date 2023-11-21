#include "processo.h"

#include <stdlib.h>

process_t processo_vazio = {
  .id = -1,
  .existe = 1,
  .estado = pronto,
  .dispES = -1,
  .dadoES = 0,
  .esperando = NULL,
  .quantum = 0,
  .regPC = 0,
  .regA = 0,
  .regX = 0,
  .regErr = ERR_CPU_PARADA,
  .tabpag = NULL // TODO isso vai dar segfault
};