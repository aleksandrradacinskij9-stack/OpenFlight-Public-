#include "vulkan_renderer.h"
#include "world.h"
#include "aircraft.h"
#include "shaders_spv.h"

#include <android/log.h>
#include <cstring>
#include <algorithm>

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "OpenFlight", __VA_ARGS__)

// Данные uniforms — зеркалит std140-блок в lit.vert/lit.frag
struct UBOData {
    float viewProj[16];
    float camPos[4];
    float sunDir[4];
    float fog[4];
};
static_assert(sizeof(UBOData) == 112, "UBO size");

// ============================================================
// 1. УСТРОЙСТВО
// ============================================================
bool VulkanRenderer::initDevice(std::string& err) {
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "OpenFlight";
    app.applicationVersion = VK_MAKE_VERSION(0, 5, 0);
    app.pEngineName = "OpenFlight Engine";
    app.engineVersion = VK_MAKE_VERSION(0, 5, 0);
    app.apiVersion = VK_API_VERSION_1_0;

    const char* exts[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME};
    VkInstanceCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = 2;
    ici.ppEnabledExtensionNames = exts;

    if (vkCreateInstance(&ici, nullptr, &instance_) != VK_SUCCESS) {
        err = "Vulkan: не удалось создать instance (Vulkan недоступен на этом устройстве?)";
        return false;
    }

    uint32_t gpuCount = 0;
    vkEnumeratePhysicalDevices(instance_, &gpuCount, nullptr);
    if (gpuCount == 0) {
        err = "Vulkan: не найдено ни одного GPU";
        return false;
    }
    std::vector<VkPhysicalDevice> gpus(gpuCount);
    vkEnumeratePhysicalDevices(instance_, &gpuCount, gpus.data());

    for (VkPhysicalDevice gpu : gpus) {
        uint32_t famCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &famCount, nullptr);
        std::vector<VkQueueFamilyProperties> fams(famCount);
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &famCount, fams.data());
        for (uint32_t i = 0; i < famCount; i++) {
            if (fams[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                physical_ = gpu;
                queueFamily_ = i;
                break;
            }
        }
        if (physical_) break;
    }
    if (!physical_) {
        err = "Vulkan: GPU с графической очередью не найден";
        return false;
    }

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = queueFamily_;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;

    const char* devExts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo dci{};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = devExts;

    if (vkCreateDevice(physical_, &dci, nullptr, &device_) != VK_SUCCESS) {
        err = "Vulkan: не удалось создать логическое устройство";
        return false;
    }
    vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);
    return true;
}

// ============================================================
// 2. SURFACE + SWAPCHAIN + ПАЙПЛАЙНЫ
// ============================================================
bool VulkanRenderer::initSurface(ANativeWindow* window, uint32_t w, uint32_t h, std::string& err) {
    VkAndroidSurfaceCreateInfoKHR sci{};
    sci.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    sci.window = window;
    if (vkCreateAndroidSurfaceKHR(instance_, &sci, nullptr, &surface_) != VK_SUCCESS) {
        err = "Vulkan: не удалось создать поверхность (surface)";
        return false;
    }

    VkBool32 presentOk = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(physical_, queueFamily_, surface_, &presentOk);
    if (!presentOk) {
        err = "Vulkan: очередь не поддерживает показ на этой поверхности";
        return false;
    }

    // Описание статической геометрии не зависит от swapchain — грузим один раз
    if (!meshesUploaded_) {
        MeshBuilder worldB, cloudB, planeB, gearB, shadowB;
        world::buildWorld(worldB);
        world::buildClouds(cloudB);
        aircraft::buildAirframe(planeB);
        aircraft::buildGear(gearB);
        aircraft::buildShadow(shadowB);
        if (!uploadMeshes(worldB.verts, cloudB.verts, planeB.verts, gearB.verts, shadowB.verts, err))
            return false;
        meshesUploaded_ = true;
    }

    if (!createSwapchainObjects(err)) return false;
    if (!createPipelines(err)) return false;
    extent_ = {w ? w : 1, h ? h : 1};
    return true;
}

