#include "../inc/ui.hpp"

using namespace ftxui;

/**
 * @brief 输入一个字符串，把字符串转换为HEX输出
 * @param s 输入的字符串
 * @retval 返回的是直接可以打印的16进制字符串
 */
static std::string stringToHex(std::string &s)
{
    const char *table = {"0123456789ABCDEF"};
    std::string ret;
    ret.reserve(s.size() * 2);

    for (int i = 0; i < s.size(); i++)
    {
        unsigned char tmp = static_cast<unsigned char>(s[i]);

        ret.push_back(table[tmp >> 4]);
        ret.push_back(table[tmp & 0x0f]);
        ret.push_back(' ');
    }

    return ret;
}

/**
 * @brief 输入一个字符串，把字符串转换为ASCII输出，去掉换行符号
 * @param s 输入的字符串
 * @retval 返回的是直接可以打印的16进制字符串
 */
static std::string stringToAscii(std::string &s)
{
    std::string ret;

    for (int i = 0; i < s.size(); i++)
    {
        if (s[i] != '\n')
        {
            ret.push_back(s[i]);
        }
        else
        {
            ret.push_back(' ');
        }
    }

    return ret;
}

/**
 * @brief 绘制一个简易的垂直滚动条
 * @param height 要绘制的高度
 * @param allLineNum 对应的实际数据的长度
 * @param displayStartLine 对应的需要作为正在显示数据的起始位置
 * @param displayLineNum 对应的需要作为正在显示数据的长度
 * @param color 被选中内容的显示颜色
 */
static Element drawVerticalScrollbar(int height, int allLineNum, int displayStartLine, int displayLineNum, Color &cor)
{
    auto view = canvas(2, 4 * height, [height, allLineNum, displayStartLine, displayLineNum, cor](Canvas &c)
                       {
        /* 绘制进度条的函数 */
        int h = c.height(); // 每个高度对应4个像素
        int w = c.width();  // 每个宽度对应2个像素

        int startH,endH;
        if(allLineNum==0){
            startH = 0;
            endH =   h;
        }else{
            startH = ((double)displayStartLine / allLineNum) * h;
            endH = ((double)(displayLineNum + displayStartLine) / allLineNum) * h;
        }
        
        for (int i = 0; i < h; i++)
        {
            if (i <= endH && i >= startH)
            {
                c.DrawBlock(0, i, true, cor);
                c.DrawBlock(1, i, true, cor);
                continue;
            }
        } }) |
                size(WIDTH, EQUAL, 1) | size(HEIGHT, EQUAL, height);
    return view;
}

/**
 * @brief 使用二分查找，返回数组中小于等于n且最靠近n的元素下标
 * @param l 传入数组
 * @param n 传入数值
 * @retval 找到时返回下标，没有符合条件的元素时返回-1
 */
static int binarySearch_LeastValue_ArrayIndex(const std::vector<int> &l, int n)
{
    int left = 0;
    int right = static_cast<int>(l.size()) - 1;
    int result = -1;

    while (left <= right)
    {
        int middle = left + (right - left) / 2;

        if (l[middle] <= n)
        {
            result = middle;
            left = middle + 1;
        }
        else
        {
            right = middle - 1;
        }
    }

    return result;
}

/**
 * @brief 按显示宽度整理换行，并重建每一行的起始位置
 * @param text 字符串内容
 * @param searchTable 对字符串的搜索表
 * @param width 一行允许的最大字符数；小于等于0时不自动换行
 * @param cursorPosition 光标位置，换行变化时会同步调整
 */
static void RebuildInputLineIndex(
    std::string &text,
    std::vector<int> &searchTable,
    int width,
    int &cursorPosition)
{
    std::string formatted;
    formatted.reserve(text.size());

    int newCursorPosition = 0;
    int lineLength = 0;

    for (int i = 0; i < static_cast<int>(text.size()); i++)
    {
        char c = text[i];

        if (c == '\n')
        {
            formatted.push_back(c);
            lineLength = 0;

            if (i < cursorPosition)
            {
                newCursorPosition++;
            }
            continue;
        }

        if (width > 0 && lineLength >= width)
        {
            formatted.push_back('\n');
            lineLength = 0;

            if (i <= cursorPosition)
            {
                newCursorPosition++;
            }
        }

        formatted.push_back(c);
        lineLength++;

        if (i < cursorPosition)
        {
            newCursorPosition++;
        }
    }

    text.swap(formatted);
    cursorPosition = newCursorPosition;

    searchTable.clear();
    searchTable.push_back(0);

    for (int i = 0; i < static_cast<int>(text.size()); i++)
    {
        if (text[i] == '\n')
        {
            searchTable.push_back(i + 1);
        }
    }
}

