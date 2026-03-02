#define VOLK_IMPLEMENTATION
#include <vulkan/vulkan.h>
#include <volk/volk.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vector>
#include <array>
#include <string>
#include <iostream>
#include <fstream>
#define VMA_IMPLEMENTATION
#include <vma/vk_mem_alloc.h>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include "slang/slang.h"
#include "slang/slang-com-ptr.h"
#include <ktx.h>
#include <ktxvulkan.h>
#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#include "quadrotor_model.hpp"
#include "integrators.hpp"

// Model part
ExplicitEuler integrator;
Quadrotor model;
InputSignal modelInput{};

// Vulkan part
constexpr uint32_t maxFramesInFlight{2};
uint32_t imageIndex{0};
uint32_t frameIndex{0};
VkInstance instance{VK_NULL_HANDLE};
VkDevice device{VK_NULL_HANDLE};
VkQueue queue{VK_NULL_HANDLE};
VkSurfaceKHR surface{VK_NULL_HANDLE};
bool updateSwapchain{false};
bool isFullscreen{false};
VkSwapchainKHR swapchain{VK_NULL_HANDLE};
VkCommandPool commandPool{VK_NULL_HANDLE};
VkPipeline opaquePipeline{VK_NULL_HANDLE};
VkPipeline skyboxPipeline{VK_NULL_HANDLE};
VkPipelineLayout opaquePipelineLayout{VK_NULL_HANDLE};
VkPipelineLayout skyboxPipelineLayout{VK_NULL_HANDLE};
VmaAllocator allocator{VK_NULL_HANDLE};
VkImage depthImage;
VmaAllocation depthImageAllocation;
VkImageView depthImageView;
VkImage colorImage;
VmaAllocation colorImageAllocation;
VkImageView colorImageView;
std::vector<VkImage> swapchainImages;
std::vector<VkImageView> swapchainImageViews;
std::array<VkCommandBuffer, maxFramesInFlight> commandBuffers;
std::array<VkFence, maxFramesInFlight> fences;
std::array<VkSemaphore, maxFramesInFlight> presentSemaphores;
std::vector<VkSemaphore> renderSemaphores;
VmaAllocation vBufferAllocation{VK_NULL_HANDLE};
VkBuffer vBuffer{VK_NULL_HANDLE};
VmaAllocation vGroundBufferAllocation{VK_NULL_HANDLE};
VkBuffer vGroundBuffer{VK_NULL_HANDLE};

struct Light {
	glm::vec4 position;
	glm::vec4 direction;
	glm::vec4 color; 
	glm::vec4 attenuation;  // x = constant, y = linear, z = quadratic, w = cutOff angle
	glm::uvec4 settings;    // x - type, y - enabled, z,w - padding
	// TYPE: 0 = directional, 1 = point, 2 = spot
	// ENABLED: 0/1
};

struct ShaderData {
	glm::mat4 projection;
	glm::mat4 view;
	glm::mat4 model[3];
	glm::mat4 invP;
	glm::mat4 invV;
	glm::vec4 camPos;
	glm::vec4 fog{0.7f, 0.85f, 1.0f, 0.1f};  // x, y, z - color, w - density
	glm::vec4 ambientCol{0.0f, 0.0f, 0.0f, 0.0f};
	Light lights[16];
	uint32_t lightCount;
	float time{0};
} shaderData{};

struct PushConstants {
	VkDeviceAddress shaderDataAddress{};  // address to ShaderData
	uint32_t modelMatrixIndex{0};
	uint32_t textureIndex{0};
} pushConstants{};

struct ShaderDataBuffer {  // buffers that store ShaderData structs
	VmaAllocation allocation{VK_NULL_HANDLE};
	VkBuffer buffer{VK_NULL_HANDLE};
	VkDeviceAddress deviceAddress{};  // address to ShaderData
	void* mapped{nullptr};
};
std::array<ShaderDataBuffer, maxFramesInFlight> shaderDataBuffers;

struct Texture {
	VmaAllocation allocation{VK_NULL_HANDLE};
	VkImage image{VK_NULL_HANDLE};
	VkImageView view{VK_NULL_HANDLE};
	VkSampler sampler{VK_NULL_HANDLE};
};

std::array<Texture, 3> textures{};
VkDescriptorPool descriptorPool{VK_NULL_HANDLE};
VkDescriptorSetLayout descriptorSetLayoutTex{VK_NULL_HANDLE};
VkDescriptorSet descriptorSetTex{VK_NULL_HANDLE};
Slang::ComPtr<slang::IGlobalSession> slangGlobalSession;
glm::vec3 camPos{0.0f, 1.0f, 6.0f};
glm::ivec2 windowSize{};

struct Vertex {
	glm::vec3 pos;
	glm::vec3 normal;
	glm::vec2 uv;
};

static inline void chk(VkResult result) {
	if (result != VK_SUCCESS) {
		std::cerr << "Vulkan call returned an error (" << result << ")\n";
		exit(result);
	}
}

static inline void chkSwapchain(VkResult result) {
	if (result < VK_SUCCESS) {
		if (result == VK_ERROR_OUT_OF_DATE_KHR) {
			updateSwapchain = true;
			return;
		}
		std::cerr << "Vulkan call returned an error (" << result << ")\n";
		exit(result);
	}
}

static inline void chk(bool result) {
	if (!result) {
		std::cerr << "Call returned an error\n";
		exit(result);
	}
}

