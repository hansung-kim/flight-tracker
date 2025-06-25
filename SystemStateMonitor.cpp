#include "SystemStateMonitor.h"
#include "rtl-sdr.h"
#include "Common.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <iostream>
#include <thread>
#include <libusb-1.0/libusb.h>
#include <string>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <mutex>

class SystemStateMonitor {
public:
    SystemStateMonitor(Modes_t *modes);
    ~SystemStateMonitor();

    void StartMonitoring();
    void StopMonitoring();

private:
    void MonitorLoop();

    Modes_t *mModes;
    std::string mClientIp;

    std::thread mMonitorThread;
    bool mIsRunning;
    int mUdpSockFd;
    uint16_t mClientPort;
    sockaddr_in mClientAddr;
    bool InitUdp();
    void SendHeartbeat();

    bool IsRtlSdrConnected(const char* device_name = nullptr);
    libusb_context* mUsbContext;
    bool mLastStatus;
    std::chrono::steady_clock::time_point mLastChecked;
};

SystemStateMonitor::SystemStateMonitor(Modes_t *modes) {
    mModes = modes;
    mIsRunning = false;
    mUsbContext = nullptr;
    mLastStatus = false;

    if (libusb_init(&mUsbContext) != 0) {
        std::cerr << "Failed to initialize libusb" << std::endl;
        mUsbContext = nullptr;
    }
}

SystemStateMonitor::~SystemStateMonitor() {
    StopMonitoring();
    if (mUsbContext) libusb_exit(mUsbContext);
}

void SystemStateMonitor::StartMonitoring() {
    if (!mIsRunning) {
        mIsRunning = true;
        mMonitorThread = std::thread(&SystemStateMonitor::MonitorLoop, this);
    }
}

bool SystemStateMonitor::InitUdp() {
    if (mUdpSockFd > 0) {
        return true;
    }

    if(strlen(mModes->client_ip) <= 0) {
        std::cerr << "client IP address was not updated" << mClientIp << std::endl;
        return false;
    }

    mClientIp = mModes->client_ip;
    mClientPort = 55555;

    memset(&mClientAddr, 0, sizeof(mClientAddr));
    mClientAddr.sin_family = AF_INET;
    mClientAddr.sin_port = htons(mClientPort);
    if (inet_pton(AF_INET, mClientIp.c_str(), &mClientAddr.sin_addr) <= 0) {
        std::cerr << "Invalid client IP address: " << mClientIp << std::endl;
        return false;
    }

    mUdpSockFd = socket(AF_INET, SOCK_DGRAM, 0);
    if (mUdpSockFd < 0) {
        std::cerr << "Failed to create UDP socket" << std::endl;
        return false;
    }

    return true;
}

void SystemStateMonitor::SendHeartbeat() {
    if (!InitUdp()) {
        std::cerr << "Failed to initialize UDP socket for heartbeat" << std::endl;
        return;
    }

    if (mUdpSockFd < 0) {
        std::cerr << "UDP socket is not initialized" << std::endl;
        return;
    }

    std::string heartbeatMessage = "Heartbeat from RTL-SDR Monitor";
    ssize_t sentBytes = sendto(mUdpSockFd, heartbeatMessage.c_str(), heartbeatMessage.size(), 0,
                               reinterpret_cast<struct sockaddr*>(&mClientAddr), sizeof(mClientAddr));
    if (sentBytes < 0) {
        std::cerr << "Failed to send heartbeat message: " << strerror(errno) << std::endl;
    } else {
        std::cout << "Heartbeat sent successfully" << std::endl;
    }

}

void SystemStateMonitor::StopMonitoring() {
    if (mIsRunning) {
        mIsRunning = false;
        if (mMonitorThread.joinable())
            mMonitorThread.join();
    }
}

bool SystemStateMonitor::IsRtlSdrConnected(const char* device_name) {
    // Minimal interval between checks
    auto now = std::chrono::steady_clock::now();
    if (now - mLastChecked < std::chrono::milliseconds(500)) {
        return mLastStatus;
    }
    mLastChecked = now;

    libusb_device** devs = nullptr;
    ssize_t cnt = libusb_get_device_list(mUsbContext, &devs);
    if (cnt < 0) return false;

    bool connected = false;
    const uint16_t default_vid = 0x0bda;
    const uint16_t default_pid = 0x2832;

    std::string keyword;
    if (device_name) {
        keyword = device_name;
        std::transform(keyword.begin(), keyword.end(), keyword.begin(), ::tolower);
    }

    for (ssize_t i = 0; i < cnt; ++i) {
        libusb_device* dev = devs[i];
        libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(dev, &desc) != 0) continue;

        if (!device_name) {
            if (desc.idVendor == default_vid && desc.idProduct == default_pid) {
                connected = true;
                break;
            }
        } else {
            if (desc.iProduct) {
                libusb_device_handle* handle = nullptr;
                if (libusb_open(dev, &handle) == 0) {
                    unsigned char product[256];
                    int ret = libusb_get_string_descriptor_ascii(handle, desc.iProduct, product, sizeof(product));
                    if (ret > 0) {
                        std::string prod_str(reinterpret_cast<char*>(product));
                        std::transform(prod_str.begin(), prod_str.end(), prod_str.begin(), ::tolower);
                        //std::cout << "[DEBUG] USB Product: " << prod_str << std::endl;
                        if (prod_str.find(keyword) != std::string::npos) {
                            connected = true;
                            libusb_close(handle);
                            break;
                        }
                    }
                    libusb_close(handle);
                }
            }
        }
    }

    libusb_free_device_list(devs, 1);
    mLastStatus = connected;
    return connected;
}

void SystemStateMonitor::MonitorLoop() {
    bool wasConnected = true;

    while (mIsRunning) {
        // Perform monitoring tasks
        std::cout << "Monitoring system state..." << std::endl;

        bool isConnected = IsRtlSdrConnected(nullptr); //RTL
        if (isConnected) {
            std::cout << "RTL-SDR Device Connected." << std::endl;
        } else {
            std::cout << "RTL-SDR Device Disconnected!" << std::endl;
        }

        if (isConnected && !wasConnected) {
            std::cout << "[Monitor] RTL-SDR reconnected. Reinitializing..." << std::endl;

            if (mModes->dev) {
                rtlsdr_cancel_async(mModes->dev);
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                rtlsdr_close(mModes->dev);
                mModes->dev = nullptr;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            RestartReaderThread();

            wasConnected = true;
        } else if (!isConnected && wasConnected) {
            std::cout << "[Monitor] RTL-SDR disconnected." << std::endl;
            NotifyReaderExit();
            wasConnected = false;
        }

        SendHeartbeat();
    
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
}

extern "C" {
void* SystemStateMonitor_create(Modes_t* modes) {
    printf("Creating SystemStateMonitor...\n");
    return new SystemStateMonitor(modes);
}
void SystemStateMonitor_start(void* obj) {
    printf("Starting SystemStateMonitor...\n");
    static_cast<SystemStateMonitor*>(obj)->StartMonitoring();
}
void SystemStateMonitor_stop(void* obj) {
    printf("Stopping SystemStateMonitor...\n");
    static_cast<SystemStateMonitor*>(obj)->StopMonitoring();
}
void SystemStateMonitor_destroy(void* obj) {
    printf("Destroying SystemStateMonitor...\n");
    delete static_cast<SystemStateMonitor*>(obj);
}
}
