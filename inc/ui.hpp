#pragma once

#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>

#include <iostream>
#include <functional>
#include <mutex>
#include <string>
#include <utility>

#include "serial.hpp"

#define OUTPUT_LABLE_DEFAULT_HEIGHT 12 // output默认高度
#define INPUT_LABLE_DEFAULT_HEIGHT 8   // input默认高度

using namespace ftxui;

/**
 * ******************************************************************************************************
 * @name windowRadioBox
 * @brief 由window包装起来的radioBox
 * ******************************************************************************************************
 */
class windowRadioBox : public ftxui::ComponentBase
{
private:
    std::vector<std::string> menu; // 选项
    int &selected;                 // 被选中的项目
    std::string windowName;        // Window上显示的名字
    int width;                     // 宽度

public:
    /* 构造函数 */
    windowRadioBox(std::vector<std::string> menu, int &selected, std::string windowName = "", int width = 12);
    /* 获取selected数值 */
    int getSelected() const;
    /* 界面渲染函数重写 */
    Element OnRender() override;
};

/**
 * ******************************************************************************************************
 * @name outputLable
 * @brief 有滚动输出栏，带有上下滑动栏，有hex和ascii切换的功能，用于显示串口接收到的数据的显示
 * ******************************************************************************************************
 */
class outputLable : public ftxui::ComponentBase
{
private:
    /* 外部传入参数 */
    ftxui::Color focusColor; // 使用RGBA颜色，红，绿，蓝，透明度

    std::vector<std::string> lines; // 以一个换行记为一行
    std::string lastLine;           // 最后一行，没有换行
    std::string rawLine;            // 原始信息，保存两份数据，快速调用

    int lineHeight; // 从第几行开始显示
    int widthMinus; // 其他地方减少的宽度，忽略window的4

    /* 内部自动更新参数 */
    int dataHeight; // 实际数据行数,高度
    int width;      // 总宽度，实际数据宽度是总宽度减少4，宽度是根据终端宽度确定的，使用的时候要减去其他地方减少的宽度

    Color barColor; // 导航栏的颜色

    /* 内部其他组件的参数 */
    Component radioGroup; // 纵向布局，控制下面的RadioBox
    /* RadioBox-0 选择ascii或hex */
    int mode = 0;                                              // 显示hex或者显示string的mode参数，在RadioBox-0中更新 0->char 1->hex
    std::vector<std::string> radiobox_Mode = {"Ascii", "Hex"}; // radioBox-0 显示字符串内容
    Component radio0;                                          // 句柄

    /* RadioBox-1 选择在hex模式下是否自动根据\n换行*/
    int hexAutoLB = 0;                                               // 在hex模式下是否自动换行,在radioBox-1中更新 0->自动换行 1->不自动换行
    std::vector<std::string> radiobox_AutoLB = {"0A换行", "不换行"}; // radioBox-1 显示字符串内容
    Component radio1;                                                // 句柄
    int nonLBHeight = 0;                                             // 不自动换行模式下的高度
    int nonLBlineHeight = 0;                                         // 不自动换行模式下的现在高度

    /* button-0 清空按钮 */
    Component button;

public:
    outputLable(Color c = Color::Red);
    /* 界面渲染函数重写 */
    Element OnRender() override;

    /* 输入事件 */
    bool OnEvent(Event event) override;

    /* 是否参与焦点 */
    bool Focusable() const override;

    /* 向上移动一行光标 */
    void upper();

    /* 向下移动一行光标 */
    void lower();

    /**
     * @brief 送入一个字符到显示
     * @param c 送入的字符
     */
    void putChar(char c);

    /**
     * @brief 送入一个字符串到显示，实际调用路径使用putChar依次送入
     * @param s 送入的字符串
     */
    void putString(std::string s);

    /* 设置数据行高度*/
    void setDataHeight(int h);
};

/**
 * ******************************************************************************************************
 * @name inputLable
 * @brief 向串口的打印窗口显示
 * ******************************************************************************************************
 */
class inputLable : public ftxui::ComponentBase
{
private:
    /* 外部传入参数 */
    Color focusColor;        // 使用RGBA颜色，红，绿，蓝，透明度
    Color currentFocusColor; // 当前的颜色

    std::vector<std::string> lines; // 以一个换行记为一行
    std::string lastLine;           // 最后一行，没有换行
    std::string rawLine;            // 原始信息，保存两份数据，快速调用

    int lineHeight; // 从第几行开始显示
    int widthMinus; // 其他地方减少的宽度，忽略window的4

    /* 内部自动更新参数 */
    int dataHeight; // 实际数据行数,高度
    int width;      // 总宽度，实际数据宽度是总宽度减少4，宽度是根据终端宽度确定的，使用的时候要减去其他地方减少的宽度

    Color barColor;                   // 导航栏的颜色
    int barDataHeight = 1;            // 导航栏数据总行数
    int barDataDisplayLineHeight = 0; // 导航栏显示的第一行

    /* 输入组件的参数 */
    Component inputRoot;  // 横向布局，组合输入、滚动条和控制区
    Component inputGroup; // 纵向布局，控制分别显示2种模式下的输入框