/**
 * @brief 删除Hex输入中不属于0-9、a-f、A-F的字符
 */
static void SanitizeHexInput(std::string &text, int &cursorPosition)
{
    std::string filtered;
    filtered.reserve(text.size());

    int newCursorPosition = 0;

    for (int i = 0; i < static_cast<int>(text.size()); i++)
    {
        char c = text[i];

        bool isHex =
            (c >= '0' && c <= '9') ||
            (c >= 'a' && c <= 'f') ||
            (c >= 'A' && c <= 'F');

        if (isHex || c == '\n')
        {
            filtered.push_back(c);

            if (i < cursorPosition)
            {
                newCursorPosition++;
            }
        }
    }

    text.swap(filtered);
    cursorPosition = newCursorPosition;
}

static int HexDigitValue(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    return -1;
}

/**
 * @brief 将十六进制文本转换为原始字节
 * @param text 十六进制输入，空白及换行会被忽略
 * @retval 转换后的原始数据；没有有效十六进制字符时返回空字符串
 */
static std::string HexStringToBytes(const std::string &text)
{
    std::string digits;
    digits.reserve(text.size());

    for (char c : text)
    {
        if (HexDigitValue(c) >= 0)
        {
            digits.push_back(c);
        }
    }

    if (digits.empty())
    {
        return {};
    }

    /* 奇数位左侧补0，例如 ABC -> 0A BC */
    if (digits.size() % 2 != 0)
    {
        digits.insert(digits.begin(), '0');
    }

    std::string bytes;
    bytes.reserve(digits.size() / 2);
    for (std::size_t i = 0; i < digits.size(); i += 2)
    {
        const int high = HexDigitValue(digits[i]);
        const int low = HexDigitValue(digits[i + 1]);
        bytes.push_back(static_cast<char>((high << 4) | low));
    }

    return bytes;
}

/**
 * @brief 根据光标位置计算总行数、光标行和第一显示行
 */
static void UpdateInputDisplayInformation(
    const std::vector<int> &searchTable,
    int cursorPosition,
    int visibleHeight,
    int &totalHeight,
    int &cursorHeight,
    int &firstDisplayHeight)
{
    totalHeight = static_cast<int>(searchTable.size());
    if (totalHeight < 1)
    {
        totalHeight = 1;
    }

    if (visibleHeight < 1)
    {
        visibleHeight = 1;
    }

    cursorHeight = binarySearch_LeastValue_ArrayIndex(
        searchTable,
        cursorPosition);
    if (cursorHeight < 0)
    {
        cursorHeight = 0;
    }

    int maxFirst = totalHeight - visibleHeight;
    if (maxFirst < 0)
    {
        maxFirst = 0;
    }

    firstDisplayHeight =
        cursorHeight - (visibleHeight - 1) / 2;
    if (firstDisplayHeight < 0)
    {
        firstDisplayHeight = 0;
    }
    if (firstDisplayHeight > maxFirst)
    {
        firstDisplayHeight = maxFirst;
    }
}

/**
 * @brief 把光标向上或向下移动一行，并尽量保持当前列位置
 * @param direction -1表示向上，1表示向下
 */
static int MoveInputCursorVertical(
    const std::string &text,
    const std::vector<int> &searchTable,
    int cursorPosition,
    int direction)
{
    if (searchTable.empty())
    {
        return cursorPosition;
    }

    if (cursorPosition < 0)
    {
        cursorPosition = 0;
    }
    if (cursorPosition > static_cast<int>(text.size()))
    {
        cursorPosition = static_cast<int>(text.size());
    }

    int currentLine = binarySearch_LeastValue_ArrayIndex(
        searchTable,
        cursorPosition);
    if (currentLine < 0)
    {
        currentLine = 0;
    }

    int targetLine = currentLine + direction;
    int lineCount = static_cast<int>(searchTable.size());
    if (targetLine < 0 || targetLine >= lineCount)
    {
        return cursorPosition;
    }

    int currentLineStart = searchTable[currentLine];
    int targetLineStart = searchTable[targetLine];
    int currentColumn = cursorPosition - currentLineStart;

    int targetLineEnd;
    if (targetLine + 1 < lineCount)
    {
        targetLineEnd = searchTable[targetLine + 1] - 1;
    }
    else
    {
        targetLineEnd = static_cast<int>(text.size());
    }

    int targetLineLength = targetLineEnd - targetLineStart;
    int targetColumn = currentColumn;
    if (targetColumn > targetLineLength)
    {
        targetColumn = targetLineLength;
    }

    return targetLineStart + targetColumn;
}

