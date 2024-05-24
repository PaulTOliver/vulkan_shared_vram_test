#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <iostream>
#include <fcntl.h>

// ----------------------------------------------------------------------------
// VARIABLES
// ----------------------------------------------------------------------------
const char *appName = "VRAM sharing test (reader)";
const char *fdPath;

// ----------------------------------------------------------------------------
// COMMON LOGIC
// ----------------------------------------------------------------------------
#include "common.cpp"

// ----------------------------------------------------------------------------
// LOADING SHARED MEMORY FD
// ----------------------------------------------------------------------------
int receiveFD(int sock) {
    // This function does the arcane magic recving
    // file descriptors over unix domain sockets
    msghdr   msg;
    iovec    iov[1];
    cmsghdr *cmsg = NULL;
    char     ctrl_buf[CMSG_SPACE(sizeof(int))];
    char     data[1];

    memset(&msg, 0, sizeof(msghdr));
    memset(ctrl_buf, 0, CMSG_SPACE(sizeof(int)));

    iov[0].iov_base = data;
    iov[0].iov_len  = sizeof(data);

    msg.msg_name       = NULL;
    msg.msg_namelen    = 0;
    msg.msg_control    = ctrl_buf;
    msg.msg_controllen = CMSG_SPACE(sizeof(int));
    msg.msg_iov        = iov;
    msg.msg_iovlen     = 1;

    recvmsg(sock, &msg, 0);

    cmsg = CMSG_FIRSTHDR(&msg);

    return *((int *)CMSG_DATA(cmsg));
}

void loadFD() {
    sockaddr_un addr;
    int         sock;

    // Create and connect a unix domain socket
    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (connect(sock, (sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(sock);
        throw std::runtime_error("Failed to connect to socket!");
    }

    // Recvive a file descriptor
    sharedBufferFD = receiveFD(sock);

    std::cout << "Shared buffer FD: " << sharedBufferFD << std::endl;
    std::cout << "My PID: " << getpid() << std::endl;
}

// ----------------------------------------------------------------------------
// SPECIFIC WRITER INITIALIZATION
// ----------------------------------------------------------------------------
void createSharedMemoryObjectsAndFDs() {
    std::cout << "Shared buffer FD: " << sharedBufferFD << std::endl;

    VkExternalMemoryBufferCreateInfo externalBufferCreateInfo = {
        .sType       = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,
        .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT,
    };

    VkBufferCreateInfo bufferCreateInfo = {
        .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext       = &externalBufferCreateInfo,
        .size        = SHARED_BUFFER_SIZE,
        .usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    if (vkCreateBuffer(device, &bufferCreateInfo, nullptr, &sharedBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shared buffer!");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, sharedBuffer, &memRequirements);
    std::cout << "Memory requirements size: " << memRequirements.size << std::endl;
    std::cout << "Memory type bits: " << memRequirements.memoryTypeBits << std::endl;

    VkImportMemoryFdInfoKHR importMemoryFdInfo = {
        .sType      = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT,
        .fd         = sharedBufferFD,
    };

    VkMemoryAllocateInfo allocInfo = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext           = &importMemoryFdInfo,
        .allocationSize  = memRequirements.size,
        .memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
    };

    VkResult allocResult = vkAllocateMemory(device, &allocInfo, nullptr, &sharedMemory);

    if (allocResult != VK_SUCCESS) {
        std::cout << "Failed to allocate memory! Error code: " << allocResult << std::endl;

        switch (allocResult) {
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:
            std::cout << "Error: Out of device memory." << std::endl;
            break;
        case VK_ERROR_OUT_OF_HOST_MEMORY:
            std::cout << "Error: Out of host memory." << std::endl;
            break;
        case VK_ERROR_INVALID_EXTERNAL_HANDLE:
            std::cout << "Error: Invalid external handle." << std::endl;
            break;
        default:
            std::cout << "Error: Unknown error." << std::endl;
            break;
        }

        throw std::runtime_error("Failed to allocate memory with external import!");
    }

    if (vkBindBufferMemory(device, sharedBuffer, sharedMemory, 0) != VK_SUCCESS) {
        throw std::runtime_error("Failed to bind buffer memory!");
    }
}

// ----------------------------------------------------------------------------
// READ DATA THROUGH THE GPU
// ----------------------------------------------------------------------------
void readFromSharedMemory() {
    std::cout << "Reading string data from GPU:" << std::endl;

    std::ios oldState(nullptr);
    oldState.copyfmt(std::cout);

    void *data;

    if (vkMapMemory(device, sharedMemory, 0, VK_WHOLE_SIZE, 0, &data) != VK_SUCCESS) {
        throw std::runtime_error("Unable to map memory!");
    }

    VkMappedMemoryRange memoryRange = {
        .sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
        .memory = sharedMemory,
        .offset = 0,
        .size   = VK_WHOLE_SIZE,
    };

    vkInvalidateMappedMemoryRanges(device, 1, &memoryRange);

    //for (size_t i = 0; i < SHARED_BUFFER_SIZE; i++) {
    //    char c = static_cast<char *>(data)[i];

    //    std::cout << "0x" << std::hex << (int)(c) << " ";

    //    //if (c == 0) {
    //    //    break;
    //    //}
    //}
    for (size_t i = 0; i < SHARED_BUFFER_SIZE; i++) {
        char c = ((char *)(data))[i];

        std::cout << (int)(c) << " ";

        //if (c == 0) {
        //    break;
        //}
    }

    std::cout.copyfmt(oldState);
    std::cout << std::endl;

    vkUnmapMemory(device, sharedMemory);
}

// ----------------------------------------------------------------------------
// ENTRY POINT
// ----------------------------------------------------------------------------
int main() {
    std::cout << "Launching Vulkan reader test app" << std::endl;

    loadFD();
    initVulkan();

    std::cout << std::endl;
    std::cout << "Ready to receive data." << std::endl;
    std::cout << "Push string from wtiter app then come back here..." << std::endl;
    std::cout << "Press ENTER to continue..." << std::endl;
    std::getchar();

    readFromSharedMemory();

    close(socketFD);
    cleanup();

    return 0;
}