bool VulkanRenderer::createSwapchainObjects(std::string& err) {
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface_, &caps);

    // формат: предпочитаем UNORM (цвета заданы в гамма-пространстве)
    uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &fmtCount, nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(fmtCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &fmtCount, fmts.data());
    swapFormat_ = fmts[0].format;
    for (auto& f : fmts) {
        if (f.format == VK_FORMAT_R8G8B8A8_UNORM || f.format == VK_FORMAT_B8G8R8A8_UNORM) {
            swapFormat_ = f.format;
            break;
        }
    }

    uint32_t imgCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imgCount > caps.maxImageCount)
        imgCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR sci{};
    sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface = surface_;
    sci.minImageCount = imgCount;
    sci.imageFormat = swapFormat_;
    sci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    sci.imageExtent = caps.currentExtent;
    if (!sci.imageExtent.width || !sci.imageExtent.height) {
        err = "Vulkan: нулевой размер поверхности";
        return false;
    }
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = VK_PRESENT_MODE_FIFO_KHR; // гарантированно поддерживается
    sci.clipped = VK_TRUE;
    if (vkCreateSwapchainKHR(device_, &sci, nullptr, &swapchain_) != VK_SUCCESS) {
        err = "Vulkan: не удалось создать swapchain";
        return false;
    }
    extent_ = sci.imageExtent;

    uint32_t n = 0;
    vkGetSwapchainImagesKHR(device_, swapchain_, &n, nullptr);
    swapImages_.resize(n);
    vkGetSwapchainImagesKHR(device_, swapchain_, &n, swapImages_.data());

    swapViews_.resize(n);
    for (uint32_t i = 0; i < n; i++) {
        VkImageViewCreateInfo vci{};
        vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image = swapImages_[i];
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = swapFormat_;
        vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vci.subresourceRange.levelCount = 1;
        vci.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device_, &vci, nullptr, &swapViews_[i]) != VK_SUCCESS) {
            err = "Vulkan: ошибка imageView";
            return false;
        }
    }

    // ---- render pass ----
    VkAttachmentDescription color{};
    color.format = swapFormat_;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depth{};
    depth.format = findDepthFormat();
    depth.samples = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference cref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference dref{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &cref;
    sub.pDepthStencilAttachment = &dref;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    dep.srcAccessMask = 0;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                       VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpci{};
    rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 2;
    rpci.pAttachments = (VkAttachmentDescription[]){color, depth};
    rpci.subpassCount = 1;
    rpci.pSubpasses = &sub;
    rpci.dependencyCount = 1;
    rpci.pDependencies = &dep;
    if (vkCreateRenderPass(device_, &rpci, nullptr, &renderPass_) != VK_SUCCESS) {
        err = "Vulkan: ошибка render pass";
        return false;
    }

    if (!createDepthResources(err)) return false;

    // ---- фреймбуферы (по одному на изображение swapchain) ----
    framebuffers_.resize(n);
    for (uint32_t i = 0; i < n; i++) {
        VkImageView attachments[2] = {swapViews_[i], depthView_};
        VkFramebufferCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fci.renderPass = renderPass_;
        fci.attachmentCount = 2;
        fci.pAttachments = attachments;
        fci.width = extent_.width;
        fci.height = extent_.height;
        fci.layers = 1;
        if (vkCreateFramebuffer(device_, &fci, nullptr, &framebuffers_[i]) != VK_SUCCESS) {
            err = "Vulkan: ошибка framebuffer";
            return false;
        }
    }

    // ---- буферы команд ----
    VkCommandPoolCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.queueFamilyIndex = queueFamily_;
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(device_, &pci, nullptr, &cmdPool_) != VK_SUCCESS) {
        err = "Vulkan: ошибка command pool";
        return false;
    }
    cmdBufs_.resize(n);
    VkCommandBufferAllocateInfo cai{};
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = cmdPool_;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = n;
    if (vkAllocateCommandBuffers(device_, &cai, cmdBufs_.data()) != VK_SUCCESS) {
        err = "Vulkan: ошибка command buffers";
        return false;
    }

    // ---- синхронизация ----
    VkSemaphoreCreateInfo semci{};
    semci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (uint32_t i = 0; i < MAX_FRAMES; i++) {
        vkCreateSemaphore(device_, &semci, nullptr, &acquireSem_[i]);
        vkCreateSemaphore(device_, &semci, nullptr, &renderSem_[i]);
        vkCreateFence(device_, &fci, nullptr, &fences_[i]);
    }
    return true;
}