/**
 * ******************************************************************************************************
 * @name windowRadioBox
 * @brief 由window包装起来的radioBox
 * ******************************************************************************************************
 */
windowRadioBox::windowRadioBox(std::vector<std::string> menu, int &selected, std::string windowName, int width)
    : selected(selected)
{
    this->menu = menu;
    this->windowName = windowName;
    this->width = width;

    // 准备子组建RadioBox -> 0
    auto radioBox = Radiobox(&this->menu, &this->selected);

    // 挂载
    Add(radioBox);
}

Element windowRadioBox::OnRender()
{
    return window(text(this->windowName), this->ChildAt(0)->Render()) | size(WIDTH, EQUAL, this->width);
}

int windowRadioBox::getSelected() const
{
    return this->selected;
}

/**
 * ******************************************************************************************************
 * @name outputLable
 * @brief 用过滚动输出栏，带有上下滑动栏
 * ******************************************************************************************************
 */
outputLable::outputLable(Color c)
{
    // 准备自己的数据
    this->focusColor = c;
    this->lineHeight = 0;
    this->dataHeight = OUTPUT_LABLE_DEFAULT_HEIGHT;
    this->widthMinus = 2;
    this->barColor = Color(0xa1, 0xa1, 0xa0);

    this->nonLBHeight = 0;
    this->nonLBlineHeight = 0;

    // 准备子组建RadioBox-0 -> 0
    this->radio0 = Make<windowRadioBox>(
        this->radiobox_Mode,
        this->mode);

    // 准备子组建RadioBox-1 -> 1
    this->radio1 = Make<windowRadioBox>(
        this->radiobox_AutoLB,
        this->hexAutoLB);

    this->radio1 = Maybe(this->radio1, [this]
                         { return this->mode == 1; });

    // 准备子组建button-0 ->2
    this->button = Button(
        "清空",
        [this]
        {
            this->lines.clear();
            this->lastLine.clear();
            this->rawLine.clear();
            this->lineHeight = 0;
            this->nonLBHeight = 0;
            this->nonLBlineHeight = 0;
        });

    this->radioGroup = Container::Vertical({this->radio0,
                                            this->radio1,
                                            this->button});
    Add(this->radioGroup);
}

