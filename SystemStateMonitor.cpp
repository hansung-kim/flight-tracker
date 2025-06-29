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
#include <sstream>
#include <fstream>
#include <curl/curl.h>

typedef struct {
    uint8_t sdrConnected; // 0 or 1
    uint8_t wifiEnabled;  // 0 or 1
    uint8_t reserved[62];  // Padding to make the size 64 bytes
} HeartbeatMsg_t;

const char* my_ckey = "J1y+RwJnL_ZM1nKZ!w1YVWx%DzlqPPPL~g83DKb(3l~E%>J}26gG=jCyT8fP-Pz4a!OD)ZBK)q|]Hp$?MD}O--L6A%k:7)b]].a#%3bP#>B9Go";
const char* reconnect_cmd = "nc -w 60 localhost 30002 | nc -w 60 data.adsbhub.org 5001";

class SystemStateMonitor {
public:
    SystemStateMonitor(Modes_t *modes);
    ~SystemStateMonitor();

    void StartMonitoring();
    void StopMonitoring();

private:
    void MonitorLoop();
    void NetMonitorLoop();
    void MaintainADSBHubConnection(const std::string& ckey, const std::string& reconnectCmd);
    
    bool mIsSDRConnected;
    bool mIsWiFiEnabled;
    
    Modes_t *mModes;

    HeartbeatMsg_t mHeartbeatMsg;

    std::string mClientIp;
    std::thread mMonitorThread;
    std::thread mNetMonitorThread;
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

    mUdpSockFd = -1;

    if (libusb_init(&mUsbContext) != 0) {
        std::cerr << "Failed to initialize libusb" << std::endl;
        mUsbContext = nullptr;
    }

    memset(&mHeartbeatMsg, 0, sizeof(mHeartbeatMsg));

    mIsSDRConnected = false;
    mIsWiFiEnabled = false;
}

SystemStateMonitor::~SystemStateMonitor() {
    StopMonitoring();
    if (mUsbContext) libusb_exit(mUsbContext);
}

void SystemStateMonitor::StartMonitoring() {
    if (!mIsRunning) {
        mIsRunning = true;
        mMonitorThread = std::thread(&SystemStateMonitor::MonitorLoop, this);
        mNetMonitorThread =  std::thread(&SystemStateMonitor::NetMonitorLoop, this);
    }
}