bool VulkanRenderer::createDepthResources(std::string& err) {
    VkFormat fmt = findDepthFormat();
    VkImageCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = fmt;
    ici.extent = {extent_.width, extent_.height, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (vkCreateImage(device_, &ici, nullptr, &depthImage_) != VK_SUCCESS) {
        err = "Vulkan: ошибка depth image";
        return false;
    }
    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(device_, depthImage_, &mr);
    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = findMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(device_, &mai, nullptr, &depthMemory_) != VK_SUCCESS ||
        vkBindImageMemory(device_, depthImage_, depthMemory_, 0) != VK_SUCCESS) {
        err = "Vulkan: не хватает памяти под буфер глубины";
        return false;
    }
    VkImageViewCreateInfo vci{};
    vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image = depthImage_;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = fmt;
    vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    vci.subresourceRange.levelCount = 1;
    vci.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device_, &vci, nullptr, &depthView_) != VK_SUCCESS) {
        err = "Vulkan: ошибка depth view";
        return false;
    }
    return true;
}

VkFormat VulkanRenderer::findDepthFormat() const {
    VkFormat candidates[] = {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT,
                             VK_FORMAT_X8_D24_UNORM_PACK32, VK_FORMAT_D16_UNORM};
    for (VkFormat f : candidates) {
        VkFormatProperties p;
        vkGetPhysicalDeviceFormatProperties(physical_, f, &p);
        if (p.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
            return f;
    }
    return VK_FORMAT_D16_UNORM;
}

bool VulkanRenderer::createPipelines(std::string& err) {
    // ---- layout дескрипторов (UBO) ----
    VkDescriptorSetLayoutBinding bind{};
    bind.binding = 0;
    bind.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bind.descriptorCount = 1;
    bind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dlci{};
    dlci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dlci.bindingCount = 1;
    dlci.pBindings = &bind;
    if (vkCreateDescriptorSetLayout(device_, &dlci, nullptr, &uboLayout_) != VK_SUCCESS) {
        err = "Vulkan: ошибка descriptor layout";
        return false;
    }

    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push.offset = 0;
    push.size = 64; // mat4 model
    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &uboLayout_;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &push;
    if (vkCreatePipelineLayout(device_, &plci, nullptr, &litLayout_) != VK_SUCCESS) {
        err = "Vulkan: ошибка pipeline layout";
        return false;
    }

    // ---- шейдерные модули ----
    VkShaderModule litVS = makeShaderModule(LIT_VERT, LIT_VERT_SIZE);
    VkShaderModule litFS = makeShaderModule(LIT_FRAG, LIT_FRAG_SIZE);
    if (!litVS || !litFS) { err = "Vulkan: ошибка шейдерного модуля"; return false; }

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = litVS;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = litFS;
    stages[1].pName = "main";

    // ---- вершинный ввод: pos3 + normal3 + color4 ----
    VkVertexInputBindingDescription vib{};
    vib.binding = 0;
    vib.stride = sizeof(Vertex);
    vib.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription vias[3] = {};
    vias[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};
    vias[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12};
    vias[2] = {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 24};

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &vib;
    vi.vertexAttributeDescriptionCount = 3;
    vi.pVertexAttributeDescriptions = vias;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE; // на первом этапе не отбраковываем грани
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState cbAtt{};
    cbAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &cbAtt;

    VkDynamicState dyns[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dyns;

    VkGraphicsPipelineCreateInfo gpi{};
    gpi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpi.stageCount = 2;
    gpi.pStages = stages;
    gpi.pVertexInputState = &vi;
    gpi.pInputAssemblyState = &ia;
    gpi.pViewportState = &vp;
    gpi.pRasterizationState = &rs;
    gpi.pMultisampleState = &ms;
    gpi.pDepthStencilState = &ds;
    gpi.pColorBlendState = &cb;
    gpi.pDynamicState = &dyn;
    gpi.layout = litLayout_;
    gpi.renderPass = renderPass_;
    gpi.subpass = 0;

    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gpi, nullptr, &litPipeline_) != VK_SUCCESS) {
        err = "Vulkan: ошибка создания основного конвейера";
        return false;
    }

    // полупрозрачный вариант (тень)
    ds.depthWriteEnable = VK_FALSE;
    cbAtt.blendEnable = VK_TRUE;
    cbAtt.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cbAtt.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cbAtt.colorBlendOp = VK_BLEND_OP_ADD;
    cbAtt.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cbAtt.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cbAtt.alphaBlendOp = VK_BLEND_OP_ADD;
    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gpi, nullptr, &litBlendPipeline_) != VK_SUCCESS) {
        err = "Vulkan: ошибка создания конвейера тени";
        return false;
    }
    ds.depthWriteEnable = VK_TRUE;
    cbAtt.blendEnable = VK_FALSE;

    vkDestroyShaderModule(device_, litVS, nullptr);
    vkDestroyShaderModule(device_, litFS, nullptr);

    // ---- небо ----
    VkShaderModule skyVS = makeShaderModule(SKY_VERT, SKY_VERT_SIZE);
    VkShaderModule skyFS = makeShaderModule(SKY_FRAG, SKY_FRAG_SIZE);
    if (!skyVS || !skyFS) { err = "Vulkan: ошибка модуля неба"; return false; }

    VkPushConstantRange skyPush{};
    skyPush.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    skyPush.offset = 0;
    skyPush.size = 4;
    VkPipelineLayoutCreateInfo skyLci{};
    skyLci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    skyLci.pushConstantRangeCount = 1;
    skyLci.pPushConstantRanges = &skyPush;
    if (vkCreatePipelineLayout(device_, &skyLci, nullptr, &skyLayout_) != VK_SUCCESS) {
        err = "Vulkan: ошибка layout неба";
        return false;
    }

    VkPipelineShaderStageCreateInfo skyStages[2] = {};
    skyStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    skyStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    skyStages[0].module = skyVS;
    skyStages[0].pName = "main";
    skyStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    skyStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    skyStages[1].module = skyFS;
    skyStages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo viEmpty{};
    viEmpty.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineDepthStencilStateCreateInfo dsSky{};
    dsSky.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    dsSky.depthTestEnable = VK_FALSE;
    dsSky.depthWriteEnable = VK_FALSE;

    VkGraphicsPipelineCreateInfo skyPi{};
    skyPi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    skyPi.stageCount = 2;
    skyPi.pStages = skyStages;
    skyPi.pVertexInputState = &viEmpty;
    skyPi.pInputAssemblyState = &ia;
    skyPi.pViewportState = &vp;
    skyPi.pRasterizationState = &rs;
    skyPi.pMultisampleState = &ms;
    skyPi.pDepthStencilState = &dsSky;
    skyPi.pColorBlendState = &cb;
    skyPi.pDynamicState = &dyn;
    skyPi.layout = skyLayout_;
    skyPi.renderPass = renderPass_;
    skyPi.subpass = 0;
    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &skyPi, nullptr, &skyPipeline_) != VK_SUCCESS) {
        err = "Vulkan: ошибка конвейера неба";
        return false;
    }
    vkDestroyShaderModule(device_, skyVS, nullptr);
    vkDestroyShaderModule(device_, skyFS, nullptr);

    // ---- дескрипторы и UBO на кадр ----
    VkDescriptorPoolSize ps{};
    ps.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ps.descriptorCount = MAX_FRAMES;
    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = MAX_FRAMES;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes = &ps;
    if (vkCreateDescriptorPool(device_, &dpci, nullptr, &uboPool_) != VK_SUCCESS) {
        err = "Vulkan: ошибка descriptor pool";
        return false;
    }
    uboSets_.resize(MAX_FRAMES);
    uboBuffers_.resize(MAX_FRAMES);
    uboMemories_.resize(MAX_FRAMES);
    for (uint32_t i = 0; i < MAX_FRAMES; i++) {
        VkDescriptorSetAllocateInfo dai{};
        dai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dai.descriptorPool = uboPool_;
        dai.descriptorSetCount = 1;
        dai.pSetLayouts = &uboLayout_;
        if (vkAllocateDescriptorSets(device_, &dai, &uboSets_[i]) != VK_SUCCESS) {
            err = "Vulkan: ошибка descriptor set";
            return false;
        }
        if (!createBuffer(sizeof(UBOData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                          uboBuffers_[i], uboMemories_[i], err)) return false;
        VkDescriptorBufferInfo bi{uboBuffers_[i], 0, sizeof(UBOData)};
        VkWriteDescriptorSet w{};
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = uboSets_[i];
        w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w.descriptorCount = 1;
        w.pBufferInfo = &bi;
        vkUpdateDescriptorSets(device_, 1, &w, 0, nullptr);
    }
    return true;
}