Element outputLable::OnRender()
{
    /* 准备数据 */
    Color currentColor(255, 255, 255);
    if (this->Focused())
    {
        currentColor = this->focusColor;
    }

    Dimensions terminal_size = Terminal::Size();
    this->width = terminal_size.dimx - this->widthMinus - 15;

    /* 准备数据 1个字符在hex模式下占用3个宽度 */
    int numPerline = this->width / 3;
    if (numPerline < 1)
    {
        numPerline = 1;
    }
    this->nonLBHeight = this->rawLine.size() / numPerline;
    if (this->rawLine.size() % numPerline != 0)
    {
        this->nonLBHeight++;
    }

    /* 计算布局其中显示的内容 */
    std::vector<Element> lines;
    Element view;
    if (this->mode == 0)
    {
        /* Ascii 模式 */
        std::string windowName = "OutPut Line: " + std::to_string(this->lineHeight + 1) + '/' + std::to_string(this->lines.size() + 1);
        for (int i = this->lineHeight; i < this->lines.size(); i++)
        {
            lines.push_back(paragraph(stringToAscii(this->lines.at(i))) | size(WIDTH, EQUAL, this->width));
        }
        lines.push_back(paragraph(stringToAscii(this->lastLine)) | size(WIDTH, EQUAL, this->width));
        /* Ascii布局不用考虑其他因素 */
        view = hbox({window(text(windowName) | color(currentColor), frame(vbox(lines)) | size(WIDTH, EQUAL, this->width) | size(HEIGHT, EQUAL, this->dataHeight) | center),
                     window(text(""), drawVerticalScrollbar(this->dataHeight, this->lines.size() + 1, this->lineHeight, this->dataHeight, this->barColor)),
                     vbox({this->radioGroup->Render()})

        });
    }
    else if (this->mode == 1)
    {
        /* Hex 模式 */
        /* 在Hex 模式下考虑是否见到0A换行 */
        std::string windowName = "OutPut Line: " + std::to_string(this->lineHeight + 1) + '/' + std::to_string(this->lines.size() + 1);
        if (this->hexAutoLB == 0)
        {
            /* 自动换行 */
            for (int i = this->lineHeight; i < this->lines.size(); i++)
            {
                lines.push_back(paragraph(stringToHex(this->lines.at(i))) | size(WIDTH, EQUAL, this->width));
            }

            lines.push_back(paragraph(stringToHex(this->lastLine)) | size(WIDTH, EQUAL, this->width));
            view = hbox({window(text(windowName) | color(currentColor), frame(vbox(lines)) | size(WIDTH, EQUAL, this->width) | size(HEIGHT, EQUAL, this->dataHeight) | center),
                         window(text(""), drawVerticalScrollbar(this->dataHeight, this->lines.size() + 1, this->lineHeight, this->dataHeight, this->barColor)),
                         vbox({this->radioGroup->Render()})

            });
        }
        else if (this->hexAutoLB == 1)
        {
            /* 不自动换行 , 不自动换行的话需要重新准备数据 */
            std::string tmp;

            int maxStart = this->nonLBHeight - this->dataHeight;
            if (maxStart < 0)
            {
                maxStart = 0;
            }
            if (this->nonLBlineHeight < 0)
            {
                this->nonLBlineHeight = 0;
            }
            if (this->nonLBlineHeight > maxStart)
            {
                this->nonLBlineHeight = maxStart;
            }

            int rawSize = static_cast<int>(this->rawLine.size());
            int startByte = this->nonLBlineHeight * numPerline;
            int endByte = startByte + this->dataHeight * numPerline;
            if (endByte > rawSize)
            {
                endByte = rawSize;
            }

            std::string windowName = "OutPut Line: " + std::to_string(this->nonLBlineHeight + 1) + '/' + std::to_string(this->nonLBHeight <= 0 ? 1 : this->nonLBHeight);
            for (int i = startByte; i < endByte; i++)
            {
                if (i > startByte && i % numPerline == 0)
                {
                    lines.push_back(paragraph(stringToHex(tmp)) | size(WIDTH, EQUAL, this->width));
                    tmp.clear();
                }

                tmp.push_back(this->rawLine[i]);
            }
            if (!tmp.empty())
            {
                lines.push_back(paragraph(stringToHex(tmp)) | size(WIDTH, EQUAL, this->width));
            }

            view = hbox({window(text(windowName) | color(currentColor), frame(vbox(lines)) | size(WIDTH, EQUAL, this->width) | size(HEIGHT, EQUAL, this->dataHeight) | center),
                         window(text(""), drawVerticalScrollbar(this->dataHeight, this->nonLBHeight, this->nonLBlineHeight, this->dataHeight, this->barColor)),
                         vbox({this->radioGroup->Render()})

            });
        }
    }
    return view;
}

/* 输入事件 */
bool outputLable::OnEvent(Event event)
{
    if (event == Event::W || event == Event::w)
    {
        this->upper();
        return true;
    }
    else if (event == Event::s || event == Event::S)
    {
        this->lower();
        return true;
    }
    else if (event.is_mouse())
    {
        /* 监控鼠标事件 */
        Mouse mouse = event.mouse();
        if (mouse.button == Mouse::WheelUp)
        {
            this->upper();
            return true;
        }
        else if (mouse.button == Mouse::WheelDown)
        {
            this->lower();
            return true;
        }
    }
    return ComponentBase::OnEvent(event);
}

/* 是否参与焦点 */
bool outputLable::Focusable() const
{
    return ComponentBase::Focusable();
}

/**
 * @brief 送入一个字符到显示
 * @param c 送入的字符
 */
void outputLable::putChar(char c)
{
    this->rawLine.push_back(c);
    if (c == '\n')
    {
        this->lastLine.push_back(c);
        this->lines.push_back(this->lastLine);
        this->lastLine.clear();
        return;
    }
    this->lastLine.push_back(c);
}

/* 向上移动一行光标 */
void outputLable::upper()
{
    if (this->mode == 1 && this->hexAutoLB == 1)
    {
        this->nonLBlineHeight--;
        if (this->nonLBlineHeight < 0)
        {
            this->nonLBlineHeight = 0;
        }
    }
    else
    {
        this->lineHeight--;
        if (this->lineHeight < 0)
        {
            this->lineHeight = 0;
        }
    }
}