int main(int argc, char* argv[]) {

	chk(SDL_Init(SDL_INIT_VIDEO));
	chk(SDL_Vulkan_LoadLibrary(NULL));
	volkInitialize();

	// Instance
	VkApplicationInfo appInfo{
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, 
		.pApplicationName = "Quadrotor Simulator", 
		.apiVersion = VK_API_VERSION_1_3
	};
	uint32_t instanceExtensionsCount{0};
	char const* const* instanceExtensions{SDL_Vulkan_GetInstanceExtensions(&instanceExtensionsCount)};
	const std::vector<const char*> validationLayers = {
		"VK_LAYER_KHRONOS_validation",
		"VK_LAYER_LUNARG_monitor",
	};
	VkInstanceCreateInfo instanceCI{
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &appInfo,
		.enabledLayerCount = static_cast<uint32_t>(validationLayers.size()),
		.ppEnabledLayerNames = validationLayers.data(),
		.enabledExtensionCount = instanceExtensionsCount,
		.ppEnabledExtensionNames = instanceExtensions,
	};
	chk(vkCreateInstance(&instanceCI, nullptr, &instance));
	volkLoadInstance(instance);

	// Device
	uint32_t deviceCount{0};
	chk(vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr));
	std::vector<VkPhysicalDevice> devices(deviceCount);
	chk(vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data()));
	uint32_t deviceIndex{0};
	if (argc > 1) {
		deviceIndex = std::stoi(argv[1]);
		assert(deviceIndex < deviceCount);
	}
	VkPhysicalDeviceProperties2 deviceProperties{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2
	};
	vkGetPhysicalDeviceProperties2(devices[deviceIndex], &deviceProperties);
	std::cout << "Selected device: " << deviceProperties.properties.deviceName << "\n";

	// Find a queue family for graphics
	uint32_t queueFamilyCount{0};
	vkGetPhysicalDeviceQueueFamilyProperties(devices[deviceIndex], &queueFamilyCount, nullptr);
	std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(devices[deviceIndex], &queueFamilyCount, queueFamilies.data());
	uint32_t queueFamily{0};
	for (size_t i = 0; i < queueFamilies.size(); i++) {
		if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
			queueFamily = i;
			break;
		}
	}
	chk(SDL_Vulkan_GetPresentationSupport(instance, devices[deviceIndex], queueFamily));

	// Logical device
	const float qfpriorities{1.0f};
	VkDeviceQueueCreateInfo queueCI{
		.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, 
		.queueFamilyIndex = queueFamily, 
		.queueCount = 1, 
		.pQueuePriorities = &qfpriorities
	};
	VkPhysicalDeviceVulkan11Features enabledVk11Features{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
		.shaderDrawParameters = true,
	};

	VkPhysicalDeviceVulkan12Features enabledVk12Features{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
		.pNext = &enabledVk11Features,
		.descriptorIndexing = true, 
		.shaderSampledImageArrayNonUniformIndexing = true, 
		.descriptorBindingVariableDescriptorCount = true, 
		.runtimeDescriptorArray = true, 
		.bufferDeviceAddress = true
		
	};
	VkPhysicalDeviceVulkan13Features enabledVk13Features{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, 
		.pNext = &enabledVk12Features, 
		.synchronization2 = true, 
		.dynamicRendering = true
	};
	const std::vector<const char*> deviceExtensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
	const VkPhysicalDeviceFeatures enabledVk10Features{
		.samplerAnisotropy = VK_TRUE,
		.shaderInt64 = VK_TRUE,
	};
	VkDeviceCreateInfo deviceCI{
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.pNext = &enabledVk13Features,
		.queueCreateInfoCount = 1,
		.pQueueCreateInfos = &queueCI,
		.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size()),
		.ppEnabledExtensionNames = deviceExtensions.data(),
		.pEnabledFeatures = &enabledVk10Features
	};
	chk(vkCreateDevice(devices[deviceIndex], &deviceCI, nullptr, &device));
	vkGetDeviceQueue(device, queueFamily, 0, &queue);

	// VMA
	VmaVulkanFunctions vkFunctions{
		.vkGetInstanceProcAddr = vkGetInstanceProcAddr, 
		.vkGetDeviceProcAddr = vkGetDeviceProcAddr, 
		.vkCreateImage = vkCreateImage
	};
	VmaAllocatorCreateInfo allocatorCI{
		.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT, 
		.physicalDevice = devices[deviceIndex], 
		.device = device, 
		.pVulkanFunctions = &vkFunctions, 
		.instance = instance
	};
	chk(vmaCreateAllocator(&allocatorCI, &allocator));

	// Window and surface
	SDL_Window* window = SDL_CreateWindow("Quadrotor Simulator", 1280u, 720u, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
	assert(window);
	chk(SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface));
	chk(SDL_GetWindowSize(window, &windowSize.x, &windowSize.y));
	VkSurfaceCapabilitiesKHR surfaceCaps{};
	chk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(devices[deviceIndex], surface, &surfaceCaps));

	// Swap chain
	const VkFormat imageFormat{VK_FORMAT_B8G8R8A8_SRGB};
	VkSwapchainCreateInfoKHR swapchainCI{
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = surface,
		.minImageCount = surfaceCaps.minImageCount,
		.imageFormat = imageFormat,
		.imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR,
		.imageExtent{.width = surfaceCaps.currentExtent.width, .height = surfaceCaps.currentExtent.height },
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
		.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.presentMode = VK_PRESENT_MODE_FIFO_KHR,
	};
	chk(vkCreateSwapchainKHR(device, &swapchainCI, nullptr, &swapchain));
	uint32_t imageCount{0};
	chk(vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr));
	swapchainImages.resize(imageCount);
	chk(vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages.data()));
	swapchainImageViews.resize(imageCount);
	for (auto i = 0; i < imageCount; i++) {
		VkImageViewCreateInfo viewCI{
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, 
			.image = swapchainImages[i], 
			.viewType = VK_IMAGE_VIEW_TYPE_2D, 
			.format = imageFormat, 
			.subresourceRange{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 }
		};
		chk(vkCreateImageView(device, &viewCI, nullptr, &swapchainImageViews[i]));
	}

	// Get number of samples
	VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT;
	VkPhysicalDeviceProperties physicalDeviceProperties;
	vkGetPhysicalDeviceProperties(devices[deviceIndex], &physicalDeviceProperties);

	VkSampleCountFlags counts = physicalDeviceProperties.limits.framebufferColorSampleCounts & physicalDeviceProperties.limits.framebufferDepthSampleCounts;
	if (counts & VK_SAMPLE_COUNT_64_BIT) { sampleCount = VK_SAMPLE_COUNT_64_BIT; } else if (counts & VK_SAMPLE_COUNT_32_BIT) { sampleCount = VK_SAMPLE_COUNT_32_BIT; } else if (counts & VK_SAMPLE_COUNT_16_BIT) { sampleCount = VK_SAMPLE_COUNT_16_BIT; } else if (counts & VK_SAMPLE_COUNT_8_BIT) { sampleCount = VK_SAMPLE_COUNT_8_BIT; } else if (counts & VK_SAMPLE_COUNT_4_BIT) { sampleCount = VK_SAMPLE_COUNT_4_BIT; } else if (counts & VK_SAMPLE_COUNT_2_BIT) { sampleCount = VK_SAMPLE_COUNT_2_BIT; }

	std::cout << "MSAA Samples count: " << sampleCount << "\n";

	// Depth attachment
	std::vector<VkFormat> depthFormatList{VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT};
	VkFormat depthFormat{VK_FORMAT_UNDEFINED};
	for (VkFormat& format : depthFormatList) {
		VkFormatProperties2 formatProperties{
			.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2
		};
		vkGetPhysicalDeviceFormatProperties2(devices[deviceIndex], format, &formatProperties);
		if (formatProperties.formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
			depthFormat = format;
			break;
		}
	}
	assert(depthFormat != VK_FORMAT_UNDEFINED);
	VkImageCreateInfo depthImageCI{
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = depthFormat,
		.extent{.width = static_cast<uint32_t>(windowSize.x), .height = static_cast<uint32_t>(windowSize.y), .depth = 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = sampleCount,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};
	VmaAllocationCreateInfo allocCI{
		.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT, 
		.usage = VMA_MEMORY_USAGE_AUTO
	};
	chk(vmaCreateImage(allocator, &depthImageCI, &allocCI, &depthImage, &depthImageAllocation, nullptr));
	VkImageViewCreateInfo depthViewCI{
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, 
		.image = depthImage, 
		.viewType = VK_IMAGE_VIEW_TYPE_2D, 
		.format = depthFormat, 
		.subresourceRange{.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT, .levelCount = 1, .layerCount = 1 }
	};
	chk(vkCreateImageView(device, &depthViewCI, nullptr, &depthImageView));

	// Main color image
	VkImageCreateInfo colorImageCI{
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = imageFormat,
		.extent{.width = static_cast<uint32_t>(windowSize.x), .height = static_cast<uint32_t>(windowSize.y), .depth = 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = sampleCount,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};
	VmaAllocationCreateInfo colorAllocCI{
		.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
		.usage = VMA_MEMORY_USAGE_AUTO
	};
	chk(vmaCreateImage(allocator, &colorImageCI, &colorAllocCI, &colorImage, &colorImageAllocation, nullptr));
	VkImageViewCreateInfo colorViewCI{
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = colorImage,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = imageFormat,
		.subresourceRange{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 }
	};
	chk(vkCreateImageView(device, &colorViewCI, nullptr, &colorImageView));

	// Mesh data
	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	chk(tinyobj::LoadObj(&attrib, &shapes, &materials, nullptr, nullptr, "assets/suzanne.obj"));
	const VkDeviceSize indexCount{shapes[0].mesh.indices.size()};
	std::vector<Vertex> vertices{};
	std::vector<uint16_t> indices{};

	// Load vertex and index data
	for (auto& index : shapes[0].mesh.indices) {
		Vertex v{
			.pos = { attrib.vertices[index.vertex_index * 3], attrib.vertices[index.vertex_index * 3 + 1], attrib.vertices[index.vertex_index * 3 + 2] },
			.normal = { attrib.normals[index.normal_index * 3], attrib.normals[index.normal_index * 3 + 1], attrib.normals[index.normal_index * 3 + 2] },
			.uv = { attrib.texcoords[index.texcoord_index * 2], 1.0 - attrib.texcoords[index.texcoord_index * 2 + 1] }
		};
		vertices.push_back(v);
		indices.push_back(indices.size());
	}
	VkDeviceSize vBufSize{sizeof(Vertex) * vertices.size()};
	VkDeviceSize iBufSize{sizeof(uint16_t) * indices.size()};
	VkBufferCreateInfo bufferCI{
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, 
		.size = vBufSize + iBufSize, 
		.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT
	};
	VmaAllocationCreateInfo bufferAllocCI{
		.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, 
		.usage = VMA_MEMORY_USAGE_AUTO
	};
	chk(vmaCreateBuffer(allocator, &bufferCI, &bufferAllocCI, &vBuffer, &vBufferAllocation, nullptr));
	void* bufferPtr{nullptr};
	chk(vmaMapMemory(allocator, vBufferAllocation, &bufferPtr));
	memcpy(bufferPtr, vertices.data(), vBufSize);
	memcpy(((char*)bufferPtr) + vBufSize, indices.data(), iBufSize);
	vmaUnmapMemory(allocator, vBufferAllocation);

	// Ground buffer
	std::vector<Vertex> groundVertices{
		{{-150.0f, 0.0f, -150.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}}, // top-left
		{{ 150.0f, 0.0f, -150.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}}, // top-right
		{{ 150.0f, 0.0f,  150.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}}, // bottom-right
		{{-150.0f, 0.0f,  150.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}}  // bottom-left
	};
	std::vector<uint16_t> groundIndices{0, 2, 1, 0, 3, 2};

	VkDeviceSize vGroundBufSize{sizeof(Vertex) * groundVertices.size()};
	VkDeviceSize iGroundBufSize{sizeof(uint16_t) * groundIndices.size()};
	VkBufferCreateInfo GroundBufferCI{
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = vGroundBufSize + iGroundBufSize,
		.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT
	};
	VmaAllocationCreateInfo GroundBufferAllocCI{
		.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
		.usage = VMA_MEMORY_USAGE_AUTO
	};
	chk(vmaCreateBuffer(allocator, &GroundBufferCI, &GroundBufferAllocCI, &vGroundBuffer, &vGroundBufferAllocation, nullptr));
	void* groundBufferPtr{nullptr};
	chk(vmaMapMemory(allocator, vGroundBufferAllocation, &groundBufferPtr));
	memcpy(groundBufferPtr, groundVertices.data(), vGroundBufSize);
	memcpy(((char*)groundBufferPtr) + vGroundBufSize, groundIndices.data(), iGroundBufSize);
	vmaUnmapMemory(allocator, vGroundBufferAllocation);


	// Shader data buffers
	for (auto i = 0; i < maxFramesInFlight; i++) {
		VkBufferCreateInfo uBufferCI{
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, 
			.size = sizeof(ShaderData), 
			.usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
		};
		VmaAllocationCreateInfo uBufferAllocCI{
			.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, 
			.usage = VMA_MEMORY_USAGE_AUTO
		};
		chk(vmaCreateBuffer(allocator, &uBufferCI, &uBufferAllocCI, &shaderDataBuffers[i].buffer, &shaderDataBuffers[i].allocation, nullptr));
		chk(vmaMapMemory(allocator, shaderDataBuffers[i].allocation, &shaderDataBuffers[i].mapped));
		VkBufferDeviceAddressInfo uBufferBdaInfo{
			.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, 
			.buffer = shaderDataBuffers[i].buffer
		};
		shaderDataBuffers[i].deviceAddress = vkGetBufferDeviceAddress(device, &uBufferBdaInfo);
	}

	// Sync objects
	VkSemaphoreCreateInfo semaphoreCI{
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
	};
	VkFenceCreateInfo fenceCI{
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, 
		.flags = VK_FENCE_CREATE_SIGNALED_BIT
	};
	for (auto i = 0; i < maxFramesInFlight; i++) {
		chk(vkCreateFence(device, &fenceCI, nullptr, &fences[i]));
		chk(vkCreateSemaphore(device, &semaphoreCI, nullptr, &presentSemaphores[i]));
	}
	renderSemaphores.resize(swapchainImages.size());
	for (auto& semaphore : renderSemaphores) {
		chk(vkCreateSemaphore(device, &semaphoreCI, nullptr, &semaphore));
	}

	// Comm
	// and pool
	VkCommandPoolCreateInfo commandPoolCI{
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, 
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, 
		.queueFamilyIndex = queueFamily
	};
	chk(vkCreateCommandPool(device, &commandPoolCI, nullptr, &commandPool));
	VkCommandBufferAllocateInfo cbAllocCI{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, 
		.commandPool = commandPool, 
		.commandBufferCount = maxFramesInFlight
	};
	chk(vkAllocateCommandBuffers(device, &cbAllocCI, commandBuffers.data()));

	// Texture images
	std::vector<VkDescriptorImageInfo> textureDescriptors{};
	for (auto i = 0; i < textures.size(); i++) {
		ktxTexture* ktxTexture{nullptr};
		std::string filename = "assets/suzanne" + std::to_string(i) + ".ktx";
		ktxTexture_CreateFromNamedFile(filename.c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktxTexture);
		VkImageCreateInfo texImgCI{
			.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
			.imageType = VK_IMAGE_TYPE_2D,
			.format = ktxTexture_GetVkFormat(ktxTexture),
			.extent = {.width = ktxTexture->baseWidth, .height = ktxTexture->baseHeight, .depth = 1 },
			.mipLevels = ktxTexture->numLevels,
			.arrayLayers = 1,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.tiling = VK_IMAGE_TILING_OPTIMAL,
			.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
		};
		VmaAllocationCreateInfo texImageAllocCI{
			.usage = VMA_MEMORY_USAGE_AUTO
		};
		chk(vmaCreateImage(allocator, &texImgCI, &texImageAllocCI, &textures[i].image, &textures[i].allocation, nullptr));
		VkImageViewCreateInfo texVewCI{
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, 
			.image = textures[i].image, 
			.viewType = VK_IMAGE_VIEW_TYPE_2D, 
			.format = texImgCI.format, 
			.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = ktxTexture->numLevels, .layerCount = 1 }
		};
		chk(vkCreateImageView(device, &texVewCI, nullptr, &textures[i].view));

		// Upload
		VkBuffer imgSrcBuffer{};
		VmaAllocation imgSrcAllocation{};
		VkBufferCreateInfo imgSrcBufferCI{
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, 
			.size = (uint32_t)ktxTexture->dataSize, 
			.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT
		};
		VmaAllocationCreateInfo imgSrcAllocCI{
			.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, 
			.usage = VMA_MEMORY_USAGE_AUTO
		};
		chk(vmaCreateBuffer(allocator, &imgSrcBufferCI, &imgSrcAllocCI, &imgSrcBuffer, &imgSrcAllocation, nullptr));
		void* imgSrcBufferPtr{nullptr};
		chk(vmaMapMemory(allocator, imgSrcAllocation, &imgSrcBufferPtr));
		memcpy(imgSrcBufferPtr, ktxTexture->pData, ktxTexture->dataSize);
		VkFenceCreateInfo fenceOneTimeCI{
			.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO
		};
		VkFence fenceOneTime{};
		chk(vkCreateFence(device, &fenceOneTimeCI, nullptr, &fenceOneTime));
		VkCommandBuffer cbOneTime{};
		VkCommandBufferAllocateInfo cbOneTimeAI{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, 
			.commandPool = commandPool, 
			.commandBufferCount = 1
		};
		chk(vkAllocateCommandBuffers(device, &cbOneTimeAI, &cbOneTime));
		VkCommandBufferBeginInfo cbOneTimeBI{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, 
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
		};
		chk(vkBeginCommandBuffer(cbOneTime, &cbOneTimeBI));
		VkImageMemoryBarrier2 barrierTexImage{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
			.srcStageMask = VK_PIPELINE_STAGE_2_NONE,
			.srcAccessMask = VK_ACCESS_2_NONE,
			.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.image = textures[i].image,
			.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = ktxTexture->numLevels, .layerCount = 1 }
		};
		VkDependencyInfo barrierTexInfo{
			.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, 
			.imageMemoryBarrierCount = 1, 
			.pImageMemoryBarriers = &barrierTexImage
		};
		vkCmdPipelineBarrier2(cbOneTime, &barrierTexInfo);
		std::vector<VkBufferImageCopy> copyRegions{};
		for (auto j = 0; j < ktxTexture->numLevels; j++) {
			ktx_size_t mipOffset{0};
			KTX_error_code ret = ktxTexture_GetImageOffset(ktxTexture, j, 0, 0, &mipOffset);
			copyRegions.push_back({
				.bufferOffset = mipOffset,
				.imageSubresource{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = (uint32_t)j, .layerCount = 1},
				.imageExtent{.width = ktxTexture->baseWidth >> j, .height = ktxTexture->baseHeight >> j, .depth = 1 },
			});
		}
		vkCmdCopyBufferToImage(cbOneTime, imgSrcBuffer, textures[i].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<uint32_t>(copyRegions.size()), copyRegions.data());
		VkImageMemoryBarrier2 barrierTexRead{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
			.srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT,
			.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
			.image = textures[i].image,
			.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = ktxTexture->numLevels, .layerCount = 1 }
		};
		barrierTexInfo.pImageMemoryBarriers = &barrierTexRead;
		vkCmdPipelineBarrier2(cbOneTime, &barrierTexInfo);
		chk(vkEndCommandBuffer(cbOneTime));
		VkSubmitInfo oneTimeSI{
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, 
			.commandBufferCount = 1, 
			.pCommandBuffers = &cbOneTime
		};
		chk(vkQueueSubmit(queue, 1, &oneTimeSI, fenceOneTime));
		chk(vkWaitForFences(device, 1, &fenceOneTime, VK_TRUE, UINT64_MAX));
		vkDestroyFence(device, fenceOneTime, nullptr);
		vmaUnmapMemory(allocator, imgSrcAllocation);
		vmaDestroyBuffer(allocator, imgSrcBuffer, imgSrcAllocation);

		// Sampler
		VkSamplerCreateInfo samplerCI{
			.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
			.magFilter = VK_FILTER_LINEAR,
			.minFilter = VK_FILTER_LINEAR,
			.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
			.anisotropyEnable = VK_TRUE,
			.maxAnisotropy = 8.0f,
			.maxLod = (float)ktxTexture->numLevels,
		};
		chk(vkCreateSampler(device, &samplerCI, nullptr, &textures[i].sampler));
		ktxTexture_Destroy(ktxTexture);
		textureDescriptors.push_back({.sampler = textures[i].sampler, .imageView = textures[i].view, .imageLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL});
	}

	// Descriptor (indexing)
	VkDescriptorBindingFlags descVariableFlag{
		VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT
	};
	VkDescriptorSetLayoutBindingFlagsCreateInfo descBindingFlags{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO, 
		.bindingCount = 1, 
		.pBindingFlags = &descVariableFlag
	};
	VkDescriptorSetLayoutBinding descLayoutBindingTex{
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 
		.descriptorCount = static_cast<uint32_t>(textures.size()), 
		.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
	};
	VkDescriptorSetLayoutCreateInfo descLayoutTexCI{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, 
		.pNext = &descBindingFlags, 
		.bindingCount = 1, 
		.pBindings = &descLayoutBindingTex
	};
	chk(vkCreateDescriptorSetLayout(device, &descLayoutTexCI, nullptr, &descriptorSetLayoutTex));
	VkDescriptorPoolSize poolSize{
		.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.descriptorCount = static_cast<uint32_t>(textures.size())
	};
	VkDescriptorPoolCreateInfo descPoolCI{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, 
		.maxSets = 1, 
		.poolSizeCount = 1, 
		.pPoolSizes = &poolSize
	};
	chk(vkCreateDescriptorPool(device, &descPoolCI, nullptr, &descriptorPool));
	uint32_t variableDescCount{static_cast<uint32_t>(textures.size())};
	VkDescriptorSetVariableDescriptorCountAllocateInfo variableDescCountAI{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO_EXT, 
		.descriptorSetCount = 1, 
		.pDescriptorCounts = &variableDescCount
	};
	VkDescriptorSetAllocateInfo texDescSetAlloc{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, 
		.pNext = &variableDescCountAI, 
		.descriptorPool = descriptorPool, 
		.descriptorSetCount = 1, 
		.pSetLayouts = &descriptorSetLayoutTex
	};
	chk(vkAllocateDescriptorSets(device, &texDescSetAlloc, &descriptorSetTex));
	VkWriteDescriptorSet writeDescSet{
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, 
		.dstSet = descriptorSetTex, 
		.dstBinding = 0, 
		.descriptorCount = static_cast<uint32_t>(textureDescriptors.size()), 
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 
		.pImageInfo = textureDescriptors.data()
	};
	vkUpdateDescriptorSets(device, 1, &writeDescSet, 0, nullptr);

	// Initialize Slang shader compiler
	slang::createGlobalSession(slangGlobalSession.writeRef());
	auto slangTargets{std::to_array<slang::TargetDesc>({ {
			.format{SLANG_SPIRV}, 
			.profile{slangGlobalSession->findProfile("spirv_1_4")
	} } })};
	auto slangOptions{std::to_array<slang::CompilerOptionEntry>({ { 
			slang::CompilerOptionName::EmitSpirvDirectly, 
			{slang::CompilerOptionValueKind::Int, 1}
	} })};
	slang::SessionDesc slangSessionDesc{
		.targets{slangTargets.data()},
		.targetCount{SlangInt(slangTargets.size())}, 
		.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR, 
		.compilerOptionEntries{slangOptions.data()}, 
		.compilerOptionEntryCount{uint32_t(slangOptions.size())}
	};

	// Load shaders
	Slang::ComPtr<slang::ISession> opqueSlangSession;
	Slang::ComPtr<slang::ISession> skyboxSlangSession;
	slangGlobalSession->createSession(slangSessionDesc, opqueSlangSession.writeRef());
	slangGlobalSession->createSession(slangSessionDesc, skyboxSlangSession.writeRef());
	Slang::ComPtr<slang::IModule> opaqueSlangModule{
		opqueSlangSession->loadModuleFromSource("triangle", "assets/shader.slang", nullptr, nullptr)
	};
	Slang::ComPtr<slang::IModule> skyboxSlangModule{
		skyboxSlangSession->loadModuleFromSource("triangle", "assets/shader-skybox.slang", nullptr, nullptr)
	};
	Slang::ComPtr<ISlangBlob> opaqueSpirv;
	Slang::ComPtr<ISlangBlob> skyboxSpirv; 
	opaqueSlangModule->getTargetCode(0, opaqueSpirv.writeRef());
	skyboxSlangModule->getTargetCode(0, skyboxSpirv.writeRef());
	VkShaderModuleCreateInfo opaqueShaderModuleCI{
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, 
		.codeSize = opaqueSpirv->getBufferSize(),
		.pCode = (uint32_t*)opaqueSpirv->getBufferPointer()
	};
	VkShaderModuleCreateInfo skyboxShaderModuleCI{
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = skyboxSpirv->getBufferSize(),
		.pCode = (uint32_t*)skyboxSpirv->getBufferPointer()
	};
	VkShaderModule opaqueShaderModule{};
	VkShaderModule skyboxShaderModule{};
	chk(vkCreateShaderModule(device, &opaqueShaderModuleCI, nullptr, &opaqueShaderModule));
	chk(vkCreateShaderModule(device, &skyboxShaderModuleCI, nullptr, &skyboxShaderModule));

	// Opaque Pipeline
	VkPushConstantRange pushConstantRange{
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT, 
		.size = sizeof(PushConstants)
	};
	VkPipelineLayoutCreateInfo pipelineLayoutCI{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, 
		.setLayoutCount = 1, 
		.pSetLayouts = &descriptorSetLayoutTex, 
		.pushConstantRangeCount = 1, 
		.pPushConstantRanges = &pushConstantRange
	};
	chk(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &opaquePipelineLayout));
	std::vector<VkPipelineShaderStageCreateInfo> shaderStages{{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, 
			.stage = VK_SHADER_STAGE_VERTEX_BIT, 
			.module = opaqueShaderModule,
			.pName = "main"
		},{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, 
			.stage = VK_SHADER_STAGE_FRAGMENT_BIT, 
			.module = opaqueShaderModule, 
			.pName = "main" 
		}
	};
	VkVertexInputBindingDescription vertexBinding{
		.binding = 0, 
		.stride = sizeof(Vertex), 
		.inputRate = VK_VERTEX_INPUT_RATE_VERTEX
	};
	std::vector<VkVertexInputAttributeDescription> vertexAttributes{
		{.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT },
		{.location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, normal) },
		{.location = 2, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(Vertex, uv) },
	};
	VkPipelineVertexInputStateCreateInfo vertexInputState{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = &vertexBinding,
		.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributes.size()),
		.pVertexAttributeDescriptions = vertexAttributes.data(),
	};
	VkPipelineInputAssemblyStateCreateInfo inputAssemblyState{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, 
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
	};
	std::vector<VkDynamicState> dynamicStates{
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR
	};
	VkPipelineDynamicStateCreateInfo dynamicState{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, 
		.dynamicStateCount = 2, 
		.pDynamicStates = dynamicStates.data()
	};
	VkPipelineViewportStateCreateInfo viewportState{.
		sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1, 
		.scissorCount = 1
	};
	VkPipelineRasterizationStateCreateInfo rasterizationState{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_BACK_BIT,
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
		.lineWidth = 1,
	};
	VkPipelineMultisampleStateCreateInfo multisampleState{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, 
		.rasterizationSamples = sampleCount
	};
	VkPipelineDepthStencilStateCreateInfo depthStencilState{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, 
		.depthTestEnable = VK_TRUE, 
		.depthWriteEnable = VK_TRUE, 
		.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL
	};
	VkPipelineColorBlendAttachmentState blendAttachment{
		.colorWriteMask = 0xF
	};
	VkPipelineColorBlendStateCreateInfo colorBlendState{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, 
		.attachmentCount = 1, .pAttachments = &blendAttachment
	};
	VkPipelineRenderingCreateInfo renderingCI{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, 
		.colorAttachmentCount = 1, 
		.pColorAttachmentFormats = &imageFormat, 
		.depthAttachmentFormat = depthFormat
	};
	VkGraphicsPipelineCreateInfo pipelineCI{
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.pNext = &renderingCI,
		.stageCount = 2,
		.pStages = shaderStages.data(),
		.pVertexInputState = &vertexInputState,
		.pInputAssemblyState = &inputAssemblyState,
		.pViewportState = &viewportState,
		.pRasterizationState = &rasterizationState,
		.pMultisampleState = &multisampleState,
		.pDepthStencilState = &depthStencilState,
		.pColorBlendState = &colorBlendState,
		.pDynamicState = &dynamicState,
		.layout = opaquePipelineLayout,
	};
	chk(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &opaquePipeline));

	// Skybox Pipeline
	VkPushConstantRange skyboxPushConstantRange{
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
		.size = sizeof(PushConstants)
	};
	VkPipelineLayoutCreateInfo skyboxPipelineLayoutCI{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 0,
		.pSetLayouts = nullptr,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &skyboxPushConstantRange
	};
	chk(vkCreatePipelineLayout(device, &skyboxPipelineLayoutCI, nullptr, &skyboxPipelineLayout));
	std::vector<VkPipelineShaderStageCreateInfo> skyboxShaderStages{{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_VERTEX_BIT,
			.module = skyboxShaderModule,
			.pName = "main"
		},{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
			.module = skyboxShaderModule,
			.pName = "main"
		}
	};
	VkPipelineVertexInputStateCreateInfo skyboxVertexInputState{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 0,
		.pVertexBindingDescriptions = nullptr,
		.vertexAttributeDescriptionCount = 0,
		.pVertexAttributeDescriptions = nullptr,
	};
	VkPipelineInputAssemblyStateCreateInfo skyboxInputAssemblyState{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
	};
	std::vector<VkDynamicState> skyboxDynamicStates{
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR
	};
	VkPipelineDynamicStateCreateInfo skyboxDynamicState{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = 2,
		.pDynamicStates = dynamicStates.data()
	};
	VkPipelineViewportStateCreateInfo skyboxViewportState{.
		sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.scissorCount = 1
	};
	VkPipelineRasterizationStateCreateInfo skyboxRasterizationState{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.cullMode = VK_CULL_MODE_FRONT_BIT,
		.lineWidth = 1,
	};
	VkPipelineMultisampleStateCreateInfo skyboxMultisampleState{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = sampleCount
	};
	VkPipelineDepthStencilStateCreateInfo skyboxDepthStencilState{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = VK_TRUE,
		.depthWriteEnable = VK_FALSE,
		.depthCompareOp = VK_COMPARE_OP_EQUAL,
	};
	VkPipelineColorBlendAttachmentState skyboxBlendAttachment{
		.colorWriteMask = 0xF
	};
	VkPipelineColorBlendStateCreateInfo skyboxColorBlendState{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1, .pAttachments = &blendAttachment
	};
	VkPipelineRenderingCreateInfo skyboxRenderingCI{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
		.colorAttachmentCount = 1,
		.pColorAttachmentFormats = &imageFormat,
		.depthAttachmentFormat = depthFormat
	};
	VkGraphicsPipelineCreateInfo skyboxPipelineCI{
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.pNext = &skyboxRenderingCI,
		.stageCount = 2,
		.pStages = skyboxShaderStages.data(),
		.pVertexInputState = &skyboxVertexInputState,
		.pInputAssemblyState = &skyboxInputAssemblyState,
		.pViewportState = &skyboxViewportState,
		.pRasterizationState = &skyboxRasterizationState,
		.pMultisampleState = &skyboxMultisampleState,
		.pDepthStencilState = &skyboxDepthStencilState,
		.pColorBlendState = &skyboxColorBlendState,
		.pDynamicState = &skyboxDynamicState,
		.layout = skyboxPipelineLayout,
	};
	chk(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &skyboxPipelineCI, nullptr, &skyboxPipeline));

	// Populate model matrices
	shaderData.model[0] = glm::translate(glm::mat4(1.0f), glm::vec3(-3.0f, 0.0f, 0.0f));
	shaderData.model[1] = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f));
	shaderData.model[2] = glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, 0.0f, 0.0f));
	for (auto i = 0; i < maxFramesInFlight; i++) {
		memcpy(shaderDataBuffers[frameIndex].mapped, &shaderData, sizeof(ShaderData));
	}

	// Create lights
	shaderData.lightCount = 3;
	shaderData.lights[0] = {  // Sun
		.position = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f),
		.direction = glm::vec4(1.0f, -0.5f, -1.0f, 0.0f),
		.color = glm::vec4(1.64f, 1.27f, 0.99f, 1.0f),
		.settings = glm::uvec4(0, 1, 0, 0),
	};
	shaderData.lights[1] = {  // Sky
		.position = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f),
		.direction = glm::vec4(0.0f, -1.0f, 0.0f, 0.0f),
		.color = glm::vec4(0.16f, 0.2f, 0.28f, 1.0f),
		.settings = glm::uvec4(0, 1, 0, 0),
	};
	shaderData.lights[2] = {  // Sun bounce
		.position = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f),
		.direction = shaderData.lights[0].direction * glm::vec4(-1.0f, 0.0f, -1.0f, 0.0f),
		.color = glm::vec4(0.4f, 0.28f, 0.2f, 1.0f),
		.settings = glm::uvec4(0, 1, 0, 0),
	};
	

	// Render loop
	uint64_t lastTime{SDL_GetTicks()};
	bool quit{false};
	while (!quit) {
		// Sync
		chk(vkWaitForFences(device, 1, &fences[frameIndex], true, UINT64_MAX));
		chk(vkResetFences(device, 1, &fences[frameIndex]));
		chkSwapchain(vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, presentSemaphores[frameIndex], VK_NULL_HANDLE, &imageIndex));

		// Update camera position
		camPos = model.getState()[0] + glm::vec3(1.0f, 1.0f, 4.0f);

		// Update shader data
		shaderData.model[1] = glm::translate(glm::mat4(1.0f), model.getState()[0]);
		shaderData.view = glm::lookAt(camPos, model.getState()[0], glm::vec3(0.0f, 1.0f, 0.0f));
		shaderData.projection = glm::perspective(glm::radians(70.0f), (float)windowSize.x / (float)windowSize.y, 0.1f, 1024.0f);
		shaderData.projection[1][1] *= -1;
		
		shaderData.invV = glm::inverse(shaderData.view);
		shaderData.invP = glm::inverse(shaderData.projection);
		
		shaderData.camPos = glm::vec4(camPos, 1.0f);
		
		memcpy(shaderDataBuffers[frameIndex].mapped, &shaderData, sizeof(ShaderData));

		// Build command buffer
		auto cb = commandBuffers[frameIndex];
		chk(vkResetCommandBuffer(cb, 0));
		VkCommandBufferBeginInfo cbBI{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, 
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
		};
		chk(vkBeginCommandBuffer(cb, &cbBI));

		// Change image layouts
		std::array<VkImageMemoryBarrier2, 3> outputBarriers{
			VkImageMemoryBarrier2{  // color attachment for rendering
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
				.srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
				.srcAccessMask = 0,
				.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
				.image = colorImage,
				.subresourceRange{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 }
			},
			VkImageMemoryBarrier2{  // swapchain image for transfer destination
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
				.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				.srcAccessMask = 0,
				.dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT,
				.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				.image = swapchainImages[imageIndex],
				.subresourceRange{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 }
			},
			VkImageMemoryBarrier2{  // depth attachment for rendering
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
				.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
				.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
				.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
				.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
				.image = depthImage,
				.subresourceRange{.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, .levelCount = 1, .layerCount = 1 }
			}
		};
		VkDependencyInfo barrierDependencyInfo{
			.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, 
			.imageMemoryBarrierCount = 3, 
			.pImageMemoryBarriers = outputBarriers.data()
		};
		vkCmdPipelineBarrier2(cb, &barrierDependencyInfo);

		VkRenderingAttachmentInfo colorAttachmentInfo{
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView = colorImageView,
			.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.clearValue{.color{ 0.0f, 0.0f, 0.0f, 1.0f }}
		};
		VkRenderingAttachmentInfo depthAttachmentInfo{
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView = depthImageView,
			.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.clearValue = {.depthStencil = {1.0f,  0}}
		};
		VkRenderingInfo renderingInfo{
			.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
			.renderArea{.extent{.width = static_cast<uint32_t>(windowSize.x), .height = static_cast<uint32_t>(windowSize.y) }},
			.layerCount = 1,
			.colorAttachmentCount = 1,
			.pColorAttachments = &colorAttachmentInfo,
			.pDepthAttachment = &depthAttachmentInfo
		};
		vkCmdBeginRendering(cb, &renderingInfo);
		VkViewport vp{
			.width = static_cast<float>(windowSize.x), 
			.height = static_cast<float>(windowSize.y), 
			.minDepth = 0.0f, 
			.maxDepth = 1.0f
		};
		vkCmdSetViewport(cb, 0, 1, &vp);
		VkRect2D scissor{
			.extent{.width = static_cast<uint32_t>(windowSize.x), .height = static_cast<uint32_t>(windowSize.y) 
		}};
		vkCmdSetScissor(cb, 0, 1, &scissor);

		// Push Constants
		PushConstants pc{
			.shaderDataAddress = shaderDataBuffers[frameIndex].deviceAddress,
			.modelMatrixIndex = 0,
			.textureIndex = 0,
		};
		vkCmdPushConstants(cb, opaquePipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &pc);

		// Opaque
		vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, opaquePipeline);
		vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, opaquePipelineLayout, 0, 1, &descriptorSetTex, 0, nullptr);
		VkDeviceSize vOffset{0};
		vkCmdBindVertexBuffers(cb, 0, 1, &vBuffer, &vOffset);
		vkCmdBindIndexBuffer(cb, vBuffer, vBufSize, VK_INDEX_TYPE_UINT16);
		vkCmdDrawIndexed(cb, indexCount, 3, 0, 0, 0);

		// Ground
		vkCmdBindVertexBuffers(cb, 0, 1, &vGroundBuffer, &vOffset);
		vkCmdBindIndexBuffer(cb, vGroundBuffer, vGroundBufSize, VK_INDEX_TYPE_UINT16);
		vkCmdDrawIndexed(cb, groundIndices.size(), 1, 0, 0, 3);

		// Skybox
		vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, skyboxPipeline);
		vkCmdDraw(cb, 3, 1, 0, 0);

		vkCmdEndRendering(cb);

		// Resolve
		VkImageMemoryBarrier2 barrierResolve{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
			.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT,
			.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			.image = colorImage,
			.subresourceRange{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 }
		};
		VkDependencyInfo barrierResolveDependencyInfo{
			.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, 
			.imageMemoryBarrierCount = 1, 
			.pImageMemoryBarriers = &barrierResolve
		};
		vkCmdPipelineBarrier2(cb, &barrierResolveDependencyInfo);

		VkImageResolve resolveRegion{
			.srcSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1},
			.dstSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1},
			.extent = {
				.width = static_cast<uint32_t>(windowSize.x),   
				.height = static_cast<uint32_t>(windowSize.y),
				.depth = 1
			}
		};
		vkCmdResolveImage(
			cb, 
			colorImage, 
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			swapchainImages[imageIndex], 
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, 
			&resolveRegion
		);

		VkImageMemoryBarrier2 barrierPresent{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
			.srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT,
			.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.dstAccessMask = 0,
			.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			.image = swapchainImages[imageIndex],
			.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1}
		};
		VkDependencyInfo barrierPresentDependencyInfo{
			.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
			.imageMemoryBarrierCount = 1,
			.pImageMemoryBarriers = &barrierPresent
		};
		vkCmdPipelineBarrier2(cb, &barrierPresentDependencyInfo);

		chk(vkEndCommandBuffer(cb));

		// Submit to graphics queue
		VkPipelineStageFlags waitStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		VkSubmitInfo submitInfo{
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &presentSemaphores[frameIndex],
			.pWaitDstStageMask = &waitStages,
			.commandBufferCount = 1,
			.pCommandBuffers = &cb,
			.signalSemaphoreCount = 1,
			.pSignalSemaphores = &renderSemaphores[imageIndex],
		};
		chk(vkQueueSubmit(queue, 1, &submitInfo, fences[frameIndex]));
		frameIndex = (frameIndex + 1) % maxFramesInFlight;
		VkPresentInfoKHR presentInfo{
			.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &renderSemaphores[imageIndex],
			.swapchainCount = 1,
			.pSwapchains = &swapchain,
			.pImageIndices = &imageIndex
		};
		chkSwapchain(vkQueuePresentKHR(queue, &presentInfo));

		// Event polling
		float elapsedTime{(SDL_GetTicks() - lastTime) / 1000.0f};
		shaderData.time += elapsedTime;
		lastTime = SDL_GetTicks();
		for (SDL_Event event; SDL_PollEvent(&event);) {
			if (event.type == SDL_EVENT_QUIT) {
				quit = true;
				break;
			}
			//if (event.type == SDL_EVENT_MOUSE_WHEEL) {
			//	camPos.z += (float)event.wheel.y * elapsedTime * 100.0f;
			//}
			if (event.type == SDL_EVENT_KEY_DOWN) {
				if (event.key.key == SDLK_SPACE) {
					modelInput.thrust = 1;
				}
			}
			if (event.type == SDL_EVENT_KEY_UP) {
				if (event.key.key == SDLK_SPACE) {
					modelInput.thrust = 0;
				}
			}
			// Fullscreen
			if (event.type == SDL_EVENT_KEY_DOWN) {
				if (event.key.key == SDLK_F11) {
					isFullscreen = !isFullscreen;
					SDL_SetWindowFullscreen(window, isFullscreen);
				}
			}
			// Window resize
			if (event.type == SDL_EVENT_WINDOW_RESIZED) {
				updateSwapchain = true;
			}
		}

		// Model integration
		model.setInput(modelInput);
		integrator.takeStep(model, elapsedTime);

		// Swapchain update (if window resized)
		if (updateSwapchain) {
			chk(SDL_GetWindowSize(window, &windowSize.x, &windowSize.y));
			updateSwapchain = false;
			chk(vkDeviceWaitIdle(device));
			chk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(devices[deviceIndex], surface, &surfaceCaps));
			swapchainCI.oldSwapchain = swapchain;
			swapchainCI.imageExtent = {.width = static_cast<uint32_t>(windowSize.x), .height = static_cast<uint32_t>(windowSize.y)};
			chk(vkCreateSwapchainKHR(device, &swapchainCI, nullptr, &swapchain));
			for (auto i = 0; i < imageCount; i++) {
				vkDestroyImageView(device, swapchainImageViews[i], nullptr);
			}
			chk(vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr));
			swapchainImages.resize(imageCount);
			chk(vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages.data()));
			swapchainImageViews.resize(imageCount);
			for (auto i = 0; i < imageCount; i++) {
				VkImageViewCreateInfo viewCI{
					.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, 
					.image = swapchainImages[i], 
					.viewType = VK_IMAGE_VIEW_TYPE_2D, 
					.format = imageFormat, 
					.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1}
				};
				chk(vkCreateImageView(device, &viewCI, nullptr, &swapchainImageViews[i]));
			}
			vkDestroySwapchainKHR(device, swapchainCI.oldSwapchain, nullptr);

			vmaDestroyImage(allocator, depthImage, depthImageAllocation);
			vkDestroyImageView(device, depthImageView, nullptr);
			depthImageCI.extent = {
				.width = static_cast<uint32_t>(windowSize.x), 
				.height = static_cast<uint32_t>(windowSize.y), 
				.depth = 1
			};
			VmaAllocationCreateInfo allocCI{
				.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT, 
				.usage = VMA_MEMORY_USAGE_AUTO
			};
			chk(vmaCreateImage(allocator, &depthImageCI, &allocCI, &depthImage, &depthImageAllocation, nullptr));
			VkImageViewCreateInfo viewCI{
				.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, 
				.image = depthImage, 
				.viewType = VK_IMAGE_VIEW_TYPE_2D, 
				.format = depthFormat, 
				.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT, .levelCount = 1, .layerCount = 1 }
			};
			chk(vkCreateImageView(device, &viewCI, nullptr, &depthImageView));

			vmaDestroyImage(allocator, colorImage, colorImageAllocation);
			vkDestroyImageView(device, colorImageView, nullptr);
			colorImageCI.extent = {
				.width = static_cast<uint32_t>(windowSize.x),
				.height = static_cast<uint32_t>(windowSize.y),
				.depth = 1
			};
			chk(vmaCreateImage(allocator, &colorImageCI, &allocCI, &colorImage, &colorImageAllocation, nullptr));
			VkImageViewCreateInfo colorViewCI{
				.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
				.image = colorImage,
				.viewType = VK_IMAGE_VIEW_TYPE_2D,
				.format = imageFormat,
				.subresourceRange{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 }
			};
			chk(vkCreateImageView(device, &colorViewCI, nullptr, &colorImageView));
		}
	}

	// Tear down
	chk(vkDeviceWaitIdle(device));
	for (auto i = 0; i < maxFramesInFlight; i++) {
		vkDestroyFence(device, fences[i], nullptr);
		vkDestroySemaphore(device, presentSemaphores[i], nullptr);
		vmaUnmapMemory(allocator, shaderDataBuffers[i].allocation);
		vmaDestroyBuffer(allocator, shaderDataBuffers[i].buffer, shaderDataBuffers[i].allocation);
	}
	for (auto i = 0; i < renderSemaphores.size(); i++) {
		vkDestroySemaphore(device, renderSemaphores[i], nullptr);
	}
	vmaDestroyImage(allocator, depthImage, depthImageAllocation);
	vkDestroyImageView(device, depthImageView, nullptr);
	vmaDestroyImage(allocator, colorImage, colorImageAllocation);
	vkDestroyImageView(device, colorImageView, nullptr);
	for (auto i = 0; i < swapchainImageViews.size(); i++) {
		vkDestroyImageView(device, swapchainImageViews[i], nullptr);
	}
	vmaDestroyBuffer(allocator, vBuffer, vBufferAllocation);
	for (auto i = 0; i < textures.size(); i++) {
		vkDestroyImageView(device, textures[i].view, nullptr);
		vkDestroySampler(device, textures[i].sampler, nullptr);
		vmaDestroyImage(allocator, textures[i].image, textures[i].allocation);
	}
	vmaDestroyBuffer(allocator, vGroundBuffer, vGroundBufferAllocation);
	vkDestroyDescriptorSetLayout(device, descriptorSetLayoutTex, nullptr);
	vkDestroyDescriptorPool(device, descriptorPool, nullptr);
	vkDestroyPipelineLayout(device, opaquePipelineLayout, nullptr);
	vkDestroyPipelineLayout(device, skyboxPipelineLayout, nullptr);
	vkDestroyPipeline(device, opaquePipeline, nullptr);
	vkDestroyPipeline(device, skyboxPipeline, nullptr);
	vkDestroySwapchainKHR(device, swapchain, nullptr);
	vkDestroySurfaceKHR(instance, surface, nullptr);
	vkDestroyCommandPool(device, commandPool, nullptr);
	vkDestroyShaderModule(device, opaqueShaderModule, nullptr);
	vkDestroyShaderModule(device, skyboxShaderModule, nullptr);
	vmaDestroyAllocator(allocator);
	SDL_DestroyWindow(window);
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
	SDL_Quit();
	vkDestroyDevice(device, nullptr);
	vkDestroyInstance(instance, nullptr);
}