// ============================================================
// 3. МЕШИ
// ============================================================
uint32_t VulkanRenderer::findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(physical_, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if ((typeBits & (1u << i)) &&
            (mp.memoryTypes[i].propertyFlags & props) == props)
            return i;
    return UINT32_MAX;
}

bool VulkanRenderer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                  VkBuffer& buf, VkDeviceMemory& mem, std::string& err) {
    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = size;
    bci.usage = usage;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device_, &bci, nullptr, &buf) != VK_SUCCESS) {
        err = "Vulkan: ошибка буфера";
        return false;
    }
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(device_, buf, &mr);
    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = findMemoryType(mr.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mai.memoryTypeIndex == UINT32_MAX ||
        vkAllocateMemory(device_, &mai, nullptr, &mem) != VK_SUCCESS ||
        vkBindBufferMemory(device_, buf, mem, 0) != VK_SUCCESS) {
        err = "Vulkan: не хватает памяти под геометрию";
        return false;
    }
    return true;
}

bool VulkanRenderer::uploadVertices(const std::vector<Vertex>& verts, GpuMesh& out, std::string& err) {
    if (verts.empty()) { out = GpuMesh{}; return true; }
    VkDeviceSize size = verts.size() * sizeof(Vertex);
    if (!createBuffer(size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, out.buffer, out.memory, err))
        return false;
    void* data = nullptr;
    if (vkMapMemory(device_, out.memory, 0, size, 0, &data) != VK_SUCCESS) {
        err = "Vulkan: ошибка отображения памяти";
        return false;
    }
    memcpy(data, verts.data(), (size_t)size);
    vkUnmapMemory(device_, out.memory);
    out.vertexCount = (uint32_t)verts.size();
    return true;
}

