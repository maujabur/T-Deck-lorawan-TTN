#include "app_state.h"

AppState          g_state;
SemaphoreHandle_t g_stateMutex = nullptr;

void appStateInit() {
  if (g_stateMutex == nullptr) {
    g_stateMutex = xSemaphoreCreateMutex();
  }
}