/* 向下移动一行光标 */
void outputLable::lower()
{
    if (this->mode == 1 && this->hexAutoLB == 1)
    {
        int maxH = this->nonLBHeight - this->dataHeight;
        if (maxH < 0)
        {
            maxH = 0;
        }

        this->nonLBlineHeight++;
        if (this->nonLBlineHeight > maxH)
        {
            this->nonLBlineHeight = maxH;
        }
    }
    else
    {
        int totalLines = static_cast<int>(this->lines.size()) + 1;
        int maxH = totalLines - this->dataHeight;
        if (maxH < 0)
        {
            maxH = 0;
        }

        this->lineHeight++;
        if (this->lineHeight > maxH)
        {
            this->lineHeight = maxH;
        }
    }
}

void outputLable::putString(std::string s)
{
    for (int i = 0; i < s.size(); i++)
    {
        this->putChar(s[i]);
    }
}

void outputLable::setDataHeight(int h)
{
    this->dataHeight = h;
}
/**
 * ******************************************************************************************************
 * @name inputLable
 * @brief 向串口的打印窗口显示
 * ******************************************************************************************************
 */
inputLable::inputLable(Color cor)
{
    /* 初始化数据 */
    this->focusColor = cor;
    this->currentFocusColor = Color(255, 255, 255);
    this->lineHeight = 0;
    this->dataHeight = INPUT_LABLE_DEFAULT_HEIGHT;
    this->widthMinus = 2;
    this->width = 1;
    this->barColor = Color(0xa1, 0xa1, 0xa0);

    this->barDataDisplayLineHeight = 0;
    this->barDataHeight = 0;
    this->searchTableAscii = {0};
    this->searchTableHex = {0};

    /* 初始化Ascii框 */
    this->inputAscii = Input({.content = &this->inputDataAscii,
                              .placeholder = "在此输入发送的数值",
                              .multiline = true,
                              .on_change = [this]
                              {
                                  RebuildInputLineIndex(
                                      this->inputDataAscii,
                                      this->searchTableAscii,
                                      0,
                                      this->cursorPositionAscii);
                              },
                              .cursor_position = &this->cursorPositionAscii});

    this->inputAscii = Maybe(this->inputAscii, [this]
                             { return this->mode == 0; });

    /* 初始化Hex框 */
    this->inputHex = Input({.content = &this->inputDataHex,
                            .placeholder = "在此输入发送的数值\n只能输入0123456789aAbBcCdDeEfF",
                            .multiline = true,
                            .on_change = [this]
                            {
                                SanitizeHexInput(
                                    this->inputDataHex,
                                    this->cursorPositionHex);
                                RebuildInputLineIndex(
                                    this->inputDataHex,
                                    this->searchTableHex,
                                    this->width,
                                    this->cursorPositionHex);
                            },
                            .cursor_position = &this->cursorPositionHex});

    this->inputHex = Maybe(this->inputHex, [this]
                           { return this->mode == 1; });

    this->inputGroup = Container::Vertical({this->inputAscii,
                                            this->inputHex});

    // 准备子组建RadioBox-0
    this->radio0 = Make<windowRadioBox>(
        this->radiobox_Mode,
        this->mode);

    // 准备子组建button-0
    this->button = Button(
        "清空",
        [this]
        {
            if (this->mode == 0)
            {
                this->inputDataAscii.clear();
                this->cursorPositionAscii = 0;
                this->searchTableAscii = {0};

                UpdateInputDisplayInformation(
                    this->searchTableAscii,
                    this->cursorPositionAscii,
                    this->dataHeight,
                    this->heightAscii,
                    this->cursorHeightAscii,
                    this->firstDisplayHeightAscii);

                this->barDataHeight = this->heightAscii;
                this->barDataDisplayLineHeight =
                    this->firstDisplayHeightAscii;
            }
            else
            {
                this->inputDataHex.clear();
                this->cursorPositionHex = 0;
                this->searchTableHex = {0};

                UpdateInputDisplayInformation(
                    this->searchTableHex,
                    this->cursorPositionHex,
                    this->dataHeight,
                    this->heightHex,
                    this->cursorHeightHex,
                    this->firstDisplayHeightHex);

                this->barDataHeight = this->heightHex;
                this->barDataDisplayLineHeight =
                    this->firstDisplayHeightHex;
            }
        });

    this->sendButton = Button(
        "发送",
        [this]
        {
            if (!this->sendCallback)
            {
                return;
            }

            const bool isHex = this->mode == 1;
            std::string data;
            if (isHex)
            {
                data = HexStringToBytes(this->inputDataHex);
            }
            else
            {
                data = this->inputDataAscii;
            }
            bool ret = this->sendCallback(data, isHex);
            if (ret)
            {
                if (isHex)
                {
                    this->inputDataHex.clear();
                    this->cursorPositionHex = 0;
                    this->searchTableHex = {0};

                    UpdateInputDisplayInformation(
                        this->searchTableHex,
                        this->cursorPositionHex,
                        this->dataHeight,
                        this->heightHex,
                        this->cursorHeightHex,
                        this->firstDisplayHeightHex);

                    this->barDataHeight = this->heightHex;
                    this->barDataDisplayLineHeight =
                        this->firstDisplayHeightHex;
                }
                else
                {
                    this->inputDataAscii.clear();
                    this->cursorPositionAscii = 0;
                    this->searchTableAscii = {0};

                    UpdateInputDisplayInformation(
                        this->searchTableAscii,
                        this->cursorPositionAscii,
                        this->dataHeight,
                        this->heightAscii,
                        this->cursorHeightAscii,
                        this->firstDisplayHeightAscii);

                    this->barDataHeight = this->heightAscii;
                    this->barDataDisplayLineHeight =
                        this->firstDisplayHeightAscii;
                }
            }
        });

    this->radioGroup = Container::Vertical({this->radio0,
                                            this->button,
                                            this->sendButton});

    // 准备inputPanel顶层
    Component inputPanel = Renderer(this->inputGroup, [this]
                                    { return window(
                                          text("InPut Line: " + std::to_string(this->barDataDisplayLineHeight + 1) + '/' + std::to_string(this->barDataHeight)) | color(this->currentFocusColor),
                                          frame(this->inputGroup->Render()) | size(WIDTH, EQUAL, this->width) | size(HEIGHT, EQUAL, this->dataHeight)); });

    Component scrollPanel = Renderer([this]
                                     { return window(
                                           text(""),
                                           drawVerticalScrollbar(this->dataHeight, this->barDataHeight, this->barDataDisplayLineHeight, this->dataHeight, this->barColor)); });

    Component controlPanel = Renderer(this->radioGroup, [this]
                                      { return this->radioGroup->Render() | size(HEIGHT, EQUAL, this->dataHeight); });

    this->inputRoot = Container::Horizontal({inputPanel,
                                             scrollPanel,
                                             controlPanel});

    Add(this->inputRoot);
}