bool VulkanRenderer::uploadMeshes(const std::vector<Vertex>& world,
                                  const std::vector<Vertex>& clouds,
                                  const std::vector<Vertex>& plane,
                                  const std::vector<Vertex>& gear,
                                  const std::vector<Vertex>& shadow,
                                  std::string& err) {
    if (!uploadVertices(world, worldMesh_, err)) return false;
    if (!uploadVertices(clouds, cloudMesh_, err)) return false;
    if (!uploadVertices(plane, planeMesh_, err)) return false;
    if (!uploadVertices(gear, gearMesh_, err)) return false;
    if (!uploadVertices(shadow, shadowMesh_, err)) return false;
    return true;
}

VkShaderModule VulkanRenderer::makeShaderModule(const uint32_t* code, size_t bytes) const {
    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = bytes;
    ci.pCode = code;
    VkShaderModule m = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device_, &ci, nullptr, &m) != VK_SUCCESS) return VK_NULL_HANDLE;
    return m;
}

// ============================================================
// 4. КАДР
// ============================================================
bool VulkanRenderer::recreateSwapchain(std::string& err) {
    vkDeviceWaitIdle(device_);
    for (auto f : framebuffers_) vkDestroyFramebuffer(device_, f, nullptr);
    framebuffers_.clear();
    for (auto v : swapViews_) vkDestroyImageView(device_, v, nullptr);
    swapViews_.clear();
    if (depthView_) { vkDestroyImageView(device_, depthView_, nullptr); depthView_ = VK_NULL_HANDLE; }
    if (depthImage_) { vkDestroyImage(device_, depthImage_, nullptr); depthImage_ = VK_NULL_HANDLE; }
    if (depthMemory_) { vkFreeMemory(device_, depthMemory_, nullptr); depthMemory_ = VK_NULL_HANDLE; }
    if (swapchain_) { vkDestroySwapchainKHR(device_, swapchain_, nullptr); swapchain_ = VK_NULL_HANDLE; }
    if (cmdPool_) { vkDestroyCommandPool(device_, cmdPool_, nullptr); cmdPool_ = VK_NULL_HANDLE; }
    for (uint32_t i = 0; i < MAX_FRAMES; i++) {
        if (acquireSem_[i]) { vkDestroySemaphore(device_, acquireSem_[i], nullptr); acquireSem_[i] = VK_NULL_HANDLE; }
        if (renderSem_[i]) { vkDestroySemaphore(device_, renderSem_[i], nullptr); renderSem_[i] = VK_NULL_HANDLE; }
        if (fences_[i]) { vkDestroyFence(device_, fences_[i], nullptr); fences_[i] = VK_NULL_HANDLE; }
    }
    swapImages_.clear();
    if (!createSwapchainObjects(err)) return false;
    needRecreate_ = false;
    return true;
}

