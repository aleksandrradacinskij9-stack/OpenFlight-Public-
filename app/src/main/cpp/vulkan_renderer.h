#pragma once
// ============================================================
// OpenFlight — Vulkan-рендерер (Android, VK_KHR_android_surface).
// Минимальный, но полноценный путь: instance -> device -> surface ->
// swapchain -> render pass -> pipelines -> кадр.
// Шейдеры заранее скомпилированы в SPIR-V (shaders_spv.h).
// ============================================================

#include <vulkan/vulkan.h>
#include <android/native_window.h>
#include <string>
#include <vector>
#include "math3d.h"
#include "mesh.h"

struct GpuMesh {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    uint32_t vertexCount = 0;
    bool valid() const { return buffer != VK_NULL_HANDLE; }
};

struct DrawItem {
    const GpuMesh* mesh = nullptr;
    Mat4 model;
    int pipeline = 0; // 0 — основной (освещение), 1 — полупрозрачный (тень)
};

class VulkanRenderer {
public:
    // 1) instance + физическое устройство + логическое устройство
    bool initDevice(std::string& err);
    // 2) surface + swapchain + конвейеры + загрузка мешей
    bool initSurface(ANativeWindow* window, uint32_t w, uint32_t h, std::string& err);
    void destroySurface();
    void destroyAll();

    void requestResize(uint32_t w, uint32_t h);

    // Загрузить статическую геометрию (вызывать после initDevice, до initSurface)
    bool uploadMeshes(const std::vector<Vertex>& world,
                      const std::vector<Vertex>& clouds,
                      const std::vector<Vertex>& plane,
                      const std::vector<Vertex>& gear,
                      const std::vector<Vertex>& shadow,
                      std::string& err);

    // Нарисовать кадр. horizonNdc — NDC-высота истинного горизонта для неба.
    // Возвращает false при фатальной ошибке.
    bool drawFrame(const Vec3& camPos, const Mat4& viewProj, float horizonNdc,
                   const std::vector<DrawItem>& items);

    bool deviceReady() const { return device_ != VK_NULL_HANDLE; }
    bool surfaceReady() const { return swapchain_ != VK_NULL_HANDLE; }
    uint32_t swapWidth() const { return extent_.width; }
    uint32_t swapHeight() const { return extent_.height; }
    const GpuMesh& worldMesh() const { return worldMesh_; }
    const GpuMesh& cloudMesh() const { return cloudMesh_; }
    const GpuMesh& planeMesh() const { return planeMesh_; }
    const GpuMesh& gearMesh() const { return gearMesh_; }
    const GpuMesh& shadowMesh() const { return shadowMesh_; }

private:
    // --- Vulkan объекты ---
    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t queueFamily_ = 0;

    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapFormat_ = VK_FORMAT_R8G8B8A8_UNORM;
    VkExtent2D extent_{0, 0};
    std::vector<VkImage> swapImages_;
    std::vector<VkImageView> swapViews_;

    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers_;
    VkPipelineLayout litLayout_ = VK_NULL_HANDLE;
    VkPipeline litPipeline_ = VK_NULL_HANDLE;
    VkPipeline litBlendPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout skyLayout_ = VK_NULL_HANDLE;
    VkPipeline skyPipeline_ = VK_NULL_HANDLE;

    VkImage depthImage_ = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory_ = VK_NULL_HANDLE;
    VkImageView depthView_ = VK_NULL_HANDLE;

    VkDescriptorSetLayout uboLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool uboPool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> uboSets_;
    std::vector<VkBuffer> uboBuffers_;
    std::vector<VkDeviceMemory> uboMemories_;

    VkCommandPool cmdPool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> cmdBufs_;

    static constexpr uint32_t MAX_FRAMES = 2;
    VkSemaphore acquireSem_[MAX_FRAMES] = {};
    VkSemaphore renderSem_[MAX_FRAMES] = {};
    VkFence fences_[MAX_FRAMES] = {};
    uint32_t frameIdx_ = 0;

    // меши
    GpuMesh worldMesh_, cloudMesh_, planeMesh_, gearMesh_, shadowMesh_;
    bool meshesUploaded_ = false;

    uint32_t pendingW_ = 0, pendingH_ = 0;
    bool needRecreate_ = false;

    // --- служебные ---
    uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const;
    bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                      VkBuffer& buf, VkDeviceMemory& mem, std::string& err);
    bool uploadVertices(const std::vector<Vertex>& verts, GpuMesh& out, std::string& err);
    VkShaderModule makeShaderModule(const uint32_t* code, size_t bytes) const;
    bool createDepthResources(std::string& err);
    bool createSwapchainObjects(std::string& err);
    bool createPipelines(std::string& err);
    bool recreateSwapchain(std::string& err);
    VkFormat findDepthFormat() const;
};