Element inputLable::OnRender()
{
    /* 准备数据 */
    if (this->Focused())
    {
        this->currentFocusColor = this->focusColor;
    }
    else
    {
        this->currentFocusColor = Color(255, 255, 255);
    }

    /* 准备宽度数据 */
    Dimensions terminal_size = Terminal::Size();
    this->width = terminal_size.dimx - this->widthMinus - 15;
    if (this->width < 1)
    {
        this->width = 1;
    }
    /* 更新当前输入框的光标行和第一显示行 */
    if (this->mode == 0)
    {
        UpdateInputDisplayInformation(
            this->searchTableAscii,
            this->cursorPositionAscii,
            this->dataHeight,
            this->heightAscii,
            this->cursorHeightAscii,
            this->firstDisplayHeightAscii);

        this->barDataHeight = this->heightAscii;
        this->barDataDisplayLineHeight =
            this->firstDisplayHeightAscii;
    }
    else
    {
        UpdateInputDisplayInformation(
            this->searchTableHex,
            this->cursorPositionHex,
            this->dataHeight,
            this->heightHex,
            this->cursorHeightHex,
            this->firstDisplayHeightHex);

        this->barDataHeight = this->heightHex;
        this->barDataDisplayLineHeight =
            this->firstDisplayHeightHex;
    }

    /* 返回结果 */
    return this->inputRoot->Render();
}
/* 输入事件 */
bool inputLable::OnEvent(Event event)
{
    if (event.is_mouse())
    {
        /* 监控鼠标事件 */
        Mouse mouse = event.mouse();
        if (mouse.button == Mouse::WheelUp)
        {
            this->upper();
            return true;
        }
        else if (mouse.button == Mouse::WheelDown)
        {
            this->lower();
            return true;
        }
    }
    return ComponentBase::OnEvent(event);
}

/* 是否参与焦点 */
bool inputLable::Focusable() const
{
    return ComponentBase::Focusable();
}

void inputLable::upper()
{
    if (this->mode == 0)
    {
        this->cursorPositionAscii = MoveInputCursorVertical(
            this->inputDataAscii,
            this->searchTableAscii,
            this->cursorPositionAscii,
            -1);
    }
    else
    {
        this->cursorPositionHex = MoveInputCursorVertical(
            this->inputDataHex,
            this->searchTableHex,
            this->cursorPositionHex,
            -1);
    }
}

