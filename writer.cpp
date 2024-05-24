#include <filesystem>
#include <sys/socket.h>
#include <sys/un.h>

// ----------------------------------------------------------------------------
// VARIABLES
// ----------------------------------------------------------------------------
const char *appName = "VRAM sharing test (writer)";

// ----------------------------------------------------------------------------
// COMMON LOGIC
// ----------------------------------------------------------------------------
#include "common.cpp"

// ----------------------------------------------------------------------------
// SPECIFIC WRITER INITIALIZATION
// ----------------------------------------------------------------------------
void createSharedMemoryObjectsAndFDs() {
    std::cout << "Creating shared memory objects" << std::endl;

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

    VkMemoryAllocateInfo allocInfo = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = memRequirements.size,
        .memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
    };

    if (vkAllocateMemory(device, &allocInfo, nullptr, &sharedMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate shared memory!");
    }

    if (vkBindBufferMemory(device, sharedBuffer, sharedMemory, 0) != VK_SUCCESS) {
        throw std::runtime_error("Failed to bind buffer memory!");
    }

    std::cout << "Exporting shared memory FD" << std::endl;

    VkMemoryGetFdInfoKHR getFDInfo = {
        .sType      = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,
        .memory     = sharedMemory,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT,
    };

    if (reinterpret_cast<PFN_vkGetMemoryFdKHR>(vkGetDeviceProcAddr(device, "vkGetMemoryFdKHR"))(device, &getFDInfo, &sharedBufferFD) != VK_SUCCESS) {
        throw std::runtime_error("Failed to get memory FD!");
    }

    std::cout << "Shared buffer FD: " << sharedBufferFD << std::endl;
}

// ----------------------------------------------------------------------------
// SEND DATA THROUGH THE GPU
// ----------------------------------------------------------------------------
void writeToSharedMemory() {
    std::cout << "Moving string data to GPU:" << std::endl;
    std::ios oldState(nullptr);
    oldState.copyfmt(std::cout);

    void *data;

    if (vkMapMemory(device, sharedMemory, 0, VK_WHOLE_SIZE, 0, &data) != VK_SUCCESS) {
        throw std::runtime_error("Unable to map memory!");
    }

    //for (size_t i = 0; i < sharedData.length() + 1; i++) {
    //    std::cout << "0x" << std::hex << (int)(sharedData.data()[i]) << " ";
    //    static_cast<char *>(data)[i] = sharedData.data()[i];
    //}
    for (size_t i = 0; i < 5; i++) {
        ((char *)(data))[i] = 0x1;
    }

    std::cout.copyfmt(oldState);
    std::cout << std::endl;

    vkUnmapMemory(device, sharedMemory);

    VkMappedMemoryRange memoryRange = {
        .sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
        .memory = sharedMemory,
        .offset = 0,
        .size   = VK_WHOLE_SIZE,
    };

    vkFlushMappedMemoryRanges(device, 1, &memoryRange);
}

// ----------------------------------------------------------------------------
// EXPORT FILE DESCRIPTOR SO CLIENT CAN READ IT
// ----------------------------------------------------------------------------
void shareFD(int sock){
    // This function does the arcane magic for sending
    // file descriptors over unix domain sockets
    msghdr   msg;
    iovec    iov[1];
    cmsghdr *cmsg = NULL;
    char     ctrl_buf[CMSG_SPACE(sizeof(int))];
    char     data[2] = "F";

    memset(&msg, 0, sizeof(msghdr));
    memset(ctrl_buf, 0, CMSG_SPACE(sizeof(int)));

    data[0]         = ' ';
    iov[0].iov_base = data;
    iov[0].iov_len  = sizeof(data);

    msg.msg_name       = NULL;
    msg.msg_namelen    = 0;
    msg.msg_iov        = iov;
    msg.msg_iovlen     = 1;
    msg.msg_controllen = CMSG_SPACE(sizeof(int));
    msg.msg_control    = ctrl_buf;

    cmsg             = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type  = SCM_RIGHTS;
    cmsg->cmsg_len   = CMSG_LEN(sizeof(int));

    *((int *)CMSG_DATA(cmsg)) = sharedBufferFD;

    if (sendmsg(sock, &msg, 0) < 0) {
        perror("sendmsg");
        throw std::runtime_error("Failed to send FD through socket!");
    }
}

void sendFD() {
    sockaddr_un addr;
    int sock;
    int conn;

    // Create a unix domain socket
    sock = socket(AF_UNIX, SOCK_STREAM, 0);

    // Bind it to a abstract address
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (bind(sock, (sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(sock);
        throw std::runtime_error("Failed to bind socket!");
    }

    // Listen
    if (listen(sock, 1) < 0) {
        perror("listen");
        close(sock);
        throw std::runtime_error("Failed to listen to socket!");
    }

    // Just send the file descriptor to anyone who connects
    conn = accept(sock, NULL, 0);

    if (conn < 0) {
        perror("conn");
        close(sock);
        throw std::runtime_error("Failed to connect to socket!");
    }

    shareFD(conn);
    close(conn);
}

// ----------------------------------------------------------------------------
// READ BACK THE GPU DATA
// ----------------------------------------------------------------------------
void readBackMemory() {
    std::cout << "Reading back the data from the GPU:" << std::endl;
    std::ios oldState(nullptr);
    oldState.copyfmt(std::cout);

    void *data;

    if (vkMapMemory(device, sharedMemory, 0, VK_WHOLE_SIZE, 0, &data) != VK_SUCCESS) {
        throw std::runtime_error("Unable to map memory!");
    }


    for (size_t i = 0; i < SHARED_BUFFER_SIZE; i++) {
        char c = static_cast<char *>(data)[i];

        std::cout << (int)(c) << " ";
    }

    std::cout.copyfmt(oldState);
    std::cout << std::endl;

    vkUnmapMemory(device, sharedMemory);
}

// ----------------------------------------------------------------------------
// ENTRY POINT
// ----------------------------------------------------------------------------
int main() {
    std::cout << "Launching Vulkan writer test app" << std::endl;

    // Remove lingering socket file.
    std::filesystem::remove(SOCKET_PATH);

    initVulkan();

    std::cout << std::endl;
    std::cout << "Sending FD through socket. You can run reader app now..." << std::endl;
    sendFD();

    std::cout << std::endl;
    //std::cout << "Type a string that will get loaded into the GPU (max length of " << SHARED_BUFFER_SIZE << "):" << std::endl;
    //std::getline(std::cin, sharedData);
    std::cout << "Will push string of bytes to the GPU." << std::endl;
    std::cout << "Press ENTER to continue..." << std::endl;
    std::getchar();

    writeToSharedMemory();

    std::cout << "Reader app should have received data." << std::endl;
    std::cout << "Press ENTER to exit..." << std::endl;
    std::getchar();

    // Test memory readback in this process to make sure writing worked in
    // the first place.
    readBackMemory();

    // Clean IPC resources
    close(connFD);
    close(socketFD);
    unlink(SOCKET_PATH);
    std::filesystem::remove(SOCKET_PATH);

    cleanup();

    return 0;
}
