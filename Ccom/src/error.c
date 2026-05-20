#include "error.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <stdint.h>

void checkError(ERROR e, const char* str)
{
    uint8_t flag=1;
    if(str==NULL){
        flag=0;
    }
    if (e == passFalse)
    {   
        if(flag){
            printLog("CHECK","%s",str);
        }
    }
    else if (e == passTrue)
    {
        printLog("CHECK","%s",str);
    }
}

void printLog(const char* moduleID, const char* format, ...)
{
    // 获取当前时间
    time_t raw_time;
    struct tm* time_info;
    char time_buffer[20];
    
    time(&raw_time);
    time_info = localtime(&raw_time);
    strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", time_info);

    // 打印时间戳和模块标识
    printf("[%s] [%s] ", time_buffer, moduleID);

    // 处理可变参数，printf 风格格式化输出
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);

    printf("\n");
}