void inputLable::lower()
{
    if (this->mode == 0)
    {
        this->cursorPositionAscii = MoveInputCursorVertical(
            this->inputDataAscii,
            this->searchTableAscii,
            this->cursorPositionAscii,
            1);
    }
    else
    {
        this->cursorPositionHex = MoveInputCursorVertical(
            this->inputDataHex,
            this->searchTableHex,
            this->cursorPositionHex,
            1);
    }
}

void inputLable::setDataHeight(int h)
{
    this->dataHeight = h;
}

void inputLable::setSendCallback(
    std::function<bool(const std::string &, bool)> callback)
{
    this->sendCallback = std::move(callback);
}

/**
 * ******************************************************************************************************
 * @name dropDownWithWindow
 * @brief 带有window的下拉框
 * ******************************************************************************************************
 */
dropDownWithWindow::dropDownWithWindow(
    ConstStringListRef list,
    int *selected,
    Color *cor,
    std::string windowName,
    std::mutex *listMutex)
{
    this->focusColor = cor;
    this->windowName = windowName;
    this->list = list;
    this->selected = selected;
    this->listMutex = listMutex;
    this->dropDown = Dropdown(this->list, this->selected);

    Add(this->dropDown);
}
/* 界面渲染函数重写 */
Element dropDownWithWindow::OnRender()
{
    std::unique_lock<std::mutex> lock;
    if (this->listMutex != nullptr)
    {
        lock = std::unique_lock<std::mutex>(*this->listMutex);
    }

    std::string str = this->windowName + " [" + std::to_string(*(this->selected) + 1) + "/" + std::to_string(this->list.size()) + "]";
    Color current(255, 255, 255);
    if (this->Focused())
    {
        current = *(this->focusColor);
    }
    return window(text(str) | color(current), this->dropDown->Render());
}

/* 输入事件 */
bool dropDownWithWindow::OnEvent(Event event)
{
    std::unique_lock<std::mutex> lock;
    if (this->listMutex != nullptr)
    {
        lock = std::unique_lock<std::mutex>(*this->listMutex);
    }

    return ComponentBase::OnEvent(event);
}

/* 是否参与焦点 */
bool dropDownWithWindow::Focusable() const
{
    return ComponentBase::Focusable();
}

void dropDownWithWindow::setWindowName(std::string windowName)
{
    this->windowName = windowName;
}

/**
 * ******************************************************************************************************
 * @name serialSelectMenu
 * @brief 显示串口的菜单组件
 * ******************************************************************************************************
 */

serialSelectMenu::serialSelectMenu(Color cor)
{
    /* 初始化数据 */
    this->focusColor = cor;
    this->portNameStr = getSerialPortList();

    /* 初始化组建*/
    this->portName = Make<dropDownWithWindow>(
        &(this->portNameStr),
        &(this->portNameSelected),
        &(this->focusColor),
        "串口选择",
        &this->portNameMutex);
    this->portBaudRate = Make<dropDownWithWindow>(&(this->portBaudRateStr), &(this->portBaudRateSelected), &(this->focusColor), "波特率");
    this->portDataBits = Make<dropDownWithWindow>(&(this->portDataBitsStr), &(this->portDataBitsSelected), &(this->focusColor), "数据位");
    this->portParityBit = Make<dropDownWithWindow>(&(this->portParityBitStr), &(this->portParityBitSelected), &(this->focusColor), "校验位");
    this->portStopBit = Make<dropDownWithWindow>(&(this->portStopBitStr), &(this->portStopBitSelected), &(this->focusColor), "停止位");
    this->openButton = Button("打开串口", [this]
                              {
        /* 打开串口逻辑 */
        /* 准备数据 */    
        const int baudTable[]={9600,19200,38400,57600,115200,230400,460800,921600};
        const int dataTable[]={8,7,6,5};
        const sp_parity parityTable[]={SP_PARITY_NONE,SP_PARITY_ODD,SP_PARITY_EVEN,SP_PARITY_MARK,SP_PARITY_SPACE};
        const int stopTable[]={1,3,2};
        
        this->port.setBaudRate(baudTable[this->portBaudRateSelected]);
        this->port.setDataBits(dataTable[this->portDataBitsSelected]);
        this->port.setParityBit(parityTable[this->portParityBitSelected]);
        this->port.setStopBit(stopTable[this->portStopBitSelected]);

        std::string selectedPort;
        {
            std::lock_guard<std::mutex> lock(this->portNameMutex);
            if (this->portNameStr.empty())
            {
                return;
            }

            if (this->portNameSelected < 0 ||
                this->portNameSelected >=
                    static_cast<int>(this->portNameStr.size()))
            {
                this->portNameSelected =
                    static_cast<int>(this->portNameStr.size()) - 1;
            }
            selectedPort = this->portNameStr[this->portNameSelected];
        }

        this->port.setPortName(selectedPort);

        bool ret=this->port.open(SP_MODE_READ_WRITE);
        if(!ret){
            return;
        } });
    this->closeButton = Button("关闭串口", [this]
                               {
        /* 关闭串口逻辑 */
        bool ret=this->port.close();
        if(!ret){
            return;
        } });
    this->openButton = Maybe(this->openButton, [this]
                             { return this->port.getStatus() == 0; });
    this->closeButton = Maybe(this->closeButton, [this]
                              { return this->port.getStatus() == 1; });
    this->line1 = Container::Horizontal({this->portBaudRate,
                                         this->portDataBits,
                                         this->portParityBit,
                                         this->portStopBit});

    this->line2 = Container::Horizontal({this->portName,
                                         this->openButton,
                                         this->closeButton});

    this->root = Container::Vertical({this->line1, this->line2});

    Add(this->root);
}