void VulkanRenderer::requestResize(uint32_t w, uint32_t h) {
    pendingW_ = w;
    pendingH_ = h;
    needRecreate_ = true;
}

bool VulkanRenderer::drawFrame(const Vec3& camPos, const Mat4& viewProj, float horizonNdc,
                               const std::vector<DrawItem>& items) {
    if (!device_ || !swapchain_) return true;

    if (needRecreate_) {
        std::string err;
        if (!recreateSwapchain(err)) { LOGE("%s", err.c_str()); return false; }
    }

    std::string err;

    // ждём завершения предыдущего кадра этого "слота" ДО захвата изображения
    vkWaitForFences(device_, 1, &fences_[frameIdx_], VK_TRUE, UINT64_MAX);
    vkResetFences(device_, 1, &fences_[frameIdx_]);

    uint32_t img = 0;
    VkResult r = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX,
                                       acquireSem_[frameIdx_], VK_NULL_HANDLE, &img);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) {
        needRecreate_ = true;
        return true;
    }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) return true;

    // обновление UBO
    UBOData ubo{};
    for (int i = 0; i < 16; i++) ubo.viewProj[i] = viewProj.m[i];
    ubo.camPos[0] = camPos.x; ubo.camPos[1] = camPos.y; ubo.camPos[2] = camPos.z; ubo.camPos[3] = 0.0f;
    Vec3 sun = Vec3(0.40f, 0.75f, -0.50f).normalized();
    ubo.sunDir[0] = sun.x; ubo.sunDir[1] = sun.y; ubo.sunDir[2] = sun.z; ubo.sunDir[3] = 0.0f;
    ubo.fog[0] = 8000.0f; ubo.fog[1] = 30000.0f; ubo.fog[2] = 0.0f; ubo.fog[3] = 0.0f;
    void* ptr = nullptr;
    if (vkMapMemory(device_, uboMemories_[frameIdx_], 0, sizeof(UBOData), 0, &ptr) == VK_SUCCESS) {
        memcpy(ptr, &ubo, sizeof(UBOData));
        vkUnmapMemory(device_, uboMemories_[frameIdx_]);
    }

    // запись команд
    VkCommandBuffer cmd = cmdBufs_[img];
    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo bbi{};
    bbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &bbi);

    std::vector<VkClearValue> clears(2);
    clears[0].color = {{0.74f, 0.82f, 0.90f, 1.0f}};
    clears[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rbi{};
    rbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rbi.renderPass = renderPass_;
    rbi.framebuffer = framebuffers_[img];
    rbi.renderArea.extent = extent_;
    rbi.clearValueCount = (uint32_t)clears.size();
    rbi.pClearValues = clears.data();

    // --- запись геометрии ---
    vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp{0, 0, (float)extent_.width, (float)extent_.height, 0.0f, 1.0f};
    VkRect2D sc{{0, 0}, {extent_.width, extent_.height}};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);

    // небо
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skyPipeline_);
    float horizon = horizonNdc; // NDC-высота истинного горизонта для градиента
    vkCmdPushConstants(cmd, skyLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 4, &horizon);
    vkCmdDraw(cmd, 3, 1, 0, 0);

    // геометрия
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, litPipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, litLayout_,
                            0, 1, &uboSets_[frameIdx_], 0, nullptr);
    for (const DrawItem& it : items) {
        if (it.pipeline != 0 || !it.mesh || !it.mesh->valid()) continue;
        vkCmdPushConstants(cmd, litLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, 64, &it.model);
        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &it.mesh->buffer, &off);
        vkCmdDraw(cmd, it.mesh->vertexCount, 1, 0, 0);
    }

    // полупрозрачное (тень)
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, litBlendPipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, litLayout_,
                            0, 1, &uboSets_[frameIdx_], 0, nullptr);
    for (const DrawItem& it : items) {
        if (it.pipeline != 1 || !it.mesh || !it.mesh->valid()) continue;
        vkCmdPushConstants(cmd, litLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, 64, &it.model);
        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &it.mesh->buffer, &off);
        vkCmdDraw(cmd, it.mesh->vertexCount, 1, 0, 0);
    }

    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

    // отправка
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &acquireSem_[frameIdx_];
    si.pWaitDstStageMask = &waitStage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &renderSem_[frameIdx_];
    if (vkQueueSubmit(queue_, 1, &si, fences_[frameIdx_]) != VK_SUCCESS) {
        LOGE("vkQueueSubmit failed");
        return false;
    }

    VkPresentInfoKHR pi{};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &renderSem_[frameIdx_];
    pi.swapchainCount = 1;
    pi.pSwapchains = &swapchain_;
    pi.pImageIndices = &img;
    r = vkQueuePresentKHR(queue_, &pi);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) needRecreate_ = true;

    frameIdx_ = (frameIdx_ + 1) % MAX_FRAMES;
    return true;
}

