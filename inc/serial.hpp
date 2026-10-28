#pragma once

#include <libserialport.h>
#include <cstddef>
#include <iostream>
#include <functional>
#include <string>
#include <utility>
#include <vector>

class serial
{
private:
    int fd; // 文件描述符
    struct sp_port *handler; // 串口句柄
    bool isOpen; // 串口是否已打开

    std::string portName; // 串口名
    int baudRate;  // 波特率
    int dataBits;  // 数据位
    sp_parity parityBit; // 校验位
    int stopBit;   // 停止位

public:
    serial();
    ~serial();
    serial(const serial &) = delete;
    serial &operator=(const serial &) = delete;

    /**
     * @brief 打开串口
     * @param m 打开模式
     * @retval true 打开成功
     * @retval false 串口已打开或打开失败
     */
    bool open(enum sp_mode m);

    /**
     * @brief 关闭串口
     * @retval true 关闭成功
     * @retval false 串口已关闭或关闭失败
     */
    bool close();

    /**
     * @brief 非阻塞发送字符串
     * @retval true 字符串已全部写入
     * @retval false 串口未打开或字符串未全部写入
     */
    bool writeString(const std::string &message);

    /**
     * @brief 非阻塞读取当前已到达的数据
     * @param maxLength 单次最多读取的字节数
     * @retval 读取到的字符串；无数据或读取失败时返回空字符串
     */
    std::string readString(std::size_t maxLength = 1024);

    /**
     * @brief 获取状态，是否打开
     * @retval this->isOpen,1-> opened 0->closed
     */
    bool getStatus();

    /* 设置参数 */
    void setPortName(const std::string &name);
    void setBaudRate(int baudRate);
    void setDataBits(int dataBits);
    void setParityBit(sp_parity parityBit);
    void setStopBit(int stopBit);
};

/**
 * @brief 获取当前可用的串口列表
 * @retval 当前系统可用的串口名称；获取失败时返回空列表
 */
std::vector<std::string> getSerialPortList();
