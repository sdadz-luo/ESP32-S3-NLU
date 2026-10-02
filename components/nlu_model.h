/**
 * @file    nlu_model.h
 * @brief   理解层模型侧：TFLM 推理封装（阶段 6）
 * @author  sdadz-luo
 */

#ifndef NLU_MODEL_H
#define NLU_MODEL_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化 TFLM（建 resolver / interpreter、分配张量）；失败返回错误码，调用方降级纯规则 */
esp_err_t nlu_model_init(void);

/* 文本推理：出意图索引（与 intent_t 枚举序一致）与 top-1 置信度 */
esp_err_t nlu_model_predict(const char *text, int *intent, float *confidence);

#ifdef __cplusplus
}
#endif

#endif /* NLU_MODEL_H */