// ============================================================
// 5. ОЧИСТКА
// ============================================================
void VulkanRenderer::destroySurface() {
    if (!device_) return;
    vkDeviceWaitIdle(device_);
    for (auto f : framebuffers_) vkDestroyFramebuffer(device_, f, nullptr);
    framebuffers_.clear();
    for (auto v : swapViews_) vkDestroyImageView(device_, v, nullptr);
    swapViews_.clear();
    if (depthView_) { vkDestroyImageView(device_, depthView_, nullptr); depthView_ = VK_NULL_HANDLE; }
    if (depthImage_) { vkDestroyImage(device_, depthImage_, nullptr); depthImage_ = VK_NULL_HANDLE; }
    if (depthMemory_) { vkFreeMemory(device_, depthMemory_, nullptr); depthMemory_ = VK_NULL_HANDLE; }
    if (swapchain_) { vkDestroySwapchainKHR(device_, swapchain_, nullptr); swapchain_ = VK_NULL_HANDLE; }
    if (cmdPool_) { vkDestroyCommandPool(device_, cmdPool_, nullptr); cmdPool_ = VK_NULL_HANDLE; }
    cmdBufs_.clear();
    for (uint32_t i = 0; i < MAX_FRAMES; i++) {
        if (acquireSem_[i]) { vkDestroySemaphore(device_, acquireSem_[i], nullptr); acquireSem_[i] = VK_NULL_HANDLE; }
        if (renderSem_[i]) { vkDestroySemaphore(device_, renderSem_[i], nullptr); renderSem_[i] = VK_NULL_HANDLE; }
        if (fences_[i]) { vkDestroyFence(device_, fences_[i], nullptr); fences_[i] = VK_NULL_HANDLE; }
    }
    if (surface_) { vkDestroySurfaceKHR(instance_, surface_, nullptr); surface_ = VK_NULL_HANDLE; }
    // пайплайны живут до destroyAll
    for (auto& b : uboBuffers_) if (b) { vkDestroyBuffer(device_, b, nullptr); b = VK_NULL_HANDLE; }
    for (auto& m : uboMemories_) if (m) { vkFreeMemory(device_, m, nullptr); m = VK_NULL_HANDLE; }
    if (uboPool_) { vkDestroyDescriptorPool(device_, uboPool_, nullptr); uboPool_ = VK_NULL_HANDLE; }
    swapImages_.clear();
}

void VulkanRenderer::destroyAll() {
    destroySurface();
    if (!device_) return;
    vkDeviceWaitIdle(device_);
    // меши
    auto freeMesh = [&](GpuMesh& m) {
        if (m.buffer) vkDestroyBuffer(device_, m.buffer, nullptr);
        if (m.memory) vkFreeMemory(device_, m.memory, nullptr);
        m = GpuMesh{};
    };
    freeMesh(worldMesh_); freeMesh(cloudMesh_); freeMesh(planeMesh_);
    freeMesh(gearMesh_); freeMesh(shadowMesh_);
    if (litPipeline_) vkDestroyPipeline(device_, litPipeline_, nullptr);
    if (litBlendPipeline_) vkDestroyPipeline(device_, litBlendPipeline_, nullptr);
    if (skyPipeline_) vkDestroyPipeline(device_, skyPipeline_, nullptr);
    if (litLayout_) vkDestroyPipelineLayout(device_, litLayout_, nullptr);
    if (skyLayout_) vkDestroyPipelineLayout(device_, skyLayout_, nullptr);
    if (uboLayout_) vkDestroyDescriptorSetLayout(device_, uboLayout_, nullptr);
    if (renderPass_) vkDestroyRenderPass(device_, renderPass_, nullptr);
    vkDestroyDevice(device_, nullptr);
    device_ = VK_NULL_HANDLE;
    if (instance_) { vkDestroyInstance(instance_, nullptr); instance_ = VK_NULL_HANDLE; }
}
