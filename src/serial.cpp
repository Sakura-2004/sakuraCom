#include "../inc/serial.hpp"

#include <cerrno>
#include <fcntl.h>
#include <unistd.h>


serial::serial()
    : fd(-1),
      handler(nullptr),
      isOpen(false),
      baudRate(115200),
      dataBits(8),
      parityBit(SP_PARITY_NONE),
      stopBit(1)
{
}

serial::~serial()
{
    close();
}

/**
 * @brief 打开串口
 * @retval true 打开成功
 * @retval false 串口已打开或打开失败
 */
bool serial::open(enum sp_mode m)
{
    if (this->isOpen)
    {
        return false;
    }

    enum sp_return result =
        sp_get_port_by_name(this->portName.c_str(), &(this->handler));
    if (result != SP_OK || this->handler == nullptr)
    {
        this->handler = nullptr;
        return false;
    }

    result = sp_open(this->handler, m);
    if (result != SP_OK)
    {
        sp_free_port(this->handler);
        this->handler = nullptr;
        return false;
    }

    this->isOpen = true;

    result = sp_set_baudrate(this->handler, this->baudRate);
    if (result != SP_OK)
    {
        close();
        return false;
    }

    result = sp_set_bits(this->handler, this->dataBits);
    if (result != SP_OK)
    {
        close();
        return false;
    }

    result = sp_set_parity(this->handler, this->parityBit);
    if (result != SP_OK)
    {
        close();
        return false;
    }

    result = sp_set_stopbits(this->handler, this->stopBit);
    if (result != SP_OK)
    {
        close();
        return false;
    }

    result = sp_get_port_handle(this->handler, &(this->fd));
    if (result != SP_OK)
    {
        close();
        return false;
    }

    const int flags = fcntl(this->fd, F_GETFL);
    if (flags == -1 ||
        fcntl(this->fd, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        close();
        return false;
    }

    return true;
}

/**
 * @brief 关闭串口
 * @retval true 关闭成功
 * @retval false 串口已关闭或关闭失败
 */
bool serial::close()
{
    if (!this->isOpen)
    {
        return false;
    }

    enum sp_return result = sp_close(this->handler);

    sp_free_port(this->handler);
    this->handler = nullptr;
    this->fd = -1;
    this->isOpen = false;

    return result == SP_OK;
}

/**
 * @brief 非阻塞发送字符串
 */
bool serial::writeString(const std::string &message)
{
    if (!this->isOpen || this->fd < 0)
    {
        return false;
    }

    std::size_t sent = 0;
    while (sent < message.size())
    {
        const ssize_t result =
            ::write(this->fd, message.data() + sent, message.size() - sent);

        if (result > 0)
        {
            sent += static_cast<std::size_t>(result);
            continue;
        }

        if (result < 0 && errno == EINTR)
        {
            continue;
        }

        // EAGAIN/EWOULDBLOCK 表示发送缓冲区已满，本次非阻塞写入结束。
        return false;
    }

    return true;
}

/**
 * @brief 非阻塞读取当前已到达的数据
 */
std::string serial::readString(std::size_t maxLength)
{
    if (!this->isOpen || this->fd < 0 || maxLength == 0)
    {
        return {};
    }

    std::string data(maxLength, '\0');
    ssize_t result = -1;

    do
    {
        result = ::read(this->fd, data.data(), data.size());
    } while (result < 0 && errno == EINTR);

    if (result <= 0)
    {
        return {};
    }

    data.resize(static_cast<std::size_t>(result));
    return data;
}

void serial::setPortName(const std::string &name)
{
    this->portName = name;
}

void serial::setBaudRate(int baudRate)
{
    this->baudRate = baudRate;
}

void serial::setDataBits(int dataBits)
{
    this->dataBits = dataBits;
}

void serial::setParityBit(sp_parity parityBit)
{
    this->parityBit = parityBit;
}

void serial::setStopBit(int stopBit)
{
    this->stopBit = stopBit;
}

bool serial::getStatus(){
    return this->isOpen;
}

/**
 * @brief 获取当前可用的串口列表
 */
std::vector<std::string> getSerialPortList()
{
    struct sp_port **ports = nullptr;
    if (sp_list_ports(&ports) != SP_OK || ports == nullptr)
    {
        return {};
    }

    std::vector<std::string> portList;
    for (std::size_t i = 0; ports[i] != nullptr; ++i)
    {
        const char *name = sp_get_port_name(ports[i]);
        if (name != nullptr)
        {
            portList.emplace_back(name);
        }
    }

    sp_free_port_list(ports);
    return portList;
}