    /* Input0 -> Ascii输入框 */
    Component inputAscii;                    // 输入Ascii框
    std::string inputDataAscii;              // 输入数据
    std::vector<int> searchTableAscii = {0}; // Ascii用于建立索引加快查找光标高度
    int cursorPositionAscii = 0;             // ascii模式的光标位置
    int heightAscii = 1;                     // ascii模式的总行数
    int cursorHeightAscii = 0;               // ascii光标在第几行（从0开始）
    int firstDisplayHeightAscii = 0;         // ascii输入框第一行对应的内容行

    /* Input1 -> Hex输入框 */
    Component inputHex;                    // 输入Hex框
    std::string inputDataHex;              // 输入数据
    std::vector<int> searchTableHex = {0}; // Hex用于建立索引加快查找光标高度
    int cursorPositionHex = 0;             // hex模式的光标位置
    int heightHex = 1;                     // hex模式的总行数
    int cursorHeightHex = 0;               // hex光标在第几行（从0开始）
    int firstDisplayHeightHex = 0;         // hex输入框第一行对应的内容行

    /* 控制组件的参数 */
    Component radioGroup; // 纵向布局，控制下面的RadioBox
    /* RadioBox-0 选择ascii或hex */
    int mode = 0;                                              // 显示hex或者显示string的mode参数，在RadioBox-0中更新 0->char 1->hex
    std::vector<std::string> radiobox_Mode = {"Ascii", "Hex"}; // radioBox-0 显示字符串内容
    Component radio0;                                          // 句柄

    /* button-0 清空按钮 */
    Component button;

    /* button-1 发送按钮 */
    Component sendButton;
    std::function<bool(const std::string &, bool)> sendCallback;

public:
    inputLable(Color cor = Color::Red);
    /* 界面渲染函数重写 */
    Element OnRender() override;

    /* 输入事件 */
    bool OnEvent(Event event) override;

    /* 是否参与焦点 */
    bool Focusable() const override;

    /* 向上移动一行光标 */
    void upper();

    /* 向下移动一行光标 */
    void lower();

    /* 设置数据行高度*/
    void setDataHeight(int h);

    /**
     * @brief 设置发送按钮回调
     * @param callback 参数1为待发送的原始数据，参数2表示是否为Hex模式
     */
    void setSendCallback(
        std::function<bool(const std::string &, bool)> callback);
};

/**
 * ******************************************************************************************************
 * @name dropDownWithWindow
 * @brief 带有window的下拉框
 * ******************************************************************************************************
 */
class dropDownWithWindow : public ftxui::ComponentBase
{
private:
    Color *focusColor;       // 被选中的颜色，跟随主组件一起更新
    std::string windowName;  // window左上角显示名
    Component dropDown;      // 组件本身
    ConstStringListRef list; // dropDown的list
    int *selected;           // 选的是几
    std::mutex *listMutex;   // 保护外部列表；为空时无需加锁

public:
    dropDownWithWindow(
        ConstStringListRef list,
        int *selected,
        Color *cor,
        std::string windowName = "window",
        std::mutex *listMutex = nullptr);
    /* 界面渲染函数重写 */
    Element OnRender() override;

    /* 输入事件 */
    bool OnEvent(Event event) override;

    /* 是否参与焦点 */
    bool Focusable() const override;

    /* 设置window的名字 */
    void setWindowName(std::string windowName);
};

/**
 * ******************************************************************************************************
 * @name serialSelectMenu
 * @brief 显示串口的菜单组件
 * ******************************************************************************************************
 */
class serialSelectMenu : public ftxui::ComponentBase
{
private:
    /* 外部更新 */
    Color focusColor;        // 使用RGBA颜色，红，绿，蓝，透明度
    Color currentFocusColor; // 当前的颜色

    /* 内部处理 */
    /* 串口 */
    serial port;
    /* 菜单集合 */
    Component root;  // 根
    Component line1; // 第一行设置4种参数
    Component line2; // 第二行选择串口名和放置按钮

    std::mutex portNameMutex; // 保护串口名称列表和选中索引
    Component portName;
    std::vector<std::string> portNameStr;
    int portNameSelected = 0;

    Component portBaudRate;
    std::vector<std::string> portBaudRateStr{{"9600"}, {"19200"}, {"38400"}, {"57600"}, {"115200"}, {"230400"}, {"460800"}, {"921600"}};
    int portBaudRateSelected = 4;

    Component portDataBits;
    std::vector<std::string> portDataBitsStr{{"8位"}, {"7位"}, {"6位"}, {"5位"}};
    int portDataBitsSelected = 0;

    Component portParityBit;
    std::vector<std::string> portParityBitStr{{"无校验"}, {"奇校验"}, {"偶校验"}, {"1校验"}, {"0校验"}};
    int portParityBitSelected = 0;

    Component portStopBit;
    std::vector<std::string> portStopBitStr{{"1位"}, {"1.5位"}, {"2位"}};
    int portStopBitSelected = 0;
    /* 按钮 */
    Component openButton;
    Component closeButton;

public:
    serialSelectMenu(Color cor = Color::Red);
    /* 界面渲染函数重写 */
    Element OnRender() override;

    /* 输入事件 */
    bool OnEvent(Event event) override;

    /* 是否参与焦点 */
    bool Focusable() const override;

    /* 调用刷新串口列表 */
    void RefreshSerial();

    /* 调用以从打开的串口中读数据 */
    std::string tryReadOnce();

    /* 调用以向打开的串口中写数据 */
    bool tryWriteOnce(std::string str);
};