/* 界面渲染函数重写 */
Element serialSelectMenu::OnRender()
{
    Dimensions terminal_size = Terminal::Size();
    int contentWidth = terminal_size.dimx - 2;
    if (contentWidth < 4)
    {
        contentWidth = 4;
    }

    /* 四个dropdown等宽，余数分摊到前几列，避免末尾出现空隙 */
    const int baseWidth = contentWidth / 4;
    const int remainder = contentWidth % 4;
    auto columnWidth = [baseWidth, remainder](int index)
    {
        return baseWidth + (index < remainder ? 1 : 0);
    };

    Element line1View = hbox({
        this->portBaudRate->Render() |
            size(WIDTH, EQUAL, columnWidth(0)),
        this->portDataBits->Render() |
            size(WIDTH, EQUAL, columnWidth(1)),
        this->portParityBit->Render() |
            size(WIDTH, EQUAL, columnWidth(2)),
        this->portStopBit->Render() |
            size(WIDTH, EQUAL, columnWidth(3)),
    });

    /* 串口名占据剩余空间，按钮保持自然宽度并靠右 */
    Element line2View = hbox({
        this->portName->Render() | flex,
        this->openButton->Render(),
        this->closeButton->Render(),
    });

    return vbox({line1View, line2View}) |
           size(WIDTH, EQUAL, contentWidth);
}

/* 输入事件 */
bool serialSelectMenu::OnEvent(Event event)
{
    return ComponentBase::OnEvent(event);
}

/* 是否参与焦点 */
bool serialSelectMenu::Focusable() const
{
    return ComponentBase::Focusable();
}

void serialSelectMenu::RefreshSerial()
{
    std::vector<std::string> newPorts = getSerialPortList();

    std::lock_guard<std::mutex> lock(this->portNameMutex);

    std::string selectedPort;
    if (!this->portNameStr.empty() &&
        this->portNameSelected >= 0 &&
        this->portNameSelected <
            static_cast<int>(this->portNameStr.size()))
    {
        selectedPort = this->portNameStr[this->portNameSelected];
    }

    this->portNameStr = std::move(newPorts);

    if (this->portNameStr.empty())
    {
        this->portNameSelected = 0;
        return;
    }

    if (!selectedPort.empty())
    {
        for (std::size_t i = 0; i < this->portNameStr.size(); ++i)
        {
            if (this->portNameStr[i] == selectedPort)
            {
                this->portNameSelected = static_cast<int>(i);
                return;
            }
        }
    }

    if (this->portNameSelected < 0)
    {
        this->portNameSelected = 0;
    }
    else if (this->portNameSelected >=
             static_cast<int>(this->portNameStr.size()))
    {
        this->portNameSelected =
            static_cast<int>(this->portNameStr.size()) - 1;
    }
}

std::string serialSelectMenu::tryReadOnce()
{
    if (!this->port.getStatus())
    {
        return {};
    }

    return this->port.readString(4096);
}

bool serialSelectMenu::tryWriteOnce(std::string str)
{
    if (!this->port.getStatus())
    {
        return false;
    }
    return this->port.writeString(str);
}
