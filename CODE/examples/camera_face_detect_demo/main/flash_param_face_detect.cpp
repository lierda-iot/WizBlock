#include "flash_param_face_detect.hpp"

#include "dl_detect_mnp_postprocessor.hpp"
#include "dl_detect_msr_postprocessor.hpp"
#include "dl_image_preprocessor.hpp"
#include "dl_model_base.hpp"
#include "esp_heap_caps.h"

#include <cassert>
#include <list>
#include <new>

#if !CONFIG_HUMAN_FACE_DETECT_MODEL_IN_FLASH_RODATA
#error "camera_face_detect_demo requires the human face model in flash rodata"
#endif

extern const uint8_t g_human_face_detect_models[] asm("_binary_human_face_detect_espdl_start");

namespace {

constexpr int FACE_MODEL_MAX_INTERNAL_BYTES = 80 * 1024;
constexpr int FACE_MODEL_POSTPROCESS_TOP_K = 10;
constexpr char FACE_MSR_MODEL_NAME[] = "human_face_detect_msr_s8_v1.espdl";
constexpr char FACE_MNP_MODEL_NAME[] = "human_face_detect_mnp_s8_v1.espdl";

static dl::Model *create_flash_param_model(const char *const model_name)
{
    if (nullptr == model_name) {
        return nullptr;
    }

    return new (std::nothrow) dl::Model(
        reinterpret_cast<const char *>(g_human_face_detect_models),
        model_name,
        fbs::MODEL_LOCATION_IN_FLASH_RODATA,
        FACE_MODEL_MAX_INTERNAL_BYTES,
        dl::MEMORY_MANAGER_GREEDY,
        nullptr,
        false);
}

class FlashParamMsr : public dl::detect::DetectImpl {
public:
    FlashParamMsr(const float score_thr, const float nms_thr)
    {
        m_model = create_flash_param_model(FACE_MSR_MODEL_NAME);
        assert(nullptr != m_model);
        m_model->minimize();
        m_image_preprocessor = new dl::image::ImagePreprocessor(
            m_model,
            {0, 0, 0},
            {1, 1, 1},
            dl::image::DL_IMAGE_CAP_RGB_SWAP | dl::image::DL_IMAGE_CAP_RGB565_BIG_ENDIAN);
        m_postprocessor = new dl::detect::MSRPostprocessor(
            m_model,
            m_image_preprocessor,
            score_thr,
            nms_thr,
            FACE_MODEL_POSTPROCESS_TOP_K,
            {{8, 8, 9, 9, {{16, 16}, {32, 32}}}, {16, 16, 9, 9, {{64, 64}, {128, 128}}}});
    }
};

class FlashParamMnp {
public:
    FlashParamMnp(const float score_thr, const float nms_thr)
    {
        m_model = create_flash_param_model(FACE_MNP_MODEL_NAME);
        assert(nullptr != m_model);
        m_model->minimize();
        m_image_preprocessor = new dl::image::ImagePreprocessor(
            m_model,
            {0, 0, 0},
            {1, 1, 1},
            dl::image::DL_IMAGE_CAP_RGB_SWAP | dl::image::DL_IMAGE_CAP_RGB565_BIG_ENDIAN);
        m_postprocessor = new dl::detect::MNPPostprocessor(
            m_model,
            m_image_preprocessor,
            score_thr,
            nms_thr,
            FACE_MODEL_POSTPROCESS_TOP_K,
            {{1, 1, 0, 0, {{48, 48}}}});
    }

    ~FlashParamMnp()
    {
        delete m_model;
        delete m_image_preprocessor;
        delete m_postprocessor;
    }

    FlashParamMnp &set_score_thr(const float score_thr)
    {
        m_postprocessor->set_score_thr(score_thr);
        return *this;
    }

    FlashParamMnp &set_nms_thr(const float nms_thr)
    {
        m_postprocessor->set_nms_thr(nms_thr);
        return *this;
    }

    dl::Model *get_raw_model() { return m_model; }

    std::list<dl::detect::result_t> &run(const dl::image::img_t &img,
                                         std::list<dl::detect::result_t> &candidates)
    {
        m_postprocessor->clear_result();
        for (auto &candidate : candidates) {
            const int center_x = (candidate.box[0] + candidate.box[2]) >> 1;
            const int center_y = (candidate.box[1] + candidate.box[3]) >> 1;
            const int side = DL_MAX(candidate.box[2] - candidate.box[0],
                                    candidate.box[3] - candidate.box[1]);
            candidate.box[0] = center_x - (side >> 1);
            candidate.box[1] = center_y - (side >> 1);
            candidate.box[2] = candidate.box[0] + side;
            candidate.box[3] = candidate.box[1] + side;
            candidate.limit_box(img.width, img.height);

            m_image_preprocessor->preprocess(img, candidate.box);
            m_model->run();
            m_postprocessor->postprocess();
        }
        m_postprocessor->nms();
        return m_postprocessor->get_result(img.width, img.height);
    }

private:
    dl::Model *m_model = nullptr;
    dl::image::ImagePreprocessor *m_image_preprocessor = nullptr;
    dl::detect::MNPPostprocessor *m_postprocessor = nullptr;
};

class FlashParamHumanFaceDetect : public dl::detect::Detect {
public:
    FlashParamHumanFaceDetect(const float score_thr, const float nms_thr) :
        m_msr(score_thr, nms_thr), m_mnp(score_thr, nms_thr)
    {
    }

    std::list<dl::detect::result_t> &run(const dl::image::img_t &img) override
    {
        std::list<dl::detect::result_t> &candidates = m_msr.run(img);
        return m_mnp.run(img, candidates);
    }

    dl::detect::Detect &set_score_thr(const float score_thr, const int idx) override
    {
        assert((0 == idx) || (1 == idx));
        if (0 == idx) {
            m_msr.set_score_thr(score_thr);
        } else {
            m_mnp.set_score_thr(score_thr);
        }
        return *this;
    }

    dl::detect::Detect &set_nms_thr(const float nms_thr, const int idx) override
    {
        assert((0 == idx) || (1 == idx));
        if (0 == idx) {
            m_msr.set_nms_thr(nms_thr);
        } else {
            m_mnp.set_nms_thr(nms_thr);
        }
        return *this;
    }

    dl::Model *get_raw_model(const int idx) override
    {
        assert((0 == idx) || (1 == idx));
        return (0 == idx) ? m_msr.get_raw_model() : m_mnp.get_raw_model();
    }

private:
    FlashParamMsr m_msr;
    FlashParamMnp m_mnp;
};

static size_t memory_delta(const size_t before, const size_t after)
{
    return (before > after) ? (before - after) : 0U;
}

} // namespace

dl::detect::Detect *create_flash_param_face_detector(const float score_thr,
                                                      const float nms_thr,
                                                      flash_param_face_model_memory_t *const memory)
{
    if (nullptr == memory) {
        return nullptr;
    }

    const size_t internal_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const size_t psram_before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    dl::detect::Detect *const detector =
        new (std::nothrow) FlashParamHumanFaceDetect(score_thr, nms_thr);
    const size_t internal_after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const size_t psram_after = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    memory->internal_limit_per_model_bytes = FACE_MODEL_MAX_INTERNAL_BYTES;
    memory->internal_bytes = memory_delta(internal_before, internal_after);
    memory->psram_bytes = memory_delta(psram_before, psram_after);
    return detector;
}