bool SystemStateMonitor::InitUdp() {
    if (mUdpSockFd > 0) {
        return true;
    }

    if(strlen(mModes->client_ip) <= 0) {
        std::cerr << "client IP address was not updated" << mClientIp << std::endl;
        mClientIp.clear();
        mClientIp.append("192.168.137.1");
    } else {
        mClientIp.clear();
        mClientIp.append(mModes->client_ip);
    }
    
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

    mHeartbeatMsg.sdrConnected = static_cast<uint8_t>(mIsSDRConnected ? 1 : 0);
    mHeartbeatMsg.wifiEnabled = static_cast<uint8_t>(mIsWiFiEnabled ? 1 : 0);
              
    std::cout << "Sending heartbeat to " << mClientIp << ":" << mClientPort
            << " [SDR :" << static_cast<int>(mHeartbeatMsg.sdrConnected)
            << ", WiFi : " << static_cast<int>(mHeartbeatMsg.wifiEnabled)
            << "]" << std::endl;

    ssize_t sentBytes = sendto(mUdpSockFd, &mHeartbeatMsg, sizeof(mHeartbeatMsg), 0,
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

bool isWiFiConnected() {
    std::string result;
    char buffer[128] = {0};

    FILE* fp = popen("iwgetid -r", "r");
    if (!fp) return false;

    if (fgets(buffer, sizeof(buffer), fp)) {
        result = buffer;
        result.erase(result.find_last_not_of("\n\r") + 1);
    }

    pclose(fp);

    return !result.empty();
}

void SystemStateMonitor::MaintainADSBHubConnection(const std::string& ckey, const std::string& reconnectCmd) {
    static std::string myip4 = "0.0.0.0";
    static std::string myip6 = "";
    static int cmin = 0;

    bool connected = false;
    FILE* pipe = popen("netstat -an | grep ':5001'", "r");
    if (pipe) {
        char buffer[512];
        while (fgets(buffer, sizeof(buffer), pipe)) {
            if (strstr(buffer, "ESTABLISHED")) {
                connected = true;
                break;
            }
        }
        pclose(pipe);
    }

    if (!connected) {
        std::cout << "[ADSBHub] Not connected. Reconnecting..." << std::endl;
        std::string fullCmd = "(" + reconnectCmd + ") &";
        system(fullCmd.c_str());
    } else {
        std::cout << "[ADSBHub] Connected." << std::endl;
    }

    if (!ckey.empty()) {
        cmin--;
        if (cmin <= 0) {
            cmin = 5;

            auto getPublicIP = [](const std::string& url) -> std::string {
                std::string result;
                FILE* fp = popen(("curl -s " + url).c_str(), "r");
                if (fp) {
                    char line[128];
                    if (fgets(line, sizeof(line), fp)) {
                        result = line;
                        result.erase(result.find_last_not_of("\n\r") + 1); // trim
                    }
                    pclose(fp);
                }
                return result;
            };

            std::string ip4 = getPublicIP("https://ip4.adsbhub.org/getmyip.php");
            std::string ip6 = getPublicIP("https://ip6.adsbhub.org/getmyip.php");

            if (ip4 != myip4 || ip6 != myip6) {
                CURL* curl = curl_easy_init();
                if (curl) {
                    std::ostringstream url;
                    url << "https://www.adsbhub.org/updateip.php?sessid=" << ckey
                        << "&myip=" << ip4 << "&myip6=" << ip6;

                    curl_easy_setopt(curl, CURLOPT_URL, url.str().c_str());
                    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
                    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

                    CURLcode res = curl_easy_perform(curl);
                    if (res == CURLE_OK) {
                        std::cout << "[ADSBHub] IP updated: " << ip4 << " / " << ip6 << std::endl;
                        myip4 = ip4;
                        myip6 = ip6;
                    } else {
                        std::cerr << "[ADSBHub] IP update failed: "
                                  << curl_easy_strerror(res) << std::endl;
                    }
                    curl_easy_cleanup(curl);
                }
            }
        }
    }
}

void SystemStateMonitor::NetMonitorLoop() {
    bool wasConnected = false;
    mIsWiFiEnabled = false;

    while (1) {
        bool nowConnected = isWiFiConnected();

        if (nowConnected && !wasConnected) {
            std::cout << "[ADSBHub] Wi-Fi is connected (first or reconnected)." << std::endl;
            mIsWiFiEnabled = true;
            MaintainADSBHubConnection(my_ckey, reconnect_cmd);
        }

        if (nowConnected) {
            std::cout << "[ADSBHub] Wi-Fi is connected." << std::endl;
            mIsWiFiEnabled = true;
        } else {
            std::cout << "[ADSBHub] Wi-Fi NOT connected." << std::endl;
            mIsWiFiEnabled = false;
        }

        wasConnected = nowConnected;

        std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    //return NULL;
}

void SystemStateMonitor::MonitorLoop() {
    bool wasConnected = true;

    while (mIsRunning) {
        // Perform monitoring tasks
        std::cout << "Monitoring system state..." << std::endl;

        bool isConnected = IsRtlSdrConnected(nullptr); //RTL
        if (isConnected) {
            std::cout << "RTL-SDR Device Connected." << std::endl;
            mIsSDRConnected = true;
        } else {
            std::cout << "RTL-SDR Device Disconnected!" << std::endl;
            mIsSDRConnected = false;
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

            mIsSDRConnected = true;
            wasConnected = true;
        } else if (!isConnected && wasConnected) {
            std::cout << "[Monitor] RTL-SDR disconnected." << std::endl;
            NotifyReaderExit();
            mIsSDRConnected = false;
            wasConnected = false;
        }

        SendHeartbeat();
    
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
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
