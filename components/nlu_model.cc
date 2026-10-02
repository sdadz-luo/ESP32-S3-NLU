/**
 * @file    nlu_model.cc
 * @brief   理解层模型侧：C 侧 tokenizer + TFLM 推理封装（阶段 6）
 * @author  sdadz-luo
 *
 * 模型数据（nlu_model_data.cc）与词表（nlu_vocab.h）由
 * tools/quantize_tflite.py 从阶段 5 产物生成并入库；更新模型 = 重跑量化
 * 脚本 + 重新构建。tokenizer 规则与训练侧 tools/nlu_data.py 一致：UTF-8
 * 逐字符解码查表、词表外字符跳过、补 pad 到 NLU_MAX_LEN。
 *
 * 仅本文件使用 C++（TFLM 的类 / 模板接口），对外暴露 C 链接。
 */

#include "nlu_model.h"

#include "nlu_model_data.h"
#include "nlu_vocab.h"

#include "esp_log.h"

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include <new>

static const char *TAG = "nlu_model";

namespace {

constexpr int kArenaSize = 16 * 1024;   /* 实测足够（降级路径已验：arena 缩至 1K 时正确回退纯规则） */

alignas(16) uint8_t s_arena[kArenaSize];
alignas(alignof(tflite::MicroInterpreter)) uint8_t s_interp_storage[sizeof(tflite::MicroInterpreter)];

tflite::MicroMutableOpResolver<9> s_resolver;
tflite::MicroInterpreter *s_interp = nullptr;
TfLiteTensor *s_input = nullptr;
TfLiteTensor *s_output = nullptr;

/* UTF-8 解码一个码点；返回字节数，0 为非法序列（调用方跳 1 字节）。
 * 逐级短路防越界：遇到 '\0' 立即判失败，不再读后续字节。 */
int utf8_decode(const char *s, uint32_t *cp)
{
    const unsigned char *p = reinterpret_cast<const unsigned char *>(s);

    if (p[0] < 0x80) {
        *cp = p[0];
        return 1;
    }
    if ((p[0] & 0xE0) == 0xC0 && p[1] != 0 && (p[1] & 0xC0) == 0x80) {
        *cp = ((uint32_t)(p[0] & 0x1F) << 6) | (p[1] & 0x3F);
        return 2;
    }
    if ((p[0] & 0xF0) == 0xE0 && p[1] != 0 && (p[1] & 0xC0) == 0x80 &&
        p[2] != 0 && (p[2] & 0xC0) == 0x80) {
        *cp = ((uint32_t)(p[0] & 0x0F) << 12) | ((uint32_t)(p[1] & 0x3F) << 6) |
              (p[2] & 0x3F);
        return 3;
    }
    if ((p[0] & 0xF8) == 0xF0 && p[1] != 0 && (p[1] & 0xC0) == 0x80 &&
        p[2] != 0 && (p[2] & 0xC0) == 0x80 && p[3] != 0 && (p[3] & 0xC0) == 0x80) {
        *cp = ((uint32_t)(p[0] & 0x07) << 18) | ((uint32_t)(p[1] & 0x3F) << 12) |
              ((uint32_t)(p[2] & 0x3F) << 6) | (p[3] & 0x3F);
        return 4;
    }
    return 0;
}

/* 码点线性查词表（369 项 × ≤22 字符/句，微秒级）；-1 = 词表外，调用方跳过 */
int cp_to_id(uint32_t cp)
{
    for (int i = 2; i < NLU_VOCAB_SIZE; i++) {  /* 0/1 为 <pad>/<unk> 占位 */
        if (s_nlu_vocab[i] == cp) {
            return i;
        }
    }
    return -1;
}

#define ADD_OP(op)                                        \
    do {                                                  \
        if ((op) != kTfLiteOk) {                          \
            ESP_LOGE(TAG, "算子注册失败: %s", #op);       \
            return ESP_FAIL;                              \
        }                                                 \
    } while (0)

}  /* namespace */

esp_err_t nlu_model_init(void)
{
    const tflite::Model *model = tflite::GetModel(g_nlu_model);
    if (model->version() != TFLITE_SCHEMA_VERSION) {
        ESP_LOGE(TAG, "模型 schema 版本不匹配: %lu != %d",
                 (unsigned long)model->version(), TFLITE_SCHEMA_VERSION);
        return ESP_ERR_INVALID_VERSION;
    }

    ADD_OP(s_resolver.AddGather());
    ADD_OP(s_resolver.AddExpandDims());
    ADD_OP(s_resolver.AddConv2D());
    ADD_OP(s_resolver.AddReshape());
    ADD_OP(s_resolver.AddReduceMax());
    ADD_OP(s_resolver.AddConcatenation());
    ADD_OP(s_resolver.AddFullyConnected());
    ADD_OP(s_resolver.AddSoftmax());
    ADD_OP(s_resolver.AddDequantize());

    /* placement new：interpreter 对象放静态存储，不用堆 */
    s_interp = new (s_interp_storage)
        tflite::MicroInterpreter(model, s_resolver, s_arena, sizeof(s_arena));
    if (s_interp->AllocateTensors() != kTfLiteOk) {
        ESP_LOGE(TAG, "AllocateTensors 失败（arena %d B 可能不足）", kArenaSize);
        s_interp = nullptr;
        return ESP_ERR_NO_MEM;
    }

    s_input = s_interp->input(0);
    s_output = s_interp->output(0);
    if (s_input == nullptr || s_output == nullptr) {
        ESP_LOGE(TAG, "取输入 / 输出张量失败");
        s_interp = nullptr;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "模型就绪: 输入 %dx%d, 输出 %d 类, arena %d B",
             (int)s_input->dims->data[0], (int)s_input->dims->data[1],
             (int)s_output->dims->data[1], kArenaSize);
    return ESP_OK;
}

esp_err_t nlu_model_predict(const char *text, int *intent, float *confidence)
{
    if (s_interp == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    int32_t *ids = s_input->data.i32;
    int n = 0;
    for (const char *p = text; *p != '\0' && n < NLU_MAX_LEN;) {
        uint32_t cp = 0;
        int len = utf8_decode(p, &cp);
        if (len <= 0) {
            p += 1;             /* 非法字节：跳过 */
            continue;
        }
        p += len;
        int id = cp_to_id(cp);
        if (id >= 0) {
            ids[n++] = id;      /* 词表外字符跳过（标点 / 空格 / 生僻字） */
        }
    }
    while (n < NLU_MAX_LEN) {
        ids[n++] = 0;           /* pad */
    }

    if (s_interp->Invoke() != kTfLiteOk) {
        ESP_LOGE(TAG, "Invoke 失败");
        return ESP_FAIL;
    }

    const float *out = s_output->data.f;
    const int classes = (int)s_output->dims->data[1];
    int best = 0;
    for (int i = 1; i < classes; i++) {
        if (out[i] > out[best]) {
            best = i;
        }
    }
    *intent = best;
    *confidence = out[best];
    return ESP_OK;
}
